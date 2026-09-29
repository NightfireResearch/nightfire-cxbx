# spec-mesh: graphics packs (01xxxxxx) and their meshes

This spec covers the `01xxxxxx.bin` graphics packs: the block container, the entities in it, and the Xbox mesh
block (`KXE\x06`), with enough detail to rebuild every model in Blender, skinned characters included. Offsets
and sizes are from the Xbox `default.xbe`. The PS2 `ACTION.ELF` supplied the function names. Textures and
materials are in [spec-texture.md](spec-texture.md). This spec gives only their byte counts.

The tool is `tools/anim/parse_gfx.py`. It is importable and has a command-line interface; section 10 covers both.

## 0. Summary

- **Container.** A pack is a u32 `0` followed by blocks, and it ends at block `0x1d`. Each block starts with a u32
  holding `type << 24 | size`, where size counts the header. The walker accounts for every byte of every graphics
  pack: the 2,012 files are the 1,955 `01` entries of the 32 level bundles plus the 57 dumped files, and all 2,012
  validate. The only bytes after the end block are zero padding of up to 31 bytes, which is the bundle's 32-byte
  alignment.
- **Entities.** Each entity is one `celglist_tag` of 0x3C bytes. It is written as `[0x2e collision] 0x0d mesh 0x04
  params`, and block 0x04 closes the entity. A `0x02xxxxxx` hashcode goes through the hashtable to the celglist,
  then through `celglist+4` (an index) to `d3dGeometryObjs[idx]` at 0x2a0e68, which is the mesh block in place.
  There is one mesh per entity: no sub-objects and no LOD chain. The "LOD" block links an entity to its alternate
  state, such as locked to unlocked or intact to burned.
- **Mesh.** A 0x60-byte header, then vertices (28 bytes rigid, 32 bytes skinned), u16 indices, 12-byte
  triangle-strip primitives, 54-byte bone palettes, and int16 morph deltas. Each section begins 3 bytes after the
  previous section's data ends. All header fields are now known. **+0x14** is the total number of strip triangles,
  **+0x18** is the number of degenerate ones (a statistic, and not always exact), and **+0x20** holds exporter
  flags that the game never reads.
- **Coordinates.** Right-handed, y up, and characters face +z. One unit is about one metre: a human is 1.72 to
  2.13 units tall. **The stored normals point into the mesh**, and the strip order below winds the same way
  (checked 28 Sept 2026: 97% of skin 05000007's vertex normals point towards their own bone, and all the gun
  suppressor's point at its centre). So an exporter reverses every triangle and negates the normals (section 6).
  The engine mirrors x in its view matrix to feed D3D's left-handed projection (`psiUseCamera` 0xdd950).
- **Skinning.** A vertex has two bones and one weight. The decode was checked on **all 195 distinct rigged
  skin/mesh pairs** (333,132 vertices):
  - Every bone byte is a SkinRegTable register, and every palette resolves consistently.
  - Every referenced bone is one the skin uses, and every weight is in [0, 1].
  - Posed with real animation frames, 188 of 194 meshes keep 99% of their edges within 2.5x of the bind length.
    The alternative decodes tear the mesh: 99% stretch of 13x to 205x.
- **Rigid parts.** Discrete parts are bone-local. They are drawn with the bone's world matrix, so in bind pose the
  model-space position is `v + joint(bone)`. The data agrees in 186 of the 193 parts where the two hypotheses can
  be told apart.

## 1. The load path (Xbox)

`LoaderProcess` 0xbe7b0 passes bundle entries of types 0, 2 and 11 to `parsemap_parsemap(hash, 0)`. Type 11 sets
MemType 1 first. Type 1, the level's map file, goes to `parsemap_parsemap(hash, 1)` (spec-load section 1).

| Function | Xbox | PS2 | Role |
|---|---|---|---|
| parsemap_parsemap | 0xa6b20 | 0x1d2490 | cdecl `(u32 hash, char isMap)`; a state machine: 0 = load `%8.8X.bin` (psiFileLoadForParse), 1 = loop parsenextblock until it returns true, 2 = `map_data_dynamic` (packs only, isMap 0), 3 = Mem_Shrink the discard area |
| parsemap_parsenextblock | 0xa6a80 | 0x1d1a88 | `bh.size = *p & 0xffffff; bh.identifier = *p >> 24`; dispatches; `FileNextBlock += bh.size`. If `BinFileVersion == 1` it walks a header table of block offsets instead (never used: every file starts with u32 0) |
| parsemap_handle_block_id | 0xa6820 | 0x1d17c0 | switch on the block type (section 2) |
| parsemap_block_entity_params | 0xa6180 | 0x1d1ca8 | fills `*pCurrCelList`, adds `hash -> celglist` to the hashtable, calls psiCreateMapTextures once per file and then psiCreateEntityGfx, then `pCurrCelList++` |
| psiCreateEntityGfx | 0xde220 | 0x1173a8 | cdecl `(celglist_tag*, map_tag*, memType)`: validates the mesh, registers the vertex, index and morph buffers, remaps the texture ids, and replaces `celglist+4` with the geometry index (section 5.1) |
| hashtable_hashcode_to_celglist | 0x6b610 | 0x1caf08 | `hash != -1 ? hashtable_getentry(hash)->pData : NULL` |
| psiDrawObjectMatrix | 0xdde50 | | transposes a `_MATRIX` to a D3D 4x4 (adding the 0,0,0,1 column), d3dSetMatrix, `RecurseAndDrawBoxes(celglist->gfxEntityIdx)` |
| RecurseAndDrawBoxes | 0xdd4c0 | | the primitive loop (5.6). Ghidra ends the function at 0xdd56d; the body continues to 0xdd8d2 |
| psiGetTriList | 0xdf6c0 | | builds a triangle list from a mesh (drops degenerate triangles; used for hit tests) |
| d3dCreateVertexBuffers / d3dCreateIndexBuffer | 0xe4680 / 0xe4890 | | `ptr' = (ptr + 3) & ~3`; memmove the data up to `ptr'`; register a 0x24- or 0x1c-byte slot |
| d3dGetVertexDataSize / d3dGetIndexDataSize | 0xe4f80 / 0xe4fb0 | | `n * (skinned ? 32 : 28) + 3` (6 for morph streams) / `n * 2 + 3` |
| maybePsiResetResources | 0xe02e0 | | clears `d3dGeometryObjs[0x800]` and sets `NumXboxEntityGfxsCreated = 1` (index 0 means no geometry) and the texture base `DAT_00194810 = 1` |

## 2. The container

```
file   : u32 BinFileVersion (always 0)  block*  END  zero padding (< 32 bytes, to the bundle's 32-byte granule)
block  : u32 header = type << 24 | size (size includes the header, 24 bits)   u8 payload[size - 4]
```

The grammar every one of the 2,012 files follows:

```
0e 10 0f 18*  ( [2e] 0d 04 )*  [05] 19*  1c 1a 2c [28] [26] 21 27 [30] 1d
```

- **0e** is the map header, **10** and **0f** the texture tables, and **18** the texture data.
- The repeated group is one entity: an optional collision block, then its mesh, then its params.
- The trailing blocks are AI paths, paths, the discard marker, placements, the hashlist, sounds, radiators,
  portals, the "LOD" links, particles and the end marker.
- Types 05, 26, 28 and 30 occur only in map files (bundle type 1).

| type | name (handler) | count | bytes | payload and consumer |
|---|---|---|---|---|
| 0x04 | entity_params (`parsemap_block_entity_params` 0xa6180; 0x1f goes to the same handler but never occurs) | 16,816 | 1,141,340 | see 3.1. Closes one entity |
| 0x05 | AI paths (`AIPath_Parse`) | 30 | 1,407,460 | map files only; not decoded here |
| 0x0d | model (`handle_block_id`: `pCurrCelList->gfx = block + 4`) | 16,816 | 184,236,620 | the KXE mesh (section 5); always directly before its 0x04 |
| 0x0e | map_header (`parsemap_block_map_header` 0xa5eb0) | 2,012 | 40,240 | `u32 hash (= file name), u32 entityCnt, u32 pathCnt, u32 textureCnt`. Allocates `pCelList = entityCnt * 0x3c`, the path pointer array, and the 0x14-byte `map_tag` |
| 0x0f | palette_header (`parsemap_block_palette_header` 0xa6030) | 2,012 | 194,912 | 8 bytes per texture (spec-texture) |
| 0x10 | bmp_header (inline: `map+8 = block+4; map+0xc = (size-4)/12`) | 2,012 | 288,344 | 12 bytes per texture; the count equals textureCnt in every file (spec-texture) |
| 0x18 | texture_data (`parsemap_block_texture_data_Xbox` 0xa6110; also 0x17) | 23,358 | 296,554,428 | one texture (`KXT\x06`, `TD` ...; spec-texture) |
| 0x19 | path (kept only when isMap) | 659 | 455,060 | the count equals the map header's pathCnt in every file |
| 0x1a | map_data (`_static` 0xa6550 at once, `_dynamic` 0xa66f0 in state 2) | 2,012 | 2,317,608 | placements (3.4); the records tile every block exactly |
| 0x1c | memory_discard (inline: `FileDiscard = block`) | 2,012 | 8,048 | marker, no payload: everything after it is freed after loading |
| 0x1d | end (inline: returns true) | 2,012 | 8,048 | marker, no payload |
| 0x21 | portals (`parsemap_block_portal_data` 0xa6260) | 2,012 | 66,944 | `u32 n`, then n x 56 bytes: `vec3 quad[4]; u16 celA; u16 celB; u16 flags; u16 pad`; the size is `8 + 56n` in every file |
| 0x26 | ambient radiators (`Light_SetAmbientRadiators`, isMap only) | 69 | 57,352 | map files |
| 0x27 | "LOD" (`parsemap_block_LOD` 0xa5fe0) | 2,012 | 142,576 | per entity `{s32 index (-1 none); f32 (1.0, or 99999.0 when none; not read)}` -> `celglist+0x34 = &pCelList[index]`. The size is `4 + 8 * entityCnt` in every file |
| 0x28 | map sounds (`Sound_LoadMapSounds`, isMap only) | 46 | 3,920 | map files |
| 0x2c | hashlist (`parsemap_block_hashlist` 0xa5f80) | 2,012 | 16,720 | u32 hashcodes that `isLoadable()` if not yet in the hashtable (0x06000000 skipped): 1,419 type-06, 645 type-05, 104 type-02 |
| 0x2e | coll_data (`parsemap_block_Coll_Data_New` 0xa63f0; also 0x2f) | 7,987 | 54,295,468 | 3.3; collision for the entity that follows |
| 0x30 | particles (`parsemap_block_particles` 0x68020) | 31 | 58,900 | map files; 1,900 bytes each |
| | file header / tail padding | | 8,048 / 26,476 | |

The block total plus the header and padding equals the total of all file sizes. Nothing is unexplained at the
container level. The payloads of 05, 19, 1a (beyond the record walk), 26, 28, 30 and the collision geometry
belong to other areas. The walker checks their sizes and record tiling only.

## 3. The non-mesh blocks a model exporter meets

### 3.1 Entity params (0x04) -> celglist_tag

| block off | size | type | -> celglist_tag | evidence |
|---|---|---|---|---|
| +0x04 | 4 | u32 | hashcode (0x02xxxxxx, or 0xffffffff) | 0xa6180 `hashtable_additem(hash, celglist)` unless -1 |
| +0x08 | 4 | u32 | +0x38 flags ("applyFlagsToObject"; 0x800 = do not apply the object's tint, AnimObjectDraw) | 0xa6180 |
| +0x0c | 16 | f32[4] | +0x0c sphere (centre, radius) | 0xa6180 |
| +0x1c | 12 | f32[3] | +0x1c bboxMin | 0xa6180 |
| +0x28 | 12 | f32[3] | +0x28 bboxMax | 0xa6180 |
| +0x34 | .. | char[] | +0x00 name points here; NUL, then 0xff padding to 4 (checked in all 16,816) | 0xa6180 |

In the data, 10,238 entities have a 0x02 hashcode. The other 6,578 are 0xffffffff: level geometry and objects
reachable only by index, through the map placements (`celEntityNum`).

### 3.2 celglist_tag: 0x3C bytes (confirmed)

The size comes from the map header's `Mem_Malloc(entityCnt * 0x3c)` and the LOD block's `i * 0x3c` stride.

| off | size | type | name | evidence | conf. |
|---|---|---|---|---|---|
| 0x00 | 4 | char* | name | entity_params | confirmed |
| 0x04 | 4 | ModelData* then int | gfx. Handler 0x0d stores `block+4`; psiCreateEntityGfx replaces it with the index into `d3dGeometryObjs` (0 = none) | 0xa6820 case 0xd; 0xde220 | confirmed |
| 0x08 | 4 | COLLDATA_tag* | collData (0x18-byte malloc, 3.3) | 0xa63f0 | confirmed |
| 0x0c | 16 | f32[4] | sphereBounds | 0xa6180 | confirmed |
| 0x1c | 12 | _VECTOR | bboxMin (AnimObjectNew reads bboxMin.y +0x20) | 0xa6180 | confirmed |
| 0x28 | 12 | _VECTOR | bboxMax | 0xa6180 | confirmed |
| 0x34 | 4 | celglist_tag* | **altGlist** ("LOD" block). The Ghidra guess `lodRelated` u8 is wrong: it is a full pointer. In the data it pairs `Lock - KeypadLocked -> KeypadUnlocked`, `OBJECT_RedWire -> RedWireBurned`, `room_communication_tv -> _damaged` (290 links); it is not a level of detail | 0xa5fe0 | confirmed (write); reader not traced |
| 0x38 | 4 | u32 | flags | 0xa6180; map_data_static copies it to `cel+0x84` | confirmed |

### 3.3 Collision (0x2e): container level only

`u32 version` is 5 in all 7,987 blocks, and version 5 has 16 more header bytes; its first u32 is added to
`OldCollisionSize`. Then come `u16 nBoxes (+8), u16 nB (+0xa), u16 nC (+0xc)`, and the data at +0x20:

- `nC * 64` bytes: collDataC; `sizeofC = nC * 0x40`
- `nBoxes * 48` bytes: COLLBOX, whose first 6 floats are a min/max box
- `nB * 8` bytes: collDataB
- the rest of the block: dataStartD

The counts fit their block in every file. **Original bug:** the loop that widens each box by 0.01 never advances
its pointer, so the first box grows by `0.01 * nBoxes` and the others are left alone.

### 3.4 Placements (0x1a), for assembling a level

A record is `0x4c + 8n` bytes (the Ghidra `TARGET_PLACEMENT`):

| off | size | type | name |
|---|---|---|---|
| +0x00 | 2 | u16 | celEntityNum |
| +0x04 | 4 | u32 | glistHashcode (-1: use `pCelList[celEntityNum]`) |
| +0x08 | 4 | u32 | placementType (0x8000 = static cel, 0xa000 clear = dynamic object) |
| +0x0c | 12 | vec3 | position |
| +0x18 | 12 | vec3 | rotation: Euler, `RotMatrixZYX` in View_DrawCelGlist 0xda670 |
| +0x24 | 16 | quat | |
| +0x34 | 12 | vec3 | ? |
| +0x40, +0x42, +0x44 | 2 each | u16 | path fields |
| +0x48 | 4 | u32 | n |
| +0x4c | 8n | | n `{u32 key, u32 value}` parameters |

Static level cells are drawn by View_DrawCelGlist with the matrix `RotMatrixZYX(rot) + position`. Cel flag 8
makes the cell a billboard and flag 4 jitters its scale. Pack files hold one 80-byte record.

## 4. From a 0x02 hashcode to its geometry

```
skin riggedHash / discreteHash, weapon_data gfx, datum entity ... : HASHCODE 0x02xxxxxx
  -> hashtable_hashcode_to_celglist(h)      (0x6b610)  = &pCelList[i] of the 0x04 block whose +4 == h
  -> celglist->gfxEntityIdx (+4)            index into d3dGeometryObjs[0x800] (0x2a0e68; slot 0 = none)
  -> ModelData* = the 0x0d block's payload, used in place (the loader aligns its sections, 5.1)
```

Every mesh's own hashcode (+4) equals its entity's, in all 16,816 entities. The same hashcode can be defined by
several packs and bundles: 16,816 entities contain 6,426 distinct meshes. In the game, the entity in the
currently loaded bundle wins, and copies of a hashcode can differ slightly between bundles, so export from the
bundle the skin came from. Rigged hashcodes with bit 0x00100000 (`0x021xxxxx`) are not entities: the loader
replaces them with the first loaded entry of `SleeveEnts_SleeveTable`
(`020009d0, 02000900, 020001f7, 02000929, 020009e6, 020009ff, 02000a14, 02000a13`; spec-load).

There is no hierarchy inside an entity. An entity is exactly one mesh. The mesh's primitives split it by material
and by bone palette, but they share one vertex buffer. Bodies with several parts are skins: rigged meshes plus
discrete parts plus datum attachments, each a separate entity.

530 distinct meshes are **empty**: all counts are 0 and only the 0x60-byte header is present (for example
`Script Player`, `Bolt_datum1_object`, `Pulse_1..20`, and `blockingpoly_*`). They are helpers, datums or
collision-only objects. psiCreateEntityGfx leaves `gfxEntityIdx = 0` for them, so they draw nothing.

## 5. The mesh block (0x0d, `KXE\x06`)

Offsets are relative to the magic, which is the block payload at block + 4.

### 5.1 Header (0x60 bytes)

| off | size | type | name | evidence | conf. |
|---|---|---|---|---|---|
| 0x00 | 4 | u32 | magic 0x0645584B `KXE\6` | 0xde220 `magicTag == 0x645584b` | confirmed |
| 0x04 | 4 | u32 | hashcode (= the entity's; -1 for unnamed) | data: 16,816 of 16,816 | confirmed |
| 0x08 | 4 | u32 | vtxCnt | 0xde220 | confirmed |
| 0x0c | 4 | u32 | idxCnt | 0xde220 | confirmed |
| 0x10 | 4 | u32 | primCnt | 0xde220, 0xdd4c0 loop bound | confirmed |
| 0x14 | 4 | u32 | **stripTriCnt**: the sum of the primitives' strip lengths, i.e. triangles drawn, degenerate ones included. Must be non-zero for the mesh to be created | 0xde220 (checked; `DAT_002adf20 += +0x14 - +0x18`); psiGetTriList allocates `(+0x14 - +0x18) * 12`. Data: equals the sum in 16,816 of 16,816 | confirmed |
| 0x18 | 4 | u32 | **degenerateTriCnt**. RecurseAndDrawBoxes moves it from the "drawn" to the "degenerate" counter (0x2ae290 / 0x2ae294) | 0xdd84e. It equals the number of strip triangles with a repeated index in 93% of distinct meshes; 691 differ (below) | confirmed meaning, inexact data |
| 0x1c | 4 | u32 | paletteCnt. Non-zero means skinned (32-byte vertices, skinned shaders) | 0xde220 (`!= 0` -> skinned stride; the morph data follows `paletteCnt * 0x36` bytes) | confirmed |
| 0x20 | 4 | u32 | **exporter flags, never read by the game**. Values by use: 0x2031 (objects, weapons, heads), 0x8019 (level cells), 0xc047 (rooms), small values 0x20-0xfe elsewhere | no reader in 0xde220, 0xdd4c0 or 0xdf6c0 | likely |
| 0x24 | 4 | u32 | morphTargetCnt (22 on faces) | 0xde220 `batchCnt` | confirmed |
| 0x28 | 4 | u32 | morphVtxCnt: the first morphVtxCnt vertices have deltas | 0xde220 | confirmed |
| 0x2c | 32 | char[32] | Maya node name | data | confirmed |
| 0x4c | 4 | | runtime vtxBuffer handle (0 on disk) | 0xde220 | confirmed |
| 0x50 | 4 | | runtime idxBuffer handle | 0xde220 | confirmed |
| 0x54 | 4 | | runtime prims* | 0xde220 | confirmed |
| 0x58 | 4 | | runtime palettes* | 0xde220 | confirmed |
| 0x5c | 4 | | runtime morph stream base (0 = no morphs) | 0xde220, 0xdd4c0 | confirmed |
| 0x60 | | | sections (5.2) | | |

In 691 of 6,426 distinct meshes, +0x18 does not match the degenerate triangles in the index data. The counts
differ by small amounts, in both directions. Rendering is not affected. In 253 of those meshes the stored count is
the larger one, so `psiGetTriList` allocates fewer 12-byte triangles than it writes (see section 12).

### 5.2 Sections

Each section starts where the previous one's data ended **plus 3**. On disk each data run is unaligned and
followed by 3 spare bytes. At load, d3dCreateVertexBuffers and d3dCreateIndexBuffer move the vertex, index and
morph data up to the next 4-byte boundary, into those spare bytes. The primitives and palettes are used where they
lie.

```
0x60                vertices   vtxCnt * (paletteCnt ? 32 : 28)                   + 3
                    indices    idxCnt * u16                                      + 3
                    prims      primCnt * 12
                    palettes   paletteCnt * 0x36
                    morphs     morphTargetCnt * morphVtxCnt * s16[3]             + 3   (only if both counts != 0)
                    0xff padding to a 4-byte boundary = the end of the block      (all 16,816 blocks end exactly here)
```

### 5.3 Vertices

The vertex declarations at 0x1b50e4 (rigid) and 0x1b5158 (skinned) are
`STREAM(0), REG(0,FLOAT3), REG(1,NORMPACKED3), REG(2,D3DCOLOR), REG(3,FLOAT2)`, plus `REG(4,D3DCOLOR)` when
skinned. Tokens: 0x40320000, 0x40160001, 0x40400002, 0x40220003, 0x40400004.

| off | size | type | meaning |
|---|---|---|---|
| 0x00 | 12 | f32[3] | position (model space for rigged and static meshes; bone-local for discrete parts, section 8) |
| 0x0c | 4 | u32 NORMPACKED3 | normal: `x = sext11(bits 0-10) / 1023`, `y = sext11(bits 11-21) / 1023`, `z = sext10(bits 22-31) / 511`. The lengths come out 0.998-1.000 over every vertex |
| 0x10 | 4 | D3DCOLOR | vertex colour, bytes B, G, R, A. It is the prelit or lighting colour; the base texture is modulated by diffuse x 2 (spec-texture 4.2). A is 0xff on 94% of vertices |
| 0x14 | 8 | f32[2] | u, v (D3D: v down; Blender `v' = 1 - v`). 471 vertices in 13 meshes hold garbage (±1e23) |
| 0x1c | 4 | u32 | skinned only: the bone/weight word (section 7) |

### 5.4 Indices and strips

These are u16 vertex indices. Every index is below vtxCnt, and the strips use exactly idxCnt indices. Primitive
*p* covers `count + 2` consecutive indices, starting where the previous primitive's indices ended. It is drawn as
its own `DrawIndexedVertices(D3DPT_TRIANGLESTRIP = 6, count + 2, base + start * 2)` (d3dDrawIndexedVertices
0xe4b80), so there is no restart index, and parity starts afresh with each primitive.

A primitive's strips are joined with **degenerate triangles**: a repeated index, as in `a b b c` or `a b c c d`.
Drop any triangle with two equal indices, as psiGetTriList does.

Triangle *k* (0-based within the primitive) is `(i[k], i[k+1], i[k+2])` for even *k* and
`(i[k+1], i[k], i[k+2])` for odd *k*. In this order the face normal `cross(b - a, c - a)` points to the same side
as the stored vertex normals for **2,307,897 of 2,312,006** non-degenerate triangles (99.8%) of the distinct
meshes. Both point **into** the mesh, though: reverse the triangles and negate the normals for a right-handed
program (section 6).

A primitive's cull state (5.6) is CCW culling unless flag 3 makes it double-sided. How that, the view's x mirror,
D3D's left-handed convention and the NV2A's front-face setting combine to keep the outside of these inward-wound
meshes in the game is not worked out here; the geometry itself is not in doubt.

### 5.5 Primitive record (12 bytes)

| off | size | type | name | meaning |
|---|---|---|---|---|
| 0x0 | 2 | u16 | tex0 | the pack's local texture index (into the 0x10/0x0f lists), 0xffff = none. psiCreateEntityGfx rewrites it to `local + DAT_00194810` (the pack's first global Tex slot), or 0 if that texture has no handle. All 39,106 used ids are below textureCnt |
| 0x2 | 2 | u16 | tex1 | the same, for stage 1: an environment map (spec-texture 4.2). Only bound when changeMask bit 2 is set; 109 primitives carry a tex1 that is never bound |
| 0x4 | 4 | u32 | count | strip length - 2 (triangles, degenerate ones included) |
| 0x8 | 2 | u16 | stateFlags | bit 0 morph streams on; bits 1-2 blend (0/3 alpha, 1 additive, 2 subtractive); bit 3 double-sided (cull none; clear = CCW culling); bit 4 alpha ref 180 (else 1); bit 5 z-write; bit 6 never read (set on 90%); bits 7, 8 wrap U, V (clear = clamp) |
| 0xa | 1 | u8 | changeMask | which state groups this primitive sets: 0x01 morph, 0x02 blend, 0x04 tex1, 0x08 cull, 0x10 alpha ref, 0x20 z-write, 0x40 wrap. A state that is not flagged **carries over** from the previous primitive. The first primitive has 0x7b (all but tex1) in all but 4 meshes; tex1 is reset to none after every draw |
| 0xb | 1 | u8 | bonePalette | 1-based palette number. **0 keeps the palette loaded by an earlier primitive** (896 primitives of skinned meshes rely on this) |

The render-state functions are d3dSetupRenderStatesAndFog 0xe5060, d3dSetCullMode 0xe5610 (`0x901` =
D3DCULL_CCW), alpha ref 0xe5650, z-write 0xe4fc0, d3dSetDeferredTextureState 0xe5230, d3dSetTextureStage1 0xe3c10
and d3dSetTextureStage0 0xe4950. spec-texture.md gives the materials they build.

### 5.6 The draw loop (RecurseAndDrawBoxes 0xdd4c0, from the disassembly 0xdd4c0-0xdd8d2)

```c
void RecurseAndDrawBoxes(int idx)                     // idx in EBP
{
    ModelData *g = d3dGeometryObjs[idx];
    if (bCharacterLight) gfxSetCharacterLightIntensity(0.5f);
    if (g->morphBase /*+0x5c*/) copy Morph_Weight/Morph_Index[0..Morph_Count) to streamW/streamIdx, zero-fill to 8;
    d3dSetRenderState(1);
    d3dBindBuffers(g->vtxBuffer /*+0x4c*/, g->idxBuffer /*+0x50*/);       // stride 28 or 32 from the buffer slot
    int start = 0;
    for (i = 0; i < g->primCnt /*+0x10*/; i++) {
        Prim *p = &g->prims /*+0x54*/[i];
        if (p->bonePalette) {                                            // upload the palette's matrices
            u8 *set = g->palettes /*+0x58*/ + (p->bonePalette - 1) * 0x36;
            for (slot = 0; set[slot] != 0xff; slot++)
                d3dSetSkinMatrix(transpose3x4(g_skinPalette /*0x2adf28*/[set[slot]]), slot);   // 0xe4e40
        }
        if (p->changeMask) {
            if (p->changeMask & 0x02) d3dSetupRenderStatesAndFog((p->stateFlags >> 1) & 3);
            if (p->changeMask & 0x04) d3dSetTextureStage1(TexFrame(p->tex1), p->tex0 != 0);
            if (p->changeMask & 0x08) d3dSetCullMode(!((p->stateFlags >> 3) & 1));
            if (p->changeMask & 0x10) d3dSetAlphaRef(p->stateFlags & 0x10 ? 0xb4 : 1);
            if (p->changeMask & 0x20) d3dSetZWrite((p->stateFlags >> 5) & 1);
            if (p->changeMask & 0x40) d3dSetDeferredTextureState((p->stateFlags >> 7) & 1, (p->stateFlags >> 8) & 1);
            if (p->changeMask & 0x01) d3dSetStreamSources(p->stateFlags & 1 ? g->morphBase : 0, streamIdx/streamW...);
        }
        d3dSetTextureStage0(TexFrame(p->tex0));     // Tex[tex0]->handle[(ticks / Tex->fps) % Tex->numFrames]
        d3dDrawIndexedVertices(start, p->count);    // TRIANGLESTRIP, count + 2 indices
        drawnTris += p->count;  start += p->count + 2;
    }
    drawnTris -= g->degenerateTriCnt; degenerateTris += g->degenerateTriCnt;
    d3dSetTextureStage1(0, 0);
    if (g->morphBase) d3dSetStreamSources(0, ...);
    if (bCharacterLight) ...;
}
```

### 5.7 Bone palettes (0x36 bytes each)

A palette lists up to 53 **skin bone indices** (the same numbering as the skeleton, the skin's bone bytes and
`boneMtxs`), ended by 0xff and padded with 0xff. Palette slot *s* is loaded into the constant registers
`SkinRegTable[s]` (0x1b5208) and the two after it. Every palette in the data is terminated, with only 0xff after
the terminator. Of the 107 distinct skinned meshes, 106 have a single palette and one has two. Characters use at most about 50
bones, so a single palette covers them.

### 5.8 Morph targets

`morphTargetCnt` blocks (target-major) of `morphVtxCnt` `s16[3]` position deltas. The offset is
`delta * weight / 32767` model units, added before skinning. The morph-enabled strips only reference vertices
below morphVtxCnt. There are 61 distinct morphing meshes, all with 22 targets; spec-facial section 2.3 has the
channels.

## 6. Coordinate system, handedness, units

- **y is up.** Characters stand along y: skin 05000007 spans y -1.16 (feet) to 0.76 (head top), with the root at
  the pelvis.
- **+z is forward.** Toes (bones 52 and 56) are at z ≈ +0.12, against about 0.00 for the ankles. First-person
  arms stretch from z 0.61 to z 1.79 in front of a camera at the origin.
- **The world is right-handed.** `psiUseCamera` 0xdd950 builds the camera matrix, then calls
  `maybeMatrixAxisScale(&m, -1, 1, 1, 1)`, negating its x axis, before `d3dSetViewMatrixFromRigidTransform`.
  `createProjectionMatrix` 0xe84a0 is a plain left-handed perspective (`w = +z`, no sign flips), so the x mirror
  is what adapts the right-handed world to it. The data agrees:
  - The third-person weapon hand (bone 35, which holds about 57 of the 60 weapon datums of skin 05000007) is at
    x = -0.79. That is the right hand of a figure facing +z in a right-handed frame.
  - The first-person gun (bone 38, child of bone 21 in the `x ≈ -0.33` arm chain) is at -x. With the mirrored
    view, -x is screen right.
- **Winding and normals:** triangles in the order of 5.4 are counter-clockwise seen from **inside**, and the
  stored normals point inward too. (An earlier version of this spec said outside; a Blender import with backface
  culling showed every character inside-out.) `tools/anim/export_gltf.py` reverses each triangle and negates the
  normals.
- **Units:** about 1 metre. Full-body characters are 1.72-2.13 units tall (Mp_jaws is the tallest, 2.13).
- **Blender (z up, right-handed):** `(x, y, z)_blender = (x, -z, y)`, a +90° rotation about X. This is
  what an OBJ or glTF importer's "Y up" option does. Reverse the faces and negate the normals as above. For OBJ,
  flip `v` as `v' = 1 - v` (glTF shares Direct3D's top-left UV origin and needs no flip). Characters then face -y,
  which is Blender's front view.

## 7. Skinning, decoded and validated

### 7.1 Vertex word -> bones and weight

The skinned shader (#2, spec-pose 5.2) reads v4 = (R, G, B, A) / 255 = (b2, b1, b0, b3) / 255, where b0-b3 are
the bytes in memory order:

```
regA = 256*v4.z - 96 -> ARL -> constant register b0 - 96 (hardware c[b0])     slotA = SLOT[b0]
regB = 256*v4.y - 96                                     (b1)                   slotB = SLOT[b1]
wA   = v4.x/256 + v4.w = (b3*256 + b2) / 65280     wB = 1 - wA
boneA = palette[slotA], boneB = palette[slotB]    (palette = the primitive's bonePalette, or the carried one)
posed = wA * (v * PAL[boneA]) + wB * (v * PAL[boneB])      PAL[b] = boneMtxs[b] * invBind[b]  (psiBuildMatrixPalette)
lit normal uses bone A only
```

`SkinRegTable` was read from the binary (0x1b5208, 53 ints). With 96 added, the table is:

| slots | bytes |
|---|---|
| 0-18 | 0, 3, ... 54 |
| 19-30 | 60, 63, ... 93 |
| 31-52 | 126, 129, ... 189 |

This **confirms spec-pose 5.2's inverse**:

- `s = b/3` for b < 57
- `s = (b-3)/3` for 60 ≤ b ≤ 93
- `s = (b-33)/3` for b ≥ 126

In the data, every b0 and b1 of all 333,132 vertices is in this set.

The weight is a u16 split over the bytes: **b3 is the high byte and b2 the low byte**. The encoder meant
`u16 / 65535`: `0xffff` means "bone A only", and 73% of the vertices of the distinct skinned meshes are exactly
that. The GPU's `u16 / 65280` gives
1.0039 and a weight of -0.0039 on bone B, which is invisible. `parse_gfx.decode_bones(u)` returns `u16 / 65535`;
`gpu=True` returns the hardware value.

### 7.2 Validation over all bundles

Run `python parse_gfx.py --skins [--posed] [-v]`. For each bundle, every skin's rigged meshes are taken from that
bundle's packs, with sleeves substituted as the loader does. Each distinct skin/mesh pair is checked once:
**195 pairs, 333,132 vertices**.

| check | result |
|---|---|
| b0, b1 in the SkinRegTable set; slot < palette length | 333,132 / 333,132 |
| a vertex drawn by several primitives gets the same bones from each palette | all (no conflicts) |
| both bones used by the skin (bone byte bit 7) | all |
| weight in [0, 1] (with u16/65535) | all; 72,573 vertices are blended, the rest single-bone |
| bind pose: distance from the vertex to its dominant bone (the segments from its joint, `-invBind.t`, to its children's joints) | the 98 full-body pairs (1.72-2.13 tall): median 0.05-0.11, max 0.14-0.41; first-person hands: median 0.03, max 0.14 |
| the dominant bone is among the 3 nearest bones | 95.9% (misses are mostly bones of thin parts such as fingers, and the sleeve bone below) |
| **posed** with the middle frame of up to 3 of the bundle's sequences for the skeleton | 194 meshes posed: 99% of edges within 2.5x of bind length in 188; max stretch < 15 in 191. The worst are `spacesuit_hands1` (99% 3.3) and `mayhew` (2.9): extreme poses, not tearing. As a control, swapping the weights, swapping the bones or skipping the palette gives a 99% stretch of 13-205 |

Skin 05000007 (body 020004c2 `generic_head_grunt`) looks right when posed with sequence 040002b8, a crouched
two-handed pose (`020004c2_posed.obj` in the scratch folder).

**Sleeves and bone 37.** In every first-person skeleton (every skeleton of 38-62 bones), bone 37 is a
child of bone 2 with a default offset of (9.055, 0, 0.565). Its joint therefore sits 9 units to the side. The
sleeve meshes (for example `polySurface1` 020001f7 and 020009d0) weight about 116 forearm vertices to it. In bind
pose this is harmless, because the palette is identity. In Blender, however, the joint appears far away, and those
vertices swing about a distant pivot if bone 37 ever rotates. The 95th-percentile distance of 9 units reported for
the sleeve meshes comes from this bone alone.

## 8. Rigid (discrete) parts

`AnimObjectUpdate` 0x18690 copies `boneMtxs[skin->discreteBone[i]]` (the bone's **world** matrix, not the palette)
into the child object's matrix. The child is drawn with `psiDrawObjectMatrix(celglist, &obj->mtx)`. So a rigid
part's vertices are in **bone-local space**. Bind rotations are identity, so in bind pose
`model = v + joint(bone)`, where `joint = -invBind.t`, or the skeleton offsets times skinScale summed down the
chain.

The data test (`--skins`) covers 201 discrete parts on existing meshes, plus 24 references to empty helper meshes
(`Pulse_*`, `Bolt_datum*_object`, `Lens`):

- In 8 parts the joint is at the origin, so the two hypotheses cannot be told apart.
- Of the remaining 193, 186 have their vertices nearer the joint when treated as bone-local.
- Examples: `Key` (joint 1.71 from the origin) has median |v| = 0.096 and |v - joint| = 1.80; `Grip`,
  `Frag` and `Case` behave the same way.
- The 7 exceptions: two animated scenery objects (`tree_top`, `OBJECT_windows`), and small parts whose own joint
  is near the origin but which hang in the hand (`Clip` and `Suppressor`, whose bone-local positions land within
  0.03-0.05 of the hand bones).

For Blender, parent the part to its bone, keep its vertices as they are, and give it the bone's rest matrix
(identity rotation at the joint).

## 9. Sanity artefacts (scratch `mesh/obj/`)

| file | content |
|---|---|
| `020004c2.obj/.png` | skin 05000007's body `generic_head_grunt`: 1,438 vertices, 1,966 triangles, one palette of 40 bones. It renders as a solid T-pose man in a uniform with a belt and pouches, facing +z, arms along ±x, feet at y -1.16. The head carries the 22 morph targets |
| `020004c2_posed.obj/.png` | the same body posed with sequence 040002b8, frame 45: a crouched combat stance, with no torn triangles |
| `020009d0.obj/.png` | skin 050000ab's first-person arms (`polySurface1`, 1,923 vertices, 2,452 triangles): two sleeved forearms and hands reaching forward (z 0.61 to 1.79), below the eye (y -0.60 to -0.15), open at the shoulder end |
| `020009e6.obj/.png` | `Bond_hands_malewhite` (skin 050000b0): the same arms, bare |

The PNGs come from a scratch software rasteriser, `mesh/render.py`. It treats the data as right-handed, culls
back faces with the order of 5.4, and draws four views: from -z, from +z, from +x and from above. All three models
render closed, and none is inside out.

## 10. Tool: `tools/anim/parse_gfx.py`

```python
import parse_gfx as G
blocks = G.walk(data)                    # [Block(type, off, size)], END included; payload = data[off+4:off+size]
pack = G.parse_pack(data)                # .hash .entityCnt .textureCnt .entities .by_hash .hashlist .blocks .validate()
e = pack.by_hash[0x020004c2]             # Entity: .hash .name .flags .sphere .bbmin .bbmax .lod .mesh .coll_block
m = e.mesh                               # or G.parse_mesh(data, block.off + 4)
m.vertices   # [{'pos','normal','normal_raw','colour'(r,g,b,a),'uv','bones'(raw u32|None)}]
m.triangles()          # [(a, b, c, primIndex)]: winding per 5.4, degenerate ones dropped
m.prims                # [Prim: tex0 tex1 count flags change palette first] (.describe())
m.palettes             # [[skin bone, ...]]
m.skin_weights()       # ([[(boneA, wA), (boneB, wB)] per vertex], problems)
m.morph_targets        # [[(dx, dy, dz)] * morphVtxCnt] * morphTargetCnt
G.decode_normal(u), G.decode_bones(u, gpu=False), G.iter_packs(), G.bundle_contents(fn), G.write_obj(m, path)
```

```
python parse_gfx.py [-v]                 validate every 01 file of every bundle and the dump
python parse_gfx.py --skins [--posed] [-v]
python parse_gfx.py --obj 020004c2 OUTDIR [--bundle 07000001] [--skin 05000007]
```

The last command writes an OBJ. With `--skin`, a discrete part is moved to its bone's joint.

Results:

- 2,012 of 2,012 packs validate. For every mesh the checks are:
  - the sections end exactly at the block end, followed only by 0xff padding
  - the runtime words are zero
  - the strip lengths sum to +0x14 and use exactly idxCnt indices
  - every index is below vtxCnt
  - the palettes are terminated
  - the palette numbers are in range
  - morph strips stay below morphVtxCnt
- The skinning results are in section 7.2.

## 11. Answers to the facial spec's open header fields

- +0x14: total strip triangles.
- +0x18: degenerate strip triangles.
- +0x20: exporter flags (0x2031 on the heads), never read.

## 12. Open questions and original bugs

1. **Bug in psiGetTriList (0xdf6c0).** It allocates `(+0x14 - +0x18) * 12` bytes but writes one entry for every
   non-degenerate triangle it finds. In 253 distinct meshes +0x18 is larger than the real number of degenerate
   triangles, so the allocation is too small. For example, `rightdoor` has 375 against a real 341, which would
   write 34 triangles (408 bytes) past the buffer. Only objects that psiGetTriList is used on are at risk; its
   callers were not traced.
2. **Bug in Coll_Data_New.** The box-widening loop never advances its pointer (3.3).
3. psiCreateEntityGfx does not bound `NumXboxEntityGfxsCreated` against the 0x800 slots of `d3dGeometryObjs`.
   The vertex and index buffer registrations are bounded; this counter is not.
4. Primitive flag bit 6 and mesh +0x20 are never read. Their exporter meaning is unknown.
5. Who reads `celglist+0x34` (the alternate-state glist) was not traced.
6. Bone 37 of the first-person skeletons (7.2): is it ever animated? If it only translates, the far pivot never
   matters.
7. The collision data (after the three tables of 0x2e), the AI paths (0x05), paths (0x19), sounds (0x28),
   radiators (0x26) and particles (0x30) are size-checked, not decoded. They are outside this spec.
