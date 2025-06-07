#ifndef ANIM_H
#define ANIM_H

#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct AnimState {
    char _pad_1[0x50];
    char field_0x50; // bit one when weapon is zoomed in
    char _pad_2;
    char currentWeaponId; // 0x52
    char otherWeaponId; // 0x53
    char thirdWeaponId; // 0x54
    char _pad_3[3]; // 55-57
    AnimObj* animObj; // 0x58
} AnimState;

#pragma pack(pop)

static_assert(offsetof(AnimState, animObj) == 0x58, "Offset of animObj wrong");


void AnimObjectDelete(obj_tag* obj);
void AnimLoadFile(HASHCODE hashcode, char param_2);
uchar AnimGetBoneWorldTrans(obj_tag *param_1,uint whichBoneMatrix,int param_3,_VECTOR *param_4,_MATRIX *param_5);
bool AnimObjectIsClose(obj_tag* obj, uint unk);
void AnimObjectDraw(obj_tag *obj, viewer_tag *viewer);
void psiBuildMatrixPalette(obj_tag *gameObj, AnimObj *animObj, char param_3);
void AnimObjectHeadTrack(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *q, _MATRIX *m, int param_5);
void AnimObjectAimAt(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *q, _MATRIX *m, int param_5);

#endif // ANIM_H