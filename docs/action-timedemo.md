# Action engine time demos

A time demo is a recording of a play session that can be played back later. During playback, the game is checked
frame by frame against what it did when the session was recorded. It is the test bed for lifting code into the
action engine: record the game once, then play the recordings back on every change. A function that behaves
differently shows up as the first frame whose game state no longer matches.

The code is in `src/action/devtools/Demo.cpp`. It reaches the game through one-line hooks in the input, clock,
sound, movie and file layers.

Version 1 recordings, which made the sound follow the demo clock and made streamed music echo, no longer play.

## Recording

Put `DemoRecord=on` in `Release/settings.ini` under `[Settings]` and play as normal. Every session is then
recorded to `Release/demos/<date>-<time>.nfdemo`. You can also set `NIGHTFIRE_DEMO_RECORD=<file>` to record one
run to a file of your choice.

- **Starting in a level:** add `-level NAME` (and `-difficulty N`) to boot straight into a level without the
  front end or a save (README, "Starting an on-foot level directly"). The recording keeps those options, and
  playback applies them itself.
- **F6** marks a moment, for example where something looks wrong. A playback prints the mark when it reaches it.
- **A recording ends when the game leaves for a driving mission.** The driving engine is not recorded. When the
  game comes back to the action engine, a new recording starts from the results file the driving engine left.
- **Saves are fine.** A recording starts by copying the files the session begins from (`settings.ini`,
  `psiLaunch.bin`, and everything under `saves/`, `tdata/` and `cache/`) into the demo. Each recording therefore
  stands alone, whatever the save folder holds later.
- **Size:** about 25 bytes a frame, so roughly 5 MB per hour.
- **Debug keys:** F8 and F9, the debug teleport, do nothing while recording. F7, the frame dump, still works.

## Playing back

```bash
cd Release && NIGHTFIRE_DEMO_PLAY=demos/<file>.nfdemo ./action.exe
```

Playback works in a sandbox, `demoplay/<name>/` under the working directory. The folder is emptied, filled with
the recording's starting files, and becomes the working directory. Saves made during playback, logs and frame
dumps all land there. The disc is still found through `DiscPath`.

The game runs on the recorded input. At the end of every frame the game's state is compared with the recording.
A line is printed for each of the first 20 frames that differ, naming what differed:

| Check | What it covers |
|---|---|
| `rng` | the random generator's state (0x0018cdf8) |
| `gamestate` | `GameState` (0x001f6580) |
| `channels` | the switch channels and their times |
| `objects` | every live object's type, flags, state, position and rotation, in list order |
| `players` | the players' positions, rotations and states |

One field is let through: `GameState`+0x04 (`collisionStamp`). `Collide_StraddleCels` takes the next number for
every collision query, to mark the portals it has crossed. Some queries come from drawing
(`View_AddForcedObjects`), which the host can make happen a different number of times. So a playback's stamp
can run a constant few behind or ahead of the recording's while the game is otherwise the same. Nothing reads
the value except those comparisons, which only ask whether a portal carries this query's number. A `gamestate`
difference that is only a constant offset there is reported once and not counted (first seen in SpaceStationD,
2 behind at frame 7711).

Every 1800 frames a progress line gives the frame, the level and the object count.

At the end of the recording, the game exits with one of these codes:

| Code | Meaning |
|---|---|
| 0 | every frame matched |
| 2 | some frames differed |
| 3 | the game asked for input in an order the recording does not have, which happens once it has gone a different way |
| 4 | the demo could not be read |

Environment variables for playback:

| Variable | Effect |
|---|---|
| `NIGHTFIRE_DEMO_STOP=1` | stop at the first frame that differs, for bisecting |
| `NIGHTFIRE_DEMO_UNTIL=<frame>` | end the playback at that frame, as if the recording ended there, for bisecting a difference that shows before it |
| `NIGHTFIRE_DEMO_FAST=N` | present every Nth frame and do not pace, for unattended runs (many times real speed; leave it unset to watch and listen) |
| `NIGHTFIRE_DEMO_DUMP=f1,f2,...` | dump those frames, as F7 does |
| `NIGHTFIRE_DEMO_SHOT_EVERY=N` | write every Nth frame to `demo_shots/<frame>.bmp` in the playback's folder, the picture alone, for comparing two builds frame for frame; `NIGHTFIRE_DEMO_SHOT_FROM=<frame>` starts them there (with `NIGHTFIRE_DEMO_UNTIL`, a window) |
| `NIGHTFIRE_DEMO_DUMP_GAMESTATE=f` | print `GameState`'s bytes at that frame (they are also printed at the first frame where `gamestate` differs) |
| `NIGHTFIRE_ORIGINAL=0xADDR,...` | run those functions as the original, ports or not: to compare a parcel of ports with the originals, to bisect which one changed the game, or to measure what the original's code reaches |

Tools that build on these:
- `tools/demo/play_demos.ps1` plays every recording.
- `tools/demo/demo_coverage.py` lists which functions, or with `--blocks` which basic blocks, the recordings reach.
- `tools/demo/bisect_parcel.py` finds which port of a parcel makes a demo differ, by playing it with half the
  suspects restored to the original at a time (each step is `tools/demo/bisect_originals.ps1`).
- `tools/draft_calls.py` lists calls from ported code into drafts: game functions we define but do not patch
  in. Such a call runs the draft where the game ran the original.

## Rebasing a recording onto changed code

A change that is meant to change the game, a port made exact to the original for instance, makes every recording
stop matching. Rebasing makes a new recording from the old one's input, without anyone having to play again:

```bash
cd Release && NIGHTFIRE_DEMO_PLAY=<old>.nfdemo NIGHTFIRE_DEMO_REBASE=<new>.nfdemo ./action.exe
```

- **Input:** the old recording's input goes to the game frame by frame. A game that polls a controller or reads
  the mouse more often in a frame than the recording did gets the last state again; extras are dropped.
- **What is new:** the sound device's answers come live, and the checksums come from the game as it now plays.
- **Same starting point:** the new file keeps the old one's starting files and launch options.
- **Normal speed only:** a rebase runs at the game's own pace, whatever `NIGHTFIRE_DEMO_FAST` says. The sound
  answers come from a device playing in real time, so frames that went by faster would hear every sound last
  longer, and anything waiting on one would wait longer.
- **Where the games part:** it prints the first frame where the new game differs from the old recording, and how
  many frames differ.

Rebasing onto the code that recorded the demo gives back the same recording: TowerA, 0 of 8,935 frames differ.
The input is the same, but the game it meets is not, so the session plays out a little differently: a shot that
hit may miss, a guard who turned may not. Anything that needed exact input can fail, such as typing a keypad code
that the random generator now makes different. Replace a recording with its rebase only after checking that the
session still shows what it was for.

### Checking a rebase against the original code

A rebase made for a port that was made exact can be checked against the Xbox's own code: play it with the changed
functions put back to the original (`NIGHTFIRE_ORIGINAL`). If the port is exact, every frame matches. The math
ports of 16 October 2026 passed this way on all nine sessions.

This works only for functions whose globals are still the game's own. A function that reads or writes a global we
now own (an `XBE_GLOBAL` definition of ours, docs/global-coverage.md) sees the Xbox's stale copy when run as the
original, so it differs for that reason alone. `Player_Start`, `Mission_Init` and `Mission_Status` are such cases.
Restoring every port at once does not work either: the ported main loop carries the demo's own hooks.

## Restarts

`Mem_Init` wipes the heap (fills it with 0x98), and `control_init_object_lists` starts the object list again. A
level load does both in one frame. A restart spans frames between the two, and the object
list and players still point into the wiped heap meanwhile. The game walks neither, and the checksums leave them
out until the list is new (`Demo_HeapReset`, `Demo_ObjectListsReset`).

## What is held still, and why

The game runs at a fixed number of updates per frame (`GameFlow_Main` once per `mainloop`). Input is therefore
the only thing a recording needs per frame, provided nothing else the game can see depends on the host. In demo
mode, both recording and playback do the following.

**Input** is taken at the boundary with the host:
- every poll of every controller port, the keyboard pad included, after `psiInput_PollDevices` has it;
- the mouse after `MouseLook_Update`: whether the pointer is captured, and the counts the game has not yet taken.

The packet number is replaced by a count of state changes, because the keyboard pad's counter moves on every poll.

**The clock** follows the frames. `timestamp()`, the XAPI `QueryPerformanceCounter` pair and `KeQuerySystemTime`
(through its import slot, from 18 November 2002) report the frame count at the frame rate. Each read adds one
microsecond, so a loop that waits on the clock still finishes.

**Disc reads and writes** finish before they return, instead of going pending and taking however many frames the
host took.

**What the sound device says** is treated as input. The game asks each buffer whether it is still playing and where
its play cursor is: `dsndUpdateVoices` frees voice slots by the status, and the streaming code refills music and
speech by the cursor. A recording keeps every answer, and a playback gives the game the recorded answers in place
of the device's. The game therefore plays its real sound while you record. During a fast playback the sound you
hear is not what the game is told.

**The movie clock** follows that clock too (`FmvAudio::SetVirtualClock`): frames fall due by the clock, and the
movie ends once the clock has passed its audio.

**File times** in a directory listing read zero, because a playback's files were made when it started.

The picture is still drawn and paced as usual, so a recording plays like the game.

## Limits

- **The CPU vendor.** The game runs the x87's transcendental instructions itself: `default.xbe` has 69 `FSIN`, 62
  `FCOS`, 25 `FPATAN` and more, most of them in game code (0x18000-0x45000). AMD and Intel processors round some of
  these differently, so a recording stays in step only on a processor of the vendor it was recorded on, and is
  surest on the same machine. The Xbox's CPU was a Pentium III, so Intel's results are likely the closer to it
  (not checked).
- **The recordings hold our build's behaviour, not the original's.** They were made with our ports in place, and
  some of those are known to be inexact against the Xbox. The random helpers were made exact (0438ade) and the set was
  rebased onto them. So were the rotation-matrix functions (`RotMatrix`, `RotMatrixZYX`, `RotTransMatrix`,
  `Vec_Spherical_2_Cartesian`, the searchlight sweep, through X87Ops.h's `Extended` and the FSIN and FCOS
  instructions), `Plane_PlaneEq`, `vecutil_point_on_poly`, `DistancePointToPlane` and `Vec_Dot`'s callers
  (devtools/MathExactShadow.cpp). Parcel 13 made the rest exact: every port that used the host's sin, cos, tan, atan2
  or acos where the original runs FSIN, FCOS, FPTAN, FPATAN or the XBE's `__CIacos` (`View_RotTransMatrix`,
  `Quat_Slerp_Acc`, `vecutil_cartesian_to_spherical_acc`, `Searchlight_Update`, `Camera_CalcViewAngles`, `Sensor`,
  `Autoaim`, the HUD, the error screen and the drones), and the vector helpers whose originals return the x87's
  unrounded sum (`Vec_Magnitude`, `Vec_Dist3D`, `Vec_SqDist3D`, `Vec_NormaliseLen`, `Vec_AngleDifference`,
  `maybeAtan2`, `Vec_ScalarTripleProduct`), with their callers (docs/nightfire-port-lifts.md, parcel 13). Still
  inexact: plain double arithmetic where two NaNs meet (SSE2 keeps the first, the x87 the larger), in the ports
  that do not use `X87NaNs`.

  A port made exact to the original changes the game against the recordings. Rebase them onto it (below).
- **Changes to the sound code.** The sound answers are replayed in the order they were asked. A change that makes
  the game ask the device a different number of times breaks playback (exit code 3), even if the game is otherwise
  unchanged.
- **Settings.** The recording's own `settings.ini` is used for playback: resolution, widescreen, FOV, region,
  language and mouse settings. What is drawn affects the game (objects drawn in the last two ticks are what the AI
  and auto-aim consider), so a playback has to match the recording here.
- **No pinpointing yet.** The checks say which part of the state first differs, not which object or field. To find
  that, stop at the frame (`NIGHTFIRE_DEMO_STOP`) and compare from there.

## The file

`NFDEMO` 0 2, then sections of four-character tag, u32 length and data (`INFO`, `ARGS` with the launch
options, then `FILE` for each starting file with its path), up to a `STRM` section. The stream runs to the end of
the file. Each event is a tag byte and a fixed payload:

| Tag | Event |
|---|---|
| 0x10+port | a poll of that port, the same as its last |
| 0x20+port | a poll that changed: connected, buttons, triggers, sticks (14 bytes) |
| 0x30 | the mouse, the same as the last frame |
| 0x31 | the mouse, changed: captured, x and y counts (9 bytes) |
| 0x40 | end of frame: five u32 checksums |
| 0x50 | an F6 mark (u32 frame) |
| 0x60 | the session left for the driving engine |
| 0x70 | a sound answer: buffer, query (status or play cursor), u32 answer |
| 0x71 | a sound answer the same as that buffer and query's last: buffer, query |

A recording that was stopped from outside ends part-way through a frame. Playback stops at the last whole one.
