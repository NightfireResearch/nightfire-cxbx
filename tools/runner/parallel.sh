#!/bin/sh
# tools/runner/parallel.sh <list> [slots]: the drive runs in <list> shared out over the runner's slots (default
# RUNNER_SLOTS, else 8: more saturate the runner's CPU, and lockstep runs stop repeating exactly - see
# docs/test-runner.md), each slot taking the next run as it finishes one. A line is "<name> <drive_game.ps1
# arguments>", as for "remote.sh drive"; blank lines and lines starting with # are skipped. Results land in
# build/remote/<name> as for a serial run. The build already on the runner is used: sync and build first, then this
# refreshes the slots from it.
#
# Only runs that need no window focus can share the desktop: lockstep runs (-GameHold holds a key inside the game)
# and shadow runs. Runs that send keys (-Keys, -HoldKey) go to the serial "remote.sh drive", one at a time.
set -e
LIST=$1; N=${2:-${RUNNER_SLOTS:-8}}
[ -f "$LIST" ] || { sed -n '2,10p' "$0"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
if grep -v '^#' "$LIST" | grep -qE -- "-Keys|-HoldKey|-AfterHoldKeys"; then
    echo "parallel: $LIST has runs that send keys; run those with remote.sh drive"; exit 2
fi
"$HERE/remote.sh" slots "$N"

CLAIMS=$(mktemp -d)
total=$(grep -cvE '^(#|$)' "$LIST" || true)
start=$(date +%s)
worker() {   # slot: claims each line no other slot has (mkdir is atomic) and runs it. The list comes in on fd 3
    i=0          # and ssh gets /dev/null: ssh reads its stdin, and would swallow the rest of the list
    while IFS= read -r line <&3; do
        i=$((i + 1))
        case "$line" in ''|'#'*) continue ;; esac
        mkdir "$CLAIMS/$i" 2> /dev/null || continue
        name=${line%% *}; args=${line#* }
        t=$(date +%s)
        if RUNNER_SLOT=$1 "$HERE/remote.sh" drive "$name" "$args" < /dev/null > /dev/null 2>&1; then
            status=done; touch "$CLAIMS/$i.done"
        else
            status=FAILED
        fi
        echo "[$(date +%H:%M:%S)] slot $1: $name $status in $(( $(date +%s) - t ))s ($(ls "$CLAIMS" | grep -c done)/$total done)"
    done 3< "$LIST"
}
first=${RUNNER_SLOT_FIRST:-1}
for s in $(seq "$first" $((first + N - 1))); do worker "$s" & done
wait
finished=$(ls "$CLAIMS" | grep -c done || true)
rm -rf "$CLAIMS"
echo "parallel: $finished of $total runs done on $N slots in $(( ($(date +%s) - start) / 60 )) min"
[ "$finished" = "$total" ]
