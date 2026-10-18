# The driving engine on Apple Silicon macOS

Status: good enough for now (16 October 2026) - parked by the user with the loose ends in section 7.2. On the Mac: A1 and G3 done, A2's tooling working (section 7.1); step 4 of the roadmap in [portable.md](portable.md).
SSE header, 3.1), M3, G1 and G2 ([sdl-gpu.md](sdl-gpu.md)), and what of G3 runs without a Mac. It follows the 64-bit work ([64-bit.md](64-bit.md)), which
it assumes is finished:
packages 3a-5, 8 and 9 (the 64-bit standalone executable exact in lockstep against `lbase_c`) and 10 (Linux
x86-64, LP64). The macOS branch for an agent running on the Mac is drafted from this once those land.

The target is the standalone test and play executables (`driving_standalone`, `driving_standalone_test`) as native
arm64 Mach-O programs: no Rosetta, no Wine, no x86 code. The injected build and the shadows stay 32-bit Windows,
the oracle.

## Summary

| Question | Decision |
|---|---|
| Graphics | A backend of our own over **SDL3's GPU API** (Metal on macOS), behind the same `D3D9_*` entry points the seam calls; the NV2A translators' value graph emitted as **SPIR-V**, turned into MSL on the Mac by SPIRV-Cross. Not dxvk: upstream needs Vulkan features MoltenVK lacks (1). |
| The game window | `__PAGEZERO` cannot be shrunk on arm64, so the window's base is not 0: `kGameWindowBase` becomes 0x1'0000'0000 on macOS (one addition per decode), null stays the word 0 (2). |
| ABI | One ABI: the game code is ordinary arm64 C++ (Apple's AAPCS64 variant, Itanium C++ ABI). The Linux port's two-ABI split is not needed on any 64-bit host (3). |
| x86 left in the 64-bit build | ~360 SSE intrinsic uses in 20 files, `__rdtsc`, `__movsd`, `_mm_empty`, one `fldcw`, the X87.h conversions; replaced by a portable header that keeps SSE's exact semantics, `RSQRTSS` included (3). |
| Floating point | `-ffp-contract=off` everywhere (clang contracts into `fmadd` by default, and arm64 always has FMA); denormals already agree; out-of-range conversions through helpers. Expect the simulation to match `lbase_c` very closely, possibly exactly; frames never compare across machines (4). |
| Platform | HostPosix with Darwin's gaps filled (no `MAP_FIXED_NOREPLACE`, `/proc`, `clock_nanosleep`, `pthread_condattr_setclock`, unnamed semaphores, `dl_iterate_phdr`); SDL3 as on Linux; FFmpeg built by our script; ad-hoc signing only (5). |
| Testing | The agent runs on the Mac with `RUNNER_HOST=local`; drive_game.sh made BSD-safe; lockstep runs headless on the real GPU; baselines `lmac_c_*` (6). |

Most of the work can be done and tested before the Mac is involved: the portable SSE header, the non-zero window
base, the FP and threading audits and the new graphics backend all run on x86-64 Windows or Linux first (7).

## 1. Graphics

### 1.1 dxvk on MoltenVK: not usable as it is

- **Upstream DXVK requires what Metal does not have.** DXVK 3.x wants a Vulkan 1.4 driver with
  `VK_EXT_transform_feedback` among its required extensions [dxvk-wiki]; it also needs the `geometryShader`
  feature, and "upstream DXVK 3.1.1 itself does not run on MoltenVK" for that reason [d9vk-mac]. MoltenVK
  implements Vulkan 1.4 and lists `VK_EXT_robustness2`, `VK_EXT_depth_clip_enable` and the maintenance extensions,
  but neither transform feedback nor `VK_EXT_custom_border_color` [mvk-guide]; Metal has no transform feedback API
  [apple-tf]. D3D9 itself needs neither feature, but dxvk asks for them at device creation.
- **The macOS forks are Wine DLLs.** Gcenx's DXVK-macOS covers D3D10/11 only and removed its d3d9.dll in 2023
  [gcenx-dxvk]; a DXVK 3.1 fork with D3D9 on stock MoltenVK 1.4.1 appeared on 16 September 2026, as i386/x86_64
  Wine PE DLLs, verified on one benchmark [d9vk-mac]. None is dxvk-native, none is arm64. CodeWeavers state that
  upstream DXVK is not compatible with macOS [cw-dxvk].
- **Nobody else's D3D9-on-Metal fits.** D3DMetal (Apple's Game Porting Toolkit, used by CrossOver) translates D3D11
  and D3D12 [cw-d3dmetal, kegworks]; Whisky, the free front end, stopped development in 2025 [whisky]. CrossOver's
  D3D9 path is Wine's own wined3d. All of these are Wine-hosted.

Taking dxvk anyway would mean maintaining a dxvk-native fork for macOS arm64 with the device requirements patched
out, two translation layers (D3D9 to Vulkan to Metal) whose bugs are someone else's, and a Mac-only build of it.

### 1.2 Our own backend

The D3D9 backend (src/common/gfx/d3d9Backend.cpp, 3,300 lines) is small in what it asks of Direct3D: about 40
device calls (render, sampler and texture-stage states, `DrawPrimitive*`, textures, two render targets, readback,
gamma ramp), 16 texture formats, and two shader paths. The game never sees Direct3D: the seam (d3dSeam.cpp) calls
the `D3D9_*` functions of d3d9Backend.h. A second implementation of that header is the natural unit.

| Option | For | Against |
|---|---|---|
| **A. SDL3 GPU API** (recommended) | One C API over Metal, Vulkan and D3D12 [sdl-gpu]; SDL3 is already built from source here (its GPU subsystem is switched off today); SDL already owns the window. **Develop and test on this PC and the runner** (its Vulkan backend), then only the Metal leg is new on the Mac. Could later replace dxvk on Linux. | Immutable pipelines (blend, depth, cull and vertex layout baked in): a pipeline cache keyed by state, as Metal itself would need. No fixed function: the pretransformed and immediate paths get small shaders of their own. Takes MSL or metallib on Metal, so SPIR-V goes through SPIRV-Cross. |
| B. Metal directly (metal-cpp or Objective-C++) | No intermediate; MSL text is the easiest shader output to write and read. | Mac-only code, written and tested only on the Mac by the agent there; a third backend to keep if Linux ever wants one too. |
| C. Vulkan directly, on MoltenVK | Portable. | Vulkan's verbosity plus MoltenVK's layering, for nothing A does not give. |
| D. dxvk-native fork (1.1) | No new backend code if it works. | As above; a fork to carry; unproven on arm64. |

**Shaders.** The translators (nv2aVertexShader.cpp, nv2aPixelShader.cpp) build a hash-consed graph of scalar
values in shaderBuilder.cpp, and `Assemble` lowers it to vs_2_0/ps_2_0 tokens with register allocation and the
D3D9 read-port and phase rules ([shader-bytecode.md](../architecture/shader-bytecode.md)). SPIR-V is SSA: each node
becomes one instruction, with no registers, ports or phases, so a SPIR-V lowering is simpler than the D3D9 one
(~1,000 lines with the module boilerplate; `spirv-val` checks it). "Rounding as the compiler rounded" applies only
to the D3D9 frames' baselines; on the new path each operation is marked `NoContraction` unless the NV2A program
has a MAD there (emitted as `Fma`). On the Mac, SPIRV-Cross (pinned, FetchContent) makes MSL; each library is
prefixed with `#pragma METAL fp math_mode(safe)` (macOS 15+; it overrides the compile options, whose old default
is fast math) [dawn-mathmode, apple-mathmode]. FXAA's HLSL is compiled once to SPIR-V (dxc `-spirv`) and committed
beside its D3D9 bytecode.

**Formats on Apple GPUs.** DXT1/3/5 are BC1-3 (supported on Apple Silicon Macs; `SDL_GPUTextureSupportsFormat`
checks); A8R8G8B8/X8R8G8B8 are B8G8R8A8; R5G6B5, X1R5G5B5/A1R5G5B5 and A4R4G4B4 have 16-bit equivalents with a
component swizzle; L8, A8 and A8L8 are R8/RG8 with a swizzle; P8 is expanded already. D24S8 does not exist on Apple
GPUs: Depth32Float_Stencil8 (the lockable D32F is Depth32Float).

**Threads.** The window and its events stay on the main thread (HostWindowSdl.cpp already does this); the backend
records and submits from the game thread. Acquiring the swapchain texture may have to hop to the main thread as
`GfxHost_SizeWindow` does: to check in package G2. Lockstep runs need no swapchain at all: the backend renders into
an offscreen target and the dump path downloads it, so no window or WindowServer session is needed (6).

**Effort.** G1 (SPIR-V lowering) M; G2 (the SDL GPU backend, on Windows/Linux Vulkan) L; G3 (the Metal leg on the
Mac) S-M. Option D's spike is not recommended; if the user wants a picture on the Mac sooner, a one-day timebox
building dxvk-native with the fork's patches against MoltenVK is the only shortcut.

## 2. The game window on arm64

**`__PAGEZERO` is 4 GB and stays 4 GB.** On arm64 macOS a main executable must be position-independent, and a
smaller `__PAGEZERO` (`-pagezero_size`) is refused: binaries built so are killed before `main` or reported as
malformed, ad-hoc signed or not, hardened runtime or not; Apple calls it unsupportable on arm64
[apple-pagezero, obs-luajit, hn-pagezero]. So nothing of ours can live below 4 GB, and the base-0 design of
64-bit.md 7.1 does not apply. Notarisation is moot: the binary does not run.

**The base.** The executable is mapped at 0x1'0000'0000 plus a small ASLR slide, and the dyld shared cache from
about 0x1'8000'0000. A window of [0x1'0000'0000, 0x2'0000'0000) therefore holds the executable's statics, vtables
and functions (which the data names: registered variables, EAGLAnim's channel functions, the resolver's symbols),
and the free range between the image and the shared cache (about 1.9 GB) takes the game's arena and contiguous
memory. The Xbox had 64 MB; the arena (1 GB today) can shrink to 256 MB if room is short.

What changes, all in platform/:

- **Ptr32.h.** `kGameWindowBase` is `0x100000000` on macOS (still a constant, so a decode is one add); a
  start-up check stops the game if the image or any window allocation falls outside [base, base + 4 GB). If a
  future macOS slides further, it becomes a variable set once at start-up from the image's address.
- **Null.** With base 0 the word 0 was null for free. Now `PtrFrom32(0)` must give `nullptr` and `PtrTo32(nullptr)`
  0 (a conditional select per decode and encode). The window's first 64 KB is never allocated, so no real object
  has the word 0.
- **Words as offsets.** The loaders' relative arithmetic stays right (CARP's `data += item`: offset plus offset is
  the target's offset) provided every address becomes a word through `PtrTo32`. A plain `uint32_t(uintptr_t(p))`
  is right only when the base is 0, so such leftovers are invisible on Windows and Linux. Package M1 finds them
  there first.
- **The pointer as a float** (AnimMisc.cpp, 64-bit.md 3: the original compares a time with a frame's address) must
  use the word, so that every host compares the same value.
- **Host.h.** `HostReserveLow(size, lowest)` and `HostAllocatePages(size, lowest, limit)` take offsets in the
  window. KernelImports' "below 0x10000000" for contiguous memory and the D3D resources' data words become window
  offsets. Darwin places mappings with `mach_vm_allocate(VM_FLAGS_FIXED)` (fails where anything is mapped, as
  `MAP_FIXED_NOREPLACE` does on Linux) after a `mach_vm_region` walk in place of /proc/self/maps.
- **Build arm64, not arm64e.** Without pointer authentication the game's tables of raw function addresses
  (`XbeVirtual`, 265 uses, the vtable constants) are called as they are.

**Done on x86 (package M1, 15 October 2026; 64-bit.md 7.11).** `NIGHTFIRE_WINDOW_BASE` (CMake, 0 by default) sets
`kGameWindowBase`; with `0x100000000` the image is linked at base + 4 MB, null is the word 0 both ways, the window's
first 64 KB stays free, the host's page and reservation ranges are window offsets, and the arena's set-up stops the
game if the image is outside the window. Windows x64 (runner) is exact against `lbase_c` in all 16 lockstep runs with
the base at 4 GB and at 0, and Linux x86-64 (this PC's WSL2) 16/16 exact against `lwsl_c`, the i386 portable-C
build's baselines, with either base. What the Mac needs beyond the plan above, found there:

- **The game's thread stacks must be in the window**: its data holds stack addresses (a sprite's vertex arrays go into
  an EAGL request's `Ptr32` parameters). HostPosix gives every thread it starts a stack mapped in the window
  (`pthread_attr_setstack`), which Darwin takes as Linux does; the first thread runs no game code.
- **A resolver's answer may be a number** (event ids for the animation banks' `event.` names): `AnswerTo32` keeps
  values below 4 GB as they are when the window is above it.
- **A `Ptr32` through `...` is its word**; one site (a `Crt::sprintf`), found by making the class non-trivial.
- **Toolchain**: on Linux the executable is `-fPIE` code linked `-no-pie` at the fixed address, with the PIC start-up
  files and lld's `--no-relax` (it turns GOT loads into 32-bit immediates). On the Mac the executable is PIE anyway
  and slides: the window base is the constant 4 GB, so the image must land within 4 GB of it - the start-up check
  says so if it does not (then the base becomes a variable, as planned).

## 3. ABI and what is still x86

**One ABI.** The Linux port compiled the game for `i686-pc-windows-gnu-elf` because 32-bit x86 has `__thiscall`,
`__fastcall` with a dummy EDX, MSVC's 8-byte alignment of doubles and its bitfields. On x86-64 and arm64 there is
one convention, doubles are 8-aligned everywhere, and the game's bitfields (EAGL's TAR headers: 32-bit units only)
lay out the same in MSVC's and the Itanium ABI. `XbeOriginal`'s `{address, 0}` member-function pointer is right in
the ARM variant of the Itanium ABI too (the virtual flag lives in the adjustment). Itanium reuses a non-POD base's
tail padding where MSVC does not: that can only move file structures, whose layout checks are unconditional and
fail at compile time. So the 64-bit build after package 10 is ABI-neutral C++, compiled for arm64 like any other
program, `-fms-extensions` kept for `__declspec` and friends.

Apple's arm64 differs from AAPCS64 where it matters here in that `char` and `wchar_t` are signed (as on x86), and
`long double` is `double` [apple-arm64]. The C runtime's 12-byte long double is our own emulation in integers
(CRuntimeFloat.cpp), so nothing depends on the host's. `long` is 64-bit as on Linux (package 10). Unaligned loads
are allowed.

**What is x86 in the 64-bit standalone build** (counted on driving-64 at 7d096e7, outside devtools, with
`NIGHTFIRE_X87=0` and `NIGHTFIRE_STANDALONE`; the 108 naked functions are all behind one or the other):

| What | Where | On arm64 |
|---|---|---|
| SSE intrinsics: 34 kinds, ~360 uses (`_mm_mul_ps` 87, `_mm_add_ps` 55, loads, stores, shuffles, `_ss` forms, `_mm_sqrt_ss`, `_mm_rsqrt_ss` 2, conversions) | VU0Math.cpp (90), D3DXMath, RenderMethod, the sound mixer and filters (Mixer, Filters, Decode), cameras, EAGLAnim, ParticleCache, BondCarSimulate, X87.h | a header of ours with exactly those 34, real intrinsics on x86 and C with SSE's semantics elsewhere (package M0) |
| `RSQRTSS` (`VU0_rsqrt`, the unit-vector scale) | VU0Math.cpp | an estimate whose bits are the vendor's: Intel and AMD already differ [ocallahan-rsqrt]; a table reproducing the runner's Intel results, generated there |
| `Truncate`/`RoundToInt` (CVTTSS2SI/CVTSS2SI: out of range gives 0x80000000; arm64's FCVTZS saturates) | X87.h | spelled out in C |
| `__rdtsc` (EAGL profiler; RenderHigh's frames-per-second, in Xbox cycles) | Profiler.cpp, RenderHigh.cpp | a host cycle count scaled to the Xbox's 733 MHz (`mach_absolute_time`); the arm64 PMU counter is not readable from user mode |
| `__movsd` (REP MOVSD) | Model.cpp | the C version in src/common/linux/intrin.h, moved to a non-MSVC header |
| `_mm_empty` | GameLoop.cpp, EventsD.cpp | nothing (no MMX) |
| `fldcw` (Windows' x87 control word on each thread) | HostPosix.cpp | x86 only |
| `_BitScan*64` | GameWindow.cpp (MSVC only) | `__builtin_clzll`/`ctzll` already the non-MSVC path |

### 3.1 The portable SSE header (package M0, done 15 October 2026)

- **Sse.h** (src/driving/platform) is the one place the game's ports get their x86 intrinsics from: the 30 `_mm_*`
  intrinsics the driving engine uses outside devtools, `__rdtsc`, `__movsd`, `__movsb` (`_mm_getcsr`/`_mm_setcsr`
  are devtools-only and stay out). `NIGHTFIRE_SSE` (CMake option, on by default, forced off when the target is
  arm/aarch64) chooses: 1 includes the system headers, so the game's code compiles to exactly what it did; 0 maps
  each name by macro onto **SseC.h**'s C forms (`namespace sse`). On x86 the system headers are included first even
  with 0, so a system header that pulls in `<xmmintrin.h>` later adds nothing the macros would clash with; and on
  x86 `__rdtsc`, `__movsd` and `__movsb` stay the instructions with 0 (winnt.h declares them again, which a macro
  would break; none of them feeds the simulation, and sse_test checks their C forms).
- **The C forms** keep SSE's results: lane-wise IEEE single operations, then SSE's NaN choice made explicitly
  (an operand's NaN quieted, the first's when both are NaN, otherwise the negative default NaN `0xffc00000`), which
  ARM would otherwise answer differently; `SQRTSS` of a negative gives the default NaN; `CVTTSS2SI`/`CVTSS2SI`
  give `0x80000000` out of range (round to nearest even spelled out, so no libm and no host rounding mode);
  REP MOVSD/MOVSB copy upwards element by element; `__rdtsc` counts 733,333,333 Hz from `std::chrono::steady_clock`
  (as XboxTimer.cpp does on Windows); `_mm_empty` is nothing.
- **RSQRTSS** is a table: the runner's Intel i7-9700K reads only the exponent's parity and the mantissa's top 10
  bits (this PC's AMD reads 12), and every two exponent steps halve the result exactly; zero and denormals give
  infinity of their sign, other negatives the default NaN, +infinity +0. `sse_test --rsqrt-table` (the devtools
  target of src/driving/devtools/SseTest.cpp) finds the bits by sweep, checks the exponent rule on every normal
  input and prints SseRsqrtTable.h (2 x 1024 words). The committed table is the runner's.
- **Tests.** `sse_test` compares every C form with the instruction: all 2^32 inputs for `RSQRTSS`, `SQRTSS`,
  `CVTTSS2SI`, `CVTSS2SI` and `CVTSI2SS`; 6,034^2 operand pairs (edges: zeros, denormals, limits, infinities,
  quiet and signalling NaNs of both signs, integer limits, half-way values; plus random) through the arithmetic in
  every lane; every shuffle selector; loads, stores, sets, `CVTDQ2PS`, overlapping string moves. On the runner, x64
  and Win32 MSVC builds: 22,494,486,810 checks, 0 differ. 3,104 lanes are counted apart: an addition or
  multiplication of two NaNs, where MSVC swapped the instruction's operands (it treats them as commutative), so
  the hardware gave the second NaN; the compiled game is subject to the same choice, so the C form cannot be "more
  exact" there. On this PC (AMD, clang x86-64, with an AMD table) also 0 differ; mutations (a positive default NaN,
  ties rounded away from zero) are caught.
- **The builds.** The Win32 targets (driving, drivinginject, driving_standalone, driving_standalone_test) and the
  x64 standalone ones compile to the same objects as before: every section of every object identical, bytes and
  relocations (the object comparison of the 64-bit packages). The Linux i386 build uses REP MOVSD/MOVSB from Sse.h
  now (src/common/linux/intrin.h no longer has them). **The Windows x64 build with `-DNIGHTFIRE_SSE=OFF`** (every
  SSE intrinsic of the game as C, RSQRTSS from the table) on the runner: 16 of 16 lockstep runs exact against
  `lbase_c` (round m0ssec: 0 px differ, sound traces identical to tick 9000). Linux x86-64 with the C forms forced
  on waits for package M1/10's preset; with `-DNIGHTFIRE_SSE=OFF` it compiles cleanly file by file already (clang
  x86_64 `-fsyntax-only` on the users of Sse.h). Its lockstep comparison has to be on one machine: in this PC's WSL
  against WSL baselines made the same way (`lwsl_c`), with this PC's AMD table in place of the Intel one (print it
  with `sse_test --rsqrt-table` there), since `lbase_c` is the runner's Intel.
- **Left for the Mac.** Nothing in M0: on arm64 the option is forced off. The aarch64 compile of the game itself is
  package M2's probe.

**Memory ordering.** x86 is TSO; arm64 is weakly ordered. The game's thread hand-offs (the sound thread, the 100 Hz
server, the multimedia timer, 39 `volatile`s in platform/, sound/snd/Platform, the D3D backend) were written for
x86 and may rely on store order. The host's interlocked calls must be sequentially consistent atomics, and each
`volatile` flag that publishes data needs acquire/release. Package M2 ran ThreadSanitizer on Linux x86-64 (below).

**Package M2's findings (16 October 2026).** ThreadSanitizer (`-DNIGHTFIRE_SANITIZE=thread`, Linux x86-64 in WSL2,
16 lockstep runs to tick 1000) reported about 30 races a run, in 20 places. Four handed data from one thread to
another through a plain or `volatile` variable, so that on arm64 the reader could see the flag before the data;
those are now `std::atomic` (sequentially consistent, a plain `mov` load on x86):

- THREAD (RealSystem.cpp): the start block's `started` flag, after which the creator's stack frame is reused.
- TIMER (RealSystem.cpp): the task list, which the timer thread runs while the game adds tasks whose state (the
  rumble's signal, the clock) was written just before. The timer thread reads each slot once (the original reads
  it again to call it).
- FILESYS (FileSys.cpp): a chunked transfer's `remaining` and `op`, which its caller waits on before reading the
  byte count and the data; and the worker, which now takes an op's callback, flags, handle, status and argument
  under the queue's lock before filing it as done (the original read them after, when another thread may already
  have completed the op and reused it for a new one).
- SndLockstep (devtools): the driver thread's "alive" flag.

The rest share a value and nothing else - the tick counts, the rumble speeds and flag, the sound buffers' samples
that the device reads behind the mixer as DirectSound's hardware did, and the worker's unlocked test of a cancel
bit - and are suppressed with their reasons in tools/tsan_suppressions.txt, as are lavapipe's and dxvk's own.
UBSan's float-cast-overflow (`-DNIGHTFIRE_SANITIZE=float-cast-overflow`) found no out-of-range conversion in 16
runs to tick 9000, nor with `-DNIGHTFIRE_SSE=OFF` (the C forms arm64 uses) in missions 1, 5 and 6 to tick 4500.

## 4. Floating point

- **Contraction.** Since clang 14 the default is `-ffp-contract=on`, which fuses `a * b + c` within an expression,
  at every optimisation level; on x86 nothing changed because the build targets no FMA, but arm64 always has
  `fmadd` [openfoam-fp, skia-fp]. The 170 `#pragma fp_contract(off)` are MSVC's spelling, so the arm64 preset
  passes `-ffp-contract=off` globally, and the check is mechanical: no `fmadd`/`fmsub`/`fnmadd`/`fnmsub` in the
  game's objects (`objdump -d` count, in CI's arm64 probe).
- **Denormals.** No game code sets MXCSR's FTZ/DAZ (only devtools read it); macOS arm64 runs with FPCR.FZ clear,
  and AArch64 NEON is IEEE for denormals in scalar and vector alike. They agree.
- **NaNs and min/max.** The game uses no `_mm_min`/`_mm_max`. `X87NaN` and `QuietNaN` already pick NaNs
  explicitly. Integer division by zero traps on x86 and gives 0 on arm64: only a bug would show it.
- **Out-of-range float-to-integer conversions** are undefined in C++ and differ (x86 0x80000000, arm64
  saturation). The X87.h helpers handle the ported sites; package M2 finds the rest with UBSan's
  `-fsanitize=float-cast-overflow` in a lockstep run on Linux x86-64.
- **The C trig of `NIGHTFIRE_X87=OFF`** calls the host's `sin`, `cos`, `tan`, `atan2`, `log`, `pow`, `acos`,
  `asin`. Apple's libm, glibc and the UCRT may differ in the last bit. When the x87 code became C, all 16 sound
  traces stayed identical to the x87 baselines (64-bit.md 4.4: only particles moved), so the simulation is not
  sensitive to these bits in these runs. If the Mac's traces still differ from `lbase_c`, the fix is to stop
  using the host's libm: correctly rounded functions (CORE-MATH, MIT-licensed, binary64 [core-math]) give the same
  bits on every host, at the price of one more baseline regeneration on every machine.

**What to expect.** Exactness across machines was never the bar (lockstep-machine-baselines): the Mac gets its own
baselines. With M0's SSE header (exact by construction, RSQRTSS from the runner's table), contraction off and the
host libm as the one uncontrolled input, the Mac's sound traces should match `lbase_c`'s, possibly exactly, and
that comparison is worth making once as a check of the port. Frames differ by GPU and renderer and are compared
only with `lmac_c`.

## 5. Platform layer

**HostPosix on Darwin** (and the Linux-only bits around it):

| Linux | Darwin |
|---|---|
| `MAP_FIXED_NOREPLACE` | absent: `mach_vm_allocate` with `VM_FLAGS_FIXED`, or a hinted `mmap` whose result is checked |
| /proc/self/maps (pages, crash report) | `mach_vm_region` |
| /proc/self/exe (xboxPath.cpp, launchInfo.cpp) | `_NSGetExecutablePath`, then `realpath` |
| `clock_nanosleep(TIMER_ABSTIME)` (the multimedia timer) | absent: `mach_wait_until`, or `nanosleep` to the deadline |
| `pthread_condattr_setclock(CLOCK_MONOTONIC)` | absent: `pthread_cond_timedwait_relative_np` |
| `sem_init` (Win32Harness.h) | unnamed semaphores are unimplemented: `dispatch_semaphore` |
| `dl_iterate_phdr` (SndCallTrace) | no use: see 6 |
| RWX pages (`PROT_READ \| PROT_WRITE \| PROT_EXEC` in MapAt/MapLow) | W^X: refused with the hardened runtime unless `MAP_JIT` [apple-mmap]; nothing executes there, so drop `PROT_EXEC` |
| ucontext registers (CrashReportPosix) | `uc_mcontext->__ss.__x[]`, `__pc`, `__sp`, `__lr` |

pthreads, `clock_gettime(CLOCK_MONOTONIC)`, `nanosleep` and POSIX files are the same. Thread priorities are
accepted and not applied, as on Linux. APFS is case-insensitive by default; HostPosix's component-by-component
case matching works either way. Paths are as on Linux.

**SDL3** (3.4.18, FetchContent as today): Cocoa window, CoreAudio output (the voice mixer and the movie stream are
unchanged), and pads through Apple's GameController framework and HID (Xbox and PlayStation pads over Bluetooth or
USB). Pads read while the window is in the background need `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` (GameController
delivers background events only when asked) - to confirm on the Mac. The keyboard pad works unchanged; F8/F9 need
fn on a Mac keyboard. SDL's GPU subsystem is switched on for this build.

**FFmpeg.** `tools/fmv/build_ffmpeg.sh macos-arm64 <prefix>`: the same 8.0.3 submodule and configuration,
`--arch=aarch64 --cc=clang`, shared, autodetection off as now (no VideoToolbox, to keep the decode the same as the
other builds'), install names `@rpath`. Ffmpeg.cpp opens `libavutil.60.dylib`, `libavcodec.62.dylib`,
`libavformat.62.dylib`. Homebrew's ffmpeg would work but drifts in version.

**Signing.** All arm64 code must carry a signature; Apple's linker (and lld) ad-hoc sign arm64 output
automatically [lld-sign]. A locally built executable has no quarantine attribute, so Gatekeeper does not stop it.
Anything rewritten after linking (`install_name_tool`) is re-signed with `codesign -s - -f`. Developer ID signing
and notarisation are only for handing a build to someone else (an open question).

**The preset.** `macos` is taken (the mingw cross build, docs/macos-build.md), so: `macos-arm64` - Ninja, Apple
clang from the Command Line Tools, `CMAKE_OSX_ARCHITECTURES=arm64`, `CMAKE_OSX_DEPLOYMENT_TARGET=15.0` (the
`math_mode` pragma), `NIGHTFIRE_SDL=ON`, `NIGHTFIRE_X87=OFF`, the new graphics backend, `-ffp-contract=off`, run
path `@executable_path`, targets `driving_standalone` and `driving_standalone_test` (and `gamewindow_test`). The
output folder holds the executables, libSDL3.dylib, SPIRV-Cross (static) and FFmpeg's three dylibs; settings.ini,
saves/ and the disc as on Linux. A `.app` bundle is not needed for development.

## 6. Testing on the Mac

- **Where.** The agent runs on the Mac: `RUNNER_HOST=local` (tools/runner/remote.sh's local mode) with a checkout
  of its own and the disc copied onto the Mac's disk. Optionally the Mac is also reachable from this PC by SSH
  (`RUNNER_OS=macos`, as `RUNNER_OS=linux` is today).
- **drive_game.sh / remote.sh on macOS.** BSD tools: `date +%s%N` (no `%N`), `timeout` and `nproc` are not in
  the base system (Homebrew's coreutils gives `gtimeout`/`gnproc`, or the scripts use `perl`/`sysctl -n
  hw.ncpu`); bash is 3.2 unless Homebrew's is used. No `NF_VK_ICD`/lavapipe/DXVK settings: the real GPU, headless
  (SDL's offscreen video driver, the dummy audio driver, the backend's offscreen target). `caffeinate -i` around a
  round keeps the Mac awake. Slots: 4-6 on the M2 Max to start.
- **The sound call trace.** SndCallTrace moved `call rel32` sites in the executable's code at start-up; arm64 has
  no such encoding and signed code pages cannot be made writable. Since package M3 (done) the test executable's
  sources outside sound/snd (and SndCallTrace.cpp) are compiled with a forced include, devtools/SndCallTraceRedirect.h,
  that names the SND entries' trace wrappers (`Traced_<entry>`) in place of the entries, so the redirection is made
  by the compiler and nothing is patched; with the trace off a wrapper only calls its entry. drivinginject keeps
  the run-time redirection (its originals' entries are the game's). `NIGHTFIRE_HEAP_PAD` and `NIGHTFIRE_X87COUNT`
  stay unavailable, as on Linux.
- **Baselines.** `lmac_c_*` (build/baselines), from the first build whose 16 runs match each other twice (two
  checkouts, as `lbase_c` was made). Each round also compares the sound traces with `lbase_c`'s (compare.py's
  trace comparison) and reports the first differing line, if any.
- **By eye.** The user plays a mission windowed with a pad and listens, as for the Linux and SDL builds.

## 7. Work packages

Sizes as in 64-bit.md: S < 1 day of agent time, M 1-2, L 3+. M0-M3 and G1-G2 run on x86 (this PC's WSL2/Docker and
the runner) before or beside the last 64-bit packages; A1-A4 and G3 run on the Mac. Two agents at a time fit them.

| # | Package | Where | Size | Test |
|---|---|---|---|---|
| M0 | **Done 15 Oct 2026 (3.1)**: asm identical on Win32 and x64 MSVC; sse_test 0 differ on the runner (x64, Win32); x64 with `-DNIGHTFIRE_SSE=OFF` 16/16 exact against `lbase_c`; Linux x86-64 forced-C lockstep waits for M1. **Portable SSE**: platform/Sse.h with the 34 intrinsics the game uses (x86: the intrinsics themselves; elsewhere: C with SSE's exact semantics); RSQRTSS by a table generated on the runner from its own instruction (the input bits it depends on found by sweep); `Truncate`/`RoundToInt` in C; `__rdtsc` as a 733 MHz host cycle count; `__movsd`, `_mm_empty`, `fldcw` guarded | x86 | M | 32-bit and x64 MSVC: per-function asm identical; a devtools test of the C forms against the instructions (random and edge inputs, all four RSQRTSS exponent parities); Linux x86-64 built with the C forms forced on: lockstep exact against `lbase_c` on the runner |
| M1 | **A non-zero window base**: `kGameWindowBase` configurable, null as the word 0, the window [4 GB, 8 GB) on Linux x86-64 (executable linked into it) and Windows x64 (`/BASE:0x140000000` is already there); AnimMisc's pointer-as-float through the word. **Done 15 October 2026** with 64-bit package 10 (64-bit.md 7.11, section 2 above): `NIGHTFIRE_WINDOW_BASE`, thread stacks in the window, resolver numbers | x86 | S-M | `gamewindow_test`; lockstep exact against `lbase_c` with the base at 4 GB. **Passed**: gamewindow_test 0 differ on Windows x64 and Linux x86-64, both bases; Windows x64 16/16 exact against `lbase_c` at 4 GB and at 0; Linux x86-64 16/16 exact against `lwsl_c` at 4 GB and at 0 |
| M2 | **FP and thread audit**: `-ffp-contract=off` in the non-MSVC builds; an arm64 compile probe in the nf-cross image (`--target=aarch64-linux-gnu`, Ubuntu's arm64 headers) for both standalone targets, with the fused-op count; UBSan float-cast-overflow and ThreadSanitizer lockstep runs on Linux x86-64; the races and conversions fixed. **Done 16 Oct 2026** (3, 4 and the notes below): contraction off; probe 0 errors, no unexpected fused operations; UBSan 0 reports; the TSan races that hand data over fixed, the value-only ones listed (tools/tsan_suppressions.txt) | x86 | M | probe: 0 errors, 0 fused ops; UBSan/TSan clean; lockstep exact against `lbase_c`. **Passed**: see the notes below the table |
| M3 | **Trace redirection without code patching** (6). **Done** 15 Oct 2026: SndCallTraceRedirect.h, a forced include on driving_standalone_test's sources outside sound/snd (a per-source option conditional on the target, CMakeLists.txt); the standalone build's call-site moving (PE and `dl_iterate_phdr`) removed. One difference by design: a call to an entry the linker folded with another (SNDSYS_restore/SNDSYSI_exithook under MSVC) is now traced, where the moving left it alone; no run reaches it | x86 | S | traces identical to the patched ones on Windows and Linux: runner, 32-bit X87=OFF 16/16 and x64 16/16 exact against `lbase_c` (frames and traces), drivinginject (unchanged mechanism) 8/8 against `lbase`; WSL2 Linux i386 (lavapipe, X87=ON), the patched build (60f3014) against this one: m1/m2 idle and accelerating to tick 4500, traces and frames identical |
| G1 | **SPIR-V lowering** of shaderBuilder's graph (`NoContraction`, `Fma` for NV2A MADs), the fixed-function shaders (pretransformed, immediate, movie quad), FXAA's SPIR-V | x86 | M | `spirv-val` on every program the 16 runs create (a dump mode like shadercheck's) **Done 16 Oct 2026** ([sdl-gpu.md](sdl-gpu.md)): 99 programs from the logs and Driving.xbe (178 modules with point-sprite variants) and every module a mission-1 run creates (47) pass `spirv-val --target-env vulkan1.0`; the D3D9 tokens of all 99 unchanged by the shared `ShaderBuilder::Fuse`; `Fma` where the D3D9 lowering writes a mad (the compiler-fusion rule), not only NV2A MADs |
| G2 | **The SDL GPU backend** behind d3d9Backend.h (`NIGHTFIRE_GFX=sdlgpu`): pipeline and sampler caches, formats, streams and the ring buffer, palettes, YUV, render targets, readback for the dumps, gamma, offscreen mode for lockstep; on Windows/Linux through SDL's Vulkan backend | x86 | L | lockstep sound traces unchanged (the renderer is below them); frames against the D3D9 build's by eye and by a pixel-difference threshold; two runs identical **Done 16 Oct 2026** ([sdl-gpu.md](sdl-gpu.md)): Windows x64 on the runner (Vulkan, RTX 3090): 16/16 lockstep runs with sound traces identical to `lbase_c` to tick 9000, frames within 1-17k px of the D3D9 build over 16 frames (by eye: the same), a second round 0 px different; Linux i386 in WSL2 (lavapipe): m1/m2 idle and accelerating traces identical to `lwsl`, two runs identical. Windowed presenting is untested (needs a window) |
| A1 | **macOS bring-up**: the `macos-arm64` preset; HostPosix's Darwin rows (5); xboxPath/launchInfo; CrashReportPosix for arm64; build_ffmpeg.sh macos-arm64; `kGameWindowBase` 0x100000000 with its start-up check | Mac | M | `gamewindow_test` (1,000,000 operations, everything inside the window); the test executable boots to the first tick headless. **Done 15 Oct 2026 (7.1)**: 0 differ, image at 4 GB plus 2-75 MB over 12 launches; all 16 lockstep runs to tick 9000 |
| G3 | **The Metal leg**: SPIRV-Cross to MSL with the `math_mode(safe)` prefix, Apple formats (D32F_S8, swizzles), swapchain on the main thread if needed. **Prepared on x86 (16 October 2026, [sdl-gpu.md](sdl-gpu.md) "G3 on x86")**: SPIRV-Cross `vulkan-sdk-1.4.363.0` by FetchContent in every sdlgpu build; the backend hands SDL MSL when the device takes it (SDL's slots kept, the pragma first, NoContraction as SPIRV-Cross's fma helpers, the multiply's -0 fixed); `spirvmsl` checks all 178 modules (0 differ); vertex streams padded to 4-byte elements and strides on every host (runner x64 Vulkan 16/16: traces identical to `lbase_c`, frames 0 px against G2's); no `SDL_WINDOW_VULKAN` on macOS; formats and the swapchain needed nothing. **Left for the Mac**: compiling the MSL (no Metal compiler on x86), the first frames, two runs identical, the helpers' speed - the checklist in sdl-gpu.md | Mac | S | the first frames dumped; two runs identical. **15 Oct 2026 (7.1)**: every module compiles (`mslcompile`), frames right by eye; two runs' frames differ by up to ~500 scattered pixels a run (left open) |
| A2 | **Mac test tooling**: drive_game.sh/remote.sh on macOS, local slots, `lmac_c` baselines, the comparison with `lbase_c` traces | Mac | S | 16 runs to tick 9000 twice, identical; the trace comparison reported. **15 Oct 2026 (7.1)**: the tooling works (test-runner.md); sound traces identical round to round; no `lmac_c` yet (the frames), and `lbase_c` is not on the Mac for the comparison |
| A3 | **Play**: windowed SDL/Metal, CoreAudio, pads (background events), keyboard; the user's playing and listening check | Mac | S | the user |
| A4 | (only if A2's traces differ from `lbase_c`) **Portable libm**: correctly rounded functions for the C trig on every host; new `lbase_c`/`lloc_c`/`lmac_c` once | x86 + Mac | M | traces identical across the runner, Linux and the Mac |

**Package M2's tests (16 October 2026).**

- **Contraction off.** On Linux i386, `-ffp-contract=off` changes instruction order only, in 167 of 7,115
  functions. There was no FMA there to begin with. m1/m2 to tick 4500 gave the same traces and frames as before.
- **The arm64 probe.** tools/arm64_probe.sh in the `nf-cross-arm64` image (nf-cross plus g++-aarch64-linux-gnu,
  binutils-aarch64-linux-gnu and lld) compiles both targets' 571 objects for aarch64-linux-gnu with 0 errors, after
  `YieldProcessor` became `YIELD` there (src/common/linux/win32Calls.h). The objects hold 6 fused operations, all
  in code that asks for one: `ShaderBuilder::Mad`'s literal folding by `fmaf`, and `Crt::CIpow`'s inlined
  `fmod(y, 2.0)`, which LLVM expands exactly with one FMADD.
- **Sanitizers.** `-DNIGHTFIRE_SANITIZE=<sanitizer>` instruments the standalone executables only. The runtime
  comes from Clang's shared library, so that the game's `operator new` and `delete` take precedence. Under
  ThreadSanitizer, the WSL2 runs also need `LD_LIBRARY_PATH=.:Release`, because its `dlopen` misses the
  executable's run path for FFmpeg. Rounds use `RUNNER_BUILD` pointing at the sanitizer build, with
  `ENV='UBSAN_OPTIONS=log_path=...'` or `ENV='LD_LIBRARY_PATH=.:Release;TSAN_OPTIONS=log_path=...:suppressions=<checkout>/tools/tsan_suppressions.txt'`.
- **Lockstep after the fixes.**
  - Linux x86-64 in WSL2: 16/16 exact against `lwsl_c` to tick 9000, as were the UBSan build (16/16, 0 reports)
    and the ThreadSanitizer build (16/16 to tick 1000, 0 reports after the fixes and suppressions).
  - On the runner: the 32-bit standalone build with `NIGHTFIRE_X87=OFF` and the x64 one, each 16/16 exact against
    `lbase_c`.
  - The full injected round: shadows on all 8 missions with 0 differences, and 16/16 exact against `lbase`.
- **Built.** MSVC Win32 (all four driving targets), MSVC x64, clang linux-i386 and linux-x64, the mingw cross build
  (nf-cross, `linux` preset), and the arm64 probe.
- **Left for the Mac.** Real weak-memory behaviour can only show on arm64 itself. ThreadSanitizer on x86-64 finds
  the C++ races, which is what matters there, but its runs reach only tick 1000.

M0, M1 and M3 can start now (they touch only 32-bit-neutral code and the x64 build); M2 after package 10; G1 and G2
any time (G2 needs SDL's GPU subsystem in the Windows build, off by default). A1 needs M0-M2; G3 needs G2.

### 7.1 Done on the Mac (15 October 2026, M2 Max, macOS 27.0.1, Apple clang 21)

- **Built**: `cmake --preset macos-arm64 -DNF_FFMPEG_LIB_DIR=$HOME/ffmpeg-arm64/lib` (FFmpeg from
  `build_ffmpeg.sh macos-arm64`), then both standalone executables, `gamewindow_test`, `spirvmsl` and `mslcompile`.
  The whole game compiled for arm64 with 13 errors, all in the platform layer and the harness. Fused operations in the
  test executable: 3, none of them a contraction (ShaderBuilder's explicit `fmaf` twice; `fmod(y, 2.0)` in `CIpow`,
  whose product by 2 is exact).
- **The window at 4 GB** works as planned, with one change: the arena's 1 GB is a zero-fill section of the image
  (HostPosix.cpp), because before `main` Darwin puts its malloc regions and the main thread's stack at random in the
  ~1.9 GB between the image and the shared cache (0x1'8000'0000), and 1 GB in one piece was missing in 2 launches of 3.
- **Headless**: SDL's Metal GPU driver needs a video driver that makes Metal views, which the offscreen one does not.
  A run with `SDL_VIDEO_DRIVER=offscreen` gets Cocoa instead, as a background application whose window is never
  shown (HostWindowSdl.cpp), so the scripts are the same on every host. Such a run needs the user's login session.
- **Metal**: the MSL compiles; Metal also matches a pixel program's inputs to the vertex outputs by type, so the pixel
  inputs are declared as the interface's float4s (FXAA's float2 had its pipeline refused). Every module compiles with
  Metal's run-time compiler (`mslcompile`, 184 from the logs and Driving.xbe, 47 from a run). Frames are right by eye.
- **SDL 3.4.18**: 3.2.30's Metal driver never reused a command buffer's transfer and uniform buffers once the backend
  released the fence it had waited for, so every batch took new GPU memory - 307 GB of footprint after 100 s, which
  twice made the Mac unusable when games were left running. 3.4's fences fixed it; the pin moved for every host
  (untested on Windows and Linux so far). A round on 3.4.18: sound traces identical to the 3.2.30 round's in all 16.
- **Rounds**: the 16 lockstep runs take 7-9 minutes on 3 slots. **Run at most 3 at once, at background priority**
  (drive_game.sh does): 6 unpaced games starved the desktop. Sound traces are identical round to round. Frames are
  not: 1-524 scattered pixels a run over its 16 frames, by up to ~20 levels, on foliage and snow (mission 5 most) -
  probably alpha-tested edges falling either side of the test. The Vulkan build gives 0 between rounds; the Mac is the
  only host whose addresses change from run to run (the image slides), which is the first thing to rule out.
  Left open by the user's decision (small, by eye invisible); `lmac_c` waits for it, or for frames to be compared
  with a threshold here.
- **CI** (`macos-arm64` in .github/workflows/ci.yml, GitHub's macos-26 Apple Silicon image): the preset's build with
  FFmpeg from the submodule (cached by its commit), `gamewindow_test`, and `tools/fused_ops.sh` on both executables
  (fused multiply-adds only where the source asks for them). No game runs there: they need the disc.
- **Not done**: the comparison of the Mac's traces with `lbase_c` (the baselines are on the PC: copy
  build/baselines/lbase_c_* over), A3, and the x86 checks of these changes (the Windows and Linux builds, CI).

### 7.2 Parked (16 October 2026)

The user judged the Mac good enough for now and parked these; pick them up from here.

1. **Frames differ slightly between rounds on the Mac** (sound traces are identical): 1 to ~500 scattered pixels a
   run over its 16 frames, by up to ~20 levels, on foliage and snow (mission 5 most) - alpha-tested edges falling either
   side of the test is the likely mechanism. The Vulkan build is exact between rounds; the Mac is the only host whose
   image loads at a different address each run (ASLR slide), so the first experiment is two rounds with the image at
   one address (or runs grouped by their logged load address). Until it is fixed, `lmac_c` cannot hold frames: either
   fix it, or compare the Mac's frames with a threshold and keep exact sound traces.
2. **The Mac's sound traces against `lbase_c`** are not compared yet: copy `build/baselines/lbase_c_*/sndtrace.log`
   from the PC (only the traces are needed). If they differ, `lmac_c` baselines are enough (the user's decision, 10);
   A4 stays dropped.
3. **A3, play**: windowed Metal, CoreAudio and keyboard work (the user played it; the movie-sound bug found then is
   fixed - 64-bit FFmpeg layout, a29370a). Not yet checked: pads, including SDL's background pad events.
4. **The no-contraction shader helpers' speed** (sdl-gpu.md's checklist): ~300 fps headless, so not a problem so far.
5. Not run on the Mac: the x86 checks of later Mac commits run on the PC before they reach dev (as for 3.4.18 and
   a29370a: every x86 build exact).

## 8. The agent on the Mac: first steps

1. Check the toolchain: `xcode-select -p`, `clang --version` (Apple clang 17 or later), `cmake --version` (3.21+),
   `ninja`, `python3`; `sw_vers` (macOS 15 or later); `sysctl -n machdep.cpu.brand_string`.
2. Check out the branch into a folder of its own; copy the disc onto the Mac's disk (not a network share) and point
   DiscPath at it.
3. Build and run `gamewindow_test` first: it shows where the image, the shared cache and the window's
   allocations land, which proves or breaks decision 2 before anything else is built. Print the image's address
   over a few launches (ASLR).
4. Build FFmpeg (`build_ffmpeg.sh macos-arm64 ~/ffmpeg-arm64`), then `cmake --preset macos-arm64
   -DNF_FFMPEG_LIB_DIR=...` and `driving_standalone_test`; count fused operations in its objects (tools/arm64_probe.sh's
   count: only `ShaderBuilder::Mad`'s fmaf and `Crt::CIpow`'s inlined fmod are expected).
5. Boot mission 1 headless with lockstep on to tick 500, dump frames; then the full A2 round.
6. Never open a window while the user works on the Mac unless asked; headless runs need no window.

## 9. Prerequisites the user installs on the Mac

- macOS 15 or later (the docs record an M2 Max on macOS 26.6.2, 20 September 2026).
- Xcode Command Line Tools (`xcode-select --install`); full Xcode is not needed (no offline Metal compile).
- Homebrew: `brew install cmake ninja pkg-config python3 coreutils` (and `bash` for a modern shell in the scripts).
- Claude Code, and the repository's GitHub access.
- The disc copied onto the Mac.
- Optional: an Xbox or PlayStation pad; Remote Login (SSH) if this PC should drive rounds; "Developer Tools"
  for the terminal in Privacy & Security (spares Gatekeeper's scan of every new build).

## 10. Decisions (the user, 15 October 2026)

1. **Graphics: the SDL GPU backend** with SPIR-V (G1, G2, G3). dxvk is not used on the Mac.
2. **Exactness: `lmac_c` baselines are enough.** A4 (a portable libm) is dropped unless the user asks again; the Mac
   is compared with itself, and its sound traces against `lbase_c` are only reported.
3. **The Mac runs its own rounds locally**; nothing drives it from the PC.
4. **macOS 15 or later.**
5. Still open: distribution (ad-hoc signed development builds for now), and whether the SDL GPU backend later replaces
   dxvk-native on Linux and becomes selectable on Windows.

## 11. Brief for the agent on the Mac

Read this file, docs/driving/64-bit.md, docs/driving/portable.md, docs/test-runner.md and CLAUDE.md. Work on this
branch (driving-macos), committing as you go and pushing it when a package passes its test; the user merges.
Start with section 8's first steps. Packages, in order: whichever of M0-M3, G1 and G2 are not marked done in section 7
(they can run on the Mac too - the x86-only tests become "x64 build unchanged" checks you cannot run there, so note
them for the PC), then A1, G3 (what is left of it: sdl-gpu.md, "For the agent on the Mac"), A2, A3. The bar: `driving_standalone_test` runs all 16 lockstep runs headless on the
Mac, twice, identical to each other; `lmac_c` baselines made from them; the sound-trace comparison with `lbase_c`
reported; then a windowed run for the user (A3) only when the user asks. Keep the Windows, Linux and x64 builds
building (CI). Never open a window while the user works unless asked.

## Sources

- [dxvk-wiki] DXVK wiki, Driver support (required Vulkan 1.4 features and extensions): https://github.com/doitsujin/dxvk/wiki/Driver-support
- [d9vk-mac] highball-engine, "d9vk: DXVK 3.1 macOS fork", 16 Sept 2026: https://github.com/gauthierpiarrette/highball-engine/releases/tag/d9vk-dxvk-macos-3.1-20260916
- [mvk-guide] MoltenVK Runtime User Guide (Vulkan 1.4, supported extensions): https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- [apple-tf] Apple Developer Forums, "Does Metal support Transform Feedback?": https://developer.apple.com/forums/thread/21104
- [gcenx-dxvk] Gcenx/DXVK-macOS releases: https://github.com/Gcenx/DXVK-macOS/releases
- [cw-dxvk] CodeWeavers, "Will DXVK (Upstream) run on Mac or Linux?": https://www.codeweavers.com/compatibility/crossover/dxvk
- [cw-d3dmetal] CodeWeavers, Advanced Settings in CrossOver Mac 26: https://support.codeweavers.com/en_US/advanced-settings-in-crossover-mac-26
- [kegworks] Kegworks: https://github.com/gxgl/Kegworks
- [whisky] heise, "Open source tool Whisky stops further development": https://heise.de/-10355657
- [sdl-gpu] SDL3 wiki, CategoryGPU: https://wiki.libsdl.org/SDL3/CategoryGPU
- [dawn-mathmode] Dawn, "MacOS use relaxed math where possible" (the `math_mode` pragma): https://dawn.googlesource.com/dawn/+/58c1c685a8dab745a8c22d231b7b1cd68bfc977a%5E%21
- [apple-mathmode] Apple, MTLCompileOptions.mathMode: https://developer.apple.com/documentation/metal/mtlcompileoptions/mathmode
- [apple-pagezero] Apple Developer Forums, "macOS 11 ARM64: allocate memory in 32-bit address space": https://developer.apple.com/forums/thread/655950
- [obs-luajit] OBS Studio PR 4105 (`-pagezero_size` kills the app on Apple Silicon): https://github.com/obsproject/obs-studio/pull/4105
- [hn-pagezero] Hacker News, on PAGEZERO's minimum on ARM64: https://news.ycombinator.com/item?id=43438602
- [apple-arm64] Apple, Writing ARM64 code for Apple platforms: https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
- [ocallahan-rsqrt] R. O'Callahan, "Emulating AMD approximate arithmetic instructions on Intel", 2021: https://robert.ocallahan.org/2021/09/emulating-amd-rsqrtss-etc-on-intel.html
- [openfoam-fp] OpenFOAM issue 2958, "Add -ffp-contract=off for Clang 14 and later": https://develop.openfoam.com/Development/openfoam/-/issues/2958
- [skia-fp] skcms, "Fix issues with newer clang" (fp-contract on by default since clang 14): https://skia.googlesource.com/skcms/+/b25b07b4b07990811de121c0356155b2ba0f4318
- [core-math] P. Zimmermann, CORE-MATH (FPBench 2023; MIT licence): https://members.loria.fr/PZimmermann/talks/core-math-fpbench2023.pdf
- [apple-mmap] Apple Developer Forums, allow-jit vs allow-unsigned-executable-memory (mmap man page on W^X): https://developer.apple.com/forums/thread/776290
- [lld-sign] lld-macho, "Only codesign by default on arm64 macOS": https://llvm.googlesource.com/llvm-project/lld/+/f87957fbe24e7cbdfc0351f5bef27542c2f06113%5E%21/
- [sse2neon] sse2neon (considered for M0 and not taken: its RSQRTSS and conversions are not SSE's bits without care): https://github.com/DLTcollab/sse2neon
