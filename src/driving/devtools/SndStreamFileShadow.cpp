#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // as the build defines it (CMakeLists.txt)
#endif

#include "SndStreamFileShadow.h"

#include "../sound/snd/Stream.h"
#include "../platform/FileSys.h"
#include "../platform/RealSystem.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDSTREAMFILESHADOW=1, at injection time on the loader's thread before the game runs: EA's STREAM
// (sound/snd/Stream.cpp, docs/driving/sound.md 4.5) against the originals, as sound.md 9.3 step 6 asks for STREAM -
// both versions driven through the same scripted sequence behind a synchronous fake FILESYS.
//
// A case: STREAM_create into a workspace block (random request/filter/reader counts, ring sizes 0x1800..0x10000,
// the block at a random offset modulo 128, some invalid parameter sets), the dead setfilter/setpriority/taphandle/
// isendofstream on it, then a few hundred random steps - queue disc stream files (BIGF entries of driving\*.spe and
// *.mus, read from their offsets on to the 'SCEl' end chunk), a synthetic chunk file, a missing file, a memory
// request; complete pending FILESYS operations; STREAM_get from each reader and STREAM_release in and out of order;
// greedy level and state; cancel requests; kill - and STREAM_destroy at the end. The workspace (the STREAM's memory,
// the script's state and the fake's state) is snapshotted, run with the module's range swapped back to the
// originals (common/xbeOriginal.h), saved, put back, run with ours, and the two compared word by word. Inside it is
// the log of every call: each fake call with its arguments (FILESYS open/close/read/completeop/callbackop/
// priorityop/closesync, MUTEX create/lock/unlock/destroy - which pins the lock scope - THREAD_iscurrent/yield,
// SYNCTASK_run), every API call's result, and a hash of the STREAM header and records after each step and at
// every lock and unlock (so a store moved across a lock boundary shows). Also
// compared: the request generation global 0x002475fc, and whether a side faulted (SEH: counted, not fatal).
//
// The fake FILESYS is replaced by five-byte jumps over both the originals' entries and our FILESYS's (the port
// calls ours directly), put back after; operations complete only when the script pumps them, in order, the
// callback running on this thread as it would on the worker.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

using namespace SND;

const uint32_t kEndTag = 0x6c454353u;   // 'SCEl', what SNDSTRMI_queue asks for
const uint32_t kGeneration = 0x002475fcu;

// ---- random numbers (the case set-up's; the script's own lives in the workspace)

uint32_t g_seed = 1;

uint32_t NextOf(uint32_t &seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

uint32_t Next() {
    return NextOf(g_seed);
}

int Range(int lo, int hi) {   // inclusive
    return lo + (int)(Next() % (uint32_t)(hi - lo + 1));
}

uint32_t BE32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// ---- the files the fake FILESYS serves

struct Entry {
    uint32_t offset, size;
};

struct SourceFile {
    const char *name;        // what is queued, and what FILESYS_open is asked for
    FILE *file;              // a disc file, read on demand
    const uint8_t *memory;   // or one in memory
    uint32_t size;
    Entry entries[256];
    int entryCount;
};

const int kFiles = 4;        // 0, 1 disc, 2 synthetic, 3 missing
SourceFile g_files[kFiles] = {
    { "D:\\driving\\mis01en.spe" }, { "D:\\driving\\mis01.mus" }, { "D:\\synthetic.asf" }, { "D:\\missing.asf" } };
int g_discFiles;

uint8_t *g_synthetic;
const uint32_t kSyntheticSize = 0x30000;
uint8_t *g_memoryStream;     // a stream for STREAM_queuemem
uint32_t g_memoryStreamSize;
bool g_memoryFromDisc;

void LoadDiscFile(SourceFile &f) {
    char path[MAX_PATH];
    if (!Xbox_ResolvePath(f.name, path, sizeof(path)))
        return;
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return;
    uint8_t header[16];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, "BIGF", 4) != 0) {
        fclose(file);
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    uint32_t count = BE32(header + 8), headerSize = BE32(header + 12);
    if (headerSize > 0x10000 || headerSize < 16) {
        fclose(file);
        return;
    }
    uint8_t *directory = (uint8_t *)malloc(headerSize);
    fseek(file, 0, SEEK_SET);
    if (directory == NULL || fread(directory, 1, headerSize, file) != headerSize) {
        free(directory);
        fclose(file);
        return;
    }
    uint32_t at = 16;
    for (uint32_t i = 0; i < count && f.entryCount < 256 && at + 9 <= headerSize; i++) {
        Entry e = { BE32(directory + at), BE32(directory + at + 4) };
        at += 8;
        while (at < headerSize && directory[at] != 0)
            at++;
        at++;
        if (e.offset + 8 <= (uint32_t)size && e.size >= 8)
            f.entries[f.entryCount++] = e;
    }
    free(directory);
    if (f.entryCount == 0) {
        fclose(file);
        return;
    }
    f.file = file;
    f.size = (uint32_t)size;
    g_discFiles++;
}

// chunks {tag, size} with data: headers, data, a few foreign tags, the end chunk; sometimes a size word with a
// top byte (which STREAM takes as the end)
uint32_t MakeChunks(uint8_t *out, uint32_t room, int streams) {
    static const uint32_t tags[] = { 0x6c484353u /*SCHl*/, 0x6c434353u /*SCCl*/, 0x6c444353u /*SCDl*/,
                                     0x6c444353u, 0x6c444353u, 0x6c444353u, 0x5858584eu, 0x6c504353u };
    uint32_t at = 0;
    for (int s = 0; s < streams; s++) {
        int chunks = Range(1, 40);
        for (int c = 0; c <= chunks; c++) {
            uint32_t size = c == chunks ? 8 : (uint32_t)Range(2, 0x900) * 4;
            if (at + size + 8 > room)
                return at;
            uint32_t *p = (uint32_t *)(out + at);
            p[0] = c == chunks ? kEndTag : (c == 0 ? tags[0] : tags[Range(1, 7)]);
            p[1] = size;
            if (c != chunks && Range(0, 199) == 0)
                p[1] |= 0x01000000u;
            for (uint32_t k = 8; k < size; k += 4)
                *(uint32_t *)(out + at + k) = Next();
            at += size;
        }
    }
    return at;
}

void MakeSources() {
    for (int i = 0; i < 2; i++)
        LoadDiscFile(g_files[i]);
    g_synthetic = (uint8_t *)malloc(kSyntheticSize);
    SourceFile &syn = g_files[2];
    uint32_t at = 0;
    while (syn.entryCount < 24) {
        uint32_t used = MakeChunks(g_synthetic + at, kSyntheticSize - at, 1);
        if (used == 0)
            break;
        syn.entries[syn.entryCount].offset = at;
        syn.entries[syn.entryCount].size = used;
        syn.entryCount++;
        at += used;
    }
    syn.memory = g_synthetic;
    syn.size = at;
    // the memory request: the smallest whole stream of the music file that ends in 'SCEl', or a synthetic one
    g_memoryStream = (uint8_t *)malloc(0x20000);
    SourceFile &mus = g_files[1];
    for (int i = 0; i < mus.entryCount && mus.file != NULL; i++) {
        Entry e = mus.entries[i];
        if (e.size > 0x20000 || (g_memoryFromDisc && e.size >= g_memoryStreamSize))
            continue;
        fseek(mus.file, (long)e.offset, SEEK_SET);
        if (fread(g_memoryStream, 1, e.size, mus.file) != e.size)
            continue;
        uint32_t walk = 0;
        bool ends = false;
        while (walk + 8 <= e.size) {
            uint32_t tag = *(uint32_t *)(g_memoryStream + walk), size = *(uint32_t *)(g_memoryStream + walk + 4);
            if (tag == kEndTag) {
                ends = true;
                break;
            }
            if (size < 8)
                break;
            walk += size;
        }
        if (ends) {
            g_memoryFromDisc = true;
            g_memoryStreamSize = e.size;
        }
    }
    if (g_memoryFromDisc) {   // read the chosen one again (the loop may have overwritten it)
        for (int i = 0; i < mus.entryCount; i++)
            if (mus.entries[i].size == g_memoryStreamSize) {
                fseek(mus.file, (long)mus.entries[i].offset, SEEK_SET);
                if (fread(g_memoryStream, 1, g_memoryStreamSize, mus.file) == g_memoryStreamSize)
                    break;
            }
    } else {
        g_memoryStreamSize = MakeChunks(g_memoryStream, 0x20000, 1);
    }
}

uint32_t ReadSource(int index, uint32_t offset, uint8_t *buffer, uint32_t count) {
    SourceFile &f = g_files[index];
    if (offset >= f.size)
        return 0;
    if (count > f.size - offset)
        count = f.size - offset;
    if (f.memory != NULL) {
        memcpy(buffer, f.memory + offset, count);
        return count;
    }
    if (f.file == NULL || fseek(f.file, (long)offset, SEEK_SET) != 0)
        return 0;
    return (uint32_t)fread(buffer, 1, count, f.file);
}

// ---- the workspace

const uint32_t kBlock = 0x12000;
const int kMaxOps = 4096;
const int kMaxPending = 64;
const int kLogRecords = 16384;
const int kHeld = 48;

enum { OP_OPEN = 1, OP_CLOSE, OP_READ };

struct Op {
    uint32_t kind;
    uint32_t handle;
    int32_t source;          // -1 none
    int32_t offset;
    uint8_t *buffer;
    int32_t count;
    int32_t user;
    int32_t result;
    uint32_t callback;
    uint32_t pad;
};

struct Record {
    uint32_t w[6];
};

struct Workspace {
    alignas(128) uint8_t block[kBlock];
    uint32_t rng;
    uint32_t nextHandle;
    int32_t opCount;
    int32_t pendingCount;
    uint32_t pending[kMaxPending];
    Op ops[kMaxOps];
    StrmInternal *stream;
    StrmReader *handle;
    int32_t lockDepth, badUnlocks;
    int32_t yields, forced;
    int32_t isMain;
    int32_t heldCount;
    uint32_t *held[kHeld];
    uint32_t ids[32];
    int32_t idCount;
    int32_t logCount;
    uint32_t logHash;
    int32_t step;
    uint32_t pad[2];
    Record log[kLogRecords];
};

Workspace *W, *g_snapshot, *g_resultOriginal;

// ---- the case parameters (outside the workspace, read-only during a run)

struct Params {
    int requests, filters, readers, size, offset;
    int steps;
    int setFilters;
    int startQueue;
    uint32_t seed;
    int isMain;
    int allowMissing;
} g_p;

// ---- the log

enum {
    L_OPEN = 0x100, L_CLOSE, L_READ, L_COMPLETE, L_CALLBACK, L_PRIORITY, L_CLOSESYNC, L_MUTEX_CREATE, L_MUTEX_DESTROY,
    L_LOCK, L_UNLOCK, L_ISCURRENT, L_YIELD, L_SYNCTASK, L_PUMP, L_FORCED,
    L_CREATE = 0x200, L_SETFILTER, L_SETPRIORITY, L_TAP, L_QUEUEFILE, L_QUEUEMEM, L_GET, L_RELEASE, L_GREEDYLEVEL,
    L_GREEDYSTATE, L_CANCEL, L_KILL, L_DESTROY, L_QUERY, L_STEP
};

const char *LogName(uint32_t kind) {
    static const char *fake[] = { "FILESYS_open", "FILESYS_close", "FILESYS_read", "FILESYS_completeop",
                                  "FILESYS_callbackop", "FILESYS_priorityop", "FILESYS_closesync", "MUTEX_create",
                                  "REALMUTEX_destroy", "MUTEX_lock", "MUTEX_unlock", "THREAD_iscurrent",
                                  "THREAD_yield", "SYNCTASK_run", "pump", "forced idle" };
    static const char *api[] = { "STREAM_create", "STREAM_setfilter", "STREAM_setpriority", "STREAM_taphandle",
                                 "STREAM_queuefile", "STREAM_queuemem", "STREAM_get", "STREAM_release",
                                 "STREAM_setgreedylevel", "STREAM_setgreedystate", "STREAM_cancelrequest",
                                 "STREAM_kill", "STREAM_destroy", "queries", "step" };
    if (kind >= 0x100 && kind < 0x100 + sizeof(fake) / sizeof(fake[0]))
        return fake[kind - 0x100];
    if (kind >= 0x200 && kind < 0x200 + sizeof(api) / sizeof(api[0]))
        return api[kind - 0x200];
    return "?";
}

void Log(uint32_t kind, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0) {
    uint32_t v[6] = { kind, a, b, c, d, e };
    uint32_t h = W->logHash;
    for (int i = 0; i < 24; i++)
        h = (h ^ ((const uint8_t *)v)[i]) * 16777619u;
    W->logHash = h;
    if (W->logCount < kLogRecords)
        memcpy(W->log[W->logCount].w, v, sizeof(v));
    W->logCount++;
}

uint32_t P(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

uint32_t HashBytes(const void *data, uint32_t bytes) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < bytes; i++)
        h = (h ^ ((const uint8_t *)data)[i]) * 16777619u;
    return h;
}

uint32_t HashName(const char *name) {
    return name == NULL ? 0 : HashBytes(name, (uint32_t)strlen(name));
}

// ---- the fakes (cdecl, called by the original and by ours alike)

Op *NewOp(uint32_t kind, int source, int offset, void *buffer, int count, int user) {
    W->nextHandle += 0x10;
    uint32_t handle = W->nextHandle;
    if (W->opCount >= kMaxOps)
        return NULL;
    Op *op = &W->ops[W->opCount++];
    op->kind = kind;
    op->handle = handle;
    op->source = source;
    op->offset = offset;
    op->buffer = (uint8_t *)buffer;
    op->count = count;
    op->user = user;
    op->result = 0;
    op->callback = 0;
    return op;
}

Op *FindOp(uint32_t handle) {
    for (int i = W->opCount - 1; i >= 0; i--)
        if (W->ops[i].handle == handle)
            return &W->ops[i];
    return NULL;
}

unsigned FakeOpen(const char *name, int mode, int priority, int user) {
    Log(L_OPEN, HashName(name), (uint32_t)mode, (uint32_t)priority, (uint32_t)user);
    int source = -1;
    for (int i = 0; i < kFiles; i++)
        if (name != NULL && strcmp(name, g_files[i].name) == 0 && (g_files[i].file != NULL || g_files[i].memory))
            source = i;
    Op *op = NewOp(OP_OPEN, source, 0, NULL, 0, user);
    if (op == NULL)
        return 0;
    op->result = source >= 0 ? 0x40 + source : 0;   // the slot; 0 = not found
    return op->handle;
}

unsigned FakeClose(int slot, int priority, int user) {
    Log(L_CLOSE, (uint32_t)slot, (uint32_t)priority, (uint32_t)user);
    Op *op = NewOp(OP_CLOSE, slot - 0x40, 0, NULL, 0, user);
    if (op == NULL)
        return 0;
    op->result = 1;
    return op->handle;
}

unsigned FakeRead(int slot, int offset, void *buffer, int count, int priority, int user) {
    Log(L_READ, (uint32_t)slot, (uint32_t)offset, P(buffer), (uint32_t)count, (uint32_t)priority);
    int source = slot - 0x40;
    Op *op = NewOp(OP_READ, source >= 0 && source < kFiles ? source : -1, offset, buffer, count, user);
    return op == NULL ? 0 : op->handle;
}

int FakeCompleteop(unsigned handle) {
    Op *op = FindOp(handle);
    int result = op == NULL ? -1 : op->result;
    Log(L_COMPLETE, handle, (uint32_t)result);
    return result;
}

void FakeCallbackop(unsigned handle, FsCallback callback) {
    Log(L_CALLBACK, handle, P((const void *)callback));
    Op *op = FindOp(handle);
    if (op == NULL || W->pendingCount >= kMaxPending)
        return;
    op->callback = P((const void *)callback);
    W->pending[W->pendingCount++] = handle;
}

void FakePriorityop(unsigned handle, int priority) {
    Log(L_PRIORITY, handle, (uint32_t)priority);
}

bool FakeClosesync(int slot, int priority) {
    Log(L_CLOSESYNC, (uint32_t)slot, (uint32_t)priority);
    return true;
}

char FakeMutexCreate(RealMutex *mutex) {
    Log(L_MUTEX_CREATE, P(mutex));
    return 1;
}

void FakeMutexDestroy(RealMutex *mutex) {
    Log(L_MUTEX_DESTROY, P(mutex));
}

// what the lock guards, as the lock is taken and given back: a write moved across a lock boundary shows here
uint32_t RecordsHash() {
    StrmInternal *s = W->stream;
    if (s == NULL)
        return 0;
    uint32_t bytes = (uint32_t)(s->ringBase - (uint8_t *)s);
    uint32_t h = HashBytes(s, bytes > kBlock ? 0x190 : bytes);
    // and the chunk header each reader's next STREAM_get hands out
    for (int i = 0; i < s->numReaders && i < 16 && bytes <= kBlock; i++) {
        const uint8_t *next = (const uint8_t *)s->readers[i].next;
        if (s->readers[i].bytes > 0 && next >= W->block && next + 8 <= W->block + kBlock)
            h = (h ^ HashBytes(next, 8)) * 16777619u;
    }
    return h;
}

void FakeMutexLock(RealMutex *mutex) {
    W->lockDepth++;
    Log(L_LOCK, P(mutex), (uint32_t)W->lockDepth, RecordsHash());
}

void FakeMutexUnlock(RealMutex *mutex) {
    if (W->lockDepth <= 0)
        W->badUnlocks++;
    else
        W->lockDepth--;
    Log(L_UNLOCK, P(mutex), (uint32_t)W->lockDepth, RecordsHash());
}

bool FakeIsCurrent(RealThread *thread) {
    Log(L_ISCURRENT, P(thread));
    return W->isMain != 0;
}

unsigned FakeSyncTaskRun(int argument) {
    Log(L_SYNCTASK, (uint32_t)argument);
    return 0;
}

// Completes the oldest pending operation: a read's bytes are copied now, then the callback runs.
bool Pump() {
    if (W->pendingCount == 0)
        return false;
    uint32_t handle = W->pending[0];
    W->pendingCount--;
    memmove(W->pending, W->pending + 1, (size_t)W->pendingCount * sizeof(W->pending[0]));
    Op *op = FindOp(handle);
    if (op == NULL)
        return true;
    if (op->kind == OP_READ)
        op->result = op->source < 0 || op->count < 0 ? 0
                                                     : (int)ReadSource(op->source, (uint32_t)op->offset, op->buffer,
                                                                       (uint32_t)op->count);
    Log(L_PUMP, handle, op->kind, (uint32_t)op->result);
    ((FsCallback)(uintptr_t)op->callback)(handle, 0, op->user);
    return true;
}

// STREAM_destroy's wait: each yield lets the worker finish one operation. With nothing left in flight a stream
// stuck reading (its file was not found) never goes idle, in the original as in ours: after a while the fake
// says so and puts it to idle itself, the same for both.
void FakeYield(unsigned milliseconds) {
    Log(L_YIELD, milliseconds);
    W->yields++;
    if (!Pump() && W->yields > 64 && W->stream != NULL && W->stream->state == 1) {
        W->stream->state = 0;
        W->forced++;
        Log(L_FORCED);
    }
}

// ---- five-byte jumps over the originals and ours the fakes stand in for

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[40];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
    if (g_hookCount >= (int)(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    for (int i = 0; i < g_hookCount; i++)
        if (g_hooks[i].at == at)
            return;   // the same entry twice (ours is the original's address): once is enough
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old)) {
        h.on = false;
        return;
    }
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

void HookBoth(uint32_t original, const void *ours, const void *fake) {
    HookInstall(original, fake);
    HookInstall(P(ours), fake);
}

void HooksInstall() {
    HookBoth(0x0010cdc0u, (const void *)&FILESYS_open, (const void *)&FakeOpen);
    HookBoth(0x0010ce60u, (const void *)&FILESYS_close, (const void *)&FakeClose);
    HookBoth(0x0010ceb0u, (const void *)&FILESYS_read, (const void *)&FakeRead);
    HookBoth(0x0010c700u, (const void *)&FILESYS_completeop, (const void *)&FakeCompleteop);
    HookBoth(0x0010c980u, (const void *)&FILESYS_callbackop, (const void *)&FakeCallbackop);
    HookBoth(0x0010ca50u, (const void *)&FILESYS_priorityop, (const void *)&FakePriorityop);
    HookBoth(0x0010b750u, (const void *)&FILESYS_closesync, (const void *)&FakeClosesync);
    HookBoth(0x0014a4f0u, (const void *)&MUTEX_create, (const void *)&FakeMutexCreate);
    HookBoth(0x0014a510u, (const void *)&REALMUTEX_destroy, (const void *)&FakeMutexDestroy);
    HookBoth(0x0014a520u, (const void *)&MUTEX_lock, (const void *)&FakeMutexLock);
    HookBoth(0x0014a530u, (const void *)&MUTEX_unlock, (const void *)&FakeMutexUnlock);
    HookBoth(0x0010a7f0u, (const void *)&THREAD_iscurrent, (const void *)&FakeIsCurrent);
    HookBoth(0x0010a7e0u, (const void *)&THREAD_yield, (const void *)&FakeYield);
    HookBoth(0x0010ac40u, (const void *)&SYNCTASK_run, (const void *)&FakeSyncTaskRun);
}

// ---- the originals' addresses and ours

void Originals(bool original) {
    XbeOriginal_RestoreRange(0x0014aba0u, 0x0014bee0u, original);
    XbeOriginal_RestoreRange(0x001503b0u, 0x001503e0u, original);
}

typedef StrmReader *(*CreateFn)(int, int, int, StrmInternal *, int);
typedef void (*SetFilterFn)(StrmReader *, int, uint32_t, uint32_t, int);
typedef void (*SetPriorityFn)(StrmReader *, int, int);
typedef StrmReader *(*TapFn)(StrmReader *, int);
typedef uint32_t (*QueueFileFn)(StrmReader *, const char *, int, uint32_t);
typedef uint32_t (*QueueMemFn)(StrmReader *, const uint32_t *, int, uint32_t);
typedef uint32_t *(*GetFn)(StrmReader *);
typedef int (*IntFn)(StrmReader *);
typedef bool (*BoolFn)(StrmReader *);
typedef void (*SetIntFn)(StrmReader *, int);
typedef void (*ReleaseFn)(StrmReader *, uint32_t *);
typedef void (*CancelFn)(StrmReader *, uint32_t);
typedef void (*HandleFn)(StrmReader *);
typedef int (*OverheadFn)(int, int, int);

bool g_original;

#define PICK(type, address, port) (g_original ? (type)(uintptr_t)(address) : (type)(port))

int ScriptRange(int lo, int hi) {
    return lo + (int)(NextOf(W->rng) % (uint32_t)(hi - lo + 1));
}

void LogState(int step) {
    StrmInternal *s = W->stream;
    uint32_t bytes = (uint32_t)(s->ringBase - (uint8_t *)s);
    if (bytes > kBlock)
        bytes = 0x190;
    Log(L_STEP, (uint32_t)step, HashBytes(s, bytes), (uint32_t)s->state, (uint32_t)s->bytesBuffered,
        (uint32_t)(s->parsePos - s->ringBase));
}

void Hold(uint32_t *chunk) {
    if (chunk != NULL && W->heldCount < kHeld)
        W->held[W->heldCount++] = chunk;
}

void ReleaseHeld(int index) {
    StrmReader *handle = W->handle;
    uint32_t *chunk = W->held[index];
    for (int i = index; i + 1 < W->heldCount; i++)
        W->held[i] = W->held[i + 1];
    W->heldCount--;
    // the reader the chunk came from does not matter to STREAM_release: it only uses the STREAM behind it
    PICK(ReleaseFn, 0x0014b9f0u, &STREAM_release)(handle, chunk);
    Log(L_RELEASE, P(chunk), chunk[0], chunk[1]);
}

// 0, 1 a disc file, 2 the synthetic file, 3 a file FILESYS does not find, 4 memory
void Queue(int kind) {
    StrmReader *handle = W->handle;
    uint32_t id = 0;
    if (kind == 4) {   // memory
        int size = ScriptRange(0, 3) == 0 ? (int)g_memoryStreamSize : 0;
        id = PICK(QueueMemFn, 0x0014b470u, &STREAM_queuemem)(handle, (const uint32_t *)g_memoryStream, size,
                                                              kEndTag);
        Log(L_QUEUEMEM, (uint32_t)size, id);
    } else {
        int source = kind;
        if (source < 2 && g_files[source].file == NULL)
            source = 2;
        SourceFile &f = g_files[source];
        int offset = 0;
        if (f.entryCount > 0) {
            Entry e = f.entries[ScriptRange(0, f.entryCount - 1)];
            offset = (int)e.offset;
        }
        id = PICK(QueueFileFn, 0x0014b3b0u, &STREAM_queuefile)(handle, f.name, offset, kEndTag);
        Log(L_QUEUEFILE, (uint32_t)source, (uint32_t)offset, id);
    }
    if (id != 0 && W->idCount < 32)
        W->ids[W->idCount++] = id;
}

// One case's script; runs the same on both sides (its random numbers are the workspace's)
void Script() {
    StrmInternal *memory = (StrmInternal *)(W->block + g_p.offset);
    StrmReader *handle = PICK(CreateFn, 0x0014b0c0u, &STREAM_create)(g_p.requests, g_p.filters, g_p.readers,
                                                                        memory, g_p.size);
    Log(L_CREATE, P(handle), PICK(OverheadFn, 0x0014b090u, &STREAM_overhead)(g_p.requests, g_p.filters,
                                                                               g_p.readers));
    if (handle == NULL)
        return;
    W->handle = handle;
    W->stream = handle->internal;
    W->isMain = g_p.isMain;
    StrmInternal *s = W->stream;
    int readers = s->numReaders;

    // the dead entries, on a fresh stream
    for (int i = 0; i < g_p.setFilters; i++) {
        int index = ScriptRange(0, s->numFilters + 1);
        uint32_t mask = ScriptRange(0, 2) == 0 ? 0 : 0xffffffffu;
        uint32_t value = mask == 0 ? 0 : (ScriptRange(0, 1) ? 0x6c484353u : 0x6c444353u);
        int reader = ScriptRange(-3, readers + 1);
        PICK(SetFilterFn, 0x0014b2a0u, &STREAM_setfilter)(handle, index, mask, value, reader);
        Log(L_SETFILTER, (uint32_t)index, mask, value, (uint32_t)reader);
    }
    for (int r = 0; r <= readers + 1; r++)
        Log(L_TAP, (uint32_t)r, P(PICK(TapFn, 0x0014b380u, &STREAM_taphandle)(handle, r)));
    if (ScriptRange(0, 3) == 0) {
        int idle = ScriptRange(0, 0x100), greedy = ScriptRange(0, 0x100);
        PICK(SetPriorityFn, 0x0014b310u, &STREAM_setpriority)(handle, idle, greedy);
        Log(L_SETPRIORITY, (uint32_t)idle, (uint32_t)greedy);
    }
    if (ScriptRange(0, 1) == 0) {
        int level = ScriptRange(0, s->ringEnd - s->ringBase);
        PICK(SetIntFn, 0x0014b9a0u, &STREAM_setgreedylevel)(handle, level);
        Log(L_GREEDYLEVEL, (uint32_t)level);
    }
    for (int i = 0; i < g_p.startQueue; i++)
        Queue(g_p.allowMissing && i == 0 ? 3 : (ScriptRange(0, 9) < 7 ? ScriptRange(0, 1) : (ScriptRange(0, 1) ? 2 : 4)));
    LogState(-1);

    for (int step = 0; step < g_p.steps; step++) {
        W->step = step;
        int op = ScriptRange(0, 99);
        StrmReader *reader = &W->stream->readers[ScriptRange(0, readers - 1)];
        if (op < 32) {
            int n = ScriptRange(1, 3);
            for (int i = 0; i < n; i++)
                Pump();
        } else if (op < 58) {
            int n = ScriptRange(1, 6);
            for (int i = 0; i < n; i++) {
                uint32_t *chunk = PICK(GetFn, 0x0014b520u, &STREAM_get)(reader);
                Log(L_GET, P(reader), P(chunk), chunk != NULL ? chunk[0] : 0, chunk != NULL ? chunk[1] : 0);
                Hold(chunk);
            }
        } else if (op < 80) {
            int n = ScriptRange(1, 4);
            for (int i = 0; i < n && W->heldCount > 0; i++) {
                int index = ScriptRange(0, 4) == 0 ? ScriptRange(0, W->heldCount - 1) : 0;
                uint32_t *chunk = W->held[index];
                ReleaseHeld(index);
                if (ScriptRange(0, 9) == 0) {   // again at once: STREAM_release ignores a released chunk
                    PICK(ReleaseFn, 0x0014b9f0u, &STREAM_release)(W->handle, chunk);
                    Log(L_RELEASE, P(chunk), chunk[0], chunk[1]);
                }
            }
            if (ScriptRange(0, 19) == 0) {   // outside the ring
                uint32_t *bogus = ScriptRange(0, 1) ? (uint32_t *)W->stream : (uint32_t *)(W->stream->ringEnd - 4);
                PICK(ReleaseFn, 0x0014b9f0u, &STREAM_release)(W->handle, bogus);
                Log(L_RELEASE, P(bogus));
            }
        } else if (op < 85) {
            StrmInternal *s = W->stream;
            int level = ScriptRange(0, (int)(s->ringEnd - s->ringBase) / 2);
            int edge = ScriptRange(0, 2);
            if (edge == 1) {          // exactly what is buffered: the comparisons' equal case
                level = s->bytesBuffered;
            } else if (edge == 2) {   // what will be buffered once the next chunk is delivered
                const uint8_t *next = s->parsePos;
                level = s->bytesBuffered;
                if (next >= W->block && next + 8 <= W->block + kBlock && s->parsePos != s->writePos)
                    level += (int)(((const uint32_t *)next)[1] & 0xffffff);
            }
            PICK(SetIntFn, 0x0014b9a0u, &STREAM_setgreedylevel)(W->handle, level);
            Log(L_GREEDYLEVEL, (uint32_t)level);
        } else if (op < 87) {
            int greedy = ScriptRange(0, 1);
            PICK(SetIntFn, 0x0014b340u, &STREAM_setgreedystate)(W->handle, greedy);
            Log(L_GREEDYSTATE, (uint32_t)greedy);
        } else if (op < 92) {
            int kind = ScriptRange(0, 19);
            Queue(kind < 14 ? kind % 2 : (kind < 17 ? 2 : (kind < 19 ? 4 : (g_p.allowMissing ? 3 : 2))));
        } else if (op < 95) {
            uint32_t id = W->idCount > 0 && ScriptRange(0, 4) != 0 ? W->ids[ScriptRange(0, W->idCount - 1)]
                                                                    : NextOf(W->rng);
            PICK(CancelFn, 0x0014ba80u, &STREAM_cancelrequest)(W->handle, id);
            Log(L_CANCEL, id);
        } else if (op < 97) {
            if (ScriptRange(0, 2) == 0) {
                PICK(HandleFn, 0x0014bcb0u, &STREAM_kill)(W->handle);
                W->heldCount = 0;   // every chunk is released by the kill: what was held is stale
                Log(L_KILL);
            }
        } else {
            Log(L_QUERY, (uint32_t)PICK(IntFn, 0x0014b5d0u, &STREAM_gettable)(reader),
                (uint32_t)PICK(IntFn, 0x0014b5f0u, &STREAM_state)(W->handle),
                (uint32_t)PICK(IntFn, 0x0014b640u, &STREAM_buffersize)(W->handle),
                (uint32_t)(uint8_t)PICK(BoolFn, 0x0014b610u, &STREAM_isendofstream)(reader));
        }
        LogState(step);
    }
    PICK(HandleFn, 0x0014be60u, &STREAM_destroy)(W->handle);
    Log(L_DESTROY, W->stream->magic, (uint32_t)W->stream->state);
    // what the bad handles get
    Log(L_QUERY, (uint32_t)PICK(IntFn, 0x0014b5f0u, &STREAM_state)(W->handle),
        (uint32_t)PICK(IntFn, 0x0014b5d0u, &STREAM_gettable)(NULL),
        P(PICK(GetFn, 0x0014b520u, &STREAM_get)(W->handle)));
}

uint32_t g_faultAt[2];   // where each side faulted

#ifdef _MSC_VER
int FaultFilter(EXCEPTION_POINTERS *e, bool original) {
    g_faultAt[original ? 0 : 1] = (uint32_t)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

bool Guarded(bool original) {
    g_original = original;
    g_faultAt[original ? 0 : 1] = 0;
#ifdef _MSC_VER
    __try {
        Script();
        return true;
    } __except (FaultFilter(GetExceptionInformation(), original)) {
        return false;
    }
#else
    Script();
    return true;
#endif
}

int g_cases, g_checks, g_differ, g_details, g_faultsBoth, g_forced, g_reads, g_records, g_created;

void Detail(const char *format, ...) {
    if (g_details++ >= 10)
        return;
    char line[400];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[sndstreamfileshadow]   %s\n", line);
}

const char *Region(size_t offset) {
    if (offset < offsetof(Workspace, rng))
        return "STREAM memory";
    if (offset < offsetof(Workspace, log))
        return "script/fake state";
    return "call log";
}

void SetupCase(int index) {
    g_seed = 0x9e3779b9u ^ ((uint32_t)index * 0x85ebca6bu);
    if (g_seed == 0)
        g_seed = 1;
    memset(&g_p, 0, sizeof(g_p));
    g_p.requests = Range(2, 8);
    g_p.filters = Range(1, 3);
    g_p.readers = Range(1, g_p.filters);
    if (Range(0, 3) != 0)
        g_p.filters = g_p.readers = 1;   // what SNDSTRMI_create asks for
    static const int rings[] = { 0x1800, 0x2000, 0x3000, 0x3fff, 0x4000, 0x6000, 0x7fff, 0x8000, 0xc000, 0x10000 };
    int ring = rings[Range(0, 9)] + (Range(0, 2) == 0 ? Range(0, 0x7f) : 0);
    g_p.size = STREAM_overhead(g_p.requests, g_p.filters, g_p.readers) + ring;
    g_p.offset = Range(0, 0x1f) * 4;
    if (index % 13 == 7) {   // parameters STREAM_create refuses
        switch (Range(0, 5)) {
        case 0: g_p.size -= ring - 0x17ff; break;
        case 1: g_p.requests = 1; break;
        case 2: g_p.requests = 0x101; g_p.size = 0x10000; break;
        case 3: g_p.filters = 0; break;
        case 4: g_p.filters = 17; break;
        default: g_p.readers = g_p.filters + 1; break;
        }
    }
    if (g_p.size + g_p.offset > (int)kBlock)
        g_p.size = (int)kBlock - g_p.offset;
    g_p.steps = Range(60, 400);
    g_p.setFilters = g_p.filters > 1 || Range(0, 3) == 0 ? Range(1, 5) : 0;
    g_p.startQueue = Range(1, 3);
    g_p.seed = Next() | 1;
    g_p.isMain = Range(0, 1);
    g_p.allowMissing = index % 20 == 3;

    memset(W, 0, sizeof(Workspace));
    W->rng = g_p.seed;
    W->logHash = 2166136261u;
    // the ring's memory starts as noise, so stale bytes a chunk walk might read are the same on both sides
    uint32_t noise = g_p.seed;
    for (uint32_t i = 0; i < kBlock; i += 4)
        *(uint32_t *)(W->block + i) = NextOf(noise) | 0x80000000u;
}

void RunCase(int index) {
    SetupCase(index);
    memcpy(g_snapshot, W, sizeof(Workspace));
    uint32_t generation = *(uint32_t *)(uintptr_t)kGeneration;

    Originals(true);
    bool okOriginal = Guarded(true);
    Originals(false);
    memcpy(g_resultOriginal, W, sizeof(Workspace));
    uint32_t generationOriginal = *(uint32_t *)(uintptr_t)kGeneration;

    memcpy(W, g_snapshot, sizeof(Workspace));
    *(uint32_t *)(uintptr_t)kGeneration = generation;
    bool okOurs = Guarded(false);
    uint32_t generationOurs = *(uint32_t *)(uintptr_t)kGeneration;

    g_cases++;
    g_checks += 3;
    bool differ = false;
    if (okOriginal != okOurs) {
        differ = true;
        Detail("case %d: the original %s, ours %s", index, okOriginal ? "ran" : "faulted", okOurs ? "ran" : "faulted");
    } else if (!okOriginal) {
        g_faultsBoth++;
        const Workspace *o = g_resultOriginal;
        int last = o->logCount < kLogRecords ? o->logCount - 1 : kLogRecords - 1;
        Detail("case %d faulted on both sides, at step %d of %d (last call %s; the original at %08x, ours at %08x)",
               index, o->step, g_p.steps, last >= 0 ? LogName(o->log[last].w[0]) : "none", g_faultAt[0],
               g_faultAt[1]);
    }
    if (generationOriginal != generationOurs) {
        differ = true;
        Detail("case %d: the request generation differs: original %08x, ours %08x", index, generationOriginal,
               generationOurs);
    }
    const Workspace *a = g_resultOriginal, *b = W;
    int records = a->logCount < kLogRecords ? a->logCount : kLogRecords;
    g_checks += records;
    g_records += a->logCount;
    if (a->logCount != b->logCount || a->logHash != b->logHash) {
        differ = true;
        int first = -1;
        for (int i = 0; i < records && i < b->logCount; i++)
            if (memcmp(&a->log[i], &b->log[i], sizeof(Record)) != 0) {
                first = i;
                break;
            }
        Detail("case %d: the calls differ (%d records, hash %08x / %d records, hash %08x), first at %d", index,
               a->logCount, a->logHash, b->logCount, b->logHash, first);
        if (first >= 0) {
            const uint32_t *x = a->log[first].w, *y = b->log[first].w;
            Detail("  original %s %08x %08x %08x %08x %08x", LogName(x[0]), x[1], x[2], x[3], x[4], x[5]);
            Detail("  ours     %s %08x %08x %08x %08x %08x", LogName(y[0]), y[1], y[2], y[3], y[4], y[5]);
        }
    }
    const uint32_t *wa = (const uint32_t *)a, *wb = (const uint32_t *)b;
    size_t words = offsetof(Workspace, log) / 4, first = (size_t)-1;
    int differing = 0;
    for (size_t j = 0; j < words; j++)
        if (wa[j] != wb[j]) {
            if (first == (size_t)-1)
                first = j;
            differing++;
        }
    if (differing != 0) {
        differ = true;
        Detail("case %d: %d words differ, first %s +0x%x: original %08x, ours %08x", index, differing,
               Region(first * 4), (unsigned)(first * 4), wa[first], wb[first]);
    }
    if (differ)
        g_differ++;
    if (a->handle != NULL)
        g_created++;
    g_forced += a->forced;
    for (int i = 0; i < records; i++)
        if (a->log[i].w[0] == L_READ)
            g_reads++;
    if (a->badUnlocks != 0 || a->lockDepth != 0)
        Detail("case %d: the original left the mutex unbalanced (%d bad unlocks, depth %d)", index, a->badUnlocks,
               a->lockDepth);
}

}   // namespace

void SndStreamFileShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_SNDSTREAMFILESHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    W = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_snapshot = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_resultOriginal = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (W == NULL || g_snapshot == NULL || g_resultOriginal == NULL) {
        printf("[sndstreamfileshadow] no memory\n");
        fflush(stdout);
        return;
    }
    g_seed = 0x5eed5eedu;
    MakeSources();

    int replaced = XbeOriginal_RestoreRange(0x0014aba0u, 0x0014bee0u, true);
    XbeOriginal_RestoreRange(0x0014aba0u, 0x0014bee0u, false);
    replaced += XbeOriginal_RestoreRange(0x001503b0u, 0x001503e0u, true);
    XbeOriginal_RestoreRange(0x001503b0u, 0x001503e0u, false);

    uint32_t savedGeneration = *(uint32_t *)(uintptr_t)kGeneration;
    HooksInstall();
    const int cases = 120;
    for (int i = 0; i < cases; i++)
        RunCase(i);
    HooksRemove();
    *(uint32_t *)(uintptr_t)kGeneration = savedGeneration;

    for (int i = 0; i < kFiles; i++)
        if (g_files[i].file != NULL) {
            fclose(g_files[i].file);
            g_files[i].file = NULL;
        }
    free(g_synthetic);
    free(g_memoryStream);
    g_synthetic = g_memoryStream = NULL;
    VirtualFree(W, 0, MEM_RELEASE);
    VirtualFree(g_snapshot, 0, MEM_RELEASE);
    VirtualFree(g_resultOriginal, 0, MEM_RELEASE);
    W = NULL;

    printf("[sndstreamfileshadow] STREAM file side: %d cases, %d checks, %d differ (%d streams created, %d calls "
           "logged, %d FILESYS reads, %d forced idle, %d faulted on both sides; %d disc files, memory stream %u "
           "bytes from %s; %d of 30 entry points replaced)\n",
           g_cases, g_checks, g_differ, g_created, g_records, g_reads, g_forced, g_faultsBoth, g_discFiles,
           g_memoryStreamSize, g_memoryFromDisc ? "disc" : "synthetic", replaced);
    fflush(stdout);
}
