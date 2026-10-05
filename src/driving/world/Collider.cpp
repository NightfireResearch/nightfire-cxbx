#pragma fp_contract(off)

#include "Collider.h"

#include "CollisionManager.h"
#include "../engine/CoreFoundation.h"
#include "../engine/UMemory.hpp"
#include "../engine/PhysicsUtil.h"
#include "../platform/RealMath.h"

// ---------------------------------------------------------------------------------------------------------------
// WCollider and its vectors (0x000bd750..0x000be6f0), ported from the listings.
//
// The lists come from the collision manager (GetInstanceList, GetBarrierList, GetWorldNormal). The region round a
// collider that moves by a position and a radius is Util_FitColliderRegion's (0x000bd690, which takes its arguments
// in registers; its C++ core is called). The vectors' helpers are ColStl's (CollisionInstance.h): the game's copies
// of them, folded.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

// The 8-byte vectors' length_error throw the instance list's reserve calls (CollisionVectorXlen's twin)
#define PairVector_Xlen ((void (__fastcall *)(void *, int))0x000a8b90)

#define ZeroVector (*(const Coord3 *)0x00243030)   // the game's zero vector

namespace {

const uint32_t kMaxPairs = 0x1fffffff;           // max_size of a vector of 8-byte elements
const uint32_t kMaxBarriers = 0x6666666;         // and of 0x28-byte ones
const uint32_t kNearbyReserve = 100;             // the instance lists' first allocation
const uint32_t kBarrierReserve = 21;
constexpr float kRegionSlack = 1.1f;             // the region's radius over the collider's

// The fields both constructors clear before the first refresh
void ClearCollider(WCollider *collider) {
    collider->position = ZeroVector;
    collider->radius = 0.0f;
    collider->previousPosition = ZeroVector;
    collider->previousRadius = 0.0f;
    collider->regionCentre = ZeroVector;
    collider->regionRadius = 0.0f;
    collider->instances.first = NULL;
    collider->instances.last = NULL;
    collider->instances.end = NULL;
    collider->barriers.first = NULL;
    collider->barriers.last = NULL;
    collider->barriers.end = NULL;
    collider->barrierStamps.first = NULL;
    collider->barrierStamps.last = NULL;
    collider->barrierStamps.end = NULL;
}

}  // namespace

// ---- the vectors

// FUNC_AT(0x000bd910)
BarrierListEntry* BarrierUninitializedCopy(BarrierListEntry *first, BarrierListEntry *last, BarrierListEntry *dest) {
    return ColStl::UninitializedCopy(first, last, dest);
}

// FUNC_AT(0x000bd940)
ColliderStamp* ColliderStampList::Ucopy(ColliderStamp *first, ColliderStamp *last, ColliderStamp *dest) {
    return ColStl::UninitializedCopy(first, last, dest);
}

// FUNC_AT(0x000bd970)
BarrierListEntry* BarrierList::Ucopy(BarrierListEntry *first, BarrierListEntry *last, BarrierListEntry *dest) {
    return BarrierUninitializedCopy(first, last, dest);
}

// FUNC_AT(0x000bd9a0)
void BarrierList::Tidy() {
    ColStl::Tidy(this);
}

// FUNC_AT(0x000bd9f0)
void CollisionVectorXlen() {
    WORLD_UNTESTED("a collider vector's _Xlen");
    ThrowLengthError("vector<T> too long");
}

// FUNC_AT(0x000bda70)
void ColliderStampList::InsertN(ColliderStamp *where, uint32_t count, const ColliderStamp *value) {
    ColStl::InsertN(this, where, count, value, kMaxPairs, CollisionVectorXlen);
}

// FUNC_AT(0x000bde10)
void BarrierList::Reserve(uint32_t count) {
    if (count > kMaxBarriers) {
        CollisionVectorXlen();
        return;
    }
    if (Capacity() < count) {
        uint32_t bytes = count * sizeof(BarrierListEntry);
        BarrierListEntry *storage = static_cast<BarrierListEntry *>(UMemory::FastAlloc(bytes, "STL"));
        BarrierUninitializedCopy(first, last, storage);
        uint32_t size = Size();
        if (first != NULL)
            UMemory::FastFree(first, uint32_t(end - first) * sizeof(BarrierListEntry));
        end = storage + count;
        last = storage + size;
        first = storage;
    }
}

// FUNC_AT(0x000bdf60)
void InstanceList::Reserve(uint32_t count) {
    ColStl::Reserve(this, count, kMaxPairs, [this] {
        WORLD_UNTESTED("a collider instance list's _Xlen");
        PairVector_Xlen(this, 0);
    });
}

// FUNC_AT(0x000be070)
void ColliderStampList::Reserve(uint32_t count) {
    ColStl::Reserve(this, count, kMaxPairs, CollisionVectorXlen);
}

// FUNC_AT(0x000be260)
void ColliderStampList::PushBack(const ColliderStamp *value) {
    ColStl::PushBack(this, value, [this](ColliderStamp *where, uint32_t count, const ColliderStamp *what) {
        InsertN(where, count, what);
    });
}

// ---- WCollider

// FUNC_AT(0x000bd750)
bool WCollider::Validate() {
    if (!regionValid)
        return false;
    for (InstanceListEntry *entry = instances.first; entry != instances.last; entry++)
        if (entry->strips != NULL && entry->strips->article != entry->instance->article)
            return false;
    for (ColliderStamp *stamp = barrierStamps.first; stamp != barrierStamps.last; stamp++)
        if (stamp->article != *stamp->source)
            return false;
    return true;
}

// FUNC_AT(0x000bd7b0)
bool WCollider::InRegion(const Coord4 *sweep, uint32_t mask) {
    if (!Validate() || (collisionMask & mask) != mask)
        return false;
    float reach = regionRadius * regionRadius;
    if (!(VU0_v3distancesquare(&sweep[0], &regionCentre) < reach))
        return false;
    return VU0_v3distancesquare(&sweep[1], &regionCentre) < reach;
}

// Inside when the sphere is wholly inside the region (touching its edge is outside)
// FUNC_AT(0x000bd820)
bool WCollider::InRegion(const Coord3 *position, float radius, uint32_t mask) {
    if (!Validate() || (collisionMask & mask) != mask)
        return false;
    float slack = regionRadius - radius;
    if (slack < 0.0f)
        return false;
    float distanceSquared = VU0_v3distancesquare(position, &regionCentre);
    return double(slack) * slack > distanceSquared;
}

// FUNC_AT(0x000bd890)
bool WCollider::GetWorldNormal(const Coord4 *segment, WorldCollisionInfo *info) {
    if ((instances.first != NULL && instances.last - instances.first != 0) ||
        (barriers.first != NULL && barriers.last - barriers.first != 0))
        return fgCollisionMgr->GetWorldNormal(&instances, &barriers, segment, info,
                                              (collisionMask & kCollideInstances) != 0);
    return false;
}

// FUNC_AT(0x000bdd40)
void WCollider::Clear() {
    if (regionValid) {
        for (InstanceListEntry *entry = instances.first; entry != instances.last; entry++) {
            if (entry->strips != NULL) {
                ColStl::Tidy(entry->strips);   // the strips' vector destructor (0x0004f400)
                OperatorDelete(entry->strips);
                entry->strips = NULL;
            }
        }
        ColStl::Tidy(&instances);
        ColStl::Tidy(&barriers);
        ColStl::Tidy(&barrierStamps);
    }
    regionValid = false;
}

// FUNC_AT(0x000be180)
void WCollider::Destruct() {
    Clear();
    ColStl::Tidy(&barrierStamps);
    ColStl::Tidy(&barriers);
    ColStl::Tidy(&instances);
}

// FUNC_AT(0x000be2d0)
void WCollider::PrepareRegion() {
    if (collisionMask & kCollideInstances) {
        instances.Reserve(kNearbyReserve);
        fgCollisionMgr->GetInstanceList(&instances, &regionCentre, regionRadius, true, flat);
    }
    if (collisionMask & kCollideBarriers) {
        InstanceList nearby = {};
        nearby.Reserve(kNearbyReserve);
        fgCollisionMgr->GetInstanceList(&nearby, &regionCentre, regionRadius, false, flat);
        barriers.Reserve(kBarrierReserve);
        fgCollisionMgr->GetBarrierList(&barriers, &nearby, &regionCentre, regionRadius);
        barrierStamps.Reserve(barriers.Size());
        for (BarrierListEntry *barrier = barriers.first; barrier != barriers.last; barrier++) {
            ColliderStamp stamp = { barrier->instance->article, &barrier->instance->article };
            barrierStamps.PushBack(&stamp);
        }
        if (nearby.first != NULL)
            UMemory::FastFree(nearby.first, uint32_t(nearby.end - nearby.first) * sizeof(InstanceListEntry));
    }
    regionValid = true;
}

// The region round a sweep: its middle, reaching both ends, a little larger
// FUNC_AT(0x000be420)
void WCollider::Refresh(const Coord4 *sweep) {
    if (InRegion(sweep, collisionMask))
        return;
    Clear();
    VU0_v3add(&sweep[0], &sweep[1], &position);
    VU0_v4scale(&position, 0.5f, &position);
    float distance = vec3distance(&position, &sweep[0]);
    radius = distance;
    previousRadius = distance;
    previousPosition = position;
    regionCentre = position;
    regionRadius = distance * kRegionSlack;
    PrepareRegion();
}

// FUNC_AT(0x000be4b0)
void WCollider::Refresh(const Coord3 *position, float radius) {
    if (!InRegion(position, radius, collisionMask)) {
        bool hadRegion = regionValid;
        Clear();
        this->position = *position;
        this->radius = radius;
        // the position and the radius after it read as one Coord4, and so do the previous ones
        Util_FitColliderRegion(hadRegion, reinterpret_cast<const Coord4 *>(&this->position),
                               reinterpret_cast<Coord4 *>(&regionCentre), &regionRadius, radius,
                               reinterpret_cast<const Coord4 *>(&previousPosition));
        PrepareRegion();
    }
    previousPosition = *position;
    previousRadius = radius;
}

// FUNC_AT(0x000be540)
WCollider* WCollider::Construct(const Coord3 *position, float radius, bool flat, uint32_t mask) {
    ClearCollider(this);
    this->flat = flat;
    regionValid = false;
    collisionMask = mask;
    Refresh(position, radius);
    return this;
}

// FUNC_AT(0x000be620)
WCollider* WCollider::Construct(const Coord4 *sweep, uint32_t mask) {
    ClearCollider(this);
    regionValid = false;
    flat = false;
    collisionMask = mask;
    Refresh(sweep);
    return this;
}
