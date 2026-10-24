// The scripted roles' states: truck drivers, abseilers, running from a scary object, the elevator jumper, "hold it
// right there", interrogators and civilian challengers, Kiko, the allies that lead the player along a mission path or
// follow the player, the castle chat guard, ambushers and the test states. Also NDrone2_ReFindMissionPath, which puts
// a drone back on its mission path, and NDrone2_FindAmbushCover.

#include "NDrone2_Roles.h"
#include "NDrone2_Combat.h"
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneInit.h"                  // NDrone2_Enable
#include "DroneMove.h"
#include "DroneTables.h"
#include "DroneVision_Alerts.h"
#include "DroneWeapon.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Route.h"
#include "../../game.h"                 // GameState
#include "../../engine/Anim.h"          // AnimState, sAnimScript_tag
#include "../../math/math.h"            // Vec_Copy, Vec_MulR32, Vec_Dist3D (ours)
#include "../../util/Random.h"
#include "../obj/Drone_AIHints.h"       // CoverNode_tag
#include "../sp/SwitchChannels.h"

#include <bit>
#include <math.h>
#include "NDrone2.h"
#include "NDrone2_Alert.h"
#include "NDrone2_Cover.h"
#include "../../math/X87Ops.h"

// Return in ST0, unrounded

// What NDrone2_UpdateMissionRoute returns at the end of the path; the route's status while it cannot go on
#define MISSION_ROUTE_DONE 3
#define AIROUTE_STATUS_9 9
#define AIROUTE_STATUS_C 0xc

// Below this alertness a drone waits with the stand idle anims, at or above it with anim state 0x21
#define ALERT_LOOK 0.66f
static_assert(std::bit_cast<uint32_t>(ALERT_LOOK) == 0x3f28f5c3, "not the original's 0.66");

// The abseilers' DTYPEs (DroneTypeSettings 27 and 28): a slider stops on the rope level with its opponent
#define DTYPE_ABSEIL_TO_GROUND DTYPE_Abseil27
#define DTYPE_ABSEIL_HANG DTYPE_Abseil28
#define ABSEIL_SLIDE_ACCELERATION 6.9f
static_assert(std::bit_cast<uint32_t>(ABSEIL_SLIDE_ACCELERATION) == 0x40dccccd, "not the original's 6.9");
static_assert(std::bit_cast<uint32_t>(0.02f) == 0x3ca3d70a, "not the original's 0.02");

// CoverNode_tag.flags: held by a drone (set by NDrone2_TakeCover; Drone_AIHints.h calls the bit COVERNODE_WIDER)
static constexpr uint kCoverNodeHeld = 0x10;

constexpr float ALLY_WITHIN_ANGLE = (float)(M_PI / 6.0);
constexpr float ALLY_IN_WAY_ANGLE = (float)(M_PI / 3.0);
static_assert(std::bit_cast<uint32_t>(ALLY_WITHIN_ANGLE) == 0x3f060a92, "not the original's pi/6");
static_assert(std::bit_cast<uint32_t>(ALLY_IN_WAY_ANGLE) == 0x3f860a92, "not the original's pi/3");

// data comes back as timer A's message's extraData
static void Roles_SetTimer(DroneTimer *timer, uint dueFrame, uint data = 0) {
    timer->dueFrame = dueFrame;
    timer->data = data;
    timer->on = 1;
}

// GameState.NumFramesUnpaused plus ticks scaled by FRAME_RATE_DIV, as the original works it out in x87 and __ftol2
static uint Roles_FramesFromNow(float ticks) {
    return Ftol((double)FRAME_RATE_DIV * ticks + (double)GameState.NumFramesUnpaused);
}

// ---------------------------------------------------------------------------------------------------------------
// Truck drivers

// The opponent is in sight, nearer than radius and armed (with properties 0x3c and 0x51). In TowerA and TowerB being
// seen is enough; in CastleIndoors1 the drone's own weapon counts too.
// AUTOINJECT
bool NDrone2_NearArmedBond(DCVars_tag *dcv, float radius) {
    if (behaviour_util_getProperty(DRONE_PROP_REACTS_TO_SIGHT, dcv->drone->currentBehaviour) == 0)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_ALWAYS_REACTS_TO_PLAYER, dcv->drone->currentBehaviour) == 0)
        return false;
    // A NaN distance is not near
    if (!(dcv->drone->distanceToTarget < radius))
        return false;
    if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT))
        return false;
    if (GameState.CurrentLevelHashcode == HT_Level_TowerA || GameState.CurrentLevelHashcode == HT_Level_TowerB)
        return true;
    if (DroneWeap_OpponentHasWeapon(dcv))
        return true;
    return GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && NDrone2_ObjectIsArmed(dcv->gameObj);
}

// A truck driver starts on its mission path (with property 0x1d) and plays its script, then walks the path or
// stands; an alert during the script scares it. Flags 0x20 and 0x80 are off while the script plays.
// AUTOINJECT
bool NDrone2_DSTATE_TruckDriverInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (behaviour_util_getProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour) != 0)
            NDrone2_AssignAIPath(dcv, AIPATH_MISSION);
        dcv->drone->targetRot.y = dcv->gameObj->rotation.y;
        if (dcv->drone->playScript != 0) {
            DroneAnim_SetScript(dcv->drone->playScript, DANIM_STAND_IDLE, 0x11a, 0x1e, 0, DRONE_MSG_ScriptEnd, 0, dcv);
            dcv->drone->playScript = (HASHCODE)0;
            dcv->drone->flags &= ~DRONE_FLAG_COLLIDES;
            dcv->drone->flags &= ~DRONE_FLAG_FALLS;
            return true;
        }
        behaviour_util_setProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour, 0);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverIdle, 0);

    case DRONE_MSG_Exit:
        dcv->drone->flags |= DRONE_FLAG_COLLIDES;
        dcv->drone->flags |= DRONE_FLAG_FALLS;
        return true;

    case DRONE_MSG_ScriptEnd:
        if (dcv->drone->route2.count != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverMission, 0);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverIdle, 0);

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_TruckDriverInitAlert, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_TruckDriverInitAlert, 1);
        return true;

    case 0x11:
    case 0x12:
    case 0x13:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverInitAlert, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_TruckDriverInitAlert);
        return true;

    default:
        return false;
    }
}

// Alerted during its script: the script skips to 90 frames before its end, and then the driver is scared off.
// AUTOINJECT
bool NDrone2_DSTATE_TruckDriverInitAlert(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter: {
        dcv->drone->flags &= ~DRONE_FLAG_COLLIDES;
        dcv->drone->flags &= ~DRONE_FLAG_FALLS;
        sAnimScript_tag *script = dcv->drone->animScript;
        if (script != NULL)
            script->curFrame = (float)(script->numFrames - 90);
        return true;
    }

    case DRONE_MSG_Exit:
        dcv->drone->flags |= DRONE_FLAG_COLLIDES;
        dcv->drone->flags |= DRONE_FLAG_FALLS;
        return true;

    case DRONE_MSG_ScriptEnd:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);
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

// Standing about until the armed opponent comes within 6 m.
// AUTOINJECT
bool NDrone2_DSTATE_TruckDriverIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        DroneAnim_SetStandIdleAnim(dcv, 0);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedBond(dcv, 6.0f)) {
            dcv->drone->flags |= DRONE_FLAG_ATTACK_SIGHT;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);
        }
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

    case 0x11:
    case 0x12:
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

// Walking the mission path; at its end property 0x1d is cleared and the driver stands. While the route cannot go on
// it waits. Flags 0x20 and 0x80 are off until timer A, a second on.
// AUTOINJECT
bool NDrone2_DSTATE_TruckDriverMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags &= ~DRONE_FLAG_COLLIDES;
        dcv->drone->flags &= ~DRONE_FLAG_FALLS;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        DroneAnim_CallAnim(DANIM_WALK_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        Roles_SetTimer(&dcv->drone->timerA, Roles_FramesFromNow(60.0f));
        return true;

    case DRONE_MSG_Exit:
    case DRONE_MSG_TimerA:
        dcv->drone->flags |= DRONE_FLAG_COLLIDES;
        dcv->drone->flags |= DRONE_FLAG_FALLS;
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_UpdateMissionRoute(dcv) == MISSION_ROUTE_DONE) {
            behaviour_util_setProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_TruckDriverIdle, 0);
        }
        if (dcv->drone->route2.status == AIROUTE_STATUS_9 || dcv->drone->route2.status == AIROUTE_STATUS_C) {
            // A NaN alertness looks up
            if (dcv->drone->alertness < ALERT_LOOK)
                DroneAnim_SetStandIdleAnim(dcv, 0);
            else
                DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        }
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

    case 0x11:
    case 0x12:
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
// Abseiling. The drone floats (flies) down its rope towards moveTarget; once its feet touch the floor it becomes the
// soldier it was (its saved anim set and base DTYPE) and aims.

// On the rope before sliding, for up to a second.
// AUTOINJECT
bool NDrone2_DSTATE_AbseilInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_SetDAnimNow(0xb, 0, 0, dcv);
        Roles_SetTimer(&dcv->drone->timerA, Rand_Rand(FRAME_RATE_INT) + GameState.NumFramesUnpaused);
        dcv->drone->flies = 1;
        dcv->drone->moveTarget = dcv->gameObj->position;
        dcv->drone->stateScratch = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AbseilSlide, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Sliding down, firing at the opponent while it is seen. DTYPE 27 slides to the ground; DTYPE 28 hangs once it is no
// more than 3 m (in frames' worth) above its opponent, or has none.
// AUTOINJECT
bool NDrone2_DSTATE_AbseilSlide(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(3, 0, 0, 0, 0.0f, 0, dcv);
        drone->flies = 1;
        drone->floatAcceleration = ABSEIL_SLIDE_ACCELERATION;
        if (drone->dtype == DTYPE_ABSEIL_TO_GROUND)
            drone->moveTarget.y = -1000.0f;
        return true;

    case DRONE_MSG_Update: {
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_AttackTalk(dcv);
        if (gameObj->animState->animFlags & ANIM_CONTACT_FLOOR) {
            drone->animSet = dcv->drone->savedAnimSet;
            drone->flies = 0;
            drone->dtype = drone->dtypeBase;
            DroneAnim_CallAnim(0x21, 0, DSTATE_AimStand, 0, 0.0f, 0, dcv);
            drone->stateScratch = 1;
        }
        drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && !drone->gunThroughWall;
        if (drone->dtype == DTYPE_ABSEIL_HANG) {
            obj_tag *opponent = drone->opponent;
            if (opponent == NULL)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AbseilHang, 0);
            if (drone->moveTarget.y > drone->opponentLastPos.y)
                drone->moveTarget.y = drone->opponentLastPos.y;
            if ((double)FRAME_RATE_MUL * 3.0f + opponent->position.y > gameObj->position.y)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AbseilHang, 0);
        }
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        return true;
    }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Hanging on the rope, firing, its fall slowing by 2% a frame. It slides on (scratch1c, 0 or 1, metres lower) once
// the opponent is far enough below.
// AUTOINJECT
bool NDrone2_DSTATE_AbseilHang(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(2, 0, 0, 0, 0.0f, 0, dcv);
        drone->flies = 1;
        dcv->aiStateMachine->scratch1c = Ftol(Float_FRand(1.0f));
        drone->floatAcceleration = 0.0f;
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_AttackTalk(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        Vec_MulR32(&drone->velocity, &drone->velocity, (float)(1.0f - (double)FRAME_RATE_DIV * 0.02f));
        drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && !drone->gunThroughWall;
        if (drone->moveTarget.y > drone->opponentLastPos.y)
            drone->moveTarget.y = drone->opponentLastPos.y;
        if (drone->opponent != NULL &&
            (int)dcv->aiStateMachine->scratch1c + (double)FRAME_RATE_MUL * 3.0f + drone->opponent->position.y <
                gameObj->position.y)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AbseilSlide, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Off the rope, aiming.
// AUTOINJECT
bool NDrone2_DSTATE_AbseilStepOff(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        drone->animSet = dcv->drone->savedAnimSet;
        drone->flies = 0;
        drone->dtype = drone->dtypeBase;
        drone->animState = 0;
        DroneAnim_CallAnim(0x21, 0, DSTATE_AimStand, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        drone->firing = 0;
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Killed on the rope: it falls, and on the floor lies dead. In Tower2Elevator it is taken away once it is no longer
// drawn. The death channel is set on leaving.
// AUTOINJECT
bool NDrone2_DSTATE_AbseilDeath(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        if (GameState.CurrentLevelHashcode == HT_Level_Tower2Elevator) {
            dcv->drone->flags &= ~DRONE_FLAG_COLLIDES;
            drone->flags |= DRONE_FLAG_FALLS_ANYWAY;
        } else {
            gameObj->effectFlags |= EFFECT_NO_COLLISION;
        }
        drone->stateScratch = 0;
        Drone_AlertStatusSet(3, dcv);
        NDrone2_LogDroneDeath(dcv);
        drone->stateScratch = 1;
        drone->stateScratch2 = 0;
        drone->flies = 0;
        DroneAnim_CallAnim(DANIM_DEATH, 0, 0, 0, 0.0f, 0, dcv);
        DroneWeap_DropWeapon(dcv);
        return true;

    case DRONE_MSG_Exit:
        if (drone->stateScratch != 0)
            DroneFunc_SetDeathChannel(drone);
        return true;

    case DRONE_MSG_Update:
        if (GameState.CurrentLevelHashcode == HT_Level_Tower2Elevator) {
            if (gameObj->drawnCountdown == 0) {
                NDrone2_Enable(0, dcv->drone);
                dcv->gameObj->flags |= 1;
            }
            return true;
        }
        if (drone->stateScratch2 == 0 && (gameObj->animState->animFlags & ANIM_CONTACT_FLOOR)) {
            DroneAnim_SetDAnimNow(10, 0, 0, dcv);
            drone->animEndState = DSTATE_Dead;
            drone->stateScratch2 = 1;
        }
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Scary objects (DroneFunc_ConsiderExplosive): run from it, hide where it cannot, and recover once explosiveTimeout
// has passed.

// AUTOINJECT
bool NDrone2_DSTATE_RunAwayFromObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        drone->alertness = 1.0f;
        NDrone2_DeathTalk(dcv);
        dcv->drone->flags |= DRONE_FLAG_DEAF;
        Drone_AlertStatusSet(1, dcv);
        if (DroneMove_FindSafetyFromScaryObject(dcv))
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HideFromScaryObject, 0);

    case DRONE_MSG_Exit:
        drone->explosiveSeen = 0;
        dcv->drone->flags &= ~DRONE_FLAG_DEAF;
        return true;

    case DRONE_MSG_Update:
        NDrone2_DistanceToAIPoint(dcv, &dcv->drone->aiPoint);
        if (drone->explosiveTimeout < GameState.NumFramesUnpaused)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RecoverFromScaryObject, 0);
        switch (NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&dcv->drone->aiPoint.pos, dcv->drone->aiPoint.radius,
                                           0x300)) {
        case 0:
        case 1:
        case 2:
            DroneAnim_CallAnim(0x1e, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case 9:
        case 0xc:
            if (DroneAnim_CanDoAnimState(dcv, 0x11, 0))
                DroneAnim_CallAnim(0x11, 0, 0, 0, 0.0f, 0, dcv);
            else if (DroneAnim_CanDoAnimState(dcv, 0x19, 0))
                DroneAnim_CallAnim(0x19, 0, 0, 0, 0.0f, 0, dcv);
            else
                DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case 0xa:
            return true;
        default:
            drone->explosiveSeen = 0;
            Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
            return true;
        }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_RecoverFromScaryObject, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_RecoverFromScaryObject, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_RecoverFromScaryObject);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_HideFromScaryObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        drone->explosiveSeen = 1;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        if (DroneAnim_CanDoAnimState(dcv, 0x11, 0))
            DroneAnim_CallAnim(0x11, 0, 0, 0, 0.0f, 0, dcv);
        else if (DroneAnim_CanDoAnimState(dcv, 0x19, 0))
            DroneAnim_CallAnim(0x19, 0, 0, 0, 0.0f, 0, dcv);
        else
            DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Exit:
        drone->explosiveSeen = 0;
        return true;

    case DRONE_MSG_Update:
        if (drone->explosiveTimeout < GameState.NumFramesUnpaused)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RecoverFromScaryObject, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_RecoverFromScaryObject, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_RecoverFromScaryObject, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_RecoverFromScaryObject);
        return true;

    default:
        return false;
    }
}

// Mayhew (DTYPE 12) goes back to the mission path; a drone that was running to the alarm runs on; the rest attack.
// AUTOINJECT
bool NDrone2_DSTATE_RecoverFromScaryObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (drone->dtype == DTYPE_Mayhew) {
            if (NDrone2_ReFindMissionPath(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadWait, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
        }
        drone->explosiveSeen = 0;
        if (dcv->drone->returnState == DSTATE_RunToAlarm)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunToAlarm, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Other scripted states

// Opens the nearest elevator door, runs out once it is open and attacks half a second later.
// AUTOINJECT
bool NDrone2_DSTATE_ElevatorJumper(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        NDrone2_FindElevatorDoor(dcv);
        NDrone2_OpenDoor(dcv);
        dcv->drone->stateScratch = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (dcv->drone->stateScratch == 0 && NDrone2_DoorIsOpen(dcv)) {
            DroneAnim_CallAnim(0x1e, 0, 0, 0, 0.0f, 0, dcv);
            dcv->drone->stateScratch = 1;
            Roles_SetTimer(&dcv->drone->timerA, Roles_FramesFromNow(30.0f));
        }
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    default:
        return false;
    }
}

// Fully alert, aiming at the opponent.
// AUTOINJECT
bool NDrone2_DSTATE_HoldItRightThere(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->alertness = 1.0f;
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        return true;

    default:
        return false;
    }
}

// Fails the mission (reason 0) and goes back to the initial state.
// AUTOINJECT
bool NDrone2_DSTATE_FailMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        DroneFunc_SetMissionFailReason(drone, 0);
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_StandBlind(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DANIM_STAND_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    default:
        return false;
    }
}

// Stops both of the drone's anim scripts where they are.
// AUTOINJECT
bool NDrone2_DSTATE_HangUp(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Update:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->animScript != NULL)
            dcv->drone->animScript->speed = 0.0f;
        if (dcv->drone->unknown430 != NULL)
            dcv->drone->unknown430->speed = 0.0f;
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Interrogation. An interrogator holds the opponent at gunpoint (DroneVision_BeginInterogate tells the others, who
// come to assist) and goes through its lines by timer A, then attacks; it attacks at once if the opponent draws a
// weapon he did not have, or gets 4 m further away than the nearest he came.

// The opponent has drawn a weapon: sight flag 0x10 marks one he had when the interrogation began (our name)
#define DRONE_SIGHT_OPPONENT_WAS_ARMED 0x10

static bool Interogate_OpponentDrew(DCVars_tag *dcv) {
    if ((dcv->drone->sightFlags & DRONE_SIGHT_OPPONENT_WAS_ARMED) && !DroneWeap_OpponentHasWeapon(dcv))
        dcv->drone->sightFlags &= ~DRONE_SIGHT_OPPONENT_WAS_ARMED;
    return !(dcv->drone->sightFlags & DRONE_SIGHT_OPPONENT_WAS_ARMED) && DroneWeap_OpponentHasWeapon(dcv);
}

static void Interogate_TrackNearest(Drone_tag *drone) {
    if (drone->distanceToTarget < drone->interrogateNearest)
        drone->interrogateNearest = drone->distanceToTarget;
}

// Told another drone is interrogating: after up to 2 s it goes within 10 m of the opponent, or waits where it is.
// AUTOINJECT
bool NDrone2_DSTATE_InterogateAssist(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        DroneVision_SetInterogateAssist(dcv->drone);
        Roles_SetTimer(&dcv->drone->timerA,
                       Ftol((double)Rand_Rand(120) * FRAME_RATE_DIV + (double)GameState.NumFramesUnpaused));
        return true;

    case DRONE_MSG_TimerA:
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        AINetwork_InvalidateRoute(&dcv->drone->route1);
        AINetwork_SetupGoalPositionToObj(&dcv->drone->goalTarget, &dcv->drone->aiPoint, dcv->drone->opponent, 10.0f);
        if (NDrone2_CanReachGoalPosition(dcv)) {
            dcv->drone->initialState = DSTATE_InterogateAssistWait;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
        }
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_InterogateAssistWait, 0);
        return true;

    default:
        return false;
    }
}

// Covering the interrogation, facing the opponent, until the interrogator is done; then it attacks.
// AUTOINJECT
bool NDrone2_DSTATE_InterogateAssistWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        DroneVision_SetInterogateAssist(dcv->drone);
        if (DroneFunc_WeaponInHand(dcv) > 1)
            DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
        else
            DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Exit:
        DroneVision_EndInterogateAssist(dcv->drone);
        return true;

    case DRONE_MSG_Update:
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        if (dcv->drone->alertedBy != NULL && !DroneVision_TestEndInterogationAssist(dcv->drone)) {
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            return true;
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactBullet:
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

// AUTOINJECT
bool NDrone2_DSTATE_Interogator(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        return true;

    default:
        return false;
    }
}

// Holding the opponent at gunpoint. A civilian, or a drone without a gun in its hands, attacks at once. Timer A steps
// through the lines: six steps in CastleExterior, two in CastleIndoors1, four elsewhere.
// AUTOINJECT
bool NDrone2_DSTATE_Interogate(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        // The original asks for the weapon in hand twice
        if (NDrone2_IsCivilian(dcv) || DroneFunc_WeaponInHand(dcv) <= 1 || DroneFunc_WeaponInHand(dcv) <= 1)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        DroneVision_BeginInterogate(dcv->drone);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        Drone_AlertStatusSet(1, dcv);
        if (DroneWeap_OpponentHasWeapon(dcv))
            dcv->drone->sightFlags |= DRONE_SIGHT_OPPONENT_WAS_ARMED;
        else
            dcv->drone->sightFlags &= ~DRONE_SIGHT_OPPONENT_WAS_ARMED;
        dcv->drone->interrogateNearest = dcv->drone->distanceToTarget;
        dcv->drone->interrogateDistance = dcv->drone->interrogateNearest;
        // The original first stores 1.0f in interrogateFrame
        Vec_Copy(&dcv->drone->opponent->position, &dcv->drone->interrogatePos);
        dcv->drone->interrogateFrame = GameState.NumFramesUnpaused;
        Roles_SetTimer(&dcv->drone->timerA, Roles_FramesFromNow(60.0f));
        dcv->drone->stateScratch = 0;
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Exit:
        DroneVision_EndInterogate(dcv->drone);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (Interogate_OpponentDrew(dcv)) {
            NDrone2_PlaySFX(dcv, 0x4a, NULL, 1, 1.0f);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        Interogate_TrackNearest(dcv->drone);
        if (dcv->drone->interrogateNearest - dcv->drone->distanceToTarget < -4.0f) {
            NDrone2_SpeechSFX(dcv, 0x4a, 0, 1, 1);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        }
        return true;

    case DRONE_MSG_TimerA: {
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        uint step = dcv->drone->stateScratch++;
        float ticks;
        if (GameState.CurrentLevelHashcode == HT_Level_CastleExterior) {
            switch (step) {
            case 0:
                DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
                NDrone2_SpeechSFX(dcv, 0x49, 0, 1, 1);
                ticks = 337.0f;
                break;
            case 1:
                DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
                NDrone2_SpeechSFX(dcv, 0x4d, 0, 1, 1);
                ticks = 60.0f;
                break;
            case 2:
                DroneAnim_CallAnim(0xb, 0, 0, 0, 0.0f, 0, dcv);
                ticks = 120.0f;
                break;
            case 3:
                DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
                ticks = 60.0f;
                break;
            case 4:
                NDrone2_SpeechSFX(dcv, 0x4e, 0, 1, 1);
                ticks = 45.0f;
                break;
            case 5:
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            default:
                return true;
            }
        } else if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1) {
            switch (step) {
            case 0:
                DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
                NDrone2_SpeechSFX(dcv, 0x51, 0, 1, 1);
                ticks = 180.0f;
                break;
            case 1:
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            default:
                return true;
            }
        } else {
            switch (step) {
            case 0:
                DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
                ticks = 180.0f;
                break;
            case 1:
                DroneAnim_CallAnim(0xb, 0, 0, 0, 0.0f, 0, dcv);
                ticks = 180.0f;
                break;
            case 2:
                DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
                ticks = 60.0f;
                break;
            case 3:
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            default:
                return true;
            }
        }
        Roles_SetTimer(&dcv->drone->timerA, Roles_FramesFromNow(ticks));
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

// Walking towards the opponent, facing him, until within 4 m; then it interrogates. As there, it attacks if he draws
// or gets 4 m further away than the nearest he came.
// AUTOINJECT
bool NDrone2_DSTATE_InterogateWalk(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        DroneAnim_CallAnim(0x35, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (Interogate_OpponentDrew(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        Interogate_TrackNearest(dcv->drone);
        if (dcv->drone->distanceToTarget < 4.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Interogate, 0);
        if (dcv->drone->interrogateNearest - dcv->drone->distanceToTarget < -4.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
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

// A civilian challenges the opponent with one of nine lines, facing him, then runs to the alarm.
// AUTOINJECT
bool NDrone2_DSTATE_CivilianChallenge(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->knowledge |= DRONE_KNOWS_INTERROGATING;
        DroneAnim_CallAnim(0x3e, 0, DSTATE_RunToAlarm, 0, 0.0f, 0, dcv);
        dcv->drone->speechChannel = NDrone2_SpeechSFX(dcv, Rand_Rand(9) + 0xc3, 0, 0, 0);
        return true;

    case DRONE_MSG_Exit:
        DroneVision_EndInterogate(dcv->drone);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_RunToAlarm, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_RunToAlarm, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_RunToAlarm);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Kiko (skin class 19 on a mission path)

// Walking the mission path; at its end property 0x1d is cleared and she stands blind. Seeing the opponent, an alert or
// a forced attack makes her run on, except in EvilBaseC.
// AUTOINJECT
bool NDrone2_DSTATE_KikoMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        DroneAnim_CallAnim(0x34, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (GameState.CurrentLevelHashcode != HT_Level_EvilBaseC && DroneVision_EnemyLookForOpponent(dcv) != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KikoMissionRun, 0);
        if (NDrone2_UpdateMissionRoute(dcv) == MISSION_ROUTE_DONE) {
            behaviour_util_setProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_StandBlind, 0);
        }
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

    case 0x11:
    case 0x12:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        if (GameState.CurrentLevelHashcode == HT_Level_EvilBaseC || DroneVision_EnemyAlerts(dcv, msg) == 0)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KikoMissionRun, 0);

    case DRONE_MSG_ConsideredAlerted:
        if (GameState.CurrentLevelHashcode == HT_Level_EvilBaseC)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KikoMissionRun, 0);

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, 0);
        return true;

    default:
        return false;
    }
}

// Running along the mission path, and at its end to the alarm.
// AUTOINJECT
bool NDrone2_DSTATE_KikoMissionRun(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x1e, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_UpdateMissionRoute(dcv) == MISSION_ROUTE_DONE)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunToAlarm, 0);
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

// ---------------------------------------------------------------------------------------------------------------
// Allies. One that leads takes the player along its mission path (route2), waiting when the player falls behind;
// one that follows keeps near the player. Hit or seeing an explosive, they come back to the state they were in.

// An armed enemy within 10 m is near (NDrone2_NearArmedDrone): an ally runs to a point and hides there, or hides
// where it is (our name; each state has it inline)
static bool Ally_RunAndHide(DCVars_tag *dcv) {
    if (NDrone2_FindRunToPoint(dcv)) {
        dcv->drone->initialState = DSTATE_AllyLeadHide;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyGoToGoalPosition, 0);
    }
    return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadHide, 0);
}

#define ALLY_ARMED_RADIUS 10.0f

// The player's distance along the mission path, ahead of the ally (negative behind), kept in pathDistance
static float Ally_PathDistance(DCVars_tag *dcv) {
    dcv->aiStateMachine->pathDistance = (float)DroneMove_ObjectMissionPathDistance(dcv, NDrone2_Player(), 5.0f);
    return dcv->aiStateMachine->pathDistance;
}

// Leading at a run, standing at the path's wait nodes; done at its end. With the player close in front of it, it gets
// out of the way; with the player behind (more than 9 m along the path, or off it and 5 m away) it waits. Between 15
// and 25 m from the end it also waits while the player is behind or drones were attacking last frame. Message 0x1b
// makes it wait.
// AUTOINJECT
bool NDrone2_DSTATE_AllyLead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(1, dcv);
        drone->activeRoute = &drone->route2;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x1e, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedDrone(dcv, ALLY_ARMED_RADIUS, 1))
            return Ally_RunAndHide(dcv);
        switch (NDrone2_UpdateMissionRoute(dcv)) {
        case 0:
        case 2:
            DroneAnim_CallAnim(0x1e, 0, 0, 0, 0.0f, 0, dcv);
            break;
        case MISSION_ROUTE_DONE:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadDone, 0);
        case 9:
        case 0xc:
            DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
            break;
        }
        // A NaN distance does not count as close
        if (NDrone2_BondWithinAngle(dcv, ALLY_WITHIN_ANGLE) && NDrone2_Player() != NULL &&
            Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 3.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadPlayerInWay, 0);
        if (Ally_PathDistance(dcv) == 0.0f &&
            (NDrone2_Player() == NULL || !(Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 5.0f)))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadWait, 0);
        if (dcv->aiStateMachine->pathDistance < -9.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadWait, 0);
        if (!(drone->route2.remaining < 25.0f) || drone->route2.remaining <= 15.0f)
            return true;
        if (!(dcv->aiStateMachine->pathDistance < 0.0f) && NPCGlobals.prevSeenAttacking == 0)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadBondCombat, 0);

    case 0x1b:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadWait, 0);
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

// The player is in the way: standing while he is within 3.5 m and 60 degrees in front, then leading again.
// AUTOINJECT
bool NDrone2_DSTATE_AllyLeadPlayerInWay(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(2, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        drone->activeRoute = &drone->route2;
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedDrone(dcv, ALLY_ARMED_RADIUS, 1))
            return Ally_RunAndHide(dcv);
        if (NDrone2_BondWithinAngle(dcv, ALLY_IN_WAY_ANGLE) && NDrone2_Player() != NULL &&
            Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 3.5f)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLead, 0);

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

// Hiding until no armed enemy is within 14 m; two seconds later it finds its mission path again and waits there.
// AUTOINJECT
bool NDrone2_DSTATE_AllyLeadHide(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(2, dcv);
        drone->activeRoute = &drone->route2;
        DroneAnim_CallAnim(0x19, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->stateScratch = 0;
        return true;

    case DRONE_MSG_Update:
        if (dcv->drone->stateScratch == 0 && NDrone2_NotNearArmedDrone(dcv, 14.0f)) {
            dcv->drone->stateScratch = 1;
            Roles_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        }
        return true;

    case DRONE_MSG_TimerA:
        if (NDrone2_ReFindMissionPath(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLeadWait, 0);
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

// Waiting for the player to come within 7 m along the path (or 5 m, off it), then leading again.
// AUTOINJECT
bool NDrone2_DSTATE_AllyLeadWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(2, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        drone->activeRoute = &drone->route2;
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedDrone(dcv, ALLY_ARMED_RADIUS, 1))
            return Ally_RunAndHide(dcv);
        // A NaN distance waits
        if (!(Ally_PathDistance(dcv) > -7.0f))
            return true;
        if (dcv->aiStateMachine->pathDistance == 0.0f &&
            (NDrone2_Player() == NULL || !(Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 5.0f)))
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLead, 0);

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

// Standing at the mission path's next node until a switch channel is on: the state change's parameter, else
// SWITCH_MISSION_WAIT.
// AUTOINJECT
bool NDrone2_DSTATE_AllyLeadMissionWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        drone->stateScratch = drone->sm.stateParam != 0 ? drone->sm.stateParam : SWITCH_MISSION_WAIT;
        drone->activeRoute = &drone->route2;
        Drone_AlertStatusSet(2, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_MoveTo_RouteNode(dcv, 1, &dcv->drone->route2);
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (switch_channels[drone->stateScratch] != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLead, 0);
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

// Waiting while the player is behind along the path or drones were attacking last frame, then leading again.
// AUTOINJECT
bool NDrone2_DSTATE_AllyLeadBondCombat(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(2, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        drone->activeRoute = &drone->route2;
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_NearArmedDrone(dcv, ALLY_ARMED_RADIUS, 1))
            return Ally_RunAndHide(dcv);
        // A NaN distance waits
        if (!(Ally_PathDistance(dcv) >= 0.0f))
            return true;
        if (NPCGlobals.prevSeenAttacking != 0)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyLead, 0);

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

// Standing until it sees the opponent, then following the player.
// AUTOINJECT
bool NDrone2_DSTATE_AllyFollowInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyFollow, 0);
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

// Running after the player (to within 2 m), facing the opponent; within 4 m of the player, or with no way to him,
// it waits.
// AUTOINJECT
bool NDrone2_DSTATE_AllyFollow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x1e, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update: {
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (NDrone2_Player() != NULL && Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 4.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyFollowWait, 0);
        uint moved = NDrone2_MoveToObject(dcv, NDrone2_Player(), 2.0f, 1);
        if (moved <= 2 || moved == 0xa)
            return true;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyFollowWait, 0);
        return true;
    }

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

// Standing, facing the opponent, while the player is within 8 m; once he is further and there is a way to him, it
// follows again.
// AUTOINJECT
bool NDrone2_DSTATE_AllyFollowWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(2, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (NDrone2_Player() != NULL && Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 8.0f)
            return true;
        if (NDrone2_MoveToObject(dcv, NDrone2_Player(), 2.0f, 1) <= 2)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AllyFollow, 0);
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

// Done: standing alert. The Xbox's state table has this handler for AllyLeadDone too.
// AUTOINJECT
bool NDrone2_DSTATE_AllyFollowDone(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(1, dcv);
        DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
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

// Back to the mission path (route2): of its nodes within 20 m (in plan), the nearest the drone can walk straight to
// becomes the current one and the drone walks there (true). With none, the nearest node becomes the current one and
// the drone goes there by the network if it can (GoToGoalPosition); otherwise its goal is the player. Either way the
// path's waypoint moves on from the new current node.
// AUTOINJECT
bool NDrone2_ReFindMissionPath(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *gameObj = dcv->gameObj;
    AIRoute_tag *route = &drone->route2;
    uint reachable = 0xffff;
    uint nearest = 0xffff;
    float reachableDist = 400.0f;      // squared
    float nearestDist = 400.0f;
    CelPos_tag reachablePos, nearestPos, candidate;

    for (uint i = 0; i < route->count; i++) {
        MAPNODE_tag *node = &route->path->fileNodes[route->nodes[i]].node;
        float dist = (float)Vec_SqDist2D(&node->pos, &gameObj->position);
        if (reachable == 0xffff || dist < reachableDist) {
            Vec_Copy(&node->pos, &candidate.pos);
            candidate.cel = node->cel;
            if (NDrone2_CanWalkStraightTo(drone, gameObj, &candidate, 1)) {
                Vec_Copy(&candidate.pos, &reachablePos.pos);
                reachablePos.cel = candidate.cel;
                reachable = i;
                reachableDist = dist;
            }
        }
        if (nearest == 0xffff || dist < nearestDist) {
            Vec_Copy(&node->pos, &nearestPos.pos);
            nearestPos.cel = node->cel;
            nearest = i;
            nearestDist = dist;
        }
    }

    if (reachable != 0xffff) {
        route->current = (short)reachable;
        if (LinkCreep_Calc(route, 0)) {
            LinkCreep_Dest(route, 0, &route->waypoint);
            NDrone2_SetMoveTarget(dcv, &route->waypoint);
        }
        dcv->drone->activeRoute = NULL;
        AINetwork_InvalidateRoute(&dcv->drone->route1);
        AINetwork_InvalidateRoute(&dcv->drone->route1);
        AINetwork_SetupGoalPosition(&dcv->drone->goalTarget, &dcv->drone->aiPoint, &reachablePos, 2.0f);
        NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&dcv->drone->aiPoint.pos, dcv->drone->aiPoint.radius, 0);
        return true;
    }

    if (nearest != 0xffff) {
        route->current = (short)nearest;
        if (LinkCreep_Calc(route, 0)) {
            LinkCreep_Dest(route, 0, &route->waypoint);
            NDrone2_SetMoveTarget(dcv, &route->waypoint);
        }
        if (drone->skinClass == SKINCLASS_MAYHEW)
            drone->route1.path = AINetwork_NavPathForPosition(&gameObj->position, gameObj->inCel, 1);
        AINetwork_ResetRoute(&drone->route1, gameObj, NULL);
        AINetwork_InvalidateRoute(&drone->route1);
        AINetwork_SetupGoalPosition(&drone->goalTarget, &drone->aiPoint, &nearestPos, 1.0f);
        if (NDrone2_CanReachGoalPosition(dcv)) {
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
            return false;
        }
    }

    obj_tag *player = NDrone2_Player();
    nearestPos.pos = player->position;
    nearestPos.cel = player->inCel;
    AINetwork_ResetRoute(&drone->route1, gameObj, NULL);
    AINetwork_InvalidateRoute(&drone->route1);
    AINetwork_SetupGoalPosition(&drone->goalTarget, &drone->aiPoint, &nearestPos, 2.0f);
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// The castle chat guard

// Chatting, alert (0x21) from alertness 0.66, else with the stand idle anims. Once its chat channel is on it plays its
// script; leaving with the script not yet played sets channel 0x1a. Alerts it does not act on make Idle its return
// state.
// AUTOINJECT
bool NDrone2_DSTATE_CastleChatGuard1(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        // A NaN alertness idles
        if (dcv->drone->alertness >= ALERT_LOOK)
            DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        else
            DroneAnim_SetStandIdleAnim(dcv, 0);
        return true;

    case DRONE_MSG_Exit:
        if (dcv->drone->playScript != 0)
            SwitchChannel_SetActive(0x1a);   // the level's number
        return true;

    case DRONE_MSG_Update:
        if (dcv->drone->playScript != 0 && switch_channels[dcv->drone->chatSwitchChannel] != 0) {
            DroneAnim_SetScript(dcv->drone->playScript, 0, 0x11a, 0, 0, DRONE_MSG_ScriptEnd, 0, dcv);
            dcv->drone->playScript = (HASHCODE)0;
        }
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ScriptEnd:
        if (dcv->drone->alertness >= ALERT_LOOK)
            DroneAnim_CallAnim(0x21, 0, DSTATE_Idle, 0, 0.0f, 0, dcv);
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
        if (!dcv->drone->returnPosKept)
            dcv->drone->returnState = DSTATE_Idle;
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

// ---------------------------------------------------------------------------------------------------------------
// Ambushers

// The nearest cover node within 2 m that no drone holds and this one can use (a low node only with property 0xf):
// the drone lets go of its old node, takes this one and is sent to it, at a corner on the side nearer it. Kept from
// the original: whether the node is a corner or low is read from the entry after the table's last node, not from the
// node chosen; with neither, it returns false having already changed its node.
// AUTOINJECT
bool NDrone2_FindAmbushCover(DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    CoverNode_tag *node = NPCGlobals.coverNodes;
    int best = -1;
    float bestDist = 2.0f;
    if (NPCGlobals.numCoverNodes == 0 || node == NULL)
        return false;

    for (uint i = 0; i < NPCGlobals.numCoverNodes; i++, node++) {
        if (node->flags & kCoverNodeHeld)
            continue;
        if (!NDrone2_CornerCoverAnimOK(dcv, node))
            continue;
        if (node->type == COVER_LOW && behaviour_util_getProperty(DRONE_PROP_LOW_COVER, dcv->drone->currentBehaviour) == 0)
            continue;
        double dist = Vec_Dist3D(&gameObj->position, &node->pos);
        // A NaN distance is taken
        if (!(dist >= bestDist)) {
            best = i;
            bestDist = (float)dist;
        }
    }
    if (best == -1)
        return false;

    if (dcv->drone->coverNode != NULL) {
        dcv->drone->coverNode->occupant = NULL;
        dcv->drone->coverNode->flags &= ~kCoverNodeHeld;
    }
    dcv->drone->flags &= ~DRONE_FLAG_GOING_TO_COVER;
    dcv->drone->flags &= ~DRONE_FLAG_HOLDS_COVER;
    drone->coverNode = &NPCGlobals.coverNodes[best];
    uint side;
    switch (node->type) {
    case COVER_CORNER: {
        NDrone2_GetPosForCornerCover(drone->coverNode, 1, &drone->coverPos.pos, &drone->aiPointRot, 0);
        float dist1 = Vec_Dist3D(&gameObj->position, &drone->coverPos.pos);
        NDrone2_GetPosForCornerCover(drone->coverNode, 2, &drone->coverPos.pos, &drone->aiPointRot, 0);
        side = 2;
        if (Vec_Dist3D(&gameObj->position, &drone->coverPos.pos) > dist1) {
            NDrone2_GetPosForCornerCover(drone->coverNode, 1, &drone->coverPos.pos, &drone->aiPointRot, 0);
            side = 1;
        }
        break;
    }
    case COVER_LOW:
        NDrone2_GetPosForCornerCover(drone->coverNode, 3, &drone->coverPos.pos, &drone->aiPointRot, 0);
        side = 3;
        break;
    default:
        return false;
    }
    AINetwork_InvalidateRoute(&dcv->drone->route1);
    drone->coverPos.cel = drone->coverNode->cel;
    AINetwork_SetupGoalPosition(&dcv->drone->goalTarget, &dcv->drone->aiPoint, &drone->coverPos, 0.5f);
    NDrone2_TakeCover(dcv, side);
    return true;
}

// With an ambush cover node, it goes there (UnderCoverInit); otherwise it crouches and waits.
// AUTOINJECT
bool NDrone2_DSTATE_AmbushInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (NDrone2_FindAmbushCover(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverInit, 0);
        DroneAnim_CallAnim(0x11, 0, 0, 0, 0.0f, 0, dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AmbushWait, 0);
        return true;

    default:
        return false;
    }
}

// Lying in wait, seeing all round, until it sees the opponent or the player comes within 3 m; then it attacks.
// Leaving clears property 0x10.
// AUTOINJECT
bool NDrone2_DSTATE_AmbushWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        drone->viewAngle = 0.0f;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        return true;

    case DRONE_MSG_Exit:
        behaviour_util_setProperty(DRONE_PROP_AMBUSH, dcv->drone->currentBehaviour, 0);
        return true;

    case DRONE_MSG_Update:
        // A NaN distance waits
        if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) &&
            (NDrone2_Player() == NULL ||
             !(Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 3.0f)))
            return true;
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

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The test states

// Standing still, going through anim states 0x1a (2 s), 0x1b (10 frames) and 0xa (2 s) by timer A.
// AUTOINJECT
bool NDrone2_DSTATE_Tester1(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(0x1a, 0, 0, 0, 0.0f, 0, dcv);
        Roles_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + 120);
        return true;

    case DRONE_MSG_Update:
        drone->standingStill = 1;
        return true;

    case DRONE_MSG_TimerA:
        switch ((uint)(uintptr_t)msg->extraData) {
        case 0:
            DroneAnim_CallAnim(0x1b, 0, 0, 0, 0.0f, 0, dcv);
            Roles_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + 10, 1);
            break;
        case 1:
            DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
            Roles_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + 120, 2);
            break;
        case 2:
            DroneAnim_CallAnim(0x1a, 0, 0, 0, 0.0f, 0, dcv);
            Roles_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + 120);
            break;
        }
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_Tester2(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(0xd, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Tester1, 0);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_Tester3(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(0xd, 0, 0, 0, 0.0f, 0, dcv);
        Roles_SetTimer(&dcv->drone->timerA, Roles_FramesFromNow(60.0f));
        return true;

    case DRONE_MSG_TimerA:
        DroneAnim_CallAnim(0xa, 0, DSTATE_Tester4, 0, 0.0f, 0, dcv);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_Tester4(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
        Roles_SetTimer(&dcv->drone->timerA, Roles_FramesFromNow(60.0f));
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Tester1, 0);
        return true;

    default:
        return false;
    }
}
