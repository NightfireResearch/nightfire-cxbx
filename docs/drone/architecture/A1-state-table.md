# Appendix A1: NDrone2_StateFuncs (Xbox 0x00177ca0, 250 x 4-byte function pointers)

Generated (by a research script, not in the repository) from the XBE bytes (table), the PS2 table at 0x0029c120, and Ghidra decompiles.
Every entry is `bool fn(DCVars_tag *dcv, Drone_tag *drone, obj_tag *obj, MsgObject *msg)` (cdecl, called only by
NDrone2_ProcessStateMachine 0x4e180). "shared" = the Xbox linker folded identical bodies (COMDAT folding): the PS2 has
a separate function for each of these entries.

"msgs" = message types the handler tests (top-level cases of its switch on msg->msgType, plus == / != tests), from the
decompile; blank = the handler is written in a form the scraper does not follow (read it by hand). -1 = the handler
tests a range (msgType - k). "sets" = constant targets of Drone_SM_SetState inside the handler; "dyn" = non-constant
targets (a helper's return value, a Drone field); "anim end" = constant end-states passed to DroneAnim_CallAnim
(applied by DroneAnim_SetEndAIState when the animation finishes). Transitions made inside helpers are not listed.

| # | DSTATE (PS2 name) | Xbox fn | bytes | PS2 fn | shared | msgs | sets | dyn | anim end |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Global | 0x0004e950 | 273 | 0x0015bf10 |  | 0,1,10,29 | WaitSwitch, PlayScript, HostageKillerAttack, TruckDriverInit, BotInit | (DSTATE)param_4->extraData; pDVar3->field806_0x49e |  |
| 1 | WaitSwitch | 0x0004eaa0 | 248 | 0x0015c040 |  | 0,1,3,14 | PlayScript, TruckDriverInit | pDVar1->field806_0x49e; param_1->drone->field806_0x49e |  |
| 2 | Disabled | 0x0004ebc0 | 165 | 0x00172b20 |  |  | WaitSwitch, PlayScript, TruckDriverInit | (DSTATE)*(undefined4 *)(param_2 + 0x930) |  |
| 3 | PlayScript | 0x0004ec70 | 1083 | 0x0015c148 |  | 0,1,3,5,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | CastleChatGuard1 | (DSTATE)iVar3; param_1->drone->field806_0x49e | AimStandFire |
| 4 | Idle | 0x0004f100 | 733 | 0x0015c5f8 |  | 0,1,3,4,5,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Alert, EnemyMission, SpaceDrake | (DSTATE)iVar7 |  |
| 5 | Alert | 0x0004f440 | 588 | 0x0015c928 |  | 0,1,3,4,5,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | EnemyMission | (DSTATE)iVar2 |  |
| 6 | InitPatrol | 0x0004f6f0 | 152 | 0x00172bd0 |  | 0,1 | Idle, Patrol |  |  |
| 7 | Patrol | 0x0004f790 | 589 | 0x0015cb78 |  | 0,1,3,4,6,7,8,9,17,18,20,21,22,23,24,25,27,30,31,33 |  | (DSTATE)iVar10; DVar3 |  |
| 8 | HostageKiller | 0x0004fa40 | 534 | 0x0015ce08 |  | 0,1,3,5,6,7,8,9,10,17,18,20,21,22,23,24,25,26,30,31,33 | Idle, Attack | (DSTATE)iVar2 |  |
| 9 | HostageKillerAttack | 0x0004fcc0 | 402 | 0x0015d020 |  | 0,1,3,5,6,7,8,9,10,23,24,25,33 | Attack |  |  |
| 10 | Hostage | 0x0004fea0 | 267 | 0x0015d258 |  | 0,1,3,5,6,7,8,9,11,23,24,25,26,33 | HostageDie, HostageSaved |  |  |
| 11 | HostageDie | 0x0004b420 | 78 | 0x00172c80 |  | 0,1 |  |  | HostageDead |
| 12 | HostageSaved | 0x00050000 | 1 | 0x0015d3d0 |  | 0,1,6,7,8,9,23,24,25,33 | HostageIdle, HostageHide, HostageGoToGoalPosition |  |  |
| 13 | HostageIdle | 0x00050150 | 317 | 0x0015d540 |  | 0,1,3,6,7,8,9,23,24,25,33 | HostageHide |  |  |
| 14 | HostageHide | 0x000502e0 | 268 | 0x0015d6c0 |  | 0,1,3,6,7,8,9,23,24,25,33 | HostageIdle |  |  |
| 15 | HostageDead | 0x00050440 | 190 | 0x00172d08 |  | 0,1,3,12 | Fade |  |  |
| 16 | CivilianInit | 0x00050530 | 200 | 0x00172e10 |  | 0,1 | Civilian, CivilianPatrol, CivilianMission, PartyGirl |  |  |
| 17 | Civilian | 0x00050600 | 602 | 0x0015d7f8 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | CivilianScared, PartyGirl, TruckDriverIdle, Attack | (DSTATE)iVar4 |  |
| 18 | CivilianScared | 0x000508c0 | 288 | 0x0015da80 |  | 0,1,6,7,8,9,23,24,25,33 | CivilianHiding, HostageGoToGoalPosition | (DSTATE)iVar2 |  |
| 19 | CivilianHiding | 0x0004b470 | 205 | 0x0015dc00 |  | 0,1,6,7,8,9,23,24,25,33 |  |  |  |
| 20 | CivilianPatrol | 0x00050a20 | 548 | 0x0015dcf0 |  | 0,1,2,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Idle, Attack | (DSTATE)iVar3 |  |
| 21 | ReturnToPatrolPath | 0x00050ca0 | 60 | 0x00172ef0 |  | -1,0 | GoToGoalPosition |  |  |
| 22 | CivilianMission | 0x00050ce0 | 782 | 0x0015df40 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33,34 | Civilian, CivilianScared, CivilianMissionWait, KikoMission, Attack | (DSTATE)iVar9; DVar5 |  |
| 23 | CivilianMissionWait | 0x00051050 | 780 | 0x0015e290 |  | 0,1,3,6,7,8,9,12,17,18,20,21,22,23,24,25,30,31,33,34 | CivilianScared, CivilianMission, Attack | (DSTATE)iVar3 |  |
| 24 | StandBlind | 0x0004b590 | 45 | 0x00172f50 |  | 0,1 |  |  |  |
| 25 | KikoMission | 0x000513c0 | 396 | 0x0015e608 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | StandBlind, KikoMissionRun |  |  |
| 26 | KikoMissionRun | 0x000515b0 | 246 | 0x0015e7b8 |  | 0,1,3,6,7,8,9,23,24,25,33 | RunToAlarm |  |  |
| 27 | EnemyMission | 0x00051700 | 662 | 0x0015e8c8 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Idle, KikoMission | (DSTATE)iVar6; DVar4 |  |
| 28 | AllyLeadInit | 0x000519f0 | 369 | 0x0015ebd0 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyLeadWait |  |  |
| 29 | AllyLead | 0x00051bc0 | 665 | 0x0015ed78 |  | 0,1,3,6,7,8,9,23,24,25,27,33 | AllyLeadPlayerInWay, AllyLeadHide, AllyLeadWait, AllyLeadBondCombat, AllyLeadDone, AllyGoToGoalPosition |  |  |
| 30 | AllyLeadPlayerInWay | 0x00051ec0 | 332 | 0x0015f108 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyLead, AllyLeadHide, AllyGoToGoalPosition |  |  |
| 31 | AllyLeadHide | 0x00052050 | 318 | 0x0015f2e8 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | AllyLeadWait |  |  |
| 32 | AllyLeadWait | 0x000521e0 | 379 | 0x0015f488 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyLead, AllyLeadHide, AllyGoToGoalPosition |  |  |
| 33 | AllyLeadMissionWait | 0x000523a0 | 243 | 0x0015f6a8 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyLead |  |  |
| 34 | AllyLeadBondCombat | 0x000524e0 | 320 | 0x0015f800 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyLead, AllyLeadHide, AllyGoToGoalPosition |  |  |
| 35 | AllyLeadDone | 0x0004b5c0 | 124 | 0x0015f9c8 | with 39 | 0,1,6,7,8,9,23,24,25,33 |  |  |  |
| 36 | AllyFollowInit | 0x00052660 | 182 | 0x0015faa8 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyFollow |  |  |
| 37 | AllyFollow | 0x00052760 | 286 | 0x0015fbc0 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyFollowWait |  |  |
| 38 | AllyFollowWait | 0x000528c0 | 276 | 0x0015fd80 |  | 0,1,3,6,7,8,9,23,24,25,33 | AllyFollow |  |  |
| 39 | AllyFollowDone | 0x0004b5c0 | 124 | 0x0015ff38 | with 35 | 0,1,6,7,8,9,23,24,25,33 |  |  |  |
| 40 | AllyGoToGoalPosition | 0x00052a20 | 364 | 0x00160018 |  | 0,3,6,7,8,9,23,24,25,27,33 |  | param_1->drone->field806_0x49e |  |
| 41 | SniperIdle | 0x00052c00 | 317 | 0x00160190 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 |  | (DSTATE)iVar2 |  |
| 42 | SniperAim | 0x00052da0 | 305 | 0x001602f8 |  | 0,1,3,6,7,8,9,23,24,25,33 | SniperFire |  |  |
| 43 | SniperFire | 0x00052f20 | 427 | 0x00160468 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | SniperAim, SniperReload, AltAttack |  |  |
| 44 | SniperReload | 0x0004b680 | 359 | 0x00160660 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 |  |  | SniperFire |
| 45 | GrenadeThrow | 0x0004be80 | 195 | 0x00173b60 |  | 0,1,3,6,7,8,9,23 |  |  | Attack |
| 46 | PartyGirlInit | 0x00053110 | 128 | 0x00172fa0 |  | 0,1 | CivilianMission, PartyGirl |  |  |
| 47 | PartyGirl | 0x00053190 | 571 | 0x00160820 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Civilian, CivilianScared, CivilianPatrol | (DSTATE)iVar3 |  |
| 48 | CivilianGuard | 0x00053430 | 553 | 0x00160a88 |  | 0,1,2,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Attack | (DSTATE)iVar3 |  |
| 49 | CivilianDoorGuard | 0x000536c0 | 1220 | 0x00160d28 |  | 0,1,2,3,6,7,8,9,12,17,18,20,21,22,23,24,25,30,31,33 | Attack | (DSTATE)iVar3 |  |
| 50 | TruckDriverInit | 0x00053c00 | 431 | 0x001612b0 |  | 0,1,2,5,6,7,8,9,17,18,19,22,23,24,25,30,33 | TruckDriverInitAlert, TruckDriverIdle, TruckDriverMission |  |  |
| 51 | TruckDriverInitAlert | 0x00053e10 | 270 | 0x00161478 |  | 0,1,2,5,6,7,8,9,23,24,25,33 | CivilianScared |  |  |
| 52 | TruckDriverIdle | 0x00053f80 | 339 | 0x001615b0 |  | 0,1,3,6,7,8,9,17,18,21,22,23,24,25,30,31,33 | CivilianScared | (DSTATE)iVar2 |  |
| 53 | TruckDriverMission | 0x00054130 | 551 | 0x00161710 |  | 0,1,2,3,6,7,8,9,12,17,18,21,22,23,24,25,30,31,33 | TruckDriverIdle | (DSTATE)iVar5 |  |
| 54 | CastleChatGuard1 | 0x000543c0 | 545 | 0x001619c0 |  | 0,1,2,3,5,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 |  | (DSTATE)iVar3 | Idle |
| 55 | AmbushInit | 0x00054650 | 95 | 0x00173040 |  | 0,1 | AmbushWait, UnderCoverInit |  |  |
| 56 | AmbushWait | 0x000546b0 | 305 | 0x00161c40 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | Attack |  |  |
| 57 | InterogateAssist | 0x00054840 | 330 | 0x00161da0 |  |  | InterogateAssistWait, NoOpponent, GoToGoalPosition |  |  |
| 58 | InterogateAssistWait | 0x00054990 | 409 | 0x00161f40 |  | 0,1,2,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Attack, NoOpponent | (DSTATE)iVar2 |  |
| 59 | Interogator | 0x00054b80 | 40 | 0x001730d0 |  | -1,0 | Idle |  |  |
| 60 | Interogate | 0x00054bb0 | 1548 | 0x00162120 |  | 0,1,2,3,6,7,8,9,12,17,18,20,21,22,23,24,25,30,31,33 | Attack, NoOpponent | (DSTATE)iVar4 |  |
| 61 | InterogateWalk | 0x00055240 | 519 | 0x00162a18 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Interogate, Attack | (DSTATE)iVar3 |  |
| 62 | CivilianChallenge | 0x0004b840 | 322 | 0x00162c38 |  | 0,1,2,3,6,7,8,9,23,24,25,33 |  |  | RunToAlarm |
| 63 | Surrender_Anim | 0x000554a0 | 521 | 0x00162d80 |  | 0,1,3,6,7,8,9,12,25 | KnockedOut_Anim, Death_Anim, NoOpponent |  | Surrendered |
| 64 | Surrendered | 0x000556f0 | 249 | 0x00163030 |  | 0,1,3,6,7,8,9,25 | Unsurrender_Anim, KnockedOut_Anim, Death_Anim, NoOpponent |  |  |
| 65 | Unsurrender_Anim | 0x0004b9e0 | 239 | 0x00163150 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | Attack |
| 66 | KnockedOut_Anim | 0x00055830 | 223 | 0x00173110 |  | 0,1,2,3,8,9,10 | Death_Anim |  | Dead |
| 67 | Knocked_Out | 0x00055940 | 74 | 0x00173228 |  |  | Death_Anim |  |  |
| 68 | Death_Anim | 0x000567f0 | 526 | 0x00163ed8 |  | 0,1,2,3,10,12 | SpecialDeath_Anim, AbseilDeath, AstronautDeath |  |  |
| 69 | DeathByExplosion | 0x00056a30 | 368 | 0x00164160 |  | 0,1,2,3,10,12 | SpecialDeath_Anim |  |  |
| 70 | SpecialDeath_Anim | 0x00056bd0 | 773 | 0x00164348 |  | 0,1,3,5,10,12 | HostageDead, Death_Anim, Dead |  |  |
| 71 | Dead | 0x00056f00 | 434 | 0x001646c8 |  | 0,1,3,10,12,13 | Fade |  |  |
| 72 | Fade | 0x0004bb20 | 178 | 0x001733b8 |  | 0,1,3,10 |  |  |  |
| 73 | FadeFast | 0x0004bbf0 | 182 | 0x001734b8 |  |  |  |  |  |
| 74 | Taser | 0x000559b0 | 615 | 0x00163278 |  | 0,1,7,8,9,10,12 | Death_Anim, Stunned |  |  |
| 75 | Stunned | 0x00055c50 | 494 | 0x00163590 |  | 0,1,2,6,7,8,9,10,12,24,25 | KnockedOut_Anim, Death_Anim, Taser, Stunned_Recover |  |  |
| 76 | Stunned_Recover | 0x00055e90 | 239 | 0x00163798 |  | 0,1,7,8,9,10,25 | Death_Anim |  | Attack |
| 77 | StunGrenadeImpact | 0x00055fc0 | 211 | 0x00173290 |  | 0,1,7,8,9,10 | Death_Anim |  | StunGrenadeLoop |
| 78 | StunGrenadeLoop | 0x000560c0 | 397 | 0x001638a0 |  | 0,1,2,6,7,8,9,10,12,24,25 | KnockedOut_Anim, Death_Anim, StunGrenadeRecover |  |  |
| 79 | StunGrenadeRecover | 0x000562a0 | 237 | 0x00163a30 |  | 0,1,7,8,9,10,24,25 | Death_Anim |  | Attack |
| 80 | StunDartImpact | 0x000563d0 | 234 | 0x00163b10 |  | 0,1,8,9,10 | Death_Anim |  | StunDartLoop |
| 81 | StunDartLoop | 0x000564e0 | 397 | 0x00163c50 |  | 0,1,2,6,7,8,9,10,12,24,25 | KnockedOut_Anim, Death_Anim, StunDartRecover |  |  |
| 82 | StunDartRecover | 0x000566c0 | 239 | 0x00163de0 |  | 0,1,7,8,9,10,24,25 | Death_Anim, StunDartImpact |  | Attack |
| 83 | PunchImpact | 0x0004bcd0 | 162 | 0x001735c0 |  | 0,1,3,6,7,8,9,25 |  |  |  |
| 84 | ExplosiveImpact | 0x0004bdb0 | 155 | 0x00173690 |  | 0,1,3,6,7,8,9 |  |  |  |
| 85 | BulletImpact | 0x000570e0 | 160 | 0x00173778 |  | 0,1,3,6,7,8,9 | ExplosiveImpact |  |  |
| 86 | Attack | 0x000571b0 | 345 | 0x001648f0 |  | 0,1 | SniperAim, Combat, AimStand | (DSTATE)iVar7 |  |
| 87 | Alerted1stEncounter | 0x00057310 | 40 | 0x00173858 |  | -1,0 | CombatOutOfRange |  |  |
| 88 | Combat | 0x00057340 | 150 | 0x00173898 |  | 0,1,2,3 |  | (DSTATE)iVar1 |  |
| 89 | CombatNoMove | 0x000573f0 | 621 | 0x00164ac8 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 | Idle, SniperFire, NoOpponent, AimStandFire, CrouchCover, AltAttack |  |  |
| 90 | CombatOutOfRange | 0x000576b0 | 331 | 0x00164d70 |  | 0,1,2,3,6,7,8,9,23,24,25,27,33 | GoToGoalPosition | (DSTATE)iVar2 |  |
| 91 | CombatNewSighting | 0x00057840 | 137 | 0x00173960 |  | 0,1 | AimStandFire, AimCrouchFire |  |  |
| 92 | CombatNoSight | 0x000578d0 | 217 | 0x00164f18 |  | 0,1,2,3,6,7,8,9,23,24,25,33 |  | (DSTATE)iVar1 |  |
| 93 | CombatTooClose | 0x000579f0 | 61 | 0x00173a18 |  | -1,0 | AimStand |  |  |
| 94 | CombatWait | 0x00057a30 | 520 | 0x00165040 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 |  | (DSTATE)iVar1 |  |
| 95 | CombatNoRoute | 0x00057c80 | 891 | 0x001652e8 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 | Combat, GoToGoalPosition, AimStand, RunForCover | (DSTATE)iVar2 |  |
| 96 | NoOpponent | 0x00058040 | 297 | 0x00165780 |  | 0,1,3,12 | Attack |  |  |
| 97 | Obstructed | 0x00058190 | 182 | 0x00173a70 |  | 0,1,3,12 |  | param_1->drone->field805_0x49c |  |
| 98 | AlertToPosition | 0x00058270 | 846 | 0x00165950 |  | 0,1,3,6,7,8,9,12,17,18,20,21,22,23,24,25,27,30,31,33 | Attack, CombatNoRoute | (DSTATE)iVar2; param_1->drone->field805_0x49c; param_1->drone->field806_0x49e |  |
| 99 | GoToGoalPosition | 0x00058650 | 848 | 0x00165d20 |  | 0,1,2,3,6,7,8,9,23,24,25,27,33 | DeleteMe | (DSTATE)iVar10; param_1->drone->field806_0x49e |  |
| 100 | HostageGoToGoalPosition | 0x00058a10 | 341 | 0x00166148 |  | 0,3,6,7,8,9,23,24,25,27,33 |  | param_1->drone->field806_0x49e |  |
| 101 | SearchArea | 0x00058be0 | 740 | 0x001662d0 |  | 0,1,3,6,7,8,9,12,17,18,20,21,22,23,24,25,27,30,31,33,36,37 | Attack, GoToGoalPosition | (DSTATE)iVar4 |  |
| 102 | DrawWeapon | 0x00059040 | 305 | 0x001666e0 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | CivilianScared |  |  |
| 103 | AimStand | 0x000591d0 | 365 | 0x00166858 |  | 0,1,3,5,6,7,8,9,23,24,25,33 | CombatNoSight, DrawWeapon, AimStandFire, AltAttack |  |  |
| 104 | AimStandFire | 0x00059390 | 798 | 0x00166a08 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 | Idle, SniperFire, CombatNoMove, CombatNoSight, NoOpponent, AimBackoff, AltAttack | (DSTATE)iVar3 |  |
| 105 | AimStandReload | 0x0004bf80 | 307 | 0x00166db8 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | AimStandFire |
| 106 | AimStandDiscard | 0x0004c110 | 172 | 0x00166f20 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | DrawWeapon |
| 107 | Prone | 0x0004c200 | 205 | 0x00167020 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | ProneFire |
| 108 | ProneFire | 0x00059700 | 285 | 0x00167108 |  | 0,1,3,6,7,8,9,23,24,25,33 | Idle, NoOpponent |  |  |
| 109 | AimBackoff | 0x00059860 | 452 | 0x00167278 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | AimStand | DVar4 |  |
| 110 | CrouchCover | 0x00059a70 | 357 | 0x00167470 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | AimStand |  |  |
| 111 | AimCrouch | 0x00059c30 | 213 | 0x00167608 |  | 0,1,3,6,7,8,9,23,24,25,33 | CrouchCover |  | AimCrouchFire |
| 112 | AimCrouchFire | 0x00059d50 | 650 | 0x00167740 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | Idle, SniperFire, NoOpponent, AimBackoff, AimCrouchReload, AltAttack | (DSTATE)pSVar3->field21_0x1c |  |
| 113 | AimCrouchReload | 0x0004c320 | 247 | 0x00167a50 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | AimCrouchFire |
| 114 | AltAttack | 0x0004c470 | 239 | 0x00167b68 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  |  |
| 115 | StepAimLeft | 0x0005a020 | 242 | 0x00167c70 |  | 0,1,3,6,7,8,9,23,24,25,33 | AimStandFire |  | AimStandFire |
| 116 | StepAimRight | 0x0005a160 | 242 | 0x00167d80 |  | 0,1,3,6,7,8,9,23,24,25,33 | AimStandFire |  | AimStandFire |
| 117 | StrafeAimLeft | 0x0005a2a0 | 402 | 0x00167e90 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | AimStand |  |  |
| 118 | StrafeAimRight | 0x0005a480 | 402 | 0x00168060 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | AimStand |  |  |
| 119 | StrafeDodgeLeft | 0x0005a660 | 359 | 0x00168230 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | AimStand |  |  |
| 120 | StrafeDodgeRight | 0x0005a810 | 359 | 0x001683d0 |  | 0,1,3,6,7,8,9,12,23,24,25,33 | AimStand |  |  |
| 121 | RollLeftCrouch | 0x0004c5b0 | 213 | 0x00168570 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | AimCrouchFire |
| 122 | RollRightCrouch | 0x0004c6d0 | 213 | 0x00168660 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | AimCrouchFire |
| 123 | SmokedOut | 0x0004d640 | 174 | 0x00174330 |  | 0,1,3,6,7,8,9,25 |  |  | SmokedOut_Loop |
| 124 | SmokedOut_Loop | 0x0005dab0 | 278 | 0x0016bf58 |  | 0,1,3,6,7,8,9,12,23,25 | SmokedOut_Recover |  |  |
| 125 | SmokedOut_Recover | 0x0004d730 | 193 | 0x00174410 |  | 0,1,3,6,7,8,9,25 |  |  |  |
| 126 | Investigate | 0x0005a9c0 | 379 | 0x00168750 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | Idle | (DSTATE)iVar3; (DSTATE)pSVar2->field21_0x1c |  |
| 127 | DroneStuck | 0x00058f40 | 189 | 0x001665d0 |  | 0,1,3,6,7,8,9,23,24,25,33 | Attack, CombatNoRoute |  |  |
| 128 | HoldItRightThere | 0x0004c7f0 | 156 | 0x00173c50 |  |  |  |  |  |
| 129 | OpenDoor | 0x0005aba0 | 623 | 0x001688d8 |  |  |  | *(DSTATE *)&param_1->drone->field_0x4a2 |  |
| 130 | KickObject | 0x0005ae60 | 263 | 0x00168ba0 |  |  |  | *(DSTATE *)&param_1->drone->field_0x4a2 |  |
| 131 | ActionAnim | 0x0005afb0 | 317 | 0x00168d08 |  | 0,1,3,5,6,7,8,9,12,23,24,25,32,33 | AimCrouch | *(DSTATE *)&param_1->drone->field_0x4a2 |  |
| 132 | StandFiddle | 0x0005b140 | 235 | 0x00168ea0 |  | 0,1,3,6,7,8,9,12,23,24,25,33 |  | *(DSTATE *)&param_1->drone->field_0x4a2 |  |
| 133 | EnemyRunToPoint | 0x0005b270 | 341 | 0x00168ff0 |  |  | GoToGoalPosition, HostageGoToGoalPosition, RunToAlarm | param_1->drone->field806_0x49e |  |
| 134 | RunAwayFromObject | 0x0005b3d0 | 590 | 0x00169188 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | HideFromScaryObject, RecoverFromScaryObject | param_1->drone->field806_0x49e |  |
| 135 | HideFromScaryObject | 0x0005b6a0 | 358 | 0x001693f8 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | RecoverFromScaryObject |  |  |
| 136 | RecoverFromScaryObject | 0x0005b860 | 144 | 0x00173d38 |  | 0,1 | AllyLeadWait, Attack, GoToGoalPosition, RunToAlarm |  |  |
| 137 | RunToAlarm | 0x0005b8f0 | 158 | 0x00173dd8 |  |  | Idle, GoToGoalPosition |  |  |
| 138 | PressAlarm | 0x0004c890 | 510 | 0x00169588 |  | 0,1,3,6,7,8,9,12,23,24,25,33 |  |  | DonePressAlarm |
| 139 | DonePressAlarm | 0x0005b990 | 192 | 0x00173eb0 |  | 0,1 | CivilianScared, Attack, HostageGoToGoalPosition, JustStand |  |  |
| 140 | RunForCover | 0x0005ba50 | 769 | 0x00169830 |  | 0,1,2,3,6,7,8,9,23,24,25,27,33 | Attack, UnderCoverInit |  |  |
| 141 | ElevatorJumper | 0x0005bdd0 | 283 | 0x00169b68 |  | 0,1,3,12 | Attack |  |  |
| 142 | AbseilInit | 0x0005bf10 | 290 | 0x00169cf0 |  | 0,1,3,6,8,9,12 | AbseilSlide |  |  |
| 143 | AbseilSlide | 0x0005c070 | 453 | 0x00169e10 |  | 0,1,3,6,8,9 | AbseilHang |  | AimStand |
| 144 | AbseilHang | 0x0005c260 | 400 | 0x0016a020 |  | 0,1,3,6,8,9 | AbseilSlide |  |  |
| 145 | AbseilStepOff | 0x0004caf0 | 207 | 0x00173fc0 |  | 0,1,3,6,8,9 |  |  | AimStand |
| 146 | AbseilDeath | 0x0004cbf0 | 329 | 0x0016a208 |  | 0,1,2,3,10 |  |  |  |
| 147 | UnderCoverInit | 0x0004cd60 | 356 | 0x0016a3a0 |  | 0,1,2,3,6,7,8,9,23,24,25,33 |  |  |  |
| 148 | UnderCoverIdle | 0x0005c420 | 1028 | 0x0016a538 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 | Attack, RunForCover, UnderCoverAim, UnderCoverLeaveNow |  |  |
| 149 | UnderCoverAim | 0x0004cf20 | 261 | 0x0016a9b0 |  | 0,1,2,3,6,7,8,9,23,24,25,33 |  |  |  |
| 150 | UnderCoverFire | 0x0005c880 | 538 | 0x0016aac0 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 | UnderCoverSniperFire, UnderCoverReturn, UnderCoverLeaveNow |  |  |
| 151 | UnderCoverSniperFire | 0x0005cb00 | 685 | 0x0016acf0 |  | 0,1,2,3,6,7,8,9,12,13,23,24,25,33 | UnderCoverSniperReload, UnderCoverReturn, UnderCoverLeaveNow |  |  |
| 152 | UnderCoverSniperReload | 0x0004d080 | 248 | 0x0016af90 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | UnderCoverSniperFire |
| 153 | UnderCoverReturn | 0x0005ce10 | 357 | 0x0016b0a0 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | UnderCoverLeave |  |  |
| 154 | UnderCoverTypeChange | 0x0005cfd0 | 224 | 0x00174098 |  | 0,1,2,3 | RunForCover, UnderCoverIdle, UnderCoverLeaveNow |  |  |
| 155 | UnderCoverLeave | 0x0005d0c0 | 81 | 0x001741a0 |  |  | UnderCoverLeaveNow |  |  |
| 156 | UnderCoverLeaveNow | 0x0004d1d0 | 341 | 0x0016b218 |  | 0,1,2,3,6,7,8,9,23,24,25,33 |  |  | Attack |
| 157 | SeenOpponent | 0x0005d120 | 40 | 0x00174218 | with 160 | -1,0 | Attack |  |  |
| 158 | SeenDeadBody | 0x0004d380 | 236 | 0x0016b3b0 |  | 0,1,6,7,8,9,23,24,25,33 |  |  | AlertToPosition |
| 159 | SeenSurrenderedDrone | 0x0004d4c0 | 269 | 0x0016b4c0 |  | 0,1,6,7,8,9,23,24,25,33 |  |  | AlertToPosition |
| 160 | SeenDroneShot | 0x0005d120 | 40 | 0x00174258 | with 157 | -1,0 | Attack |  |  |
| 161 | SeenExplosive | 0x0004d620 | 25 | 0x00174298 |  |  |  |  |  |
| 162 | HeardNoise | 0x0005d150 | 60 | 0x001742c8 |  | -1,0 | HeardNoiseAware |  |  |
| 163 | HeardNoiseAware | 0x0005d190 | 418 | 0x0016b600 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33 | HeardNoiseSuspect | (DSTATE)iVar2 |  |
| 164 | HeardNoiseSuspect | 0x0005d390 | 918 | 0x0016b7c0 |  | 0,1,3,6,7,8,9,17,18,20,21,22,23,24,25,30,31,33,35 | Attack, AlertToPosition, HeardNoiseAlert | (DSTATE)iVar2 | AlertToPosition |
| 165 | HeardNoiseAlert | 0x0005d790 | 695 | 0x0016bc20 |  | 0,1,3,6,7,8,9,12,17,18,21,22,23,24,25,30,31,33,35 | Attack | (DSTATE)iVar4 | Attack |
| 166 | NinjaStand | 0x0005dc10 | 382 | 0x0016c070 |  | 0,1,3,6,8,9 | KnockedOut_Anim, Death_Anim, NinjaAttack |  |  |
| 167 | NinjaAttack | 0x0005ddc0 | 1065 | 0x0016c230 |  | 0,1,3,6,8,9 | KnockedOut_Anim, Death_Anim, NinjaStand, NinjaAttackLongRange, NinjaAttackMidRange, NinjaAttackShortRange, NinjaSword, NinjaBackflip, NinjaSideflip, NinjaStandFire |  |  |
| 168 | NinjaAttackLongRange | 0x0005e220 | 558 | 0x0016c6a0 |  | 0,1,3,6,8,9 | KnockedOut_Anim, Death_Anim, NinjaAttack, NinjaSideflip, NinjaStandFire, NinjaNoRoute |  |  |
| 169 | NinjaAttackMidRange | 0x0005e4a0 | 823 | 0x0016c928 |  | 0,1,3,6,8,9,12 | KnockedOut_Anim, Death_Anim, NinjaAttack, NinjaSomersault, NinjaSideflip, NinjaStandFire, NinjaNoRoute |  |  |
| 170 | NinjaAttackShortRange | 0x0005e830 | 1091 | 0x0016cd30 |  | 0,1,2,3,6,8,9,12 | KnockedOut_Anim, Death_Anim, NinjaAttack, NinjaGetCloseToPlayer, NinjaSword, NinjaBackflip, NinjaSideflip |  |  |
| 171 | NinjaGetCloseToPlayer | 0x0005ecb0 | 481 | 0x0016d250 |  | 0,1,3,12 | NinjaAttack, NinjaAttackShortRange, NinjaSword, NinjaSomersault |  |  |
| 172 | NinjaSword | 0x0005f2c0 | 333 | 0x0016d7e0 |  | 0,1,3,6,8,9 | KnockedOut_Anim, Death_Anim, NinjaAttack |  | NinjaAttack |
| 173 | NinjaSomersault | 0x0005f440 | 329 | 0x0016d978 |  | 0,1,3,6,8,9 | KnockedOut_Anim, Death_Anim |  | NinjaSword |
| 174 | NinjaBackflip | 0x0005f5c0 | 327 | 0x0016db10 |  | 0,1,3,6,8,9 | KnockedOut_Anim, Death_Anim |  |  |
| 175 | NinjaSideflip | 0x0005eee0 | 221 | 0x001744e8 |  | 0,1 | NinjaAttack, NinjaSideflipLeft, NinjaSideflipRight |  |  |
| 176 | NinjaSideflipLeft | 0x0005efc0 | 341 | 0x0016d4b0 |  | 0,1,2,3,6,8,9 | KnockedOut_Anim, Death_Anim |  |  |
| 177 | NinjaSideflipRight | 0x0005f140 | 342 | 0x0016d648 |  | 0,1,2,3,6,8,9 | KnockedOut_Anim, Death_Anim |  |  |
| 178 | NinjaStandFire | 0x0005f730 | 593 | 0x0016dc98 |  | 0,1,3,6,8,9,12 | KnockedOut_Anim, Death_Anim, NinjaAttack, NinjaSomersault, NinjaSideflip |  |  |
| 179 | NinjaNoRoute | 0x0005f9c0 | 587 | 0x0016df58 |  | 0,1,3,6,8,9,12 | KnockedOut_Anim, Death_Anim, NinjaAttack |  |  |
| 180 | DeleteMe | 0x0004d840 | 57 | 0x001745e0 |  | -1,0 |  |  |  |
| 181 | FailMission | 0x0005fc40 | 61 | 0x00174648 |  | -1,0 |  | *(DSTATE *)(*(int *)(param_1 + 4) + 0x49e) |  |
| 182 | JustStand | 0x0004d880 | 169 | 0x001746a0 |  | 0,1,3,5,8 |  |  |  |
| 183 | Tester1 | 0x0004d950 | 365 | 0x0016e270 |  | 0,1,3,12 |  |  |  |
| 184 | Tester2 | 0x0005fc80 | 76 | 0x00174830 |  |  | Tester1 |  |  |
| 185 | Tester3 | 0x0004daf0 | 155 | 0x001748a8 |  |  |  |  | Tester4 |
| 186 | Tester4 | 0x0005fcd0 | 149 | 0x001749b8 |  |  | Tester1 |  |  |
| 187 | HangUp | 0x0004db90 | 68 | 0x00174ab8 |  |  |  |  |  |
| 188 | WaitForever | 0x0004dbe0 | 20 | 0x00174b10 |  |  |  |  |  |
| 189 | AstronautLaunch | 0x0005fd70 | 342 | 0x0016e408 |  | 0,1,2,3,8,9 | AstronautCombatMove |  |  |
| 190 | AstronautHit | 0x00060030 | 214 | 0x00174d70 |  | 0,1,3,8,9,12 | AstronautCombat |  |  |
| 191 | AstronautDeath | 0x00060140 | 195 | 0x00174e98 |  | 0,1,3,12 | Fade |  |  |
| 192 | AstronautCombat | 0x0004dc00 | 199 | 0x00174b38 |  | 0,1,3,8,9 |  |  |  |
| 193 | AstronautCombatMove | 0x0005fef0 | 270 | 0x00174c30 |  | 0,1,3,8,9,12 | AstronautCombat |  |  |
| 194 | SpaceDrake | 0x00060230 | 302 | 0x0016e5c0 |  | 0,1,3,4,5,8 | DeleteMe |  |  |
| 195 | BotInit | 0x00060390 | 68 | 0x00174fa8 |  | 0,1 | BotIdle |  |  |
| 196 | BotRespawn | 0x0004dcf0 | 55 | 0x00175018 |  | -1,0 |  |  |  |
| 197 | BotGlobal | 0x000603e0 | 3852 | 0x0016e740 |  | 0,3,6,8,9,17,18,19,20,21,22,24,30,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69 | BotAttack, BotAttackBackoff, BotAttackReload, BotAttackChangeWeapon, BotAttackUnarmed, BotGotoGoalPosition, BotDeathAnim, BotImpactStunGrenade, BotIdle | (DSTATE)iVar20; DVar15 |  |
| 198 | BotCollector | 0x000613f0 | 120 | 0x00175060 |  | 0,1 | BotGotoGoalPosition, BotIdle |  |  |
| 199 | BotGuardian | 0x00061470 | 44 | 0x00175108 | with 200,201,202,203,204,205,206,211,233 | 0,1 | BotIdle |  |  |
| 200 | BotTeamPlayer | 0x00061470 | 44 | 0x00175150 | with 199,201,202,203,204,205,206,211,233 | 0,1 | BotIdle |  |  |
| 201 | BotBully | 0x00061470 | 44 | 0x00175198 | with 199,200,202,203,204,205,206,211,233 | 0,1 | BotIdle |  |  |
| 202 | BotBerserker | 0x00061470 | 44 | 0x001751e0 | with 199,200,201,203,204,205,206,211,233 | 0,1 | BotIdle |  |  |
| 203 | BotGreedy | 0x00061470 | 44 | 0x00175228 | with 199,200,201,202,204,205,206,211,233 | 0,1 | BotIdle |  |  |
| 204 | BotVengeful | 0x00061470 | 44 | 0x00175270 | with 199,200,201,202,203,205,206,211,233 | 0,1 | BotIdle |  |  |
| 205 | BotJudge | 0x00061470 | 44 | 0x001752b8 | with 199,200,201,202,203,204,206,211,233 | 0,1 | BotIdle |  |  |
| 206 | BotAssassin | 0x00061470 | 44 | 0x00175300 | with 199,200,201,202,203,204,205,211,233 | 0,1 | BotIdle |  |  |
| 207 | BotAttack | 0x000614a0 | 466 | 0x0016f878 |  |  | BotAttackRun, BotAttackNoRoute, BotAttackFire, BotAttackNoOpponent, BotDoorOpen | DVar3 |  |
| 208 | BotAttackRun | 0x00061690 | 504 | 0x0016fa70 |  |  | BotAttackNoRoute, BotAttackNoOpponent, BotAttackBackoff, BotCoverRunTo, BotDoorOpen | DVar8 |  |
| 209 | BotAttackNoRoute | 0x000618b0 | 52 | 0x00175348 |  | 0,1 | BotIdle |  |  |
| 210 | BotAttackFire | 0x000618f0 | 535 | 0x0016fcc8 |  |  | BotAttackRun, BotAttackNoRoute, BotAttackNoOpponent, BotAttackBackoff, BotDoorOpen | DVar6 |  |
| 211 | BotAttackNoOpponent | 0x00061470 | 44 | 0x001753b0 | with 199,200,201,202,203,204,205,206,233 | 0,1 | BotIdle |  |  |
| 212 | BotAttackStrafeAimLeft | 0x00061b30 | 325 | 0x0016ff30 |  | 0,1,3,12 | BotAttack, BotAttackRun, BotAttackNoOpponent | (DSTATE)pSVar1->field21_0x1c |  |
| 213 | BotAttackStrafeAimRight | 0x00061ca0 | 325 | 0x00170098 |  | 0,1,3,12 | BotAttack, BotAttackRun, BotAttackNoOpponent | (DSTATE)pSVar1->field21_0x1c |  |
| 214 | BotAttackRunChangePosition | 0x00061e10 | 243 | 0x00170200 |  | 0,1,3,12,27 | BotAttack |  |  |
| 215 | BotAttackBackoff | 0x00061f40 | 542 | 0x00170310 |  | 0,1,3,12 | BotAttack, BotAttackNoOpponent, BotAttackUnarmed | (DSTATE)uVar6; DVar5 |  |
| 216 | BotAttackCrouch | 0x00062190 | 732 | 0x00170560 |  | 0,1,3,28 | BotAttack, BotAttackRun, BotAttackNoRoute, BotAttackNoOpponent, BotAttackRunChangePosition, BotAttackBackoff, BotCoverRunTo | DVar8 |  |
| 217 | BotAttackRollLeftCrouch | 0x000624c0 | 133 | 0x001753f8 |  | 0,1,3,5 | BotAttackNoOpponent, BotAttackCrouch |  | BotAttackCrouch |
| 218 | BotAttackRollRightCrouch | 0x00062560 | 133 | 0x001754b0 |  | 0,1,3,5 | BotAttackNoOpponent, BotAttackCrouch |  | BotAttackCrouch |
| 219 | BotAttackStepAimLeft | 0x00062600 | 226 | 0x00175568 |  |  | BotAttackFire, BotAttackNoOpponent | (DSTATE)iVar1 | BotAttackFire |
| 220 | BotAttackStepAimRight | 0x000626f0 | 226 | 0x00175688 |  |  | BotAttackFire, BotAttackNoOpponent | (DSTATE)iVar1 | BotAttackFire |
| 221 | BotAttackReload | 0x000627e0 | 278 | 0x00170870 |  | 0,1 | BotAttack, BotAttackBackoff, BotAttackChangeWeapon |  | BotAttack, BotAttackBackoff |
| 222 | BotAttackChangeWeapon | 0x00062900 | 159 | 0x001757a8 |  | 0,1 |  | *(DSTATE *)(param_1[1] + 0x49e) |  |
| 223 | BotAttackUnarmed | 0x000629a0 | 124 | 0x00175868 |  | 0,1 | BotAttack, BotAttackBackoff |  |  |
| 224 | BotCoverRunTo | 0x00062a20 | 333 | 0x001709c0 |  | 0,1,2,3,27 | BotAttack, BotCoverInit |  |  |
| 225 | BotCoverInit | 0x0004dd30 | 249 | 0x00170b28 |  | 0,1,2,3,6,7,8,9,23,24,25,33 |  |  |  |
| 226 | BotCoverIdle | 0x00062bc0 | 727 | 0x00170c90 |  | 0,1,2,3,6,7,8,9,12,23,24,25,33 | BotAttack, BotCoverRunTo, BotCoverAim, BotCoverLeaveNow |  |  |
| 227 | BotCoverAim | 0x0004de70 | 149 | 0x00170fc8 |  | 0,1,2,3,6,7,8,9,23,24,25,33 |  |  |  |
| 228 | BotCoverFire | 0x00062ee0 | 329 | 0x001710b0 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | BotCoverReturn, BotCoverLeaveNow |  |  |
| 229 | BotCoverReturn | 0x00063070 | 268 | 0x00171270 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | BotCoverLeave |  |  |
| 230 | BotCoverTypeChange | 0x000631c0 | 239 | 0x001713e0 |  | 0,1,2,3 | BotCoverRunTo, BotCoverIdle, BotCoverLeaveNow |  |  |
| 231 | BotCoverLeave | 0x000632c0 | 213 | 0x00171500 |  | 0,1,2,3,6,7,8,9,23,24,25,33 | BotCoverLeaveNow |  |  |
| 232 | BotCoverLeaveNow | 0x0004df50 | 176 | 0x00171618 |  | 0,1,3,6,7,8,9,23,24,25,33 |  |  | BotAttack |
| 233 | BotStuck | 0x00061470 | 44 | 0x00175900 | with 199,200,201,202,203,204,205,206,211 | 0,1 | BotIdle |  |  |
| 234 | BotAlertToPosition | 0x000633e0 | 407 | 0x00171728 |  | 0,1,3,12,27 | BotAttack, BotAttackRun, BotAttackNoRoute, BotDoorOpen |  |  |
| 235 | BotGotoGoalPosition | 0x000635d0 | 368 | 0x00171920 |  |  | BotDoorOpen, BotIdle | (DSTATE)iVar4 |  |
| 236 | BotSeenOpponent | 0x00063760 | 44 | 0x00175948 | with 237 | 0,1 | BotAttack |  |  |
| 237 | BotSeenDroneShot | 0x00063760 | 44 | 0x00175990 | with 236 | 0,1 | BotAttack |  |  |
| 238 | BotHeardNoise | 0x0004e040 | 11 | 0x001759d8 |  |  |  |  |  |
| 239 | BotImpactBullet | 0x00063790 | 236 | 0x00171ad0 |  | 0,1,2,8,9,13 |  | DVar2 |  |
| 240 | BotImpactExplosive | 0x000638b0 | 82 | 0x001759e8 |  | 0,1,6,8 |  | DVar2 |  |
| 241 | BotImpactPunch | 0x00063930 | 93 | 0x00175a68 |  | 0,1,6,8,9 |  | DVar3 |  |
| 242 | BotDeathAnim | 0x0004e050 | 117 | 0x00175be8 |  |  |  |  |  |
| 243 | BotDeathByExplosion | 0x0004e0d0 | 165 | 0x00175c88 |  | -1,0 |  |  |  |
| 244 | BotDead | 0x00063a60 | 391 | 0x00171bf8 |  | 0,1,12,13 | BotRespawn |  |  |
| 245 | BotImpactStunGrenade | 0x000639c0 | 150 | 0x00175af8 |  |  | BotIdle |  |  |
| 246 | BotDoorOpen | 0x00063c10 | 424 | 0x00171db0 |  | 0,1,2,3,12,32 |  | *(DSTATE *)&param_1->drone->field_0x4a2 |  |
| 247 | BotGuardFriendIdle | 0x00063e00 | 187 | 0x00175d60 |  |  | BotGuardFriendFollow, BotIdle |  |  |
| 248 | BotGuardFriendFollow | 0x00063ec0 | 570 | 0x00171fc0 |  |  | BotGuardFriendIdle, BotIdle |  |  |
| 249 | BotIdle | 0x00064100 | 602 | 0x00172260 |  | 0,1,3,12 | BotAttack, BotGotoGoalPosition |  |  |
