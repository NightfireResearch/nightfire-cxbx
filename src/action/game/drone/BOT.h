#ifndef BOT_H
#define BOT_H

#include "../../actionhelpers.h"
#include "../../engine/AINetwork.h"

#pragma pack(push, 1)
// A multiplayer bot's statistics: the defaults per character (BOT_getDefaultStats, 29 entries at 0x00163628, indexed by
// the mp_characters id), copied into each bot and edited on the bot setup page. BOT_setDroneStats puts them in the
// bot's drone. See docs/drone/bots-and-navigation/README.md, 5.3.
typedef struct BOT_stats_t {
    uchar accuracy;             // 0x00 - lower is better: 1 very good, 3 good, 5 average, 8 poor
    uchar unused1;              // 0x01 - 0 in every entry
    uchar baseAggression;       // 0x02 - 2 normal, 3 high, 4 very high
    uchar unused3;              // 0x03 - 0 in every entry
    ushort health;              // 0x04 - 50..300 (Jaws has 300: the one value over 255, so this is 16-bit)
    uchar speed;                // 0x06 - 0 slow, 1 normal, 2 fast
    uchar reaction;             // 0x07 - 50..200
    uchar recover;              // 0x08 - 50..200: frames without sight after being hit, x2/3
    uchar isBad;                // 0x09 - on the Phoenix side
    uchar preferredWeaponClass; // 0x0a - 0 none, 1..5
    uchar personality;          // 0x0b - 0 None, 1 Collector, 2 Guardian, 3 Team Player, 4 Judge, 5 Berserker,
                                //        6 Greedy, 7 Vengeful, 8 Assassin
    uchar traitFlags;           // 0x0c - 0x10 regenerates health, 8 attacks on sight, 4 prefers fists up close; 1, 2
                                //        only partly decoded
    uchar editable;             // 0x0d - 1 for the characters whose stats the bot setup page may change
} BOT_stats_t;

static_assert(sizeof(BOT_stats_t) == 0xe, "BOT_stats_t size mismatch");
static_assert(offsetof(BOT_stats_t, health) == 0x4, "BOT_stats_t health offset mismatch");
static_assert(offsetof(BOT_stats_t, isBad) == 0x9, "BOT_stats_t isBad offset mismatch");
static_assert(offsetof(BOT_stats_t, editable) == 0xd, "BOT_stats_t editable offset mismatch");

// BOT_stats_t.personality
typedef enum {
    BOT_PERSONALITY_NONE = 0,
    BOT_PERSONALITY_COLLECTOR = 1,
    BOT_PERSONALITY_GUARDIAN = 2,
    BOT_PERSONALITY_TEAM_PLAYER = 3,
    BOT_PERSONALITY_JUDGE = 4,
    BOT_PERSONALITY_BERSERKER = 5,
    BOT_PERSONALITY_GREEDY = 6,
    BOT_PERSONALITY_VENGEFUL = 7,
    BOT_PERSONALITY_ASSASSIN = 8,
} BOT_PERSONALITY;

// BOT_stats_t.traitFlags
enum {
    BOT_TRAIT_FISTS_UP_CLOSE = 4,
    BOT_TRAIT_ATTACK_ON_SIGHT = 8,
    BOT_TRAIT_REGENERATE = 0x10,
};

// One of a bot's two goals (BOTSTATE_pickGoal): slot 0 a pickup, an opponent or a friend to guard, slot 1 the game
// mode's objective. See docs/drone/bots-and-navigation/README.md 5.2 and 5.6.
typedef struct BOT_goal_t {
    CelPos_tag pos;             // 0x00
    float timeBudget;           // 0x10
    float startTime;            // 0x14 - MPGame.TimeIncPaused
    float period;               // 0x18 - 5 x the frame rate
    float weightHealth;         // 0x1c
    float weightAmmo;           // 0x20
    float weightWeapon;         // 0x24
    float weightObjective;      // 0x28
    void *target;               // 0x2c - MP_PICKUP*, the objective or the opponent
    uint arriveOrNextState;     // 0x30
    uchar flags;                // 0x34 - 1 complete
    uchar kind;                 // 0x35 - 1 pickup, 2 objective, 3 object / opponent
    uchar pickFlags;            // 0x36 - 2 single pass, 8 avoid the opponent's path, 0x20 ignore visit times
    uchar maxEmitterDistance;   // 0x37
    uchar lastRouteStatus;      // 0x38
    uchar unknown39;            // 0x39 - BOT_init: 0 in goal 0, 1 in goal 1
    uchar subtype;              // 0x3a - 1 enemy flag, 2 own base, 3 GoldenEye ... 9 opponent
    uchar _pad3b;
} BOT_goal_t;

// What a bot knows of one player (BOT_vars_t.players), refreshed by BOT_setOtherPlayerInfo (0x1a660; invented name, the PS2 name of the function)
typedef struct BOT_playerInfo_t {
    float activeTime;           // 0x0 - MPGame.TimeIncPaused while BOT_PLAYER_ACTIVE is set
    float distanceSq;           // 0x4
    float facing;               // 0x8 - degrees: the player's facing off the line to the bot
    uchar flags;                // 0xc - BOT_PLAYER_*
    uchar _pad0d[3];
} BOT_playerInfo_t;

// BOT_playerInfo_t.flags
#define BOT_PLAYER_ACTIVE   1   // a drone with burstOver clear, a player with byte 0x8eb set; kept for 2 s after
#define BOT_PLAYER_PRESENT  2   // in the game and alive
#define BOT_PLAYER_IN_SIGHT 4   // NDrone2_CanSeeObject from bone 5
#define BOT_PLAYER_TEAMMATE 8

// One weapon as a bot holds it (BOT_vars_t.weapons, indexed by weapon id). Invented name.
typedef struct BOT_weapon_t {
    float zoomRoot;             // 0x0 - sqrt of the weapon's maxZoom (BOTWEAP_resetWeapons)
    short rounds;               // 0x4 - rounds in clip
    uchar held;                 // 0x6
    uchar unknown7[5];
} BOT_weapon_t;

// A bot player's state (NUM_BOTS at 0x001d98e0, one per bot agent; Drone_tag.botVars). Ghidra's type is 0x75f
// bytes, too short.
typedef struct BOT_vars_t {
    BOT_goal_t goals[2];            // 0x000
    BOT_stats_t stats;              // 0x078 - a copy of the bot's stats
    uchar _pad86[2];
    BOT_playerInfo_t players[NUM_AGENTS]; // 0x088 - what it knows of each MPGame player (BOT_setOtherPlayerInfo)
    _VECTOR opponentLastPos;        // 0x128
    BOT_weapon_t weapons[114];      // 0x134 - per weapon id: sqrt(maxZoom), rounds in clip, held. One slot per id
                                    //         (NUM_WEAPONS), but the bot code only walks ids 0-82 (BOTWEAP_CheckWeaponsLoaded,
                                    //         BOTWEAP_listHeldLoadedWeapons stop at 0x53): 83 and up are gadgets
                                    //         (camera, decryptor, Q-worm, ...) and vehicle / boss weapons
    short reserveAmmo[33];          // 0x68c
    uchar _pad6ce[2];
    obj_tag *attackers[16];         // 0x6d0 - cursor at attackerCursor
    float savedCombatRanges[3];     // 0x710 - drone+0xd0, 0xd4, 0xe0
    float distraction;              // 0x71c
    uint goalReturnState;           // 0x720 - the DSTATE to go back to after a goal
    uint goalFromState;             // 0x724 - the DSTATE the bot was in when it set off for its goal (BOTSTATE_gotoGoal)
    uint flags;                     // 0x728 - 2 recovering, 4 objective goal active
    uint regenTimer;                // 0x72c - trait 0x10
    DYNAMICSOUNDS *sound;           // 0x730 - the bot's last cry (BOT_soundEffect)
    uint lastImpactFrame;           // 0x734
    uint lastRouteFailFrame;        // 0x738
    uint recoveryEndFrame;          // 0x73c
    uint ammoRegenTimer;            // 0x740 - weapon 0x45 (skin 0x1a)
    void *perPlayerSettings;        // 0x744 - MPSettings_PerPlayer
    Drone_tag *drone;               // 0x748
    obj_tag *guardFriend;           // 0x74c
    short playerIndex;              // 0x750
    short botIndex;                 // 0x752
    short nearestNavNode;           // 0x754 - -1 each frame
    ushort redirectState;           // 0x756 - where BOT_validateStateChange sends a refused change
    uchar skin;                     // 0x758
    uchar activeGoal;               // 0x759 - 0xff none
    uchar stateClass;               // 0x75a
    uchar visibilityCursor;         // 0x75b
    uchar weapon;                   // 0x75c
    uchar armour;                   // 0x75d
    uchar nextWeapon;               // 0x75e
    uchar preferredOpponent;        // 0x75f - player index, 0xff none
    uchar attackerCursor;           // 0x760
    uchar routeFailCount;           // 0x761
    uchar lastPickup;               // 0x762 - never the same pickup twice in a row
    uchar atObjective;              // 0x763
    uchar targeted;                 // 0x764 - another bot's drone has this bot as its opponent (BOT_setOtherPlayerInfo)
    uchar insideObjective;          // 0x765 - the protection / demolition object
    uchar _pad766[2];
} BOT_vars_t;

static_assert(sizeof(BOT_goal_t) == 0x3c, "BOT_goal_t is 0x3c bytes");
static_assert(sizeof(BOT_playerInfo_t) == 0x10, "BOT_playerInfo_t is 0x10 bytes");
static_assert(sizeof(BOT_weapon_t) == 0xc, "BOT_weapon_t is 0xc bytes");
static_assert(offsetof(BOT_weapon_t, held) == 0x6, "Wrong offset for BOT_weapon_t.held");
static_assert(sizeof(BOT_vars_t) == 0x768, "BOT_vars_t is 0x768 bytes");
static_assert(offsetof(BOT_vars_t, players) == 0x88, "Wrong offset for BOT_vars_t.players");
static_assert(offsetof(BOT_vars_t, weapons) == 0x134, "Wrong offset for BOT_vars_t.weapons");
static_assert(offsetof(BOT_vars_t, reserveAmmo) == 0x68c, "Wrong offset for BOT_vars_t.reserveAmmo");
static_assert(offsetof(BOT_vars_t, attackers) == 0x6d0, "Wrong offset for BOT_vars_t.attackers");
static_assert(offsetof(BOT_vars_t, drone) == 0x748, "Wrong offset for BOT_vars_t.drone");
static_assert(offsetof(BOT_vars_t, insideObjective) == 0x765, "Wrong offset for BOT_vars_t.insideObjective");

// One per bot, indexed by BOT_vars_t.botIndex
extern BOT_vars_t BOT_vars[NUM_BOTS];

#pragma pack(pop)

// What BOTSTATE_getStateType says a bot DSTATE is (docs/drone/bots-and-navigation/README.md 5.5). NDrone2_DSTATE_BotGlobal
// runs the goal and combat checks for classes 3..9, 12 and 13; Drone_SM_RouteMsgDCV, DroneWeap_FireWeapon and
// NDrone2_DealWithObjHit test for ATTACK, IMPACT and DEATH. 11 is not used.
typedef enum {
    BOTSTATE_CLASS_NONE = 0,        // not a bot state
    BOTSTATE_CLASS_INIT = 1,        // BotInit, BotRespawn
    BOTSTATE_CLASS_GLOBAL = 2,      // BotGlobal
    BOTSTATE_CLASS_PERSONALITY = 3, // BotCollector .. BotAssassin
    BOTSTATE_CLASS_ATTACK = 4,      // BotAttack .. BotAttackUnarmed
    BOTSTATE_CLASS_MOVE = 5,        // BotStuck, BotAlertToPosition, BotGotoGoalPosition
    BOTSTATE_CLASS_ALERTED = 6,     // BotSeenOpponent, BotSeenDroneShot, BotHeardNoise
    BOTSTATE_CLASS_IMPACT = 7,      // BotImpactBullet/Explosive/Punch/StunGrenade
    BOTSTATE_CLASS_DOOR = 8,        // BotDoorOpen
    BOTSTATE_CLASS_IDLE = 9,        // BotIdle
    BOTSTATE_CLASS_DEATH = 10,      // BotDeathAnim, BotDeathByExplosion, BotDead
    BOTSTATE_CLASS_COVER = 12,      // BotCoverRunTo .. BotCoverLeaveNow
    BOTSTATE_CLASS_GUARD = 13,      // BotGuardFriendIdle, BotGuardFriendFollow
} BOTSTATE_CLASS;

BOT_stats_t* BOT_getDefaultStats(uint identifier);
// noSpawn (0 from every caller): BOT_init does not start the drone, BOT_respawn does not move it to a spawn point
struct MPBOT;
obj_tag* BOT_init(short playerNum, _VECTOR *pos, _VECTOR *rot, obj_tag *gameObj, MPBOT *bot, char noSpawn);
bool BOT_respawn(obj_tag* gameObj, int playerNum, char noSpawn);
void BOT_setOtherPlayerInfo(DCVars_tag *dcv);
// Both return in ST0; BOT_getAggressionMul's value is not rounded to a float (see BOT.cpp)
double BOT_getAggressionMul(Drone_tag *drone);
float BOT_getMovementSpeedMul(Drone_tag *drone);
bool BOT_getMovePossibility(Drone_tag *drone, int odds);
uint BOTSTATE_getStateType(uint state);   // a BOTSTATE_CLASS
bool MPDrone_MaybeIsDyingOrDead(obj_tag *obj);
bool BOT_reactToDroneAlertMsg(uint msg, Drone_tag *drone, Drone_tag *from);
uchar BOT_validateStateChange(DCVars_tag *dc, uint newState);   // false refuses; then BOT_vars_t.redirectState
obj_tag* BOT_handleOpponentHistory(Drone_tag *drone, obj_tag *attacker);
DYNAMICSOUNDS* BOT_soundEffect(DCVars_tag *dc, int kind);
void BOT_setGender(Drone_tag *drone);
void BOT_fellOutMap(obj_tag *obj);
void BOT_opponentTargetting(DCVars_tag *dc);
void BOT_SetHealth(Drone_tag *drone, float health);
void BOT_setDroneStats(Drone_tag *drone, BOT_stats_t *stats);
void BOT_handlePain(DCVars_tag *dc, float damage, ushort hitType, short location);
void BOT_postLoadInit(Drone_tag *drone);

#endif
