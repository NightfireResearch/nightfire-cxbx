# Drone behaviour properties (the 91-entry bitfield)

Layout from the table at 0x1634e0 (2 bytes per property: word | shift<<3, then a mask index into 0x163598). Readers/setters: every direct call of behaviour_util_getProperty (0x19440) / setProperty (0x19470) with an immediate id (capstone scan, 482 call sites, all resolved). No code tests the behaviour words any other way (scanned for [reg+0x3d8..0x3ec] and via the +0x3d4 pointer). Level use: how many of the 555 drone placements in the 32 bundles set it in behaviour 1 / behaviour 2. Meaning: inferred from the readers; **(guess)** where the reader was only glanced at. "never read" = dead data on Xbox.

| id | word.bit (width) | meaning | level use b1/b2 | read by | set by |
|---|---|---|---|---|---|
| 0x00 | 0.0 (1) | move: crouch-walk allowed (with 0x02: anim state 0xf) | 155/168 | FUN_000354c0, FUN_000436b0 | NDrone2_DefaultInit |
| 0x01 | 0.1 (1) | move: anim state 8 allowed (only if 0x02 clear) | 59/63 | FUN_000354c0, FUN_00043700 | BOT_init, NDrone2_DefaultInit, NDrone2_init_DMODE_Defaults |
| 0x02 | 0.2 (1) | move: modifier for 0x00/0x01 | 214/204 | FUN_000354c0, FUN_000436b0, FUN_00043700 |  |
| 0x03 | 0.3 (1) | (no reader) | 59/63 | **never read** |  |
| 0x04 | 0.4 (1) | (no reader) | 1/1 | **never read** |  |
| 0x05 | 0.5 (1) | captain on difficulty 2 (Normal): upgraded skin, x DroneCaptain_Mod_* | 46/14 | NDrone2_DefaultInit |  |
| 0x06 | 0.6 (1) | combat: may back off (anim state 4, 1.8 m clear) | 190/195 | FUN_000354c0, NDrone2_CanBackoff | BOT_init, NDrone2_DefaultInit, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Defaults |
| 0x07 | 0.7 (1) | (no reader) | 441/440 | **never read** | BOT_init, NDrone2_init_DMODE_Defaults |
| 0x08 | 0.8 (1) | captain on difficulty 3 (Hard) | 94/29 | NDrone2_DefaultInit |  |
| 0x09 | 0.9 (1) | captain on difficulty 1 (Easy) | 28/10 | NDrone2_DefaultInit |  |
| 0x0a | 0.10 (1) | cover: lean (stand/crouch) | 212/211 | DroneAnim_CanStandLean, FUN_000338b0, FUN_00033a40 | NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Defaults |
| 0x0b | 0.11 (1) | cover: stand lean variant | 120/124 | DroneAnim_CanStandLean, FUN_000338b0, FUN_00033a40 |  |
| 0x0c | 0.12 (1) | cover: step out | 207/202 | DroneAnim_CanStandStepOut, FUN_00033990, FUN_00033a40 | NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Defaults |
| 0x0d | 0.13 (1) | cover: stand lean variant 2 | 148/148 | DroneAnim_CanStandLean, FUN_000338b0, FUN_00033a40 |  |
| 0x0e | 0.14 (1) | (no reader) | 195/192 | **never read** |  |
| 0x0f | 0.15 (1) | cover: may use crouch cover | 428/422 | FUN_000338b0, FUN_00033990, FUN_000442e0, NDrone2_FindAmbushCover | NDrone2_init_DMODE_Defaults |
| 0x10 | 0.16 (1) | cover: force crouch-only cover (cleared when shot out of UnderCover) | 54/48 | DroneAnim_CoverAnim, FUN_00033870, FUN_000338b0, FUN_00033990, FUN_0003fae0, FUN_0003fe30, NDrone2_DSTATE_UnderCoverIdle | FUN_00040d80, NDrone2_BulletImpact, NDrone2_DSTATE_AmbushWait, NDrone2_DSTATE_UnderCoverIdle |
| 0x11 | 0.17 (1) | combat: dodge/roll move (anim state 5, target >= 6 m) | 429/423 | FUN_00043650 | NDrone2_init_DMODE_Defaults |
| 0x12 | 0.18 (1) | (no reader) | 160/170 | **never read** |  |
| 0x13 | 0.19 (1) | combat: strafe-dodge (anim state 0x29) | 110/113 | FUN_000435b0, FUN_00045870, FUN_00045900, FUN_00045c30 | BOT_init, NDrone2_DefaultInit, NDrone2_init_DMODE_Defaults |
| 0x14 | 0.20 (1) | doors: open behaviour A | 388/374 | NDrone2_DSTATE_BotDoorOpen, NDrone2_DSTATE_OpenDoor | NDrone2_DefaultInit |
| 0x15 | 0.21 (1) | doors: open behaviour B | 0/0 | NDrone2_DSTATE_BotDoorOpen, NDrone2_DSTATE_OpenDoor | NDrone2_DefaultInit |
| 0x16 | 0.22 (1) | doors: open behaviour C | 152/158 | NDrone2_DSTATE_BotDoorOpen, NDrone2_DSTATE_OpenDoor | NDrone2_DefaultInit |
| 0x17 | 0.23 (1) | firing: keep firing at last-seen position when sight lost (>15 frames) | 447/440 | DroneWeap_DoFiring, NDrone2_SetOpponent | FUN_000408b0, NDrone2_DefaultInit, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_SniperAlert |
| 0x18 | 0.24 (1) | on sighting: go straight to Attack | 211/216 | FUN_0003ea80, NDrone2_DSTATE_Attack, NDrone2_ReactToOpponentSighted | BOT_init, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Defaults |
| 0x19 | 0.25 (1) | on sighting: choose a combat move | 122/126 | FUN_0003ea80, NDrone2_ReactToOpponentSighted |  |
| 0x1a | 0.26 (1) | on sighting: challenge/interrogate (Castle, Tower levels) | 23/8 | FUN_0003ea80, NDrone2_ReactToOpponentSighted | NDrone2_DoModeSettingsNEW |
| 0x1b | 0.27 (1) | on sighting: 50/50 attack or combat move | 97/104 | FUN_0003ea80, NDrone2_ReactToOpponentSighted |  |
| 0x1c | 0.28 (1) | stun grenade: resists (plays flinch anim instead of StunGrenadeImpact) | 159/131 | DroneFunc_HandleImpact |  |
| 0x1d | 0.29 (1) | on init: take nearest AI path of type 8 (NDrone2_AssignAIPath) | 484/445 | NDrone2_DSTATE_Alert, NDrone2_DSTATE_AllyLeadInit, NDrone2_DSTATE_CivilianInit, NDrone2_DSTATE_Idle, NDrone2_DSTATE_PartyGirlInit, NDrone2_DSTATE_TruckDriverInit | FUN_00040d10, FUN_00040dd0, FUN_00040e80, NDrone2_DSTATE_Alert, NDrone2_DSTATE_AllyLeadInit, NDrone2_DSTATE_CivilianInit, NDrone2_DSTATE_CivilianMission, NDrone2_DSTATE_EnemyMission, NDrone2_DSTATE_Idle, NDrone2_DSTATE_KikoMission, NDrone2_DSTATE_PartyGirlInit, NDrone2_DSTATE_TruckDriverInit, NDrone2_DSTATE_TruckDriverMission, NDrone2_DoModeSettingsNEW, NDrone2_init_DMODE_CastleChatGuard2, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_PartyGirlLooker |
| 0x1e | 0.30 (1) | (no reader) | 137/132 | **never read** |  |
| 0x1f | 0.31 (1) | hearing: reacts to sounds (Sound_Alertness, msg 0x14) | 501/469 | DroneFunc_HandleSoundAlerts, DroneVision_AlertSound, FUN_00038ff0 | BOT_init, FUN_0003fae0, FUN_0003fe30, FUN_000408b0, FUN_000409b0, FUN_00040c10, FUN_00040dd0, FUN_00040e80, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_HostageTied, NDrone2_init_DMODE_PartyGirlLooker, NDrone2_init_DMODE_SniperAlert |
| 0x20 | 1.0 (2) | 2-bit: initial alertness level -> {0, 0, 0.66, 1.0} | 555/555 | NDrone2_DoModeSettingsNEW | BOT_init |
| 0x21 | 1.2 (1) | HeardNoiseSuspect behaviour | 395/406 | NDrone2_DSTATE_HeardNoiseSuspect | FUN_00040da0, NDrone2_init_DMODE_Defaults |
| 0x22 | 1.3 (1) | (no reader) | 4/0 | **never read** | FUN_000409fc |
| 0x23 | 1.4 (1) | (no reader) | 36/52 | **never read** | FUN_00040980, FUN_00040d30 |
| 0x24 | 1.5 (1) | patrol: init to InitPatrol (with 0x25) | 459/402 | FUN_0003fe30, FUN_000403b0, NDrone2_DSTATE_InitPatrol | FUN_00040dd0, FUN_00040e80, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_HostageTied, NDrone2_init_DMODE_PartyGirlLooker |
| 0x25 | 1.6 (1) | patrol: follow patrol path | 366/387 | FUN_0003fe30, FUN_000403b0, FUN_00046c20, NDrone2_DSTATE_InitPatrol, NDrone2_DSTATE_Patrol | FUN_00040960 |
| 0x26 | 1.7 (1) | contagion: alerted by seeing a drone whose cause bit 8 (saw a dead body) is set; only read in ConsiderAlerted | 508/455 | DroneVision_ConsiderAlerted | BOT_init, FUN_000409fc, FUN_00040c10, FUN_00040dd0, FUN_00040e80, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_PartyGirlLooker |
| 0x27 | 1.8 (1) | contagion: alerted by drone-shot (msg 0x12 / cause bit 2) | 508/455 | DroneVision_ConsiderAlerted, FUN_00038ff0, FUN_00065160 | BOT_init, FUN_000409fc, FUN_00040c10, FUN_00040dd0, FUN_00040e80, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_PartyGirlLooker |
| 0x28 | 1.9 (1) | contagion: alerted by opponent-sighted alarm (msg 0x11 / cause bit 1) | 462/445 | DroneVision_ConsiderAlerted, FUN_00038ff0, FUN_00065120 | BOT_init, FUN_000409fc, FUN_00040dd0, FUN_00040e80, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_PartyGirlLooker |
| 0x29 | 1.10 (1) | contagion: alerted by dead body (msg 0x15 / cause bit 8) | 508/455 | DroneVision_ConsiderAlerted, FUN_00038ff0, FUN_000651e0 | FUN_000409fc, FUN_00040dd0, FUN_00040e80, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_PartyGirlLooker |
| 0x2a | 1.11 (1) | (no reader) | 191/196 | **never read** | NDrone2_init_DMODE_Defaults |
| 0x2b | 1.12 (1) | (no reader) | 196/191 | **never read** |  |
| 0x2c | 1.13 (1) | (no reader) | 204/191 | **never read** |  |
| 0x2d | 1.14 (1) | (no reader) | 142/125 | **never read** |  |
| 0x2e | 1.15 (1) | combat: roll (anim state check) | 190/195 | FUN_000354c0, FUN_00043600, FUN_00045b30, NDrone2_CanRollLeft, NDrone2_CanRollRight | NDrone2_init_DMODE_Defaults |
| 0x2f | 1.16 (1) | (no reader) | 386/386 | **never read** | NDrone2_init_DMODE_Defaults |
| 0x30 | 1.17 (3) | cover: uses cover at all (NDrone2_CoverAvailable) | 422/419 | NDrone2_CoverAvailable | NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Defaults |
| 0x31 | 1.20 (1) | death: broadcasts on death unless head-shot (DSTATE_Dead) | 540/493 | NDrone2_DSTATE_BotDead, NDrone2_DSTATE_Dead | BOT_init, FUN_000408b0, FUN_00040c10, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_SniperAlert |
| 0x32 | 1.21 (1) | when hurt: broadcast hurt alert 0x12 (DroneFunc_SendHurtMessage) | 540/493 | DroneFunc_SendHurtMessage, NDrone2_DSTATE_Taser, NDrone2_ExplosiveImpact | BOT_init, FUN_000408b0, FUN_000409b0, FUN_00040c10, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_SniperAlert |
| 0x33 | 1.22 (1) | on sighting: broadcast 0x11 alarm to others (20 m) | 538/467 | FUN_0003ea80, NDrone2_DSTATE_Civilian, NDrone2_DSTATE_CivilianDoorGuard, NDrone2_DSTATE_CivilianGuard, NDrone2_DSTATE_CivilianMission, NDrone2_DSTATE_CivilianMissionWait, NDrone2_DSTATE_CivilianPatrol, NDrone2_DSTATE_PartyGirl, NDrone2_ReactToOpponentSighted | BOT_init, FUN_000408b0, FUN_000409b0, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_SniperAlert |
| 0x34 | 1.23 (1) | explosives: reacts to grenades/smoke (with 0x36) | 317/270 | DroneFunc_ConsiderExplosive, DroneFunc_HandleExplosives, DroneFunc_HandleImpact, NDrone2_DefaultInit | NDrone2_DefaultInit |
| 0x35 | 1.24 (1) | (no reader) | 140/147 | **never read** | NDrone2_DefaultInit |
| 0x36 | 1.25 (1) | explosives: ignores smoke (with 0x34) | 99/105 | DroneFunc_ConsiderExplosive, DroneFunc_HandleExplosives, DroneFunc_HandleImpact, LinkCreep_Handler, NDrone2_DefaultInit | NDrone2_DefaultInit |
| 0x37 | 1.26 (1) | (no reader) | 551/505 | **never read** | NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Defaults |
| 0x38 | 1.27 (1) | (no reader) | 509/494 | **never read** | NDrone2_DoModeSettingsNEW, NDrone2_DoModeSettingsOLD |
| 0x39 | 1.28 (1) | (no reader) | 36/21 | **never read** | FUN_000408b0, NDrone2_init_DMODE_SniperAlert |
| 0x3a | 1.29 (1) | explosives: aware of explosives A | 375/393 | DroneFunc_ConsiderExplosive, DroneFunc_HandleExplosives, NDrone2_DefaultInit | NDrone2_DSTATE_PlayScript, NDrone2_DefaultInit |
| 0x3b | 1.30 (1) | explosives: aware B (then Attack) | 467/434 | DroneFunc_ConsiderExplosive, DroneFunc_HandleExplosives, NDrone2_DefaultInit | FUN_00040c10, NDrone2_DSTATE_PlayScript, NDrone2_DefaultInit, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults |
| 0x3c | 1.31 (1) | vision: can spot opponents at all | 549/504 | DroneVision_EnemyLookForOpponent, DroneVision_HaveOpponentSight, NDrone2_NearArmedBond, NDrone2_NearArmedPerson, NDrone2_ReactToOpponentSighted | BOT_init, FUN_000408b0, FUN_000409b0, FUN_00040c10, FUN_00040dd0, FUN_00040e80, NDrone2_DefaultInit, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_HostageTied, NDrone2_init_DMODE_MissionFailer, NDrone2_init_DMODE_PartyGirlLooker, NDrone2_init_DMODE_SniperAlert |
| 0x3d | 2.0 (1) | (no reader) | 191/195 | **never read** |  |
| 0x3e | 2.1 (1) | (no reader) | 212/196 | **never read** |  |
| 0x3f | 2.2 (1) | (no reader) | 191/188 | **never read** |  |
| 0x40 | 2.3 (1) | combat: no combat moves (stand and fire) | 62/68 | FUN_0003ae20, NDrone2_ChooseCombatMove, NDrone2_DSTATE_AimCrouchFire, NDrone2_DSTATE_AimStandFire, NDrone2_DSTATE_BotAttack, NDrone2_DSTATE_BotAttackCrouch, NDrone2_DSTATE_BotAttackFire, NDrone2_DSTATE_CombatNoMove, NDrone2_DSTATE_CombatNoRoute, NDrone2_DSTATE_HeardNoiseAlert, NDrone2_DSTATE_HeardNoiseSuspect | FUN_000408b0, FUN_000409fc, FUN_00040ae0, FUN_00040d60, FUN_00040da0, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_HostageTied, NDrone2_init_DMODE_SniperAlert |
| 0x41 | 2.4 (1) | combat: strafe | 190/188 | FUN_000354c0, FUN_00043560, FUN_00045bc0, NDrone2_CanStrafeLeft, NDrone2_CanStrafeRight | BOT_init, NDrone2_DefaultInit, NDrone2_init_DMODE_Defaults |
| 0x42 | 2.5 (1) | combat: side-step | 388/381 | FUN_000354c0, FUN_00043510, FUN_00045ca0, NDrone2_CanStepLeft, NDrone2_CanStepRight | BOT_init, NDrone2_DefaultInit, NDrone2_init_DMODE_Defaults |
| 0x43 | 2.6 (1) | may surrender (close, unarmed-ish, facing Bond) | 254/252 | NDrone2_CheckSurrender | FUN_000408b0, FUN_000409fc, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_SniperAlert |
| 0x44 | 2.7 (2) | 2-bit: cover distance cap (cover farther than combatRange*1.5 rejected when set) | 29/16 | FUN_00044310 | NDrone2_init_DMODE_Defaults |
| 0x45 | 2.9 (1) | (no reader) | 363/372 | **never read** |  |
| 0x46 | 2.10 (1) | idle: look-around/fidget timer (NDrone2_SetIdleTimeOut) | 524/466 | NDrone2_DSTATE_Alert, NDrone2_DSTATE_Idle, NDrone2_DSTATE_Patrol, NDrone2_DSTATE_SpaceDrake | FUN_00040dd0, FUN_00040e80, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_PartyGirlLooker |
| 0x47 | 2.11 (1) | idle: extra idle anim variant (4 instead of 3) | 38/7 | DroneAnim_SetStandIdleAnim | NDrone2_init_DMODE_PartyGirlLooker |
| 0x48 | 2.12 (1) | (no reader) | 380/381 | **never read** | NDrone2_init_DMODE_Defaults |
| 0x49 | 2.13 (1) | (no reader) | 69/15 | **never read** |  |
| 0x4a | 2.14 (1) | on sighting: throw grenade (DSTATE_GrenadeThrow) | 0/0 | FUN_0003ea80, NDrone2_ReactToOpponentSighted |  |
| 0x4b | 2.15 (1) | Castle Indoors 1: DTYPE 0x16 instead of 0x13 | 6/0 | FUN_0003fae0, FUN_0003fe30 |  |
| 0x4c | 2.16 (1) | head-track the opponent/player | 456/414 | FUN_0003a780, FUN_0003a850, NDrone2_HeadTrackObj | BOT_init, FUN_00040c10, NDrone2_DSTATE_HostageIdle, NDrone2_DefaultInit, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults |
| 0x4d | 2.17 (1) | (no reader) | 1/0 | **never read** | BOT_init, NDrone2_DoModeSettingsOLD |
| 0x4e | 2.18 (1) | hostage-tied/run: DTYPE 5, NDrone2_FindRunToPoint | 4/2 | FUN_0003fae0, FUN_0003fe30, NDrone2_FindRunToPoint | NDrone2_init_DMODE_HostageTied |
| 0x4f | 2.19 (1) | contagion: alerted by surrendered drone (msg 0x13 / cause bit 4) | 537/471 | DroneVision_ConsiderAlerted, FUN_00038ff0, FUN_000651a0 | FUN_00040c10, FUN_00040e80, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults |
| 0x50 | 2.20 (1) | NDrone2_NearArmedDrone check | 69/19 | NDrone2_NearArmedDrone | FUN_00040c10, FUN_00040e80, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian |
| 0x51 | 2.21 (1) | reacts to the player even when not suspicious (<0.66) | 531/454 | DroneVision_EnemyLookForOpponent, NDrone2_NearArmedBond, NDrone2_ReactToOpponentSighted | FUN_00040c10, FUN_00040dd0, FUN_00040e80, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_CivDoorGuard, NDrone2_init_DMODE_Civilian, NDrone2_init_DMODE_Defaults, NDrone2_init_DMODE_PartyGirlLooker |
| 0x52 | 2.22 (1) | first attack: mission-fail label 0x4000033 (PartyGirl: fail reason 0xb) | 38/7 | DroneFunc_FirstAttack, NDrone2_DSTATE_PartyGirl | NDrone2_DoModeSettingsOLD |
| 0x53 | 2.23 (1) | death: mission fail (0x4000034/33) | 77/49 | DroneFunc_OnInitDeath, NDrone2_DSTATE_HostageDead | FUN_00040c10, NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_Civilian |
| 0x54 | 2.24 (1) | death by player: mission fail | 84/53 | DroneFunc_OnInitDeath | FUN_0003fae0, FUN_0003fe30, NDrone2_DoModeSettingsOLD |
| 0x55 | 2.25 (1) | seen player: mission fail reason 10 | 0/0 | DroneVision_HaveOpponentSight | NDrone2_DoModeSettingsOLD, NDrone2_init_DMODE_MissionFailer |
| 0x56 | 2.26 (1) | run-to-point: flee to alarm (DTYPE 0xe) / EnemyRunToPoint | 0/30 | FUN_0003fae0, NDrone2_DSTATE_EnemyRunToPoint, NDrone2_FindRunToPoint | FUN_00040d30, NDrone2_DSTATE_DonePressAlarm, NDrone2_DoModeSettingsNEW |
| 0x57 | 2.27 (1) | on sighting: allowed while in EnemyRunToPoint/99 | 9/0 | DroneVision_EnemyLookForOpponent, NDrone2_ReactToOpponentSighted |  |
| 0x58 | 2.28 (1) | (no reader) | 0/0 | **never read** | FUN_00040980 |
| 0x59 | 2.29 (1) | at goal position: delete self (DSTATE_DeleteMe) | 0/2 | NDrone2_DSTATE_GoToGoalPosition |  |
| 0x5a | 2.30 (1) | (no reader) | 546/478 | **never read** | FUN_000408b0, NDrone2_init_DMODE_Defaults |
