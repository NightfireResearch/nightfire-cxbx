# Functions taken from nightfire-port

[nightfire-port](https://github.com/Jeffsteelflex/nightfire-port) (Jeffsteelflex) reimplements the PS2 build's
action engine, and its owner has given us permission to use it (16 October 2026, to be put in writing in its
next release). This file records what is taken from it.

Our authority is still `default.xbe`. Their code is a reading of the PS2's MIPS: PS2 struct layouts (`obj_tag`
0x110 bytes against the Xbox's 0xe4, `Drone_tag` 0xd20 against 0x978), PS2 maths (`PS2Sinf`, VU0, the EE's
division) and comments in Portuguese. So each function is ported from the Xbox's own code, with theirs as a guide
to what it does. A function ported that way:
- is written to docs/style-guide.md against our Xbox structures, with our tags (`AUTOINJECT`, `FUNC_AT`);
- carries the GameCube build's checks (docs/gamecube-checks.md);
- notes in this file what their reading got wrong, if anything, so it can go back to them.

## How a parcel is checked

1. `tools/demo/demo_coverage.py` lists which of the parcel's functions the time demos reach, before porting.
2. The functions are ported.
3. `tools/demo/play_demos.ps1` plays every demo. All must still match frame for frame.
4. A function the demos do not reach gets a shadow test or a replay of its own (`src/action/devtools`), or is
   listed below as unverified.

## Parcels

| # | Their files | Functions | Reached by the demos | Commit |
|---|---|---|---|---|
| 1 | script, script_support, script_player, simple_script, drone_script | 36 + 8 they inline or split differently | 31 of 36. Shadow-tested: Script_Set2Start, SP_Hit, and the three weight helpers (ScriptShadow.cpp, 650 cases). Checked against the disassembly only: Script_FreeFromHashTable. Unverified: Player_Cam2Mode, Drone_CoderCreate | see git log |

| 2 | trigger, door, sensor, player_stats | 45 + 6 (Trigger_Hit, PlrStats_GetDrivingData, Door_SetupSlide, Door_IsOpen, Door_MoveGroup, Door_CloseIfClear) | 40 of 45; 459 of 549 blocks. Every function, and the missed branches, shadow-tested (TriggerShadow, DoorShadow, SecurityShadow, PlrStatShadow: 44,050 cases, 0 differ; each suite shown to catch a planted bug) | see git log |

| 3 | drone_sm, drone_func, drone_behaviour | 32 + 8 (Drone_SM_QueueMsg, DroneAnim_CurrentMoveState, DroneAnim_SetWalkIdleAnim, NDrone2_GetHeadPosOld, NDrone2_PatrolTalk, NDrone2_PowerstationTalk, NDrone2_HandleTalking, NDrone2_FadeOut) | 30 of 32; 391 of 624 blocks. DroneAIShadow: 136,925 cases, 0 differ; 33 planted bugs, 31 caught (the other two need a NaN, or are a branch that cannot be reached) | see git log |

| 4 | ainetwork, aipath | 57 + 18 (AINetwork_AlterDestFor_DROUTE_Nearest, GetRouteDistanceBetweenNodes, OptimiseRoute, CalcRoute, SetupRouteToObject, SetupRouteToPosition, RouteNodeOffset, ResetRoute, VerifyRouteTarget, CalcRouteToObject, CalcRouteToPosition, LinkCreep_ForNodes, LinkCreep_Calc, LinkCreep_CalcToRouteEnd, both NDrone2_MoveTest, NDrone2_NavNodeCache, NDrone_InitDoorNodes) | 55 of 57; 786 of 895 blocks. NavShadow: 57,500 cases, 0 differ (10 planted bugs: 8 caught; one is in a branch that cannot change the result, one only shows in a count); AIHintsShadow: 33,491 cases, 0 differ (4 planted, all caught) | see git log |

| 5 | loadsave, menu_cheats, menu_codename | 47 names = 43 Xbox functions (their five `BIN_PullBits` are four; `P_PS2MEMCARDINIT_Handler`, `PS2_DoCardFlow` have no Xbox counterpart) + 3 (LS_FindBlockByLabel, LS_GetLoadStuff's adaptor, Menu_StartLS) | none: the demos neither save nor use the menus. SaveShadow: 23,640 cases, 0 differ; CodenameShadow: 8,480 cases, 0 differ; 8 planted bugs, all caught. MenuProbe A/B with the 51 functions as the original: codenames, secrets_test, front_end, nightfire and the new saveload (load a codename, save it, the delete box): identical screenshots, message logs and saves | see git log |

| 6 | drone_states, drone_damage, drone_weapon | 60 + 5 of their names under other Xbox names (Drone_BulletHit = 0x31f20, DroneWeap_ManageBurstCount = 0x67590, NDrone2_CanMoveToRelativePos = 0x45490, DroneWeap_PlaceTertiary = 0x67bf0, DroneWeap_CanThrowGrenade = 0x66c50) + 6 helpers (DroneWeap_AimAtOpponent 0x666a0, DroneWeap_FireDelay 0x66e90, DroneWeap_BurstLength 0x66f10, DroneWeap_NextBulletTimeNewBurst 0x67c00, DroneWeap_DropPathClear 0x67640, Drone_MayDealBackshotDamage 0x31da0) | 55 of 60; 800 of 1244 blocks. DroneStateShadow: 21,280 cases; DroneCivilianShadow: 56,000; DroneDamageShadow: 98,000; DroneWeaponShadow: 130,000; all 0 differ. 13 planted bugs, one per suite line, all caught | see git log |

| 7 | drone_vision, drone_move, drone_route, drone_spawner | 62 + 39 Xbox functions renamed from FUN_ (31 under their names, e.g. NDrone2_Steer = 0x43070, DroneVision_OpponentVisibility = 0x65510, NDrone2_FollowRoute = 0x46a80, DroneSpawner_DroneDelete = 0x308b0; 8 under ours, for helpers they inline, e.g. DroneVision_GlobalAlarm 0x650a0) + 15 FUNC_AT helpers (e.g. SolveQuadratic 0x42b20, NDrone2_Fall 0x3ef30, NDrone2_StartRouteCreep 0x46220). Of their 34 names without an Xbox match, NDrone2_DroneInSights is MSVC's shared `return false` (0xde5e0), and four are inline (below) | 59 of 62; 897 of 1119 blocks. DroneVisionShadow: 196,000 cases; DroneAlertShadow: 129,000; DroneOpponentShadow: 155,284; DroneMoveShadow: 152,000; DroneCollisionShadow: 128,000; DroneRouteShadow: 158,300; all 0 differ, with 296 OpponentVisibility and 290 NextRouteAngle results accepted 1-2 ulp apart (FSIN and the XBE's acos against the host's sin and acos). 25 planted bugs, one per suite line, all caught | see git log |

| 8 | drone_init, drone_anim, drone (the creation half, and from Drone_DCVfromOBJ on) | 44 + 39 Xbox functions theirs inline or name otherwise: 32 renamed from FUN_ in Ghidra (the mode initialisers, e.g. NDrone2_init_DMODE_Sniper = 0x408b0; NDrone2_GetDroneTypeAttackTypeFriend = 0x3fe30, NDrone2_DoTypeSettingsOLD = 0x40430, NDrone2_GetDTYPENEW = 0x3fae0; the anim helpers, e.g. DroneAnim_GetDAnimForAnimState = 0x33040, DroneAnim_PreTransCallback = 0x33320, DAnimFunc_Run = 0x35ec0), two already named (DroneAnim_SetDAnimNow 0x35dc0, DroneAnim_SetDAnimVeryInternal 0x32f30) and 5 FUNC_AT (Drone_UpdateCombatChannels 0x314a0, NDrone2_HandleDoor 0x3cbf0, and the three ControlDTYPE_ functions 0x3f8a0, 0x3f910, 0x3f930, created in Ghidra). Of their 49 names without an Xbox match, 12 are MSVC's folded copies or the shared empty function 0xe0ec0, and 7 are inline (below) | 34 of 44; 539 of 667 blocks. DroneModeShadow: 82,000 cases; DroneInitShadow: 66,000; DroneAnimShadow: 216,000; DroneCoreShadow: 108,000; all 0 differ. At least one planted bug per suite line, all caught (two plants that change nothing are not counted: a `<` for `<=` behind a `!= 0` test, and a `/ 9` that truncates as `/ 10` does) | see git log |

| 9 | drone_combat (the drone states and their helpers) | 213, in four packages: combat and aiming (NDrone2_Combat.cpp), cover, doors, snipers, alerts and searching (NDrone2_Cover.cpp, NDrone2_Alert.cpp), civilians, alarms, ninja, astronaut, impacts, stuns and surrender (NDrone2_Special.cpp, NDrone2_Reactions.cpp, NDrone2_Civilian.cpp, NDrone2_Death.cpp), scripted roles: truck drivers, abseiling, scary objects, interrogation, allies, hostages, ambush, testers (NDrone2_Roles.cpp, NDrone2_Hostage.cpp). Their NDrone2_DSTATE_AllyLeadDone is AllyFollowDone (0x4b5c0) in the Xbox state table; NDrone2_HostageSituationActive is the shared empty function 0xe0ec0 | 106 of 213; 1,195 of 3,557 blocks (five sessions now end early: tests/demos/action/README.md). DroneCombatShadow: 230,000 cases; DroneCoverShadow: 317,000; DroneReactShadow: 262,000; DroneRoleShadow: 128,000; all 0 differ. Under the shadow run the originals reach 877 of 878 of the roles' blocks and all but 7 of the 1,710 of cover, alerts and reactions (the rest unreachable with the shipped tables, or rare). One planted bug per package, all caught | see git log |

| 10 | player, player_traverse, player_weapon | 76 + helpers they inline: Player.cpp (the player: init, update, health, pain and death, substates, jumping, zero G, scan mode, aiming, collision, using things), PlayerTraverse.cpp (ladders, the grapple, wires, ziplines, creep walls), PlayerWeapon.cpp (ammo, choosing and changing weapons, the gun in the view, firing, zoom, auto-aim, the laser sight, the muzzle flash, the watch), the grapple, wire, creep-wall, icon and camera object makers, Camera_Shear, camera_tracking, MP_RegisterBulletHit. FUNC_AT helpers: PlayerAnimSetInitDead 0xa96a0, Player_UpdateWalkClass 0xa9160, AccelFunc0Flag 0xb7520, Player_LeaveLadder 0xaeac0, Player_HangByHandA/B 0xafa80/0xafaf0, Player_WeaponUsable 0xb9f70, Player_DroneAwake 0xb7bd0. Their Player_FeetOnPoint is PlayerNotDrone_FeetOnPoint 0xab450, Player_CollLadder is PlayerCollLadder 0xae930, Draw_LaserBeam is Player_SetupLaser 0xb8260 | 67 of 76; 1,202 of 1,895 blocks. PlayerShadow: 153,000 cases; PlayerWeaponShadow: 180,000; all 0 differ; a planted bug caught in each of their eleven groups. Under the shadow run the originals reach about 1,072 of 1,104 of the player's blocks and 818 of 836 of the weapons' | see git log |

| 11 | anim | 54, all in src/action/engine/Anim.cpp but the player's five anim-set functions (Player.cpp): loading (skins, sequences, scripts, the sleeves), script lists and scripts, script ticks and commands, sequence frames and poses, faces and datums, the bone matrices, head tracking, aiming and drawing, the anim object, anim sets. Of their 9 names without an Xbox function of that name, AnimSeqGetInfo is 0x14420, AnimSeqSet 0x17960, AnimSet_WalkUpdater 0x171a0 and AnimDistanceTableGet 0x174a0 (renamed from FUN_ in Ghidra, the first three ported); AnimDistanceTableCreate is 0x16950 (renamed, left to the original); AnimDistanceTableInit, AnimTagGetUnique and AnimSeqDelete are inline; MP_NeedSkin is not called. Static helpers renamed from FUN_: AnimFramesPassed 0x12a20, AnimFrameEmitChannels 0x13c70, AnimData_ReadHalf 0x13880. 21 AUTOLTCG adaptors for the register-argument originals | 52 of 54; 1,240 of 1,552 blocks (AnimSleeveGetEntity and AnimObjectInitialPose not reached). AnimPortShadow: 73,500 cases in eight groups, all 0 differ; one planted bug per group, all caught. Under the shadow run the originals reach 1,601 of the 1,651 blocks of the 54 and their helpers | see git log |

Parcel 1's first playback found two things the coverage alone would not have shown:
- **A crash in our code.** The port of `Script_CreateEntity` called our `Break_Create` by name, which is a known-broken
  draft that was never patched in. `tools/draft_calls.py` now finds such calls.
- **A gap in the demos themselves:** the collision stamp (see docs/action-timedemo.md).

## Where their reading differs from the Xbox

### Parcel 1
- `Script_SoundEnd`, `Script_EntityEnd`, `Script_SubScriptEnd`, `Script_LightEnd`, `Script_TextStart`,
  `Script_AnimEnd`, `Script_CameraEnd`, `Script_SpriteEnd` and `Script_FadeStart` are separate functions there.
  On the Xbox they are inline in `Script_Run`.
- `Script_FreeFromHashTable` skips freeing the -1 a wrong-version script leaves. The Xbox frees it (an original
  bug, kept).
- `SS_Activate` takes two arguments there; the Xbox's reads one.
- `Script_Interp`'s footstep test reads the AnimObj's flags there. The Xbox reads `AnimState`+0x50
  (`animFlags & 4`).
- Platform differences, not errors: `Script_SpriteStart` rescales its coordinates to 512x448 on the PS2, where the
  Xbox stores them raw; the PS2's camera block is 0x50 bytes, the Xbox's 0x48.

Everything else in their readings of parcel 1 matched the Xbox.

### Parcel 2
- `Trigger_Hit`, bullets: the field at the weapon definition's +2 is `weaponBaseNum`, not the ammo type.
- `InverterTrigger_Update` indexes the channels with the sign-extended short. Theirs masks it to 0-255.
- `SSys_Msg`:
  - Their comment says only laser 2 speaks in TowerC. The Xbox, and their own code, silence every item.
  - The four message tables are not in item order in memory: items 1-4 are at 0x18cc50, 0x18cc18, 0x18ccc0 and
    0x18cc88.
  - The Xbox has no guard against a message index of 7 or more.
- `Monitor_Create` takes 4 arguments, not 5.
- The door group state array has 101 entries (a memset of 0xca bytes), not 104.
- Their own additions: guards on a NULL user and viewer, a rows check in `PlrStats_ResetScoring`, and the channel
  clamps.

Everything else in their readings of parcel 2 matched the Xbox.

### Parcel 3
- Inlined on the Xbox: `DroneFunc_DropGrenade`.
- A call on the Xbox where theirs is inline: the "drone active" test in `DroneFunc_DoForcedAttack` and
  `NDrone2_PowerstationTalk` calls `MPDrone_MaybeIsDyingOrDead` (0x1a190); `Drone_MayhewTalk` inlines it.
- `DroneFunc_OnInitDeath`'s death log is a call to `NDrone2_LogDroneDeath` for every enemy whose flags lack
  0x1000000. Theirs tests "killed by the player" around `PlrStat_LogEnemyDispatched`.
- `NDrone2_NearArmedPerson` takes only the DCVars on the Xbox; theirs also takes a radius.
- `NDrone2_ExplosionDir` reads the impact position at +0x8, not +0x10 (layout).
- `DroneFunc_ConsiderExplosive`'s fuse timeout is ((2 x rate + fuse) + frame) on the x87; theirs adds frame + fuse +
  2 x rate in float.
- `DroneFunc_DoBondTalk` and `NDrone2_BondTalk` work their timeouts out in extended precision and truncate with
  `__ftol2`. Theirs uses float rounding and `fptoui`: PS2 maths.
- Their own additions: a drop on a full queue in `Drone_SM_RouteMsg`, the `ch < 256` guard in
  `NDrone2_PowerstationTalk`, and a NULL-player guard in `DroneAnim_LocationDeathAnim`.

Everything else in their readings of parcel 3 matched the Xbox.

### Parcel 4
- Separate functions on the Xbox that theirs inline: the route-to-position check (0x49c50, ours
  `AINetwork_VerifyRouteToPosition`), the creep's segment set-up (0x4a990, `LinkCreep_Setup`), the link lookup
  (0x47b40, `AINetwork_FindLink`), and five helpers ours keep static because only ported code calls them: the
  skip test of `AINetwork_OptimiseRoute` (0x47ff0), the waypoint copy (0x49cd0), the open and closed list helpers
  (0x48310, 0x48360), one path's part of `SetLinksFlagInCircle` (0x4a420) and the creep step's corner check (0x4a3c0).
  The other way round, `AIPath_Link2Cel` is inline in `AIPath_BindNodes` on the Xbox.
- `NDrone2_MoveTest` hands `AINetwork_BoundsTest` a "blocked" flag in both builds, but the Xbox `BoundsTest` takes six
  arguments and never writes it, so the flag never fails a move. Likewise `LinkCreep_Increment`'s blocked flag is
  cleared and never set, and `AINetwork_BoundsNodeTest` reads five arguments; theirs passes the flag on.
- `AINetwork_EmitPath` returns 0 or 1 in the whole of EAX, and `DroneMove_FindSafetyFromScaryPosition` tests all of it: a
  `bool` port (AL only) desynchronised the CastleCourtyard demo in CastleIndoors2. tools/narrow_returns.py did not
  flag it.
- `AINetwork_CalcRoute` passes an argument on to `AINetwork_DoAStarPath`, which on the Xbox reads only four.
- `LinkCreep_AtSpecialNode`: the Xbox counts the mover as arrived within 1 m when *someone* stands within 0.4 m of the
  node (`NDrone2_DroneNearPos` true). Theirs reads the same; our README said the opposite (fixed).
- Layout, not errors: the feet height is `AnimObj.rootHeight` (AnimState+0xac; theirs +0xcc), the creep's bounds
  switch is Drone_tag+0x82c and the blocked mark +0x290 (theirs +0xba8 and +0x31c), the multiplayer test in the
  route checks is `MPSettings.maybeDroneAIEnabled` (+0x1e4; theirs +0x184).
- Their own additions: the guard against link 0xffff in `UpdateLinksUsedCount`, the guard on index -1 in
  `LinkCreep_AtSpecialNode`, and the NULL-DCVars path of `LinkCreep_Handler`.
- From the parcel's AI hints (Drone_AIHints.cpp): `Drone_AIVolume_Create` creates a 0xf4-byte object (theirs 0x120)
  and has no early return when the cover table is missing; its half-size abs keeps -0 and negates a NaN (compare and
  FCHS); the kick object's script is read at SCRIPTINFO+0x90 (theirs +0x98); the drone's target slot is +0x14c
  (theirs +0x174); the cover and AI-point creators have no guard when the table is full; the low-cover anim table
  has five entries on the stack, unchecked (theirs eight, with a guard).

Everything else in their readings of parcel 4 matched the Xbox.

### Parcel 5
- `BIN_PullBits`: their five typed overloads are four functions on the Xbox. The core gathers the bits into a
  64-bit value (each bit sign-extended from 32, so a set bit 31 fills the high word, which it writes too), and
  `BIN_PullBits_U32` / `_U8` / `_U16` call it and store the low 4/1/2 bytes. Their `int32_t` and `uint32_t`
  overloads are `_U32`, their `int16_t` and `uint16_t` ones `_U16`.
- `LS_MakeMission` is `FUN_0006f110` (renamed in Ghidra). `GSET` differs by platform: the Xbox block has no screen
  position and 7 reserved words (theirs 0xdb), and its load drops the language, sound mode and widescreen rather
  than applying them. `LS_LoadBonus` calls `Menu_SetBonus(lo, hi, slot, 0)`.
- Their additions, not in the Xbox: the zero-length and slot-size guards of the block walk, the slot bound in
  `LS_GetSaveStuff` and `LS_LoadFromBuffer` (the Xbox copies `Boot_LoadPTPData`'s 0x12c0 bytes into a 0x1000-byte
  slot), the level-count clamp in `LS_LoadMission`, the player and table guards in the cheat checkboxes and keypad,
  the slot guard in `Menu_MapDefaultCodename`.
- `Menu_IsValidCodename` is the Xbox's 0x76330 (we had named it `Menu_ValidateCodename`; renamed), and
  `Menu_SelectCodenameInControl` is `FUN_0007edf0` (renamed). The set-up of a load or save that the wheels call,
  *Menu_StartLS* (`FUN_0007fef0`, invented name), takes the codename in EAX, the operation in ECX and a stale flag
  in DL; theirs inlines it.
- The memory card has no Xbox counterpart: `P_PS2MEMCARDINIT_Handler` and `PS2_DoCardFlow` are PS2 only;
  `XBox_DoSaveFlow` (not ported) is the Xbox's box flow. `C_LBERROPTIONS_Handler` is that flow's error list on the
  Xbox (the page under the box is told, the box closed, the return page gone to, the states flushed; B can leave for
  the dashboard), not the PS2 card's answers.
- `Menu_UpdateMessageBox`'s delete: `psiDeleteData` is the empty debug hook on the Xbox, and the deletion itself is
  done by `P_CNSELECT`'s answer to the confirmation box (`FUN_000dfec0`). `C_SBCNSELECT`'s X asks with that box
  rather than deleting, and its Select and Y check the free space (`FUN_0008fcc0`) before a new codename;
  `P_CNSELECT`'s update answers the boxes (delete, delete failed, the dashboard for space). None of that is in
  theirs.
- `Menu_EnumSaves` takes `SaveEnum_EntriesPerFrame` (0x194804) names a frame, and `psiGetNextSave`'s -1 means "not
  yet": theirs takes up to 0x100.

Everything else in their readings of parcel 5 matched the Xbox.

### Parcel 6
- The 25 states: their readings match the Xbox, side effects in order. Two things their shared helpers hide.
  AllyLeadInit has no alert or forced-attack cases. Patrol handles message 0x1b (the obstructed path) but not 5 (a
  script's end), and Idle and Alert the other way round. Their JustStand uses `sin(t + pi/2)` where the Xbox uses
  FCOS: the same.
- Not their reading, but ours: every Xbox call to `NDrone2_NearArmedPerson` pushes a radius it never reads.
- `Drone_BulletHit` is FUN_00031f20 on the Xbox, and calls `Drone_Message(obj, 8, hit, 0)`. Theirs builds DCVars and
  sends the message itself.
- `DroneFunc_HandleImpact`'s taser test truncates the health with `__ftol2` (signed, the low dword; NaN gives 0).
  Theirs converts it to unsigned.
- `NDrone2_PainTalk` asks `Drone_maybeDisallowedFromSayingThisSfx` (0x3be21). Theirs compares with the sound
  playing.
- `DroneAnim_LocationImpactAnim`'s abseil variant table has 19 entries, built on the stack. Theirs has 18.
- `DroneFunc_SetMissionFailReason` is inline in the impacts on the Xbox. Their reasons 5, 6 and 7 give the same
  label and channel, so ours calls our port with them.
- Separate functions on the Xbox:
  - helpers called only from the damage code: 0x36350 (the player's touch), 0x363d0 (a hurt volume's damage),
    0x36430 (`NDrone2_BodyHitEffect`), 0x36190 (whether the impact anim is due), 0x3a410 (TowerA/B calming on
    impact), 0x3a3f0 (whether a state may be resumed). The explosion's damage block 0x3db50 is
    `NDrone2_HitDamage` at location 0, handed `hitDamageScale`, which it never reads.
  - `NDrone2_CanMoveToRelativePos` is two functions: 0x45490 checks flag 0x10 and calls 0x453e0. 0x453e0 does the
    feet, matrix and move test, and scales a bot's offset by its speed unless its fourth argument is set (this
    caller sets it).
  - `DroneWeap_PlaceTertiary` (0x67bf0) is a lone jump to `DroneWeap_ThrowGrenade`. Ghidra gives both the one name.
  - The back-shot test is `Drone_MayDealBackshotDamage` (0x31da0, the drone in ECX). It lists the PowerStation
    levels outright, which comes to the same as theirs.
  - `DroneWeap_DropWeapon`'s line-of-sight check is 0x67640 (ray flags 0x125).
  - 0x67c00 is MSVC's copy of `DroneWeap_NextBulletTime` for a new burst. `DroneWeap_DoFiring` calls it.
- The same result by another route:
  - `DroneWeap_HandleFiring`'s bots check is inside 0x666a0.
  - Their `DroneWeap_ManageBurstCount` calls `NextBulletTime(0, 0)`. The Xbox (0x67590, the DCVars in EAX)
    works out max(now, last shot + delay) itself.
- `DroneWeap_ThrowGrenade` tilts the throw with RotMatrix (0xd6560), not `Mat_Rotate`.
- `_VECTOR` is 12 bytes on the Xbox, so their "w is stack garbage" divergences do not arise. `DroneWeap_FireWeapon`'s
  flash rotation writes `.z = 0`.
- Register arguments their C++ does not show:
  - `NDrone2_DealWithObjHit`: the drone in EAX.
  - `DroneWeap_DoFiring` and `DroneWeap_NextBulletTime`: the DCVars in EDI.
  - `DroneWeap_ManageBurstCount`: the DCVars in EAX.
  - `DroneWeap_BurstDelay`: the drone in ESI.
- Layout, not errors: the bot traits are `BOT_vars_t.stats.traitFlags` (+0x84; theirs +0xac).
- Their additions, not in the Xbox: the NULL guards on `NDrone2_ExplosiveImpact`'s source object,
  `NDrone2_DoHitEffects`' sound, `DroneAnim_LocationImpactAnim` and the gas puff.
- Ghidra's labels for the DroneFiring floats read wrong in `DroneWeap_BurstDelay`: 0x164094 is BurstDelay_Min (the
  clamp) and 0x1640a0 is MinDist (compared with the distance), as Player.cpp names them. The drone tuning floats now
  have one definition, in src/action/game/drone/DroneTuning.h.

Everything else in their readings of parcel 6 matched the Xbox.

### Parcel 7
- Separate functions on the Xbox that theirs inline:
  - vision: the opponent-in-view test (0x656a0, ours `DroneVision_OpponentInView`), the bone and gun-through-wall half of
    their `SeekOpponentLOS` (0x66030, `DroneVision_OpponentLOS`), the eye's line of sight of `CanSeePosition`
    (0x64660), and the two drone-list walks (0x64580, 0x645c0) of `ProcessDroneSight` and `FindAlertedDrones`.
  - alerts: `EnemyAlerts`' global-alarm case (0x650a0) and its four shout handlers (0x65120-0x651e0); their
    `DroneVision_ShoutFromOtherDrone` is inline in each, and in case 0x16.
  - opponents: `NDrone2_SetOpponentAimPos` is 0x378f0, with its bone lookup 0x37870; `FindOpponent`'s angles to the
    target (0x37bc0); three parts of their `Drone_InitComms` (0x31190, the targets' frame; 0x31110, clearing them;
    0x30950, the dummy player). `Drone_ProcessOpponents` is 0x313e0.
  - movement: `NDrone2_Steer` 0x43070, `NDrone2_EvaluateObstacle` 0x42cc0 (the obstacle drone in EAX, the record in
    ESI), `SphereSphereSweep` 0x42bb0 and its quadratic 0x42b20; `DroneMove_SetBoundryFlags` 0x454e0 (calling 0x453e0
    four times), `DroneMove_NoBunching` 0x437f0, `DroneMove_ObjectMissionPathDistance` 0x44cb0,
    `NDrone2_NearestNodeDCV` 0x44670 (theirs builds the node search itself), the three astronaut moves 0x44f70,
    0x45230, 0x452c0.
  - collision and tracking: `NDrone2_DoCollision` 0x3cce0, `DroneFunc_NearPlayerCollision` 0x3b7f0,
    `NDrone2_DoGravity` 0x3cd50 (a tail jump into DoCollision, where theirs repeats its tests; the same result), the
    fall 0x3ef30 (their gravity lambda), `NDrone2_AimTest` 0x3ab90, the aiming 0x3ab00, `NDrone2_DoHeadTracking`
    0x3a850 (it calls `NDrone2_HeadTrackObj` 0x3a6c0), the nearest-in-room choice 0x3a780,
    `NDrone2_DoHeadTrackingBehaviour` 0x3a940, `NDrone2_DoTracking` 0x3ac10, and the feet's collision 0xab390 (the
    object in EAX, the hit in ECX).
  - routes and spawners: `NDrone2_InitAIPath` 0x44a20, the creep start 0x46220 (its first argument never read),
    `NDrone2_FollowRoute` 0x46a80, `NDrone2_UpdatePatrolRoute` 0x46c20, `NDrone2_UpdateMissionRoute` 0x46d40,
    `NDrone2_FindAlarmPoint` 0x470f0 (in their drone_combat.cpp), the goal set-up 0x470b0, a new wave's reset 0x30860,
    the wave speech 0x30910, `DroneSpawner_DroneDelete` 0x308b0 (called from `Drone_Delete`).
- Inline on the Xbox: `NDrone2_GetAnglesToMoveTarget` (in `NDrone2_maybeCalcDistanceAndDirToTarget`, 0x46080).
  `NDrone2_DroneInSights` is 0xde5e0, `XOR AL,AL; RET`, which MSVC shares with `Particles_Update`: always false, as
  theirs. `AnimScriptPeekSfx` has no counterpart: the Xbox `NDrone2_SpeechSFX` has no deferred facial sound (their
  `talk_pending`, `SFXPreCache`); it calls `NDrone2_PlayFacialAnim` (0x374e0) unless +0x36 or +0x37 is set.
- `NDrone2_UpdateMissionRoute` returns `FollowRoute`'s status (0x46d7e `MOV EAX,EDI`); their note says it returns
  nothing.
- `DroneSpawner_SpawnDrone` only calls `NDrone2_CreateFromDIVars(&entry->vars)` and `NDrone2_PostLoad_Init`, and
  counts (0x324a0-0x324c0). Their creation-time draw, DIVars copy and group set-up are not in it.
- `NDrone2_PlaySFX` is cdecl with five stack arguments (Ghidra's `__fastcall` with seven is wrong). It logs every
  request in a 16-entry ring at NPCGlobals+0x13a8, played or not, and writes the frame to Drone_tag+0x3d0.
- NaN and precision, from the disassembly:
  - `DroneVision_ConsiderAlerted`'s frame test is on the x87 with both frames loaded by FILD (plus 2^32) and not
    rounded (0x659d4-0x65a15); theirs is in float. `OpponentVisibility`'s `< 0.7` counts a NaN as seen (0x65711
    `TEST AH,5; JP`).
  - `NDrone2_EvaluateObstacle` compares `limit * 7030` unrounded (0x42e65); the quadratic tests the unrounded
    discriminant against 0 (0x42b38) but the float one for "> 0" (0x42b60). Theirs is in float.
  - `NDrone2_Collision`: a NaN root height gives the dead drone's radius as the root height (0x3f39f), not 0.4, and the
    dead capsule's half distance, not the half axis (0x3f442); the fall's water test falls on a NaN (0x3ef80), where
    theirs returns.
  - `DroneFunc_HandleSoundAlerts` stores the alertness step rounded but adds it unrounded.
  - The bots' "lost opponent" patience is `__ftol2((_FRAME_RATE * 20) * BOT_getAggressionMul)` compared unsigned
    (0x37d8d-0x37da9); theirs `fptoui(FRAME_RATE * (mul * 20))`. `SetOpponent` converts unseenFrames unsigned on the
    x87 (0x37ae7).
- `NDrone2_FindOpponent` walks MPGame's 10 Xbox player slots (0x262754-0x262934, stride 0x30), and slots 4-9 for "a
  bot already targets it"; theirs 8, and 4-7.
- The obstacle record of `NDrone2_Steer` is 0x30 bytes on the Xbox, the obstacle's ending where the drone's begins
  (ESP+0x18 and ESP+0x48); theirs is one 0xd0-byte record.
- Layout, not errors: the sound alertness is `DYNAMICSOUNDS.alertness` (+0x38; theirs +0x48); a bot's view of a player
  is `botVars->players[i]` (+0x94 + 16i; theirs +0xbc), its weapon +0x75c (theirs +0x768) and its flags +0x728 (theirs
  +0x734); the spawner's template is 0xac bytes (theirs 0xd0), "not drawn recently" `obj_tag.drawnCountdown` (+0xd8).
- Their additions, not in the Xbox: the NULL guards in `Drone_PositionVisible`, `NDrone2_NotNearArmedDrone`,
  `DroneFunc_LostSightTalk`, `DroneMove_NextRouteAngle` and the feet's `Coll_AddHitToList`; the 16-box cap in
  `AnimGetCollData`; the group bounds in the spawners and `DroneSpawner_Init`'s entry bound; the zeroed `w` of their
  4-float vectors in `GetWeaponInfo` and `GunThroughWall`.
- `DroneVision_ShoutFromOtherDrone` was guessed to be 0x650a0 from the call graph; it is the global alarm, which theirs
  inlines in `EnemyAlerts`.

Everything else in their readings of parcel 7 matched the Xbox.

Found along the way:
- **Registers a port must keep.** The original keeps the drone in ECX across its call to `NDrone2_ObjectIsArmed`
  (0x64be0) in `DroneVision_LogSeenOpponent`: the whole-program optimiser knew that callee leaves ECX alone, and our
  port of it does not. The caller is ours now, so the game is unaffected, but the shadow's original run had to keep
  0x64be0 original. `tools/caller_saved.py --action` now lists every live original caller that reads ECX or EDX after
  calling a port whose original never writes them. It finds four, all in math.cpp's ports, which their current
  compiled forms happen not to clobber (maybeAtan2: Vec_Cartesian_2_Spherical; Vec_Normalise: Mat_LookAt and 0xd8590;
  Vec_NormaliseLen: Cel_ObjectLeftCel); Vec_Normalise and Vec_NormaliseLen depend on the CRT's sqrt helper for that.
- **FSIN, FCOS and acos.** The host's sin, cos and acos (SSE2) can differ from the x87's FSIN and FCOS and the XBE's
  `__CIacos` by an ulp or two; the shadows count such results apart rather than as differences (the user accepts
  1-ulp differences of this kind; the time demos are the check that they do not matter). Since then `FSin` and `FCos`
  run the instructions themselves and return an `Extended`, the x87's 80-bit value, so their callers match exactly. A
  NaN keeps its sign and payload through the helpers in src/action/math/X87Ops.h (`FSin`, `FCos`, `Negate` for FCHS, `X87NaNs` for an operation on two NaNs, where SSE2 takes the first and the x87 the larger
  or the positive). MSVC compiles `-x + c` as `c - x` and `x <= 0 ? 0 : x` as MAXSD, both of which lose a NaN's or a
  -0's sign.
- `ShadowHook_Install` no longer warns "cannot follow" when a test hooks an address one of the standard fakes already
  hooks (0x1a190).
- `DroneSpawner.cpp` existed but was not compiled; it now is, and actionhelpers.h no longer includes DroneSpawner.h
  (parsemap.cpp does).

### Parcel 8
- Folded on the Xbox (MSVC's identical-code folding), so one Xbox function serves several of their names:
  - `init_DMODE_Normal`, `_Guard`, `_Retreater`, `_Attacker`, `_Assassin` and `_Interogator` are one jump to
    `NDrone2_init_DMODE_Defaults` (0x408a0, now named `NDrone2_init_DMODE_Normal`; a jump leaves nothing to port);
    `_Hostage` and `_HostageTied` are 0x40a80; `_Mayhew` and `_TruckDriver` are 0x40d10.
  - `init_DMODE_DeleteMe`, `_Ninja`, `_Bot`, `NDrone2_initDTYPE_BotInit`, `DroneAnim_SnapRotate2` and
    `DAStateFunc_AimStand` are the shared empty function 0xe0ec0 (Ghidra's `__profiling_or_debugging_hook_point`).
    DefaultInit's call of it at 0x4184b (their `control_init_collision`) does nothing.
  - Ghidra had split `init_DMODE_HostageKiller` (0x409b0) at a bogus FUN_000409fc in its middle; the function runs
    whole from 0x409b0.
- Separate functions on the Xbox that theirs inline:
  - modes and types: `NDrone2_GetDTYPENEW` is 0x3fae0 (the drone in EDI, the mode in ESI; only
    `GetDroneTypeAttackTypeFriend` calls it, so ours is a static helper there). `GetDroneTypeAttackTypeFriend` has its
    mode-0 call (the base DTYPE) inline. The state for a drone with no second type is 0x3fd20 (the drone in EAX; ours
    `NDrone2_SecondStateFor`, static), and `DoTypeSettingsOLD`'s worked-out initial state is 0x403b0 (ours
    `NDrone2_DefaultInitialState`).
  - anim: `DroneAnim_SetDAnimNow` 0x35dc0, `DroneAnim_FreeScript` 0x33120, and register-argument helpers behind
    AUTOLTCG adaptors (now reached only from our code): `CanSetAnimCall` 0x32dd0 (EAX), `CallFullyComplete` 0x33e60
    (ESI), `SetDAnimVeryInternal` 0x32f30 (EAX = &animMode, DX the row), `BaseCallState` 0x32ed0 (AX),
    `SkipAnim` 0x32fe0 (EAX), `TakeCall` 0x344f0 (ESI), `GetDAnimForAnimState` 0x33040 (AX, DX, ESI, EDI),
    `GetDAnimForCall` 0x33de0 (EBX, DX, AX, ECX).
  - core: `Drone_InitComms`' combat music and channels 0x97 and 0x98 (0x314a0, ours `Drone_UpdateCombatChannels`),
    `ControlSTANDARD`'s door step (0x3cbf0, `NDrone2_HandleDoor`), `DroneSpawner_CreateBlank` (0x2fb90, called by
    `Drone_PostLoad_Init`); `PreDroneControl`'s change of mode is a call to `NDrone2_ChangeToAttackMode`.
- Inline on the Xbox: `DroneAnim_ZeroAnimCall` (a memset in TakeCall, CallFullyComplete and CallAnim),
  `CheckForAnimComplete` and `SetAnimCall` (in `CallHandler`, 0x35cd7 and 0x35d72), `NDrone2_AddToGroup` (in
  `SetupGroups`, 0x36ea0), `DroneInit_TakeXHits` (in `DefaultInit`, 0x42543: folded to weapon 0x33's damage, doubled
  by `fadd st0,st0` off difficulty 1; their `weapon < 0x48` test is gone), `Drone_PauseNoDraw` (twice in
  `InitComms`, the only user of 0x1dfa2b; ours is a static helper).
- Their additions, not in the Xbox: the bound on the secondary mode's index and the anim-object guard in
  `DoModeSettingsOLD`; the guard on property 0x20's alertness index in `DoModeSettingsNEW` (a two-bit field, which
  cannot overrun the four levels); the guard on `animState->currentWeaponId` in `DefaultInit` (0x42739); the script
  NULL guards in `SetHTAnim` (after AnimScriptAppendBlend, 0x33fc3), `OnStartAnim` (0x331e5) and `DAnimFunc_Run`
  (0x35edf).
- Values: `Drone_PostLoad_Init`'s default slope is 0x3f5f66f3 (0x32011), an ulp below theirs; `Drone_LevelReset`
  sets NPCGlobals+0x14c to 6 (0x317b4), not 3.
- NaN and precision: `PreTransCallback` compares `rootHeight - 0.02` unrounded with 0.2 (0x33381); theirs in float.
  The mode and type settings test the alertness for "at least 1" (`test ah,1`: a NaN is not), and the sniper's
  initial state for "below 1" (`test ah,5`).
- No deferred facial animation in `ControlSTANDARD` (their `talk_pending`): the facial anim counts as done when the
  script's flags (+0x74) have 0x10000000 set (0x3f6cd). Zoe's control has `DroneFunc_SetMissionFailReason(15)`
  inline (0x3f8f2), the same effect as theirs.
- `NDrone2_CreateObj`'s "owner" is `DIVars_tag.glist` (+0x1c), handed to `Control_CreateObjEx` as the celglist; the
  object type is then 0x4d (MINISUB).
- Layout, not errors: the bot weapon `PostLoad_Init` reads is `BOT_vars_t.weapon` (+0x75c; theirs +0x768); their
  DefaultInit range_e8, range_f0 and range_fc are our viewRange (0xcc), combatRanges[1] (0xd4) and combatRanges[4]
  (0xe0); the slot byte at Drone_tag+0x465 is the anim state's priority. `DoModeSettingsNEW`'s offsets are the
  GameCube's too: its +0x121 (`chatSwitchChannel`) is the switch channel DSTATE_CastleChatGuard1 waits for.

Everything else in their readings of parcel 8 matched the Xbox, the mode initialisers' property lists and every jump
table (read by its bytes) included.

Found along the way:
- **Shadow fakes and inlining.** A fake on an original's address does not catch a call our code makes within one
  file, where MSVC may inline the port: `DroneWeaponShadow`'s fake on 0x35dc0 (our `PlayFiringAnim` inlines our
  `SetDAnimNow`) and `DroneOpponentShadow`'s on 0x31260 (our `Drone_ProcessOpponents` inlines `Drone_GetOpponentInfo`)
  stopped firing, which crashed one suite and failed the other. Both now run the real chain down to deeper fakes.
- `Drone_tag+0x858` holds the death script's hashcode (key 13, now `deathScript`), not an object: NDrone2_Death.cpp
  tests it against 0. `DIVars_tag.glist` is a `celglist_tag *`; `Drone_tag+0x82c` is the door the drone is at
  (`door`), and +0x28c a `Drone_tag *`.
- `DroneTypeSettings`' control functions (0x3f8a0, 0x3f910, 0x3f930) had no function in Ghidra; the json counted them
  in `ControlSTANDARD`, which ends at 0x3f89b.

### Parcel 9
- `NDrone2_DSTATE_RunForCover` slides towards the cover by 0.025 of the gap a frame (`push 0x3ccccccd` at 0x5bb7c);
  theirs uses 0.1. The other slides are as theirs: UnderCoverInit and UnderCoverIdle 0.08, SpecialDeath 0.05.
- Found in our first drafts, not in theirs (their readings were right): `DroneFunc_FirstSightState` returns
  DSTATE_Interogate (0x3c, `mov ax, 0x3c` at 0x3eb43) for CastleIndoors1 and the RefRoom, and DSTATE_CivilianChallenge
  (0x3e) for TowerA and TowerB, by the jump table at 0x3ebd8/0x3ebe4; the eight `NDrone2_Can*` side moves test
  `Drone_tag` 0x290 (`bunchFlags`, which DroneMove_NoBunching sets: 2 ahead, 4 behind, 8 left, 0x10 right), not the
  boundary flags at 0x294; `NDrone2_DSTATE_AllyLeadMissionWait` reads the handler's drone argument (0x5242d).
- `DroneAnim_ValidateCoverAnim` (0x33c10) takes the DCVars in ESI and the anim state in DI (an AUTOLTCG adaptor), and
  reads the anim table for state 7 directly: `DroneAnim_CanDoAnimState(dcv, 7, 0)` folded in.
- GameCube check 92 ("ERROR: Drone has no second weapon", 0x800b032c) is `NDrone2_DSTATE_StunGrenadeRecover`
  (0x562a0); the Xbox dropped the test.

### Parcel 10 (package A: player.cpp and player_traverse.cpp)
- Their `Player_FeetOnPoint` is `PlayerNotDrone_FeetOnPoint` (0xab450), whose fourth argument is never read; their
  `Player_CollLadder` is `PlayerCollLadder` (0xae930).
- `Player_ZeroG` turns at REC x PI x 0.4 a frame (not their 2 pi / 5), and eases its turn by FRM x 0.2 x gap x 0.2 on
  the previous turn, clamped to FRM x 0.05 either way.
- `Player_Climb` leaves the ladder through a function of its own, `Player_LeaveLadder` (0xaeac0), which also tests
  weapon 0x47.
- `Player_CollWire`, `Player_Wire` and `Player_Creep` place the hands through two helpers, `Player_HangByHandA`
  (0xafa80) and `Player_HangByHandB` (0xafaf0): x87 sin and cos of -rotation.y, offsets of +-0.0542. The grab's root
  motion uses 0.082 (0x3da7ef9d).
- `Wire_Create` negates the rotation's z before creating the object.
- `Player_CollCreepWall` casts its ray from the point on the line down by 5.
- `camera_tracking` measures along the line from its position, not its centre.
- `Player_Update` fades the HUD by two steps a frame (it decrements twice).
- `MP_RegisterBulletHit` never reads the health it is given.
- `BLData` is 0x8fc bytes (ours was 0x8f9): `Player_Init`'s create size is 0x8fc.

### Parcel 10 (package B: player_weapon.cpp)
- Their three names without an Xbox function of that name: `Draw_LaserBeam` is `Player_SetupLaser` (0xb8260, already
  ours), `Draw_TaserBeam` is `Player_SetupTaser` (0xb8400) and `FractalThing` is 0xb82e0 (renamed in Ghidra). On the
  Xbox `Player_MuzzleFlash` calls the first two, after its own bone lookup and ray.
- Two Xbox helpers they inline: 0xb9f70, which `Player_WeaponChange` asks whether it can go back to the last weapon or
  gadget (`Player_WeaponUsable`, our name), and 0xb7bd0, the "alive and not in state 1" test of the drones that
  `Player_SetWeaponAnimObj` makes before lowering the gun (`Player_DroneAwake`, our name). Both FUNC_AT.
- `Player_WeaponRecoil` does not set the gun's rotation itself: it calls a register-argument function for that,
  `Player_TiltWeaponTowardsCrosshairs` (0xb7eb0, the player in EAX), before `Player_SetWeaponAnimObj`. Its walk sway
  lifts by |cos(phase)| (FCOS), not |sin(phase + pi/2)|, and keeps that value unrounded on the x87 until it is added.
- `Player_SetFiringAnim` takes the weapon in EDI and `Player_WeaponInitBullet` the player in EAX (AUTOLTCG adaptors).
- `Player_GetAimingPoint` divides the crosshair offset by the zoom before multiplying by the screen size (theirs
  multiplies first); only the rounding differs.
- `BLData::weaponStats` starts at 0x158, not 0x15c: each 12-byte entry is the remembered zoom (float), the clip, the
  "have it" flag, the fire mode and the variant. Ours was laid out 4 bytes late, with the zoom in the previous entry's
  padding. `Player_RamSave` copies 33 ammo counts (0x42 bytes, not the 34 of `BLData::ammo`), the 114 entries and the
  0x33 bytes after them.
- `Check_AutoAim`'s angles are FPATAN and the C runtime's x87 `__CIasin` (called as the XBE's own, 0xee8a0);
  `Player_SetWatch` takes its hands' angles modulo 2 pi through the runtime's `fmodf`, which ours does with `fmod`
  (exact for the finite values it is given).
- Fields named (Player.h): `autoAimTarget` 0xd4, `beamObjs` 0x77c, `gadgetTarget` 0x810 (Check_Target's), `zoomSound`
  0x828, `swayPhase`, `recoilX`, `recoilY`, `idleSwayPhase` 0x86c-0x878, `refireTimer` 0x8bc, `lastWeapon` 0x8c0,
  `lastGadget` 0x8c2, `ramLoadedWeaponId` 0x8c4 (a short), `burstShotsLeft` 0x8c6, `muzzleFlashFrames` 0x8c8,
  `idleTicks` 0x8ca, `idleWait` 0x8ce, `twinGunHand` 0x8e8, `idleState` 0x8e9, `shotFired` 0x8eb, `bulletSpawned`
  0x8ec, `taserHit` 0x8ed, `laserSight` 0x8f4; `GUNTURRET::owner` (was `drone`: the player who set the Ronin down
  as well); `GlobalVars_t::DecoderDisplayShown` (0x1f657c).

### Parcel 11 (anim.cpp)
- `AnimLoadFile` (0x14a20) does not call `MP_NeedSkin` for a skin, as theirs does; nothing on the Xbox matches that
  name here.
- `AnimPostLoadInit` (0x11200) gathers the loaded skins in a buffer of 128 hashcodes (theirs: 0xa8).
- Their `AnimObjectNew` swaps skin 0x05000090 on two levels inline: the PS2's own table. The Xbox calls
  `maybeFixupSkinHashcode` (0x11350, the skin in EAX): on HendersonA, B, C and Tower2A 0x05000036 becomes 0xc3 and
  0x05000038 0xc2; on CastleIndoors1, CastleIndoors2, EvilBase and EvilBaseC 0x15 becomes 0xbd, 0x18 0xc5, 0x3b 0xbf
  and 0x5e 0xbe (a jump table on the level, based at 0x07000001).
- Their `AnimSeqGetInfo`, `AnimSeqSet` and `AnimSet_WalkUpdater` are 0x14420, 0x17960 and 0x171a0, the first two
  with register arguments (EAX raw, ECX hash, ESI seq; ECX script, EBX hash and five on the stack).
  `AnimDistanceTableGet` (0x174a0) takes the hash in ESI; `AnimDistanceTableCreate` (0x16950) is called through it
  and left to the original. `AnimDistanceTableInit` is the `nDistanceTables = 0` in `AnimPreLoadInit`;
  `AnimTagGetUnique` is inline in `AnimSetAppend` and `AnimTagAllocAndGetUnique` (0x15970); `AnimSeqDelete` is inline
  in `AnimScriptDelete` and `AnimScriptNew`.
- Corrected in ours, not theirs: the overlay script flag is 0x4000 and sharing the root 0x8000 (ours had one for
  both); `AnimSleeveGetEntity` (0x118b0, EAX) tries all eight sleeves from the index, wrapping (ours stopped at the
  end, and was not patched in); our `maybeFixupSkinHashcode` had its level groups crossed.
- The comparisons follow the original's FCOM tests: `test ah, 1` takes the "less" branch for NaN as well, so many
  are written `!(x >= c)`.
- Fields named (Anim.h): `AnimObj::sleeveGlist` 0x30 (was `maybeSleeveGlist`), `distancePhase` 0x64 (was
  `unknown64`), `rootHeight` 0x54, `heightOffset` 0x58, `distanceThisTick` 0x5c, `rootTravel` 0x60, the head-track and
  aim rates, ranges and angles 0x68-0x84, `poseBufSize` 0x88, `facialTimer` 0x8a, the target bones 0x8c/0x8d,
  `headTrackDelay` 0x8e, `lookMorphX/Y` 0x8f/0x90, `facialState` 0x91, `lod` 0x94; `ANIMOBJ_POSE_VALID` (was
  `ANIMOBJ_FLAG_1`) and the other ANIMOBJ_, ANIMSCRIPT_, ANIMLIST_ and ANIMSEQ_ flags; the whole of
  `sAnimScript_tag`, `sAnimSeq_tag`, `sAnimRawSeq_tag`, `AnimDistanceTable` and `sAnimSet_tag`; `BLData::animSet`
  is an `sAnimSet_tag *`.
- docs/anim, which the brief points to, is on the blender-exports branch (7af6075), not this one, so it is not
  updated here.

## Original bugs found while porting (kept; candidates for FIX_ORIGINAL_GAME_BUGS)

- `Script_Load`, a file with 0x3f streams or more: hands its record to `DList_RemoveFromInUse2Free(&ScriptList)`,
  a list it was never in, and leaves it in the hashtable.
- `Script_FreeFromHashTable`: calls `Mem_Free` on the -1 left for a wrong-version script file.
- `Script_EntityStart` writes as many placement parameters into `tptr` as the stream gives, and
  `Script_EventHandler` reads as many arguments into `args[8]`: neither is bounded.
- `Script_CreateEntity` compares the object's type with the placement type for switch, destroy, trigger, pickup
  and emitter entities. They never match, so those objects are always deleted and created again, never reused.
- `FuseBox_Update`: a broken fuse of list 1 clears list 0's second channel.
- `SP_LoadScript` loads script 0's loadable part, whichever script is active.
- `Door_Create` never checks that `control_create_object` succeeded.
- Unchecked dereferences of the user: `Door_Activate` (one-way and swing doors). Unchecked dereferences of the
  user, viewer and sensor data: `Monitor_Update` and `Monitor_Activate`.
- `PlrStats_SetupScoreTable` trusts the count it is given.
- Channel and message indexes in triggers, doors and the security system are never bounded. Trigger inputs above
  255 read and write `switch_channels_hold`.
- The AI message queue's insert (0x4e3c0) writes through NULL when all 1022 items are in use.
- `Drone_SM_RouteMsg` ignores a failed `Drone_DCVfromOBJ` and carries on with uninitialised DCVars.
- `DroneAnim_LocationDeathAnim` uses `glb_players[0]` and its animState without checking them.
- `NDrone2_GetHeadPos` writes `*pos` unchecked for objects that are not drones.
- `AINetwork_UpdateLinksUsedCount` counts the route on link 0xffff (past the table) when two of its nodes are not
  linked.
- `AINetwork_OptimiseRoute` adds, for each node, the length of the link numbered by the node's slot in its parent's
  link list, not of the link itself.
- `AINetwork_NodeSearchCel` compares a node's distance with the search radius squared; `AINetwork_NodeSearch`'s
  portal retries search the origin's own cel again, not the portal's.
- `AINetwork_BoundsTest` (strict) tests node b with the distance rather than its square, and with node a's height;
  `AINetwork_BoundsNodeTest` also uses node a's height for node b.
- `LinkCreep_Calc` never releases link 0's creeping flag (it tests `> 0`).
- `LinkCreep_Handler` uses the DCVars without a check.
- A* and the emitter improve a listed node's cost without moving it in the open list; `Emitter_GetNodeAtDistance`
  sorts its open list by f values left over from the last search.
- `Drone_CoverLowNode` indexes its five-entry anim table with unchecked placement keys.
- `NDrone_InitDoorNodes` never resets its nearest-door distance between nodes (harmless: the first door is always
  taken).

- `Menu_UpdateMessageBox`: a save refused (saving not allowed, or no codename) returns with its re-entry latch
  still set, so no later load, save, listing or message box runs until the game restarts.
- `LS_FindBlockByLabel` never ends on a header with a label and a zero length. `LS_Load` reads as many bytes as a
  full save of the current state would take, so a file longer than that (a different level count) can be cut in a
  header; a corrupt file can do it too. The shadow test has to avoid both.
- `LS_LoadMission` trusts the level count against its 20-entry stack tables; `LS_LoadFromBuffer` copies
  `Boot_LoadPTPData`'s 0x12c0 bytes into a 0x1000-byte slot (into slot 1); `Menu_EnumSaves` adds names to
  `codename_buf` without a bound.
- `C_CHCHWEAP_Handler` uses `glb_blokes[0]` unchecked; `P_CNSELECT_Handler` answers an option box of type 3 that
  nothing in the Xbox build creates.
- `NDrone2_DoHitEffects` reads the bullet's weapon definition for the impact sound, and with bots on for a fired
  shot's weapon id, without the NULL check it makes for the damage. `NDrone2_ExplosiveImpact` reads
  `impact->unknown14` before its own NULL check. `DroneAnim_LocationImpactAnim` uses `glb_players[0]` and
  `lastHit` unchecked, and the body hit's gas puff (0x36430) uses what `Gas_Init` returns unchecked.
- The explosion's damage (0x3db50) is handed `hitDamageScale` and never applies it.
- `DroneFunc_InjuredTalk` runs only with bots on (`maybeDroneAIEnabled != 0`), which looks inverted; its helper draws
  `Rand_Rand(0)`, always 0.
- `DroneWeap_DoBulletAccuracy` applies `DroneFiring_TooClose_Accuracy` twice.
- `DroneWeap_FireWeapon` converts the sound's pitch, `Rand_Rand(500) - 250`, as unsigned, so a negative draw is about
  4.29e9; its test of the sound against -1 can never pass (a ushort).
- `DroneWeap_ThrowGrenade` uses up a grenade even when a Ninja's own grenade is thrown instead, and builds a bone
  matrix that RotMatrix then overwrites.
- 0x67640 (`DroneWeap_DropWeapon`'s line of sight) passes the argument slot that still holds the drone as the
  hit-list pointer. Ours passes NULL: the ray fills it in either way.
- `NDrone2_DSTATE_Civilian` works out a radius of 10 or 20 for `NDrone2_NearArmedPerson`, which does not read it.
  `NDrone2_DSTATE_CivilianPatrol` attacks for every skin class but changes only class 16's side, where the other
  civilian states go to CivilianScared. Impacts in CivilianPatrol, CivilianMission and CivilianMissionWait send the
  drone to Attack.
- `NDrone2_DSTATE_Patrol`'s timeout plays the look-round anim but sets no new timeout, so it looks round once.
- `NDrone2_PlaySFX` and `NDrone2_SpeechSFX` read their table entry through NULL for a sound number of 0x15a or more.
  `DroneFunc_LostSightTalk` uses `NDrone2_Player()` unchecked; `DroneFunc_HandleSoundAlerts` hands `&sound->location`
  to build_FindCel before its NULL check; `NDrone2_SpeechSFXd` carries on after a failed `Drone_DCVfromOBJ`; every shout
  (messages 0x11, 0x12, 0x13, 0x15, 0x16) finds its sender and does nothing with it.
- `DroneVision_CanSeeObjectFrom` drops the caller's mask for objects that are neither drones nor the player;
  `DroneVision_SeekOpponentLOS` tests a line of sight from the second eye and ignores the answer (it still counts as a
  vision test); `DroneVision_GunThroughWall` hands its own argument slot as the sound pointer and an uninitialised
  hit-list pointer; `DroneVision_FindAlertedDrones`, resuming, counts a row for each pair and restarts the rows.
- `NDrone2_FindOpponent`'s bot branch works out a teammate's angle and fills a DCVars, and uses neither; the bone lookup
  0x37870 reads the position at 0x24 for a NULL object; `NDrone2_DroneAlertToPosition` leaves the last alert's rotation
  and velocity in NPCGlobals.alert; 0x31110 frees every target but leaves its position.
- `SphereSphereSweep`, when the first time is the later, sets both to the second; its quadratic returns both roots as 0
  for a discriminant of exactly 0. `NDrone2_Steer`'s obstacle record never has its flags cleared: they start as stack
  garbage, and once a standing obstacle has been met every later one counts as standing (see Departures).
- `NDrone2_DoAiming` (0x3ab00) compares the aim target with the opponent but aims at targetPos, so it sets the aim
  every frame; `Drone_FeetOnPoint` reads the floor hit's object after `Collide_FreeHitList`; `NDrone2_Collision`'s
  bounds push uses inCel unchecked.
- `DroneSpawner_Create`, `_Init` and `_Delete` never bound the group (31 entries); `_Init` writes an entry for every
  drone in the group's chain whatever the count it sized them by, and clears what `Mem_Malloc` returns without a check;
  0x30860 tests the slots but not the entries before clearing them; the wave speech plays with NULL DCVars.
  `NDrone2_FindAlarmPoint` returns true for the mission path's alarm point even when it cannot be reached.

- `NDrone2_DoModeSettingsOLD` indexes `DroneModeSettings` with the secondary mode unchecked, and for modes 9, 10 and
  0x13 writes `gameObj->animState->currentWeaponId` with neither checked. `NDrone2_DefaultInit` reads `animState`
  unchecked when bots are off; `DroneInit_Collision` dereferences `obj->animState` unchecked when the ray hits.
  `NDrone2_initDTYPE_SniperAlert` fills a DCVars it never uses; `GetDroneTypeAttackTypeFriend`'s sniper case writes
  SniperIdle and then SniperAim over it (unreachable: the table gives DTYPE 2 its own state).
- `NDrone2_SetupGroups` (and its inline AddToGroup) never bounds the group against 31.
- `DroneAnim_SetHTAnim` uses the blend script after `AnimScriptAppendBlend` unchecked; `OnStartAnim` (a mode 6 row)
  and `DAnimFunc_Run` use `animScript` unchecked; `DroneAnim_EventFunc` carries on with uninitialised DCVars when
  `Drone_DCVfromOBJ` fails.
- `Drone_Delete` uses other drones', bullets' and casings' extraObjectData unchecked (only the gun turret's is checked).
- `NDrone2_SeenPlayerTalk` returns early for skin class 0x10, so its own speech line for that class further down is never reached.
- `NDrone2_FindAmbushCover` reads whether the node is a corner or low cover from the entry after the table's last node, not from the node it chose; with neither, it returns false having already changed the drone's node.
- `ThirdCam_Create` never checks that the object was created.
- `Player_Collision` dereferences the remote-controlled device unchecked in substate 12.
- `Player_CollWire`'s branch for wire type 1 is unreachable.
- `Player_SSScanMode` leaves the matrix's translation x and z as stack (Concat of `RotMatrixY`'s unwritten
  translation).
- `Player_DealWithObjHit` always returns 0.0.
- `Player_SetWeaponAnimObj`, putting away a weapon used up (state 14), clears `weaponStats[prevHeldWeaponId].enabled`:
  the variant held, where everything else indexes the base weapon. In state 9 it adds the idle anim without the check
  for 0 every other state makes.
- `Player_RamSave` and `Player_InitAmmoWeapons` cover 33 ammo types of `BLData::ammo`'s 34.

- `Mat_Identity` leaves the translation alone, so `AnimProcessScriptCmds`' fire event aims its bullet along a matrix
  whose translation is stack, and `AnimObjectDraw`'s laser does the same.
- `AnimFrameSet` blends the bones the skin does not use from a buffer it never initialises.
- `AnimObjectDraw`, for a weapon id out of range, takes a stale morph count as the weapon's model.
- `AnimObjectAimAt`'s upper yaw limit tests the pitch.
- Unchecked: the script `AnimFacialAppend` makes; `seq->startTime` in `AnimProcessScriptCmds`; the sync master in
  `AnimScriptTick`; `morphWeights` in `AnimObjectNew`; the allocation in `AnimPostLoadInit`. A datum without a bone
  sends `AnimGetBoneWorldTrans2`'s chain walk off its stack (128 matrices, indexed by the byte).
- `AnimSeqSet` keeps the unpack cache of the sequence it replaces.
- `AnimFrameResolve` deletes every script whose fade direction is 0 once any script has faded, and gives a new script
  the bone count of skeleton 0.

## Departures from the original

- The AI dispatcher's Exit, Enter and 0x2e messages leave their scope and frames as uninitialised stack in the
  original. Ours writes zero there, as the driving engine's port does for uninitialised stack
  (docs/driving/original-quirks.md, 1.1).
- `NDrone2_Steer` starts the obstacle record's flags at 0, where the original leaves stack (the shadow zeroes the
  stack below the original to match). `DroneVision_GunThroughWall` hands a local for the sound and NULL for the hit
  list where the original passes its argument slot and stack; `NDrone2_SpeechSFXd` zeroes the DCVars a failed lookup
  leaves.
- `Drone_InitComms` hands `DroneFunc_HandleSoundAlerts` a DCVars that is uninitialised stack in the original when the
  first listed drone's object is not a drone; ours is zeroed.
- `Player_CollCreepWall` starts the hit list for its ray at NULL; the original leaves it as stack.
- `Player_WeaponInitBullet` starts its hit list at NULL where the original uses its argument slot (the weapon
  pointer; `Collide_RayIntersect` fills it in either way). `Player_MuzzleFlash`, in multiplayer, starts the flash it
  asks `AnimDatumGetWeaponInfo` for at 0, where the original's slot holds the player pointer.

- `AnimFrameSet` clears the next pose before blending into it, `AnimProcessScriptCmds`' muzzle matrix and
  `AnimObjectDraw`'s gun matrix start zeroed: all uninitialised stack in the original. `AnimObjectDraw` gives an
  out-of-range weapon the morph count 0 where the morph block did not run.

## What the demos do not cover

The recorded set (tests/demos/action) is single player, started with `-level`. It does not reach multiplayer
(`MP_*`, the bots `BOT_*`/`BOTSTATE_*`/`BOTWEAP_*`/`DSTATE_Bot*`, split screen and its rendering), the front end
and its menus, or saving and loading. Parcels in those areas need their own recordings first. The recorder takes
all four controller ports, so a split-screen or bot match can be recorded. A launch option to boot straight into
a multiplayer match, like `-level`, would make that practical. Until then, those parcels come last, with shadow
tests and MenuProbe replays as their check.
