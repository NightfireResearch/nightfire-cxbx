# EA's graphics library in the driving engine (EAGL, EAGLAnim, realgraph FONT/SHAPE/LOCALE)

A map of EAGL as linked into `Driving.xbe`, written to plan replacing it. Every address is Driving.xbe's (Ghidra
program `/Xbox_EU/Driving.xbe`). Names are Ghidra's unless marked *(invented)* or *(candidate)*; *PS2: X* means the
name comes from the PS2 build's symbols (`/PS2_EU_51258/DRIVING.ELF` and the PS2 symbol sheet), which has the same
API with a PS2 implementation (DMA chains, VU1 microcode, GS texture memory), so only names, signatures and intent
cross over. *MW: X* means a name or layout from the Need for Speed Most Wanted (2005) decompilation
(`src/Speed/Indep/Src/EAGL4Anim/`, `symbols/PS2/`), which carries EAGL4Anim and realgraph about three years newer
than this build; every MW item used here was checked against Driving.xbe, and section 1.4 says how far each one
holds.

**Status (3 October 2026): steps 0-3 of 9.2 done.** The classification fixes of 1.3 are in
`tools/subsystems_driving.txt`; realgraph FONT/SHAPE/LOCALE is ported (`src/driving/eagl/Realgraph.cpp`, checked
by `src/driving/devtools/RealgraphShadow.cpp` on every font, image container and string table in the archives:
606,458 comparisons, all the same), and so is `EAGL::Transform` (`src/driving/eagl/Transform.cpp`, bit for bit,
checked by `MathShadow.cpp`). Found on the way: `FONT_getrectx` keeps the kerned pen position unrounded on the
x87 through the glyph's rectangle and advance (Ghidra's decompilation rounds it); a character below 0x20 makes
FONT index 0x20 glyphs before the table; SHAPE's loader would read `SHAPE_version(NULL)` for a file that is not
SHPX (left out); `Transform::Invert` returns the determinant to its callers, as the maths library's does;
`0x000f13b0` is the same code as `Determinant4x4`, and `0x000f3160` the same as `0x00115440`. The loader is ported
too (`src/driving/eagl/Loader.cpp`: SymbolPool, both ConstructorPools, DynamicLoader, RegisterShapes, 41
functions), checked by `src/driving/devtools/LoaderShadow.cpp` - every object in the archives loaded by both on the
same addresses, the images, symbol tables, allocations, messages and callbacks compared byte for byte, the same.
What the data never does is in 3.1. The D3D8 library underneath is ours already
(the seam, `src/driving/gfx/`), and so are the maths, files, memory and threads EAGL calls (section 5).

Conventions:

- "thiscall" is MSVC's: `this` in ECX, arguments on the stack, callee pops. "cdecl" is arguments on the stack,
  caller pops. Register arguments are named where they occur (section 7.1); they were checked in the disassembly.
- Sizes are bytes to the next Ghidra function, so they include alignment padding and any code Ghidra left outside
  a function (section 7.3). The coverage tool counts the same way.
- "Live" is `tools/function_coverage.py`'s: reachable from a root, where every function nothing references counts
  as a root. "Strictly live" (section 6.3) drops that last assumption for EAGL's own functions.
- Port classes **I/D/S/M/A** are `docs/driving/maths.md` section 3.3's (integer, x87 double-reproducible, SSE,
  mixed, assembly-only).

Contents: 1 overview, 2 data layouts and globals, 3 how it works (loading, a frame, the render-method machine,
animation), 4 module reference, 5 the interface downward, 6 the interface to the game and what is dead, 7 patching
hazards, 8 quirks a port must decide on, 9 port structure, order and tests.

---------------------------------------------------------------------------------------------------------------

## 1. Overview

### 1.1 What it is

Three libraries that the coverage tool files together as `platform.eagl`:

- **EAGL** (EA Graphics Library, Xbox back end): device, render contexts, viewports, transforms, textures ("TAR"),
  render states ("GeoPrimState"), models, a render-method interpreter, the ELF "dynamic loader" that every model,
  skeleton, animation bank and the render-method library are loaded through, a profiler, and an immediate-mode
  font driver. Strings: `EAGL::Device new`, `EAGL::ViewPort::gpModelViewProjectionMatrix`.
- **EAGLAnim**: the animation runtime - FnAnim channel objects (raw, keyed, delta-compressed, blenders, mirror,
  phase, compound), its memory pool, event resolution, and a Skeleton/BoneMask helper set. EAGLAnim uses graphics
  EAGL only for its allocator, `EAGL::Transform`, the symbol pool and the exception funclets (section 1.2).
- **realgraph** FONT, SHAPE and LOCALE: EA's portable font (`FNTX`), image container (`SHPX`) and string-table
  (`LOCH`) code. FONT draws through a driver table that EAGL fills (`EAGLFont`).

The binary is laid out by source file, so modules are address ranges. Section 4 has the detail; this is the shape:

| module | range(s) | funcs | KB | named (PS2-evidenced) | dead now | live only as unreferenced | strictly live (KB) |
|---|---|---|---|---|---|---|---|
| A loader: DynamicLoader, SymbolPool, ConstructorPools | `0x000e51d0..0x000e6610`, `0x000ede60..0x000ee010`, `0x000f39c0..0x000f42b0` | 42 | 7.7 | 36 (14) | 0 | 2 (0.1) | 40 (7.7) |
| B device, render contexts, viewports | `0x000e4340..0x000e51d0`, `0x000e6610..0x000e8b40`, `0x000ee010..0x000ee490`, `0x000f3450..0x000f39c0` | 104 | 15.4 | 59 (49) | 0 | 13 (0.9) | 91 (14.5) |
| C Model, DynamicModel | `0x000e8b40..0x000eb220`, `0x000f0e40` | 52 | 9.7 | 32 (26) | 0 | 37 (7.0) | 15 (2.7) |
| D textures (TAR) and their property parser | `0x000eb220..0x000ede60` | 30 | 11.0 | 9 (6) | 0 | 2 (0.6) | 28 (10.5) |
| E GeoPrimState and its property parser | `0x000eec70..0x000f0e40` | 33 | 8.5 | 9 (5) | 0 | 1 (0.0) | 32 (8.4) |
| F render methods, vertex buffers, shaders, the interpreter, DrawArray | `0x000f0e50..0x000f1500`, `0x000f4340..0x000f4580`, `0x000f55a0..0x000f70b0`, `0x0014cf10` | 99 | 9.2 | 26 (18) | 5 | 17 (1.7) | 77 (6.9) |
| G `EAGL::Transform` | `0x000f1500..0x000f3450`, `0x000f8780`, `0x000160e0` | 31 | 8.3 | 28 (28) | 0 | 20 (4.5) | 11 (3.8) |
| H EAGLFont (the FONT driver) | `0x000ee490..0x000eec70` | 4 | 2.0 | 2 (0) | 0 | 0 | 4 (2.0) |
| I PrintMessage, profiler | `0x000f42b0..0x000f4340`, `0x000f4580..0x000f55a0` | 20 | 4.2 | 6 (5) | 0 | 6 (0.4) | 14 (3.8) |
| J EAGLAnim objects: ctors, vtable stubs, factory, pool, events | `0x000f70b0..0x000f8780`, `0x00014210` | 79 | 5.7 | 57 (47) | 0 | 3 (0.3) | 76 (5.5) |
| K EAGLAnim Skeleton and BoneMask *(candidate)* | `0x000f88a0..0x000fa9c0` | 21 | 8.3 | 0 | 0 | 13 (3.1) | 8 (5.2) |
| L EAGLAnim evaluation and decompression | `0x000fa9c0..0x001073a0` | 203 | 50.5 | 144 (124) | 0 | 27 (3.8) | 176 (46.7) |
| M realgraph FONT, LOCALE, SHAPE | `0x001073a0..0x00108460`, `0x0014a370..0x0014a4f0` | 32 | 4.6 | 26 (15) | 0 | 0 | 32 (4.6) |
| N exception funclets | `0x00154c30..0x001552a8` | 52 | 0.7 | 52 | 1 | 34 (0.4) | 17 (0.3) |
| allocator stubs, game-side callbacks | `0x000e4f50`, `0x000e4f60`; `0x0007aea0`, `0x0007d040`, `0x000c5700` | 5 | 0.1 | 5 | 0 | 0 | 5 (0.1) |
| **total** | | **807** | **145.8** | **491 (340)** | **6** | **175 (22.6)** | **626 (122.6)** |

807 is the coverage tool's 805 plus two functions it misfiles (1.3). "PS2-evidenced" counts functions whose Ghidra
plate carries a `[symbol-matching]` block (Version Tracking reviewed by hand, paired vtables, windowed review).
The remaining Ghidra names came from RTTI, strings and the original analysis.

Measured live/dead: the coverage tool says 799 live and 6 dead. 175 more (22.6 KB) are live only because nothing
references them and the tool treats such a function as a root; section 6.3 lists them. They are mostly unused
public API (Model::GetChild, the DynamicModel/DrawArray vertex-array path, most of Transform, Skeleton helpers).

### 1.2 How the parts relate

```
 game / engine (R*, G*, Act*, RCARPFile, USimple*Material ...)
   |  Device/RenderContext/ViewPort/TextureRenderContext calls, GeoPrimState setters, Model::Draw,
   |  DynamicModel, TAR, DynamicLoader, FONT_*, SHAPE_*, LOCALE_getstr, FnAnim virtuals
   v
 B view/context ---- E GeoPrimState ---- D TAR ---- M SHAPE (pixels)
   |                     ^                 ^
   |                     |                 |
 C Model --> F render-method interpreter (opcode table 0x001ce700) --> D3D8 entry points (our seam)
   ^              ^                                               \--> D3D8's own state tables (direct writes)
   |              |
 A DynamicLoader: ELF objects (.o, .dat+.rel) --> ConstructorPool / RuntimeAllocConstructorPool callbacks
   (TAR, RenderMethod, Model, VertexBuffer, GeoPrimState, TAR-from-properties, EAGLAnim AnimBank)
 G Transform: used by B, C, F, K, L
 H EAGLFont <- M FONT (driver table 0x001cd110, installed by the game)
 J/K/L EAGLAnim: alloc + Transform + SymbolPool only
```

Measured module-to-module call counts (direct calls only): Model -> DrawArray 24, -> Transform 6; DrawArray ->
D3D wrappers 26, -> GeoPrimState 15; EAGLFont -> GeoPrimState 12; Profiler -> RenderContext 6, -> ViewPort 4;
anim eval -> anim objects 16, -> skeleton 10, -> Transform 5; skeleton -> anim eval 16; TAR -> SHAPE 4,
-> SymbolPool 3; loader -> SymbolPool 8.

### 1.3 Classification fixes for `tools/subsystems_driving.txt`

- `0x000e4340` (832 bytes, filed `engine.data` as `FUN_000e4340`) is **ViewPort::SetShape** *(PS2: ViewPort::
  SetShape(float x6))*: our `RenderState.hpp` already calls it as `ViewPort::SetRect`. EAGL's range should start
  there, not at `0x000e4680`.
- `0x0014a370` (224 bytes, filed `platform.sound`) is SHAPE's file loader (`EAX` = file name, appends `.xsh`,
  `FILE_loadpackz`, checks `SHPX`); it belongs with SHAPE.
- `0x0007aea0 EAGLNamespace::NameLookup`, `0x0007d040 EAGL_allocator` and `0x000c5700 EAGL_deallocator` are the
  game's callbacks into EAGL (a resolver for the loader and the allocator overrides), not EAGL. They are counted in
  the table under "game-side callbacks".
- `0x000eea10 D3D8::D3DDevice_End` sits inside EAGL's range: a copy of the XDK's inline function compiled into the
  font unit. The seam already patches it (`d3d8Entries.inc`).
- `0x0014cf10 EAGL::DrawTextured::Begin` sits in the movie player's range and is dead (6.3).

### 1.4 What the MW (EAGL4) decompilation confirmed, and how firmly

| item | MW source | checked against Driving.xbe | confidence |
|---|---|---|---|
| FnAnim vtable order | `FnAnim.h` (16 virtuals) | Xbox has 15: the same order without `EvalPose`. Checked slot by slot on FnCompoundChannel's vtable `0x001a0be8` | firm |
| FnAnim fields | `mStat` +4, `mType` +8 | ctors write +8; type ids below | firm |
| AnimTypeId values | `AnimTypeId.h` | compound 0xf, raw event 1, raw linear 2, cycle 3, event blender 4, graft 5, DeltaQ 0x11, DeltaF3 0x14, DeltaF1 0x15 all match the Xbox ctors and the factory `FUN_000f7de0`. Nightfire has no ids above 21 | firm for 0..21 |
| FnAnimMemoryMap | 0x10, `mpAnim` +0xc | matches; `FnDeltaQ::SetAnimMemoryMap` writes +0xc | firm |
| FnRunBlender layout | 0x80, fields listed in `FnRunBlender.h` | size and Ghidra's struct offsets agree (+0x58 align quaternion, +0x68 init flag, +0x7c speeds) | good |
| FnDeltaQ layout | 0x30, `mMinRanges` +0x10, `mBins` +0x14, `mBinSize` +0x18, `mPrevKey` +0x1c ... | size matches; the Xbox ctor sets both +0x18 and +0x1c to -1, so +0x18 is not MW's `mBinSize` here | size only |
| DeltaCompressedData | `DecompressValues` (4/8/16-bit deltas, `DofInfo{min,range,start}`) | same algorithm and data layout at `0x001069d0`; **the arithmetic order differs** (8.6) | algorithm firm, not bit-exact |
| realgraph `Font` header | `struct Font` 0x80, `Glyph` 0x10 | FNTX files on disc: `FNTX`, size, version 0x0135, count, flags at the same offsets | header firm, rest unchecked |
| realgraph `ShapeFile` header | signature, size, count, header size | SHPX files: magic, size, count, 4-char directory id (`G344`), then 8-byte entries | firm |

MW has no EAGL graphics code (no GeoPrimState, TAR, Model, RenderMethod), so modules A-I come from the Xbox binary
and the PS2 names only.

---------------------------------------------------------------------------------------------------------------

## 2. Data layouts and globals

Layouts are the Xbox's. Ghidra has structs for several (`/EAGL/*`, `/EAGLAnim/*`, from `tools/structs_driving.json`);
where its field names are wrong that is said. `?` marks a field seen but not understood.

### 2.1 Device (0x1c) *(PS2: EAGL::Device)*

| off | meaning |
|---|---|
| +0x00 | self |
| +0x04 | 0 |
| +0x10 | RenderContext list head |
| +0x14 | TextureRenderContext list head |
| +0x18 | bool initialised |

One instance, in RRenderer. `Device::Init` (`0x000e4f70`) sets `gpCurrentDevice`, registers the four load-time
types (`EAGL::TAR`, `RenderMethod`, `Model`, `VertexBuffer`) in the ConstructorPool and the two property-built types
(`EAGL::GeoPrimState`, `EAGL::TAR`) in the RuntimeAllocConstructorPool, registers the six viewport matrices
(`SymbolInit`) and five `EAGL::RenderMethodConstants::g*` vectors (`0x001cdab0..0x001cdaf0`) by name, and stores
`D3D_ReturnsTrue(0)` at `0x0023ff14`.

### 2.2 RenderContext (0x14c) *(PS2: EAGL::RenderContext; Ghidra struct mostly right)*

| off | meaning |
|---|---|
| +0x00 | RenderContextExtension* (its first word points back; the extension's setters dereference it) |
| +0x04 | RenderContextPrivate* |
| +0x08, +0x0c | back buffer and Z formats |
| +0x10 | multisample type (0x11 = none: EndFrame skips its state restore) |
| +0x14, +0x18 | width, height (SetSize, as floats) |
| +0x1c..+0x24 | front/back/Z buffer depths |
| +0x2c | bool sync to VBL |
| +0x30..+0x79 | the extension's render-state block: +0x30.. stencil, +0x58 stencil enable, +0x59 Z writes, +0x5c colour write mask, +0x61 fog enable, +0x64..+0x70 fog start/end/density/table mode, +0x74 fog colour, +0x78/+0x79 two bytes |
| +0x80, +0x84 | push buffer size and kick-off size (D3D_SetPushBufferSize) |
| +0x90 | ? (written by an undefined setter at `0x000e7ee0`) |
| +0xbc | D3DPRESENT_PARAMETERS (0x40) |
| +0x110, +0x114, +0x118 | back buffer, render target, depth surface (D3D8 surface headers) |
| +0x120..+0x128 | optional surface aliases refreshed every EndFrame |
| +0x138 | current ViewPort, +0x13c ViewPort list, +0x140 next RenderContext, +0x148 Device |

The game keeps RRenderer's at `RRenderer::fgRenderer + 0x64` (our `src/driving/render/RenderState.hpp`).

### 2.3 TextureRenderContext (0xd4)

Render-to-texture context: +0x10/+0x14 width and height (floats), +0x24 current ViewPort, +0x28 ViewPort list,
+0x34 the target texture, +0x38 its depth surface, +0xd0 Device. Six engine.render callers (shadow maps,
offscreen buffers).

### 2.4 ViewPort (0x1a0) *(PS2: EAGL::ViewPort)*

| off | meaning |
|---|---|
| +0x04 | previous ViewPort (0 none, 1 "nothing to restore") - views nest |
| +0x08 | enable model sphere cull |
| +0x10 | D3DVIEWPORT8 (X, Y, W, H, MinZ, MaxZ), handed to `D3DDevice_SetViewport` |
| +0x28, +0x2c | RenderContext or TextureRenderContext |
| +0x30 | projection type (0 perspective) |
| +0x34 | background colour (ClearViewPort) |
| +0x40, +0x80, +0xc0 | projection, view, view-projection matrices |
| +0x118 | shape: six floats (SetShape) |
| +0x130..+0x13c | perspective parameters: fov (degrees), aspect, near, far |
| +0x140..+0x14c | projection scale and offset applied after the D3DX build |
| +0x158..+0x174 | four frustum planes as (tan, sin) pairs, for IsSphereInView |
| +0x178..+0x184 | guard-band factors (SetGuardBandScale) |
| +0x188 | bool view active ("dirty" in Ghidra) |
| +0x18c | next ViewPort, +0x190 linked ViewPort (SetViewMatrix re-begins it) |

### 2.5 GeoPrimState (0x4c) *(PS2: EAGL::GeoPrimState)*

The per-primitive render state. The game derives its materials from it (`UVolatileMaterial`,
`USimpleTexturedMaterial` and others construct it with `GeoPrimState::GeoPrimState` `0x000ef480` and destroy it with
`0x000ef490`, *PS2: ~GeoPrimState*, accepted in review). Setters are thiscall one-store functions:

| off | setter | meaning |
|---|---|---|
| +0x00 | `0x000eec70` SetPrimitiveType | getter `0x000eec80` (accepted, *PS2: GetPrimitiveType*) |
| +0x04 | `0x000eec90` SetTransparencyMethod | |
| +0x08 | `0x000eecb0` | byte |
| +0x0c | `0x000eef20` | |
| +0x10 | `0x000eecd0` SetDepthTestMethod | OpenGL numbering (0x201 LESS, 0x207 ALWAYS) |
| +0x14 | `0x000eecf0` *(candidate: SetAlphaBlendMode)* | 0..5 picks +0x30/+0x34/+0x38 = op/src/dst in OpenGL numbering (0x8006 ADD, 0x800b REVERSE_SUBTRACT; 0x302/0x303/0x306/1/0) |
| +0x18 | `0x000eedc0` | byte (28 callers) |
| +0x1c | `0x000eede0` | |
| +0x20 | `0x000eee00` SetAlphaTestMethod | |
| +0x24 | `0x000eee20` | byte |
| +0x28 | `0x000eee60` | |
| +0x2c | `0x000eef40` | |
| +0x30..+0x38 | `0x000eef80` | blend op, src, dst directly |
| +0x3c, +0x40 | `0x000eefe0`, `0x000eefc0` | |
| +0x48 | `0x000ef020` | byte flag |

`FUN_000ef050` (1072 bytes) applies a GeoPrimState to the device: it compares each field with a cache at
`0x001cd18c..0x001cd1d4` and, for each change, calls `D3DDevice_SetRenderState_Simple` and writes D3D8's render-state
table directly (5.2). It is reached from render-method opcode 16 and from the font driver.

### 2.6 TAR (0x4c) *(PS2: EAGL::TAR, "texture attribute record")*

A texture binding: +0x00 0, +0x04..+0x10 address modes U/V/W and a fourth (1 wrap, 3 clamp in the constructor's
mapping), +0x14 filter, +0x20 byte, +0x28/+0x34 1.0f (scales), +0x40 pointer to the texture record (whose +0x1c is
the SHAPE: `TAR::GetShape`), +0x48 self (the extension pointer). Built from a SHAPE by `TAR::TAR` (`0x000eca20`)
-> `FUN_000ec000`; the D3D texture is made and committed by `FUN_000eba80` (the commit our seam patches, 7.5) and
bound by `FUN_000eb3f0` *(candidate: TAR::Use)*, which also sets palettes, YUV enable and bump-env state and writes
texture-stage state 0 directly into D3D8's table. `TAR::SwapShape` / `SwapClut` replace the image or palette (car
colour variations, the pause-menu girl).

### 2.7 Model (0xd8) and DynamicModel (0x58) *(PS2: EAGL::Model, EAGL::DynamicModel)*

Model is built by the loader from a `Model` symbol (2.10). Fields used by `Model::Draw` (`0x000ea210`), with
Ghidra's names corrected:

| off | meaning |
|---|---|
| +0x00 | name |
| +0x0c | model matrix |
| +0x4c, +0x5c | scale and offset (COORD4 each), loaded into the shader constants by `ModelSetScale` |
| +0x8c, +0x98 | bounding sphere centre and radius |
| +0xa4 | list of child models drawn with the parent's matrix (Ghidra: numChildren) |
| +0xa8 | list of hierarchy children drawn with their own matrix (Ghidra: children) |
| +0xac | list of children drawn with their matrix times the parent's |
| +0xb0 | next sibling |
| +0xc0 | variation index, copied to `CurrentVariation` (Ghidra: tarList) |
| +0xc4 | per-geometry enable table |
| +0xc8 | on |
| +0xcc | the optimised draw list: entries `{0xffff, GeoPrim*}` and skippable blocks (Ghidra: optimized) |
| +0xd0 | patch (variation) applied first by `Model::Patc` (`0x000e9510`, name cut short: *PS2: Model::Patch*) |

DynamicModel: +0x00 model matrix, +0x44 capacity, +0x48 count, +0x4c GeoPrim* array, +0x50 DrawArray* array (only
filled by the dead `DynamicModel::SetPrimitiveType` path). The game constructs, `AddGeoPrim`s, `Draw`s and destroys
them (effects, decals, sky).

### 2.8 GeoPrim and RenderMethod (0x34) *(PS2: EAGL::RenderMethod, EAGL::GeoPrim)*

A **GeoPrim** is `{RenderMethod* +0x00; parameter block +0x04...}`: the parameter block is what the render method's
packets read, eight bytes per parameter (`+0` a count or name, `+4` a pointer to the data, indexed by
`CurrentVariation * stride`). Models' GeoPrims come from the loaded files; the game builds its own for its materials
(`FUN_0011be40` and its kin in `engine.render`: a GeoPrim whose +0x18 is the address of
`gpModelViewProjectionMatrix`, from `GetRegisteredVar`).

A **RenderMethod**:

| off | meaning |
|---|---|
| +0x00 | packet stream (an owned copy when cloned: its length in dwords is at [-4]) |
| +0x04 | number of shader variants |
| +0x08 | vertex declaration |
| +0x0c, +0x10 | vertex shader microcode table, created handles (`EAGL::VertexShader`) |
| +0x14, +0x18 | pixel shader (register combiner) definitions, created handles |
| +0x1c | next in the list at `0x0023ff24` (render methods whose shaders are not created yet) |
| +0x24 | ? (selects which pixel-shader half is created) |
| +0x28 | 1 when cloned |
| +0x2c | parent render method (`__RenderMethod:::ParentRM_*` in `eaglrm.o`) |

`FUN_000f1270` (thiscall, 7 callers in render/effects/camera) makes a render method that inherits from a parent;
`FUN_000f0e50` copies the parent's packets; `FUN_000f1050` creates the shaders; `RenderMethodConstructor`
(`0x000f11c0`) is the loader's callback.

Packets: a header dword `opcode << 16 | length in dwords`, then operands; a zero header ends the stream. Section 3.3
lists the opcodes.

### 2.9 VertexBuffer and DrawArray

The loader's `VertexBuffer` object: +0x00 the D3D8 vertex buffer header, +0x10 the data's offset in the file;
`VertexBufferConstructor` (`0x000f0ee0`) registers it against the ELF image base (`D3DResource_Register` adds the
base, the bug the backend once had) and sets `0x00240814`. EAGL's dynamic vertex buffer is three D3D buffers behind
one object, rotated per lock (`FUN_000f6d00`), filled per draw by opcode 11 (`FUN_000f6890`).

DrawArray (0x4c, Ghidra struct right): CPU-side vertex arrays drawn through a DynamicModel. Only the dead
`DynamicModel::SetPrimitiveType` creates them, so in this game the DrawArray path is dead apart from the class's
static pieces (6.3).

### 2.10 DynamicLoader (0x1c) and the symbol tables *(PS2: EAGL::DynamicLoader)*

| off | meaning |
|---|---|
| +0x00 | symbol table (0x428 bytes, `EAGL::HashPointer`: a 256-bucket ELF-hash table over the object's symbols, linked into the list at `0x0023fb88`; +0x424 an optional resolver callback the game installs, `EAGLNamespace::NameLookup` from `RCARPFile::Resolve`) |
| +0x04, +0x08 | constructor and destructor records run for this object |
| +0x10, +0x14 | the ELF image and its size |
| +0x18 | the second half of a split object (`.rel` after `.dat`): offsets past +0x14 resolve into it (on this disc every `.rel` is a byte-for-byte copy of its `.dat`, so nothing ever resolves into it) |

`gSymbolPool` (`0x0023fb8c`) is a global name -> address pool: `RegisterVar`/`UnRegisterVar`/`GetRegisteredVar`
(EAGL's matrices, the game's globals registered by `ActManager` and `ActSkeleton`) and `RegisterShapes` (every image
of a SHPX file under `"shape" + name`).

### 2.11 EAGLAnim objects

| class | size | notes |
|---|---|---|
| FnAnim | 0xc | vptr, +4 stat, +8 type id (written as a dword) |
| FnAnimMemoryMap | 0x10 | +0xc the anim data ("memory map") |
| FnCompoundChannel | 0x18 | +0x10 sub-channel array, +0x14 use-FPS flag, +0x15 fps |
| FnDeltaF1, FnDeltaF3 | 0x30 | key indices, two decode buffers, dequantisation parameters (Ghidra's struct) |
| FnDeltaQ | 0x30 | see 1.4 |
| FnTurnBlender | 0x5c | Ghidra's struct |
| FnRunBlender | 0x80 | Ghidra's struct, agrees with MW |

Vtables (`0x001a0be8..0x001a1540`, 4-byte MSVC entries, no header; 15 slots for FnAnim, 18-19 with the memory-map
methods): FnCompoundChannel `0x001a0be8`, FnAnim `0x001a0c6c`, FnRawPoseChannel `0x001a0ca8`, FnRawEventChannel
`0x001a0cf0`, FnRawLinearChannel `0x001a0d38`, FnKeyDeltaChan `0x001a0d80`, FnKeyLerpChan `0x001a0dc8`,
FnKeyQuatChan `0x001a0e10`, FnDeltaChan `0x001a0e58`, the factory's inline classes (cycle, event blender, graft)
`0x001a0ea0` and `0x001a0f54`, FnPoseBlender `0x001a0edc`, FnPoseMirror `0x001a0f18`, FnPhaseChan `0x001a0fd0`,
FnRawStateChan `0x001a1018`, FnDeltaLerpChan `0x001a1060`, FnDeltaQuatChan `0x001a10a8`, FnAnimMemoryMap
`0x001a1130`, FnDeltaF1 `0x001a1270`, FnDeltaF3 `0x001a12e8`, FnDeltaSingleQ `0x001a1360`, FnDeltaQFast
`0x001a13e0`, FnDeltaQ `0x001a1450`, FnTurnBlender `0x001a14b4`, FnRunBlender `0x001a1504`.
FnAnim's slot order: 0 scalar deleting dtor, 1 GetTargetCheckSum, 2 UseFPS, 3 Eval, 4 GetLength, 5 FindMatchTime,
6 EvalSQT, 7 EvalPhase, 8 EvalVel2D, 9 EvalEvent, 10 EvalWeights, 11 EvalState, 12 FindTime, 13 GetPhaseChan,
14 GetAttributes; FnAnimMemoryMap adds 15 SetAnimMemoryMap, 16/17 GetAnimMemoryMap. `0x000f70e0` (return false)
and `0x000f7310` fill the unimplemented slots. `NewFnAnim` calls slot 15 on every new object.

### 2.12 SHAPE, FONT, LOCALE files

- **SHPX** (`.xsh`): `"SHPX"`, total size, image count, 4-char directory id (letter + 3 digits, `SHAPE_version`),
  then 8-byte entries `{4-char name, offset}` from +0x10. `SHAPE_locatez` finds an image by name, `SHAPE_longname`
  reads an optional long-name attachment. Image headers are realgraph's (`SHAPE_type`, `SHAPE_depth`,
  `SHAPE_cluttype`, `SHAPE_rowbytes`).
- **FNTX** (`.xfn`): realgraph `Font` header (MW: signature, size, version 0x0135, glyph count, flags, centre,
  ascent/descent, glyph table, kern table, shape, 24 state words) and 16-byte glyphs. `FONT_getkern` and
  `FONT_bsearch` search the glyph and kern tables.
- **LOCH** (`.loc`): `"LOCH"`, header size, flags, language count, then `LOCI` string tables; `LOCALE_getstr`
  (`0x00107c00`) optionally maps an id through a sorted table (`bsearch`) and returns a pointer into the table.

### 2.13 Globals

| address | name | meaning |
|---|---|---|
| `0x001caf68`, `0x001caf6c` | `EAGLInternal::EAGLMalloc`, `maybeEaglFree` | allocator pointers: default `eagl_alloc`/`eagl_free` (CRT malloc/free); RRenderer installs `EAGL_allocator` (`UMemory::Alloc(size, 0x100, name)`) and `EAGL_deallocator` through `Device::SetNewOverride`/`SetDeleteOverride` |
| `0x001cdab0..0x001cdaf0` | `RenderMethodConstants::g*` | five vec4 constants render methods read |
| `0x001ce700` | render-method opcode table | 37 handlers (3.3) |
| `0x001cd110` | EAGLFont driver table | `FONTEAGL_*` entries (`0x001cd114` draw, `0x001cd118` startdraw, `0x001cd120`, `0x001cd124`, `0x001cd12c`) |
| `0x001cec7c` | multiply pointer | `0x001066f0`, a thunk to `D3DXMatrixMultiply`; nine EAGLAnim callers |
| `0x001cec98` | `FONTcurrentdriver` | set by `FONT_installdriver` |
| `0x001cb938..0x001cb960` | RenderContextExtension state shadows | stencil, Z, colour mask ... re-sent after every Swap |
| `0x001cd18c..0x001cd1d4` | GeoPrimState apply cache | invalidated (-1) by EndFrame |
| `0x001d1874`, `0x001d1878` | SHAPE allocator pointers | `MEM_allocalign`, `MEM_free` |
| `0x0023f950` | `gpViewMatrix` | registered by name; render methods read it |
| `0x0023f990` | `gpModelViewMatrix` | |
| `0x0023f9d0` | `gpModelViewProjectionMatrix` | |
| `0x0023fa10` | `gpProjectionMatrix` | |
| `0x0023fa50` | `gpModelMatrix` (`gGlobalModelMatrix`) | |
| `0x0023fa90` | `gpViewProjectionMatrix` (Ghidra: `gGlobalModelViewProjectionMatrix`, misnamed) | |
| `0x0023fb60`, `0x0023fb64`, `0x0023fb68` | current Device, RenderContext, TextureRenderContext | |
| `0x0023fb88` | loaded objects' symbol tables (list) | |
| `0x0023fb8c` | `DynamicLoader::gSymbolPool` | |
| `0x0023fbe0` | `constructorPool` | (`runtimeAllocConstructorPool` beside it) |
| `0x0023ff10`, `0x0023ff11` | saved fog states | |
| `0x0023ff14` | `D3D_ReturnsTrue(0)` | |
| `0x0023ff18` | device created | gates every direct state write |
| `0x0023ff20`, `0x0023ff24` | vertex-buffer and render-method lists | |
| `0x0023ff60` | `EAGLInternal::CurrentVariation` | |
| `0x0023ff80..0x0023ff8c` | texture bound per stage | |
| `0x002401bc`, `0x002401c0`, `0x002401c4` | interpreter: render method, parameter cursor, packet cursor | |
| `0x002401c8..0x002401d0` | the current stream (opcode 2) | |
| `0x00240470..0x00240478` | primitive type, pixel and vertex shader in use | |
| `0x00240480` | stream sources 0..15 | |
| `0x00240804`, `0x00240808` | skinning source (opcode 31) | |
| `0x00240814` | "a vertex buffer was registered" flag | |
| `0x0024052c`, `0x0024053c` | profiler | |
| `0x002414b0` | FnAnim free lists by type | |
| `0x00241510`, `0x00241518`, `0x00241520` | anim memory pool: base, bump pointer, 256 size-class free lists | |
| `0x00241be0` | the default FONT (`FONT_init`) | |

---------------------------------------------------------------------------------------------------------------

## 3. How it works

### 3.1 Loading: everything is an ELF object

EAGL opens no files of its own except SHAPE's loader (5.4). The game reads a file into memory and hands it to a
`DynamicLoader` (`0x000e62f0` one buffer, `0x000e6330` split `.dat` + `.rel`), whose constructor runs:

1. `Initialize` (`0x000e52a0`): turns every section's file offset into a pointer in place, parses the section
   table (`.symtab`, `.strtab`, `SHT_REL`; anything else is reported through `PrintMessage("dlopen: ...")`),
   rewrites each `__Class:::Name` string as `Name`, a NUL, 0x7f and `Class` (the class is what the pools are
   searched by), and hashes every symbol into a new 256-bucket table (ELF hash, `FUN_000e51d0`, string in EDX).
2. `Resolve` (`0x000e5e80`): applies relocations. The objects are **MIPS** ELF (machine 8), the PS2 tool chain's
   format reused as a data container: types 2 (`R_MIPS_32`), 4 (`R_MIPS_26`), 5/6 (`HI16`/`LO16`) are handled, GP
   and GOT relocations rejected. Undefined symbols are looked up in every other loaded object, then through the
   game's resolver callback, then in `gSymbolPool`; failing those, a symbol `RUNTIME_ALLOC::<properties>` (class
   `<type>`) is *built*: its constructor is found by type in the RuntimeAllocConstructorPool and given the property
   text. A symbol nothing resolves keeps its own value (with a message).
3. `RunConstructors` (`0x000e5d70`): for each symbol whose type is in the ConstructorPool (`EAGL::TAR`,
   `RenderMethod`, `Model`, `VertexBuffer`, and `AnimBank` which the game registers), call its constructor on the
   symbol's data.

Files on the disc that go through it: `data\render\eaglrm.o` (`misc.viv`; 117 KB of `.data`, 351 symbols: 
`__RenderMethod:::ParentRM_*` render methods and their `*__EAGLMicroCodeVS`/`*__EAGLMicroCodePS` shader programs -
data, no CPU code), `data\render\bondrm.o` (the game's render methods), `data\actors\skeleton\*skel.o`, and the
`.dat`/`.rel` pairs for models and animation banks in the mission archives (212 `.dat`, 204 `.rel`, entries usually
refpack-compressed, `10 FB`). CARP files (`.crp`) carry their own EAGL references through `RCARPFile::Resolve` /
`LoadEAGLMaterials`.

What the disc's 278 objects actually use (found porting the loader): only `R_MIPS_32` relocations (37,605 of
them), only section types 0-3 and 9, and **no `RUNTIME_ALLOC::` symbol anywhere** - the string occurs nowhere in
the archives, and in the binary only `Resolve` refers to it. So the RuntimeAllocConstructorPool's constructors (the
property parsers of step 4) are registered but never called with the shipped data; the `.rel` half of a split
object is never reached either. `LoaderShadow.cpp` perturbs its copies of the objects to cover those paths.
Quirks kept: `UnRegisterShapes` looks every name up by the image's directory name padded with spaces, so a name
`RegisterShapes` took from the long name or cut short is never removed; `SymbolPool::Search` walks the whole table
on a miss, and its fallback resolvers can only run when a matched slot empties under it, which cannot happen;
`GetSymbol` returns its record by value (EAX = the output); the original `Search` relies on `HashFunction` leaving
ECX alone (both are ours now, and no caller we have not ported relies on a scratch register surviving a call into
any of the driving engine's 379 replaced functions).

The property parsers (`RuntimeAllocGeoPrimStateConstructor` `0x000ef4c0` with `FUN_000ef7a0`/`FUN_000f04d0`, and
`RuntimeAllocTARConstructor` `0x000ecbe0` with `FUN_000ed390`/`FUN_000ed700`/`FUN_000edb30`) turn text such as
`EAGL::ABM_ADD`, `EAGL::ATM_GEQUAL`, `EAGL::CM_CLAMP`, `EAGL::FM_BILINEAR`, `EAGL::MMM_LINEAR`, `EAGL::STAGE_TWO`,
`EAGL::XBOXCM_BORDER`, `XBOXEXTOBJ_SetAlphaBlend`, `XBOXEXTOBJ_SetAnisotropy` ... into a GeoPrimState or TAR
(`sscanf`, `atof`, `atol`, `strncmp`; property objects from `EAGLInternal::Property new`).

### 3.2 A frame

```
RRenderer::StartFrame -> RenderContext::BeginFrame (0x000e6610): current context, SetRenderTarget
  RViewCamera::SetRenderCamera etc. -> ViewPort::BeginView (0x000e4be0):
      nests over the current view, SetRenderTarget, SetViewport, builds view-projection,
      copies view/projection/view-projection into the registered globals
    ClearViewPort (0x000e49d0) -> D3DDevice_Clear (flags 1 colour, 2 Z, 4 stencil)
    DrawGroupDrawModel / ActCharacter::Draw / RVehicle::RenderShadowGeometry -> Model::Draw (0x000ea210):
      sphere cull (IsSphereInView), model / model-view / model-view-projection globals, patch, CurrentVariation,
      ModelSetScale, then for each {0xffff, GeoPrim} in the draw list: RenderMethod::Draw(GeoPrim) (0x000f0fc0);
      then the three child lists
    effects -> DynamicModel::Draw (0x000e99e0): same matrices, CurrentVariation = 0, each GeoPrim
    HUD/menus -> FONT_drawtextfa -> FONTcurrentdriver -> EAGLFont::FONTEAGL_* -> GeoPrimState apply,
      D3DDevice_Begin / SetVertexData* / End (immediate mode)
  ViewPort::EndView (0x000e49a0): re-begins the view it nested over
RenderContext::EndFrame (0x000e6640): profiler frame mark (0x000f52d0), unbinds textures still bound from the
  frame, SetShaderConstantMode(0), Swap(0), SetShaderConstantMode(1), then re-sends ~40 render states (fog, stencil,
  Z, shaders, the extension block) because Swap clobbers them, writing D3D8's state table directly
```

### 3.3 The render-method machine

`FUN_000f0fc0` *(candidate: RenderMethod::Draw(GeoPrim*), PS2 has `RenderMethod::Draw(EAGL::GeoPrim *)`)* is
thiscall with ECX = the RenderMethod and the GeoPrim on the stack. It sets `0x002401bc` = method,
`0x002401c4` = packet cursor, `0x002401c0` = GeoPrim + 4, and loops: `table[header >> 16]()`, cursor +=
`header & 0xffff` dwords. Handlers take no arguments and read the three globals; most consume one 8-byte parameter.
The table at `0x001ce700` (37 entries; the meanings are read from the handler bodies):

| op | handler | what it does |
|---|---|---|
| 0, 1, 10, 12, 13, 23, 24 | `0x000f6010`, `0x000f6020`, `0x000f65a0`, `0x000f65b0` | skip a parameter |
| 2 | `0x000f6030` | load the stream description (`0x002401c8..d0`) for the variation |
| 3 | `0x000f6090` | select vertex + pixel shader variant from the parameter |
| 4 | `0x000f60f0` | select shader variant 0 |
| 5-8 | `0x000f6120..0x000f61e0` | vertex shader constants from the parameter |
| 9 | `0x000f6220` | stream source from inline data |
| 11 | `0x000f6890` | bind streams: copy the CPU vertex array into the dynamic buffer ring |
| 14 | `0x000f6270` | stream source 0 from the parameter |
| 15 | `0x000f62b0` | bind a TAR (`FUN_000ebd60`, `FUN_000eb3f0`) |
| 16 | `0x000f6320` | apply a GeoPrimState (`FUN_000ef050`), note its primitive type |
| 17, 18 | `0x000f6380`, `0x000f63e0` | two vertex shader constant blocks |
| 19, 20, 27, 28 | `0x000f6440`, `0x000f64a0`, `0x000f6640`, `0x000f66a0` | a matrix, transposed, into 4 vertex shader constants |
| 21, 22 | `0x000f6500`, `0x000f6550` | matrix palette constants (`FUN_000f5df0`) |
| 25 | `0x000f65c0` | `DrawVertices` |
| 26 | `0x000f6600` | `DrawIndexedVertices` |
| 29, 30 | `0x000f6a00`, `0x000f6a50` | CPU skinning (`FUN_000f6aa0` -> `FUN_000f5eb0`, SSE) |
| 31 | `0x000f6700` | set the skinning source |
| 32, 33 | `0x000f6730`, `0x000f6790` | a transposed matrix into pixel shader constants |
| 34, 35 | `0x000f67f0`, `0x000f6830` | pixel shader constants |
| 36 | `0x000f6870` | `RunPushBuffer` of the method's buffer if its flag is set |

The D3D calls go through one-line wrappers at `0x000f4340..0x000f4500` (RunPushBuffer, SetVertexShader,
SetPixelShader, SetVertexShaderConstant1/4/NotInline by count, SetStreamSource x2, SetPixelShaderConstant,
DrawVertices, DrawIndexedVertices). Ghidra files these under "PrintMessage".

This is an interpreter over data, not generated code. `docs/driving-engine-plan.md` says EAGL "compiles each
model's render method into allocated memory and calls it"; nothing in this pass supports that on the Xbox: the only
indirect calls in EAGL's bodies are the opcode table, the allocator pointers, the multiply thunk, SHAPE's allocator,
two profiler callbacks (`0x00241be8`, `0x00241bf0`), the ConstructorPool callbacks and the FnAnim/FONT dispatch. The
PS2 build's `RenderMethod::Compile` builds DMA chains, which is probably where the idea came from. The DEP fault that
plan section describes is worth re-checking against this (the loader's executable heap costs nothing either way).

### 3.4 Animation

The game asks `MemoryPoolManager::NewFnAnim` (`0x00014210`, an EAGL function linked into the engine.anim range) for
an FnAnim of the type stored in the anim data; the factory `FUN_000f7de0` *(MW: FnAnimFactory)* constructs it in the
pool and installs its vtable; slot 15 attaches the data. From then on the engine (ActAnimGroup, ActPoser,
ActCharacter) calls the virtuals: `EvalSQT` (scale/quaternion/translation per bone, masked by a BoneMask),
`EvalVel2D` (root motion), `EvalEvent` (event channels, resolved through `EventTarget`), `EvalPhase`/`FindMatchTime`
(blender synchronisation), `EvalWeights`, `EvalState`. Deleting returns the object to its type's free list
(`0x002414b0`, inline helper `0x000141e0`). `Initializer::InitInternal` and `ShutDown` bracket the system
(ActManager); `MemoryPoolManager::Init`/`Cleanup` are called from game events.

Decompression: `DeltaCompressedData::DecompressValues` (`0x001069d0`) and `DecompressValuesIndexed` (`0x00106df0`)
for the F1/F3 families; the Q families unquantise 48-bit quaternions (`DeltaQFastMinRange::UnQuantize`
`0x00102e30`, `DeltaQFastPhysical::UnQuantize` `0x00102f00`). `RawPoseChannel` stores per-channel decoder function
pointers at run time (`EulF3` `0x000fdec0`, `TranF3` `0x000fdf20`, `QuatF4` `0x000fdf60` and their `*Interp`
versions `0x000fdf90`, `0x000fdfd0`, `0x000fe060`, written from `0x000fda70`, `0x000fdc20`, `0x000fe110`).

---------------------------------------------------------------------------------------------------------------

## 4. Module reference

Each module's functions are in `python tools/function_coverage.py --driving platform.eagl --why`; this section gives
what each is for and the ones that matter.

### 4.1 A loader (42 functions, 7.7 KB)

DynamicLoader (`0x000e51d0..0x000e6390`): ELF hash `FUN_000e51d0` (EDX), `RunDestructors`, `Initialize`, `GetAddr`
(`0x000e57e0`, 4 anim callers), `GetSymbol`, `GetElfData` (`0x000e5ae0`, accepted *PS2: GetElfData*), the lookup
in another loaded object `FUN_000e5af0` (dlsym-like), `GetNextSymbol`/`GetNextAddr`, `RegisterVar`/`UnRegisterVar`/`GetRegisteredVar`, `Release`,
`RunConstructors`, `Resolve`, two ctors and the dtor. `FUN_000e6390` is ConstructorPool's constructor (the static
initialisers use it for both pools).
`RegisterShapes`/`UnRegisterShapes` (`0x000ede60`, `0x000edf50`). SymbolPool (`0x000f3c70..0x000f41d0`: hash
insert/search/remove), ConstructorPool and RuntimeAllocConstructorPool (`0x000f3a20..0x000f3bf0`), `SymbolInit`.
Pure functions on memory: the best shadow-test candidate in EAGL proper.

### 4.2 B device, contexts and viewports (104 functions, 15.4 KB)

ViewPort: `SetShape` `0x000e4340`, `GetShape`, *(candidate)* `SetPerspective` `0x000e46d0` (fov, aspect, near, far:
`D3DXMatrixPerspectiveFovRH`, then the frustum planes with x87 `FPTAN`/`FPATAN`/`FSIN`), *(candidate)*
`SetOrthographicScreenSpace` `0x000e4870` (`D3DXMatrixOrthoRH`), `SetOrthographic` `0x000e4900`
(`D3DXMatrixOrthoOffCenterRH`), `EndView`, `ClearViewPort`, `IsSphereInView`, *(candidate)* `SetGuardBandScale`
`0x000e4b50`, `BeginView`, `ReBegin`, `SetViewMatrix`, the ctors at `0x000f37d0`/`0x000f3860`,
`SetBackgroundColour`, `FUN_000f3990` (returns +0x40, the projection matrix).

Device (`0x000e4f70..0x000e51d0`, `0x000e8900..0x000e8b30`): `Init`, ctor, dtor, `NewRenderContext`,
`DeleteRenderContext`, `Get`, current-context getters/setters, the allocator overrides,
`DeviceExtension::NewTextureRenderContext`.

RenderContext (`0x000e6610..0x000e8740`): `BeginFrame`, `EndFrame`, `SetSize`/`GetSize`, buffer depths,
`SetSyncToVBL`, `SetupFrameBuffers` (`0x000e6c00`, 2032 bytes: `D3D_SetPushBufferSize`, `Direct3D_CreateDevice`,
back buffer and depth surface, `XGetVideoFlags`, gamma, flicker and soft-display filters, `PersistDisplay`),
`SetZWritesEnable`/`GetZWritesEnable`, `NewViewPort`/`DeleteViewPort`. The RenderContextExtension setters at
`0x000e74e0..0x000e7d70` (13 unnamed, called by RRenderer, RStateManager, ActCharacter::StartShadow/EndShadow) each
store a field of the +0x30 block, update the shadow at `0x001cb938..`, and if the device exists call the matching
`SetRenderState_*` or write D3D8's table: stencil func/ref/masks/ops (`0x000e7520..0x000e76f0`), stencil enable
(`0x000e7890`), render mask, fog enable/start/end/density/table mode (`changeFogState`, `0x000e79a0..0x000e7a60`),
`SetFogColour`, `0x000e7ca0` (the visibility-test wrapper, `d3d9Backend.cpp` relies on its "not yet" handling),
`GetFrontBuffer`, `GetBackBuffer`. Each setter's getter sits in the 16-byte gap after it, never made a function and
never called (7.3).

TextureRenderContext (`0x000f3450..0x000f37d0`, 13 functions, all named).

### 4.3 C Model and DynamicModel (52 functions, 9.7 KB)

Live: `Model::Draw`, `Patc`, `Optimize` (from `ModelConstructor`), `SetModelMatrix`, `ModelSetScale`, the
DynamicModel ctor/dtor/`AddGeoPrim`/`Draw`/`DrawNoTransform`/`SetModelMatrix`, `FUN_000e9a70`
*(PS2: DynamicModel::GetModelMatrix, deferred in review)*, `ModelConstructor`/`ModelDestructor` and the destructor
helpers `0x000e8b40..0x000e8be0`. Dead or unreferenced: `GetChild`, `GetGeometry`, `GetTARList`/`SetTexture`,
`DrawInstances`, `Call` and through it the whole morph path (`MorphModel`, `ClearMorphModel`, `FUN_000ea940`,
`FUN_000eaa90`, `FUN_000eabd0`), and every DynamicModel vertex-array method (`SetPrimitiveType`, `SetVar`,
`SetStream`, `SetParamName`, `Lock`, `Unlock`, `SetNumVerts`, `GetIndexFromName`).

### 4.4 D textures (30 functions, 11.0 KB)

`TAR::SwapClut` `0x000eb220`, `FUN_000eb3f0` *(candidate: TAR::Use)*, `TAR::GetShape`, the commit `FUN_000eba80`
(contiguous memory through `XPhysicalAlloc` `0x0010e7e9`, `D3DDevice_CreateTexture2`/`XGSetTextureHeader`/
`D3DResource_Register`, palettes `CreatePalette2`/`Palette_Lock2`, swizzle through `D3DSurface_LockRect`), texture
creation `FUN_000ec000`, `TAR::TAR`/`~TAR`/`SwapShape`, `TARConstructor`/`TARDestructor`, the runtime constructor
and its parsers (`0x000ecbe0..0x000ede60`). `FUN_000eb910` (copies a rectangle into each mip with
`D3DXLoadSurfaceFromMemory`) has no caller. `0x000ed310` is named `RMissileStreak::RMissileStreak` because the
linker folded identical constructors: it zeroes three dwords. Its neighbour `0x000ed320` is a live element destructor
Ghidra never made a function (7.3).

### 4.5 E GeoPrimState (33 functions, 8.5 KB)

The setters (2.5), `GeoPrimStateExtension` ctor, the apply `FUN_000ef050`, the runtime constructor and its two
parsers (`FUN_000ef7a0` 3248 bytes, `FUN_000f04d0` 1920 bytes), the destructor helpers.

### 4.6 F render methods (99 functions, 9.2 KB)

Render method and vertex buffer construction (`0x000f0e50..0x000f13b0`), the interpreter `0x000f0fc0`, the D3D
wrappers (3.3), `DrawArray` (`0x000f55a0..0x000f5cb0`), `DrawGouraud` (ctor, `Init`, `InternalFlush`; used only by
the profiler), `DrawTextured` (dead), fence helpers (`FUN_000f5d70`), the opcode handlers and
their helpers (`0x000f5df0..0x000f6c20`), the dynamic vertex buffer (`0x000f6c20..0x000f6f30`), shader create/delete
(`0x000f6fa0..0x000f7010`), and `FUN_000f7040`, which builds a push buffer in place and runs it (no caller).
`FUN_000f13b0` is a 3x3 cofactor helper for Transform (matrix in EAX).

### 4.7 G Transform (31 functions, 8.3 KB)

*(PS2: EAGL::Transform, 57 names)*. Only EAGL and EAGLAnim call it, plus `Transform::TransformPoint` (`0x000160e0`,
in the engine.anim range) from `ActIK::Solve`. Live: `PostMult`, `BuildRotTrans` x2, `BuildMatrix`, `Transpose`,
`AppendMatrix`, `PrependMatrix`, `BuildRotate` (`FSIN`, `FCOS`, `FSQRT`), quaternion to matrix `FUN_000f3090`,
matrix to quaternion `FUN_000f3160` (`FSQRT`), `BuildSQT` (`0x000f8780`). `0x000f19f0` is named `BuildQuatTrans`
but reads the matrix and writes a quaternion and a translation: it is **ExtractQuatTrans** (PS2 has both names).
The 2 KB after it (`0x000f1a70..0x000f226f`) is undefined code: the six `TransformPoints` overloads, unreferenced.

### 4.8 H EAGLFont (4 functions, 2.0 KB)

`FONTEAGL_draw` `0x000ee490` (one quad per character through `SetVertexData*`), `FONTEAGL_startdraw` `0x000ee920`,
`FUN_000ee760` *(candidate: FONTEAGL_createfont: sets up a GeoPrimState and shaders)*, `FUN_000eea20`
*(candidate: FONTEAGL_destroyfont)*, `FUN_000ee190` *(candidate: FONTEAGL_drawarray or enddraw)*. All reached
through the driver table only.

### 4.9 I PrintMessage and the profiler (20 functions, 4.2 KB)

`PrintMessage` (`maybe_snprintf` + `OutputDebugStringA`), `EAGL::ProfilerRegion` (history, `DrawRegion`,
`DrawRegions`, `ProcessRegions`) with RDTSC timers (`XboxTimer.cpp` audited them) and a frame mark `0x000f52d0`
that EndFrame calls. Reached from static initialisers and one front-end function; whether it draws in retail is
unchecked.

### 4.10 J EAGLAnim objects (79 functions, 5.7 KB)

Every FnXxx constructor, destructor and scalar-deleting destructor, the trivial virtuals, `AnimBank`
constructor/destructor (the loader's callback), `MemoryPoolManager` (`NewBlock` `0x000f7c90`, accepted;
`NewBlockByIdx`, `ResetPool`, `Init`, `Cleanup`, `InitAnimMemoryMap`), the factory `FUN_000f7de0`, `EventTarget`
(dtor, `ResolveEventId`, `GetEventId`).

### 4.11 K EAGLAnim Skeleton and BoneMask (21 functions, 8.3 KB, all unnamed)

Called from `ActPoser::DoMainPose`, `DoIK`, `DoSkeletonPose`, `CalcSnapAndCorrectionMatrices`, `Skin`,
`ActSkeleton::GetStillPose`, `ActIK::Init`, and `FnPoseMirror`. By call shape against the sheet's 22
`EAGLAnim::Skeleton` and 12 `BoneMask` names *(candidates, unconfirmed)*: `0x000f9df0` PoseSQTToGlobal,
`0x000f9f10` PoseLocalToGlobal, `0x000fa290` PoseGlobalToSkin, `0x000fa340` GetStillPose, `0x000f8be0` (3184
bytes) and `0x000f9850` MirrorPose, `0x000f88a0..0x000f8b30` BoneMask operators over the 256-bit bitset helpers at
`0x00106330`/`0x00106350`/`0x00106380` (copy, fill, set bit; `docs/driving/maths.md` 5.1). The skeleton
hierarchy multiplies go through the pointer at `0x001cec7c`.

### 4.12 L EAGLAnim evaluation (203 functions, 50.5 KB)

The bulk: per-type `Eval`/`EvalSQT`/`EvalSQTMask(ed)`/`EvalVel2D`/`EvalWeights`/`FindLowerKey`/
`InitBuffersAsRequired` for the raw, key, delta (lerp, quat, F1, F3, single-Q, Q-fast, Q) and phase channels, the
pose and turn/run blenders (`SetWeight`, `BlendVel`, `BlendFacing`, `AlignCycleBeginEnd`, `ComputeCycleIdx`,
`FindMatchTime`), `RawPoseChannel`, `RawEventChannel::Eval`, `FnRawStateChan::Decode`/`EvalState`,
`CompoundChannel::InitAnimMemoryMap`, `ScratchBuffer`, `AttributeBlock`, `DeltaCompressedData`, `VU0_fastqslerp`,
`QuatMultXxYxZ`. x87 throughout (3235 x87 instructions in the module, 29 `FSQRT`, `FSIN`/`FCOS` in `FUN_000fde00`);
SSE only as `CVTTSS2SI` float-to-int truncation (one per function, 40 functions). The biggest:
`FnDeltaSingleQ::EvalSQTMasked` 3776, `FnDeltaQ::EvalSQTMasked` 2784, `FnDeltaF3::EvalSQTMask` 2576,
`FnDeltaF3::EvalSQT` 2288, `FnDeltaF1::EvalSQTMask` 2000, `FnDeltaF1::EvalSQT` 1552, `DecompressValuesIndexed`
1456, `FnPoseBlender::EvalSQT` 1296.

### 4.13 M realgraph FONT, LOCALE, SHAPE (32 functions, 4.6 KB)

`FONT_drawtextfa`, `FONT_drawtextx<unsigned char>` (`0x001073f0`), `FONT_getrectx` (and a one-instruction thunk at
`0x00107b30` that callers use), `FONT_drawtexta`, `FONT_create`/`destroy`/`restore`/`init`/`installdriver`,
`FONT_bsearch`, `FONT_getkern`, `LOCALE_getstr`; `SHAPE_locatez`, `rowbytes`, `name`, `longname`, `createsize`,
`createat`, `create`, `depth`, `type`, `cluttype`, `version`, `loadfile`, `loadfilez` and the loader `0x0014a370`.
Pure apart from FONT's driver calls and SHAPE's allocation.

---------------------------------------------------------------------------------------------------------------

## 5. The interface downward

### 5.1 D3D8, D3DX and XGRAPHICS entry points

65 D3D8 entry points (plus `D3D_ReturnsTrue` `0x00169450`), 7 D3DX and 3 XGRAPHICS functions are called from code
inside Ghidra's function bodies. Another 33 D3D8 entry points are called only from code Ghidra never made a function
(7.3), all unreferenced: `SetGammaRamp`, `SetFlickerFilter`, `SetSoftDisplayFilter`, `CopyRects`, `SetIndices`,
`CreateIndexBuffer2`, `IsBusy`, `SetTextureState_TexCoordIndex`/`BorderColor`/`ColorKeyColor`, `XGWriteSurfaceToFile`
(EAGL's screenshot `0x000e78d0`), and 22 `SetRenderState_*` setters from three state-dump blocks after
`FUN_000e6390`, `FUN_000eabd0` and `FUN_000eea20`. That is why the coverage tool's edge list shows more than EAGL
can reach. By area, the live ones:

| area | entry points (callers) |
|---|---|
| device and frame | `Direct3D_CreateDevice`, `D3D_SetPushBufferSize`, `Reset`, `GetBackBuffer2`, `GetDepthStencilSurface2`, `SetRenderTarget`, `Clear`, `Swap`, `PersistDisplay`, `SetScreenSpaceOffset`, `SetShaderConstantMode`, `GetGammaRamp`, `GetTile`/`SetTile` (RenderContext, ViewPort) |
| viewport | `SetViewport`; D3DX `MatrixPerspectiveFovRH`, `MatrixOrthoRH`, `MatrixOrthoOffCenterRH`, `Vec3Project` (ViewPort) |
| visibility | `BeginVisibilityTest`, `EndVisibilityTest`, `GetVisibilityTestResult` (`0x000e7ca0`) |
| render state | `SetRenderState_Simple` (11 functions, register-argument), `CullMode`, `FillMode`, `ZEnable`, `FogColor`, `StencilEnable`, `StencilFail`, `ShadowFunc`, `YuvEnable` |
| textures | `CreateTexture2`, `D3D_CreateStandAloneSurface`, `Texture_GetSurfaceLevel2`, `Surface_GetDesc`, `Get2DSurfaceDesc`, `Surface_LockRect`, `SetTexture`, `SetTextureState_BumpEnv`, `CreatePalette2`, `Palette_Lock2`, `SetPalette`; XG `XGSetTextureHeader`, `XGBytesPerPixelFromFormat` (`0x00178fb8`) |
| shaders | `CreateVertexShader`, `DeleteVertexShader`, `SetVertexShader`, `SetVertexShaderConstant1`/`4`/`NotInline` (register arguments), `CreatePixelShader`, `DeletePixelShader`, `SetPixelShader`, `SetPixelShaderConstant` |
| geometry | `CreateVertexBuffer2`, `VertexBuffer_Lock2`, XG `XGSetVertexBufferHeader` (passes its pointer minus `0x80000000`), `SetStreamSource`, `DrawVertices`, `DrawIndexedVertices`, `RunPushBuffer` (2 callers, never reached so far) |
| immediate mode | `Begin`, `End` (also the copy at `0x000eea10`), `SetVertexData2f`, `SetVertexData4f`, `SetVertexDataColor` (font, profiler, `0x000ee190`) |
| resources | `Resource_Register` (adds the base), `Release`, `BlockUntilNotBusy`, `InsertFence`, `BlockOnFence` |
| matrices | `D3DXMatrixMultiply` (16 functions; Ghidra `VU0_MATRIX4_mult`), `D3DXMatrixTranspose` (7) |

### 5.2 Direct writes into D3D8's state

EAGL also uses the XDK's inline state functions, which write D3D8's deferred tables and dirty flags directly. The
seam keeps working because the backend reads those tables back at draw time; a port has to turn each into an
explicit call:

- `0x00175424`, D3D8's dirty flags: `|= 0x2000` (fog), `|= 0x3000` (fog and combiners). Writers: `EndFrame`,
  `SetupFrameBuffers`, `changeFogState` and the fog setters `0x000e79a0..0x000e7a60`, `FUN_000eb3f0`,
  `FUN_000ef050`.
- the render-state table `0x00175628` (slot = (address - `0x00175628`) / 4): slots 57-83 (the "simple" states:
  alpha/blend/Z/stencil, written after each `SetRenderState_Simple`), 93-96 (fog), 103, 116-123 (set up once in
  `SetupFrameBuffers`). Writers: `EndFrame`, `SetupFrameBuffers`, `SetZWritesEnable`, the extension setters,
  `FUN_000ef050`.
- the texture-stage table `0x00175428`, stage 0 words 1-6 and 8 (address modes, filters, LOD bias), from
  `FUN_000eb3f0`.
- `D3D8::D3DRS_FogEnable` by name (`EndFrame`, `SetupFrameBuffers`, `changeFogState`).

The only push-buffer pointer access (`0x00175420`) is in undefined, unreferenced code at `0x000f44e0`.

### 5.3 Maths

EAGL does its own maths, x87 (section 4.7, 4.12; `docs/driving/maths.md` section 3 applies): transcendentals in
`ViewPort::SetPerspective` (5 `FPTAN`, 4 `FPATAN`, 4 `FSIN` - class **A**), `Transform::BuildRotate` and
`FUN_000fde00` (`FSIN`/`FCOS`), `FSQRT` in about 15 functions (class **D** when the result is stored). SSE: the CPU
skinning loop `FUN_000f5eb0` (42 SSE instructions, `MOVAPS` matrix rows indexed by byte bone indices, 64-byte
stride) and `CVTTSS2SI` conversions. No FPU control-word changes. Calls out: `D3DXMatrixMultiply`/`Transpose` and
the projection builders (D3DX, ours), `sin_fractionalangle`, `cos_fractionalangle`, `v3unitcrossprod` (EA maths,
ours), and engine.anim's inline helpers `0x000141d0`, `0x000141e0`, `0x000162e0`, `0x00016530`, `0x00016820`,
`0x00019870` (copies of UMath/VU0 inlines a port of EAGLAnim must provide itself).

### 5.4 Files and memory

- Files: only `0x0014a370` (SHAPE) calls `FILE_loadpackz`. Everything else parses memory the game supplies (3.1).
- Memory: through `EAGLMalloc`/`EAGLFree` (45 and 56 indirect call sites; `UMemory` once RRenderer has installed its
  callbacks); SHAPE through `MEM_allocalign`/`MEM_free` pointers; textures and push buffers through
  `XPhysicalAlloc` (`0x0010e7e9`) and `MmFreeContiguousMemory`; a few `__builtin_delete`; the anim pool carves its
  own blocks; MSVC's vector constructor/destructor iterators (`??_L`, `??_M`) for TAR and GeoPrimState arrays.
- C runtime: `strncmp`, `strstr`, `strncpy`, `_memmove`, `isspace`, `atol`, `atof`, `sscanf`, `bsearch`, `sprintf`,
  `printf` (EAGLAnim diagnostics), `maybe_snprintf`, `__ftol2`, `REAL_addexit`/`removeexit`, `MEM_copy`/`MEM_fill`,
  `TIMER_gettick` (profiler), `OutputDebugStringA`.

---------------------------------------------------------------------------------------------------------------

## 6. The interface to the game, and what is dead

### 6.1 Entry points by caller

162 EAGL functions are called directly from outside EAGL (live callers only):

| caller subsystem | EAGL entries | caller functions | what |
|---|---|---|---|
| engine.render | 96 | 119 | RRenderer (device, contexts, buffer depths, VBL, allocator overrides, `FUN_000e74e0`), RStateManager and the `0x00091560..0x00092140` helpers (GeoPrimState setters, ~30 callers each for `0x000eecf0`, `0x000eedc0`), materials (GeoPrimState ctor/dtor, `FUN_000f1270`, `FUN_000f12c0`, `GetRegisteredVar`), DynamicModel users, `DrawGroupDrawModel` -> `Model::Draw`, shadow maps and offscreen buffers (TextureRenderContext, `ClearViewPort`, `BeginView`), fog (`changeFogState`, `0x000e79a0..0x000e7a60`), `SetRenderMask`, `GetBackBuffer`/`GetFrontBuffer`, `SHAPE_locatez`, TAR functions `0x000ec610..0x000ec8a0` |
| engine.anim | 43 | 34 | ActManager (`Initializer::InitInternal`, `ShutDown`, `RegisterVar`), ActModel/ActSkeleton/BankInfo (DynamicLoader, `GetAddr`, `GetNextAddr`, `FindTars` -> `GetSymbol`, `RegisterShapes`, `TAR::GetShape`/`SwapShape`), ActCharacter (`Model::Draw`, stencil setters `0x000e7520..0x000e7890` for shadows, `Device::Get`/`GetCurrentRenderContext`), ActPoser/ActIK (Skeleton block, `Transform::TransformPoint`), EventTarget, `NewFnAnim`, `Model::SetModelMatrix` |
| game.frontend | 35 | 44 | GHud, GLoadingScreen (BeginFrame/EndFrame/BeginView/Clear directly), GOrthoHudView (`SetOrthographic`, `SetViewMatrix`), GGallery and GGirl (TAR, SHAPE), GSubtitles and GSystem (FONT, LOCALE) |
| game.effects | 27 | 28 | streaks, debris, muzzle flash, EMP, missile cam, sniper zoom, tyre tracks: DynamicModel, GeoPrimState setters, `SetZWritesEnable`, `RVehicle::RenderShadowGeometry` -> `Model::Draw` |
| engine.camera | 18 | 15 | RViewCamera/RRenderWorldCamera: viewport setup (`SetPerspective`, `SetGuardBandScale`, `SetViewMatrix`, Begin/EndView), `NewViewPort`/`DeleteViewPort` |
| engine.static | 15 | 43 | global constructors/destructors of GeoPrimStates, materials, pools |
| engine.core | 14 | 6 | the disc-error screen (`0x0005c960`), `0x0005a6b0`, FONT setup |
| engine.data | 13 | 6 | RCARPFile (DynamicLoader, `Resolve`, `Release`, `RegisterShapes`), UFileLoader (`SHAPE_loadfile(z)`), `TextureRenderContext::GetSize` |
| game.events | 5 | 2 | ERestart/ESetVideo (`SetOrthographic`, viewports, `MemoryPoolManager::Init`/`Cleanup`) |
| others | 6 | 6 | `EAGL_deallocator` (world, missions), DAudio (FONT), `SHAPE_version` (from the SHAPE loader) |

The calls that are not edges: the FnAnim virtuals (engine.anim through vtables), the FONT driver (through
`FONTcurrentdriver`), and the loader's constructor callbacks.

Data structures the game shares with EAGL (layouts in section 2): RRenderer's Device and RenderContext; GeoPrimState
as the base of the game's materials; GeoPrims and RenderMethods the game builds; TARs, SHAPEs and FONTs it creates;
Models from the loaded objects; DynamicLoaders held by RCARPFile, ActModel, ActSkeleton and BankInfo; FnAnim objects
held by ActAnimGroup; the six viewport matrices and the render-method constants, which loaded data references **by
name**, so a port can move them as long as it registers them.

### 6.2 What our code already touches

- `src/driving/render/RenderState.hpp` (used by `RGlareManager.cpp` and `PlayMPC.hpp`) declares AUTOGEN calls to
  `RenderContext::SetZWritesEnable` `0x000e73f0`, `BeginFrame` `0x000e6610`, `EndFrame` `0x000e6640`, `GetSize`
  `0x000e6a80`, `ViewPort::SetShape` (as `SetRect`) `0x000e4340`, `BeginView` `0x000e4be0`, `EndView` `0x000e49a0`,
  `ClearViewPort` `0x000e49d0`, `GeoPrimState::SetDepthTestMethod` `0x000eecd0`.
- `src/driving/gfx/d3dSeam.cpp` patches the texture commit's two branches (`0x000ebbec`, `0x000ebbf4`, 7.5), and
  replaces `D3DDevice_End` at `0x000eea10` inside EAGL's range. Its comments name `0x000eb910` as
  `D3DXLoadSurfaceFromMemory`'s caller and `0x000e7890` as the screenshot: `0x000eb910` has no caller, and the
  screenshot is the undefined `0x000e78d0` (also uncalled); `0x000e7890` is the stencil-enable setter beside it.
- `src/driving/platform/XboxStartup.cpp` turns the WBINVDs inside opcode handlers 15, 25 and 26 (`0x000f62b9`,
  `0x000f65c9`, `0x000f6609`) into NOPs.
- `src/driving/platform/XboxTimer.cpp` redirects the bare `RDTSC; RET` at `0x000f4520` (undefined, uncalled) and
  documents the profiler's RDTSC sites.
- `src/common/gfx/d3d9Backend.cpp` reads D3D8's tables EAGL writes (5.2) and documents the visibility wrapper
  `0x000e7ca0`.

### 6.3 Dead code

Dead now (6): `DrawTextured::DrawTextured` `0x000f5a10`, `Init` `0x000f5a30`, `InternalFlush` `0x000f5be0`,
`FUN_000f5c00`, `DrawTextured::Begin` `0x0014cf10` and a funclet - the movie player's quad, dead since the movie
player became ours (`docs/driving-fmv.md`). `DrawTextured::~DrawTextured` is kept live only by `PlayMPC__Play_Unwind_0`, an unreferenced funclet of the
original `PlayMPC::Play`, so it is dead in practice too.
The movie player also used `SHAPE_create`, `TAR::TAR`, `TAR::SwapShape` and the frame functions, all still live
through other callers.

Live only as unreferenced roots, or reached only from them (175 functions, 22.6 KB; the list was computed by
re-running the coverage graph with EAGL's unreferenced functions removed from the roots): most of Transform (20:
`Invert`, `Determinant`, `ElementMinor`, `Inverse`, all `Append*`/`Prepend*` except the matrix ones,
`BuildAimedTrans`, the second `BuildRotTrans`), the Model getters and morph path (37), the DynamicModel/DrawArray
vertex-array API (16), Skeleton candidates (13: `MirrorPose` `0x000f9850`, `0x000fa610..0x000fa900`, the BoneMask
operators), 27 in anim eval (`FnPoseBlender::Set`, `FnKeyDeltaChan::EvalToPrevValues`, `FUN_000fd0c0` 992 bytes,
the bitset/attribute helpers `0x00106540..0x00106980`, `GetMemoryUsage`), profiler pieces (6), the RenderContext
viewport accessors `0x000ee130..0x000ee180` (6), the push-buffer builder `0x000f7040`, `ViewPortPrivate::ReBegin`,
`FUN_000e4d80`, `FUN_000eb910`, and 34 funclets. "Nothing references it" was checked three ways: Ghidra's
references, a scan of `.text` for direct calls, and a scan of the whole image for the address as a dword.

Code Ghidra never made a function (7.3) is a further ~15 KB, almost all unreferenced getters, setters and debug
routines.

---------------------------------------------------------------------------------------------------------------

## 7. Patching hazards

### 7.1 Register arguments

| address | registers | what |
|---|---|---|
| `0x000e51d0` | EDX = string | ELF hash (DynamicLoader) |
| `0x000f13b0` | EAX = matrix | 3x3 cofactor for `Invert`/`Determinant`, returns ST0 |
| `0x00107e60` | EAX = depth, EBX | SHAPE size helper (`createsize`, `createat`) |
| `0x0014a370` | EAX = file name | SHAPE loader |
| `0x000e8b20`, `0x000e8b30` | EAX (+ ECX) | model destructor helpers (unreferenced) |
| `0x000ee130..0x000ee180` | EAX = index, ECX | RenderContext float-array accessors (unreferenced) |
| `0x00106330`, `0x00106350`, `0x00106380` | thiscall, `RET 4`/`RET 4`/`RET 8` | bitset copy/fill/set (`maths.md` 5.1) |

Below EAGL, `D3DDevice_SetRenderState_Simple` and `SetVertexShaderConstant1`/`4`/`NotInline` take ECX/EDX (the seam
already adapts them). The rest of EAGL is thiscall for methods and cdecl for free functions; `tools/abi_driving.json`
found no other register inputs. Seven functions are flagged incomplete by that tool because they end in a tail jump
through a pointer, which an entry patch must preserve: `RuntimeAllocGeoPrimStateDestructor` `0x000ef780` (`JMP
[EAGLFree]`), `FnPoseMirror::Eval`/`EvalSQT` (`0x000f78a0`, `0x000f78f0`: `JMP [vtable + 0xc]` / `+ 0x18` into
the mirrored channel), `FONT_destroy` `0x00107b60` (`JMP` to the driver's slot), and three funclets.

### 7.2 Thunks

`thunk_FUN_000e4ba0` `0x000f3910`, `thunk_FUN_000f4e70` `0x000f5010`, `FnDeltaChan::~FnDeltaChan` `0x000f8280`,
`FnKeyDeltaChan::~FnKeyDeltaChan` `0x000f82c0`, `MemoryPoolManager::ResetPool` `0x000faa40`, the `FONT_getrectx`
thunk `0x00107b30`, and `0x001066f0` (multiply, reached through `0x001cec7c`). Patch the target, or both.

### 7.3 Code Ghidra cut short or never made a function

152 gaps of 8 bytes or more lie between EAGL's functions (17.7 KB with padding). Of the code in them:

- **Live, never made functions**: four FnAnim virtuals referenced only from their vtables - `0x000fb120`
  (FnRawPoseChannel slot 3 Eval, `0x001a0cb4`), `0x000fd9c0` (FnRawStateChan slot 12 FindTime, `0x001a1048`),
  `0x00101ff0` (FnDeltaQFast slot 4 GetLength, `0x001a13f0`), `0x00104390` (FnTurnBlender slot 3 Eval,
  `0x001a14c0`) - and `0x000ed320`, the 12-byte element destructor TAR code passes to `??_M` (pushed at
  `0x000ed02d`, `0x000edbf2`, `0x000eddf2`, `0x000ef731`).
- **Switch jump tables** in `.text` after their function: `0x000e57a4` (Initialize), `0x000e6259` (Resolve),
  `0x000e7375` (SetupFrameBuffers), `0x000eb15e`, `0x000ebd08` (the texture commit), `0x000edd82`, `0x000eed8c`
  (`0x000eecf0`), `0x000f5696` (DrawArray::SetPrimitiveType), `0x000f7db0`, `0x000f818c` (the factory),
  `0x000fd820` (FnRawStateChan::Decode), `0x0010842a` (SHAPE_cluttype).
- **Unreferenced code**: everything else. Notably the getter after almost every RenderContextExtension and
  GeoPrimState setter (`0x000e7560`, `0x000e78c0`, `0x000eeca0` ...), the screenshot `0x000e78d0`, the RDTSC helper
  `0x000f4520`, the six `TransformPoints` overloads (`0x000f1a70..0x000f226f`), three render-state dump blocks
  (`0x000e63e0`, `0x000eaca0`, `0x000eea50`, which hold the 22 otherwise-unused `SetRenderState_*` calls), the
  immediate-mode helper at `0x000f44e0`, and `0x000fc1c0` (928 bytes in anim eval).

The coverage tool attributes gap code to the function before it, so its edge lists include calls made from dead gap
code; section 5.1 separates the two.

### 7.4 Table-driven dispatch

The render-method opcode table `0x001ce700` (37 entries), the FONT driver table `0x001cd110`, the ConstructorPool
and RuntimeAllocConstructorPool (callbacks by type name), the DynamicLoader's constructor/destructor records, 25
EAGLAnim vtables (2.11), RawPoseChannel's decoder pointers stored at run time (3.4), the multiply pointer
`0x001cec7c`, SHAPE's allocator pointers, the allocator pointers, the loader's resolver callback (+0x424). Patching
an entry point does not catch calls through these; replacing a class means replacing its table slots too.

### 7.5 Self-modifying code and our byte patches

None in EAGL itself. Ours: the texture commit's two `JB`/`JA` at `0x000ebbec`/`0x000ebbf4` (registers every texture
in place, `d3dSeam.cpp`), three WBINVDs NOPed, the RDTSC helper redirected, the D3DDevice_End copy replaced. A port
of the texture module retires the commit patch; the others go with their functions.

---------------------------------------------------------------------------------------------------------------

## 8. Quirks a port must decide on

1. **The direct D3D8 state writes** (5.2): while the D3D8 seam stays underneath, a port must either keep writing
   those tables (the backend reads them) or call the backend explicitly. Mixing the two in one frame is how state
   goes stale.
2. **EndFrame's state restore after Swap** re-sends ~40 states from shadows, and resets the GeoPrimState cache to -1.
   A port that drops the restore must make sure nothing relies on the cache having been invalidated.
3. **The texture commit's physical-memory test** (2.6, 7.5): decide the in-place path for every texture, as the
   patch does, and record which textures the game writes after committing (the pause-menu girl).
4. **Render methods address engine globals by name**: `gpViewMatrix` etc. and the five render-method constants must
   be registered under the same names, and `CurrentVariation` must index parameter data exactly as now.
5. **`RunPushBuffer` (opcode 36)**: never reached in ~700 underwater runs; a port can report and skip it, as the
   seam does.
6. **DeltaCompressedData's arithmetic order**: the Xbox computes `(q * range + current) + min` where MW writes
   `current += q * range + min`. Floats round differently; follow the Xbox listing, not MW.
7. **x87 transcendentals** in `SetPerspective`: the frustum planes feed `IsSphereInView`, so a different last bit can
   cull or keep a model at the edge of the screen. Keep them in assembly (class A).
8. **Morph, instancing, DrawArray**: dead in this game; a port can leave them out and assert.
9. **Ghidra's misleading names**: `BuildQuatTrans` is ExtractQuatTrans, `gGlobalModelViewProjectionMatrix` is the
   view-projection, `RMissileStreak::RMissileStreak` at `0x000ed310` is a folded EAGL constructor, `Model::Patc` is
   Patch, the Model struct's `tarList`/`children`/`numChildren` are mislabelled (2.7), the "PrintMessage" functions
   at `0x000f4340..0x000f4500` are D3D wrappers.

---------------------------------------------------------------------------------------------------------------

## 9. Port structure, order and tests

### 9.1 Shape

One directory, `src/driving/eagl/`, by module, each a separate compilation unit with its own test harness beside it
(the project's rule: test code stays out of the reimplemented functions): `Loader.cpp`, `Transform.cpp`, `Shape.cpp`/
`Font.cpp`/`Locale.cpp`, `GeoPrimState.cpp`, `Tar.cpp`, `RenderMethod.cpp`, `View.cpp`, `Model.cpp`, and
`anim/` for EAGLAnim. Layouts as `static_assert`ed structs (the game code still reads them), registered globals
kept at their addresses until every reader is ours. Calls down go to the D3D8 entry points (the seam) first, not
straight to the backend, so each step can be compared against the original running on the same seam.

### 9.2 Order

| step | what | funcs (strictly live) / KB | test |
|---|---|---|---|
| 0 | housekeeping: the 1.3 classification fixes; create the five live functions of 7.3 in Ghidra and name the candidates once confirmed (the user's review process) | - | - |
| 1 | realgraph LOCALE, SHAPE, FONT text measurement (`LOCALE_getstr`, `SHAPE_*`, `FONT_getrectx`, `getkern`, `bsearch`) | ~28 / 4 | shadow: every `.loc`, `.xsh`, `.xfn` in the archives, every id/name, compare outputs |
| 2 | `EAGL::Transform` (live 11 + `BuildSQT` + `TransformPoint`) | 13 / 4 | shadow, bit-exact (classes D, A for `BuildRotate`); random and recorded inputs |
| 3 | loader: SymbolPool, ConstructorPools, DynamicLoader, RegisterShapes | 40 / 8 | shadow: load every `.o` and `.dat`+`.rel` pair with both, compare the relocated images, symbol tables and the sequence of constructor callbacks byte for byte |
| 4 | property parsers (GeoPrimState and TAR runtime constructors) | ~10 / 9 | none: no `RUNTIME_ALLOC::` symbol exists on this disc (3.1), so the shipped game never calls them. Port provisionally with a loud untested warning (or an assert) beside them; a synthetic test is not worth the effort (the user, 3 Oct 2026). The loader already prints a one-time warning if a RUNTIME_ALLOC symbol ever turns up |
| 5 | EAGLAnim, inside out: pool, scratch, attributes, bitsets; `DeltaCompressedData`; raw and key channels; delta families; phase, compound, blenders, mirror; Skeleton | 260 / 57 | shadow per FnAnim type: every anim in every bank, evaluated by both at a sweep of times and masks, `EvalSQT`/`Vel2D`/`Event`/`Phase` outputs compared bit for bit; then the driving replays that reach characters (peds, sniper) |
| 6 | GeoPrimState setters and apply | 32 / 8 | in-game: runner frame dumps against the original; state-call trace comparison through the seam |
| 7 | TAR and the texture commit (retires the seam's byte patch) | 28 / 11 | in-game frame dumps; the pause-menu girl; car colour swaps |
| 8 | render methods: construction, shaders, the interpreter and handlers, vertex buffer ring, D3D wrappers | 77 / 7 | trace comparison: log every D3D8 entry point call with arguments from both versions over a frame; frame dumps |
| 9 | device, contexts, viewports (incl. `SetPerspective` in assembly), font driver | 95 / 17 | frame dumps; menus and HUD text; split-view and shadow-map passes |
| 10 | Model and DynamicModel | 15 / 3 | frame dumps over the levels |
| 11 | profiler and PrintMessage | 14 / 4 | stub or port last |

About 600 live functions, 120 KB, in all; steps 1-5 (about 350 functions, 80 KB) are testable without a frame and
are where to start. Steps 6-10 need the test runner (frame dumps through `drive_game.ps1` on the
vr-desktop runner, never local game windows) and are best taken together once the D3D8 call
trace comparison exists, because the frame is one unit: a half-ported frame mixes two state caches.

### 9.3 Risks

- **Bit-exact rendering** depends on the x87 view maths (8.7), the SSE skinning loop (`FUN_000f5eb0`, class S with
  the same lane reads), and the order of D3D calls and state writes (8.1, 8.2). The call-trace comparison is the
  real test; frame dumps catch the rest.
- **Animation decompression feeds game logic**: `EvalVel2D` is root motion, event channels drive sounds and game
  events, `FindMatchTime`/phase choose blend points. A last-bit difference can move a pedestrian or fire an event a
  frame late. Hence bit-exact shadow tests before any in-game check.
- **Shared layouts**: GeoPrimState is a base class of game materials, GeoPrims and RenderMethods are built by game
  code, Models/TARs/SHAPEs are read by game code. Field offsets must not move.
- **Hidden callers**: the vtables, the opcode table, the FONT driver and the loader's callbacks (7.4); the five
  never-defined live functions (7.3).
- **Data-driven reach**: render methods and loaded objects decide which handlers and constructors run. Other levels
  may reach paths the underwater level does not (`RunPushBuffer`, unseen opcodes); the port should report unknown
  opcodes and types rather than ignore them.
