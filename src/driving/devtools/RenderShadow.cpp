#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "RenderShadow.h"
#include "FpControl.h"

#include "../world/Render.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_RENDERSHADOW=1, from the first simulation tick on the open track: world/Render.cpp against the
// originals (0x000c7330-0x000c8940 swapped back in for each original run, common/xbeOriginal.h), on identical
// inputs, compared byte for byte. Before each side's run the state the functions write is put back: the statics
// 0x0023b3c0-0x0023e030 (the visible list, the view block, fgRender, GetNextPoint's cursor, the current list), the
// active curtains, the RRenderWorldCulling and the WRender.
//
//   - GenerateCurtainsAndNodes (with FindVisibleTreeNodesAndCurtains) for the live frustum and for frustums set up
//     by the original RRenderWorldCulling::Setup2dFrustrum at random points and headings over the map and near the
//     live eye: the visible node list, the active curtains, the WRender compared.
//   - The culling's walk on that list, on the same nodes reversed with repeats, and on an empty list:
//     PrepareForCull, then GetNextPoint to the end (every output compared) with SetCull keeping or culling each
//     instance by a rule of our own, then SetCull past the list's limit; the lists and the cursor compared.
//   - CopyDrawPasses for every pass range, from the walk's draws and from draws at and around the passes' ends.
//   - SimpleDrawVisibleTreeNodes for the same points, headings and view radii (the view block set by us).
//   - std::sort (RenderSort): Sort on random draws (few, many and all equal keys, both predicates, small ideals to
//     reach the heap sort), and each helper on its own.
//
// DrawPass, DrawWorld, DrawWorldAtPoint, the constructor, Init and ShutDown draw or load; lockstep runs test them.
//
// One mutation this catches: FindVisibleTreeNodesAndCurtains visiting the children in the order 0, 1, 2, 3 (the
// original's is 0, 1, 3, 2) changes the order of the visible node list in every case that reaches a fourth child.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x000c7330, 0x000c8940, original);
}

// ---- the originals

typedef CachedDrawInfo *(__fastcall *GenerateFn)(WRender *, int, const RCamera *, float);
typedef void (*PrepareFn)(CachedDrawInfo *);
typedef int (*NextPointFn)(Coord4 *, float *, bool *, float *);
typedef void (*SetCullFn)(int, bool, float);
typedef void (__fastcall *CopyPassesFn)(WRender *, int, const CachedDrawInfo *, CachedDrawInfo *, int, int);
typedef void (__fastcall *TreeWalkFn)(WRender *, int, WMapNode *, const Coord4 *, bool, int);
typedef void (*SortFn)(WRenderSortEntry *, WRenderSortEntry *, int, RenderSortPredicate);
typedef void (*RangeFn)(WRenderSortEntry *, WRenderSortEntry *, RenderSortPredicate);
typedef void (*MakeHeapFn)(WRenderSortEntry *, WRenderSortEntry *, RenderSortPredicate, int, int);
typedef void (*RotateFn)(WRenderSortEntry *, WRenderSortEntry *, WRenderSortEntry *, int, int);
typedef void (*ThreeFn)(WRenderSortEntry *, WRenderSortEntry *, WRenderSortEntry *, RenderSortPredicate);
typedef RenderSortRange *(*PartitionFn)(RenderSortRange *, WRenderSortEntry *, WRenderSortEntry *, RenderSortPredicate);
typedef void (*HeapFn)(WRenderSortEntry *, int, int, WRenderSortEntry, RenderSortPredicate);
typedef void (__fastcall *SetupFrustumFn)(void *, int, const Coord4 *position, const float *frame, float, float);

#define Orig_GenerateCurtainsAndNodes ((GenerateFn)0x000c7510)
#define Orig_PrepareForCull ((PrepareFn)0x000c75b0)
#define Orig_GetNextPoint ((NextPointFn)0x000c75e0)
#define Orig_SetCull ((SetCullFn)0x000c7700)
#define Orig_CopyDrawPasses ((CopyPassesFn)0x000c7750)
#define Orig_SimpleDrawVisibleTreeNodes ((TreeWalkFn)0x000c77f0)
#define Orig_PushHeap ((HeapFn)0x000c7920)
#define Orig_Rotate ((RotateFn)0x000c7990)
#define Orig_Med3 ((ThreeFn)0x000c7d70)
#define Orig_AdjustHeap ((HeapFn)0x000c7df0)
#define Orig_Median ((ThreeFn)0x000c7e80)
#define Orig_MakeHeap ((MakeHeapFn)0x000c7f20)
#define Orig_UnguardedPartition ((PartitionFn)0x000c7f70)
#define Orig_InsertionSort ((RangeFn)0x000c8180)
#define Orig_SortHeap ((RangeFn)0x000c8220)
#define Orig_Sort ((SortFn)0x000c8280)
#define RRenderWorldCulling_Setup2dFrustrum ((SetupFrustumFn)0x0008d440)

// ---- the game's state

#define RenderStatics ((uint8_t *)0x0023b3c0)
const uint32_t kRenderStaticsBytes = 0x0023e030 - 0x0023b3c0;
#define CurtainState ((uint8_t *)0x0023e280)
const uint32_t kCurtainStateBytes = 0x0023f290 - 0x0023e280;
#define WorldCulling ((uint8_t *)0x001f2c80)
const uint32_t kWorldCullingBytes = 0xa0;
#define CullingFov FLOAT_AT(0x001f2ce4)
#define CullingFar FLOAT_AT(0x001f2ce8)
#define CullingEye (*(const Coord4 *)0x001f2cd0)
#define AtPointRadius FLOAT_AT(0x001c483c)          // what DrawWorldAtPoint's caller passes

#define VisibleList (*(CachedDrawInfo *)0x0023b3c0)
#define CurrentList (*(CachedDrawInfo **)0x0023e014)
#define ViewCull (*(WViewCull *)0x0023dfd0)

struct Snapshot {
    uint8_t statics[kRenderStaticsBytes];
    uint8_t curtains[kCurtainStateBytes];
    uint8_t culling[kWorldCullingBytes];
    uint8_t render[sizeof(WRender)];
};

void Take(Snapshot *s) {
    memcpy(s->statics, RenderStatics, sizeof(s->statics));
    memcpy(s->curtains, CurtainState, sizeof(s->curtains));
    memcpy(s->culling, WorldCulling, sizeof(s->culling));
    memcpy(s->render, fgRender, sizeof(s->render));
}

void Put(const Snapshot *s) {
    memcpy(RenderStatics, s->statics, sizeof(s->statics));
    memcpy(CurtainState, s->curtains, sizeof(s->curtains));
    memcpy(WorldCulling, s->culling, sizeof(s->culling));
    memcpy(fgRender, s->render, sizeof(s->render));
}

Snapshot g_base, g_pre, g_afterOriginal, g_afterPort;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[render]   %s #%d: %s\n", what, index, detail);
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

void CheckSnapshots(const char *what, int index) {
    CheckBytes(what, index, g_afterOriginal.statics, g_afterPort.statics, sizeof(g_afterPort.statics));
    CheckBytes(what, index, g_afterOriginal.curtains, g_afterPort.curtains, sizeof(g_afterPort.curtains));
    CheckBytes(what, index, g_afterOriginal.culling, g_afterPort.culling, sizeof(g_afterPort.culling));
    CheckBytes(what, index, g_afterOriginal.render, g_afterPort.render, sizeof(g_afterPort.render));
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

// Runs one side; a fault is counted, not fatal. The original runs inside the window.
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

// Both sides from the state as it is now, the state after each kept
bool SideBySide(CaseFn run, void *original, void *port) {
    g_cases++;
    Take(&g_pre);
    bool ok = Guarded(run, original, true);
    Take(&g_afterOriginal);
    Put(&g_pre);
    ok = Guarded(run, port, false) && ok;
    Take(&g_afterPort);
    return ok;
}

// ---- random inputs (a generator of our own: the game's is not touched)

uint32_t g_seed = 0x5eedc733;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

// The map's square as WRender makes it
Coord4 RootBox() {
    const WMapHeader *map = fgWorld->map;
    double half = double(map->size) * 0.5f;
    Coord4 box = { float(half + map->corner.x), map->corner.y, float(half + map->corner.z), float(half) };
    return box;
}

// ---- the tree walk for a camera

// One camera for both sides: GenerateCurtainsAndNodes keeps its address in the WRender
RCamera g_camera;

struct GenerateCase {
    CachedDrawInfo *result;
};

void RunGenerate(void *context, bool original) {
    GenerateCase *c = static_cast<GenerateCase *>(context);
    c->result = original ? Orig_GenerateCurtainsAndNodes(fgRender, 0, &g_camera, 120.0f)
                         : fgRender->GenerateCurtainsAndNodes(&g_camera, 120.0f);
}

// ---- the culling's walk

const int kMaxWalk = 8192;

struct WalkRecord {
    int item;
    Coord4 sphere;
    float height;
    uint8_t checkFar;
    float farScale;
};

struct WalkCase {
    const CachedDrawInfo *source;       // NULL: walk the visible list itself
    CachedDrawInfo list;                // the walked list after the side's run
    WalkRecord records[kMaxWalk];
    int count;
};

WalkCase g_walk[2];
CachedDrawInfo g_walkList;              // one list for both sides: the cursor keeps its address

void RunWalk(void *context, bool original) {
    WalkCase *c = static_cast<WalkCase *>(context);
    CachedDrawInfo *list = &VisibleList;
    if (c->source != NULL) {
        g_walkList = *c->source;
        list = &g_walkList;
    }
    memset(c->records, 0xaa, sizeof(c->records));
    c->count = 0;
    original ? Orig_PrepareForCull(list) : WRender::PrepareForCull(list);
    for (int step = 0; step < kMaxWalk; step++) {
        WalkRecord &r = c->records[step];
        bool *checkFar = reinterpret_cast<bool *>(&r.checkFar);
        r.item = original ? Orig_GetNextPoint(&r.sphere, &r.height, checkFar, &r.farScale)
                          : WRender::GetNextPoint(&r.sphere, &r.height, checkFar, &r.farScale);
        c->count = step + 1;
        if (r.item == WRender::kNoMorePoints)
            break;
        bool culled = (uint32_t(r.item) >> 6) % 3 == uint32_t(step) % 2;
        float distance = float(step) * 0.37f - 20.0f;
        original ? Orig_SetCull(r.item, culled, distance) : WRender::SetCull(r.item, culled, distance);
    }
    for (int extra = 0; extra < kMaxDraws + 16; extra++) {
        bool culled = extra % 7 == 0;
        original ? Orig_SetCull(0x1000 + extra, culled, float(extra)) : WRender::SetCull(0x1000 + extra, culled, float(extra));
    }
    if (c->source != NULL)
        c->list = g_walkList;
}

void CheckWalk(const char *what, int index) {
    const WalkCase &a = g_walk[0], &b = g_walk[1];
    CheckU32(what, index, a.count, b.count);
    if (a.count == b.count)
        CheckBytes(what, index, a.records, b.records, a.count * sizeof(WalkRecord));
    if (a.source != NULL)
        CheckBytes(what, index, &a.list, &b.list, sizeof(a.list));
    CheckSnapshots(what, index);
}

// ---- CopyDrawPasses

struct CopyTarget {
    CachedDrawInfo list;
    WRenderSortEntry spill[WRender::kPassCount * kMaxDraws];   // passes can overlap: the copy can run past the list
};

struct CopyCase {
    const CachedDrawInfo *from;
    int firstPass, lastPass;
    CopyTarget to;
};

CopyCase g_copy[2];
CachedDrawInfo g_copyFrom;

void RunCopy(void *context, bool original) {
    CopyCase *c = static_cast<CopyCase *>(context);
    memset(&c->to, 0xcd, sizeof(c->to));
    if (original)
        Orig_CopyDrawPasses(fgRender, 0, c->from, &c->to.list, c->firstPass, c->lastPass);
    else
        fgRender->CopyDrawPasses(c->from, &c->to.list, c->firstPass, c->lastPass);
}

void TestCopies(const CachedDrawInfo *from, int index) {
    for (int lastPass = 0; lastPass < WRender::kPassCount; lastPass++) {
        for (int firstPass = 0; firstPass <= lastPass; firstPass++) {
            for (CopyCase &c : g_copy) {
                c.from = from;
                c.firstPass = firstPass;
                c.lastPass = lastPass;
            }
            g_cases++;
            if (!Guarded(RunCopy, &g_copy[0], true) || !Guarded(RunCopy, &g_copy[1], false))
                continue;
            CheckBytes("CopyDrawPasses", index * 100 + lastPass * 10 + firstPass, &g_copy[0].to, &g_copy[1].to,
                       sizeof(CopyTarget));
        }
    }
}

// Draws at, inside and just outside each pass's ends, and anywhere among the instances
void MakeEdgeDraws(CachedDrawInfo *list) {
    memset(list, 0, sizeof(*list));
    const WRender *render = fgRender;
    int count = 0;
    while (count < kMaxDraws - 1) {
        int pass = int(Next() % WRender::kPassCount);
        uintptr_t first = uintptr_t(render->passFirst[pass]);
        uintptr_t end = uintptr_t(render->passFirst[pass] + render->passCount[pass]);
        uintptr_t key;
        switch (Next() % 7) {
        case 0: key = first; break;
        case 1: key = first - 1; break;
        case 2: key = end; break;
        case 3: key = end - 1; break;
        case 4: key = end - sizeof(CARP::Instance); break;
        case 5: key = Next(); break;
        default: key = uintptr_t(render->instances + Next() % 4096); break;
        }
        list->draws[count].key = uint32_t(key);
        list->draws[count].distance = Uniform(-100.0f, 100.0f);
        count++;
        if (Next() % 600 == 0)
            break;
    }
    list->drawCount = count;
}

// ---- SimpleDrawVisibleTreeNodes

struct SimpleCase {
    Coord4 box;
};

void RunSimple(void *context, bool original) {
    SimpleCase *c = static_cast<SimpleCase *>(context);
    if (original)
        Orig_SimpleDrawVisibleTreeNodes(fgRender, 0, fgWorld->map->root, &c->box, true, 0);
    else
        fgRender->SimpleDrawVisibleTreeNodes(fgWorld->map->root, &c->box, true, 0);
}

// ---- the cases for one view

// Sets the frustum through the original (no-op for the live one), then runs every culling test from it
void TestView(int index, const Coord4 &eye, float yaw, float pitch, float viewRadius, bool live) {
    Put(&g_base);
    if (!live) {
        float frame[16] = {};
        frame[0] = 1.0f;
        frame[5] = 1.0f;
        frame[8] = sinf(yaw) * cosf(pitch);
        frame[9] = sinf(pitch);
        frame[10] = cosf(yaw) * cosf(pitch);
        frame[15] = 1.0f;
        RRenderWorldCulling_Setup2dFrustrum(WorldCulling, 0, &eye, frame, CullingFov, CullingFar);
    }

    static GenerateCase generate[2];
    memset(generate, 0, sizeof(generate));
    memset(&g_camera, 0, sizeof(g_camera));
    *MatrixRow(&g_camera.matrix, 3) = eye;
    if (!SideBySide(RunGenerate, &generate[0], &generate[1]))
        return;
    CheckU32("GenerateCurtainsAndNodes result", index, uint32_t(uintptr_t(generate[0].result)),
             uint32_t(uintptr_t(generate[1].result)));
    CheckSnapshots("GenerateCurtainsAndNodes", index);

    // the walk on the visible list, on its nodes reversed with repeats, on an empty list
    static CachedDrawInfo reversed, empty;
    memset(&reversed, 0, sizeof(reversed));
    for (int i = VisibleList.nodeCount - 1; i >= 0 && reversed.nodeCount < kMaxVisibleNodes; i--) {
        reversed.nodes[reversed.nodeCount++] = VisibleList.nodes[i];
        if (i % 3 == 0 && reversed.nodeCount < kMaxVisibleNodes)
            reversed.nodes[reversed.nodeCount++] = VisibleList.nodes[i];
    }
    memset(&empty, 0, sizeof(empty));
    const CachedDrawInfo *sources[3] = { NULL, &reversed, &empty };
    Snapshot *generated = &g_afterPort;
    static Snapshot afterGenerate;
    afterGenerate = *generated;
    for (int s = 0; s < 3; s++) {
        Put(&afterGenerate);
        g_walk[0].source = g_walk[1].source = sources[s];
        if (SideBySide(RunWalk, &g_walk[0], &g_walk[1]))
            CheckWalk("cull walk", index * 10 + s);
        if (s == 0) {
            g_copyFrom = VisibleList;
            TestCopies(&g_copyFrom, index * 10);
        }
    }
    MakeEdgeDraws(&g_copyFrom);
    TestCopies(&g_copyFrom, index * 10 + 1);

    // the simple walk from the same point
    Put(&g_base);
    ViewCull.radius = viewRadius;
    ViewCull.eye = eye;
    Coord4 facing = { sinf(yaw), 0.0f, cosf(yaw), 0.0f };
    ViewCull.facing = facing;
    CurrentList = &VisibleList;
    VisibleList.nodeCount = 0;
    static SimpleCase simple[2];
    simple[0].box = simple[1].box = RootBox();
    if (SideBySide(RunSimple, &simple[0], &simple[1]))
        CheckSnapshots("SimpleDrawVisibleTreeNodes", index);
}

void TestCulling() {
    const WMapHeader *map = fgWorld->map;
    Coord4 live = CullingEye;
    if (live.x == 0.0f && live.y == 0.0f && live.z == 0.0f) {
        Coord4 box = RootBox();
        live.x = box.x;
        live.y = box.y;
        live.z = box.z;
    }
    TestView(0, live, 0.0f, 0.0f, AtPointRadius, true);
    for (int i = 1; i <= 80; i++) {
        Coord4 eye;
        if (i % 2 == 0) {
            eye.x = live.x + Uniform(-60.0f, 60.0f);
            eye.y = live.y + Uniform(-5.0f, 20.0f);
            eye.z = live.z + Uniform(-60.0f, 60.0f);
        } else {
            eye.x = map->corner.x + Uniform(0.0f, map->size);
            eye.y = map->corner.y + Uniform(0.0f, 40.0f);
            eye.z = map->corner.z + Uniform(0.0f, map->size);
        }
        eye.w = 1.0f;
        float radius = i % 5 == 0 ? AtPointRadius : Uniform(0.0f, 120.0f);
        TestView(i, eye, Uniform(-3.2f, 3.2f), Uniform(-0.4f, 0.4f), radius, false);
    }
    Put(&g_base);
}

// ---- std::sort

const int kMaxSort = 1100;

struct SortCase {
    int op;
    RenderSortPredicate pred;
    WRenderSortEntry entries[kMaxSort + 2];     // one each side of the range, to see writes outside it
    int count;
    int a, b, c;                                // the op's indices
    WRenderSortEntry value;
    RenderSortRange range;
};

enum SortOp { kOpSort, kOpHeapSort, kOpInsertion, kOpRotate, kOpMed3, kOpMedian, kOpPartition, kOpPushHeap,
              kOpAdjustHeap, kOpCount };

SortCase g_sort[2];

void RunSort(void *context, bool original) {
    SortCase *c = static_cast<SortCase *>(context);
    WRenderSortEntry *first = &c->entries[1];
    WRenderSortEntry *last = first + c->count;
    switch (c->op) {
    case kOpSort:
        original ? Orig_Sort(first, last, c->a, c->pred) : RenderSort::Sort(first, last, c->a, c->pred);
        break;
    case kOpHeapSort:
        if (original) {
            Orig_MakeHeap(first, last, c->pred, 0, 0);
            Orig_SortHeap(first, last, c->pred);
        } else {
            RenderSort::MakeHeap(first, last, c->pred);
            RenderSort::SortHeap(first, last, c->pred);
        }
        break;
    case kOpInsertion:
        original ? Orig_InsertionSort(first, last, c->pred) : RenderSort::InsertionSort(first, last, c->pred);
        break;
    case kOpRotate:
        original ? Orig_Rotate(first, first + c->a, last, 0, 0) : RenderSort::Rotate(first, first + c->a, last);
        break;
    case kOpMed3:
        original ? Orig_Med3(first + c->a, first + c->b, first + c->c, c->pred)
                 : RenderSort::Med3(first + c->a, first + c->b, first + c->c, c->pred);
        break;
    case kOpMedian:
        original ? Orig_Median(first, first + c->count / 2, last - 1, c->pred)
                 : RenderSort::Median(first, first + c->count / 2, last - 1, c->pred);
        break;
    case kOpPartition:
        original ? Orig_UnguardedPartition(&c->range, first, last, c->pred)
                 : RenderSort::UnguardedPartition(&c->range, first, last, c->pred);
        c->range.first = reinterpret_cast<WRenderSortEntry *>(c->range.first - first);   // as indices
        c->range.last = reinterpret_cast<WRenderSortEntry *>(c->range.last - first);
        break;
    case kOpPushHeap:
        original ? Orig_PushHeap(first, c->a, c->b, c->value, c->pred)
                 : RenderSort::PushHeap(first, c->a, c->b, c->value, c->pred);
        break;
    case kOpAdjustHeap:
        original ? Orig_AdjustHeap(first, c->a, c->b, c->value, c->pred)
                 : RenderSort::AdjustHeap(first, c->a, c->b, c->value, c->pred);
        break;
    }
}

WRenderSortEntry RandomEntry(uint32_t keyRange) {
    WRenderSortEntry e;
    e.key = keyRange == 0 ? 7 : Next() % keyRange;
    e.distance = float(int(Next() % (keyRange == 0 ? 1 : keyRange))) * 0.5f - 40.0f;
    return e;
}

void TestSorts() {
    static const int kSizes[] = { 0, 1, 2, 3, 4, 5, 8, 16, 31, 32, 33, 34, 40, 41, 42, 63, 64, 100, 257, 600, 1023, 1100 };
    static const uint32_t kKeyRanges[] = { 0, 2, 5, 64, 1u << 24 };
    const int kCount = int(sizeof(kSizes) / sizeof(kSizes[0]));
    for (int round = 0; round < 6; round++) {
        for (int s = 0; s < kCount; s++) {
            for (int op = 0; op < kOpCount; op++) {
                int count = kSizes[s];
                if ((op == kOpMed3 || op == kOpMedian || op == kOpPartition || op == kOpAdjustHeap) && count < 1)
                    continue;
                if ((op == kOpPartition || op == kOpPushHeap) && count < 2)
                    continue;
                SortCase &c = g_sort[0];
                c.op = op;
                c.pred = (round + s) % 2 == 0 ? aLessThanB : someComparisonOperator;
                c.count = count;
                uint32_t keyRange = kKeyRanges[(round + op + s) % 5];
                for (int i = 0; i < count + 2; i++)
                    c.entries[i] = RandomEntry(keyRange);
                c.a = c.b = c.c = 0;
                if (op == kOpSort)
                    c.a = round == 0 ? 0 : round == 1 ? 1 : round == 2 ? 2 : count;
                if (op == kOpRotate)
                    c.a = count == 0 ? 0 : int(Next() % (count + 1));
                if (op == kOpMed3) {
                    c.a = int(Next() % count);
                    c.b = int(Next() % count);
                    c.c = int(Next() % count);
                }
                if (op == kOpPushHeap) {
                    c.a = int(Next() % count);          // hole
                    c.b = int(Next() % (c.a + 1));      // top
                }
                if (op == kOpAdjustHeap) {
                    c.b = 1 + int(Next() % count);      // bottom
                    c.a = int(Next() % c.b);            // hole
                }
                c.value = RandomEntry(keyRange);
                c.range.first = c.range.last = NULL;
                g_sort[1] = c;
                g_cases++;
                if (!Guarded(RunSort, &g_sort[0], true) || !Guarded(RunSort, &g_sort[1], false))
                    continue;
                int index = (round * kCount + s) * 10 + op;
                CheckBytes("sort", index, g_sort[0].entries, g_sort[1].entries, (count + 2) * sizeof(WRenderSortEntry));
                if (op == kOpPartition)
                    CheckBytes("partition", index, &g_sort[0].range, &g_sort[1].range, sizeof(RenderSortRange));
            }
        }
    }
}

}  // namespace

void RenderShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_RENDERSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestSorts();
    if (fgWorld != NULL && fgWorld->map != NULL && fgRender != NULL) {
        Take(&g_base);
        TestCulling();
        Put(&g_base);
    } else {
        printf("[render] no world or no WRender - the culling skipped\n");
    }
    ResetFpu();
    printf("[render] tree walks, cull walk, CopyDrawPasses, std::sort vs originals: %d cases, %d checks, %d differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[render]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
