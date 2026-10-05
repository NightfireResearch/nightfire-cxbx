#ifndef DRIVING_WORLD_COLLIDER_H_
#define DRIVING_WORLD_COLLIDER_H_

#include <stddef.h>
#include <stdint.h>

#include "CollisionInstance.h"
#include "CollisionTypes.h"

// ---------------------------------------------------------------------------------------------------------------
// WCollider (0x68): a moving thing's cache of what it can hit. It keeps a region round itself (a sphere a little
// larger than the thing, or stretched along its last move) and, for that region, the collision instances and the
// barrier segments the collision manager lists, so the per-frame queries test only those. A refresh rebuilds the
// lists only when the thing leaves its region or the lists are out of date (an instance they came from changed).
// See Collider.cpp.
// ---------------------------------------------------------------------------------------------------------------

// What a barrier list was made from: an instance's article then, and where the instance keeps it
struct ColliderStamp {
    CollisionArticle *article;
    CollisionArticle *const *source;
};

// The barrier stamps' vector (the instance and barrier lists are CollisionTypes.h's; their reserve and copies
// here are each one of the game's compiled copies).
struct ColliderStampList : ColVector<ColliderStamp> {
    void Reserve(uint32_t count);                                                       // 0x000be070
    void PushBack(const ColliderStamp *value);                                          // 0x000be260
    void InsertN(ColliderStamp *where, uint32_t count, const ColliderStamp *value);     // 0x000bda70
    ColliderStamp* Ucopy(ColliderStamp *first, ColliderStamp *last, ColliderStamp *dest);   // 0x000bd940
};

// The collider vectors' length_error throw (0x000bd9f0; the 8-byte vectors call it too, with the vector in ECX,
// which it does not read)
void CollisionVectorXlen();

// uninitialized_copy of barrier list entries (0x000bd910): answers the end of the copy
BarrierListEntry* BarrierUninitializedCopy(BarrierListEntry *first, BarrierListEntry *last, BarrierListEntry *dest);

class WCollider {
public:
    enum CollisionMask : uint32_t {
        kCollideBarriers = 0x4,    // list the barrier segments round the region
        kCollideInstances = 0x8,   // list the collision instances in the region
    };

    Coord3 position;               // +0x00
    float radius;                  // +0x0c
    Coord3 previousPosition;       // +0x10
    float previousRadius;          // +0x1c
    Coord3 regionCentre;           // +0x20
    float regionRadius;            // +0x2c
    InstanceList instances;        // +0x30 with the strips near the region
    BarrierList barriers;          // +0x40
    ColliderStampList barrierStamps;   // +0x50 one per barrier
    bool regionValid;              // +0x60 the lists are built
    bool flat;                     // +0x61 the instance list ignores heights (WCollisionMgr::GetInstanceList)
    uint8_t unknown62[2];
    uint32_t collisionMask;        // +0x64 CollisionMask

    WCollider* Construct(const Coord3 *position, float radius, bool flat, uint32_t mask);   // 0x000be540
    WCollider* Construct(const Coord4 *sweep, uint32_t mask);                    // 0x000be620
    void Destruct();                                                             // 0x000be180

    bool Validate();                                                             // 0x000bd750
    bool InRegion(const Coord4 *sweep, uint32_t mask);                           // 0x000bd7b0
    bool InRegion(const Coord3 *position, float radius, uint32_t mask);          // 0x000bd820
    // The manager's GetWorldNormal over the lists: the nearer of the barrier and ground hits along a segment
    bool GetWorldNormal(const Coord4 *segment, WorldCollisionInfo *info);        // 0x000bd890
    void Clear();                                                                // 0x000bdd40
    void PrepareRegion();                                                        // 0x000be2d0
    void Refresh(const Coord4 *sweep);                                           // 0x000be420
    void Refresh(const Coord3 *position, float radius);                          // 0x000be4b0
};
static_assert(sizeof(WCollider) == 0x68, "a collider is 0x68 bytes");
static_assert(offsetof(WCollider, instances) == 0x30, "WCollider::instances");
static_assert(offsetof(WCollider, regionValid) == 0x60, "WCollider::regionValid");

#endif // DRIVING_WORLD_COLLIDER_H_
