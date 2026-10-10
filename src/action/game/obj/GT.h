#ifndef GT_H_
#define GT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A gun turret's extra data: the Ronin, and the ceiling-mounted guns. Only what the player's camera and GT_Track
// read is named so far; GT_Update uses much more.
typedef struct {
    char unknown00[0x78];
    _VECTOR unknown78;     // 0x78 - GT_Track takes only targets whose direction has a dot product of 0.01 or more with it
    char unknown84[0xa0 - 0x84];
    SCRIPTINFO *unknownA0; // 0xa0 - GT_Track measures directions to targets from its matrix's position
    char unknownA4[0xa8 - 0xa4];
    obj_tag *playerController; // 0xa8 - MP_PlayerKilled credits a kill by the turret to it
    obj_tag *target;       // 0xac - name ours: GT_Track keeps it while it stays one of the two nearest in sight
    obj_tag *muzzleFlash;  // 0xb0 - hidden between shots; it follows the gun's aim, and the Ronin's remote camera
                           //        rides on it (Player_PositionCamera)
    obj_tag *barrel;       // 0xb4 - spins while firing; bullets leave from it
    char unknownB8[0xc0 - 0xb8];
    float barrelLength;    // 0xc0 - name ours: GT_Track checks sight from this far along the barrel
    char unknownC4[0xdc - 0xc4];
    short isCeilingMountedTurret; // 0xdc
    char unknownDE[0xe4 - 0xde];
} GUNTURRET;

static_assert(sizeof(GUNTURRET) == 0xe4, "Bad size for GUNTURRET");
static_assert(offsetof(GUNTURRET, muzzleFlash) == 0xb0, "Bad offset of muzzleFlash");
static_assert(offsetof(GUNTURRET, unknownA0) == 0xa0, "Bad offset of unknownA0");
static_assert(offsetof(GUNTURRET, barrelLength) == 0xc0, "Bad offset of barrelLength");
static_assert(offsetof(GUNTURRET, isCeilingMountedTurret) == 0xdc, "Bad offset of isCeilingMountedTurret");

#pragma pack(pop)

void GT_LoseControl(obj_tag * gameObj);
void GT_Activate(obj_tag *gameObj);
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *param_3, level_tag *lvl, celglist_tag *celgl, obj_tag* param_5);
obj_tag* GT_Track(GUNTURRET *gun, obj_tag *obj);

#endif