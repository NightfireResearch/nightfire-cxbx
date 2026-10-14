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
is emulated; and DXVK on macOS goes through MoltenVK, whose D3D9 coverage is incomplete. Planned after level 1, in
two steps (see the roadmap below).

## Roadmap (the user's order, 14 October 2026)

1. Driving runs 100% standalone on Windows with no dependence on Driving.xbe; then the stack merges back to dev.
2. Platform independence, still 32-bit x86 (level 1 above: Linux with dxvk-native and SDL3).
3. 64-bit: pointer-sized layouts and offset-based loading of the data files.
4. Architecture independence (Apple Silicon and other non-x86 hosts): the x87 code as portable C, and graphics there.

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
- Step 4: a Win32-types header (HANDLE, DWORD, the ERROR_ numbers) so that XAPI and its callers compile without
  windows.h, the src/common pieces above, and the Linux preset.
- The movie stream's clock ignores the device's own buffer (about 10 ms).
- `HostPosix`'s `IsBadReadPtr` makes two system calls per page, and MEM_validate calls it for every free block once
  a loop.

The user's listening check of the SDL build (14 October 2026) passed after one fix: the movies' 5.1 was folded in FFmpeg's channel order rather than EA's, so their dialogue played on the right speaker only - in the XAudio2 build too (74b43b0, docs/driving-fmv.md).
