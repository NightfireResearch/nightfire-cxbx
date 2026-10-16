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
| `NIGHTFIRE_DEMO_FAST=N` | present every Nth frame and do not pace, for unattended runs (many times real speed; leave it unset to watch and listen) |
| `NIGHTFIRE_DEMO_DUMP=f1,f2,...` | dump those frames, as F7 does |
| `NIGHTFIRE_DEMO_DUMP_GAMESTATE=f` | print `GameState`'s bytes at that frame (they are also printed at the first frame where `gamestate` differs) |
| `NIGHTFIRE_ORIGINAL=0xADDR,...` | run those functions as the original, ports or not: to compare a parcel of ports with the originals, to bisect which one changed the game, or to measure what the original's code reaches |

Tools that build on these:
- `tools/demo/play_demos.ps1` plays every recording.
- `tools/demo/demo_coverage.py` lists which functions, or with `--blocks` which basic blocks, the recordings reach.
- `tools/demo/bisect_parcel.py` finds which port of a parcel makes a demo differ, by playing it with half the
  suspects restored to the original at a time (each step is `tools/demo/bisect_originals.ps1`).
- `tools/draft_calls.py` lists calls from ported code into drafts: game functions we define but do not patch
  in. Such a call runs the draft where the game ran the original.

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
