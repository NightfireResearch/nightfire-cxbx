#ifndef NDRONE2_H
#define NDRONE2_H

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"
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
    DTYPE_AbseilToGround,
    DTYPE_AbseilHang,           // a slider stops on the rope level with its opponent
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
    DRONE_MSG_AlertSighting = 0x11, // the alert shouts (NDrone2_DroneAlertToObject): an opponent seen
    DRONE_MSG_AlertDroneShot = 0x12,  // a drone hurt
    DRONE_MSG_AlertSurrender = 0x13,  // a drone surrendering
    DRONE_MSG_HeardNoise = 0x14,
    DRONE_MSG_DroneAlert = 0x15,    // a dead body etc.; broadcast
    DRONE_MSG_AlertAttack = 0x16,   // NDrone2_SendAttackMessage
    DRONE_MSG_ImpactSmoke = 0x17,
    DRONE_MSG_ImpactStunGrenade = 0x18,
    DRONE_MSG_ImpactStunDart = 0x19,
    DRONE_MSG_TalkToMissionObject = 0x1a,
    DRONE_MSG_1B = 0x1b,            // no sender found for either
    DRONE_MSG_1C = 0x1c,
    DRONE_MSG_ForceState = 0x1d,    // extraData = the DSTATE
    DRONE_MSG_GlobalAlarm = 0x1e,   // every 30 frames while SWITCH_GLOBAL_ALARM is on
    DRONE_MSG_ConsideredAlerted = 0x1f,
    DRONE_MSG_AnimEvent = 0x20,
    DRONE_MSG_ExplosiveSeen = 0x21, // extraData = the object
    DRONE_MSG_ObjectHit = 0x22,     // non-damaging (inferred)
    DRONE_MSG_TurnedBehind = 0x23,  // the search and heard-noise states' own, at the end of an anim
    DRONE_MSG_SearchLooked = 0x24,
    DRONE_MSG_SearchDone = 0x25,
    DRONE_MSG_BotStateChanged = 0x2e, // to BotGlobal; scope = the new state

    // The bot messages, to DSTATE_BotGlobal (docs/drone/bots-and-navigation). 0x2f-0x39, 0x3e and 0x3f tell an
    // agent what it did in the multiplayer modes (MP_setPlayerStatus); BotGlobal only answers them.
    DRONE_MSG_BotFlagTaken = 0x2f,
    DRONE_MSG_BotFlagDropped = 0x30,
    DRONE_MSG_BotFlagCaptured = 0x31,
    DRONE_MSG_BotBlueprintTaken = 0x32,
    DRONE_MSG_BotBlueprintDropped = 0x33,
    DRONE_MSG_BotBlueprintDelivered = 0x34,
    DRONE_MSG_BotKeyTaken = 0x35,           // the GoldenEye key
    DRONE_MSG_BotCrystalTaken = 0x36,       // and crystal
    DRONE_MSG_BotKeyDropped = 0x37,
    DRONE_MSG_BotCrystalDropped = 0x38,
    DRONE_MSG_BotEnteredHill = 0x39,
    DRONE_MSG_BotLeftHill = 0x3a,           // the bot picks the objective goal again
    DRONE_MSG_BotGoalComplete = 0x3b,       // extraData = the goal slot
    DRONE_MSG_BotGoalUninit = 0x3c,         // extraData = the goal slot
    DRONE_MSG_BotCancelGoals = 0x3d,        // extraData = the object
    DRONE_MSG_BotUplinkTaken = 0x3e,
    DRONE_MSG_BotUplinkTakenOver = 0x3f,
    DRONE_MSG_BotObjectiveGone = 0x40,      // extraData = the demolition or protection object that went
    DRONE_MSG_BotObjectiveDestroyed = 0x41, // extraData = the object destroyed
    DRONE_MSG_BotChangeWeapon = 0x42,       // extraData = the weapon
    DRONE_MSG_BotPlayerDied = 0x43,         // extraData = the agent's object
    DRONE_MSG_BotWeaponChanged = 0x44,      // extraData = the weapon; sender = the agent's object
    DRONE_MSG_BotNewOpponent = 0x45,        // extraData = the opponent (NDrone2_SetOpponent)
} DRONE_MSG;

// A point drones take notice of (NPCGlobals.awarePoints, built each frame by Drone_BuildDynamicAwarePoints).
// Only the fields our code reads.
typedef struct DroneAwarePoint {
    uchar type;                 // +0x0 - 2: grenades and the like (DroneFunc_HandleExplosives)
    char _unknown01[7];
    obj_tag *obj;               // +0x8
    _VECTOR pos;                // +0xc
    cel_tag *cel;               // +0x18
    float radius;               // +0x1c
} DroneAwarePoint;
LAYOUT_32(sizeof(DroneAwarePoint) == 0x20, "DroneAwarePoint is 0x20 bytes");

#define MAX_AWARE_POINTS 64

// A target drones share (NPCGlobals.targets, handed out by DroneFunc_AllocateTargetID; Drone_tag.targetSlot)
typedef struct DroneTarget_tag {
    uint flags;                 // +0x0 - DRONE_TARGET_*; with neither bit 0 nor 1 set, Drone_IsCoverNodeUsable counts
                                //        only nodes a drone of this target holds
    obj_tag *obj;               // +0x4
    uint seenFrame;             // +0x8 - DroneFunc_TargetSeen; DroneFunc_UpdateTargets keeps it at the frame
    _VECTOR pos;                // +0xc - where it was seen, or each frame where it is (DroneFunc_UpdateTargets)
} DroneTarget_tag;

// DroneTarget_tag.flags
typedef enum {
    DRONE_TARGET_USED = 0x1,    // handed out (DroneFunc_AllocateTargetID)
    DRONE_TARGET_SEEN = 0x2,    // DroneFunc_TargetSeen
} DroneTargetFlags;

// The last alert a drone broadcast (NPCGlobals.alert; NDrone2_DroneAlertToObject, NDrone2_DroneAlertToPosition):
// the message's extraData
typedef struct DroneAlert_tag {
    obj_tag *obj;               // +0x00 - what it is about; NULL for a position
    obj_tag *sender;            // +0x04
    uint frame;                 // +0x08 - GameState.NumFramesUnpaused
    uint msgType;               // +0x0c
    _VECTOR pos;                // +0x10
    cel_tag *cel;               // +0x1c
    _VECTOR rot;                // +0x20 - the object's (left as it was for a position)
    _VECTOR velocity;           // +0x2c - the object's last position less its position (the same)
    float param[3];             // +0x38 - three values the sender passes
} DroneAlert_tag;
LAYOUT_32(sizeof(DroneAlert_tag) == 0x44, "DroneAlert_tag is 0x44 bytes");
LAYOUT_32(sizeof(DroneTarget_tag) == 0x18, "DroneTarget_tag is 0x18 bytes");

#define MAX_DRONE_TARGETS 8

// NPCGlobals.groups, spawners and groupCounts; no reader or writer checks a group against it
#define DRONE_GROUPS 31

// The level's cover nodes and AI points (Drone_AIHints.h)
struct CoverNode_tag;
struct DroneAIPoint_tag;

// Drone_SfxInfo rows (NDrone2_PlaySFX, NDrone2_SpeechSFX, DroneFunc_ChooseSFX and the talk functions), named
// after the sound each plays (Action_SFX) unless the code gives a use; only those the code names
typedef enum {
    DRONE_SFX_CHR_GENERIC_GUARD_DIE = 0x1,
    DRONE_SFX_CHR_DRAKE_DEATH = 0x2,
    DRONE_SFX_CHR_GENERIC_GUARD_DIE_2 = 0x3,
    DRONE_SFX_CHR_GENERIC_GUARD_DIE_3 = 0x4,
    DRONE_SFX_CHR_GENERIC_GUARD_DIE_4 = 0x5,
    DRONE_SFX_CHR_ROOK_DEATH = 0x8,
    DRONE_SFX_CHR_HOSTAGE_GIRL_DEATH = 0x9,
    DRONE_SFX_CHR_HOSTAGE_GIRL_DEATH_2 = 0xa,
    DRONE_SFX_CHR_ZOE_DEATH = 0xd,
    DRONE_SFX_CHR_KIKO_DEATH = 0xe,
    DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN = 0xf,
    DRONE_SFX_CHR_DRAKE_PAIN = 0x10,
    DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN_2 = 0x11,
    DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN_3 = 0x12,
    DRONE_SFX_CHR_GENERIC_GUARD_PAIN = 0x13,
    DRONE_SFX_CHR_GENERIC_SOLDIER_PAIN_4 = 0x14,
    DRONE_SFX_CHR_MAYHEW_PAIN_2 = 0x15,
    DRONE_SFX_CHR_ROOK_PAIN = 0x16,
    DRONE_SFX_CHR_GEN_HOSTAGE_PAIN_SERV = 0x17,
    DRONE_SFX_CHR_GEN_HOSTAGE_PAIN_SERV_2 = 0x18,
    DRONE_SFX_CHR_ZOE_PAIN = 0x1b,
    DRONE_SFX_CHR_GEN_HOSTAGE_PAIN_SERV_5 = 0x1c,
    DRONE_SFX_21 = 0x21,
    DRONE_SFX_SURRENDERED_DRONE = 0x23,
    DRONE_SFX_HEARD_THAT = 0x26,
    DRONE_SFX_DOOR_OPEN = 0x27,
    DRONE_SFX_KICK = 0x28,
    DRONE_SFX_CHR_SPEECH_M2_INTER_YAK1 = 0x29,
    DRONE_SFX_CHR_SPEECH_M2_INTER_SERV10 = 0x2f,
    DRONE_SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK11 = 0x33,
    DRONE_SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK18 = 0x36,
    DRONE_SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA01 = 0x37,
    DRONE_SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA24 = 0x3f,
    DRONE_SFX_BROKEN_OBJECT_FIRST = 0x40,
    DRONE_SFX_BROKEN_OBJECT_LAST = 0x43,
    DRONE_SFX_CHR_SPEECH_M2_BUNKER_FAIL_MAYHEW13 = 0x44,
    DRONE_SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW14 = 0x45,
    DRONE_SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW15 = 0x46,
    DRONE_SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW19 = 0x47,
    DRONE_SFX_CIVILIAN_SCARED = 0x48,
    DRONE_SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA17 = 0x49,
    DRONE_SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA18_2 = 0x4a,
    DRONE_SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA12 = 0x4d,
    DRONE_SFX_CHR_SPEECH_M1_GUARD_CHATTER_SOLDA04 = 0x4e,
    DRONE_SFX_DOOR_GUARD_ZONE2_FIRST = 0x4f,
    DRONE_SFX_DOOR_GUARD_ZONE2_LAST = 0x50,
    DRONE_SFX_CHR_SPEECH_CAS_IG_INT_GUARD8_B = 0x51,
    DRONE_SFX_CHR_SPEECH_CAS_IG_KIKO02 = 0x52,
    DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND1A = 0x53,
    DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND1B = 0x54,
    DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND2A = 0x55,
    DRONE_SFX_CHR_SPEECH_CAS_IG_KIKOBOND2B = 0x56,
    DRONE_SFX_CHR_SPEECH_CAS_IG_KIKO03 = 0x57,
    DRONE_SFX_CHR_SPEECH_CAS_IG_GUEST_F1_AUS = 0x58,
    DRONE_SFX_CHR_SPEECH_CAS_IG_GUEST_M2_E = 0x84,
    DRONE_SFX_CHR_SPEECH_CAS_IG_GUEST_M10_E = 0x8c,
    DRONE_SFX_CHR_SPEECH_CAS_IG_INT_GUARD5_B = 0x8e,
    DRONE_SFX_DOOR_GUARD_ZONE3_FIRST = 0x8f,
    DRONE_SFX_DOOR_GUARD_ZONE3_LAST = 0x92,
    DRONE_SFX_CHR_SPEECH_CAS_IG_INT_GUARD18_B = 0x93,
    DRONE_SFX_CHR_SPEECH_CAS_IG_INT_GUARD19_B = 0x94,
    DRONE_SFX_CHR_SPEECH_CAS_IG_MAYHEW1 = 0x95,
    DRONE_SFX_CHR_SPEECH_CAS_IG_ROOK1 = 0x97,
    DRONE_SFX_CHR_SPEECH_M1_RESCUE_ZOE7 = 0x9b,
    DRONE_SFX_CHR_SPEECH_CAS_IG_ZOE6B2 = 0x9c,
    DRONE_SFX_CHR_SPEECH_CAS_IG_ZOE06B = 0x9d,
    DRONE_SFX_CHR_SPEECH_CAS_IG_ZOE08 = 0x9e,
    DRONE_SFX_CHR_SPEECH_M2_INTER_SERV3 = 0x9f,
    DRONE_SFX_CHR_SPEECH_GEN_YAK_COMBAT_YAK11_2 = 0xa3,
    DRONE_SFX_CHR_SPEECH_M3_YAK_005 = 0xaa,
    DRONE_SFX_CHR_SPEECH_M5_BO1_001 = 0xac,
    DRONE_SFX_CHR_SPEECH_M5_BO1_016 = 0xb2,
    DRONE_SFX_CHR_SPEECH_M5_BO1_009 = 0xb3,
    DRONE_SFX_CHR_SPEECH_M5_BO1_014 = 0xb7,
    DRONE_SFX_CHR_SPEECH_M5_BO1_020 = 0xb9,
    DRONE_SFX_CHR_SPEECH_M5_BO1_021 = 0xba,
    DRONE_SFX_CHR_SPEECH_M5_BO1_022 = 0xbb,
    DRONE_SFX_CHR_SPEECH_M5_BO1_017 = 0xbd,
    DRONE_SFX_CHR_SPEECH_M5_BO1_018 = 0xbe,
    DRONE_SFX_CHR_SPEECH_M3_GUA_002 = 0xc3,
    DRONE_SFX_CHR_SPEECH_M3_GUA_023 = 0xcb,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_40B = 0xcc,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_41B = 0xcd,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_44B = 0xcf,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_45B = 0xd0,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_46B = 0xd1,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_42B = 0xd2,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_2B = 0xd3,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_4B = 0xd5,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_10B = 0xd6,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_1B = 0xd7,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_11B = 0xd8,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_14B = 0xd9,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_16B = 0xda,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_17B = 0xdb,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_19B = 0xdc,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_20B = 0xdd,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_21B = 0xde,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_27B = 0xdf,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_29B = 0xe0,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_30B = 0xe1,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_32B = 0xe2,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_35B = 0xe3,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_36B = 0xe4,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_37B = 0xe5,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_38B = 0xe6,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_39B = 0xe7,
    DRONE_SFX_CHR_SPEECH_NUK_IG_SNIP2_9B = 0xe8,
    DRONE_SFX_CHR_SPEECH_AST_ROOK_TAUNT13 = 0xe9,
    DRONE_SFX_CHR_SPEECH_AST_ROOK_TAUNT4 = 0xeb,
    DRONE_SFX_CHR_SPEECH_AST_ROOK_TAUNT7 = 0xec,
    DRONE_SFX_CHR_SPEECH_AST_ROOK_TAUNT5 = 0xf2,
    DRONE_SFX_CHR_SPEECH_USB_PHX_20 = 0xf3,
    DRONE_SFX_CHR_SPEECH_USB_PHX_25 = 0xf4,
    DRONE_SFX_CHR_SPEECH_USB_PHX_21 = 0xf7,
    DRONE_SFX_CHR_SPEECH_USB_PHX_26 = 0xf8,
    DRONE_SFX_CHR_SPEECH_USB_PHX_24 = 0xfb,
    DRONE_SFX_CHR_SPEECH_USB_PHX_29 = 0xfc,
    DRONE_SFX_CHR_SPEECH_USB_PHX_1 = 0x10d,
    DRONE_SFX_CHR_SPEECH_USB_PHX_8 = 0x10e,
    DRONE_SFX_CHR_SPEECH_USB_PHX_13 = 0x11b,
    DRONE_SFX_CHR_SPEECH_USB_PHX_5 = 0x121,
    DRONE_SFX_CHR_SPEECH_USB_PHX_45 = 0x133,
    DRONE_SFX_CHR_SPEECH_USB_PHX_50 = 0x137,
    DRONE_SFX_BOT_PAIN = 0x14d,
    DRONE_SFX_BOT_PAIN_FEMALE = 0x152,
    DRONE_SFX_BOT_DEATH = 0x154,
    DRONE_SFX_BOT_DEATH_FEMALE = 0x157,
} DroneSfx;
#define DRONE_SFX_COUNT 346

// One sound NDrone2_PlaySFX played (NPCGlobals.sfxLog): its Drone_SfxInfo number, 0 when it did not start
typedef struct DroneSfxLog {
    uint sfx;
    DYNAMICSOUNDS *handle;
} DroneSfxLog;
LAYOUT_32(sizeof(DroneSfxLog) == 8, "DroneSfxLog is 8 bytes");

#define DRONE_SFX_LOG_SIZE 16

// The third of DRONE_PROP_ALERTNESS's levels ({0, 0, 0.66, 1.0}): from it drones use their alert anims and states
#define DRONE_ALERTNESS_SUSPICIOUS 0.66f

// The NPC system's globals: one block that Drone_LevelReset clears whole (0x142c bytes). FUN_00030e70 and
// Drone_PostLoad_Init clear the aware points at +0xb54 and safetyEmitter at +0x1354.
typedef struct {
    uchar enabled;              // +0x0 - Drone_EnableAll's last setting
    char _pad1[3];
    Drone_tag *NDrone2List;     // +0x4 - every live drone, linked through Drone_tag.next: NDrone2Info's head,
    Drone_tag *NDrone2Tail;     // +0x8 - tail,
    ushort NDrone2Count;        // +0xc - count (how many are on NDrone2List)
    ushort NDrone2EntrySize;    // +0xe - and entry size
    Drone_tag *groups[DRONE_GROUPS];    // +0x10 - each group's first drone, linked through groupNext
    obj_tag *spawners[DRONE_GROUPS];    // +0x8c - the group's DroneSpawner (DroneSpawner_Create)
    ushort groupCounts[DRONE_GROUPS];   // +0x108 - drones in each group; 0xffff once DroneSpawner_Init has taken them
    char _pad146[0x148 - 0x146];
    uint deadFrames;            // +0x148 - how long a drone lies in DSTATE_Dead before it fades (our name)
    uint maxDeadDrones;         // +0x14c - 6 (Drone_LevelReset; 3 on the PS2): with more dead than this, the oldest
                                //          fades fast (DroneFunc_DeadDrone)
    uint deadDrones;            // +0x150 - Drone_InitComms counts the drones in DSTATE_HostageDead, Dead, Fade or
                                //          AstronautDeath,
    uint deadOrFadingFast;      // +0x154   those and the ones in FadeFast (our name),
    uint oldestDeadFrame;       // +0x158   the earliest frame one of them entered (Fade: plus deadFrames),
    uint oldestDeadId;          // +0x15c   and that drone's sm.id
    uint prevEnemies;           // +0x160 - Drone_InitComms keeps last frame's counts here: enemies,
    uint prevNeutrals;          // +0x164   neutrals,
    uint prevAllies;            // +0x168   allies,
    uint prevOutOfRangeCount;   // +0x16c   outOfRangeCount,
    uint prevSeenAttacking;     // +0x170   seenAttacking,
    uint prevSeenOpponentCount; // +0x174   seenOpponentCount (while set, DroneVision_ProcessDroneSight makes no drone
                                //          say it lost sight)
    uint prevLostOpponentCount; // +0x178   and lostOpponentCount (our names)
    uint enemies;               // +0x17c - enabled drones, by side (1, 2, 3), counted each frame by
    uint allies;                // +0x180   NDrone2_ControlSTANDARD (our names)
    uint neutrals;              // +0x184
    uint outOfRangeCount;       // +0x188 - drones in DSTATE_CombatOutOfRange, counted each frame
    uint seenAttacking;         // +0x18c - NDrone2_SeenAndAttacking counts each drone it lets through
    uint seenOpponentCount;     // +0x190 - DroneVision_ProcessDroneSight: drones that see their opponent, counted
    uint lostOpponentCount;     //  +0x194   every frame; and drones that lost theirs over 2 s ago (our names)
    uchar newSightSaid;         // +0x198 - a drone has said it sees the opponent, and none since that it lost it
    uchar lostSightSaid;        // +0x199 - the other way round
    char _pad19a[0x19c - 0x19a];
    uint32_t hostagesSaved;     // +0x19c
    uchar dummyPlayerActive;    // +0x1a0 - Drone_UpdateDummyPlayer (from Drone_InitComms) follows the cheat byte 0x1dfa29
                                //          (C_CHCHDUMMY): on, drones targeting glb_players[0] retarget dummyPlayer
    char _pad1a1[0x1a4 - 0x1a1];
    obj_tag *dummyPlayer;       // +0x1a4 - DroneSpawner_CreateBlank's object; moved onto glb_players[0] when switched on
    uint32_t postLoadFrame;     // +0x1a8 - DroneVision_ProcessDroneSight waits 15 frames after it (and after
                                //          enabledFrame); nightfire-port's postload_frame
    uint32_t enabledFrame;     // +0x1ac - GameState.NumFramesUnpaused when Drone_EnableAll last enabled the drones
    uint32_t impactAnimFrame;   // +0x1b0 - when any drone last played an impact anim (Punch and BulletImpact)
    uint32_t nextPatrolTalkFrame;   // +0x1b4 - NDrone2_PatrolTalk: no drone mutters before
    uint32_t injuredTalkFrame;  // +0x1b8 - DroneFunc_InjuredSFX: 5 s after its last line; nothing reads it
    uint32_t lastPowerstationChannel;   // +0x1bc - NDrone2_PowerstationTalk: the last channel whose line was said
    AITarget_tag playerTarget;  // +0x1c0 - the player's feet, shared by every route to the player
                                //          (NDrone2_NavNodeCache rebuilds it each frame)
    char _pad1f0[0x1f8 - 0x1f0];
    AITarget_tag alertTarget;   // +0x1f8 - where the last alert was (NDrone2_SetupAlertTarget)
    char _pad228[0x230 - 0x228];
    ushort NumDrones;          // +0x230 - drones given a state machine this level; the last id handed out
    ushort visionTests;         // +0x232 - counted by the DroneVision line-of-sight tests
    uint sightCursor;           // +0x234 - the sm.id of the drone whose line of sight was tested last
    uint alertCursor[2];        // +0x238 - the sm.ids of the pair DroneVision_FindAlertedDrones goes on from
    DroneAlert_tag alert;       // +0x240
    uint coverCursor;           // +0x284 - the next cover node Drone_ProcessCoverNodes looks at
    uint numCoverNodes;         // +0x288
    struct CoverNode_tag *coverNodes;   // +0x28c - Drone_PreLoad_Init points it at a table of MAX_COVER_NODES
    DroneTarget_tag targets[MAX_DRONE_TARGETS];  // +0x290
    char _pad350[0xb50 - 0x350];
    uint32_t numAwarePoints;    // +0xb50
    DroneAwarePoint awarePoints[MAX_AWARE_POINTS];  // +0xb54
    AIEmitter_tag safetyEmitter;    // +0x1354 - DroneMove_FindSafetyFromScaryPosition's flood (Ghidra's name)
    uint numAIPoints;           // +0x137c
    struct DroneAIPoint_tag *aiPoints;  // +0x1380 - Drone_PreLoad_Init points it at a table of MAX_AI_POINTS
    uint unknown1384;           // +0x1384 - cleared by Drone_PreLoad_Init
    char _pad1388[0x138c - 0x1388];
    float coverWaitScale;       // +0x138c - 0.5 (Drone_LevelReset): scales a bot's random wait in cover (BotCoverWait)
    uchar blind;                // +0x1390 - while set, no drone sees its opponent (DroneVision_OpponentInView);
                                //           nothing sets it
    uchar alarmSaid;            // +0x1391 - in EvilBase the alarm message goes out only once (our name)
    uchar interogating;         // +0x1392 - while set in CastleIndoors1, DroneFunc_DoForcedAttack acts only on DTYPEs
                                //           0x13 and 0x16. Set by DroneVision_BeginInterogate, cleared by
                                //           DroneVision_EndInterogate outside CastleIndoors1
    char _pad1393;
    uint alarmFrame;            // +0x1394 - Drone_InitComms sends DRONE_MSG 0x1e every 30 frames while
                                //           SWITCH_GLOBAL_ALARM is on
    char _pad1398[0x13a8 - 0x1398];
    DroneSfxLog sfxLog[DRONE_SFX_LOG_SIZE];  // +0x13a8 - the last sounds NDrone2_PlaySFX played, in a ring
    uint sfxLogNext;            // +0x1428 - the entry it writes next
} NPCGlobals_t;
LAYOUT_32(sizeof(NPCGlobals_t) == 0x142c, "Bad size for NPCGlobals_t");
LAYOUT_32(offsetof(NPCGlobals_t, sfxLog) == 0x13a8, "Wrong offset for sfxLog");
LAYOUT_32(offsetof(NPCGlobals_t, sfxLogNext) == 0x1428, "Wrong offset for sfxLogNext");
LAYOUT_32(offsetof(NPCGlobals_t, NDrone2List) == 0x4, "Wrong offset for NDrone2List");
LAYOUT_32(offsetof(NPCGlobals_t, NDrone2Count) == 0xc, "Wrong offset for NDrone2Count");
LAYOUT_32(offsetof(NPCGlobals_t, groups) == 0x10, "Wrong offset for groups");
LAYOUT_32(offsetof(NPCGlobals_t, spawners) == 0x8c, "Wrong offset for spawners");
LAYOUT_32(offsetof(NPCGlobals_t, groupCounts) == 0x108, "Wrong offset for groupCounts");
LAYOUT_32(offsetof(NPCGlobals_t, prevSeenOpponentCount) == 0x174, "Wrong offset for prevSeenOpponentCount");
LAYOUT_32(offsetof(NPCGlobals_t, seenOpponentCount) == 0x190, "Wrong offset for seenOpponentCount");
LAYOUT_32(offsetof(NPCGlobals_t, lostSightSaid) == 0x199, "Wrong offset for lostSightSaid");
LAYOUT_32(offsetof(NPCGlobals_t, postLoadFrame) == 0x1a8, "Wrong offset for postLoadFrame");
LAYOUT_32(offsetof(NPCGlobals_t, sightCursor) == 0x234, "Wrong offset for sightCursor");
LAYOUT_32(offsetof(NPCGlobals_t, alertCursor) == 0x238, "Wrong offset for alertCursor");
LAYOUT_32(offsetof(NPCGlobals_t, blind) == 0x1390, "Wrong offset for blind");
LAYOUT_32(offsetof(NPCGlobals_t, hostagesSaved) == 0x19c, "Wrong offset for hostagesSaved");
LAYOUT_32(offsetof(NPCGlobals_t, dummyPlayer) == 0x1a4, "Wrong offset for dummyPlayer");
LAYOUT_32(offsetof(NPCGlobals_t, enabledFrame) == 0x1ac, "Wrong offset for enabledFrame");
LAYOUT_32(offsetof(NPCGlobals_t, NumDrones) == 0x230, "Wrong offset for NumDrones");
LAYOUT_32(offsetof(NPCGlobals_t, playerTarget) == 0x1c0, "Wrong offset for playerTarget");
LAYOUT_32(offsetof(NPCGlobals_t, deadFrames) == 0x148, "Wrong offset for deadFrames");
LAYOUT_32(offsetof(NPCGlobals_t, maxDeadDrones) == 0x14c, "Wrong offset for maxDeadDrones");
LAYOUT_32(offsetof(NPCGlobals_t, oldestDeadId) == 0x15c, "Wrong offset for oldestDeadId");
LAYOUT_32(offsetof(NPCGlobals_t, seenAttacking) == 0x18c, "Wrong offset for seenAttacking");
LAYOUT_32(offsetof(NPCGlobals_t, nextPatrolTalkFrame) == 0x1b4, "Wrong offset for nextPatrolTalkFrame");
LAYOUT_32(offsetof(NPCGlobals_t, impactAnimFrame) == 0x1b0, "Wrong offset for impactAnimFrame");
LAYOUT_32(offsetof(NPCGlobals_t, lastPowerstationChannel) == 0x1bc, "Wrong offset for lastPowerstationChannel");
LAYOUT_32(offsetof(NPCGlobals_t, numAwarePoints) == 0xb50, "Wrong offset for numAwarePoints");
LAYOUT_32(offsetof(NPCGlobals_t, interogating) == 0x1392, "Wrong offset for interogating");
LAYOUT_32(offsetof(NPCGlobals_t, visionTests) == 0x232, "Wrong offset for visionTests");
LAYOUT_32(offsetof(NPCGlobals_t, coverCursor) == 0x284, "Wrong offset for coverCursor");
LAYOUT_32(offsetof(NPCGlobals_t, numCoverNodes) == 0x288, "Wrong offset for numCoverNodes");
LAYOUT_32(offsetof(NPCGlobals_t, coverNodes) == 0x28c, "Wrong offset for coverNodes");
LAYOUT_32(offsetof(NPCGlobals_t, targets) == 0x290, "Wrong offset for targets");
LAYOUT_32(offsetof(NPCGlobals_t, alertTarget) == 0x1f8, "Wrong offset for alertTarget");
LAYOUT_32(offsetof(NPCGlobals_t, alert) == 0x240, "Wrong offset for alert");
LAYOUT_32(offsetof(NPCGlobals_t, numAIPoints) == 0x137c, "Wrong offset for numAIPoints");
LAYOUT_32(offsetof(NPCGlobals_t, aiPoints) == 0x1380, "Wrong offset for aiPoints");
LAYOUT_32(offsetof(NPCGlobals_t, safetyEmitter) == 0x1354, "Wrong offset for safetyEmitter");
LAYOUT_32(offsetof(NPCGlobals_t, oldestDeadFrame) == 0x158, "Wrong offset for oldestDeadFrame");
LAYOUT_32(offsetof(NPCGlobals_t, prevLostOpponentCount) == 0x178, "Wrong offset for prevLostOpponentCount");
LAYOUT_32(offsetof(NPCGlobals_t, outOfRangeCount) == 0x188, "Wrong offset for outOfRangeCount");
LAYOUT_32(offsetof(NPCGlobals_t, coverWaitScale) == 0x138c, "Wrong offset for coverWaitScale");
LAYOUT_32(offsetof(NPCGlobals_t, alarmFrame) == 0x1394, "Wrong offset for alarmFrame");
extern NPCGlobals_t NPCGlobals;
// NPCGlobals.NDrone2List and NDrone2Count are the head and count of a list of Drone_tags (LList_Init by
// Drone_PreLoad_Init): the list's LLISTINFO_tag
#define NDrone2Info (*(LLISTINFO_tag *)&NPCGlobals.NDrone2List)
static_assert(offsetof(NPCGlobals_t, NDrone2Tail) - offsetof(NPCGlobals_t, NDrone2List) == offsetof(LLISTINFO_tag, tail),
              "NDrone2Info's tail");
static_assert(offsetof(NPCGlobals_t, NDrone2Count) - offsetof(NPCGlobals_t, NDrone2List) == offsetof(LLISTINFO_tag, count),
              "NDrone2Info's count");
static_assert(offsetof(NPCGlobals_t, NDrone2EntrySize) - offsetof(NPCGlobals_t, NDrone2List) ==
              offsetof(LLISTINFO_tag, elementSize), "NDrone2Info's entry size");

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
bool NDrone2_NearDrone(DCVars_tag *dcv, float radius);
void NDrone2_NavNodeCache(void);
bool NDrone2_CanMoveToRelativePos(Drone_tag *drone, obj_tag *obj, float x, float y, float z);
bool NDrone2_CanWalkStraightTo(Drone_tag *drone, obj_tag *obj, CelPos_tag *pos, char checkBlocked);
extern _VECTOR LastWalkTarget;   // where it last tested a walk to (NDrone2_Nav.cpp)
bool NDrone2_MoveTestRelative(_VECTOR *offset, Drone_tag *drone, obj_tag *obj, char unscaled, char checkBlocked);
bool NDrone2_AtDest(DCVars_tag *dcv);
void NDrone2_SetMoveTarget(DCVars_tag *dcv, CelPos_tag *target);

// The state machine (DroneSM.cpp; docs/drone/architecture/README.md 3.2-3.4).
bool Drone_SM_Init(void);
void Drone_SM_QueueMsg(MsgObject *msg);
void Drone_SM_SendDelayedMsgs(void);
bool Drone_SM_InitObject(obj_tag *gameObj);
bool NDrone2_ProcessStateMachine(DCVars_tag *dcv, uint state, MsgObject *msg);
bool Drone_SM_SetState(StateMachineInfo_tag *sm, DSTATE next, intptr_t param);
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
void Drone_AddDynamicAwarePoint(uchar type, obj_tag *obj, _VECTOR *pos, cel_tag *cel, float radius);
HASHCODE Drone_GetSkin(obj_tag *gameObj);

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

// DroneFunc_SetMissionFailReason's reasons, named for who fails the mission (our names)
typedef enum {
    DRONE_FAIL_STATE = 0,                       // DSTATE_FailMission: no message
    DRONE_FAIL_CIVILIAN_KILLED_BY_PLAYER = 1,   // a drone not an enemy (DRONE_PROP_PLAYER_KILL_FAILS_MISSION)
    DRONE_FAIL_ENEMY_KILLED_BY_PLAYER = 2,
    DRONE_FAIL_CIVILIAN_KILLED = 3,             // by anyone (DRONE_PROP_DEATH_FAILS_MISSION)
    DRONE_FAIL_ENEMY_KILLED = 4,
    DRONE_FAIL_UNARMED_PUNCHED = 5,             // in CastleIndoors1
    DRONE_FAIL_CIVILIAN_PUNCHED = 6,            // unarmed, outside the towers
    DRONE_FAIL_UNARMED_TASERED = 7,             // in CastleIndoors1
    DRONE_FAIL_ATTACKED = 8,                    // DroneFunc_FirstAttack (DRONE_PROP_ALERT_FAILS_MISSION)
    DRONE_FAIL_HOSTAGE_DEAD = 9,
    DRONE_FAIL_SEEN = 10,                       // DRONE_PROP_SIGHTING_FAILS_MISSION
    DRONE_FAIL_PARTY_GIRL_ALERTED = 11,         // DSTATE_PartyGirl (DRONE_PROP_ALERT_FAILS_MISSION)
    DRONE_FAIL_DOOR_GUARD = 12,                 // DSTATE_CivilianDoorGuard: the opponent in zone 3
    DRONE_FAIL_TASERED = 13,                    // DTYPE_Zoe, DTYPE_Mayhew
    DRONE_FAIL_STUN_DARTED = 14,
    DRONE_FAIL_ZOE_LEASH = 15,                  // SWITCH_ZOE_LEASH: more than 10 m from the player
    DRONE_FAIL_ALARM = 16,                      // DSTATE_PressAlarm: the nearest switch's channel
} DroneFailReason;

void DroneFunc_SetMissionFailReason(Drone_tag *drone, uint reason);
void NDrone2_SetAsDead(DCVars_tag *dcv);
bool NDrone2_FadeOut(DCVars_tag *dcv);
bool DroneFunc_DeadDrone(DCVars_tag *dcv);
bool DroneFunc_SetDeathChannel(Drone_tag *drone);
void DroneFunc_OnInitDeath(DCVars_tag *dcv);
uint NDrone2_ExplosionDir(DCVars_tag *dcv, struct DroneImpact_tag *impact);
bool NDrone2_DSTATE_Death_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_DeathByExplosion(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
void NDrone2_LogDroneDeath(DCVars_tag *dcv);
bool NDrone2_DSTATE_SpecialDeath_Anim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
double NDrone2_DistanceToObj(DCVars_tag *dcv, obj_tag *obj);
bool NDrone2_DSTATE_Dead(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Fade(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_FadeFast(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

// NDrone2_Talk.cpp
DYNAMICSOUNDS* NDrone2_PlaySFX(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char check, float scale);
DYNAMICSOUNDS* NDrone2_SpeechSFX(DCVars_tag *dcv, uint sfx, char force, char face, char needFace);
void NDrone2_SpeechSFXd(Drone_tag *drone, uint sfx, char force, char face, char needFace);
bool DroneFunc_NewSightTalk(DCVars_tag *dcv);
bool DroneFunc_LostSightTalk(DCVars_tag *dcv);
bool Drone_IsTalking(Drone_tag *drone);
bool NDrone2_DeathTalk(DCVars_tag *dcv);
bool NDrone2_HitTalkExtra(DCVars_tag *dcv);
bool NDrone2_PainTalk(DCVars_tag *dcv);
void DroneFunc_InjuredTalk(DCVars_tag *dcv);
void NDrone2_PlayFacialAnim(DCVars_tag *dcv, HASHCODE script);
bool Drone_IsSayingSfx(Drone_tag *drone, uint sfx);
bool DroneFunc_InjuredSFX(DCVars_tag *dcv, uint sfx);
bool DroneFunc_DoBondTalk(DCVars_tag *dcv, uint sfx, int trackTicks, uint waitTicks, char repeat);
bool NDrone2_BondTalk(DCVars_tag *dcv);
void Drone_MayhewTalk(uint sfx);
bool NDrone2_PatrolTalk(DCVars_tag *dcv);
bool NDrone2_PowerstationTalk(DCVars_tag *dcv);
void NDrone2_HandleTalking(DCVars_tag *dcv);

#endif // NDRONE2_H
