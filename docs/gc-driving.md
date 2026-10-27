# The driving engine on the GameCube's data

A sibling of [gc-data.md](gc-data.md) and [ps2-driving.md](ps2-driving.md): our driving engine (driving_standalone,
docs/driving/standalone.md) loading the GameCube US release's driving files in place of the Xbox's, behind a build
switch, with the game code staying the Xbox port. It started as a scoping study (kept below) and became, on the branch
`gc-driving-rm`, a working build of the shortcut the PS2 branch took: **the GameCube's models drawn with the Xbox
disc's render methods**.

Status (27 October 2026): the switch is in (`NIGHTFIRE_DRIVING_DATA_GC`, the default build unchanged), and **every
mission loads and its world, cars and props draw from the GameCube's geometry and textures with the Xbox's render
methods**, compared with the Xbox build on the test runner ([In game](#in-game)). The HUD, fonts and front-end
galleries draw from the GameCube's images, and the sound banks play the GameCube's samples (re-encoded). Not done:
characters (skinned GeoPrims are left out of the draw lists), morph targets, movies, and most methods are drawn with a
simpler Xbox method than their twin ([the mapping table](#the-mapping-table)). See [What is left](#what-is-left).

**The exception to "nothing from the Xbox disc".** The action engine's GameCube build reads nothing from the Xbox
disc. This build reads two files from it, by agreement: `data\render\eaglrm.o` and `data\render\bondrm.o` from the
Xbox disc's `driving\misc.viv` - the Xbox's render methods (NV2A vertex shaders, register-combiner set-ups and packet
programs, data the D3D9 and SDL GPU backends already run). Everything else comes from the GameCube disc. On the
GameCube the render methods are code in `driving.elf` (`GeoPrim_Bond*` classes over the GX's TEV), so its disc has
nothing to stand in for them. settings.ini:

```
DiscPath=C:/nightfire-gc/disc               ; the extracted GameCube disc: driving.elf, driving\ ...
XboxDiscPath=C:\nightfire-cxbx\disc         ; the Xbox disc: driving\misc.viv (its render methods only)
```

A build that needs no Xbox disc would replace those two files with render methods of our own (the scoping study's
milestone 7); the GeoPrim conversion below would stay as it is, since it produces Xbox GeoPrims for whichever methods
draw them.

Sources: the GameCube disc as extracted by the extractor project (Q:\nightfire-re, read only: `iso_extract/gc_us`),
the US Xbox disc (`iso_extract/xbox_us`) for the twins, Agent Under Fire's GameCube `driving.elf` (Ghidra
`/Similar_Games/AUF/GameCube/driving.elf`, fully symbolised: the GameCube GeoPrim's layout, the render methods'
`*_FieldNames` tables, the vertex formats, the SHPG texture types, the bank's DSP fields), the field-name tables of
Nightfire's own GameCube `driving.elf` (`/GameCube_US/driving.elf`), and our driving code. Every count is from
`gcdrive_check` or `tools/gcdriving_check.py` over the two discs ([Tools](#tools)).

## The switch

A CMake option **`NIGHTFIRE_DRIVING_DATA_GC`** (default OFF), for driving_standalone and driving_standalone_test. On,
the targets are compiled with `NIGHTFIRE_DRIVING_DATA_GC=1` and the files of `src/driving/gc/` are added; a handful of
seams in game files are guarded with `#if` ([Seams](#seams)). Off, nothing compiles differently: checked with
`tools/pe_compare.py` on 27 October 2026, driving_standalone built from `gc-driving` and from `gc-driving-rm` has
identical `.text`, `.data`, `.reloc` and `.rsrc` (`.rdata` differs only in the debug directory's timestamp).
drivinginject never gets it.

```
cmake -B build/gc -A Win32 -DPython3_EXECUTABLE=C:/Python314/python.exe -DNIGHTFIRE_DRIVING_DATA_GC=ON
cmake --build build/gc --config Release --target driving_standalone
```

Both engines would be switched together in a GameCube run folder: the action engine's GameCube build hands over to
a GameCube driving build through `psiLaunch.bin` exactly as today, since both sides of the hand-over are our code.
`DiscPath` names the extracted GameCube disc, whose `driving\` folder sits where the Xbox's does. With `Region=PAL` in
settings.ini the game asks for `PAL\eng\*.mad`, which the US disc has not, and plays no movie; the disc's movies are
`ntsc\eng\` ([What is left](#what-is-left)).

## Seams

Each a guarded line or two in the game file; the work in `src/driving/gc/`.

| Seam | Game file | GameCube code |
|---|---|---|
| A file's name: `.xsh`, `.xfn` are `.gsh`, `.gfn` on the GameCube disc | platform/FileSys.cpp `FILEDEV_resolveopen` | GcFiles.cpp `Gc_FileName` |
| A file just loaded (and unpacked): data groups, image containers, fonts, sound banks, controller configurations, the animation lookup | platform/FileSys.cpp `LoadInternal`, `FILE_loadpackz` | GcFiles.cpp `Gc_ConvertFile`; GcUData.cpp, GcShape.cpp, GcSound.cpp |
| The render-method libraries | data/RCARPFile.cpp `LoadEAGLMaterials` | GcFiles.cpp `Gc_LoadXboxRenderMethods` (the Xbox disc), GcRenderMethods.cpp (the GameCube methods' names) |
| An EAGL object relocated, before its constructors run: every symbol's data turned round by class | eagl/Loader.cpp `DynamicLoader::Resolve` | GcObject.cpp `Gc_ConvertObject` |
| A CARP file's models (tracks, cars, props, weapons), its constructors run | data/RCARPFile.cpp `Resolve`, after `loader->Resolve()` | GcModel.cpp `Gc_ConvertModels` |
| A character's models | anim/Model.cpp `LoadObject` | the same (all skinned: taken out of the draw lists) |
| A stream's SCDl chunk: its frame count and channel offsets | sound/snd/Streams.cpp `SNDSTRMI_parsedata` | GcFiles.cpp `Gc_ConvertStreamChunk` |
| The traffic's car types: `NUM_TRAFFIC_TYPES_GC` (the GameCube release's key; the Xbox's is `_XBOX`) | game/AIVehicleController.cpp | - |
| The pedestrian types: `NUM_PED_TYPES_GC` (no attribute file sets it, so the default stays) | game/AIPedestrian.cpp | - |

The GameCube's `driving.elf` holds the strings `NUM_TRAFFIC_TYPES_GC` and `NUM_PED_TYPES_GC` where the Xbox's has
`_XBOX`; the GameCube's `paris.atr` sets `NUM_TRAFFIC_TYPES_GC=3`.

## Data groups (CARP, galleries)

The containers are the Xbox's UGroup trees, big-endian, their flags words bit-fields from the top bit down
(GcUData.h). The payloads are turned round **record type by record type** (GcUData.cpp's table): a layout string per
type (`w` a word, `h` a halfword, `b` a byte) taken from the structure the game reads it as (data/Carp.h, world/,
game/, render/), with a function for the types whose layout depends on their contents (AI elements by type, rules by
type, map nodes and their index lists, collision articles' strips, vertices, barriers and window groups, skeletons'
bones then names, effects by type, gallery keys by part). Strings, symbolic references, texture names and the
gallery's picture stream (`fi`) are left as they are; the model object (`ELFd`, `ELFr`) goes to the loader.
`gcdrive_check payloads` compares every converted payload with its Xbox twin: 47 of the 51 types that have twins of the
same size are 99.5% or more byte for byte the Xbox's, most 100%; the rest differ where the data does (collision
noise in the last bits, records in another order, `Sect`'s own section sizes, the 8 unknown bytes of `co`).

Found on the way, besides the structures:

- **Mirrored bit-fields in payloads too.** An instance's packed dimensions (`in`, `Cams`: x, y, z in 10 bits each and
  two flags) are laid out from the top bit down on the GameCube (x in bits 31-22), from bit 0 up on the Xbox.
- **Fields the GameCube's tools wrote at another width:** `ps` +0x05 is a halfword on the GameCube (two bytes on the
  Xbox), `mp`, `rj` and `rr`'s first word are two halfwords, an effect's flags and bits (+0x10) one word.
- **Gallery keys in either order.** The galleries' keys (`gk`) are big-endian in the first key of a part, but later
  keys hold some fields already in the Xbox's order (the orientation, some data, often the whole last key): each word
  is taken in whichever order reads as a sane float or a small integer (an integer first for integer fields). 98% of
  the gallery bytes then agree with the twins; the rest are particle words both readings make plausible.
- The 5-bit field in each data record's flags (the Xbox's 1 and 2 are the GameCube's 4) is passed through; nothing
  reads it.

## EAGL objects

The GameCube's objects are the Xbox's container: little-endian MIPS ELF, the same sections, symbols and relocations;
an actor's `.dat` is the first half and its `.rel` the second (the split form our loader already had). Their data is
big-endian except at the relocation sites, which the tools left little-endian and the loader fills with addresses. So
GcObject.cpp runs as `DynamicLoader::Resolve` finishes the relocations and before the constructors: it collects the
relocation sites and turns each symbol's data round by its class, never a site, and each word once:

| Class | Layout |
|---|---|
| `Model` (0xd8) | words; +0x6c..+0x9b hold halfwords of the GameCube's own where every Xbox twin has zeros, so they are cleared; then what its pointers lead to: the draw list (words: a block's entry and two words, or 0xffff and a GeoPrim), the enable table (halfwords, one entry a block), the TAR list (name, count, TARs), the patch list. Morph data is left out (cleared). |
| `EAGL::TAR` (0x4c) | words, but the shape's and the clut's four-character names |
| `Skeleton` | two halfwords, then words: the count, the length scales, 0x70 bytes a bone |
| `Bone`, `BBOX` | a word (the index); six words |
| `AnimationBank` | its header's words, then each anim by type: compound (halfwords, its attribute block, its sub-anims), raw events (words), DeltaQuat and KeyQuat (halfword indexes and key times, the delta-compressed values' dofs as words and their deltas as halfwords when 16-bit), DeltaF1 and DeltaF3 (halfword indexes and times, range records of words and halfwords, each key block's first-key halfwords, its byte deltas left, the constants' halfword indexes and words). The disc's banks hold only these six types. |

The GameCube's objects define no `RenderMethod` and no `VertexBuffer`: their GeoPrims name render methods `driving.elf`
defines (27 names), and GcRenderMethods.cpp registers those names so the references resolve to the mapping table's
entries. The skinned methods' four externals (`ShadowDarkness`, `ReferenceTriangle`, `ModelAlphaValue`,
`DrawAsShadow`) are registered too, to a block of zeros.

## GeoPrims

What `GcModel.cpp` reads (and `tools/gcdriving_check.py geoprims` decodes offline):

- A GameCube GeoPrim is `{render method, pcode, then a {count, pointer} pair per field}` - one word more than the
  Xbox's - in the order of the method's `*_FieldNames` table (Agent Under Fire's `driving.elf` names them; Nightfire's
  GameCube `driving.elf` registers 42 methods with the same kind of tables, which give the field lists; BondCarGlass,
  which no registration names, has its list from its GeoPrims). Counts big-endian, pointers relocation sites.
- **Positions**: big-endian floats, or shorts in 1/256ths for the "Packed" methods (`x / 256` agrees with the Xbox
  twin's floats exactly). **Texture coordinates**: shorts, in 1/256ths for the packed and car methods (the Xbox packed
  methods' own unit: the ratio to the twins is 1.0 for 290,000 coordinates), 1/16384ths for the other static methods
  (BondTexture: 64 times the twins'), 1/8192ths for BondScrollTex1. **Colours**: RGB565 halfwords for the packed
  methods, RGBA8 words for the others; the car methods have none (they are worked out per frame from the damage).
- **Display list**: GX triangle strips (0x98) and triangles (0x90, 73 on the disc against 95,317 strips), each vertex a
  16-bit index per attribute the method's vertex format has (position, normal, colour, texture coordinates: 3 for the
  static methods, 4 for the car and dynamic ones, 5 for BondCarGlass, 2 for Gouraud) - and every attribute's index is
  the same on this disc, so one index serves.
- Over every CARP file: 6,903 GameCube GeoPrims against 6,961 for the Xbox twins (nearly one for one, unlike the
  PS2's batches), 494,332 vertices; of 4,112 models with an Xbox twin and both bounds, 4,085 agree to 0.05.

An Xbox GeoPrim is made from each, as the PS2 branch makes its own: the positions as `FLOAT3`, the texture coordinates
as `SHORT2` in 1/256ths, a `D3DCOLOR` array (the GameCube's 0xff made the Xbox packed methods' 0x80; white for the car
methods), and 16-bit indices joining the strips into one strip with repeated indices, or triangles for a GeoPrimState
whose primitive type is a list. The state and the TAR are the GameCube GeoPrim's own fields. Each stream's source is a
`StaticVertexBuffer` and a D3D8 vertex buffer header over the array, laid out where opcode 9 reads it; the child
method's dynamic stream packets become inline ones (as the PS2 branch does), each with a variation stride of 0: the
GameCube's colours are one set whatever the model's variation, which picks only its textures. The arrays are host memory
(`calloc`), never freed.

## The mapping table

`GcRenderMethods.cpp`. The Xbox twin's methods come from `tools/gcdriving_check.py geoprims` (the Xbox parent methods
of the models each GameCube method's models are paired with, by name; a model mixes methods, so each row lists the
leading ones).

| GameCube method (GeoPrims) | Xbox method now | The Xbox twins' methods | Notes |
|---|---|---|---|
| `BondTexturePacked` (3,669) | `BondTexturePacked` | `BondTexturePacked`, `BondRoadPacked` (roads), `BondReflectPacked` | |
| `BondTexture` (433) | `BondTexturePacked` | `BondTexture`, `BondRoad` | coordinates / 64 |
| `BondReflectTexturePacked` (393) | `BondReflectPacked` | `BondReflectPacked`, `BondRoadReflect`, `BondTexturePacked` | `{0.4375, 0.5, 5, 0}`, as every Xbox GeoPrim of it in paris_mis01.crp |
| `BondDynamicTexture` (289) | `BondTexturePacked` | `BondDynTexture` | unlit |
| `BondScrollTex1`, `...Packed`, `BondScrollTex2`, `...Packed`, `BondScrollTex3Packed` (138) | `BondTexturePacked` | `BondScrollTex1/2/3` | the first coordinate set, not scrolled |
| `BondFrameAnimPacked` (9) | `BondTexturePacked` | `BondFrameAnim` | not animated |
| `BondUnderWater` (338), `BondWater` (27), `BondKelp` (18) | `BondTexturePacked` | `BondUnderWaterPacked`, `BondUnderWater`, `BondWater`, `BondKelp` | no caustics, no scrolling, no sway |
| `BondCarTexture` (288), `...Dull` (892), `...Switch` (149), `...NoDamage` (71), `BondCarChrome` (75), `BondCarGlass` (52) | `BondTexturePacked` | `BondCarTexture`, `...Dull`, `...Switch`, `BondCarChrome`, `BondCarGlass`, `BondCarSpecularBump` | unlit, white, no reflection, no damage deformation |
| `BondCarGouraudDull` (48) | none | `BondCarShadow` | the cars' shadows: left out |
| `BondGouraudPacked` (14) | none | `BondGouraud` | no texture; left out |
| `BondLitTextureAndShadow_Skin` (1,231), `...Specular_Skin` (20) | none | the same names | characters: left out |

The field lists pair the methods: the GameCube's car methods carry the Xbox car methods' streams (normals, damage
vectors and zones, sphere and specular maps, lighting); converting those into the Xbox twins' own parameters is what
drawing the cars lit and damaged needs.

## Textures, fonts, galleries

`GcShape.cpp`, applied to every image container as it loads:

- **SHPG to SHPX.** The directory is the same, its words big-endian (the size word little-endian on both). A GameCube
  image's header is 16 bytes: the type byte, the 24-bit offset to the next attachment, width and height, a word and a
  flags word (the mip count in the high nibble of +0x0e, plus one: every image on the disc has one level). Its texels
  are the GX's tiles (TARPrivate::InitFromShape: type 0x10 + the GX format; 0x18-0x1a C4, C8, C14X2; anything else
  CMPR). Of the disc's 2,474 images, 1,496 are C8 with an RGB5A3 palette (`0x19` + `0x32`), 773 CMPR (`0x1e`), 167
  RGBA8 (`0x16`), 29 RGB5A3 (`0x15`) and 9 C4 (`0x18`); the fonts' images are C4 and C8.
- Each image becomes: CMPR the Xbox's DXT1 (0x60: the same blocks, reordered - nothing lost), C4 and C8 with sides that
  are powers of two an 8-bit image (0x7b, swizzled) with a 256-colour palette attachment (0x2a), anything else 32-bit
  (0x7d, swizzled when its sides are powers of two). The GameCube's info attachment (`i`) is empty; the Xbox's holds
  the 0x40 bytes the texture code keeps its shared record in (EAGL_FindSharedData), so one is made. The name
  attachment is kept.
- **An Xbox image's pixels start at +0x10** when its flags have no 0x1000 (the "data at offset" word is then the first
  texel: eagl/Tar.cpp's ImageData). The first build wrote a 0x14-byte header, which shifted every image by four bytes:
  invisible in 32-bit and 8-bit images, half a block in DXT1 (the noise in the first frames of mission 6).
- `gcdrive_check shapes` decodes each converted image and its Xbox twin: of 2,342 images compared, 610 within 4 (of
  255) per channel, 1,532 within 16, the median 12 (the GameCube's art is not always the Xbox's: the loading screens'
  pieces are cut differently, some car liveries differ).
- **Fonts** (`FNTG`, `.gfn`): the header's halfwords and words, the glyphs' code and position halfwords, the kern
  table's count and pairs' halfwords; the image converted as above.
- **Galleries** (`.gal`): the data group turned round, then each `SHPG` item converted, appended at the end of the file
  and its record pointed at it.

## Sound

- **Banks** (`BNKb`, GcSound.cpp): the PT headers' tags are the same on both discs (values big-endian on both). A
  GameCube timbre's samples are DSP ADPCM (representation 0x12, at 32 kHz), its sixteen coefficients and a byte in tag
  0x8f, its data where tag 0x88 (0x89 ... per channel) says from the bank's start. Each is decoded to PCM and encoded
  as Xbox ADPCM (0x14), and the bank rebuilt (header, PT headers, samples), each timbre's tags kept but the
  representation, the data offsets and the coefficients, with the Xbox banks' render mode (tag 0x8c, 0x420).
  `gcdrive_check banks`: 42 banks, 1,207 timbres, the same patch counts as the twins; the Xbox ADPCM decoded again
  against the DSP decode, 27 dB. A bank of no patches is not a silent stand-in: the pedestrians' voice picker loops
  until a patch is present (the female voices never stop looking), which is how mission 1 found it.
- **Streams** (`.asf`): the same EA-XA in the same chunks; each SCDl's frame count and channel offsets are big-endian
  on the GameCube and are turned round as the chunk is submitted.

## Controls

`.def` files name the GameCube pad's controls; GcFiles.cpp renames them to the Xbox pad's by place: `DButtonA` .. `Y`
the Xbox's analogue `AButtonA` .. `Y`, the digital triggers `AButtonLTrigger`/`RTrigger`, Z `DButtonBack`; in the
front end's tables `LStick`/`RStick` `ButtonLThumb`/`RThumb` and `ButtonZTrigger` `ButtonBlack`. The GameCube's
bindings are kept (fire is A).

## In game

On the test runner (vr-desktop, slot 14 with its own checkout `C:\nightfire-gcrm`, builds `build/gc` with the switch and
`build/xb` without; the GameCube disc's `driving\` copied to `C:\nightfire-gc\disc`; the Xbox disc the runner's
`C:\nightfire-cxbx\disc`, European), driving_standalone `-mission N`, frames dumped with `DumpEvery=500`, every mission
run for two minutes after its load with no input:

| Mission | Track | GameCube data (frames 500-6000) |
|---|---|---|
| 1 | paris_mis01 | the opening in the van (seat, door, a car on the road below, an explosion on the road), then Paris at night from the van: the street, the building fronts with lit windows and shop signs, traffic |
| 2 | uw_mis11 | the underwater approach behind the submersible car, a large structure ahead, the torpedo HUD |
| 3 | junglea_mis13a | the jungle track behind the armoured car: dirt road, palms, ferns, the rock walls |
| 4 | jungleb_mis13b | from a gunner's seat over the jungle hillside, the objective text, a rocket's lights; mission failed |
| 5 | snow1a_mis3 | the snow village at night: the buildings, lamps, cars, then the cable car's cabin and the snow slope with fir trees |
| 6 | snow2a_mis4 | the snow road behind the Vanquish: road markings, snow banks, fir trees, the mountain and sky, the timer, the enemy car ahead |
| 7 | junglec_mis13c | the dock: the crane, the waterfalls, the gantries, an explosion; mission failed |
| 8 | snow2a_race | the snow race: the red Cobra, the traffic cars coming up behind, the race timer |

Every mission loads and runs with no fault. Against the default build on the Xbox disc (European; the same runner):

- **Mission 6**: the Xbox build plays the opening movie first (its frames count; the GameCube build finds no movie,
  see [What is left](#what-is-left)), so the GameCube's frame 500 (timer 4:52) stands against the Xbox's frame 1000
  (4:57): the same road, snow banks, fir trees, mountain and sky, the same car, plate and lights, the same HUD (timer,
  radar, weapon). Differences: the Xbox's car is lit and reflective (the car methods are drawn unlit here), and its
  snow banks lighter (their road methods light them).
- **Mission 1**: the Xbox's frames 2000-3500 are the sniping view (the rifle in first person, the street below),
  where the GameCube build is still in the van at 2000 and on the street from the van at 3500 - the same divergence
  the PS2 build shows; the rifle is a character model (skinned), which is not drawn here.

The other missions' Xbox runs were not taken: by then new jobs in the runner's desktop session hung before they
started (a login shell or PowerShell that never ran its first line, other agents' jobs alike).

Heap left at "Entering main loop": 14.4 MB (mission 1) to 21.6 MB (mission 7) with the GameCube's data; the Xbox
build leaves 12.9 MB in mission 1 and 14.2 MB in mission 6, where the GameCube data leaves 14.4 and 15.7 (its images
mostly 8-bit and DXT1 with no mip levels; the converted GeoPrims' arrays are host memory, outside the heap).

## What is left

- **Characters**: their GeoPrims (`BondLitTextureAndShadow_Skin`, `_Specular_Skin`) are taken out of the draw lists.
  Their display lists do not parse with the other methods' rule (one 16-bit index per attribute); their layout (the
  fields `PalletMatrix`, `Weights` and the rest) is to be found and made into the Xbox skinned methods' streams.
  Skeletons and animation banks are converted already (`tools/gcdriving_check.py anims`: every byte the conversion
  touches is the Xbox twin's but an unread halfword of the DeltaF header), so the characters animate unseen.
- **Morph targets** (the heads): the models' morph data is cleared.
- **The methods drawn with a simpler Xbox method** ([the mapping table](#the-mapping-table)): lit and reflective cars,
  damage deformation, scrolling and animated textures, water, caustics, the car shadows and roads' shadow projection
  (`BondRoad*`), the textureless Gouraud GeoPrims. The GameCube GeoPrims carry the streams the Xbox twins' methods
  take (normals, damage vectors and zones, the maps); each needs its Xbox parameters filled in. The vertex colours'
  scale is right for the Xbox packed methods' 0x80 (the twins' ratio is 0.5 on most white vertices), but the twins'
  colours are not the GameCube's for every vertex.
- **Movies**: the disc's are `ntsc\eng\*.mad` (640x448, stereo EA-XA at 32 kHz with big-endian SCDl words, the
  scoping study's finding); with `Region=NTSC` the player would find them, and PlayMPC.cpp needs the SCDl words turned
  round. Not tried.
- **Gallery keys**: 2% of their bytes still differ from the twins (particle words both orders make plausible).
- Converted GeoPrims are never freed (a level's unload leaks them); the Linux build, the 64-bit build (the arrays are
  `calloc`ed outside the game window) and case-sensitive file names are untried.
- The default build is unchanged; CI does not build the switch's sources. They compile with MSVC and, with the switch
  on, in the clang/mingw-w64 cross build (the `linux` preset in the nf-cross image, 27 October 2026, no warnings).

## Tools

`gcdrive_check` (src/driving/devtools/GcDriveCheck.cpp, `cmake --build . --config Release --target gcdrive_check`)
links the conversions themselves (GcUData.cpp, GcShape.cpp, GcSound.cpp) and runs them over the two extracted discs:

```
gcdrive_check <gc disc> <xbox disc> files            every entry against its Xbox twin, by archive kind and extension
gcdrive_check <gc disc> <xbox disc> udata            .crp/.gal headers turned round, trees against the twins
gcdrive_check <gc disc> <xbox disc> payloads [type]  every payload turned round by type, byte for byte against the twin
                                                     (with a type: where in its element the bytes differ, and the
                                                     first differing records side by side)
gcdrive_check <gc disc> <xbox disc> shapes           every image converted and decoded against the Xbox twin's
gcdrive_check <gc disc> <xbox disc> banks            every bank converted: timbres against the twins, the Xbox ADPCM
                                                     against the DSP decode
gcdrive_check <gc disc> <xbox disc> objects          EAGL objects' classes, relocations and names no object defines
gcdrive_check <gc disc> <xbox disc> sound            PT headers of every stream and bank patch: platform, codec, rate
```

A disc is the folder holding `driving\`. `tools/gcdriving_check.py` (Python, no game, reads both discs' `driving`
folders) decodes the models:

```
python tools/gcdriving_check.py geoprims <gc driving dir> <xbox driving dir> [archive file.crp ...]
python tools/gcdriving_check.py methods <gc driving dir>
```

`geoprims` prints the GeoPrim and vertex counts, how many models' bounds agree with the Xbox twins', the Xbox parent
methods each GameCube method's models use (the mapping table's evidence), the texture coordinates' scale against the
Xbox's and the colours against the Xbox's, matched by vertex position (about 20 s over every CARP file). `methods`
lists the render methods the objects name, the display lists' indices per vertex and their primitive kinds.

## The scoping study

What follows is the study as written before the build (26 October 2026); where the build has since answered a
question, the answer is in the sections above.

### The disc

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

### Format by format

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

### Seams (as proposed; see [Seams](#seams) for the build's)

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

### Models and render methods

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

### Compared with the action engine's GameCube work

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

### Milestones

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

### Research items

The build has answered the render methods' fields, the SHPG types and palettes, the DSP coefficients, Z and the
triggers, and the CARP record types (the sections above).

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

### Tools (at the study)

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
