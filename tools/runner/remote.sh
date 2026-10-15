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
#   tools/runner/remote.sh os                          # windows or linux: the runner's system
#
# Settings (environment): RUNNER_HOST (vr-desktop), RUNNER_USER (Charlie), RUNNER_KEY (~/.ssh/nightfire_runner),
# RUNNER_REPO (/c/nightfire-cxbx, the checkout there), RUNNER_SLOT (unset: "desktop", "drive" and "run" use the
# checkout's Release/ and the serial task; a number: that slot's run folder and task, so several run at once -
# tools/runner/parallel.sh shares a list of drive runs out over the slots), RUNNER_SLOT_FIRST (1: the first slot
# "slots" and parallel.sh use, so that two users of the runner can take separate slots - 1-8 and 9-16, say),
# RUNNER_PRESET (windows-x64: "build" builds the 64-bit standalone executables into Release64/, which "slots" copies).
#
# A Linux runner (RUNNER_OS=linux; asked with uname when unset, and implied by RUNNER_HOST=wsl, which is this PC's
# WSL2 - commands go through wsl.exe instead of ssh) runs the same commands its own way: its checkout is
# ~/nightfire-cxbx, "build" is the linux-i386 preset (default targets driving_standalone driving_standalone_test,
# installed into the checkout's Release/), slots are ~/nightfire-slots/<n> (a disc symlink), "setup" makes the
# checkout and folders, and "desktop" runs the command directly - no scheduled task - with tools/drive_game.sh for
# "drive", headless unless RUNNER_DISPLAY names a display (:0). Under RUNNER_HOST=wsl all of it lives in ~/nf/runner.
# Its own settings: RUNNER_BUILD (a build folder made elsewhere on it: "build" only installs from that),
# RUNNER_FFMPEG_LIB and RUNNER_DXVK_DIR (the preset's FFmpeg and dxvk-native, for the first configure),
# RUNNER_BUILD_JOBS (parallel compiles), RUNNER_WSL_DISTRO (Ubuntu). See docs/test-runner.md.
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
# Every call is also cut off after RUNNER_SSH_TIMEOUT seconds (default an hour: a full build fits): ssh has hung for
# hours across a runner reboot. timeout's -k follows the TERM with a KILL, which a hung ssh.exe does not ignore.
SSH_TIMEOUT=${RUNNER_SSH_TIMEOUT:-3600}
r() { timeout -k 10 "$SSH_TIMEOUT" ssh -i "$KEY" -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=15 -o ServerAliveCountMax=4 \
    -o CanonicalizeHostname=yes -o CanonicalDomains=local -o HostKeyAlias="$HOST" "$USER_@$HOST" "$@"; }
# A wait loop's poll: ssh has been seen to hang for half an hour despite ServerAlive, so each poll is cut off after a
# minute (a KILL ten seconds after the TERM: polls stuck through a runner reboot ignored the TERM alone), counts as
# "not yet", and the loop polls again.
poll() { timeout -k 10 60 ssh -i "$KEY" -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=15 \
    -o ServerAliveCountMax=4 -o CanonicalizeHostname=yes -o CanonicalDomains=local -o HostKeyAlias="$HOST" \
    "$USER_@$HOST" "$@" < /dev/null; }

# RUNNER_HOST=local: this machine is the runner - the same commands, run here without SSH, in a checkout of its own
# (RUNNER_REPO, default /q/nf-localrun: a clone with disc/ and Release/settings.ini, saves/, tdata/ as on a runner)
# and slot folders under /q/nightfire-slots. The desktop session is this one, so desktop jobs run directly.
LOCAL=
if [ "$HOST" = local ]; then
    LOCAL=1
    RREPO=${RUNNER_REPO:-/q/nf-localrun}
    JOBS=/q/nightfire-runner
    SLOTS=/q/nightfire-slots
    JOBDIR=$JOBS${SLOT:+/slot$SLOT}
    RUNDIR=$RREPO
    [ -z "$SLOT" ] || RUNDIR=$SLOTS/$SLOT
    mkdir -p "$JOBS"
    r() { bash -c "$*"; }
    poll() { bash -c "$*"; }
fi

# A Linux runner. Its paths are left for its shell to expand ($HOME). RUNNER_HOST=wsl: a distribution of this PC's
# WSL2, through wsl.exe; the command goes across base64-encoded, so that no quoting is lost on the way, and stdin and
# stdout stay free for data as with ssh. MSYS_NO_PATHCONV keeps Git Bash from rewriting the /paths in it.
OS=${RUNNER_OS:-}
if [ "$HOST" = wsl ]; then
    OS=linux
    r() { MSYS_NO_PATHCONV=1 timeout -k 10 "$SSH_TIMEOUT" wsl.exe -d "${RUNNER_WSL_DISTRO:-Ubuntu}" --cd "~" -e bash -c \
          "eval \"\$(echo $(printf '%s' "$*" | base64 -w0) | base64 -d)\""; }
    poll() { r "$@" < /dev/null; }
elif [ -z "$OS" ]; then
    if [ -n "$LOCAL" ]; then OS=windows
    else case $(r uname -s < /dev/null 2> /dev/null) in Linux*) OS=linux ;; *) OS=windows ;; esac
    fi
fi
if [ "$OS" = linux ]; then
    ROOT='$HOME'
    [ "$HOST" = wsl ] && ROOT='$HOME/nf/runner'
    RREPO=${RUNNER_REPO:-$ROOT/nightfire-cxbx}
    JOBS=$ROOT/nightfire-runner
    SLOTS=$ROOT/nightfire-slots
    JOBDIR=$JOBS${SLOT:+/slot$SLOT}
    RUNDIR=$RREPO
    [ -z "$SLOT" ] || RUNDIR=$SLOTS/$SLOT
    FFMPEG=${RUNNER_FFMPEG_LIB:-'$HOME/ffmpeg-i386/lib'}
    DXVK=${RUNNER_DXVK_DIR:-}
    if [ "$HOST" = wsl ]; then   # where the Linux port's setup on this PC keeps them
        FFMPEG=${RUNNER_FFMPEG_LIB:-'$HOME/nf/ffmpeg-i386/lib'}
        DXVK=${RUNNER_DXVK_DIR:-'$HOME/nf/dl/dxvk-native/x/usr'}
    fi
fi

cmd=$1; shift || true
case "$cmd" in
sync)
    cd "$REPO"
    # The newest commit of ours the runner can fetch: where this branch meets its upstream
    upstream=${RUNNER_SYNC_BASE:-'@{upstream}'}
    base=$(git merge-base HEAD "$upstream")
    head=$(git rev-parse HEAD)
    if [ "$OS" = linux ]; then
        # A Linux runner may have no access to the origin (WSL2 has no GitHub key): without the base commit it gets
        # the whole history as a bundle
        r "cd $RREPO && { GIT_TERMINAL_PROMPT=0 GIT_SSH_COMMAND='ssh -o BatchMode=yes -o ConnectTimeout=15' git fetch -q origin < /dev/null 2> /dev/null || true; }"
        r "cd $RREPO && git cat-file -e $base^{commit} 2> /dev/null" || base=
    else
        r "cd $RREPO && git fetch -q origin"
    fi
    if [ -z "$base" ]; then
        git bundle create - HEAD 2>/dev/null | r "mkdir -p $JOBS && cat > $JOBS/sync.bundle"
        r "cd $RREPO && git fetch -q $JOBS/sync.bundle HEAD && git checkout -qf FETCH_HEAD"
    elif [ "$head" != "$base" ]; then
        # Local commits not pushed: send them as a bundle
        git bundle create - "$base..HEAD" 2>/dev/null | r "mkdir -p $JOBS && cat > $JOBS/sync.bundle"
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
    if [ "$OS" = linux ]; then
        # The linux-i386 preset (docs/driving/portable.md), configured once (again with RUNNER_CMAKE_ARGS), then
        # incremental; the executables and the libraries beside them go to Release/, where runs and "slots" take
        # them from, as on Windows. RUNNER_BUILD=<folder>: a build made elsewhere on the runner, installed as it is.
        targets=${*:-driving_standalone driving_standalone_test}
        from=${RUNNER_BUILD:-build/linux-i386}
        compile="true"
        if [ -z "$RUNNER_BUILD" ]; then
            compile="{ [ -f build/linux-i386/CMakeCache.txt ] && [ -z '$RUNNER_CMAKE_ARGS' ] || \
                       cmake --preset linux-i386 -DNF_FFMPEG_LIB_DIR=$FFMPEG ${DXVK:+-DNF_DXVK_DIR=$DXVK} $RUNNER_CMAKE_ARGS > $JOBS/configure.log 2>&1 || \
                       { echo 'FAILED: configure, the end of $JOBS/configure.log:'; tail -20 $JOBS/configure.log; exit 1; }; } && \
                     nice cmake --build build/linux-i386 --target $targets -j ${RUNNER_BUILD_JOBS:-\$(nproc)} 2>&1 | grep -E 'error:|^FAILED|Linking'; \
                     [ \${PIPESTATUS[0]} = 0 ]"
        fi
        r "mkdir -p $JOBS && cd $RREPO && $compile && mkdir -p Release && cd $from && \
           cp -f \$(ls -d driving_standalone driving_standalone_test driving_standalone*.map lib*.so* 2>/dev/null) $RREPO/Release/ && \
           echo \"-> \$(cd $RREPO/Release && ls driving_standalone driving_standalone_test 2>/dev/null | tr '\n' ' ')in $RREPO/Release from $from\""
        exit
    fi
    if [ "$RUNNER_PRESET" = windows-x64 ]; then
        # The 64-bit standalone executables (docs/driving/64-bit.md): the preset's build, installed with its x86-64
        # DLLs into Release64/ beside Release/ (so ../disc is the disc), run as 'Release64\driving_standalone_test.exe'
        # with Release/ as the working folder
        targets=${*:-driving_standalone driving_standalone_test}
        r "cd $RREPO && CMAKE=\$(ls -d '/c/Program Files/Microsoft Visual Studio/2022/'*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin | head -1)/cmake.exe && \
           { [ -f build/windows-x64/CMakeCache.txt ] && [ -z '$RUNNER_CMAKE_ARGS' ] || \"\$CMAKE\" --preset windows-x64 $RUNNER_CMAKE_ARGS > /dev/null; } && \
           \"\$CMAKE\" --build build/windows-x64 --config Release --target $targets -- -m -v:m 2>&1 | grep -E ' error |-> '; \
           mkdir -p Release64 && cp -f build/windows-x64/Release/*.exe build/windows-x64/Release/*.dll build/windows-x64/Release/*.map Release64/ && cp -f build/windows-x64/Release/*.pdb Release64/ 2>/dev/null; true"
        exit
    fi
    targets=${*:-action actioninject driving drivinginject}
    # In-source configure, as here (Release/ beside disc/); once, then incremental. RUNNER_CMAKE_ARGS configures
    # again with those options (-DNIGHTFIRE_SDL=ON, say; they stay in the cache until set otherwise).
    r "cd $RREPO && CMAKE=\$(ls -d '/c/Program Files/Microsoft Visual Studio/2022/'*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin | head -1)/cmake.exe && \
       { [ -f CMakeCache.txt ] && [ -z '$RUNNER_CMAKE_ARGS' ] || \"\$CMAKE\" -B . -A Win32 $RUNNER_CMAKE_ARGS > /dev/null; } && \
       \"\$CMAKE\" --build . --config Release --target $targets -- -m -v:m 2>&1 | grep -E ' error |-> '"
    ;;
desktop)
    [ -n "$1" ] || { echo "desktop: no command"; exit 2; }
    if [ "$OS" = linux ]; then
        # No session to reach: the command runs here and now, on RUNNER_DISPLAY's display when that is set
        r "mkdir -p $JOBDIR && cd $RUNDIR && { ${RUNNER_DISPLAY:+export DISPLAY=$RUNNER_DISPLAY; }$1
           } > $JOBDIR/job.log 2>&1; rc=\$?; cat $JOBDIR/job.log; exit \$rc"
        exit
    fi
    if [ -n "$LOCAL" ]; then
        mkdir -p "$JOBDIR"
        (cd "$RUNDIR" && sh -c "$1") > "$JOBDIR/job.log" 2>&1 && rc=0 || rc=$?
        cat "$JOBDIR/job.log"; exit $rc
    fi
    printf 'cd %s\n%s\n' "$RUNDIR" "$1" | r "mkdir -p $JOBDIR && rm -f $JOBDIR/done && cat > $JOBDIR/job.sh && schtasks //run //tn $TASK > /dev/null"
    # Wait for the job to finish (desktop_job.sh writes the exit code to done)
    while ! poll "test -f $JOBDIR/done"; do sleep 5; done
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
    if [ "$OS" = linux ]; then
        "$0" desktop "rm -f Release/d3d9_dump_* Release/d3d9_trace_*.log Release/sndtrace.log*; bash $RREPO/tools/drive_game.sh $args ${RUNNER_DISPLAY:+-Display} -LogPath $JOBDIR/drive.log > /dev/null 2>&1; true" > /dev/null
    else
        "$0" desktop "rm -f Release/d3d9_dump_* Release/d3d9_trace_*.log Release/sndtrace.log; powershell -NoProfile -ExecutionPolicy Bypass -File '$(win $RREPO)/tools/drive_game.ps1' $args -LogPath '$(win $JOBDIR)/drive.log' > /dev/null 2>&1; true" > /dev/null
    fi
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
        if [ "$OS" = linux ]; then
            r "mkdir -p $d/Release && cd $d/Release && find . -maxdepth 1 -type f -delete && rm -rf saves tdata cache && \
               cp $RREPO/Release/driving_standalone* $RREPO/Release/lib*.so* $RREPO/Release/settings.ini . && \
               cp -r $RREPO/Release/saves . && { [ ! -d $RREPO/Release/tdata ] || cp -r $RREPO/Release/tdata .; } && \
               { [ -e $d/disc ] || ln -s $RREPO/disc $d/disc; }"
            continue
        fi
        r "mkdir -p $d/Release && cd $d/Release && find . -maxdepth 1 -type f -delete && rm -rf saves tdata cache && \
           cp $RREPO/Release/*.exe $RREPO/Release/*.dll $RREPO/Release/*.map $RREPO/Release/settings.ini . && \
           cp -r $RREPO/Release/saves $RREPO/Release/tdata . && \
           { [ ! -d $RREPO/Release64 ] || { rm -rf $d/Release64 && cp -r $RREPO/Release64 $d/Release64; }; } && \
           { [ -e $d/disc ] || powershell -NoProfile -Command \"New-Item -ItemType Junction -Path '$(win $d)/disc' -Target '$(win $RREPO)/disc'\" > /dev/null; }"
    done
    echo "slots $first-$((first + n - 1)) refreshed from $RREPO/Release"
    ;;
setup)
    # The scheduled tasks that run jobs in the desktop session: the serial one, NightfireRunner, and one per slot,
    # NightfireRunner1... Run only when the user is logged on (/IT), so they start in their interactive session.
    # Started on demand by "desktop"; the trigger date is never reached.
    n=${1:-8}
    if [ "$OS" = linux ]; then
        # No tasks: the checkout (filled by "sync"; its origin is ours), the job and slot folders, and a check of what
        # a build and the runs need
        r "mkdir -p $JOBS $SLOTS && { [ -d $RREPO/.git ] || { git init -q $RREPO && git -C $RREPO remote add origin $(git -C "$REPO" remote get-url origin); }; } && \
           for t in git cmake ninja clang lld nice; do command -v \$t > /dev/null || echo \"missing: \$t\"; done; \
           [ -e /usr/share/vulkan/icd.d/lvp_icd.json ] || echo 'missing: lavapipe (mesa-vulkan-drivers:i386)'; \
           [ -e $FFMPEG/libavcodec.so.62 ] || echo \"missing: FFmpeg in $FFMPEG (tools/fmv/build_ffmpeg.sh linux-i386)\"; \
           for f in disc Release/settings.ini Release/saves; do [ -e $RREPO/\$f ] || echo \"missing: $RREPO/\$f (copy it over: docs/test-runner.md)\"; done; \
           echo \"checkout $RREPO, slots in $SLOTS\""
        exit
    fi
    for i in '' $(seq 1 "$n"); do
        r "mkdir -p $JOBS${i:+/slot$i} && schtasks //create //f //tn NightfireRunner$i //it //ru $USER_ //sc once //sd 01/01/2099 //st 00:00 \
           //tr '\"C:\\Program Files\\Git\\bin\\bash.exe\" -l $RREPO/tools/runner/desktop_job.sh $i' > /dev/null"
    done
    echo "tasks NightfireRunner and NightfireRunner1-$n"
    ;;
os)
    echo "${OS:-windows}"
    ;;
*)
    sed -n '2,39p' "$0"
    exit 2
    ;;
esac
