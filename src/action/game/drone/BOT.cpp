#include "BOT.h"
#include "Behaviour.h"
#include "DroneTables.h"        // DefaultBotStats; also brings in NDrone2.h (DSTATE) and Drone.h (Drone_tag)
#include "../mp/multiplayer.h"  // MPSettings, mpbots, MP_skins
#include "../obj/Player.h"      // BLData
#include "../../game.h"         // MPGame, GameState

#include <bit>
#include <string.h>

// Each bot DSTATE's class (BOTSTATE_CLASS_*), one byte per state from DSTATE_BotInit (195) to DSTATE_BotIdle (249).
// BOTSTATE_getStateType is its only reader (the original indexes it from 0x001638cd = 0x00163990 - 195), so it can
// become ours once that function is - left the game's for now, like the generated drone tables.
// XBE_GLOBAL(0x00163990, 0x37)
#define BotStateClasses (*(const uchar(*)[DSTATE_BotIdle - DSTATE_BotInit + 1])0x00163990)

// The original, through multiplayer.cpp's generated body
short Control_Plr2Ind(obj_tag* a);
// AUTOGEN
void NDrone2_Enable(char param_1, Drone_tag *param_2);
// AUTOGEN
void NDrone2_PostLoad_Init(obj_tag *param_1);
// AUTOGEN
bool NDrone2_CanSeeObject(Drone_tag *param_1, obj_tag *param_2, uint param_3, ushort param_4);
// AUTOGEN
void BOTWEAP_InitWeapon(Drone_tag *param_1);
// AUTOGEN
bool AnimObjectUpdate(obj_tag *obj);
// AUTOGEN
bool MP_changeAssassinOrTarget(obj_tag *oldObj, obj_tag *newObj);

// The MPBOT a bot gets when none is passed: Drake's default stats (BOT_init is its only user)
// XBE_GLOBAL(0x001dc550, 0x12)
static MPBOT DefaultMPBOT;

// The original's radians-to-degrees constant, one ulp below 180 / pi as a float
constexpr float RadToDeg = 57.295776f;
static_assert(std::bit_cast<uint32_t>(RadToDeg) == 0x42652ee0, "not the original's 57.295776");
constexpr float Pi = 3.1415927f;
static_assert(std::bit_cast<uint32_t>(Pi) == 0x40490fdb, "not the original's pi");

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
    placement.keys.mode = 100;
    placement.keys.secondaryMode = 100;
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
    if (!MPSettings.maybeIsTeamGame)
        MPSettings.Player[playerNum].TeamId = NO_TEAM;
    if (MPSettings.GameMode == GM_ASSASSIN)
        MPSettings.Player[playerNum].TeamId = PHOENIX;

    // The original tests only Started's low byte
    if (MPSettings.maybeDroneAIEnabled && (uchar)MPSettings.Started && !noSpawn)
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
    if (myTeam == NO_TEAM || (!MPSettings.maybeIsTeamGame && MPSettings.GameMode != GM_ASSASSIN))
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
            info->flags &= ~(BOTPLAYER_PRESENT | BOTPLAYER_TEAMMATE);
            continue;
        }

        info->flags |= BOTPLAYER_PRESENT;
        if (MPSettings.Player[i].TeamId == myTeam)
            info->flags |= BOTPLAYER_TEAMMATE;
        else
            info->flags &= ~BOTPLAYER_TEAMMATE;

        if (!(info->flags & BOTPLAYER_TEAMMATE)) {
            bool wasFiring = info->flags & BOTPLAYER_FIRING;
            bool firing;
            if (other->objectType == OBJECTTYPE_DRONE || other->objectType == OBJECTTYPE_DEAD_DRONE)
                firing = ((Drone_tag *)other->extraObjectData)->unknown3c == 0;
            else
                firing = ((BLData *)other->extraObjectData)->firedThisFrame == 1;
            if (firing)
                info->flags |= BOTPLAYER_FIRING;
            else
                info->flags &= ~BOTPLAYER_FIRING;

            if (firing)
                info->lastFiringTime = MPGame.TimeIncPaused;
            if (wasFiring && !firing && MPGame.TimeIncPaused < (double)_FRAME_RATE * 2 + info->lastFiringTime)
                info->flags |= BOTPLAYER_FIRING;
        } else if (vars->stats.personality != BOT_PERSONALITY_GUARDIAN) {
            info->flags &= ~BOTPLAYER_PRESENT;
            continue;
        }

        _VECTOR toOther;
        Vec_Subtract(&other->position, &me->position, &toOther);
        info->facingAngle = Vec_AngleDifference(other->rotation.y, maybeAtan2(toOther.x, toOther.z) + Pi) * RadToDeg;

        BOT_vars_t *otherVars;
        if (i > myIndex && i >= NUM_PLAYERS && (otherVars = ((Drone_tag *)other->extraObjectData)->botVars) != NULL &&
            (otherVars->players[myIndex].flags & BOTPLAYER_PRESENT)) {
            const BOT_playerInfo_t *mine = &otherVars->players[myIndex];
            info->distanceSq = mine->distanceSq;
            info->flags = (info->flags & ~BOTPLAYER_VISIBLE) | (mine->flags & BOTPLAYER_VISIBLE);
        } else {
            info->distanceSq = Vec_SqDist3D(&me->position, &other->position);
            if (checkSight) {
                if (NDrone2_CanSeeObject(drone, other, 5, 0))
                    info->flags |= BOTPLAYER_VISIBLE;
                else
                    info->flags &= ~BOTPLAYER_VISIBLE;
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

    if (MPSettings.maybeDroneAIEnabled && drone->opponent != NULL && drone->distanceToTarget < 2.5f) {
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
