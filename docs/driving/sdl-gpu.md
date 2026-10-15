# The SDL GPU backend (packages G1 and G2)

Status: built and tested on x86 (16 October 2026); the Metal leg (G3) is the Mac's. Plan and decisions:
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

## For the Mac (G3)

- SDL's Metal driver takes MSL or metallib, not SPIR-V: run each module through SPIRV-Cross (pinned, FetchContent)
  in `CreateShader` with `#pragma METAL fp math_mode(safe)` in front (macos.md 1.2), and create the device with
  `SDL_GPU_SHADERFORMAT_MSL`. SPIRV-Cross must keep SDL's binding order (samplers, uniform buffers) - check its
  resource remapping against SDL_CreateGPUShader's MSL rules.
- HostWindowSdl.cpp makes the window with `SDL_WINDOW_VULKAN` everywhere but Windows-with-D3D9; on macOS with the
  Metal driver that flag should be left off (it asks SDL to load a Vulkan loader).
- Metal wants vertex strides and attribute offsets that are multiples of 4: SHORT1/SHORT3 elements (2- and 6-byte)
  in the game's declarations would need the stream copied out to a padded layout in `ShaderDraw`.
- D24 depth does not exist on Apple GPUs; D32F is what the backend picks first anyway.
- The swapchain is acquired on the window's thread already (`SDL_RunOnMainThread`).
