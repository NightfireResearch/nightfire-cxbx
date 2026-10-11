#!/bin/sh
# Drives the second Windows machine (the test runner) over SSH: brings its checkout to this working tree, builds
# there, and runs replays in its logged-in desktop session, fetching the results back.
#
#   tools/runner/remote.sh sync                       # its checkout = this one (commits, uncommitted changes, new files)
#   tools/runner/remote.sh build [target ...]          # default: action actioninject driving drivinginject
#   tools/runner/remote.sh run <script.txt> [name] [timeout]   # tools/ui/run_menu.sh there; results to build/remote/<name>
#   tools/runner/remote.sh desktop '<command>'         # any command, in the desktop session, from the checkout
#   tools/runner/remote.sh drive <name> "<args>"       # tools/drive_game.ps1 there; log and dumped frames to build/remote/<name>
#   tools/runner/remote.sh ssh '<command>'             # any command over SSH (no desktop: fine for builds, not for D3D)
#   tools/runner/remote.sh slots [n]                   # n run folders (default 8), each a copy of the built Release/
#   tools/runner/remote.sh setup [n]                   # the scheduled tasks: the serial one and one per slot
#
# Settings (environment): RUNNER_HOST (vr-desktop), RUNNER_USER (Charlie), RUNNER_KEY (~/.ssh/nightfire_runner),
# RUNNER_REPO (/c/nightfire-cxbx, the checkout there), RUNNER_SLOT (unset: "desktop", "drive" and "run" use the
# checkout's Release/ and the serial task; a number: that slot's run folder and task, so several run at once -
# tools/runner/parallel.sh shares a list of drive runs out over the slots), RUNNER_SLOT_FIRST (1: the first slot
# "slots" and parallel.sh use, so that two users of the runner can take separate slots - 1-8 and 9-16, say).
#
# A runner is only a speed-up: everything here also runs locally (tools/ui/run_menu.sh, cmake), and nothing may
# require one. The defaults are Charlie's runner, reachable only on Charlie's home LAN - not a public machine, so not usable by
# Ryan or agents working for him. The key is kept outside the repository. Set up your own runner and point these
# settings at it (docs/test-runner.md).
#
# Why the desktop: Direct3D needs the interactive session, which an SSH login (session 0) is not. "desktop" writes a
# job file and starts the scheduled task NightfireRunner (created by "setup"), which runs desktop_job.sh in the
# logged-in user's session; this waits for its "done" file. disc/ and Release/settings.ini, Release/saves are not in
# git: copy them over once (see docs/test-runner.md).
set -e
HOST=${RUNNER_HOST:-vr-desktop}
USER_=${RUNNER_USER:-Charlie}
KEY=${RUNNER_KEY:-$HOME/.ssh/nightfire_runner}
RREPO=${RUNNER_REPO:-/c/nightfire-cxbx}
JOBS=/c/nightfire-runner
SLOTS=/c/nightfire-slots
REPO=$(cd "$(dirname "$0")/../.." && pwd)

# A slot has its own job files (slot<n>/ beside the serial ones), task (NightfireRunner<n>) and run folder:
# <n>/Release, a copy of the checkout's, beside <n>/disc, a junction to the checkout's disc/. The game reads ../disc
# from its executable's folder and writes everything else - dumps, logs, traces, saves - into its working folder.
SLOT=${RUNNER_SLOT:-}
JOBDIR=$JOBS${SLOT:+/slot$SLOT}
TASK=NightfireRunner$SLOT
RUNDIR=$RREPO
[ -z "$SLOT" ] || RUNDIR=$SLOTS/$SLOT
win() { echo "$1" | sed 's#^/\(.\)/#\1:/#'; }   # /c/x -> c:/x, for Windows programs

# CanonicalDomains: macOS resolves a Windows machine's bare name only with ".local" (mDNS); ssh tries that first
# and falls back to the bare name, so either works. HostKeyAlias checks the host key under the configured name
# whichever of the two it connected by, so one known_hosts entry serves both (BatchMode cannot ask about a new one).
# ServerAlive: a connection that drops silently fails within a minute instead of hanging the caller's wait loop.
r() { ssh -i "$KEY" -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=15 -o ServerAliveCountMax=4 \
    -o CanonicalizeHostname=yes -o CanonicalDomains=local -o HostKeyAlias="$HOST" "$USER_@$HOST" "$@"; }

cmd=$1; shift || true
case "$cmd" in
sync)
    cd "$REPO"
    r "cd $RREPO && git fetch -q origin"
    # The newest commit of ours the runner can fetch: where this branch meets its upstream
    base=$(git merge-base HEAD '@{upstream}')
    head=$(git rev-parse HEAD)
    if [ "$head" != "$base" ]; then
        # Local commits not pushed: send them as a bundle
        git bundle create - "$base..HEAD" 2>/dev/null | r "cat > $JOBS/sync.bundle"
        r "cd $RREPO && git fetch -q $JOBS/sync.bundle HEAD && git checkout -qf FETCH_HEAD"
    else
        r "cd $RREPO && git checkout -qf $base"
    fi
    # Uncommitted changes and new (untracked, not ignored) files on top. git clean leaves ignored files (disc/,
    # Release/, build/) alone.
    r "cd $RREPO && git clean -fdq"
    if ! git diff --quiet HEAD; then
        git diff --binary HEAD | r "cd $RREPO && git apply --whitespace=nowarn -"
    fi
    if [ -n "$(git ls-files --others --exclude-standard | grep -v '\.mp4$')" ]; then
        git ls-files --others --exclude-standard -z | grep -zv '\.mp4$' | xargs -0 tar cf - | r "cd $RREPO && tar xf -"
    fi
    echo "runner at $(r "cd $RREPO && git rev-parse --short HEAD") plus $(git diff --stat HEAD | tail -1)"
    ;;
build)
    targets=${*:-action actioninject driving drivinginject}
    # In-source configure, as here (Release/ beside disc/); once, then incremental
    r "cd $RREPO && CMAKE=\$(ls -d '/c/Program Files/Microsoft Visual Studio/2022/'*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin | head -1)/cmake.exe && \
       { [ -f CMakeCache.txt ] || \"\$CMAKE\" -B . -A Win32 > /dev/null; } && \
       \"\$CMAKE\" --build . --config Release --target $targets -- -m -v:m 2>&1 | grep -E ' error |-> '"
    ;;
desktop)
    [ -n "$1" ] || { echo "desktop: no command"; exit 2; }
    printf 'cd %s\n%s\n' "$RUNDIR" "$1" | r "mkdir -p $JOBDIR && rm -f $JOBDIR/done && cat > $JOBDIR/job.sh && schtasks //run //tn $TASK > /dev/null"
    # Wait for the job to finish (desktop_job.sh writes the exit code to done)
    while ! r "test -f $JOBDIR/done"; do sleep 5; done
    r "cat $JOBDIR/job.log; exit \$(cat $JOBDIR/done)"
    ;;
run)
    script=$1; name=${2:-$(basename "$1" .txt)}; timeout=${3:-300}
    "$0" desktop "tools/ui/run_menu.sh $script $name $timeout"
    mkdir -p "$REPO/build/remote/$name"
    r "cd $RREPO/build/menurun/$name && tar cf - run.log menu.log menu_shots d3d9_trace_*.log d3d9_dump_frame_*.bmp 2>/dev/null" |
        (cd "$REPO/build/remote/$name" && rm -rf menu_shots && tar xf -)
    echo "results in build/remote/$name"
    ;;
drive)
    # tools/runner/remote.sh drive <name> "<tools/drive_game.ps1 arguments>": a drive_game run in the desktop session;
    # its log, the frames it dumped and its sound trace if it wrote one come back to build/remote/<name> (drive.log,
    # d3d9_dump_frame_*.bmp, sndtrace.log). Each run starts without the last one's outputs: a frame dump also writes
    # the frame's textures and draw trace, which stay on the runner.
    name=$1; args=$2
    [ -n "$name" ] && [ -n "$args" ] || { echo "drive: tools/runner/remote.sh drive <name> \"<drive_game.ps1 arguments>\""; exit 2; }
    "$0" desktop "rm -f Release/d3d9_dump_* Release/d3d9_trace_*.log Release/sndtrace.log; powershell -NoProfile -ExecutionPolicy Bypass -File '$(win $RREPO)/tools/drive_game.ps1' $args -LogPath '$(win $JOBDIR)/drive.log' > /dev/null 2>&1; true" > /dev/null
    mkdir -p "$REPO/build/remote/$name"
    rm -f "$REPO/build/remote/$name"/d3d9_dump_frame_*.bmp "$REPO/build/remote/$name/sndtrace.log"
    r "cp $JOBDIR/drive.log $RUNDIR/Release/drive.log; cd $RUNDIR/Release && tar cf - drive.log \$(ls d3d9_dump_frame_*.bmp 2>/dev/null | grep -v alpha) \$(ls sndtrace.log 2>/dev/null)" |
        (cd "$REPO/build/remote/$name" && tar xf -)
    echo "build/remote/$name: $(ls "$REPO/build/remote/$name" | wc -l) files"
    ;;
ssh)
    r "cd $RREPO && $*"
    ;;
slots)
    # Fresh run folders from the checkout's build: its executables, DLLs and maps, settings.ini, saves/ and tdata/.
    # After "build", before runs start in the slots - never while one is running.
    n=${1:-8}; first=${RUNNER_SLOT_FIRST:-1}
    for i in $(seq "$first" $((first + n - 1))); do
        d=$SLOTS/$i
        r "mkdir -p $d/Release && cd $d/Release && find . -maxdepth 1 -type f -delete && rm -rf saves tdata cache && \
           cp $RREPO/Release/*.exe $RREPO/Release/*.dll $RREPO/Release/*.map $RREPO/Release/settings.ini . && \
           cp -r $RREPO/Release/saves $RREPO/Release/tdata . && \
           { [ -e $d/disc ] || powershell -NoProfile -Command \"New-Item -ItemType Junction -Path '$(win $d)/disc' -Target '$(win $RREPO)/disc'\" > /dev/null; }"
    done
    echo "slots $first-$((first + n - 1)) refreshed from $RREPO/Release"
    ;;
setup)
    # The scheduled tasks that run jobs in the desktop session: the serial one, NightfireRunner, and one per slot,
    # NightfireRunner1... Run only when the user is logged on (/IT), so they start in their interactive session.
    # Started on demand by "desktop"; the trigger date is never reached.
    n=${1:-8}
    for i in '' $(seq 1 "$n"); do
        r "mkdir -p $JOBS${i:+/slot$i} && schtasks //create //f //tn NightfireRunner$i //it //ru $USER_ //sc once //sd 01/01/2099 //st 00:00 \
           //tr '\"C:\\Program Files\\Git\\bin\\bash.exe\" -l $RREPO/tools/runner/desktop_job.sh $i' > /dev/null"
    done
    echo "tasks NightfireRunner and NightfireRunner1-$n"
    ;;
*)
    sed -n '2,28p' "$0"
    exit 2
    ;;
esac
