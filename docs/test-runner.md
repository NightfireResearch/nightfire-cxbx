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

## To do before the next full test round: parallel slots

A full round (lockstep runs of missions 1-8 to tick 9000, idle and accelerating, then every shadow on every mission)
takes about four hours, because `remote.sh` runs one game at a time: one `job.sh`, one scheduled task, one checkout,
and every run writes into the same `Release\` (frame dumps, `drive.log`, `sndtrace.log`, `crash.dmp`). The game itself
does not need to run alone: a lockstep run simulates exactly one tick per rendered frame and holds the sound in
lockstep too, so machine load changes only how long a run takes, never its frames or sound trace (runs repeat exactly
to tick 9000), and `-GameHold` is applied inside the game, so window focus does not matter.

The plan, about one to one and a half hours a round with three or four slots:

1. **Slots on the runner:** run folders, each a copy of `Release\` with `disc\` as a junction to the checkout's
   (remove such a folder by deleting the junction first - `git worktree remove` and some tools follow junctions and
   delete the target), and its own logs, dumps, traces, `settings.ini` and `saves\`. One build, copied to every slot.
2. **`remote.sh`:** `drive` takes a slot; `job.sh` and the `done` marker are per slot; the scheduled task launches
   several games in the one desktop session; results come back per slot. Builds stay serial.
3. **Prove it before relying on it:** one mission in two slots at once against a serial run of the same build -
   frames and sound traces identical - then one full round parallel against serial.

Watch for files the game writes outside its working folder (crash dumps, saves, loader logs), and for the shadow
runs' CPU-heavy first tick slowing each other down. A second runner (the development PC) could take a share when
nobody is using it.

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
