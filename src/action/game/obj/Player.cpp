#include "Player.h"
#include "../Upgrade.h"
#include "../mp/multiplayer.h"
#include "../view.h"
#include "../../engine/viewer.h"
#include "../../engine/Anim.h"
#include "../../engine/mouseLook.h"
#include "../../engine/mouseSteer.h"

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
    blData->muzzleFlashRelated = 0;
}

// Only used for laser and taser beams
// AUTOINJECT
void PositionBeam(obj_tag *param_1, _VECTOR *param_2, _VECTOR *param_3) {

    Vec_Copy(param_2, &param_1->position);

    float distance = Vec_Dist3D(param_2, param_3);

    param_1->transformFlags &= ~TRANSFORM_MOVED;
    param_1->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY;
    param_1->transformFlags |= TRANSFORM_MOVED;

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
// Strafing was deliberately left alone, which is why scoped movement is sideways-only in the original. That
// is a concession to aiming a scope with a thumbstick, and it is dropped here for every input device rather
// than only for the mouse: making it depend on whether the pointer happens to be captured meant the same pad,
// in the same level, moved differently depending on something the player was not thinking about. Suppressing
// the *turn* is not optional - while scoped, Player_Aiming turns the player through its own scope-speed path,
// and leaving this one in as well would add the two together.
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
        turn = 0.0f; // Player_Aiming turns through the scope path instead; both would add up
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

    // Channel 0x48 is "cover blown": set once Bond has rendezvoused with Zoe and been told to stop
    // playing a party guest. Until then he walks rather than jogs, and Player_HandleJump refuses to let
    // him jump at all. See docs/switch-channels.md for the rest of the hard-coded channels, and for why
    // a channel number need not mean the same thing in another level.
    if ((GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1) && (switch_channels[0x48] == '\0')) {
        speedScale = speedScale * 0.9f;
    }
    float moveZ = forwards * speedScale;
    float moveX = sideways * speedScale;

    float accel = TurnAccel(turn, &blData->turnAccelState, Plr_NoAimTurnSpeed_X,
                            Plr_NoAimTurnSpeed_X_Mul, Plr_NoAimTurnSpeed_X_Steps, 0.05f, 0.95f);
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

// AUTOGEN
bool Player_WeaponHasAmmo(BLData *param_1, short param_2);
// AUTOGEN
void Player_WeaponChange(obj_tag *param_1, char param_2, char param_3);
// AUTOGEN
void Player_AutoAim(obj_tag *param_1);
// AUTOGEN
void Player_ClearAimInertia(obj_tag *param_1);
// AnimScriptAdd, with the script's playback speed set (sAnimScript_tag +0x94) when it starts.
// AUTOGEN
sAnimScript_tag* AnimScriptAddSpeed(obj_tag* obj, HASHCODE hashcode, float speed);
// AnimScriptAdd, played backwards: raises the "reverse" flag at 0x001d7828 and tail-calls AnimScriptAdd.
// AUTOGEN
sAnimScript_tag* AnimScriptAddReverse(obj_tag* obj, HASHCODE hashcode);
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
// AUTOGEN
void Player_Zoom(obj_tag* player);

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
void Player_RamLoad(BLData * param_1, char param_2);
// AUTOGEN
void Player_RamSave(BLData * param_1);
// AUTOGEN
void Player_InitAmmoWeapons(obj_tag * param_1);
// AUTOGEN
undefined4 Player_EquipWeapon(BLData * param_1, short weaponNum, short param_3);
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

    blData->field_0xd8 = 0;
    blData->field_0xdc = 0;
    blData->crosshairOffsetX = 0.0f;
    blData->crosshairOffsetY = 0.0f;
    blData->field_0xd4 = 0;
    blData->field_0xf2 = 0;

    // The original pushes blData->playerNum as a second argument, but WatchOrHands_Create never reads it
    blData->weaponObject = WatchOrHands_Create((int *)player);

    blData->field_0x86c[0] = 0;
    blData->field_0x86c[1] = 0;
    blData->field_0x86c[2] = 0;
    blData->field_0x86c[3] = 0;
    blData->field_0x828 = 0;
    blData->field_0x8bc = 0.0f;
    blData->field_0x8c6 = 0;
    blData->field_0x8e6 = 0;
    blData->field_0x8e8 = 0;

    FUN_000b6dd0(player, blData->playerNum); // creates blData->weaponRelatedObjs[]
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
            player->animState->switchingToWeaponId = blData->ramLoadedWeaponId;
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
            player->animState->switchingToWeaponId = blData->ramLoadedWeaponId;
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
            player->animState->switchingToWeaponId = blData->ramLoadedWeaponId;
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
            player->animState->switchingToWeaponId = blData->ramLoadedWeaponId;
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
            player->animState->switchingToWeaponId = blData->ramLoadedWeaponId;
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
            player->animState->switchingToWeaponId = blData->ramLoadedWeaponId;
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
            player->animState->switchingToWeaponId = blData->ramLoadedWeaponId;
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

// Drones: damage they take and their armour (NDrone2_HitDamage, FUN_0003db50), their firing (DroneWeap_*,
// Drone_ModBulletDamage) and their captains (NDrone2_DefaultInit). P_TWEAKS/P_TWEAKS2 edit most of them.
#define DroneDamage_Easy                         FLOAT_AT(0x00164068)
#define DroneDamage_Normal                       FLOAT_AT(0x0016406c)
#define DroneDamage_Hard                         FLOAT_AT(0x00164070)
#define DroneDamage_Head                         FLOAT_AT(0x00164074)
#define DroneDamage_Legs                         FLOAT_AT(0x00164078)
#define DroneDamage_Arms                         FLOAT_AT(0x0016407c)
#define DroneDamage_Torso                        FLOAT_AT(0x00164080)
#define DroneArmour_Helmet                       FLOAT_AT(0x00164084)
#define DroneArmour_Combat                       FLOAT_AT(0x00164088)
#define DroneArmour_Jacket                       FLOAT_AT(0x0016408c)
#define DroneArmour_Vest                         FLOAT_AT(0x00164090)
#define DroneFiring_BurstDelay_Min               FLOAT_AT(0x00164094)
#define DroneFiring_BurstDelay_Normal            FLOAT_AT(0x00164098)
#define DroneFiring_BurstDelay_Max               FLOAT_AT(0x0016409c)
#define DroneFiring_BurstDelay_MinDist           FLOAT_AT(0x001640a0)
#define DroneFiring_BurstDelay_MaxDist           FLOAT_AT(0x001640a4)
#define DroneFiring_Accuracy_Easy                FLOAT_AT(0x001640a8)
#define DroneFiring_Accuracy_Normal              FLOAT_AT(0x001640ac)
#define DroneFiring_Accuracy_Hard                FLOAT_AT(0x001640b0)
#define DroneFiring_NewSighting_TimeToHit        FLOAT_AT(0x001640b4)
#define DroneFiring_TargetFirstMoved_TimeToHit   FLOAT_AT(0x001640b8)
#define DroneFiring_TargetFirstMoved_Accuracy    FLOAT_AT(0x001640bc)
#define DroneFiring_TargetMoving_Accuracy        FLOAT_AT(0x001640c0)
#define DroneFiring_TargetFirstStopped_TimeToHit FLOAT_AT(0x001640c4)
#define DroneFiring_TargetFirstStopped_Accuracy  FLOAT_AT(0x001640c8)
#define DroneFiring_TooClose_Distance            FLOAT_AT(0x001640cc)
#define DroneFiring_TooClose_Accuracy            FLOAT_AT(0x001640d0)
#define DroneFiring_TooClose_Damage              FLOAT_AT(0x001640d4)
#define DroneFiring_PlayerBackShot_Damage        FLOAT_AT(0x001640d8)
#define DroneCaptain_Mod_BulletDamage            FLOAT_AT(0x001640dc)
#define DroneCaptain_Mod_BulletAccuracy          FLOAT_AT(0x001640e0)
#define DroneCaptain_Mod_Health                  FLOAT_AT(0x001640e4)

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
