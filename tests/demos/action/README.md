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

**Rebased 16 October 2026** onto the random helpers made exact to the original (0438ade, merged as f9842b3), with
tools/demo/rebase_demos.ps1: the same input, played into the corrected game. The games part from the old recordings
between frame 26 (CastleCourtyard) and frame 5081 (TowerA); CastleIndoors1 does not change. Every session still
passes through the same levels at the same frames, with the same object counts at each 1,800-frame mark. The
differences are in floating-point detail, and the HendersonB keypads still open. The rebased recordings matched
themselves, every frame, twice.

**Rebased again 16-17 October 2026**, twice: onto the maths made exact to the original (the rotation matrices, the
plane and polygon tests, the ray-triangle test, the view frustum test and the searchlight sweep, with FSIN and FCOS
run as the instructions), then onto `Player_Move`, `ApplyMatrixLV` and `MatrixMultiplyVector` made exact, with the
scope stopping forward and backward movement as the original does. Played with those fourteen functions put back to
the Xbox's own code (`NIGHTFIRE_ORIGINAL`), every recording matches every frame, so the ports are exact on all of
this play. Small differences grow into different play:

| File | Frames | Now |
|---|---|---|
| CastleExterior | 5,649 | as before: on into CastleCourtyard |
| CastleIndoors1 | 635 | as before |
| PowerStationA1 | 9,975 | as before |
| TowerA | 8,935 | as before: on into the next level |
| CastleCourtyard | 17,816 | reaches the second level later, then fails the mission (front end, 0x07000048), where it used to reach the third |
| EvilBase | 12,049 | fails the mission by frame 3,600 |
| HendersonB | 16,613 | fails the mission in the first level; the leftover input takes the menus into CastleExterior |
| SpaceStationD | 13,696 | fails the mission near the end |
| Tower2A | 12,674 | fails the mission in the first level, and the menus take it into CastleExterior; it used to go on twice |

**Rebased a third time 17 October 2026** onto parcel 13: every port that used the host's float maths or trig where the original runs the x87 (View_RotTransMatrix, Vec_Magnitude and its family, the quaternions, Quat_Slerp_Acc, the angle helpers, the drone, player, sensor and HUD trig: 77 functions). Played with those 77 put back to the Xbox's own code, every recording matches every frame (HandleMapSoundAllocation together with Sound_LoadMapSounds, whose map-sound tables are ours). The table above is the state after this rebase.

The last five are still exact tests of what they play, but they no longer reach their later levels. Those five want
recording again.

A recording made before a change that alters the game's behaviour on purpose stops matching from the frame the
change first shows. Rebase the set onto the change with tools/demo/rebase_demos.ps1, check that the sessions still
show what they were for, and replace the files here. Record a level again only when its rebase no longer does.
