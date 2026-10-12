#ifndef DRIVING_GAME_MISSIONMANAGER_H_
#define DRIVING_GAME_MISSIONMANAGER_H_

// ---------------------------------------------------------------------------------------------------------------
// SMissionManager (0x1c70 bytes, one, glbMissionManager, from operator new): the mission's state. It holds the
// mission's rules (one list run every step, one per stage), the objectives list and the two objective slots the
// HUD points at, the player's health and shield, the shot, hit and kill counts, the magic counters and
// programmer-defined events the rules test, timed actions, cinematic mode, the drive-missile's steering, the
// section profiles, and the end-of-mission scores it tallies into the launch page. See MissionManager.cpp
// (state, objectives, rules, lifetime) and MissionScores.cpp (health, counters and the scores).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "MissionRules.h"               // SMissionTimer, kMissionTimers
#include "../../helpers.h"
#include "../data/Carp.h"               // CARP::AIElement
#include "../data/CoordConvert.h"       // Coord4
#include "../world/Targeting.h"         // PointerList

class ActionQueue;
struct LaunchPage;
class Missile;
class UData;

// ---- the lists

// std::list<T *> (Targeting.h's PointerList) with the two compiled members the linker left in this range; every
// list of pointers calls them.
struct MissionList : PointerList {
    // _Buynode() (Ghidra: FUN_000b8490): a head node linked to itself. A method of the list's, which does not
    // read `this`: every list of pointers calls it.
    static PointerListNode* BuyHead();                                                     // 0x000b8490
    // _Incsize, another copy of PointerList::IncreaseSize (Ghidra: FUN_000b9090).
    void IncreaseSize(uint32_t count);                                              // 0x000b9090

    // The constructor, as the manager's constructor writes it (and 0x0011cc10's)
    void Construct() {
        head = BuyHead();
        size = 0;
    }
    // push_back, as the compiled code inlines it
    void PushBack(void *value);
};

// ---- what the lists and slots hold

// An objective (0x14 bytes, operator new; EObjectiveAdd and EObjectiveInsert make them; name ours)
enum MissionObjectiveState : int32_t {
    kObjectiveIncomplete = 0,
    kObjectiveCurrent = 1,          // the first objective added is made current
    kObjectivePassed = 2,
    kObjectiveFailed = 3,
};

struct MissionObjective {
    int32_t id;                 // +0x00 what the Pass/Fail/Set events find it by
    int32_t unknown04;
    int32_t score;              // +0x08 added to the objectives score when it is passed
    int32_t unknown0c;
    int32_t state;              // +0x10 MissionObjectiveState
};
static_assert(sizeof(MissionObjective) == 0x14, "an objective is 0x14 bytes");

// A mission message (12 bytes, operator new; ESetMissionMessage's three words; name ours)
struct MissionMessage {
    int32_t unknown00;
    int32_t unknown04;
    int32_t unknown08;
};
static_assert(sizeof(MissionMessage) == 0xc, "a mission message is 12 bytes");

// What an objective slot points at, by its kind (objectiveType)
enum MissionObjectiveKind : int32_t {
    kObjectiveNone = 0,
    kObjectiveMarker = 1,           // ESetObjective's first reference: a position
    kObjectiveKind2 = 2,            // its second: kept, but GetCurrObjective answers NULL for it
    kObjectiveElement = 3,          // its third: an AI element, followed through AIElementController
};

// The first kind (provisional: only what the manager reads)
struct ObjectiveMarker {
    Coord4 position;            // +0x00
    float unknown10;
    float arrowHeight;          // +0x14 GetObjectiveArrowHeight's answer
};
struct ObjectiveKind2Target;

// ---- the mission set

// A mission set (0x2f8 bytes): the level's 'MissionSets' record, or the manager's default (DefaultMissionSet:
// "vanquish", 3000 health, 1000 shield). Name ours, after the tag. Per-difficulty arrays are indexed by the
// manager's difficulty: 1 and 2 their own entries, anything else entry 0.
struct MissionSet {
    char name[0x2c];            // +0x000 the car type (SetCarType)
    uint8_t weaponEnabled[32];  // +0x02c by weapon
    int32_t weaponAmmo[32];     // +0x04c loaded only up to 10000
    int32_t weaponClipSize[32]; // +0x0cc
    int32_t weaponDamage[32];   // +0x14c SWeaponManager::LoadWeapon's last argument
    uint8_t unknown1cc[0x80];
    // The best score of each kind (MissionScore), in this order
    int32_t objectivesValue;    // +0x24c
    int32_t stylinValue;        // +0x250
    int32_t accuracyValue;      // +0x254
    int32_t value4;             // +0x258 scored as 0
    int32_t healthValue;        // +0x25c
    int32_t shieldValue;        // +0x260
    int32_t timeValue;          // +0x264
    int32_t value3;             // +0x268
    int32_t damageValue;        // +0x26c
    int32_t value9;             // +0x270
    int32_t value10;            // +0x274
    int32_t killsValue;         // +0x278
    int32_t maxHealth;          // +0x27c
    int32_t health;             // +0x280 zero: 10000 health, no shield
    int32_t maxShield;          // +0x284
    int32_t shield;             // +0x288
    int32_t fullScoreTime;      // +0x28c seconds: the full time score up to here...
    int32_t zeroScoreTime;      // +0x290 ... falling to none here
    int32_t healthScale[3];     // +0x294 percent, by difficulty; entry 0 at or below zero: no scaling
    int32_t shieldScale[3];     // +0x2a0 percent, by difficulty
    int32_t enemyAccuracyBoost[3];  // +0x2ac
    float enemyGlueFactor[3];   // +0x2b8 Load makes all three 1 when the first is 0
    float missionText[3];       // +0x2c4 ShowMissionText's; the same
    int32_t unknown2d0[3];      // +0x2d0 copied to the launch page
    int32_t unknown2dc;
    int32_t unknown2e0;         // +0x2e0 over the shots fired: score 3's share
    int32_t unknown2e4;         // +0x2e4
    int32_t unknown2e8;         // +0x2e8
    int32_t healthPercentCap;   // +0x2ec the health percentage the launch page gets is at most this
    int32_t damageZeroScore;    // +0x2f0 damage from here on scores nothing...
    int32_t damageFullScore;    // +0x2f4 ... the full damage score down here
};
static_assert(sizeof(MissionSet) == 0x2f8, "a mission set is 0x2f8 bytes");
static_assert(offsetof(MissionSet, weaponAmmo) == 0x4c && offsetof(MissionSet, objectivesValue) == 0x24c &&
              offsetof(MissionSet, maxHealth) == 0x27c && offsetof(MissionSet, healthScale) == 0x294 &&
              offsetof(MissionSet, enemyGlueFactor) == 0x2b8 && offsetof(MissionSet, unknown2e0) == 0x2e0 &&
              offsetof(MissionSet, damageFullScore) == 0x2f4, "mission set layout");

// ---- the manager's records

// Two characters' records ResetCharInfo fills and nothing in the manager reads (name ours)
struct MissionCharInfo {
    int32_t id;                 // +0x00 1 and 2
    float unknown04;            // +0x04 0
    float unknown08;            // +0x08 1000
    float unknown0c;            // +0x0c 1000
    float unknown10;            // +0x10 0
    float unknown14;            // +0x14 1000
};
static_assert(sizeof(MissionCharInfo) == 0x18, "a character record is 0x18 bytes");

// A timed action (EStartTimedAction / EStopTimedAction): stylin points by how long it took - maxPoints up to
// fullPointsTime seconds, falling linearly to minPoints at timeLimit, minPoints after that. Name ours.
struct MissionTimedAction {
    uint8_t active;             // +0x00
    uint8_t unknown01[3];
    float timeLimit;            // +0x04 UpdateTimedActions completes it past this
    float fullPointsTime;       // +0x08
    int32_t minPoints;          // +0x0c
    int32_t maxPoints;          // +0x10
    int32_t startStep;          // +0x14
};
static_assert(sizeof(MissionTimedAction) == 0x18, "a timed action is 0x18 bytes");

// A section profile (EProfileMissionSection): the frame rate's extremes and sum while the section is current
struct MissionSectionProfile {
    char name[0x40];            // +0x00
    float minFramesPerSecond;   // +0x40 1000 when new
    float maxFramesPerSecond;   // +0x44
    float totalFramesPerSecond; // +0x48
    int32_t frames;             // +0x4c
};
static_assert(sizeof(MissionSectionProfile) == 0x50, "a section profile is 0x50 bytes");

// ---- the manager

enum MissionState : int32_t {
    kMissionNotStarted = 0,
    kMissionRunning = 1,            // the rules run
    kMissionStopped = 2,            // Stop's (the pause)
    kMissionWon = 3,
    kMissionLost = 4,               // Start and Stop leave a won or lost mission alone
};

// The scores (scores[], the launch page's arrays), by what makes them (names ours where the code does not say)
enum MissionScore : int32_t {
    kScoreObjectives = 0,       // passed objectives' scores
    kScoreStylin = 1,           // AddScore's and the timed actions'; each positive one is a "Stylin"
    kScoreAccuracy = 2,         // shots hit over shots fired
    kScore3 = 3,                // the set's unknown2e0 over shots fired
    kScore4 = 4,                // always 0
    kScoreHealth = 5,
    kScoreShield = 6,
    kScoreTime = 7,
    kScoreDamage = 8,
    kScore9 = 9,                // the mean of the accuracy and score 3 shares
    kScore10 = 10,              // not set here
    kScoreKills = 11,           // kills over unknown500
    kScoreCount = 12,
};

// The programmer-defined events the rules test (SRuleProg), by number; the manager raises a few itself
enum MissionEvent : int32_t {
    kEventCinematicSkipped = 0x03,  // SkipCinematic's, and a camera-change action during a cinematic
    kEventObjectiveNotSeen = 0x04,  // the objective out of radar range and behind for five seconds
    kEvent05 = 0x05,                // Load's, for the launch page's unknowna3c 3 or 4
    kEvent0C = 0x0c,                // magic counter 12 at 1 (raised by IncMagicCounter)
    kEvent0D = 0x0d,                // ... at 2
    kEvent0E = 0x0e,                // magic counter 14 at 1
    kEvent0F = 0x0f,                // ... at 2
    kEvent10 = 0x10,                // ... at 3
    kEventSuperEasy = 0x18,         // gSuperEasy
    kEventCount = 64,
};

constexpr int kMissionStages = 64;
constexpr int kMagicCounters = 64;
constexpr int kObjectiveSlots = 2;
constexpr int kTimedActions = 8;
constexpr int kSectionProfiles = 64;
constexpr int kMaxObjectives = 7;

class SMissionManager {
public:
    MissionList objectives;             // +0x000 MissionObjective *
    MissionList messages;               // +0x00c MissionMessage *
    int32_t stageCount;                 // +0x018 one past the highest stage a rule is in
    int32_t currentStage;               // +0x01c
    int32_t nextStage;                  // +0x020 ChangeStage's; current from the next step
    UData *rules;                       // +0x024 Load's
    UData *missionSets;                 // +0x028
    MissionSet *missionSet;             // +0x02c
    MissionList globalRules;            // +0x030 SMissionRule *, run every step
    MissionList stageRules[kMissionStages];     // +0x03c ... run while their stage is current
    uint8_t unknown33c[0x24];
    SMissionTimer timers[kMissionTimers];       // +0x360
    int32_t unknown428;                 // +0x428 -1 from construction
    uint8_t unknown42c[8];
    uint8_t events[kEventCount];        // +0x434 programmer-defined events (MissionEvent), also raised by counters
    int32_t missionState;               // +0x474 MissionState: won or lost, no targeting and no firing; lost, no
                                        //        auto-drive camera input
    uint8_t autoDrive;                  // +0x478 SetAutoDrive's: the hands fire the weapons; ResolveCollision weighs
                                        //        two cars by car class and hit points
    uint8_t missileHasTarget;           // +0x479 the drive-missile's lock (auto-drive lock actions)
    uint8_t unknown47a[2];
    Missile *driveMissile;              // +0x47c the missile the player steers, NULL for none; while there is one,
                                        //        no auto-drive camera rotation, spin, zoom or aiming
    float steerX;                       // +0x480 the two horizontal and two vertical steering inputs: the
    float steerY;                       // +0x484 larger of each pair steers the missile
    float steerX2;                      // +0x488
    float steerY2;                      // +0x48c
    ActionQueue *actionQueue;           // +0x490 "MissionManQ"
    int32_t unknown494;                 // +0x494 ResetCharInfo: 0
    int32_t unknown498;                 // +0x498 ResetCharInfo: 100
    float playerHealth;                 // +0x49c the player's car's hit point location
    float playerMaxHealth;              // +0x4a0
    float playerShield;                 // +0x4a4 its shield point location (InitPlayerCar)
    float playerMaxShield;              // +0x4a8
    MissionCharInfo charInfo[2];        // +0x4ac
    int32_t difficulty;                 // +0x4dc the launch page's, 0..2 (the auto-drive camera's auto-aim limits
                                        //        are 3 - it thirds; the scaling)
    int32_t shotsFired;                 // +0x4e0
    int32_t shotsHit;                   // +0x4e4
    int32_t kills;                      // +0x4e8 never below 0
    float damage;                       // +0x4ec taken by the player outside cinematics
    int32_t cinematicMode;              // +0x4f0 a mask of cinematic sources; non-zero: no targeting, no firing,
                                        //        no auto-drive camera spin, no damage counted
    uint8_t skippingCinematic;          // +0x4f4 SkipCinematic's fade to black is under way
    uint8_t unknown4f5[3];
    int32_t objectiveSeenStep;          // +0x4f8 the step the objective was last near or ahead
    int32_t unknown4fc;                 // +0x4fc
    int32_t unknown500;                 // +0x500 kills over it make the kills score; one more for each scoreable
                                        //        sentry made
    int32_t lastDamageStep;             // +0x504
    int32_t magicCounters[kMagicCounters];      // +0x508
    int32_t magicThresholds[kMagicCounters];    // +0x608 a counter reaching its threshold raises its event; -1 never
    int32_t stylinCount;                // +0x708
    int32_t missionEndTime;             // +0x70c seconds, set when the mission is won
    uint8_t superEasy;                  // +0x710 gSuperEasy; set: the sniper rifle fires only while the camera is
                                        //        locked on; zero: the auto-drive camera's lock-on input sets
                                        //        CameraLockOnFlag
    uint8_t unknown711[3];
    int32_t lastShotStep;               // +0x714
    int32_t loadCount;                  // +0x718 Load's; from the second load, at least half health
    union {                             // +0x71c by objectiveType
        const void *objectiveTarget[kObjectiveSlots];
        const ObjectiveMarker *objectiveMarker[kObjectiveSlots];
        CARP::AIElement *objectiveElement[kObjectiveSlots];
    };
    int32_t objectiveType[kObjectiveSlots];     // +0x724 MissionObjectiveKind
    uint32_t objectiveComment[kObjectiveSlots]; // +0x72c
    uint8_t objectiveVisible[kObjectiveSlots];  // +0x734 rechecked every 10 steps
    uint8_t unknown736[2];
    int32_t startScores[kScoreCount];   // +0x738 the launch page's accumulated scores at construction
    int32_t scores[kScoreCount];        // +0x768 MissionScore
    int32_t objectiveCheckStep;         // +0x798 when objectiveVisible is next rechecked
    MissionTimedAction timedActions[kTimedActions];         // +0x79c
    MissionSectionProfile sections[kSectionProfiles];       // +0x85c
    int32_t sectionCount;               // +0x1c5c
    int32_t currentSection;             // +0x1c60 -1 for none
    const char *carType;                // +0x1c64
    uint8_t unknown1c68[8];

    // ---- lifetime (MissionManager.cpp)
    SMissionManager* Construct();                                                       // 0x000b8c30
    void Destruct();                                                                    // 0x000b8e70
    // glbMissionManager made and constructed, and destructed and deleted (Ghidra: SMissionManager::Construct
    // and SMissionManager::Destruct; the names here are ours, beside the constructor's)
    static void Create();                                                               // 0x000b8ff0
    static void Destroy();                                                              // 0x000b9060
    // The track's rules and mission set
    void Load(UData *rules, UData *missionSets);                                        // 0x000b9140
    void Reset();                                                                       // 0x000b8bf0
    void ClearState();                                                                  // 0x000b8a30
    void DestroyRuleList(MissionList *rules);                                           // 0x000b8900
    void ClearObjectiveList();                                                          // 0x000b8960
    void ClearMessageList();                                                            // 0x000b89c0

    // ---- state and the rules
    void SetCarType();                                                                  // 0x000b60a0
    const char* GetCarType();                                                           // 0x000b6140
    void LoadWeaponSet();                                                               // 0x000b6160
    void Start();                                                                       // 0x000b6380
    void Stop();                                                                        // 0x000b63a0
    void ChangeStage(int stage);                                                        // 0x000b64b0
    void SetAutoDrive(bool on, bool changeControls);                                    // 0x000b64c0
    // Both answer 0 or 1 in the whole of EAX
    int GoingToSkipCinematic();                                                         // 0x000b6590
    int IsSkippingCinematic();                                                          // 0x000b65b0
    void ClearCinematicMode(int mask);                                                  // 0x000b7950
    void SkipCinematic();                                                               // 0x000b79d0
    void SetCinematicMode(int mask);                                                    // 0x000b8160
    void ProgrammerDefinedEvent(int event, const char *text);                           // 0x000b72f0
    bool CheckProgrammerEvent(int event);                                               // 0x000b7300
    void ClearProgrammerEvent(int event);                                               // 0x000b7310
    void StartSectionProfile(const char *name);                                         // 0x000b7320
    void UpdateAutoDriveMissile();                                                      // 0x000b7ae0
    void InitPlayerCar();                                                               // 0x000b8090
    void SetDriveMissile(Missile *missile);                                             // 0x000b80b0
    void Win();                                                                         // 0x000b84b0
    void Lose();                                                                        // 0x000b8580
    void CallStage(int stage);                                                          // 0x000b85d0
    void ProcessRules();                                                                // 0x000b8610
    void Update();                                                                      // 0x000b8680

    // ---- objectives
    void SetObjectiveComment(int slot, uint32_t comment);                               // 0x000b6c30
    void ClearObjective(int slot);                                                      // 0x000b6c50
    const Coord4* GetCurrObjective(int slot);                                           // 0x000b6c70
    uint32_t GetObjectiveComment(int slot);                                             // 0x000b6cc0
    double GetObjectiveArrowHeight(int slot);                                           // 0x000b6cd0
    bool IsObjectiveVisibleToUser(int slot);                                            // 0x000b6d40
    bool CheckObjectiveVisibleToUser(int slot);                                         // 0x000b6d50
    void SetObjective(int slot, const ObjectiveMarker *marker);                         // 0x000b7f60
    void SetObjective(int slot, const ObjectiveKind2Target *target);                    // 0x000b7fb0
    void SetObjective(int slot, CARP::AIElement *element);                              // 0x000b8000
    void UpdateObjectiveSpecial();                                                      // 0x000b81a0
    MissionObjective* GetObjectiveByIndex(int index);                                   // 0x000b8460
    void PassObjective(int id);                                                         // 0x000b87f0
    void FailObjective(int id);                                                         // 0x000b8840
    void SetCurrentObjective(int id);                                                   // 0x000b8880
    void SetIncompleteObjective(int id);                                                // 0x000b88c0
    // EObjectiveAdd's, EObjectiveInsert's (before the objective `beforeId`) and ESetMissionMessage's (Ghidra:
    // FUN_000b9320, FUN_000b93c0, FUN_000b94a0; names ours)
    void AddObjective(int id, int unknown04, int score, int unknown0c);                 // 0x000b9320
    void InsertObjective(int beforeId, int id, int unknown04, int score, int unknown0c);    // 0x000b93c0
    void AddMessage(int unknown00, int unknown04, int unknown08);                       // 0x000b94a0

    // ---- health, counters and the scores (MissionScores.cpp)
    void RestoreLives();                                                                // 0x000b63c0
    void ClearScoreBuffer(LaunchPage *page);                                            // 0x000b63f0
    void BoostPlayerHealth(float fraction);                                             // 0x000b65f0
    void BoostPlayerShield(float fraction);                                             // 0x000b6640
    // A cheat's: shield and its maximum the set's maximum times `times` (Ghidra: FUN_000b6690; name ours)
    void SetPlayerShieldTimes(int times);                                               // 0x000b6690
    void IncPlayerDamage(float damage);                                                 // 0x000b66c0
    void IncShotsFired(int shots);                                                      // 0x000b66f0
    void IncShotsHit(bool hit);                                                         // 0x000b6720
    void IncKills(int kills);                                                           // 0x000b6770
    int GetMagicCounter(int counter);                                                   // 0x000b67a0
    void SetMagicCounterThreshold(int counter, int threshold);                          // 0x000b67b0
    void IncMagicCounter(int counter);                                                  // 0x000b7a40
    void SetMagicCounter(int counter, int value);                                       // 0x000b7ac0
    // Health, or its fraction of the maximum (left unrounded)
    double GetPlayerHealth(bool fraction);                                              // 0x000b67d0
    void ResetCharInfo();                                                               // 0x000b67f0
    void UpdateCharInfo();                                                              // 0x000b6b20
    void StartTimedAction(int action, float fullPointsTime, float timeLimit, int minPoints, int maxPoints);  // 0x000b6bd0
    void CompleteTimedAction(int action);                                               // 0x000b7de0
    void UpdateTimedActions();                                                          // 0x000b7f10
    void AddScore(int score, int points);                                               // 0x000b6e30
    void CalcEndMissionScores();                                                        // 0x000b6e80
    int GetEnemyAccuracyBoost();                                                        // 0x000b7260
    float GetEnemyGlueFactor();                                                         // 0x000b7290
    float ShowMissionText();                                                            // 0x000b72c0
    // The scores into the launch page; with `endOfMission`, only for a won mission, and its scores also rounded
    // and accumulated
    void TallyScores(LaunchPage *page, bool endOfMission);                              // 0x000b7470
};
static_assert(sizeof(SMissionManager) == 0x1c70, "SMissionManager is 0x1c70 bytes");
static_assert(offsetof(SMissionManager, stageRules) == 0x3c && offsetof(SMissionManager, timers) == 0x360 &&
              offsetof(SMissionManager, events) == 0x434 && offsetof(SMissionManager, missionState) == 0x474 &&
              offsetof(SMissionManager, autoDrive) == 0x478 && offsetof(SMissionManager, driveMissile) == 0x47c &&
              offsetof(SMissionManager, actionQueue) == 0x490 && offsetof(SMissionManager, playerHealth) == 0x49c &&
              offsetof(SMissionManager, difficulty) == 0x4dc && offsetof(SMissionManager, cinematicMode) == 0x4f0 &&
              offsetof(SMissionManager, magicCounters) == 0x508 && offsetof(SMissionManager, stylinCount) == 0x708 &&
              offsetof(SMissionManager, superEasy) == 0x710 && offsetof(SMissionManager, objectiveTarget) == 0x71c &&
              offsetof(SMissionManager, startScores) == 0x738 && offsetof(SMissionManager, scores) == 0x768 &&
              offsetof(SMissionManager, timedActions) == 0x79c && offsetof(SMissionManager, sections) == 0x85c &&
              offsetof(SMissionManager, sectionCount) == 0x1c5c && offsetof(SMissionManager, carType) == 0x1c64,
              "SMissionManager layout");

#define glbMissionManager (*(SMissionManager **)0x00239220)
// Set nowhere in the game; read by EPlayerLose, SWeaponManager::FireWeapon and ResetCharInfo
#define gPlayerCanNeverLose U8_AT(0x00239218)

extern MissionSet DefaultMissionSet;    // the mission set without a level's
extern uint8_t MissionHealthTimes100;  // MissionManager.cpp
extern int32_t MissionCinematicMode;   // the cinematic mask as the manager last published it

#endif // DRIVING_GAME_MISSIONMANAGER_H_
