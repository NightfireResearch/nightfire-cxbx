#include "Drone.h"


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