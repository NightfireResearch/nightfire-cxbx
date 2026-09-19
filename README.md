# Nightfire-cxbx

An ongoing decompilation of *007: Nightfire* (Xbox), done by running the original game and replacing it a
function at a time.

**The action engine now runs with no emulator in the process.** `action.exe` maps `default.xbe` itself,
provides the Xbox kernel calls the game makes, and runs it. Graphics go through Direct3D 9, audio through
XAudio2, input through XInput, and files through a plain Win32 layer. The driving engine still needs
cxbx-reloaded; see `docs/driving-engine-plan.md`.

The project is named for how it started, which was inside cxbx-reloaded. That is now the fallback rather
than the foundation.

## How it works

The game is the original x86 code. Roughly 10% of its functions have been reimplemented in C++ in this
repository, and at startup those are patched over the originals - `tools/preprocess.py` turns `AUTOINJECT`
and `FUNC_AT(<address>)` comment tags into a table of jumps. Everything else still runs as shipped. The
proportion goes up over time; nothing has to be finished before the game runs.

Replacing a whole library rather than one function is done with a "seam": every entry point of the library
goes through a dispatcher that can send the call either to the original or to a native backend, chosen by a
setting. That is how Direct3D 8 became Direct3D 9 and DirectSound became XAudio2, and it is why both hosts
still work - the CXBX path is the reference to bisect a regression against.

The pieces:

| | |
|---|---|
| `action.exe` | The standalone loader (`src/loader/`). Maps the XBE at its link address, resolves its kernel imports, creates the window, runs the game. |
| `actioninject.dll` | The reimplemented functions and the seams. Loaded by either host, unchanged. |
| `action_cxbx.exe` | The old launcher: runs the game inside cxbx-reloaded and injects the same DLL. |
| `driving_cxbx.exe` | The same, for the driving engine, which has no standalone path yet. |

## Getting started

You need a dump of your own Xbox disc. The game data is not in this repository and will not be.

* Extract the disc to a folder, so that you have `default.xbe`, `Driving.xbe` and the data directories.
* Put it where the loader will find it: by default `../disc` relative to the executable, or set `DiscPath`
  in `settings.ini`.
* Build (below), or download a release.
* Run `action.exe`. It prints what it is doing, and says plainly what it could not do.

For the driving engine, or to compare against the emulator, you also need cxbx-reloaded extracted somewhere,
and `action_cxbx.exe` / `driving_cxbx.exe` will ask for its location the first time.

Switching between the two engines is still a manual restart: the game writes `psiLaunch.bin` and stops, and
you launch the other executable.

## Building

CMake, and a 32-bit MSVC toolchain. The game is a 32-bit x86 binary and the loader has to share its address
space, so this cannot be built for x64.

```
cmake -B . -G "Visual Studio 17 2022" -A Win32
msbuild nightfiRE.sln /p:Configuration=Release /p:Platform=Win32
```

Everything lands in `Release/`. `tools/preprocess.py` runs as a pre-build step and needs a function name in
`tools/functions_action.json` for every `AUTOINJECT` tag, or an address via `FUNC_AT`.

## Tools

| | |
|---|---|
| `tools/drive_game.ps1` | Launches the game and presses keys at it, to reproduce something several menus in without a person at the keyboard. |
| `tools/survey_xbe.py` | Prints an XBE's base, size, kernel imports and FS-segment usage - how much of the startup incompatibility applies to it. |
| `tools/kernel_imports.py` | Which kernel imports live game code actually reaches. |
| `tools/preprocess.py` | Generates the injection table from the source tags. |
| `ghidra/NightfireSync.py` | Exports names and signatures from Ghidra into `tools/functions_*.json`. |

## Ghidra

Various bits of information are automatically generated from the Ghidra project. To create or update these:

* Install the latest release of Ghidra (11.3)
* Launch the PyGhidra variant (eg, MacOS: `/opt/homebrew/Caskroom/ghidra/11.3-20250205/ghidra_11.3_PUBLIC/support/pyghidraRun`)
* Add the `ghidra` subfolder as a Script Directory using the Script Manager within the Code Browser tool
* Run the `NightfireSync` script

Conventions: rename in Ghidra, re-sync the JSON, cite addresses in comments so a claim can be re-checked.

## Documentation

`docs/cxbx-removal-plan.md` is the main one - what has been done to get the action engine off cxbx-reloaded,
what is left, and the method. `docs/driving-engine-plan.md` is the same for the driving engine, which is
where the work goes next. `docs/audio-inventory.md` records what the game actually asks of DirectSound.

They are written to be read by whoever picks the work up, including agents, and they carry the reasoning and
the dead ends rather than only the conclusions.

## Project goals

### Action engine

- [x] Allow extraction of game assets
- [x] Allow patching and hot reloading of game assets
- [x] Re-implement Xbox-specific code to run independently of cxbx-reloaded
- [ ] Play through the whole game standalone, closing the gaps that finds
- [ ] Implement high-resolution, widescreen fixes etc
- [ ] Fix crash when control has been passed from the Driving engine
- [ ] Swap out various blocks of logic to fully understand how the engine works/data structures
- [ ] Re-implement the remaining code, resulting in a full source decompilation

The third box is ticked in the sense that the engine boots, plays video and audio, takes input and loads a
mission with no emulator present. It is not ticked in the sense that everything is implemented: 19 of 96
kernel imports are, and the rest announce themselves by name when something reaches them. See the plan for
the full list of what is known to be missing.

### Driving engine

- [ ] Understand / fix the performance issues
- [ ] Understand / fix the crashes
- [ ] Allow extraction of game assets
- [ ] Allow patching and hot reloading of game assets
- [ ] Swap out various blocks of logic to fully understand how the engine works/data structures
- [ ] Re-implement Xbox-specific code to run independently of cxbx-reloaded
- [ ] Re-implement the remaining code, resulting in a full source decompilation

The plan for these has been rewritten now that the loader exists: the performance and crash work was largely
a set of corrections for cxbx-reloaded's behaviour, and going standalone first removes the need for most of
it. Encouragingly, the startup incompatibility is the same size as the action engine's - 31 sites, not the
thousands a first look suggests.

### Misc

- [ ] Understand what messages are passed between the two engines and how this works
- [ ] Automate the hand-off between engines, which is currently a manual restart

### C++

The Driving engine makes extensive use of C++. Lower-level functions like file operations, sound drivers etc are pure C, wrapped in C++ abstractions.

It has exception / frame handlers. This is a bunch of functions in the approximate range 001503e0 to 00156bf0, which are set up in potentially exception-generating functions like initialisation of objects. These reference tables approximately from 001a87c8 to 001b3d7c which supply rollback steps to undo partial initialisation (stopped by an exception).

These functions do NOT need to be reimplemented, however they do reveal some details about the composition of classes.

The PS2 code doesn't really seem to do this - exceptions just assert / crash?

## Special Thanks

Based on [Reburn3](https://github.com/reburndev/reburn3) - a proof of concept by MattKC targeting Burnout 3: Takedown.
