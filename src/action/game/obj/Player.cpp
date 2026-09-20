#include "Player.h"
#include "../mp/multiplayer.h"
#include "../view.h"
#include "../../engine/viewer.h"
#include "../../engine/Anim.h"
#include "../../engine/mouseLook.h"

#include "../../input.h"
#include "../../game.h"
#include "../sp/SwitchChannels.h"
#include <math.h>
#include <string.h>

// AUTOGEN
unsigned short Player_ChangeSubState(obj_tag* obj, unsigned short newState);
// AUTOGEN
void Player_Disable(obj_tag *param_1,char param_2);
// AUTOGEN
void Player_Enable(obj_tag *param_1, _MATRIX *mtx, int param_3);
// AUTOGEN
void Player_SetHealth(BLData *obj, float health);
// AUTOGEN
void Concat(float *param_1, float *param_2);


#define player_start_positions_index U32_AT(0x002774c8)
#define player_start_position U32_AT(0x002774cc) // Unused - only ever written?
#define player_start ((PlayerStartPosition*)(0x002774d0))

// AUTOINJECT
void Player_ResetStartPos(void) {
    player_start_positions_index = 0;
    player_start_position = 0;
}


// AUTOINJECT
void Player_AddNewStartPos(_VECTOR *pos, _VECTOR *rot, ushort maybeEnabled, level_tag *lvl) {
    Vec_Copy(pos, &player_start[player_start_positions_index].pos);
    Vec_Copy(rot, &player_start[player_start_positions_index].rot);
    player_start[player_start_positions_index].isEnabled = maybeEnabled;
    memcpy(&player_start[player_start_positions_index].levelData, lvl, sizeof(level_tag_PlayerStartPosition));
    player_start_positions_index++;
}

// AUTOINJECT
void Player_SetCamMode(BLData *player, unsigned short newMode) {

    // Passed as ushort, stored as (u?)char
    player->camMode = (char)newMode;

    switch((CamMode)player->camMode) {
        case CamMode_Default: // 0x00
            HUD_Reset(player);
            glb_viewer[player->playerNum]->nightVisionRelated = 0;
            break;

        case CamMode_PostMPGameThirdPerson:

            // Ghidra can't identify where these are read, but done
            // for completeness anyway
            player->someMPCameraThing1 = 1.5f;
            player->someMPCameraThing2 = 0.15f;
            player->someMPCameraThing3 = 1.5f;
            player->someMPCameraThing4 = 0;

            HUD_Reset(player);
            glb_viewer[player->playerNum]->nightVisionRelated = 0;
            break;
        
        case CamMode_Redeemer:
            HUD_Reset(player);
            glb_viewer[player->playerNum]->nightVisionRelated = 0;
            HUD_Enable(player->hudInfo, Redeemer, 1, 0);
            break;
        
        case CamMode_RCCar:
            HUD_Reset(player);
            glb_viewer[player->playerNum]->nightVisionRelated = 0;
            HUD_Enable(player->hudInfo, RCCar, 1, 0);
            break;

        case CamMode_Ronin:
            HUD_Reset(player);
            glb_viewer[player->playerNum]->nightVisionRelated = 0;
            HUD_Enable(player->hudInfo, Ronin, 1, 0);
            break;

    }

}

// AUTOINJECT
void Player_ChangeState(obj_tag* obj, unsigned short newState) { 
    obj->curState = newState;
}

// AUTOINJECT
void Player_CheckWeaponsLoaded(BLData *blData) {

    for(int i = 0; i < 114; i++) { // Unclear why it goes to 114 rather than 115 (ie the data size)?
        if(weapon_data[i].isBaseWeapon) {

            HASHCODE model = weapon_data[i].weaponModelHashcode;
            
            if((model == 0) || hashtable_getitem(model) == NULL) {
                blData->weaponStats[i].enabled = false;
            }

        }
    }
}

// AUTOINJECT
short Player_AmmoIndex(short weaponIndex) {
     // Alt-fire weapons which don't switch ammo types (eg burst, silenced weapons but not grenade alt-fire mode of OICW) 
    // use the base weapon index for the ammo
    if(weapon_data[weaponIndex].offsetToNextAltFireVariant != 0 && weapon_data[weapon_data[weaponIndex].weaponBaseNum].ammoType == weapon_data[weaponIndex].ammoType) {
        weaponIndex = weapon_data[weaponIndex].weaponBaseNum;
    }
    return weaponIndex;
}

// AUTOINJECT
ushort Player_AmmoInGun(BLData *playerInfo, ushort weaponIndex) {
    return playerInfo->weaponStats[Player_AmmoIndex(weaponIndex)].clipOrCooldown;
}

// AUTOINJECT
void Player_CreateSight(obj_tag *playerObj, byte viewerNum) {

    obj_tag* sightObj = control_create_object(0,NULL, NULL, NULL);
    BLData *blData = (BLData *)playerObj->extraObjectData;

    if (sightObj == NULL) 
        return;
    
    sightObj->renderType |= 2;
    sightObj->objectType = OBJECTTYPE_DELETED;
    sightObj->effectFlags |= 0x20;
    sightObj->maybeParent = playerObj;
    sightObj->tweakR = 0x7f;
    sightObj->tweakG = 0;
    sightObj->tweakB = 0;
    hashtable_set_object_to_entity_gfx(sightObj, (HASHCODE)0x2000132);
    View_SetDrawInThisViewOnly(sightObj, viewerNum);
    blData->sightObj = sightObj;
  
}

// AUTOINJECT
void Player_CreateMuzzleFlash(obj_tag *playerObj, byte viewerNum) {
  
    obj_tag* muzzleFlashObj = control_create_object(0,NULL, NULL, NULL);
    BLData *blData = (BLData *)playerObj->extraObjectData;
    
    if (muzzleFlashObj == NULL) 
        return;
    
    muzzleFlashObj->maybeParent = playerObj;
    muzzleFlashObj->objectType = OBJECTTYPE_DELETED;
    muzzleFlashObj->effectFlags |= 0x8221;
    View_SetDrawInThisViewOnly(muzzleFlashObj,viewerNum);
    blData->muzzleFlashObj = muzzleFlashObj;
    blData->muzzleFlashRelated = 0;
}

// Only used for laser and taser beams
// AUTOINJECT
void PositionBeam(obj_tag *param_1, _VECTOR *param_2, _VECTOR *param_3) {

    Vec_Copy(param_2, &param_1->position);

    float distance = Vec_Dist3D(param_2, param_3);

    param_1->renderType &= 0xdf;
    param_1->renderType |= 1;
    param_1->renderType |= 0x20;

    param_1->scale = distance;

    param_1->radius = 2.0f * distance;

    float dx = param_2->x - param_3->x;
    float dy = param_2->y - param_3->y;
    float dz = param_2->z - param_3->z;

    vecutil_cartesian_to_spherical_acc(&param_1->rotation, dx, dy, dz);

    param_1->rotation.x *= -1;
    param_1->rotation.z = 0.0f;

}

// AUTOINJECT
void Player_SetupLaser(BLData *param_1, _VECTOR *targetPos) {
  
    _VECTOR sourcePos;

    AnimGetBoneWorldTrans(param_1->weaponObject, 0, 0, &sourcePos, (_MATRIX *)0x0);
    
    obj_tag* poVar1 = param_1->weaponRelatedObjs[0];

    poVar1->maybeBrightness = 0xff;
    
    // Strong or weak beam ("lazer" 0x2000131 or "uv_lazer" 0x20006c1)
    HASHCODE hc = (glb_players[param_1->playerNum]->animState->currentWeaponId == 0x4e) ? (HASHCODE)0x2000131 : (HASHCODE)0x20006c1;

    hashtable_set_object_to_entity_gfx(poVar1, hc);
    View_SetDrawInAllViews(poVar1);
    PositionBeam(poVar1, &sourcePos, targetPos);

}

// AUTOGEN
obj_tag* Player_Init(ushort playerNum, _VECTOR *pos, _VECTOR *rot, level_tag *spawnPointData);

// AUTOGEN
void Player_GetHeadPos(_VECTOR *headPosOut, obj_tag *player, _MATRIX *param_3);

// AUTOINJECT
void Player_Start(void) {

    // Find the first locaton which is both enabled, and either it doesn't require a switch, or its corresponding switch channel is active
    for(int i = 0; i < player_start_positions_index; i++) {

        if(player_start[i].isEnabled && ((player_start[i].levelData.requiredSwitch == 0) || (switch_channels[player_start[i].levelData.requiredSwitch] != 0))) {
            Player_Init(0, &player_start[i].pos, &player_start[i].rot, (level_tag*)&player_start[i].levelData);
            return;
        }

    }

    // No ideal spawn was found. Less ideally, spawn in the first enabled position, ignoring the switches
    for(int i = 0; i < player_start_positions_index; i++) {

        if(player_start[i].isEnabled) {
            player_start[i].levelData.requiredSwitch = 0; // Remove the spawn point's requirement for this switch channel - unclear what this does
            Player_Init(0, &player_start[i].pos, &player_start[i].rot, (level_tag*)&player_start[i].levelData);
            return;
        }

    }

    // No spawn point was active, return without spawning anything
    return;

}

// AUTOINJECT
void Player_WeaponNone(obj_tag* obj) {

    BLData* blData = (BLData*)obj->extraObjectData;
    
    obj->animState->prevHeldWeaponId = obj->animState->currentWeaponId;
    obj->animState->switchingToWeaponId = Weap_MaybeHandsOnlyOrLadder;
    obj->animState->currentWeaponId = Weap_MaybeHandsOnlyOrLadder;
    
    blData->weaponObject->curState = 0;
    blData->muzzleFlashRelated = 0;
    blData->lensFlareRelated = 1.0f;

    obj->animState->animFlags &= 0xfe;

}


// The game's own view pitch clamp, and the one place mouse look reaches the player.
//
// Player_Update runs this immediately after Player_Aiming and before it uses either angle: the yaw in
// rotationDelta.y is added to the object's rotation a few lines further on, and the pitch here is what the
// camera reads. So this is the moment when the frame's aim has been decided but not yet acted on, which is
// exactly where an extra contribution belongs - and, the point of doing it here rather than by pretending
// to be a stick, it is past the deadzone in psiInput_PollDevices and past the AccelFunc0 ramps in
// Player_Move and Player_Aiming. See engine/mouseLook.h for why neither should apply to a mouse.
//
// Player 0 only. Split-screen players are on pads; there is one mouse.
//
// AUTOINJECT
void Player_ViewClamping(obj_tag *player) {

    BLData *blData = (BLData*)player->extraObjectData;

    // Bit 0 of animFlags is "weapon is zoomed in" (engine/Anim.h). Mouse look needs it for the wheel, which
    // adjusts the zoom instead of changing weapon while it is set, and for aim speed.
    if (blData->playerNum == 0) {
        MouseLook_SetScoped((player->animState->animFlags & 1) != 0);
    }

    float yawRadians = 0.0f, pitchFraction = 0.0f;
    if (blData->playerNum == 0 && MouseLook_TakeAimDelta(&yawRadians, &pitchFraction)) {

        if (player->subState == MovementType_ZeroG || player->subState == MovementType_ZeroG_Anim) {
            // Zero G does not steer the way everything else does. Player_ZeroG builds a rotation matrix from
            // the frame's turn and concatenates it straight onto the object's matrix, then holds
            // pitchFromHorizontal at zero - so by the time this runs the orientation is already baked in, and
            // adding to either of the fields the rest of the game uses achieves nothing. (That is why mouse
            // look looked completely dead in the last mission while working everywhere else.) The answer is
            // to apply a second rotation the same way, which is the three calls below, copied from what
            // Player_ZeroG itself does. Zero G also counts pitch the other way up, hence the negation.
            _VECTOR delta;
            delta.x = -(pitchFraction * 1.5707964f); // back into radians, which is what RotMatrix wants
            delta.y = yawRadians;
            delta.z = 0.0f;

            _MATRIX rotation;
            RotMatrix(&delta, &rotation);
            Concat(player->transformMatrix.m, rotation.m);
            Mat_CopyRot(&rotation, &player->transformMatrix);
        }
        else {
            blData->rotationDelta.y += yawRadians;
            blData->pitchFromHorizontal += pitchFraction;

            // Without this the pitch springs straight back to wherever the game last decided the view should
            // settle: Player_ClampSomeAngles eases pitchFromHorizontal towards pitchAutoLevelTarget by 7.5% a
            // frame whenever aimAutoLevelState is 2 or 3, which at 50fps undoes the whole movement in about a
            // quarter of a second. Player_Aiming sets the state to 1 on every frame the stick moves the view,
            // for exactly this reason, so mouse look says the same thing rather than inventing its own escape.
            blData->aimAutoLevelState = 1;
        }
    }

    // Unchanged from the original, and deliberately after the above so that the mouse cannot drive the view
    // past straight up or straight down either.
    if (blData->pitchFromHorizontal < -1.0f) {
        blData->pitchFromHorizontal = -1.0f;
    }
    else if (blData->pitchFromHorizontal > 1.0f) {
        blData->pitchFromHorizontal = 1.0f;
    }

}

// ---------------------------------------------------------------------------------------------------------------
// Walking, strafing and turning on foot. Transliterated from 0x000a9cf0 so that one line of it can change:
// the original refuses to walk forwards or backwards while the scope is up.
//
//     if ((player->animState->animFlags & 1) != 0) { forward = 0; turn = 0; }
//
// Strafing is deliberately left alone, which is why scoped movement is sideways-only in the original. That is
// a concession to aiming a scope with a thumbstick; with a mouse it is just an obstruction, so it is lifted
// while the pointer is captured and left exactly as it was otherwise. Suppressing the *turn* is not optional
// either way - while scoped, Player_Aiming turns the player through its own scope-speed path, and leaving
// this one in as well would add the two together.
//
// Everything else here is the original's arithmetic, reproduced rather than improved:
//
//  - the walk axis is shaped by a square law with a small bias, and backwards is 75% of forwards;
//  - both ground axes ease 20% of the way towards their target per frame, without overshooting it, snap to
//    zero once the stick is centred and the residue is under 0.01, and clamp to +-FRAME_RATE_MUL * 0.1;
//  - the turn goes through AccelFunc0, which carries its state in blData->turnAccelState;
//  - swimming adds the forward component along the view direction instead of the object's own forward, and
//    accumulates into the movement vector rather than replacing it.
//
// ABI note, since this calls two functions by address: AccelFunc0 (0x000b7420) ends in a bare RET, not
// RET 0x1c, so it is caller-cleans __cdecl despite Ghidra labelling it __stdcall - taking that label at face
// value would unbalance the stack by 28 bytes. It returns its float in ST0, which is what MSVC expects.
// ---------------------------------------------------------------------------------------------------------------

// Tuning values the original passes straight through to AccelFunc0. Only the first has a name in the
// symbols; the other two sit next to it in the same table and are read by address, as the original does.
#define Ply_NoAimTurnSpeed_X FLOAT_AT(0x00181a70) // 0.04
#define Ply_TurnAccelParamA  FLOAT_AT(0x00181a74) // 2.0
#define Ply_TurnAccelParamB  FLOAT_AT(0x00181a78) // 120.0

// False while dying or dead, and in the substates that move the player themselves (climbing, wire, creeping,
// zipline). Called rather than reproduced, so that naming it in Ghidra later does not leave two copies.
static bool PlayerMoveAllowed(obj_tag *player) {
    return reinterpret_cast<bool (__cdecl *)(obj_tag *)>(0x000a9930)(player);
}

static float TurnAccel(float channel, float *state, float a, float b, float c, float d, float e) {
    return reinterpret_cast<float (__cdecl *)(float, float *, float, float, float, float, float)>(0x000b7420)
        (channel, state, a, b, c, d, e);
}

static void RotMatrixX(float angle, float *matrix) {
    reinterpret_cast<void (__cdecl *)(float, float *)>(0x000d67b0)(angle, matrix);
}

// Eases value towards target by 20% of the gap, stopping there rather than overshooting it.
static float EaseTowards(float value, float target) {
    float gap = target - value;
    float moved = FRAME_RATE_MUL * 0.2f * gap + value;
    if (gap < 0.0f) {
        return (moved < target) ? target : moved;
    }
    return (target < moved) ? target : moved;
}

static float ClampToStep(float value) {
    float limit = FRAME_RATE_MUL * 0.1f;
    if (value < -limit) return -limit;
    if (value > limit) return limit;
    return value;
}

// AUTOINJECT
void Player_Move(BLData *blData, obj_tag *player, float speedScale) {

    if (!PlayerMoveAllowed(player)) {
        return;
    }

    float forward = Input_Actionf(blData->playerNum, ACTION_WALK_F_B, 1);
    float strafe  = Input_Actionf(blData->playerNum, ACTION_WALK_L_R, 1);
    float turn    = -Input_Actionf(blData->playerNum, ACTION_AIM_L_R, 1);

    char weapon = player->animState->currentWeaponId;
    if (((weapon == 0x50) || (weapon == 0x51)) && (blData->weaponObject->curState == 9)) { // the two grapples, reeling in
        forward = 0.0f;
        strafe = 0.0f;
        turn = 0.0f;
    }
    if (blData->movementDisabled != '\0') {
        forward = 0.0f;
        strafe = 0.0f;
        turn = 0.0f;
    }
    if ((player->animState->animFlags & 1) != 0) { // scoped
        if (!MouseLook_Captured()) {
            forward = 0.0f;
        }
        turn = 0.0f;
    }
    if ((blData->previousSubState == 7) && (blData->maybeCamRelatedCountdown != '\0')) {
        strafe = 0.0f;
    }

    if (forward > 0.0f) {
        forward = FRAME_RATE_MUL * 0.005f + FRAME_RATE_MUL * 0.1f * forward * forward;
    }
    else if (forward < 0.0f) {
        forward = FRAME_RATE_MUL * 0.1f * -0.75f * forward * forward - FRAME_RATE_MUL * 0.005f;
    }
    strafe = FRAME_RATE_MUL * 0.075f * strafe;

    float sideways = EaseTowards(blData->lastMovement.x, strafe);
    if ((strafe >= -0.0002f) && (strafe <= 0.0002f) && (fabsf(sideways) < 0.01f)) {
        sideways = 0.0f;
    }
    sideways = ClampToStep(sideways);

    float forwards = EaseTowards(blData->lastMovement.z, forward);
    if ((forward >= -0.0002f) && (forward <= 0.0002f) && (fabsf(forwards) < 0.01f)) {
        forwards = 0.0f;
    }
    forwards = ClampToStep(forwards);

    if ((GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1) && (switch_channels[0x48] == '\0')) {
        speedScale = speedScale * 0.9f;
    }
    float moveZ = forwards * speedScale;
    float moveX = sideways * speedScale;

    float accel = TurnAccel(turn, &blData->turnAccelState, Ply_NoAimTurnSpeed_X,
                            Ply_TurnAccelParamA, Ply_TurnAccelParamB, 0.05f, 0.95f);
    blData->turnAccelState = accel;
    float yaw = accel * FRAME_RATE_MUL * turn;

    if (player->subState != MovementType_Swim) {
        blData->movement.x = moveX;
        blData->movement.z = moveZ;
        blData->rotationDelta.y = yaw;
        return;
    }

    // Swimming goes where the player is looking, and adds to the movement vector rather than replacing it.
    _MATRIX pitched;
    _VECTOR direction;
    RotMatrixX(player->rotation.x - blData->pitchFromHorizontal * 1.5707964f, pitched.m);
    Mat_GetDir(&direction, &pitched);
    auxVec_AddMulR32(&blData->movement, &direction, moveZ, &blData->movement);
    blData->movement.x = moveX;
    blData->rotationDelta.y = yaw;
}
