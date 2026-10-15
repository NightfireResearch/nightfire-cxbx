#!/bin/bash
# tools/drive_game.sh: the Linux counterpart of tools/drive_game.ps1, for the runs that send no keys - lockstep runs
# (-GameHold holds a key inside the game) and shadow-less timed runs, which is everything tools/runner/round.sh makes.
# Starts the game with the environment and arguments given, waits for the stop pattern (or the timeout), stops it,
# and leaves its output in the log; frames, sndtrace.log and the rest stay in the working folder, as on Windows.
#
#   tools/drive_game.sh -Exe Release/driving_standalone_test -GameArgs '-mission 1' -GameHold accelerate \
#       -GameEnv 'NIGHTFIRE_LOCKSTEP=1;NIGHTFIRE_DUMP_TICKS=500,9000' -StopPattern 'the last' -TailWaitMs 1200000
#
# The parameters are drive_game.ps1's, so one run list serves both: -Exe (a Windows spelling such as
# 'Release\driving_standalone_test.exe' is taken as Release/driving_standalone_test), -WorkingDirectory (Release),
# -LogPath (the game's stdout; stderr goes to <LogPath>.err), -GameArgs, -GameEnv ("NAME=value" pairs separated by
# ";"), -GameHold, -DumpAfterMs, -Teleport, -StopPattern (an extended regular expression), -StartupWaitMs,
# -TailWaitMs. -Keys, -HoldKey and -AfterHoldKeys are refused: nothing here sends keys.
#
# Headless by default: SDL's offscreen video and dummy audio drivers, dxvk presenting through SDL, and the Vulkan
# driver NF_VK_ICD names - "lvp" (the default: Mesa's lavapipe, Vulkan on the CPU, the only choice in WSL2), a path
# to an ICD's JSON file, or "system" for whatever the Vulkan loader finds (a GPU's driver). LP_NUM_THREADS (default
# 3) is lavapipe's thread count. -Display runs it on the display in DISPLAY instead (:0 when unset), with SDL's X11
# video driver, its default audio and the system's Vulkan driver unless NF_VK_ICD says otherwise. On macOS the game
# renders through Metal on the GPU, its window hidden unless -Display.
set -u

Exe=Release/driving_standalone_test
WorkingDirectory=Release
LogPath=${TMPDIR:-/tmp}/nightfire-drive.log
GameArgs=""
GameEnv=""
GameHold=""
DumpAfterMs=-1
Teleport=""
StopPattern=""
StartupWaitMs=16000
TailWaitMs=30000
Display=""

while [ $# -gt 0 ]; do
    case "$1" in
    -Exe) Exe=$2; shift ;;
    -WorkingDirectory) WorkingDirectory=$2; shift ;;
    -LogPath) LogPath=$2; shift ;;
    -GameArgs) GameArgs=$2; shift ;;
    -GameEnv) GameEnv=$2; shift ;;
    -GameHold) GameHold=$2; shift ;;
    -DumpAfterMs) DumpAfterMs=$2; shift ;;
    -Teleport) Teleport=$2; shift ;;
    -StopPattern) StopPattern=$2; shift ;;
    -StartupWaitMs) StartupWaitMs=$2; shift ;;
    -TailWaitMs) TailWaitMs=$2; shift ;;
    -Display) Display=1 ;;
    -BetweenKeysMs|-HoldMs|-HoldDelayMs|-HoldAfterPattern) shift ;;   # only mean something with keys
    -Keys|-HoldKey|-AfterHoldKeys)
        echo "!! $1: drive_game.sh sends no keys; use -GameHold, or drive_game.ps1 on Windows"; exit 2 ;;
    *) echo "!! unknown parameter '$1' - see the header of $0"; exit 2 ;;
    esac
    shift
done

# A Windows spelling of the executable: separators, and .exe when there is no such file
Exe=${Exe//\\//}
[ -e "$Exe" ] || Exe=${Exe%.exe}
[ -x "$Exe" ] || { echo "!! no executable $Exe"; exit 1; }
Exe=$(cd "$(dirname "$Exe")" && pwd)/$(basename "$Exe")
cd "$WorkingDirectory" || exit 1
case "$LogPath" in /*) ;; *) LogPath=$OLDPWD/$LogPath ;; esac
rm -f "$LogPath" "$LogPath.err"

if [ "$(uname -s)" = Darwin ]; then
    # Metal, on the GPU: headless, the game takes "offscreen" as Cocoa with its window hidden (HostWindowSdl.cpp)
    [ -n "$Display" ] || export SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy
    icd=system
elif [ -n "$Display" ]; then
    export DISPLAY=${DISPLAY:-:0}
    export SDL_VIDEO_DRIVER=${SDL_VIDEO_DRIVER:-x11}   # the 32-bit SDL is built without Wayland
    icd=${NF_VK_ICD:-system}
else
    export SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy
    icd=${NF_VK_ICD:-lvp}
fi
case "$icd" in
    system) ;;
    lvp) export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ;;
    *) export VK_DRIVER_FILES=$icd ;;
esac
export DXVK_WSI_DRIVER=SDL3 LP_NUM_THREADS=${LP_NUM_THREADS:-3}
# dxvk's logs to stderr only (none in the working folder), and no state cache carried from run to run
export DXVK_LOG_LEVEL=${DXVK_LOG_LEVEL:-warn} DXVK_LOG_PATH=none DXVK_STATE_CACHE=0

unset NIGHTFIRE_TELEPORT NIGHTFIRE_HOLD NIGHTFIRE_DUMP_MS
[ -z "$Teleport" ] || export NIGHTFIRE_TELEPORT=$Teleport
[ -z "$GameHold" ] || export NIGHTFIRE_HOLD=$GameHold
[ "$DumpAfterMs" -lt 0 ] || export NIGHTFIRE_DUMP_MS=$DumpAfterMs
IFS=';' read -ra pairs <<< "$GameEnv"
for pair in ${pairs[@]+"${pairs[@]}"}; do   # bash 3.2 (macOS) calls an empty array unbound
    [ -n "$pair" ] && export "${pair%%=*}=${pair#*=}"
done

# shellcheck disable=SC2086   # GameArgs is split into words, as Start-Process splits its ArgumentList
nice "$Exe" $GameArgs > "$LogPath" 2> "$LogPath.err" &
pid=$!
# Stopped from outside (a dropped ssh connection, say): take the game down too
trap 'kill -KILL $pid 2> /dev/null; exit 1' HUP INT TERM

running() { kill -0 "$pid" 2> /dev/null; }
if [ "$(date +%N)" != N ]; then
    ms() { echo $(( $(date +%s%N) / 1000000 )); }
else   # BSD date (macOS) has no %N
    ms() { perl -MTime::HiRes=time -e 'printf "%d\n", time() * 1000'; }
fi
sleep_ms() { sleep "$(awk -v ms="$1" 'BEGIN { printf "%.3f", ms / 1000 }')"; }

sleep_ms "$StartupWaitMs"
if ! running; then
    wait "$pid"; code=$?
    echo "!! the game exited during startup (code $code) - see $LogPath"
    tail -n 25 "$LogPath" "$LogPath.err" 2> /dev/null
    exit 1
fi

deadline=$(( $(ms) + TailWaitMs ))
if [ -n "$StopPattern" ]; then
    while running && [ "$(ms)" -lt "$deadline" ]; do
        match=$(tail -n 40 "$LogPath" 2> /dev/null | grep -aE -- "$StopPattern" | tail -n 1)
        if [ -n "$match" ]; then
            echo ">> log matched '$StopPattern'"
            sleep 1
            # A frame dump the matched line names is written at the next present, which a starved game can be slow
            # to reach: wait for the file (up to a minute) before stopping it.
            dump=$(echo "$match" | sed -n 's/.*(\(d3d9_dump_frame_[0-9]*\.bmp\)).*/\1/p')
            if [ -n "$dump" ]; then
                dumpDeadline=$(( $(ms) + 60000 ))
                while [ ! -e "$dump" ] && running && [ "$(ms)" -lt "$dumpDeadline" ]; do sleep 0.25; done
                sleep 0.5
            fi
            break
        fi
        sleep 0.5
    done
else
    while running && [ "$(ms)" -lt "$deadline" ]; do sleep 0.5; done
fi

if running; then
    echo "=== still running; stopping it ==="
    kill -KILL "$pid"
    wait "$pid" 2> /dev/null
else
    wait "$pid"
    echo "=== the game exited, code $? ==="
fi

if grep -aE "exception 0x|unimplemented kernel import|does not implement" "$LogPath" "$LogPath.err" > /dev/null 2>&1; then
    echo "=== faults ==="
    grep -ahE "exception 0x|unimplemented kernel import|does not implement" "$LogPath" "$LogPath.err" | tail -n 20
else
    echo "=== no faults reported ==="
fi
echo "=== full log: $LogPath ==="
