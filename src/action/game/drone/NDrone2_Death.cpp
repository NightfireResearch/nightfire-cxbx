// A drone's death: the mission failing over it, its death channel, its last words and the captain's grenades, being
// set as dead, and fading out. Also which way an explosion that kills it came from.

#include "NDrone2.h"
#include "../obj/Pickup.h"
#include "DroneTables.h"
#include "DroneWeapon.h"
#include "Behaviour.h"
#include "DroneAnim.h"                 // DroneAnim_CallAnim, DroneAnim_LocationDeathAnim (ours)
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_Subtract, maybeAtan2, Vec_AngleDifference (ours)
#include "../obj/Switch.h"              // Switch_GetNearest, Switch_GetSwitchChannel (ours)
#include "../sp/Mission.h"              // Mission_SetFailLabel (ours)
#include "../sp/SwitchChannels.h"
#include "NDrone2_Opponent.h"
#include "../mp/multiplayer.h"          // MPSettings
#include "../sp/PlayerStats.h"          // PlrStat_LogEnemyDispatched (ours)
#include "../../sound/Sound.h"          // Sound_Play3D

#include <bit>
#include <math.h>

// The originals, called by address: Ghidra's prototypes have untyped arguments
#define DroneAnim_SetScript ((bool (__cdecl *)(HASHCODE script, ushort animState, ushort nextRow, uchar blend,                                              short endState, uint endMsg, uint flags, DCVars_tag *dcv))0x00034680)
#define NDrone2_SlideToPosition ((void (__cdecl *)(_VECTOR *pos, float speed, char turn, DCVars_tag *dcv))0x00043b20)

// How far obj is from the drone. Not named on PS2; the name is ours.
// FUNC_AT(0x0002faa0)
double NDrone2_DistanceToObj(DCVars_tag *dcv, obj_tag *obj) {
    return Vec_Dist3D(&dcv->gameObj->position, &obj->position);
}
// Returns in ST0, unrounded

// DroneInit.cpp
void NDrone2_Enable(char enable, Drone_tag *drone);

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

// An enemy killed by the player (by its opponent in multiplayer) counts as dispatched, unless it was knocked out
// AUTOINJECT
void NDrone2_LogDroneDeath(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *killer = MPSettings.maybeDroneAIEnabled == 0 ? NDrone2_Player() : drone->opponent;
    if (drone->lastAttacker == killer && !(drone->flags & 0x1000000) && drone->side == 1)
        PlrStat_LogEnemyDispatched(0);
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
        Pickup_CreateSimple(&dcv->gameObj->transformMatrix, 0x35, 2, 0);
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
        Pickup_CreateSimple(&dcv->gameObj->transformMatrix, 0x34, 1, 0);
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
    double turn = Vec_AngleDifference(gameObj->rotation.y, yaw);
    if (fabs(turn) < (float)M_PI_4)
        return 1;
    if (fabs(turn) > (float)(3 * M_PI_4))
        return 0;
    return turn < 0.0f ? 2 : 3;
}

// ---------------------------------------------------------------------------------------------------------------
// The death states

static void Death_SetTimer(DroneTimer *timer, uint dueFrame) {
    timer->dueFrame = dueFrame;
    timer->unknown8 = 0;
    timer->on = 1;
}

// GameState.NumFramesUnpaused plus ticks scaled by FRAME_RATE_DIV, as the original works it out in x87 and __ftol2
static uint Death_FramesFromNow(float ticks) {
    return (uint)(long long)((double)FRAME_RATE_DIV * ticks + (double)GameState.NumFramesUnpaused);
}

// With switch channel 0x35 on, a dying drone whose death channel is 0x88, 0x29 or 0x2a is taken away
#define CHANNEL_REMOVE_DYING 0x35

// DSTATE_DeathByExplosion's anim state; its variant is the side the explosion came from (NDrone2_ExplosionDir)
#define DANIM_DEATH_EXPLOSION 0x44

// Dying: the death anim, then Dead. Abseilers and astronauts have their own death, and a drone with a death script its
// special death. The death channel is set on leaving, or by timer A (HendersonB only, 90 ticks on). The drone drops
// its weapon unless it is still firing with an anim set other than 4 and 9.
// AUTOINJECT
bool NDrone2_DSTATE_Death_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        gameObj->effectFlags |= 0x20;
        if (GameState.CurrentLevelHashcode == HT_Level_Tower2Elevator)
            dcv->drone->flags |= 4;
        switch (dcv->drone->dtype) {
        case DTYPE_Abseil27:
        case DTYPE_Abseil28:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AbseilDeath, 0);
        case DTYPE_Astronaut:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautDeath, 0);
        default:
            break;
        }
        // stateScratch: whether the death channel is still to be set
        dcv->drone->stateScratch = 0;
        Drone_AlertStatusSet(3, dcv);
        if (dcv->drone->deathScript != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SpecialDeath_Anim, 0);
        DroneFunc_OnInitDeath(dcv);
        if (GameState.CurrentLevelHashcode == HT_Level_HendersonB)
            Death_SetTimer(&dcv->drone->timerA, Death_FramesFromNow(90.0f));
        drone->stateScratch = 1;
        DroneAnim_LocationDeathAnim(dcv, DSTATE_Dead);
        return true;

    case DRONE_MSG_Exit:
        if (dcv->drone->stateScratch != 0)
            DroneFunc_SetDeathChannel(drone);
        return true;

    case DRONE_MSG_Update:
        if (!drone->firing || drone->animSet == 4 || drone->animSet == 9)
            DroneWeap_DropWeapon(dcv);
        if (switch_channels[CHANNEL_REMOVE_DYING] != 0 &&
            (drone->deathSwitchChannel == 0x88 || drone->deathSwitchChannel == 0x29 ||
             drone->deathSwitchChannel == 0x2a)) {
            if (drone->stateScratch != 0) {
                DroneFunc_SetDeathChannel(drone);
                drone->stateScratch = 0;
            }
            NDrone2_Enable(0, dcv->drone);
            dcv->gameObj->flags |= 1;
        }
        return true;

    case DRONE_MSG_TimerA:
        if (drone->stateScratch != 0) {
            DroneFunc_SetDeathChannel(drone);
            drone->stateScratch = 0;
        }
        return true;

    default:
        return false;
    }
}

// Dying in an explosion: as Death_Anim, with the anim for the side the explosion came from (the state change's
// parameter is the impact).
// AUTOINJECT
bool NDrone2_DSTATE_DeathByExplosion(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        gameObj->effectFlags |= 0x20;
        Drone_AlertStatusSet(3, dcv);
        if (dcv->drone->deathScript != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SpecialDeath_Anim, 0);
        DroneFunc_OnInitDeath(dcv);
        if (GameState.CurrentLevelHashcode == HT_Level_HendersonB)
            Death_SetTimer(&dcv->drone->timerA, Death_FramesFromNow(90.0f));
        drone->stateScratch = 1;
        DroneAnim_CallAnim(DANIM_DEATH_EXPLOSION,
                           NDrone2_ExplosionDir(dcv, (DroneImpact_tag *)(uintptr_t)dcv->aiStateMachine->stateParam),
                           DSTATE_Dead, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Exit:
        if (dcv->drone->stateScratch != 0)
            DroneFunc_SetDeathChannel(drone);
        return true;

    case DRONE_MSG_Update:
        if (!dcv->drone->firing || drone->animSet == 4 || drone->animSet == 9)
            DroneWeap_DropWeapon(dcv);
        return true;

    case DRONE_MSG_TimerA:
        if (drone->stateScratch != 0) {
            DroneFunc_SetDeathChannel(drone);
            drone->stateScratch = 0;
        }
        return true;

    default:
        return false;
    }
}

// The death script whose death is played only 2 m or more from the player, with sound 0x148 at its end; a drone
// that surrenders drops it
#define DEATH_SCRIPT_21F 0x0600021f
#define SFX_DEATH_SCRIPT_21F 0x148

// The row played after a death script
#define DANIM_ROW_AFTER_DEATH_SCRIPT 0xbf

// Evaluates x twice (the original calls Vec_AngleDifference again for the value)
#define ABS(x) ((x) < 0.0f ? -(x) : (x))

static_assert(std::bit_cast<uint32_t>((float)M_PI_2) == 0x3fc90fdb, "not the original's pi/2");
static_assert(std::bit_cast<uint32_t>(0.05f) == 0x3d4ccccd, "not the original's 0.05");

// Dying by the drone's death script (placement key 13), when it stands within 1 m (in plan) and 90 degrees of where
// the script starts: the drone slides there and plays it, then lies dead (HostageDead for a hostage). Otherwise the
// script is dropped and it dies as usual. Timer A (60 ticks on) sets stateScratch.
// AUTOINJECT
bool NDrone2_DSTATE_SpecialDeath_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        gameObj->effectFlags |= 0x20;
        Drone_AlertStatusSet(3, dcv);
        // A NaN distance plays the script; NDrone2_Player() is not checked for NULL
        if (!(drone->deathScript == DEATH_SCRIPT_21F && NDrone2_DistanceToObj(dcv, NDrone2_Player()) < 2.0f) &&
            Vec_Dist2D(&dcv->gameObj->position, &dcv->drone->scriptedMovePos) < 1.0f &&
            ABS(Vec_AngleDifference(dcv->drone->scriptedMoveRot.y, dcv->gameObj->rotation.y)) < (float)M_PI_2 &&
            dcv->drone->deathScript != 0) {
            DroneFunc_OnInitDeath(dcv);
            DroneFunc_SetDeathChannel(drone);
            drone->flags &= ~0x80000000;
            Vec_Copy(&dcv->drone->scriptedMoveRot, &dcv->drone->moveTargetRot);
            DroneAnim_SetScript(dcv->drone->deathScript, 0x40, 0, 0, 0, DRONE_MSG_ScriptEnd, 0, dcv);
            drone->unknown1a = 1;
            drone->animEndState = dcv->drone->dtype == DTYPE_Hostage ? DSTATE_HostageDead : DSTATE_Dead;
            drone->animEndMsg = DRONE_MSG_ScriptEnd;
            drone->animNextRow = DANIM_ROW_AFTER_DEATH_SCRIPT;
            dcv->drone->flags &= ~0x20;
            dcv->drone->flags &= ~0x80;
            drone->stateScratch = 0;
            Death_SetTimer(&dcv->drone->timerA, Death_FramesFromNow(60.0f));
            return true;
        }
        if (drone->deathScript == DEATH_SCRIPT_21F)
            drone->deathSwitchChannel = 0;
        drone->deathScript = (HASHCODE)0;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);

    case DRONE_MSG_Update:
        NDrone2_SlideToPosition(&drone->scriptedMovePos, 0.05f, 1, dcv);
        if (!drone->firing || drone->animSet == 4 || drone->animSet == 9)
            DroneWeap_DropWeapon(dcv);
        return true;

    case DRONE_MSG_ScriptEnd:
        if (drone->deathScript == DEATH_SCRIPT_21F && glb_players[0] != NULL)
            Sound_Play3D((Action_SFX)SFX_DEATH_SCRIPT_21F, &glb_players[0]->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        if (dcv->drone->dtype == DTYPE_Hostage)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageDead, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Dead, 0);
        return true;

    case DRONE_MSG_TimerA:
        drone->stateScratch = 1;
        return true;

    default:
        return false;
    }
}

// Dead: the body lies for NPCGlobals.deadFrames (timer A), then fades. With property 0x31, unless shot in the head,
// it raises message 0x15 about itself on entering and again each time timer B runs out (60 ticks on). It is taken
// away once it has not been drawn for two ticks.
// AUTOINJECT
bool NDrone2_DSTATE_Dead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        gameObj->effectFlags |= 0x20;
        NDrone2_SetAsDead(dcv);
        Drone_AlertStatusSet(3, dcv);
        Death_SetTimer(&dcv->drone->timerA, NPCGlobals.deadFrames + GameState.NumFramesUnpaused);
        if (behaviour_util_getProperty(DRONE_PROP_31, dcv->drone->currentBehaviour) != 0 && !dcv->drone->headShot) {
            dcv->drone->flags2 |= 8;
            NDrone2_DroneAlertToObject(dcv, DRONE_MSG_DroneAlert, dcv->gameObj, 0.0f, 0.0f, 1.0f);
        }
        return true;

    case DRONE_MSG_Update:
        if (gameObj->drawnCountdown == 0) {
            NDrone2_Enable(0, dcv->drone);
            dcv->gameObj->flags |= 1;
        }
        DroneFunc_DeadDrone(dcv);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Fade, 0);
        return true;

    case DRONE_MSG_TimerB:
        if (behaviour_util_getProperty(DRONE_PROP_31, dcv->drone->currentBehaviour) == 0)
            return true;
        if (!drone->headShot) {
            dcv->drone->flags2 |= 8;
            NDrone2_DroneAlertToObject(dcv, DRONE_MSG_DroneAlert, dcv->gameObj, 0.0f, 0.0f, 1.0f);
        }
        Death_SetTimer(&dcv->drone->timerB, Death_FramesFromNow(60.0f));
        return true;

    default:
        return false;
    }
}

// Fading out from the object's alpha (NDrone2_FadeOut); taken away once faded, or not drawn for two ticks.
// AUTOINJECT
bool NDrone2_DSTATE_Fade(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        if (GameState.CurrentLevelHashcode != HT_Level_Tower2Elevator) {
            dcv->drone->flags &= ~0x20;
            dcv->drone->flags &= ~0x80;
        }
        dcv->drone->fadeAlpha = dcv->gameObj->tweakA;
        return true;

    case DRONE_MSG_Update:
        if (gameObj->drawnCountdown == 0 || NDrone2_FadeOut(dcv)) {
            NDrone2_Enable(0, dcv->drone);
            dcv->gameObj->flags |= 1;
        }
        DroneFunc_DeadDrone(dcv);
        return true;

    default:
        return false;
    }
}

// Fading out fast, with too many bodies about (DroneFunc_DeadDrone); from Fade it goes on from the alpha it had.
// AUTOINJECT
bool NDrone2_DSTATE_FadeFast(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        if (GameState.CurrentLevelHashcode != HT_Level_Tower2Elevator) {
            dcv->drone->flags &= ~0x20;
            dcv->drone->flags &= ~0x80;
        }
        if (drone->sm.prevState != DSTATE_Fade)
            dcv->drone->fadeAlpha = dcv->gameObj->tweakA;
        return true;

    case DRONE_MSG_Update:
        if (gameObj->drawnCountdown == 0 || NDrone2_FadeOut(dcv)) {
            NDrone2_Enable(0, dcv->drone);
            dcv->gameObj->flags |= 1;
        }
        return true;

    default:
        return false;
    }
}
