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

## Departures from the original

- The AI dispatcher's Exit, Enter and 0x2e messages leave their scope and frames as uninitialised stack in the
  original. Ours writes zero there, as the driving engine's port does for uninitialised stack
  (docs/driving/original-quirks.md, 1.1).

## What the demos do not cover

The recorded set (tests/demos/action) is single player, started with `-level`. It does not reach multiplayer
(`MP_*`, the bots `BOT_*`/`BOTSTATE_*`/`BOTWEAP_*`/`DSTATE_Bot*`, split screen and its rendering), the front end
and its menus, or saving and loading. Parcels in those areas need their own recordings first. The recorder takes
all four controller ports, so a split-screen or bot match can be recorded. A launch option to boot straight into
a multiplayer match, like `-level`, would make that practical. Until then, those parcels come last, with shadow
tests and MenuProbe replays as their check.
