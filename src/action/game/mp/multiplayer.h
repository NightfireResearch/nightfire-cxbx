#include <stddef.h>
#include "../../actionhelpers.h"
#include "../../math/math.h"


#define MAX_MP_AGENTS 8 // = 4 bots + 4 players? Or is it 7??


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

typedef enum  {
    PHOENIX = 0,
    MI6 = 1,
    NO_TEAM = 2,
    TEAM_FORCE_UINT32 = 0x7fffffff
} MPTeam;

#define TEAM_GET_NAME(team) ((team == PHOENIX) ? "PHOENIX" : ((team == MI6) ? "MI6" : ((team == NO_TEAM) ? "NO_TEAM" : "UNKNOWN")))


#pragma pack(push, 1)

// Should contain team, character ID, health bonus etc for each of 10 agents in the multiplayer game
typedef struct {
    char Name[32];
    MPTeam TeamId;
    undefined4 SkinNum; // Only set when the game actually launches, not set in menu
    undefined4 SomeField2;
    undefined4 HealthModifier; // Handicaps are -ve, boosts are +ve. Only applies to players. 
} MPSettings_PerPlayer;
static_assert(sizeof(MPSettings_PerPlayer) == 0x30, "MPSettings_PerPlayer is wrong size");

typedef struct { // on Xbox, starts at 0025fe38

    MPSettings_PerPlayer Player[10]; // Different on PS2 and Xbox.  1E0: Xbox

    undefined4 isMultiplayer; // on Xbox, at 00260018
    undefined4 maybeDroneAIEnabled;
    undefined4 Started;
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
    undefined4 RespawnSelectionMode;
    undefined4 ShowTeamAndNameOverhead;
    undefined4 LocationDamageEnabled;
    undefined4 MiniVehiclesEnabled;
    undefined4 GrappleEnabled;
    undefined4 ExplosiveSceneryEnabled;
    short numActivePickups;
    undefined field72_0x1da;
    undefined field73_0x1db;
} MPSettings_t;

static_assert(sizeof(MPSettings_t) == 572, "MPSettings_t is wrong size");

#define MPSettings (*((MPSettings_t*)0x0025fe38))

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

#define SpawnPoints ((MPSpawnPoint*)0x00261d58)

typedef struct {
  HASHCODE skinHashcode; // ?
  HASHCODE fileHashcode; // ?
  int handType;
  bool isInThisMpGame;
  char unknown2[3];
} MP_skin;

static_assert(sizeof(MP_skin) == 0x10, "MP_skin is wrong size");


typedef struct {
    char unknown[16];
    char SkinNum;
    char unknown2;
} MPBOT;

static_assert(sizeof(MPBOT) == 18, "MPBOT is wrong size");

typedef struct {
    char Enabled;
    char NumBots;
    MPBOT bot[6]; // FIXME: How many bots are there? Platform-specific? Enough memory for 10 on Xbox
} MPBOTS;

#pragma pack(pop)


void MP_setLoadingSkins(void);
bool MP_areObjectsOnSameTeam(obj_tag* a, obj_tag* b);
bool MP_isObjectOnTeam(obj_tag *param_1,uint teamId);
MPTeam MP_getObjectTeam(obj_tag* param_1);
bool MP_IsAssasin(obj_tag *param_1);
bool MP_IsTarget(obj_tag *param_1);
void MP_RegisterSpawnPoint(_VECTOR *position, _VECTOR *facingDirection, ushort teamId);


// FIXME move to a separate file
bool build_PointOnFloor(cel_tag *cel, obj_tag* obj, _VECTOR *position, float distance, _VECTOR *searchDirection);
