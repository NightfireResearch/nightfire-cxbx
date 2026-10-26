# The driving engine on the GameCube's data

A scoping spike: what it would take for our driving engine (driving_standalone, docs/driving/standalone.md) to load
the GameCube version's driving files (the US release) in place of the Xbox's, as docs/gc-data.md does for the action
engine. The game code would stay the Xbox port; the files would be found on the GameCube's disc and the formats that
differ made into what the Xbox code expects as they load. Nothing in game yet.

Status (26 October 2026): scoped, with an offline checker (`gcdrive_check`, [Tools](#tools)) that compares every file
of the two discs and turns the GameCube's data-group record headers into the Xbox's. No seam in game code; the
default build is untouched (the checker is a separate target, excluded from the default build).

**Verdict: harder than the action engine's GameCube work.** The file system is easier (the same EA BIGF archives
under the same names), the text, attribute and tuning files are mostly the Xbox's byte for byte, the movies are the
same EA MAD codec (no HVQM4), and the streams are the same EA-XA codec. But the driving engine's models are bound to
its renderer in a way the action engine's are not: on the Xbox, EAGL's render methods (vertex and pixel shader
programs and their packet streams) are *data* on the disc (`eaglrm.o`, `bondrm.o`) and every model carries D3D
vertex buffers made for them; on the GameCube the render methods are *code* compiled into `driving.elf` (C++
`GeoPrim_*` classes over the GX's TEV), and the models carry GX vertex arrays and display lists for those. So
converting the models is the action work's mesh conversion again, plus render methods to supply that the GameCube
disc does not have in any form. On top of that the data is mixed-endian inside single files (little-endian ELF
framing and relocated pointers around big-endian payloads, little-endian chunk sizes around big-endian chunk
contents), and the data-group format (CARP) has some 55 record types to turn round field by field.

Sources: the GameCube disc as extracted by the extractor project (Q:\nightfire-re, read only:
`iso_extract/gc_us`), the US Xbox disc (`iso_extract/xbox_us`) for the twins, `driving.elf` read statically (its
strings and program headers; it has no symbol table; it is imported in Ghidra as `/GameCube_US/driving.elf` but was
not analysed for this spike), and our driving code and docs. The extractor project's driving support is a BIGF
unpacker (`common/extraction/extract_bigf.py`, `extract_driving.py`); it has no GameCube or PS2 driving format
readers beyond that. Every count below is from `gcdrive_check` or the spike's scripts over the two discs; entries that
occur in several archives are counted once unless said otherwise.

## The switch (proposed)

A CMake option **`NIGHTFIRE_DRIVING_DATA_GC`** (default OFF) for driving_standalone and driving_standalone_test,
compiled as `NIGHTFIRE_DATA_GC=1` like the action engine's, with the GameCube code in `src/driving/gc/` and each
seam in game files `#if NIGHTFIRE_DATA_GC`. Off, nothing compiled differently, checked with `tools/pe_compare.py`.
drivinginject never gets it. `DiscPath` names the extracted GameCube disc (the folder holding `driving.elf` and
`driving\`), the one the action engine's GameCube build uses. The two engines' GameCube builds must be shipped
together: the hand-over page (`psiLaunch.bin`, src/common/launchInfo.cpp) is ours on both sides, so it needs no
change, but an action GameCube build must not hand over to an Xbox-data driving build or the other way round.

Not added yet: with no seam to guard, the option would do nothing.

## The disc

The GameCube's driving files are loose in `driving\` beside `driving.elf` (2.9 MB, PowerPC, entry 0x80007100) - the
same archive names as the Xbox's `driving\`, the same BIGF container:

| GameCube | Size | Xbox twin (US) |
|---|---|---|
| `misc.viv` (86 entries: car models, controls, galleries, locale, loading screens, movies `ntsc\eng\*.mad`) | 140 MB | `misc.viv`, 90 entries, 188 MB |
| `race.viv`, `mis01/3/4/11/13a/13b/13c.viv` (tracks, cars, actors, anims, banks, tuning) | 69 MB | the same eight, 75 MB |
| `mis*.mus` (music streams, `*.asf`) | 61 MB | the same seven, 79 MB |
| `mis*en.spe` (speech streams, `*.asf`) | 124 MB | the same seven, 185 MB (the PAL disc has fr/ge/sp too) |
| `debug.gfn` | 7 KB | `debug.xfn` |
| - | | `bond.dat` (204 KB, high-entropy; nothing in our code reads it) |

Missing on the GameCube: `data\render\eaglrm.o` and `bondrm.o` (the render-method library, see
[Models](#models-and-render-methods)), `data\loading\error.xfn`, `data\loading\test1.sub`. Present only on the
GameCube: `all.asf` in `mis3.mus`. The BIGF directory is big-endian on both discs and the header's total size little-endian on both (as
our FileSys reads them), so the archives open unchanged. `misc.viv`'s header total does not match its length on
either disc.

## Format by format

Counts are files paired by name (`.gsh`/`.gfn` with `.xsh`/`.xfn`), compared after RefPack unpacking.

| Format | Same? | What differs | Known | Unknown | Seam in our code | Size |
|---|---|---|---|---|---|---|
| Archives (`.viv`, `.mus`, `.spe`) | yes | none in the container | all | - | none: FileSys.cpp reads them as they are; `DiscPath` | none |
| File names | nearly | `.gsh` for `.xsh`, `.gfn` for `.xfn` (the game builds these names in code: Realgraph.cpp, Character.cpp, Model.cpp, GameLoop.cpp, Hud.cpp, FrontEndSystem.cpp, RCARPFile.cpp); four Xbox files missing (above) | all | - | FileSys.cpp's open: one rename, and the missing files answered | small |
| Text and data: `.atr` (69), `.h` (42), `.loc` (9), `.sub` (9), `.txt` (4), `.dir` (1) | yes | byte for byte the Xbox's | - | - | none | none |
| `.ini` (17), `.tun` (32), `.def` (12) | mostly | 1 `.ini` (`data\audio\default.ini`: the mix groups), 2 `.tun` (car render, switch effects: other material lists), 11 `.def`: the control maps name GameCube buttons (`DButtonA`, `DButtonB`, `DButtonX`, `DButtonY`, `DButtonLTrigger`, `DButtonRTrigger`, `DButtonZTrigger`) where the Xbox's name `AButtonA`..`AButtonWhite`, `DButtonBack` | all | how the GameCube's analogue triggers map | engine/InputDevice.cpp XBoxPadDevice::Initialize's scalar names, or the names mapped as the `.def` loads | small |
| Data groups: CARP `.crp` (54: tracks, cars, weapons), galleries `.gal` (4) | container yes, payloads no | record headers big-endian, their flags word a bit-field laid out from the top bit down (`gcdrive_check udata` turns them round: 202 of 210 groups and 57,241 of 58,054 data records then have the Xbox's flags, counts and sizes; the rest are records whose payload size differs). A 5-bit field in each data record's flags differs in value for most types (the Xbox's 1 or 2 is the GameCube's 4; 27, 31, 24 the same). Payloads big-endian field by field, with bytes and halfwords: of 58k payloads 15k are byte-identical (strings, symbolic references), 3.7k are the Xbox's with every word turned round, 39k need each type's layout (collision nodes `cn`, instances `in`, symbolic tables `rs`, paths `pt`, AI, triggers, rules ...). 368 records on the Xbox only (`wn` names, `sr` references numbered differently), 114 on the GameCube only | the header layout; the record types the engine reads (data/Carp.h, UData.h, UGroup.h, the resolvers) | the 5-bit field's meaning; record types our code only passes through | engine/UGroup.cpp Deserialize (or the loader before it): headers then payloads by tag, as the file loads | medium-large |
| EAGL objects: skeletons `*skel.o` (9) | container yes | little-endian MIPS ELF on both discs (the PS2 tool chain's format as a container); the `.data` payload big-endian words (545 of 547 non-zero words are the Xbox's turned round) | all | - | the loader's file read: words turned round outside the relocation sites | small |
| EAGL objects: animation banks `.dat` + `.rel` (18 banks) | container yes | same sizes; half the words the same, a third turned round, the rest halfwords (EAGLAnim's compressed channels). On the GameCube the `.rel` is a real second half (string, symbol and relocation sections) where the Xbox's `.rel` is a copy of the `.dat` - the split path the Xbox data never reaches (docs/driving/eagl.md 3.1) | channel layouts: ours (eagl/anim, AnimDecode.cpp) | - | anim/AnimationDatabase.cpp load, by channel type | medium |
| EAGL objects: models (`.dat` + `.rel` actors, 62; `.crp` ELFd + ELFr, 54 files, 12,919 models) | no | the same `__Model`, `__BBOX`, `__EAGL::TAR`, GeoPrimState names, but **no `__VertexBuffer` (37,019 on the Xbox) and no `__RenderMethod` (7,391 on the Xbox)**: each GeoPrim names a render method the executable defines (27 names, e.g. `BondCarTextureDull`) and its parameters are big-endian vertex arrays (float positions, 16-bit normals, texture coordinates, damage vectors, byte damage zones) and a **GX display list** (`98 00 0a ...`: indexed triangle strips). Pointers (relocation sites) little-endian inside the big-endian data. The Xbox's `LightBlock` is a `PositionalLightBlock`; the GameCube's actors add `COORD3`, `int`, `MATRIX4 *` externals | the GeoPrim shape (a render method, then count/pointer pairs: eagl.md 2.8) and the display lists' commands | each of the 21 GameCube render methods' parameter list and what its TEV does (driving.elf, no symbols) | eagl/Loader.cpp (big-endian payloads, the split object), a GeoPrim converter per render method ([Models](#models-and-render-methods)) | large |
| Render methods | **absent** | the Xbox's are `eaglrm.o`/`bondrm.o` on the disc (packets, NV2A vertex and pixel shader programs); the GameCube's are `GeoPrim_Bond*` C++ classes in `driving.elf` (strings `EAGL::TevStage`, `EAGL::GCBuffer`, `DisplayList`). The Xbox's has render methods the GameCube's has not (`BondCarShadow`, `BondCarSpecularBump`, `BondSpecularBumpRoad`, `BondRoad*`, `BondReflectPacked`, `BondUnderWaterPacked`, `BondGouraud`, `BondDynTexture`, `BondFrameAnim`) and the other way round (`BondCarTextureNoDamage`, `BondCarGouraudDull`, `BondTexturePacked` ...) | the Xbox's interpreter (eagl/RenderMethod.cpp), the backends' NV2A shader translation | the GameCube's TEV set-ups per class | a render-method library of our own, loaded where RCARPFile loads `eaglrm.o`/`bondrm.o` | large |
| Textures: SHPG `.gsh` (133 files, 2,472 images paired) | no | `SHPG` (size little-endian, the rest big-endian) for `SHPX`; image types 30 (4 bpp: CMPR, for the Xbox's DXT1 0x60), 25 (8 bpp with a 16-bit 256-entry palette attachment, for P8 0x7b with a 32-bit palette, and for DXT3/5 0x61), 24 (4 bpp, 16-entry palette), 22 (32 bpp), 21 (16 bpp); mostly no mip levels; 1,320 of 2,472 at another size than the Xbox's | the container; CMPR, RGBA8, RGB5A3 from the action work (src/action/gc/GcTexture.cpp) | the palette entries' format (RGB5A3 or IA8: attachment type 0x32), types 21/22 exactly | eagl/Realgraph.cpp SHAPE load (one place: `SHAPE` loader, TAR from SHAPE) | medium |
| Fonts: FNTG `.gfn` (2 + `debug.gfn`) | no | `FNTG` for `FNTX`, big-endian fields, its glyph image a GameCube one | the FNTX layout (Realgraph.cpp) | - | Realgraph.cpp FONT load | small |
| Sound banks `.bnk` (42) | container yes, codec no | `BNKb` (big-endian header) for `BNKl`; patches' PT headers platform 6, codec 18 (GameCube DSP ADPCM) where the Xbox's are platform 7, codec 20 (Xbox ADPCM); **1,302 of 1,323 patches at 32 kHz where the Xbox's are mostly 16-24 kHz**, the durations by the headers about the same (1,212 s against 1,233 s), so 21.7 MB against 13.2 MB: the GameCube's effects are the better ones here, the reverse of the action engine's | the PT tags (endian-neutral) | where each sample's DSP coefficients are | audio/Bank.cpp or the bank load in snd: decoded to 16-bit PCM once (sample representation S16), as the action build's PcmBanks | medium |
| Streams `.asf` (36 music, 200 speech) | codec yes | the same EA-XA (codec 10) in the same SCHl/SCCl/SCDl/SCEl chunks, at 32 kHz where the Xbox's are 48 kHz; chunk sizes little-endian on both, but the SCCl count and each SCDl's sample count and channel offsets big-endian on the GameCube | all | - | the stream parser (snd SNDSTRM): the SCCl/SCDl words turned round | small |
| Movies `.mad` (12) | yes but audio | the same EA MAD video (same chunk counts, 1,564 frames in `paris_intro`), at 640x448 where the Xbox's are 640x480; the audio stereo EA-XA at 32 kHz where the Xbox's is 5.1 at 48 kHz, SCDl words big-endian. No HVQM4 here (that is the action engine's GameCube movies) | all | - | engine/PlayMPC.cpp: the SCDl header turned round before adpcm_ea_r1, the size from the MADk header | small |
| `alookup.bin` | yes, turned round | each word big-endian | all | - | its loader | small |
| `decal.dat` | yes | byte for byte | - | - | none | none |

## Seams

| Seam | Game file | GameCube code |
|---|---|---|
| The disc and names | platform/FileSys.cpp: `DiscPath`; open renames `.xsh`/`.xfn` and answers the four missing files | `src/driving/gc/GcViv` (offline reader, done) |
| Data groups | engine/UGroup.cpp Deserialize (CARP, galleries) | `src/driving/gc/GcUData` (headers, done), payloads by tag (to do) |
| EAGL objects | eagl/Loader.cpp DynamicLoader (big-endian payloads around little-endian pointers; the split `.dat`/`.rel` the Xbox data never used) | per class: skeletons, anim banks, models |
| Models | a GeoPrim converter as the objects load: GX display lists and vertex arrays into Xbox vertex buffers and GeoPrims for our render methods | to do |
| Render methods | data/RCARPFile.cpp LoadEAGLMaterials (`eaglrm.o`, `bondrm.o`) | a library of our own |
| Textures, fonts | eagl/Realgraph.cpp SHAPE and FONT loads | as src/action/gc/GcTexture.cpp, plus C4/C8 palettes |
| Sound | the snd bank load (codec 18 to PCM), the stream parser (SCCl/SCDl words) | DSP decoding as src/action/gc/GcSound.cpp |
| Movies | engine/PlayMPC.cpp (SCDl words, stereo 32 kHz, 640x448) | - |
| Controls | engine/InputDevice.cpp scalar names | - |

## Models and render methods

What makes this harder than the action engine's meshes, from the objects' symbols and relocations
(`gcdrive_check objects`) and a GeoPrim walked by hand (taxi.crp, the first `BondCarTextureDull` primitive):

```
+00  render method                -> BondCarTextureDull      (defined by driving.elf, not by any data file)
+08  1    -> 0x0000               the primitive's counts
+10  1    -> gpModelViewMatrix, +18 1 -> gpViewMatrix
+20  267  -> float x, y, z        positions, big-endian
+28  267  -> s16 x 3              normals
+30  267  -> s16 x 2              texture coordinates
+38, +40  267 -> s16 x 3          damage vectors (two sets)
+48  16   -> EAGL::DamageZones
+50  267  -> u8                   damage zone per vertex
+58  267  -> float (half used)
+60  3424 -> 98 00 0a ...         a GX display list: triangle strips of 16-bit indices
+68  1    -> a 0x24-byte record, +70 GeoPrimState "STATE::texalpha=noA;primtype=tristrip", lights, lighting props
```

The Xbox's twin of the same primitive is a `RenderMethod` (`ParentRM_BondCarTextureDull` plus its
`*__EAGLMicroCodeVS`/`PS`) and D3D vertex buffers. The state strings (`STATE::...`, made by RStateManager) and the
textures (TAR) are shared ground; the geometry and the shading are not. A conversion needs, per GameCube render
method (21 used): its parameter list, a vertex layout and an Xbox-style render method that draws it as the GameCube's
TEV set-up does - written by us, as nothing on the GameCube disc is the Xbox's. Two ways to get there:

- **Our own render-method library**: packet streams and NV2A shader programs (or the shader source the backends
  already translate them into), one per GameCube class, checked frame by frame against the Xbox build. The honest
  route; the main cost of the whole job.
- **The Xbox's library as a stop-gap**: load `eaglrm.o`/`bondrm.o` from an Xbox disc and feed the converted GameCube
  geometry to the nearest Xbox method. Breaks the rule the action work kept (nothing read from the Xbox disc), so only
  as a documented, temporary exception if wanted at all.

The game code also builds or looks up render methods by name (`RenderMethod.cpp`'s static ones, materials,
shadows, reflections); which of those the GameCube build has, and what it does for the Xbox-only ones such as
`BondCarShadow` and `BondCarSpecularBump`, is a research item in driving.elf.

## Compared with the action engine's GameCube work

| | Action engine (done) | Driving engine |
|---|---|---|
| File system | replaced whole (loose files for archives, EDL bundles) | unchanged archives; a rename |
| Endianness | big-endian payloads, not uniform | the same, and mixed inside files: little-endian ELF framing and pointers, SCHl chunk sizes, SHPG size; mirrored bit-fields in data-group headers |
| Containers | one bundle format, fixed block types | CARP data groups (~55 record types), ELF objects (per class), SHPG, FNTG, BNKb |
| Geometry | GameCube meshes (indexed strips) to the Xbox's KXE, drawn by the Xbox's fixed renderer | GX display lists and vertex arrays to vertex buffers, **and render methods to supply** |
| Data in the executable | glyph tables, button symbols | the render methods (shading for every model) |
| Textures | CMPR, RGB5A3, RGBA8 | the same plus C4/C8 palettes; mostly no mips |
| Sound | DSP ADPCM banks, IMA music, worse rates | DSP ADPCM banks (codec 18) at better rates; EA-XA streams already our codec |
| Movies | HVQM4 (no decoder) | EA MAD, as the Xbox's |
| Streaming | none | the same EA stream parser, a few words turned round |
| Hand-over | ours | ours, unchanged |

## Milestones

| # | Milestone | Needs | Size |
|---|---|---|---|
| 0 | Offline census and the checker: every file paired, record headers turned round (`gcdrive_check`) | - | done |
| 1 | The disc served: `DiscPath`, the renames, the missing files; the switch and pe_compare | 0 | small (0.5 day) |
| 2 | Containers turned round and checked against the twins offline: CARP and gallery payloads by tag, skeletons, anim banks, `alookup.bin`, fonts | 1 | medium-large (1-1.5 weeks; the CARP schema is most of it) |
| 3 | Textures: SHPG to SHPX (CMPR, C4/C8 with palettes, RGBA8, 16-bit), checked by decoding both | 1 | medium (2-3 days, reusing GcTexture.cpp) |
| 4 | Sound: banks' DSP ADPCM to PCM through the EA library, streams' SCCl/SCDl words, 32 kHz | 1 | medium (3-4 days) |
| 5 | Movies and controls: PlayMPC's SCDl words and size; the `.def` button names | 1 | small (1 day) |
| 6 | Models: the loader's split objects and big-endian payloads; GeoPrims converted per render method (display lists to strips, vertex arrays to buffers), offline against the Xbox twins' vertices as gcdata_check meshes did | 2 | large (2-3 weeks) |
| 7 | Render methods of our own for the 21 GameCube classes (and whatever the game code asks for by name), checked by frames against the Xbox build on the runner | 6 | large (2-4 weeks), the main risk |
| 8 | A mission in game end to end (mission 1), then all eight; lockstep against itself | 2-7 | medium |

Milestones 1-5 are of the action work's size and kind; 6-7 have no counterpart there.

## Research items

- **The GameCube's render methods** in driving.elf (`/GameCube_US/driving.elf` in Ghidra; no symbols): each
  `GeoPrim_Bond*` class's parameter list and TEV stages, and which render methods the game code itself builds.
- **The data groups' 5-bit field** (flags bits 3-7 on the Xbox, 28-24 on the GameCube): the Xbox's 1 and 2 are the
  GameCube's 4; whether any code reads it.
- **The CARP record types our code does not read** but passes on (their layouts for the swap).
- **SHPG palettes and types 21/22**: the 16-bit palette format and which 16- and 32-bit GX formats.
- **DSP coefficients** in the GameCube banks: in the PT header or before each sample's data.
- **The GameCube's analogue triggers and Z** in the control maps against XBoxPadDevice's scalars.
- **The GeoPrimState strings** both discs use (`STATE::modify=bumpmap;...`): whether RStateManager makes the same
  states from the GameCube's.

## Tools

`gcdrive_check` (src/driving/devtools/GcDriveCheck.cpp, `cmake --build . --config Release --target gcdrive_check`),
with src/driving/gc/GcViv.cpp (the archives, RefPack through platform/RefPack.cpp) and src/driving/gc/GcUData.cpp
(the record headers). No game state; runs over the two extracted discs.

```
gcdrive_check <gc disc> <xbox disc> files     every entry against its Xbox twin, by archive kind and extension
gcdrive_check <gc disc> <xbox disc> udata     .crp/.gal headers turned round, trees and payloads against the twins
gcdrive_check <gc disc> <xbox disc> objects   EAGL objects' classes, relocations and names no object defines
gcdrive_check <gc disc> <xbox disc> sound     PT headers of every stream and bank patch: platform, codec, rate
```

A disc is the folder holding `driving\`.
