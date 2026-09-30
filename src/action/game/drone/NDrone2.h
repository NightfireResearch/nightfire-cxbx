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