# Nightfire-cxbx

A port of Nightfire to PC using code injection.

## How it works

This project consists of:
* A launcher, which wraps around cxbx-reloaded, and injects a DLL into the resulting Xbox process
* The injector, which patches the existing game code, allowing us to change, add, or replace functionality
* The injected game code, currently consisting of a few modified functions related to the filesystem, but eventually we could inject essentially the entire game engine.

## Getting Started

* Download and extract cxbx-reloaded
* Configure cxbx-reloaded, paying special attention to EEPROM settings (with EU disc, must ensure PAL 50 Hz)
* Build or download a release of this project
* Dump your Xbox disc of Nightfire to a folder, and extract its contents (you should have `default.xbe` and some data directories)
* (Optional, for developers) download Cmake and ensure you have a C/C++ compiler installed
* Download the latest release (or for developers, download and build from this repo)
* Run the executable
* First time setup - select the locations of `cxbx-reloaded` and `default.xbe`

## Ghidra Script

Various bits of information are automatically generated from the Ghidra project. To create or update these:
* Install the latest release of Ghidra (11.3)
* Launch the PyGhidra variant (eg, MacOS: `/opt/homebrew/Caskroom/ghidra/11.3-20250205/ghidra_11.3_PUBLIC/support/pyghidraRun`)
* Add the `ghidra` subfolder as a Script Directory using the Script Manager within the Code Browser tool
* Run the `NightfireSync` script

## Project goals

### Action engine

- [x] Allow extraction of game assets
- [x] Allow patching and hot reloading of game assets
- [ ] Fix crash when control has been passed from the Driving engine
- [ ] Implement high-resolution, widescreen fixes etc
- [ ] Swap out various blocks of logic to fully understand how the engine works/data structures
- [ ] Re-implement Xbox-specific code to run independently of cxbx-reloaded
- [ ] Re-implement the remaining code, resulting in a full source decompilation

### Driving engine

- [ ] Understand / fix the performance issues
- [ ] Understand / fix the crashes
- [ ] Allow extraction of game assets
- [ ] Allow patching and hot reloading of game assets
- [ ] Swap out various blocks of logic to fully understand how the engine works/data structures
- [ ] Re-implement Xbox-specific code to run independently of cxbx-reloaded
- [ ] Re-implement the remaining code, resulting in a full source decompilation

### Misc

- [ ] Understand what messages are passed between the two engines and how this works


### C++

The Driving engine makes extensive use of C++. Lower-level functions like file operations, sound drivers etc are pure C, wrapped in C++ abstractions.

It has exception / frame handlers. This is a bunch of functions in the approximate range 001503e0 to 00156bf0, which are set up in potentially exception-generating functions like initialisation of objects. These reference tables approximately from 001a87c8 to 001b3d7c which supply rollback steps to undo partial initialisation (stopped by an exception).

These functions do NOT need to be reimplemented, however they do reveal some details about the composition of classes.

The PS2 code doesn't really seem to do this - exceptions just assert / crash?

## Special Thanks

Based on [Reburn3](https://github.com/reburndev/reburn3) - a proof of concept by MattKC targeting Burnout 3: Takedown.