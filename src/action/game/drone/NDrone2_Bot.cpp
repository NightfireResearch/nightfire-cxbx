// The multiplayer bots' drone states (DSTATE_BotInit .. DSTATE_BotIdle): the bots' life and death, their impacts and
// their moves in combat. The brain is DSTATE_BotGlobal; the goals, weapons and aim are the BOT and BOTSTATE code. See
// docs/drone/bots-and-navigation/README.md 5.

#include "NDrone2_Bot.h"
#include "BOT.h"
#include "BOTSTATE.h"                   // BOTSTATE_pickGoal
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "DroneWeapon.h"
#include "NDrone2_Combat.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Opponent.h"
#include "../mp/multiplayer.h"          // MP_PlayerKilled, MP_ResetBotPickupTimes
#include "../../game.h"                 // GameState
#include "../../engine/Collide.h"       // Coll_AddHitToList

#include <math.h>
#include <stdint.h>

#pragma fp_contract(off)

// The bot code's, called by address
#define BOT_soundEffect ((void (__cdecl *)(DCVars_tag *dcv, int sound))0x0001aca0)
#define BOTSTATE_setNearestNavNode ((void (__cdecl *)(DCVars_tag *dcv))0x0001bb30)
#define BOTSTATE_uninitGoal ((void (__cdecl *)(BOT_vars_t *bot, uint slot))0x0001bb50)
#define BOTSTATE_setGoalPickPrefs ((void (__cdecl *)(BOT_vars_t *bot, uint slot, float health, float ammo, float weapon, float objective, uchar flags, uint maxDistance))0x0001bef0)
#define BOTSTATE_stopAnim ((void (__cdecl *)(Drone_tag *drone))0x0001c2d0)
#define BOTSTATE_chooseUnarmedAttackAnim ((short (__cdecl *)(DCVars_tag *dcv))0x0001b970)
#define BOTSTATE_changeWeapon ((uint (__cdecl *)(BOT_vars_t *bot, uint weapon, char now, char quiet))0x0001e3f0)
// A random combat move for the bot, or 0 (FUN_0001c8c0; -1 for any)
#define BOTSTATE_chooseCombatMove ((short (__cdecl *)(DCVars_tag *dcv, int moves))0x0001c8c0)
// Sends a message to the bots of a team (2 every bot): extraData, after delay frames, from sender
#define MP_sendTeamBotMessage ((void (__cdecl *)(int team, uint msgType, uint extraData, uint delay, uint sender))0x0009e430)

// The anim states these ask for (our names)
#define DANIM_STUNNED 0x2d
#define DANIM_DEATH_EXPLOSION 0x44
#define DANIM_ROLL_LEFT 0x53
#define DANIM_ROLL_RIGHT 0x54

// The message a drone sends when it dies in view (property 0x31): DRONE_MSG_DroneAlert
#define DEATH_ALERT_MSG 0x15

constexpr float kStunRecoverScale = -4.0f;      // the stun lasts four times the recover stat
constexpr float kUnarmedRange = 1.5f;

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

static void SetTimerA(DCVars_tag *dcv, uint dueFrame) {
    dcv->drone->timerA.dueFrame = dueFrame;
    dcv->drone->timerA.unknown8 = 0;
    dcv->drone->timerA.on = 1;
}

static void SetTimerB(DCVars_tag *dcv, uint dueFrame) {
    dcv->drone->timerB.dueFrame = dueFrame;
    dcv->drone->timerB.unknown8 = 0;
    dcv->drone->timerB.on = 1;
}

static BOT_vars_t *Bot(DCVars_tag *dcv) {
    return dcv->drone->botVars;
}

// ---- life and death

// The first state: the nearest nav node found, then idle.
// AUTOINJECT
bool NDrone2_DSTATE_BotInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        BOTSTATE_setNearestNavNode(dcv);
        Bot(dcv)->stateClass = BOTSTATE_CLASS_NONE;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
    default:
        return false;
    }
}

// Made again as a new object (BOT_respawn).
// AUTOINJECT
bool NDrone2_DSTATE_BotRespawn(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        BOT_respawn(dcv->drone->gameObj, dcv->drone->botVars->playerIndex, 0);
        return true;
    default:
        return false;
    }
}

// The death cry, the opponent and the goal dropped, the death counted, and the death anim, then DSTATE_BotDead; the
// weapon dropped each frame.
// AUTOINJECT
bool NDrone2_DSTATE_BotDeathAnim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        BOT_soundEffect(dcv, 1);
        NDrone2_SetOpponent(dcv->drone, NULL);
        BOTSTATE_uninitGoal(Bot(dcv), Bot(dcv)->activeGoal);
        NDrone2_LogDroneDeath(dcv);
        MP_PlayerKilled(dcv->gameObj);
        DroneAnim_LocationDeathAnim(dcv, DSTATE_BotDead);
        return true;
    case DRONE_MSG_Update:
        DroneWeap_DropWeapon(dcv);
        return true;
    default:
        return false;
    }
}

// The same, thrown by the explosion: its source is entered in the object's hit list (at 9999 m), and the anim faces
// the blast (the state's parameter is the impact).
// AUTOINJECT
bool NDrone2_DSTATE_BotDeathByExplosion(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        BOT_soundEffect(dcv, 1);
        DroneImpact_tag *impact = (DroneImpact_tag *)msg->extraData;
        Coll_AddHitToList(&dcv->gameObj->hitList, 9999.0f, NULL, impact->source);
        uint dir = NDrone2_ExplosionDir(dcv, (DroneImpact_tag *)dcv->aiStateMachine->stateParam);
        DroneAnim_CallAnim(DANIM_DEATH_EXPLOSION, dir, DSTATE_BotDead, 0, 0.0f, 0, dcv);
        NDrone2_SetOpponent(dcv->drone, NULL);
        BOTSTATE_uninitGoal(Bot(dcv), Bot(dcv)->activeGoal);
        NDrone2_LogDroneDeath(dcv);
        MP_PlayerKilled(dcv->gameObj);
        DroneWeap_DropWeapon(dcv);
        return true;
    }
    default:
        return false;
    }
}

// With property 0x31, a drone that died in view (not by a head shot) tells those around it, and again each second
static void BotDeadAlert(DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_31, dcv->drone->currentBehaviour) == 0)
        return;
    if (!dcv->drone->headShot)
        NDrone2_DroneAlertToObject(dcv, DEATH_ALERT_MSG, dcv->gameObj, 0.0f, 0.0f, 1.0f);
    SetTimerB(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT);
}

// Lying dead for 3 s, then respawning; the pickups it visited are forgotten.
// AUTOINJECT
bool NDrone2_DSTATE_BotDead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        MP_ResetBotPickupTimes(Bot(dcv)->botIndex);
        NDrone2_SetOpponent(dcv->drone, NULL);
        BOTSTATE_uninitGoal(Bot(dcv), Bot(dcv)->activeGoal);
        NDrone2_SetAsDead(dcv);
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT * 3);
        BotDeadAlert(dcv);
        return true;
    case DRONE_MSG_TimerA:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotRespawn, 0);
    case DRONE_MSG_TimerB:
        BotDeadAlert(dcv);
        return true;
    default:
        return false;
    }
}

// ---- impacts

// Hit by a bullet: a fifth of a second (timer B), then a combat move or the attack. Another bullet or an explosion
// starts the impact again.
// AUTOINJECT
bool NDrone2_DSTATE_BotImpactBullet(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        NDrone2_BulletImpact(dcv, (HITDATA_tag *)msg->extraData, 0, 1);
        SetTimerB(dcv, FRAME_RATE_INT / 5 + GameState.NumFramesUnpaused);
        return true;
    case DRONE_MSG_Exit:
        if (dcv->aiStateMachine->prevState == DSTATE_BotImpactBullet)
            BOTSTATE_stopAnim(dcv->drone);
        return true;
    case DRONE_MSG_ImpactBullet:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactBullet, (int)(uintptr_t)msg->extraData);
    case DRONE_MSG_ImpactExplosive:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactExplosive, (int)(uintptr_t)msg->extraData);
    case DRONE_MSG_TimerB: {
        BOTSTATE_stopAnim(dcv->drone);
        short next = BOTSTATE_chooseCombatMove(dcv, -1);
        if (next == 0)
            next = DSTATE_BotAttack;
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)next, 0);
    }
    default:
        return false;
    }
}

// Thrown by an explosion; a punch or a bullet starts that impact.
// AUTOINJECT
bool NDrone2_DSTATE_BotImpactExplosive(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 1);
        return true;
    case DRONE_MSG_ImpactPunch:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactPunch, (int)(uintptr_t)msg->extraData);
    case DRONE_MSG_ImpactBullet:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactBullet, (int)(uintptr_t)msg->extraData);
    default:
        return false;
    }
}

// Punched; another punch, a bullet or an explosion starts that impact.
// AUTOINJECT
bool NDrone2_DSTATE_BotImpactPunch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        NDrone2_PunchImpact(dcv, (HITDATA_tag *)msg->extraData, 0, 1);
        return true;
    case DRONE_MSG_ImpactPunch:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactPunch, (int)(uintptr_t)msg->extraData);
    case DRONE_MSG_ImpactBullet:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactBullet, (int)(uintptr_t)msg->extraData);
    case DRONE_MSG_ImpactExplosive:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactExplosive, (int)(uintptr_t)msg->extraData);
    default:
        return false;
    }
}

// Stunned for four times the recover stat (timer A), then idle.
// AUTOINJECT
bool NDrone2_DSTATE_BotImpactStunGrenade(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        DroneAnim_CallAnim(DANIM_STUNNED, 0, 0, 0, 0.0f, 0, dcv);
        int recover = dcv->drone->recover;
        SetTimerA(dcv, GameState.NumFramesUnpaused - Ftol((double)recover * kStunRecoverScale));
        return true;
    }
    case DRONE_MSG_TimerA:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
    default:
        return false;
    }
}

// ---- the simple states

// Picks up what it finds (pickups only), or idles.
// AUTOINJECT
bool NDrone2_DSTATE_BotCollector(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        BOTSTATE_setGoalPickPrefs(Bot(dcv), 0, 1.0f, 1.0f, 1.0f, 0.0f, 0, 0);
        if (BOTSTATE_pickGoal(dcv, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotGotoGoalPosition, 0);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
    default:
        return false;
    }
}

// Back to idle at once: the personality states BotGuardian .. BotAssassin, BotAttackNoOpponent and BotStuck share it.
// AUTOINJECT
bool NDrone2_DSTATE_BotStuck(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
    default:
        return false;
    }
}

// No route to the opponent: the attack route dropped, and idle.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackNoRoute(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        NDrone2_InvalidateAttackRoute(dcv);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
    default:
        return false;
    }
}

// Straight to the attack: BotSeenOpponent and BotSeenDroneShot share it.
// AUTOINJECT
bool NDrone2_DSTATE_BotSeenDroneShot(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
    default:
        return false;
    }
}

// Handles nothing but the null message.
// AUTOINJECT
bool NDrone2_DSTATE_BotHeardNoise(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return msg->msgType == DRONE_MSG_Null;
}

// ---- the attack moves

// Rolling to the side, aiming, then crouching to fire (or, with no opponent, BotAttackNoOpponent).
static bool BotAttackRoll(DCVars_tag *dcv, MsgObject *msg, uint anim) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(anim, 0, DSTATE_BotAttackCrouch, 0, 0.0f, 0, dcv);
        return true;
    case DRONE_MSG_Update:
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        return true;
    case DRONE_MSG_ScriptEnd:
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackCrouch, 0);
    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_BotAttackRollLeftCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return BotAttackRoll(dcv, msg, DANIM_ROLL_LEFT);
}

// AUTOINJECT
bool NDrone2_DSTATE_BotAttackRollRightCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return BotAttackRoll(dcv, msg, DANIM_ROLL_RIGHT);
}

// Changing to nextWeapon (the team's bots told of the weapon a second later), then attacking.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackChangeWeapon(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        dcv->drone->burstShots = 0;
        dcv->drone->firing = 0;
        if (BOTSTATE_changeWeapon(Bot(dcv), Bot(dcv)->nextWeapon, 1, 0) != 0)
            // the sender is the object itself, not its state machine's id
            MP_sendTeamBotMessage(2, 0x44, Bot(dcv)->weapon, FRAME_RATE_INT, (uint)(uintptr_t)dcv->gameObj);
        dcv->drone->initialState = DSTATE_BotAttack;
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
    default:
        return false;
    }
}

// Hand to hand within 1.5 m (backing off when no blow can be struck); further away, the attack.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackUnarmed(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        dcv->drone->burstShots = 0;
        dcv->drone->firing = 0;
        if (dcv->drone->distanceToTarget > kUnarmedRange)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        if (BOTSTATE_chooseUnarmedAttackAnim(dcv) == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackBackoff, 0);
        return true;
    default:
        return false;
    }
}
