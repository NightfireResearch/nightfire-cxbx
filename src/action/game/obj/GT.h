#ifndef GT_H_
#define GT_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

// A gun turret's extra data: the ceiling guns (kind 1), and the minigun the player or a bot sets down (kind 0, the
// Ronin's). Its body and base are scripts; the body's streams 0-2 register its flash, barrel and third part.
typedef struct {
    _MATRIX aim;           // 0x00 - where the body points
    _MATRIX rest;          // 0x3c - where it pointed when it opened, to go back to before it closes
    _VECTOR facing;        // 0x78 - only what is in front of it is a target
    _VECTOR control;       // 0x84 - the player's pitch and yaw, steering it (state 7)
    char unknown90[0xa0 - 0x90];
    SCRIPTINFO *body;      // 0xa0
    SCRIPTINFO *base;      // 0xa4
    // Who it belongs to: a drone, which Drone_Delete blows the turret up with, or the player who set it down (weapon
    // 0x53 takes control of it again: Player_WeaponInitBullet)
    obj_tag *owner;        // 0xa8
    obj_tag *target;       // 0xac
    obj_tag *muzzleFlash;  // 0xb0 - hidden between shots; it follows the gun's aim, and the Ronin's remote camera
                           //        rides on it (Player_PositionCamera)
    obj_tag *barrel;       // 0xb4 - spins while firing; bullets leave from it
    obj_tag *part;         // 0xb8 - turned with the aim
    float health;          // 0xbc - explosions take their damage from it (GT_ApplyDamage)
    float muzzle;          // 0xc0 - how far ahead of the barrel bullets leave it
    float spin;            // 0xc4 - the barrel's
    float spinSpeed;       // 0xc8 - a frame, under the player's control
    float timer;           // 0xcc - seconds, by state
    float fireTimer;       // 0xd0
    float unknownD4;       // 0xd4 - cleared by GT_Create
    ushort enableChannel;  // 0xd8 - kind 1 opens once it is on
    ushort disableChannel; // 0xda - and closes once it is
    short kind;            // 0xdc
    ushort key1;           // 0xde - the placement's second key, not read
    short ammo;            // 0xe0 - -1 for no limit
    uchar controlled;      // 0xe2 - by the player
    char unknownE3;
} GUNTURRET;

LAYOUT_32(sizeof(GUNTURRET) == 0xe4, "Bad size for GUNTURRET");
static_assert(offsetof(GUNTURRET, body) == 0xa0, "Bad offset of body");
LAYOUT_32(offsetof(GUNTURRET, muzzleFlash) == 0xb0, "Bad offset of muzzleFlash");
LAYOUT_32(offsetof(GUNTURRET, owner) == 0xa8, "Bad offset of owner");
LAYOUT_32(offsetof(GUNTURRET, health) == 0xbc, "Bad offset of health");
LAYOUT_32(offsetof(GUNTURRET, enableChannel) == 0xd8, "Bad offset of enableChannel");
LAYOUT_32(offsetof(GUNTURRET, controlled) == 0xe2, "Bad offset of controlled");

// A turret's placement keys (level_tag)
typedef struct GTPlacement {
    PlacementHeader header; // 0x00
    uint kind;             // 0x2c
    uint key1;             // 0x30
    HASHCODE bodyScript;   // 0x34
    HASHCODE baseScript;   // 0x38
    uint enableChannel;    // 0x3c
    uint disableChannel;   // 0x40
} GTPlacement;

static_assert(sizeof(GTPlacement) == 0x44, "Bad size for GTPlacement");

// GT_Track's candidates, nearest first (QuickSort's Compare_TargetObj)
typedef struct TARGETOBJ {
    obj_tag *obj;
    float distSq;
} TARGETOBJ;

#pragma pack(pop)

extern float TurretRange, TurretRefire;

void GT_LoseControl(obj_tag * gameObj);
void GT_Activate(obj_tag *gameObj);
void GT_ApplyDamage(obj_tag *obj, float damage);
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *quat, level_tag *lvl, celglist_tag *celgl, obj_tag* owner);
obj_tag* GT_DeployMiniGun(_VECTOR *pos, _MATRIX *mtx, obj_tag *owner);
void GT_Register(obj_tag *obj, ushort stream, void *context);
obj_tag* GT_Track(GUNTURRET *turret, obj_tag *obj);
void GT_RandMove(GUNTURRET *turret);
bool GT_InterpolateAimDir(_MATRIX *towards, _MATRIX *from);
void GT_AimAt(GUNTURRET *turret, _VECTOR *at);
void GT_Update(obj_tag *obj);
bool GT_TakeControl(obj_tag *gameObj, obj_tag *player);
void GT_Delete(obj_tag *obj);
void GT_Disable(obj_tag *obj);
void GT_PlayerHasDied(obj_tag *player);

#endif
