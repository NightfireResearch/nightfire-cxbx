// The drone states a drone starts in (Global, WaitSwitch, Disabled), the idle, alert and patrol states, the run to an
// AI point, and the small DeleteMe, JustStand and WaitForever. See docs/drone/architecture/README.md 3.

#include "NDrone2_States.h"
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "NDrone2_Damage.h"
#include "DroneVision_Alerts.h"
#include "NDrone2_Route.h"
#include "../sp/SwitchChannels.h"       // switch_channels
#include "../../game.h"                 // GameState
#include "../../util/Random.h"          // Rand_Rand
#include "../../math/X87Ops.h"
#include "../../engine/Collide.h"       // HITDATA_tag

#include <math.h>

// DroneInit.cpp
void NDrone2_Enable(char enable, Drone_tag *drone);

// The originals, called by address: Ghidra's prototypes have untyped arguments
// FUN_00046c20; the PS2 build's name
// Takes nothing and answers DSTATE_Obstructed; the caller passes dcv all the same

// The anim states these ask for (our names)
#define DANIM_ALERT_STAND 0x21
#define DANIM_ALERT_LOOK 0x22
#define DANIM_PATROL_ALERT_LOOK 0x36
#define DANIM_IDLE_FIDGET 0x4b

// From this alertness Idle sends a drone to Alert, and Idle and Patrol use the alert anims (our name)
#define ALERT_SUSPICIOUS 0.66f

// What every drone does with a message its state did not handle (Drone_SM_RouteMsgDCV). On entering it goes to its
// start: a bot to BotInit, one waiting for a switch channel that was off at init to WaitSwitch, a truck driver to
// its own start, one with a script to PlayScript, any other to its initial state. A live hostage killer attacks when
// told; DRONE_MSG_ForceState changes to the state it carries.
// AUTOINJECT
bool NDrone2_DSTATE_Global(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->dtype == DTYPE_Bot)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotInit, 0);
        if (dcv->drone->waitSwitchChannel != 0 && !dcv->drone->waitSwitchInitial)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_WaitSwitch, 0);
        if (drone->dtype == DTYPE_TruckDriver)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverInit, 0);
        if (dcv->drone->playScript != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PlayScript, 0);
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    case DRONE_MSG_AttackNow:
        if ((dcv->drone->flags & DRONE_FLAG_ENABLED) && dcv->drone->dtype == DTYPE_HostageKiller &&
            dcv->drone->health > 0.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageKillerAttack, 0);
        return true;

    case DRONE_MSG_ForceState:
        if (msg->extraData != NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)(uintptr_t)msg->extraData, 0);
        return true;

    default:
        return false;
    }
}

// Switched off until its switch channel comes on (checked each frame) or DRONE_MSG_Enable arrives; then on, to its
// start.
// AUTOINJECT
bool NDrone2_DSTATE_WaitSwitch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        NDrone2_Enable(0, dcv->drone);
        return true;

    case DRONE_MSG_Update:
        if (!switch_channels[dcv->drone->waitSwitchChannel])
            return true;
        NDrone2_Enable(1, dcv->drone);
        if (dcv->drone->dtype == DTYPE_TruckDriver)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverInit, 0);
        if (dcv->drone->playScript != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PlayScript, 0);
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);

    case DRONE_MSG_Enable:
        NDrone2_Enable(1, dcv->drone);
        // no truck driver test here, and true whatever the change to the initial state answers
        if (dcv->drone->playScript != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PlayScript, 0);
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    default:
        return false;
    }
}

// Waits for the drone's AI to be switched on, then goes on as Global's Enter does, but to the state in stateScratch
// rather than the initial one.
// AUTOINJECT
bool NDrone2_DSTATE_Disabled(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Enter:
        return true;

    case DRONE_MSG_Update:
        if (!(dcv->drone->flags & DRONE_FLAG_ENABLED))
            return true;
        if (dcv->drone->waitSwitchChannel != 0 && !dcv->drone->waitSwitchInitial)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_WaitSwitch, 0);
        if (drone->dtype == DTYPE_TruckDriver)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverInit, 0);
        if (dcv->drone->playScript != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PlayScript, 0);
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)drone->stateScratch, 0);
        return true;

    default:
        return false;
    }
}

// Standing still. A drone with a mission route (taken on entering when property 0x1d asks for one) goes on it; a
// suspicious one goes to Alert; the rest look for an opponent each frame and fidget now and then (property 0x46).
// Skin class 0x14 in SpaceStationD is the SpaceDrake.
// AUTOINJECT
bool NDrone2_DSTATE_Idle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (GameState.CurrentLevelHashcode == HT_Level_SpaceStationD && dcv->drone->skinClass == SKINCLASS_DRAKE)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SpaceDrake, 0);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        if (behaviour_util_getProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour) != 0)
            NDrone2_AssignAIPath(dcv, AIPATH_MISSION);
        if (dcv->drone->route2.count > 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_EnemyMission, 0);
        behaviour_util_setProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour, 0);
        if (dcv->drone->alertness >= ALERT_SUSPICIOUS)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Alert, 0);
        DroneAnim_SetStandIdleAnim(dcv, 0);
        if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0)
            NDrone2_SetIdleTimeOut(dcv, 45, 5);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        DroneAnim_SetStandIdleAnim(dcv, 1);
        return true;

    case DRONE_MSG_StateTimeout:
        if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0) {
            if (dcv->drone->alertness >= ALERT_SUSPICIOUS)
                DroneAnim_CallAnim(DANIM_ALERT_LOOK, 0, 0, 0, 0.0f, 0, dcv);
            else
                DroneAnim_CallAnim(DANIM_IDLE_FIDGET, Rand_Rand(3), 0, 0, 0.0f, 0, dcv);
            NDrone2_SetIdleTimeOut(dcv, 45, 5);
        }
        return true;

    case DRONE_MSG_ScriptEnd:
        if (dcv->drone->route2.count > 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_EnemyMission, 0);
        if (dcv->drone->alertness >= ALERT_SUSPICIOUS)
            DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        else
            DroneAnim_SetStandIdleAnim(dcv, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// Standing alerted: as Idle, but always in the alert stance, looking round now and then (property 0x46).
// AUTOINJECT
bool NDrone2_DSTATE_Alert(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        if (behaviour_util_getProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour) != 0)
            NDrone2_AssignAIPath(dcv, AIPATH_MISSION);
        if (dcv->drone->route2.count > 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_EnemyMission, 0);
        behaviour_util_setProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour, 0);
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0)
            NDrone2_SetIdleTimeOut(dcv, 45, 5);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_StateTimeout:
        if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0) {
            DroneAnim_CallAnim(DANIM_ALERT_LOOK, 0, 0, 0, 0.0f, 0, dcv);
            NDrone2_SetIdleTimeOut(dcv, 45, 5);
        }
        return true;

    case DRONE_MSG_ScriptEnd:
        if (dcv->drone->route2.count > 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_EnemyMission, 0);
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// A patroller (property 0x24 or 0x25) takes a patrol path (AI path type 4) and walks it; without one, or without
// either property, it stands idle.
// AUTOINJECT
bool NDrone2_DSTATE_InitPatrol(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (behaviour_util_getProperty(DRONE_PROP_PATROL, dcv->drone->currentBehaviour) == 0 &&
            behaviour_util_getProperty(DRONE_PROP_ALERT_PATROL, dcv->drone->currentBehaviour) == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        NDrone2_AssignAIPath(dcv, AIPATH_PATROL);
        if (dcv->drone->route2.count > 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Patrol, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        return true;

    default:
        return false;
    }
}

// Walking the patrol route (route2), muttering now and then and looking for an opponent each frame. With property
// 0x46 it pauses to look round: a suspicious drone that follows its patrol path (property 0x25) sooner and in the
// alert stance.
// AUTOINJECT
bool NDrone2_DSTATE_Patrol(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        if (behaviour_util_getProperty(DRONE_PROP_ALERT_PATROL, dcv->drone->currentBehaviour) != 0 &&
            dcv->drone->alertness >= ALERT_SUSPICIOUS) {
            if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0)
                NDrone2_SetIdleTimeOut(dcv, 5, 10);
        } else if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0) {
            NDrone2_SetIdleTimeOut(dcv, 45, 10);
        }
        return true;

    case DRONE_MSG_Update:
        NDrone2_PatrolTalk(dcv);
        NDrone2_UpdatePatrolRoute(dcv);
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_StateTimeout:
        // no new timeout: it looks round once
        if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0) {
            if (behaviour_util_getProperty(DRONE_PROP_ALERT_PATROL, dcv->drone->currentBehaviour) != 0 &&
                dcv->drone->alertness >= ALERT_SUSPICIOUS) {
                DroneAnim_CallAnim(DANIM_PATROL_ALERT_LOOK, 0, 0, 0, 0.0f, 0, dcv);
                return true;
            }
            DroneAnim_CallAnim(DANIM_IDLE_FIDGET, Rand_Rand(3), DSTATE_Patrol, 0, 0.0f, 0, dcv);
        }
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case 0x1b:
        Drone_SM_SetState(dcv->aiStateMachine, NDrone2_ObstructedPatrolPath(dcv), 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// An enemy running to an AI point (NDrone2_FindRunToPoint), or with property 0x56 to the alarm, then attacking. In
// RefRoom and Tower2A-C, a drone not in DRONE_FLAG_ATTACK_MODE that is not yet fully alert stands idle or alert there
// instead. With no point it goes straight to that state.
// AUTOINJECT
bool NDrone2_DSTATE_EnemyRunToPoint(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        if (behaviour_util_getProperty(DRONE_PROP_RUNS_TO_ALARM, dcv->drone->currentBehaviour) != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunToAlarm, 0);
        dcv->drone->initialState = DSTATE_Attack;
        switch (GameState.CurrentLevelHashcode) {
        case HT_Level_RefRoom:
        case HT_Level_Tower2A:
        case HT_Level_Tower2B:
        case HT_Level_Tower2C:
            if (!(dcv->drone->flags & DRONE_FLAG_ATTACK_MODE)) {
                if (dcv->drone->alertness < ALERT_SUSPICIOUS)
                    dcv->drone->initialState = DSTATE_Idle;
                else if (dcv->drone->alertness < 1.0f)
                    dcv->drone->initialState = DSTATE_Alert;
            }
            break;
        default:
            break;
        }
        if (!NDrone2_FindRunToPoint(dcv)) {
            Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
            return true;
        }
        dcv->drone->returnState = dcv->drone->initialState;
        if (dcv->drone->side == 1)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageGoToGoalPosition, 0);

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    default:
        return false;
    }
}

// Switched off and marked for deletion on entering
// AUTOINJECT
bool NDrone2_DSTATE_DeleteMe(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        NDrone2_Enable(0, dcv->drone);
        dcv->gameObj->flags |= 1;
        return true;

    default:
        return false;
    }
}

// Stands, turning targetRot.y by 0.01 a frame and moving the object round a circle of radius 0.01 with the
// frame count. A shot sends it back here.
// AUTOINJECT
bool NDrone2_DSTATE_JustStand(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Enter:
    case DRONE_MSG_ScriptEnd:
        return true;

    case DRONE_MSG_Update: {
        drone->targetRot.y += 0.01f;
        drone->standingStill = 1;
        // On the x87: FSIN and FCOS of the unrounded product, rounded once at each store
        double phase = (double)GameState.NumFramesUnpaused * 0.005f;
        gameObj->position.x = (float)(FSin(phase) * Extended(0.01f) + Extended(gameObj->position.x));
        gameObj->position.z = (float)(FCos(phase) * Extended(0.01f) + Extended(gameObj->position.z));
        return true;
    }

    case DRONE_MSG_ImpactBullet:
        NDrone2_BulletImpact(dcv, (HITDATA_tag *)msg->extraData, DSTATE_JustStand, 1);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_WaitForever(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return msg->msgType == DRONE_MSG_Null || msg->msgType == DRONE_MSG_Enter;
}
