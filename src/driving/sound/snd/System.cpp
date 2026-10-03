#include "System.h"

#include "Voices.h"

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
// Each function is the original at its address, ported from the listing; calls to modules not ported (the platform
// driver, platform.system's SYNCTASK/REAL_addexit/memclr, engine.core's dummyNullFunction) go to their originals'
// addresses, function addresses the library stores (the SYNCTASK, the exit hook) are the originals' too, so what
// the rest of the program sees is unchanged. The library's globals stay where they are.
//
// devtools/SndSystemShadow.cpp compares the pure helpers, the lists, the heap, the client list and the 100 Hz
// server (on snapshots of the voices, the platform driver replaced by recording fakes) with the originals.
// ---------------------------------------------------------------------------------------------------------------

namespace {

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}

inline int16_t &S16(uint32_t address) {
    return *(int16_t *)(uintptr_t)address;
}

inline uint16_t &U16(uint32_t address) {
    return *(uint16_t *)(uintptr_t)address;
}

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

// ---- globals (sound.md 2.6)

const uint32_t kCapsResult = 0x00244c28;       // what SNDPLATFORM_outputcaps answered
const uint32_t kCapsRead = 0x00244c2c;         // set once the caps are read
const uint32_t kSysTaskAdded = 0x00244c30;     // systaskadded
const uint32_t kOpts = 0x00244cc8;             // the options, 0x120 bytes (MW: SNDSYSCAP/SNDSYSSET)
const uint32_t kOptsSettable = 0x00244ce8;     // their settable part, 0xe8 bytes ...
const uint32_t kOptsSaved = 0x00244de8;        // ... and its saved copy
const uint32_t kVectors = 0x00244dd0;          // sndopts2, 6 words
const uint32_t kRandomBase = 0x00244cec;       // the options' seed word
const uint32_t kNumBanks = 0x00244cf0;         // uint16
const uint32_t kStealEqual = 0x00244d04;
const uint32_t kOutputMode = 0x00244d10;
const uint32_t kOpt14 = 0x00244d14;
const uint32_t kInited = 0x00244ed0;           // SNDSYS_is_inited
const uint32_t kMasterVolume = 0x00244ed1;
const uint32_t kNesting = 0x00244ed3;          // critical-section nesting (int8)
const uint32_t kNum100HzClients = 0x00244ed4;  // int8, never registered
const uint32_t kNumServerClients = 0x00244ed5; // int8
const uint32_t kNumUserClients = 0x00244ed6;   // int8, never registered
const uint32_t kNumVoices = 0x00244ed8;        // int16
const uint32_t kTick = 0x00244edc;             // the server's tick counter
const uint32_t k100HzClients = 0x00244ee0;
const uint32_t kServerClients = 0x00244ef8;
const uint32_t kVoices = 0x00244f3c;           // sndvoicei_buffer
const uint32_t kBanks = 0x00244f40;            // sndbanki_buffer
const uint32_t kHeap = 0x00244f6c;             // pSndHeap
const uint32_t kAzimuth = 0x00244f7c;          // the speaker azimuth tables for 2/4/5.1 output (uint16 each)
const uint32_t kSoundMutex = 0x00244fc0;       // the CRITICAL_SECTION
const uint32_t kRandom = 0x00245978;           // iSNDrandom's six words
const uint32_t kRandomSeeds = 0x001d9d30;      // their .rdata starting values (+ the seed)
const uint32_t kAuthorByte = 0x001d9d48;       // a byte of the SNDAUTHOR string, written 'S' by SNDSYSI_init

// The functions the library keeps the addresses of: the originals', which jump to ours
const uint32_t kSystemTaskAddress = 0x0013d3e0;   // SNDREAL_systemtask
const uint32_t kExitHookAddress = 0x001413d0;     // ~ASystem
const uint32_t kDummyNullAddress = 0x000d3580;    // dummyNullFunction

// The exit / restore callbacks other modules register (each called with no argument by SNDSYS_restore) ...
const uint32_t kRestoreHooks[6] = { 0x00244f20, 0x00244f24, 0x00244f28, 0x00244f34, 0x00244f38, 0x00244f30 };
// ... and the bank module's, called with -1
const uint32_t kBankExitHook = 0x00244f2c;     // SNDbank_on_exit_func

inline SND::MemHeap *Heap() {
    return *(SND::MemHeap **)(uintptr_t)kHeap;
}

inline SND::Voice *VoiceAt(int index) {
    return (SND::Voice *)(*(uint8_t **)(uintptr_t)kVoices + index * 0x88);
}

inline int32_t Mul(int32_t a, int32_t b) {   // IMUL r32: wraps
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

// REP MOVSD, forward a dword at a time (SNDSYSI_init has SNDSYS_getopts copy the options onto themselves)
inline void CopyDwords(uint32_t *to, const uint32_t *from, int count) {
    for (int i = 0; i < count; i++)
        to[i] = from[i];
}

// ---- the originals called from here

inline int PlatformOutputCaps() {
    return ((int (*)(void))0x0013da00)();                   // SNDPLATFORM_outputcaps
}
inline void PlatformOutputSet() {
    ((void (*)(void))0x0013dae0)();                         // SNDPLATFORM_outputset
}
inline int PlatformInit() {
    return ((int (*)(void))0x0013dc50)();                   // SNDPLATFORM_init
}
inline void PlatformRestore() {
    ((void (*)(void))0x0013ddc0)();                         // SNDPLATFORM_restore
}
inline void PlatformSetVol(int voice) {
    ((void (*)(int))0x0013df50)(voice);                     // SNDPLATFORM_setvol
}
inline void PlatformSetPitch(int voice) {
    ((void (*)(int))0x0013e320)(voice);                     // SNDPLATFORM_setpitch
}
inline void MemClr(void *memory, int bytes) {
    ((void (*)(void *, int))0x0013f600)(memory, bytes);     // memclr (platform.system)
}
inline void DummyNull() {
    ((void (*)(void))kDummyNullAddress)();                  // dummyNullFunction (engine.core)
}
inline void SyncTaskAdd(uint32_t task, int interval, int delay) {
    ((void (*)(uint32_t, int, int))0x0010aa50)(task, interval, delay);   // SYNCTASK_add
}
inline void RealAddExit(uint32_t callback) {
    ((void (*)(uint32_t))0x0010b310)(callback);             // REAL_addexit
}

// The kernel's critical-section calls, through the XBE's import table
inline void ImportCall(uint32_t slot) {
    ((void (__stdcall *)(void *)) * (void **)(uintptr_t)slot)((void *)(uintptr_t)kSoundMutex);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The servers
// ---------------------------------------------------------------------------------------------------------------

// The SND thread's tick (sound.md 3.2): reclaim finished hardware voices, the 100 Hz clients (none ever), then per
// voice in use: pitch LFO, volume LFO, fade (a fade below zero stops the voice), envelope (the next segment when
// the current one runs out, stop after the last); a volume change is recalculated and sent.
// FUNC_AT(0x0013b7b0)
void SNDSYSI_100hzserver(void) {
    U32(kTick) = U32(kTick) + 1;
    iSNDserve();
    for (int i = 0; i < (int8_t)U8(kNum100HzClients); i++)
        (*(void (**)(void))(uintptr_t)(k100HzClients + i * 4))();

    for (int voice = 0; voice < S16(kNumVoices); voice++) {
        SND::Voice *v = VoiceAt(voice);
        if (v->inUse != 1 || v->handle < 0)
            continue;
        if (v->pitchLfo != NULL) {
            uint8_t position = (uint8_t)(v->pitchLfoPos + 1);
            v->pitchLfoPos = position;
            if (position >= v->pitchLfoLength)
                v->pitchLfoPos = 0;
            v->detuneLinear = 0;
            iSNDcalcpitch(voice);
            PlatformSetPitch(voice);
        }
        int changed = 0;
        if (v->volLfo != NULL) {
            uint8_t position = (uint8_t)(v->volLfoPos + 1);
            v->volLfoPos = position;
            changed = 1;
            if (position >= v->volLfoLength)
                v->volLfoPos = 0;
        }
        int32_t step = v->fadeStep;
        if (step != 0) {
            int32_t fade = (int32_t)((uint32_t)v->fade + (uint32_t)step);
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
            v->env = (int32_t)((uint32_t)v->env + (uint32_t)v->envStep);
            changed = 1;
        }
        if (ticks == 0) {
            uint8_t segment = (uint8_t)(v->envCurrent + 1);
            v->envCurrent = segment;
            if ((int8_t)segment >= (int8_t)v->envCount) {
                SNDstop(v->handle);
                continue;
            }
            const int32_t *entry = (const int32_t *)((const uint8_t *)v->envTable + (int8_t)segment * 8);
            v->envTicks = entry[0];
            if (entry[0] < 0)
                v->envTicks = 0x7fffffff;
            int32_t delta = (int32_t)(((uint32_t)entry[1] << 16) - (uint32_t)v->env);
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
    if (U8(kInited) == 0)
        return;
    for (int i = 0; i < (int8_t)U8(kNumServerClients); i++)
        (*(void (**)(void))(uintptr_t)(kServerClients + i * 4))();
}

// FUNC_AT(0x0013f900)
void iSNDserveraddclient(SndServerClient client) {
    *(void (**)(void))(uintptr_t)(kServerClients + (int8_t)U8(kNumServerClients) * 4) = client;
    U8(kNumServerClients) = (uint8_t)(U8(kNumServerClients) + 1);
}

// FUNC_AT(0x0013f920)
void iSNDserverremoveclient(SndServerClient client) {
    int8_t count = (int8_t)U8(kNumServerClients);
    int i = 0;
    if (count <= 0)
        return;
    while (*(void (**)(void))(uintptr_t)(kServerClients + i * 4) != client) {
        if (++i >= count)
            return;
    }
    count = (int8_t)(count - 1);
    U8(kNumServerClients) = (uint8_t)count;
    for (; i < (int8_t)U8(kNumServerClients); i++)
        U32(kServerClients + i * 4) = U32(kServerClients + i * 4 + 4);
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
    uint32_t added = U32(kSysTaskAdded);
    U32(0x00244ddc) = kDummyNullAddress;
    if (added == 0) {
        SyncTaskAdd(kSystemTaskAddress, 0, 1);
        U32(kSysTaskAdded) = 1;
    }
    RealAddExit(kExitHookAddress);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The critical section
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013b950)
void SNDSYS_entercritical(void) {
    SNDI_mutexlock();
    U8(kNesting) = (uint8_t)(U8(kNesting) + 1);
}

// FUNC_AT(0x0013b970)
void SNDSYS_leavecritical(void) {
    U8(kNesting) = (uint8_t)(U8(kNesting) - 1);
    SNDI_mutexunlock();
}

// FUNC_AT(0x0013e7a0)
void SNDI_mutexalloc(void) {
    ImportCall(0x00189cf0);                                 // RtlInitializeCriticalSection
}

// FUNC_AT(0x0013e7b0)
void SNDI_mutexlock(void) {
    ImportCall(0x00189c40);                                 // RtlEnterCriticalSection
}

// FUNC_AT(0x0013e7c0)
void SNDI_mutexunlock(void) {
    ImportCall(0x00189c3c);                                 // RtlLeaveCriticalSection
}

// ---------------------------------------------------------------------------------------------------------------
// Init and restore
// ---------------------------------------------------------------------------------------------------------------

// The options: on the first call the platform's caps and the defaults (16 banks, steal at equal priority), saved;
// then all 0x120 bytes copied out. The caps' result.
// FUNC_AT(0x0013d090)
int SNDSYS_getopts(void *opts) {
    if (U32(kCapsRead) == 0) {
        U32(kCapsResult) = (uint32_t)PlatformOutputCaps();
        U16(kNumBanks) = 0x10;
        U8(kOpt14) = 0x5a;
        U8(kStealEqual) = 1;
        U32(kCapsRead) = 1;
        CopyDwords((uint32_t *)(uintptr_t)kOptsSaved, (const uint32_t *)(uintptr_t)kOptsSettable, 0x3a);
    }
    int result = (int)U32(kCapsResult);
    CopyDwords((uint32_t *)opts, (const uint32_t *)(uintptr_t)kOpts, 0x48);
    return result;
}

// The settable part and the vector table copied in, the platform told, the settable part saved.
// FUNC_AT(0x0013d0f0)
int SNDSYS_setops(const void *opts) {
    CopyDwords((uint32_t *)(uintptr_t)kOptsSettable, (const uint32_t *)((const uint8_t *)opts + 0x20), 0x3a);
    CopyDwords((uint32_t *)(uintptr_t)kVectors, (const uint32_t *)((const uint8_t *)opts + 0x108), 6);
    PlatformOutputSet();
    CopyDwords((uint32_t *)(uintptr_t)kOptsSaved, (const uint32_t *)(uintptr_t)kOptsSettable, 0x3a);
    return 0;
}

// From ASystem::ASystem with the 256 KB audio heap: 0, or the platform's (negative) failure.
// FUNC_AT(0x0013d140)
int SNDSYSI_init(void *memory, int size) {
    U8(kAuthorByte) = 0x53;
    if (U8(kInited) != 0)
        return 0;
    MemClr(memory, size);
    SNDMEMI_init(memory, size);
    if (S16(kNumVoices) == 0) {
        int result = SNDSYS_getopts((void *)(uintptr_t)kOpts);
        if (result < 0)
            return result;
        SNDSYS_setops((const void *)(uintptr_t)kOpts);
    }
    SNDI_randomseeed((int)U32(kRandomBase));
    SNDI_mutexalloc();
    SNDSYS_entercritical();
    U32(kVoices) = (uint32_t)(uintptr_t)SNDMEMI_alloc(S16(kNumVoices) * 0x88);
    U32(kBanks) = (uint32_t)(uintptr_t)SNDMEMI_alloc(U16(kNumBanks) << 3);
    SNDSYS_leavecritical();
    U32(kTick) = 0;
    U8(kMasterVolume) = 0x7f;
    U8(kNumUserClients) = 0;
    U8(kNum100HzClients) = 0;
    U8(kNumServerClients) = 0;
    int result = PlatformInit();
    if (result < 0) {
        PlatformRestore();
        DummyNull();
        return result;
    }
    // the speaker azimuths (65536ths of a turn): stereo, then 4 and 5.1 speakers (from the options for 4)
    uint16_t *az = (uint16_t *)(uintptr_t)kAzimuth;   // az[i] at 0x00244f7c + 2i
    int8_t mode = (int8_t)U8(kOutputMode);
    U8(kInited) = 1;
    if (mode == 2) {
        az[0] = 0xc000;
        az[1] = 0x4000;
    } else {
        az[0] = 0xe000;
        az[1] = 0x2000;
    }
    az[6] = U16(0x00244d5c);                          // 0x00244f88
    az[7] = U16(0x00244d58);
    az[8] = U16(0x00244d5a);
    az[12] = 0xe000;                                  // 0x00244f94
    az[13] = 0x2000;
    az[14] = 0xa000;
    az[15] = 0x6000;
    az[18] = 0xe000;                                  // 0x00244fa0
    az[19] = 0;
    az[20] = 0x2000;
    az[21] = 0xa000;
    az[22] = 0x6000;
    az[24] = 0xe000;                                  // 0x00244fac
    az[25] = 0;
    az[26] = 0x2000;
    az[27] = 0xa000;
    az[28] = 0x6000;
    az[29] = 0;                                       // 0x00244fb6
    SNDI_precalcaztospkrvol();
    return 0;
}

// Asked only by the old movie player.
// FUNC_AT(0x0013d310)
int SNDSYS_inited(void) {
    SND_UNTESTED("SNDSYS_inited");
    return (int8_t)U8(kInited);
}

// From ASystem::Shutdown and the exit hook: -14 if not up; else the reverb off, the modules' restore hooks, every
// voice stopped, the platform driver down, the arrays freed; the heap's percentage in use.
// FUNC_AT(0x0013d320)
int SNDSYS_restore(void) {
    if (U8(kInited) == 0)
        return -14;
    SNDfxinitbus(0, 0, 0, -1, -1);
    for (int i = 0; i < 6; i++) {
        void (*hook)(void) = *(void (**)(void))(uintptr_t)kRestoreHooks[i];
        if (hook != NULL)
            hook();
    }
    SNDstopall();
    void (*bankExit)(int) = *(void (**)(int))(uintptr_t)kBankExitHook;
    if (bankExit != NULL)
        bankExit(-1);
    PlatformRestore();
    SNDSYS_entercritical();
    SNDMEMI_free(*(void **)(uintptr_t)kVoices);
    SNDMEMI_free(*(void **)(uintptr_t)kBanks);
    SNDSYS_leavecritical();
    int used = SNDMEMI_restore();
    U8(kInited) = 0;
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
    for (int i = 0; i < S16(kNumVoices); i++)
        SNDstop(VoiceAt(i)->handle);
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
    SND::MemHeap *heap = (SND::MemHeap *)memory;
    U32(kHeap) = (uint32_t)(uintptr_t)memory;
    heap->size = size;
    SND::MemEntry *table = (SND::MemEntry *)((uint8_t *)memory + size - 8);
    Heap()->table = table;
    Heap()->base = (uint8_t *)memory + 0x18;
    Heap()->base = Heap()->base + 0xf;
    Heap()->base = (uint8_t *)((uintptr_t)Heap()->base & ~(uintptr_t)0xf);
    int32_t left = size - 0xf;
    Heap()->limit = left - 0x20;
    Heap()->lowWater = left;
}

// First fit in offset order, the size rounded up to 16; NULL if nothing fits.
// FUNC_AT(0x0013f780)
void* SNDMEMI_alloc(int size) {
    SND::MemHeap *heap = Heap();
    int32_t count = heap->count;
    int32_t rounded = (int32_t)(((uint32_t)size + 0xf) & ~0xfu);
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
                    start = (int32_t)((uint32_t)table[index + 1].size + (uint32_t)table[index + 1].offset);
                    gap = (int32_t)((uint32_t)table[index].offset - (uint32_t)start);
                }
                if ((uint32_t)gap + (uint32_t)start > (uint32_t)limit)
                    gap = (int32_t)((uint32_t)limit - (uint32_t)start);
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
                SND::MemEntry *e = &Heap()->table[i];
                e->offset = e[1].offset;
                e->size = e[1].size;
                heap = Heap();
            }
        } else {
            // past the last block
            SND::MemEntry *last = &heap->table[index + 1];
            start = (int32_t)((uint32_t)last->size + (uint32_t)last->offset);
            gap = (int32_t)((uint32_t)heap->limit - (uint32_t)start);
            if (rounded > gap)
                return NULL;
        }
    }
    if (count == 0 && rounded > gap)
        return NULL;
    SND::MemEntry *e = &heap->table[index];
    e->offset = start;
    e->size = rounded;
    Heap()->count--;
    Heap()->limit -= 8;
    heap = Heap();
    int32_t left = (int32_t)((uint32_t)heap->limit - (uint32_t)start - (uint32_t)rounded);
    void *block = heap->base + start;
    if (left < heap->lowWater)
        heap->lowWater = left;
    return block;
}

// The block's record removed (a pointer the heap did not give is ignored).
// FUNC_AT(0x0013f880)
void SNDMEMI_free(void *block) {
    SND::MemHeap *heap = Heap();
    int32_t count = heap->count;
    int32_t offset = (int32_t)((uint8_t *)block - heap->base);
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
    Heap()->limit += 8;
    while (index > Heap()->count) {
        SND::MemEntry *t = &Heap()->table[index];
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
    SND::MemHeap *heap = Heap();
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
    return ((uint64_t)high << 32) | low;
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
        U32(kRandom + i * 4) = U32(kRandomSeeds + i * 4) + (uint32_t)seed;
}

// Six words added into each other with carries, the last counting up (and carrying into the others when it
// wraps); the first word is the number.
// FUNC_AT(0x001412e0)
uint32_t iSNDrandom(void) {
    uint32_t s1 = U32(kRandom + 4), s2 = U32(kRandom + 8), s3 = U32(kRandom + 12);
    uint32_t s4 = U32(kRandom + 16), s5 = U32(kRandom + 20);
    uint32_t a = s4 + s5;
    uint32_t carry = (a < s5 || a < s4) ? 1 : 0;
    U32(kRandom + 16) = a;
    a += carry + s3;
    carry = a < s3 ? 1 : 0;
    U32(kRandom + 12) = a;
    a += carry + s2;
    carry = a < s2 ? 1 : 0;
    U32(kRandom + 8) = a;
    a += carry + s1;
    carry = a < s1 ? 1 : 0;
    U32(kRandom + 4) = a;
    uint32_t result = U32(kRandom) + a + carry;
    s5++;
    U32(kRandom + 20) = s5;
    U32(kRandom) = result;
    if (s5 == 0 && ++U32(kRandom + 16) == 0 && ++U32(kRandom + 12) == 0 && ++U32(kRandom + 8) == 0 &&
        ++U32(kRandom + 4) == 0) {
        result++;
        U32(kRandom) = result;
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
    int32_t r = (int32_t)(iSNDrandom() & 0x7fff) - 0x4000;
    return Mul(r, range) >> 14;
}
