#pragma fp_contract(off)

#include "WorldPos.h"
#include "CollisionManager.h"
#include "WorldMath.h"
#include "../engine/UMemory.hpp"
#include "../../helpers.h"

#include <stddef.h>
#include <bit>

// ---------------------------------------------------------------------------------------------------------------
// WWorldPos: the searches go to the collision manager (WCollisionMgr::FindFaceInCInst for each instance,
// GetInstanceList for the instances near a point).
// ---------------------------------------------------------------------------------------------------------------

#define SimStepCount I32_AT(0x00234e34)
#define ZeroVector (*(const Coord3 *)0x00243030)        // the game's zero vector, never written

// The face's unit normal (thiscall on the face; name unknown)
#define FUN_0005d3f0 ((void (__fastcall *)(const StripTriangle *, int, Coord3 *))0x0005d3f0)

static constexpr float kFlatFaceSize = 0.2f;
static constexpr float kNoFace = 1e38f;
static_assert(std::bit_cast<uint32_t>(kNoFace) == 0x7e967699, "the searches' starting distance");

// FUNC_AT(0x000d2fd0)
WWorldPos* WWorldPos::Construct() {
    face.corner[2].tag.type = 0;
    face.corner[2].tag.subType = 0;
    valid = 0;
    instance = NULL;
    article = NULL;
    lastQueryStep = 0;
    face.corner[0].Position() = ZeroVector;
    face.corner[1].Position() = ZeroVector;
    face.corner[2].Position() = ZeroVector;
    face.corner[2].tag.id = 0;
    return this;
}

// FUNC_AT(0x000d2f10)
void WWorldPos::MakeFaceAtPoint(const Coord3 *point) {
    face.corner[0].x = point->x;
    face.corner[1].x = point->x + kFlatFaceSize;
    face.corner[2].x = point->x;
    face.corner[0].y = point->y;
    face.corner[1].y = point->y;
    face.corner[2].y = point->y;
    face.corner[0].z = point->z;
    face.corner[1].z = point->z;
    face.corner[2].z = point->z + kFlatFaceSize;
    face.corner[2].tag.type = 1;
    face.corner[2].tag.subType = 0;
    face.corner[2].tag.id = 0;
    instance = fgCollisionMgr->instances;   // the first instance
    article = NULL;
    valid = 1;
}

// FUNC_AT(0x000d2f90)
double WWorldPos::HeightAtPoint(const Coord3 *point, bool unusedFlag) {
    (void)unusedFlag;
    if (!valid)
        return 0.0;
    Coord3 normal;
    FUN_0005d3f0(&face, 0, &normal);
    return WWorldMath::GetPlaneY(&normal, &face.corner[0].Position(), point);
}

// The candidate face and height live across the loop, as the original's stack slots do (only the two flag bytes
// are cleared before each search).
// FUNC_AT(0x000d3050)
bool WWorldPos::FindClosestFace(const InstanceList *instances, const Coord3 *point) {
    valid = 0;
    bool found = false;
    float nearest = kNoFace;
    StripTriangle candidate = {};   // the original leaves corner[0]'s count word as stack garbage
    float height;
    for (const InstanceListEntry *entry = instances->first; entry != instances->last; entry++) {
        candidate.corner[2].tag.type = 0;
        candidate.corner[2].tag.subType = 0;
        if (!fgCollisionMgr->FindFaceInCInst(point, entry, &candidate, &height))
            continue;
        found = true;
        if (height < nearest) {
            instance = entry->instance;
            article = entry->instance->article;
            face = candidate;
            valid = 1;
            nearest = height;
        }
    }
    return found;
}

// FUNC_AT(0x000d3110)
bool WWorldPos::FindClosestFace(const InstanceList *instances, const Coord4 *from, const Coord4 *to) {
    lastQueryStep = SimStepCount;
    valid = 0;
    alignas(16) MATRIX4 segmentSpace;
    if (!WWorldMath::MakeSegSpaceMatrix(from, to, &segmentSpace))
        return false;
    valid = 0;
    float nearest = kNoFace;
    StripTriangle candidate = {};   // the original leaves corner[0]'s count word as stack garbage
    float height;
    for (const InstanceListEntry *entry = instances->first; entry != instances->last; entry++) {
        candidate.corner[2].tag.type = 0;
        candidate.corner[2].tag.subType = 0;
        if (fgCollisionMgr->FindFaceInCInst(&segmentSpace, to, entry, &candidate, &height) && height < nearest) {
            instance = entry->instance;
            article = entry->instance->article;
            face = candidate;
            valid = 1;
            nearest = height;
        }
    }
    return false;
}

// The face is checked against the point whenever there is one (the check may have effects), but kept only with
// keepCurrent and an unchanged instance. The instance list's storage goes back to the fast pool at its capacity.
// FUNC_AT(0x000d31f0)
bool WWorldPos::FindClosestFace(const Coord3 *point, bool keepCurrent) {
    lastQueryStep = SimStepCount;
    if (keepCurrent && valid && (instance == NULL || article != instance->article))
        keepCurrent = false;
    if (valid && PointInTriangle2D(point, face.corner) && keepCurrent)
        return false;
    InstanceList nearby;
    nearby.first = NULL;
    nearby.last = NULL;
    nearby.end = NULL;
    fgCollisionMgr->GetInstanceList(&nearby, point, 0.0f, false, true);
    FindClosestFace(&nearby, point);
    if (nearby.first != NULL)
        UMemory::FastFree(nearby.first, unsigned(nearby.end - nearby.first) * sizeof(InstanceListEntry));
    return true;
}
