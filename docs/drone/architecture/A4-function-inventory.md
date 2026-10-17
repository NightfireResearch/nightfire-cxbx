# Appendix A4: function inventory for area A

Size = Ghidra body size in bytes. "ours" = reimplemented in src/action with // AUTOINJECT. Callers from
tools/xrefs_action.json (data@ = referenced from a table at that address). Names in parentheses are descriptions of
unnamed functions, other non-Ghidra names are PS2 names matched by comparing bodies in this session.

## State machine core

| address | name | Ghidra name | bytes | ours | callers |
|---|---|---|---|---|---|
| 0x00032e70 | DroneAnim_SetEndAIState |  | 89 |  | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim |
| 0x0003a3f0 | (static) state is resumable (not 0, not 0x53-0x55) | FUN_0003a3f0 | 20 |  | DroneFunc_HandleImpact |
| 0x0004e180 | NDrone2_ProcessStateMachine |  | 43 |  | Drone_SM_InitObject |
| 0x0004e1b0 | (static) Drone_SM find drone object by SM id | FUN_0004e1b0 | 38 |  | Drone_SM_RouteMsg |
| 0x0004e1e0 | Drone_SM_Init |  | 183 |  | Drone_LevelReset |
| 0x0004e2a0 | Drone_SM_InitObject |  | 117 |  | N2_PostLoad_Init |
| 0x0004e320 | Drone_SM_SetState |  | 160 |  | BOTSTATE_processGoals, BOTSTATE_setStateChange, BOT_fellOutMap, DroneAnim_CoverAnim, DroneAnim_SetEndAIState, DroneFunc_ConsiderExplosive, DroneFunc_DeadDrone, DroneFunc_DoForcedAttack (+210) |
| 0x0004e3c0 | (static) Drone_SM queue delayed message | FUN_0004e3c0 | 68 |  | Drone_SM_RouteMsg |
| 0x0004e410 | Drone_SM_RouteMsgDCV |  | 539 |  | Drone_SM_RouteMsg, Drone_SM_SendMsgSelf |
| 0x0004e630 | Drone_SM_RouteMsg |  | 345 |  | Drone_SM_BroadcastMsg, Drone_SM_SendDelayedMsgs, Drone_SM_SendMsg, Drone_SM_SendMsgSelf, MP_PlayerKilled, MP_objectBeingDeleted, MP_sendTeamBotMessage |
| 0x0004e890 | Drone_SM_SendMsgSelf |  | 106 |  | DroneAnim_SetEndAIState, DroneFunc_HandleExplosives, DroneFunc_HandleSoundAlerts, DroneVision_HaveOpponentSight, Drone_Message, MP_sendBotMessage, N2_ControlSTANDARD, S:HostageKillerAttack (+2) |
| 0x0004e900 | Drone_SM_BroadcastMsg |  | 70 |  | Drone_EnableAll, Drone_InitComms, Drone_Message, N2_DroneAlertToObject, N2_DroneAlertToPosition |
| 0x00064380 | Drone_SM_SendDelayedMsgs |  | 64 |  | Drone_InitComms |

## Lifecycle / per-frame control

| address | name | Ghidra name | bytes | ours | callers |
|---|---|---|---|---|---|
| 0x0002fac0 | Drone_DCVfromOBJ |  | 58 | yes | BOTSTATE_getPreferredTraitOpponentObjIndex, BOTWEAP_changeWeapon, DroneAnim_EventFunc, DroneAnim_SetAnimD, DroneAnim_SetEndAIState, DroneVision_HaveOpponentSight, DroneVision_ProcessDroneSight, Drone_BrokenObject (+21) |
| 0x0002fb00 | Drone_PreLoad_Init |  | 139 |  | ResetMap_GameInit |
| 0x0002fb90 | DroneSpawner_CreateBlank |  | 180 |  | Drone_PostLoad_Init |
| 0x0002fc50 | NDrone2_CreateFromDIVars |  | 146 |  | DroneSpawner_SpawnDrone, Drone_Create |
| 0x0002fcf0 | Drone_Create |  | 134 | yes | BOT_init, Drone_CoderCreate, parsemap_create_dynamic_objects |
| 0x0002fd80 | Drone_CoderCreate |  | 153 |  | Script_EventHandler |
| 0x00030530 | DroneSpawner_Create |  | 309 |  | parsemap_create_dynamic_objects |
| 0x00030670 | DroneSpawner_Init |  | 395 |  | N2_SetupGroups |
| 0x00030800 | DroneSpawner_Delete |  | 89 |  | data@00163de0 |
| 0x000308b0 | DroneSpawner_DroneDelete | FUN_000308b0 | 83 |  | Drone_Delete |
| 0x00031530 | Drone_Control |  | 580 |  | data@00163bb0, data@00163c64 |
| 0x00031780 | Drone_LevelReset |  | 88 |  | ResetMap_GameInit |
| 0x000317e0 | Drone_SM_SendMsg |  | 74 |  | DroneAnim_EventFunc, Drone_SM_InitObject, S:HostageKiller, S:HostageKillerAttack |
| 0x00031830 | Drone_Message |  | 140 |  | DroneVision_ConsiderAlerted, Drone_EnableAll, Drone_ExplosiveHit, Drone_MessageObjVicinity, FUN_00031f20, N2_DealWithObjHit, N2_MonitorTalkToMissionObj |
| 0x00031970 | Drone_FellOutMap |  | 49 |  | control_handle_cel_change |
| 0x00031fd0 | Drone_PostLoad_Init |  | 343 |  | ResetMap_Load |
| 0x00032130 | Drone_Delete |  | 606 |  | data@00163bb8, data@00163c6c |
| 0x000323e0 | DroneSpawner_SpawnDrone |  | 235 |  | DroneSpawner_Control |
| 0x000324d0 | DroneSpawner_Control |  | 855 |  | data@00163dd8 |
| 0x00032870 | Drone_EnableAll |  | 242 |  | Script_EventHandler, Script_Free |
| 0x00032a00 | Drone_InitComms |  | 778 |  | control_movement_object_handler |
| 0x00036e90 | NDrone2_SetupGroups |  | 140 |  | Drone_PostLoad_Init |
| 0x00037000 | NDrone2_SetDronePartner |  | 456 |  | S:CivilianDoorGuard |
| 0x000376b0 | NDrone2_Enable |  | 125 |  | BOT_respawn, DroneSpawner_Init, Drone_EnableAll, Drone_FellOutMap, S:AbseilDeath, S:Dead, S:Death_Anim, S:DeleteMe (+3) |
| 0x00038db0 | DroneFunc_DeadDrone |  | 49 |  | S:AstronautDeath, S:Dead, S:Fade, S:HostageDead |
| 0x00038df0 | NDrone2_SetAsDead |  | 118 |  | S:BotDead, S:Dead, S:HostageDead |
| 0x000397f0 | NDrone2_ChangeToAttackMode |  | 116 |  | DroneFunc_FirstAttack, FUN_0003fe30, FUN_00040430, S:CivilianMission, S:EnemyMission, S:GoToGoalPosition, N2_PreDroneControl |
| 0x0003b670 | NDrone2_PostDroneControl |  | 74 |  | Drone_Control |
| 0x0003ed20 | NDrone2_PreDroneControl |  | 516 |  | Drone_Control |
| 0x0003f5f0 | NDrone2_ControlSTANDARD |  | 684 |  | Drone_Control |
| 0x0003f8a0 | NDrone2_ControlDTYPE_Zoe | (no function defined) |  |  | DroneTypeSettings table |
| 0x0003f910 | NDrone2_ControlDTYPE_Ninja | (no function defined) |  |  | DroneTypeSettings table |
| 0x0003f930 | NDrone2_ControlDTYPE_Astronaut (jmp NDrone2_ControlSTANDARD) | (no function defined) |  |  | DroneTypeSettings table |
| 0x0003f940 | NDrone2_CreateObj |  | 143 |  | N2_CreateFromDIVars |
| 0x0003f9d0 | DroneInit_Collision |  | 265 |  | N2_DefaultInit |
| 0x000417f0 | NDrone2_DefaultInit |  | 4227 |  | N2_PostLoad_Init |
| 0x00042a90 | NDrone2_PostLoad_Init |  | 130 |  | BOT_init, DroneSpawner_SpawnDrone, Drone_CoderCreate, Drone_PostLoad_Init |

## Mode / type set-up

| address | name | Ghidra name | bytes | ours | callers |
|---|---|---|---|---|---|
| 0x00019440 | behaviour_util_getProperty |  | 47 |  | DroneAnim_CanStandLean, DroneAnim_CanStandStepOut, DroneAnim_CoverAnim, DroneAnim_SetStandIdleAnim, DroneFunc_ConsiderExplosive, DroneFunc_FirstAttack, DroneFunc_HandleExplosives, DroneFunc_HandleImpact (+98) |
| 0x00019470 | behaviour_util_setProperty |  | 83 |  | BOT_init, FUN_0003fae0, FUN_0003fe30, FUN_000408b0, FUN_00040960, FUN_00040980, FUN_000409b0, FUN_000409fc (+36) |
| 0x000194d0 | behaviour_util_get |  | 696 |  | N2_DoModeSettingsNEW |
| 0x000197b0 | behaviour_util_getStats |  | 22 |  | N2_DoModeSettingsNEW |
| 0x0003a410 | (static) Tower civilian reset | FUN_0003a410 | 181 |  | DroneFunc_HandleImpact |
| 0x0003fae0 | NDrone2_GetDTYPENEW | FUN_0003fae0 | 502 |  | FUN_0003fe30 |
| 0x0003fd20 | (inlined on PS2) second-behaviour state helper | FUN_0003fd20 | 55 |  | FUN_0003fe30 |
| 0x0003fe30 | NDrone2_GetDroneTypeAttackTypeFriend | FUN_0003fe30 | 1057 |  | N2_DoModeSettingsNEW |
| 0x000403b0 | (inlined on PS2) DoTypeSettings default-state helper | FUN_000403b0 | 122 |  | FUN_00040430 |
| 0x00040430 | NDrone2_DoTypeSettingsOLD | FUN_00040430 | 271 |  | FUN_00108ff0, N2_DefaultInit |
| 0x00040600 | NDrone2_init_DMODE_Defaults |  | 668 |  | FUN_00040960, FUN_00040980, FUN_00040d30, FUN_00040d60, FUN_00040d80, FUN_00040da0, N2_init_DMODE_CastleChatGuard1, N2_init_DMODE_CastleChatGuard2 (+1) |
| 0x000408a0 | NDrone2_init_DMODE_Normal (ICF: Normal/Guard/Retreater/Attacker/Assassin/Interogator) | NDrone2_init_DMODE_Defaults | 5 |  | data@00177700 |
| 0x000408b0 | NDrone2_init_DMODE_Sniper | FUN_000408b0 | 173 |  |  |
| 0x00040960 | NDrone2_init_DMODE_Stealth | FUN_00040960 | 32 |  |  |
| 0x00040980 | NDrone2_init_DMODE_RunToPoint | FUN_00040980 | 48 |  |  |
| 0x000409b0 | NDrone2_init_DMODE_HostageKiller | FUN_000409b0 | 75 |  |  |
| 0x00040a80 | NDrone2_init_DMODE_Hostage/HostageTied (ICF) | NDrone2_init_DMODE_HostageTied | 90 |  | data@00161f10 |
| 0x00040ae0 | NDrone2_init_DMODE_JustStand4Demo | FUN_00040ae0 | 24 |  | D3D8::D3DDevice_SetTextureState_ColorKeyColor |
| 0x00040b00 | NDrone2_init_DMODE_Civilian |  | 272 |  |  |
| 0x00040c10 | NDrone2_init_DMODE_CivilianScared | FUN_00040c10 | 205 |  |  |
| 0x00040ce0 | NDrone2_init_DMODE_MissionFailer |  | 42 |  |  |
| 0x00040d10 | NDrone2_init_DMODE_Mayhew/TruckDriver (ICF) | FUN_00040d10 | 24 |  |  |
| 0x00040d30 | NDrone2_init_DMODE_AlarmRaiser | FUN_00040d30 | 48 |  |  |
| 0x00040d60 | NDrone2_init_DMODE_SearchLight | FUN_00040d60 | 32 |  |  |
| 0x00040d80 | NDrone2_init_DMODE_Ambush | FUN_00040d80 | 32 |  |  |
| 0x00040da0 | NDrone2_init_DMODE_Zoe | FUN_00040da0 | 48 |  |  |
| 0x00040dd0 | NDrone2_init_DMODE_PartyGirl | FUN_00040dd0 | 173 |  |  |
| 0x00040e80 | NDrone2_init_DMODE_CivilianGuard | FUN_00040e80 | 205 |  |  |
| 0x00040f50 | NDrone2_init_DMODE_CivDoorGuard |  | 224 |  |  |
| 0x00041030 | NDrone2_init_DMODE_CastleChatGuard1 |  | 23 |  |  |
| 0x00041050 | NDrone2_init_DMODE_CastleChatGuard2 |  | 39 |  |  |
| 0x00041080 | NDrone2_init_DMODE_SniperAlert |  | 157 |  |  |
| 0x00041120 | NDrone2_init_DMODE_PartyGirlLooker |  | 198 |  |  |
| 0x000411f0 | NDrone2_initDTYPE_Sniper | FUN_000411f0 | 12 |  |  |
| 0x00041200 | NDrone2_initDTYPE_SniperAlert |  | 45 |  | N2_ChangeToAttackMode, data@001779d0 |
| 0x00041230 | NDrone2_DoModeSettingsNEW |  | 659 |  | N2_DefaultInit, N2_DoModeSettingsOLD |
| 0x000414d0 | NDrone2_DoModeSettingsOLD |  | 759 |  | N2_DefaultInit |

## Helpers called from the per-frame path (other areas; listed for the call order)

| address | name | Ghidra name | bytes | ours | callers |
|---|---|---|---|---|---|
| 0x0001a660 | BOT_setOtherPlayerInfo | FUN_0001a660 | 788 |  | N2_ControlSTANDARD |
| 0x00030950 | Drone_PauseNoDraw (probable) | FUN_00030950 | 186 |  | Drone_InitComms |
| 0x00031110 | (unnamed Drone_InitComms step) | FUN_00031110 | 123 |  | Drone_InitComms |
| 0x00031190 | (unnamed Drone_InitComms step) | FUN_00031190 | 67 |  | Drone_InitComms |
| 0x000313e0 | Drone_ProcessOpponents (probable) | FUN_000313e0 | 178 |  | Drone_InitComms |
| 0x000314a0 | (unnamed Drone_InitComms step) | FUN_000314a0 | 141 |  | Drone_InitComms |
| 0x0003ac10 | NDrone2_DoTracking | FUN_0003ac10 | 198 |  | N2_ControlSTANDARD |
| 0x0003c430 | NDrone2_HandleTalking | FUN_0003c430 | 489 |  | N2_ControlSTANDARD |
| 0x0003cbf0 | (ControlSTANDARD door step; PS2 inlines DoorIsOpen/OpenDoor) | FUN_0003cbf0 | 172 |  | N2_ControlSTANDARD |
| 0x000437f0 | DroneMove_NoBunching | FUN_000437f0 | 270 |  | N2_ControlSTANDARD |
| 0x00044de0 | NDrone2_NavNodeCache (probable) | FUN_00044de0 | 66 |  | Drone_InitComms, MiniSub_Update |
| 0x000454e0 | DroneMove_SetBoundryFlags | FUN_000454e0 | 262 |  | N2_ControlSTANDARD |

## State handlers

| address | name | Ghidra name | bytes | ours | callers |
|---|---|---|---|---|---|
| 0x0004b420 | NDrone2_DSTATE_HostageDie |  | 78 |  | data@00177ccc |
| 0x0004b470 | NDrone2_DSTATE_CivilianHiding |  | 205 |  | data@00177cec |
| 0x0004b590 | NDrone2_DSTATE_StandBlind |  | 45 |  | data@00177d00 |
| 0x0004b5c0 | NDrone2_DSTATE_AllyFollowDone |  | 124 |  | data@00177d2c, data@00177d3c |
| 0x0004b680 | NDrone2_DSTATE_SniperReload |  | 359 |  | data@00177d50 |
| 0x0004b840 | NDrone2_DSTATE_CivilianChallenge |  | 322 |  | data@00177d98 |
| 0x0004b9e0 | NDrone2_DSTATE_Unsurrender_Anim |  | 239 |  | data@00177da4 |
| 0x0004bb20 | NDrone2_DSTATE_Fade |  | 178 |  | data@00177dc0 |
| 0x0004bbf0 | NDrone2_DSTATE_FadeFast |  | 182 |  | data@00177dc4 |
| 0x0004bcd0 | NDrone2_DSTATE_PunchImpact |  | 162 |  | data@00177dec |
| 0x0004bdb0 | NDrone2_DSTATE_ExplosiveImpact |  | 155 |  | data@00177df0 |
| 0x0004be80 | NDrone2_DSTATE_GrenadeThrow |  | 195 |  | data@00177d54 |
| 0x0004bf80 | NDrone2_DSTATE_AimStandReload |  | 307 |  | data@00177e44 |
| 0x0004c110 | NDrone2_DSTATE_AimStandDiscard |  | 172 |  | data@00177e48 |
| 0x0004c200 | NDrone2_DSTATE_Prone |  | 205 |  | data@00177e4c |
| 0x0004c320 | NDrone2_DSTATE_AimCrouchReload |  | 247 |  | data@00177e64 |
| 0x0004c470 | NDrone2_DSTATE_AltAttack |  | 239 |  | data@00177e68 |
| 0x0004c5b0 | NDrone2_DSTATE_RollLeftCrouch |  | 213 |  | data@00177e84 |
| 0x0004c6d0 | NDrone2_DSTATE_RollRightCrouch |  | 213 |  | data@00177e88 |
| 0x0004c7f0 | NDrone2_DSTATE_HoldItRightThere |  | 156 |  | data@00177ea0 |
| 0x0004c890 | NDrone2_DSTATE_PressAlarm |  | 510 |  | data@00177ec8 |
| 0x0004caf0 | NDrone2_DSTATE_AbseilStepOff |  | 207 |  | data@00177ee4 |
| 0x0004cbf0 | NDrone2_DSTATE_AbseilDeath |  | 329 |  | data@00177ee8 |
| 0x0004cd60 | NDrone2_DSTATE_UnderCoverInit |  | 356 |  | data@00177eec |
| 0x0004cf20 | NDrone2_DSTATE_UnderCoverAim |  | 261 |  | data@00177ef4 |
| 0x0004d080 | NDrone2_DSTATE_UnderCoverSniperReload |  | 248 |  | data@00177f00 |
| 0x0004d1d0 | NDrone2_DSTATE_UnderCoverLeaveNow |  | 341 |  | data@00177f10 |
| 0x0004d380 | NDrone2_DSTATE_SeenDeadBody |  | 236 |  | data@00177f18 |
| 0x0004d4c0 | NDrone2_DSTATE_SeenSurrenderedDrone |  | 269 |  | data@00177f1c |
| 0x0004d620 | NDrone2_DSTATE_SeenExplosive |  | 25 |  | data@00177f24 |
| 0x0004d640 | NDrone2_DSTATE_SmokedOut |  | 174 |  | data@00177e8c |
| 0x0004d730 | NDrone2_DSTATE_SmokedOut_Recover |  | 193 |  | data@00177e94 |
| 0x0004d840 | NDrone2_DSTATE_DeleteMe |  | 57 |  | data@00177f70 |
| 0x0004d880 | NDrone2_DSTATE_JustStand |  | 169 |  | data@00177f78 |
| 0x0004d950 | NDrone2_DSTATE_Tester1 |  | 365 |  | data@00177f7c |
| 0x0004daf0 | NDrone2_DSTATE_Tester3 |  | 155 |  | data@00177f84 |
| 0x0004db90 | NDrone2_DSTATE_HangUp |  | 68 |  | data@00177f8c |
| 0x0004dbe0 | NDrone2_DSTATE_WaitForever |  | 20 |  | data@00177f90 |
| 0x0004dc00 | NDrone2_DSTATE_AstronautCombat |  | 199 |  | data@00177fa0 |
| 0x0004dcf0 | NDrone2_DSTATE_BotRespawn |  | 55 |  | data@00177fb0 |
| 0x0004dd30 | NDrone2_DSTATE_BotCoverInit |  | 249 |  | data@00178024 |
| 0x0004de70 | NDrone2_DSTATE_BotCoverAim |  | 149 |  | data@0017802c |
| 0x0004df50 | NDrone2_DSTATE_BotCoverLeaveNow |  | 176 |  | data@00178040 |
| 0x0004e040 | NDrone2_DSTATE_BotHeardNoise |  | 11 |  | data@00178058 |
| 0x0004e050 | NDrone2_DSTATE_BotDeathAnim |  | 117 |  | data@00178068 |
| 0x0004e0d0 | NDrone2_DSTATE_BotDeathByExplosion |  | 165 |  | data@0017806c |
| 0x0004e950 | NDrone2_DSTATE_Global |  | 273 |  | data@00177ca0 |
| 0x0004eaa0 | NDrone2_DSTATE_WaitSwitch |  | 248 |  | data@00177ca4 |
| 0x0004ebc0 | NDrone2_DSTATE_Disabled |  | 165 |  | data@00177ca8 |
| 0x0004ec70 | NDrone2_DSTATE_PlayScript |  | 1083 |  | data@00177cac |
| 0x0004f100 | NDrone2_DSTATE_Idle |  | 733 |  | data@00177cb0 |
| 0x0004f440 | NDrone2_DSTATE_Alert |  | 588 |  | data@00177cb4 |
| 0x0004f6f0 | NDrone2_DSTATE_InitPatrol |  | 152 |  | data@00177cb8 |
| 0x0004f790 | NDrone2_DSTATE_Patrol |  | 589 |  | data@00177cbc |
| 0x0004fa40 | NDrone2_DSTATE_HostageKiller |  | 534 |  | data@00177cc0 |
| 0x0004fcc0 | NDrone2_DSTATE_HostageKillerAttack |  | 402 |  | data@00177cc4 |
| 0x0004fea0 | NDrone2_DSTATE_Hostage |  | 267 |  | data@00177cc8 |
| 0x00050000 | NDrone2_DSTATE_HostageSaved |  | 1 |  | D3D8::D3DResource_AddRef, D3D8::D3DResource_GetType, D3D8::D3DResource_Release, D3D8::D3D_BlockOnResource, D3D8::D3D_DestroyResource, FUN_00104ee0, FUN_00104f20, FUN_001050c0 (+2) |
| 0x00050150 | NDrone2_DSTATE_HostageIdle |  | 317 |  | data@00177cd4 |
| 0x000502e0 | NDrone2_DSTATE_HostageHide |  | 268 |  | data@00177cd8 |
| 0x00050440 | NDrone2_DSTATE_HostageDead |  | 190 |  | data@00177cdc |
| 0x00050530 | NDrone2_DSTATE_CivilianInit |  | 200 |  | data@00177ce0 |
| 0x00050600 | NDrone2_DSTATE_Civilian |  | 602 |  | data@00177ce4 |
| 0x000508c0 | NDrone2_DSTATE_CivilianScared |  | 288 |  | data@00177ce8 |
| 0x00050a20 | NDrone2_DSTATE_CivilianPatrol |  | 548 |  | data@00177cf0 |
| 0x00050ca0 | NDrone2_DSTATE_ReturnToPatrolPath |  | 60 |  | data@00177cf4 |
| 0x00050ce0 | NDrone2_DSTATE_CivilianMission |  | 782 |  | data@00177cf8 |
| 0x00051050 | NDrone2_DSTATE_CivilianMissionWait |  | 780 |  | data@00177cfc |
| 0x000513c0 | NDrone2_DSTATE_KikoMission |  | 396 |  | data@00177d04 |
| 0x000515b0 | NDrone2_DSTATE_KikoMissionRun |  | 246 |  | data@00177d08 |
| 0x00051700 | NDrone2_DSTATE_EnemyMission |  | 662 |  | data@00177d0c |
| 0x000519f0 | NDrone2_DSTATE_AllyLeadInit |  | 369 |  | data@00177d10 |
| 0x00051bc0 | NDrone2_DSTATE_AllyLead |  | 665 |  | data@00177d14 |
| 0x00051ec0 | NDrone2_DSTATE_AllyLeadPlayerInWay |  | 332 |  | data@00177d18 |
| 0x00052050 | NDrone2_DSTATE_AllyLeadHide |  | 318 |  | data@00177d1c |
| 0x000521e0 | NDrone2_DSTATE_AllyLeadWait |  | 379 |  | data@00177d20 |
| 0x000523a0 | NDrone2_DSTATE_AllyLeadMissionWait |  | 243 |  | data@00177d24 |
| 0x000524e0 | NDrone2_DSTATE_AllyLeadBondCombat |  | 320 |  | data@00177d28 |
| 0x00052660 | NDrone2_DSTATE_AllyFollowInit |  | 182 |  | data@00177d30 |
| 0x00052760 | NDrone2_DSTATE_AllyFollow |  | 286 |  | data@00177d34 |
| 0x000528c0 | NDrone2_DSTATE_AllyFollowWait |  | 276 |  | data@00177d38 |
| 0x00052a20 | NDrone2_DSTATE_AllyGoToGoalPosition |  | 364 |  | data@00177d40 |
| 0x00052c00 | NDrone2_DSTATE_SniperIdle |  | 317 |  | data@00177d44 |
| 0x00052da0 | NDrone2_DSTATE_SniperAim |  | 305 |  | data@00177d48 |
| 0x00052f20 | NDrone2_DSTATE_SniperFire |  | 427 |  | data@00177d4c |
| 0x00053110 | NDrone2_DSTATE_PartyGirlInit |  | 128 |  | data@00177d58 |
| 0x00053190 | NDrone2_DSTATE_PartyGirl |  | 571 |  | data@00177d5c |
| 0x00053430 | NDrone2_DSTATE_CivilianGuard |  | 553 |  | data@00177d60 |
| 0x000536c0 | NDrone2_DSTATE_CivilianDoorGuard |  | 1220 |  | data@00177d64 |
| 0x00053c00 | NDrone2_DSTATE_TruckDriverInit |  | 431 |  | data@00177d68 |
| 0x00053e10 | NDrone2_DSTATE_TruckDriverInitAlert |  | 270 |  | data@00177d6c |
| 0x00053f80 | NDrone2_DSTATE_TruckDriverIdle |  | 339 |  | data@00177d70 |
| 0x00054130 | NDrone2_DSTATE_TruckDriverMission |  | 551 |  | data@00177d74 |
| 0x000543c0 | NDrone2_DSTATE_CastleChatGuard1 |  | 545 |  | data@00177d78 |
| 0x00054650 | NDrone2_DSTATE_AmbushInit |  | 95 |  | data@00177d7c |
| 0x000546b0 | NDrone2_DSTATE_AmbushWait |  | 305 |  | data@00177d80 |
| 0x00054840 | NDrone2_DSTATE_InterogateAssist |  | 330 |  | data@00177d84 |
| 0x00054990 | NDrone2_DSTATE_InterogateAssistWait |  | 409 |  | data@00177d88 |
| 0x00054b80 | NDrone2_DSTATE_Interogator |  | 40 |  | data@00177d8c |
| 0x00054bb0 | NDrone2_DSTATE_Interogate |  | 1548 |  | data@00177d90 |
| 0x00055240 | NDrone2_DSTATE_InterogateWalk |  | 519 |  | data@00177d94 |
| 0x000554a0 | NDrone2_DSTATE_Surrender_Anim |  | 521 |  | data@00177d9c |
| 0x000556f0 | NDrone2_DSTATE_Surrendered |  | 249 |  | data@00177da0 |
| 0x00055830 | NDrone2_DSTATE_KnockedOut_Anim |  | 223 |  | data@00177da8 |
| 0x00055940 | NDrone2_DSTATE_Knocked_Out |  | 74 |  | data@00177dac |
| 0x000559b0 | NDrone2_DSTATE_Taser |  | 615 |  | data@00177dc8 |
| 0x00055c50 | NDrone2_DSTATE_Stunned |  | 494 |  | data@00177dcc |
| 0x00055e90 | NDrone2_DSTATE_Stunned_Recover |  | 239 |  | data@00177dd0 |
| 0x00055fc0 | NDrone2_DSTATE_StunGrenadeImpact |  | 211 |  | data@00177dd4 |
| 0x000560c0 | NDrone2_DSTATE_StunGrenadeLoop |  | 397 |  | data@00177dd8 |
| 0x000562a0 | NDrone2_DSTATE_StunGrenadeRecover |  | 237 |  | data@00177ddc |
| 0x000563d0 | NDrone2_DSTATE_StunDartImpact |  | 234 |  | data@00177de0 |
| 0x000564e0 | NDrone2_DSTATE_StunDartLoop |  | 397 |  | data@00177de4 |
| 0x000566c0 | NDrone2_DSTATE_StunDartRecover |  | 239 |  | data@00177de8 |
| 0x000567f0 | NDrone2_DSTATE_Death_Anim |  | 526 |  | data@00177db0 |
| 0x00056a30 | NDrone2_DSTATE_DeathByExplosion |  | 368 |  | data@00177db4 |
| 0x00056bd0 | NDrone2_DSTATE_SpecialDeath_Anim |  | 773 |  | data@00177db8 |
| 0x00056f00 | NDrone2_DSTATE_Dead |  | 434 |  | data@00177dbc |
| 0x000570e0 | NDrone2_DSTATE_BulletImpact |  | 160 |  | data@00177df4 |
| 0x000571b0 | NDrone2_DSTATE_Attack |  | 345 |  | data@00177df8 |
| 0x00057310 | NDrone2_DSTATE_Alerted1stEncounter |  | 40 |  | data@00177dfc |
| 0x00057340 | NDrone2_DSTATE_Combat |  | 150 |  | data@00177e00 |
| 0x000573f0 | NDrone2_DSTATE_CombatNoMove |  | 621 |  | data@00177e04 |
| 0x000576b0 | NDrone2_DSTATE_CombatOutOfRange |  | 331 |  | data@00177e08 |
| 0x00057840 | NDrone2_DSTATE_CombatNewSighting |  | 137 |  | data@00177e0c |
| 0x000578d0 | NDrone2_DSTATE_CombatNoSight |  | 217 |  | data@00177e10 |
| 0x000579f0 | NDrone2_DSTATE_CombatTooClose |  | 61 |  | data@00177e14 |
| 0x00057a30 | NDrone2_DSTATE_CombatWait |  | 520 |  | data@00177e18 |
| 0x00057c80 | NDrone2_DSTATE_CombatNoRoute |  | 891 |  | data@00177e1c |
| 0x00058040 | NDrone2_DSTATE_NoOpponent |  | 297 |  | data@00177e20 |
| 0x00058190 | NDrone2_DSTATE_Obstructed |  | 182 |  | data@00177e24 |
| 0x00058270 | NDrone2_DSTATE_AlertToPosition |  | 846 |  | data@00177e28 |
| 0x00058650 | NDrone2_DSTATE_GoToGoalPosition |  | 848 |  | data@00177e2c |
| 0x00058a10 | NDrone2_DSTATE_HostageGoToGoalPosition |  | 341 |  | data@00177e30 |
| 0x00058be0 | NDrone2_DSTATE_SearchArea |  | 740 |  | data@00177e34 |
| 0x00058f40 | NDrone2_DSTATE_DroneStuck |  | 189 |  | data@00177e9c |
| 0x00059040 | NDrone2_DSTATE_DrawWeapon |  | 305 |  | data@00177e38 |
| 0x000591d0 | NDrone2_DSTATE_AimStand |  | 365 |  | data@00177e3c |
| 0x00059390 | NDrone2_DSTATE_AimStandFire |  | 798 |  | data@00177e40 |
| 0x00059700 | NDrone2_DSTATE_ProneFire |  | 285 |  | data@00177e50 |
| 0x00059860 | NDrone2_DSTATE_AimBackoff |  | 452 |  | data@00177e54 |
| 0x00059a70 | NDrone2_DSTATE_CrouchCover |  | 357 |  | data@00177e58 |
| 0x00059c30 | NDrone2_DSTATE_AimCrouch |  | 213 |  | data@00177e5c |
| 0x00059d50 | NDrone2_DSTATE_AimCrouchFire |  | 650 |  | data@00177e60 |
| 0x0005a020 | NDrone2_DSTATE_StepAimLeft |  | 242 |  | data@00177e6c |
| 0x0005a160 | NDrone2_DSTATE_StepAimRight |  | 242 |  | data@00177e70 |
| 0x0005a2a0 | NDrone2_DSTATE_StrafeAimLeft |  | 402 |  | data@00177e74 |
| 0x0005a480 | NDrone2_DSTATE_StrafeAimRight |  | 402 |  | data@00177e78 |
| 0x0005a660 | NDrone2_DSTATE_StrafeDodgeLeft |  | 359 |  | data@00177e7c |
| 0x0005a810 | NDrone2_DSTATE_StrafeDodgeRight |  | 359 |  | data@00177e80 |
| 0x0005a9c0 | NDrone2_DSTATE_Investigate |  | 379 |  | data@00177e98 |
| 0x0005aba0 | NDrone2_DSTATE_OpenDoor |  | 623 |  | data@00177ea4 |
| 0x0005ae60 | NDrone2_DSTATE_KickObject |  | 263 |  | data@00177ea8 |
| 0x0005afb0 | NDrone2_DSTATE_ActionAnim |  | 317 |  | data@00177eac |
| 0x0005b140 | NDrone2_DSTATE_StandFiddle |  | 235 |  | data@00177eb0 |
| 0x0005b270 | NDrone2_DSTATE_EnemyRunToPoint |  | 341 |  | data@00177eb4 |
| 0x0005b3d0 | NDrone2_DSTATE_RunAwayFromObject |  | 590 |  | data@00177eb8 |
| 0x0005b6a0 | NDrone2_DSTATE_HideFromScaryObject |  | 358 |  | data@00177ebc |
| 0x0005b860 | NDrone2_DSTATE_RecoverFromScaryObject |  | 144 |  | data@00177ec0 |
| 0x0005b8f0 | NDrone2_DSTATE_RunToAlarm |  | 158 |  | data@00177ec4 |
| 0x0005b990 | NDrone2_DSTATE_DonePressAlarm |  | 192 |  | data@00177ecc |
| 0x0005ba50 | NDrone2_DSTATE_RunForCover |  | 769 |  | data@00177ed0 |
| 0x0005bdd0 | NDrone2_DSTATE_ElevatorJumper |  | 283 |  | data@00177ed4 |
| 0x0005bf10 | NDrone2_DSTATE_AbseilInit |  | 290 |  | data@00177ed8 |
| 0x0005c070 | NDrone2_DSTATE_AbseilSlide |  | 453 |  | data@00177edc |
| 0x0005c260 | NDrone2_DSTATE_AbseilHang |  | 400 |  | data@00177ee0 |
| 0x0005c420 | NDrone2_DSTATE_UnderCoverIdle |  | 1028 |  | data@00177ef0 |
| 0x0005c880 | NDrone2_DSTATE_UnderCoverFire |  | 538 |  | data@00177ef8 |
| 0x0005cb00 | NDrone2_DSTATE_UnderCoverSniperFire |  | 685 |  | data@00177efc |
| 0x0005ce10 | NDrone2_DSTATE_UnderCoverReturn |  | 357 |  | data@00177f04 |
| 0x0005cfd0 | NDrone2_DSTATE_UnderCoverTypeChange |  | 224 |  | data@00177f08 |
| 0x0005d0c0 | NDrone2_DSTATE_UnderCoverLeave |  | 81 |  | data@00177f0c |
| 0x0005d120 | NDrone2_DSTATE_SeenDroneShot |  | 40 |  | data@00177f14, data@00177f20 |
| 0x0005d150 | NDrone2_DSTATE_HeardNoise |  | 60 |  | data@00177f28 |
| 0x0005d190 | NDrone2_DSTATE_HeardNoiseAware |  | 418 |  | data@00177f2c |
| 0x0005d390 | NDrone2_DSTATE_HeardNoiseSuspect |  | 918 |  | data@00177f30 |
| 0x0005d790 | NDrone2_DSTATE_HeardNoiseAlert |  | 695 |  | data@00177f34 |
| 0x0005dab0 | NDrone2_DSTATE_SmokedOut_Loop |  | 278 |  | data@00177e90 |
| 0x0005dc10 | NDrone2_DSTATE_NinjaStand |  | 382 |  | data@00177f38 |
| 0x0005ddc0 | NDrone2_DSTATE_NinjaAttack |  | 1065 |  | data@00177f3c |
| 0x0005e220 | NDrone2_DSTATE_NinjaAttackLongRange |  | 558 |  | data@00177f40 |
| 0x0005e4a0 | NDrone2_DSTATE_NinjaAttackMidRange |  | 823 |  | data@00177f44 |
| 0x0005e830 | NDrone2_DSTATE_NinjaAttackShortRange |  | 1091 |  | data@00177f48 |
| 0x0005ecb0 | NDrone2_DSTATE_NinjaGetCloseToPlayer |  | 481 |  | data@00177f4c |
| 0x0005eee0 | NDrone2_DSTATE_NinjaSideflip |  | 221 |  | data@00177f5c |
| 0x0005efc0 | NDrone2_DSTATE_NinjaSideflipLeft |  | 341 |  | data@00177f60 |
| 0x0005f140 | NDrone2_DSTATE_NinjaSideflipRight |  | 342 |  | data@00177f64 |
| 0x0005f2c0 | NDrone2_DSTATE_NinjaSword |  | 333 |  | data@00177f50 |
| 0x0005f440 | NDrone2_DSTATE_NinjaSomersault |  | 329 |  | data@00177f54 |
| 0x0005f5c0 | NDrone2_DSTATE_NinjaBackflip |  | 327 |  | data@00177f58 |
| 0x0005f730 | NDrone2_DSTATE_NinjaStandFire |  | 593 |  | data@00177f68 |
| 0x0005f9c0 | NDrone2_DSTATE_NinjaNoRoute |  | 587 |  | data@00177f6c |
| 0x0005fc40 | NDrone2_DSTATE_FailMission |  | 61 |  | data@00177f74 |
| 0x0005fc80 | NDrone2_DSTATE_Tester2 |  | 76 |  | data@00177f80 |
| 0x0005fcd0 | NDrone2_DSTATE_Tester4 |  | 149 |  | data@00177f88 |
| 0x0005fd70 | NDrone2_DSTATE_AstronautLaunch |  | 342 |  | data@00177f94 |
| 0x0005fef0 | NDrone2_DSTATE_AstronautCombatMove |  | 270 |  | data@00177fa4 |
| 0x00060030 | NDrone2_DSTATE_AstronautHit |  | 214 |  | data@00177f98 |
| 0x00060140 | NDrone2_DSTATE_AstronautDeath |  | 195 |  | data@00177f9c |
| 0x00060230 | NDrone2_DSTATE_SpaceDrake |  | 302 |  | data@00177fa8 |
| 0x00060390 | NDrone2_DSTATE_BotInit |  | 68 |  | data@00177fac |
| 0x000603e0 | NDrone2_DSTATE_BotGlobal |  | 3852 |  | data@00177fb4 |
| 0x000613f0 | NDrone2_DSTATE_BotCollector |  | 120 |  | data@00177fb8 |
| 0x00061470 | NDrone2_DSTATE_BotStuck |  | 44 |  | data@00177fbc, data@00177fc0, data@00177fc4, data@00177fc8, data@00177fcc, data@00177fd0, data@00177fd4, data@00177fd8 (+2) |
| 0x000614a0 | NDrone2_DSTATE_BotAttack |  | 466 |  | data@00177fdc |
| 0x00061690 | NDrone2_DSTATE_BotAttackRun |  | 504 |  | data@00177fe0 |
| 0x000618b0 | NDrone2_DSTATE_BotAttackNoRoute |  | 52 |  | data@00177fe4 |
| 0x000618f0 | NDrone2_DSTATE_BotAttackFire |  | 535 |  | data@00177fe8 |
| 0x00061b30 | NDrone2_DSTATE_BotAttackStrafeAimLeft |  | 325 |  | data@00177ff0 |
| 0x00061ca0 | NDrone2_DSTATE_BotAttackStrafeAimRight |  | 325 |  | data@00177ff4 |
| 0x00061e10 | NDrone2_DSTATE_BotAttackRunChangePosition |  | 243 |  | data@00177ff8 |
| 0x00061f40 | NDrone2_DSTATE_BotAttackBackoff |  | 542 |  | data@00177ffc |
| 0x00062190 | NDrone2_DSTATE_BotAttackCrouch |  | 732 |  | data@00178000 |
| 0x000624c0 | NDrone2_DSTATE_BotAttackRollLeftCrouch |  | 133 |  | data@00178004 |
| 0x00062560 | NDrone2_DSTATE_BotAttackRollRightCrouch |  | 133 |  | data@00178008 |
| 0x00062600 | NDrone2_DSTATE_BotAttackStepAimLeft |  | 226 |  | data@0017800c |
| 0x000626f0 | NDrone2_DSTATE_BotAttackStepAimRight |  | 226 |  | data@00178010 |
| 0x000627e0 | NDrone2_DSTATE_BotAttackReload |  | 278 |  | data@00178014 |
| 0x00062900 | NDrone2_DSTATE_BotAttackChangeWeapon |  | 159 |  | data@00178018 |
| 0x000629a0 | NDrone2_DSTATE_BotAttackUnarmed |  | 124 |  | data@0017801c |
| 0x00062a20 | NDrone2_DSTATE_BotCoverRunTo |  | 333 |  | data@00178020 |
| 0x00062bc0 | NDrone2_DSTATE_BotCoverIdle |  | 727 |  | data@00178028 |
| 0x00062ee0 | NDrone2_DSTATE_BotCoverFire |  | 329 |  | data@00178030 |
| 0x00063070 | NDrone2_DSTATE_BotCoverReturn |  | 268 |  | data@00178034 |
| 0x000631c0 | NDrone2_DSTATE_BotCoverTypeChange |  | 239 |  | data@00178038 |
| 0x000632c0 | NDrone2_DSTATE_BotCoverLeave |  | 213 |  | data@0017803c |
| 0x000633e0 | NDrone2_DSTATE_BotAlertToPosition |  | 407 |  | data@00178048 |
| 0x000635d0 | NDrone2_DSTATE_BotGotoGoalPosition |  | 368 |  | data@0017804c |
| 0x00063760 | NDrone2_DSTATE_BotSeenDroneShot |  | 44 |  | data@00178050, data@00178054 |
| 0x00063790 | NDrone2_DSTATE_BotImpactBullet |  | 236 |  | data@0017805c |
| 0x000638b0 | NDrone2_DSTATE_BotImpactExplosive |  | 82 |  | data@00178060 |
| 0x00063930 | NDrone2_DSTATE_BotImpactPunch |  | 93 |  | data@00178064 |
| 0x000639c0 | NDrone2_DSTATE_BotImpactStunGrenade |  | 150 |  | data@00178074 |
| 0x00063a60 | NDrone2_DSTATE_BotDead |  | 391 |  | data@00178070 |
| 0x00063c10 | NDrone2_DSTATE_BotDoorOpen |  | 424 |  | data@00178078 |
| 0x00063e00 | NDrone2_DSTATE_BotGuardFriendIdle |  | 187 |  | data@0017807c |
| 0x00063ec0 | NDrone2_DSTATE_BotGuardFriendFollow |  | 570 |  | data@00178080 |
| 0x00064100 | NDrone2_DSTATE_BotIdle |  | 602 |  | data@00178084 |

