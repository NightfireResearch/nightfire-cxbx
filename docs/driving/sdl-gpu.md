# The SDL GPU backend (packages G1, G2 and G3's x86 part)

Status: built and tested on x86 (16 October 2026); of the Metal leg (G3), what needs no Mac is done (MSL by
SPIRV-Cross, 4-byte vertex streams, the macOS window) and the rest is the Mac's checklist at the end. Plan and decisions:
[macos.md](macos.md) sections 1 and 10.

The driving engine's D3D8 seam (src/driving/gfx/d3dSeam.cpp) calls the `D3D9_*` entry points of
src/common/gfx/d3d9Backend.h. `NIGHTFIRE_GFX` picks what implements them:

| `NIGHTFIRE_GFX` | Backend | Hosts |
|---|---|---|
| `d3d9` (default) | d3d9Backend.cpp: Direct3D 9 (Windows', or dxvk-native's on Linux) | everything as before |
| `sdlgpu` | sdlGpuBackend.cpp: SDL3's GPU API (its Vulkan driver on Windows and Linux; Metal on macOS) | needs `NIGHTFIRE_SDL=ON` |

```
cmake -S . -B build/win32-sdlgpu -A Win32 -DNIGHTFIRE_SDL=ON -DNIGHTFIRE_GFX=sdlgpu      # Windows, 32-bit
cmake --preset windows-x64 -DNIGHTFIRE_SDL=ON -DNIGHTFIRE_GFX=sdlgpu                    # Windows, 64-bit
cmake --preset linux-i386 -DNF_FFMPEG_LIB_DIR=... -DNIGHTFIRE_GFX=sdlgpu                 # Linux (no dxvk linked)
```

It switches SDL's GPU subsystem on (`SDL_GPU`, off otherwise). The action engine stays on Direct3D 9.

## G1: SPIR-V

**The translators' graph as SPIR-V** (src/common/gfx/shaderSpirv.cpp, `ShaderBuilder::AssembleSpirv`). The NV2A
vertex and pixel translators build ShaderBuilder's hash-consed graph of scalars as before; `Nv2aVertexShader_TranslateSpirv`
and `Nv2aPixelShader_TranslateSpirv` lower it to a SPIR-V 1.0 module instead of vs_2_0/ps_2_0 tokens. SPIR-V is SSA,
so each node is one instruction. Every add, multiply and divide is `NoContraction`; a product feeds a sum as one
`Fma` (also `NoContraction`) exactly where the D3D9 lowering writes a mad - `ShaderBuilder::Fuse` is the first step of
`Assemble` on its own, so both lowerings share the decision - and a dp is its products summed from the first. Min,
max and the comparisons are selects on ordered comparisons, as the builder folds literals; rsq and log take the
absolute value as D3D9 does; lit is D3D9's.

The interface is fixed, so that any vertex program goes with any pixel program (shaderSpirv.h): inputs at their
register's location (with the conversion SDL's vertex formats need: shorts and bytes as integers, D3DCOLOR swizzled);
outputs 0 diffuse, 1 specular (both clamped to 0..1, as D3D9 clamps oD0/oD1), 2-5 texture coordinates, 6 fog; the
vertex constants a uniform block of 208 float4s (the game's 192 and the host's c200-c204) at SDL's set 1; the pixel
constants a block of 32 at set 3, samplers at set 2.

**What D3D9 did by itself** is the end of each pixel program (`hostPixelEnd`): the fog blend (from the vertex
program's factor) and the alpha test (on the alpha as 8 bits, function and reference from the constants), and the
visibility-test mask.

**The fixed-function programs** (src/common/gfx/fixedFunction.cpp), also ShaderBuilder programs: the pretransformed
vertices (D3DFVF_XYZRHW - the immediate path, screen quads, movie frames, partial clears), the texture stages
(COLOROP/ALPHAOP and their arguments with D3D9's meanings, saturated per stage, cached by state), and a full-screen
pass's vertices. A vertex program can also draw each point as a two-triangle quad (`pointSprite`: the point's
attributes an instance's, the corner the vertex index), since Vulkan without `largePoints` (which SDL does not ask
for) has only one-pixel points, and Metal's point sizes are limited too.

**FXAA**: fxaa.hlsl's pixel shader compiled by glslang (`python3 tools/gfx/fxaa_spirv.py`, in WSL2 here) into
fxaaSpirv.h, its sets and input location rewritten to the interface's.

**Checks.** `spirvdump <folder> <logs or XBE>` (src/common/gfx/devtools/spirvDumpMain.cpp, portable, no Direct3D)
writes every program of d3d9_shaders.log, d3d9_pixel_shaders.log and the XBE's vertex programs (each also as a point
sprite) as .spv, with a hash of its D3D9 tokens per line. `NIGHTFIRE_SPIRVDUMP=1` makes the backend write every module
it creates (translated, fixed-function, FXAA) into spirv/. Then `spirv-val --target-env vulkan1.0` on each.
spirvdump has no target; it builds from five files: `g++ -std=c++17 -O1 -o spirvdump src/common/gfx/devtools/spirvDumpMain.cpp
src/common/gfx/{nv2aPixelShader,nv2aVertexShader,shaderBuilder,shaderSpirv}.cpp`.

- 99 programs from the logs and Driving.xbe (178 modules with the sprite variants): all valid.
- Every module mission 1 creates (47, `NIGHTFIRE_SPIRVDUMP=1`): all valid.
- The D3D9 tokens of all 99 are bit for bit what the tree before the change produced (the hashes, gcc build), so
  moving `Fuse` out of `Assemble` changed nothing for the D3D9 backend.

## G2: the backend

src/common/gfx/sdlGpuBackend.cpp follows d3d9Backend.cpp call for call - the texture cache keyed by the Xbox header,
the stage packing, the vertex constants and c200-c204, render targets, the backbuffer capture and readback, the
visibility tests' scaling, render scale, FXAA - with what SDL's API needs in between:

- **Recording.** SDL uploads only between render passes, so a frame is recorded (passes, draws, blits, downloads)
  and replayed at a flush: every upload of the batch first, through two mapped transfer buffers (the 16 MB ring for
  vertices and indices, copied into one GPU buffer; 32 MB for textures), then the commands in order. A flush happens
  at Swap, and early when results are needed now (a dump, the pause menu's readback, a visibility count) or a buffer
  is full. Swap waits for the previous frame's batch, so at most one frame is queued: without it, a slow GPU
  (lavapipe) let SDL's cycled transfer buffers pile up until the 32-bit address space ran out.
- **Vertex data** is copied per draw, exactly the range the draw reads, as d3d9Backend.cpp does for EAGL's volatile
  streams; indices too (fans, quad lists and polygons become triangle lists).
- **Pipelines** are cached by everything SDL bakes in: programs, vertex layout and strides, primitive, blend,
  colour mask, depth test/write/function/bias, cull, target format. Samplers by state (no border colour in SDL:
  border clamps to the edge). Uniform blocks are pushed only when they change.
- **Textures** arrive as B8G8R8A8 (X8 alpha opaque, the 16-bit formats, L8, A8 and A8L8 widened, P8 through the
  stage's palette, YUY2 converted) or BC1-3 where the device samples them (decoded otherwise). A texture uploaded
  again in one batch goes to a new texture, since the batch's earlier draws read the old pixels.
- **Targets.** The back buffer is a texture of its own (opaque: alpha writes masked, as X8R8G8B8 behaves); render
  targets likewise for X8 formats. Depth is D32F reversed, as d3d9Backend.cpp prefers. New targets are cleared, so a
  run reads the same pixels each time. A clear of the whole target is a pass's load operation; of part (the viewport,
  as D3D9 clears) a quad.
- **Coordinates.** SDL's clip space is D3D's (y up, z 0..1), so the translated programs' positions go as they are,
  with c200.xy - D3D9's half-pixel offset - zero, since SDL rasterises with D3D10's pixel centres. For the same
  reason the pretransformed vertices d3d9Backend.cpp moved by -0.5 are not moved, and the immediate path's, which it
  did not move, move by +0.5.
- **Visibility tests**: SDL has no occlusion queries. A test's draws are drawn a second time, first, into an R8 mask
  depth-tested against the target's depth (writing nothing else); End downloads the mask, and the count is its
  covered pixels (times the game's samples per pixel, over the render scale's area) once the batch has run.
- **Presenting**: a run nobody watches (`GfxHost_PresentEvery`, the lockstep runs) never claims the window and renders
  offscreen. Otherwise the window is claimed on its own thread and each frame is blitted into the swapchain there
  (`SDL_RunOnMainThread`), the swapchain's thread; the window is made with `SDL_WINDOW_VULKAN` in this build.
- Not carried over: texture replacement and texture dumps, the draw traces, DumpBurst, CheckVertices, and the gamma
  ramp (D3D9 ignores it in a window too). The backend's log is sdlgpu_backend.log.

## Tests (16 October 2026)

| What | Result |
|---|---|
| Windows x64 on the runner (`RUNNER_CMAKE_ARGS="-DNIGHTFIRE_SDL=ON -DNIGHTFIRE_GFX=sdlgpu" EXE='Release64\driving_standalone_test.exe'`, SDL's Vulkan driver, RTX 3090), round gpu1 against `lbase_c` | sound traces 16/16 identical to tick 9000; frames 1,013 to 16,664 pixels different over each run's 16 frames, the same by eye (the immediate path's +0.5 included) |
| The same, round gpu2 against gpu1 | 16/16: 0 pixels, traces identical (deterministic) |
| Linux i386 in WSL2 (lavapipe, 640x480, headless, offscreen), m1 and m2, idle and accelerating, against `lwsl` (dxvk on lavapipe) | sound traces identical to tick 9000; frames within 2-4k pixels (m1 idle 2,101) |
| The same, two runs of m1 idle and m2 accelerate | 0 pixels, traces identical |
| The 32-bit D3D9 build (actioninject, drivinginject, driving_standalone, driving_standalone_test), objects against the tree before | identical except nv2aPixelShader, nv2aVertexShader and shaderBuilder (the SPIR-V entry points and `Fuse`); their D3D9 tokens are unchanged (spirvdump's hashes) |

About 490 frames a second offscreen on the runner's 3090 (lockstep, 640x480), 20-25 on lavapipe.

The WSL runs used the scratchpad scripts of the session (a copy of the worktree built with the linux-i386 toolchain
and `-DNIGHTFIRE_GFX=sdlgpu`, run with `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy` and lavapipe); `round.sh`
with `RUNNER_HOST=wsl` and `RUNNER_CMAKE_ARGS=-DNIGHTFIRE_GFX=sdlgpu` should do the same.

## G3 on x86: what is prepared for the Mac

**MSL by SPIRV-Cross** (src/common/gfx/shaderMsl.cpp). SPIRV-Cross is fetched by CMake in every sdlgpu build
(FetchContent, tag `vulkan-sdk-1.4.363.0` of 16 September 2026; static; only its GLSL and MSL backends; built with
C++ exceptions, which shaderMsl.cpp catches, though the toolchains say `-fno-exceptions`). The device is created
for SPIR-V or MSL; when SDL's device reports `SDL_GPU_SHADERFORMAT_MSL` (its Metal driver) `CreateShader` hands SDL
the module's MSL, otherwise the SPIR-V as before. `NIGHTFIRE_SPIRVDUMP=1` writes each module's MSL beside its .spv on
every host. The translation:

- **Bindings.** Every resource keeps its SPIR-V slot: the uniform block at binding n of SDL's uniform set is
  `[[buffer(n)]]`, a sampler at binding n of SDL's sampler set is `[[texture(n)]]` and `[[sampler(n)]]` (SDL's MSL
  order for programs with only uniform buffers and sampled textures; SDL puts vertex buffers from `[[buffer(14)]]`),
  vertex inputs `[[attribute(location)]]` through `[[stage_in]]`, stage outputs and inputs `[[user(locnN)]]`.
  Anything else (storage buffers or images, separate samplers, a resource in another set) is refused.
- **The entry point** is SPIRV-Cross's name for it (`main` becomes `main0`; FXAA's `psmain` stays).
- **`#pragma METAL fp math_mode(safe)`** first: SDL compiles MSL with `options:nil`, Metal's default fast math.
- **NoContraction.** SPIRV-Cross writes every NoContraction add, subtract and multiply as a call of a helper
  (`spvFAdd`, `spvFSub`, `spvFMul`, `[[clang::optnone]]`) that does it as one `fma`, which nothing can fuse further,
  and the SPIR-V `Fma` as `fma`. Its multiply is `fma(l, r, T(0))`, which turns a product of -0 into +0 (-0 + +0 is
  +0); shaderMsl.cpp rewrites it to add -0, the identity, so the product keeps the sign the SPIR-V drivers give it
  (and refuses a SPIRV-Cross whose helper reads otherwise). Divides stay `1.0 / x` (no contraction to prevent; their
  rounding in safe mode is the Mac's to check), as do `rsqrt`, `exp2`, `log2`, `fract`.

**`spirvmsl <folder>`** (src/common/gfx/devtools/spirvMslMain.cpp, target `spirvmsl` in an sdlgpu build): every .spv
of a folder translated as the backend translates it, written as .metal, and checked against its SPIR-V - the pragma
first; the entry point a vertex or fragment function of the name SDL is given; exactly the buffer, texture, sampler
and attribute slots above and nothing else; the `user(locnN)` locations; in a module whose every add, subtract and
multiply is NoContraction, no float `+`, `-` or `*` left as an operator; exactly one helper call per NoContraction
operation and one `fma` per `Fma` whose result is used (SPIRV-Cross writes nothing for an unused value, but does
write a temporary for an unused value other unused code reads twice: calls in temporaries nothing reads are not
counted); the -0 multiply; and that every pixel program's inputs are outputs of every vertex program. Mutations (a
texture slot off by one, the +0 multiply, one multiply left plain) are each caught.

- spirvdump's 178 modules (99 programs and their point-sprite variants): all translate; 2,523 checks, 0 differ;
  13,070 NoContraction operations and 3,351 Fma in use, all written as helpers or `fma`, 410 unused.
- Every module a mission 1 run creates to tick 500 (45, `NIGHTFIRE_SPIRVDUMP=1`, the backend's own translation):
  all pass, FXAA's pixel program aside, whose glslang SPIR-V has 79 plain operations and so plain operators.
- No Metal compiler runs on x86 here (Apple's Metal tools for Windows need an Apple account download), so the MSL
  has not been compiled: the Mac's first check.

**Vertex streams padded to 4 bytes** (`RepackStream`, every host): Metal reads an attribute only at a multiple of 4
bytes in a vertex whose stride is a multiple of 4. A stream with an element of 2 or 6 bytes (SHORT1 read as SHORT2,
SHORT3 as SHORT4), one placed off 4, or a stride that is not a multiple of 4 is copied out per vertex with each
element rounded up to 4 bytes in turn; the bytes it gains are zero and are the components the program does not read
(the translator gives undeclared components their defaults). Mission 1 repacks often: the SHORT1 streams of stride 2
(to 4) and a SHORT3 stream of stride 6 (to 8); `sdlgpu_backend.log` says which shader and stream, once each.

**The window** is made without `SDL_WINDOW_VULKAN` on macOS: SDL's Metal driver makes a Metal view when it claims the
window (`SDL_Metal_CreateView` converts any window), while the flag would make SDL load a Vulkan loader the Mac does
not have.

Already so before G3: D32F depth first (Apple GPUs have no D24); every texture arrives as B8G8R8A8 or BC1-3 (checked
with `SDL_GPUTextureSupportsFormat`), so no component swizzles are needed; the swapchain is acquired on the window's
thread (`SDL_RunOnMainThread`); SDL's backend order puts Metal before Vulkan, so MoltenVK is never picked.

### Tests (G3 preparation)

| What | Result |
|---|---|
| Windows x64 on the runner (as in G2's tests, Vulkan), round g3a | sound traces 16/16 identical to `lbase_c` to tick 9000; frames 0 pixels different from G2's round gpu2 in all 16 |
| Linux i386 in WSL2 (lavapipe), m1 and m2, idle and accelerating, against the same runs of the G2 build (09b9c65) | 4/4: frames 0 pixels different, traces identical; 7-9 streams repacked per run |
| `spirvmsl` on spirvdump's 178 modules, clang i386 and MSVC Win32 builds | 0 differ; the two builds' MSL byte for byte the same |
| Builds | linux-i386 sdlgpu (driving_standalone, driving_standalone_test, spirvmsl), Windows Win32 sdlgpu (drivinginject, driving_standalone, driving_standalone_test, spirvmsl), Windows x64 sdlgpu (the runner); the d3d9 builds compile none of it |

So the padding changes no frame: the components it zeroes are ones the programs never read.

### For the agent on the Mac

1. Build with the `macos-arm64` preset (A1) and `-DNIGHTFIRE_GFX=sdlgpu`: SPIRV-Cross comes by FetchContent (git)
   like SDL3. `sdlgpu_backend.log` must say "SDL's metal driver, shaders as MSL".
2. Compile the MSL: `spirvdump` (as above) into a folder, `spirvmsl` over it, then `xcrun -sdk macosx metal -c
   -std=macos-metal2.1 <file>.metal` on every .metal (needs the full Xcode or the Metal toolchain component; without
   it, `SDL_CreateGPUShader` compiling them at run time is the check - any failure is in sdlgpu_backend.log as
   "SDL_CreateGPUShader failed"). Watch for: the `math_mode` pragma accepted (macOS 15), `[[clang::optnone]]` on the
   helpers, `precise::clamp`.
3. Boot mission 1 headless in lockstep to tick 500 with frames dumped; look at them. Then two runs of the 16, identical
   to each other (G3's test), and `lmac_c` from them (A2).
4. Speed: the helpers are `optnone` functions, one call per add or multiply. If frames are slow, measure first; the
   alternative is SPIRV-Cross without them plus `#pragma METAL fp contract(off)` (if the Metal compiler takes it),
   which must then be shown to change no frame against the helper build.
5. If frames differ from the Vulkan build's by more than the D3D9/Vulkan comparison did (sdl-gpu.md's tests), the
   suspects in order: divides and `rsqrt`/`exp2`/`log2` precision in safe mode, the immediate path's +0.5 and pixel
   centres, sampler addressing (no border colour), point sprites (`instance_id` with a base instance of 0).
6. Windowed (A3, only when the user asks): the window claimed on the main thread, presenting through the Metal layer.
