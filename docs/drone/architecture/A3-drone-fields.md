# Appendix A3: Drone_tag (0x978 bytes) field accesses

Generated from Ghidra decompiles of all 3998 Xbox functions (by a research script, not in the repository). An access is counted when the
decompiler shows it on a variable typed Drone_tag *, a ->drone-> chain, or *(T *)((int)droneVar + off). Accesses
through untyped ints (e.g. *(int *)(param_1 + 0x3f4) where param_1 is an int) are MISSED, so treat "no writer" as
"none found", not "none". R = read, W = written, A = address taken. Sizes/types are Ghidra's current guesses.
Offsets are the base of the access, so a byte access inside a dword field shows as its own row.

| off | Ghidra size/type/name | #fn | writers | readers (first 12) |
|---|---|---|---|---|
| 0x000 |  | 1 |  | Drone_FeetOnPoint |
| 0x004 |  | 9 |  | Drone_EnableAll, Drone_FeetOnPoint, Drone_MessageObjVicinity, Drone_SM_RouteMsg, FUN_00030950, FUN_000313e0, N2_FindOpponent, N2_NearArmedDrone, Player_Enable |
| 0x008 |  | 1 |  | DroneSpawner_Init |
| 0x00c | 4 obj_tag * gameObj | 39 | N2_CreateObj | BOTSTATE_combatWeaponChangeChoice, BOTSTATE_setPickupVisitTime, BOTWEAP_InitWeapon, BOTWEAP_decrRounds, BOT_init, Check_AutoAim, Check_Target, DroneAnim_CallAnim, DroneAnim_LocationDeathAnim, DroneAnim_SetEndAIState, DroneAnim_SetHTAnim, DroneAnim_SnapRotate (+26) |
| 0x010 | 4 obj_tag * (unnamed) | 1 | N2_DefaultInit |  |
| 0x014 |  | 2 | N2_DefaultInit | Drone_FeetOnPoint |
| 0x015 |  | 5 | N2_DefaultInit | N2_CivilianScaredTalk, S:CivilianPatrol, N2_DeathTalk, N2_PainTalk |
| 0x016 |  | 4 | FUN_0003a410, S:PressAlarm, S:RunToAlarm | N2_SeenAndAttacking |
| 0x017 |  | 1 | N2_ControlSTANDARD |  |
| 0x018 | 1 undefined1 (unnamed) | 2 | N2_DefaultInit | N2_PreDroneControl |
| 0x019 |  | 2 | N2_DefaultInit | DroneFunc_OnInitDeath |
| 0x01a |  | 1 | S:SpecialDeath_Anim |  |
| 0x01b | 1 undefined1 (unnamed) | 3 | N2_DefaultInit | DroneFunc_HandleImpact, N2_PunchImpact |
| 0x01c | 4 undefined4 (unnamed) | 2 |  | S:CombatNoMove, N2_DefaultInit |
| 0x020 |  | 1 | DroneAnim_EventFunc |  |
| 0x021 |  | 1 | DroneAnim_EventFunc |  |
| 0x023 | 1 char (unnamed) | 6 | S:AbseilDeath, S:AbseilInit, S:AstronautLaunch, N2_DefaultInit | DroneInit_Collision, N2_ControlSTANDARD |
| 0x024 |  | 2 | S:AstronautCombat, N2_DefaultInit |  |
| 0x027 |  | 1 | N2_DefaultInit |  |
| 0x028 | 1 undefined1 (unnamed) | 1 | N2_ChangeToAttackMode |  |
| 0x029 | 1 undefined1 usingSecondBehaviour | 2 | FUN_0003a410, N2_ChangeToAttackMode |  |
| 0x02a | 1 undefined1 (unnamed) | 5 | Check_Target, N2_PreDroneControl | DroneFunc_DoBondTalk, DroneWeap_OpponentIsAimingAtMe, N2_BondTalk |
| 0x02b |  | 4 | DroneFunc_ConsiderExplosive | DroneFunc_HandleExplosives, DroneMove_FindSafetyFromScaryObject, DroneVision_EnemyAlerts |
| 0x02c |  | 1 | N2_HeadTrackObj |  |
| 0x02d |  | 1 | N2_PostLoad_Init |  |
| 0x02e |  | 16 | S:BotCoverAim, S:BotCoverFire, S:BotCoverIdle, S:BotCoverInit, S:BotCoverLeave, S:BotCoverLeaveNow, S:BotCoverReturn, S:BotCoverTypeChange, S:UnderCoverAim, S:UnderCoverFire, S:UnderCoverIdle, S:UnderCoverInit, S:UnderCoverLeaveNow, S:UnderCoverReturn, S:UnderCoverSniperFire, S:UnderCoverTypeChange |  |
| 0x02f | 1 undefined1 (unnamed) | 4 | S:StunDartLoop, S:StunGrenadeLoop, S:Stunned | N2_Collision |
| 0x031 |  | 2 | N2_DoModeSettingsNEW, N2_DoModeSettingsOLD |  |
| 0x032 |  | 3 | N2_DoModeSettingsNEW, N2_DoModeSettingsOLD | N2_DefaultInit |
| 0x033 |  | 8 | S:AlertToPosition, S:GoToGoalPosition, S:HeardNoise, S:HeardNoiseAlert, S:HeardNoiseAware, S:HeardNoiseSuspect | DroneVision_EnemyAlerts, S:CastleChatGuard1 |
| 0x034 |  | 3 | S:HeardNoiseAlert, S:HeardNoiseSuspect, S:SearchArea |  |
| 0x035 |  | 2 | N2_PlayFacialAnim | N2_ControlSTANDARD |
| 0x036 |  | 3 | N2_ControlSTANDARD, N2_PlayFacialAnim | N2_SpeechSFX |
| 0x037 |  | 2 | N2_PlayFacialAnim | N2_SpeechSFX |
| 0x038 | 1 undefined1 (unnamed) | 11 | DroneWeap_AimTarget_IsOpponent, S:BotCoverIdle, S:BotCoverInit, S:BotDoorOpen, S:OpenDoor, S:Prone, S:StandFiddle, S:UnderCoverIdle, S:UnderCoverInit, N2_PreDroneControl | DroneInit_Collision |
| 0x03a |  | 3 |  | DroneFunc_OnInitDeath, S:BotDead, S:Dead |
| 0x03b |  | 30 | DroneWeap_Fire, Drone_SM_RouteMsgDCV, S:AimCrouchReload, S:AimStandReload, S:AstronautDeath, S:AstronautHit, S:AstronautLaunch, S:BotAttackReload, S:CombatNoMove, S:NinjaAttackShortRange, S:NinjaSideflipLeft, S:NinjaSideflipRight, S:ProneFire, S:SniperReload, S:StepAimLeft, S:StepAimRight, S:StrafeDodgeLeft, S:StrafeDodgeRight, S:UnderCoverSniperReload | S:BotAttackBackoff, S:BotAttackCrouch, S:BotAttackFire, S:BotAttackStepAimLeft, S:BotAttackStepAimRight, S:BotAttackStrafeAimLeft, S:BotAttackStrafeAimRight, S:BotCoverFire, S:DeathByExplosion, S:Death_Anim, S:SpecialDeath_Anim |
| 0x03c |  | 28 | DroneWeap_Fire, DroneWeap_FireWeapon | Drone_FeetOnPoint, S:AimBackoff, S:AimCrouchFire, S:AimStandFire, S:BotAttackBackoff, S:BotAttackCrouch, S:BotAttackFire, S:BotCoverFire, S:CombatNoMove, S:CombatNoRoute, S:CombatWait, S:GoToGoalPosition (+14) |
| 0x03d |  | 2 | DroneWeap_Fire, Drone_SM_RouteMsgDCV |  |
| 0x03e |  | 3 | N2_DefaultInit | DroneWeap_DoFiring, DroneWeap_FireWeapon |
| 0x03f |  | 1 | DroneWeap_DoFiring |  |
| 0x040 |  | 1 | DroneWeap_FireWeapon |  |
| 0x041 |  | 1 | DroneWeap_FireWeapon |  |
| 0x042 |  | 1 | DroneVision_LogSeenOpponent |  |
| 0x043 |  | 2 | S:HeardNoiseAlert, S:HeardNoiseSuspect |  |
| 0x044 | 1 char isCivilian | 25 | S:Civilian, S:CivilianDoorGuard, S:CivilianGuard, S:CivilianMission, S:CivilianMissionWait, S:CivilianPatrol, S:Stunned_Recover, N2_DoModeSettingsOLD | Check_AutoAim, Check_Target, DroneFunc_AllocateTargetID, DroneFunc_OnInitDeath, DroneFunc_SetDeathChannel, DroneVision_ConsiderAlerted, DroneVision_EnemyLookForOpponent, DroneVision_LogSeenOpponent, FUN_00064910, N2_ControlSTANDARD, S:EnemyRunToPoint, S:KnockedOut_Anim (+5) |
| 0x045 |  | 3 | N2_DefaultInit | DroneAnim_EventFunc, S:DrawWeapon |
| 0x048 |  | 1 |  | Drone_FeetOnPoint |
| 0x04c |  | 1 |  | Player_Enable |
| 0x050 | 4 float (unnamed) | 4 | S:AstronautCombat, S:AstronautCombatMove, S:AstronautLaunch, N2_DefaultInit |  |
| 0x084 | 4 float (unnamed) | 2 | N2_DefaultInit | N2_Collision |
| 0x088 | 4 float (unnamed) | 1 | N2_DefaultInit |  |
| 0x090 | 4 float health | 36 | MP_GoldenEyeUpdate, S:BotGlobal, S:KnockedOut_Anim, S:SpaceDrake, N2_DefaultInit, N2_DoModeSettingsNEW, N2_SetAsDead, Pickup_Handler | BOTSTATE_setGoalPickPrefs, Check_AutoAim, DroneAnim_EventFunc, DroneFunc_HandleImpact, DroneFunc_InjuredTalk, DroneFunc_LostSightTalk, DroneFunc_NewSightTalk, DroneWeap_FireWeapon, DroneWeap_Ready2Fire, N2_AttackTalk, N2_CivilianScaredTalk, N2_ControlSTANDARD (+16) |
| 0x094 | 4 float (unnamed) | 1 | N2_DefaultInit |  |
| 0x098 | 1 byte bulletAccuracy | 2 | N2_DefaultInit, N2_DoModeSettingsNEW |  |
| 0x099 |  | 2 | N2_DoModeSettingsNEW | BOT_getAggressionMul |
| 0x09a |  | 1 | N2_DoModeSettingsNEW |  |
| 0x09b |  | 1 | N2_DoModeSettingsNEW |  |
| 0x09c |  | 2 | N2_DoModeSettingsNEW | DroneFunc_ReactionTime |
| 0x09d |  | 3 | N2_DoModeSettingsNEW | DroneFunc_RecoverTime, S:BotImpactStunGrenade |
| 0x09e |  | 1 | N2_DoModeSettingsNEW |  |
| 0x09f |  | 1 | N2_DefaultInit |  |
| 0x0a0 |  | 1 |  | DroneFunc_HandleImpact |
| 0x0a4 | 4 float (unnamed) | 2 | N2_DefaultInit, N2_DoModeSettingsNEW |  |
| 0x0a8 |  | 2 | N2_DefaultInit | S:GoToGoalPosition |
| 0x0a9 | 1 byte (unnamed) | 34 | FUN_0003a410, N2_ChangeToAttackMode, S:GoToGoalPosition, N2_DefaultInit, N2_DoModeSettingsNEW, N2_DoModeSettingsOLD | DroneFunc_DoForcedAttack, DroneFunc_SendHurtMessage, DroneInit_Collision, DroneVision_ConsiderAlerted, DroneVision_EnemyLookForOpponent, DroneVision_HaveOpponentSight, DroneWeap_FireWeapon, DroneWeap_Ready2Fire, Drone_Control, Drone_MayDealBackshotDamage, Drone_SM_RouteMsgDCV, FUN_000453e0 (+16) |
| 0x0aa | 1 byte (unnamed) | 5 | FUN_0003a410, N2_ChangeToAttackMode, N2_DefaultInit, N2_DoModeSettingsOLD | S:CivilianScared |
| 0x0ab | 1 byte (unnamed) | 2 | N2_DoModeSettingsNEW | N2_DefaultInit |
| 0x0ac |  | 1 | N2_DoModeSettingsNEW |  |
| 0x0ad |  | 1 | N2_DoModeSettingsNEW |  |
| 0x0b0 | 4 undefined4 initialisedOnFrame | 1 | N2_PostLoad_Init |  |
| 0x0b4 | 4 undefined4 (unnamed) | 1 | N2_DefaultInit |  |
| 0x0b8 | 4 undefined4 (unnamed) | 1 | N2_DefaultInit |  |
| 0x0bc | 2 short (unnamed) | 31 | N2_DefaultInit | Check_AutoAim, DroneAnim_CallAnim, DroneFunc_InjuredTalk, DroneFunc_LostSightTalk, DroneFunc_NewSightTalk, DroneInit_Collision, DroneVision_ConsiderAlerted, Drone_ModBulletDamage, FUN_0003c430, FUN_0003cce0, N2_AttackTalk, N2_BondTalk (+18) |
| 0x0be | 2 ushort (unnamed) | 18 | DroneWeap_ChangeWeapon, N2_DefaultInit, N2_DoModeSettingsOLD | DroneAnim_CallAnim, DroneAnim_CanDoAnimState, DroneAnim_SetDAnimInternal, DroneAnim_ValidateCoverAnim, DroneFunc_HandleImpact, Drone_MayDealBackshotDamage, N2_CanStrafeLeft, N2_CanStrafeRight, S:CombatNoMove, S:DeathByExplosion, S:Death_Anim, S:SpecialDeath_Anim (+3) |
| 0x0c0 | 2 undefined2 (unnamed) | 3 | N2_DefaultInit | S:AbseilSlide, S:AbseilStepOff |
| 0x0c4 | 4 obj_tag * skinHashcode | 3 | N2_DefaultInit | N2_BondTalk, N2_ExplosiveImpact |
| 0x0c8 | 4 float (unnamed) | 5 | DroneFunc_SetAsAttacking, DroneVision_ConsiderAlerted, N2_DefaultInit | FUN_00065510, N2_FindOpponent |
| 0x0cc | 4 float (unnamed) | 6 | DroneFunc_SetAsAttacking, DroneVision_ConsiderAlerted, S:SniperAim, N2_DefaultInit | FUN_00065510, N2_FindOpponent |
| 0x0d0 | 4 float someCombatRange1 | 10 | BOTSTATE_defaultCombatRange, BOTSTATE_reallyCloseCombatRange, N2_DefaultInit | S:BotAttackCrouch, S:BotAttackFire, S:BotAttackReload, S:BotAttackRun, S:BotGlobal, S:NinjaAttack, S:NinjaAttackShortRange |
| 0x0d4 | 4 float someCombatRange3 | 11 | BOTSTATE_defaultCombatRange, BOTSTATE_reallyCloseCombatRange, S:NinjaStand, N2_DefaultInit | S:AimBackoff, S:Attack, S:BotAttack, S:BotAttackBackoff, S:BotAttackRun, S:CombatNoRoute, S:GoToGoalPosition |
| 0x0d8 | 4 float (unnamed) | 1 | N2_DefaultInit |  |
| 0x0dc | 4 undefined4 (unnamed) | 1 | N2_DefaultInit |  |
| 0x0e0 | 4 float someCombatRange2 | 8 | BOTSTATE_defaultCombatRange, BOTSTATE_reallyCloseCombatRange, S:NinjaStand, S:SniperAim, N2_DefaultInit | DroneFunc_SetAsAttacking, S:BotAttackCrouch, S:BotAttackFire |
| 0x0e4 | 4 float (unnamed) | 2 | N2_DefaultInit | Drone_ModBulletDamage |
| 0x0e8 | 4 uint (unnamed) | 1 | N2_PreDroneControl |  |
| 0x0ec | 4 int aiStateMachine | 14 | DroneAnim_CallAnim, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, Drone_SM_InitObject | BOTSTATE_processGoals, BOT_fellOutMap, DroneAnim_SetEndAIState, Drone_Control, Drone_DCVfromOBJ, Drone_InitComms, Drone_Message, Drone_SM_RouteMsg, MP_GoldenEyeUpdate, N2_DroneAlertToObject |
| 0x0f0 | 4 cel_tag * (unnamed) | 9 | Drone_SM_InitObject | DroneFunc_ConsiderExplosive, DroneFunc_HandleImpact, DroneVision_ConsiderAlerted, Drone_SM_RouteMsg, N2_ExplosiveImpact, N2_PreDroneControl, N2_PunchImpact, N2_ReachedDestNode |
| 0x0f4 |  | 1 |  | Drone_SM_InitObject |
| 0x0f8 |  | 1 |  | Drone_SM_InitObject |
| 0x0fc | 4 cel_tag * (unnamed) | 5 | DroneFunc_ConsiderExplosive, DroneFunc_HandleImpact, Drone_SM_InitObject, N2_ExplosiveImpact, N2_PunchImpact |  |
| 0x104 |  | 1 | Drone_SM_InitObject |  |
| 0x110 |  | 1 |  | Drone_SM_InitObject |
| 0x114 | 4 void * processFunction | 1 | Drone_SM_InitObject |  |
| 0x118 | 1 byte waitingForSwitchNumber | 3 | N2_DefaultInit | S:Global, S:WaitSwitch |
| 0x119 | 1 byte associatedSwitchChannel | 9 | DroneFunc_SetAsAttacking, N2_DefaultInit, N2_PreDroneControl | DroneFunc_FirstAttack, DroneFunc_HostageSaved, S:CivilianMission, S:EnemyMission, S:HostageSaved, N2_DoModeSettingsOLD |
| 0x11a |  | 2 | N2_DefaultInit | S:Global |
| 0x11b |  | 1 | N2_DefaultInit |  |
| 0x11c | 2 undefined2 (unnamed) | 1 | N2_DefaultInit |  |
| 0x11e | 2 ushort (unnamed) | 4 | DroneFunc_SetAsAttacking, N2_DefaultInit, N2_PreDroneControl | N2_DoModeSettingsOLD |
| 0x120 | 1 byte (unnamed) | 4 | S:SpecialDeath_Anim, N2_DefaultInit | DroneFunc_SetDeathChannel, S:Death_Anim |
| 0x121 |  | 4 | N2_DoModeSettingsNEW, N2_init_DMODE_CastleChatGuard1, N2_init_DMODE_CastleChatGuard2 | S:CastleChatGuard1 |
| 0x128 | 4 obj_tag * (unnamed) | 2 | N2_CreateFromDIVars, N2_DefaultInit |  |
| 0x12c | 4 obj_tag * (unnamed) | 2 | N2_CreateFromDIVars, N2_DefaultInit |  |
| 0x130 | 4 float (unnamed) | 7 | Drone_SM_RouteMsgDCV, S:RollLeftCrouch, S:RollRightCrouch, S:StrafeDodgeLeft, S:StrafeDodgeRight, N2_DefaultInit | N2_PunchImpact |
| 0x138 | 4 float (unnamed) | 2 |  | FUN_0003b7f0, N2_PreDroneControl |
| 0x148 | 4 obj_tag * opponent | 72 | N2_FindOpponent, N2_SetOpponent | BOTSTATE_opponentIsMissile, BOTSTATE_pickupWeaponChangeChoice, BOTWEAP_tooCloseForWeapon, BOT_getAggressionMul, DroneVision_EnemyLookForOpponent, DroneVision_HaveOpponentSight, DroneVision_LogSeenOpponent, DroneWeap_AimTarget_IsOpponent, DroneWeap_DoOpponentTargetting, DroneWeap_FireWeapon, DroneWeap_OpponentHasWeapon, DroneWeap_OpponentIsAimingAtMe (+58) |
| 0x14c |  | 5 | N2_FindOpponent, N2_SetOpponent | DroneFunc_TargetSeen, Drone_GetOpponentInfo, S:BotCoverInit |
| 0x150 | 12 _VECTOR (unnamed) | 1 |  | Drone_GetOpponentInfo |
| 0x15c | 4 undefined4 (unnamed) | 2 |  | Drone_GetOpponentInfo, Pickup_Handler |
| 0x160 | 4 float (unnamed) | 1 |  | Drone_GetOpponentInfo |
| 0x168 | 4 float distanceToTarget | 32 | Drone_GetOpponentInfo | BOT_getAggressionMul, DroneAnim_CoverAnim, DroneFunc_InRangeOfTarget, DroneWeap_FireWeapon, DroneWeap_Ready2Fire, Drone_ModBulletDamage, FUN_00065510, N2_BondTalk, N2_CheckSurrender, N2_CheckUnsurrender, S:AimBackoff, S:AimCrouchFire (+19) |
| 0x16c |  | 1 |  | Drone_GetOpponentInfo |
| 0x174 | 4 float (unnamed) | 1 |  | Drone_GetOpponentInfo |
| 0x178 | 4 undefined4 (unnamed) | 1 | Drone_GetOpponentInfo |  |
| 0x17c | 4 float (unnamed) | 2 | Drone_GetOpponentInfo | FUN_00065510 |
| 0x180 | 4 undefined4 (unnamed) | 1 | Drone_GetOpponentInfo |  |
| 0x184 | 4 float angleToOpponentRadians | 3 | Drone_GetOpponentInfo | N2_BondIsFacingMe, N2_CheckUnsurrender |
| 0x188 | 4 float (unnamed) | 7 | Drone_GetOpponentInfo | N2_BondTalk, N2_CheckSurrender, S:BotAttackBackoff, S:BotGlobal, S:HeardNoiseAlert, S:UnderCoverLeaveNow |
| 0x18e |  | 1 |  | FUN_00066110 |
| 0x190 |  | 1 | DroneWeap_DoOpponentTargetting |  |
| 0x191 |  | 1 | DroneWeap_DoOpponentTargetting |  |
| 0x192 |  | 1 | DroneWeap_DoOpponentTargetting |  |
| 0x193 |  | 1 | DroneWeap_DoOpponentTargetting |  |
| 0x194 |  | 1 | DroneWeap_DoOpponentTargetting |  |
| 0x19c |  | 3 |  | DroneWeap_FireWeapon, DroneWeap_Ready2Fire, N2_SetOpponent |
| 0x1a8 |  | 1 |  | DroneWeap_FireWeapon |
| 0x1b4 | 12 _VECTOR maybeVectorToOpponent | 2 |  | N2_FindOpponent, N2_SetOpponent |
| 0x1b8 |  | 1 | N2_SetOpponent |  |
| 0x1c4 |  | 1 |  | DroneWeap_DoOpponentTargetting |
| 0x1c8 |  | 37 | S:Interogate | BOTSTATE_combatWeaponChangeChoice, DroneFunc_ReactionTime, DroneVision_EnemyLookForOpponent, DroneVision_HaveOpponentSight, DroneWeap_FireWeapon, FUN_00064910, FUN_00066110, N2_AttackTalk, S:AbseilHang, S:AbseilSlide, S:AllyFollowInit, S:AmbushWait (+24) |
| 0x1cc |  | 2 |  | FUN_000313e0, FUN_00065510 |
| 0x1d0 |  | 2 |  | FUN_00065510, FUN_00066110 |
| 0x1d4 |  | 2 |  | DroneVision_HaveOpponentSight, N2_SetOpponent |
| 0x1d6 |  | 1 |  | Pickup_Handler |
| 0x1d8 | 4 uint (unnamed) | 5 | DroneVision_HaveOpponentSight | Drone_GetOpponentInfo, S:BotGlobal, N2_HasOpponent, N2_SeenAndAttacking |
| 0x1dc | 4 undefined4 (unnamed) | 1 | Drone_GetOpponentInfo |  |
| 0x1e0 | 4 undefined4 (unnamed) | 1 | Drone_GetOpponentInfo |  |
| 0x1e4 | 4 float (unnamed) | 1 | Drone_GetOpponentInfo |  |
| 0x1e8 | 4 undefined4 (unnamed) | 6 |  | DroneFunc_LostSightTalk, DroneFunc_NewSightTalk, DroneVision_HaveOpponentSight, Drone_GetOpponentInfo, N2_AttackTalk, N2_SetOpponent |
| 0x1f4 |  | 1 |  | DroneVision_HaveOpponentSight |
| 0x200 |  | 2 |  | DroneVision_HaveOpponentSight, N2_SeenAndAttacking |
| 0x204 | 4 uint (unnamed) | 18 |  | DroneWeap_DoFiring, N2_AttackTalk, S:AimBackoff, S:AimStand, S:AimStandFire, S:BotAttack, S:BotAttackCrouch, S:BotAttackFire, S:BotAttackStrafeAimLeft, S:BotAttackStrafeAimRight, S:ProneFire, S:SniperFire (+6) |
| 0x208 |  | 1 | S:Interogate |  |
| 0x20c |  | 2 | S:Interogate | S:InterogateWalk |
| 0x210 |  | 1 | S:Interogate |  |
| 0x218 |  | 1 |  | S:Interogate |
| 0x230 |  | 7 | S:HostageSaved, N2_SetupHostageKiller | DroneAnim_EventFunc, S:Hostage, S:HostageKiller, S:HostageKillerAttack, N2_SetupHostageExecute |
| 0x234 | 4 int (unnamed) | 2 |  | DroneFunc_OnInitDeath, DroneFunc_SetDeathChannel |
| 0x238 |  | 2 |  | FUN_00064dc0, N2_NearArmedDrone |
| 0x240 | 4 undefined *32 (unnamed) | 1 | N2_FindOpponent |  |
| 0x244 | 4 int (unnamed) | 2 | Check_Target | N2_PreDroneControl |
| 0x248 | 4 int (unnamed) | 5 | N2_PreDroneControl | S:NinjaAttackLongRange, S:NinjaAttackMidRange, S:NinjaAttackShortRange, S:NinjaStandFire |
| 0x24c |  | 1 |  | S:CivilianDoorGuard |
| 0x284 |  | 1 |  | N2_ControlSTANDARD |
| 0x288 |  | 1 |  | N2_ControlSTANDARD |
| 0x290 |  | 7 |  | N2_CanRollLeft, N2_CanRollRight, N2_CanStepLeft, N2_CanStepRight, N2_CanStrafeLeft, N2_CanStrafeRight, N2_ControlSTANDARD |
| 0x294 | 4 uint (unnamed) | 2 | FUN_000454e0 | Drone_Control |
| 0x298 |  | 1 |  | DroneFunc_ConsiderExplosive |
| 0x29c |  | 2 |  | DroneFunc_ConsiderExplosive, DroneMove_FindSafetyFromScaryObject |
| 0x2a0 |  | 1 |  | DroneFunc_ConsiderExplosive |
| 0x2a4 |  | 1 |  | DroneFunc_ConsiderExplosive |
| 0x2a8 |  | 1 |  | DroneFunc_ConsiderExplosive |
| 0x2ac | 4 float (unnamed) | 2 | DroneFunc_ConsiderExplosive | DroneMove_FindSafetyFromScaryObject |
| 0x2b0 | 2 undefined2 (unnamed) | 1 | DroneFunc_ConsiderExplosive |  |
| 0x2b4 | 4 undefined4 maybeStopHeadTrackingOnFrame | 1 | N2_HeadTrackObj |  |
| 0x2bc | 4 int (unnamed) | 2 | N2_HeadTrackObj, N2_PostLoad_Init |  |
| 0x2cc |  | 1 |  | FUN_00065510 |
| 0x2dc |  | 1 |  | FUN_00066110 |
| 0x2e8 |  | 1 |  | FUN_00066110 |
| 0x2f4 |  | 1 |  | FUN_00066110 |
| 0x2fc |  | 1 |  | N2_DefaultInit |
| 0x318 | 12 _VECTOR (unnamed) | 1 |  | N2_Collision |
| 0x324 | 12 _VECTOR (unnamed) | 1 |  | N2_Collision |
| 0x368 | 4 cel_tag * (unnamed) | 1 | N2_Collision |  |
| 0x374 | 4 float (unnamed) | 1 | N2_Collision |  |
| 0x378 | 4 undefined4 (unnamed) | 1 | N2_Collision |  |
| 0x37c | 2 ushort (unnamed) | 2 | N2_Collision, N2_DefaultInit |  |
| 0x380 | 2 undefined2 (unnamed) | 1 | N2_Collision |  |
| 0x388 | 4 undefined4 (unnamed) | 1 | N2_PreDroneControl |  |
| 0x38c | 4 float (unnamed) | 1 | Drone_Control |  |
| 0x390 | 4 float (unnamed) | 1 | Drone_Control |  |
| 0x394 | 12 _VECTOR (unnamed) | 2 |  | BOT_fellOutMap, N2_DefaultInit |
| 0x3a0 | 12 _VECTOR (unnamed) | 2 |  | Drone_Control, N2_DefaultInit |
| 0x3ac | 4 float (unnamed) | 1 | N2_DefaultInit |  |
| 0x3b0 |  | 1 |  | N2_ControlSTANDARD |
| 0x3b4 |  | 1 |  | N2_ControlSTANDARD |
| 0x3bc |  | 3 |  | DroneAnim_CallHandler, DroneAnim_SetScript, S:CombatNoMove |
| 0x3c0 | 4 undefined4 (unnamed) | 2 | N2_PreDroneControl | N2_ControlSTANDARD |
| 0x3ca | 2 undefined2 (unnamed) | 1 | N2_DefaultInit |  |
| 0x3cc | 4 undefined4 speechChannel | 11 | DroneFunc_DoBondTalk, DroneFunc_InjuredTalk, DroneFunc_LostSightTalk, DroneFunc_NewSightTalk, Drone_IsTalking, N2_AttackTalk, N2_CivilianScaredTalk, N2_ControlSTANDARD, S:CivilianChallenge, N2_DefaultInit, N2_SpeechSFX |  |
| 0x3d4 | 4 uint * maybeCurrentBehaviour | 76 | FUN_0003a410, N2_ChangeToAttackMode, N2_DoModeSettingsNEW, N2_DoModeSettingsOLD | DroneAnim_CoverAnim, DroneFunc_ConsiderExplosive, DroneFunc_FirstAttack, DroneFunc_HandleExplosives, DroneFunc_HandleImpact, DroneFunc_HandleSoundAlerts, DroneFunc_OnInitDeath, DroneFunc_SendHurtMessage, DroneVision_ConsiderAlerted, DroneVision_EnemyLookForOpponent, DroneVision_HaveOpponentSight, DroneWeap_DoFiring (+60) |
| 0x3d8 | 4 undefined4 (unnamed) | 3 | N2_DoModeSettingsOLD | FUN_0003a410, N2_DoModeSettingsNEW |
| 0x3dc | 4 undefined4 (unnamed) | 1 | N2_DoModeSettingsOLD |  |
| 0x3e0 | 4 undefined4 (unnamed) | 1 | N2_DoModeSettingsOLD |  |
| 0x3e4 | 4 undefined4 maybeSecondBehaviour | 3 | N2_DoModeSettingsOLD | N2_ChangeToAttackMode, N2_DoModeSettingsNEW |
| 0x3e8 | 4 undefined4 (unnamed) | 1 | N2_DoModeSettingsOLD |  |
| 0x3ec | 4 undefined4 (unnamed) | 1 | N2_DoModeSettingsOLD |  |
| 0x3f0 | 4 undefined4 (unnamed) | 4 | N2_DefaultInit, N2_SetAsDead | DroneWeap_AimTarget_IsOpponent, N2_SetAngleToObj |
| 0x3f4 | 4 uint (unnamed) | 186 | DroneAnim_SetHTAnim, DroneFunc_DoForcedAttack, DroneFunc_SendHurtMessage, DroneFunc_SetAsAttacking, DroneInit_Collision, DroneVision_ConsiderAlerted, DroneVision_EnemyLookForOpponent, Drone_Control, FUN_0003a410, FUN_0003ea80, FUN_00064910, S:AbseilDeath, S:AbseilHang, S:AbseilInit, S:AbseilSlide, S:AbseilStepOff, S:ActionAnim, S:AimBackoff, S:AimCrouch, S:AimCrouchFire, S:AimCrouchReload, S:AimStand, S:AimStandDiscard, S:AimStandFire, S:AimStandReload, S:AlertToPosition, S:AllyFollow, S:AllyFollowInit, S:AllyFollowWait, S:AllyGoToGoalPosition, S:AllyLead, S:AllyLeadBondCombat, S:AllyLeadHide, S:AllyLeadPlayerInWay, S:AllyLeadWait, S:AltAttack, S:AstronautCombat, S:AstronautLaunch, S:Attack, S:BotGlobal, S:Civilian, S:CivilianChallenge, S:CivilianDoorGuard, S:CivilianGuard, S:CivilianHiding, S:CivilianMission, S:CivilianMissionWait, S:CivilianPatrol, S:CivilianScared, S:Combat, S:CombatNoMove, S:CombatNoRoute, S:CombatNoSight, S:CombatOutOfRange, S:CombatWait, S:CrouchCover, S:DrawWeapon, S:DroneStuck, S:ElevatorJumper, S:EnemyRunToPoint, S:ExplosiveImpact, S:GoToGoalPosition, S:GrenadeThrow, S:HeardNoiseAlert, S:HeardNoiseAware, S:HeardNoiseSuspect, S:HideFromScaryObject, S:HoldItRightThere, S:HostageHide, S:HostageKiller, S:HostageKillerAttack, S:Interogate, S:InterogateWalk, S:Investigate, S:KickObject, S:KikoMission, S:KikoMissionRun, S:KnockedOut_Anim, S:NinjaAttackLongRange, S:NinjaAttackMidRange, S:NinjaAttackShortRange, S:NinjaBackflip, S:NinjaGetCloseToPlayer, S:NinjaNoRoute, S:NinjaSideflip, S:NinjaSideflipLeft, S:NinjaSideflipRight, S:NinjaSomersault, S:NinjaStandFire, S:Obstructed, S:OpenDoor, S:PartyGirl, S:PlayScript, S:PressAlarm, S:Prone, S:ProneFire, S:PunchImpact, S:RollLeftCrouch, S:RollRightCrouch, S:RunAwayFromObject, S:RunForCover, S:RunToAlarm, S:SearchArea, S:SeenDeadBody, S:SeenSurrenderedDrone, S:SmokedOut_Recover, S:SniperAim, S:SniperFire, S:SniperIdle, S:SniperReload, S:SpaceDrake, S:SpecialDeath_Anim, S:StandFiddle, S:StepAimLeft, S:StepAimRight, S:StrafeAimLeft, S:StrafeAimRight, S:StrafeDodgeLeft, S:StrafeDodgeRight, S:StunDartRecover, S:StunGrenadeRecover, S:Surrender_Anim, S:Taser, S:TruckDriverIdle, S:TruckDriverInit, S:TruckDriverMission, S:UnderCoverAim, S:UnderCoverFire, S:UnderCoverIdle, S:UnderCoverInit, S:UnderCoverSniperFire, S:UnderCoverSniperReload, N2_DefaultInit, N2_DoModeSettingsNEW, N2_Enable, N2_ExplosiveImpact, N2_initDTYPE_SniperAlert | DroneFunc_FirstAttack, DroneFunc_HandleImpact, DroneFunc_HandleSoundAlerts, DroneFunc_OnInitDeath, DroneVision_EnemyAlerts, Drone_GetOpponentInfo, Drone_SM_RouteMsgDCV, FUN_0003cce0, FUN_0003ef30, FUN_00045340, FUN_000453e0, FUN_00045490 (+37) |
| 0x3f8 | 4 uint (unnamed) | 20 | DroneFunc_SendHurtMessage, DroneVision_ConsiderAlerted, FUN_0003a410, FUN_0003ea80, S:Civilian, S:CivilianChallenge, S:CivilianDoorGuard, S:CivilianGuard, S:CivilianMission, S:CivilianMissionWait, S:CivilianPatrol, S:Dead, S:PartyGirl, S:Surrender_Anim, S:Taser, N2_DefaultInit, N2_ExplosiveImpact | DroneVision_EnemyLookForOpponent, FUN_00065510, S:GoToGoalPosition |
| 0x3fc | 4 float (unnamed) | 5 | N2_DoModeSettingsNEW, N2_DoModeSettingsOLD | N2_ControlSTANDARD, S:BotDoorOpen, S:OpenDoor |
| 0x400 | 4 undefined4 (unnamed) | 1 | N2_DoModeSettingsNEW |  |
| 0x404 | 4 float (unnamed) | 1 | N2_PreDroneControl |  |
| 0x408 | 4 float cumulativeAlertnessScaled | 47 | DroneFunc_HandleSoundAlerts, DroneFunc_SetAsAttacking, FUN_0003ea80, FUN_00064910, N2_ControlSTANDARD, S:Attack, S:BotGlobal, S:Combat, S:CombatNoSight, S:CombatOutOfRange, S:CombatWait, S:HoldItRightThere, S:HostageHide, S:NinjaAttack, S:NinjaAttackLongRange, S:NinjaAttackMidRange, S:NinjaAttackShortRange, S:NinjaNoRoute, S:SearchArea, S:SeenDeadBody, S:SeenSurrenderedDrone, N2_DoModeSettingsNEW, N2_DoModeSettingsOLD, N2_ExplosiveImpact, N2_PunchImpact | DroneFunc_ReactionTime, DroneVision_CanSeeObjectFrom, DroneVision_ConsiderAlerted, DroneVision_EnemyLookForOpponent, FUN_00065510, N2_CheckSurrender, S:AlertToPosition, S:BotDoorOpen, S:CastleChatGuard1, S:CivilianGuard, S:CivilianMission, S:CivilianMissionWait (+10) |
| 0x40c | 4 float currentAlertness | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x410 | 4 float currentAlertnessScaled | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x414 | 4 cel_tag * (unnamed) | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x418 | 1 char alertStatus | 1 | Drone_AlertStatusSet |  |
| 0x419 | 1 undefined1 lastAlertStatus | 1 | Drone_AlertStatusSet |  |
| 0x41a | 1 undefined1 (unnamed) | 1 | Drone_AlertStatusSet |  |
| 0x41c | 4 undefined4 lastAlertStateChangeTime | 1 | Drone_AlertStatusSet |  |
| 0x420 | 4 int (unnamed) | 1 | DroneFunc_SetAsAttacking |  |
| 0x424 |  | 2 |  | DroneVision_ConsiderAlerted, S:InterogateAssistWait |
| 0x42c | 4 sAnimScript_tag_xbox * someAnimScript | 5 | DroneAnim_SetHTAnim | DroneAnim_EventFunc, DroneAnim_SnapRotate, S:TruckDriverInitAlert, N2_Enable |
| 0x434 | 4 sAnimScript_tag_xbox * (unnamed) | 6 |  | DroneAnim_CallHandler, DroneAnim_CanSetAnimCall, DroneAnim_SetDAnimInternal, DroneAnim_SetHTAnim, DroneWeap_Ready2Fire, FUN_0003cce0 |
| 0x438 | 4 int maybeFacialAnim | 5 |  | DroneFunc_DoBondTalk, N2_BondTalk, N2_ControlSTANDARD, N2_PlayFacialAnim, N2_SpeechSFX |
| 0x43c | 4 undefined4 (unnamed) | 1 | N2_PunchImpact |  |
| 0x440 | 4 undefined * someFunc1 | 3 | DroneAnim_SetDAnimInternal, DroneAnim_SetHTAnim | Drone_Control |
| 0x444 | 4 undefined * someFunc2 | 3 | DroneAnim_SetDAnimInternal, DroneAnim_SetHTAnim | Drone_Control |
| 0x448 | 4 float (unnamed) | 2 | DroneAnim_SetHTAnim, DroneAnim_SnapRotate |  |
| 0x450 | 4 obj_tag * (unnamed) | 7 | S:CastleChatGuard1, S:PlayScript, S:TruckDriverInit, N2_DefaultInit | S:ActionAnim, S:Global, S:WaitSwitch |
| 0x454 | 2 ushort (unnamed) | 7 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetDAnimInternal, DroneAnim_SetScript | DroneAnim_CanSetAnimCall |
| 0x456 | 1 undefined1 (unnamed) | 5 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetScript |  |
| 0x457 |  | 5 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetScript |  |
| 0x458 | 2 undefined2 (unnamed) | 6 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetScript | DroneAnim_CanSetAnimCall |
| 0x45a | 1 undefined1 (unnamed) | 5 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetScript |  |
| 0x45b |  | 5 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetScript |  |
| 0x45c | 2 ushort (unnamed) | 5 | DroneAnim_CallAnim, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim | DroneAnim_CallFullyComplete, DroneAnim_SetScript |
| 0x460 | 4 undefined4 (unnamed) | 5 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetScript |  |
| 0x464 |  | 4 | DroneAnim_SetScript | DroneAnim_CallHandler, DroneAnim_CanSetAnimCall, DroneWeap_Ready2Fire |
| 0x465 |  | 1 | DroneAnim_SetScript |  |
| 0x466 | 2 ushort (unnamed) | 10 | DroneAnim_SetScript | DroneAnim_CallAnim, DroneAnim_CanSetAnimCall, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetDAnimInternal, DroneWeap_AimTarget_IsOpponent, S:AlertToPosition, S:HeardNoiseSuspect, S:Taser |
| 0x468 | 2 undefined2 (unnamed) | 3 | DroneAnim_SetScript, N2_DefaultInit | FUN_000332b0 |
| 0x46a |  | 1 |  | DroneAnim_SetDAnimInternal |
| 0x46c | 4 uint (unnamed) | 5 | DroneAnim_SetScript | DroneAnim_SetDAnimInternal, Drone_GetOpponentInfo, FUN_0003cce0, N2_SetAngleToObj |
| 0x470 | 4 uint (unnamed) | 1 |  | DroneAnim_SetHTAnim |
| 0x474 |  | 2 | DroneAnim_CallHandler, DroneAnim_SetDAnimInternal |  |
| 0x475 | 1 undefined1 (unnamed) | 3 | DroneAnim_EventFunc, DroneAnim_SetHTAnim | DroneAnim_CallHandler |
| 0x476 | 1 undefined1 (unnamed) | 2 | DroneAnim_CallHandler, DroneAnim_SetHTAnim |  |
| 0x477 | 1 undefined1 (unnamed) | 6 | DroneAnim_CallAnim, DroneAnim_CallHandler, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetAnimD, DroneAnim_SetScript |  |
| 0x478 | 4 HASHCODE animHashcode | 2 | DroneAnim_SetHTAnim | DroneAnim_SetDAnimInternal |
| 0x47c | 4 int (unnamed) | 2 | DroneAnim_SetHTAnim | Drone_Control |
| 0x480 |  | 3 | DroneAnim_SetScript, S:PressAlarm | DroneAnim_CallHandler |
| 0x481 |  | 1 | DroneAnim_SetScript |  |
| 0x482 |  | 1 | DroneAnim_SetScript |  |
| 0x484 | 4 float (unnamed) | 5 | DroneAnim_CallAnim, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetScript | DroneAnim_CallHandler |
| 0x488 | 4 float (unnamed) | 2 | DroneAnim_SetScript | Drone_Control |
| 0x48c |  | 1 |  | DroneAnim_SetDAnimInternal |
| 0x490 |  | 3 |  | DroneAnim_SetScript, S:PressAlarm, S:SpecialDeath_Anim |
| 0x492 | 2 short (unnamed) | 8 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetAnimD, DroneAnim_SetScript, S:AbseilDeath, S:SpecialDeath_Anim |  |
| 0x494 | 4 int (unnamed) | 7 | DroneAnim_CallAnim, DroneAnim_CallFullyComplete, DroneAnim_LocationDeathAnim, DroneAnim_LocationImpactAnim, DroneAnim_SetAnimD, DroneAnim_SetScript, S:SpecialDeath_Anim |  |
| 0x49c | 2 undefined2 (unnamed) | 12 | BOTSTATE_gotoGoal, DroneVision_EnemyAlerts, S:CastleChatGuard1, S:CombatOutOfRange, S:EnemyRunToPoint, S:RunToAlarm | S:AlertToPosition, S:HeardNoiseAware, S:HeardNoiseSuspect, S:Obstructed, S:RecoverFromScaryObject, S:SearchArea |
| 0x49e | 2 short (unnamed) | 35 | BOTSTATE_gotoGoal, DroneFunc_FirstAttack, FUN_0003a410, S:AllyLead, S:AllyLeadBondCombat, S:AllyLeadPlayerInWay, S:AllyLeadWait, S:Attack, S:BotAttackReload, S:BotGlobal, S:CivilianDoorGuard, S:CivilianGuard, S:CivilianScared, S:CombatOutOfRange, S:EnemyRunToPoint, S:HeardNoiseSuspect, S:HostageSaved, S:ReturnToPatrolPath, S:RunToAlarm, S:SearchArea, S:SeenDeadBody, S:SeenSurrenderedDrone, N2_DefaultInit, N2_ReachedDestNode | FUN_0003fd20, S:AimStandDiscard, S:AlertToPosition, S:AllyGoToGoalPosition, S:DrawWeapon, S:Global, S:GoToGoalPosition, S:HostageGoToGoalPosition, S:PlayScript, S:RunAwayFromObject, S:WaitSwitch |
| 0x4a0 | 2 short (unnamed) | 7 | FUN_0003a410, N2_ChangeToAttackMode, N2_DefaultInit | DroneFunc_FirstAttack, S:CivilianMission, S:EnemyMission, S:HostageSaved |
| 0x4a2 |  | 11 | S:BotAlertToPosition, S:BotAttack, S:BotAttackFire, S:BotAttackRun, S:BotGotoGoalPosition | S:ActionAnim, S:BotDoorOpen, S:KickObject, S:OpenDoor, S:StandFiddle, N2_ReachedDestNode |
| 0x4a4 | 12 _VECTOR (unnamed) | 2 |  | DroneVision_EnemyAlerts, S:SearchArea |
| 0x4b0 | 4 cel_tag * (unnamed) | 1 | DroneVision_EnemyAlerts |  |
| 0x4cc |  | 1 |  | FUN_00046d90 |
| 0x4d8 |  | 1 | FUN_00046d90 |  |
| 0x518 | 4 float dist2dtosomepoint | 2 | FUN_00046d90, N2_maybeCalcDistanceAndDirToTarget |  |
| 0x51c | 4 float (unnamed) | 1 | N2_maybeCalcDistanceAndDirToTarget |  |
| 0x520 | 12 _VECTOR maybeTargetPos | 4 | S:AbseilInit | FUN_00046d90, N2_DefaultInit, N2_maybeCalcDistanceAndDirToTarget |
| 0x524 |  | 1 | S:AbseilInit |  |
| 0x528 |  | 1 | S:AbseilInit |  |
| 0x52c | 4 int * (unnamed) | 2 | N2_DefaultInit, N2_maybeCalcDistanceAndDirToTarget |  |
| 0x530 | 4 undefined4 (unnamed) | 3 | N2_maybeCalcDistanceAndDirToTarget | S:SpecialDeath_Anim, N2_DefaultInit |
| 0x534 | 4 float angletosomepoint | 7 | DroneAnim_SnapRotate, DroneWeap_AimTarget_IsOpponent, S:CivilianDoorGuard, S:PressAlarm, S:TruckDriverInit, N2_SetAngleToObj, N2_maybeCalcDistanceAndDirToTarget |  |
| 0x538 | 4 undefined4 (unnamed) | 1 | N2_maybeCalcDistanceAndDirToTarget |  |
| 0x564 | 40 AIPoint_tag someGoalPoint | 11 |  | BOTSTATE_gotoGoal, FUN_00046d90, S:AllyGoToGoalPosition, S:BotGotoGoalPosition, S:BotGuardFriendFollow, S:CombatOutOfRange, S:GoToGoalPosition, S:HostageGoToGoalPosition, S:RunAwayFromObject, S:SearchArea, N2_ReFindMissionPath |
| 0x568 |  | 2 |  | S:BotCoverRunTo, S:RunForCover |
| 0x574 |  | 2 |  | S:AlertToPosition, S:BotAlertToPosition |
| 0x578 |  | 11 |  | S:AlertToPosition, S:AllyGoToGoalPosition, S:BotAlertToPosition, S:BotCoverRunTo, S:BotGotoGoalPosition, S:BotGuardFriendFollow, S:GoToGoalPosition, S:HostageGoToGoalPosition, S:RunAwayFromObject, S:RunForCover, N2_ReFindMissionPath |
| 0x57c |  | 14 |  | S:AllyGoToGoalPosition, S:BotCoverIdle, S:BotCoverInit, S:BotCoverReturn, S:BotCoverRunTo, S:BotGotoGoalPosition, S:BotGuardFriendFollow, S:GoToGoalPosition, S:HostageGoToGoalPosition, S:RunAwayFromObject, S:RunForCover, S:UnderCoverIdle (+2) |
| 0x590 |  | 1 |  | S:PressAlarm |
| 0x630 | 4 undefined4 (unnamed) | 7 | FUN_00046d90, N2_DefaultInit, N2_InvalidateAttackRoute, N2_ReFindMissionPath | BOTSTATE_isPathWithinObjectRange, S:OpenDoor, S:RunForCover |
| 0x634 | 160 AIRoute_tag someAiRoute | 6 |  | FUN_00046d90, S:CombatOutOfRange, S:SearchArea, N2_DefaultInit, N2_InvalidateAttackRoute, N2_ReFindMissionPath |
| 0x63c |  | 1 |  | S:DroneStuck |
| 0x63d |  | 1 |  | S:DroneStuck |
| 0x63e |  | 1 |  | S:DroneStuck |
| 0x63f |  | 1 |  | S:DroneStuck |
| 0x648 |  | 1 |  | FUN_00046d90 |
| 0x654 |  | 1 |  | FUN_00046d90 |
| 0x678 |  | 1 | FUN_00046d90 |  |
| 0x679 |  | 1 | FUN_00046d90 |  |
| 0x680 |  | 2 |  | FUN_00046d90, S:BotAttackRun |
| 0x684 |  | 2 |  | FUN_00046d90, N2_DefaultInit |
| 0x688 |  | 1 |  | N2_DefaultInit |
| 0x6c4 |  | 1 |  | N2_DefaultInit |
| 0x6c8 |  | 3 |  | N2_DefaultInit, N2_PostLoad_Init, N2_ReFindMissionPath |
| 0x6d8 | 2 ushort (unnamed) | 3 |  | S:AllyLeadMissionWait, N2_DefaultInit, N2_ReFindMissionPath |
| 0x6dc |  | 3 |  | S:CivilianMission, S:EnemyMission, S:TruckDriverMission |
| 0x70c | 16 CelPos_tag (unnamed) | 1 |  | N2_ReFindMissionPath |
| 0x71c | 2 short (unnamed) | 1 | N2_ReFindMissionPath |  |
| 0x71e | 2 ushort (unnamed) | 8 |  | S:Alert, S:AllyLeadInit, S:AstronautLaunch, S:CivilianPatrol, S:Idle, S:PartyGirl, S:TruckDriverInit, N2_ReFindMissionPath |
| 0x728 | 4 undefined4 (unnamed) | 1 | N2_DefaultInit |  |
| 0x72c | 4 undefined4 (unnamed) | 1 | N2_DefaultInit |  |
| 0x768 | 4 int * * (unnamed) | 2 | N2_DefaultInit | N2_ReFindMissionPath |
| 0x76c | 4 undefined4 (unnamed) | 2 | N2_DefaultInit | N2_ReFindMissionPath |
| 0x77c | 48 AITarget_tag someGoalTarget | 6 | N2_DefaultInit | BOTSTATE_gotoGoal, FUN_00046d90, S:CombatOutOfRange, S:SearchArea, N2_ReFindMissionPath |
| 0x780 |  | 1 | N2_DefaultInit |  |
| 0x784 |  | 1 | N2_DefaultInit |  |
| 0x788 |  | 1 | N2_DefaultInit |  |
| 0x78c |  | 1 | N2_DefaultInit |  |
| 0x790 |  | 1 | N2_DefaultInit |  |
| 0x794 |  | 1 |  | N2_DefaultInit |
| 0x798 |  | 1 | N2_DefaultInit |  |
| 0x79c |  | 1 | N2_DefaultInit |  |
| 0x7a0 |  | 1 | N2_DefaultInit |  |
| 0x7a4 |  | 1 |  | N2_DefaultInit |
| 0x7a6 |  | 1 | N2_DefaultInit |  |
| 0x7a8 |  | 1 | N2_DefaultInit |  |
| 0x7b4 | 4 undefined4 (unnamed) | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x7b8 | 4 undefined4 (unnamed) | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x7bc | 12 _VECTOR (unnamed) | 1 |  | DroneFunc_HandleSoundAlerts |
| 0x7c8 | 4 cel_tag * (unnamed) | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x7e0 | 4 undefined4 (unnamed) | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x7e4 | 4 int (unnamed) | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x7e8 | 4 float (unnamed) | 1 | DroneFunc_HandleSoundAlerts |  |
| 0x824 |  | 2 |  | FUN_00046d90, N2_DefaultInit |
| 0x825 | 1 byte (unnamed) | 1 | N2_DefaultInit |  |
| 0x828 | 4 undefined4 (unnamed) | 2 | N2_DefaultInit | N2_Collision |
| 0x82c | 4 int (unnamed) | 3 | N2_ReachedDestNode | Drone_Control, S:OpenDoor |
| 0x830 |  | 1 |  | N2_ReachedDestNode |
| 0x832 |  | 1 |  | N2_ReachedDestNode |
| 0x838 |  | 17 |  | DroneAnim_CoverAnim, S:BotCoverAim, S:BotCoverFire, S:BotCoverIdle, S:BotCoverInit, S:BotCoverLeave, S:BotCoverReturn, S:BotCoverRunTo, S:BotCoverTypeChange, S:RunForCover, S:UnderCoverAim, S:UnderCoverFire (+5) |
| 0x83c |  | 15 | DroneAnim_CoverAnim | S:BotCoverFire, S:BotCoverIdle, S:BotCoverLeave, S:BotCoverLeaveNow, S:BotCoverReturn, S:BotCoverRunTo, S:BotCoverTypeChange, S:RunForCover, S:UnderCoverFire, S:UnderCoverIdle, S:UnderCoverLeaveNow, S:UnderCoverReturn (+2) |
| 0x83e |  | 1 | DroneAnim_CoverAnim |  |
| 0x83f |  | 2 | DroneAnim_CoverAnim | S:BotCoverFire |
| 0x840 | 2 short (unnamed) | 13 | DroneWeap_ChangeWeapon, DroneWeap_FireWeapon, S:AimCrouchReload, S:AimStandReload, S:SniperReload, S:UnderCoverIdle, S:UnderCoverSniperReload, N2_DefaultInit, Pickup_Handler | S:AimCrouchFire, S:BotGlobal, S:SniperFire, S:UnderCoverSniperFire |
| 0x842 | 2 short (unnamed) | 8 | DroneWeap_ChangeWeapon, N2_DefaultInit, Pickup_Handler | S:AimCrouchReload, S:AimStandReload, S:SniperReload, S:UnderCoverIdle, S:UnderCoverSniperReload |
| 0x844 |  | 4 | S:BotAttackReload | DroneWeap_DoFiring, DroneWeap_NextBulletTime, FUN_00067590 |
| 0x848 |  | 3 |  | DroneWeap_DoFiring, DroneWeap_NextBulletTime, FUN_00067590 |
| 0x84c |  | 4 |  | DroneWeap_FireWeapon, DroneWeap_NextBulletTime, FUN_00067590, Pickup_Handler |
| 0x850 | 2 ushort (unnamed) | 1 | N2_DefaultInit |  |
| 0x852 | 2 ushort (unnamed) | 1 | N2_DefaultInit |  |
| 0x854 | 4 float (unnamed) | 2 | N2_DefaultInit | S:BotCoverIdle |
| 0x858 | 4 obj_tag * (unnamed) | 6 | S:SpecialDeath_Anim, N2_DefaultInit | DroneInit_Collision, S:DeathByExplosion, S:Death_Anim, S:HostageKillerAttack |
| 0x85c | 4 undefined4 (unnamed) | 2 | N2_DefaultInit | S:SpecialDeath_Anim |
| 0x860 | 4 undefined4 (unnamed) | 1 | N2_DefaultInit |  |
| 0x864 | 4 float (unnamed) | 1 | N2_DefaultInit |  |
| 0x868 | 4 float (unnamed) | 2 | N2_DefaultInit | S:SpecialDeath_Anim |
| 0x86c | 4 void * (unnamed) | 2 | N2_DefaultInit | S:SpecialDeath_Anim |
| 0x870 | 4 float (unnamed) | 1 | N2_DefaultInit |  |
| 0x874 | 164 DIVars_tag diVars | 3 | N2_CreateFromDIVars | DroneSpawner_Init, N2_PostLoad_Init |
| 0x878 |  | 1 |  | N2_CreateFromDIVars |
| 0x884 |  | 1 |  | N2_CreateFromDIVars |
| 0x888 |  | 1 |  | S:CivilianDoorGuard |
| 0x894 |  | 1 |  | N2_CreateFromDIVars |
| 0x8a8 |  | 2 |  | FUN_0003a410, N2_ChangeToAttackMode |
| 0x8b8 |  | 1 |  | BOT_init |
| 0x8de |  | 1 |  | Pickup_Handler |
| 0x918 | 1 undefined1 (unnamed) | 65 | Drone_SM_RouteMsgDCV, S:AbseilInit, S:ActionAnim, S:AimBackoff, S:AimStandFire, S:AlertToPosition, S:AllyLeadHide, S:AstronautCombatMove, S:AstronautDeath, S:AstronautHit, S:BotAlertToPosition, S:BotAttackBackoff, S:BotAttackRunChangePosition, S:BotAttackStrafeAimLeft, S:BotAttackStrafeAimRight, S:BotCoverIdle, S:BotDead, S:BotDoorOpen, S:BotIdle, S:BotImpactStunGrenade, S:CivilianDoorGuard, S:CivilianMissionWait, S:CombatNoMove, S:CombatNoRoute, S:CombatWait, S:CrouchCover, S:Dead, S:DeathByExplosion, S:Death_Anim, S:ElevatorJumper, S:HeardNoiseAlert, S:HostageDead, S:Interogate, S:KickObject, S:NinjaAttackMidRange, S:NinjaAttackShortRange, S:NinjaGetCloseToPlayer, S:NinjaNoRoute, S:NinjaStandFire, S:NoOpponent, S:Obstructed, S:OpenDoor, S:PressAlarm, S:SearchArea, S:SmokedOut_Loop, S:SniperFire, S:SniperReload, S:SpecialDeath_Anim, S:StandFiddle, S:StrafeAimLeft, S:StrafeAimRight, S:StrafeDodgeLeft, S:StrafeDodgeRight, S:StunDartLoop, S:StunGrenadeLoop, S:Stunned, S:Surrender_Anim, S:Taser, S:Tester1, S:Tester3, S:Tester4, S:TruckDriverMission, S:UnderCoverIdle, S:UnderCoverSniperFire, N2_PreDroneControl |  |
| 0x91c | 4 uint someTimeout | 64 | S:AbseilInit, S:ActionAnim, S:AimBackoff, S:AimStandFire, S:AlertToPosition, S:AllyLeadHide, S:AstronautCombatMove, S:AstronautDeath, S:AstronautHit, S:BotAlertToPosition, S:BotAttackBackoff, S:BotAttackRunChangePosition, S:BotAttackStrafeAimLeft, S:BotAttackStrafeAimRight, S:BotCoverIdle, S:BotDead, S:BotDoorOpen, S:BotIdle, S:BotImpactStunGrenade, S:CivilianDoorGuard, S:CivilianMissionWait, S:CombatNoMove, S:CombatNoRoute, S:CombatWait, S:CrouchCover, S:Dead, S:DeathByExplosion, S:Death_Anim, S:ElevatorJumper, S:HeardNoiseAlert, S:HostageDead, S:Interogate, S:KickObject, S:NinjaAttackMidRange, S:NinjaAttackShortRange, S:NinjaGetCloseToPlayer, S:NinjaNoRoute, S:NinjaStandFire, S:NoOpponent, S:Obstructed, S:OpenDoor, S:PressAlarm, S:SearchArea, S:SmokedOut_Loop, S:SniperFire, S:SniperReload, S:SpecialDeath_Anim, S:StandFiddle, S:StrafeAimLeft, S:StrafeAimRight, S:StrafeDodgeLeft, S:StrafeDodgeRight, S:StunDartLoop, S:StunGrenadeLoop, S:Stunned, S:Surrender_Anim, S:Taser, S:Tester1, S:Tester3, S:Tester4, S:TruckDriverMission, S:UnderCoverIdle, S:UnderCoverSniperFire | N2_PreDroneControl |
| 0x920 | 4 undefined4 (unnamed) | 64 | S:AbseilInit, S:ActionAnim, S:AimBackoff, S:AimStandFire, S:AlertToPosition, S:AllyLeadHide, S:AstronautCombatMove, S:AstronautDeath, S:AstronautHit, S:BotAlertToPosition, S:BotAttackBackoff, S:BotAttackRunChangePosition, S:BotAttackStrafeAimLeft, S:BotAttackStrafeAimRight, S:BotCoverIdle, S:BotDead, S:BotDoorOpen, S:BotIdle, S:BotImpactStunGrenade, S:CivilianDoorGuard, S:CivilianMissionWait, S:CombatNoMove, S:CombatNoRoute, S:CombatWait, S:CrouchCover, S:Dead, S:DeathByExplosion, S:Death_Anim, S:ElevatorJumper, S:HeardNoiseAlert, S:HostageDead, S:Interogate, S:KickObject, S:NinjaAttackMidRange, S:NinjaAttackShortRange, S:NinjaGetCloseToPlayer, S:NinjaNoRoute, S:NinjaStandFire, S:NoOpponent, S:Obstructed, S:OpenDoor, S:PressAlarm, S:SearchArea, S:SmokedOut_Loop, S:SniperFire, S:SniperReload, S:SpecialDeath_Anim, S:StandFiddle, S:StrafeAimLeft, S:StrafeAimRight, S:StrafeDodgeLeft, S:StrafeDodgeRight, S:StunDartLoop, S:StunGrenadeLoop, S:Stunned, S:Surrender_Anim, S:Taser, S:Tester1, S:Tester3, S:Tester4, S:TruckDriverMission, S:UnderCoverIdle, S:UnderCoverSniperFire, N2_PreDroneControl |  |
| 0x924 | 1 undefined1 (unnamed) | 5 | Drone_SM_RouteMsgDCV, S:BotDead, S:Dead, S:UnderCoverSniperFire, N2_PreDroneControl |  |
| 0x928 | 4 uint maybeDiedOnFrame | 4 | S:BotDead, S:Dead, S:UnderCoverSniperFire | N2_PreDroneControl |
| 0x92c | 4 undefined4 (unnamed) | 4 | S:BotDead, S:Dead, S:UnderCoverSniperFire, N2_PreDroneControl |  |
| 0x930 | 4 int (unnamed) | 23 | DroneFunc_DoBondTalk, N2_BondTalk, S:AbseilDeath, S:AbseilInit, S:AllyLeadHide, S:AstronautLaunch, S:BotCoverIdle, S:Civilian, S:CivilianDoorGuard, S:CivilianGuard, S:CivilianMission, S:CivilianMissionWait, S:CombatNoRoute, S:DeathByExplosion, S:Death_Anim, S:ElevatorJumper, S:Interogate, S:PartyGirl, S:PressAlarm, S:SpecialDeath_Anim, S:Taser, S:UnderCoverIdle | S:Stunned |
| 0x934 | 4 uint (unnamed) | 10 | DroneFunc_DoBondTalk, N2_BondTalk, S:AbseilDeath, S:AstronautLaunch, S:Civilian, S:CivilianDoorGuard, S:CivilianGuard, S:CivilianMission, S:CivilianMissionWait, S:PartyGirl |  |
| 0x938 | 4 int (unnamed) | 3 | DroneFunc_DoBondTalk, S:StunDartImpact, S:Taser |  |
| 0x948 |  | 1 |  | FUN_0003c430 |
| 0x94c |  | 1 |  | FUN_0003c430 |
| 0x950 |  | 1 | S:Civilian |  |
| 0x954 |  | 1 | S:Fade |  |
| 0x974 | 4 BOT_vars_t * botWeaponInfoEtc | 38 | BOT_init | BOTSTATE_combatWeaponChangeChoice, BOTSTATE_defaultCombatRange, BOTSTATE_gotoGoal, BOTSTATE_opponentIsMissile, BOTSTATE_pickGoal, BOTSTATE_processGoals, BOTSTATE_setPickupVisitTime, BOTSTATE_uninitGoal, BOTWEAP_AmmoInGun, BOTWEAP_EquipWeapon, BOTWEAP_InitWeapon, BOTWEAP_decrRounds (+25) |

## StateMachineInfo_tag accesses (SM at Drone+0xec; +0x28 = Drone+0x114 processFunction)

Drone_SM_SetState calls are counted as writes of +0x0c/+0x18/+0x24.

| SM off | Drone off | #fn | functions |
|---|---|---|---|
| 0x00 | 0x0ec | 13 | DroneAnim_EventFunc(R), DroneFunc_DeadDrone(R), Drone_SM_RouteMsg(R), Drone_SM_RouteMsgDCV(R), Drone_SM_SendMsgSelf(R), Drone_SM_SetState(R), MP_BluePrintUpdate(R), MP_GoldenEyeUpdate(R), MP_PlayerKilled(R), MP_UplinkUpdate(R), MP_setPlayerStatus(R), S:HostageKiller(R), S:HostageKillerAttack(R) |
| 0x04 | 0x0f0 | 48 | BOTSTATE_gotoGoal(R), BOTSTATE_processGoals(R), DroneVision_EnemyAlerts(R), DroneVision_EnemyLookForOpponent(R), DroneWeap_FireWeapon(R), Drone_SM_RouteMsg(R), Drone_SM_RouteMsgDCV(AR), FUN_00038ff0(A), FUN_00064910(R), S:ActionAnim(R), S:AimCrouch(R), S:AimCrouchFire(R), S:AimStandFire(R), S:AllyFollow(R), S:AllyFollowDone(R), S:AllyFollowInit(R), S:AllyFollowWait(R), S:AllyLead(R), S:AllyLeadBondCombat(R), S:AllyLeadHide(R), S:AllyLeadMissionWait(R), S:AllyLeadPlayerInWay(R), S:AllyLeadWait(R), S:BotCoverAim(R), S:BotCoverFire(R) ... |
| 0x06 | 0x0f2 | 2 | BOTSTATE_gotoGoal(R), Drone_SM_RouteMsgDCV(A) |
| 0x08 | 0x0f4 | 1 | Drone_SM_RouteMsgDCV(A) |
| 0x0a | 0x0f6 | 1 | Drone_SM_RouteMsgDCV(A) |
| 0x0c | 0x0f8 | 217 | BOTSTATE_processGoals(W), BOTSTATE_setStateChange(W), BOT_fellOutMap(W), DroneAnim_CoverAnim(W), DroneAnim_SetEndAIState(W), DroneFunc_ConsiderExplosive(W), DroneFunc_DeadDrone(W), DroneFunc_DoForcedAttack(W), DroneFunc_HandleImpact(W), DroneMove_AstronautCombat(W), Drone_SM_RouteMsgDCV(R), Drone_SM_SetState(W), FUN_0001d6a0(W), FUN_000354c0(W), FUN_000363d0(W), FUN_00038ff0(W), FUN_00045d30(W), MP_GoldenEyeUpdate(W), N2_BulletImpact(W), S:AbseilHang(W), S:AbseilInit(W), S:AbseilSlide(W), S:ActionAnim(W), S:AimBackoff(W), S:AimCrouch(W) ... |
| 0x10 | 0x0fc | 5 | FUN_0003a410(W), S:SmokedOut_Recover(R), S:StunDartRecover(R), S:StunGrenadeRecover(R), S:Stunned_Recover(R) |
| 0x14 | 0x100 | 1 | Drone_SM_RouteMsgDCV(A) |
| 0x18 | 0x104 | 217 | BOTSTATE_processGoals(W), BOTSTATE_setStateChange(W), BOT_fellOutMap(W), DroneAnim_CoverAnim(W), DroneAnim_SetEndAIState(W), DroneFunc_ConsiderExplosive(W), DroneFunc_DeadDrone(W), DroneFunc_DoForcedAttack(W), DroneFunc_HandleImpact(W), DroneMove_AstronautCombat(W), Drone_SM_RouteMsgDCV(RW), Drone_SM_SetState(W), FUN_0001d6a0(W), FUN_000354c0(W), FUN_000363d0(W), FUN_00038ff0(W), FUN_00045d30(W), MP_GoldenEyeUpdate(W), N2_BulletImpact(W), S:AbseilHang(W), S:AbseilInit(W), S:AbseilSlide(W), S:ActionAnim(W), S:AimBackoff(W), S:AimCrouch(W) ... |
| 0x1c | 0x108 | 42 | DroneFunc_DoForcedAttack(RW), S:AbseilHang(RW), S:AimCrouchFire(RW), S:AimStandFire(RW), S:Alert(RW), S:AlertToPosition(RW), S:Attack(RW), S:BotAttackStepAimLeft(RW), S:BotAttackStepAimRight(RW), S:BotAttackStrafeAimLeft(RW), S:BotAttackStrafeAimRight(RW), S:CastleChatGuard1(RW), S:Civilian(RW), S:CivilianDoorGuard(RW), S:CivilianGuard(RW), S:CivilianMission(RW), S:CivilianMissionWait(RW), S:CivilianPatrol(RW), S:CivilianScared(RW), S:Combat(RW), S:CombatNoRoute(RW), S:CombatNoSight(RW), S:CombatOutOfRange(RW), S:CombatWait(RW), S:EnemyMission(RW) ... |
| 0x20 | 0x10c | 3 | S:AllyLead(ARW), S:AllyLeadBondCombat(RW), S:AllyLeadWait(RW) |
| 0x24 | 0x110 | 217 | BOTSTATE_processGoals(W), BOTSTATE_setStateChange(W), BOT_fellOutMap(W), DroneAnim_CoverAnim(W), DroneAnim_SetEndAIState(W), DroneFunc_ConsiderExplosive(W), DroneFunc_DeadDrone(W), DroneFunc_DoForcedAttack(W), DroneFunc_HandleImpact(W), DroneMove_AstronautCombat(W), Drone_SM_SetState(W), Drone_maybeDisallowedFromSayingThisSfx(R), FUN_0001d6a0(W), FUN_000354c0(W), FUN_000363d0(W), FUN_00038ff0(W), FUN_00045d30(W), MP_GoldenEyeUpdate(W), N2_BulletImpact(W), S:AbseilHang(W), S:AbseilInit(W), S:AbseilSlide(W), S:ActionAnim(W), S:AimBackoff(W), S:AimCrouch(W) ... |
| 0x28 | 0x114 | 1 | Drone_SM_RouteMsgDCV(R) |
