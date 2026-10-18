#include "Player.h"
#include "Pickup.h"
#include "../Upgrade.h"
#include "../mp/multiplayer.h"
#include "../view.h"
#include "../../engine/viewer.h"
#include "../../engine/Anim.h"
#include "../../engine/mouseLook.h"
#include "../../engine/mouseSteer.h"
#include "../../engine/viewFov.h"
#include "../../engine/Camera.h"
#include "../../engine/Collide.h"
#include "GT.h"

#include "../../input.h"
#include "../../game.h"
#include "../sp/SwitchChannels.h"
#include "../drone/DroneTuning.h"
#include "../drone/Drone.h"
#include "../drone/BOT.h"
#include "../drone/DroneWeapon.h"
#include "../../sound/music.h"
#include "bullet.h"
#include "car.h"
#include "control.h"
#include "build.h"
#include "Light.h"
#include "Lock.h"
#include "Door.h"
#include "Trigger.h"
#include "SS.h"
#include "Monitor.h"
#include "PCQWorm.h"
#include "GunImp.h"
#include "Hurt.h"
#include "ScriptPlayer.h"
#include "../../ui/HUD.h"
#include "../../ui/MenuManager.h"
#include "../sp/PlayerStats.h"
#include "../../math/X87Ops.h"
#include <bit>
#include <math.h>
#include <string.h>

#include "Env.h"

// XBE_GLOBAL(0x002774c8, 0x4)
static uint32_t player_start_positions_index;
// XBE_GLOBAL(0x002774cc, 0x4)
static uint32_t player_start_position; // Unused - only ever written?
// XBE_GLOBAL(0x002774d0, 0x17e8)
static PlayerStartPosition player_start[0x1e];

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

// The original's, called by address: Ghidra's prototypes have untyped arguments.

// What Player_Enable looks for around the player when asked to (the radius, the collision flags).
#define PLAYER_ENABLE_ATTACH_RADIUS 2.0f
#define PLAYER_ENABLE_ATTACH_FLAGS 0x8c0

// Takes the player out of play for a cutscene: hidden with his weapon, and not hit-tested. With freeze, he only
// stops where he is.
// AUTOINJECT
void Player_Disable(obj_tag *player, char freeze) {
    if (player == NULL)
        return;

    BLData *blData = (BLData *)player->extraObjectData;
    if (freeze) {
        blData->frozen = 1;
        return;
    }

    blData->active = 0;
    player->effectFlags |= FLAG_HIDDEN;
    blData->hitTest.type = 0;
    if (blData->weaponObject != NULL)
        blData->weaponObject->effectFlags |= FLAG_HIDDEN;
}

// Puts the player back in play after Player_Disable, placed at mtx if given (facing along its direction), with his
// movement and the controls cleared. With checkAttach, a wire or creep wall within reach takes him on.
// AUTOINJECT
void Player_Enable(obj_tag *player, _MATRIX *mtx, int checkAttach) {
    if (player == NULL)
        return;

    BLData *blData = (BLData *)player->extraObjectData;
    blData->active = 2;
    blData->frozen = 0;
    player->effectFlags &= ~FLAG_HIDDEN;
    if (mtx != NULL) {
        _VECTOR dir;
        Mat_GetDir(&dir, mtx);
        Vec_Copy(Mat_Position(*mtx), &player->position);
        vecutil_cartesian_to_spherical_acc(&player->rotation, dir.x, dir.y, dir.z);
        Vec_Copy(&player->position, &player->lastPosition);
        Vec_Copy(&player->rotation, &player->lastRotation);
    }

    blData->hitTest.type = 0x800;
    if (blData->weaponObject != NULL)
        blData->weaponObject->effectFlags &= ~FLAG_HIDDEN;
    blData->jumpState = 0;
    blData->jumpWait = 0;
    Vec_Zero(&blData->field_0x3c);
    BLData *movement = (BLData *)player->extraObjectData;
    Vec_Zero(&movement->lastMovement);
    Vec_Zero(&movement->lastRotationDelta);
    Vec_Zero(&movement->movement);
    Vec_Zero(&movement->rotationDelta);
    Input_ClearAllActions(-1);
    control_unlink_object(player);
    build_LinkToRoom(player, 1, glb_world);

    if (checkAttach) {
        HITDATA_tag *hitList = NULL;
        Collide_SphereIntersect(&player->position, PLAYER_ENABLE_ATTACH_RADIUS, player->inCel, player, NULL, &hitList,
                                0, 1, PLAYER_ENABLE_ATTACH_FLAGS, 4);
        for (HITDATA_tag *hit = hitList; hit != NULL; hit = hit->next) {
            obj_tag *hitObj = hit->hitObj;
            if (hitObj == NULL)
                continue;
            if (hitObj->objectType == OBJECTTYPE_THIRDCAM_OR_WIRE) {
                Player_CollWire(player, blData, hitObj);
                break;
            }
            if (hitObj->objectType == OBJECTTYPE_CREEPWALL) {
                Player_CollCreepWall(player, blData, hitObj);
                break;
            }
        }
        Collide_FreeHitList(&hitList);
    }

    control_unlink_object(player);
    build_LinkToRoom(player, 1, glb_world);
}

// The camera mode a script asks for (event 9): 3 is the third-person view (camera mode 2); anything else is the
// first-person view, level, with the player on foot.
// AUTOINJECT
void Player_Cam2Mode(ushort playerNum, uint mode) {
    if (glb_blokes[playerNum] == NULL)
        return;

    if (mode == 3) {
        Player_SetCamMode(glb_blokes[playerNum], 2);
        return;
    }

    glb_blokes[playerNum]->pitchFromHorizontal = 0.0f;
    Player_SetCamMode(glb_blokes[playerNum], 0);
    Player_ChangeSubState(glb_players[playerNum], MovementType_Walk);
}

// An explosion's damage, as pain of no kind in no place; the health left
// AUTOINJECT
float Player_Hurt(obj_tag *player, float damage, _VECTOR *from) {
    BLData *blData = (BLData *)player->extraObjectData;
    _Player_HandlePain(player, damage, blData, -1, 0);
    return blData->health;
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

// Gives the player ammo for a weapon: fills an empty gun first, for weapons that load a clip (someFlags 0x10),
// and puts the rest in the carried stock up to the ammo type's maximum. Returns how much was taken, loaded plus
// stocked (only the low 16 bits mean anything - the original leaves the rest of EAX as it was). Nothing is taken
// when the stock is already full, even if the gun is empty.
//
// The two sniper rifles' pickups (0x1f, 0x25) stand for whichever sniper the player's upgrade level gives: the
// weapon after that one in weapon_data, which takes the same ammo.
// AUTOINJECT
uint Player_EquipAmmo(BLData *playerInfo, short weaponIndex, short amount) {
    uchar sniperUpgrade = Upgrades[0][WUG_Sniper];
    if (weaponIndex == 0x1f)
        weaponIndex = (short)(UpgradedSnipers[sniperUpgrade] + 1);
    if (weaponIndex == 0x25)
        weaponIndex = (short)(UpgradedSilencedSnipers[sniperUpgrade] + 1);

    weapon_definition_tag *weapon = &weapon_data[weaponIndex];
    if (playerInfo->ammo[weapon->ammoType] >= ammo_data[weapon->ammoType].maybeMaxNumPerPlayer)
        return 0;
    if (amount == 0)
        return 0;

    short loaded = 0;
    if ((weapon->weaponFlags & 0x10) && playerInfo->weaponStats[Player_AmmoIndex(weaponIndex)].clipOrCooldown == 0) {
        loaded = (amount < weapon->clipSize) ? amount : weapon->clipSize;
        playerInfo->weaponStats[Player_AmmoIndex(weaponIndex)].clipOrCooldown = loaded;
        amount -= weapon->clipSize;
        if (amount <= 0)
            return (ushort)loaded;
    }

    short before = playerInfo->ammo[weapon->ammoType];
    playerInfo->ammo[weapon->ammoType] = before + amount;
    short max = ammo_data[weapon->ammoType].maybeMaxNumPerPlayer;
    if (playerInfo->ammo[weapon->ammoType] >= max)
        playerInfo->ammo[weapon->ammoType] = max;
    return (ushort)(playerInfo->ammo[weapon->ammoType] - (loaded + before));
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
    
    sightObj->transformFlags |= TRANSFORM_KEEP_ROTATION;
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
    blData->muzzleFlashFrames = 0;
}

// Only used for laser and taser beams
// AUTOINJECT
void PositionBeam(obj_tag *param_1, _VECTOR *param_2, _VECTOR *param_3) {

    Vec_Copy(param_2, &param_1->position);

    param_1->transformFlags &= ~TRANSFORM_MOVED;

    float distance = Vec_Dist3D(param_2, param_3);

    param_1->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY;
    param_1->scale = distance;

    Vec_Copy(param_2, &param_1->centrePoint);
    param_1->transformFlags |= TRANSFORM_MOVED;
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
    
    obj_tag* poVar1 = param_1->beamObjs[0];

    poVar1->tweakA = 0xff;
    
    // Strong or weak beam ("lazer" 0x2000131 or "uv_lazer" 0x20006c1)
    HASHCODE hc = (glb_players[param_1->playerNum]->animState->currentWeaponId == 0x4e) ? (HASHCODE)0x2000131 : (HASHCODE)0x20006c1;

    hashtable_set_object_to_entity_gfx(poVar1, hc);
    View_SetDrawInAllViews(poVar1);
    PositionBeam(poVar1, &sourcePos, targetPos);

}

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
    NF_WARN_IF(player_start_positions_index > 0, "MAJOR PROBLEM\n"); // GC check (0x800debe8)
    return;

}

// AUTOINJECT
void Player_WeaponNone(obj_tag* obj) {

    BLData* blData = (BLData*)obj->extraObjectData;
    
    obj->animState->prevHeldWeaponId = obj->animState->currentWeaponId;
    obj->animState->switchingToWeaponId = Weap_MaybeHandsOnlyOrLadder;
    obj->animState->currentWeaponId = Weap_MaybeHandsOnlyOrLadder;
    
    blData->weaponObject->curState = 0;
    blData->muzzleFlashFrames = 0;
    blData->zoom = 1.0f;

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
    if (blData->playerNum == 0 && MouseSteer_IsRemoteControl(player->subState)) {
        // Controlling something remotely: the guided missile, an RC car or helicopter, an emplacement or the
        // Ronin. Player_Aiming leaves the player alone in these substates, since the sticks are steering the
        // device - so the mouse does the same, and engine/mouseSteer.cpp hands the movement to the device. Once
        // the device has gone (the burst of static after the missile blows up), the movement is thrown away
        // rather than turning the player where nobody can see it.
        if (blData->remoteControlDevice != NULL)
            MouseLook_LeaveForSteering();
        else
            MouseLook_TakeAimDelta(&yawRadians, &pitchFraction);
    }
    else if (blData->playerNum == 0 && MouseLook_TakeAimDelta(&yawRadians, &pitchFraction)) {

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
            Concat(&player->transformMatrix, &rotation);
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
// Walking, strafing and turning on foot (0x000a9cf0):
//
//  - with the scope up there is no walking forwards or backwards and no turning here (Player_Aiming turns
//    through the scope's own path), so scoped movement is sideways only;
//  - the walk axis is shaped by a square law with a small bias, and backwards is 75% of forwards;
//  - both ground axes ease 20% of the way towards their target per frame, without overshooting it, snap to
//    zero once the stick is centred and the residue is under 0.01, and clamp to +-FRAME_RATE_MUL * 0.1;
//  - the turn goes through AccelFunc0, which carries its state in blData->turnAccelState;
//  - swimming adds the forward component along the view direction instead of the object's own forward, and
//    accumulates into the movement vector rather than replacing it.
// ---------------------------------------------------------------------------------------------------------------

// XBE_GLOBAL(0x00181a70, 0x4)
static float Plr_NoAimTurnSpeed_X = 0.04f;
// XBE_GLOBAL(0x00181a74, 0x4)
static float Plr_NoAimTurnSpeed_X_Mul = 2.0f;
// XBE_GLOBAL(0x00181a78, 0x4)
static float Plr_NoAimTurnSpeed_X_Steps = 120.0f;

// False while dying or dead, and in the substates that move the player themselves (climbing, wire, creeping,
// zipline). Called rather than reproduced, so that naming it in Ghidra later does not leave two copies.
static bool PlayerMoveAllowed(obj_tag *player) {
    return reinterpret_cast<bool (__cdecl *)(obj_tag *)>(0x000a9930)(player);
}

// Eases value towards target by rate of the gap, stopping there rather than overshooting it. On the x87 the gap is
// stored as a float and the eased value stays unrounded.
static double EaseTowards(float value, float target, double rate) {
    float gap = target - value;
    double moved = rate * gap + value;
    if (gap < 0.0f) {
        return (moved < target) ? target : moved;
    }
    return (target < moved) ? target : moved;
}

// The lower bound is the limit as the x87 holds it, the upper one the limit stored as a float
static float ClampToStep(float value, double limit) {
    if (value < -limit) return (float)-limit;
    if (value > (float)limit) return (float)limit;
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
        forward = 0.0f;
        turn = 0.0f;
    }
    if ((blData->previousSubState == 7) && (blData->traverseWait != '\0')) {
        strafe = 0.0f;
    }

    // On the x87 each of these is rounded once, when stored
    if (forward > 0.0f) {
        forward = (float)((double)FRAME_RATE_MUL * 0.1f * forward * forward + (double)FRAME_RATE_MUL * 0.005f);
    }
    else if (forward < 0.0f) {
        forward = (float)((double)FRAME_RATE_MUL * 0.1f * -0.75f * forward * forward - (double)FRAME_RATE_MUL * 0.005f);
    }
    strafe = (float)((double)FRAME_RATE_MUL * 0.075f * strafe);

    // The sideways ease uses the rate as the x87 holds it, the forwards one the rate stored as a float
    double easeRate = (double)FRAME_RATE_MUL * 0.2f;
    double limit = (double)FRAME_RATE_MUL * 0.1f;

    double eased = EaseTowards(blData->lastMovement.x, strafe, easeRate);
    float sideways = (float)eased;
    if ((strafe >= -0.0002f) && (strafe <= 0.0002f) && (fabs(eased) < 0.01f)) {
        sideways = 0.0f;
    }
    sideways = ClampToStep(sideways, limit);

    eased = EaseTowards(blData->lastMovement.z, forward, (float)easeRate);
    float forwards = (float)eased;
    if ((forward >= -0.0002f) && (forward <= 0.0002f) && (fabs(eased) < 0.01f)) {
        forwards = 0.0f;
    }
    forwards = ClampToStep(forwards, limit);

    // Channel 0x48 is "cover blown": set once Bond has rendezvoused with Zoe and been told to stop
    // playing a party guest. Until then he walks rather than jogs, and Player_HandleJump refuses to let
    // him jump at all. See docs/switch-channels.md for the rest of the hard-coded channels, and for why
    // a channel number need not mean the same thing in another level.
    // The scaled speed stays unrounded on the x87
    double speed = speedScale;
    if ((GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1) && (switch_channels[0x48] == '\0')) {
        speed = (double)speedScale * 0.9f;
    }
    float moveZ = (float)(forwards * speed);
    float moveX = (float)(sideways * speed);

    double accel = _AccelFunc0(turn, &blData->turnAccelState, Plr_NoAimTurnSpeed_X,
                             Plr_NoAimTurnSpeed_X_Mul, Plr_NoAimTurnSpeed_X_Steps, 0.05f, 0.95f);
    blData->turnAccelState = (float)accel;
    float yaw = (float)(accel * FRAME_RATE_MUL * turn);

    if (player->subState != MovementType_Swim) {
        blData->movement.x = moveX;
        blData->movement.z = moveZ;
        blData->rotationDelta.y = yaw;
        return;
    }

    // Swimming goes where the player is looking, and adds to the movement vector rather than replacing it.
    _MATRIX pitched;
    _VECTOR direction;
    RotMatrixX((float)(player->rotation.x - (double)blData->pitchFromHorizontal * 1.5707964f), &pitched);
    Mat_GetDir(&direction, &pitched);
    auxVec_AddMulR32(&blData->movement, &direction, moveZ, &blData->movement);
    blData->movement.x = moveX;
    blData->rotationDelta.y = yaw;
}

// AnimScriptAdd, with the script's playback speed set (sAnimScript_tag +0x94) when it starts.
// AnimScriptAdd, played backwards: raises the "reverse" flag at 0x001d7828 and tail-calls AnimScriptAdd.
// Leaving the scope: target zoom back to 1x, view auto-levelling suppressed (aimAutoLevelState = 1), walk
// animation reset, zoom motor sound stopped. Not a function on PS2 (inlined in Player_Weapon), so the name is
// invented.
// AUTOGEN
void Player_ScopeOff(obj_tag *param_1);
// Sets the zoom target and limit for the CURRENT weapon: 1x, or the zoom remembered for this weapon base when
// the player's "remember zoom" option is on and the scope is up, clamped to weapon_data[].maxZoom. Also
// inlined on PS2; the name is invented.
// AUTOGEN
void Player_ResetZoom(obj_tag *param_1);

static_assert(offsetof(weapon_definition_tag, offsetToNextAltFireVariant) == 0x05, "offsetToNextAltFireVariant");
static_assert(offsetof(weapon_definition_tag, numFireModes) == 0x2e, "numFireModes");
static_assert(offsetof(weapon_definition_tag, weaponFlags) == 0x68, "weaponFlags");
static_assert(offsetof(weapon_definition_tag, animScopeIn) == 0xb8, "animScopeIn");
static_assert(offsetof(weapon_definition_tag, animScopeOut) == 0xbc, "animScopeOut");

// AnimState::animFlags bits. SCOPED is what the player asked for this frame (after the weapon has had its say);
// SCOPE_APPLIED is what the zoom was last set up for, so the edge between them runs the enter/leave code once.
#define ANIMFLAG_SCOPED        0x01
#define ANIMFLAG_SCOPE_APPLIED 0x02

// weapon_definition_tag::someFlags bits this function reads.
// 0x40: a proper sighted scope - more weapon states knock it down (reload-like states 6-8 and 16), and the
// alt-fire button does nothing while it is up (the sniper rifles, flags 0x408050/0x4080d0).
#define WPNFLAG_SIGHTED_SCOPE      0x40
// 0x8000: in weapon state 9 the scope may stay up if it already was, but cannot be raised.
#define WPNFLAG_SCOPE_HOLD_STATE9  0x8000

// Weapon object (BLData::weaponObject->curState) states set here: the bring-to-eye and lower-from-eye
// animations of weapons that have one (someAnimHC2 non-zero).
#define WEAPONSTATE_IDLE        0x00
#define WEAPONSTATE_SCOPE_RAISE 0x0f
#define WEAPONSTATE_SCOPE_LOWER 0x10

// Player_WeaponChange's third argument: 0 cycles weapons, 1 cycles gadgets, 2 is the party-guest restricted
// cycle used in CastleIndoors1 before channel 0x48 ("cover blown") is set.
#define WEAPONCHANGE_WEAPONS   0
#define WEAPONCHANGE_GADGETS   1
#define WEAPONCHANGE_UNARMED   2

// Player_Weapon runs every frame for the player: scope up/down (hold or toggle, per the player's option),
// the scope's bring-to-eye animations, alt-fire (switching to the alt-fire variant, or cycling a fire-mode
// counter), gadget and weapon cycling, CastleIndoors1's hand-over of the P2K, then zoom and auto-aim.
// AUTOINJECT
void Player_Weapon(obj_tag *player) {
    AnimState *anim = player->animState;
    BLData *blData = (BLData *)player->extraObjectData;
    short subState = (short)player->subState;
    char weaponId = anim->currentWeaponId;
    player->transformFlags |= TRANSFORM_MOVED;
    char playerNum = blData->playerNum;
    weapon_definition_tag *weapon = &weapon_data[weaponId];

    // No scope handling on a ladder whose first short is set, nor on a wire or while creeping.
    bool handleScope;
    if (subState == MovementType_Climb) {
        handleScope = (((ObjData_Ladder *)blData->attachedToSpecialMovementItem->extraObjectData)->unknown0 == 0);
    }
    else {
        handleScope = (subState <= 5) || (subState > 7); // not MovementType_Wire or MovementType_Creep
    }

    if (handleScope) {
        char wasScoped = anim->animFlags & ANIMFLAG_SCOPED;

        if (PlayerInputs[playerNum].manualAimToggle == 0) {
            // Hold to aim: scoped exactly while the button is down
            anim->animFlags &= ~ANIMFLAG_SCOPED;
            if (Input_Action(playerNum, ACTION_AIM_ZOOM_SCOPE, 1)) {
                player->animState->animFlags |= ANIMFLAG_SCOPED;
            }
        }
        else {
            // Toggle: flip on each press
            if (Input_Action(playerNum, ACTION_AIM_ZOOM_SCOPE, 4)) {
                player->animState->animFlags ^= ANIMFLAG_SCOPED;
            }
        }

        // States in which the weapon forces the scope down (jump tables at 0x000bad0c/0x000bad28).
        bool forceDown = false;
        uint flags = weapon_data[weaponId].weaponFlags;
        switch (blData->weaponObject->curState) {
            case 1: case 2: case 3: case 4: case 10: case 13: case 14: case 15:
                forceDown = true;
                break;
            case 6: case 7: case 8: case 16:
                forceDown = (flags & WPNFLAG_SIGHTED_SCOPE) != 0;
                break;
            case 9:
                forceDown = (flags & WPNFLAG_SIGHTED_SCOPE) && (flags & WPNFLAG_SCOPE_HOLD_STATE9) && !wasScoped;
                break;
        }
        if (forceDown) {
            player->animState->animFlags &= ~ANIMFLAG_SCOPED;
        }

        AnimState *animNow = player->animState;
        char isScoped = animNow->animFlags & ANIMFLAG_SCOPED;
        if ((isScoped != wasScoped) && (weapon_data[weaponId].animScopeIn != 0)) {
            if (isScoped) {
                // Just asked for the scope: hold the bit off and play the bring-to-eye animation first
                animNow->animFlags &= ~ANIMFLAG_SCOPED;
                blData->weaponObject->curState = WEAPONSTATE_SCOPE_RAISE;
                AnimScriptAddSpeed(blData->weaponObject, weapon_data[weaponId].animScopeIn, 1.25f);
            }
            else if (blData->weaponObject->curState == WEAPONSTATE_IDLE) {
                // Lowering. The upgraded camera drops back to the plain camera as it comes down.
                blData->weaponObject->curState = WEAPONSTATE_SCOPE_LOWER;
                if (weapon->weaponVariantNum == Weap_Camera_Upgraded) {
                    player->animState->currentWeaponId = Weap_Camera;
                    player->animState->switchingToWeaponId = Weap_Camera;
                    weapon = &weapon_data[player->animState->currentWeaponId];
                }
                if (weapon->animScopeOut != 0) {
                    AnimScriptAddSpeed(blData->weaponObject, weapon->animScopeOut, 1.25f);
                }
                else {
                    AnimScriptAddReverse(blData->weaponObject, weapon->animScopeIn);
                }
            }
        }
    }

    // Run the enter/leave-scope code on the edge
    AnimState *anim2 = player->animState;
    char animFlags = anim2->animFlags;
    if ((animFlags & ANIMFLAG_SCOPED) != ((animFlags >> 1) & 1)) {
        anim2->animFlags = animFlags ^ ANIMFLAG_SCOPE_APPLIED;
        if (animFlags & ANIMFLAG_SCOPED) {
            Player_ResetZoom(player);
            blData->aimAutoLevelState = 0;
            Player_ClearAimInertia(player); // inlined in the original
        }
        else {
            Player_ScopeOff(player);
        }
    }

    // Alt-fire. The camera variants also take the fire button as alt-fire. Only the low byte of each result
    // is kept (MOV BL,AL / OR BL,AL), as in the original.
    uchar altFire = (uchar)Input_Action(playerNum, ACTION_ALTFIRE, 4);
    if ((weapon->weaponVariantNum == Weap_Camera) || (weapon->weaponVariantNum == Weap_Camera_Upgraded)) {
        altFire |= (uchar)Input_Action(playerNum, ACTION_FIRE, 4);
    }
    AnimState *anim3 = player->animState;
    bool altFireBlocked = (anim3->animFlags & ANIMFLAG_SCOPED) &&
                          (weapon_data[anim3->currentWeaponId].weaponFlags & WPNFLAG_SIGHTED_SCOPE);
    if (!altFireBlocked && altFire && (blData->weaponObject->curState == WEAPONSTATE_IDLE)) {
        if (weapon->offsetToNextAltFireVariant != 0) {
            // The offset is signed: the alt-fire variant of a variant points back at it (0xff)
            short altWeapon = (short)anim3->currentWeaponId + (short)(signed char)weapon->offsetToNextAltFireVariant;
            if (Player_WeaponHasAmmo(blData, altWeapon)) {
                player->animState->switchingToWeaponId = (char)altWeapon;
                Player_ResetZoom(player);
            }
        }
        else if (weapon->numFireModes != 1) {
            // No alt-fire variant: step this weapon's fire-mode counter, wrapping at unk15 (byte compare)
            blData->weaponStats[anim3->currentWeaponId].fireModeIndex++;
            char cur = player->animState->currentWeaponId;
            if (blData->weaponStats[cur].fireModeIndex == weapon->numFireModes) {
                blData->weaponStats[cur].fireModeIndex = 0;
            }
        }
    }

    if (Input_Action(playerNum, ACTION_GADGET_NEXT, 4)) {
        Player_WeaponChange(player, 1, WEAPONCHANGE_GADGETS);
    }
    else if (Input_Action(playerNum, ACTION_GADGET_PREV, 4)) {
        Player_WeaponChange(player, -1, WEAPONCHANGE_GADGETS);
    }

    // CastleIndoors1: until channel 0x48 ("cover blown", see docs/switch-channels.md) Bond is a party guest and
    // weapon cycling is restricted; on the frame it goes up he is handed his (upgraded) P2K. Upgrade_Weapon(6)
    // is exactly the original's inlined UpgradedHandguns[Upgrades[0][WUG_Handgun]], truncated to a byte.
    char changeMode = WEAPONCHANGE_WEAPONS;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1) {
        if (switch_channels[0x48] == 0) {
            changeMode = WEAPONCHANGE_UNARMED;
        }
        else if (switch_channels_prev[0x48] == 0) {
            player->animState->switchingToWeaponId = (char)Upgrade_Weapon(6);
        }
    }
    if (Input_Action(playerNum, ACTION_WEAPON_NEXT, 4)) {
        Player_WeaponChange(player, 1, changeMode);
    }
    else if (Input_Action(playerNum, ACTION_WEAPON_PREV, 4)) {
        Player_WeaponChange(player, -1, changeMode);
    }

    Player_Zoom(player);
    Player_AutoAim(player);
}

// AUTOGEN
obj_tag * WatchOrHands_Create(int * param_1);
// AUTOGEN
undefined4 FUN_000b6dd0(obj_tag * param_1, byte param_2);
// AUTOGEN
void MP_EquipPlayer(obj_tag * param_1);

// How far away autoaim will snap to a target, in its two bands. Player_InitWeapon sets them per game mode
// and difficulty; the XBE's initial values (12, 18) are the same as the normal-difficulty ones.
// XBE_GLOBAL(0x00181a40, 0x4)
static float MAX_AUTO_AIM_DIST_NEAR = 12.0f;
// XBE_GLOBAL(0x00181a44, 0x4)
static float MAX_AUTO_AIM_DIST_MIDL = 18.0f;
// The two hint flags the PS2 build calls CamHint and StickyHint; cleared at the start of every level.
#define CamHint (*(uchar *)0x00279145)
#define StickyHint (*(uchar *)0x00279144)

// Ammo counts passed to Player_EquipWeapon. 999 is "as much as the ammo type allows" - Player_EquipWeapon
// caps it; 0 is for gadgets that have no ammo.
#define EQUIP_FULL_AMMO 999
#define EQUIP_HANDGUN_AMMO 0x30

// Unnamed weapons this function hands out (the numbers are weapon_data indices)
#define Weap_Unnamed01 0x01 // given to the player on every single-player level, with no ammo
#define Weap_Unnamed50 0x50 // a gadget (no ammo) given on most levels
#define Weap_Unnamed59 0x59 // a gadget with separate definitions for 50Hz (0x59) and 60Hz (0x5b) video
#define Weap_Unnamed5B 0x5b

// Sets up the player's weapons at the start of a level: autoaim ranges, the weapon/hand objects, the sight and
// muzzle flash, then the level's starting loadout. Levels that continue a mission (Henderson B-D, Castle
// Courtyard, ...) first try Player_RamLoad to carry over what the player had at the end of the previous part,
// and only hand out the level's own loadout if that did not give the player their handgun (the test for a
// RamLoad that found nothing). Every level the switch does not list (hashes outside 0x7000001-0x700004a, the
// test maps, and the multiplayer arenas' hashes) gets almost everything.
//
// The upgradeable weapons are resolved through Upgrade_Weapon, as the PS2 build does; the Xbox build has
// those calls inlined as reads of the Upgraded* tables (which are ours), and Upgrade_Weapon is exactly that
// lookup for these ids - player 0, no side effects.
// AUTOINJECT
void Player_InitWeapon(BLData *blData, obj_tag *player) {

    if (MPSettings.isMultiplayer == 0) {
        if (GameState.difficultyModifier == 2) {
            MAX_AUTO_AIM_DIST_NEAR = 8.0f;
            MAX_AUTO_AIM_DIST_MIDL = 14.0f;
        } else if (GameState.difficultyModifier == 3) {
            // The hardest difficulty has no autoaim at all
            MAX_AUTO_AIM_DIST_NEAR = 0.0f;
            MAX_AUTO_AIM_DIST_MIDL = 0.0f;
        } else {
            MAX_AUTO_AIM_DIST_NEAR = 12.0f;
            MAX_AUTO_AIM_DIST_MIDL = 18.0f;
        }
    } else {
        MAX_AUTO_AIM_DIST_NEAR = 10.0f;
        MAX_AUTO_AIM_DIST_MIDL = 16.0f;
    }

    if (blData == NULL)
        return;

    blData->aimX = 0;
    blData->aimY = 0;
    blData->crosshairOffsetX = 0.0f;
    blData->crosshairOffsetY = 0.0f;
    blData->autoAimTarget = NULL;
    blData->field_0xf2 = 0;

    // The original pushes blData->playerNum as a second argument, but WatchOrHands_Create never reads it
    blData->weaponObject = WatchOrHands_Create((int *)player);

    blData->swayPhase = 0.0f;
    blData->recoilX = 0.0f;
    blData->recoilY = 0.0f;
    blData->idleSwayPhase = 0.0f;
    blData->zoomSound = NULL;
    blData->refireTimer = 0.0f;
    blData->burstShotsLeft = 0;
    blData->field_0x8e6 = 0;
    blData->twinGunHand = 0;

    FUN_000b6dd0(player, blData->playerNum); // creates blData->beamObjs[]
    Player_CreateSight(player, blData->playerNum);
    Player_CreateMuzzleFlash(player, blData->playerNum);

    uint laser = Upgrade_Weapon(0x4e);
    uint handgun = Upgrade_Weapon(0x06);
    uint sniper = Upgrade_Weapon(0x1e);
    uint taser = Upgrade_Weapon(0x4a);
    uint pda = Upgrade_Weapon(Weap_Decryptor);
    uint dartGun = Upgrade_Weapon(0x43);

    CamHint = 0;
    StickyHint = 0;

    short gadget = (VIDEO_FRAME_RATE != 50) ? Weap_Unnamed5B : Weap_Unnamed59;

    player->animState->currentWeaponId = Weap_MaybeHandsOnlyOrLadder;
    player->animState->prevHeldWeaponId = Weap_MaybeHandsOnlyOrLadder;
    blData->aimAutoLevelState = 3;

    if (MPSettings.isMultiplayer != 0) {
        MP_EquipPlayer(player);
        Player_CheckWeaponsLoaded(blData);
        return;
    }

    // Each "continuing" level tries the RamLoad first and falls through to the loadout of the level that
    // starts that mission if it did not restore the handgun (or, for Tower2, the weapon it starts with).
    switch (GameState.CurrentLevelHashcode) {

        case HT_Level_HendersonB:
        case HT_Level_HendersonC:
        case HT_Level_HendersonD:
            Player_RamLoad(blData, 0);
            player->animState->switchingToWeaponId = (char)blData->ramLoadedWeaponId;
            if (blData->weaponStats[handgun].enabled)
                break;
            // fall through
        case HT_Level_HendersonA:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, (short)handgun, EQUIP_HANDGUN_AMMO);
            Player_EquipWeapon(blData, (short)taser, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, gadget, EQUIP_FULL_AMMO);
            player->animState->switchingToWeaponId = (char)handgun;
            break;

        case HT_Level_CastleCourtyard:
            Player_RamLoad(blData, 0);
            player->animState->switchingToWeaponId = (char)blData->ramLoadedWeaponId;
            if (blData->weaponStats[handgun].enabled)
                break;
            // fall through
        case HT_Level_CastleExterior:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, (short)handgun, EQUIP_HANDGUN_AMMO);
            Player_EquipWeapon(blData, (short)taser, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Unnamed50, 0);
            Player_EquipWeapon(blData, Weap_Camera, 0);
            player->animState->switchingToWeaponId = (char)handgun;
            break;

        case HT_Level_CastleIndoors2:
            Player_RamLoad(blData, 0);
            player->animState->switchingToWeaponId = (char)blData->ramLoadedWeaponId;
            if (blData->weaponStats[handgun].enabled)
                break;
            // fall through
        case HT_Level_CastleIndoors1:
            // Castle Indoors 1 RamLoads unconditionally (so a fall-through from Indoors 2 loads twice) - which
            // keeps what the RamLoad restores besides the weapons - then re-arms the player and takes the armour away
            Player_RamLoad(blData, 0);
            Player_InitAmmoWeapons(player);
            blData->armor = 0.0f;
            Player_EquipWeapon(blData, (short)handgun, EQUIP_HANDGUN_AMMO);
            Player_EquipWeapon(blData, (short)taser, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Unnamed50, 0);
            Player_EquipWeapon(blData, Weap_Camera, 0);
            player->animState->switchingToWeaponId = Weap_Unnamed01;
            break;

        case HT_Level_TowerB:
        case HT_Level_TowerC:
            Player_RamLoad(blData, 0);
            player->animState->switchingToWeaponId = (char)blData->ramLoadedWeaponId;
            if (blData->weaponStats[handgun].enabled)
                break;
            // fall through
        case HT_Level_TowerA:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, (short)handgun, EQUIP_HANDGUN_AMMO);
            Player_EquipWeapon(blData, (short)taser, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, (short)pda, 0);
            Player_EquipWeapon(blData, Weap_QWorm, 0);
            Player_EquipWeapon(blData, Weap_Unnamed50, 0);
            Player_EquipWeapon(blData, (short)dartGun, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, gadget, EQUIP_FULL_AMMO);
            player->animState->switchingToWeaponId = (char)dartGun;
            break;

        case HT_Level_PowerStationA2:
            Player_RamLoad(blData, 0);
            player->animState->switchingToWeaponId = (char)blData->ramLoadedWeaponId;
            if (blData->weaponStats[handgun].enabled)
                break;
            // fall through
        case HT_Level_PowerStationA1:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, (short)handgun, EQUIP_HANDGUN_AMMO);
            Player_EquipWeapon(blData, (short)sniper, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, (short)taser, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Unnamed50, 0);
            Player_EquipWeapon(blData, Weap_Camera, 0);
            Player_EquipWeapon(blData, Weap_QWorm, 0);
            player->animState->switchingToWeaponId = (char)sniper;
            break;

        case HT_Level_Tower2B:
        case HT_Level_Tower2C:
        case HT_Level_Tower2Elevator:
            Player_RamLoad(blData, 0);
            player->animState->switchingToWeaponId = (char)blData->ramLoadedWeaponId;
            // Tower2 starts without the handgun, so its test is for the weapon it does start with
            if (blData->weaponStats[0x10].enabled)
                break;
            // fall through
        case HT_Level_Tower2A:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, 0x10, 7);
            Player_EquipWeapon(blData, Weap_Unnamed50, 0);
            player->animState->switchingToWeaponId = 0x10;
            break;

        case HT_Level_EvilSilo:
        case HT_Level_EvilBaseC:
        case 0x7000017:
            Player_RamLoad(blData, 0);
            player->animState->switchingToWeaponId = (char)blData->ramLoadedWeaponId;
            if (blData->weaponStats[handgun].enabled)
                break;
            // fall through
        case HT_Level_EvilBase:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, (short)handgun, EQUIP_HANDGUN_AMMO);
            Player_EquipWeapon(blData, 0x11, 12);
            Player_EquipWeapon(blData, 0x34, 5);
            Player_EquipWeapon(blData, Weap_RemoteMine, 5);
            Player_EquipWeapon(blData, (short)taser, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Unnamed50, 0);
            Player_EquipWeapon(blData, Weap_Camera, 0);
            Player_EquipWeapon(blData, (short)pda, 0);
            player->animState->switchingToWeaponId = 0x11;
            break;

        // The space station levels (0x7000018-0x700001a have no HT_Level_ names yet): no RamLoad, only weapon 0x33,
        // and the armour set to 50
        case 0x7000018:
        case 0x7000019:
        case 0x700001a:
        case HT_Level_SpaceStationD:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, Weap_LaserBeamFromSamurai, EQUIP_FULL_AMMO);
            player->animState->switchingToWeaponId = Weap_LaserBeamFromSamurai;
            blData->armor = 50.0f;
            break;

        // Every other level: a debug-style arsenal of nearly everything, in the original's order
        default:
            Player_InitAmmoWeapons(player);
            Player_EquipWeapon(blData, 0x06, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x12, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x18, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x46, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_RemoteMine, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x36, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x35, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x34, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x3a, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Ronin, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x2c, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x2e, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_SubTorpedo, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x4a, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Unnamed50, 0);
            Player_EquipWeapon(blData, 0x1e, EQUIP_FULL_AMMO);
            Player_EquipAmmo(blData, 0x1f, 10);
            Player_EquipWeapon(blData, 0x24, EQUIP_FULL_AMMO);
            Player_EquipAmmo(blData, 0x25, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x0e, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x10, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Camera, 0);
            Player_EquipWeapon(blData, 0x1c, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x15, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x0a, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x0d, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Decryptor, 0);
            Player_EquipWeapon(blData, Weap_QWorm, 0);
            Player_EquipWeapon(blData, 0x41, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_Unnamed59, EQUIP_FULL_AMMO); // 0x59 whatever the video rate
            Player_EquipWeapon(blData, Weap_Satchel, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x1a, EQUIP_FULL_AMMO);
            Player_EquipAmmo(blData, 0x1b, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_LaserBeamFromSamurai, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x11, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x02, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x2a, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x42, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x43, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, Weap_OddjobHat, EQUIP_FULL_AMMO);
            Player_EquipWeapon(blData, 0x16, EQUIP_FULL_AMMO);
            player->animState->switchingToWeaponId = (char)handgun;
            break;
    }

    // Every single-player level gets the laser (whatever the upgrade level makes it) and weapon 1
    Player_EquipWeapon(blData, (short)laser, EQUIP_FULL_AMMO);
    Player_EquipWeapon(blData, Weap_Unnamed01, 0);
    Player_CheckWeaponsLoaded(blData);

    // If the weapon chosen above did not survive (its model is not loaded, or the RamLoad picked something the
    // player no longer has), fall back to weapon 1. The original reads weaponBaseNum as a 16-bit value; its
    // high byte (field2_0x3) is zero in every entry of the table, so the char reads the same.
    AnimState *anim = player->animState;
    if (!blData->weaponStats[weapon_data[anim->switchingToWeaponId].weaponBaseNum].enabled)
        anim->switchingToWeaponId = Weap_Unnamed01;

    Player_RamSave(blData);
    Player_CheckWeaponsLoaded(blData);
}

// ---------------------------------------------------------------------------------------------------------------
#define Plr_AimSpeed_X             FLOAT_AT(0x00181a48)
#define Plr_AimSpeed_Y             FLOAT_AT(0x00181a4c)
#define Plr_AimTurnSpeed_X         FLOAT_AT(0x00181a50)
#define Plr_AimTurnSpeed_Y         FLOAT_AT(0x00181a54)
#define Plr_ScopeSpeed_X           FLOAT_AT(0x00181a58)
#define Plr_ScopeSpeed_X_Mul       FLOAT_AT(0x00181a5c)
#define Plr_ScopeSpeed_X_Steps     FLOAT_AT(0x00181a60)
#define Plr_ScopeSpeed_Y           FLOAT_AT(0x00181a64)
#define Plr_ScopeSpeed_Y_Mul       FLOAT_AT(0x00181a68)
#define Plr_ScopeSpeed_Y_Steps     FLOAT_AT(0x00181a6c)
#define Plr_NoAimTurnSpeed_Y       FLOAT_AT(0x00181a7c)
#define Plr_NoAimTurnSpeed_Y_Mul   FLOAT_AT(0x00181a80)
#define Plr_NoAimTurnSpeed_Y_Steps FLOAT_AT(0x00181a84)

// Health given back on continuing a mission, per difficulty (read by Player_RamLoad)
#define ContinueHealthBoostEasy   FLOAT_AT(0x0017e680)
#define ContinueHealthBoostMedium FLOAT_AT(0x0017e684)
#define ContinueHealthBoostHard   FLOAT_AT(0x0017e688)

// Autoaim (read by Check_AutoAim, and Autoaim_Range by Player_AutoAim). Autoaim_HardMul is in .bss, so it is
// 0 until the first level loads anyway.
#define Autoaim_Angle_H   FLOAT_AT(0x00181afc)
#define Autoaim_Angle_V   FLOAT_AT(0x00181b00)
#define Autoaim_Range     FLOAT_AT(0x00181b04)
#define Autoaim_LockOnMul FLOAT_AT(0x00181b08)
#define Autoaim_EasyMul   FLOAT_AT(0x00181b0c)
#define Autoaim_NormalMul FLOAT_AT(0x00181b10)
#define Autoaim_HardMul   FLOAT_AT(0x0027915c)

// Damage the player takes, per difficulty and per body part (read by Player_HandlePain; the body-part ones also by
// BOT_handlePain; all editable in the P_TWEAKS debug menu)
#define Plr_DMod_Multi     FLOAT_AT(0x0017e8cc)
#define Plr_DMod_Head      FLOAT_AT(0x0017e8d0)
#define Plr_DMod_LowerLimb FLOAT_AT(0x0017e8d4)
#define Plr_DMod_UpperLimb FLOAT_AT(0x0017e8d8)
#define Plr_DMod_Easy      FLOAT_AT(0x0017e8dc)
#define Plr_DMod_Normal    FLOAT_AT(0x0017e8e0)
#define Plr_DMod_Hard      FLOAT_AT(0x0017e8e4)


// Level 0x700004C has no name in assets.h; it is scored like the late levels (the Ravine is 0x700004B).
#define HT_Level_Unnamed4C 0x700004C

// The drone settings most single-player levels start from (Henderson's, and the Evil Base's); the level cases
// below override the few that differ. The original writes each level's full set, the compiler merging the common
// stores - only the final values matter, since nothing reads them in between.
static void SetStandardDroneTuning(void) {
    DroneDamage_Easy = 1.5f;
    DroneDamage_Normal = 1.0f;
    DroneDamage_Hard = 0.8f;
    DroneDamage_Head = 10.0f;
    DroneDamage_Legs = 0.75f;
    DroneDamage_Arms = 1.0f;
    DroneDamage_Torso = 1.0f;
    DroneArmour_Helmet = 1.0f;
    DroneArmour_Combat = 0.25f;
    DroneArmour_Jacket = 0.5f;
    DroneArmour_Vest = 0.75f;
    DroneFiring_BurstDelay_Min = 15.0f;
    DroneFiring_BurstDelay_Normal = 45.0f;
    DroneFiring_BurstDelay_Max = 60.0f;
    DroneFiring_BurstDelay_MinDist = 4.0f;
    DroneFiring_BurstDelay_MaxDist = 30.0f;
    DroneFiring_Accuracy_Easy = 0.5f;
    DroneFiring_Accuracy_Normal = 0.7f;
    DroneFiring_Accuracy_Hard = 1.0f;
    DroneFiring_NewSighting_TimeToHit = 2.0f;
    DroneFiring_TargetFirstMoved_TimeToHit = 2.0f;
    DroneFiring_TargetFirstMoved_Accuracy = 0.25f;
    DroneFiring_TargetMoving_Accuracy = 0.75f;
    DroneFiring_TargetFirstStopped_TimeToHit = 2.0f;
    DroneFiring_TargetFirstStopped_Accuracy = 1.0f;
    DroneFiring_TooClose_Distance = 3.0f;
    DroneFiring_TooClose_Accuracy = 2.0f;
    DroneFiring_TooClose_Damage = 2.0f;
    DroneFiring_PlayerBackShot_Damage = 2.0f;
    DroneCaptain_Mod_BulletDamage = 2.0f;
    DroneCaptain_Mod_BulletAccuracy = 2.0f;
    DroneCaptain_Mod_Health = 2.0f;
}

static void SetPlayerDamage(float easy, float normal, float hard) {
    Plr_DMod_Easy = easy;
    Plr_DMod_Normal = normal;
    Plr_DMod_Hard = hard;
}

// Sets the game's difficulty tuning for the level being loaded (called from ResetMap_Load). Despite the name
// nothing is read from a file: the values are all constants. The player's aiming, autoaim and continue-health
// values are the same everywhere; the drones' damage, armour and accuracy and the player's damage taken are set
// per level. Levels the switch does not list (the cut-scene levels, the test maps, multiplayer arenas, driving
// levels) keep whatever the drone and player-damage values were - the XBE's initial values, the previous level's,
// or what the P_TWEAKS debug menus set.
// AUTOINJECT
void __stdcall ReadTuningVars(void) {

    Plr_AimSpeed_X = 0.1275f;
    Plr_AimSpeed_Y = 0.136f;
    Plr_AimTurnSpeed_X = 0.05f;
    Plr_AimTurnSpeed_Y = 0.06f;
    Plr_ScopeSpeed_X = 0.015f;
    Plr_ScopeSpeed_X_Mul = 3.0f;
    Plr_ScopeSpeed_X_Steps = 120.0f;
    Plr_ScopeSpeed_Y = 0.011f;
    Plr_ScopeSpeed_Y_Mul = 3.0f;
    Plr_ScopeSpeed_Y_Steps = 120.0f;
    Plr_NoAimTurnSpeed_X = 0.04f;
    Plr_NoAimTurnSpeed_X_Mul = 2.0f;
    Plr_NoAimTurnSpeed_X_Steps = 120.0f;
    Plr_NoAimTurnSpeed_Y = 0.01f;
    Plr_NoAimTurnSpeed_Y_Mul = 2.0f;
    Plr_NoAimTurnSpeed_Y_Steps = 120.0f;

    ContinueHealthBoostEasy = 50.0f;
    ContinueHealthBoostMedium = 50.0f;
    ContinueHealthBoostHard = 50.0f;

    Autoaim_Angle_H = 0.12f;
    Autoaim_Angle_V = 0.22f;
    Autoaim_Range = 25.0f;
    Autoaim_LockOnMul = 1.4f;
    Autoaim_EasyMul = 1.9f;
    Autoaim_NormalMul = 1.0f;
    Autoaim_HardMul = 0.0f;   // no autoaim at all on the hardest difficulty

    switch ((uint)GameState.CurrentLevelHashcode) {
    case HT_Level_HendersonA:
    case HT_Level_HendersonB:
    case HT_Level_HendersonC:
    case HT_Level_HendersonD:
        SetStandardDroneTuning();
        SetPlayerDamage(0.5f, 0.7f, 1.2f);
        break;

    case HT_Level_CastleExterior:
    case HT_Level_CastleCourtyard:
    case HT_Level_CastleIndoors1:
    case HT_Level_CastleIndoors2:
        SetStandardDroneTuning();
        DroneFiring_Accuracy_Easy = 0.8f;
        DroneFiring_Accuracy_Normal = 0.9f;
        DroneFiring_Accuracy_Hard = 1.3f;
        SetPlayerDamage(0.6f, 0.7f, 1.0f);
        break;

    case HT_Level_TowerA:
    case HT_Level_TowerB:
    case HT_Level_TowerC:
        SetStandardDroneTuning();
        DroneFiring_Accuracy_Easy = 0.8f;
        DroneFiring_Accuracy_Normal = 1.0f;
        DroneFiring_Accuracy_Hard = 1.0f;
        SetPlayerDamage(0.6f, 0.8f, 1.0f);
        break;

    case HT_Level_PowerStationA1:
    case HT_Level_PowerStationA2:
        SetStandardDroneTuning();
        DroneFiring_Accuracy_Easy = 0.6f;
        DroneFiring_Accuracy_Normal = 0.8f;
        DroneFiring_Accuracy_Hard = 1.2f;
        // Drones are quick to hit a target that has just stopped, but only just (0.1 against 1.0 elsewhere)
        DroneFiring_TargetFirstStopped_TimeToHit = 1.0f;
        DroneFiring_TargetFirstStopped_Accuracy = 0.1f;
        DroneCaptain_Mod_BulletDamage = 1.5f;
        DroneCaptain_Mod_BulletAccuracy = 1.5f;
        SetPlayerDamage(0.4f, 0.7f, 1.2f);
        break;

    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_Tower2Elevator:
        SetStandardDroneTuning();
        DroneDamage_Normal = 1.2f;
        DroneDamage_Hard = 1.0f;
        DroneFiring_Accuracy_Easy = 0.7f;
        DroneFiring_Accuracy_Normal = 0.8f;
        DroneFiring_Accuracy_Hard = 1.0f;
        SetPlayerDamage(0.5f, 0.6f, 1.0f);
        break;

    case HT_Level_EvilBase:
    case HT_Level_EvilSilo:
    case HT_Level_EvilBaseC:
        SetStandardDroneTuning();
        SetPlayerDamage(0.4f, 0.6f, 1.0f);
        break;

    case HT_Level_SpaceStationD:
        SetStandardDroneTuning();
        DroneDamage_Easy = 1.0f;
        DroneDamage_Normal = 1.0f;
        DroneDamage_Hard = 1.0f;
        DroneFiring_Accuracy_Easy = 0.6f;
        DroneFiring_Accuracy_Normal = 0.8f;
        DroneFiring_Accuracy_Hard = 1.2f;
        SetPlayerDamage(0.6f, 0.7f, 1.0f);
        break;

    case HT_Level_SpaceStation:
    case HT_Level_Facility:
    case HT_Level_Atlantis:
    case HT_Level_SkyRail:
    case HT_Level_SubPen:
    case HT_Level_StealthShip:
    case HT_Level_FortKnox:
    case HT_Level_MissileSilo:
    case HT_Level_SnowBlind:
    case HT_Level_Ravine:
    case HT_Level_Unnamed4C:
        SetStandardDroneTuning();
        DroneDamage_Easy = 2.0f;
        DroneDamage_Normal = 1.5f;
        DroneDamage_Hard = 0.5f;
        DroneArmour_Helmet = 0.5f;
        DroneFiring_Accuracy_Easy = 0.8f;
        DroneFiring_Accuracy_Normal = 1.0f;
        DroneFiring_Accuracy_Hard = 1.5f;
        DroneFiring_TooClose_Distance = 4.0f;
        // These levels set the body-part damage the player takes instead of the per-difficulty values
        Plr_DMod_Multi = 4.0f;
        Plr_DMod_Head = 4.0f;
        Plr_DMod_LowerLimb = 0.8f;
        Plr_DMod_UpperLimb = 0.8f;
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Player_PositionCamera (0x000a8a60): the player's camera, once a frame. Player_Init hands it to the camera system
// as the viewer's updater (Camera_SetUpdator), so it runs from Camera_UpdateAll, not from Player_Update.
//
// First the head height - crouching lowers the view, anything else brings it back, and the remote-control
// substates leave it where it was. Then whatever the camera mode says: first person, the remote devices' views,
// the multiplayer end-of-match camera, or one of the debug cameras. First person is the only one that passes the
// weapon's zoom on; every other view is drawn unzoomed.
// ---------------------------------------------------------------------------------------------------------------

#define surfaced U8_AT(0x00276760)       // non-zero while swimming at the surface
#define debug_cam_posy FLOAT_AT(0x001fec50) // height of camera mode 8's overhead view above the player

// The bone AnimGetBoneWorldTrans gives the head for (also Player_Update's breath effect).
#define BONE_HEAD 0x80000005


// The original's, called by address: ends in a bare RET, so caller-cleans whatever Ghidra's __stdcall says.
static float moveTowardsValue(float current, float target, float speed) {
    return reinterpret_cast<float (__cdecl *)(float, float, float)>(0x00024000)(current, target, speed);
}

// The debug cameras' placements, called by address: unnamed, and only these camera modes use them.
static void CameraMode2_PlaceA(_VECTOR *position, _VECTOR *rotation, viewer_tag *viewer, float a, float b) {
    reinterpret_cast<void (__cdecl *)(_VECTOR *, _VECTOR *, viewer_tag *, float, float)>(0x00024710)
        (position, rotation, viewer, a, b);
}
static void CameraMode2_PlaceB(obj_tag *obj, viewer_tag *viewer, float a, float b) {
    reinterpret_cast<void (__cdecl *)(obj_tag *, viewer_tag *, float, float)>(0x000246a0)(obj, viewer, a, b);
}
static void CameraMode2_PlaceC(_VECTOR *position, viewer_tag *viewer, float a) {
    reinterpret_cast<void (__cdecl *)(_VECTOR *, viewer_tag *, float)>(0x000245a0)(position, viewer, a);
}
static void CameraMode2_PlaceD(_VECTOR *position, viewer_tag *viewer, float a) {
    reinterpret_cast<void (__cdecl *)(_VECTOR *, viewer_tag *, float)>(0x00024610)(position, viewer, a);
}
static void CameraMode3_Place(obj_tag *obj, viewer_tag *viewer) {
    reinterpret_cast<void (__cdecl *)(obj_tag *, viewer_tag *)>(0x00024b10)(obj, viewer);
}
static void CameraMode4_Place(obj_tag *obj, viewer_tag *viewer) {
    reinterpret_cast<void (__cdecl *)(obj_tag *, viewer_tag *)>(0x00024d40)(obj, viewer);
}
static void CameraMode5_Place(obj_tag *obj, viewer_tag *viewer) {
    reinterpret_cast<void (__cdecl *)(obj_tag *, viewer_tag *)>(0x00024d70)(obj, viewer);
}
static void Camera_Follow(obj_tag *obj, viewer_tag *viewer) { // camera modes 6 and 10
    reinterpret_cast<void (__cdecl *)(obj_tag *, viewer_tag *)>(0x00024880)(obj, viewer);
}
static void Camera_LookAtFrom(viewer_tag *viewer, _VECTOR *target) { // the end-of-match camera
    reinterpret_cast<void (__cdecl *)(viewer_tag *, _VECTOR *)>(0x00025450)(viewer, target);
}

// AUTOINJECT
void Player_PositionCamera(obj_tag *player) {

    BLData *blData = (BLData*)player->extraObjectData;
    char playerNum = blData->playerNum;
    viewer_tag *viewer = glb_viewer[playerNum];

    if (!GS_IsPaused(-1)) {
        switch ((short)player->subState) {
            case MovementType_Crouch: {
                double offset = (double)FRAME_RATE_MUL * 0.0225f + blData->headCrouchOffset;
                blData->headCrouchOffset = (float)offset;
                if (offset > 0.45f)
                    blData->headCrouchOffset = 0.45f;
                break;
            }
            case MovementType_RemoteControl:
            case MovementType_RCVehicle:
            case MovementType_Emplacement:
            case MovementType_Ronin:
                break;
            default: {
                double offset = blData->headCrouchOffset - (double)FRAME_RATE_MUL * 0.0225f;
                blData->headCrouchOffset = (float)offset;
                if (offset < 0.0)
                    blData->headCrouchOffset = 0.0f;
                break;
            }
        }
    }

    obj_tag *device;

    switch ((uchar)blData->camMode) {

        case CamMode_Default:
        case CamMode_GunImp: {
            View_SetDrawInOtherViewsOnly(player, playerNum);
            View_SetDrawInThisViewOnly(blData->weaponObject, playerNum);

            weapon_definition_tag *weapon = &weapon_data[player->animState->currentWeaponId];
            bool realScope = (player->animState->animFlags & 1) && (weapon->weaponFlags & WPNFLAG_REAL_SCOPE);
            if (realScope)
                View_SetDrawInNoViews(blData->weaponObject);
            if (weapon->weaponModelHashcode == 0)
                View_SetDrawInNoViews(blData->weaponObject);

            blData->headSwimOffset = moveTowardsValue(blData->headSwimOffset, surfaced ? 0.7f : 0.0f, 0.1f);

            viewer->zoom = blData->zoom;
            // Not in the original: the player view's field of view comes from settings.ini, but a real scope
            // keeps showing what it always did, so its zoom makes up the difference (engine/viewFov.h).
            if (realScope)
                viewer->zoom *= ViewFov_ScopeZoomCorrection();

            if (player->curState == 2 || player->curState == 3)
                blData->headCrouchOffset = 0.0f;

            Camera_SetToPlayer(player, viewer);
            return;
        }

        case CamMode_PostMPGameThirdPerson:
            viewer->zoom = 1.0f;
            blData->thirdPersonSubject = player;
            AnimGetBoneWorldTrans(player, BONE_HEAD, 0, &blData->thirdPersonTarget, NULL);
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_LookAtFrom(viewer, &blData->thirdPersonTarget);
            viewer->someCel = NULL;
            return;

        case 2:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            CameraMode2_PlaceA(&player->position, &player->rotation, viewer, 1.0f, 10.0f);
            CameraMode2_PlaceB(player, viewer, 1.0f, 0.0f);
            CameraMode2_PlaceC(&player->position, viewer, 1.0f);
            CameraMode2_PlaceD(&player->position, viewer, 1.0f);
            viewer->zoom = 1.0f;
            viewer->someCel = NULL;
            return;

        case 3:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            CameraMode3_Place(player, viewer);
            viewer->zoom = 1.0f;
            viewer->someCel = NULL;
            return;

        case 4:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            CameraMode4_Place(player, viewer);
            viewer->zoom = 1.0f;
            return;

        case 5:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            CameraMode5_Place(player, viewer);
            viewer->zoom = 1.0f;
            viewer->someCel = NULL;
            return;

        case 6:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_Follow(player, viewer);
            viewer->zoom = 1.0f;
            viewer->someCel = NULL;
            return;

        case 8: // looking straight down on the player from debug_cam_posy above
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            viewer->pos.x = player->position.x;
            viewer->pos.z = player->position.z;
            viewer->someVec.y = 0.0f;
            viewer->pos.y = (float)((double)debug_cam_posy + player->position.y + 2.0f);
            viewer->someVec.x = 1.5707964f;
            viewer->someVec.z = 0.0f;
            viewer->zoom = 1.0f;
            Camera_Set(viewer, &viewer->pos, &viewer->someVec);
            return;

        case CamMode_RemoteDeviceBehind: {
            device = blData->remoteControlDevice;
            if (device == NULL)
                return;

            _VECTOR offset = { 0.0f, 0.0f, -0.2f };
            View_SetDrawInAllViews(player);
            _VECTOR rotation = device->rotation;
            _MATRIX rotationMatrix;
            RotMatrixZYX(&rotation, &rotationMatrix);
            ApplyMatrixLVI(&rotationMatrix, &offset);
            offset.x += device->position.x;
            offset.y += device->position.y;
            offset.z += device->position.z;
            Vec_Copy(&offset, &viewer->pos);
            viewer->someVec = device->rotation;
            viewer->zoom = 1.0f;
            Camera_Set(viewer, &viewer->pos, &viewer->someVec);
            return;
        }

        case CamMode_FollowObject:
            View_SetDrawInNoViews(player);
            View_SetDrawInAllViews(glb_blokes[playerNum]->cameraFollowObject);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_Follow(glb_blokes[playerNum]->cameraFollowObject, viewer);
            viewer->zoom = 1.0f;
            return;

        case CamMode_CreepWall:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            camera_tracking(player, viewer);
            viewer->zoom = 1.0f;
            viewer->someCel = NULL;
            return;

        case CamMode_Redeemer: // the missile's own view
            device = blData->remoteControlDevice;
            if (device == NULL)
                return;
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            View_SetDrawInOtherViewsOnly(device, playerNum);
            Mat_Copy(&device->transformMatrix, &viewer->viewMatrix);
            return;

        case CamMode_RCCar: { // from on top of the vehicle, raised by its height (a fixed 0.1 for the helicopter)
            device = blData->remoteControlDevice;
            if (device == NULL)
                return;
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            View_SetDrawInOtherViewsOnly(device, playerNum);

            _VECTOR up;
            Mat_GetUp(&up, &device->transformMatrix);
            Mat_Copy(&device->transformMatrix, &viewer->viewMatrix);

            celglist_tag *model = device->objGraphics;
            float height;
            if (model == hashtable_hashcode_to_celglist(GFX_LittleNellie_Body)) {
                height = 0.1f;
            }
            else {
                height = model->extentMin.y * 3.0f;
                if (height < 0.0f)
                    height = -height;
            }
            auxVec_AddMulR32(Mat_Position(device->transformMatrix), &up, height, Mat_Position(viewer->viewMatrix));
            return;
        }

        case CamMode_Ronin: { // riding on the gun, a little above and behind its muzzle
            device = blData->remoteControlDevice;
            if (device == NULL)
                return;
            GUNTURRET *turret = (GUNTURRET*)device->extraObjectData;
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            View_SetDrawInOtherViewsOnly(device, playerNum);

            _VECTOR offset = { 0.0f, 0.15f, -0.4f };
            ApplyMatrixLVI(&turret->muzzleFlash->transformMatrix, &offset);
            _VECTOR position;
            Vec_Add2(&offset, Mat_Position(turret->muzzleFlash->transformMatrix), &position);
            Mat_Copy(&turret->muzzleFlash->transformMatrix, &viewer->viewMatrix);
            Matrix_SetTrans(&position, &viewer->viewMatrix);
            return;
        }

    }

}

// ---------------------------------------------------------------------------------------------------------------
// Substates, health and death, and walking (the substate handlers Player_Update calls)
// ---------------------------------------------------------------------------------------------------------------

// The originals, called by address
#define MP_PlayerKilled ((void (__cdecl *)(obj_tag *player))0x000a1fc0)
#define MP_ReSpawn ((bool (__cdecl *)(obj_tag *player, short playerNum))0x0009f0b0)

#define CheatInfo (*((CheatInfo_t*)0x001f65dc))
#define debug_scanmode U8_AT(0x001fec49)   // the player starts every level flying (MovementType_ScanMode)
// XBE_GLOBAL(0x001f6648, 0xc) is GRAVITY_VECTOR: in zero G its y is 0

// BLData::jumpState
#define JUMP_NONE    0
#define JUMP_RISING  1
#define JUMP_FALLING 2

// Floor materials (HITDATA_tag::materialType) that land with their own sound
#define MATERIAL_0D 0x0d
#define MATERIAL_0E 0x0e


// The level and the original's text for a death in multiplayer
#define TXT_MP_DIED   ((Action_TranslatedText)0x02000037)  // with the player's name
#define TXT_MP_KILLED ((Action_TranslatedText)0x02000038)  // with the killer's name

void _Player_HandleJump(BLData *blData, obj_tag *player, _VECTOR *gravity);

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// What the original's FCOMP and FCHS give: the sign flipped unless x >= 0, so -0 stays -0 (MSVC's fabs clears it)
static float Magnitude(float x) {
    return x >= 0.0f ? x : std::bit_cast<float>(std::bit_cast<uint32_t>(x) ^ 0x80000000u);
}

// AUTOINJECT
void Player_ClearInertia(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    Vec_Zero(&blData->lastMovement);
    Vec_Zero(&blData->lastRotationDelta);
    Vec_Zero(&blData->movement);
    Vec_Zero(&blData->rotationDelta);
}

// AUTOINJECT
void Player_ClearAimInertia(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    blData->aimTurnX = 0.0f;
    blData->aimTurnY = 0.0f;
}

// AUTOINJECT
void Player_SetFlashBang(BLData *blData, float time, uchar blur) {
    if (blData == NULL)
        return;
    blData->flashTime = time;
    blData->flashTimeLeft = time;
    blData->flashBlur = blur;
}

// Nothing changes while the stats may not be updated (PlrStat_OkToUpdate)
// AUTOINJECT
void Player_SetHealth(BLData *blData, float health) {
    if (!PlrStat_OkToUpdate())
        return;
    if (health < 0.0f)
        health = 0.0f;
    blData->health = health;
    PlrStat_LogHealth(health, blData->playerNum);
}

// The eyes: above the position by the eye height less the crouch, and, given a matrix, the view's. Flying and in zero
// G the offset turns with the player, and the matrix is left alone.
// AUTOINJECT
void Player_GetHeadPos(_VECTOR *headPos, obj_tag *player, _MATRIX *mtx) {
    BLData *blData = (BLData *)player->extraObjectData;
    short subState = (short)player->subState;
    if (subState == MovementType_ScanMode || subState == MovementType_ZeroG || subState == MovementType_ZeroG_Anim) {
        _VECTOR offset;
        offset.y = (float)((double)blData->headSwimOffset - blData->headCrouchOffset);
        offset.z = 0.0f;
        offset.x = 0.0f;
        ApplyMatrixLV(&player->transformMatrix, &offset, headPos);
        Vec_Add2(headPos, &player->position, headPos);
        return;
    }

    headPos->x = player->position.x;
    headPos->y = (float)((double)blData->headSwimOffset + player->position.y - blData->headCrouchOffset);
    headPos->z = player->position.z;
    if (mtx != NULL)
        Mat_Copy(&glb_viewer[blData->playerNum]->viewMatrix, mtx);
}

// The player's anim set lists by weapon_definition_tag::animSet (1 to 6), for any other, and in single player: each
// the standing list, then the crouching one
#define PLAYER_ANIMSET_LIST_SIZE 9
#define PlayerAnimSetSinglePlayer ((HASHCODE *)0x0017e68c)
#define PlayerAnimSetDefault ((HASHCODE *)0x0017e71c)
static HASHCODE *const PlayerAnimSetByClass[6] = {
    (HASHCODE *)0x0017e764, (HASHCODE *)0x0017e884, (HASHCODE *)0x0017e6d4,
    (HASHCODE *)0x0017e7f4, (HASHCODE *)0x0017e7ac, (HASHCODE *)0x0017e83c,
};

// The player's walk speeds, as AnimSetAppend's distance phase and scale
static constexpr float kPlayerSetPhase = 0.37f;
static constexpr float kPlayerSetScale = 0.5f;
static_assert(std::bit_cast<uint32_t>(kPlayerSetPhase) == 0x3ebd70a4, "0.37");

// The player's standing anim set list for the held weapon
static HASHCODE *PlayerAnimSetList(obj_tag *player) {
    short animSet = weapon_data[player->animState->currentWeaponId].animSet;
    if (MPSettings.isMultiplayer == 0)
        animSet = 999;
    if (animSet == 999)
        return PlayerAnimSetSinglePlayer;
    if (animSet >= 1 && animSet <= 6)
        return PlayerAnimSetByClass[animSet - 1];
    return PlayerAnimSetDefault;
}

// AUTOINJECT
void PlayerAnimSetInitNormal(obj_tag *player) {
    HASHCODE *list = PlayerAnimSetList(player);
    ((BLData *)player->extraObjectData)->animSet = AnimSetAppend(player, list, kPlayerSetPhase, kPlayerSetScale);
}

// AUTOINJECT
void PlayerAnimSetInitCrouch(obj_tag *player) {
    HASHCODE *list = PlayerAnimSetList(player) + PLAYER_ANIMSET_LIST_SIZE;
    ((BLData *)player->extraObjectData)->animSet = AnimSetAppend(player, list, kPlayerSetPhase, kPlayerSetScale);
}

// AUTOINJECT
void PlayerAnimSetInit(obj_tag *player) {
    ((BLData *)player->extraObjectData)->animSet = NULL;
    AnimSetDeleteAll(player);
    if (player->subState == MovementType_Crouch)
        PlayerAnimSetInitCrouch(player);
    else
        PlayerAnimSetInitNormal(player);
}

// The scripts that stand up from a crouch and crouch down, by weapon_definition_tag::animSet (1 to 6), and for any
// other; class 3 in single player
static const HASHCODE PlayerCrouch2StandScripts[7] = {
    (HASHCODE)0x060009da, (HASHCODE)0x060009ae, (HASHCODE)0x060001dd,
    (HASHCODE)0x060001d1, (HASHCODE)0x060001e9, (HASHCODE)0x060009b6, (HASHCODE)0x060001f4,
};
static const HASHCODE PlayerStand2CrouchScripts[7] = {
    (HASHCODE)0x060009dd, (HASHCODE)0x060009b1, (HASHCODE)0x060001dc,
    (HASHCODE)0x060001d0, (HASHCODE)0x060001e8, (HASHCODE)0x060009b9, (HASHCODE)0x060001f3,
};

// Plays the change of stance for the held weapon, at four frames a tick
static void PlayerAnimChangeStance(obj_tag *player, const HASHCODE scripts[7]) {
    short animSet = weapon_data[player->animState->currentWeaponId].animSet;
    if (MPSettings.isMultiplayer == 0)
        animSet = 3;
    HASHCODE hashcode = (animSet >= 1 && animSet <= 6) ? scripts[animSet - 1] : scripts[6];
    sAnimScript_tag *script = AnimScriptAdd(player, hashcode);
    if (script == NULL)
        return;
    script->flags |= ANIMSCRIPT_GAME_FLAG | ANIMSCRIPT_ZERO_ROOT_Z | ANIMSCRIPT_ZERO_ROOT_X;
    script->speed = script->speed >= 0.0f ? 4.0f : -4.0f;
}

// AUTOINJECT
void PlayerAnimCrouch2Stand(obj_tag *player) {
    PlayerAnimChangeStance(player, PlayerCrouch2StandScripts);
}

// AUTOINJECT
void PlayerAnimStand2Crouch(obj_tag *player) {
    PlayerAnimChangeStance(player, PlayerStand2CrouchScripts);
}

// The anim of a dead player: lying in zero G, falling otherwise. Not a function on PS2; the name is ours.
// FUNC_AT(0x000a96a0)
void PlayerAnimSetInitDead(obj_tag *player) {
    ((BLData *)player->extraObjectData)->animSet = NULL;
    AnimSetDeleteAll(player);
    if (GRAVITY_VECTOR.y == 0.0f)
        AnimScriptAdd(player, (HASHCODE)0x060008e0);
    else
        AnimScriptAdd(player, (HASHCODE)0x06000151);
}

// Returns the substate the player was in. Flying and zero G keep the matrix's rotation; everything else stands the
// player up straight.
// AUTOINJECT
int Player_ChangeSubState(obj_tag *player, ushort newState) {
    short oldState = (short)player->subState;
    BLData *blData = (BLData *)player->extraObjectData;
    player->subState = newState;
    blData->animSet = NULL;
    AnimSetDeleteAll(player);
    if (newState == MovementType_ScanMode || newState == MovementType_ZeroG || newState == MovementType_ZeroG_Anim) {
        player->transformFlags |= TRANSFORM_KEEP_ROTATION;
    }
    else {
        player->transformFlags &= ~TRANSFORM_KEEP_ROTATION;
        player->rotation.x = 0.0f;
    }
    player->effectFlags &= ~FLAG_HIDDEN;

    switch (newState) {
        case MovementType_Walk:
            PlayerAnimSetInitNormal(player);
            ((BLData *)player->extraObjectData)->field_0x8e4 = 0xff;
            if (oldState == MovementType_Crouch) {
                PlayerAnimCrouch2Stand(player);
                blData->animWait = (char)Ftol(_FRAME_RATE);
            }
            break;
        case MovementType_Crouch:
            PlayerAnimSetInitCrouch(player);
            ((BLData *)player->extraObjectData)->field_0x8e4 = 0xff;
            if (oldState == MovementType_Walk) {
                PlayerAnimStand2Crouch(player);
                blData->animWait = (char)Ftol(_FRAME_RATE);
            }
            break;
        case MovementType_ZeroG:
        case MovementType_ZeroG_Anim:
            PlayerAnimSetInitNormal(player);
            ((BLData *)player->extraObjectData)->field_0x8e4 = 0xff;
            break;
        case MovementType_Dead:
        case MovementType_DeadInWater:
            PlayerAnimSetInitDead(player);
            break;
    }

    if (newState >= 1 && newState <= 16 && newState != MovementType_Crouch) {
        Sound_StopAllWithId((Action_SFX)0x12);
        Sound_StopAllWithId((Action_SFX)0x3d);
        Sound_StopAllWithId((Action_SFX)0xf0);
    }
    blData->field_0x8ea = 0;
    return oldState;
}

// Whether the player swims: in a cel with water above the feet (0.3 below the position), and not dying. Going in
// changes to the swimming substate and clears the movement. The player's object in ESI, his data in EDI.
int _Player_InWater(obj_tag *player, BLData *blData) {
    cel_tag *cel = player->inCel;
    if (cel == NULL)
        return 0;

    if ((double)player->position.y - 0.3f < cel->shearHeight) {
        player->animState->inWater |= 1;
        if (MPSettings.isMultiplayer == 0 && player->curState != 2) {
            Player_ChangeSubState(player, MovementType_Swim);
            blData->jumpState = JUMP_NONE;
            blData->traverseWait = (char)Ftol((double)FRAME_RATE_DIV * 5.0f);
            blData->aimAutoLevelState = 2;
            blData->movement.y = 0.0f;
            player->rotation.x = 0.0f;
            player->rotation.z = 0.0f;
            Player_ClearInertia(player);
            player->animState->animObj.distancePhase = 0.0f;
        }
    }
    else {
        player->animState->inWater &= ~1;
    }
    return player->subState == MovementType_Swim;
}

// AUTOLTCG
void __declspec(naked) Player_InWater(void) {
    _asm {
        push edi
        push esi
        call _Player_InWater
        add esp, 8
        ret
    }
}

// How well lit the player is: his own light (the average of the object's three) plus, for each of the closest
// lights, an eighth of its colour scaled by distance over brightness; 255 is fully lit.
// AUTOINJECT
void Player_InShadow(BLData *blData, obj_tag *player) {
    int sum = player->ambientR + player->ambientG + player->ambientB;
    int light = Ftol((double)sum * 0.33333334f);

    ushort count;
    light_tag **lights = Lights_CalcClosestLights(&player->centrePoint, (ushort)player->effectFlags, &count);
    for (ushort i = 0; i < count; i++) {
        uchar colour = (lights[i]->clr_r + lights[i]->clr_g + lights[i]->clr_b) >> 3;
        double lit = Vec_Dist3D(&lights[i]->position, &player->centrePoint);
        lit = lit / lights[i]->brightness * (int)colour;
        light += Ftol(lit);
    }

    double visibility = (double)(int)(ushort)light * 0.003921569f;
    blData->visibility = (float)visibility;
    if (player->subState == MovementType_Crouch)
        blData->visibility = (float)(visibility * 0.5f);
}

// The damage the player takes, scaled by the difficulty (single player) or the multiplayer options and where he was
// hit, taken from the armour first except for pain types 1 to 7, then from health; a flash on the HUD and the pad's
// rumble. Pain types 5 to 7 come from the level, not a player; 7 is drowning, which the immortality cheat stops
// rumbling. hitLocation is the bone hit, or -1. The original takes the player's data in ESI, the hit location in DI
// and the pain type in BX.
void _Player_HandlePain(obj_tag *player, float damage, BLData *blData, short hitLocation, ushort painType) {
    if (switch_channels[0x5f] != 0)
        return;
    if (blData->health <= 0.0f)
        return;
    if (damage <= 0.0f)
        return;
    if (MPSettings.maybeDroneAIEnabled != 0 && GameFlow_GetState() != 2)
        return;

    blData->hudFadeIn = 1.0f;

    double hurt = damage;
    if (MPSettings.isMultiplayer != 0) {
        if (hitLocation != -1)
            hurt *= Plr_DMod_Multi;
        if (MPSettings.LocationDamageEnabled != 0) {
            switch (hitLocation) {
                case 5:
                    hurt *= Plr_DMod_Head;
                    break;
                case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56:
                    hurt *= Plr_DMod_LowerLimb;
                    break;
                case 20: case 21: case 32: case 35:
                    hurt *= Plr_DMod_UpperLimb;
                    break;
            }
        }
        if (MPSettings.TripleDamageModifierProfessionalMode != 0)
            hurt *= 3.0f;
    }
    else {
        switch (GameState.difficultyModifier) {
            case 1:
                hurt *= Plr_DMod_Easy;
                break;
            case 3:
                hurt *= Plr_DMod_Hard;
                break;
            case 4:
                hurt = hurt + hurt;
                break;
            default:
                hurt *= Plr_DMod_Normal;
                break;
        }
    }

    double armourTakes;
    switch (painType) {
        case 5: case 6: case 7:
            if (MPSettings.maybeDroneAIEnabled != 0)
                MPGame.players[blData->playerNum].maybeIdxOfLastInjurer = -2;
            // fall through
        case 1: case 2: case 3: case 4:
            armourTakes = 0.0;
            break;
        default:
            armourTakes = 1.0;
            break;
    }

    float flash = (float)hurt;
    double absorbed = armourTakes * hurt;
    if (absorbed > blData->armor)
        absorbed = blData->armor;
    float health = blData->health;
    blData->armor = (float)(blData->armor - absorbed);
    damage = (float)(hurt - absorbed);
    if (CheatInfo.Immortal == 0) {
        double left = (double)health - damage;
        health = (left < 0.0) ? 0.0f : (float)left;
    }

    if (painType == 7 && CheatInfo.Immortal != 0) {
        blData->hitDirections = 0xc;
    }
    else {
        if (Rand_Rand(4) == 0)
            Sound_Play3D((Action_SFX)0x88, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Input_RumbleStart(blData->playerNum, 5, Ftol(damage));
        blData->hitDirections = (painType == 6) ? 2 : 0xc;
    }

    double fade = flash * 21.333334f;
    if (128.0f < fade)
        fade = 128.0f;
    fade += blData->hitDirectionFade;
    if (255.0f < fade)
        fade = 255.0f;
    blData->hitDirectionFade = (uchar)Ftol(fade);

    if (health < 1.0f)
        health = 0.0f;
    Player_SetHealth(blData, health);
}

// AUTOLTCG
void __declspec(naked) Player_HandlePain(void) {
    _asm {
        movzx eax, bx
        push eax
        movsx eax, di
        push eax
        push esi
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        call _Player_HandlePain
        add esp, 0x14
        ret
    }
}

// True once the player is dead. The health is kept between 0 and 500; at 0, for a player whose object is still of
// the given type, the death: the cars, emplacements and gun turrets he was using let go, the dropped weapon (in
// multiplayer, with its ammo, and the grenade launcher's grenades as well) or the death music, the death message in
// multiplayer, and the dead substate.
// AUTOINJECT
bool Player_CheckForDeath(obj_tag *player, uint objectType) {
    BLData *blData = (BLData *)player->extraObjectData;
    if (player->curState == 2 || player->curState == 3)
        return true;
    if (CheatInfo.Immortal != 0)
        return false;

    float health = blData->health;
    if (health < 0.0f)
        health = 0.0f;
    else if (health > 500.0f)
        health = 500.0f;
    Player_SetHealth(blData, health);
    if (!(blData->health <= 0.0f))
        return false;
    if ((uchar)player->objectType != objectType)
        return false;

    weapon_definition_tag *weapon = &weapon_data[player->animState->currentWeaponId];
    Car_PlayerHasDied(player);
    GunImp_PlayerHasDied(player);
    GT_PlayerHasDied(player);
    if (MPSettings.isMultiplayer == 0) {
        switch_channels[0x5e] = 1;
        Music_Event(6, 1);
    }
    else if (weapon->worldModelGfx != 0 && blData->weaponObject != NULL) {
        ushort dropped = weapon->weaponBaseNum;
        Pickup_CreateSimple(&blData->weaponObject->transformMatrix, dropped, blData->weaponStats[dropped].clipOrCooldown,
                            0);
        if (weapon->weaponBaseNum == 0x1a) {
            ushort grenades = weapon_data[0x1b].weaponVariantNum;
            obj_tag *pickup = Pickup_CreateSimple(&blData->weaponObject->transformMatrix, 0x1b,
                                                  blData->weaponStats[grenades].clipOrCooldown, 1);
            if (pickup != NULL)
                pickup->effectFlags |= FLAG_HIDDEN;
        }
    }

    Player_ClearInertia(player);
    HUD_Update(blData, player);
    player->creationTimeFrames = GameState.NumFramesUnpaused;
    player->objectType = OBJECTTYPE_DEAD_PLAYER;
    Player_WeaponNone(player);
    player->animState->animFlags &= ~1;
    Player_SetCamMode(blData, CamMode_Default);
    Player_SetHealth(blData, 0.0f);
    player->curState = 2;

    if (MPSettings.isMultiplayer != 0) {
        char *text = Txt_GetStringFromHeap(0);
        short self = blData->playerNum;
        short injurer = MPGame.players[self].maybeIdxOfLastInjurer;
        short killer = (injurer == self) ? -1 : injurer;
        if (killer == -2 || killer == -1)
            sprintf(text, Txt_BindLabel(TXT_MP_DIED, 0), MPSettings.Player[self].Name);
        else
            sprintf(text, Txt_BindLabel(TXT_MP_KILLED, 0), MPSettings.Player[injurer].Name);
        Text_AddMsg((char)self, 0, 1, text, 0, 0xb4);
    }

    Sound_Play3D((Action_SFX)0x89, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    Player_ChangeSubState(player, (player->animState->inWater & 1) ? MovementType_DeadInWater : MovementType_Dead);
    MP_PlayerKilled(player);
    Collide_FreeHitList(&player->hitList);
    return true;
}

// The dead player, every frame. In zero G the body tumbles. In single player the death channel (0x62) goes up once the
// blood pane has run and skip is pressed; in multiplayer the player comes back after five seconds, unless he has used
// up his lives (game mode 0x10).
void _Player_HandleDeath(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    _Player_InWater(player, blData);
    if (GRAVITY_VECTOR.y == 0.0f) {
        player->rotation.x = (float)((double)player->rotation.x + 0.001f);
        player->rotation.y = (float)((double)player->rotation.y + 0.001f);
        player->rotation.z = (float)((double)player->rotation.z + 0.001f);
    }

    if (MPSettings.isMultiplayer == 0) {
        if (!PlrStat_OkToUpdate())
            return;
        if (!blData->hudInfo->pane[Blood].enabled) {
            HUD_DisableAll(blData);
            HUD_Enable(blData->hudInfo, Blood, 1, 0);
            return;
        }
        if (HUD_State(blData->hudInfo, Blood) == 0 && Input_Action(-1, ACTION_SKIP_CUTSCENE, 1) == 0)
            return;
        if (Menu_PlayerHasQuitOrRestarted())
            return;
        switch_channels[0x62] = 1;
        return;
    }

    if (!blData->hudInfo->pane[Blood].enabled)
        HUD_Enable(blData->hudInfo, Blood, 1, 0);
    if ((uint)(GameState.NumFramesUnpaused - player->creationTimeFrames) < FRAME_RATE_INT * 5)
        return;
    if (MPSettings.GameMode == GM_TOPAGENT && (int)MPGame.players[Control_Plr2Ind(player)].deaths >= (int)MPSettings.MaxPoints)
        return;
    if (player->curState == 3)
        return;
    MP_ReSpawn(player, blData->playerNum);
}

// AUTOLTCG
void __declspec(naked) Player_HandleDeath(void) {
    _asm {
        push eax
        call _Player_HandleDeath
        add esp, 4
        ret
    }
}

// Puts the player at a place, standing on the floor below it (searched for 3 below a point 0.1 above it) with his
// eyes 1.1 above that, in the given substate (flying, with debug_scanmode), with everything he was doing cleared.
// AUTOINJECT
void Player_StandAtNewPosition(obj_tag *player, _VECTOR *pos, _VECTOR *rot, ushort subState) {
    BLData *blData = (BLData *)player->extraObjectData;
    player->position = *pos;
    player->lastPosition = *pos;
    blData->lastPosition = player->lastPosition;
    player->rotation = *rot;
    player->lastRotation = *rot;
    player->curState = 1;
    if (debug_scanmode)
        Player_ChangeSubState(player, MovementType_ScanMode);
    else
        Player_ChangeSubState(player, subState);
    Player_ClearInertia(player);
    Player_ClearAimInertia(player);

    blData->active = 1;
    blData->frozen = 0;
    blData->traverseWait = 0;
    blData->field_0x8e3 = 0;
    blData->walkClass = 1;
    blData->ropeFatigue = 0.0f;
    blData->bondMomentTimer = 0;
    blData->weaponChangeTimer = 0;
    blData->turnKick = 0.0f;
    blData->thirdIcon = 0xff;
    blData->nightVisionActive = 0;
    blData->nightVisionTimer = 1800.0f;
    blData->stepDistance = 0.0f;
    blData->fallFrames = 0.0f;
    blData->hitDirectionFade = 0;
    blData->hitDirections = 0;
    surfaced = 1;
    player->animState->inWater &= ~1;
    blData->jumpState = JUMP_NONE;
    blData->headSwimOffset = 0.7f;
    blData->headCrouchOffset = 0.0f;
    blData->animWait = 0;
    Vec_Zero(&blData->movement);
    Vec_Zero(&blData->lastMovement);
    Vec_Zero(&blData->lastRotationDelta);
    Vec_Zero(&blData->rotationDelta);
    Vec_Zero(&blData->field_0x3c);
    blData->field_0x8cc = 0;

    _VECTOR floor;
    Vec_Copy(&player->position, &floor);
    floor.y = (float)((double)floor.y + 0.1f);
    build_PointOnFloor(build_FindCel(&floor, glb_world), NULL, &floor, 3.0f, NULL);
    floor.y = (float)((double)floor.y + 1.1f);
    Vec_Copy(&floor, &player->position);
    control_unlink_object(player);
    Control_BuildWorldSph(player);
    build_LinkToRoom(player, 1, glb_world);

    glb_viewer[blData->playerNum]->someCel = NULL;
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    Camera_Set(viewer, &viewer->pos, &viewer->someVec);
    if (MPSettings.isMultiplayer != 0)
        AnimObjectSetAiming(player, &blData->muzzle, 1000.0f, 0.2f, 0, 5);
    player->transformFlags |= TRANSFORM_MOVED;
}

// Sets the walking pace from the speed - 1 at up to 0.2, 2 from 0.8 - and stops the view levelling itself. A change
// of pace sets it levelling again when the player's auto-centre option is on. Inlined on PS2; the name is ours.
// FUNC_AT(0x000a9160)
void Player_UpdateWalkClass(obj_tag *player, float speed, float levelTarget) {
    BLData *blData = (BLData *)player->extraObjectData;
    blData->pitchAutoLevelTarget = levelTarget;
    if (speed < 0.0f)
        speed = -speed;
    char walkClass;
    if (speed <= 0.2f)
        walkClass = 1;
    else if (speed >= 0.8f)
        walkClass = 2;
    else
        return;
    if (walkClass == blData->walkClass)
        return;
    blData->walkClass = walkClass;
    if (PlayerInputs[blData->playerNum].levelViewOnPace != 0)
        blData->aimAutoLevelState = 2;
}

// Turns the player's matrix by the frame's rotationDelta
static void TurnByRotationDelta(BLData *blData, obj_tag *player) {
    _MATRIX turn;
    RotMatrix(&blData->rotationDelta, &turn);
    Concat(&player->transformMatrix, &turn);
    Mat_CopyRot(&turn, &player->transformMatrix);
}

// Counts down the wait for a crouch or stand anim, and chooses the anim set again when it ends
static void WaitForAnimSet(BLData *blData, obj_tag *player, void (__cdecl *initAnimSet)(obj_tag *)) {
    if (blData->animWait == 0)
        return;
    blData->animWait--;
    if (AnimScriptIsStopped(player) || blData->animWait == 1) {
        blData->animWait = 0;
        initAnimSet(player);
    }
}

// The walking anims, from the frame's movement
static void UpdateWalkAnims(BLData *blData, obj_tag *player, float speed) {
    AnimSetUpdate(player, ((BLData *)player->extraObjectData)->animSet, speed, blData->movement.z,
                  FRAME_RATE_MUL * 0.1f, blData->movement.x, FRAME_RATE_MUL * 0.075f);
}

// Walking: into the water, the stick, the anims, the pace (by twice the speed), jumping and crouching. The player's
// object in EAX, his data in ECX.
void _Player_SSWalk(obj_tag *player, BLData *blData) {
    if (_Player_InWater(player, blData))
        return;
    WaitForAnimSet(blData, player, PlayerAnimSetInitNormal);
    Player_Move(blData, player, 1.0f);
    float speed = Magnitude(blData->movement.z);
    if (blData->animWait == 0)
        UpdateWalkAnims(blData, player, speed);
    Player_UpdateWalkClass(player, speed + speed, 0.0f);
    _Player_HandleJump(blData, player, &GRAVITY_VECTOR);
    TurnByRotationDelta(blData, player);
}

// AUTOLTCG
void __declspec(naked) Player_SSWalk(void) {
    _asm {
        push ecx
        push eax
        call _Player_SSWalk
        add esp, 8
        ret
    }
}

// Crouching: as walking at three quarters of the speed, standing up (as the player's crouch option says: on letting
// go of crouch, or on pressing crouch or jump) when there is room, 0.95 above the matrix's position. The player's
// object in EAX, his data in ECX.
void _Player_SSCrouch(obj_tag *player, BLData *blData) {
    if (_Player_InWater(player, blData))
        return;
    WaitForAnimSet(blData, player, PlayerAnimSetInitCrouch);
    Player_Move(blData, player, 0.75f);
    if (blData->animWait == 0) {
        float speed = Magnitude(blData->movement.z);
        UpdateWalkAnims(blData, player, speed);
    }
    Player_UpdateWalkClass(player, blData->movement.z, 0.0f);

    char playerNum = blData->playerNum;
    uchar standUp;
    if (PlayerInputs[playerNum].crouchToggle == 0) {
        standUp = Input_Action(playerNum, ACTION_CROUCH, 1) == 0;
    }
    else {
        standUp = (uchar)Input_Action(playerNum, ACTION_CROUCH, 4);
        standUp |= (uchar)Input_Action(blData->playerNum, ACTION_JUMP, 4);
    }
    if (!(player->animState->animFlags & ANIM_CONTACT_ABOVE) && standUp && blData->animWait == 0) {
        _VECTOR above;
        Vec_Copy(Mat_Position(player->transformMatrix), &above);
        above.y = (float)((double)above.y + 0.95f);
        if (Collide_LineOfSight(&player->position, &above, player->inCel, player, NULL, 9))
            Player_ChangeSubState(player, MovementType_Walk);
    }
    TurnByRotationDelta(blData, player);
}

// AUTOLTCG
void __declspec(naked) Player_SSCrouch(void) {
    _asm {
        push ecx
        push eax
        call _Player_SSCrouch
        add esp, 8
        ret
    }
}

// Breath under water: the air pane shows swimming or on a wire; the head (its bone) under the cel's water eases
// underwater towards 1 and, outside zero G, uses up air at 5 a second, with the going-under sound; above it the air
// comes back at 20 a second, with the coming-up sound. Out of air, the player is hurt twice a second. The player's
// object in EAX, his data in ECX.
void _Player_MonitorAir(obj_tag *player, BLData *blData) {
    short subState = (short)player->subState;
    HUD_Enable(blData->hudInfo, Air, subState == MovementType_Swim || subState == MovementType_Wire, 0);
    _VECTOR head;
    _MATRIX headMatrix;
    AnimGetBoneWorldTrans(player, BONE_HEAD, 0, &head, &headMatrix);
    if (player->inCel == NULL)
        return;

    bool breathes = !(player->subState >= MovementType_ZeroG && player->subState <= MovementType_ZeroG_Anim);
    uint flags;
    if (player->inCel->shearHeight > head.y) {
        blData->underwater = moveTowardsValue(blData->underwater, 1.0f, REC_FRAME_RATE);
        flags = blData->waterFlags | WATER_HEAD_UNDER;
    }
    else {
        float away = -blData->underwater;
        double underwater = blData->underwater + (double)REC_FRAME_RATE * away * 0.5f;
        if (away < 0.0f) {
            if (underwater < 0.0)
                underwater = 0.0;
        }
        else if (underwater > 0.0) {
            underwater = 0.0;
        }
        blData->underwater = (float)underwater;
        if (blData->waterFlags & WATER_HEAD_UNDER)
            blData->waterFlags |= WATER_HEAD_WAS_UNDER;
        flags = blData->waterFlags & ~WATER_HEAD_UNDER;
    }
    blData->waterFlags = flags;
    blData->waterFlags = flags | (player->subState == MovementType_Wire ? WATER_HEAD_UNDER : 0);

    if (player->inCel != NULL && player->inCel->shearHeight > head.y && breathes) {
        blData->air = (float)(blData->air - (double)REC_FRAME_RATE * 5.0f);
        if (surfaced) {
            if (blData->waterSound != NULL)
                Sound_Stop(blData->waterSound);
            blData->waterSound = Sound_Play3D((Action_SFX)0x27, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            surfaced = 0;
        }
    }
    else {
        blData->air = (float)((double)REC_FRAME_RATE * 20.0f + blData->air);
        if (!surfaced) {
            if (blData->waterSound != NULL) {
                Sound_Stop(blData->waterSound);
                blData->waterSound = NULL;
            }
            Sound_PlayExt((Action_SFX)0x60c, 100.0f, 0, 0);
            surfaced = 1;
        }
    }

    float air = blData->air;
    if (air < 0.0f)
        air = 0.0f;
    else if (air > 100.0f)
        air = 100.0f;
    blData->air = air;
    if (air < 1.0f && GameState.NumFramesUnpaused % (uint)Ftol((double)_FRAME_RATE * 0.5f) == 0) {
        Sound_PlayExt((Action_SFX)0x60b, 100.0f, 0, 0);
        _Player_HandlePain(player, 2.0f, blData, -1, 7);
    }
}

// AUTOLTCG
void __declspec(naked) Player_MonitorAir(void) {
    _asm {
        push ecx
        push eax
        call _Player_MonitorAir
        add esp, 8
        ret
    }
}

// On the ground: crouch, or jump - by gravity times -0.4, with room above (0.95 over the matrix's position) - or, with
// the wire icon showing, catch the nearest wire (within 1000). In CastleIndoors1, before channel 0x48, jumping only
// makes a sound. In the air: hitting something above starts the fall; after jumpWait frames, landing ends the jump.
// The player's data in EAX, his object in ECX.
void _Player_HandleJump(BLData *blData, obj_tag *player, _VECTOR *gravity) {
    switch (blData->jumpState) {
        case JUMP_NONE: {
            if ((uchar)Input_Action(blData->playerNum, ACTION_CROUCH, 4) && blData->animWait == 0) {
                Player_ChangeSubState(player, MovementType_Crouch);
                return;
            }
            if (blData->groundBits == 0)
                return;
            if (player->animState->animFlags & ANIM_CONTACT_ABOVE)
                return;
            if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && switch_channels[0x48] == 0) {
                if (Input_Action(blData->playerNum, ACTION_JUMP, 4))
                    Sound_PlayExt((Action_SFX)0x1db, 100.0f, 0, 0);
                return;
            }
            if (!Input_Action(blData->playerNum, ACTION_JUMP, 4))
                return;

            if (blData->thirdIcon == THIRDICON_WIRE) {
                obj_tag *wire = NULL;
                float nearest = 1000.0f;
                for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
                    if (obj->objectType != OBJECTTYPE_THIRDCAM_OR_WIRE)
                        continue;
                    double distance = Vec_Dist3D(&obj->position, &player->position);
                    if (distance < nearest) {
                        nearest = (float)distance;
                        wire = obj;
                    }
                }
                if (wire != NULL) {
                    Player_CollWire(player, blData, wire);
                    return;
                }
            }

            _VECTOR above;
            Vec_Copy(Mat_Position(player->transformMatrix), &above);
            above.y = (float)((double)above.y + 0.95f);
            if (!Collide_LineOfSight(&player->position, &above, player->inCel, player, NULL, 9))
                return;
            Vec_Zero(&blData->field_0x3c);
            auxVec_AddMulR32(&blData->field_0x3c, gravity, -0.4f, &blData->field_0x3c);
            blData->jumpState = JUMP_RISING;
            blData->jumpWait = 4;
            player->animState->animFlags &= ~ANIM_CONTACT_FLOOR;
            Sound_Play3D((Action_SFX)0xa3, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            return;
        }

        case JUMP_RISING: {
            char flags = player->animState->animFlags;
            if (flags & ANIM_CONTACT_ABOVE) {
                blData->jumpState = JUMP_FALLING;
                Vec_Zero(&blData->field_0x3c);
                return;
            }
            if (blData->jumpWait != 0) {
                blData->jumpWait--;
                return;
            }
            if (!(flags & ANIM_CONTACT_FLOOR))
                return;
            if (blData->field_0x3c.y > 0.0f)
                return;
        }
            // fall through
        case JUMP_FALLING:
            if (!(player->animState->animFlags & ANIM_CONTACT_FLOOR))
                return;
            Vec_Zero(&blData->field_0x3c);
            blData->jumpState = JUMP_NONE;
            if (player->animState->floor.materialType == MATERIAL_0D || player->animState->floor.materialType == MATERIAL_0E)
                Sound_Play3D((Action_SFX)0x5fb, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            else
                Sound_Play3D((Action_SFX)0xa3, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            blData->field_0x8f6 = 8;
            blData->stepDistance = 999.0f;
            return;
    }
}

// AUTOLTCG
void __declspec(naked) Player_HandleJump(void) {
    _asm {
        push dword ptr [esp + 4]
        push ecx
        push eax
        call _Player_HandleJump
        add esp, 0xc
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Turning and aiming speeds, and the right stick (Player_Aiming)
// ---------------------------------------------------------------------------------------------------------------

// A turning speed that accelerates while the stick is held over: from base towards base * mul (when full) in
// steps frames, without going past it on the way up, and always adding the step on the way down (so a speed above
// its target keeps growing). In the dead zone it is base; it never passes +-(base + base * mul). Returned unrounded:
// the state plus a step can need more than a float holds. The callers store it in *speed themselves.
static double Accelerate(float input, float speed, float base, float mul, float steps, float deadZone, bool full) {
    float step = (float)(((double)base * mul - base) / steps);
    float target = full ? (float)((double)base * mul) : base;

    double accelerated = speed;
    if (speed < target) {
        accelerated = (double)speed + step;
        if (accelerated > target)
            accelerated = target;
    }
    else if (speed > target) {
        accelerated = (double)speed + step;
        if (accelerated < target)
            accelerated = target;
    }

    float magnitude = input < 0.0f ? -input : input;
    if (magnitude <= deadZone)
        accelerated = base;

    double limit = ((double)mul + 1.0f) * base;
    if (accelerated < 0.0) {
        float low = (float)-limit;
        return (accelerated < low) ? low : accelerated;
    }
    float high = (float)limit;
    return (accelerated > high) ? high : accelerated;
}

// The speed goes for base * mul once the stick is at least fullZone over
double _AccelFunc0(float input, float *speed, float base, float mul, float steps, float deadZone, float fullZone) {
    float magnitude = input < 0.0f ? -input : input;
    return Accelerate(input, *speed, base, mul, steps, deadZone, magnitude >= fullZone);
}

// The original's caller keeps EDX across the call (Player_Aiming), and Ghidra has its return in ST0 as custom
// storage; the adaptor keeps ECX and EDX.
// AUTOLTCG
void __declspec(naked) AccelFunc0(void) {
    _asm {
        push ecx
        push edx
        mov eax, esp
        push dword ptr [eax + 0x24]
        push dword ptr [eax + 0x20]
        push dword ptr [eax + 0x1c]
        push dword ptr [eax + 0x18]
        push dword ptr [eax + 0x14]
        push dword ptr [eax + 0x10]
        push dword ptr [eax + 0x0c]
        call _AccelFunc0
        add esp, 0x1c
        pop edx
        pop ecx
        ret
    }
}

// AccelFunc0 with the caller saying when the speed goes for base * mul. The name is ours.
// FUNC_AT(0x000b7520)
double AccelFunc0Flag(float input, float *speed, float base, float mul, float steps, float deadZone, int full) {
    return Accelerate(input, *speed, base, mul, steps, deadZone, full != 0);
}

// The right stick, outside the remote-control substates and zero G. Not aiming, it pitches the view (and stops the
// view levelling itself while it does). Aiming through a sighted scope (WPNFLAG_SIGHTED_SCOPE), it turns and
// pitches the view, slowed by the zoom. Aiming otherwise, it moves the crosshair over the screen, and turns or
// pitches the view only once the crosshair is pushed past its edge - slower under water.
// AUTOINJECT
void Player_Aiming(obj_tag *player) {
    short subState = (short)player->subState;
    BLData *blData = (BLData *)player->extraObjectData;
    if (subState >= MovementType_ZeroG && (subState <= MovementType_Emplacement || subState == MovementType_Ronin))
        return;

    AnimState *anim = player->animState;
    if (!(anim->animFlags & 1)) {
        float look = Input_Actionf(blData->playerNum, ACTION_AIM_U_D, 1);
        double accel = _AccelFunc0(look, &blData->lookAccel, Plr_NoAimTurnSpeed_Y, Plr_NoAimTurnSpeed_Y_Mul,
                                  Plr_NoAimTurnSpeed_Y_Steps, 0.05f, 0.95f);
        blData->lookAccel = (float)accel;
        double pitch = accel * FRAME_RATE_MUL * look;
        blData->lastRotationDelta.x = (float)pitch;
        if (pitch > 0.0002f || pitch < -0.0002f || pitch != pitch) {
            blData->aimAutoLevelState = 1;
            blData->pitchFromHorizontal = (float)(pitch * 1.5707964f + blData->pitchFromHorizontal);
        }
        return;
    }

    if (weapon_data[anim->currentWeaponId].weaponFlags & WPNFLAG_SIGHTED_SCOPE) {
        float turn = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_X, 1);
        float pitch = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_Y, 1);
        float accelX = (float)_AccelFunc0(turn, &blData->scopeAccelX, Plr_ScopeSpeed_X, Plr_ScopeSpeed_X_Mul,
                                         Plr_ScopeSpeed_X_Steps, 0.05f, 0.95f);
        blData->scopeAccelX = accelX;
        double accelY = _AccelFunc0(pitch, &blData->scopeAccelY, Plr_ScopeSpeed_Y, Plr_ScopeSpeed_Y_Mul,
                                   Plr_ScopeSpeed_Y_Steps, 0.05f, 0.95f);
        blData->scopeAccelY = (float)accelY;
        double turnBy = (double)accelX * FRAME_RATE_MUL * turn;
        blData->aimX = 0.0f;
        blData->aimY = 0.0f;
        float pitchBy = (float)(accelY * FRAME_RATE_MUL * pitch);
        blData->lastRotationDelta.y = (float)turnBy;
        blData->lastRotationDelta.x = pitchBy;
        double unzoom = 1.0 / blData->zoom;
        float unzoomed = (float)unzoom;
        blData->rotationDelta.y = (float)(blData->rotationDelta.y - unzoom * turnBy);
        blData->pitchFromHorizontal = (float)((double)pitchBy * 0.63661975f * unzoomed + blData->pitchFromHorizontal);
        return;
    }

    float stickX = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_X, 1);
    float stickY = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_Y, 1);
    float turn = 0.0f;
    float pitch = 0.0f;
    double aimY = (double)Plr_AimSpeed_Y * FRAME_RATE_MUL * stickY * 0.125f + blData->aimY;
    blData->aimX = (float)((double)FRAME_RATE_MUL * Plr_AimSpeed_X * stickX * 0.125f + blData->aimX);
    blData->aimY = (float)aimY;

    int turning = 0;
    float aimXMagnitude = blData->aimX < 0.0f ? -blData->aimX : blData->aimX;
    if (aimXMagnitude > 0.296f) {
        turning = 1;
        turn = (float)((double)stickX * 0.125f);
        if (blData->aimX < -0.296f)
            blData->aimX = -0.296f;
        else if (blData->aimX > 0.296f)
            blData->aimX = 0.296f;
    }

    // The y edge is tested on the unrounded sum
    int pitching = 0;
    double aimYMagnitude = aimY < 0.0 ? -aimY : aimY;
    if (aimYMagnitude > 0.4f) {
        pitching = 1;
        pitch = (float)((double)stickY * 0.125f);
        if (aimY < -0.4f)
            blData->aimY = -0.4f;
        else if (aimY > 0.4f)
            blData->aimY = 0.4f;
    }

    float accelX = (float)AccelFunc0Flag(stickX, &blData->aimAccelX, Plr_AimTurnSpeed_X, Plr_ScopeSpeed_X_Mul,
                                         Plr_ScopeSpeed_X_Steps, 0.05f, turning);
    blData->aimAccelX = accelX;
    double accelY = AccelFunc0Flag(stickY, &blData->aimAccelY, Plr_AimTurnSpeed_Y, Plr_ScopeSpeed_Y_Mul,
                                   Plr_ScopeSpeed_Y_Steps, 0.05f, pitching);
    blData->aimAccelY = (float)accelY;
    turn = (float)((double)accelX * FRAME_RATE_MUL * turn);
    blData->aimTurnX = turn;
    pitch = (float)(accelY * FRAME_RATE_MUL * pitch);
    blData->aimTurnY = pitch;

    double unzoom = 1.0 / blData->zoom;
    double turnBy = unzoom * turn;
    double pitchBy = (double)(float)unzoom * pitch;
    if (!surfaced) {
        turnBy *= 0.4f;
        pitchBy *= 0.7f;
    }
    blData->rotationDelta.y = (float)(blData->rotationDelta.y - turnBy * 1.5707964f);
    blData->pitchFromHorizontal = (float)(pitchBy + blData->pitchFromHorizontal);
}

// ---------------------------------------------------------------------------------------------------------------
// Falling, and the collision capsule (Player_Collision)
// ---------------------------------------------------------------------------------------------------------------

// The bones a dead player's capsule runs between, with the head
#define BONE_37 0x80000037
#define BONE_33 0x80000033

// 0.45 a bit over: the constant's bits are 0x3ee66667, not 0.45f's
static const float kCrouchCapsuleTop = std::bit_cast<float>(0x3ee66667u);

// The fall speed is kept within +-45 a second
static void ClampFall(BLData *blData) {
    if (blData->field_0x3c.y < -45.0f)
        blData->field_0x3c.y = -45.0f;
    else if (blData->field_0x3c.y > 45.0f)
        blData->field_0x3c.y = 45.0f;
}

// The player's capsule along up from his position: from top (scaled) to the bottom, rootHeight under it less offset
static void SetCapsule(BLData *blData, obj_tag *player, _VECTOR *up, float top, float bottom) {
    auxVec_AddMulR32(&player->position, up, top, &blData->hitTest.rayStart);
    auxVec_AddMulR32(&player->position, up, bottom, &blData->hitTest.rayEnd);
}

// Moves the player by his fall speed (gravity, unless he stands on a floor that is not too steep, in water or in a
// substate that holds him), then sets up his capsule for the frame's collision: its length and radius by substate,
// and the object it ignores. Zero G drifts without gravity; the dead fall until they hit something.
// AUTOINJECT
void Player_Collision(obj_tag *player) {
    float rootHeight = player->animState->animObj.rootHeight;
    BLData *blData = (BLData *)player->extraObjectData;
    MPGame.players[blData->playerNum].maybeIdxOfLastInjurer = -2;
    blData->hitTest.mask = 0;
    blData->hitTest.cel = player->inCel;
    _VECTOR up;
    Mat_GetUp(&up, &player->transformMatrix);

    _VECTOR *fall = &blData->field_0x3c;
    switch ((short)player->subState) {
        case MovementType_Climb:
        case MovementType_Grapple:
        case MovementType_ScanMode:
        case MovementType_Wire:
        case MovementType_Emplacement:
        case MovementType_DeadInWater:
        case 15:
            break;

        case MovementType_ZeroG:
        case MovementType_ZeroG_Anim:
            auxVec_AddMulR32(&player->position, fall, (float)((double)REC_FRAME_RATE * 3.0f), &player->position);
            break;

        case MovementType_Dead:
            if (player->animState->animFlags & ANIM_CONTACT_HIT)
                Vec_Zero(fall);
            else
                auxVec_AddMulR32(fall, &GRAVITY_VECTOR, REC_FRAME_RATE, fall);
            ClampFall(blData);
            auxVec_AddMulR32(&player->position, fall, REC_FRAME_RATE, &player->position);
            break;

        default:
            if (player->inCel != NULL && player->inCel->shearHeight > player->position.y)
                break;
            if ((player->animState->animFlags & ANIM_CONTACT_FLOOR) && !(blData->floorNormalY < 0.5f)) {
                if (blData->jumpState != JUMP_RISING) {
                    if (player->animState->floor.hitObj != NULL) {
                        Vec_MulR32(fall, fall, (float)((double)REC_FRAME_RATE + REC_FRAME_RATE));
                    }
                    else {
                        blData->movement.y = 0.0f;
                        Vec_Zero(fall);
                    }
                }
            }
            else {
                auxVec_AddMulR32(fall, &GRAVITY_VECTOR, REC_FRAME_RATE, fall);
            }
            ClampFall(blData);
            auxVec_AddMulR32(&player->position, fall, REC_FRAME_RATE, &player->position);
            break;
    }

    HITTEST_tag *capsule = &blData->hitTest;
    switch ((short)player->subState) {
        case MovementType_Climb:
        case MovementType_Creep:
            capsule->type = 0x800;
            capsule->radius = 0.275f;
            capsule->damage = 0.0f;
            SetCapsule(blData, player, &up, 0.55f, (float)-((double)rootHeight - capsule->radius));
            break;

        case MovementType_Wire:
        case 15:
            capsule->ignore = blData->attachedToSpecialMovementItem;
            capsule->type = 0x800;
            capsule->radius = 0.55f;
            capsule->damage = 0.0f;
            if (player->subState == 15)
                capsule->mask = 0x1c0;
            SetCapsule(blData, player, &up, 0.275f, (float)(2.1f - (double)rootHeight));
            break;

        case MovementType_Crouch: {
            float top = (float)-((double)rootHeight - 0.55f - 0.1f);
            capsule->ignore = blData->weaponObject;
            capsule->type = 0x800;
            capsule->radius = 0.55f;
            capsule->damage = 0.0f;
            SetCapsule(blData, player, &up, top, (float)-((double)rootHeight - kCrouchCapsuleTop));
            break;
        }

        case MovementType_Dead:
        case MovementType_DeadInWater: {
            // From the head, as long as half the body (head to bone 0x33) or half the neck (head to bone 0x37), the
            // longer, and no wider than the anim's root height
            capsule->radius = (0.55f < rootHeight) ? 0.55f : rootHeight;
            _VECTOR head, neck, body;
            AnimGetBoneWorldTrans(player, BONE_HEAD, 0, &head, NULL);
            AnimGetBoneWorldTrans(player, BONE_37, 0, &neck, NULL);
            AnimGetBoneWorldTrans(player, BONE_33, 0, &body, NULL);
            Vec_Subtract(&head, &neck, &up);
            float length = (float)(Vec_NormaliseLen(&up, &up) * 0.5f);
            double halfBody = Vec_Dist3D(&head, &body) * 0.5f;
            if (!(length > halfBody))
                length = (float)halfBody;
            capsule->ignore = blData->weaponObject;
            capsule->type = 0x800;
            capsule->damage = 1.0f;
            Vec_Copy(&head, &capsule->rayStart);
            auxVec_AddMulR32(&player->position, &up, (float)-((double)length - 0.55f), &capsule->rayEnd);
            break;
        }

        case MovementType_Emplacement:
            // On anything but a gun turret, the capsule is set up but does not collide
            if (blData->remoteControlDevice->objectType != OBJECTTYPE_GUNTURRET) {
                capsule->ignore = blData->remoteControlDevice;
                capsule->type = 0x800;
                capsule->radius = 0.55f;
                capsule->damage = 0.0f;
                SetCapsule(blData, player, &up, 0.275f, (float)-((double)rootHeight - 0.55f));
                capsule->type = 0;
                break;
            }
            // fall through
        default:
            capsule->ignore = blData->weaponObject;
            capsule->type = 0x800;
            capsule->radius = 0.55f;
            capsule->damage = 0.0f;
            SetCapsule(blData, player, &up, 0.275f, (float)-((double)rootHeight - 0.55f));
            break;
    }
    player->unknown_0xd9 = 1;
}

// ---------------------------------------------------------------------------------------------------------------
// What the collision found (Player_CollisionHandler, Player_DealWithObjHit)
// ---------------------------------------------------------------------------------------------------------------

// The originals, called by address (the weapon's are the next parcel's)
#define PlayerNotDrone_FeetOnPoint ((obj_tag *(__cdecl *)(obj_tag *obj, HITTEST_tag *test, float *floorNormalY, char dying))0x000ab450)
#define PlayerCollLadder ((void (__cdecl *)(obj_tag *player, BLData *blData, obj_tag *ladder))0x000ae930)

// The anim flags the collision works out afresh every frame
#define ANIM_CONTACTS (ANIM_CONTACT_FLOOR | ANIM_CONTACT_HIT | ANIM_CONTACT_ABOVE | ANIM_CONTACT_MOVER)

// A fall of more than a second (60 frames) hurts by its frames beyond that, from 10 on
#define FALL_SAFE_FRAMES 60.0f
#define FALL_HURT_FRAMES 10

// The bot traits (BOT_stats_t::traitFlags) that make its hits on the player hurt more: with fists, and otherwise
#define BOT_TRAIT_FIST_DAMAGE 0x04
#define BOT_TRAIT_SHOT_DAMAGE 0x02

// The ids of the fists, which the fist trait boosts
static bool IsFist(short weapon) {
    return weapon == 1 || weapon == 0x64 || weapon == 0x65 || weapon == 0x66;
}

// Which sides the hit came from, for the HUD: the shot's direction turned into the view's frame. Head on (neither
// across nor up and down by half), the overlays 2 and 3; otherwise left (4) or right (8), down (1) or up (2) by the
// larger part.
static uchar HitDirections(_VECTOR *direction) {
    float x = direction->x < 0.0f ? -direction->x : direction->x;
    float y = direction->y < 0.0f ? -direction->y : direction->y;
    if (x < 0.5f && y < 0.5f)
        return 0xc;

    uchar directions = 0;
    if (x > y) {
        if (direction->x < 0.0f)
            directions = 4;
        if (direction->x > 0.0f)
            directions |= 8;
    }
    else {
        if (direction->y < 0.0f)
            directions = 1;
        if (direction->y > 0.0f)
            directions |= 2;
    }
    return directions;
}

// One thing the player's capsule hit: water (its ripple, and the floor it counts as), a wire or ladder to catch, a
// hurt volume, or a bullet. A bullet goes through multiplayer's rules (bots' damage traits, assassination, friendly
// fire in MP_RegisterBulletHit) and the drones' (Drone_ModPlayerHitDamage), then hurts, with where it came from shown
// on the HUD and its sound. Returns 0, always, in ST0.
// AUTOINJECT
float Player_DealWithObjHit(obj_tag *player, BLData *blData, HITDATA_tag *hit) {
    char headshot = 0;
    int material = hit->materialType & 0x3f;
    if (material == MATERIAL_0D || material == MATERIAL_0E) {
        AnimState *anim = player->animState;
        if ((uchar)anim->floor.materialType < MATERIAL_0D || (uchar)anim->floor.materialType > MATERIAL_0E)
            anim->floor = *hit;
        if (player->animState->animFlags & 4)
            Env_WaterRipple(hit);
    }

    obj_tag *hitObj = hit->hitObj;
    if (hitObj == NULL)
        return 0.0f;

    switch ((uchar)hitObj->objectType) {
        case OBJECTTYPE_THIRDCAM_OR_WIRE:
            Player_CollWire(player, blData, hitObj);
            return 0.0f;

        case OBJECTTYPE_LADDER:
            PlayerCollLadder(player, blData, hitObj);
            return 0.0f;

        case OBJECTTYPE_HURT: {
            ushort painType = Hurt_GetType(hitObj);
            float damage = (float)((double)Hurt_GetDamage(hit->hitObj) + hit->dmgAmt);
            _Player_HandlePain(player, damage, blData, -1, painType);
            return 0.0f;
        }

        case OBJECTTYPE_BULLET:
            break;

        default:
            return 0.0f;
    }

    if (MPSettings.maybeDroneAIEnabled != 0) {
        BU_tag *bullet = (BU_tag *)hitObj->extraObjectData;
        if (Control_Plr2Ind(bullet->firedByObj) >= 4 && bullet->firedByObj != NULL) {
            BOT_vars_t *bot = ((Drone_tag *)bullet->firedByObj->extraObjectData)->botVars;
            if (IsFist(bullet->wpnDef->weaponVariantNum)) {
                if (bot->stats.traitFlags & BOT_TRAIT_FIST_DAMAGE)
                    hit->dmgAmt = (float)((double)hit->dmgAmt * 1.5f);
            }
            else if (bot->stats.traitFlags & BOT_TRAIT_SHOT_DAMAGE) {
                hit->dmgAmt = (float)((double)hit->dmgAmt * 1.25f);
            }
        }

        // In assassination the assassin kills his target outright with a hit to the head (bone 5) or, within 70
        // degrees of where the target faces, to bones 1 to 4
        if (MPSettings.GameMode == GM_ASSASSIN && MP_IsAssasin(bullet->firedByObj) && MP_IsTarget(player)) {
            int bone = (ushort)hit->hitBoneIdx;
            if (bone > 0) {
                if (bone <= 4) {
                    float from = maybeAtan2(hit->maybehitDirection.x, hit->maybehitDirection.z);
                    double degrees = Vec_AngleDifference(from, player->rotation.y) * 57.295776f;
                    if (degrees < 0.0)
                        degrees = -degrees;
                    if (degrees < 70.0f)
                        hit->dmgAmt = blData->health;
                }
                else if (bone == 5) {
                    hit->dmgAmt = blData->health;
                }
            }
        }
        if (hit->hitBoneIdx == 5)
            headshot = 1;
    }

    if (!MP_RegisterBulletHit(blData->playerNum, hit->hitObj, (float)((double)blData->health - hit->dmgAmt)))
        return 0.0f;
    BU_tag *bullet = (BU_tag *)hit->hitObj->extraObjectData;
    if (!Drone_ModPlayerHitDamage(player, hit))
        return 0.0f;
    _Player_HandlePain(player, hit->dmgAmt, blData, hit->hitBoneIdx, 0);

    _VECTOR direction;
    Vec_Copy(&hit->maybehitDirection, &direction);
    ApplyMatrixLVI(&glb_viewer[blData->playerNum]->worldMatrix, &direction);
    Vec_Normalise(&direction, &direction);
    blData->hitDirections = HitDirections(&direction);

    if (!(bullet->wpnDef->impactFlags & 0x180))
        Sound_PlayExt((Action_SFX)(headshot ? 0x600 : 0x412), 100.0f, 0, 0);
    if (MPSettings.isMultiplayer == 0 && (bullet->wpnDef->damageClass & 1)) {
        if (bullet->wpnDef->weaponBaseNum == 0x65)
            blData->turnKick = -0.1f;
        if (bullet->wpnDef->weaponBaseNum == 0x66)
            blData->turnKick = 0.1f;
        if (bullet->wpnDef->weaponBaseNum == 0x63)
            blData->turnKick = 0.1f;
        Sound_Play3D((Action_SFX)0x171, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }
    return 0.0f;
}

// After the collision: the feet (outside a ladder), the fall - its length, and the damage from a long one on landing
// - the capsule's push and each hit, the contact flags from the first that counts, then the gun, death, the laser,
// the muzzle flash, the grapple's rope and the watch. On a ladder the hits are only felt for, the ladder's own left
// out. Nothing while the player is inactive or flying freely.
// AUTOINJECT
void Player_CollisionHandler(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    blData->thirdIcon = THIRDICON_NONE;
    if (!blData->active)
        return;
    player->animState->animFlags &= ~ANIM_CONTACTS;
    player->animState->floor.materialType = 0;
    if (debug_scanmode)
        return;

    if (player->subState != MovementType_Climb) {
        char dying = player->curState == 2 || player->curState == 3;
        PlayerNotDrone_FeetOnPoint(player, &blData->hitTest, &blData->floorNormalY, dying);
        blData->groundBits = (ushort)((short)blData->groundBits >> 1);
        if (player->animState->animFlags & ANIM_CONTACT_FLOOR)
            blData->groundBits |= 8;

        switch ((short)player->subState) {
            case 1: case 2: case 3: case 5: case 6: case 7: case 8: case 9: case 13: case 14: case 15:
                blData->fallFrames = 0.0f;
                break;
        }
        if (blData->groundBits != 0) {
            if (blData->fallFrames > FALL_SAFE_FRAMES) {
                double over = (double)blData->fallFrames - FALL_SAFE_FRAMES;
                if (over < 0.0)
                    over = 0.0;
                short frames = (short)Ftol(over);
                if (frames >= FALL_HURT_FRAMES)
                    _Player_HandlePain(player, frames, blData, -1, 6);
            }
            blData->fallFrames = 0.0f;
        }
        else {
            if (!(blData->field_0x3c.y >= 0.0f))
                blData->fallFrames = (float)((double)FRAME_RATE_MUL + blData->fallFrames);
            else
                blData->fallFrames = 0.0f;
            if (GameState.CurrentLevelHashcode == HT_Level_Tower2B && player->position.y < -10.0f)
                switch_channels[0x7c] = 1;
        }

        if (player->hitList != NULL) {
            Vec_Add2(&blData->hitTest.push, &player->position, &player->position);
            HITDATA_tag *contact = NULL;
            for (HITDATA_tag *hit = player->hitList; hit != NULL; hit = hit->next) {
                Player_DealWithObjHit(player, blData, hit);
                if (!(hit->flags & HIT_NO_CONTACT) && contact == NULL)
                    contact = hit;
            }
            if (contact != NULL) {
                _VECTOR offset;
                Vec_Subtract(&contact->hitPosition, &blData->hitTest.rayStart, &offset);
                player->animState->animFlags |= ANIM_CONTACT_HIT;
                if (offset.y > 0.0f)
                    player->animState->animFlags |= ANIM_CONTACT_ABOVE;
            }
        }
        if ((player->animState->animFlags & ANIM_CONTACT_FLOOR) && blData->jumpState != JUMP_RISING) {
            blData->movement.y = 0.0f;
            Vec_Zero(&blData->field_0x3c);
        }
    }
    else {
        HITDATA_tag *contact = NULL;
        for (HITDATA_tag *hit = player->hitList; hit != NULL; hit = hit->next) {
            if (!(hit->flags & HIT_NO_CONTACT) && contact == NULL)
                contact = hit;
            if (contact != NULL && contact->hitObj == blData->attachedToSpecialMovementItem)
                contact = NULL;
        }
        if (contact != NULL) {
            _VECTOR offset;
            Vec_Subtract(&contact->hitPosition, &blData->hitTest.rayStart, &offset);
            if (offset.y > 0.0f)
                player->animState->animFlags |= ANIM_CONTACT_ABOVE;
            if (offset.y < 0.0f)
                player->animState->animFlags |= ANIM_CONTACT_FLOOR;
        }
        blData->fallFrames = 0.0f;
    }

    player->transformFlags |= TRANSFORM_MOVED;
    Vec_Copy(&player->position, Mat_Position(player->transformMatrix));
    Player_PositionGun(player, blData);
    AnimObjectUpdate(blData->weaponObject);
    glb_viewer[blData->playerNum]->camMode = (uchar)blData->camMode;
    Player_CheckForDeath(player, OBJECTTYPE_PLAYER);
    Player_LaserPointer(player);
    if (blData->shotFired)
        Player_MuzzleFlash(player, &blData->muzzle);
    if (blData->muzzleFlashFrames > 0)
        blData->muzzleFlashFrames--;
    if (blData->muzzleFlashFrames == 0)
        View_SetDrawInNoViews(blData->muzzleFlashObj);
    else
        Draw_MuzzleFlash(player);
    _Player_GrappleSetRope(player);
    Player_SetWatch(player);
}

// ---------------------------------------------------------------------------------------------------------------
// Making the player (Player_Init)
// ---------------------------------------------------------------------------------------------------------------

#define AnimDebugCreate ((void (__cdecl *)(void))0x00019180)
#define BodyGlow_Create ((obj_tag *(__cdecl *)(obj_tag *obj))0x000ce4b0)

// The camera updater, by its original address (which jumps to ours), as the game has it
#define PLAYER_POSITION_CAMERA ((cameraUpdateFunc)0x000a8a60)

// XBE_GLOBAL(0x001637c0, 0x1d0)
#define MP_skins ((MP_skin*)0x001637c0)
// The fists' models, by BLData::punchSkin
// XBE_GLOBAL(0x0017e8e8, 0x10)
#define PunchSkin (*(HASHCODE(*)[4])0x0017e8e8)

// The player's skins in single player, and the fists that go with them
#define SKIN_BOND          ((HASHCODE)0x05000007)
#define SKIN_LEVELS_1_TO_4 ((HASHCODE)0x05000009)
#define SKIN_LEVELS_7_8    ((HASHCODE)0x0500000d)
#define SKIN_LEVEL_1B      ((HASHCODE)0x0500007c)
#define SKIN_MP_FISTS      ((HASHCODE)0x050000b0)

// The anims Player_Init starts with
#define ANIM_START_MP      ((HASHCODE)0x060001d4)
#define ANIM_START_CROUCH  ((HASHCODE)0x060009ff)
#define ANIM_START         ((HASHCODE)0x06000a00)

// The start position's parameters as Player_Init reads them
#pragma pack(push, 1)
typedef struct {
    uchar _unknown00[0x2c];
    uint switchChannel;     // 0x2c - unless 0, the start is used only once its channel is set
    uchar _unknown30[0x38 - 0x30];
    uint startState;        // 0x38 - PlayerStartState
} Player_Start_Params;
#pragma pack(pop)
static_assert(offsetof(Player_Start_Params, startState) == 0x38, "Bad offset of startState");

typedef enum {
    PlayerStart_Standing = 0,
    PlayerStart_Swimming = 1,
    PlayerStart_ZeroG = 2,
    PlayerStart_Crouching = 3,
} PlayerStartState;

// Makes player playerNum (if not made already, and his start's channel is set): his object and data, his skin and
// fists (by the level, or his multiplayer character), the camera, health (with his multiplayer handicap), the HUD,
// weapons and grapple, gravity, and puts him at the start in the start's state.
// AUTOINJECT
obj_tag* Player_Init(ushort playerNum, _VECTOR *pos, _VECTOR *rot, level_tag *spawnPointData) {
    Player_Start_Params *params = (Player_Start_Params *)spawnPointData;
    if (params->switchChannel != 0 && switch_channels[params->switchChannel] == 0)
        return NULL;

    short num = (short)playerNum;
    switch_channels[0x5e] = 0;
    switch_channels[0x62] = 0;
    if (glb_players[num] != NULL)
        return NULL;

    obj_tag *player = control_create_object(sizeof(BLData), pos, NULL, NULL);
    BLData *blData = (BLData *)player->extraObjectData;
    glb_players[num] = player;
    glb_blokes[num] = blData;
    blData->playerNum = (char)playerNum;
    blData->playerNum2 = (char)playerNum;
    player->maybeCollision = &blData->hitTest;
    Sound_StopAllWithId(SFX_GADGET_WATCH_LASER_SHOOT);
    Sound_StopAllWithId(SFX_GADGET_WATCH_TASER_SHOOT);
    Sound_StopAllWithId(SFX_GADGET_WATCH_LASER_BURN_LOOP);
    Camera_Enable(playerNum, 1, 1, NULL);
    Camera_SetFade(playerNum, -45.0f, 0xff, 1);

    blData->aimX = 0.0f;
    blData->aimY = 0.0f;
    blData->pitchAutoLevelTarget = 0.0f;
    blData->pitchFromHorizontal = 0.0f;
    blData->field_0x8e7 = 0;
    blData->zoomMax = 1.0f;
    blData->zoomTarget = 1.0f;
    blData->zoom = 1.0f;
    blData->scopeAccelX = 0.0f;
    blData->scopeAccelY = 0.0f;
    blData->aimAccelX = 0.0f;
    blData->aimAccelY = 0.0f;
    blData->turnAccelState = 0.0f;
    blData->lookAccel = 0.0f;
    blData->field_0x8e5 = 0;
    Vec_Zero(&blData->movement);
    Vec_Zero(&blData->lastMovement);
    Vec_Zero(&blData->lastRotationDelta);
    Vec_Zero(&blData->rotationDelta);
    Vec_Zero(&blData->field_0x3c);
    blData->someMPCameraThing1 = 1.5f;
    blData->someMPCameraThing2 = 1.5f;
    blData->someMPCameraThing3 = 1.5f;
    blData->someMPCameraThing4 = 0;
    blData->remoteControlDevice = NULL;
    blData->cameraFollowObject = NULL;
    blData->attachedToSpecialMovementItem = NULL;
    blData->gadgetTarget = NULL;
    player->objectType = OBJECTTYPE_PLAYER;
    player->rotation.x = 0.0f;
    player->rotation.y = rot->y;
    player->rotation.z = 0.0f;
    build_LinkToRoom(player, 0, glb_world);

    HASHCODE skin;
    char punchSkin;
    if (MPSettings.isMultiplayer == 0) {
        switch (GameState.CurrentLevelHashcode) {
            case 0x07000001: case 0x07000002: case 0x07000003: case 0x07000004:
                skin = SKIN_LEVELS_1_TO_4;
                punchSkin = 1;
                break;
            case 0x07000007: case 0x07000008:
                skin = SKIN_LEVELS_7_8;
                punchSkin = 2;
                break;
            case 0x0700001b:
                skin = SKIN_LEVEL_1B;
                punchSkin = 3;
                break;
            default:
                skin = SKIN_BOND;
                punchSkin = 0;
                break;
        }
        weapon_data[1].weaponModelHashcode = PunchSkin[punchSkin];
        weapon_data[0x37].offsetToNextAltFireVariant = 1;
    }
    else {
        MP_skin *mpSkin = &MP_skins[MPSettings.Player[num].SkinNum];
        skin = mpSkin->skinHashcode;
        punchSkin = (char)mpSkin->handType;
        weapon_data[1].weaponModelHashcode = SKIN_MP_FISTS;
        weapon_data[0x37].offsetToNextAltFireVariant = 2;
    }
    blData->punchSkin = punchSkin;
    AnimObjectNew(player, skin);
    AnimDebugCreate();
    Player_SetCamMode(blData, CamMode_Default);

    glb_viewer[num]->zoom = 1.0f;
    Camera_Set(glb_viewer[num], pos, &player->rotation);
    glb_viewer[num]->someCel = NULL;
    glb_viewer[num]->player = player;

    blData->armor = 0.0f;
    blData->field_0x844 = 0.0f;
    blData->hudFadeIn = 1.0f;
    float health = 100.0f;
    if (MPSettings.isMultiplayer != 0)
        health = (float)((double)(int)MPSettings.Player[blData->playerNum].HealthModifier + 100.0f);
    Player_SetHealth(blData, health);
    blData->healthShown = blData->health;
    blData->visibility = 0.0f;
    blData->flashTime = 0.0f;
    blData->flashTimeLeft = 0.0f;
    blData->flashBlur = 0xff;
    blData->air = 100.0f;
    blData->underwater = 0.0f;
    blData->waterFlags = 0;
    blData->alpha = 1.0f;
    blData->hitDirectionFade = 0;
    blData->hitDirections = 0;
    blData->pauseMenu = -1;
    HUD_Init(blData, player);
    HUD_Update(blData, player);
    Player_InitWeapon(blData, player);
    _Player_InitGrapple(blData, player);
    Camera_SetUpdator(playerNum, player, PLAYER_POSITION_CAMERA);
    player->effectFlags |= 0x800;
    blData->active = 1;
    blData->frozen = 0;

    GRAVITY_VECTOR.x = 0.0f;
    GRAVITY_VECTOR.y = -9.8f;
    GRAVITY_VECTOR.z = 0.0f;
    if (MPSettings.isMultiplayer != 0) {
        Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Walk);
        AnimScriptAdd(player, ANIM_START_MP);
    }
    else {
        switch (params->startState) {
            case PlayerStart_Swimming:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Swim);
                AnimScriptAdd(player, ANIM_START);
                break;
            case PlayerStart_ZeroG:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_ZeroG);
                AnimScriptAdd(player, ANIM_START);
                Vec_Zero(&GRAVITY_VECTOR);
                break;
            case PlayerStart_Crouching:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Crouch);
                AnimScriptAdd(player, ANIM_START_CROUCH);
                blData->headCrouchOffset = 0.45f;
                break;
            default:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Walk);
                AnimScriptAdd(player, ANIM_START);
                break;
        }
    }

    blData->bodyGlow = BodyGlow_Create(player);
    Control_BuildWorldSph(player);
    PlarStat_LogTimerUnpause(blData->playerNum);
    return player;
}

// ---------------------------------------------------------------------------------------------------------------
// Zero G, and flying (Player_ZeroG, Player_SSScanMode)
// ---------------------------------------------------------------------------------------------------------------


// Zero G: turned at 0.4 pi a second (slowed by the zoom aiming), and moved along all three of the player's own axes by
// the sticks and jump and crouch, the speed eased a fifth of a fifth of the way a frame towards a tenth, within +-0.05
// a frame. Reload sets the player upright over a second and a half. The original takes the player's data in ESI.
static double ZeroGEase(float previous, float target) {
    float gap = target - previous;
    double eased = (double)FRAME_RATE_MUL * 0.2f * gap * 0.2f + previous;
    if (gap < 0.0f)
        return (eased < target) ? target : eased;
    return (eased > target) ? target : eased;
}

// One axis of zero G's movement: eased, kept to +-half a step, added to the frame's movement
static void ZeroGAxis(float *last, float *movement, float target) {
    double eased = ZeroGEase(*last, target);
    *last = (float)eased;
    double limit = (double)FRAME_RATE_MUL * 0.1f * 0.5f;
    float high = (float)limit;
    float low = (float)Negate(limit);
    if (eased < low)
        *last = low;
    else if (eased > high)
        *last = high;
    *movement = *last + *movement;
}

void _Player_ZeroG(BLData *blData, obj_tag *player) {
    Vec_Zero(&blData->field_0x3c);
    blData->aimAutoLevelState = 2;
    blData->field_0x8e7 = 0;
    blData->pitchFromHorizontal = 0.0f;

    if (!(player->animState->animFlags & 1)) {
        float turn = Input_Actionf(blData->playerNum, ACTION_AIM_L_R, 1);
        blData->rotationDelta.y = (float)Negate((double)REC_FRAME_RATE * 3.1415927f * 0.4f * turn);
        float pitch = Input_Actionf(blData->playerNum, ACTION_AIM_U_D, 1);
        blData->rotationDelta.x = (float)Negate((double)REC_FRAME_RATE * 3.1415927f * 0.4f * pitch);
    }
    else {
        float turn = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_X, 1);
        blData->rotationDelta.y = (float)((double)REC_FRAME_RATE * 3.1415927f * 0.4f / blData->zoom * turn * -0.75f);
        float pitch = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_Y, 1);
        blData->rotationDelta.x = (float)((double)REC_FRAME_RATE * 3.1415927f * 0.4f / blData->zoom * pitch * -0.75f);
    }
    blData->rotationDelta.z = 0.0f;

    ZeroGAxis(&blData->lastMovement.x, &blData->movement.x,
              Input_Actionf(blData->playerNum, ACTION_WALK_L_R, 1) * 0.1f);
    ZeroGAxis(&blData->lastMovement.z, &blData->movement.z,
              Input_Actionf(blData->playerNum, ACTION_WALK_F_B, 1) * 0.1f);
    int down = (short)Input_Action(blData->playerNum, ACTION_CROUCH, 1);
    int up = (short)Input_Action(blData->playerNum, ACTION_JUMP, 1);
    ZeroGAxis(&blData->lastMovement.y, &blData->movement.y, (float)(up - down) * 0.1f);

    _MATRIX turn;
    RotMatrix(&blData->rotationDelta, &turn);
    Concat(&player->transformMatrix, &turn);
    Mat_CopyRot(&turn, &player->transformMatrix);

    if (Input_Action(blData->playerNum, ACTION_RELOAD, 4))
        blData->zeroGTurn = (short)Ftol((double)_FRAME_RATE * 1.5f);
    if (blData->zeroGTurn != 0) {
        blData->zeroGTurn--;
        // Upright: facing as now but level, or, facing straight up or down, facing where the head was
        _VECTOR dir, up;
        Mat_GetDir(&dir, &player->transformMatrix);
        Mat_GetUp(&up, &player->transformMatrix);
        double dot = (double)CONST_UP_VECTOR.z * dir.z + (double)CONST_UP_VECTOR.y * dir.y +
                     (double)CONST_UP_VECTOR.x * dir.x;
        float dotStored = (float)dot;
        if (dot > 0.995f)
            Vec_Negate(&up, &dir);
        else if (dotStored < -0.995f)
            Vec_Copy(&up, &dir);
        else
            auxVec_AddMulR32(&dir, &CONST_UP_VECTOR, -dotStored, &dir);
        Mat_Align2Up(blData->zeroGUpright.m, &CONST_UP_VECTOR.x, &dir.x);
        Quat_MatrixToQuaternion(&blData->zeroGQuat, &blData->zeroGUpright);
        quaternion_tag now, turned;
        Quat_MatrixToQuaternion(&now, &player->transformMatrix);
        Quat_Slerp_Acc((float)((double)REC_FRAME_RATE * 3.0f), now.q, blData->zeroGQuat.q, turned.q);
        Quat_QuaternionToMatrix(&player->transformMatrix, &turned);
        Mat_Normalize(&player->transformMatrix);
    }

    if (player->subState == MovementType_ZeroG_Anim)
        UpdateWalkAnims(blData, player, Magnitude(blData->movement.z));
}

// AUTOLTCG
void __declspec(naked) Player_ZeroG(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call _Player_ZeroG
        add esp, 8
        ret
    }
}

// Flying freely (substate 5): the right stick turns at pi a second, the left flies along the player's own axes (eased a
// fifth of the way a frame, sideways up to 0.225 and forwards up to 0.5 a frame, a third and a fifth as much
// backwards), jump and crouch climb and drop 3 a second. Aiming, the zoom stick changes the field of view (0.2 to
// 3.14). The original takes the player's data in EAX and the player in EBX.
static double ScanEase(float previous, float target) {
    float gap = target - previous;
    double eased = (double)FRAME_RATE_MUL * 0.2f * gap + previous;
    if (gap < 0.0f)
        return (eased < target) ? target : eased;
    return (eased > target) ? target : eased;
}

void _Player_SSScanMode(BLData *blData, obj_tag *player) {
    if (!(player->animState->animFlags & 1)) {
        blData->aimAutoLevelState = 2;
        blData->movement.y = 0.0f;
        float turn = Input_Actionf(blData->playerNum, ACTION_AIM_L_R, 1);
        blData->rotationDelta.y = (float)((double)REC_FRAME_RATE * 3.1415927f * Negate(turn));
        float pitch = Input_Actionf(blData->playerNum, ACTION_AIM_U_D, 1);
        blData->rotationDelta.x = (float)Negate((double)REC_FRAME_RATE * 3.1415927f * pitch);

        float strafe = Input_Actionf(blData->playerNum, ACTION_WALK_L_R, 1);
        float target = (float)((double)FRAME_RATE_MUL * 0.075f * 3.0f * strafe);
        double eased = ScanEase(blData->lastMovement.x, target);
        blData->lastMovement.x = (float)eased;
        double step = (double)FRAME_RATE_MUL * 0.075f;
        float low = (float)(-3.0f * step);
        if (eased < low) {
            blData->lastMovement.x = low;
        }
        else {
            float high = (float)(step * 3.0f);
            if (eased > high)
                blData->lastMovement.x = high;
        }
        blData->movement.x = blData->lastMovement.x + blData->movement.x;

        float forward = Input_Actionf(blData->playerNum, ACTION_WALK_F_B, 1);
        target = (float)((double)FRAME_RATE_MUL * 0.1f * 5.0f * forward);
        eased = ScanEase(blData->lastMovement.z, target);
        blData->lastMovement.z = (float)eased;
        step = (double)FRAME_RATE_MUL * 0.1f;
        low = (float)(-5.0f * step);
        if (eased < low) {
            blData->lastMovement.z = low;
        }
        else {
            float high = (float)(step * 5.0f);
            if (eased > high)
                blData->lastMovement.z = high;
        }
        blData->movement.z = blData->movement.z + blData->lastMovement.z;

        glb_viewer[blData->playerNum]->someCel = NULL;
        blData->rotationDelta.z = 0.0f;
        _MATRIX yaw, pitched;
        RotMatrixY(blData->rotationDelta.y, &yaw);
        RotMatrixX(blData->rotationDelta.x, &pitched);
        Concat(&yaw, &player->transformMatrix);
        Concat(&player->transformMatrix, &pitched);
        Mat_CopyRot(&pitched, &player->transformMatrix);

        float climb = Input_Actionf(blData->playerNum, ACTION_JUMP, 1);
        double y = ((double)climb - Input_Actionf(blData->playerNum, ACTION_CROUCH, 1)) * REC_FRAME_RATE * 3.0f +
                   player->position.y;
        player->position.y = (float)y;
        Mat_Position(player->transformMatrix)->y = (float)y;
    }
    else {
        double zoom = (double)Input_Actionf(blData->playerNum, ACTION_SCOPE_ZOOM, 0xc) * REC_FRAME_RATE;
        if (zoom != 0.0) {
            viewer_tag *viewer = glb_viewer[blData->playerNum];
            double fov = zoom + viewer->fovRadians;
            if (fov < 0.2f)
                fov = 0.2f;
            else if (fov > 3.14f)
                fov = 3.14f;
            viewer->fovRadians = (float)fov;
            Camera_CalcViewAngles(blData->playerNum, glb_viewer[blData->playerNum]->fovRadians);
        }
    }
    Vec_Zero(&blData->field_0x3c);
    player->animState->animObj.rootTravel = 0.0f;
}

// AUTOLTCG
void __declspec(naked) Player_SSScanMode(void) {
    _asm {
        push ebx
        push eax
        call _Player_SSScanMode
        add esp, 8
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Using things (Player_Activate)
// ---------------------------------------------------------------------------------------------------------------


// What the player uses: with the standing icon showing, the nearest creep wall (true); otherwise whatever is within 1
// of the point in front of his eyes - scripted objects, doors (true when one opens), switches, triggers, cars, gun
// turrets and emplacements, monitors, the worm, and locks he is in front of. The original takes the player's data in
// EAX and the player in EDI.
uchar _Player_Activate(BLData *blData, obj_tag *player) {
    HITDATA_tag *hitList = NULL;
    uchar used = 0;
    if (blData->thirdIcon == THIRDICON_STANDING) {
        obj_tag *wall = NULL;
        float nearest = 1000.0f;
        for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
            if (obj->objectType != OBJECTTYPE_CREEPWALL)
                continue;
            double distance = Vec_SqDist3D(&obj->position, &player->position);
            if (distance < nearest) {
                nearest = (float)distance;
                wall = obj;
            }
        }
        if (wall != NULL) {
            Player_CollCreepWall(player, blData, wall);
            return 1;
        }
    }

    _VECTOR reach, dir;
    Player_GetHeadPos(&reach, player, NULL);
    Mat_GetDir(&dir, &glb_viewer[blData->playerNum]->viewMatrix);
    Vec_Add2(&reach, &dir, &reach);
    if (!Collide_SphereIntersect(&reach, 1.0f, player->inCel, player, blData->weaponObject, &hitList, 0, 0, 0, 4))
        return 0;

    for (HITDATA_tag *hit = hitList; hit != NULL; hit = hit->next) {
        obj_tag *obj = hit->hitObj;
        if (obj == NULL)
            continue;
        if (obj->scriptPlayer != NULL && obj->objectType == 0) {
            SP_Activate(obj, player);
            continue;
        }
        switch ((uchar)obj->objectType) {
            case OBJECTTYPE_DOOR:
                used |= (uchar)Door_Activate(obj, player, 1);
                break;
            case OBJECTTYPE_TRIGGER:
                Trigger_Activate(obj, player);
                break;
            case OBJECTTYPE_SIMPLESCRIPT:
                SS_Activate(obj);
                break;
            case OBJECTTYPE_CAR:
                Car_Activate(obj, player);
                break;
            case OBJECTTYPE_GUNTURRET:
                GT_Activate(obj);
                break;
            case OBJECTTYPE_MONITOR:
                Monitor_Activate(obj, player);
                break;
            case OBJECTTYPE_LOCK: {
                // Only from in front of it
                _VECTOR facing, toPlayer;
                Mat_GetDir(&facing, &obj->transformMatrix);
                Vec_Subtract(Mat_Position(player->transformMatrix), Mat_Position(obj->transformMatrix), &toPlayer);
                double dot = (double)toPlayer.z * facing.z + (double)toPlayer.y * facing.y +
                             (double)toPlayer.x * facing.x;
                if (dot >= 0.0)
                    Lock_Activate(obj, player);
                break;
            }
            case OBJECTTYPE_GUNTURRET2:
                GunImp_Activate(obj, player);
                break;
            case OBJECTTYPE_QWORM:
                PCQWorm_Activate(obj);
                break;
        }
    }
    Collide_FreeHitList(&hitList);
    return used;
}

// AUTOLTCG
void __declspec(naked) Player_Activate(void) {
    _asm {
        push edi
        push eax
        call _Player_Activate
        add esp, 8
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Every frame (Player_Update)
// ---------------------------------------------------------------------------------------------------------------

#define Env_Breath ((void (__cdecl *)(_VECTOR *pos, _VECTOR *dir, cel_tag *cel, int seed))0x00068db0)

#define benchmark U8_AT(0x002adf32) // set, the player stands still

// The player's object in ESI, his data in EAX
static void __declspec(naked) Player_Swim(obj_tag *player, BLData *blData) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, [esp + 0xc]
        mov ecx, 0x000aa590
        call ecx
        pop esi
        ret
    }
}

// The player's data in ECX, his object on the stack
static void __declspec(naked) Player_ClampSomeAngles(BLData *blData, obj_tag *player) {
    _asm {
        mov ecx, [esp + 4]
        push dword ptr [esp + 8]
        mov eax, 0x000ab180
        call eax
        add esp, 4
        ret
    }
}

// The player's data in ESI
static void __declspec(naked) Player_UpdateBlurAndFade(BLData *blData) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 0x000ab0b0
        call eax
        pop esi
        ret
    }
}

// The gadgets that recharge, a unit at a time, every so many frames: 0x4a and 0x4e every 4, 0x4c and 0x4f every 2,
// 0x45 every 6 and 0x33 every 4
static void Recharge(BLData *blData, int weapon) {
    short charge = blData->weaponStats[weapon].clipOrCooldown;
    if (charge < weapon_data[weapon].clipSize)
        blData->weaponStats[weapon].clipOrCooldown = (short)Ftol((double)charge + FRAME_RATE_MUL);
}

// The player, every frame: the pause menu, the HUD, then (unless he is waiting to come in, or paused) his
// alpha, the cel's music, the substate's movement, air, aim and the view, the move itself, light and blur,
// collision, the gun and the HUD's fade, and the view's sway.
// AUTOINJECT
void Player_Update(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    if (Input_Action(blData->playerNum, ACTION_PAUSE, 4) && GameState.maybePaused == 0 && ScriptCam == 0) {
        blData->pauseMenu = (short)MenuManager_Create(0x80000002, 0x4000004b, blData->playerNum,
                                                      (uchar)blData->playerNum, ACTION_PAUSE, 1, 3);
        GS_PauseGame(1);
    }
    HUD_Update(blData, player);

    if (blData->active != 1) {
        if (blData->active == 0)
            return;
        blData->active--;
        return;
    }
    if (GS_IsPaused(blData->playerNum))
        return;

    if (blData->alpha < 0.5f)
        player->tweakA = (uchar)Ftol(40.0f);
    else
        player->tweakA = (uchar)Ftol((double)blData->alpha * 255.0f);
    float alpha = moveTowardsValue(blData->alpha, 2.0f, (float)((double)REC_FRAME_RATE * 0.05f));
    blData->alpha = alpha;
    if (alpha < 0.0f)
        alpha = 0.0f;
    else if (alpha > 1.0f)
        alpha = 1.0f;
    blData->alpha = alpha;

    if (player->inCel != NULL && player->inCel->musicEvent != 0)
        Music_Event(player->inCel->musicEvent, 1);
    blData->turnKick = (float)((double)blData->turnKick * 0.9f);

    _VECTOR startPosition;
    Vec_Copy(&player->position, &startPosition);
    switch ((short)player->curState) {
        case 0:
            player->curState = 1;
            break;

        case 1:
            PlarStat_LogUpdateElapsedTime(blData->playerNum);
            Vec_Zero(&blData->movement);
            Vec_Zero(&blData->rotationDelta);
            if (blData->traverseWait != 0)
                blData->traverseWait--;
            if (blData->field_0x8e3 != 0)
                blData->field_0x8e3--;

            // Player 4's action 0x27 turns flying on and off
            if (Input_Action(3, (GameActions_tag)0x27, 4)) {
                debug_scanmode ^= 1;
                if (debug_scanmode) {
                    Player_ChangeSubState(player, MovementType_ScanMode);
                }
                else {
                    _VECTOR dir;
                    Mat_GetDir(&dir, &player->transformMatrix);
                    Vec_Cartesian_2_Spherical(&player->rotation, dir.x, dir.y, dir.z);
                    player->rotation.x = 0.0f;
                    player->rotation.z = 0.0f;
                    Vec_Copy(&player->rotation, &player->lastRotation);
                    Player_ChangeSubState(player, MovementType_Walk);
                    Camera_CalcViewAngles(blData->playerNum, 1.0471976f);
                }
            }
            if (benchmark)
                break;

            Vec_Copy(&player->position, &blData->lastPosition);
            blData->field_0x8cc--;
            if (!blData->frozen) {
                switch ((short)player->subState) {
                    case MovementType_Walk:
                        _Player_SSWalk(player, blData);
                        break;
                    case MovementType_Crouch:
                        _Player_SSCrouch(player, blData);
                        break;
                    case MovementType_Climb:
                        Player_Climb(blData, player);
                        break;
                    case MovementType_Wire:
                        Player_Wire(player);
                        break;
                    case MovementType_Zipline:
                        Player_Zipline(player);
                        break;
                    case MovementType_Creep:
                        Player_Creep(blData, player);
                        break;
                    case MovementType_ZeroG:
                    case MovementType_ZeroG_Anim:
                        _Player_ZeroG(blData, player);
                        break;
                    case MovementType_RemoteControl:
                        // The device lets go when the timer runs out, or straight away without one once the device
                        // has gone
                        if (blData->remoteControlTimer > 0) {
                            blData->remoteControlTimer--;
                            if (blData->remoteControlTimer != 0)
                                break;
                            blData->remoteControlDevice = NULL;
                        }
                        else if (blData->remoteControlDevice != NULL) {
                            break;
                        }
                        Player_SetCamMode(blData, CamMode_Default);
                        glb_viewer[blData->playerNum]->someCel = NULL;
                        Player_ChangeSubState(player, blData->previousSubState);
                        break;
                    case MovementType_Grapple:
                        _Player_SSGrapple(blData, player);
                        break;
                    case MovementType_Swim:
                        Player_Swim(player, blData);
                        break;
                    case MovementType_ScanMode:
                        _Player_SSScanMode(blData, player);
                        break;
                }
            }

            _Player_MonitorAir(player, blData);
            Player_ClampSomeAngles(blData, player);
            Player_Aiming(player);
            Player_ViewClamping(player);

            _VECTOR move;
            ApplyMatrixLV(&player->transformMatrix, &blData->movement, &move);
            Vec_Add2(&player->position, &move, &player->position);
            Vec_Copy(&blData->movement, &blData->lastMovement);
            blData->rotationDelta.y = (float)((double)blData->turnKick + blData->rotationDelta.y);
            Vec_Add2(&player->rotation, &blData->rotationDelta, &player->rotation);
            Vec_Copy(&blData->rotationDelta, &blData->lastRotationDelta);
            break;

        case 2:
        case 3:
            AnimObjectUpdate(player);
            _Player_HandleDeath(player);
            break;
    }

    Player_InShadow(blData, player);
    Player_UpdateBlurAndFade(blData);
    if (blData->field_0x8cc <= 0)
        blData->field_0x8cc = (short)((Rand_Random() & 0x3ff) + 0xff);
    AnimObjectUpdate(player);
    player->animState->animObj.rootTravel = Vec_Dist3D(&player->position, &startPosition);
    Player_Collision(player);

    switch ((short)player->curState) {
        case 1:
        case 4:
            if (blData->frozen)
                break;
            if (blData->camMode != CamMode_Default) {
                _VECTOR dir, head;
                Mat_GetDir(&dir, &player->transformMatrix);
                AnimGetBoneWorldTrans(player, BONE_HEAD, 0, &head, NULL);
                Env_Breath(&head, &dir, player->inCel, player->creationTimeFrames);
            }
            if (blData->weaponObject->curState != 9) {
                uint frames = GameState.NumFramesUnpaused;
                if (!(frames & 3)) {
                    Recharge(blData, 0x4a);
                    Recharge(blData, 0x4e);
                }
                if (!(frames & 1)) {
                    Recharge(blData, 0x4c);
                    Recharge(blData, 0x4f);
                }
                if (frames % 6 == 0)
                    Recharge(blData, 0x45);
                if (!(frames & 3))
                    Recharge(blData, 0x33);
            }
            Player_WeaponRecoil(player);
            Player_Weapon(player);
            Player_WeaponFiring(player, 0);

            // With the HUD option on, the bars fade out (by twice the step a frame, as the original does)
            if (PlayerInputs[blData->playerNum].hudVisibility) {
                double fade = blData->hudFadeIn - (double)FRAME_RATE_MUL * 0.0020833334f;
                blData->hudFadeIn = (float)fade;
                if (fade < 0.0)
                    blData->hudFadeIn = 0.0f;
                else
                    blData->hudFadeIn = (float)(fade - (double)FRAME_RATE_MUL * 0.0020833334f);
            }
            else {
                blData->hudFadeIn = 1.0f;
            }
            break;

        case 2:
        case 3:
            Player_WeaponRecoil(player);
            break;
    }

    Camera_ApplySwing(glb_viewer[blData->playerNum], weapon_data[player->animState->currentWeaponId].scopeSway);
    View_SetDrawInOtherViewsOnly(player, blData->playerNum);
    player->transformFlags |= TRANSFORM_MOVED;
    if (!(player->transformFlags & TRANSFORM_MATRIX_PLACED))
        Vec_Copy(&player->position, Mat_Position(player->transformMatrix));
}
