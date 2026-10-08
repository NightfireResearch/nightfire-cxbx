#ifndef DRIVING_CAMERA_WORLDCAMERA_H_
#define DRIVING_CAMERA_WORLDCAMERA_H_

// ---------------------------------------------------------------------------------------------------------------
// The world's views:
//   RRenderWorldCamera (0x4c bytes, vtable 0x00191b2c): an RViewCamera that draws the world through its camera -
//     culling the track (through WRender), the actors, the scene objects and the gallery's canvases, then the
//     static world, the cars and deferred scene objects, the effects, the post-processing and the headlight.
//   RPlayerViewCamera (0x4c bytes, vtable 0x00191910): a player's view; ConfigureView sets the culling up.
// RWorldCamera's methods (Camera.h has the class) are ported here too, with ActActorDatabase::DrawActorWeapons,
// which DoRender calls. See WorldCamera.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Camera.h"

struct CachedDrawInfo;

// What the culling steps through: each module's next item (its key, or -1 at the end) with its bounding sphere,
// the curtain test's margin and the far-plane test, and where it is told whether the item is culled
typedef int (*CullNextItem)(Coord4 *sphere, float *height, bool *checkFar, float *farScale);
typedef void (*CullSetItem)(int item, bool culled, float distance);

class RRenderWorldCamera : public RViewCamera {
public:
    RRenderWorldCamera* Construct(RCamera *camera);                                             // 0x0008c8e0
    void Destruct();                                                                            // 0x0008c950
    RRenderWorldCamera* Delete(unsigned flags);     // vtable slot 0                            // 0x0008ccf0
    // The "Enable headlights" debug variable
    static void LoadAttributes();                                                               // 0x0008c960

    void PreRender();                   // vtable slot 2                                        // 0x0008c990
    // Each item in the frustum and not behind a curtain is kept; the third argument is not used
    void CullModule(CullNextItem next, CullSetItem set, bool unused);                           // 0x0008c9b0
    CachedDrawInfo* PerformCulling();   // the track's draws                                    // 0x0008ca90
    void DrawStaticWorldGeometry(CachedDrawInfo *list);                                         // 0x0008cb30
    void DrawFinalStaticWorldGeometry(CachedDrawInfo *list);                                    // 0x0008cb90
    void DrawPostProcessingEffects();                                                           // 0x0008cbf0
    void DrawVehiclesAndDeferredSceneObjects();                                                 // 0x0008cc70
    void DrawTyreTracks();                                                                      // 0x0008cd10
    void DrawBulletStreaks();           // and the tracers, flashes, missiles and grenades       // 0x0008cd60
    void AddPlayerHeadlight();                                                                  // 0x0008ce50
    void DrawEffects();                                                                         // 0x0008cf10
    void DoRender();                    // vtable slot 4                                        // 0x0008cfa0
};
static_assert(sizeof(RRenderWorldCamera) == 0x4c, "RRenderWorldCamera is 76 bytes");

class RPlayerViewCamera : public RRenderWorldCamera {
public:
    RPlayerViewCamera* Construct(RCamera *camera);                                              // 0x0008a3f0
    void Destruct();                                                                            // 0x0008a410
    RPlayerViewCamera* Delete(unsigned flags);      // vtable slot 0                            // 0x0008a4d0
    // The 2D culling frustum from the camera; vtable slot 5
    void ConfigureView();                                                                       // 0x0008a420
};
static_assert(sizeof(RPlayerViewCamera) == 0x4c, "RPlayerViewCamera is 76 bytes");

// FUN_000970a0 (the name is ours): the largest whole number not above the value, by __ftol2's truncation
int FloorToInt(float value);                                                                    // 0x000970a0

// FUN_0008d120 (the name is ours): the point's x and z through the matrix's rows 0 and 2, less row 3 -
// RRenderWorldCulling::QuadtreeFrustrumCheck2d's
void TransformPointXZ(const Coord3 *point, const MATRIX4 *matrix, Coord4 *out);                 // 0x0008d120

// ---- the action engine's actors as the world's view draws them (not ported; only what is read here)

class ActActor {
public:
    uint8_t unknown00[0x40];
    uint8_t culled;                     // +0x40 ActActorDatabase::SetActorCull's
};

struct ActActorNode {                   // a std::list node
    ActActorNode *next;
    ActActorNode *prev;
    ActActor *actor;
};

class ActActorDatabase {
public:
    uint32_t allocator;
    ActActorNode *actors;               // +0x04 the list's head
    uint32_t actorCount;

    // ActActor::DrawWeapons for every actor not culled (Ghidra files it under RRenderWorldCamera; ECX is the
    // database)
    void DrawActorWeapons(RViewCamera *view, bool unknown);                                     // 0x00013020
};

#endif // DRIVING_CAMERA_WORLDCAMERA_H_
