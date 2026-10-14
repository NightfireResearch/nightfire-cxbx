# Driving engine: a portable build

Status: plan (14 October 2026). Follows the standalone build (docs/driving/standalone.md), which it assumes: an
executable built from our source alone, with no Driving.xbe mapped and no injection.

## Where the dependencies are

A standalone build is still a Windows program: it needs Wine elsewhere only because of the APIs below. They are few and
concentrated.

| Dependency | Where | Replacement |
|---|---|---|
| Threads, TLS, critical sections, heap, files, timers, events | src/driving/platform/ (CRuntime, FileSys, RealSystem, XboxStartup, XboxTimer, XboxXapi), Scheduler.cpp, sound/xaudio2Driving.cpp | a platform layer with Win32 and POSIX/SDL3 implementations |
| Window, keyboard, pads (Win32 window, XInput) | platform/XboxInput.cpp, Pad.cpp, standalone/StandaloneMain.cpp, src/common/renderWindow.h | SDL3 |
| Sound output (XAudio2 source voices behind the DirectSound seam) | sound/xaudio2Driving.cpp, sound/dsndSeam.cpp | SDL3 audio (or miniaudio) and a small voice mixer of our own; the SND mixer above it is already ours |
| Direct3D 9 | src/common/gfx/d3d9Backend.cpp | kept: DXVK's native build (dxvk-native, formerly D9VK) runs D3D9 over Vulkan on Linux without Wine |
| HLSL compiler (D3DCompile) | none since step 1: only devtools/shaderCheck.cpp | the NV2A translators emit D3D9 shader bytecode themselves (vs_2_0/ps_2_0 tokens) - DXVK has no compiler |
| Movies | src/common/fmv (FFmpeg) | already portable |

Already portable: no structured exception handling outside devtools; four `__thiscall`s (GCC and Clang support the
attribute on i386); the x87 inline assembly (108 naked functions) is MSVC syntax, which Clang accepts on x86 with
`-fms-extensions`; the mingw cross build compiles everything.

## Two levels

**1. Linux on x86, 32-bit.** The platform layer, SDL3 for the window, input and sound output, dxvk-native for D3D9, the
bytecode-emitting shader translator, and a Linux Clang/GCC `-m32` preset. Every structure layout and the x87 code stay
as they are, so the lockstep runs remain bit-exact and the whole test suite carries over (baselines are per machine:
a Linux runner makes its own). Dependencies: SDL3 and dxvk-native, both of which build for i386. This is the target.

**2. 64-bit and other architectures (including macOS / Apple Silicon).** Much larger: 864 `static_assert(sizeof...)`
pin 32-bit layouts, and the game's data files (CARP, the TAR shape and font files...) are loaded into memory with their
32-bit pointers fixed up in place, so every such structure needs offset-based loading; the x87 code would become
portable C, which loses bit-exactness with the original (and so lockstep against the baselines) unless x87 arithmetic
is emulated; and DXVK on macOS goes through MoltenVK, whose D3D9 coverage is incomplete. Not planned.

## Order of work for level 1

Each step keeps the Windows build working and is tested by the lockstep runs and shadows on the existing runner.

1. **Shader bytecode.** Done (14 October 2026, docs/architecture/shader-bytecode.md): the translators
   (nv2aVertexShader.cpp, nv2aPixelShader.cpp, through shaderBuilder.cpp) write D3D9 bytecode and FXAA's is
   committed; D3DCompile is used only by the devtools check comparing the two. Lockstep frames are exact against
   the baselines.
2. **Platform layer.** One interface (threads, TLS, locks, events, heap, files, time) with the Win32 implementation the
   code uses today, then a POSIX implementation. Behaviour-neutral on Windows: lockstep and shadows unchanged.
3. **Window, input and sound output on SDL3**, behind a build option on Windows first; the sound traces and frames of the
   lockstep runs must not change (output devices are below the traced layer).
4. **Linux preset**: Clang `-m32`, dxvk-native and SDL3 as dependencies, the standalone executable only.
5. **A Linux runner** (the user would switch vr-desktop to Linux): its own baselines from a tested commit, then the
   usual rounds. Lockstep across machines is not comparable (docs/test-runner.md), so Windows and Linux each keep theirs.

Steps 1-3 need nothing from the standalone work and can be done in parallel with it; 4 and 5 need the standalone
executable.
