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
```

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
