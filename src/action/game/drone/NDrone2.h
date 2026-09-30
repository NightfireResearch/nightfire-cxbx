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


// The NPC system's globals: one block that Drone_LevelReset clears whole (0x142c bytes). Only what our code uses
// is named so far; Ghidra also has NDrone2List at +0x4, NumDrones at +0x230 and the AI network's cover nodes at
// +0x288, and FUN_00030e70 and Drone_PostLoad_Init clear arrays at +0xb54 (0x800 bytes) and +0x1354 (0x28).
typedef struct {
    char _pad0[0x19c];
    uint32_t hostagesSaved; // +0x19c
    char _pad1[0x142c - 0x1a0];
} NPCGlobals_t;
static_assert(sizeof(NPCGlobals_t) == 0x142c, "Bad size for NPCGlobals_t");
#define NPCGlobals (*(NPCGlobals_t *)0x001e5630)

obj_tag* NDrone2_CreateFromDIVars(DIVars_tag *diVars);

bool NDrone2_DSTATE_HostageDead(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *);
void DroneFunc_HostageSaved(DCVars_tag *dcVars);
void DroneFunc_CheckAlarmRaised(void);

#endif // NDRONE2_H