#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // as the build defines it (CMakeLists.txt)
#endif

#include "SndSystemShadow.h"

#include "../sound/snd/Platform.h"
#include "../sound/snd/System.h"
#include "../sound/snd/Voices.h"
#include "../platform/RealPrint.h"
#include "../platform/RealSystem.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDSYSSHADOW=1, at injection time on the loader's thread before the game runs: the sound library's
// system module (sound/snd/System.cpp, docs/driving/sound.md 3.1, 3.2, 4.1) against the originals, on the same
// inputs, compared byte for byte. The originals run with their entry bytes swapped back (common/xbeOriginal.h) -
// all of module A at once, so the original's side is the original throughout module A; what it calls in other
// modules (the voice manager's ports, the platform driver's fakes below) is the same for both sides.
//
// - iSNDmulu64 / iSNDdivu64: edges and random operands (the divide also with the high word at or above the
//   divisor, where the result is not the quotient but still defined).
// - SNDI_randomseeed / iSNDrandom / randrange: interleaved sequences from the same seeds, the six state words after;
//   and from random states, including the words at 0xffffffff that make the counting word carry into the rest.
// - SNDLINKI_init/push/pushtail/pop/remove: random operation sequences on four lists over 64 nodes in a workspace
//   at one address (snapshot, original, save, put back, port, compare every byte and the popped node).
// - SNDMEMI: two arenas through the same random sequence of SNDMEMI_init (sizes and alignments), allocations
//   (also through the CODA_New thunk; small, large, too large), frees (live blocks through either entry, double
//   frees, pointers the heap never gave), SNDMEMI_percentused and its thunk - after every step the returned offsets,
//   the header (pointers as offsets) and every other byte of the two arenas.
// - iSNDserveraddclient/removeclient, SNDSYS_service, SNDREAL_systemtask: random add/remove/service on the client
//   list (recording fake clients), compared on the globals and the call sequence.
// - SNDSYSI_100hzserver: random voice tables (LFOs, fades crossing their targets and zero, envelopes stepping and
//   ending, slave and free voices, 100 Hz clients), 30 ticks per case; voices, tables and globals compared, and
//   the calls the server makes into the platform driver (setpitch, setvol, stop - recording fakes that also note
//   the voice's pitch and volume at the call).
// - SNDSYS_vectortoreal, SNDSYSI_init, the 100 Hz server on the new voices, SNDstopall, SNDSYS_restore (and its
//   exit-hook thunk), SNDSYS_inited, the critical section: one script per case on random options and states, with
//   the platform driver, SYNCTASK_add, REAL_addexit, dummyNullFunction and the kernel's critical-section imports
//   (the import table's slots) replaced by recording fakes; the globals (0x00244ba8..0x00244fc0,
//   0x00245364..0x002459a0), the heap arena and the byte SNDSYSI_init writes into .rdata compared.
//
// The platform driver's entries are replaced for the run by five-byte jumps to the fakes, put back afterwards;
// every global the test touches is saved first and put back at the end.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

// ---- module A's entries (the originals the test swaps back)

const unsigned kModuleA[] = {
    0x0013b7b0, 0x0013b950, 0x0013b970, 0x0013d090, 0x0013d0f0, 0x0013d140, 0x0013d310, 0x0013d320, 0x0013d3e0,
    0x0013d3f0, 0x0013e7a0, 0x0013e7b0, 0x0013e7c0, 0x0013f0a0, 0x0013f0b0, 0x0013f0e0, 0x0013f110, 0x0013f140,
    0x0013f710, 0x0013f770, 0x0013f780, 0x0013f880, 0x0013f900, 0x0013f920, 0x0013f980, 0x0013f9e0, 0x0013fa50,
    0x00141280, 0x001412e0, 0x00141390, 0x001413d0, 0x00141860, 0x00141870, 0x001429d0, 0x00142ea0, 0 };

void Originals(bool original) {
    for (const unsigned *a = kModuleA; *a != 0; a++)
        XbeOriginal_Restore(*a, original);
}

// ---- results

int g_cases, g_checks, g_differ, g_details, g_faults;

void Detail(const char *format, ...) {
    if (g_details >= 10)
        return;
    g_details++;
    va_list arguments;
    va_start(arguments, format);
    printf("[sndsysshadow]   ");
    vprintf(format, arguments);
    printf("\n");
    va_end(arguments);
}

bool Check(bool same, const char *format, ...) {
    g_checks++;
    if (same)
        return true;
    g_differ++;
    if (g_details < 10) {
        g_details++;
        va_list arguments;
        va_start(arguments, format);
        printf("[sndsysshadow]   ");
        vprintf(format, arguments);
        printf("\n");
        va_end(arguments);
    }
    return false;
}

bool CheckBytes(const void *a, const void *b, size_t n, const char *what, int at) {
    g_checks++;
    if (memcmp(a, b, n) == 0)
        return true;
    size_t i = 0;
    while (((const uint8_t *)a)[i] == ((const uint8_t *)b)[i])
        i++;
    g_differ++;
    Detail("%s, case %d: byte +0x%x differs (original %02x, port %02x)", what, at, (unsigned)i,
           ((const uint8_t *)a)[i], ((const uint8_t *)b)[i]);
    return false;
}

// ---- random numbers

uint32_t g_seed = 0x51a7e5edu;

uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

int Range(int lo, int hi) {   // inclusive
    return lo + (int)(Next() % (uint32_t)(hi - lo + 1));
}

bool Chance(int percent) {
    return (int)(Next() % 100) < percent;
}

// ---- globals touched, saved first and put back at the end

struct Region {
    uint32_t lo, hi;
};
const Region kRegions[] = { { 0x00244ba8, 0x00244fc0 }, { 0x00245364, 0x002459a0 } };
const int kRegionCount = 2;
const uint32_t kAuthorByte = 0x001d9d48;
const uint32_t kImportSlots[3] = { 0x00189cf0, 0x00189c40, 0x00189c3c };   // RtlInitialize/Enter/LeaveCriticalSection

struct Globals {
    uint8_t r0[0x00244fc0 - 0x00244ba8];
    uint8_t r1[0x002459a0 - 0x00245364];
    uint8_t author;
};

void SaveGlobals(Globals *g) {
    memcpy(g->r0, (void *)(uintptr_t)kRegions[0].lo, sizeof(g->r0));
    memcpy(g->r1, (void *)(uintptr_t)kRegions[1].lo, sizeof(g->r1));
    g->author = *(uint8_t *)(uintptr_t)kAuthorByte;
}

void LoadGlobals(const Globals *g) {
    memcpy((void *)(uintptr_t)kRegions[0].lo, g->r0, sizeof(g->r0));
    memcpy((void *)(uintptr_t)kRegions[1].lo, g->r1, sizeof(g->r1));
    *(uint8_t *)(uintptr_t)kAuthorByte = g->author;
}

Globals g_real;   // as the test found them

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

// ---- the call log the fakes write, one per side

const int kLogMax = 2048;
struct CallLog {
    uint32_t count;
    uint32_t hash;
    uint32_t entries[kLogMax][4];
};
CallLog g_log[2];
int g_side;

void LogReset() {
    memset(&g_log[0], 0, sizeof(CallLog));
    memset(&g_log[1], 0, sizeof(CallLog));
}

void Log(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
    CallLog &l = g_log[g_side];
    uint32_t v[4] = { a, b, c, d };
    uint32_t h = l.hash * 16777619u + 1;
    for (int i = 0; i < 16; i++)
        h = (h ^ ((const uint8_t *)v)[i]) * 16777619u;
    l.hash = h;
    if (l.count < (uint32_t)kLogMax)
        memcpy(l.entries[l.count], v, sizeof(v));
    l.count++;
}

bool CheckLogs(const char *what, int at) {
    const CallLog &o = g_log[0], &p = g_log[1];
    g_checks++;
    if (o.count == p.count && o.hash == p.hash)
        return true;
    g_differ++;
    uint32_t n = o.count < p.count ? o.count : p.count;
    if (n > (uint32_t)kLogMax)
        n = kLogMax;
    uint32_t i = 0;
    while (i < n && memcmp(o.entries[i], p.entries[i], 16) == 0)
        i++;
    if (i < n)
        Detail("%s, case %d: call %u differs: original %x(%x, %x, %x), port %x(%x, %x, %x)", what, at, i,
               o.entries[i][0], o.entries[i][1], o.entries[i][2], o.entries[i][3], p.entries[i][0], p.entries[i][1],
               p.entries[i][2], p.entries[i][3]);
    else
        Detail("%s, case %d: %u calls in the original, %u in the port", what, at, o.count, p.count);
    return false;
}

// ---- five-byte jumps over the originals the fakes stand in for, and the import slots

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[32];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
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

// A ported callee: the original's entry and our port's, both to the fake (our ports call each other directly, the
// originals by address). The answer is whether the original's entry took the jump.
bool HookBoth(uint32_t at, const void *ours, const void *to) {
    HookInstall(at, to);
    bool on = g_hooks[g_hookCount - 1].on;
    if ((uint32_t)(uintptr_t)ours != at)
        HookInstall((uint32_t)(uintptr_t)ours, to);
    return on;
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

uint32_t g_importSaved[3];
bool g_importsOn;
bool g_canVector;   // SYNCTASK_add and REAL_addexit replaced (always in game; not in an offline harness)

void SetImportSlots(const uint32_t *values) {
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)0x00189c3c, 0xc0, PAGE_READWRITE, &old))
        return;
    for (int i = 0; i < 3; i++)
        U32(kImportSlots[i]) = values[i];
    VirtualProtect((void *)(uintptr_t)0x00189c3c, 0xc0, old, &old);
}

// ---- SEH: a fault on either side is counted, not fatal

typedef void (*SideFn)(bool original);

bool Guarded(SideFn fn, bool original) {
#ifdef _MSC_VER
    __try {
        fn(original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    fn(original);
    return true;
#endif
}

// Runs one side: the originals swapped in for the original's
bool RunSide(SideFn fn, bool original) {
    g_side = original ? 0 : 1;
    if (original)
        Originals(true);
    bool ok = Guarded(fn, original);
    if (original)
        Originals(false);
    return ok;
}

bool CheckFaults(bool okO, bool okP, const char *what, int at) {
    if (okO && okP)
        return true;
    if (!okO && !okP) {
        g_faults++;
        Detail("%s, case %d: faults on both sides", what, at);
        return true;   // both fault: the same behaviour, as far as can be told
    }
    return Check(false, "%s, case %d: %s faulted", what, at, okO ? "the port" : "the original");
}

// =================================================================================================================
// 1. iSNDmulu64 / iSNDdivu64
// =================================================================================================================

const int kArith = 50000;
struct ArithSpace {
    uint32_t a[kArith], b[kArith], c[kArith];
    uint64_t mulO[kArith], mulP[kArith];
    uint32_t divO[kArith], divP[kArith];
};
ArithSpace *g_ar;

uint32_t Operand() {
    static const uint32_t edges[] = { 0, 1, 2, 0xffff, 0x10000, 0x10001, 0x7fffffff, 0x80000000u, 0xfffffffeu,
                                      0xffffffffu, 0x8000, 0xffff0000u, 0x0000ffffu, 48000, 1000 };
    switch (Next() % 4) {
    case 0:
        return edges[Next() % (sizeof(edges) / sizeof(edges[0]))];
    case 1:
        return Next() & ((1u << (Next() % 32)) - 1);
    default:
        return Next();
    }
}

void ArithSide(bool original) {
    typedef uint64_t (*MulFn)(uint32_t, uint32_t);
    typedef uint32_t (*DivFn)(uint32_t, uint32_t, uint32_t);
    MulFn mul = original ? (MulFn)0x0013f9e0 : &iSNDmulu64;
    DivFn div = original ? (DivFn)0x0013fa50 : &iSNDdivu64;
    uint64_t *mo = original ? g_ar->mulO : g_ar->mulP;
    uint32_t *dv = original ? g_ar->divO : g_ar->divP;
    for (int i = 0; i < kArith; i++) {
        mo[i] = mul(g_ar->a[i], g_ar->b[i]);
        dv[i] = div(g_ar->a[i], g_ar->b[i], g_ar->c[i]);
    }
}

void TestArith() {
    g_ar = (ArithSpace *)VirtualAlloc(NULL, sizeof(ArithSpace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_ar == NULL)
        return;
    ArithSpace &w = *g_ar;
    for (int i = 0; i < kArith; i++) {
        w.a[i] = Operand();
        w.b[i] = Operand();
        w.c[i] = Operand();
        if (Chance(70) && w.c[i] != 0)
            w.b[i] %= w.c[i];   // a real quotient
    }
    bool okO = RunSide(ArithSide, true);
    bool okP = RunSide(ArithSide, false);
    g_cases += 2 * kArith;
    if (CheckFaults(okO, okP, "arith", 0) && okO && okP) {
        for (int i = 0; i < kArith; i++) {
            if (!Check(w.mulO[i] == w.mulP[i], "iSNDmulu64(%08x, %08x): original %016llx, port %016llx", w.a[i],
                       w.b[i], (unsigned long long)w.mulO[i], (unsigned long long)w.mulP[i]))
                break;
        }
        for (int i = 0; i < kArith; i++) {
            if (!Check(w.divO[i] == w.divP[i], "iSNDdivu64(%08x, %08x, %08x): original %08x, port %08x", w.a[i],
                       w.b[i], w.c[i], w.divO[i], w.divP[i]))
                break;
        }
    }
    VirtualFree(g_ar, 0, MEM_RELEASE);
    g_ar = NULL;
}

// =================================================================================================================
// 2. SNDI_randomseeed / iSNDrandom / randrange
// =================================================================================================================

const int kRandomSteps = 2000;
const uint32_t kRandomState = 0x00245978;
struct RandomRun {
    int32_t values[kRandomSteps];
    uint32_t state[6];
};
RandomRun g_randO, g_randP;
int32_t g_ranges[kRandomSteps];
uint32_t g_startState[6];
int g_randomSeed;
bool g_randomFromSeed;
int g_randomSteps;

void RandomSide(bool original) {
    typedef void (*SeedFn)(int);
    typedef uint32_t (*RandomFn)(void);
    typedef int (*RangeFn)(int);
    SeedFn seed = original ? (SeedFn)0x00141280 : &SNDI_randomseeed;
    RandomFn random = original ? (RandomFn)0x001412e0 : &iSNDrandom;
    RangeFn range = original ? (RangeFn)0x00142ea0 : &randrange;
    RandomRun *r = original ? &g_randO : &g_randP;
    if (g_randomFromSeed)
        seed(g_randomSeed);
    else
        memcpy((void *)(uintptr_t)kRandomState, g_startState, sizeof(g_startState));
    for (int i = 0; i < g_randomSteps; i++)
        r->values[i] = (i & 1) != 0 ? range(g_ranges[i]) : (int32_t)random();
    memcpy(r->state, (void *)(uintptr_t)kRandomState, sizeof(r->state));
}

void TestRandom() {
    static const int seeds[] = { 0, 1, -1, 0x7fffffff, (int)0x80000000u, 12345, 0x1d9d30 };
    for (int c = 0; c < 200; c++) {
        g_randomFromSeed = c < 120;
        if (g_randomFromSeed) {
            g_randomSeed = c < 7 ? seeds[c] : (int)Next();
            g_randomSteps = kRandomSteps;
        } else {
            for (int i = 0; i < 6; i++) {
                switch (Next() % 4) {
                case 0: g_startState[i] = 0xffffffffu; break;
                case 1: g_startState[i] = 0xffffffffu - (Next() % 8); break;
                case 2: g_startState[i] = Next() % 4; break;
                default: g_startState[i] = Next(); break;
                }
            }
            g_randomSteps = 64;
        }
        for (int i = 0; i < g_randomSteps; i++) {
            switch (Next() % 5) {
            case 0: g_ranges[i] = Range(-0x20000, -1); break;
            case 1: g_ranges[i] = Range(0x10000, 0x30000); break;
            case 2: g_ranges[i] = Chance(50) ? 0 : 0x10000; break;
            default: g_ranges[i] = Range(0, 0x10000); break;
            }
        }
        memset(&g_randO, 0, sizeof(g_randO));
        memset(&g_randP, 0, sizeof(g_randP));
        bool okO = RunSide(RandomSide, true);
        bool okP = RunSide(RandomSide, false);
        g_cases++;
        if (!CheckFaults(okO, okP, "random", c))
            continue;
        for (int i = 0; i < g_randomSteps; i++) {
            if (!Check(g_randO.values[i] == g_randP.values[i], "random case %d step %d (%s): original %08x, port %08x",
                       c, i, (i & 1) ? "randrange" : "iSNDrandom", g_randO.values[i], g_randP.values[i]))
                break;
        }
        CheckBytes(g_randO.state, g_randP.state, sizeof(g_randO.state), "random state", c);
    }
}

// =================================================================================================================
// 3. SNDLINKI
// =================================================================================================================

const int kNodes = 64, kLists = 4;
struct ListSpace {
    SND::LinkList lists[kLists];
    uint8_t nodes[kNodes][0x30];
    SND::LinkNode *popped;
};
ListSpace *g_ls, *g_lsSnapshot, *g_lsOriginal;
int g_lsOp, g_lsList, g_lsNode;

void ListSide(bool original) {
    typedef void (*InitFn)(SND::LinkList *);
    typedef void (*PushFn)(SND::LinkList *, SND::LinkNode *);
    typedef SND::LinkNode *(*PopFn)(SND::LinkList *);
    SND::LinkList *list = &g_ls->lists[g_lsList];
    SND::LinkNode *node = (SND::LinkNode *)g_ls->nodes[g_lsNode];
    switch (g_lsOp) {
    case 0: (original ? (InitFn)0x0013f0a0 : &SNDLINKI_init)(list); break;
    case 1: (original ? (PushFn)0x0013f0b0 : &SNDLINKI_push)(list, node); break;
    case 2: (original ? (PushFn)0x0013f0e0 : &SNDLINKI_pushtail)(list, node); break;
    case 3: g_ls->popped = (original ? (PopFn)0x0013f110 : &SNDLINKI_pop)(list); break;
    default: (original ? (PushFn)0x0013f140 : &SNDLINKI_remove)(list, node); break;
    }
}

void TestLists() {
    g_ls = (ListSpace *)VirtualAlloc(NULL, sizeof(ListSpace) * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_ls == NULL)
        return;
    g_lsSnapshot = g_ls + 1;
    g_lsOriginal = g_ls + 2;
    for (int i = 0; i < kNodes; i++)
        for (int j = 0; j < 0x30; j++)
            g_ls->nodes[i][j] = (uint8_t)Next();
    int where[kNodes];   // the list each node is on, -1 none
    for (int i = 0; i < kNodes; i++)
        where[i] = -1;
    for (int l = 0; l < kLists; l++) {
        SND::LinkList *list = &g_ls->lists[l];
        list->head = list->tail = NULL;
        list->count = 0;
    }
    static const char *names[] = { "SNDLINKI_init", "SNDLINKI_push", "SNDLINKI_pushtail", "SNDLINKI_pop",
                                   "SNDLINKI_remove" };
    for (int op = 0; op < 20000; op++) {
        g_lsList = (int)(Next() % kLists);
        int r = (int)(Next() % 100);
        g_lsOp = r < 1 ? 0 : r < 30 ? 1 : r < 55 ? 2 : r < 75 ? 3 : 4;
        if (g_lsOp == 1 || g_lsOp == 2) {   // a free node
            int start = (int)(Next() % kNodes), n = 0;
            while (n < kNodes && where[(start + n) % kNodes] != -1)
                n++;
            if (n == kNodes)
                g_lsOp = 3;
            else
                g_lsNode = (start + n) % kNodes;
        }
        if (g_lsOp == 4) {   // a member of the list
            int members[kNodes], m = 0;
            for (int i = 0; i < kNodes; i++)
                if (where[i] == g_lsList)
                    members[m++] = i;
            if (m == 0)
                g_lsOp = 3;
            else
                g_lsNode = members[Next() % m];
        }
        if (g_lsOp == 0)   // init drops the members (their links stay as they were)
            for (int i = 0; i < kNodes; i++)
                if (where[i] == g_lsList)
                    where[i] = -2;   // unreachable: neither pushed again nor removed
        g_ls->popped = (SND::LinkNode *)(uintptr_t)0xdeadbeefu;
        memcpy(g_lsSnapshot, g_ls, sizeof(ListSpace));
        bool okO = RunSide(ListSide, true);
        memcpy(g_lsOriginal, g_ls, sizeof(ListSpace));
        memcpy(g_ls, g_lsSnapshot, sizeof(ListSpace));
        bool okP = RunSide(ListSide, false);
        g_cases++;
        if (!CheckFaults(okO, okP, names[g_lsOp], op))
            break;
        if (!CheckBytes(g_lsOriginal, g_ls, sizeof(ListSpace), names[g_lsOp], op))
            break;
        if (g_lsOp == 1 || g_lsOp == 2)
            where[g_lsNode] = g_lsList;
        else if (g_lsOp == 4)
            where[g_lsNode] = -1;
        else if (g_lsOp == 3 && g_ls->popped != NULL)
            where[((uint8_t *)g_ls->popped - g_ls->nodes[0]) / 0x30] = -1;
    }
    VirtualFree(g_ls, 0, MEM_RELEASE);
}

// =================================================================================================================
// 4. SNDMEMI
// =================================================================================================================

const int kArenaBytes = 0x20000;
const uint32_t kHeapPointer = 0x00244f6c;
uint8_t *g_arena[2];      // original's, port's (page aligned: the same 16-byte phase)
int g_heapPhase, g_heapSize;
int g_heapOp, g_heapArg, g_heapVia;
void *g_heapBlock[2];
intptr_t g_heapResult[2];

void HeapSide(bool original) {
    int s = original ? 0 : 1;
    U32(kHeapPointer) = (uint32_t)(uintptr_t)(g_arena[s] + g_heapPhase);
    typedef void (*InitFn)(void *, int);
    typedef void *(*AllocFn)(int);
    typedef void (*FreeFn)(void *);
    typedef int (*PercentFn)(void);
    switch (g_heapOp) {
    case 0:
        (original ? (InitFn)0x0013f710 : &SNDMEMI_init)(g_arena[s] + g_heapPhase, g_heapSize);
        g_heapResult[s] = 0;
        break;
    case 1: {
        void *p;
        if (g_heapVia)
            p = (original ? (AllocFn)0x00141860 : &SNDMEMI_allocthunk)(g_heapArg);
        else
            p = (original ? (AllocFn)0x0013f780 : &SNDMEMI_alloc)(g_heapArg);
        g_heapResult[s] = p == NULL ? -1 : (intptr_t)((uint8_t *)p - g_arena[s]);
        break;
    }
    case 2:
        if (g_heapVia)
            (original ? (FreeFn)0x00141870 : &SNDMEMI_freethunk)(g_heapBlock[s]);
        else
            (original ? (FreeFn)0x0013f880 : &SNDMEMI_free)(g_heapBlock[s]);
        g_heapResult[s] = 0;
        break;
    default:
        g_heapResult[s] = g_heapVia ? (original ? (PercentFn)0x0013f770 : &SNDMEMI_restore)()
                                    : (original ? (PercentFn)0x001429d0 : &SNDMEMI_percentused)();
        break;
    }
}

bool CompareHeaps(const char *what, int at) {
    const SND::MemHeap *o = (const SND::MemHeap *)(g_arena[0] + g_heapPhase);
    const SND::MemHeap *p = (const SND::MemHeap *)(g_arena[1] + g_heapPhase);
    intptr_t ob = o->base - g_arena[0], pb = p->base - g_arena[1];
    intptr_t ot = (uint8_t *)o->table - g_arena[0], pt = (uint8_t *)p->table - g_arena[1];
    if (!Check(ob == pb && ot == pt, "%s, case %d: header base +%ld/+%ld table +%ld/+%ld", what, at, (long)ob,
               (long)pb, (long)ot, (long)pt))
        return false;
    return CheckBytes(g_arena[0] + g_heapPhase + 8, g_arena[1] + g_heapPhase + 8, kArenaBytes - g_heapPhase - 8,
                      what, at);
}

void TestHeap() {
    g_arena[0] = (uint8_t *)VirtualAlloc(NULL, kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_arena[1] = (uint8_t *)VirtualAlloc(NULL, kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_arena[0] == NULL || g_arena[1] == NULL)
        return;
    static const char *names[] = { "SNDMEMI_init", "SNDMEMI_alloc", "SNDMEMI_free", "SNDMEMI_percentused" };
    const int kLive = 300;
    intptr_t live[kLive];
    bool failed = false;
    for (int round = 0; round < 12 && !failed; round++) {
        g_heapPhase = round < 2 ? 0 : (int)(Next() % 16);
        g_heapSize = round == 0 ? kArenaBytes : Range(0x200, kArenaBytes - g_heapPhase);
        memset(g_arena[0], 0, kArenaBytes);
        memset(g_arena[1], 0, kArenaBytes);
        for (int i = 0; i < kLive; i++)
            live[i] = -1;
        int ops = 1500;
        for (int op = -1; op < ops; op++) {
            g_heapVia = Chance(25);
            if (op < 0) {
                g_heapOp = 0;
            } else {
                int r = (int)(Next() % 100);
                g_heapOp = r < 50 ? 1 : r < 92 ? 2 : 3;
            }
            int slot = (int)(Next() % kLive);
            if (g_heapOp == 1) {
                switch (Next() % 8) {
                case 0: g_heapArg = Range(0, 16); break;
                case 1: g_heapArg = Range(0, g_heapSize / 2); break;
                case 2: g_heapArg = g_heapSize + Range(-64, 64); break;
                case 3:   // now and then a size whose rounding goes negative, or a negative one: odd but defined
                    if (Chance(5))
                        g_heapArg = Chance(50) ? 0x7fffffff - Range(0, 30) : -Range(1, 0x1000);
                    else
                        g_heapArg = Range(1, 1200);
                    break;
                default: g_heapArg = Range(1, 1200); break;
                }
            } else if (g_heapOp == 2) {
                int kind = (int)(Next() % 10);
                if (kind < 7) {   // a live block (or the slot's freed one again: a double free)
                    int n = 0;
                    while (n < kLive && live[(slot + n) % kLive] < 0)
                        n++;
                    if (n < kLive)
                        slot = (slot + n) % kLive;
                }
                intptr_t offset = live[slot] >= 0 && kind < 9 ? live[slot] : Range(0, kArenaBytes - 1);
                g_heapBlock[0] = g_arena[0] + offset;
                g_heapBlock[1] = g_arena[1] + offset;
            }
            bool okO = RunSide(HeapSide, true);
            bool okP = RunSide(HeapSide, false);
            g_cases++;
            int at = round * 10000 + op;
            if (!CheckFaults(okO, okP, names[g_heapOp], at) || !okO) {
                failed = true;
                break;
            }
            if (!Check(g_heapResult[0] == g_heapResult[1], "%s(%d), case %d: original %ld, port %ld",
                       names[g_heapOp], g_heapOp == 1 ? g_heapArg : 0, at, (long)g_heapResult[0],
                       (long)g_heapResult[1]) ||
                !CompareHeaps(names[g_heapOp], at)) {
                failed = true;
                break;
            }
            if (g_heapOp == 1 && g_heapResult[1] >= 0) {
                int n = 0;
                while (n < kLive && live[(slot + n) % kLive] >= 0)
                    n++;
                if (n < kLive)
                    live[(slot + n) % kLive] = g_heapResult[1];
            } else if (g_heapOp == 2) {
                for (int i = 0; i < kLive; i++)
                    if (live[i] == (intptr_t)((uint8_t *)g_heapBlock[1] - g_arena[1]) && Chance(90))
                        live[i] = -1;   // sometimes kept: freed again later
            }
        }
    }
    VirtualFree(g_arena[0], 0, MEM_RELEASE);
    VirtualFree(g_arena[1], 0, MEM_RELEASE);
}

// =================================================================================================================
// 5. The server clients
// =================================================================================================================

template <int N>
void FakeClient(void) {
    Log(0x40 + N);
}
typedef void (*ClientFn)(void);
const ClientFn kClients[8] = { FakeClient<0>, FakeClient<1>, FakeClient<2>, FakeClient<3>,
                               FakeClient<4>, FakeClient<5>, FakeClient<6>, FakeClient<7> };

int g_clientOp;
ClientFn g_clientArg;

void ClientSide(bool original) {
    typedef void (*ClientOpFn)(ClientFn);
    typedef void (*ServiceFn)(void);
    typedef int (*TaskFn)(int, int);
    switch (g_clientOp) {
    case 0: (original ? (ClientOpFn)0x0013f900 : &iSNDserveraddclient)(g_clientArg); break;
    case 1: (original ? (ClientOpFn)0x0013f920 : &iSNDserverremoveclient)(g_clientArg); break;
    case 2: (original ? (ServiceFn)0x0013f980 : &SNDSYS_service)(); break;
    default: Log(0x50, (uint32_t)(original ? (TaskFn)0x0013d3e0 : &SNDREAL_systemtask)(7, 3)); break;
    }
}

Globals g_snapshotGlobals, g_originalGlobals, g_portGlobals;

bool CompareGlobals(const char *what, int at) {
    return CheckBytes(g_originalGlobals.r0, g_portGlobals.r0, sizeof(g_portGlobals.r0), what, at) &&
           CheckBytes(g_originalGlobals.r1, g_portGlobals.r1, sizeof(g_portGlobals.r1), what, at) &&
           Check(g_originalGlobals.author == g_portGlobals.author, "%s, case %d: the .rdata byte differs", what, at);
}

// Snapshot the globals, run both sides from it, keep both results, leave the port's in place.
bool RunBoth(SideFn fn, const char *what, int at, uint8_t *space = NULL, uint8_t *spaceSnapshot = NULL,
             uint8_t *spaceOriginal = NULL, size_t spaceBytes = 0) {
    SaveGlobals(&g_snapshotGlobals);
    if (space != NULL)
        memcpy(spaceSnapshot, space, spaceBytes);
    LogReset();
    bool okO = RunSide(fn, true);
    SaveGlobals(&g_originalGlobals);
    if (space != NULL) {
        memcpy(spaceOriginal, space, spaceBytes);
        memcpy(space, spaceSnapshot, spaceBytes);
    }
    LoadGlobals(&g_snapshotGlobals);
    bool okP = RunSide(fn, false);
    SaveGlobals(&g_portGlobals);
    g_cases++;
    if (!CheckFaults(okO, okP, what, at))
        return false;
    bool same = CheckLogs(what, at) && CompareGlobals(what, at);
    if (same && space != NULL)
        same = CheckBytes(spaceOriginal, space, spaceBytes, what, at);
    return same;
}

void TestClients() {
    static const char *names[] = { "iSNDserveraddclient", "iSNDserverremoveclient", "SNDSYS_service",
                                   "SNDREAL_systemtask" };
    U8(0x00244ed5) = 0;
    for (int op = 0; op < 4000; op++) {
        int count = (int8_t)U8(0x00244ed5);
        int r = (int)(Next() % 100);
        g_clientOp = r < 35 ? 0 : r < 65 ? 1 : r < 85 ? 2 : 3;
        if (g_clientOp == 0 && count >= 6)
            g_clientOp = 1;
        g_clientArg = kClients[Next() % 8];
        U8(0x00244ed0) = (uint8_t)(Chance(80) ? 1 : 0);   // SNDSYS_is_inited
        if (!RunBoth(ClientSide, names[g_clientOp], op))
            break;
    }
}

// =================================================================================================================
// 6. SNDSYSI_100hzserver
// =================================================================================================================

const int kVoiceCount = 224;
struct ServerSpace {
    SND::Voice voices[kVoiceCount];
    SND::EnvSegment env[kVoiceCount][8];
    int8_t lfo[2][256];
    int8_t volTable[256];
    int8_t bendTable[256];
    int32_t results[8];
};
ServerSpace *g_ss, *g_ssSnapshot, *g_ssOriginal;
int g_ticks;

SND::Voice *FakeVoice(int voice) {
    return (SND::Voice *)(*(uint8_t **)(uintptr_t)0x00244f3cu + voice * 0x88);
}

// The platform driver (and the rest of what the system calls) as recording fakes
void FakeSetPitch(int voice) {
    Log(1, (uint32_t)voice, FakeVoice(voice)->pitch, FakeVoice(voice)->detuneLinear);
}
void FakeSetVol(int voice) {
    Log(2, (uint32_t)voice, (uint32_t)(uint8_t)FakeVoice(voice)->vol);
}
void FakeStop(int voice) {
    Log(3, (uint32_t)voice);
}
int g_capsResult, g_initResult, g_voicesToSet;
int FakeOutputCaps(void) {
    Log(4);
    U32(0x00244cd0) ^= 0x5a5a5a5au;   // a mark in the caps part the copies must carry
    return g_capsResult;
}
int FakeOutputSet(void) {
    uint32_t h = 0;
    for (uint32_t a = 0x00244ce8; a < 0x00244de8; a += 4)
        h = h * 31 + U32(a);
    for (uint32_t a = 0x00244dd0; a < 0x00244de8; a += 4)
        h = h * 31 + U32(a);
    Log(5, h);
    S16(0x00244ed8) = (int16_t)g_voicesToSet;   // NUM_VOICES, as the real one sets it
    return 0;
}
int FakePlatformInit(void) {
    Log(6, (uint32_t)S16(0x00244ed8), U32(0x00244f3c), U32(0x00244f40));
    return g_initResult;
}
int FakePlatformRestore(void) {
    Log(7);
    return 0;
}
void FakeFxInit(int bus, int path) {
    Log(8, (uint32_t)bus, (uint32_t)path);
}
void FakeSetFxLevel(int voice, int bus) {
    Log(9, (uint32_t)voice, (uint32_t)bus);
}
void FakeSyncTaskAdd(uint32_t task, int interval, int delay) {
    Log(10, task, (uint32_t)interval, (uint32_t)delay);
}
void FakeAddExit(uint32_t callback) {
    Log(11, callback);
}
void FakeDummyNull(void) {
    Log(12);
}
void __stdcall FakeRtlInit(void *section) {
    Log(13, (uint32_t)(uintptr_t)section);
}
void __stdcall FakeRtlEnter(void *section) {
    Log(14, (uint32_t)(uintptr_t)section, U8(0x00244ed3));
}
void __stdcall FakeRtlLeave(void *section) {
    Log(15, (uint32_t)(uintptr_t)section, U8(0x00244ed3));
}
template <int N>
void FakeHook(void) {
    Log(16, N);
}
typedef void (*HookFn)(void);
const HookFn kRestoreHooks[6] = { FakeHook<0>, FakeHook<1>, FakeHook<2>, FakeHook<3>, FakeHook<4>, FakeHook<5> };
void FakeBankExit(int argument) {
    Log(17, (uint32_t)argument);
}
template <int N>
void Fake100Hz(void) {
    Log(0x60 + N);
}

void InstallFakes() {
    HookBoth(0x0013da00, (const void *)&SNDPLATFORM_outputcaps, (const void *)&FakeOutputCaps);
    HookBoth(0x0013dae0, (const void *)&SNDPLATFORM_outputset, (const void *)&FakeOutputSet);
    HookBoth(0x0013dc50, (const void *)&SNDPLATFORM_init, (const void *)&FakePlatformInit);
    HookBoth(0x0013ddc0, (const void *)&SNDPLATFORM_restore, (const void *)&FakePlatformRestore);
    HookBoth(0x0013de50, (const void *)&SNDPLATFORM_stop, (const void *)&FakeStop);
    HookBoth(0x0013df50, (const void *)&SNDPLATFORM_setvol, (const void *)&FakeSetVol);
    HookBoth(0x0013e320, (const void *)&SNDPLATFORM_setpitch, (const void *)&FakeSetPitch);
    HookBoth(0x00140a80, (const void *)&SNDPLATFORM_fxinit, (const void *)&FakeFxInit);
    HookBoth(0x00140070, (const void *)&SNDPLATFORM_setfxlevel, (const void *)&FakeSetFxLevel);
    bool syncTaskAdd = HookBoth(0x0010aa50, (const void *)&SYNCTASK_add, (const void *)&FakeSyncTaskAdd);
    bool addExit = HookBoth(0x0010b310, (const void *)&REAL_addexit, (const void *)&FakeAddExit);
    g_canVector = syncTaskAdd && addExit;
    HookInstall(0x000d3580, (const void *)&FakeDummyNull);
    for (int i = 0; i < 3; i++)
        g_importSaved[i] = U32(kImportSlots[i]);
    uint32_t fakes[3] = { (uint32_t)(uintptr_t)&FakeRtlInit, (uint32_t)(uintptr_t)&FakeRtlEnter,
                          (uint32_t)(uintptr_t)&FakeRtlLeave };
    SetImportSlots(fakes);
    g_importsOn = true;
}

void RemoveFakes() {
    HooksRemove();
    if (g_importsOn)
        SetImportSlots(g_importSaved);
    g_importsOn = false;
}

void ServerSide(bool original) {
    typedef void (*ServerFn)(void);
    ServerFn server = original ? (ServerFn)0x0013b7b0 : &SNDSYSI_100hzserver;
    for (int t = 0; t < g_ticks; t++)
        server();
}

void RandomVoice(ServerSpace *s, int i, int gen) {
    SND::Voice *v = &s->voices[i];
    for (int j = 0; j < 0x88; j++)
        ((uint8_t *)v)[j] = (uint8_t)Next();
    v->handle = Chance(90) ? (i | (gen << 8)) : -1;
    v->inUse = (uint8_t)(Chance(85) ? 1 : Chance(50) ? 0 : 2);
    v->key = (uint8_t)(Chance(80) ? 0 : Range(1, 3));
    v->master = -1;
    v->pitchLfo = Chance(40) ? s->lfo[1] : NULL;
    v->volLfo = Chance(40) ? s->lfo[0] : NULL;
    v->pitchLfoLength = (uint8_t)Range(0, 40);
    v->volLfoLength = (uint8_t)Range(0, 40);
    v->pitchLfoPos = (uint8_t)Range(0, 50);
    v->volLfoPos = (uint8_t)Range(0, 50);
    v->bendTable = Chance(30) ? s->bendTable + 128 : NULL;
    v->volTable = Chance(30) ? s->volTable + 128 : NULL;
    v->bendRange = (int16_t)Range(-1200, 1200);
    v->detune = (int16_t)Range(-2400, 2400);
    v->detuneLinear = (uint16_t)(Chance(50) ? 0 : Next());
    v->pitchLfoDepth = (int16_t)Range(-200, 200);
    v->builtinVol = (int8_t)Range(0, 127);
    if (Chance(50)) {
        v->fadeStep = 0;
    } else {
        v->fadeStep = Range(-0x80000, 0x80000);
        if (Chance(5))
            v->fadeStep = Chance(50) ? 0x7fffffff : (int32_t)0x80000001u;
    }
    v->fade = Range(-0x10000, 0x7f0000);
    v->fadeTarget = Range(-0x20000, 0x7f0000);
    if (Chance(30))   // a fade that lands exactly on its target
        v->fadeTarget = (int32_t)((uint32_t)v->fade + (uint32_t)v->fadeStep * (uint32_t)Range(1, 5));
    v->env = Range(0, 0x7f0000);
    v->envStep = Chance(30) ? 0 : Range(-0x40000, 0x40000);
    v->envTicks = Chance(10) ? Range(-3, 0) : Range(1, 6);
    v->envCount = (uint8_t)Range(1, 8);
    v->envCurrent = (uint8_t)Range(0, v->envCount - 1);
    if (Chance(3))
        v->envCount = (uint8_t)Range(0x80, 0xff);   // a negative count: stops at once
    v->envTable = s->env[i];
    for (int k = 0; k < 8; k++) {
        int ticks = Range(-2, 12);
        s->env[i][k].ticks = ticks == 0 ? 1 : ticks;
        s->env[i][k].level = Range(0, 127);
    }
}

void TestServer() {
    g_ss = (ServerSpace *)VirtualAlloc(NULL, sizeof(ServerSpace) * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_ss == NULL)
        return;
    g_ssSnapshot = g_ss + 1;
    g_ssOriginal = g_ss + 2;
    for (int c = 0; c < 300; c++) {
        LoadGlobals(&g_real);
        memset((void *)(uintptr_t)0x00244c48, 0, 0x30);   // no hardware buffers on the lists (iSNDserve: nothing)
        for (int j = 0; j < 256; j++) {
            g_ss->lfo[0][j] = (int8_t)Range(0, 127);
            g_ss->lfo[1][j] = (int8_t)Range(0, 127);
            g_ss->volTable[j] = (int8_t)Range(0, 127);
            g_ss->bendTable[j] = (int8_t)Range(0, 127);
        }
        int gen = Range(0, 0x7fffff);
        for (int i = 0; i < kVoiceCount; i++)
            RandomVoice(g_ss, i, gen);
        U32(0x00244f3c) = (uint32_t)(uintptr_t)g_ss->voices;
        S16(0x00244ed8) = (int16_t)(Chance(70) ? kVoiceCount : Range(0, kVoiceCount));
        U32(0x00244edc) = Next();
        U8(0x00244ed1) = (uint8_t)Range(0, 127);
        int clients = Chance(80) ? 0 : Range(1, 3);
        U8(0x00244ed4) = (uint8_t)clients;
        U32(0x00244ee0) = (uint32_t)(uintptr_t)&Fake100Hz<0>;
        U32(0x00244ee4) = (uint32_t)(uintptr_t)&Fake100Hz<1>;
        U32(0x00244ee8) = (uint32_t)(uintptr_t)&Fake100Hz<2>;
        g_ticks = Range(1, 30);
        if (!RunBoth(ServerSide, "SNDSYSI_100hzserver", c, (uint8_t *)g_ss, (uint8_t *)g_ssSnapshot,
                     (uint8_t *)g_ssOriginal, sizeof(ServerSpace)))
            break;
    }
    VirtualFree(g_ss, 0, MEM_RELEASE);
}

// =================================================================================================================
// 7. vectortoreal, init, stopall, restore, the critical section
// =================================================================================================================

const int kSystemArena = 0x40000;
struct SystemSpace {
    uint8_t arena[kSystemArena];
    int32_t results[16];
};
SystemSpace *g_sys, *g_sysSnapshot, *g_sysOriginal;
int g_sysSize, g_sysPhase, g_sysTicks;
uint32_t g_sysVoiceSeed;
bool g_sysExitHook;

void SystemSide(bool original) {
    typedef int (*IntFn)(void);
    typedef int (*InitFn)(void *, int);
    typedef void (*VoidFn)(void);
    int32_t *r = g_sys->results;
    r[0] = g_canVector ? (original ? (IntFn)0x0013d3f0 : &SNDSYS_vectortoreal)() : 0;
    r[1] = (original ? (InitFn)0x0013d140 : &SNDSYSI_init)(g_sys->arena + g_sysPhase, g_sysSize);
    r[2] = (original ? (IntFn)0x0013d310 : &SNDSYS_inited)();
    // the new voices given something to do (the same on both sides)
    uint8_t *voices = *(uint8_t **)(uintptr_t)0x00244f3cu;
    if (r[1] == 0 && voices != NULL) {
        uint32_t seed = g_sysVoiceSeed;
        int n = S16(0x00244ed8);
        for (int i = 0; i < n; i++) {
            SND::Voice *v = (SND::Voice *)(voices + i * 0x88);
            seed = seed * 1103515245u + 12345u;
            v->handle = (seed >> 9) & 1 ? (int32_t)(i | 0x100) : -1;
            v->inUse = (uint8_t)((seed >> 10) & 1);
            v->envTicks = 1000;
            v->fadeStep = (int32_t)((seed >> 11) & 0xffff) - 0x8000;
            v->fade = (int32_t)((seed >> 12) & 0x3fff) << 8;
            v->fadeTarget = 0x400000;
            v->renderMode = (uint16_t)((seed >> 13) & 1 ? 0x420 : 0x24);   // SNDfxmasterlevel finds its bus by it
        }
    }
    if (voices != NULL)
        for (int t = 0; t < g_sysTicks; t++)
            (original ? (VoidFn)0x0013b7b0 : &SNDSYSI_100hzserver)();
    (original ? (VoidFn)0x0013b950 : &SNDSYS_entercritical)();
    (original ? (VoidFn)0x0013b970 : &SNDSYS_leavecritical)();
    if (g_sysExitHook)
        r[3] = (original ? (IntFn)0x001413d0 : &SNDSYSI_exithook)();
    else
        r[3] = (original ? (IntFn)0x0013d320 : &SNDSYS_restore)();
    r[4] = (original ? (IntFn)0x0013d320 : &SNDSYS_restore)();   // again: -14 once down
    r[5] = (original ? (IntFn)0x0013d310 : &SNDSYS_inited)();
}

void TestSystem() {
    g_sys = (SystemSpace *)VirtualAlloc(NULL, sizeof(SystemSpace) * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_sys == NULL)
        return;
    g_sysSnapshot = g_sys + 1;
    g_sysOriginal = g_sys + 2;
    for (int c = 0; c < 120; c++) {
        LoadGlobals(&g_real);
        memset((void *)(uintptr_t)0x00244c48, 0, 0x30);
        // random options and state
        for (uint32_t a = 0x00244cc8; a < 0x00244de8; a += 4)
            U32(a) = Next();
        U8(0x00244d10) = (uint8_t)(Chance(50) ? 2 : Range(0, 6));
        U32(0x00244c2c) = Chance(50) ? 0 : 1;          // caps read
        U32(0x00244c28) = Next();
        U32(0x00244c30) = Chance(50) ? 0 : 1;          // systaskadded
        U16(0x00244cf0) = (uint16_t)Range(0, 40);      // NUM_BANKS (when the caps were read)
        U8(0x00244ed0) = (uint8_t)(Chance(10) ? 1 : 0);   // already up
        S16(0x00244ed8) = (int16_t)(Chance(50) ? 0 : Range(1, 224));
        U8(0x00244ed3) = (uint8_t)Next();
        for (int i = 0; i < 6; i++)
            U32(0x00244f20 + 4 * i) = Chance(50) ? 0 : (uint32_t)(uintptr_t)kRestoreHooks[i];
        U32(0x00244f2c) = Chance(50) ? 0 : (uint32_t)(uintptr_t)&FakeBankExit;
        U8(0x00244ed4) = 0;
        for (int i = 0; i < kSystemArena; i++)
            g_sys->arena[i] = (uint8_t)Next();
        memset(g_sys->results, 0x55, sizeof(g_sys->results));
        g_capsResult = Chance(15) ? -Range(1, 20) : Range(0, 5);
        g_initResult = Chance(15) ? -Range(1, 20) : 0;
        g_voicesToSet = Chance(80) ? 224 : Range(0, 300);
        g_sysPhase = (int)(Next() % 16);
        g_sysSize = Chance(70) ? 0x40000 - g_sysPhase : Range(0x10000, kSystemArena - g_sysPhase);
        if (U8(0x00244ed0) != 0) {   // already up: a heap and arrays to restore (set up by the port, before both)
            if (S16(0x00244ed8) == 0)
                S16(0x00244ed8) = 1;
            memset(g_sys->arena, 0, 0x100);
            SNDMEMI_init(g_sys->arena + g_sysPhase, g_sysSize);
            U32(0x00244f3c) = (uint32_t)(uintptr_t)(g_sys->arena + 0x200);   // not the heap's: freeing it is ignored
            for (int i = 0; i < S16(0x00244ed8); i++) {
                SND::Voice *v = (SND::Voice *)(g_sys->arena + 0x200 + i * 0x88);
                memset(v, 0, sizeof(*v));
                v->handle = Chance(50) ? (int32_t)(i | 0x300) : -1;
                v->inUse = (uint8_t)Chance(50);
                v->renderMode = (uint16_t)(Chance(50) ? 0x420 : 0x24);
            }
            U32(0x00244f40) = (uint32_t)(uintptr_t)SNDMEMI_alloc(0x40);
        }
        g_sysTicks = Range(0, 3);
        g_sysVoiceSeed = Next();
        g_sysExitHook = Chance(30);
        if (!RunBoth(SystemSide, "init/restore", c, (uint8_t *)g_sys, (uint8_t *)g_sysSnapshot,
                     (uint8_t *)g_sysOriginal, sizeof(SystemSpace)))
            break;
    }
    VirtualFree(g_sys, 0, MEM_RELEASE);
}

}   // namespace

void SndSystemShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_SNDSYSSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;

    int patched = 0, total = 0;
    for (const unsigned *a = kModuleA; *a != 0; a++) {
        total++;
        if (XbeOriginal_Restore(*a, true)) {
            patched++;
            XbeOriginal_Restore(*a, false);
        }
    }
    if (patched != total)
        printf("[sndsysshadow] only %d of module A's %d entries are patched: the others compare the original with "
               "itself\n", patched, total);

    SaveGlobals(&g_real);
    uint32_t heapPointer = U32(kHeapPointer);

    TestArith();
    TestRandom();
    LoadGlobals(&g_real);
    TestLists();
    TestHeap();
    U32(kHeapPointer) = heapPointer;
    InstallFakes();
    TestClients();
    LoadGlobals(&g_real);
    TestServer();
    LoadGlobals(&g_real);
    TestSystem();
    RemoveFakes();
    LoadGlobals(&g_real);

    printf("[sndsysshadow] module A (system, heap, lists, random, 100 Hz server, init/restore): %d cases, %d checks, "
           "%d differ (%d cases fault on both sides)\n", g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
