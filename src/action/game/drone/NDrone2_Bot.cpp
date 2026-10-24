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
#include "DroneVision_Alerts.h"         // NDrone2_ReactToOpponentSighted
#include "../obj/control.h"             // Control_Plr2Ind
#include "NDrone2_Damage.h"
#include "NDrone2_Opponent.h"
#include "../mp/multiplayer.h"          // MP_PlayerKilled, MP_ResetBotPickupTimes
#include "../../game.h"                 // GameState
#include "../../engine/Collide.h"       // Coll_AddHitToList
#include "../../math/math.h"            // Vec_Subtract, Atan2
#include "../../math/X87Ops.h"          // Extended
#include "../../util/Random.h"          // Rand_Rand
#include "../weapon_stats.h"            // weapon_data

#include <bit>
#include <math.h>
#include <stdint.h>
#include "BOTWEAP.h"

#pragma fp_contract(off)

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
static_assert(std::bit_cast<uint32_t>(kCoverSlide) == 0x3da3d70a, "not the original's 0.08");
constexpr float kAttackRouteRadius = 2.0f;
static_assert(std::bit_cast<uint32_t>(-0.04f) == 0xbd23d70a, "not the original's -0.04");
static_assert(std::bit_cast<uint32_t>(-0.1f) == 0xbdcccccd, "not the original's -0.1");

static void SetTimerA(DCVars_tag *dcv, uint dueFrame) {
    dcv->drone->timerA.dueFrame = dueFrame;
    dcv->drone->timerA.data = 0;
    dcv->drone->timerA.on = 1;
}

static void SetTimerB(DCVars_tag *dcv, uint dueFrame) {
    dcv->drone->timerB.dueFrame = dueFrame;
    dcv->drone->timerB.data = 0;
    dcv->drone->timerB.on = 1;
}

static BOT_vars_t *Bot(DCVars_tag *dcv) {
    return dcv->drone->botVars;
}

// ---- the brain

constexpr float kRegenHealth = 5.0f;        // a second, with BOT_TRAIT_REGENERATE
constexpr float kUnseenDistraction = -2.0f;
constexpr float kCloseCombat = 3.0f;        // a random move within this

// Whether the opponent is within 1.5 m and a quarter turn of the bot's facing
static bool BotCanStrike(Drone_tag *drone) {
    return drone->distanceToTarget <= kUnarmedRange && fabsf(drone->opponentBearing) < HALF_PI;
}

// The bot's state as a state class has it: alerts the bot is not attentive for (dying, initialising, attacking...)
// pass it by.
static bool BotHearsAlerts(uint stateClass) {
    switch (stateClass) {
    case BOTSTATE_CLASS_PERSONALITY:
    case BOTSTATE_CLASS_MOVE:
    case BOTSTATE_CLASS_ALERTED:
    case BOTSTATE_CLASS_IMPACT:
    case BOTSTATE_CLASS_DOOR:
    case BOTSTATE_CLASS_IDLE:
        return true;
    default:
        return stateClass > BOTSTATE_CLASS_DEATH;
    }
}

// Each frame, for the attack states: reloading or changing weapon with an empty clip; hand to hand (or a change to the
// fists) when the opponent is close and ahead; backing off; a random move within 3 m; then the weapon for the range
// (in a hill, a check of the side move instead). What the state change answered, or BOT_CHECKS_NONE when there was
// none.
#define BOT_CHECKS_NONE -1
static int BotAttackChecks(DCVars_tag *dcv, Drone_tag *drone, BOT_vars_t *bot) {
    uint state = dcv->aiStateMachine->curState;
    if (state != DSTATE_BotAttackReload && state != DSTATE_BotAttackChangeWeapon) {
        if (dcv->drone->clip <= 0) {
            if (BOTWEAP_hasAmmo(dcv->drone, bot->weapon))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackReload, 0);
            bot->nextWeapon = (uchar)BOTSTATE_changeWeapon(bot, 0, 1, 1);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);
        }
        if (state != DSTATE_BotAttackUnarmed && !BOTSTATE_opponentIsMissile(drone, (obj_tag *)-1) &&
            BotCanStrike(dcv->drone)) {
            if ((dcv->drone->botVars->stats.traitFlags & BOT_TRAIT_FISTS_UP_CLOSE) ||
                (bot->weapon != 1 && (Rand_Rand(200) == 199 || BOTWEAP_shouldChangeWeapon(bot->weapon)))) {
                if (bot->weapon != 1) {
                    bot->nextWeapon = 1;
                    return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);
                }
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackUnarmed, 0);
            }
            if (bot->weapon == 1)
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackUnarmed, 0);
        }
        if (dcv->aiStateMachine->curState != DSTATE_BotAttackUnarmed && bot->weapon == 1 &&
            dcv->drone->distanceToTarget > kUnarmedRange) {
            bot->nextWeapon = (uchar)BOTSTATE_changeWeapon(bot, 0, 1, 1);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);
        }
        if (!(drone->opponent != NULL && BOTSTATE_opponentIsMissile(drone, (obj_tag *)-1)) &&
            !(dcv->drone->botVars->stats.traitFlags & BOT_TRAIT_FISTS_UP_CLOSE) &&
            !(bot->weapon == 1 && BOTWEAP_shouldChangeWeapon(1))) {
            if (dcv->aiStateMachine->curState != DSTATE_BotAttackBackoff &&
                dcv->drone->distanceToTarget <= dcv->drone->combatRanges[0] && NDrone2_CanBackoff(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackBackoff, 0);
            if (dcv->drone->distanceToTarget < kCloseCombat) {
                switch (dcv->aiStateMachine->curState) {
                case DSTATE_BotAttackStrafeAimLeft:
                case DSTATE_BotAttackStrafeAimRight:
                case DSTATE_BotAttackRunChangePosition:
                case DSTATE_BotAttackBackoff:
                case DSTATE_BotAttackRollLeftCrouch:
                case DSTATE_BotAttackRollRightCrouch:
                case DSTATE_BotAttackStepAimLeft:
                case DSTATE_BotAttackStepAimRight:
                case DSTATE_BotAttackUnarmed:
                    break;
                default: {
                    short move = BOTSTATE_chooseDodgeMove(dcv, -1);
                    if (move == 0) {
                        if (!BotCanStrike(dcv->drone))
                            break;
                        move = DSTATE_BotAttackUnarmed;
                    }
                    return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
                }
                }
            }
        }
    }

    Drone_tag *self = dcv->drone;
    if (self->opponent == NULL)
        return BOT_CHECKS_NONE;
    if (MPGame.players[self->botVars->playerIndex].flags & MPPLAYER_IN_HILL) {
        int side;
        switch (dcv->aiStateMachine->curState) {
        case DSTATE_BotAttackStrafeAimLeft:
        case DSTATE_BotAttackRollLeftCrouch:
        case DSTATE_BotAttackStepAimLeft:
            side = 2;
            break;
        case DSTATE_BotAttackStrafeAimRight:
        case DSTATE_BotAttackRollRightCrouch:
        case DSTATE_BotAttackStepAimRight:
            side = 1;
            break;
        default:
            return BOT_CHECKS_NONE;
        }
        if (BOTSTATE_checkAttackMove(dcv, (short)dcv->aiStateMachine->curState) != 0)
            return BOT_CHECKS_NONE;
        short move = BOTSTATE_chooseDodgeMove(dcv, side);
        if (move == 0)
            move = DSTATE_BotAttack;
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
    }
    state = dcv->aiStateMachine->curState;
    if (state == DSTATE_BotAttackReload || state == DSTATE_BotAttackChangeWeapon)
        return BOT_CHECKS_NONE;
    uchar weapon = bot->weapon;
    if (weapon_data[weapon].botWeaponClass != 4) {
        short explosive = BOTWEAP_getExplosiveWeapon(self, self->distanceToTarget);
        if (explosive == 0)
            return BOT_CHECKS_NONE;
        bot->nextWeapon = (uchar)explosive;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);
    }
    if (!BOTWEAP_tooCloseForWeapon(self, weapon, self->distanceToTarget))
        return BOT_CHECKS_NONE;
    bot->nextWeapon = (uchar)BOTSTATE_changeWeapon(bot, 0, 1, 1);
    return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);
}

// Whether the protection / demolition objective has the bot inside (within 3 m of its box), for a bot of the side that
// holds it: then it idles there
static bool BotAtObjective(DCVars_tag *dcv) {
    ushort team;
    obj_tag *objective;
    if (MPSettings.GameMode == GM_PROTECTION) {
        team = MI6;
        objective = MP_getProtectionObj_NotExt();
    } else if (MPSettings.GameMode == GM_DEMOLITION) {
        team = PHOENIX;
        objective = MP_getDemolitionObj_NotExt();
    } else {
        return false;
    }
    if (objective == NULL || team == NO_TEAM ||
        (uint)MPSettings.Player[dcv->drone->botVars->playerIndex].TeamId != team || objective->objGraphics == NULL)
        return false;
    _VECTOR offset;
    Vec_Subtract(&dcv->gameObj->centrePoint, Mat_Position(objective->transformMatrix), &offset);
    celglist_tag *model = objective->objGraphics;
    dcv->drone->botVars->insideObjective = Intersect_SphereBox(&offset, kObjectiveRadius, &model->extentMin,
                                                               &model->extentMax);
    return dcv->drone->botVars->insideObjective != 0;
}

// Each frame: dying (to BotDeathAnim), the state saved before a goal, recovering, weapon 0x45's regeneration (skin
// 0x1a), seeing the opponent at once (BOT_TRAIT_ATTACK_ON_SIGHT), regenerating health, distraction while the opponent
// is out of sight, and by state class: the objective's box, the goals, the attack checks, or (with the opponent seen
// in the last 5 s) the reaction to it.
static bool BotGlobalUpdate(DCVars_tag *dcv, Drone_tag *drone) {
    StateMachineInfo_tag *sm = dcv->aiStateMachine;
    if (sm->curState == 0)
        return Drone_SM_SetState(sm, DSTATE_BotIdle, 0);
    BOT_vars_t *bot = dcv->drone->botVars;
    uint stateClass = BOTSTATE_getStateType(sm->curState);
    dcv->drone->botVars->stateClass = (uchar)stateClass;
    obj_tag *gameObj = drone->gameObj;
    if (MPDrone_IsDyingOrDead(gameObj) || gameObj->objectType == OBJECTTYPE_DEAD_DRONE) {
        MPGame.players[dcv->drone->botVars->playerIndex].flags = 0;
        if (stateClass == BOTSTATE_CLASS_DEATH)
            return false;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDeathAnim, 0);
    }
    if (bot->goalReturnState != 0) {
        if (dcv->aiStateMachine->curState != bot->goalReturnState) {
            Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)bot->goalReturnState, 0);
            bot->goalReturnState = 0;
            return true;
        }
        bot->goalReturnState = 0;
    }
    dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
    obj_tag *opponent = dcv->drone->opponent;
    if (opponent != NULL && opponent->objectType == OBJECTTYPE_GFX)
        NDrone2_SetOpponent(dcv->drone, NULL);

    // Recovering from a hit: blind to everyone until it is over
    BOT_vars_t *own = drone->botVars;
    if (own->flags & BOT_FLAG_RECOVERING) {
        if (GameState.NumFramesUnpaused > own->recoveryEndFrame) {
            own->flags &= ~BOT_FLAG_RECOVERING;
        } else if (drone->opponent != NULL) {
            dcv->drone->flags &= ~DRONE_FLAG_LOOKS_FOR_OPPONENT;
            dcv->drone->sightFlags &= ~DRONE_SIGHT_SEES_OPPONENT;
            for (int i = 0; i < NUM_AGENTS; i++)
                bot->players[i].flags &= ~(BOT_PLAYER_PRESENT | BOT_PLAYER_IN_SIGHT);
        }
    }

    if (bot->skin == BOT_ODDJOB && bot->ammoRegenTimer != 0) {
        int elapsed = GameState.NumFramesUnpaused - bot->ammoRegenTimer;
        if (elapsed < 0)
            elapsed = bot->ammoRegenTimer - GameState.NumFramesUnpaused;
        if ((double)_FRAME_RATE * 10.0f <= elapsed) {
            if (bot->weapons[Weap_OddjobHat].rounds < weapon_data[Weap_OddjobHat].clipSize)
                bot->weapons[Weap_OddjobHat].rounds++;
            bot->ammoRegenTimer = 0;
            bot->nextWeapon = (uchar)BOTSTATE_changeWeapon(bot, 0, 1, 1);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);
        }
    }

    Drone_tag *self = dcv->drone;
    if (!(self->sightFlags & DRONE_SIGHT_SEES_OPPONENT) &&
        (self->botVars->stats.traitFlags & BOT_TRAIT_ATTACK_ON_SIGHT) && self->opponent != NULL) {
        short index = Control_Plr2Ind(self->opponent);
        if (index != -1) {
            uchar flags = bot->players[index].flags;
            if ((flags & BOT_PLAYER_PRESENT) && (flags & BOT_PLAYER_IN_SIGHT))
                dcv->drone->sightFlags |= DRONE_SIGHT_SEES_OPPONENT;
        }
    }

    if (dcv->drone->botVars->stats.traitFlags & BOT_TRAIT_REGENERATE) {
        uint due = bot->regenTimer;
        uint now = GameState.NumFramesUnpaused;
        if (due <= now || ((int)due < 0 && now < 0x100000)) {
            dcv->drone->health = dcv->drone->health + kRegenHealth;
            double maxHealth = (int)bot->stats.health;
            if (maxHealth < dcv->drone->health)
                dcv->drone->health = (float)maxHealth;
            BOT_SetHealth(drone, drone->health);
            bot->regenTimer = GameState.NumFramesUnpaused + FRAME_RATE_INT;
        }
    }

    short index = Control_Plr2Ind(dcv->drone->opponent);
    self = dcv->drone;
    bool seen;
    if (index >= 0) {
        uchar flags = self->botVars->players[index].flags;
        seen = (flags & BOT_PLAYER_PRESENT) && (flags & BOT_PLAYER_IN_SIGHT);
    } else {
        seen = (self->sightFlags & DRONE_SIGHT_SEES_OPPONENT) != 0;
    }
    if (!seen)
        BOTSTATE_increaseDistraction(self, kUnseenDistraction);

    if (stateClass == BOTSTATE_CLASS_MOVE && BotAtObjective(dcv))
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);

    switch (stateClass) {
    case BOTSTATE_CLASS_NONE:
        return true;
    case BOTSTATE_CLASS_INIT:
    case BOTSTATE_CLASS_GLOBAL:
        BOTSTATE_getStateType(dcv->aiStateMachine->curState);
        return true;
    case BOTSTATE_CLASS_DEATH:
        MPGame.players[dcv->drone->botVars->playerIndex].flags = 0;
        BOTSTATE_getStateType(dcv->aiStateMachine->curState);
        return true;
    default:
        break;
    }

    bot = dcv->drone->botVars;
    BOTSTATE_setNearestNavNode(dcv);
    uint before = dcv->aiStateMachine->curState;
    BOTSTATE_processGoals(dcv);
    if (dcv->aiStateMachine->curState == before) {
        if (stateClass == BOTSTATE_CLASS_ATTACK) {
            int changed = BotAttackChecks(dcv, drone, bot);
            if (changed != BOT_CHECKS_NONE)
                return changed != 0;
        } else {
            if (dcv->drone->opponent != NULL) {
                uint unseen = GameState.NumFramesUnpaused - drone->opponentSeenFrame;
                if ((double)_FRAME_RATE * 5.0f > (double)unseen)
                    dcv->drone->sightFlags |= DRONE_SIGHT_SEES_OPPONENT;
            }
            if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) {
                short state = NDrone2_ReactToOpponentSighted(dcv, DSTATE_BotSeenOpponent);
                return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)state, 0);
            }
        }
    }
    BOTSTATE_getStateType(dcv->aiStateMachine->curState);
    return true;
}

// A new state was entered (its class kept, cover let go outside the cover states, the friend forgotten outside the
// guard states and BotGotoGoalPosition): alert and status 1 in most, idle and status 3 dying or starting.
static bool BotStateChanged(DCVars_tag *dcv) {
    StateMachineInfo_tag *sm = dcv->aiStateMachine;
    if (sm->curState == 0)
        return Drone_SM_SetState(sm, DSTATE_BotIdle, 0);
    uint stateClass = BOTSTATE_getStateType(sm->curState);
    if (stateClass != BOTSTATE_CLASS_COVER)
        NDrone2_DisownCover(dcv);
    if (stateClass != BOTSTATE_CLASS_GUARD && dcv->aiStateMachine->curState != DSTATE_BotGotoGoalPosition)
        dcv->drone->botVars->guardFriend = NULL;
    uint alert = 1;
    char status = 1;
    switch (stateClass) {
    case BOTSTATE_CLASS_NONE:
        return true;
    case BOTSTATE_CLASS_DEATH:
        MPGame.players[dcv->drone->botVars->playerIndex].flags = 0;
        // fall through
    case BOTSTATE_CLASS_INIT:
        alert = 0;
        status = 3;
        break;
    }
    dcv->drone->alertness = (float)alert;
    Drone_AlertStatusSet(status, dcv);
    dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
    dcv->drone->botVars->stateClass = (uchar)stateClass;
    return true;
}

// The bots' global state: every message of a bot passes through it before its state's. Each frame, BotGlobalUpdate;
// impacts (all but in death) taken, with the bot recovering; the drones' alerts for the classes that hear them; the stun
// grenade; and the bot messages (goals, objectives, weapons, a player's death).
// AUTOINJECT
bool NDrone2_DSTATE_BotGlobal(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_BotStateChanged:
        return BotStateChanged(dcv);

    case DRONE_MSG_Update:
        return BotGlobalUpdate(dcv, drone);

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
        if (BOTSTATE_getStateType(dcv->aiStateMachine->curState) == BOTSTATE_CLASS_DEATH)
            return true;
        if (msg->msgType == DRONE_MSG_ImpactPunch)
            NDrone2_PunchImpact(dcv, (HITDATA_tag *)msg->extraData, 0, 0);
        else if (msg->msgType == DRONE_MSG_ImpactBullet)
            NDrone2_BulletImpact(dcv, (HITDATA_tag *)msg->extraData, 0, 0);
        else
            NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, 0, 0);
        drone->botVars->lastImpactFrame = GameState.NumFramesUnpaused;
        BOTSTATE_startRecovery(dcv);
        return true;

    case DRONE_MSG_AlertSighting:
    case DRONE_MSG_AlertDroneShot:
    case DRONE_MSG_AlertSurrender:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case DRONE_MSG_AlertAttack:
    case DRONE_MSG_GlobalAlarm:
        if (BotHearsAlerts(BOTSTATE_getStateType(dcv->aiStateMachine->curState)))
            NDrone2_HandleDroneAlert(dcv, msg, 0);
        return true;

    case DRONE_MSG_ImpactStunGrenade:
        if (BOTSTATE_getStateType(dcv->aiStateMachine->curState) == BOTSTATE_CLASS_DEATH)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactStunGrenade, 0);

    case DRONE_MSG_BotFlagTaken: case DRONE_MSG_BotFlagDropped: case DRONE_MSG_BotFlagCaptured:
    case DRONE_MSG_BotBlueprintTaken: case DRONE_MSG_BotBlueprintDropped: case DRONE_MSG_BotBlueprintDelivered:
    case DRONE_MSG_BotKeyTaken: case DRONE_MSG_BotCrystalTaken: case DRONE_MSG_BotKeyDropped:
    case DRONE_MSG_BotCrystalDropped: case DRONE_MSG_BotEnteredHill: case DRONE_MSG_BotUplinkTaken:
    case DRONE_MSG_BotUplinkTakenOver:
        return true;

    case DRONE_MSG_BotLeftHill:
        if (BOTSTATE_getStateType(dcv->aiStateMachine->curState) == BOTSTATE_CLASS_ATTACK)
            return true;
        if (dcv->drone->opponent != NULL)
            return true;
        BOTSTATE_setGoalPickPrefs(dcv->drone->botVars, 1, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0);
        if (BOTSTATE_pickGoal(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotGotoGoalPosition, 0);
        return true;

    case DRONE_MSG_BotGoalComplete:
        BOTSTATE_setGoalComplete(dcv, (uint)(uintptr_t)msg->extraData);
        return true;

    case DRONE_MSG_BotGoalUninit:
        BOTSTATE_uninitGoal(dcv->drone->botVars, (uint)(uintptr_t)msg->extraData);
        return true;

    case DRONE_MSG_BotCancelGoals:
        BOTSTATE_removeGoalsForObject(dcv, (obj_tag *)msg->extraData, DSTATE_BotIdle);
        return true;

    case DRONE_MSG_BotObjectiveGone:
    case DRONE_MSG_BotObjectiveDestroyed:
        if (dcv->drone->opponent == msg->extraData) {
            obj_tag *objective;
            if (MPSettings.GameMode == GM_PROTECTION)
                objective = MP_getProtectionObj_NotExt();
            else if (MPSettings.GameMode == GM_DEMOLITION)
                objective = MP_getDemolitionObj_NotExt();
            else
                return true;
            if (objective != (obj_tag *)-1)
                NDrone2_SetOpponent(dcv->drone, objective);
        }
        return true;

    case DRONE_MSG_BotChangeWeapon:
        dcv->drone->botVars->nextWeapon = (uchar)(uintptr_t)msg->extraData;
        dcv->drone->initialState = (short)dcv->aiStateMachine->curState;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackChangeWeapon, 0);

    case DRONE_MSG_BotPlayerDied: {
        obj_tag *self = dcv->gameObj;
        if (MPDrone_IsDyingOrDead(self) || self->objectType == OBJECTTYPE_DEAD_DRONE)
            return true;
        obj_tag *dead = (obj_tag *)msg->extraData;
        BOT_vars_t *bot = dcv->drone->botVars;
        uchar index = (uchar)Control_Plr2Ind(dead);
        if (dcv->drone->opponent == dead)
            NDrone2_SetOpponent(dcv->drone, NULL);
        if (bot->guardFriend == dead)
            bot->guardFriend = NULL;
        BOTSTATE_removeGoalsForObject(dcv, dead, DSTATE_BotIdle);
        if (BOTSTATE_getStateType(dcv->aiStateMachine->curState) == BOTSTATE_CLASS_GUARD)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotIdle, 0);
        if (index == bot->preferredOpponent)
            bot->preferredOpponent = 0xff;
        return true;
    }

    case DRONE_MSG_BotWeaponChanged: {
        obj_tag *picker = (obj_tag *)(uintptr_t)msg->sender;    // the sender is an object here
        obj_tag *self = dcv->gameObj;
        if (picker == self)
            return true;
        if (MPDrone_IsDyingOrDead(self) || self->objectType == OBJECTTYPE_DEAD_DRONE)
            return true;
        if (dcv->drone->opponent == picker)
            BOTSTATE_combatWeaponChangeChoice(dcv->drone, picker, 1, 0, 1);
        return true;
    }

    case DRONE_MSG_BotNewOpponent:
        BOTSTATE_combatWeaponChangeChoice(drone, (obj_tag *)msg->extraData, 1, 0, 0);
        BOTSTATE_defaultCombatRange(drone);
        if (drone->opponent == NULL)
            return true;
        if (BOTSTATE_opponentIsMissile(drone, (obj_tag *)-1)) {
            BOT_goal_t *goal = BOTSTATE_getActiveGoal(drone);
            if (goal != NULL)
                goal->timeBudget = 0.0f;
            if (drone->botVars->weapon == Weap_Satchel)
                BOTSTATE_reallyCloseCombatRange(drone);
            return true;
        }
        if (drone->opponent == NULL || dcv->aiStateMachine->curState != DSTATE_BotIdle ||
            !dcv->drone->botVars->insideObjective)
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);

    default:
        return false;
    }
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
    if (behaviour_util_getProperty(DRONE_PROP_DEAD_BODY_ALERTS, dcv->drone->currentBehaviour) == 0)
        return;
    if (!dcv->drone->headShot)
        NDrone2_DroneAlertToObject(dcv, DRONE_MSG_DroneAlert, dcv->gameObj, 0.0f, 0.0f, 1.0f);
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
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactBullet, (intptr_t)msg->extraData);
    case DRONE_MSG_ImpactExplosive:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactExplosive, (intptr_t)msg->extraData);
    case DRONE_MSG_TimerB: {
        BOTSTATE_stopAnim(dcv->drone);
        short next = BOTSTATE_chooseDodgeMove(dcv, -1);
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
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactPunch, (intptr_t)msg->extraData);
    case DRONE_MSG_ImpactBullet:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactBullet, (intptr_t)msg->extraData);
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
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactPunch, (intptr_t)msg->extraData);
    case DRONE_MSG_ImpactBullet:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactBullet, (intptr_t)msg->extraData);
    case DRONE_MSG_ImpactExplosive:
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotImpactExplosive, (intptr_t)msg->extraData);
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
        DroneAnim_CallAnim(DANIM_STUN_GRENADE_A, 0, 0, 0, 0.0f, 0, dcv);
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
            MP_sendTeamBotMessage(NO_TEAM, DRONE_MSG_BotWeaponChanged, (void *)(uintptr_t)Bot(dcv)->weapon,
                                  FRAME_RATE_INT, (uint)(uintptr_t)dcv->gameObj);
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
        ((dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && dcv->drone->opponentBearing < QUARTER_PI))
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
        anim = DANIM_WALK_AIMED;
        if (!DroneAnim_CanDoAnimState(dcv, DANIM_WALK_AIMED, 0))
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
        anim = DANIM_WALK_AIMED;
        fire = 1;
    }
    NDrone2_SetRunAnim(dcv, anim, fire);
    return anim;
}

// Turns the drone's heading (targetRot.y) towards moveTarget, plus offset; not while unknown3f0 has 0x8000000.
// FUN_00043cf0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
void NDrone2_FaceMoveTarget(DCVars_tag *dcv, float offset) {
    if (dcv->drone->unknown3f0 & 0x8000000)
        return;
    _VECTOR delta;
    Vec_Subtract(&dcv->drone->moveTarget, &dcv->gameObj->position, &delta);
    dcv->drone->targetRot.y = (float)(Extended(Atan2(delta.x, delta.z)) + Extended(offset));
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
        if (!(dcv->drone->flags & DRONE_FLAG_STATIONARY) &&
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) == 0 &&
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
        DroneAnim_CallAnim(DroneAnim_CanDoAnimState(dcv, DANIM_WALK_AIMED, 0) ? DANIM_WALK_AIMED : DANIM_RUN, 0, 0, 0,
                           0.0f, 0, dcv);
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
        if ((dcv->drone->flags & DRONE_FLAG_STATIONARY) ||
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) != 0)
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
            if (fabsf(self->opponentBearing) < HALF_PI) {
                BOT_vars_t *bot = self->botVars;
                if ((bot->stats.traitFlags & BOT_TRAIT_FISTS_UP_CLOSE) || bot->weapon == 1 || Rand_Rand(8) == 7)
                    return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackUnarmed, 0);
            }
            short move = BOTSTATE_chooseDodgeMove(dcv, 3);
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
        if (!(dcv->drone->flags & DRONE_FLAG_STATIONARY) &&
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) == 0 &&
            dcv->drone->distanceToTarget <= dcv->drone->combatRanges[0] && NDrone2_CanBackoff(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackBackoff, 0);
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttackNoOpponent, 0);
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        short move = BOTSTATE_EvasiveMove(dcv);
        if (move != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
        if ((dcv->drone->flags & DRONE_FLAG_STATIONARY) ||
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) != 0) {
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
        if (!BOTWEAP_reload(dcv->gameObj, 0)) {
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

// How long to wait in cover before looking out: up to a share of 120 frames (NPCGlobals.coverWaitScale,
// coverWaitShare),
// and a second
static void BotCoverWait(DCVars_tag *dcv) {
    uint wait = Rand_Rand(Ftol((double)NPCGlobals.coverWaitScale * dcv->drone->coverWaitShare * kCoverWaitScale));
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
        dcv->drone->flags &= ~DRONE_FLAG_GOING_TO_COVER;
        dcv->drone->standingStill = 1;
        Drone_tag *self = dcv->drone;
        NDrone2_GetCoverPosForTarget(self->coverNode, self->targetSlot, &self->aiPoint.pos, NULL, 0);
        return true;
    }
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_Update:
        NDrone2_SlideToPosition(&dcv->drone->aiPoint.pos, kCoverSlide, 0, dcv);
        NDrone2_SetAngleToCover(dcv, PI);
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
        dcv->drone->standingStill = 0;
        dcv->drone->stateScratch = 0;
        return true;
    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;
    case DRONE_MSG_Update:
        NDrone2_SlideToPosition(&dcv->drone->aiPoint.pos, kCoverSlide, 0, dcv);
        NDrone2_SetAngleToCover(dcv, PI);
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

// Running to where an alert came from (NDrone2_MoveToAlertPosition), for up to 5 s; seeing the opponent, the attack;
// running past the point once the wait is over and it is still 8 m beyond reach.
// AUTOINJECT
bool NDrone2_DSTATE_BotAlertToPosition(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        DroneAnim_CallAnim(DroneAnim_CanDoAnimState(dcv, DANIM_WALK_AIMED, 0) ? DANIM_WALK_AIMED : DANIM_RUN, 0, 0, 0,
                           0.0f, 0, dcv);
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
                BOTSTATE_setPickupVisitTime(drone, (MP_PICKUP *)goal->target);
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
        bool alert =
            self->baseAlertness1 >= DRONE_ALERTNESS_SUSPICIOUS || self->alertness >= DRONE_ALERTNESS_SUSPICIOUS;
        if ((alert && behaviour_util_getProperty(DRONE_PROP_DOOR_ANIM_ALERT, self->currentBehaviour) != 0) ||
            behaviour_util_getProperty(DRONE_PROP_DOOR_ANIM, dcv->drone->currentBehaviour) != 0 ||
            behaviour_util_getProperty(DRONE_PROP_DOOR_ANIM_2, dcv->drone->currentBehaviour) != 0) {
            DroneAnim_CallAnim(DANIM_USE_DOOR, 0, 0, 0, 0.0f, 0, dcv);
        } else {
            if (dcv->drone->alertness < 1.0f) {
                DroneAnim_CallAnim(DANIM_IDLE_FIDGET, 1, 0, 0, 0.0f, 0, dcv);
            } else {
                DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
                dcv->drone->standingStill = 1;
            }
            NDrone2_OpenDoor(dcv);
        }
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        return true;
    }
    case DRONE_MSG_Exit:
        dcv->drone->standingStill = 0;
        return true;
    case DRONE_MSG_Update:
        if (NDrone2_DoorIsOpen(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        dcv->drone->standingStill = 1;
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
        DroneAnim_CallAnim(DANIM_LOOK_AROUND, 0, 0, 0, 0.0f, 0, dcv);
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
        if (!DroneAnim_CallAnim(DANIM_WALK_AIMED, 0, 0, 0, 0.0f, 0, dcv))
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
            if (!DroneAnim_CallAnim(DANIM_WALK_AIMED, 0, 0, 0, 0.0f, 0, dcv))
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
