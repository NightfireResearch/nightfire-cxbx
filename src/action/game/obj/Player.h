#ifndef PLAYER_H_
#define PLAYER_H_

#include "../../actionhelpers.h"

typedef enum {
    CamMode_Default = 0x00,
    CamMode_PostMPGameThirdPerson = 0x01,
    CamMode_Redeemer = 0x0c, // Redeemer = Sentinel Missile?
    CamMode_RCCar = 0x0d,
    CamMode_Ronin = 0x0f,
} CamMode;

#pragma pack(push, 1)

typedef struct WeaponStatus {
    short clipOrCooldown;
    char enabled;
    // For a weapon with no alt-fire variant, Player_Weapon steps this on each alt-fire press and wraps it at
    // weapon_data[].numFireModes; PS2's field_0x3.
    char fireModeIndex; // 0x3
    char pad[0xc-4];
} WeaponStatus;

static_assert(sizeof(WeaponStatus) == 0xc, "WeaponStatus size wrong");
static_assert(offsetof(WeaponStatus, fireModeIndex) == 0x3, "Offset of fireModeIndex not correct");

// WIP
typedef struct BLData {
    _VECTOR lastPosition; // 0x00 - Player_Update copies the object's position here every frame
    // This frame's movement, in the object's local frame. Player_Move fills it in, Player_Update rotates it
    // by the object's matrix and adds it to the position, then copies it to lastMovement for next time -
    // which is what makes lastMovement the state Player_Move smooths from.
    _VECTOR movement;     // 0x0c
    _VECTOR lastMovement; // 0x18
    // This frame's change in the object's rotation, not an angle: Player_Update zeroes it before the
    // movement handlers run and adds it to obj_tag::rotation afterwards. .y is the turn, and it goes
    // negative turning right. Anything wanting to steer the player adds to it in between - which is what
    // mouse look does, in Player_ViewClamping.
    _VECTOR rotationDelta; // 0x24
    char _pad_0b[0xbc - 0x30];
    float someMPCameraThing1; // 0xbc
    float someMPCameraThing2; // 0xc0
    float someMPCameraThing3; // 0xc4
    undefined4 someMPCameraThing4; // 0xc8
    char _pad_11111[0xd4 - 0xcc];
    // Three words Player_InitWeapon zeroes; nothing else is known about them yet
    undefined4 field_0xd4; // 0xd4
    undefined4 field_0xd8; // 0xd8
    undefined4 field_0xdc; // 0xdc
    float crosshairOffsetX; // 0xe0
    float crosshairOffsetY; // 0xe4
    char _pad_1[0xf0-0xe8];
    // Whether the game is easing the view pitch back towards pitchAutoLevelTarget. 0 and 1 mean it is not,
    // 1 specifically meaning "the player is aiming right now"; 2 and 3 mean it is. Player_Aiming sets this
    // to 1 on any frame the stick moves the view, which is what stops the easing fighting the player - see
    // Player_ClampSomeAngles, which is the other half of it, and Player_ViewClamping, which is where mouse
    // look has to do the same thing for the same reason.
    char aimAutoLevelState; // 0xf0
    char _pad_1b[0xf2-0xf1];
    char field_0xf2; // 0xf2 - zeroed by Player_InitWeapon
    char crosshairType; // 0xf3
    char _pad_11[0x110-0xf4];
    // Non-zero while something else is driving the player - Player_Move takes the controls away entirely.
    char movementDisabled; // 0x110
    char _pad_11b[0x114-0x111];
    // How much of each ammo type the player carries, not counting what is loaded (weaponStats[].clipOrCooldown):
    // indexed by weapon_definition_tag::ammoType, capped at ammo_data[].maybeMaxNumPerPlayer.
    short ammo[34]; // 0x114
    char _pad_11c[0x15c-0x158];
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
    char _pad_2a[0x814-0x80c];
    // The ladder (or wire, zipline...) the player is on, in the movement substates that attach him to one.
    // Name from the PS2 symbols.
    obj_tag* attachedToSpecialMovementItem; // 0x814
    char _pad_2a2[0x824-0x818];
    float health; // 0x824 - used eg. by SP_Update to abort an in-progress NIS if the player has died
    undefined4 field_0x828; // 0x828 - zeroed by Player_InitWeapon; FUN_000b70b0 stops the zoom motor sound it holds
    char _pad_2b[0x838-0x828-4];
    // Where the player is looking vertically, as a fraction of a right angle rather than in radians, so
    // +-1 is straight up and straight down - which is exactly the range Player_ViewClamping enforces.
    float pitchFromHorizontal; // 0x838
    float pitchAutoLevelTarget; // 0x83c - what aimAutoLevelState above eases pitchFromHorizontal towards
    // Armour, which Player_RamSave carries between the parts of a mission beside health. Player_InitWeapon
    // takes it away at the start of Castle Indoors 1 and sets it to 50 on the space station levels.
    float armor; // 0x840
    char _pad_2c[0x84c-0x844];
    // HUD_UpdateHealthPane draws the armour and health bars at 4x this alpha, capped at 1, and sets it to 1 while
    // the game is paused - so the bars fade in over its first quarter. (Its writers were not traced.)
    float hudFadeIn; // 0x84c
    char _pad_2c2[0x860-0x850];
    float lensFlareRelated; // 0x860
    char _pad_222222[0x86c-0x864];
    undefined4 field_0x86c[4]; // 0x86c-0x87b - zeroed by Player_InitWeapon
    char _pad_222222b[0x88c-0x87c];
    float turnAccelState; // 0x88c - AccelFunc0's carried state for the turn axis; see Player_Move
    char _pad_222223[0x8b0-0x890];
    float nightVisionTimer; // 0x8b0
    char _pad_22[0x8bc-0x8b0-4];
    float field_0x8bc; // 0x8bc - zeroed by Player_InitWeapon
    char _pad_22b[0x8c4-0x8bc-4];
    // The weapon the player was switching to when Player_RamSave ran at the end of the previous part of the
    // mission (it saves animState->switchingToWeaponId); Player_RamLoad puts it back here, and
    // Player_InitWeapon hands it back to animState->switchingToWeaponId.
    char ramLoadedWeaponId; // 0x8c4
    char _pad_22c;
    short field_0x8c6; // 0x8c6 - zeroed by Player_InitWeapon
    short muzzleFlashRelated; // 0x8c8
    char _pad_23[0x8d2-0x8c8-2];
    short previousSubState; //0x8d2
    char _pad_3[4];
    // Frames left of the "Bond moment" icon (TEX_GOLD007BONUS, the health pane's sprite 16): it spins in while
    // above 100, then sits at the top of the screen; HUD_UpdateHealthPane counts it down by FRAME_RATE_MUL.
    short bondMomentTimer; // 0x8d8
    char someNightVisionThing;
    char _pad_33[3];
    char playerNum; // 0x8de
    char pad_4;
    char camMode; // 0x8e0 - CamMode
    char maybeCamRelatedCountdown; // 0x8e1
    char pad_5[0x8e6-0x8e2];
    char field_0x8e6; // 0x8e6 - zeroed by Player_InitWeapon
    char _pad_5b;
    char field_0x8e8; // 0x8e8 - zeroed by Player_InitWeapon
    char _pad_5c[0x8ef-0x8e9];
    // The third-person action icon to show (index into HUD.cpp's ThirdIconSprites: grapple, wire, standing, ...),
    // 0xff for none
    uchar thirdIcon; // 0x8ef
    char _pad_5d;
    char nightVisionActive; // 0x8f1
    char _pad_5e[0x8f7-0x8f2];
    // Which sides the player was last hit from (bits 0-3: the health pane's full-screen overlays 20-23) and how
    // bright that flash still is; HUD_UpdateHealthPane fades it by FRAME_RATE_MUL a frame
    uchar hitDirections; // 0x8f7
    uchar hitDirectionFade; // 0x8f8
    // ...
} BLData;

static_assert(offsetof(BLData, someMPCameraThing1) == 0xbc, "Offset of someMPCameraThing1 not correct");
static_assert(offsetof(BLData, crosshairOffsetX) == 0xe0, "Offset of crosshairOffsetX not correct");
static_assert(offsetof(BLData, hudInfo) == 0x770, "Offset of hudInfo not correct");
static_assert(offsetof(BLData, nightVisionActive) == 0x8f1, "Offset of nightVisionActive not correct");
static_assert(offsetof(BLData, nightVisionTimer) == 0x8b0, "Offset of nightVisionTimer not correct");
static_assert(offsetof(BLData, hudFadeIn) == 0x84c, "Offset of hudFadeIn not correct");
static_assert(offsetof(BLData, bondMomentTimer) == 0x8d8, "Offset of bondMomentTimer not correct");
static_assert(offsetof(BLData, thirdIcon) == 0x8ef, "Offset of thirdIcon not correct");
static_assert(offsetof(BLData, hitDirections) == 0x8f7, "Offset of hitDirections not correct");
static_assert(offsetof(BLData, hitDirectionFade) == 0x8f8, "Offset of hitDirectionFade not correct");
static_assert(offsetof(BLData, nightVisionActive) == 0x8f1, "Offset of nightVisionActive not correct");


//char (*__kaboom)[offsetof(BLData,playerNum)] = 1;
static_assert(offsetof(BLData, remoteControlDevice) == 0x808, "Offset of remoteControlDevice not correct");
static_assert(offsetof(BLData, health) == 0x824, "Offset of health not correct");
static_assert(offsetof(BLData, playerNum) == 0x8de, "Offset of playerNum not correct");
static_assert(offsetof(BLData, rotationDelta) == 0x24, "Offset of rotationDelta not correct");
static_assert(offsetof(BLData, movement) == 0x0c, "Offset of movement not correct");
static_assert(offsetof(BLData, lastMovement) == 0x18, "Offset of lastMovement not correct");
static_assert(offsetof(BLData, movementDisabled) == 0x110, "Offset of movementDisabled not correct");
static_assert(offsetof(BLData, ammo) == 0x114, "Offset of ammo not correct");
static_assert(offsetof(BLData, weaponStats) == 0x15c, "Offset of weaponStats not correct");
static_assert(offsetof(BLData, turnAccelState) == 0x88c, "Offset of turnAccelState not correct");
static_assert(offsetof(BLData, maybeCamRelatedCountdown) == 0x8e1, "Offset of maybeCamRelatedCountdown not correct");
static_assert(offsetof(BLData, pitchFromHorizontal) == 0x838, "Offset of pitchFromHorizontal not correct");
static_assert(offsetof(BLData, pitchAutoLevelTarget) == 0x83c, "Offset of pitchAutoLevelTarget not correct");
static_assert(offsetof(BLData, aimAutoLevelState) == 0xf0, "Offset of aimAutoLevelState not correct");
static_assert(offsetof(BLData, field_0xd4) == 0xd4, "Offset of field_0xd4 not correct");
static_assert(offsetof(BLData, field_0xf2) == 0xf2, "Offset of field_0xf2 not correct");
static_assert(offsetof(BLData, weaponObject) == 0x778, "Offset of weaponObject not correct");
static_assert(offsetof(BLData, weaponRelatedObjs) == 0x77c, "Offset of weaponRelatedObjs not correct");
static_assert(offsetof(BLData, field_0x828) == 0x828, "Offset of field_0x828 not correct");
static_assert(offsetof(BLData, attachedToSpecialMovementItem) == 0x814, "Offset of attachedToSpecialMovementItem not correct");
static_assert(offsetof(BLData, armor) == 0x840, "Offset of armor not correct");
static_assert(offsetof(BLData, field_0x86c) == 0x86c, "Offset of field_0x86c not correct");
static_assert(offsetof(BLData, field_0x8bc) == 0x8bc, "Offset of field_0x8bc not correct");
static_assert(offsetof(BLData, ramLoadedWeaponId) == 0x8c4, "Offset of ramLoadedWeaponId not correct");
static_assert(offsetof(BLData, field_0x8c6) == 0x8c6, "Offset of field_0x8c6 not correct");
static_assert(offsetof(BLData, field_0x8e6) == 0x8e6, "Offset of field_0x8e6 not correct");
static_assert(offsetof(BLData, field_0x8e8) == 0x8e8, "Offset of field_0x8e8 not correct");


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
    MovementType_ZeroG_Anim = 9, // Ghidra's ZEROG_2 - the same movement, animated; Player_ZeroG handles both
    MovementType_RemoteControl = 10, // Steering the Sentinel's guided missile (Bullet_Update)
    MovementType_RCVehicle = 11,     // Driving an RC car or helicopter (Car_Activate, Car_Update)
    MovementType_Emplacement = 12,   // On a gun emplacement (GunImp_Activate, GunImp_Update)
    MovementType_Zipline = 15,
    MovementType_Ronin = 16,
} MovementType;


typedef struct {
    char unknown[0x2c];
    short requiredSwitch;
    char unknown2[0xb0-2-0x2c];
} level_tag_PlayerStartPosition;

typedef struct {
    _VECTOR pos;
    _VECTOR rot;
    char unknown[2];
    short isEnabled;
    level_tag_PlayerStartPosition levelData;
} PlayerStartPosition;

static_assert(sizeof(PlayerStartPosition) == 0xcc, "Bad size for PlayerStartPosition");


#pragma pack(pop)

void Player_ResetStartPos(void);
void __stdcall ReadTuningVars(void);
void Player_ChangeState(obj_tag* obj, unsigned short newState);
unsigned short Player_ChangeSubState(obj_tag* obj, unsigned short newState); // Return the previous substate
void Player_SetCamMode(BLData *param_1,unsigned short param_2);
void Player_Disable(obj_tag *param_1,char param_2);
void Player_WeaponNone(obj_tag *param_1);
void Player_ViewClamping(obj_tag *player);
void Player_Move(BLData *blData, obj_tag *player, float speedScale);
void Player_Weapon(obj_tag *player);
void Player_Enable(obj_tag *param_1, _MATRIX *mtx, int param_3);
void Player_SetHealth(BLData *obj, float health);
void Player_CheckWeaponsLoaded(BLData *blData);
short Player_AmmoIndex(short weaponIndex);
uint Player_EquipAmmo(BLData *playerInfo, short weaponIndex, short amount);
ushort Player_AmmoInGun(BLData *playerInfo, ushort weaponIndex);
void Player_CreateSight(obj_tag *playerObj, byte viewerNum);
void Player_CreateMuzzleFlash(obj_tag *playerObj, byte viewerNum);
void Player_SetupLaser(BLData *param_1, _VECTOR *targetPos);
void Player_Start(void);
void Player_AddNewStartPos(_VECTOR *pos, _VECTOR *rot, ushort maybeEnabled, level_tag *lvl);
void Player_GetHeadPos(_VECTOR *headPosOut, obj_tag *player, _MATRIX *param_3);
void Player_InitWeapon(BLData *blData, obj_tag *player);

void PositionBeam(obj_tag *param_1, _VECTOR *param_2, _VECTOR *param_3);

#endif // PLAYER_H_