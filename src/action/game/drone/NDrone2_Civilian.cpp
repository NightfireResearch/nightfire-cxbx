// The civilians' states: where a civilian starts (CivilianInit), standing about, walking a patrol path, walking a
// mission path and waiting there for the player, and going back to the patrol path. Also the party girls, and the
// start of an ally that leads the player, the civilian guards (one standing about, and the pairs at doors), and
// civilians scared off and hiding. A civilian that sees an armed opponent (NDrone2_NearArmedPerson) is scared off,
// or with skin class 16 turns enemy and attacks.

#include "NDrone2_Civilian.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "NDrone2_Damage.h"
#include "Behaviour.h"
#include "../../game.h"                 // GameState
#include "NDrone2_Opponent.h"
#include "NDrone2_Route.h"
#include "DroneVision_Alerts.h"
#include "DroneModes.h"
#include "../../util/Random.h"

#include <bit>
#include <math.h>
#include "NDrone2_Alert.h"
#include "NDrone2_Combat.h"
#include "NDrone2_Roles.h"

// What NDrone2_UpdateMissionRoute returns at the end of the path; the route's status while it cannot go on
#define MISSION_ROUTE_DONE 3
#define AIROUTE_STATUS_9 9
#define AIROUTE_STATUS_C 0xc

#define SFX_CIVILIAN_SCARED 0x48

// The lines a door guard picks from (DroneFunc_ChooseSFX) as the opponent comes into zones 2 and 3
#define DOOR_GUARD_SFX_ZONE2 0x4f
#define DOOR_GUARD_SFX_ZONE2_LAST 0x50
#define DOOR_GUARD_SFX_ZONE3 0x8f
#define DOOR_GUARD_SFX_ZONE3_LAST 0x92

// Below this alertness a civilian waits with the stand idle anims, at or above it with anim state 0x21
#define CIVILIAN_ALERT_LOOK 0.66f
// The player is close enough to keep a civilian waiting within 3 m (in plan) and 30 degrees of its facing
#define CIVILIAN_WAIT_DISTANCE 3.0f
constexpr float CIVILIAN_WAIT_ANGLE = (float)(M_PI / 6.0);
static_assert(std::bit_cast<uint32_t>(CIVILIAN_WAIT_ANGLE) == 0x3f060a92, "not the original's pi/6");
static_assert(std::bit_cast<uint32_t>(0.76f) == 0x3f428f5c, "not the original's 0.76");
static_assert(std::bit_cast<uint32_t>(CIVILIAN_ALERT_LOOK) == 0x3f28f5c3, "not the original's 0.66");

static void Civilian_SetTimer(DroneTimer *timer, uint dueFrame, uint unknown8 = 0) {
    timer->dueFrame = dueFrame;
    timer->unknown8 = unknown8;
    timer->on = 1;
}

// Seeing an armed opponent: the drone is flagged scared, and once (with property 0x33) tells the drones within 20 m
// of the player (our name; each state has it inline)
static void Civilian_SeenArmedPerson(DCVars_tag *dcv) {
    dcv->drone->flags |= 0x8000000;
    if (behaviour_util_getProperty(DRONE_PROP_33, dcv->drone->currentBehaviour) != 0 &&
        !(dcv->drone->flags & 0x200000)) {
        dcv->drone->flags2 |= 1;
        NDrone2_DroneAlertToObject(dcv, 0x11, NDrone2_Player(), 20.0f, 20.0f, 0.76f);
        dcv->drone->flags |= 0x200000;
    }
}

// Picks the civilian's state from its paths: a mission path (with property 0x1d), else a patrol path, else it stands.
// Skin class 13 is a party girl.
// AUTOINJECT
bool NDrone2_DSTATE_CivilianInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->skinClass == 13)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PartyGirl, 0);
        if (behaviour_util_getProperty(DRONE_PROP_1D, dcv->drone->currentBehaviour) != 0) {
            NDrone2_AssignAIPath(dcv, AIPATH_MISSION);
            if (dcv->drone->route2.count != 0)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianMission, 0);
        }
        behaviour_util_setProperty(DRONE_PROP_1D, dcv->drone->currentBehaviour, 0);
        NDrone2_AssignAIPath(dcv, AIPATH_PATROL);
        if (dcv->drone->route2.count != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianPatrol, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Civilian, 0);
        return true;

    default:
        return false;
    }
}

// Standing about, talking to the player, until it sees an armed opponent. Skin class 13 is a party girl, 15 a truck
// driver.
// AUTOINJECT
bool NDrone2_DSTATE_Civilian(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->skinClass == 13)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PartyGirl, 0);
        if (dcv->drone->skinClass == 15)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverIdle, 0);
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        DroneAnim_SetStandIdleAnim(dcv, 0);
        dcv->drone->armedPersonRadius = GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 ? 10.0f : 20.0f;
        dcv->drone->stateScratch = 0xff;
        dcv->drone->stateScratch2 = 0;
        return true;

    case DRONE_MSG_Update:
        if (!NDrone2_NearArmedPerson(dcv)) {
            NDrone2_BondTalk(dcv);
            return true;
        }
        Civilian_SeenArmedPerson(dcv);
        if (dcv->drone->skinClass == 16) {
            dcv->drone->side = 1;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_CivilianScared, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_CivilianScared, 1);
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
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_CivilianScared);
        return true;

    default:
        return false;
    }
}

// Walking the patrol path (Idle without one). Seeing an armed opponent, it attacks; a drone with unknown15 set
// cries out first.
// AUTOINJECT
bool NDrone2_DSTATE_CivilianPatrol(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Exit:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->route2.count == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        DroneAnim_CallAnim(DANIM_WALK_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_UpdatePatrolRoute(dcv);
        if (!NDrone2_NearArmedPerson(dcv))
            return true;
        Civilian_SeenArmedPerson(dcv);
        if (dcv->drone->skinClass == 16) {
            dcv->drone->side = 1;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        if (dcv->drone->unknown15)
            NDrone2_PlaySFX(dcv, SFX_CIVILIAN_SCARED, NULL, 1, 1.0f);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);

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

// Back to the mission path: the drone walks to the path's nearest point, then patrols.
// AUTOINJECT
bool NDrone2_DSTATE_ReturnToPatrolPath(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        NDrone2_ReFindMissionPath(dcv);
        dcv->drone->initialState = DSTATE_CivilianPatrol;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
        return true;

    default:
        return false;
    }
}

// Walking the mission path, talking to the player. At its end the property 0x1d is cleared and the drone stands, or
// attacks if it has a second state (not in Tower2B, nor with a mode change channel); while the route cannot go on it
// waits. Message 0x22 makes it wait for the player. Skin class 19 is Kiko.
// AUTOINJECT
bool NDrone2_DSTATE_CivilianMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->skinClass == 19)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KikoMission, 0);
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        DroneAnim_CallAnim(DANIM_WALK_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->stateScratch = 0xff;
        dcv->drone->stateScratch2 = 0;
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedPerson(dcv)) {
            Civilian_SeenArmedPerson(dcv);
            if (dcv->drone->skinClass == 16) {
                dcv->drone->side = 1;
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            }
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);
        }
        NDrone2_BondTalk(dcv);
        if (NDrone2_UpdateMissionRoute(dcv) == MISSION_ROUTE_DONE) {
            behaviour_util_setProperty(DRONE_PROP_1D, dcv->drone->currentBehaviour, 0);
            if (GameState.CurrentLevelHashcode != HT_Level_Tower2B && dcv->drone->secondState != 0 &&
                dcv->drone->modeChangeSwitchChannel == 0)
                return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)NDrone2_ChangeToAttackMode(dcv->drone), 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Civilian, 0);
        }
        if (dcv->drone->route2.status == AIROUTE_STATUS_9 || dcv->drone->route2.status == AIROUTE_STATUS_C) {
            // A NaN alertness looks up
            if (dcv->drone->alertness < CIVILIAN_ALERT_LOOK)
                DroneAnim_SetStandIdleAnim(dcv, 0);
            else
                DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
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

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    case DRONE_MSG_ObjectHit:
        if (dcv->drone->skinClass == 19)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianMissionWait, 0);

    default:
        return false;
    }
}

// Waiting on the mission path: every two seconds (timer A, also restarted by message 0x22) it goes on unless the
// player stands close in front of it.
// AUTOINJECT
bool NDrone2_DSTATE_CivilianMissionWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        // A NaN alertness looks up
        if (dcv->drone->alertness < CIVILIAN_ALERT_LOOK)
            DroneAnim_SetStandIdleAnim(dcv, 0);
        else
            DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        Civilian_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        dcv->drone->stateScratch = 0xff;
        dcv->drone->stateScratch2 = 0;
        return true;

    case DRONE_MSG_Update:
        if (!NDrone2_NearArmedPerson(dcv)) {
            NDrone2_BondTalk(dcv);
            return true;
        }
        Civilian_SeenArmedPerson(dcv);
        if (dcv->drone->skinClass == 16) {
            dcv->drone->side = 1;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);

    case DRONE_MSG_TimerA:
        // A NaN distance goes on
        if (NDrone2_BondWithinAngle(dcv, CIVILIAN_WAIT_ANGLE) && NDrone2_Player() != NULL &&
            Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < CIVILIAN_WAIT_DISTANCE) {
            Civilian_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
            return true;
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianMission, 0);

    case DRONE_MSG_ObjectHit:
        Civilian_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
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

// An ally that leads the player: it takes a mission path (with property 0x1d) and, once the player is within 3 m,
// goes to AllyLeadWait. Without a path the property is cleared and it stands.
// AUTOINJECT
bool NDrone2_DSTATE_AllyLeadInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (behaviour_util_getProperty(DRONE_PROP_1D, dcv->drone->currentBehaviour) != 0)
            NDrone2_AssignAIPath(dcv, AIPATH_MISSION);
        dcv->drone->flags |= 0x10000;
        if (dcv->drone->route2.count != 0) {
            DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        }
        DroneAnim_SetStandIdleAnim(dcv, 0);
        behaviour_util_setProperty(DRONE_PROP_1D, dcv->drone->currentBehaviour, 0);
        return true;

    case DRONE_MSG_Update:
        // A NaN distance waits
        if (NDrone2_Player() != NULL &&
            Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < CIVILIAN_WAIT_DISTANCE &&
            dcv->drone->route2.count != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadWait, 0);
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

// A party girl starts on her mission path (with property 0x1d), else as a party girl.
// AUTOINJECT
bool NDrone2_DSTATE_PartyGirlInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (behaviour_util_getProperty(DRONE_PROP_1D, dcv->drone->currentBehaviour) != 0) {
            NDrone2_AssignAIPath(dcv, AIPATH_MISSION);
            if (dcv->drone->route2.count != 0)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianMission, 0);
        }
        behaviour_util_setProperty(DRONE_PROP_1D, dcv->drone->currentBehaviour, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PartyGirl, 0);
        return true;

    default:
        return false;
    }
}

// A party girl patrols if she has a patrol path, else stands about, talking to the player. Seeing an armed opponent
// she is scared off, and with property 0x52 the mission fails. Skin class 18 is a plain civilian.
// AUTOINJECT
bool NDrone2_DSTATE_PartyGirl(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->skinClass == 18)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Civilian, 0);
        NDrone2_AssignAIPath(dcv, AIPATH_PATROL);
        if (dcv->drone->route2.count != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianPatrol, 0);
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        dcv->drone->stateScratch = 0xff;
        dcv->drone->stateScratch2 = 0;
        return true;

    case DRONE_MSG_Update:
        if (!NDrone2_NearArmedPerson(dcv)) {
            DroneAnim_SetStandIdleAnim(dcv, 1);
            NDrone2_BondTalk(dcv);
            return true;
        }
        Civilian_SeenArmedPerson(dcv);
        if (behaviour_util_getProperty(DRONE_PROP_52, dcv->drone->currentBehaviour) != 0)
            DroneFunc_SetMissionFailReason(drone, 11);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_CivilianScared, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_CivilianScared, 1);
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
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_CivilianScared);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Guards, and civilians scared off

// A civilian guard: stands about (looking up from alertness 0.66), talking to the player, and attacks an armed
// opponent, or when it is itself armed and sees its opponent. It is an enemy once it leaves.
// AUTOINJECT
bool NDrone2_DSTATE_CivilianGuard(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        dcv->drone->initialState = DSTATE_Attack;
        // A NaN alertness stands idle
        DroneAnim_CallAnim(dcv->drone->alertness >= CIVILIAN_ALERT_LOOK ? 0x21 : DANIM_STAND_IDLE, 0, 0, 0, 0.0f, 0,
                           dcv);
        dcv->drone->stateScratch = 0xff;
        dcv->drone->stateScratch2 = 0;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->side = 1;
        return true;

    case DRONE_MSG_Update:
        if (!NDrone2_NearArmedPerson(dcv) &&
            !(NDrone2_ObjectIsArmed(gameObj) && (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT))) {
            NDrone2_BondTalk(dcv);
            return true;
        }
        Civilian_SeenArmedPerson(dcv);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);

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

// One of a door's two guards (its partner found on entering). It changes between three idle anims (timer A, 5 to 24
// seconds each, its data the next one), and by where the opponent is from the door (NDrone2_DoorGuardHandler) faces
// the way it guards, faces the opponent, or tells it to stop (anim state 0x5f); an opponent that gets through fails
// the mission and is attacked. stateScratch is the idle anim, stateScratch2 the zone it last reacted to (4 for none).
// AUTOINJECT
bool NDrone2_DSTATE_CivilianDoorGuard(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        NDrone2_SetDronePartner(DTYPE_CivDoorGuard, dcv);
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        dcv->drone->initialState = DSTATE_Attack;
        DroneAnim_CallAnim(DANIM_STAND_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->stateScratch = DANIM_STAND_IDLE;
        dcv->drone->stateScratch2 = 4;
        Civilian_SetTimer(&dcv->drone->timerA, Rand_Rand(25) * FRAME_RATE_INT + GameState.NumFramesUnpaused, 1);
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->side = 1;
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedPerson(dcv)) {
            Civilian_SeenArmedPerson(dcv);
            dcv->drone->side = 1;
            dcv->drone->initialState = DSTATE_Attack;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        switch (NDrone2_DoorGuardHandler(dcv)) {
        case 0:
            dcv->drone->angleToSomePoint = dcv->drone->diVars.rotation.y;
            if (dcv->drone->stateScratch2 != 0) {
                dcv->drone->stateScratch2 = 0;
                DroneAnim_CallAnim(dcv->drone->stateScratch, 0, 0, 0, 0.0f, 0, dcv);
            }
            return true;
        case 1:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (dcv->drone->stateScratch2 != 1) {
                dcv->drone->stateScratch2 = 1;
                DroneAnim_CallAnim(dcv->drone->stateScratch, 0, 0, 0, 0.0f, 0, dcv);
            }
            return true;
        case 2:
            if (dcv->drone->stateScratch2 != 2) {
                dcv->drone->stateScratch2 = 2;
                DroneAnim_CallAnim(0x5f, 1, 0, 0, 0.0f, 0, dcv);
                if (dcv->drone->guardZone != 0xff)
                    NDrone2_SpeechSFX(dcv, DroneFunc_ChooseSFX(DOOR_GUARD_SFX_ZONE2, DOOR_GUARD_SFX_ZONE2_LAST), 0, 1,
                                      1);
            }
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return true;
        case 3:
            NDrone2_SpeechSFX(dcv, DroneFunc_ChooseSFX(DOOR_GUARD_SFX_ZONE3, DOOR_GUARD_SFX_ZONE3_LAST), 0, 1, 1);
            DroneFunc_SetMissionFailReason(drone, 12);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        default:
            return true;
        }

    case DRONE_MSG_TimerA:
        switch ((uintptr_t)msg->extraData) {
        case 0:
            dcv->drone->stateScratch = DANIM_STAND_IDLE;
            DroneAnim_CallAnim(DANIM_STAND_IDLE, 0, 0, 0, 0.0f, 0, dcv);
            Civilian_SetTimer(&dcv->drone->timerA, (Rand_Rand(20) + 5) * FRAME_RATE_INT + GameState.NumFramesUnpaused,
                              1);
            return true;
        case 1:
            dcv->drone->stateScratch = DANIM_STAND_IDLE + 1;
            DroneAnim_CallAnim(DANIM_STAND_IDLE + 1, 0, 0, 0, 0.0f, 0, dcv);
            Civilian_SetTimer(&dcv->drone->timerA, (Rand_Rand(20) + 5) * FRAME_RATE_INT + GameState.NumFramesUnpaused,
                              2);
            return true;
        case 2:
            dcv->drone->stateScratch = DANIM_STAND_IDLE + 2;
            DroneAnim_CallAnim(DANIM_STAND_IDLE + 2, 0, 0, 0, 0.0f, 0, dcv);
            Civilian_SetTimer(&dcv->drone->timerA, (Rand_Rand(20) + 5) * FRAME_RATE_INT + GameState.NumFramesUnpaused,
                              0);
            return true;
        default:
            return true;
        }

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

// Scared off: unless its first attack gives it a state, it cries out and hides, a drone of type 7 running first to
// a point to run to. Impacts come back to the state it is in.
// AUTOINJECT
bool NDrone2_DSTATE_CivilianScared(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->aiStateMachine->scratch1c = DroneFunc_FirstAttack(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(2, dcv);
        dcv->drone->flags |= 0x400000;
        dcv->drone->flags |= 0x800000;
        NDrone2_CivilianScaredTalk(dcv);
        if (dcv->drone->dtype2 == DTYPE_RunToPoint && NDrone2_FindRunToPoint(dcv)) {
            dcv->drone->initialState = DSTATE_CivilianHiding;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageGoToGoalPosition, 0);
        }
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianHiding, 0);
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

// Hiding (anim state 0x19); impacts scare it again
// AUTOINJECT
bool NDrone2_DSTATE_CivilianHiding(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(2, dcv);
        dcv->drone->flags |= 0x10000;
        DroneAnim_CallAnim(0x19, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_CivilianScared, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_CivilianScared, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_CivilianScared);
        return true;

    default:
        return false;
    }
}
