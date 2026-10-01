// Fragment for src/action/game/drone/BOT.cpp. The whole file after the change is below: the AUTOGEN declaration of
// BOT_getDefaultStats is replaced by our definition, BOT_respawn's AUTOGEN stays.

#include "BOT.h"
#include "DroneTables.h"        // DefaultBotStats; also brings in NDrone2.h (DSTATE) and Drone.h (Drone_tag)
#include "../mp/multiplayer.h"  // MPSettings

// Each bot DSTATE's class (BOTSTATE_CLASS_*), one byte per state from DSTATE_BotInit (195) to DSTATE_BotIdle (249).
// BOTSTATE_getStateType is its only reader (the original indexes it from 0x001638cd = 0x00163990 - 195), so it can
// become ours once that function is - left the game's for now, like the generated drone tables.
// XBE_GLOBAL(0x00163990, 0x37)
#define BotStateClasses (*(const uchar(*)[DSTATE_BotIdle - DSTATE_BotInit + 1])0x00163990)

// AUTOGEN
bool BOT_respawn(obj_tag* gameObj, int playerNum, char param_3);

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
