# spec-world: how a level is assembled, and its static world

This spec covers a level as a whole: which bundle entries the loader parses and in what order, the placement
records, the static world (rooms and static objects) with its exact transforms, rooms and portals, collision,
paths and AI paths, and the level-wide blocks (ambient radiators, map sounds, particle definitions, fog, sky).
The per-type dynamic objects are covered in `objects-*.md`.

All addresses and offsets are from the Xbox `default.xbe`. The tool is `tools/level/parse_level.py` (section 9).
Confidence is **confirmed** (read in the code and checked against the data), **likely** (read in the code, or
strongly implied by the data), or **guess**.

## 0. Summary

- **Only the map file places objects.** A bundle's graphics packs (entry types 0, 2 and 11) are parsed first, in
  directory order. The map file (type 1) is parsed last. Every placement a pack carries is decoded with
  `doCreation = 0`, so it creates nothing. After the whole bundle has loaded, `ResetMap_Load` runs the **map
  file's** placements again with `doCreation = 1`, and those are all of the level's dynamic objects. Pack
  entities reach the world only through the map's placements, which name them by hashcode: 1,863 map placements
  use an entity defined in another file. The 06 scripts (type 7) and 08 menus (type 8) only register themselves.
  They place nothing. **(confirmed)**
- **Static world.** Any placement whose type has bit 0x8000 becomes a `cel_tag` straight away. In the data the
  static types are:
  - **0xc047 rooms** (649) and **0xc023 underwater rooms** (6). Their position is forced to 0 and their mesh is
    already in world space.
  - **0x8019 static objects** (4,725).
  - One **0x8017** water surface.

  Objects are drawn with **`M = T(pos) · Rz(rot.z) · Ry(rot.y) · Rx(rot.x)`** (RotMatrixZYX), with the standard
  right-handed rotation matrices. That is Blender Euler mode `XYZ` in the engine's y-up frame. The quat at
  +0x24 is the **inverse** of this rotation, and the vec3 at +0x34 is a scale that is (1,1,1) on every static
  placement and is never read for statics. **(confirmed)**
- **An object is drawn only if it is linked to a room.** A static object must be linked to a room by
  `build_link_objects_to_rooms`, and its entity must have a mesh and lack entity flag 0x10. Across the 32
  bundles, 4,848 static cels are drawn. Rendered top-down, level 07000001 shows rooms that tile seamlessly and
  furniture where it belongs. The collision layer renders the same picture. **(confirmed)**
- **0x2000 placements are never used.** The placements of type 0x2031 (1,833 with packs deduplicated by
  name as in the brief; 5,392 by content) have bit 0x2000 and not 0x8000. Neither handler takes them, so the
  game ignores them. They are unplaced copies, such as the `*_damaged` alternate states. **(confirmed)**
- **Rooms and portals.** A room is a 0xc0xx cel. A portal (block 0x21) joins two rooms, which it names by the map
  file's entity index. Visibility is a portal walk from the viewer's room, capped at 64 cels. Every static object
  belongs to exactly one room, its link. **(confirmed)**
- **Collision** decodes fully:
  - a BVH of 48-byte boxes;
  - int16 vertices quantised per leaf;
  - 8-byte triangles;
  - quantised planes;
  - one surface byte per triangle.

  Across the distinct files that is 2,031,353 triangles in 6,455 blocks, every one inside its leaf box. The
  collision uses the entity's own space and is placed with the cel's transform, which is the identity for world
  geometry. **(confirmed)**
- **Paths and AI paths.** A path (0x19) is a list of 28-byte frames `{vec3 pos; quat}` in world space. The
  placement's +0x40 equals the path's frame count in all 586 uses. The AI paths (0x05) are route graphs and area
  bounds, with node positions and links. **(confirmed)**
- **Level-wide data.**
  - Ambient radiators (0x26) are point lights with a position, a range and an RGB colour.
  - Map sounds (0x28) are positional looping sounds.
  - The particles block (0x30) holds definitions, not placements.
  - **Fog is hard-coded per level** in `psiFog`.
  - The sky is the SkyObject placements, drawn first with fog off. See the SkyObject agent's spec.

## 1. How a level is assembled (question 1)

### 1.1 Order

`ResetMap_Load` 0xbfb60 (loadState 3) runs these steps in order:

1. It calls `hashtable_initialise`, `Mem_Init` and so on.
2. It calls `LoaderLoad(1, level, 0, 0)` 0xbe910 until that returns false. **Each bundle entry**, in directory
   order, goes to `LoaderProcess` 0xbe7b0:

   | entry type | handler | what it does to the world |
   |---|---|---|
   | 0, 2, 11 (11 sets MemType 1 first) | `parsemap_parsemap(hash, 0)` 0xa6b20 | object pack: entities, textures, and static cels from its own 0x1a (section 3) |
   | 1 (always the last entry) | `AnimPostLoadInit()`, then `parsemap_parsemap(hash, 1)` | the map file: rooms, static objects, portals, paths, AI paths, radiators, sounds |
   | 3, 4, 5 | AnimLoadFile | skins, sequences, anim scripts |
   | 6 | AnimSkeletonProcess | skeletons |
   | 7 | `Script_Load(hash, 0, 0, buf)` 0xc1550 | with a file buffer it only **registers** the script (header, sections) in the hashtable. It runs nothing and places nothing |
   | 8 | MenuManager_Load | UI |
   | 12 | adds a `LoadableFiles[]` entry (animation-sequence streaming: `isLoadable`, `LoadableLoad`/`LoadableReload`, which only delete and reload hashtype 0x04) | nothing |

3. `build_link_objects_to_rooms(world cels)` 0x20f70 links the static objects to the rooms (3.5).
4. `parsemap_reset_to_block_memory_discard`.
5. **`parsemap_block_map_data_dynamic(&DynamicBH, DynamicPtr, 1)`** 0xa66f0. `DynamicBH`/`DynamicPtr` were last
   set by the map file's 0x1a block, so this call creates every dynamic object of the level
   (`parsemap_create_dynamic_objects`, `objects-*.md`).
6. `build_LinkDoors2Portals`, `AIPath_BindNodes`, the Mem_Shrink of the discard area, `Player_Start`,
   `Drone_PostLoad_Init`, `Cameras_PostLoad` and so on.

### 1.2 Per file (`parsemap_parsemap`)

- **State 0** loads the file.
- **State 1** calls `parsemap_parsenextblock` until the END block. `parsemap_handle_block_id` 0xa6820 handles the
  blocks as they come:
  - 0x0e **map_header** 0xa5eb0 allocates `pCelList` (entityCnt × 0x3c), the path pointer array `m_paths`
    (pathCnt × 4, 0x274b4c) and `map_tag`. `MapEntityCount = entityCnt`.
  - 0x04 **entity_params** adds each entity to the hashtable as it is parsed.
  - 0x19 **path**: `*m_ppaths++ = payload` **only when isMap**. Packs have 27 of these blocks; they are ignored.
  - 0x1a **map_data**: stores `DynamicBH`/`DynamicPtr`, then runs **`parsemap_block_map_data_static` 0xa6550
    immediately**.
  - 0x21 portals, 0x27 alternate glists (the "LOD" block), 0x2c hashlist, 0x2e/0x2f collision.
  - 0x30 particles, **for packs too**: 450 particle definitions sit in packs.
  - 0x05 AI paths (AIPath_Parse, no isMap check; only map files have them).
  - 0x26 radiators and 0x28 map sounds, **only when isMap**. Packs hold 27 radiator blocks and 1 sound block,
    and the game ignores them.
- **State 2:** for **packs only**, `parsemap_block_map_data_dynamic(..., doCreation 0)`.
  `parsemap_create_dynamic_objects` returns without creating anything when doCreation is 0. The one exception is
  `Searchlight_Create`, which is called regardless, and no pack holds a Searchlight placement.
- **State 3** (packs only) Mem_Shrinks the part after block 0x1c. The map keeps its placements and paths in memory
  for step 5 above.

`parsemap_parsemap` is called only from LoaderProcess. `map_data_dynamic` is called only from `parsemap_parsemap`
(doCreation 0) and `ResetMap_Load` (doCreation 1). `LoaderLoad` is called only from ResetMap_Load: for the level,
and for 07000500 in the FMV/background path. **Object-pack placements therefore never create objects**, whether in
a loadable or streamed section, on a reload, or in a second pass. **(confirmed)**

### 1.3 What the object packs' own placements are

Across the distinct files (deduplicated by content: many pack names have different contents in different bundles):

- 5,007 are 0x2031 records with no keys.
- There are dynamic-type records: Pickup 114, Lock 69, FuseBox 46, Switch 46, MPObject 44 and so on, 27 of them
  with path fields.
- 21 are 0x8019 static records (27 over all bundles), plus 5 records with type 0xffffffff.

All of these sit at or next to the origin: they are the pack's own editor scene. The dynamic ones are never
created. The static ones do create cels, at the pack's local coordinates: `OBJECT_Bleeper`, `SniperRifle`,
`LITTLE-PEEP-HATCH` and so on, at (0,0,0). They are drawn only if some room contains the origin and passes the link
test. None does in the approximation of 3.5, so they are never drawn. The 0xffffffff records have bit 0x8000, so
they too become cels, with type bits 0xffff. Such a cel is neither a room nor linked, so it is never drawn.
**An exporter should take dynamic objects from the map file only, and static cels from every file (in practice all
drawn ones come from the map).**

### 1.4 Other things that place or move objects

These belong to other areas, listed here so nothing is missed:

- ScriptPlayer placements run the registered 06 scripts, which move objects and cameras.
- The subtitles hack in ResetMap_Load: for FMV levels it calls `SP_Create(0, 0, 0x06000xxx ...)`.
- Paths are followed by Copter, Door, Lock, Sensor, SS, Hurt, Trigger and ThirdCam (the callers of
  `Spline_Interp` 0xd8df0).
- Player start positions come from placements.

Nothing else in the bundle holds positions: 08 menus, type 12 loadables and the hashlist carry none.

## 2. Placement records (block 0x1a)

Each record is `0x4c + 8n` bytes, and the records tile every block exactly:

| off | type | field | notes |
|---|---|---|---|
| +0x00 | u16 | celEntityNum | index into the **same file's** entities |
| +0x02 | u16 | padding | uninitialised garbage, never read |
| +0x04 | u32 | glistHashcode | 0xffffffff means use `pCelList[celEntityNum]` |
| +0x08 | u32 | placementType | see below |
| +0x0c | vec3 | pos | |
| +0x18 | vec3 | rot | Euler angles in radians |
| +0x24 | quat | quat | x y z w. **The inverse of the Euler rotation** (2.2) |
| +0x34 | vec3 | scale | (1,1,1) on all static placements; AI volumes scale their glist bbox by it (objects spec) |
| +0x40 | u16 | pathFrames | copied to level_tag+2 **only when non-zero**; equals the frame count of path `pathIndex` in all 586 uses |
| +0x42 | u16 | pathRate | copied to level_tag+4 |
| +0x44 | u16 | pathIndex | level_tag+8 = `m_paths[pathIndex]`, a pointer to that 0x19 payload |
| +0x46 | u16 | padding | garbage |
| +0x48 | u32 | n | |
| +0x4c | n × {u32 key, u32 value} | | `level_tag` dword at `0x2c + 4*key`. The level_tag is 0xb0 bytes, so keys 0-32 fit; Drone uses up to key 32 |

### 2.1 Type word

| value | meaning | handled by |
|---|---|---|
| `& 0x8000` | static cel (0xffffffff counts too) | `map_data_static`, as soon as the block is parsed |
| `& 0x4000` (with 0x8000) | room (0xc023, 0xc047) | |
| `& 0x2000` without 0x8000 | unplaced: nobody takes it | ignored |
| `(t & 0xa000) == 0` | dynamic, `id = t & 0xff` (the `Place_*` enum) | `map_data_dynamic`, map file only (1.2) |
| 0xffffffff | "none" (packs only) | becomes an undrawn cel (1.3) |

No other bits occur (validated).

### 2.2 Quaternion vs Euler (confirmed)

`Quat_QuatToMat` 0xd5800 is the standard conversion: `m[1] = 2(xy + zw)` is R[1][0]. The data always gives
`quat = conj(q(rot))`. For yaw-only records the quat is `(0, -sin(y/2), 0, cos(y/2))`.

- Over the 876 rotated static placements, `quat_matrix(conj(quat)) == Rx·Ry·Rz(rot)` in every case. That includes
  the 17 with multi-axis rotations.
- Over the 3,211 rotated dynamic placements, 3,074 agree.

**Use `rot` through the right Euler builder. Do not use the quat directly**: that mirrors every yaw. The quat is
read only by the types that ask for it (GunImp, GunTurret, Emitter; objects spec).

### 2.3 Glist resolution (confirmed; all 21,725 records resolve)

| handler | glistHashcode == -1 | otherwise |
|---|---|---|
| static | `celEntityNum <= MapEntityCount` → `pCelList[celEntityNum]`. **Off by one**: == count reads one past the array, but it never happens | `hashtable_getitem(hash)`. Only files parsed so far are visible (this file's entities included, since 0x04 precedes 0x1a). A NULL result would crash on `+0x38`; never in the data |
| dynamic | `celEntityNum < MapEntityCount`, else NULL | `hashtable_getitem(hash)`. For the map pass, everything is loaded |

A dynamic object is created only when `(type & 0xa000) == 0` **and** the glist is not NULL. The hashtable is a
sorted array. `hashtable_additem` does not check for duplicates: a duplicate insert would use a stale index. No
bundle defines a hashcode twice (checked).

## 3. The static world (question 2)

### 3.1 From placement to cel: `parsemap_block_map_data_static` 0xa6550

For each record with type bit 0x8000 and a glist:

1. `cel = build_alloc_cel(world head)` 0x20700 allocates 0x94 bytes. `init_cel` sets the defaults: ambient
   0x50 × 3, `cel+0x93 = 0x10`, `shearHeight = -500`, and bbox (-1..1).
2. The cel is prepended to `glb_world->cels`, so the list runs newest first through `nextCelOfWorld` at +0x14.
3. The fields are filled in:
   - `cel+0x38 = glist`
   - `cel+0x40..0x57 = glist bbox` (celglist+0x1c..0x33, untransformed)
   - `cel+0x84 = glist flags` (celglist+0x38 = the entity flags)
   - `cel+0x58 = pos`, `cel+0x64 = rot`
   - `cel+0x3c = type & 0xffff`
4. The level_tag keys are built exactly as for dynamic objects.
5. `parseentityfixup_entity(cel, &flags, level_tag)` 0xa4790 runs (table below).
6. `parseentity_transform_bounding_box` 0xa4b60 computes `cel+0x70 = RotMatrix(rot) · sphere.centre + pos`
   (**Rx·Ry·Rz order**) and `cel+0x7c = sphere radius`. These are used for culling and for room linking.

`parseentityfixup_entity` for the types that occur. Its other cases, such as 0x34 and 0xd8-0xf3, need a type
without bit 0x8000, so they are dead code from this handler.

| type & 0xffff | count (distinct) | name | cel bits after fixup | position | keys → cel |
|---|---|---|---|---|---|
| 0xc047 | 649 | **room** | `0x0804c047` (0x40000 = room, 0x8000000 = identity collision), plus `0x10000000` if key3 | **forced to (0,0,0)**; the mesh is in world space | k0-2 ambient RGB (+0x8a/8c/8e; all 0 → 0x3c grey), k3 **indoor** (0x10000000: no visible breath, `Env_Breath`; the sound listener's reverb on, `Sound_UpdateListeners`), k4 **music event** (+0x91: `Music_Event(id, 1)` while the player is inside, `Player_Update`), k5 switch channel (+0x92), k6-8 **alternate ambient** RGB used while that switch channel is on (+0x8b/8d/8f; `build_LinkToRoom` picks `(&ambient)[switch_channels[ch]]`), k9 **sound environment** (+0x88, s16 → `SFXSetEnvironment`; values 0-100; > 0x13 also turns the reverb on). `shearHeight = -500` |
| 0xc023 | 6 | **underwater room** (`waterarea1`, `southgarden_underwater0/1`, `Water_pit` ...) | `0x0804c023`, plus 0x10000000 if k3 | forced to 0 | k0-2 ambient, k3 indoor, k4 music event; `+0x88 = 0x9898`; **shearHeight = bbox max y** (the water surface). View_AddCels does not draw 0x8018 children while the viewer is in a 0xc023 room |
| 0x8019 | 4,725 | **static object** | 0x8019, plus 0x8000000 when pos and rot are both exactly 0 | pos | `cel+0x93 = k1 << 4 | k0` (default k0 0, k1 1). `FUN_000d9bd0` (called by View_DrawCelGlist) turns it into the draw alpha: mode `k0 & 3` = 0 opaque, 1 `alpha = 255 - k1*frame`, 2 `alpha = k1*frame` (8-bit wrap: pulsing or scrolling transparency); **likely** |
| 0x8017 | 1 (`bathhousewater`) | **water surface** | 0x8017 (+0x8000000) | pos | when linked it sets its room's `shearHeight = its bbox min y` (build_link_objects_to_rooms) |
| 0x8018 / 0x803d | 0 | (0x803d → 0x28019) | | | not in the data |
| other (0xffff) | 5 records in packs | | unchanged | | never linked or drawn |

### 3.2 Drawing: `View_DrawCelGlist` 0xda670 (confirmed from the code)

```
FUN_000d9bd0(cel)                       lights (Lights_CalcClosestLights with mask = entity flags) + alpha (+0x93)
if !(entity flags & 8):  M3 = RotMatrixZYX(rot)             else: M3 = rotation part of the viewer matrix (billboard)
if  (entity flags & 4):  M3 = M3 · diag(1+rand(.05), 1+rand(.05), 1+rand(.05))   (per-frame scale jitter)
M = [M3 | pos];  psiDrawObjectMatrix(glist, M)
```

`RotMatrixZYX` 0xd6710 writes `m[0] = cz·cy, m[4] = cz·sy·sx - sz·cx, m[8] = cz·sy·cx + sz·sx, m[1] = sz·cy,
m[5] = cz·cx + sz·sy·sx, m[9] = sz·sy·cx - cz·sx, m[2] = -sy, m[6] = cy·sx, m[10] = cy·cx`. The engine's
`_MATRIX` stores the image of x in m[0..2], of y in m[4..6], of z in m[8..10], and the translation in m[12..14]
(`MatrixMultiplyVector` 0xd7500; `psiDrawObjectMatrix` 0xdde50 transposes it into the D3D constant registers).
So in column-vector form:

```
world = T(pos) · Rz(rot.z) · Ry(rot.y) · Rx(rot.x) · v
Rx(a) = [[1,0,0],[0,c,-s],[0,s,c]]   Ry(a) = [[c,0,s],[0,1,0],[-s,0,c]]   Rz(a) = [[c,-s,0],[s,c,0],[0,0,1]]
```

This is Blender Euler mode `XYZ` in the engine's (y-up) frame. For Blender's z-up frame, conjugate by the
axis change `(x, y, z) → (x, -z, y)`, or just give glTF the matrix and let its y-up convention do the rest.
`parse_level.rot_zyx(rot)` and `Cel.world` implement it.

**The three Euler builders:**

- `RotMatrix` 0xd6560 and `RotTransMatrix` 0xd6630 build **Rx·Ry·Rz** instead. They are used for the cel's
  bounding sphere (3.1), the cel's collision transform (5.3) and sky objects (7.5).
- For a single-axis rotation both orders agree. That covers all static placements except 17 in 07000009:
  16 `OBJECT_Light_Small` with rot `(-1.553, 0, π)` and 1 `OBJECT_Light`.
- For those 17, the quat (2.2) says the editor meant Rx·Ry·Rz, but the game draws Rz·Ry·Rx. The difference is a
  0.13 m shift and a mirrored orientation of a symmetric ceiling lamp. 3 of the 17 are not linked to a room and
  so are not drawn at all.

**To reproduce the game, use rot_zyx for drawing.**

### 3.3 Transforms in the data (validated)

- **Rooms:** 655 across the bundles. Their pos and rot are 0, except 2 rooms whose non-zero pos the fixup
  overrides. The room mesh is in world coordinates.
- **Static objects:**
  - 1,866 drawn cels are translated and 834 rotated. Almost all rotations are yaw only.
  - The scale is (1,1,1) and the quat equals conj(rot) on every static record.
  - `EntryHall_bonzia` has pos `(-0.05, 0.42, -0.20)`: a local nudge of a mesh already modelled in place.
    Translated pieces come in both styles: meshes modelled at the origin and moved, and meshes modelled in place
    and nudged. Both are handled correctly by the same `T·R` rule.
- **Render check (07000001, HendersonA, the first mission):** `parse_level.py obj 07000001` writes 103 pieces:
  10 drawn rooms and 93 objects (11 rooms, one of them an empty-mesh helper). Top-down renders are in the
  scratch folder `level/world/` (`07000001_cut.png`, cut at y = 7.8). They show a mansion floor plan whose rooms
  abut exactly, with tables, chairs, cupboards, plants and a garden in place. The collision layer
  (`07000001_coll_cut.png`) is the same picture. `07000009_persp.png` shows the Phoenix tower building with its
  rotated palms and lamps.

### 3.4 Entity flags (celglist+0x38 → cel+0x84) as the static draw uses them

| bit | count on static cels | effect (code) | typical entities |
|---|---|---|---|
| 0x1 | 1,499 | View_AddCels puts the cel in sort list 1 with a negative distance: transparent, drawn back to front | `jeanmarc-sunny` light shafts, `fx_fire_*`, `vol_windows`, `lamp_green`, coronas |
| 0x4 | 76 | per-frame random scale 0.95-1.05 (the scale jitter) | `corona_*` |
| 0x8 | 76 | billboard: rotation = the viewer's | `corona_*` |
| 0x10 | 298 | **never drawn** (View_AddCels) | `blockingpoly*`, `tank_ramp_cell`: invisible collision |
| 0x40 | 42 | collision hits get flag 2 (Collide_Intersect): see-through or penetrable | water surfaces, pine trees |
| 0x2000 | 0 | sort list 2 | |
| 0x40000 | 14 | not drawn when `object_display_mask == 0x10` (viewer 4) | doors, lift doors, helicopter |
| 0x20, 0x100, 0x200, 0x1000, 0x100000 | 897 / 115 / 639 / 51 / 41 | not read by the static draw (render states elsewhere; guess: 0x20 additive, 0x200 glass) | |

The low 16 bits are also the light mask passed to `Lights_CalcClosestLights`.

### 3.5 Which static cels are drawn: `build_link_objects_to_rooms` 0x20f70 (confirmed code; the tool approximates it)

For each cel of type 0x8017, 0x8018 or 0x8019, the function looks at every room cel (0xc047/0xc023) where:

- the object's sphere radius is at most the room's, and
- the object's sphere centre lies inside the room's bbox (celglist bbox, world space).

For each such room it calls `Collide_SphereIntersect(centre, 4001, room, ..., mask 0x90)`, and it keeps the room
whose hit is nearest. The object is chained onto `room+0x18` (and `obj+0x1c = room`). **Objects that no room
accepts are never drawn.** `View_AddCels` 0xdaa00 draws only:

- the visible rooms (the `GfxList`), and
- each visible room's +0x18 children, when the child has a glist, lacks flag 0x10, passes the 0xc023/0x8018 and
  0x40000 rules, and is inside the view frustum (`Vision_InView(cel+0x70, radius)`).

The tool approximates the sphere test with the distance to the room's nearest collision triangle. With that:

- 4,645 map objects link;
- 60 do not (31 of them have geometry, such as `lamp_noshadow` ×10 in 07000015 and `pylon*` in 0700000d,
  whose centres lie outside every room bbox);
- all 27 pack statics stay unlinked.

`static_world(link=False)` returns every drawable cel, for an exporter that wants the unlinked pieces too.

### 3.6 Exporter recipe (static world)

```python
lv = parse_level.Level('07000001')
for entity, M, cel in lv.static_world():            # linked and drawable, as the game draws them
    mesh = entity.mesh                              # parse_gfx Mesh (reverse triangles / negate normals, spec-mesh 6)
    room = cel if cel.is_room else cel.room         # group by room
    # M: 4x4 row-major, column vectors, y up; cel.eflags & 8 = billboard, & 1 = transparent
```

## 4. Rooms (cels) and portals (question 3)

- **A cel is a `cel_tag`** built from one static placement. It holds the glist, pos, rot, bbox, sphere, ambient
  light, switch channel, room link and portal list.
- **A room is a cel with bit 0x40000**: types 0xc047 and 0xc023. `gbl_room_cels` (`FUN_00020bd0`) lists them.
  `build_FindCel` 0x20c30 finds the room containing a point: the bbox test, then vertical rays against the room
  collision when several rooms qualify. Dynamic objects are linked to rooms (`build_LinkToRoom` 0x21110) and take
  their ambient light from the room.
- **The rooms are the world.** 655 rooms in 30 maps. Every drawn static object belongs to exactly one room.

**Portals** (block 0x21, `parsemap_block_portal_data` 0xa6260) are `u32 n` followed by n records of 56 bytes:
`vec3 quad[4]; u16 celA; u16 celB; u16 flags; u16 pad`.

- **celA and celB are entity indices of the map file.** The handler walks the world cel list and matches the
  cels whose glist is `&pCelList[idx]`. The list runs newest first and the handler keeps the last match, so it
  gets the earliest-created cel with that glist.
- `build_alloc_portal` 0x20730 (0x7c bytes) receives the plane of the quad and the reversed quad. It stores the
  quad centre and the quad bbox widened by 0.1.
- celB gets a portal into celA, open unless `flags & 2`. celA gets one into celB, open unless `flags & 1`. The two
  portals are partners. In the data the flags are always 0.
- **Validation:** 843 portals. All 825 whose cels exist lie on the bbox boundary of both rooms.
- **Original bug:** the handler never resets its two cel pointers. In 07000012 (Tower2B), portals 35-52 name
  entity 552 (`scriptplayer - Sky bridge`, a ScriptPlayer, not a cel). They therefore attach to **the previous
  portal's rooms** again. `Level.portals()` reproduces this and marks them `stale`.

**Visibility:** `View_CaptureSceneSub` 0xda130 → `Vision_Portal_Recurse` 0xdbc40 starts at `viewer->someCel`,
adds it (`Vision_AddCelToDraw`, at most 64 cels), and recurses through open portals clipped to the frustum
(`ClipPortalWithPlane`). `switch_ForceDrawAll` adds every cel instead. For an export, group by room. The portal
quads can be exported as a debug layer.

## 5. Collision (question 4)

Blocks 0x2e/0x2f precede their entity's mesh. They are read by `parsemap_block_Coll_Data_New` 0xa63f0, and
traversed by `ASH_RecurseBoxesBox` 0x29250, `Intersect_RayGeom` 0x29a30, `Intersect_UnpackTriangleVerticesToWorld`
0x28960 and `Intersect_UnpackTrianglePlaneEq` 0x288e0. **(confirmed)**

```
payload +0x00 u32 version (5 in all 7,987 blocks)   +0x04 u16 nBoxes  +0x06 u16 nTris  +0x08 u16 nChunks  +0x0a u16 0
        +0x0c u32 size (summed into OldCollisionSize)  +0x10 12 zero bytes           (version 5 only)
        +0x1c chunks  nChunks × 64 bytes    word pool: vertices (3 × s16) and planes (3 × s16, f32)
              boxes   nBoxes × 48           COLLBOX_tag
              tris    nTris × 8             u16 i0, i1, i2 (word offsets), u16 plane (word offset)
              surface nTris bytes (+ padding to 4)
COLLDATA_tag (0x18, runtime): +0 boxes*, +4 tris*, +8 surface*, +0xc chunks*, +0x10 u16 nChunks*64, +0x12 nTris, +0x14 nBoxes
```

### 5.1 COLLBOX (48 bytes)

`f32 min[3]; s16 childA; s16 childB; f32 max[3]; s16 triEnd; u16 chunk; f32 ox, oy, oz, scale`

- **Inner node** when `childA >= 0`. Its children are `boxes[childA]` and `boxes[childB]`, and `boxes[0]` is the
  root.
- **Leaf** when childA is negative (0x8000 | the number of 64-byte chunks it uses). Its triangles are
  `[childB, triEnd)`.
- A leaf's vertex: `p = chunks + 64*chunk + 2*word`, and `v = s16[3] * scale + (ox, oy, oz)`. The scale is about
  1e-4, which gives ±3.3 m per leaf at 0.1 mm steps.
- A triangle's plane: `n = s16[3] * 6.103888e-05` (1/16383), `d = f32`, with `n·v + d = 0`.
- The surface byte: bits 0-5 are the surface type. The ray-test mask bit 1 skips 0x0d/0x0e; mask bit 2 skips 0x10.
  Bits 6-7 are flags. The type drives footsteps and impacts: other areas.
- The ray test also reads `*(u32*)(surface - 4) & 0xc0000000` with mask 0x100000.

**Original bug:** the load-time loop that widens each box by 0.01 never advances its pointer, so only box 0 grows
(by `0.01 * nBoxes`).

### 5.2 Validation

Across the distinct files: 6,455 blocks and 2,031,353 triangles.

- Every triangle is covered exactly once by the leaves.
- Every vertex is inside its leaf box (±0.02).
- The planes agree with the triangle normals, except about 90 sliver or quantised triangles in 93 blocks.
- The collision renders as the same shapes as the meshes (07000001).

### 5.3 Positioning

The collision is in the entity's own space.

- **For a static cel**, `Collide_Intersect` 0x2be10 uses the identity when `cel bits & 0x8000000` (pos and rot both
  zero, and every room). Otherwise it uses `RotTransMatrix(rot, pos)` = `T(pos)·Rx·Ry·Rz` (note: not the draw
  order; identical for single-axis rotations). This is `Cel.world_collision`.
- **For a dynamic object** it uses `obj->mtx`.

**Export:** `Collision.triangles()` returns `(v0, v1, v2, surface, plane, leaf)`, and
`parse_level.py obj ... --coll` writes the layer.

## 6. Paths and AI paths (question 5)

### 6.1 Path (0x19, map files only; confirmed)

The payload is `pathFrames × 28` bytes: `{vec3 pos; quat x y z w}` in world space. Its first quat is (0,0,0,±1).

- A placement uses path `pathIndex` (+0x44) with `pathFrames` (+0x40) frames, which equals the block's frame
  count in all 586 uses. The rate is at +0x42 (level_tag+4). `Copter_CreatePath` uses frame rate = +0x54 key or
  pathRate.
- `Spline_Interp` 0xd8df0 evaluates positions as a Catmull-Rom spline through the neighbouring frames and slerps
  the quats. So export the path as a polyline through the frame positions, or as a spline.
- There are 617 path blocks across the maps. The 27 path blocks in packs are ignored, and the 27 pack placements
  with path fields never run (1.3).

### 6.2 AI paths (0x05, `AIPath_Parse` 0xa6c40; confirmed)

```
u32 version (8, else the block is ignored)   u32 count
record: char name[0x80]; u32 flags; u32 zero[8]; u32 nodeCount; u32 extraBytes; u8 extra[extraBytes]
  flags bit 0 = 1: BOUND (area outline, "Boundary"); 0: PATH (route graph: "Mayhew Route", "BotPath2", ...)
  path node  0x40: u16 index, u16 pathNo, u32 link (0x2468abce → 0), u32 0x87654321, u32 0x9abcdef0,
                   vec3 pos, f32 1.0, then sentinels and 3 × f32 0.1234 (runtime space)
  bound node 0x20: the same first 0x20 bytes
  u32 linkCount; path link 16: {u16 index, u16 pathNo, u32 0, u16 nodeA, u16 nodeB, f32}
                 bound link 20: {u16 index, u16 pathNo, u32 0, u16 nodeA, u16 nodeB, u32 0x01234567 × 2}
```

- The runtime writes the index and path number into +0/+2 of nodes and links, and adds 0.4 to each node's y
  (+0x14) at load time.
- Each record is decoded to the byte, ending exactly at the block end in all 28 maps. There are 177 paths and 33
  bounds.
- Export each record as nodes (points) plus links (edges). The flag bits above bit 0 (2, 4, 8, 0x200, 0x3d00 ...)
  and the `extra` bytes are the path's type or team; they belong to the drone agent.

## 7. Level-wide data (question 6)

### 7.1 Ambient radiators (0x26, `Light_SetAmbientRadiators` 0x6e3b0, map pass only; confirmed)

The block is `u32 n`, then n records of 32 bytes: `vec3 pos; f32 range²; f32 1/range²; f32 r, g, b (0..1)`.

- Each record becomes a Light-list point light with brightness or range = `sqrt(range²)` and colour ×255. The
  light is not switch-driven.
- There are 4-181 per map, 1,666 in all. See objects-dressing.md: they light only flagged objects, because the
  world mesh is prelit.
- `Level.radiators()` returns `pos, range, colour`, which are good as Blender point lights.

### 7.2 Map sounds (0x28, `Sound_LoadMapSounds` 0xcb870 → GMapSounds 0x299ae8; confirmed)

The block is `u32 n`, then n records of 32 bytes: `u32 0; u32 sfx (low 20 bits id, 0x40000000 set, 0xffff =
none); vec3 pos; f32 100 (volume/pitch, ftol'd); f32 radiusOuter; f32 radiusInner`.

- `HandleMapSoundAllocation` 0xcbf90 starts a looping 3-D sound when the listener comes within radiusOuter, and
  stops it beyond 1.2 × radiusOuter.
- There are 78 across the maps. Export them as empties with two radii.

### 7.3 Particles (0x30, `parsemap_block_particles` 0x68020; packs and maps)

These are particle-system **definitions**, each registered under its own hashcode. They carry no positions: the
Emitter placements (objects spec) place them.

```
u32 n; n × {u32 word (low byte must be 4; bits 8+ kept as a u16); u32 hash (registered); u32 texture hash
(-1 none; the hashtable entry's first dword is kept); u32 hash2 (-1 none); u16 a; u16 b; f32 × 10; u32 nKeys;
nKeys × {f32 size xyz (halved at load); f32; f32}}
```

Every block decodes exactly to its end. **(confirmed layout; field meanings guess)**

### 7.4 Fog (confirmed)

`psiFog` 0xddb00 ignores its arguments for the listed levels and hard-codes near, far and RGB. Every other level
has no fog. `View_DrawSky` switches fog off for the sky and back on afterwards. The table is `parse_level.FOG` and
`Level.fog()`. Examples:

| level | near | far | colour |
|---|---|---|---|
| HendersonB | 10 | 240 | #3b3f80 |
| CastleExterior | 26 | 213 | #354866 |
| TowerA | 10 | 114 | #798096 |
| SkyRail | 10 | 179 | #182f53 |

The table also has entries for 0x0700000f and 0x07000010, which are not in the bundle set.

### 7.5 Sky (another agent's type; how it is drawn)

- SkyObject placements (type 42) call `View_AddSkyObj` 0xd9c60. Up to 21 slots of 0x34 bytes live at 0x29d7a0.
- `View_DrawSky` 0xd9d60 draws them before the world with fog and lights off:
  - If glist flag 8: the viewer rotation (a billboard). Otherwise `RotMatrix(rot)` (Rx·Ry·Rz), plus a slow spin
    in SpaceStationD.
  - The position is **the camera position if glist flag 2** (skybox, moon or stars), else the placement position.
  - There is optional lightning flashing (an alternate glist, and thunder sounds).
- There is no separate sky block.

### 7.6 Lighting in general

The world meshes are prelit through their vertex colours. Rooms add an ambient RGB (3.1). Dynamic objects take
their room's ambient, plus the nearest radiators and Light objects.

## 8. Per-level numbers (from `parse_level.py`)

`drawn` is the number of static cels drawn under the approximation of 3.5. `dynamic objects` counts map placements
that would be created: type & 0xa000 == 0, glist resolved, before the singleplayer/multiplayer filters.

| bundle | level | files (packs+map) | rooms | static cels | drawn | dynamic objects | portals | paths | AI paths/bounds | radiators | map sounds | fog |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 07000001 | HendersonA | 57+1 | 11 | 122 | 103 | 205 | 9 | 24 | 4/1 | 26 | 12 | - |
| 07000002 | HendersonB | 54+1 | 28 | 235 | 199 | 266 | 37 | 21 | 11/1 | 50 | 4 | 10-240 #3b3f80 |
| 07000003 | HendersonC | 53+1 | 25 | 271 | 250 | 242 | 33 | 21 | 4/1 | 43 | 9 | - |
| 07000004 | HendersonD | 53+1 | 2 | 49 | 36 | 28 | 0 | 13 | 1/1 | 26 | 6 | 7-55 #0b121d |
| 07000005 | CastleExterior | 51+1 | 19 | 137 | 114 | 227 | 21 | 7 | 6/1 | 23 | 2 | 26-213 #354866 |
| 07000006 | CastleCourtyard | 49+1 | 37 | 364 | 348 | 450 | 44 | 35 | 18/1 | 48 | 1 | 12-96 #5a7598 |
| 07000007 | CastleIndoors1 | 51+1 | 32 | 325 | 294 | 194 | 34 | 8 | 8/1 | 104 | 0 | 10-150 #c47f60 |
| 07000008 | CastleIndoors2 | 54+1 | 18 | 118 | 97 | 184 | 21 | 26 | 6/1 | 22 | 0 | 10-200 #506482 |
| 07000009 | TowerA | 40+1 | 14 | 179 | 166 | 91 | 16 | 7 | 6/1 | 66 | 4 | 10-114 #798096 |
| 0700000a | TowerB | 38+1 | 30 | 509 | 470 | 283 | 38 | 25 | 7/1 | 69 | 0 | 10-100 #867f84 |
| 0700000b | TowerC | 56+1 | 23 | 197 | 166 | 163 | 33 | 19 | 1/1 | 114 | 3 | - |
| 0700000c | PowerStationA1 | 56+1 | 22 | 305 | 265 | 268 | 26 | 18 | 26/1 | 38 | 7 | 45-145 #d2d2c1 |
| 0700000d | PowerStationA2 | 50+1 | 15 | 281 | 256 | 281 | 21 | 15 | 27/1 | 21 | 3 | 20-196 #998b70 |
| 07000011 | Tower2A | 46+1 | 20 | 164 | 131 | 161 | 30 | 8 | 2/1 | 96 | 0 | - |
| 07000012 | Tower2B | 39+1 | 27 | 470 | 423 | 312 | 53 | 28 | 15/1 | 75 | 0 | - |
| 07000013 | Tower2C | 42+1 | 2 | 57 | 48 | 121 | 2 | 2 | 2/1 | 28 | 0 | - |
| 07000014 | EvilBase | 57+1 | 41 | 288 | 270 | 745 | 55 | 74 | 8/1 | 83 | 9 | 4-77 #8080be |
| 07000015 | EvilSilo | 55+1 | 59 | 388 | 342 | 254 | 62 | 46 | 1/1 | 163 | 7 | - |
| 07000016 | EvilBaseC | 65+1 | 8 | 123 | 107 | 215 | 7 | 12 | 5/1 | 65 | 9 | 7-151 #8080db |
| 0700001b | SpaceStationD | 27+1 | 1 | 28 | 20 | 152 | 0 | 0 | 7/0 | 4 | 2 | - |
| 07000023 | Atlantis (MP) | 110+1 | 28 | 78 | 47 | 98 | 31 | 0 | 1/1 | 45 | 0 | - |
| 07000024 | SkyRail (MP) | 111+1 | 49 | 150 | 129 | 112 | 70 | 0 | 1/1 | 31 | 0 | 10-179 #182f53 |
| 07000025 | SubPen (MP) | 109+1 | 19 | 204 | 189 | 126 | 23 | 10 | 1/1 | 62 | 0 | - |
| 07000026 | StealthShip (MP) | 110+1 | 1 | 24 | 15 | 99 | 0 | 0 | 1/1 | 41 | 0 | - |
| 07000027 | FortKnox (MP) | 110+1 | 19 | 68 | 54 | 113 | 22 | 0 | 1/1 | 58 | 0 | 17-182 #b8abce |
| 07000028 | MissileSilo (MP) | 110+1 | 4 | 60 | 44 | 83 | 3 | 0 | 1/1 | 53 | 0 | 10-122 #83b67b |
| 07000029 | SnowBlind (MP) | 110+1 | 21 | 168 | 148 | 97 | 25 | 1 | 1/1 | 21 | 0 | - |
| 07000048 | Menu_Pre | 5+1 | 0 | 0 | 0 | 0 | 0 | 0 | - | 0 | 0 | - |
| 0700004a | Tower2Elevator | 42+1 | 57 | 69 | 61 | 229 | 97 | 162 | 5/7 | 181 | 0 | 3-35 #161f34 |
| 0700004b | Ravine (MP) | 110+1 | 23 | 101 | 56 | 123 | 30 | 4 | - | 10 | 0 | - |
| 07000500 | FMV background | 2+1 | 0 | 0 | 0 | 0 | 0 | 0 | - | 0 | 0 | - |
| 07000999 | (no map file) | 2 packs | | | | | | | | | | |

Most undrawn static cels are `blockingpoly*` collision (flag 0x10) or empty-mesh helpers.

## 9. Tool: `tools/level/parse_level.py`

It imports `tools/anim/bundle.py`, `parse_gfx.py` and `nfpaths.py`.

- **Placements:**
  - `Level(path | '07000001')` gives `.files` in load order, `.map`, `.hashtable` and `.placements()`.
  - `Placement` has fields for every byte, plus `type_split()` (id / static / room / unplaced / kind),
    `keys`, `level_tag()` (the exact 0xb0 bytes), `dword(k)`, `as_float(k)` and `to_json(level)`.
  - `Level.resolve(pl, static)` resolves the glist with the game's rules.
  - `Level.dynamic_objects()` returns what `map_data_dynamic(doCreation=1)` hands to the Create switch.
- **Static world:**
  - `Level.static_cels()` returns `Cel` objects: bits, kind, pos, rot, ambient, music event, alpha mode, room,
    children, `.world` (draw) and `.world_collision`.
  - `Level.link_rooms()` gives the room links. `Level.static_world(link=True)` returns `[(Entity, 4x4, Cel)]`.
  - `rot_zyx`, `rot_xyz`, `quat_matrix`, `matrix` and `xform` are the matrix helpers.
- **Other blocks:** `Level.portals()`, `.paths()`, `.ai_paths()`, `.radiators()`, `.map_sounds()`,
  `.particles()` and `.fog()`; `decode_collision()` returns a `Collision` (`.boxes`, `.triangles()`,
  `.validate()`).
- **CLI:**
  - `validate [-v]`: the whole census, with room linking under `-v`, in about 15 s.
  - `dump BUNDLE [out.json]`.
  - `obj BUNDLE out.obj [--coll coll.obj] [--all]`.

The `validate -v` output over all 32 bundles:

- 21,725 placements decoded. Every one tiles its block and every glist resolves.
- Placement +0x40 equals the path frame count in 586 of 586 uses. No duplicate hashcodes.
- 2,031,353 collision triangles validated.
- 843 portals (18 stale). All AI-path, radiator, sound and particle blocks consume their blocks exactly.
- **Problems reported:**
  - 20 collision blocks with a few sliver triangles whose plane disagrees;
  - 07000999 has no map file: two packs and one script, never loaded as a level.

## 10. Open questions and original bugs

- **Room linking is approximated.** `Collide_SphereIntersect`'s exact hit rule (radius 4001, mask 0x90,
  flags 4) is replaced by the nearest collision triangle. An exact reimplementation would settle the ~60 borderline
  objects.
- **Unread cel fields.** Room key 4 (music event) and key 9 (sound environment): the values are decoded, but the
  music and reverb tables they index are not.
- **The `alpha` mode** of 0x8019 k0/k1 is read from `FUN_000d9bd0`. It is not checked in game.
- **Entity flags** 0x20, 0x100, 0x200, 0x1000 and 0x100000 are not read by the static draw path. Their render-state
  meaning (additive, glass ...) is unconfirmed.
- **AI path fields.** The `flags` bits above bit 0 and the `extra` bytes: drone area.
- **Particle-definition fields.** The 10 floats and the key fields are not named.
- **Original bugs:**
  - static glist index `<= MapEntityCount` (off by one);
  - the portal handler reuses the previous portal's cels (07000012);
  - the collision box widening loop does not advance;
  - `hashtable_additem` inserts duplicates at a stale index (no duplicates occur);
  - multi-axis static rotations are drawn Rz·Ry·Rx, while their collision and culling use Rx·Ry·Rz (17 lamps
    in 07000009).
