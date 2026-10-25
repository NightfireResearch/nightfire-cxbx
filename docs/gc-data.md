# The action engine on the GameCube's data

A spike: our action engine (action_standalone, docs/action-standalone.md) loading the GameCube version's files (the
US release) in place of the Xbox's, behind a build switch, as docs/ps2-data.md (branch `ps2-data`) does for the
PS2's. The game code stays the Xbox port; what changes is how the files are found and how the formats that differ
become what the Xbox code expects.

Status (25 October 2026): the disc's files are served (milestone 1), every level bundle converts to the Xbox's layout
(milestone 2: textures, placements, collision, paths, skins, scripts, sequences), the rigid meshes to KXEs
(milestone 3) and the skinned ones, with the heads' morph targets, to skinned KXEs (milestone 7), all checked offline
against the US Xbox disc's twins. In game HendersonB, CastleCourtyard and SpaceStationD load from the GameCube's
bundles and draw from the GameCube's meshes and textures on the test runner; CastleCourtyard's frames are within
2-2.6 grey levels of the Xbox build's at the same moments. Cutscenes (the loadables) and sound are still to come.
See [Milestones](#milestones) and [In game](#in-game).

Sources: the GameCube disc as extracted by the extractor project (Q:\nightfire-re, read only:
`iso_extract/gc_us`), the US Xbox disc (`iso_extract/xbox_us`), the GameCube executable in Ghidra
(`/GameCube_US/Nightfire.elf`; the ELFs on the disc have no symbol table, the names are the project's own, see
docs/gamecube-checks.md) and our Xbox code. Every comparison below was made file by file between the two discs with
`gcdata_check` ([Tools](#tools)).

## The switch

A CMake option, **`NIGHTFIRE_ACTION_DATA_GC`** (default OFF), for the standalone targets only (action_standalone,
action_standalone_test). On, they are compiled with `NIGHTFIRE_DATA_GC=1` and:

- `src/action/engine/FS.cpp` (the Xbox's `filesys.dNN` archives) is replaced by `src/action/gc/GcFs.cpp`;
- the files of `src/action/gc/` are added;
- the seams in game files are guarded with `#if NIGHTFIRE_DATA_GC` ([Seams](#seams)).

Off, nothing is compiled differently. Checked on 25 October 2026 with `tools/pe_compare.py` (from the PS2 branch):
action_standalone and action_standalone_test built with the GameCube work in the tree have the same `.text`,
`.data`, `.reloc`, `.rsrc` and `.rdata` as those built before it, but for the debug directory's timestamp. The one
seam in a game file (Anim.cpp) comes after the file's last runtime `__LINE__` use, so moves no assert's line.
actioninject never gets the switch.

`DiscPath` (settings.ini) names the extracted GameCube disc: the folder holding `Nightfire.elf`, `common\` and
`gc\`.

```
cmake -B build/gcon -A Win32 -DPython3_EXECUTABLE=C:/Python314/python.exe -DNIGHTFIRE_ACTION_DATA_GC=ON
cmake --build build/gcon --config Release --target action_standalone
```

## The disc

No archive: the GameCube's disc file system holds every file loose (an extraction is a folder tree). The action
engine's files are under `gc\` (bundles, sound, movies) and `common\` (text, the pause screen's film); the driving
engine has `driving\` and `driving.elf` (not covered here). Endianness: the GameCube is big-endian, and the data is
not swapped throughout: each format below says which way round it is.

| GameCube | Size | Xbox twin |
|---|---|---|
| `gc\<hash>.bin`, 104 bundles: levels 0700xxxx (EDL-compressed), their sequence bundles 07d0xxxx and 07e0000n, cutscene loadables 078/079/07A0xxxx, `common.bin` (the levels' sequence groups) | 270 MB | `<hash>.bin` in `filesys.dNN` (1.3 GB with everything) |
| `gc\audio\SFX\`: `English\SB_n\SB_m.{sfx,shf,sbf,shg}`, `music\MFX_n\MFX_m.{ssd,smf}`, `SBInfo.sbi`, `MFXInfo.mxi` | 240 MB | the same names in the archives, but no `.SHG` |
| `gc\movies\<hash>.h4m`, 22 movies | 425 MB | `eurocom\30_fps\<hash>.xmv`, the same 22 hashcodes (373 MB) |
| `common\*Txt.dat`, `Woman.rle` | 3.7 MB | the same, byte for byte |
| `common\*TxtU.dat`, `08000001.bin`, `08000002.bin`, `TuningVars.txt`, `def_Startup.cfg`; `gc\data\` (`lightmap.tpl`, `pcl.tpl`, `bumpmap.raw`); `gc\language\american` (empty) | small | none (the GameCube's own, or leftovers) |

`gc\07000001.bin` (HendersonA) is 0 bytes in this extraction; every other level is there.

## Format by format

| Format | Same? | What differs | Known | Unknown | Seam in our code | Size |
|---|---|---|---|---|---|---|
| File system | no | loose files; names mapped by `GcDisc_Path` (`ENGLISH/SB_0/SB_0.SHF` is `gc\audio\SFX\English\SB_0\SB_0.shf`, a text bank `common\<name>`, a bundle `gc\<hash>.bin`) | all | - | FS.cpp, whole (GcFs.cpp) | small, done |
| Level bundle | nearly | EDL-compressed with the big-endian flag (the same bit stream, read from words the other way round); the header's two words big-endian, the directory's entries little-endian as the Xbox's. The anim sequences (type 4) are not in the level's bundle but in its group's (07e0000n, from `common.bin`) and its own (07d0xxxx). Extra entry types: 12 (a loadable, its name a binary hashcode; the Xbox's bundles have none), 13 `GC EXTRA STATIC` and 14 `GC EXTRA DISCARD` (the meshes' data: 1.6-2.7 MB and 0.9-1.4 MB in the three levels looked at) | all | the loadables' mode (as the PS2's) | GcBundle.cpp, in GcFs.cpp's open | small, done |
| Skins, anim scripts, skeletons, scripts, menus (types 3, 5, 6, 7, 8); sequences (4) | yes | byte for byte the Xbox's: over 29 levels 974 skins, 16,097 anim scripts, 1,260 skeletons, 393 of 394 scripts, 29 menus, 15,969 sequences. The Xbox's levels also hold 27 "Spec" skins and 30 scripts the GameCube's lack (HendersonB's two are in its 078/079 loadables) | all | - | none (the sequences: GcBundle_Load) | done |
| Map packs (types 0, 1, 2, 11): container, header, placements, portals, LOD, paths, collision | nearly | block headers little-endian on both; most payloads big-endian (placements, map header, paths, LOD, sounds, entity parameters 0x1f, collision 0x2f), some little-endian as on the Xbox (palette headers, portals, hashlists, AI paths, particles). Collision: the box's quantisation scale is a power of two's exponent (n for 2^-n) where the Xbox has the float | all; after conversion, over 1,831 packs: headers, LOD, portals, hashlists, AI paths, particles, sounds all the Xbox's; placements 1,815 the same (16 apart in float noise or padding); collision 6,850 of 7,322 the same, 393 more within 0.1% of bytes; entity parameters 15,236 the same, 217 within two bytes | the 62 entity parameter blocks and 79 collision blocks that differ more | GcMap.cpp | done |
| Textures | no | block 0x17: a 0x5c-byte big-endian header (name, size, GX format index, mip levels) then the GX's tiled texels, every level and frame. Three formats only in the level bundles: CMPR (16,016 textures), RGB5A3 (1,395), RGBA8 (437); no I4/I8/IA, no C4/C8 palettes. Every size a power of two from 4 to 512 but one (128x132, which the GameCube draws as 32x32). "DT" headers (another texture's frames) count every header of the level from 0, as the Xbox's do from 1 | all (GC psiCreateMapTextures 0x8002fd0c) | - | GcTexture.cpp: block 0x18 (KXT) | done |
| Static meshes | no | block 0x0c (the GameCube's mesh header, 0xac bytes, big-endian, magic 0x0f) where the Xbox has a KXE in 0x0d; its offsets reach into the bundle's GC EXTRA entries (vertex arrays in STATIC, strips and index streams in DISCARD). Not stored display lists: the game builds one per mesh as the level loads ([Meshes](#meshes)). Half the quads are cut along the other diagonal | all that a conversion needs | the layer flags beyond the blends; where the Xbox's alpha reference and clamping come from | GcMesh.cpp: 0x0c to 0x0d (KXE) as the bundle converts | done |
| Skinned meshes, morphs | no | the same 0x0c headers, the positions in bind pose, with runs of one or two skeleton bones and two 14-bit weights a position, which the GameCube applies on the CPU (SkinIt); a head's 22 morph targets in the same header (no 0x2b blocks on this disc, where the PS2's have them) ([Skinned meshes](#skinned-meshes)) | all that a conversion needs | - | GcMesh.cpp: skinned KXEs with bone palettes and morph streams | done |
| Text banks | yes | the nine `*Txt.dat` byte for byte the Xbox's; the GameCube adds `*TxtU.dat` | - | what reads `TxtU` | none | none |
| Fonts, button symbols | glyphs probably | the glyph tables are executable data (`GC FontInc.cpp`); the button symbols are the GameCube pad's | - | the GameCube's symbol table in Nightfire.elf (no symbols: find it by the Xbox table's layout) | SpecialChars, as the PS2's | small |
| Sound banks | container yes, codec no | the same files, big-endian (the `.SHF` sample headers' nine words; `.SFX` 98% of bytes the Xbox's once each word is turned round); samples Nintendo DSP ADPCM (8 bytes per 14 samples) with the DSP's 96-byte headers (coefficients, history) in the new `.SHG`; every sample mono, at lower rates ([Sound](#sound)) | the codec, the rates | the `.SFX` fields that differ | SFX.cpp LoadSoundBank, as the PS2's | medium |
| Music | container yes, codec nearly | `.SMF` markers big-endian, positions four times the byte offset; `.SSD` headerless IMA ADPCM, stereo, one byte (two samples) of each channel in turn, low nibble first, 32 kHz | the layout (by decoding) | the exact rate (32,000 bytes a second implies 32 kHz) | GcFs.cpp's reads, as the PS2's | small-medium |
| Streamed speech | n/a | none on either disc's language folder by that name; the GameCube has no streams at all | - | - | GcFs.cpp (a stream that cannot open is given up) | done |
| FMV | no | HVQM4 1.5 (`HVQM4 1.5` magic; 640x480, 33,366 µs a frame) where the Xbox has XMV (WMV2) | the container's header | the decoder (FFmpeg has none) | Fmv.cpp replaced | medium-large |
| Woman (pause background) | yes | `common\Woman.rle` byte for byte the Xbox's | - | - | none | none |
| `SBInfo.sbi`, `MFXInfo.mxi` | yes, turned round | each word big-endian; otherwise the Xbox's (the Xbox's padded to 2 KB) | all | - | GcFs.cpp | done |

## Seams

| Seam | Game file | GameCube code |
|---|---|---|
| The file system (whole file replaced) | engine/FS.cpp out of the build | gc/GcFs.cpp: loose files through GcDisc_Path, bundles converted whole when opened (gc/GcBundle.cpp, GcMap.cpp, GcTexture.cpp, GcMesh.cpp); for now silent stand-ins for the sound banks and an empty music list |
| The levels' swapped skins | engine/Anim.cpp AnimObjectNew | gc/GcAnim.cpp: the GameCube's own FixupSkinHashcode (0x80045298: Elite_B becomes Elite in PowerStationA1, Elite_Beret in EvilBase), since the Xbox's swaps in "Spec" skins the GameCube's bundles lack |
| Meshes | none: block 0x0c becomes 0x0d as the bundle converts (GcMap.cpp), so parsemap and psiCreateEntityGfx are the Xbox's | gc/GcMesh.cpp |
| The cutscene loadables (to come) | engine/Loader.cpp, as the PS2 branch's | the 078/079/07A bundles |
| Sound banks, music (to come) | sound/SFX.cpp LoadSoundBank; GcFs.cpp's reads | DSP ADPCM decoder, Xbox ADPCM encoder (the PS2 branch's) |
| Movies (to come) | engine/Fmv.cpp replaced | an HVQM4 decoder |

## Textures

GcTexture_ToKxt turns block 0x17 into the Xbox's texture header and frames, which psiCreateMapTextures registers
unchanged:

- **CMPR** is DXT1's blocks, four to an 8x8 tile, with the colours big-endian and each row's texels in the other bit
  order: reordered into DXT1, nothing lost.
- **RGB5A3** and **RGBA8** become swizzled A8R8G8B8 (RGB5A3 could go to A1R5G5B5 or A4R4G4B4 exactly, as half the
  memory, but which one changes per texel).
- Every mip level the GameCube has is kept.

Checked (`gcdata_check textures`, every level bundle): all 17,848 textures convert and every KXT decodes to the
GameCube's own texels exactly. Against the Xbox's twins (`all`): of 15,708 pairs compared, 12,456 decode to the same
texels (the DXT1 textures are the Xbox's blocks); the rest, mostly where the Xbox has DXT2-5 with 8-bit alpha and the
GameCube RGB5A3 with 3, average about 29 dB; 71 are at half the Xbox's size, 17 at another. The worst are the
WomanBack and cracked glass textures, whose texels differ outright.

The "DT" headers: `gcdata_check dt` resolves each against the texture the index names, counting the level's texture
headers from 0. 77 of 88 in HendersonB (169 of 212 in SkyRail) name a texture of the same name; the rest name one of
another name, probably the same texels under two names ("white_cloth_64" for "white cloth 64", "SHALWATER_anim1"
for "water_shalwater"). The Xbox's psiCreateMapTextures takes `Tex[index + 1]`, its Tex[0] being the default texture,
so the index is kept as it is.

## Meshes

Read from GC PsiDraw.cpp (Nightfire.elf in Ghidra): psiCreateEntityGfx's counterpart (0x8002f864) draws each mesh once,
as the level loads, into a display list through maybe_psiDrawClonedObject (0x8002ea08); later draws call that list.
parsemap's case 0x0c keeps the header's address in the cel, as the Xbox's 0x0d does.

**Where the data is.** The header's offsets count from the header itself and reach past its pack into the same
bundle: the loader reads the whole bundle into one buffer, and LoaderProcess (0x8005c444) only notes where the two
GC EXTRA entries are. The vertex arrays are in GC EXTRA STATIC, the strip groups, strip lengths and index streams in
GC EXTRA DISCARD, which psiPreDraw (0x8002d8a0) clears and gives back once the display lists are built. A few
headers (in 0x0c blocks larger than 0xac bytes) have their data inline in the pack; the offsets work the same way.

**The header** (GcMeshHeader in src/action/gc/GcMesh.h; big-endian words):

| Offset | Meaning |
|---|---|
| 0x00 | 0x0f (psiDrawObjectMatrix draws nothing else) |
| 0x04 | the name, 20 bytes |
| 0x18 | strip groups; 0x1c the position indices, 0x20 the positions (both must be non-zero) |
| 0x28, 0x2c | 0 on a rigid mesh |
| 0x30 | flags: 0x200 turns the depth writes off (a z mode of (1, 0, 3) where it is otherwise (1, 1, 3)) |
| 0x34 | bit 0: the layers' blend and alpha-compare states left as they are; bit 2: fogged, by the look of it |
| 0x38 | eight layers' flags: 0x02 additive, 0x04 subtractive (both with depth writes off), 0x40 the next layer's texture as a second stage in the same pass; bit 0 picks another alpha compare |
| 0x58 | -1: float positions (16 bytes a vertex: three floats, three signed bytes of normal with 7 fraction bits); else the byte at 0x5b is the position shift: 12 bytes a vertex, three signed halfwords of position with 16 - shift fraction bits, three of normal with 14 |
| 0x5f | the texture coordinates' shift: two signed halfwords with 16 - shift fraction bits |
| 0x60 | the centre, three floats, added to every position (the draw adds it to the matrix's translation) |
| 0x70 | the strip groups, 20 bytes each: a word of counts - the strips' vertices less two each (bits 0-15), the strips (16-27), **double-sided (28)**, the layers with a texture less one (29-31) - then eight halfwords, each layer's texture in the pack (-1 for none) |
| 0x74 | each strip's vertices less two (halfwords) |
| 0x78 | the position indices (halfwords; one index serves position and normal, the array interleaving them) |
| 0x7c | the positions and normals (GX attributes 9 and 10) |
| 0x80 | the texture coordinate indices; 0x84 the coordinates (attribute 13) |
| 0x88 | the colour indices; 0x8c the colours (attribute 11, RGBA8; 0 for none) |
| 0x28, 0x2c (skinned) | a head's morph targets (22) and morph positions (239 or 285); 0 on the others |
| 0x90-0xa0 | a skinned mesh's five offsets ([Skinned meshes](#skinned-meshes)); 0 on a rigid one |
| 0xa4 | the display list's size |

The vertex formats are GXSetVtxAttrFmt's (0x8017f9f8) through the draw's format switch (0x80036fd8, cases 1 and 12).

**The draw order.** For each of the eight layers (skipping one whose layer before has flag 0x40), each strip group
with a texture in that layer draws its strips as GX triangle strips (`0x98`); a group with none in that layer only
moves the cursors on. Per strip vertex the display list gets the position index (twice: position and normal), the
colour index when the mesh has colours, and the coordinate index. The position indices are walked from the start
for every layer; a group's coordinate and colour indices come once for each of its layers with a texture, in layer
order. A group's texture numbers are the pack's, offset by where the map's textures start (as the Xbox's).

**The conversion** (GcMesh_ConvertToKxe): every strip vertex (its three indices) becomes a 28-byte KXE vertex,
shared where the indices repeat; the strips are joined with degenerate triangles into one strip per texture and
state, wound as they are (GX strips turn as D3D's do); normals are turned round, colours made B, G, R, A, and a mesh
with no colours gets white. The states: the layer's blend, depth writes unless the mesh's flag or a blend turns them
off, wrap in both directions, double-sided from the group's bit 28 - and, with `FIX_ORIGINAL_GAME_BUGS`, also for
every blended layer with depth writes off (below).

**Checked** (`gcdata_check meshes`, every level, against the Xbox's twins: the same entity in the same pack):

| Check | Result |
|---|---|
| meshes / converted / skinned (left out) / nothing to draw | 15,515 / 13,618 / 448 / 1,449 |
| compared (the Xbox's twin rigid and drawing something) | 13,558 |
| Xbox vertices at one of ours (2 mm) / ours at an Xbox vertex | 4,272,343 of 4,439,746 (96.2%) / 4,203,345 of 4,369,361 (96.2%) |
| at those: texture coordinates / normal (cos > 0.9) / colour the same | 91% / 89% / 93% (another 1.5% with the alpha alone different) |
| our triangles: the Xbox's, wound the same / the quad's other diagonal / wound the other way / not on the Xbox | 1,421,245 / 1,610,426 / 12,086 / 177,941 of 3,221,698 |
| on the triangles that are the Xbox's: texture / double-sided / blend / depth writes the same | 97.3% / 98.4% (1.6% double-sided on ours only, 0.01% on the Xbox's only) / 99.9% / 99.96% |
| on them: wrapping / alpha reference 180 the same | 99.5% (ours wraps, the Xbox's clamps) / 99.3% (the Xbox's alone has it) |

The vertices that miss are almost all in 83 meshes (skies, skylines, far scenery) whose large position shift leaves
steps of more than 2 mm. Half the triangles are the other diagonal of the same quad: the two exporters cut quads
differently, as the PS2's did. The Xbox's twins also have no geometry for 60 invisible blocking meshes (their only
texture "blocking_polly") which the GameCube's have. They are converted like the rest; none has shown in the frames
looked at, but whether the game hides them (they are collision placements, by their names) is still to check.

### Skinned meshes

Read from GC psiDrawSkinObjectMatrix (0x8000cec4), SkinIt (0x8000c740) and the morph pass (0x8000c9cc). The GameCube
skins on the CPU: each frame SkinIt transforms the mesh's positions and normals into a buffer, which the mesh's
display list then draws in place of the position array (maybe_psiDrawClonedObject's second argument). The skins,
skeletons and the bone matrices (psiBuildMatrixPalette 0x8000d0b8) are the Xbox's; only the mesh's data differs.

| Offset | Meaning |
|---|---|
| 0x7c | the positions and normals as a rigid mesh's, in bind pose, model space |
| 0x90 | per position, two signed halfwords: bone A's and bone B's weights, 14 fraction bits (8192 + 8192, 13107 + 3276) |
| 0x94 | runs: a skeleton bone and a count of positions, halfwords; the runs cover the positions in order, each moved by its bone |
| 0x98 | per run, the second bone (0xff for none: the first alone, at weight 1) and the run's count again |
| 0x9c | a head's morph deltas: per morph position, the 22 targets' x, y and z as signed halfwords in the positions' fixed point |
| 0xa0 | the morph positions, halfwords, rising (0x2c of them) |

SkinIt loads the positions with the position shift, and a position's normal is turned by bone A only. The morph pass
runs first and adds, for each morph position, the weighted deltas of the targets the game names (Morph_Index,
Morph_Weight) to the bind position, as the PS2's SkinIt and the Xbox's morph shaders do. A bone number is a
skeleton bone: the palette psiBuildMatrixPalette builds has one matrix per bone of the skeleton.

The conversion gives each KXE vertex (32 bytes) the Xbox's bone word: its two bones' slots in the primitive's palette
(as the skinned shader's constant registers) and bone A's weight as a u16. Each strip's bones go into the open
palette while it has room (53 slots), else a new one, loaded by the primitive that changes it; every mesh on this
disc fits one. A head's morph targets become the KXE's morph streams as the PS2 conversion makes them: the primitives
that draw a morph position get state bit 0, the vertices they draw go first, and each delta is written as
`(int16_t)(model units * 32767)`.

**Checked** (`gcdata_check skins`, every level, against the Xbox's twins):

| Check | Result |
|---|---|
| skinned meshes / converted / compared | 448 / 448 / 448, one palette each; 141 heads with morph targets |
| our vertices at an Xbox vertex / the Xbox's at ours | 574,538 of 574,538 / 577,300 of 577,321 |
| at those: the same bones / bone A's weight (0.01) where they are | 98.4% (another 1.6% where the Xbox blends a second bone at under 5% and the GameCube does not; 84, 0.015%, differ) / 100% |
| normal (cos > 0.9) / texture coordinates / colour the same | 99.4% / 99.1% / 95.7% |
| our triangles: the Xbox's, wound the same / the quad's other diagonal / wound the other way / not on the Xbox | 769,487 / 20,947 / 72 / 151 of 790,657 |
| on those: double-sided the same | 762,169 of 762,313 (136 ours only, 8 the Xbox's only) |
| morph vertices that move: every delta of every target the same / within 4 / within 32 (1 mm at full weight) / more | 432 / 19,460 / 17,029 / 503 of 37,424 |

The deltas differ by the GameCube's coarser step (a position shift of 2 to 4 is 2 to 8 Xbox delta units); the 503 are
probably two morph positions at one place, as on the PS2's (where the lips meet).

### Culling

The data says which surfaces are double-sided. Each strip group's bit 28 sets the GameCube's cull mode as the group
is drawn: set, `GX_CULL_NONE`; clear, `GX_CULL_BACK` (GC GX.cpp's state flush 0x80036244 writes the GX generation
mode through the GX's own table, 0 2 1 3 at 0x8030f2e4). So the bit is the Xbox's primitive state bit 3.

The effects the GameCube loses are **meshes**: SpaceStationD's explosion is pack 0x01000081's flat quads
(`Blast_frame_1` to `_17`, `Single_Blast_frame`, `flash`, `ExplodeMarker`, `Vertical_smoke`), the bullet impacts pack
0x0100002d's `HitSpark` and `gas_steam`, the dust pack 0x01000004's `smoke_1_dust` and `smoke_2_fire`. All are
additive with depth writes off. On the Xbox every one of them is double-sided (state 0x1ca); on the GameCube most
have bit 28 clear (`cloud_n`, `smoke`, `gas_steam` and a few others have it set), so the GameCube culls them whenever
they face away from the camera. Over every level, 10,544 blended triangles were double-sided on the Xbox and not in
the GameCube's data, against 12,205 double-sided in both and 9,919 single-sided in both; giving the double-sided bit
to every blended layer with depth writes off (the mesh flag 0x200) leaves 148 the Xbox has and we lack and 186 we
have and it lacks. That is the GameCube release's bug, and the conversion fixes it only with
`FIX_ORIGINAL_GAME_BUGS` (on by default; docs/original-bug-fixes.md, "GcMesh StripStates"): built without it, bit 28
alone decides, and the effects are culled as on the retail GameCube. Under the modding design (branch
`modding-design`, docs/modding.md) the fix belongs in the fixes module with the other original-bug fixes, once that
module exists.

The particles are drawn otherwise: GC psiDrawParticleList (0x8002af38) sets `GX_CULL_BACK` (its state 1, through the
GX's cull mode call, 0x80181688) for every particle quad, where the Xbox's sets no cull mode that we have found and
draws with whatever the last mesh primitive left. In our port the particles keep the Xbox's behaviour.

## Sound

What the GameCube's sound data is, against the Xbox's (`.SHF` headers of every bank, paired sample by sample: the
Xbox's 11 stereo samples are two mono samples each on the GameCube, which makes the counts agree, 7,750 to 7,750):

| Xbox | GameCube | Samples |
|---|---|---|
| 32,000 Hz mono | 19,000 Hz | 4,719 |
| 32,000 Hz mono | 11,025 Hz | 1,406 |
| 22,050 Hz mono | 12,000 Hz | 1,554 |
| 19,000 Hz mono | 19,000 Hz | 46 |
| 32,000 Hz stereo | 11,025 Hz, two mono | 18 (9 pairs) |
| 44,100 Hz stereo | 19,000 Hz, two mono | 4 (2 pairs) |
| 32,000 Hz mono | 22,050 Hz | 3 |

- **Format:** every GameCube sample is 4-bit DSP ADPCM (the GameCube's audio DSP's), the Xbox's 4-bit Xbox ADPCM
  (IMA). The lengths agree (median 0.4% apart, 95% within 2%), so the samples were resampled, not cut.
- **Sizes:** the banks are 78 MB of sample data on the GameCube against 148 MB on the Xbox.
- **Why it sounds worse:** most effects play at 19 kHz or less (11-12 kHz for a third of them) where the Xbox's
  play at 22-32 kHz, so everything above about 9.5 kHz (5.5-6 kHz for the 11-12 kHz third) is gone; the stereo
  effects are split into two mono samples, played as two voices.
- **Music:** headerless IMA ADPCM, stereo, 32 kHz (32,000 bytes a second against the Xbox's 49,613 for 44.1 kHz
  stereo Xbox ADPCM); `.SMF` markers the Xbox's, big-endian, with positions in units of a quarter byte and each
  section start's decoder state as two (step index, sample) halfword pairs.

So in sound the GameCube's data is the poorer: a GameCube build can only play what is there. Converting means
decoding DSP ADPCM with the `.SHG` coefficients and encoding Xbox ADPCM at the GameCube's rates, as the PS2 branch
does from SPU ADPCM; the music is the same codec family as the Xbox's and wants re-blocking and its marker
positions moved.

## In game

On the test runner (vr-desktop, `C:\nightfire-gc\run`: `DiscPath=C:/nightfire-gc/gcdisc`, a copy of the bundles,
`common\` and the two sound tables the levels tried need; `DumpEvery=250`), `action_standalone.exe -level <level>`,
no input, 90-100 s each; the Xbox build (the default build, in `C:\nightfire-gc\xbox`, reading the runner's Xbox
disc) ran the same:

| Level | GameCube build | Against the Xbox build |
|---|---|---|
| HendersonB (07000002) | the bunker's rooms and corridors, the crates, lamps, the gun and the first-person hands from the GameCube's meshes and textures, the HUD; 576 meshes converted, 10 of them skinned; runs to the end | the Xbox plays the intro cutscene first (its scripts are in the GameCube's loadables, not loaded yet), so the moments differ; the same rooms, textures and lighting once the Xbox's player is in them |
| CastleCourtyard (07000006) | the snowy courtyard, the truck, falling snow, the gun and the hands; 454 meshes, 8 skinned | no cutscene, so the frames line up: at frames 250, 1500, 3000 and 4500 the two differ by 2.0-2.6 grey levels on average, 1.3-1.8% of pixels by more than 32, mostly the snowflakes (before the skinned meshes, without the hands: 2.6-3.4) |
| SpaceStationD (0700001b) | the station's exterior, walkway and trusses, the beam weapon and the space suit's hands, a guard in his suit in a window, the HUD; 345 meshes, 8 skinned | the Xbox plays the intro cutscene first |

The skinned meshes pose and move with no stretched or stray vertices in the frames looked at; with no one playing
and no cutscenes, few characters come close enough to judge faces. The log has the scripts the GameCube keeps in its
loadables (0x0600007f, 0x06000084) as not loaded, and "EMITTER TEXTURE MISSING" once.

Before GcAnim.cpp the first run stopped in Drone_PostLoad_Init: the Xbox's FixupSkinHashcode had swapped in a "Spec"
skin that the GameCube's bundle does not have.

A time demo's rebase, as the PS2 work did, needs the cutscenes (milestone 4) before the Xbox's recordings can line up
with the GameCube build.

## Milestones

| # | Milestone | Needs | Size |
|---|---|---|---|
| 1 | The disc's files are served: names to paths, bundles decompressed | - | done |
| 2 | Bundles converted: the directory, the sequences merged, every map block, textures to KXT; checked against the Xbox's twins offline | 1 | done |
| 3 | A level's static world: block 0x0c and the GC EXTRA entries to KXE, the double-sided bit carried over | 2 | done (rigid meshes) |
| 4 | Placements and objects in play: the 078/079/07A loadables (cutscene scripts and sequences) as the PS2 branch's LoadableLoad; collision and paths confirmed with the Xbox's time demos | 3 | medium (2-3 days) |
| 5 | Sound: DSP ADPCM banks to Xbox ADPCM, the `.SFX` fields, the music re-blocked and its markers | 1 | medium (2-3 days) |
| 6 | Text, fonts, menus: text needs nothing; the GameCube's button symbols from Nightfire.elf | 2 | small (1 day) |
| 7 | Skinned characters and morphs: the skinning data at 0x90-0xa0, bone palettes and morph streams into the KXE | 3 | done; faces in game wait for the cutscenes (4) |
| 8 | Movies: an HVQM4 decoder, or the Xbox's movies in its place | 1 | medium-large (3-5 days), or skip |

## Research items

- **The GameCube's second bones under 5%:** its exporter left them out (1.6% of skinned vertices are moved by one
  bone where the Xbox's blend a second at 0.2-5%); the conversion keeps the GameCube's.
- **Mesh states the data does not obviously give:** the Xbox's alpha reference 180 (0.7% of triangles) and clamped
  textures (0.5%); perhaps the texture header's words at 0x24 and 0x28, or layer flag bit 0.
- **The invisible blocking meshes** the GameCube has geometry for and the Xbox not: whether the game hides them.
- **The `.SFX` fields that differ** after the words are turned round (2% of bytes): halfwords or bytes inside
  words, or values that changed with the stereo samples split in two.
- **The HVQM4 movies:** the audio track's format, a decoder to port.
- **`07000001.bin`** (HendersonA) is empty in the extraction; check the disc image.
- **The `*TxtU.dat` banks and `08000001/2.bin`:** what reads them on the GameCube.
- **Entity parameters and collision blocks that differ** beyond float noise (62 and 79 over all levels).

## Tools

`gcdata_check` (src/action/devtools/GcDataCheck.cpp, `cmake --build . --config Release --target gcdata_check`): the
GameCube code's conversions (the same GcBundle, GcMap, GcMesh, GcTexture and GcDisc code GcFs.cpp links) run offline
over the extracted disc, no game.

```
gcdata_check <gc disc> files                          every bundle: compressed or not, its entries by type
gcdata_check <gc disc> bundle 07000002                a bundle's directory and its packs' blocks
gcdata_check <gc disc> textures 07000002 [<out>]      every texture to a KXT, decoded back; TGAs into <out>
gcdata_check <gc disc> dt 07000002                    the "DT" headers' indices against the names they point at
gcdata_check <gc disc> meshes <xbox disc> [<hash>...]  every rigid mesh to a KXE, against the Xbox's twins
gcdata_check <gc disc> skins <xbox disc> [<hash>...]   every skinned mesh: bones, weights and morphs too
gcdata_check <gc disc> convert 07000002 <xbox disc>   the level as the Xbox's, against the Xbox's bundle
gcdata_check <gc disc> all <xbox disc>                convert, for every level, totals only
gcdata_check <gc disc> names <xbox disc>              every name the Xbox build asks for, found on both discs
gcdata_check <gc disc> xbox <xbox disc> <name> <out>  a file out of the Xbox's archives
```

`<xbox disc>` is an extracted Xbox disc (the folder with `eurocom\filesys.d00`): the checker reads its archives
itself. `GCDATA_SHOW=1` prints the first difference of each block that differs.

`tools/pe_compare.py` compares two builds of an executable section by section: the default build before and after a
GameCube seam must come out identical.
