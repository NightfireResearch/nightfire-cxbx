#ifndef DRIVING_SOUND_SND_SYSTEM_H_
#define DRIVING_SOUND_SND_SYSTEM_H_

// EA's sound library, module A: the system - init and restore, the options, the 100 Hz server, the main-thread
// server clients, the critical section, the SNDMEMI heap, the SNDLINKI lists, the 64-bit helpers and the random
// number generator (docs/driving/sound.md 3.1, 3.2, 4.1). See System.cpp.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "SndUntested.h"

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

// An SNDMEMI allocation record: offset from the heap's base, rounded size (MW: SNDMEMREC)
struct MemEntry {
    int32_t offset;              // +0x00
    int32_t size;                // +0x04
};

// The SNDMEMI heap's header, at the start of the memory SNDSYSI_init is given (pSndHeap, 0x00244f6c; MW:
// SNDMEMSTATE). The records are a table at the memory's end growing down: record 0 is the last 8 bytes, record -1
// the 8 before, ...; they are kept in offset order, record 0 the lowest.
struct MemHeap {
    uint8_t *base;               // +0x00 the first 16-byte boundary past the header
    MemEntry *table;             // +0x04 record 0
    int32_t size;                // +0x08 the whole memory
    int32_t limit;               // +0x0c bytes from base the blocks may use (shrinks by 8 per record)
    int32_t lowWater;            // +0x10 the least ever left past the last block
    int32_t count;               // +0x14 minus the number of records
};
static_assert(sizeof(MemHeap) == 0x18, "the SNDMEMI header is 0x18 bytes");

// The options (0x120 at 0x00244cc8, sound.md 2.6; MW: SNDSYSOPTS): the platform's caps, the settable part (saved at
// 0x00244de8) and the vector table. MW's names where its layout agrees with how this build uses a field; the
// settable part's layout differs from MW's past +0x12.
struct SysCaps {                 // MW: SNDSYSCAP, as SNDPLATFORM_outputcaps fills it
    uint16_t outputRateMin;      // +0x00 8000
    uint16_t outputRateMax;      // +0x02 48000
    uint8_t outputModeMin;       // +0x04 5
    uint8_t outputModeMax;       // +0x05 5
    uint8_t mixerVoicesMax;      // +0x06 0x40
    uint8_t unknown07;
    uint8_t unknown08;
    uint8_t hardwareVoicesMax;   // +0x09 0xc0
    uint8_t middleVoicesMax;     // +0x0a 0
    uint8_t unknown0b[8];
    uint8_t numRenderModes;      // +0x13 2
    uint16_t renderModes[6];     // +0x14 0x420, 0x24
};
static_assert(sizeof(SysCaps) == 0x20, "the caps are 0x20 bytes");

struct SysSet {                  // MW: SNDSYSSET; SNDPLATFORM_outputset clamps it to the caps
    int32_t unknown00;           // +0x00
    uint32_t randomSeed;         // +0x04 SNDI_randomseeed's
    uint16_t maxBanks;           // +0x08 NUM_BANKS (16)
    uint16_t outputRate;         // +0x0a platformSampleRate (48000)
    uint16_t unknown0c;
    uint8_t mixerVoices;         // +0x0e 32
    uint8_t unknown0f;
    uint8_t unknown10;
    uint8_t middleVoices;        // +0x11 mixVoiceOffset1: 0
    uint8_t hardwareVoices;      // +0x12 mixVoiceOffset2: 192 (NUM_VOICES = the three counts' sum)
    uint8_t unknown13;
    uint8_t unknown14;           // +0x14 10 by the caps, clamped to 1..200
    uint8_t unknown15[7];
    uint8_t stealEqualPriority;  // +0x1c SNDVOICEI_alloc steals at equal priority too
    uint8_t unknown1d[4];
    uint8_t unknown21;           // +0x21 0 by the caps
    uint8_t unknown22[5];
    uint8_t maxStreams;          // +0x27 NUM_STREAMS (16, at most 32)
    uint8_t outputMode;          // +0x28 the speakers: 1, 2 ... 6 (the caps say 5, SetOpts writes 1 or 2)
    uint8_t unknown29;           // +0x29 1 by the caps
    uint8_t unknown2a[2];
    uint8_t heapThreshold;       // +0x2c 0x5a (MW: sndheapthreshold)
    uint8_t numRenderModes;      // +0x2d 2
    uint8_t unknown2e[2];
    uint16_t renderModes[6];     // +0x30 0x420, 0x24 (the zero ones dropped by SNDPLATFORM_outputset)
    uint16_t unknown3c[8];
    uint16_t speakerAzimuth[7][6];   // +0x4c row n: the azimuths of n speakers (row 0 unused)
    uint8_t unknownA0[0x48];
};
static_assert(offsetof(SysSet, maxStreams) == 0x27, "NUM_STREAMS is at 0x00244d0f");
static_assert(offsetof(SysSet, speakerAzimuth) == 0x4c, "the speaker azimuths are at 0x00244d34");
static_assert(sizeof(SysSet) == 0xe8, "the settable options are 0xe8 bytes");

struct SysVectors {              // sndopts2 (MW: SNDSYSVEC), every one dummyNullFunction
    uint32_t functions[6];       // the functions' addresses
};

struct SysOpts {
    SysCaps caps;                // +0x000
    SysSet set;                  // +0x020
    SysVectors vectors;          // +0x108
};
static_assert(sizeof(SysOpts) == 0x120, "the options are 0x120 bytes");

}  // namespace SND

// A 100 Hz or main-thread server client, a module's restore hook (SNDSYS_restore calls them)
typedef void (*SndServerClient)(void);
typedef void (*SndRestoreHook)(void);

// init and restore, the options
int SNDSYS_getopts(SND::SysOpts *opts);                                      // 0x0013d090
int SNDSYS_setops(const SND::SysOpts *opts);                                 // 0x0013d0f0
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
