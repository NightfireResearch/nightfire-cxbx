#ifndef PLAYER_H_
#define PLAYER_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct WeaponStatus {
    short clipOrCooldown;
    char enabled;
    char pad[0xc-3];
} WeaponStatus;

static_assert(sizeof(WeaponStatus) == 0xc, "WeaponStatus size wrong");

// WIP
typedef struct BLData {
    char _pad_0[0xe0];
    float crosshairOffsetX; // 0xe0
    float crosshairOffsetY; // 0xe4
    char _pad_1[0xf3-0xe8]; // next entry offset - (first byte above previous)
    char crosshairType; // 0xf3
    char _pad_11[0x15c-0xf4];
    WeaponStatus weaponStats[114]; // 0x15c-0x6b3 inclusive
    char _pad_222[0x770-0x6b4];
    HUDINFO_tag* hudInfo; // 0x770
    char _pad_2222[4];
    obj_tag* weaponObject;
    obj_tag* weaponRelatedObjs[32]; // Maybe the attachement points for weapons? Unclear, but set up in Player_InitWeapon
    obj_tag* sightObj; // 0x7fc
    char _unknown[4];
    obj_tag* muzzleFlashObj; // 0x804
    obj_tag* remoteControlDevice; // 0x808
    char _pad_2[0x8b0-0x808-4];
    float nightVisionTimer; // 0x8b0
    char _pad_22[0x8c8-0x8b0-4];
    short muzzleFlashRelated; // 0x8c8
    char _pad_23[0x8d2-0x8c8-2];
    short previousSubState; //0x8d2
    char _pad_3[6];
    char someNightVisionThing;
    char _pad_33[3];
    char playerNum; // 0x8de
    char pad_4;
    char camMode; // 0x8e0
    char pad_5[16];
    char nightVisionActive; // 0x8f1
    // ...
} BLData;

static_assert(offsetof(BLData, hudInfo) == 0x770, "Offset of hudInfo not correct");
static_assert(offsetof(BLData, crosshairOffsetX) == 0xe0, "Offset of crosshairOffsetX not correct");
static_assert(offsetof(BLData, nightVisionActive) == 0x8f1, "Offset of nightVisionActive not correct");
static_assert(offsetof(BLData, nightVisionTimer) == 0x8b0, "Offset of nightVisionTimer not correct");
static_assert(offsetof(BLData, nightVisionActive) == 0x8f1, "Offset of nightVisionActive not correct");


//char (*__kaboom)[offsetof(BLData,playerNum)] = 1;
static_assert(offsetof(BLData, remoteControlDevice) == 0x808, "Offset of remoteControlDevice not correct");
static_assert(offsetof(BLData, playerNum) == 0x8de, "Offset of playerNum not correct");


typedef enum {
    MovementType_Walk = 0,
    MovementType_Climb = 1, // Ladder
    MovementType_Grapple = 2,
    MovementType_Swim = 3,
    MovementType_Crouch = 4,
    MovementType_Decoding = 5, // Scanning with decoder
    MovementType_Wire = 6,
    MovementType_Creep = 7,
    MovementType_ZeroG = 8,
    MovementType_RemoteControl = 10, // Experimentally, seems to be if firing Sentinel? Maybe also RC Cars?
    MovementType_Zipline = 15,
    MovementType_Ronin = 16,
} MovementType;


#pragma pack(pop)

void Player_ChangeState(obj_tag* obj, unsigned short newState);
unsigned short Player_ChangeSubState(obj_tag* obj, unsigned short newState); // Return the previous substate
void Player_SetCamMode(BLData *param_1,unsigned short param_2);
void Player_Disable(obj_tag *param_1,char param_2);
void Player_WeaponNone(obj_tag *param_1);
void Player_Enable(obj_tag *param_1, _MATRIX *mtx, int param_3);
void Player_SetHealth(BLData *obj, float health);
void Player_CheckWeaponsLoaded(BLData *blData);
short Player_AmmoIndex(short weaponIndex);
ushort Player_AmmoInGun(BLData *playerInfo, ushort weaponIndex);
void Player_CreateSight(obj_tag *playerObj, byte viewerNum);
void Player_CreateMuzzleFlash(obj_tag *playerObj, byte viewerNum);
void Player_SetupLaser(BLData *param_1, _VECTOR *targetPos);

void PositionBeam(obj_tag *param_1, _VECTOR *param_2, _VECTOR *param_3);

#endif // PLAYER_H_