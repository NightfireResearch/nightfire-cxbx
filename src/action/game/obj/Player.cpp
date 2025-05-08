#include "Player.h"
#include "../mp/multiplayer.h"
#include "../view.h"

// AUTOGEN
unsigned short Player_ChangeSubState(obj_tag* obj, unsigned short newState);
// AUTOGEN
void Player_SetCamMode(BLData *param_1,unsigned short param_2);
// AUTOGEN
void Player_Disable(obj_tag *param_1,char param_2);
// AUTOGEN
void Player_WeaponNone(obj_tag *param_1);
// AUTOGEN
void Player_Enable(obj_tag *param_1, _MATRIX *mtx, int param_3);
// AUTOGEN
void Player_SetHealth(BLData *obj, float health);

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
    sightObj->specialFlags |= 0x20;
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
    muzzleFlashObj->specialFlags = muzzleFlashObj->specialFlags | 0x8221;
    View_SetDrawInThisViewOnly(muzzleFlashObj,viewerNum);
    blData->muzzleFlashObj = muzzleFlashObj;
    blData->muzzleFlashRelated = 0;
}