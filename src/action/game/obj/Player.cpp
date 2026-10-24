#include "Player.h"
#include "Pickup.h"
#include "Lightning.h"
#include "BodyGlow.h"
#include "PlayerWeapon.h"
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
#include "../../math/math.h"

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
void Player_AddNewStartPos(_VECTOR *pos, _VECTOR *rot, ushort enabled, level_tag *lvl) {
    Vec_Copy(pos, &player_start[player_start_positions_index].pos);
    Vec_Copy(rot, &player_start[player_start_positions_index].rot);
    player_start[player_start_positions_index].isEnabled = enabled;
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
            glb_viewer[player->playerNum]->visionMode = VISION_NORMAL;
            break;

        case CamMode_PostMPGameThirdPerson:

            player->endCamera.range = 1.5f;
            player->endCamera.distance = 0.15f;
            player->endCamera.distanceWanted = 1.5f;
            player->endCamera.angle = 0.0f;

            HUD_Reset(player);
            glb_viewer[player->playerNum]->visionMode = VISION_NORMAL;
            break;
        
        case CamMode_Redeemer:
            HUD_Reset(player);
            glb_viewer[player->playerNum]->visionMode = VISION_NORMAL;
            HUD_Enable(player->hudInfo, Redeemer, 1, 0);
            break;
        
        case CamMode_RCCar:
            HUD_Reset(player);
            glb_viewer[player->playerNum]->visionMode = VISION_NORMAL;
            HUD_Enable(player->hudInfo, RCCar, 1, 0);
            break;

        case CamMode_Ronin:
            HUD_Reset(player);
            glb_viewer[player->playerNum]->visionMode = VISION_NORMAL;
            HUD_Enable(player->hudInfo, Ronin, 1, 0);
            break;

    }

}

// What Player_Enable looks for around the player when asked to (the radius, the collision flags).
#define PLAYER_ENABLE_ATTACH_RADIUS 2.0f
#define PLAYER_ENABLE_ATTACH_FLAGS (HITMASK_SKIP_ROOMS | HITMASK_SKIP_CHILD_CELS | HITMASK_SKIP_PASSABLE)

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
    player->effectFlags |= EFFECT_HIDDEN;
    blData->hitTest.type = 0;
    if (blData->weaponObject != NULL)
        blData->weaponObject->effectFlags |= EFFECT_HIDDEN;
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
    player->effectFlags &= ~EFFECT_HIDDEN;
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
        blData->weaponObject->effectFlags &= ~EFFECT_HIDDEN;
    blData->jumpState = JUMP_NONE;
    blData->jumpWait = 0;
    Vec_Zero(&blData->fallVelocity);
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

// The camera mode a script asks for (event 9): 3 is the third-person view (CamMode_ThirdPerson); anything else is the
// first-person view, level, with the player on foot.
// AUTOINJECT
void Player_Cam2Mode(ushort playerNum, uint mode) {
    if (glb_blokes[playerNum] == NULL)
        return;

    if (mode == 3) {
        Player_SetCamMode(glb_blokes[playerNum], CamMode_ThirdPerson);
        return;
    }

    glb_blokes[playerNum]->pitchFromHorizontal = 0.0f;
    Player_SetCamMode(glb_blokes[playerNum], CamMode_Default);
    Player_ChangeSubState(glb_players[playerNum], MovementType_Walk);
}

// An explosion's damage, as pain of no kind in no place; the health left
// AUTOINJECT
float Player_Hurt(obj_tag *player, float damage, _VECTOR *from) {
    BLData *blData = (BLData *)player->extraObjectData;
    Player_HandlePain(player, damage, blData, -1, 0);
    return blData->health;
}

// AUTOINJECT
void Player_ChangeState(obj_tag* obj, unsigned short newState) { 
    obj->curState = newState;
}

// AUTOINJECT
void Player_CheckWeaponsLoaded(BLData *blData) {

    for(int i = 0; i < NUM_WEAPONS; i++) { // weapon_data has one entry more than the weapon ids
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

// Gives the player ammo for a weapon: fills an empty gun first, for weapons with WPNFLAG_SWITCH_TO_ALT, and puts the
// rest in the carried stock up to the ammo type's maximum. Returns how much was taken, loaded plus stocked (only the
// low 16 bits mean anything - the original leaves the rest of EAX as it was). Nothing is taken when the stock is
// already full, even if the gun is empty.
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
    if (playerInfo->ammo[weapon->ammoType] >= ammo_data[weapon->ammoType].maxNumPerPlayer)
        return 0;
    if (amount == 0)
        return 0;

    short loaded = 0;
    if ((weapon->weaponFlags & WPNFLAG_SWITCH_TO_ALT) &&
        playerInfo->weaponStats[Player_AmmoIndex(weaponIndex)].clipOrCooldown == 0) {
        loaded = (amount < weapon->clipSize) ? amount : weapon->clipSize;
        playerInfo->weaponStats[Player_AmmoIndex(weaponIndex)].clipOrCooldown = loaded;
        amount -= weapon->clipSize;
        if (amount <= 0)
            return (ushort)loaded;
    }

    short before = playerInfo->ammo[weapon->ammoType];
    playerInfo->ammo[weapon->ammoType] = before + amount;
    short max = ammo_data[weapon->ammoType].maxNumPerPlayer;
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
    sightObj->effectFlags |= EFFECT_NO_COLLISION;
    sightObj->parent = playerObj;
    sightObj->tweakR = 0x7f;
    sightObj->tweakG = 0;
    sightObj->tweakB = 0;
    hashtable_set_object_to_entity_gfx(sightObj, HT_Entity_gunsight3d);
    View_SetDrawInThisViewOnly(sightObj, viewerNum);
    blData->sightObj = sightObj;
  
}

// AUTOINJECT
void Player_CreateMuzzleFlash(obj_tag *playerObj, byte viewerNum) {
  
    obj_tag* muzzleFlashObj = control_create_object(0,NULL, NULL, NULL);
    BLData *blData = (BLData *)playerObj->extraObjectData;
    
    if (muzzleFlashObj == NULL) 
        return;
    
    muzzleFlashObj->parent = playerObj;
    muzzleFlashObj->objectType = OBJECTTYPE_DELETED;
    muzzleFlashObj->effectFlags |= EFFECT_BACK_TO_FRONT | EFFECT_NO_COLLISION | EFFECT_BIT9 | EFFECT_STRADDLES_CELS;
    View_SetDrawInThisViewOnly(muzzleFlashObj,viewerNum);
    blData->muzzleFlashObj = muzzleFlashObj;
    blData->muzzleFlashFrames = 0;
}

// Only used for laser and taser beams
// AUTOINJECT
void PositionBeam(obj_tag *beam, _VECTOR *start, _VECTOR *end) {

    Vec_Copy(start, &beam->position);

    beam->transformFlags &= ~TRANSFORM_MOVED;

    float distance = Vec_Dist3D(start, end);

    beam->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY;
    beam->scale = distance;

    Vec_Copy(start, &beam->centrePoint);
    beam->transformFlags |= TRANSFORM_MOVED;
    beam->radius = 2.0f * distance;

    float dx = start->x - end->x;
    float dy = start->y - end->y;
    float dz = start->z - end->z;

    vecutil_cartesian_to_spherical_acc(&beam->rotation, dx, dy, dz);

    beam->rotation.x *= -1;
    beam->rotation.z = 0.0f;

}

// AUTOINJECT
void Player_SetupLaser(BLData *blData, _VECTOR *targetPos) {
  
    _VECTOR sourcePos;

    AnimGetBoneWorldTrans(blData->weaponObject, 0, 0, &sourcePos, (_MATRIX *)0x0);
    
    obj_tag* beam = blData->beamObjs[0];

    beam->tweakA = 0xff;
    
    // Strong or weak beam ("lazer" 0x2000131 or "uv_lazer" 0x20006c1)
    HASHCODE hc = (glb_players[blData->playerNum]->animState->currentWeaponId == 0x4e)
        ? HT_Entity_lazer : HT_Entity_uv_lazer;

    hashtable_set_object_to_entity_gfx(beam, hc);
    View_SetDrawInAllViews(beam);
    PositionBeam(beam, &sourcePos, targetPos);

}

#define WEAPON_TASER 0x4c

// The taser's bolt: from the gun's datum 0 to the target, pulled in to the taser's range, as a jagged line of a
// segment per 1.25 units (2 to 32), each segment a beam drawn in every view at a random brightness.
// AUTOINJECT
void Player_SetupTaser(BLData *blData, _VECTOR *target) {
    _VECTOR start;
    AnimGetBoneWorldTrans(blData->weaponObject, 0, 0, &start, NULL);
    blData->beamObjs[0]->position = start;

    float range = weapon_data[WEAPON_TASER].range;
    float distance = Vec_Dist3D(&start, target);
    if (distance > range) {
        _VECTOR towards;
        Vec_Subtract(target, &start, &towards);
        Vec_Scalef(&towards, range / distance, &towards);
        Vec_Add2(&start, &towards, target);
        distance = range;
    }
    short segments = (short)Ftol((double)distance * 0.8f);
    if (segments < 2)
        segments = 2;
    else if (segments > 32)
        segments = 32;

    FractalThing(blData->beamObjs, 0, segments, &start, target, 1);
    for (short i = 0; i < segments - 1; i++) {
        obj_tag *beam = blData->beamObjs[i];
        hashtable_set_object_to_entity_gfx(beam, HT_Entity_taser);
        View_SetDrawInAllViews(beam);
        PositionBeam(beam, &beam->position, &blData->beamObjs[i + 1]->position);
        beam->tweakA = (uchar)(Rand_Rand(0x80) + 0x7f);
    }
    obj_tag *last = blData->beamObjs[segments - 1];
    hashtable_set_object_to_entity_gfx(last, HT_Entity_taser);
    View_SetDrawInAllViews(last);
    PositionBeam(last, &last->position, target);
    last->tweakA = (uchar)(Rand_Rand(0x80) + 0x7f);
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
    obj->animState->switchingToWeaponId = Weap_Hands;
    obj->animState->currentWeaponId = Weap_Hands;
    
    blData->weaponObject->curState = GUN_IDLE;
    blData->muzzleFlashFrames = 0;
    blData->zoom = 1.0f;

    obj->animState->animFlags &= ~ANIM_SCOPED;

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
        MouseLook_SetScoped((player->animState->animFlags & ANIM_SCOPED) != 0);
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
            delta.x = -(pitchFraction * HALF_PI); // back into radians, which is what RotMatrix wants
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
            blData->aimAutoLevelState = AIMLEVEL_AIMING;
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

// False while dying or dead (PLAYERSTATE_DYING to PLAYERSTATE_4), and in the substates that move the player
// themselves (climbing, wire, creeping, zipline). Not named on PS2; the name is ours.
// FUNC_AT(0x000a9930)
bool PlayerMoveAllowed(obj_tag *player) {
    short state = (short)player->curState;
    if (state >= PLAYERSTATE_DYING && state <= PLAYERSTATE_4)
        return false;
    switch ((short)player->subState) {
        case MovementType_Climb:
        case MovementType_Wire:
        case MovementType_Creep:
        case MovementType_Zipline:
            return false;
        default:
            return true;
    }
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

    float forward = Input_Actionf(blData->playerNum, ACTION_WALK_F_B, ACTION_HELD);
    float strafe  = Input_Actionf(blData->playerNum, ACTION_WALK_L_R, ACTION_HELD);
    float turn    = -Input_Actionf(blData->playerNum, ACTION_AIM_L_R, ACTION_HELD);

    char weapon = player->animState->currentWeaponId;
    // the two grapples, reeling in
    if (((weapon == 0x50) || (weapon == 0x51)) && (blData->weaponObject->curState == GUN_FIRING)) {
        forward = 0.0f;
        strafe = 0.0f;
        turn = 0.0f;
    }
    if (blData->movementDisabled != '\0') {
        forward = 0.0f;
        strafe = 0.0f;
        turn = 0.0f;
    }
    if ((player->animState->animFlags & ANIM_SCOPED) != 0) {
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

    // In CastleIndoors1, until SWITCH_CASTLEINDOORS1_ARMED is on, the player moves at 0.9 speed
    // The scaled speed stays unrounded on the x87
    double speed = speedScale;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 &&
        switch_channels[SWITCH_CASTLEINDOORS1_ARMED] == 0) {
        speed = (double)speedScale * 0.9f;
    }
    float moveZ = (float)(forwards * speed);
    float moveX = (float)(sideways * speed);

    double accel = AccelFunc0(turn, &blData->turnAccelState, Plr_NoAimTurnSpeed_X,
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
    RotMatrixX((float)(player->rotation.x - (double)blData->pitchFromHorizontal * HALF_PI), &pitched);
    Mat_GetDir(&direction, &pitched);
    auxVec_AddMulR32(&blData->movement, &direction, moveZ, &blData->movement);
    blData->movement.x = moveX;
    blData->rotationDelta.y = yaw;
}

// AnimScriptAdd, with the script's playback speed set (sAnimScript_tag +0x94) when it starts.
// AnimScriptAdd, played backwards: raises the "reverse" flag at 0x001d7828 and tail-calls AnimScriptAdd.
// Leaving the scope: the zoom back to 1x, the view's auto-levelling off, the walk anim reset and the zoom motor's
// sound stopped.
// AUTOINJECT
void Player_ScopeOff(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    blData->aimAutoLevelState = AIMLEVEL_AIMING;
    blData->zoomTarget = 1.0f;
    Player_ResetWalkAnim(player);
    if (blData->zoomSound != NULL) {
        Sound_Stop(blData->zoomSound);
        blData->zoomSound = NULL;
    }
}

// The zoom for the held weapon: 1x, or with the scope up and the player's "remember zoom" option on, the zoom
// its base weapon was left at; never past the weapon's maxZoom.
// AUTOINJECT
void Player_ResetZoom(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    int weapon = player->animState->currentWeaponId;
    blData->zoomTarget = 1.0f;
    if (PlayerInputs[blData->playerNum].rememberZoom && (player->animState->animFlags & ANIM_SCOPED) != 0)
        blData->zoomTarget = blData->weaponStats[(ushort)weapon_data[weapon].weaponBaseNum].zoom;
    blData->zoomMax = weapon_data[weapon].maxZoom;
    if (blData->zoomMax < blData->zoomTarget)
        blData->zoomTarget = blData->zoomMax;
}

static_assert(offsetof(weapon_definition_tag, offsetToNextAltFireVariant) == 0x05, "offsetToNextAltFireVariant");
static_assert(offsetof(weapon_definition_tag, numFireModes) == 0x2e, "numFireModes");
static_assert(offsetof(weapon_definition_tag, weaponFlags) == 0x68, "weaponFlags");
static_assert(offsetof(weapon_definition_tag, animScopeIn) == 0xb8, "animScopeIn");
static_assert(offsetof(weapon_definition_tag, animScopeOut) == 0xbc, "animScopeOut");

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
        char wasScoped = anim->animFlags & ANIM_SCOPED;

        if (PlayerInputs[playerNum].manualAimToggle == 0) {
            // Hold to aim: scoped exactly while the button is down
            anim->animFlags &= ~ANIM_SCOPED;
            if (Input_Action(playerNum, ACTION_AIM_ZOOM_SCOPE, ACTION_HELD)) {
                player->animState->animFlags |= ANIM_SCOPED;
            }
        }
        else {
            // Toggle: flip on each press
            if (Input_Action(playerNum, ACTION_AIM_ZOOM_SCOPE, ACTION_PRESSED)) {
                player->animState->animFlags ^= ANIM_SCOPED;
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
                forceDown = (flags & WPNFLAG_REAL_SCOPE) != 0;
                break;
            case 9:
                forceDown = (flags & WPNFLAG_REAL_SCOPE) && (flags & WPNFLAG_SINGLE_ACTION) && !wasScoped;
                break;
        }
        if (forceDown) {
            player->animState->animFlags &= ~ANIM_SCOPED;
        }

        AnimState *animNow = player->animState;
        char isScoped = animNow->animFlags & ANIM_SCOPED;
        if ((isScoped != wasScoped) && (weapon_data[weaponId].animScopeIn != 0)) {
            if (isScoped) {
                // Just asked for the scope: hold the bit off and play the bring-to-eye animation first
                animNow->animFlags &= ~ANIM_SCOPED;
                blData->weaponObject->curState = GUN_SCOPE_RAISE;
                AnimScriptAddSpeed(blData->weaponObject, weapon_data[weaponId].animScopeIn, 1.25f);
            }
            else if (blData->weaponObject->curState == GUN_IDLE) {
                // Lowering. The upgraded camera drops back to the plain camera as it comes down.
                blData->weaponObject->curState = GUN_SCOPE_LOWER;
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
    if ((animFlags & ANIM_SCOPED) != ((animFlags >> 1) & 1)) {
        anim2->animFlags = animFlags ^ ANIM_SCOPE_APPLIED;
        if (animFlags & ANIM_SCOPED) {
            Player_ResetZoom(player);
            blData->aimAutoLevelState = AIMLEVEL_OFF;
            Player_ClearAimInertia(player); // inlined in the original
        }
        else {
            Player_ScopeOff(player);
        }
    }

    // Alt-fire. The camera variants also take the fire button as alt-fire. Only the low byte of each result
    // is kept (MOV BL,AL / OR BL,AL), as in the original.
    uchar altFire = (uchar)Input_Action(playerNum, ACTION_ALTFIRE, ACTION_PRESSED);
    if ((weapon->weaponVariantNum == Weap_Camera) || (weapon->weaponVariantNum == Weap_Camera_Upgraded)) {
        altFire |= (uchar)Input_Action(playerNum, ACTION_FIRE, ACTION_PRESSED);
    }
    AnimState *anim3 = player->animState;
    bool altFireBlocked = (anim3->animFlags & ANIM_SCOPED) &&
                          (weapon_data[anim3->currentWeaponId].weaponFlags & WPNFLAG_REAL_SCOPE);
    if (!altFireBlocked && altFire && (blData->weaponObject->curState == GUN_IDLE)) {
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

    if (Input_Action(playerNum, ACTION_GADGET_NEXT, ACTION_PRESSED)) {
        Player_WeaponChange(player, 1, WEAPONCHANGE_GADGETS);
    }
    else if (Input_Action(playerNum, ACTION_GADGET_PREV, ACTION_PRESSED)) {
        Player_WeaponChange(player, -1, WEAPONCHANGE_GADGETS);
    }

    // CastleIndoors1: until SWITCH_CASTLEINDOORS1_ARMED is on, weapon cycling is restricted; on the frame it goes
    // on the player is handed his (upgraded) P2K. Upgrade_Weapon(6)
    // is exactly the original's inlined UpgradedHandguns[Upgrades[0][WUG_Handgun]], truncated to a byte.
    char changeMode = WEAPONCHANGE_WEAPONS;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1) {
        if (switch_channels[SWITCH_CASTLEINDOORS1_ARMED] == 0) {
            changeMode = WEAPONCHANGE_UNARMED;
        }
        else if (switch_channels_prev[SWITCH_CASTLEINDOORS1_ARMED] == 0) {
            player->animState->switchingToWeaponId = (char)Upgrade_Weapon(6);
        }
    }
    if (Input_Action(playerNum, ACTION_WEAPON_NEXT, ACTION_PRESSED)) {
        Player_WeaponChange(player, 1, changeMode);
    }
    else if (Input_Action(playerNum, ACTION_WEAPON_PREV, ACTION_PRESSED)) {
        Player_WeaponChange(player, -1, changeMode);
    }

    Player_Zoom(player);
    Player_AutoAim(player);
}

// How far away autoaim will snap to a target, in its two bands. Player_InitWeapon sets them per game mode
// and difficulty; the XBE's initial values (12, 18) are the same as the normal-difficulty ones.
// XBE_GLOBAL(0x00181a40, 0x4)
static float MAX_AUTO_AIM_DIST_NEAR = 12.0f;
// XBE_GLOBAL(0x00181a44, 0x4)
static float MAX_AUTO_AIM_DIST_MIDL = 18.0f;
// The two hint flags the PS2 build calls CamHint and StickyHint; cleared at the start of every level.
// XBE_GLOBAL(0x00279145, 0x1)
uchar CamHint;
// XBE_GLOBAL(0x00279144, 0x1)
uchar StickyHint;

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
        if (GameState.difficultyModifier == DIFF_AGENT) {
            MAX_AUTO_AIM_DIST_NEAR = 8.0f;
            MAX_AUTO_AIM_DIST_MIDL = 14.0f;
        } else if (GameState.difficultyModifier == DIFF_00AGENT) {
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
    blData->weaponObject = WatchOrHands_Create(player);

    blData->swayPhase = 0.0f;
    blData->recoilX = 0.0f;
    blData->recoilY = 0.0f;
    blData->idleSwayPhase = 0.0f;
    blData->zoomSound = NULL;
    blData->refireTimer = 0.0f;
    blData->burstShotsLeft = 0;
    blData->field_0x8e6 = 0;
    blData->twinGunHand = 0;

    Player_CreateBeams(player, blData->playerNum);
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

    player->animState->currentWeaponId = Weap_Hands;
    player->animState->prevHeldWeaponId = Weap_Hands;
    blData->aimAutoLevelState = AIMLEVEL_EASED;

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
        case HT_Level_000017:
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
        case HT_Level_SpaceStationA:
        case HT_Level_SpaceStationB:
        case HT_Level_SpaceStationC:
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
// XBE_GLOBAL(0x00181a48, 0x4)
float Plr_AimSpeed_X = 0.15f;
// XBE_GLOBAL(0x00181a4c, 0x4)
float Plr_AimSpeed_Y = 0.16f;
// XBE_GLOBAL(0x00181a50, 0x4)
float Plr_AimTurnSpeed_X = 0.05f;
// XBE_GLOBAL(0x00181a54, 0x4)
float Plr_AimTurnSpeed_Y = 0.06f;
// XBE_GLOBAL(0x00181a58, 0x4)
float Plr_ScopeSpeed_X = 0.015f;
// XBE_GLOBAL(0x00181a5c, 0x4)
float Plr_ScopeSpeed_X_Mul = 3.0f;
// XBE_GLOBAL(0x00181a60, 0x4)
float Plr_ScopeSpeed_X_Steps = 120.0f;
// XBE_GLOBAL(0x00181a64, 0x4)
float Plr_ScopeSpeed_Y = 0.011f;
// XBE_GLOBAL(0x00181a68, 0x4)
float Plr_ScopeSpeed_Y_Mul = 3.0f;
// XBE_GLOBAL(0x00181a6c, 0x4)
float Plr_ScopeSpeed_Y_Steps = 120.0f;
// XBE_GLOBAL(0x00181a7c, 0x4)
float Plr_NoAimTurnSpeed_Y = 0.01f;
// XBE_GLOBAL(0x00181a80, 0x4)
float Plr_NoAimTurnSpeed_Y_Mul = 2.0f;
// XBE_GLOBAL(0x00181a84, 0x4)
float Plr_NoAimTurnSpeed_Y_Steps = 120.0f;

// Health given back on continuing a mission, per difficulty (read by Player_RamLoad)
// XBE_GLOBAL(0x0017e680, 0x4)
float ContinueHealthBoostEasy = 50.0f;
// XBE_GLOBAL(0x0017e684, 0x4)
float ContinueHealthBoostMedium = 50.0f;
// XBE_GLOBAL(0x0017e688, 0x4)
float ContinueHealthBoostHard = 50.0f;

// Autoaim (read by Check_AutoAim, and Autoaim_Range by Player_AutoAim). Autoaim_HardMul is in .bss, so it is
// 0 until the first level loads anyway.
// XBE_GLOBAL(0x00181afc, 0x4)
float Autoaim_Angle_H = 0.17453f;
// XBE_GLOBAL(0x00181b00, 0x4)
float Autoaim_Angle_V = 0.17453f;
// XBE_GLOBAL(0x00181b04, 0x4)
float Autoaim_Range = 35.0f;
// XBE_GLOBAL(0x00181b08, 0x4)
float Autoaim_LockOnMul = 2.0f;
// XBE_GLOBAL(0x00181b0c, 0x4)
float Autoaim_EasyMul = 1.25f;
// XBE_GLOBAL(0x00181b10, 0x4)
float Autoaim_NormalMul = 1.0f;
// XBE_GLOBAL(0x0027915c, 0x4)
float Autoaim_HardMul;

// Damage the player takes, per difficulty and per body part (read by Player_HandlePain; the body-part ones also by
// BOT_handlePain; all editable in the P_TWEAKS debug menu)
extern float Plr_DMod_Multi;
extern float Plr_DMod_Head;
extern float Plr_DMod_LowerLimb;
extern float Plr_DMod_UpperLimb;
// XBE_GLOBAL(0x0017e8dc, 0x4)
float Plr_DMod_Easy = 0.75f;
// XBE_GLOBAL(0x0017e8e0, 0x4)
float Plr_DMod_Normal = 1.0f;
// XBE_GLOBAL(0x0017e8e4, 0x4)
float Plr_DMod_Hard = 1.25f;

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

// XBE_GLOBAL(0x00276760, 0x1)
uint8_t surfaced;  // non-zero while swimming at the surface
extern float debug_cam_posy;  // height of camera mode 8's overhead view above the player

// AUTOINJECT
void Player_PositionCamera(obj_tag *player) {

    BLData *blData = (BLData*)player->extraObjectData;
    char playerNum = blData->playerNum;
    viewer_tag *viewer = glb_viewer[playerNum];

    if (!GS_IsPaused(-1)) {
        switch ((short)player->subState) {
            case MovementType_Crouch: {
                // On the x87: the sum is stored, then compared unrounded
                Extended offset = Extended(FRAME_RATE_MUL) * Extended(0.0225f) + Extended(blData->headCrouchOffset);
                blData->headCrouchOffset = (float)offset;
                if (offset > Extended(0.45f))
                    blData->headCrouchOffset = 0.45f;
                break;
            }
            case MovementType_RemoteControl:
            case MovementType_RCVehicle:
            case MovementType_Emplacement:
            case MovementType_Ronin:
                break;
            default: {
                Extended offset = Extended(blData->headCrouchOffset) - Extended(FRAME_RATE_MUL) * Extended(0.0225f);
                blData->headCrouchOffset = (float)offset;
                if (offset < Extended(0.0f))
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
            bool realScope = (player->animState->animFlags & ANIM_SCOPED) && (weapon->weaponFlags & WPNFLAG_REAL_SCOPE);
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

            if (player->curState == PLAYERSTATE_DYING || player->curState == PLAYERSTATE_3)
                blData->headCrouchOffset = 0.0f;

            Camera_SetToPlayer(player, viewer);
            return;
        }

        case CamMode_PostMPGameThirdPerson:
            viewer->zoom = 1.0f;
            blData->endCamera.subject = player;
            AnimGetBoneWorldTrans(player, ANIM_BONE(BONE_HEAD), 0, &blData->endCamera.target, NULL);
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_LookAtFrom(viewer, &blData->endCamera);
            viewer->cel = NULL;
            return;

        case CamMode_ThirdPerson:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_PlaceBehind(&player->position, &player->rotation, viewer, 1.0f, 10.0f);
            Camera_PlaceOver(player, viewer, 1.0f, 0.0f);
            Camera_PlaceAround(&player->position, viewer, 1.0f);
            Camera_PlaceAbove(&player->position, viewer, 1.0f);
            viewer->zoom = 1.0f;
            viewer->cel = NULL;
            return;

        case 3:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_Orbit(player, viewer);
            viewer->zoom = 1.0f;
            viewer->cel = NULL;
            return;

        case 4:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_LookAtObject(player, viewer);
            viewer->zoom = 1.0f;
            return;

        case 5:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_Trail(player, viewer);
            viewer->zoom = 1.0f;
            viewer->cel = NULL;
            return;

        case 6:
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            Camera_Follow(player, viewer);
            viewer->zoom = 1.0f;
            viewer->cel = NULL;
            return;

        case 8: // looking straight down on the player from debug_cam_posy above
            View_SetDrawInAllViews(player);
            View_SetDrawInNoViews(blData->weaponObject);
            viewer->pos.x = player->position.x;
            viewer->pos.z = player->position.z;
            viewer->rot.y = 0.0f;
            viewer->pos.y = (float)((double)debug_cam_posy + player->position.y + 2.0f);
            viewer->rot.x = HALF_PI;
            viewer->rot.z = 0.0f;
            viewer->zoom = 1.0f;
            Camera_Set(viewer, &viewer->pos, &viewer->rot);
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
            viewer->rot = device->rotation;
            viewer->zoom = 1.0f;
            Camera_Set(viewer, &viewer->pos, &viewer->rot);
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
            viewer->cel = NULL;
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
            if (model == hashtable_hashcode_to_celglist(HT_Entity_LittleNellie)) {
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

extern uint8_t debug_scanmode;  // the player starts every level flying (MovementType_ScanMode)
// XBE_GLOBAL(0x001f6648, 0xc) is GRAVITY_VECTOR: in zero G its y is 0

void Player_HandleJump(BLData *blData, obj_tag *player, _VECTOR *gravity);

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

// AUTOINJECT
void Player_Kill(obj_tag *player) {
    Player_SetHealth((BLData *)player->extraObjectData, 0.0f);
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
// XBE_GLOBAL(0x0017e68c, 0x48)
static HASHCODE PlayerAnimSetSinglePlayer[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    // The anims Player_Init starts with
    HT_Script_AnimStart, HT_Script_AnimStart, HT_Script_AnimStart, (HASHCODE)0, HT_Script_AnimStart,
    HT_Script_AnimStart, HT_Script_AnimStart, HT_Script_AnimStart, (HASHCODE)0, HT_Script_AnimStartCrouch,
    HT_Script_AnimStartCrouch, HT_Script_AnimStartCrouch, (HASHCODE)0, HT_Script_AnimStartCrouch,
    HT_Script_AnimStartCrouch, HT_Script_AnimStartCrouch, HT_Script_AnimStartCrouch,
};
// XBE_GLOBAL(0x0017e6d4, 0x48)
static HASHCODE PlayerAnimSetClass3[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    HT_Script_PlayerRifle_Idle, HT_Script_PlayerRifle_Walk, HT_Script_PlayerRifle_Run, (HASHCODE)0,
    HT_Script_PlayerRifle_Strafe1, HT_Script_PlayerRifle_Strafe1, HT_Script_PlayerRifle_Strafe2,
    HT_Script_PlayerRifle_Strafe2, (HASHCODE)0, HT_Script_PlayerRifle_CrouchIdle, HT_Script_PlayerRifle_CrouchWalk,
    HT_Script_PlayerRifle_CrouchWalk, (HASHCODE)0, HT_Script_PlayerRifle_CrouchStrafe1,
    HT_Script_PlayerRifle_CrouchStrafe1, HT_Script_PlayerRifle_CrouchStrafe2, HT_Script_PlayerRifle_CrouchStrafe2,
};
// XBE_GLOBAL(0x0017e71c, 0x48)
static HASHCODE PlayerAnimSetDefault[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    HT_Script_PlayerUnarmed_Idle, HT_Script_PlayerUnarmed_Walk, HT_Script_PlayerUnarmed_Run, (HASHCODE)0,
    HT_Script_PlayerUnarmed_Strafe1, HT_Script_PlayerUnarmed_Strafe1, HT_Script_PlayerUnarmed_Strafe2,
    HT_Script_PlayerUnarmed_Strafe2, (HASHCODE)0, HT_Script_PlayerUnarmed_CrouchIdle,
    HT_Script_PlayerUnarmed_CrouchWalk, HT_Script_PlayerUnarmed_CrouchWalk, (HASHCODE)0,
    HT_Script_PlayerUnarmed_CrouchStrafe1, HT_Script_PlayerUnarmed_CrouchStrafe1, HT_Script_PlayerUnarmed_CrouchStrafe2,
    HT_Script_PlayerUnarmed_CrouchStrafe2,
};
// XBE_GLOBAL(0x0017e764, 0x48)
static HASHCODE PlayerAnimSetClass1[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    HT_Script_PlayerHandgun_Idle, HT_Script_PlayerHandgun_Walk, HT_Script_PlayerHandgun_Run, (HASHCODE)0,
    HT_Script_PlayerHandgun_Strafe1, HT_Script_PlayerHandgun_Strafe1, HT_Script_PlayerHandgun_Strafe2,
    HT_Script_PlayerHandgun_Strafe2, (HASHCODE)0, HT_Script_PlayerHandgun_CrouchIdle,
    HT_Script_PlayerHandgun_CrouchWalk, HT_Script_PlayerHandgun_CrouchWalk, (HASHCODE)0,
    HT_Script_PlayerHandgun_CrouchStrafe1, HT_Script_PlayerHandgun_CrouchStrafe1, HT_Script_PlayerHandgun_CrouchStrafe2,
    HT_Script_PlayerHandgun_CrouchStrafe2,
};
// XBE_GLOBAL(0x0017e7ac, 0x48)
static HASHCODE PlayerAnimSetClass5[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    HT_Script_PlayerLauncher_Idle, HT_Script_PlayerLauncher_Walk, HT_Script_PlayerLauncher_Run, (HASHCODE)0,
    HT_Script_PlayerLauncher_Strafe1, HT_Script_PlayerLauncher_Strafe1, HT_Script_PlayerLauncher_Strafe2,
    HT_Script_PlayerLauncher_Strafe2, (HASHCODE)0, HT_Script_PlayerLauncher_CrouchIdle,
    HT_Script_PlayerLauncher_CrouchWalk, HT_Script_PlayerLauncher_CrouchWalk, (HASHCODE)0,
    HT_Script_PlayerLauncher_CrouchStrafe1, HT_Script_PlayerLauncher_CrouchStrafe1,
    HT_Script_PlayerLauncher_CrouchStrafe2, HT_Script_PlayerLauncher_CrouchStrafe2,
};
// XBE_GLOBAL(0x0017e7f4, 0x48)
static HASHCODE PlayerAnimSetClass4[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    HT_Script_PlayerTwoHandedHandgun_Idle, HT_Script_PlayerTwoHandedHandgun_Walk, HT_Script_PlayerTwoHandedHandgun_Run,
    (HASHCODE)0, HT_Script_PlayerTwoHandedHandgun_Strafe1, HT_Script_PlayerTwoHandedHandgun_Strafe1,
    HT_Script_PlayerTwoHandedHandgun_Strafe2, HT_Script_PlayerTwoHandedHandgun_Strafe2, (HASHCODE)0,
    HT_Script_PlayerTwoHandedHandgun_CrouchIdle, HT_Script_PlayerTwoHandedHandgun_CrouchWalk,
    HT_Script_PlayerTwoHandedHandgun_CrouchWalk, (HASHCODE)0, HT_Script_PlayerTwoHandedHandgun_CrouchStrafe1,
    HT_Script_PlayerTwoHandedHandgun_CrouchStrafe1, HT_Script_PlayerTwoHandedHandgun_CrouchStrafe2,
    HT_Script_PlayerTwoHandedHandgun_CrouchStrafe2,
};
// XBE_GLOBAL(0x0017e83c, 0x48)
static HASHCODE PlayerAnimSetClass6[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    HT_Script_PlayerTwinHandguns_Idle, HT_Script_PlayerTwinHandguns_Walk, HT_Script_PlayerTwinHandguns_Run, (HASHCODE)0,
    HT_Script_PlayerTwinHandguns_Strafe1, HT_Script_PlayerTwinHandguns_Strafe1, HT_Script_PlayerTwinHandguns_Strafe2,
    HT_Script_PlayerTwinHandguns_Strafe2, (HASHCODE)0, HT_Script_PlayerTwinHandguns_CrouchIdle,
    HT_Script_PlayerTwinHandguns_CrouchWalk, HT_Script_PlayerTwinHandguns_CrouchWalk, (HASHCODE)0,
    HT_Script_PlayerTwinHandguns_CrouchStrafe1, HT_Script_PlayerTwinHandguns_CrouchStrafe1,
    HT_Script_PlayerTwinHandguns_CrouchStrafe2, HT_Script_PlayerTwinHandguns_CrouchStrafe2,
};
// XBE_GLOBAL(0x0017e884, 0x48)
static HASHCODE PlayerAnimSetClass2[2 * PLAYER_ANIMSET_LIST_SIZE] = {
    HT_Script_PlayerSMG_Idle, HT_Script_PlayerSMG_Walk, HT_Script_PlayerSMG_Run, (HASHCODE)0,
    HT_Script_PlayerSMG_Strafe1, HT_Script_PlayerSMG_Strafe1, HT_Script_PlayerSMG_Strafe2, HT_Script_PlayerSMG_Strafe2,
    (HASHCODE)0, HT_Script_PlayerSMG_CrouchIdle, HT_Script_PlayerSMG_CrouchWalk, HT_Script_PlayerSMG_CrouchWalk,
    (HASHCODE)0, HT_Script_PlayerSMG_CrouchStrafe1, HT_Script_PlayerSMG_CrouchStrafe1,
    HT_Script_PlayerSMG_CrouchStrafe2, HT_Script_PlayerSMG_CrouchStrafe2,
};
static HASHCODE *const PlayerAnimSetByClass[6] = {
    PlayerAnimSetClass1, PlayerAnimSetClass2, PlayerAnimSetClass3,
    PlayerAnimSetClass4, PlayerAnimSetClass5, PlayerAnimSetClass6,
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
    HT_Script_PlayerHandgun_CrouchToStand, HT_Script_PlayerSMG_CrouchToStand, HT_Script_PlayerRifle_CrouchToStand,
    HT_Script_PlayerTwoHandedHandgun_CrouchToStand, HT_Script_PlayerLauncher_CrouchToStand,
    HT_Script_PlayerTwinHandguns_CrouchToStand, HT_Script_PlayerUnarmed_CrouchToStand,
};
static const HASHCODE PlayerStand2CrouchScripts[7] = {
    HT_Script_PlayerHandgun_StandToCrouch, HT_Script_PlayerSMG_StandToCrouch, HT_Script_PlayerRifle_StandToCrouch,
    HT_Script_PlayerTwoHandedHandgun_StandToCrouch, HT_Script_PlayerLauncher_StandToCrouch,
    HT_Script_PlayerTwinHandguns_StandToCrouch, HT_Script_PlayerUnarmed_StandToCrouch,
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
        AnimScriptAdd(player, HT_Script_PlayerDeadZeroG);
    else
        AnimScriptAdd(player, HT_Script_PlayerDead);
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
    player->effectFlags &= ~EFFECT_HIDDEN;

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
        Sound_StopAllWithId(SFX_GADGET_WATCH_LASER_SHOOT);
        Sound_StopAllWithId(SFX_GADGET_WATCH_TASER_SHOOT);
        Sound_StopAllWithId(SFX_GADGET_WATCH_LASER_BURN_LOOP);
    }
    blData->field_0x8ea = 0;
    return oldState;
}

// Whether the player swims: in a cel with water above the feet (0.3 below the position), and not dying. Going in
// changes to the swimming substate and clears the movement. The player's object in ESI, his data in EDI.
int Player_InWater(obj_tag *player, BLData *blData) {
    cel_tag *cel = player->inCel;
    if (cel == NULL)
        return 0;

    if ((double)player->position.y - 0.3f < cel->shearHeight) {
        player->animState->inWater |= 1;
        if (MPSettings.isMultiplayer == 0 && player->curState != PLAYERSTATE_DYING) {
            Player_ChangeSubState(player, MovementType_Swim);
            blData->jumpState = JUMP_NONE;
            blData->traverseWait = (char)Ftol((double)FRAME_RATE_DIV * 5.0f);
            blData->aimAutoLevelState = AIMLEVEL_EASING;
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
void Player_HandlePain(obj_tag *player, float damage, BLData *blData, short hitLocation, ushort painType) {
    if (switch_channels[SWITCH_PLAYER_NO_PAIN] != 0)
        return;
    if (blData->health <= 0.0f)
        return;
    if (damage <= 0.0f)
        return;
    if (MPSettings.dronesAreBots != 0 && GameFlow_GetState() != GAMEFLOW_GAME)
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
            case DIFF_OPERATIVE:
                hurt *= Plr_DMod_Easy;
                break;
            case DIFF_00AGENT:
                hurt *= Plr_DMod_Hard;
                break;
            case DIFF_4:
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
            if (MPSettings.dronesAreBots != 0)
                MPGame.players[blData->playerNum].idxOfLastInjurer = -2;
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
            Sound_Play3D(SFX_CHR_BOND_PAIN_01, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
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

// True once the player is dead. The health is kept between 0 and 500; at 0, for a player whose object is still of
// the given type, the death: the cars, emplacements and gun turrets he was using let go, the dropped weapon (in
// multiplayer, with its ammo, and the grenade launcher's grenades as well) or the death music, the death message in
// multiplayer, and the dead substate.
// AUTOINJECT
bool Player_CheckForDeath(obj_tag *player, uint objectType) {
    BLData *blData = (BLData *)player->extraObjectData;
    if (player->curState == PLAYERSTATE_DYING || player->curState == PLAYERSTATE_3)
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
        switch_channels[SWITCH_PLAYER_DIED] = 1;
        Music_Event(MUSICEV_DEATH, 1);
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
                pickup->effectFlags |= EFFECT_HIDDEN;
        }
    }

    Player_ClearInertia(player);
    HUD_Update(blData, player);
    player->creationTimeFrames = GameState.NumFramesUnpaused;
    player->objectType = OBJECTTYPE_DEAD_PLAYER;
    Player_WeaponNone(player);
    player->animState->animFlags &= ~ANIM_SCOPED;
    Player_SetCamMode(blData, CamMode_Default);
    Player_SetHealth(blData, 0.0f);
    player->curState = PLAYERSTATE_DYING;

    if (MPSettings.isMultiplayer != 0) {
        char *text = Txt_GetStringFromHeap(0);
        short self = blData->playerNum;
        short injurer = MPGame.players[self].idxOfLastInjurer;
        short killer = (injurer == self) ? -1 : injurer;
        if (killer == -2 || killer == -1)
            sprintf(text, Txt_BindLabel(NOTIF_X_DEFEATED, 0), MPSettings.Player[self].Name);
        else
            sprintf(text, Txt_BindLabel(NOTIF_DEFEATED_BY_X, 0), MPSettings.Player[injurer].Name);
        Text_AddMsg((char)self, 0, TXTMSG_INFO, text, 0, 0xb4);
    }

    Sound_Play3D(SFX_CHR_BOND_DIE, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    Player_ChangeSubState(player, (player->animState->inWater & 1) ? MovementType_DeadInWater : MovementType_Dead);
    MP_PlayerKilled(player);
    Collide_FreeHitList(&player->hitList);
    return true;
}

// The dead player, every frame. In zero G the body tumbles. In single player SWITCH_PLAYER_FINISHED_DYING goes up
// once the blood pane has run and skip is pressed; in multiplayer the player comes back after five seconds, unless he
// has used up his lives (game mode 0x10).
void Player_HandleDeath(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    Player_InWater(player, blData);
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
        if (HUD_State(blData->hudInfo, Blood) == 0 && Input_Action(-1, ACTION_SKIP_CUTSCENE, ACTION_HELD) == 0)
            return;
        if (Menu_PlayerHasQuitOrRestarted())
            return;
        switch_channels[SWITCH_PLAYER_FINISHED_DYING] = 1;
        return;
    }

    if (!blData->hudInfo->pane[Blood].enabled)
        HUD_Enable(blData->hudInfo, Blood, 1, 0);
    if ((uint)(GameState.NumFramesUnpaused - player->creationTimeFrames) < FRAME_RATE_INT * 5)
        return;
    if (MPSettings.GameMode == GM_TOPAGENT && (int)MPGame.players[Control_Plr2Ind(player)].deaths >= (int)MPSettings.MaxPoints)
        return;
    if (player->curState == PLAYERSTATE_3)
        return;
    MP_ReSpawn(player, blData->playerNum);
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
    player->curState = PLAYERSTATE_ALIVE;
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
    blData->thirdIcon = THIRDICON_NONE;
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
    Vec_Zero(&blData->fallVelocity);
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

    glb_viewer[blData->playerNum]->cel = NULL;
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    Camera_Set(viewer, &viewer->pos, &viewer->rot);
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
        blData->aimAutoLevelState = AIMLEVEL_EASING;
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
void Player_SSWalk(obj_tag *player, BLData *blData) {
    if (Player_InWater(player, blData))
        return;
    WaitForAnimSet(blData, player, PlayerAnimSetInitNormal);
    Player_Move(blData, player, 1.0f);
    float speed = Magnitude(blData->movement.z);
    if (blData->animWait == 0)
        UpdateWalkAnims(blData, player, speed);
    Player_UpdateWalkClass(player, speed + speed, 0.0f);
    Player_HandleJump(blData, player, &GRAVITY_VECTOR);
    TurnByRotationDelta(blData, player);
}

// Crouching: as walking at three quarters of the speed, standing up (as the player's crouch option says: on letting
// go of crouch, or on pressing crouch or jump) when there is room, 0.95 above the matrix's position. The player's
// object in EAX, his data in ECX.
void Player_SSCrouch(obj_tag *player, BLData *blData) {
    if (Player_InWater(player, blData))
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
        standUp = Input_Action(playerNum, ACTION_CROUCH, ACTION_HELD) == 0;
    }
    else {
        standUp = (uchar)Input_Action(playerNum, ACTION_CROUCH, ACTION_PRESSED);
        standUp |= (uchar)Input_Action(blData->playerNum, ACTION_JUMP, ACTION_PRESSED);
    }
    if (!(player->animState->animFlags & ANIM_CONTACT_ABOVE) && standUp && blData->animWait == 0) {
        _VECTOR above;
        Vec_Copy(Mat_Position(player->transformMatrix), &above);
        above.y = (float)((double)above.y + 0.95f);
        if (Collide_LineOfSight(&player->position, &above, player->inCel, player, NULL,
                                HITMASK_SKIP_WATER | HITMASK_SKIP_NO_CONTACT))
            Player_ChangeSubState(player, MovementType_Walk);
    }
    TurnByRotationDelta(blData, player);
}

// Breath under water: the air pane shows swimming or on a wire; the head (its bone) under the cel's water eases
// underwater towards 1 and, outside zero G, uses up air at 5 a second, with the going-under sound; above it the air
// comes back at 20 a second, with the coming-up sound. Out of air, the player is hurt twice a second. The player's
// object in EAX, his data in ECX.
void Player_MonitorAir(obj_tag *player, BLData *blData) {
    short subState = (short)player->subState;
    HUD_Enable(blData->hudInfo, Air, subState == MovementType_Swim || subState == MovementType_Wire, 0);
    _VECTOR head;
    _MATRIX headMatrix;
    AnimGetBoneWorldTrans(player, ANIM_BONE(BONE_HEAD), 0, &head, &headMatrix);
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
            blData->waterSound = Sound_Play3D(SFX_ENV_UNDERWATER_AMBIENCE_LOOP_01, &player->position, 100.0f, -1.0f,
                                              -1.0f, 0, 0, 0);
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
            Sound_PlayExt(SFX_CHR_BOND_AIR_GASP, 100.0f, 0, 0);
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
        Sound_PlayExt(SFX_CHR_BOND_GULP, 100.0f, 0, 0);
        Player_HandlePain(player, 2.0f, blData, -1, 7);
    }
}

// On the ground: crouch, or jump - by gravity times -0.4, with room above (0.95 over the matrix's position) - or, with
// the wire icon showing, catch the nearest wire (within 1000). In CastleIndoors1, before channel 0x48, jumping only
// makes a sound. In the air: hitting something above starts the fall; after jumpWait frames, landing ends the jump.
// The player's data in EAX, his object in ECX.
void Player_HandleJump(BLData *blData, obj_tag *player, _VECTOR *gravity) {
    switch (blData->jumpState) {
        case JUMP_NONE: {
            if ((uchar)Input_Action(blData->playerNum, ACTION_CROUCH, ACTION_PRESSED) && blData->animWait == 0) {
                Player_ChangeSubState(player, MovementType_Crouch);
                return;
            }
            if (blData->groundBits == 0)
                return;
            if (player->animState->animFlags & ANIM_CONTACT_ABOVE)
                return;
            if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 &&
                switch_channels[SWITCH_CASTLEINDOORS1_ARMED] == 0) {
                if (Input_Action(blData->playerNum, ACTION_JUMP, ACTION_PRESSED))
                    Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_ERROR, 100.0f, 0, 0);
                return;
            }
            if (!Input_Action(blData->playerNum, ACTION_JUMP, ACTION_PRESSED))
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
            if (!Collide_LineOfSight(&player->position, &above, player->inCel, player, NULL,
                                     HITMASK_SKIP_WATER | HITMASK_SKIP_NO_CONTACT))
                return;
            Vec_Zero(&blData->fallVelocity);
            auxVec_AddMulR32(&blData->fallVelocity, gravity, -0.4f, &blData->fallVelocity);
            blData->jumpState = JUMP_RISING;
            blData->jumpWait = 4;
            player->animState->animFlags &= ~ANIM_CONTACT_FLOOR;
            Sound_Play3D(SFX_GENERIC_CLOTH_MOVEMENT_01, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            return;
        }

        case JUMP_RISING: {
            char flags = player->animState->animFlags;
            if (flags & ANIM_CONTACT_ABOVE) {
                blData->jumpState = JUMP_FALLING;
                Vec_Zero(&blData->fallVelocity);
                return;
            }
            if (blData->jumpWait != 0) {
                blData->jumpWait--;
                return;
            }
            if (!(flags & ANIM_CONTACT_FLOOR))
                return;
            if (blData->fallVelocity.y > 0.0f)
                return;
        }
            // fall through
        case JUMP_FALLING:
            if (!(player->animState->animFlags & ANIM_CONTACT_FLOOR))
                return;
            Vec_Zero(&blData->fallVelocity);
            blData->jumpState = JUMP_NONE;
            if (player->animState->floor.materialType == MATERIAL_DEEPWATER ||
                player->animState->floor.materialType == MATERIAL_SHALWATER)
                Sound_Play3D(SFX_ENV_LAND_IN_WATER, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            else
                Sound_Play3D(SFX_GENERIC_CLOTH_MOVEMENT_01, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            blData->landingStepFrames = 8;
            blData->stepDistance = 999.0f;
            return;
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
double AccelFunc0(float input, float *speed, float base, float mul, float steps, float deadZone, float fullZone) {
    float magnitude = input < 0.0f ? -input : input;
    return Accelerate(input, *speed, base, mul, steps, deadZone, magnitude >= fullZone);
}

// AccelFunc0 with the caller saying when the speed goes for base * mul. The name is ours.
// FUNC_AT(0x000b7520)
double AccelFunc0Flag(float input, float *speed, float base, float mul, float steps, float deadZone, int full) {
    return Accelerate(input, *speed, base, mul, steps, deadZone, full != 0);
}

// The right stick, outside the remote-control substates and zero G. Not aiming, it pitches the view (and stops the
// view levelling itself while it does). Aiming through a sighted scope (WPNFLAG_REAL_SCOPE), it turns and
// pitches the view, slowed by the zoom. Aiming otherwise, it moves the crosshair over the screen, and turns or
// pitches the view only once the crosshair is pushed past its edge - slower under water.
// AUTOINJECT
void Player_Aiming(obj_tag *player) {
    short subState = (short)player->subState;
    BLData *blData = (BLData *)player->extraObjectData;
    if (subState >= MovementType_ZeroG && (subState <= MovementType_Emplacement || subState == MovementType_Ronin))
        return;

    AnimState *anim = player->animState;
    if (!(anim->animFlags & ANIM_SCOPED)) {
        float look = Input_Actionf(blData->playerNum, ACTION_AIM_U_D, ACTION_HELD);
        double accel = AccelFunc0(look, &blData->lookAccel, Plr_NoAimTurnSpeed_Y, Plr_NoAimTurnSpeed_Y_Mul,
                                  Plr_NoAimTurnSpeed_Y_Steps, 0.05f, 0.95f);
        blData->lookAccel = (float)accel;
        double pitch = accel * FRAME_RATE_MUL * look;
        blData->lastRotationDelta.x = (float)pitch;
        if (pitch > 0.0002f || pitch < -0.0002f || pitch != pitch) {
            blData->aimAutoLevelState = AIMLEVEL_AIMING;
            blData->pitchFromHorizontal = (float)(pitch * HALF_PI + blData->pitchFromHorizontal);
        }
        return;
    }

    if (weapon_data[anim->currentWeaponId].weaponFlags & WPNFLAG_REAL_SCOPE) {
        float turn = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_X, ACTION_HELD);
        float pitch = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_Y, ACTION_HELD);
        float accelX = (float)AccelFunc0(turn, &blData->scopeAccelX, Plr_ScopeSpeed_X, Plr_ScopeSpeed_X_Mul,
                                         Plr_ScopeSpeed_X_Steps, 0.05f, 0.95f);
        blData->scopeAccelX = accelX;
        double accelY = AccelFunc0(pitch, &blData->scopeAccelY, Plr_ScopeSpeed_Y, Plr_ScopeSpeed_Y_Mul,
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

    float stickX = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_X, ACTION_HELD);
    float stickY = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_Y, ACTION_HELD);
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
    blData->rotationDelta.y = (float)(blData->rotationDelta.y - turnBy * HALF_PI);
    blData->pitchFromHorizontal = (float)(pitchBy + blData->pitchFromHorizontal);
}

// ---------------------------------------------------------------------------------------------------------------
// Falling, and the collision capsule (Player_Collision)
// ---------------------------------------------------------------------------------------------------------------

// 0.45 a bit over: the constant's bits are 0x3ee66667, not 0.45f's
static const float kCrouchCapsuleTop = std::bit_cast<float>(0x3ee66667u);

// The fall speed is kept within +-45 a second
static void ClampFall(BLData *blData) {
    if (blData->fallVelocity.y < -45.0f)
        blData->fallVelocity.y = -45.0f;
    else if (blData->fallVelocity.y > 45.0f)
        blData->fallVelocity.y = 45.0f;
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
    MPGame.players[blData->playerNum].idxOfLastInjurer = -2;
    blData->hitTest.mask = 0;
    blData->hitTest.cel = player->inCel;
    _VECTOR up;
    Mat_GetUp(&up, &player->transformMatrix);

    _VECTOR *fall = &blData->fallVelocity;
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
                capsule->mask = HITMASK_SKIP_ROOMS | HITMASK_SKIP_CHILD_CELS | HITMASK_SKIP_DRONES;
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
            // From the head, as long as half the head to either foot, the longer, and no wider than the anim's root
            // height
            capsule->radius = (0.55f < rootHeight) ? 0.55f : rootHeight;
            _VECTOR head, leftFoot, rightFoot;
            AnimGetBoneWorldTrans(player, ANIM_BONE(BONE_HEAD), 0, &head, NULL);
            AnimGetBoneWorldTrans(player, ANIM_BONE(BONE_LEFT_FOOT), 0, &leftFoot, NULL);
            AnimGetBoneWorldTrans(player, ANIM_BONE(BONE_RIGHT_FOOT), 0, &rightFoot, NULL);
            Vec_Subtract(&head, &leftFoot, &up);
            float length = (float)(Vec_NormaliseLen(&up, &up) * 0.5f);
            double halfBody = Vec_Dist3D(&head, &rightFoot) * 0.5f;
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
    player->collideFlags = OBJCOLL_ACTOR;
}

// ---------------------------------------------------------------------------------------------------------------
// What the collision found (Player_CollisionHandler, Player_DealWithObjHit)
// ---------------------------------------------------------------------------------------------------------------

// What the feet stand on moves the player or drone with it (unless it is a drone, player, bullet, body or rigid
// body), and is given a hit from above at the same point.
void PlayerOrDrone_FeetOnPoint_DoCollision(obj_tag *obj, HITDATA_tag *hit) {
    obj_tag *under = hit->hitObj;
    if (under == NULL)
        return;
    switch ((uchar)under->objectType) {
        case OBJECTTYPE_DRONE:
        case OBJECTTYPE_PLAYER:
        case OBJECTTYPE_BULLET:
        case OBJECTTYPE_DEAD_DRONE:
        case OBJECTTYPE_DEAD_PLAYER:
        case OBJECTTYPE_RIGIDBODY:
            break;
        default:
            Control_InheritVelocity(obj, under, 3.0f);
            obj->animState->animFlags |= ANIM_CONTACT_MOVER;
            break;
    }
    HITDATA_tag *above = Coll_AddHitToList(&under->hitList, 1.0f, NULL, obj);
    above->dmgAmt = 0.0f;
    Vec_Copy(&CONST_UP_VECTOR, (_VECTOR *)&above->surfaceNormal);
    Vec_Negate(&CONST_UP_VECTOR, &above->hitDirection);
    Vec_Copy(&hit->hitPosition, &above->hitPosition);
}

// A ray from the position to 0.6 below the test's end. The first hit is stood on when its distance is within 0.1 of
// the root height (*floorNormalY gets its normal's y); the first hit that may be stood on moves the player with it.
// At the surface, the first hit of a material is kept as the anim's floor unless the floor is already 0xd or 0xe.
// Returns what it stands on.
// AUTOINJECT
obj_tag* PlayerNotDrone_FeetOnPoint(obj_tag *obj, HITTEST_tag *test, float *floorNormalY, char dying) {
    HITDATA_tag *hits = NULL;
    HITDATA_tag *stoodOn = NULL;
    HITDATA_tag *material = NULL;
    _VECTOR up, below;
    Mat_GetUp(&up, &obj->transformMatrix);
    if (floorNormalY != NULL)
        *floorNormalY = 1.0f;
    float root = obj->animState->animObj.rootHeight;
    auxVec_AddMulR32(&test->rayEnd, &up, (float)-((double)root + 0.6f), &below);
    Collide_RayIntersect(&obj->position, &below, obj->inCel, obj, NULL, &hits, 0,
                         HITMASK_SKIP_HIDDEN | HITMASK_SKIP_NO_CONTACT, 8);

    if (hits != NULL) {
        if (hits->materialType != MATERIAL_NONE)
            material = hits;
        if ((double)hits->dist - 0.1f <= root) {
            stoodOn = hits;
            if (floorNormalY != NULL)
                *floorNormalY = hits->surfaceNormal.b;
        }
        for (HITDATA_tag *hit = hits; hit != NULL; hit = hit->next) {
            if (hit->hitCel != NULL && (hit->hitCel->flags & CEL_NO_CONTACT))
                continue;
            if (hit->hitObj != NULL && (hit->hitObj->effectFlags & EFFECT_NO_CONTACT))
                continue;
            PlayerOrDrone_FeetOnPoint_DoCollision(obj, hit);
            break;
        }
    }

    if (test->found != 0 || stoodOn != NULL)
        obj->animState->animFlags |= ANIM_CONTACT_FLOOR;
    if (material != NULL && surfaced) {
        AnimState *anim = obj->animState;
        uchar floor = (uchar)anim->floor.materialType;
        if ((floor < MATERIAL_DEEPWATER || floor > MATERIAL_SHALWATER) && (anim->animFlags & ANIM_CONTACT_FLOOR))
            memcpy(&anim->floor, material, sizeof(HITDATA_tag));
    }
    Collide_FreeHitList(&hits);
    // Read from the hit after the list is freed, as the original does
    return stoodOn != NULL ? stoodOn->hitObj : NULL;
}

// The anim flags the collision works out afresh every frame
#define ANIM_CONTACTS (ANIM_CONTACT_FLOOR | ANIM_CONTACT_HIT | ANIM_CONTACT_ABOVE | ANIM_CONTACT_MOVER)

// A fall of more than a second (60 frames) hurts by its frames beyond that, from 10 on
#define FALL_SAFE_FRAMES 60.0f
#define FALL_HURT_FRAMES 10

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
    int material = hit->materialType & MATERIAL_MASK;
    if (material == MATERIAL_DEEPWATER || material == MATERIAL_SHALWATER) {
        AnimState *anim = player->animState;
        if ((uchar)anim->floor.materialType < MATERIAL_DEEPWATER ||
            (uchar)anim->floor.materialType > MATERIAL_SHALWATER)
            anim->floor = *hit;
        if (player->animState->animFlags & ANIM_FOOTSTEP)
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
            Player_HandlePain(player, damage, blData, -1, painType);
            return 0.0f;
        }

        case OBJECTTYPE_BULLET:
            break;

        default:
            return 0.0f;
    }

    if (MPSettings.dronesAreBots != 0) {
        BU_tag *bullet = (BU_tag *)hitObj->extraObjectData;
        if (Control_Plr2Ind(bullet->firedByObj) >= 4 && bullet->firedByObj != NULL) {
            BOT_vars_t *bot = ((Drone_tag *)bullet->firedByObj->extraObjectData)->botVars;
            if (IsFist(bullet->wpnDef->weaponVariantNum)) {
                if (bot->stats.traitFlags & BOT_TRAIT_FISTS_UP_CLOSE)
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
                    float from = Atan2(hit->hitDirection.x, hit->hitDirection.z);
                    double degrees = Vec_AngleDifference(from, player->rotation.y) * RAD_TO_DEG;
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
    Player_HandlePain(player, hit->dmgAmt, blData, hit->hitBoneIdx, 0);

    _VECTOR direction;
    Vec_Copy(&hit->hitDirection, &direction);
    ApplyMatrixLVI(&glb_viewer[blData->playerNum]->worldMatrix, &direction);
    Vec_Normalise(&direction, &direction);
    blData->hitDirections = HitDirections(&direction);

    if (!(bullet->wpnDef->impactFlags & (IMPACTFLAG_BOUNCES | IMPACTFLAG_STICKS)))
        Sound_PlayExt(headshot ? SFX_IMPACT_PLAYER_HEAD_SHOT : SFX_IMPACT_PLAYER_BODY, 100.0f, 0, 0);
    if (MPSettings.isMultiplayer == 0 && (bullet->wpnDef->damageClass & 1)) {
        if (bullet->wpnDef->weaponBaseNum == 0x65)
            blData->turnKick = -0.1f;
        if (bullet->wpnDef->weaponBaseNum == 0x66)
            blData->turnKick = 0.1f;
        if (bullet->wpnDef->weaponBaseNum == 0x63)
            blData->turnKick = 0.1f;
        Sound_Play3D(SFX_PUNCH_IMPACT, &player->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
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
    player->animState->floor.materialType = MATERIAL_NONE;
    if (debug_scanmode)
        return;

    if (player->subState != MovementType_Climb) {
        char dying = player->curState == PLAYERSTATE_DYING || player->curState == PLAYERSTATE_3;
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
                    Player_HandlePain(player, frames, blData, -1, 6);
            }
            blData->fallFrames = 0.0f;
        }
        else {
            if (!(blData->fallVelocity.y >= 0.0f))
                blData->fallFrames = (float)((double)FRAME_RATE_MUL + blData->fallFrames);
            else
                blData->fallFrames = 0.0f;
            if (GameState.CurrentLevelHashcode == HT_Level_Tower2B && player->position.y < -10.0f)
                switch_channels[SWITCH_TOWER2B_FELL] = 1;
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
            Vec_Zero(&blData->fallVelocity);
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
    Player_GrappleSetRope(player);
    Player_SetWatch(player);
}

// ---------------------------------------------------------------------------------------------------------------
// Making the player (Player_Init)
// ---------------------------------------------------------------------------------------------------------------

// The fists' models, by BLData::punchSkin
// XBE_GLOBAL(0x0017e8e8, 0x10) readonly relocate
HASHCODE PunchSkin[4] = {
    HT_Skin_Fists_Combat,
    HT_Skin_Fists_BlueSuit,
    HT_Skin_Fists_Tux,
    HT_Skin_Fists_Spacesuit,
};

// The start position's parameters as Player_Init reads them
#pragma pack(push, 1)
typedef struct {
    PlacementHeader header; // 0x00
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
    switch_channels[SWITCH_PLAYER_DIED] = 0;
    switch_channels[SWITCH_PLAYER_FINISHED_DYING] = 0;
    if (glb_players[num] != NULL)
        return NULL;

    obj_tag *player = control_create_object(sizeof(BLData), pos, NULL, NULL);
    BLData *blData = (BLData *)player->extraObjectData;
    glb_players[num] = player;
    glb_blokes[num] = blData;
    blData->playerNum = (char)playerNum;
    blData->playerNum2 = (char)playerNum;
    player->hitTest = &blData->hitTest;
    Sound_StopAllWithId(SFX_GADGET_WATCH_LASER_SHOOT);
    Sound_StopAllWithId(SFX_GADGET_WATCH_TASER_SHOOT);
    Sound_StopAllWithId(SFX_GADGET_WATCH_LASER_BURN_LOOP);
    Camera_Enable(playerNum, 1, 1, NULL);
    Camera_SetFade(playerNum, -45.0f, 0xff, 1);

    blData->aimX = 0.0f;
    blData->aimY = 0.0f;
    blData->pitchAutoLevelTarget = 0.0f;
    blData->pitchFromHorizontal = 0.0f;
    blData->aimAutoLevelDelay = 0;
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
    Vec_Zero(&blData->fallVelocity);
    blData->endCamera.range = 1.5f;
    blData->endCamera.distance = 1.5f;
    blData->endCamera.distanceWanted = 1.5f;
    blData->endCamera.angle = 0.0f;
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
            case HT_Level_HendersonA: case HT_Level_HendersonB: case HT_Level_HendersonC: case HT_Level_HendersonD:
                // The player's skins in single player, and the fists that go with them
                skin = HT_Skin_Bond_BlueSuit;
                punchSkin = 1;
                break;
            case HT_Level_CastleIndoors1: case HT_Level_CastleIndoors2:
                skin = HT_Skin_Bond_Tux;
                punchSkin = 2;
                break;
            case HT_Level_SpaceStationD:
                skin = HT_Skin_Bond_Spacesuit;
                punchSkin = 3;
                break;
            default:
                skin = HT_Skin_Bond_Combat;
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
        weapon_data[1].weaponModelHashcode = HT_Skin_Bond_hands_malewhite;
        weapon_data[0x37].offsetToNextAltFireVariant = 2;
    }
    blData->punchSkin = punchSkin;
    AnimObjectNew(player, skin);
    AnimDebugCreate();
    Player_SetCamMode(blData, CamMode_Default);

    glb_viewer[num]->zoom = 1.0f;
    Camera_Set(glb_viewer[num], pos, &player->rotation);
    glb_viewer[num]->cel = NULL;
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
    Player_InitGrapple(blData, player);
    Camera_SetUpdator(playerNum, player, (cameraUpdateFunc)Player_PositionCamera);
    player->effectFlags |= EFFECT_CEL_LIT;
    blData->active = 1;
    blData->frozen = 0;

    GRAVITY_VECTOR.x = 0.0f;
    GRAVITY_VECTOR.y = -9.8f;
    GRAVITY_VECTOR.z = 0.0f;
    if (MPSettings.isMultiplayer != 0) {
        Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Walk);
        AnimScriptAdd(player, HT_Script_PlayerRifle_Idle);
    }
    else {
        switch (params->startState) {
            case PlayerStart_Swimming:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Swim);
                AnimScriptAdd(player, HT_Script_AnimStart);
                break;
            case PlayerStart_ZeroG:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_ZeroG);
                AnimScriptAdd(player, HT_Script_AnimStart);
                Vec_Zero(&GRAVITY_VECTOR);
                break;
            case PlayerStart_Crouching:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Crouch);
                AnimScriptAdd(player, HT_Script_AnimStartCrouch);
                blData->headCrouchOffset = 0.45f;
                break;
            default:
                Player_StandAtNewPosition(player, &player->position, &player->rotation, MovementType_Walk);
                AnimScriptAdd(player, HT_Script_AnimStart);
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

void Player_ZeroG(BLData *blData, obj_tag *player) {
    Vec_Zero(&blData->fallVelocity);
    blData->aimAutoLevelState = AIMLEVEL_EASING;
    blData->aimAutoLevelDelay = 0;
    blData->pitchFromHorizontal = 0.0f;

    if (!(player->animState->animFlags & ANIM_SCOPED)) {
        float turn = Input_Actionf(blData->playerNum, ACTION_AIM_L_R, ACTION_HELD);
        blData->rotationDelta.y = (float)Negate((double)REC_FRAME_RATE * PI * 0.4f * turn);
        float pitch = Input_Actionf(blData->playerNum, ACTION_AIM_U_D, ACTION_HELD);
        blData->rotationDelta.x = (float)Negate((double)REC_FRAME_RATE * PI * 0.4f * pitch);
    }
    else {
        float turn = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_X, ACTION_HELD);
        blData->rotationDelta.y = (float)((double)REC_FRAME_RATE * PI * 0.4f / blData->zoom * turn * -0.75f);
        float pitch = Input_Actionf(blData->playerNum, ACTION_TURRET_AIM_Y, ACTION_HELD);
        blData->rotationDelta.x = (float)((double)REC_FRAME_RATE * PI * 0.4f / blData->zoom * pitch * -0.75f);
    }
    blData->rotationDelta.z = 0.0f;

    ZeroGAxis(&blData->lastMovement.x, &blData->movement.x,
              Input_Actionf(blData->playerNum, ACTION_WALK_L_R, ACTION_HELD) * 0.1f);
    ZeroGAxis(&blData->lastMovement.z, &blData->movement.z,
              Input_Actionf(blData->playerNum, ACTION_WALK_F_B, ACTION_HELD) * 0.1f);
    int down = (short)Input_Action(blData->playerNum, ACTION_CROUCH, ACTION_HELD);
    int up = (short)Input_Action(blData->playerNum, ACTION_JUMP, ACTION_HELD);
    ZeroGAxis(&blData->lastMovement.y, &blData->movement.y, (float)(up - down) * 0.1f);

    _MATRIX turn;
    RotMatrix(&blData->rotationDelta, &turn);
    Concat(&player->transformMatrix, &turn);
    Mat_CopyRot(&turn, &player->transformMatrix);

    if (Input_Action(blData->playerNum, ACTION_RELOAD, ACTION_PRESSED))
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

void Player_SSScanMode(BLData *blData, obj_tag *player) {
    if (!(player->animState->animFlags & ANIM_SCOPED)) {
        blData->aimAutoLevelState = AIMLEVEL_EASING;
        blData->movement.y = 0.0f;
        float turn = Input_Actionf(blData->playerNum, ACTION_AIM_L_R, ACTION_HELD);
        blData->rotationDelta.y = (float)((double)REC_FRAME_RATE * PI * Negate(turn));
        float pitch = Input_Actionf(blData->playerNum, ACTION_AIM_U_D, ACTION_HELD);
        blData->rotationDelta.x = (float)Negate((double)REC_FRAME_RATE * PI * pitch);

        float strafe = Input_Actionf(blData->playerNum, ACTION_WALK_L_R, ACTION_HELD);
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

        float forward = Input_Actionf(blData->playerNum, ACTION_WALK_F_B, ACTION_HELD);
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

        glb_viewer[blData->playerNum]->cel = NULL;
        blData->rotationDelta.z = 0.0f;
        _MATRIX yaw, pitched;
        RotMatrixY(blData->rotationDelta.y, &yaw);
        RotMatrixX(blData->rotationDelta.x, &pitched);
        Concat(&yaw, &player->transformMatrix);
        Concat(&player->transformMatrix, &pitched);
        Mat_CopyRot(&pitched, &player->transformMatrix);

        float climb = Input_Actionf(blData->playerNum, ACTION_JUMP, ACTION_HELD);
        double y = ((double)climb - Input_Actionf(blData->playerNum, ACTION_CROUCH,
                                                  ACTION_HELD)) * REC_FRAME_RATE * 3.0f +
                   player->position.y;
        player->position.y = (float)y;
        Mat_Position(player->transformMatrix)->y = (float)y;
    }
    else {
        double zoom = (double)Input_Actionf(blData->playerNum, ACTION_SCOPE_ZOOM,
                                            ACTION_PRESSED | ACTION_REPEAT) * REC_FRAME_RATE;
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
    Vec_Zero(&blData->fallVelocity);
    player->animState->animObj.rootTravel = 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// Using things (Player_Activate)
// ---------------------------------------------------------------------------------------------------------------

// What the player uses: with the standing icon showing, the nearest creep wall (true); otherwise whatever is within 1
// of the point in front of his eyes - scripted objects, doors (true when one opens), switches, triggers, cars, gun
// turrets and emplacements, monitors, the worm, and locks he is in front of. The original takes the player's data in
// EAX and the player in EDI.
uchar Player_Activate(BLData *blData, obj_tag *player) {
    HITDATA_tag *hitList = NULL;
    uchar used = 0;
    if (blData->thirdIcon == THIRDICON_WALLHUG) {
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

// ---------------------------------------------------------------------------------------------------------------
// Every frame (Player_Update)
// ---------------------------------------------------------------------------------------------------------------

// XBE_GLOBAL(0x002adf32, 0x1)
uint8_t benchmark;  // set, the player stands still
extern uint8_t GlobalBlur;

// Swimming: the move, then walking out once the feet stand on something with the surface at least 0.3 below the
// position, and the movement kept from taking the position above 0.3 below the surface.
void Player_Swim(obj_tag *player, BLData *blData) {
    Player_Move(blData, player, 1.0f);
    if (player->inCel == NULL)
        return;

    if ((player->animState->animFlags & ANIM_CONTACT_FLOOR) != 0
        && !((double)player->position.y - 0.3f < player->inCel->shearHeight)) {
        blData->aimAutoLevelState = AIMLEVEL_EASING;
        Player_ChangeSubState(player, MovementType_Walk);
    }

    _VECTOR move;
    ApplyMatrixLV(&player->transformMatrix, &blData->movement, &move);
    float surface = player->inCel->shearHeight - 0.3f;
    if (!(player->position.y < surface)) {
        if (move.y > 0.0f)
            move.y = 0.0f;
    }
    else if ((double)move.y + player->position.y > surface) {
        move.y = surface - player->position.y;
    }
    _MATRIX inverse;
    Mat_Inverse(&player->transformMatrix, &inverse);
    ApplyMatrixLV(&inverse, &move, &blData->movement);
}

// Eases the pitch 7.5% of the way to pitchAutoLevelTarget a frame while aimAutoLevelState is 2 or 3, after
// aimAutoLevelDelay frames, and not while jumping. Once the step is within 0.0002, state 2 becomes 3.
void Player_ClampSomeAngles(BLData *blData, obj_tag *player) {
    char state = blData->aimAutoLevelState;
    if (state < AIMLEVEL_EASING || state > AIMLEVEL_EASED)
        return;
    if (blData->aimAutoLevelDelay != 0) {
        blData->aimAutoLevelDelay--;
        return;
    }

    double step = ((double)blData->pitchAutoLevelTarget - blData->pitchFromHorizontal) * 0.075f;
    if (player->subState == MovementType_Walk && blData->jumpState != JUMP_NONE)
        return;
    double pitch = step + blData->pitchFromHorizontal;
    blData->pitchFromHorizontal = (float)pitch;
    if (pitch < -1.0f)
        blData->pitchFromHorizontal = -1.0f;
    else if (pitch > 1.0f)
        blData->pitchFromHorizontal = 1.0f;
    if (step <= 0.0002f && step >= -0.0002f && state == AIMLEVEL_EASING)
        blData->aimAutoLevelState = AIMLEVEL_EASED;
}

// A flash bang's fade (Player_SetFlashBang): flashTimeLeft counts down a frame at a time. While flashTime is set,
// single player blurs by flashBlur; over the first half of flashTime the fade is held, then it runs over the
// frames left and flashTime is cleared.
void Player_UpdateBlurAndFade(BLData *blData) {
    if (blData->flashTimeLeft != 0.0f)
        blData->flashTimeLeft -= 1.0f;
    if (blData->flashTime == 0.0f)
        return;
    if (MPSettings.isMultiplayer == 0)
        GlobalBlur = blData->flashBlur;
    if (blData->flashTime * 0.5f < blData->flashTimeLeft) {
        Camera_SetFade(blData->playerNum, -100000.0f, blData->flashBlur | 0xffffff00, 0);
        return;
    }
    Camera_SetFade(blData->playerNum, -blData->flashTimeLeft, blData->flashBlur | 0xffffff00, 0);
    blData->flashTime = 0.0f;
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
    if (Input_Action(blData->playerNum, ACTION_PAUSE, ACTION_PRESSED) && GameState.LevelEnding == 0 && ScriptCam == 0) {
        blData->pauseMenu = (short)MenuManager_Create(HT_Menu_ControllerMenu, P_PAUSE, blData->playerNum,
                                                      (uchar)blData->playerNum, ACTION_PAUSE, 1, MENU_STATUS_PAUSE);
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
        case PLAYERSTATE_START:
            player->curState = PLAYERSTATE_ALIVE;
            break;

        case PLAYERSTATE_ALIVE:
            PlarStat_LogUpdateElapsedTime(blData->playerNum);
            Vec_Zero(&blData->movement);
            Vec_Zero(&blData->rotationDelta);
            if (blData->traverseWait != 0)
                blData->traverseWait--;
            if (blData->field_0x8e3 != 0)
                blData->field_0x8e3--;

            // Player 4's action 0x27 turns flying on and off
            if (Input_Action(3, ACTION_39, ACTION_PRESSED)) {
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
                        Player_SSWalk(player, blData);
                        break;
                    case MovementType_Crouch:
                        Player_SSCrouch(player, blData);
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
                        Player_ZeroG(blData, player);
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
                        glb_viewer[blData->playerNum]->cel = NULL;
                        Player_ChangeSubState(player, blData->previousSubState);
                        break;
                    case MovementType_Grapple:
                        Player_SSGrapple(blData, player);
                        break;
                    case MovementType_Swim:
                        Player_Swim(player, blData);
                        break;
                    case MovementType_ScanMode:
                        Player_SSScanMode(blData, player);
                        break;
                }
            }

            Player_MonitorAir(player, blData);
            Player_ClampSomeAngles(blData, player);
            Player_Aiming(player);
            Player_ViewClamping(player);

            _VECTOR move;
            ApplyMatrixLV(&player->transformMatrix, &blData->movement, &move);
            Vec_Add2(&player->position, &move, &player->position);
            Vec_Copy(&blData->movement, &blData->lastMovement);
            blData->rotationDelta.y = (float)(Extended(blData->turnKick) + Extended(blData->rotationDelta.y));
            Vec_Add2(&player->rotation, &blData->rotationDelta, &player->rotation);
            Vec_Copy(&blData->rotationDelta, &blData->lastRotationDelta);
            break;

        case PLAYERSTATE_DYING:
        case PLAYERSTATE_3:
            AnimObjectUpdate(player);
            Player_HandleDeath(player);
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
        case PLAYERSTATE_ALIVE:
        case PLAYERSTATE_4:
            if (blData->frozen)
                break;
            if (blData->camMode != CamMode_Default) {
                _VECTOR dir, head;
                Mat_GetDir(&dir, &player->transformMatrix);
                AnimGetBoneWorldTrans(player, ANIM_BONE(BONE_HEAD), 0, &head, NULL);
                Env_Breath(&head, &dir, player->inCel, player->creationTimeFrames);
            }
            if (blData->weaponObject->curState != GUN_FIRING) {
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

        case PLAYERSTATE_DYING:
        case PLAYERSTATE_3:
            Player_WeaponRecoil(player);
            break;
    }

    Camera_ApplySwing(glb_viewer[blData->playerNum], weapon_data[player->animState->currentWeaponId].scopeSway);
    View_SetDrawInOtherViewsOnly(player, blData->playerNum);
    player->transformFlags |= TRANSFORM_MOVED;
    if (!(player->transformFlags & TRANSFORM_MATRIX_PLACED))
        Vec_Copy(&player->position, Mat_Position(player->transformMatrix));
}
