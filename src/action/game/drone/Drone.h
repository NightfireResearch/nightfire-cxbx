#ifndef DRONE_H_
#define DRONE_H_

#include "../../actionhelpers.h"
#include "../../engine/AINetwork.h"

#pragma pack(push, 1)

// The 33 keys of a drone placement in the level (level_tag+0x2c; names from level_tag_Drone and
// docs/drone/architecture/README.md 4.1, which also says where each lands in Drone_tag).
typedef struct DroneKeys {
    HASHCODE skin;                  // 0x00 key 0
    HASHCODE playScript;            // 0x04 key 1 - 0x06000000 for none
    int minDifficulty;              // 0x08 key 2 - only Drone_Create reads it
    uint waitSwitchChannel;         // 0x0c key 3
    uint group;                     // 0x10 key 4
    uint mode;                      // 0x14 key 5 - a DMODE below 0x24, else behaviour-driven (100 = has a second behaviour)
    uint modeChangeSwitchChannel;   // 0x18 key 6
    uint key7;                      // 0x1c key 7 - copied to Drone_tag+0x12c, meaning unknown
    uint secondaryMode;             // 0x20 key 8
    uint weapon;                    // 0x24 key 9
    uint drawWeapon;                // 0x28 key 10
    uint grenades;                  // 0x2c key 11
    uint deathSwitchChannel;        // 0x30 key 12
    HASHCODE deathScript;           // 0x34 key 13
    uint combatRange;               // 0x38 key 14
    uint behaviour[11];             // 0x3c keys 15-25 - the behaviour block (behaviour_util_get; docs/drone/behaviours)
    uint statsOverride[7];          // 0x68 keys 26-32 - the packed stats records
} DroneKeys;

// A drone's state machine (Drone_tag+0xec). See docs/drone/architecture/README.md 3.1.
typedef struct StateMachineInfo_tag {
    uint id;                    // 0x00 - unique per drone (++NPCGlobals.NumDrones): the address messages use
    uint curState;              // 0x04 - the DSTATE being run
    uint prevState;             // 0x08
    uint nextState;             // 0x0c - requested; only Drone_SM_SetState writes it
    uint resumeState;           // 0x10 - where to return after an impact or stun
    uint stateEnteredFrame;     // 0x14 - GameState.NumFramesUnpaused at Enter
    uchar changePending;        // 0x18
    uchar pad19[3];
    uint scratch1c;             // 0x1c - per-state timers and counters
    uint scratch20;             // 0x20 - per-state (the AllyLead states)
    int stateParam;             // 0x24 - Drone_SM_SetState's third argument
} StateMachineInfo_tag;

typedef struct DCVars_tag {
    obj_tag* gameObj;
    Drone_tag* drone;
    cel_tag* cel;
    StateMachineInfo_tag* aiStateMachine;   // &drone->sm
} DCVars_tag;

typedef struct DIVars_tag {
    obj_tag* gameObj;       // 0x00 - the created object (set by NDrone2_CreateFromDIVars)
    _VECTOR position;       // 0x04
    _VECTOR rotation;       // 0x10 - only .y is used
    int glist;              // 0x1c - celglist for the unused MINISUB path; always 0
    DroneKeys keys;         // 0x20
} DIVars_tag;

// Drone_tag.sightFlags
#define DRONE_SIGHT_SEES_OPPONENT 4     // registered the opponent after the reaction time (DroneVision_HaveOpponentSight)
#define DRONE_SIGHT_REACTED 8           // has reacted to a first sighting: DroneFunc_ReactionTime is 0 from then on

// One drone (the obj_tag's extraObjectData), 0x978 bytes. Only the fields docs/drone/architecture/README.md 4.3 is
// sure of are named; A3-drone-fields.md lists every offset the code touches.
typedef struct Drone_tag {
    Drone_tag *prev;                // 0x000 - NPCGlobals.NDrone2List
    Drone_tag *next;                // 0x004
    Drone_tag *groupNext;           // 0x008 - NDrone2_SetupGroups
    obj_tag *gameObj;               // 0x00c
    void *bodyGlow;                 // 0x010
    uchar skinFlags[8];             // 0x014 - +0x18 ninja eyes, +0x19 captain, +0x1b impact immunity (inferred)
    char _pad1c[0x23 - 0x1c];
    uchar flies;                    // 0x023 - abseil / astronaut movement (inferred)
    char _pad24[0x29 - 0x24];
    uchar usingSecondBehaviour;     // 0x029
    char _pad2a[0x31 - 0x2a];
    uchar modeFlag31;               // 0x031
    uchar modeFromOldTable;         // 0x032 - DoModeSettingsOLD ran: DefaultInit then runs DoTypeSettingsOLD
    char _pad33[0x3c - 0x33];
    uchar unknown3c;                // 0x03c - DroneWeap_Fire clears it
    char _pad3d[0x44 - 0x3d];
    uchar side;                     // 0x044 - 1 enemy, 2 ally, 3 civilian / neutral
    char _pad45[0x90 - 0x45];
    float health;                   // 0x090
    float startHealth;              // 0x094
    uchar accuracy;                 // 0x098 - the behaviour / bot stats (lower is better)
    uchar aggression;               // 0x099
    uchar speed;                    // 0x09a
    uchar stat9b;                   // 0x09b
    uchar reaction;                 // 0x09c
    uchar recover;                  // 0x09d
    uchar badSide;                  // 0x09e - bots: BOT_stats_t.isBad
    uchar armour;                   // 0x09f - only ever written 0 on the Xbox
    char _pada0[0xa4 - 0xa0];
    float accuracyF;                // 0x0a4 - the accuracy as a float (BOT_setDroneStats)
    uchar dtypeBase;                // 0x0a8 - GetDTYPENEW(class, 0)
    uchar dtype;                    // 0x0a9 - the active DTYPE: indexes DroneTypeSettings; 30 = bot
    uchar dtype2;                   // 0x0aa - the DTYPE after the switch to the second behaviour
    uchar behaviourClass;           // 0x0ab - 0-9 soldier, 10/11/13/14 civilian kinds, 12 ally, 15 ninja, 16 bot
    uchar behaviour1Mode;           // 0x0ac
    uchar behaviour2Mode;           // 0x0ad
    char _padae[0xb0 - 0xae];
    uint initialisedOnFrame;        // 0x0b0
    char _padb4[0xbc - 0xb4];
    short skinClass;                // 0x0bc
    short animSet;                  // 0x0be - the weapon stance table index (inferred)
    short savedAnimSet;             // 0x0c0
    char _padc2[0xc4 - 0xc2];
    HASHCODE skinHashcode;          // 0x0c4
    float combatRanges[7];          // 0x0c8 - from key 14
    float damageScale;              // 0x0e4
    uint stateTimeoutFrame;         // 0x0e8 - NDrone2_SetIdleTimeOut; PreDroneControl sends message 4 when it passes
    StateMachineInfo_tag sm;        // 0x0ec
    void *processFunction;          // 0x114 - NDrone2_ProcessStateMachine
    uchar waitSwitchChannel;        // 0x118 - key 3
    uchar modeChangeSwitchChannel;  // 0x119 - key 6 (also the channel DroneFunc_HostageSaved sets)
    uchar waitSwitchInitial;        // 0x11a - the channels' states at init
    uchar modeChangeSwitchInitial;  // 0x11b
    short mode;                     // 0x11c - key 5
    short secondaryMode;            // 0x11e - key 8
    uchar deathSwitchChannel;       // 0x120 - key 12
    char _pad121[0x124 - 0x121];
    int groupAfterSetup;            // 0x124
    int group;                      // 0x128 - key 4
    uint key7;                      // 0x12c
    float stateAnimSpeed;           // 0x130 - reset to 1.0 on every state change (inferred: anim speed)
    char _pad134[0x138 - 0x134];
    float playerDistance[4];        // 0x138 - PreDroneControl
    obj_tag *opponent;              // 0x148
    uchar targetSlot;               // 0x14c - NDrone2_SetOpponent's target slot, 0..7, 0xff none
    char _pad14d[0x168 - 0x14d];
    float distanceToTarget;         // 0x168 - to the opponent (Drone_GetOpponentInfo)
    char _pad16c[0x1b4 - 0x16c];
    _VECTOR maybeVectorToOpponent;  // 0x1b4 (Ghidra's name)
    char _pad1c0[0x1c8 - 0x1c0];
    uint sightFlags;                // 0x1c8 - DRONE_SIGHT_*: 4 sees its opponent (DroneVision_HaveOpponentSight), 8 has
                                    //         reacted to its first sighting (set with message 0xf; also by HostageIdle)
    char _pad1cc[0x204 - 0x1cc];
    uint framesSinceSeen;           // 0x204 - frames since the opponent was last seen
    char _pad208[0x240 - 0x208];
    Drone_tag *opponentScanResume;  // 0x240 - allies: where NDrone2_FindOpponent's walk of the drone list goes on
                                    //         from (our name)
    char _pad244[0x3d4 - 0x244];
    uint *currentBehaviour;         // 0x3d4 - behaviour1 or behaviour2
    uint behaviour1[3];             // 0x3d8 - behaviour property words (behaviour_util_*)
    uint behaviour2[3];             // 0x3e4
    char _pad3f0[0x3f4 - 0x3f0];
    uint flags;                     // 0x3f4 - 0x100 AI running, 0x200 dead, 0x400 dying, 0x10 frozen ... (README 4.3)
    uint flags2;                    // 0x3f8
    float baseAlertness1;           // 0x3fc
    float baseAlertness2;           // 0x400
    float prevAlertness;            // 0x404
    float alertness;                // 0x408 - 1.0 = starts alerted
    char _pad40c[0x418 - 0x40c];
    uchar alertStatus;              // 0x418 - Drone_AlertStatusSet
    uchar prevAlertStatus;          // 0x419 - the status before the last change
    uchar alertStatusChanged;       // 0x41a - set to 1 on a change (only Drone_AlertStatusSet writes it)
    char _pad41b;
    uint alertStatusChangedFrame;   // 0x41c - GameState.NumFramesUnpaused at the last change
    char _pad420[0x440 - 0x420];
    void *animCallback1;            // 0x440
    void *animCallback2;            // 0x444
    char _pad448[0x450 - 0x448];
    HASHCODE playScript;            // 0x450 - key 1
    char _pad454[0x492 - 0x454];
    short animEndState;             // 0x492 - DroneAnim_CallAnim's end state and message
    uint animEndMsg;                // 0x494
    char _pad498[0x49e - 0x498];
    short initialState;             // 0x49e - DroneTypeSettings; DSTATE_Global's Enter goes there
    short secondState;              // 0x4a0 - the state after NDrone2_ChangeToAttackMode
    char _pad4a2[0x564 - 0x4a2];
    AIPoint_tag aiPoint;            // 0x564
    char _pad58c[0x634 - 0x58c];
    AIRoute_tag route1;             // 0x634 - the dynamic route
    char _pad6d4[0x6d8 - 0x6d4];
    AIRoute_tag route2;             // 0x6d8 - the patrol / mission route
    char _pad778[0x874 - 0x778];
    DIVars_tag diVars;              // 0x874 - the drone's own copy of its creation vars
    uint timerA[3];                 // 0x918 - both timers are cleared on every state change
    uint timerB[3];                 // 0x924
    uint stateScratch;              // 0x930 - anim id, flags; read as a state number by DSTATE_Disabled
    char _pad934[0x974 - 0x934];
    struct BOT_vars_t *botVars;     // 0x974 - multiplayer bots only
} Drone_tag;

// The level_tag Drone_Create is given: the object header, then the drone's keys
typedef struct DroneCreationData {
    char baseObj[0x2c];
    DroneKeys keys;
} DroneCreationData;

// A state machine message (0x1c bytes). See docs/drone/architecture/README.md 3.2.
typedef struct MsgObject {
    uint msgType;           // 0x00 - a DRONE_MSG
    uint scope;             // 0x04 - 0 any state, 0xc5 BotGlobal, else only delivered in that state
    uint sender;            // 0x08 - sender's StateMachineInfo_tag.id
    int receiver;           // 0x0c - 0 broadcast, > 0 an id, < 0 multiplayer bot -1-n
    uint createdFrame;      // 0x10
    uint handleOnFrame;     // 0x14 - later than now: queued in the delayed list
    void* extraData;        // 0x18 - payload (hit record, object, state number...)
} MsgObject;

#pragma pack(pop)

static_assert(sizeof(DroneKeys) == 0x84, "DroneKeys is 33 keys");
static_assert(offsetof(DroneKeys, behaviour) == 0x3c, "DroneKeys.behaviour is key 15");
static_assert(sizeof(StateMachineInfo_tag) == 0x28, "StateMachineInfo_tag is 0x28 bytes");
static_assert(offsetof(StateMachineInfo_tag, stateParam) == 0x24, "Wrong offset for stateParam");
static_assert(sizeof(DIVars_tag) == 0xa4, "DIVars is wrong size");
static_assert(sizeof(DCVars_tag) == 0x10, "DCVars is wrong size");
static_assert(sizeof(MsgObject) == 0x1c, "MsgObject is 0x1c bytes");
static_assert(sizeof(DroneCreationData) == 0xb0, "DroneCreationData is 0xb0 bytes");
static_assert(sizeof(Drone_tag) == 0x978, "Drone_tag is 0x978 bytes");
static_assert(offsetof(Drone_tag, unknown3c) == 0x3c, "Wrong offset for unknown3c");
static_assert(offsetof(Drone_tag, side) == 0x44, "Wrong offset for side");
static_assert(offsetof(Drone_tag, health) == 0x90, "Wrong offset for health");
static_assert(offsetof(Drone_tag, dtype) == 0xa9, "Wrong offset for dtype");
static_assert(offsetof(Drone_tag, skinHashcode) == 0xc4, "Wrong offset for skinHashcode");
static_assert(offsetof(Drone_tag, stateTimeoutFrame) == 0xe8, "Wrong offset for stateTimeoutFrame");
static_assert(offsetof(Drone_tag, sm) == 0xec, "Wrong offset for sm");
static_assert(offsetof(Drone_tag, processFunction) == 0x114, "Wrong offset for processFunction");
static_assert(offsetof(Drone_tag, modeChangeSwitchChannel) == 0x119, "Wrong offset for modeChangeSwitchChannel");
static_assert(offsetof(Drone_tag, key7) == 0x12c, "Wrong offset for key7");
static_assert(offsetof(Drone_tag, opponent) == 0x148, "Wrong offset for opponent");
static_assert(offsetof(Drone_tag, distanceToTarget) == 0x168, "Wrong offset for distanceToTarget");
static_assert(offsetof(Drone_tag, maybeVectorToOpponent) == 0x1b4, "Wrong offset for maybeVectorToOpponent");
static_assert(offsetof(Drone_tag, framesSinceSeen) == 0x204, "Wrong offset for framesSinceSeen");
static_assert(offsetof(Drone_tag, opponentScanResume) == 0x240, "Wrong offset for opponentScanResume");
static_assert(offsetof(Drone_tag, accuracy) == 0x98, "Wrong offset for accuracy");
static_assert(offsetof(Drone_tag, speed) == 0x9a, "Wrong offset for speed");
static_assert(offsetof(Drone_tag, currentBehaviour) == 0x3d4, "Wrong offset for currentBehaviour");
static_assert(offsetof(Drone_tag, flags) == 0x3f4, "Wrong offset for flags");
static_assert(offsetof(Drone_tag, alertness) == 0x408, "Wrong offset for alertness");
static_assert(offsetof(Drone_tag, alertStatus) == 0x418, "Wrong offset for alertStatus");
static_assert(offsetof(Drone_tag, sightFlags) == 0x1c8, "Wrong offset for sightFlags");
static_assert(offsetof(Drone_tag, prevAlertStatus) == 0x419, "Wrong offset for prevAlertStatus");
static_assert(offsetof(Drone_tag, alertStatusChanged) == 0x41a, "Wrong offset for alertStatusChanged");
static_assert(offsetof(Drone_tag, alertStatusChangedFrame) == 0x41c, "Wrong offset for alertStatusChangedFrame");
static_assert(offsetof(Drone_tag, playScript) == 0x450, "Wrong offset for playScript");
static_assert(offsetof(Drone_tag, animEndState) == 0x492, "Wrong offset for animEndState");
static_assert(offsetof(Drone_tag, initialState) == 0x49e, "Wrong offset for initialState");
static_assert(offsetof(Drone_tag, aiPoint) == 0x564, "Wrong offset for aiPoint");
static_assert(offsetof(Drone_tag, route1) == 0x634, "Wrong offset for route1");
static_assert(offsetof(Drone_tag, route2) == 0x6d8, "Wrong offset for route2");
static_assert(offsetof(Drone_tag, diVars) == 0x874, "Wrong offset for diVars");
static_assert(offsetof(Drone_tag, timerA) == 0x918, "Wrong offset for timerA");
static_assert(offsetof(Drone_tag, stateScratch) == 0x930, "Wrong offset for stateScratch");
static_assert(offsetof(Drone_tag, botVars) == 0x974, "Wrong offset for botVars");

// Set by Drone_InitComms / ResetMap_GameInit (still the game's); while set, no drones are created and
// Drone_SM_SendMsg / Drone_Message send nothing.
#define Drone_bDisableSystem (*(uchar *)0x001dfa2a)

bool Drone_DCVfromOBJ(obj_tag* obj, DCVars_tag *dcVars);
obj_tag* Drone_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Drone_SM_RouteMsg(MsgObject *msg);
// Not a reimplementation: a cdecl shim onto the original dispatcher at 0x4e410, which takes dcVars in ESI
// (DroneSM.cpp).
void Drone_SM_RouteMsgDCV(DCVars_tag *dcVars, MsgObject *msg);
void Drone_EnableAll(char enable, HASHCODE hashcode);
// The game's (AUTOGEN in GT.cpp): false only for an object whose drone has its AI running (flag 0x100), neither
// 0x200 nor 0x400 set, health not at or below 0, that is not OBJECTTYPE_DEAD_DRONE and not marked for deletion
bool MPDrone_MaybeIsDyingOrDead(obj_tag *obj);

#endif // DRONE_H_