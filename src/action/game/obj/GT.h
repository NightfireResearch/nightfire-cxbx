#ifndef GT_H_
#define GT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A gun turret's extra data: the Ronin, and the ceiling-mounted guns. Only what the player's camera reads is
// named so far; GT_Update uses much more.
typedef struct {
    char unknown00[0xb0];
    obj_tag *muzzleFlash;  // 0xb0 - hidden between shots; it follows the gun's aim, and the Ronin's remote camera
                           //        rides on it (Player_PositionCamera)
    obj_tag *barrel;       // 0xb4 - spins while firing; bullets leave from it
    char unknownB8[0xe4 - 0xb8];
} GUNTURRET;

static_assert(sizeof(GUNTURRET) == 0xe4, "Bad size for GUNTURRET");
static_assert(offsetof(GUNTURRET, muzzleFlash) == 0xb0, "Bad offset of muzzleFlash");

#pragma pack(pop)

void GT_LoseControl(obj_tag * gameObj);
void GT_Activate(obj_tag *gameObj);
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *param_3, level_tag *lvl, celglist_tag *celgl, obj_tag* param_5);

#endif