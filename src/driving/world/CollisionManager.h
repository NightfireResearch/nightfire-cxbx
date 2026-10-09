#ifndef DRIVING_WORLD_COLLISIONMANAGER_H_
#define DRIVING_WORLD_COLLISIONMANAGER_H_

// ---------------------------------------------------------------------------------------------------------------
// WCollisionMgr, the driving engine's collision manager (Ghidra: WCollisionMgr; PS2 symbol fgCollisionMgr for the
// one instance). It answers the game's spatial questions against the loaded track: the height and normal of the
// ground under a point, which collision instances, objects and barriers are near a point or a segment (through the
// world grid), where a segment first hits the world, its barriers or its objects, and whether a shot went through a
// window. Init builds it from the track's "CDat" group of the CARP file: the collision instances ("ci", 0x40 bytes
// each, each with its collision article "ca") and the collision objects ("co", 0x30 bytes each).
//
// The class is ported in two files: its queries and the compiled copies of the C++ library's containers the linker
// put between them (0x000bedd0-0x000c2ff0, CollisionQueries.cpp), and its lists, hit checks and lifecycle
// (0x000c2ff0-0x000c5710, CollisionManager.cpp). The data it reads is CollisionTypes.h's.
//
// A segment is two Coord4s, start then end. Matrices are MATRIX4 (row vectors, the translation in row 3). Heights
// and parameters the original leaves on the x87 stack are answered as double.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "CollisionTypes.h"
#include "../data/Tree.h"               // Tree: the article map's compiled code is the data layer's maps'
#include "../engine/RbTree.h"

namespace CARP {
class Instance;
}
struct OBB;
class UGroup;
class WWorldPos;

// ---- the windows hit so far: a map from instance to a map from pane to its damage

struct WindowPaneValue {
    const WindowPane *pane;     // the key (compared unsigned)
    WindowHit hit;
};

struct WindowPaneNode : RbTreeNode<WindowPaneNode, WindowPaneValue> {};
static_assert(sizeof(WindowPaneNode) == 0x40, "a pane map node is 64 bytes");

struct WindowPaneInsert {       // std::pair<iterator, bool>
    WindowPaneNode *node;
    bool inserted;
};

struct WindowPaneIterator {
    WindowPaneNode *node;
    void Decrement();                                                           // 0x000bfe10
    // (the linker placed Increment among RWindow's code)
    void Increment();                                                           // 0x000a8a80
};

// std::map<const WindowPane *, WindowHit>
struct WindowPaneMap : RbTree<WindowPaneNode> {
    // The leftmost node under `node` (the linker placed it among RWindow's code)
    static WindowPaneNode* Min(WindowPaneNode *node);                           // 0x000a87a0
    WindowPaneMap* Construct();                                                 // 0x000c45d0
    WindowPaneMap* CopyConstruct(const WindowPaneMap *other);                   // 0x000c3ab0
    void Destruct();                                                            // 0x000c4610

    void Lrotate(WindowPaneNode *node);                                         // 0x000bfd50
    void Rrotate(WindowPaneNode *node);                                         // 0x000bfdb0
    static WindowPaneNode* Max(WindowPaneNode *node);                           // 0x000bf440
    WindowPaneNode* BuyNode(WindowPaneNode *left, WindowPaneNode *parent, WindowPaneNode *right,
                            const WindowPaneValue *value, uint8_t color);       // 0x000c0c60
    static WindowPaneNode* BuyHeadNode();                                       // 0x000c0cb0
    void EraseSubtree(WindowPaneNode *node);                                    // 0x000c0cf0
    WindowPaneNode* Copy(WindowPaneNode *node, WindowPaneNode *parent);        // 0x000c1180
    void CopyTree(const WindowPaneMap *other);                                  // 0x000c12f0
    WindowPaneNode** InsertAt(WindowPaneNode **result, bool addLeft, WindowPaneNode *where,
                              const WindowPaneValue *value);                    // 0x000c1500
    WindowPaneNode** EraseAt(WindowPaneNode **result, WindowPaneNode *where);   // 0x000c2200
    WindowPaneInsert* InsertUnique(WindowPaneInsert *result, const WindowPaneValue *value);   // 0x000c28b0
    WindowPaneNode** EraseRange(WindowPaneNode **result, WindowPaneNode *first, WindowPaneNode *last);   // 0x000c2bb0
};
static_assert(sizeof(WindowPaneMap) == 0xc, "a map is 12 bytes");

struct WindowMapValue {
    WCollisionInstance *instance;   // the key
    WindowPaneMap panes;

    WindowMapValue* Construct(WCollisionInstance *const *key, const WindowPaneMap *other);   // 0x000c46d0
    void Destruct();                                                            // 0x000c4650
};

struct WindowMapNode : RbTreeNode<WindowMapNode, WindowMapValue> {
    void DestroyValue();                                                        // 0x000c4690
};
static_assert(sizeof(WindowMapNode) == 0x20, "a window map node is 32 bytes");

// The tree's minimum, for the maps with 0x20-byte nodes (the window map's, and two elsewhere the linker folded in).
WindowMapNode* WindowMapMin(WindowMapNode *node);                               // 0x000bf460

struct WindowMapInsert {
    WindowMapNode *node;
    bool inserted;
};

// std::map<WCollisionInstance *, WindowPaneMap>
struct WindowMap : RbTree<WindowMapNode> {
    WindowMapNode* Buynode(WindowMapNode *left, WindowMapNode *parent, WindowMapNode *right,
                           const WindowMapValue *value, uint8_t color);                  // 0x000c46f0
    WindowMapNode** Insert(WindowMapNode **result, bool addLeft, WindowMapNode *where,
                           const WindowMapValue *value);                                 // 0x000c47a0
    WindowMapNode** EraseAt(WindowMapNode **result, WindowMapNode *where);                // 0x000c4980
    WindowMapInsert* InsertUnique(WindowMapInsert *result, const WindowMapValue *value);  // 0x000c4c60
    void EraseSubtree(WindowMapNode *node);                                               // 0x000c4d20
    WindowMapNode** EraseRange(WindowMapNode **result, WindowMapNode *first, WindowMapNode *last);   // 0x000c52c0
};
static_assert(sizeof(WindowMap) == 0xc, "a map is 12 bytes");

// ---- std::map<WCollisionInstance *, CollisionArticle *>: the article each instance had before SetCollisionArticle
// first changed it, put back by Restart. Its node is the data layer's maps' (0x18 bytes), and so is its compiled
// code, instruction for instruction: these entries run data/Tree.h's.

struct ArticleMapValue {
    WCollisionInstance *instance;   // the key (compared unsigned)
    CollisionArticle *article;
};

struct ArticleMapNode : RbTreeNode<ArticleMapNode, ArticleMapValue> {};
static_assert(sizeof(ArticleMapNode) == sizeof(TreeNode), "an article map node is the data layer's map node");

struct ArticleMapInsert {
    ArticleMapNode *node;
    bool inserted;
};

struct ArticleMap : RbTree<ArticleMapNode> {
    void EraseSubtree(ArticleMapNode *node);                                    // 0x000c0c20
    ArticleMapNode** InsertAt(ArticleMapNode **result, bool addLeft, ArticleMapNode *where,
                              const ArticleMapValue *value);                    // 0x000c16e0
    ArticleMapNode** EraseAt(ArticleMapNode **result, ArticleMapNode *where);   // 0x000c24d0
    ArticleMapInsert* InsertUnique(ArticleMapInsert *result, const ArticleMapValue *value);   // 0x000c2a80
    ArticleMapNode** EraseRange(ArticleMapNode **result, ArticleMapNode *first, ArticleMapNode *last);   // 0x000c2f30

    Tree *AsTree() { return reinterpret_cast<Tree *>(this); }
};
static_assert(sizeof(ArticleMap) == sizeof(Tree), "a map is 12 bytes");

// ---- the manager

class WCollisionMgr {
public:
    uint32_t unknown00;                 // +0x00
    uint32_t queryStamp;                // +0x04 one more for every instance list built
    WCollisionInstance *instances;      // +0x08 the track's "ci" data
    uint32_t instanceCount;             // +0x0c
    WCollisionObject *objects;          // +0x10 the track's "co" data
    WindowMap windows;                  // +0x14
    ArticleMap articles;                // +0x20
    uint32_t barrierMask;               // +0x2c 0xf0: faces and barriers whose sub-type shares a bit with it are
                                        //       skipped
    uint8_t unknown30;                  // +0x30
    uint8_t unknown31[3];

    // ---- the queries (CollisionQueries.cpp)

    // The height of the bumps on a surface of the given type at a point (0 for smooth ones). Unrounded.
    double SurfaceBumpHeight(const Coord3 *point, const uint8_t *faceType);                // 0x000bedd0

    // The triangle of a strip under `point` (in x/z) whose tag passes the mask, NULL if none.
    const StripVertex* FindStripTriangle(const Coord3 *point, const StripVertex *const *strip);  // 0x000bef30

    // The strip, carried by `matrix`, its triangle nearest above or below `point` (within 1 below): the triangle in
    // the strip (NULL if none) and the height of the point above it.
    const StripVertex* FindFaceInTriStrip(const MATRIX4 *matrix, const Coord3 *point, const StripVertex *const *strip,
                                          float *height);                                   // 0x000bf0e0

    // The height of the ground under a point; false if there is none. `unused` is not read.
    bool GetWorldHeightAtPoint(const Coord3 *point, float *height, bool unused);            // 0x000bf210

    // Where a segment meets the ground found under its end (in `position`): info->hitType kHitWorld if it does.
    bool GetGroundCollision(const Coord4 *segment, WWorldPos *position, WorldCollisionInfo *info);   // 0x000bf2d0

    // The nearer to `point` of two hits copied to `out` (nothing if neither hit).
    void ClosestCollisionInfo(const Coord3 *point, const WorldCollisionInfo *a, const WorldCollisionInfo *b,
                              WorldCollisionInfo *out);                                     // 0x000bf3a0

    // The face of an instance under a point: all its strips (0x000bf4c0), or those listed (0x000bffe0).
    bool FindFaceInCInstStrips(const Coord3 *point, WCollisionInstance *instance, StripTriangle *face,
                               float *height);                                              // 0x000bf4c0
    bool FindFaceInCInst(const Coord3 *point, const InstanceListEntry *entry, StripTriangle *face,
                         float *height);                                                    // 0x000bffe0

    // The face of an instance along a segment, given the segment's frame (WWorldMath::MakeSegSpaceMatrix) and its
    // end: all its strips (0x000bf680), or those listed (0x000c01d0).
    bool FindFaceInCInstStrips(const MATRIX4 *segmentFrame, const Coord4 *end, WCollisionInstance *instance,
                               StripTriangle *face, float *height);                         // 0x000bf680
    bool FindFaceInCInst(const MATRIX4 *segmentFrame, const Coord4 *end, const InstanceListEntry *entry,
                         StripTriangle *face, float *height);                               // 0x000c01d0

    // Where a segment enters a box (half extents, placed by matrix): its top or bottom face, else the diagonal
    // quad's edges. The point in the world's frame.
    bool GetOBBObjectIntersection(const Coord4 *segment, const MATRIX4 *matrix, const Coord4 *halfExtents,
                                  Coord4 *hit);                                             // 0x000bf8c0

    // The cylinder object of a list a segment meets nearest its start (`hit` holds the last meeting point tested),
    // and the first box object it meets.
    WCollisionObject* GetClosestIntersectingCylObject(const Coord4 *segment, Coord4 *hit, const ObjectList *list);   // 0x000c0440
    WCollisionObject* GetClosestIntersectingOBBObject(const Coord4 *segment, Coord4 *hit, const ObjectList *list);   // 0x000c05f0

    // The barrier of a list a segment meets nearest its start: info->point and the barrier's entry, hitType
    // kHitBarrier.
    bool GetClosestIntersectingBarrier(const BarrierList *list, const Coord4 *segment, WorldCollisionInfo *info);   // 0x000c0660

    // The same over the barriers of every listed instance, with the barrier's normal.
    bool GetBarrierNormal(const InstanceList *list, const Coord4 *segment, WorldCollisionInfo *info);   // 0x000c0890

    // The barrier hit with its normal (facing the segment's start); the cylinder object hit and the box object hit,
    // each with a normal out from the object's position in x and z (the box's too: the original's).
    bool GetBarrierCollision(const BarrierList *list, const Coord4 *segment, WorldCollisionInfo *info);     // 0x000c0b60
    bool GetCylObjectCollision(const Coord4 *segment, const ObjectList *list, WorldCollisionInfo *info);    // 0x000c0f80
    bool GetOBBObjectCollision(const Coord4 *segment, const ObjectList *list, WorldCollisionInfo *info);    // 0x000c1080

    // The nearer of the barrier hit and (with `checkGround`) the ground hit along a segment - the face of the listed
    // instances WWorldPos::FindClosestFace finds along it - with its normal.
    bool GetWorldNormal(const InstanceList *instances, const BarrierList *barriers, const Coord4 *segment,
                        WorldCollisionInfo *info, bool checkGround);                        // 0x000c0e00

    // Takes the last object off a list and answers a new OBB of it (UMemory "OBB"), its render instance's
    // velocity scaled by the time step in `velocity` (0 if it has none) and its face tag in `faceTag`.
    OBB* PopObjectOBB(ObjectList *list, Coord3 *velocity, uint16_t *faceTag);               // 0x000c1380

    // ---- the lists, hit checks and lifecycle (CollisionManager.cpp)

    // Swaps the collision article of the instance drawn by `renderInstance` for article `article` of its model
    // (none for -1), remembering the original for Restart. False if no instance is drawn by it or the model has
    // no such article.
    bool SetCollisionArticle(CARP::Instance *renderInstance, uint32_t article);          // 0x000c2ff0

    // The instances whose spheres reach a segment, from the grid cells it crosses.
    void GetInstanceListGuts(WGridCellList *cells, InstanceList *out, const Coord4 *segment); // 0x000c31f0
    void GetInstanceList(InstanceList *out, const Coord4 *segment);                       // 0x000c3410
    // The instances near a point, each with the strips near it when `wantStrips` (and only those that have
    // some); `flat` ignores heights.
    void GetInstanceListGuts(WGridCellList *cells, InstanceList *out, const Coord3 *point, float radius,
                             bool wantStrips, bool flat);                                 // 0x000c43c0
    void GetInstanceList(InstanceList *out, const Coord3 *point, float radius, bool wantStrips,
                         bool flat);                                                      // 0x000c4510
    // The strips of one instance near a point (the vector new'd, NULL if none).
    InstanceListEntry* GetInstanceStripList(InstanceListEntry *result, WCollisionInstance *instance,
                                            const Coord3 *point, float radius, bool flat);    // 0x000c4120

    // The collision objects near a point or a segment, the cylinders and the boxes apart.
    void GetObjectListsGuts(WGridCellList *cells, ObjectList *cylinders, ObjectList *boxes, const Coord3 *point,
                            float radius);                                                // 0x000c34c0
    void GetObjectLists(ObjectList *cylinders, ObjectList *boxes, const Coord4 *segment); // 0x000c36a0
    void GetObjectLists(ObjectList *cylinders, ObjectList *boxes, const Coord3 *point, float radius);   // 0x000c37c0

    // The barriers of the listed instances near a point.
    void GetBarrierList(BarrierList *out, const InstanceList *instances, const Coord3 *point, float radius);   // 0x000c3880

    // Where a segment first hits the world, its barriers or its objects: true if it does, the hit in `info`.
    // StepCheckHitWorld checks a long segment in steps of 48, moving it along (it is left at the last step
    // checked); `step` is not used.
    // Both answer an int, 0 or 1 in the whole of EAX: callers test EAX (test eax, eax; jle), so a bool, which sets
    // only AL, would hand them whatever the upper bytes held.
    int CheckHitWorld(const Coord4 *segment, WorldCollisionInfo *info);                   // 0x000c3b40
    int StepCheckHitWorld(Coord4 *segment, float step);                                   // 0x000c4000

    // Whether a barrier hit went through a window pane; with `create`, records the pane's damage, raises an
    // EHitWindow event (`kind` 1: the event's flag) and answers true.
    bool CheckHitWindow(WorldCollisionInfo *info, bool create, int kind);                 // 0x000c4d70

    WCollisionMgr* Construct();                                                           // 0x000c5500
    void Destruct();                                                                      // 0x000c5460
    static void Init(UGroup *world);                                                      // 0x000c55b0
    static void Restart();                                                                // 0x000c5380
    static void Shutdown();                                                               // 0x000c56d0
};
static_assert(sizeof(WCollisionMgr) == 0x34, "WCollisionMgr is 52 bytes");
static_assert(offsetof(WCollisionMgr, windows) == 0x14 && offsetof(WCollisionMgr, barrierMask) == 0x2c,
              "WCollisionMgr layout");

// The manager (WCollisionMgr's static instance; NULL before Init and after Shutdown).
#define fgCollisionMgr (*(WCollisionMgr **)0x00239a70)

#endif // DRIVING_WORLD_COLLISIONMANAGER_H_
