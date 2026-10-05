#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "ColGridShadow.h"
#include "FpControl.h"

#include "../engine/UMemory.hpp"
#include "../world/Collider.h"
#include "../world/CollisionInstance.h"
#include "../world/Grid.h"
#include "../world/Tree.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_COLGRIDSHADOW=1, from the first simulation tick on the loaded track: COL_C's ports against the
// originals (the package's entry bytes swapped back in for each original call, common/xbeOriginal.h, so an
// original calls the other originals of the package and a port the other ports), on identical inputs, compared.
//
//   - The grid's searches over the real grid: RangeCheckROWCOL, FindNodes round a circle, FindNodesBox and
//     FindNodes along a segment, at random points, boxes and segments in and round the grid (long ones past the
//     step limit, short ones, ends outside, a few NaN), into empty and part-filled vectors: the cells, the vector's
//     size and capacity.
//   - The scene tree: FindNode (both), CheckContainment over the real tree at random circles; InitializeTree and
//     WalkTree on a synthetic tree (every byte of it compared) and WalkTree over the real one (the visit order).
//   - Every collision instance: MakeMatrix, CalcPosition, GetRenderInstance, GetName; the same on randomised copies;
//     the dynamic objects' MakeMatrix; triggers' Size and MakeMatrix on random records.
//   - The query helpers: barrier distance, window corners and records, EHitWindow data, the three point-in-triangle
//     tests (edges, degenerate triangles, NaN), DistanceCheck2d (the real view and random ones), the predicates.
//   - The vectors and lists: random reserve / push_back / insert sequences on the collider's and the grid's vectors
//     (inserting in the middle too), the copies, the cell lists' and dynamic elements' list operations, the cell
//     nodes' AddDynamic / RemoveDynamic, WGrid's constructor.
//   - Colliders at random places on the track (both constructors, every mask), then refreshes (small and large
//     moves), InRegion, GetWorldNormal, Validate with an instance's stamp changed, Clear and the destructor. The
//     collision manager's object and its instances (what its lists write) are snapshotted before the original call,
//     put back before the port's, and compared after both; the colliders compare by value (the strip lists they own
//     by content).
//   - The dynamic grid: AddGridNodeDynamicElement with random entries and moves, UpdateDynamicNodes with the
//     dynamic elements' last positions perturbed, and Restart. The grid cells' lists, the dynamic elements, the
//     instances and objects they write are put back to the same state before each run and compared after (the
//     lists by content), and restored at the end.
//
// One mutation this catches: FindNodesBox allowing a span of 21 cells instead of 20 changes the cells of the
// boxes wider than that (radii up to 15 cells here); CheckContainment rounding the x centre to a float, as it does
// the z one, changes its answer for circles on a node's edge (rarely - the containment cases count it).
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[3][2] = {
    { 0x000bd750, 0x000bedd0 }, { 0x000c5710, 0x000c7330 }, { 0x000cedd0, 0x000cf300 },
};

struct OriginalWindow {
    OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], true);
    }
    ~OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], false);
    }
};

// ---- the originals

typedef void (__fastcall *RangeCheckFn)(WGrid *, int, const Coord4 *, uint32_t *, uint32_t *);
typedef void (__fastcall *FindBoxFn)(WGrid *, int, const Coord4 *, WGridCellList *);
typedef void (__fastcall *FindCircleFn)(WGrid *, int, const Coord3 *, float, WGridCellList *);
typedef WMapNode *(*TreeFindFn)(WMapNode *, const Coord4 *, const Coord4 *);
typedef bool (*TreeContainsFn)(WMapNode *, const Coord4 *, const Coord4 *);
typedef WMapNode *(*TreeFindFromFn)(WMapNode *, WMapNode *, const Coord4 *, const Coord4 *);
typedef void (*TreeInitFn)(WMapNode *, int, int, int);
typedef void (*TreeWalkFn)(WMapNode *, MapNodeCallback);
typedef void (__fastcall *InstanceMatrixFn)(WCollisionInstance *, int, MATRIX4 *, int);
typedef void (__fastcall *InstancePositionFn)(WCollisionInstance *, int, Coord3 *);
typedef MATRIX4 *(__fastcall *InstanceRenderFn)(WCollisionInstance *, int);
typedef const char *(__fastcall *InstanceNameFn)(WCollisionInstance *, int);
typedef void (__fastcall *ObjectMatrixFn)(WCollisionObject *, int, MATRIX4 *, int);
typedef double (__fastcall *TriggerSizeFn)(WTrigger *, int);
typedef void (__fastcall *TriggerMatrixFn)(WTrigger *, int, MATRIX4 *, int);
typedef double (__fastcall *BarrierDistanceFn)(const CollisionBarrier *, int, const Coord4 *);
typedef void (__fastcall *CornersFn)(const WindowPane *, int, const Coord4 *, const Coord4 *, Coord3 *);
typedef const WindowSet *(__fastcall *WindowFindFn)(const WindowGroup *, int, uint32_t);
typedef WindowHit *(__fastcall *HitWindowFn)(WindowHit *, int, uint32_t, uint32_t, const Coord3 *, const Coord3 *);
typedef bool (*TriangleFn)(const Coord3 *, const StripVertex *);
typedef int (*DistanceCheckFn)(const Coord4 *, float);
typedef bool (*SortFn)(const WRenderSortEntry *, const WRenderSortEntry *);
typedef void (__fastcall *StampReserveFn)(ColliderStampList *, int, uint32_t);
typedef void (__fastcall *StampPushFn)(ColliderStampList *, int, const ColliderStamp *);
typedef void (__fastcall *StampInsertFn)(ColliderStampList *, int, ColliderStamp *, uint32_t, const ColliderStamp *);
typedef ColliderStamp *(__fastcall *StampUcopyFn)(ColliderStampList *, int, ColliderStamp *, ColliderStamp *, ColliderStamp *);
typedef void (__fastcall *InstanceReserveFn)(InstanceList *, int, uint32_t);
typedef void (__fastcall *BarrierReserveFn)(BarrierList *, int, uint32_t);
typedef void (__fastcall *BarrierTidyFn)(BarrierList *, int);
typedef BarrierListEntry *(__fastcall *BarrierUcopyFn)(BarrierList *, int, BarrierListEntry *, BarrierListEntry *, BarrierListEntry *);
typedef BarrierListEntry *(*BarrierCopyFn)(BarrierListEntry *, BarrierListEntry *, BarrierListEntry *);
typedef void (__fastcall *CellInsertFn)(WGridCellList *, int, uint32_t *, uint32_t, const uint32_t *);
typedef void (__fastcall *CellPushFn)(WGridCellList *, int, const uint32_t *);
typedef WGridDynamicNode *(__fastcall *DynamicHeadFn)(WGridDynamicList *, int);
typedef WGridDynamicNode *(__fastcall *DynamicBuyFn)(WGridDynamicList *, int, WGridDynamicNode *, WGridDynamicNode *, const WGridDynamicEntry *);
typedef void (__fastcall *DynamicSizeFn)(WGridDynamicList *, int, uint32_t);
typedef WGridDynamicNode **(__fastcall *DynamicEraseFn)(WGridDynamicList *, int, WGridDynamicNode **, WGridDynamicNode *, WGridDynamicNode *);
typedef void (__fastcall *DynamicDestructFn)(WGridDynamicList *, int);
typedef void (__fastcall *NodeDynamicFn)(WGridNode *, int, uint32_t, uint32_t);
typedef WGridMoverNode *(__fastcall *MoverHeadFn)(WGridMoverList *, int);
typedef WGridMoverNode *(__fastcall *MoverBuyFn)(WGridMoverList *, int, WGridMoverNode *, WGridMoverNode *, const WGridMover *);
typedef void (__fastcall *MoverSizeFn)(WGridMoverList *, int, uint32_t);
typedef WGrid *(__fastcall *GridConstructFn)(WGrid *, int, const Coord4 *, uint32_t, uint32_t, float);
typedef void (*AddElementFn)(Coord4 *, const Coord4 *, uint32_t, uint32_t);
typedef void (*StaticFn)(void);
typedef WCollider *(__fastcall *ColliderPointFn)(WCollider *, int, const Coord3 *, float, int, uint32_t);
typedef WCollider *(__fastcall *ColliderSweepFn)(WCollider *, int, const Coord4 *, uint32_t);
typedef void (__fastcall *ColliderVoidFn)(WCollider *, int);
typedef bool (__fastcall *ColliderBoolFn)(WCollider *, int);
typedef bool (__fastcall *ColliderInSweepFn)(WCollider *, int, const Coord4 *, uint32_t);
typedef bool (__fastcall *ColliderInPointFn)(WCollider *, int, const Coord3 *, float, uint32_t);
typedef bool (__fastcall *ColliderNormalFn)(WCollider *, int, const Coord4 *, WorldCollisionInfo *);
typedef void (__fastcall *ColliderRefreshSweepFn)(WCollider *, int, const Coord4 *);
typedef void (__fastcall *ColliderRefreshPointFn)(WCollider *, int, const Coord3 *, float);

#define Orig_RangeCheck ((RangeCheckFn)0x000c5710)
#define Orig_FindNodesBox ((FindBoxFn)0x000c62b0)
#define Orig_FindNodesCircle ((FindCircleFn)0x000c6450)
#define Orig_FindNodesSegment ((FindBoxFn)0x000c64b0)

// ---- the game's state the tests read and restore

struct ShadowCollisionArrays {
    uint32_t unknown00;
    uint32_t unknown04;
    WCollisionInstance *instances;
    uint32_t instanceCount;
    WCollisionObject *objects;
};
#define CollisionManagerObject (*(uint8_t **)0x00239a70)
#define CollisionArrays (*(ShadowCollisionArrays **)0x00239a70)
const uint32_t kCollisionManagerBytes = 0x34;
#define ViewCullBytes ((uint8_t *)0x0023dfd0)

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
int g_nanOnly = 0;   // both NaN, a different sign or payload (docs/driving/maths.md 3, 9.3)
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[colgrid]   %s #%d: %s\n", what, index, detail);
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

// Floats computed by the original's x87 arithmetic and the port's SSE: equal bits, or both NaN. Where two NaNs
// meet in one operation the x87 and SSE pick different ones (the larger significand / the first operand), so the
// sign or payload of a NaN result can differ; those are counted apart, as no real data has NaN inputs.
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

void CheckDouble(const char *what, int index, double a, double b) {
    g_checks++;
    if (memcmp(&a, &b, sizeof(a)) == 0)
        return;
    if (a != a && b != b) {
        g_nanOnly++;
        return;
    }
    char detail[96];
    snprintf(detail, sizeof(detail), "original %.17g, port %.17g", a, b);
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
#ifdef _MSC_VER
    __try {
        run(context, original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        return false;
    }
#else
    run(context, original);
    return true;
#endif
}

// The original (inside the window), then the port; false if either faulted
template <class F>
bool Both(F &&run) {
    typedef typename std::remove_reference<F>::type Run;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Run *>(context))(original); };
    bool ok;
    {
        OriginalWindow window;
        ok = Guarded(thunk, &run, true);
    }
    return Guarded(thunk, &run, false) && ok;
}

// ---- inputs

uint32_t g_random = 0x2545f491;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }

float Special() {
    switch (RandomInt(6)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return NAN;
    case 3: return INFINITY;
    case 4: return 1e-30f;
    default: return -1e30f;
    }
}

// Mostly in [lo, hi], now and then a special value
float Coordinate(float lo, float hi) { return RandomInt(150) == 0 ? Special() : Uniform(lo, hi); }

// ---- vectors

template <class T>
void FreeVector(GameVector<T> *vector) {
    ColStl::Tidy(vector);
}

template <class T>
void CompareVectors(const char *what, int index, const ColVector<T> &a, const ColVector<T> &b) {
    CheckU32(what, index, a.Size(), b.Size());
    CheckU32(what, index, a.Capacity(), b.Capacity());
    if (a.Size() == b.Size() && a.Size() != 0)
        CheckBytes(what, index, a.first, b.first, a.Size() * sizeof(T));
}

// ---------------------------------------------------------------------------------------------------------------
// The grid's searches

void Prefill(WGridCellList *a, WGridCellList *b, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t value = Random();
        a->PushBack(&value);
        b->PushBack(&value);
    }
}

void TestGridQueries() {
    WGrid *grid = TheGrid;
    float spanX = float(grid->columns) * grid->cellSize, spanZ = float(grid->rows) * grid->cellSize;
    float x0 = grid->origin.x - 0.2f * spanX, x1 = grid->origin.x + 1.2f * spanX;
    float z0 = grid->origin.z - 0.2f * spanZ, z1 = grid->origin.z + 1.2f * spanZ;
    for (int i = 0; i < 3000; i++) {
        Coord4 point = { Coordinate(x0, x1), Uniform(-50.0f, 50.0f), Coordinate(z0, z1), 0.0f };
        uint32_t row[2] = { 0, 0 }, column[2] = { 0, 0 };
        Both([&](bool original) {
            if (original)
                Orig_RangeCheck(grid, 0, &point, &row[0], &column[0]);
            else
                grid->RangeCheckROWCOL(&point, &row[1], &column[1]);
        });
        CheckU32("RangeCheckROWCOL row", i, row[0], row[1]);
        CheckU32("RangeCheckROWCOL column", i, column[0], column[1]);

        float radius = RandomInt(50) == 0 ? Special() : Uniform(0.0f, 15.0f * grid->cellSize);
        WGridCellList a = {}, b = {};
        Prefill(&a, &b, RandomInt(4) == 0 ? RandomInt(40) : 0);
        Both([&](bool original) {
            if (original)
                Orig_FindNodesCircle(grid, 0, reinterpret_cast<const Coord3 *>(&point), radius, &a);
            else
                grid->FindNodes(reinterpret_cast<const Coord3 *>(&point), radius, &b);
        });
        CompareVectors("FindNodes (circle)", i, a, b);
        FreeVector(&a);
        FreeVector(&b);

        Coord4 box[2] = {};
        box[0] = { Coordinate(x0, x1), 0.0f, Coordinate(z0, z1), 0.0f };
        box[1] = { box[0].x + Uniform(-30.0f, 30.0f) * grid->cellSize, 0.0f,
                   box[0].z + Uniform(-30.0f, 30.0f) * grid->cellSize, 0.0f };
        Prefill(&a, &b, RandomInt(4) == 0 ? RandomInt(40) : 0);
        Both([&](bool original) {
            if (original)
                Orig_FindNodesBox(grid, 0, box, &a);
            else
                grid->FindNodesBox(box, &b);
        });
        CompareVectors("FindNodesBox", i, a, b);
        FreeVector(&a);
        FreeVector(&b);

        Coord4 segment[2] = {};
        segment[0] = { Coordinate(x0, x1), Uniform(-20.0f, 20.0f), Coordinate(z0, z1), 1.0f };
        float reach = RandomInt(3) == 0 ? grid->cellSize * 2 : RandomInt(2) == 0 ? spanX : 0.0f;
        if (reach == 0.0f)
            segment[1] = { Coordinate(x0, x1), Uniform(-20.0f, 20.0f), Coordinate(z0, z1), 1.0f };
        else
            segment[1] = { segment[0].x + Uniform(-reach, reach), segment[0].y,
                           segment[0].z + Uniform(-reach, reach), 1.0f };
        if (RandomInt(20) == 0)
            segment[1].x = segment[0].x;   // straight along z
        Prefill(&a, &b, RandomInt(4) == 0 ? RandomInt(40) : 0);
        Both([&](bool original) {
            if (original)
                Orig_FindNodesSegment(grid, 0, segment, &a);
            else
                grid->FindNodes(segment, &b);
        });
        CompareVectors("FindNodes (segment)", i, a, b);
        FreeVector(&a);
        FreeVector(&b);
        g_cases += 4;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The scene tree

const int kWalkMax = 200000;
WMapNode **g_walk = NULL;
int g_walkCount = 0;

void RecordNode(WMapNode *node) {
    if (g_walkCount < kWalkMax)
        g_walk[g_walkCount] = node;
    g_walkCount++;
}

void CollectNodes(WMapNode *node, std::vector<WMapNode *> *nodes) {
    if (nodes->size() > 100000)
        return;
    nodes->push_back(node);
    if (node->children != NULL)
        for (int i = 0; i < 4; i++)
            CollectNodes(&node->children[i], nodes);
}

void CompareWalks(const char *what, int index, WMapNode *root) {
    static WMapNode **logs[2];
    int counts[2] = { 0, 0 };
    for (int side = 0; side < 2; side++)
        if (logs[side] == NULL)
            logs[side] = static_cast<WMapNode **>(malloc(kWalkMax * sizeof(WMapNode *)));
    Both([&](bool original) {
        int side = original ? 0 : 1;
        g_walkCount = 0;
        if (original)
            ((TreeWalkFn)0x000cf1a0)(root, RecordNode);
        else
            WTree::WalkTree(root, RecordNode);
        counts[side] = g_walkCount < kWalkMax ? g_walkCount : kWalkMax;
        memcpy(logs[side], g_walk, counts[side] * sizeof(WMapNode *));
    });
    CheckU32(what, index, uint32_t(counts[0]), uint32_t(counts[1]));
    if (counts[0] == counts[1] && counts[0] != 0)
        CheckBytes(what, index, logs[0], logs[1], counts[0] * sizeof(WMapNode *));
}

void TestTree() {
    WMapHeader *map = fgWorld->map;
    if (map == NULL || map->root == NULL) {
        printf("[colgrid] no scene tree - its tests skipped\n");
        return;
    }
    g_walk = static_cast<WMapNode **>(malloc(kWalkMax * sizeof(WMapNode *)));
    float half = map->size * 0.5f;
    Coord4 box = { half + map->corner.x, map->corner.y, half + map->corner.z, half };
    std::vector<WMapNode *> nodes;
    CollectNodes(map->root, &nodes);
    for (int i = 0; i < 3000; i++) {
        Coord4 circle = { Coordinate(map->corner.x - 0.1f * map->size, map->corner.x + 1.1f * map->size), 0.0f,
                         Coordinate(map->corner.z - 0.1f * map->size, map->corner.z + 1.1f * map->size), 0.0f };
        int kind = RandomInt(10);
        circle.w = kind == 0 ? Special() : kind < 5 ? Uniform(0.0f, map->size / 64) : Uniform(0.0f, map->size / 2);
        WMapNode *found[2] = { NULL, NULL };
        Both([&](bool original) {
            if (original)
                found[0] = ((TreeFindFn)0x000cedd0)(map->root, &box, &circle);
            else
                found[1] = WTree::FindNode(map->root, &box, &circle);
        });
        CheckU32("WTree::FindNode", i, uint32_t(uintptr_t(found[0])), uint32_t(uintptr_t(found[1])));

        WMapNode *node = nodes[RandomInt(int(nodes.size()))];
        bool inside[2] = { false, false };
        Both([&](bool original) {
            if (original)
                inside[0] = ((TreeContainsFn)0x000cefe0)(node, &box, &circle);
            else
                inside[1] = WTree::CheckContainment(node, &box, &circle);
        });
        CheckU32("WTree::CheckContainment", i, inside[0], inside[1]);

        Both([&](bool original) {
            if (original)
                found[0] = ((TreeFindFromFn)0x000cf0b0)(map->root, node, &box, &circle);
            else
                found[1] = WTree::FindNode(map->root, node, &box, &circle);
        });
        CheckU32("WTree::FindNode (from a node)", i, uint32_t(uintptr_t(found[0])), uint32_t(uintptr_t(found[1])));
        g_cases += 3;
    }
    CompareWalks("WTree::WalkTree (the track's tree)", 0, map->root);
    g_cases++;

    // A synthetic tree, four levels below the root
    const int kNodes = 1 + 4 + 16 + 64 + 256;
    static WMapNode tree[kNodes];
    static WMapNode saved[kNodes], result[kNodes];
    for (int i = 0; i < 100; i++) {
        for (int n = 0; n < kNodes; n++) {
            uint32_t *words = reinterpret_cast<uint32_t *>(&tree[n]);
            for (int w = 0; w < 4; w++)
                words[w] = Random();
            tree[n].children = 4 * n + 1 < kNodes && RandomInt(8) != 0 ? &tree[4 * n + 1] : NULL;
        }
        memcpy(saved, tree, sizeof(tree));
        int depth = RandomInt(3) == 0 ? int(Random()) : RandomInt(8);
        int cellX = RandomInt(2) == 0 ? int(Random()) : RandomInt(16), cellZ = RandomInt(2) == 0 ? int(Random()) : 0;
        Both([&](bool original) {
            memcpy(tree, saved, sizeof(tree));
            if (original) {
                ((TreeInitFn)0x000cf0f0)(tree, depth, cellX, cellZ);
                memcpy(result, tree, sizeof(tree));
            } else {
                WTree::InitializeTree(tree, depth, uint8_t(cellX), uint8_t(cellZ));
            }
        });
        CheckBytes("WTree::InitializeTree", i, result, tree, sizeof(tree));
        CompareWalks("WTree::WalkTree (synthetic)", i, tree);
        g_cases += 2;
    }
    free(g_walk);
    g_walk = NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// Instances, objects, triggers

void CompareInstance(const char *what, int index, WCollisionInstance *instance) {
    for (int translate = 0; translate < 2; translate++) {
        MATRIX4 m[2];
        memset(m, 0xcd, sizeof(m));
        Both([&](bool original) {
            if (original)
                ((InstanceMatrixFn)0x000be8b0)(instance, 0, &m[0], translate);
            else
                instance->MakeMatrix(&m[1], translate != 0);
        });
        CheckBytes(what, index, &m[0], &m[1], sizeof(MATRIX4));
    }
    Coord3 local[2];
    memset(local, 0xcd, sizeof(local));
    Both([&](bool original) {
        if (original)
            ((InstancePositionFn)0x000be810)(instance, 0, &local[0]);
        else
            instance->CalcPosition(&local[1]);
    });
    CheckFloats("WCollisionInstance::CalcPosition", index, &local[0].x, &local[1].x, 3);
}

void TestInstances() {
    ShadowCollisionArrays *arrays = CollisionArrays;
    uint32_t count = arrays->instanceCount;
    if (count > 0x10000) {
        printf("[colgrid] %u collision instances? - instance tests skipped\n", count);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        WCollisionInstance *instance = &arrays->instances[i];
        CompareInstance("WCollisionInstance::MakeMatrix", int(i), instance);
        MATRIX4 *render[2] = { NULL, NULL };
        const char *name[2] = { NULL, NULL };
        Both([&](bool original) {
            if (original) {
                render[0] = ((InstanceRenderFn)0x000be780)(instance, 0);
                name[0] = ((InstanceNameFn)0x000be7a0)(instance, 0);
            } else {
                render[1] = instance->GetRenderInstance();
                name[1] = instance->GetName();
            }
        });
        CheckU32("WCollisionInstance::GetRenderInstance", int(i), uint32_t(uintptr_t(render[0])),
                 uint32_t(uintptr_t(render[1])));
        CheckU32("WCollisionInstance::GetName", int(i), uint32_t(uintptr_t(name[0])), uint32_t(uintptr_t(name[1])));
        g_cases += 3;
    }
    if (count != 0) {
        for (int i = 0; i < 1000; i++) {
            alignas(16) WCollisionInstance copy = arrays->instances[RandomInt(int(count))];
            copy.right = { Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f) };
            copy.forward = { Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f) };
            copy.position = { Coordinate(-3000.0f, 3000.0f), Coordinate(-100.0f, 100.0f), Coordinate(-3000.0f, 3000.0f) };
            copy.flags = uint8_t(Random());
            CompareInstance("WCollisionInstance::MakeMatrix (random)", i, &copy);
            g_cases += 2;
        }
    }

    // The dynamic objects
    int index = 0;
    WGridMoverNode *head = GridMovers.head;
    for (WGridMoverNode *node = head == NULL ? NULL : head->next; node != NULL && node != head; node = node->next) {
        WCollisionObject *object = node->value.object;
        if (object == NULL)
            continue;
        for (int translate = 0; translate < 2; translate++) {
            MATRIX4 m[2];
            memset(m, 0xcd, sizeof(m));
            Both([&](bool original) {
                if (original)
                    ((ObjectMatrixFn)0x000be6f0)(object, 0, &m[0], translate);
                else
                    object->MakeMatrix(&m[1], translate != 0);
            });
            CheckBytes("WCollisionObject::MakeMatrix", index, &m[0], &m[1], sizeof(MATRIX4));
        }
        index++;
        g_cases++;
    }

    // Triggers
    for (int i = 0; i < 1000; i++) {
        alignas(16) WTrigger trigger;
        uint32_t *words = reinterpret_cast<uint32_t *>(&trigger);
        for (int w = 0; w < 16; w++)
            words[w] = Random();
        trigger.position = { Coordinate(-3000.0f, 3000.0f), Coordinate(-100.0f, 100.0f), Coordinate(-3000.0f, 3000.0f) };
        trigger.right = { Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f) };
        trigger.forward = { Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f) };
        double size[2] = { 0, 0 };
        MATRIX4 m[2];
        memset(m, 0xcd, sizeof(m));
        int translate = RandomInt(2);
        Both([&](bool original) {
            if (original) {
                size[0] = ((TriggerSizeFn)0x000cf1f0)(&trigger, 0);
                ((TriggerMatrixFn)0x000cf260)(&trigger, 0, &m[0], translate);
            } else {
                size[1] = trigger.Size();
                trigger.MakeMatrix(&m[1], translate != 0);
            }
        });
        CheckDouble("WTrigger::Size", i, size[0], size[1]);
        CheckBytes("WTrigger::MakeMatrix", i, &m[0], &m[1], sizeof(MATRIX4));
        g_cases += 2;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The queries' helpers

void TestHelpers() {
    for (int i = 0; i < 4000; i++) {
        CollisionBarrier barrier = {};   // the tag zero
        barrier.x0 = Coordinate(-100.0f, 100.0f);
        barrier.y0 = Uniform(-5.0f, 5.0f);
        barrier.z0 = Coordinate(-100.0f, 100.0f);
        barrier.x1 = RandomInt(20) == 0 ? barrier.x0 : Coordinate(-100.0f, 100.0f);
        barrier.y1 = 0.0f;
        barrier.z1 = Coordinate(-100.0f, 100.0f);
        float dx = barrier.x1 - barrier.x0, dz = barrier.z1 - barrier.z0;
        barrier.inverseLength = RandomInt(10) == 0 ? Coordinate(0.0f, 2.0f) : 1.0f / sqrtf(dx * dx + dz * dz);
        Coord4 point = { Coordinate(-120.0f, 120.0f), 0.0f, Coordinate(-120.0f, 120.0f), 0.0f };
        if (RandomInt(10) == 0)
            point = { barrier.x0, 0.0f, barrier.z0, 0.0f };
        double distance[2] = { 0, 0 };
        Both([&](bool original) {
            if (original)
                distance[0] = ((BarrierDistanceFn)0x000be950)(&barrier, 0, &point);
            else
                distance[1] = barrier.DistanceSquared2D(&point);
        });
        CheckDouble("CollisionBarrier::DistanceSquared2D", i, distance[0], distance[1]);
        g_cases++;
    }

    for (int i = 0; i < 1000; i++) {
        WindowPane outline = { int16_t(Random()), int16_t(Random()), int16_t(Random()), int16_t(Random()) };
        Coord4 origin = { Coordinate(-3000.0f, 3000.0f), Coordinate(-100.0f, 100.0f), Coordinate(-3000.0f, 3000.0f), 0.0f };
        Coord4 along = { Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f), Coordinate(-1.0f, 1.0f), 0.0f };
        Coord3 corners[2][4];
        memset(corners, 0xcd, sizeof(corners));
        Both([&](bool original) {
            if (original)
                ((CornersFn)0x000bea50)(&outline, 0, &origin, &along, corners[0]);
            else
                outline.MakeCorners(&origin, &along, corners[1]);
        });
        CheckFloats("WindowPane::MakeCorners", i, &corners[0][0].x, &corners[1][0].x, 12);

        uint32_t point[3] = { Random(), Random(), Random() }, direction[3] = { Random(), Random(), Random() };
        WindowHit data[2];
        memset(data, 0xcd, sizeof(data));
        uint32_t window = Random(), hitter = Random();
        WindowHit *answer[2] = { NULL, NULL };
        Both([&](bool original) {
            if (original)
                answer[0] = ((HitWindowFn)0x000beb70)(&data[0], 0, window, hitter, (const Coord3 *)point,
                                                      (const Coord3 *)direction);
            else
                answer[1] = data[1].Construct(window, hitter, (const Coord3 *)point, (const Coord3 *)direction);
        });
        CheckBytes("WindowHit::Construct", i, &data[0], &data[1], sizeof(WindowHit));
        CheckU32("WindowHit::Construct's answer", i, uint32_t(answer[0] == &data[0]), uint32_t(answer[1] == &data[1]));
        g_cases += 2;
    }

    // Window record tables
    static uint32_t table[512];
    for (int i = 0; i < 200; i++) {
        uint32_t count = uint32_t(RandomInt(12));
        table[0] = count;
        table[1] = Random();
        table[2] = Random();
        uint32_t at = 3;
        for (uint32_t r = 0; r < count; r++) {
            uint32_t length = uint32_t(RandomInt(4));
            table[at] = length;
            table[at + 1] = uint32_t(RandomInt(8));
            for (uint32_t w = 0; w < 2 * length; w++)
                table[at + 2 + w] = Random();
            at += 2 + 2 * length;
        }
        for (uint32_t key = 0; key < 10; key++) {
            const WindowSet *found[2] = { NULL, NULL };
            const WindowGroup *windows = reinterpret_cast<const WindowGroup *>(table);
            Both([&](bool original) {
                if (original)
                    found[0] = ((WindowFindFn)0x000beb40)(windows, 0, key);
                else
                    found[1] = windows->FindSet(key);
            });
            CheckU32("WindowGroup::FindSet", i, uint32_t(uintptr_t(found[0])), uint32_t(uintptr_t(found[1])));
            g_cases++;
        }
    }

    // Point in triangle
    for (int i = 0; i < 6000; i++) {
        StripVertex triangle[3];
        for (int c = 0; c < 3; c++)
            triangle[c] = { Coordinate(-10.0f, 10.0f), 0.0f, Coordinate(-10.0f, 10.0f), 0 };
        if (RandomInt(8) == 0)
            triangle[2] = triangle[RandomInt(2)];                     // degenerate
        if (RandomInt(8) == 0)
            triangle[1].x = triangle[0].x;
        Coord3 point = { Coordinate(-12.0f, 12.0f), 0.0f, Coordinate(-12.0f, 12.0f) };
        int on = RandomInt(6);
        if (on < 3)
            point = { triangle[on].x, 0.0f, triangle[on].z };        // on a corner
        else if (on == 3)
            point = { (triangle[0].x + triangle[1].x) * 0.5f, 0.0f, (triangle[0].z + triangle[1].z) * 0.5f };
        bool inside[3][2] = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            if (original) {
                inside[0][side] = ((TriangleFn)0x000bebd0)(&point, triangle);
                inside[1][side] = ((TriangleFn)0x000becb0)(&point, triangle);
                inside[2][side] = ((TriangleFn)0x000bed40)(&point, triangle);
            } else {
                inside[0][side] = PointInTriangle2D(&point, triangle);
                inside[1][side] = PointInTriangle2DClockwise(&point, triangle);
                inside[2][side] = PointInTriangle2DCounter(&point, triangle);
            }
        });
        CheckU32("PointInTriangle2D", i, inside[0][0], inside[0][1]);
        CheckU32("PointInTriangle2DClockwise", i, inside[1][0], inside[1][1]);
        CheckU32("PointInTriangle2DCounter", i, inside[2][0], inside[2][1]);
        g_cases += 3;
    }

    // DistanceCheck2d: the view as it is, then random views
    uint8_t view[0x30];
    memcpy(view, ViewCullBytes, sizeof(view));
    for (int i = 0; i < 2000; i++) {
        if (i >= 1000) {
            WViewCull *cull = reinterpret_cast<WViewCull *>(ViewCullBytes);
            cull->radius = Uniform(0.0f, 300.0f);
            cull->eye = { Uniform(-3000.0f, 3000.0f), Uniform(-50.0f, 50.0f), Uniform(-3000.0f, 3000.0f), 1.0f };
            float angle = Uniform(0.0f, 6.2831853f);
            cull->facing = { cosf(angle), 0.0f, sinf(angle), 0.0f };
        }
        const WViewCull *cull = reinterpret_cast<const WViewCull *>(ViewCullBytes);
        Coord4 position = { cull->eye.x + Coordinate(-600.0f, 600.0f), Uniform(-50.0f, 50.0f),
                           cull->eye.z + Coordinate(-600.0f, 600.0f), 1.0f };
        float radius = RandomInt(30) == 0 ? Special() : Uniform(0.0f, 200.0f);
        int where[2] = { -1, -1 };
        Both([&](bool original) {
            if (original)
                where[0] = ((DistanceCheckFn)0x000c7270)(&position, radius);
            else
                where[1] = DistanceCheck2d(&position, radius);
        });
        CheckU32("DistanceCheck2d", i, uint32_t(where[0]), uint32_t(where[1]));
        g_cases++;
    }
    memcpy(ViewCullBytes, view, sizeof(view));

    for (int i = 0; i < 2000; i++) {
        WRenderSortEntry a = { Random(), Coordinate(-100.0f, 100.0f) }, b = { Random(), Coordinate(-100.0f, 100.0f) };
        if (RandomInt(5) == 0) {
            b.key = a.key;
            b.distance = a.distance;
        }
        bool less[2][2] = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            if (original) {
                less[0][side] = ((SortFn)0x000c7230)(&a, &b);
                less[1][side] = ((SortFn)0x000c7250)(&a, &b);
            } else {
                less[0][side] = aLessThanB(&a, &b);
                less[1][side] = someComparisonOperator(&a, &b);
            }
        });
        CheckU32("aLessThanB", i, less[0][0], less[0][1]);
        CheckU32("someComparisonOperator", i, less[1][0], less[1][1]);
        g_cases += 2;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Vectors and lists

ColliderStamp RandomStamp() {
    ColliderStamp stamp = { reinterpret_cast<CollisionArticle *>(uintptr_t(Random())),
                            reinterpret_cast<CollisionArticle *const *>(uintptr_t(Random())) };
    return stamp;
}

void TestStampVectors() {
    for (int i = 0; i < 300; i++) {
        ColliderStampList list[2] = {};
        for (int op = 0, ops = 1 + RandomInt(20); op < ops; op++) {
            int kind = RandomInt(3);
            uint32_t count = uint32_t(RandomInt(kind == 0 ? 64 : 10));
            uint32_t where = uint32_t(RandomInt(int(list[0].Size()) + 1));
            ColliderStamp value = RandomStamp();
            Both([&](bool original) {
                ColliderStampList *v = &list[original ? 0 : 1];
                ColliderStamp *at = v->first == NULL ? NULL : v->first + where;
                if (kind == 0) {
                    if (original)
                        ((StampReserveFn)0x000be070)(v, 0, count);
                    else
                        v->Reserve(count);
                } else if (kind == 1) {
                    if (original)
                        ((StampPushFn)0x000be260)(v, 0, &value);
                    else
                        v->PushBack(&value);
                } else {
                    if (v->first == NULL)
                        at = v->last;
                    if (original)
                        ((StampInsertFn)0x000bda70)(v, 0, at, count, &value);
                    else
                        v->InsertN(at, count, &value);
                }
            });
            CompareVectors("ColliderStampList", i * 32 + op, list[0], list[1]);
            g_cases++;
        }
        FreeVector(&list[0]);
        FreeVector(&list[1]);
    }

    // The copies
    for (int i = 0; i < 200; i++) {
        ColliderStamp from[16], to[2][20];
        BarrierListEntry barriers[8], barrierTo[2][10];
        for (int n = 0; n < 16; n++)
            from[n] = RandomStamp();
        for (int n = 0; n < 8; n++)
            for (int w = 0; w < 10; w++)
                reinterpret_cast<uint32_t *>(&barriers[n])[w] = Random();
        memset(to, 0xcd, sizeof(to));
        memset(barrierTo, 0xcd, sizeof(barrierTo));
        int n = RandomInt(17), m = RandomInt(9);
        ColliderStamp *ends[2] = {};
        BarrierListEntry *barrierEnds[2][2] = {};
        ColliderStampList stamps = {};
        BarrierList barrierList = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            if (original) {
                ends[side] = ((StampUcopyFn)0x000bd940)(&stamps, 0, from, from + n, to[side]);
                barrierEnds[side][0] = ((BarrierUcopyFn)0x000bd970)(&barrierList, 0, barriers, barriers + m, barrierTo[side]);
                barrierEnds[side][1] = ((BarrierCopyFn)0x000bd910)(barriers, barriers + m, barrierTo[side] + 1);
            } else {
                ends[side] = stamps.Ucopy(from, from + n, to[side]);
                barrierEnds[side][0] = barrierList.Ucopy(barriers, barriers + m, barrierTo[side]);
                barrierEnds[side][1] = BarrierUninitializedCopy(barriers, barriers + m, barrierTo[side] + 1);
            }
        });
        CheckBytes("ColliderStampList::Ucopy", i, to[0], to[1], sizeof(to[0]));
        CheckU32("ColliderStampList::Ucopy's end", i, uint32_t(ends[0] - to[0]), uint32_t(ends[1] - to[1]));
        CheckBytes("barrier copies", i, barrierTo[0], barrierTo[1], sizeof(barrierTo[0]));
        CheckU32("barrier copies' ends", i, uint32_t(barrierEnds[0][0] - barrierTo[0]),
                 uint32_t(barrierEnds[1][0] - barrierTo[1]));
        CheckU32("barrier copies' ends", i, uint32_t(barrierEnds[0][1] - barrierTo[0]),
                 uint32_t(barrierEnds[1][1] - barrierTo[1]));
        g_cases += 3;
    }

    // Reserve on the instance and barrier lists, with contents; the barrier list's Tidy
    for (int i = 0; i < 200; i++) {
        InstanceList instances[2] = {};
        BarrierList barriers[2] = {};
        int filled = RandomInt(30);
        instances[0].Reserve(uint32_t(filled));
        instances[1].Reserve(uint32_t(filled));
        barriers[0].Reserve(uint32_t(filled));
        barriers[1].Reserve(uint32_t(filled));
        for (int n = 0; n < filled; n++) {
            InstanceListEntry entry = { reinterpret_cast<WCollisionInstance *>(uintptr_t(Random())), NULL };
            instances[0].first[n] = entry;
            instances[1].first[n] = entry;
            BarrierListEntry barrier;
            for (int w = 0; w < 10; w++)
                reinterpret_cast<uint32_t *>(&barrier)[w] = Random();
            barriers[0].first[n] = barrier;
            barriers[1].first[n] = barrier;
        }
        if (filled != 0) {
            instances[0].last = instances[0].first + filled;
            instances[1].last = instances[1].first + filled;
            barriers[0].last = barriers[0].first + filled;
            barriers[1].last = barriers[1].first + filled;
        }
        uint32_t count = uint32_t(RandomInt(64));
        bool tidy = RandomInt(4) == 0;
        Both([&](bool original) {
            int side = original ? 0 : 1;
            if (original) {
                ((InstanceReserveFn)0x000bdf60)(&instances[side], 0, count);
                ((BarrierReserveFn)0x000bde10)(&barriers[side], 0, count);
                if (tidy)
                    ((BarrierTidyFn)0x000bd9a0)(&barriers[side], 0);
            } else {
                instances[side].Reserve(count);
                barriers[side].Reserve(count);
                if (tidy)
                    barriers[side].Tidy();
            }
        });
        CompareVectors("InstanceList::Reserve", i, instances[0], instances[1]);
        CompareVectors("BarrierList::Reserve", i, barriers[0], barriers[1]);
        FreeVector(&instances[0]);
        FreeVector(&instances[1]);
        FreeVector(&barriers[0]);
        FreeVector(&barriers[1]);
        g_cases += 2;
    }

    // The cell vector
    for (int i = 0; i < 300; i++) {
        WGridCellList list[2] = {};
        for (int op = 0, ops = 1 + RandomInt(30); op < ops; op++) {
            bool push = RandomInt(3) != 0;
            uint32_t count = uint32_t(RandomInt(10));
            uint32_t where = uint32_t(RandomInt(int(list[0].Size()) + 1));
            uint32_t value = Random();
            Both([&](bool original) {
                WGridCellList *v = &list[original ? 0 : 1];
                uint32_t *at = v->first == NULL ? v->last : v->first + where;
                if (push) {
                    if (original)
                        ((CellPushFn)0x000c5ea0)(v, 0, &value);
                    else
                        v->PushBack(&value);
                } else {
                    if (original)
                        ((CellInsertFn)0x000c5be0)(v, 0, at, count, &value);
                    else
                        v->InsertN(at, count, &value);
                }
            });
            CompareVectors("WGridCellList", i * 32 + op, list[0], list[1]);
            g_cases++;
        }
        FreeVector(&list[0]);
        FreeVector(&list[1]);
    }
}

// A cell list as entries (index | type << 16, the bytes the original leaves aside), its size, and whether its
// links hold
struct ListState {
    bool exists;
    uint32_t size;
    bool linked;
    std::vector<uint64_t> entries;
    bool operator==(const ListState &other) const {
        return exists == other.exists && size == other.size && linked == other.linked && entries == other.entries;
    }
};

ListState ReadList(const WGridDynamicList *list) {
    ListState state = { list != NULL, 0, true, {} };
    if (list == NULL)
        return state;
    state.size = list->size;
    const WGridDynamicNode *head = list->head;
    if (head == NULL) {
        state.linked = false;
        return state;
    }
    for (const WGridDynamicNode *node = head->next; node != head && state.entries.size() < 100000; node = node->next) {
        state.entries.push_back(uint64_t(node->value.index) | uint64_t(node->value.type) << 16);
        if (node->next->prev != node)
            state.linked = false;
    }
    if (head->next->prev != head)
        state.linked = false;
    return state;
}

void AppendEntry(WGridDynamicList *list, uint64_t value) {
    WGridDynamicEntry entry = { uint16_t(value), 0, uint32_t(value >> 16) };
    WGridDynamicNode *head = list->head;
    WGridDynamicNode *node = list->BuyNode(head, head->prev, &entry);
    list->IncreaseSize(1);
    head->prev = node;
    node->prev->next = node;
}

void TestLists() {
    // A cell list's operations
    for (int i = 0; i < 300; i++) {
        WGridDynamicList list[2] = {};
        Both([&](bool original) {
            if (original)
                list[0].head = ((DynamicHeadFn)0x000c5970)(&list[0], 0);
            else
                list[1].head = list[1].BuyHead();
        });
        CheckU32("WGridDynamicList::BuyHead", i, ReadList(&list[0]) == ReadList(&list[1]), 1);
        for (int op = 0, ops = 1 + RandomInt(30); op < ops; op++) {
            bool erase = RandomInt(4) == 0;
            WGridDynamicEntry entry = { uint16_t(RandomInt(50)), uint16_t(Random()), uint32_t(RandomInt(4)) };
            int size = int(list[0].size);
            int from = RandomInt(size + 1), to = from + RandomInt(size - from + 1);
            Both([&](bool original) {
                WGridDynamicList *l = &list[original ? 0 : 1];
                if (erase) {
                    WGridDynamicNode *first = l->head->next, *last;
                    for (int n = 0; n < from; n++)
                        first = first->next;
                    last = first;
                    for (int n = from; n < to; n++)
                        last = last->next;
                    WGridDynamicNode *result = NULL;
                    if (original)
                        ((DynamicEraseFn)0x000c5830)(l, 0, &result, first, last);
                    else
                        l->Erase(&result, first, last);
                } else {
                    WGridDynamicNode *head = l->head;
                    WGridDynamicNode *node = original ? ((DynamicBuyFn)0x000c5880)(l, 0, head, head->prev, &entry)
                                                      : l->BuyNode(head, head->prev, &entry);
                    if (original)
                        ((DynamicSizeFn)0x000c5a80)(l, 0, 1);
                    else
                        l->IncreaseSize(1);
                    head->prev = node;
                    node->prev->next = node;
                }
            });
            CheckU32("WGridDynamicList (erase, BuyNode, IncreaseSize)", i * 32 + op,
                     ReadList(&list[0]) == ReadList(&list[1]), 1);
            g_cases++;
        }
        Both([&](bool original) {
            if (original)
                ((DynamicDestructFn)0x000c59b0)(&list[0], 0);
            else
                list[1].Destruct();
        });
        CheckBytes("WGridDynamicList::Destruct", i, &list[0], &list[1], sizeof(WGridDynamicList));
    }

    // A cell node's AddDynamic / RemoveDynamic, with static entries
    for (int i = 0; i < 200; i++) {
        alignas(4) uint8_t nodes[2][0x10 + 64];
        WGridNode *node[2] = { reinterpret_cast<WGridNode *>(nodes[0]), reinterpret_cast<WGridNode *>(nodes[1]) };
        memset(nodes[0], 0, sizeof(nodes[0]));
        for (int type = 0; type < 4; type++) {
            node[0]->staticCount[type] = uint8_t(RandomInt(5));
            node[0]->staticOffset[type] = uint16_t(type * 16);
            for (int n = 0; n < 8; n++)
                reinterpret_cast<uint16_t *>(nodes[0] + 0x10 + type * 16)[n] = uint16_t(RandomInt(20));
        }
        memcpy(nodes[1], nodes[0], sizeof(nodes[0]));
        for (int op = 0, ops = 1 + RandomInt(40); op < ops; op++) {
            bool add = RandomInt(3) != 0;
            uint32_t index = uint32_t(RandomInt(20)), type = uint32_t(RandomInt(4));
            Both([&](bool original) {
                WGridNode *n = node[original ? 0 : 1];
                if (add) {
                    if (original)
                        ((NodeDynamicFn)0x000c5f10)(n, 0, index, type);
                    else
                        n->AddDynamic(uint16_t(index), type);
                } else {
                    if (original)
                        ((NodeDynamicFn)0x000c5900)(n, 0, index, type);
                    else
                        n->RemoveDynamic(index, type);
                }
            });
            CheckU32("WGridNode::AddDynamic / RemoveDynamic", i * 64 + op,
                     ReadList(node[0]->dynamic) == ReadList(node[1]->dynamic), 1);
            g_cases++;
        }
        for (int side = 0; side < 2; side++) {
            if (node[side]->dynamic != NULL) {
                node[side]->dynamic->Destruct();
                OperatorDelete(node[side]->dynamic);
            }
        }
    }

    // The dynamic elements' list
    for (int i = 0; i < 100; i++) {
        WGridMoverList list[2] = {};
        WGridMover value;
        for (int w = 0; w < 11; w++)
            reinterpret_cast<uint32_t *>(&value)[w] = Random();
        WGridMoverNode *node[2] = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            if (original) {
                list[side].head = ((MoverHeadFn)0x000c5990)(&list[side], 0);
                node[side] = ((MoverBuyFn)0x000c58c0)(&list[side], 0, list[side].head, list[side].head->prev, &value);
                ((MoverSizeFn)0x000c5b30)(&list[side], 0, 3);
            } else {
                list[side].head = list[side].BuyHead();
                node[side] = list[side].BuyNode(list[side].head, list[side].head->prev, &value);
                list[side].IncreaseSize(3);
            }
        });
        CheckU32("WGridMoverList::BuyHead", i, list[0].head->next == list[0].head && list[0].head->prev == list[0].head,
                 list[1].head->next == list[1].head && list[1].head->prev == list[1].head);
        CheckU32("WGridMoverList::BuyNode", i, node[0]->next == list[0].head && node[0]->prev == list[0].head,
                 node[1]->next == list[1].head && node[1]->prev == list[1].head);
        CheckBytes("WGridMoverList::BuyNode", i, &node[0]->value, &node[1]->value, sizeof(WGridMover));
        CheckU32("WGridMoverList::IncreaseSize", i, list[0].size, list[1].size);
        for (int side = 0; side < 2; side++) {
            UMemory::FastFree(node[side], sizeof(WGridMoverNode));
            UMemory::FastFree(list[side].head, sizeof(WGridMoverNode));
        }
        g_cases += 3;
    }

    // WGrid's constructor
    for (int i = 0; i < 50; i++) {
        Coord4 origin = { Uniform(-3000.0f, 0.0f), Uniform(-50.0f, 50.0f), Uniform(-3000.0f, 0.0f), Random() * 1.0f };
        uint32_t rows = uint32_t(RandomInt(64)), columns = uint32_t(RandomInt(64));
        float cell = RandomInt(10) == 0 ? Special() : Uniform(1.0f, 100.0f);
        WGrid grid[2];
        memset(grid, 0xcd, sizeof(grid));
        WGrid *answer[2] = {};
        Both([&](bool original) {
            if (original)
                answer[0] = ((GridConstructFn)0x000c57b0)(&grid[0], 0, &origin, rows, columns, cell);
            else
                answer[1] = grid[1].Construct(&origin, rows, columns, cell);
        });
        CheckBytes("WGrid::Construct", i, &grid[0], &grid[1], offsetof(WGrid, nodes));
        CheckU32("WGrid::Construct's answer", i, answer[0] == &grid[0], answer[1] == &grid[1]);
        if (rows * columns != 0)
            CheckBytes("WGrid::Construct's nodes", i, grid[0].nodes, grid[1].nodes, rows * columns * sizeof(WGridNode *));
        UMemory::Free(grid[0].nodes);
        UMemory::Free(grid[1].nodes);
        g_cases++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Colliders

struct WorldState {
    std::vector<uint8_t> manager, instances;

    void Save() {
        ShadowCollisionArrays *arrays = CollisionArrays;
        manager.assign(CollisionManagerObject, CollisionManagerObject + kCollisionManagerBytes);
        const uint8_t *from = reinterpret_cast<const uint8_t *>(arrays->instances);
        instances.assign(from, from + arrays->instanceCount * sizeof(WCollisionInstance));
    }
    void Restore() const {
        memcpy(CollisionManagerObject, manager.data(), manager.size());
        if (!instances.empty())
            memcpy(CollisionArrays->instances, instances.data(), instances.size());
    }
};

void CompareWorld(const char *what, int index, const WorldState &a, const WorldState &b) {
    CheckBytes(what, index, a.manager.data(), b.manager.data(), a.manager.size());
    if (a.instances.size() == b.instances.size() && !a.instances.empty())
        CheckBytes(what, index, a.instances.data(), b.instances.data(), a.instances.size());
}

void CompareColliders(const char *what, int index, const WCollider &a, const WCollider &b) {
    CheckBytes(what, index, &a, &b, offsetof(WCollider, instances));
    CheckBytes(what, index, &a.regionValid, &b.regionValid, sizeof(WCollider) - offsetof(WCollider, regionValid));
    CheckU32(what, index, a.instances.Size(), b.instances.Size());
    CheckU32(what, index, a.instances.Capacity(), b.instances.Capacity());
    if (a.instances.Size() == b.instances.Size()) {
        for (uint32_t i = 0; i < a.instances.Size(); i++) {
            const InstanceListEntry &x = a.instances.first[i], &y = b.instances.first[i];
            CheckU32(what, index, uint32_t(uintptr_t(x.instance)), uint32_t(uintptr_t(y.instance)));
            CheckU32(what, index, x.strips == NULL, y.strips == NULL);
            if (x.strips != NULL && y.strips != NULL) {
                CompareVectors(what, index, *x.strips, *y.strips);
                CheckU32(what, index, uint32_t(uintptr_t(x.strips->article)), uint32_t(uintptr_t(y.strips->article)));
            }
        }
    }
    CompareVectors(what, index, a.barriers, b.barriers);
    CompareVectors(what, index, a.barrierStamps, b.barrierStamps);
}

// GetWorldNormal's answer, less the bytes it leaves as its stack had them (as ColQueryShadow masks them):
// WorldCollisionInfo's constructor copies +0x20..+0x3f from its own stack (a barrier hit then overwrites them), and
// leaves +0x4c, +0x56 and +0x5c.
void CheckWorldInfo(const char *what, int index, const WorldCollisionInfo *a, const WorldCollisionInfo *b) {
    uint8_t bytes[2][sizeof(WorldCollisionInfo)];
    memcpy(bytes[0], a, sizeof(WorldCollisionInfo));
    memcpy(bytes[1], b, sizeof(WorldCollisionInfo));
    for (uint8_t *copy : bytes) {
        memset(copy + 0x4c, 0, 4);
        memset(copy + 0x56, 0, 2);
        memset(copy + 0x5c, 0, 4);
        if (copy[0x53] == kHitWorld)
            memset(copy + 0x20, 0, 0x20);
    }
    CheckBytes(what, index, bytes[0], bytes[1], sizeof(WorldCollisionInfo));
}

Coord3 Near(const Coord3 &at, float reach) {
    Coord3 offset = { at.x + Uniform(-reach, reach), at.y + Uniform(-reach * 0.2f, reach * 0.2f), at.z + Uniform(-reach, reach) };
    return offset;
}

void TestColliders() {
    ShadowCollisionArrays *arrays = CollisionArrays;
    uint32_t count = arrays->instanceCount;
    if (count == 0 || count > 0x10000) {
        printf("[colgrid] no collision instances - collider tests skipped\n");
        return;
    }
    static const uint32_t kMasks[] = { 0x4, 0x8, 0xc, 0xc, 0xc, 0x0, 0xffffffff, 0x1c };
    WorldState initial;   // the manager's counters and the instances' stamps, put back at the end
    initial.Save();
    for (int i = 0; i < 400; i++) {
        const Coord3 &base = arrays->instances[RandomInt(int(count))].position;
        Coord3 position = Near(base, 30.0f);
        float radius = RandomInt(40) == 0 ? 0.0f : Uniform(0.5f, 25.0f);
        uint32_t mask = kMasks[RandomInt(8)];
        bool sweep = RandomInt(2) == 0;
        bool flag = RandomInt(2) == 0;
        Coord4 segment[2] = {};
        segment[0] = { position.x, position.y, position.z, 1.0f };
        Coord3 end = Near(position, RandomInt(2) == 0 ? 3.0f : 40.0f);
        segment[1] = { end.x, end.y, end.z, 1.0f };

        WCollider collider[2];
        memset(collider, 0xcd, sizeof(collider));
        WorldState before, after[2];
        before.Save();
        bool ok = true;
        int side = 0;
        auto run = [&](bool original) {
            side = original ? 0 : 1;
            before.Restore();
            if (original) {
                if (sweep)
                    ((ColliderSweepFn)0x000be620)(&collider[0], 0, segment, mask);
                else
                    ((ColliderPointFn)0x000be540)(&collider[0], 0, &position, radius, flag, mask);
            } else {
                if (sweep)
                    collider[1].Construct(segment, mask);
                else
                    collider[1].Construct(&position, radius, flag, mask);
            }
            after[side].Save();
        };
        ok = Both(run);
        g_cases++;
        if (!ok)
            continue;   // a half-built collider: leaked, not compared
        CompareWorld("WCollider::Construct (world)", i, after[0], after[1]);
        CompareColliders("WCollider::Construct", i, collider[0], collider[1]);

        for (int step = 0, steps = RandomInt(6); step < steps; step++) {
            int kind = RandomInt(5);
            Coord3 to = Near(collider[1].position, RandomInt(2) == 0 ? 2.0f : 40.0f);
            float toRadius = RandomInt(3) == 0 ? collider[1].radius : Uniform(0.5f, 25.0f);
            Coord4 move[2] = {};
            move[0] = { collider[1].position.x, collider[1].position.y, collider[1].position.z, 1.0f };
            move[1] = { to.x, to.y, to.z, 1.0f };
            uint32_t testMask = RandomInt(2) == 0 ? collider[1].collisionMask : kMasks[RandomInt(8)];
            bool answer[2] = {};
            alignas(16) WorldCollisionInfo info[2];
            memset(info, 0xcd, sizeof(info));
            before.Save();
            ok = Both([&](bool original) {
                int s = original ? 0 : 1;
                WCollider *c = &collider[s];
                before.Restore();
                if (kind == 0) {
                    answer[s] = original ? ((ColliderInSweepFn)0x000bd7b0)(c, 0, move, testMask)
                                         : c->InRegion(move, testMask);
                } else if (kind == 1) {
                    answer[s] = original ? ((ColliderInPointFn)0x000bd820)(c, 0, &to, toRadius, testMask)
                                         : c->InRegion(&to, toRadius, testMask);
                } else if (kind == 2) {
                    // the segment the collider moves along, and a whole WorldCollisionInfo for the answer
                    answer[s] = original ? ((ColliderNormalFn)0x000bd890)(c, 0, move, &info[0])
                                         : c->GetWorldNormal(move, &info[1]);
                } else if (kind == 3) {
                    if (original)
                        ((ColliderRefreshPointFn)0x000be4b0)(c, 0, &to, toRadius);
                    else
                        c->Refresh(&to, toRadius);
                } else {
                    if (original)
                        ((ColliderRefreshSweepFn)0x000be420)(c, 0, move);
                    else
                        c->Refresh(move);
                }
                after[s].Save();
            });
            g_cases++;
            if (!ok)
                break;
            static const char *const kNames[] = { "WCollider::InRegion (sweep)", "WCollider::InRegion (sphere)",
                                                  "WCollider::GetWorldNormal", "WCollider::Refresh (sphere)",
                                                  "WCollider::Refresh (sweep)" };
            CheckU32(kNames[kind], i, answer[0], answer[1]);
            if (kind == 2)
                CheckWorldInfo(kNames[kind], i, &info[0], &info[1]);
            CompareWorld(kNames[kind], i, after[0], after[1]);
            CompareColliders(kNames[kind], i, collider[0], collider[1]);
        }
        if (!ok)
            continue;

        // Validate, as it is and with an instance it listed changed
        for (int changed = 0; changed < 2; changed++) {
            CollisionArticle **source = NULL;
            CollisionArticle *was = NULL;
            if (changed) {
                if (collider[1].barrierStamps.Size() != 0)
                    source = const_cast<CollisionArticle **>(
                        collider[1].barrierStamps.first[RandomInt(int(collider[1].barrierStamps.Size()))].source);
                else if (collider[1].instances.Size() != 0 && collider[1].instances.first[0].strips != NULL)
                    source = &collider[1].instances.first[0].instance->article;
                if (source == NULL)
                    break;
                was = *source;
                *source = reinterpret_cast<CollisionArticle *>(uintptr_t(was) ^ 4);
            }
            bool valid[2] = {};
            Both([&](bool original) {
                if (original)
                    valid[0] = ((ColliderBoolFn)0x000bd750)(&collider[0], 0);
                else
                    valid[1] = collider[1].Validate();
            });
            CheckU32("WCollider::Validate", i * 2 + changed, valid[0], valid[1]);
            if (source != NULL)
                *source = was;
            g_cases++;
        }

        // Clear, then the destructor
        Both([&](bool original) {
            if (original)
                ((ColliderVoidFn)0x000bdd40)(&collider[0], 0);
            else
                collider[1].Clear();
        });
        CompareColliders("WCollider::Clear", i, collider[0], collider[1]);
        Both([&](bool original) {
            if (original)
                ((ColliderVoidFn)0x000be180)(&collider[0], 0);
            else
                collider[1].Destruct();
        });
        CompareColliders("WCollider::Destruct", i, collider[0], collider[1]);
        g_cases += 2;
    }
    initial.Restore();
}

// ---------------------------------------------------------------------------------------------------------------
// The dynamic grid

struct GridState {
    std::vector<uint32_t> cells;      // the cells with a node
    std::vector<ListState> lists;

    void Read() {
        cells.clear();
        lists.clear();
        WGrid *grid = TheGrid;
        for (uint32_t cell = 0; cell < grid->rows * grid->columns; cell++) {
            if (grid->nodes[cell] == NULL)
                continue;
            cells.push_back(cell);
            lists.push_back(ReadList(grid->nodes[cell]->dynamic));
        }
    }

    // Puts every cell's list back as it was (by content: new nodes)
    void Restore() const {
        WGrid *grid = TheGrid;
        for (size_t i = 0; i < cells.size(); i++) {
            WGridNode *node = grid->nodes[cells[i]];
            if (node == NULL)
                continue;
            if (!lists[i].exists) {
                if (node->dynamic != NULL) {
                    node->dynamic->Destruct();
                    OperatorDelete(node->dynamic);
                    node->dynamic = NULL;
                }
                continue;
            }
            WGridDynamicList *list = node->dynamic;
            if (list == NULL) {
                list = static_cast<WGridDynamicList *>(OperatorNew(sizeof(WGridDynamicList)));
                list->allocator = 0;
                list->head = list->BuyHead();
                list->size = 0;
                node->dynamic = list;
            } else {
                WGridDynamicNode *ignored;
                list->Erase(&ignored, list->head->next, list->head);
            }
            for (uint64_t entry : lists[i].entries)
                AppendEntry(list, entry);
        }
    }
};

void CompareGrids(const char *what, int index, const GridState &a, const GridState &b) {
    g_checks++;
    if (a.cells != b.cells) {
        Differ(what, index, "different cells have nodes");
        return;
    }
    int differing = 0, first = -1;
    for (size_t i = 0; i < a.lists.size(); i++) {
        if (!(a.lists[i] == b.lists[i])) {
            if (first < 0)
                first = int(a.cells[i]);
            differing++;
        }
    }
    if (differing != 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "%d cell lists differ, the first cell %d", differing, first);
        Differ(what, index, detail);
    }
}

// The dynamic elements' values, and the instance and object state they write
struct MoverState {
    std::vector<WGridMover> movers;
    std::vector<Coord3> objects;
    std::vector<uint8_t> instances;

    void Read() {
        movers.clear();
        objects.clear();
        WGridMoverNode *head = GridMovers.head;
        for (WGridMoverNode *node = head->next; node != head; node = node->next) {
            movers.push_back(node->value);
            if (node->value.object != NULL)
                objects.push_back(node->value.object->position);
        }
        ShadowCollisionArrays *arrays = CollisionArrays;
        const uint8_t *from = reinterpret_cast<const uint8_t *>(arrays->instances);
        instances.assign(from, from + arrays->instanceCount * sizeof(WCollisionInstance));
    }
    void Restore() const {
        size_t m = 0, o = 0;
        WGridMoverNode *head = GridMovers.head;
        for (WGridMoverNode *node = head->next; node != head && m < movers.size(); node = node->next) {
            node->value = movers[m++];
            if (node->value.object != NULL && o < objects.size())
                node->value.object->position = objects[o++];
        }
        if (!instances.empty())
            memcpy(CollisionArrays->instances, instances.data(), instances.size());
    }
};

void CompareMovers(const char *what, int index, const MoverState &a, const MoverState &b) {
    CheckU32(what, index, uint32_t(a.movers.size()), uint32_t(b.movers.size()));
    if (a.movers.size() == b.movers.size()) {
        for (size_t i = 0; i < a.movers.size(); i++) {
            WGridMover x = a.movers[i], y = b.movers[i];
            x.unknown16 = y.unknown16 = 0;   // the original's stack bytes
            CheckBytes(what, index, &x, &y, sizeof(WGridMover));
        }
    }
    CheckU32(what, index, uint32_t(a.objects.size()), uint32_t(b.objects.size()));
    if (a.objects.size() == b.objects.size() && !a.objects.empty())
        CheckBytes(what, index, a.objects.data(), b.objects.data(), a.objects.size() * sizeof(Coord3));
    if (a.instances.size() == b.instances.size() && !a.instances.empty())
        CheckBytes(what, index, a.instances.data(), b.instances.data(), a.instances.size());
}

void TestDynamicGrid() {
    if (GridMovers.head == NULL) {
        printf("[colgrid] no dynamic element list - dynamic grid tests skipped\n");
        return;
    }
    GridState grid0;
    grid0.Read();
    MoverState movers0;
    movers0.Read();
    WGrid *grid = TheGrid;
    float spanX = float(grid->columns) * grid->cellSize, spanZ = float(grid->rows) * grid->cellSize;

    for (int i = 0; i < 300; i++) {
        uint32_t type, index;
        Coord4 last;
        if (!movers0.movers.empty() && RandomInt(3) != 0) {
            const WGridMover &mover = movers0.movers[RandomInt(int(movers0.movers.size()))];
            type = mover.type;
            index = mover.index;
            last = RandomInt(2) == 0 ? mover.lastPosition : *mover.position;
        } else {
            type = uint32_t(RandomInt(4));
            index = uint32_t(RandomInt(300));
            last = { grid->origin.x + Uniform(0.0f, spanX), 0.0f, grid->origin.z + Uniform(0.0f, spanZ), 1.0f };
        }
        if (RandomInt(2) == 0)
            last.w = Uniform(0.5f, 3.0f * grid->cellSize);
        Coord4 position = { last.x + Uniform(-2.0f, 2.0f) * grid->cellSize, last.y,
                           last.z + Uniform(-2.0f, 2.0f) * grid->cellSize, RandomInt(2) == 0 ? last.w : Uniform(0.5f, 3.0f * grid->cellSize) };
        Coord4 lasts[2] = { last, last };
        GridState after[2];
        Both([&](bool original) {
            int side = original ? 0 : 1;
            grid0.Restore();
            if (original)
                ((AddElementFn)0x000c6d70)(&lasts[0], &position, type, index);
            else
                WGrid::AddGridNodeDynamicElement(&lasts[1], &position, type, index);
            after[side].Read();
        });
        CompareGrids("WGrid::AddGridNodeDynamicElement", i, after[0], after[1]);
        CheckBytes("WGrid::AddGridNodeDynamicElement (last)", i, &lasts[0], &lasts[1], sizeof(Coord4));
        g_cases++;
    }
    grid0.Restore();

    for (int round = 0; round < 20; round++) {
        MoverState perturbed = movers0;
        for (WGridMover &mover : perturbed.movers) {
            if (RandomInt(2) == 0)
                continue;
            float reach = RandomInt(3) == 0 ? 0.005f : 3.0f * grid->cellSize;
            mover.lastPosition.x += Uniform(-reach, reach);
            mover.lastPosition.y += Uniform(-reach, reach) * 0.1f;
            mover.lastPosition.z += Uniform(-reach, reach);
        }
        GridState gridAfter[2];
        MoverState moverAfter[2];
        Both([&](bool original) {
            int side = original ? 0 : 1;
            grid0.Restore();
            perturbed.Restore();
            if (original)
                ((StaticFn)0x000c7200)();
            else
                WGrid::UpdateDynamicNodes();
            gridAfter[side].Read();
            moverAfter[side].Read();
        });
        CompareGrids("WGrid::UpdateDynamicNodes (cells)", round, gridAfter[0], gridAfter[1]);
        CompareMovers("WGrid::UpdateDynamicNodes (elements)", round, moverAfter[0], moverAfter[1]);
        g_cases++;
    }
    grid0.Restore();
    movers0.Restore();

    // Restart: a new grid and new elements from the CARP data, both times
    struct Rebuilt {
        WGrid header;
        std::vector<WGridNode *> nodes;
        GridState cells;
        MoverState movers;
        uint32_t moverCount;
    } rebuilt[2];
    Both([&](bool original) {
        int side = original ? 0 : 1;
        if (original)
            ((StaticFn)0x000c6290)();
        else
            WGrid::Restart();
        rebuilt[side].header = *TheGrid;
        rebuilt[side].nodes.assign(TheGrid->nodes, TheGrid->nodes + TheGrid->rows * TheGrid->columns);
        rebuilt[side].cells.Read();
        rebuilt[side].movers.Read();
        rebuilt[side].moverCount = GridMovers.size;
    });
    CheckBytes("WGrid::Restart (grid)", 0, &rebuilt[0].header, &rebuilt[1].header, offsetof(WGrid, nodes));
    CheckU32("WGrid::Restart (nodes)", 0, uint32_t(rebuilt[0].nodes.size()), uint32_t(rebuilt[1].nodes.size()));
    if (rebuilt[0].nodes.size() == rebuilt[1].nodes.size() && !rebuilt[0].nodes.empty())
        CheckBytes("WGrid::Restart (nodes)", 0, rebuilt[0].nodes.data(), rebuilt[1].nodes.data(),
                   rebuilt[0].nodes.size() * sizeof(WGridNode *));
    CompareGrids("WGrid::Restart (cells)", 0, rebuilt[0].cells, rebuilt[1].cells);
    CompareMovers("WGrid::Restart (elements)", 0, rebuilt[0].movers, rebuilt[1].movers);
    CheckU32("WGrid::Restart (element count)", 0, rebuilt[0].moverCount, rebuilt[1].moverCount);
    g_cases++;
    // As before the test: the cells' lists and the elements' values (Restart rebuilt them in the same order)
    grid0.Restore();
    movers0.Restore();
}

}  // namespace

void ColGridShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_COLGRIDSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    if (TheGrid == NULL || fgWorld == NULL || CollisionArrays == NULL) {
        printf("[colgrid] the track's collision data is not loaded - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    TestGridQueries();
    TestTree();
    TestInstances();
    TestHelpers();
    TestStampVectors();
    TestLists();
    TestColliders();
    TestDynamicGrid();
    ResetFpu();
    printf("[colgrid] colliders, instances, grid, tree vs originals: %d cases, %d checks, %d differ%s\n", g_cases,
           g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[colgrid]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
