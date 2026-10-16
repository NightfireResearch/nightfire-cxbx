#ifndef NDRONE2_H
#define NDRONE2_H

#include "../../actionhelpers.h"
#include "Drone.h"

// From PS2, we can see a table of function pointers with this name, and NDrone2_ProcessStateMachine just runs one according to the drone's current state
// Each name is that of the function in its slot of the PS2 table (NDrone2_StateFuncs at 0x0029c120; the names are the
// developers' own, from the mangled symbols in the ELF's .strtab). The enum's own names are in no build.
typedef enum {
    DSTATE_Global = 0,
    DSTATE_WaitSwitch,
    DSTATE_Disabled,
    DSTATE_PlayScript,
    DSTATE_Idle,
    DSTATE_Alert,
    DSTATE_InitPatrol,
    DSTATE_Patrol,
    DSTATE_HostageKiller,
    DSTATE_HostageKillerAttack,
    DSTATE_Hostage, // 10
    DSTATE_HostageDie,
    DSTATE_HostageSaved,
    DSTATE_HostageIdle,
    DSTATE_HostageHide,
    DSTATE_HostageDead,
    DSTATE_CivilianInit,
    DSTATE_Civilian,
    DSTATE_CivilianScared,
    DSTATE_CivilianHiding,
    DSTATE_CivilianPatrol, // 20
    DSTATE_ReturnToPatrolPath,
    DSTATE_CivilianMission,
    DSTATE_CivilianMissionWait,
    DSTATE_StandBlind,
    DSTATE_KikoMission,
    DSTATE_KikoMissionRun,
    DSTATE_EnemyMission,
    DSTATE_AllyLeadInit,
    DSTATE_AllyLead,
    DSTATE_AllyLeadPlayerInWay, // 30
    DSTATE_AllyLeadHide,
    DSTATE_AllyLeadWait,
    DSTATE_AllyLeadMissionWait,
    DSTATE_AllyLeadBondCombat,
    DSTATE_AllyLeadDone, // 35
    DSTATE_AllyFollowInit,
    DSTATE_AllyFollow,
    DSTATE_AllyFollowWait,
    DSTATE_AllyFollowDone, // 39
    DSTATE_AllyGoToGoalPosition, // 40
    DSTATE_SniperIdle,
    DSTATE_SniperAim,
    DSTATE_SniperFire,
    DSTATE_SniperReload,
    DSTATE_GrenadeThrow,
    DSTATE_PartyGirlInit,
    DSTATE_PartyGirl,
    DSTATE_CivilianGuard,
    DSTATE_CivilianDoorGuard,
    DSTATE_TruckDriverInit, // 50
    DSTATE_TruckDriverInitAlert,
    DSTATE_TruckDriverIdle,
    DSTATE_TruckDriverMission,
    DSTATE_CastleChatGuard1,
    DSTATE_AmbushInit,
    DSTATE_AmbushWait,
    DSTATE_InterogateAssist, // all misspellings of "interrogate" are intentional to match the original code
    DSTATE_InterogateAssistWait,
    DSTATE_Interogator,
    DSTATE_Interogate, // 60
    DSTATE_InterogateWalk,
    DSTATE_CivilianChallenge,
    DSTATE_Surrender_Anim,
    DSTATE_Surrendered,
    DSTATE_Unsurrender_Anim,
    DSTATE_KnockedOut_Anim,
    DSTATE_Knocked_Out,
    DSTATE_Death_Anim,
    DSTATE_DeathByExplosion,
    DSTATE_SpecialDeath_Anim, // 70
    DSTATE_Dead,
    DSTATE_Fade,
    DSTATE_FadeFast,
    DSTATE_Taser,
    DSTATE_Stunned,
    DSTATE_Stunned_Recover,
    DSTATE_StunGrenadeImpact,
    DSTATE_StunGrenadeLoop,
    DSTATE_StunGrenadeRecover,
    DSTATE_StunDartImpact, // 80
    DSTATE_StunDartLoop,
    DSTATE_StunDartRecover,
    DSTATE_PunchImpact,
    DSTATE_ExplosiveImpact,
    DSTATE_BulletImpact,
    DSTATE_Attack,
    DSTATE_Alerted1stEncounter,
    DSTATE_Combat,
    DSTATE_CombatNoMove,
    DSTATE_CombatOutOfRange, // 90
    DSTATE_CombatNewSighting,
    DSTATE_CombatNoSight,
    DSTATE_CombatTooClose,
    DSTATE_CombatWait,
    DSTATE_CombatNoRoute,
    DSTATE_NoOpponent,
    DSTATE_Obstructed,
    DSTATE_AlertToPosition,
    DSTATE_GoToGoalPosition,
    DSTATE_HostageGoToGoalPosition, // 100
    DSTATE_SearchArea,
    DSTATE_DrawWeapon,
    DSTATE_AimStand,
    DSTATE_AimStandFire,
    DSTATE_AimStandReload,
    DSTATE_AimStandDiscard,
    DSTATE_Prone,
    DSTATE_ProneFire,
    DSTATE_AimBackoff,
    DSTATE_CrouchCover, // 110
    DSTATE_AimCrouch,
    DSTATE_AimCrouchFire,
    DSTATE_AimCrouchReload,
    DSTATE_AltAttack,
    DSTATE_StepAimLeft,
    DSTATE_StepAimRight,
    DSTATE_StrafeAimLeft,
    DSTATE_StrafeAimRight,
    DSTATE_StrafeDodgeLeft,
    DSTATE_StrafeDodgeRight, // 120
    DSTATE_RollLeftCrouch,
    DSTATE_RollRightCrouch,
    DSTATE_SmokedOut,
    DSTATE_SmokedOut_Loop,
    DSTATE_SmokedOut_Recover,
    DSTATE_Investigate,
    DSTATE_DroneStuck,
    DSTATE_HoldItRightThere,
    DSTATE_OpenDoor,
    DSTATE_KickObject, // 130
    DSTATE_ActionAnim,
    DSTATE_StandFiddle,
    DSTATE_EnemyRunToPoint,
    DSTATE_RunAwayFromObject,
    DSTATE_HideFromScaryObject,
    DSTATE_RecoverFromScaryObject,
    DSTATE_RunToAlarm,
    DSTATE_PressAlarm,
    DSTATE_DonePressAlarm, // 139
    DSTATE_RunForCover, // 140
    DSTATE_ElevatorJumper,
    DSTATE_AbseilInit,
    DSTATE_AbseilSlide,
    DSTATE_AbseilHang,
    DSTATE_AbseilStepOff,
    DSTATE_AbseilDeath,
    DSTATE_UnderCoverInit,
    DSTATE_UnderCoverIdle,
    DSTATE_UnderCoverAim,
    DSTATE_UnderCoverFire, // 150
    DSTATE_UnderCoverSniperFire,
    DSTATE_UnderCoverSniperReload,
    DSTATE_UnderCoverReturn,
    DSTATE_UnderCoverTypeChange,
    DSTATE_UnderCoverLeave,
    DSTATE_UnderCoverLeaveNow,
    DSTATE_SeenOpponent,
    DSTATE_SeenDeadBody,
    DSTATE_SeenSurrenderedDrone,
    DSTATE_SeenDroneShot, // 160
    DSTATE_SeenExplosive,
    DSTATE_HeardNoise,
    DSTATE_HeardNoiseAware,
    DSTATE_HeardNoiseSuspect,
    DSTATE_HeardNoiseAlert,
    DSTATE_NinjaStand,
    DSTATE_NinjaAttack,
    DSTATE_NinjaAttackLongRange,
    DSTATE_NinjaAttackMidRange,
    DSTATE_NinjaAttackShortRange, // 170
    DSTATE_NinjaGetCloseToPlayer, // 171
    DSTATE_NinjaSword,
    DSTATE_NinjaSomersault,
    DSTATE_NinjaBackflip,
    DSTATE_NinjaSideflip,
    DSTATE_NinjaSideflipLeft,
    DSTATE_NinjaSideflipRight,
    DSTATE_NinjaStandFire,
    DSTATE_NinjaNoRoute,
    DSTATE_DeleteMe, // 180
    DSTATE_FailMission,
    DSTATE_JustStand,
    DSTATE_Tester1,
    DSTATE_Tester2,
    DSTATE_Tester3,
    DSTATE_Tester4,
    DSTATE_HangUp,
    DSTATE_WaitForever,
    DSTATE_AstronautLaunch,
    DSTATE_AstronautHit, // 190
    DSTATE_AstronautDeath,
    DSTATE_AstronautCombat,
    DSTATE_AstronautCombatMove,
    DSTATE_SpaceDrake,
    DSTATE_BotInit,
    DSTATE_BotRespawn,
    DSTATE_BotGlobal,
    DSTATE_BotCollector,
    DSTATE_BotGuardian,
    DSTATE_BotTeamPlayer, // 200
    DSTATE_BotBully,
    DSTATE_BotBerserker,
    DSTATE_BotGreedy,
    DSTATE_BotVengeful,
    DSTATE_BotJudge,
    DSTATE_BotAssassin,
    DSTATE_BotAttack,
    DSTATE_BotAttackRun,
    DSTATE_BotAttackNoRoute,
    DSTATE_BotAttackFire, // 210
    DSTATE_BotAttackNoOpponent,
    DSTATE_BotAttackStrafeAimLeft,
    DSTATE_BotAttackStrafeAimRight,
    DSTATE_BotAttackRunChangePosition,
    DSTATE_BotAttackBackoff,
    DSTATE_BotAttackCrouch,
    DSTATE_BotAttackRollLeftCrouch,
    DSTATE_BotAttackRollRightCrouch,
    DSTATE_BotAttackStepAimLeft,
    DSTATE_BotAttackStepAimRight, // 220
    DSTATE_BotAttackReload,
    DSTATE_BotAttackChangeWeapon,
    DSTATE_BotAttackUnarmed,
    DSTATE_BotCoverRunTo,
    DSTATE_BotCoverInit,
    DSTATE_BotCoverIdle,
    DSTATE_BotCoverAim,
    DSTATE_BotCoverFire,
    DSTATE_BotCoverReturn,
    DSTATE_BotCoverTypeChange, // 230
    DSTATE_BotCoverLeave,
    DSTATE_BotCoverLeaveNow,
    DSTATE_BotStuck,
    DSTATE_BotAlertToPosition,
    DSTATE_BotGotoGoalPosition,
    DSTATE_BotSeenOpponent,
    DSTATE_BotSeenDroneShot,
    DSTATE_BotHeardNoise,
    DSTATE_BotImpactBullet,
    DSTATE_BotImpactExplosive, // 240
    DSTATE_BotImpactPunch,
    DSTATE_BotDeathAnim,
    DSTATE_BotDeathByExplosion,
    DSTATE_BotDead,
    DSTATE_BotImpactStunGrenade,
    DSTATE_BotDoorOpen,
    DSTATE_BotGuardFriendIdle,
    DSTATE_BotGuardFriendFollow,
    DSTATE_BotIdle,
} DSTATE;

// A drone mode: placement key 5 when below 0x24, indexing DroneModeSettings (NDrone2_DoModeSettingsOLD). Names are the
// PS2 build's (its init_DMODE_* functions, in table order); 31, 32, 34 and 35 have no init function to name them.
typedef enum {
    DMODE_Normal, // 0
    DMODE_Guard,
    DMODE_Retreater,
    DMODE_Sniper,
    DMODE_Stealth,
    DMODE_Attacker, // 5
    DMODE_RunToPoint,
    DMODE_Assassin,
    DMODE_HostageKiller,
    DMODE_Hostage,
    DMODE_HostageTied, // 10
    DMODE_JustStand4Demo,
    DMODE_DeleteMe,
    DMODE_Civilian,
    DMODE_CivilianScared,
    DMODE_MissionFailer, // 15
    DMODE_Mayhew,
    DMODE_Ninja,
    DMODE_AlarmRaiser,
    DMODE_SearchLight,
    DMODE_Ambush, // 20
    DMODE_Zoe,
    DMODE_PartyGirl,
    DMODE_CivilianGuard,
    DMODE_Interogator,
    DMODE_CivDoorGuard, // 25
    DMODE_TruckDriver,
    DMODE_CastleChatGuard1,
    DMODE_CastleChatGuard2,
    DMODE_SniperAlert,
    DMODE_PartyGirlLooker, // 30
    DMODE_Unnamed31,
    DMODE_Unnamed32,
    DMODE_Bot,
    DMODE_Unnamed34,
    DMODE_Unnamed35, // 35
    DMODE_COUNT
} DMODE;

// A drone type: Drone_tag.dtype, indexing DroneTypeSettings (initial and second state, init and control function).
// INVENTED NAMES, not canonical: inferred from the modes that map to them and the states they start in (the PS2
// build has no names for them). Types 31-84 are one per bot state (196..249) and look unused.
typedef enum {
    DTYPE_Normal, // 0
    DTYPE_Guard,
    DTYPE_Sniper,
    DTYPE_Assassin,
    DTYPE_HostageKiller,
    DTYPE_Hostage, // 5
    DTYPE_Attacker,
    DTYPE_RunToPoint,
    DTYPE_JustStand,
    DTYPE_Civilian,
    DTYPE_CivilianScared, // 10
    DTYPE_MissionFailer,
    DTYPE_Mayhew,
    DTYPE_Ninja,
    DTYPE_AlarmRaiser,
    DTYPE_SearchLight, // 15
    DTYPE_Ambush,
    DTYPE_Zoe,
    DTYPE_PartyGirl,
    DTYPE_CivilianGuard,
    DTYPE_Interogator, // 20
    DTYPE_DeleteMe,
    DTYPE_CivDoorGuard,
    DTYPE_TruckDriver,
    DTYPE_CastleChatGuard,
    DTYPE_SniperAlert, // 25
    DTYPE_Unnamed26,
    DTYPE_Abseil27,
    DTYPE_Abseil28,
    DTYPE_Astronaut,
    DTYPE_Bot, // 30
    DTYPE_COUNT = 85
} DTYPE;

// State machine messages (MsgObject.msgType). INVENTED NAMES, not canonical: from what sends and handles each
// (docs/drone/architecture/README.md 3.2). Ids not listed are tested by states but their senders were not found.
typedef enum {
    DRONE_MSG_Null = 0,             // every state answers "handled"; nothing sends it
    DRONE_MSG_Enter = 1,
    DRONE_MSG_Exit = 2,
    DRONE_MSG_Update = 3,           // once per frame, from NDrone2_ControlSTANDARD
    DRONE_MSG_StateTimeout = 4,     // scoped to the state that set it
    DRONE_MSG_ScriptEnd = 5,        // the drone's own, after an animation script ends
    DRONE_MSG_ImpactPunch = 6,      // extraData = the hit record
    DRONE_MSG_ImpactTaser = 7,
    DRONE_MSG_ImpactBullet = 8,
    DRONE_MSG_ImpactExplosive = 9,
    DRONE_MSG_AttackNow = 0xa,
    DRONE_MSG_ToHostages = 0xb,     // scope = DSTATE_Hostage
    DRONE_MSG_TimerA = 0xc,
    DRONE_MSG_TimerB = 0xd,
    DRONE_MSG_Enable = 0xe,         // leave WaitSwitch
    DRONE_MSG_OpponentSighted = 0xf,
    DRONE_MSG_HeardNoise = 0x14,
    DRONE_MSG_DroneAlert = 0x15,    // a dead body etc.; broadcast
    DRONE_MSG_ImpactSmoke = 0x17,
    DRONE_MSG_ImpactStunGrenade = 0x18,
    DRONE_MSG_ImpactStunDart = 0x19,
    DRONE_MSG_TalkToMissionObject = 0x1a,
    DRONE_MSG_ForceState = 0x1d,    // extraData = the DSTATE
    DRONE_MSG_GlobalAlarm = 0x1e,   // every 30 frames while switch 0x96 is on
    DRONE_MSG_ConsideredAlerted = 0x1f,
    DRONE_MSG_AnimEvent = 0x20,
    DRONE_MSG_ExplosiveSeen = 0x21, // extraData = the object
    DRONE_MSG_ObjectHit = 0x22,     // non-damaging (inferred)
    DRONE_MSG_BotStateChanged = 0x2e, // to BotGlobal; scope = the new state
    // 0x2f-0x45: bot messages (docs/drone/bots-and-navigation)
} DRONE_MSG;


// A point drones take notice of (NPCGlobals.awarePoints, built each frame by Drone_BuildDynamicAwarePoints).
// Only the fields our code reads.
typedef struct DroneAwarePoint {
    uchar type;                 // +0x0 - 2: grenades and the like (DroneFunc_HandleExplosives)
    char _unknown01[7];
    obj_tag *obj;               // +0x8
    _VECTOR pos;                // +0xc
    char _unknown18[4];
    float radius;               // +0x1c
} DroneAwarePoint;
static_assert(sizeof(DroneAwarePoint) == 0x20, "DroneAwarePoint is 0x20 bytes");

#define MAX_AWARE_POINTS 64

// A target drones share (NPCGlobals.targets, handed out by DroneFunc_AllocateTargetID; Drone_tag.targetSlot)
typedef struct DroneTarget_tag {
    uchar flags;                // +0x0 - with neither bit 0 nor 1 set, Drone_IsCoverNodeUsable counts only nodes a
    char _pad1[3];              //        drone of this target holds
    obj_tag *obj;               // +0x4
    char _unknown8[4];
    _VECTOR pos;                // +0xc
} DroneTarget_tag;
static_assert(sizeof(DroneTarget_tag) == 0x18, "DroneTarget_tag is 0x18 bytes");

#define MAX_DRONE_TARGETS 8

// The level's cover nodes and AI points (Drone_AIHints.h)
struct CoverNode_tag;
struct DroneAIPoint_tag;

// The NPC system's globals: one block that Drone_LevelReset clears whole (0x142c bytes). Only what our code uses
// is named so far; FUN_00030e70 and
// Drone_PostLoad_Init clear the aware points at +0xb54 and an array at +0x1354 (0x28).
typedef struct {
    uchar enabled;              // +0x0 - Drone_EnableAll's last setting
    char _pad1[3];
    Drone_tag *NDrone2List;     // +0x4 - every live drone, linked through Drone_tag.next
    char _pad8[0x148 - 0x8];
    uint deadFrames;            // +0x148 - how long a drone lies in DSTATE_Dead before it fades (our name)
    uint unknown14c;            // +0x14c - DroneFunc_DeadDrone: with more than this in unknown150, the drone whose
    uint unknown150;            // +0x150   sm.id is unknown15c fades fast
    char _pad154[0x15c - 0x154];
    uint unknown15c;            // +0x15c
    char _pad160[0x18c - 0x160];
    uint seenAttacking;         // +0x18c - NDrone2_SeenAndAttacking counts each drone it lets through
    char _pad190[0x19c - 0x190];
    uint32_t hostagesSaved;     // +0x19c
    uchar dummyPlayerActive;    // +0x1a0 - FUN_00030950 (from Drone_InitComms) follows the cheat byte 0x1dfa29
                                //          (C_CHCHDUMMY): on, drones targeting glb_players[0] retarget dummyPlayer
    char _pad1a1[0x1a4 - 0x1a1];
    obj_tag *dummyPlayer;       // +0x1a4 - DroneSpawner_CreateBlank's object; moved onto glb_players[0] when switched on
    char _pad1a8[0x1ac - 0x1a8];
    uint32_t enabledFrame;      // +0x1ac - GameState.NumFramesUnpaused when Drone_EnableAll last enabled the drones
    uint32_t impactAnimFrame;   // +0x1b0 - when any drone last played an impact anim (Punch and BulletImpact)
    uint32_t nextPatrolTalkFrame;   // +0x1b4 - NDrone2_PatrolTalk: no drone mutters before
    char _pad1b8[0x1bc - 0x1b8];
    uint32_t lastPowerstationChannel;   // +0x1bc - NDrone2_PowerstationTalk: the last channel whose line was said
    AITarget_tag playerTarget;  // +0x1c0 - the player's feet, shared by every route to the player
                                //          (NDrone2_NavNodeCache rebuilds it each frame)
    char _pad1f0[0x230 - 0x1f0];
    ushort NumDrones;           // +0x230 - drones given a state machine this level; the last id handed out
    ushort visionTests;         // +0x232 - counted by the DroneVision line-of-sight tests
    char _pad234[0x284 - 0x234];
    uint coverCursor;           // +0x284 - the next cover node Drone_ProcessCoverNodes looks at
    uint numCoverNodes;         // +0x288
    struct CoverNode_tag *coverNodes;   // +0x28c - Drone_PreLoad_Init points it at a table of MAX_COVER_NODES
    DroneTarget_tag targets[MAX_DRONE_TARGETS];  // +0x290
    char _pad350[0xb50 - 0x350];
    uint32_t numAwarePoints;    // +0xb50
    DroneAwarePoint awarePoints[MAX_AWARE_POINTS];  // +0xb54
    char _pad1354[0x137c - 0x1354];
    uint numAIPoints;           // +0x137c
    struct DroneAIPoint_tag *aiPoints;  // +0x1380 - Drone_PreLoad_Init points it at a table of MAX_AI_POINTS
    char _pad1384[0x1392 - 0x1384];
    uchar unknown1392;          // +0x1392 - while set in CastleIndoors1, DroneFunc_DoForcedAttack acts only on DTYPEs
                                //           0x13 and 0x16
    char _pad1393[0x142c - 0x1393];
} NPCGlobals_t;
static_assert(sizeof(NPCGlobals_t) == 0x142c, "Bad size for NPCGlobals_t");
static_assert(offsetof(NPCGlobals_t, NDrone2List) == 0x4, "Wrong offset for NDrone2List");
static_assert(offsetof(NPCGlobals_t, hostagesSaved) == 0x19c, "Wrong offset for hostagesSaved");
static_assert(offsetof(NPCGlobals_t, dummyPlayer) == 0x1a4, "Wrong offset for dummyPlayer");
static_assert(offsetof(NPCGlobals_t, enabledFrame) == 0x1ac, "Wrong offset for enabledFrame");
static_assert(offsetof(NPCGlobals_t, NumDrones) == 0x230, "Wrong offset for NumDrones");
static_assert(offsetof(NPCGlobals_t, playerTarget) == 0x1c0, "Wrong offset for playerTarget");
static_assert(offsetof(NPCGlobals_t, deadFrames) == 0x148, "Wrong offset for deadFrames");
static_assert(offsetof(NPCGlobals_t, unknown14c) == 0x14c, "Wrong offset for unknown14c");
static_assert(offsetof(NPCGlobals_t, unknown15c) == 0x15c, "Wrong offset for unknown15c");
static_assert(offsetof(NPCGlobals_t, seenAttacking) == 0x18c, "Wrong offset for seenAttacking");
static_assert(offsetof(NPCGlobals_t, nextPatrolTalkFrame) == 0x1b4, "Wrong offset for nextPatrolTalkFrame");
static_assert(offsetof(NPCGlobals_t, impactAnimFrame) == 0x1b0, "Wrong offset for impactAnimFrame");
static_assert(offsetof(NPCGlobals_t, lastPowerstationChannel) == 0x1bc, "Wrong offset for lastPowerstationChannel");
static_assert(offsetof(NPCGlobals_t, numAwarePoints) == 0xb50, "Wrong offset for numAwarePoints");
static_assert(offsetof(NPCGlobals_t, unknown1392) == 0x1392, "Wrong offset for unknown1392");
static_assert(offsetof(NPCGlobals_t, visionTests) == 0x232, "Wrong offset for visionTests");
static_assert(offsetof(NPCGlobals_t, coverCursor) == 0x284, "Wrong offset for coverCursor");
static_assert(offsetof(NPCGlobals_t, numCoverNodes) == 0x288, "Wrong offset for numCoverNodes");
static_assert(offsetof(NPCGlobals_t, coverNodes) == 0x28c, "Wrong offset for coverNodes");
static_assert(offsetof(NPCGlobals_t, targets) == 0x290, "Wrong offset for targets");
static_assert(offsetof(NPCGlobals_t, numAIPoints) == 0x137c, "Wrong offset for numAIPoints");
static_assert(offsetof(NPCGlobals_t, aiPoints) == 0x1380, "Wrong offset for aiPoints");
#define NPCGlobals (*(NPCGlobals_t *)0x001e5630)

// Navigation (NDrone2.cpp, NDrone2_Nav.cpp; docs/drone/bots-and-navigation)
obj_tag* NDrone2_Player(void);
cel_tag* NDrone2_FindCel(_VECTOR *pos);
bool NDrone2_FeetPos(obj_tag *obj, _VECTOR *feet);
char NDrone2_MoveTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, obj_tag *objA, obj_tag *objB,
                      _VECTOR *hit, char strict, char checkBlocked);
char NDrone2_MoveTest(CelPos_tag *from, CelPos_tag *to, obj_tag *objA, obj_tag *objB, _VECTOR *hit, char strict,
                      char checkBlocked);
char NDrone2_MoveTestFromNode(MAPNODE_tag *node, CelPos_tag *to, obj_tag *objA, obj_tag *objB, _VECTOR *hit,
                              char strict, char checkBlocked);
char NDrone2_MoveTestToNode(CelPos_tag *from, MAPNODE_tag *node, obj_tag *objA, obj_tag *objB, _VECTOR *hit,
                            char strict, char checkBlocked);
short NDrone2_NearestNode(CelPos_tag *pos, AIPath_tag *path, obj_tag *obj);
bool NDrone2_DistanceToEmitter(CelPos_tag *pos, AIPath_tag *path, ushort *nearest, AIEmitter_tag *emitter,
                               obj_tag *obj, float *distance);
bool NDrone2_DroneNearPos(_VECTOR *pos, float radius, obj_tag *except);
void NDrone2_NavNodeCache(void);
bool NDrone2_CanMoveToRelativePos(Drone_tag *drone, obj_tag *obj, float x, float y, float z);

// The state machine (DroneSM.cpp; docs/drone/architecture/README.md 3.2-3.4).
bool Drone_SM_Init(void);
void Drone_SM_QueueMsg(MsgObject *msg);
void Drone_SM_SendDelayedMsgs(void);
bool Drone_SM_InitObject(obj_tag *gameObj);
bool NDrone2_ProcessStateMachine(DCVars_tag *dcv, uint state, MsgObject *msg);
bool Drone_SM_SetState(StateMachineInfo_tag *sm, DSTATE next, int param);
void Drone_SM_SendMsg(uint msgType, uint scope, uint sender, int receiver);
void Drone_SM_SendMsgSelf(uint msgType, void *extraData, uint delay, uint scope, DCVars_tag *dcVars);
void Drone_SM_BroadcastMsg(uint msgType, void *extraData, uint delay, uint sender);
bool Drone_Message(obj_tag *gameObj, uint msgType, void *extraData, uint delay);
void DroneAnim_SetEndAIState(Drone_tag *drone, short newState, uint msgType);

obj_tag* NDrone2_CreateFromDIVars(DIVars_tag *diVars);

bool NDrone2_DSTATE_HostageDead(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *);
bool NDrone2_DSTATE_PlayScript(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
void DroneFunc_HostageSaved(DCVars_tag *dcVars);
void DroneFunc_CheckAlarmRaised(void);
uint DroneFunc_RecoverTime(DCVars_tag *dcv, HITDATA_tag *hit);
uint DroneFunc_ReactionTime(Drone_tag *drone);
void NDrone2_SetIdleTimeOut(DCVars_tag *dcv, int minSeconds, uint randSeconds);
void Drone_AlertStatusSet(char newStatus, DCVars_tag *dcv);

// Drone.cpp
uchar *Drone_AlertStatus(obj_tag *gameObj);
void Drone_MessageObjVicinity(uint msgType, obj_tag *obj, float radius, void *extraData, uint delay);
void Drone_FellOutMap(obj_tag *gameObj);

// NDrone2.cpp
bool NDrone2_ObjectIsArmed(obj_tag *gameObj);
bool NDrone2_GetHeadPos(obj_tag *gameObj, _VECTOR *pos, float *yaw);
bool NDrone2_GetHeadPosOld(obj_tag *gameObj, _VECTOR *pos, float *yaw);
bool DroneFunc_GetBonePos(obj_tag *gameObj, uint bone, _VECTOR *pos);
bool NDrone2_NearArmedPerson(DCVars_tag *dcv);
bool NDrone2_BondWithinAngle(DCVars_tag *dcv, float angle);
bool NDrone2_BondIsFacingMe(DCVars_tag *dcv, float degrees);
void NDrone2_SetAngleToObj(DCVars_tag *dcv, obj_tag *obj, float offset, char force);
bool NDrone2_HeadTrackObj(obj_tag *obj, int ticks, float speed, DCVars_tag *dcv);
bool DroneFunc_DoForcedAttack(DCVars_tag *dcv);
void DroneFunc_ConsiderExplosive(DCVars_tag *dcv, obj_tag *explosive, short nextState);
void DroneFunc_HandleExplosives(DCVars_tag *dcv);
void NDrone2_SeenAndAttacking(DCVars_tag *dcv);

// NDrone2_Death.cpp
void DroneFunc_SetMissionFailReason(Drone_tag *drone, uint reason);
void NDrone2_SetAsDead(DCVars_tag *dcv);
bool NDrone2_FadeOut(DCVars_tag *dcv);
bool DroneFunc_DeadDrone(DCVars_tag *dcv);
bool DroneFunc_SetDeathChannel(Drone_tag *drone);
void DroneFunc_OnInitDeath(DCVars_tag *dcv);
uint NDrone2_ExplosionDir(DCVars_tag *dcv, struct DroneImpact_tag *impact);
bool NDrone2_DSTATE_Death_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_DeathByExplosion(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Dead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Fade(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_FadeFast(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

// NDrone2_Talk.cpp
bool Drone_IsTalking(Drone_tag *drone);
bool NDrone2_DeathTalk(DCVars_tag *dcv);
bool NDrone2_HitTalkExtra(DCVars_tag *dcv);
bool NDrone2_PainTalk(DCVars_tag *dcv);
void DroneFunc_InjuredTalk(DCVars_tag *dcv);
bool DroneFunc_DoBondTalk(DCVars_tag *dcv, uint sfx, int trackTicks, uint waitTicks, char repeat);
bool NDrone2_BondTalk(DCVars_tag *dcv);
void Drone_MayhewTalk(uint sfx);
bool NDrone2_PatrolTalk(DCVars_tag *dcv);
bool NDrone2_PowerstationTalk(DCVars_tag *dcv);
// The original takes dcv in EDI: the game reaches the adaptor, C++ calls the core
void _NDrone2_HandleTalking(DCVars_tag *dcv);
void NDrone2_HandleTalking(void);

#endif // NDRONE2_H