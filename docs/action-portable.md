# The action engine on Linux

Status (23 October 2026): done. The 13 recordings match the C baseline on Linux x86-64, every frame (Results,
below), and the x87 recordings on macOS arm64 (below). Follows the standalone executable (docs/action-standalone.md) and the 64-bit build (docs/action-64bit.md),
which it assumes: our code alone, no XBE, 13 of 13 recordings matching the C baseline on Windows x64.

The target is **Linux x86-64**, the driving engine's `linux-x64` preset (docs/driving/64-bit.md 7.11): clang, one ABI,
no x87, SDL3 for the window, input and sound, D3D9 on dxvk-native, FFmpeg's shared libraries. The action engine has
no 32-bit Linux build: its 32-bit x87 recordings are AMD-only and its 64-bit build already runs on the C baseline.

## What stands in the way

The game's code has no Windows calls; they are in about twenty files of platform code (an inventory of 23 October
2026):

| Area | Files | Becomes |
|---|---|---|
| Threads, TLS, heaps, locks, time | engine/XboxStartup.cpp, engine/CRuntime.cpp, game.cpp | the driving engine's host layer (src/driving/platform/Host.h), as KernelImports.cpp and XboxStartup.cpp there use it |
| Pages | engine/XboxSystem.cpp (contiguous memory), XboxHeap.cpp | `HostAllocatePages` |
| Files | engine/XboxFile.cpp and .h, FS.cpp (an `OVERLAPPED` read), psiSave.cpp, psiFile.cpp | Host files; the reads completed synchronously; `fsync`, `ftruncate`, `stat` and `statvfs` added to the host |
| Command line, errors, console | LaunchOptions.cpp, XboxError.cpp, common/console.h | `HostCommandLine`, `HostExitProcess`, nothing |
| Window and crash report | standalone/StandaloneMain.cpp | `HostWindow_*` (HostWindowSdl.cpp) and the driving engine's CrashReport |
| Pads and keys | engine/psiInput.cpp, input.cpp | `HostInput_ReadPad`, `HostInput_SetRumble`, `HostInput_KeyHeld` (more keys) |
| The mouse | engine/mouseLook.cpp (raw input, cursor clipping) | new: a mouse in HostInput (relative motion, buttons, wheel, capture) |
| Sound output | sound/xaudio2Backend.cpp (XAudio2, X3DAudio, reverb) | new: an SDL3 backend behind the same `XA2_*` seam, with the 3D panning, doppler and reverb send the driving engine's mixer has no need of |
| Movies' sound | common/fmv/FmvAudio.cpp | FmvAudioSdl.cpp (and its virtual clock, which the demos use) |
| Graphics | d3d9Backend.cpp | already portable over dxvk-native; the action needs `GfxHost_Window`, `GfxHost_SizeWindow` and `GfxHost_SdlWindow` |
| Demo harness | devtools/Demo.cpp | the host's files and time; the hardware watchpoints stay Windows-only |

LP64, where `long` is 8 bytes (none of these show on Windows x64):

- `ulong` (helpers.h) is `unsigned long`: `menu_bonus` (4 players' unlock bits, lo and hi) doubles, and
  ui_secrets.cpp reads it as 32-bit pairs.
- `ULONG` (actionhelpers.h) for the heap and critical-section calls; the XInput state's `dwPacketNumber`;
  `FileOperationXbox.fileSize`; the mouse's accumulators and `Demo_Mouse`'s arguments.
- `malloc`ed data the game keeps addresses of (psiFile.cpp's patch files) must come from the game window, as the
  driving engine's `operator new` does (GameWindow.cpp).

## Packages

1. **The host layer on Windows**: threads, TLS, heaps, locks, time, pages, files, command line, through Host.h, with
   HostWin32.cpp linked into the action targets. Behaviour-neutral: 13 of 13 recordings on 32-bit and 64-bit, the
   suites clean.
2. **LP64**: the `long`s above as fixed-width types.
3. **SDL3 on Windows** behind `NIGHTFIRE_SDL`: the window and crash report, pads and keys, the mouse, the movies'
   sound, and the sound backend. The recordings do not depend on output devices, so they must still match.
4. **The Linux build**: the action targets out of the `if(WIN32)` block for SDL builds, the `linux-x64` preset
   building them, the demo harness on the host. Then the 13 recordings on Linux x86-64 (WSL2, headless) against the C
   baseline.

## As done (23 October 2026)

**1. The host layer.** The action engine's platform code calls src/driving/platform/Host.h, whose Win32 body
(HostWin32.cpp) and, on 64-bit, the game window's heaps and `operator new` (GameWindow.cpp) are in every action
target. New host calls: `HostFlushFile`, `HostSetEndOfFile`, `HostGetFileAttributes`, `HostCreateDirectory`,
`HostRemoveDirectory`, `HostSetCurrentDirectory`, `HostGetFullPathName`, `HostGetDiskFreeSpace(Ex)`; Win32Types.h
has the file types the game's XAPI speaks. Every overlapped read and write completes when it is made (XboxFile.cpp's
`TransferAt`): a playback always waited for them anyway, and the game's polls see each one finished at once - in
normal play a streaming read now holds up the game's thread for as long as it takes. `Xbox_getptd` fills the C
runtime's per-thread block through `Crt::PerThreadData`: on 64-bit it had written 8-byte words past the block's end,
which did no harm until the C runtime's heap moved into the game window and the XboxHeap's map lay beside it.

**2. LP64.** `ulong` is gone (glibc's <sys/types.h> has its own, `unsigned long`): its nine uses are `uint32_t`.
`ULONG` is `uint32_t` on LP64, as Win32Types.h's; `FileOperationXbox.fileSize` and `Demo_Mouse`'s arguments are
`int32_t`. psiFile.cpp's patch files in multi-file mode come from `new`, which a 64-bit build serves from the window.
msvcNames.h has `_strupr`.

**3. SDL3** (`NIGHTFIRE_SDL`, the standalone targets only; actioninject stays Win32's):

- The window and crash report: StandaloneMain.cpp opens the host's window (HostWindowWin32.cpp, or HostWindowSdl.cpp)
  and installs the driving engine's crash reporter (src/driving/standalone/CrashReport.h); `NIGHTFIRE_REPORT_AFTER`
  is its thread report. `Mem_TakeHeap` takes the heap's 40 MB before the window opens: SDL's and the system's
  libraries otherwise leave no 40 MB together below 256 MB, where contiguous memory comes from. The window has no
  icon now (res.rc's was the old window class's).
- Pads and keys: psiInput.cpp and input.cpp read HostInput.h (`HostInput_ReadPad`, `HostInput_SetRumble`,
  `HostInput_KeyHeld`, whose keys now include the action engine's).
- The mouse: HostInput.h's `HostMouse_Read`, `HostMouse_HasWindow`, `HostMouse_InWindow` and `HostMouse_Capture`.
  The Win32 body is mouseLook.cpp's raw input, clipped and hidden pointer and re-centring, moved as it was; the SDL
  body adds up the window's mouse events and captures with relative mouse mode. mouseLook.cpp keeps what counts and
  when the pointer is held.
- Sound: sdlAudioAction.cpp is the `XA2_*` backend on the voice mixer that src/common/sound/voiceMixer.cpp now is
  for both engines (the driving engine's sdlAudioDriving.cpp is the mixbins over it, with the same arithmetic). The
  decoded PCM cache is shared with the XAudio2 backend (sound/pcmCache.cpp). A 3D voice's gains are the game's
  rolloff curve over its distance times a constant-power pan between speakers at 45 degrees either side of the
  listener's front (what X3DAudio does for the XAudio2 backend); there is no reverb, as in the mingw build. The
  seam's buffers are words (`PtrTo32`/`PtrFrom32`), and the time demos' sound answers are the backend's, as before.
- The movies' sound: FmvAudioSdl.cpp, with the virtual clock the demos use.

**4. The Linux build.** The action engine's sources are set outside the Windows block; action_standalone and
action_standalone_test are made for Windows and for 64-bit Linux (not 32-bit Linux, nor macOS yet), with the POSIX
host, the crash reporter's POSIX body, the SDL output, dxvk-native and FFmpeg beside the executable, as the 64-bit
driving_standalone has them. The `linux-x64` preset builds them. Demo.cpp's files and time go through the host; its
hardware watchpoints are Win32's.

```
sh tools/fmv/build_ffmpeg.sh linux-x86_64 ~/ffmpeg-x64           # FFMPEG_SOURCE=<checkout> if the submodule is elsewhere
cmake --preset linux-x64 -DNF_FFMPEG_LIB_DIR=$HOME/ffmpeg-x64/lib  # -DSDL_UNIX_CONSOLE_BUILD=ON on a headless machine
cmake --build --preset linux-x64
```

## Results

| Build | The 13 recordings |
|---|---|
| Windows 32-bit (x87): action_standalone_test, the injected build, and action_standalone_test with `NIGHTFIRE_XBOX_HEAP_CHECK=1` | 13 of 13 each |
| Windows 32-bit (x87) with `NIGHTFIRE_SDL` | 13 of 13 |
| Windows x64 (C), on the runner | 13 of 13 against the C baseline |
| Linux x86-64 (C), this PC's WSL2: headless, SDL's offscreen video and dummy audio, dxvk-native on lavapipe | 13 of 13 against the C baseline |

The 19 shadow suites are clean. Every Windows target, the SDL variant of both engines' standalone executables, the
mingw cross build and the 64-bit Linux driving executables build.

## macOS on Apple Silicon (23 October 2026)

action_standalone and action_standalone_test build with the driving engine's `macos-arm64` preset (docs/driving/macos.md)
and run there: the host layer's Darwin rows, SDL3, and SDL's GPU API on Metal in place of Direct3D 9
(docs/driving/sdl-gpu.md; the action standalone targets now take the backend `NIGHTFIRE_GFX` names, as the driving
engine's do). The game's code compiled for arm64 as it was. What the Mac needed:

- **The game window at 4 GB.** On Windows and Linux the window's base is 0, so a word and an address are the same
  number and code that takes one for the other works by accident; on macOS the base is 0x1'0000'0000. Four places did:
  the D3D seam's vertex and index buffers (their data word used as an address for the alignment shift, the move and
  the slots' pointers), its overlay buffers, the back-buffer capture (a truncated address used as one), and the sound
  banks' psi sample headers (offset plus a NULL buffer: an offset, which only base 0 can store). Each now goes through
  `PtrFrom32`/`PtrTo32`, which gives the same values at base 0.
- **Contiguous memory.** On macOS the arena lies in the image as a zero-fill section, which with the image just above
  4 GB covered nearly all of the window's first 256 MB, where contiguous memory goes (Mem_TakeHeap's 40 MB was
  refused). The section is 256 MB longer; its part below 256 MB of the window serves contiguous memory, a 64 KB
  granule at a time (HostPosix.cpp). Given back to the system, that part took the window's frameworks.
- **More vertex streams**: the SDL GPU backend held 8 streams per draw; the action engine draws with up to 9 (16, as
  the NV2A).
- **The linker**: the game's 1-packed structures put some pointers off 8 bytes, and arm64 macOS's chained fixups
  refuse those in statics (`Font1`, `TextMsgFormats` and about 150 others); the action executables link with the
  older rebasing (`-no_fixup_chains`), which takes them.
- d3dSeam.cpp takes the Win32 types from Win32Types.h off Windows (it had dxvk-native's windows.h).

Results on the M2 Max (macOS 27, Metal, headless: the window hidden, SDL's dummy audio): all 13 recordings in
tests/demos/action, the x87 recordings, matched every frame - about 222,600 frames, EvilBase's 75,377 in under two
minutes - twice. No C baseline was compared: that set (tools/demo/rebase_demos.sh's `tests/demos/action-c`) is not in
the repository. The 64-bit builds of Windows and Linux are unchanged by the window fixes at base 0 (the same values),
but untested here. CI builds both engines' Mac executables (the `macos-arm64` job). Not tried yet: a windowed run
with sound, the mouse and pads by hand.

Not tried: a windowed Linux run with sound (no X11, Wayland or ALSA development packages in this WSL2), and the mouse
and pads on SDL by hand.
