# The test runner

A second Windows PC (`vr-desktop`) builds and runs replays so that long test runs need not tie up the development
machine. `tools/runner/remote.sh` drives it over SSH.

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
`build/baselines/<BASE>_m<n>_<idle|accelerate>`.

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
