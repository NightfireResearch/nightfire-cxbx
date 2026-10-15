# The test runner

A second Windows PC (`vr-desktop`) builds and runs replays so that long test runs need not tie up the development
machine. `tools/runner/remote.sh` drives it over SSH. A Linux machine, or WSL2 on the development machine, can
serve as a runner for the driving engine's Linux build ("A Linux runner, and WSL2", below).

**The runner is a convenience, never a requirement.** It only makes testing faster. Everything it does
(building, the replays, the screenshot comparisons) runs the same way on any development machine, and no part of
the workflow may depend on having a runner.

**This runner is Charlie's own machine, reachable only on Charlie's home LAN.** It is not publicly accessible, so it
will not work for Ryan, or for any agent working on Ryan's behalf. The SSH key (`~/.ssh/nightfire_runner`) lives on
Charlie's development machine, outside this repository. Anyone else wanting a runner needs their own machine set up
as described below, with `RUNNER_HOST`, `RUNNER_USER` and `RUNNER_KEY` pointing at it.

```
tools/runner/remote.sh sync                          # its checkout = this working tree
tools/runner/remote.sh build                         # action actioninject driving drivinginject
tools/runner/remote.sh run tools/ui/scripts/weapons.txt   # results in build/remote/weapons
tools/runner/remote.sh drive m2 "-Exe 'Release\driving.exe' -GameArgs '-mission 2' -DumpAfterMs 6000 -StopPattern 'dumping frame'"
                                                     # a drive_game.ps1 run; log and dumped frames in build/remote/m2
```

Frames from the runner are not pixel-identical to ones from another machine (its GPU and timing differ), so
compare a change against a baseline run on the runner itself. Two runs of the same build land a few frames
apart: compare the ones that land on the same frame number, or allow for animation.

Shadow tests and traces are switched on by environment variables, passed with `-GameEnv` (several separated by
`;`, which survives the runner's quoting where a list does not):

```
tools/runner/remote.sh drive shadow "-Exe 'Release\driving.exe' -GameArgs '-mission 2' -GameEnv 'NIGHTFIRE_MEMSHADOW=1;NIGHTFIRE_FSSHADOW=1' -DumpAfterMs 8000 -StopPattern 'dumping frame'"
```

A driving run with `-mission` needs a valid mission (1-8). Without one the game falls back to `psiLaunch.bin`, which
the runner does not have, and loads a track with an empty name.

`sync` brings the runner's checkout (`C:\nightfire-cxbx`) to this working tree. Commits that are not pushed go
across as a git bundle, uncommitted changes as a patch, and new files as a tar. The runner's ignored files
(`disc/`, `Release/`, `build/`) are left alone.

## Why runs go through a scheduled task

Direct3D needs the interactive desktop session, and an SSH login runs in session 0, which has none. So builds run
over SSH directly, but game runs do not. `remote.sh desktop '<command>'` writes the command to
`C:\nightfire-runner\job.sh` and starts the scheduled task `NightfireRunner`. The task runs
`tools/runner/desktop_job.sh` in the logged-in user's session, which leaves `job.log` and a `done` file holding the
exit code. `remote.sh setup` creates the task. It is set to run only when the user is logged on, so someone must be
logged in to the runner.

## Parallel slots

Games that need no window focus can run several at once, each in its own **slot**: a run folder
`C:\nightfire-slots\<n>`, holding a copy of the checkout's `Release\` (executables, DLLs, maps, `settings.ini`,
`saves\`, `tdata\`) and `disc` as a junction to the checkout's `disc\`. The game reads `..\disc` from its executable's
folder and writes everything else (frame dumps, logs, `sndtrace.log`, `crash.dmp`, saves) into its working folder,
so slots never share a file. Each slot also has its own job files (`C:\nightfire-runner\slot<n>\`) and scheduled task
(`NightfireRunner<n>`). Builds stay in the checkout and serial; a slot runs a copy, so building never locks a running
game's DLL.

```
tools/runner/remote.sh setup 8                       # the tasks: NightfireRunner and NightfireRunner1-8 (once)
tools/runner/remote.sh slots 8                       # refresh slots 1-8 from the checkout's build
RUNNER_SLOT=2 tools/runner/remote.sh drive m2 "..."  # one run in slot 2
tools/runner/parallel.sh runs.list                   # "<name> <drive_game.ps1 arguments>" per line, over 8 slots
BASE=lbase tools/runner/round.sh new                 # the full round: sync, build, every run, summary
python tools/runner/compare.py build/baselines/lbase_m1_idle build/remote/new_m1_idle
```

`RUNNER_CMAKE_ARGS` makes `build` (and so `round.sh`) configure the runner's checkout again with those options before
building: `-DNIGHTFIRE_SDL=ON`, for example (docs/driving/portable.md). The options stay in that checkout's cache, so
set `-DNIGHTFIRE_SDL=OFF` afterwards.

`round.sh <prefix> [lockstep|shadow|all] [missions]` runs the lockstep runs (each mission to tick 9000, idle and
accelerating, 16 frames and the sound call trace, sound in lockstep) and the shadow runs (every shadow at once, per
mission). It runs the shadow runs and the lockstep runs as two separate batches, and writes the summary to
`build/remote/<prefix>.summary`. With `BASE=<prefix>`, it compares each lockstep run against
`build/baselines/<BASE>_m<n>_<idle|accelerate>`. `EXE='Release\driving_standalone_test.exe'` runs the standalone executable's lockstep
runs instead (it builds that target, or the ones `TARGETS` names; it has no shadows, so the round skips them), and `ENV='NIGHTFIRE_TRAP_ORIGINAL=2'` adds variables to the
lockstep runs.

### Quick rounds and full rounds

- **Full round** (`round.sh <prefix> all`): every shadow on every mission and every lockstep run to tick 9000. It is the
  gate: run it before a merge or a push, and after anything wide (a platform layer, a shared header, a global's
  ownership).
- **Quick round** (`round.sh <prefix> quick [shadow-regex] [missions]`): the lockstep runs to tick 4500 (9 frames;
  `compare.py` compares them as a prefix of the 9000-tick baselines, frames paired by the tick `drive.log` gives) and
  only the shadows whose variable matches the regex (case aside; `''` for none, `.` for all). Use it while working on
  a package: name the package's shadows (`'gameai'`, `'snd(mix|filter)'`) and any missions that reach the change. A
  quick round never replaces the full one before a merge.
- **Sharding** (`SHARDS=k`): each mission's shadow variables are split into k groups (every kth variable), one run
  each, named `<prefix>_shadow_m<n>_s<i>`. The summary merges them per mission and says how many dumped their frame
  ("frame dumped: 2 of 2"). Default 1. Measured 15 October 2026: 16 half-runs on 8 slots took 6 min against 5 for
  the 8 whole runs, because each run's boot and load (about a minute) is paid twice; sharding only pays once the
  shadow runs are long again.

`TICKS_TO=<tick>` sets the lockstep runs' last tick for either kind; `BASELINES=<folder>` says where the `BASE` runs
are (default `build/baselines`).

**Fault reports in shadow runs.** A shadow test runs the original and the port inside a `__try`; a case that faults
on both sides passes, and a full shadow run catches several hundred faults. The loader
(src/loader/loadermain.cpp) gives a fault inside a `__try` of the inject DLL (its handler is
`NightfireHost_GuardHandler`'s, src/driving/devtools/FaultGuard.cpp) one line, `[loader] guarded fault at ...`, naming
the address and where the guarding function returns to, after the first 10 such faults have had the full report
(`NIGHTFIRE_FULL_FAULT_REPORTS=<n>` changes the 10). A fault outside every such `__try` gets the full report as
before, crash.dmp is still written for the first hard fault, and a fault that nothing catches gets the full report
and the dump from the unhandled-exception filter before the process ends. crash.log is kept open rather than
reopened per report. Measured on 15 October 2026 on the same shadows (8 slots, 630 to 1080 faults a mission):
before, a shadow run took 25 to 31 minutes, almost all of it faults, about 2 s each under the load of 8 runs (30 ms
alone). After, it takes 4.5 to 6 minutes. Even the full reports (now about 15 to 50 ms apart) became cheap once
crash.log stopped being reopened, which points at the open and close of a growing file as the cost.

**Lockstep runs are not paced.** A lockstep run simulates one tick per frame whatever the time, and its frames are
dumped from the back buffer before `Present`, so it neither waits for a frame's time nor presents every frame: every
50th is presented (`NIGHTFIRE_PRESENT_EVERY=<n>`; 0 presents and paces every frame, as before). The scheduler's
clock comes off the 50 Hz timer as well (`Teleport_BeforeRun`, one tick per call), since the game loop otherwise
waits for it. All 16 runs match lbase exactly (frames and sound traces to
tick 9000, 15 October 2026), and the batch takes 4 minutes, down from 8 (play runs at 150 to 300 frames a second;
what is left is mostly the load and the intro, which follow real time).

Only runs that send no keys can share the desktop. A lockstep run simulates exactly one tick per rendered frame,
holds the sound in lockstep, and applies `-GameHold` inside the game, so neither focus nor load matters: a starved
run is slower, not different (below). Shadow runs send no keys either. `parallel.sh` refuses a list with `-Keys`, `-HoldKey` or `-AfterHoldKeys`. Those runs
go to the serial `remote.sh drive`. Refresh the slots (`parallel.sh` does) only while none is running.

Two users of the runner at once (two sessions, say) each need their own slots and checkout: `RUNNER_SLOT_FIRST=9`
makes `slots` and `parallel.sh` use slots 9 onwards, and `RUNNER_REPO` points at a second checkout. Their runs share
the CPU, and the table below applies to the total.

Checked on 11 October 2026: three lockstep runs at once (mission 1 twice, mission 6 accelerating) matched serial runs
of the same build, with 0 pixels different and identical sound traces to tick 9000. A full round on 4 slots (16
lockstep runs and 8 shadow runs) took 35 minutes, against about four hours serially, and every lockstep run matched
its serial baseline.

How many slots: the runner (i7-9700K, 8 cores; RTX 3090; 32 GB) runs the 16 lockstep runs

| slots | 16 lockstep runs | CPU | result |
| --- | --- | --- | --- |
| 4 | 16 min | not measured | exact |
| 8 | 8 min | 35-78% | exact |
| 16 | 5 min | 97-100% | 10 runs' frames and 2 sound traces differed - before the fix below |
| 8, plus 10 busy-loop processes | 65 min | 100% | exact, after the fix |

Memory (about 2 GB for 16 games) and the GPU (at most about 80%, 2 GB) are nowhere near their limits. The CPU is.
8 is the default (`RUNNER_SLOTS`), and `round.sh` keeps the shadow runs, the heavy ones, out of the lockstep batch,
for speed: a saturated CPU no longer changes a lockstep run's result.

The 16-slot failures (11 October 2026) had two causes, both wall time leaking into lockstep:

- **The first pass of the game loop ran several ticks.** Lockstep set the scheduler's `oneTickPerRun` from inside a
  tick, so the first `Scheduler::Run` of a level (and the first after an `ERestart`, whose `Scheduler::Reset` clears
  the flag) ran as many ticks as the clock had moved since "Entering main loop" - two or three when the host was
  slow. Every later tick was then shifted (mission 6 idle differed from tick 0). `Teleport_BeforeRun` now sets the
  flag at the top of every `Scheduler::Run`.
- **`TIMER_gettick` jumped after a slow frame.** It followed the real clock again whenever more than a second passed
  between two ticks, meant for loads and movies; a starved frame looked the same (the first frame, which translates
  shaders, comes close to a second on its own). Voice fades and pitch ramps, the stream hold timeout and the
  Mission Failed screen's animated figure (`GGirl::UpdateTexture`) read it. It now switches to the clock only after
  1000 reads with no tick in between, which only a wait loop does.

`NIGHTFIRE_LOCKSTEP_JITTER=<ms>` (devtools/SndLockstep.cpp) tests for this kind of leak without loading the
runner: random delays of up to that many ms before each loop pass and tick, an occasional stall of over a second,
and delays during the load. With 40 ms, the build before the fix differed from the baselines in 15 of the 16
lockstep runs; with the fix, all 16 matched them exactly. GetLocalTime, read by the dashboard clock, also answers a
fixed time in lockstep. A starved run also showed a harness race: `drive_game.ps1` stopped the game a second after
"the last" dump was announced, before a slow game had written it. It now waits for the named file.

`parallel.sh` reads its list on a file descriptor of its own and gives `remote.sh` an empty stdin. `ssh` reads its
stdin, so inside a plain `while read` loop it swallows the rest of the list: the first try ran 4 of 24 runs and
reported success. Scripts that loop over `remote.sh` calls need the same care.

## Setting up a runner

- **SSH:** OpenSSH Server with key login, an administrator account, and Git Bash as the default shell
  (`HKLM:\SOFTWARE\OpenSSH` `DefaultShell`). The key is `~/.ssh/nightfire_runner`.
- **Build tools:** Visual Studio 2022 with the "Desktop development with C++" workload. Its bundled CMake is the one
  `build` uses.
- **Other tools:** Git, signed in to GitHub over SSH, and Python 3 with `capstone` and `pillow`.
- **Files git does not hold:** clone to `C:\nightfire-cxbx`, then copy `disc/`, `Release/settings.ini` and
  `Release/saves` from a working machine:

  ```
  tar cf - disc Release/settings.ini Release/saves | ssh -i ~/.ssh/nightfire_runner Charlie@vr-desktop.local 'cd /c/nightfire-cxbx && tar xf -'
  ```
- **Name:** a Mac finds a Windows machine by its bare name only with `.local` added (mDNS). `remote.sh` asks ssh to try
  `vr-desktop.local` first and fall back to `vr-desktop`, so the default works from either.
- **Login and power:** log in automatically, and never sleep. A disconnected RDP session can leave Direct3D with no
  display. Log in at the console, or reconnect, before a batch of runs.

## A Linux runner, and WSL2

The driving engine's standalone build also runs as a native 32-bit Linux executable (docs/driving/portable.md, step 4),
and the same commands drive a Linux runner: `remote.sh` asks the runner's system with `uname` (or takes
`RUNNER_OS=linux`), and `RUNNER_HOST=wsl` makes this PC's WSL2 the runner, with commands going through
`wsl.exe -d Ubuntu` (`RUNNER_WSL_DISTRO`) instead of ssh. What differs on Linux:

| | Windows runner | Linux runner (WSL2: everything under `~/nf/runner`) |
| --- | --- | --- |
| checkout | `C:\nightfire-cxbx` | `~/nightfire-cxbx`; `sync` sends the whole history as a bundle when the runner cannot fetch the base commit (WSL2 has no GitHub key) |
| `build` | MSBuild, `action actioninject driving drivinginject` | the `linux-i386` preset, `driving_standalone driving_standalone_test`; the executables and the libraries beside them are copied to the checkout's `Release/` |
| `desktop`, `drive` | a scheduled task in the desktop session, drive_game.ps1 | run directly, with `tools/drive_game.sh` |
| slots | `C:\nightfire-slots\<n>`, a disc junction | `~/nightfire-slots/<n>`, a disc symlink |
| `setup` | the scheduled tasks | the checkout and folders, and a list of what is missing |

`round.sh` on a Linux runner runs the standalone test executable's lockstep runs (`EXE` defaults to
`Release/driving_standalone_test`; there are no shadows), and `compare.py` compares them with a baseline as on
Windows. `RUNNER_BUILD=<folder on the runner>` makes `build` install a build made elsewhere instead of compiling
(another branch's, say), and `RUNNER_BUILD_JOBS` limits the compile's parallelism.

**drive_game.sh** takes drive_game.ps1's parameters, so the run lists are the same (`'Release\driving_standalone_test.exe'`
is read as `Release/driving_standalone_test`), but sends no keys: `-Keys`, `-HoldKey` and `-AfterHoldKeys` are
refused. It runs headless by default: SDL's offscreen video and dummy audio, dxvk presenting through SDL, and the
Vulkan driver `NF_VK_ICD` names - `lvp` (the default, Mesa's lavapipe on the CPU, the only one in WSL2), `system`
(the GPU's) or a path to an ICD file; `LP_NUM_THREADS` (default 3) is lavapipe's thread count. `RUNNER_DISPLAY=:0`
(passed on as `-Display`) runs the games on that display instead, with SDL's X11 driver and the system's Vulkan.

```
RUNNER_HOST=wsl tools/runner/remote.sh setup         # the checkout and folders; says what is missing
RUNNER_HOST=wsl tools/runner/remote.sh sync
RUNNER_HOST=wsl RUNNER_BUILD_JOBS=6 tools/runner/remote.sh build driving_standalone_test
RUNNER_HOST=wsl RUNNER_SLOTS=2 BASE=lwsl tools/runner/round.sh new lockstep
RUNNER_HOST=linux-box RUNNER_USER=charlie BASE=llin tools/runner/round.sh new   # a Linux machine over ssh
```

### Setting up a Linux runner

- **Packages and FFmpeg:** as docs/driving/portable.md's "Building" lists them (Ubuntu 24.04: the i386 architecture,
  clang, lld, cmake, ninja, the i386 X11/Wayland/sound development packages, `mesa-vulkan-drivers:i386`), and FFmpeg
  built once with `tools/fmv/build_ffmpeg.sh linux-i386 ~/ffmpeg-i386`. `build` configures with
  `-DNF_FFMPEG_LIB_DIR` set to `RUNNER_FFMPEG_LIB` (default `~/ffmpeg-i386/lib`) and, when `RUNNER_DXVK_DIR` is set,
  an extracted dxvk-native release (otherwise CMake downloads it); SDL3 comes by FetchContent. Under `RUNNER_HOST=wsl`
  these default to the Linux port's folders on this PC, `~/nf/ffmpeg-i386/lib` and `~/nf/dl/dxvk-native/x/usr`.
- **SSH** (not for WSL2): key login with `RUNNER_KEY`, bash as the login shell, and `RUNNER_USER` set to the account.
- **Files git does not hold:** `remote.sh setup` makes the checkout, `sync` fills it, and then the disc (the driving
  engine needs only `disc/driving`), `Release/settings.ini` and `Release/saves` go over as on Windows; `remote.sh ssh`
  runs in the checkout and passes stdin on:

  ```
  tar cf - disc/driving Release/settings.ini Release/saves | RUNNER_HOST=wsl tools/runner/remote.sh ssh 'tar xf -'
  ```

  settings.ini's `DiscPath=../disc` holds for the checkout and the slots. On lavapipe, set `RenderWidth=640` and
  `RenderHeight=480` in the runner's copy: rendering is on the CPU, and 1920x1080 is several times slower. A runner
  with a GPU can keep the Windows resolution.
- **Display:** none needed. For runs on a real display, a logged-in X session and `RUNNER_DISPLAY=:0` (plus
  `XAUTHORITY` in the runner's environment if its X server asks for one).

### Baselines for a Linux runner

Lockstep results are exact run to run on one machine and differ between machines (and between the Windows and Linux
builds' renderers), so a Linux runner keeps its own baselines, named for the machine: `lwsl_*` for this PC's WSL2,
and a new prefix for each other Linux runner (`llin_*` for vr-desktop under Linux, say). Make them with a round from
a tested commit, its prefix the baseline name, and copy the run folders into build/baselines:

```
RUNNER_HOST=wsl RUNNER_SLOTS=2 tools/runner/round.sh lwsl lockstep
cp -r build/remote/lwsl_m* build/baselines/
RUNNER_HOST=wsl RUNNER_SLOTS=2 BASE=lwsl tools/runner/round.sh new lockstep    # later rounds
```

Then check them with a second round against them (every run should be exact), and regenerate after any intended
change in behaviour, as for the Windows ones. Across builds on one machine the sound traces still compare:
`compare.py --sound build/baselines/lloc_m1_idle build/remote/lwsl_m1_idle` (lloc_* are this PC's Windows
baselines) tells whether the Linux build simulates as the Windows build does on the same CPU.

Checked on 15 October 2026 in WSL2 here (driving-port-linux 084b37f, lavapipe, 640x480, 2 slots, LP_NUM_THREADS=3):
a lockstep round of missions 1 and 2 took 14 minutes (5.5 to 7.5 minutes a run), and made lwsl_m1_*, lwsl_m2_*,
whose sound traces are identical to lloc's to tick 9000. A second round against them: m1 idle, m1 accelerate and m2
idle exact; m2 accelerate differed from tick 1 (an engine voice started two ticks late, and the frames dumped one
frame later), while two further m2 accelerate runs were exact. So the Linux build still has a start-up timing leak,
seen in one run of four; until it is found, a lone difference there wants a rerun before it counts.
