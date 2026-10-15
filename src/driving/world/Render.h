#ifndef DRIVING_WORLD_RENDER_H_
#define DRIVING_WORLD_RENDER_H_

// ---------------------------------------------------------------------------------------------------------------
// WRender, the world's drawing (one instance, fgRender): the track's render instances split into five draw passes,
// the visibility curtains, and the walk of the scene tree that finds the visible nodes and curtains for a camera.
// The renderer's culling then steps through the visible nodes' instances (GetNextPoint) and keeps the ones it
// sees (SetCull) as the draws of a CachedDrawInfo, which DrawPass draws a range of passes of. DrawWorldAtPoint
// does it all for a point with a simple distance test instead of the frustum. See Render.cpp.
//
// A pass's instances are a run of the instance array: an instance is in its article's pass, or in the last pass
// when it has kInstanceSky (RSky::Draw draws the run of those at the start of the array).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Tree.h"                         // WMapNode, WRenderSortEntry
#include "VisCurtain.h"
#include "World.h"                        // ProcAnimState; CARP::Instance
#include "../camera/Camera.h"             // RCamera
#include "../../common/xbeClass.h"        // LAYOUT_32

constexpr int kMaxVisibleNodes = 0x300;
constexpr int kMaxDraws = 0x400;

// The visible nodes of the scene tree and the instances to draw (Ghidra: CachedDrawInfo). A draw's key is the
// instance's address.
struct CachedDrawInfo {
    int32_t nodeCount;                          // +0x000
    int32_t drawCount;                          // +0x004
    WMapNode *nodes[kMaxVisibleNodes];          // +0x008
    WRenderSortEntry draws[kMaxDraws];          // +0xc08
};
LAYOUT_32(sizeof(CachedDrawInfo) == 0x2c08, "CachedDrawInfo is 0x2c08 bytes");
LAYOUT_32(offsetof(CachedDrawInfo, draws) == 0xc08, "CachedDrawInfo::draws");

// CARP::Instance::flags as WRender reads them (World.h has the ones the world sets)
enum WRenderInstanceFlags : uint8_t {
    kInstanceSky = 0x04,                // the last pass; RSky::Draw draws these
    kInstanceNoFarCull = 0x08,          // GetNextPoint answers no far-plane test
    kInstance40 = 0x40,                 // GetNextPoint: the far-plane test always, scaled 0.3 or 2
};

class WRender {
public:
    static constexpr int kPassCount = 5;
    static constexpr int kSkyPass = 4;

    // DrawPass's sort before drawing
    enum SortMode {
        kNoSort = 0,
        kSortByInstance = 1,            // aLessThanB
        kSortByDistance = 2,            // someComparisonOperator, farthest first
    };

    static constexpr int kNoMorePoints = -1;    // GetNextPoint at the end of the list

    CARP::Instance *instances;          // +0x00 the track's render instances ('in  ')
    CARP::Instance *passFirst[kPassCount];  // +0x04 each pass's first instance; an empty pass: where the next starts
    int32_t passCount[kPassCount];      // +0x18
    ProcAnimState *procAnims;           // +0x2c 'ps  ', NULL if none
    uint32_t curtainCount;              // +0x30 'VisC'; not set when the track has none
    WVisCurtain *curtains;              // +0x34 NULL if none
    uint8_t unknown38[0x9c];
    const RCamera *camera;              // +0xd4 GenerateCurtainsAndNodes's
    float unknownD8;                    // +0xd8 GenerateCurtainsAndNodes's; RRenderWorldCamera passes 120
    uint8_t unknownDC[4];

    // The tree walk for a camera: the visible nodes into fgRender's list, their visible curtains made active.
    // `clip` false: the node and everything under it is inside the frustum.
    void FindVisibleTreeNodesAndCurtains(WMapNode *node, const Coord4 *box, bool clip, int depth);   // 0x000c7330
    CachedDrawInfo* GenerateCurtainsAndNodes(const RCamera *camera, float unknown);              // 0x000c7510
    // The culling's walk over the instances of a list's visible nodes. PrepareForCull empties the draws of the
    // list GenerateCurtainsAndNodes filled last, not of `list`.
    static void PrepareForCull(CachedDrawInfo *list);                                           // 0x000c75b0
    // The next instance (its address) and its bounding sphere, the curtain test's margin and the far-plane test
    // to use; kNoMorePoints at the end.
    static intptr_t GetNextPoint(Coord4 *sphere, float *height, bool *checkFar, float *farScale);   // 0x000c75e0
    // Adds the instance to the draws unless it is culled (at most kMaxDraws - 1 of them)
    static void SetCull(intptr_t item, bool culled, float distance);                            // 0x000c7700
    // The draws of passes lastPass down to firstPass, in that order
    void CopyDrawPasses(const CachedDrawInfo *from, CachedDrawInfo *to, int firstPass, int lastPass);   // 0x000c7750
    // DrawWorldAtPoint's tree walk: the visible nodes by DistanceCheck2d, no curtains
    void SimpleDrawVisibleTreeNodes(WMapNode *node, const Coord4 *box, bool clip, int depth);   // 0x000c77f0
    WRender* Construct();                                                                       // 0x000c7a50
    static void Init();                                                                         // 0x000c7ce0
    static void ShutDown();                                                                     // 0x000c7d50
    // Sorts the draws (SortMode) and draws passes lastPass down to firstPass
    void DrawPass(CachedDrawInfo *list, int firstPass, int lastPass, int sort);                 // 0x000c8370
    void DrawWorld(CachedDrawInfo *list, int sort);                                             // 0x000c8450
    void DrawWorldAtPoint(CachedDrawInfo *list, const Coord3 *eye, float radius, float ambient, Coord4 facing);   // 0x000c8490
};
LAYOUT_32(sizeof(WRender) == 0xe0, "WRender is 224 bytes");
LAYOUT_32(offsetof(WRender, procAnims) == 0x2c && offsetof(WRender, curtains) == 0x34 &&
          offsetof(WRender, camera) == 0xd4, "WRender layout");

extern WRender *fgRender;               // the name is ours

// The cursor of GetNextPoint's walk
struct WCullCursor {
    int32_t node;                       // the index of the visible node
    int32_t item;                       // the index of its instance
    CachedDrawInfo *list;               // PrepareForCull's
};

extern CachedDrawInfo VisibleList;
extern CachedDrawInfo *CurrentList;     // the list the tree walks fill
extern WCullCursor Cursor;

// std::sort over the draws, as the game compiled it (Dinkumware's helpers, named without their underscore; the
// namespace is ours). Ranges are [first, last); holes and counts are indices.
typedef bool (*RenderSortPredicate)(const WRenderSortEntry *a, const WRenderSortEntry *b);

struct RenderSortRange {
    WRenderSortEntry *first;
    WRenderSortEntry *last;
};

namespace RenderSort {
void PushHeap(WRenderSortEntry *first, int hole, int top, WRenderSortEntry value, RenderSortPredicate pred);   // 0x000c7920
void Rotate(WRenderSortEntry *first, WRenderSortEntry *mid, WRenderSortEntry *last);           // 0x000c7990
void Med3(WRenderSortEntry *first, WRenderSortEntry *mid, WRenderSortEntry *last, RenderSortPredicate pred);   // 0x000c7d70
void AdjustHeap(WRenderSortEntry *first, int hole, int bottom, WRenderSortEntry value, RenderSortPredicate pred);   // 0x000c7df0
void Median(WRenderSortEntry *first, WRenderSortEntry *mid, WRenderSortEntry *last, RenderSortPredicate pred);   // 0x000c7e80
void MakeHeap(WRenderSortEntry *first, WRenderSortEntry *last, RenderSortPredicate pred);      // 0x000c7f20
RenderSortRange* UnguardedPartition(RenderSortRange *result, WRenderSortEntry *first, WRenderSortEntry *last,
                                    RenderSortPredicate pred);                                  // 0x000c7f70
void InsertionSort(WRenderSortEntry *first, WRenderSortEntry *last, RenderSortPredicate pred); // 0x000c8180
void SortHeap(WRenderSortEntry *first, WRenderSortEntry *last, RenderSortPredicate pred);      // 0x000c8220
void Sort(WRenderSortEntry *first, WRenderSortEntry *last, int ideal, RenderSortPredicate pred);   // 0x000c8280
}  // namespace RenderSort

#endif // DRIVING_WORLD_RENDER_H_
