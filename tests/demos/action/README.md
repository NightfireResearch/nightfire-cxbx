# Action engine time demos

Recordings of play, played back as tests: `tools/demo/play_demos.ps1` plays each one and checks the game's state
against the recording every frame (docs/action-timedemo.md). After a change to the action engine, every recording
here should still match. The first frame that differs, and what part of the state differs there, are where to
look.

Each starts with `-level`, and carries its own settings.ini and saves, so it needs nothing else from `Release/`.

## The set

Recorded 16 October 2026 by Charlie on an AMD Ryzen 9800X3D (PAL, 50 Hz, 1920x1080), format 2. They stay in step
only on an AMD processor (the game runs the x87's transcendental instructions, which AMD and Intel round
differently), so this set is not for the Intel runner (vr-desktop).

| File | Level | Frames | Notes |
|---|---|---|---|
| CastleExterior-20261016-031815 | CastleExterior | 5,649 | carries on into CastleCourtyard |
| HendersonB-20261016-032357 | HendersonB | 16,613 | |
| CastleCourtyard-20261016-032931 | CastleCourtyard | 17,816 | guards knocked out by hand |
| CastleIndoors1-20261016-033530 | CastleIndoors1 | 635 | cover blown at once: the mission-fail path |
| TowerA-20261016-033544 | TowerA | 8,935 | |
| PowerStationA1-20261016-033845 | PowerStationA1 | 9,975 | |
| Tower2A-20261016-034206 | Tower2A | 12,674 | |
| EvilBase-20261016-034622 | EvilBase | 12,049 | |
| SpaceStationD-20261016-035024 | SpaceStationD | 13,696 | |

About 98,000 frames (33 minutes at 50 Hz). All matched on the build that recorded them (commit 5fa7d7c).

A recording made before a change that alters the game's behaviour on purpose stops matching from the frame the
change first shows. Record that level again after such a change, and replace the file here.
