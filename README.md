# Nightfire-cxbx

*007: Nightfire* (Xbox, 2002), rebuilt as a native PC game from the original's own code.

## What this is

A fan research project that rebuilt the Xbox version of *007: Nightfire* function by function, until none of the
original program is needed any more. What you get is the game as it shipped, running as an ordinary program on a
modern computer: no emulator and no Xbox files beyond the game's data. You supply that data from your own copy of
the disc.

The goal is faithfulness: the same game, behaving exactly as the original did, with the conveniences a PC needs on
top: high resolutions and widescreen, mouse and keyboard as well as controllers, and saves on your own disk.

## Where it is today (October 2026)

- **The whole game plays**, both the on-foot missions and the driving missions. The story moves between them as it
  did on the console. Split-screen multiplayer with bots works too.
- **Windows** is the main platform. Builds for **Linux** and **Apple Silicon Macs** also run the game, but for now
  you have to build them yourself ([DEVELOPERS.md](DEVELOPERS.md#building)).
- **High resolution and widescreen**, mouse look, and Xbox-style controllers.
- **Checked against the original.** Recorded play sessions replay frame for frame, identical to the original game.
  That is how we know a change hasn't altered the game.
- **Rough edges:**
  - There are no ready-made downloads or installer yet. One is planned: `docs/install-and-update.md`.
  - Only the PAL (European) Xbox disc has been tested.
  - Loading the PS2 version's data is an early experiment.

## What you need

- **Your own copy of the game for the original Xbox**, copied to your computer. The game's data is not included
  here and never will be, so please don't ask for it.
- From the disc you need the `eurocom` and `driving` folders, about 3.2 GB in all. Extract the disc image with any
  Xbox disc extraction tool.
- A computer with a graphics card that supports Direct3D 9 (Windows) or Vulkan or Metal (Linux, macOS). Any machine
  from the last decade is plenty.

## Playing

1. Get the program: build it (see [DEVELOPERS.md](DEVELOPERS.md#building)). Ready-made downloads will come with the installer.
2. Put the disc's folders in a folder called `disc` next to the program's folder, or tell the game where they are
   with `DiscPath=` in `settings.ini`. The game writes `settings.ini` the first time it runs.
3. Run `action_standalone` to start the game from the beginning. It starts `driving_standalone` by itself when a
   driving mission comes up.
4. For a higher resolution, set `RenderWidth` and `RenderHeight` in `settings.ini`, with `Widescreen=1` for 16:9.

Your saves and settings stay in the folder you run the game from.

Something wrong? Please open an issue at [github.com/NightfireResearch](https://github.com/NightfireResearch).
Include the end of the game's log, which says plainly what it was doing and what it could not do.

Working on the project? [DEVELOPERS.md](DEVELOPERS.md) covers how it is built, tested and put together.

## Special Thanks

Based on [Reburn3](https://github.com/reburndev/reburn3) - a proof of concept by MattKC targeting Burnout 3: Takedown.
