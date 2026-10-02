#!/bin/sh
# Drives the second Windows machine (the test runner) over SSH: brings its checkout to this working tree, builds
# there, and runs replays in its logged-in desktop session, fetching the results back.
#
#   tools/runner/remote.sh sync                       # its checkout = this one (commits, uncommitted changes, new files)
#   tools/runner/remote.sh build [target ...]          # default: action actioninject driving drivinginject
#   tools/runner/remote.sh run <script.txt> [name] [timeout]   # tools/ui/run_menu.sh there; results to build/remote/<name>
#   tools/runner/remote.sh desktop '<command>'         # any command, in the desktop session, from the checkout
#   tools/runner/remote.sh ssh '<command>'             # any command over SSH (no desktop: fine for builds, not for D3D)
#
# Settings (environment): RUNNER_HOST (vr-desktop), RUNNER_USER (Charlie), RUNNER_KEY (~/.ssh/nightfire_runner),
# RUNNER_REPO (/c/nightfire-cxbx, the checkout there).
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
REPO=$(cd "$(dirname "$0")/../.." && pwd)

r() { ssh -i "$KEY" -o BatchMode=yes -o ConnectTimeout=15 "$USER_@$HOST" "$@"; }

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
    git ls-files --others --exclude-standard -z | grep -zv '\.mp4$' | xargs -0r tar cf - | r "cd $RREPO && tar xf -"
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
    printf 'cd %s\n%s\n' "$RREPO" "$1" | r "mkdir -p $JOBS && rm -f $JOBS/done && cat > $JOBS/job.sh && schtasks //run //tn NightfireRunner > /dev/null"
    # Wait for the job to finish (desktop_job.sh writes the exit code to done)
    while ! r "test -f $JOBS/done"; do sleep 5; done
    r "cat $JOBS/job.log; exit \$(cat $JOBS/done)"
    ;;
run)
    script=$1; name=${2:-$(basename "$1" .txt)}; timeout=${3:-300}
    "$0" desktop "tools/ui/run_menu.sh $script $name $timeout"
    mkdir -p "$REPO/build/remote/$name"
    r "cd $RREPO/build/menurun/$name && tar cf - run.log menu.log menu_shots d3d9_trace_*.log d3d9_dump_frame_*.bmp 2>/dev/null" |
        (cd "$REPO/build/remote/$name" && rm -rf menu_shots && tar xf -)
    echo "results in build/remote/$name"
    ;;
ssh)
    r "cd $RREPO && $*"
    ;;
setup)
    # The scheduled task that runs jobs in the desktop session: run only when the user is logged on (/IT), so it
    # starts in their interactive session. Started on demand by "desktop"; the trigger date is never reached.
    r "mkdir -p $JOBS && schtasks //create //f //tn NightfireRunner //it //ru $USER_ //sc once //sd 01/01/2099 //st 00:00 \
       //tr '\"C:\\Program Files\\Git\\bin\\bash.exe\" -l /c/nightfire-cxbx/tools/runner/desktop_job.sh'"
    ;;
*)
    sed -n '2,20p' "$0"
    exit 2
    ;;
esac
