#ifndef ANIMATION_H_
#define ANIMATION_H_

#include "../actionhelpers.h"

#pragma pack(push, 1)
typedef struct AnimState {
    char _pad_1[0x52];
    char currentWeaponId;
    char some_0x53;
    char some_0x54;
} AnimState;
#pragma pack(pop)

void AnimObjectDelete(obj_tag* obj);

#endif // ANIMATION_H