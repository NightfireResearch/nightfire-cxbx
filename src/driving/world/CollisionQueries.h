#ifndef DRIVING_WORLD_COLLISIONQUERIES_H_
#define DRIVING_WORLD_COLLISIONQUERIES_H_

// ---------------------------------------------------------------------------------------------------------------
// WCollisionMgr's queries (0x000bedd0..0x000c2ff0, CollisionQueries.cpp): the height and normal of the ground under
// a point, the face of a collision instance's triangle strips under a point or along a segment, a segment against
// the barriers, the collision objects' cylinders and boxes, the closest of two hits - and the compiled copies of the
// C++ library's containers the collision manager's lists and maps are made of (the linker put them here, between
// the queries).
//
// The methods are declared with their classes: WCollisionMgr and its maps in CollisionManager.h, the vectors, the
// grid cell iterator and the strip triangle in CollisionTypes.h. This header has the free helpers.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "CollisionManager.h"

// The warning beside a provisional port here: code no shipped data reaches (the STL's "too long" and "invalid
// iterator" throws), ported from the listing without a test. Once.
inline void CollisionQueriesUntested(const char *what) {
    printf("[collision] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what "
           "it computes against the original.\n", what);
    fflush(stdout);
}

#define COLLISION_QUERIES_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            CollisionQueriesUntested(what); \
        } \
    } while (0)

// The library helpers of the collision vectors (cdecl; some callers push a trailing iterator tag nobody reads).
void** PointerUninitializedCopy(void *const *first, void *const *last, void **dest);           // 0x000bfeb0
void BarrierFill(BarrierListEntry *first, BarrierListEntry *last, const BarrierListEntry *value);   // 0x000c0d30
void BarrierUninitializedFill(BarrierListEntry *first, uint32_t count, const BarrierListEntry *value);   // 0x000c0da0
BarrierListEntry** BarrierCopyBackward(BarrierListEntry **result, BarrierListEntry *first, BarrierListEntry *last,
                                       BarrierListEntry *dest);                                  // 0x000c0d60
BarrierListEntry** BarrierCopyBackwardTagged(BarrierListEntry **result, BarrierListEntry *first,
                                             BarrierListEntry *last, BarrierListEntry *dest);    // 0x000bfe70
void InstanceUninitializedFill(InstanceListEntry *first, uint32_t count, const InstanceListEntry *value);   // 0x000c0dd0

// The face of a strip's triangle carried by the inverse of `matrix` into `face` (corner[1].flags and corner[2].tag
// copied from the strip).
void MakeStripFace(StripTriangle *face, const StripVertex *triangle, const MATRIX4 *matrix);     // 0x000beeb0

// A triangle's unit normal, pointing up (y at most 0.9999; (0, 1, 0) for a degenerate one). 0x000beff0 takes the
// triangle in EAX and the normal in ESI: FUN_000beff0 is the adapter.
void StripTriangleNormal(const StripVertex *triangle, Coord3 *normal);
void FUN_000beff0();

#endif // DRIVING_WORLD_COLLISIONQUERIES_H_
