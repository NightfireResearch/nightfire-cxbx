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
tools/runner/remote.sh setup 4                       # the tasks: NightfireRunner and NightfireRunner1-4 (once)
tools/runner/remote.sh slots 4                       # refresh slots 1-4 from the checkout's build
RUNNER_SLOT=2 tools/runner/remote.sh drive m2 "..."  # one run in slot 2
tools/runner/parallel.sh runs.list 4                 # "<name> <drive_game.ps1 arguments>" per line, over 4 slots
RUNNER_SLOTS=4 BASE=lbase tools/runner/round.sh new  # the full round: sync, build, every run, summary
python tools/runner/compare.py build/baselines/lbase_m1_idle build/remote/new_m1_idle
```

`round.sh <prefix> [lockstep|shadow|all] [missions]` runs the lockstep runs (each mission to tick 9000, idle and
accelerating, 16 frames and the sound call trace, sound in lockstep) and the shadow runs (every shadow at once, per
mission). It writes the summary to `build/remote/<prefix>.summary`. With `BASE=<prefix>`, it compares each lockstep
run against `build/baselines/<BASE>_m<n>_<idle|accelerate>`.

Only runs that send no keys can share the desktop. A lockstep run simulates exactly one tick per rendered frame,
holds the sound in lockstep, and applies `-GameHold` inside the game, so load and focus change only how long it takes.
Shadow runs send no keys either. `parallel.sh` refuses a list with `-Keys`, `-HoldKey` or `-AfterHoldKeys`. Those runs
go to the serial `remote.sh drive`. Refresh the slots (`parallel.sh` does) only while none is running.

Checked on 11 October 2026: three lockstep runs at once (mission 1 twice, mission 6 accelerating) matched serial runs
of the same build, with 0 pixels different and identical sound traces to tick 9000. A full round on 4 slots (16
lockstep runs and 8 shadow runs) took 35 minutes, against about four hours serially, and every lockstep run matched
its serial baseline.

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
