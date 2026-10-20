#ifndef DRONE_H_
#define DRONE_H_

#include "../../actionhelpers.h"
#include "../../engine/AINetwork.h"
#include "../../engine/Collide.h"     // HITTEST_tag

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
    union {
        uint scratch20;         // 0x20 - per-state
        float pathDistance;     //        the AllyLead states: DroneMove_ObjectMissionPathDistance to the player
    };
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
    celglist_tag *glist;    // 0x1c - NDrone2_CreateObj makes a MINISUB-type object with this model; always NULL
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
    float radius;           // 0x04 - the explosion's (Explode_Propagate)
    _VECTOR position;       // 0x08 - where it went off (NDrone2_ExplosionDir)
    obj_tag *source;        // 0x14 - who set it off (Explode_Propagate), kept as the drone's lastAttacker; with
                            //        bots, MP_RegisterBulletHit's object; a 0x50000ba skin is not hurt by its own
} DroneImpact_tag;

// Drone_tag.sightFlags
#define DRONE_SIGHT_LOS 2               // a line of sight to the opponent this frame (DroneVision_SeekOpponentLOS)
#define DRONE_SIGHT_HAD_LOS 0x40        // and the frame before
#define DRONE_SIGHT_SEES_OPPONENT 4    // registered the opponent after the reaction time (DroneVision_HaveOpponentSight)
#define DRONE_SIGHT_REACTED 8           // has reacted to a first sighting: DroneFunc_ReactionTime is 0 from then on

// A row's or anim state's function (DroneData_AnimFunc, DroneData_DAStateFunc), run with a DANIMFUNC_ phase
typedef void (*DroneAnimFunc)(Drone_tag *drone, ushort phase);
#define DANIMFUNC_START 0   // the row's script has started
#define DANIMFUNC_FRAME 1   // each frame it plays (Drone_Control)
#define DANIMFUNC_END 2     // the row ends

// One drone (the obj_tag's extraObjectData), 0x978 bytes. Only the fields docs/drone/architecture/README.md 4.3 is
// sure of are named; A3-drone-fields.md lists every offset the code touches.
typedef struct Drone_tag {
    Drone_tag *prev;                // 0x000 - NPCGlobals.NDrone2List
    Drone_tag *next;                // 0x004
    Drone_tag *groupNext;           // 0x008 - NDrone2_SetupGroups
    obj_tag *gameObj;               // 0x00c
    obj_tag *bodyGlow;              // 0x010 - BodyGlow_Create's object
    uchar vulnerable;               // 0x014 - NDrone2_HitDamage takes health only while it is set (nightfire-port's
                                    //         name)
    uchar unknown15;                // 0x015 - NDrone2_DeathTalk: death cry 9 rather than 1
    uchar unknown16;                // 0x016 - set by PressAlarm and RunToAlarm, cleared by DroneFunc_HandleImpact in
                                    //         TowerA/TowerB; NDrone2_SeenAndAttacking
                                    //         then counts the drone as attacking
    uchar unknown17;                // 0x017 - DroneMove_NoBunching's answer, each frame (NDrone2_ControlSTANDARD)
    uchar ninjaEyes;                // 0x018 (inferred)
    uchar captain;                  // 0x019 - drops grenades when it dies (DroneFunc_OnInitDeath)
    uchar unknown1a;                // 0x01a
    uchar impactImmune;             // 0x01b (inferred)
    uchar heavyHit;                 // 0x01c - NDrone2_BulletImpact: the last shot took over a quarter of the start
                                    //         health and left over a third
    uchar unknown1d;                // 0x01d - cleared with gunThroughWall while the drone does not aim
    uchar gunThroughWall;           // 0x01e - its muzzle is through a wall (DroneVision_GunThroughWall)
    uchar aimByEvent;              // 0x01f - NDrone2_AimTest answers aimEvent rather than the script's progress
    uchar aimEvent;                 // 0x020 - DroneAnim_EventFunc
    uchar unknown21;                // 0x021 - DroneAnim_PlayFiringAnim fires only while it is set
    uchar aiming;                   // 0x022 - NDrone2_DoTracking: aiming at its opponent
    uchar flies;                    // 0x023 - abseil / astronaut movement (inferred)
    uchar floatsToMoveTarget;       // 0x024 - with flies: NDrone2_Move accelerates it towards moveTarget (set by
                                    //         DroneMove_AstronautCombat)
    uchar avoidingPlayer;           // 0x025 - NDrone2_Steer is turning it away from a player
    uchar touchingPlayer;           // 0x026 - set when the player's collision hits it (NDrone2_DealWithObjHit)
    uchar rootHeightSet;            // 0x027 - DroneAnim_PreTransCallback: the anim gave a root height
    uchar attackModeTaken;          // 0x028 - set by NDrone2_ChangeToAttackMode (our name)
    uchar usingSecondBehaviour;     // 0x029
    uchar inPlayersSights;          // 0x02a - set by Check_Target and NDrone2_PreDroneControl; the talk functions
                                    //         speak while it is set (the player aiming at the drone: inferred)
    uchar explosiveSeen;            // 0x02b - DroneFunc_ConsiderExplosive set it running from explosivePos
    uchar headTracking;             // 0x02c - NDrone2_HeadTrackObj was given an object
    uchar unknown2d;                // 0x02d - cleared by NDrone2_PostLoad_Init
    uchar atCornerCover;            // 0x02e - the cover states set it while the node held is a corner, not low cover (our
                                    //         name)
    uchar smallCollision;           // 0x02f - set by the stun states: NDrone2_Collision makes a 0.3 sphere at the feet
    char _pad30;
    uchar modeFlag31;               // 0x031
    uchar modeFromOldTable;         // 0x032 - DoModeSettingsOLD ran: DefaultInit then runs DoTypeSettingsOLD
    uchar unknown33;                // 0x033 - set: DroneVision_EnemyAlerts does not keep the return state
    uchar turnedToAlert;            // 0x034 - the heard-noise states turned it towards the noise; DSTATE_SearchArea's end
                                    //         clears it (our name)
    uchar facialAnimDone;           // 0x035 - NDrone2_PlayFacialAnim: the facial script's done flag points here
    uchar facialAnimPlaying;        // 0x036 - set by NDrone2_PlayFacialAnim; NDrone2_SpeechSFX starts no other then
    uchar unknown37;                // 0x037 - cleared by NDrone2_PlayFacialAnim; NDrone2_SpeechSFX starts none while set
    uchar unknown38;               // 0x038 - set every frame by DSTATE_JustStand
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
    uchar seenPlayerLogged;         // 0x042 - DroneVision_LogSeenOpponent has counted it
    uchar heardNoiseStage;          // 0x043 - the furthest heard-noise state entered: 2 Aware, 3 Suspect, 4 Alert (our
                                    //         name)
    uchar side;                    // 0x044 - 1 enemy, 2 ally, 3 civilian / neutral
    char secondWeapon;              // 0x045 - DroneAnim_EventFunc's event 2 changes to it
    char _pad46[0x50 - 0x46];
    float floatAcceleration;        // 0x050 - NDrone2_Move: how fast it speeds up towards moveTarget, a second
    char _pad54[0x60 - 0x54];
    _VECTOR contactLocal;           // 0x060 - NDrone2_DealWithObjHit: where the player touched it, in its own space,
    _VECTOR contactRot;             // 0x06c   that as a rotation,
    _VECTOR contactDir;             // 0x078   and in the world, from its position
    float rootHeight;               // 0x084 - from the position down to the feet (NDrone2_Collision, Drone_FeetOnPoint)
    float prevRootHeight;           // 0x088 - rootHeight before DroneAnim_PreTransCallback changed it
    float animRootHeight;           // 0x08c - the anim's root height (AnimObj.rootHeight), 0 for none
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
    uint unknownb4;                 // 0x0b4 - 0xf from NDrone2_DefaultInit (0x3c for skin class 0x13 in EvilBase)
    uint sightTestFrame;            // 0x0b8 - when DroneVision_ProcessDroneSight last tested its line of sight
    short skinClass;               // 0x0bc
    short animSet;                  // 0x0be - the weapon stance table index (inferred)
    short savedAnimSet;             // 0x0c0
    char _padc2[0xc4 - 0xc2];
    HASHCODE skinHashcode;          // 0x0c4
    float viewAngle;                // 0x0c8 - the ranges from key 14 start here: half the angle it sees in (0 all
                                    //         round; DroneVision)
    float viewRange;                // 0x0cc - how far it sees, at alertness 0
    float combatRanges[5];          // 0x0d0 - the rest of key 14's ranges
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
    uchar chatSwitchChannel;        // 0x121 - DSTATE_CastleChatGuard1 waits for this switch channel (0x19 or 0x11)
    char _pad122[0x124 - 0x122];
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
    _VECTOR opponentLastRot;        // 0x15c - the opponent's rotation (Drone_GetOpponentInfo)
    float distanceToTarget;         // 0x168 - to the opponent (Drone_GetOpponentInfo)
    _VECTOR opponentDelta;          // 0x16c - from the drone to the opponent: DroneWeap_CloseCombatImpact's direction
    _VECTOR opponentAngles;         // 0x178 - the rotation DroneWeap_DoBulletAccuracy turns its miss by
    float opponentFacing;           // 0x184 - NDrone2_BondIsFacingMe: under the angle, the opponent faces the drone
                                    //         (inferred)
    float opponentBearing;          // 0x188 - Drone_GetOpponentInfo: the opponent's angle off the drone's facing
    ushort hitsTaken;               // 0x18c - counted by NDrone2_DealWithObjHit and Drone_BulletHit
    ushort viewBone;                // 0x18e - the Drone_View_Bones entry its line of sight tests next
    uchar targetMoving;            // 0x190 - DroneWeap_DoOpponentTargetting: the player moves in the plane
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
    float opponentVisibilityScale;  // 0x1cc - the AI volumes' visibility where the opponent is
    float opponentVisibility;       // 0x1d0 - DroneVision_OpponentVisibility: 0.7 or more is enough to look
    uint firstSeenFrame;            // 0x1d4 - DroneVision_HaveOpponentSight, at the first sighting
    uint opponentSeenFrame;         // 0x1d8 - DroneVision_HaveOpponentSight (inferred)
    _VECTOR seenFromPos;            // 0x1dc - where the drone stood when Drone_GetOpponentInfo last passed its sighting
                                    //         to its target (our name)
    _VECTOR opponentSeenPos;        // 0x1e8 - where it was last seen (DroneVision_HaveOpponentSight)
    _VECTOR opponentSeenRot;        // 0x1f4 - and its rotation then
    uint sightFrames;               // 0x200 - frames of continuous sight (DroneVision_HaveOpponentSight)
    uint unseenFrames;              // 0x204 - frames without sight of the opponent: over 15 ends a burst
                                    //         (DroneWeap_DoFiring; inferred)
    float interrogateDistance;      // 0x208 - DSTATE_Interogate: the opponent's distance when it started,
    float interrogateNearest;       // 0x20c   the nearest it has come since,
    uint interrogateFrame;          // 0x210   the frame it started (our names)
    char _pad214[0x218 - 0x214];
    _VECTOR interrogatePos;         // 0x218 - the opponent's position then
    char _pad224[0x230 - 0x224];
    obj_tag *unknown230;            // 0x230 - an object; Drone_Delete clears it. A hostage killer's hostage and the
                                    //         hostage's killer (NDrone2_SetupHostageKiller)
    obj_tag *lastAttacker;          // 0x234 - who killed it, for DroneFunc_OnInitDeath and DroneFunc_SetDeathChannel
    obj_tag *nearArmedDrone;        // 0x238 - the armed enemy NDrone2_NearArmedDrone found
    obj_tag *unknown23c;            // 0x23c - a door guard's partner (NDrone2_SetDronePartner); Drone_Delete clears it
    Drone_tag *opponentCursor;     // 0x240 - the drone an ally's NDrone2_FindOpponent looked at last (NDrone2List)
    uint inPlayersSightsFrame;      // 0x244 - set by Check_Target; 30 x FRAME_RATE_DIV frames on, PreDroneControl
                                    //         clears inPlayersSights
    uint inPlayersSightsFrames;     // 0x248 - frames inPlayersSights has been set (PreDroneControl)
    uint guardZone;                 // 0x24c - NDrone2_DoorGuardHandler: where the opponent is from the guarded door,
                                    //         0-3; 0xff on the partner whose planes the other holds (our name)
    char _pad250[0x254 - 0x250];
    plane_equ_tag guardPlanes[3];   // 0x254 - the door guard's planes: through both guards, and across at each
                                    //         (NDrone2_SetDronePartner; our name)
    float nearestDroneDist;         // 0x284 - NDrone2_Steer: how far the nearest drone within 3 m is,
    Drone_tag *nearestDrone;        // 0x288   and which (DroneMove_NoBunching)
    Drone_tag *unknown28c;          // 0x28c - cleared by NDrone2_Steer and, when it is the drone deleted, Drone_Delete
    uint bunchFlags;                // 0x290 - the ways another drone is in: DroneMove_NoBunching adds which side
                                    //         nearestDrone is on (bits as boundaryFlags), LinkCreep_Handler 0x1e
                                    //         when a creep step is blocked; the NDrone2_Can* side moves test it
    uint boundaryFlags;             // 0x294 - DroneMove_SetBoundryFlags: 2 ahead, 4 behind, 8 left, 0x10 right blocked
    uint explosiveTimeout;          // 0x298 - DroneFunc_ConsiderExplosive: the frame it stops running
    _VECTOR explosivePos;           // 0x29c - and from where
    cel_tag *explosiveCel;          // 0x2a8
    float explosiveRadius;          // 0x2ac - how far
    short explosiveState;           // 0x2b0 - the state it goes to afterwards
    char _pad2b2[0x2b4 - 0x2b2];
    uint headTrackUntil;            // 0x2b4 - NDrone2_HeadTrackObj
    short headMode;                 // 0x2b8 - 2: NDrone2_DoHeadTrackingBehaviour looks further and holds its choice
    char _pad2ba[0x2bc - 0x2ba];
    obj_tag *headTarget;            // 0x2bc
    char _pad2c0[0x2cc - 0x2c0];
    float headYaw;                  // 0x2cc - NDrone2_GetHeadPos returns these two for a drone
    _VECTOR headPos;                // 0x2d0
    uint secondEyeFlags;            // 0x2dc - with 0x30000000 set, DroneVision_SeekOpponentLOS also looks from
    char _pad2e0[0x2e8 - 0x2e0];    //         secondEyePos (and ignores what it sees)
    _VECTOR secondEyePos;           // 0x2e8
    cel_tag *secondEyeCel;          // 0x2f4
    char _pad2f8[0x2fa - 0x2f8];
    ushort moveQuad;                // 0x2fa - DroneAnim_SetCombatMoveAnim: the quarter (DroneAnim_GetMoveQuad) its last
                                    //         combat walk anim was chosen for; 0 when none was (our name)
    HITTEST_tag collision;          // 0x2fc - the capsule NDrone2_Collision sets up for the collision update
    HITDATA_tag *lastHit;           // 0x388 - set by NDrone2_PreDroneControl; DroneAnim_LocationDeathAnim reads it
    float moved;                    // 0x38c - how far it moved last frame (Drone_Control): NDrone2_Steer's speed
    float unknown390;               // 0x390 - unknown3a0's length (Drone_Control)
    _VECTOR velocity;               // 0x394 - Drone_CollisionHandler moves the drone by it, NDrone2_Fall adds gravity;
                                    //         DTYPE_Astronaut: NDrone2_DealWithObjHit reverses or replaces it
    _VECTOR unknown3a0;             // 0x3a0
    float turnRate;                 // 0x3ac - NDrone2_Steer turns moveRotation by this share of the angle a frame
    _VECTOR moveRotation;           // 0x3b0 - the heading it walks at (only .y is turned)
    float animCallSpeedTaken;       // 0x3bc - animCallSpeed, when the call's row starts (DroneAnim_CallHandler)
    float yawOffset;                // 0x3c0 - added to the heading for NDrone2_Move only; cleared every frame by
                                    //         PreDroneControl (nightfire-port's name)
    char _pad3c4[0x3ca - 0x3c4];
    ushort unknown3ca;              // 0x3ca - cleared by NDrone2_DefaultInit; NDrone2_MonitorTalkToMissionObj's turn
    DYNAMICSOUNDS *speechChannel;   // 0x3cc - what the drone is saying (NDrone2_SpeechSFX); Drone_IsTalking clears it
                                    //         once it has finished
    uint unknown3d0;                // 0x3d0 - a frame: for 3 s after it, NDrone2_HitTalkExtra says nothing 1 time in 5;
                                    //         NDrone2_PlaySFX sets it to the frame of each sound it plays
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
    float soundAlertness;           // 0x40c - Sound_Alertness where it stands (DroneFunc_HandleSoundAlerts)
    float soundAlertStep;           // 0x410 - a hundredth of it, added to alertness
    float heardAlertness;           // 0x414 - the alertness of the last sound heard (DYNAMICSOUNDS.alertness)
    uchar alertStatus;              // 0x418 - Drone_AlertStatusSet
    uchar prevAlertStatus;          // 0x419 - the status before the last change
    uchar alertStatusChanged;       // 0x41a - set to 1 on a change (only Drone_AlertStatusSet writes it)
    char _pad41b;
    uint alertStatusChangedFrame;   // 0x41c - GameState.NumFramesUnpaused at the last change
    uint alertedFrame;              // 0x420 - set by DroneFunc_SetAsAttacking (a frame: inferred); 60 x FRAME_RATE_DIV
                                    //         frames on, DroneVision_ConsiderAlerted lets it alert others
    Drone_tag *alertedBy;   // 0x424 - the drone DroneVision_ConsiderAlerted found it alerted by
    float positionVisibility;       // 0x428 - the AI volumes' visibility where it stands (DroneVision_ProcessDroneSight)
    struct sAnimScript_tag *animScript; // 0x42c - DroneAnim_SetHTAnim
    struct sAnimScript_tag *unknown430; // 0x430 - an anim script; DSTATE_HangUp stops it with animScript
    void *unknown434;               // 0x434 - an anim script (A3); while set, DroneWeap_Ready2Fire and
                                    //         DroneAnim_PlayFiringAnim hold fire
    void *facialAnim;               // 0x438 - NDrone2_SpeechSFX's facial animation; the talk functions count a line
                                    //         as said when either it or the speech started
    uint impactAnimFrame;           // 0x43c - when it last played an impact anim (Punch and BulletImpact)
    DroneAnimFunc animFunc;         // 0x440 - the row's function (DroneData_AnimFunc), run with a DANIMFUNC_ phase
    DroneAnimFunc animStateFunc;    // 0x444 - the anim state's (DroneData_DAStateFunc)
    float unknown448;               // 0x448 - pi, set by DSTATE_PlayScript at the end of script 0x60007ac
    char _pad44c[0x450 - 0x44c];
    HASHCODE playScript;            // 0x450 - key 1
    DroneAnimCall animCall;         // 0x454 - the anim state asked for (DroneAnim_CallAnim)
    uchar animMode;                 // 0x464 - the playing row's completion mode (docs/drone/behaviours 6.1)
    uchar animPriority;             // 0x465 - DroneAnimStates[animState].priority
    short animState;                // 0x466 - the anim state playing
    ushort animRow;                 // 0x468 - the Drone_AnimInfo row playing
    ushort animStartFrame;          // 0x46a - the script's first frame (999 = its last)
    uint unknown46c;                // 0x46c - set by DroneAnim_SetScript; with 0x380, NDrone2_SetAngleToObj turns only
                                    //         when forced
    uint prevAnimFlags;             // 0x470 - unknown46c of the row before
    uchar animCallFlag;             // 0x474 - from the transition that started the row; cleared once no script blends
                                    //         out (unknown434)
    uchar animPlaying;              // 0x475 - a row that does not loop is playing (cleared at its script's end)
    uchar animOnce;                 // 0x476 - the row does not loop; DroneAnim_CallHandler moves on to animNextRow
    uchar animCallTaken;            // 0x477 - the call's anim state is playing; cleared by every new anim call
    HASHCODE animHashcode;          // 0x478 - the row's script
    uint unknown47c;                // 0x47c - a frame: in it, Drone_Control does not move a drone it does not animate
    uchar animEndBlend;             // 0x480 - blend frames into animNextRow
    uchar unknown481;               // 0x481 - the slot's move state (docs/drone/behaviours 6.1); DroneAnim_LocationDeathAnim
                                    //         picks a random death variant when it is set
    uchar aimMode;                  // 0x482 - DroneAnim_SetScript: 2 aims, 3 aims from half way (NDrone2_AimTest); 0
                                    //         and 0xd turn the head tracking off (our name)
    char _pad483;
    float animCallSpeed;            // 0x484 - DroneAnim_CallAnim's speed
    float animMoveSpeed;            // 0x488 - DroneAnim_SetScript; while 0, NDrone2_Steer only turns
    float animSpeed;                // 0x48c - the script's speed
    ushort animNextRow;             // 0x490 - the row played when this one ends, 0 for none
    short animEndState;             // 0x492 - DroneAnim_CallAnim's end state and message
    uint animEndMsg;                // 0x494
    uint idleAnimFrame;             // 0x498 - DroneAnim_SetStandIdleAnim / SetWalkIdleAnim: no new idle anim before
    short returnState;              // 0x49c - DSTATE_EnemyRunToPoint sets it to initialState; the Obstructed and
                                    //         AlertToPosition states read it (inferred name)
    short initialState;            // 0x49e - DroneTypeSettings; DSTATE_Global's Enter goes there
    short secondState;              // 0x4a0 - the state after NDrone2_ChangeToAttackMode
    short nodeReturnState;          // 0x4a2 - NDrone2_ReachedDestNode: the state it left for a door, kick or action
                                    //         node (our name)
    _VECTOR returnPos;              // 0x4a4 - DroneVision_EnemyAlerts keeps returnState, the feet and the cel here
    cel_tag *returnCel;             // 0x4b0   before an alert, unless unknown33 or explosiveSeen
    _VECTOR pushOffset;            // 0x4b4 - each shot pushes it 0.175 along the shot, to 0.175 at most
                                    //         (NDrone2_DealWithObjHit)
    _VECTOR prevPushOffset;         // 0x4c0 - the pushOffset NDrone2_Move added last frame, taken off again
    CelPos_tag goalFrom;            // 0x4cc - where route1 started when NDrone2_MoveToGoalPosition planned it
    char _pad4dc[0x518 - 0x4dc];
    float moveTargetDistance;       // 0x518 - Ghidra's dist2dtosomepoint (DroneMove_DistanceToNextRouteNode)
    float moveTargetRadius;         // 0x51c - NDrone2_AtDest: arrived within it (0.3 from
                                    //         NDrone2_maybeCalcDistanceAndDirToTarget; our name)
    _VECTOR moveTarget;             // 0x520 - where it moves to: the route's waypoint, or where it floats to
    cel_tag *moveTargetCel;         // 0x52c - NDrone2_DefaultInit: the object's cel
    union {
        _VECTOR moveTargetRot;      // 0x530 - NDrone2_DefaultInit: the object's rotation
        struct {
            float _unknown530;
            float angleToSomePoint; // 0x534 - Ghidra's name; DSTATE_JustStand turns it 0.01 a frame
            float _unknown538;
        };
    };
    char _pad53c[0x564 - 0x53c];
    AIPoint_tag aiPoint;            // 0x564
    _VECTOR aiPointRot;             // 0x58c - only .y is set: the alarm point's (NDrone2_FindAlarmPoint)
    char _pad598[0x61c - 0x598];
    CelPos_tag coverPos;            // 0x61c - where the cover states slide the drone to
    char _pad62c[0x630 - 0x62c];
    AIRoute_tag *activeRoute;       // 0x630 - route1 or route2 (DroneMove_NextRouteAngle)
    AIRoute_tag route1;             // 0x634 - the dynamic route
    char _pad6d4[0x6d8 - 0x6d4];
    AIRoute_tag route2;             // 0x6d8 - the patrol / mission route
    struct DroneAIPoint_tag *missionPoint;  // 0x778 - the type 8 AI point nearest the end of a mission path
                                            //         (NDrone2_AssignAIPath)
    AITarget_tag goalTarget;        // 0x77c - the target aiPoint's goal is built in (AINetwork_SetupGoalPosition)
    char _pad7ac[0x7b4 - 0x7ac];
    AITarget_tag soundTarget;       // 0x7b4 - the last sound heard (DroneFunc_HandleSoundAlerts)
    uint heardFrame;               // 0x7e4 - when it was heard
    float heardLevel;               // 0x7e8 - and how loud (Sound_Alertness's level)
    char _pad7ec[0x824 - 0x7ec];
    uchar unknown824;               // 0x824 - with unknown825 a word that NDrone2_MoveToGoalPosition hands, less the
                                    //         flags it is given, to AINetwork_CalcRouteToPosition, which does not read it
    uchar unknown825;               // 0x825 - bit 0 cleared by DSTATE_PlayScript for script 0x60007ac
    char _pad826[0x828 - 0x826];
    uint boundsPushFlags;           // 0x828 - NDrone2_Collision hands it to AINetwork_GetBoundsPushVectorForSphere
                                    //         (our name)
    obj_tag *door;                  // 0x82c - a door it is dealing with: while set, LinkCreep_Handler does not test
                                    //         the bounds; NDrone2_HandleDoor opens it, or lets go of it beyond 2 m
    ushort actionNode;              // 0x830 - the route node NDrone2_ReachedDestNode sent it to a door, kick or action
                                    //         state from (our name)
    ushort waitNode;                // 0x832 - the route node it last waited at (0xffff once it has; our name)
    uint doorFrame;                 // 0x834 - 60 x FRAME_RATE_DIV frames after it, NDrone2_HandleDoor acts (our name)
    struct CoverNode_tag *coverNode; // 0x838 - the cover node it holds (DroneAnim_CoverAnim)
    ushort coverFlags;              // 0x83c - DRONE_COVER_* (NDrone2_Cover.h): the corner's side, the cover lost, left
    uchar coverStep;                // 0x83e - DroneAnim_CoverAnim's step: 0 enter, 1 idle, 2 aim, 3 fire, 4 return,
                                    //         5 leave
    uchar coverMode;                // 0x83f - at a corner: 0/1 crouched lean / step out, 2/3 standing, 4 the corner as
                                    //         low cover
    short clip;                     // 0x840 - rounds left in the clip
    short clipSize;                 // 0x842
    short burstShots;               // 0x844 - left in this burst
    char _pad846[0x848 - 0x846];
    uint nextShotFrame;             // 0x848
    uint lastShotFrame;             // 0x84c - the last shot, plus the pause before the burst after it
    ushort grenadeWeapon;           // 0x850 - the weapon_data entry DroneWeap_ThrowGrenade throws
    ushort grenades;                // 0x852 - how many are left
    float unknown854;               // 0x854 - 0.5 from NDrone2_DefaultInit
    HASHCODE deathScript;           // 0x858 - key 13; set: DSTATE_Death_Anim and DeathByExplosion go to
                                    //         SpecialDeath_Anim
    _VECTOR scriptedMovePos;        // 0x85c - with unknown1a, DroneAnim_PreTransCallback adds the root's moves here
    _VECTOR scriptedMoveRot;        // 0x868 - with a death script, NDrone2_DefaultInit starts it and scriptedMovePos
                                    //         at the object's rotation and position
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
static_assert(sizeof(StateMachineInfo_tag) == 0x2c, "StateMachineInfo_tag is 0x2c bytes");
static_assert(offsetof(StateMachineInfo_tag, stateParam) == 0x24, "Wrong offset for stateParam");
static_assert(sizeof(DroneAnimCall) == 0x10, "DroneAnimCall is 0x10 bytes");
static_assert(sizeof(DroneTimer) == 0xc, "DroneTimer is 0xc bytes");
static_assert(offsetof(DroneImpact_tag, source) == 0x14, "Wrong offset for DroneImpact_tag.source");
static_assert(sizeof(DIVars_tag) == 0xa4, "DIVars is wrong size");
static_assert(sizeof(DCVars_tag) == 0x10, "DCVars is wrong size");
static_assert(sizeof(MsgObject) == 0x1c, "MsgObject is 0x1c bytes");
static_assert(sizeof(DroneCreationData) == 0xb0, "DroneCreationData is 0xb0 bytes");
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
static_assert(offsetof(Drone_tag, bunchFlags) == 0x290, "Wrong offset for bunchFlags");
static_assert(offsetof(Drone_tag, door) == 0x82c, "Wrong offset for door");
static_assert(offsetof(Drone_tag, opponentLastRot) == 0x15c, "Wrong offset for opponentLastRot");
static_assert(offsetof(Drone_tag, seenFromPos) == 0x1dc, "Wrong offset for seenFromPos");
static_assert(offsetof(Drone_tag, unknown230) == 0x230, "Wrong offset for unknown230");
static_assert(offsetof(Drone_tag, unknown23c) == 0x23c, "Wrong offset for unknown23c");
static_assert(offsetof(Drone_tag, inPlayersSightsFrames) == 0x248, "Wrong offset for inPlayersSightsFrames");
static_assert(offsetof(Drone_tag, unknown28c) == 0x28c, "Wrong offset for unknown28c");
static_assert(offsetof(Drone_tag, unknown3a0) == 0x3a0, "Wrong offset for unknown3a0");
static_assert(offsetof(Drone_tag, yawOffset) == 0x3c0, "Wrong offset for yawOffset");
static_assert(offsetof(Drone_tag, unknown47c) == 0x47c, "Wrong offset for unknown47c");
static_assert(offsetof(Drone_tag, doorFrame) == 0x834, "Wrong offset for doorFrame");
static_assert(offsetof(Drone_tag, actionNode) == 0x830, "Wrong offset for actionNode");
static_assert(offsetof(Drone_tag, coverNode) == 0x838, "Wrong offset for coverNode");
static_assert(offsetof(Drone_tag, coverMode) == 0x83f, "Wrong offset for coverMode");
static_assert(offsetof(Drone_tag, turnedToAlert) == 0x34, "Wrong offset for turnedToAlert");
static_assert(offsetof(Drone_tag, heardNoiseStage) == 0x43, "Wrong offset for heardNoiseStage");
static_assert(offsetof(Drone_tag, atCornerCover) == 0x2e, "Wrong offset for atCornerCover");
static_assert(offsetof(Drone_tag, guardZone) == 0x24c, "Wrong offset for guardZone");
static_assert(offsetof(Drone_tag, guardPlanes) == 0x254, "Wrong offset for guardPlanes");
static_assert(offsetof(Drone_tag, coverPos) == 0x61c, "Wrong offset for coverPos");
static_assert(offsetof(Drone_tag, nodeReturnState) == 0x4a2, "Wrong offset for nodeReturnState");
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
static_assert(offsetof(Drone_tag, animCallTaken) == 0x477, "Wrong offset for animCallTaken");
static_assert(offsetof(Drone_tag, rootHeightSet) == 0x27, "Wrong offset for rootHeightSet");
static_assert(offsetof(Drone_tag, secondWeapon) == 0x45, "Wrong offset for secondWeapon");
static_assert(offsetof(Drone_tag, animRootHeight) == 0x8c, "Wrong offset for animRootHeight");
static_assert(offsetof(Drone_tag, animCallSpeedTaken) == 0x3bc, "Wrong offset for animCallSpeedTaken");
static_assert(offsetof(Drone_tag, animFunc) == 0x440, "Wrong offset for animFunc");
static_assert(offsetof(Drone_tag, animPriority) == 0x465, "Wrong offset for animPriority");
static_assert(offsetof(Drone_tag, animStartFrame) == 0x46a, "Wrong offset for animStartFrame");
static_assert(offsetof(Drone_tag, prevAnimFlags) == 0x470, "Wrong offset for prevAnimFlags");
static_assert(offsetof(Drone_tag, animHashcode) == 0x478, "Wrong offset for animHashcode");
static_assert(offsetof(Drone_tag, animEndBlend) == 0x480, "Wrong offset for animEndBlend");
static_assert(offsetof(Drone_tag, animSpeed) == 0x48c, "Wrong offset for animSpeed");
static_assert(offsetof(Drone_tag, animNextRow) == 0x490, "Wrong offset for animNextRow");
static_assert(offsetof(Drone_tag, scriptedMovePos) == 0x85c, "Wrong offset for scriptedMovePos");
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
static_assert(offsetof(Drone_tag, deathScript) == 0x858, "Wrong offset for deathScript");
static_assert(offsetof(Drone_tag, scriptedMoveRot) == 0x868, "Wrong offset for scriptedMoveRot");
static_assert(offsetof(Drone_tag, unknown854) == 0x854, "Wrong offset for unknown854");
static_assert(offsetof(Drone_tag, moveTargetCel) == 0x52c, "Wrong offset for moveTargetCel");
static_assert(offsetof(Drone_tag, moveTargetRot) == 0x530, "Wrong offset for moveTargetRot");
static_assert(offsetof(Drone_tag, unknownb4) == 0xb4, "Wrong offset for unknownb4");
static_assert(offsetof(Drone_tag, unknown2d) == 0x2d, "Wrong offset for unknown2d");
static_assert(offsetof(Drone_tag, unknown3ca) == 0x3ca, "Wrong offset for unknown3ca");
static_assert(offsetof(Drone_tag, armedPersonRadius) == 0x950, "Wrong offset for armedPersonRadius");
static_assert(offsetof(Drone_tag, unknown21) == 0x21, "Wrong offset for unknown21");
static_assert(offsetof(Drone_tag, burstOver) == 0x3c, "Wrong offset for burstOver");
static_assert(offsetof(Drone_tag, unknown41) == 0x41, "Wrong offset for unknown41");
static_assert(offsetof(Drone_tag, opponentLastPos) == 0x150, "Wrong offset for opponentLastPos");
static_assert(offsetof(Drone_tag, opponentDelta) == 0x16c, "Wrong offset for opponentDelta");
static_assert(offsetof(Drone_tag, opponentFacing) == 0x184, "Wrong offset for opponentFacing");
static_assert(offsetof(Drone_tag, targetMoving) == 0x190, "Wrong offset for targetMoving");
static_assert(offsetof(Drone_tag, targetPos) == 0x19c, "Wrong offset for targetPos");
static_assert(offsetof(Drone_tag, targetChangeFrame) == 0x1c4, "Wrong offset for targetChangeFrame");
static_assert(offsetof(Drone_tag, unknown434) == 0x434, "Wrong offset for unknown434");
static_assert(offsetof(Drone_tag, unknown46c) == 0x46c, "Wrong offset for unknown46c");
static_assert(offsetof(Drone_tag, clip) == 0x840, "Wrong offset for clip");
static_assert(offsetof(Drone_tag, burstShots) == 0x844, "Wrong offset for burstShots");
static_assert(offsetof(Drone_tag, nextShotFrame) == 0x848, "Wrong offset for nextShotFrame");
static_assert(offsetof(Drone_tag, grenades) == 0x852, "Wrong offset for grenades");
static_assert(offsetof(Drone_tag, aimByEvent) == 0x1f, "Wrong offset for aimByEvent");
static_assert(offsetof(Drone_tag, gunThroughWall) == 0x1e, "Wrong offset for gunThroughWall");
static_assert(offsetof(Drone_tag, seenPlayerLogged) == 0x42, "Wrong offset for seenPlayerLogged");
static_assert(offsetof(Drone_tag, sightTestFrame) == 0xb8, "Wrong offset for sightTestFrame");
static_assert(offsetof(Drone_tag, viewRange) == 0xcc, "Wrong offset for viewRange");
static_assert(offsetof(Drone_tag, damageScale) == 0xe4, "Wrong offset for damageScale");
static_assert(offsetof(Drone_tag, viewBone) == 0x18e, "Wrong offset for viewBone");
static_assert(offsetof(Drone_tag, opponentVisibility) == 0x1d0, "Wrong offset for opponentVisibility");
static_assert(offsetof(Drone_tag, opponentSeenPos) == 0x1e8, "Wrong offset for opponentSeenPos");
static_assert(offsetof(Drone_tag, opponentSeenRot) == 0x1f4, "Wrong offset for opponentSeenRot");
static_assert(offsetof(Drone_tag, secondEyeFlags) == 0x2dc, "Wrong offset for secondEyeFlags");
static_assert(offsetof(Drone_tag, secondEyeCel) == 0x2f4, "Wrong offset for secondEyeCel");
static_assert(offsetof(Drone_tag, moveQuad) == 0x2fa, "Wrong offset for moveQuad");
static_assert(offsetof(Drone_tag, alertedBy) == 0x424, "Wrong offset for alertedBy");
static_assert(offsetof(Drone_tag, positionVisibility) == 0x428, "Wrong offset for positionVisibility");
static_assert(offsetof(Drone_tag, aiming) == 0x22, "Wrong offset for aiming");
static_assert(offsetof(Drone_tag, smallCollision) == 0x2f, "Wrong offset for smallCollision");
static_assert(offsetof(Drone_tag, rootHeight) == 0x84, "Wrong offset for rootHeight");
static_assert(offsetof(Drone_tag, headMode) == 0x2b8, "Wrong offset for headMode");
static_assert(offsetof(Drone_tag, collision) == 0x2fc, "Wrong offset for collision");
static_assert(offsetof(Drone_tag, animScript) == 0x42c, "Wrong offset for animScript");
static_assert(offsetof(Drone_tag, aimMode) == 0x482, "Wrong offset for aimMode");
static_assert(offsetof(Drone_tag, boundsPushFlags) == 0x828, "Wrong offset for boundsPushFlags");
static_assert(offsetof(Drone_tag, unknown33) == 0x33, "Wrong offset for unknown33");
static_assert(offsetof(Drone_tag, facialAnimDone) == 0x35, "Wrong offset for facialAnimDone");
static_assert(offsetof(Drone_tag, unknown37) == 0x37, "Wrong offset for unknown37");
static_assert(offsetof(Drone_tag, nearArmedDrone) == 0x238, "Wrong offset for nearArmedDrone");
static_assert(offsetof(Drone_tag, soundAlertness) == 0x40c, "Wrong offset for soundAlertness");
static_assert(offsetof(Drone_tag, heardAlertness) == 0x414, "Wrong offset for heardAlertness");
static_assert(offsetof(Drone_tag, returnPos) == 0x4a4, "Wrong offset for returnPos");
static_assert(offsetof(Drone_tag, returnCel) == 0x4b0, "Wrong offset for returnCel");
static_assert(offsetof(Drone_tag, soundTarget) == 0x7b4, "Wrong offset for soundTarget");
static_assert(offsetof(Drone_tag, heardFrame) == 0x7e4, "Wrong offset for heardFrame");
static_assert(offsetof(Drone_tag, heardLevel) == 0x7e8, "Wrong offset for heardLevel");
static_assert(offsetof(Drone_tag, floatsToMoveTarget) == 0x24, "Wrong offset for floatsToMoveTarget");
static_assert(offsetof(Drone_tag, floatAcceleration) == 0x50, "Wrong offset for floatAcceleration");
static_assert(offsetof(Drone_tag, avoidingPlayer) == 0x25, "Wrong offset for avoidingPlayer");
static_assert(offsetof(Drone_tag, nearestDroneDist) == 0x284, "Wrong offset for nearestDroneDist");
static_assert(offsetof(Drone_tag, nearestDrone) == 0x288, "Wrong offset for nearestDrone");
static_assert(offsetof(Drone_tag, boundaryFlags) == 0x294, "Wrong offset for boundaryFlags");
static_assert(offsetof(Drone_tag, moved) == 0x38c, "Wrong offset for moved");
static_assert(offsetof(Drone_tag, turnRate) == 0x3ac, "Wrong offset for turnRate");
static_assert(offsetof(Drone_tag, moveRotation) == 0x3b0, "Wrong offset for moveRotation");
static_assert(offsetof(Drone_tag, animMoveSpeed) == 0x488, "Wrong offset for animMoveSpeed");
static_assert(offsetof(Drone_tag, prevPushOffset) == 0x4c0, "Wrong offset for prevPushOffset");
static_assert(offsetof(Drone_tag, moveTargetDistance) == 0x518, "Wrong offset for moveTargetDistance");
static_assert(offsetof(Drone_tag, moveTarget) == 0x520, "Wrong offset for moveTarget");
static_assert(offsetof(Drone_tag, activeRoute) == 0x630, "Wrong offset for activeRoute");
static_assert(offsetof(Drone_tag, goalTarget) == 0x77c, "Wrong offset for goalTarget");
static_assert(offsetof(Drone_tag, goalFrom) == 0x4cc, "Wrong offset for goalFrom");
static_assert(offsetof(Drone_tag, aiPointRot) == 0x58c, "Wrong offset for aiPointRot");
static_assert(offsetof(Drone_tag, missionPoint) == 0x778, "Wrong offset for missionPoint");
static_assert(offsetof(Drone_tag, unknown824) == 0x824, "Wrong offset for unknown824");
static_assert(offsetof(Drone_tag, attackModeTaken) == 0x28, "Wrong offset for attackModeTaken");
static_assert(offsetof(Drone_tag, chatSwitchChannel) == 0x121, "Wrong offset for chatSwitchChannel");
static_assert(offsetof(Drone_tag, baseAlertness2) == 0x400, "Wrong offset for baseAlertness2");
static_assert(offsetof(Drone_tag, behaviourClass) == 0xab, "Wrong offset for behaviourClass");
static_assert(offsetof(Drone_tag, interrogateDistance) == 0x208, "Wrong offset for interrogateDistance");
static_assert(offsetof(Drone_tag, interrogateFrame) == 0x210, "Wrong offset for interrogateFrame");
static_assert(offsetof(Drone_tag, interrogatePos) == 0x218, "Wrong offset for interrogatePos");
static_assert(offsetof(Drone_tag, unknown430) == 0x430, "Wrong offset for unknown430");
static_assert(offsetof(StateMachineInfo_tag, pathDistance) == 0x20, "Wrong offset for pathDistance");

// Set by Drone_InitComms / ResetMap_GameInit (still the game's); while set, no drones are created and
// Drone_SM_SendMsg / Drone_Message send nothing.
#define Drone_bDisableSystem (*(uchar *)0x001dfa2a)

bool Drone_DCVfromOBJ(obj_tag* obj, DCVars_tag *dcVars);
void Drone_BrokenObject(obj_tag *obj, HITDATA_tag *hit);
obj_tag* Drone_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
bool Drone_PositionVisible(_VECTOR *pos, cel_tag *cel);
void Drone_SM_RouteMsg(MsgObject *msg);
// The dispatcher. The original takes dcVars in ESI: the game reaches the adaptor, C++ calls the core (DroneSM.cpp).
void _Drone_SM_RouteMsgDCV(DCVars_tag *dcVars, MsgObject *msg);
void Drone_SM_RouteMsgDCV(void);
void Drone_EnableAll(char enable, HASHCODE hashcode);
obj_tag* Drone_CoderCreate(_VECTOR *pos, _VECTOR *rot, HASHCODE skin, HASHCODE playScript, uint unused, uint weapon, ushort initialState);
void Drone_PreLoad_Init(void);
void Drone_LevelReset(void);
void Drone_PostLoad_Init(void);
void Drone_UpdateCombatChannels(void);
void Drone_InitComms(void);
void Drone_GetOpponentInfo(Drone_tag *drone, obj_tag *gameObj);
bool NDrone2_PreDroneControl(DCVars_tag *dcv);
void NDrone2_HandleDoor(DCVars_tag *dcv);
bool NDrone2_ControlSTANDARD(DCVars_tag *dcv);
bool NDrone2_ControlDTYPE_Zoe(DCVars_tag *dcv);
bool NDrone2_ControlDTYPE_Ninja(DCVars_tag *dcv);
bool NDrone2_ControlDTYPE_Astronaut(DCVars_tag *dcv);
bool NDrone2_PostDroneControl(DCVars_tag *dcv);
bool NDrone2_DoAnimation(Drone_tag *drone);
void Drone_Control(obj_tag *gameObj);
void Drone_Delete(obj_tag *gameObj);

#endif // DRONE_H_