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

// Runs a message through the handler of `state` (NDrone2_ProcessStateMachine); true when it was handled
typedef bool (*DroneProcessFunc)(DCVars_tag *dcv, uint state, MsgObject *msg);

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
    DroneProcessFunc processFunction;   // 0x28 - what Drone_SM_RouteMsgDCV runs messages through
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

// An anim state asked for, until DroneAnim_CallHandler plays it (Drone_tag+0x454; docs/drone/behaviours 6.2)
typedef struct DroneAnimCall {
    ushort state;           // 0x0 - the anim state
    uchar pending;          // 0x2
    uchar _pad3;
    ushort fromState;       // 0x4 - the anim state playing when it was asked for
    uchar kind;             // 0x6 - DroneAnimStates[state].kind
    uchar priority;         // 0x7 - DroneAnimStates[state].priority
    ushort variant;         // 0x8
    ushort _padA;
    uint frame;             // 0xc - GameState.NumFramesUnpaused when it was asked for
} DroneAnimCall;

// A drone timer (Drone_tag.timerA, timerB): NDrone2_PreDroneControl sends a message when it runs out
typedef struct DroneTimer {
    uchar on;               // 0x0 - cleared on every state change (Drone_SM_RouteMsgDCV)
    char _pad1[3];
    uint dueFrame;          // 0x4
    uint unknown8;          // 0x8
} DroneTimer;

// What an explosion hands a drone it hits (the payload of DRONE_MSG_ImpactExplosive; NDrone2_ExplosiveImpact).
// Only the fields our code reads.
typedef struct DroneImpact_tag {
    float damage;           // 0x00 - NDrone2_ExplosiveImpact takes it when above 0
    char _unknown04[4];
    _VECTOR position;       // 0x08 - where it went off (NDrone2_ExplosionDir)
    obj_tag *unknown14;     // 0x14 - with bots, MP_RegisterBulletHit's object; a 0x50000ba skin is not hurt by its
                            //        own (NDrone2_ExplosiveImpact)
} DroneImpact_tag;

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
    uchar vulnerable;               // 0x014 - NDrone2_HitDamage takes health only while it is set (nightfire-port's
                                    //         name)
    uchar unknown15;                // 0x015 - NDrone2_DeathTalk: death cry 9 rather than 1
    uchar unknown16;                // 0x016 - set by PressAlarm and RunToAlarm, cleared by DroneFunc_HandleImpact in
                                    //         TowerA/TowerB; NDrone2_SeenAndAttacking
                                    //         then counts the drone as attacking
    uchar unknown17;                // 0x017
    uchar ninjaEyes;                // 0x018 (inferred)
    uchar captain;                  // 0x019 - drops grenades when it dies (DroneFunc_OnInitDeath)
    uchar unknown1a;                // 0x01a
    uchar impactImmune;             // 0x01b (inferred)
    uchar heavyHit;                 // 0x01c - NDrone2_BulletImpact: the last shot took over a quarter of the start
                                    //         health and left over a third
    char _pad1d[0x21 - 0x1d];
    uchar unknown21;                // 0x021 - DroneAnim_PlayFiringAnim fires only while it is set
    char _pad22;
    uchar flies;                    // 0x023 - abseil / astronaut movement (inferred)
    char _pad24[0x26 - 0x24];
    uchar touchingPlayer;           // 0x026 - set when the player's collision hits it (NDrone2_DealWithObjHit)
    char _pad27[0x29 - 0x27];
    uchar usingSecondBehaviour;     // 0x029
    uchar inPlayersSights;          // 0x02a - set by Check_Target and NDrone2_PreDroneControl; the talk functions
                                    //         speak while it is set (the player aiming at the drone: inferred)
    uchar explosiveSeen;            // 0x02b - DroneFunc_ConsiderExplosive set it running from explosivePos
    uchar headTracking;             // 0x02c - NDrone2_HeadTrackObj was given an object
    char _pad2d[0x31 - 0x2d];
    uchar modeFlag31;               // 0x031
    uchar modeFromOldTable;         // 0x032 - DoModeSettingsOLD ran: DefaultInit then runs DoTypeSettingsOLD
    char _pad33[0x38 - 0x33];
    uchar unknown38;                // 0x038 - set every frame by DSTATE_JustStand
    char _pad39;
    uchar headShot;                 // 0x03a - no death cry (DroneFunc_OnInitDeath); DroneAnim_LocationDeathAnim
                                    //         clears it (inferred name)
    uchar firing;                 // 0x03b - DroneWeap_Fire sets it; DroneWeap_HandleFiring fires while it is set
    uchar burstOver;                // 0x03c - set when DroneWeap_DoFiring returns false (DroneWeap_HandleFiring)
    uchar oneBurst;                 // 0x03d - with burstOver set, DroneWeap_HandleFiring clears firing; set by
                                    //         Drone_SM_RouteMsgDCV on a change to a death or BulletImpact state
    uchar firesByAnim;              // 0x03e - shots come from DroneAnim_PlayFiringAnim, not DroneWeap_DoFiring
    uchar readyToFire;              // 0x03f - DroneWeap_DoFiring: the shot is due and DroneWeap_Ready2Fire agrees
    uchar fired;                    // 0x040 - set by DroneWeap_FireWeapon, cleared by DroneAnim_PlayFiringAnim
    uchar unknown41;                // 0x041 - while set, DroneWeap_DoBulletAccuracy misses beyond the too-close
                                    //         distance; DroneWeap_FireWeapon clears it
    char _pad42[0x44 - 0x42];
    uchar side;                     // 0x044 - 1 enemy, 2 ally, 3 civilian / neutral
    char _pad45[0x60 - 0x45];
    _VECTOR contactLocal;           // 0x060 - NDrone2_DealWithObjHit: where the player touched it, in its own space,
    _VECTOR contactRot;             // 0x06c   that as a rotation,
    _VECTOR contactDir;             // 0x078   and in the world, from its position
    char _pad84[0x90 - 0x84];
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
    ushort stunFrames;              // 0x0a0 - DroneFunc_HandleImpact: taser hits, one frame's worth each
    char _pada2[0xa4 - 0xa2];
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
    uchar waitSwitchChannel;       // 0x118 - key 3
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
    float hitDamageScale;           // 0x130 - multiplies the damage NDrone2_DoHitEffects applies; reset to 1.0 on every
                                    //         state change
    float lastDamage;               // 0x134 - what NDrone2_HitDamage took (or would have) last
    float playerDistance[4];        // 0x138 - PreDroneControl
    obj_tag *opponent;              // 0x148
    uchar targetSlot;               // 0x14c - the NPCGlobals.targets entry for the opponent (0xff for none)
    char _pad14d[0x150 - 0x14d];
    _VECTOR opponentLastPos;        // 0x150 - aimed at while there is no opponent (DroneWeap_HandleFiring; inferred)
    char _pad15c[0x168 - 0x15c];
    float distanceToTarget;         // 0x168 - to the opponent (Drone_GetOpponentInfo)
    _VECTOR opponentDelta;          // 0x16c - from the drone to the opponent: DroneWeap_CloseCombatImpact's direction
    _VECTOR opponentAngles;         // 0x178 - the rotation DroneWeap_DoBulletAccuracy turns its miss by
    float opponentFacing;           // 0x184 - NDrone2_BondIsFacingMe: under the angle, the opponent faces the drone
                                    //         (inferred)
    float opponentBearing;          // 0x188 - Drone_GetOpponentInfo: the opponent's angle off the drone's facing
    ushort hitsTaken;               // 0x18c - counted by NDrone2_DealWithObjHit and Drone_BulletHit
    char _pad18e[0x190 - 0x18e];
    uchar targetMoving;             // 0x190 - DroneWeap_DoOpponentTargetting: the player moves in the plane
    uchar targetStartedMoving;      // 0x191 - for TargetFirstMoved_TimeToHit after it starts
    uchar targetStartedSeen;        // 0x192
    uchar targetStopped;            // 0x193 - for TargetFirstStopped_TimeToHit after it stops
    uchar targetStoppedSeen;        // 0x194
    char _pad195[0x19c - 0x195];
    _VECTOR targetPos;              // 0x19c - where the drone aims: the opponent plus aimOffset
    _VECTOR aimError;               // 0x1a8 - DroneWeap_DoBulletAccuracy's miss, added to targetPos for each shot
    _VECTOR aimOffset;              // 0x1b4 - from the opponent's position to the point aimed at
    char _pad1c0[0x1c4 - 0x1c0];
    uint targetChangeFrame;         // 0x1c4 - when targetMoving last changed (DroneWeap_DoOpponentTargetting)
    uint sightFlags;                // 0x1c8 - DRONE_SIGHT_*: 4 sees its opponent (DroneVision_HaveOpponentSight), 8 has
                                    //         reacted to its first sighting (set with message 0xf; also by HostageIdle)
    char _pad1cc[0x1d8 - 0x1cc];
    uint opponentSeenFrame;         // 0x1d8 - DroneVision_HaveOpponentSight (inferred)
    char _pad1dc[0x200 - 0x1dc];
    uint sightFrames;               // 0x200 - frames of continuous sight (DroneVision_HaveOpponentSight)
    uint unseenFrames;              // 0x204 - frames without sight of the opponent: over 15 ends a burst
                                    //         (DroneWeap_DoFiring; inferred)
    char _pad208[0x234 - 0x208];
    obj_tag *lastAttacker;          // 0x234 - who killed it, for DroneFunc_OnInitDeath and DroneFunc_SetDeathChannel
    char _pad238[0x290 - 0x238];
    uint unknown290;                // 0x290 - LinkCreep_Handler sets 0x1e when a creep step is blocked
    char _pad294[0x298 - 0x294];
    uint explosiveTimeout;          // 0x298 - DroneFunc_ConsiderExplosive: the frame it stops running
    _VECTOR explosivePos;           // 0x29c - and from where
    cel_tag *explosiveCel;          // 0x2a8
    float explosiveRadius;          // 0x2ac - how far
    short explosiveState;           // 0x2b0 - the state it goes to afterwards
    char _pad2b2[0x2b4 - 0x2b2];
    uint headTrackUntil;            // 0x2b4 - NDrone2_HeadTrackObj
    char _pad2b8[0x2bc - 0x2b8];
    obj_tag *headTarget;            // 0x2bc
    char _pad2c0[0x2cc - 0x2c0];
    float headYaw;                  // 0x2cc - NDrone2_GetHeadPos returns these two for a drone
    _VECTOR headPos;                // 0x2d0
    char _pad2dc[0x388 - 0x2dc];
    HITDATA_tag *lastHit;           // 0x388 - set by NDrone2_PreDroneControl; DroneAnim_LocationDeathAnim reads it
    char _pad38c[0x394 - 0x38c];
    _VECTOR velocity;               // 0x394 - DTYPE_Astronaut: NDrone2_DealWithObjHit reverses or replaces it (inferred)
    char _pad3a0[0x3cc - 0x3a0];
    DYNAMICSOUNDS *speechChannel;   // 0x3cc - what the drone is saying (NDrone2_SpeechSFX); Drone_IsTalking clears it
                                    //         once it has finished
    uint unknown3d0;                // 0x3d0 - a frame: for 3 s after it, NDrone2_HitTalkExtra says nothing 1 time in 5
    uint *currentBehaviour;         // 0x3d4 - behaviour1 or behaviour2
    uint behaviour1[3];             // 0x3d8 - behaviour property words (behaviour_util_*)
    uint behaviour2[3];             // 0x3e4
    uint unknown3f0;                // 0x3f0 - cleared by NDrone2_SetAsDead
    uint flags;                    // 0x3f4 - 0x100 AI running, 0x200 dead, 0x400 dying, 0x10 frozen ... (README 4.3)
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
    char _pad420[0x434 - 0x420];
    void *unknown434;               // 0x434 - an anim script (A3); while set, DroneWeap_Ready2Fire and
                                    //         DroneAnim_PlayFiringAnim hold fire
    void *facialAnim;               // 0x438 - NDrone2_SpeechSFX's facial animation; the talk functions count a line
                                    //         as said when either it or the speech started
    uint impactAnimFrame;           // 0x43c - when it last played an impact anim (Punch and BulletImpact)
    void *animCallback1;            // 0x440
    void *animCallback2;            // 0x444
    float unknown448;               // 0x448 - pi, set by DSTATE_PlayScript at the end of script 0x60007ac
    char _pad44c[0x450 - 0x44c];
    HASHCODE playScript;            // 0x450 - key 1
    DroneAnimCall animCall;         // 0x454 - the anim state asked for (DroneAnim_CallAnim)
    uchar animMode;                 // 0x464 - the playing row's completion mode (docs/drone/behaviours 6.1)
    char _pad465;
    short animState;                // 0x466 - the anim state playing
    ushort animRow;                 // 0x468 - the Drone_AnimInfo row playing
    char _pad46a[0x46c - 0x46a];
    uint unknown46c;                // 0x46c - set by DroneAnim_SetScript; with 0x380, NDrone2_SetAngleToObj turns only
                                    //         when forced
    char _pad470[0x477 - 0x470];
    uchar unknown477;               // 0x477 - cleared by every new anim call
    char _pad478[0x481 - 0x478];
    uchar unknown481;               // 0x481 - the slot's move state (docs/drone/behaviours 6.1); DroneAnim_LocationDeathAnim
                                    //         picks a random death variant when it is set
    char _pad482[0x484 - 0x482];
    float animCallSpeed;            // 0x484 - DroneAnim_CallAnim's speed
    char _pad488[0x492 - 0x488];
    short animEndState;             // 0x492 - DroneAnim_CallAnim's end state and message
    uint animEndMsg;                // 0x494
    uint idleAnimFrame;             // 0x498 - DroneAnim_SetStandIdleAnim / SetWalkIdleAnim: no new idle anim before
    short returnState;              // 0x49c - DSTATE_EnemyRunToPoint sets it to initialState; the Obstructed and
                                    //         AlertToPosition states read it (inferred name)
    short initialState;            // 0x49e - DroneTypeSettings; DSTATE_Global's Enter goes there
    short secondState;              // 0x4a0 - the state after NDrone2_ChangeToAttackMode
    char _pad4a2[0x4b4 - 0x4a2];
    _VECTOR pushOffset;             // 0x4b4 - each shot pushes it 0.175 along the shot, to 0.175 at most
                                    //         (NDrone2_DealWithObjHit)
    char _pad4c0[0x534 - 0x4c0];
    float angleToSomePoint;         // 0x534 - Ghidra's name; DSTATE_JustStand turns it 0.01 a frame
    char _pad538[0x564 - 0x538];
    AIPoint_tag aiPoint;            // 0x564
    char _pad58c[0x634 - 0x58c];
    AIRoute_tag route1;             // 0x634 - the dynamic route
    char _pad6d4[0x6d8 - 0x6d4];
    AIRoute_tag route2;             // 0x6d8 - the patrol / mission route
    char _pad778[0x825 - 0x778];
    uchar unknown825;               // 0x825 - bit 0 cleared by DSTATE_PlayScript for script 0x60007ac
    char _pad826[0x82c - 0x826];
    uint unknown82c;                // 0x82c - while set, LinkCreep_Handler does not test the bounds
    char _pad830[0x840 - 0x830];
    short clip;                     // 0x840 - rounds left in the clip
    short clipSize;                 // 0x842
    short burstShots;               // 0x844 - left in this burst
    char _pad846[0x848 - 0x846];
    uint nextShotFrame;             // 0x848
    uint lastShotFrame;             // 0x84c - the last shot, plus the pause before the burst after it
    ushort grenadeWeapon;           // 0x850 - the weapon_data entry DroneWeap_ThrowGrenade throws
    ushort grenades;                // 0x852 - how many are left
    char _pad854[0x858 - 0x854];
    obj_tag *unknown858;            // 0x858 - set: DSTATE_Death_Anim and DeathByExplosion go to SpecialDeath_Anim
    char _pad85c[0x874 - 0x85c];
    DIVars_tag diVars;              // 0x874 - the drone's own copy of its creation vars
    DroneTimer timerA;              // 0x918 - both timers are switched off on every state change
    DroneTimer timerB;              // 0x924
    uint stateScratch;              // 0x930 - anim id, flags; read as a state number by DSTATE_Disabled; in
                                    //         DSTATE_PlayScript the state an impact sends it to; the talk functions'
                                    //         line counter (0xff on entering a state)
    uint stateScratch2;             // 0x934 - in DSTATE_PlayScript: whether it looks for an opponent while playing;
                                    //         the talk functions' next-line frame
    uint stateScratch3;             // 0x938 - DroneFunc_DoBondTalk's lines while aimed at
    char _pad93c[0x948 - 0x93c];
    uint talkStep;                  // 0x948 - NDrone2_HandleTalking's conversation (99 = over)
    uint nextTalkFrame;             // 0x94c
    float armedPersonRadius;        // 0x950 - only DSTATE_Civilian sets it, to pass NDrone2_NearArmedPerson, which does
                                    //         not read it (our name)
    float fadeAlpha;                // 0x954 - NDrone2_FadeOut counts it down into the object's tweakA
    char _pad958[0x974 - 0x958];
    struct BOT_vars_t *botVars;     // 0x974 - multiplayer bots only
} Drone_tag;

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
static_assert(sizeof(StateMachineInfo_tag) == 0x2c, "StateMachineInfo_tag is 0x2c bytes");
static_assert(offsetof(StateMachineInfo_tag, stateParam) == 0x24, "Wrong offset for stateParam");
static_assert(sizeof(DroneAnimCall) == 0x10, "DroneAnimCall is 0x10 bytes");
static_assert(sizeof(DroneTimer) == 0xc, "DroneTimer is 0xc bytes");
static_assert(offsetof(DroneImpact_tag, unknown14) == 0x14, "Wrong offset for DroneImpact_tag.unknown14");
static_assert(sizeof(DIVars_tag) == 0xa4, "DIVars is wrong size");
static_assert(sizeof(DCVars_tag) == 0x10, "DCVars is wrong size");
static_assert(sizeof(MsgObject) == 0x1c, "MsgObject is 0x1c bytes");
static_assert(sizeof(Drone_tag) == 0x978, "Drone_tag is 0x978 bytes");
static_assert(offsetof(Drone_tag, side) == 0x44, "Wrong offset for side");
static_assert(offsetof(Drone_tag, oneBurst) == 0x3d, "Wrong offset for oneBurst");
static_assert(offsetof(Drone_tag, hitDamageScale) == 0x130, "Wrong offset for hitDamageScale");
static_assert(offsetof(Drone_tag, health) == 0x90, "Wrong offset for health");
static_assert(offsetof(Drone_tag, dtype) == 0xa9, "Wrong offset for dtype");
static_assert(offsetof(Drone_tag, skinHashcode) == 0xc4, "Wrong offset for skinHashcode");
static_assert(offsetof(Drone_tag, stateTimeoutFrame) == 0xe8, "Wrong offset for stateTimeoutFrame");
static_assert(offsetof(Drone_tag, sm) == 0xec, "Wrong offset for sm");
static_assert(offsetof(Drone_tag, sm) + offsetof(StateMachineInfo_tag, processFunction) == 0x114, "Wrong offset for processFunction");
static_assert(offsetof(Drone_tag, modeChangeSwitchChannel) == 0x119, "Wrong offset for modeChangeSwitchChannel");
static_assert(offsetof(Drone_tag, key7) == 0x12c, "Wrong offset for key7");
static_assert(offsetof(Drone_tag, opponent) == 0x148, "Wrong offset for opponent");
static_assert(offsetof(Drone_tag, targetSlot) == 0x14c, "Wrong offset for targetSlot");
static_assert(offsetof(Drone_tag, distanceToTarget) == 0x168, "Wrong offset for distanceToTarget");
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
static_assert(offsetof(Drone_tag, unknown38) == 0x38, "Wrong offset for unknown38");
static_assert(offsetof(Drone_tag, returnState) == 0x49c, "Wrong offset for returnState");
static_assert(offsetof(Drone_tag, angleToSomePoint) == 0x534, "Wrong offset for angleToSomePoint");
static_assert(offsetof(Drone_tag, aiPoint) == 0x564, "Wrong offset for aiPoint");
static_assert(offsetof(Drone_tag, route1) == 0x634, "Wrong offset for route1");
static_assert(offsetof(Drone_tag, route2) == 0x6d8, "Wrong offset for route2");
static_assert(offsetof(Drone_tag, diVars) == 0x874, "Wrong offset for diVars");
static_assert(offsetof(Drone_tag, timerA) == 0x918, "Wrong offset for timerA");
static_assert(offsetof(Drone_tag, stateScratch) == 0x930, "Wrong offset for stateScratch");
static_assert(offsetof(Drone_tag, stateScratch2) == 0x934, "Wrong offset for stateScratch2");
static_assert(offsetof(Drone_tag, unknown448) == 0x448, "Wrong offset for unknown448");
static_assert(offsetof(Drone_tag, unknown825) == 0x825, "Wrong offset for unknown825");
static_assert(offsetof(Drone_tag, botVars) == 0x974, "Wrong offset for botVars");
static_assert(offsetof(Drone_tag, unknown290) == 0x290, "Wrong offset for unknown290");
static_assert(offsetof(Drone_tag, unknown82c) == 0x82c, "Wrong offset for unknown82c");
static_assert(offsetof(Drone_tag, captain) == 0x19, "Wrong offset for captain");
static_assert(offsetof(Drone_tag, inPlayersSights) == 0x2a, "Wrong offset for inPlayersSights");
static_assert(offsetof(Drone_tag, headTracking) == 0x2c, "Wrong offset for headTracking");
static_assert(offsetof(Drone_tag, headShot) == 0x3a, "Wrong offset for headShot");
static_assert(offsetof(Drone_tag, waitSwitchChannel) == 0x118, "Wrong offset for waitSwitchChannel");
static_assert(offsetof(Drone_tag, opponentBearing) == 0x188, "Wrong offset for opponentBearing");
static_assert(offsetof(Drone_tag, opponentSeenFrame) == 0x1d8, "Wrong offset for opponentSeenFrame");
static_assert(offsetof(Drone_tag, sightFrames) == 0x200, "Wrong offset for sightFrames");
static_assert(offsetof(Drone_tag, lastAttacker) == 0x234, "Wrong offset for lastAttacker");
static_assert(offsetof(Drone_tag, explosiveTimeout) == 0x298, "Wrong offset for explosiveTimeout");
static_assert(offsetof(Drone_tag, explosiveState) == 0x2b0, "Wrong offset for explosiveState");
static_assert(offsetof(Drone_tag, headTrackUntil) == 0x2b4, "Wrong offset for headTrackUntil");
static_assert(offsetof(Drone_tag, headTarget) == 0x2bc, "Wrong offset for headTarget");
static_assert(offsetof(Drone_tag, headPos) == 0x2d0, "Wrong offset for headPos");
static_assert(offsetof(Drone_tag, lastHit) == 0x388, "Wrong offset for lastHit");
static_assert(offsetof(Drone_tag, speechChannel) == 0x3cc, "Wrong offset for speechChannel");
static_assert(offsetof(Drone_tag, unknown3f0) == 0x3f0, "Wrong offset for unknown3f0");
static_assert(offsetof(Drone_tag, facialAnim) == 0x438, "Wrong offset for facialAnim");
static_assert(offsetof(Drone_tag, animCall) == 0x454, "Wrong offset for animCall");
static_assert(offsetof(Drone_tag, animState) == 0x466, "Wrong offset for animState");
static_assert(offsetof(Drone_tag, animRow) == 0x468, "Wrong offset for animRow");
static_assert(offsetof(Drone_tag, unknown477) == 0x477, "Wrong offset for unknown477");
static_assert(offsetof(Drone_tag, unknown481) == 0x481, "Wrong offset for unknown481");
static_assert(offsetof(Drone_tag, animCallSpeed) == 0x484, "Wrong offset for animCallSpeed");
static_assert(offsetof(Drone_tag, idleAnimFrame) == 0x498, "Wrong offset for idleAnimFrame");
static_assert(offsetof(Drone_tag, timerB) == 0x924, "Wrong offset for timerB");
static_assert(offsetof(Drone_tag, stateScratch3) == 0x938, "Wrong offset for stateScratch3");
static_assert(offsetof(Drone_tag, talkStep) == 0x948, "Wrong offset for talkStep");
static_assert(offsetof(Drone_tag, fadeAlpha) == 0x954, "Wrong offset for fadeAlpha");
static_assert(offsetof(DroneImpact_tag, position) == 0x8, "Wrong offset for DroneImpact_tag.position");
static_assert(offsetof(Drone_tag, vulnerable) == 0x14, "Wrong offset for vulnerable");
static_assert(offsetof(Drone_tag, heavyHit) == 0x1c, "Wrong offset for heavyHit");
static_assert(offsetof(Drone_tag, touchingPlayer) == 0x26, "Wrong offset for touchingPlayer");
static_assert(offsetof(Drone_tag, contactLocal) == 0x60, "Wrong offset for contactLocal");
static_assert(offsetof(Drone_tag, contactDir) == 0x78, "Wrong offset for contactDir");
static_assert(offsetof(Drone_tag, stunFrames) == 0xa0, "Wrong offset for stunFrames");
static_assert(offsetof(Drone_tag, lastDamage) == 0x134, "Wrong offset for lastDamage");
static_assert(offsetof(Drone_tag, hitsTaken) == 0x18c, "Wrong offset for hitsTaken");
static_assert(offsetof(Drone_tag, velocity) == 0x394, "Wrong offset for velocity");
static_assert(offsetof(Drone_tag, unknown3d0) == 0x3d0, "Wrong offset for unknown3d0");
static_assert(offsetof(Drone_tag, impactAnimFrame) == 0x43c, "Wrong offset for impactAnimFrame");
static_assert(offsetof(Drone_tag, pushOffset) == 0x4b4, "Wrong offset for pushOffset");
static_assert(offsetof(Drone_tag, unknown858) == 0x858, "Wrong offset for unknown858");
static_assert(offsetof(Drone_tag, armedPersonRadius) == 0x950, "Wrong offset for armedPersonRadius");
static_assert(offsetof(Drone_tag, unknown21) == 0x21, "Wrong offset for unknown21");
static_assert(offsetof(Drone_tag, burstOver) == 0x3c, "Wrong offset for burstOver");
static_assert(offsetof(Drone_tag, unknown41) == 0x41, "Wrong offset for unknown41");
static_assert(offsetof(Drone_tag, opponentLastPos) == 0x150, "Wrong offset for opponentLastPos");
static_assert(offsetof(Drone_tag, opponentDelta) == 0x16c, "Wrong offset for opponentDelta");
static_assert(offsetof(Drone_tag, opponentFacing) == 0x184, "Wrong offset for opponentFacing");
static_assert(offsetof(Drone_tag, targetMoving) == 0x190, "Wrong offset for targetMoving");
static_assert(offsetof(Drone_tag, targetPos) == 0x19c, "Wrong offset for targetPos");
static_assert(offsetof(Drone_tag, aimOffset) == 0x1b4, "Wrong offset for aimOffset");
static_assert(offsetof(Drone_tag, targetChangeFrame) == 0x1c4, "Wrong offset for targetChangeFrame");
static_assert(offsetof(Drone_tag, unseenFrames) == 0x204, "Wrong offset for unseenFrames");
static_assert(offsetof(Drone_tag, unknown434) == 0x434, "Wrong offset for unknown434");
static_assert(offsetof(Drone_tag, unknown46c) == 0x46c, "Wrong offset for unknown46c");
static_assert(offsetof(Drone_tag, clip) == 0x840, "Wrong offset for clip");
static_assert(offsetof(Drone_tag, burstShots) == 0x844, "Wrong offset for burstShots");
static_assert(offsetof(Drone_tag, nextShotFrame) == 0x848, "Wrong offset for nextShotFrame");
static_assert(offsetof(Drone_tag, grenades) == 0x852, "Wrong offset for grenades");

// Set by Drone_InitComms / ResetMap_GameInit (still the game's); while set, no drones are created and
// Drone_SM_SendMsg / Drone_Message send nothing.
#define Drone_bDisableSystem (*(uchar *)0x001dfa2a)

bool Drone_DCVfromOBJ(obj_tag* obj, DCVars_tag *dcVars);
obj_tag* Drone_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Drone_SM_RouteMsg(MsgObject *msg);
// The dispatcher. The original takes dcVars in ESI: the game reaches the adaptor, C++ calls the core (DroneSM.cpp).
void _Drone_SM_RouteMsgDCV(DCVars_tag *dcVars, MsgObject *msg);
void Drone_SM_RouteMsgDCV(void);
void Drone_EnableAll(char enable, HASHCODE hashcode);
obj_tag* Drone_CoderCreate(_VECTOR *pos, _VECTOR *rot, HASHCODE skin, HASHCODE playScript, uint unused, uint weapon, ushort initialState);

#endif // DRONE_H_