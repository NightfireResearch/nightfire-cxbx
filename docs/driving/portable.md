# Driving engine: a portable build

Status: level 1 builds and runs on Linux (15 October 2026, "Step 4 as done" below). Follows the standalone build
(docs/driving/standalone.md), which it assumes: an executable built from our source alone, with no Driving.xbe mapped
and no injection.

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
is emulated; and DXVK on macOS goes through MoltenVK, whose D3D9 coverage is incomplete. Planned after level 1, in
two steps (see the roadmap below).

## Roadmap (the user's order, 14 October 2026)

1. Driving runs 100% standalone on Windows with no dependence on Driving.xbe; then the stack merges back to dev.
2. Platform independence, still 32-bit x86 (level 1 above: Linux with dxvk-native and SDL3).
3. 64-bit: pointer-sized layouts and offset-based loading of the data files (audit and plan: [64-bit.md](64-bit.md)).
4. Architecture independence (Apple Silicon and other non-x86 hosts): the x87 code as portable C, and graphics there
   (plan for macOS: [macos.md](macos.md)).

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

## Steps 2 and 3 as done (14 October 2026)

**The platform layer** is `src/driving/platform/Host.h`: threads (create, resume, exit, priority, exit code, id), TLS,
a recursive lock kept in place (`HostLock`, 24 bytes), interlocked increment and decrement, events and waits, sleeping,
heaps, files (open, read, write, seek, size, delete, directory search), `GetPrivateProfileString`, the performance
counter, `timeGetTime`, the multimedia timer, local time and time zone, the command line, environment variables, and
the process odds and ends (exit, debug break, `RaiseException`, `IsBadReadPtr`/`IsBadWritePtr`). It speaks Win32's
numbers (handles, error codes, flags), which XAPI shares, so XAPI passes them straight through.

- `HostWin32.cpp` is the Win32 call each site made before, one for one.
- `HostPosix.cpp` is pthreads, `clock_gettime` and POSIX files. Handles are objects of its own, and errors are Win32
  numbers kept per thread. Not exactly Windows: priorities are accepted but not applied, share modes are not
  enforced, `IsBadWritePtr` only tests that the memory can be read, `RaiseException` reports and aborts (there is no
  SEH), and the time zone has no transition dates. It builds to an i386 object in the nf-cross image with
  `clang++ -m32 -isystem /usr/include/x86_64-linux-gnu` and an empty `gnu/stubs-32.h`; the image has no i386 libraries
  to link with.

The game's ports call XAPI as the originals did: FILESYS, THREAD, SIGNAL, MEM's heap check and the XAPI timer go
through `Xbox_*` (XboxXapi.cpp, XboxStartup.cpp), and those call the host. The C runtime's heap and locks, LaunchOptions'
command line, PlayMPC's archive search, the Scheduler's idle sleep and the sound seam's clock call the host directly.

**What still calls Win32 in src/driving**, and why:
- the injection's patch writers (`WriteJump`/`PatchSite`: `VirtualProtect` and `GetLastError` in XboxXapi, XboxStartup,
  XboxTimer and XboxInput), which write into the mapped XBE and have no meaning without it;
- the output backends behind the build option: `xaudio2Driving.cpp` (XAudio2, COM) and `HostInputWin32.cpp` (XInput,
  `GetAsyncKeyState`, the foreground window), plus `HostWindowWin32.cpp` for a standalone build without SDL;
- devtools, which are Windows-only test code.

Outside src/driving, src/common still uses Win32: `xboxPath.cpp` (module path, `GetFullPathNameA`),
`fmv/Ffmpeg.cpp` (`LoadLibrary` of the FFmpeg DLLs), `fmv/FmvAudio.cpp` (XAudio2), and `gfx/d3d9Backend.cpp` and
`textureReplace.cpp` (settings, directories, the window when SDL is off). The loader, `dllmain.cpp` and `launchInfo.cpp`
are the injected build's own and are not part of a standalone or Linux build.

**SDL3** (`-DNIGHTFIRE_SDL=ON`; the default is off, and off builds exactly as before) is built from source:
FetchContent of SDL's git repository at the tag `release-3.2.30`, shallow, with SDL's render, GPU, camera, sensor,
dialog and tray subsystems off. One source serves MSVC, the mingw cross build and a future Linux `-m32` build alike.
`NF_SDL3_DIR` takes an installed SDL3 CMake package instead, for a machine without network access. `SDL3.dll` is
copied beside drivinginject.dll. With the option on:
- **Window** (`HostWindowSdl.cpp`). The injection DLL exports `NightfireHost_OpenWindow` and `NightfireHost_RunWindow`.
  The loader looks for them: when they are there it opens that window and runs its events on its main thread in
  place of its own Win32 window. The action engine's DLL and SDL-off builds do not export them. The window class is
  still `NightfireRender` (`SDL_RegisterApp`), so drive_game.ps1 and the devtools find the window as before. The D3D9
  backend takes the window from `GfxHost_Window` and sizes it with `GfxHost_SizeWindow`, which runs on the window's
  thread (`SDL_RunOnMainThread`). On other hosts `HostWindow_Native` is the `SDL_Window`, which is what dxvk-native
  takes as an HWND.
- **Input** (`HostInputSdl.cpp`). SDL gamepads are given in XInput's numbers: the same button bits, triggers turned
  back into XInput's byte, and stick Y inverted bit for bit as SDL inverts XInput's. A pad takes its player index as
  its port. The keyboard pad uses the same keys as before. Pads are read even when the window is in the background,
  as XInput reads them. XboxInput.cpp (the XAPI translation, the keyboard pad and `NIGHTFIRE_HOLD`) is shared by
  both paths through `HostInput.h`.
- **Sound output** (`sound/sdlAudioDriving.cpp`). A voice mixer of ours sits behind the same `DrivingAudio.h` the
  XAudio2 body implements. Each voice reads the game's buffer in place (8- or 16-bit PCM, Xbox ADPCM decoded a block
  at a time) at its SetFrequency rate with linear interpolation, applies its volume and the stereo pair its mixbins
  make (the XAudio2 body's matrix), and is mixed into float stereo at 48 kHz. The mix is made in 4 ms chunks on SDL's
  audio thread. The play cursor is where the current chunk began, as with XAudio2. A restart or seek takes effect at
  the next chunk; XAudio2 instead lets two queued chunks (8 ms) play out first. The movies get a stream of their own
  on the same device (`common/fmv/FmvAudioSdl.cpp`), folded to stereo with the same matrix.

**Tests** (vr-desktop, slots 9-16, against `lbase`):
- Round p2, option off: all 16 lockstep runs exact (every frame, every sound-trace line to tick 9000), and the
  shadow runs on all eight missions show 0 differences.
- Round s3, option on (`RUNNER_CMAKE_ARGS=-DNIGHTFIRE_SDL=ON`): all 16 lockstep runs exact. The logs confirm SDL 3.2.30
  (window, wasapi output with 480-frame device blocks) and the mixer carrying sound.
- The first s3 attempt hung 14 of the 16 runs in each mission's opening movie. A stream from
  `SDL_OpenAudioDeviceStream` refuses other streams on its device, so the movie's stream never bound and its clock
  never moved. The mix now opens its own device and binds its stream to it, and a movie whose stream does not bind
  plays on the wall clock.
- drivinginject also builds with the mingw cross toolchain (the nf-cross `linux` preset), option off.

**Left**:
- Step 4: done, below.
- The movie stream's clock ignores the device's own buffer (about 10 ms).
- `HostPosix`'s `IsBadReadPtr` makes two system calls per page, and MEM_validate calls it for every free block once
  a loop.

The user's listening check of the SDL build (14 October 2026) passed after one fix: the movies' 5.1 was folded in FFmpeg's channel order rather than EA's, so their dialogue played on the right speaker only - in the XAudio2 build too (74b43b0, docs/driving-fmv.md).

## Step 4 as done: the Linux preset (15 October 2026)

`driving_standalone` and `driving_standalone_test` build as native 32-bit x86 Linux executables (ELF, glibc, no
Wine) with the `linux-i386` preset, and run a mission: boot, load, the intro movie, the simulation with D3D9 on
dxvk-native, SDL3's window, input and sound output, frames dumped, and the sound call trace in lockstep.

### Building

Prerequisites on an x86-64 Debian or Ubuntu (24.04 here, in WSL2):

```
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install clang lld cmake ninja-build pkg-config python3 gcc-multilib g++-multilib libc6-dev-i386 \
    make nasm libvulkan1:i386 mesa-vulkan-drivers:i386 \
    libx11-dev:i386 libxext-dev:i386 libxrandr-dev:i386 libxcursor-dev:i386 libxfixes-dev:i386 libxi-dev:i386 \
    libxss-dev:i386 libxtst-dev:i386 libxkbcommon-dev:i386 libwayland-dev:i386 libwayland-bin libegl-dev:i386 \
    libdecor-0-dev:i386 libasound2-dev:i386 libpulse-dev:i386 libpipewire-0.3-dev:i386 libudev-dev:i386 \
    libdbus-1-dev:i386
sh tools/fmv/build_ffmpeg.sh linux-i386 ~/ffmpeg-i386      # once: FFmpeg 8.0.3 as i386 shared libraries
cmake --preset linux-i386 -DNF_FFMPEG_LIB_DIR=$HOME/ffmpeg-i386/lib
cmake --build --preset linux-i386                           # driving_standalone and driving_standalone_test
```

The SDL3 development packages only make SDL's X11, Wayland, PulseAudio, PipeWire and ALSA backends available; SDL is
loaded from beside the executable. The output folder (build/linux-i386) holds the executables, `libdrivinghost.so`,
`libSDL3.so.0`, `libdxvk_d3d9.so.0` and FFmpeg's three libraries, found through the executables' run path
(`$ORIGIN`). Beside them at run time: settings.ini and saves/ as on Windows, and the disc where DiscPath says.

- **SDL3**: the same FetchContent of `release-3.2.30` as on Windows, built here for i386 (`NIGHTFIRE_SDL` is on in
  the preset; a build without it is refused, the other backends being Win32's).
- **dxvk-native**: the release tarball `dxvk-native-3.1.1-steamrt-sniper.tar.gz` (15 September 2026), fetched by
  CMake and checked by its SHA-256. It carries i386 libraries (`usr/lib32`) beside the x86-64 ones, built for the
  Steam runtime (glibc 2.31 and later), so nothing of dxvk is built here; `NF_DXVK_DIR` takes an extracted release
  instead. Its headers (`usr/include/dxvk`) are the D3D9 headers the backend compiles against.
- **FFmpeg**: the third_party/ffmpeg submodule's 8.0.3 with the Windows build's configuration, built by
  `tools/fmv/build_ffmpeg.sh linux-i386 <prefix>` (gcc -m32) into a folder outside the repository; the committed
  headers fit it. `Ffmpeg.cpp` opens `libavutil.so.60`, `libavcodec.so.62` and `libavformat.so.62` with dlopen.

### The two ABIs

The game's code is not compiled for Linux's C++ ABI. It depends on MSVC's: methods are `__thiscall` and the code
calls game vtables through `__fastcall` pointers with a dummy EDX (FnAnim's AnimVCall, URefCounter's tree helpers,
the particle and texture-context vtables...), the register adaptors and the checks against the XBE's conventions
(XBE_ABI_CHECK) assume it, doubles and 64-bit integers are 8-aligned in structures (4 in the System V i386 ABI), and
bitfields are laid out MSVC's way. Clang gives exactly that for its MinGW target, and the triple
`i686-pc-windows-gnu-elf` makes it emit ELF objects that link into a Linux executable. So
(`cmake/xbox-abi-linux.cmake`) the game's sources are compiled for that triple, with Linux's own headers and
predefined macros in place of Windows' (`_WIN32` and the MinGW macros undefined, `__linux__` defined, wchar_t 32-bit,
no stack probes, and the clang resource directory's MSVC `<intrin.h>` replaced by src/common/linux/intrin.h), and
linked by the ordinary Linux driver. A `__main` stub stands for the MinGW start-up's (StandaloneMain.cpp).

The sources that talk to Linux libraries are compiled for Linux, into `libdrivinghost.so`: the host layer
(HostPosix.cpp), SDL's window, input and sound output, FFmpeg's loader, xboxPath.cpp and the whole D3D9 backend
(common/gfx) with src/common/linux/win32Calls.cpp. Between the two sides:

- only free functions are called, and no structure crosses whose layout differs (Host.h, HostInput.h, HostWindow.h,
  DrivingAudio.h, d3d9Backend.h and backendHost.h hold none). A class whose methods both sides call stays on the game
  side: FmvAudio (FmvAudioSdl.cpp calls only SDL's C functions).
- the host side is a shared library linked with `-Bsymbolic-functions`, because an inline function or template
  instance compiled on both sides has the same name on both, with different conventions: each side must call its
  own. For the same reason the game side must not use a standard library member compiled into libstdc++.so: it
  calls only free functions there (operator new, `std::__throw_*`, the red-black tree helpers). The lockstep
  harness's `unordered_map`s became `map`s and its `std::string` a `vector<char>` for this. `nm -D --defined-only` on
  the executables lists nothing of the standard library, which is the check.
- FFmpeg's headers are read with `#pragma pack(4)` off Windows (Ffmpeg.h), so that PlayMPC sees its structures with
  the libraries' layout.
- the game keeps Windows' 4-byte stack alignment, where Linux code assumes 16 and keeps SSE values on the stack: the
  host library and SDL are compiled with `-mstackrealign`, and FFmpeg too (build_ffmpeg.sh). dxvk is only called from
  the host library. glibc and libstdc++ (Ubuntu's i386 builds) are called from the game directly.

Floating point is SSE2 throughout (`-msse2 -mfpmath=sse`, as MSVC's /arch:SSE2), and every thread the host layer
starts - the game's, the multimedia timer's - and the main thread (HostSetCommandLine) set the x87 control word to
Windows' (0x027f: 53-bit precision, round to nearest, exceptions masked). Linux starts threads at 64-bit precision,
and the x87 helpers' results would differ.

### What changed in the sources

- `platform/Win32Types.h`: windows.h on Windows; elsewhere the Win32 types, structures and numbers the XAPI layer
  and its callers use, as 32-bit Windows defines them (DWORD unsigned long, BOOL int...). It replaces `<windows.h>`
  in PlayMPC, d3dSeam, CRuntime, CRuntimeStd, FileSys and the Real* and Xbox* platform files.
- `devtools/Win32Harness.h`: the Win32 calls of the lockstep harness (Teleport, SndLockstep, SndCallTrace) over the
  host layer, POSIX semaphores and atomics off Windows. SndCallTrace's moving of call sites finds the executable's
  code segments with dl_iterate_phdr where Windows reads the PE sections. Teleport's F8/F9 go through
  HostInput_KeyHeld (new kKeyF8, kKeyF9) and HostInput_WindowHasFocus.
- HostPosix: paths take either separator and are matched without regard to case component by component (a disc
  copied onto a Linux file system keeps its own case); contiguous pages are placed at the lowest free 64 KB
  boundary below 0x10000000, as the Win32 body places them (the D3D resources' data words are those addresses);
  threads get at least the 1 MB stack an MSVC executable's threads reserve (the game's thread asks for 64 KB); the
  x87 control word above.
- src/common: `xboxPath.cpp` (the executable's folder from /proc/self/exe, realpath, mkdir), `fmv/Ffmpeg.cpp`
  (dlopen), `launchInfo.cpp` (the hand-over to the next part of a mission starts this executable again; there is no
  action engine here), `gfx/d3d9Backend.cpp` and `gfx/textureReplace.cpp` (settings, the clock, directories through
  src/common/linux/win32Calls.cpp, the Win32 window sizing only without SDL). `src/common/linux/msvcNames.h`, included
  ahead of every source, maps `_stricmp` to `strcasecmp`.
- `HostWindowSdl.cpp` makes the window with SDL_WINDOW_VULKAN off Windows (dxvk presents through it).
- The backend's pretransformed draws (the immediate path - fonts, the HUD's text - and the quads after it) give the
  texture coordinate's q as 1 in the vertex (D3DFVF_TEXCOORDSIZE4): the translated pixel shaders divide by it, and
  dxvk's fixed function does not fill it with 1 as Windows' D3D9 does, so the text was missing.

The MSVC build (driving_standalone, driving_standalone_test, drivinginject, driving, action, actioninject) and the
mingw cross build (nf-cross, `linux` preset, everything) still build.

### Running it headless

Without a display, SDL's offscreen video driver and dummy audio driver, and Mesa's lavapipe (llvmpipe Vulkan on the
CPU) through `VK_EXT_headless_surface`:

```
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy DXVK_WSI_DRIVER=SDL3 \
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json LP_NUM_THREADS=3 \
NIGHTFIRE_LOCKSTEP=1 NIGHTFIRE_SNDLOCKSTEP=1 NIGHTFIRE_SNDTRACE=1 NIGHTFIRE_DUMP_TICKS=500,...,9000 \
./driving_standalone_test -mission 1                      # NIGHTFIRE_HOLD=accelerate for the accelerating runs
```

Frames are dumped through the backend's dump path as on Windows. A lockstep run to tick 9000 takes 4-7 minutes at
about 24 frames a second on lavapipe (about 12% of this PC's 16 threads). The disc read through WSL's /mnt/q is slow
(a mission load of minutes); a copy on the Linux file system loads in seconds.

### Tests (15 October 2026, WSL2 Ubuntu 24.04 on this PC, Ryzen 9800X3D)

`driving_standalone_test`, lockstep with sound in lockstep to tick 9000 (the round's settings and dump ticks, 640x480),
sound call traces compared with compare.py's trace comparison against `lloc_*`, this PC's Windows baselines (MSVC
build):

| Run | Against lloc |
|---|---|
| m1 idle, m1 accelerate, m2 idle, m3 idle, m4 idle, m5 accelerate, m6 idle, m7 accelerate, m8 idle | identical: every line to tick 9000 (83,197 to 1,010,286 lines a run) |
| m3 accelerate | the same calls, in the same order, to tick 9000 (300,846 lines); 378 differ in an argument by a step or a few (SNDpitchmult 201, SND3dpos 146, SNDvol 31), from tick 185 to the end, on 30 voices (the player's engine pair v2 and v3 most) - the same on two Linux runs |

Against `lbase` (the runner's Intel CPU) all but m2 idle differ, as on Windows.

### Step 4 follow-ups (15 October 2026)

**m3 accelerate was the compiler, not Linux.** An x87 function declared `float` (or `double`) leaves its result in
ST0; for FSIN, FCOS, FPTAN, FPATAN, FYL2X and the C runtime's acos and pow that is a 64-bit result, not rounded to
the declared type. What the caller then does is the compiler's choice, made per call site, and MSVC and clang
choose differently: MSVC rounds it where the code stores a float (directly, or through a double), and otherwise
multiplies, adds or subtracts on the x87 before rounding (one rounding of the exact result) or stores a double
first; clang always takes the value as it is into its SSE code (`fstpl`, a rounding to double), and keeps a
`float` result unrounded (`fstpt`) when it goes on in double. The m3 accelerate difference was two such sites:
AVehicle::CalculateRpm, whose FYL2X logarithm MSVC multiplies by 1000 on the x87, and
WCollisionMgr::SurfaceBumpHeight, which adds the cosine on the x87 to a sine MSVC rounds to a float. The mingw
build showed the same on the runner against lbase (first difference at tick 205; after the fix identical, frames
too), so it was clang's, not Linux's.

Every call to an x87 function that answers in ST0 was listed in both builds' disassembly (the MSVC driving_standalone
and the clang Linux one) with what follows the call, and the sites where the two differ now spell MSVC's choice
out: the operation on the x87 in a naked helper (`SineTurnsTimes`, `CosineTurnsTimes`, `MinusSineTurns` in
RealMath; `LogTimes` in VehicleSound.cpp, `CosTurnsPlus` in CollisionQueries.cpp, `CrtAcosMinus` in EventsA.cpp,
`CrtPowTimes` in VehicleRender.cpp), and the rounding through an argument (`AsFloat`, `AsDouble`: a value passed as
a float or double argument is stored as one by both compilers). Each helper does what MSVC's code did at that site,
so the Windows build's results are unchanged (the runner rounds below). The sites: CalculateRpm, SurfaceBumpHeight,
PBondCar's boat and submarine bobbing, ECollision::ProcessSideA, BuildRotate, VU0_EulerToQuat, QuatFromTo,
ProcAnimSway, RSky, the reflection map's vertices, the HUD's stylin wobble, the decal cylinder, REmp::Init and the
muzzle flash size. A new call site of these functions needs the same check (the disassembly of both builds).

**The sync tasks followed the wall clock.** SYNCTASK_run ran its tasks by LibTicks, the real 50 Hz timer's count,
and IOModule::Update (the pads into the action queues) is one of them, every second count. On lavapipe (13 to 24
frames a second) that was every tick, and Linux m8 accelerate never saw the held trigger (its trace was m8 idle's).
dev's fix (729ebd3, docs/test-runner.md) counts simulation ticks under `NIGHTFIRE_SNDLOCKSTEP` with every task due
at the first tick, and is exact against lbase without new baselines. It also closed the unpaced leak seen here
(m3 accelerate parting from lloc at tick 25 or 26 with `NIGHTFIRE_PRESENT_EVERY=50`), so unpaced is the default for
Linux runs as for Windows ones.

**The exit crash** is gone with dev's StandaloneMain, which ends with HostExitProcess once the window closes;
HostPosix's is `fflush(NULL)` then `_exit`, so no static destructor runs beside the game's thread (SIGINT, which
SDL turns into the window closing, exits with status 0).

After merging dev (portable maths, the sync-task fix), X87 on, 16 runs to tick 9000 each way:

| Linux (WSL2, `RUNNER_HOST=wsl`, 3 slots) | Against lloc |
|---|---|
| unpaced (default `NIGHTFIRE_PRESENT_EVERY=50`) | all 16 identical: every line (62,158 to 1,345,342 lines a run), m8 accelerate included |
| paced (`NIGHTFIRE_PRESENT_EVERY=0`) | all 16 identical |

On the runner: the injected build's full round against lbase (16 lockstep runs and the shadows) and
`EXE='Release\driving_standalone_test.exe'`'s 16 lockstep runs are exact, the shadows clean; with
`-DNIGHTFIRE_X87=OFF` the standalone's 16 lockstep runs are exact against lbase_c.

The lockstep harness's package-0 tools need the Windows build: CodeRetarget moves references in the PE image and
X87Count reads MSVC's link map, so on Linux devtools/RetargetPosix.cpp stands in for both (`NIGHTFIRE_HEAP_PAD` and
`NIGHTFIRE_X87COUNT` say they are unavailable and change nothing); `NIGHTFIRE_PARTICLE_ORDER` works.

Frames are not comparable with the Windows baselines (another renderer, and 640x480 here), but match them by eye:
the level, the cars, the HUD and its text, the Mission Failed screen.

**Left**:
- A run with a window (WSLg, or a Linux desktop with a GPU) - not tried: game windows on this PC need the user.
- A Linux runner (step 5), with its own baselines.
- The Linux build with `-DNIGHTFIRE_X87=OFF`: done 15 October 2026, with baselines of its own for this PC's WSL2,
  `lwsl_c_*` (16 runs, lavapipe, 640x480), against which the x86-64 Linux build is compared (64-bit.md 7.11).

## The x87 option (15 October 2026)

`-DNIGHTFIRE_X87=OFF` builds the driving targets with no x87 transcendentals: the 43 functions of
[64-bit.md](64-bit.md) 4.1 (FSIN, FCOS, FPTAN, FPATAN, FYL2X and the runtime's `_CIacos`/`_CIasin`/`_CIpow`) each get
a C form in double from `<math.h>`. It is ON by default, which leaves every target exactly as before: the injected
build's shadows compare our code with the original's, and they need the original instructions. The 64-bit build
will use OFF; the Linux build is tested with ON so far (its traces against lloc). The C forms sit next to the assembly in the same files, under `#if NIGHTFIRE_X87` (`X87.h` refuses to
compile without the definition). The injection tags stay on the assembly. A caller that keeps an unrounded result
calls `SineTurns`, `CosineTurns`, `TangentTurns` or `AtanTurns` (RealMath.h), which return double in both builds.
The helpers that spell out MSVC's handling of such a result (step 4's follow-ups: `SineTurnsTimes`, `CosineTurnsTimes`,
`MinusSineTurns`, `LogTimes`, `CosTurnsPlus`, `CrtAcosMinus`, `CrtPowTimes`) do the same operation in double with OFF,
and `AsFloat` and `AsDouble` are plain conversions there.
With OFF the particle systems' map also stops using emitter addresses as keys (original-quirks.md 1.3), so the frames
no longer depend on the heap.

OFF is a different game run, so it has its own runner baselines, `lbase_c_*` (build/baselines). Against `lbase` the
simulation does not change: all 16 sound traces are identical to tick 9000. Frames differ in particles on missions
1, 2, 5, 6 and 7, and by 43 pixels on mission 8 (64-bit.md 4.4). Compilers still emit some x87 code of their own: MSVC returns floats in ST0 and
sometimes multiplies there, under Windows' 53-bit precision control. A Linux `-m32` build should therefore use
`-msse2 -mfpmath=sse`, or set the precision control on each game thread (64-bit.md 4.2).
