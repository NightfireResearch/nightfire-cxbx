# Textures and materials in the graphics packs (Xbox)

How the action engine's graphics packs (`01xxxxxx` files) store textures, how a mesh primitive picks its
textures, and which render states a primitive sets - enough to carry a model's textures and basic material
settings into Blender. The container walk and the mesh itself are in [spec-mesh.md](spec-mesh.md)
(`tools/anim/parse_gfx.py`); the tool for this spec is `tools/anim/parse_tex.py`.

Every offset is the Xbox one. Evidence is cited by Xbox address; "confirmed" means read in the code and checked
against every file, "likely" means the code or data points that way but the last step was not traced.

## 1. Summary

- A pack's textures are three parallel lists: block **0x10** (12-byte headers), block **0x0f** (8-byte records,
  unused on Xbox) and one block **0x18** per texture, in the same order. The 0x18 block holds either a whole
  texture (`KXT\x06` header, then the pixels of every frame and mip level) or a **`TD`** record: a copy of the
  header whose pixels are those of a texture loaded earlier in the same level.
- A `TD` names its source by a **level-wide load index** (its low 16 bits + 1), not by hashcode. The numbering is
  NumXboxTexLoaded: reset to 1 per level, and advanced by every pack's texture count when the pack creates its
  textures (at its first entity block). All 5,417 `TD`s in the 32 bundles resolve that way to a `KXT` with the
  same width and height (4,266 also have the same name; the rest are a second material name for one bitmap).
- Formats in the data: **DXT1**, **DXT5**, **DXT3**, **A8R8G8B8** (swizzled) and **LIN_A8R8G8B8**. No palettes,
  no 16-bit formats, no cube maps. All 3,219 distinct textures decode, every frame and every mip level.
- A primitive's `tex0` and `tex1` are **indices into its own pack's texture list** (0xffff = none).
  `tex0` is the base texture (stage 0); `tex1` is a second-stage **environment/reflection map** (the textures
  used there are called `refmap`, `Enviromenttest10`, `kiko_reflect`...), almost always on an additive pass.
- `stateFlags` bits (applied only when the matching `changeMask` bit is set): 0 morph, 1-2 blend mode (alpha,
  additive, subtractive), 3 double-sided, 4 alpha-test threshold 180 (else 1), 5 depth write, 7/8 wrap U/V
  (else clamp). Bit 6 is set on most primitives but the draw loop never reads it.

## 2. File layout

A pack is `u32 0`, then level blocks: `u32 header = id << 24 | size` (size includes the header), payload
`size - 4` bytes (parsemap_parsenextblock 0xa6a80). The texture blocks come right after the map header 0x0e:

```
0x0e map header    (+0x0c u32 texture count = number of 0x10 records; see spec-mesh.md)
0x10 texture headers: N x 12 bytes
0x0f "palette" records: N x 8 bytes
0x18 texture data  x N, in list order
... then the entities (0x2e collision, 0x0d KXE mesh, 0x04 entity params) and the rest
```

### 2.1 Block 0x10 - texture header list (`bmpHeaderEntry`, 12 bytes)

parsemap_handle_block_id 0xa6820 case 0x10: `pBmpHeader = payload; m_pmap->texHeaderData = payload;
m_pmap->numTexHeaderEntries = (size - 4) / 12`.

| + | size | field | notes |
|---|---|---|---|
| 0x00 | u8 | flags | exporter flags; bit 4 is set on most textures with alpha, bit 3 on some 1-bit-alpha DXT1s. No Xbox reader found (guess) |
| 0x01 | u8 | ? | varies freely; no reader found |
| 0x02 | u16 | width - 1 | copied +1 into texData[i].w (0xa6110) |
| 0x04 | u16 | height - 1 | copied +1 into texData[i].h. In 2,366 of 22,360 entries this is not the KXT size (mostly half: the PS2 size) |
| 0x06 | u8 | numFrames | = KXT numFrames (1 for still textures) |
| 0x07 | u8 | fps | = KXT fps |
| 0x08 | u32 | hashcode | 0x03xxxxxx or 0xffffffff. At load: registered with `hashtable_additem(hash, &texData[i])` if not -1, then **overwritten with the address of the texture's 0x18 payload** (0xa6110) |

### 2.2 Block 0x0f - "palette header" (8 bytes)

parsemap_block_palette_header 0xa6030: `m_pmap->paletteHeaderData = payload; numPaletteHeaderEntries =
(size - 4) / 8; texData = Mem_Malloc(n * 8)`. Records are `{u32 ?, u32 hashcode}`; the hashcode equals the 0x10
record's in all 22,360 entries. The first word looks like leftover memory. On Xbox nothing reads the records
(0xa6110 only steps `pPallHeader` by 8); the block exists for its count, which sizes `texData`:

`texData[i]` (8 bytes, runtime): `+0 u32 level-wide texture index` (written by psiCreateMapTextures; 0 = none),
`+4 u16 width`, `+6 u16 height` (from the 0x10 record).

### 2.3 Block 0x18 - texture data

parsemap_block_texture_data_Xbox 0xa6110 (also case 0x17): binds this payload to the current 0x10 record and
advances to the next. The payload starts with a 0x54-byte header, followed by `numFrames` u32 runtime handles and
then the pixels. `KXT` layout (psiCreateMapTextures 0xddf90):

| + | size | field | notes |
|---|---|---|---|
| 0x00 | u32 | magic | `KXT\x06` (0x0654584B), or `u16 loadIndex, 'TD'` (0x4454xxxx) for a duplicate |
| 0x04 | u32 | hashcode | 0x03xxxxxx or -1. Same as the 0x10 record in all but 36 entries |
| 0x08 | u32 | frameSize | bytes per frame: the mip chain plus 0..127 bytes of slack (below) |
| 0x0c | u32 | width | |
| 0x10 | u32 | height | |
| 0x14 | u32 | alphaBits | 0, 1, 4 or 8. For formatType 0 it picks the format (below); for DXT it only describes the alpha |
| 0x18 | u32 | levels | mip levels (1..8); forced to 1 when linear |
| 0x1c | u32 | linear | 1 = linear (unswizzled) texture |
| 0x20 | u32 | formatType | 0 raw, 1 DXT1, 2/3 DXT3, 4/5 DXT5 (below) |
| 0x24 | u32 | numFrames | frames of an animated texture (1..60); 0 treated as 1 |
| 0x28 | u32 | fps | on disk, frames per second; the loader replaces it with `60 / fps` (1 if fps is 0 or >= 60): game ticks per frame |
| 0x2c | u32 | ? | 0 or 1; set on UI textures and a few others. No reader found |
| 0x30 | u32 | ? | 0 or 1, like 0x2c (the two agree in all but 2 entries). No reader found |
| 0x34 | char[32] | name | the material/bitmap name from the exporter ("Watchface", "bond_head") |
| 0x54 | u32[numFrames] | handles | zero on disk; the D3D texture slot of each frame |
| ... | numFrames x frameSize | pixels | frame f at `0x54 + 4*numFrames + f*frameSize` |

then 0..3 bytes of padding to the block size (block size = 4 + 0x54 + 4n + n*frameSize + pad, checked on all 16,943
`KXT` blocks).

Within a frame the mip levels follow each other with no padding, level 0 first. RegisterTexture 0xe4400 moves
the chain up in place to the next 128-byte boundary (the GPU needs that alignment), which is what the slack in
`frameSize` is for; offline, the pixels start exactly at the offset above.

A `TD` payload is the same 0x54-byte header (width, height, format fields, name, numFrames copied from the
source) and `numFrames` handle slots, with no pixels: block size = 4 + 0x54 + 4 * numFrames (all 5,417).

### 2.4 Formats

psiCreateMapTextures turns (formatType, alphaBits) into a RegisterTexture type, and RegisterTexture 0xe4400 turns
that (with `linear`) into the Xbox `D3DFORMAT` it passes to XGSetTextureHeader:

| formatType | alphaBits | RegisterTexture type | D3DFORMAT (linear) | in the data (entries / distinct) |
|---|---|---|---|---|
| 0 | 8 | 2 | 0x06 A8R8G8B8 (0x12 LIN_A8R8G8B8) | 2,686 / 350 swizzled, 58 / 2 linear |
| 0 | 4 | 1 | 0x04 A4R4G4B4 (0x1d) | none |
| 0 | other | 0 | 0x02 A1R5G5B5 (0x10) | none |
| 1 (or >5) | any | 3 | 0x0c DXT1 | 18,257 / 2,530 (alphaBits 1 on 362 KXTs: 1-bit alpha) |
| 2, 3 | any | 4, 5 | 0x0e DXT3 (DXT2/3) | 2 / 2 |
| 4, 5 | any | 6, 7 | 0x0f DXT5 (DXT4/5) | 1,357 / 335 |

(RegisterTexture also knows 8 = P8, 9 = A16B16G16R16 and 10 = X8R8G8B8, for other callers; the packs never use
them.) Entry counts are over all 22,360 list entries of the 1,955 packs in the 32 bundles, a `TD` counted as its
source's format; "distinct" is distinct pixel data. The dump folder's 57 packs hold 998 entries: 646 DXT1,
300 A8R8G8B8, 50 DXT5, 2 LIN_A8R8G8B8.

Level sizes (RegisterTexture's own computation): DXT levels are measured in 4x4 blocks with a 4x4 minimum,
8 bytes a block for DXT1 and 16 for DXT3/5; A8R8G8B8 is 4 bytes a texel. Pixel layouts:

- **DXT1/3/5**: standard S3TC blocks, row-major blocks, not swizzled. DXT1 with `c0 <= c1` is the 3-colour +
  transparent mode.
- **A8R8G8B8 swizzled**: each level Morton-ordered on its own dimensions: x and y bits interleaved from bit 0, x
  first, and the surplus bits of the larger dimension on top (the `Unswizzle` of src/common/gfx/d3d9Backend.cpp,
  = d3dComputeSwizzleMasks 0xe3a20). Bytes B, G, R, A.
- **LIN_A8R8G8B8**: one level, rows of `width * 4` bytes padded to 64 (XGSetTextureHeader with pitch 0).

No palettes (the 0x0f block is not one), no cube maps and no volume textures occur.

## 3. Loading: the level-wide texture list

**psiCreateMapTextures 0xddf90** (PS2 0x11d800), `void __cdecl (map_tag *map)` (stack). Called once per pack,
from parsemap_block_entity_params 0xa6180 at the pack's **first entity block** (`StartingNewMap`), so a pack with
no entity block never creates its textures (and takes no numbers). `map_tag` (0x14 bytes, 0xa5eb0):
`+0 paletteHeaderData, +4 numPaletteHeaderEntries, +8 texHeaderData, +0xc numTexHeaderEntries, +0x10 texData`.

```c
DAT_00194810 = NumXboxTexLoaded;                 // 0x19480c; this pack's first index (psiCreateEntityGfx reads it)
for (i = 0; i < map->numTexHeaderEntries; i++, NumXboxTexLoaded++) {
    t = texHeaderData[i].data;                   // +8, the 0x18 payload (0 or -1 if the pack had no data block)
    Tex[NumXboxTexLoaded] = t;                   // 0x2abe80, u32 pointers
    texData[i].index = NumXboxTexLoaded;
    if (!t || t == -1) { texData[i].index = 0; continue; }
    if (t->magic != 'KXT\6' && (t->magic & 0xffff0000) != 'TD') {   // unknown: disable
        t->fps = 1; t->numFrames = 1; t[+0x54]... = 0; texData[i].index = 0; continue; }
    t->fps = (t->fps == 0 || t->fps >= 60) ? 1 : 60 / t->fps;        // ticks per frame
    if (t->numFrames == 0) t->fps = 1;
    if ('TD')
        for (f = 0; f < t->numFrames; f++)       // share the source's D3D slots
            t->handle[f] = Tex[(t->magic & 0xffff) + 1]->handle[f];
    else {
        (texture memory statistics per format)
        for (f = 0; f < t->numFrames; f++)
            t->handle[f] = RegisterTexture(t->width, t->height, type(t->formatType, t->alphaBits),
                                           t->levels, pixels + f * t->frameSize, t->linear);
    }
}
```

`NumXboxTexLoaded` (0x19480c) and `DAT_00194810` are set to 1 by maybePsiResetResources 0xe02e0, which also
points `Tex[0]` at a dummy (0x2ae4f8). So within a level the textures are numbered 1, 2, 3... in pack load order
(bundle directory order; the level's own map pack, bundle type 1, comes last), and a `TD` with load index `v`
names `Tex[v + 1]`, i.e. the (v)th texture created in the level counting from 0. Offline, `parse_tex.Level`
rebuilds this numbering from the bundle and resolves every `TD`; in all 32 bundles every `TD` resolves, to an
earlier texture, which is always a `KXT` with the same size and format.

**RegisterTexture 0xe4400**, `int __cdecl (width, height, type, levels, pixels, linear)` (stack, 6 args): takes the
first free slot of the 2048-entry D3D texture table (Gfx + 0x6c9c, 36-byte `D3DTexture` + bookkeeping), picks the
format (table above), sums the mip chain size, moves the pixels up to 128-byte alignment, `XGSetTextureHeader`,
`D3DResource_Register`, returns the slot. Reimplemented in src/action/engine/Direct3D/d3dSeam.cpp.

**psiCreateEntityGfx 0xde220** (PS2 0x1173a8), per mesh: rebases each primitive's texture indices onto the
level list, in place:

```c
for each prim p:
    g = (p->tex0 == 0xffff) ? 0 : p->tex0 + DAT_00194810;     // local index -> level index
    p->tex0 = (Tex[g]->handle[0] != 0) ? g : 0;               // Tex[g]+0x54
    (same for p->tex1)
```

So on disk `tex0`/`tex1` are **indices into the pack's own 0x10 list** (0xffff = none). Checked: no primitive in
any pack indexes past its pack's list.

## 4. Drawing: textures and render states per primitive

**RecurseAndDrawBoxes 0xdd4c0** (Ghidra's body stops at 0xdd56c; the per-primitive loop is the untracked tail
0xdd56d-0xdd8d2, read from raw bytes). After binding the mesh's buffers, for each 12-byte primitive
`{u16 tex0, u16 tex1, u32 stripLen-2, u16 stateFlags, u8 changeMask, u8 bonePalette}`:

```c
if (p->bonePalette) upload the palette's bone matrices (d3dSetSkinMatrix 0xe4e40)   // spec-pose.md
if (p->changeMask) {
    if (changeMask & 0x02) d3dSetupRenderStatesAndFog((stateFlags >> 1) & 3);      // 0xe5060 blend mode
    if (changeMask & 0x04) {                                                      // stage 1
        T = Tex[p->tex1];
        d3dSetTextureStage1(T->handle[(GameTicks / T->fps) % T->numFrames], p->tex0 != 0);   // 0xe3c10
    }
    if (changeMask & 0x08) d3dSetCullMode(!((stateFlags >> 3) & 1));             // 0xe5610
    if (changeMask & 0x10) d3dSetRenderState1((stateFlags & 0x10) ? 0xb4 : 1);    // 0xe5650 alpha ref
    if (changeMask & 0x20) d3dSetRenderState2((stateFlags >> 5) & 1);             // 0xe4fc0 depth write
    if (changeMask & 0x40) d3dSetDeferredTextureState((stateFlags >> 7) & 1,
                                                      (stateFlags >> 8) & 1);     // 0xe5230 wrap U, V
    if (changeMask & 0x01) morph streams on/off by stateFlags bit 0                // spec-facial.md 3.6
}
T = Tex[p->tex0];
d3dSetTextureStage0(T->handle[(GameTicks / T->fps) % T->numFrames]);              // 0xe4950, every primitive
d3dDrawIndexedVertices(first, p->stripLen);                                       // strip of stripLen+2 indices
...
after the loop: d3dSetTextureStage1(0, 0); morph streams off.
```

`GameTicks` is the u32 at 0x1f65b4 (read by the AI and sprite code as the game clock; likely 60 Hz), so an
animated texture shows frame `(ticks / (60 / fps)) % numFrames`. A primitive with `tex0 = 0xffff` binds `Tex[0]`,
the dummy: untextured.

### 4.1 stateFlags and changeMask

Each state group is re-sent only when its `changeMask` bit is set; otherwise the primitive inherits the state the
previous primitive (or the previous draw) left. The first primitive of every mesh has changeMask 0x7b (1,140
meshes) or 0x7f (4), so a mesh sets everything but stage 1 up front; stage 1 is switched off at the end of every
mesh, so it starts off. `parse_tex.effective_states()` replays this.

| changeMask bit | stateFlags bits | state | meaning (confirmed from the callee) |
|---|---|---|---|
| 0x01 | bit 0 | morph | bind the morph-target streams (spec-facial.md) |
| 0x02 | bits 1-2 | blend | 0 and 3: `SRCALPHA, INVSRCALPHA`, op ADD (normal alpha blend); 1: `SRCALPHA, ONE`, op ADD (**additive**); 2: `SRCALPHA, ONE`, op REVSUBTRACT (**subtractive**). NV2A methods 0x40344/0x40348/0x40350 in d3dSetupRenderStatesAndFog; any mode but 0 also turns the fog colour black (mode 3 never occurs) |
| 0x04 | (tex1) | stage-1 texture | bind `tex1` on stage 1, or unbind if 0 (below) |
| 0x08 | bit 3 | cull | bit set = **double-sided** (cull off), clear = cull on (0x901 = CCW culling) |
| 0x10 | bit 4 | alpha ref | bit set = 0xb4 (**alpha-test cutout at 180/255**), clear = 1 (discard only alpha 0) |
| 0x20 | bit 5 | depth write | bit set = z-write on; clear on transparent/additive passes |
| 0x40 | bits 7, 8 | address U, V | bit set = WRAP (1), clear = CLAMP (3), stage 0 |
| 0x80 | - | - | never set |
| - | bit 6 | ? | set on 4,491 of 4,960 primitives; never read by the draw loop (exporter flag, perhaps "lit") |
| - | bits 9-15 | - | never set |

Counts over the 1,223 distinct meshes (4,960 primitives) in the 32 bundles: blend 0 on 4,631, additive 324,
subtractive 5; double-sided 546; alpha ref 180 on 6; depth write on 4,515; wrap U and V on 4,928; `tex0` none on
161; `tex1` set on 174 (78 with changeMask 0x04).

### 4.2 tex1: the environment map

d3dSetTextureStage1 0xe3c10, `void __cdecl (int slot, int hasTex0)` (stack; the second argument is only cached).
With a texture it sets `Gfx_MiscModeFlags |= 0x20` (the vertex shader permutation index, so a different vertex
shader runs - likely the one generating the reflection coordinates; the shader itself was not read) and these
deferred texture-stage states (D3D__TextureState 0x1117d0, 32 dwords a stage; D3DTSS numbering of the XDK):

| state | no tex1 | with tex1 |
|---|---|---|
| stage 0 COLOROP (0x111800) | MODULATE2X (texture x diffuse x 2) | SELECTARG2 (diffuse only) |
| stage 0 COLORARG1 (0x111808) | TEXTURE | TEXTURE |
| stage 0 ALPHAOP (0x111810) | MODULATE | (left as MODULATE: tex0 alpha x diffuse alpha) |
| stage 1 COLOROP / ARG1 / ARG2 | DISABLE | MODULATE2X / TEXTURE / CURRENT |
| stage 1 ALPHAOP / ARG1 / ARG2 | DISABLE | MODULATE / TEXTURE / CURRENT |

So with an environment map the colour is `envmap x diffuse x 2` and the alpha is `tex0.a x envmap.a x diffuse.a`:
`tex0` only masks the reflection through its alpha (one such base texture is literally called
`trenchcoat_modmap2`), though 161 of the 174 primitives with `tex1` have no `tex0` at all (every `tex0`-less primitive is such a pass). The textures used as `tex1` are all reflection
maps by name (`Enviromenttest10` x64, `room_refmap` x28, `refmap` x17, `reflection`, `kiko_reflect`...), and the
primitives carrying them are mostly additive with depth write off (114 of 174, `stateFlags` 0x182/0x18a): a
reflection pass drawn over the base primitive. The other 60 are normal-blend chrome (59 with depth write).

The base colour path without `tex1` is `texture x vertex diffuse x 2` (MODULATE2X): vertex lighting at 0.5 is
"unlit", which is why gfxSetCharacterLightIntensity(0.5) is the neutral setting for characters.

### 4.3 Blender mapping (suggested)

- Image: the PNG of `tex0` (frame 0; `--frames` writes all frames of an animated texture). UVs from the mesh.
- Extension: REPEAT when wrap U/V (bits 7/8), else EXTEND (clamp).
- Blend: `alphaBits == 0` and blend 0 -> OPAQUE; alpha ref 180 (bit 4) or 1-bit alpha (DXT1 with alphaBits 1) ->
  alpha CLIP; blend 0 with alpha -> BLEND; additive (1) -> an Add shader / emission with BLEND and no depth write;
  subtractive (2) is rare (5 primitives).
- Double-sided (bit 3) -> backface culling off; otherwise on.
- `tex1` -> a reflection/sphere-mapped texture added on top, masked by `tex0`'s alpha.

## 5. Tool

`python tools/anim/parse_tex.py`:

- `list [07xxxxxx | 01xxxxxx.bin ...]` - every texture of the packs (default: the dump folder, each pack resolved
  through the bundle holding an identical copy).
- `png OUTDIR [--bundle 07xxxxxx] [--pack 01xxxxxx] [--name substr] [--frames] [--mips]` - decode to PNG
  (`pack_index_name.png`), RGBA 8-bit, no dependencies beyond the standard library.
- `validate [--png OUTDIR]` - walks all 32 bundles and the dump, checks every block's byte accounting, resolves every
  `TD`, decodes every frame and mip level of every distinct texture.
- `skin 05xxxxxx [--png OUTDIR]` - the textures and effective render states of a skin's meshes.

Importable: `Level(bundle_path)` (packs in load order, `TD`s resolved), `Pack(bytes, name)` (`.textures`,
`.meshes`, `.prims(hash)`), `Texture.decode(frame, level) -> (w, h, rgba)`, `write_png`, `material(stateFlags)`,
`effective_states(prims)`. The block walk and the mesh primitives come from `parse_gfx.py`.

### Validation (all 32 bundles and the dump)

```
1955 packs in 32 bundles, 22360 texture entries: KXT 16943, TD 5417
formats: DXT1 18257, A8R8G8B8 2686, DXT5 1357, LIN_A8R8G8B8 58, DXT3 2
distinct KXT textures decoded (every frame, every mip): 3219 of 3219
  (DXT1 2530, A8R8G8B8 350, DXT5 335, LIN_A8R8G8B8 2, DXT3 2)
TD duplicates unresolved: 0
dump: 57 packs, 998 textures, 998 with pixels resolved
0 errors (block sizes, frame sizes vs mip chains, header/record/data counts)
```

Checked by eye: the face `bond_head` (256x256 DXT1) and `Watchface`, the swizzled non-square `pause_bg` (512x256)
and `buttons` (256x128), the linear `Fire2` and `WomanBack`, the DXT5 `METALTHIN_GrateRound` (cut-out holes),
the DXT3 `STONE_markingdown` (legible lettering) and the environment map `Enviromenttest10` all decode correctly.

### Textures of skins 05000007 and 050000ab

All primitives: blend alpha, single-sided, alpha ref 1, depth write on, wrap U/V, no `tex1`.

**05000007** (level 07000001), rigged mesh 020004c2 in pack 01000092, 9 primitives:

| tex0 | texture | size | format |
|---|---|---|---|
| 0 (x2) | bond_head | 256x256 | DXT1, 7 mips |
| 1 | bond_tongue | 32x32 | DXT1 |
| 2 | Bond_eyeball (TD -> 01000075 #2) | 64x32 | DXT1 |
| 3 | newcombatvest | 256x256 | DXT1 |
| 4 | bond_combat_newpant | 256x256 | DXT1 |
| 5 | ammopouch | 64x64 | DXT1 |
| 6 | bond_combat_hand | 64x64 | DXT1 |
| 7 | pouch2 | 32x32 | DXT1 |

(Pack 01000092 has 8 textures; the first three primitives, with morph on, are the head.)

**050000ab** (level 07000005), rigged mesh 020009d0 (first-person arms) in pack 010001f8, 9 primitives:

| tex0 | texture | size | format |
|---|---|---|---|
| 0 | Watchring (TD -> 01000071 #0) | 128x128 | DXT1 |
| 1 | Metal_Tile_Sil (TD -> 01000071 #1) | 64x32 | DXT1 |
| 2 | Watchbutton | 32x32 | DXT1 |
| 3 | Watchstrap | 64x32 | DXT1 |
| 4 | Watchface | 128x128 | DXT1 |
| 5 | combat_cuff | 64x64 | DXT1 |
| 6 | combat_sleeve2 | 128x64 | A8R8G8B8 (swizzled) |
| 7 | combatpalm | 256x256 | DXT1 |
| 8 | comabat_hand2 | 256x256 | DXT1 |

## 6. Functions

| Function | Xbox | PS2 | Convention | Role |
|---|---|---|---|---|
| parsemap_handle_block_id | 0xa6820 | 0x1d17c0 | cdecl (char isMap) | block dispatch; 0x10 inline |
| parsemap_block_palette_header | 0xa6030 | 0x1d2760 | void, globals | 0x0f: counts, allocates texData |
| parsemap_block_texture_data_Xbox | 0xa6110 | 0x1d2980 | void, globals | 0x17/0x18: hash -> texData, bind payload |
| parsemap_block_entity_params | 0xa6180 | 0x1d1ca8 | void, globals | first entity: psiCreateMapTextures |
| psiCreateMapTextures | 0xddf90 | 0x11d800 | cdecl (map_tag*) | level numbering, TD sharing, RegisterTexture |
| RegisterTexture | 0xe4400 | - | cdecl, 6 stack args | D3D slot, format, 128-byte alignment |
| psiCreateEntityGfx | 0xde220 | 0x1173a8 | cdecl (celglist*, map_tag*, memType) | prim tex0/tex1 local -> level index |
| RecurseAndDrawBoxes | 0xdd4c0 (loop 0xdd56d-0xdd8d2) | - | cdecl (geomIdx) | per-primitive states and textures |
| d3dSetTextureStage0 | 0xe4950 | - | cdecl (slot) | stage 0 texture |
| d3dSetTextureStage1 | 0xe3c10 | - | cdecl (slot, hasTex0) | stage 1 env map and its combiner states |
| d3dSetupRenderStatesAndFog | 0xe5060 | - | cdecl (mode) | blend mode |
| d3dSetCullMode | 0xe5610 | - | cdecl (enable) | cull |
| d3dSetRenderState1 | 0xe5650 | - | cdecl (ref) | alpha ref |
| d3dSetRenderState2 | 0xe4fc0 | - | cdecl (enable) | depth write |
| d3dSetDeferredTextureState | 0xe5230 | - | cdecl (wrapU, wrapV) | stage 0 address modes |
| maybePsiResetResources | 0xe02e0 | - | - | NumXboxTexLoaded = 1, Tex[0] = dummy |

## 7. Open questions

- Header 0x2c / 0x30 (0/1, mostly on UI textures) and 0x10-record byte 0 / byte 1 have no reader in the load or
  draw path; they may be read by the sprite/HUD code through the texture hashcodes, or be exporter leftovers.
- `stateFlags` bit 6 is set on 90% of primitives and never read by RecurseAndDrawBoxes.
- The vertex shader selected by `Gfx_MiscModeFlags & 0x20` (tex1 on) was not disassembled, so the exact
  reflection texture coordinates (sphere map vs camera-space reflection) are inferred, not read.
- 0x10-record sizes differ from the real texture size in 2,366 entries (mostly half); `texData[i].w/h` carry the
  0x10 values, so any code that scales UVs by them would see the PS2 size. No such reader was found.
- The global order assumes the loader creates packs in bundle directory order (LoaderProcess 0xbe7b0); it is
  confirmed by every `TD` resolving to a texture of the same size and format, 4,266 of them also by name.
