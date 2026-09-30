# Appendix A2: DroneModeSettings and DroneTypeSettings

Read from the XBE (Xbox) and compared byte for byte with the PS2 ACTION.ELF tables of the same names
(PS2 DroneModeSettings 0x0029bb60, DroneTypeSettings 0x0029bd08): the data are identical, only the function
pointers differ. Names: PS2 symbols (init_DMODE_* / initDTYPE_* / ControlDTYPE_* functions, in table order).
DTYPE names are INFERRED from the mode that maps to them and the states they start in (the PS2 has no DTYPE
enum strings). * = code with no function defined in Ghidra (read with capstone).

## DroneModeSettings, Xbox 0x001776f8, 36 x 12 bytes (index = key5 "mode" when < 0x24)

Entry: +0 u8 dtype, +1 u8 side (Drone+0x44: 1 enemy, 2 ally, 3 civilian/neutral), +2 u16 pad (0), +4 float initial
alertness (Drone+0x3fc and +0x408), +8 init function (Drone_tag *) or NULL. Read only by NDrone2_DoModeSettingsOLD 0x414d0.

| # | DMODE (PS2) | dtype | side | alertness | init fn (Xbox) |
|---|---|---|---|---|---|
| 0 (0x0) | Normal | 0 Normal | 1 | 0.0 | 0x000408a0 init_DMODE_Normal (Ghidra: NDrone2_init_DMODE_Defaults, a thunk to 0x40600) |
| 1 (0x1) | Guard | 1 Guard | 1 | 0.0 | 0x000408a0 init_DMODE_Normal (Ghidra: NDrone2_init_DMODE_Defaults, a thunk to 0x40600) |
| 2 (0x2) | Retreater | 1 Guard | 1 | 1.0 | 0x000408a0 init_DMODE_Normal (Ghidra: NDrone2_init_DMODE_Defaults, a thunk to 0x40600) |
| 3 (0x3) | Sniper | 2 Sniper | 1 | 0.0 | 0x000408b0 NDrone2_init_DMODE_Sniper |
| 4 (0x4) | Stealth | 1 Guard | 1 | 0.0 | 0x00040960 NDrone2_init_DMODE_Stealth |
| 5 (0x5) | Attacker | 6 Attacker | 1 | 0.0 | 0x000408a0 init_DMODE_Normal (Ghidra: NDrone2_init_DMODE_Defaults, a thunk to 0x40600) |
| 6 (0x6) | RunToPoint | 7 RunToPoint | 1 | 1.0 | 0x00040980 NDrone2_init_DMODE_RunToPoint |
| 7 (0x7) | Assassin | 3 Assassin | 1 | 0.0 | 0x000408a0 init_DMODE_Normal (Ghidra: NDrone2_init_DMODE_Defaults, a thunk to 0x40600) |
| 8 (0x8) | HostageKiller | 4 HostageKiller | 1 | 0.0 | 0x000409b0 NDrone2_init_DMODE_HostageKiller |
| 9 (0x9) | Hostage | 5 Hostage | 3 | 0.0 | 0x00040a80 NDrone2_init_DMODE_Hostage/HostageTied (ICF) |
| 10 (0xa) | HostageTied | 5 Hostage | 3 | 0.0 | 0x00040a80 NDrone2_init_DMODE_Hostage/HostageTied (ICF) |
| 11 (0xb) | JustStand4Demo | 8 JustStand | 3 | 0.0 | 0x00040ae0 NDrone2_init_DMODE_JustStand4Demo |
| 12 (0xc) | DeleteMe | 21 DeleteMe | 1 | 0.0 | 0x000e0ec0 (empty stub; Ghidra: __profiling_or_debugging_hook_point) |
| 13 (0xd) | Civilian | 9 Civilian | 3 | 0.0 | 0x00040b00 NDrone2_init_DMODE_Civilian |
| 14 (0xe) | CivilianScared | 10 CivilianScared | 3 | 1.0 | 0x00040c10 NDrone2_init_DMODE_CivilianScared |
| 15 (0xf) | MissionFailer | 11 MissionFailer | 1 | 0.0 | 0x00040ce0 NDrone2_init_DMODE_MissionFailer |
| 16 (0x10) | Mayhew | 12 Mayhew | 2 | 0.0 | 0x00040d10 NDrone2_init_DMODE_Mayhew/TruckDriver (ICF) |
| 17 (0x11) | Ninja | 13 Ninja | 1 | 0.0 | 0x000e0ec0 (empty stub; Ghidra: __profiling_or_debugging_hook_point) |
| 18 (0x12) | AlarmRaiser | 14 AlarmRaiser | 1 | 1.0 | 0x00040d30 NDrone2_init_DMODE_AlarmRaiser |
| 19 (0x13) | SearchLight | 15 SearchLight | 1 | 0.0 | 0x00040d60 NDrone2_init_DMODE_SearchLight |
| 20 (0x14) | Ambush | 16 Ambush | 1 | 0.0 | 0x00040d80 NDrone2_init_DMODE_Ambush |
| 21 (0x15) | Zoe | 17 Zoe | 2 | 0.0 | 0x00040da0 NDrone2_init_DMODE_Zoe |
| 22 (0x16) | PartyGirl | 18 PartyGirl | 3 | 0.0 | 0x00040dd0 NDrone2_init_DMODE_PartyGirl |
| 23 (0x17) | CivilianGuard | 19 CivilianGuard | 3 | 0.0 | 0x00040e80 NDrone2_init_DMODE_CivilianGuard |
| 24 (0x18) | Interogator | 20 Interogator | 1 | 0.0 | 0x000408a0 init_DMODE_Normal (Ghidra: NDrone2_init_DMODE_Defaults, a thunk to 0x40600) |
| 25 (0x19) | CivDoorGuard | 22 CivDoorGuard | 3 | 0.0 | 0x00040f50 NDrone2_init_DMODE_CivDoorGuard |
| 26 (0x1a) | TruckDriver | 23 TruckDriver | 3 | 0.0 | 0x00040d10 NDrone2_init_DMODE_Mayhew/TruckDriver (ICF) |
| 27 (0x1b) | CastleChatGuard1 | 24 CastleChatGuard | 1 | 0.0 | 0x00041030 NDrone2_init_DMODE_CastleChatGuard1 |
| 28 (0x1c) | CastleChatGuard2 | 24 CastleChatGuard | 1 | 0.0 | 0x00041050 NDrone2_init_DMODE_CastleChatGuard2 |
| 29 (0x1d) | SniperAlert | 25 SniperAlert | 1 | 1.0 | 0x00041080 NDrone2_init_DMODE_SniperAlert |
| 30 (0x1e) | PartyGirlLooker | 18 PartyGirl | 3 | 0.0 | 0x00041120 NDrone2_init_DMODE_PartyGirlLooker |
| 31 (0x1f) | (31, no init) | 2 Sniper | 1 | 0.0 | NULL |
| 32 (0x20) | (32, no init) | 26 (26: from DMODE 32) | 3 | 0.0 | NULL |
| 33 (0x21) | Bot | 30 Bot | 2 | 0.0 | 0x000e0ec0 (empty stub; Ghidra: __profiling_or_debugging_hook_point) |
| 34 (0x22) | (34, no init) | 8 JustStand | 3 | 0.0 | NULL |
| 35 (0x23) | (35, all zero) | 0 Normal | 0 | 0.0 | NULL |

## DroneTypeSettings, Xbox 0x001778a0, 85 x 12 bytes (index = DTYPE, Drone+0xa9 / +0xa8 / +0xaa)

Entry: +0 s16 initial DSTATE (-1 = computed: see the main file), +2 s16 "second" DSTATE used when the drone
switches to this type through its second behaviour (Drone+0x4a0; 0 = use the active type's), +4 init function
(Drone_tag *) run by NDrone2_GetDroneTypeAttackTypeFriend / NDrone2_DoTypeSettingsOLD, +8 per-frame control
function (DCVars_tag *) called by Drone_Control instead of NDrone2_ControlSTANDARD when non-NULL.
Readers: 0x3fe30 (+0,+2,+4), 0x40430 (+0,+2,+4), Drone_Control 0x31530 (+8), NDrone2_ChangeToAttackMode (+4 of type 25).
Types 31-84 are one per bot state (initial state 196..249): bots are created as type 30 and the bot code picks
states itself, so these rows look unused (not verified).

| DTYPE | name (inferred) | initial | second | init fn | control fn |
|---|---|---|---|---|---|
| 0 | Normal | -1 | 0 |  |  |
| 1 | Guard | 6 InitPatrol | 0 |  |  |
| 2 | Sniper | 41 SniperIdle | 42 SniperAim | 0x000411f0 NDrone2_initDTYPE_Sniper |  |
| 3 | Assassin | -1 | 0 |  |  |
| 4 | HostageKiller | 8 HostageKiller | 0 |  |  |
| 5 | Hostage | 10 Hostage | 10 Hostage |  |  |
| 6 | Attacker | 86 Attack | 0 |  |  |
| 7 | RunToPoint | 133 EnemyRunToPoint | 133 EnemyRunToPoint |  |  |
| 8 | JustStand | 183 Tester1 | 183 Tester1 |  |  |
| 9 | Civilian | 16 CivilianInit | 18 CivilianScared |  |  |
| 10 | CivilianScared | 18 CivilianScared | 18 CivilianScared |  |  |
| 11 | MissionFailer | -1 | 181 FailMission |  |  |
| 12 | Mayhew | 28 AllyLeadInit | 28 AllyLeadInit |  |  |
| 13 | Ninja | 166 NinjaStand | 167 NinjaAttack |  | 0x0003f910 NDrone2_ControlDTYPE_Ninja* |
| 14 | AlarmRaiser | 137 RunToAlarm | 137 RunToAlarm |  |  |
| 15 | SearchLight | 0 | 0 |  |  |
| 16 | Ambush | 55 AmbushInit | 86 Attack |  |  |
| 17 | Zoe | 4 Idle | 0 |  | 0x0003f8a0 NDrone2_ControlDTYPE_Zoe* |
| 18 | PartyGirl | 46 PartyGirlInit | 0 |  |  |
| 19 | CivilianGuard | 48 CivilianGuard | 102 DrawWeapon |  |  |
| 20 | Interogator | -1 | 0 |  |  |
| 21 | DeleteMe | 180 DeleteMe | 180 DeleteMe |  |  |
| 22 | CivDoorGuard | 49 CivilianDoorGuard | 0 |  |  |
| 23 | TruckDriver | 50 TruckDriverInit | 18 CivilianScared |  |  |
| 24 | CastleChatGuard | 54 CastleChatGuard1 | 0 |  |  |
| 25 | SniperAlert | 42 SniperAim | 0 | 0x00041200 NDrone2_initDTYPE_SniperAlert |  |
| 26 | (26: from DMODE 32) | -1 | 0 |  |  |
| 27 | Abseil? (script 0x0600089c) | 142 AbseilInit | 0 |  |  |
| 28 | Abseil? (script 0x06000892) | 142 AbseilInit | 0 |  |  |
| 29 | Astronaut | 189 AstronautLaunch | 0 |  | 0x0003f930 NDrone2_ControlDTYPE_Astronaut* |
| 30 | Bot | 195 BotInit | 0 | 0x000e0ec0 (empty stub; Ghidra: __profiling_or_debugging_hook_point) |  |
| 31 |  | 196 BotRespawn | 0 |  |  |
| 32 |  | 197 BotGlobal | 0 |  |  |
| 33 |  | 198 BotCollector | 0 |  |  |
| 34 |  | 199 BotGuardian | 0 |  |  |
| 35 |  | 200 BotTeamPlayer | 0 |  |  |
| 36 |  | 201 BotBully | 0 |  |  |
| 37 |  | 202 BotBerserker | 0 |  |  |
| 38 |  | 203 BotGreedy | 0 |  |  |
| 39 |  | 204 BotVengeful | 0 |  |  |
| 40 |  | 205 BotJudge | 0 |  |  |
| 41 |  | 206 BotAssassin | 0 |  |  |
| 42 |  | 207 BotAttack | 0 |  |  |
| 43 |  | 208 BotAttackRun | 0 |  |  |
| 44 |  | 209 BotAttackNoRoute | 0 |  |  |
| 45 |  | 210 BotAttackFire | 0 |  |  |
| 46 |  | 211 BotAttackNoOpponent | 0 |  |  |
| 47 |  | 212 BotAttackStrafeAimLeft | 0 |  |  |
| 48 |  | 213 BotAttackStrafeAimRight | 0 |  |  |
| 49 |  | 214 BotAttackRunChangePosition | 0 |  |  |
| 50 |  | 215 BotAttackBackoff | 0 |  |  |
| 51 |  | 216 BotAttackCrouch | 0 |  |  |
| 52 |  | 217 BotAttackRollLeftCrouch | 0 |  |  |
| 53 |  | 218 BotAttackRollRightCrouch | 0 |  |  |
| 54 |  | 219 BotAttackStepAimLeft | 0 |  |  |
| 55 |  | 220 BotAttackStepAimRight | 0 |  |  |
| 56 |  | 221 BotAttackReload | 0 |  |  |
| 57 |  | 222 BotAttackChangeWeapon | 0 |  |  |
| 58 |  | 223 BotAttackUnarmed | 0 |  |  |
| 59 |  | 224 BotCoverRunTo | 0 |  |  |
| 60 |  | 225 BotCoverInit | 0 |  |  |
| 61 |  | 226 BotCoverIdle | 0 |  |  |
| 62 |  | 227 BotCoverAim | 0 |  |  |
| 63 |  | 228 BotCoverFire | 0 |  |  |
| 64 |  | 229 BotCoverReturn | 0 |  |  |
| 65 |  | 230 BotCoverTypeChange | 0 |  |  |
| 66 |  | 231 BotCoverLeave | 0 |  |  |
| 67 |  | 232 BotCoverLeaveNow | 0 |  |  |
| 68 |  | 233 BotStuck | 0 |  |  |
| 69 |  | 234 BotAlertToPosition | 0 |  |  |
| 70 |  | 235 BotGotoGoalPosition | 0 |  |  |
| 71 |  | 236 BotSeenOpponent | 0 |  |  |
| 72 |  | 237 BotSeenDroneShot | 0 |  |  |
| 73 |  | 238 BotHeardNoise | 0 |  |  |
| 74 |  | 239 BotImpactBullet | 0 |  |  |
| 75 |  | 240 BotImpactExplosive | 0 |  |  |
| 76 |  | 241 BotImpactPunch | 0 |  |  |
| 77 |  | 242 BotDeathAnim | 0 |  |  |
| 78 |  | 243 BotDeathByExplosion | 0 |  |  |
| 79 |  | 244 BotDead | 0 |  |  |
| 80 |  | 245 BotImpactStunGrenade | 0 |  |  |
| 81 |  | 246 BotDoorOpen | 0 |  |  |
| 82 |  | 247 BotGuardFriendIdle | 0 |  |  |
| 83 |  | 248 BotGuardFriendFollow | 0 |  |  |
| 84 |  | 249 BotIdle | 0 |  |  |
