# Where the core departs from the original

The core is meant to be the original game, verified against default.xbe and Driving.xbe, with only what other
platforms need: our graphics backends, SDL windows and controllers, host files, 64-bit pointers. Fixes to the
original's bugs, other control schemes, other menus and other ways to play belong to a mod layer that wraps the
core. That layer does not exist yet. Until it does, a few such features live in the core. Each is listed here, so
that each can be moved out or switched off when the mod layer arrives.

Rules:
- A port that differs from the original by mistake is a bug to fix, not an entry here.
- A deliberate departure needs an entry here and a comment at the code that says what the original does.
- Where it can be done, the original's behaviour is the default, or a build switch away.

## Gameplay: to move to the mod layer

| What | Where | The original | The switch |
|---|---|---|---|
| Fixes for the original's bugs (the driving engine's slow-motion rate, the action engine's keycode digits) | docs/original-bug-fixes.md, one entry each | the bug | CMake `NIGHTFIRE_FIX_ORIGINAL_GAME_BUGS` (on by default) |
| Mouse look: the mouse turns and aims the player | engine/mouseLook.cpp/.h, read each frame by `Input_UpdateWithMouse` (input.cpp, around the injected `Input_Update`) and applied in `Player_ViewClamping` (game/obj/Player.cpp) | pads only | settings.ini `MouseLook` |
| Mouse steering of remote vehicles (guided missile, RC car and helicopter, gun emplacements, Ronin): the mouse becomes an elastic stick | engine/mouseSteer.cpp/.h | pads only | settings.ini `MouseLook` |
| Field of view from settings.ini; a real scope's zoom makes up the difference so it shows what it always did | engine/viewFov.cpp/.h, Camera.cpp, Player.cpp, car.cpp, GunImp.cpp | 60 degrees, fixed | settings.ini `FOV` (60 by default, the original's) |

Taken out of the core, for the mod layer to bring back:
- Walking forwards and backwards with the scope up (the original allows only sideways movement while scoped,
  `Player_Move`). Removed 17 October 2026.

## Platform: allowed in the core

- The graphics backends (Direct3D 9, SDL GPU) in place of the Xbox's Direct3D, anti-aliasing included
  (`NF_ANTIALIASING`); XAudio2 in place of DirectSound, with the reverb send switchable (settings.ini `Reverb`);
  host files in place of the Xbox's drives (settings.ini `DiscPath`, `XboxFile.cpp`); keyboard and host controllers
  through psiInput.
- A drone's breath keeps its own phase of the two-second cycle from a hash of its state machine's id (its number in
  the level), not from the low 12 bits of its object's address as the original does (`Drone_Update`, `BreathPhase`
  in game/drone/Drone.cpp). The address is wherever the host's heap put the object, so the same game breathed at
  other moments on 32-bit, 64-bit and other hosts, and a breath makes smoke that draws from the game's random numbers.
  Decided 23 October 2026, after speedrunners confirmed no run relies on random-number manipulation. It replaced a
  shadow of the Xbox heap that gave every host the Xbox's addresses. Recordings in which drones breathe (the
  CastleExterior ones) diverge from those made before it.
- A directory search (the saves the game lists, `HostFindFirstFile`) gives its names sorted as NTFS sorts them, on
  every host (src/driving/platform/Host.h). The Xbox's FATX listed them as they lay, and so do FAT
  and POSIX directories, while the menus keep the first save found (`Menu_EnumSaves`). Added 24 October 2026, so
  that every host lists them alike.
- The C forms of the x87 code (64-bit and non-x86 hosts) take their transcendentals from DetMath, the same bits on
  every host but not the x87's (docs/deterministic-maths.md).
- Uninitialised stack the original leaves in a record is written as zero (docs/driving/original-quirks.md,
  docs/nightfire-port-lifts.md "Departures from the original").

## Devtools: off unless asked for

None of these changes the game when it is not enabled: time demos (settings.ini `DemoRecord`, `NIGHTFIRE_DEMO_*`,
devtools/Demo.cpp), `-level` and `-difficulty` (engine/LaunchOptions.cpp), the F7-F9 teleport and dump keys
(devtools/Teleport.cpp, off in time demos), file dumps (settings.ini `DumpFiles`), `NIGHTFIRE_ORIGINAL`, and the
shadow tests (MenuShadowTests).
