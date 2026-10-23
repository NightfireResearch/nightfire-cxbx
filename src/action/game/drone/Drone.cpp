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
#include "../obj/Drone_AIHints.h"  // CoverNode_tag, AIBoxList, Drone_ProcessCoverNodes
#include "../obj/DroneSpawner.h"   // DroneSpawner_tag, DroneSpawner_DroneDelete
#include "../obj/Explode.h"        // Explode_Create
#include "../obj/GT.h"             // GUNTURRET
#include "../sp/SwitchChannels.h"
#include "../../engine/Anim.h"     // AnimGetBoneWorldTrans
#include "../../math/X87Ops.h"     // FSin, FCos
#include "../../memory.h"          // Mem_Free
#include "../../sound/Sound.h"     // Sound_ZeroAlertness
#include "../../util/LList.h"
#include "DroneTables.h"           // DroneTypeSettings
#include "DroneCollision.h"        // NDrone2_Collision
#include "DroneMove.h"             // NDrone2_Move, DroneMove_SetBoundryFlags, DroneMove_NoBunching
#include "DroneTracking.h"         // NDrone2_DoTracking
#include "DroneVision_Alerts.h"    // DroneFunc_HandleSoundAlerts
#include "DroneWeapon.h"           // DroneWeap_HandleFiring
#include "DroneInit.h"
#include "BOT.h"                    // BOT_fellOutMap
#include "DroneModes.h"
#include "DroneAnim.h"
#include "NDrone2_Special.h"             // NDrone2_CreateNinjaEyes

#include <math.h>
#include "string.h"

// Another package's, or still the game's

#include "../obj/BodyGlow.h"
#include "NDrone2_Combat.h"
#include "NDrone2_Cover.h"
#include "../obj/Env.h"

// XBE_GLOBAL(0x001dfa2a, 0x1)
uchar Drone_bDisableSystem;

// The lines Drone_BrokenObject picks from
#define DRONE_BROKEN_OBJECT_SFX 0x40
#define DRONE_BROKEN_OBJECT_SFX_LAST 0x43

// The cheats' switches (ui_cheats.cpp)
extern uint32_t switch_NO_DRONES;
// XBE_GLOBAL(0x001dfa29, 0x1)
char Drone_DummyPlayer;
// Set while Drone_InitComms has the drones hidden (switch_NO_DRONES)
// XBE_GLOBAL(0x001dfa2b, 0x1)
uchar Drone_bDontDraw;

// The tables Drone_PreLoad_Init points NPCGlobals at
// XBE_GLOBAL(0x001e6a60, 0x7800)
CoverNode_tag CoverNodeTable[MAX_COVER_NODES];
// XBE_GLOBAL(0x001dfa30, 0x5c00)
DroneAIPoint_tag AIPointTable[MAX_AI_POINTS];

// A cartridge case's extraObjectData; only what Drone_Delete reads
typedef struct {
    char _unknown00[0x20];
    obj_tag *unknown20;     // 0x20 - Drone_Delete clears it when it is the drone deleted
} CasingData;

#define HEAD_BONE 5
#define WORLD_BONE(bone) (0x80000000 | (bone))

static const float kPi = 3.14159274f;
static const float kHalfPi = 1.57079637f;

// 0 to 0xfff for a drone, scattered by its state machine's id (its number in the level): Knuth's multiplicative hash
static uint BreathPhase(const Drone_tag *drone) {
    return (drone->sm.id * 2654435761u) >> 20;
}

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
    if (hashcode == HT_Script_00001b) {
        Drone_Message(NULL, DRONE_MSG_AttackNow, NULL, FRAME_RATE_INT * 8);
        Music_Event(0xf, 1);
    } else if (hashcode == HT_Script_00002e) {
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
    if (MPSettings.dronesAreBots != 0) {
        BOT_fellOutMap(gameObj);
        return;
    }
    if (gameObj->extraObjectData != NULL)
        NDrone2_Enable(0, (Drone_tag *)gameObj->extraObjectData);
    gameObj->flags |= 1;
}

// ---- once a frame, from Drone_InitComms (parcel 7: opponents)

// A point drones take notice of this frame, while there is room for it
// AUTOINJECT
void Drone_AddDynamicAwarePoint(uchar type, obj_tag *obj, _VECTOR *pos, cel_tag *cel, float radius) {
    if (NPCGlobals.numAwarePoints >= MAX_AWARE_POINTS)
        return;
    DroneAwarePoint *point = &NPCGlobals.awarePoints[NPCGlobals.numAwarePoints++];
    point->type = type;
    point->obj = obj;
    point->pos = *pos;
    point->cel = cel;
    point->radius = radius;
}

// Outside multiplayer, the points drones take notice of this frame (NPCGlobals.awarePoints), each with the AI links
// around it flagged. Type 0x2a objects: points of types 0 and 2, 3 m, AILINK_SMOKE. Bullets of weapon 0x36: type 1,
// 3 m, AILINK_SMOKE. Of the others, type 2 and AILINK_EXPLOSIVE: 12 m for 0x2a and 0x2c-0x2f; for 0x34 and 0x6c, and
// again while the fuse is under 180 (alone for 0x3b-0x40); 8 m for a remote mine not in state 3.
// AUTOINJECT
void Drone_BuildDynamicAwarePoints(void) {
    if (MPSettings.dronesAreBots)
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
// A drone's skin hashcode, or 0 for an object without drone data. FUN_00030a10 in Ghidra. Not named on PS2; the
// name is ours.
// AUTOINJECT
HASHCODE Drone_GetSkin(obj_tag *gameObj) {
    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    if (drone == NULL)
        return (HASHCODE)0;
    return drone->skinHashcode;
}

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

// In HendersonA, the player breaking something with a bullet: the drones of DTYPE 12 have their say, until the first
// of them that is dead
// AUTOINJECT
void Drone_BrokenObject(obj_tag *obj, HITDATA_tag *hit) {

    if (Drone_bDisableSystem || GameState.CurrentLevelHashcode != HT_Level_HendersonA)
        return;

    obj_tag *bullet = hit->hitObj;
    if (bullet == NULL || bullet->extraObjectData == NULL || bullet->objectType != OBJECTTYPE_BULLET
        || ((BU_tag *)bullet->extraObjectData)->firedByObj != glb_players[0])
        return;

    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (drone->dtype != 12)
            continue;
        if (drone->health <= 0.0f)
            return;
        DCVars_tag dcv;
        if (Drone_DCVfromOBJ(drone->gameObj, &dcv))
            NDrone2_SpeechSFX(&dcv, DroneFunc_ChooseSFX(DRONE_BROKEN_OBJECT_SFX, DRONE_BROKEN_OBJECT_SFX_LAST), 0, 0, 0);
    }
}

// ---- the level's set-up

// Before the level's placements are read: the empty drone and AI volume lists, the cover node and AI point tables,
// no alert, and no groups.
// AUTOINJECT
void Drone_PreLoad_Init(void) {
    LList_Init(&NDrone2Info, sizeof(Drone_tag), 0);
    LList_Init(&AIBoxList, sizeof(DroneAIVolume_tag), 0);
    NPCGlobals.safetyEmitter.data = NULL;
    NPCGlobals.coverCursor = 0;
    NPCGlobals.numCoverNodes = 0;
    NPCGlobals.coverNodes = CoverNodeTable;
    NPCGlobals.numAIPoints = 0;
    NPCGlobals.aiPoints = AIPointTable;
    NPCGlobals.unknown1384 = 0;
    NPCGlobals.alert.frame = 0;
    NPCGlobals.alert.param[2] = 0.0f;
    NPCGlobals.alert.cel = NULL;
    for (int i = 0; i < DRONE_GROUPS; i++) {
        NPCGlobals.spawners[i] = NULL;
        NPCGlobals.groups[i] = NULL;
        NPCGlobals.groupCounts[i] = 0;
    }
}

// Clears the NPC globals, then, unless the drone system is disabled, sets the few that do not start at 0 and the
// state machine's message pool.
// AUTOINJECT
void Drone_LevelReset(void) {
    memset(&NPCGlobals, 0, sizeof(NPCGlobals));
    if (Drone_bDisableSystem)
        return;
    NPCGlobals.unknown138c = 0.5f;
    NPCGlobals.deadFrames = FRAME_RATE_INT * 10;
    NPCGlobals.unknown14c = 6;
    NPCGlobals.enabled = 1;
    NPCGlobals.NumDrones = 0;
    NPCGlobals.unknown1390 = 0;
    Drone_SM_Init();
}

// 50 degrees, one ulp under the nearest float; and 45
static const float kMaxSlope = 0.87266463f;
static const float kMaxSlopeCastleIndoors2 = 0.785398185f;

// Once the level is read: the move test's limits (CastleIndoors2's are lower; EvilBaseC starts with channel 0x96
// off), the safety emitter, every cover node's and AI point's flood, the dummy player, the groups, each drone's own
// set-up, and the door and kick nodes and the passable bounds.
// AUTOINJECT
void Drone_PostLoad_Init(void) {
    if (Drone_bDisableSystem)
        return;

    NPCGlobals.postLoadFrame = GameState.NumFramesUnpaused;
    NPCGlobals.playerTarget.builtFrame = 0;
    NPCGlobals.alertTarget.builtFrame = 0;
    if (GameState.CurrentLevelHashcode == HT_Level_EvilBaseC)
        switch_channels[0x96] = 0;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors2) {
        MoveTest_MaxSlope = kMaxSlopeCastleIndoors2;
        MoveTest_StepHeight = 1.5f;
    } else {
        MoveTest_MaxSlope = kMaxSlope;
        MoveTest_StepHeight = 2.0f;
    }

    memset(&NPCGlobals.safetyEmitter, 0, sizeof(NPCGlobals.safetyEmitter));
    MoveTest_UnknownB8 = 30.0f;
    MoveTest_MaxDistanceSq = 900.0f;
    MoveTest_MaxRise = 4.0f;
    if (AIPath_MaxNodes != 0)
        AINetwork_AllocEmitter(&NPCGlobals.safetyEmitter, AIPath_MaxNodes);
    for (uint i = 0; i < NPCGlobals.numCoverNodes; i++)
        AINetwork_EmitPath(&NPCGlobals.coverNodes[i].emitter, 0.0f);
    for (uint i = 0; i < NPCGlobals.numAIPoints; i++)
        AINetwork_EmitPath(&NPCGlobals.aiPoints[i].emitter, 0.0f);

    Drone_DummyPlayer = 0;
    DroneSpawner_CreateBlank();
    NDrone2_SetupGroups();
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next)
        NDrone2_PostLoad_Init(drone->gameObj);
    NDrone_InitDoorNodes();
    NDrone_InitKickNodes();
    AINetwork_InitPassableBoundries();
}

// ---- once a frame, before the objects

// Stops (pause) or restarts the drones' thinking and drawing. Inline twice in the original's Drone_InitComms;
// nightfire-port's name.
static void Drone_PauseNoDraw(bool pause) {
    Drone_bDisableSystem = pause;
    Drone_bDontDraw = pause;
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (pause)
            drone->gameObj->effectFlags |= FLAG_HIDDEN;
        else
            drone->gameObj->effectFlags &= ~FLAG_HIDDEN;
    }
}

// Music event 0xe when no enemy drone is idle and none is alerted near its opponent; a non-ally drone alerted within
// 30 m of its opponent turns channel 0x97 on instead. Channel 0x98 says whether one is. Our name; nightfire-port has
// it inline in Drone_InitComms.
// FUNC_AT(0x000314a0)
void Drone_UpdateCombatChannels(void) {
    bool calm = true;
    bool alerted = false;
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (drone->flags & 0x600)
            continue;
        if (!(drone->flags & 0x100)) {
            if (drone->side == 1)
                calm = false;
            continue;
        }
        if (drone->alertness >= 1.0f && drone->distanceToTarget < 30.0f && drone->side != 2) {
            alerted = true;
            break;
        }
    }
    if (alerted)
        switch_channels[0x97] = 1;
    else if (calm)
        Music_Event(0xe, 1);
    switch_channels[0x98] = alerted;
}

// The NPC system's frame: hides the drones while the no-drones cheat is on, follows the dummy-player cheat, then
// (drones enabled and present) the aware points, targets, cover nodes, opponents and sight, the combat channels and
// the nav node cache; counts the dead drones, repeats the global alarm while channel 0x96 is on (once in EvilBase),
// sends the delayed messages, keeps this frame's counts and starts the music's counts again.
// AUTOINJECT
void Drone_InitComms(void) {
    if (switch_NO_DRONES) {
        if (!Drone_bDontDraw || !Drone_bDisableSystem)
            Drone_PauseNoDraw(true);
        return;
    }
    if (Drone_bDontDraw || Drone_bDisableSystem)
        Drone_PauseNoDraw(false);

    Drone_UpdateDummyPlayer(Drone_DummyPlayer);
    if (Drone_bDisableSystem || !NPCGlobals.enabled || NPCGlobals.NDrone2List == NULL)
        return;

    AINetwork_ClearLinkFlags(AILINK_EXPLOSIVE | AILINK_SMOKE | AILINK_CREEPING);
    Drone_BuildDynamicAwarePoints();
    DroneFunc_UpdateTargets();
    Drone_ProcessCoverNodes();
    Drone_ProcessOpponents();
    DroneFunc_ClearTargets();
    DroneVision_ProcessDroneSight();
    Drone_UpdateCombatChannels();
    NDrone2_NavNodeCache();

    NPCGlobals.unknown150 = 0;
    NPCGlobals.deadOrFadingFast = 0;
    NPCGlobals.oldestDeadFrame = GameState.NumFramesUnpaused;
    NPCGlobals.unknown15c = 0;
    // A drone whose object is not a drone keeps the last one's DCVars (before the first, the original's are
    // uninitialised stack; ours are zero)
    DCVars_tag dcv;
    memset(&dcv, 0, sizeof(dcv));
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        Drone_DCVfromOBJ(drone->gameObj, &dcv);
        DroneFunc_HandleSoundAlerts(&dcv);
        uint since;
        switch (drone->sm.curState) {
        case DSTATE_HostageDead:
        case DSTATE_Dead:
        case DSTATE_AstronautDeath:
            since = drone->sm.stateEnteredFrame;
            break;
        case DSTATE_Fade:
            since = drone->sm.stateEnteredFrame + NPCGlobals.deadFrames;
            break;
        case DSTATE_FadeFast:
            NPCGlobals.deadOrFadingFast++;
            continue;
        default:
            continue;
        }
        NPCGlobals.unknown150++;
        if (since < NPCGlobals.oldestDeadFrame) {
            NPCGlobals.oldestDeadFrame = since;
            NPCGlobals.unknown15c = drone->sm.id;
        }
    }
    NPCGlobals.deadOrFadingFast += NPCGlobals.unknown150;
    Sound_ZeroAlertness();

    if (switch_channels[0x96] && NPCGlobals.alarmFrame + 30 < GameState.NumFramesUnpaused
        && (GameState.CurrentLevelHashcode != HT_Level_EvilBase || !NPCGlobals.alarmSaid)) {
        NPCGlobals.alarmSaid = 1;
        // Drone_Message(NULL, ...), inline
        if (!Drone_bDisableSystem)
            Drone_SM_BroadcastMsg(DRONE_MSG_GlobalAlarm, NULL, 0, 0);
        NPCGlobals.alarmFrame = GameState.NumFramesUnpaused;
    }
    Drone_SM_SendDelayedMsgs();
    DroneFunc_CheckAlarmRaised();

    NPCGlobals.prevEnemies = NPCGlobals.enemies;
    NPCGlobals.prevNeutrals = NPCGlobals.neutrals;
    NPCGlobals.prevAllies = NPCGlobals.allies;
    NPCGlobals.prevUnknown188 = NPCGlobals.unknown188;
    NPCGlobals.prevSeenAttacking = NPCGlobals.seenAttacking;
    NPCGlobals.unknown174 = NPCGlobals.seenOpponentCount;
    NPCGlobals.prevLostOpponentCount = NPCGlobals.lostOpponentCount;
    NPCGlobals.unknown188 = 0;
    NPCGlobals.seenAttacking = 0;
    NPCGlobals.seenOpponentCount = 0;
    NPCGlobals.lostOpponentCount = 0;
    NPCGlobals.enemies = 0;
    NPCGlobals.neutrals = 0;
    NPCGlobals.allies = 0;
    if (MusicVars.attackers <= MusicVars.calmThreshold)
        MusicVars.combat = 0;
    MusicVars.countingStarted = 0;
    MusicVars.attackers = 0;
    MusicVars.nearestOpponent = 1000.0f;
    MusicVars.nearestSeenOpponent = 1000.0f;
}

// ---- each drone's frame

// What the drone knows of its opponent this frame: where it is and faces (kept when there is none), the sighting
// passed to the drone's target if it is newer, the distance and direction to it, how far it faces from the drone
// (opponentFacing) and how far it stands off the way the drone faces, with its anim's turn (opponentBearing).
// Nothing while the AI is not running.
// AUTOINJECT
void Drone_GetOpponentInfo(Drone_tag *drone, obj_tag *gameObj) {
    if (!(drone->flags & 0x100))
        return;

    if (drone->opponent != NULL) {
        Vec_Copy(&drone->opponent->position, &drone->opponentLastPos);
        Vec_Copy(&drone->opponent->rotation, &drone->opponentLastRot);
        if (drone->targetSlot != NO_TARGET_SLOT) {
            DroneTarget_tag *target = &NPCGlobals.targets[drone->targetSlot];
            if ((target->flags & DRONE_TARGET_USED) && drone->opponentSeenFrame > target->seenFrame) {
                target->seenFrame = drone->opponentSeenFrame;
                Vec_Copy(&drone->opponentSeenPos, &target->pos);
                drone->seenFromPos = gameObj->position;
            }
        }
    }

    Vec_Subtract(&drone->opponentLastPos, &gameObj->position, &drone->opponentDelta);
    drone->distanceToTarget = Vec_Magnitude(&drone->opponentDelta);
    drone->opponentAngles.x = 0.0f;
    double yaw = Atan2(drone->opponentDelta.x, drone->opponentDelta.z);
    drone->opponentAngles.y = (float)yaw;
    drone->opponentAngles.z = 0.0f;
    drone->opponentFacing = Vec_AngleDifference(drone->opponentLastRot.y, (float)(yaw + kPi));

    uint turn = drone->unknown46c;
    float facing = gameObj->rotation.y;
    if (turn & 8)
        facing += kHalfPi;
    if (turn & 0x10)
        facing += kPi;
    if (turn & 0x20)
        facing -= kHalfPi;
    drone->opponentBearing = Vec_AngleDifference(facing, drone->opponentAngles.y);
}

// The start of the frame: the frame's flags cleared, how long the player has had the drone in his sights, the
// distance to each player, the mode-change channel (secondary mode DeleteMe deletes the drone, any other changes it
// to its attack mode and state; either ends the frame's control), and the state's timeout and the two timers, as
// messages. A Ninja's eyes. False only when the attack state change fails.
// AUTOINJECT
bool NDrone2_PreDroneControl(DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    gameObj->transformFlags &= ~TRANSFORM_MOVED;
    drone->prevAlertness = drone->alertness;
    drone->lastHit = NULL;
    drone->unknown38 = 0;
    drone->yawOffset = 0.0f;
    // On the x87, both frames converted unsigned
    if (GameState.NumFramesUnpaused > drone->inPlayersSightsFrame + FRAME_RATE_DIV * 30.0)
        drone->inPlayersSights = 0;
    if (drone->inPlayersSights)
        drone->inPlayersSightsFrames++;
    else
        drone->inPlayersSightsFrames = 0;

    for (int i = 0; i < 4; i++) {
        if (glb_players[i] != NULL)
            drone->playerDistance[i] = Vec_Dist3D(&gameObj->position, &glb_players[i]->position);
        else
            drone->playerDistance[i] = 100000.0f;
    }

    if (drone->modeChangeSwitchChannel != 0 && switch_channels[drone->modeChangeSwitchChannel]) {
        if (drone->secondaryMode == DMODE_DeleteMe) {
            dcv->gameObj->flags |= 1;
            drone->secondaryMode = 0;
            drone->modeChangeSwitchChannel = 0;
            return true;
        }
        if (drone->secondaryMode != 0) {
            drone->secondaryMode = 0;
            drone->modeChangeSwitchChannel = 0;
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)NDrone2_ChangeToAttackMode(drone), 0);
        }
    }

    uint now = GameState.NumFramesUnpaused;
    if (drone->stateTimeoutFrame != 0 && now >= drone->stateTimeoutFrame) {
        drone->stateTimeoutFrame = 0;
        Drone_SM_SendMsgSelf(DRONE_MSG_StateTimeout, NULL, 0, drone->sm.curState, dcv);
    }
    // A timer's third word is its message's payload
    if (drone->timerA.on && drone->timerA.dueFrame <= now) {
        void *data = (void *)(uintptr_t)drone->timerA.unknown8;
        drone->timerA.on = 0;
        drone->timerA.unknown8 = 0;
        Drone_SM_SendMsgSelf(DRONE_MSG_TimerA, data, 0, 0, dcv);
    }
    if (drone->timerB.on && drone->timerB.dueFrame <= now) {
        void *data = (void *)(uintptr_t)drone->timerB.unknown8;
        drone->timerB.on = 0;
        drone->timerB.unknown8 = 0;
        Drone_SM_SendMsgSelf(DRONE_MSG_TimerB, data, 0, 0, dcv);
    }
    if (drone->ninjaEyes)
        NDrone2_CreateNinjaEyes(dcv->gameObj);
    return true;
}

// A door the drone is dealing with, 60 x FRAME_RATE_DIV frames after doorFrame: let go of beyond 2 m, else opened if
// it is shut. Our name; nightfire-port has it inline in NDrone2_ControlSTANDARD.
// FUNC_AT(0x0003cbf0)
void NDrone2_HandleDoor(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *door = drone->door;
    if (door == NULL || door->objectType != OBJECTTYPE_DOOR)
        return;
    // On the x87, both frames converted unsigned
    if (!(GameState.NumFramesUnpaused > drone->doorFrame + FRAME_RATE_DIV * 60.0))
        return;
    if (Vec_Dist3D(&dcv->gameObj->position, &door->position) > 2.0f)
        drone->door = NULL;
    else if (!NDrone2_DoorIsOpen(dcv))
        NDrone2_OpenDoor(dcv);
}

// The control every drone type runs (DroneTypeSettings' control functions add to it): the alertness eased back
// towards its base over 50 s, the state machine's update message, the speech and facial anim finished; then, with
// the AI running, the drone counted by side, its opponent, its tracking (and its breath, drawn and alive), talking,
// moving (or flying), collision, firing, doors and explosives.
// AUTOINJECT
bool NDrone2_ControlSTANDARD(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *gameObj = dcv->gameObj;
    if (drone->alertness > 1.0f)
        drone->alertness = 1.0f;
    if (drone->alertness > drone->baseAlertness1 && drone->alertness < 1.0f)
        drone->alertness = (float)(drone->alertness - 1.0 / ((double)_FRAME_RATE * 50.0));
    if (drone->alertness < drone->baseAlertness1)
        drone->alertness = drone->baseAlertness1;

    Drone_SM_SendMsgSelf(DRONE_MSG_Update, NULL, 0, 0, dcv);
    if (dcv->drone->speechChannel != NULL && Sound_IsFinnished(dcv->drone->speechChannel))
        dcv->drone->speechChannel = NULL;
    Drone_tag *speaker = dcv->drone;
    if (speaker->facialAnimPlaying) {
        if (speaker->facialAnimDone || speaker->facialAnim == NULL
            || (((sAnimScript_tag *)speaker->facialAnim)->flags & 0x10000000))
            speaker->facialAnimPlaying = 0;
    }

    if (!(dcv->drone->flags & 0x100)) {
        NDrone2_DoTracking(0, dcv);
        return true;
    }

    switch (drone->side) {
    case 1:
        NPCGlobals.enemies++;
        break;
    case 2:
        NPCGlobals.allies++;
        break;
    case 3:
        NPCGlobals.neutrals++;
        break;
    }
    if (dcv->drone->dtype == DTYPE_Bot)
        BOT_setOtherPlayerInfo(dcv);
    NDrone2_FindOpponent(dcv->drone);

    if (gameObj->drawnCountdown && drone->health > 0.0f) {
        // Its breath, 0.12 m out from the head along the bone's z axis
        _VECTOR head, dir, breath;
        _MATRIX bone;
        AnimGetBoneWorldTrans(gameObj, WORLD_BONE(HEAD_BONE), 0, &head, &bone);
        _VECTOR forward = {0.0f, 0.0f, 1.0f};
        ApplyMatrixLV(&bone, &forward, &dir);
        auxVec_AddMulR32(&head, &dir, 0.12f, &breath);
        // in a phase of its own (docs/core-deviations.md): the original's is the low 12 bits of the object's address,
        // which differs from host to host; here the same 12 bits of a hash of the drone's number in the level
        Env_Breath(&breath, &dir, gameObj->inCel, gameObj->creationTimeFrames + BreathPhase(drone) * 0x80);
        NDrone2_DoTracking(1, dcv);
    } else {
        NDrone2_DoTracking(0, dcv);
    }
    NDrone2_HandleTalking(dcv);

    if (drone->flies) {
        NDrone2_Move(dcv);
    } else {
        drone->bunchFlags = 0;
        drone->nearestDrone = NULL;
        drone->nearestDroneDist = 0.0f;
        DroneMove_SetBoundryFlags(dcv);
        Vec_Copy(&gameObj->rotation, &drone->moveRotation);
        drone->moveRotation.y += drone->yawOffset;
        NDrone2_Move(dcv);
        drone->moveRotation.y -= drone->yawOffset;
        Vec_Copy(&drone->moveRotation, &gameObj->rotation);
        drone->unknown17 = DroneMove_NoBunching(dcv);
    }
    NDrone2_Collision(dcv->gameObj);
    DroneWeap_HandleFiring(dcv);
    NDrone2_HandleDoor(dcv);
    DroneFunc_HandleExplosives(dcv);
    return true;
}

// Zoe: once channel 1 is on, channel 0x9b too; while 0x9b is on, the player more than 10 m from her fails the
// mission (reason 15, inline in the original). Then the standard control.
// FUNC_AT(0x0003f8a0)
bool NDrone2_ControlDTYPE_Zoe(DCVars_tag *dcv) {
    if (switch_channels[1]) {
        switch_channels[0x9b] = 1;
        switch_channels_time[0x9b] = GameState.NumFramesUnpaused;
    }
    if (switch_channels[0x9b] && glb_players[0] != NULL
        && Vec_Dist3D(&dcv->gameObj->position, &glb_players[0]->position) > 10.0f)
        DroneFunc_SetMissionFailReason(dcv->drone, 15);
    return NDrone2_ControlSTANDARD(dcv);
}

// FUNC_AT(0x0003f910)
bool NDrone2_ControlDTYPE_Ninja(DCVars_tag *dcv) {
    NDrone2_CreateNinjaEyes(dcv->gameObj);
    return NDrone2_ControlSTANDARD(dcv);
}

// FUNC_AT(0x0003f930)
bool NDrone2_ControlDTYPE_Astronaut(DCVars_tag *dcv) {
    return NDrone2_ControlSTANDARD(dcv);
}

// The end of the control: with the AI running, the object is marked moved, and a drone out of health that is not
// yet dead is marked dying (0x400).
// AUTOINJECT
bool NDrone2_PostDroneControl(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->flags & 0x100) {
        dcv->gameObj->transformFlags |= TRANSFORM_MOVED;
        if (drone->health <= 0.0f && !(drone->flags & 0x200))
            dcv->drone->flags |= 0x400;
    }
    return true;
}

// Whether the drone's anim runs this frame (flag 0x80000): always for skin class 12, a drone with no root height, one
// drawn lately or one with unknown434's script; else unless its anim moves it without animating (unknown46c 4).
// AUTOINJECT
bool NDrone2_DoAnimation(Drone_tag *drone) {
    if (!(drone->flags & 0x80000))
        return false;
    if (drone->skinClass == 0xc)
        return true;
    obj_tag *gameObj = drone->gameObj;
    if (gameObj->animState->animObj.rootHeight == 0.0f || gameObj->drawnCountdown || drone->unknown434 != NULL)
        return true;
    return !(drone->unknown46c & 4);
}

// A drone's frame (the object's update): nothing while the NPC system is off, or in multiplayer for a dead drone or
// one that is no player's. PreDroneControl, then the type's control (or the standard one) unless PreDroneControl
// ended the frame, PostDroneControl; then the anim: run, or (not run, and not in unknown47c's frame) the drone moved
// along its heading by the anim's speed, held back from what blocks it ahead or behind when flag 1 is set and it has
// no door; the anim callbacks, the anim calls; how far it moved in the plane, and unknown3a0's length.
// AUTOINJECT
void Drone_Control(obj_tag *gameObj) {
    if (!NPCGlobals.enabled || Drone_bDisableSystem)
        return;
    if (MPSettings.dronesAreBots
        && (gameObj->objectType == OBJECTTYPE_DEAD_DRONE || Control_Plr2Ind(gameObj) < 0))
        return;

    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    DCVars_tag dcv;
    dcv.gameObj = gameObj;
    dcv.drone = drone;
    dcv.cel = gameObj->inCel;
    dcv.aiStateMachine = &drone->sm;
    if (NDrone2_PreDroneControl(&dcv)) {
        if (MPSettings.dronesAreBots
            && (gameObj->objectType == OBJECTTYPE_DEAD_DRONE || Control_Plr2Ind(gameObj) < 0))
            return;
        bool (*control)(DCVars_tag *dcv) = DroneTypeSettings[dcv.drone->dtype].control;
        if (control != NULL)
            control(&dcv);
        else
            NDrone2_ControlSTANDARD(&dcv);
    }
    NDrone2_PostDroneControl(&dcv);
    DroneAnim_SnapRotate(drone);

    if (drone->flags & 0x80000) {
        if (!NDrone2_DoAnimation(drone) && drone->unknown47c != GameState.NumFramesUnpaused) {
            drone->flags &= ~0x100000;
            uint flags = drone->flags;
            float speed = drone->animMoveSpeed;
            if (speed != 0.0f) {
                if ((flags & 1) && drone->door == NULL) {
                    if ((drone->boundaryFlags & 2) && speed > 0.0f)
                        speed = 0.0f;
                    if ((drone->boundaryFlags & 4) && speed < 0.0f)
                        speed = 0.0f;
                }
                dcv.gameObj->position.x = (float)(FSin(dcv.gameObj->rotation.y) * speed + dcv.gameObj->position.x);
                dcv.gameObj->position.z = (float)(FCos(dcv.gameObj->rotation.y) * speed + dcv.gameObj->position.z);
            }
        } else {
            AnimObjectUpdate(gameObj);
            // The original calls DroneAnim_SnapRotate2 here, an empty function on the Xbox
            drone->flags |= 0x100000;
        }
        if (drone->animFunc != NULL)
            drone->animFunc(drone, DANIMFUNC_FRAME);
        if (drone->animStateFunc != NULL)
            drone->animStateFunc(drone, DANIMFUNC_FRAME);
    }
    DroneAnim_CallHandler(&dcv);

    double dz = (double)gameObj->position.z - gameObj->lastPosition.z;
    double dx = (double)gameObj->position.x - gameObj->lastPosition.x;
    drone->moved = (float)sqrt(dz * dz + dx * dx);
    drone->unknown390 = Vec_Magnitude(&drone->unknown3a0);
}

// ---- deletion

// A drone going: its body glow, the cover node it holds, its route lists; everything that points at it (other
// drones' opponent, links and head tracking, bullets, cartridge cases), its group's spawner told, and a gun turret
// it belongs to blown up; its own head tracking off, and off the drone list.
// AUTOINJECT
void Drone_Delete(obj_tag *gameObj) {
    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    if (drone->bodyGlow != NULL) {
        BodyGlow_Delete(drone->bodyGlow);
        drone->bodyGlow = NULL;
    }
    for (uint i = 0; i < NPCGlobals.numCoverNodes; i++) {
        CoverNode_tag *node = &NPCGlobals.coverNodes[i];
        if (node->occupant == gameObj) {
            node->occupant = NULL;
            node->flags &= ~COVERNODE_WIDER;
        }
    }
    if (drone->route1.nodes != NULL)
        Mem_Free((void **)&drone->route1.nodes);
    if (drone->route2.nodes != NULL)
        Mem_Free((void **)&drone->route2.nodes);

    obj_tag *next;
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = next) {
        next = obj->nextObject;
        switch (obj->objectType) {
        case OBJECTTYPE_DRONE: {
            Drone_tag *other = (Drone_tag *)obj->extraObjectData;
            if (other->opponent == gameObj)
                other->opponent = NULL;
            if (other->opponentCursor == drone)
                other->opponentCursor = NULL;
            if (other->nearestDrone == drone)
                other->nearestDrone = NULL;
            if (other->unknown28c == drone)
                other->unknown28c = NULL;
            if (other->unknown230 == gameObj)
                other->unknown230 = NULL;
            if (other->lastAttacker == gameObj)
                other->lastAttacker = NULL;
            if (other->unknown23c == gameObj)
                other->unknown23c = NULL;
            if (other->headTarget == gameObj) {
                AnimObjectSetHeadTracking(obj, NULL, 3.0f, 0.14f, 1, 1, HEAD_BONE);
                other->headTarget = NULL;
                other->headTracking = 0;
            }
            break;
        }
        case OBJECTTYPE_BULLET: {
            BU_tag *bullet = (BU_tag *)obj->extraObjectData;
            if (bullet->firedByObj == gameObj)
                bullet->firedByObj = NULL;
            break;
        }
        case OBJECTTYPE_CASING: {
            CasingData *casing = (CasingData *)obj->extraObjectData;
            if (casing->unknown20 == gameObj)
                casing->unknown20 = NULL;
            break;
        }
        case OBJECTTYPE_DRONE_SPAWNER:
            if (((DroneSpawner_tag *)obj->extraObjectData)->group == (uint)drone->group)
                DroneSpawner_DroneDelete(obj, gameObj);
            break;
        case OBJECTTYPE_GUNTURRET: {
            GUNTURRET *turret = (GUNTURRET *)obj->extraObjectData;
            if (turret != NULL && turret->owner == gameObj) {
                obj->flags |= 1;
                Explode_Create(obj, &obj->position, &CONST_UP_VECTOR, 5.0f, 5.0f, HT_Script_BulletExplosion, 50.0f,
                               0xff,
                               0xff, 100, NULL, 0);
            }
            break;
        }
        }
    }

    AnimObjectSetHeadTracking(gameObj, NULL, 3.0f, 0.14f, 1, 1, HEAD_BONE);
    drone->headTarget = NULL;
    drone->headTracking = 0;
    LList_Remove(&NDrone2Info, (LLNODE_tag *)drone);
}
