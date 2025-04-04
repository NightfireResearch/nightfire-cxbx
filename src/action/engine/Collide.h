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
  _VECTOR someDirection; // 0x10
  _VECTOR unknown1;
  char undef[4];
  _VECTOR maybehitDirection; // 0x2C
  _VECTOR hitPosition;
  char materialType;
  char unknown3;
  short hitBoneIdx;
  cel_tag* hitCel;
  obj_tag* hitObj;
} HITDATA_tag;

static_assert(sizeof(HITDATA_tag) == 0x50, "HITDATA_tag is not the expected size");

#pragma pack(pop)

bool Collide_RayIntersect(_VECTOR *startPosition,_VECTOR *endPosition,cel_tag *cel,obj_tag *obj,obj_tag *param_5,HITDATA_tag **hitDataOut,char param_7,uint param_8,ushort param_9);
void Collide_FreeHitList(HITDATA_tag **param_1);
HITDATA_tag* Coll_GetFreeHit(void);
bool Collide_LineOfSight(_VECTOR *param_1,_VECTOR *param_2,cel_tag *param_3,obj_tag *param_4,obj_tag *param_5,uint param_6);
bool Collide_RayTriangle(_VECTOR *ptStart,_VECTOR *ptEnd,_VECTOR *vtx1,_VECTOR *vtx2,_VECTOR *vtx3, float *distanceOut);
void Collide_FilterBullets(HITDATA_tag **hitData, ushort flags);

#endif // COLLIDE_H