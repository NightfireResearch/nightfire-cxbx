# Nightfire-cxbx

An ongoing decompilation of *007: Nightfire* (Xbox), done by running the original game and replacing it a
function at a time.

**Both engines now run with no emulator in the process.** The game is two programs: the action engine
(`default.xbe`, on foot) and the driving engine (`Driving.xbe`, EA's EAGL engine, for the car missions).
`action.exe` and `driving.exe` map their XBE themselves, provide the Xbox kernel calls it makes, and run it.
Graphics go through Direct3D 9, audio through XAudio2, input through XInput, and files through a plain Win32
layer. The two hand over to each other as the console did, by relaunching.

Together, these two executables allow you to play through the entire game, both single and multiplayer.

## How it works

The original Xbox game is x86 code, which is executable on a modern PC, except for its interactions with
the operating system / kernel. The Xbox provides one set of APIs; modern PCs have similar but not identical ones.

Roughly 10% of its functions have been reimplemented in C++ in this repository, and at startup those are 
patched over the originals - `tools/preprocess.py` turns `AUTOINJECT`and `FUNC_AT(<address>)` comment tags 
into a table of jumps. Everything else still runs as shipped. The proportion goes up over time; nothing has
to be finished before the game runs.

Replacing a whole library rather than one function is done with a "seam". In the action engine every entry
point goes through a dispatcher that can send the call either to the original or to a native backend, chosen
by a setting - which is how Direct3D 8 became Direct3D 9 and DirectSound became XAudio2, and why both hosts
still work: the CXBX path is the reference to bisect a regression against. The driving engine's seams sit at
the library boundary instead, every D3D8 and DirectSound entry point patched at its own address, because its
engine above them is too large and too unnamed to reimplement first. Both engines share the Direct3D 9
backend, which translates the NV2A's vertex programs and register combiners to HLSL at run time.

The pieces:

| | |
|---|---|
| `action.exe` | The standalone loader (`src/loader/`), for the action engine. Maps the XBE at its link address, resolves its kernel imports, creates the window, runs the game. |
| `driving.exe` | The same loader, built for the driving engine. |
| `actioninject.dll`, `drivinginject.dll` | The reimplemented functions and the seams, one per engine. Loaded by either host, unchanged. |
| `action_cxbx.exe`, `driving_cxbx.exe` | The old launchers: run the game inside cxbx-reloaded and inject the same DLLs. |

## Getting started

You need a dump of your own Xbox disc. The game data is not in this repository and will not be.

* Extract the disc to a folder, so that you have `default.xbe`, `Driving.xbe` and the data directories.
* Put it where the loader will find it: by default `../disc` relative to the executable, or set `DiscPath`
  in `settings.ini`.
* Build (below), or download a release.
* Run `action.exe` to start the game from the beginning, or `driving.exe -mission N` to go straight to a
  driving mission (below). Both print what they are doing, and say plainly what they could not do.
* For a higher resolution, set `RenderWidth` and `RenderHeight` in `settings.ini` (with `Widescreen=1` for a
  16:9 size). Both engines render their 640x480 into a backbuffer of that size; 0 turns it off.

To compare against the emulator you also need cxbx-reloaded extracted somewhere, and `action_cxbx.exe` /
`driving_cxbx.exe` will ask for its location the first time. `GraphicsBackend` and `AudioBackend` in
`settings.ini` choose between CXBX's emulation and the native backends there; the standalone loaders always
use the native ones.

Switching between the two engines, and between the parts of a driving mission, is a process relaunch: the
game writes its launch data to `psiLaunch.bin`, the loader starts `action.exe` or `driving.exe` from beside
itself with the same log, and exits. Under the CXBX launchers it still stops and you start the other one.

### Starting a driving mission directly

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

### Settings for development

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

CMake, and a 32-bit MSVC toolchain. The game is a 32-bit x86 binary and the loader has to share its address
space, so this cannot be built for x64.

```
cmake -B . -G "Visual Studio 17 2022" -A Win32
msbuild nightfiRE.sln /p:Configuration=Release /p:Platform=Win32
```

The targets are `action` and `actioninject`, `driving` and `drivinginject`, and the two `_cxbx` launchers. A
DLL is compiled with the shared backends in it, so a change under `src/common/` wants both injects rebuilt.
Everything lands in `Release/`. `tools/preprocess.py` runs as a pre-build step and needs a function name in
`tools/functions_action.json` or `tools/functions_driving.json` for every `AUTOINJECT` tag, or an address via
`FUNC_AT`.

## Tools

| | |
|---|---|
| `tools/drive_game.ps1` | Launches either engine and presses keys at it, to reproduce something several menus in without a person at the keyboard. Also holds keys, teleports the car, dumps frames, and passes `-GameArgs`. |
| `tools/symbolise.py` | Names the XBE functions in a loader crash report (`Release/crash.log`). |
| `tools/bmp2png.py` | Converts the backend's frame and texture dumps to PNG. |
| `tools/nv2a_psh_dump.py` | Decodes the NV2A register-combiner programs the driving engine creates (`--summary` tabulates what they use). |
| `tools/vsh_dump.py`, `tools/vsh_translate_test.ps1` | Dumps the action engine's vertex shaders, and compile-checks the translator over all of them. |
| `tools/xbe_entry_points.py` | Regenerates the driving engine's D3D8 and DirectSound entry-point tables, with the stack bytes each pops, out of the binary. |
| `tools/survey_xbe.py` | Prints an XBE's base, size, kernel imports and FS-segment usage - how much of the startup incompatibility applies to it. |
| `tools/kernel_imports.py` | Which kernel imports live game code actually reaches. |
| `tools/gen_kernel_ordinals.py` | Regenerates the loader's kernel ordinal-to-name table from Cxbx-Reloaded's thunk table. |
| `tools/dsp_image_dump.py` | Extracts and identifies the audio DSP program the action engine downloads. |
| `tools/xadpcm_test.ps1` | Checks the Xbox ADPCM decoder offline. |
| `tools/check_loader_image.ps1` | Verifies `action.exe` was linked at the XBE's base, big enough, with ASLR off. Run by CI, because losing any of those link options fails at runtime rather than at build time. |
| `tools/preprocess.py` | Generates the injection table from the source tags. |
| `ghidra/NightfireSync.py` | Exports names and signatures from Ghidra into `tools/functions_*.json`. |

## Ghidra

Various bits of information are automatically generated from the Ghidra project. To create or update these:

* Install the latest release of Ghidra
* Launch the PyGhidra variant (eg, MacOS: `/opt/homebrew/Caskroom/ghidra/11.3-20250205/ghidra_11.3_PUBLIC/support/pyghidraRun`)
* Add the `ghidra` subfolder as a Script Directory using the Script Manager within the Code Browser tool
* Run the `NightfireSync` script

Conventions: rename in Ghidra, re-sync the JSON, cite addresses in comments so a claim can be re-checked.

## Documentation

`docs/cxbx-removal-plan.md` is the action engine's: what has been done to get it off cxbx-reloaded, what is
left, and the method. `docs/driving-engine-plan.md` is the same for the driving engine, which is where the
work is now; its section 0.1 is the current state and the list of what is left. `docs/audio-inventory.md`
records what the action engine actually asks of DirectSound. `docs/macOS-D3D9-setup.md` is how to run this on
Apple Silicon at full speed, which needs DXVK rather than Wine's own Direct3D - and which currently pins you
to a Wine version that is no longer published. `CHEATSHEET.md` is the helper macros for reaching the game's
memory from injected code.

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

## Special Thanks

Based on [Reburn3](https://github.com/reburndev/reburn3) - a proof of concept by MattKC targeting Burnout 3: Takedown.
