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

**The missions, recorded 21 October 2026** on dev a155fad1, each from its first level (`-level`, the default
difficulty) to its end or until the game was closed:

| File | Mission | Frames | Levels |
|---|---|---|---|
| CastleExterior-20261021-001543 | The Exchange | 46,681 | CastleExterior, CastleCourtyard, CastleIndoors1, CastleIndoors2; ends where the game ran out of heap (the next frame crashed; docs/action-standalone.md) |
| HendersonA-20261021-003340 | Double Cross | 30,037 | HendersonA, HendersonB, HendersonC, HendersonD |
| TowerA-20261021-004417 | Night Shift | 15,855 | TowerA, TowerB, TowerC |
| PowerStationA1-20261021-005031 | Chain Reaction | 64,861 | PowerStationA1, PowerStationA2, then the front end |
| Tower2A-20261021-011224 | Phoenix Fire | 18,648 | Tower2A, Tower2B, Tower2Elevator, Tower2C |
| EvilBase-20261021-011851 | Countdown | 50,009 | EvilBase, EvilSilo, EvilBaseC, then 0x07000017 |
| SpaceStationD-20261021-013542 | Equinox | 22,727 | SpaceStationD, then the front end |

About 249,000 frames (83 minutes at 50 Hz). All matched, every frame, on the build that recorded them; with
`NIGHTFIRE_MEM_TRACE` the heap peaks at 24-36 MB of 42 and fragments (the largest free block down to 1.5 MB in The
Exchange, 3.6 MB in Chain Reaction).

**Kept from 16 October 2026** (commit 5fa7d7c, rebased since as below), the four that still play what they were
recorded for:

| File | Level | Frames | Notes |
|---|---|---|---|
| CastleExterior-20261016-031815 | CastleExterior | 5,649 | carries on into CastleCourtyard |
| CastleIndoors1-20261016-033530 | CastleIndoors1 | 635 | cover blown at once: the mission-fail path |
| TowerA-20261016-033544 | TowerA | 8,935 | on into the next level |
| PowerStationA1-20261016-033845 | PowerStationA1 | 9,975 | |

They were rebased three times on 16-17 October 2026 (tools/demo/rebase_demos.ps1, the same input played into the
corrected game): onto the random helpers made exact to the original, onto the maths made exact (the rotation
matrices, the plane, polygon and ray-triangle tests, the view frustum test, FSIN and FCOS run as the instructions,
Player_Move, ApplyMatrixLV, MatrixMultiplyVector), and onto every port that used the host's float maths where the
original runs the x87 (77 functions). Played with those functions put back to the Xbox's own code
(`NIGHTFIRE_ORIGINAL`), every one matched every frame. Five others from that set (CastleCourtyard, EvilBase,
HendersonB, SpaceStationD, Tower2A) no longer reached their later levels after the rebases and were replaced by the
mission recordings.

A recording made before a change that alters the game's behaviour on purpose stops matching from the frame the
change first shows. Rebase the set onto the change with tools/demo/rebase_demos.ps1, check that the sessions still
show what they were for, and replace the files here. Record a level again only when its rebase no longer does.
