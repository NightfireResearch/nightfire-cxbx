#include "Player.h"
#include "../mp/multiplayer.h"
#include "../view.h"
#include "../../engine/Anim.h"

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

HASHCODE AnimSet_Rifle_SP[9] = {
    (HASHCODE) 0x06000a00,
    (HASHCODE) 0x06000a00,
    (HASHCODE) 0x06000a00,
    (HASHCODE) 0x0,
    (HASHCODE) 0x06000a00,
    (HASHCODE) 0x06000a00,
    (HASHCODE) 0x06000a00,
    (HASHCODE) 0x06000a00,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_RifleCrouch_SP[9] = {
    (HASHCODE) 0x060009ff,
    (HASHCODE) 0x060009ff,
    (HASHCODE) 0x060009ff,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009ff,
    (HASHCODE) 0x060009ff,
    (HASHCODE) 0x060009ff,
    (HASHCODE) 0x060009ff,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_Handgun[9] = {
    (HASHCODE) 0x060009dc,
    (HASHCODE) 0x060009de,
    (HASHCODE) 0x060009db,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009d7,
    (HASHCODE) 0x060009d7,
    (HASHCODE) 0x060009d8,
    (HASHCODE) 0x060009d8,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_HandgunCrouch[9] = {
    (HASHCODE) 0x060009d9,
    (HASHCODE) 0x060009f5,
    (HASHCODE) 0x060009f5,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009f3,
    (HASHCODE) 0x060009f3,
    (HASHCODE) 0x060009f4,
    (HASHCODE) 0x060009f4,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_SMG[9] = {
    (HASHCODE) 0x060009b0,
    (HASHCODE) 0x060009b2,
    (HASHCODE) 0x060009af,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009b3,
    (HASHCODE) 0x060009b3,
    (HASHCODE) 0x060009b4,
    (HASHCODE) 0x060009b4,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_SMGCrouch[9] = {
    (HASHCODE) 0x060009ad,
    (HASHCODE) 0x060009aa,
    (HASHCODE) 0x060009aa,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009ab,
    (HASHCODE) 0x060009ab,
    (HASHCODE) 0x060009ac,
    (HASHCODE) 0x060009ac,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_Rifle[9] = {
    (HASHCODE) 0x060001d4,
    (HASHCODE) 0x060001db,
    (HASHCODE) 0x060001d9,
    (HASHCODE) 0x0,
    (HASHCODE) 0x0600097c,
    (HASHCODE) 0x0600097c,
    (HASHCODE) 0x0600097d,
    (HASHCODE) 0x0600097d,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_RifleCrouch[9] = {
    (HASHCODE) 0x060001d7,
    (HASHCODE) 0x060008f9,
    (HASHCODE) 0x060008f9,
    (HASHCODE) 0x0,
    (HASHCODE) 0x06000914,
    (HASHCODE) 0x06000914,
    (HASHCODE) 0x06000915,
    (HASHCODE) 0x06000915,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_Handgun2H[9] = {
    (HASHCODE) 0x060001ce,
    (HASHCODE) 0x060001cf,
    (HASHCODE) 0x060001cd,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009fc,
    (HASHCODE) 0x060009fc,
    (HASHCODE) 0x060009fd,
    (HASHCODE) 0x060009fd,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_Handgun2HCrouch[9] = {
    (HASHCODE) 0x060001cb,
    (HASHCODE) 0x060009d2,
    (HASHCODE) 0x060009d2,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009d1,
    (HASHCODE) 0x060009d1,
    (HASHCODE) 0x060009d0,
    (HASHCODE) 0x060009d0,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_Launcher[9] = {
    (HASHCODE) 0x060001e6,
    (HASHCODE) 0x060001e7,
    (HASHCODE) 0x060001e5,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009df,
    (HASHCODE) 0x060009df,
    (HASHCODE) 0x060009e0,
    (HASHCODE) 0x060009e0,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_LauncherCrouch[9] = {
    (HASHCODE) 0x060001e3,
    (HASHCODE) 0x0600091f,
    (HASHCODE) 0x0600091f,
    (HASHCODE) 0x0,
    (HASHCODE) 0x0600091e,
    (HASHCODE) 0x0600091e,
    (HASHCODE) 0x0600091d,
    (HASHCODE) 0x0600091d,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_TwinHandguns[9] = {
    (HASHCODE) 0x060009fe,
    (HASHCODE) 0x060009b5,
    (HASHCODE) 0x060009b7,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009ba,
    (HASHCODE) 0x060009ba,
    (HASHCODE) 0x060009bb,
    (HASHCODE) 0x060009bb,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_TwinHandgunsCrouch[9] = {
    (HASHCODE) 0x060009bc,
    (HASHCODE) 0x060009d3,
    (HASHCODE) 0x060009d3,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009d4,
    (HASHCODE) 0x060009d4,
    (HASHCODE) 0x060009d5,
    (HASHCODE) 0x060009d5,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_Unarmed[9] = {
    (HASHCODE) 0x060001f1,
    (HASHCODE) 0x060001f2,
    (HASHCODE) 0x060001f0,
    (HASHCODE) 0x0,
    (HASHCODE) 0x060009e4,
    (HASHCODE) 0x060009e4,
    (HASHCODE) 0x060009e5,
    (HASHCODE) 0x060009e5,
    (HASHCODE) 0x0
};

HASHCODE AnimSet_UnarmedCrouch[9] = {
    (HASHCODE) 0x060001ee,
    (HASHCODE) 0x06000931,
    (HASHCODE) 0x06000931,
    (HASHCODE) 0x0,
    (HASHCODE) 0x06000930,
    (HASHCODE) 0x06000930,
    (HASHCODE) 0x0600092f,
    (HASHCODE) 0x0600092f,
    (HASHCODE) 0x0
};


// AUTOGEN
int** AnimSetAppend(obj_tag* gameObj, HASHCODE *list, float a, float b);
// AUTOGEN
void AnimSetDeleteAll(obj_tag *param_1);

// AUTOINJECT
void PlayerAnimSetInitCrouch(obj_tag *gameObj) {

    int wpnSet = weapon_data[gameObj->animState->currentWeaponId].weaponAnimationSet & 0xFF;

    HASHCODE* animSet = NULL;

    if (!MPSettings.isMultiplayer) {
        wpnSet = 999;
    }

    switch(wpnSet) {
        case 1:
            animSet = AnimSet_HandgunCrouch;
            break;
        case 2:
            animSet = AnimSet_SMGCrouch;
            break;
        case 3:
            animSet = AnimSet_RifleCrouch;
            break;
        case 4:
            animSet = AnimSet_Handgun2HCrouch;
            break;
        case 5:
            animSet = AnimSet_LauncherCrouch;
            break;
        case 6:
            animSet = AnimSet_TwinHandgunsCrouch;
            break;
        case 999:
            animSet = AnimSet_RifleCrouch_SP;
            break;
        default: 
            animSet = AnimSet_UnarmedCrouch;
            break;
    }

    BLData *blData = (BLData *)gameObj->extraObjectData;
    blData->animSet = AnimSetAppend(gameObj, animSet, 0.37f, 0.5f);

}

// AUTOINJECT
void PlayerAnimSetInitNormal(obj_tag *gameObj) {
  
    int wpnSet = weapon_data[gameObj->animState->currentWeaponId].weaponAnimationSet & 0xFF;

    HASHCODE* animSet = NULL;

    if (!MPSettings.isMultiplayer) {
        wpnSet = 999;
    }
    
    switch(wpnSet) {
        case 1:
            animSet = AnimSet_Handgun;
            break;
        case 2:
            animSet = AnimSet_SMG;
            break;
        case 3:
            animSet = AnimSet_Rifle;
            break;
        case 4:
            animSet = AnimSet_Handgun2H;
            break;
        case 5:
            animSet = AnimSet_Launcher;
            break;
        case 6:
            animSet = AnimSet_TwinHandguns;
            break;
        case 999:
            animSet = AnimSet_Rifle_SP;
            break;
        default: 
            animSet = AnimSet_Unarmed;
            break;
    }
    
    BLData *blData = (BLData *)gameObj->extraObjectData;
    blData->animSet = AnimSetAppend(gameObj, animSet, 0.37f, 0.5f);

}

// AUTOINJECT
void PlayerAnimSetInit(obj_tag *gameObj) {

    BLData *blData = (BLData*)gameObj->extraObjectData;
    blData->animSet = NULL;
    AnimSetDeleteAll(gameObj);

    if (gameObj->subState == MovementType_Crouch) {
        PlayerAnimSetInitCrouch(gameObj);
    } else {
        PlayerAnimSetInitNormal(gameObj);
    }
}