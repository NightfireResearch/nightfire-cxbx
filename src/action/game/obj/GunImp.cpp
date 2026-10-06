#include "GunImp.h"

#include <bit>
#include <stdint.h>

#include "Player.h"
#include "../mp/multiplayer.h"
#include "../../input.h"
#include "../../math/math.h"
#include "../../sound/Sound.h"
#include "../../engine/Camera.h"
#include "../../engine/viewFov.h"

// AUTOGEN
obj_tag * GunImp_Create(_VECTOR *pos, quaternion_tag *quat, celglist_tag *celgl);

// The emplacement's field of view in single player: 30 degrees, its sights zoomed in to twice the game's own
// view. In multiplayer the player keeps the usual one.
static constexpr float EmplacementFov = (float)(M_PI / 6.0);
static_assert(std::bit_cast<uint32_t>(EmplacementFov) == 0x3f060a92, "not the original's pi/6");

// A player getting on an emplacement (not while crouching): the player is held still and puts the weapon away,
// and the camera moves to the gun (Player_PositionCamera, camera mode 14). From now on GunImp_Update places the
// player at the gun by writing the player's matrix, which the two transformFlags bits let it do. Reload gets off
// again (GunImp_Update); it is marked as held here, so that the press that got the player on does not count as
// one.
// AUTOINJECT
void GunImp_Activate(obj_tag *gunObj, obj_tag *player) {

    if (gunObj->curState != 0)
        return;

    GUNIMP *gunImp = (GUNIMP*)gunObj->extraObjectData;
    BLData *blData = (BLData*)player->extraObjectData;

    if (player->subState == MovementType_Crouch)
        return;

    blData->previousSubState = Player_ChangeSubState(player, MovementType_Emplacement);
    blData->remoteControlDevice = gunObj;
    Player_SetCamMode(blData, CamMode_GunImp);
    gunObj->curState = 1;
    gunObj->subState = blData->playerNum;
    Player_Disable(player, 1);
    gunImp->user = player;
    Input_SetAction(gunObj->subState, ACTION_RELOAD, 1);
    gunImp->user->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    Player_WeaponNone(player);

    if (MPSettings.isMultiplayer == 0)
        Camera_CalcViewAngles(gunObj->subState, EmplacementFov);
    else
        Camera_CalcViewAngles(gunObj->subState, ViewFov_PlayerFov());

}

// Getting off: the player gets his substate, camera, view and weapon back, and is left facing the way the gun
// was. Also run when the emplacement is reset with nobody on it, which only does the second half.
// AUTOINJECT
void GunImp_Deactivate(obj_tag *gunObj) {

    if (gunObj->curState == 1) {
        short playerNum = gunObj->subState;
        BLData *blData = glb_blokes[playerNum];

        blData->remoteControlDevice = NULL;
        Player_SetCamMode(blData, CamMode_Default);
        Player_ChangeSubState(glb_players[playerNum], blData->previousSubState);
        Player_Enable(glb_players[playerNum], NULL, 0);
        AnimState *anim = glb_players[playerNum]->animState;
        anim->switchingToWeaponId = anim->prevHeldWeaponId;
        Camera_CalcViewAngles(gunObj->subState, ViewFov_PlayerFov());
        blData->field_0x8b8 = 0;
        Vec_Zero(&blData->field_0x3c);
        Vec_Zero(&blData->movement);
    }

    Input_SetAction(gunObj->subState, ACTION_RELOAD, 1);

    GUNIMP *gunImp = (GUNIMP*)gunObj->extraObjectData;
    gunImp->user->transformFlags &= ~(TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX);

    _VECTOR facing;
    Mat_GetDir(&facing, &gunImp->user->transformMatrix);
    vecutil_cartesian_to_spherical_acc(&gunImp->user->rotation, facing.x, facing.y, facing.z);
    gunImp->user->rotation.z = 0.0f;
    gunImp->user->rotation.x = 0.0f;
    gunImp->user = NULL;
    gunObj->curState = 0;

    Sound_Stop(gunImp->firingSound);
    gunImp->firingSound = NULL;

}
