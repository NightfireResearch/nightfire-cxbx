# The driving engine on the PS2's data (scoping)

A scoping study for a sibling of [ps2-data.md](ps2-data.md): our driving engine (driving_standalone,
docs/driving/standalone.md) loading the PS2 release's driving files (`SLES-51258`, the European release) in place
of the Xbox's, behind a build switch, with the game code staying the Xbox port. The question it answers: does the
driving engine take the PS2's files as readily as the action engine did, or are there further challenges?

Status (26 October 2026): **scoping only**. Nothing in the game builds is changed. The offline checker
`tools/ps2driving_check.py` (below) reads both discs and produced every count in this document.

**Short answer: harder than the action engine's PS2 work, though most of the data is easier.** The containers,
the streams, the animation, the skeletons and the level data (CARP) are the Xbox's, byte for byte or nearly. The
work is concentrated in one place, and that place is harder than anything the action engine needed:

- the PS2's models carry PS2 geometry (DMA chains of VIF packets for the VU1) and name PS2 render methods. They
  have none of the Xbox objects our renderer draws: no `VertexBuffer`s (the Xbox has 37,019) and no per-primitive
  `RenderMethod`s (7,391);
- the Xbox's render methods (vertex shader microcode, register-combiner set-ups and packet programs) are **disc
  data**, in `misc.viv`'s `eaglrm.o` and `bondrm.o`, not code in the XBE. The action engine's PS2 build could hand
  converted meshes to shaders that were already in our code. A driving build that reads nothing from the Xbox disc
  has no render methods to hand them to: we would have to write our own.

Sources: the PS2 disc as extracted by the extractor project (Q:\nightfire-re, read only:
`iso_extract/ps2_eu_sles-51258`, whose BIGF and RefPack readers, `common/extraction/extract_bigf.py`, the checker
follows); the Xbox disc in `disc/driving` (European, the twin of the PS2's release); `DRIVING.ELF` and its
symbol file `DRIVING\DRIVING.SYM`; Ghidra's `/PS2_EU_51258/DRIVING.ELF`; our driving code and its docs
(docs/driving/eagl.md, filesys.md, core-data.md, sound.md). The extractor has no PS2 driving-format readers beyond
BIGF and RefPack; its driving handler is a stub (`nightfire_platform.py`).

## The proposed switch

A CMake option **`NIGHTFIRE_DRIVING_DATA_PS2`** (default OFF), for driving_standalone (and its test target), on
the model of `NIGHTFIRE_ACTION_DATA_PS2`: on, the target is compiled with a define of its own, the files of a new
`src/driving/ps2/` are added, and a handful of seams in game files are guarded with `#if`. Off, nothing compiles
differently, checked as the action build was (`tools/pe_compare.py`: the same `.text`, `.data`, `.reloc`, `.rsrc`).
drivinginject never gets it.

Both engines would be switched together in a PS2 run folder: the PS2 action build hands over to a PS2 driving
build through `psiLaunch.bin` exactly as today, since both sides of the hand-over are our Xbox code (the launch
page's layout is ours on both ends; the PS2's own action-to-driving hand-over never comes into it). `DiscPath`
names the extracted PS2 disc, whose `DRIVING\` folder sits where the Xbox's `driving\` does.

## The disc

| PS2 (`SLES-51258`) | Xbox (`disc/driving`) | Notes |
|---|---|---|
| `DRIVING.ELF` (2.9 MB, no symbol table) | `Driving.xbe` | the executable |
| `DRIVING\DRIVING.SYM` (586 KB) | - | 12,719 symbols, demangled C++ names (cut at 63 characters), object file names (`d:\ToBurn\BondPS2\PS2_EE_Release\*.obj`, `EAGLPS2\lib.ps2\libeagla.a(...)`) - but of **another link**: its addresses do not line up with the shipped ELF (no constant offset; `_end` 0x55cf04 against the ELF's 0x4279b4; 220 of its 6,322 `.text` function symbols land on a prologue). Ghidra's `/PS2_EU_51258/DRIVING.ELF` already carries matched names (about 82% of 10,278 functions named, 1,750-2,000 `FUN_`) |
| `DRIVING\MISC.VIV`, `RACE.VIV`, `MIS01.VIV` ... `MIS13C.VIV` (8 mission archives) | the same names, lower case | BIGF archives |
| `DRIVING\MISxxEN.SPE`, `MISxxFR.SPE` | `misxx{en,fr,ge,sp}.spe` | speech; the PS2 has two languages, the Xbox four |
| `DRIVING\MISxx.MUS` | `misxx.mus` | music |
| - | `bond.dat`, `debug.xfn` | |

Every archive the PS2 has, the Xbox has; inside them the PS2 has 1,737 entries and the Xbox 1,862. Paired by name
(with the PS2's extensions `.ssh`, `.sfn`, `.mpc` for the Xbox's `.xsh`, `.xfn`, `.mad`):

| ext | identical | same size | differ | PS2 only | Xbox only | what |
|---|---|---|---|---|---|---|
| `.asf` | 0 | 268 | 170 | 0 | 0 | music and speech streams: identical apart from the header (below) |
| `.ssh` / `.xsh` | 0 | 0 | 218 | 0 | 32 | textures (SHAPE containers) |
| `.dat` | 8 | 0 | 162 | 0 | 42 | EAGL objects: models and animation banks; `decal.dat` etc. identical |
| `.rel` | 0 | 0 | 162 | 0 | 42 | the second half of a PS2 `.dat` (below) |
| `.atr` | 155 | 0 | 0 | 0 | 4 | attributes: all identical |
| `.def` | 9 | 0 | 91 | 0 | 0 | controller configurations: the PS2's button names |
| `.crp` | 0 | 1 | 88 | 0 | 4 | CARP level and object data: records identical, the embedded EAGL object not |
| `.tun` | 51 | 0 | 24 | 0 | 0 | tuning: render tuning differs |
| `.o` | 40 | 32 | 2 | 0 | 0 | skeletons (same but padding), `eaglrm.o` and `bondrm.o` (different) |
| `.h` | 56 | 0 | 0 | 0 | 0 | sound bank indices |
| `.bnk` | 0 | 0 | 56 | 0 | 0 | sound banks: another codec |
| `.mpc` / `.mad` | 0 | 0 | 48 | 0 | 0 | movies: MPEG-2 against EA MAD |
| `.ini` | 23 | 6 | 2 | 0 | 0 | audio and loading-screen settings: a few values |
| `.gal` | 0 | 0 | 12 | 0 | 0 | front-end galleries (data groups with textures) |
| `.sfn` / `.xfn` | 0 | 0 | 11 | 0 | 0 | fonts |
| `.sub`, `.loc`, `.dir`, `.bin`, `.txt` | 40 | 2 | 1 | 0 | 1 | subtitles, string tables, the attribute directory, the anim lookup: identical or text changes |

(Counted over the archives both discs have; the Xbox's German and Spanish `.spe` files are extra.) The Xbox-only
entries are pedestrian models and textures (`p_mjack1.dat`, `b_mjacka.xsh`...: 24 models in `mis01.viv`, 9 each
in `mis4.viv` and `race.viv`), four cars in
`mis01.viv` (`coupe`, `sedan`, `station_wagon`, `taxi`, which `misc.viv` also has), `misc.viv`'s `test1.sub`. **The PS2's mission archives have no pedestrian models at all**, though
`DRIVING.ELF` has the same 22 pedestrian descriptions as the Xbox (`0.p_mjack.b_mjacka.100`..., our
AIPedestrian.cpp's `kPedDescriptions`): how the PS2 does without them is a research item.

Endianness is not an issue: BIGF's directory is big-endian on both, everything else little-endian on both (EE and
x86); RefPack is the same.

## Format by format

| Format | Same? | What differs | Known | Unknown | Seam in our code | Size |
|---|---|---|---|---|---|---|
| Archives (BIGF `.viv`, `.mus`, `.spe`) | yes | the PS2's names are upper case (a case-sensitive host needs folding); entries at 2 KB alignment on both; RefPack the same | all | - | none on Windows; FileSys.cpp's open on Linux/macOS | none |
| File names | no | the game asks for `.xsh`, `.xfn` and `.mad` (baked into the executable: `data\track\%s_S.xsh`, `common.xsh`, `%s\paris_intro.mad`...); the PS2's executable asks for `.ssh` and `.sfn`, and for `.mad` too, but the disc has `.mpc` | all | - | UFileLoader / FileSys open: the three extensions mapped | small |
| CARP (`.crp`): records | nearly | over six tracks and a car (`carp` below): the instances, collision, AI, triggers, paths and the like are the Xbox's to the byte or the same size with float noise (collision articles `ca`: 3.7% of words differ; `as` 18%, by a few units in the last place); a few record types differ in count (`cd`, `wn`, `si`) | the layout (core-data.md, UGroup) | what `wn`, `cd` and `si` are | none expected (lockstep against the Xbox's recordings will not hold: the collision differs) | small |
| CARP: the embedded EAGL object (`ELFd`/`ELFr`) | no | the models' geometry and materials: see EAGL objects | as below | as below | as below | (models row) |
| EAGL objects: container | yes | MIPS ELF on both, the same sections; a PS2 `.dat` stops before the section table and its `.rel` holds the rest (`.dat` + `.rel` = one file, which is what the Xbox's `.dat` is; the Xbox's `.rel` is a copy of its `.dat`); the loader's split-object form (`0x000e6330`, DynamicLoader +0x18) is made for the PS2's | all | whether our loader copes with the section table itself lying in the second half (no Xbox data ever took that path) | none, or the two halves joined before the loader | none-small |
| Skeletons (`*skel.o`), anim banks (`anims\*.dat`) | yes | 161 objects, 6.2 MB: 8,142 bytes differ, in padding by the three looked at (exporter garbage between records) | - | - | none | none |
| Models: records | yes | `Model` records (0xd8) are the Xbox's field for field (bounds, scale/offset, flags, the draw list's shape); 12,960 models on the PS2, 12,981 on the Xbox (the pedestrians) | layout | - | none | none |
| Models: geometry | **no** | Xbox: per primitive a `RenderMethod` child of a `ParentRM_*` and `VertexBuffer`s (D3D vertex data). PS2: a `GeoPrim` is a render-method record and a **DMA chain of VIF packets** (a RET tag, then STCYCL/STMOD/STMASK/STROW, UNPACKs of V4-32 headers, V3-16/V3-8 positions, V2-16 texture coordinates, V4-8 colours/normals, MSCAL/MSCNT to the VU1), the same family as the action engine's 0x2d meshes, with per-model scale/offset for the packed formats (`*Packed` methods). Textures by `EAGL::TAR` (5,822 on the PS2, 3,085 on the Xbox) | the outer layout; the VIF codes | the chains past the first packet (REF/CALL into data blocks), each render method's parameter block and VU memory layout | a converter from PS2 GeoPrims to the Xbox's GeoPrim + RenderMethod child + VertexBuffer, before RunConstructors (eagl/Loader.cpp), or a PS2 draw path (see Render methods) | **large (research first)** |
| Render methods | **no** | Xbox: `eaglrm.o` (`ParentRM_*`) and `bondrm.o` (54 `RenderMethod`s) are NV2A vertex-shader microcode, combiner set-ups and packet programs, loaded from the disc; the models reference 26 of them. PS2: the same files hold VU1 microcode (`*0__EAGLMicroCode`) and the methods' data under plain names (222 names defined in `eaglrm.o` and `bondrm.o` together), no `RenderMethod` objects; the models reference 26 others. The two sets do not pair one for one (Xbox `BondRoad`, `BondRoadReflect`, `BondSpecularBumpRoad`, `BondCarSpecularBump`...; PS2 `MatCarDash`, `MatBondScrollTexturePacked`, `MatBondTexture` beside `MatBondTexturePacked`...) | both name lists | the PS2 methods' behaviour (VU1 programs) | a render-method library of our own (Xbox-format records with shaders we write, through the NV2A translation in src/common/gfx), or a PS2 draw path beside the interpreter | **large** |
| Materials (GeoPrimState) | mostly | of the PS2's 549 GeoPrimState references, 472 are `STATE::...` names as on the Xbox (560 there) and 77 (in the actors' models) are built from text, `RUNTIME_ALLOC::UID=2745;SetAlphaBlendMode=EAGL::ABM_BLEND;...`, with six properties (`UID`, `SetAlphaBlendMode`, `SetAlphaTestMethod`, `SetCullEnable`, `SetTextureEnable`, `SetPrimitiveType`): the path our RuntimeAlloc.cpp has ported but the Xbox's data never reaches (eagl.md 3.1) | the property names | whether the parser takes `UID` | eagl/RuntimeAlloc.cpp: from provisional to tested | small |
| Textures (`SHPS` / `SHPX`) | container yes, pixels no | the same directory and names; PS2 image types 1 (4-bit, 112), 2 (8-bit, 1,601), 4 (2), 5 (32-bit, 321); Xbox 0x60/0x61 (DXT), 0x7b (8-bit), 0x7d (32-bit). Of 2,028 images in both: 1,836 the same size, 71 with width and height swapped, 121 another size (the PS2's often smaller); 356 Xbox-only images (specular maps the Xbox's car materials use, effect textures) | the types (realgraph's) | whether the PS2's 8-bit palettes are in the GS's CSM1 order with alpha 0x80 = opaque, as the action engine's were; the swapped images' texture coordinates | Realgraph.cpp SHAPE load (convert to 0x7d or DXT) | small-medium |
| Fonts (`FNTS` / `FNTX`) | header yes | the glyph tables match; the font image is a SHAPE | header | - | through the texture conversion | small |
| Galleries (`.gal`) | records yes | front-end data groups: the records the same size; `tx` items are SHAPE containers | layout | - | through the texture conversion | small |
| String tables (`.loc`), subtitles (`.sub`) | yes | two tables differ in text | - | - | none | none |
| Attributes (`.atr`), anim lookup | yes | identical | - | - | none | none |
| Tuning (`.tun`) | text, keys differ | `Render\CarRender\default.tun`: 1,203 keys on the PS2, 989 on the Xbox; 261 PS2-only (`Desat`, `Skyblend`, `[Dash]Ambient{...}`, `Refl*`), 51 Xbox-only (`SpherifyNormals{...}`); the lighting and fog files have the same keys with other values | - | what the Xbox code does with keys it does not find | none expected (DAFI's defaults), check in game | small |
| Controls (`.def`) | text, names differ | the PS2's button names (`ButtonCross`, `ButtonSquare`, `ButtonL1`...) where the Xbox has `ButtonA`, `ButtonRTrigger`...; other bindings and thresholds (`>  0.1`) | all | - | the input config's button-name table (InputConfigManager) | small |
| Streams (`.asf` in `.mus`, `.spe`) | **yes, but the header** | 438 streams, 138,750 blocks, 136,263 identical (the 438 headers and 2,049 data blocks are not; three streams differ in block count); the `SCHl` header differs in the platform word (5 against 7), an explicit `0x80` = 2 (the default), and the render mode `0x8c`: 4 on the PS2, 0x24 on the Xbox | codec 0x0a (EA-XA) on both | why three streams differ in block count | the stream header's render mode, which SNDCTRLI_getfxbus and the platform driver test (snd/Streams.cpp) | small |
| Sound banks (`.bnk`) | container yes, codec no | BNKl v5 on both, the same 1,379 samples in the same order; the Xbox's are all sample rep 20 (Xbox ADPCM); 1,130 of the PS2's carry no rep (the platform default: SPU ADPCM by the sizes, e.g. 3,584 samples = 128 blocks of 28) and 249 rep 10 (EA-XA); the PS2's carry `0x80` = 2, no `0x8c` but on the EA-XA ones | the codecs (the action engine's SPU ADPCM decoder; our EA-XA) | the PS2's default rep's number | snd/Banks.cpp's load: each sample converted (as SFX.cpp's LoadSoundBank does on the action side), the bank rebuilt | medium |
| Movies (`.mpc` / `.mad`) | no | Xbox `MADk/MADm/MADe` (EA MAD) and EA-XA sound; PS2 `MPCh` chunks of MPEG-2 video (the first starts with a sequence header) with the same `SCHl/SCDl` EA-XA sound (stereo on the PS2, six channels on the Xbox); same names but the extension | all | - | engine/PlayMPC.cpp: the `MPCh` chunks to FFmpeg's mpeg2video (in the PS2 prebuilt already) | small |
| Executable data | to check | the pedestrian descriptions are in both executables; the shapes and file names the code asks for by name are the Xbox's (`blood.xsh`, `ext.xsh`, effect images) | - | which named images and files the PS2 data lacks and what the Xbox code does then | per case | small-medium |

## Seams

| Seam | Game file | PS2 code |
|---|---|---|
| The extensions `.xsh`, `.xfn`, `.mad` | platform/FileSys.cpp or engine/UFileLoader.cpp, one place | the mapping |
| A SHAPE container's images | eagl/Realgraph.cpp (SHAPE load) or engine/UFileLoader.cpp's shape load | ps2/Ps2Shape.cpp: SHPS to SHPX |
| A PS2 model's GeoPrims | eagl/Loader.cpp, before RunConstructors | ps2/Ps2Model.cpp: GeoPrims to the Xbox's records |
| The render methods the PS2's models name | eagl/Loader.cpp's resolver (the symbol lookups) | ps2/Ps2RenderMethods.cpp: our library |
| Bank samples | sound/snd/Banks.cpp | ps2/Ps2Sound.cpp |
| A stream header's render mode | sound/snd/Streams.cpp | - |
| `MPCh` movies | engine/PlayMPC.cpp | - |
| Controller button names | the input configuration's name table | - |
| Pedestrians the data lacks | game/AIPedestrian.cpp (to find) | - |

## Milestones

| # | Milestone | Needs | Size |
|---|---|---|---|
| 1 | The switch, the extension mapping, case folding; the offline checker (done: `tools/ps2driving_check.py`) | - | small |
| 2 | Text: controls' button names, tuning keys checked | 1 | small |
| 3 | Sound: stream headers (the render mode), bank samples converted | 1 | medium |
| 4 | Movies from `MPCh` | 1 | small |
| 5 | Textures: SHPS to SHPX (palettes, the swapped images), fonts, galleries; the front end and HUD drawn | 1 | small-medium |
| 6 | Research: the PS2 GeoPrim (the whole DMA chain, each render method's parameter block, the VU memory layout) from DRIVING.ELF's `RenderMethod::Draw`/`Compile` and `bondrm.o`'s templates; checked against the Xbox twins offline (the Xbox's `VertexBuffer`s and render-method children, read by the checker only) | 1 | research |
| 7 | A render-method library of our own for the 26 methods the PS2's models name (and the game's own GeoPrims: effects, sky, HUD), behaving as the Xbox's do | 6 | large |
| 8 | Static models (tracks, cars, props) converted and drawn | 6, 7 | medium-large |
| 9 | Skinned characters (`BondLitTextureAndShadow_Skin`, `_Specular_Skin`): bone palettes; the skeletons and anims are already the same | 8 | medium |
| 10 | In game: every mission on the runner; what the PS2 data lacks (pedestrians, Xbox-only effect images) | 2-9 | medium |

Milestones 1-5 are of the action engine's size or smaller and could be done in a few sessions; they would give
sound, movies, menus and the loading screens, but no world. Milestones 6-9 are the bulk.

## How it compares with the action engine's PS2 work

| | Action engine | Driving engine |
|---|---|---|
| Symbols | ACTION.ELF's own symbol table, 5,607 names on the shipped addresses | DRIVING.SYM is of another link; Ghidra's matched names cover about 82% of functions (no `RenderMethod` function found by name) |
| Containers | a new filesystem (FileList, FILES.BIN), bundle headers padded | the same BIGF archives |
| Level data | mostly the same records | the same records (CARP) |
| Animation | identical sequences, re-bundled | identical banks and skeletons, in the same files |
| Sound | SPU ADPCM everywhere (banks, music, streams) | streams and music identical but a header word; only the banks to convert |
| Movies | PSS to replace the XMV player | MPEG-2 in the EA container the player already parses |
| Textures | palettes into KXT | palettes into SHPX (the same work) |
| Meshes | VIF packets into KXE, drawn by shaders that are in our code | VIF packets into GeoPrims, VertexBuffers and render-method children, **drawn by render methods that are Xbox disc data**, which a PS2 build must replace with its own |
| Materials | from the DIRECT packets' GS registers | the Xbox's `STATE::` names, and 77 built from text (`RUNTIME_ALLOC`), a path our port has but nothing exercised |
| Data in the executable | button symbols (`specialchar`) | the names it asks for; nothing like a table to lift found yet |

The structurally harder parts:

1. **The render methods are data.** On the Xbox the EAGL interpreter runs packet programs and NV2A shaders that
   come from `eaglrm.o`/`bondrm.o` and from per-primitive children in every model; our D3D seam translates the
   NV2A microcode. The PS2's files carry VU1 programs instead. Without the Xbox disc the PS2 build needs its own
   implementation of every render method its data uses - either Xbox-format records with shaders we author
   (exercising the same interpreter and NV2A translation), or a separate draw path for PS2 GeoPrims. Reading the
   Xbox's render methods from an Xbox disc would make it a hybrid that needs both discs, which the action engine's
   PS2 build was careful not to be.
2. **Geometry is per render method.** Each PS2 render method has its own VU1 memory layout (a skinned one, a
   reflective one, a packed-position one), so the VIF decoder is one per family, not one for all, and the Xbox
   exporter's choices (strips, vertex formats, which parent) have to be learnt from the twins.
3. **Lockstep against the Xbox's recordings will not hold**: the collision articles differ in the low bits, so
   the driving lockstep baselines and demos can only check a PS2 build against itself.

Not harder than it looked: streaming (the same stream files and blocks), endianness (none), the hand-over between
the engines (ours on both sides), the archive layer (unchanged).

## Research items

- **PS2 GeoPrims.** A first census of each GeoPrim's first packet (a scratch script, not yet in the checker; 2,211 GeoPrims in
  `paris_mis01.crp`, `vanquish.crp` and `c_bondc1.dat`: V4-8 7,009, V4-32 3,107, V2-16 2,037, V3-16 1,719, V3-8 996,
  V4-16 724 unpacks, 2,208 MSCAL/MSCNT); the rest of each chain and the render-method record (`GeoPrim+0` -> a
  record whose +8 names the method) are next. Ghidra's DRIVING.ELF has the PS2 EAGL code (DRIVING.SYM names
  `EAGL::RenderMethod::Draw(EAGL::GeoPrim *)`, `Compile`, `Optimize`, `Release`, and `paulvif.o`, `dmamgr.o`,
  `singledraw.o` in libeagla.a), to be matched by hand since the symbol file is of another link.
- **Which render methods the game itself names.** The game builds GeoPrims of its own for effects, the sky,
  decals and the HUD against render methods by name (eagl.md 2.8); the Xbox code names Xbox methods. Each needs
  an implementation in the PS2 build whatever the models use.
- **The pedestrians.** The PS2's mission archives have none of the 22 pedestrian models both executables list.
  Find what the PS2's AICharacterPedestrian/PedestrianSet do when the file is missing, and do the same.
- **Xbox-only images and keys.** 356 images the Xbox has and the PS2 lacks (car specular maps, effect images such
  as `common.ssh`'s), the `SpherifyNormals` keys: which the Xbox code asks for by name, and what happens then.
- **The swapped textures.** 71 images have width and height swapped against the Xbox's; check the PS2 models' UVs
  are swapped to match, so that the conversion keeps the PS2's image as it is.
- **The PS2 bank default codec**: confirm SPU ADPCM for the samples with no rep (decode one and compare with the
  Xbox's sample as PCM).

## Tools

`tools/ps2driving_check.py` (Python, no game, reads both discs, writes nothing):

```
python tools/ps2driving_check.py compare <PS2 DRIVING dir> <Xbox driving dir> [--by-file]   every entry by name
python tools/ps2driving_check.py list <archive.viv>                                         one archive's directory
python tools/ps2driving_check.py headers <PS2 dir> <Xbox dir> .crp                          first bytes of both twins
python tools/ps2driving_check.py carp <PS2 dir> <Xbox dir> mis01.viv data\track\paris_mis01.crp ...
                                                                                            CARP/gallery records
python tools/ps2driving_check.py objects <PS2 dir> <Xbox dir>                               EAGL classes, render methods
python tools/ps2driving_check.py shapes <PS2 dir> <Xbox dir>                                texture types and sizes
python tools/ps2driving_check.py banks <PS2 dir> <Xbox dir>                                 sound bank samples
python tools/ps2driving_check.py streams <PS2 dir> <Xbox dir>                               .asf blocks and headers
python tools/ps2driving_check.py syms <DRIVING.SYM> [regex]                                 the PS2 symbol file
```

`compare` takes about four minutes over both discs (Python RefPack); the others less.
