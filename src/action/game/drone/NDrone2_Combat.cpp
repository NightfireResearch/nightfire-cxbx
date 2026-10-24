// Combat and aiming: a drone's first reaction to an opponent, the lines it shouts, the combat moves it can make
// (steps, strafes, dodges, rolls, backing off) and the one it chooses, going after the opponent, cover, and the combat
// and aiming states. See docs/drone/architecture/README.md 3.

#include "NDrone2_Combat.h"
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneInit.h"                  // DroneFunc_SetAsAttacking
#include "DroneModes.h"                 // NDrone2_ChangeToAttackMode
#include "DroneMove.h"
#include "DroneTables.h"
#include "DroneVision.h"                // NDrone2_CanSeeObject
#include "DroneWeapon.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Route.h"
#include "NDrone2_Opponent.h"
#include "../obj/Drone_AIHints.h"       // CoverNode_tag
#include "../obj/build.h"               // build_FindCel
#include "../mp/multiplayer.h"          // MPSettings
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_* (ours)
#include "../../math/X87Ops.h"          // FSin, FCos
#include "../../util/Random.h"          // Rand_Rand
#include "../../engine/psiDraw.h"       // psiStubFalse

#include <bit>
#include <math.h>
#include <stdint.h>

#pragma fp_contract(off)

// The originals, called by address: Ghidra's prototypes have untyped arguments
// Whether a point at offset from the drone's feet, in its object's frame, can be walked to (scaled by a bot's
// speed unless unscaled is set)

static_assert(offsetof(MPSettings_t, dronesAreBots) == 0x1e4, "MPSettings.dronesAreBots is 0x0026001c");

// Set by the BLIND_DRONES cheat (ui_cheats.cpp): drones ignore the player
extern uint32_t switch_BLIND_DRONES;

// Drone_tag.bunchFlags: the sides another drone is on (DroneMove_NoBunching)
#define BUNCH_AHEAD 2
#define BUNCH_BEHIND 4
#define BUNCH_LEFT 8
#define BUNCH_RIGHT 0x10

// CoverNode_tag.flags: held by a drone (set by NDrone2_TakeCover, cleared by NDrone2_DisownCover)
#define COVERNODE_HELD 0x10

// The anim states these ask for (our names)
#define DANIM_BACKOFF 4
#define DANIM_MOVE_8 8
#define DANIM_CROUCH_WALK 0xf
#define DANIM_AIM_CROUCH 5
#define DANIM_AIM_STAND 0xa
#define DANIM_STRAFE_LEFT 0xc
#define DANIM_STRAFE_RIGHT 0xd
#define DANIM_CROUCH_COVER 0x11
#define DANIM_PRONE 0x1d
#define DANIM_RUN 0x1e
#define DANIM_ALERT_STAND 0x21
#define DANIM_STRAFE_DODGE 0x29
#define DANIM_ALT_ATTACK 0x3d
#define DANIM_DISCARD 0x45
#define DANIM_DRAW_WEAPON 0x46
#define DANIM_RELOAD_B 0x4a
#define DANIM_RELOAD 0x52
#define DANIM_ROLL_LEFT 0x53
#define DANIM_ROLL_RIGHT 0x54
#define DANIM_STEP_LEFT 0x5a
#define DANIM_STEP_RIGHT 0x5b

// The original's constants
constexpr float kAimTurnTolerance = 0.5235988f;        // pi/6
static_assert(std::bit_cast<uint32_t>(kAimTurnTolerance) == 0x3f060a92, "not the original's pi/6");
static_assert(std::bit_cast<uint32_t>(1.8f) == 0x3fe66666, "not the original's 1.8");
static_assert(std::bit_cast<uint32_t>(3.6f) == 0x40666666, "not the original's 3.6");
static_assert(std::bit_cast<uint32_t>(5.2f) == 0x40a66666, "not the original's 5.2");
static_assert(std::bit_cast<uint32_t>(-0.1f) == 0xbdcccccd, "not the original's -0.1");
static_assert(std::bit_cast<uint32_t>(0.7f) == 0x3f333333, "not the original's 0.7");
static_assert(std::bit_cast<uint32_t>(-0.7f) == 0xbf333333, "not the original's -0.7");
static_assert(std::bit_cast<uint32_t>(-0.3f) == 0xbe99999a, "not the original's -0.3");
static_assert(std::bit_cast<uint32_t>(-1.1f) == 0xbf8ccccd, "not the original's -1.1");
static_assert(std::bit_cast<uint32_t>(HALF_PI) == 0x3fc90fdb, "not the original's pi/2");

// Starts timer A, to run out at the frame given
static void SetTimerA(DCVars_tag *dcv, uint dueFrame) {
    dcv->drone->timerA.dueFrame = dueFrame;
    dcv->drone->timerA.data = 0;
    dcv->drone->timerA.on = 1;
}

// The frame a second from now (at 60 a second), worked out on the x87
static uint SecondFromNow(void) {
    return Ftol((double)FRAME_RATE_DIV * 60.0f + (double)GameState.NumFramesUnpaused);
}

// The weapon in the drone's object's hand, without an anim state none. Not named on PS2; the name is ours.
// AUTOINJECT
void DroneFunc_SetWeaponInHand(DCVars_tag *dcv, char weapon) {
    if (dcv->gameObj == NULL || dcv->gameObj->animState == NULL)
        return;
    dcv->gameObj->animState->currentWeaponId = weapon;
}

// A captain with weapon 0x19 in hand changes to 0x18
static void CaptainWeapon(DCVars_tag *dcv, Drone_tag *drone) {
    if (drone->captain && DroneFunc_WeaponInHand(dcv) == 0x19)
        DroneFunc_SetWeaponInHand(dcv, 0x18);
}

// ---- the first attack

// The opponent was lost (or never had a route): the route the drone attacked by is dropped.
// AUTOINJECT
void NDrone2_InvalidateAttackRoute(DCVars_tag *dcv) {
    dcv->drone->activeRoute = NULL;
    AINetwork_InvalidateRoute(&dcv->drone->route1);
}

// The state for a drone first seeing its opponent, or 0 to attack: it surrenders when caught (NDrone2_CheckSurrender);
// with property 0x33 it tells the drones within 20 m, once; with 0x1a it interrogates or challenges in the levels that
// have one; then it shouts, and with 0x1b, 0x19 or 0x4a makes a combat move or throws a grenade.
short DroneFunc_FirstSightState(DCVars_tag *dcv) {
    if (NDrone2_CheckSurrender(dcv)) {
        dcv->drone->alertness = 1.0f;
        return DSTATE_Surrender_Anim;
    }
    if (behaviour_util_getProperty(DRONE_PROP_SIGHTING_ALERTS, dcv->drone->currentBehaviour) != 0 &&
        !(dcv->drone->flags & DRONE_FLAG_ALERTED_OTHERS)) {
        dcv->drone->knowledge |= DRONE_KNOWS_SIGHTING;
        NDrone2_DroneAlertToObject(dcv, 0x11, dcv->drone->opponent, 20.0f, 20.0f, 1.0f);
        dcv->drone->flags |= DRONE_FLAG_ALERTED_OTHERS;
    }
    if (behaviour_util_getProperty(DRONE_PROP_INTERROGATE_ON_SIGHT, dcv->drone->currentBehaviour) != 0) {
        switch (GameState.CurrentLevelHashcode) {
        case HT_Level_CastleIndoors1:
        case HT_Level_RefRoom:
            return DSTATE_Interogate;
        case HT_Level_TowerA:
        case HT_Level_TowerB:
            return DSTATE_CivilianChallenge;
        default:
            break;
        }
    }
    NDrone2_SeenPlayerTalk(dcv);
    if (behaviour_util_getProperty(DRONE_PROP_ATTACK_OR_MOVE_ON_SIGHT, dcv->drone->currentBehaviour) != 0) {
        switch (Rand_Rand(2)) {
        case 0:
            return 0;
        case 1:
            return NDrone2_ChooseCombatMove(dcv);
        }
    }
    if (behaviour_util_getProperty(DRONE_PROP_ATTACK_ON_SIGHT, dcv->drone->currentBehaviour) != 0)
        return 0;
    if (behaviour_util_getProperty(DRONE_PROP_COMBAT_MOVE_ON_SIGHT, dcv->drone->currentBehaviour) != 0)
        return NDrone2_ChooseCombatMove(dcv);
    return behaviour_util_getProperty(DRONE_PROP_GRENADE_ON_SIGHT, dcv->drone->currentBehaviour) != 0 ? DSTATE_GrenadeThrow : 0;
}

// The state for a drone alerted by another, or 0: one told of an interrogation goes to help with it, in the levels
// that have one.
short DroneFunc_FirstAlertState(DCVars_tag *dcv) {
    uint knowledge = dcv->drone->knowledge;
    if (knowledge & (DRONE_KNOWS_HURT | DRONE_KNOWS_BIT4 | DRONE_KNOWS_INTERROGATING |
                     (DRONE_KNOWS_HURT | DRONE_KNOWS_SURRENDERED | DRONE_KNOWS_DEAD | DRONE_KNOWS_BIT4) << 16))
        return 0;
    if (!(knowledge & (DRONE_KNOWS_INTERROGATING << 16)))
        return 0;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_CastleExterior:
    case HT_Level_CastleIndoors1:
    case HT_Level_RefRoom:
        return DSTATE_InterogateAssist;
    default:
        return 0;
    }
}

// What a drone with no gun in hand does: draws its second weapon, if it has one, else is scared. An armed civilian
// becomes an enemy, and attacks (0).
// AUTOINJECT
short DroneFunc_AttackNoWeapon(DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    if (gameObj != NULL && gameObj->animState != NULL && gameObj->animState->currentWeaponId > 1) {
        if (dcv->drone->side == 3)
            dcv->drone->side = 1;
        return 0;
    }
    return dcv->drone->secondWeapon > 1 ? DSTATE_DrawWeapon : DSTATE_CivilianScared;
}

// The start of an attack, once: the reaction to a first sighting or alert, then the drone is set attacking, the others
// told, a line shouted, the mission failed with property 0x52, and the change to its attack mode made. The state to
// go to, or 0 to attack (NDrone2_DSTATE_Attack decides).
// AUTOINJECT
short DroneFunc_FirstAttack(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->flags & DRONE_FLAG_ATTACK_STARTED)
        return 0;
    if (switch_BLIND_DRONES)
        return 0;

    if (drone->flags & DRONE_FLAG_ATTACK_SIGHT) {
        short state = DroneFunc_FirstSightState(dcv);
        if (state != 0) {
            DroneFunc_SetAsAttacking(drone);
            return state;
        }
    } else if (drone->flags & DRONE_FLAGS_ATTACK_ALERTED) {
        short state = DroneFunc_FirstAlertState(dcv);
        if (state != 0)
            return state;
    }

    DroneFunc_SetAsAttacking(drone);
    NDrone2_SendAttackMessage(dcv);
    NDrone2_AttackTalk(dcv);
    if (behaviour_util_getProperty(DRONE_PROP_ALERT_FAILS_MISSION, dcv->drone->currentBehaviour) != 0)
        DroneFunc_SetMissionFailReason(drone, 8);
    if (dcv->drone->secondState != 0 && !dcv->drone->modeChangeSwitchChannel) {
        dcv->drone->initialState = DSTATE_Attack;
        short state = NDrone2_ChangeToAttackMode(dcv->drone);
        return state == DSTATE_DeleteMe ? DSTATE_Attack : state;
    }
    return DroneFunc_AttackNoWeapon(dcv);
}

// ---- talk

// One of the sounds first..last.
// AUTOINJECT
uint DroneFunc_ChooseSFX(uint first, uint last) {
    return first + Rand_Rand(last - first + 1);
}

// One of the sounds first..last, or for a drone with an odd id the one as many further on. FUN_00036290 in Ghidra.
// Not named on PS2; the name is ours.
// AUTOINJECT
uint DroneFunc_ChooseSFXByID(Drone_tag *drone, uint first, uint last) {
    uint sfx = first + Rand_Rand(last - first + 1);
    if (drone->sm.id & 1)
        sfx += last - first + 1;
    return sfx;
}

// Whether the opponent is nearer than range.
// AUTOINJECT
bool DroneFunc_InRangeOfTarget(DCVars_tag *dcv, float range) {
    return dcv->drone->distanceToTarget < range;
}

// The weapon in the object's hand, 0 without an anim state. FUN_00035f70 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
char DroneFunc_WeaponInHand(DCVars_tag *dcv) {
    if (dcv->gameObj == NULL || dcv->gameObj->animState == NULL)
        return 0;
    return dcv->gameObj->animState->currentWeaponId;
}

constexpr float kSurrenderAlertness = 0.66f;
constexpr float kSurrenderRange = 2.0f;
constexpr float kSurrenderBearing = 2.0943952f;     // 2pi/3
constexpr float kSurrenderFacing = 45.0f;
static_assert(std::bit_cast<uint32_t>(kSurrenderAlertness) == 0x3f28f5c3, "not the original's 0.66");
static_assert(std::bit_cast<uint32_t>(kSurrenderBearing) == 0x40060a92, "not the original's 2pi/3");

// A drone that may surrender (property 0x43) does, fully alerted, when it is not yet alert, its opponent is within
// 2 m behind it (over 2pi/3 off its facing), in sight, armed and facing it.
// AUTOINJECT
bool NDrone2_CheckSurrender(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *opponent = drone->opponent;
    if (opponent == NULL)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_MAY_SURRENDER, drone->currentBehaviour) == 0)
        return false;
    drone = dcv->drone;
    if (drone->alertness >= kSurrenderAlertness)        // NaN goes on
        return false;
    if (!(drone->distanceToTarget < kSurrenderRange))
        return false;
    if (!(fabsf(drone->opponentBearing) > kSurrenderBearing))
        return false;
    if (!NDrone2_CanSeeObject(drone, opponent, 5, 0))
        return false;
    if (!DroneWeap_OpponentHasWeapon(dcv))
        return false;
    if (!NDrone2_BondIsFacingMe(dcv, kSurrenderFacing))
        return false;
    dcv->drone->alertness = 1.0f;
    return true;
}

// An enemy with an opponent, not yet alarmed, raises the alarm (DRONE_FLAG_ALERTED_OTHERS, DRONE_KNOWS_BIT4) and
// alerts the drones within 20 m to its opponent (message 0x16).
// AUTOINJECT
void NDrone2_SendAttackMessage(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->side != 1 || (drone->flags & DRONE_FLAG_ALERTED_OTHERS) || drone->opponent == NULL)
        return;
    drone->knowledge |= DRONE_KNOWS_BIT4;
    dcv->drone->flags |= DRONE_FLAG_ALERTED_OTHERS;
    NDrone2_DroneAlertToObject(dcv, 0x16, dcv->drone->opponent, 20.0f, 20.0f, 1.0f);
}

// Shouted when attacking, by skin class (and level), now and then; the opponent's last sighting is passed on to the
// others.
// AUTOINJECT
bool NDrone2_AttackTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    uint chance = 0;
    uint sfx;
    if (MPSettings.dronesAreBots != 0)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;

    switch ((ushort)drone->skinClass) {
    case SKINCLASS_YAKUZA:
        if (GameState.CurrentLevelHashcode == HT_Level_TowerC || GameState.CurrentLevelHashcode == HT_Level_Tower2A ||
            GameState.CurrentLevelHashcode == HT_Level_Tower2C) {
            sfx = DroneFunc_ChooseSFX(0xa3, 0xaa);
        } else {
            sfx = DroneFunc_ChooseSFX(0x33, 0x36);
            chance = 2;
        }
        break;
    case SKINCLASS_SNOW_GUARD:
        sfx = DroneFunc_ChooseSFX(0x37, 0x3f);
        chance = 3;
        break;
    case SKINCLASS_BLACK_OPS:
        if (dcv->drone->sightFlags & DRONE_SIGHT_REACTED)
            return false;
        sfx = DroneFunc_ChooseSFX(0xac, 0xb2);
        break;
    case SKINCLASS_SNIPER:
    case SKINCLASS_ELITE:
        if (GameState.CurrentLevelHashcode != HT_Level_PowerStationA1 &&
            GameState.CurrentLevelHashcode != HT_Level_PowerStationA2)
            return false;
        if (drone->unseenFrames <= FRAME_RATE_INT * 3 || drone->unseenFrames >= FRAME_RATE_INT * 4)
            return false;
        sfx = DroneFunc_ChooseSFX(0xd3, 0xd5);
        break;
    case SKINCLASS_ELITE_COMBAT:
        if (GameState.CurrentLevelHashcode != HT_Level_EvilBase && GameState.CurrentLevelHashcode != HT_Level_EvilBaseC &&
            GameState.CurrentLevelHashcode != HT_Level_EvilSilo)
            return false;
        sfx = DroneFunc_ChooseSFXByID(drone, 0x11b, 0x121);
        break;
    case SKINCLASS_CASTLE_GUARD:
        if (GameState.CurrentLevelHashcode != HT_Level_CastleIndoors1)
            return false;
        sfx = DroneFunc_ChooseSFX(0x93, 0x94);
        chance = 3;
        break;
    default:
        return false;
    }

    if (sfx == 0)
        return false;
    if (chance != 0 && Rand_Rand(chance) != 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->opponentSeenPos);
    return true;
}

// Said one time in three on seeing the player, by level (and skin class, and which of a pair by the drone's id).
// Skin class 0x10 says nothing: its own line, after it, is never reached.
// AUTOINJECT
bool NDrone2_SeenPlayerTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    uint sfx = 0;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;
    if (Rand_Rand(3) != 0)
        return false;
    if (drone->skinClass == SKINCLASS_ROOK)
        return false;

    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_CastleExterior:
    case HT_Level_CastleCourtyard:
    case HT_Level_CastleIndoors1:
    case HT_Level_CastleIndoors2:
        if (drone->skinClass == SKINCLASS_SNOW_GUARD)
            sfx = DroneFunc_ChooseSFX(0x49, 0x4e);
        else
            sfx = 0x21;
        break;
    case HT_Level_TowerA:
    case HT_Level_TowerB:
        sfx = DroneFunc_ChooseSFX(0xc3, 0xcb);
        break;
    case HT_Level_Tower2B:
    case HT_Level_Tower2Elevator:
        sfx = DroneFunc_ChooseSFX(0xac, 0xb2);
        break;
    case HT_Level_EvilBase:
        if (Rand_Rand(2) != 0)
            sfx = DroneFunc_ChooseSFXByID(drone, 0xf3, 0xf4);
        else
            sfx = DroneFunc_ChooseSFXByID(drone, 0x10d, 0x10e);
        break;
    case HT_Level_EvilSilo:
        if (Rand_Rand(2) != 0)
            sfx = DroneFunc_ChooseSFXByID(drone, 0xf7, 0xf8);
        else
            sfx = DroneFunc_ChooseSFXByID(drone, 0x10d, 0x10e);
        break;
    case HT_Level_EvilBaseC:
        if (Rand_Rand(2) != 0)
            sfx = DroneFunc_ChooseSFXByID(drone, 0xfb, 0xfc);
        else
            sfx = DroneFunc_ChooseSFXByID(drone, 0x10d, 0x10e);
        break;
    default:
        break;
    }
    if (drone->skinClass == SKINCLASS_ROOK) {
        if (drone->sfxFrame + FRAME_RATE_INT * 3 > GameState.NumFramesUnpaused)
            return false;
        sfx = DroneFunc_ChooseSFX(0xe9, 0xeb);
    }

    if (sfx == 0)
        return false;
    drone->speechChannel = NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return true;
}

// ---- what the drone can do

// It has an opponent, or saw one in the last 30 frames.
// AUTOINJECT
bool NDrone2_HasOpponent(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->opponent != NULL)
        return true;
    return drone->opponentSeenFrame != 0 && drone->opponentSeenFrame + 30 >= GameState.NumFramesUnpaused;
}

// Close combat (anim state 0x3d, variant 0-3, 0 for any other): within 1.5 m of the opponent, and only while the
// player's stats are kept (or in multiplayer).
// AUTOINJECT
bool NDrone2_CanAltAttack(DCVars_tag *dcv, short variant) {
    if (MPSettings.dronesAreBots == 0 && !PlrStat_OkToUpdate())
        return false;
    if (variant < 0 || variant > 3)
        variant = 0;
    bool can = DroneAnim_CanDoAnimState(dcv, DANIM_ALT_ATTACK, variant);
    if (dcv->drone->distanceToTarget > 1.5f)
        can = false;
    return can;
}

// The combat moves' tests: the behaviour property, the drone not stationary, and the anim state. FUN_00043510, 43560,
// 435b0 and 43600 in Ghidra. Not named on PS2; the names are ours.
// AUTOINJECT
bool NDrone2_StepAllowed(DCVars_tag *dcv) {
    return behaviour_util_getProperty(DRONE_PROP_SIDE_STEP, dcv->drone->currentBehaviour) != 0 &&
           !(dcv->drone->flags & DRONE_FLAG_STATIONARY) && DroneAnim_CanDoAnimState(dcv, DANIM_STEP_LEFT, 0);
}

// AUTOINJECT
bool NDrone2_StrafeAllowed(DCVars_tag *dcv) {
    return behaviour_util_getProperty(DRONE_PROP_STRAFE, dcv->drone->currentBehaviour) != 0 &&
           !(dcv->drone->flags & DRONE_FLAG_STATIONARY) && DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_LEFT, 0);
}

// AUTOINJECT
bool NDrone2_StrafeDodgeAllowed(DCVars_tag *dcv) {
    return behaviour_util_getProperty(DRONE_PROP_STRAFE_DODGE, dcv->drone->currentBehaviour) != 0 &&
           !(dcv->drone->flags & DRONE_FLAG_STATIONARY) && DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_DODGE, 0);
}

// AUTOINJECT
bool NDrone2_RollAllowed(DCVars_tag *dcv) {
    return behaviour_util_getProperty(DRONE_PROP_ROLL, dcv->drone->currentBehaviour) != 0 &&
           !(dcv->drone->flags & DRONE_FLAG_STATIONARY) && DroneAnim_CanDoAnimState(dcv, DANIM_ROLL_LEFT, 0);
}

// Walking crouched (property 0 or 2, anim state 0xf). FUN_000436b0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool NDrone2_CanCrouchWalk(DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_WALK_AIMED_ANY_WAY, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_CROUCH_WALK, dcv->drone->currentBehaviour) == 0)
        return false;
    return DroneAnim_CanDoAnimState(dcv, DANIM_CROUCH_WALK, 0);
}

// Anim state 8 (property 1 without 2). FUN_00043700 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool NDrone2_CanMove8(DCVars_tag *dcv) {
    return behaviour_util_getProperty(DRONE_PROP_WALK_AIMED, dcv->drone->currentBehaviour) != 0 &&
           behaviour_util_getProperty(DRONE_PROP_CROUCH_WALK, dcv->drone->currentBehaviour) == 0 &&
           DroneAnim_CanDoAnimState(dcv, DANIM_MOVE_8, 0);
}

// AimStand three times in four, else 0. FUN_000437b0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
short NDrone2_ChooseAimStand(void) {
    return Rand_Rand(4) != 0 ? DSTATE_AimStand : 0;
}

// Crouching to aim (property 0x11, anim state 5), 6 m or more from the opponent, half the time.
// AUTOINJECT
bool NDrone2_CanAimCrouch(DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_AIM_CROUCH, dcv->drone->currentBehaviour) == 0)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_AIM_CROUCH, 0))
        return false;
    if (dcv->drone->distanceToTarget < 6.0f)
        return false;
    return Rand_Rand(2) == 0;
}

// AimCrouch when NDrone2_CanAimCrouch agrees, else 0. FUN_000437d0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
short NDrone2_ChooseAimCrouch(DCVars_tag *dcv) {
    return NDrone2_CanAimCrouch(dcv) ? DSTATE_AimCrouch : 0;
}

// A step to the side, 1.8 m, aiming (property 0x42, anim state 0x5a; 0x5b too to the right).
// AUTOINJECT
bool NDrone2_CanStepLeft(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_LEFT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_SIDE_STEP, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STEP_LEFT, 0))
        return false;
    _VECTOR offset = {1.8f, 0.0f, 0.0f};
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// AUTOINJECT
bool NDrone2_CanStepRight(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_RIGHT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_SIDE_STEP, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STEP_LEFT, 0))
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STEP_RIGHT, 0))
        return false;
    _VECTOR offset = {-1.8f, 0.0f, 0.0f};
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// Walking sideways, aiming (property 0x41, anim state 0xc; 0xd too to the right): 4 m in anim sets 6 and 9, else
// 1.8 m.
// AUTOINJECT
bool NDrone2_CanStrafeLeft(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_LEFT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_STRAFE, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_LEFT, 0))
        return false;
    _VECTOR offset;
    offset.x = (ushort)dcv->drone->animSet == 6 || (ushort)dcv->drone->animSet == 9 ? 4.0f : 1.8f;
    offset.y = 0.0f;
    offset.z = 0.0f;
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// AUTOINJECT
bool NDrone2_CanStrafeRight(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_RIGHT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_STRAFE, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_LEFT, 0))
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_RIGHT, 0))
        return false;
    _VECTOR offset;
    offset.x = (ushort)dcv->drone->animSet == 6 || (ushort)dcv->drone->animSet == 9 ? -4.0f : -1.8f;
    offset.y = 0.0f;
    offset.z = 0.0f;
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// A dodge to the side, 3.6 m (property 0x13, anim state 0x29; 0x2a too to the right).
// AUTOINJECT
bool NDrone2_CanStrafeDodgeLeft(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_LEFT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_STRAFE_DODGE, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_DODGE, 0))
        return false;
    _VECTOR offset = {3.6f, 0.0f, 0.0f};
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// AUTOINJECT
bool NDrone2_CanStrafeDodgeRight(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_RIGHT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_STRAFE_DODGE, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_DODGE, 0))
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_DODGE + 1, 0))
        return false;
    _VECTOR offset = {-3.6f, 0.0f, 0.0f};
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// A roll to the side, 5.2 m (property 0x2e, anim state 0x53 both ways).
// AUTOINJECT
bool NDrone2_CanRollLeft(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_LEFT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_ROLL, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_ROLL_LEFT, 0))
        return false;
    _VECTOR offset = {5.2f, 0.0f, 0.0f};
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// AUTOINJECT
bool NDrone2_CanRollRight(DCVars_tag *dcv) {
    if (dcv->drone->bunchFlags & BUNCH_RIGHT)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_ROLL, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_ROLL_LEFT, 0))
        return false;
    _VECTOR offset = {-5.2f, 0.0f, 0.0f};
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// One of the pair of moves the drone can make, the side drawn at random (the other when it cannot), or 0.
// FUN_00045b30, 45bc0, 45c30 and 45ca0 in Ghidra. Not named on PS2; the names are ours.
// AUTOINJECT
short NDrone2_ChooseRoll(DCVars_tag *dcv) {
    if (!NDrone2_RollAllowed(dcv))
        return 0;
    if (Rand_Rand(2) != 0) {
        if (NDrone2_CanRollLeft(dcv))
            return DSTATE_RollLeftCrouch;
        if (NDrone2_CanRollRight(dcv))
            return DSTATE_RollRightCrouch;
    } else {
        if (NDrone2_CanRollRight(dcv))
            return DSTATE_RollRightCrouch;
        if (NDrone2_CanRollLeft(dcv))
            return DSTATE_RollLeftCrouch;
    }
    return 0;
}

// Only the property: the anim state and the stationary flag are not asked about
// AUTOINJECT
short NDrone2_ChooseStrafe(DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_STRAFE, dcv->drone->currentBehaviour) == 0)
        return 0;
    if (Rand_Rand(2) != 0) {
        if (NDrone2_CanStrafeLeft(dcv))
            return DSTATE_StrafeAimLeft;
        if (NDrone2_CanStrafeRight(dcv))
            return DSTATE_StrafeAimRight;
    } else {
        if (NDrone2_CanStrafeRight(dcv))
            return DSTATE_StrafeAimRight;
        if (NDrone2_CanStrafeLeft(dcv))
            return DSTATE_StrafeAimLeft;
    }
    return 0;
}

// The same
// AUTOINJECT
short NDrone2_ChooseStrafeDodge(DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_STRAFE_DODGE, dcv->drone->currentBehaviour) == 0)
        return 0;
    if (Rand_Rand(2) != 0) {
        if (NDrone2_CanStrafeDodgeLeft(dcv))
            return DSTATE_StrafeDodgeLeft;
        if (NDrone2_CanStrafeDodgeRight(dcv))
            return DSTATE_StrafeDodgeRight;
    } else {
        if (NDrone2_CanStrafeDodgeRight(dcv))
            return DSTATE_StrafeDodgeRight;
        if (NDrone2_CanStrafeDodgeLeft(dcv))
            return DSTATE_StrafeDodgeLeft;
    }
    return 0;
}

// AUTOINJECT
short NDrone2_ChooseStep(DCVars_tag *dcv) {
    if (!NDrone2_StepAllowed(dcv))
        return 0;
    if (Rand_Rand(2) != 0) {
        if (NDrone2_CanStepLeft(dcv))
            return DSTATE_StepAimLeft;
        if (NDrone2_CanStepRight(dcv))
            return DSTATE_StepAimRight;
    } else {
        if (NDrone2_CanStepRight(dcv))
            return DSTATE_StepAimRight;
        if (NDrone2_CanStepLeft(dcv))
            return DSTATE_StepAimLeft;
    }
    return 0;
}

// Backing off 1.8 m, aiming (property 6, anim state 4).
// AUTOINJECT
bool NDrone2_CanBackoff(DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_BACK_OFF, dcv->drone->currentBehaviour) == 0)
        return false;
    if (!DroneAnim_CanDoAnimState(dcv, DANIM_BACKOFF, 0))
        return false;
    _VECTOR offset = {0.0f, 0.0f, -1.8f};
    return NDrone2_MoveTestRelative(&offset, dcv->drone, dcv->gameObj, 0, 1);
}

// The moves NDrone2_ChooseCombatMove and NDrone2_EvasiveMove draw from (our names)
enum {
    COMBATMOVE_STRAFE_DODGE = 1,
    COMBATMOVE_STRAFE,
    COMBATMOVE_ROLL,
    COMBATMOVE_STEP,
    COMBATMOVE_AIM_CROUCH,
    COMBATMOVE_AIM_STAND,
};

// A combat move, drawn from those the drone can make, or 0 to stay: aimed at, it may dodge, strafe or roll; not
// bunched up, it may strafe; it may step; not aimed at, crouch or stand to aim. Seven times in fifteen it stays put
// unless aimed at or bunched. A move it cannot make falls to the next, down to aiming where it stands.
// AUTOINJECT
short NDrone2_ChooseCombatMove(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    short moves[16] = {};
    short count = 0;
    short move;
    short current = (short)drone->sm.curState;

    if (drone->flags & DRONE_FLAG_STATIONARY)
        return 0;
    if (behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, drone->currentBehaviour) != 0)
        return 0;
    uchar bunched = drone->bunched;
    bool aimedAt = DroneWeap_OpponentIsAimingAtMe(dcv, 0.0f);
    if (Rand_Rand(15) > 6 && !aimedAt && !bunched)
        return 0;

    if (aimedAt && NDrone2_StrafeDodgeAllowed(dcv))
        moves[count++] = COMBATMOVE_STRAFE_DODGE;
    if ((aimedAt || bunched) && NDrone2_StrafeAllowed(dcv))
        moves[count++] = COMBATMOVE_STRAFE;
    if (aimedAt && NDrone2_RollAllowed(dcv))
        moves[count++] = COMBATMOVE_ROLL;
    if (NDrone2_StepAllowed(dcv))
        moves[count++] = COMBATMOVE_STEP;
    if (!aimedAt) {
        if (NDrone2_CanAimCrouch(dcv))
            moves[count++] = COMBATMOVE_AIM_CROUCH;
        moves[count++] = COMBATMOVE_AIM_STAND;
    }

    switch (moves[Rand_Rand(count)]) {
    case COMBATMOVE_STRAFE_DODGE:
        if (aimedAt && (move = NDrone2_ChooseStrafeDodge(dcv)) != 0)
            break;
    case COMBATMOVE_STRAFE:
        if (aimedAt && (move = NDrone2_ChooseStrafe(dcv)) != 0)
            break;
    case COMBATMOVE_ROLL:
        if (aimedAt && (move = NDrone2_ChooseRoll(dcv)) != 0)
            break;
    case COMBATMOVE_STEP:
        if ((move = NDrone2_ChooseStep(dcv)) != 0)
            break;
    case COMBATMOVE_AIM_CROUCH:
        if ((move = NDrone2_ChooseAimCrouch(dcv)) != 0)
            break;
    case COMBATMOVE_AIM_STAND:
        if ((move = NDrone2_ChooseAimStand()) != 0)
            break;
        return 0;
    default:
        return 0;
    }

    // Aiming where it already aims is no move
    if (move == DSTATE_AimStand && current == DSTATE_AimStandFire)
        return 0;
    if (move == DSTATE_AimCrouch && current == DSTATE_AimCrouchFire)
        return 0;
    if (move == current)
        return 0;
    return move;
}

// Aimed at from within 7.5 degrees (not stationary), a dodge to one side, if it can, and the change to it (not for a
// bot).
// The roll and strafe drawn are tried and their answers dropped; a step drawn does nothing.
// AUTOINJECT
short NDrone2_EvasiveMove(DCVars_tag *dcv) {
    short moves[16] = {};
    short count = 0;
    short move = 0;
    if (dcv->drone->flags & DRONE_FLAG_STATIONARY)
        return 0;
    if (!DroneWeap_OpponentIsAimingAtMe(dcv, 7.5f))
        return 0;

    if (NDrone2_RollAllowed(dcv))
        moves[count++] = 1;
    if (NDrone2_StrafeAllowed(dcv))
        moves[count++] = 2;
    if (NDrone2_StrafeDodgeAllowed(dcv))
        moves[count++] = 3;
    if (NDrone2_StepAllowed(dcv))
        moves[count++] = 4;

    switch (moves[Rand_Rand(count)]) {
    case 1:
        NDrone2_ChooseRoll(dcv);
    case 2:
        NDrone2_ChooseStrafe(dcv);
    case 3:
        move = NDrone2_ChooseStrafeDodge(dcv);
        break;
    default:
        break;
    }
    if (dcv->drone->dtype != DTYPE_Bot && move != 0)
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)move, 0);
    return move;
}

// ---- going after the opponent

// Goes for obj over route1: checks the route's target, plans a new route when the old is no good, and with follow
// walks it. 6 with no object, 8 when the target will not do, 3 once within the route's arrive radius; otherwise what
// planning answered (above 2 and not planned), or following (2, 3 or 0xc, else 0).
// AUTOINJECT
uint NDrone2_MoveToObject(DCVars_tag *dcv, obj_tag *obj, float radius, char follow) {
    Drone_tag *drone = dcv->drone;
    ushort links = drone->avoidLinks;
    AIRoute_tag *route = &drone->route1;
    uint result = 0;
    if (obj == NULL)
        return 6;
    drone->activeRoute = route;
    if (!AINetwork_VerifyRouteTarget(dcv->gameObj, obj, &dcv->drone->goalTarget, &dcv->drone->aiPoint, radius, route))
        return 8;

    if (!AINetwork_RouteIsValid(route)) {
        result = AINetwork_CalcRouteToObject(dcv->gameObj, obj, &dcv->drone->route1, &dcv->drone->goalTarget,
                                             &dcv->drone->aiPoint, radius, links);
        if (result <= 1) {
            if (follow) {
                route->current = -1;
                NDrone2_StartRouteCreep(0, route, dcv, 0);
            }
        } else if (result != 2) {
            return result;
        }
        Vec_Copy(&dcv->drone->route1.start.pos, &dcv->drone->goalFrom.pos);
        dcv->drone->goalFrom.cel = drone->route1.start.cel;
    }

    dcv->drone->moveTargetDistance = (float)Vec_Dist2D(&dcv->gameObj->position, &dcv->drone->moveTarget);
    route->remaining = (float)AINetwork_GetRouteDistance(route);
    if (route->remaining < route->arriveRadius)
        return 3;
    if (follow) {
        result = NDrone2_FollowRoute(route, dcv);
        if (result < 2 || (result > 3 && result != 0xc))
            result = 0;
    }
    return result;
}

// Goes to another live drone (one in four, drawn for each) that sees the same opponent. True once the way is set.
// AUTOINJECT
bool NDrone2_SetGoalToDroneWithLosToOpponent(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
        if (other == drone)
            continue;
        obj_tag *gameObj = other->gameObj;
        if (gameObj == NULL)
            continue;
        Drone_tag *its = (Drone_tag *)gameObj->extraObjectData;
        if (its->flags & (DRONE_FLAG_DEAD | DRONE_FLAG_DYING))
            continue;
        if (!(its->flags & DRONE_FLAG_ENABLED))
            continue;
        if (its->health <= 0.0f)
            continue;
        if (gameObj->objectType == OBJECTTYPE_DEAD_DRONE)
            continue;
        if (gameObj->flags & 1)
            continue;
        if (other->flags & (DRONE_FLAG_DEAD | DRONE_FLAG_BIT14))
            continue;
        if (other->opponent != drone->opponent)
            continue;
        if (!(other->sightFlags & DRONE_SIGHT_SEES_OPPONENT))
            continue;
        if (Rand_Rand(4) != 0)
            continue;
        CelPos_tag at;
        at.pos = gameObj->position;
        at.cel = gameObj->inCel;
        uint result = NDrone2_MoveToGoalPosition(dcv, &at, 3.0f, 0);
        if (result == 0 || result == 2)
            return true;
    }
    return false;
}

// Goes as far as it can straight towards the opponent (AINetwork_FurthestPosition). True once the way is set.
// AUTOINJECT
bool NDrone2_SetGoalToCloserPosition(DCVars_tag *dcv) {
    obj_tag *opponent = dcv->drone->opponent;
    obj_tag *gameObj = dcv->gameObj;
    if (opponent == NULL)
        return false;
    CelPos_tag at;
    if (!AINetwork_FurthestPosition(&gameObj->position, &opponent->position, &at.pos))
        return false;
    at.cel = build_FindCel(&at.pos, glb_world);
    if (at.cel == NULL)
        return false;
    return NDrone2_MoveToGoalPosition(dcv, &at, 3.0f, 0) <= 2;
}

// ---- cover

// Crouches behind the node: with property 0x10, or at a corner without side 1.
// AUTOINJECT
bool DroneAnim_ForceCrouchCover(DCVars_tag *dcv, CoverNode_tag *node) {
    if (behaviour_util_getProperty(DRONE_PROP_AMBUSH, dcv->drone->currentBehaviour) != 0)
        return true;
    return node->type == COVER_CORNER && !(node->flags & COVERNODE_SIDE1);
}

// A corner may be used as low cover with side 2 and not side 1; any other node always.
// AUTOINJECT
bool DroneAnim_CanUseCornerAsLowCover(DCVars_tag *dcv, CoverNode_tag *node) {
    if (node->type != COVER_CORNER)
        return true;
    return !(node->flags & COVERNODE_SIDE1) && (node->flags & COVERNODE_SIDE2);
}

// Whether the drone could stand at the cover node: one of properties 0xa, 0xc, 0xb or 0xd, and a low node or a corner
// with COVERNODE_SIDE1. FUN_00033a40 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool DroneAnim_CanStandAtCover(DCVars_tag *dcv, CoverNode_tag *node) {
    if (behaviour_util_getProperty(DRONE_PROP_COVER_LEAN, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_COVER_STEP_OUT, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_COVER_LEAN_2, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_COVER_LEAN_3, dcv->drone->currentBehaviour) == 0)
        return false;
    return node->type != COVER_CORNER || (node->flags & COVERNODE_SIDE1);
}

// Leaning out crouched (property 0x10 or 0xf, at a corner side 2; property 0xa, 0xb or 0xd; a corner with
// COVERNODE_STAND2 and anim state 0x13).
// AUTOINJECT
bool DroneAnim_CanCrouchLean(DCVars_tag *dcv, CoverNode_tag *node) {
    if (behaviour_util_getProperty(DRONE_PROP_AMBUSH, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_LOW_COVER, dcv->drone->currentBehaviour) == 0)
        return false;
    if (node->type == COVER_CORNER && !(node->flags & COVERNODE_SIDE2))
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_COVER_LEAN, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_COVER_LEAN_2, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_COVER_LEAN_3, dcv->drone->currentBehaviour) == 0)
        return false;
    if (node->type != COVER_CORNER || !(node->flags & COVERNODE_STAND2))
        return false;
    return DroneAnim_CanDoAnimState(dcv, 0x13, 0);
}

// Stepping out crouched (property 0x10 or 0xf, at a corner side 2; property 0xc; a corner with COVERNODE_STAND1 and
// anim state 0x15).
// AUTOINJECT
bool DroneAnim_CanCrouchStepOut(DCVars_tag *dcv, CoverNode_tag *node) {
    if (behaviour_util_getProperty(DRONE_PROP_AMBUSH, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_LOW_COVER, dcv->drone->currentBehaviour) == 0)
        return false;
    if (node->type == COVER_CORNER && !(node->flags & COVERNODE_SIDE2))
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_COVER_STEP_OUT, dcv->drone->currentBehaviour) == 0)
        return false;
    if (node->type != COVER_CORNER || !(node->flags & COVERNODE_STAND1))
        return false;
    return DroneAnim_CanDoAnimState(dcv, 0x15, 0);
}

// Leaning out standing (property 0xa, 0xb or 0xd; a corner it can stand at, with COVERNODE_STAND2 and anim state 0x17).
// AUTOINJECT
bool DroneAnim_CanStandLean(DCVars_tag *dcv, CoverNode_tag *node) {
    if (behaviour_util_getProperty(DRONE_PROP_COVER_LEAN, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_COVER_LEAN_2, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_COVER_LEAN_3, dcv->drone->currentBehaviour) == 0)
        return false;
    if (!DroneAnim_CanStandAtCover(dcv, node))
        return false;
    if (node->type != COVER_CORNER || !(node->flags & COVERNODE_STAND2))
        return false;
    return DroneAnim_CanDoAnimState(dcv, 0x17, 0);
}

// Stepping out standing (property 0xc; a corner it can stand at, with COVERNODE_STAND1 and anim state 0x1b).
// AUTOINJECT
bool DroneAnim_CanStandStepOut(DCVars_tag *dcv, CoverNode_tag *node) {
    if (behaviour_util_getProperty(DRONE_PROP_COVER_STEP_OUT, dcv->drone->currentBehaviour) == 0)
        return false;
    if (!DroneAnim_CanStandAtCover(dcv, node))
        return false;
    if (node->type != COVER_CORNER || !(node->flags & COVERNODE_STAND1))
        return false;
    return DroneAnim_CanDoAnimState(dcv, 0x1b, 0);
}

// How the node serves against the drone's opponent, or 0: 1 or 2 the side of a corner (the one asked for, else the
// first it can use), 3 low cover. It must be usable against the opponent's target slot (with force, from its inner
// angles too), at least combatRanges[3] from the opponent (and with property 0x44 within 1.5 x combatRanges[4]), have
// an anim the drone can use, and unless the drone has cover already, no live drone within 1.5 m of it.
// AUTOINJECT
short NDrone2_CoverNodeOK(DCVars_tag *dcv, CoverNode_tag *node, ushort kind, char force) {
    Drone_tag *drone = dcv->drone;
    uint *usable = &node->usable[drone->targetSlot];
    if (drone->opponent == NULL)
        return 0;
    if (!(*usable & 0xe))
        return 0;
    if (force && !(*usable & COVERUSE_INNER_MASK))
        return 0;
    _VECTOR delta;
    Vec_Subtract(&drone->opponent->position, &node->pos, &delta);
    float distance = Vec_Magnitude(&delta);
    if (behaviour_util_getProperty(DRONE_PROP_NEAR_COVER_ONLY, drone->currentBehaviour) != 0 &&
        (double)drone->combatRanges[4] * 1.5f < distance)
        return 0;
    if (distance < drone->combatRanges[3])
        return 0;

    if (node->type == COVER_CORNER) {
        if (!NDrone2_CornerCoverAnimOK(dcv, node))
            return 0;
        switch (kind) {
        case 1:
            if (!(*usable & 2))
                return 0;
            break;
        case 2:
            if (!(*usable & 4))
                return 0;
            break;
        default:
            if (*usable & 2)
                kind = 1;
            else if (*usable & 4)
                kind = 2;
            else
                return 0;
            break;
        }
    } else {
        if (!NDrone2_LowCoverOK(dcv, node))
            return 0;
        if (!(*usable & 8))
            return 0;
        kind = 3;
    }

    if (!(dcv->drone->flags & DRONE_FLAG_HOLDS_COVER)) {
        for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
            if (other == drone || (other->flags & DRONE_FLAG_DEAD))
                continue;
            if (Vec_Dist3D(&other->gameObj->position, &node->pos) < 1.5f)
                return 0;
        }
    }
    return kind;
}

// Where to stand at the node for the kind of use (1 or 2 a corner's side, 3 low cover), and its rotation (turned a
// quarter on side 1); with step, where to step out to. False for any other kind.
// AUTOINJECT
bool NDrone2_GetPosForCornerCover(CoverNode_tag *node, ushort kind, _VECTOR *pos, _VECTOR *rot, char step) {
    *pos = node->pos;
    if (rot != NULL)
        *rot = node->rot;

    Extended across;    // to the side
    float back;         // and back
    switch (kind) {
    case 1:
        across = Extended((double)(int)node->unknown5) * Extended(-0.1f);
        back = 0.1f;
        if (rot != NULL)
            rot->y = rot->y - HALF_PI;
        if (step)
            back = -0.7f;
        break;
    case 2:
        back = (float)((double)(int)node->unknown6 * 0.1f);
        across = step ? 0.7f : -0.1f;
        break;
    case 3:
        back = 0.0f;
        across = step ? -1.1f : -0.3f;
        break;
    default:
        return false;
    }
    pos->x = (float)(FCos(node->rot.y) * Extended(back) + FSin(node->rot.y) * across + Extended(pos->x));
    pos->z = (float)(FCos(node->rot.y) * across - FSin(node->rot.y) * Extended(back) + Extended(pos->z));
    return true;
}

// Whether a corner has an anim the drone can use there (when it must crouch there, a crouched one or the corner as
// low cover); any other node does. False without a node. FUN_00044250 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool NDrone2_CornerCoverAnimOK(DCVars_tag *dcv, CoverNode_tag *node) {
    if (node == NULL)
        return false;
    if (node->type != COVER_CORNER)
        return true;
    if (DroneAnim_ForceCrouchCover(dcv, node))
        return DroneAnim_CanCrouchLean(dcv, node) || DroneAnim_CanCrouchStepOut(dcv, node) ||
               DroneAnim_CanUseCornerAsLowCover(dcv, node);
    return DroneAnim_CanCrouchLean(dcv, node) || DroneAnim_CanCrouchStepOut(dcv, node) ||
           DroneAnim_CanStandLean(dcv, node) || DroneAnim_CanStandStepOut(dcv, node) ||
           DroneAnim_CanUseCornerAsLowCover(dcv, node);
}

// Whether a low node may be used: property 0xf; any other node may. FUN_000442e0 in Ghidra. Not named on PS2; the
// name is ours.
// AUTOINJECT
bool NDrone2_LowCoverOK(DCVars_tag *dcv, CoverNode_tag *node) {
    if (node->type != COVER_LOW)
        return true;
    return behaviour_util_getProperty(DRONE_PROP_LOW_COVER, dcv->drone->currentBehaviour) != 0;
}

// Where to stand at the node against a target slot: the kind of use its bits give (low cover over side 2 over side
// 1), or false when it serves none. FUN_000445a0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool NDrone2_GetCoverPosForTarget(CoverNode_tag *node, uint target, _VECTOR *pos, _VECTOR *rot, char step) {
    uint usable = node->usable[target];
    ushort kind = 0;
    if (usable & 2)
        kind = 1;
    if (usable & 4)
        kind = 2;
    if (usable & 8)
        kind = 3;
    if (kind == 0)
        return false;
    return NDrone2_GetPosForCornerCover(node, kind, pos, rot, step);
}

// Takes the cover node the drone was given, for the kind of use found (the low two bits of coverFlags): held by the
// drone, the place to slide to worked out for its target, and the drone marked as having cover and seeking it.
// FUN_000464a0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
void NDrone2_TakeCover(DCVars_tag *dcv, char kind) {
    Drone_tag *drone = dcv->drone;
    drone->coverNode->occupant = dcv->gameObj;
    drone->coverFlags = (drone->coverFlags & ~3) | (kind & 3);
    drone->coverNode->flags |= COVERNODE_HELD;
    NDrone2_GetCoverPosForTarget(drone->coverNode, drone->targetSlot, &drone->coverPos.pos, NULL, 0);
    drone->coverPos.cel = drone->coverNode->cel;
    dcv->drone->flags |= DRONE_FLAG_GOING_TO_COVER;
    dcv->drone->flags |= DRONE_FLAG_HOLDS_COVER;
}

// Lets go of its cover node.
// AUTOINJECT
void NDrone2_DisownCover(DCVars_tag *dcv) {
    if (dcv->drone->coverNode != NULL) {
        dcv->drone->coverNode->occupant = NULL;
        dcv->drone->coverNode->flags &= ~COVERNODE_HELD;
    }
    dcv->drone->flags &= ~DRONE_FLAG_GOING_TO_COVER;
    dcv->drone->flags &= ~DRONE_FLAG_HOLDS_COVER;
}

// Takes the cover node nearest over its path (by the node's flood), no further than the opponent and not held, that
// serves (NDrone2_CoverNodeOK); the route goes to it. Without force, not while it seeks cover already.
// AUTOINJECT
bool NDrone2_FindCover(DCVars_tag *dcv, char force) {
    CoverNode_tag *node = NPCGlobals.coverNodes;
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    int best = -1;
    short bestKind = 0;
    uchar bestDistance = 0xff;
    if (NPCGlobals.numCoverNodes == 0)
        return false;
    if (!force && (drone->flags & DRONE_FLAG_HOLDS_COVER))
        return false;
    if (node == NULL)
        return false;
    ushort nearest = NDrone2_NearestNodeDCV(dcv);
    if (nearest == 0xffff)
        return false;

    float opponentDistance2 = drone->distanceToTarget * drone->distanceToTarget;
    for (uint i = 0; i < NPCGlobals.numCoverNodes; i++, node++) {
        if (node->flags & COVERNODE_HELD)
            continue;
        AIPath_tag *path = node->emitter.path;
        if (path == NULL || path != drone->route1.path)
            continue;
        uchar *flood = node->emitter.data;
        if (flood == NULL || nearest >= path->nodeCount)
            continue;
        uchar distance = flood[nearest];
        if (distance == 0xff || distance >= bestDistance)
            continue;
        _VECTOR delta;
        Vec_Subtract(&node->pos, &gameObj->position, &delta);
        Vec_AngleDifference(Atan2(delta.x, delta.z), drone->opponentAngles.y);
        if ((double)distance * distance > opponentDistance2)
            continue;
        short kind = NDrone2_CoverNodeOK(dcv, node, 0, force);
        if (kind == 0)
            continue;
        if ((dcv->drone->flags & DRONE_FLAG_GOING_TO_COVER) && drone->coverNode != NULL)
            continue;
        bestDistance = distance;
        best = i;
        bestKind = kind;
    }
    if (best == -1)
        return false;

    NDrone2_DisownCover(dcv);
    drone->coverNode = &NPCGlobals.coverNodes[best];
    // The type read is the entry's after the table's last: node has run off the end
    float radius = node->type == COVER_CORNER || node->type == COVER_LOW ? 0.5f : 1.0f;
    NDrone2_InvalidateAttackRoute(dcv);

    CelPos_tag goal;
    uint usable = drone->coverNode->usable[drone->targetSlot];
    ushort kind = 0;
    if (usable & 2)
        kind = 1;
    if (usable & 4)
        kind = 2;
    if (usable & 8)
        kind = 3;
    if (kind != 0)
        NDrone2_GetPosForCornerCover(drone->coverNode, kind, &goal.pos, &drone->aiPointRot, 1);
    goal.cel = drone->coverNode->cel;
    AINetwork_SetupGoalPosition(&dcv->drone->goalTarget, &dcv->drone->aiPoint, &goal, radius);
    NDrone2_TakeCover(dcv, (char)bestKind);
    return true;
}

// Cover to run to (property 0x30): what it has, or unless it sees the opponent within 4 m, a node it finds.
// AUTOINJECT
bool NDrone2_CoverAvailable(DCVars_tag *dcv, char force) {
    if (behaviour_util_getProperty(DRONE_PROP_USES_COVER, dcv->drone->currentBehaviour) == 0)
        return false;
    if (dcv->drone->flags & DRONE_FLAG_GOING_TO_COVER)
        return true;
    if ((dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && dcv->drone->distanceToTarget < 4.0f)
        return false;
    return NDrone2_FindCover(dcv, force);
}

// ---- the combat walk

// The anim playing is a transition (completion mode above 3, but not 8).
// AUTOINJECT
bool DroneAnim_InTransition(Drone_tag *drone) {
    if (drone == NULL)
        return false;
    return drone->animMode > 3 && drone->animMode != 8;
}

// The quarter the walk is in, from its angle off the line to the opponent: 0 towards, 1 left, 2 away, 3 right. The
// quarter it was in already keeps a wider share.
// AUTOINJECT
int DroneAnim_GetMoveQuad(float angle, ushort prev) {
    float size = angle < 0.0f ? -angle : angle;
    switch (prev) {
    case 0:
        if (size > THREE_QUARTER_PI)
            return 2;
        if (size > 0.9250245f)
            return angle > 0.0f ? 3 : 1;
        return 0;
    case 1:
        if (size > 2.495821f)
            return 2;
        if (angle > QUARTER_PI)
            return 3;
        if (angle < -0.9250245f)
            return 1;
        return 0;
    case 2:
        if (size > 2.2165682f)
            return 2;
        if (size > QUARTER_PI)
            return angle > 0.0f ? 3 : 1;
        return 0;
    case 3:
        if (size > 2.495821f)
            return 2;
        if (angle > 0.9250245f)
            return 3;
        if (angle < -QUARTER_PI)
            return 1;
        return 0;
    default:
        return 0;
    }
}

// The roll a strafe at 2 to 2.5 m from its route's end may become: of the two anims given, the one it has, or with
// both, the second half the time and otherwise 0x53 (the left roll, whichever way it strafes); 0 for neither
static short CombatRoll(DCVars_tag *dcv, short first, short second) {
    bool hasFirst = DroneAnim_CanDoAnimState(dcv, first, 0);
    bool hasSecond = DroneAnim_CanDoAnimState(dcv, second, 0);
    if (hasFirst)
        return hasSecond ? (Rand_Rand(2) != 0 ? DANIM_ROLL_LEFT : second) : first;
    return hasSecond ? second : 0;
}

// The anim for walking a combat route: by the quarter the walk is in from the opponent, running (or with property 0, 1
// or 2 walking aiming) towards it, strafing (sometimes rolling, distance being what is left of the route) or stepping
// to the side, backing off; the yaw offset that keeps it facing the opponent. It fires (firing) while walking aimed,
// having seen the opponent in the last 3 s. With a drone ahead (BUNCH_AHEAD), it waits instead.
// AUTOINJECT
bool DroneAnim_SetCombatMoveAnim(DCVars_tag *dcv, float distance) {
    Drone_tag *drone = dcv->drone;
    float yawOffset = 0.0f;
    short anim = DANIM_RUN;
    uchar fire = 0;
    if (drone->bunchFlags & BUNCH_AHEAD) {
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatWait, 0);
        return true;
    }
    if (drone->opponent == NULL) {
        drone->firing = 0;
        DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
        return true;
    }

    _VECTOR aimAt, toOpponent, toTarget;
    Vec_Add2(&drone->opponent->position, &drone->aimOffset, &aimAt);
    Vec_Subtract(&aimAt, &dcv->gameObj->position, &toOpponent);
    float opponentAngle = Atan2(toOpponent.x, toOpponent.z);
    Vec_Subtract(&drone->moveTarget, &dcv->gameObj->position, &toTarget);
    float targetAngle = Atan2(toTarget.x, toTarget.z);
    if (!NDrone2_DoAnimation(dcv->drone) || dcv->drone->opponent == NULL) {
        DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
        return true;
    }

    uchar seen = dcv->drone->unseenFrames < FRAME_RATE_INT * 3;
    ushort quad = DroneAnim_GetMoveQuad(Vec_AngleDifference(targetAngle, opponentAngle), drone->moveQuad);
    drone->moveQuad = 0;
    switch (quad) {
    case 1:
        if (dcv->drone->bunchFlags & BUNCH_LEFT)
            break;
        if (behaviour_util_getProperty(DRONE_PROP_WALK_AIMED_ANY_WAY, dcv->drone->currentBehaviour) != 0 ||
            behaviour_util_getProperty(DRONE_PROP_STRAFE, dcv->drone->currentBehaviour) != 0) {
            if (drone->animState == DANIM_STRAFE_LEFT || drone->animState == DANIM_STRAFE_DODGE)
                anim = drone->animState;
            else if (DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_LEFT, 0))
                anim = DANIM_STRAFE_LEFT;
            else
                break;
            yawOffset = HALF_PI;
            fire = seen;
            drone->moveQuad = quad;
            if (distance > 2.0f && distance < 2.5f &&
                behaviour_util_getProperty(DRONE_PROP_ROLL, dcv->drone->currentBehaviour) != 0) {
                short roll = CombatRoll(dcv, DANIM_ROLL_LEFT, 0x55);
                if (roll != 0) {
                    anim = roll;
                    fire = 0;
                }
            }
        } else if (behaviour_util_getProperty(DRONE_PROP_SIDE_STEP, dcv->drone->currentBehaviour) != 0 &&
                   DroneAnim_CanDoAnimState(dcv, DANIM_STEP_LEFT, 0) && DroneMove_DistanceToNextRouteNode(dcv) < 3.0f) {
            float limit = drone->animState == DANIM_STEP_LEFT ? 2.7925267f : 2.6179938f;
            Extended turn = AbsFchs(DroneMove_RouteTurn(dcv));
            if (turn < Extended(limit)) {
                anim = DANIM_STEP_LEFT;
                yawOffset = HALF_PI;
                drone->moveQuad = quad;
                fire = 0;
            }
        }
        break;

    case 2:
        if (dcv->drone->bunchFlags & BUNCH_BEHIND)
            break;
        if ((behaviour_util_getProperty(DRONE_PROP_WALK_AIMED_ANY_WAY, dcv->drone->currentBehaviour) != 0 ||
             behaviour_util_getProperty(DRONE_PROP_BACK_OFF, dcv->drone->currentBehaviour) != 0) &&
            DroneAnim_CanDoAnimState(dcv, DANIM_BACKOFF, 0)) {
            anim = DANIM_BACKOFF;
            yawOffset = PI;
            fire = seen;
            drone->moveQuad = quad;
        }
        break;

    case 3:
        if (dcv->drone->bunchFlags & BUNCH_RIGHT)
            break;
        if (behaviour_util_getProperty(DRONE_PROP_WALK_AIMED_ANY_WAY, dcv->drone->currentBehaviour) != 0 ||
            behaviour_util_getProperty(DRONE_PROP_STRAFE, dcv->drone->currentBehaviour) != 0) {
            if (drone->animState == DANIM_STRAFE_RIGHT || drone->animState == DANIM_STRAFE_DODGE + 1)
                anim = drone->animState;
            else if (DroneAnim_CanDoAnimState(dcv, DANIM_STRAFE_RIGHT, 0))
                anim = DANIM_STRAFE_RIGHT;
            else
                break;
            yawOffset = -HALF_PI;
            fire = seen;
            drone->moveQuad = quad;
            if (distance > 2.0f && distance < 2.5f &&
                behaviour_util_getProperty(DRONE_PROP_ROLL, dcv->drone->currentBehaviour) != 0) {
                short roll = CombatRoll(dcv, DANIM_ROLL_RIGHT, 0x56);
                if (roll != 0) {
                    anim = roll;
                    fire = 0;
                }
            }
        } else if (behaviour_util_getProperty(DRONE_PROP_SIDE_STEP, dcv->drone->currentBehaviour) != 0 &&
                   DroneAnim_CanDoAnimState(dcv, DANIM_STEP_RIGHT, 0) && DroneMove_DistanceToNextRouteNode(dcv) < 3.0f) {
            float limit = drone->animState == DANIM_STEP_RIGHT ? 2.7925267f : 2.6179938f;
            Extended turn = AbsFchs(DroneMove_RouteTurn(dcv));
            if (turn < Extended(limit)) {
                anim = DANIM_STEP_RIGHT;
                yawOffset = -HALF_PI;
                drone->moveQuad = quad;
                fire = 0;
            }
        }
        break;

    default:
        if ((behaviour_util_getProperty(DRONE_PROP_WALK_AIMED, dcv->drone->currentBehaviour) != 0 ||
             behaviour_util_getProperty(DRONE_PROP_WALK_AIMED_ANY_WAY, dcv->drone->currentBehaviour) != 0) &&
            DroneAnim_CanDoAnimState(dcv, 8, 0)) {
            if (!(dcv->drone->bunchFlags & BUNCH_AHEAD))
                anim = 8;
        } else if (behaviour_util_getProperty(DRONE_PROP_CROUCH_WALK, dcv->drone->currentBehaviour) != 0 &&
                   DroneAnim_CanDoAnimState(dcv, 0xf, 0)) {
            if (!(dcv->drone->bunchFlags & BUNCH_AHEAD))
                anim = 0xf;
        } else {
            break;
        }
        fire = seen;
        drone->moveQuad = quad;
        break;
    }

    drone->firing = fire;
    DroneAnim_CallAnim(anim, 0, 0, 0, yawOffset, 0, dcv);
    drone->yawOffset = yawOffset;
    return true;
}

// The combat states share this: the state to go to, or 0 to stay. Without an opponent (or a sighting in the last 30
// frames), NoOpponent; in a transition, stay. Otherwise the drone goes for the opponent over its route (to 6 m or
// combatRanges[1], whichever is nearer, walking it unless it fires), and each state decides from that.
// AUTOINJECT
short DroneFunc_CombatState(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    short state = (short)drone->sm.curState;
    if (!NDrone2_HasOpponent(dcv))
        return DSTATE_NoOpponent;
    if (DroneAnim_InTransition(drone))
        return 0;

    char follow = state == DSTATE_AimStandFire || state == DSTATE_AimCrouchFire ? 0 : 1;
    float range = drone->combatRanges[1] > 6.0f ? 6.0f : drone->combatRanges[1];
    uint route = NDrone2_MoveToObject(dcv, drone->opponent, range, follow);
    bool routeOK = route <= 3 || route == 10;

    switch (state) {
    case DSTATE_Combat:
        if ((dcv->drone->flags & DRONE_FLAG_STATIONARY) ||
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) != 0)
            return DSTATE_CombatNoMove;
        if (!DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[1]))
            return DSTATE_CombatOutOfRange;
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)
            return DSTATE_CombatNewSighting;
        return dcv->drone->unseenFrames > 15 ? DSTATE_CombatNoSight : DSTATE_CombatNoRoute;

    case DSTATE_CombatOutOfRange:
        if ((dcv->drone->flags & DRONE_FLAG_STATIONARY) ||
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) != 0)
            return DSTATE_CombatNoMove;
        if (NDrone2_CoverAvailable(dcv, 1))
            return DSTATE_RunForCover;
        if (DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[1]))
            return DSTATE_Combat;
        switch (route) {
        case 0:
        case 1:
        case 2:
            if (dcv->drone->bunchFlags & BUNCH_AHEAD) {
                DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
                return 0;
            }
            if (GameState.CurrentLevelHashcode == HT_Level_CastleExterior ||
                GameState.CurrentLevelHashcode == HT_Level_CastleCourtyard) {
                DroneAnim_SetCombatMoveAnim(dcv, dcv->drone->activeRoute->remaining);
                return 0;
            }
            DroneAnim_CallAnim(DANIM_RUN, 0, 0, 0, 0.0f, 0, dcv);
            return 0;
        case 3:
        case 9:
        case 11:
        case 12:
            break;
        case 5:
            return DSTATE_DroneStuck;
        case 10:
            return 0;
        default:
            return DSTATE_CombatNoRoute;
        }
        break;

    case DSTATE_CombatNoSight:
        if ((dcv->drone->flags & DRONE_FLAG_STATIONARY) ||
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) != 0)
            return DSTATE_CombatNoMove;
        if (NDrone2_CoverAvailable(dcv, 1))
            return DSTATE_RunForCover;
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)
            return DSTATE_CombatNewSighting;
        if (dcv->drone->unseenFrames > 15) {
            switch (route) {
            case 0:
            case 1:
            case 2:
                DroneAnim_SetCombatMoveAnim(dcv, dcv->drone->activeRoute->remaining);
                return 0;
            case 3:
            case 9:
            case 11:
            case 12:
                break;
            case 5:
                return DSTATE_DroneStuck;
            case 10:
                return 0;
            default:
                return DSTATE_CombatNoRoute;
            }
        }
        break;

    case DSTATE_CombatWait:
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (routeOK)
            return DSTATE_Combat;
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) {
            drone->firing = 1;
            DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
        } else {
            DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        }
        return 0;

    case DSTATE_AimStandFire:
        if (dcv->drone->clip <= 0)
            return DSTATE_AimStandReload;
        if (routeOK && !DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[1]))
            return DSTATE_CombatOutOfRange;
        if (dcv->drone->unseenFrames > 15)
            return DSTATE_CombatNoSight;
        if (NDrone2_CoverAvailable(dcv, 1))
            return DSTATE_RunForCover;
        if (drone->animState == DANIM_AIM_STAND && drone->animMode == 2) {
            if (DroneWeap_CanThrowGrenade(dcv))
                return DSTATE_GrenadeThrow;
            short move = NDrone2_ChooseCombatMove(dcv);
            if (move != 0)
                return move;
        }
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
        return 0;

    case DSTATE_AimCrouchFire:
        if (routeOK && !DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[1]))
            return DSTATE_CombatOutOfRange;
        if (dcv->drone->unseenFrames > 15)
            return DSTATE_CombatNoSight;
        if (NDrone2_CoverAvailable(dcv, 1))
            return DSTATE_RunForCover;
        if (DroneWeap_CanThrowGrenade(dcv))
            return DSTATE_GrenadeThrow;
        // two seconds in the state before a move
        if ((double)GameState.NumFramesUnpaused >
            (double)FRAME_RATE_DIV * 120.0f + (double)drone->sm.stateEnteredFrame) {
            short move = NDrone2_ChooseCombatMove(dcv);
            if (move != 0)
                return move;
        }
        DroneAnim_CallAnim(DANIM_AIM_CROUCH, 0, 0, 0, 0.0f, 0, dcv);
        return 0;

    default:
        return 0;
    }

    // In range of a route (or with none to walk): face the opponent and aim when it is seen
    NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
    if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) {
        drone->firing = 1;
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
    } else {
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
    }
    return 0;
}

// ---- the states

// Every impact message goes to DroneFunc_HandleImpact with nextState; a punch and a bullet with anim, the rest with
// anim set. False for any other message.
static bool HandleImpacts(DCVars_tag *dcv, MsgObject *msg, uint nextState, char anim) {
    switch (msg->msgType) {
    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, nextState, anim);
        return true;
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, nextState, 1);
        return true;
    default:
        return false;
    }
}

// While firing: shots come from the anim until the burst is over, then from DroneWeap_Fire. True when the anim fired.
static bool FireOrAnim(DCVars_tag *dcv, Drone_tag *drone) {
    if (!dcv->drone->burstOver) {
        if (drone->firing) {
            DroneAnim_PlayFiringAnim(dcv);
            return true;
        }
    } else if (drone->firing) {
        DroneWeap_Fire(1, dcv);
    }
    return false;
}

// The way into a fight: the first attack's state, the reaction of one with no weapon, a sniper's aim, aiming where it
// stands (property 0x18, in range), or Combat.
// AUTOINJECT
bool NDrone2_DSTATE_Attack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->aiStateMachine->scratch1c = DroneFunc_FirstAttack(dcv);
        if (dcv->aiStateMachine->scratch1c == 0) {
            dcv->drone->alertness = 1.0f;
            Drone_AlertStatusSet(1, dcv);
            dcv->aiStateMachine->scratch1c = DroneFunc_AttackNoWeapon(dcv);
        }
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        CaptainWeapon(dcv, drone);
        dcv->drone->flags |= DRONE_FLAG_DEAF;
        if (dcv->drone->dtype == DTYPE_Sniper || dcv->drone->dtype == DTYPE_SniperAlert)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SniperAim, 0);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        if (behaviour_util_getProperty(DRONE_PROP_ATTACK_ON_SIGHT, dcv->drone->currentBehaviour) != 0 &&
            DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[1]) && !psiStubFalse()) {
            dcv->drone->initialState = DSTATE_AimStandFire;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
        }
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Combat, 0);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_Alerted1stEncounter(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatOutOfRange, 0);
        return true;
    default:
        return false;
    }
}

// Fully alert; each frame DroneFunc_CombatState says where to go.
// AUTOINJECT
bool NDrone2_DSTATE_Combat(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->alertness = 1.0f;
        return true;

    case DRONE_MSG_Exit:
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        dcv->aiStateMachine->scratch1c = DroneFunc_CombatState(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    default:
        return false;
    }
}

// A drone that makes no combat moves fights where it stands: turning to its opponent and firing, close combat when it
// can, and skin class 0xc sometimes crouches to cover. Anim set 0x13 waits a second before its first burst.
// AUTOINJECT
bool NDrone2_DSTATE_CombatNoMove(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (switch_BLIND_DRONES)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        Drone_AlertStatusSet(1, dcv);
        if (dcv->drone->dtype == DTYPE_Sniper)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SniperFire, 0);
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_AttackTalk(dcv);
        if (drone->animSet == 0x13) {
            drone->firing = 0;
            SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT);
            return true;
        }
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Exit:
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (!(dcv->drone->flags & DRONE_FLAG_STATIONARY) &&
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStandFire, 0);
        DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, kAimTurnTolerance);
        if (NDrone2_CanAltAttack(dcv, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AltAttack, 0);
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        drone->firing = (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) != 0;
        if (drone->firing)
            drone->firing = !drone->gunThroughWall;
        if (FireOrAnim(dcv, drone))
            return true;
        if (drone->skinClass == SKINCLASS_ZOE && Rand_Rand(3) == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CrouchCover, 0);
        return true;

    case DRONE_MSG_TimerA:
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 1);
    }
}

// Too far from the opponent: going for it (DroneFunc_CombatState). With BLIND_DRONES on, it investigates where the
// opponent is instead.
// AUTOINJECT
bool NDrone2_DSTATE_CombatOutOfRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
    case 0x1b:
        return true;

    case DRONE_MSG_Enter:
        if (switch_BLIND_DRONES) {
            AINetwork_InvalidateRoute(&dcv->drone->route1);
            AINetwork_SetupGoalPositionToObj(&dcv->drone->goalTarget, &dcv->drone->aiPoint, dcv->drone->opponent, 2.0f);
            dcv->drone->initialState = DSTATE_Investigate;
            dcv->drone->returnState = DSTATE_CombatNoRoute;
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
        }
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->alertness = 1.0f;
        return true;

    case DRONE_MSG_Exit:
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NPCGlobals.outOfRangeCount++;
        dcv->aiStateMachine->scratch1c = DroneFunc_CombatState(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 0);
    }
}

// The opponent seen again: aim, crouched sometimes, after a line.
// AUTOINJECT
bool NDrone2_DSTATE_CombatNewSighting(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_SeenPlayerTalk(dcv);
        if (NDrone2_CanAimCrouch(dcv) && !(dcv->drone->distanceToTarget < 6.0f) && Rand_Rand(2) != 0) {
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimCrouchFire, 0);
            return true;
        }
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStandFire, 0);

    default:
        return false;
    }
}

// The opponent out of sight: looking for it (DroneFunc_CombatState).
// AUTOINJECT
bool NDrone2_DSTATE_CombatNoSight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->alertness = 1.0f;
        return true;

    case DRONE_MSG_Exit:
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        dcv->aiStateMachine->scratch1c = DroneFunc_CombatState(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 0);
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_CombatTooClose(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;
    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
        return true;
    default:
        return false;
    }
}

// Waiting for a way to the opponent, turned to it and firing when it is seen; every 3 s DroneFunc_CombatState
// decides again.
// AUTOINJECT
bool NDrone2_DSTATE_CombatWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->alertness = 1.0f;
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT * 3);
        DroneWeap_Fire(1, dcv);
        drone->firing = 0;
        return true;

    case DRONE_MSG_Exit:
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, kAimTurnTolerance);
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) {
            drone->firing = !drone->gunThroughWall;
            DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
            FireOrAnim(dcv, drone);
        } else {
            drone->firing = 0;
            DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        }
        return true;

    case DRONE_MSG_TimerA:
        dcv->aiStateMachine->scratch1c = DroneFunc_CombatState(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT * 3);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 0);
    }
}

// No route to the opponent: turned to it, firing when it is seen, else a combat move. Each second it tries cover, then
// by turns a route to the opponent and one to a drone that sees it; seeing it, it aims or closes in.
// AUTOINJECT
bool NDrone2_DSTATE_CombatNoRoute(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, SecondFromNow());
        dcv->drone->stateScratch = 0;
        NDrone2_InvalidateAttackRoute(dcv);
        DroneWeap_Fire(1, dcv);
        drone->firing = 0;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->flags |= DRONE_FLAG_COLLIDES;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) {
            drone->firing = !drone->gunThroughWall;
            DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
            FireOrAnim(dcv, drone);
            return true;
        }
        drone->firing = 0;
        dcv->aiStateMachine->scratch1c = NDrone2_ChooseCombatMove(dcv);
        if (dcv->aiStateMachine->scratch1c != 0 && dcv->aiStateMachine->scratch1c != DSTATE_AimStand &&
            dcv->aiStateMachine->scratch1c != DSTATE_AimCrouch)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_TimerA:
        if (!(dcv->drone->flags & DRONE_FLAG_STATIONARY) &&
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) == 0) {
            if (NDrone2_CoverAvailable(dcv, 1))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunForCover, 0);
            dcv->drone->stateScratch = ~dcv->drone->stateScratch;
            if (dcv->drone->stateScratch != 0) {
                uint route = NDrone2_MoveToObject(dcv, dcv->drone->opponent, 2.0f, 1);
                if (route <= 2)
                    return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Combat, 0);
                if (route == 10)
                    return true;
            } else if (NDrone2_SetGoalToDroneWithLosToOpponent(dcv)) {
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
            }
        }
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT) {
            if (Rand_Rand(2) != 0 && DroneFunc_InRangeOfTarget(dcv, dcv->drone->combatRanges[1]))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
            if (NDrone2_SetGoalToCloserPosition(dcv))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_GoToGoalPosition, 0);
        }
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, SecondFromNow());
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 1);
    }
}

// The opponent gone: standing alert, attacking again once it has one (checked each second, and each frame outside
// multiplayer).
// AUTOINJECT
bool NDrone2_DSTATE_NoOpponent(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        Drone_AlertStatusSet(0, dcv);
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        if (MPSettings.dronesAreBots != 0)
            return true;
        SetTimerA(dcv, SecondFromNow());
        return true;

    case DRONE_MSG_Update:
        if (MPSettings.dronesAreBots != 0)
            return true;
        if (dcv->drone->opponent != NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_TimerA:
        if (dcv->drone->opponent != NULL)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        SetTimerA(dcv, SecondFromNow());
        return true;

    default:
        return false;
    }
}

// Its way blocked: it stands alert for a second, then goes back to returnState.
// AUTOINJECT
bool NDrone2_DSTATE_Obstructed(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(DANIM_ALERT_STAND, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT);
        NDrone2_InvalidateAttackRoute(dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->returnState, 0);
        return true;

    default:
        return false;
    }
}

// Stuck: it attacks again once the player's target has been rebuilt since its route was planned.
// AUTOINJECT
bool NDrone2_DSTATE_DroneStuck(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatNoRoute, 0);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (NPCGlobals.playerTarget.builtFrame > dcv->drone->route1.targetFrame)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 0);
    }
}

// Drawing its second weapon (one with none is scared), then on to its initial state; on leaving, a drone still
// without a gun takes weapon 6.
// AUTOINJECT
bool NDrone2_DSTATE_DrawWeapon(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (dcv->drone->secondWeapon <= 1)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CivilianScared, 0);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        DroneAnim_CallAnim(DANIM_DRAW_WEAPON, 0, dcv->drone->initialState, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Exit:
        if (DroneFunc_WeaponInHand(dcv) <= 1)
            DroneWeap_ChangeWeapon(dcv, 6);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_DrawWeapon);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_DrawWeapon, 0);
    }
}

// Raising its gun to aim: unarmed, it draws one; close enough, it fights hand to hand; at the anim's end it fires
// (AimStandFire), or looks for the opponent it has lost.
// AUTOINJECT
bool NDrone2_DSTATE_AimStand(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        if (!NDrone2_ObjectIsArmed(dcv->gameObj))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_DrawWeapon, 0);
        if (NDrone2_CanAltAttack(dcv, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AltAttack, 0);
        CaptainWeapon(dcv, drone);
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, DRONE_MSG_ScriptEnd, 0.0f, 0, dcv);
        drone->firing = 0;
        return true;

    case DRONE_MSG_ScriptEnd:
        if (dcv->drone->unseenFrames > 15)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatNoSight, 0);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStandFire, 0);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AimStandFire);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_AimStandFire, 1);
    }
}

// Standing and firing at the opponent: hand to hand when close, backing off within 4 m, looking for it when it is
// lost, and otherwise DroneFunc_CombatState's moves between bursts.
// AUTOINJECT
bool NDrone2_DSTATE_AimStandFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (switch_BLIND_DRONES)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        Drone_AlertStatusSet(1, dcv);
        if (dcv->drone->dtype == DTYPE_Sniper)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SniperFire, 0);
        if ((dcv->drone->flags & DRONE_FLAG_STATIONARY) ||
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatNoMove, 0);
        DroneAnim_CallAnim(DANIM_AIM_STAND, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        CaptainWeapon(dcv, drone);
        NDrone2_AttackTalk(dcv);
        if (drone->animSet == 0x13) {
            drone->firing = 0;
            SetTimerA(dcv, GameState.NumFramesUnpaused + FRAME_RATE_INT);
            return true;
        }
        if (dcv->drone->unseenFrames > 15)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatNoSight, 0);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Exit:
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, kAimTurnTolerance);
        if (NDrone2_CanAltAttack(dcv, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AltAttack, 0);
        if (!(dcv->drone->flags & DRONE_FLAG_STATIONARY) &&
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) == 0 &&
            dcv->drone->distanceToTarget <= 4.0f && NDrone2_CanBackoff(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimBackoff, 0);
        if (dcv->drone->unseenFrames > 15)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CombatNoSight, 0);
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        if (drone->animMode == 2)
            drone->standingStill = 1;
        if (drone->firing)
            drone->firing = !drone->gunThroughWall;
        if (!dcv->drone->burstOver && drone->firing) {
            DroneAnim_PlayFiringAnim(dcv);
            return true;
        }
        dcv->aiStateMachine->scratch1c = DroneFunc_CombatState(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        drone->firing = !drone->gunThroughWall;
        if (drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_TimerA:
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 1);
    }
}

// Reloading (one time in thirty the other anim, if it has it), then firing again; the clip is full at once.
// AUTOINJECT
bool NDrone2_DSTATE_AimStandReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->firing = 0;
        if (Rand_Rand(30) == 0 && DroneAnim_CanDoAnimState(dcv, DANIM_RELOAD_B, 0))
            DroneAnim_CallAnim(DANIM_RELOAD_B, 0, DSTATE_AimStandFire, 0, 0.0f, 0, dcv);
        else
            DroneAnim_CallAnim(DANIM_RELOAD, 0, DSTATE_AimStandFire, 0, 0.0f, 0, dcv);
        dcv->drone->clip = dcv->drone->clipSize;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, 0);
        return true;

    default:
        return HandleImpacts(dcv, msg, 0, 0);
    }
}

// Throwing its weapon away, then on to DrawWeapon.
// AUTOINJECT
bool NDrone2_DSTATE_AimStandDiscard(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(2, dcv);
        DroneAnim_CallAnim(DANIM_DISCARD, 0, DSTATE_DrawWeapon, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, dcv->drone->initialState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->drone->initialState, 0);
    }
}

// Going prone, then firing from there (ProneFire).
// AUTOINJECT
bool NDrone2_DSTATE_Prone(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        DroneAnim_CallAnim(DANIM_PRONE, 0, DSTATE_ProneFire, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        dcv->drone->standingStill = 1;
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_ProneFire);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_ProneFire, 1);
    }
}

// Firing lying down while it sees the opponent.
// AUTOINJECT
bool NDrone2_DSTATE_ProneFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (switch_BLIND_DRONES)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        // firing is set on dcv's drone from the one passed in
        if (dcv->drone->burstOver || !drone->firing) {
            if (dcv->drone->unseenFrames > 15)
                dcv->drone->firing = 0;
            else
                dcv->drone->firing = !drone->gunThroughWall;
            if (drone->firing)
                DroneWeap_Fire(1, dcv);
        }
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 1);
    }
}

// Backing off, firing, for 2-5 s, while the opponent is in range and seen and there is room; it dodges when aimed at,
// and leaves for a combat move (whatever NDrone2_ChooseCombatMove answers, 0 too).
// AUTOINJECT
bool NDrone2_DSTATE_AimBackoff(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(DANIM_BACKOFF, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, GameState.NumFramesUnpaused + (Rand_Rand(4) + 2) * FRAME_RATE_INT);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (dcv->drone->distanceToTarget > dcv->drone->combatRanges[1] || dcv->drone->unseenFrames > 15 ||
            !NDrone2_CanBackoff(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
        if (NDrone2_EvasiveMove(dcv) != 0)
            return true;
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        if (FireOrAnim(dcv, drone))
            return true;
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)NDrone2_ChooseCombatMove(dcv), 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AimStandFire);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_AimStandFire, 1);
    }
}

// Crouched in cover for 2 or 3 s, turned to the opponent (not skin class 0xc), then aiming again.
// AUTOINJECT
bool NDrone2_DSTATE_CrouchCover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(0, dcv);
        DroneAnim_CallAnim(DANIM_CROUCH_COVER, 0, 0, 0, 0.0f, 0, dcv);
        SetTimerA(dcv, GameState.NumFramesUnpaused + Rand_Rand(FRAME_RATE_INT) + FRAME_RATE_INT * 2);
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        if (drone->skinClass == SKINCLASS_ZOE)
            return true;
        DroneMove_TurnToObj(dcv, dcv->drone->opponent, 0.0f, kAimTurnTolerance);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AimStand);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_AimStand, 0);
    }
}

// Crouching to aim, then firing from there; DTYPE_Zoe takes cover instead.
// AUTOINJECT
bool NDrone2_DSTATE_AimCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        Drone_AlertStatusSet(1, dcv);
        if (dcv->drone->dtype == DTYPE_Zoe)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CrouchCover, 0);
        DroneAnim_CallAnim(DANIM_AIM_CROUCH, 0, DSTATE_AimCrouchFire, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 0);
    }
}

// Firing crouched: as AimStandFire, but turning without the anim and asking DroneFunc_CombatState only after a
// second in the state; an empty clip reloads.
// AUTOINJECT
bool NDrone2_DSTATE_AimCrouchFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (switch_BLIND_DRONES)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Idle, 0);
        Drone_AlertStatusSet(1, dcv);
        if (dcv->drone->dtype == DTYPE_Sniper)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SniperFire, 0);
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        NDrone2_InvalidateAttackRoute(dcv);
        DroneAnim_CallAnim(DANIM_AIM_CROUCH, 0, 0, 0, 0.0f, 0, dcv);
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_Exit:
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        if (NDrone2_CanAltAttack(dcv, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AltAttack, 0);
        if (!(dcv->drone->flags & DRONE_FLAG_STATIONARY) &&
            behaviour_util_getProperty(DRONE_PROP_STAYS_PUT, dcv->drone->currentBehaviour) == 0 &&
            dcv->drone->distanceToTarget <= 4.0f && NDrone2_CanBackoff(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimBackoff, 0);
        if (!NDrone2_HasOpponent(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_NoOpponent, 0);
        if (drone->firing)
            drone->firing = !drone->gunThroughWall;
        if (!dcv->drone->burstOver && drone->firing)
            return true;
        if ((double)GameState.NumFramesUnpaused <
            (double)FRAME_RATE_DIV * 60.0f + (double)drone->sm.stateEnteredFrame)
            return true;
        dcv->aiStateMachine->scratch1c = DroneFunc_CombatState(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        if (dcv->drone->clip <= 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimCrouchReload, 0);
        drone->firing = !drone->gunThroughWall;
        if (drone->firing)
            DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return HandleImpacts(dcv, msg, dcv->aiStateMachine->curState, 1);
    }
}

// Reloading crouched, then firing again; the clip is full at once.
// AUTOINJECT
bool NDrone2_DSTATE_AimCrouchReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->clip = dcv->drone->clipSize;
        dcv->drone->firing = 0;
        DroneAnim_CallAnim(DANIM_RELOAD, 0, DSTATE_AimCrouchFire, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, 0);
        return true;

    default:
        return HandleImpacts(dcv, msg, 0, 0);
    }
}

// Hand to hand (one of four anims), then firing again.
// AUTOINJECT
bool NDrone2_DSTATE_AltAttack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(DANIM_ALT_ATTACK, Rand_Rand(4), DSTATE_AimStandFire, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AimStandFire);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_AimStandFire, 0);
    }
}

// A step aside while aiming, then firing again. StepAimLeft and StepAimRight differ only in the anim.
static bool StepAim(DCVars_tag *dcv, Drone_tag *drone, MsgObject *msg, short anim) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(anim, 0, DSTATE_AimStandFire, 0, 0.0f, 0, dcv);
        dcv->drone->firing = 0;
        return true;

    case DRONE_MSG_Update:
        if (!DroneAnim_CanDoAnimState(dcv, anim, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStandFire, 0);
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AimStandFire);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_AimStandFire, 1);
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_StepAimLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return StepAim(dcv, drone, msg, DANIM_STEP_LEFT);
}

// AUTOINJECT
bool NDrone2_DSTATE_StepAimRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return StepAim(dcv, drone, msg, DANIM_STEP_RIGHT);
}

// Strafing (or dodging) while aiming, for a few seconds, while it can go on that way and sees the opponent; then
// aiming again. The four states differ in the anim, the time, the test, and whether they fire.
static bool StrafeAim(DCVars_tag *dcv, Drone_tag *drone, MsgObject *msg, short anim, bool dodge,
                      bool (*canGoOn)(DCVars_tag *dcv)) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(anim, 0, 0, 0, 0.0f, 0, dcv);
        if (dodge) {
            SetTimerA(dcv, GameState.NumFramesUnpaused + (Rand_Rand(2) + 2) * FRAME_RATE_INT);
            dcv->drone->firing = 0;
            dcv->drone->hitDamageScale = 0.0f;
        } else {
            SetTimerA(dcv, GameState.NumFramesUnpaused + (Rand_Rand(4) + 2) * FRAME_RATE_INT);
            DroneWeap_Fire(1, dcv);
        }
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        if (drone->animMode == 2 && (!canGoOn(dcv) || dcv->drone->unseenFrames > 15))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimStand, 0);
        if (!dodge)
            FireOrAnim(dcv, drone);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AimStandFire);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_AimStandFire, 1);
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_StrafeAimLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return StrafeAim(dcv, drone, msg, DANIM_STRAFE_LEFT, false, NDrone2_CanStrafeLeft);
}

// AUTOINJECT
bool NDrone2_DSTATE_StrafeAimRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return StrafeAim(dcv, drone, msg, DANIM_STRAFE_RIGHT, false, NDrone2_CanStrafeRight);
}

// AUTOINJECT
bool NDrone2_DSTATE_StrafeDodgeLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return StrafeAim(dcv, drone, msg, DANIM_STRAFE_DODGE, true, NDrone2_CanStrafeDodgeLeft);
}

// The anim asked for is the right strafe's (0xd), not the dodge's (0x2a).
// AUTOINJECT
bool NDrone2_DSTATE_StrafeDodgeRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return StrafeAim(dcv, drone, msg, DANIM_STRAFE_RIGHT, true, NDrone2_CanStrafeDodgeRight);
}

// A roll aside, taking half damage, ending crouched to fire (AimCrouchFire).
static bool RollCrouch(DCVars_tag *dcv, Drone_tag *drone, MsgObject *msg, short anim, uint variant) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        DroneAnim_CallAnim(anim, variant, DSTATE_AimCrouchFire, 0, 0.0f, 0, dcv);
        dcv->drone->hitDamageScale = 0.5f;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(0, 1, dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_AimStandFire);
        return true;

    default:
        return HandleImpacts(dcv, msg, DSTATE_AimStandFire, 1);
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_RollLeftCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return RollCrouch(dcv, drone, msg, DANIM_ROLL_LEFT, 0);
}

// AUTOINJECT
bool NDrone2_DSTATE_RollRightCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    return RollCrouch(dcv, drone, msg, DANIM_ROLL_RIGHT, 1);
}
