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
#include "NDrone2_Cover.h"
#include "NDrone2_Route.h"              // NDrone2_MoveToGoalPosition
#include "NDrone2_Alert.h"              // NDrone2_MoveToAlertPosition, NDrone2_DistanceToAIPoint
#include "../obj/control.h"             // Control_Plr2Ind
#include "NDrone2_Damage.h"
#include "NDrone2_Opponent.h"
#include "../mp/multiplayer.h"          // MP_PlayerKilled, MP_ResetBotPickupTimes
#include "../../game.h"                 // GameState
#include "../../engine/Collide.h"       // Coll_AddHitToList
#include "../../math/math.h"            // Vec_Subtract, maybeAtan2
#include "../../math/X87Ops.h"          // Extended
#include "../../util/Random.h"          // Rand_Rand

#include <bit>
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
// A combat move for the bot: BOTSTATE_chooseCombatMove's pick, or 0 for staying put; FUN_0001c8c0's moves (-1 any of
// them; Ghidra has no name for it, ours) a random strafe, step or roll, or 0
#define BOTSTATE_chooseCombatMove ((short (__cdecl *)(DCVars_tag *dcv))0x0001c820)
#define BOTSTATE_chooseRandomMove ((short (__cdecl *)(DCVars_tag *dcv, int moves))0x0001c8c0)
#define BOTSTATE_EvasiveMove ((short (__cdecl *)(DCVars_tag *dcv))0x0001bab0)
// Whether the bot may go on with a route of that status (false: it gives up)
#define BOTSTATE_validateRoute ((bool (__cdecl *)(DCVars_tag *dcv, uint status))0x0001c240)
// FUN_0001e5b0: whether the bot's weapon can be reloaded; FUN_0001eeb0: whether a weapon has a reload anim (Ghidra has
// no names for them; ours)
#define BOTWEAP_canReload ((bool (__cdecl *)(obj_tag *gameObj, int unused))0x0001e5b0)
#define BOTWEAP_hasReloadAnim ((bool (__cdecl *)(ushort weapon))0x0001eeb0)
// The state to react to the opponent in (BotAttack or BotAttackFire given), 0 with the drones blinded (FUN_00064910,
// the entry of NDrone2_ReactToOpponentSighted)
#define NDrone2_ReactToOpponentSighted ((short (__cdecl *)(DCVars_tag *dcv, short state))0x00064910)
#define BOTSTATE_getActiveGoal ((BOT_goal_t *(__cdecl *)(Drone_tag *drone))0x0001bed0)
#define BOTSTATE_setPickupVisitTime ((void (__cdecl *)(Drone_tag *drone, void *pickup))0x0001c550)
#define BOTSTATE_initGoal ((void (__cdecl *)(BOT_vars_t *bot, uint slot, uchar kind, void *pos, void *target, uchar flags))0x0001bbc0)
#define BOTSTATE_gotoGoal ((uchar (__cdecl *)(DCVars_tag *dcv, int slot, uint nextState))0x0001bc30)
#define BOTSTATE_combatWeaponChangeChoice ((ushort (__cdecl *)(Drone_tag *drone, obj_tag *opponent, char a, char b, char c))0x0001d750)
#define MP_getProtectionObj_NotExt ((obj_tag *(__cdecl *)(void))0x000a1600)
#define MP_getDemolitionObj_NotExt ((obj_tag *(__cdecl *)(void))0x000a1610)
// Sends a message to the bots of a team (2 every bot): extraData, after delay frames, from sender
#define MP_sendTeamBotMessage ((void (__cdecl *)(int team, uint msgType, uint extraData, uint delay, uint sender))0x0009e430)

// The anim states these ask for (our names)
#define DANIM_BACKOFF 4
#define DANIM_AIM_CROUCH 5
#define DANIM_MOVE_8 8
#define DANIM_AIM_STAND 0xa
#define DANIM_STRAFE_LEFT 0xc
#define DANIM_STRAFE_RIGHT 0xd
#define DANIM_CROUCH_WALK 0xf
#define DANIM_RUN 0x1e
#define DANIM_GUARD 0x22
#define DANIM_STAND_ALERT 0x21
#define DANIM_STAND_IDLE 0x24
#define DANIM_STUNNED 0x2d
#define DANIM_DOOR_WAIT 0x4b
#define DANIM_USE_DOOR 0x4e
#define DANIM_DEATH_EXPLOSION 0x44
#define DANIM_ROLL_LEFT 0x53
#define DANIM_ROLL_RIGHT 0x54
#define DANIM_RELOAD 0x52
#define DANIM_STEP_LEFT 0x5a
#define DANIM_STEP_RIGHT 0x5b

// Drone_tag.flags: makes no combat moves
#define DRONE_FLAG_FROZEN 0x10
// BOT_vars_t.flags: recovering from a hit (no route to the opponent)
#define BOT_FLAG_RECOVERING 2

// Two messages the attack states end on (meaning unknown)
#define DRONE_MSG_1B 0x1b
#define DRONE_MSG_1C 0x1c

// The message a drone sends when it dies in view (property 0x31): DRONE_MSG_DroneAlert
#define DEATH_ALERT_MSG 0x15

constexpr float kStunRecoverScale = -4.0f;      // the stun lasts four times the recover stat
constexpr float kUnarmedRange = 1.5f;
constexpr float kCoverSlide = 0.08f;           // NDrone2_SlideToPosition's share of the way a frame
constexpr float kCoverDrift = 1.5f;            // how far from the cover position it may get
constexpr float kCoverTooClose = 4.0f;         // an opponent in sight this near: no running to cover
constexpr float kCoverAimedAt = 7.5f;          // DroneWeap_OpponentIsAimingAtMe's angle
constexpr float kCoverWaitScale = 120.0f;
constexpr float kGuardRangeSq = 20.25f;        // 4.5 m: a guard follows its friend beyond it
constexpr float kAlertStopRange = 8.0f;        // past the alert point by this, it stops running
constexpr float kObjectiveRadius = 3.0f;       // within this of the protection / demolition object's box
constexpr float kDoorAlertness = 0.66f;
constexpr float kPi = 3.1415927f;
static_assert(std::bit_cast<uint32_t>(kCoverSlide) == 0x3da3d70a, "not the original's 0.08");
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "not the original's pi");
constexpr float kAttackRouteRadius = 2.0f;
constexpr float kQuarterPi = 0.7853982f;
constexpr float kHalfPi = 1.5707964f;
static_assert(std::bit_cast<uint32_t>(kQuarterPi) == 0x3f490fdb, "not the original's pi/4");
static_assert(std::bit_cast<uint32_t>(kHalfPi) == 0x3fc90fdb, "not the original's pi/2");
static_assert(std::bit_cast<uint32_t>(-0.04f) == 0xbd23d70a, "not the original's -0.04");
static_assert(std::bit_cast<uint32_t>(-0.1f) == 0xbdcccccd, "not the original's -0.1");

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
        short next = BOTSTATE_chooseRandomMove(dcv, -1);
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

// ---- running at the opponent

// Plays the run anim, unless it is playing; fires while running when asked, but only at an opponent it sees within a
// quarter turn of its facing. FUN_00043900 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
void NDrone2_SetRunAnim(DCVars_tag *dcv, uint anim, char fire) {
    if ((ushort)dcv->drone->animState != anim)
        DroneAnim_CallAnim(anim, 0, 0, 0, 0.0f, 0, dcv);
    if (fire == 0 ||
        ((dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && dcv->drone->opponentBearing < kQuarterPi))
        dcv->drone->firing = fire;
}

// The run anim towards the opponent, `remaining` of the route ahead: standing aiming while over twice the second combat
// range is left, crouch-walking or walking aimed (anim 8) within it, firing; otherwise running (a bot walks with anim 8
// when it can). FUN_00043960 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
uint NDrone2_ChooseRunAnim(DCVars_tag *dcv, float remaining) {
    char fire = 0;
    uint anim = DANIM_RUN;
    if (dcv->drone->dtype == DTYPE_Bot) {
        anim = DANIM_MOVE_8;
        if (!DroneAnim_CanDoAnimState(dcv, DANIM_MOVE_8, 0))
            anim = DANIM_RUN;
    }
    Drone_tag *drone = dcv->drone;
    uint sees = drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT;
    if (sees && (double)drone->combatRanges[1] * 0.5f > remaining) {
        anim = DANIM_AIM_STAND;
        fire = 1;
    } else if (sees && drone->distanceToTarget < drone->combatRanges[1] && NDrone2_CanCrouchWalk(dcv)) {
        anim = DANIM_CROUCH_WALK;
        fire = 1;
    } else if ((double)dcv->drone->combatRanges[1] * 1.5f > dcv->drone->distanceToTarget && NDrone2_CanMove8(dcv)) {
        anim = DANIM_MOVE_8;
        fire = 1;
    }
    NDrone2_SetRunAnim(dcv, anim, fire);
    return anim;
}

// Turns the drone's heading (angleToSomePoint) towards moveTarget, plus offset; not while unknown3f0 has 0x8000000.
// FUN_00043cf0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
void NDrone2_FaceMoveTarget(DCVars_tag *dcv, float offset) {
    if (dcv->drone->unknown3f0 & 0x8000000)
        return;
    _VECTOR delta;
    Vec_Subtract(&dcv->drone->moveTarget, &dcv->gameObj->position, &delta);
    dcv->drone->angleToSomePoint = (float)(Extended(maybeAtan2(delta.x, delta.z)) + Extended(offset));
}

// The route to the opponent, within 2 m (none while the bot is recovering): its status
static uint BotRouteToOpponent(DCVars_tag *dcv, Drone_tag *drone) {
    if (drone->botVars->flags & BOT_FLAG_RECOVERING)
        return AIROUTE_FOLLOWING;
    return NDrone2_MoveToObject(dcv, dcv->drone->opponent, kAttackRouteRadius, 1);
}

// The combat move BOTSTATE_chooseCombatMove picks (0 stays in the state machine's hands)
static bool BotCombatMove(DCVars_tag *dcv) {
    short move = BOTSTATE_chooseCombatMove(dcv);
    return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
}

// Deciding how to attack: on, now and then, a combat move; each frame, unless it may not move or the opponent is in
// sight and within the second combat range, the route to it (run while on the way, fire on arriving, open a door on
// the way); then, seeing the opponent, the reaction to it.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
        NDrone2_SendAttackMessage(dcv);
        NDrone2_InvalidateAttackRoute(dcv);
        if (Rand_Rand(3) == 0)
            return BotCombatMove(dcv);
        return true;
    case DRONE_MSG_Update: {
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        if (!(dcv->drone->flags & DRONE_FLAG_FROZEN) &&
            behaviour_util_getProperty(DRONE_PROP_40, dcv->drone->currentBehaviour) == 0 &&
            !(dcv->drone->unseenFrames <= 15 && DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[1]))) {
            uint route = BotRouteToOpponent(dcv, drone);
            if (!BOTSTATE_validateRoute(dcv, route))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoRoute, 0);
            switch (route) {
            case AIROUTE_FOLLOWING:
            case AIROUTE_PARTIAL:
            case AIROUTE_DIRECT:
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackRun, 0);
            case AIROUTE_ARRIVED:
                NDrone2_InvalidateAttackRoute(dcv);
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackFire, 0);
            case AIROUTE_DOOR:
                dcv->drone->nodeReturnState = DSTATE_BotAttack;
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDoorOpen, 0);
            }
        }
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) {
            short state = NDrone2_ReactToOpponentSighted(dcv, DSTATE_BotAttackFire);
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)state, 0);
        }
        return true;
    }
    default:
        return false;
    }
}

// Running along the route to the opponent: to cover from a player (or bot) when there is some, backing off when too
// close, and reacting once the opponent is in sight within the second combat range.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackRun(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DroneAnim_CanDoAnimState(dcv, DANIM_MOVE_8, 0) ? DANIM_MOVE_8 : DANIM_RUN, 0, 0, 0, 0.0f, 0,
                           dcv);
        NDrone2_InvalidateAttackRoute(dcv);
        return true;
    case DRONE_MSG_Update: {
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        obj_tag *opponent = dcv->drone->opponent;
        if (opponent != NULL &&
            (opponent->objectType == OBJECTTYPE_PLAYER || opponent->objectType == OBJECTTYPE_DRONE) &&
            NDrone2_CoverAvailable(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverRunTo, 0);
        uint route = BotRouteToOpponent(dcv, drone);
        if (!BOTSTATE_validateRoute(dcv, route))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoRoute, 0);
        switch (route) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            if (dcv->drone->distanceToTarget <= dcv->drone->combatRanges[0] && NDrone2_CanBackoff(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackBackoff, 0);
            NDrone2_ChooseRunAnim(dcv, dcv->drone->route1.remaining);
            break;
        case AIROUTE_ARRIVED:
            NDrone2_InvalidateAttackRoute(dcv);
            break;
        case AIROUTE_DOOR:
            dcv->drone->nodeReturnState = DSTATE_BotAttack;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDoorOpen, 0);
        }
        Drone_tag *self = dcv->drone;
        if (DroneFunc_InRangeOfTarget(dcv, self->combatRanges[1]) && (self->sightFlags & DRONE_SIGHT_SEES_OPPONENT)) {
            short state = NDrone2_ReactToOpponentSighted(dcv, DSTATE_BotAttack);
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)state, 0);
        }
        return true;
    }
    default:
        return false;
    }
}

// Standing aiming and firing: between bursts, now and then a combat move; too close, backing off; with the opponent
// in sight within the fifth combat range, a combat move; else after the opponent along the route.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
        NDrone2_InvalidateAttackRoute(dcv);
        if (!dcv->drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;
    case DRONE_MSG_Update: {
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, -0.04f, 0);
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        if (!dcv->drone->burstOver && drone->firing) {
            if (Rand_Rand(3) != 0)
                return true;
            return BotCombatMove(dcv);
        }
        if ((dcv->drone->flags & DRONE_FLAG_FROZEN) ||
            behaviour_util_getProperty(DRONE_PROP_40, dcv->drone->currentBehaviour) != 0)
            return true;
        if (dcv->drone->distanceToTarget <= dcv->drone->combatRanges[0] && NDrone2_CanBackoff(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackBackoff, 0);
        Drone_tag *self = dcv->drone;
        if (DroneFunc_InRangeOfTarget(dcv, self->combatRanges[4]) && self->unseenFrames <= 15)
            return BotCombatMove(dcv);
        uint route = BotRouteToOpponent(dcv, drone);
        if (!BOTSTATE_validateRoute(dcv, route))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoRoute, 0);
        switch (route) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackRun, 0);
        case AIROUTE_ARRIVED:
            NDrone2_InvalidateAttackRoute(dcv);
            return BotCombatMove(dcv);
        case AIROUTE_DOOR:
            dcv->drone->nodeReturnState = DSTATE_BotAttack;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDoorOpen, 0);
        default:
            return BotCombatMove(dcv);
        }
    }
    default:
        return false;
    }
}

// Strafing aimed for 2 s and a random number more (fewer to the right: the original draws 8 to the left, 4 to the
// right), firing; a combat move (or the attack) once the side is blocked, the run once the opponent is out of sight.
static bool BotAttackStrafe(DCVars_tag *dcv, MsgObject *msg, uint anim, uint extraSeconds,
                            bool (*canStrafe)(DCVars_tag *dcv)) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(anim, 0, 0, 0, 0.0f, 0, dcv);
        if (!dcv->drone->firing)
            DroneWeap_Fire(1, dcv);
        SetTimerA(dcv, (Rand_Rand(extraSeconds) + 2) * FRAME_RATE_INT + GameState.NumFramesUnpaused);
        return true;
    case DRONE_MSG_TimerA:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
    case DRONE_MSG_Update:
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        if (!canStrafe(dcv)) {
            dcv->aiStateMachine->scratch1c = BOTSTATE_chooseCombatMove(dcv);
            StateMachineInfo_tag *sm = dcv->aiStateMachine;
            if (sm->scratch1c != 0)
                return Drone_SM_SetState(sm, (DSTATE)sm->scratch1c, 0);
            return Drone_SM_SetState(sm, DSTATE_BotAttack, 0);
        }
        if (dcv->drone->unseenFrames <= 15)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackRun, 0);
    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_BotAttackStrafeAimLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return BotAttackStrafe(dcv, msg, DANIM_STRAFE_LEFT, 8, NDrone2_CanStrafeLeft);
}

// AUTOINJECT
bool NDrone2_DSTATE_BotAttackStrafeAimRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return BotAttackStrafe(dcv, msg, DANIM_STRAFE_RIGHT, 4, NDrone2_CanStrafeRight);
}

// Running to a new position (moveTarget, set by whoever chose the state) for up to 2 s, facing it, until it is there
// or another drone is within 6 m; then the attack.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackRunChangePosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        NDrone2_InvalidateAttackRoute(dcv);
        DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, Rand_Rand(3) * FRAME_RATE_INT + GameState.NumFramesUnpaused);
        return true;
    case DRONE_MSG_Update:
        if (NDrone2_AtDest(dcv) || NDrone2_NearDrone(dcv, 6.0f))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        NDrone2_FaceMoveTarget(dcv, 0.0f);
        return true;
    case DRONE_MSG_TimerA:
    case DRONE_MSG_1B:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
    default:
        return false;
    }
}

// Backing off aimed, firing, for 2 s; when it cannot, hand to hand with an opponent within a quarter turn ahead (always
// with fists, or the fists trait, else one time in eight) or a random move. While backing: the attack once out of the
// second combat range or blocked, an evasive move when there is one, and between bursts now and then a combat move.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackBackoff(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        if (!NDrone2_CanBackoff(dcv)) {
            Drone_tag *self = dcv->drone;
            if (fabsf(self->opponentBearing) < kHalfPi) {
                BOT_vars_t *bot = self->botVars;
                if ((bot->stats.traitFlags & BOT_TRAIT_FISTS_UP_CLOSE) || bot->weapon == 1 || Rand_Rand(8) == 7)
                    return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackUnarmed, 0);
            }
            short move = BOTSTATE_chooseRandomMove(dcv, 3);
            if (move == 0)
                move = DSTATE_BotAttack;
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
        }
        DroneAnim_CallAnim(DANIM_BACKOFF, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, (Rand_Rand(1) + 2) * FRAME_RATE_INT + GameState.NumFramesUnpaused);
        if (!dcv->drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;
    }
    case DRONE_MSG_Update: {
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        if (dcv->drone->distanceToTarget > dcv->drone->combatRanges[1] || !NDrone2_CanBackoff(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        short move = BOTSTATE_EvasiveMove(dcv);
        if (move != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        if (!(Rand_Rand(4) & 1))
            return true;
        return BotCombatMove(dcv);
    }
    case DRONE_MSG_TimerA: {
        short move = BOTSTATE_chooseCombatMove(dcv);
        if (move == 0)
            move = DSTATE_BotAttack;
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
    }
    default:
        return false;
    }
}

// Crouched aiming and firing: backing off when too close, an evasive move, to cover from a player (or bot), a combat
// move with the opponent in sight within the fifth combat range, else after it along the route. A drone that may not
// move stands up now and then, or once the opponent is out of sight.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DANIM_AIM_CROUCH, 0, 0, 0, 0.0f, 0, dcv);
        NDrone2_InvalidateAttackRoute(dcv);
        if (!dcv->drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;
    case DRONE_MSG_1C:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackRunChangePosition, 0);
    case DRONE_MSG_Update: {
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, -0.1f, 0);
        if (!(dcv->drone->flags & DRONE_FLAG_FROZEN) &&
            behaviour_util_getProperty(DRONE_PROP_40, dcv->drone->currentBehaviour) == 0 &&
            dcv->drone->distanceToTarget <= dcv->drone->combatRanges[0] && NDrone2_CanBackoff(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackBackoff, 0);
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        short move = BOTSTATE_EvasiveMove(dcv);
        if (move != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
        if ((dcv->drone->flags & DRONE_FLAG_FROZEN) ||
            behaviour_util_getProperty(DRONE_PROP_40, dcv->drone->currentBehaviour) != 0) {
            if (Rand_Rand(3) != 0 && dcv->drone->unseenFrames <= 15)
                return true;
            if (!DroneAnim_CanDoAnimState(dcv, DANIM_AIM_CROUCH, 0))
                return true;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        }
        obj_tag *opponent = dcv->drone->opponent;
        if (opponent != NULL &&
            (opponent->objectType == OBJECTTYPE_PLAYER || opponent->objectType == OBJECTTYPE_DRONE) &&
            NDrone2_CoverAvailable(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverRunTo, 0);
        Drone_tag *self = dcv->drone;
        if (DroneFunc_InRangeOfTarget(dcv, self->combatRanges[4]) && self->unseenFrames <= 15)
            return BotCombatMove(dcv);
        uint route = BotRouteToOpponent(dcv, drone);
        if (!BOTSTATE_validateRoute(dcv, route))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoRoute, 0);
        switch (route) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackRun, 0);
        case AIROUTE_ARRIVED:
            NDrone2_InvalidateAttackRoute(dcv);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        case AIROUTE_DOOR:
            return true;
        default:
            return BotCombatMove(dcv);
        }
    }
    default:
        return false;
    }
}

// A step to the side, aimed and firing, then firing standing; one time in five a combat move instead.
static bool BotAttackStep(DCVars_tag *dcv, MsgObject *msg, uint anim) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(anim, 0, DSTATE_BotAttackFire, 0, 0.0f, 0, dcv);
        if (!dcv->drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;
    case DRONE_MSG_Update:
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        if (!DroneAnim_CanDoAnimState(dcv, anim, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackFire, 0);
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        if (Rand_Rand(5) == 3) {
            dcv->aiStateMachine->scratch1c = BOTSTATE_chooseCombatMove(dcv);
            StateMachineInfo_tag *sm = dcv->aiStateMachine;
            if (sm->scratch1c != 0)
                return Drone_SM_SetState(sm, (DSTATE)sm->scratch1c, 0);
        }
        return true;
    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_BotAttackStepAimLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return BotAttackStep(dcv, msg, DANIM_STEP_LEFT);
}

// AUTOINJECT
bool NDrone2_DSTATE_BotAttackStepAimRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return BotAttackStep(dcv, msg, DANIM_STEP_RIGHT);
}

// Reloading: with nothing to reload, changing weapon; with a reload anim, playing it into the attack (backing off when
// too close); without one, straight on to that.
// AUTOINJECT
bool NDrone2_DSTATE_BotAttackReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        dcv->drone->burstShots = 0;
        dcv->drone->firing = 0;
        if (!BOTWEAP_canReload(dcv->gameObj, 0)) {
            dcv->drone->initialState = DSTATE_BotAttack;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);
        }
        bool anim = BOTWEAP_hasReloadAnim(drone->botVars->weapon);
        bool backoff = dcv->drone->distanceToTarget <= dcv->drone->combatRanges[0] && NDrone2_CanBackoff(dcv);
        if (!anim)
            return Drone_SM_SetState(dcv->aiStateMachine, backoff ? DSTATE_BotAttackBackoff : DSTATE_BotAttack, 0);
        DroneAnim_CallAnim(DANIM_RELOAD, 0, backoff ? DSTATE_BotAttackBackoff : DSTATE_BotAttack, 0, 0.0f, 0, dcv);
        return true;
    }
    default:
        return false;
    }
}

// ---- cover

// Whether the opponent is a player or a bot, and the drone has cover from it
static bool BotCoverFromOpponent(DCVars_tag *dcv) {
    obj_tag *opponent = dcv->drone->opponent;
    return opponent != NULL &&
           (opponent->objectType == OBJECTTYPE_PLAYER || opponent->objectType == OBJECTTYPE_DRONE) &&
           NDrone2_CoverAvailable(dcv, 1);
}

// Whether the cover node still serves, the way the drone uses it (its low two bits of coverFlags)
static short BotCoverStillOK(DCVars_tag *dcv, char force) {
    return NDrone2_CoverNodeOK(dcv, dcv->drone->coverNode, (uchar)dcv->drone->coverFlags & DRONE_COVER_SIDE_MASK,
                               force);
}

// The cover states take an impact or see an explosive in the state they are in
static bool BotCoverImpact(DCVars_tag *dcv, MsgObject *msg, char anim) {
    DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, anim);
    return true;
}

static bool BotCoverExplosive(DCVars_tag *dcv, MsgObject *msg) {
    DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
    return true;
}

static void SetAtCornerCover(DCVars_tag *dcv) {
    dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
}

// How long to wait in cover before looking out: up to a share of 120 frames (NPCGlobals.unknown138c, unknown854),
// and a second
static void BotCoverWait(DCVars_tag *dcv) {
    uint wait = Rand_Rand(Ftol((double)NPCGlobals.unknown138c * dcv->drone->unknown854 * kCoverWaitScale));
    SetTimerA(dcv, wait + FRAME_RATE_INT + GameState.NumFramesUnpaused);
}

// Running to the cover node chosen (aiPoint), while it still serves and no opponent in sight is within 4 m; the attack
// otherwise. There, BotCoverInit.
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverRunTo(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        if (dcv->drone->aiPoint.set)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
    case DRONE_MSG_Exit:
        NDrone2_DisownCover(dcv);
        return true;
    case DRONE_MSG_Update: {
        if (!dcv->drone->aiPoint.set || !BotCoverStillOK(dcv, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        Drone_tag *self = dcv->drone;
        if ((self->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && self->distanceToTarget < kCoverTooClose)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        uint route = NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&self->aiPoint.pos, self->aiPoint.radius, 0);
        if (!BOTSTATE_validateRoute(dcv, route))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        switch (route) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case AIROUTE_ARRIVED:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverInit, 0);
        default:
            return true;
        }
    }
    case DRONE_MSG_1B:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
    default:
        return false;
    }
}

// At the cover node: the anim into cover (BotCoverIdle after it), the place to slide to worked out again, sliding into
// it and turning along the cover.
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        DroneAnim_CoverAnim(dcv, COVER_STEP_ARRIVE, DSTATE_BotCoverIdle);
        SetAtCornerCover(dcv);
        dcv->drone->flags &= ~0x800;
        dcv->drone->unknown38 = 1;
        Drone_tag *self = dcv->drone;
        NDrone2_GetCoverPosForTarget(self->coverNode, self->targetSlot, &self->aiPoint.pos, NULL, 0);
        return true;
    }
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_Update:
        NDrone2_SlideToPosition(&dcv->drone->aiPoint.pos, kCoverSlide, 0, dcv);
        NDrone2_SetAngleToCover(dcv, kPi);
        return true;
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        return BotCoverImpact(dcv, msg, 0);
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        return BotCoverImpact(dcv, msg, 1);
    case DRONE_MSG_ExplosiveSeen:
        return BotCoverExplosive(dcv, msg);
    default:
        return false;
    }
}

// Waiting in cover, kept in place: leaving when the node no longer serves or it has drifted 1.5 m. At each wait's end
// (timer A), now and then running to other cover, aiming out when the node serves from its inner angles, running to
// other cover, or after three waits the attack.
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CoverAnim(dcv, COVER_STEP_IDLE, 0);
        BotCoverWait(dcv);
        SetAtCornerCover(dcv);
        dcv->drone->unknown38 = 0;
        dcv->drone->stateScratch = 0;
        return true;
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_Update:
        NDrone2_SlideToPosition(&dcv->drone->aiPoint.pos, kCoverSlide, 0, dcv);
        NDrone2_SetAngleToCover(dcv, kPi);
        if (!BotCoverStillOK(dcv, 0) || Vec_Dist2D(&dcv->gameObj->position, &dcv->drone->aiPoint.pos) > kCoverDrift)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverLeaveNow, 0);
        return true;
    case DRONE_MSG_TimerA:
        if (Rand_Rand(5) == 0 && BotCoverFromOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverRunTo, 0);
        if (BotCoverStillOK(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverAim, 0);
        if (BotCoverFromOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverRunTo, 0);
        BotCoverWait(dcv);
        if (dcv->drone->stateScratch++ > 2)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        return true;
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        return BotCoverImpact(dcv, msg, 0);
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        return BotCoverImpact(dcv, msg, 1);
    case DRONE_MSG_ExplosiveSeen:
        return BotCoverExplosive(dcv, msg);
    default:
        return false;
    }
}

// Aiming out of cover, then firing (BotCoverFire).
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverAim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Update:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CoverAnim(dcv, COVER_STEP_AIM, DSTATE_BotCoverFire);
        SetAtCornerCover(dcv);
        return true;
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        return BotCoverImpact(dcv, msg, 0);
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        return BotCoverImpact(dcv, msg, 1);
    case DRONE_MSG_ExplosiveSeen:
        return BotCoverExplosive(dcv, msg);
    default:
        return false;
    }
}

// Firing out of cover: when the node no longer serves, back into cover (crouched lean or standing lean) or leaving it;
// between bursts back into cover when the opponent aims at it, or one time in three when it faces it.
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        if (!dcv->drone->firing)
            DroneWeap_Fire(1, dcv);
        SetAtCornerCover(dcv);
        return true;
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_Update:
        if (!BotCoverStillOK(dcv, 0)) {
            uchar mode = dcv->drone->coverMode;
            if (mode == COVER_MODE_CROUCH_LEAN || mode == COVER_MODE_STAND_LEAN)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverReturn, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverLeaveNow, 0);
        }
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        if (!DroneWeap_OpponentIsAimingAtMe(dcv, kCoverAimedAt)) {
            if (!NDrone2_BondIsFacingMe(dcv, 45.0f))
                return true;
            if (Rand_Rand(3) != 0)
                return true;
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverReturn, 0);
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        return BotCoverImpact(dcv, msg, 0);
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        return BotCoverImpact(dcv, msg, 1);
    case DRONE_MSG_ExplosiveSeen:
        return BotCoverExplosive(dcv, msg);
    default:
        return false;
    }
}

// Back into cover (BotCoverTypeChange after it; leaving when there is no anim for it); the cover is marked lost
// (DRONE_COVER_LOST) once the node no longer serves or the drone has drifted 1.5 m.
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverReturn(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        if (!DroneAnim_CoverAnim(dcv, COVER_STEP_RETURN, DSTATE_BotCoverTypeChange))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverLeave, 0);
        SetAtCornerCover(dcv);
        return true;
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_Update:
        if (!BotCoverStillOK(dcv, 0) || Vec_Dist2D(&dcv->gameObj->position, &dcv->drone->aiPoint.pos) > kCoverDrift)
            dcv->drone->coverFlags |= DRONE_COVER_LOST;
        return true;
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        return BotCoverImpact(dcv, msg, 0);
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        return BotCoverImpact(dcv, msg, 1);
    case DRONE_MSG_ExplosiveSeen:
        return BotCoverExplosive(dcv, msg);
    default:
        return false;
    }
}

// Back in cover: leaving it once lost; running to other cover when the node no longer serves from its inner angles
// and there is some; otherwise maybe another way of using the corner (back to BotCoverIdle).
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverTypeChange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case DRONE_MSG_Update:
        return true;
    case DRONE_MSG_Enter: {
        ushort coverFlags = dcv->drone->coverFlags;
        if (coverFlags & DRONE_COVER_LOST)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverLeaveNow, 0);
        if (!NDrone2_CoverNodeOK(dcv, dcv->drone->coverNode, coverFlags & DRONE_COVER_SIDE_MASK, 1) &&
            BotCoverFromOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverRunTo, 0);
        if (!DroneAnim_CoverAnim(dcv, COVER_STEP_CHANGE, DSTATE_BotCoverIdle))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverIdle, 0);
        SetAtCornerCover(dcv);
        return true;
    }
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    default:
        return false;
    }
}

// Leaving cover (BotCoverLeaveNow after the anim, or at once without one, or once it has left).
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverLeave(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        if (!DroneAnim_CoverAnim(dcv, COVER_STEP_LEAVE, DSTATE_BotCoverLeaveNow))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverLeaveNow, 0);
        SetAtCornerCover(dcv);
        return true;
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_Update:
        if (dcv->drone->coverFlags & DRONE_COVER_LEFT)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotCoverLeaveNow, 0);
        return true;
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        return BotCoverImpact(dcv, msg, 0);
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        return BotCoverImpact(dcv, msg, 1);
    case DRONE_MSG_ExplosiveSeen:
        return BotCoverExplosive(dcv, msg);
    default:
        return false;
    }
}

// Out of cover: the node let go, aiming, into the attack; turning to the opponent meanwhile.
// AUTOINJECT
bool NDrone2_DSTATE_BotCoverLeaveNow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        dcv->drone->coverFlags |= DRONE_COVER_LEFT;
        NDrone2_DisownCover(dcv);
        dcv->drone->atCornerCover = 0;
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, DSTATE_BotAttack, 0, 0.0f, 0, dcv);
        return true;
    case DRONE_MSG_Update:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        return true;
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        return BotCoverImpact(dcv, msg, 0);
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        return BotCoverImpact(dcv, msg, 1);
    case DRONE_MSG_ExplosiveSeen:
        return BotCoverExplosive(dcv, msg);
    default:
        return false;
    }
}

// ---- going places

// BOT_goal_t.kind
#define BOT_GOAL_PICKUP 1
#define BOT_GOAL_OBJECT 3

// Running to where an alert came from (NDrone2_MoveToAlertPosition), for up to 5 s; seeing the opponent, the attack;
// running past the point once the wait is over and it is still 8 m beyond reach.
// AUTOINJECT
bool NDrone2_DSTATE_BotAlertToPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DroneAnim_CanDoAnimState(dcv, DANIM_MOVE_8, 0) ? DANIM_MOVE_8 : DANIM_RUN, 0, 0, 0, 0.0f, 0,
                           dcv);
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT * 5);
        return true;
    case DRONE_MSG_Update: {
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        uint route = NDrone2_MoveToAlertPosition(dcv);
        if (!BOTSTATE_validateRoute(dcv, route))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoRoute, 0);
        switch (route) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackRun, 0);
        case AIROUTE_ARRIVED:
            NDrone2_InvalidateAttackRoute(dcv);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        case AIROUTE_DOOR:
            dcv->drone->nodeReturnState = DSTATE_BotAttack;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDoorOpen, 0);
        default:
            return true;
        }
    }
    case DRONE_MSG_TimerA:
        if (!((double)dcv->drone->aiPoint.radius + kAlertStopRange > dcv->drone->aiPoint.distance))
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
        return true;
    case DRONE_MSG_1B:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
    default:
        return false;
    }
}

// Going to the active goal (aiPoint), keeping the route's status in it: a pickup the route fails for is marked visited,
// and idle. Without a goal, back to the state it was saved from (goalReturnState, then cleared), or idle.
// AUTOINJECT
bool NDrone2_DSTATE_BotGotoGoalPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
        NDrone2_DistanceToAIPoint(dcv, &dcv->drone->aiPoint);
        return true;
    case DRONE_MSG_Update: {
        BOT_goal_t *goal = BOTSTATE_getActiveGoal(drone);
        BOT_vars_t *bot = dcv->drone->botVars;
        if (goal == NULL || goal->kind == 0) {
            if (bot->goalReturnState != 0) {
                Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)bot->goalReturnState, 0);
                bot->goalReturnState = 0;
                return true;
            }
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
        }
        Drone_tag *self = dcv->drone;
        uint route = NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&self->aiPoint.pos, self->aiPoint.radius, 0);
        goal->lastRouteStatus = (uchar)route;
        if (!BOTSTATE_validateRoute(dcv, route)) {
            if (goal->kind == BOT_GOAL_PICKUP)
                BOTSTATE_setPickupVisitTime(drone, goal->target);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
        }
        switch (route) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_PARTIAL:
        case AIROUTE_DIRECT:
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        case AIROUTE_DOOR:
            dcv->drone->nodeReturnState = DSTATE_BotGotoGoalPosition;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDoorOpen, 0);
        default:
            return true;
        }
    }
    default:
        return false;
    }
}

// Opening the door on its route and waiting for it, then back to the state it left (nodeReturnState). With a door
// property (0x15, 0x16, or alerted 0x14) the door anim, the door opened by its event; otherwise a stand anim and the
// door opened now. Every two seconds (timer A) it tries the door again. (As the original: 0x15 and 0x16 count without
// the alertness.)
// AUTOINJECT
bool NDrone2_DSTATE_BotDoorOpen(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        if (NDrone2_DoorIsOpen(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        Drone_tag *self = dcv->drone;
        bool alert = self->baseAlertness1 >= kDoorAlertness || self->alertness >= kDoorAlertness;
        if ((alert && behaviour_util_getProperty(DRONE_PROP_14, self->currentBehaviour) != 0) ||
            behaviour_util_getProperty(DRONE_PROP_15, dcv->drone->currentBehaviour) != 0 ||
            behaviour_util_getProperty(DRONE_PROP_16, dcv->drone->currentBehaviour) != 0) {
            DroneAnim_CallAnim(DANIM_USE_DOOR, 0, 0, 0, 0.0f, 0, dcv);
        } else {
            if (dcv->drone->alertness < 1.0f) {
                DroneAnim_CallAnim(DANIM_DOOR_WAIT, 1, 0, 0, 0.0f, 0, dcv);
            } else {
                DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
                dcv->drone->unknown38 = 1;
            }
            NDrone2_OpenDoor(dcv);
        }
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        return true;
    }
    case DRONE_MSG_Exit:
        dcv->drone->unknown38 = 0;
        return true;
    case DRONE_MSG_Update:
        if (NDrone2_DoorIsOpen(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        dcv->drone->unknown38 = 1;
        return true;
    case DRONE_MSG_TimerA:
        NDrone2_OpenDoor(dcv);
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        return true;
    case DRONE_MSG_AnimEvent:
        NDrone2_OpenDoor(dcv);
        return true;
    default:
        return false;
    }
}

// ---- guarding a friend (the Guardian's)

// What the bot knows of its friend: the entry for it, or NULL when it has none or the friend is no player
static BOT_playerInfo_t *GuardFriendInfo(BOT_vars_t *bot, short index) {
    return index == -1 ? NULL : &bot->players[index];
}

// Standing guard by the friend; following it once it is present and over 4.5 m away; idle without one.
// AUTOINJECT
bool NDrone2_DSTATE_BotGuardFriendIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DANIM_GUARD, 0, 0, 0, 0.0f, 0, dcv);
        return true;
    case DRONE_MSG_Update: {
        BOT_vars_t *bot = dcv->drone->botVars;
        if (bot->guardFriend == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
        BOT_playerInfo_t *info = GuardFriendInfo(bot, MP_PlayerOrBotInd(bot->guardFriend));
        if (info == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
        if ((info->flags & BOT_PLAYER_PRESENT) && info->distanceSq > kGuardRangeSq)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotGuardFriendFollow, 0);
        return true;
    }
    default:
        return false;
    }
}

// Following the friend (a goal of kind 3 on it), and once there, while it is still over 4.5 m away, setting off after
// it again; back to guarding when the route fails or the friend is near.
// AUTOINJECT
bool NDrone2_DSTATE_BotGuardFriendFollow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        BOT_vars_t *bot = dcv->drone->botVars;
        BOTSTATE_initGoal(bot, 0, BOT_GOAL_OBJECT, NULL, bot->guardFriend, 0);
        BOTSTATE_gotoGoal(dcv, 0, DSTATE_BotGuardFriendIdle);
        if (!DroneAnim_CallAnim(DANIM_MOVE_8, 0, 0, 0, 0.0f, 0, dcv))
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
        NDrone2_DistanceToAIPoint(dcv, &dcv->drone->aiPoint);
        return true;
    }
    case DRONE_MSG_Update: {
        BOT_vars_t *bot = dcv->drone->botVars;
        obj_tag *guarded = bot->guardFriend;
        if (guarded == NULL || MP_PlayerOrBotInd(guarded) == -1)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
        BOT_goal_t *goal = BOTSTATE_getActiveGoal(drone);
        if (goal == NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
        uint route = NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&dcv->drone->aiPoint.pos,
                                                dcv->drone->aiPoint.radius, 0);
        goal->lastRouteStatus = (uchar)route;
        if (!BOTSTATE_validateRoute(dcv, route))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotGuardFriendIdle, 0);
        if (route <= AIROUTE_DIRECT) {
            if (!DroneAnim_CallAnim(DANIM_MOVE_8, 0, 0, 0, 0.0f, 0, dcv))
                DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return true;
        }
        if (route != AIROUTE_ARRIVED)
            return true;
        BOT_playerInfo_t *info = GuardFriendInfo(bot, Control_Plr2Ind(guarded));
        if (info == NULL || !(info->flags & BOT_PLAYER_PRESENT) || !(info->distanceSq > kGuardRangeSq))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotGuardFriendIdle, 0);
        NDrone2_InvalidateAttackRoute(dcv);
        BOT_vars_t *self = dcv->drone->botVars;
        BOTSTATE_initGoal(self, 0, BOT_GOAL_OBJECT, NULL, self->guardFriend, 0);
        BOTSTATE_gotoGoal(dcv, 0, DSTATE_BotGuardFriendIdle);
        NDrone2_DistanceToAIPoint(dcv, &dcv->drone->aiPoint);
        return true;
    }
    default:
        return false;
    }
}

// ---- idle

// Waiting for something to do: on, attacking an opponent it sees (choosing the weapon for it) or forgetting its
// opponent; each frame, chasing the opponent its personality prefers, or inside the protection or demolition object
// checking it still is (within 3 m of its box: at the objective), or else going for the game's objective.
// AUTOINJECT
bool NDrone2_DSTATE_BotIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter: {
        obj_tag *opponent = drone->opponent;
        if (opponent != NULL && (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)) {
            BOTSTATE_combatWeaponChangeChoice(dcv->drone, opponent, 1, 0, 1);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
        }
        NDrone2_SetOpponent(dcv->drone, NULL);
        SetTimerA(dcv, GameState.NumFramesUnpaused + 2);
        return true;
    }
    case DRONE_MSG_TimerA:
        DroneAnim_CallAnim(DANIM_STAND_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        return true;
    case DRONE_MSG_Update: {
        dcv->drone->botVars->preferredOpponent = BOTSTATE_getPreferredTraitOpponentObjIndex(dcv->drone);
        BOT_vars_t *bot = dcv->drone->botVars;
        if (bot->preferredOpponent != 0xff) {
            BOTSTATE_setGoalPickPrefs(bot, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0);
            if (BOTSTATE_pickGoal(dcv, 0))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotGotoGoalPosition, 0);
        }
        bot = dcv->drone->botVars;
        if (bot->insideObjective &&
            (MPSettings.GameMode == GM_PROTECTION || MPSettings.GameMode == GM_DEMOLITION)) {
            obj_tag *objective = MPSettings.GameMode == GM_PROTECTION ? MP_getProtectionObj_NotExt()
                                                                     : MP_getDemolitionObj_NotExt();
            if (objective == NULL || objective->objGraphics == NULL)
                return true;
            _VECTOR offset;
            Vec_Subtract(&dcv->gameObj->centrePoint, Mat_Position(objective->transformMatrix), &offset);
            celglist_tag *model = objective->objGraphics;
            dcv->drone->botVars->insideObjective = Intersect_SphereBox(&offset, kObjectiveRadius, &model->extentMin,
                                                                       &model->extentMax);
            if (dcv->drone->botVars->insideObjective)
                drone->botVars->atObjective = 1;
            return true;
        }
        BOTSTATE_setGoalPickPrefs(bot, 1, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0);
        if (BOTSTATE_pickGoal(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotGotoGoalPosition, 0);
        return true;
    }
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
