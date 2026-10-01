# Collision, rigid bodies, camera, and the animation runtime

This note covers four engine areas of the action engine (`default.xbe`) ahead of reimplementing them: the collision
system (`engine.collision`, 0x288e0-0x2cf00), the rigid-body code (`engine.physics`, `RB_*`, 0xc0020-0xc1190), the
cameras (`engine.camera`, 0x24000-0x26100), and where the animation runtime stands. It was reviewed from the Xbox
decompile and disassembly, with the PS2 build's symbols for names. All addresses are Xbox ones.

Evidence is marked in place: **(decompile)**, **(disasm)**, **(xrefs)** for what was read in the XBE, **(PS2)** for a
PS2 symbol match, and **(inference)** for reasoning not checked against the code or in play. Names marked
"(invented name)" are not in any build.

The collision *data* (the `COLLDATA_tag`/`COLLBOX_tag` layout, quantised vertices and planes, the surface byte,
the load-time widening bug, positioning with `RotTransMatrix`) is already documented on branch `blender-exports` in
`docs/level/spec-world.md` sections 4-5 and is not repeated here. This document is about the code that uses it.

## Status at a glance

From the coverage run (`tools/function_coverage.py`, R = ours, D = unreachable now, L = live original):

| subsystem | functions | ours (R) | dead (D) | live original (L) | ours |
|---|---|---|---|---|---|
| engine.collision | 42 | 5 | 0 | 37 | `Coll_ResetHitHeap`, `Collide_FreeHitList`, `Coll_GetFreeHit`, `Collide_RayTriangle`, `Collide_LineOfSight` |
| engine.physics | 19 | 0 | 0 | 19 | none |
| engine.camera | 37 | 7 | 0 | 30 | `Camera_SetUpdator`, `Camera_CalcViewAngles`, `Camera_ScreenCoords`, `Camera_Create`, `Camera_CreateCameras`, `Camera_Update`, `Camera_UpdateAll` |
| engine.anim | 79 | 3 | 1 | 75 | `AnimSkeletonProcess`, `AnimObjectSetSleeveType`, `AnimProcessSkinData`; `AnimSleeveGetEntity` is dead |

Our code: `src/action/engine/Collide.cpp`/`.h`, `src/action/engine/Camera.cpp`/`.h` and `viewer.h`,
`src/action/engine/Anim.cpp`. By bytes, collision is 3% ours and the 5 replaced functions are the hit-pool plumbing
and two leaves; none of the query or intersection code is ours.

## 1. Collision

### 1.1 Shape of it

Collision is a query system over two kinds of geometry - static cels (rooms and static objects, each with a
`COLLDATA_tag` box tree) and dynamic objects (either their glist's `COLLDATA_tag`, or per-bone boxes for animated
characters) - with the portal graph as the only spatial index above the per-mesh trees. There is no world-wide grid
or BSP. A query is a `HITTEST_tag` (0x8c bytes) describing a moving volume; the result is a doubly linked list of
`HITDATA_tag` records (0x50 bytes) sorted by distance.

```
query (HITTEST_tag: kind, ray/sphere/capsule, start cel, mask, ignore objects)
  Collide_Pick 0x2ba00                                   broad phase
    Collide_StraddleCels 0x2b910  BFS from test->cel through every portal the volume touches (Intersect_Portal)
    per reached room: its static sub-cels  (bbox / sphere test by kind)      -> candidate hit (cel)
                      its objects          (Collide_PickObj 0x2aa80, sphere)  -> candidate hit (obj)
                      the room itself, if it has collision                    -> candidate hit (cel), dist = i*0.1-1000
    ForcedList objects (straddling more than one room)  Collide_PickObj
  Collide_Intersect 0x2be10 per candidate               narrow phase, in the geometry's local space
    kind 0x101 sphere   -> Intersect_PointGeom 0x29630   (box tree via FUN_000291c0, sphere-vs-box recursion)
    kind 0x201 ray      -> FUN_00029fa0                  (nearest front-facing triangle; tree via FUN_000292e0)
    kind 0x800 capsule  -> Intersect_CylGeom 0x2a2c0
    kind 0x2000 gather  -> Intersect_RayGeom 0x29a30     (copies triangles near a segment into TriHeap)
    animated object without collData -> Collide_Jointy 0x2acd0 (bone boxes, or capsule vs capsule)
    Intersect_CheckIfFlagsMatch 0x28a50 discards by surface type / flags (dist = 1e8)
  Collide_Sort 0x295a0                                   drop dist == 1e8, QuickSort by distance, relink
```

**(decompile)** for every arrow. The kind is the u16 at `HITTEST+0x80`; its high byte doubles as shape flags read
by `Collide_Pick` (0x01 sphere from `pos`, 0x02 ray with length, 0x08 segment, 0x10 and 0x20 also seen).

### 1.2 Structures and globals

| item | address / size | what it is |
|---|---|---|
| `HITTEST_tag` | 0x8c | the query. +0x00 scratch plane (reused by the narrow phase), +0x10 accumulated push-out vector, +0x1c ray start, +0x28 ray end, +0x34 ray delta, +0x40 `pos`, +0x4c sphere centre, +0x58 sphere radius, +0x5c owner (an `obj_tag*`), +0x60 firing object (ignored), +0x64 second ignored object, +0x68 matrix for the current geometry, +0x6c start cel, +0x74 the geometry's `COLLDATA_tag*`, +0x78 radius/width, +0x7c damage, +0x80 kind, +0x84 mask (u16), +0x86 flags (u16), +0x88 result flags. Layout from Ghidra's struct plus field use **(decompile)**; several fields still unnamed |
| `HITDATA_tag` | 0x50 | one hit. +0 prev, +4 next, +8 damage, **+0xc distance (sort key; 1e8 = 0x4cbebc20 means "no hit")**, +0x10 plane, +0x20 unknown vector, +0x2c direction, +0x38 position, +0x44 surface byte, +0x45 flags (1 = mirrored hit, 2 = see-through), +0x46 bone index, +0x48 cel, +0x4c object. Our `Collide.h` calls +0xc `unknown1f` - it is the distance |
| `HitHeap` | 0x1ddc60 (`LLISTINFO_tag`, 0xc) | free list of `HITDATA_tag`; grown 0x40 at a time by `Coll_GetFreeHit`, refused past `HitAllocCnt` > 1000 (0x1dec24) - so at most 1,024 hits exist |
| `ColBoxs` / `BoxCnt` | 0x1dc9a0, 8-byte entries / 0x1ddc6c | leaf boxes found by the tree walks. Room for 600 entries before `HitHeap` **(disasm)** |
| `SrtList` | 0x1ddc70, 4-byte entries | `Collide_Sort`'s array; room for about 1,005 entries before `HitAllocCnt` **(disasm)** |
| `TriHeap` / `TriAllocCnt` / count | 0x1dec30 / 0x1dec28 / 0x1dec2c | gathered triangle vertices (12 bytes each). Starts at 100, grows by 100 up to just over 500 vertices; `Collide_unknown` reports at most 166 triangles |
| straddle list | 0x1dec38, 127 x `cel_tag*` | rooms reached by `Collide_StraddleCels` (126 by BFS, plus the start cel appended last) |
| `ForcedList` | 0x1df41c | objects overlapping more than one room; tested by every query regardless of room. Linked through `obj_tag+0/+4` (`llPrev`/`llNext`; Ghidra still calls them `someHitDataMaybe`/`drone`) |
| `GameState+4` | - | a query stamp: portals visited in this query carry it at `portal+0x20` (and their partner) |
| `Ident`, `DeltaP` | globals | per-candidate scratch: "geometry is in world space" and the push-out of the current hit |

Each `obj_tag` carries `hitList` (+0xac) and `maybeCollision` (+0xb0), a pointer to the object's own `HITTEST_tag`
when it takes part in per-frame collision. The player's lives inside its `BLData` at +0x6e4 (so `BLData+0x6f4` is the
push-out and +0x700 the ray start, both read by `Player_CollisionHandler`); a rigid body's at `RIGIDBODY+0x94`
**(decompile)**. The one-shot query wrappers build a `HITTEST_tag` and a fake 0xe4-byte `obj_tag` (Ghidra's
`someHitRelatedThingy`) on the stack, so the broad phase can treat "the caller" like any object owning a hit list.
The PS2's `control_init_collision` is an empty function **(PS2)**.

### 1.3 The queries other code uses

| function | builds | callers (xrefs) |
|---|---|---|
| `Collide_RayIntersect` 0x2c4e0 | kind 0x201, all hits, sorted | 27: bullets (`Bullet_Update`, `Player_WeaponInitBullet`), feet (`PlayerNotDrone_FeetOnPoint`, `Drone_FeetOnPoint`, `DroneInit_Collision` drops each drone 25 m to the floor), `build_FindCel`, `build_PointOnFloor`, the third-person cameras, `Player_LaserPointer`, `Player_CollCreepWall`, `Searchlight_Update`, `Sensor_SetBeam`, `SpaceLaser_Update`, `Script_Interp`, `AnimObjectDraw` |
| `Collide_LineOfSight` 0x2c5b0 (ours) | ray with mask \| 4, flags 0x10 (stop at first) | 17: drone vision (`DroneVision_CanSeeObjectFrom`, `LineOfSightToObject`), auto-aim, radar, lens flare and coronas, `Player_HandleJump`, `Player_SSCrouch`, sensors |
| `Collide_SphereIntersect` 0x2c610 | kind 0x101 (0x100 variant), adds the push-out to the caller's point | `Bullet_Update`, `Bullet_DoTrails`, `Explode_Propagate`, `Pickup_Create`, `Player_Enable`, `Player_Activate`, `build_link_objects_to_rooms`, `Camera_Orbit` |
| `FUN_0002c6f0` | kind 0x800 capsule | `RB_RestingContactRot` only |
| `Collide_unknown` 0x2c760 | kind 0x2000 triangle gather into `TriHeap` | `maybe_psiDrawShadow`, `Collide_PlaceObject` |
| `Collide_PlaceObject` 0x2c8e0 | ray down 3 units, then a gather to check the footprint is flat (planes within 0.96 of the hit normal) | `GT_DeployMiniGun` only |
| `Collide_RayTriangle` 0x2b1b0 (ours) | one triangle; the second vector is a **direction**, not an end point | `Cel_ObjectLeftCel`, `Intersect_Portal` |
| `Intersect_ConeSphere` 0x2b260 | cone vs sphere | `Sensor_InCone`, `FUN_0006b830` |
| `Collide_GetDamageNObjects` 0x2b350 | sums `damage` over a hit list and collects hit objects | `SP_GetHitDamage`, `MP_PlayerKilled`, `GT_Update`, `Monitor_Update` |
| `Collide_FilterBullets` 0x2b3b0 | drops bullet hits whose weapon flags match, re-sorts | `Break_Update`, `Destroy_CollisionHandler`, `Copter_Update`, `FuseBox_Update`, `SP_Hit`, `SP_GetHitDamage` |

`Debris_Create_Loop` 0x2b410 sits in the collision range but is breakable debris (its only caller is `Break_Kill`).

### 1.4 Per-frame collision of moving objects

`control_movement_object_handler` 0x2dd00 runs, in this order **(decompile)**: every dynamic object's update
function (which moves it and fills its own `HITTEST_tag`), deletion of flagged objects, a pass that rebuilds the
world sphere and calls `control_handle_cel_change` for every object marked moved (`rendererType & 0x20`), then
**`Collide_Update(DynamicObjList_FirstObject)`** 0x2cca0, then the moved/cel-change pass again, then re-parenting
of child objects into their parent's room.

`Collide_Update` has three loops:

1. Free every object's hit list; zero its push-out (`HITTEST+0x10`) and point `HITTEST+0x5c` at the object.
2. For each object with a non-zero kind: `Collide_Pick`, then `Collide_Intersect` per candidate (stopping early
   when flags 0x10 ask for the first hit), `Collide_Sort`, and then **mirror** each object hit onto the other
   object's list: a new `HITDATA` with flag 1, `hitObj` = the mover and `damage` = the mover's `HITTEST+0x7c`. This
   is how a bullet's damage reaches what it hit. The mirroring walks the sorted list and stops at the first hit that
   is not see-through (flag 2, surface flag bits, or a random 50% for surfaces with bit 7).
3. Call each object type's `collideFunc` from `control_funcs[type]`: `Player_CollisionHandler` 0xae1c0,
   `Drone_CollisionHandler` 0x3eff0, `Bullet_CollisionHandler` 0x21a00, `Car_CollisionHandler` 0x27d40,
   `RB_CollisionHandler` 0xc0f60, `Destroy_CollisionHandler`, `Explode_CollisionHandler` and others.

The response is not in the collision code: the narrow phase only accumulates a push-out vector in `HITTEST+0x10`
(and moves the test's own copies of its points by it), and each type's handler applies it - the player adds it to
its position, then works out feet, fall damage (frames of falling over 60), ceilings and slopes from the hit list.
Movement is therefore one discrete step per frame with a push-out; no swept time-of-impact search was found.

### 1.5 Cels and portals

- **Room membership**: `control_handle_cel_change` 0x2d3a0 uses `Cel_ObjectLeftCel` 0xd8810 - the segment from the
  last to the new position against each portal quad of the current room (two `Collide_RayTriangle` calls). If no
  portal was crossed and the point left the room's bbox, `build_LinkToRoom` searches again; on failure drones call
  `Drone_FellOutMap`, bullets and effects are deleted, cars deactivated, and anything else is clamped back into the
  old room's bbox.
- **Straddling**: `Controls_StraddleTest` 0x2d2a0 runs `Collide_StraddleCels` with the object's sphere; if it
  touches more than one room the object goes on `ForcedList` (effect flag 0x10000000), otherwise it comes off.
- **Query reach**: `Collide_StraddleCels` limits a query to the rooms its volume reaches through portals. Geometry
  in a room the volume reaches only without crossing a portal quad is not tested (inference from the code: a long
  ray that leaves the room through a wall, not a portal, cannot see the next room).
- **Cameras** follow the same rule: `Camera_CheckLocation` 0x250a0 moves `viewer->someCel` with
  `Cel_ObjectLeftCel`, falling back to `build_FindCel`.

### 1.6 Characters: `Collide_Jointy`

An object with `rendererType & 8` and no `collData` (characters) is tested by `Collide_Jointy` **(decompile)**:

- sphere (0x101) and ray (0x201) queries go against per-bone oriented boxes from `AnimGetCollData` 0x136b0
  (engine.anim): 0x4c-byte entries of a matrix, half-extents at +0x3c and the bone index at +0x48, which ends up in
  `HITDATA.hitBoneIdx` (hit locations for damage);
- capsule (0x800) against another capsule mover is the character-to-character push: segment-to-segment closest
  points, pushing by the full overlap in single player (and never moving the player) and by half in multiplayer.
  Drones push each other only one way, decided by a float at `Drone_tag+0x38c`.

## 2. Rigid bodies (`RB_*`)

**Who uses it:** only placement type 216 RigidBody **(xrefs)**: `RB_Create` 0xc0990 is called only from
`parsemap_create_dynamic_objects`, and `RB_Init` only from `RB_Create`. In the data that means hanging lamps
(`rb_redlamp` etc.), swinging kitchen pans, buckets and one concrete block (objects-dressing.md on
`blender-exports`). Debris, breakables, cars, the GT minigun and the player do **not** use it; cars have their own
code (`Car_CollisionHandler`).

**State** (`RIGIDBODY`, the object's 0x12c-byte extra data; Ghidra's partial struct): +0x00 linear velocity,
+0x0c angular velocity, +0x18/+0x24 linear/angular acceleration, +0x4c orientation quaternion, +0x5c pointer to the
object's matrix, +0x80 restitution (-0.5 free, -0.75 hinged), +0x84 pivot depth (|bbox min y|), +0x88/+0x8c linear
and angular damping time constants, the mode (k2) as `noGravity`, the inertia diagonal and its reciprocals, the mass,
`1/mass` (Ghidra: `windSpeedScale`), accumulated force and torque impulses, the embedded `HITTEST_tag` at +0x94, the
collision sound.

**Per frame** (`RB_Update` 0xc0da0, the type's update function): if under the room's water line (`shearHeight`) on
15 frames of 16, a buoyancy kick and a gas puff; `RB_ApplyWind` (`Env_GetWindVel`, as a force or, when hinged, as
an impulse at the pivot); `RB_EulerEvolve` 0xc0890 with dt = `REC_FRAME_RATE`:

1. `RB_ApplyImpulses`: torque impulse into body space, times inverse inertia, clamped to +-4 per axis, back to world,
   added to the angular velocity; free bodies add force x 1/mass to the velocity.
2. `RB_unknownHelperFunc2`: exponential damping of angular velocity (`exp(-dt/tau)` done with `f2xm1`/`fscale`) and,
   for free bodies, a gravity term through `RB_VecSpecial`.
3. Explicit Euler: velocity += accel·dt, position += velocity·dt (free only), orientation integrated from omega
   (`RB_apply_omega`), then the quaternion to the object matrix.
4. Hinged bodies add a restoring torque from the up vector each frame - a pendulum.

**Contacts** come one frame later through `RB_CollisionHandler`: free bodies `RB_RestingContact` (bullet hits give
an impulse of 0.5 x damage along the hit direction at the hit point plus the collision sound; other movers nudge it;
a push-out reflects the velocity with the restitution), hinged bodies `RB_RestingContactRot` (bullet torque, masked
to the axes the mode allows: mode 1 keeps x and z, 2 only x, 3 only y, 4 only z; and a capsule query to swing away
from the world). Every placement in the data uses mode 1, so free-body code is not exercised by any level.

**Likely original bug:** `RB_Build` 0xc00a0 stores `1/I.y` in the reciprocal's z and `1/I.z` in its y
**(decompile)**. Harmless for the symmetric lamps; kept as it is.

## 3. Cameras

### 3.1 Viewers

A camera is a `viewer_tag` (allocated by `build_alloc_viewer` 0x20610, initialised by `Viewer_Init` 0x24290) in
`glb_viewer[11]` at 0x1f661c. `Camera_CreateCameras` (ours) makes: 0-3 the players' views (only as many as there are
players), 4 a second view of the main world - the **script/cutscene camera**, 5 and 7 each with its own fresh world
and a 2000-unit box cel, 6/8/10 with no world, 9 on the main world (our code comments it as possibly Xbox-only; not checked against the PS2). The roles of 5-10
are not established (inference: 3D HUD and menu items and 2D overlay layers; `Cameras_PostLoad` hangs a sprite on
viewer 5 for three-player splits).

Each frame `Camera_UpdateAll` (ours) calls for each viewer: `Camera_Update` (the per-viewer callback set by
`Camera_SetUpdator`; `Camera_Shear` when under the room's `shearHeight`; random shake decaying by a random quarter a
frame; world-to-view matrices), `Camera_CheckLocation` (room tracking), and `Camera_UpdateGlbVars` 0x24fd0 - which
is the **fade** state machine (0x205: 0 idle, 1 fading alpha 0..255 into 0x203, 2 done), started by
`Camera_SetFade` 0x243b0.

`Camera_Enable` 0x24150 sets the viewer's enabled/draw bytes (+0x29/+0x2a) and, when enabling, allocates three
per-viewer lists whose sizes come from a 3 x u16 table at 0x163a28 (halved in multiplayer); 12-byte entries.
**(decompile)** - what the lists hold is not known (inference: draw lists for the vision pass).

### 3.2 Player camera modes

`Player_PositionCamera` 0xa8a60 (called from the player update) switches on `BLData.camMode` **(decompile)**:

| mode | what | function |
|---|---|---|
| 0, GunImp | first person: head position, pitch from aim, crouch and swim offsets | `Camera_SetToPlayer` 0x24530 |
| 1 PostMPGameThirdPerson | orbit round the head bone after a match, pulled in by a 1.5-unit sphere query | `FUN_00025450` = PS2 `Camera_Orbit` by order (likely) |
| 2 | behind and above, tracking | PS2 `camera_track_XZ_pos_behind` / `camera_track_Y_pos` / `Camera_PosYRot` / `Camera_PosXRot` = `FUN_00024710` / `FUN_000246a0` / `FUN_000245a0` / `FUN_00024610` (likely, by order and body) |
| 3 | free orbit on the aim stick, easing back | `FUN_00024b10` |
| 4 | look at the player from where the camera is | `FUN_00024d40` |
| 5 | chase camera, ray-cast from the player and eased at 1/12 a frame | `FUN_00024d70` |
| 6, 10 | third person with a five-ray boom (`FUN_000247b0`: ray from the head, shortened at the hit, minus 0.1); 10 follows another object | `FUN_00024880` |
| 8 | debug top-down | inline |
| 9, RCMissile, RCCar, GT | remote devices and the minigun: the device's matrix | inline |
| CreepWall | wall-hug, ledge and zipline rails: along the ledge object, or a `ThirdCam` spline | `camera_tracking` 0x25810 |

The PS2 also names `camera_chasecam`, `camera_freechase`, `camera_fixed`, `camera_snake` and `Camera_Reset`; which
of `FUN_00024880`/`24b10`/`24d70` they are has not been settled (sizes differ between the builds).

`ThirdCam` (placement 231) registers a spline in a table at 0x1dc700 indexed by k0; `ThirdCam_GetPos` evaluates
it. `CamSubject` (253) is a photo target, not a camera: a hit by weapon 0x54 (the spy camera) sets its switch
channel.

### 3.3 Effects

- **Shake** `Camera_Shake` 0x25330 (callers `Explode_Create`, `Bullet_DoTrails`): for viewers 0-3, amplitude
  `(1 - (d/2r)^2) * 2r` capped at 10, written to viewer+0x1dc. Our `viewer.h` calls that field `apocalypseEffect`; it
  is the general shake amplitude.
- **Shear** `Camera_Shear` 0x25610: the underwater wobble (a slowly rotating non-uniform scale of 2.5%) and
  `GlobalBlur = 0x91`, whenever the eye is below the room's `shearHeight` (rooms default to -500).
- **Swing** `Camera_ApplySwing` 0x24490 (caller `Player_Update`): sinusoidal roll/pitch/yaw scaled by
  `(viewer+0xf4 - 1)`. +0xf4 is the field we call `projectionScaleZ` (set to 1.0 by `Camera_CalcViewAngles`), but
  `Player_PositionCamera` writes a player float into it every first-person frame (Ghidra: `lensFlareRelated`). Its
  real meaning is open; renaming it is part of reimplementing the swing.

### 3.4 Cutscenes

Scripts drive viewer 4. `Script_CameraStart` 0xc1f00 marks the stream as `SStream_Camera`, clears input, calls
`Camera_PushStates` (each viewer's two enable bytes onto its own stack), disables viewers 0-6, enables 4, raises the
letterbox `Borders` and sends a music event. While it plays, `Script_UpdateCamera` 0xc24b0 (argument in EAX) copies
a 15-float matrix and a field of view from the stream into viewer 4 and recomputes its projection; the matrix comes
from `Script_Interp`'s spline/sequence evaluation. `Script_KillStream` and `P_NIS_Handler` call `Camera_PopStates`;
`C_NIS_Handler` and `Player_Init` call `Camera_Enable` directly. Fades are started by `Script_Run`,
`Script_Update`, `Script_FFwd`, `GameFlow_PushState` and the player's blur/fade code.

## 4. Animation at run time

The animation system (files, sequences, scripts, pose, skinning, faces) is mapped in detail on branch
`blender-exports` in `docs/anim/README.md` (read it with `git show blender-exports:docs/anim/README.md`) and the
`spec-*.md` beside it. In short what is reimplemented and what is not:

- **On `dev`**: 3 of 79 `engine.anim` functions are ours - `AnimSkeletonProcess`, `AnimObjectSetSleeveType` and
  `AnimProcessSkinData` (loading, not playback); `AnimSleeveGetEntity` is unreachable now. `psiBuildMatrixPalette`
  0xdd290 (now filed under `engine.anim` by the coverage tool) is a NOAUTOINJECT stub on `dev`.
- **On `blender-exports`, not merged**: `psiBuildMatrixPalette`, `AnimGetBoneWorldTrans` 0x13070 and
  `AnimFrameCopy` 0x13e50, shadow-tested bit for bit against the original on the space station level
  (`devtools/AnimShadow.cpp`, `common/xbeOriginal.*`). The branch forked from `dev` before `AnimProcessSkinData`
  landed, so `Anim.cpp`/`Anim.h` need a hand merge.
- **Still original at run time**: `AnimObjectUpdate` 0x18690 (script ticking, events, locomotion sets),
  `AnimFrameResolve` 0x160a0 (sampling and blending, root motion), `AnimObjectDraw` 0x16c20 (palette upload and
  draw), facial layers, and `AnimGetCollData` 0x136b0, which feeds collision (1.6).

Unknown at run time, relevant here: how `AnimGetCollData` builds its boxes (which bones, what sizes) and when;
why `AnimObjectDraw` casts a `Collide_RayIntersect` ray (inference: a floor probe for shadows or lighting); and how
root motion and the movement code split the job of moving a character before collision runs - the drone and player
update functions consume root motion, and collision then pushes the result out.

## 5. Well understood, and not

**Well understood (decompile-level, several cross-checked against callers):** the query pipeline and the
`HITTEST`/`HITDATA` roles; the hit pool, sort and their limits; the box-tree walks; cel membership through portals and
`ForcedList`; the order inside `control_movement_object_handler`; the hit mirroring that carries damage; the RB
integrator and who uses it; the viewer table, the fade machine, shake, shear and the cutscene camera hand-over.

**Unknown or uncertain - the risks:**

1. **The narrow-phase maths is unread in detail.** `Intersect_CylGeom` (1,984 bytes), `Intersect_PointGeom`
   (1,024), `Intersect_Portal` (848), `Intersect_RayBox` (816) and `Intersect_AABB_SweptVolume` were only skimmed.
   Player movement feel (step-up, slope sliding, wall sliding, the 0.05/0.2/-0.13 tolerances) lives in them and in
   `Player_CollisionHandler`. These are the functions where a near-miss reimplementation shows as "the player
   catches on edges" or "falls through a seam", and they run hundreds of times a frame.
2. **The `HITTEST` kind/flag encoding is partly guessed.** 0x101/0x100, 0x201, 0x800, 0x2000 are seen with their
   narrow phases; 0x400 and 0x1000 appear in the switches with no caller found; bits of +0x84, +0x86 and +0x88 are
   decoded only where a branch was read. Each caller passes literal masks (8, 0x1d, 0x104, 3, ...).
3. **Xbox names in this range are partly wrong.** Ghidra's `Intersect_RayGeom` 0x29a30 is the triangle *gather*,
   while the actual nearest-hit ray test is `FUN_00029fa0`. The PS2 has `ASH_RecurseBoxesRay`,
   `ASH_Intersect_RayBox`, `ASH_vecutil_Dist2Tri` and `Intersect_CylTriGeom`, kept in a separate code block from the
   rest of the collision (0x109xxx against 0x1eaxxx) - inference: a hand-optimised module ("ASH" unexplained). The
   likely pairing is `FUN_000292e0` = `ASH_RecurseBoxesRay` and `FUN_000291c0` a sphere variant, but it has not been
   checked one by one; `Collide_unknown` is probably PS2's `Collide_Filter...` or another name. Do the PS2 matching
   before naming.
4. **Custom calling conventions.** `Collide_Sort` takes its list in EBX; `FUN_00029370` (EBX/EDI/ESI),
   `FUN_000293b0`/`FUN_00029410` (ESI) and `Script_UpdateCamera` (EAX) likewise **(disasm)**. Each needs a naked shim
   or all its callers replaced together.
5. **Unchecked fixed arrays.** `ASH_RecurseBoxesBox` appends to the 600-entry `ColBoxs` without a bound (overflow
   lands in `HitHeap`); `SrtList` holds about 1,005 pointers while up to 1,024 hits can exist. Neither is known to
   overflow in the shipped levels; a reimplementation should assert (GameCube-check style), not change behaviour.
6. **Likely original bug in `FUN_00029fa0`**: with flags 0x10 the "skip see-through surfaces" test reads the surface
   byte of the best triangle so far (`local_3c`, initially -1, so `surface[-1]` on the first test) instead of the
   triangle being tested. Needs confirming against the disassembly; keep it if confirmed.
7. **Portal-limited queries** (1.5): rays only see rooms reached through portal quads. Whether this is ever visible
   (bullets through thin walls between rooms with no portal) is untested.
8. **Floating-point fidelity.** The original uses x87 with 80-bit intermediates (`float10` everywhere in these
   functions); our build compiles to SSE floats. Collision thresholds (1e8 sentinels, -0.0002, 0.05) and the
   per-frame push-out make small differences accumulate in position over a replay. Expect shadow tests to need
   tolerances, and replays to diverge in time.
9. **RB free-body path never runs in the data** (all mode 1), so it cannot be tested from levels, and `RB_Build`'s
   swapped reciprocals hide there.
10. **Camera unknowns:** the roles of viewers 5-10; the three `Camera_Enable` lists; the field at viewer+0xf4; which
    PS2 camera functions the five unnamed Xbox ones are; `Viewer_Init`'s defaults. `viewer.h` has only part of the
    struct named.
11. **Anim-collision coupling**: per-bone boxes from `AnimGetCollData` decide headshots and limb damage; until that
    function and the pose it reads are ours, character hit detection cannot be moved over in one piece.

## 6. Suggested order

1. **Names and layouts first** (no behaviour change): finish `HITTEST_tag` with offset asserts in `Collide.h`, rename
   `HITDATA+0xc` to the distance and viewer+0x1dc to the shake amplitude, add `RIGIDBODY`, and pair the `FUN_` names
   with the PS2 one at a time (the Ghidra MCP quirks apply). Cheap, and every later step depends on it.
2. **Leaves, shadow-tested per call**: `Intersect_BoxBox`, `Intersect_SphereBox`, `Intersect_PointOOBox`,
   `Intersect_RayBox`, `Intersect_ConeSphere`, `Intersect_CheckIfFlagsMatch`, the triangle unpackers, the
   `FUN_000293xx` distance tests (with shims), `Coll_AddHitToList`, `Collide_Sort` (EBX shim), `Collide_GetDamageNObjects`,
   `Collide_FilterBullets`. Pure functions of their arguments, so the AnimShadow-style wrapper (run both, compare,
   keep ours) applies directly.
3. **The box-tree walks and narrow phases** (`ASH_RecurseBoxesBox` and siblings, `FUN_00029fa0`, `Intersect_RayGeom`,
   `Intersect_PointGeom`, then `Intersect_CylGeom`), shadow-tested with `ColBoxs`/`BoxCnt`/`TriHeap` and the hit
   record snapshotted and compared. Do `Intersect_CylGeom` last: it is the player's movement.
4. **Broad phase and drivers**: `Collide_StraddleCels`, `Intersect_Portal`, `Collide_PickObj`, `Collide_Pick`,
   `Collide_Intersect`, the wrappers, then `Collide_Update` - compare whole hit lists per query in replays.
   `Collide_Jointy` waits for `AnimGetCollData` (or keeps calling the original).
5. **Cameras**, any time after step 1 since they are mostly independent: the fade machine, shake, shear, swing,
   `Camera_Enable`/`PushStates`/`PopStates`, then the player modes and `camera_tracking`. Small, testable by frame
   dumps from the replays, and they unblock reading `viewer_tag` properly for the vision code.
6. **Rigid bodies** last or alongside the objects pass: 19 small functions with one user type, easy to test on a
   level with swinging lamps (shoot them), and nothing else depends on them.
7. **Animation** separately: merge `blender-exports`' three functions first, then `AnimFrameResolve` and
   `AnimObjectUpdate` as docs/anim plans; `AnimGetCollData` before step 4's `Collide_Jointy`.

Add each function's GameCube checks as it lands (docs/gamecube-checks.md); `Coll_GetFreeHit` already carries one.
