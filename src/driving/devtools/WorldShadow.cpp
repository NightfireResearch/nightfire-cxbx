#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "WorldShadow.h"
#include "FpControl.h"

#include "../engine/UMemory.hpp"
#include "../world/SoundGroup.h"
#include "../world/SoundMap.h"
#include "../world/VisCurtain.h"
#include "../world/World.h"
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_WORLDSHADOW=1, from the first simulation tick on the open track: the pure ports against the originals
// (the ports' entry bytes swapped back in for each original call, common/xbeOriginal.h), on identical inputs,
// compared byte for byte (floats: equal bits, or both NaN).
//
//   - The curtains: random curtains (some degenerate) built by WVisCurtain's constructor, added by AddActiveCurtain
//     from random eyes in batches past the list's limit, the whole active list compared; then
//     IsVisibleAgainstCurtains at random spheres and at spheres placed behind the curtains. The active list is put
//     back to the same state before each side's run, and as it was at the end.
//   - WSound::SetUnknown118 on random sound images (zero sums, sums equal to the old one, NaN), the whole 0x160
//     bytes compared.
//   - The two map head allocators: the nodes' bytes.
//   - WWorld's queries over the open world: GetSceneObjFromInstance and GetProcAnimStateFromInstance for every
//     render instance and pointers either side of the array, GetEnviroDesc for every render index.
//   - SetTrackName (the global put back after).
//   - The scene object vector: random reserve / push_back / insert / insert-n (in the middle, past the end, from
//     an element of the vector itself) / tidy sequences, size, capacity and contents compared after each step.
//
// LoadTrackFile, Open, Reset, Close, the constructor and destructor and SetVoice change the game's state far and
// wide; lockstep runs test them in game.
//
// One mutation this catches: AddActiveCurtain crossing toStart with down in the other order turns the eye-side
// plane round, and every added curtain's second column differs.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[4][2] = {
    { 0x000595c0, 0x00059630 }, { 0x00094030, 0x000940c0 }, { 0x000d0d50, 0x000d1060 }, { 0x000d11a0, 0x000d2860 },
};

void OriginalWindow(bool original) {
    for (const uint32_t *range : kRanges)
        XbeOriginal_RestoreRange(range[0], range[1], original);
}

// ---- the originals

typedef void (*InitCurtainsFn)(void);
typedef void (*AddCurtainFn)(const WVisCurtain *, const Coord4 *);
typedef bool (*VisibleFn)(const Coord4 *, float);
typedef WVisCurtain *(__fastcall *CurtainConstructFn)(WVisCurtain *, int, const Coord4 *, const Coord4 *);
typedef void (__fastcall *SoundSetFn)(WSound *, int, float, float);
typedef void *(*BuyHeadFn)(void);
typedef RSceneObj *(__fastcall *SceneObjFn)(WWorld *, int, const CARP::Instance *);
typedef ProcAnimState *(__fastcall *ProcAnimFn)(WWorld *, int, const CARP::Instance *);
typedef EnviroDesc *(__fastcall *EnviroFn)(WWorld *, int, const WWorldPos *);
typedef void (*TrackNameFn)(const char *, bool);
typedef uint32_t (__fastcall *ListSizeFn)(WorldSceneObjectList *, int);
typedef void (__fastcall *ListReserveFn)(WorldSceneObjectList *, int, uint32_t);
typedef void (__fastcall *ListPushFn)(WorldSceneObjectList *, int, const WorldSceneObject *);
typedef WorldSceneObject **(__fastcall *ListInsertFn)(WorldSceneObjectList *, int, WorldSceneObject **,
                                                      WorldSceneObject *, const WorldSceneObject *);
typedef void (__fastcall *ListInsertNFn)(WorldSceneObjectList *, int, WorldSceneObject *, uint32_t,
                                         const WorldSceneObject *);
typedef void (__fastcall *ListTidyFn)(WorldSceneObjectList *, int);

#define Orig_InitActiveCurtainList ((InitCurtainsFn)0x000d0d50)
#define Orig_AddActiveCurtain ((AddCurtainFn)0x000d0d60)
#define Orig_IsVisibleAgainstCurtains ((VisibleFn)0x000d0f50)
#define Orig_CurtainConstruct ((CurtainConstructFn)0x000d0ff0)
#define Orig_SetUnknown118 ((SoundSetFn)0x000d11a0)
#define Orig_MapBuyHead ((BuyHeadFn)0x00094030)
#define Orig_RefCounterMapBuyHead ((BuyHeadFn)0x00094070)
#define Orig_GetSceneObjFromInstance ((SceneObjFn)0x000d14a0)
#define Orig_GetProcAnimStateFromInstance ((ProcAnimFn)0x000d14e0)
#define Orig_GetEnviroDesc ((EnviroFn)0x000d1470)
#define Orig_SetTrackName ((TrackNameFn)0x000d1210)
#define Orig_ListSize ((ListSizeFn)0x000d1520)
#define Orig_ListReserve ((ListReserveFn)0x000d1f50)
#define Orig_ListPushBack ((ListPushFn)0x000d2110)
#define Orig_ListInsert ((ListInsertFn)0x000d20a0)
#define Orig_ListInsertN ((ListInsertNFn)0x000d1c20)
#define Orig_ListTidy ((ListTidyFn)0x000d1ae0)

// ---- the game's state

#define CurtainState ((uint8_t *)0x0023e280)            // the count, then the 64 matrices at 0x0023e290
const uint32_t kCurtainStateBytes = 0x0023f290 - 0x0023e280;
#define ActiveCurtainCount I32_AT(0x0023e280)
#define ActiveCurtains ((const float *)0x0023e290)
#define TrackNameBytes ((uint8_t *)0x0023f290)
const uint32_t kTrackNameBytes = 0x80;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0, g_nanOnly = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[world]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
    Differ(what, index, detail);
}

void CheckU32(const char *what, int index, uint32_t a, uint32_t b) {
    g_checks++;
    if (a == b)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "original %08x, port %08x", a, b);
    Differ(what, index, detail);
}

// Equal bits, or both NaN (counted apart: where two NaNs meet the x87 and SSE pick different ones)
void CheckFloats(const char *what, int index, const float *a, const float *b, int count) {
    for (int i = 0; i < count; i++) {
        g_checks++;
        if (memcmp(&a[i], &b[i], sizeof(float)) == 0)
            continue;
        if (a[i] != a[i] && b[i] != b[i]) {
            g_nanOnly++;
            continue;
        }
        char detail[96];
        snprintf(detail, sizeof(detail), "float %d of %d: original %.9g, port %.9g", i, count, a[i], b[i]);
        Differ(what, index, detail);
    }
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

// Runs one side; a fault is counted, not fatal. The original runs inside the window that swaps the ported
// functions' originals back in.
bool Guarded(CaseFn run, void *context, bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(context, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(context, original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

// ---- random inputs (a generator of our own: the game's is not touched)

uint32_t g_seed = 0x5eed0a17;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

float QuietNaN() {
    uint32_t bits = 0x7fc00000;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

Coord4 RandomPoint(float range) {
    Coord4 p = { Uniform(-range, range), Uniform(-range / 4, range / 4), Uniform(-range, range), Uniform(-5, 5) };
    return p;
}

// ---- the curtains

struct CurtainCase {
    Coord4 start, end, eye;
    WVisCurtain curtain;
};

struct CurtainBatch {
    CurtainCase cases[72];
    int count;
};

void RunCurtainBatch(void *context, bool original) {
    CurtainBatch *batch = static_cast<CurtainBatch *>(context);
    if (original)
        Orig_InitActiveCurtainList();
    else
        InitActiveCurtainList();
    for (int i = 0; i < batch->count; i++) {
        CurtainCase &c = batch->cases[i];
        if (original) {
            Orig_CurtainConstruct(&c.curtain, 0, &c.start, &c.end);
            Orig_AddActiveCurtain(&c.curtain, &c.eye);
        } else {
            c.curtain.Construct(&c.start, &c.end);
            AddActiveCurtain(&c.curtain, &c.eye);
        }
    }
}

struct VisibleCase {
    Coord4 sphere;
    float farMargin;
    bool result;
};

void RunVisible(void *context, bool original) {
    VisibleCase *c = static_cast<VisibleCase *>(context);
    c->result = original ? Orig_IsVisibleAgainstCurtains(&c->sphere, c->farMargin)
                         : IsVisibleAgainstCurtains(&c->sphere, c->farMargin);
}

void MakeCurtainBatch(CurtainBatch *batch, int count) {
    batch->count = count;
    Coord4 eye = RandomPoint(400.0f);
    for (int i = 0; i < count; i++) {
        CurtainCase &c = batch->cases[i];
        memset(&c, 0, sizeof(c));
        c.start = RandomPoint(300.0f);
        c.end = c.start;
        c.end.x += Uniform(-60, 60);
        c.end.y += Uniform(1, 40);
        c.end.z += Uniform(-60, 60);
        switch (Next() % 16) {
        case 0: c.end.x = c.start.x; c.end.z = c.start.z; break;    // no length
        case 1: c.end.y = c.start.y; break;                         // no height
        case 2: c.end = c.start; break;
        default: break;
        }
        c.eye = (Next() % 4 == 0) ? RandomPoint(400.0f) : eye;
    }
}

void TestCurtains() {
    static CurtainBatch batch, batchCopy;
    static uint8_t saved[kCurtainStateBytes], before[kCurtainStateBytes], afterOriginal[kCurtainStateBytes];
    memcpy(saved, CurtainState, kCurtainStateBytes);
    for (int b = 0; b < 60; b++) {
        MakeCurtainBatch(&batch, b % 3 == 0 ? 72 : 1 + int(Next() % 40));
        memcpy(&batchCopy, &batch, sizeof(batch));
        memcpy(before, CurtainState, kCurtainStateBytes);
        g_cases++;
        bool ok = Guarded(RunCurtainBatch, &batch, true);
        memcpy(afterOriginal, CurtainState, kCurtainStateBytes);
        memcpy(CurtainState, before, kCurtainStateBytes);
        ok &= Guarded(RunCurtainBatch, &batchCopy, false);
        if (!ok)
            continue;
        for (int i = 0; i < batch.count; i++)
            CheckFloats("curtain", b * 100 + i, &batch.cases[i].curtain.start.x, &batchCopy.cases[i].curtain.start.x, 8);
        CheckU32("active curtain count", b, *reinterpret_cast<const uint32_t *>(afterOriginal), uint32_t(ActiveCurtainCount));
        CheckFloats("active curtains", b, reinterpret_cast<const float *>(afterOriginal + 0x10), ActiveCurtains,
                    16 * kMaxActiveCurtains);

        // spheres at random, and behind the curtains as seen from their eyes
        for (int s = 0; s < 80; s++) {
            VisibleCase c;
            const CurtainCase &curtain = batch.cases[Next() % batch.count];
            if (s % 2 == 0) {
                c.sphere = RandomPoint(400.0f);
            } else {
                float t = Uniform(0.0f, 1.0f), beyond = Uniform(0.05f, 2.0f);
                Coord4 at = { curtain.start.x + (curtain.end.x - curtain.start.x) * t,
                              curtain.start.y + (curtain.end.y - curtain.start.y) * Uniform(0.0f, 1.0f),
                              curtain.start.z + (curtain.end.z - curtain.start.z) * t, 0.0f };
                c.sphere.x = at.x + (at.x - curtain.eye.x) * beyond;
                c.sphere.y = at.y;
                c.sphere.z = at.z + (at.z - curtain.eye.z) * beyond;
            }
            c.sphere.w = (s % 11 == 0) ? QuietNaN() : Uniform(0.0f, 20.0f);
            c.farMargin = (s % 7 == 0) ? -Uniform(0.0f, 20.0f) : Uniform(0.0f, 50.0f);
            VisibleCase port = c;
            g_cases++;
            if (Guarded(RunVisible, &c, true) && Guarded(RunVisible, &port, false))
                CheckU32("IsVisibleAgainstCurtains", b * 100 + s, c.result, port.result);
        }
    }
    memcpy(CurtainState, saved, kCurtainStateBytes);
}

// ---- WSound::SetUnknown118

struct SoundCase {
    alignas(16) uint8_t image[sizeof(WSound)];
    float first, second;
};

void RunSound(void *context, bool original) {
    SoundCase *c = static_cast<SoundCase *>(context);
    WSound *sound = reinterpret_cast<WSound *>(c->image);
    if (original)
        Orig_SetUnknown118(sound, 0, c->first, c->second);
    else
        sound->SetUnknown118(c->first, c->second);
}

float SoundValue() {
    switch (Next() % 8) {
    case 0: return 0.0f;
    case 1: return QuietNaN();
    case 2: return -0.0f;
    default: return Uniform(-30.0f, 30.0f);
    }
}

void TestSound() {
    static SoundCase c, port;
    for (int i = 0; i < 2000; i++) {
        for (size_t b = 0; b < sizeof(c.image); b++)
            c.image[b] = uint8_t(Next());
        WSound *sound = reinterpret_cast<WSound *>(c.image);
        sound->unknown118 = SoundValue();
        sound->unknown11c = SoundValue();
        c.first = SoundValue();
        c.second = SoundValue();
        switch (i % 4) {
        case 0: c.second = -c.first; break;                                 // a zero sum
        case 1: c.first = sound->unknown118; c.second = sound->unknown11c; break;   // the old sum
        default: break;
        }
        memcpy(&port, &c, sizeof(c));
        g_cases++;
        if (Guarded(RunSound, &c, true) && Guarded(RunSound, &port, false))
            CheckBytes("WSound::SetUnknown118", i, c.image, port.image, sizeof(c.image));
    }
}

// ---- the map heads

struct HeadCase {
    BuyHeadFn original;
    bool refCounter;
    void *node;
};

void RunHead(void *context, bool original) {
    HeadCase *c = static_cast<HeadCase *>(context);
    if (original)
        c->node = c->original();
    else
        c->node = c->refCounter ? static_cast<void *>(RefCounterMapBuyHead()) : static_cast<void *>(MapBuyHead());
}

void TestHeads() {
    for (int i = 0; i < 2; i++) {
        HeadCase c = { i == 0 ? Orig_MapBuyHead : Orig_RefCounterMapBuyHead, i == 1, NULL };
        HeadCase port = c;
        uint32_t bytes = i == 0 ? sizeof(TreeNode) : sizeof(RefCounterNode);
        g_cases++;
        if (Guarded(RunHead, &c, true) && Guarded(RunHead, &port, false))
            CheckBytes(i == 0 ? "MapBuyHead" : "RefCounterMapBuyHead", i, c.node, port.node, bytes);
        if (c.node != NULL)
            UMemory::FastFree(c.node, bytes);
        if (port.node != NULL)
            UMemory::FastFree(port.node, bytes);
    }
}

// ---- the world's queries

struct QueryCase {
    const CARP::Instance *instance;
    WWorldPos position;
    void *sceneObj, *procAnim, *enviro;
    bool enviroOnly;
};

void RunQuery(void *context, bool original) {
    QueryCase *c = static_cast<QueryCase *>(context);
    WWorld *world = fgWorld;
    if (c->enviroOnly) {
        c->enviro = original ? Orig_GetEnviroDesc(world, 0, &c->position) : world->GetEnviroDesc(&c->position);
        return;
    }
    c->sceneObj = original ? Orig_GetSceneObjFromInstance(world, 0, c->instance)
                           : world->GetSceneObjFromInstance(c->instance);
    c->procAnim = original ? Orig_GetProcAnimStateFromInstance(world, 0, c->instance)
                           : world->GetProcAnimStateFromInstance(c->instance);
}

void TestQueries() {
    WWorld *world = fgWorld;
    if (world->instances == NULL) {
        printf("[world] no render instances - the queries skipped\n");
        return;
    }
    const uint8_t *base = reinterpret_cast<const uint8_t *>(world->instances);
    int count = int(world->instanceCount);
    for (int i = -2; i < count + 2; i++) {
        QueryCase c;
        memset(&c, 0, sizeof(c));
        c.instance = reinterpret_cast<const CARP::Instance *>(base + i * int(sizeof(CARP::Instance)));
        if (i >= 0 && i < count && (world->instances[i].flags & kWorldInstanceProcAnim) && world->procAnims == NULL)
            continue;
        QueryCase port = c;
        g_cases++;
        if (Guarded(RunQuery, &c, true) && Guarded(RunQuery, &port, false)) {
            CheckU32("GetSceneObjFromInstance", i, uint32_t(uintptr_t(c.sceneObj)), uint32_t(uintptr_t(port.sceneObj)));
            CheckU32("GetProcAnimStateFromInstance", i, uint32_t(uintptr_t(c.procAnim)),
                     uint32_t(uintptr_t(port.procAnim)));
        }
    }

    if (world->enviroDescs == NULL)
        return;
    static WCollisionInstance instance;
    for (int i = 0; i < count; i++) {
        QueryCase c;
        memset(&c, 0, sizeof(c));
        instance.renderIndex = uint16_t(i);
        c.position.instance = &instance;
        c.enviroOnly = true;
        QueryCase port = c;
        g_cases++;
        if (Guarded(RunQuery, &c, true) && Guarded(RunQuery, &port, false))
            CheckU32("GetEnviroDesc", i, uint32_t(uintptr_t(c.enviro)), uint32_t(uintptr_t(port.enviro)));
    }
}

// ---- SetTrackName

struct TrackNameCase {
    char name[0x40];
    bool copy;
};

void RunTrackName(void *context, bool original) {
    TrackNameCase *c = static_cast<TrackNameCase *>(context);
    if (original)
        Orig_SetTrackName(c->name, c->copy);
    else
        WWorld::SetTrackName(c->name, c->copy);
}

void TestTrackName() {
    static uint8_t saved[kTrackNameBytes], afterOriginal[kTrackNameBytes];
    memcpy(saved, TrackNameBytes, kTrackNameBytes);
    for (int i = 0; i < 20; i++) {
        TrackNameCase c;
        memset(&c, 0, sizeof(c));
        int length = int(Next() % 0x3f);
        for (int k = 0; k < length; k++)
            c.name[k] = char('a' + Next() % 26);
        c.copy = i % 3 != 0;
        memset(TrackNameBytes, 0xcd, kTrackNameBytes);
        g_cases++;
        bool ok = Guarded(RunTrackName, &c, true);
        memcpy(afterOriginal, TrackNameBytes, kTrackNameBytes);
        memset(TrackNameBytes, 0xcd, kTrackNameBytes);
        if (ok && Guarded(RunTrackName, &c, false))
            CheckBytes("SetTrackName", i, afterOriginal, TrackNameBytes, kTrackNameBytes);
    }
    memcpy(TrackNameBytes, saved, kTrackNameBytes);
}

// ---- the scene object vector

enum ListOp { kOpReserve, kOpPushBack, kOpInsert, kOpInsertN, kOpTidy, kOpCount };

struct ListCase {
    WorldSceneObjectList list;
    int op;
    uint32_t count;
    uint32_t where;             // an index
    int valueFrom;              // -1: value, else an element of the vector
    WorldSceneObject value;
    uint32_t inserted;          // Insert's answer, as an index
    uint32_t size;
};

void RunList(void *context, bool original) {
    ListCase *c = static_cast<ListCase *>(context);
    WorldSceneObjectList *list = &c->list;
    WorldSceneObject *where = list->first + c->where;
    const WorldSceneObject *value = c->valueFrom < 0 ? &c->value : &list->first[c->valueFrom];
    WorldSceneObject *result = NULL;
    switch (c->op) {
    case kOpReserve:
        original ? Orig_ListReserve(list, 0, c->count) : list->Reserve(c->count);
        break;
    case kOpPushBack:
        original ? Orig_ListPushBack(list, 0, value) : list->PushBack(value);
        break;
    case kOpInsert:
        original ? (void)Orig_ListInsert(list, 0, &result, where, value) : (void)list->Insert(&result, where, value);
        c->inserted = uint32_t(result - list->first);
        break;
    case kOpInsertN:
        original ? Orig_ListInsertN(list, 0, where, c->count, value) : list->InsertN(where, c->count, value);
        break;
    case kOpTidy:
        original ? Orig_ListTidy(list, 0) : list->Tidy();
        break;
    }
    c->size = original ? Orig_ListSize(list, 0) : list->Size();
}

void TestList() {
    static ListCase original, port;
    for (int sequence = 0; sequence < 150; sequence++) {
        memset(&original, 0, sizeof(original));
        memset(&port, 0, sizeof(port));
        int steps = 1 + int(Next() % 30);
        bool failed = false;
        for (int step = 0; step < steps && !failed; step++) {
            uint32_t size = port.list.Size();
            original.op = int(Next() % kOpCount);
            if (original.op == kOpTidy && Next() % 3 != 0)
                original.op = kOpPushBack;
            original.count = original.op == kOpReserve ? Next() % 48 : Next() % 12;
            original.where = size == 0 ? 0 : Next() % (size + 1);
            original.valueFrom = (size != 0 && Next() % 4 == 0) ? int(Next() % size) : -1;
            for (size_t b = 0; b < sizeof(original.value); b++)
                reinterpret_cast<uint8_t *>(&original.value)[b] = uint8_t(Next());
            port.op = original.op;
            port.count = original.count;
            port.where = original.where;
            port.valueFrom = original.valueFrom;
            port.value = original.value;
            g_cases++;
            if (!Guarded(RunList, &original, true) || !Guarded(RunList, &port, false)) {
                failed = true;
                break;
            }
            int index = sequence * 100 + step;
            CheckU32("vector size", index, original.size, port.size);
            CheckU32("vector capacity", index, original.list.Capacity(), port.list.Capacity());
            if (original.op == kOpInsert)
                CheckU32("vector insert", index, original.inserted, port.inserted);
            if (original.size == port.size && original.size != 0)
                CheckBytes("vector contents", index, original.list.first, port.list.first,
                           original.size * sizeof(WorldSceneObject));
        }
        if (!failed) {
            original.op = port.op = kOpTidy;
            Guarded(RunList, &original, true);
            Guarded(RunList, &port, false);
        }
    }
}

}  // namespace

void WorldShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_WORLDSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestCurtains();
    TestSound();
    TestHeads();
    TestTrackName();
    TestList();
    if (fgWorld != NULL)
        TestQueries();
    else
        printf("[world] no world - the queries skipped\n");
    ResetFpu();
    printf("[world] curtains, sound, map heads, world queries, scene object vector vs originals: %d cases, %d checks, "
           "%d differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_nanOnly != 0)
        printf("[world]   %d results both NaN with different bits\n", g_nanOnly);
    if (g_faults != 0)
        printf("[world]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
