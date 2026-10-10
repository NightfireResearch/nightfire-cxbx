#pragma once
#include <stddef.h>
#include "../../actionhelpers.h"
#include "../../math/math.h"
#include "../../engine/AINetwork.h"
#include "../drone/BOT.h"


typedef enum {
    GM_QUICK=0,
    GM_ARENA=1,
    GM_TOPAGENT=16,
    GM_UNK3=32,
    GM_ASSASSIN=1024,
    TEAMGAME=536870912,
    GM_TEAMARENA=536870914,
    GM_CTF=536870916,
    GM_DEMOLITION=536870976,
    GM_PROTECTION=536871040,
    GM_BLUEPRINT=536871168,
    GM_GOLDENEYE=536871424,
    GM_KOTH=1073743872,
    GM_UPLINK=1610612744,
    GM_TEAMKOTH=1610616832,
    GM_FORCE_UINT32 = 0x7fffffff
} MultiplayerGameMode;

typedef enum {
    WEAPSET_NORMAL=0,
    WEAPSET_PISTOLS=1,
    WEAPSET_AUTOMATIC=2,
    WEAPSET_SNIPERS=3,
    WEAPSET_EXPLOSIVES=4,
    WEAPSET_EXPLOSIVES2=5,
    WEAPSET_MI6=6,
    WEAPSET_PHOENIX=7,
    WEAPSET_MODERN=8,
    WEAPSET_STEALTHY=9,
    WEAPSET_RANDOM=10,
    WEAPSET_FORCE_UINT32 = 0x7fffffff
} WeaponSet;

enum RespawnMode : uint32_t {
    RESPAWN_NEAR = 0,
    RESPAWN_FAR = 1,
    RESPAWN_RANDOM = 2
};

typedef enum  {
    PHOENIX = 0,
    MI6 = 1,
    NO_TEAM = 2,
    TEAM_FORCE_UINT32 = 0x7fffffff
} MPTeam;

#define TEAM_GET_NAME(team) ((team == PHOENIX) ? "PHOENIX" : ((team == MI6) ? "MI6" : ((team == NO_TEAM) ? "NO_TEAM" : "UNKNOWN")))


#pragma pack(push, 1)

// Should contain team, character ID, health bonus etc for each agent in the multiplayer game
typedef struct {
    char Name[32];
    MPTeam TeamId;
    undefined4 SkinNum; // Only set when the game actually launches, not set in menu
    undefined4 SomeField2;
    undefined4 HealthModifier; // Handicaps are -ve, boosts are +ve. Only applies to players. 
} MPSettings_PerPlayer;
static_assert(sizeof(MPSettings_PerPlayer) == 0x30, "MPSettings_PerPlayer is wrong size");

typedef struct { // on Xbox, starts at 0025fe38

    MPSettings_PerPlayer Player[NUM_AGENTS]; // Different on PS2 and Xbox.  1E0: Xbox

    undefined4 isMultiplayer; // on Xbox, at 00260018
    undefined4 maybeDroneAIEnabled;
    bool Started; // a byte: MP_Start clears it, then sets it once the match is set up
    undefined _pad1e9[3];
    undefined4 maybeIsTeamGame;
    undefined4 field53_0x190;
    undefined4 numPlayersAndBots;
    undefined4 FriendlyFire;
    undefined4 MaxPoints;
    undefined4 MaxDuration;
    MultiplayerGameMode GameMode;
    undefined4 multiplayerLevelHashcode;
    undefined4 numPlayers;
    undefined4 numBots;
    WeaponSet weaponSet;
    undefined4 GunEmplacementsEnabled;
    undefined4 TripleDamageModifierProfessionalMode;
    RespawnMode RespawnSelectionMode;
    undefined4 ShowTeamAndNameOverhead;
    undefined4 LocationDamageEnabled;
    undefined4 MiniVehiclesEnabled;
    undefined4 GrappleEnabled;
    undefined4 ExplosiveSceneryEnabled;
    ushort numActivePickups;
    undefined field72_0x1da;
    undefined field73_0x1db;
} MPSettings_t;

static_assert(sizeof(MPSettings_t) == 572, "MPSettings_t is wrong size");
static_assert(offsetof(MPSettings_t, RespawnSelectionMode) == 0x220, "RespawnSelectionMode is at wrong offset");
static_assert(offsetof(MPSettings_t, ShowTeamAndNameOverhead) == 0x224, "ShowTeamAndNameOverhead is at wrong offset");

#define MPSettings (*((MPSettings_t*)0x0025fe38))
#define MultiplayerLayout_LeftRightOrTopBtm U32_AT(0x001f660c) // Part of a DrawInfo struct which also contains IsWidescreen?

//char (*__kaboom)[sizeof(MPSettings_t)] = 1;

// This struct is complete and correct
typedef struct {
    _VECTOR spawnPos;
    _VECTOR facingDir;
    short initialised;
    short _pad;
} MPSpawnPoint;

// True on Xbox, PS2 is larger due to padding of _VECTOR
static_assert(sizeof(MPSpawnPoint) == 0x1c, "Size of MPSpawnPoint not correct");

#define SpawnPoints (*(MPSpawnPoint(*)[64])0x00261d58)

typedef struct {
  HASHCODE skinHashcode; // ?
  HASHCODE fileHashcode; // ?
  int handType;
  bool isInThisMpGame;
  char unknown2[3];
} MP_skin;

static_assert(sizeof(MP_skin) == 0x10, "MP_skin is wrong size");

// XBE_GLOBAL(0x001637c0, 0x1d0)
#define MP_skins ((MP_skin*)0x001637c0)


typedef struct MPBOT {
    BOT_stats_t stats;  // 0x00 the character's defaults (BOT_getDefaultStats), then P_MPBOTSETUP's
    char isPlaying;     // 0x0e the bot is in the game (P_MPBOTSETUP's "Playing" option)
    char isGood;        // 0x0f its character is on MI6's side (Menu_IsBotGood)
    char SkinNum;       // 0x10 its character: an mp_characters identifier
    char statsEdited;   // 0x11 its stats were changed on P_MPBOTSETUP, so moving the character wheel keeps them
} MPBOT;

static_assert(sizeof(MPBOT) == 18, "MPBOT is wrong size");

typedef struct {
    char Enabled;
    char NumBots;
    MPBOT bot[NUM_BOTS];
} MPBOTS;

static_assert(sizeof(MPBOTS) == 0x6e, "MPBOTS is wrong size");

// XBE_GLOBAL(0x00245280, 0x6e)
#define mpbots (*(MPBOTS*)0x00245280)

// The controllers' places on the join page (P_MPJOIN)
typedef struct {
    char joined;        // 0x00
    char ready;         // 0x01 finished setting up (C_RBMPSETUP)
    char pad[2];
    MPTeam team;        // 0x04
    uint skin;          // 0x08 an mp_characters identifier
    uchar controllerPort; // 0x0c the controller's port, which P_MPCONFIRM hands to PlayerInputs[].controllerPort
    uchar _pad0d[3];
} MPJoinSlot;
static_assert(sizeof(MPJoinSlot) == 0x10, "MPJoinSlot is wrong size");

#define mp_join_slots (*(MPJoinSlot(*)[NUM_PLAYERS])0x00245240)


typedef struct {
    obj_tag* gameObj;
    CelPos_tag celPos;          // 0x04 - where bots go for it
    AIEmitter_tag aiEmitter;    // 0x14
    uint flags;                 // 0x3c - 1 or 2: its bit in MPGame.teamObjectiveFlags (BOTSTATE_processGoals)
    char unknown40[4];
} MP_OBJ_EXT;

static_assert(sizeof(MP_OBJ_EXT) == 0x44, "MP_OBJ_EXT is wrong size");
static_assert(offsetof(MP_OBJ_EXT, aiEmitter) == 0x14, "Bad offset of MP_OBJ_EXT.aiEmitter");
static_assert(offsetof(MP_OBJ_EXT, flags) == 0x3c, "Bad offset of MP_OBJ_EXT.flags");

typedef enum {
    CTF_FLAG = 0,
    CTF_BASE,
    UPLINK,
    DEMOLITION,
    ESPIONAGEBASE,
    BLUEPRINT,
    GOLDENEYE_KEY,
    GOLDENEYE_CRYSTAL,
    PROTECTION,
    KOH,
} MPOBJECTTYPE;

typedef struct {
    ushort type; // MPOBJECTTYPE
    ushort num;
    short unknown04;            // 0x04 MP_PlayerKilled sets it to 30 seconds of frames when a hurt object of types 5-7
                                //      was among the holder's damage
    char unknown06[2];
    obj_tag *holder;            // 0x08 MP_PlayerKilled hands the object back when this one dies
    obj_tag *scriptPlayer;      // 0x0c
    _MATRIX resetMtx;           // 0x10 MP_ResetMPObject puts the object back here (MP_GoldeneyeResetObject,
                                //      MP_BluePrintReachedBase pick it)
    short attackerIdx;          // 0x4c demolition, protection: the agent index (Control_Plr2Ind) that last damaged it
                                //      (our name)
    short holderIdx;            // 0x4e uplink: the agent index that took it (Control_Plr2Ind), -1 none
} MPOBJECT;

static_assert(sizeof(MPOBJECT) == 0x50, "MPOBJECT is wrong size");
static_assert(offsetof(MPOBJECT, resetMtx) == 0x10, "MPOBJECT.resetMtx is at wrong offset");
static_assert(offsetof(MPOBJECT, attackerIdx) == 0x4c, "MPOBJECT.attackerIdx is at wrong offset");
static_assert(offsetof(MPOBJECT, holderIdx) == 0x4e, "MPOBJECT.holderIdx is at wrong offset");


typedef struct {
    MP_OBJ_EXT keys[2];
    obj_tag *deathRay;
    char unused1[64]; // Seems unused
    obj_tag *targetedPlayer;
    char unused2[64]; // Also seems unused
} GoldenEyeStruct;

static_assert(sizeof(GoldenEyeStruct) == 0x110, "GoldenEyeStruct is wrong size"); // Known from MP_Init

typedef struct {
  _MATRIX mtx;
  uint maybePlacementData;
  char unknown[24];
  celglist_tag *celgl;
} SpawnPlace;

static_assert(sizeof(SpawnPlace) == 0x5c, "Bad size for SpawnPlace"); // Known from MP_Init via size of DemolitionPlaces array

#pragma pack(pop)

#define GoldenEye (*(GoldenEyeStruct(*))0x00261678)
#define GoldenEyeSpawns (*(SpawnPlace(*)[16])0x00262978)
#define GoldenEyeKeyCount U16_AT(0x00262970)
#define GoldenEyeNonKeyCount U16_AT(0x00262972)


// One multiplayer weapon/ammo pickup slot (Ghidra's MP_PICKUP; 64 of them, 0x1600 bytes, from MP_Init's clear)
#pragma pack(push, 1)
typedef struct {
    obj_tag *gameObj;
    CelPos_tag celPos;                  // 0x04
    AIEmitter_tag aiEmitter;            // 0x14 so bots can path to it
    float maybeBotPickupVisitTimes[NUM_BOTS]; // 0x3c one per bot: MP_Pickup_Process ages them all, MP_ResetBotPickupTimes
                                        //      and BOTSTATE_pickGoal index it by bot
    uint32_t unknown_0x54;              // 0x54 no reference in the XBE (MP_Init's clear aside)
} MP_PICKUP;
#pragma pack(pop)
static_assert(sizeof(MP_PICKUP) == 0x58, "MP_PICKUP is wrong size");
static_assert(offsetof(MP_PICKUP, aiEmitter) == 0x14, "Bad offset of MP_PICKUP.aiEmitter");

#define MPpickups (*(MP_PICKUP(*)[64])0x00260078)

// What a radar blip stands for (MP_GetRadarObjects); HUD_RadarUpdate picks its sprite by it
typedef enum {
    MP_RADAR_AGENT = 0,
    MP_RADAR_FLAG = 1,
    MP_RADAR_UPLINK = 2,
    MP_RADAR_OBJECTIVE = 3,         // the demolition or protection object
    MP_RADAR_GOLDENEYE_KEY = 4,
    MP_RADAR_BLUEPRINT = 6,
    MP_RADAR_BASE = 7,              // an espionage base
} MP_RADAR_TYPE;

// One radar blip, as MP_GetRadarObjects lists them for HUD_RadarUpdate
#pragma pack(push, 1)
typedef struct {
    _VECTOR pos;        // 0x00
    uint colour;        // 0x0c 0xRRGGBBAA
    ushort type;        // 0x10 MP_RADAR_TYPE
    ushort _pad12;
} MP_RADAR_OBJECT;
#pragma pack(pop)
static_assert(sizeof(MP_RADAR_OBJECT) == 0x14, "MP_RADAR_OBJECT is wrong size");

// Room for 22 up to Uplinks (0x002633d8); MP_GetRadarObjects writes at most NUM_AGENTS + 8
#define MPRadarObjects (*(MP_RADAR_OBJECT(*)[22])0x00263220)

#define BluePrints (*(SpawnPlace(*)[8])0x00262458)
#define BluePrintCount U16_AT(0x002637d4)

#define CurrentAssassinObjId (*(obj_tag **)0x0026178c)
#define AssassinTarget (*(obj_tag **)0x00261788)
#define MPObjects (*(obj_tag*(*)[64])0x00263640)
#define Bases (*(MP_OBJ_EXT(*)[2])0x00261bd0)
#define EsponageBase (*(MP_OBJ_EXT(*)[2])0x00261a70)
#define TimeSpr (*(sprite **)0x002637d8)
#define StatusSpr (*(sprite **)0x002637dc)

void MP_setLoadingSkins(void);
bool MP_areObjectsOnSameTeam(obj_tag* a, obj_tag* b);
bool MP_isObjectOnTeam(obj_tag *param_1,uint teamId);
MPTeam MP_getObjectTeam(obj_tag* param_1);
bool MP_IsAssasin(obj_tag *param_1);
bool MP_IsTarget(obj_tag *param_1);
void MP_RegisterSpawnPoint(_VECTOR *position, _VECTOR *facingDirection, ushort teamId);
uint MP_GetSpawnPoint(short teamId, obj_tag *respawningPlayer);
obj_tag* MP_RegisterMPObject(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void MP_objectBeingDeleted(obj_tag* obj);
void MP_Update(void);
MP_OBJ_EXT* MP_getFlagObj(uint i);
MP_OBJ_EXT* MP_getBaseObj(uint i);
MP_OBJ_EXT* MP_getDemolitionObj(void);
MP_OBJ_EXT* MP_getProtectionObj(void);
MP_OBJ_EXT* MP_getHillObj(void);
MP_OBJ_EXT* MP_getObjExtFromMPOBJECT(MPOBJECT *mpObj);
void _MP_recalcObjExtPaths(MP_OBJ_EXT *ext, bool keepPos);
// The original takes ext in ESI: MP_recalcObjExtPaths is the entry for that, _MP_recalcObjExtPaths the C++ under it.
void MP_recalcObjExtPaths(bool keepPos);
void MP_ResetMPObject(MPOBJECT *mpObj, ushort state, obj_tag *gameObj, bool keepPlace);
void MP_SetUpPlayerSomehow(obj_tag *gameObj, obj_tag *holder, uint attach);
short MP_PlayerOrBotInd(obj_tag *obj);
void MP_SortOutWhoWon(void);
void MP_Pickup_Process(void);
void MP_CheckForEndCondition(void);
void MP_RestartScenario(void);
void MP_Init(void);
obj_tag* MP_CreateObject(_MATRIX *mtx, unsigned short* data, celglist_tag *celgl);
bool MP_ReSpawn(obj_tag* obj, ushort idx);
// The original takes obj in EAX and team in CX: MP_HitBy is the entry for that, _MP_HitBy the C++ under it
obj_tag* MP_HitBy(obj_tag *obj, ushort team, obj_tag *exclude, ushort *teamOut);
obj_tag* _MP_HitBy(obj_tag *obj, ushort team, obj_tag *exclude, ushort *teamOut);
obj_tag* MP_GetTarget(ushort team, obj_tag *exclude, bool aliveOnly);
bool MP_playerIsDead(obj_tag *obj);
void MP_ResetBotPickupTimes(ushort botNum);
ushort MP_GetRadarObjects(obj_tag *viewer, MP_RADAR_OBJECT **objects);
void MP_assassinReset(bool handOver);
void MP_Start(void);

// multiplayer_modes.cpp
void MP_KOHUpdate(obj_tag *hill);
// The original takes gameObj in EAX: MP_UplinkUpdate is the entry for that, _MP_UplinkUpdate the C++ under it.
void MP_UplinkUpdate(MPOBJECT *mpObj, obj_tag *gameObj);
void _MP_UplinkUpdate(MPOBJECT *mpObj, obj_tag *gameObj);
void MP_PlayerKilled(obj_tag *obj);

// multiplayer_flag.cpp
// The originals take gameObj in EAX (and MP_FlagUpdate mpObj in EDI): the plain names are the entries for that,
// the _ names the C++ under them.
void MP_FlagUpdate(obj_tag *gameObj, MPOBJECT *mpObj, bool dropped);
void _MP_FlagUpdate(obj_tag *gameObj, MPOBJECT *mpObj, bool dropped);
void MP_BluePrintUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj);
void _MP_BluePrintUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj);
// multiplayer_goldeneye.cpp
void MP_GoldeneyeResetObject(MPOBJECT *mpObj, obj_tag *gameObj, int unused, char state);
// The original takes gameObj in EAX: MP_GoldenEyeUpdate is the entry for that, _MP_GoldenEyeUpdate the C++ under it.
void MP_GoldenEyeUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj);
void _MP_GoldenEyeUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj);
// multiplayer_objects.cpp
void MP_ObjectUpdate(obj_tag *gameObj);
// The original takes defenders in AX and gameObj in EBX: MP_DemolitionProtectionUpdate is the entry for that,
// _MP_DemolitionProtectionUpdate the C++ under it.
void MP_DemolitionProtectionUpdate(MPOBJECT *mpObj, ushort defenders);
void _MP_DemolitionProtectionUpdate(MPOBJECT *mpObj, ushort defenders, obj_tag *gameObj);
void MP_BluePrintReachedBase(obj_tag *gameObj, MPOBJECT *mpObj);

// FIXME move to a separate file
bool build_PointOnFloor(cel_tag *cel, obj_tag* obj, _VECTOR *position, float distance, _VECTOR *searchDirection);
