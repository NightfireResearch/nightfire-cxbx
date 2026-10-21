# Action engine time demos

Recordings of play, played back as tests: `tools/demo/play_demos.ps1` plays each one and checks the game's state
against the recording every frame (docs/action-timedemo.md). After a change to the action engine, every recording
here should still match. The first frame that differs, and what part of the state differs there, are where to
look.

Each starts with `-level`, and carries its own settings.ini and saves, so it needs nothing else from `Release/`.

## The set

Recorded by Charlie on an AMD Ryzen 9800X3D (PAL, 50 Hz, 1920x1080), format 2. They stay in step only on an AMD
processor (the game runs the x87's transcendental instructions, which AMD and Intel round differently), so this set
is not for the Intel runner (vr-desktop).

**The missions, recorded 21 October 2026** on dev da057abe with the x87 and type fixes that made every port exact
(the all-original runs, docs/action-standalone.md), each from its first level (`-level`, the default difficulty) to
its end or until the game was closed. The Exchange three times, one for each way through its first level:

| File | Mission | Frames | Levels |
|---|---|---|---|
| CastleExterior-20261021-190013 | The Exchange | 8,959 | CastleExterior, CastleCourtyard |
| CastleExterior-20261021-190314 | The Exchange | 11,176 | CastleExterior, CastleCourtyard, CastleIndoors1 |
| CastleExterior-20261021-202200 | The Exchange | 26,005 | CastleExterior, CastleCourtyard, CastleIndoors1, CastleIndoors2 |
| HendersonA-20261021-190659 | Double Cross | 24,483 | HendersonA, HendersonB, HendersonC, HendersonD |
| TowerA-20261021-191511 | Night Shift | 18,292 | TowerA, TowerB, TowerC |
| PowerStationA1-20261021-192119 | Chain Reaction | 27,279 | PowerStationA1, PowerStationA2 |
| Tower2A-20261021-193027 | Phoenix Fire | 17,604 | Tower2A, Tower2B, Tower2Elevator, Tower2C |
| EvilBase-20261021-203043 | Countdown | 75,377 | EvilBase, EvilSilo, EvilBaseC |
| SpaceStationD-20261021-195726 | Equinox | 13,395 | SpaceStationD, then the front end |

About 222,600 frames (74 minutes at 50 Hz), all on the pad. Every one matched every frame on the build that
recorded them and with every game function run as the original (`NIGHTFIRE_ORIGINAL=game`). A session that moves
the mouse cannot be checked that way: mouse look is ours, and the original code never reads the mouse, so the
game parts from the recording at the first frame the mouse moves. Two such sessions were recorded again without it.

**Kept from 16 October 2026** (commit 5fa7d7c, rebased since as below), the four that still play what they were
recorded for:

| File | Level | Frames | Notes |
|---|---|---|---|
| CastleExterior-20261016-031815 | CastleExterior | 5,649 | carries on into CastleCourtyard |
| CastleIndoors1-20261016-033530 | CastleIndoors1 | 635 | cover blown at once: the mission-fail path |
| TowerA-20261016-033544 | TowerA | 8,935 | on into the next level |
| PowerStationA1-20261016-033845 | PowerStationA1 | 9,975 | |

They were rebased four times: on 16-17 October 2026 (tools/demo/rebase_demos.ps1, the same input played into the
corrected game): onto the random helpers made exact to the original, onto the maths made exact (the rotation
matrices, the plane, polygon and ray-triangle tests, the view frustum test, FSIN and FCOS run as the instructions,
Player_Move, ApplyMatrixLV, MatrixMultiplyVector), and onto every port that used the host's float maths where the
original runs the x87 (77 functions), and on 20-21 October onto the x87 and type fixes above. Played with those functions put back to the Xbox's own code
(`NIGHTFIRE_ORIGINAL`), every one matched every frame. Five others from that set (CastleCourtyard, EvilBase,
HendersonB, SpaceStationD, Tower2A) no longer reached their later levels after the rebases and were replaced by
mission recordings, which the set above replaced in turn.

A recording made before a change that alters the game's behaviour on purpose stops matching from the frame the
change first shows. Rebase the set onto the change with tools/demo/rebase_demos.ps1, check that the sessions still
show what they were for, and replace the files here. Record a level again only when its rebase no longer does.
