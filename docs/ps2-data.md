# The action engine on the PS2's data

A side project: our action engine (action_standalone, docs/action-standalone.md) loading the PS2 version's files
(`SLES-51258`, the European release) in place of the Xbox's, behind a build switch. The game code stays the Xbox
port; what changes is how the files are found and how the formats that differ become what the Xbox code expects.

Status (24 October 2026): milestones 1, 2, 5 and 6 done, 3 done for rigid meshes, 4 done but for what only a person
playing can judge. The PS2 data's levels load and play on the test runner (HendersonA, CastleCourtyard,
SpaceStationD, SkyRail, the front end) with the PS2's textures, sound, music and movies, the Xbox's skinned meshes
standing in for the PS2's until milestone 7; see [State](#state) and [In game](#in-game).

Sources: the PS2 disc as extracted by the extractor project (Q:\nightfire-re, read only; its PS2 extraction in
`iso_extract/ps2_eu_sles-51258`), `ACTION.ELF`'s own symbol table (5,607 symbols with GCC-mangled signatures: every
function and most data have names), our Xbox code and the Xbox format specs on the `blender-exports` branch
(docs/level, docs/anim). Every comparison below was made file by file between the two discs.

## The switch

A CMake option, **`NIGHTFIRE_ACTION_DATA_PS2`** (default OFF), for the standalone targets only (action_standalone,
action_standalone_test). On, they are compiled with `NIGHTFIRE_DATA_PS2=1` and:

- `src/action/engine/FS.cpp` (the Xbox's `filesys.dNN` archives) is replaced by `src/action/ps2/FS_ps2.cpp`, and
  `src/action/engine/Fmv.cpp` (XMV movies) by `src/action/ps2/Fmv_ps2.cpp` (PSS movies);
- the files of `src/action/ps2/` are added;
- a handful of seams in game files are guarded with `#if NIGHTFIRE_DATA_PS2` (listed under [Seams](#seams));
- FFmpeg comes from `third_party/ffmpeg-prebuilt-ps2` (the same release, with the MPEG program stream demuxer and
  the MPEG-2 decoder added: `sh tools/fmv/build_ffmpeg.sh windows-x86-ps2`) instead of `third_party/ffmpeg-prebuilt`.

Off, nothing is compiled differently: the guarded lines vanish in the preprocessor and FS.cpp is built as before,
so the default build's code and the time demos are untouched. Checked on 24 October 2026: action_standalone,
action_standalone_test and actioninject built before and after the change have identical `.text`, `.data`,
`.reloc` and `.rsrc` sections; `.rdata` differs only in the debug directory's timestamp. (A seam must not move a
line of a file whose asserts print `__LINE__`: memory.cpp's heap size is why the PS2 build sets `HEAP_SIZE` from
CMake instead of an `#if` there.) actioninject never gets the switch (it maps the Xbox's XBE, whose original code
expects the Xbox's data).

Why compile time and not a runtime setting: the PS2 path replaces whole loaders (the archive reader) and adds
conversion at load time; a runtime switch would put a branch in every one of those game functions and make the
default build's code differ from today's. A build that reads one console's data or the other's is also what the
original had (the PS2 and Xbox builds are the same source with `#ifdef`s: the Xbox parsemap still has the PS2's
palette and texture block cases).

The PS2 build reads its disc through `DiscPath` (settings.ini) like the Xbox build, so `DiscPath` names the
extracted PS2 disc: the folder holding `FILES.BIN`, `ACTION.ELF` and `PS2\`.

```
cmake -B build/ps2data -A Win32 -DPython3_EXECUTABLE=C:/Python314/python.exe -DNIGHTFIRE_ACTION_DATA_PS2=ON
cmake --build build/ps2data --config Release --target action_standalone action_standalone_test
```

The offline checker (`ps2data_check`, below) is a separate small target and needs no switch.

## Format by format

Endianness is little on both (EE and x86). Integers and floats in the files are the same sizes; the PS2's files
have no pointers in them that the Xbox's lack (both store offsets or hashcodes and fix them up at load).

| Format | Same? | What differs | Known | Unknown | Seam in our code | Size |
|---|---|---|---|---|---|---|
| Archive / filesystem | no | Xbox: `filesys.dNN`, entries by CRC32 of the lower-cased name, EDL compression. PS2: one `FILES.BIN`, 118 files at 2 KB sectors, named by `FileList` in ACTION.ELF (16-character name, byte offset, size); sound, music and streams are loose files under `PS2\` read by the IOP | everything (psiFileOpen 0x10bf00 does `strupr` + `strcmp` over FileList, 0x76 entries) | - | FS.cpp, whole (FS_ps2.cpp) | small, done |
| Level bundle (`07xxxxxx.bin`) | nearly | Header and directory each padded to a 2 KB sector: the header is 0x800 bytes (Xbox 0x20), `dirSize` is sector-rounded. Directory entries (`u32 len, u8 type, name`) and data are as the Xbox's | PS2 LoaderLoad 0x1d51a0 reads 0x800 | - | Loader.cpp: the header's size | small, done |
| Bundle contents | split | The PS2 splits a level over variant bundles: `07F0xxxx` holds the level's anim sequences (type 4; the Xbox has them in the level's own bundle), `078/079/07A0xxxx` cutscene sequences and their script, loaded on demand (type 12 "loadable" entries name them). PS2 LoaderLoad takes a mode: 0 and 3 set bits 20-23 to 0xF (the sequences), 5 to 0xD, 4 a stored hashcode; PS2 ResetMap_Load calls `LoaderLoad(0, level)` then loops `LoaderLoad(1, level)`. The PS2 bundle also carries the pause screen's `woman` (type 16) | the variant naming; all 468 + 7 sequences of 07F00001 / 07800001 are byte for byte the Xbox's | how the PS2 streams sequences (AnimSeqGetHdrBuf, LoaderLoadAnims 0x1d72e8, LoadableLoad 0x1d7188) | game.cpp ResetMap_Load (the sequences: done), Loader.cpp LoadableLoad (the loadables) | medium |
| Skeletons, skins, anim scripts, game scripts, menus (types 6, 3, 5, 7, 8) | yes | 42 of 45 skeletons byte-identical, three differ in the float noise (under 0.008); skins, anim scripts, scripts and menus identical | - | - | none | none |
| Anim sequences (type 4) | yes | identical, only in other bundles (above) | - | - | loader only | (in the loader row) |
| Map/graphics packs (`01xxxxxx`): container, header, placements, portals, LOD, paths, collision | yes | same blocks and records: placements 0x4c + 8n with 12-byte vectors, entity params 0x34 + name, portals, LOD, AI paths, collision (version 5, the same COLLBOX 0x30) | the container walks identically; placement layout as the Xbox's | collision and AI path payloads checked by size only | parsemap.cpp as is | small |
| Textures | no | Xbox: block 0x18 `KXT` (DXT1/3/5, swizzled A8R8G8B8). PS2: blocks 0x16 (texels) then 0x12 (palette) per texture, after 0x10/0x0f as on the Xbox. 4-bit (16-colour, 64-byte palette) or 8-bit (256 colours, 1 KB palette in the GS's CSM1 order: bits 3 and 4 of the index swapped); colours RGBA with alpha 0x80 = opaque; no mip levels; animated textures are frames back to back; 0x10 flag bit 0 = 8-bit, 0x0f byte 0 = the palette's last index (0x0f or 0xff). Every size is a power of two | all of it (PS2 psiCreateMapTextures 0x11d800 reads the same fields) | - | parsemap.cpp case 0x12 (converts to a `KXT` of DXT1, DXT5 or A8R8G8B8, which psiCreateMapTextures registers as is) | small, done |
| Static meshes | no | Xbox: block 0x0d `KXE` (vertex/index buffers, strip primitives). PS2: block 0x2d, VIF packets: a box tree (0x38-byte boxes, leaves hold a VIF chain), per batch a GIF tag (NLOOP vertices, triangle strip or fan), UV (V2-32), position (V3-32), colour (V4-8 unsigned, 0x80 = 1.0) and normal + ADC flag (V4-8 signed, w = -1 skips the triangle), textures by STROW whose first word is the pack's texture index (the box's texture list holds the offsets of those STROWs; PS2 psiCreateEntityGfx 0x1173a8 patches them) and each leaf's DIRECT packet sets ALPHA_1, ZBUF_1 and TEST_1 (see [Meshes](#meshes)) | the rigid layout and its render states (above) | which meshes the Xbox draws double-sided (no PS2 field found yet); the box flags (bit 0 is not it) | parsemap.cpp case 0x2d (converts to a `KXE`, which psiCreateEntityGfx registers as is) | medium; rigid done |
| Skinned meshes | no | PS2 0x2d with a skin record (footer word 2): float array, offsets, a 0xff-terminated bone list; drawn by the VU1 microcode `SKIN_1_BONE` / `SKIN_2_BONE` / `SKIN_2_BONE2` (`.vif` data at 0x242908...) with `SkinIt` 0x10c128, `CacheSkin` 0x1114b8 | the record's shape (nightfire-re) | the vertex/bone layout | the same case 0x2d (KXE with 32-byte vertices and bone palettes) | research |
| Facial animation (morphs) | partly | The morph *sequences* are the shared type-4 files (identical). The targets: Xbox in the KXE (int16 deltas); PS2 a block 0x2b (`parsemap_block_morph_data` 0x1d2b88) | that block 0x2b exists and its handler's name | its layout; how SkinIt applies it | parsemap.cpp case 0x2b | research |
| Text banks (`UKTxt.dat`...) | yes | same layout and entry count (2,801 in UKTxt), a few strings differ (the pads' button names, by the look of the diff) | - | - | none (FileList) | none |
| Fonts | glyphs yes, button symbols no | Glyph tables are executable data on both: the PS2's `FontDataNFont2`, `FontDataSerpLight`, `FontDataMedium` are, all 149 glyphs each, our FontData.inc's Font1, Font2 and Font3, and the font textures (NFont2 512x128, SerpLight 512x64, Medium 512x128, buttons 256x128) are the same sizes. The button symbols in text (`~A`...) differ: the PS2's `specialchar` (0x002de700) has 17 entries (A B C D E F H I J L R S T V W X Y) where the Xbox's SpecialChars has 23, at other places in the PS2's `buttons` texture, and seemingly another field order (u, v, w, h, screen w, h) | everything; the record layout is the Xbox's | - | SpecialChars filled from ACTION.ELF's `specialchar` (Ps2ExecutableData.cpp) | small, done |
| Menus, UI | yes | menu files identical; sprites and HUD art come through the textures | - | the HUD's PS2 sprite coordinates (as fonts) | none | small |
| Sound banks (`SB_n.SHF/SFX/SBF`) | container yes, codec no | Same three files and the same 36-byte sample headers, but PS2 SPU ADPCM (VAG blocks, 16 bytes = 28 samples) where the Xbox has Xbox ADPCM; the header's rate word is the SPU pitch (4096 = 48 kHz: 1882 = 22,050 Hz) where the Xbox has Hz; sample counts differ (the banks were built separately: SB_0 has 6 samples on PS2, 5 on Xbox) | the codec (nightfire-re's extract_audio.py; FFmpeg's `adpcm_psx`) | the `.SFX` parameter differences (SB_0's differ from byte 593) | SFX.cpp LoadSoundBank: each sample re-encoded as Xbox ADPCM | medium, done |
| Music, streams | container yes | `MUSIC\MFX_n\*.SMF/SSD`, `ENGLISH\STREAMS\STREAMS.BIN/LUT`, `MFXINFO.MXI`, `SBINFO.SBI`: the same names; PS2 data is SPU ADPCM (music stereo, 0x80-byte interleave, 32 kHz; streams mono 22,050 Hz) | nightfire-re | - | FS_ps2.cpp: music served as the Xbox's track; the streams not (see [Sound and movies](#sound-and-movies)) | medium; music done |
| FMV | no | Xbox `eurocom\25_fps\<hash>.xmv` (WMV2 + Xbox ADPCM); PS2 `MOVIES\25_FPS\<LANG>\<hash>.PSS` (MPEG-2 program stream; the sound 16-bit PCM in private stream 1, `SShd`/`SSbd`), the same hashcode names | all | - | Fmv.cpp replaced by Fmv_ps2.cpp | small, done |
| Saves | n/a | ours are host files (psiSave.cpp); the PS2 memory card format matters only to import PS2 saves | - | the card layout | psiSave.cpp | small (optional) |
| Woman (pause background) | yes | Xbox `woman.rle` (a file); PS2 each level bundle's entry type 16 `woman`, byte for byte the Xbox's file padded with zeros | all | - | FS_ps2.cpp serves `woman.rle` from the first level bundle that has it | small, done |

## Seams

Where the PS2 build branches (`#if NIGHTFIRE_DATA_PS2`), each a line or two in the game file and the work in
`src/action/ps2/`:

| Seam | Game file | PS2 code |
|---|---|---|
| The archive (whole file replaced) | engine/FS.cpp out of the build | ps2/FS_ps2.cpp: FileList from ACTION.ELF, FILES.BIN, loose `PS2\` files, `woman.rle` from a level bundle |
| A level's sequences are in their own bundle | game.cpp ResetMap_Load, before the level's LoaderLoad loop | ps2/Ps2Loader.cpp: the 07F bundle through LoaderLoad (variant 8) |
| A bundle's header is a sector | engine/Loader.cpp | - |
| The heap: the converted textures and the decoded sound need room; textures must stay in the heap or contiguous memory, below 256 MB, which the D3D seam's addresses assume | memory.cpp's `HEAP_SIZE`, set to 64 MB by CMake (textures as A8R8G8B8 took 12-35 MB a level and wanted 88-104 MB, which left contiguous memory short on the runner: xboxInitTextures's 8 KB was refused and the game stopped at start; as DXT they take 2-4 MB) | - |
| Block 0x12 (a PS2 texture's palette, after its texels) | engine/parsemap.cpp | ps2/Ps2Texture.cpp, ps2/Ps2Convert.cpp: `KXT` DXT1 (opaque or cut-out), DXT5 (alpha between), swizzled A8R8G8B8 (under 4 texels a side), linear A8R8G8B8 (Fire2 and WomanBack, whose pixels Woman.cpp writes and reads, as on the Xbox) |
| Block 0x2d (a PS2 mesh) | engine/parsemap.cpp | ps2/Ps2Mesh.cpp, ps2/Ps2Convert.cpp: `KXE` |
| Block 0x04 after a skinned 0x2d: **interim** stand-in (below) | engine/parsemap.cpp | ps2/Ps2XboxStandIn.cpp |
| A cutscene's loadable bundle (0x0780xxxx-0x07A0xxxx) | engine/Loader.cpp: the Xbox's LoadableLoad and LoadableReload left out | ps2/Ps2Loader.cpp: the PS2's (0x1d7188, 0x1d7250) |
| The levels' own "Spec" skins (FixupSkinHashcode): the Xbox's alone, neither the skins nor the function are on the PS2 | engine/Anim.cpp AnimObjectNew | - |
| The Xbox disc for the stand-ins | engine/XboxSettings.cpp: `XboxDiscPath` (or the environment's `NIGHTFIRE_XBOX_DISC`) | - |
| A sound bank's samples (SPU ADPCM, the pitch word) | sound/SFX.cpp LoadSoundBank, after the sample headers are read | ps2/Ps2Sound.cpp, ps2/Ps2Convert.cpp: Xbox ADPCM, Hz, loop offset |
| Music tracks (`.SSD`) and their markers (`.SMF`) | none: FS_ps2.cpp's reads | ps2/FS_ps2.cpp, ps2/Ps2Sound.cpp |
| The button symbols in text | none: FS_Init reads ACTION.ELF | ps2/Ps2ExecutableData.cpp: `specialchar` into Font.cpp's SpecialChars |
| Movies (whole file replaced) | engine/Fmv.cpp out of the build | ps2/Fmv_ps2.cpp |
| A time demo rebased onto the PS2's data prints its viewer (devtools) | devtools/Demo.cpp | - |

psiCreateMapTextures and psiCreateEntityGfx stay the Xbox's: the conversions hand them the Xbox's own structures.

### Interim: the Xbox's skinned meshes

Until milestone 7 converts the PS2's skinned meshes, `src/action/ps2/Ps2XboxStandIn.cpp` takes them from the Xbox
disc's copy of the level: settings.ini's `XboxDiscPath` names the Xbox disc folder (holding `eurocom\filesys.d00`).
When block 0x2d is skinned, the entity params block after it looks the entity's hashcode up in the Xbox's bundle of
the level being loaded (read from the Xbox's archives and EDL-decompressed once a level, as FS.cpp does) and copies
that KXE into the heap. It works because skins and skeletons are byte for byte the same on both discs, and all 63
skinned meshes of 07000001, 07000006, 07000024 and 0700001b have an Xbox twin with the same hashcode in the pack of
the same name, whose texture list holds the textures the mesh uses at the same indices (checked offline), so the
Xbox's mesh draws with the PS2's textures. Without `XboxDiscPath` skinned meshes are not drawn (and a level with
drones may not load). Remove the file, its seam and the setting with milestone 7.

## Milestones

| # | Milestone | Needs | Size |
|---|---|---|---|
| 1 | The PS2 archive opens and files load: FileList, FILES.BIN, the loose sound files, the bundle header | - | small |
| 2 | PS2 textures decode into our texture path | 1 | small |
| 3 | A level's static world renders from PS2 meshes: rigid 0x2d meshes as KXE with their render states; winding and culling settled | 1, 2 | medium |
| 4 | Placements and objects: the variant bundles (07F sequences, loadables) loaded as the PS2's ResetMap_Load and LoadableLoad do; the pause screen's woman; collision and AI paths confirmed in play | 3 | medium |
| 5 | Text, fonts, menus: FileList text banks (done with 1); the PS2's button symbols in text | 2 | small |
| 6 | Sound: SPU ADPCM in the bank, stream and music decoders, the pitch word; FMV from PSS | 1 | medium |
| 7 | Skinned characters and anims: the PS2 skinned 0x2d (research), bone palettes into KXE; the sequences are shared | 3, 4 | research, then medium |
| 8 | Facial animation: block 0x2b morph targets into KXE morph streams | 7 | research |

## Research items and how to crack them

- **PS2 skinned meshes.** The 0x2d footer's third word points at a skin record (float array + length, an
  offset list ending 0xffffffff, a bone list ending 0xff). Read `SkinIt__FP13_PS2GLISTINFOPiT1iP8_VECTOR3i`
  (0x10c128), `CacheSkin` (0x1114b8), `psiDrawSkinObjectMatrix` (0x111a20), `FillMatrixChainSkin` (0x122df8) and
  the VU1 programs `SKIN_1_BONE`, `SKIN_2_BONE`, `SKIN_2_BONE2` (VIF MPG data at 0x242908-0x244928, symbols
  `.vif.N` and `_$SKIN_*`). The ELF's symbols give every name and argument type; capstone (MIPS32, little
  endian; the R5900's MMI and VU0 macro instructions come out as `.word`) disassembles it offline, and Ghidra has
  the ELF as `/PS2_EU_51258/ACTION.ELF`. The Xbox side says what it must become: the same skin's KXE mesh (32-byte
  vertices, two bones and a weight, 0x36-byte palettes; spec-mesh.md section 7) - the skins (type 3) are
  identical, so each PS2 skinned mesh has an Xbox twin with the same bones to compare vertex by vertex.
- **Facial morphs.** Block 0x2b and `parsemap_block_morph_data` (0x1d2b88), `AnimFacialUpdate`/`AnimFacialBlend`
  (0x1bea38, 0x1bed70), `Morph_Weight`/`Morph_Index`/`Morph_Count`. Compare with the 61 Xbox morphing meshes (22
  targets each, int16 deltas).
- **Double-sided meshes.** The Xbox marks 30-40% of meshes double-sided somewhere (primitive state bit 3); no PS2
  field found yet says which (box flag bit 0 does not: `ps2data_check meshes` prints the comparison). Perhaps the
  VU1 program (the MSCAL/MSCNT address), the GIF tag's PRIM bits, or the GS culls nothing and the Xbox port's
  exporter decided. Until then every converted primitive is double-sided.
- **Sequence streaming.** If the PS2 loads only sequence headers and streams the data (AnimSeqGetHdrBuf), we need
  not: loading the 07F bundle whole, as the Xbox loads its type-4 entries, is enough, since the files are identical.

## Meshes

How a PS2 rigid mesh becomes a KXE (src/action/ps2/Ps2Mesh.cpp), and what the checks found:

- **Vertices** are copied per batch (28-byte rigid vertices): position, ST as UV, colour with RGB as is (the GS's
  modulate takes 0x80 as 1.0, the Xbox's MODULATE2X takes about 0x80 as 1.0 too) and alpha doubled (0x80 opaque
  becomes 0xff).
- **Normals point the other way.** At the same place on the Xbox twin of each named entity, 90% of the PS2's
  normals point opposite the Xbox's (07000001: 65,499 of 70,938 once turned round; the rest are mostly effect
  quads, glass and shadows, whose sides the two exports chose differently). They are negated.
- **Winding.** A strip run's triangles wind alike, the first even, as D3D's do; but the GS culls nothing and 7% of
  the runs wind against their own normals. Each run (and each fan triangle) is emitted the Xbox's way round by its
  triangles' vote against the normals: afterwards 92% of the triangles agree with the (Xbox-signed) normals, 99.5%
  on the Xbox twins. Runs are joined with degenerate triangles; one primitive per texture and state.
- **Render states** come from each leaf's DIRECT packet: ALPHA_1 0x44 (Cs - Cd) * As + Cd is the Xbox's blend 0,
  0x48 additive (1), 0x42 subtractive (2); ZBUF_1's ZMSK clears the Xbox's z-write; TEST_1's AREF 0x7f (against 1)
  is the Xbox's alpha reference 180. Over the twins of 07000001 and 07000024 the states each mesh uses agree with
  the Xbox's in 869 of 870 meshes for every state. Double-sided: see the research items.
- **Matching the Xbox.** 401 of the 405 named twins in 07000001 (469 of 479 in 07000024) have the same bounds to
  5 cm; the rest are names several entities share (the weapons' Barrel, Grip...). No PS2 mesh with nothing to draw
  (no box list) has a twin that draws.
- Over all 99 bundles: 15,908 meshes, 13,888 converted, 421 skinned (left out), every VIF chain read to its end,
  no batch whose counts disagree with its GIF tag, strips only (no fans in the data).

## In game

On the test runner (vr-desktop, `C:\nightfire-ps2\run\Release`: `DiscPath` the extracted PS2 disc, `XboxDiscPath`
the Xbox disc, `DumpEvery=250`), `action_standalone.exe -level <level>`, no input, 100-120 s:

| Level | What the frames show |
|---|---|
| HendersonA | the intro cutscene (the loadable 0x07800001: the characters in the PS2's room), then play: the Japanese house's room, furniture, lamps and garden from the PS2's meshes and textures, the Xbox's drones attacking, the first-person gun and hands, the HUD; with nobody playing, "Mission Failed" |
| CastleCourtyard | the snowy courtyard, the truck, falling snow, the gun |
| SpaceStationD | the intro (Bond in the space suit), then the station's exterior and walkway, the beam weapon, the HUD |
| SkyRail (`-level 07000024`, a multiplayer map) | the snow map with the multiplayer HUD and timer; with no match settings the match ends at once, then the debriefing and the front end |
| the front end (no `-level`, or an unknown one) | the attract movies (PSS), then the PS2's title screen |

With milestone 6 the levels' intro movies play from the PSS files (HendersonA's, frame 500 of a run), sound effects
are the PS2's banks and the music the PS2's tracks, and the front end plays each attract movie once (four in 100 s)
where it had retried them thousands of times while none could open. The 35 "SPRITE HASHCODE ... NOT FOUND" lines in a
HendersonA run are the Xbox build's own: the same hashcodes, in neither disc's HendersonA bundle (they are in other
levels'), which the HUD asks for anyway.

## Sound and movies

- **Sound banks.** LoadSoundBank reads the PS2's three files as it does the Xbox's; after the sample headers,
  `Ps2_ConvertSoundBank` decodes each sample's SPU ADPCM (16-byte blocks of 28 samples, the five filters, up to the
  block with the end flag; the same samples as FFmpeg's `adpcm_psx` within rounding), encodes Xbox IMA ADPCM (36-byte
  blocks of 64 samples per channel, each code the best of 16 with a one-sample look-ahead) into new memory and frees
  the SPU data. The header's rate word becomes Hz (pitch x 48,000 / 4,096, put on the nearest standard rate:
  1882 = 22,050 Hz), and the block with the SPU's loop-start flag becomes the loop offset (the Xbox ADPCM block it
  falls in). IMA ADPCM is coarser than the SPU's: 15-24 dB signal to noise against the PS2's decoded samples
  (`ps2data_check sound`), audible as a little hiss on quiet sounds; the decoder was checked against the reference.
- **Music.** The PS2's tracks are stereo SPU ADPCM, interleaved in 0x80-byte blocks, at 32 kHz; the Xbox's music
  code reads two channels of Xbox ADPCM at 44.1 kHz from wherever it likes in the file. FS_ps2.cpp therefore serves
  a `.SSD` as the Xbox's track would be: its size is the Xbox's equivalent (track 1: 14,582,948 bytes against the
  Xbox's 14,583,808), and each read decodes the PS2 blocks it needs (eight blocks before it to settle the
  predictors), resamples them and encodes the Xbox blocks asked for. The `.SMF` marker files' byte positions are
  moved by the same ratio as they load. `ps2data_check music` checks that tracks read in pieces match tracks read whole.
- **Voice streams** (`ENGLISH\STREAMS\STREAMS.BIN`) are not converted: the
  Xbox's disc has no `STREAMS.LUT` and its stream code one channel (`SFX_NUM_STREAMS`), so how the PS2's index
  maps onto the Xbox's stream calls is still to be worked out.
- **Movies.** Fmv_ps2.cpp keeps Fmv.cpp's drawing and sound path and finds `MOVIES\25_FPS\<language>\<name>.PSS`
  (the sound language, else English). FFmpeg (the PS2 build's DLLs, above) decodes the MPEG-2 video at 512x512,
  scaled to the Xbox movies' 640x480. The sound is not one FFmpeg reads: private stream 1 carries a 4-byte prefix,
  then an `SShd` header (format 1 = 16-bit PCM, 48 kHz, two channels, 0x200-byte interleave) and `SSbd` data, so
  Fmv_ps2.cpp reads it from the file itself and gives it to the movie sound as frames.

## Multiplayer maps by name

`-level` knows the single-player levels' names (LaunchOptions). The multiplayer maps (SkyRail and the rest) are
left out on purpose: a map started with `-level` has no match settings, so its match ends at once, and adding the
names would change the default build. `-level 07000024` (the bundle's hashcode) starts one for a look.

## Tools

`ps2data_check` (src/action/devtools/Ps2DataCheck.cpp, `cmake --build . --config Release --target ps2data_check`):
the PS2 code's conversions (the same Ps2Disc, Ps2Texture and Ps2Mesh code the game links) run offline over the
extracted disc, no game.

```
Release\ps2data_check.exe <disc> files                                    the FileList and every file's size
Release\ps2data_check.exe <disc> bundle 07000001                          a bundle's directory, block counts
Release\ps2data_check.exe <disc> textures 07000001 [<out>]                every texture to a KXT; frame 0 as PNG
Release\ps2data_check.exe <disc> meshes 07000001 [<xbox bundle>] [--obj <out>]
                                                                          every mesh to a KXE, checked; with the
                                                                          Xbox's decompressed bundle, against the twins
Release\ps2data_check.exe <disc> all                                      every level bundle's textures and meshes
Release\ps2data_check.exe <disc> woman [<xbox woman.rle>]                 the pause screen's film, as FS_ps2 finds it
Release\ps2data_check.exe <disc> sound 0 [<out>]                          a sound bank to Xbox ADPCM, the signal to
                                                                          noise of each sample; WAVs of both
Release\ps2data_check.exe <disc> music 1 [<wav>]                          a track as the Xbox's, read in pieces and whole
```

`textures` also prints the DXT formats chosen and the mean and worst PSNR of the DXT texels against the PS2's.

`tools/pe_compare.py` compares two builds of an executable section by section: the default build before and after a
PS2 seam must come out identical.

The Xbox's decompressed bundles are what `tools/anim/extract_all.py` (blender-exports branch) writes to
`build/anim/lvl/`.

## State

| # | State |
|---|---|
| 1 | done: FS_ps2.cpp (FileList from ACTION.ELF's symbol table, FILES.BIN, loose PS2\ files), the bundle header. Checked offline and in game |
| 2 | done: block 0x12 to a KXT of DXT1 or DXT5 (A8R8G8B8 where it must be). All 25,082 textures of the 99 bundles convert (one 64x64 header with 32x32 texels is decoded at 32x32); 07000001's take 2.8 MB at a mean PSNR of 38 dB; right in game |
| 3 | rigid meshes convert to KXE with their render states; checked against the Xbox twins offline, and in game the static worlds draw right ([In game](#in-game)); every converted primitive double-sided |
| 4 | mostly done: the 07F sequence bundle loads before the level (PS2 ResetMap_Load's `LoaderLoad(0, level)`); the cutscenes' loadable bundles through the PS2's LoadableLoad and LoadableReload (HendersonA's and SpaceStationD's intros play); `woman.rle` from a level bundle; no Xbox "Spec" skins; skinned meshes from the Xbox disc (interim). Drones, cutscenes and the HUD work in the levels tried. The loadables and the sequences' reload go into the memory the level's sequence bundle took, as the PS2's LoaderLoad reads modes 2 and 3 into LoaderAnimBase: every loadable on the disc is smaller than its level's 07F bundle, so a level's cutscenes take no more memory (a bundle that did not fit would take new memory and give the old back). Collision, AI paths and objects in play: see [Time demos](#time-demos) |
| 5 | done: text banks load through FileList (done with 1); the fonts need nothing (glyph tables and texture sizes identical); SpecialChars takes the PS2's 17 symbols from ACTION.ELF's `specialchar` (the same 0x18-byte records, coordinates in the PS2's `buttons` texture), its other slots empty, so prompts show the PS2's pad |
| 6 | done but the voice streams: sound banks and music from SPU ADPCM (above), movies from PSS through FFmpeg |
| 7-8 | not started |
