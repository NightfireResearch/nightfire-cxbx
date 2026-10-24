#ifndef MUSIC_H_
#define MUSIC_H_

#include "../actionhelpers.h"

void Music_Event(uint evtId,undefined4 val);
uint32_t __cdecl Get_Music_Event(int evtId);
void MusicFadeDown(void);
void UpdateMusicalEvents(void);
int Music_PlayingCombat(void);
int Music_RequestedCombat(void);
int Music_PlayingStealth(void);
int Music_RequestedStealth(void);
void Music_TrackNis(void);
void Music_SetAIVars(HASHCODE level);
void PickRandomStealth(void);
void PickRandomCombat(int instant);
int Music_MissionEnded(void);
void UpdateMFX_02MayhewA(void);
void UpdateMFX_02MayhewB(void);
void UpdateMFX_02MayhewC(void);
void UpdateMFX_MayhewD(void);
void UpdateMFX_01CastleA(void);
void UpdateMFX_01CastleB(void);
void UpdateMFX_01CastleC(void);
void UpdateMFX_01CastleD(void);
void UpdateMFX_03TowerA(void);
void UpdateMFX_03TowerB(void);
void UpdateMFX_03TowerC(void);
void UpdateMFX_PowerStationA1(void);
void UpdateMFX_PowerStationA2(void);
void UpdateMFX_HT_Level_Tower2A(void);
void UpdateMFX_HT_Level_Tower2B(void);
void UpdateMFX_HT_Level_Tower2Elevator(void);
void UpdateMFX_HT_Level_Tower2C(void);
void UpdateMFX_HT_Level_EvilBase(void);
void UpdateMFX_HT_Level_EvilSilo(void);
void UpdateMFX_HT_Level_EvilBaseC(void);
void UpdateMFX_HT_Level_SpaceStationD(void);

// The music events the code gives a meaning (Music_Event's ids); the others are the cels' (cel_tag::musicEvent)
enum MusicEvent {
    MUSICEV_LEVEL = 0,          // the level's hashcode
    MUSICEV_NEW_LEVEL = 1,      // with MUSICEV_LEVEL: start its track
    MUSICEV_ALERT = 2,          // 2: the drones' alert; another value counts towards the combat music
    MUSICEV_END_B = 3,          // with MUSICEV_END: the mission's end
    MUSICEV_NIS_DONE = 5,
    MUSICEV_DEATH = 6,          // the row's deathSection
    MUSICEV_VICTORY = 7,        // the row's victorySection
    MUSICEV_END = 8,
    MUSICEV_COMBAT = 10,        // the drones' combat threshold drops to 1
    MUSICEV_NIS_STARTED = 11,   // the cut scene's hashcode
    MUSICEV_NIS_ENDED = 12,     // the same
    MUSICEV_TRAVERSE = 13,      // the player is on a rope or creep wall (Player_Wire, Player_Creep)
    MUSICEV_DRONES_CALM = 14,   // a drone is calm, and none near is alerted
    MUSICEV_ATTACK_NOW = 15,    // with DRONE_MSG_AttackNow
    MUSICEV_HOSTAGE_SAVED = 16,
};

#pragma pack(push, 1)

// The music's view of the fighting (0x001ee260). NDrone2_SeenAndAttacking fills it in from the drones; the music
// code (UpdateMusicalEvents and the level functions) sets the thresholds and reads it.
typedef struct MusicVars_t {
    ushort combatThreshold;     // 0x00 - attackers it takes to start the combat music
    ushort calmThreshold;       // 0x02 - Drone_InitComms ends the combat music when no more drones than this count
    uint seenSeconds;           // 0x04 - a drone counts while it saw its opponent this recently
    float closeRange;           // 0x08 - or, once the combat music is on, while it is this close
    ushort combat;              // 0x0c - the combat music is on
    ushort attackers;           // 0x0e - drones counted
    uint combatFrame;           // 0x10 - GameState.NumFramesUnpaused when it was last started
    float nearestOpponent;      // 0x14 - the least distance to an opponent, of every drone
    float nearestSeenOpponent;  // 0x18 - the same, of drones that see theirs
    uint lastSeenFrame;         // 0x1c - the latest a drone saw its opponent
    uint countingStarted;       // 0x20 - 0: NDrone2_SeenAndAttacking starts its list of counted drones afresh
} MusicVars_t;

#define MUSIC_LIST_LENGTH 5

// A level's music (MapMusic): its track, its sections and the lists the level functions pick from (-1 ends one)
typedef struct MUSICDATA {
    HASHCODE level;
    int track;
    int startSection;
    int deathSection;           // 0x0c
    int victorySection;         // 0x10
    int endFromCombat;          // 0x14 - MusicFadeDown's, with a combat section playing
    int endSection;             // 0x18 - and otherwise
    int alertCount;             // 0x1c - frames of alert before the combat music
    int attackCount;            // 0x20 - frames of calm before it ends
    int combat[MUSIC_LIST_LENGTH];   // 0x24
    int stealth[MUSIC_LIST_LENGTH];  // 0x38
} MUSICDATA;

// A level's MusicVars thresholds (MapDroneData)
typedef struct MUSICAIVARS {
    HASHCODE level;
    ushort combatThreshold;
    ushort calmThreshold;
    uint seenSeconds;
    float closeRange;
} MUSICAIVARS;

#pragma pack(pop)

static_assert(sizeof(MUSICDATA) == 0x4c, "MUSICDATA is 0x4c bytes");
static_assert(offsetof(MUSICDATA, stealth) == 0x38, "Bad offset of MUSICDATA.stealth");
static_assert(sizeof(MUSICAIVARS) == 0x10, "MUSICAIVARS is 0x10 bytes");
static_assert(offsetof(MusicVars_t, countingStarted) == 0x20, "Wrong offset for countingStarted");

// The frame's events, by id (Music_Event)
extern uint32_t MusicEventList[0x40];

extern MusicVars_t MusicVars;

extern MUSICDATA MapMusic[35];
extern MUSICAIVARS MapDroneData[12];

#endif // MUSIC_H_
