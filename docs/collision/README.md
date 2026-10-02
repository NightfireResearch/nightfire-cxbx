# The collision system (`engine.collision`)

The action engine's collision code (`default.xbe` 0x288e0-0x2cf00) answers every "what is in the way" question in
the game: bullets and laser beams, feet on floors, drone line of sight, the player's capsule against walls,
characters pushing each other, cameras, shadows and object placement. This folder documents it function by
function, as reviewed from the Xbox decompile and disassembly with the PS2 build's symbols (`/PS2_EU_51258/ACTION.ELF`)
for names, ahead of reimplementing it. It builds on section 1 of
[`docs/architecture/collision-physics-camera.md`](../architecture/collision-physics-camera.md), which gives the
overview and is not repeated here except where this review corrects it (see "Corrections" below).

All addresses are Xbox ones. Evidence is marked as in the other deep docs: **(decompile)**, **(disasm)**,
**(xrefs)**, **(PS2)** for a PS2 symbol or PS2 decompile, **(inference)** for reasoning not checked in play.
Names marked "(invented name)" are in no build; Ghidra names that are wrong are flagged where they appear. Ghidra
was read only - nothing in the database was renamed or retyped for this.

| document | covers |
|---|---|
| this page | the shape of the system, who calls what, what is ours, corrections to the overview, unknowns, the suggested reimplementation order and how to test it |
| [data.md](data.md) | `HITTEST_tag`, `HITDATA_tag`, `COLLDATA_tag`/`COLLBOX_tag` as the code uses them, portals and cels as collision sees them, the query kinds, the mask and flag bits, every global, the calling conventions |
| [functions.md](functions.md) | every function in the range: address, size, PS2 name, status, callers and callees, and what it does, with the quirks a faithful port has to keep |

## Status

From `python tools/function_coverage.py engine.collision` on 2 October 2026:

| | functions | bytes |
|---|---|---|
| in the range | 42 | 17.5 KB |
| ours (`src/action/engine/Collide.cpp`) | 5: `Coll_ResetHitHeap`, `Collide_FreeHitList`, `Coll_GetFreeHit`, `Collide_RayTriangle`, `Collide_LineOfSight` | 0.5 KB |
| still original | 37 | 17.0 KB |
| of which not collision | `Debris_Create_Loop` 0x2b410 (breakable debris, only caller `Break_Kill`) | 0.4 KB |
| dead | none | |

All 37 are live. The only ones our code calls directly are `Collide_RayIntersect`, `Collide_unknown` (from our
`maybe_psiDrawShadow`), `Intersect_ConeSphere`, `Collide_GetDamageNObjects` and `Collide_FilterBullets`; the rest are
kept alive by original game code (players, drones, bullets, cameras, rigid bodies, AI navigation) and by each other.

Original functions outside the range that the collision code depends on and that are **not ours yet** (they would
come along or need shadowing first): `vecutil_Dist2Tri` 0xd82d0 (608 B), `vecutil_Dist2Tri_PointGeom` 0xd80f0
(480 B), `vecutil_calculate_closest_point_on_line` 0xd7d00 (320 B), all engine.math; `QuickSort` 0xbda60 and
`CompHitData` 0xbd8d0 (engine.util); `AnimGetCollData` 0x136b0 (engine.anim); `Debris_CreateEx` 0xcccf0 (effects);
`build_FindCel` 0x20c30 (engine.world). The other vector and matrix helpers it calls (`Vec_*`, `Mat_*`,
`RotPreTransVec`, `DistancePointToPlane`, `auxVec_AddMulR32`, `vecutil_point_on_poly` and so on) are either ours or
trivially portable.

## The shape of it

```
                         ┌──────────── one-shot queries (stack HITTEST_tag + fake 0xe4-byte owner) ─────────────┐
 27 callers ─ Collide_RayIntersect 0x2c4e0 ─┐  (Collide_LineOfSight 0x2c5b0, ours, wraps it: mask|4, first hit)  │
  8 callers ─ Collide_SphereIntersect 0x2c610 ┼─► core driver 0x2c440 (Ghidra: "Collide_CylinderIntersect")       │
 RB_Resting ─ FUN_0002c6f0 (PS2: Collide_CylinderIntersect) ┘    Pick → Intersect per candidate → Sort           │
 shadows,   ─ Collide_unknown 0x2c760 ─ Pick as a capsule, narrow phase as a triangle gather → TriHeap           │
 placement    Collide_PlaceObject 0x2c8e0 ─ RayIntersect down + Collide_unknown, flatness check                  │
                         └────────────────────────────────────────────────────────────────────────────────────┘
 per frame: control_movement_object_handler → Collide_Update 0x2cca0
            1 free every hit list, zero push-outs       2 per mover: Pick, Intersect, Sort, mirror object hits
            3 control_funcs[type].collideFunc (Player_/Drone_/Bullet_/Car_/RB_/Destroy_/Explode_CollisionHandler)

 Collide_Pick 0x2ba00 (broad phase)
   Collide_StraddleCels 0x2b910 ─ breadth-first over portals the volume touches (Intersect_Portal 0x2b5c0)
   per reached room: static sub-cels (bbox or bounding sphere), objects in the room (Collide_PickObj 0x2aa80),
                     the room's own mesh (dist = index*0.1 - 1000, so rooms sort first)
   then every object on ForcedList (straddling rooms)                          → candidate HITDATA list
 Collide_Intersect 0x2be10 (narrow phase, per candidate, in the mesh's local space)
   characters without a mesh   → Collide_Jointy 0x2acd0      (bone boxes / capsule vs capsule, world space)
   0x101 sphere                → Intersect_PointGeom 0x29630  (tree: RecurseBoxesSph 0x291c0)
   0x201 ray                   → FUN_00029fa0  = PS2 Intersect_RayGeom   (tree: ASH_RecurseBoxesRay 0x292e0)
   0x800 capsule               → Intersect_CylGeom 0x2a2c0    (tree: ASH_RecurseBoxesBox 0x29250)
   0x2000 triangle gather      → 0x29a30 "Intersect_RayGeom" = PS2 Intersect_CylTriGeom (tree: ASH_RecurseBoxesBox)
   0x100/0x1000 sphere only    → object distance from bounding spheres, no mesh
   then the surface filter (Intersect_CheckIfFlagsMatch 0x28a50), back to world space, push-out accumulated
 Collide_Sort 0x295a0 ─ drop dist == 1e8, QuickSort(CompHitData) by distance, relink
```

The three layers are cleanly separable, which is what makes a staged port possible:

1. **Leaves** (0x288e0-0x29560 plus `Collide_RayTriangle`, `Intersect_ConeSphere`): pure geometry on vectors and
   boxes, the quantised-mesh unpackers, the surface filter, the box-tree walkers and the hit pool. No game state
   except `ColBoxs`/`BoxCnt` and `HitHeap`.
2. **Mesh tests** (`Intersect_PointGeom`, the ray test, `Intersect_CylGeom`, the gather, `Collide_Jointy`): one
   query against one mesh in its local space, writing one `HITDATA_tag`, `DeltaP` and the test's scratch fields.
3. **Drivers** (`Collide_StraddleCels`, `Collide_Pick`, `Collide_PickObj`, `Collide_Intersect`, `Collide_Sort`,
   the core driver, the wrappers, `Collide_Update`): the portal walk, candidate lists, space changes and the
   per-frame pass.

### How level data feeds it

- Each entity's collision block (map blocks 0x2e/0x2f) is attached to its `celglist_tag` (+8 `colldata`) by
  `parsemap_block_Coll_Data_New` 0xa63f0 (ours, `src/action/engine/parsemap.cpp`), which points a 0x18-byte
  `COLLDATA_tag` into the loaded block without copying. The block format, its validation over every map, the
  quantisation and the load-time widening bug are in `docs/level/spec-world.md` sections 4-5 on branch
  `blender-exports` (not yet on `dev`); [data.md](data.md) restates only what the code needs.
- A static cel's mesh is in the cel's own space: identity when `cel_tag+0x3c & 0x8000000` (every room, and any cel
  with zero position and rotation), otherwise `RotTransMatrix(cel+0x64 rot, cel+0x58 pos)`. A dynamic object's
  mesh is in `obj_tag+0x70 mtx` space.
- Rooms (cels with type bit 0x40000) carry the portal list (`cel+0x28`), their static sub-cels (`cel+0x18`, linked
  through each sub-cel's +0x18) and their objects (`cel+0x8`, linked through `obj+0x8`). Portals come from map block
  0x21 via `build_alloc_portal` 0x20730.
- Characters have no mesh; `AnimGetCollData` 0x136b0 supplies per-bone oriented boxes each time they are tested.
- `Coll_ResetHitHeap` (ours) runs from `ResetMap_GameInit` on every level start: empties the hit pool, reallocates
  a 100-vertex `TriHeap`.

### Who calls what (xrefs)

| entry point | callers |
|---|---|
| `Collide_RayIntersect` | 27: `AIBounds_Link2Cel`, `AnimObjectDraw`, `Bullet_Update`, `Check_Target`, `Collide_LineOfSight`, `Collide_PlaceObject`, `Creature_CalcPoint`, `DroneInit_Collision`, `DroneVision_GunThroughWall`, `Drone_FeetOnPoint`, `FUN_000247b0`/`FUN_00024d70` (cameras), `FUN_00067640`, `GunImp_Update`, `Pickup_Update`, `PlayerNotDrone_FeetOnPoint`, `Player_CollCreepWall`, `Player_LaserPointer`, `Player_MuzzleFlash`, `Player_WeaponInitBullet`, `Script_Interp`, `Searchlight_Update`, `Sensor_SetBeam`, `SpaceLaser_Update`, `Sub_ApplyDamage`, `build_FindCel`, `build_PointOnFloor` (ours: `build.cpp`, `Searchlight.cpp`, `Sensor.cpp`, `SpaceLaser.cpp`) |
| `Collide_LineOfSight` (ours) | 18: drone vision, auto-aim, radar, lens flare, coronas, `Player_HandleJump`, `Player_SSCrouch`, sensors, searchlights, `Sub_DeployTorpedoes`, `GT_Track`, `Pickup_Update`, `Bullet_DoTrails`, `Bullet_acquiretarget` |
| `Collide_SphereIntersect` | `Bullet_DoTrails`, `Bullet_Update`, `Explode_Propagate`, `FUN_00025450` (camera orbit), `Pickup_Create`, `Player_Activate`, `Player_Enable`, `build_link_objects_to_rooms` |
| `FUN_0002c6f0` (capsule query) | `RB_RestingContactRot` |
| `Collide_unknown` (gather) | `maybe_psiDrawShadow` (ours, `psiDraw.cpp`), `Collide_PlaceObject` |
| `Collide_PlaceObject` | `GT_DeployMiniGun` |
| `Collide_Update` | `control_movement_object_handler` 0x2dd00 |
| `Collide_StraddleCels` | `Collide_Pick`, `Controls_StraddleTest`, `View_AddForcedObjects`, `build_LinkDoors2Portals`, `AINetwork_TestRayCels`, `AINetwork_BoundsTest`, `AINetwork_BoundsNodeTest` |
| `Collide_FreeHitList` (ours) | 39 - every caller of a query frees its list |
| `Coll_AddHitToList` | the broad phase, plus `Explode_Propagate`, `NDrone2_DSTATE_BotDeathByExplosion`, `PlayerOrDrone_FeetOnPoint_DoCollision`, which fake hits |
| leaves used outside | `Intersect_SphereBox` (AI nav, MP king-of-the-hill, bot states), `Intersect_BoxBox` and `Collide_RayTriangle` (`Cel_ObjectLeftCel`), `Intersect_PointOOBox` (drone AI boxes), `Intersect_ConeSphere` (`Sensor_InCone`, `FUN_0006b830`), `Collide_GetDamageNObjects`, `Collide_FilterBullets` (damage readers) |

## Corrections to the overview

Points where `collision-physics-camera.md` section 1 is wrong or incomplete, found in this review:

1. **Two Ghidra names are swapped relative to the PS2.** 0x29a30, named `Intersect_RayGeom` in the Xbox database,
   is the PS2's `Intersect_CylTriGeom` (the 0x2000 triangle gather); `FUN_00029fa0` is the PS2's
   `Intersect_RayGeom` (the 0x201 ray test). PS2 `Collide_Intersect` 0x1ec298 calls `Intersect_RayGeom` for 0x201
   and `Intersect_CylTriGeom` for 0x2000 **(PS2)**. The overview's diagram has the right behaviour against the
   wrong name.
2. **"Collide_CylinderIntersect" 0x2c440 is not the PS2 function of that name.** It is the shared driver (Pick,
   Intersect per candidate, Sort) with a register calling convention, which the PS2 inlined into each wrapper.
   The PS2's `Collide_CylinderIntersect(HITTEST_tag*, HITDATA_tag**, signed char, unsigned short)` is Xbox
   `FUN_0002c6f0`; both have the single caller `RB_RestingContactRot` **(PS2, xrefs)**.
3. **`Intersect_CheckIfFlagsMatch`** is the PS2's `Collide_Filter(ushort, ushort, uint, float&)` **(PS2)**.
4. **The portal test for rays uses the infinite line, not the segment.** `Intersect_Portal` calls
   `Collide_RayTriangle` with the ray's direction and ignores the returned parameter, and `Collide_RayTriangle`
   has no range check (decompile, and our port agrees). So a ray reaches a neighbouring room whenever the
   line through it pierces a portal of a room already reached, however short the ray. The overview's "a ray
   that leaves through a wall cannot see the next room" holds only if no portal lies on the line.
5. **`HITTEST+0x74` holds a `celglist_tag*`, not a `COLLDATA_tag*`** - the mesh tests read `*(+0x74)+8`. For a
   static cel it is copied from `cel_tag+0x38` (Ghidra's `collisionData`), which is therefore the cel's glist.
6. **`ColBoxs` entries are `{COLLBOX_tag *leaf; float t}`**, the `t` written only by the ray walker.
7. **The gather (`Collide_unknown`) picks candidates as a capsule** (kind 0x0800 during `Collide_Pick`) and only
   then switches the kind to 0x2000 for the narrow phase, so it gathers from rooms, static sub-cels and object
   meshes alike, not just rooms.
8. **Collide_Intersect's "first hit" (flags 0x10) is first in list order, not nearest**: the first candidate that
   intersects marks every later candidate 1e8; candidates are in prepend order (ForcedList objects first, then the
   last room's entries...). Within one mesh the early-out is likewise the first acceptable triangle that is closer
   than the best so far.
9. **Character pushes move the mover being tested, not the other one.** In single player `Collide_Jointy` refuses
   to push anything out of the player and pushes the player out of drones at full strength - the reverse of the
   overview's "never moving the player". In multiplayer each side moves half the overlap in its own pass.

## Calling conventions (a porting hazard)

Nine functions take arguments in registers - link-time code generation - so they cannot be replaced one at a time
by a plain `__cdecl` function. Measured by `tools/abi_facts.py` (`tools/abi_action.json`) and checked in the
disassembly:

| function | inputs |
|---|---|
| `Intersect_AABB_SweptVolume` 0x29020 | ECX, EDX, EDI, ESI, EBX: five `_VECTOR*` (three triangle vertices, box min, box max - see functions.md) |
| `FUN_00029370` | stack `float *distSq`; EBX sphere A, EDI sphere B; ESI receives A-B (written, not read) |
| `FUN_000293b0`, `FUN_00029410` | stack `float *distSq`; ESI = `HITTEST_tag*` |
| `Collide_Sort` 0x295a0 | EBX = `HITDATA_tag **list` |
| `Collide_PickObj` 0x2aa80 | EAX = `HITTEST_tag*`; stack obj, owner |
| `Collide_Jointy` 0x2acd0 | stack `HITTEST_tag*`; EAX = `HITDATA_tag*` |
| `Collide_Intersect` 0x2be10 | stack `HITTEST_tag*`, `ushort flags`; EAX = `HITDATA_tag*` |
| core driver 0x2c440 | EDI = `HITTEST_tag*`, EAX = flags (u16); stack `char startCelOnly` |

All pop nothing (caller cleans). `Coll_AddHitToList` 0x29560 is plain `__cdecl` but **returns the new
`HITDATA_tag*` in EAX**, which `Collide_Pick` and `Collide_PickObj` use (Ghidra types it `void`; the PS2 returns it
too). The register-argument functions have to be ported together with all their callers (they are all internal),
or given naked thunks. `Collide_Sort` is also called by the stack-convention `Collide_FilterBullets`, which is
already declared in our code.

## Quirks a faithful port has to keep

Collected here; each is described at its function in [functions.md](functions.md).

- `Intersect_RayBox`'s far limit is `tNear <= HITTEST+0x78` in units of the ray delta, where +0x78 is the ray's
  length, so the broad phase accepts boxes up to length² ray-lengths away (conservative, harmless, but changes
  which leaves are walked and so which triangles are visited first).
- With flags 0x10, the ray, sphere and gather tests skip a triangle when the surface byte of the **best triangle
  so far** has bits 0xc0 set, starting from index -1: the first reads `surface[-1]`, the last byte of the triangle
  array. The gather reads the dword at `surface-4` instead. Intended: the current triangle's surface byte.
- `Intersect_Portal`'s sphere and capsule tests pass the portal plane for both triangles of the quad (the negated
  plane it builds is never used: `(&plane)[i >> 1]` with i in 0..1).
- `Collide_PickObj` overwrites the real test's `sphPos`/`sphRad` with each object's sphere for ray and capsule
  queries, and for sphere queries writes the centre difference into `HITTEST+0`; the capsule path copies the
  stale `HITTEST+0` into the new hit's plane and direction.
- `Intersect_PointGeom` writes the hit position twice; the first, from a still-zero normal, is dead.
- `Collide_Update`'s mirroring loop calls `Rand_Rand(0x3f)` for every hit whose surface has bit 7 - the result
  decides whether the mirroring passes on, and it advances the game's RNG.
- `Debris_Create_Loop` steps triangles by 3 (one debris per third triangle) and ignores the object's rotation.
- No bounds checks: `ColBoxs` holds 600 entries before it runs into `HitHeap`; `SrtList` about 1,005 before
  `HitAllocCnt`; a NULL from `Coll_GetFreeHit` (pool exhausted at 1,024 hits) is written through by every caller.

## Unknowns and risks

- **Floating point.** The originals run on x87 with 80-bit intermediates; our code is SSE. The mesh tests are
  full of threshold comparisons (plane distance against radius, t in [0,1], 0.05/0.0002/0.999 parallel cut-offs,
  `< best`), so ours will occasionally disagree on a boundary case, which can change which triangle wins or whether
  a push-out happens. Shadow comparisons need a tolerance and a "near a boundary" classification rather than exact
  equality; replays that depend on exact positions (feet, slopes) may drift.
- **The order of hits matters.** `Collide_Update` mirrors and stops on the sorted order; `QuickSort` is not stable,
  so ties (equal distances, common for the room entries' fake distances and for zero-distance box hits) must sort
  exactly as the original - keep calling the original `QuickSort`/`CompHitData` or port them literally.
- **HITTEST kinds per object type** are not tabulated: which types use 0x101, 0x201, 0x800 or none, and with
  which masks and flags, is set across many update functions (the player in `BLData+0x6e4`, rigid bodies at
  `RIGIDBODY+0x94`, `Car_Update` writes 0x101). A one-frame census probe over `DynamicObjList` would settle it.
- **Unnamed fields.** `cel_tag+0x84` flags (0x20 no collision, 0x40 see-through, 0x1000 masked by 0x20),
  `obj_tag+0xd9` bits 1/2/4 (owner-ignores-class, and "simple sphere" response), effect flags 0x10/0x20/0x40/0x1000
  as masks, and hit flag 4 are known only by use.
- **`HITTEST+0x88` bit 1** is set by the capsule test when a floor-facing triangle is touched below the capsule's
  end point; that it means "grounded" is **(inference)**: its readers in the handlers were not traced.
- **`vecutil_Dist2Tri`'s region codes** (0 = inside the face, 99 = behind the plane, others edge/vertex regions)
  are read from its decompile, not tested.
- **The 0x400 kind** zeroes every narrow-phase result (dist 1e8) and no 0x400 user was found; it may be dead.
- **PS2 differences**: the PS2 `HITTEST_tag` is 0xa0 bytes and `HITDATA_tag` 0x60 (16-byte vectors); do not take
  offsets from it. `Collide_unknown` has no PS2 counterpart (the Xbox shadow code is Xbox-only).

## Suggested reimplementation order

Sizes are the original function sizes. Each step is shadow-tested before the next starts (see "Testing").

1. **Pure leaves, stack convention (≈2.1 KB, 9 functions)**: `Intersect_UnpackTrianglePlaneEq`,
   `Intersect_UnpackTriangleVerticesToWorld`, `Intersect_CheckIfFlagsMatch`, `Intersect_SphereBox`,
   `Intersect_BoxBox`, `Intersect_PointOOBox` (returns a bool in AL; the high bytes of EAX are FPU status junk),
   `Intersect_RayBox`, `Intersect_ConeSphere`, `Collide_GetDamageNObjects`. Offline shadow tests with random
   inputs, run under `MenuShadowTests=on` like `MatrixShadow.cpp`.
2. **Hit list plumbing (≈0.3 KB)**: `Coll_AddHitToList` (return the hit in EAX), `Collide_Sort` (EBX convention -
   inject with a naked thunk, or wait for step 5), `Collide_FilterBullets`. Test on synthetic lists drawn from the
   real pool; keep the original `QuickSort`.
3. **Box-tree walkers (≈0.4 KB)**: `RecurseBoxesSph` 0x291c0, `ASH_RecurseBoxesBox` 0x29250, `ASH_RecurseBoxesRay`
   0x292e0. Need real meshes: an in-level shadow test (below) comparing `BoxCnt` and the `ColBoxs` sequence.
4. **Engine.math dependencies (≈1.4 KB, outside the range)**: `vecutil_calculate_closest_point_on_line`,
   `vecutil_Dist2Tri`, `vecutil_Dist2Tri_PointGeom`. Shadow them with the math tests; they are what the mesh tests
   spend their time in.
5. **Mesh tests (≈6.7 KB)**: the ray test 0x29fa0, `Intersect_PointGeom`, `Intersect_CylGeom`, the gather 0x29a30
   with `Intersect_AABB_SweptVolume` folded in (register convention, internal), and `Collide_Jointy` (needs
   `AnimGetCollData`). In-level shadow test per mesh on copies of a `HITTEST_tag`, comparing the `HITDATA_tag`,
   `DeltaP`, the mutated test, `+0x88` and `TriHeap`.
6. **Broad phase and drivers (≈4.6 KB, then ≈1.8 KB of wrappers)**: `Intersect_Portal`, `Collide_StraddleCels`, `Collide_PickObj` with the
   three sphere helpers, `Collide_Pick`, `Collide_Intersect`, the core driver 0x2c440, together (they share the
   register conventions). Then the wrappers `Collide_RayIntersect`, `Collide_SphereIntersect`, `FUN_0002c6f0`,
   `Collide_unknown`, `Collide_PlaceObject`.
7. **`Collide_Update` (0.6 KB)** last, with the GameCube check #65 ("Someone hasn't freed there hitlist!", hit heap
   count against `HitAllocCnt` after the first loop) as an `NF_WARN`. GC check #31 is already in our
   `Coll_GetFreeHit`.
8. `Debris_Create_Loop` belongs with the breakables (`Break_Kill`), not here.

Remaining after step 7: nothing in `engine.collision` except `Debris_Create_Loop`. Per the project rule, new test
harness code goes in its own compilation unit (`src/action/devtools/CollideShadow.cpp`), not inside `Collide.cpp`.

## Testing

- **Offline shadow tests** (steps 1-2, 4): as `src/action/devtools/MatrixShadow.cpp` - random inputs, run ours and
  the original under `XbeOriginalScope`, compare with a relative tolerance for floats and exact for booleans away
  from the boundary (count a boolean mismatch as a failure only when the deciding quantity is more than ~1e-4 from
  its threshold). Register-convention originals need a small asm trampoline to call.
- **In-level shadow tests** (steps 3, 5, 6): after a level loads (the `MenuProbe` level and teleport steps, or the
  F7/F8/F9 teleport in `devtools/Teleport.cpp`), take the current room and its portal neighbours, and for each
  room, static sub-cel and object mesh run a few hundred random queries (rays through the bbox, spheres and
  capsules near it) through ours and the original on separate copies of the `HITTEST_tag` and `HITDATA_tag`. Save
  and restore every global the code touches between the two runs: `BoxCnt` 0x1ddc6c, `ColBoxs` 0x1dc9a0,
  `DeltaP` 0x1dec10, `Ident` 0x1dec1c, the portal stamp `GameState+4` 0x1f6584, the portal loop counter 0x1dec34,
  `TriHeap`/count (0x1dec30/0x1dec2c), and the RNG (`RandWord0/1` 0x18cdf8/0x18cdfc) for `Collide_Update`. Hits come
  from the shared `HitHeap`; free both lists afterwards.
- **Live dual run** (steps 6-7): in a debug build, have our `Collide_Update` snapshot each mover's `HITTEST_tag`
  and position, run the original pass into scratch lists, restore, run ours, compare the lists (count, order,
  distance, position, normal, surface, flags, bone, cel, object, damage) and the push-outs, and keep ours. This
  catches order and tie effects the per-mesh tests miss.
- **Replays** (`tools/ui/scripts/`, per `docs/test-runner.md`): `weapons.txt` (bullets, feet, the scope ray),
  `laser_safe.txt` (laser ray, frozen drones), `multiplayer_bots.txt` (capsule pushes at half strength, bot sight),
  `level_reloads.txt` (pool reset across levels). Per the targeted-testing rule, run the replays that reach the
  change, and leave the full suite to the test runner.
