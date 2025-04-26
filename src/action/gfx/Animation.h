#ifndef ANIMATION_H_
#define ANIMATION_H_

#include "../actionhelpers.h"

#pragma pack(push, 1)
typedef struct AnimState {
    char _pad_1[0x50];
    char field_0x50; // bit one when weapon is zoomed in
    char _pad_2;
    char currentWeaponId; // 0x52
    char otherWeaponId; // 0x53
    char thirdWeaponId; // 0x54
} AnimState;
#pragma pack(pop)

void AnimObjectDelete(obj_tag* obj);

#endif // ANIMATION_H