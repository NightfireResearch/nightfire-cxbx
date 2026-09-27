# Driving engine: level collision geometry

How `Driving.xbe` (EA's EAGL engine, Xbox) stores and queries the static collision of a track,
and the on-disk layout of the chunks involved. Addresses are Xbox (`Driving.xbe`) unless marked PS2
(`DRIVING.ELF`, which has the same code and better names). The layouts that follow from this are proposed
in `driving-symbol-matching/results/struct-pilot-collision.json`, and `tools/carp_collision.py`
exports the geometry to OBJ.

```
python tools/carp_collision.py Release/dump_driving/data/track/snow2a_mis4.crp snow.obj
python tools/carp_collision.py <file.crp> - --list                  # summary of chunks and counts
python tools/carp_collision.py <file.crp> - --query 7.915,-43.023,-417.366   # emulated height query
```

## Runtime in brief

1. `WWorld::LoadTrackFile` (0xd12d0) loads `data\track\<name>.crp` through
   `RCARPFileLoader::LoadCARPFile` (0x7c0d0) → `RCARPFile::Load` (0x7bf00): `UFileLoader::FileLoad`
   reads the whole file into memory and `UGroup::Deserialize` (0x117d60) → `UGroup::ResolveOffsets`
   (0x117cb0) turns every relative offset into a pointer **in place**. `CARP::ResolveSymbolicReferences`
   (0x119d80) then replaces each `sr` string datum with a pointer to the named symbol
   (`CARP::SymbolicResolver::ProcessData` 0x118b40 → `USymbolTable::NameLookup`, `UData::AdoptData`).
   The root group is stored in `RCARPFile+8` and `WWorld+0x14`.
2. `WWorld::Open` (0xd21a0) calls `WCollisionMgr::Init` (0xc55b0) and `WGrid::Init` (0xc5fc0) with that
   root, and points `WWorld+0x20` at the render instances (`Map ` group, datum `in`; count in `+0x28`).
3. `WCollisionMgr::Init` allocates the manager (0x34 bytes, global `fgCollisionMgr` = `*(0x239a70)`),
   points `+0x8`/`+0xc` at the `ci` array and count, `+0x10` at the `co` array, and then, for every
   instance, replaces its article pointer (`+0x1c`) with the pointer to that article's `ca` datum,
   the collision geometry.
4. `WGrid::Init` allocates the grid (0x24 bytes, `*(0x23b3a0)`) from `CGrd`, fills its node pointer array
   from the `cn` data, and puts the `de` elements (instances and objects that move) on dynamic lists.

A query goes **grid → instances → strip spheres → triangles** (or barriers):

* `WGrid::FindNodes`/`FindNodesBox` (0xc6450/0xc64b0/0xc62b0) turn a point+radius, box or segment into
  cell indices (`col = (x-origin.x)/cell`, `row = (z-origin.z)/cell`, index `row*cols+col`;
  `RangeCheckROWCOL` 0xc5710). A box wider than 20 cells in a direction collapses to its first cell in
  that direction.
* `WCollisionMgr::GetInstanceListGuts` (0xc43c0; segment form 0xc31f0) walks the kind-0 list of each
  cell (node iterator `FUN_000bfee0`/`FUN_000bff50`), de-duplicates with a query stamp (manager `+0x4`,
  instance `+0x10`), and keeps instances whose centre (`CalcPosition` 0xbe810) is within
  `radius + inst+0x3c` using the octagonal distance `max(|dx|,|dz|) + 0.25*min(|dx|,|dz|)`. With the
  strips flag it also calls `GetInstanceStripList` (0xc4120), which keeps only the strip spheres
  (and, inside them, triangles) that reach the query. The result is a list of `WCollisionInstanceCache`
  {instance, strip list}.
* The query point is transformed into the instance's local space with `WCollisionInstance::MakeMatrix`
  (0xbe8b0), which is a **world→local** matrix, and tested against the article's strips there.
* Ground height: `WCollisionMgr::GetWorldHeightAtPoint` (0xbf210) → `WWorldPos::FindClosestFace(p, true)`
  (0xd31f0) → `GetInstanceList(p, 0, …)` → `WWorldPos::FindClosestFace(list, p)` (0xd3050) →
  `WCollisionMgr::FindFaceInCInst` (0xbffe0; point overload `FUN_000bf4c0`). Per instance: reject
  unless `|local.x| <= +0xc` and `|local.z| <= +0x2c`; per strip sphere: reject unless the XZ distance
  to its centre is under its radius; per strip: `FUN_000bef30` returns the **first** triangle that
  contains the point in XZ (one-sided test `FUN_000bed40`/`FUN_000becb0` alternating per triangle,
  or two-sided `FUN_000bebd0`), skipping faces with flag bit 0x04 or a bit shared with the manager mask
  (`+0x2c`, 0xf0). The distance is `local.y + 0.5 - min(v.y)` (reversed when instance flag bit 0 is
  set) and must be positive; the smallest across all instances wins. `FUN_000beeb0` copies that face
  into the `WWorldPos` in **world space** (OrthoInverse of the instance matrix), and the height is
  `WWorldMath::GetPlaneY` (0xd2be0) with the face normal from `FUN_0005d3f0` (forced to +Y).
* Swept/segment collision (car body, projectiles, camera): `WCollisionMgr::CheckHitWorld` (0xc3b40) and
  `CheckHitWindow` (0xc4d70) use the segment form of `FindClosestFace` (0xd3110) →
  `FindFaceInCInst` (0xc01d0) → `FindFaceInTriStrip` (0xbf0e0, two-sided, vertices transformed into
  segment space), then barriers (`GetClosestIntersectingBarrier` 0xc0660) and objects
  (`GetObjectLists` 0xc36a0 → cylinders `GetClosestIntersectingCylObject` 0xc0440, boxes
  `GetClosestIntersectingOBBObject` 0xc05f0 / `GetOBBObjectIntersection` 0xbf8c0).
* Car contact: `WCollider::PrepareRegion` (0xbe2d0) collects, for the collider's region sphere, an
  instance+strip list when its mask has bit 8 and a world-space barrier list (`GetBarrierList`
  0xc3880, 0x28-byte `WCollisionBarrierListEntry`) when it has bit 4; `WCollider::GetWorldNormal`
  (0xbd890) → `WCollisionMgr::GetWorldNormal` (0xc0e00) → `ClosestCollisionInfo` (0xbf3a0).
  `GetGroundCollision` (0xbf2d0, from `Simulate` 0x60b70) intersects a segment with the face under it.
* `WCollisionMgr::SetCollisionArticle` (0xc2ff0, from `ESetCollisionGeometry`) swaps an instance's
  geometry to article datum `ca`+n (or none), keyed by render instance: this is how doors and
  breakables change their collision.
* `WCollisionMgr::SurfaceBumpHeight` (0xbedd0) adds a small sin/cos ripple for some surfaces
  (2,3,5,10: 0.01; 4,7: 0.015; 6: -10).

## File format

### UGroup (the whole .crp)

The file is one serialised `UGroup` tree. Every entry is 16 bytes, little-endian:

| off | type | group entry | data entry (`UData`) |
|---|---|---|---|
| +0x0 | u32 | tag (FourCC, stored so it reads backwards: `PRAC` = `'CARP'`) | tag; when flags bit 0 is set the low 16 bits are an index (`'cn'+n`, `'sr'+n`…) |
| +0x4 | u32 | flags: bits 5.. = number of child groups; bit 1 = offset is relative; bit 3 = data sorted (binary search); bit 4 = groups sorted | flags: bits 8.. = size in bytes; bit 1 = relative; bit 0 = indexed tag |
| +0x8 | u32 | number of data entries | count or element index (meaning per tag) |
| +0xc | u32 | offset of the entry array **in 16-byte units** from this entry (bit 1) | offset of the data **in bytes** from this entry (bit 1) |

The entry array of a group holds its child groups first, then its data entries (`UGroup::GetArray`
0x117940, `GroupLocateTag` 0x117950, `DataLocateTag` 0x117b50, `DataCountType` 0x1179f0,
`DataLocateFirst` 0x117a60, `ResolveOffsets` 0x117cb0). The header `50524143 fa1b0000 06000000 01000000`
reads: tag CARP, flags 0x1bfa (223 groups, sorted, relative), 6 data entries, array at +16.

Top level: `Arti` groups (one per article, each with `Base`, `Name`, `as`, `ca`, `in`, `sr`… data),
`CDat` (collision), `Map ` (instances, triggers, AI, cameras…), `RNgp` (road network), `Shar`; data
`DBN `, `ELFd`, `ELFr`, `MapN`, `Sect`, `sn`.

### CDat group (collision)

| datum | size | contents | read by |
|---|---|---|---|
| `CGrd` | 36 | the `WGrid` object itself | `WGrid::Init` 0xc5fc0 → ctor 0xc57b0 |
| `ci` | 64 × count (`+8`) | `WCollisionInstance[]` | `WCollisionMgr::Init` 0xc55b0 |
| `co` | 48 × count | `WCollisionObject[]` (size 0 when a track has none) | `WCollisionMgr::Init`; `GetObjectListsGuts` 0xc34c0 |
| `cn`+i | 0x10 + 2 × entries | `WGridNode` for cell `+8` (the `count` field is the node index) | `WGrid::Init` stores `nodes[+8] = data` |
| `de`+i | 8 | dynamic grid element: u16 index, u16 0, u32 kind (0 = `ci`, 2 = `co`) | `WGrid::Init` |
| `sr`+i | string | `"CARP::<article name>"`, resolved to a pointer to that `Arti` group | `CARP::SymbolicResolver` |

**CGrd / WGrid** (36 bytes): `+0x0` COORD4 origin (w = 1.0), `+0x10` float cell size (24.0 in all four
tracks), `+0x14` float 1/cell, `+0x18` u32 rows (along Z), `+0x1c` u32 columns (along X), `+0x20`
node pointer array (runtime; junk in the file). `WGrid::Init` passes the datum and its `+0x18`, `+0x1c`,
`+0x10` straight to the constructor.

**WGridNode** (`cn`): `+0x0` u32 pointer to the node's dynamic list (0 in the file), `+0x4` u8 count[4],
`+0x8` u16 offset[4] (bytes, from `+0x10`), `+0x10` u16 indices. Kinds, by the argument the callers pass
to the iterator `FUN_000bfee0`: 0 = `ci` index (`GetInstanceListGuts`), 1 = trigger index
(`WTriggerManager::Process` 0xd04a0; the maximum index + 1 equals the `Map`/`Trgr` count in every
track), 2 = `co` index (`GetObjectListsGuts`), 3 = road network segment (no Xbox reader found; the
maximum index + 1 equals the `RNgp`/`rs` count: 143 in uw_mis11, 407 in snow2a_mis4).

**WCollisionInstance** (`ci`, 64 bytes). Rows 0 and 2 of a 3×3 rotation plus a translation form the
world→local matrix of `MakeMatrix` 0xbe8b0 (row-vector convention: `local = p.x*row0 + p.y*row1 +
p.z*row2 + t`, as `VU0_MATRIX4_vect3mult` 0x116240 computes).

| off | type | field | evidence |
|---|---|---|---|
| 0x00 | float[3] | row0 | MakeMatrix → mtx[0]; CalcPosition |
| 0x0c | float | half extent X (local) | FUN_000bf4c0/FindFaceInCInst 0xbffe0: `-[+0xc] <= local.x <= [+0xc]` |
| 0x10 | u32 | query stamp (runtime) | GetInstanceListGuts compares with and writes manager `+0x4` |
| 0x14 | float | half extent Y | GetInstanceListGuts 0xc31f0: `t.y ± [+0x14]` against the segment's y range |
| 0x18 | u8 | flags: bits 0/1 → row1 = row2 × row0 (FUN_00115cc0), else (0,1,0); bit 0 also reverses the height test; bit 1 is copied to collision info `+0x52` | MakeMatrix, FUN_000bf4c0, GetBarrierNormal 0xc0890 |
| 0x19 | u8 | unknown (0..43) | no reader found |
| 0x1a | u16 | render instance index (`WWorld+0x20` + i×0x40) | GetRenderInstance 0xbe780, SetCollisionArticle, GetName 0xbe7a0 (`'iN'`+i in `Map `) |
| 0x1c | ptr | file: tag reference `'sr'`+n → article group; after Init: the article's `ca` datum | Init 0xc55b0, SetCollisionArticle |
| 0x20 | float[3] | row2 | MakeMatrix → mtx[2] |
| 0x2c | float | half extent Z | as +0xc for local.z |
| 0x30 | float[3] | translation (world→local) | MakeMatrix(…, true) → mtx[3] |
| 0x3c | float | XZ bounding radius | GetInstanceListGuts: `max+0.25*min < r + [+0x3c]`; 0xc31f0 squared test |

Local→world is the orthonormal inverse: `world_i = row_i · (local − t)` (`OrthoInverse` 0x114e80, used by
`FUN_000beeb0` on the faces it returns). The instance centre is `CalcPosition` = `−(row_i · t)`.
The render instance's rotation is the transpose of these rows; its position differs by a fixed offset
per article (the collision local origin is the centre of the collision box), except for 1.5 % (snow)
to 2.6 % (uw) of instances, probably scaled or mirrored render instances.

**WCollisionObject** (`co`, 48 bytes; only snow2a_mis4 of the four tracks has any: 525, all thin posts):

| off | type | field | evidence |
|---|---|---|---|
| 0x00 | float[3] | position (box centre; cylinder base) | WCollisionObject::MakeMatrix 0xbe6f0 translation; GetClosestIntersectingCylObject 0xc0440 |
| 0x0c | float | radius | GetObjectListsGuts `dist < r + [+0xc]`; cylinder circle radius |
| 0x10 | float[3] | half extents | GetOBBObjectIntersection builds ±x,±y,±z from it; cylinder spans y .. y+2·[+0x14] |
| 0x1c | float | 1.0 in the file | unread |
| 0x20 | u8 | 0 = oriented box, else cylinder | GetObjectListsGuts splits the lists; CheckHitWorld sends them to 0xc1080 (OBB) / 0xc0f80 (cylinder) |
| 0x24 | u16 | render instance index: the box rotation is rows 0..2 of `WWorld+0x20`[i] | WCollisionObject::MakeMatrix; WGrid::Init |
| 0x21, 0x26..0x2f | | unknown | |

### Collision article (`ca` datum in an `Arti` group)

`ca` (index 0) is the default geometry. Some articles have `ca`+1… (6 articles in uw_mis11, 2 in jungleb_mis13b, 1 in
paris_mis01), which `SetCollisionArticle` can switch to. Everything is in the instance's local space.

| off | type | field | evidence |
|---|---|---|---|
| 0x00 | u16 | strip count | GetInstanceStripList 0xc4120, FUN_000bf4c0 loop bound |
| 0x02 | u16 | barrier array offset (bytes, from +0x20) | GetBarrierList 0xc3880, GetBarrierNormal 0xc0890 |
| 0x04 | u16 | barrier count | same |
| 0x06 | 26 bytes | unknown (mostly 0; +0xd is 0xfe or 0x7a; three int16 triples follow) | no reader found |
| 0x20 | `WCollisionStripSphere`[count] | 16 bytes each: float3 centre, u16 radius×16, u16 strip offset (bytes, from +0x20) | GetInstanceStripList, FindFaceInCInst |

**Strip** (at `ca + 0x20 + offset`): N vertices of 16 bytes, float3 position + a 32-bit word.
The word of vertex 0 is the vertex count N (FindFaceInTriStrip 0xbf0e0 `[+0xc] - 2` triangles); the word
of vertex 1 is the strip flags (`FUN_000bef30` `[+0x1c]`): bit 1 = two-sided (test `FUN_000bebd0`),
otherwise bit 0 = the first triangle uses the second orientation, and the orientation alternates per
triangle. Triangle i uses vertices i, i+1, i+2 and keeps its attributes in vertex i+2's word:

| off in word | type | field | evidence |
|---|---|---|---|
| +0xc | u8 | surface (`WSurface`) | copied to WWorldPos +0x2c (`FUN_000beeb0`); `SurfaceBumpHeight` switches on it; `ProcessPhysics` indexes `friction[]` 0x1c3728 with the WSurface byte |
| +0xd | u8 | face flags: 0x04 = ignored by the point (height) query; any bit in the query mask (manager 0xf0; WCollider `+0x64`) = ignored | `FUN_000bef30`, FindFaceInTriStrip, GetBarrierList |
| +0xe | u16 | triangle bounding radius × 16 | GetInstanceStripList (`[middle vertex + 0x1e]`) |

Surface numbers, from the `friction[k]` registrations in `InitializeBondCarGlobals` (0x61b00.., array
0x1c3728 + 4k): 0 NODRIVE, 1 PAVED, 2 GRAVEL, 3 GRASS, 4 COBBLE, 5 DIRT, 6 WATER, 7 WOOD, 8 ICE,
9 SNOW, 10 PAVED_ROUGH, 12 RAILROAD, 13 METAL; 11, 14 and 15 are not registered (the table has 16
slots). The bump cases in `SurfaceBumpHeight` (gravel, grass, dirt, rough paving; cobble, wood; water)
fit this numbering.

Winding: `FUN_000bed40` accepts a point when `(v0−v1)×(p−v1)` etc. are ≥ 0 in XZ, which is when
`(v1−v0)×(v2−v0)` has +Y. So for one-sided strips, writing triangle i as (v_i, v_i+1, v_i+2) when its
parity is 0 and (v_i+1, v_i, v_i+2) when 1 gives walkable ground an upward normal in local space; the
world transform is a proper rotation, so it stays up. Checked: every one-sided strip in snow2a_mis4
(5334) and uw_mis11 (5486) that has near-horizontal faces has them all facing up. Two-sided strips
(flags = 2) never set bit 0 and have no defined facing (standard strip alternation still holds: 92–96 %
of neighbouring triangles agree).

**Barrier** (32 bytes, at `ca + 0x20 + [+0x2]`): vertical wall quads the car and camera slide along.

| off | type | field | evidence |
|---|---|---|---|
| 0x00 | float[3] | p0: x, y min, z | GetClosestIntersectingBarrier 0xc0660: hit.y between `[+0x4]` and `[+0x14]`, XZ segment test |
| 0x0c | u8 | unknown class (0x10..0x60 in steps of 0x10) | GetBarrierNormal copies the word at +0xc to collision info +0x50, the surface slot |
| 0x0d | u8 | flags, masked like face flags | GetBarrierList, GetBarrierNormal |
| 0x0e | u16 | 0 or 0x322a | unread |
| 0x10 | float[3] | p1: x, y max, z | as p0 |
| 0x1c | float | 1 / XZ length | GetBarrierNormal: normal = ((z1−z0)·w, 0, −(x1−x0)·w); matches 1/len to 1e-7 in every barrier |

## Validation (tools/carp_collision.py)

| track | instances | articles | triangles | OBJ vertices | barriers | objects | world bbox |
|---|---|---|---|---|---|---|---|
| uw_mis11 | 453 (24 dynamic) | 159 | 129 925 | 167 253 | 44 | 0 | (−289.5, −590.8, −805.5) – (3296.0, 288.4, 1413.9) |
| snow2a_mis4 | 1505 | 457 | 32 854 | 46 456 | 6 275 | 525 (352 OBB, 173 cyl) | (−490.6, −317.9, −1188.9) – (2285.9, 96.1, 1983.5) |
| paris_mis01 | 994 | 246 | 42 594 | 66 788 | 25 015 | 0 | (−1475.8, −20.2, −2757.6) – (847.1, 59.7, 937.5) |
| jungleb_mis13b | 641 | 324 | 60 261 | 72 711 | 27 | 0 | (−2662.0, −891.2, −3287.7) – (1372.3, −0.3, 229.6) |

Every ground-truth point lies inside the bounding box. Every (cell, instance) pair in the grid
satisfies distance(cell centre, instance centre) ≤ instance radius + half the cell diagonal, which
confirms the instance transform (exceptions: 3 of 3784 pairs in uw_mis11, all one static `bigpipe`,
by 1.2–7.8 m; none of 12 402 in snow2a_mis4).

snow2a_mis4 (the car drives on the ground): at all three points the face straight below is 0.72 m
down (paved road, normal (0,1,0), instances `PTSR2_str`/`PTSR2_strCustom24`), and nothing is above.
The emulated `GetWorldHeightAtPoint` gives −43.746, −43.746 and −43.745.

uw_mis11 (the car floats inside an enclosed cave): rays in all six axis directions hit at 2–45 m
from each point. Down: 5.74, 2.37, 8.66 m (gravel, one-sided strips, normals up); the emulated height
query finds the same faces (−18.396, −14.889, −13.006). 12 of the 18 hits have normals pointing back
at the point. Of the 6 that point away, 4 are in two-sided strips (facing undefined) and 2 are
one-sided ground faces seen from below: an up-facing slope 23.9 m overhead at the first point and a
steep floor 24.4 m along +X at the third, which suggests overlapping cave pieces rather than a wrong
transform. The +Y hit at the third point is surface 11 with flag 0x10, a face the manager mask (0xf0)
ignores (probably the water surface).

## Drawing it in-engine

* Everything needed is already in memory after `WWorld::Open`: `fgCollisionMgr = *(WCollisionMgr**)0x239a70`,
  `+0x8` instances, `+0xc` count; each instance's `+0x1c` points at its `ca` datum (null if switched off).
  Hooking the end of `WCollisionMgr::Init` (0xc55b0) or reading the global each frame both work.
  Instances listed in `de` move at run time (they are re-gridded by `WGrid::UpdateDynamicNodes`
  0xc7200); whether their `ci` matrices are rewritten in place was not checked.
* Strip vertices are 16 bytes with the position first, so a strip can go to D3D as it is:
  `DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, count - 2, strip, 16)` with an XYZ-only FVF (the 4th dword is
  attributes, not a coordinate). Use a world matrix of `OrthoInverse(MakeMatrix(inst, true))`, which is
  row-vector like D3D, and `D3DCULL_NONE`. Colour by the surface byte in vertex i+2 if the draw is done
  per triangle.
* Barriers are two points each; `GetBarrierList` already produces world-space copies
  (`WCollisionBarrierListEntry`, 0x28 bytes) for a collider's region, as a cheap alternative for
  drawing only what is near the car.
* The face under the car: `WWorldPos` +0x00/+0x10/+0x20 hold its three world-space vertices after
  `FindClosestFace` (0xd31f0); `GetGroundCollision` (0xbf2d0) is called from the car `Simulate`
  (0x60b70) with it.

## Unknown or suspicious

* The `Map ` groups of these four tracks have no `iN` names, so `WCollisionInstance::GetName` returns
  `"<unnamed>"`; the OBJ names instances by index and article instead.
* Instance `+0x19`, object `+0x1c/+0x21/+0x26..`, article header `+0x06..+0x1f`, barrier `+0xc`/`+0xe`:
  no reader found.
* Surface values 11, 14 (uw_mis11) and 15 (12 956 triangles on snow2a_mis4's hills and mountains) have no
  friction name; 15 may be off-road terrain. Face flag 0x10 (uw_mis11: the water surface overhead;
  masked by 0xf0) and 0x02 are not explained.
* Instance flag bit 0 reverses the height test, but some flag-1 instances are nearly upright
  (row1.y ≈ 0.7–0.9), so the meaning of bits 0 and 1 is only partly understood.
* Road-segment meaning of grid node kind 3 rests on the count match only.
* The cylinder base is at `position.y`, while the box is centred on `position`; the snow posts
  (half height 2.88) therefore extend below the road as boxes. That is what the code tests, so the OBJ
  draws it that way.

## Why the car goes through the cave wall in uw_mis11 (the speedrun clip)

The clip: from (1338.976, -304.836, 1293.299), diving along about (0.752, -0.641, -0.153) with accelerate held,
the car passes through a gravel wall into the void. The wall is one triangle, `ci0028 Cave_Tunnel_12A_UP45`
strip 22 triangle 8, at the lower end of a tunnel piece pitched up 45°, where it meets `ci0013 Cave_BigTunnel1`.
The triangle and its bounding data are fine; the car never tests it, because the whole instance is left out of
the car's collision list.

- **How walls are hit underwater.** `SimulateGame` (0xb4a60) runs `RigidBody::CollideWithWorld` (0xb1420) each
  50 Hz step; `CollideWithGround` returns at once under water, so no height queries happen. The sub's collider
  (mask 12: strips and barriers) keeps a region around the car - centre pos + 1.1·Δ, radius
  (min(|Δ|, 1) + 2.559) × 1.1, about 3 m at 10 m/s - and `WCollider::PrepareRegion` (0xbe2d0) fills it through
  `GetInstanceList` (0xc4510) → grid cells → **`GetInstanceListGuts` (0xc43c0)**. Then 11 segments from the car's
  centre (box corners, nose, sides, pushed on by velocity × dt) are tested against the listed instances' strips
  (`WCollisionMgr::GetWorldNormal` 0xc0e00 → `FindFaceInCInst` 0xc01d0 → `FindFaceInTriStrip` 0xbf0e0). A hit
  becomes a velocity impulse (`GenerateImpulse` 0xafdb0); there is no position correction, so an unreported wall
  does nothing at all.
- **The failing test.** `GetInstanceListGuts` keeps an instance when
  `max(|dx|,|dz|) + 0.25·min(|dx|,|dz|) < R_region + inst[+0x3c]`, dx and dz from the region centre to the
  instance centre. Two errors add up here:
  1. `+0x3c` (28.626 for ci0028) is the diagonal of the local X/Z half extents. The piece is pitched 45°, so
     its local Y extent (±22.5) spreads into world XZ too: its geometry reaches **34.73 m** from its centre in
     world XZ, and 33 of its 900 vertices - all at the lower -Z end, the seam - lie beyond the radius. The wall
     triangle's vertices are 28.2, 29.7 and 32.5 m out.
  2. The octagonal distance approximation reads long in this direction: from the start point dx = 9.2,
     dz = 29.7, which it puts at 31.99 m against a true 31.08 m.

  At 10 m/s: 31.98 against 3.03 + 28.63 = 31.66, so ci0028 is rejected by 0.32 m on every step up to the wall.
  The neighbouring ci0013 is listed, but none of its triangles is on the path (the nearest is 2 m away), and the
  grid cell beyond lists no instances at all.
- **Speed.** Only the region radius depends on speed. Replaying the collider step by step
  (`tools/collision_clip/clip_sim.py`): at 2-15 m/s, and accelerating from rest at the start point, ci0028 is
  never listed and the car's centre crosses the wall; at 18 m/s it is listed on one step, 0.11 m before the
  crossing; from 21 m/s the wall is reported 2-3 m early, as normal. So the trick is to arrive slowly, which
  teleporting to the start point and holding accelerate does.
- **Where else.** 23 of uw_mis11's 453 instances have geometry beyond their radius in world XZ. The worst are the
  pitched cave pieces: ci0026/27/28 `Cave_Tunnel_12A_UP45` (6.1 m over), ci0030 `Cave_Tunnel_24A_UP45` (6.7 m),
  ci0016 `Cave_Tunnel_12A` (2.7 m). Their other seams are candidates for the same trick (not checked).
- **Fixes**, either of which makes the replay report the wall 2.6-2.9 m early at every speed from 2 to 50 m/s:
  the data - set `+0x3c` to the world-XZ reach (34.74 for ci0026-28, 61.52 for ci0030, 34.15 for ci0016;
  `world_xz_radius` in `clip_sim.py`) - or the code - a true Euclidean XZ distance in 0xc43c0.
- **To confirm at run time:** at the end of `PrepareRegion` (0xbe2d0) the collider's list (owner +0x64, entries
  +0x34..+0x38) should not contain ci0028 while approaching slowly from the start point.
