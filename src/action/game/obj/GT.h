#ifndef GT_H_
#define GT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A gun turret's extra data: the Ronin, and the ceiling-mounted guns. Only what the player's camera reads is
// named so far; GT_Update uses much more.
typedef struct {
    char unknown00[0xa8];
    // Who it belongs to: a drone, which Drone_Delete blows the turret up with, or the player who set it down (weapon
    // 0x53 takes control of it again: Player_WeaponInitBullet)
    obj_tag *owner;        // 0xa8
    char unknownAC[0xb0 - 0xac];
    obj_tag *muzzleFlash;  // 0xb0 - hidden between shots; it follows the gun's aim, and the Ronin's remote camera
                           //        rides on it (Player_PositionCamera)
    obj_tag *barrel;       // 0xb4 - spins while firing; bullets leave from it
    char unknownB8[0xbc - 0xb8];
    float health;          // 0xbc - explosions take their damage from it (GT_ApplyDamage)
    char unknownC0[0xe4 - 0xc0];
} GUNTURRET;

static_assert(sizeof(GUNTURRET) == 0xe4, "Bad size for GUNTURRET");
static_assert(offsetof(GUNTURRET, muzzleFlash) == 0xb0, "Bad offset of muzzleFlash");
static_assert(offsetof(GUNTURRET, owner) == 0xa8, "Bad offset of owner");
static_assert(offsetof(GUNTURRET, health) == 0xbc, "Bad offset of health");

#pragma pack(pop)

void GT_LoseControl(obj_tag * gameObj);
void GT_Activate(obj_tag *gameObj);
void GT_ApplyDamage(obj_tag *obj, float damage);
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *param_3, level_tag *lvl, celglist_tag *celgl, obj_tag* param_5);

#endif