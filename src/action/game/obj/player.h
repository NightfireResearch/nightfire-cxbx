#ifndef PLAYER_H_
#define PLAYER_H_

#include "object.h"

#pragma pack(push, 1)

// WIP
typedef struct {
    char _pad_1[0x770];
    HUDINFO_tag* hudInfo;
    char _pad_111[0x808-4-0x770];
    obj_tag* remoteControlDevice; // 0x808
    char _pad_2[0xc6];
    short previousSubState; //0x8d2
    char _pad_3[0xa];
    char playerNum; // 0x8de
    // ...
} BLData;

static_assert(offsetof(BLData, hudInfo) == 0x770, "Offset of hudInfo not correct");


//char (*__kaboom)[offsetof(BLData,playerNum)] = 1;
static_assert(offsetof(BLData, remoteControlDevice) == 0x808, "Offset of remoteControlDevice not correct");
static_assert(offsetof(BLData, playerNum) == 0x8de, "Offset of playerNum not correct");

#pragma pack(pop)


unsigned short Player_ChangeSubState(obj_tag* obj, unsigned short newState);
void Player_SetCamMode(BLData *param_1,unsigned short param_2);
void Player_Disable(obj_tag *param_1,char param_2);
void Player_WeaponNone(obj_tag *param_1);


#endif // PLAYER_H_