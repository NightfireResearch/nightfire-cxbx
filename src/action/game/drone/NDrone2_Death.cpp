// A drone's death: the mission failing over it, its death channel, its last words and the captain's grenades, being
// set as dead, and fading out. Also which way an explosion that kills it came from.

#include "NDrone2.h"
#include "DroneTables.h"
#include "Behaviour.h"
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_Subtract, maybeAtan2, Vec_AngleDifference (ours)
#include "../obj/Switch.h"              // Switch_GetNearest, Switch_GetSwitchChannel (ours)
#include "../sp/Mission.h"              // Mission_SetFailLabel (ours)
#include "../sp/SwitchChannels.h"

#include <math.h>

// The originals, called by address: Ghidra's prototypes have untyped arguments
#define NDrone2_Player ((obj_tag *(__cdecl *)(void))0x00035f90)
#define DroneWeap_DropWeapon ((void (__cdecl *)(DCVars_tag *dcv))0x00067690)
#define NDrone2_LogDroneDeath ((void (__cdecl *)(DCVars_tag *dcv))0x0003a1d0)
#define Pickup_CreateSimple ((obj_tag *(__cdecl *)(_MATRIX *mtx, short weapon, short count, _VECTOR *pos))0x000a7f10)

// The switch channels the engine sets when a mission fails (docs/switch-channels.md)
#define CHANNEL_FAIL_CIVILIAN 0x60      // with TXT_MISSION_FAIL_KILLED_CIVILIAN
#define CHANNEL_FAIL 99                 // with TXT_MISSION_FAIL_COVER_LOST, or alone
#define CHANNEL_ALARM 0x61              // a switch on it fails with TXT_MISSION_FAIL_ALARM_TRIGGERED

// Fails the mission: 1, 3, 6 and 9 for killing a civilian, 2, 4, 5, 7, 8 and 10-12 with the cover lost, 16 by the
// switch nearest the drone (its channel, if it has one), anything else with no message.
// AUTOINJECT
void DroneFunc_SetMissionFailReason(Drone_tag *drone, uint reason) {
    switch (reason) {
    case 1:
    case 3:
    case 6:
    case 9:
        Mission_SetFailLabel(TXT_MISSION_FAIL_KILLED_CIVILIAN);
        SwitchChannel_SetActive(CHANNEL_FAIL_CIVILIAN);
        break;
    case 2:
    case 4:
    case 5:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
        Mission_SetFailLabel(TXT_MISSION_FAIL_COVER_LOST);
        SwitchChannel_SetActive(CHANNEL_FAIL);
        break;
    case 16: {
        ushort channel = Switch_GetSwitchChannel(Switch_GetNearest(&drone->gameObj->position));
        if (channel != 0) {
            if (channel == CHANNEL_ALARM)
                Mission_SetFailLabel(TXT_MISSION_FAIL_ALARM_TRIGGERED);
            // Not bounded by the table, unlike SwitchChannel_SetActive
            switch_channels[channel] = 1;
            switch_channels_time[channel] = GameState.NumFramesUnpaused;
        }
        break;
    }
    default:
        SwitchChannel_SetActive(CHANNEL_FAIL);
        break;
    }
}

// The drone is dead: it drops its weapon, and is flagged dead. Its health goes to 0 unless flags 0x1000000 is set.
// AUTOINJECT
void NDrone2_SetAsDead(DCVars_tag *dcv) {
    DroneWeap_DropWeapon(dcv);
    dcv->drone->flags |= 0x200;
    dcv->drone->flags &= ~0x8000;
    dcv->drone->unknown3f0 = 0;
    if (GameState.CurrentLevelHashcode != HT_Level_Tower2Elevator) {
        dcv->drone->flags &= ~0x20;
        dcv->drone->flags &= ~0x80;
    }
    dcv->gameObj->effectFlags |= 0x20;
    if (!(dcv->drone->flags & 0x1000000))
        dcv->drone->health = 0.0f;
}

// One frame of fading out: fadeAlpha counts down (0.6 a frame, 8 in FadeFast) into the object's alpha. True once
// it has gone.
// AUTOINJECT
bool NDrone2_FadeOut(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    float step = drone->sm.curState == DSTATE_FadeFast ? 8.0f : 0.6f;
    if (!(drone->fadeAlpha > 0.0f)) {
        dcv->gameObj->tweakA = 0;
        return true;
    }
    drone->fadeAlpha = drone->fadeAlpha - step;
    if (drone->fadeAlpha < 0.0f)
        drone->fadeAlpha = 0.0f;
    dcv->gameObj->tweakA = (uchar)(long long)drone->fadeAlpha;   // __ftol2, the low byte
    return false;
}

// With more dead drones about than NPCGlobals allows, the one it names fades fast.
// AUTOINJECT
bool DroneFunc_DeadDrone(DCVars_tag *dcv) {
    if (NPCGlobals.unknown150 > NPCGlobals.unknown14c && NPCGlobals.unknown15c == dcv->aiStateMachine->id) {
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_FadeFast, 0);
        return true;
    }
    return false;
}

// Sets the drone's death channel (placement key 12), if it has one. Channel 99 fails the mission instead, as a
// civilian killed or the cover lost depending on the side, whoever killed it.
// AUTOINJECT
bool DroneFunc_SetDeathChannel(Drone_tag *drone) {
    uchar channel = drone->deathSwitchChannel;
    if (channel == 0)
        return false;
    if (channel == CHANNEL_FAIL) {
        bool byPlayer = drone->lastAttacker == NDrone2_Player();
        if (drone->side == 1)
            DroneFunc_SetMissionFailReason(drone, byPlayer ? 2 : 4);
        else
            DroneFunc_SetMissionFailReason(drone, byPlayer ? 1 : 3);
        return true;
    }
    SwitchChannel_SetActive(channel);
    return true;
}

// As a drone starts to die: the mission fails if it must not (properties 0x53, or 0x54 when the player killed it),
// an enemy's death is logged, it cries out unless shot in the head, and a captain leaves grenades. An enemy with
// flags 0x1000000 set only leaves the grenades.
// AUTOINJECT
void DroneFunc_OnInitDeath(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    bool byPlayer = drone->lastAttacker != NULL && drone->lastAttacker->objectType == OBJECTTYPE_PLAYER;

    if (drone->side != 1) {
        if (behaviour_util_getProperty(DRONE_PROP_54, drone->currentBehaviour) != 0 && byPlayer)
            DroneFunc_SetMissionFailReason(drone, 1);
        else if (behaviour_util_getProperty(DRONE_PROP_53, dcv->drone->currentBehaviour) != 0)
            DroneFunc_SetMissionFailReason(drone, byPlayer ? 1 : 3);
        if (!dcv->drone->headShot)
            NDrone2_DeathTalk(dcv);
    } else if (!(drone->flags & 0x1000000)) {
        if (behaviour_util_getProperty(DRONE_PROP_54, drone->currentBehaviour) != 0 && byPlayer)
            DroneFunc_SetMissionFailReason(drone, 2);
        else if (behaviour_util_getProperty(DRONE_PROP_53, dcv->drone->currentBehaviour) != 0)
            DroneFunc_SetMissionFailReason(drone, byPlayer ? 2 : 4);
        NDrone2_LogDroneDeath(dcv);
        if (!dcv->drone->headShot)
            NDrone2_DeathTalk(dcv);
    }

    if (!drone->captain)
        return;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_HendersonA:
    case HT_Level_HendersonB:
    case HT_Level_HendersonC:
    case HT_Level_HendersonD:
    case HT_Level_CastleExterior:
    case HT_Level_CastleCourtyard:
    case HT_Level_CastleIndoors1:
    case HT_Level_CastleIndoors2:
        Pickup_CreateSimple(&dcv->gameObj->transformMatrix, 0x35, 2, NULL);
        break;
    case HT_Level_TowerA:
    case HT_Level_TowerB:
    case HT_Level_TowerC:
    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_EvilBase:
    case HT_Level_EvilSilo:
    case HT_Level_EvilBaseC:
    case HT_Level_Tower2Elevator:
        Pickup_CreateSimple(&dcv->gameObj->transformMatrix, 0x34, 1, NULL);
        break;
    default:
        break;
    }
}

// Which side of the drone an explosion went off: 1 in front (or with no explosion), 0 behind, 2 and 3 the sides.
// AUTOINJECT
uint NDrone2_ExplosionDir(DCVars_tag *dcv, DroneImpact_tag *impact) {
    obj_tag *gameObj = dcv->gameObj;
    if (impact == NULL)
        return 1;
    _VECTOR toBlast;
    Vec_Subtract(&impact->position, &gameObj->position, &toBlast);
    float yaw = maybeAtan2(toBlast.x, toBlast.z);
    float turn = Vec_AngleDifference(gameObj->rotation.y, yaw);
    if (fabsf(turn) < (float)M_PI_4)
        return 1;
    if (fabsf(turn) > (float)(3 * M_PI_4))
        return 0;
    return turn < 0.0f ? 2 : 3;
}
