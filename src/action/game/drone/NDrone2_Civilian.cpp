// The civilians' states: where a civilian starts (CivilianInit), standing about, walking a patrol path, walking a
// mission path and waiting there for the player, and going back to the patrol path. Also the party girls, and the
// start of an ally that leads the player. A civilian that sees an armed opponent (NDrone2_NearArmedPerson) is
// scared off, or with skin class 16 turns enemy and attacks.

#include "NDrone2_Civilian.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "NDrone2_Damage.h"
#include "Behaviour.h"
#include "../../game.h"                 // GameState

#include <bit>
#include <math.h>

// The originals, called by address: Ghidra's prototypes have untyped arguments
#define NDrone2_AssignAIPath ((void (__cdecl *)(DCVars_tag *dcv, uint pathType))0x00046ad0)
#define NDrone2_ReFindMissionPath ((bool (__cdecl *)(DCVars_tag *dcv))0x00047290)
#define NDrone2_DroneAlertToObject ((void (__cdecl *)(DCVars_tag *dcv, uint msgType, obj_tag *obj, float radiusA, float radiusB, float alertness))0x00038f40)
#define NDrone2_ChangeToAttackMode ((short (__cdecl *)(Drone_tag *drone))0x000397f0)
#define NDrone2_PlaySFX ((DYNAMICSOUNDS *(__cdecl *)(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char wait, float volume))0x00037340)
// FUN_00046c20 and FUN_00046d40 in Ghidra; the names are the PS2 build's
#define NDrone2_UpdatePatrolRoute ((void (__cdecl *)(DCVars_tag *dcv))0x00046c20)
#define NDrone2_UpdateMissionRoute ((int (__cdecl *)(DCVars_tag *dcv))0x00046d40)
// Returns in ST0, unrounded
#define Vec_Dist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e20)

// Declared with AUTOGEN in NDrone2.cpp
ushort DroneVision_EnemyAlerts(DCVars_tag *dcv, MsgObject *msg);

// What NDrone2_UpdateMissionRoute returns at the end of the path; the route's status while it cannot go on
#define MISSION_ROUTE_DONE 3
#define AIROUTE_STATUS_9 9
#define AIROUTE_STATUS_C 0xc

#define SFX_CIVILIAN_SCARED 0x48

// Below this alertness a civilian waits with the stand idle anims, at or above it with anim state 0x21
#define CIVILIAN_ALERT_LOOK 0.66f
// The player is close enough to keep a civilian waiting within 3 m (in plan) and 30 degrees of its facing
#define CIVILIAN_WAIT_DISTANCE 3.0f
constexpr float CIVILIAN_WAIT_ANGLE = (float)(M_PI / 6.0);
static_assert(std::bit_cast<uint32_t>(CIVILIAN_WAIT_ANGLE) == 0x3f060a92, "not the original's pi/6");
static_assert(std::bit_cast<uint32_t>(0.76f) == 0x3f428f5c, "not the original's 0.76");
static_assert(std::bit_cast<uint32_t>(CIVILIAN_ALERT_LOOK) == 0x3f28f5c3, "not the original's 0.66");

static void Civilian_SetTimer(DroneTimer *timer, uint dueFrame) {
    timer->dueFrame = dueFrame;
    timer->unknown8 = 0;
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
