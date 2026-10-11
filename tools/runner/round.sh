#!/bin/sh
# tools/runner/round.sh <prefix> [lockstep|shadow|all] [missions]: a driving test round on the runner's slots.
#
#   lockstep: for each mission two lockstep runs to tick 9000 (idle, and the accelerator held), 16 frames each and the
#             sound call trace, sound in lockstep too - <prefix>_m<n>_idle, <prefix>_m<n>_accelerate
#   shadow:   every shadow test at once on each mission - <prefix>_shadow_m<n>
#
# Syncs and builds driving + drivinginject first, then shares the runs out with parallel.sh (RUNNER_SLOTS slots,
# default 3). Results in build/remote/<name>; the summary in build/remote/<prefix>.summary. Settings (environment):
# BASE=<prefix> compares each lockstep run with build/baselines/<BASE>_m<n>_<idle|accelerate> (tools/runner/compare.py),
# SKIP=<regex> leaves out shadow variables matching it. Missions default to 1-8.
set -e
P=$1; WHAT=${2:-all}; MS=${3:-1 2 3 4 5 6 7 8}
[ -n "$P" ] || { sed -n '2,12p' "$0"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
cd "$REPO"
mkdir -p build/remote
LIST=build/remote/$P.list
SUMMARY=build/remote/$P.summary

"$HERE/remote.sh" sync > /dev/null
"$HERE/remote.sh" build driving drivinginject | grep " error " && exit 1

TICKS=500,1000,1500,2000,2500,3000,3500,4000,4500,5000,5500,6000,6500,7000,8000,9000
SHADOWS=$(grep -rhoE '"NIGHTFIRE_[A-Z0-9]*SHADOW[A-Z0-9]*"' src/driving src/inject_driving.cpp | tr -d '"' | sort -u |
          grep -vE "${SKIP:-^$}" | sed 's/$/=1/' | tr '\n' ';' | sed 's/;$//')
: > "$LIST"
# The shadow runs first: they take longest
if [ "$WHAT" != lockstep ]; then
    for m in $MS; do
        echo "${P}_shadow_m$m -Exe 'Release\\driving.exe' -GameArgs '-mission $m' -GameEnv '$SHADOWS' -DumpAfterMs 20000 -StopPattern 'dumping frame' -StartupWaitMs 3000 -TailWaitMs 1200000" >> "$LIST"
    done
fi
if [ "$WHAT" != shadow ]; then
    for m in $MS; do
        for h in idle accelerate; do
            hold=""; [ $h = accelerate ] && hold="-GameHold accelerate"
            echo "${P}_m${m}_$h -Exe 'Release\\driving.exe' -GameArgs '-mission $m' $hold -GameEnv 'NIGHTFIRE_LOCKSTEP=1;NIGHTFIRE_DUMP_TICKS=$TICKS;NIGHTFIRE_SNDTRACE=1;NIGHTFIRE_SNDLOCKSTEP=1' -StopPattern 'the last' -StartupWaitMs 3000 -TailWaitMs 1200000" >> "$LIST"
        done
    done
fi
"$HERE/parallel.sh" "$LIST" || echo "!! not every run finished: see the summary"

: > "$SUMMARY"
for m in $MS; do
    if [ "$WHAT" != lockstep ]; then
        L=build/remote/${P}_shadow_m$m/drive.log
        [ -f "$L" ] || { echo "== shadows, mission $m: NO LOG" >> "$SUMMARY"; continue; }
        # A summary line names its differences. Passes: "0 differ", "0 of n cases differ", "same, n NaN-only
        # differences" (the math shadows) and a "differ by kind:" list of zeros
        grep -a ' differ' "$L" | grep -av ' 0 differ\|: 0 of [0-9]* cases differ\|: same, [0-9]* NaN-only' |
            awk '/differ by kind:/ { s = $0; sub(/.*differ by kind:/, "", s); if (s ~ / [1-9][0-9]*(,|$)/) print; next } { print }' > "$L.differ" || true
        echo "== shadows, mission $m: $(grep -a -c ' differ' "$L") summaries, $(wc -l < "$L.differ") with differences, $(grep -a -c 'fault at' "$L") loader faults, frame dumped: $(grep -a -c 'dumping frame' "$L")" >> "$SUMMARY"
        cat "$L.differ" >> "$SUMMARY"
        grep -a 'faulted\|with faults\|Exception\|crash' "$L" | sort | uniq -c | head -20 >> "$SUMMARY" || true
    fi
    if [ "$WHAT" != shadow ]; then
        for h in idle accelerate; do
            D=build/remote/${P}_m${m}_$h
            if [ -n "$BASE" ]; then
                python "$HERE/compare.py" "build/baselines/${BASE}_m${m}_$h" "$D" >> "$SUMMARY" || true
            else
                echo "${P}_m${m}_$h: $(ls "$D" | grep -c 'bmp$') frames, $(wc -l < "$D/sndtrace.log" 2>/dev/null || echo no) trace lines" >> "$SUMMARY"
            fi
        done
    fi
done
cat "$SUMMARY"
