// Hostage killers and their hostages. A killer takes the free hostage nearest it (Drone_tag.unknown230 points each at
// the other), the two face each other and take turns to talk; told to attack, the killer executes the hostage. A
// hostage whose killer is dead is saved: it counts, sets its mode change channel and hides from armed enemies.

#include "NDrone2_Hostage.h"
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "DroneVision_Alerts.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Route.h"
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_Copy, Vec_Subtract, maybeAtan2, Vec_Dist3D (ours)
#include "../../sound/music.h"          // Music_Event
#include "../../util/Random.h"
#include "../sp/SwitchChannels.h"

#include <float.h>
#include "NDrone2.h"

// A killer takes a hostage within 5 m
#define HOSTAGE_RANGE 5.0f

// The music event a saved hostage plays
#define MUSIC_HOSTAGE_SAVED 0x10

// Empty. The Xbox's call goes to an empty function the linker shares with others (0x000e0ec0).
static void NDrone2_HostageSituationActive(DCVars_tag *dcv) {
}

static Drone_tag *Hostage_Partner(Drone_tag *drone) {
    return (Drone_tag *)drone->unknown230->extraObjectData;
}

// The killer and its hostage face each other: each moves towards the other, turned to it.
// AUTOINJECT
void NDrone2_SetupHostageExecute(DCVars_tag *dcv) {
    obj_tag *killerObj = dcv->gameObj;
    obj_tag *hostageObj = dcv->drone->unknown230;
    if (hostageObj == NULL || killerObj == NULL)
        return;
    Drone_tag *hostage = (Drone_tag *)hostageObj->extraObjectData;
    Drone_tag *killer = (Drone_tag *)killerObj->extraObjectData;
    Vec_Copy(&hostageObj->position, &killer->moveTarget);
    killer->moveTargetCel = hostageObj->inCel;
    Vec_Copy(&killerObj->position, &hostage->moveTarget);
    hostage->moveTargetCel = killerObj->inCel;
    _VECTOR delta;
    Vec_Subtract(&killerObj->position, &hostageObj->position, &delta);
    killer->moveTargetRot.x = 0.0f;
    killer->moveTargetRot.y = maybeAtan2(-delta.x, -delta.z);
    killer->moveTargetRot.z = 0.0f;
    hostage->moveTargetRot.x = 0.0f;
    hostage->moveTargetRot.y = maybeAtan2(delta.x, delta.z);
    hostage->moveTargetRot.z = 0.0f;
}

// A killer without a hostage takes the nearest free one (DTYPE_Hostage) within 5 m; false if there is none.
// AUTOINJECT
bool NDrone2_SetupHostageKiller(DCVars_tag *dcv) {
    if (dcv->drone->unknown230 != NULL)
        return true;
    Drone_tag *chosen = NULL;
    float best = FLT_MAX;
    for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
        if (other->dtype != DTYPE_Hostage || other->unknown230 != NULL)
            continue;
        double dist = Vec_Dist3D(&other->gameObj->position, &dcv->gameObj->position);
        if (dist < best && dist < HOSTAGE_RANGE) {
            best = (float)dist;
            chosen = other;
        }
    }
    if (chosen == NULL)
        return false;
    dcv->drone->unknown230 = chosen->gameObj;
    chosen->unknown230 = dcv->gameObj;
    NDrone2_SetupHostageExecute(dcv);
    return true;
}

// The killer and the hostage take turns to talk (message 0x1a), each once the other has finished: unknown3ca is 0
// before the killer's first line, 1 while the killer talks, 2 while the hostage does.
// AUTOINJECT
bool NDrone2_MonitorTalkToMissionObj(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    switch (drone->unknown3ca) {
    case 0:
        Drone_Message(dcv->gameObj, DRONE_MSG_TalkToMissionObject, NULL, 0);
        drone->unknown3ca++;
        break;

    case 1:
        if (drone->speechChannel != NULL) {
            if (!Sound_IsFinnished(drone->speechChannel))
                return true;
            drone->speechChannel = NULL;
        }
        Drone_Message(dcv->drone->unknown230, DRONE_MSG_TalkToMissionObject, NULL, 0);
        dcv->drone->unknown3ca++;
        break;

    case 2: {
        Drone_tag *hostage = Hostage_Partner(drone);
        if (hostage->speechChannel != NULL) {
            if (!Sound_IsFinnished(hostage->speechChannel))
                return true;
            hostage->speechChannel = NULL;
        }
        Drone_Message(dcv->gameObj, DRONE_MSG_TalkToMissionObject, NULL, 0);
        dcv->drone->unknown3ca = 1;
        break;
    }
    }
    return true;
}

// One of the killer's six lines or the hostage's four; false for any other drone.
// AUTOINJECT
bool NDrone2_TalkToMissionObj(DCVars_tag *dcv) {
    uint sfx;
    switch (dcv->drone->dtype) {
    case DTYPE_HostageKiller:
        sfx = Rand_Rand(6) + 0x29;
        break;
    case DTYPE_Hostage:
        sfx = Rand_Rand(4) + 0x2f;
        break;
    default:
        return false;
    }
    if (sfx != 0)
        NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return true;
}

// Holding a hostage (without one it goes idle), talking with it. Message 0xa starts the execution while the hostage
// lives; at the script's end the hostage is told it dies and the killer attacks.
// AUTOINJECT
bool NDrone2_DSTATE_HostageKiller(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        if (!NDrone2_SetupHostageKiller(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        dcv->drone->flags |= 0x10;
        DroneAnim_CallAnim(9, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_MonitorTalkToMissionObj(dcv);
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_TalkToMissionObject:
        NDrone2_TalkToMissionObj(dcv);
        return true;

    case DRONE_MSG_AttackNow:
        if (Hostage_Partner(dcv->drone)->health > 0.0f)
            DroneAnim_CallAnim(0x58, 0, 0, DRONE_MSG_ScriptEnd, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ScriptEnd:
        Drone_SM_SendMsg(DRONE_MSG_ToHostages, DSTATE_Hostage, dcv->aiStateMachine->id,
                         Hostage_Partner(dcv->drone)->sm.id);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
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

// With its hostage alive it turns to it and a second later (message 0xa) executes it; otherwise it attacks, without
// flag 0x10 when it came from WaitSwitch or has no death script.
// AUTOINJECT
bool NDrone2_DSTATE_HostageKillerAttack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(1, dcv);
        if (dcv->drone->unknown230 != NULL && Hostage_Partner(dcv->drone)->health > 0.0f) {
            NDrone2_SetupHostageExecute(dcv);
            Drone_SM_SendMsgSelf(DRONE_MSG_AttackNow, NULL, FRAME_RATE_INT, 0, dcv);
            DroneAnim_CallAnim(9, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        }
        if (dcv->aiStateMachine->curState == DSTATE_WaitSwitch || dcv->drone->deathScript == 0)
            dcv->drone->flags &= ~0x10;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_AttackNow:
        if (dcv->drone->unknown230 != NULL) {
            if (dcv->drone->deathScript == 0)
                dcv->drone->flags &= ~0x10;
            DroneAnim_CallAnim(0x58, 0, 0, DRONE_MSG_ScriptEnd, 0.0f, 0, dcv);
        }
        return true;

    case DRONE_MSG_ScriptEnd:
        Drone_SM_SendMsg(DRONE_MSG_ToHostages, DSTATE_Hostage, dcv->aiStateMachine->id,
                         Hostage_Partner(dcv->drone)->sm.id);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// Held while its killer lives; without one, or with it dead, it is saved. Message 0xb from the killer kills it.
// AUTOINJECT
bool NDrone2_DSTATE_Hostage(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(2, dcv);
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update: {
        obj_tag *killer = dcv->drone->unknown230;
        // A NaN health is dead
        if (killer == NULL || !(((Drone_tag *)killer->extraObjectData)->health > 0.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageSaved, 0);
        NDrone2_HostageSituationActive(dcv);
        return true;
    }

    case DRONE_MSG_ScriptEnd:
        DroneAnim_SetDAnimNow(0xf4, 0, 0, dcv);
        return true;

    case DRONE_MSG_ToHostages:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageDie, 0);
        return true;

    case DRONE_MSG_TalkToMissionObject:
        NDrone2_TalkToMissionObj(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// Executed: a cry, the death anim into HostageDead, and the death logged.
// AUTOINJECT
bool NDrone2_DSTATE_HostageDie(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(3, dcv);
        NDrone2_PlaySFX(dcv, 0xa, NULL, 1, 1.0f);
        DroneAnim_CallAnim(DANIM_DEATH, 0, DSTATE_HostageDead, 0, 0.0f, 0, dcv);
        NDrone2_LogDroneDeath(dcv);
        return true;

    default:
        return false;
    }
}

// Saved: it counts (DroneFunc_HostageSaved). With a second state and no mode change channel it stands, or hides if
// an armed person is near; otherwise it runs to a point and hides there, or hides where it is.
// AUTOINJECT
bool NDrone2_DSTATE_HostageSaved(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(0, dcv);
        dcv->drone->unknown230 = NULL;
        Music_Event(MUSIC_HOSTAGE_SAVED, 1);
        DroneFunc_HostageSaved(dcv);
        if (dcv->drone->secondState != 0 && dcv->drone->modeChangeSwitchChannel == 0) {
            if (NDrone2_NearArmedPerson(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageHide, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageIdle, 0);
        }
        if (NDrone2_FindRunToPoint(dcv)) {
            dcv->drone->initialState = DSTATE_HostageHide;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageGoToGoalPosition, 0);
        }
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageHide, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// Calm (property 0x4c, alertness 0), facing the opponent, until an armed enemy is within 20 m.
// AUTOINJECT
bool NDrone2_DSTATE_HostageIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->sightFlags |= DRONE_SIGHT_REACTED;
        behaviour_util_setProperty(DRONE_PROP_4C, dcv->drone->currentBehaviour, 1);
        drone->alertness = 0.0f;
        drone->flags &= ~DRONE_FLAG_ATTACK_MODE;
        Drone_AlertStatusSet(0, dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        DroneAnim_CallAnim(DANIM_STAND_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedDrone(dcv, 20.0f, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageHide, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_HostageHide, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_HostageHide, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_HostageHide);
        return true;

    default:
        return false;
    }
}

// Cowering (alertness 1) until no armed enemy is within 20 m.
// AUTOINJECT
bool NDrone2_DSTATE_HostageHide(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        drone->alertness = 1.0f;
        drone->flags |= DRONE_FLAG_ATTACK_MODE;
        Drone_AlertStatusSet(2, dcv);
        DroneAnim_CallAnim(0x19, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (!NDrone2_NearArmedDrone(dcv, 20.0f, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageIdle, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, 0);
        return true;

    default:
        return false;
    }
}

// Dead: with property 0x53 the mission fails (reason 9). It fades when timer A runs out, NPCGlobals.deadFrames on.
// AUTOINJECT
bool NDrone2_DSTATE_HostageDead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter: {
        Drone_AlertStatusSet(3, dcv);
        NDrone2_SetAsDead(dcv);
        if (behaviour_util_getProperty(DRONE_PROP_53, dcv->drone->currentBehaviour) != 0)
            DroneFunc_SetMissionFailReason(drone, 9);
        dcv->drone->flags &= ~0x8000;
        DroneTimer *timer = &dcv->drone->timerA;
        timer->dueFrame = GameState.NumFramesUnpaused + NPCGlobals.deadFrames;
        timer->unknown8 = 0;
        timer->on = 1;
        return true;
    }

    case DRONE_MSG_Update:
        DroneFunc_DeadDrone(dcv);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Fade, 0);
        return true;

    default:
        return false;
    }
}

// Shows the hostage count's message. The original takes the count in EAX; DroneFunc_HostageSaved's pushes are not
// read.
// UNINJECTABLE - custom calling convention and only used from DroneFunc_HostageSaved
void DroneFunc_NotifyHostageSaved(uint numHostagesSaved) {
    char *str = (char *)Txt_BindLabel(numHostagesSaved == 1 ? NOTIF_ONE_HOSTAGE_RELEASED : NOTIF_ALL_HOSTAGES_RELEASED, 0);
    Text_AddMsg(NULL, NULL, 1, str, 0, 180);
}

// A hostage saved: once per mode change channel, it counts and sets the channel; in HendersonB and HendersonC the
// count is shown.
// AUTOINJECT
void DroneFunc_HostageSaved(DCVars_tag *dcVars) {
    const int switchChannel = dcVars->drone->modeChangeSwitchChannel;
    if (switch_channels[switchChannel])
        return;
    NPCGlobals.hostagesSaved++;
    SwitchChannel_SetActive(switchChannel);
    if (GameState.CurrentLevelHashcode == HT_Level_HendersonB || GameState.CurrentLevelHashcode == HT_Level_HendersonC)
        DroneFunc_NotifyHostageSaved(NPCGlobals.hostagesSaved);
}
