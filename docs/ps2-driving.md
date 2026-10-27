# The driving engine on the PS2's data

A sibling of [ps2-data.md](ps2-data.md): our driving engine (driving_standalone, docs/driving/standalone.md) loading
the PS2 release's driving files (`SLES-51258`, the European release) in place of the Xbox's, behind a build switch,
with the game code staying the Xbox port. It started as a scoping study (the question: does the driving engine take
the PS2's files as readily as the action engine did?) and became, on the branch `ps2-driving-rm`, a first working
build of the shortcut that study pointed at: **the PS2's models drawn with the Xbox disc's render methods**.

Status (27 October 2026): the switch is in (`NIGHTFIRE_DRIVING_DATA_PS2`, the default build unchanged), and **every
mission's world, cars and props draw from the PS2's geometry and textures with the Xbox's render methods** (mission 6
compared frame for frame with the Xbox build on the test runner: the same scene, see [In game](#in-game)). The HUD,
fonts and menus draw from the PS2's images. The cars use the Xbox's car methods (lit, reflective, damage streams)
and cast the Xbox's shadow. Not done: characters (skinned GeoPrims are left out of the draw lists),
sound (the PS2's banks), movies (`MPCh`), and several methods are drawn with a simpler Xbox method than their twin
(the mapping table). See [What is left](#what-is-left).

**The exception to "nothing from the Xbox disc".** The action engine's PS2 build reads nothing from the Xbox disc.
This build does read two files from it, by agreement: `data\render\eaglrm.o` and `data\render\bondrm.o` from the Xbox
disc's `driving\misc.viv` - the Xbox's render methods (NV2A vertex shaders, register-combiner set-ups and packet
programs, data the D3D9 and SDL GPU backends already run). Everything else comes from the PS2 disc. settings.ini:

```
DiscPath=C:/nightfire-ps2/disc              ; the extracted PS2 disc: DRIVING.ELF, DRIVING\ ...
XboxDiscPath=C:\nightfire-cxbx\disc         ; the Xbox disc: driving\misc.viv (its render methods only)
```

A build that needs no Xbox disc would replace those two files with render methods of our own (the scoping study's
milestone 7); the PS2 GeoPrim conversion below would stay as it is, since it produces Xbox GeoPrims for whichever
methods draw them.

Sources: the PS2 disc as extracted by the extractor project (Q:\nightfire-re, read only:
`iso_extract/ps2_eu_sles-51258`, whose BIGF and RefPack readers, `common/extraction/extract_bigf.py`, the checker
follows); the Xbox disc in `disc/driving` (European, the twin of the PS2's release); `DRIVING.ELF` and its
symbol file `DRIVING\DRIVING.SYM`; Ghidra's `/PS2_EU_51258/DRIVING.ELF`; our driving code and its docs
(docs/driving/eagl.md, filesys.md, core-data.md, sound.md). The extractor has no PS2 driving-format readers beyond
BIGF and RefPack; its driving handler is a stub (`nightfire_platform.py`).

The scoping study's findings follow the build's sections; where the build has since answered a question, the
answer is in the build's sections and the old text says so.

## The switch

A CMake option **`NIGHTFIRE_DRIVING_DATA_PS2`** (default OFF), for driving_standalone and driving_standalone_test.
On, the targets are compiled with `NIGHTFIRE_DRIVING_DATA_PS2=1` and the files of `src/driving/ps2/` are added; a
handful of seams in game files are guarded with `#if` ([Seams](#seams)). Off, nothing compiles differently: checked
with `tools/pe_compare.py` on 27 October 2026, driving_standalone built before the branch and after every commit of
it has identical `.text`, `.data`, `.reloc` and `.rsrc` (`.rdata` differs only in the debug directory's timestamp).
drivinginject never gets it.

```
cmake -B build/ps2 -A Win32 -DPython3_EXECUTABLE=C:/Python314/python.exe -DNIGHTFIRE_DRIVING_DATA_PS2=ON
cmake --build build/ps2 --config Release --target driving_standalone
```

Both engines would be switched together in a PS2 run folder: the PS2 action build hands over to a PS2 driving
build through `psiLaunch.bin` exactly as today, since both sides of the hand-over are our Xbox code. `DiscPath`
names the extracted PS2 disc, whose `DRIVING\` folder sits where the Xbox's `driving\` does. File names are matched
without regard to case by Windows and by the BIGF directory search; a case-sensitive host still needs folding (not
done: the Linux build was not tried with the switch).

## Seams

Each a guarded line or two in the game file; the work in `src/driving/ps2/`.

| Seam | Game file | PS2 code |
|---|---|---|
| A file's name: `.xsh`, `.xfn`, `.mad` are `.ssh`, `.sfn`, `.mpc` on the PS2 disc | platform/FileSys.cpp `FILEDEV_resolveopen` | Ps2Files.cpp `Ps2_FileName` |
| A file just loaded (and unpacked): image containers, fonts, galleries, controller configurations | platform/FileSys.cpp `LoadInternal`, `FILE_loadpackz` | Ps2Files.cpp `Ps2_ConvertFile`; Ps2Shape.cpp |
| The render-method libraries | data/RCARPFile.cpp `LoadEAGLMaterials` | Ps2Files.cpp `Ps2_LoadXboxRenderMethods` (the Xbox disc), Ps2RenderMethods.cpp (the PS2 methods' names) |
| A CARP file's models (tracks, cars, props, weapons), once relocated | data/RCARPFile.cpp `Resolve`, after `loader->Resolve()` | Ps2Model.cpp `Ps2_ConvertModels` |
| A character's models | anim/Model.cpp `LoadObject` | the same (all skinned: taken out of the draw lists) |
| The traffic's car types: `NUM_TRAFFIC_TYPES_PS2` (the PS2 release's key; the Xbox's is `_XBOX`) | game/AIVehicleController.cpp | - |
| The pedestrian types: `NUM_PED_TYPES_PS2` (0 in every PS2 attribute file: the PS2 release has no pedestrians, which is why its archives have no pedestrian models) | game/AIPedestrian.cpp | - |

The PS2's `DRIVING.ELF` reads the same attribute names with `_PS2` where the Xbox's has `_XBOX`
(`AIVehicleController::CreateSimTrafficCars` in Ghidra's DRIVING.ELF: the same seven car names, the same draw).

## The render methods

The Xbox's `eaglrm.o` and `bondrm.o` are loaded from the Xbox disc by `LoadEAGLMaterials`, as the Xbox game loads
them, and stay loaded. The PS2's models name PS2 render methods (`MatBondTexturePacked`...) that no Xbox object
defines; each name (and its microcode's, `<name>0__EAGLMicroCode`) is registered as a variable whose value is its
entry in the mapping table, so that the loader resolves a model's reference to that entry and the conversion can
tell which method a GeoPrim was made for.

For each mapped PS2 method the conversion makes one child of the Xbox method (`RenderMethod::ConstructChild`, as
the game makes children for its materials) and changes its packet stream in one way: each dynamic stream packet
(opcode 11, which copies a CPU array into the dynamic buffer ring every draw) becomes an inline stream packet
(opcode 9, the Xbox models' own form: a stream source over the GeoPrim's array) - the two have the same operands.
The colour stream's variation stride is set to the GeoPrim's own layout; the rest are 0.

## GeoPrims

What `Ps2Model.cpp` reads (and `tools/ps2driving_check.py geoprims` decodes offline):

- A PS2 GeoPrim is `{render method, 0}`. The render method is a PS2 `RenderMethod` record: `+0x00` its compiled
  program, `+0x08` its parent (the PS2 method), `+0x14` its name (`__RenderMethod:::<model>..._name`). The program
  is the parent's packets compiled for this GeoPrim, and it names only what changed since the GeoPrim before it in
  the model's draw list: TARs (a model's first GeoPrim names all of them), the GeoPrimState (the first named is the
  one used: reflective methods name a second, `STATE::name=glossmap`, for their second pass), matrices and
  microcode. So the conversion walks each draw list in order and carries the TARs and the state forward.
- After a `0x04000002` word the program points at the variation table: DMA `CALL` tags, one a variation (a model's
  `variation`, Model +0xc0, picks one: 1 to 44 of them). Each calls a block that unpacks that variation's vertex
  colours (`STMASK 0x40404040`, `STROW 0,0,0,0x80`, `UNPACK V3-8` masked: RGB, alpha 0x80) and calls the geometry,
  or is the geometry itself for methods with one colour set.
- The geometry chain (CNT tags, ending in RET): an `UNPACK V4-32` header at VU address 0 whose x is the vertex
  count; three `UNPACK V4-8` words of strip starts (bytes, three to a vertex - two for `BondCarShadow`, whose VU
  output is position and colour - the list ended by 0); the positions
  (`V3-16` unsigned, value - 0x8000 in 1/256ths, or `V3-32` / `V4-32` floats; the models' scale and offset fields are
  1 and 0, and the bounds of all 467 Paris models with an Xbox twin agree with the Xbox's to 0.01); the texture
  coordinates (`V2-16` unsigned less 0x8000 - in 1/256ths for the world's methods, 1/1024ths for the car methods,
  established from the twins' ranges - or `V2-32` floats); then the method's other fields, in the order its PS2
  template lists them (the parameter names in the PS2's `bondrm.o`: `MatCarTextureDullPacked` has Coordinates,
  STQ, Normals, DmgVec, DmgNrm, ColourSlot, ColourSet): colours (`V3-8` masked), normals (`V3-8` unmasked, signed),
  the damage vector and damage normal (`V4-16`, xyz less 0x8000 in 1/1024ths; the vector's w the damage weight in
  the same units, the normal's w the damage zone as it is) - each checked value for value against the Xbox's car
  streams at the same positions; `MSCNT`. `MatCarDash` has every field as `V4-32` floats (Coordinates, Normals,
  STQ, DmgVec, DmgNrm).
- **Normals**: the PS2's point the other way from what the Xbox methods light by. With them as they are the cars
  came out black with bright reflections; negated, as the Xbox's at frame 1500. (The Xbox's own car normals are not
  the geometric ones either - they correlate with neither the PS2's nor normals computed from the strips - so the
  sign was settled by the frames.)
- Models that share a draw list (`Model[0].00`, `.01` ... of one placement) are converted once; GeoPrims shared
  between lists likewise. Paris: 1,890 model symbols, 497 distinct draw lists, 1,779 distinct GeoPrims (the Xbox's
  twins have 789: the PS2 cuts a primitive into VU1-sized batches), 80,517 vertices against the Xbox's 66,388.

An Xbox GeoPrim is made from each: the positions as `FLOAT3`, the texture coordinates as `SHORT2` (1/256ths, the
Xbox methods' constant register `{0, 1, 2, 1/256}`), one `D3DCOLOR` array per variation (`0xff000000 | rgb`, the
PS2's 0x80 being the Xbox's 0x80: both mean 1.0 in these methods), and 16-bit indices that join the strips into one
strip with repeated indices (a new strip always starting on an even index, so the winding holds) or, for a
GeoPrimState whose primitive type is a list, triangles. The car methods' other streams: normals as `NORMPACKED3`,
damage vectors and normals as `FLOAT3`, damage weights as `FLOAT1`, zones as `SHORT1` (the Xbox car methods'
declaration; `BondCarTextureSwitch` takes `FLOAT2` texture coordinates). Each stream's source is a `StaticVertexBuffer`
and a D3D8 vertex buffer header over the array, registered against it, laid out where opcode 9 reads it (0x10 into
the parameter's data, one 0x30-byte record a variation for the colours). The parameters follow the mapping table;
the draw list's entry is pointed at the new GeoPrim, or set to null (`WalkDrawList` skips nulls) for a method mapped
to none. The arrays are host memory (`calloc`), not the game heap, and are never freed (a level's unload leaks them).

## The mapping table

`Ps2RenderMethods.cpp`. The pairs come from the models both discs have (`ps2driving_check.py geoprims` lists the
Xbox parent methods each PS2 method's models use), and each Xbox method's parameter list from the Xbox's own
GeoPrims of it.

| PS2 method (models) | Xbox method now | The Xbox twin's own method | Parameters / notes |
|---|---|---|---|
| `MatBondTexturePacked` | `BondTexturePacked` | `BondTexturePacked`, `BondRoadPacked` (roads: car shadows projected) | state, TAR 0, stream, indices, MVP/model/model-view/projection, `GAME::FishEyeParams`, `GAME::PositionalLights` x8, `{0,1,2,1/256}`, positions, texture coordinates, colours |
| `MatBondTexture` | `BondTexture` | `BondTexture` | the same (float positions and coordinates on the PS2) |
| `MatBondReflectTexturePacked` | `BondReflectPacked` | `BondReflectPacked`, `BondRoadReflect` | state, TAR 0, `EAGL::EnvironmentMap`, ..., `EAGL::CameraPos`, `{0.4375, 0.5, 5, 0}` (every Paris GeoPrim of it has this), `{0,1,2,1/256}`, streams |
| `MatDynamicTexturePacked` | `BondTexturePacked` | `BondDynTexture` (lit: `GAME::LightSources`, short positions) | drawn unlit, white |
| `MatBondScrollTexture`, `...Packed`, `...2`, `...2Packed`, `...3Packed` | `BondTexturePacked` | `BondScrollTex1/2/3` (two coordinate sets, scroll constants, `GAME::SimStep`) | the first coordinate set, not scrolled |
| `MatBondFrameAnimPacked` | `BondTexturePacked` | `BondFrameAnim` | not animated |
| `MatBondWater`, `MatBondWaterPacked`, `BondUnderWater`, `BondUnderWaterPacked`, `BondKelp`, `BondGouraud` | `BondTexturePacked` | `BondWater`, `BondUnderWater`, `BondUnderWaterPacked`, `BondKelp`, `BondGouraud` | |
| `MatCarTexturePacked` | `BondCarTexture` | the same | state, TAR 0, `GAME::ReflectionMap`, `GAME::SpecularMap`, stream, indices, MVP, model, `GAME::SpecularMat`, `GAME::ReflMatrix`, `GAME::LightSources`, `GAME::CameraPos`, positional lights x8, `EAGL::DamageZones` x16, zeros x8, zeros, `{0,1,2,1/256}`, `{0.5,0.5,0,0}`, `GAME::ReflectionInfo2`, `GAME::CarLightingGlossyProps`, `GAME::ReflectionCarPos`; positions, damage vectors, normals, damage normals, coordinates, damage weights, zones |
| `MatCarChrome` | `BondCarChrome` | the same | as `BondCarTexture` with `GAME::CarLightingChromeProps` |
| `MatCarTextureDullPacked` | `BondCarTextureDull` | the same | state, TAR 0, stream, indices, MVP, model, light sources, positional lights, damage zones, zeros, `{0,1,2,1/256}`, `GAME::CarLightingDullProps`; the seven car streams |
| `MatCarTextureSwitchPacked` | `BondCarTextureSwitch` | the same | as Dull with `{0.5,1,1,1}` and the `GAME::SwitchData[n]` the PS2 program names (the brake and indicator lights); float coordinates |
| `MatCarGlassPacked` | `BondCarGlass` | the same | as `BondCarTexture` with the damage texture (the program's second TAR) and `GAME::CarLightingGlassProps` |
| `MatCarDash` | `BondCarTextureDull` | `BondCarSpecularBump` (tangents, a bump map) | not bump-mapped |
| `MatCarGouraudDullPacked`, `BondCarShadow` | `BondCarShadow` | `BondCarShadow` (the Xbox pairs both PS2 methods' models with it) | state, stream, indices, MVP, model, light sources, `{0,1,2,1/256}`, positions |
| `BondLitTextureAndShadow_Skin`, `BondLitTextureAndShadowSpecular_Skin` | none | the same names | characters: left out |

The 26 are every method the PS2's model objects name (`ps2driving_check.py objects`). The PS2's own `bondrm.o`
templates list each method's parameter names (`MatBondTexturePacked`: Parameters, MyMatrix, WorldMatrix, Texture,
SplitReg1-3, Coordinates, STQ, State, Colours), which is where the VIF layout's meaning was confirmed; Agent Under
Fire's GameCube driving.elf registers its methods with field-name tables of the same kind (`<Method>_FieldNames`),
which could pair the remaining methods by fields rather than by name.

## Textures, fonts, galleries

`Ps2Shape.cpp`, applied to every file as it loads (`Ps2_ConvertFile`):

- **SHPS to SHPX.** The same directory; each image keeps the PS2's 16-byte header, which is the Xbox's too (the
  pixels follow at +0x10 unless flag 0x1000 says otherwise), with the type, the swizzle flag and the mip count
  changed. A gallery's atlas entries (1x1 images) keep their place in the atlas in +0x08 and +0x0c. (A first version
  wrote a 0x14-byte header: every texture shifted by four bytes, which showed as a stray texel column beside each
  glyph of the HUD's digits, as the HUD's sprites cut from the wrong place of their atlas - the radar - and as noise
  on a truck's roof in mission 5.) Palettised
  images (type 1, 4-bit; type 2, 8-bit with the GS's CSM1 palette order, bits 3 and 4 of the index exchanged: checked
  by decoding against the Xbox's twins) whose sides are powers of two become 8-bit (type 0x7b, `P8`) with a
  256-colour palette attachment (0x2a), as the Xbox's own 8-bit images are; everything else becomes 32-bit (0x7d).
  Alpha 0x80 is opaque (doubled, capped at 0xff). Images whose sides are powers of two are **swizzled** with their
  PS2 mip levels kept (flags bits 28..31); the rest stay linear. Swizzling is not optional: the NV2A (and our
  backends, faithfully) addresses a linear texture in texels, so the first build, all linear, drew every model in
  one texel's colour.
- The first build made every image 32-bit and ran out of game heap in mission 5 (snow1a): 8.7 MB free where the Xbox
  data leaves 15.7 MB. With the 8-bit images it leaves 17.5 MB (mission 6, at "Init AI Characters").
- **The cars' shadow** (`common`'s `cshd`): the PS2 subtracts a white shape on black from the ground, the Xbox's
  code multiplies the ground by its own image, a dark grey (0x45) shape on white; the conversion makes the PS2's
  image the Xbox's (255 - value x 0xba / 0xff), which matches the Xbox's twin. Before that the shadow quad under
  each car drew black.
- **Fonts** (`FNTS`, `.sfn`): the header and glyph tables are the Xbox's; the image (at `+0x1c`) is converted as
  above and the magic made `FNTX`.
- **Galleries** (`.gal`, data groups): each `SHPS` item is converted, appended at the end of the file and its record
  pointed at it (size and offset).
- **Controller configurations** (`.def`): the PS2 pad's control names become the Xbox pad's by the buttons' places
  (Cross A, Circle B, Square X, Triangle Y, L1 left trigger, R1 right trigger, L2 white, R2 black, L3/R3 thumbs,
  Select back; the front-end labels likewise). The PS2's bindings are kept (gas is Cross, so A).

## In game

On the test runner (vr-desktop, slot 13 with its own checkout `C:\nightfire-ps2rm`; the PS2 disc at
`C:\nightfire-ps2\disc`), driving_standalone `-mission N`, frames dumped with `DumpEvery`:

- **Mission 6 (snow2a_mis4), frame 1500**, against the default build on the Xbox disc at the same frame: the same
  road, snow banks, fir trees, sky and mountain, the player's Vanquish (plate, lights, interior through the rear
  window), its shadow, the enemy car ahead, the timer and weapon text, the radar. Differences: the PS2's Vanquish is
  a little lighter and more reflective than the Xbox's; the timer reads 4:32 against the Xbox's 4:47 at the same
  frame (the PS2 build has no opening movie, so its timer starts earlier); the traffic car differs (a random pick).
  The first build's frame showed the faults since fixed: a black chequered block under the car, a stray texel
  column beside each digit, the radar broken up, the car unlit with a blank plate.
- **Mission 1 (paris_mis01)**: the opening in-engine scene inside the van (seat, driver, door, a truck and a car on
  the road), then Paris at night from the van: the river embankment, the bridge with its gilded statues and lamps,
  building fronts with lit windows, the road markings; the mission-failed screen when nothing is done. The PS2
  build has no opening movie (no `MPCh` player), so it reaches the scene the Xbox reaches after its movie.
- **Missions 2, 3, 4, 5, 7, 8**: each loads and draws: the underwater approach with the submersible car (2), the
  jungle track with the armoured car (3), the jungle hillside from the red car (4), the snow village's buildings,
  cable car and lamps at night (5: the same view as the Xbox at frame 1500, the truck's roof included, less the
  characters and the weapon), the
  dock with its crane and waterfall (7), the snow race with the red Cobra and traffic (8). No faults in any.

## What is left

- **Characters**: their GeoPrims (`BondLitTextureAndShadow_Skin`, `_Specular_Skin`) are taken out of the draw lists.
  They need the skinned layout (bone indices and weights, the matrix palette) converted to the Xbox skinned methods'
  streams; the skeletons and animation are the Xbox's already.
- **The methods drawn with a simpler Xbox method** (table above): lit dynamic objects, scrolling and animated
  textures, water, the dashboards' bump mapping. Each needs its extra fields found in the VIF layout (the PS2
  templates name them) and its Xbox parameters filled in. Damage deformation has its streams but was not tried in
  game.
- **Roads**: the Xbox draws roads with `BondRoad*` methods (they also take the shadow map); the PS2's road models use
  the plain methods. The cars' own shadow quads draw as the Xbox's.
- In mission 1's sniping section the van's interior is in view where the Xbox build shows the street and the rifle.
  Not investigated.
- **Sound, movies, the PS2's speech languages**: as the scoping study (milestones 3, 4).
- Converted GeoPrims are never freed; the Linux build and case-sensitive file names are untried.

## Is the shortcut viable for the whole game?

Yes, for the world: the conversion is generic (one VIF layout family for every static method, the texture and
colour layouts shared), the bounds of every Paris model with an Xbox twin agree, and all eight missions draw. The
Xbox methods accept the converted GeoPrims unchanged apart from one opcode swap. What remains is per method and
mechanical (more streams, more constants) rather than research, except the skinned characters. The cost is the
exception itself: the build needs both discs. Replacing `eaglrm.o`/`bondrm.o` with render methods of our own later
would not change the conversion.

## The scoping study

What follows is the study as written before the build (26 October 2026), with notes where the build has
answered it.

### The disc

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

### Format by format

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

### Milestones

| # | Milestone | Needs | Size |
|---|---|---|---|
| 1 | The switch, the extension mapping, case folding; the offline checker (done: `tools/ps2driving_check.py`) - **done but case folding** | - | small |
| 2 | Text: controls' button names, tuning keys checked - **button names done**; the attribute keys `_PS2` for `_XBOX` | 1 | small |
| 3 | Sound: stream headers (the render mode), bank samples converted | 1 | medium |
| 4 | Movies from `MPCh` | 1 | small |
| 5 | Textures: SHPS to SHPX (palettes, the swapped images), fonts, galleries; the front end and HUD drawn - **done** (the swapped images look right in game) | 1 | small-medium |
| 6 | **Done for the static methods** ([GeoPrims](#geoprims)). Research: the PS2 GeoPrim (the whole DMA chain, each render method's parameter block, the VU memory layout) from DRIVING.ELF's `RenderMethod::Draw`/`Compile` and `bondrm.o`'s templates; checked against the Xbox twins offline (the Xbox's `VertexBuffer`s and render-method children, read by the checker only) | 1 | research |
| 7 | **Replaced, by agreement, by the Xbox disc's render methods** ([the exception](#the-driving-engine-on-the-ps2s-data)). A render-method library of our own for the 26 methods the PS2's models name (and the game's own GeoPrims: effects, sky, HUD), behaving as the Xbox's do | 6 | large |
| 8 | Static models (tracks, cars, props) converted and drawn - **done**, several methods with a simpler Xbox method ([the mapping table](#the-mapping-table)) | 6, 7 | medium-large |
| 9 | Skinned characters (`BondLitTextureAndShadow_Skin`, `_Specular_Skin`): bone palettes; the skeletons and anims are already the same | 8 | medium |
| 10 | In game: every mission on the runner - **all eight load and draw**; what the PS2 data lacks (pedestrians: the PS2 has none by design, `NUM_PED_TYPES_PS2=0`; Xbox-only effect images) | 2-9 | medium |

Milestones 1-5 are of the action engine's size or smaller and could be done in a few sessions; they would give
sound, movies, menus and the loading screens, but no world. Milestones 6-9 are the bulk.

### How it compares with the action engine's PS2 work

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

### Research items

- **PS2 GeoPrims** (answered for the static methods: [GeoPrims](#geoprims)). A first census of each GeoPrim's first packet (a scratch script, not yet in the checker; 2,211 GeoPrims in
  `paris_mis01.crp`, `vanquish.crp` and `c_bondc1.dat`: V4-8 7,009, V4-32 3,107, V2-16 2,037, V3-16 1,719, V3-8 996,
  V4-16 724 unpacks, 2,208 MSCAL/MSCNT); the rest of each chain and the render-method record (`GeoPrim+0` -> a
  record whose +8 names the method) are next. Ghidra's DRIVING.ELF has the PS2 EAGL code (DRIVING.SYM names
  `EAGL::RenderMethod::Draw(EAGL::GeoPrim *)`, `Compile`, `Optimize`, `Release`, and `paulvif.o`, `dmamgr.o`,
  `singledraw.o` in libeagla.a), to be matched by hand since the symbol file is of another link.
- **Which render methods the game itself names.** The game builds GeoPrims of its own for effects, the sky,
  decals and the HUD against render methods by name (eagl.md 2.8); the Xbox code names Xbox methods. Each needs
  an implementation in the PS2 build whatever the models use.
- **The pedestrians** (answered): every PS2 attribute file sets `NUM_PED_TYPES_PS2=0`, the key the PS2's executable
  reads where the Xbox's reads `NUM_PED_TYPES_XBOX`; the PS2 release has no pedestrians, so its archives need none.
- **Xbox-only images and keys.** 356 images the Xbox has and the PS2 lacks (car specular maps, effect images such
  as `common.ssh`'s), the `SpherifyNormals` keys: which the Xbox code asks for by name, and what happens then.
- **The swapped textures** (no sign of trouble in game: the PS2's images are drawn as they are). 71 images have width and height swapped against the Xbox's; check the PS2 models' UVs
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
python tools/ps2driving_check.py geoprims <PS2 dir> <Xbox dir> mis01.viv data\track\paris_mis01.crp ...
                                                                                            PS2 GeoPrims against the twins
python tools/ps2driving_check.py objects <PS2 dir> <Xbox dir>                               EAGL classes, render methods
python tools/ps2driving_check.py shapes <PS2 dir> <Xbox dir>                                texture types and sizes
python tools/ps2driving_check.py banks <PS2 dir> <Xbox dir>                                 sound bank samples
python tools/ps2driving_check.py streams <PS2 dir> <Xbox dir>                               .asf blocks and headers
python tools/ps2driving_check.py syms <DRIVING.SYM> [regex]                                 the PS2 symbol file
```

`compare` takes about four minutes over both discs (Python RefPack); the others less.

`geoprims` decodes as `src/driving/ps2/Ps2Model.cpp` does and prints, per CARP file, the counts (models, distinct
draw lists, GeoPrims, vertices, triangles, variations), how many models' bounds agree with the Xbox twin's, the Xbox
parent methods each PS2 method's models use (the mapping table's evidence) and the 16-bit texture coordinates' range
by method (the car methods' 0-1024).
