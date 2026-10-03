#include "FileSys.h"

#include "RealMemory.h"
#include "RealPrint.h"
#include "RealSystem.h"
#include "RefPack.h"
#include "XboxXapi.h"

#include <windows.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's file system layer, as the driving engine has it: what lies between a request such as
// FILE_load("data\\sim\\...") and the Win32 file calls. docs/driving/filesys.md describes the original; each
// function here is the original at the same address, ported from it.
//
// Four layers, top down:
//   FILE_*            whole-file helpers (load, size, exists, save, unpack), each one locked sequence of FILESYS
//                     calls run through FILESYS_atomic on the caller's thread;
//   FILESYS sync      submit an op, wait for it, complete it;
//   FILESYS ops       a queue of operation records executed in priority order on one worker thread, with
//                     callbacks on that thread - the sound streamer (STREAM, still the original) drives these
//                     asynchronously;
//   file device       a table of open files ("slots"), and the .viv (BIGF) archives a path may be found in.
//
// The records and globals stay where the original kept them - the block FILESYS_init allocates, and the
// addresses below - so the layer allocates exactly what it did and the rest of the binary (and a dump) finds
// its state where it always was. The queue helpers, the slot functions and the archive lookup have no callers
// outside the layer, several take register arguments, and they die with it, so they are plain functions here.
//
// Latent bugs in the original, fixed (docs/driving/filesys.md section 7 has the full list):
//   - running out of op records crashed; a submit now fails and returns 0, which every caller handles (7.1);
//   - FILESYS_size cleared the worker's running op, so a concurrent wait could return early (7.2);
//   - the done queue's lock was a byte copy of the pending queue's critical section (7.4);
//   - a failed addbig leaked the directory, and an unknown archive format read a negative size (7.8);
//   - unbounded name copies (7.9); an open with no free slot failed with error 0 (7.10);
//   - a read error with no handler, or one that declines, hung the worker; it now reports the error.
// The subfile seek test was wrong too (7.3), but always seeking is what kept shared archive handles correct, so
// subfiles always seek here, which is what the original did in practice.
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 4)

typedef unsigned (*QueueKeyFn)(void *node, int argument);
typedef int (*QueueMatchFn)(void *node, int argument);

struct LockQueue {           // 0x38
    int count;               // +0x00
    unsigned flags;          // +0x04 bit 0 "changed", the foreach stop flag; bit 1 "made by Q_init", never read
    void *head;              // +0x08 a node's first dword is its link
    void *tail;              // +0x0c
    QueueKeyFn keyFn;        // +0x10 sort key, the identity function (0x0014a540) when none was given
    int keyArg;              // +0x14
    RealMutex mutex;         // +0x18
};
static_assert(sizeof(LockQueue) == 0x38, "LockQueue is 0x38 bytes");

struct FsOp {                // 0x30, numOps of them in the FILESYS block
    FsOp *next;              // +0x00 queue link
    unsigned handle;         // +0x04 seq << 5 | worker; 0 while free
    int type;                // +0x08 FsOpType
    unsigned flags;          // +0x0c FsOpFlags
    int8_t status;           // +0x10 0 pending/running, 1 succeeded, -2 failed
    uint8_t priority;        // +0x11 smaller runs first
    uint16_t pad12;
    int error;               // +0x14 GetLastError() after the operation
    int slot;                // +0x18 file slot handle: the input of close/read/write, the output of open
    int userData;            // +0x1c
    FsCallback callback;     // +0x20
    int arg0;                // +0x24
    int arg1;                // +0x28
    int arg2;                // +0x2c
};
static_assert(sizeof(FsOp) == 0x30, "FsOp is 0x30 bytes");

struct FsWorker {            // 0xb8; two in the FILESYS block, only worker 0 ever used
    int alive;               // +0x00 set by the worker thread when it starts
    RealThread thread;       // +0x04
    FsOp *current;           // +0x10 the op being executed
    LockQueue pending;       // +0x14 sorted by FsOpKey
    LockQueue done;          // +0x4c
    RealSignal signal;       // +0x84 "there is work"
    RealMutex atomicMutex;   // +0x8c serialises FILESYS_atomic
    HANDLE doneEvent;        // +0xac manual reset: "an op finished" (and the thread's start handshake)
    unsigned seq;            // +0xb0 24 bits, never 0
    int ceiling;             // +0xb4 the worker only starts ops with priority <= this
};
static_assert(sizeof(FsWorker) == 0xb8, "FsWorker is 0xb8 bytes");

struct BigFile {             // 0x118, FILE_malloc'd "BigFile"
    BigFile *next;           // +0x00 link in the big list
    int id;                  // +0x04 -1, -2, ...
    int unused8;             // +0x08
    int slot;                // +0x0c the open archive
    uint8_t *dir;            // +0x10 its directory, FILE_malloc'd "BF Header"
    int dirSize;             // +0x14
    char name[0x100];        // +0x18 the archive's path as given, matched against the part of a path before '|'
};
static_assert(sizeof(BigFile) == 0x118, "BigFile is 0x118 bytes");

struct FileSlot {            // 16 bytes; a slot handle is ~index
    HANDLE handle;           // +0x00 1 while claimed but not yet open, 0 free
    int pos;                 // +0x04 absolute in the underlying file
    int size;                // +0x08
    int base;                // +0x0c 0 for a loose file, the entry's offset for a file inside an archive
};
static_assert(sizeof(FileSlot) == 16, "FileSlot is 16 bytes");

#pragma pack(pop)

enum FsOpType { OP_OPEN, OP_CLOSE, OP_READ, OP_WRITE, OP_SIZE, OP_NOP5, OP_EXISTS, OP_DELETE, OP_NULL, OP_ADDBIG,
                OP_DELBIG };
enum FsOpFlags { OPF_OWNSNAME = 1, OPF_CANCELLED = 2, OPF_HASCALLBACK = 4, OPF_CALLEDBACK = 8 };
enum FsStatus { ST_PENDING = 0, ST_OK = 1, ST_FAILED = -2, ST_CANCELLED = -1, ST_UNKNOWN = -3 };
enum FsMode { MODE_READ = 1, MODE_CREATE = 2, MODE_TRUNCATE = 4 };

#define SyncPriority       (*(int *)0x001d1c00u)            // 100: every FILE_ helper's
#define pFILE_malloc       (*(FileMallocFn *)0x001d1c0cu)
#define pFILE_mfree        (*(FileFreeFn *)0x001d1c10u)
#define ReadErrorHandler   (*(FsReadErrorFn *)0x001d1c14u)
#define FileRoot           (*(const char **)0x001d1c28u)    // "D:"
#define AsyncMutex         ((RealMutex *)0x00242838u)
#define AsyncGeneration    (*(unsigned *)0x00242858u)
#define AsyncFreeHead      (*(AsyncRequest **)0x0024285cu)
#define AsyncFreeTail      (*(AsyncRequest **)0x00242860u)
#define AsyncCount         (*(int *)0x00242864u)
#define AsyncRequests      (*(AsyncRequest **)0x00242868u)
#define FsWorkers          (*(FsWorker **)0x00242870u)      // non-zero once initialised
#define FreeOps            ((LockQueue *)0x00242874u)
#define BigIdMutex         ((RealMutex *)0x002428acu)
#define OpRecords          (*(FsOp **)0x002428ccu)          // written once, read by nothing
#define NextBigId          (*(int *)0x002428d0u)
#define BigFiles           ((LockQueue *)0x002428d4u)
#define WorkerExit         (*(volatile int *)0x0024290cu)  // only ever 0: the worker never leaves
#define SubDirectory       ((const char *)0x00242910u)      // never written: always empty
#define SlotMutex          ((RealMutex *)0x00242a10u)
#define SlotCount          (*(int *)0x00242a30u)
#define Slots              (*(FileSlot **)0x00242a34u)


static const int kChunk = 0x2000;   // a sync read or write goes as ops of this size

static int BlockBytes(int numFiles, int numOps) {
    return (numOps * 3 + 0x17 + numFiles) * 16;
}

// ---- the locked queue (0x0014a540..0x0014aa0f)

// The default sort key: the node's address. The linker folded every "return the argument" into this one, so
// render code (0x000a2fc0) and two vtables (0x001d4840, 0x001d4870) call it too.
// FUNC_AT(0x0014a540)
unsigned QueueKeyIdentity(void *node) {
    return (unsigned)node;
}

static int Q_unlink(LockQueue *q, void *node) {             // 0x0014a550 (ECX = q, ESI = node); caller locks
    if (node == NULL || q->count == 0)
        return 0;
    if (node == q->head) {
        q->count--;
        if (node == q->tail)
            q->head = q->tail = NULL;
        else
            q->head = *(void **)node;
    } else {
        void *previous = q->head;
        while (*(void **)previous != NULL && *(void **)previous != node)
            previous = *(void **)previous;
        if (*(void **)previous != node)
            return 0;
        q->count--;
        *(void **)previous = *(void **)node;
        if (node == q->tail)
            q->tail = previous;
    }
    *(void **)node = NULL;
    q->flags |= 1;
    return 1;
}

static void Q_init(LockQueue *q, QueueKeyFn keyFn, int keyArg) {   // 0x0014a5f0
    MUTEX_create(&q->mutex);
    q->count = 0;
    q->flags = 2;
    q->head = q->tail = NULL;
    q->keyArg = keyArg;
    q->keyFn = keyFn != NULL ? keyFn : (QueueKeyFn)QueueKeyIdentity;
}

static void Q_lock(LockQueue *q) { MUTEX_lock(&q->mutex); }         // 0x0014a9e0
static void Q_unlock(LockQueue *q) { MUTEX_unlock(&q->mutex); }     // 0x0014aa00

static void Q_pushfront(LockQueue *q, void *node) {                 // 0x0014a690
    Q_lock(q);
    if (node != NULL) {
        *(void **)node = q->head;
        q->head = node;
        q->count++;
        if (*(void **)node == NULL)
            q->tail = node;
        q->flags |= 1;
    }
    Q_unlock(q);
}

static void Q_pushback(LockQueue *q, void *node) {                  // 0x0014a6d0
    Q_lock(q);
    if (node != NULL) {
        *(void **)node = NULL;
        q->count++;
        void *old = q->tail;
        q->tail = node;
        if (old != NULL)
            *(void **)old = node;
        else
            q->head = node;
        q->flags |= 1;
    }
    Q_unlock(q);
}

static void *Q_popfront(LockQueue *q) {                             // 0x0014a720
    Q_lock(q);
    void *node = q->head;
    if (node != NULL) {
        if (node == q->tail)
            q->head = q->tail = NULL;
        else
            q->head = *(void **)node;
        q->count--;
        *(void **)node = NULL;
    }
    q->flags |= 1;
    Q_unlock(q);
    return node;
}

// Ascending key, before the first node with an equal or greater one.
static void Q_insertsorted(LockQueue *q, void *node) {              // 0x0014a770
    Q_lock(q);
    if (node != NULL) {
        unsigned key = q->keyFn(node, q->keyArg);
        q->count++;
        void *previous = NULL, *current = q->head;
        while (current != NULL && q->keyFn(current, q->keyArg) < key) {
            previous = current;
            current = *(void **)current;
        }
        *(void **)node = current;
        if (previous != NULL)
            *(void **)previous = node;
        else
            q->head = node;
        if (current == NULL)
            q->tail = node;
        q->flags |= 1;
    }
    Q_unlock(q);
}

static int Q_remove(LockQueue *q, void *node) {                     // 0x0014a800
    Q_lock(q);
    int removed = node != NULL ? Q_unlink(q, node) : 0;
    Q_unlock(q);
    return removed;
}

static void *Q_find(LockQueue *q, QueueMatchFn match, int argument) {   // 0x0014a840
    Q_lock(q);
    void *node = q->head;
    while (node != NULL && match != NULL && !match(node, argument))
        node = *(void **)node;
    Q_unlock(q);
    return node;
}

static void *Q_findremove(LockQueue *q, QueueMatchFn match, int argument) {   // 0x0014a890
    Q_lock(q);
    void *node = q->head;
    while (node != NULL && match != NULL && !match(node, argument))
        node = *(void **)node;
    if (node != NULL && !Q_unlink(q, node))
        node = NULL;
    Q_unlock(q);
    return node;
}

// Walks the queue until fn answers 0 or the queue changes; -1 if it ran to the end, else how many it visited.
static int Q_foreach(LockQueue *q, QueueMatchFn fn, int argument) {   // 0x0014a910
    int count = 0;
    Q_lock(q);
    unsigned saved = q->flags & 1;
    q->flags &= ~1u;
    Q_unlock(q);
    for (void *node = q->head; node != NULL && !(q->flags & 1); node = *(void **)node, count++) {
        if (!fn(node, argument))
            q->flags |= 1;
    }
    Q_lock(q);
    if (!(q->flags & 1))
        count = -1;
    q->flags |= saved;
    Q_unlock(q);
    return count;
}

static int Q_foreachlocked(LockQueue *q, QueueMatchFn fn, int argument) {   // 0x0014a9b0
    Q_lock(q);
    int count = Q_foreach(q, fn, argument);
    Q_unlock(q);
    return count;
}

// ---- the file device: a table of open files

static FileSlot *SlotOf(int slot) {
    if (slot >= 0 || ~slot >= SlotCount || Slots == NULL || Slots[~slot].handle == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return &Slots[~slot];
}

static void FILEDEV_init(int count, FileSlot *table) {              // 0x0010d1f0
    MUTEX_create(SlotMutex);
    Slots = table;
    SlotCount = count;
}

// The game installs the disc-error screen (0x0005c960); it returns 1, "try again".
// FUNC_AT(0x0010d840)
void FILEDEV_setreaderror(FsReadErrorFn handler) {
    ReadErrorHandler = handler;
}

static int FILEDEV_claim() {                                        // 0x0010d220
    MUTEX_lock(SlotMutex);
    for (int i = 0; i < SlotCount; i++) {
        if (Slots[i].handle == NULL) {
            Slots[i].handle = (HANDLE)1;
            MUTEX_unlock(SlotMutex);
            return i;
        }
    }
    MUTEX_unlock(SlotMutex);
    return -1;
}

// A file inside an archive: a slot sharing the archive's handle, its own base, size and position.
static bool FILEDEV_opensub(int archiveSlot, int base, int size, int *out) {   // 0x0010d290
    int i = FILEDEV_claim();
    *out = 0;
    if (i < 0) {
        SetLastError(ERROR_TOO_MANY_OPEN_FILES);
        return false;
    }
    Slots[i].handle = Slots[~archiveSlot].handle;
    Slots[i].pos = 0;
    Slots[i].size = size;
    Slots[i].base = base;
    *out = ~i;
    return true;
}

// The game's path relative to the disc root ("driving\\misc.viv" is "D:\\driving\\misc.viv"), or as it is if it
// already names a drive.
static void FullPath(char *full, size_t size, const char *path) {
    if (path[0] != 0 && path[1] == ':')
        snprintf(full, size, "%s", path);
    else if (SubDirectory[0] == 0)
        snprintf(full, size, "%s\\%s", FileRoot, path);
    else
        snprintf(full, size, "%s\\%s\\%s", FileRoot, SubDirectory, path);
}

static bool FILEDEV_open(const char *path, int mode, int *out) {   // 0x0010d2f0
    int i = FILEDEV_claim();
    *out = 0;
    SetLastError(0);
    if (i < 0) {
        SetLastError(ERROR_TOO_MANY_OPEN_FILES);
        return false;
    }
    DWORD access = (mode & MODE_READ) ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
    DWORD share = (mode & MODE_READ) ? FILE_SHARE_READ : 0;
    DWORD disposition = (mode & MODE_CREATE) ? ((mode & MODE_TRUNCATE) ? CREATE_ALWAYS : CREATE_NEW)
                                             : ((mode & MODE_TRUNCATE) ? TRUNCATE_EXISTING : OPEN_EXISTING);
    char full[256];
    FullPath(full, sizeof(full), path);
    for (char *p = full; *p != 0; p++) {
        if (*p == '/')
            *p = '\\';
    }
    HANDLE handle = Xbox_CreateFileA(full, access, share, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        Slots[i].handle = NULL;
        return false;
    }
    Slots[i].handle = handle;
    Slots[i].pos = 0;
    Slots[i].base = 0;
    Slots[i].size = (int)GetFileSize(handle, NULL);
    *out = ~i;
    return true;
}

static int FILEDEV_getsize(int slot) {                              // 0x0010d460
    FileSlot *s = SlotOf(slot);
    return s != NULL ? s->size : 0;
}

static int Clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

// Reads at 'offset' into the (sub)file, no further than its end. A subfile always seeks: it shares its
// archive's handle with every other file open in that archive.
static int FILEDEV_read(int slot, void *buffer, int offset, int count) {   // 0x0010d4a0
    FileSlot *s = SlotOf(slot);
    if (s == NULL)
        return 0;
    SetLastError(0);
    int start = s->base, end = s->base + s->size;
    if (s->base != 0 || s->pos != offset) {
        s->pos = Clamp(s->base + offset, start, end);
        s->pos = (int)SetFilePointer(s->handle, s->pos, NULL, FILE_BEGIN);
    }
    if ((int64_t)s->pos + count > end)
        count = end - s->pos;
    DWORD got = 0;
    if (count > 0 && !ReadFile(s->handle, buffer, (DWORD)count, &got, NULL)) {
        DWORD error = GetLastError();
        if (ReadErrorHandler == NULL || !ReadErrorHandler())
            printf("FILESYS: read error %lu\n", error);   // the original slept forever here
        SetLastError(error != 0 ? error : ERROR_READ_FAULT);
    }
    s->pos += (int)got;
    if (s->pos > end)
        s->pos = end;
    return (int)got;
}

static int FILEDEV_write(int slot, const void *buffer, int offset, int count) {   // 0x0010d680; loose files only
    FileSlot *s = SlotOf(slot);
    if (s == NULL)
        return 0;
    SetLastError(0);
    if (s->pos != offset) {
        s->pos = Clamp(offset, 0, s->size);
        s->pos = (int)SetFilePointer(s->handle, s->pos, NULL, FILE_BEGIN);
    }
    DWORD put = 0;
    WriteFile(s->handle, buffer, (DWORD)count, &put, NULL);
    s->pos += (int)put;
    if (s->pos > s->size)
        s->size = s->pos;
    return (int)put;
}

// A subfile's handle is its archive's, so only a loose file's is closed.
static bool FILEDEV_close(int slot) {                              // 0x0010d760
    FileSlot *s = SlotOf(slot);
    if (s == NULL)
        return false;
    SetLastError(0);
    if (s->base == 0 && s->handle != INVALID_HANDLE_VALUE)
        CloseHandle(s->handle);
    memset(s, 0, sizeof(*s));
    return true;
}

static BOOL FILEDEV_delete(const char *path) {                     // 0x0010d7d0
    char full[256];
    SetLastError(0);
    FullPath(full, sizeof(full), path);
    return Xbox_DeleteFileA(full);
}

// ---- big (.viv) archive directories: big-endian, entries of offset, size and a NUL-terminated name

static unsigned ReadBE(const uint8_t *bytes, int width) {           // 0x0010dc90 (EAX = bytes, ECX = width)
    if (width < 1 || width > 4)
        return 0;
    unsigned value = 0;
    for (int i = 0; i < width; i++)
        value = value << 8 | bytes[i];
    return value;
}

// Wider than four bytes: only the low dword, the last four bytes, is kept.
static unsigned ReadBEWide(const uint8_t *bytes, int width) {      // 0x0010dd30 (EAX = width, EDI = bytes)
    return width > 4 ? ReadBE(bytes + width - 4, 4) : ReadBE(bytes, width);
}

static int BIG_kind(const uint8_t *header) {                      // 0x0010dd80
    if (header[0] == 0xc0 && header[1] == 0xfb)
        return 1;
    if (header[0] == 'B' && header[1] == 'I' && header[2] == 'G')
        return header[3] == 'F' ? 2 : 3;
    return 0;
}

int BIG_dirsize(const uint8_t *header) {                           // 0x0010ddf0
    switch (BIG_kind(header)) {
    case 1:
        return (int)ReadBE(header + 2, 2) + 4;
    case 2:
    case 3:
        return (int)ReadBE(header + 0xc, 4);
    }
    return 0;
}

static int BIG_stricmp(const char *a, const char *b) {             // 0x0010dce0 (ECX = a, EAX = b)
    for (;; a++, b++) {
        int x = toupper((unsigned char)*a), y = toupper((unsigned char)*b);
        if (x != y || x == 0)
            return x - y;
    }
}

// The entry called 'name' (case-insensitive), or with no name the index'th; its name, or NULL.
const char *BIG_find(const uint8_t *dir, const char *name, int index, int *outOffset, int *outSize) {   // 0x0010de40
    int size = BIG_dirsize(dir);
    const uint8_t *t = dir + size - 8;
    int footer = 0;
    if (((t[0] | 0x20) >= 'a' && (t[0] | 0x20) <= 'z') && t[1] >= '0' && t[1] <= '9' && t[2] >= '0' &&
        t[2] <= '9' && t[3] >= '0' && t[3] <= '9')
        footer = 8;   // an EA "L231"-style footer
    int offsetWidth = 4, sizeWidth = 4;
    const uint8_t *p = dir + 0x10;
    const uint8_t *end = dir + size - footer;
    switch (BIG_kind(dir)) {
    case 1:
        offsetWidth = sizeWidth = 3;
        p = dir + 6;
        break;
    case 3:
        offsetWidth = dir[3] - '0';
        sizeWidth = offsetWidth < 4 ? offsetWidth : 4;
        break;
    }
    for (int n = 0; p < end; n++) {
        const char *entry = (const char *)p + offsetWidth + sizeWidth;
        if (name != NULL ? BIG_stricmp(entry, name) == 0 : n == index) {
            if (outOffset != NULL)
                *outOffset = (int)ReadBEWide(p, offsetWidth);
            if (outSize != NULL)
                *outSize = (int)ReadBE(p + offsetWidth, sizeWidth);
            return entry;
        }
        p = (const uint8_t *)entry + strlen(entry) + 1;
    }
    if (outOffset != NULL)
        *outOffset = 0;
    if (outSize != NULL)
        *outSize = 0;
    return NULL;
}

// ---- resolving a path: "a.viv|x" (in that archive), "|x" (in any), or "x" (on disc, then in any for reading)

struct ResolveCtx {
    const char *bigName;     // "" for any archive
    const char *sub;
    int slot;
    int found;
};

static int FILEDEV_bigmatch(void *node, int argument) {            // 0x0010bf60; 0 stops the walk
    BigFile *big = (BigFile *)node;
    ResolveCtx *c = (ResolveCtx *)argument;
    bool anyBig = c->bigName[0] == 0;
    if (!anyBig && strcmp(big->name, c->bigName) != 0)
        return 1;
    int offset, size;
    if (BIG_find(big->dir, c->sub, 0, &offset, &size) != NULL) {
        c->found = FILEDEV_opensub(big->slot, offset, size, &c->slot);
        return 0;
    }
    return anyBig;
}

static int FILEDEV_resolveopen(const char *path, int mode, int searchBig) {   // 0x0010c070 (ESI = path)
    char name[256];
    ResolveCtx c = { name, path, 0, 0 };
    const char *bar = strchr(path, '|');
    if (bar != NULL) {
        size_t length = (size_t)(bar - path);
        if (length >= sizeof(name))
            length = sizeof(name) - 1;
        memcpy(name, path, length);
        name[length] = 0;
        c.sub = bar + 1;
    } else {
        c.found = FILEDEV_open(path, mode, &c.slot);
        name[0] = 0;
    }
    if (searchBig && !c.found)
        Q_foreachlocked(BigFiles, FILEDEV_bigmatch, (int)&c);
    return c.slot;
}

// ---- initialisation

// AUTOINJECT
void FILESYS_setmemcallbacks(FileMallocFn allocate, FileFreeFn release) {
    pFILE_malloc = allocate;
    pFILE_mfree = release;
}

// cdBuffer is the PS2's CD buffer size, unused. False if already initialised (the original's AL).
// AUTOINJECT
bool FILESYS_init(int numFiles, int cdBuffer, int numOps) {
    if (FsWorkers != NULL)
        return false;
    int files = numFiles != 0 ? numFiles : 0x10, ops = numOps != 0 ? numOps : 0x20;
    void *block = pFILE_malloc("File Sys", BlockBytes(files, ops), 0x100);
    return FILESYS_initadr(numFiles, cdBuffer, numOps, block);
}

// The block: the slot table, two worker records, then the op records, all on the free list.
// AUTOINJECT
bool FILESYS_initadr(int numFiles, int cdBuffer, int numOps, void *block) {
    (void)cdBuffer;
    if (FsWorkers == NULL) {
        WorkerExit = 0;
        int files = numFiles != 0 ? numFiles : 0x10, ops = numOps != 0 ? numOps : 0x20;
        MEM_fill(block, 0, BlockBytes(files, ops));
        FILEDEV_init(files, (FileSlot *)block);
        Q_init(FreeOps, NULL, 0);
        Q_init(BigFiles, NULL, 0);
        FsWorkers = (FsWorker *)((uint8_t *)block + files * 16);
        FsOp *op = OpRecords = (FsOp *)((uint8_t *)FsWorkers + 2 * sizeof(FsWorker));
        for (int i = 0; i < ops; i++, op++)
            Q_pushfront(FreeOps, op);
        MUTEX_create(BigIdMutex);
        NextBigId = -1;
    }
    return true;
}

// ---- the worker

static unsigned FsOpKey(void *node, int unused) {                  // 0x0010c030: priority, then sequence
    (void)unused;
    FsOp *op = (FsOp *)node;
    return (op->handle >> 5 & 0xffffff) | (unsigned)op->priority << 24;
}

static int FsOpHandleIs(void *node, int handle) {                  // 0x0010c010
    return ((FsOp *)node)->handle == (unsigned)handle;
}

static int BigIdIs(void *node, int id) {                           // 0x0010c050
    return ((BigFile *)node)->id == id;
}

static FsWorker *WorkerOf(unsigned handle) {
    return &FsWorkers[handle & 0x1f];
}

static void FsWorkerThread(void *parameter);

static void FILESYS_startworker(int index) {                       // 0x0010cae0 (EBX = index)
    FsWorker *w = &FsWorkers[index];
    if (w->alive)
        return;
    Q_lock(FreeOps);
    w->current = NULL;
    Q_init(&w->pending, FsOpKey, 0);
    Q_init(&w->done, NULL, 0);   // the original copied the pending queue's lock
    SIGNAL_create(&w->signal);
    w->doneEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    MUTEX_create(&w->atomicMutex);
    w->seq = 1;
    w->ceiling = 0xff;
    if (THREAD_createparam(&w->thread, FsWorkerThread, (void *)(intptr_t)index, 0, 0, 1)) {
        WaitForSingleObject(w->doneEvent, INFINITE);
        ResetEvent(w->doneEvent);
    }
    Q_unlock(FreeOps);
}

// A record off the free list, numbered; NULL when all are in use (the original crashed).
static FsOp *FILESYS_allocop(int type, int priority, int userData, int index) {   // 0x0010cbf0
    FsOp *op = (FsOp *)Q_popfront(FreeOps);
    if (op == NULL) {
        printf("FILESYS: out of operation records\n");
        return NULL;
    }
    FsWorker *w = &FsWorkers[index];
    if (!w->alive)
        FILESYS_startworker(index);
    op->priority = (uint8_t)priority;
    op->type = type;
    op->flags &= ~0xfu;
    op->status = ST_PENDING;
    op->error = 0;
    op->slot = 0;
    op->userData = userData;
    op->callback = NULL;
    op->arg0 = op->arg1 = op->arg2 = 0;
    if (w->alive)
        Q_lock(&w->pending);
    op->handle = w->seq << 5 | (unsigned)index;
    w->seq = (w->seq + 1) & 0xffffff;
    if (w->seq == 0)
        w->seq = 1;
    if (w->alive)
        Q_unlock(&w->pending);
    return op;
}

static void Fail(FsOp *op) {
    op->status = ST_FAILED;
    op->error = (int)GetLastError();
}

static void Execute(FsOp *op) {
    switch (op->type) {
    case OP_OPEN:
        op->slot = FILEDEV_resolveopen((const char *)op->arg2, op->arg0, op->arg0 & MODE_READ);
        if (op->slot != 0)
            op->status = ST_OK;
        else
            Fail(op);
        break;
    case OP_CLOSE:   // the close itself happens in FILESYS_completeop
        op->status = ST_OK;
        break;
    case OP_READ:
    case OP_WRITE:
        op->arg1 = op->type == OP_READ ? FILEDEV_read(op->slot, (void *)op->arg2, op->arg0, op->arg1)
                                       : FILEDEV_write(op->slot, (const void *)op->arg2, op->arg0, op->arg1);
        op->status = ST_OK;
        op->error = (int)GetLastError();
        if (op->error != 0)
            op->status = ST_FAILED;
        break;
    case OP_SIZE:
        if (op->slot == 0) {
            op->arg0 = 0;
            Fail(op);
        } else {
            op->arg0 = FILEDEV_getsize(op->slot);
            op->status = ST_OK;
        }
        break;
    case OP_NOP5:
    case OP_NULL:
        op->status = ST_OK;
        op->error = 0;
        break;
    case OP_EXISTS: {   // "not there" is a success with 0
        int slot = FILEDEV_resolveopen((const char *)op->arg2, MODE_READ, 1);
        op->slot = slot;
        if (slot != 0) {
            FILEDEV_close(slot);
            op->arg0 = 1;
        } else {
            op->arg0 = 0;
        }
        op->status = ST_OK;
        break;
    }
    case OP_DELETE:
        if (FILEDEV_delete((const char *)op->arg2))
            op->status = ST_OK;
        else
            Fail(op);
        break;
    case OP_ADDBIG: {
        BigFile *big = (BigFile *)op->arg2;
        if (big == NULL)
            break;
        uint8_t header[16];
        big->slot = FILEDEV_resolveopen(big->name, MODE_READ, 1);
        if (big->slot == 0 || FILEDEV_read(big->slot, header, 0, 16) != 16) {
            Fail(op);
            break;
        }
        big->dirSize = BIG_dirsize(header);
        if (big->dirSize < 16) {
            SetLastError(ERROR_BAD_FORMAT);
            Fail(op);
            break;
        }
        big->dir = (uint8_t *)pFILE_malloc("BF Header", big->dirSize, 0);
        if (big->dir == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            Fail(op);
            break;
        }
        MEM_copy(big->dir, header, 16);
        FILEDEV_read(big->slot, big->dir + 16, 16, big->dirSize - 16);
        op->status = ST_OK;
        break;
    }
    case OP_DELBIG:
        Q_remove(BigFiles, (void *)op->arg2);
        op->status = ST_OK;
        break;
    }
}

FsTraceFn FsTrace = NULL;

static void FsWorkerThread(void *parameter) {                      // 0x0010c110
    FsWorker *w = &FsWorkers[(intptr_t)parameter];
    w->alive = 1;
    SetEvent(w->doneEvent);
    while (!WorkerExit) {
        Q_lock(&w->pending);
        FsOp *op = w->current = (FsOp *)Q_popfront(&w->pending);
        if (op != NULL && op->priority > w->ceiling) {   // FILESYS_atomic is holding back this priority
            Q_insertsorted(&w->pending, op);
            op = w->current = NULL;
        }
        Q_unlock(&w->pending);
        if (op == NULL) {
            SIGNAL_wait(&w->signal);
            continue;
        }
        if (!(op->flags & OPF_CANCELLED))
            Execute(op);
        if (FsTrace != NULL)
            FsTrace(op->type, op->priority, op->type == OP_ADDBIG ? ((BigFile *)op->arg2)->name
                                            : (op->flags & OPF_OWNSNAME) ? (const char *)op->arg2 : NULL,
                    op->status, op->type == OP_READ || op->type == OP_WRITE ? op->arg1 : op->slot);
        Q_lock(&w->pending);
        Q_pushfront(&w->done, op);
        w->current = NULL;
        Q_unlock(&w->pending);
        if (op->callback != NULL) {
            unsigned flags = op->flags;
            op->flags = flags | OPF_CALLEDBACK;
            op->callback(op->handle, (flags & OPF_CANCELLED) ? ST_CANCELLED : op->status, op->userData);
        }
        SetEvent(w->doneEvent);
    }
    w->alive = 0;
}

static unsigned Submit(FsOp *op) {
    FsWorker *w = WorkerOf(op->handle);
    unsigned handle = op->handle;   // the op may finish and be completed before Submit returns
    Q_insertsorted(&w->pending, op);
    SIGNAL_post(&w->signal);
    return handle;
}

// A copy of a name for an op to own (freed in FILESYS_completeop).
static int CopyName(const char *name) {
    if (name == NULL)
        name = "";
    size_t length = strlen(name) + 1;
    char *copy = (char *)pFILE_malloc("FileName", (int)length, 0);
    memcpy(copy, name, length);
    return (int)copy;
}

// ---- the asynchronous API: each returns the op's handle, or 0

// AUTOINJECT
unsigned FILESYS_open(const char *name, int mode, int priority, int userData) {
    FsOp *op = FILESYS_allocop(OP_OPEN, priority, userData, 0);
    if (op == NULL)
        return 0;
    op->flags |= OPF_OWNSNAME;
    op->arg2 = CopyName(name);
    op->arg0 = mode;
    return Submit(op);
}

// AUTOINJECT
unsigned FILESYS_close(int slot, int priority, int userData) {
    FsOp *op = FILESYS_allocop(OP_CLOSE, priority, userData, 0);
    if (op == NULL)
        return 0;
    op->slot = slot;
    return Submit(op);
}

// AUTOINJECT
unsigned FILESYS_read(int slot, int offset, void *buffer, int count, int priority, int userData) {
    FsOp *op = FILESYS_allocop(OP_READ, priority, userData, 0);
    if (op == NULL)
        return 0;
    op->slot = slot;
    op->arg0 = offset;
    op->arg1 = count;
    op->arg2 = (int)buffer;
    return Submit(op);
}

// Not a Ghidra function (Ghidra runs FILESYS_read into it); only FILESYS_writesync uses it.
unsigned FILESYS_write(int slot, int offset, const void *buffer, int count, int priority, int userData) {   // 0x0010cf20
    FsOp *op = FILESYS_allocop(OP_WRITE, priority, userData, 0);
    if (op == NULL)
        return 0;
    op->slot = slot;
    op->arg0 = offset;
    op->arg1 = count;
    op->arg2 = (int)buffer;
    return Submit(op);
}

// Done here, on the caller's thread, and filed as done at once.
// AUTOINJECT
unsigned FILESYS_size(int slot, int priority, int userData) {
    FsOp *op = FILESYS_allocop(OP_SIZE, priority, userData, 0);
    if (op == NULL)
        return 0;
    FsWorker *w = FsWorkers;
    if (slot == 0) {
        op->arg0 = 0;
        Fail(op);
    } else {
        op->arg0 = FILEDEV_getsize(slot);
        op->status = ST_OK;
    }
    Q_lock(&w->pending);
    Q_pushfront(&w->done, op);   // the original also cleared w->current here, under the running op
    Q_unlock(&w->pending);
    return op->handle;
}

// AUTOINJECT
unsigned FILESYS_exists(const char *name, int priority, int userData) {
    FsOp *op = FILESYS_allocop(OP_EXISTS, priority, userData, 0);
    if (op == NULL)
        return 0;
    op->flags |= OPF_OWNSNAME;
    op->arg2 = CopyName(name);
    return Submit(op);
}

// The archive is opened and its directory read on the worker; it is searched once the op is completed.
// AUTOINJECT
unsigned FILESYS_addbig(const char *name, int allocFlags, int priority, int userData) {
    BigFile *big = (BigFile *)pFILE_malloc("BigFile", sizeof(BigFile), allocFlags);
    if (big == NULL)
        return 0;
    MUTEX_lock(BigIdMutex);
    big->id = NextBigId--;
    MUTEX_unlock(BigIdMutex);
    big->unused8 = 0;
    big->slot = 0;
    big->dir = NULL;
    big->dirSize = 0;
    snprintf(big->name, sizeof(big->name), "%s", name);
    FsOp *op = FILESYS_allocop(OP_ADDBIG, priority, userData, 0);
    if (op == NULL) {
        pFILE_mfree(big);
        return 0;
    }
    op->arg2 = (int)big;
    return Submit(op);
}

// AUTOINJECT
unsigned FILESYS_delbig(int bigId, int priority, int userData) {
    BigFile *big = (BigFile *)Q_find(BigFiles, BigIdIs, bigId);
    if (big == NULL)
        return 0;
    FsOp *op = FILESYS_allocop(OP_DELBIG, priority, userData, 0);
    if (op == NULL)
        return 0;
    op->arg2 = (int)big;
    return Submit(op);
}

// ---- finding, waiting for, completing an op

enum Where { NOWHERE, RUNNING, PENDING, DONE };

// Caller holds w->pending's lock.
static FsOp *Find(FsWorker *w, unsigned handle, Where *where) {
    if (w->current != NULL && w->current->handle == handle) {
        *where = RUNNING;
        return w->current;
    }
    FsOp *op = (FsOp *)Q_find(&w->pending, FsOpHandleIs, (int)handle);
    if (op != NULL) {
        *where = PENDING;
        return op;
    }
    op = (FsOp *)Q_find(&w->done, FsOpHandleIs, (int)handle);
    *where = op != NULL ? DONE : NOWHERE;
    return op;
}

// 0 while pending or running, then the op's status (-1 if cancelled); -3 for an unknown handle.
// AUTOINJECT
int FILESYS_opstatus(unsigned handle) {
    FsWorker *w = WorkerOf(handle);
    if (!w->alive)
        return ST_UNKNOWN;
    Q_lock(&w->pending);
    Where where;
    FsOp *op = Find(w, handle, &where);
    int result = ST_UNKNOWN;
    if (where == RUNNING || where == PENDING)
        result = ST_PENDING;
    else if (where == DONE)
        result = (op->flags & OPF_CANCELLED) ? ST_CANCELLED : op->status;
    Q_unlock(&w->pending);
    return result;
}

// The main thread keeps running the periodic tasks while it waits, as the game relies on during loads.
// AUTOINJECT
int FILESYS_waitop(unsigned handle) {
    FsWorker *w = WorkerOf(handle);
    if (handle == 0 || !w->alive)
        return ST_UNKNOWN;
    for (;;) {
        Q_lock(&w->pending);
        Where where;
        Find(w, handle, &where);
        Q_unlock(&w->pending);
        if (where != RUNNING && where != PENDING)
            break;
        if (THREAD_iscurrent(NULL)) {
            SYNCTASK_run(0);
            THREAD_yield(1);
        } else {
            WaitForSingleObject(w->doneEvent, INFINITE);
            ResetEvent(w->doneEvent);
        }
    }
    return FILESYS_opstatus(handle);
}

// Takes a finished op off the done queue and back to the free list, finishing its work: a close, registering
// or dropping an archive. Answers the type's result.
// AUTOINJECT
int FILESYS_completeop(unsigned handle) {
    FsOp *op = (FsOp *)Q_findremove(&WorkerOf(handle)->done, FsOpHandleIs, (int)handle);
    if (op == NULL) {
        printf("FILESYS: completing op %08x, which is not done\n", handle);
        return 0;
    }
    int result = 0;
    switch (op->type) {
    case OP_OPEN:
        if ((op->flags & OPF_CANCELLED) && op->slot != 0)
            FILEDEV_close(op->slot);
        else
            result = op->slot;
        break;
    case OP_CLOSE:
        result = FILEDEV_close(op->slot);
        break;
    case OP_READ:
    case OP_WRITE:
        result = op->arg1;
        break;
    case OP_SIZE:
    case OP_EXISTS:
        result = op->arg0;
        break;
    case OP_NULL:
        break;
    case OP_ADDBIG: {
        BigFile *big = (BigFile *)op->arg2;
        if (big == NULL)
            break;
        if (!(op->flags & OPF_CANCELLED) && op->status == ST_OK) {
            Q_pushback(BigFiles, big);
            result = big->id;
        } else {
            if (big->slot != 0)
                FILEDEV_close(big->slot);
            if (big->dir != NULL)
                pFILE_mfree(big->dir);
            pFILE_mfree(big);
        }
        break;
    }
    case OP_DELBIG: {
        BigFile *big = (BigFile *)op->arg2;
        if (big == NULL)
            break;
        pFILE_mfree(big->dir);
        FILEDEV_close(big->slot);
        pFILE_mfree(big);
        result = 1;
        break;
    }
    default:   // 5 and delete
        result = op->status == ST_OK;
        break;
    }
    if (op->flags & OPF_OWNSNAME)
        pFILE_mfree((void *)op->arg2);
    op->handle = 0;
    Q_pushback(FreeOps, op);
    return result;
}

// Runs the callback at once (on this thread) if the op is done already, else when it finishes, on the worker.
// AUTOINJECT
void FILESYS_callbackop(unsigned handle, FsCallback callback) {
    FsWorker *w = WorkerOf(handle);
    if (!w->alive)
        return;
    Q_lock(&w->pending);
    Where where;
    FsOp *op = Find(w, handle, &where);
    if (op != NULL) {
        op->flags |= OPF_HASCALLBACK;
        if (where == DONE) {
            op->flags |= OPF_HASCALLBACK | OPF_CALLEDBACK;
            op->callback = NULL;
            callback(op->handle, op->status, op->userData);
        } else {
            op->callback = callback;
        }
    }
    Q_unlock(&w->pending);
}

// A pending op is filed as done, cancelled, at once; a running one finishes and reports -1.
// AUTOINJECT
void FILESYS_cancelop(unsigned handle) {
    FsWorker *w = WorkerOf(handle);
    if (!w->alive)
        return;
    Q_lock(&w->pending);
    Where where;
    FsOp *op = Find(w, handle, &where);
    if (op != NULL && where != DONE && op->type != OP_CLOSE && op->type != OP_NOP5 && op->type != OP_DELETE &&
        op->type != OP_DELBIG) {
        op->flags |= OPF_CANCELLED;
        if (Q_remove(&w->pending, op)) {
            Q_pushfront(&w->done, op);
            if (op->callback != NULL) {
                op->flags |= OPF_CALLEDBACK;
                op->callback(op->handle, ST_CANCELLED, op->userData);
            }
        }
    }
    Q_unlock(&w->pending);
}

// A pending op moves to its new priority's place (keeping its sequence number).
// AUTOINJECT
void FILESYS_priorityop(unsigned handle, int priority) {
    FsWorker *w = WorkerOf(handle);
    FsOp *op = (FsOp *)Q_findremove(&w->pending, FsOpHandleIs, (int)handle);
    if (op != NULL) {
        op->priority = (uint8_t)priority;
        Q_insertsorted(&w->pending, op);
        SIGNAL_post(&w->signal);
    }
}

// Runs fn(priority, argument) on this thread with the worker holding back every op of lower priority (a larger
// number), and with every other FILESYS_atomic on that worker waiting its turn. 0 without running it if a
// higher priority is already being held.
// AUTOINJECT
int FILESYS_atomic(FsAtomicFn function, int worker, int priority, void *argument) {
    if (worker < 0 || worker > 0x1f)
        return 0;
    FsWorker *w = &FsWorkers[worker];
    if (!w->alive)
        FILESYS_startworker(worker);
    int result = 0;
    MUTEX_lock(&w->atomicMutex);
    int old = w->ceiling;
    if (priority <= old) {
        w->ceiling = priority;
        result = function(priority, argument);
        w->ceiling = old;
        SIGNAL_post(&w->signal);
    }
    MUTEX_unlock(&w->atomicMutex);
    return result;
}

// ---- synchronous wrappers: submit, wait, complete

// AUTOINJECT
bool FILESYS_opensync(const char *name, int mode, int priority, int *outSlot) {
    unsigned handle = FILESYS_open(name, mode, priority, 0);
    if (handle == 0) {
        *outSlot = 0;
        return false;
    }
    FILESYS_waitop(handle);
    bool ok = FILESYS_opstatus(handle) == ST_OK;
    *outSlot = FILESYS_completeop(handle);
    return ok;
}

// AUTOINJECT
bool FILESYS_closesync(int slot, int priority) {
    unsigned handle = FILESYS_close(slot, priority, 0);
    if (handle == 0)
        return false;
    FILESYS_waitop(handle);
    return FILESYS_completeop(handle) != 0;
}

// AUTOINJECT
int FILESYS_sizesync(int slot, int priority) {
    unsigned handle = FILESYS_size(slot, priority, 0);
    if (handle == 0)
        return 0;
    FILESYS_waitop(handle);
    return FILESYS_completeop(handle);
}

// AUTOINJECT
bool FILESYS_addbigsync(const char *name, int allocFlags, int priority, int *outId) {
    unsigned handle = FILESYS_addbig(name, allocFlags, priority, 0);
    if (handle == 0) {
        *outId = 0;
        return false;
    }
    FILESYS_waitop(handle);
    bool ok = FILESYS_opstatus(handle) == ST_OK;
    *outId = FILESYS_completeop(handle);
    return ok;
}

// AUTOINJECT
bool FILESYS_delbigsync(int bigId, int priority) {
    unsigned handle = FILESYS_delbig(bigId, priority, 0);
    if (handle == 0)
        return false;
    FILESYS_waitop(handle);
    return FILESYS_completeop(handle) != 0;
}

// AUTOINJECT
bool FILESYS_existssync(const char *name, int priority) {
    unsigned handle = FILESYS_exists(name, priority, 0);
    if (handle == 0)
        return false;
    FILESYS_waitop(handle);
    return FILESYS_completeop(handle) != 0;
}

// A transfer in ops of kChunk bytes, each submitted by the previous one's callback on the worker, until a
// short one, an error or the end. The original took its arguments in five registers (0x0010b5f0).
typedef unsigned (*ChunkOpFn)(int slot, int offset, void *buffer, int count, int priority, int userData);

struct ChunkCtx {
    int priority;
    int slot;
    int offset;
    volatile int remaining;
    int done;                // bytes transferred so far: the result
    int chunk;               // the current op's size
    char *buffer;
    ChunkOpFn opfn;
    volatile unsigned op;    // the current op
};

static void ChunkDone(unsigned handle, int status, int userData) { // 0x0010b550
    ChunkCtx *c = (ChunkCtx *)userData;
    int n = FILESYS_completeop(handle);
    if (status != ST_OK) {
        c->remaining = 0;
        c->op = 0;
        return;
    }
    c->offset += n;
    c->done += n;
    c->buffer += n;
    int remaining = n < c->chunk ? 0 : c->remaining - n;
    if (remaining > 0) {
        c->chunk = remaining < kChunk ? remaining : kChunk;
        c->remaining = remaining;
        unsigned next = c->opfn(c->slot, c->offset, c->buffer, c->chunk, c->priority, (int)c);
        c->op = next;
        if (next != 0) {
            FILESYS_callbackop(next, ChunkDone);
            return;
        }
    }
    c->remaining = 0;
    c->op = 0;
}

static int ChunkedSync(int slot, int count, int priority, int offset, void *buffer, ChunkOpFn opfn) {
    ChunkCtx c = { priority, slot, offset, count, 0, count < kChunk ? count : kChunk, (char *)buffer, opfn, 0 };
    unsigned first = opfn(slot, offset, buffer, c.chunk, priority, (int)&c);
    if (first != 0) {
        c.op = first;
        FILESYS_callbackop(first, ChunkDone);
        do
            FILESYS_waitop(c.op);   // -3 at once between ops, while the worker submits the next
        while (c.remaining != 0 || c.op != 0);
    }
    return c.done;
}

// AUTOINJECT
int FILESYS_readsync(int slot, int offset, void *buffer, int count, int priority) {
    return ChunkedSync(slot, count, priority, offset, buffer, FILESYS_read);
}

// AUTOINJECT
int FILESYS_writesync(int slot, int offset, const void *buffer, int count, int priority) {
    return ChunkedSync(slot, count, priority, offset, (void *)buffer, (ChunkOpFn)FILESYS_write);
}

// ---- FILE_: whole files, each one locked sequence through FILESYS_atomic at SyncPriority (the open at that
// priority, the rest one better). The z variants differ only in a flag nothing on the Xbox reads.

struct FileReq {
    char *path;
    void *buffer;
    int size;
    int zflag;
    int allocFlags;
};

static int SizeInternal(int priority, void *argument) {            // 0x0010d870
    FileReq *r = (FileReq *)argument;
    int slot;
    if (!FILESYS_opensync(r->path, MODE_READ, priority, &slot))
        return 0;
    int size = FILESYS_sizesync(slot, priority - 1);
    FILESYS_closesync(slot, priority - 1);
    return size;
}

// The allocation is named after the file. 0x0010da00 is the same, and also keeps the size.
static void *LoadInternal(int priority, FileReq *r, bool keepSize) {
    int slot;
    if (!FILESYS_opensync(r->path, MODE_READ, priority, &slot))
        return NULL;
    int size = FILESYS_sizesync(slot, priority - 1);
    void *buffer = pFILE_malloc(r->path, size, r->allocFlags);
    if (buffer != NULL) {
        FILESYS_readsync(slot, 0, buffer, size, priority - 1);
        if (keepSize)
            r->size = size;
    }
    FILESYS_closesync(slot, priority - 1);
    return buffer;
}

static int FILE_load_internal(int priority, void *argument) {      // 0x0010d900
    return (int)LoadInternal(priority, (FileReq *)argument, false);
}

static int FILE_loadsize_internal(int priority, void *argument) {  // 0x0010da00
    return (int)LoadInternal(priority, (FileReq *)argument, true);
}

static int FILE_loadat_internal(int priority, void *argument) {    // 0x0010dae0
    FileReq *r = (FileReq *)argument;
    int count = r->size > 0 ? r->size : 0x7fffffff;
    int slot;
    if (!FILESYS_opensync(r->path, MODE_READ, priority, &slot))
        return 0;
    int got = FILESYS_readsync(slot, 0, r->buffer, count, priority - 1);
    FILESYS_closesync(slot, priority - 1);
    return got;
}

static int FILE_save_internal(int priority, void *argument) {      // 0x0010dbe0
    FileReq *r = (FileReq *)argument;
    int slot;
    if (!FILESYS_opensync(r->path, MODE_CREATE | MODE_TRUNCATE, priority, &slot))
        return 0;
    int put = FILESYS_writesync(slot, 0, r->buffer, r->size, priority - 1);
    FILESYS_closesync(slot, priority - 1);
    return put == r->size;
}

static int Atomic(FsAtomicFn function, char *path, void *buffer, int size, int zflag, int allocFlags,
                  FileReq *out = NULL) {
    FileReq request = { path, buffer, size, zflag, allocFlags };
    int result = FILESYS_atomic(function, 0, SyncPriority, &request);
    if (out != NULL)
        *out = request;
    return result;
}

// True for a file in an open archive as well as on disc.
// AUTOINJECT
bool FILE_exists(char *path) {
    return FILESYS_existssync(path, SyncPriority);
}

// AUTOINJECT
int FILE_sizez(char *path) {
    return Atomic(SizeInternal, path, NULL, 0, 0, 0);
}

// AUTOINJECT
void* FILE_load(char *path, int flags) {
    return (void *)Atomic(FILE_load_internal, path, NULL, 0, 1, flags);
}

// AUTOINJECT
void* FILE_loadz(char *path, int flags) {
    return (void *)Atomic(FILE_load_internal, path, NULL, 0, 0, flags);
}

// AUTOINJECT
void* FILE_loadsizez(char *path, int *outSize, int flags) {
    FileReq request;
    void *data = (void *)Atomic(FILE_loadsize_internal, path, NULL, 0, 0, flags, &request);
    if (outSize != NULL)
        *outSize = request.size;
    return data;
}

// AUTOINJECT
int FILE_loadat(char *path, void *buffer, int size) {
    return Atomic(FILE_loadat_internal, path, buffer, size, 1, 0);
}

// AUTOINJECT
int FILE_loadatz(char *path, void *buffer, int size) {
    return Atomic(FILE_loadat_internal, path, buffer, size, 0, 0);
}

// AUTOINJECT
bool FILE_save(char *path, void *buffer, int size) {
    return Atomic(FILE_save_internal, path, buffer, size, 1, 0) == 1;
}

// The size a packed file unpacks to, from its first 0x80 bytes; 0 if it is not packed.
// AUTOINJECT
unsigned FILE_unpacksizez(char *path) {
    uint8_t head[0x80];
    if (!FILE_loadatz(path, head, sizeof(head)))
        return 0;
    return unpacksizez(head);
}

// A file, unpacked if it is packed.
// AUTOINJECT
void* FILE_loadpackz(char *path, int flags) {
    int size = 0;
    uint8_t *data = (uint8_t *)FILE_loadsizez(path, &size, flags);
    if (data == NULL)
        return NULL;
    unsigned unpacked = unpacksizez(data);
    if (size != 0 && unpacked != 0) {
        void *packed = pFILE_malloc(path, size, flags ^ 0x100);
        if (packed == NULL) {
            pFILE_mfree(data);
            return NULL;
        }
        MEM_copy(packed, data, size);
        pFILE_mfree(data);
        data = (uint8_t *)pFILE_malloc(path, (int)unpacked, flags);
        if (data != NULL && !UNPACK_unpack((const uint8_t *)packed, data)) {
            pFILE_mfree(data);
            data = NULL;
        }
        pFILE_mfree(packed);
    }
    return data;
}

// ---- ASYNCFILE: what is left of it. Its loader only served the movie player, which is ours, so nothing can
// submit a request any more; init and restore still allocate and free its table.

#pragma pack(push, 4)
struct AsyncRequest {        // 0x30
    unsigned id;             // +0x00 generation | index
    AsyncRequest *link;      // +0x04 free list
    int doneBytes;           // +0x08
    int released;            // +0x0c
    int cancelled;           // +0x10
    void *buffer;            // +0x14 1: "allocate one"
    void (*callback)(int);   // +0x18
    unsigned op;             // +0x1c current FILESYS op
    int slot;                // +0x20
    int offset;              // +0x24
    int remaining;           // +0x28
    uint8_t *write;          // +0x2c
};
static_assert(sizeof(AsyncRequest) == 0x30, "AsyncRequest is 0x30 bytes");
#pragma pack(pop)

// The original returns whatever was left in EAX; nobody reads it.
// AUTOINJECT
int ASYNCFILE_init(int count, int allocFlags) {
    if (AsyncRequests != NULL || count > 0x100)
        return 0;
    AsyncCount = count;
    AsyncRequests = AsyncFreeHead = (AsyncRequest *)pFILE_malloc("ASYNCFILE", count * (int)sizeof(AsyncRequest),
                                                                 allocFlags);
    AsyncFreeTail = AsyncRequests + count - 1;
    MUTEX_create(AsyncMutex);
    for (int i = 0; i < count; i++) {
        AsyncRequests[i].id = (unsigned)i;
        AsyncRequests[i].buffer = NULL;
        AsyncRequests[i].link = &AsyncRequests[i + 1];
    }
    AsyncFreeTail->link = NULL;
    return 1;
}

static void ASYNCFILE_free(AsyncRequest *r) {                      // 0x0010b8b0 (ESI = record)
    if (r->cancelled && (uintptr_t)r->buffer > 1)
        pFILE_mfree(r->buffer);
    r->id &= 0xff;
    r->op = 0;
    MUTEX_lock(AsyncMutex);
    if (AsyncFreeHead != NULL)
        AsyncFreeTail->link = r;
    else
        AsyncFreeHead = r;
    AsyncFreeTail = r;
    r->link = NULL;
    MUTEX_unlock(AsyncMutex);
}

// 1 if the request was live and is now cancelled, else -1.
// AUTOINJECT
int ASYNCFILE_cancel(unsigned id) {
    MUTEX_lock(AsyncMutex);
    AsyncRequest *r = NULL;
    if (id >= 0x100 && (int)(id & 0xff) < AsyncCount && AsyncRequests[id & 0xff].id == id)
        r = &AsyncRequests[id & 0xff];
    unsigned op = 0;
    int was = 0, released = 0;
    if (r != NULL) {
        op = r->op;
        was = r->cancelled;
        released = r->released;
        if (op != 0 || !released)
            r->cancelled = 1;
    }
    MUTEX_unlock(AsyncMutex);
    if (r != NULL && !was && r->cancelled) {
        if (op != 0)
            FILESYS_cancelop(op);
        else if (!released)
            ASYNCFILE_free(r);
        return 1;
    }
    return -1;
}

// AUTOINJECT
void ASYNCFILE_restore() {
    if (AsyncRequests == NULL)
        return;
    for (int i = 0; i < AsyncCount; i++)
        ASYNCFILE_cancel(AsyncRequests[i].id);
    for (;;) {
        bool busy = false;
        for (int i = 0; i < AsyncCount; i++)
            busy |= AsyncRequests[i].op != 0;
        if (!busy)
            break;
        if (THREAD_iscurrent(NULL))
            SYNCTASK_run(0);
        THREAD_yield(0);
    }
    pFILE_mfree(AsyncRequests);
    REALMUTEX_destroy(AsyncMutex);
    AsyncRequests = NULL;
}
