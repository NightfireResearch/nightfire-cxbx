// Multiplayer, remote-controlled tank / helicopter
#include "../gamestate.h"
#include "../mp/multiplayer.h"
#include "object.h"
#include "player.h"
#include "../../math/math.h"
#include "../view.h"

#include <stddef.h> // for offsetof?
#include <stdio.h>

// Until we implement fully, use the in-game memory addresses
#define Tanks ((obj_tag**)0x001dc980) // MAX_TANKS? entries
#define NumTanks U16_AT(0x001dc798)
#define TankSpawns (*(_MATRIX(*)[8])0x001dc7a0)

#define MAX_TANKS 8 // Locations defined on the map


#pragma pack(push, 1)

typedef struct {
    char _pad_1[0xa4];
    quaternion_tag orientation; // 0xA4
    _VECTOR someVector; // 0xB4
    obj_tag *turretGeom; // 0xC0
    obj_tag *barrelGeom; // 0xC4
    obj_tag *bodyGeom;   // 0xC8 -- Possibly redundant? Or is this the main tank body? Or special destroyed variant?
    obj_tag* playerController; // 0xCC (see Car_PlayerHasDied)
    char _pad_2[12];
    float _unknown_0xdc;
    float _unknown_0xe0;
    float tankMachinegunTemperature; // 0xE4
    uint soundHandle; // 0xE8
    char _pad_3[2];
    short tankNum; // 0xEE
    short mainAmmo; // 0xF0
    char isHeli; // 0xF2 - this could be Tank vs Helicopter? Would make sense as the behaviour changes depending on multiple options for "miniVehiclesEnabled" - could be None, Tanks, Helicopters, Both
    char machineGunOverheated;
    char _pad_5[0x4];
} CAR_INFO;

#pragma pack(pop)

// Way to see the size or offset of a struct at compile time (will error out, and reveal the size of kaboom - which is our size/offset)
//char (*__kaboom)[offsetof(CAR_INFO,tankMachinegunTemperature)] = 1;

// Test against Xbox compiled code; other platforms may differ
static_assert(sizeof(CAR_INFO) == 0xf8, "Size of CAR_INFO not correct");
static_assert(offsetof(CAR_INFO, turretGeom) == 0xc0, "Offset of turretGeom not correct");
static_assert(offsetof(CAR_INFO, tankMachinegunTemperature) == 0xe4, "Offset of tankMachinegunTemperature not correct");
static_assert(offsetof(CAR_INFO, tankNum) == 0xee, "Offset of tankNum not correct");
static_assert(offsetof(CAR_INFO, isHeli) == 0xf2, "Offset of isHeli not correct");

// AUTOGEN
obj_tag * Control_CreateObjEx(unsigned short, _VECTOR *, _VECTOR *, _MATRIX *, celglist_tag *, obj_tag *,char,unsigned short,float,unsigned short,unsigned char,unsigned char,unsigned char);
// AUTOGEN
celglist_tag * hashtable_hashcode_to_celglist(HASHCODE hashcode);
// AUTOGEN
void hashtable_set_object_to_entity_gfx(obj_tag *obj, HASHCODE hashcode);
// AUTOGEN
uint Rand_Rand(int max);
// AUTOGEN
void Quat_MatToQuat(quaternion_tag *quatOut, _MATRIX *matIn);
// AUTOGEN
obj_tag * control_first_object(void);

// Ghidra detects this as a thunked function, so we can't AUTOGEN it due to duplicate function names
void Car_Deactivate(obj_tag *object) {
    reinterpret_cast<void (*)(obj_tag *)>(0x00026a00)(object);
}

// AUTOGEN
unsigned short Player_ChangeSubState(obj_tag* obj, unsigned short newState);
// AUTOGEN
void Player_SetCamMode(BLData *param_1,unsigned short param_2);
// AUTOGEN
void Player_Disable(obj_tag *param_1,char param_2);
// AUTOGEN
void Player_WeaponNone(obj_tag *param_1);
// AUTOGEN
void __cdecl Camera_CalcViewAngles(ushort playerNum,float param_2);
// AUTOGEN
uint __cdecl Sound_Play3D(Action_SFX param_1,_VECTOR *position,float param_3,float param_4,float param_5, undefined2 param_6,undefined4 param_7,int param_8);

// WIP
void Car_CollisionHandler(obj_tag* me) {

    // TODO: Get hit data, work out what to do

    return;

}


// baseObj is const? or it's a pointer to a const obj_tag?
// Only used in here, no need to inject once we've reimplemented Car_Create
// + we cannot inject, LTCG has used special registers
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
    tankInfo->turretGeom->someFlags_0xcc |= 0x40;
    tankInfo->barrelGeom->someFlags_0xcc |= 0x40;

    Mat_Copy(&baseObj->transformMatrix, &tankInfo->turretGeom->transformMatrix);
    Vec_Copy(Mat_Position(baseObj->transformMatrix), Mat_Position(tankInfo->turretGeom->transformMatrix)); // This is pointless, it's the same matrix, but the game code does this?


}

// AUTOINJECT
void Car_PlayerHasDied(obj_tag *player) {

    obj_tag* objAt = control_first_object();

    while(objAt != NULL) {
        
        CAR_INFO* car = (CAR_INFO*)(objAt->extraObjectData);
        
        if(objAt->objectType == CAR && car->playerController == player) {
            // We've found the car which is being remote controlled by the player
            Car_Deactivate(objAt);
            break;
        }

        objAt = objAt->nextObject;
    }

}

// AUTOINJECT at 000268e0
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
    carObj->playerNum = playerData->playerNum;
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

    Camera_CalcViewAngles(carObj->playerNum, DEG2RAD(60.0f)); // FIXME: Hardcoded FOV?

}

// AUTOINJECT
obj_tag * Car_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *level) {

    printf("Car_Create: Spawning at %f %f %f, miniVehiclesEnabled %i\n", pos->x, pos->y, pos->z, MPSettings.miniVehiclesEnabled);

    if(MPSettings.isMultiplayer && !MPSettings.miniVehiclesEnabled)
        return NULL;
    
    if(GameState.currentLevelHashcode == HT_Level_Ravine)
        return NULL;

    if(NumTanks >= MAX_TANKS)
        return NULL;
    
    obj_tag* baseObj = Control_CreateObjEx(sizeof(CAR_INFO),pos,rot,NULL,celgl,NULL,'\x01',0x44,1.0,0,0xff,0xff,0xff);
    
    if(baseObj == NULL)
        return NULL;

    CAR_INFO *tankInfo = (CAR_INFO*)baseObj->extraObjectData;

    baseObj->objectType = CAR;
    baseObj->maybeCollision = tankInfo;
    Quat_MatToQuat(&tankInfo->orientation, &baseObj->transformMatrix);

    switch(MPSettings.miniVehiclesEnabled) {
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

    Vec_Zero(&tankInfo->someVector);

    baseObj->someFlags_0xcc |= 0x40;

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