# Collision data: structures, kinds, bits and globals

Xbox layouts only (the PS2's `HITTEST_tag` is 0xa0 bytes and its `HITDATA_tag` 0x60, with 16-byte vectors). Field
names in `code` are Ghidra's or ours where they exist; descriptions are from use in the collision functions
**(decompile)** unless marked. See [README.md](README.md) for the overview and [functions.md](functions.md) for the
code.

## `HITTEST_tag` (0x8c bytes) - a query

One per query. Movers own one (`obj_tag+0xb0`, Ghidra `maybeCollision`); the one-shot wrappers build one on the
stack, zeroed, together with a zeroed 0xe4-byte fake owner object (Ghidra `someHitRelatedThingy`, the size of an
`obj_tag`) whose +0xac receives the hit list and whose +0xb0 points back at the test.

| offset | Ghidra name | type | use |
|---|---|---|---|
| +0x00 | `field0_0x0` | plane (n.xyz, d) | scratch. The mesh tests unpack each triangle's plane here (on their local copy); `Collide_PickObj`'s sphere-sphere path writes the centre difference here on the **real** test, and its capsule path copies it into the hit |
| +0x10 | `field13_0x10` | `_VECTOR` | accumulated push-out. Zeroed per frame by `Collide_Update`; `Collide_Intersect` adds each hit's world push-out; `Collide_Jointy`'s capsule push adds directly; `Collide_SphereIntersect` adds it to the caller's point at the end |
| +0x1c | `rayStartPos` | `_VECTOR` | ray / capsule start (moved by push-outs) |
| +0x28 | `rayEndPos` | `_VECTOR` | ray / capsule end (moved by push-outs) |
| +0x34 | `field16_0x34` | `_VECTOR` | end - start, written by `Collide_Pick` when the kind's high byte has 0x02 or 0x08; copied into ray hits' +0x2c |
| +0x40 | `pos` | `_VECTOR` | sphere centre for kinds whose high byte has 0x01 or 0x10 (copied to +0x4c by Pick); the mesh tests also write their closest point here on the local copy |
| +0x4c | `sphPos` | `_VECTOR` | sphere centre used by the sphere tests (moved by push-outs); overwritten by `Collide_PickObj` with each object's centre for ray and capsule queries |
| +0x58 | `sphRad` | float | sphere radius for the tree walk; ditto |
| +0x5c | `someData` | `obj_tag*` | owner: hits go on `owner->hitList` (+0xac). Read: +0xd9 bit 1 (ignore objects with +0xd9 bit 2), +0xdb object type, +0xbc type data, +0x24 position (capsule push) |
| +0x60 | `firingObj` | `obj_tag*` | ignored object 1 (also never hit if equal to the owner) |
| +0x64 | `field22_0x64` | `obj_tag*` | ignored object 2 |
| +0x68 | `someMtx` | `_MATRIX*` | set on the narrow phase's local copy: the mesh's local-to-world matrix (inverted, used to bring points in, and inverted back before the mesh test) |
| +0x6c | `cel` | `cel_tag*` | start room. A query with no cel does nothing (`Collide_Pick` returns at once); `Collide_unknown` finds one with `build_FindCel` |
| +0x70 | - | | not used by collision |
| +0x74 | `maybeCelCollisionData` | `celglist_tag*` | the current candidate's glist, set by `Collide_Intersect`; mesh tests read `glist->colldata` (+8) |
| +0x78 | `maybeToleranceOrWidth` | float | radius for spheres and capsules; for rays `Collide_Pick` replaces it with max(length, 1.0) (`Collide_RayIntersect` sets -10.0 first) |
| +0x7c | `someDamageAmount` | float | copied into every hit's +0x08 and into mirrored hits |
| +0x80 | (u16) | kind | see "Kinds". The high byte (+0x81, `field33_0x81`) doubles as shape flags |
| +0x84 | `maybeCollisionMaskTypes` | u16 | what to skip; see "Mask bits" |
| +0x86 | `field37_0x86` | u16 | behaviour flags; see "Flag bits". OR'd (low byte) into each hit's +0x45 |
| +0x88 | `field39_0x88` | u16 | result flags, zeroed per frame by `Collide_Update`. Bit 0 set by `Intersect_CylGeom` (floor contact, **inference**); OR'd back from the local copy by `Collide_Intersect` |

The gather and the capsule test read +0x84 as a u32, so "mask 0x10000" there is flags (+0x86) bit 0x01 and
"0x100000" is flags bit 0x10.

## `HITDATA_tag` (0x50 bytes) - a hit

Pool nodes from `HitHeap`, zeroed on allocation. Our `Collide.h` declares it (calling +0xc `unknown1f`).

| offset | Ghidra name | type | use |
|---|---|---|---|
| +0x00 | `prev` | `HITDATA_tag*` | list links (an `LLNODE_tag`) |
| +0x04 | `next` | `HITDATA_tag*` | |
| +0x08 | `damageAmount` | float | the test's damage (+0x7c), or the mover's for mirrored hits |
| +0x0c | `maybeDist` | float | **sort key**. The broad phase stores a coarse distance (squared, or a box `t`, or `index*0.1-1000` for a room's own mesh, or 1.0 for a sphere-in-box); the narrow phase replaces it with the real one. **1e8 (0x4cbebc20) = no hit**, dropped by `Collide_Sort`. Ray hits on bones with the start inside the box get -1.0 |
| +0x10 | `normalPlane` | plane (n, d) | surface plane, normal in world space after `Collide_Intersect` (d stays local) |
| +0x20 | `someOtherVectorUsedInBreak` | `_VECTOR` | not written by collision code |
| +0x2c | `maybehitDirection` | `_VECTOR` | ray direction: the delta from the broad phase, normalised by `Collide_Intersect` when the kind's high byte has 0x02; for capsules the negated normal; for bones the normal |
| +0x38 | `hitPosition` | `_VECTOR` | contact point, world space |
| +0x44 | `materialAndFlags` | u8 | the triangle's surface byte (0 for bones) |
| +0x45 | (u8) | flags | 1 = mirrored onto the hit object by `Collide_Update`; 2 = see-through candidate (`obj->effectFlags & 0x40` or `cel+0x84 & 0x40`); plus the low byte of the test's flags. The mesh tests skip the push-out when `& 6` |
| +0x46 | `hitBoneIdx` | u16 | bone index from `AnimGetCollData` entry +0x48 |
| +0x48 | `hitCel` | `cel_tag*` | the static cel hit (NULL for objects) |
| +0x4c | `hitObj` | `obj_tag*` | the object hit (or, in a mirrored hit, the mover) |

## Kinds (`HITTEST+0x80`)

| kind | high-byte flags (+0x81) | made by | broad phase (Pick / PickObj) | narrow phase (Collide_Intersect) |
|---|---|---|---|---|
| 0x0101 | 0x01: sphere = (`pos`, width) | `Collide_SphereIntersect`, movers (`Car_Update` writes it) | sub-cels: sphere vs bounding sphere, or vs bbox for identity cels (dist 1.0); objects: sphere vs sphere | characters: `Collide_Jointy` (bone boxes, world space); `obj+0xd9 & 4`: sphere vs the object's sphere; else `Intersect_PointGeom` |
| 0x0100 | 0x01 | `Collide_SphereIntersect` with its 8th argument set | as 0x0101 | no mesh: objects get `sqrt(dist) - (r + width)` clamped at 0; cels keep dist = width |
| 0x0201 | 0x02: delta and length computed | `Collide_RayIntersect` (and `Collide_LineOfSight`) | non-identity sub-cels: segment vs bounding sphere; identity sub-cels: `Intersect_RayBox` on the bbox; objects: segment vs sphere | characters: `Collide_Jointy`; else the ray test 0x29fa0 |
| 0x0800 | 0x08: delta computed | `FUN_0002c6f0` (rigid bodies); movers - player and drones, by the character push in `Collide_Jointy` (**inference**) | sub-cels and objects: segment vs bounding sphere of radius r*1.5 widened by width | character vs character capsule: `Collide_Jointy`; else `Intersect_CylGeom` |
| 0x1000 | 0x10: sphere = (`pos`, width) | not seen | as 0x0101 | as 0x0100 |
| 0x2000 | 0x20 | `Collide_unknown`, which runs the broad phase as **0x0800** and switches to 0x2000 after | (as 0x0800) | the triangle gather 0x29a30 into `TriHeap`; leaves no hit (dist stays 1e8) and skips the surface filter. Characters give nothing |
| 0x0400 | - | not seen | objects as sphere vs sphere; no sub-cels | dist forced to 1e8 - no hit ever |

Every kind also tests each reached room's own mesh (unless mask 0x40) and walks `ForcedList` (unless mask 0x10).

## Mask bits (`HITTEST+0x84`) - what to skip

| bit | where | effect |
|---|---|---|
| 0x0001 | `Intersect_CheckIfFlagsMatch`, gather | skip surface types 0x0d and 0x0e (low 6 bits) |
| 0x0002 | same | skip surface type 0x10 |
| 0x0004 | `Collide_PickObj` | skip objects with `effectFlags & 0x10`. `Collide_LineOfSight` always sets it |
| 0x0008 | `Intersect_CheckIfFlagsMatch`, `Collide_PickObj` | skip see-through: hit flag 2, or surface bit 6 or 7; skip objects with `effectFlags & 0x40` |
| 0x0010 | `Collide_Pick` | skip all objects (rooms' lists and `ForcedList`) |
| 0x0020 | `Collide_Pick`, `Collide_PickObj` | skip sub-cels with `cel+0x84 & 0x1000`; skip objects with `effectFlags & 0x1000` |
| 0x0040 | `Collide_Pick` | skip the rooms' own meshes |
| 0x0080 | `Collide_Pick` | skip static sub-cels |
| 0x0100 | `Collide_PickObj` | skip drones and dead drones |
| 0x0200 | `Collide_PickObj` | skip bullets |
| 0x0400 | `Collide_PickObj` | skip players and dead players |
| 0x0800 | `Intersect_CheckIfFlagsMatch` | skip surfaces with bit 6 (0x40) |

Every object with `effectFlags & 0x20` is skipped whatever the mask; objects with `effectFlags & 0x30000000`
(0x10000000 = on `ForcedList`) are skipped in the rooms' lists (the forced ones are tested once from the list).
`Collide_PickObj` builds the effect-flag mask as `(((m&0x20)<<4 | m&8 | 4) << 1 | m&4) << 2`.

Masks seen at call sites: 0x70c (`build_PointOnFloor`), 0x18 (`Searchlight_Update`), 0x8 (`SpaceLaser_Update`),
0x1d (`Collide_PlaceObject`), 0x307 and 0xd (the gathers), `param | 4` (`Collide_LineOfSight`).

## Flag bits (`HITTEST+0x86`)

| bit | effect |
|---|---|
| 0x0001 | gather: also take triangles nearly parallel to the segment (closest point to the centroid) |
| 0x0002, 0x0004 | (inference) via the OR into hit +0x45: no push-out in the sphere and capsule tests |
| 0x0008 | `Intersect_PointGeom`: two-sided (accept plane distances down to -radius) |
| 0x0010 | first hit only: the drivers stop at the first candidate that intersects (list order) and mark the rest 1e8; the mesh tests stop at the first acceptable triangle; the buggy surface-bits skip (see functions.md) |
| 0x0020 | `Collide_Update`: mirroring passes through hits whose surface byte is exactly 0x10 |

Seen: 0x10 from `Collide_LineOfSight`; 1 from `Collide_PlaceObject`'s gather; 4 from the shadow gather.

## Surface byte

Per triangle, `COLLDATA+8` array: bits 0-5 type (0x0d/0x0e and 0x10 have mask bits of their own; the type also
indexes `EffectInfo` for debris and drives footsteps and impacts elsewhere), bit 6 see-through-ish (mask 0x800,
mask 0x8, `Collide_Update` passes through), bit 7 "50% pass-through" for bullet mirroring (`Rand_Rand`).

## `COLLDATA_tag` and `COLLBOX_tag` as the code uses them

`COLLDATA_tag` (0x16 used, allocated 0x18), from `celglist_tag+8`:

| offset | ours (`celglist.h`) | use |
|---|---|---|
| +0x00 | `collBoxes` | `COLLBOX_tag[]`, root at [0]. The walkers take a pointer to this field (`COLLBOX_tag**`) as their first argument |
| +0x04 | `dataStartB` | triangles, 8 bytes each: u16 i0, i1, i2 (word offsets of the vertices in the leaf's chunk), u16 plane (word offset of the plane) |
| +0x08 | `dataStartD` | surface bytes, one per triangle. `Collide_Pick` requires it non-NULL |
| +0x0c | `dataStartC` | chunks, 64 bytes each: the word pool of s16 vertices and planes |
| +0x10, +0x12, +0x14 | `sizeofC`, `countB`, `numCollBoxes` | not read by the queries (`Debris_Create_Loop` reads `numCollBoxes`) |

`COLLBOX_tag` (0x30): +0x00 min, +0x0c s16 childA (negative = leaf), +0x0e s16 childB (inner: second child; leaf:
first triangle), +0x10 max, +0x1c s16 triangle end (exclusive), +0x1e u16 chunk index, +0x20 offset xyz and +0x2c
scale (Ghidra's `VertexTransform` view). A leaf's vertex = `s16[3] * scale + offset`; its chunk is
`dataStartC + 64*chunk`; a plane = `s16[3] * 6.103888e-05` (1/16383) and `f32 d` at word +3.

## Cels and portals as collision reads them

`cel_tag` (0x94), fields used here (Ghidra names where it has them):

| offset | use |
|---|---|
| +0x08 | room: head of its object list (linked through `obj_tag+0x08`, Ghidra `maybePrevInCel`) |
| +0x18 | room: head of its static sub-cel list; sub-cel: next sub-cel |
| +0x28 `portal` | room: first portal |
| +0x38 `collisionData` | the cel's `celglist_tag*` (so `+0x38 → +8` is the `COLLDATA_tag*`) |
| +0x3c `bitsFromPlacementTag` | bit 0x8000000 (`>> 27 & 1`): mesh is in world space (identity). Room bit 0x40000 |
| +0x40 / +0x4c | bbox min / max (world) |
| +0x58 / +0x64 | position / rotation, for `RotTransMatrix` |
| +0x70, +0x7c | bounding sphere centre (`somePosition`) and radius, read together as a sphere |
| +0x84 | u32 flags: 0x20 no collision, 0x40 see-through (hit flag 2), 0x1000 skipped under mask 0x20 |

`portal_tag` (0x7c, from `build_alloc_portal` 0x20730):

| offset | use |
|---|---|
| +0x00 | next portal of the same room (Ghidra `someOther`) |
| +0x04 | partner portal (the same opening seen from the other room) |
| +0x08 | the room it leads to |
| +0x10 | plane: normal (3 floats), d at +0x1c |
| +0x20 | query stamp: `Collide_StraddleCels` writes `GameState+4` here and on the partner |
| +0x28 | the quad, 4 x vec3 (12 bytes each); triangles from the table at 0x163b80: (0,1,2) and (2,3,0) |
| +0x58 | quad centre (closest-point fallback for parallel capsules) |
| +0x64 / +0x70 | quad bbox min / max, widened by 0.1 |

`obj_tag` fields used: +0x00/+0x04 list links (`ForcedList`; loop stops if a node's next is itself), +0x08 next in
room, +0x20 room, +0x24 position, +0x60 centre and +0x6c radius (bounding sphere), +0x70 matrix, +0xac hit list,
+0xb0 `HITTEST_tag*`, +0xb4 glist, +0xbc type data (drone: float at +0x38c decides one-way pushes; bullet:
weapon definition at +0x38, its u16 flags at +0x10; player: velocity at +0xc), +0xcc effect flags, +0xd6 renderer
type (bit 8 = animated character), +0xd9 bits 1/2/4 (see `Collide_PickObj` and `Collide_Intersect`), +0xdb object
type.

## Globals

| address | name (Ghidra) | type | use |
|---|---|---|---|
| 0x1dc9a0 | `ColBoxs` | `{COLLBOX_tag*; float t}[600]` | leaves found by the last tree walk; `t` only from the ray walk. No bounds check: entry 600 lands on `HitHeap` |
| 0x1ddc60 | `HitHeap` | `LLISTINFO_tag` (0xc) | free list of hits, element size 0x50, grown 0x40 at a time |
| 0x1ddc6c | `BoxCnt` | u32 | entries in `ColBoxs` |
| 0x1ddc70 | `SrtList` | `HITDATA_tag*[~1005]` | `Collide_Sort`'s array |
| 0x1dec10 | `DeltaP` | `_VECTOR` | push-out of the current candidate (local space in the mesh tests, rotated to world by `Collide_Intersect`) |
| 0x1dec1c | `Ident` | u8 | current candidate's mesh is in world space (cel bit 0x8000000) |
| 0x1dec24 | `HitAllocCnt` | u32 | hits allocated; ours declares it `static` in `Collide.cpp` at its XBE address |
| 0x1dec28 | `TriAllocCnt` | u32 | `TriHeap` capacity in vertices (100, +100 per growth, refuses to grow past 500) |
| 0x1dec2c | (`DAT_001dec2c`) | u32 | vertices in `TriHeap` (multiple of 3); reset by `Collide_unknown` |
| 0x1dec30 | `TriHeap` | `_VECTOR*` | gathered triangles, world space |
| 0x1dec34 | (`DAT_001dec34`) | u16 | `Intersect_Portal`'s triangle loop counter, a global for no reason |
| 0x1dec38 | (`DAT_001dec38`) | `cel_tag*[127]` | rooms reached by `Collide_StraddleCels` (returned to the caller) |
| 0x1df41c | `ForcedList` | `LLISTINFO_tag` | objects straddling rooms; `.head` read |
| 0x1f6584 | `GameState+4` | u32 | portal query stamp, incremented by `Collide_Pick` and again by `Collide_StraddleCels` |
| 0x1f6674 | `glb_world` | | passed to `build_FindCel` |
| 0x260018 | `MPSettings.isMultiplayer` | | capsule push strength (1.0 single player, 0.5 multiplayer) |
| 0x163b80 | (`DAT_00163b80`) | u16[2][3] | portal quad triangle indices {0,1,2},{2,3,0} |
| 0x15d2e8-0x15d510 | | floats | constants (1e8, 6.103888e-05, 0.05, 0.1, 0.0002, 0.999, 1.5, -1000, ...) |

## Calling conventions

See the table in [README.md](README.md#calling-conventions-a-porting-hazard). Summary: everything pops nothing;
`Intersect_AABB_SweptVolume`, the three sphere helpers, `Collide_Sort`, `Collide_PickObj`, `Collide_Jointy`,
`Collide_Intersect` and the core driver take register arguments; `Coll_AddHitToList` returns its hit in EAX;
`Intersect_PointOOBox` returns its bool in AL with junk above it.
