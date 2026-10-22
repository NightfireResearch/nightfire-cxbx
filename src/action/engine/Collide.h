#ifndef COLLIDE_H
#define COLLIDE_H

// The records come before actionhelpers.h, which includes Anim.h and Drone.h: both embed them.
#include <stddef.h>
#include "../../common/xbeClass.h"
#include "../../helpers.h"
#include "../math/math.h"

typedef struct cel_tag cel_tag;
typedef struct obj_tag obj_tag;
typedef struct celglist_tag celglist_tag;
typedef struct portal_tag portal_tag;
typedef struct COLLDATA_tag COLLDATA_tag;
typedef struct COLLBOX_tag COLLBOX_tag;
typedef struct COLLQUANT_tag COLLQUANT_tag;

#pragma pack(push, 1)

// HITDATA_tag::flags
enum {
  HIT_COPIED = 0x01,        // Collide_Update's copy of a hit on the object hit, which it does not test again
  HIT_NO_CONTACT = 0x02,    // the cel or object is one the test passes through
  HIT_NO_PUSH = 0x04,       // from the test's flags: the geometry does not push the test out
};

// The distance Collide_Intersect and the Intersect_ tests leave in a hit they reject
#define HIT_NONE 1e8f

typedef struct HITDATA_tag {
  HITDATA_tag* prev; // Conforms to LLNODE_tag
  HITDATA_tag* next;
  float dmgAmt;           // 0x08 - the test's damage
  float dist;             // 0x0c - the hit list is sorted by it; HIT_NONE for a rejected hit
  plane_equ_tag surfaceNormal;
  _VECTOR unknown1;      // 0x20
  _VECTOR maybehitDirection; // 0x2C - ray direction
  _VECTOR hitPosition;   // 0x38
  char materialType;     // 0x44 - the triangle's material byte: 0x3f the surface, 0x40 and 0x80 flags
  char flags;            // 0x45 - HIT_COPIED, HIT_NO_CONTACT, and the test's flags
  short hitBoneIdx;
  cel_tag* hitCel;
  obj_tag* hitObj;
} HITDATA_tag;

XBOX_SIZE(HITDATA_tag, 0x50);
LAYOUT_32(offsetof(HITDATA_tag, surfaceNormal) == 0x10, "Bad offset of surfaceNormal");
LAYOUT_32(offsetof(HITDATA_tag, maybehitDirection) == 0x2c, "Bad offset of maybehitDirection");
LAYOUT_32(offsetof(HITDATA_tag, hitPosition) == 0x38, "Bad offset of hitPosition");
LAYOUT_32(offsetof(HITDATA_tag, flags) == 0x45, "Bad offset of flags");

// HITTEST_tag::type
enum {
  HITTEST_SPHERE_ONE_SIDED = 0x100,
  HITTEST_SPHERE = 0x101,
  HITTEST_RAY_BOXES = 0x200,    // only Collide_Pick and Intersect_Portal know it
  HITTEST_RAY = 0x201,
  HITTEST_POINT = 0x400,
  HITTEST_CYLINDER = 0x800,
  HITTEST_SPHERE_BOXES = 0x1000,
  HITTEST_CYLINDER_TRIS = 0x2000,   // Collide_CylinderTris: the triangles round a cylinder, into TriHeap
};

// A collision query (0x8c bytes), as Collide_StraddleCels and the ray tests take it
typedef struct HITTEST_tag {
  plane_equ_tag plane;  // 0x00 - the triangle under test; Collide_PickObj leaves the spheres' difference here
  _VECTOR push;         // 0x10 - how far the geometry pushed the test out (a drone's capsule: Drone_CollisionHandler
                        //        adds it to the position)
  _VECTOR rayStart;     // 0x1c
  _VECTOR rayEnd;       // 0x28
  _VECTOR dir;          // 0x34 - rayEnd - rayStart
  _VECTOR pos;          // 0x40 - a sphere's centre; the ray tests leave their point here
  _VECTOR sphere;       // 0x4c - the bounding sphere Collide_Pick tests, and its radius
  float sphereRadius;   // 0x58
  obj_tag *tester;      // 0x5c - whose hit list the hits go to
  obj_tag *ignoreA;     // 0x60 - objects the test does not collide with
  obj_tag *ignore;      // 0x64 - (the player's capsule: his gun, his ladder)
  _MATRIX *matrix;      // 0x68 - Collide_Intersect's copy: the cel or object's matrix
  cel_tag *cel;         // 0x6c - where the test starts
  char _unknown70[0x74 - 0x70];
  celglist_tag *glist;  // 0x74 - the geometry Collide_Intersect tests against
  float radius;         // 0x78 - a sphere's or cylinder's radius (a drone's capsule: its radius); a ray's length
  float damage;         // 0x7c - copied to the hits (1 for a dead player's capsule, else 0)
  ushort type;          // 0x80 - HITTEST_ (0 for none)
  char _unknown82[0x84 - 0x82];
  ushort mask;          // 0x84 - what the test passes through (Ghidra: maybeCollisionMaskTypes)
  ushort flags;         // 0x86 - 0x10: stop at the first hit
  ushort found;         // 0x88 - bit 1: Intersect_CylGeom found a floor (Drone_FeetOnPoint counts the drone as on
                        //        the ground)
  char _unknown8a[0x8c - 0x8a];
} HITTEST_tag;

LAYOUT_32(sizeof(HITTEST_tag) == 0x8c, "HITTEST_tag is 0x8c bytes");
static_assert(offsetof(HITTEST_tag, push) == 0x10, "Bad offset of HITTEST_tag.push");
static_assert(offsetof(HITTEST_tag, sphereRadius) == 0x58, "Bad offset of HITTEST_tag.sphereRadius");
LAYOUT_32(offsetof(HITTEST_tag, cel) == 0x6c, "Bad offset of HITTEST_tag.cel");
LAYOUT_32(offsetof(HITTEST_tag, glist) == 0x74, "Bad offset of HITTEST_tag.glist");
LAYOUT_32(offsetof(HITTEST_tag, radius) == 0x78, "Bad offset of HITTEST_tag.radius");
LAYOUT_32(offsetof(HITTEST_tag, ignore) == 0x64, "Bad offset of HITTEST_tag.ignore");
LAYOUT_32(offsetof(HITTEST_tag, type) == 0x80, "Bad offset of HITTEST_tag.type");
LAYOUT_32(offsetof(HITTEST_tag, found) == 0x88, "Bad offset of HITTEST_tag.found");

// One of the boxes a search collects (ColBoxs): the box, and for a ray where it enters it
typedef struct BOXENTRY_tag {
  COLLBOX_tag *box;
  float t;
} BOXENTRY_tag;

#pragma pack(pop)

#include "../actionhelpers.h"
#include "../util/LList.h"

#define MAX_COLBOXES 600

extern BOXENTRY_tag ColBoxs[MAX_COLBOXES];
extern uint32_t BoxCnt;
// The push the geometry tests accumulate, in the cel's or object's space
extern _VECTOR DeltaP;
// Collide_Intersect: the cel's geometry is in world space (no matrix)
extern uint8_t Ident;
// The triangles Intersect_CylTriGeom collects, three vertices each: TriOnCnt of TriAllocCnt vertices of 12 bytes
extern uint32_t TriAllocCnt;
extern uint32_t TriOnCnt;
extern _VECTOR *TriHeap;

// The hits, the hit heap and the narrow phase
HITDATA_tag* Coll_GetFreeHit(void);
HITDATA_tag* Coll_AddHitToList(HITDATA_tag **list, float dist, cel_tag *cel, obj_tag *obj);
void Collide_FreeHitList(HITDATA_tag **param_1);
void Coll_ResetHitHeap(void);
void _Collide_Sort(HITDATA_tag **list);
bool Intersect_CheckIfFlagsMatch(uchar material, uchar hitFlags, ushort mask, float *dist);
void Collide_FilterBullets(HITDATA_tag **hitData, ushort flags);
double Collide_GetDamageNObjects(HITDATA_tag *hitDatas, obj_tag **objectList, ushort *objectListCountOut, ushort maxObjectsInList);

// The broad phase
cel_tag** Collide_StraddleCels(HITTEST_tag *test, ushort *count);
bool Intersect_Portal(HITTEST_tag *test, portal_tag *portal);
void Collide_Pick(HITTEST_tag *test);
void _Collide_PickObj(HITTEST_tag *test, obj_tag *obj, obj_tag *tester);
bool _Intersect_SphereSphere(_VECTOR *a, float radiusA, _VECTOR *b, float radiusB, _VECTOR *difference, float *distSq);
bool _Intersect_RaySphere(HITTEST_tag *test, float *distSq);
bool _Intersect_CylSphere(HITTEST_tag *test, float *distSq);

// The narrow phase
bool _Collide_Intersect(HITTEST_tag *test, HITDATA_tag *hit, ushort flags);
bool _Collide_Jointy(HITTEST_tag *test, HITDATA_tag *hit);
void _Collide_TestHits(HITTEST_tag *test, ushort flags, char onlyCel);
void Intersect_PointGeom(HITTEST_tag *test, HITDATA_tag *hit);
void Intersect_RayGeom(HITTEST_tag *test, HITDATA_tag *hit);
void Intersect_CylGeom(HITTEST_tag *test, HITDATA_tag *hit);
void Intersect_CylTriGeom(HITTEST_tag *test, HITDATA_tag *hit);

// Boxes and triangles
bool Intersect_SphereBox(_VECTOR *centre, float radius, _VECTOR *boxMin, _VECTOR *boxMax);
bool Intersect_BoxBox(_VECTOR *min1, _VECTOR *max1, _VECTOR *min2, _VECTOR *max2);
bool Intersect_RayBox(HITTEST_tag *test, _VECTOR *boxMin, _VECTOR *boxMax, float *t);
bool Intersect_PointOOBox(_MATRIX *mtx, _VECTOR *halfSize, _VECTOR *point, float radius);
bool _Intersect_AABB_SweptVolume(_VECTOR *v0, _VECTOR *v1, _VECTOR *v2, _VECTOR *boxMin, _VECTOR *boxMax);
void RecurseBoxesSph(COLLDATA_tag *coll, COLLBOX_tag *box, _VECTOR *centre, float radius);
void ASH_RecurseBoxesBox(COLLDATA_tag *coll, COLLBOX_tag *box, _VECTOR *boxMin, _VECTOR *boxMax);
void ASH_RecurseBoxesRay(COLLDATA_tag *coll, COLLBOX_tag *box, HITTEST_tag *test);
bool Intersect_UnpackTrianglePlaneEq(uchar *vertexBlock, ushort normal, plane_equ_tag *plane);
void Intersect_UnpackTriangleVerticesToWorld(uchar *vertexBlock, ushort v0, ushort v1, ushort v2, _VECTOR *vertsOut, COLLQUANT_tag *quant);

// The queries
bool Collide_RayIntersect(_VECTOR *startPosition,_VECTOR *endPosition,cel_tag *cel,obj_tag *obj,obj_tag *param_5,HITDATA_tag **hitDataOut,char onlyCel,ushort mask,ushort flags);
bool Collide_SphereIntersect(_VECTOR *centre, float radius, cel_tag *cel, obj_tag *ignoreA, obj_tag *ignoreB, HITDATA_tag **hitList, char onlyCel, char oneSided, ushort mask, ushort flags);
bool Collide_CylinderIntersect(HITTEST_tag *test, HITDATA_tag **hitList, char onlyCel, ushort flags);
_VECTOR* Collide_CylinderTris(_VECTOR *start, _VECTOR *end, float radius, cel_tag *cel, uint *count, ushort mask, ushort flags);
bool Collide_PlaceObject(_VECTOR *pos, _MATRIX *mtx, float radius, float extent, obj_tag *obj);
bool Collide_LineOfSight(_VECTOR *param_1,_VECTOR *param_2,cel_tag *param_3,obj_tag *param_4,obj_tag *param_5,uint param_6);
bool Collide_RayTriangle(_VECTOR *ptStart,_VECTOR *ptEnd,_VECTOR *vtx1,_VECTOR *vtx2,_VECTOR *vtx3, float *distanceOut);
bool Intersect_ConeSphere(_VECTOR *apex, _VECTOR *dir, float cosA, float sinA, _VECTOR *sphere);
void Collide_Update(obj_tag *first);

// Register-argument adaptors patched over the originals (the registers in Collide.cpp); never call them from C.
void Collide_Sort(void);
void Intersect_AABB_SweptVolume(void);
void Intersect_SphereSphere(void);
void Intersect_RaySphere(void);
void Intersect_CylSphere(void);
void Collide_PickObj(void);
void Collide_Jointy(void);
void Collide_Intersect(void);
void Collide_TestHits(void);

// Game objects which span several cels (Collide.cpp)
extern LLISTINFO_tag ForcedList;

#endif // COLLIDE_H
