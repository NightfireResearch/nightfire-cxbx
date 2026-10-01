# The frame loop and the object framework

This document covers how the action engine (`default.xbe`) runs a frame, how its dynamic objects are created,
updated, collided, drawn and deleted, and the world-object and effect types that plug into that framework. It is
an architecture review ahead of reimplementation, written from the Xbox binary (Ghidra decompiles and disassembly,
read-only), our own source in `src/action`, and the level-data docs on the `blender-exports` branch
(`docs/level/*.md`), which already describe every placement type from the data side and are not repeated here.

Evidence is marked as **verified** (the code was read for this document), **source** (our reimplementation says so
and matches the original), or **inference**. All addresses are Xbox. Names marked "(invented name)" are ours.
Function counts come from the coverage run of 1 October 2026 (`R` replaced by our code, `D` unreachable now, `L`
live original).

## The shape of it

```
Game_Main 0xe8e90 (R): xboxInit* x5, GetPTPData, Graphics_Init_LowLevel, then forever:
  mainloop 0xdd1d0 (R): GS_SetRefreshRate(50|60|override) ; GameFlow_Main
    GameFlow_Main 0x6aca0 (R): frame counters ; state stack switch (boot / load / FMV / fade / driving ...)
      Game_Run 0x6aa90 (R)                         -- logic, skipped while SkipCodeFrame
        Input_Update, Sound_UpdateListeners, MenuManager_Update/Monitor, Mission_Update, MP_Update, Text_*
        UpdateAllShards, Env_Update (wind, UpdateDrops)
        if not paused: SSys_Monitor, Light_Update,
                       control_movement_object_handler 0x2dd00 (L)   <-- the object framework
        Camera_UpdateAll
      Game_Draw 0xdac20 (L)                        -- unless InhibitGameDraw
        psiPreDraw (R: d3dBeginFrame, clear, background movie)
        View_CaptureScene x11 viewers -> 3 object buckets per viewer
        sky, solid bucket, sky, alpha bucket (sorted), shards, drops, bucket 2, sprites, fades
        psiPostDraw 0xdd8e0 (L): screen blur, d3dSwap (present + pacing), SFXUpdate, rumble
```

There is no variable time step anywhere: one call of `GameFlow_Main` is one game tick, and every speed in the game
is expressed per tick through the frame-rate globals (`REC_FRAME_RATE`, `FRAME_RATE_MUL`, ...).

## 1. The frame loop

### 1.1 Entry and the outer loop (verified, source)

`Game_Main` 0xe8e90 (ours, `src/action/main.cpp`) initialises input, graphics, the file system, textures and sound,
reads the PTP data shared with the driving engine, sets up the low-level graphics, then calls `mainloop` for ever.
`mainloop` 0xdd1d0 (ours, `game.cpp`) recomputes the refresh rate every tick (60 when `Graphics_IsPalI()` is true -
despite the name, everywhere except PAL-I - else 50, or `Settings_GetFPSOverride()`), passes it to
`GS_SetRefreshRate` 0x6b040, which fills `VIDEO_FRAME_RATE` 0x17c0f0, `FRAME_RATE_INT` 0x17c0f4, `_FRAME_RATE`
0x17c0f8, `FRAME_RATE_DIV`, `FRAME_RATE_MUL` and `REC_FRAME_RATE` 0x17c104, and calls `GameFlow_Main`.

### 1.2 GameFlow: a 64-deep state stack (verified, source)

`GameStateStack` 0x17bff0 (64 dwords) with `StackIndex` 0x17bfe8 (u16). `GameFlow_GetState` returns the top (0 when
empty); `GameFlow_PushState` 0x6abf0 (L) translates three "request" states into the state actually pushed:

| state | meaning (as read in GameFlow_Main / PushState) |
|---|---|
| 1 | boot: zero counters, `Mem_Init`, `bootup_bootup`, push 2, `Boot_LoadPTPData`, `Reset_MapLoadSettings` |
| 2 | in game; records `LoadTimeStart`; pushes 3 if `ReloadGame` |
| 3 | `ResetMap_Load` 0xbfb60 (the level loader, called every tick until it pops itself) |
| 4 | `Locks_Init`, pop (coverage marks `Locks_Init` dead, so nothing pushes 4 any more: inference) |
| 5 | valid, does nothing (GameCube check) |
| 6 | full-screen movie: pop when it finishes or is skipped |
| 7 -> 8 | request a fade on viewer 6 (`Camera_SetFade`); 8 waits for `glb_viewer[6]+0x205` to clear |
| 9 | launch the driving engine (`psiLaunchDriving` with the PTP data) |
| 10 | `Input_Update`, wait for `CheatInfo`, draw only, no logic |
| 11 -> 12 | timed hold: 12 counts `glb_viewer[6]+0x1fc` down, then pops |
| 13 -> 14 | fade with logic suspended: 14 runs only `Camera_UpdateAll` and sets `SkipCodeFrame` |

After the switch, `Game_Run` runs unless `SkipCodeFrame`; `Game_Draw` runs unless `GameState.InhibitGameDraw`
(set for states 1, 3, 4, 5, 9 by `set_InhibitGameDrawIfRequired`) or `sloflag`. `GameState` is 0x58 bytes at
0x1f6580 and carries the three frame counters: `NumFrames` (every tick), `NumFramesUnpaused` and `VideoFrames`
(only when not paused and not `sloflag`). `NumFramesUnpaused` is the game's clock: object creation times
(`obj+0xc8`), switch-channel timestamps and many countdowns use it.

Pushers of game states include menu handlers (`C_GCPAUSE`, `P_ENDMISSION`, `P_NFMAP`, ...), `Mission_Update`,
`MP_Update`, `SP_Update`, `Trigger_Update` (movie triggers push 6 then 7) and `ResetMap_LevelToLoad`.

### 1.3 Game_Run (verified, source)

Order, from our `Game_Run` (`game.cpp`, matching 0x6aa90): `Input_Update`; return early if `FreezeGame` and
`switch_allowFreeze`; `Sound_UpdateListeners`; if `sloflag` only `Camera_UpdateAll`; then `MenuManager_Update`,
`MenuManager_Monitor`, `Mission_Update`, `MP_Update`, the text updates, `UpdateAllShards`, `Env_Update`. Then:

- **not paused** (`GS_IsPaused(-1)`): `SSys_Monitor`, `Light_Update`, `control_movement_object_handler(0)`,
  `Camera_UpdateAll`.
- **paused**: `Light_Update` only if a script camera is running, `psiDecompressWoman`, `Camera_UpdateAll`.

Pause is `GS_PauseGame` (R): `GameState.SomeAlternatePauseState` plus a per-player flag in `MPGame.players[]`.
There is a second, easily missed caller of the object update: `P_NIS_Handler` 0x82120 (a menu page handler, run
from `MenuManager_Update`) calls `Script_Update(FMVScript)` and then `control_movement_object_handler(0)` itself,
so in-engine cutscenes keep objects moving while the game is otherwise paused (verified call; that the NIS page is
shown while paused is inference). `UpdateAllShards` and `Env_Update` (and with it rain/snow) run even when paused.

### 1.4 Game_Draw (verified)

`Game_Draw` 0xdac20 (L, 1552 bytes) works over `glb_viewer` 0x1f661c (11 viewer pointers):

1. Screen blur decay, `psiPreDraw` (R: first-frame gamma, `d3dBeginFrame`, clear, `maybeStartBackgroundMovie`).
2. `View_CaptureScene` (R) for all 11 viewers: portal visibility, and the object/cel list per viewer split into
   three buckets (counts at viewer+0x14/0x16/0x18).
3. Viewers 0-3 (the split-screen views): sky pass 1, bucket 0 (solid), sky pass 0, bucket 1 (alpha, quick-sorted
   by `Compare_AlphaObj`), `DrawAllShards`, `DrawDrops`, optional `psiClearZ` on viewer 5, bucket 2, then
   `Sprite_BuildList`/`View_DrawSprites` and `psiFadeView`.
4. Viewers 4-9 run the same sequence again, one viewer at a time (overlay views; which is which - weapon,
   HUD, fade - is inference). Viewer 10 is captured but never drawn.
5. `psiPostDraw` 0xdd8e0 (L): full-screen viewport, `psiBlurScreen`, `d3dSwap`, `SFXUpdate`,
   `psiInput_RumbleUpdate`. So the sound mixer is updated after the present, once per tick.

Per object, `View_DrawObjList` 0xda890 chooses the renderer from the object's control flags (section 2.2), not from
its type: bit 0x08 -> `AnimObjectDraw` (skinned); 0x10 -> a stripped `psiDrawParticles` (now the
no-op hook point 0xe0ec0); 0x100 -> `Emitter_Draw`; otherwise `View_DrawObjectGlist`. Cels go to
`View_DrawCelGlist`. There is no per-type draw function.

### 1.5 Who owns timing (verified, source)

Nothing in the game measures elapsed time to decide how far to move: logic is one fixed tick per call, scaled by
the frame-rate globals. Pacing comes from presentation: on the Xbox, `D3DDevice_Swap` waited for vertical blank; in
our build `d3dSwap` (R, `d3dSeam.cpp`) presents through the D3D9 backend with `D3DPRESENT_INTERVAL_IMMEDIATE`, and
`PaceFrame` in `src/common/gfx/d3d9Backend.cpp` sleeps to `g_targetFrameRate`. `timestamp()` (R, now
QueryPerformanceCounter) is used for measurement only: load times, swap accounting, sound and streaming timeouts.

## 2. The object framework (`control`, 0x2cf00-0x2e3f0)

### 2.1 Lists and globals

| global | address | what |
|---|---|---|
| `DynamicObjList` | 0x1df338 | list head used as a fake `obj_tag`; its `nextObject` (+0x14) is `DynamicObjList_FirstObj` 0x1df34c |
| `DynamicObjCount` | 0x1df828 | live object count (ours now) |
| `ForcedList` | 0x1df41c (0xc) | `LLISTINFO_tag` of objects straddling more than one cel; drawn by `View_AddForcedObjects` |
| `control_funcs` | 0x163b98 (0x3fc) | 85 entries x {update, collide, delete}, indexed by `obj->objectType` |
| `switch_channels` (+`_prev`, `_hold`, `_time`) | 0x1df138, 0x1dee38, 0x1df238, 0x1df428 | 256-byte wiring state, see `docs/switch-channels.md` |
| `EffectList` | 0x2237c0 | `DList` of 200 nodes x 0x58 bytes: decals (bullet holes) attached to cels or objects |
| `TheDoors`, `DoorGroupsStates` | 0x1df98c, 0x1df8c0 (0xca) | the door type's private list and per-group state |

Every object is one heap block from `Mem_Malloc(0xe4 + extraSize, 0x0404)`: the 0xe4-byte `obj_tag` followed by the
type's own data, reached through `obj->extraObjectData` (+0xbc, always `obj + 1`). `control_create_object`
0x2dc10 (R) zeroes it, applies defaults (`control_init_object`: display mask 0x1f, flags |= 2, scale 1, tints and
ambient 0xff, `creationTimeFrames = NumFramesUnpaused`), builds the matrix from Euler `rot` or a quaternion, and
**inserts it at the head** of the list. `Control_CreateObjEx` 0x2e320 (L) wraps it with a matrix, glist, parent,
control flags, scale, effect flags, tint and optional `build_LinkToRoom`. 77 functions call
`control_create_object`, 16 call `Control_CreateObjEx` (verified xrefs).

Cel membership is a second doubly linked list threaded through `obj+0x8/+0xc`, and the **cel itself is the
sentinel**: `control_link_object_to_cel` 0x2d200 sets `obj->nextInCel = (obj_tag*)cel` and pushes onto `cel+0x8`,
so `cel_tag` shares the first 0x14 bytes of the `obj_tag` layout (links at 0x8/0xc, decal list at 0x10). Linking
also takes the cel's ambient light, picked by the cel's own switch channel. A reimplementation must keep that
type pun, or replace cels and objects together.

### 2.2 `obj_tag` (0xe4 bytes; `src/action/game/obj/object.h`)

Most of it is typed and `static_assert`ed. What this review adds or corrects (verified unless noted):

| offset | field | finding |
|---|---|---|
| 0x10 | decal list head | `Effect_RemoveSObjs(obj)` walks and frees `obj+0x10` into `EffectList`; header has it as padding |
| 0x54 | previous centre point | `Control_BuildWorldSph` copies `centrePoint` here before recomputing; `control_handle_cel_change` compares the two; header has padding |
| 0xb0 | `HITTEST_tag*` | the object's collision probe; `Collide_Update` writes +0x5c (owner) and zeroes +0x10 (velocity?) each tick |
| 0xcc | `effectFlags` | low 16 bits from the glist (`applyFlagsToObject`); 0x20 = disabled/not collidable (inference from Hurt and Trigger); 0x8000 straddling; **0x10000000 = on ForcedList, 0x20000000 = always treat as straddling**. `object.h`'s `FLAG_IN_FORCEDLIST = 0x30000000` merges the two; `control_delete_object` does test the pair, `Controls_StraddleTest` keeps them apart |
| 0xd6 | control flags | header calls it `renderType`, Ghidra `rendererType`; it is mostly movement/framework flags, below |
| 0xd8 | countdown | set to 2 at creation, decremented each tick by the framework; no reader identified (unknown) |
| 0xda | `flags` | 1 = delete at end of update, 2 = new (first-tick sphere and ambient light), 4 = character lighting, 8 = shadowed character (`View_DrawObjList`) |

Control flags at 0xd6 (verified in `control_movement_object_handler`, `Control_GetObjMatrix`,
`Control_BuildWorldSph`, `control_handle_cel_change`, `View_DrawObjList`, `Control_ObjectReferenced`):

| bit | meaning |
|---|---|
| 0x001 | scale applies to Z only |
| 0x002 | matrix rotation is authoritative, only translation is refreshed from `position` |
| 0x004 | matrix entirely authoritative (not rebuilt) |
| 0x006 (either) | `lastRotation` is re-derived from the matrix each tick (`Mat_GetDir`) |
| 0x008 | skinned: bound sphere from the anim object; drawn by `AnimObjectDraw` |
| 0x010 | particle renderer (stubbed on Xbox) |
| 0x020 | moved this tick: wrap rotation to +-pi, rebuild sphere, re-cel, re-light; cleared by `control_handle_cel_change`. Every mover's Update sets it |
| 0x040 | copy matrix translation back into `position` |
| 0x080 | cel crossing tested on the centre point rather than `position`/`lastPosition` |
| 0x100 | radius is fixed (not taken from the glist); also selects `Emitter_Draw` |
| 0x200 | ignore `scale` when building the matrix |
| 0x400 | "referenced": on deletion, scrub pointers to it from bullets (+0x24, +0x34), explosions (+0x1c) and every hit list (`Control_ObjectReferenced` sets it) |

### 2.3 How a type plugs in (verified)

A type is an `ObjectType` byte (`obj+0xdb`) plus one row of `control_funcs`. There is no constructor in the table
and no vtable: a type's `*_Create` allocates the object, fills its own data, sets `objectType`, the glist and
flags, links it to a room and, where it needs one, adds it to a private registry (`TheDoors`, `GT_Register`,
`SpaceLaser_Register`, `RainBoxRoot`). Creation comes from three places: level placements through
`parsemap_create_dynamic_objects` 0xa4bc0 (R, ours in `engine/parsemap.cpp`), runtime spawning by other code
(bullets, debris, gas, explosions, emitters, muzzle flashes), and scripts (`Script_CreateEntity`,
`Script_AnimStart`).

The 70 populated rows of the table (read from the XBE at 0x163b98; slot 1 = collide, slot 2 = delete):

| type | update | collide | delete |
|---|---|---|---|
| 2 Drone / 17 dead drone | `Drone_Control` 0x31530 | `Drone_CollisionHandler` (2 only) | `Drone_Delete` |
| 3 Player / 18 dead player | `Player_Update` 0xad520 | `Player_CollisionHandler` | - |
| 5 Bullet | `Bullet_Update` 0x23670 | `Bullet_CollisionHandler` | `Bullet_Delete` |
| 15 Explode | `Explode_Update` | `Explode_CollisionHandler` | `Explode_Delete` |
| 26 RigidBody | `RB_Update` 0xc0da0 | `RB_CollisionHandler` | - |
| 28 Door | `Door_Update` | `Door_ReactToHit` | - |
| 33 Destroy | `Destroy_Update` | `Destroy_CollisionHandler` | - |
| 40 Car | `Car_Update` 0x26f50 | `Car_CollisionHandler` | `Car_Deactivate` 0x27fb0 (a 16-byte thunk to 0x26a00) |
| 66 ThirdIcon | - | `ThirdIcon_Update` | - |
| 74 Sub, 77 MiniSub | `Sub_Update`, `MiniSub_Update` | hook point 0xe0ec0 | hook point 0xe0ec0 |
| 27 ScriptPlayer, 35 Creature, 48 DroneSpawner, 54 GT, 59 Copter | update | - | `SP_Delete`, `Creature_Delete`, `DroneSpawner_Delete`, `GT_Delete`, `Copter_Delete` |
| 50 Emitter, 53 MPObject | update | - | hook point 0xe0ec0 |
| 61 AnimObject, 62 DroneAIVolume | - | - | `Discrete_Delete`, `Drone_AIVolume_Delete` |
| 6, 7, 12-14, 16, 21, 25, 29, 31, 32, 34, 36-39, 42, 46, 47, 49, 51, 52, 55-58, 60, 63-65, 67-73, 75, 76, 78-80, 82-84 | update only | - | - |

Update-only types include Bubble (6), Rotor, Particles, Casing, Gas (`gases_smoke`), Effect (`Debris_Update`), anim
debug (21) and 31 (`AnimObjectUpdate`), Sunreflection (25), Trigger, Break, Searchlight, Cloud, SimpleScript,
Flicker, Hurt, Occlude, Leaf, Pickup, LeafGen, Lightning, Ripples, Sensor, Monitor, Switch, Lock, FuseBox, Tree,
SoundTrigger, Swoosh, Hint, MusicTrigger, Corona, BodyGlow, GunImp (71, which `object.h` calls GUNTURRET2),
CamSubject, PCQWorm, Mine, OneSided, Shooter, SpaceMissile, SpaceLaser, DynamicObject, InverterTrigger, Apocalypse.
Types 6, 25 and 31 have no name in our `ObjectType` enum; 1 (GFX), 41, 43-45, 81 have no row (static or
player-handled objects: CreepWall, ThirdCam/Wire, Ladder and Grapple are driven from the player's collision code).

### 2.4 `control_movement_object_handler` 0x2dd00 (L, 1568 bytes; verified)

The heart of the framework; its `char` argument is never read.

1. `Drone_InitComms` (global AI work and delayed messages), `Doors_Calc` (resets `DoorGroupsStates`, propagates
   the moving door's state to its group), `Trigger_Calc` (Bond-moment channels 0x65-0x6e, two level-specific
   channel rules, then a pre-pass over type-2 triggers, below), `maybeControl_Switches_Update` (rooms 0xc047 whose
   switch channel changed swap to their alternate glist).
2. Snapshot channels: `prev = hold`, `hold = switch_channels`. Objects can then compare current against `hold`
   (changed this tick, before anyone wrote) and `hold` against `prev` (changed last tick).
3. **Update pass**: walk the list from the head, saving `next` first; refresh `lastRotation` from the matrix for
   0x006 objects; call the type's update.
4. **Delete pass**, repeated until a pass deletes nothing: objects with `flags & 1` get `Effect_RemoveSObjs` and
   `control_delete_object` (type's delete, free hit list, `MP_objectBeingDeleted`, off `ForcedList`,
   `AnimObjectDelete`, `SP_RemoveObj`, unlink, `Mem_Free`), plus the 0x400 scrub; new objects (`flags & 2`) get
   `Control_BuildWorldSph` and `Lights_CalcAmbientLight`.
5. **Post-move pass** for 0x020 objects: wrap rotation, `Control_BuildWorldSph`, `control_handle_cel_change`,
   `Lights_CalcAmbientLight`, `lastPosition = position` (and `lastRotation` unless matrix-driven); 0x040 copies
   the matrix translation back.
6. `Collide_Update` 0x2cca0 (L): return every hit list to `HitHeap`; for every object with a probe
   (`obj+0xb0`, active count at probe+0x80) run `Collide_Pick`/`Collide_Intersect`/`Collide_Sort`, and **mirror
   each object hit onto the hit object's own list** with the prober's damage (+0x7c); then call every type's
   collide handler, in list order.
7. The post-move pass again, so that collision responses are re-celled and re-lit.
8. Last pass: decrement `obj+0xd8`; reset the anim LOD bytes when drawing; re-link children whose parent
   (`obj+0x1c`) changed cel.

Consequences worth keeping (verified from the code unless marked):

- **Update order is newest first.** Objects are inserted at the head and the walk goes forward, so an object
  created during the update pass is not updated until the next tick. Any reimplementation of the lists must keep
  head insertion, or same-tick switch-channel interactions change.
- **Hit lists are a tick old.** Update functions read `obj->hitList` as filled by the previous tick's
  `Collide_Update`; the collide handlers are the only code that sees this tick's hits. That is why the "collide"
  slot is better read as "after collision": ThirdIcon puts its update there.
- **Deletion is deferred** to the end of the update pass; `Control_DeleteAllObjectsOfType` only sets the flag.
- **Messaging between objects** is, in order of frequency: switch channels (the general wiring, with `_time`
  stamps for ordered multiplexers); hit lists (damage arrives as a hit with `damageAmount`); direct calls found
  by scanning the list by type (`Control_ReturnNextObjectOfType`, `Trigger_Calc`, `Doors_Calc`) or through a
  private registry; `Player_Activate` calling `Door_/Trigger_/SS_/Monitor_/Lock_/SP_Activate` on the player's
  hits; and, for drones only, `MsgObject` messages (`docs/drone/`).
- **Type-2 triggers run early and twice.** `Trigger_Calc` clears their output channels and calls `Trigger_Update`
  with the gate `0x29b2cc` set; the normal update pass calls it again and it returns at the gate, but only after
  decrementing the countdown at data+0x38, which therefore runs at twice the rate for that trigger type.
- `control_handle_cel_change` 0x2d3a0 has type-specific fall-out handling: drones call `Drone_FellOutMap`,
  bullets/Bubble/Effect are deleted, casings stop drawing, cars `Car_Deactivate`, anything else is clamped back
  into the cel's box unless effect flag 0x20.

## 3. World objects and effects as a family

### 3.1 Shared pattern

Nearly every type has the same three-part shape, which `Hurt` 0x6c530 shows in miniature: a Create that calls
`control_create_object(sizeof data, &pos, &rot, NULL)`, copies placement keys (the `level_tag` `param[k]` dwords,
`path`, `pathFrames`, `pathRate`) into its data, sets `objectType`, calls `Control_SetGList`, ORs control flags
(0x44 for a path-driven object) and calls `build_LinkToRoom`; an Update that polls switch channels and its hit
list, moves itself by path (`Spline_Interp`/`KeyFrame_Interp`) or physics, sets 0x020 when it moved and
`flags |= 1` when it is finished; and, rarely, a delete. Speeds are scaled by `REC_FRAME_RATE` or
`FRAME_RATE_MUL`; countdowns are in ticks or in `NumFramesUnpaused`. Volumes are the glist's collision mesh, so
"touched" means "appears in my hit list" (`docs/level/objects-logic.md` section 0.3-0.4).

Effects follow the same pattern but are spawned at runtime, live a few seconds and delete themselves: `Effect_Create`
0x70430 takes a hit record, looks the surface up in `EffectInfo` (6-bit effect type at hit+0x44) and fans out to
`Emitter_Create` (a variable-size object, 0x128 + 0x2c per particle), `Debris_CreateEx` (type 16 objects of 0x20
bytes, gravity and fade), `Gas_Init` (type 14 smoke), impact sounds and a decal node from `EffectList` (oldest
recycled when all 200 are used). Shards (`Break`) and weather drops are not objects at all: they are global pools
updated from `Game_Run`/`Env_Update` and drawn by name in `Game_Draw`.

### 3.2 Inventory by module

| module (subsystem) | funcs | R | D | bytes | character |
|---|---|---|---|---|---|
| control (engine.objects) | 25 | 11 | 0 | 5360 | framework; the update loop, cel change and CreateObjEx are live |
| GameFlow, GS, mainloop, bootup (engine.flow) | 15 | 8 | 0 | 4272 | mostly ours; `PushState`/`PopState`/`ClrStep` and the PTP loaders live |
| Car, GunImp (objects) | 18 | 6 | 1 | 10208 | player-driven vehicle and gun emplacement: `Car_Update` 3408 B, `GunImp_Update` 2336 B; create/activate are ours |
| GT gun turret | 15 | 0 | 0 | 6480 | player-usable turret, scripted anims, `GT_Update` 3120 B, `GT_Track` 1120 B |
| Sub, SSys, Switch, FuseBox, Locks, PCQWorm, Sensor, Monitor | 43 | 13 | 1 | 11632 | Switch, PCQWorm, Sensor all ours; SSys is the 4-slot self-destruct countdown polled from `Game_Run`; `Lock_Update` is split by Ghidra into 7 fragments (0xd1550-0xd1c90) |
| Timer, Trigger, Sound/MusicTrigger, Hint | 24 | 1 | 0 | 5216 | logic glue: `Trigger_Update` 1008 B is a 13-case channel calculator (latch, AND, ordered AND, fan-out, OR, movie) |
| Pickup, rotor | 9 | 2 | 0 | 5632 | `Pickup_Handler` 2048 B, `Pickup_CreateFromSet` (MP weapon sets) |
| Door | 17 | 1 | 0 | 3728 | path follower with groups, locks, auto-close |
| Break, Shatter, Destroy | 14 | 0 | 0 | 3568 | breakables; `ShatterPolyRecurse` builds glass shards in the psi shard VB/IB slots |
| Copter | 11 | 2 | 0 | 3408 | path-flying gunship, `Copter_Update` 1408 B |
| Explode, Mine, Shooter | 10 | 0 | 0 | 2576 | `Explode_Propagate` 1024 B (radius damage, chain reactions) |
| Creature, Env, Hurt/Occlude, MiniSub, SpaceMissile/Laser | 39 | 5 | 0 | 8672 | small; SpaceMissile/Laser all ours |
| Emitter (effects) | 5 | 0 | 0 | 2576 | particle emitters from block 0x30 definitions |
| Effect, Particles | 14 | 0 | 0 | 3008 | impact dispatcher and decals |
| weather, corona, swoosh, DynamicObject, Apocalypse | 20 | 0 | 0 | 5648 | drops/rain boxes (`UpdateDrops` 1168 B), clouds, lightning |
| Debris, Bubble, Gas, Flicker, Leaf, Ripples, BodyGlow | 17 | 0 | 0 | 7744 | `BodyGlow_Update` 1360 B, `gases_smoke` 1248 B, `Flicker_Update` 1040 B |

Totals: `objects` 200 functions, 30 R, 2 D, 168 L (16%, 13% of bytes); `effects` 56, none ours. The object types
whose code is filed elsewhere (Player, Drone, Bullet, RigidBody, ScriptPlayer, SimpleScript, Searchlight, Light,
Ladder/Wire/CreepWall/Grapple, CamSubject/ThirdCam, OneSided, MPObject, AnimObject) are covered by those
subsystems; of their table entries only `rotor_update`, `SP_Update`/`SP_Delete`, `Searchlight_Update`,
`Sensor_Update`, `Switch_Update`, `PCQWorm_Update`, `SpaceMissile_Update`, `SpaceLaser_Update` and `Copter_Delete`
are ours, so 11 of the 70 rows dispatch into our code.

Already in `src/action/game/obj`: `control.cpp` (lists, create, delete, unlink, init, SetGList, type queries),
`car.cpp`, `Sensor.cpp`, `Switch.cpp`, `PCQWorm.cpp`, `SpaceMissile.cpp`, `SpaceLaser.cpp`, `Searchlight.cpp`,
`ScriptPlayer.cpp`, `Player.cpp` (19 functions), `Light.cpp`. Most of the other 60-odd files there are only AUTOGEN
declarations. `Break.cpp` holds a full draft of Break_Update/ApplyDamage/DoBreak/Create marked NOAUTOINJECT, and
`parsemap_create_dynamic_objects` calls the original `Break_Create` by address on purpose: the draft's `Break_Kill`
is a no-op, which leaked shard vertex/index slots (a shared 2048-slot table) until revisiting a segment crashed.

### 3.3 Dependencies

The family depends on: the collision system (`Collide_Update`, `Collide_PickObj`, hit heap) for every
interaction; cels/rooms and portals (`build_LinkToRoom`, `Cel_ObjectLeftCel`); lighting (`Lights_CalcAmbientLight`,
radiators); `Spline_Interp`/`KeyFrame_Interp` and the map's path table; sound (`Sound_Play3D`, the
SoundTrigger/MusicTrigger types); scripts (GT, Copter and ScriptPlayer drive anim streams); input (Car, GT, GunImp
read `Input_Action` directly); the psi layer (shards, particles, glist drawing); and the player and drones, which
read and write the same switch channels and hit lists. In the other direction, the HUD, missions, the drone AI
(doors, alarms, cover) and multiplayer (`MP_objectBeingDeleted`, pickups) all reach into object data by type.

## 4. What is well understood, and what is not

Well understood: the outer loop and the GameFlow state machine (ours); the order of `Game_Run`; the whole of
`control_movement_object_handler`, `Collide_Update`'s structure and the handler table; object creation and
deletion; the switch-channel model; the placement data and every type's keys (`blender-exports` docs).

Unknowns and risks, roughly in order of how much they could hurt:

1. **Order dependence is implicit.** Behaviour within a tick depends on list order (newest first), on the
   channel snapshot taken before the update pass, on hit lists being a tick old, and on the double post-move
   pass. None of this is written down in the code; a port that "tidies" the loop (per-type passes, a vector of
   objects, immediate deletion) will change outcomes in ways that only show up in specific levels. Replay tests
   need to cover trigger chains and multiplexers (ordered AND uses `switch_channels_time` with a 4-tick window).
2. **Frame-rate coupling.** Speeds use `REC_FRAME_RATE`, but many timers count ticks or `NumFramesUnpaused`
   directly (object age, trigger countdowns, Corona's alternate-frame update, Ripples' "interval in 1/60-s
   frames"), and the region default is 50 or 60. The FPS override (`Settings_GetFPSOverride`) therefore changes
   gameplay timing unevenly. Draw code also mutates state (`Emitter_Draw` advances a spin by `REC_FRAME_RATE`
   each time it draws), so decoupling draw rate from tick rate is not free. Not audited type by type.
3. **`obj_tag` gaps.** The meaning of `obj+0xd8` (countdown from 2), `obj+0xd9` (Door sets 0x08, no reader
   found), the unnamed pads around 0x10 and 0x54 (now identified above but not in the header), several
   `effectFlags` bits and the HITTEST layout at `obj+0xb0` (+0x10, +0x5c, +0x7c, +0x80, +0x86, +0x88) are not
   pinned down. `object.h` merges the two ForcedList bits. These are cheap to fix but every reimplemented type
   touches them.
4. **The cel/object type pun** (cel used as list sentinel and as decal-list owner at +0x10) ties the object lists
   to `cel_tag`'s layout. Moving the lists into our memory means moving cel linkage too.
5. **Breakables and shards.** The disabled Break draft shows the risk: resource ownership (psi shard slots,
   `InitShards`, `DrawAllShards`) crosses the object boundary into the platform layer. `Break_Kill`,
   `ShatterPolyRecurse` and the shard pool need reading before any Break/Destroy work.
6. **Stubbed slots.** Sub and MiniSub point collide and delete at the empty hook point 0xe0ec0, as do Emitter's
   and MPObject's deletes; the particle renderer and `Particles_Update` (deletes itself because
   `FUN_000de5e0` is `XOR AL,AL; RET`) are PS2 leftovers. Whether the stubs hide missing cleanup (emitters own no
   heap, but subs fire torpedoes) is unknown.
7. **The NIS path.** `P_NIS_Handler` runs the object update from inside `MenuManager_Update`, before
   `Mission_Update` and the rest of `Game_Run`; when both it and `Game_Run` call the handler in one tick is not
   established. A replacement of `Game_Run`'s ordering must keep this second entry point.
8. **Function boundaries and names in Ghidra.** `Lock_Update` is split into seven fragments by a mis-recovered
   switch; two functions are named `Car_Deactivate` (0x26a00 real, 0x27fb0 thunk) and two `Debris_CreateEx`
   (0xcccf0, 0xcce60); 29 functions in the objects and effects modules are still `FUN_`. These need fixing in
   Ghidra before decompiles are trusted.
9. **Viewer roles.** Which of the 11 viewers is the HUD, weapon, fade or menu view, and why viewer 10 is captured
   but never drawn, is not established; it matters when `Game_Draw` is ported.
10. **Private registries** (`TheDoors`, GT, SpaceLaser, RainBox) duplicate list membership; their unlink paths
    (only GT, Copter, Creature have delete functions) and behaviour on level reset are unread.

## 5. Suggested order

1. **Type the gaps first** (no behaviour change): add the 0x10 decal head, 0x54 previous centre, 0xd8, the split
   ForcedList bits and a `ControlFlags` enum for 0xd6 to `object.h`; type `HITTEST_tag` far enough for
   `Collide_Update`. Every later step needs them, and they are checkable with the existing layout asserts.
2. **The small framework leaves**: `Control_GetObjMatrix`, `Control_BuildWorldSph`, `Control_NextLOD`,
   `Control_ObjectReferenced`, `Control_CreateObjEx`, `control_link_object_to_cel`, `Controls_StraddleTest`,
   `control_handle_cel_change`. They are short, have shadow-testable inputs and outputs, and are called by
   everything.
3. **`control_movement_object_handler` itself**, as a literal port with the passes in the original order, keeping
   the original `control_funcs` table. This puts the frame's object loop in our code (so it can be instrumented
   and replay-tested) without touching any type. `Collide_Update` follows, but belongs to the collision work.
4. **Logic glue next**: Trigger family (`Trigger_Update`, `Trigger_Calc`, Inverter, Sound/Music triggers, Hint,
   Timer), Door, Lock, FuseBox, Monitor, SSys. They are small, deterministic, data-driven and are what level
   progression rests on; the `blender-exports` docs already give every key. Replays that reach them are easy to
   name per level.
5. **Self-contained effects**: Debris, Bubble, Leaf/LeafGen, Ripples, Corona, Swoosh, Cloud, Lightning,
   Sunreflection, DynamicObject, Apocalypse, Flicker. Few dependencies, visual checks only, low risk.
6. **Effect dispatch and emitters**: `Effect_Create` and its fan-out, `EffectList` decals, Emitter, Gas,
   BodyGlow. Needs the `EffectInfo` table typed.
7. **Combat objects**: Explode/Mine/Shooter, Hurt, Pickup (with MP weapon sets), Break/Destroy (after the shard
   pool is understood).
8. **Big controllable types last**: GT, GunImp, the rest of Car, Copter, Sub/MiniSub, Creature. They are the
   largest, read input directly and drive script anim streams, so they should follow the player and script work.
9. **`Game_Draw`** once the viewer roles are known; it is self-contained but long, and the draw-side state
   mutation (item 2 in section 4) has to be kept.
