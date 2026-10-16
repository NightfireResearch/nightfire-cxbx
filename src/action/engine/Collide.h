#ifndef COLLIDE_H
#define COLLIDE_H

#include "../actionhelpers.h"

#include "../util/LList.h"

#pragma pack(push, 1)

typedef struct HITDATA_tag {
  HITDATA_tag* prev; // Conforms to LLNODE_tag
  HITDATA_tag* next;
  float dmgAmt;
  float unknown1f;
  plane_equ_tag surfaceNormal;
  _VECTOR unknown1;      // 0x20
  _VECTOR maybehitDirection; // 0x2C - ray direction
  _VECTOR hitPosition;   // 0x38
  char materialType;
  char unknown3;
  short hitBoneIdx;
  cel_tag* hitCel;
  obj_tag* hitObj;
} HITDATA_tag;

static_assert(sizeof(HITDATA_tag) == 0x50, "HITDATA_tag is not the expected size");
static_assert(offsetof(HITDATA_tag, surfaceNormal) == 0x10, "Bad offset of surfaceNormal");
static_assert(offsetof(HITDATA_tag, maybehitDirection) == 0x2c, "Bad offset of maybehitDirection");
static_assert(offsetof(HITDATA_tag, hitPosition) == 0x38, "Bad offset of hitPosition");

// A collision query (0x8c bytes), as Collide_StraddleCels and the ray tests take it. Only the fields our code sets
// are named.
typedef struct HITTEST_tag {
  char _unknown00[0x1c];
  _VECTOR rayStart;     // 0x1c
  _VECTOR rayEnd;       // 0x28
  char _unknown34[0x6c - 0x34];
  cel_tag *cel;         // 0x6c - where the ray starts
  char _unknown70[0x78 - 0x70];
  float rayLength;      // 0x78
  char _unknown7c[0x80 - 0x7c];
  uchar unknown80;      // 0x80 - 1 from the AI network's tests
  uchar unknown81;      // 0x81 - 2 from the AI network's tests
  char _unknown82[0x8c - 0x82];
} HITTEST_tag;

static_assert(sizeof(HITTEST_tag) == 0x8c, "HITTEST_tag is 0x8c bytes");
static_assert(offsetof(HITTEST_tag, cel) == 0x6c, "Bad offset of HITTEST_tag.cel");
static_assert(offsetof(HITTEST_tag, rayLength) == 0x78, "Bad offset of HITTEST_tag.rayLength");

#pragma pack(pop)

bool Collide_RayIntersect(_VECTOR *startPosition,_VECTOR *endPosition,cel_tag *cel,obj_tag *obj,obj_tag *param_5,HITDATA_tag **hitDataOut,char param_7,uint maybeFlags,ushort param_9);
void Collide_FreeHitList(HITDATA_tag **param_1);
HITDATA_tag* Coll_GetFreeHit(void);
bool Collide_LineOfSight(_VECTOR *param_1,_VECTOR *param_2,cel_tag *param_3,obj_tag *param_4,obj_tag *param_5,uint param_6);
bool Collide_RayTriangle(_VECTOR *ptStart,_VECTOR *ptEnd,_VECTOR *vtx1,_VECTOR *vtx2,_VECTOR *vtx3, float *distanceOut);
void Collide_FilterBullets(HITDATA_tag **hitData, ushort flags);
float Collide_GetDamageNObjects(HITDATA_tag *hitDatas, obj_tag **objectList, ushort *objectListCountOut, ushort maxObjectsInList);
bool Intersect_ConeSphere(_VECTOR *coneApex, float *coneDirection, float coneCos, float coneSin, _VECTOR *sphereCenter);

void Coll_ResetHitHeap(void);

#endif // COLLIDE_H