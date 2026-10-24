// The states of the special drones: those that run to an alarm and press it, an enemy walking a mission path, the
// ninja, the space drake and the astronauts.

#include "NDrone2_Special.h"
#include "NDrone2_Combat.h"
#include "../obj/control.h"
#include "../obj/Effect.h"
#include "../../engine/Anim.h"
#include "../../util/hashtable.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Route.h"               // NDrone2_AssignAIPath, NDrone2_UpdateMissionRoute, NDrone2_FindAlarmPoint
#include "DroneAnim.h"
#include "DroneMove.h"                   // the astronaut's moves
#include "DroneModes.h"                  // NDrone2_ChangeToAttackMode
#include "DroneTables.h"
#include "DroneVision_Alerts.h"          // DroneVision_EnemyAlerts, DroneVision_EnemyLookForOpponent
#include "DroneWeapon.h"
#include "Behaviour.h"
#include "../../game.h"                  // GameState
#include "../sp/SwitchChannels.h"
#include "../../util/Random.h"
#include "NDrone2_Alert.h"

// The originals, called by address

// The cheat that blinds the drones (ui_cheats.cpp)
extern uint32_t switch_BLIND_DRONES;

// Below this alertness an enemy on its mission path walks (and waits) idly
#define ENEMY_ALERT_LOOK 0.66f

static void Special_SetTimer(DroneTimer *timer, uint dueFrame, uint data = 0) {
    timer->dueFrame = dueFrame;
    timer->data = data;
    timer->on = 1;
}

// GameState.NumFramesUnpaused plus ticks scaled by FRAME_RATE_DIV, as the original works it out in x87 and __ftol2
static uint Special_FramesFromNow(float ticks) {
    return (uint)(long long)((double)FRAME_RATE_DIV * ticks + (double)GameState.NumFramesUnpaused);
}

// ---------------------------------------------------------------------------------------------------------------
// Alarms

// Off to the alarm point (NDrone2_FindAlarmPoint) to press it, then attack; with none it stands idle
// AUTOINJECT
bool NDrone2_DSTATE_RunToAlarm(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        if (NDrone2_FindAlarmPoint(dcv)) {
            dcv->drone->runningToAlarm = 1;
            dcv->drone->initialState = DSTATE_PressAlarm;
            dcv->drone->returnState = DSTATE_Attack;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
        }
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    default:
        return false;
    }
}

// At the alarm, facing the way its point faces: 3 seconds on (timer A), it presses it (anim state 0x3c, then
// DonePressAlarm); 30 ticks after that the mission fails by the alarm (fail reason 16). stateScratch is 1 once it is
// pressing. In TowerA and TowerB it waits with anim state 0x21, elsewhere 0x22.
// AUTOINJECT
bool NDrone2_DSTATE_PressAlarm(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        drone->stateScratch = 0;
        if (GameState.CurrentLevelHashcode >= HT_Level_TowerA && GameState.CurrentLevelHashcode <= HT_Level_TowerB)
            DroneAnim_CallAnim(0x21, 0, 0, 0, 0.0f, 0, dcv);
        else
            DroneAnim_CallAnim(0x22, 0, 0, 0, 0.0f, 0, dcv);
        Special_SetTimer(&dcv->drone->timerA, FRAME_RATE_INT * 3 + GameState.NumFramesUnpaused);
        drone->targetRot.y = drone->aiPointRot.y;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_TimerA:
        if (msg->extraData == NULL) {
            drone->stateScratch = 1;
            DroneAnim_CallAnim(0x3c, 0, DSTATE_DonePressAlarm, 0, 0.0f, 0, dcv);
            Special_SetTimer(&dcv->drone->timerA, Special_FramesFromNow(30.0f), 1);
            return true;
        }
        if (!NDrone2_IsCivilian(dcv)) {
            drone->animNextRow = 0x32;
            drone->animEndBlend = 0x1e;
        }
        DroneFunc_SetMissionFailReason(drone, 16);
        dcv->drone->runningToAlarm = 0;
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

// The alarm pressed: skin class 0x13 just stands; armed (or with a second weapon) it attacks, otherwise it is scared
// (property 0x56 cleared), running first to a point to run to if there is one.
// AUTOINJECT
bool NDrone2_DSTATE_DonePressAlarm(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (drone->skinClass == SKINCLASS_KIKO_COMBAT)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_JustStand, 0);
        if ((dcv->gameObj != NULL && dcv->gameObj->animState != NULL && dcv->gameObj->animState->currentWeaponId > 1) ||
            dcv->drone->secondWeapon > 1) {
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            return true;
        }
        behaviour_util_setProperty(DRONE_PROP_RUNS_TO_ALARM, dcv->drone->currentBehaviour, 0);
        if (NDrone2_FindRunToPoint(dcv)) {
            dcv->drone->initialState = DSTATE_CivilianScared;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_HostageGoToGoalPosition, 0);
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// An enemy on a mission path

// Walking its mission path, looking for its opponent. Where the route waits it stands idle, or alert (from 0.66)
// faces its opponent with anim state 10, firing while it sees it unless its gun is through a wall. At the end of the
// path it attacks if it has a second state (not with a mode change channel, nor in CastleCourtyard), else stands.
// Skin class 0x13 takes the KikoMission state.
// AUTOINJECT
bool NDrone2_DSTATE_EnemyMission(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->skinClass == SKINCLASS_KIKO_COMBAT)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KikoMission, 0);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        // A NaN alertness walks alert
        DroneAnim_CallAnim(dcv->drone->alertness < ENEMY_ALERT_LOOK ? 0x37 : 0x35, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        if (NDrone2_UpdateMissionRoute(dcv) == 3) {
            behaviour_util_setProperty(DRONE_PROP_MISSION_PATH, dcv->drone->currentBehaviour, 0);
            AINetwork_InvalidateRoute(&drone->route2);
            drone->route2.count = 0;
            if (dcv->drone->secondState != 0 && dcv->drone->modeChangeSwitchChannel == 0 &&
                GameState.CurrentLevelHashcode != HT_Level_CastleCourtyard)
                return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)NDrone2_ChangeToAttackMode(dcv->drone), 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        }
        if (dcv->drone->route2.status == 9 || dcv->drone->route2.status == 0xc) {
            // A NaN alertness is alert
            if (dcv->drone->alertness < ENEMY_ALERT_LOOK) {
                DroneAnim_SetStandIdleAnim(dcv, 1);
                return true;
            }
            drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && !drone->gunThroughWall;
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            DroneAnim_CallAnim(0xa, 0, 0, 0, 0.0f, 0, dcv);
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

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The ninja. NinjaAttack picks an attack by the distance to the opponent: beyond 12 m LongRange, beyond 6 m
// MidRange, beyond 2 m ShortRange; closer it flips aside or back, fires standing, or uses the sword.

// A punch or a shot: its effects (scaled), then a cry of pain, or with no health left the given death (our name;
// each ninja state has it inline)
static bool Ninja_Hit(DCVars_tag *dcv, MsgObject *msg, DSTATE death, float scale) {
    NDrone2_DoHitEffects(dcv, (HITDATA_tag *)msg->extraData, scale);
    // A NaN health cries out
    if (dcv->drone->health <= 0.0f)
        return Drone_SM_SetState(dcv->aiStateMachine, death, 0);
    NDrone2_PainTalk(dcv);
    return true;
}

// Room for a flip 3 m to either side, or 5 m back (our names)
static bool Ninja_CanSideflip(DCVars_tag *dcv) {
    return NDrone2_CanMoveToRelativePos(dcv->drone, dcv->gameObj, 3.0f, 0.0f, 0.0f) ||
           NDrone2_CanMoveToRelativePos(dcv->drone, dcv->gameObj, -3.0f, 0.0f, 0.0f);
}

static bool Ninja_CanBackflip(DCVars_tag *dcv) {
    return NDrone2_CanMoveToRelativePos(dcv->drone, dcv->gameObj, 0.0f, 0.0f, -5.0f);
}

// Room to somersault 5 m forward over an opponent between 5 and 7 m away (our name)
static bool Ninja_CanSomersault(DCVars_tag *dcv, Drone_tag *drone) {
    return NDrone2_CanMoveToRelativePos(dcv->drone, dcv->gameObj, 0.0f, 0.0f, 5.0f) &&
           drone->distanceToTarget > 5.0f && drone->distanceToTarget < 7.0f;
}

// The ninja's glowing eyes: an effect at each eye bone (the skin 0x500001e has its own), five frames long
// AUTOINJECT
void NDrone2_CreateNinjaEyes(obj_tag *gameObj) {
    short eyes[2];
    if (gameObj->animState->animObj.skin->hashcode == HT_Skin_Ninja) {
        eyes[0] = 0x60;
        eyes[1] = 0x61;
    } else {
        eyes[0] = 0x54;
        eyes[1] = 0x56;
    }
    for (short bone : eyes) {
        _VECTOR pos;
        AnimGetBoneWorldTrans(gameObj, bone, 0, &pos, NULL);
        obj_tag *eye = control_create_object(sizeof(Effect_tag), &pos, NULL, NULL);
        if (eye == NULL)
            continue;
        Effect_tag *effect = (Effect_tag *)eye->extraObjectData;
        eye->objectType = OBJECTTYPE_EFFECT;
        eye->tweakA = 0x80;
        eye->curState = 1;
        effect->life = 5;
        effect->fade = 0x19;
        hashtable_set_object_to_entity_gfx(eye, HT_Entity_gunsight3d);
        eye->effectFlags |= 8;
        control_link_object_to_cel(eye, gameObj->inCel);
    }
}

// Standing with the sword in hand (the object's datum 100), attacking at once, or with the drones blinded once it
// sees its opponent
// AUTOINJECT
bool NDrone2_DSTATE_NinjaStand(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        drone->flags |= DRONE_FLAG_BOUNDS;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->combatRanges[1] = 3.0f;
        dcv->drone->combatRanges[4] = 5.0f;
        AnimDatumSetEntity(dcv->gameObj, 0, (HASHCODE)0, 0xff);
        AnimDatumSetEntity(dcv->gameObj, 100, HT_Entity_Ninja_sword_3rd_LOD0, 0xff);
        if (switch_BLIND_DRONES == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        DroneAnim_CallAnim(0x6b, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 1.0f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 1.0f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_NinjaAttack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (switch_BLIND_DRONES != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaStand, 0);
        drone->flags |= DRONE_FLAG_BOUNDS;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->alertness = 1.0f;
        if (dcv->drone->distanceToTarget > 12.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttackLongRange, 0);
        if (dcv->drone->distanceToTarget > 6.0f && dcv->drone->distanceToTarget <= 12.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttackMidRange, 0);
        if (dcv->drone->distanceToTarget > 2.0f && dcv->drone->distanceToTarget <= 6.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttackShortRange, 0);
        if (DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[0])) {
            if (Rand_Rand(10) == 0 && Ninja_CanSideflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
            if (Ninja_CanBackflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaBackflip, 0);
        }
        if (Rand_Rand(2) != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaStandFire, 0);
        if (dcv->drone->distanceToTarget > 0.0f && dcv->drone->distanceToTarget <= 2.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSword, 0);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (dcv->drone->distanceToTarget > 12.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttackLongRange, 0);
        if (dcv->drone->distanceToTarget > 6.0f && dcv->drone->distanceToTarget <= 12.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttackMidRange, 0);
        if (dcv->drone->distanceToTarget > 2.0f && dcv->drone->distanceToTarget <= 6.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttackShortRange, 0);
        if (!DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[0]))
            return true;
        if (Ninja_CanBackflip(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaBackflip, 0);
        if (Rand_Rand(10) == 0 && Ninja_CanSideflip(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 1.0f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 1.0f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// Running at a far opponent, firing (anim state 0x67), until within 12 m; now and then, aimed at by the player for
// over 30 frames, it flips aside.
// AUTOINJECT
bool NDrone2_DSTATE_NinjaAttackLongRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->alertness = 1.0f;
        DroneAnim_CallAnim(0x67, 0, 0, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        // A NaN distance attacks again
        if (!(dcv->drone->distanceToTarget > 12.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        switch (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1)) {
        case 0:
        case 1:
        case 2:
            DroneAnim_CallAnim(0x67, 0, 0, 0, 0.0f, 0, dcv);
            if (dcv->drone->inPlayersSightsFrames > 30 && Rand_Rand(2) != 0 && Rand_Rand(10) == 0 &&
                Ninja_CanSideflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
            return true;
        case 3:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaStandFire, 0);
        case 10:
            return true;
        default:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaNoRoute, 0);
        }

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 1.0f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 1.0f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// Closing in from 6 to 12 m, firing. Every 30 ticks (timer A; 60 the first time) it may fire standing while it sees
// its opponent, and, aimed at by the player for over 30 frames, somersault over it or flip aside.
// AUTOINJECT
bool NDrone2_DSTATE_NinjaAttackMidRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->alertness = 1.0f;
        Special_SetTimer(&dcv->drone->timerA, Special_FramesFromNow(60.0f));
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (dcv->drone->distanceToTarget > 6.0f && dcv->drone->distanceToTarget <= 12.0f) {
            switch (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1)) {
            case 0:
            case 1:
            case 2:
                DroneAnim_CallAnim(0x67, 0, 0, 0, 0.0f, 0, dcv);
                return true;
            case 3:
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaStandFire, 0);
            case 10:
                return true;
            default:
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaNoRoute, 0);
            }
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);

    case DRONE_MSG_TimerA:
        if ((dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && Rand_Rand(3) == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaStandFire, 0);
        if (dcv->drone->inPlayersSightsFrames > 30) {
            if (Ninja_CanSomersault(dcv, drone))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSomersault, 0);
            if (Rand_Rand(10) == 0 && Ninja_CanSideflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
        }
        Special_SetTimer(&dcv->drone->timerA, Special_FramesFromNow(30.0f));
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 1.0f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 1.0f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// From 2 to 6 m: firing in bursts, flipping aside or back now and then, and every 2 to 4 seconds (timer A) closing
// in (GetCloseToPlayer), at once one time in three.
// AUTOINJECT
bool NDrone2_DSTATE_NinjaAttackShortRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter: {
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->alertness = 1.0f;
        DroneAnim_CallAnim(0x6b, 0, 0, 0, 0.0f, 0, dcv);
        double extra = Float_FRand((float)(FRAME_RATE_INT * 2));
        Special_SetTimer(&dcv->drone->timerA,
                         (uint)(long long)(extra + (double)(GameState.NumFramesUnpaused + FRAME_RATE_INT * 2)));
        DroneWeap_Fire(1, dcv);
        if (Rand_Rand(3) != 0)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaGetCloseToPlayer, 0);
    }

    case DRONE_MSG_Exit:
        dcv->drone->firing = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        // A NaN distance attacks again
        if (!(dcv->drone->distanceToTarget > 2.0f && dcv->drone->distanceToTarget <= 6.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        if (DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[0])) {
            if (Rand_Rand(10) == 0 && Ninja_CanSideflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
            if (Ninja_CanBackflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaBackflip, 0);
        }
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        if (dcv->drone->inPlayersSightsFrames != 0 && Rand_Rand(2) != 0) {
            if (Ninja_CanSideflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
            if (Ninja_CanBackflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaBackflip, 0);
        }
        if (dcv->drone->distanceToTarget > 0.0f && dcv->drone->distanceToTarget <= 2.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSword, 0);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_TimerA:
        if (Rand_Rand(2) != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaGetCloseToPlayer, 0);
        if (Rand_Rand(10) == 0 && Ninja_CanSideflip(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
        if (Ninja_CanBackflip(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaBackflip, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaGetCloseToPlayer, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 1.0f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 1.0f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// Closing in, firing: within 2 m the sword, from 5 to 7 m (with room) a somersault over the opponent. Back to
// ShortRange after 3 seconds (timer A).
// AUTOINJECT
bool NDrone2_DSTATE_NinjaGetCloseToPlayer(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x6b, 0, 0, 0, 0.0f, 0, dcv);
        Special_SetTimer(&dcv->drone->timerA, FRAME_RATE_INT * 3 + GameState.NumFramesUnpaused);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (dcv->drone->distanceToTarget > 0.0f && dcv->drone->distanceToTarget <= 2.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSword, 0);
        switch (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1)) {
        case 2:
            if (Ninja_CanSomersault(dcv, drone))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSomersault, 0);
            // fall through
        case 0:
        case 1:
            DroneAnim_CallAnim(0x67, 0, 0, 0, 0.0f, 0, dcv);
            if (!dcv->drone->burstOver && drone->firing)
                return true;
            DroneWeap_Fire(1, dcv);
            return true;
        case 10:
            return true;
        default:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        }

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttackShortRange, 0);
        return true;

    default:
        return false;
    }
}

// A sword stroke (anim state 0x6d) on an opponent within 2 m, then NinjaAttack. Hits do half their effect.
// AUTOINJECT
bool NDrone2_DSTATE_NinjaSword(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        if (dcv->drone->distanceToTarget > 0.0f && dcv->drone->distanceToTarget <= 2.0f) {
            DroneAnim_CallAnim(0x6d, 0, DSTATE_NinjaAttack, 0, 0.0f, 0, dcv);
            return true;
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 0.5f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 0.5f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// The flips: each drops its attack route, plays its anim (0x6a somersault, 0x66 back, 0x68 and 0x69 aside; a random
// variant but for the somersault) and fires through it, then attacks again (the somersault with the sword). Hits do
// half their effect.
// AUTOINJECT
bool NDrone2_DSTATE_NinjaSomersault(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        DroneAnim_CallAnim(0x6a, 0, DSTATE_NinjaSword, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (dcv->drone->burstOver || !drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 0.5f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 0.5f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_NinjaBackflip(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        DroneAnim_CallAnim(0x66, Rand_Rand(3) != 0 ? 1 : 0, DSTATE_NinjaAttack, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (dcv->drone->burstOver || !drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 0.5f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 0.5f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// Picks a side to flip to: either with room for both, the first with room for one, else attacks again
// AUTOINJECT
bool NDrone2_DSTATE_NinjaSideflip(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        if (NDrone2_CanMoveToRelativePos(dcv->drone, dcv->gameObj, 3.0f, 0.0f, 0.0f)) {
            if (NDrone2_CanMoveToRelativePos(dcv->drone, dcv->gameObj, -3.0f, 0.0f, 0.0f) && Rand_Rand(2) != 0)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflipRight, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflipLeft, 0);
        }
        if (NDrone2_CanMoveToRelativePos(dcv->drone, dcv->gameObj, -3.0f, 0.0f, 0.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflipRight, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_NinjaSideflipLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        DroneAnim_CallAnim(0x68, Rand_Rand(3) == 0 ? 1 : 0, DSTATE_NinjaAttack, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->firing = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (dcv->drone->burstOver || !drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 0.5f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 0.5f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_NinjaSideflipRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        DroneAnim_CallAnim(0x69, Rand_Rand(3) != 0 ? 1 : 0, DSTATE_NinjaAttack, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->firing = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (dcv->drone->burstOver || !drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 0.5f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 0.5f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// Firing standing, facing the opponent, for 3 seconds (timer A), then NinjaAttack; between bursts, aimed at by the
// player, it may somersault over it or flip aside.
// AUTOINJECT
bool NDrone2_DSTATE_NinjaStandFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x6b, 0, 0, 0, 0.0f, 0, dcv);
        Special_SetTimer(&dcv->drone->timerA, FRAME_RATE_INT * 3 + GameState.NumFramesUnpaused);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        if (dcv->drone->inPlayersSightsFrames != 0) {
            if (Ninja_CanSomersault(dcv, drone))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSomersault, 0);
            if (Rand_Rand(10) == 0 && Ninja_CanSideflip(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaSideflip, 0);
        }
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 1.0f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 1.0f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// With no route to its opponent: standing and firing at it while it sees it, trying the route again every 30 ticks
// (timer A)
// AUTOINJECT
bool NDrone2_DSTATE_NinjaNoRoute(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->alertness = 1.0f;
        Special_SetTimer(&dcv->drone->timerA, Special_FramesFromNow(30.0f));
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x6b, 0, 0, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) != 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        DroneWeap_Fire(1, dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) != 0;
        return true;

    case DRONE_MSG_TimerA:
        if (NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1) <= 3)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NinjaAttack, 0);
        Special_SetTimer(&dcv->drone->timerA, Special_FramesFromNow(30.0f));
        return true;

    case DRONE_MSG_ImpactPunch:
        return Ninja_Hit(dcv, msg, DSTATE_KnockedOut_Anim, 1.0f);

    case DRONE_MSG_ImpactBullet:
        return Ninja_Hit(dcv, msg, DSTATE_Death_Anim, 1.0f);

    case DRONE_MSG_ImpactExplosive:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The space drake

// Standing (health 10), with idle anims while property 0x46 is set; shots do nothing until switch channel 10 comes
// on, and then it goes (DeleteMe).
// AUTOINJECT
bool NDrone2_DSTATE_SpaceDrake(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->health = 10.0f;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        DroneAnim_SetStandIdleAnim(dcv, 0);
        if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0)
            NDrone2_SetIdleTimeOut(dcv, 45, 5);
        return true;

    case DRONE_MSG_Update:
        if (switch_channels[SWITCH_SPACE_DRAKE] != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_DeleteMe, 0);
        DroneAnim_SetStandIdleAnim(dcv, 1);
        return true;

    case DRONE_MSG_StateTimeout:
        if (behaviour_util_getProperty(DRONE_PROP_LOOKS_AROUND, dcv->drone->currentBehaviour) != 0) {
            DroneAnim_CallAnim(0x4b, Rand_Rand(3), 0, 0, 0.0f, 0, dcv);
            NDrone2_SetIdleTimeOut(dcv, 45, 5);
        }
        return true;

    case DRONE_MSG_ScriptEnd:
        DroneAnim_SetStandIdleAnim(dcv, 0);
        return true;

    case DRONE_MSG_ImpactBullet:
        if (switch_channels[SWITCH_SPACE_DRAKE] != 0)
            DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The astronauts

// Floating along its mission path, firing while it sees its opponent, then AstronautCombatMove (at once without a
// path)
// AUTOINJECT
bool NDrone2_DSTATE_AstronautLaunch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        dcv->drone->flags &= ~DRONE_FLAG_BIT1;
        dcv->drone->flies = 1;
        NDrone2_AssignAIPath(dcv, AIPATH_MISSION);
        if (dcv->drone->route2.count == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautCombatMove, 0);
        dcv->drone->floatAcceleration = 6.9f;
        DroneAnim_CallAnim(0x72, 0, 0, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        dcv->drone->firing = 0;
        dcv->drone->stateScratch = 0;
        dcv->drone->stateScratch2 = 0;
        dcv->drone->flags &= ~DRONE_FLAG_COLLIDES;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->flags |= DRONE_FLAG_COLLIDES;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (NDrone2_UpdateMissionRoute(dcv) == 3)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautCombatMove, 0);
        dcv->drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) != 0;
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Knocked back by a hit (DroneMove_AstronautHitMove), for 30 ticks (timer A), then AstronautCombat
// AUTOINJECT
bool NDrone2_DSTATE_AstronautHit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Special_SetTimer(&dcv->drone->timerA, Special_FramesFromNow(30.0f));
        DroneMove_AstronautHitMove(dcv);
        dcv->drone->firing = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautCombat, 0);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Dead: the death channel, the player's tally, a drifting death anim (DroneMove_AstronautDeathMove); it fades after
// NPCGlobals.deadFrames (timer A), or fast with too many bodies about.
// AUTOINJECT
bool NDrone2_DSTATE_AstronautDeath(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(3, dcv);
        DroneFunc_SetDeathChannel(drone);
        NDrone2_LogDroneDeath(dcv);
        dcv->drone->firing = 0;
        DroneAnim_CallAnim(0x6f, Rand_Rand(2), 0, 0, 0.0f, 0, dcv);
        DroneMove_AstronautDeathMove(dcv);
        Special_SetTimer(&dcv->drone->timerA, NPCGlobals.deadFrames + GameState.NumFramesUnpaused);
        return true;

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

// Floating still in combat (anim state 0x70), turning on its opponent (DroneMove_AstronautCombat)
// AUTOINJECT
bool NDrone2_DSTATE_AstronautCombat(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->floatAcceleration = 0.0f;
        dcv->drone->floatsToMoveTarget = 0;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        DroneAnim_CallAnim(0x70, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (drone->opponent != NULL)
            DroneMove_AstronautCombat(dcv);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Floating about in combat for 5 seconds (timer A), then AstronautCombat; hits knock it back (AstronautHit)
// AUTOINJECT
bool NDrone2_DSTATE_AstronautCombatMove(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->floatAcceleration = 2.0f;
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        Special_SetTimer(&dcv->drone->timerA, FRAME_RATE_INT * 5 + GameState.NumFramesUnpaused);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautCombat, 0);
        DroneMove_AstronautCombat(dcv);
        DroneMove_AstronautAnim(dcv);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautCombat, 0);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_AstronautHit, 0);
        return true;

    default:
        return false;
    }
}
