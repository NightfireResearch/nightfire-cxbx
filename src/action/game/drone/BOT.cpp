#include "BOT.h"
#include "BOTWEAP.h"
#include "Behaviour.h"
#include "DroneTables.h"        // DefaultBotStats; also brings in NDrone2.h (DSTATE) and Drone.h (Drone_tag)
#include "../mp/multiplayer.h"  // MPSettings, mpbots, MP_skins
#include "../obj/Player.h"      // BLData
#include "../../game.h"         // MPGame, GameState

#include <bit>
#include <math.h>
#include <string.h>
#include "DroneInit.h"
#include "DroneVision.h"
#include "../../engine/Anim.h"
#include "../obj/control.h"
#include "BOTSTATE.h"
#include "NDrone2_Opponent.h"   // NDrone2_SetOpponent
#include "../obj/build.h"       // build_LinkToRoom
#include "../sp/PlayerStats.h"  // PlrStat_LogHealth
#include "../../math/X87Ops.h"
#include "../../sound/Sound.h"
#include "../../util/Random.h"
#include "../../math/math.h"

// XBE_GLOBAL(0x001d98e0, 0x2c70)
BOT_vars_t BOT_vars[NUM_BOTS];

// Each bot DSTATE's class, one byte per state from DSTATE_BotInit (195) to DSTATE_BotIdle (249). BOTSTATE_getStateType
// is its only reader.
// XBE_GLOBAL(0x00163990, 0x37)
static const uchar BotStateClasses[DSTATE_BotIdle - DSTATE_BotInit + 1] = {
    BOTSTATE_CLASS_INIT, BOTSTATE_CLASS_INIT, BOTSTATE_CLASS_GLOBAL, BOTSTATE_CLASS_PERSONALITY,
    BOTSTATE_CLASS_PERSONALITY, BOTSTATE_CLASS_PERSONALITY, BOTSTATE_CLASS_PERSONALITY, BOTSTATE_CLASS_PERSONALITY,
    BOTSTATE_CLASS_PERSONALITY, BOTSTATE_CLASS_PERSONALITY, BOTSTATE_CLASS_PERSONALITY, BOTSTATE_CLASS_PERSONALITY,
    BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK,
    BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK,
    BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK,
    BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_ATTACK, BOTSTATE_CLASS_COVER, BOTSTATE_CLASS_COVER, BOTSTATE_CLASS_COVER,
    BOTSTATE_CLASS_COVER, BOTSTATE_CLASS_COVER, BOTSTATE_CLASS_COVER, BOTSTATE_CLASS_COVER, BOTSTATE_CLASS_COVER,
    BOTSTATE_CLASS_COVER, BOTSTATE_CLASS_MOVE, BOTSTATE_CLASS_MOVE, BOTSTATE_CLASS_MOVE, BOTSTATE_CLASS_ALERTED,
    BOTSTATE_CLASS_ALERTED, BOTSTATE_CLASS_ALERTED, BOTSTATE_CLASS_IMPACT, BOTSTATE_CLASS_IMPACT,
    BOTSTATE_CLASS_IMPACT, BOTSTATE_CLASS_DEATH, BOTSTATE_CLASS_DEATH, BOTSTATE_CLASS_DEATH, BOTSTATE_CLASS_IMPACT,
    BOTSTATE_CLASS_DOOR, BOTSTATE_CLASS_GUARD, BOTSTATE_CLASS_GUARD, BOTSTATE_CLASS_IDLE,
};

// The player's damage modifiers (Player.cpp)
// XBE_GLOBAL(0x0017e8cc, 0x4)
float Plr_DMod_Multi = 4.0f;
// XBE_GLOBAL(0x0017e8d0, 0x4)
float Plr_DMod_Head = 4.0f;
// XBE_GLOBAL(0x0017e8d4, 0x4)
float Plr_DMod_LowerLimb = 0.8f;
// XBE_GLOBAL(0x0017e8d8, 0x4)
float Plr_DMod_UpperLimb = 0.8f;

// How much a sighting nearer than 24 m distracts a bot, a metre closer
constexpr float SightingDistraction = 0.16666667f;
static_assert(std::bit_cast<uint32_t>(SightingDistraction) == 0x3e2aaaab, "not the original's 1/6");
// How far from settled a bot's aim starts
constexpr float SettleWobble = 0.099f;
static_assert(std::bit_cast<uint32_t>(SettleWobble) == 0x3dcac083, "not the original's 0.099");

// The MPBOT a bot gets when none is passed: Drake's default stats (BOT_init is its only user)
// XBE_GLOBAL(0x001dc550, 0x12)
static MPBOT DefaultMPBOT;

// The original's radians-to-degrees constant, one ulp below 180 / pi as a float

// Creates bot `playerNum` (NUM_PLAYERS and up) as a drone at `pos`, with `bot`'s character and stats, or Drake's
// defaults when `bot` is NULL. Passed an existing object, it only clears the bot's state and returns the object.
// AUTOINJECT
obj_tag* BOT_init(short playerNum, _VECTOR *pos, _VECTOR *rot, obj_tag *gameObj, MPBOT *bot, char noSpawn) {
    int botIndex = playerNum - NUM_PLAYERS;
    BOT_vars_t *vars = &BOT_vars[botIndex];

    if (Drone_bDisableSystem)
        return NULL;

    memset(vars, 0, sizeof(BOT_vars_t));

    if (bot == NULL) {
        DefaultMPBOT.isGood = MPSettings.Player[playerNum].TeamId;
        DefaultMPBOT.stats = DefaultBotStats[1];
        DefaultMPBOT.SkinNum = 1;
        bot = &DefaultMPBOT;
    }
    const BOT_stats_t *stats = &bot->stats;
    uchar skinNum = bot->SkinNum;

    DroneCreationData placement;
    memset(&placement, 0, sizeof(placement));
    placement.keys.mode = DRONE_MODE_BEHAVIOUR;
    placement.keys.secondaryMode = DRONE_MODE_BEHAVIOUR;
    placement.keys.skin = MP_skins[skinNum].skinHashcode;
    placement.keys.behaviour[0] = 0x10;     // the bot behaviour class
    if (stats->accuracy < 3)
        placement.keys.combatRange = 3;
    else if (stats->accuracy < 6)
        placement.keys.combatRange = 0;
    else if (stats->accuracy < 9)
        placement.keys.combatRange = 2;
    else
        placement.keys.combatRange = 1;

    // Both behaviours: attack mode 0, 3 words of 0x5b properties
    for (int i = 0; i < 2; i++) {
        uint *behaviour = &placement.keys.behaviour[1 + i * 5];
        behaviour[0] = 0;
        behaviour[1] = 0x5b0003;
        uint *words = &behaviour[2];
        behaviour_util_setProperty(0x20, words, 3);
        behaviour_util_setProperty(0x01, words, 1);
        behaviour_util_setProperty(0x1f, words, 1);
        behaviour_util_setProperty(0x41, words, 1);
        behaviour_util_setProperty(0x42, words, 1);
        behaviour_util_setProperty(0x13, words, 1);
        behaviour_util_setProperty(0x3c, words, 1);
        behaviour_util_setProperty(0x18, words, 1);
        behaviour_util_setProperty(0x26, words, 1);
        behaviour_util_setProperty(0x27, words, 1);
        behaviour_util_setProperty(0x28, words, 1);
        behaviour_util_setProperty(0x31, words, 1);
        behaviour_util_setProperty(0x32, words, 1);
        behaviour_util_setProperty(0x33, words, 1);
        behaviour_util_setProperty(0x4c, words, 1);
        behaviour_util_setProperty(0x4d, words, 1);
        behaviour_util_setProperty(0x07, words, 1);
        behaviour_util_setProperty(0x06, words, 1);
    }

    if (gameObj != NULL)
        return gameObj;

    gameObj = Drone_Create(pos, rot, (level_tag *)&placement);
    if (gameObj == NULL)
        return NULL;

    MPGame.players[playerNum].playerObj = gameObj;
    Vec_Copy(pos, &gameObj->position);
    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    drone->botVars = vars;
    vars->botIndex = botIndex;
    vars->playerIndex = playerNum;
    vars->perPlayerSettings = &MPSettings.Player[playerNum];
    vars->skin = skinNum;
    MPSettings.Player[playerNum].SkinNum = skinNum;
    vars->activeGoal = 0xff;
    vars->drone = drone;
    vars->preferredOpponent = 0xff;
    vars->lastPickup = 0xff;
    vars->stats = *stats;
    if (drone->botVars->stats.traitFlags & BOT_TRAIT_REGENERATE)
        vars->regenTimer = FRAME_RATE_INT + GameState.NumFramesUnpaused;

    BOTWEAP_InitWeapon(drone);
    drone->diVars.keys.weapon = vars->weapon;
    vars->goals[0].unknown39 = 0;
    vars->goals[1].unknown39 = 1;
    if (!MPSettings.isTeamGame)
        MPSettings.Player[playerNum].TeamId = NO_TEAM;
    if (MPSettings.GameMode == GM_ASSASSIN)
        MPSettings.Player[playerNum].TeamId = PHOENIX;

    // The original tests only Started's low byte
    if (MPSettings.dronesAreBots && (uchar)MPSettings.Started && !noSpawn)
        NDrone2_PostLoad_Init(drone->gameObj);
    AnimObjectUpdate(gameObj);
    return gameObj;
}

// A dead bot comes back: its old drone is disabled and marked for deletion, and a new one is created 1 m above it
// and moved to a spawn point. Not in Top Agent once the bot's count reaches MaxPoints; then, and when the new bot
// cannot be created, the old object becomes a dead drone. Returns MP_ReSpawn's result.
// AUTOINJECT
bool BOT_respawn(obj_tag* gameObj, int playerNum, char noSpawn) {
    // a signed comparison in the original
    if (MPSettings.isMultiplayer &&
        (MPSettings.GameMode != GM_TOPAGENT || (int)MPGame.players[playerNum].deaths < (int)MPSettings.MaxPoints)) {
        _VECTOR pos, rot;
        Vec_Copy(&gameObj->position, &pos);
        Vec_Copy(&gameObj->rotation, &rot);
        pos.y += 1.0f;
        NDrone2_Enable(false, (Drone_tag *)gameObj->extraObjectData);
        gameObj->flags |= 1;

        MPBOT *bot = mpbots.Enabled ? &mpbots.bot[playerNum - NUM_PLAYERS] : NULL;
        obj_tag *newObj = BOT_init(playerNum, &pos, &rot, NULL, bot, noSpawn);
        if (newObj != NULL) {
            if (MPSettings.GameMode == GM_ASSASSIN)
                MP_changeAssassinOrTarget(gameObj, newObj);
            if (noSpawn)
                return true;
            return MP_ReSpawn(newObj, playerNum);
        }
    }
    gameObj->objectType = OBJECTTYPE_DEAD_DRONE;
    gameObj->creationTimeFrames = GameState.NumFramesUnpaused;
    return false;
}

// Each frame (NDrone2_ControlSTANDARD) a bot refreshes what it knows of every other agent (BOT_vars_t.players):
// present, team-mate, firing, distance, how far it faces from the bot and, for one agent per frame in turn, whether
// the bot can see it. A bot later in the table that already has this bot in its own lends its distance and sight.
// Also notes whether another bot's drone has this bot as its opponent. PS2 name; unnamed in the Xbox's Ghidra.
// FUNC_AT(0x0001a660)
void BOT_setOtherPlayerInfo(DCVars_tag *dcv) {
    obj_tag *me = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    BOT_vars_t *vars = drone->botVars;
    bool checkSight = false;

    int myIndex = Control_Plr2Ind(me);
    if (myIndex < 0)
        return;

    vars->targeted = false;
    for (int i = NUM_PLAYERS; i < NUM_AGENTS; i++) {
        obj_tag *other = MPGame.players[i].playerObj;
        if (i != myIndex && other != NULL && other->objectType == OBJECTTYPE_DRONE &&
            ((Drone_tag *)other->extraObjectData)->opponent == me) {
            vars->targeted = true;
            break;
        }
    }

    // 3 matches no agent's team
    int myTeam = MPSettings.Player[myIndex].TeamId;
    if (myTeam == NO_TEAM || (!MPSettings.isTeamGame && MPSettings.GameMode != GM_ASSASSIN))
        myTeam = 3;

    uint sightCursor = vars->visibilityCursor;
    if (sightCursor >= NUM_AGENTS) {
        sightCursor = 0;
        vars->visibilityCursor = 0;
    }

    for (int i = 0; i < NUM_AGENTS; i++) {
        BOT_playerInfo_t *info = &vars->players[i];
        if (i == sightCursor)
            checkSight = true;

        obj_tag *other = MPGame.players[i].playerObj;
        if (other == NULL || other == me || other->objectType == OBJECTTYPE_DEAD_DRONE ||
            other->objectType == OBJECTTYPE_DEAD_PLAYER) {
            info->flags &= ~(BOT_PLAYER_PRESENT | BOT_PLAYER_TEAMMATE);
            continue;
        }

        info->flags |= BOT_PLAYER_PRESENT;
        if (MPSettings.Player[i].TeamId == myTeam)
            info->flags |= BOT_PLAYER_TEAMMATE;
        else
            info->flags &= ~BOT_PLAYER_TEAMMATE;

        if (!(info->flags & BOT_PLAYER_TEAMMATE)) {
            bool wasFiring = info->flags & BOT_PLAYER_ACTIVE;
            bool firing;
            if (other->objectType == OBJECTTYPE_DRONE || other->objectType == OBJECTTYPE_DEAD_DRONE)
                firing = ((Drone_tag *)other->extraObjectData)->burstOver == 0;
            else
                firing = ((BLData *)other->extraObjectData)->shotFired == 1;
            if (firing)
                info->flags |= BOT_PLAYER_ACTIVE;
            else
                info->flags &= ~BOT_PLAYER_ACTIVE;

            if (firing)
                info->activeTime = MPGame.TimeIncPaused;
            if (wasFiring && !firing && MPGame.TimeIncPaused < (double)_FRAME_RATE * 2 + info->activeTime)
                info->flags |= BOT_PLAYER_ACTIVE;
        } else if (vars->stats.personality != BOT_PERSONALITY_GUARDIAN) {
            info->flags &= ~BOT_PLAYER_PRESENT;
            continue;
        }

        _VECTOR toOther;
        Vec_Subtract(&other->position, &me->position, &toOther);
        info->facing = Vec_AngleDifference(other->rotation.y, Atan2(toOther.x, toOther.z) + PI) * RAD_TO_DEG;

        BOT_vars_t *otherVars;
        if (i > myIndex && i >= NUM_PLAYERS && (otherVars = ((Drone_tag *)other->extraObjectData)->botVars) != NULL &&
            (otherVars->players[myIndex].flags & BOT_PLAYER_PRESENT)) {
            const BOT_playerInfo_t *mine = &otherVars->players[myIndex];
            info->distanceSq = mine->distanceSq;
            info->flags = (info->flags & ~BOT_PLAYER_IN_SIGHT) | (mine->flags & BOT_PLAYER_IN_SIGHT);
        } else {
            info->distanceSq = Vec_SqDist3D(&me->position, &other->position);
            if (checkSight) {
                if (NDrone2_CanSeeObject(drone, other, 5, 0))
                    info->flags |= BOT_PLAYER_IN_SIGHT;
                else
                    info->flags &= ~BOT_PLAYER_IN_SIGHT;
                vars->visibilityCursor = i;
                checkSight = false;
            }
        }
    }

    // no sight test this frame: start again from agent 0
    if (checkSight)
        vars->visibilityCursor = 0;
    else
        vars->visibilityCursor++;
}

// A character's default bot stats. `identifier` is the mp_characters id; nothing checks it (29 entries).
// AUTOINJECT
BOT_stats_t* BOT_getDefaultStats(uint identifier) {
    return (BOT_stats_t *)&DefaultBotStats[identifier];
}

// How readily a bot attacks: a multiplier on its aggression stat, and up to 3x more when an opponent is within 2.5 m
// (only in multiplayer with bots). Used for the opponent search interval (NDrone2_FindOpponent) and the time between
// shots (DroneWeap_NextBulletTime).
//
// The original returns its x87 result in ST0 without rounding it to a float, and NDrone2_FindOpponent goes on
// multiplying it in ST0 before truncating to an int. The sum and product are therefore done in double and returned
// as a double (also in ST0): with the FPU at 53-bit precision that is the original's value bit for bit, where a float
// return would round it early. The constants are floats in the original, hence the f suffixes.
// AUTOINJECT
double BOT_getAggressionMul(Drone_tag *drone) {
    double mul;
    switch (drone->aggression) {
        case 0: mul = 0.3f; break;
        case 1: mul = 0.5f; break;
        case 2: mul = 0.7f; break;
        case 3: mul = 0.85f; break;
        case 4: mul = 1.0f; break;
        default: mul = 1.0f; break;     // no stat above 4 exists; the original's starting value
    }

    if (MPSettings.dronesAreBots && drone->opponent != NULL && drone->distanceToTarget < 2.5f) {
        // 1 at 2.5 m, rising as the opponent closes in, capped at 3 (reached from 0.5 m in)
        double closeness = (2.5f - (double)drone->distanceToTarget) + 1.0f;
        if (!(closeness <= 3.0f))
            closeness = 3.0f;
        mul = mul * closeness;
    }
    return mul;
}

// The arrive radius (BOTSTATE_gotoGoal) and a move speed factor (FUN_00033320, FUN_000453e0) from the bot's speed
// stat: slow 0.7, normal 1.0, fast 1.3. Any other value gives 1.0.
// AUTOINJECT
float BOT_getMovementSpeedMul(Drone_tag *drone) {
    switch (drone->speed) {
        case 0: return 0.7f;
        case 1: return 1.0f;
        case 2: return 1.3f;
        default: return 1.0f;
    }
}

// Whether a bot makes an evasive or combat move this time (BOTSTATE_EvasiveMove with odds 2 and 3,
// BOTSTATE_chooseCombatMove with 2): a 1 in (accuracy - speed + odds + 2) chance, so accurate and fast bots move more.
// Consumes one Rand_Rand.
// AUTOINJECT
bool BOT_getMovePossibility(Drone_tag *drone, int odds) {
    int range = (int)drone->accuracy - (int)drone->speed + odds + 2;
    return Rand_Rand((uint)range) == (uint)(range - 1);
}

// The class of a bot state (BOTSTATE_CLASS_*), or 0 for a state that is not a bot's.
// AUTOINJECT
uint BOTSTATE_getStateType(uint state) {
    if (state < (uint)DSTATE_BotInit || state > (uint)DSTATE_BotIdle)
        return 0;
    return BotStateClasses[state - DSTATE_BotInit];
}

// Whether a multiplayer drone is out of play: none, dead or dying, not running its AI, without health (a NaN
// counts as alive), a dead drone's object, or marked for deletion
// FUNC_AT(0x0001a190)
bool MPDrone_IsDyingOrDead(obj_tag *obj) {
    if (obj == NULL)
        return true;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    if (drone->flags & (DRONE_FLAG_DYING | DRONE_FLAG_DEAD))
        return true;
    if (!(drone->flags & DRONE_FLAG_ENABLED))
        return true;
    if (drone->health < 0.0f || drone->health == 0.0f)
        return true;
    if (obj->objectType == OBJECTTYPE_DEAD_DRONE)
        return true;
    if (obj->flags & 1)
        return true;
    return false;
}

// Whether a bot heeds a drone alert (DRONE_MSG_AlertSighting to DRONE_MSG_AlertAttack, and the global alarm): one
// without a sender always, one from another bot when they are on the same team, or in any game without teams but
// Assassin.
// AUTOINJECT
bool BOT_reactToDroneAlertMsg(uint msg, Drone_tag *drone, Drone_tag *from) {
    if (msg < DRONE_MSG_AlertSighting || (msg > DRONE_MSG_AlertAttack && msg != DRONE_MSG_GlobalAlarm))
        return false;
    if (drone->botVars == NULL)
        return false;
    if (from == NULL)
        return true;
    if (from->botVars == NULL)
        return false;
    if (!MPSettings.isTeamGame && MPSettings.GameMode != GM_ASSASSIN)
        return true;
    return ((MPSettings_PerPlayer *)drone->botVars->perPlayerSettings)->TeamId ==
           ((MPSettings_PerPlayer *)from->botVars->perPlayerSettings)->TeamId;
}

// Whether a bot takes up a state change (Drone_SM_SetState): false refuses it, and the state machine goes to
// redirectState instead (0 for none). A bot in the hill dodges rather than run; impacts, a heard noise and cover
// are refused; on the way to a goal an attack is taken up only once distracted enough or with the opponent in sight,
// and sightings add to the distraction; a dying bot only dies or respawns.
// AUTOINJECT
uchar BOT_validateStateChange(DCVars_tag *dc, uint newState) {
    uint current = dc->aiStateMachine->curState;
    dc->drone->botVars->redirectState = 0;
    if (current == 0)
        return true;

    BOT_vars_t *bot = dc->drone->botVars;
    uchar allowed = true;
    uint currentType = BOTSTATE_getStateType(current);
    uint newType = BOTSTATE_getStateType(newState);
    short playerIndex = Control_Plr2Ind(dc->gameObj);
    Drone_tag *drone = dc->drone;
    if (playerIndex < 0)
        return false;

    if ((MPGame.players[playerIndex].flags & MPPLAYER_IN_HILL) &&
        (newState == DSTATE_BotAttackRun || newState == DSTATE_BotAttackRunChangePosition)) {
        short dodge = BOTSTATE_chooseDodgeMove(dc, DODGE_LEFT | DODGE_RIGHT);
        dc->drone->botVars->redirectState = dodge;
        return false;
    }
    if (newState == DSTATE_BotImpactBullet || newState == DSTATE_BotImpactExplosive ||
        newState == DSTATE_BotImpactPunch)
        return false;
    if (newState == DSTATE_BotHeardNoise) {
        drone->heardNoiseStage = 4;
        if (dc->drone->opponent == NULL)
            dc->drone->botVars->atObjective = 1;
        return false;
    }
    if (newType == BOTSTATE_CLASS_COVER)
        return false;
    if (newType == BOTSTATE_CLASS_ALERTED && current == DSTATE_BotGotoGoalPosition) {
        BOT_goal_t *goal = BOTSTATE_getActiveGoal(drone);
        if (goal != NULL && (goal->pickFlags & BOT_PICK_NOT_DISTRACTED))
            return false;
    }
    if ((drone->botVars->flags & BOT_FLAG_RECOVERING) && newState == DSTATE_BotAttackRun)
        return false;
    if (newState == DSTATE_BotAttackChangeWeapon && bot->weapon == 1 && dc->drone->animRow >= 0x35 &&
        dc->drone->animRow <= 0x38)
        return false;

    if (current == DSTATE_BotGotoGoalPosition) {
        short opponentIndex = Control_Plr2Ind(dc->drone->opponent);
        Drone_tag *seer = dc->drone;
        bool inSight = seer->sightFlags & DRONE_SIGHT_SEES_OPPONENT;
        if (opponentIndex >= 0) {
            uchar flags = seer->botVars->players[opponentIndex].flags;
            inSight = (flags & BOT_PLAYER_PRESENT) && (flags & BOT_PLAYER_IN_SIGHT);
        }
        if (newType == BOTSTATE_CLASS_ATTACK) {
            if (BOTSTATE_isDistracted(seer) && !inSight)
                return false;
            if (drone->botVars->flags & BOT_FLAG_ENEMY_FLAG)
                return false;
        }
    } else if (newType == BOTSTATE_CLASS_ATTACK) {
        if (drone->botVars->flags & BOT_FLAG_ENEMY_FLAG)
            return false;
    }

    switch (currentType) {
        case BOTSTATE_CLASS_ATTACK:
        case BOTSTATE_CLASS_IMPACT:
        case BOTSTATE_CLASS_COVER:
            if (newState == DSTATE_BotSeenOpponent)
                allowed = false;
            break;

        case BOTSTATE_CLASS_DEATH:
            if (newType != BOTSTATE_CLASS_DEATH && newState != DSTATE_BotRespawn)
                return false;
            break;

        case BOTSTATE_CLASS_MOVE:
            if (current != DSTATE_BotGotoGoalPosition)
                break;
            if (newState == DSTATE_BotSeenOpponent) {
                // the nearer the opponent, the more a sighting distracts: 1.7 from 24 m out, up to 4.93 close up
                float amount = 1.7f;
                if (drone->opponent != NULL) {
                    float distance = drone->distanceToTarget;
                    if (distance > 30.0f)
                        distance = 30.0f;
                    else if (distance < 1.0f)
                        distance = 1.0f;
                    Extended closeness = (Extended(30.0f) - Extended(distance)) * Extended(SightingDistraction);
                    if (closeness < Extended(1.0f))
                        closeness = 1.0f;
                    amount = (float)(closeness * Extended(1.7f));
                }
                if (amount != 0.0f) {
                    if (!BOTSTATE_increaseDistraction(dc->drone, amount))
                        allowed = false;
                    break;
                }
            }
            if (newType == BOTSTATE_CLASS_ATTACK && !BOTSTATE_increaseDistraction(dc->drone, 1.0f))
                allowed = false;
            break;
    }
    return allowed;
}

// Who a bot fights, from who has attacked it lately (the last 16 hits, BOT_vars_t.attackers): the attacker seen
// most often - five times as much for its current opponent - or the new one. A dead attacker's later entries are
// forgotten. Changing opponent resets the drone's sight frames. Only for a drone with DRONE_FLAG_LOOKS_FOR_OPPONENT.
// AUTOINJECT
obj_tag* BOT_handleOpponentHistory(Drone_tag *drone, obj_tag *attacker) {
    if (!(drone->flags & DRONE_FLAG_LOOKS_FOR_OPPONENT))
        return NULL;

    BOT_vars_t *bot = drone->botVars;
    int counts[16] = {0};
    obj_tag *history[16];
    memcpy(history, bot->attackers, sizeof(history));

    uint cursor = bot->attackerCursor;
    if (cursor >= 16)
        cursor = 0;
    bot->attackers[cursor] = attacker;
    bot->attackerCursor = (uchar)(cursor + 1);

    for (int i = 0; i < 16; i++) {
        obj_tag *obj = history[i];
        int count = 0;
        if (obj != NULL) {
            if (MP_playerIsDead(obj)) {
                for (int j = i + 1; j < 16; j++) {
                    if (history[j] == obj) {
                        history[j] = NULL;
                        bot->attackers[j] = NULL;
                    }
                }
            } else {
                for (int j = i + 1; j < 16; j++) {
                    if (history[j] == obj) {
                        count++;
                        history[j] = NULL;
                    }
                }
                if (obj == drone->opponent)
                    count *= 5;
            }
        }
        counts[i] = count;
    }

    int most = 0, best = -1;
    for (int i = 0; i < 16; i++) {
        if (counts[i] > most) {
            most = counts[i];
            best = i;
        }
    }
    obj_tag *chosen = best != -1 ? history[best] : attacker;
    if (chosen != drone->opponent) {
        NDrone2_SetOpponent(drone, chosen);
        drone->unseenFrames = 0;
        drone->sightFrames = 1;
    }
    return chosen;
}

// A bot's cry: 1 on dying, 30 when hurt, 31 when hurt unless the last cry is still playing. Three death cries and
// five (two in the other voice, BOT_setGender) pain cries, one at random. Returns the sound, or 0 for none.
// AUTOINJECT
DYNAMICSOUNDS* BOT_soundEffect(DCVars_tag *dc, int kind) {
    Drone_tag *drone = dc->drone;
    uint first, count;
    switch (kind) {
        case BOT_SOUND_DEATH:
            first = drone->female ? DRONE_SFX_BOT_DEATH_FEMALE : DRONE_SFX_BOT_DEATH;
            count = 3;
            break;
        case BOT_SOUND_PAIN_UNLESS_PLAYING:
            if (Sound_Playing_Ref(drone->botVars->sound, drone->sm.id))
                return NULL;
            // fall through
        case BOT_SOUND_PAIN:
            if (drone->female) {
                first = DRONE_SFX_BOT_PAIN_FEMALE;
                count = 2;
            } else {
                first = DRONE_SFX_BOT_PAIN;
                count = 5;
            }
            break;
        default:
            return NULL;
    }
    uint sfx = Rand_Rand(count) + first;
    Sound_Stop_Ref(drone->botVars->sound, drone->sm.id);
    drone->botVars->sound = NULL;
    drone->botVars->sound = NDrone2_PlaySFX(dc, sfx, NULL, 1, 1.0f);
    return drone->botVars->sound;
}

// The voice a bot's cries use (Drone_tag.female) by its character: the other voice for characters 3-5 and 18-23.
// nightfire-port's name.
// AUTOINJECT
void BOT_setGender(Drone_tag *drone) {
    drone->female = 0;
    switch (drone->botVars->skin) {
        case 3: case 4: case 5: case 18: case 19: case 20: case 21: case 22: case 23:
            drone->female = 1;
            break;
    }
}

// A bot that has fallen out of the map is put back at a spawn point far from the others, with its goals,
// attackers and what it knows of the players cleared, and starts again idle with no opponent.
// AUTOINJECT
void BOT_fellOutMap(obj_tag *obj) {
    RespawnMode respawnMode = MPSettings.RespawnSelectionMode;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    BOT_vars_t *bot = drone->botVars;

    MPSettings.RespawnSelectionMode = RESPAWN_FAR;
    uint spawn = MP_GetSpawnPoint((ushort)MPSettings.Player[bot->playerIndex].TeamId, obj);
    MPSettings.RespawnSelectionMode = respawnMode;

    MPSpawnPoint *point = &SpawnPoints[(ushort)spawn];
    Vec_Copy2(&point->spawnPos, &obj->position, &obj->lastPosition);
    Vec_Copy2(&point->facingDir, &obj->rotation, &obj->lastRotation);
    Vec_Zero(&drone->velocity);
    build_LinkToRoom(obj, 0, glb_world);

    bot->goalFromState = DSTATE_BotIdle;
    bot->nearestNavNode = -1;
    bot->flags = 0;
    bot->activeGoal = 0xff;
    bot->stateClass = BOTSTATE_CLASS_IDLE;
    memset(bot->players, 0, sizeof(bot->players));
    bot->targeted = 0;
    bot->redirectState = DSTATE_BotIdle;
    bot->insideObjective = 0;
    memset(bot->attackers, 0, sizeof(bot->attackers));
    bot->attackerCursor = 0;
    bot->routeFailCount = 0;
    NDrone2_SetOpponent(drone, NULL);
    Drone_SM_SetState(&drone->sm, DSTATE_BotIdle, 0);
}

// Where a bot aims (targetPos): at its opponent plus aimOffset, with a wobble in aimError while it is still finding
// its aim after the opponent started or stopped moving. The wobble time grows with the bot's accuracy stat and the distance. Whether
// the opponent moves (more than 2.5 cm a call) is decided every two seconds.
// AUTOINJECT
void BOT_opponentTargetting(DCVars_tag *dc) {
    Drone_tag *drone = dc->drone;
    uint frames = GameState.NumFramesUnpaused;

    // as unsigned values
    Extended period = (double)(uint)((drone->accuracy / 3 + 1) * (FRAME_RATE_INT / 2));
    Extended scale = 1.0f;
    if (drone->distanceToTarget > 2.0f)
        scale = (Extended(drone->distanceToTarget) - Extended(2.0f)) * Extended(0.05f) + Extended(1.0f);
    float settle = (float)(scale * period);

    obj_tag *opponent = drone->opponent;
    if (opponent == NULL)
        return;

    uint since = frames - drone->targetChangeFrame;
    float wobble;
    if (since < (uint)Ftol(settle)) {
        Extended left = (Extended(settle) - Extended((double)since)) / Extended(settle);
        wobble = (float)(Extended(0.1f) - left * Extended(SettleWobble));
    } else {
        wobble = 0.1f;
    }

    _VECTOR moved;
    Vec_Subtract(&opponent->position, &drone->botVars->opponentLastPos, &moved);
    drone->botVars->opponentLastPos = drone->opponent->position;
    float distanceMoved = (float)Vec_Magnitude(&moved);

    if (drone->targetChangeFrame + FRAME_RATE_INT * 2 < frames) {
        if (!drone->targetStartedMoving) {
            if (distanceMoved > 0.025f) {
                drone->targetStartedMoving = 1;
                drone->unknown198 = 40.0f;
                drone->targetChangeFrame = frames;
            }
        } else if (distanceMoved < 0.025f) {
            drone->targetStartedMoving = 0;
            drone->unknown198 = 0.0f;
        }
    }

    drone->aimError.z = 0.0f;
    drone->aimError.x = 0.0f;
    drone->aimError.y = 0.0f;
    if (wobble != 0.1f) {
        Extended angle = Extended((double)frames) * Extended(0.05f);
        drone->aimError.x = (float)(FCos(angle) * Extended(1.2f));
        drone->aimError.y = (float)(FSin(angle) * Extended(1.7f));
    }
    Vec_Add2(&drone->opponent->position, &drone->aimOffset, &drone->targetPos);
    drone->targetPosFrame = frames;
}

// Below 1 a bot's health is 0
// AUTOINJECT
void BOT_SetHealth(Drone_tag *drone, float health) {
    if (health < 1.0f)
        health = 0.0f;
    drone->health = X87Copy(health);
    PlrStat_LogHealth(health, drone->botVars->playerIndex);
}

// A bot's stats in its drone: health (none below 1), accuracy, aggression, speed, reaction, recovery and side
// AUTOINJECT
void BOT_setDroneStats(Drone_tag *drone, BOT_stats_t *stats) {
    float health = stats->health;
    if (health < 1.0f)
        health = 0.0f;
    drone->health = health;
    PlrStat_LogHealth(health, drone->botVars->playerIndex);
    drone->accuracy = stats->accuracy;
    drone->aggression = stats->baseAggression;
    drone->speed = stats->speed;
    drone->stat9b = 3;
    drone->reaction = stats->reaction;
    drone->recover = stats->recover;
    drone->badSide = stats->isBad;
    drone->accuracyF = drone->accuracy;
}

// A bot is hurt: the damage, scaled for where it hit (with location damage on) and tripled in professional mode,
// comes first off its armour for hits of type 0 or above 7, and the rest off its health. Then a cry, and the bot is
// distracted by eight times the damage. Nothing while it is out of play, unhurt or outside a match.
// AUTOINJECT
void BOT_handlePain(DCVars_tag *dc, float damage, ushort hitType, short location) {
    Drone_tag *drone = dc->drone;
    if (MPDrone_IsDyingOrDead(dc->gameObj))
        return;
    if (drone->health < 0.0f || drone->health == 0.0f)
        return;
    if (damage < 0.0f || damage == 0.0f)
        return;
    if (GameFlow_GetState() != 2)
        return;

    Extended hurt = location != -1 ? Extended(Plr_DMod_Multi) * Extended(damage) : Extended(damage);
    if (MPSettings.LocationDamageEnabled) {
        switch (location) {
            case 5:
                hurt = hurt * Extended(Plr_DMod_Head);
                break;
            case 20: case 21: case 32: case 35:
                hurt = hurt * Extended(Plr_DMod_UpperLimb);
                break;
            case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56:
                hurt = hurt * Extended(Plr_DMod_LowerLimb);
                break;
        }
    }
    if (MPSettings.TripleDamageModifierProfessionalMode)
        hurt = hurt * Extended(3.0f);

    Extended armourTakes = hitType > 0 && hitType <= 7 ? 0.0f : 1.0f;
    BOT_vars_t *bot = drone->botVars;
    float armour = bot->armour;
    Extended absorbed = armourTakes * hurt;
    if (Extended(armour) < absorbed)
        absorbed = armour;
    bot->armour = (uchar)Ftol((double)(Extended(armour) - absorbed));
    float taken = (float)(hurt - absorbed);

    BOT_SetHealth(drone, drone->health - taken);
    PlrStat_LogHealth(drone->health, drone->botVars->playerIndex);
    BOT_soundEffect(dc, drone->health > 0.0f ? BOT_SOUND_PAIN_UNLESS_PLAYING : BOT_SOUND_DEATH);
    drone->lastDamage = taken;
    BOTSTATE_increaseDistraction(drone, taken * 8.0f);
}

// A bot's drone once its level is loaded: its stats, DRONE_FLAG_BOUNDS, and its combat ranges kept for
// BOTSTATE_defaultCombatRange
// AUTOINJECT
void BOT_postLoadInit(Drone_tag *drone) {
    BOT_setDroneStats(drone, &drone->botVars->stats);
    drone->flags |= DRONE_FLAG_BOUNDS;
    drone->botVars->savedCombatRanges[0] = drone->combatRanges[0];
    drone->botVars->savedCombatRanges[1] = drone->combatRanges[1];
    drone->botVars->savedCombatRanges[2] = drone->combatRanges[4];
}
