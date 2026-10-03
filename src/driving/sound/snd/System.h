#ifndef DRIVING_SOUND_SND_SYSTEM_H_
#define DRIVING_SOUND_SND_SYSTEM_H_

// EA's sound library, module A: the system - init and restore, the options, the 100 Hz server, the main-thread
// server clients, the critical section, the SNDMEMI heap, the SNDLINKI lists, the 64-bit helpers and the random
// number generator (docs/driving/sound.md 3.1, 3.2, 4.1). See System.cpp.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// The warning beside a provisional port (the pattern of eagl/anim/AnimUntested.h). Guarded: other sound modules
// may define the same macro.
#ifndef SND_UNTESTED
inline void SndSystemUntested(const char *what) {
    printf("[snd] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define SND_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            SndSystemUntested(what); \
        } \
    } while (0)
#endif

namespace SND {

// A node of an SNDLINKI list: the hardware buffer node (sound.md 2.3), the stream request records. Only the links
// are the list's.
struct LinkNode {
    LinkNode *next;              // +0x00
    LinkNode *prev;              // +0x04
};

// SNDLINKLIST (0xc, MW: SNDLINKLIST)
struct LinkList {
    LinkNode *head;              // +0x00
    LinkNode *tail;              // +0x04
    int32_t count;               // +0x08
};
static_assert(sizeof(LinkList) == 0xc, "SNDLINKLIST is 0xc bytes");

// An SNDMEMI allocation record: offset from the heap's base, rounded size
struct MemEntry {
    int32_t offset;              // +0x00
    int32_t size;                // +0x04
};

// The SNDMEMI heap's header, at the start of the memory SNDSYSI_init is given (pSndHeap, 0x00244f6c). The records
// are a table at the memory's end growing down: record 0 is the last 8 bytes, record -1 the 8 before, ...; they are
// kept in offset order, record 0 the lowest.
struct MemHeap {
    uint8_t *base;               // +0x00 the first 16-byte boundary past the header
    MemEntry *table;             // +0x04 record 0
    int32_t size;                // +0x08 the whole memory
    int32_t limit;               // +0x0c bytes from base the blocks may use (shrinks by 8 per record)
    int32_t lowWater;            // +0x10 the least ever left past the last block
    int32_t count;               // +0x14 minus the number of records
};
static_assert(sizeof(MemHeap) == 0x18, "the SNDMEMI header is 0x18 bytes");

}  // namespace SND

// init and restore, the options
int SNDSYS_getopts(void *opts);                                              // 0x0013d090
int SNDSYS_setops(const void *opts);                                         // 0x0013d0f0
int SNDSYSI_init(void *memory, int size);                                    // 0x0013d140
int SNDSYS_inited(void);                                                     // 0x0013d310 dead
int SNDSYS_restore(void);                                                    // 0x0013d320
int SNDREAL_systemtask(int argument, int ticksLate);                         // 0x0013d3e0
int SNDSYS_vectortoreal(void);                                               // 0x0013d3f0
int SNDSYSI_exithook(void);                                                  // 0x001413d0 Ghidra: ~ASystem
void SNDstopall(void);                                                       // 0x00141390

// the servers
void SNDSYSI_100hzserver(void);                                              // 0x0013b7b0
void SNDSYS_service(void);                                                   // 0x0013f980
typedef void (*SndServerClient)(void);   // a 100 Hz client
void iSNDserveraddclient(SndServerClient client);                              // 0x0013f900
void iSNDserverremoveclient(SndServerClient client);                           // 0x0013f920

// the critical section (SoundMutex, 0x00244fc0)
void SNDSYS_entercritical(void);                                             // 0x0013b950
void SNDSYS_leavecritical(void);                                             // 0x0013b970
void SNDI_mutexalloc(void);                                                  // 0x0013e7a0
void SNDI_mutexlock(void);                                                   // 0x0013e7b0
void SNDI_mutexunlock(void);                                                 // 0x0013e7c0

// the lists
void SNDLINKI_init(SND::LinkList *list);                                     // 0x0013f0a0
void SNDLINKI_push(SND::LinkList *list, SND::LinkNode *node);                // 0x0013f0b0
void SNDLINKI_pushtail(SND::LinkList *list, SND::LinkNode *node);            // 0x0013f0e0
SND::LinkNode* SNDLINKI_pop(SND::LinkList *list);                            // 0x0013f110
void SNDLINKI_remove(SND::LinkList *list, SND::LinkNode *node);              // 0x0013f140

// the heap
void SNDMEMI_init(void *memory, int size);                                   // 0x0013f710
int SNDMEMI_restore(void);                                                   // 0x0013f770 thunk to 0x001429d0
void* SNDMEMI_alloc(int size);                                               // 0x0013f780
void SNDMEMI_free(void *block);                                              // 0x0013f880
void* SNDMEMI_allocthunk(int size);                                          // 0x00141860 Ghidra: SNDMEMI_alloc (CODA_New)
void SNDMEMI_freethunk(void *block);                                         // 0x00141870 Ghidra: SNDMEMI_free (CODA_Delete)
int SNDMEMI_percentused(void);                                               // 0x001429d0 Ghidra: SNDMEMI_restore

// arithmetic and random numbers
uint64_t iSNDmulu64(uint32_t a, uint32_t b);                                 // 0x0013f9e0
uint32_t iSNDdivu64(uint32_t low, uint32_t high, uint32_t divisor);          // 0x0013fa50
void SNDI_randomseeed(int seed);                                             // 0x00141280
uint32_t iSNDrandom(void);                                                   // 0x001412e0
int randrange(int range);                                                    // 0x00142ea0

#endif // DRIVING_SOUND_SND_SYSTEM_H_
