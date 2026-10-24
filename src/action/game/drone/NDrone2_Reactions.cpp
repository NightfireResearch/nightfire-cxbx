// How a drone reacts to what is done to it: the impact states (a punch, an explosion, a shot), being knocked out,
// the taser and the stuns (stun grenades and darts), smoke, and surrendering. The impact messages themselves go
// through DroneFunc_HandleImpact (NDrone2_Damage.cpp).

#include "NDrone2_Reactions.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Opponent.h"            // NDrone2_DroneAlertToObject
#include "DroneAnim.h"
#include "DroneTables.h"
#include "DroneVision.h"                 // NDrone2_CanSeeObject
#include "DroneWeapon.h"
#include "Behaviour.h"
#include "../../game.h"                  // GameState
#include "../../math/math.h"             // Vec_SqDist3D
#include "../sp/PlayerStats.h"           // PlrStat_LogEnemyDisabled, PlrStat_LogEnemySurrender
#include "../../util/Random.h"

#include <bit>
#include <math.h>
#include "NDrone2_Alert.h"

// The anim state that gets a stunned drone back up
#define DANIM_GET_UP 0x76
// The taser's two falls; the Stunned state's loops follow from which (0x2f after 0x31, 0x30 after 0x32)
#define DANIM_TASER_FALL_A 0x31
#define DANIM_TASER_FALL_B 0x32

// The skin class that turns enemy when it gets up (as DSTATE_Civilian has it)
#define SKIN_CLASS_TURNCOAT 16

// Mission fail reasons for stunning drone types 0xc and 0x11
#define FAIL_TASERED 13
#define FAIL_STUN_DARTED 14

static_assert(std::bit_cast<uint32_t>((float)M_PI_2) == 0x3fc90fdb, "not the original's pi/2");

static void Reactions_SetTimer(DroneTimer *timer, uint dueFrame, uint data = 0) {
    timer->dueFrame = dueFrame;
    timer->data = data;
    timer->on = 1;
}

// GameState.NumFramesUnpaused plus ticks scaled by FRAME_RATE_DIV, as the original works it out in x87 and __ftol2
static uint Reactions_FramesFromNow(float ticks) {
    return (uint)(long long)((double)FRAME_RATE_DIV * ticks + (double)GameState.NumFramesUnpaused);
}

// ---------------------------------------------------------------------------------------------------------------
// The impact states: each plays its impact once on entering, then goes back to the state it resumes

// AUTOINJECT
bool NDrone2_DSTATE_PunchImpact(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_PunchImpact(dcv, (HITDATA_tag *)msg->extraData, 0, 1);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_ExplosiveImpact(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// The bullet impact itself is played by whoever sends the drone here (NDrone2_BulletImpact); another shot is taken
// as it comes, and an explosion goes to ExplosiveImpact.
// AUTOINJECT
bool NDrone2_DSTATE_BulletImpact(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        drone->firing = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
        NDrone2_BulletImpact(dcv, (HITDATA_tag *)msg->extraData, 0, 0);
        return true;

    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_ExplosiveImpact, (intptr_t)msg->extraData);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Knocked out

// Falling unconscious: the drone has no health left, drops its weapon and lies as dead. An enemy counts as disabled
// for the player; leaving, the death channel is set (not in TowerA or TowerB).
// AUTOINJECT
bool NDrone2_DSTATE_KnockedOut_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Update:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        gameObj->effectFlags |= EFFECT_NO_COLLISION;
        Drone_AlertStatusSet(3, dcv);
        drone->health = 0.0f;
        DroneAnim_CallAnim(0x50, 0, DSTATE_Dead, 0, 0.0f, 0, dcv);
        dcv->drone->flags |= DRONE_FLAG_KNOCKED_OUT;
        DroneWeap_DropWeapon(dcv);
        if (drone->side == 1)
            PlrStat_LogEnemyDisabled(0);
        return true;

    case DRONE_MSG_Exit:
        if (GameState.CurrentLevelHashcode != HT_Level_TowerA && GameState.CurrentLevelHashcode != HT_Level_TowerB)
            DroneFunc_SetDeathChannel(drone);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_Knocked_Out(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The taser and the stuns

// Whether a player is within twice radius of the drone (and 3 m of its height), or a drone within radius
// AUTOINJECT
bool DroneFunc_AnyoneNear(DCVars_tag *dcv, float radius) {
    float reach = (radius + radius) * (radius + radius);
    for (int i = 0; i < 4; i++) {
        obj_tag *player = glb_players[i];
        if (player == NULL)
            continue;
        // A NaN distance is not near
        if (Vec_SqDist3D(&dcv->gameObj->position, &player->position) < reach &&
            fabs((double)dcv->gameObj->position.y - player->position.y) <= 3.0)
            return true;
    }
    return NDrone2_DroneNearPos(&dcv->gameObj->position, radius, dcv->gameObj);
}

// Tasered: the drone falls (one of two ways) and drops its weapon, unless a civilian or unarmed. Timer A (30 ticks,
// restarted by another taser hit) checks every 5 ticks for the fall to have ended, then it is stunned for the time
// worked out on entering. It tells the others it is hurt (DroneFunc_SendHurtMessage, inline in the original).
// AUTOINJECT
bool NDrone2_DSTATE_Taser(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        drone->stateScratch3 = DroneFunc_RecoverTime(dcv, (HITDATA_tag *)msg->extraData);
        if (!NDrone2_IsCivilian(dcv) && dcv->drone->animSet != 4) {
            DroneWeap_DropWeapon(dcv);
            DroneWeap_ChangeWeapon(dcv, 1);
        }
        if (drone->dtype == DTYPE_Zoe || drone->dtype == DTYPE_Mayhew)
            DroneFunc_SetMissionFailReason(drone, FAIL_TASERED);
        Drone_AlertStatusSet(2, dcv);
        // stateScratch: the fall played
        dcv->drone->stateScratch = Rand_Rand(2) != 0 ? DANIM_TASER_FALL_A : DANIM_TASER_FALL_B;
        DroneAnim_CallAnim(dcv->drone->stateScratch, 0, 0, 0, 0.0f, 0, dcv);
        Reactions_SetTimer(&dcv->drone->timerA, Reactions_FramesFromNow(30.0f));
        DroneFunc_SendHurtMessage(dcv);
        return true;

    case DRONE_MSG_TimerA:
        if (drone->animState == DANIM_TASER_FALL_A || drone->animState == DANIM_TASER_FALL_B)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Stunned, 0);
        Reactions_SetTimer(&dcv->drone->timerA, Reactions_FramesFromNow(5.0f));
        return true;

    case DRONE_MSG_ImpactTaser:
        Reactions_SetTimer(&dcv->drone->timerA, Reactions_FramesFromNow(30.0f));
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// Lying stunned after the taser for the time worked out then (timer A), longer while anyone is within 2 m; then it
// gets up. Another taser hit after the second fall adds to the time, after the first it falls again.
// AUTOINJECT
bool NDrone2_DSTATE_Stunned(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        Reactions_SetTimer(&dcv->drone->timerA, drone->stateScratch3 + GameState.NumFramesUnpaused);
        Drone_AlertStatusSet(2, dcv);
        if (dcv->drone->stateScratch == DANIM_TASER_FALL_B)
            DroneAnim_CallAnim(0x30, 0, 0, 0, 0.0f, 0, dcv);
        else
            DroneAnim_CallAnim(0x2f, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->smallCollision = 1;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->smallCollision = 0;
        return true;

    case DRONE_MSG_TimerA:
        if (!DroneFunc_AnyoneNear(dcv, 2.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Stunned_Recover, 0);
        Reactions_SetTimer(&dcv->drone->timerA, FRAME_RATE_INT * 5 + GameState.NumFramesUnpaused);
        return true;

    case DRONE_MSG_ImpactTaser:
        if (dcv->drone->stateScratch != DANIM_TASER_FALL_B)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Taser, 0);
        Reactions_SetTimer(&dcv->drone->timerA, DroneFunc_RecoverTime(dcv, (HITDATA_tag *)msg->extraData) +
                                                    GameState.NumFramesUnpaused);
        return true;

    case DRONE_MSG_ImpactPunch:
        NDrone2_PainTalk(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KnockedOut_Anim, 0);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    case DRONE_MSG_ImpactStunGrenade:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactStunDart:
        Reactions_SetTimer(&dcv->drone->timerA, DroneFunc_RecoverTime(dcv, (HITDATA_tag *)msg->extraData) +
                                                    GameState.NumFramesUnpaused);
        return true;

    default:
        return false;
    }
}

// Getting up after the taser: a civilian goes back to the state it resumes, the rest (with weapon 6) attack. Skin
// class 16 turns enemy.
// AUTOINJECT
bool NDrone2_DSTATE_Stunned_Recover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        if (dcv->drone->skinClass == SKIN_CLASS_TURNCOAT)
            dcv->drone->side = 1;
        if (NDrone2_IsCivilian(dcv)) {
            DroneAnim_CallAnim(DANIM_GET_UP, 0, (short)dcv->aiStateMachine->resumeState, 0, 0.0f, 0, dcv);
            return true;
        }
        DroneWeap_ChangeWeapon(dcv, 6);
        DroneAnim_CallAnim(DANIM_GET_UP, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// Hit by a stun grenade: the drone falls one of two ways and drops its weapon unless unarmed, then StunGrenadeLoop
// for the time worked out here.
// AUTOINJECT
bool NDrone2_DSTATE_StunGrenadeImpact(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        drone->stateScratch3 = DroneFunc_RecoverTime(dcv, (HITDATA_tag *)msg->extraData);
        if (dcv->drone->animSet != 4) {
            DroneWeap_DropWeapon(dcv);
            DroneWeap_ChangeWeapon(dcv, 1);
        }
        Drone_AlertStatusSet(2, dcv);
        DroneAnim_CallAnim(Rand_Rand(2) != 0 ? 0x2d : 0x2e, 0, DSTATE_StunGrenadeLoop, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactTaser:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// Stunned by a grenade for the time worked out on impact (timer A), longer while anyone is within 2 m; another stun
// grenade or dart starts the time again.
// AUTOINJECT
bool NDrone2_DSTATE_StunGrenadeLoop(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        Reactions_SetTimer(&dcv->drone->timerA, drone->stateScratch3 + GameState.NumFramesUnpaused);
        dcv->drone->smallCollision = 1;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->smallCollision = 0;
        return true;

    case DRONE_MSG_TimerA:
        if (!DroneFunc_AnyoneNear(dcv, 2.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_StunGrenadeRecover, 0);
        Reactions_SetTimer(&dcv->drone->timerA, FRAME_RATE_INT * 5 + GameState.NumFramesUnpaused);
        return true;

    case DRONE_MSG_ImpactPunch:
        NDrone2_PainTalk(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KnockedOut_Anim, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        Reactions_SetTimer(&dcv->drone->timerA, DroneFunc_RecoverTime(dcv, (HITDATA_tag *)msg->extraData) +
                                                    GameState.NumFramesUnpaused);
        return true;

    default:
        return false;
    }
}

// Getting up after a stun grenade: a civilian goes back to the state it resumes, the rest attack
// AUTOINJECT
bool NDrone2_DSTATE_StunGrenadeRecover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        if (NDrone2_IsCivilian(dcv)) {
            DroneAnim_CallAnim(DANIM_GET_UP, 0, (short)dcv->aiStateMachine->resumeState, 0, 0.0f, 0, dcv);
        } else {
            NF_WARN_IF(drone->secondWeapon == 0, "ERROR: Drone has no second weapon"); // GC check (0x800b032c)
            DroneAnim_CallAnim(DANIM_GET_UP, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        }
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// Hit by a stun dart: the drone falls one of two ways (anim sets 5, 6 and 9 drop their weapon), then StunDartLoop
// for the time worked out here.
// AUTOINJECT
bool NDrone2_DSTATE_StunDartImpact(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        drone->stateScratch3 = DroneFunc_RecoverTime(dcv, (HITDATA_tag *)msg->extraData);
        if (drone->animSet == 5 || drone->animSet == 6 || drone->animSet == 9) {
            DroneWeap_DropWeapon(dcv);
            DroneWeap_ChangeWeapon(dcv, 1);
        }
        if (drone->dtype == DTYPE_Zoe || drone->dtype == DTYPE_Mayhew)
            DroneFunc_SetMissionFailReason(drone, FAIL_STUN_DARTED);
        Drone_AlertStatusSet(0, dcv);
        DroneAnim_CallAnim(Rand_Rand(2) != 0 ? 0x2b : 0x2c, 0, DSTATE_StunDartLoop, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// Asleep from a stun dart for the time worked out on impact (timer A), longer while anyone is within 2 m; another
// taser hit or dart starts the time again.
// AUTOINJECT
bool NDrone2_DSTATE_StunDartLoop(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        Reactions_SetTimer(&dcv->drone->timerA, drone->stateScratch3 + GameState.NumFramesUnpaused);
        dcv->drone->smallCollision = 1;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->smallCollision = 0;
        return true;

    case DRONE_MSG_TimerA:
        if (!DroneFunc_AnyoneNear(dcv, 2.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_StunDartRecover, 0);
        Reactions_SetTimer(&dcv->drone->timerA, FRAME_RATE_INT * 5 + GameState.NumFramesUnpaused);
        return true;

    case DRONE_MSG_ImpactPunch:
        NDrone2_PainTalk(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KnockedOut_Anim, 0);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    case DRONE_MSG_ImpactStunGrenade:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunDart:
        Reactions_SetTimer(&dcv->drone->timerA, DroneFunc_RecoverTime(dcv, (HITDATA_tag *)msg->extraData) +
                                                    GameState.NumFramesUnpaused);
        return true;

    default:
        return false;
    }
}

// Waking from a stun dart: a civilian goes back to the state it resumes, the rest attack. Another dart puts it back
// down.
// AUTOINJECT
bool NDrone2_DSTATE_StunDartRecover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_AttackNow:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        if (NDrone2_IsCivilian(dcv))
            DroneAnim_CallAnim(DANIM_GET_UP, 0, (short)dcv->aiStateMachine->resumeState, 0, 0.0f, 0, dcv);
        else
            DroneAnim_CallAnim(DANIM_GET_UP, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunGrenade:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    case DRONE_MSG_ImpactStunDart:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_StunDartImpact, 0);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Smoke

// Caught in smoke: the drone doubles over (anim state 0x20), then SmokedOut_Loop
// AUTOINJECT
bool NDrone2_DSTATE_SmokedOut(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(2, dcv);
        DroneAnim_CallAnim(0x20, 0, DSTATE_SmokedOut_Loop, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        DroneFunc_HandleImpact(dcv, msg, 0, 0);
        return true;

    default:
        return false;
    }
}

// Coughing in smoke until 5 frames pass with none (each smoke message restarts timer A)
// AUTOINJECT
bool NDrone2_DSTATE_SmokedOut_Loop(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(0x20, 0, 0, 0, 0.0f, 0, dcv);
        Reactions_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + 5);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactSmoke:
        Reactions_SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + 5);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SmokedOut_Recover, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
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

// Out of the smoke: anim state 10, then the state it resumes
// AUTOINJECT
bool NDrone2_DSTATE_SmokedOut_Recover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0xa, 0, (short)dcv->aiStateMachine->resumeState, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
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

// ---------------------------------------------------------------------------------------------------------------
// Surrender

// Whether a surrendered drone takes up arms again: with no opponent, beyond 12 m of it, beyond 2 m of it while it is
// unarmed, while it faces away, or when it cannot see it
// AUTOINJECT
bool NDrone2_CheckUnsurrender(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *opponent = drone->opponent;
    if (opponent == NULL)
        return true;
    // A NaN distance goes on to the facing test
    if (drone->distanceToTarget > 12.0f)
        return true;
    if (drone->distanceToTarget > 2.0f && opponent->animState->currentWeaponId <= 1)
        return true;
    if (fabsf(drone->opponentFacing) > (float)M_PI_2)
        return true;
    return !NDrone2_CanSeeObject(drone, opponent, 5, 0);
}

// Putting its hands up: the drone drops its weapon and counts as surrendered for the player. Every 15 ticks (timer A)
// it tells the drones around it (message 0x13). Death script 0x0600021f is dropped, with the death channel.
// AUTOINJECT
bool NDrone2_DSTATE_Surrender_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(0x5c, 0, DSTATE_Surrendered, 0, 0.0f, 0, dcv);
        Drone_AlertStatusSet(2, dcv);
        DroneWeap_DropWeapon(dcv);
        DroneWeap_ChangeWeapon(dcv, 1);
        Reactions_SetTimer(&dcv->drone->timerA, Reactions_FramesFromNow(15.0f), 1);
        PlrStat_LogEnemySurrender(0);
        if (drone->deathScript == HT_Script_DeathScriptIcyQuip) {
            drone->deathSwitchChannel = 0;
            drone->deathScript = (HASHCODE)0;
        }
        return true;

    case DRONE_MSG_Update:
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, (float)M_PI, 1);
        return true;

    case DRONE_MSG_TimerA:
        dcv->drone->knowledge |= DRONE_KNOWS_SURRENDERED;
        NDrone2_DroneAlertToObject(dcv, 0x13, dcv->gameObj, 0.0f, 0.0f, 1.0f);
        Reactions_SetTimer(&dcv->drone->timerA, Reactions_FramesFromNow(15.0f), 1);
        return true;

    case DRONE_MSG_ImpactPunch:
        NDrone2_PainTalk(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KnockedOut_Anim, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Death_Anim, 0);
        return true;

    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// Hands up, facing its opponent, until NDrone2_CheckUnsurrender lets it go
// AUTOINJECT
bool NDrone2_DSTATE_Surrendered(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        if (drone->opponent == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 1);
        return true;

    case DRONE_MSG_Update:
        if (NDrone2_CheckUnsurrender(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Unsurrender_Anim, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
        NDrone2_PainTalk(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_KnockedOut_Anim, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, 0, 1);
        return true;

    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Death_Anim, 0);
        return true;

    case DRONE_MSG_ImpactExplosive:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        return true;

    default:
        return false;
    }
}

// Lowering its hands to attack again, turning to its opponent
// AUTOINJECT
bool NDrone2_DSTATE_Unsurrender_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        DroneAnim_CallAnim(0xa, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
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
