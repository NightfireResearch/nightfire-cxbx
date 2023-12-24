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


## Special Thanks

Based on [Reburn3](https://github.com/reburndev/reburn3) - a proof of concept by MattKC targeting Burnout 3: Takedown.