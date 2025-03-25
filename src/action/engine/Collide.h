#ifndef COLLIDE_H
#define COLLIDE_H

#include "../actionhelpers.h"

#include "../util/LList.h"

#pragma pack(push, 1)

struct HITDATA_tag;

typedef struct HITDATA_tag {
  HITDATA_tag* prev; // Conforms to LLNODE_tag
  HITDATA_tag* next;
  float dmgAmt;
  float unknown1f;
  _VECTOR unknown0;
  _VECTOR unknown1;
  char undef[4];
  _VECTOR unknown2;
  _VECTOR maybeHitStartPos;
  char materialType;
  char unknown3;
  short hitBoneIdx;
  cel_tag* hitCel;
  obj_tag* hitObj;
  char unknown4[4];
} HITDATA_tag;

#pragma pack(pop)

bool Collide_RayIntersect(_VECTOR *startPosition,_VECTOR *endPosition,cel_tag *cel,obj_tag *obj,obj_tag *param_5,HITDATA_tag **hitDataOut,char param_7,uint param_8,ushort param_9);
void Collide_FreeHitList(HITDATA_tag **param_1);

#endif // COLLIDE_H