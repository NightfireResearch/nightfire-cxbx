#include "System.h"
#include "Voices.h"
#include "SndGlobals.h"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's sound library, module A (docs/driving/sound.md 3.1, 3.2, 4.1): the system.
//
// - SNDSYSI_init/SNDSYS_restore: the heap, the options (SNDSYS_getopts/setops around the platform's caps), the
//   voice and bank arrays, the critical section, the platform driver, the speaker azimuth tables; restore undoes it
//   in reverse and answers the heap's percentage in use.
// - SNDSYSI_100hzserver: the SND thread's tick (pitch LFO, volume LFO, fade, envelope per voice in use; integer
//   only), SNDSYS_service: the main thread's (SYNCTASK -> SNDREAL_systemtask), with its client list.
// - SNDMEMI: a first-fit heap whose allocation records sit in a table at the memory's end, in offset order.
// - SNDLINKI: doubly linked lists with a count. iSNDmulu64/divu64: 32 x 32 -> 64 and 64 / 32 by hand.
//   iSNDrandom: a six-word add-with-carry generator seeded by SNDI_randomseeed (sic) from .rdata.
//
// Each function is the original at its address, ported from the listing; calls to the other modules (the platform
// driver, platform.system's SYNCTASK/REAL_addexit/memclr, engine.core's dummyNullFunction) go through their
// originals' addresses (the shadow test's fakes are there), function addresses the library stores (the SYNCTASK,
// the exit hook) are the originals' too, so what the rest of the program sees is unchanged. The library's globals
// stay where they are (SndGlobals.h).
//
// devtools/SndSystemShadow.cpp compares the pure helpers, the lists, the heap, the client list and the 100 Hz
// server (on snapshots of the voices, the platform driver replaced by recording fakes) with the originals.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- globals (sound.md 2.6)
#define CapsResult I32_AT(0x00244c28)                  // what SNDPLATFORM_outputcaps answered
#define CapsRead U32_AT(0x00244c2c)                    // set once the caps are read
#define SysTaskAdded U32_AT(0x00244c30)                // systaskadded
#define CriticalNesting U8_AT(0x00244ed3)              // counts up only
#define Num100HzClients I8_AT(0x00244ed4)              // never registered
#define NumServerClients I8_AT(0x00244ed5)
#define ServerClients100Hz ((SndServerClient *)0x00244ee0)   // [6]
#define ServerClients ((SndServerClient *)0x00244ef8)        // [6]
#define RestoreHooks ((SndRestoreHook *)0x00244f20)          // the modules' restore hooks; [3] is BankExitHook
#define RandomState ((uint32_t *)0x00245978)                 // iSNDrandom's six words
#define AuthorByte U8_AT(0x001d9d48)                   // a byte of the SNDAUTHOR string, written 'S' by SNDSYSI_init

// The random generator's starting words, + the seed (0x001d9d30 in the original's .rdata)
const uint32_t kRandomSeeds[6] = { 0xf22d0e56, 0x883126e9, 0xc624dd2f, 0x0702c49c, 0x9e353f7d, 0x6fdf3b64 };

// The functions the library keeps the addresses of: the originals', which jump to ours
const uint32_t kSystemTaskAddress = 0x0013d3e0;   // SNDREAL_systemtask
const uint32_t kExitHookAddress = 0x001413d0;     // ~ASystem
const uint32_t kDummyNullAddress = 0x000d3580;    // dummyNullFunction

int32_t Mul(int32_t a, int32_t b) {   // IMUL r32: wraps
    return int32_t(uint32_t(a) * uint32_t(b));
}

// REP MOVSD, forward a dword at a time (SNDSYSI_init has SNDSYS_getopts copy the options onto themselves)
void CopyDwords(void *to, const void *from, size_t bytes) {
    uint32_t *d = static_cast<uint32_t *>(to);
    const uint32_t *s = static_cast<const uint32_t *>(from);
    for (size_t i = 0; i < bytes / 4; i++)
        d[i] = s[i];
}

// ---- the originals called from here
#define PlatformOutputCaps ((int (*)(void))0x0013da00)          // SNDPLATFORM_outputcaps
#define PlatformOutputSet ((int (*)(void))0x0013dae0)           // SNDPLATFORM_outputset
#define PlatformInit ((int (*)(void))0x0013dc50)                // SNDPLATFORM_init
#define PlatformRestore ((int (*)(void))0x0013ddc0)             // SNDPLATFORM_restore
#define PlatformSetVol ((void (*)(int))0x0013df50)              // SNDPLATFORM_setvol
#define PlatformSetPitch ((int (*)(int))0x0013e320)             // SNDPLATFORM_setpitch
#define MemClear ((void (*)(void *, int))0x0013f600)            // memclr (platform.system)
#define DummyNull ((void (*)(void))0x000d3580)                  // dummyNullFunction (engine.core)
#define SYNCTASK_add ((void (*)(uint32_t, int, int))0x0010aa50)
#define REAL_addexit ((void (*)(uint32_t))0x0010b310)

// The kernel's critical-section calls, through the XBE's import table
typedef void (__stdcall *CriticalSectionCall)(void *section);
#define ImportRtlInitializeCriticalSection (*(CriticalSectionCall *)0x00189cf0)
#define ImportRtlEnterCriticalSection (*(CriticalSectionCall *)0x00189c40)
#define ImportRtlLeaveCriticalSection (*(CriticalSectionCall *)0x00189c3c)

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The servers
// ---------------------------------------------------------------------------------------------------------------

// The SND thread's tick (sound.md 3.2): reclaim finished hardware voices, the 100 Hz clients (none ever), then per
// voice in use: pitch LFO, volume LFO, fade (a fade below zero stops the voice), envelope (the next segment when
// the current one runs out, stop after the last); a volume change is recalculated and sent.
// FUNC_AT(0x0013b7b0)
void SNDSYSI_100hzserver(void) {
    SndTick++;
    iSNDserve();
    for (int i = 0; i < Num100HzClients; i++)
        ServerClients100Hz[i]();

    for (int voice = 0; voice < NumVoices; voice++) {
        SND::Voice *v = &VoiceArray[voice];
        if (v->inUse != 1 || v->handle < 0)
            continue;
        if (v->pitchLfo != NULL) {
            uint8_t position = v->pitchLfoPos + 1;
            v->pitchLfoPos = position;
            if (position >= v->pitchLfoLength)
                v->pitchLfoPos = 0;
            v->detuneLinear = 0;
            iSNDcalcpitch(voice);
            PlatformSetPitch(voice);
        }
        int changed = 0;
        if (v->volLfo != NULL) {
            uint8_t position = v->volLfoPos + 1;
            v->volLfoPos = position;
            changed = 1;
            if (position >= v->volLfoLength)
                v->volLfoPos = 0;
        }
        int32_t step = v->fadeStep;
        if (step != 0) {
            int32_t fade = int32_t(uint32_t(v->fade) + uint32_t(step));
            int32_t target = v->fadeTarget;
            changed = 1;
            v->fade = fade;
            if (step < 0 ? fade <= target : fade >= target) {
                v->fade = target;
                v->fadeStep = 0;
            }
            if (v->fade < 0) {
                SNDstop(v->handle);
                continue;
            }
        }
        int32_t ticks = v->envTicks - 1;
        v->envTicks = ticks;
        if (v->envStep != 0) {
            v->env = int32_t(uint32_t(v->env) + uint32_t(v->envStep));
            changed = 1;
        }
        if (ticks == 0) {
            uint8_t segment = v->envCurrent + 1;
            v->envCurrent = segment;
            if (int8_t(segment) >= int8_t(v->envCount)) {   // both bytes signed
                SNDstop(v->handle);
                continue;
            }
            const SND::EnvSegment *entry = &v->envTable[int8_t(segment)];
            v->envTicks = entry->ticks;
            if (entry->ticks < 0)
                v->envTicks = 0x7fffffff;
            int32_t delta = int32_t((uint32_t(entry->level) << 16) - uint32_t(v->env));
            v->envStep = delta / v->envTicks;   // IDIV: 0 ticks in a segment faults, as in the original
        }
        if (changed) {
            iSNDcalcvol(voice);
            if (v->handle >= 0)
                PlatformSetVol(voice);
        }
    }
}

// The main thread's service (SYNCTASK -> SNDREAL_systemtask): every server client (SNDSTRMI_service while a stream
// exists), once the system is up.
// FUNC_AT(0x0013f980)
void SNDSYS_service(void) {
    if (SystemInited == 0)
        return;
    for (int i = 0; i < NumServerClients; i++)
        ServerClients[i]();
}

// FUNC_AT(0x0013f900)
void iSNDserveraddclient(SndServerClient client) {
    ServerClients[NumServerClients] = client;
    NumServerClients++;
}

// FUNC_AT(0x0013f920)
void iSNDserverremoveclient(SndServerClient client) {
    int8_t count = NumServerClients;
    int i = 0;
    if (count <= 0)
        return;
    while (ServerClients[i] != client) {
        if (++i >= count)
            return;
    }
    count--;
    NumServerClients = count;
    for (; i < NumServerClients; i++)
        ServerClients[i] = ServerClients[i + 1];
}

// FUNC_AT(0x0013d3e0)
int SNDREAL_systemtask(int argument, int ticksLate) {
    (void)argument;
    (void)ticksLate;
    SNDSYS_service();
    return 0;
}

// From ASystem::ASystem: the vector table's slot, the SYNCTASK (once), the exit hook.
// FUNC_AT(0x0013d3f0)
int SNDSYS_vectortoreal(void) {
    uint32_t added = SysTaskAdded;
    SndOptions.vectors.functions[3] = kDummyNullAddress;   // 0x00244ddc
    if (added == 0) {
        SYNCTASK_add(kSystemTaskAddress, 0, 1);
        SysTaskAdded = 1;
    }
    REAL_addexit(kExitHookAddress);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The critical section
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013b950)
void SNDSYS_entercritical(void) {
    SNDI_mutexlock();
    CriticalNesting++;
}

// FUNC_AT(0x0013b970)
void SNDSYS_leavecritical(void) {
    CriticalNesting--;
    SNDI_mutexunlock();
}

// FUNC_AT(0x0013e7a0)
void SNDI_mutexalloc(void) {
    ImportRtlInitializeCriticalSection(SoundMutex);
}

// FUNC_AT(0x0013e7b0)
void SNDI_mutexlock(void) {
    ImportRtlEnterCriticalSection(SoundMutex);
}

// FUNC_AT(0x0013e7c0)
void SNDI_mutexunlock(void) {
    ImportRtlLeaveCriticalSection(SoundMutex);
}

// ---------------------------------------------------------------------------------------------------------------
// Init and restore
// ---------------------------------------------------------------------------------------------------------------

// The options: on the first call the platform's caps and the defaults (16 banks, steal at equal priority), saved;
// then all 0x120 bytes copied out. The caps' result.
// FUNC_AT(0x0013d090)
int SNDSYS_getopts(SND::SysOpts *opts) {
    if (CapsRead == 0) {
        CapsResult = PlatformOutputCaps();
        NumBanks = 0x10;
        SndOptions.set.heapThreshold = 0x5a;
        SndOptions.set.stealEqualPriority = 1;
        CapsRead = 1;
        CopyDwords(&SndSavedSet, &SndOptions.set, sizeof(SND::SysSet));
    }
    int result = CapsResult;
    CopyDwords(opts, &SndOptions, sizeof(SND::SysOpts));
    return result;
}

// The settable part and the vector table copied in, the platform told, the settable part saved.
// FUNC_AT(0x0013d0f0)
int SNDSYS_setops(const SND::SysOpts *opts) {
    CopyDwords(&SndOptions.set, &opts->set, sizeof(SND::SysSet));
    CopyDwords(&SndOptions.vectors, &opts->vectors, sizeof(SND::SysVectors));
    PlatformOutputSet();
    CopyDwords(&SndSavedSet, &SndOptions.set, sizeof(SND::SysSet));
    return 0;
}

// From ASystem::ASystem with the 256 KB audio heap: 0, or the platform's (negative) failure.
// FUNC_AT(0x0013d140)
int SNDSYSI_init(void *memory, int size) {
    AuthorByte = 'S';
    if (SystemInited != 0)
        return 0;
    MemClear(memory, size);
    SNDMEMI_init(memory, size);
    if (NumVoices == 0) {
        int result = SNDSYS_getopts(&SndOptions);
        if (result < 0)
            return result;
        SNDSYS_setops(&SndOptions);
    }
    SNDI_randomseeed(SndOptions.set.randomSeed);
    SNDI_mutexalloc();
    SNDSYS_entercritical();
    VoiceArray = static_cast<SND::Voice *>(SNDMEMI_alloc(NumVoices * int(sizeof(SND::Voice))));
    BankArray = static_cast<SND::BankSlot *>(SNDMEMI_alloc(NumBanks * int(sizeof(SND::BankSlot))));
    SNDSYS_leavecritical();
    SndTick = 0;
    MasterVolume = 0x7f;
    NumUserDataClients = 0;
    Num100HzClients = 0;
    NumServerClients = 0;
    int result = PlatformInit();
    if (result < 0) {
        PlatformRestore();
        DummyNull();
        return result;
    }
    // the channels' default azimuths (65536ths of a turn) for sounds of 2 (by the output mode), 3 (from the
    // options' row for three speakers), 4, 5 and 6 channels
    int8_t mode = OutputMode;
    SystemInited = 1;
    if (mode == 2) {
        ChannelAzimuth[2][0] = 0xc000;
        ChannelAzimuth[2][1] = 0x4000;
    } else {
        ChannelAzimuth[2][0] = 0xe000;
        ChannelAzimuth[2][1] = 0x2000;
    }
    ChannelAzimuth[3][0] = SndOptions.set.speakerAzimuth[3][2];
    ChannelAzimuth[3][1] = SndOptions.set.speakerAzimuth[3][0];
    ChannelAzimuth[3][2] = SndOptions.set.speakerAzimuth[3][1];
    ChannelAzimuth[4][0] = 0xe000;
    ChannelAzimuth[4][1] = 0x2000;
    ChannelAzimuth[4][2] = 0xa000;
    ChannelAzimuth[4][3] = 0x6000;
    ChannelAzimuth[5][0] = 0xe000;
    ChannelAzimuth[5][1] = 0;
    ChannelAzimuth[5][2] = 0x2000;
    ChannelAzimuth[5][3] = 0xa000;
    ChannelAzimuth[5][4] = 0x6000;
    ChannelAzimuth[6][0] = 0xe000;
    ChannelAzimuth[6][1] = 0;
    ChannelAzimuth[6][2] = 0x2000;
    ChannelAzimuth[6][3] = 0xa000;
    ChannelAzimuth[6][4] = 0x6000;
    ChannelAzimuth[6][5] = 0;
    SNDI_precalcaztospkrvol();
    return 0;
}

// Asked only by the old movie player.
// FUNC_AT(0x0013d310)
int SNDSYS_inited(void) {
    SND_UNTESTED("SNDSYS_inited");
    return int8_t(SystemInited);
}

// From ASystem::Shutdown and the exit hook: -14 if not up; else the reverb off, the modules' restore hooks, every
// voice stopped, the platform driver down, the arrays freed; the heap's percentage in use.
// FUNC_AT(0x0013d320)
int SNDSYS_restore(void) {
    if (SystemInited == 0)
        return -14;
    SNDfxinitbus(0, 0, 0, -1, -1);
    // the modules' restore hooks, called with no argument in this order (0x00244f20, f24, f28, f34, f38 - the
    // streams' - and f30) ...
    static const int kOrder[6] = { 0, 1, 2, 5, 6, 4 };
    for (int i = 0; i < 6; i++) {
        SndRestoreHook hook = RestoreHooks[kOrder[i]];
        if (hook != NULL)
            hook();
    }
    SNDstopall();
    // ... and the bank module's, with -1
    int (*bankExit)(int) = BankExitHook;
    if (bankExit != NULL)
        bankExit(-1);
    PlatformRestore();
    SNDSYS_entercritical();
    SNDMEMI_free(VoiceArray);
    SNDMEMI_free(BankArray);
    SNDSYS_leavecritical();
    int used = SNDMEMI_restore();
    SystemInited = 0;
    DummyNull();
    return used;
}

// The exit hook REAL_addexit is given (Ghidra: ~ASystem): a jump to SNDSYS_restore.
// FUNC_AT(0x001413d0)
int SNDSYSI_exithook(void) {
    return SNDSYS_restore();
}

// FUNC_AT(0x00141390)
void SNDstopall(void) {
    SNDSYS_entercritical();
    for (int i = 0; i < NumVoices; i++)
        SNDstop(VoiceArray[i].handle);
    SNDSYS_leavecritical();
}

// ---------------------------------------------------------------------------------------------------------------
// SNDLINKI
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013f0a0)
void SNDLINKI_init(SND::LinkList *list) {
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

// FUNC_AT(0x0013f0b0)
void SNDLINKI_push(SND::LinkList *list, SND::LinkNode *node) {
    node->next = list->head;
    node->prev = NULL;
    SND::LinkNode *head = list->head;
    if (head != NULL) {
        head->prev = node;
        list->head = node;
        list->count++;
        return;
    }
    list->tail = node;
    list->head = node;
    list->count++;
}

// FUNC_AT(0x0013f0e0)
void SNDLINKI_pushtail(SND::LinkList *list, SND::LinkNode *node) {
    node->next = NULL;
    node->prev = list->tail;
    SND::LinkNode *tail = list->tail;
    if (tail != NULL) {
        tail->next = node;
        list->tail = node;
        list->count++;
        return;
    }
    list->head = node;
    list->tail = node;
    list->count++;
}

// The head, unlinked (its own links are left as they were), or NULL.
// FUNC_AT(0x0013f110)
SND::LinkNode* SNDLINKI_pop(SND::LinkList *list) {
    SND::LinkNode *node = list->head;
    if (node == NULL)
        return NULL;
    SND::LinkNode *next = node->next;
    list->head = next;
    if (next == NULL) {
        list->tail = NULL;
        list->count--;
        return node;
    }
    next->prev = NULL;
    list->count--;
    return node;
}

// FUNC_AT(0x0013f140)
void SNDLINKI_remove(SND::LinkList *list, SND::LinkNode *node) {
    if (node == list->head)
        list->head = list->head->next;
    if (node == list->tail)
        list->tail = list->tail->prev;
    if (node->prev != NULL)
        node->prev->next = node->next;
    if (node->next != NULL)
        node->next->prev = node->prev;
    list->count--;
}

// ---------------------------------------------------------------------------------------------------------------
// SNDMEMI
// ---------------------------------------------------------------------------------------------------------------

// The header at the memory's start (the caller has cleared it: the record count is not set here).
// FUNC_AT(0x0013f710)
void SNDMEMI_init(void *memory, int size) {
    SND::MemHeap *heap = static_cast<SND::MemHeap *>(memory);
    SndHeap = heap;
    heap->size = size;
    SND::MemEntry *table = (SND::MemEntry *)((uint8_t *)memory + size - 8);
    SndHeap->table = table;
    SndHeap->base = (uint8_t *)memory + sizeof(SND::MemHeap);
    SndHeap->base = SndHeap->base + 0xf;
    SndHeap->base = (uint8_t *)((uintptr_t)SndHeap->base & ~uintptr_t(0xf));
    int32_t left = size - 0xf;
    SndHeap->limit = left - 0x20;
    SndHeap->lowWater = left;
}

// First fit in offset order, the size rounded up to 16; NULL if nothing fits.
// FUNC_AT(0x0013f780)
void* SNDMEMI_alloc(int size) {
    SND::MemHeap *heap = SndHeap;
    int32_t count = heap->count;
    int32_t rounded = (uint32_t(size) + 0xf) & ~0xfu;
    int32_t index = 0;
    int32_t start, gap;
    if (count == 0) {
        gap = heap->limit;
        start = count;
    } else {
        bool found = false;
        if (count < 0) {
            SND::MemEntry *table = heap->table;
            int32_t limit = heap->limit;
            do {
                if (index == 0) {
                    gap = table[0].offset;
                    start = 0;
                } else {
                    start = uint32_t(table[index + 1].size) + uint32_t(table[index + 1].offset);
                    gap = uint32_t(table[index].offset) - uint32_t(start);
                }
                if (uint32_t(gap) + uint32_t(start) > uint32_t(limit))
                    gap = uint32_t(limit) - uint32_t(start);
                if (rounded <= gap) {
                    found = true;
                    break;
                }
                count = heap->count;
                index--;
            } while (index > count);
        }
        if (found) {
            // make room at 'index': the records below it move down one
            for (int32_t i = heap->count; i < index; i++) {
                SND::MemEntry *e = &SndHeap->table[i];
                e->offset = e[1].offset;
                e->size = e[1].size;
                heap = SndHeap;
            }
        } else {
            // past the last block
            SND::MemEntry *last = &heap->table[index + 1];
            start = uint32_t(last->size) + uint32_t(last->offset);
            gap = uint32_t(heap->limit) - uint32_t(start);
            if (rounded > gap)
                return NULL;
        }
    }
    if (count == 0 && rounded > gap)
        return NULL;
    SND::MemEntry *e = &heap->table[index];
    e->offset = start;
    e->size = rounded;
    SndHeap->count--;
    SndHeap->limit -= 8;
    heap = SndHeap;
    int32_t left = uint32_t(heap->limit) - uint32_t(start) - uint32_t(rounded);
    void *block = heap->base + start;
    if (left < heap->lowWater)
        heap->lowWater = left;
    return block;
}

// The block's record removed (a pointer the heap did not give is ignored).
// FUNC_AT(0x0013f880)
void SNDMEMI_free(void *block) {
    SND::MemHeap *heap = SndHeap;
    int32_t count = heap->count;
    int32_t offset = (uint8_t *)block - heap->base;
    int32_t index = 0;
    if (count >= 0)
        return;
    SND::MemEntry *e = heap->table;
    while (e->offset != offset) {
        index--;
        e--;
        if (index <= count)
            return;
    }
    heap->count = count + 1;
    SndHeap->limit += 8;
    while (index > SndHeap->count) {
        SND::MemEntry *t = &SndHeap->table[index];
        t->offset = t[-1].offset;
        t->size = t[-1].size;
        index--;
    }
}

// The jump SNDSYS_restore calls (Ghidra: SNDMEMI_restore, thunk)
// FUNC_AT(0x0013f770)
int SNDMEMI_restore(void) {
    return SNDMEMI_percentused();
}

// The decoders' allocator, CODA_New (MIX_create stores this address)
// FUNC_AT(0x00141860)
void* SNDMEMI_allocthunk(int size) {
    return SNDMEMI_alloc(size);
}

// ... and CODA_Delete
// FUNC_AT(0x00141870)
void SNDMEMI_freethunk(void *block) {
    SNDMEMI_free(block);
}

// The most of the heap ever in use, in percent (Ghidra: SNDMEMI_restore; candidate SNDMEMI_percentused).
// FUNC_AT(0x001429d0)
int SNDMEMI_percentused(void) {
    SND::MemHeap *heap = SndHeap;
    int32_t size = heap->size;
    return Mul(size - heap->lowWater, 100) / size;
}

// ---------------------------------------------------------------------------------------------------------------
// Arithmetic and random numbers
// ---------------------------------------------------------------------------------------------------------------

// 32 x 32 -> 64 from four 16 x 16 products (exact)
// FUNC_AT(0x0013f9e0)
uint64_t iSNDmulu64(uint32_t a, uint32_t b) {
    uint32_t aLow = a & 0xffff, aHigh = a >> 16;
    uint32_t bLow = b & 0xffff, bHigh = b >> 16;
    uint32_t lowHigh = bLow * aHigh;    // eax
    uint32_t lowLow = bLow * aLow;      // esi
    uint32_t highHigh = bHigh * aHigh;  // edi
    uint32_t highLow = bHigh * aLow;    // ecx
    uint32_t low = ((lowHigh + highLow) << 16) + lowLow;
    uint32_t high = (lowHigh & 0xffff) + (lowLow >> 16) + (highLow & 0xffff);
    high = (high >> 16) + highHigh + (lowHigh >> 16) + (highLow >> 16);
    return (uint64_t(high) << 32) | low;
}

// (high:low) / divisor by 32 steps of shift and subtract: the quotient's low 32 bits (high must be below the
// divisor for it to be the quotient: the partial remainder's top bit is lost)
// FUNC_AT(0x0013fa50)
uint32_t iSNDdivu64(uint32_t low, uint32_t high, uint32_t divisor) {
    uint32_t quotient = 0;
    uint32_t remainder = high;
    for (int i = 32; i != 0; i--) {
        remainder = (low >> 31) + remainder * 2;
        quotient <<= 1;
        low <<= 1;
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient++;
        }
    }
    return quotient;
}

// FUNC_AT(0x00141280)
void SNDI_randomseeed(int seed) {
    for (int i = 0; i < 6; i++)
        RandomState[i] = kRandomSeeds[i] + seed;
}

// Six words added into each other with carries, the last counting up (and carrying into the others when it
// wraps); the first word is the number.
// FUNC_AT(0x001412e0)
uint32_t iSNDrandom(void) {
    uint32_t *w = RandomState;
    uint32_t s1 = w[1], s2 = w[2], s3 = w[3];
    uint32_t s4 = w[4], s5 = w[5];
    uint32_t a = s4 + s5;
    uint32_t carry = (a < s5 || a < s4) ? 1 : 0;
    w[4] = a;
    a += carry + s3;
    carry = a < s3 ? 1 : 0;
    w[3] = a;
    a += carry + s2;
    carry = a < s2 ? 1 : 0;
    w[2] = a;
    a += carry + s1;
    carry = a < s1 ? 1 : 0;
    w[1] = a;
    uint32_t result = w[0] + a + carry;
    s5++;
    w[5] = s5;
    w[0] = result;
    if (s5 == 0 && ++w[4] == 0 && ++w[3] == 0 && ++w[2] == 0 && ++w[1] == 0) {
        result++;
        w[0] = result;
    }
    return result;
}

// A random value in -range..range (range clamped to 0..0x10000)
// FUNC_AT(0x00142ea0)
int randrange(int range) {
    if (range > 0x10000)
        range = 0x10000;
    else if (range < 0)
        range = 0;
    int32_t r = int32_t(iSNDrandom() & 0x7fff) - 0x4000;
    return Mul(r, range) >> 14;
}
