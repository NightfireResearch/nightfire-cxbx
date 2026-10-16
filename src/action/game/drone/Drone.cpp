#include "Drone.h"
#include "NDrone2.h"
#include "../../game.h"
#include "../../sound/music.h"
#include "../../math/math.h"      // Vec_Dist3D
#include "../mp/multiplayer.h"     // MPSettings
#include "DroneVision.h"           // DroneVision_LineOfSightToObject
#include "../obj/Player.h"         // BLData
#include "../../engine/Vision.h"   // Vision_InView
#include "../../engine/viewer.h"   // viewer_tag, cel_tag
#include "NDrone2_Opponent.h"      // NDrone2_SetOpponent
#include "../obj/bullet.h"         // BU_tag
#include "../obj/control.h"        // control_first_object

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

// Whether player 0's camera sees pos: in its view (2 m about it), in a cel with addedToDraw set (a NULL cel is not
// checked) and with a line of sight from the camera.
// AUTOINJECT
bool Drone_PositionVisible(_VECTOR *pos, cel_tag *cel) {
    viewer_tag *viewer = glb_viewer[((BLData *)glb_players[0]->extraObjectData)->playerNum];
    if (!Vision_InView(pos, 2.0f, viewer))
        return false;
    if (cel != NULL && !cel->addedToDraw)
        return false;
    CelPos_tag from, to;
    Vec_Copy(&viewer->pos, &from.pos);
    from.cel = viewer->someCel;
    Vec_Copy(pos, &to.pos);
    to.cel = cel;
    return DroneVision_LineOfSightToObject(&from, &to, glb_players[0], NULL, 1, 0);
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

// ---- once a frame, from Drone_InitComms (parcel 7: opponents)

#define Drone_AddDynamicAwarePoint ((void (__cdecl *)(uchar type, obj_tag *obj, _VECTOR *pos, cel_tag *cel, float radius))0x00030e10)
#define Drone_GetOpponentInfo ((void (__cdecl *)(Drone_tag *drone, obj_tag *gameObj))0x00031260)


// Outside multiplayer, the points drones take notice of this frame (NPCGlobals.awarePoints), each with the AI links
// around it flagged. Type 0x2a objects: points of types 0 and 2, 3 m, AILINK_SMOKE. Bullets of weapon 0x36: type 1,
// 3 m, AILINK_SMOKE. Of the others, type 2 and AILINK_EXPLOSIVE: 12 m for 0x2a and 0x2c-0x2f; for 0x34 and 0x6c, and
// again while the fuse is under 180 (alone for 0x3b-0x40); 8 m for a remote mine not in state 3.
// AUTOINJECT
void Drone_BuildDynamicAwarePoints(void) {
    if (MPSettings.maybeDroneAIEnabled)
        return;
    NPCGlobals.numAwarePoints = 0;
    memset(NPCGlobals.awarePoints, 0, sizeof(NPCGlobals.awarePoints));
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (obj->objectType == OBJECTTYPE_BULLET) {
            BU_tag *bullet = (BU_tag *)obj->extraObjectData;
            if (bullet == NULL || bullet->wpnDef == NULL)
                continue;
            switch (bullet->wpnDef->weaponVariantNum) {
            case 0x2a:
            case 0x2c:
            case 0x2d:
            case 0x2e:
            case 0x2f:
                Drone_AddDynamicAwarePoint(2, obj, &obj->position, obj->inCel, 12.0f);
                AINetwork_SetLinksFlagInCircle(&obj->position, obj->inCel, 12.0f, AILINK_EXPLOSIVE, 0);
                break;
            case 0x34:
            case 0x6c:
                Drone_AddDynamicAwarePoint(2, obj, &obj->position, obj->inCel, 12.0f);
                AINetwork_SetLinksFlagInCircle(&obj->position, obj->inCel, 12.0f, AILINK_EXPLOSIVE, 0);
                // fall through
            case Weap_Satchel:
            case 0x3c:
            case 0x3d:
            case 0x3e:
            case 0x3f:
            case 0x40:
                if (bullet->fuseTimer < 180.0f) {
                    Drone_AddDynamicAwarePoint(2, obj, &obj->position, obj->inCel, 12.0f);
                    AINetwork_SetLinksFlagInCircle(&obj->position, obj->inCel, 12.0f, AILINK_EXPLOSIVE, 0);
                }
                break;
            case 0x36:
                Drone_AddDynamicAwarePoint(1, obj, &obj->position, obj->inCel, 3.0f);
                AINetwork_SetLinksFlagInCircle(&obj->position, obj->inCel, 3.0f, AILINK_SMOKE, 0);
                break;
            case Weap_RemoteMine:
                if (obj->curState == 3)
                    break;
                Drone_AddDynamicAwarePoint(2, obj, &obj->position, obj->inCel, 8.0f);
                AINetwork_SetLinksFlagInCircle(&obj->position, obj->inCel, 8.0f, AILINK_EXPLOSIVE, 0);
                break;
            }
        } else if (obj->objectType == OBJECTTYPE_OCCLUDE) {
            // Drone_AddDynamicAwarePoint inline in the original, the same writes
            Drone_AddDynamicAwarePoint(0, obj, &obj->position, obj->inCel, 3.0f);
            Drone_AddDynamicAwarePoint(2, obj, &obj->position, obj->inCel, 3.0f);
            AINetwork_SetLinksFlagInCircle(&obj->position, obj->inCel, 3.0f, AILINK_SMOKE, 0);
        }
    }
}

// Each drone's opponent visibility (the AI volumes' where the opponent stands; for the player 0.75 times it in
// substate 4), then what it knows of its opponent (Drone_GetOpponentInfo)
// AUTOINJECT
void Drone_ProcessOpponents(void) {
    obj_tag *player = NDrone2_Player();
    float playerVisibility = 1.0f;
    if (player != NULL) {
        playerVisibility = Drone_VisibilityForPosition(&player->position, 0);
        if (player->subState == 4)
            playerVisibility *= 0.75f;
    }
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        obj_tag *opponent = drone->opponent;
        obj_tag *gameObj = drone->gameObj;
        if (opponent != NULL) {
            if (opponent->objectType == OBJECTTYPE_DRONE)
                drone->opponentVisibilityScale = ((Drone_tag *)opponent->extraObjectData)->positionVisibility;
            if (opponent == player)
                drone->opponentVisibilityScale = playerVisibility;
            else
                drone->opponentVisibilityScale = Drone_VisibilityForPosition(&opponent->position, 0);
        }
        Drone_GetOpponentInfo(drone, gameObj);
    }
}

// The dummy-player cheat (NPCGlobals.dummyPlayerActive) switched: on, the dummy player takes glb_players[0]'s place
// and the drones after the player go after it; off, they go back to the player. Nothing without a dummy player. Our
// name; nightfire-port has it inline in Drone_InitComms.
// FUNC_AT(0x00030950)
void Drone_UpdateDummyPlayer(char active) {
    if (active == (char)NPCGlobals.dummyPlayerActive || NPCGlobals.dummyPlayer == NULL)
        return;
    obj_tag *player = glb_players[0];
    if (active) {
        obj_tag *dummy = NPCGlobals.dummyPlayer;
        Vec_Copy2(&player->position, &dummy->position, &dummy->lastPosition);
        Vec_Copy2(&player->rotation, &dummy->rotation, &dummy->lastRotation);
        dummy->inCel = player->inCel;
        for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
            if (drone->opponent == player)
                NDrone2_SetOpponent(drone, NPCGlobals.dummyPlayer);
        }
    } else {
        for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
            if (drone->opponent == NPCGlobals.dummyPlayer)
                NDrone2_SetOpponent(drone, player);
        }
    }
    NPCGlobals.dummyPlayerActive = active;
}
