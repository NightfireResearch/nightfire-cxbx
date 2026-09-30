# NDrone2: architecture, lifecycle and state machine (Xbox action engine)

Area A of the drone review. Scope: how a single-player AI character ("drone") is created from level data, what runs
every frame and in what order, the message-driven state machine and its 250-entry table, the drone's data
(`DIVars_tag`, `DCVars_tag`, `Drone_tag`, the state-machine block), and how the drone's mode and type pick its
starting state. Behaviour properties, perception, combat and animation are area B; multiplayer bots and navigation
are area C ([behaviours](../behaviours/README.md), [bots and navigation](../bots-and-navigation/README.md)). They are mentioned here only where the control flow passes through them.

Everything is Xbox `default.xbe` unless marked PS2 (`/PS2_EU_51258/ACTION.ELF`, which still has its symbol table:
function names AND data names such as `DroneModeSettings`, `DroneTypeSettings`, `NDrone2_StateFuncs`).

Evidence markers used below: **[dis]** read in the disassembly, **[dec]** Ghidra decompile, **[xref]**
`tools/xrefs_action.json`, **[bytes]** table data read from the XBE, **[PS2]** matched against the PS2 function of
the same name, **[inf]** inference, not verified.

Files in this folder:

| file | contents |
|---|---|
| README.md | this document |
| A1-state-table.md | all 250 `NDrone2_StateFuncs` entries: Xbox and PS2 function, size, folded duplicates, messages handled, transitions |
| A2-mode-and-type-tables.md | `DroneModeSettings` (36 modes) and `DroneTypeSettings` (85 types), decoded |
| A3-drone-fields.md | every `Drone_tag` offset the decompiler shows being accessed, with its readers and writers; state-machine field accesses |
| A4-function-inventory.md | the 337 functions of this area: address, size, callers, already ours? |

---

## 1. Overview

A drone is an ordinary game object (`obj_tag`, `objectType` 2 `OBJECTTYPE_DRONE`) whose `extraObjectData` is a
0x978-byte `Drone_tag` (PS2: 0xd20). All drones are also on one linked list, `NPCGlobals.NDrone2List`
(head at 0x1e5634; `Drone_tag` +0 prev, +4 next). The object system calls `Drone_Control` for it every frame and
`Drone_Delete` when it is deleted; the drone's behaviour is a message-driven state machine in the style of Rabin's
"state machine language with messaging" (Game Programming Gems): each state is one function that receives
messages (Enter, Exit, Update and ~60 game events) and changes state by requesting a new one.

```
level load                       first frame                         every frame                      deletion
----------                       -----------                         -----------                      --------
parsemap -> Drone_Create         Drone_PostLoad_Init                 control_movement_object_handler   obj->flags |= 1
  (placement type 15)              NDrone2_SetupGroups                 Drone_InitComms (global AI)      control_delete_object
  NDrone2_CreateFromDIVars           DroneSpawner_Init (takes over     for each object:                 Drone_Delete
    NDrone2_CreateObj                  its group's drones)               Drone_Control(obj)
      control_create_object(0x978)  for every drone:                       PreDroneControl (timers)
      LList_Add(NDrone2List)          NDrone2_PostLoad_Init               type control fn or
    copy DIVars into Drone+0x874        NDrone2_DefaultInit (keys ->        NDrone2_ControlSTANDARD
                                          fields, mode, type, anim)          -> msg 3 Update -> state fn
                                        Drone_SM_InitObject                PostDroneControl
                                          -> msg 1 Enter to state 0        animation, anim-end events
                                             (DSTATE_Global picks the
                                              real first state)
```

Counts: 250 state slots, 238 distinct state functions (the Xbox linker folded 12 slots onto identical bodies),
36 modes, 85 types (31 real ones plus 54 bot rows). Four functions of this area are already reimplemented
(`Drone_DCVfromOBJ`, `Drone_Create` in `src/action/game/drone/Drone.cpp`; `DroneFunc_HostageSaved`,
`DroneFunc_CheckAlarmRaised` in `NDrone2_Hostage.cpp` / `NDrone2.cpp`).

---

## 2. Lifecycle

### 2.1 Level reset and load (per level)

| order | function | what it does | evidence |
|---|---|---|---|
| 1 | `Drone_LevelReset` 0x31780 (from `ResetMap_GameInit`) | zeroes the whole `NPCGlobals` block (0x50b dwords = 0x142c bytes at 0x1e5630); sets `Drone_SomeDurationFrames` = 10 s, NPCGlobals+0x14c = 6 (max dead bodies before fast fade), `flag0` = 1 (system running), `NumDrones` = 0; calls `Drone_SM_Init` | [dec] |
| 2 | `Drone_SM_Init` 0x4e1e0 | builds the delayed-message pool: 1024 nodes of 0x20 bytes at 0x1ee438 (free list head `DAT_001f6444`, pending list `DAT_001f6440`, sorted by due frame); `drone_SM_LLDelayed` = 0x400 | [dec] |
| 3 | `Drone_PreLoad_Init` 0x2fb00 (from `ResetMap_GameInit`) | `LList_Init(&NDrone2List, 0x978)`, `LList_Init(&AIBoxList, 0xf4)`; clears cover-node, AI-point and group tables | [dec] |
| 4 | parsemap: `Drone_Create` 0x2fcf0 (ours) per placement type 15 | returns NULL if `Drone_bDisableSystem` or key2 (min difficulty) > difficulty; builds a `DIVars_tag` on the stack (pos, rot, keys 0-32) and calls `NDrone2_CreateFromDIVars` | [dec], src |
| 5 | `NDrone2_CreateFromDIVars` 0x2fc50 | `NDrone2_CreateObj`; `obj->creationTimeFrames = Rand_Rand(10000)` (per-drone random phase); copies pos, rot, the 0x84 key bytes into `Drone+0x874` (the drone's own `DIVars_tag`); copies key4 -> +0x128 and key7 -> +0x12c | [dec] |
| 6 | `NDrone2_CreateObj` 0x3f940 | if DIVars+0x1c (a celglist) is NULL: `control_create_object(0x978)`, objectType 2; else `Control_CreateObjEx(0x978, pos, rot, ..., glist, ...)` with objectType 0x4d (MINISUB) - this second path is dead for placements, because `Drone_Create` and `DroneSpawner` always leave +0x1c zero; `Drone+0xc = obj`; `LList_Add(&NDrone2List, drone)` | [dis] |
| 7 | parsemap: `DroneSpawner_Create` 0x30530 (placement 241) | a 0x3c-byte `DroneSpawner_tag` object, type 48; registered in `NPCGlobals+0x8c[group]` | [dec] |
| - | `Drone_CoderCreate` 0x2fd80 | code-driven creation (called by `BOT_init`): fake level_tag with key0 skin, key1, key9 weapon; then sets +0x49e (initial state) from its last argument and calls `NDrone2_PostLoad_Init` at once | [dec] |

### 2.2 First frame: `Drone_PostLoad_Init` 0x31fd0 (from `ResetMap_Load`)

Sets per-level AI tunables (`FLOAT_00274cac..cbc`: view cone 50 deg / 45 deg on CastleIndoors2, etc.), allocates the
AI route emitters, emits cover paths, `DroneSpawner_CreateBlank` (a spare spawner object kept in NPCGlobals+0x1a4),
then **`NDrone2_SetupGroups`** 0x36e90 and **`NDrone2_PostLoad_Init` for every drone on the list**, then door/kick
nodes and `AINetwork_InitPassableBoundries`. [dec]

`NDrone2_SetupGroups`: links every drone with a group (key4, +0x128) into a per-group singly linked list
(`NPCGlobals+0x10[group]` head, `Drone+0x8` next, `NPCGlobals+0x108[group]` u16 count, `Drone+0x124` = group) and calls
`DroneSpawner_Init` for each spawner whose group has drones. `DroneSpawner_Init` 0x30670 copies each group drone's
`DIVars` (0xa4 bytes) plus its cel into a 0xac-byte spawn record, hides it (`NDrone2_Enable(0)`), flags it for
deletion and deletes it (`control_delete_object`). Spawned drones are later re-created from those records by
`DroneSpawner_SpawnDrone` 0x323e0 = `NDrone2_CreateFromDIVars` + `NDrone2_PostLoad_Init` - the same path as a
placed drone. [dec]

`NDrone2_PostLoad_Init` 0x42a90 [dec]:
1. `drone->initialisedOnFrame` (+0xb0) = now
2. **`NDrone2_DefaultInit(&drone->diVars)`** 0x417f0 - all field set-up (2.3)
3. `NDrone2_FindOpponent`, `Drone_GetOpponentInfo`; +0x2bc = opponent
4. `someAiRoute+0x94` (+0x6c8) = `AINetwork_NavPathForPosition(pos)`
5. **`Drone_SM_InitObject(obj)`** 0x4e2a0 - starts the state machine (3.4)
6. MP only: `BOTWEAP_changeWeapon`

### 2.3 `NDrone2_DefaultInit` 0x417f0 (726 decompiled lines) - reading the placement keys

In order [dec]:
1. `control_init_object`, position/rotation from DIVars, `obj->maybeCollision = drone+0x2fc`, link into the room.
2. Allocate the two AI route buffers (`AIRouteDataSizeBytes*2` each: +0x6c4 and +0x768; freed by `Drone_Delete`).
3. Defaults: +0x130 = 1.0, +0xe4 = 1.0, +0x84/+0x88 = 1.0, +0x854 = 0.5, +0x14 = 1, health (+0x90) = 10,
   `obj->curState = 0`, flags +0x3f4 = (+0x3f4 & 0xffea05e2) | 0x801e2; zero 0x3a dwords from +0x148 (opponent block).
4. **Skin** (key0; EvilBase remaps 0x050000b1 -> 0x0500008c): a switch over ~50 skin hashes sets +0xbc (skin class,
   0-0x18) and +0xbe (anim set; default 0xc) and a few flags (+0x14, +0x15, +0x1b, +0x23). Skin 0x0500001e (class 0xb)
   starts unarmed.
5. **Weapon** (key9, 0 = skin default): sets the anim set +0xbe per weapon class and the weapon ranges +0x840/+0x842;
   key10 -> +0x45 (draw weapon). **Grenades** (key11): +0x850 kind, +0x852 count.
6. **Combat range** (key14): +0xd0 = 4.0, +0xd4 (someCombatRange3), +0xe0 (someCombatRange2), +0xcc, +0xc8 per class
   0-5, with level overrides (Tower2A, EvilBase).
7. **Switches and scripts**: +0x118 = key3 (wait switch), +0x119 = key6, +0x120 = key12 (death switch), +0x11a/+0x11b =
   the current state of those channels, +0x11c = key5 (mode), +0x11e = key8 (secondary mode), +0x858 = key13 (death
   script) with the position and rotation saved at +0x85c..+0x870.
8. **Mode**: `key5 < 0x24` -> `NDrone2_DoModeSettingsOLD(drone, key5, &key15, diVars)`, else
   `NDrone2_DoModeSettingsNEW(drone, key5, &key15, diVars)` (section 5).
9. Captain (+0x19): set when behaviour property 9/5/8 matches the difficulty (1/2/3); swaps the skin for the captain
   skin of its class and scales health, damage (+0xe4) and accuracy by `DroneCaptain_Mod_*`.
10. `AnimObjectNew(obj, skin)`; class-12 (ally) property tweaks; both AI routes initialised; goal target cleared;
    `PlrStat_LogEnemySpawned` if side 1.
11. **Play script** (key1, +0x450): 0x06000000 = none; 0x06000892 -> DTYPE 0x1c, 0x0600089c -> DTYPE 0x1b (abseil
    types, [inf]), both keep the old anim set in +0xc0 and use set 0xf.
12. First animation: `DroneAnim_SetAnimD(0x1a5 / 0x82 / 0x7d / 0x32 / 0x11a)` by class and type.
13. If the mode came from the OLD table (+0x32 set): `obj->curState = 0` and `NDrone2_DoTypeSettingsOLD(drone, dtype)`
    0x40430.
14. No cel -> delete. If the initial state is Attack (0x56) or 0x12 (CivilianScared): `DroneFunc_SetAsAttacking`,
    flags |= 0x44000000.
15. Class overrides: 0x18 (astronaut skins 0x05000078/94/ba) -> DTYPE 0x1d, initial state 0xbd (AstronautLaunch),
    health from `weapon_data[0x33]`; DTYPE 0xd (Ninja) -> health 100, accuracy 0; DTYPE 0x1b/0x1c -> +0x23 = 1,
    initial state 0x8e (AbseilInit).
16. `DroneInit_Collision`, +0x94 = starting health; SP: behaviour property tweaks per type; MP: `BOT_postLoadInit`;
    `BodyGlow_Create` -> +0x10.

### 2.4 Every frame

`control_movement_object_handler` 0x2dd00 (game loop) [dec]:

```
Drone_InitComms()                                  0x32a00  global AI step (areas B/C), then per drone:
   switch_NO_DRONES handling (sets/clears Drone_bDisableSystem, flag 0x10 in every drone's +0x3f4 [inf: "frozen"])
   Drone_PauseNoDraw? 0x30950, AINetwork_ClearLinkFlags(0x310), Drone_BuildDynamicAwarePoints, 0x31190,
   Drone_ProcessCoverNodes, Drone_ProcessOpponents? 0x313e0, 0x31110, DroneVision_ProcessDroneSight, 0x314a0,
   NDrone2_NavNodeCache? 0x44de0
   for each drone: DroneFunc_HandleSoundAlerts (-> msg 0x14); count dead drones (states 0x0f, 0x47, 0xbf; 0x48
      after Drone_SomeDurationFrames) and find the oldest (NPCGlobals+0x150/+0x15c) for DroneFunc_DeadDrone
   switch 0x96 on (alarm) -> broadcast msg 0x1e every 30 frames
   Drone_SM_SendDelayedMsgs()                      deliver queued messages that are due
   DroneFunc_CheckAlarmRaised()                    alarm music (ours)
Doors_Calc, Trigger_Calc, switches
for each object: control_funcs[type].update(obj)   -> Drone_Control for type 2 (and 17)
deletion pass: objects with flags & 1 -> control_delete_object -> Drone_Delete
```

`control_funcs` (0x163b98, 12-byte entries update/collide/delete) [bytes]: type 2 DRONE = {`Drone_Control`,
`Drone_CollisionHandler`, `Drone_Delete`}; type 17 DEAD_DRONE = {`Drone_Control`, -, `Drone_Delete`} (only
`BOT_respawn` ever sets type 17: single-player dead drones stay type 2); type 48 DRONE_SPAWNER =
{`DroneSpawner_Control`, -, `DroneSpawner_Delete`}; type 62 AIVOLUME = {-, -, `Drone_AIVolume_Delete`}.

**`Drone_Control` 0x31530** [dec, PS2 same calls]:
1. Skip unless `NPCGlobals.flag0`, not `Drone_bDisableSystem`, and (SP, or MP and the object is a live bot).
2. Build a `DCVars_tag` on the stack {obj, drone, obj->inCel, &drone->SM}.
3. **`NDrone2_PreDroneControl`** 0x3ed20: clear render bit; +0x404 = previous alertness; distances to each player
   into +0x138..+0x144; **mode-change switch** (+0x119 on): key8 == 12 -> delete the drone, else key8 != 0 ->
   `Drone_SM_SetState(NDrone2_ChangeToAttackMode())`; **state timeout** (+0xe8 reached) -> msg 4 scoped to the current
   state; **timer A** (+0x918 set and +0x91c reached) -> msg 0xc with payload +0x920; **timer B** (+0x924/+0x928) ->
   msg 0xd with payload +0x92c; ninja eyes if +0x18. Returns true (it always does, except after the mode switch).
4. If it returned true: the type's control function `DroneTypeSettings[drone->dtype(+0xa9)].control` (+8), or
   **`NDrone2_ControlSTANDARD`** 0x3f5f0 when NULL. Only three types have one, and all three end in STANDARD:
   Zoe 0x3f8a0 (switch 1 -> 0x9b, and 0x9b + player near -> 0x63), Ninja 0x3f910 (`NDrone2_CreateNinjaEyes` first),
   Astronaut 0x3f930 (a bare `jmp`). None of the three is defined as a function in Ghidra. [dis]
5. **`NDrone2_PostDroneControl`** 0x3b670: if active (+0x3f4 & 0x100): obj render flag 0x20; health <= 0 and not dead
   (0x200) -> +0x3f4 |= 0x400 [inf: "dying"].
6. `DroneAnim_SnapRotate`; if animating (+0x3f4 & 0x80000): `NDrone2_DoAnimation`, else `AnimObjectUpdate` and root
   motion (+0x488 along rot.y, clamped by +0x294 flags); the two anim callbacks +0x440/+0x444 (`someFunc1/2`).
7. `DroneAnim_CallHandler` - this is where finished animations turn into state changes and messages
   (`DroneAnim_SetEndAIState`, 3.5).
8. +0x38c = horizontal distance moved this frame, +0x390 = |velocity (+0x3a0)|.

**`NDrone2_ControlSTANDARD` 0x3f5f0** [dec; PS2 names for the unnamed callees]:
1. Alertness (+0x408): clamp to 1, decay by 1/(50 s) towards the base (+0x3fc), never below it.
2. **`Drone_SM_SendMsgSelf(3 Update)`** - the state machine runs here, before perception and movement.
3. Speech channel (+0x3cc) finished -> 0; facial-anim flag +0x36 cleared when done.
4. Only if active (+0x3f4 & 0x100): count by side (NPCGlobals+0x17c/0x180/0x184); bots: `BOT_setOtherPlayerInfo`
   0x1a660; **`NDrone2_FindOpponent`** (perception, B); breath effect; `NDrone2_DoTracking` 0x3ac10 (head tracking);
   `NDrone2_HandleTalking` 0x3c430; movement: if +0x23 == 0 `DroneMove_SetBoundryFlags` 0x454e0 + `NDrone2_Move`
   + `DroneMove_NoBunching` 0x437f0 (-> +0x17), else just `NDrone2_Move`; `NDrone2_Collision`;
   **`DroneWeap_HandleFiring`** (B); door step 0x3cbf0; `DroneFunc_HandleExplosives` (-> msg 0x21).
5. Not active: only `NDrone2_DoTracking(0)`.

So within one drone's frame the order is: timers/switch messages -> Update message (state logic) -> perception ->
movement -> collision -> firing -> animation -> animation-end transitions.

### 2.5 Death and deletion

* Impacts arrive as messages (6 punch, 7 taser, 8 bullet, 9 explosive, 0x17 smoke, 0x18 stun grenade, 0x19 stun
  dart; `Drone_Message` from `Drone_BulletHit` 0x31f20, `Drone_ExplosiveHit`, `NDrone2_DealWithObjHit`). Most states
  pass them to `DroneFunc_HandleImpact` 0x3e6a0, which saves the current state as the resume state (SM+0x10) when it
  is resumable (not 0x53-0x55) and requests PunchImpact / Taser / BulletImpact / ExplosiveImpact / SmokedOut /
  StunGrenadeImpact / StunDartImpact with the message's extraData as the state parameter. [dec]
* The impact states (area B) call `NDrone2_BulletImpact` / `ExplosiveImpact` / `PunchImpact`, which choose
  Death_Anim (0x44), DeathByExplosion (0x45) or KnockedOut_Anim (0x42) when health runs out, and the death
  animation's end state leads to **Dead (0x47)**. [dec, inf for the exact path]
* `DSTATE_Dead` enter: `NDrone2_SetAsDead` (drop weapon, +0x3f4 |= 0x200, clear 0x8000/0x20/0x80, health = 0 unless
  0x1000000), alert status 3, **timer A = now + Drone_SomeDurationFrames (10 s)**, optionally alert other drones to the
  body (msg 0x15 via `NDrone2_DroneAlertToObject`). Update: off screen (obj+0xd8 == 0) -> hide and `flags |= 1`;
  `DroneFunc_DeadDrone`: if more than NPCGlobals+0x14c (6) bodies and this is the oldest -> **FadeFast (0x49)**.
  Timer A (msg 0xc) -> **Fade (0x48)**. Fade/FadeFast delete the object once it is off screen. HostageDead (0x0f),
  AbseilDeath, AstronautDeath have their own versions. [dec]
* Deleting = `obj->flags |= 1`; the deletion pass calls **`Drone_Delete` 0x32130**: frees the body glow, releases
  owned cover nodes, frees the two route buffers, clears every reference to this drone held by other drones (+0x148,
  +0x230, +0x234, +0x23c, +0x240, +0x288, +0x28c, head tracking +0x2bc), bullets (+0x24), casings (+0x20), spawners
  (`DroneSpawner_DroneDelete` 0x308b0), gun turrets (turret explodes), then `LList_Remove(&NDrone2List)`. [dec]
* `DSTATE_DeleteMe` (0xb4): hide and delete at once (mode 12 DeleteMe).

---

## 3. The state machine

### 3.1 Data: `StateMachineInfo_tag` at Drone+0xec (0x28 bytes) and `processFunction` at Drone+0x114

PS2: Drone+0x108 and +0x130 [PS2 struct]. `DCVars_tag.aiStateMachine` points here.

| SM off | Drone off | type | name (proposed) | written by | read by | evidence |
|---|---|---|---|---|---|---|
| 0x00 | 0xec | u32 | `id` (unique per drone, `++NPCGlobals.NumDrones`; the message address) | Drone_SM_InitObject | RouteMsg, SendMsgSelf (sender/receiver), SetState, DeadDrone, HostageKiller, MP code | [dis] |
| 0x04 | 0xf0 | u32 | `curState` (DSTATE being run) | RouteMsgDCV (on change), InitObject | ~48 functions (states, vision, impacts, RouteMsg skip lists, PreDroneControl scope) | [dis] |
| 0x08 | 0xf4 | u32 | `prevState` | RouteMsgDCV (= old cur) | (none found) | [dis] |
| 0x0c | 0xf8 | u32 | `nextState` (requested) | **Drone_SM_SetState only** (217 callers) | RouteMsgDCV | [dis] |
| 0x10 | 0xfc | u32 | `resumeState` (return here after an impact / stun) | HandleImpact, Punch/ExplosiveImpact, ConsiderExplosive, 0x3a410; InitObject = initial | Stunned_Recover, StunDart/StunGrenade/SmokedOut recover, PunchImpact anim end | [dec] |
| 0x14 | 0x100 | u32 | `stateEnteredFrame` (`NumFramesUnpaused` at Enter) | RouteMsgDCV | states (time in state) [inf] | [dis] |
| 0x18 | 0x104 | u8 | `changePending` | SetState = 1, RouteMsgDCV = 0 | RouteMsgDCV loop | [dis] |
| 0x1c | 0x108 | u32 | per-state scratch (timers, counters) | ~42 state functions | same | [dec] |
| 0x20 | 0x10c | u32 | per-state scratch | AllyLead, AllyLeadBondCombat, AllyLeadWait | same | [dec] |
| 0x24 | 0x110 | s32 | `stateParam` (3rd argument of SetState: impact data etc.; Ghidra calls it speechSfxRelated) | SetState, InitObject = 0 | states, `Drone_maybeDisallowedFromSayingThisSfx` | [dis] |
| 0x28 | 0x114 | fn | `processFunction` = `NDrone2_ProcessStateMachine` | InitObject | RouteMsgDCV (every dispatch) | [dis] |

Related per-drone fields outside the block: +0xe8 state-timeout frame (msg 4), +0x918/+0x91c/+0x920 timer A,
+0x924/+0x928/+0x92c timer B (both cleared on every state change), +0x930 per-state scratch (anim id, flags; also
read as a state number by `DSTATE_Disabled`), +0x492/+0x494 end state / end message of the current animation call,
+0x49e initial state, +0x4a0 second-behaviour state, `obj->curState` (obj+0xd0, u16) mirrors `curState`.

### 3.2 Messages: `MsgObject` (0x1c bytes)

| off | name (Ghidra) | meaning | evidence |
|---|---|---|---|
| 0x00 | msgType | message id (table below) | [dis] |
| 0x04 | param_a | **state scope**: 0 = any state, 0xc5 = BotGlobal, else delivered only if it equals the receiver's current state (stale timeouts are dropped this way) | [dis] RouteMsgDCV |
| 0x08 | param_b | sender id (SM id) | [dis] |
| 0x0c | param_c | receiver: 0 = broadcast to every drone, > 0 = SM id, < 0 = MP bot -1-n | [dis] RouteMsg |
| 0x10 | createdFrame | | [dis] |
| 0x14 | handleOnFrame | due frame; later than now -> queued in the delayed list | [dis] |
| 0x18 | extraData | payload (damage record, object, state number...) | [dis] |

Senders (all verified [dis]/[dec]):
* `Drone_SM_SendMsg(type, scope, sender, receiver)` 0x317e0 - immediate, ignored when `Drone_bDisableSystem`.
* `Drone_SM_SendMsgSelf(type, extraData, delay, scope, dcv)` 0x4e890 - to the drone itself: delay 0 goes straight to
  `Drone_SM_RouteMsgDCV`, otherwise through `Drone_SM_RouteMsg` (queued).
* `Drone_SM_BroadcastMsg(type, extraData, delay, sender)` 0x4e900 - receiver 0.
* `Drone_Message(obj, type, extraData, delay)` 0x31830 - the public entry for other game code: obj NULL = broadcast.
* `DroneAnim_SetEndAIState(drone, newState, msgType)` 0x32e70 - at the end of an animation call: SetState(newState)
  if non-zero, then SendMsgSelf(msgType) if non-zero.

`Drone_SM_RouteMsg` 0x4e630: due later -> insert into the delayed list (0x4e3c0, sorted by due frame; the GameCube
version warns "Too many AI messages" when the 1024-node pool is empty, the Xbox writes through NULL [inf from GC
check #87]). Broadcast: every drone on the list except those in WaitSwitch (1), PlayScript (3), HostageDead (0xf),
Dead (0x47), Fade (0x48). By id: find the drone (0x4e1b0, list walk comparing +0xec), skip WaitSwitch, HostageDead,
Dead, Fade, FadeFast. MP (receiver < 0 and bots on): the matching bot player, skipping 1, 3, 0xf, 0x47-0x49.

Message ids (names are proposals; "handled by" counts come from A1):

| id | meaning | sent by | handled by |
|---|---|---|---|
| 0 | null [inf] - every state answers "handled" and nothing sends it | - | 223 states |
| 1 | **Enter** | RouteMsgDCV on a change; Drone_SM_InitObject (first state) | 209 |
| 2 | **Exit** | RouteMsgDCV on a change (to the old state) | 54 |
| 3 | **Update** (once per frame) | NDrone2_ControlSTANDARD | 160 |
| 4 | state timeout (scoped to the state that set it) | PreDroneControl (+0xe8), NDrone2_ReachedDestNode (1-frame delay) | 4 |
| 6 / 7 / 8 / 9 | impact: punch / taser / bullet / explosive (extraData = hit record) | NDrone2_DealWithObjHit, Drone_BulletHit, Drone_ExplosiveHit | ~140 (mostly -> HandleImpact) |
| 10 | attack now / alarm (delay = 1 or 8 s) | Drone_EnableAll broadcast, DroneVision_HaveOpponentSight, HostageKillerAttack | 19; Global: HostageKiller type -> HostageKillerAttack |
| 0xb | to hostages (scope = DSTATE_Hostage) | HostageKiller states, DroneAnim_EventFunc | 1 |
| 0xc / 0xd | timer A / timer B fired (extraData = payload) | PreDroneControl | 59 / 4 |
| 0xe | enable (leave WaitSwitch) | Drone_EnableAll | WaitSwitch |
| 0xf | opponent sighted | DroneVision_HaveOpponentSight | |
| 0x14 | heard noise | DroneFunc_HandleSoundAlerts | 25 |
| 0x15 | drone alert (dead body etc.; broadcast with extraData = NPCGlobals+0x240, 30-frame delay) | NDrone2_DroneAlertToObject / ToPosition (type is the caller's) | 28 |
| 0x17 / 0x18 / 0x19 | smoke / stun grenade / stun dart impact | NDrone2_DealWithObjHit | ~110-120 |
| 0x1a | talk-to-mission-object | NDrone2_MonitorTalkToMissionObj | 2 |
| 0x1d | **force state** (extraData = DSTATE) | (script path, [inf]) | Global |
| 0x1e | global alarm (every 30 frames while switch 0x96 is on) | Drone_InitComms | 29 |
| 0x1f | considered alerted | DroneVision_ConsiderAlerted | 27 |
| 0x20 | anim event | DroneAnim_EventFunc | 2 |
| 0x21 | explosive seen (extraData = object) | DroneFunc_HandleExplosives | 108 (-> DroneFunc_ConsiderExplosive) |
| 0x22 | object hit (non-damaging) [inf] | NDrone2_DealWithObjHit | 2 |
| 0x2e | bot state changed (param_a = new state), to BotGlobal | RouteMsgDCV (bots only) | BotGlobal |
| 0x2f-0x45 | bot messages | MP code (area C) | BotGlobal |

Types 5, 0x11-0x13, 0x16, 0x1b, 0x1c, 0x23-0x25 are tested by states but their senders were not identified here
(area B's perception code sends most of them through `Drone_Message`/`Drone_MessageObjVicinity` with a variable id).

### 3.3 Dispatch: `Drone_SM_RouteMsgDCV` 0x4e410 (custom convention: dcv in ESI, msg on the stack) [dis]

```
if msg.scope not in {0, 0xc5, SM.curState}: drop
handledFirst = false
if drone.dtype(+0xa9) == 30 (Bot) and msg.type == 3:      # bots: BotGlobal sees Update first
    process(dcv, 0xc5 BotGlobal, msg); handledFirst = true
if !process(dcv, SM.curState, msg) and !handledFirst:     # unhandled -> the global state
    process(dcv, dtype == 30 ? 0xc5 : 0 /*DSTATE_Global*/, msg)
while SM.changePending:                                     # possibly several changes in a row
    SM.changePending = 0
    (bots) classify the new state with BOTSTATE_getStateType
    if next in {0x44 Death_Anim, 0x46 SpecialDeath_Anim, 0x47 Dead, 0x55 BulletImpact} or bot type 7/10:
        drone+0x3d = 1                                      # [inf] "dying / impacted" flag
    elif not (bot type 4 and next not in {0xd3, 0xdd..0xdf}): drone+0x3b = 0
    process(dcv, SM.curState, {type 2 Exit, extraData = msg.extraData})
    SM.prevState = SM.curState; SM.curState = SM.nextState; obj->curState = (u16)curState
    drone+0x3f4 &= ~0x10000 & ~0x2000; drone+0x130 = 1.0; timers A/B off (+0x918 = +0x924 = 0)
    (bots) process(dcv, 0xc5, {type 0x2e, param_a = new state})
    SM.stateEnteredFrame = now
    process(dcv, SM.curState, {type 1 Enter})
```

`process` is `SM.processFunction` = **`NDrone2_ProcessStateMachine`** 0x4e180 (cdecl `(dcv, state, msg)`):
`state < 0xfa ? NDrone2_StateFuncs[state](dcv, dcv->drone, dcv->gameObj, msg) : false` - the table is at
**0x177ca0** (PS2 0x29c120). [dis]

A state function returns true when it handled the message. The pattern in all of them [dec]:
```
switch (msg->msgType) {
case 0:  return true;
case 1:  ...Enter: set anims, timers...; return true;
case 3:  ...Update: may call Drone_SM_SetState...; return true;
case 6: case 8: ... DroneFunc_HandleImpact(dcv, msg, curState, 0/1); return true;
...
default: return false;          // -> DSTATE_Global (or BotGlobal) gets it
}
```

The GameCube dispatcher (0x800c3fa8) counts state changes and prints "Possible infinite loop" after 1000 in one
dispatch; the Xbox has no such check (docs/gamecube-checks.md #88).

### 3.4 Changing state

* **Only `Drone_SM_SetState(SM *, DSTATE next, int param)` 0x4e320 writes `nextState`** (217 call sites [xref]). It
  ignores `next == 0` (you cannot request DSTATE_Global; it returns true without doing anything). For bots
  (dtype 30) it asks `BOT_validateStateChange`; if refused it substitutes the bot's pending state
  (`botvars+0x756`) or refuses (returns false). Then `nextState = next`, `changePending = 1`, `stateParam = param`.
  The change happens when the current dispatch finishes (3.3), so a state can request a change from Enter, Update or
  any event, and the Exit/Enter pair runs in the same frame.
* Other writers of the state: `DroneAnim_SetEndAIState` (end of an animation call; from `DroneAnim_CallAnim`,
  `DroneAnim_CallFullyComplete`, `DroneAnim_LocationDeathAnim`, `DroneAnim_LocationImpactAnim`), `DroneAnim_CoverAnim`,
  `DroneFunc_HandleImpact`, `DroneFunc_ConsiderExplosive`, `DroneFunc_DoForcedAttack`, `DroneFunc_DeadDrone`,
  `NDrone2_PreDroneControl` (mode switch), `DroneMove_AstronautCombat`, and BOTSTATE_* in MP. All go through SetState.
* **Initial state**: `Drone_SM_InitObject` 0x4e2a0 sets id, `curState = prev = next = resume = obj->curState`,
  which DefaultInit left at **0 (DSTATE_Global)**, and sends msg 1 Enter to itself. So **`DSTATE_Global`'s Enter
  handler chooses the first real state** [dec]:
  1. dtype 30 (Bot) -> BotInit (0xc3)
  2. wait switch (+0x118) set and that channel was off at init (+0x11a == 0) -> WaitSwitch (1)
  3. dtype 23 (TruckDriver) -> TruckDriverInit (0x32)
  4. play script (+0x450) set -> PlayScript (3)
  5. otherwise -> **`drone+0x49e`** (the type's initial state, section 5)

  WaitSwitch and Disabled end the same way when their switch comes on (TruckDriver / PlayScript / +0x49e).
* **Global handler (DSTATE_Global 0x4e950)** also handles msg 10 (HostageKiller types go to HostageKillerAttack when
  active and healthy) and msg 0x1d (force state = extraData). Everything else a state does not handle is dropped.
* **Bots**: BotGlobal (0xc5) plays the global role and sees every Update first; area C.

### 3.5 The table

`NDrone2_StateFuncs` 0x177ca0: 250 cdecl function pointers, one per DSTATE - not enter/update/exit triples: Enter,
Exit and Update are messages to the same function. Full decode in **A1-state-table.md**. Summary [bytes + PS2]:

* All 250 entries match the PS2 table's function names except where the Xbox linker folded identical functions:
  - 199-206 (BotGuardian ... BotAssassin), 211 (BotAttackNoOpponent) and 233 (BotStuck) all point to 0x61470, whose
    Ghidra name is `NDrone2_DSTATE_BotStuck` (the body: Update -> SetState(BotIdle)).
  - 35/39 -> 0x4b5c0 (AllyLeadDone / AllyFollowDone), 157/160 -> 0x5d120 (SeenOpponent / SeenDroneShot),
    236/237 -> 0x63760 (BotSeenOpponent / BotSeenDroneShot).
  A reimplementation should still provide one function per PS2 name (the bodies are identical, so one shared body
  per group is also faithful).
* **Corrections for `src/action/game/drone/NDrone2.h`** (verified against the PS2 table): index 35 is
  `DSTATE_AllyLeadDone` (the header calls it AllyFollowDone), 39 is `DSTATE_AllyFollowDone` (header: UNKNOWN1),
  139 is `DSTATE_DonePressAlarm` (header: DronePressAlarm), 171 is `DSTATE_NinjaGetCloseToPlayer` (header:
  NinjaGetTooCloseToPlayer). Ghidra's `DSTATE` enum has the same names as the header, so decompiles show
  "AllyFollowDone" for 35.
* Ghidra reports a 1-byte body for `NDrone2_DSTATE_HostageSaved` 0x50000 (its jump table confuses the body range);
  the decompile is fine, the size in A4 is wrong (real code runs to ~0x50110 plus tables).

State groups (for planning; transitions in A1):

| range | group | notes |
|---|---|---|
| 0-5 | Global, WaitSwitch, Disabled, PlayScript, Idle, Alert | entry states |
| 6-7, 21, 27 | patrol / mission | |
| 8-15, 100 | hostage + hostage killer | |
| 16-26, 46-53, 62 | civilians, party girl, guards, truck driver, Kiko | |
| 28-40 | ally lead / follow (Zoe, Mayhew) | |
| 41-45 | sniper, grenade | |
| 54-61 | castle chat guard, ambush, interrogation | |
| 63-85 | surrender, knock-out, death, fade, stun, impacts | common "reaction" states |
| 86-126 | combat, aim, cover, strafe, roll, smoke | area B |
| 127-156 | investigate, doors, alarms, run for cover, abseil, undercover | |
| 157-165 | seen/heard reactions | area B |
| 166-179 | ninja | |
| 180-188 | DeleteMe, FailMission, JustStand, Tester1-4, HangUp, WaitForever | |
| 189-194 | astronauts, SpaceDrake | |
| 195-249 | bots | area C |

---

## 4. Data structures

### 4.1 `DIVars_tag` (0xa4 bytes; on the stack in Drone_Create, then copied to Drone+0x874)

| off | size | name | meaning |
|---|---|---|---|
| 0x00 | 4 | gameObj | the created object (set by CreateFromDIVars) |
| 0x04 | 12 | position | placement pos |
| 0x10 | 12 | rotation | placement rot (only .y used) |
| 0x1c | 4 | glist | celglist for the unused MINISUB creation path; always 0 |
| 0x20 | 0x84 | keys[33] | placement keys 0-32 (level_tag+0x2c). **The current `maybeSAnimSkin` type is really this key array.** |

Key names follow `level_tag_Drone` (tools/structs_action.json) and docs/level/objects-actors.md (commit 7af6075);
where each key lands in Drone_tag (DefaultInit [dec]):

| key | DIVars off | name | Drone field(s) |
|---|---|---|---|
| 0 | 0x20 | skin | +0xc4 skinHashcode, +0xbc/+0xbe class and anim set |
| 1 | 0x24 | playScript | +0x450 (0x06000000 none; 0x06000892/89c -> DTYPE 0x1c/0x1b); NEW-mode special hashes 0x0600011d/11f/124/126/21f |
| 2 | 0x28 | minDifficulty | checked in Drone_Create only |
| 3 | 0x2c | waitSwitchChannel | +0x118, +0x11a = channel state at init |
| 4 | 0x30 | group | +0x128 (and +0x124 after SetupGroups) |
| 5 | 0x34 | mode | +0x11c; < 0x24 OLD table, else NEW; == 100 -> switch to behaviour 2 on attack |
| 6 | 0x38 | modeChangeSwitchChannel | +0x119 (also the channel `DroneFunc_HostageSaved` sets) |
| 7 | 0x3c | ? | +0x12c |
| 8 | 0x40 | secondaryMode | +0x11e (0/101 none, 12 = delete on switch, 100 = default "has second") |
| 9 | 0x44 | weapon | anim state `currentWeaponId`, +0xbe, +0x840/+0x842 |
| 10 | 0x48 | drawWeapon | +0x45 |
| 11 | 0x4c | grenades | +0x850 kind, +0x852 count |
| 12 | 0x50 | deathSwitchChannel | +0x120 |
| 13 | 0x54 | deathScript | +0x858, pos/rot at +0x85c..+0x870 |
| 14 | 0x58 | combatRange | +0xc8..+0xe0 range set |
| 15-25 | 0x5c-0x84 | behaviour block | `behaviour_util_get` -> +0x3d8 (behaviour 1), +0x3e4 (behaviour 2), +0xab class, +0xac/+0xad modes (area B) |
| 26-32 | 0x88-0xa0 | stats override | global stats table (area B) |

### 4.2 `DCVars_tag` (0x10 bytes, always a stack temporary)

{+0 gameObj, +4 drone, +8 cel (obj->inCel), +0xc aiStateMachine (= &drone->SM)}. Built by `Drone_DCVfromOBJ`
(ours), `Drone_Control`, `Drone_Message`, `Drone_InitComms`, `Drone_SM_RouteMsg`. Every state function and most
NDrone2_/DroneFunc_ helpers take one. `src/.../Drone.h` types `aiStateMachine` as `void *`; it should become
`StateMachineInfo_tag *`.

### 4.3 `Drone_tag` (0x978 bytes)

Every accessed offset is in **A3-drone-fields.md** (366 offsets with readers/writers). The fields that matter for the
architecture (PS2 offsets where the same field was matched in a paired function: DoModeSettingsNEW,
GetDroneTypeAttackTypeFriend, DoTypeSettingsOLD):

| off | size | proposed name | PS2 off / name | meaning, writers -> readers | evidence |
|---|---|---|---|---|---|
| 0x000 | 4 | prev | | NDrone2List link | [dis] |
| 0x004 | 4 | next | | NDrone2List link (walked by 0x4e1b0, RouteMsg, InitComms) | [dis] |
| 0x008 | 4 | groupNext | | NDrone2_SetupGroups -> DroneSpawner_Init | [dec] |
| 0x00c | 4 | gameObj | 0x0c gameObj | CreateObj | [dis] |
| 0x010 | 4 | bodyGlow | | DefaultInit (BodyGlow_Create) -> Drone_Delete | [dec] |
| 0x014-0x01b | 1 each | skin flags | 0x19 = PS2 isCaptain | +0x18 ninja eyes, +0x19 captain, +0x1b impact immunity [inf: HandleImpact skips when set] | [dec] |
| 0x023 | 1 | flies | | abseil / astronaut movement (routes get flag 0x80, ControlSTANDARD skips steering) [inf] | [dec] |
| 0x029 | 1 | usingSecondBehaviour | 0x29 usingSecondBehaviour | ChangeToAttackMode, 0x3a410 | [dec] |
| 0x031/0x032 | 1 | modeSettings flags | 0x31/0x32 | 0x32 = mode came from the OLD table (DefaultInit then runs DoTypeSettingsOLD) | [dec, PS2] |
| 0x03b/0x03d | 1 | | | written by RouteMsgDCV on every change (see 3.3) | [dis] |
| 0x044 | 1 | side ("isCivilian" in Ghidra) | 0x44 isCivilian | 1 enemy, 2 ally, 3 civilian/neutral (mode table col 1, or behaviour class) | [dec, PS2] |
| 0x090 | 4 f | health | 0xac health | DoModeSettings*, DefaultInit, damage code | [dec, PS2] |
| 0x094 | 4 f | startHealth | | DefaultInit | [dec] |
| 0x098 | 1 | bulletAccuracy | 0xb4 | behaviour stats | [PS2] |
| 0x099-0x09e | 1 each | stats | 0xb5 baseAggressionLevel, 0xb6-0xba | behaviour stats | [PS2] |
| 0x0a8 | 1 | dtypeBase | 0xc4 | GetDTYPENEW(class, 0) | [dec, PS2] |
| **0x0a9** | 1 | **dtype** | 0xc5 | active type: indexes DroneTypeSettings; 30 = bot everywhere in the SM code | [dis, PS2] |
| 0x0aa | 1 | dtype2 | 0xc6 | type used after the switch to behaviour 2 (ChangeToAttackMode copies it into +0xa9) | [dec, PS2] |
| 0x0ab | 1 | behaviourClass | 0xc7 | key15 low byte: 0-9 soldier, 10/11/13/14 civilian kinds, 12 ally, 15 ninja, 16 bot | [dec, PS2] |
| 0x0ac/0x0ad | 1 | behaviour1Mode / behaviour2Mode | 0xc8/0xc9 | from the behaviour block | [dec, PS2] |
| 0x0b0 | 4 | initialisedOnFrame | 0xcc | PostLoad_Init | [dec] |
| 0x0bc | 2 | skinClass | 0xd8 | DefaultInit skin switch (0-0x18) | [dec, PS2] |
| 0x0be / 0x0c0 | 2 | animSet / savedAnimSet | | weapon stance table index [inf] (area B) | [dec] |
| 0x0c4 | 4 | skinHashcode | | key0 (or the captain skin) | [dec] |
| 0x0c8-0x0e0 | 4 f | combat ranges | | key14 | [dec] |
| 0x0e4 | 4 f | damageScale | | 1.0, 2.0 for some weapons, DroneCaptain_Mod_BulletDamage for captains | [dec] |
| 0x0e8 | 4 | stateTimeoutFrame | | `NDrone2_SetIdleTimeOut(dcv, min, rand)` = now + (min + Rand(rand)) s -> PreDroneControl -> msg 4 | [dec] |
| **0x0ec** | 0x28 | **SM** | 0x108 | section 3.1 | [dis] |
| 0x114 | 4 | processFunction | 0x130 | | [dis] |
| 0x118 | 1 | waitSwitchChannel | | key3 | [dec] |
| 0x119 | 1 | modeChangeSwitchChannel ("associatedSwitchChannel") | 0x135 | key6 | [dec, PS2] |
| 0x11a/0x11b | 1 | initial switch states | | | [dec] |
| 0x11c | 2 | mode | | key5 | [dec] |
| 0x11e | 2 | secondaryMode | 0x13a | key8 | [dec, PS2] |
| 0x120 | 1 | deathSwitchChannel | | key12 | [dec] |
| 0x124/0x128 | 4 | group | | | [dec] |
| 0x130 | 4 f | (reset to 1.0 on every state change) | | [inf: anim speed] | [dis] |
| 0x138-0x144 | 4 f x4 | distance to player n | | PreDroneControl | [dec] |
| 0x148 | 4 | opponent | | area B | [dec] |
| 0x2fc | | collision block | | obj->maybeCollision | [dec] |
| 0x3d4 | 4 | currentBehaviour (points at +0x3d8 or +0x3e4) | 0x4d8 | | [dec, PS2] |
| 0x3d8 / 0x3e4 | 12 each | behaviour 1 / behaviour 2 property bits | 0x4dc / 0x4e8 | behaviour_util_* (area B) | [dec, PS2] |
| **0x3f4** | 4 | **flags** | 0x4f8 | 0x100 active (AI runs), 0x200 dead, 0x400 dying, 0x10 frozen/disabled, 0x80000 animate, 0x100000 anim updated, 0x20/0x80 cleared at death [inf: targetable/collidable], 0x1000000 keep health, 0x10000/0x2000 cleared per state, 0x4000000/0x40000000 start attacking. 186 functions touch it (A3) | [dec] |
| 0x3f8 | 4 | flags2 | | | [dec] |
| 0x3fc / 0x400 | 4 f | base alertness (behaviour 1 / 2) | 0x500 / 0x504 | mode table or behaviour | [dec, PS2] |
| 0x404 | 4 f | previous alertness | | PreDroneControl | [dec] |
| 0x408 | 4 f | alertness ("cumulativeAlertnessScaled") | 0x50c cumulativeSoundAlertness | 1.0 = starts alerted | [dec, PS2] |
| 0x418 | 1 | alertStatus | | Drone_AlertStatusSet | [dec] |
| 0x440/0x444 | 4 | anim callbacks | | Drone_Control | [dec] |
| 0x450 | 4 | playScript | | key1 | [dec] |
| 0x492 / 0x494 | 2 / 4 | animEndState / animEndMsg | | DroneAnim_CallAnim -> DroneAnim_SetEndAIState | [dec] |
| **0x49e** | 2 | **initialState** | 0x5a2 | type settings; DSTATE_Global Enter uses it; also rewritten by many states as the "home" state | [dec, PS2] |
| 0x4a0 | 2 | secondState | 0x5a4 | state after ChangeToAttackMode | [dec, PS2] |
| 0x634 / 0x6d8 | 0xa0 each | AI routes | | area C | [dec] |
| 0x840-0x858 | | weapon ranges, grenades, death script | | | [dec] |
| 0x874 | 0xa4 | diVars | 0xc00 | own copy of the creation vars | [dec] |
| 0x918-0x92c | | timers A and B | | 3.1 | [dec] |
| 0x930 | 4 | stateScratch | | | [dec] |
| 0x974 | 4 | botVars | | MP only (area C) | [dec] |

`NPCGlobals` (0x1e5630, 0x142c bytes) fields used by this area: +0x4 NDrone2List, +0x10[32] group heads, +0x8c[32]
spawners by group, +0x108[32] u16 group counts, +0x14c max dead bodies (6), +0x150..+0x15c dead-body bookkeeping,
+0x17c..+0x194 per-side counters, +0x19c hostagesSaved (ours), +0x1a4 blank spawner, +0x230 NumDrones,
+0x240 alert record (extraData of msg 0x15), +0x288 cover nodes, flag0 and Drone_SomeDurationFrames (named in Ghidra).

---

## 5. Modes and types: how a drone gets its first state

Decoded tables: **A2-mode-and-type-tables.md**.

Two layers [dec, PS2, bytes]:

* **DMODE** (level key5, 36 values when < 0x24): `DroneModeSettings` 0x1776f8, 12-byte entries
  {u8 dtype, u8 side, float alertness, init fn}. `NDrone2_DoModeSettingsOLD` 0x414d0 sets +0x31 = +0x32 = 1, the
  alertness (+0x3fc, +0x408), an empty behaviour, +0xa9 = dtype, +0xaa = the dtype of key8's mode (unless key8 is
  0/101 or key6 is set), side, runs the mode's init function (behaviour property bits), then a fixed set of
  properties. key5 >= 0x24 -> `NDrone2_DoModeSettingsNEW`.
* **Behaviour-driven (NEW)**: `NDrone2_DoModeSettingsNEW` 0x41230 reads the behaviour block (`behaviour_util_get`),
  sets alertness from property 0x20 (0 / 0 / 0.66 / 1.0), class/modes (+0xab..+0xad), then
  **`NDrone2_GetDroneTypeAttackTypeFriend`** 0x3fe30 computes the three DTYPEs with **`NDrone2_GetDTYPENEW`** 0x3fae0
  (class x behaviour mode -> DTYPE: e.g. class 12 -> Mayhew 0xc or Zoe 0x11, class 15 -> Ninja 0xd, class 16 -> Bot
  0x1e, mode 2 -> Sniper 2 / SniperAlert 0x19, mode 3 -> RunToPoint 7 / AlarmRaiser 0xe, mode 4 -> HostageKiller 4,
  mode 5 -> DeleteMe 0x15), then stats from `behaviour_util_getStats`. Special play-script hashes force DTYPE 0x17
  (TruckDriver) or 0x18 (CastleChatGuard).
* **DTYPE** -> `DroneTypeSettings` 0x1778a0, 12-byte entries {s16 initialState, s16 secondState, init fn, control fn}.
  `initialState` goes to +0x49e. **-1 means computed** (types 0 Normal, 3 Assassin, 11 MissionFailer, 20 Interogator,
  26): 6 InitPatrol if behaviour property 0x24 or 0x25 (patrol) is set, else 4 Idle. The code also has cases for
  Sniper, HostageKiller, RunToPoint, Ambush, CivilianGuard, CivDoorGuard, CastleChatGuard, but those types have
  explicit table entries, so the cases never run. If the drone starts alert (+0x408 >= 1) the state is promoted
  [dec 0x3fe30]: {4, 5, 6, 0x30, 0x31, 0x36, 0x56} -> 0x56 Attack; {0x10, 0x12, 0x2e, 0x32, 0xb5} -> 0x12
  CivilianScared; 0x29 -> 0x2a SniperAim; 0xa6 -> 0xa7 NinjaAttack. Skin class 0xb (0x0500001e) with alertness 1
  goes straight to attack mode (`NDrone2_ChangeToAttackMode`, health 100).
  `secondState` (+0x4a0) comes from the second DTYPE. The type's init function runs (Sniper 0x411f0 and SniperAlert
  0x41200 set flag 0x10; Bot has an empty stub on the Xbox, `initDTYPE_BotInit` on the PS2).
* `NDrone2_DoTypeSettingsOLD` 0x40430 is the same second half for OLD modes (called at the end of DefaultInit).
* `NDrone2_ChangeToAttackMode` 0x397f0 (mode switch, alarms): dtype = dtype2, Sniper -> SniperAlert (init runs) and
  Attack, key5 == 100 -> behaviour 2; returns the state to enter (+0x4a0).

Examples (initial state, from the tables): Normal -> Idle or InitPatrol; Guard -> 6 InitPatrol; Attacker -> 0x56 Attack; Hostage -> 10 Hostage;
Civilian -> 16 CivilianInit (-> Civilian / CivilianPatrol / CivilianMission / PartyGirl); CivilianScared -> 0x12;
Mayhew -> 0x1c AllyLeadInit; Zoe -> 4 Idle with control fn; Ninja -> 0xa6 NinjaStand; PartyGirl -> 0x2e;
TruckDriver -> 0x32 (via Global); SniperAlert -> 0x2a SniperAim; Astronaut (DTYPE 29, set by DefaultInit for skin
class 0x18) -> 0xbd AstronautLaunch; Bot -> 0xc3 BotInit (via Global); DeleteMe -> 0xb4.

---

## 6. Function inventory (summary; full list in A4)

| group | functions | bytes | ours |
|---|---|---|---|
| state machine core | 13 (ProcessStateMachine, SM_Init/InitObject/SetState/RouteMsgDCV/RouteMsg/SendMsgSelf/BroadcastMsg/SendDelayedMsgs, SendMsg, 3 statics) | ~1.8 KB | 0 |
| lifecycle / control | 37 (Drone_Create ... Drone_Delete, spawners, DefaultInit 0x12a0 bytes, ControlSTANDARD, Pre/Post) + 3 type control fns not defined in Ghidra | ~12.7 KB | Drone_Create, Drone_DCVfromOBJ |
| mode / type set-up | 37 (DoModeSettingsOLD/NEW, GetDTYPENEW, GetDroneTypeAttackTypeFriend, DoTypeSettingsOLD, 22 init fns, behaviour_util_*) | ~7.3 KB | 0 |
| state handlers | 238 distinct (250 slots) | ~86 KB | 0 (`NDrone2_DSTATE_HostageDead` exists in src as a NOAUTOINJECT stub) |

Unnamed Xbox functions identified in this session (PS2 bodies compared): 0x3fae0 = NDrone2_GetDTYPENEW (custom
convention: class on the stack, ESI = behaviour mode, EDI = drone), 0x3fe30 = NDrone2_GetDroneTypeAttackTypeFriend
(inlines GetDTYPENEW once), 0x40430 = NDrone2_DoTypeSettingsOLD, 0x31f20 = Drone_BulletHit, 0x308b0 =
DroneSpawner_DroneDelete, 0x1a660 BOT_setOtherPlayerInfo, 0x3ac10 NDrone2_DoTracking, 0x3c430 NDrone2_HandleTalking,
0x454e0 DroneMove_SetBoundryFlags, 0x437f0 DroneMove_NoBunching, and the init_DMODE_* functions (A2).
Ghidra's `NDrone2_init_DMODE_Defaults` name is on two functions: 0x40600 is the real Defaults, 0x408a0 is a thunk to
it that the table uses for Normal/Guard/Retreater/Attacker/Assassin/Interogator (PS2 `init_DMODE_Normal`).

---

## 7. Proposed reimplementation order

Principles: the state functions are the bulk (86 KB) but each one is small and only talks to the drone through a
few dozen helpers, so the plan is to own the frame (the skeleton) first, with every state still original, and then
move states over in groups. Keep test harnesses in `src/action/devtools/` (per the keep-game-logic-separate rule).

### Step 0: types and tables (no behaviour change)
1. Fix `NDrone2.h`: the four enum names (3.5); add `DMODE` (36) and `DTYPE` (31 + bot rows) enums from A2.
2. Replace `maybeSAnimSkin` with `uint32_t keys[33]` (names from `level_tag_Drone`), type `DCVars_tag.aiStateMachine`
   as `StateMachineInfo_tag *`, add `StateMachineInfo_tag` (3.1) and `MsgObject` field names (3.2), grow `Drone_tag`
   field by field from A3 with `static_assert(offsetof)` for every named field (the `Drone_tag` in Drone.h today only
   places +0xec and +0x119).
3. **Generate, don't hand-write**, the three tables with a tool in the style of `tools/uihandler.py`:
   `tools/drone_tables.py` reading the XBE (and the PS2 names from the PS2 build) and emitting
   `NDrone2_StateFuncs[250]` (PS2 function names, folded Xbox entries noted), `DroneModeSettings[36]` and
   `DroneTypeSettings[85]` initialisers. Until a state is ours its entry is `XBE_FN(0x...)` (the original address); the
   generator takes the set of reimplemented names from the source (grep AUTOINJECT) so the table switches entries over
   as they land. The same tool can emit the message-id enum.

### Step 1: state-machine core (leaves first; all shadow-testable)
| # | function | test |
|---|---|---|
| 1 | 0x3a3f0 resumable-state predicate, 0x4e1b0 find-by-id | shadow over all 250 states / a populated list |
| 2 | `Drone_SM_Init`, 0x4e3c0 queue insert, `Drone_SM_SendDelayedMsgs` | shadow: run both on a copy of the pool, compare the pool and list bytes |
| 3 | `Drone_SM_SetState` | shadow: random (state, param) on a scratch SM, compare the 0x28 bytes; bot branch needs an MP replay |
| 4 | `NDrone2_ProcessStateMachine` | trivial; must keep the `< 0xfa` bound |
| 5 | `Drone_SM_RouteMsgDCV` (ESI convention: needs an asm thunk for the original callers, as with other custom conventions) | shadow with instrumented state functions: record the sequence of (state, msgType) calls both implementations make for scripted SetState requests; compare SM, obj->curState, +0x3b/+0x3d/+0x130/+0x3f4/+0x918/+0x924 |
| 6 | `Drone_SM_RouteMsg`, `SendMsgSelf`, `BroadcastMsg`, `Drone_SM_SendMsg`, `Drone_Message`, `DroneAnim_SetEndAIState` | same recorder |
| 7 | `Drone_SM_InitObject` | shadow on a freshly created drone |

### Step 2: creation and set-up (deterministic given the placement: shadow-test per level)
Order: `NDrone2_CreateObj` -> `NDrone2_CreateFromDIVars` (note `Rand_Rand`: save/restore the RNG state around the
shadow) -> `GetDTYPENEW` -> `GetDroneTypeAttackTypeFriend` + helpers 0x3fd20/0x403b0 -> `DoTypeSettingsOLD` ->
the 22 init_DMODE_* functions -> `DoModeSettingsOLD` / `NEW` -> `NDrone2_DefaultInit` -> `NDrone2_PostLoad_Init`,
`NDrone2_SetupGroups`, `DroneSpawner_*`, `Drone_PostLoad_Init`, `Drone_PreLoad_Init`, `Drone_LevelReset`.
**Shadow test**: for every drone placement of every level bundle (tools/level can list them), run the original and
ours on the same DIVars and compare the whole 0x978-byte Drone_tag (masking pointers to allocations: +0x10, +0x6c4,
+0x768, anim/route pointers) plus the obj fields. This is the most valuable test in the area: DefaultInit has ~150
branches driven only by data, and the level data covers them.

### Step 3: per-frame skeleton (needs replays)
`NDrone2_PostDroneControl`, `NDrone2_PreDroneControl` (shadowable per frame on a snapshot), then
`NDrone2_ControlSTANDARD`, the three type control functions, `Drone_Control`, `Drone_InitComms` (its callees stay
original for now), `Drone_Delete`, `NDrone2_Enable`, `NDrone2_SetAsDead`, `DroneFunc_DeadDrone`. These call into
perception/movement/weapons (areas B/C), so compare with **replays**: drive a level with tools/drive_game.ps1, log per
frame for every drone {SM block, +0x3f4, health, position, obj->curState, messages delivered}, and diff original vs
ours (the `Drone_SM_RouteMsgDCV` recorder from step 1 gives the message log for free).

### Step 4: state handlers, in groups
Each handler is a pure function of (drone, obj, msg) plus the helpers it calls, so a **shadow harness can run both
versions of one state on a snapshot of the drone** (copy Drone_tag + obj + SM, call original with the message,
restore, call ours, compare) for every message the game actually delivers - hook `NDrone2_ProcessStateMachine`
during a replay and shadow every call. Helpers with side effects on other objects (sound, anims, messages to other
drones) need to be stubbed/recorded in the harness, or the group needs a replay instead.
Suggested order (small and self-contained first, then by how often they run):
1. trivial and shared: DeleteMe, JustStand, WaitForever, HangUp, StandBlind, Tester1-4, the folded Bot entries
   (0x61470 etc.), SeenDroneShot, HostageDie, AllyLeadDone/FollowDone.
2. entry states: **Global**, WaitSwitch, Disabled, PlayScript, Idle, Alert, InitPatrol, Patrol (every drone passes
   through these; they also validate the section 5 set-up).
3. reaction and death: impacts, stun/taser/smoke, surrender, knock-out, Death_Anim ... FadeFast (63-85) - most states
   forward to `DroneFunc_HandleImpact`, so own that helper in the same step.
4. civilians/hostages/allies (8-40, 46-53, 62), snipers (41-45), castle/interrogation (54-61).
5. combat, cover and seen/heard (86-165) together with area B's perception and combat helpers.
6. ninja (166-179), astronauts (189-194); bots (195-249) with area C.

### Things to watch
* Custom conventions: `Drone_SM_RouteMsgDCV` (dcv in ESI), `NDrone2_GetDTYPENEW` (ESI, EDI). Their original callers
  need thunks until the callers are ours too.
* `Drone_SM_SetState(…, 0, …)` is a no-op and the 1000-change guard does not exist on the Xbox: keep both.
* Several states compare against state numbers as ranges (msgType - k, `0x53 <= s <= 0x55`, `0xdd..0xdf`), so the
  enum values must not move.
* DefaultInit's skin switch and level-hash special cases (EvilBase, Tower2A, CastleIndoors1/2, Tower levels, Castle
  exterior weapon 0x16) are data, not bugs: keep them.
