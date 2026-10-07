#include "CollisionQueries.h"

#include "Collider.h"                      // the colliders' vector helpers, folded with these vectors'
#include "World.h"
#include "WorldMath.h"
#include "WorldPos.h"
#include "../../helpers.h"
#include "../data/AttributeContainers.h"   // the 8-byte vectors' helpers, folded with the attribute store's
#include "../engine/CoreFoundation.h"      // NullFunction, ThrowLengthError
#include "../engine/OBB.h"
#include "../engine/PhysicsUtil.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"

#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// WCollisionMgr's queries and the collision lists' library code. See CollisionQueries.h.
//
// The x87 code is ported bit for bit: what the original keeps on the x87 stack is computed in double in its order,
// rounded to float where it stores a float; comparisons keep their NaN behaviour (a condition the original jumps
// over on NaN is written so that NaN fails it). Two things in the originals cannot be reproduced and are noted
// where they happen: GetOBBObjectIntersection can read a stack slot nothing wrote (a segment through a corner of the
// box), and WorldCollisionInfo's constructor (not ported, called) leaves the segment fields as its stack had them.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's, not ported yet

#define WWorldPos_GetFaceNormal ((void (__fastcall *)(const WWorldPos *, int, float *))0x0005d3f0)
#define WorldCollisionInfo_Construct ((WorldCollisionInfo *(__fastcall *)(WorldCollisionInfo *, int))0x0001d9f0)
#define Simulation_GetScratchPadFreeZone ((uint8_t *(__fastcall *)(void *, int))0x000b2820)
#define BarrierList_Size ((uint32_t (__fastcall *)(const BarrierList *, int))0x00023f20)
#define InstanceList_Xlen ((void (*)(void))0x000a8b90)
#define InstanceList_Deallocate ((void (__fastcall *)(InstanceList *, int, InstanceListEntry *, uint32_t))0x000a87c0)
#define PointerVector_Xlen ((void (*)(void))0x000b13a0)
#define CellList_Xlen ((void (*)(void))0x00034e50)
#define PointerVector_Deallocate ((void (__fastcall *)(void *, int, void **, uint32_t))0x000ad630)
#define PointerUninitializedFill ((void (*)(void **, uint32_t, void *const *))0x000b2ec0)
#define PointerCopyBackward ((void ***(*)(void ***, void **, void **, void **))0x000b2e40)
#define PointerFill ((void (*)(void **, void **, void *const *))0x0004f3e0)
#define WindowPaneMin ((WindowPaneNode *(*)(WindowPaneNode *))0x000a87a0)
#define WindowPaneIncrement ((void (__fastcall *)(WindowPaneNode **, int))0x000a8a80)
#define ArticleMapDecrement ((void (__fastcall *)(ArticleMapNode **, int))0x00126bd0)

// ---- globals

#define Sim ((void *)0x00233ff0)                            // the Simulation
#define SimTimeStep FLOAT_AT(0x00234e30)                    // the simulation's step, in seconds (as its users read it)
#define LargeStripScratch ((StripVertex *)0x00239a80)       // FindFaceInTriStrip's vertices beyond the scratch pad's
#define WorldOrigin ((const Coord3 *)0x00243030)            // a zero vector the game keeps

namespace {

// A scene object's velocity (its vtable's slot 13).
const Coord3 *SceneObjectVelocity(void *sceneObject) {
    typedef const Coord3 *(__fastcall *VelocityFn)(void *, int);
    return reinterpret_cast<VelocityFn *>(*static_cast<void **>(sceneObject))[13](sceneObject, 0);
}

// The trig functions answer FSIN/FCOS's result unrounded on the x87 stack; SurfaceBumpHeight adds the cosine as it
// is, so it is read through a declaration that answers double (both come back in ST0).
double CosTurnsUnrounded(float turns) {
    typedef double (*TurnsFn)(float);
    return reinterpret_cast<TurnsFn>(&cos_fractionalangle)(turns);
}

// The cheap distance the barrier and cylinder searches rank hits by: the larger of |dx| and |dz| plus a quarter of
// the smaller (the smaller is |dx| unless |dx| > |dz|).
double ApproximateDistance(double dx, double dz) {
    double ax = fabs(dx);
    double az = fabs(dz);
    if (ax > az)
        return ax + az * 0.25;
    return az + ax * 0.25;
}

constexpr float kFar = 1e38f;               // the searches' first best distance
constexpr float kNoHitDistance = 9.9e31f;   // ClosestCollisionInfo's distance of an info without a hit
static_assert(std::bit_cast<uint32_t>(kFar) == 0x7e967699, "1e38");
static_assert(std::bit_cast<uint32_t>(kNoHitDistance) == 0x749c31c8, "9.9e31");

// A strip's sphere as the segment helpers read it: its centre (the fourth word, its radius and vertex offset, is not
// read).
const Coord4 *StripCentre(const CollisionStrip *strip) {
    return reinterpret_cast<const Coord4 *>(&strip->centre);
}

constexpr int kScratchTriangles = 180;      // the most triangles whose vertices fit the scratch pad (+0x3b0)
constexpr uint32_t kScratchOffset = 0x3b0;

}  // namespace

// =============================================================================================================
// The queries
// =============================================================================================================

// FUNC_AT(0x000bedd0)
double WCollisionMgr::SurfaceBumpHeight(const Coord3 *point, const uint8_t *faceType) {
    float frequency = 0.0f;   // waves per unit, in turns
    float amplitude = 0.0f;
    switch (*faceType) {
    case 2:
    case 3:
    case 5:
    case 10:
        frequency = 2.0f;
        amplitude = 0.01f;
        break;
    case 4:
    case 7:
        frequency = 2.0f;
        amplitude = 0.015f;
        break;
    case 6:
        frequency = 0.0f;
        amplitude = -10.0f;
        break;
    }
    static_assert(std::bit_cast<uint32_t>(0.01f) == 0x3c23d70a && std::bit_cast<uint32_t>(0.015f) == 0x3c75c28f,
                  "the bump amplitudes");
    if (amplitude == 0.0f)
        return 0.0;
    float alongZ = sin_fractionalangle(frequency * point->z);
    return (CosTurnsUnrounded(frequency * point->x) + alongZ) * amplitude;
}

// FUNC_AT(0x000beeb0)
void MakeStripFace(StripTriangle *face, const StripVertex *triangle, const MATRIX4 *matrix) {
    MATRIX4 inverse = *matrix;
    OrthoInverse(&inverse);
    // the tag and the strip's flags first: the transforms keep the fourth words
    face->corner[2].tag.type = triangle[2].tag.type;
    face->corner[2].tag.subType = triangle[2].tag.subType;
    face->corner[2].tag.id = triangle[2].tag.id;
    face->corner[1].flags = triangle[1].flags;
    VU0_MATRIX4_vect3mult(&triangle[0], &inverse, &face->corner[0]);
    VU0_MATRIX4_vect3mult(&triangle[1], &inverse, &face->corner[1]);
    VU0_MATRIX4_vect3mult(&triangle[2], &inverse, &face->corner[2]);
}

// FUNC_AT(0x000bef30)
const StripVertex* WCollisionMgr::FindStripTriangle(const Coord3 *point, const StripVertex *const *strip) {
    const StripVertex *vertex = *strip;   // each triangle is three vertices from here
    uint32_t stripFlags = vertex[1].flags;
    int triangles = vertex[0].count - 2;
    if (stripFlags & kStripEitherWinding) {
        for (int i = 0; i < triangles; i++, vertex++) {
            uint8_t subType = vertex[2].tag.subType;
            if (!(subType & kFaceIgnored) && PointInTriangle2D(point, vertex) && !(barrierMask & subType))
                return vertex;
        }
        return NULL;
    }
    bool clockwise = (stripFlags & kStripStartsClockwise) != 0;
    for (int i = 0; i < triangles; i++, vertex++, clockwise = !clockwise) {
        if (vertex[2].tag.subType & kFaceIgnored)
            continue;
        bool inside = clockwise ? PointInTriangle2DClockwise(point, vertex) : PointInTriangle2DCounter(point, vertex);
        if (inside && !(vertex[2].tag.subType & barrierMask))
            return vertex;
    }
    return NULL;
}

void StripTriangleNormal(const StripVertex *triangle, Coord3 *normal) {
    float edge1[3] = { triangle[1].x - triangle[0].x, triangle[1].y - triangle[0].y, triangle[1].z - triangle[0].z };
    float edge2[3] = { triangle[0].x - triangle[2].x, triangle[0].y - triangle[2].y, triangle[0].z - triangle[2].z };
    float cross[3];
    v3crossprod(edge1, edge2, cross);
    if (cross[0] == 0.0f && cross[1] == 0.0f && cross[2] == 0.0f) {
        normal->x = 0.0f;
        normal->y = 1.0f;
        normal->z = 0.0f;
    } else {
        v3unit(cross, normal);
    }
    if (normal->y < 0.0f) {
        normal->x = -normal->x;
        normal->y = -normal->y;
        normal->z = -normal->z;
    }
    if (normal->y >= 0.9999f)
        normal->y = 0.9999f;
    static_assert(std::bit_cast<uint32_t>(0.9999f) == 0x3f7ff972, "the steepest normal");
}

// FindFaceInTriStrip's helper: the triangle in EAX, the normal in ESI; EAX, ECX and EDX not kept (the original's
// calls do not keep them either).
// AUTOLTCG
__declspec(naked) void FUN_000beff0() {
    __asm {
        push esi
        push eax
        call StripTriangleNormal
        add esp, 8
        ret
    }
}

// FUNC_AT(0x000bf0e0)
const StripVertex* WCollisionMgr::FindFaceInTriStrip(const MATRIX4 *matrix, const Coord3 *point,
                                                     const StripVertex *const *strip, float *height) {
    const StripVertex *vertex = *strip;
    int triangles = vertex[0].count - 2;
    StripVertex *carried;   // the strip's vertices carried by the matrix
    if (triangles < kScratchTriangles)
        carried = reinterpret_cast<StripVertex *>(Simulation_GetScratchPadFreeZone(Sim, 0) + kScratchOffset);
    else
        carried = LargeStripScratch;
    const StripVertex *best = NULL;
    float bestHeight = kFar;
    VU0_MATRIX4_vect3mult(&vertex[0], matrix, &carried[0]);
    VU0_MATRIX4_vect3mult(&vertex[1], matrix, &carried[1]);
    for (int i = 0; i < triangles; i++, vertex++) {
        VU0_MATRIX4_vect3mult(&vertex[2], matrix, &carried[i + 2]);
        const StripVertex *triangle = &carried[i];
        if (!PointInTriangle2D(point, triangle))
            continue;
        Coord3 normal;
        StripTriangleNormal(triangle, &normal);
        float above = float(point->y - WWorldMath::GetPlaneY(&normal, &triangle[2].Position(), point));
        *height = above;
        if (above < bestHeight && above > -1.0f && !(barrierMask & vertex[2].tag.subType)) {
            best = vertex;
            bestHeight = above;
        }
    }
    *height = bestHeight;
    return best;
}

// FUNC_AT(0x000bf210)
bool WCollisionMgr::GetWorldHeightAtPoint(const Coord3 *point, float *height, bool unused) {
    (void)unused;
    WWorldPos position;
    position.Construct();
    position.FindClosestFace(point, true);
    if (position.valid) {
        Coord3 normal;
        WWorldPos_GetFaceNormal(&position, 0, &normal.x);
        *height = float(WWorldMath::GetPlaneY(&normal, &position.face.corner[0].Position(), point));
        NullFunction();   // ~WWorldPos
        return true;
    }
    NullFunction();
    return false;
}

// FUNC_AT(0x000bf2d0)
bool WCollisionMgr::GetGroundCollision(const Coord4 *segment, WWorldPos *position, WorldCollisionInfo *info) {
    info->hitType = kHitNone;
    position->FindClosestFace(reinterpret_cast<const Coord3 *>(&segment[1]), true);
    if (position->valid)
        return false;
    info->normal.y = 1.0f;
    info->normal.z = 0.0f;
    info->normal.x = 0.0f;
    info->normal.w = 1.0f;
    float t;
    if (!WWorldMath::IntersectSegPlane(&segment[0], &segment[1], &position->face.corner[0].Position(), &info->normal,
                                       &info->point, &t))
        return false;
    info->hitType = kHitWorld;
    info->faceType = position->face.corner[2].tag.type;
    info->faceSubType = position->face.corner[2].tag.subType;
    info->faceInstance = position->instance;
    Coord4 toStart;
    VU0_v4sub(segment, info, &toStart);
    if (v3dotprod(&info->normal, &toStart) < 0.0f)
        VU0_v3negate(&info->normal);
    return true;
}

// FUNC_AT(0x000bf3a0)
void WCollisionMgr::ClosestCollisionInfo(const Coord3 *point, const WorldCollisionInfo *a,
                                         const WorldCollisionInfo *b, WorldCollisionInfo *out) {
    if (!a->hitType && !b->hitType)
        return;
    float distanceA = kNoHitDistance;
    float distanceB = kNoHitDistance;
    if (a->hitType)
        distanceA = VU0_v3distancesquare(point, a);
    if (b->hitType)
        distanceB = VU0_v3distancesquare(point, b);
    memcpy(out, distanceA < distanceB ? a : b, sizeof(WorldCollisionInfo));
}

// FUNC_AT(0x000bf440)
WindowPaneNode* WindowPaneMap::Max(WindowPaneNode *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}

// FUNC_AT(0x000bf460)
WindowMapNode* WindowMapMin(WindowMapNode *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

// FUNC_AT(0x000bf480)
float StripTriangle::LowestY() const {
    float lowest = corner[0].y < corner[1].y ? corner[0].y : corner[1].y;
    return lowest < corner[2].y ? lowest : corner[2].y;
}

// FUNC_AT(0x000bf4c0)
bool WCollisionMgr::FindFaceInCInstStrips(const Coord3 *point, WCollisionInstance *instance, StripTriangle *face, float *height) {
    MATRIX4 frame;
    instance->MakeMatrix(&frame, true);
    Coord4 local;
    VU0_MATRIX4_vect3mult(point, &frame, &local);
    const CollisionArticle *article = instance->article;
    if (article == NULL)
        return false;
    // inside the instance's box in x and z (NaN passes)
    if (local.x > instance->halfSizeX || -instance->halfSizeX > local.x)
        return false;
    if (local.z > instance->halfSizeZ || -instance->halfSizeZ > local.z)
        return false;

    const StripVertex *best = NULL;
    float bestHeight = kFar;
    const CollisionStrip *strip = article->Strips();
    for (int i = 0; i < article->stripCount; i++, strip++) {
        Coord4 toCentre;
        VU0_v4sub(&strip->centre, &local, &toCentre);
        double radius = strip->radius * 0.0625;
        if (!(radius * radius > double(toCentre.z) * toCentre.z + double(toCentre.x) * toCentre.x))
            continue;
        const StripVertex *vertices = article->Vertices(strip);
        const StripVertex *triangle = FindStripTriangle(reinterpret_cast<const Coord3 *>(&local), &vertices);
        if (triangle == NULL)
            continue;
        double lowest = reinterpret_cast<const StripTriangle *>(triangle)->LowestY();
        double above;
        if (instance->flags & kInstanceFacesAbove)
            above = lowest - (double(local.y) - 0.5);
        else
            above = (double(local.y) + 0.5) - lowest;
        if (above > 0.0 && above < bestHeight) {
            bestHeight = float(above);
            *height = float(above);
            best = triangle;
        }
    }
    if (best == NULL)
        return false;
    MakeStripFace(face, best, &frame);
    return true;
}

// FUNC_AT(0x000bf680)
bool WCollisionMgr::FindFaceInCInstStrips(const MATRIX4 *segmentFrame, const Coord4 *end, WCollisionInstance *instance, StripTriangle *face, float *height) {
    MATRIX4 instanceFrame;
    instance->MakeMatrix(&instanceFrame, true);
    MATRIX4 instanceInverse = instanceFrame;
    OrthoInverse(&instanceInverse);
    MATRIX4 segmentInverse = *segmentFrame;
    OrthoInverse(&segmentInverse);
    MATRIX4 toSegment;   // the instance's frame into the segment's
    VU0_MATRIX4_mult(&toSegment, &instanceInverse, &segmentInverse);
    const CollisionArticle *article = instance->article;
    if (article == NULL)
        return false;

    const StripVertex *best = NULL;
    float bestHeight = kFar;
    Coord4 start, finish, direction;
    VU0_MATRIX4_vect3mult(segmentFrame->mtx[3], &instanceFrame, &start);
    VU0_MATRIX4_vect3mult(end, &instanceFrame, &finish);
    VU0_v4sub(&finish, &start, &direction);
    float inverseLengthSquared = float(
        1.0 / ((double(direction.x) * direction.x + double(direction.y) * direction.y) +
               double(direction.z) * direction.z));

    const CollisionStrip *strip = article->Strips();
    for (int i = 0; i < article->stripCount; i++, strip++) {
        float radius = strip->radius * 0.0625f;
        Coord4 nearest, offset;
        Util_NearestPointOnSegment(StripCentre(strip), &start, inverseLengthSquared, &direction, &nearest);
        VU0_v4sub(&strip->centre, &nearest, &offset);
        if (!(double(radius) * radius >
              (double(offset.z) * offset.z + double(offset.y) * offset.y) + double(offset.x) * offset.x))
            continue;
        const StripVertex *vertices = article->Vertices(strip);
        float above;
        const StripVertex *triangle = FindFaceInTriStrip(&toSegment, WorldOrigin, &vertices, &above);
        if (triangle == NULL)
            continue;
        double lifted = double(above) + 0.5;
        if (lifted > -1.0 && lifted < bestHeight) {
            bestHeight = float(lifted);
            *height = float(lifted);
            best = triangle;
        }
    }
    if (best == NULL)
        return false;
    MakeStripFace(face, best, &instanceFrame);
    return true;
}

// FUNC_AT(0x000bf8c0)
bool WCollisionMgr::GetOBBObjectIntersection(const Coord4 *segment, const MATRIX4 *matrix,
                                             const Coord4 *halfExtents, Coord4 *hit) {
    float ex = halfExtents->x;
    float ey = halfExtents->y;
    float ez = halfExtents->z;
    // the box's diagonal quad (top front edge, bottom back edge), its first corner again at the end: four edges
    const Coord4 quad[5] = {
        { -ex, ey, ez, 1.0f }, { ex, ey, ez, 1.0f }, { ex, -ey, -ez, 1.0f }, { -ex, -ey, -ez, 1.0f },
        { -ex, ey, ez, 1.0f },
    };
    MATRIX4 inverse;
    MatrixCopy(matrix, &inverse);
    OrthoInverse(&inverse);
    Coord4 local[2];   // the segment in the box's frame
    VU0_MATRIX4_vect3mult(&segment[0], &inverse, &local[0]);
    VU0_MATRIX4_vect3mult(&segment[1], &inverse, &local[1]);

    // The original's stack slots, shared as it shares them: slot[0] is the top face's hit and later the second
    // candidate carried back to the world, slot[1..4] the four edges' hits; low is the bottom face's hit and later
    // the first candidate carried back. Slot 0 is cleared here, where the original leaves whatever was there:
    // a segment through a corner of the quad (three edges crossed) reads it without the plane test having run.
    Coord4 slot[5];
    Coord4 low;
    slot[0] = Coord4{};

    bool crossesTop = (local[0].y > ey && local[1].y < ey) || (local[1].y > ey && local[0].y < ey);
    bool crossesBottom = (local[0].y < -ey && local[1].y > -ey) || (local[1].y < -ey && local[0].y > -ey);
    if (crossesTop || crossesBottom) {
        const Coord4 up = { 0.0f, 1.0f, 0.0f, 1.0f };
        float t;
        bool throughTop = WWorldMath::IntersectSegPlane(&local[0], &local[1], reinterpret_cast<const Coord3 *>(&quad[0]),
                                                        &up, &slot[0], &t);
        bool throughBottom = WWorldMath::IntersectSegPlane(&local[0], &local[1],
                                                           reinterpret_cast<const Coord3 *>(&quad[2]), &up, &low, &t);
        const Coord4 *face = NULL;
        if (throughTop && throughBottom)
            face = fabs(double(slot[0].y) - local[0].y) < fabs(double(low.y) - local[0].y) ? &slot[0] : &low;
        else if (throughTop)
            face = &slot[0];
        else if (throughBottom)
            face = &low;
        if (face != NULL && fabs(face->x) < ex && fabs(face->z) < ez) {
            VU0_MATRIX4_vect3mult(face, matrix, hit);
            return true;
        }
    }

    unsigned crossed = 0;
    if (FasterSegmentIntersect(local, &quad[0], &slot[1]))
        crossed = 1;
    if (FasterSegmentIntersect(local, &quad[1], &slot[2]))
        crossed |= 2;
    if (FasterSegmentIntersect(local, &quad[2], &slot[3]))
        crossed |= 4;
    if (FasterSegmentIntersect(local, &quad[3], &slot[4]))
        crossed |= 8;
    if (crossed == 0)
        return false;

    // By the edges crossed: the edge whose hit to take, or the two to take the nearer of (the original's table at
    // 0x001c9f68; entry 0 is never read, and a first edge of -1 reads slot 0)
    static const int8_t kEdgeChoice[16][2] = {
        { -1, 0 }, { 0, -1 }, { 1, -1 }, { 0, 1 }, { 2, -1 }, { 0, 2 }, { 1, 2 }, { -1, 0 },
        { 3, -1 }, { 0, 3 }, { 1, 3 }, { -1, 0 }, { 2, 3 }, { -1, 0 }, { -1, 0 }, { -1, 0 },
    };
    const Coord4 *first = &slot[kEdgeChoice[crossed][0] + 1];
    int secondEdge = kEdgeChoice[crossed][1];
    Coord4 chosen;
    if (secondEdge == -1) {
        chosen = *first;
    } else {
        const Coord4 *second = &slot[secondEdge + 1];
        VU0_MATRIX4_vect3mult(first, matrix, &low);
        VU0_MATRIX4_vect3mult(second, matrix, &slot[0]);
        float firstDistance = VU0_v3distancesquare(segment, &low);
        chosen = VU0_v3distancesquare(segment, &slot[0]) > firstDistance ? *first : *second;
    }
    if (!(chosen.y >= 0.0f))
        return false;
    if (!(double(ey) + ey > chosen.y))
        return false;
    VU0_MATRIX4_vect3mult(&chosen, matrix, hit);
    hit->w = 1.0f;
    return true;
}

namespace {

// A barrier hit's tag rides in the fourth word of segmentStart (the barrier list entry's).
FaceTag HitBarrierTag(const WorldCollisionInfo *info) {
    FaceTag tag;
    memcpy(&tag, &info->segmentStart.w, sizeof(tag));
    return tag;
}

}  // namespace

// FUNC_AT(0x000bffe0)
bool WCollisionMgr::FindFaceInCInst(const Coord3 *point, const InstanceListEntry *entry, StripTriangle *face, float *height) {
    if (entry->strips == NULL)
        return FindFaceInCInstStrips(point, entry->instance, face, height);
    WCollisionInstance *instance = entry->instance;
    MATRIX4 frame;
    instance->MakeMatrix(&frame, true);
    Coord4 local;
    VU0_MATRIX4_vect3mult(point, &frame, &local);
    const CollisionArticle *article = instance->article;
    if (article == NULL)
        return false;
    if (local.x > instance->halfSizeX || -instance->halfSizeX > local.x)
        return false;
    if (local.z > instance->halfSizeZ || -instance->halfSizeZ > local.z)
        return false;

    const StripList *strips = entry->strips;
    const StripVertex *best = NULL;
    float bestHeight = kFar;
    for (const CollisionStrip *const *it = strips->first; it != strips->last; it++) {
        const CollisionStrip *strip = *it;
        Coord3 toCentre;
        v3sub(1, &strip->centre, &local, &toCentre);
        double radius = strip->radius * 0.0625;
        if (!(radius * radius > double(toCentre.z) * toCentre.z + double(toCentre.x) * toCentre.x))
            continue;
        const StripVertex *vertices = article->Vertices(strip);
        const StripVertex *triangle = FindStripTriangle(reinterpret_cast<const Coord3 *>(&local), &vertices);
        if (triangle == NULL)
            continue;
        double lowest = reinterpret_cast<const StripTriangle *>(triangle)->LowestY();
        double above;
        if (instance->flags & kInstanceFacesAbove)
            above = lowest - (double(local.y) - 0.5);
        else
            above = (double(local.y) + 0.5) - lowest;
        if (above > 0.0 && above < bestHeight) {
            bestHeight = float(above);
            *height = float(above);
            best = triangle;
        }
    }
    if (best == NULL)
        return false;
    MakeStripFace(face, best, &frame);
    return true;
}

// FUNC_AT(0x000c01d0)
bool WCollisionMgr::FindFaceInCInst(const MATRIX4 *segmentFrame, const Coord4 *end, const InstanceListEntry *entry, StripTriangle *face, float *height) {
    if (entry->strips == NULL)
        return FindFaceInCInstStrips(segmentFrame, end, entry->instance, face, height);
    MATRIX4 instanceFrame;
    entry->instance->MakeMatrix(&instanceFrame, true);
    MATRIX4 instanceInverse = instanceFrame;
    OrthoInverse(&instanceInverse);
    MATRIX4 segmentInverse = *segmentFrame;
    OrthoInverse(&segmentInverse);
    MATRIX4 toSegment;
    VU0_MATRIX4_mult(&toSegment, &instanceInverse, &segmentInverse);
    const CollisionArticle *article = entry->instance->article;
    if (article == NULL)
        return false;

    const StripVertex *best = NULL;
    float bestHeight = kFar;
    Coord4 start, finish, direction;
    VU0_MATRIX4_vect3mult(segmentFrame->mtx[3], &instanceFrame, &start);
    VU0_MATRIX4_vect3mult(end, &instanceFrame, &finish);
    VU0_v4sub(&finish, &start, &direction);
    const StripList *strips = entry->strips;
    // summed z, x, y here (x, y, z in FindFaceInCInstStrips): kept
    float inverseLengthSquared = float(
        1.0 / ((double(direction.z) * direction.z + double(direction.x) * direction.x) +
               double(direction.y) * direction.y));

    for (const CollisionStrip *const *it = strips->first; it != strips->last; it++) {
        const CollisionStrip *strip = *it;
        float radius = strip->radius * 0.0625f;
        Coord4 nearest, offset;
        Util_NearestPointOnSegment(StripCentre(strip), &start, inverseLengthSquared, &direction, &nearest);
        VU0_v4sub(&strip->centre, &nearest, &offset);
        if (!(double(radius) * radius >
              (double(offset.z) * offset.z + double(offset.y) * offset.y) + double(offset.x) * offset.x))
            continue;
        const StripVertex *vertices = article->Vertices(strip);
        float above;
        const StripVertex *triangle = FindFaceInTriStrip(&toSegment, WorldOrigin, &vertices, &above);
        if (triangle == NULL)
            continue;
        double lifted = double(above) + 0.5;
        if (lifted > -1.0 && lifted < bestHeight) {
            bestHeight = float(lifted);
            *height = float(lifted);
            best = triangle;
        }
    }
    if (best == NULL)
        return false;
    MakeStripFace(face, best, &instanceFrame);
    return true;
}

// FUNC_AT(0x000c0440)
WCollisionObject* WCollisionMgr::GetClosestIntersectingCylObject(const Coord4 *segment, Coord4 *hit,
                                                                  const ObjectList *list) {
    WCollisionObject *best = NULL;
    if (list->first == NULL || list->last - list->first == 0)
        return NULL;
    float bestDistance = kFar;
    for (WCollisionObject *const *it = list->first; it != list->last; it++) {
        WCollisionObject *object = *it;
        float t1, t2;
        if (!WWorldMath::IntersectCircle(segment[0].x, segment[0].z, segment[1].x, segment[1].z, object->position.x,
                                         object->position.z, object->radius, &t1, &t2))
            continue;
        // the nearer root that lies on the segment
        if (t2 >= 0.0f && t2 <= 1.0f) {
            if (!(t1 >= 0.0f && t1 <= 1.0f) || t2 < t1)
                t1 = t2;
        }
        double rest = 1.0 - t1;
        hit->x = float(rest * segment[0].x + double(t1) * segment[1].x);
        hit->y = float(rest * segment[0].y + double(t1) * segment[1].y);
        hit->w = 1.0f;
        hit->z = float(rest * segment[0].z + double(t1) * segment[1].z);
        if (!(hit->y > object->position.y))
            continue;
        if (!(double(object->halfExtents.y) + object->halfExtents.y + object->position.y > hit->y))
            continue;
        double distance = ApproximateDistance(double(hit->x) - segment[0].x, double(hit->z) - segment[0].z);
        if (distance < bestDistance) {
            bestDistance = float(distance);
            best = object;
        }
    }
    return best;
}

// FUNC_AT(0x000c05f0)
WCollisionObject* WCollisionMgr::GetClosestIntersectingOBBObject(const Coord4 *segment, Coord4 *hit,
                                                                  const ObjectList *list) {
    for (WCollisionObject *const *it = list->first; it != list->last; it++) {
        WCollisionObject *object = *it;
        MATRIX4 frame;
        object->MakeMatrix(&frame, true);
        if (GetOBBObjectIntersection(segment, &frame, &object->halfExtents, hit))
            return object;
    }
    return NULL;
}

// FUNC_AT(0x000c0660)
bool WCollisionMgr::GetClosestIntersectingBarrier(const BarrierList *list, const Coord4 *segment,
                                                  WorldCollisionInfo *info) {
    const WCollisionInstance *framed = NULL;   // the rotated instance `frame` and `local` belong to
    const BarrierListEntry *best = NULL;
    float bestDistance = kFar;
    MATRIX4 frame;
    Coord4 local[2];
    info->hitType = kHitNone;
    for (const BarrierListEntry *entry = list->first; entry != list->last; entry++) {
        WCollisionInstance *instance = entry->instance;
        if (instance->flags & kInstanceTilted) {
            // the barrier is in the instance's frame: carry the segment there
            if (framed != instance) {
                instance->MakeMatrix(&frame, true);
                framed = instance;
                VU0_MATRIX4_vect3mult(&segment[0], &frame, &local[0]);
                VU0_MATRIX4_vect3mult(&segment[1], &frame, &local[1]);
            }
            const CollisionBarrier *barrier = &instance->article->Barriers()[entry->index];
            Coord4 crossing;
            if (!WWorldMath::SegmentIntersect(local, barrier->Segment(), &crossing))
                continue;
            if (!(crossing.y > barrier->y0) || !(crossing.y < barrier->y1))
                continue;
            double distance = ApproximateDistance(double(crossing.x) - local[0].x, double(crossing.z) - local[0].z);
            if (distance < bestDistance) {
                bestDistance = float(distance);
                MATRIX4 back;
                instance->MakeMatrix(&back, true);
                best = entry;
                OrthoInverse(&back);
                VU0_MATRIX4_vect3mult(&crossing, &back, &info->point);
            }
        } else {
            Coord4 crossing;
            if (!WWorldMath::SegmentIntersect(segment, entry->barrier.Segment(), &crossing))
                continue;
            if (!(crossing.y > entry->barrier.y0) || !(crossing.y < entry->barrier.y1))
                continue;
            double distance =
                ApproximateDistance(double(crossing.x) - segment[0].x, double(crossing.z) - segment[0].z);
            if (distance < bestDistance) {
                bestDistance = float(distance);
                info->point = crossing;
                best = entry;
            }
        }
    }
    if (best != NULL) {
        static_assert(offsetof(WorldCollisionInfo, groupId) + 4 - offsetof(WorldCollisionInfo, segmentStart) ==
                      sizeof(BarrierListEntry), "a barrier hit carries the list's entry");
        memcpy(&info->segmentStart, best, sizeof(BarrierListEntry));
        info->hitType = kHitBarrier;
    }
    return info->hitType != kHitNone;
}

// FUNC_AT(0x000c0890)
bool WCollisionMgr::GetBarrierNormal(const InstanceList *list, const Coord4 *segment, WorldCollisionInfo *info) {
    const CollisionBarrier *bestBarrier = NULL;
    WCollisionInstance *bestInstance = NULL;
    float bestDistance = kFar;
    uint32_t bestIndex = 0;
    Coord4 bestCrossing;
    for (const InstanceListEntry *it = list->first; it != list->last; it++) {
        WCollisionInstance *instance = it->instance;
        const CollisionArticle *article = instance->article;
        if (article == NULL || article->barrierCount == 0)
            continue;
        MATRIX4 frame;
        instance->MakeMatrix(&frame, true);
        Coord4 local[2];
        VU0_MATRIX4_vect3mult(&segment[0], &frame, &local[0]);
        VU0_MATRIX4_vect3mult(&segment[1], &frame, &local[1]);
        const CollisionBarrier *barrier = article->Barriers();
        for (int i = 0; i < article->barrierCount; i++, barrier++) {
            if (barrierMask & barrier->tag.subType)
                continue;
            Coord4 crossing;
            if (!WWorldMath::SegmentIntersect(local, barrier->Segment(), &crossing))
                continue;
            if (!(crossing.y > barrier->y0) || !(crossing.y < barrier->y1))
                continue;
            double distance = ApproximateDistance(double(crossing.x) - local[0].x, double(crossing.z) - local[0].z);
            if (distance < bestDistance) {
                bestDistance = float(distance);
                bestBarrier = barrier;
                bestInstance = instance;
                bestIndex = i;
                VU0_v4copy(&crossing, &bestCrossing);
            }
        }
    }
    info->hitType = kHitNone;
    if (bestBarrier == NULL)
        return false;

    info->hitType = kHitBarrier;
    info->faceInstance = bestInstance;
    info->flag52 = (bestInstance->flags >> 1) & 1;
    MATRIX4 back;
    bestInstance->MakeMatrix(&back, true);
    OrthoInverse(&back);
    // the barrier carried to the world (the transforms keep its fourth words, set after them), as the list's entry
    BarrierListEntry entry;
    VU0_MATRIX4_vect3mult(&bestBarrier->x0, &back, &entry.barrier.x0);
    VU0_MATRIX4_vect3mult(&bestBarrier->x1, &back, &entry.barrier.x1);
    entry.barrier.inverseLength = bestBarrier->inverseLength;
    entry.barrier.tag = bestBarrier->tag;
    entry.instance = bestInstance;
    entry.index = bestIndex;
    memcpy(&info->segmentStart, &entry, sizeof(entry));
    VU0_MATRIX4_vect3mult(&bestCrossing, &back, &info->point);
    info->point.w = 1.0f;

    // the wall's normal, (dz, 0, -dx) over its length, facing the segment's start
    double scale = info->segmentEnd.w;
    info->normal.y = 0.0f;
    info->normal.x = float((double(info->segmentEnd.z) - info->segmentStart.z) * scale);
    info->normal.z = float(-((double(info->segmentEnd.x) - info->segmentStart.x) * scale));
    info->normal.w = 0.0f;
    FaceTag tag = HitBarrierTag(info);
    info->faceType = tag.type;
    info->faceSubType = tag.subType;
    Coord4 toStart;
    VU0_v4sub(segment, &info->point, &toStart);
    if (double(toStart.x) * info->normal.x + double(toStart.z) * info->normal.z < 0.0) {
        info->normal.x = -info->normal.x;
        info->normal.z = -info->normal.z;
    }
    return true;
}

// FUNC_AT(0x000c0b60)
bool WCollisionMgr::GetBarrierCollision(const BarrierList *list, const Coord4 *segment, WorldCollisionInfo *info) {
    info->hitType = kHitNone;
    if (GetClosestIntersectingBarrier(list, segment, info)) {
        double scale = info->segmentEnd.w;
        info->normal.y = 0.0f;
        info->normal.x = float((double(info->segmentEnd.z) - info->segmentStart.z) * scale);
        info->normal.z = float(-((double(info->segmentEnd.x) - info->segmentStart.x) * scale));
        WCollisionInstance *instance = info->instance;
        FaceTag tag = HitBarrierTag(info);
        info->faceInstance = instance;
        info->normal.w = 0.0f;
        info->faceType = tag.type;
        info->faceSubType = tag.subType;
        info->flag52 = (instance->flags >> 1) & 1;
        Coord4 toStart;
        VU0_v4sub(segment, &info->point, &toStart);
        if (double(toStart.z) * info->normal.z + double(toStart.x) * info->normal.x < 0.0) {
            info->normal.x = -info->normal.x;
            info->normal.z = -info->normal.z;
        }
        info->hitType = kHitBarrier;
    }
    return info->hitType != kHitNone;
}

// FUNC_AT(0x000c0e00)
bool WCollisionMgr::GetWorldNormal(const InstanceList *instances, const BarrierList *barriers, const Coord4 *segment,
                                   WorldCollisionInfo *info, bool checkGround) {
    WorldCollisionInfo ground, barrier;
    WorldCollisionInfo_Construct(&ground, 0);
    WorldCollisionInfo_Construct(&barrier, 0);
    GetBarrierCollision(barriers, segment, &barrier);
    if (checkGround) {
        WWorldPos position;
        position.Construct();
        position.FindClosestFace(instances, &segment[0], &segment[1]);
        if (position.valid) {
            WWorldPos_GetFaceNormal(&position, 0, &ground.normal.x);
            float t;
            ground.normal.w = 1.0f;
            if (WWorldMath::IntersectSegPlane(&segment[0], &segment[1], &position.face.corner[0].Position(),
                                              &ground.normal, &ground.point, &t)) {
                ground.faceType = position.face.corner[2].tag.type;
                ground.faceSubType = position.face.corner[2].tag.subType;
                ground.hitType = kHitWorld;
                ground.faceInstance = position.instance;
                Coord4 toStart;
                VU0_v4sub(segment, &ground.point, &toStart);
                if (v3dotprod(&ground.normal, &toStart) < 0.0f)
                    VU0_v3negate(&ground.normal);
            }
        }
        NullFunction();   // ~WWorldPos
    }
    info->hitType = kHitNone;
    ClosestCollisionInfo(reinterpret_cast<const Coord3 *>(segment), &ground, &barrier, info);
    return info->hitType != kHitNone;
}

// FUNC_AT(0x000c0f80)
bool WCollisionMgr::GetCylObjectCollision(const Coord4 *segment, const ObjectList *list, WorldCollisionInfo *info) {
    info->hitType = kHitNone;
    WCollisionObject *object = GetClosestIntersectingCylObject(segment, &info->point, list);
    info->object = object;
    if (object != NULL) {
        if (object->flags & kObjectFlag01)
            info->flag52 = 1;
        if (object->isCylinder == 1) {
            // straight out from the cylinder's axis, facing the segment's start
            info->normal.y = 0.0f;
            info->normal.x = info->point.x - object->position.x;
            info->normal.w = 1.0f;
            info->normal.z = info->point.z - object->position.z;
            float scale = VU0_rsqrt(float(double(info->normal.x) * info->normal.x +
                                          double(info->normal.z) * info->normal.z));
            info->normal.x = scale * info->normal.x;
            info->normal.y = scale * info->normal.y;
            info->normal.z = scale * info->normal.z;
            Coord3 toStart;
            v3sub(1, segment, &info->point, &toStart);
            if (double(toStart.x) * info->normal.x + double(toStart.z) * info->normal.z < 0.0) {
                info->normal.x = -info->normal.x;
                info->normal.z = -info->normal.z;
            }
        }
        info->faceType = object->faceType;
        info->faceSubType = object->faceSubType;
        info->hitType = kHitObject;
    }
    return info->hitType != kHitNone;
}

// FUNC_AT(0x000c1080)
bool WCollisionMgr::GetOBBObjectCollision(const Coord4 *segment, const ObjectList *list, WorldCollisionInfo *info) {
    info->hitType = kHitNone;
    WCollisionObject *object = GetClosestIntersectingOBBObject(segment, &info->point, list);
    info->object = object;
    if (object != NULL) {
        if (object->flags & kObjectFlag01)
            info->flag52 = 1;
        if (object->isCylinder == 0) {
            // the cylinder's normal again, out from the box's position in x and z: the original's
            info->normal.y = 0.0f;
            info->normal.x = info->point.x - object->position.x;
            info->normal.w = 1.0f;
            info->normal.z = info->point.z - object->position.z;
            float scale = VU0_rsqrt(float(double(info->normal.x) * info->normal.x +
                                          double(info->normal.z) * info->normal.z));
            info->normal.x = scale * info->normal.x;
            info->normal.y = scale * info->normal.y;
            info->normal.z = scale * info->normal.z;
            Coord4 toStart;
            VU0_v4sub(segment, &info->point, &toStart);
            if (double(toStart.x) * info->normal.x + double(toStart.z) * info->normal.z < 0.0) {
                info->normal.x = -info->normal.x;
                info->normal.z = -info->normal.z;
            }
        }
        info->faceType = object->faceType;
        info->faceSubType = object->faceSubType;
        info->hitType = kHitObject;
    }
    return info->hitType != kHitNone;
}

// FUNC_AT(0x000c1380)
OBB* WCollisionMgr::PopObjectOBB(ObjectList *list, Coord3 *velocity, uint16_t *faceTag) {
    if (list->first == NULL || list->last - list->first == 0)
        return NULL;
    WCollisionObject *object = list->last[-1];
    *faceTag = uint16_t(object->faceType | object->faceSubType << 8);
    Coord4 position = { object->position.x, object->position.y, object->position.z, 1.0f };
    MATRIX4 frame;
    object->MakeMatrix(&frame, false);
    if (list->first != NULL && list->last - list->first != 0)
        list->last--;
    velocity->z = 0.0f;
    velocity->y = 0.0f;
    velocity->x = 0.0f;
    if (object->flags & kObjectFlag01) {
        RSceneObj *sceneObject = fgWorld->GetSceneObjFromInstance(&fgWorld->instances[object->renderIndex]);
        if (sceneObject != NULL) {
            if (SceneObjectVelocity(sceneObject) != NULL)
                *velocity = *SceneObjectVelocity(sceneObject);
            v3scale(1, velocity, SimTimeStep, velocity);
        }
    }
    constexpr uint32_t kOBBSize = 0xb0;
    void *memory = UMemory::FastAlloc(kOBBSize, "OBB");
    if (memory == NULL)
        return NULL;
    return static_cast<OBB *>(memory)->Construct(&frame, &position, &object->halfExtents);
}

// =============================================================================================================
// The grid cell iterator
// =============================================================================================================

// FUNC_AT(0x000bfee0)
GridCellIterator* GridCellIterator::Construct(WGridNode *cell, uint32_t type) {
    this->cell = cell;
    remaining = 0;
    this->type = type;
    current = NULL;
    active = 0;
    inDynamic = 0;
    dynamic = NULL;
    remaining = this->cell->staticCount[type];
    if (remaining > 0) {
        active = 1;
        current = this->cell->StaticIndices(type);
    }
    if (cell->dynamic != NULL) {
        WGridDynamicNode *head = this->cell->dynamic->head;
        dynamic = head == NULL ? NULL : head->next;
        active = 1;
    }
    return this;
}

// FUNC_AT(0x000bff50)
const uint16_t* GridCellIterator::Next() {
    if (!active)
        return NULL;
    if (!inDynamic && remaining > 0) {
        const uint16_t *element = current;
        current = element + 1;
        remaining--;
        active = 1;
        return element;
    }
    if (cell->dynamic != NULL) {
        inDynamic = 1;
        if (dynamic != cell->dynamic->head) {
            while (dynamic->value.type != type) {
                dynamic = dynamic->next;
                if (dynamic == cell->dynamic->head)
                    break;
            }
        }
        if (dynamic != cell->dynamic->head) {
            const uint16_t *element = &dynamic->value.index;
            current = element;
            dynamic = dynamic->next;
            return element;
        }
    }
    current = NULL;
    active = 0;
    return NULL;
}

// =============================================================================================================
// std::map<const WindowPane *, WindowHit>: the tree code data/Tree.h has for the data layer's maps, this copy's
// nodes being 0x40 bytes and its helpers its own
// =============================================================================================================

// FUNC_AT(0x000bfd50)
void WindowPaneMap::Lrotate(WindowPaneNode *node) {
    WindowPaneNode *pivot = node->right;
    node->right = pivot->left;
    if (!pivot->left->isNil)
        pivot->left->parent = node;
    pivot->parent = node->parent;
    if (node == head->parent)
        head->parent = pivot;
    else if (node == node->parent->left)
        node->parent->left = pivot;
    else
        node->parent->right = pivot;
    pivot->left = node;
    node->parent = pivot;
}

// FUNC_AT(0x000bfdb0)
void WindowPaneMap::Rrotate(WindowPaneNode *node) {
    WindowPaneNode *pivot = node->left;
    node->left = pivot->right;
    if (!pivot->right->isNil)
        pivot->right->parent = node;
    pivot->parent = node->parent;
    if (node == head->parent)
        head->parent = pivot;
    else if (node == node->parent->right)
        node->parent->right = pivot;
    else
        node->parent->left = pivot;
    pivot->right = node;
    node->parent = pivot;
}

// FUNC_AT(0x000bfe10)
void WindowPaneIterator::Decrement() {
    if (node->isNil) {   // end(): to the last node
        node = node->right;
        return;
    }
    if (!node->left->isNil) {
        WindowPaneNode *previous = node->left;
        while (!previous->right->isNil)
            previous = previous->right;
        node = previous;
        return;
    }
    WindowPaneNode *parent = node->parent;
    if (parent->isNil)
        return;
    while (node == parent->left) {
        node = parent;
        parent = parent->parent;
        if (parent->isNil)
            return;
    }
    node = parent;
}

// FUNC_AT(0x000c0c60)
WindowPaneNode* WindowPaneMap::BuyNode(WindowPaneNode *left, WindowPaneNode *parent, WindowPaneNode *right,
                                       const WindowPaneValue *value, uint8_t color) {
    WindowPaneNode *node = static_cast<WindowPaneNode *>(UMemory::FastAlloc(sizeof(WindowPaneNode), "STL"));
    if (node != NULL) {
        node->left = left;
        node->right = right;
        node->parent = parent;
        node->value = *value;
        node->color = color;
        node->isNil = 0;
    }
    return node;
}

// FUNC_AT(0x000c0cb0)
WindowPaneNode* WindowPaneMap::BuyHeadNode() {
    WindowPaneNode *node = static_cast<WindowPaneNode *>(UMemory::FastAlloc(sizeof(WindowPaneNode), "STL"));
    // the original tests each link's address before clearing it; only the first test can fail
    if (node != NULL)
        node->left = NULL;
    node->parent = NULL;
    node->right = NULL;
    node->color = kTreeBlack;
    node->isNil = 0;
    return node;
}

// FUNC_AT(0x000c0cf0)
void WindowPaneMap::EraseSubtree(WindowPaneNode *node) {
    for (WindowPaneNode *next; !node->isNil; node = next) {
        EraseSubtree(node->right);
        next = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(WindowPaneNode));
    }
}

// FUNC_AT(0x000c1180)
WindowPaneNode* WindowPaneMap::Copy(WindowPaneNode *node, WindowPaneNode *parent) {
    WindowPaneNode *top = head;
    if (!node->isNil) {
        WindowPaneNode *copy = BuyNode(head, parent, head, &node->value, node->color);
        if (top->isNil)
            top = copy;
        copy->left = Copy(node->left, copy);
        copy->right = Copy(node->right, copy);
    }
    return top;
}

// FUNC_AT(0x000c12f0)
void WindowPaneMap::CopyTree(const WindowPaneMap *other) {
    head->parent = Copy(other->head->parent, head);
    size = other->size;
    WindowPaneNode *root = head->parent;
    if (root->isNil) {
        head->left = head;
        head->right = head;
        return;
    }
    WindowPaneNode *node = root;
    while (!node->left->isNil)
        node = node->left;
    head->left = node;
    node = head->parent;
    while (!node->right->isNil)
        node = node->right;
    head->right = node;
}

// FUNC_AT(0x000c1500)
WindowPaneNode** WindowPaneMap::InsertAt(WindowPaneNode **result, bool addLeft, WindowPaneNode *where,
                                         const WindowPaneValue *value) {
    if (size >= 0x5555554) {
        COLLISION_QUERIES_UNTESTED("WindowPaneMap::InsertAt's throw");
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    }
    WindowPaneNode *node = BuyNode(head, where, head, value, kTreeRed);
    size++;
    if (where == head) {
        head->parent = node;
        head->left = node;
        head->right = node;
    } else if (addLeft) {
        where->left = node;
        if (where == head->left)
            head->left = node;
    } else {
        where->right = node;
        if (where == head->right)
            head->right = node;
    }

    for (WindowPaneNode *x = node; x->parent->color == kTreeRed;) {
        WindowPaneNode *parent = x->parent;
        WindowPaneNode *grandparent = parent->parent;
        if (parent == grandparent->left) {
            WindowPaneNode *uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    Lrotate(x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Rrotate(x->parent->parent);
            }
        } else {
            WindowPaneNode *uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    Rrotate(x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Lrotate(x->parent->parent);
            }
        }
    }
    head->parent->color = kTreeBlack;
    *result = node;
    return result;
}

// FUNC_AT(0x000c2200)
WindowPaneNode** WindowPaneMap::EraseAt(WindowPaneNode **result, WindowPaneNode *where) {
    if (where->isNil) {
        COLLISION_QUERIES_UNTESTED("WindowPaneMap::EraseAt's throw");
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    }
    WindowPaneNode *erased = where;
    WindowPaneNode *next = where;
    WindowPaneIncrement(&next, 0);

    WindowPaneNode *pnode = where;   // the node that really leaves its place: where, or its successor
    WindowPaneNode *fixnode;         // the node that takes pnode's place
    WindowPaneNode *fixnodeParent;
    if (where->left->isNil) {
        fixnode = where->right;
    } else if (where->right->isNil) {
        fixnode = where->left;
    } else {
        pnode = next;
        fixnode = pnode->right;
    }

    if (pnode == where) {
        fixnodeParent = where->parent;
        if (!fixnode->isNil)
            fixnode->parent = fixnodeParent;
        if (head->parent == where)
            head->parent = fixnode;
        else if (fixnodeParent->left == where)
            fixnodeParent->left = fixnode;
        else
            fixnodeParent->right = fixnode;
        if (head->left == where)
            head->left = fixnode->isNil ? fixnodeParent : WindowPaneMin(fixnode);
        if (head->right == where)
            head->right = fixnode->isNil ? fixnodeParent : Max(fixnode);
    } else {
        where->left->parent = pnode;
        pnode->left = where->left;
        if (pnode == where->right) {
            fixnodeParent = pnode;
        } else {
            fixnodeParent = pnode->parent;
            if (!fixnode->isNil)
                fixnode->parent = fixnodeParent;
            fixnodeParent->left = fixnode;
            pnode->right = where->right;
            where->right->parent = pnode;
        }
        if (head->parent == where)
            head->parent = pnode;
        else if (where->parent->left == where)
            where->parent->left = pnode;
        else
            where->parent->right = pnode;
        pnode->parent = where->parent;
        uint8_t color = pnode->color;
        pnode->color = where->color;
        where->color = color;
    }

    if (erased->color == kTreeBlack) {
        for (; fixnode != head->parent && fixnode->color == kTreeBlack;
             fixnode = fixnodeParent, fixnodeParent = fixnodeParent->parent) {
            if (fixnode == fixnodeParent->left) {
                WindowPaneNode *sibling = fixnodeParent->right;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Lrotate(fixnodeParent);
                    sibling = fixnodeParent->right;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->left->color == kTreeBlack && sibling->right->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->right->color == kTreeBlack) {
                    sibling->left->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    Rrotate(sibling);
                    sibling = fixnodeParent->right;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->right->color = kTreeBlack;
                Lrotate(fixnodeParent);
                break;
            } else {
                WindowPaneNode *sibling = fixnodeParent->left;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Rrotate(fixnodeParent);
                    sibling = fixnodeParent->left;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->right->color == kTreeBlack && sibling->left->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->left->color == kTreeBlack) {
                    sibling->right->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    Lrotate(sibling);
                    sibling = fixnodeParent->left;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->left->color = kTreeBlack;
                Rrotate(fixnodeParent);
                break;
            }
        }
        fixnode->color = kTreeBlack;
    }

    UMemory::FastFree(erased, sizeof(WindowPaneNode));
    if (size > 0)
        size--;
    *result = next;
    return result;
}

// FUNC_AT(0x000c28b0)
WindowPaneInsert* WindowPaneMap::InsertUnique(WindowPaneInsert *result, const WindowPaneValue *value) {
    WindowPaneNode *where = head;
    bool addLeft = true;
    for (WindowPaneNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = uintptr_t(value->pane) < uintptr_t(node->value.pane);
    }
    WindowPaneIterator at = { where };
    if (addLeft) {
        if (where == head->left) {
            WindowPaneNode *node;
            result->node = *InsertAt(&node, true, where, value);
            result->inserted = true;
            return result;
        }
        at.Decrement();
    }
    if (uintptr_t(at.node->value.pane) < uintptr_t(value->pane)) {
        WindowPaneNode *node;
        result->node = *InsertAt(&node, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = at.node;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x000c2bb0)
WindowPaneNode** WindowPaneMap::EraseRange(WindowPaneNode **result, WindowPaneNode *first, WindowPaneNode *last) {
    if (first == head->left && last == head) {
        EraseSubtree(head->parent);
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
        *result = head->left;
        return result;
    }
    while (first != last) {
        WindowPaneNode *where = first;
        if (!first->isNil) {   // ++first
            if (!first->right->isNil) {
                first = first->right;
                while (!first->left->isNil)
                    first = first->left;
            } else {
                WindowPaneNode *parent = first->parent;
                while (!parent->isNil && first == parent->right) {
                    first = parent;
                    parent = parent->parent;
                }
                first = parent;
            }
        }
        WindowPaneNode *ignored;
        EraseAt(&ignored, where);
    }
    *result = first;
    return result;
}

// =============================================================================================================
// std::map<WCollisionInstance *, CollisionArticle *>: data/Tree.h's code (the same instructions)
// =============================================================================================================

namespace {

TreeNode *AsTreeNode(ArticleMapNode *node) {
    return reinterpret_cast<TreeNode *>(node);
}

TreeNode **AsTreeNodes(ArticleMapNode **node) {
    return reinterpret_cast<TreeNode **>(node);
}

}  // namespace

// FUNC_AT(0x000c0c20)
void ArticleMap::EraseSubtree(ArticleMapNode *node) {
    AsTree()->EraseSubtree(AsTreeNode(node));
}

// FUNC_AT(0x000c16e0)
ArticleMapNode** ArticleMap::InsertAt(ArticleMapNode **result, bool addLeft, ArticleMapNode *where,
                                      const ArticleMapValue *value) {
    AsTree()->InsertAt(AsTreeNodes(result), addLeft, AsTreeNode(where), reinterpret_cast<const TreePair *>(value));
    return result;
}

// FUNC_AT(0x000c24d0)
ArticleMapNode** ArticleMap::EraseAt(ArticleMapNode **result, ArticleMapNode *where) {
    AsTree()->EraseAt(AsTreeNodes(result), AsTreeNode(where));
    return result;
}

// FUNC_AT(0x000c2a80)
ArticleMapInsert* ArticleMap::InsertUnique(ArticleMapInsert *result, const ArticleMapValue *value) {
    ArticleMapNode *where = head;
    bool addLeft = true;
    for (ArticleMapNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = uintptr_t(value->instance) < uintptr_t(node->value.instance);
    }
    ArticleMapNode *at = where;
    if (addLeft) {
        if (where == head->left) {
            ArticleMapNode *node;
            result->node = *InsertAt(&node, true, where, value);
            result->inserted = true;
            return result;
        }
        ArticleMapDecrement(&at, 0);
    }
    if (uintptr_t(at->value.instance) < uintptr_t(value->instance)) {
        ArticleMapNode *node;
        result->node = *InsertAt(&node, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = at;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x000c2f30)
ArticleMapNode** ArticleMap::EraseRange(ArticleMapNode **result, ArticleMapNode *first, ArticleMapNode *last) {
    AsTree()->EraseRange(AsTreeNodes(result), AsTreeNode(first), AsTreeNode(last));
    return result;
}

// =============================================================================================================
// The vectors: their library helpers, and vector::_Insert_n written once for the four compiled copies
// =============================================================================================================

// FUNC_AT(0x000bfe70)
BarrierListEntry** BarrierCopyBackwardTagged(BarrierListEntry **result, BarrierListEntry *first,
                                             BarrierListEntry *last, BarrierListEntry *dest) {
    while (last != first)
        *--dest = *--last;
    *result = dest;
    return result;
}

// FUNC_AT(0x000bfeb0)
void** PointerUninitializedCopy(void *const *first, void *const *last, void **dest) {
    for (; first != last; first++, dest++)
        if (dest != NULL)
            *dest = *first;
    return dest;
}

// FUNC_AT(0x000c0d30)
void BarrierFill(BarrierListEntry *first, BarrierListEntry *last, const BarrierListEntry *value) {
    for (; first != last; first++)
        *first = *value;
}

// FUNC_AT(0x000c0d60)
BarrierListEntry** BarrierCopyBackward(BarrierListEntry **result, BarrierListEntry *first, BarrierListEntry *last,
                                       BarrierListEntry *dest) {
    return BarrierCopyBackwardTagged(result, first, last, dest);
}

// FUNC_AT(0x000c0da0)
void BarrierUninitializedFill(BarrierListEntry *first, uint32_t count, const BarrierListEntry *value) {
    for (; count != 0; count--, first++)
        if (first != NULL)
            *first = *value;
}

// FUNC_AT(0x000c0dd0)
void InstanceUninitializedFill(InstanceListEntry *first, uint32_t count, const InstanceListEntry *value) {
    for (; count != 0; count--, first++)
        if (first != NULL)
            *first = *value;
}

// FUNC_AT(0x000c1230)
void** PointerVector::Ucopy(void *const *first, void *const *last, void **dest) {
    return PointerUninitializedCopy(first, last, dest);
}

// FUNC_AT(0x000c1260)
BarrierListEntry* BarrierList::Ufill(BarrierListEntry *first, uint32_t count, const BarrierListEntry *value) {
    BarrierUninitializedFill(first, count, value);
    return first + count;
}

// FUNC_AT(0x000c1290)
void** PointerVector::Ufill(void **first, uint32_t count, void *const *value) {
    PointerUninitializedFill(first, count, value);
    return first + count;
}

// FUNC_AT(0x000c12c0)
InstanceListEntry* InstanceList::Ufill(InstanceListEntry *first, uint32_t count, const InstanceListEntry *value) {
    InstanceUninitializedFill(first, count, value);
    return first + count;
}

namespace {

template <class T>
uint32_t VectorSize(const GameVector<T> *v) {
    return v->first == NULL ? 0 : uint32_t(v->last - v->first);
}

template <class T>
uint32_t VectorCapacity(const GameVector<T> *v) {
    return v->first == NULL ? 0 : uint32_t(v->end - v->first);
}

// vector::_Insert_n, Dinkumware's as compiled for each element type: Ops names the copy's helpers, each called
// where that copy calls it.
template <class Vector, class T, class Ops>
void InsertN(Vector *v, T *where, uint32_t count, const T *value) {
    T held = *value;
    uint32_t capacity = VectorCapacity(v);
    if (count == 0)
        return;
    if (Ops::kMaxSize - VectorSize(v) < count) {
        Ops::Xlen(v);
        return;
    }
    if (capacity < VectorSize(v) + count) {
        // grow by half (or to just what is needed), copy the head, the new elements and the tail across
        capacity = Ops::kMaxSize - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (capacity < VectorSize(v) + count)
            capacity = Ops::Size(v) + count;
        T *storage = static_cast<T *>(UMemory::FastAlloc(capacity * sizeof(T), "STL"));
        T *at = Ops::UninitializedCopy(v->first, where, storage);
        Ops::UninitializedFill(at, count, &held);
        Ops::UninitializedCopy(where, v->last, at + count);
        count += VectorSize(v);
        if (v->first != NULL)
            Ops::Deallocate(v, v->first, uint32_t(v->end - v->first));
        v->end = storage + capacity;
        v->last = storage + count;
        v->first = storage;
    } else if (uint32_t(v->last - where) < count) {
        // past the end: the tail moves out beyond the new elements
        Ops::Ucopy(v, where, v->last, where + count);
        Ops::Ufill(v, v->last, count - uint32_t(v->last - where), &held);
        v->last += count;
        Ops::Fill(where, v->last - count, &held);
    } else {
        T *oldLast = v->last;
        v->last = Ops::Ucopy(v, oldLast - count, oldLast, v->last);
        Ops::CopyBackward(where, oldLast - count, oldLast);
        Ops::Fill(where, where + count, &held);
    }
}

struct BarrierListOps {
    static constexpr uint32_t kMaxSize = 0x6666666;
    static void Xlen(BarrierList *) { CollisionVectorXlen(); }
    static uint32_t Size(BarrierList *v) { return BarrierList_Size(v, 0); }   // this copy calls size()
    static BarrierListEntry *UninitializedCopy(BarrierListEntry *first, BarrierListEntry *last,
                                               BarrierListEntry *dest) {
        return BarrierUninitializedCopy(first, last, dest);
    }
    static void UninitializedFill(BarrierListEntry *first, uint32_t count, const BarrierListEntry *value) {
        BarrierUninitializedFill(first, count, value);
    }
    static BarrierListEntry *Ucopy(BarrierList *v, BarrierListEntry *first, BarrierListEntry *last,
                                   BarrierListEntry *dest) {
        return v->Ucopy(first, last, dest);
    }
    static void Ufill(BarrierList *v, BarrierListEntry *first, uint32_t count, const BarrierListEntry *value) {
        v->Ufill(first, count, value);
    }
    static void CopyBackward(BarrierListEntry *first, BarrierListEntry *last, BarrierListEntry *dest) {
        BarrierListEntry *end;
        BarrierCopyBackward(&end, first, last, dest);
    }
    static void Fill(BarrierListEntry *first, BarrierListEntry *last, const BarrierListEntry *value) {
        BarrierFill(first, last, value);
    }
    static void Deallocate(BarrierList *, BarrierListEntry *first, uint32_t count) {
        BarrierList_Deallocate(first, count);
    }
};

// The 8-byte vector's helpers are the attribute store's vector's and the colliders' stamp list's, folded: the same
// instructions.
AttributeStoreBlock *AsBlocks(InstanceListEntry *entries) {
    return reinterpret_cast<AttributeStoreBlock *>(entries);
}

ColliderStamp *AsStamps(InstanceListEntry *entries) {
    return reinterpret_cast<ColliderStamp *>(entries);
}

struct InstanceListOps {
    static constexpr uint32_t kMaxSize = 0x1fffffff;
    static void Xlen(InstanceList *) { InstanceList_Xlen(); }
    static uint32_t Size(InstanceList *v) { return VectorSize(v); }
    static InstanceListEntry *UninitializedCopy(InstanceListEntry *first, InstanceListEntry *last,
                                                InstanceListEntry *dest) {
        return reinterpret_cast<InstanceListEntry *>(
            StoreBlockUninitializedCopy(AsBlocks(first), AsBlocks(last), AsBlocks(dest)));
    }
    static void UninitializedFill(InstanceListEntry *first, uint32_t count, const InstanceListEntry *value) {
        InstanceUninitializedFill(first, count, value);
    }
    static InstanceListEntry *Ucopy(InstanceList *v, InstanceListEntry *first, InstanceListEntry *last,
                                    InstanceListEntry *dest) {
        ColliderStampList *stamps = reinterpret_cast<ColliderStampList *>(v);
        return reinterpret_cast<InstanceListEntry *>(stamps->Ucopy(AsStamps(first), AsStamps(last), AsStamps(dest)));
    }
    static void Ufill(InstanceList *v, InstanceListEntry *first, uint32_t count, const InstanceListEntry *value) {
        v->Ufill(first, count, value);
    }
    static void CopyBackward(InstanceListEntry *first, InstanceListEntry *last, InstanceListEntry *dest) {
        AttributeStoreBlock *end;
        StoreBlockCopyBackward(&end, AsBlocks(first), AsBlocks(last), AsBlocks(dest));
    }
    static void Fill(InstanceListEntry *first, InstanceListEntry *last, const InstanceListEntry *value) {
        StoreBlockFill(AsBlocks(first), AsBlocks(last),
                       *reinterpret_cast<const AttributeStoreBlock *>(value));
    }
    static void Deallocate(InstanceList *v, InstanceListEntry *first, uint32_t count) {
        InstanceList_Deallocate(v, 0, first, count);
    }
};

// The vectors of pointers share one copy of each helper, whatever they point at.
template <class Vector, class T>
struct PointerListOps {
    static constexpr uint32_t kMaxSize = 0x3fffffff;
    static void **Raw(T *p) { return static_cast<void **>(static_cast<void *>(p)); }
    static void *const *Raw(const T *p) { return static_cast<void *const *>(static_cast<const void *>(p)); }
    static PointerVector *Raw(Vector *v) { return reinterpret_cast<PointerVector *>(v); }
    static uint32_t Size(Vector *v) { return VectorSize(v); }
    static T *UninitializedCopy(T *first, T *last, T *dest) {
        return static_cast<T *>(static_cast<void *>(PointerUninitializedCopy(Raw(first), Raw(last), Raw(dest))));
    }
    static void UninitializedFill(T *first, uint32_t count, const T *value) {
        PointerUninitializedFill(Raw(first), count, Raw(value));
    }
    static T *Ucopy(Vector *v, T *first, T *last, T *dest) {
        return static_cast<T *>(static_cast<void *>(Raw(v)->Ucopy(Raw(first), Raw(last), Raw(dest))));
    }
    static void Ufill(Vector *v, T *first, uint32_t count, const T *value) {
        Raw(v)->Ufill(Raw(first), count, Raw(value));
    }
    static void CopyBackward(T *first, T *last, T *dest) {
        void **end;
        PointerCopyBackward(&end, Raw(first), Raw(last), Raw(dest));
    }
    static void Fill(T *first, T *last, const T *value) {
        PointerFill(Raw(first), Raw(last), Raw(value));
    }
    static void Deallocate(Vector *v, T *first, uint32_t count) {
        PointerVector_Deallocate(v, 0, Raw(first), count);
    }
};

struct ObjectListOps : PointerListOps<ObjectList, WCollisionObject *> {
    static void Xlen(ObjectList *) { PointerVector_Xlen(); }
};

struct StripListOps : PointerListOps<StripList, const CollisionStrip *> {
    static void Xlen(StripList *v) { v->Xlen(); }
};

// vector::reserve, as the two copies of it for vectors of 4-byte elements have it
template <class T>
void Reserve(GameVector<T> *v, uint32_t count) {
    if (VectorCapacity(v) >= count)
        return;
    T *storage = static_cast<T *>(UMemory::FastAlloc(count * sizeof(T), "STL"));
    PointerUninitializedCopy(reinterpret_cast<void *const *>(v->first), reinterpret_cast<void *const *>(v->last),
                             reinterpret_cast<void **>(storage));
    uint32_t size = VectorSize(v);
    if (v->first != NULL)
        UMemory::FastFree(v->first, uint32_t(v->end - v->first) * sizeof(T));
    v->first = storage;
    v->end = storage + count;
    v->last = storage + size;
}

}  // namespace

// FUNC_AT(0x000c18c0)
void BarrierList::InsertN(BarrierListEntry *where, uint32_t count, const BarrierListEntry *value) {
    ::InsertN<BarrierList, BarrierListEntry, BarrierListOps>(this, where, count, value);
}

// FUNC_AT(0x000c1bf0)
void ObjectList::InsertN(WCollisionObject **where, uint32_t count, WCollisionObject *const *value) {
    ::InsertN<ObjectList, WCollisionObject *, ObjectListOps>(this, where, count, value);
}

// FUNC_AT(0x000c1eb0)
void StripList::Xlen() {
    COLLISION_QUERIES_UNTESTED("StripList::Xlen");
    ThrowLengthError("vector<T> too long");
}

// FUNC_AT(0x000c1f30)
void InstanceList::InsertN(InstanceListEntry *where, uint32_t count, const InstanceListEntry *value) {
    ::InsertN<InstanceList, InstanceListEntry, InstanceListOps>(this, where, count, value);
}

// FUNC_AT(0x000c27a0)
void WGridCellList::Reserve(uint32_t count) {
    if (count > 0x3fffffff) {
        COLLISION_QUERIES_UNTESTED("WGridCellList::Reserve's throw");
        CellList_Xlen();
        return;
    }
    ::Reserve<uint32_t>(this, count);
}

// FUNC_AT(0x000c2970)
void ObjectList::Reserve(uint32_t count) {
    if (count > 0x3fffffff) {
        COLLISION_QUERIES_UNTESTED("ObjectList::Reserve's throw");
        PointerVector_Xlen();
        return;
    }
    ::Reserve<WCollisionObject *>(this, count);
}

// FUNC_AT(0x000c2b40)
BarrierListEntry** BarrierList::Insert(BarrierListEntry **result, BarrierListEntry *where,
                                       const BarrierListEntry *value) {
    uint32_t offset = VectorSize(this) == 0 ? 0 : uint32_t(where - first);
    InsertN(where, 1, value);
    *result = first + offset;
    return result;
}

// FUNC_AT(0x000c2c70)
void StripList::InsertN(const CollisionStrip **where, uint32_t count, const CollisionStrip *const *value) {
    ::InsertN<StripList, const CollisionStrip *, StripListOps>(this, where, count, value);
}
