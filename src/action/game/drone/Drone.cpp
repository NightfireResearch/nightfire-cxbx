#include "Drone.h"

#include "string.h"

#define Drone_bDisableSystem U8_AT(0x001dfa2a)

// AUTOINJECT
bool Drone_DCVfromOBJ(obj_tag* gameObj, DCVars_tag* dcVars) {

    if(gameObj == NULL)
        return false;
    
    if(gameObj->objectType != OBJECTTYPE_DRONE && gameObj->objectType != OBJECTTYPE_DEAD_DRONE)
        return false;

    Drone_tag *drone = (Drone_tag*)gameObj->extraObjectData;

    dcVars->drone = drone;
    dcVars->gameObj = gameObj;
    dcVars->aiStateMachine = &drone->aiStateMachine;
    dcVars->cel = gameObj->inCel;

    return true;

}


typedef struct {
    char baseObj[0x2c];
    maybeSAnimSkin skinInfo;
} DroneCreationData;

// AUTOGEN
obj_tag* NDrone2_CreateFromDIVars(DIVars_tag *diVars);

// AUTOINJECT
obj_tag* Drone_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    
    DroneCreationData *create = (DroneCreationData*) lvl;

    if(Drone_bDisableSystem)
        return NULL;

    if(create->skinInfo.minDifficultyLevelForDrone > GameState.difficultyModifier)
        return NULL;

    DIVars_tag diVars;
    memset(&diVars, 0, sizeof(diVars));

    // Set up the DIVars info (this will then be copied again into the final locations as the drone itself is spawned)
    Vec_Copy(pos, &diVars.position);
    Vec_Copy(rot, &diVars.rotation);
    memcpy(&diVars.skinInfo, &create->skinInfo,sizeof(maybeSAnimSkin));

    // These two are not needed? Already memset to zero...
    diVars.gameObj = NULL; 
    diVars.someOtherThing = 0x0;
    
    return NDrone2_CreateFromDIVars(&diVars);
}