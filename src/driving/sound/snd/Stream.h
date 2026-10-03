#ifndef DRIVING_SOUND_SND_STREAM_H_
#define DRIVING_SOUND_SND_STREAM_H_

// EA's sound library, module E: STREAM, the file side of streaming (docs/driving/sound.md 2.5, 4.5, 7.1, 7.5).
// A STREAM is one block of memory the caller supplies: a header, request records, chunk filters, reader handles
// and a ring of chunks filled through FILESYS (or copied from memory) and handed out chunk by chunk to readers.
// See Stream.cpp.

#include <stddef.h>
#include <stdint.h>

namespace SND {

struct StrmInternal;

// A request (0x124): a file or a memory block queued for reading
struct StrmRequest {
    uint32_t id;                 // +0x000 low byte the record's index, above it a generation (0x002475fc)
    int32_t state;               // +0x004 0 free, 1 queued, 2 reading, 3 done, 4 cancelled
    StrmRequest *prev;           // +0x008
    StrmRequest *next;           // +0x00c on the queue, or the free list
    int32_t fromMemory;          // +0x010 1 STREAM_queuemem, 0 STREAM_queuefile
    char name[0x100];            // +0x014 the file (_strncpy 0xff)
    const uint8_t *memory;       // +0x114 queuemem: the next byte to copy
    int32_t offset;              // +0x118 queuefile: the file offset to start at; queuemem: the size in bytes
    uint32_t endTag;             // +0x11c the chunk tag that ends the request ('SCEl' from SNDSTRMI_queue)
    uint8_t *start;              // +0x120 where in the ring the request's data begins
};
static_assert(sizeof(StrmRequest) == 0x124, "a STREAM request is 0x124 bytes");

// A chunk filter (0xc): a chunk whose (tag & mask) == value goes to a reader (1-based), negative = skip it
struct StrmFilter {
    uint32_t mask;               // +0x0
    uint32_t value;              // +0x4
    int32_t reader;              // +0x8
};
static_assert(sizeof(StrmFilter) == 0xc, "a STREAM filter is 0xc bytes");

// A reader (0x10): what STREAM_create returns (the first) and the game knows as a STREAM *
struct StrmReader {
    StrmInternal *internal;      // +0x0
    int32_t index;               // +0x4 1-based; a delivered chunk's size word carries it in its top byte
    int32_t bytes;               // +0x8 bytes delivered to this reader and not yet taken by STREAM_get
    uint32_t *next;              // +0xc the next chunk STREAM_get hands out (Ghidra: pData)
};
static_assert(sizeof(StrmReader) == 0x10, "a STREAM reader is 0x10 bytes");

// The header (0x190), at the start of the caller's memory, tagged "STRM"; the records and the ring follow it.
// A chunk in the ring is {tag, size | reader << 24} and its data; tag -1 = wrap to ringStart, -2 = released or
// skipped (size = the bytes to step over).
struct StrmInternal {
    uint32_t magic;              // +0x000 0x4d525453 "STRM"; 0 once destroyed
    uint8_t mutex[0x20];         // +0x004 a RealMutex (platform/RealSystem.h)
    StrmRequest *requests;       // +0x024
    int32_t numRequests;         // +0x028 2..256
    StrmFilter *filters;         // +0x02c
    int32_t numFilters;          // +0x030 1..16
    StrmReader *readers;         // +0x034
    int32_t numReaders;          // +0x038 1..numFilters
    uint8_t *ringBase;           // +0x03c 128-byte aligned
    uint8_t *ringStart;          // +0x040 where the ring wraps to: ringBase, or less than 128 bytes past it
    uint8_t *ringEnd;            // +0x044 the end of the caller's memory
    int32_t state;               // +0x048 0 idle, 1 reading, 2 waiting for space
    int32_t priorityIdle;        // +0x04c 0x96
    int32_t priorityGreedy;      // +0x050 0x32
    int32_t greedyLevel;         // +0x054 below this many bytes buffered, read at priorityGreedy
    int32_t greedy;              // +0x058
    int32_t bytesBuffered;       // +0x05c delivered to readers and not released
    uint8_t *readPos;            // +0x060 the oldest chunk not yet released
    uint8_t *parsePos;           // +0x064 the next chunk to deliver
    uint8_t *writePos;           // +0x068 the end of the data read
    StrmRequest *head;           // +0x06c the request queue
    StrmRequest *current;        // +0x070 the request being read
    StrmRequest *tail;           // +0x074
    StrmRequest *freeList;       // +0x078
    char fileName[0x100];        // +0x07c the file open in fileSlot
    int32_t fileSlot;            // +0x17c FILESYS slot, 0 = none open
    int32_t fileOffset;          // +0x180 the next byte of the request to read
    uint32_t operation;          // +0x184 the FILESYS operation in flight
    int32_t readSize;            // +0x188 the bytes the current read asked for
    int32_t readChunk;           // +0x18c 0x800, 0x1000 or 0x2000 by the ring's size
};
static_assert(sizeof(StrmInternal) == 0x190, "the STREAM header is 0x190 bytes");
static_assert(offsetof(StrmInternal, fileName) == 0x7c, "STREAM fileName");
static_assert(offsetof(StrmInternal, fileSlot) == 0x17c, "STREAM fileSlot");

}  // namespace SND

int STREAM_overhead(int requests, int filters, int readers);                                     // 0x0014b090
SND::StrmReader* STREAM_create(int requests, int filters, int readers, SND::StrmInternal *memory,
                                 int size);                                                      // 0x0014b0c0
void STREAM_setgreedystate(SND::StrmReader *stream, int greedy);                               // 0x0014b340
uint32_t STREAM_queuefile(SND::StrmReader *stream, const char *name, int offset, uint32_t endTag);   // 0x0014b3b0
uint32_t STREAM_queuemem(SND::StrmReader *stream, const uint32_t *memory, int size, uint32_t endTag); // 0x0014b470
uint32_t* STREAM_get(SND::StrmReader *stream);                                                  // 0x0014b520
int STREAM_gettable(SND::StrmReader *stream);                                                  // 0x0014b5d0
int STREAM_state(SND::StrmReader *stream);                                                     // 0x0014b5f0
int STREAM_buffersize(SND::StrmReader *stream);                                                // 0x0014b640
void STREAM_setgreedylevel(SND::StrmReader *stream, int level);                                // 0x0014b9a0
void STREAM_release(SND::StrmReader *stream, uint32_t *chunk);                                 // 0x0014b9f0
void STREAM_cancelrequest(SND::StrmReader *stream, uint32_t id);                               // 0x0014ba80
void STREAM_kill(SND::StrmReader *stream);                                                     // 0x0014bcb0
void STREAM_destroy(SND::StrmReader *stream);                                                  // 0x0014be60

// FILESYS completion callbacks (on its worker thread) and the read loop
void FUN_0014aee0(unsigned operation, int status, SND::StrmInternal *s);   // 0x0014aee0 open done: read
void FUN_0014af10(unsigned operation, int status, SND::StrmInternal *s);   // 0x0014af10 close done: open the next
void FUN_0014b660(unsigned operation, int status, SND::StrmInternal *s);   // 0x0014b660 read done
void FUN_0014b730(SND::StrmInternal *s, int priority);                     // 0x0014b730 issue the next read

// Dead (the old movie player's), provisional: SND_UNTESTED
void STREAM_setfilter(SND::StrmReader *stream, int index, uint32_t mask, uint32_t value, int reader);  // 0x0014b2a0
void STREAM_setpriority(SND::StrmReader *stream, int idle, int greedy);                        // 0x0014b310
SND::StrmReader* STREAM_taphandle(SND::StrmReader *stream, int reader);                       // 0x0014b380
bool STREAM_isendofstream(SND::StrmReader *stream);                                            // 0x0014b610
int FUN_001503b0(void);                                                                          // 0x001503b0
bool FUN_001503c0(char *p);                                                                      // 0x001503c0

// The register-argument helpers (sound.md 7.1): adaptors under Ghidra's names (AUTOLTCG), and the C++ under them,
// which the ports call directly.
void FUN_0014aba0();   // ESI = s, (bytes): bytes released; greedy again when that drops below the level
void FUN_0014ac00();   // EDI = s -> EAX: pop a free request, new generation in its id
void FUN_0014ac70();   // EDI = s, ESI = request: append to the queue as queued
void freerequest();    // EAX = request, ECX = s: unlink, back to the free list (EAX, ECX survive)
void FUN_0014ad20();   // EAX = s -> EAX: deliver the chunks read; 1 = the request's end tag was delivered
void FUN_0014af50();   // ESI = s, (priority): start the next request: read on, open, or close and reopen
void SndStream_ReleaseBytes(SND::StrmInternal *s, int bytes);
SND::StrmRequest* SndStream_PopFree(SND::StrmInternal *s);
void SndStream_Append(SND::StrmInternal *s, SND::StrmRequest *request);
void SndStream_FreeRequest(SND::StrmRequest *request, SND::StrmInternal *s);
int SndStream_Deliver(SND::StrmInternal *s);
void SndStream_NextRequest(SND::StrmInternal *s, int priority);

#endif // DRIVING_SOUND_SND_STREAM_H_
