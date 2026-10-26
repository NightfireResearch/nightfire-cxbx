# Developing Nightfire-cxbx

How the project is put together, how to build it, and the tools and switches for working on it. For playing
the game, see [README.md](README.md).

## How it got here

The original game is x86 code, which a PC can run, except where it talks to the Xbox's kernel and hardware. The
project started by running the game inside the cxbx-reloaded emulator and patching our C++ over the original's
functions one at a time. `tools/preprocess.py` turns `AUTOINJECT` and `FUNC_AT(<address>)` tags into a table of
jumps. Later, a loader of our own (`action.exe`, `driving.exe`) mapped the XBE without the emulator, which was
dropped on 3 October 2026 (`docs/cxbx-removal-plan.md`).

Libraries were replaced at "seams":
- **Action engine:** above the library, in Eurocom's wrappers, so Direct3D 8 became Direct3D 9 and DirectSound
  became XAudio2.
- **Driving engine:** at the library boundary, every D3D8 and DirectSound entry point patched at its own address.
- **Both:** one Direct3D 9 backend, which translates the NV2A's shader programs to HLSL at run time. An SDL GPU
  backend (Vulkan, Metal) serves Linux and macOS.

Every function of both engines is now ours, and the game's data is in our source:
- `action_standalone` and `driving_standalone` build from this repository alone and run with no XBE mapped
  (`docs/action-standalone.md`, `docs/driving/standalone.md`).
- The injected builds (`action.exe` with `actioninject.dll`, `driving.exe` with `drivinginject.dll`) are kept as
  test beds. They still need the XBEs, and run our code and the original's side by side to compare them.

The pieces:

| | |
|---|---|
| `action_standalone`, `driving_standalone` | The game, one program per engine, with no original code. The `_test` variants add the time-demo and lockstep harnesses. |
| `action.exe`, `driving.exe` | The loader (`src/loader/`): maps the XBE at its link address, resolves its kernel imports, runs it with the inject DLL. |
| `actioninject.dll`, `drivinginject.dll` | Our functions and the seams, patched over the original's. |

Switching between the two engines, and between the parts of a driving mission, is a process relaunch. The game
writes its launch data to `psiLaunch.bin`, starts the other engine's program from beside itself with the same log,
and exits.

## Starting a driving mission directly

`driving.exe -mission N` starts a driving mission - or a later part of one - without going through the
action engine. The track name works in place of the number (`-mission jungleb_mis13b`).

| `-mission` | Track | Mission |
|---|---|---|
| 1 | `paris_mis01` | Paris Prelude |
| 2 | `uw_mis11` | Deep Descent (underwater) |
| 3 | `junglea_mis13a` | Island Infiltration, part 1 |
| 4 | `jungleb_mis13b` | Island Infiltration, part 2 |
| 5 | `snow1a_mis3` | Alpine Escape |
| 6 | `snow2a_mis4` | Enemies Vanquished |
| 7 | `junglec_mis13c` | Island Infiltration, part 3 |
| 8 | `snow2a_race` | the Alpine race (reached by a cheat in mission 6) |

The numbers are the game's own (`MissionNumToString`, `0x000596a0`), and a mission's parts are just more of
them: finishing part 1 of Island Infiltration relaunches as mission 4. `-difficulty 1` to `4` sets the
difficulty the same way. Any other flag beginning with `-` or `+` is passed to the game's own `main`, which
knows `-ntsc`, `-pal`, `-pal60`, `+streams`/`-streams` and `-T<track>`.

With no options, the mission is whatever `psiLaunch.bin` says, as before. With options and no `psiLaunch.bin`,
a clean hand-over page stands in. `tools/drive_game.ps1 -GameArgs "-mission 4"` passes options through for
unattended runs. Details are in `src/driving/platform/LaunchOptions.cpp`.

## Starting an on-foot level directly

`action.exe -level NAME` boots straight into one level, the part of a mission it belongs to, without the
front end, a codename or a save. The level loads as the game's first, through the same path a mission start
takes, and its mission starts from that part.

- **Names:** `HendersonA`-`D`, `CastleExterior`, `CastleCourtyard`, `CastleIndoors1`, `CastleIndoors2`,
  `TowerA`-`C`, `PowerStationA1`, `PowerStationA2`, `Tower2A`-`C`, `Tower2Elevator`, `EvilBase`, `EvilSilo`,
  `EvilBaseC` and `SpaceStationD`.
- **Hashcodes:** any level's hashcode also works (`-level 0x07000006`). `-level list` prints the names.
- **Difficulty:** `-difficulty 1`, `2` or `3` (Operative, Agent, 00 Agent).
- **Launch data:** with `-level`, `psiLaunch.bin` is ignored, so a run is always a fresh boot.

`tools/drive_game.ps1 -GameArgs "-level CastleCourtyard"` passes the options through. Details are in
`src/action/engine/LaunchOptions.cpp`.

## Time demos of the action engine

Set `DemoRecord=on` in `settings.ini` to record every session to `demos/`, and set `NIGHTFIRE_DEMO_PLAY=<file>`
to play one back. A playback checks the game's state against the recording every frame. A recording keeps its
`-level` and `-difficulty`, so a playback needs no options. See `docs/action-timedemo.md`.

## Settings for development

All under `[Settings]` in `settings.ini`, read at startup:

| | |
|---|---|
| `PerfLog=on` | Frame timing and draw counts every few seconds. The driving engine prints these always, with its seams' reports of entry points reached that have no replacement yet. |
| `Profile=on` | A sampling profiler that can see into the mapped XBE, which no Windows profiler can. |
| `DumpEvery=N` | Every Nth frame to a BMP, with a trace of its draws and its textures decoded beside it. |
| `DumpBurst=N`, `TraceBurstFrames=a,b,c` | Every frame for N frames; then chosen frames traced with an image after each draw. For glitches that last one frame. |
| `CheckVertices=on` | Driving engine: dumps the first frame with an impossible vertex position. |
| `Teleport=x,y,z,dx,dy,dz` | Driving engine: puts the car there once the level is up and dumps the frame. F8 in game records the current place, F9 returns to it. |

## Building

CMake and Python, plus a compiler. The presets below build the 32-bit Windows binaries: the loader, the inject
DLLs and the 32-bit standalone executables, the exact build against the original (x87). The loader shares the
XBE's address space, so it is 32-bit only. The standalone executables also build as native 64-bit programs:
`windows-x64` (docs/action-64bit.md), `linux-x64` and `linux-i386` (docs/action-portable.md,
docs/driving/portable.md), and `macos-arm64` (docs/driving/macos.md).

The movies play through a minimal FFmpeg (`docs/fmv.md`), whose DLLs and headers are committed in
`third_party/ffmpeg-prebuilt` and copied beside the executables by the build. Nothing to do unless you change it:
`tools/fmv/build_ffmpeg.sh` rebuilds it from the `third_party/ffmpeg` submodule.

**On Windows**, with Visual Studio 2022:

```
cmake --preset windows
cmake --build --preset windows
```

**On macOS**, cross-compiling with clang and the mingw-w64 sysroot:

```
brew install mingw-w64 ninja
cmake --preset macos
cmake --build --preset macos
```

**On Linux**, the same, with clang and the distribution's i686 mingw-w64 packages. They have to be mingw-w64
13 or later (Ubuntu 26.04+, Debian testing), because older releases have no `x3daudio.h`:

```
sudo pacman -S clang mingw-w64-gcc ninja                 # Arch
sudo apt install clang g++-mingw-w64-i686 ninja-build    # Debian/Ubuntu
cmake --preset linux
cmake --build --preset linux
```

All three produce the same 32-bit Windows binaries, in `build/windows/Release/`, `build/macos/` and
`build/linux/` respectively, with the FFmpeg DLLs and licence beside them, and all three are built by CI. The cross builds are a build only - see
`docs/macOS-D3D9-setup.md` for running the result on macOS, and `docs/macos-build.md` for the one
functional difference (no XAudio2 reverb) and why clang rather than mingw's GCC.

**Running a build.** The binaries read `settings.ini` from beside themselves (writing a default one the
first time) and look for the disc at `DiscPath`, `../disc` by default. From a preset build that means
`build/windows/disc` and so on, so either set `DiscPath` to wherever the disc is or copy the binaries next to
it. The repository's own scripts and the docs' smoke tests use `Release/` in the repository root, beside
`disc/`; on Windows an in-source configure builds straight there, alongside the preset build:

```
cmake -B . -A Win32
cmake --build . --config Release --target driving drivinginject
```

The targets are `action` and `actioninject`, `driving` and `drivinginject`. A
DLL is compiled with the shared backends in it, so a change under `src/common/` wants both injects rebuilt.
`tools/preprocess.py` runs before the injected DLLs are compiled and needs a function name in
`tools/functions_action.json` or `tools/functions_driving.json` for every `AUTOINJECT` tag, or an address via
`FUNC_AT`.

## Tools

| | |
|---|---|
| `tools/drive_game.ps1` | Launches either engine and presses keys at it, to reproduce something several menus in without a person at the keyboard. Also holds keys, teleports the car, dumps frames, and passes `-GameArgs`. |
| `tools/symbolise.py` | Names the XBE functions in a loader crash report (`Release/crash.log`). |
| `tools/bmp2png.py` | Converts the backend's frame and texture dumps to PNG. |
| `tools/nv2a_psh_dump.py` | Decodes the NV2A register-combiner programs the driving engine creates (`--summary` tabulates what they use). |
| `tools/vsh_dump.py` | Dumps the action engine's vertex shaders. |
| `shadercheck` (CMake target, `src/common/gfx/devtools/shaderCheckMain.cpp`) | Checks the shader translators' bytecode against the HLSL they replaced, over the backend's shader logs and the XBEs' vertex programs. |
| `tools/xbe_entry_points.py` | Regenerates the driving engine's D3D8 and DirectSound entry-point tables, with the stack bytes each pops, out of the binary. |
| `tools/abi_facts.py` | Measures every function's calling convention from the binary - bytes popped, registers read on entry - into `tools/abi_*.json`, which the injection table checks each tagged declaration against at compile time. Re-run after re-syncing from Ghidra. |
| `tools/vtables.py` | Finds the driving engine's vtables and which classes (and derived classes) they belong to, into `tools/vtables_driving.json`, which checks `// VIRTUAL(n)` declarations against every override. Re-run after re-syncing from Ghidra. |
| `tools/alloc_sizes.py` | Finds every allocation in the driving engine with its size, name and constructor, and checks Ghidra's structure sizes against them; `--json` writes `tools/alloc_sizes_driving.json`, which pads overlay classes out to the game's real sizes. |
| `tools/survey_xbe.py` | Prints an XBE's base, size, kernel imports and FS-segment usage - how much of the startup incompatibility applies to it. |
| `tools/kernel_imports.py` | Which kernel imports live game code actually reaches. |
| `tools/gen_kernel_ordinals.py` | Regenerates the loader's kernel ordinal-to-name table from Cxbx-Reloaded's thunk table (a reference checkout of its source, not a dependency). |
| `tools/dsp_image_dump.py` | Extracts and identifies the audio DSP program the action engine downloads. |
| `tools/xadpcm_test.ps1` | Checks the Xbox ADPCM decoder offline. |
| `tools/check_loader_image.py` | Verifies a loader (`action.exe`, `driving.exe`) was linked at the XBE's base, big enough, with ASLR off, and with its own code above the XBE's end. Run by CI on every platform, because losing any of those link options fails at runtime rather than at build time. |
| `tools/preprocess.py` | Generates the injection table from the source tags. |
| `ghidra/NightfireSync.py` | Exports names and signatures from Ghidra into `tools/functions_*.json`, and every structure's layout into `tools/structs_*.json` (which the driving engine's overlay classes take their fields from). |

## Ghidra

Various bits of information are automatically generated from the Ghidra project. To create or update these:

* Install the latest release of Ghidra
* Launch the PyGhidra variant (eg, MacOS: `/opt/homebrew/Caskroom/ghidra/11.3-20250205/ghidra_11.3_PUBLIC/support/pyghidraRun`)
* Add the `ghidra` subfolder as a Script Directory using the Script Manager within the Code Browser tool
* Run the `NightfireSync` script

Conventions: rename in Ghidra, re-sync the JSON, cite addresses in comments so a claim can be re-checked.

## Documentation

`docs/style-guide.md` is how reimplemented code should look - as close as possible to what the game's own
programmers wrote, with tooling and test harnesses kept out of the way. Read it before writing or reviewing a
port.

`docs/cxbx-removal-plan.md` is the history of getting the action engine off cxbx-reloaded, which was finished
and the emulator dropped on 3 October 2026. `docs/driving-engine-plan.md` is the driving engine's plan, which
is where the work is now; its section 0.1 is the current state and the list of what is left.
`docs/driving-injection-framework.md` is the design for tagging C++ replacements in the driving engine as
easily as C ones in the action engine - overloads, `__thiscall`, class layouts - and how far it has got.
`docs/audio-inventory.md`
records what the action engine actually asks of DirectSound. `docs/macOS-D3D9-setup.md` is how to run this on
Apple Silicon at full speed, which needs DXVK rather than Wine's own Direct3D - and which currently pins you
to a Wine version that is no longer published. `docs/macos-build.md` is the other half of working on a Mac:
building the Windows binaries there. `CHEATSHEET.md` is the helper macros for reaching the game's memory from
injected code.

They are written to be read by whoever picks the work up, including agents, and they carry the reasoning and
the dead ends rather than only the conclusions.

## Project goals

### Action engine

- [x] Allow extraction of game assets
- [x] Allow patching and hot reloading of game assets
- [x] Re-implement Xbox-specific code to run independently of cxbx-reloaded
- [x] Play through the whole game standalone
- [x] Implement high-resolution, widescreen fixes etc
- [x] Fix crash when control has been passed from the Driving engine
- [ ] Swap out various blocks of logic to fully understand how the engine works/data structures
- [ ] Re-implement the remaining code, resulting in a full source decompilation

### Driving engine

- [x] Understand / fix the performance issues
- [x] Understand / fix the crashes
- [x] Allow extraction of game assets
- [ ] Allow patching and hot reloading of game assets
- [ ] Swap out various blocks of logic to fully understand how the engine works/data structures
- [x] Re-implement Xbox-specific code to run independently of cxbx-reloaded
- [ ] Play through every mission standalone to confirm no crashes or visual/audible bugs
- [ ] Re-implement the remaining code, resulting in a full source decompilation

### Misc

- [ ] Understand what messages are passed between the two engines and how this works
- [x] Automate the hand-off between engines, which used to be a manual restart

The messages are the launch data page (`psiLaunch.bin`): the action engine's profile, then the mission
hand-over - which mission, the difficulty, and what happened in it. The fields the driving engine reads are
in `src/driving/platform/LaunchOptions.cpp`; the rest of the page is still to be mapped.

### C++

The Driving engine makes extensive use of C++. Lower-level functions like file operations, sound drivers etc are pure C, wrapped in C++ abstractions.

It has exception / frame handlers. This is a bunch of functions in the approximate range 001503e0 to 00156bf0, which are set up in potentially exception-generating functions like initialisation of objects. These reference tables approximately from 001a87c8 to 001b3d7c which supply rollback steps to undo partial initialisation (stopped by an exception).

These functions do NOT need to be reimplemented, however they do reveal some details about the composition of classes.

The PS2 code doesn't really seem to do this - exceptions just assert / crash?
