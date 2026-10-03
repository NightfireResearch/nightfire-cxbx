#include "Stream.h"
#include "SndUntested.h"
#include "System.h"

#include "../../platform/FileSys.h"
#include "../../platform/RealPrint.h"
#include "../../platform/RealSystem.h"
#include "../../../helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// STREAM, the file side of EA's sound streaming (docs/driving/sound.md 2.5, 3.5, 4.5): 0x0014aba0..0x0014bee0 and
// 0x001503b0..0x001503e0, ported from the listing.
//
// Three threads touch a STREAM (sound.md 7.5): the main thread (queue, STREAM_get through SNDSTRMI_service, kill,
// destroy), the SND thread (STREAM_release from the packet callbacks) and the FILESYS worker (the completion
// callbacks FUN_0014aee0/af10/b660 and what they call). Only the STREAM's own mutex (+4) serialises them, and only
// in parts - STREAM_get updates the reader outside it, FUN_0014ad20 writes a chunk's size word before taking it.
// Every MUTEX_lock/unlock below is where the original has one, around exactly the same reads and writes; the
// order of the stores inside and outside them follows the listing.
//
// The FILESYS callbacks are handed over as the originals' addresses (0x0014aee0, 0x0014af10, 0x0014b660), which
// jump here: what FILESYS stores is then the same value the original stored.
//
// The register-argument helpers (sound.md 7.1) are naked adaptors under Ghidra's names (AUTOLTCG), preserving
// what the originals preserve; the ports call the C++ cores beside them directly.
// ---------------------------------------------------------------------------------------------------------------

using namespace SND;

namespace {

#define Generation U32_AT(0x002475fc)   // the request id generation, += 0x100, never 0

constexpr uint32_t kMagic = 0x4d525453;   // "STRM"
constexpr uint32_t kWrap = 0xffffffff;    // chunk tag: continue at ringStart
constexpr uint32_t kSkip = 0xfffffffe;    // chunk tag: released or skipped

// The FILESYS completion callbacks, as the originals' addresses
#define OpenDone ((FsCallback)0x0014aee0)     // FUN_0014aee0
#define CloseDone ((FsCallback)0x0014af10)    // FUN_0014af10
#define ReadDone ((FsCallback)0x0014b660)     // FUN_0014b660

#define CrtStrncpy ((char *(*)(char *, const char *, int))0x00133d60)   // the CRT's _strncpy
#define MemFreeImport (*(bool (**)(void *))0x001d1878)                 // MEM_free, the pointer the original jumps through

StrmInternal *Valid(StrmReader *stream) {   // the check every entry starts with
    if (stream == NULL || stream->internal->magic != kMagic)
        return NULL;
    return stream->internal;
}

// The FILESYS operations' user data: the STREAM
int UserData(StrmInternal *s) {
    return int(uintptr_t(s));
}

StrmChunk *ChunkAt(uint8_t *p) {
    return reinterpret_cast<StrmChunk *>(p);
}

StrmChunk *Advance(StrmChunk *chunk, uint32_t bytes) {
    return ChunkAt(reinterpret_cast<uint8_t *>(chunk) + bytes);
}

// Where in its 128-byte block an address lies
uint32_t Misalign(const uint8_t *p) {
    return uint32_t(uintptr_t(p)) & 0x7f;
}

// A chunk's size padded so that it ends on a 128-byte boundary, from where in its block it starts
uint32_t PadTo128(uint32_t misalign, uint32_t size) {
    return ((misalign + size + 0x7f) & 0xffffff80) - misalign;
}

}  // namespace

// ---- the register-argument helpers

// bytes released: below the greedy level again, read at the greedy priority (the original's 0x0014aba0)
void SndStream_ReleaseBytes(StrmInternal *s, int bytes) {
    MUTEX_lock(&s->mutex);
    int before = s->bytesBuffered;
    int after = before - bytes;
    s->bytesBuffered = after;
    MUTEX_unlock(&s->mutex);
    int level = s->greedyLevel;
    if (before < level || after >= level)
        return;
    s->greedy = 1;
    if (s->state == 1)
        FILESYS_priorityop(s->operation, s->priorityGreedy);
}

// AUTOLTCG
__declspec(naked) void FUN_0014aba0() {
    __asm {
        push dword ptr [esp + 4]
        push esi
        call SndStream_ReleaseBytes
        add esp, 8
        ret
    }
}

StrmRequest* SndStream_PopFree(StrmInternal *s) {
    MUTEX_lock(&s->mutex);
    StrmRequest *request = s->freeList;
    if (request == NULL) {
        MUTEX_unlock(&s->mutex);
        return request;
    }
    s->freeList = request->next;
    uint32_t generation = Generation + 0x100;
    Generation = generation;
    if (generation == 0)
        Generation = 0x100;
    request->id = (request->id & 0xff) | Generation;
    MUTEX_unlock(&s->mutex);
    return request;
}

// AUTOLTCG
__declspec(naked) void FUN_0014ac00() {
    __asm {
        push edi
        call SndStream_PopFree
        add esp, 4
        ret
    }
}

void SndStream_Append(StrmInternal *s, StrmRequest *request) {
    request->state = 1;
    request->next = NULL;
    MUTEX_lock(&s->mutex);
    StrmRequest *tail = s->tail;
    if (tail == NULL) {
        request->prev = tail;
        s->head = request;
        s->current = request;
        s->tail = request;
    } else {
        request->prev = tail;
        s->tail->next = request;
        s->tail = request;
    }
    MUTEX_unlock(&s->mutex);
}

// AUTOLTCG
__declspec(naked) void FUN_0014ac70() {
    __asm {
        push esi
        push edi
        call SndStream_Append
        add esp, 8
        ret
    }
}

// unlink a request (no lock: the caller holds it) and put it on the free list
void SndStream_FreeRequest(StrmRequest *request, StrmInternal *s) {
    if (request == s->head)
        s->head = request->next;
    else
        request->prev->next = request->next;
    if (request == s->tail)
        s->tail = request->prev;
    else
        request->next->prev = request->prev;
    if (request == s->current)
        s->current = request->next != NULL ? request->next : request->prev;
    request->state = 0;
    request->next = s->freeList;
    s->freeList = request;
}

// AUTOLTCG
__declspec(naked) void freerequest() {
    __asm {
        push eax
        push ecx
        push ecx
        push eax
        call SndStream_FreeRequest
        add esp, 8
        pop ecx
        pop eax
        ret
    }
}

// Deliver the chunks the last read completed: each whole chunk between parsePos and writePos is matched against
// the filters and counted to its reader, or marked skipped; the request's end chunk is padded to 128 bytes and
// ends it (1). A cancelled request's chunks are left as they are.
int SndStream_Deliver(StrmInternal *s) {
    StrmRequest *current = s->current;
    if (s->writePos - s->parsePos < 8)
        return 0;
    for (;;) {
        StrmChunk *chunk = ChunkAt(s->parsePos);
        uint32_t size = chunk->size;
        if ((size & 0xff000000) != 0) {
            chunk->tag = current->endTag;
            size = 8;
            chunk->size = 8;
        }
        uint8_t *at = s->parsePos;
        if (at + size > s->writePos)
            return 0;
        uint32_t tag = chunk->tag;
        if (tag == current->endTag)
            size = PadTo128(Misalign(at), size);
        int reader = -2;
        bool found = false;
        for (int i = 0; i < s->numFilters; i++) {
            StrmFilter *filter = &s->filters[i];
            if ((filter->mask & tag) == filter->value) {
                found = true;
                reader = filter->reader;
                break;
            }
        }
        int cancelled;
        if (!found || reader < 0) {
            MUTEX_lock(&s->mutex);
            cancelled = current->state == 4;
            if (!cancelled) {
                chunk->tag = kSkip;
                s->parsePos += size;
            }
        } else {
            chunk->size = (uint32_t(reader) << 24) | size;
            MUTEX_lock(&s->mutex);
            cancelled = current->state == 4;
            if (!cancelled) {
                StrmReader *handle = &s->readers[reader - 1];
                int bytes = handle->bytes + int(size);
                handle->bytes = bytes;
                if (bytes == int(size))
                    handle->next = chunk;
                s->parsePos += size;
                int level = s->greedyLevel;
                int before = s->bytesBuffered;
                int after = before + int(size);
                s->bytesBuffered = after;
                if (before < level && after >= level)
                    s->greedy = 0;
            }
        }
        MUTEX_unlock(&s->mutex);
        if (cancelled) {
            if (chunk->tag == current->endTag)
                return 0;
            chunk->size = PadTo128(Misalign(s->parsePos), size) | (uint32_t(reader) << 24);
            return 0;
        }
        if (chunk->tag == current->endTag)
            return 1;
        if (s->writePos - s->parsePos < 8)
            return 0;
    }
}

// AUTOLTCG
__declspec(naked) void FUN_0014ad20() {
    __asm {
        push eax
        call SndStream_Deliver
        add esp, 4
        ret
    }
}

// Start the next queued request: a memory request is copied at once; a file already open is read on; otherwise
// open it, closing the open one first (the open continues from FUN_0014af10). No request left: idle.
void SndStream_NextRequest(StrmInternal *s, int priority) {
    StrmRequest *request = NULL;
    MUTEX_lock(&s->mutex);
    int idle = 1;
    StrmRequest *current = s->current;
    if (current != NULL) {
        bool ready = true;
        if (current->state != 1) {
            current = current->next;
            if (current == NULL)
                ready = false;
            else
                s->current = current;
        }
        if (ready) {
            request = s->current;
            request->start = s->parsePos;
            request->state = 2;
            idle = 0;
        } else {
            s->state = 0;
        }
    } else {
        s->state = 0;
    }
    MUTEX_unlock(&s->mutex);
    if (idle != 0)
        return;
    s->writePos = s->parsePos;
    if (request->fromMemory == 1) {
        s->fileOffset = 0;
        FUN_0014b730(s, priority);
        return;
    }
    s->fileOffset = request->offset;
    if (strcmp(request->name, s->fileName) == 0) {
        FUN_0014b730(s, priority);
        return;
    }
    for (int i = 0;; i++) {   // the original's inline strcpy
        char c = request->name[i];
        s->fileName[i] = c;
        if (c == 0)
            break;
    }
    if (s->fileSlot == 0) {
        unsigned operation = FILESYS_open(s->fileName, 1, priority, UserData(s));
        s->operation = operation;
        if (operation != 0)
            FILESYS_callbackop(operation, OpenDone);
        return;
    }
    unsigned operation = FILESYS_close(s->fileSlot, priority, UserData(s));
    s->operation = operation;
    if (operation != 0)
        FILESYS_callbackop(operation, CloseDone);
}

// AUTOLTCG
__declspec(naked) void FUN_0014af50() {
    __asm {
        push dword ptr [esp + 4]
        push esi
        call SndStream_NextRequest
        add esp, 8
        ret
    }
}

// ---- the FILESYS callbacks and the read loop

// FUNC_AT(0x0014aee0)
void FUN_0014aee0(unsigned operation, int status, StrmInternal *s) {
    (void)operation;
    (void)status;
    int slot = FILESYS_completeop(s->operation);
    s->fileSlot = slot;
    if (slot != 0)
        FUN_0014b730(s, s->priorityGreedy);
}

// FUNC_AT(0x0014af10)
void FUN_0014af10(unsigned operation, int status, StrmInternal *s) {
    (void)operation;
    (void)status;
    FILESYS_completeop(s->operation);
    unsigned next = FILESYS_open(s->fileName, 1, s->priorityGreedy, UserData(s));
    s->operation = next;
    if (next != 0)
        FILESYS_callbackop(next, OpenDone);
}

// FUNC_AT(0x0014b660)
void FUN_0014b660(unsigned operation, int status, StrmInternal *s) {
    (void)operation;
    (void)status;
    StrmRequest *request = s->current;
    int got;
    int done;
    if (request->fromMemory == 1) {
        got = s->readSize;
        done = s->fileOffset + got >= request->offset;
    } else {
        got = FILESYS_completeop(s->operation);
        done = got < s->readSize;
    }
    s->fileOffset += got;
    s->writePos += got;
    int ended = SndStream_Deliver(s);
    if (request->state == 4) {
        SndStream_NextRequest(s, s->priorityGreedy);
        return;
    }
    if (done == 0 && ended == 0) {
        FUN_0014b730(s, s->priorityGreedy);
        return;
    }
    MUTEX_lock(&s->mutex);
    if (request->state != 4)
        request->state = 3;
    MUTEX_unlock(&s->mutex);
    SndStream_NextRequest(s, s->priorityGreedy);
}

// Retire finished requests whose data has been consumed, then read into the free space: at the ring's end the
// undelivered bytes are moved to the start (on a file request keeping their offset within 128 bytes) and a wrap
// marker left behind. Too little space: wait (state 2) for STREAM_release.
// FUNC_AT(0x0014b730)
void FUN_0014b730(StrmInternal *s, int priority) {
    if (s->readPos != s->parsePos) {
        do {
            StrmChunk *chunk = ChunkAt(s->readPos);
            uint32_t tag = chunk->tag;
            if (tag == kWrap)
                s->readPos = s->ringStart;
            else if (tag == kSkip)
                s->readPos = (uint8_t *)Advance(chunk, chunk->size);
            else
                break;
        } while (s->readPos != s->parsePos);
    }

    MUTEX_lock(&s->mutex);
    StrmRequest *head = s->head;
    while (head->next != NULL) {
        StrmRequest *next = head->next;
        if (next->state == 1)
            break;
        // retired once the byte before the next request's data is outside the unconsumed part of the ring
        uint8_t *mark = next->start - 1;
        uint8_t *write = s->writePos;
        uint8_t *read = s->readPos;
        bool retire;
        if (read > write)
            retire = mark < read && mark >= write;
        else
            retire = mark < read || mark >= write;
        if (!retire)
            break;
        if (head == s->head)
            s->head = next;
        else
            head->prev->next = next;
        StrmRequest *prev = head->prev;
        if (head == s->tail)
            s->tail = prev;
        else
            head->next->prev = prev;
        if (head == s->current)
            s->current = head->next != NULL ? head->next : head->prev;
        head->state = 0;
        head->next = s->freeList;
        s->freeList = head;
        head = s->head;
    }
    MUTEX_unlock(&s->mutex);

    uint8_t *read = s->readPos;
    uint8_t *write = s->writePos;
    int space;
    if (read > write) {
        space = int(read - write) - 0x81;
    } else {
        space = int(s->ringEnd - write) - 0x80;
        if (space < s->readChunk) {
            int pending = int(write - s->parsePos);
            int fromMemory = s->current->fromMemory;
            if (fromMemory == 1) {
                if (int(read - s->ringStart) < pending + 1) {
                    s->state = 2;
                    return;
                }
            } else {
                if (int(read - s->ringStart) - 0x80 < pending + 1) {
                    s->state = 2;
                    return;
                }
            }
            int misalign = pending % 128;
            if (misalign != 0 && fromMemory != 1)
                s->ringStart = s->ringBase - misalign + 0x80;
            else
                s->ringStart = s->ringBase;
            MEM_copy(s->ringStart, s->parsePos, pending);
            StrmChunk *old = ChunkAt(s->parsePos);
            old->tag = kWrap;
            old->size = 8;
            uint8_t *start = s->ringStart;
            s->parsePos = start;
            s->writePos = start + pending;
            if (ChunkAt(s->readPos)->tag == kWrap) {
                s->readPos = start;
                space = int(s->ringEnd - s->writePos) - 0x80;
            } else {
                space = int(s->readPos - s->writePos) - 1;
            }
        }
    }
    if (space < s->readChunk) {
        s->state = 2;
        return;
    }
    StrmRequest *request = s->current;
    int offset = s->fileOffset;
    if (request->fromMemory == 1) {
        int size = request->offset;
        if (offset + space > size)
            s->readSize = size - offset;
        else
            s->readSize = space;
        MEM_copy(s->writePos, request->memory, s->readSize);
        request->memory += s->readSize;
        FUN_0014b660(0, 0, s);
        return;
    }
    int count = s->readChunk;
    s->readSize = count;
    unsigned operation = FILESYS_read(s->fileSlot, offset, s->writePos, count, priority, UserData(s));
    s->operation = operation;
    if (operation == 0)
        return;
    FILESYS_callbackop(operation, ReadDone);
}

// ---- the API

// The header (0x190), the readers, 0x80 for aligning the ring, the requests and the filters
// FUNC_AT(0x0014b090)
int STREAM_overhead(int requests, int filters, int readers) {
    return (readers + 0x21) * 16 + requests * 0x124 + filters * 12;
}

// FUNC_AT(0x0014b0c0)
SND::StrmReader* STREAM_create(int requests, int filters, int readers, StrmInternal *memory, int size) {
    int requestBytes = requests * 0x124;
    int filterBytes = filters * 12;
    int ring = size - ((readers + 0x21) * 16 + filterBytes + requestBytes);
    if (ring < 0x1800 || requests < 2 || requests > 0x100 || filters < 1 || filters > 0x10 || readers < 1 ||
        readers > filters)
        return NULL;
    StrmInternal *s = memory;
    s->magic = kMagic;
    MUTEX_create(&s->mutex);
    s->numFilters = filters;
    s->numReaders = readers;
    StrmRequest *requestRecords = (StrmRequest *)(s + 1);
    StrmFilter *filterRecords = (StrmFilter *)(requestRecords + requests);
    s->filters = filterRecords;
    StrmReader *readerRecords = (StrmReader *)(filterRecords + filters);
    uint8_t *ringBase = (uint8_t *)((uintptr_t(readerRecords + readers) & 0xffffff80) + 0x80);   // 128-aligned
    s->readers = readerRecords;
    s->ringBase = ringBase;
    s->ringStart = ringBase;
    s->readPos = ringBase;
    s->parsePos = ringBase;
    s->writePos = ringBase;
    s->requests = requestRecords;
    s->numRequests = requests;
    s->ringEnd = (uint8_t *)s + size;
    s->state = 0;
    s->priorityIdle = 0x96;
    s->priorityGreedy = 0x32;
    s->greedyLevel = 0;
    s->greedy = 0;
    s->bytesBuffered = 0;
    s->head = NULL;
    s->current = NULL;
    s->tail = NULL;
    s->freeList = requestRecords;
    MEM_clear(s->fileName, sizeof(s->fileName));
    s->fileSlot = 0;
    if (ring < 0x4000)
        s->readChunk = 0x800;
    else
        s->readChunk = ring >= 0x8000 ? 0x2000 : 0x1000;
    for (int i = 0; i < requests; i++) {
        StrmRequest *request = &s->requests[i];
        request->id = i;
        request->state = 0;
        request->next = &s->requests[i + 1];
    }
    s->requests[requests - 1].next = NULL;
    for (int i = 0; i < filters; i++) {
        StrmFilter *filter = &s->filters[i];
        filter->mask = 0;
        filter->value = 0;
        filter->reader = 1;
    }
    for (int i = 0; i < readers; i++) {
        StrmReader *handle = &s->readers[i];
        handle->internal = s;
        handle->index = i + 1;
        handle->bytes = 0;
    }
    return s->readers;
}

// FUNC_AT(0x0014b2a0)
void STREAM_setfilter(SND::StrmReader *stream, int index, uint32_t mask, uint32_t value, int reader) {
    SND_UNTESTED("STREAM_setfilter");
    StrmInternal *s = Valid(stream);
    if (s == NULL || index < 1 || index > s->numFilters)
        return;
    if (index == s->numFilters && (mask | value) != 0)   // the last filter stays the catch-all
        return;
    if (reader < 1 && reader != -1 && reader != -2)
        return;
    if (reader > s->numReaders || s->state != 0)
        return;
    StrmFilter *filter = &s->filters[index - 1];
    filter->mask = mask;
    filter->value = value;
    filter->reader = reader;
}

// FUNC_AT(0x0014b310)
void STREAM_setpriority(SND::StrmReader *stream, int idle, int greedy) {
    SND_UNTESTED("STREAM_setpriority");
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return;
    s->priorityIdle = idle;
    s->priorityGreedy = greedy;
}

// FUNC_AT(0x0014b340)
void STREAM_setgreedystate(SND::StrmReader *stream, int greedy) {
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return;
    s->greedy = greedy;
    if (greedy == 0 || s->state != 1)
        return;
    FILESYS_priorityop(s->operation, s->priorityGreedy);
}

// FUNC_AT(0x0014b380)
SND::StrmReader* STREAM_taphandle(SND::StrmReader *stream, int reader) {
    SND_UNTESTED("STREAM_taphandle");
    StrmInternal *s = Valid(stream);
    if (s == NULL || reader < 1 || reader > s->numReaders)
        return NULL;
    return &s->readers[reader - 1];
}

// FUNC_AT(0x0014b3b0)
uint32_t STREAM_queuefile(SND::StrmReader *stream, const char *name, int offset, uint32_t endTag) {
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return 0;
    StrmRequest *request = SndStream_PopFree(s);
    if (request == NULL)
        return 0;
    request->fromMemory = 0;
    CrtStrncpy(request->name, name, 0xff);
    request->offset = offset;
    request->endTag = endTag;
    SndStream_Append(s, request);
    MUTEX_lock(&s->mutex);
    int state = s->state;
    if (state == 0)
        s->state = 1;
    MUTEX_unlock(&s->mutex);
    if (state == 0) {
        if (s->greedy != 0)
            SndStream_NextRequest(s, s->priorityGreedy);
        else
            SndStream_NextRequest(s, s->priorityIdle);
    }
    return request->id;
}

// FUNC_AT(0x0014b470)
uint32_t STREAM_queuemem(SND::StrmReader *stream, const uint32_t *memory, int size, uint32_t endTag) {
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return 0;
    StrmRequest *request = SndStream_PopFree(s);
    if (request == NULL)
        return 0;
    if (size == 0) {   // the size of the chunks up to and including the end chunk
        const StrmChunk *chunk = reinterpret_cast<const StrmChunk *>(memory);
        while (chunk->tag != endTag) {
            uint32_t length = chunk->size;
            chunk = reinterpret_cast<const StrmChunk *>(reinterpret_cast<const uint8_t *>(chunk) + length);
            size += int(length);
        }
        size += int(chunk->size);
    }
    request->endTag = endTag;
    request->fromMemory = 1;
    request->memory = reinterpret_cast<const uint8_t *>(memory);
    request->offset = size;
    SndStream_Append(s, request);
    MUTEX_lock(&s->mutex);
    int state = s->state;
    if (state == 0)
        s->state = 1;
    MUTEX_unlock(&s->mutex);
    if (state == 0)
        SndStream_NextRequest(s, state);
    return request->id;
}

// The reader's next chunk (its size word cleared of the reader byte), and the reader moved on to its following
// chunk - outside the mutex, as the original does.
// FUNC_AT(0x0014b520)
uint32_t* STREAM_get(SND::StrmReader *stream) {
    if (stream == NULL)
        return NULL;
    StrmInternal *s = stream->internal;
    if (s->magic != kMagic || stream->bytes == 0)
        return NULL;
    StrmChunk *chunk = stream->next;
    uint32_t size = chunk->size & 0xffffff;
    chunk->size = size;
    MUTEX_lock(&s->mutex);
    int left = stream->bytes - int(size);
    stream->bytes = left;
    MUTEX_unlock(&s->mutex);
    if (left > 0) {
        uint32_t mine = uint32_t(stream->index) << 24;
        StrmChunk *p = Advance(chunk, size);
        while ((p->size & 0xff000000) != mine) {
            if (p->tag == kWrap)
                p = ChunkAt(s->ringStart);
            else
                p = Advance(p, p->size & 0xffffff);
        }
        stream->next = p;
    }
    return &chunk->tag;
}

// FUNC_AT(0x0014b5d0)
int STREAM_gettable(SND::StrmReader *stream) {
    if (Valid(stream) == NULL)
        return 0;
    return stream->bytes;
}

// FUNC_AT(0x0014b5f0)
int STREAM_state(SND::StrmReader *stream) {
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return 0;
    return s->state;
}

// FUNC_AT(0x0014b610)
bool STREAM_isendofstream(SND::StrmReader *stream) {
    SND_UNTESTED("STREAM_isendofstream");
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return false;
    return s->state == 0 && stream->bytes == 0;
}

// FUNC_AT(0x0014b640)
int STREAM_buffersize(SND::StrmReader *stream) {
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return 0;
    return int(s->ringEnd - s->ringBase);
}

// FUNC_AT(0x0014b9a0)
void STREAM_setgreedylevel(SND::StrmReader *stream, int level) {
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return;
    int old = s->greedyLevel;
    s->greedyLevel = level;
    int buffered = s->bytesBuffered;
    int below = buffered < level;
    int wasBelow = buffered < old;
    if (wasBelow != below)
        STREAM_setgreedystate(stream, below);
}

// FUNC_AT(0x0014b9f0)
void STREAM_release(SND::StrmReader *stream, uint32_t *chunkWords) {
    StrmInternal *s = Valid(stream);
    if (s == NULL)
        return;
    StrmChunk *chunk = reinterpret_cast<StrmChunk *>(chunkWords);
    uint8_t *at = reinterpret_cast<uint8_t *>(chunk);
    if (at < s->ringStart || at > s->ringEnd - 8 || chunk->tag == kSkip)
        return;
    uint32_t size = chunk->size;
    chunk->tag = kSkip;
    SndStream_ReleaseBytes(s, int(size));
    MUTEX_lock(&s->mutex);
    int state = s->state;
    if (state == 2)
        s->state = 1;
    MUTEX_unlock(&s->mutex);
    if (state != 2)
        return;
    if (s->greedy != 0)
        FUN_0014b730(s, s->priorityGreedy);
    else
        FUN_0014b730(s, s->priorityIdle);
}

// A queued request is dropped; one being read (or read) is marked cancelled and its chunks taken back from every
// reader: those not yet delivered to the reader are marked skipped in place, delivered ones consumed through
// STREAM_get/STREAM_release.
// FUNC_AT(0x0014ba80)
void STREAM_cancelrequest(SND::StrmReader *stream, uint32_t id) {
    uint8_t *from = NULL, *to = NULL, *readPos = NULL;
    if (stream == NULL)
        return;
    StrmInternal *s = stream->internal;
    if (s->magic != kMagic)
        return;
    MUTEX_lock(&s->mutex);
    int nothing = 1;
    int index = int(id & 0xff);
    if (index < s->numRequests) {
        StrmRequest *request = &s->requests[index];
        if (id == request->id && request->state != 0 && request->state != 4) {
            if (request->state == 1) {
                SndStream_FreeRequest(request, s);
            } else {
                request->state = 4;
                readPos = s->readPos;
                from = readPos;
                if (request != s->head)
                    from = request->start;
                StrmRequest *next = request->next;
                if (next != NULL && next->state != 1)
                    to = next->start;
                else
                    to = s->parsePos;
                nothing = 0;
            }
        }
    }
    MUTEX_unlock(&s->mutex);
    if (nothing != 0)
        return;
    for (int i = 0; i < s->numReaders; i++) {
        StrmReader *handle = &s->readers[i];
        if (handle->bytes <= 0)
            continue;
        uint8_t *at = (uint8_t *)handle->next;
        // which way to take the region [from, to) back from this reader
        bool consume;
        bool decided = false;
        bool mark = false;
        if (readPos > from) {
            if (at >= readPos)
                mark = true;
            else if (at < from)
                mark = true;
        } else {
            if (at >= readPos && at < from)
                mark = true;
        }
        if (mark) {
            uint32_t tagMine = uint32_t(handle->index) << 24;
            uint8_t *p = from;
            if (from == to)
                continue;
            do {
                StrmChunk *chunk = ChunkAt(p);
                if (chunk->tag == kWrap) {
                    p = s->ringStart;
                } else {
                    uint32_t word = chunk->size;
                    uint32_t size = word & 0xffffff;
                    if ((word & 0xff000000) == tagMine) {
                        MUTEX_lock(&s->mutex);
                        handle->bytes -= int(size);
                        MUTEX_unlock(&s->mutex);
                        SndStream_ReleaseBytes(s, int(size));
                        chunk->tag = kSkip;
                        chunk->size = size;
                    }
                    p += size;
                }
            } while (p != to);
            continue;
        }
        if (from > to) {
            if (at >= from)
                consume = true;
            else
                consume = at < to;
            decided = true;
        } else {
            if (at < from)
                continue;
        }
        if (!decided)
            consume = at < to;
        if (!consume)
            continue;
        for (;;) {
            STREAM_release(handle, STREAM_get(handle));
            if (handle->bytes <= 0)
                break;
            at = (uint8_t *)handle->next;
            if (from > to) {
                if (at >= from)
                    continue;
            } else {
                if (at < from)
                    break;
            }
            if (at >= to)
                break;
        }
    }
}

// Every request dropped: queued ones freed, the one being read cancelled, every reader emptied and every chunk
// not yet released marked skipped; a stream waiting for space is reset to idle.
// FUNC_AT(0x0014bcb0)
void STREAM_kill(SND::StrmReader *stream) {
    uint32_t last = 0;
    if (stream == NULL)
        return;
    StrmInternal *s = stream->internal;
    if (s->magic != kMagic)
        return;
    StrmRequest *request = s->tail;
    if (request == NULL)
        return;
    while (request->state == 1 || request->state == 2) {
        STREAM_cancelrequest(stream, request->id);
        request = s->tail;
    }
    while (s->head != s->current) {
        StrmRequest *head = s->head;
        StrmRequest *next = head->next;
        if (head == s->head)
            s->head = next;
        else
            head->prev->next = next;
        StrmRequest *prev = head->prev;
        if (head == s->tail)
            s->tail = prev;
        else
            head->next->prev = prev;
        if (head == s->current)
            s->current = head->next != NULL ? head->next : head->prev;
        head->state = 0;
        head->next = s->freeList;
        s->freeList = head;
    }
    s->current->state = 4;
    for (int i = 0; i < s->numReaders; i++)
        s->readers[i].bytes = 0;

    // FUN_0014aba0 inline, for everything buffered
    int all = s->bytesBuffered;
    MUTEX_lock(&s->mutex);
    int before = s->bytesBuffered;
    int after = before - all;
    s->bytesBuffered = after;
    MUTEX_unlock(&s->mutex);
    int level = s->greedyLevel;
    if (before >= level && after < level) {
        s->greedy = 1;
        if (s->state == 1)
            FILESYS_priorityop(s->operation, s->priorityGreedy);
    }

    uint8_t *p = s->readPos;
    while (p != s->parsePos) {
        StrmChunk *chunk = ChunkAt(p);
        if (chunk->tag == kWrap) {
            p = s->ringStart;
        } else {
            uint32_t size = chunk->size & 0xffffff;
            chunk->tag = kSkip;
            chunk->size = size;
            last = size;
            p += size;
        }
    }
    if (s->state != 2)
        return;
    if (p == s->ringStart) {
        uint8_t *base = s->ringBase;
        s->parsePos = base;
        s->ringStart = base;
        s->state = 0;
        return;
    }
    uint32_t pad = 0x80 - Misalign(s->parsePos);
    if (pad == 0x80)
        pad = 0;
    ChunkAt(s->parsePos - last)->size = last + pad;
    s->parsePos += pad;
    s->state = 0;
}

// FUNC_AT(0x0014be60)
void STREAM_destroy(SND::StrmReader *stream) {
    if (stream == NULL)
        return;
    StrmInternal *s = stream->internal;
    if (s->magic != kMagic)
        return;
    STREAM_kill(stream);
    while (s->state == 1) {   // an operation still in flight: wait for the worker to finish it
        if (THREAD_iscurrent(NULL))
            SYNCTASK_run(0);
        THREAD_yield(0);
    }
    s->magic = 0;
    REALMUTEX_destroy(&s->mutex);
    int slot = s->fileSlot;
    if (slot != 0)
        FILESYS_closesync(slot, 0x64);
}

// ---- dead, the old movie player's (sound.md 6.3)

// FUNC_AT(0x001503b0)
int FUN_001503b0(void) {
    SND_UNTESTED("FUN_001503b0");
    SNDSYS_entercritical();
    SNDSYS_leavecritical();
    return 0;
}

// FUNC_AT(0x001503c0)
bool FUN_001503c0(char *p) {
    SND_UNTESTED("FUN_001503c0");
    *p = 0;
    return MemFreeImport(p);
}
