#include "Drone.h"
#include "NDrone2.h"
#include "../../game.h"
#include "../../sound/music.h"
#include "../../math/math.h"      // Vec_Dist3D
#include "../mp/multiplayer.h"     // MPSettings

#include "string.h"


// AUTOINJECT
bool Drone_DCVfromOBJ(obj_tag* gameObj, DCVars_tag* dcVars) {

    if(gameObj == NULL)
        return false;
    
    if(gameObj->objectType != OBJECTTYPE_DRONE && gameObj->objectType != OBJECTTYPE_DEAD_DRONE)
        return false;

    Drone_tag *drone = (Drone_tag*)gameObj->extraObjectData;

    dcVars->drone = drone;
    dcVars->gameObj = gameObj;
    dcVars->aiStateMachine = &drone->sm;
    dcVars->cel = gameObj->inCel;

    return true;

}


typedef struct {
    char baseObj[0x2c];
    DroneKeys keys;
} DroneCreationData;


// AUTOINJECT
obj_tag* Drone_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    
    DroneCreationData *create = (DroneCreationData*) lvl;

    if(Drone_bDisableSystem)
        return NULL;

    if(create->keys.minDifficulty > GameState.difficultyModifier)
        return NULL;

    DIVars_tag diVars;
    memset(&diVars, 0, sizeof(diVars));

    // Set up the DIVars info (this will then be copied again into the final locations as the drone itself is spawned)
    Vec_Copy(pos, &diVars.position);
    Vec_Copy(rot, &diVars.rotation);
    memcpy(&diVars.keys, &create->keys, sizeof(DroneKeys));

    // These two are not needed? Already memset to zero...
    diVars.gameObj = NULL; 
    diVars.glist = 0x0;
    
    return NDrone2_CreateFromDIVars(&diVars);
}

// AUTOGEN
void NDrone2_Enable(char enable, Drone_tag *drone);
// AUTOGEN
void NDrone2_PostLoad_Init(obj_tag *gameObj);

// Enables or disables every drone. Enabling leaves drones whose object is in state 1 as they are, and two scripts
// get more: 0x600001b sends every drone the attack message and changes the music, 0x600002e enables those in state 1
// waiting on switch channel 10.
// AUTOINJECT
void Drone_EnableAll(char enable, HASHCODE hashcode) {
    if (Drone_bDisableSystem)
        return;

    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (!enable || drone->gameObj->curState != 1)
            NDrone2_Enable(enable, drone);
    }
    NPCGlobals.enabled = enable;
    if (!enable)
        return;

    NPCGlobals.enabledFrame = GameState.NumFramesUnpaused;
    if (hashcode == 0x600001b) {
        Drone_Message(NULL, DRONE_MSG_AttackNow, NULL, FRAME_RATE_INT * 8);
        Music_Event(0xf, 1);
    } else if (hashcode == 0x600002e) {
        for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
            obj_tag *gameObj = drone->gameObj;
            if (gameObj == NULL)
                continue;
            if (gameObj->objectType != OBJECTTYPE_DRONE && gameObj->objectType != OBJECTTYPE_DEAD_DRONE)
                continue;
            if (gameObj->curState == 1 && drone->waitSwitchChannel == 10)
                Drone_Message(gameObj, DRONE_MSG_Enable, NULL, 0);
        }
    }
}

// A drone a script creates (event 8): a placement made up on the spot with only the skin, the play script and the
// weapon set, and the state it starts in.
// AUTOINJECT
obj_tag* Drone_CoderCreate(_VECTOR *pos, _VECTOR *rot, HASHCODE skin, HASHCODE playScript, uint unused,
                           uint weapon, ushort initialState) {
    if (Drone_bDisableSystem)
        return NULL;

    DroneCreationData create;
    memset(&create, 0, sizeof(create));
    create.keys.skin = skin;
    create.keys.playScript = playScript;
    create.keys.weapon = weapon;
    create.keys.combatRange = 0;
    create.keys.mode = 0;
    create.keys.secondaryMode = 0;
    obj_tag *gameObj = Drone_Create(pos, rot, (level_tag *)&create);
    if (gameObj != NULL) {
        Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
        drone->initialState = initialState;
        NDrone2_PostLoad_Init(drone->gameObj);
    }
    return gameObj;
}

// AUTOGEN
void BOT_fellOutMap(obj_tag *param_1);
#define NDrone2_CanSeeObject ((bool (__cdecl *)(Drone_tag *drone, obj_tag *obj, uint bone, ushort mask))0x000648b0)

// The drone's alert status (Drone_tag.alertStatus), or NULL for anything that is not a drone.
// AUTOINJECT
uchar* Drone_AlertStatus(obj_tag *gameObj) {
    if (gameObj == NULL || gameObj->extraObjectData == NULL || gameObj->objectType != OBJECTTYPE_DRONE)
        return NULL;
    return &((Drone_tag *)gameObj->extraObjectData)->alertStatus;
}

// Sends a message to every drone, live or dead, within `radius` of obj (Drone_Message). A stun grenade's
// (DRONE_MSG_ImpactStunGrenade) goes only to those that can see obj from bone 5.
// AUTOINJECT
void Drone_MessageObjVicinity(uint msgType, obj_tag *obj, float radius, void *extraData, uint delay) {
    if (Drone_bDisableSystem)
        return;
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        obj_tag *gameObj = drone->gameObj;
        if (gameObj == NULL)
            continue;
        if (gameObj->objectType != OBJECTTYPE_DRONE && gameObj->objectType != OBJECTTYPE_DEAD_DRONE)
            continue;
        if (Vec_Dist3D(&gameObj->position, &obj->position) > radius)
            continue;
        if (msgType == DRONE_MSG_ImpactStunGrenade && !NDrone2_CanSeeObject(drone, obj, 5, 0x500))
            continue;
        Drone_Message(gameObj, msgType, extraData, delay);
    }
}

// A drone that has left every room: switched off and marked for deletion; in multiplayer the bot code deals with it.
// AUTOINJECT
void Drone_FellOutMap(obj_tag *gameObj) {
    if (MPSettings.maybeDroneAIEnabled != 0) {
        BOT_fellOutMap(gameObj);
        return;
    }
    if (gameObj->extraObjectData != NULL)
        NDrone2_Enable(0, (Drone_tag *)gameObj->extraObjectData);
    gameObj->flags |= 1;
}
