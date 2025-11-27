// Multiplayer, remote-controlled tank / helicopter
#include "../../game.h"
#include "../../engine/Camera.h"
#include "../mp/multiplayer.h"
#include "object.h"
#include "player.h"
#include "../../math/math.h"
#include "../view.h"
#include "../../sound/Sound.h"
#include "../../util/hashtable.h"
#include "../../util/Random.h"
#include "../../input.h"
#include "../../Sound/Sound.h"

#include "build.h"
#include "Explode.h"

#include <stddef.h> // for offsetof?
#include <stdio.h>
#include "../../actionhelpers.h"

#include "car.h"

// Until we implement fully, use the in-game memory addresses
#define Tanks ((obj_tag**)0x001dc980) // MAX_TANKS? entries
#define NumTanks U16_AT(0x001dc798)
#define TankSpawns (*(_MATRIX(*)[8])0x001dc7a0)

#define MAX_TANKS 8 // Locations defined on the map

// WIP
void Car_CollisionHandler(obj_tag* me) {

    // TODO: Get hit data, work out what to do

    return;

}

// AUTOINJECT
void Car_Reset(void) {

    for(int i = 0; i < NumTanks; i++) {
        
        obj_tag *gameObj = Tanks[i];
        if(gameObj == NULL)
            continue;
        
        CAR_INFO *carObj = (CAR_INFO*)gameObj->extraObjectData;

        gameObj->curState = 0;
        gameObj->subState = 0;
        carObj->playerController = NULL;
        carObj->damageAmt = 0;
        Vec_Zero(&carObj->someVector_b4);
        Mat_Copy(&TankSpawns[i], &gameObj->transformMatrix);
        build_LinkToRoom(gameObj, 0, glb_world);
    }
    
}


// baseObj is const? or it's a pointer to a const obj_tag?
// Only used in here, no need to inject once we've reimplemented Car_Create
// + we cannot inject, LTCG has used special registers
// UNINJECTABLE - custom calling convention
void Car_InitBits(CAR_INFO *tankInfo, obj_tag *baseObj) {

    HASHCODE turretHashcode;
    HASHCODE barrelHashcode;

    if(tankInfo->isHeli == 0) {
        turretHashcode = GFX_RCCar_Turret;
        barrelHashcode = GFX_RCCar_Barrel;
    } else {
        turretHashcode = GFX_LittleNellie_Blades;
        barrelHashcode = GFX_LittleNellie_Prop;
    }

    celglist_tag * turretCelgl = hashtable_hashcode_to_celglist(turretHashcode);
    celglist_tag * barrelCelgl = hashtable_hashcode_to_celglist(barrelHashcode);

    tankInfo->turretGeom = Control_CreateObjEx(0, Mat_Position(baseObj->transformMatrix), NULL, NULL, turretCelgl, baseObj, 0, 4, 1.0, 0x00, 0xff, 0xff, 0xff);
    tankInfo->barrelGeom = Control_CreateObjEx(0, Mat_Position(baseObj->transformMatrix), NULL, NULL, barrelCelgl, baseObj, 0, 4, 1.0, 0x20, 0xff, 0xff, 0xff);

    // Meaning currently unknown - render mode?
    tankInfo->turretGeom->effectFlags |= ObjectEffectFlags::FLAG_UNKNOWN_40;
    tankInfo->barrelGeom->effectFlags |= ObjectEffectFlags::FLAG_UNKNOWN_40;

    Mat_Copy(&baseObj->transformMatrix, &tankInfo->turretGeom->transformMatrix);
    Vec_Copy(Mat_Position(baseObj->transformMatrix), Mat_Position(tankInfo->turretGeom->transformMatrix)); // This is pointless, it's the same matrix, but the game code does this?


}

// AUTOINJECT
void Car_PlayerHasDied(obj_tag *player) {

    obj_tag* objAt = control_first_object();

    while(objAt != NULL) {

        // TODO: Tidier to implement with Control_ReturnNextObjectOfType and just do the playerController check here
        
        CAR_INFO* car = (CAR_INFO*)(objAt->extraObjectData);
        
        if(objAt->objectType == OBJECTTYPE_CAR && car->playerController == player) {
            // We've found the car which is being remote controlled by the player
            Car_Deactivate(objAt);
            break;
        }

        objAt = objAt->nextObject;
    }

}

// AUTOINJECT
void __cdecl Car_Activate(obj_tag* carObj, obj_tag* playerObj) {

    if(carObj->curState != 0)
        return;

    BLData* playerData = (BLData*)(playerObj->extraObjectData);
    CAR_INFO* carData = (CAR_INFO*)(carObj->extraObjectData);

    playerData->previousSubState = Player_ChangeSubState(playerObj, 0x0b);

    // If it's a helicopter, control its body, otherwise control the barrel
    playerData->remoteControlDevice = carData->isHeli ? carObj : carData->barrelGeom;
    
    Player_SetCamMode(playerData, 0xd);
    carObj->curState = 5;
    carObj->subState = playerData->playerNum;
    Player_Disable(playerObj, 1);
    carData->playerController = playerObj;
    carData->tankMachinegunTemperature = 0.0f;
    carData->machineGunOverheated = 0;
    
    if(carData->isHeli) {
        carData->soundHandle = Sound_Play3D(SFX_VEH_BELL_HELICOPTER_LOOP,&(carObj->position),25.0,-1.0,-1.0,0,0,0);
        carData->mainAmmo = 4;
    } else {
        carData->soundHandle = Sound_Play3D(SFX_VEH_MP_TANK_ENGINE_LOOP,&(carObj->position),100.0,-1.0,-1.0,0,0,0);
        carData->mainAmmo = 10;
    }

    Player_WeaponNone(playerObj);

    Camera_CalcViewAngles(carObj->subState, DEG2RAD(60.0f)); // FIXME: Hardcoded FOV?

}

// AUTOINJECT
obj_tag * Car_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *level) {

    printf("Car_Create: Spawning at %f %f %f, miniVehiclesEnabled %i\n", pos->x, pos->y, pos->z, MPSettings.MiniVehiclesEnabled);

    if(MPSettings.isMultiplayer && !MPSettings.MiniVehiclesEnabled)
        return NULL;
    
    if(GameState.CurrentLevelHashcode == HT_Level_Ravine)
        return NULL;

    if(NumTanks >= MAX_TANKS)
        return NULL;
    
    obj_tag* baseObj = Control_CreateObjEx(sizeof(CAR_INFO),pos,rot,NULL,celgl,NULL,'\x01',0x44,1.0,0,0xff,0xff,0xff);
    
    if(baseObj == NULL)
        return NULL;

    CAR_INFO *tankInfo = (CAR_INFO*)baseObj->extraObjectData;

    baseObj->objectType = OBJECTTYPE_CAR;
    baseObj->maybeCollision = tankInfo;
    Quat_MatToQuat(&tankInfo->orientation, &baseObj->transformMatrix);

    switch(MPSettings.MiniVehiclesEnabled) {
        default:
        case 1: // Tanks only
            tankInfo->isHeli = 0;
            break;
        case 2: // Helicopters only
            tankInfo->isHeli = 1;
            break;
        case 3: // Both
            tankInfo->isHeli = (Rand_Rand(100) <= 50) ? 1 : 0;
            break;
    }

    if(tankInfo->isHeli) {
        // Flying above the ground at the given location0
        baseObj->transformMatrix.m[0xd] += 1.5f;
        baseObj->position.y += 1.5f;
        hashtable_set_object_to_entity_gfx(baseObj, GFX_LittleNellie_Body);
    }

    Car_InitBits(tankInfo, baseObj);

    View_SetDrawInAllViews(baseObj);
    View_SetDrawInAllViews(tankInfo->turretGeom);
    View_SetDrawInAllViews(tankInfo->barrelGeom);
    View_SetDrawInAllViews(tankInfo->bodyGeom);

    Vec_Zero(&tankInfo->someVector_b4);

    baseObj->effectFlags |= ObjectEffectFlags::FLAG_UNKNOWN_40;

    // Take a copy of our initial position and orientation, so that we can respawn the tank at the right location
    // Could just as easily been stored in the CAR_INFO struct, but this is how the original code does it so we'll stick with that
    Mat_Copy(&baseObj->transformMatrix, &TankSpawns[NumTanks]);

    tankInfo->tankNum = NumTanks;
    Tanks[NumTanks] = baseObj;
    NumTanks++;

    tankInfo->machineGunOverheated = 0;
    tankInfo->tankMachinegunTemperature = 0.0f;
    

    return baseObj;
}

// AUTOINJECT
void Car_Init(void) {

    for(int i = 0; i < MAX_TANKS; i++) {
        Tanks[i] = NULL;
    }
    NumTanks = 0;

}


// AUTOINJECT
void Car_Deactivate(obj_tag *carObj) {

    if(carObj->curState == 99)
        return;

    printf("Car_Deactivate\n");

    if(carObj->curState == 1) {

        BLData* playerData = glb_blokes[carObj->subState];
        obj_tag* playerObj = glb_players[carObj->subState];

        playerData->remoteControlDevice = NULL;
        Player_SetCamMode(playerData, 0);
        Player_ChangeSubState(playerObj, playerData->previousSubState);
        Player_Enable(playerObj, NULL, 0);
        playerObj->animState->switchingToWeaponId = playerObj->animState->prevHeldWeaponId; // Bring the weapon up after the car has been deactivated

    }

    carObj->curState = 99;

    CAR_INFO* carInfo = (CAR_INFO*)(carObj->extraObjectData);

    carInfo->lastController = carInfo->playerController;
    carInfo->playerController = NULL;
    carInfo->damageAmt = 0;

    if (carInfo->soundHandle != NULL) {
      Sound_Stop((DYNAMICSOUNDS*)carInfo->soundHandle); // FIXME change the type to avoid the cast
      carInfo->soundHandle = NULL;
    }

    Explode_Create(carObj, &carObj->position, &CONST_UP_VECTOR, 10.0, 10.0, (HASHCODE)0x6000052, 50.0, 0xff, 0xff, 100, carObj, 6);
    Vec_Zero(&carInfo->someVector_b4);
    Vec_Zero(&carInfo->someVector_98);
    Vec_Zero(&carInfo->someVector_8c);
    Mat_Copy(&TankSpawns[carInfo->tankNum], &carObj->transformMatrix);
    build_LinkToRoom(carObj, 0, glb_world);

    
    carObj->subState = 1800; // Respawn timer

}

// NOAUTOINJECT
// void Car_Update(obj_tag *obj) {

//     CAR_INFO *carInfo = (CAR_INFO*)obj->extraObjectData;

//     // Handle respawn countdown if we've been killed
//     if(obj->curState == 99) { // Awaiting respawn

//         // Set some state - unknown purpose
//         carInfo->field108_0x80 = 0;
//         carInfo->field139_0xd8 = 0;

//         // Hide the components
//         View_SetDrawInNoViews(obj);
//         View_SetDrawInNoViews(carInfo->turretGeom);
//         View_SetDrawInNoViews(carInfo->barrelGeom);
//         View_SetDrawInNoViews(carInfo->bodyGeom);

//         // Check countdown timer (stored in obj->subState)
//         // I think the rounding here could result in differences in respawn times between PAL and NTSC but this is an original game bug
//         obj->subState -= FRAME_RATE_MUL; 
//         if(obj->subState < 1) {
//             obj->curState = 0; // Trigger respawn next frame
//         }

//         return;
//     }

//     // Handle weapon cooldown
//     if(carInfo->tankMachinegunTemperature <= 0.0f) {
//         carInfo->machineGunOverheated = 0;
//         carInfo->tankMachinegunTemperature = 0.0f;
//     } else {
//         carInfo->tankMachinegunTemperature -= FRAME_RATE_MUL;
//     }

//     // Ensure components are visible
//     View_SetDrawInAllViews(obj);
//     View_SetDrawInAllViews(carInfo->turretGeom);
//     View_SetDrawInAllViews(carInfo->barrelGeom);
//     View_SetDrawInAllViews(carInfo->bodyGeom);

//     // ??
//     if(obj->curState == 2)
//         return;

//     if(obj->curState == 1) {
//         // Handle player pressing button to end control of the RC vehicle
//         if(Input_Action(obj->subState, ALTFIRE_ACTIVATE_EXITTANK, 4)) {
//             Car_Deactivate(obj);
//             return;
//         }
//     }

//     // Update position of the sound
//     if(carInfo->soundHandle) {
//         Sound_SetPosition(carInfo->soundHandle, Mat_Position(obj->transformMatrix));
//     }

//     if(!carInfo->isHeli) { // Is tank

//         // Process movement
//         if(obj->curState == 1) {
//             // TODO: This
//         }


//     } else { // Is helicopter

//     }


// }

