# Collision functions, one by one

Every function in `engine.collision` (0x288e0-0x2cf00), in address order, then grouped notes. Status: **ours** =
reimplemented and injected (`// AUTOINJECT` in `src/action/engine/Collide.cpp`), **live** = original, still
reached. None is dead. "PS2" gives the PS2 symbol where there is one (from `/PS2_EU_51258/ACTION.ELF`); "-" means
the PS2 build inlined it or has no counterpart. Structures, kinds and bits are in [data.md](data.md).

## Inventory

| address | size | Xbox name (Ghidra) | PS2 name | status | callers | what it does |
|---|---|---|---|---|---|---|
| 0x288e0 | 128 | `Intersect_UnpackTrianglePlaneEq` (invented name) | - | live | 4 mesh tests | s16[3] at `words + 2*idx` × 6.103888e-05 → normal, f32 at word +3 → d; returns true |
| 0x28960 | 240 | `Intersect_UnpackTriangleVerticesToWorld` (invented; "to leaf space" really) | - | live | 4 mesh tests, `Debris_Create_Loop` | three vertices `s16[3]*scale + offset` from the leaf's `VertexTransform` (box +0x20) |
| 0x28a50 | 144 | `Intersect_CheckIfFlagsMatch` | `Collide_Filter(ushort,ushort,uint,float&)` | live | `Collide_Intersect`, ray, sphere and capsule tests | the surface filter: sets dist 1e8 for masked surface types; returns true if rejected |
| 0x28ae0 | 128 | `Intersect_SphereBox` | `Intersect_SphereBox` | live | 8 (Pick, Portal, RecurseBoxesSph, AI nav, MP hill, bot states) | sphere's bbox vs box, inclusive |
| 0x28b60 | 112 | `Intersect_BoxBox` | `Intersect_BoxBox` | live | `ASH_RecurseBoxesBox`, `Cel_ObjectLeftCel` | box overlap, inclusive, tested x, z, y |
| 0x28bd0 | 288 | `Intersect_PointOOBox` | `Intersect_PointOOBox` | live | `Collide_Jointy`, `Drone_PointInAnyAIBox`, `Drone_VisibilityForPosition` | point within `radius` of an oriented box (per-axis slab test, not a true distance) |
| 0x28cf0 | 816 | `Intersect_RayBox` | `Intersect_RayBox` and `ASH_Intersect_RayBox` (one Xbox copy) | live | `ASH_RecurseBoxesRay`, `Collide_Jointy`, `Collide_Pick` | slab test of the test's ray against an AABB |
| 0x29020 | 416 | `Intersect_AABB_SweptVolume` (invented, misleading) | - | live | gather, capsule test | triangle's bbox vs a box (register args) |
| 0x291c0 | 144 | `FUN_000291c0` | `RecurseBoxesSph` | live | `Intersect_PointGeom` | box-tree walk, sphere query |
| 0x29250 | 144 | `ASH_RecurseBoxesBox` | `ASH_RecurseBoxesBox` / `RecurseBoxesBox` | live | gather, capsule test | box-tree walk, box query |
| 0x292e0 | 144 | `FUN_000292e0` | `ASH_RecurseBoxesRay` | live | ray test | box-tree walk, ray query; stores each leaf's `t` |
| 0x29370 | 64 | `FUN_00029370` | - | live | Pick, PickObj | sphere vs sphere, squared distance out (register args) |
| 0x293b0 | 96 | `FUN_000293b0` | - | live | Pick, PickObj | test's segment vs test's sphere (register args) |
| 0x29410 | 96 | `FUN_00029410` | - | live | Pick, PickObj | the same with the radius widened by the test's width |
| 0x29470 | 96 | `Coll_ResetHitHeap` | `Coll_ResetHitHeap` | **ours** | `ResetMap_GameInit` | empty the hit pool, fresh 100-vertex `TriHeap` |
| 0x294d0 | 48 | `Collide_FreeHitList` | `Collide_FreeHitList` | **ours** | 39 | give a list's hits back to `HitHeap` |
| 0x29500 | 96 | `Coll_GetFreeHit` | - | **ours** | AddHitToList, Pick, Update | pop a zeroed hit; grow 0x40 at a time to 1,024 |
| 0x29560 | 64 | `Coll_AddHitToList` | `Coll_AddHitToList` | live | Pick, PickObj, `Explode_Propagate`, `NDrone2_DSTATE_BotDeathByExplosion`, `PlayerOrDrone_FeetOnPoint_DoCollision` | prepend a hit (dist, cel, obj); **returns it in EAX** |
| 0x295a0 | 144 | `Collide_Sort` | - | live | driver, `Collide_FilterBullets`, `Collide_Update` | drop 1e8 hits, sort by distance (EBX arg) |
| 0x29630 | 1024 | `Intersect_PointGeom` | `Intersect_PointGeom` | live | `Collide_Intersect` | sphere vs mesh, with push-out |
| 0x29a30 | 1392 | `Intersect_RayGeom` (**wrong**) | `Intersect_CylTriGeom` | live | `Collide_Intersect` | capsule-swept triangle gather into `TriHeap` (kind 0x2000) |
| 0x29fa0 | 800 | `FUN_00029fa0` | `Intersect_RayGeom` | live | `Collide_Intersect` | ray vs mesh, nearest front-facing triangle |
| 0x2a2c0 | 1984 | `Intersect_CylGeom` | `Intersect_CylGeom` | live | `Collide_Intersect` | capsule vs mesh, with push-out and the floor flag |
| 0x2aa80 | 592 | `Collide_PickObj` | `Collide_PickObj` | live | `Collide_Pick` | broad phase for one object (EAX = test) |
| 0x2acd0 | 1248 | `Collide_Jointy` | `Collide_Jointy` | live | `Collide_Intersect` | characters: bone boxes, or capsule vs capsule push (EAX = hit) |
| 0x2b1b0 | 176 | `Collide_RayTriangle` | `Collide_RayTriangle` | **ours** | `Cel_ObjectLeftCel`, `Intersect_Portal` | infinite line (point + direction) through a triangle |
| 0x2b260 | 240 | `Intersect_ConeSphere` | `Intersect_ConeSphere` | live (called by ours) | `Sensor_InCone` (ours), `FUN_0006b830` | cone vs sphere |
| 0x2b350 | 96 | `Collide_GetDamageNObjects` | `Collide_GetDamageNObjects` | live (called by ours) | `SP_GetHitDamage` (ours), `MP_PlayerKilled`, `GT_Update`, `Monitor_Update` | sum damage, collect hit objects |
| 0x2b3b0 | 96 | `Collide_FilterBullets` | `Collide_FilterBullets` | live (called by ours) | `Break_Update`, `SP_Hit`, `SP_GetHitDamage` (ours), `Destroy_CollisionHandler`, `Copter_Update`, `FuseBox_Update` | drop hits from bullets whose weapon flags match, re-sort |
| 0x2b410 | 432 | `Debris_Create_Loop` | `Debris_Create_Loop` | live | `Break_Kill` | breakables: debris over a mesh's triangles (not collision) |
| 0x2b5c0 | 848 | `Intersect_Portal` | `Intersect_Portal` | live | `Collide_StraddleCels` | does the query volume pass through a portal quad |
| 0x2b910 | 240 | `Collide_StraddleCels` | `Collide_StraddleCels` | live | Pick, `Controls_StraddleTest`, `View_AddForcedObjects`, `build_LinkDoors2Portals`, 3 AI nav | rooms reachable through portals the volume touches |
| 0x2ba00 | 1040 | `Collide_Pick` | `Collide_Pick` | live | driver, `Collide_unknown`, `Collide_Update` | broad phase |
| 0x2be10 | 1584 | `Collide_Intersect` | `Collide_Intersect` | live | driver, `Collide_unknown`, `Collide_Update` | narrow phase for one candidate (EAX = hit) |
| 0x2c440 | 160 | `Collide_CylinderIntersect` (**wrong**) | - (inlined) | live | `Collide_RayIntersect`, `Collide_SphereIntersect`, `FUN_0002c6f0` | the core driver: Pick, Intersect each, Sort (EDI = test, EAX = flags) |
| 0x2c4e0 | 208 | `Collide_RayIntersect` | `Collide_RayIntersect` | live (called by ours) | 27 | ray query |
| 0x2c5b0 | 96 | `Collide_LineOfSight` | `Collide_LineOfSight` | **ours** | 18 | is the segment clear |
| 0x2c610 | 224 | `Collide_SphereIntersect` | `Collide_SphereIntersect` | live | 8 | sphere query, moves the caller's point by the push-out |
| 0x2c6f0 | 112 | `FUN_0002c6f0` | `Collide_CylinderIntersect` | live | `RB_RestingContactRot` | capsule query on a caller's test |
| 0x2c760 | 384 | `Collide_unknown` | - (Xbox only) | live (called by ours) | `maybe_psiDrawShadow` (ours), `Collide_PlaceObject` | triangle gather around a segment |
| 0x2c8e0 | 960 | `Collide_PlaceObject` | `Collide_PlaceObject` | live | `GT_DeployMiniGun` | find a flat spot under a point and align a matrix to it |
| 0x2cca0 | 608 | `Collide_Update` | `Collide_Update` | live | `control_movement_object_handler` | per-frame collision of every mover |

Suggested names for the Ghidra database when someone next edits it (not applied): 0x29a30 `Intersect_CylTriGeom`,
0x29fa0 `Intersect_RayGeom`, 0x2c440 `Collide_RunQuery` (invented), 0x2c6f0 `Collide_CylinderIntersect`, 0x291c0
`RecurseBoxesSph`, 0x292e0 `ASH_RecurseBoxesRay`, 0x28a50 `Collide_Filter`, 0x29020 `Intersect_TriBoxBounds`
(invented), 0x29370/0x293b0/0x29410 `Pick_SphereSphere`/`Pick_SegSphere`/`Pick_SegSphereWide` (invented),
0x2c760 `Collide_GatherTriangles` (invented).

## Leaves

### `Intersect_CheckIfFlagsMatch` 0x28a50 (PS2 `Collide_Filter`)

`bool (u8 surface, u8 hitFlags, uint mask, float *dist)`. In order **(decompile)**: surface bit 6 and mask 0x800
→ dist = 1e8; mask 1 and type 0x0d/0x0e → 1e8; mask 2 and type 0x10 → 1e8; mask 8 and (hit flag 2, or surface bit
6 or 7) → 1e8 and return true. Finally return `*dist == 1e8`. So a hit already at 1e8 counts as rejected. The PS2
inlines the same test in `Collide_Intersect` too.

### `Intersect_SphereBox` 0x28ae0, `Intersect_BoxBox` 0x28b60

Plain inclusive interval tests (`<=` on both sides), x then z then y. `Intersect_SphereBox` is
`(centre, radius, min, max)`: the sphere's bounding box against the box, not a true sphere test.
`Intersect_BoxBox(aMin, aMax, bMin, bMax)`.

### `Intersect_PointOOBox` 0x28bd0

`(matrix, float halfExtents[3], point, radius)`. `d = point - matrix translation (+0x30)`; for the norm (x), up (y)
and dir (z) axes from `Mat_GetNorm/GetUp/GetDir`, `|d·axis| <= halfExtent + radius`. Returns 1 in AL when all
three pass. The return register's upper bytes are left from `FNSTSW`, so callers must test AL only (they do). Used
with the bone entries from `AnimGetCollData`, whose layout is a 0x3c-byte 3x4 matrix (translation at +0x30), half
extents at +0x3c and the bone index at +0x48 **(disasm)**.

### `Intersect_RayBox` 0x28cf0

`bool (HITTEST_tag *t, min, max, float *tOut)`. If the ray start is inside the box (`min <= s < max` per axis, with
the upper bound written as `s < max != (s == max)`, i.e. `s <= max`) → `*tOut = 0`, true. Otherwise a slab test on
`t->+0x34` (the delta, not normalised): per axis x, z, y, a zero component rejects if the start is outside that
slab, else `t0,t1 = (min-s)/d, (max-s)/d` sorted, rejecting if the far one is negative. Initial values are
±3.4e38-ish constants (0xfdcca14b, 0x7f7fc99e). Accept if `tNear <= tFar`, `tNear >= 0` and `tNear <= t->+0x78`;
`*tOut = tNear`. Because +0x78 is the ray's length while `t` is in delta units, the far limit is length, not 1 (see
README quirks).

### `Intersect_AABB_SweptVolume` 0x29020 (register arguments; the name is wrong)

Inputs ECX, EDX, ESI (three triangle vertices - the callers pass v1, v0, v2), EDI (box min), EBX (box max)
**(disasm, ABI facts)**. Per axis x, z, y: `max(v0,v1,v2) >= min` and `min(v0,v1,v2) <= max`, written as nested
ternaries. It is a triangle-bounds vs box overlap; nothing is swept.

### `Intersect_ConeSphere` 0x2b260 (called by ours)

`(apex, float dir[3], cosHalfAngle, sinHalfAngle, sphere{x,y,z,r})`. If the apex is inside the sphere → true.
Else with `d = centre - apex`, `p = d·dir`: true if `p > 0` and `p² >= cos²·|d|²` (centre inside the cone);
otherwise `q = p·cos + sqrt(| |d|² - p² |)·sin`, false if `q² - |d|² + r² < 0` or `q < 0`, else true.

### `Collide_RayTriangle` 0x2b1b0 (ours)

Already ported. The second argument is a direction; the hit parameter is written out and **no range check is
made**, so it is a line test. Used so by `Intersect_Portal` and `Cel_ObjectLeftCel`.

### `Collide_GetDamageNObjects` 0x2b350, `Collide_FilterBullets` 0x2b3b0 (called by ours)

`GetDamageNObjects(list, objs, &count, max)`: zero `*count` if given, sum `+0x08` over the list, append each
non-NULL `hitObj` while `*count < max` (duplicates kept). `FilterBullets(&list, flags)`: every hit whose object is a
bullet with `(bullet->+0x38 weapon def)->+0x10 & flags` gets dist 1e8; if any did, `Collide_Sort(&list)`. Our
commented-out draft in `Collide.cpp` matches this.

## Mesh helpers and tree walks

### `Intersect_UnpackTrianglePlaneEq` 0x288e0, `Intersect_UnpackTriangleVerticesToWorld` 0x28960

Both stack `__cdecl`, both inlined on the PS2. Plane: `n = s16 words[idx..idx+2] * 6.103888e-05`, `d = f32` at
`words + 2*idx + 6`. Vertices: for each of three word offsets, `v = s16[3] * vt->scale + vt->xyz`, written to
`out[0..2]`. The output is in the mesh's local space (the leaf decodes to it); "to world" in the name is wrong.

### The three walkers: `RecurseBoxesSph` 0x291c0, `ASH_RecurseBoxesBox` 0x29250, `ASH_RecurseBoxesRay` 0x292e0

Same shape **(decompile)**, first argument `&colldata->collBoxes`:

```
walk(boxes, node, query):
    if !overlap(query, node) return
    while node.childA >= 0:
        walk(boxes, &boxes[node.childA], query)
        node = &boxes[node.childB]
        if !overlap(query, node) return
    ColBoxs[BoxCnt++] = {node, t}            (t only for the ray walk: Intersect_RayBox's tNear)
```

Overlap: `Intersect_SphereBox(centre, r, min, max)` / `Intersect_BoxBox(qMin, qMax, min, max)` /
`Intersect_RayBox(test, min, max, &t)`. The leaves come out depth first, A before B, which fixes the order in
which triangles are visited and so tie-breaks and early-outs. No check against the 600-entry capacity.

## Hit pool and sorting

`Coll_ResetHitHeap`, `Collide_FreeHitList` and `Coll_GetFreeHit` are ours and documented in `Collide.cpp`.

### `Coll_AddHitToList` 0x29560

`HITDATA_tag* (HITDATA_tag **list, float dist, cel_tag *cel, obj_tag *obj)`: `Coll_GetFreeHit`, set dist,
prepend (only touching `prev`/`next` when the list is non-empty), set cel and obj, return the hit in EAX. Callers
use the return to copy a direction into +0x2c (ray box candidates) or the plane (capsule object candidates). Three
callers outside collision fake hits with it.

### `Collide_Sort` 0x295a0 (EBX = `HITDATA_tag **list`)

Walk the list: hits with dist bit pattern 0x4cbebc20 go back to `HitHeap` (`LList_Add`), the rest into `SrtList`.
None left → `*list = NULL`. Otherwise `QuickSort(SrtList, n, 4, Compare_HitData /*2*/, NULL)` (0xbda60, with
`CompHitData` 0xbd8d0: -1/1/0 on `+0xc` less/greater/else) and relink in array order (`prev` of the first and
`next` of the last NULL). The comparison is on the float, the removal on the exact bit pattern. `QuickSort` is a
non-stable middle-pivot quicksort, so ties must go through it (or a literal port) to keep the original order.

## Mesh tests

All four run on `Collide_Intersect`'s local copy of the test, with points already in the mesh's local space,
`+0x74` the glist and `+0x68` the local-to-world matrix. They write `hitData` (+0x0c dist, +0x10 plane, +0x38
position, +0x44 surface; local space) and `DeltaP` (local); `Collide_Intersect` converts and applies them. Shared
pattern: zero `BoxCnt` (and `DeltaP` for the pushing tests), walk the tree, `dist = 1e8` and return if no leaf,
then for each leaf `i` in `ColBoxs` and each triangle `j` in `[leaf.childB, leaf.triEnd)`, with
`chunk = colldata->chunks + 64*leaf.chunk`, triangle record `tris + 8*j`, surface `surface[j]`.

The **flags 0x10 surface skip** in the ray, sphere and capsule tests is
`if ((flags & 0x10) == 0 || (surface[best] & 0xc0) == 0) test triangle j`, with `best` the best triangle index so
far, initially 0xffffffff - so the first triangles consult `surface[-1]` **(decompile, disasm)**. Keep it.

### `Intersect_PointGeom` 0x29630 (sphere, kind 0x101)

- `lo = (flags & 8) ? -width : 0`; `best = width` (the test's +0x78); walk with `RecurseBoxesSph(sphPos, sphRad)`.
- Per triangle: unpack the plane; `d = DistancePointToPlane(sphPos, plane)`; skip unless `lo <= d <= width`.
  Unpack the vertices; `e = vecutil_Dist2Tri_PointGeom(sphPos, v0, v1, v2, plane, d, &hit->position)` (closest point
  written to the hit); skip unless `lo <= e <= width`.
- Push-out unless `hit->+0x45 & 6`: `DeltaP += n * (width - |e|)`, and `sphPos += DeltaP` (the **accumulated**
  vector, every time - so earlier pushes are re-applied to the point on each later contact).
- If `e <= best` (written `e < best != (e == best)`): remember the position, `best = e`, the
  triangle and its chunk; with flags 0x10, stop all loops if `Intersect_CheckIfFlagsMatch` does not reject it.
- End: if a triangle was kept, dist = best, surface byte, plane unpacked into the hit, position restored from the
  remembered one (a first write from the zero normal is dead). Else dist = 1e8.

### The ray test 0x29fa0 (PS2 `Intersect_RayGeom`; kind 0x201)

- Walk with `ASH_RecurseBoxesRay(test)`; `best = 1.0`.
- Per triangle: unpack the plane into the test's +0x00; `s = DistancePointToPlane(start)`, `k = n·delta`. Only
  front faces: `k < 0`; `u = -s/k`, require `0 <= u <= 1`; point `= start + u·delta` written to the test's +0x40.
  If `u < best` and `vecutil_point_on_poly(point, v0, v1, v2, plane)`: hit position = point, `best = u`, remember
  the triangle; flags 0x10 early-out as above.
- End: dist = `|start - position|` (local units = world units, the matrices are rigid), surface, plane. Else 1e8.

### `Intersect_CylGeom` 0x2a2c0 (capsule, kind 0x800)

- `best = width`; query box = bbox of start and end widened by `width + 0.1`; `ASH_RecurseBoxesBox`.
  `axis = normalise(start - end)`; `seg = end - start`.
- Per triangle (no surface pre-skip): vertices, plane into the test's +0x00; `ds, de` = plane distances of start
  and end; skip unless one of them is `>= -width` and one `<= width`, and the triangle's bounds overlap the query
  box (`Intersect_AABB_SweptVolume`).
- `k = n·seg`. If `|k| <= 0.0002` (parallel): closest point on the segment to the centroid, then
  `e = vecutil_Dist2Tri(p, v0, v1, v2, plane, &region, &test->pos)`. Else `u = clamp(-ds/k, 0, 1)`; skip if
  `k > 0.999 && u > 0.5` or `k < -0.999 && u < 0.5` (end caps facing away); `p = start + u·seg`;
  `e = vecutil_Dist2Tri(...)`; if region is not 0 (face) and not 99 (behind), `p` = closest point on the segment
  to the triangle point in `test->pos`.
- Accept if `e >= 0` and region != 99. Push-out unless `hit->+0x45 & 6`: `w = p - closest`; skip the triangle if
  `w·n <= 0`; `len = |w|`, skip if `len >= width`; `w = w/len * (width - len)`; add `w` to `DeltaP`, start, end and
  the query box. If `+0x88 == 0`, `n·axis > 0.7`, `(closest - end)·axis < 0` and the normalised version `< 0.7`,
  set `+0x88 |= 1` (a floor-like contact past the end point; meaning **inference**).
- If `e < best`: remember position (`test->pos`), triangle, chunk; with flags 0x10 stop all loops if the filter
  passes it **or** its surface has bits 0xc0.
- End: dist, surface, plane, and +0x2c = -normal. Else 1e8.

### The gather 0x29a30 (PS2 `Intersect_CylTriGeom`; kind 0x2000)

- `DeltaP = 0`; query box = bbox of start/end widened by `width + 0.05`; `ASH_RecurseBoxesBox`; `seg = end - start`.
- Per triangle: skip type 0x0d/0x0e under mask 1, type 0x10 under mask 2, and - under flags 0x10 - when the dword
  at `surface - 4` has bits 0xc0000000 (constant for the mesh; a bug). Vertices; bounds vs query box; plane into
  the test's +0x00.
- `k = seg·n`. `|k| <= 0.05`: if flags bit 0x01 is clear skip, else closest point on the segment to the centroid.
  Otherwise `u = clamp(-ds/k, 0, 1)`, `p = start + u·seg`. `e = vecutil_Dist2Tri(p, ..., &test->pos)`.
- Keep if `-0.0002 <= e <= width`: grow `TriHeap` when `count + 3 >= TriAllocCnt` (return without storing if
  `TriAllocCnt > 500`; else allocate `(TriAllocCnt+100)*12` bytes, flags 0x1a04-ish, zero, copy, `Mem_Free`,
  `TriAllocCnt += 100`); store the three vertices in **world** space (`MatrixMultiplyVector(test+0x68)` unless
  `Ident`) if `count + 3 < 501`.
- Writes no hit: dist stays 1e8 and `Collide_Intersect` returns true without filtering.

### `Collide_Jointy` 0x2acd0 (characters; stack test, EAX = hit)

Works in world space on the real test (called before `Collide_Intersect` localises anything). `dist = 1e8`.
Bone boxes come from `AnimGetCollData(obj, &count)` only when the kind's high byte has 0x01 or 0x02.

- **0x101**: first box with `Intersect_PointOOBox(entry, entry+0x3c, test->pos, width)` → surface 0, bone
  (+0x48), dist = width, damage, position = `pos`, normal = `normalise(pos - box centre)`, direction = normal.
  True.
- **0x201**: per box, bring start, end and delta into box space (`Mat_Inverse` copy, `RotPreTransVec`,
  `ApplyMatrixLV`) and `Intersect_RayBox` against `(-e, e)`. First box hit (not nearest): surface 0, bone, damage;
  `t == 0` → position = start, dist = -1.0; else position = start + t·delta and dist = `t·width` (world length);
  normal = -normalised local delta (box space, not rotated back). True.
- **0x800**: only against another mover whose own kind is 0x800. The push always moves the **owner** (the test
  being run). Player against player (owner +0xdb == 3, other a player): skip when the owner's speed
  (`|ownerData+0xc|`) is less than the other's, so the slower one is pushed. Drone against drone: skip when the other
  drone's `+0x38c` float is larger than the owner's. In single player the strength is 1.0 and nothing is pushed by
  the player (return false when the other object is the player) - the player is pushed out of drones, never the
  reverse; in multiplayer 0.5 each way. Closest
  points between the two segments (three `vecutil_calculate_closest_point_on_line` calls, an approximation);
  if `|a - b| <= rOther + width`: push `-(strength·overlap)` along `normalise(a - b)` into start, end and +0x10;
  normal = that direction, dist = overlap, position = owner position + normal·strength·overlap. True.
- The result goes straight back through `Collide_Intersect`: no surface filter, no `DeltaP`.

## Broad phase

### `Intersect_Portal` 0x2b5c0

`bool (HITTEST_tag*, portal_tag*)`, by kind:

- **0x200/0x201**: `dir = end - start`; for the two quad triangles (table 0x163b80, loop counter in the global
  0x1dec34) `Collide_RayTriangle(start, dir, ...)`; true on either. Line, not segment.
- **0x800**: `k = plane.n · delta`; `k == 0` → closest point on the segment to the portal centre (+0x58); else
  `u = clamp(-dist(start)/k, 0, 1)` and `p = start + u·delta`. True if `|vecutil_Dist2Tri(p, tri, plane)| <
  1.5·width` for either triangle.
- **0x100/0x101/0x400/0x1000**: `Intersect_SphereBox(test->pos, width, portal bbox)`, then `|Dist2Tri(pos, tri,
  plane)| < width` for either triangle. Note `pos`, not `sphPos`.
- Anything else (0x2000 never gets here: the gather picks as 0x800): false.

A negated plane is built on the stack for the second triangle but `(&plane)[i >> 1]` always picks the original.

### `Collide_StraddleCels` 0x2b910

`cel_tag** (HITTEST_tag*, u16 *count)`. `stamp = ++GameState+4`. Breadth-first from `test->cel`: for each portal of
the current room whose +0x20 is not `stamp`, stamp it and its partner, and if `Intersect_Portal` passes, append the
target room unless already listed. Next room = the next list entry. Stops when the list reaches 126 or runs out;
then **appends the start cel** and returns the static list 0x1dec38. The start cel is not in the list during the
walk, so it can be appended twice if a portal path leads back to it (**inference** from the code) - the broad
phase would then test it twice.

### `Collide_PickObj` 0x2aa80 (EAX = test; stack obj, owner)

Skip if `obj` is the owner or either ignored object, or the owner has +0xd9 bit 1 and `obj` +0xd9 bit 2; if its
effect flags meet the mask-derived bits (data.md); if masked by type (0x400 players, 0x100 drones, 0x200 bullets);
or unless (kind 0x100, or `obj` is a character, or it has a mesh). Then by kind:

- 0x200/0x201: copy `obj` centre/radius into the test's `sphPos`/`sphRad`; `FUN_000293b0` (segment vs that sphere)
  → `Coll_AddHitToList(owner, distSq, NULL, obj)`, copy the delta into hit +0x2c.
- 0x800: the same with radius ×1.5 and `FUN_00029410` (radius + width); hit +0x10 and +0x2c = the test's +0x00.
- 0x100/0x101/0x400/0x1000: `FUN_00029370(&dist, &test->sphPos, &obj->centre, out = test+0)`: hit with the squared
  centre distance.

### `Collide_Pick` 0x2ba00

Does nothing without `test->cel`. `++GameState+4`. Shape flags: high byte & 0x11 → `sphPos = pos`, `sphRad =
width`; & 0x02 → `delta = end - start`, `width = max(|delta|, 1.0)`; & 0x08 → delta. Then `Collide_StraddleCels`,
and a copy of the test for the sub-cel sphere tests. For each reached room `i` (in list order):

1. Unless mask bit 0x80: each static sub-cel (`room+0x18` chain) with a glist that has `COLLDATA` with a surface
   array, `+0x84 & 0x20` clear, and not (`+0x84 & 0x1000` under mask 0x20):
   - 0x800: segment vs the sub-cel's bounding sphere ×1.5 widened by width (`FUN_00029410` on the copy);
   - 0x200/0x201: non-identity cel → segment vs bounding sphere (`FUN_000293b0`); identity cel →
     `Intersect_RayBox` on its bbox, and the delta into hit +0x2c;
   - 0x100/0x101/0x1000: non-identity → sphere vs sphere (`FUN_00029370`); identity → `Intersect_SphereBox`, dist 1.0.
2. Unless mask 0x10: each object in the room (`room+0x8` chain) without effect flags 0x30000000 →
   `Collide_PickObj(test, obj, owner)`.
3. Unless mask 0x40, if the room has collision and `+0x84 & 0x20` is clear: a hit for the room itself with dist
   `i*0.1 - 1000` (inlined `Coll_AddHitToList`).

Then, unless mask 0x10, every object on `ForcedList` (`obj+4` chain, stopping on NULL or a self-link) →
`Collide_PickObj`. The candidate list is prepend-ordered.

## Narrow phase and drivers

### `Collide_Intersect` 0x2be10 (stack test, flags; EAX = hit)

1. `Ident = 0`. **Object candidate**: no object or no glist → false. `effectFlags & 0x40` → hit flag 2. A character
   without a mesh and a kind other than 0x100 → return `Collide_Jointy`. Matrix = `obj->mtx`; `test+0x74 = glist`.
   With `obj+0xd9 & 4` and kind 0x101: sphere-vs-sphere response - `DeltaP = sphPos - centre`, normal =
   `normalise(DeltaP)`, position = `sphPos + 0.5·DeltaP`, dist = `rObj + sphRad - |DeltaP|`, `+0x10 += normal·dist`;
   true (no filter, no damage).
   **Cel candidate**: `Ident = cel+0x3c bit 27`; `+0x84 & 0x40` → hit flag 2; matrix = identity or
   `RotTransMatrix(rot, pos)`; `test+0x74 = cel+0x38`.
2. Copy the test (0x8c bytes) to the stack, `+0x68` = the matrix, invert it, and bring `pos` and `sphPos` into
   local space (copies when `Ident`). `hit+0x45 |= flags`; `dist = width`.
3. By kind: 0x400 → 1e8. 0x101 → re-invert, `Intersect_PointGeom`. 0x201 → localise start/end, local delta,
   re-invert, ray test. 0x800 → localise, re-invert, `Intersect_CylGeom`. 0x2000 → localise, re-invert, gather,
   **return true**. Other (0x100, 0x1000): object candidates get position = centre and
   `dist = max(sqrt(dist) - (rObj + width), 0)` (the broad phase's squared distance); return true.
4. `Intersect_CheckIfFlagsMatch(surface, hit+0x45, mask, &dist)`; dist 1e8 → false.
5. damage = test's; position to world (`MatrixMultiplyVector`); `DeltaP` rotated to world (`ApplyMatrixLV`) and added
   to the real test's +0x10, start, end, `pos` and `sphPos`; normal rotated to world; `+0x88 |= local +0x88`; with
   high byte 0x02, hit +0x2c = normalised delta. True.

### Core driver 0x2c440 (Ghidra "Collide_CylinderIntersect"; EDI = test, EAX = flags, stack `char startCelOnly`)

`Collide_Pick(test)`; for each candidate on `owner->hitList`: if it is a cel other than `test->cel` and
`startCelOnly` → dist 1e8; else `Collide_Intersect(owner->+0xb0 /* = test */, hit, flags)`, and if it hit and
flags has 0x10, mark every later candidate 1e8 and stop. Then `Collide_Sort(&owner->hitList)`. With
`startCelOnly` the sub-cels of the start room are dropped too (they are cels other than the room), leaving the
start room's own mesh and objects.

### The wrappers

- **`Collide_RayIntersect` 0x2c4e0** `(start, end, cel, ignore1, ignore2, HITDATA_tag **out, char startCelOnly,
  uint mask, u16 flags)`: zero a test and a fake owner, kind 0x0201, width -10.0, then the driver; `*out` = the
  list; returns non-empty. 27 callers, several ours.
- **`Collide_LineOfSight` 0x2c5b0** (ours): `RayIntersect(..., startCelOnly 0, mask | 4, flags 0x10)`, frees the
  list, returns true when clear.
- **`Collide_SphereIntersect` 0x2c610** `(_VECTOR *pos inout, radius, cel, ignore1, ignore2, out, char
  startCelOnly, char boundsOnly, u16 mask, u16 flags)`: kind `0x101 - (boundsOnly != 0)`, flags also in +0x86,
  `pos` into +0x40; after the driver `*pos += push-out`. The PS2 signature agrees (`signed char, signed char,
  unsigned int, unsigned short`).
- **`FUN_0002c6f0`** (PS2 `Collide_CylinderIntersect`) `(HITTEST_tag *test, HITDATA_tag **out, char
  startCelOnly, u16 flags)`: uses the caller's test (rigid bodies, `RIGIDBODY+0x94`), forces kind 0x800, a fake
  owner, the driver.
- **`Collide_unknown` 0x2c760** `(start, end, float radius, cel_tag *cel, uint *triCount, u16 mask, u16 flags)`:
  test with width = radius, kind **0x0800** (`+0x80 = 0, +0x81 = 8`), cel or `build_FindCel(start, glb_world)`,
  `count = 0`; `Collide_Pick`; switch the kind to 0x2000 (`+0x81 = 0x20`); `Collide_Intersect` on every candidate
  (no early-out, results ignored); free the list (inlined `Collide_FreeHitList`); `*triCount = min(count/3, 166)`;
  return `TriHeap`. Ours: `maybe_psiDrawShadow` (radius 1, mask 0x307, flags 4).
- **`Collide_PlaceObject` 0x2c8e0** `(_VECTOR *pos inout, _MATRIX *m inout, float height, float halfSize, obj_tag
  *ignore)`: `pos.y += 0.05`; ray from `pos` 3 units down `m`'s up axis (mask 0x1d); no hit → false. `up` = hit
  normal; `pos = hitPos + up·height`; gather from `pos` to `pos - 0.1·up` (radius halfSize, the hit's cel, mask
  0xd, flags 1). For each triangle: `|dist(pos)| - height < -0.13` → false (something pokes through); if `<= 0.2`
  and `|n·up| >= 0.96`, grow the footprint box (starting at `pos`) by its bounds. `Mat_Align2Up(m, up, dir)`; true if
  both footprint corners reach at least `halfSize` along `|norm|+|dir|`. Writes the caller's `pos` and `m` even on
  failure.

### `Collide_Update` 0x2cca0

See the overview, section 1.4; the details a port needs **(decompile)**:

1. For every object from the first: free its list; if it has a test, `test+0x5c = obj`, `test+0x10 = 0`.
2. For every object with a test of non-zero kind: `+0x88 = 0`; `Collide_Pick`; for each candidate **without hit
   flag 1** (mirrored hits arriving from movers processed earlier this frame are kept as they are):
   `Collide_Intersect(test, hit, test+0x86)`, and on a hit under flags 0x10 mark the rest 1e8 and stop.
   `Collide_Sort`. Then walk the sorted list: for each hit on an object, prepend to **that object's** list a copy
   with flag 1, cel NULL, obj = this mover, damage = this mover's +0x7c, and the same dist, surface, bone,
   position, normal and direction. Continue to the next hit only if: surface bit 7 and `Rand_Rand(0x3f) & 1`; or
   flags 0x20 and surface == 0x10; or surface bit 6; or hit flag 2. Mirrored-in hits on this list are walked too,
   so they are mirrored back to the earlier mover (**inference** from the code).
3. For every object: `control_funcs[type].collideFunc(obj)` if non-NULL.

GameCube check #65 (`docs/gamecube-checks.md`) belongs after loop 1.

### `Debris_Create_Loop` 0x2b410 (breakables)

`(COLLDATA_tag*, HITDATA_tag *hit, obj_tag *obj)`: for every leaf box, for triangles `j = first, first+3, ...`
(steps of 3 - every third triangle), hit position = triangle centroid + `obj->position` (no rotation), hit plane
from the triangle, then `Debris_CreateEx(hit, &EffectInfo[hit->surface & 0x3f], obj->inCel, 1)`. The surface byte
is the caller's, not the triangle's. Port it with `Break_Kill`.
