#include "Collide.h"
#include "Anim.h"
#include "celglist.h"
#include "viewer.h"
#include "../memory.h"
#include "../game.h"
#include "../math/X87Ops.h"
#include "../game/drone/Drone.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Player.h"
#include "../game/obj/build.h"
#include "../game/obj/bullet.h"
#include "../game/obj/control.h"
#include "../game/obj/object.h"
#include "../util/Random.h"
#include "../util/Sort.h"

#include <math.h>
#include <string.h>

// XBE_GLOBAL(0x001dc9a0, 0x12c0)
BOXENTRY_tag ColBoxs[MAX_COLBOXES];

// XBE_GLOBAL(0x001ddc6c, 0x4)
uint32_t BoxCnt;
// XBE_GLOBAL(0x001dec10, 0xc)
_VECTOR DeltaP;
// XBE_GLOBAL(0x001dec1c, 0x1)
uint8_t Ident;
// XBE_GLOBAL(0x001dec28, 0x4)
uint32_t TriAllocCnt;
// XBE_GLOBAL(0x001dec2c, 0x4)
uint32_t TriOnCnt;
// XBE_GLOBAL(0x001dec30, 0x4)
_VECTOR *TriHeap;

#pragma fp_contract(off)

// XBE_GLOBAL(0x001ddc60, 0xc)
LLISTINFO_tag HitHeap;
// XBE_GLOBAL(0x001df41c, 0xc)
#define ForcedList (*(LLISTINFO_tag*)0x001df41c)
// The hits Collide_Sort sorts
// XBE_GLOBAL(0x001ddc70, 0xfa0)
static HITDATA_tag *SrtList[1000];
// The cels Collide_StraddleCels finds
// XBE_GLOBAL(0x001dec38, 0x200)
static cel_tag *StraddleCels[128];
// Intersect_Portal's triangle
// XBE_GLOBAL(0x001dec34, 0x2)
uint16_t PortalTriangle;


// The vertices and planes of a leaf box's triangles: shorts, the normals times 2^-14
constexpr float kCollScale = 6.103888e-05f;
static_assert(std::bit_cast<uint32_t>(kCollScale) == 0x38800200, "the original's constant at 0x0015d4fc");

// Intersect_RayBox: a slab the ray never leaves
constexpr float kSlabNear = std::bit_cast<float>(0xfdcca14bu);
constexpr float kSlabFar = std::bit_cast<float>(0x7f7fc99eu);

// vecutil_Dist2Tri's code for a point behind the plane
#define DIST2TRI_BEHIND 99

// AUTOINJECT
void Collide_FreeHitList(HITDATA_tag **hitList) {

    // If the list has already been freed, return
    if(hitList == NULL)
        return;

    // Iterate through the list and add each element to the HitHeap
    LLNODE_tag* head = (LLNODE_tag*)*hitList;
    while(head != NULL) {
        LLNODE_tag* next = (head->next);
        LList_Add(&HitHeap, (LLNODE_tag*)head);
        head = next;
    }

    // Discard all entries from the list
    *hitList = NULL;

    // ?? don't update the count?!

}

// XBE_GLOBAL(0x001dec24, 0x4)
static uint32_t HitAllocCnt;

// AUTOINJECT
HITDATA_tag* Coll_GetFreeHit(void) {

    // Try to obtain from the heap
    HITDATA_tag* node = (HITDATA_tag*)LList_Cut(&HitHeap);
    if(node == NULL) {

        // Heap is empty and the maximum number of allocations has been reached
        if(HitAllocCnt > 1000) {
            NF_WARN("Run out of hit datas!! Didn't forget to call Collide_FreeHitList\n"); // GC check (0x80105350)
            return NULL;
        }

        // Heap is empty but we haven't reached the maximum number of allocations yet
        HitAllocCnt += 0x40;
        LList_AllocnNodes(&HitHeap, 0x40);
        node = (HITDATA_tag*)LList_Cut(&HitHeap);
    }

    memset(node, 0, sizeof(HITDATA_tag));
    return node;
}

// A hit from the heap, at the head of the list. Not checked: with the heap used up the hit is NULL.
// AUTOINJECT
HITDATA_tag* Coll_AddHitToList(HITDATA_tag **list, float dist, cel_tag *cel, obj_tag *obj) {
    HITDATA_tag *hit = Coll_GetFreeHit();
    hit->dist = dist;
    if (*list != NULL) {
        hit->next = *list;
        hit->prev = NULL;
        (*list)->prev = hit;
    }
    *list = hit;
    hit->hitCel = cel;
    hit->hitObj = obj;
    return hit;
}

// Gives the rejected hits (HIT_NONE) back to the heap and sorts the others by distance
void _Collide_Sort(HITDATA_tag **list) {
    HITDATA_tag *hit = *list;
    if (hit == NULL)
        return;
    uint count = 0;
    while (hit != NULL) {
        HITDATA_tag *next = hit->next;
        if (hit->dist == HIT_NONE)
            LList_Add(&HitHeap, (LLNODE_tag *)hit);
        else
            SrtList[count++] = hit;
        hit = next;
    }
    if (count == 0) {
        *list = NULL;
        return;
    }
    QuickSort(SrtList, count, sizeof(HITDATA_tag *), Compare_HitData, NULL);
    *list = SrtList[0];
    HITDATA_tag *prev = SrtList[0];
    prev->prev = NULL;
    for (uint i = 1; i < count; i++) {
        prev->next = SrtList[i];
        SrtList[i]->prev = prev;
        prev = prev->next;
    }
    prev->next = NULL;
}

// The list in EBX
// AUTOLTCG
void __declspec(naked) Collide_Sort(void) {
    _asm {
        push ebx
        call _Collide_Sort
        add esp, 4
        ret
    }
}

// Whether the test passes through the hit's material (and marks it HIT_NONE): the mask's 0x800 the material's 0x40,
// 1 surfaces 13 and 14, 2 surface 16, 8 a hit that makes no contact
// AUTOINJECT
bool Intersect_CheckIfFlagsMatch(uchar material, uchar hitFlags, ushort mask, float *dist) {
    if ((material & 0x40) && (mask & 0x800))
        *dist = HIT_NONE;
    uint flags = hitFlags;
    if (material & 0xc0)
        flags |= HIT_NO_CONTACT;
    if ((mask & 1) && ((material & 0x3f) == 13 || (material & 0x3f) == 14))
        *dist = HIT_NONE;
    if ((mask & 2) && (material & 0x3f) == 16)
        *dist = HIT_NONE;
    if ((mask & 8) && (flags & HIT_NO_CONTACT)) {
        *dist = HIT_NONE;
        return true;
    }
    return *dist == HIT_NONE;
}

// x, then z, then y
// AUTOINJECT
bool Intersect_SphereBox(_VECTOR *centre, float radius, _VECTOR *boxMin, _VECTOR *boxMax) {
    if ((double)centre->x - radius > boxMax->x || radius + (double)centre->x < boxMin->x)
        return false;
    if ((double)centre->z - radius > boxMax->z || radius + (double)centre->z < boxMin->z)
        return false;
    if ((double)centre->y - radius > boxMax->y || radius + (double)centre->y < boxMin->y)
        return false;
    return true;
}

// AUTOINJECT
bool Intersect_BoxBox(_VECTOR *min1, _VECTOR *max1, _VECTOR *min2, _VECTOR *max2) {
    if (min2->x > max1->x || max2->x < min1->x)
        return false;
    if (min2->z > max1->z || max2->z < min1->z)
        return false;
    if (min2->y > max1->y || max2->y < min1->y)
        return false;
    return true;
}

// A sphere against the box mtx orients, halfSize either side of its translation
// AUTOINJECT
bool Intersect_PointOOBox(_MATRIX *mtx, _VECTOR *halfSize, _VECTOR *point, float radius) {
    _VECTOR offset, axis;
    Vec_Subtract(point, Mat_Position(*mtx), &offset);
    Mat_GetNorm(&axis, mtx);
    if (AbsFchs(((double)offset.z * axis.z + (double)offset.y * axis.y) + (double)offset.x * axis.x) >
        (double)radius + halfSize->x)
        return false;
    Mat_GetUp(&axis, mtx);
    if (AbsFchs(((double)axis.z * offset.z + (double)axis.y * offset.y) + (double)axis.x * offset.x) >
        (double)radius + halfSize->y)
        return false;
    Mat_GetDir(&axis, mtx);
    if (AbsFchs(((double)axis.z * offset.z + (double)axis.y * offset.y) + (double)axis.x * offset.x) >
        (double)radius + halfSize->z)
        return false;
    return true;
}

// The ray from rayStart along dir (dir is the whole ray: t of 1 is rayEnd) against the box: where it enters, from 0
// to the test's radius. A start inside the box enters at 0.
// AUTOINJECT
bool Intersect_RayBox(HITTEST_tag *test, _VECTOR *boxMin, _VECTOR *boxMax, float *t) {
    _VECTOR *start = &test->rayStart;
    if (start->x >= boxMin->x && start->x <= boxMax->x && start->y >= boxMin->y && start->y <= boxMax->y &&
        start->z >= boxMin->z && start->z <= boxMax->z) {
        *t = 0.0f;
        return true;
    }
    float nearX = kSlabNear, nearY = kSlabNear, nearZ = kSlabNear;
    float farX = kSlabFar, farY = kSlabFar, farZ = kSlabFar;
    // The near side is compared unrounded, and stored
    if (test->dir.x == 0.0f) {
        if (start->x < boxMin->x)
            return false;
        if (start->x > boxMax->x)
            return false;
    } else {
        double inverse = 1.0 / test->dir.x;
        float a = (float)(((double)boxMin->x - start->x) * inverse);
        double b = ((double)boxMax->x - start->x) * inverse;
        nearX = a < b ? a : (float)b;
        farX = a < b ? (float)b : a;
        if (farX < 0.0f)
            return false;
    }
    if (test->dir.z == 0.0f) {
        if (start->z < boxMin->z)
            return false;
        if (start->z > boxMax->z)
            return false;
    } else {
        double inverse = 1.0 / test->dir.z;
        float a = (float)(((double)boxMin->z - start->z) * inverse);
        double b = ((double)boxMax->z - start->z) * inverse;
        nearZ = a < b ? a : (float)b;
        farZ = a < b ? (float)b : a;
        if (farZ < 0.0f)
            return false;
    }
    if (test->dir.y == 0.0f) {
        if (start->y < boxMin->y)
            return false;
        if (start->y > boxMax->y)
            return false;
    } else {
        double inverse = 1.0 / test->dir.y;
        float a = (float)(((double)boxMin->y - start->y) * inverse);
        double b = ((double)boxMax->y - start->y) * inverse;
        nearY = a < b ? a : (float)b;
        farY = a < b ? (float)b : a;
        if (farY < 0.0f)
            return false;
    }
    float enter = nearX > nearY ? (nearX > nearZ ? nearX : nearZ) : (nearY > nearZ ? nearY : nearZ);
    float leave = farX < farY ? (farX < farZ ? farX : farZ) : (farY < farZ ? farY : farZ);
    if (enter > leave)
        return false;
    *t = enter;
    if (!(enter >= 0.0f))
        return false;
    return enter <= test->radius;
}

// The leaf boxes a ray crosses, and where it enters them, into ColBoxs
// AUTOINJECT
void ASH_RecurseBoxesRay(COLLDATA_tag *coll, COLLBOX_tag *box, HITTEST_tag *test) {
    float t;
    if (!Intersect_RayBox(test, &box->boundMin, &box->boundMax, &t))
        return;
    if (box->childA < 0) {
        ColBoxs[BoxCnt].t = t;
        ColBoxs[BoxCnt].box = box;
        BoxCnt++;
        return;
    }
    ASH_RecurseBoxesRay(coll, &coll->collBoxes[box->childA], test);
    ASH_RecurseBoxesRay(coll, &coll->collBoxes[box->childB], test);
}

// The leaf boxes a sphere touches, into ColBoxs
// AUTOINJECT
void RecurseBoxesSph(COLLDATA_tag *coll, COLLBOX_tag *box, _VECTOR *centre, float radius) {
    if (!Intersect_SphereBox(centre, radius, &box->boundMin, &box->boundMax))
        return;
    if (box->childA < 0) {
        ColBoxs[BoxCnt].box = box;
        BoxCnt++;
        return;
    }
    RecurseBoxesSph(coll, &coll->collBoxes[box->childA], centre, radius);
    RecurseBoxesSph(coll, &coll->collBoxes[box->childB], centre, radius);
}

// The leaf boxes a box touches, into ColBoxs
// AUTOINJECT
void ASH_RecurseBoxesBox(COLLDATA_tag *coll, COLLBOX_tag *box, _VECTOR *boxMin, _VECTOR *boxMax) {
    if (!Intersect_BoxBox(boxMin, boxMax, &box->boundMin, &box->boundMax))
        return;
    if (box->childA < 0) {
        ColBoxs[BoxCnt].box = box;
        BoxCnt++;
        return;
    }
    ASH_RecurseBoxesBox(coll, &coll->collBoxes[box->childA], boxMin, boxMax);
    ASH_RecurseBoxesBox(coll, &coll->collBoxes[box->childB], boxMin, boxMax);
}

// A triangle's plane from its vertex block: three shorts times kCollScale, then d
// AUTOINJECT
bool Intersect_UnpackTrianglePlaneEq(uchar *vertexBlock, ushort normal, plane_equ_tag *plane) {
    const short *entry = (const short *)vertexBlock + normal;
    plane->a = entry[0] * kCollScale;
    plane->b = entry[1] * kCollScale;
    plane->c = entry[2] * kCollScale;
    memcpy(&plane->d, &entry[3], sizeof(float));
    return true;
}

// A triangle's three vertices from its vertex block: shorts times the box's scale, plus its offset
// AUTOINJECT
void Intersect_UnpackTriangleVerticesToWorld(uchar *vertexBlock, ushort v0, ushort v1, ushort v2, _VECTOR *vertsOut, COLLQUANT_tag *quant) {
    const ushort index[3] = {v0, v1, v2};
    for (int i = 0; i < 3; i++) {
        const short *entry = (const short *)vertexBlock + index[i];
        vertsOut[i].x = (float)((double)entry[0] * quant->scale + quant->offset.x);
        vertsOut[i].y = (float)((double)entry[1] * quant->scale + quant->offset.y);
        vertsOut[i].z = (float)((double)entry[2] * quant->scale + quant->offset.z);
    }
}

// Whether the triangle's bounds meet the box: x, then z, then y
bool _Intersect_AABB_SweptVolume(_VECTOR *v0, _VECTOR *v1, _VECTOR *v2, _VECTOR *boxMin, _VECTOR *boxMax) {
    float most = v0->x > v1->x ? v0->x : v1->x;
    if ((most > v2->x ? most : v2->x) < boxMin->x)
        return false;
    float least = v0->x < v1->x ? v0->x : v1->x;
    if ((least < v2->x ? least : v2->x) > boxMax->x)
        return false;
    most = v0->z > v1->z ? v0->z : v1->z;
    if ((most > v2->z ? most : v2->z) < boxMin->z)
        return false;
    least = v0->z < v1->z ? v0->z : v1->z;
    if ((least < v2->z ? least : v2->z) > boxMax->z)
        return false;
    most = v0->y > v1->y ? v0->y : v1->y;
    if ((most > v2->y ? most : v2->y) < boxMin->y)
        return false;
    least = v0->y < v1->y ? v0->y : v1->y;
    if ((least < v2->y ? least : v2->y) > boxMax->y)
        return false;
    return true;
}

// The vertices in EDX, ECX and ESI, the box in EDI and EBX
// AUTOLTCG
void __declspec(naked) Intersect_AABB_SweptVolume(void) {
    _asm {
        push ebx
        push edi
        push esi
        push ecx
        push edx
        call _Intersect_AABB_SweptVolume
        add esp, 20
        ret
    }
}

// Two spheres: whether they overlap, a - b, and the square of the distance between the centres
bool _Intersect_SphereSphere(_VECTOR *a, float radiusA, _VECTOR *b, float radiusB, _VECTOR *difference, float *distSq) {
    Vec_Subtract(a, b, difference);
    double squared = Vec_SqMagnitude(difference);
    *distSq = (float)squared;
    double reach = (double)radiusA + radiusB;
    return squared < reach * reach;
}

// The spheres (a centre and its radius) in EBX and EDI, the difference to ESI
// AUTOLTCG
void __declspec(naked) Intersect_SphereSphere(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        push dword ptr [edi + 12]
        push edi
        push dword ptr [ebx + 12]
        push ebx
        call _Intersect_SphereSphere
        add esp, 24
        ret
    }
}

// The test's ray against its sphere
bool _Intersect_RaySphere(HITTEST_tag *test, float *distSq) {
    _VECTOR closest;
    vecutil_calculate_closest_point_on_line(&closest, &test->rayStart, &test->rayEnd, &test->sphere);
    double squared = Vec_SqDist3D(&test->sphere, &closest);
    *distSq = (float)squared;
    return squared < (double)test->sphereRadius * test->sphereRadius;
}

// The test in ESI
// AUTOLTCG
void __declspec(naked) Intersect_RaySphere(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call _Intersect_RaySphere
        add esp, 8
        ret
    }
}

// The test's cylinder against its sphere
bool _Intersect_CylSphere(HITTEST_tag *test, float *distSq) {
    _VECTOR closest;
    vecutil_calculate_closest_point_on_line(&closest, &test->rayStart, &test->rayEnd, &test->sphere);
    double squared = Vec_SqDist3D(&test->sphere, &closest);
    *distSq = (float)squared;
    double reach = (double)test->radius + test->sphereRadius;
    return squared < reach * reach;
}

// The test in ESI
// AUTOLTCG
void __declspec(naked) Intersect_CylSphere(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call _Intersect_CylSphere
        add esp, 8
        ret
    }
}

// The cels a test reaches through the portals it crosses, from its own (which comes last), at most 127
// AUTOINJECT
cel_tag** Collide_StraddleCels(HITTEST_tag *test, ushort *count) {
    cel_tag *cel = test->cel;
    *count = 0;
    GameState.collisionStamp++;
    ushort next = 0;
    while (cel != NULL) {
        for (portal_tag *portal = cel->portals; portal != NULL && *count < 0x7e; portal = portal->next) {
            if (portal->stamp == GameState.collisionStamp)
                continue;
            portal->stamp = GameState.collisionStamp;
            portal->partner->stamp = GameState.collisionStamp;
            if (!Intersect_Portal(test, portal))
                continue;
            ushort i = 0;
            while (i < *count && portal->cel != StraddleCels[i])
                i++;
            if (i == *count)
                StraddleCels[(*count)++] = portal->cel;
        }
        cel = next < *count ? StraddleCels[next++] : NULL;
        if (*count >= 0x7e)
            break;
    }
    StraddleCels[(*count)++] = test->cel;
    return StraddleCels;
}

// The portal's two triangles, and the same reversed
static const ushort kPortalTriangles[12] = {0, 1, 2, 2, 3, 0, 2, 1, 0, 0, 3, 2};

// Whether the test crosses the portal: a ray its triangles (as a line), a sphere its box and then a triangle within
// the radius, a cylinder its point nearest the plane within 1.5 radii of a triangle
// AUTOINJECT
bool Intersect_Portal(HITTEST_tag *test, portal_tag *portal) {
    plane_equ_tag back;
    back.a = -portal->plane.a;
    back.b = -portal->plane.b;
    back.c = -portal->plane.c;
    back.d = portal->plane.d;
    plane_equ_tag *planes[2] = {&portal->plane, &back};
    ushort code;
    switch (test->type) {
    case HITTEST_RAY_BOXES:
    case HITTEST_RAY: {
        _VECTOR dir;
        float t;
        Vec_Subtract(&test->rayEnd, &test->rayStart, &dir);
        for (PortalTriangle = 0; PortalTriangle < 2; PortalTriangle++) {
            const ushort *tri = &kPortalTriangles[PortalTriangle * 3];
            if (Collide_RayTriangle(&test->rayStart, &dir, &portal->corners[tri[0]], &portal->corners[tri[1]],
                                    &portal->corners[tri[2]], &t))
                return true;
        }
        return false;
    }
    case HITTEST_SPHERE_ONE_SIDED:
    case HITTEST_SPHERE:
    case HITTEST_POINT:
    case HITTEST_SPHERE_BOXES:
        if (!Intersect_SphereBox(&test->pos, test->radius, &portal->boxMin, &portal->boxMax))
            return false;
        for (PortalTriangle = 0; PortalTriangle < 2; PortalTriangle++) {
            const ushort *tri = &kPortalTriangles[PortalTriangle * 3];
            if (AbsFchs(vecutil_Dist2Tri(&test->pos, &portal->corners[tri[0]], &portal->corners[tri[1]],
                                         &portal->corners[tri[2]], planes[PortalTriangle / 2], &code, NULL)) <
                test->radius)
                return true;
        }
        return false;
    case HITTEST_CYLINDER: {
        _VECTOR nearest;
        double towards = ((double)portal->plane.c * test->dir.z + (double)portal->plane.b * test->dir.y) +
                         (double)portal->plane.a * test->dir.x;
        if (towards != 0.0) {
            float inverse = (float)(1.0 / towards);
            float s;
            if (Negate(DistancePointToPlane(&test->rayStart, &portal->plane) * inverse) < 0.0)
                s = 0.0f;
            else if (Negate(DistancePointToPlane(&test->rayStart, &portal->plane) * inverse) > 1.0)
                s = 1.0f;
            else
                s = (float)Negate(DistancePointToPlane(&test->rayStart, &portal->plane) * inverse);
            auxVec_AddMulR32(&test->rayStart, &test->dir, s, &nearest);
        } else {
            vecutil_calculate_closest_point_on_line(&nearest, &test->rayStart, &test->rayEnd, &portal->centre);
        }
        for (PortalTriangle = 0; PortalTriangle < 2; PortalTriangle++) {
            const ushort *tri = &kPortalTriangles[PortalTriangle * 3];
            if (AbsFchs(vecutil_Dist2Tri(&nearest, &portal->corners[tri[0]], &portal->corners[tri[1]],
                                         &portal->corners[tri[2]], planes[PortalTriangle / 2], &code, NULL)) <
                (double)test->radius * 1.5f)
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

// The broad phase against an object: the candidates go to the tester's hit list. A sphere leaves the difference of
// the centres in the test's plane, a ray or cylinder the object's sphere in the test's.
void _Collide_PickObj(HITTEST_tag *test, obj_tag *obj, obj_tag *tester) {
    if (tester == obj || obj == test->ignoreA || obj == test->ignore)
        return;
    if (test->tester != NULL && (test->tester->unknown_0xd9 & 1) && (obj->unknown_0xd9 & 2))
        return;
    uint skip = 0x20;
    if (test->mask & 0x20)
        skip |= 0x1000;
    if (test->mask & 8)
        skip |= 0x40;
    if (test->mask & 4)
        skip |= 0x10;
    if (obj->effectFlags & skip)
        return;
    if ((test->mask & 0x400) && (obj->objectType == OBJECTTYPE_PLAYER || obj->objectType == OBJECTTYPE_DEAD_PLAYER))
        return;
    if ((test->mask & 0x100) && (obj->objectType == OBJECTTYPE_DRONE || obj->objectType == OBJECTTYPE_DEAD_DRONE))
        return;
    if ((test->mask & 0x200) && obj->objectType == OBJECTTYPE_BULLET)
        return;
    if (test->type != HITTEST_SPHERE_ONE_SIDED && !(obj->transformFlags & TRANSFORM_ANIMATED) &&
        (obj->objGraphics == NULL || obj->objGraphics->colldata == NULL))
        return;
    float dist;
    switch (test->type) {
    case HITTEST_RAY_BOXES:
    case HITTEST_RAY:
        Vec_Copy(&obj->centrePoint, &test->sphere);
        test->sphereRadius = obj->radius;
        if (_Intersect_RaySphere(test, &dist))
            Vec_Copy(&test->dir, &Coll_AddHitToList(&tester->hitList, dist, NULL, obj)->maybehitDirection);
        break;
    case HITTEST_SPHERE_ONE_SIDED:
    case HITTEST_SPHERE:
    case HITTEST_POINT:
    case HITTEST_SPHERE_BOXES:
        if (_Intersect_SphereSphere(&test->sphere, test->sphereRadius, &obj->centrePoint, obj->radius,
                                    &test->plane.normal, &dist))
            Coll_AddHitToList(&tester->hitList, dist, NULL, obj);
        break;
    case HITTEST_CYLINDER:
        Vec_Copy(&obj->centrePoint, &test->sphere);
        test->sphereRadius = obj->radius * 1.5f;
        if (_Intersect_CylSphere(test, &dist)) {
            HITDATA_tag *hit = Coll_AddHitToList(&tester->hitList, dist, NULL, obj);
            Vec_Copy(&test->plane.normal, &hit->surfaceNormal.normal);
            Vec_Copy(&test->plane.normal, &hit->maybehitDirection);
        }
        break;
    }
}

// The test in EAX
// AUTOLTCG
void __declspec(naked) Collide_PickObj(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        call _Collide_PickObj
        add esp, 12
        ret
    }
}

// The broad phase: in each cel the test reaches, the cels chained from it (but with mask 0x80), its objects (but
// with mask 0x10) and the cel itself (but with mask 0x40), then the forced objects. The candidates go to the
// tester's hit list.
// AUTOINJECT
void Collide_Pick(HITTEST_tag *test) {
    obj_tag *tester = test->tester;
    if (test->cel == NULL)
        return;
    GameState.collisionStamp++;
    if (test->type & (HITTEST_SPHERE_ONE_SIDED | HITTEST_SPHERE_BOXES)) {
        Vec_Copy(&test->pos, &test->sphere);
        test->sphereRadius = test->radius;
    }
    if (test->type & HITTEST_RAY_BOXES) {
        Vec_Subtract(&test->rayEnd, &test->rayStart, &test->dir);
        double length = Vec_Magnitude(&test->dir);
        test->radius = (float)length;
        if (length < 1.0)
            test->radius = 1.0f;
    }
    if (test->type & HITTEST_CYLINDER)
        Vec_Subtract(&test->rayEnd, &test->rayStart, &test->dir);
    ushort count;
    cel_tag **cels = Collide_StraddleCels(test, &count);
    HITTEST_tag copy = *test;
    for (uint i = 0; i < count; i++) {
        cel_tag *room = cels[i];
        if (!(test->mask & 0x80)) {
            for (cel_tag *cel = room->childCel; cel != NULL; cel = cel->childCel) {
                celglist_tag *glist = cel->glist;
                if (glist == NULL || (cel->flags84 & CEL_NO_COLLISION) || glist->colldata == NULL)
                    continue;
                if ((cel->flags84 & CEL_MASKABLE) && (test->mask & 0x20))
                    continue;
                float dist;
                switch (test->type) {
                case HITTEST_RAY_BOXES:
                case HITTEST_RAY:
                    if (cel->bitsFromPlacementTag & CEL_WORLD_SPACE) {
                        if (Intersect_RayBox(test, &cel->someMin, &cel->someMax, &dist))
                            Vec_Copy(&test->dir, &Coll_AddHitToList(&tester->hitList, dist, cel, NULL)->maybehitDirection);
                    } else {
                        Vec_Copy(&cel->sphere, &copy.sphere);
                        copy.sphereRadius = cel->sphereRadius;
                        if (_Intersect_RaySphere(&copy, &dist))
                            Coll_AddHitToList(&tester->hitList, dist, cel, NULL);
                    }
                    break;
                case HITTEST_CYLINDER:
                    Vec_Copy(&cel->sphere, &copy.sphere);
                    copy.sphereRadius = cel->sphereRadius * 1.5f;
                    if (_Intersect_CylSphere(&copy, &dist))
                        Coll_AddHitToList(&tester->hitList, dist, cel, NULL);
                    break;
                case HITTEST_SPHERE_ONE_SIDED:
                case HITTEST_SPHERE:
                case HITTEST_SPHERE_BOXES:
                    if (cel->bitsFromPlacementTag & CEL_WORLD_SPACE) {
                        if (Intersect_SphereBox(&test->sphere, test->sphereRadius, &cel->someMin, &cel->someMax))
                            Coll_AddHitToList(&tester->hitList, 1.0f, cel, NULL);
                    } else {
                        _VECTOR difference;
                        if (_Intersect_SphereSphere(&test->sphere, test->sphereRadius, &cel->sphere, cel->sphereRadius,
                                                    &difference, &dist))
                            Coll_AddHitToList(&tester->hitList, dist, cel, NULL);
                    }
                    break;
                }
            }
        }
        if (!(test->mask & 0x10)) {
            for (obj_tag *obj = room->firstObject; obj != NULL; obj = obj->nextInCel) {
                if (!(obj->effectFlags & FLAG_IN_FORCEDLIST))
                    _Collide_PickObj(test, obj, tester);
            }
        }
        // The cel's own geometry sorts first, in the order the cels were found
        if (!(test->mask & 0x40) && !(room->flags84 & CEL_NO_COLLISION) && room->glist != NULL)
            Coll_AddHitToList(&tester->hitList, (float)((double)(int)i * 0.1f - 1000.0f), room, NULL);
    }
    if (!(test->mask & 0x10)) {
        for (LLNODE_tag *node = ForcedList.head; node != NULL && node != node->next; node = node->next)
            _Collide_PickObj(test, (obj_tag *)node, tester);
    }
}

// The narrow phase against a skinned object without collision geometry of its own: a ray or a sphere against the
// joints' boxes, a cylinder against the other's cylinder, pushing this one out (all the way in single player, half
// in multiplayer)
bool _Collide_Jointy(HITTEST_tag *test, HITDATA_tag *hit) {
    hit->dist = HIT_NONE;
    float t = HIT_NONE;
    COLLJOINT_tag *joints = NULL;
    HITTEST_tag copy = *test;
    ushort count = 0;
    if (test->type & (HITTEST_RAY_BOXES | HITTEST_SPHERE_ONE_SIDED))
        joints = AnimGetCollData(hit->hitObj, &count);
    switch (test->type) {
    case HITTEST_SPHERE:
        for (ushort i = 0; i < count; i++) {
            COLLJOINT_tag *joint = &joints[i];
            if (!Intersect_PointOOBox(&joint->m, &joint->half, &test->pos, test->radius))
                continue;
            hit->materialType = 0;
            hit->hitBoneIdx = joint->joint;
            hit->dist = test->radius;
            hit->dmgAmt = test->damage;
            Vec_Copy(&test->pos, &hit->hitPosition);
            Vec_Subtract(&test->pos, Mat_Position(joint->m), &hit->surfaceNormal.normal);
            Vec_Normalise(&hit->surfaceNormal.normal, &hit->surfaceNormal.normal);
            Vec_Copy(&hit->surfaceNormal.normal, &hit->maybehitDirection);
            return true;
        }
        return false;
    case HITTEST_RAY:
        for (ushort i = 0; i < count; i++) {
            COLLJOINT_tag *joint = &joints[i];
            _MATRIX inverse;
            _VECTOR boxMin;
            Mat_Inverse(&joint->m, &inverse);
            RotPreTransVec(&inverse, &test->rayStart, &copy.rayStart);
            RotPreTransVec(&inverse, &test->rayEnd, &copy.rayEnd);
            ApplyMatrixLV(&inverse, &test->dir, &copy.dir);
            Vec_Negate(&joint->half, &boxMin);
            if (!Intersect_RayBox(&copy, &boxMin, &joint->half, &t))
                continue;
            hit->materialType = 0;
            hit->hitBoneIdx = joint->joint;
            hit->dmgAmt = test->damage;
            if (t == 0.0f) {
                Vec_Copy(&test->rayStart, &hit->hitPosition);
                hit->dist = -1.0f;
            } else {
                auxVec_AddMulR32(&test->rayStart, &test->dir, t, &hit->hitPosition);
                hit->dist = t * test->radius;
            }
            Vec_Normalise(&copy.dir, &copy.dir);
            Vec_Negate(&copy.dir, &hit->surfaceNormal.normal);
            return true;
        }
        return false;
    case HITTEST_CYLINDER: {
        obj_tag *other = hit->hitObj;
        if (other->maybeCollision == NULL || other->maybeCollision->type != HITTEST_CYLINDER)
            return false;
        obj_tag *self = test->tester;
        if (self->objectType == OBJECTTYPE_PLAYER && other->objectType == OBJECTTYPE_PLAYER) {
            float mine = Vec_Magnitude(&((BLData *)self->extraObjectData)->movement);
            double theirSpeed = Vec_Magnitude(&((BLData *)other->extraObjectData)->movement);
            if (theirSpeed > mine)
                return false;
        }
        if (test->tester->objectType == OBJECTTYPE_DRONE && other->objectType == OBJECTTYPE_DRONE &&
            ((Drone_tag *)test->tester->extraObjectData)->moved < ((Drone_tag *)other->extraObjectData)->moved)
            return false;
        HITTEST_tag *theirs = other->maybeCollision;
        float share;
        if (MPSettings.isMultiplayer) {
            share = 0.5f;
        } else {
            share = 1.0f;
            if (other->objectType == OBJECTTYPE_PLAYER)
                return false;
        }
        _VECTOR onTheirs, onMine, normal, push;
        vecutil_calculate_closest_point_on_line(&onTheirs, &theirs->rayStart, &theirs->rayEnd, &test->rayStart);
        vecutil_calculate_closest_point_on_line(&onMine, &test->rayStart, &test->rayEnd, &onTheirs);
        vecutil_calculate_closest_point_on_line(&onTheirs, &theirs->rayStart, &theirs->rayEnd, &onMine);
        Vec_Subtract(&onTheirs, &onMine, &normal);
        float reach = theirs->radius + test->radius;
        if ((double)reach * reach < Vec_SqMagnitude(&normal))
            return false;
        double length = Vec_NormaliseLen(&normal, &normal);
        float depth = (float)(((double)test->radius + theirs->radius) - length);
        Vec_MulR32(&push, &normal, (float)-((double)share * depth));
        Vec_Add2(&test->rayStart, &push, &test->rayStart);
        Vec_Add2(&test->rayEnd, &push, &test->rayEnd);
        Vec_Add2(&test->push, &push, &test->push);
        Vec_Copy(&normal, &hit->surfaceNormal.normal);
        hit->dist = depth;
        hit->hitPosition.x = (float)((double)normal.x * share * depth + test->tester->position.x);
        hit->hitPosition.z = (float)((double)normal.z * share * depth + test->tester->position.z);
        hit->hitPosition.y = (float)((double)normal.y * share * depth + test->tester->position.y);
        return true;
    }
    default:
        return false;
    }
}

// The hit in EAX
// AUTOLTCG
void __declspec(naked) Collide_Jointy(void) {
    _asm {
        push eax
        push dword ptr [esp + 8]
        call _Collide_Jointy
        add esp, 8
        ret
    }
}

// The narrow phase of one candidate: the test goes into the cel's or object's space (none for a cel in world space),
// meets the geometry, and the hit and the push come back out; false if the hit is rejected
bool _Collide_Intersect(HITTEST_tag *test, HITDATA_tag *hit, ushort flags) {
    _MATRIX mtx;
    Ident = 0;
    cel_tag *cel = hit->hitCel;
    if (cel != NULL) {
        Ident = (cel->bitsFromPlacementTag & CEL_WORLD_SPACE) != 0;
        if (cel->flags84 & CEL_NO_CONTACT)
            hit->flags |= HIT_NO_CONTACT;
        if (!Ident)
            RotTransMatrix(&cel->rotation, &cel->position, &mtx);
        else
            Mat_IdentityT(&mtx);
        test->glist = hit->hitCel->glist;
    } else {
        obj_tag *obj = hit->hitObj;
        if (obj == NULL || obj->objGraphics == NULL)
            return false;
        if (obj->effectFlags & FLAG_UNKNOWN_40)
            hit->flags |= HIT_NO_CONTACT;
        if (test->type != HITTEST_SPHERE_ONE_SIDED && (obj->transformFlags & TRANSFORM_ANIMATED) &&
            obj->objGraphics->colldata == NULL)
            return _Collide_Jointy(test, hit);
        Mat_Copy(&obj->transformMatrix, &mtx);
        test->glist = hit->hitObj->objGraphics;
        if ((hit->hitObj->unknown_0xd9 & 4) && test->type == HITTEST_SPHERE) {
            // Sphere against sphere
            Vec_Subtract(&test->sphere, &hit->hitObj->centrePoint, &DeltaP);
            hit->dist = Vec_NormaliseLen(&hit->surfaceNormal.normal, &DeltaP);
            auxVec_AddMulR32(&test->sphere, &DeltaP, 0.5f, &hit->hitPosition);
            float depth = (float)(((double)hit->hitObj->radius + test->sphereRadius) - hit->dist);
            hit->dist = depth;
            auxVec_AddMulR32(&test->push, &hit->surfaceNormal.normal, depth, &test->push);
            return true;
        }
    }
    HITTEST_tag local = *test;
    local.matrix = &mtx;
    Mat_InverseSelf(local.matrix);
    if (Ident)
        Vec_Copy(&test->pos, &local.pos);
    else
        RotPreTransVec(local.matrix, &test->pos, &local.pos);
    if (Ident)
        Vec_Copy(&test->sphere, &local.sphere);
    else
        RotPreTransVec(local.matrix, &test->sphere, &local.sphere);
    hit->flags |= flags;
    hit->dist = test->radius;
    switch (test->type) {
    case HITTEST_POINT:
        hit->dist = HIT_NONE;
        break;
    case HITTEST_SPHERE:
        hit->dist = HIT_NONE;
        Mat_InverseSelf(local.matrix);
        Intersect_PointGeom(&local, hit);
        break;
    case HITTEST_RAY:
        hit->dist = HIT_NONE;
        if (Ident)
            Vec_Copy(&test->rayStart, &local.rayStart);
        else
            RotPreTransVec(local.matrix, &test->rayStart, &local.rayStart);
        if (Ident)
            Vec_Copy(&test->rayEnd, &local.rayEnd);
        else
            RotPreTransVec(local.matrix, &test->rayEnd, &local.rayEnd);
        Vec_Subtract(&local.rayEnd, &local.rayStart, &local.dir);
        Mat_InverseSelf(local.matrix);
        Intersect_RayGeom(&local, hit);
        break;
    case HITTEST_CYLINDER_TRIS:
        hit->dist = HIT_NONE;
        if (Ident)
            Vec_Copy(&test->rayStart, &local.rayStart);
        else
            RotPreTransVec(local.matrix, &test->rayStart, &local.rayStart);
        if (Ident)
            Vec_Copy(&test->rayEnd, &local.rayEnd);
        else
            RotPreTransVec(local.matrix, &test->rayEnd, &local.rayEnd);
        Mat_InverseSelf(local.matrix);
        Intersect_CylTriGeom(&local, hit);
        return true;
    case HITTEST_CYLINDER:
        hit->dist = HIT_NONE;
        if (Ident)
            Vec_Copy(&test->rayStart, &local.rayStart);
        else
            RotPreTransVec(local.matrix, &test->rayStart, &local.rayStart);
        if (Ident)
            Vec_Copy(&test->rayEnd, &local.rayEnd);
        else
            RotPreTransVec(local.matrix, &test->rayEnd, &local.rayEnd);
        Mat_InverseSelf(local.matrix);
        Intersect_CylGeom(&local, hit);
        break;
    default:
        // A one-sided sphere, against the object's sphere: the gap between them
        if (hit->hitObj != NULL) {
            Vec_Copy(&hit->hitObj->centrePoint, &hit->hitPosition);
            double gap = sqrt((double)hit->dist) - ((double)hit->hitObj->radius + test->radius);
            hit->dist = gap > 0.0 ? (float)gap : 0.0f;
        }
        return true;
    }
    Intersect_CheckIfFlagsMatch(hit->materialType, hit->flags, test->mask, &hit->dist);
    if (hit->dist == HIT_NONE)
        return false;
    hit->dmgAmt = test->damage;
    _VECTOR v;
    Vec_Copy(&hit->hitPosition, &v);
    if (Ident)
        Vec_Copy(&v, &hit->hitPosition);
    else
        MatrixMultiplyVector(local.matrix, &v, &hit->hitPosition);
    if (Ident)
        Vec_Copy(&DeltaP, &v);
    else
        ApplyMatrixLV(local.matrix, &DeltaP, &v);
    Vec_Add2(&v, &test->push, &test->push);
    Vec_Add2(&test->rayStart, &v, &test->rayStart);
    Vec_Add2(&test->rayEnd, &v, &test->rayEnd);
    Vec_Add2(&test->pos, &v, &test->pos);
    Vec_Add2(&test->sphere, &v, &test->sphere);
    Vec_Copy(&hit->surfaceNormal.normal, &v);
    if (Ident)
        Vec_Copy(&v, &hit->surfaceNormal.normal);
    else
        ApplyMatrixLV(local.matrix, &v, &hit->surfaceNormal.normal);
    test->found |= local.found;
    if (test->type & HITTEST_RAY_BOXES)
        Vec_Normalise(&hit->maybehitDirection, &test->dir);
    return true;
}

// The hit in EAX
// AUTOLTCG
void __declspec(naked) Collide_Intersect(void) {
    _asm {
        push dword ptr [esp + 8]
        push eax
        push dword ptr [esp + 12]
        call _Collide_Intersect
        add esp, 12
        ret
    }
}

// A sphere (the test's sphere, in its geometry's space) against the triangles of the leaf boxes it touches: each
// triangle within the radius pushes it out along its normal (DeltaP adds up the pushes); the hit is the nearest.
// With the test's flag 0x10 the first triangle its material does not let through ends the search.
// AUTOINJECT
void Intersect_PointGeom(HITTEST_tag *test, HITDATA_tag *hit) {
    float best = test->radius;
    float rejected = 0.0f;
    uchar *bestBlock = NULL;
    float least = (test->flags & 8) ? -test->radius : 0.0f;
    COLLDATA_tag *coll = test->glist->colldata;
    BoxCnt = 0;
    _VECTOR *centre = &test->sphere;
    RecurseBoxesSph(coll, coll->collBoxes, centre, test->sphereRadius);
    if (BoxCnt == 0) {
        hit->dist = HIT_NONE;
        return;
    }
    Vec_Zero(&DeltaP);
    hit->dist = test->radius;
    int bestTri = -1;
    _VECTOR bestPos;
    for (uint b = 0; b < BoxCnt; b++) {
        uchar *block = coll->dataStartC[ColBoxs[b].box->vertexBlock].raw;
        // A search ended early sets b to BoxCnt, and this test then reads the entry after the last
        for (uint t = ColBoxs[b].box->childB; t < (uint)ColBoxs[b].box->triEnd; t++) {
            // The material of the best triangle so far (before the first, the byte before the table)
            if ((test->flags & 0x10) && (coll->dataStartD[bestTri] & 0xc0))
                continue;
            collDataB *tri = &coll->dataStartB[t];
            plane_equ_tag plane;
            Intersect_UnpackTrianglePlaneEq(block, tri->normal, &plane);
            double planeDist = DistancePointToPlane(centre, &plane);
            float planeDistStored = (float)planeDist;
            if (planeDist > test->radius)
                continue;
            if (planeDistStored <= least)
                continue;
            _VECTOR verts[3];
            Intersect_UnpackTriangleVerticesToWorld(block, tri->v[0], tri->v[1], tri->v[2], verts,
                                                    &ColBoxs[b].box->quant);
            double dist = vecutil_Dist2Tri_PointGeom(centre, &verts[0], &verts[1], &verts[2], &plane, planeDistStored,
                                                     &hit->hitPosition);
            float distStored = (float)dist;
            if (dist < least)
                continue;
            if (distStored > test->radius)
                continue;
            if (!(hit->flags & (HIT_NO_CONTACT | HIT_NO_PUSH))) {
                auxVec_AddMulR32(&DeltaP, &plane.normal, (float)((double)test->radius - AbsFchs(distStored)), &DeltaP);
                Vec_Add2(centre, &DeltaP, centre);
            }
            if (!(distStored <= best))
                continue;
            best = distStored;
            bestTri = t;
            bestBlock = block;
            Vec_Copy(&hit->hitPosition, &bestPos);
            if ((test->flags & 0x10) && !Intersect_CheckIfFlagsMatch(coll->dataStartD[t], hit->flags, test->mask, &rejected)) {
                t = ColBoxs[b].box->triEnd;
                b = BoxCnt;
            }
        }
    }
    if (bestTri == -1) {
        hit->dist = HIT_NONE;
        return;
    }
    hit->dist = best;
    auxVec_AddMulR32(centre, &hit->surfaceNormal.normal, -test->radius, &hit->hitPosition);
    hit->materialType = coll->dataStartD[bestTri];
    Intersect_UnpackTrianglePlaneEq(bestBlock, coll->dataStartB[bestTri].normal, &hit->surfaceNormal);
    Vec_Copy(&bestPos, &hit->hitPosition);
}

// The ray (rayStart along dir, in its geometry's space) against the triangles facing it in the leaf boxes it crosses:
// the hit is the nearest. With the test's flag 0x10 the first triangle its material does not let through ends the
// search.
// AUTOINJECT
void Intersect_RayGeom(HITTEST_tag *test, HITDATA_tag *hit) {
    COLLDATA_tag *coll = test->glist->colldata;
    BoxCnt = 0;
    float bestT = 1.0f;
    float rejected = 0.0f;
    uchar *bestBlock = NULL;
    ASH_RecurseBoxesRay(coll, coll->collBoxes, test);
    if (BoxCnt == 0) {
        hit->dist = HIT_NONE;
        return;
    }
    int bestTri = -1;
    for (uint b = 0; b < BoxCnt; b++) {
        uchar *block = coll->dataStartC[ColBoxs[b].box->vertexBlock].raw;
        for (uint t = ColBoxs[b].box->childB; t < (uint)ColBoxs[b].box->triEnd; t++) {
            if ((test->flags & 0x10) && (coll->dataStartD[bestTri] & 0xc0))
                continue;
            collDataB *tri = &coll->dataStartB[t];
            Intersect_UnpackTrianglePlaneEq(block, tri->normal, &test->plane);
            double planeDist = DistancePointToPlane(&test->rayStart, &test->plane);
            double towards = ((double)test->dir.z * test->plane.c + (double)test->dir.y * test->plane.b) +
                             (double)test->plane.a * test->dir.x;
            if (towards >= 0.0)
                continue;
            float rayT = (float)Negate(planeDist / towards);
            if (rayT < 0.0f)
                continue;
            if (rayT > 1.0f)
                continue;
            auxVec_AddMulR32(&test->rayStart, &test->dir, rayT, &test->pos);
            if (!(rayT < bestT))
                continue;
            _VECTOR verts[3];
            Intersect_UnpackTriangleVerticesToWorld(block, tri->v[0], tri->v[1], tri->v[2], verts,
                                                    &ColBoxs[b].box->quant);
            if (!vecutil_point_on_poly(&test->pos, &verts[0], &verts[1], &verts[2], &test->plane))
                continue;
            bestT = rayT;
            hit->hitPosition = test->pos;
            bestTri = t;
            bestBlock = block;
            if ((test->flags & 0x10) && !Intersect_CheckIfFlagsMatch(coll->dataStartD[t], hit->flags, test->mask, &rejected)) {
                t = ColBoxs[b].box->triEnd;
                b = BoxCnt;
            }
        }
    }
    if (bestTri == -1) {
        hit->dist = HIT_NONE;
        return;
    }
    hit->dist = Vec_Dist3D(&test->rayStart, &hit->hitPosition);
    hit->materialType = coll->dataStartD[bestTri];
    Intersect_UnpackTrianglePlaneEq(bestBlock, coll->dataStartB[bestTri].normal, &hit->surfaceNormal);
}

// The cylinder (rayStart to rayEnd and its radius, in its geometry's space) against the triangles of the leaf boxes
// its bounds touch: for each, the point of the axis nearest the triangle (where it crosses the plane, held to the
// ends; nearly parallel, the nearest to the centroid). Nearer than the radius, the cylinder is pushed out along the
// line from the triangle's point (DeltaP adds up the pushes), and found bit 1 marks a floor under its axis. The hit
// is the nearest; its direction is against the normal.
// AUTOINJECT
void Intersect_CylGeom(HITTEST_tag *test, HITDATA_tag *hit) {
    COLLDATA_tag *coll = test->glist->colldata;
    float best = test->radius;
    float rejected = 0.0f;
    uchar *bestBlock = NULL;
    Vec_Zero(&DeltaP);
    BoxCnt = 0;
    _VECTOR boxMin, boxMax, up, dir;
    Vec_MinMax(&test->rayStart, &test->rayEnd, &boxMin, &boxMax);
    double grow = (double)test->radius + 0.1f;
    boxMin.x = (float)(boxMin.x - grow);
    boxMin.y = (float)(boxMin.y - grow);
    boxMin.z = (float)(boxMin.z - grow);
    boxMax.x = (float)(((double)boxMax.x + test->radius) + 0.1f);
    boxMax.y = (float)(((double)boxMax.y + test->radius) + 0.1f);
    boxMax.z = (float)(((double)boxMax.z + test->radius) + 0.1f);
    ASH_RecurseBoxesBox(coll, coll->collBoxes, &boxMin, &boxMax);
    Vec_Subtract(&test->rayStart, &test->rayEnd, &up);
    Vec_Normalise(&up, &up);
    if (BoxCnt == 0) {
        hit->dist = HIT_NONE;
        return;
    }
    Vec_Subtract(&test->rayEnd, &test->rayStart, &dir);
    int bestTri = -1;
    for (uint b = 0; b < BoxCnt; b++) {
        uchar *block = coll->dataStartC[ColBoxs[b].box->vertexBlock].raw;
        for (uint t = ColBoxs[b].box->childB; t < (uint)ColBoxs[b].box->triEnd; t++) {
            collDataB *tri = &coll->dataStartB[t];
            _VECTOR verts[3];
            Intersect_UnpackTriangleVerticesToWorld(block, tri->v[0], tri->v[1], tri->v[2], verts,
                                                    &ColBoxs[b].box->quant);
            Intersect_UnpackTrianglePlaneEq(block, tri->normal, &test->plane);
            float startSide = (float)DistancePointToPlane(&test->rayStart, &test->plane);
            double endSide = DistancePointToPlane(&test->rayEnd, &test->plane);
            if (startSide < -(double)test->radius && endSide < -(double)test->radius)
                continue;
            if (startSide > test->radius && endSide > test->radius)
                continue;
            if (!_Intersect_AABB_SweptVolume(&verts[0], &verts[1], &verts[2], &boxMin, &boxMax))
                continue;
            float towards = (float)(((double)dir.y * test->plane.b + (double)dir.x * test->plane.a) +
                                    (double)dir.z * test->plane.c);
            _VECTOR axisPoint;
            ushort code;
            float dist;
            if (AbsFchs(towards) > 0.0002f) {
                float inverse = 1.0f / towards;
                float s;
                if (Negate(DistancePointToPlane(&test->rayStart, &test->plane) * inverse) < 0.0)
                    s = 0.0f;
                else if (Negate(DistancePointToPlane(&test->rayStart, &test->plane) * inverse) > 1.0)
                    s = 1.0f;
                else
                    s = (float)Negate(DistancePointToPlane(&test->rayStart, &test->plane) * inverse);
                // Running almost along the normal, only the end the axis leaves the plane by counts
                if (towards > 0.999f && s > 0.5f)
                    continue;
                if (towards < -0.999f && s < 0.5f)
                    continue;
                auxVec_AddMulR32(&test->rayStart, &dir, s, &axisPoint);
                dist = (float)vecutil_Dist2Tri(&axisPoint, &verts[0], &verts[1], &verts[2], &test->plane, &code, &test->pos);
                if (code != 0 && code != DIST2TRI_BEHIND)
                    vecutil_calculate_closest_point_on_line(&axisPoint, &test->rayStart, &test->rayEnd, &test->pos);
            } else {
                _VECTOR centroid;
                Vec_Avg3(&verts[0], &verts[1], &verts[2], &centroid);
                vecutil_calculate_closest_point_on_line(&axisPoint, &test->rayStart, &test->rayEnd, &centroid);
                dist = (float)vecutil_Dist2Tri(&axisPoint, &verts[0], &verts[1], &verts[2], &test->plane, &code, &test->pos);
            }
            if (dist < 0.0f)
                continue;
            if (code == DIST2TRI_BEHIND)
                continue;
            if (!(hit->flags & (HIT_NO_CONTACT | HIT_NO_PUSH))) {
                _VECTOR away;
                Vec_Subtract(&axisPoint, &test->pos, &away);
                if (((double)away.y * test->plane.b + (double)away.x * test->plane.a) + (double)away.z * test->plane.c <= 0.0)
                    continue;
                double length = Vec_NormaliseLen(&away, &away);
                dist = (float)length;
                if (length >= test->radius)
                    continue;
                Vec_MulR32(&away, &away, test->radius - dist);
                Vec_Add2(&DeltaP, &away, &DeltaP);
                Vec_Add2(&test->rayStart, &away, &test->rayStart);
                Vec_Add2(&test->rayEnd, &away, &test->rayEnd);
                Vec_Add2(&boxMin, &away, &boxMin);
                Vec_Add2(&boxMax, &away, &boxMax);
                if (test->found == 0) {
                    _VECTOR below;
                    Vec_Subtract(&test->pos, &test->rayEnd, &below);
                    if (((double)up.y * test->plane.b + (double)up.x * test->plane.a) + (double)up.z * test->plane.c > 0.7f &&
                        ((double)below.y * up.y + (double)below.z * up.z) + (double)below.x * up.x < 0.0) {
                        Vec_Normalise(&below, &below);
                        if (((double)below.y * up.y + (double)below.z * up.z) + (double)below.x * up.x < 0.7f)
                            test->found |= 1;
                    }
                }
            }
            if (!(dist < best))
                continue;
            best = dist;
            bestTri = t;
            bestBlock = block;
            Vec_Copy(&test->pos, &hit->hitPosition);
            if ((test->flags & 0x10) &&
                (!Intersect_CheckIfFlagsMatch(coll->dataStartD[t], hit->flags, test->mask, &rejected) ||
                 (coll->dataStartD[t] & 0xc0)))
                goto done;
        }
    }
done:
    if (bestTri == -1) {
        hit->dist = HIT_NONE;
        return;
    }
    hit->dist = best;
    hit->materialType = coll->dataStartD[bestTri];
    Intersect_UnpackTrianglePlaneEq(bestBlock, coll->dataStartB[bestTri].normal, &hit->surfaceNormal);
    Vec_Negate(&hit->surfaceNormal.normal, &hit->maybehitDirection);
}

// HITTEST_CYLINDER_TRIS: the triangles within the cylinder's radius of its axis, into TriHeap in world space (by
// the test's matrix), without pushes or a hit. Gives up past 500 vertices.
// AUTOINJECT
void Intersect_CylTriGeom(HITTEST_tag *test, HITDATA_tag *hit) {
    COLLDATA_tag *coll = test->glist->colldata;
    Vec_Zero(&DeltaP);
    BoxCnt = 0;
    _VECTOR boxMin, boxMax, dir;
    Vec_MinMax(&test->rayStart, &test->rayEnd, &boxMin, &boxMax);
    double grow = (double)test->radius + 0.05f;
    boxMin.x = (float)(boxMin.x - grow);
    boxMin.y = (float)(boxMin.y - grow);
    boxMin.z = (float)(boxMin.z - grow);
    boxMax.x = (float)(((double)boxMax.x + test->radius) + 0.05f);
    boxMax.y = (float)(((double)boxMax.y + test->radius) + 0.05f);
    boxMax.z = (float)(((double)boxMax.z + test->radius) + 0.05f);
    ASH_RecurseBoxesBox(coll, coll->collBoxes, &boxMin, &boxMax);
    if (BoxCnt == 0) {
        hit->dist = HIT_NONE;
        return;
    }
    Vec_Subtract(&test->rayEnd, &test->rayStart, &dir);
    for (uint b = 0; b < BoxCnt; b++) {
        uchar *block = coll->dataStartC[ColBoxs[b].box->vertexBlock].raw;
        for (uint t = ColBoxs[b].box->childB; t < (uint)ColBoxs[b].box->triEnd; t++) {
            if ((test->mask & 1) && ((coll->dataStartD[t] & 0x3f) == 13 || (coll->dataStartD[t] & 0x3f) == 14))
                continue;
            if ((test->mask & 2) && (coll->dataStartD[t] & 0x3f) == 16)
                continue;
            // The byte before the material table, for every triangle
            if ((test->flags & 0x10) && (coll->dataStartD[-1] & 0xc0))
                continue;
            collDataB *tri = &coll->dataStartB[t];
            _VECTOR verts[3];
            Intersect_UnpackTriangleVerticesToWorld(block, tri->v[0], tri->v[1], tri->v[2], verts,
                                                    &ColBoxs[b].box->quant);
            if (!_Intersect_AABB_SweptVolume(&verts[0], &verts[1], &verts[2], &boxMin, &boxMax))
                continue;
            if (!Intersect_UnpackTrianglePlaneEq(block, tri->normal, &test->plane))
                continue;
            double towards = ((double)dir.y * test->plane.b + (double)dir.z * test->plane.c) + (double)dir.x * test->plane.a;
            _VECTOR axisPoint;
            ushort code;
            double dist;
            if (AbsFchs(towards) > 0.05f) {
                float inverse = (float)(1.0 / towards);
                float s;
                if (Negate(DistancePointToPlane(&test->rayStart, &test->plane) * inverse) < 0.0)
                    s = 0.0f;
                else if (Negate(DistancePointToPlane(&test->rayStart, &test->plane) * inverse) > 1.0)
                    s = 1.0f;
                else
                    s = (float)Negate(DistancePointToPlane(&test->rayStart, &test->plane) * inverse);
                auxVec_AddMulR32(&test->rayStart, &dir, s, &axisPoint);
                dist = vecutil_Dist2Tri(&axisPoint, &verts[0], &verts[1], &verts[2], &test->plane, &code, &test->pos);
            } else {
                if (!(test->flags & 1))
                    continue;
                _VECTOR centroid;
                Vec_Avg3(&verts[0], &verts[1], &verts[2], &centroid);
                vecutil_calculate_closest_point_on_line(&axisPoint, &test->rayStart, &test->rayEnd, &centroid);
                dist = vecutil_Dist2Tri(&axisPoint, &verts[0], &verts[1], &verts[2], &test->plane, &code, &test->pos);
            }
            if (dist < -0.0002f)
                continue;
            if (dist > test->radius)
                continue;
            if (TriOnCnt + 3 >= TriAllocCnt) {
                if (TriAllocCnt >= 501)
                    return;
                // Not checked: an allocation that fails
                _VECTOR *grown = (_VECTOR *)Mem_Malloc((TriAllocCnt + 100) * sizeof(_VECTOR), (MallocFlags)0x1a04, 0);
                memset(grown, 0, (TriAllocCnt + 100) * sizeof(_VECTOR));
                memcpy(grown, TriHeap, TriAllocCnt * sizeof(_VECTOR));
                Mem_Free((void **)&TriHeap);
                TriHeap = grown;
                TriAllocCnt += 100;
            }
            if (TriOnCnt + 3 >= 501)
                continue;
            if (Ident)
                Vec_Copy(&verts[0], &TriHeap[TriOnCnt]);
            else
                MatrixMultiplyVector(test->matrix, &verts[0], &TriHeap[TriOnCnt]);
            if (Ident)
                Vec_Copy(&verts[1], &TriHeap[TriOnCnt + 1]);
            else
                MatrixMultiplyVector(test->matrix, &verts[1], &TriHeap[TriOnCnt + 1]);
            if (Ident)
                Vec_Copy(&verts[2], &TriHeap[TriOnCnt + 2]);
            else
                MatrixMultiplyVector(test->matrix, &verts[2], &TriHeap[TriOnCnt + 2]);
            TriOnCnt += 3;
        }
    }
}

// Collide_Intersect on each candidate of the tester's list (only the test's own cel's with onlyCel); with flags
// 0x10 the first hit ends the list. Then the list is sorted.
void _Collide_TestHits(HITTEST_tag *test, ushort flags, char onlyCel) {
    Collide_Pick(test);
    for (HITDATA_tag *hit = test->tester->hitList; hit != NULL; hit = hit->next) {
        if (hit->hitCel != NULL && hit->hitCel != test->cel && onlyCel) {
            hit->dist = HIT_NONE;
            continue;
        }
        if (_Collide_Intersect(test->tester->maybeCollision, hit, flags) && (flags & 0x10)) {
            for (hit = hit->next; hit != NULL; hit = hit->next)
                hit->dist = HIT_NONE;
            break;
        }
    }
    _Collide_Sort(&test->tester->hitList);
}

// The flags in EAX, the test in EDI. Our name (Ghidra had Collide_CylinderIntersect, which is 0x2c6f0).
// AUTOLTCG
void __declspec(naked) Collide_TestHits(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        push edi
        call _Collide_TestHits
        add esp, 12
        ret
    }
}

// The hits along a ray, nearest first
// AUTOINJECT
bool Collide_RayIntersect(_VECTOR *startPosition, _VECTOR *endPosition, cel_tag *cel, obj_tag *obj, obj_tag *param_5, HITDATA_tag **hitDataOut, char onlyCel, ushort mask, ushort flags) {
    HITTEST_tag test;
    obj_tag tester;
    memset(&test, 0, sizeof(test));
    memset(&tester, 0, sizeof(tester));
    test.cel = cel;
    test.tester = &tester;
    test.mask = mask;
    test.ignore = param_5;
    test.ignoreA = obj;
    *hitDataOut = NULL;
    test.type = HITTEST_RAY;
    test.radius = -10.0f;
    tester.maybeCollision = &test;
    tester.hitList = NULL;
    Vec_Copy(startPosition, &test.rayStart);
    Vec_Copy(endPosition, &test.rayEnd);
    _Collide_TestHits(&test, flags, onlyCel);
    *hitDataOut = tester.hitList;
    return tester.hitList != NULL;
}

// The hits on a sphere, nearest first; the centre moves by the geometry's push
// AUTOINJECT
bool Collide_SphereIntersect(_VECTOR *centre, float radius, cel_tag *cel, obj_tag *ignoreA, obj_tag *ignoreB, HITDATA_tag **hitList, char onlyCel, char oneSided, ushort mask, ushort flags) {
    HITTEST_tag test;
    obj_tag tester;
    memset(&test, 0, sizeof(test));
    memset(&tester, 0, sizeof(tester));
    test.type = oneSided ? HITTEST_SPHERE_ONE_SIDED : HITTEST_SPHERE;
    test.cel = cel;
    test.radius = radius;
    test.ignore = ignoreB;
    test.ignoreA = ignoreA;
    test.tester = &tester;
    test.mask = mask;
    test.flags = flags;
    *hitList = NULL;
    tester.maybeCollision = &test;
    tester.hitList = NULL;
    Vec_Copy(centre, &test.pos);
    _Collide_TestHits(&test, flags, onlyCel);
    Vec_Add2(&test.push, centre, centre);
    *hitList = tester.hitList;
    return tester.hitList != NULL;
}

// The hits on the test as a cylinder (rayStart, rayEnd and radius set by the caller), nearest first. Leaves the
// test's tester pointing at this function's own, gone when it returns.
// AUTOINJECT
bool Collide_CylinderIntersect(HITTEST_tag *test, HITDATA_tag **hitList, char onlyCel, ushort flags) {
    obj_tag tester;
    memset(&tester, 0, sizeof(tester));
    *hitList = NULL;
    test->tester = &tester;
    test->type = HITTEST_CYLINDER;
    tester.maybeCollision = test;
    tester.hitList = *hitList;
    _Collide_TestHits(test, flags, onlyCel);
    *hitList = tester.hitList;
    return tester.hitList != NULL;
}

// The triangles round a cylinder, in world space: TriHeap, and how many in count (at most 166). Our name (Ghidra had
// Collide_unknown).
// AUTOINJECT
_VECTOR* Collide_CylinderTris(_VECTOR *start, _VECTOR *end, float radius, cel_tag *cel, uint *count, ushort mask, ushort flags) {
    HITTEST_tag test;
    obj_tag tester;
    memset(&test, 0, sizeof(test));
    memset(&tester, 0, sizeof(tester));
    test.radius = radius;
    test.tester = &tester;
    test.type = HITTEST_CYLINDER;
    test.cel = cel;
    test.ignoreA = NULL;
    test.ignore = NULL;
    test.mask = mask;
    test.flags = flags;
    tester.maybeCollision = &test;
    tester.hitList = NULL;
    if (cel == NULL)
        test.cel = build_FindCel(start, glb_world);
    TriOnCnt = 0;
    Vec_Copy(start, &test.rayStart);
    Vec_Copy(end, &test.rayEnd);
    Collide_Pick(&test);
    test.type = HITTEST_CYLINDER_TRIS;
    for (HITDATA_tag *hit = tester.hitList; hit != NULL; hit = hit->next)
        _Collide_Intersect(test.tester->maybeCollision, hit, flags);
    Collide_FreeHitList(&tester.hitList);
    uint tris = TriOnCnt / 3;
    *count = tris < 0xa6 ? tris : 0xa6;
    return TriHeap;
}

// Whether an object of the radius and half-width extent can stand here (GT_DeployMiniGun's turret): the floor up to
// 3 below, then the triangles round the spot - none may cut in by more than 0.13, and those level with the floor
// and near it must reach extent either side along the matrix's norm and direction. pos goes onto the floor, and
// the matrix stands on it.
// AUTOINJECT
bool Collide_PlaceObject(_VECTOR *pos, _MATRIX *mtx, float radius, float extent, obj_tag *obj) {
    _VECTOR dir, up, below, floorNormal, boxMin, boxMax, norm;
    HITDATA_tag *hits;
    pos->y = pos->y + 0.05f;
    Mat_GetDir(&dir, mtx);
    Mat_GetUp(&up, mtx);
    cel_tag *cel = build_FindCel(pos, glb_world);
    auxVec_AddMulR32(pos, &up, -3.0f, &below);
    if (!Collide_RayIntersect(pos, &below, cel, obj, NULL, &hits, 0, 0x1d, 0))
        return false;
    Vec_Copy(&hits->surfaceNormal.normal, &up);
    Vec_Normalise(&up, &up);
    auxVec_AddMulR32(pos, &up, -0.1f, &below);
    auxVec_AddMulR32(&hits->hitPosition, &up, radius, pos);
    Vec_Copy(&hits->surfaceNormal.normal, &floorNormal);
    Vec_Normalise(&floorNormal, &floorNormal);
    cel_tag *floorCel = hits->hitCel;
    Collide_FreeHitList(&hits);
    Vec_Copy2(pos, &boxMin, &boxMax);
    uint count;
    _VECTOR *tris = Collide_CylinderTris(pos, &below, extent, floorCel, &count, 0xd, 1);
    for (uint i = 0; i < count * 3; i += 3) {
        _VECTOR *tri = &tris[i];
        plane_equ_tag plane;
        Plane_PlaneEq(&plane, &tri[0], &tri[1], &tri[2]);
        double gap = (DistancePointToPlane(pos, &plane) >= 0.0 ? DistancePointToPlane(pos, &plane)
                                                               : Negate(DistancePointToPlane(pos, &plane))) - radius;
        if (gap < -0.13f)
            return false;
        if (gap > 0.2f)
            continue;
        if (AbsFchs(((double)floorNormal.z * plane.c + (double)floorNormal.y * plane.b) + (double)floorNormal.x * plane.a) < 0.96f)
            continue;
        _VECTOR triMin, triMax;
        Vec_MinMax3(&tri[0], &tri[1], &tri[2], &triMin, &triMax);
        Vec_Min(&boxMin, &triMin, &boxMin);
        Vec_Max(&boxMax, &triMax, &boxMax);
    }
    Mat_Align2Up(mtx->m, &up.x, &dir.x);
    Mat_GetDir(&dir, mtx);
    Mat_GetNorm(&norm, mtx);
    Vec_Subtract(&boxMin, pos, &boxMin);
    Vec_Subtract(&boxMax, pos, &boxMax);
    if (AbsFchs(((double)boxMin.z * dir.z + (double)boxMin.y * dir.y) + (double)boxMin.x * dir.x) +
        AbsFchs(((double)norm.z * boxMin.z + (double)norm.y * boxMin.y) + (double)norm.x * boxMin.x) < extent)
        return false;
    if (AbsFchs(((double)boxMax.z * dir.z + (double)boxMax.y * dir.y) + (double)boxMax.x * dir.x) +
        AbsFchs(((double)boxMax.z * norm.z + (double)boxMax.y * norm.y) + (double)boxMax.x * norm.x) < extent)
        return false;
    return true;
}

// The collision of the objects' tests for the tick: each hit on another object is copied to that object's list (to
// the first hit the test does not pass through); then each object's collision handler (control_funcs)
// AUTOINJECT
void Collide_Update(obj_tag *first) {
    for (obj_tag *obj = first; obj != NULL; obj = obj->nextObject) {
        Collide_FreeHitList(&obj->hitList);
        if (obj->maybeCollision != NULL) {
            obj->maybeCollision->tester = obj;
            Vec_Zero(&obj->maybeCollision->push);
        }
    }
    // GC check (0x801071ec): every hit back on the heap
    NF_WARN_IF(HitHeap.count != HitAllocCnt,"Someone hasn't freed there hitlist!\n");
    for (obj_tag *obj = first; obj != NULL; obj = obj->nextObject) {
        HITTEST_tag *test = obj->maybeCollision;
        if (test == NULL || test->type == 0)
            continue;
        test->found = 0;
        Collide_Pick(obj->maybeCollision);
        for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
            if (hit->flags & HIT_COPIED)
                continue;
            if (_Collide_Intersect(obj->maybeCollision, hit, test->flags) && (test->flags & 0x10)) {
                for (hit = hit->next; hit != NULL; hit = hit->next)
                    hit->dist = HIT_NONE;
                break;
            }
        }
        _Collide_Sort(&obj->hitList);
        for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
            obj_tag *other = hit->hitObj;
            if (other != NULL) {
                HITDATA_tag *copy = Coll_AddHitToList(&other->hitList, hit->dist, NULL, obj);
                copy->flags |= HIT_COPIED;
                copy->dmgAmt = obj->maybeCollision->damage;
                copy->materialType = hit->materialType;
                copy->hitBoneIdx = hit->hitBoneIdx;
                Vec_Copy(&hit->hitPosition, &copy->hitPosition);
                Vec_Copy(&hit->surfaceNormal.normal, &copy->surfaceNormal.normal);
                Vec_Copy(&hit->maybehitDirection, &copy->maybehitDirection);
            }
            // The test passes through a hit without contact, material 0x40, at random material 0x80, and with its
            // flag 0x20 surface 16
            uint passes = ((hit->materialType & 0x40) || (hit->flags & HIT_NO_CONTACT)) ? 1 : 0;
            if (hit->materialType & 0x80)
                passes |= Rand_Rand(0x3f) & 1;
            if ((test->flags & 0x20) && hit->materialType == 16)
                passes |= 1;
            if (!passes)
                break;
        }
    }
    for (obj_tag *obj = first; obj != NULL; obj = obj->nextObject) {
        if (control_funcs[(uchar)obj->objectType].collideFunc != NULL)
            control_funcs[(uchar)obj->objectType].collideFunc(obj);
    }
}

// Rejects (HIT_NONE) the hits by bullets of the weapon classes in flags, and sorts the list again if any were
// AUTOINJECT
void Collide_FilterBullets(HITDATA_tag **hitList, ushort flags) {
    bool filtered = false;
    for (HITDATA_tag *hit = *hitList; hit != NULL; hit = hit->next) {
        obj_tag *obj = hit->hitObj;
        if (obj == NULL || obj->objectType != OBJECTTYPE_BULLET)
            continue;
        if (((BU_tag *)obj->extraObjectData)->wpnDef->damageClass & flags) {
            hit->dist = HIT_NONE;
            filtered = true;
        }
    }
    if (filtered)
        _Collide_Sort(hitList);
}

// The damage of the hits, and (with objectList) the objects hit, up to maxObjectsInList
// AUTOINJECT
double Collide_GetDamageNObjects(HITDATA_tag *hitDatas, obj_tag **objectList, ushort *objectListCountOut, ushort maxObjectsInList) {
    double damage = 0.0;
    if (objectListCountOut != NULL)
        *objectListCountOut = 0;
    for (HITDATA_tag *hit = hitDatas; hit != NULL; hit = hit->next) {
        damage += hit->dmgAmt;
        if (hit->hitObj != NULL && objectList != NULL && objectListCountOut != NULL &&
            *objectListCountOut < maxObjectsInList)
            objectList[(*objectListCountOut)++] = hit->hitObj;
    }
    return damage;
}

// AUTOINJECT
bool Collide_LineOfSight(_VECTOR *param_1, _VECTOR *param_2, cel_tag *param_3, obj_tag *param_4, obj_tag *param_5, uint param_6) {
  HITDATA_tag *hitData = NULL;
  bool intersects = Collide_RayIntersect(param_1, param_2, param_3, param_4, param_5, &hitData, 0, param_6 | 4, 0x10);
  if (intersects) {
    // Discard the results, we only care whether an intersection occurred
    Collide_FreeHitList(&hitData);
  }
  return !intersects;
}

// Are the points on opposite sides of the plane, and does their line segment intersect the region bounded?
// AUTOINJECT
bool Collide_RayTriangle(_VECTOR *ptStart,_VECTOR *ptEnd,_VECTOR *vtx1,_VECTOR *vtx2,_VECTOR *vtx3, float *distanceOut) {
  _VECTOR intersectPt;
  plane_equ_tag planeEq;

  Plane_PlaneEq(&planeEq, vtx1, vtx2, vtx3);
  // Tested unrounded, divided by as a float
  double dot = Vec_Dot(ptEnd, &planeEq.normal);
  if (dot != 0.0) {
    float dist = (float)dot;
    float vDist = (float)-(DistancePointToPlane(ptStart, &planeEq) / dist);
    *distanceOut = vDist;
    auxVec_AddMulR32(ptStart, ptEnd, vDist, &intersectPt);
    return vecutil_point_on_poly(&intersectPt, vtx1, vtx2, vtx3, &planeEq);
  }

  return false;
}

// Whether a sphere (its radius after its centre) touches a cone: its apex, its unit direction and the cosine and sine
// of its half angle. Inside the sphere counts. The x87's values are kept where it keeps them; the distance's square
// is compared unrounded with the radius's, rounded with the rest.
// AUTOINJECT
bool Intersect_ConeSphere(_VECTOR *apex, _VECTOR *dir, float cosA, float sinA, _VECTOR *sphere) {
    _VECTOR toCentre;
    Vec_Subtract(sphere, apex, &toCentre);
    float radius = ((sphere_equ_tag *)sphere)->r;
    float radiusSq = radius * radius;
    double distSq = Vec_SqMagnitude(&toCentre);
    float distSqf = (float)distSq;
    if (distSq <= radiusSq)
        return true;

    double along = (double)toCentre.y * dir->y + (double)toCentre.z * dir->z + (double)toCentre.x * dir->x;
    double alongSq = along * along;
    if (alongSq >= (double)cosA * cosA * distSqf && along > 0.0)
        return true;

    double edge = sqrt(AbsFchs(distSqf - alongSq)) * sinA + along * cosA;
    if (!(edge * edge - distSqf + radiusSq >= 0.0))
        return false;
    return edge >= 0.0;
}

// Empties the hit heap (entries of 0x50 bytes, none preallocated - Coll_GetHit grows it 0x40 at a time) and
// allocates a fresh, zeroed triangle buffer of 100 entries.
// AUTOINJECT
void Coll_ResetHitHeap(void) {
    HitAllocCnt = 0;
    LList_Init(&HitHeap, 0x50, 0);
    TriAllocCnt = 100;
    TriHeap = (_VECTOR *)Mem_Malloc(0x4b0, (MallocFlags)0x1a04, 0);   // 100 * 12, as a constant in the original
    memset(TriHeap, 0, TriAllocCnt * 12);
}
