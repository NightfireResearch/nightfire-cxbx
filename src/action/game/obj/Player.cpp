#include "Player.h"
#include "../mp/multiplayer.h"
#include "../view.h"
#include "../../engine/viewer.h"
#include "../../engine/Anim.h"

#include <string.h>

// AUTOGEN
unsigned short Player_ChangeSubState(obj_tag* obj, unsigned short newState);
// AUTOGEN
void Player_Disable(obj_tag *param_1,char param_2);
// AUTOGEN
void Player_Enable(obj_tag *param_1, _MATRIX *mtx, int param_3);
// AUTOGEN
void Player_SetHealth(BLData *obj, float health);


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