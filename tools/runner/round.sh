#!/bin/sh
# tools/runner/round.sh <prefix> [lockstep|shadow|all] [missions]: a driving test round on the runner's slots.
#        tools/runner/round.sh <prefix> quick [shadow-regex] [missions]: a quick round (docs/test-runner.md).
#
#   lockstep: for each mission two lockstep runs to tick 9000 (idle, and the accelerator held), 16 frames each and the
#             sound call trace, sound in lockstep too - <prefix>_m<n>_idle, <prefix>_m<n>_accelerate
#   shadow:   every shadow test at once on each mission - <prefix>_shadow_m<n>; with SHARDS=k the shadow variables
#             are split into k groups, one run each - <prefix>_shadow_m<n>_s<1..k>
#   quick:    the lockstep runs to tick 4500 only, compared with the baselines to that tick, and only the shadows
#             whose variable (NIGHTFIRE_...SHADOW...) matches shadow-regex, case aside ('' for none, . for all)
#
# Syncs and builds the targets (TARGETS) first, then shares the runs out with parallel.sh (RUNNER_SLOTS slots,
# default 8): the shadow runs, then the lockstep runs, as two batches. A lockstep run repeats exactly only while the
# runner's CPU has room - 16 at once on its 8 cores saturated it, and frames and sound traces differed - and the
# shadow runs are the heavy ones, so the two never share the machine. Results in build/remote/<name>; the summary in
# build/remote/<prefix>.summary, one entry per mission (a sharded mission's runs merged). Settings (environment):
#   BASE=<prefix>       compare each lockstep run with $BASELINES/<BASE>_m<n>_<idle|accelerate> (tools/runner/compare.py)
#   BASELINES=<folder>  where the BASE runs are (default build/baselines)
#   SKIP=<regex>        leave out shadow variables matching it
#   SHARDS=<k>          split each mission's shadow variables into k runs (default 1)
#   TICKS_TO=<tick>     the lockstep runs' last tick (default 9000; quick 4500)
#   EXE=<path>          run another executable (Release\driving_standalone_test.exe: the standalone build's lockstep
#                       runs; it has no shadows, so they are skipped)
#                       ('Release64\driving_standalone_test.exe': the 64-bit build's, from the windows-x64 preset)
#   TARGETS=<targets>   the targets built (default driving drivinginject, or driving_standalone_test with that EXE)
#   ENV=<VAR=value;...> add variables to the lockstep runs (NIGHTFIRE_TRAP_ORIGINAL=2, say)
# Missions default to 1-8. On a Linux runner (tools/runner/remote.sh: RUNNER_OS, RUNNER_HOST=wsl) the round is the
# standalone test executable's lockstep runs (EXE defaults to Release/driving_standalone_test), with tools/drive_game.sh.
set -e
P=$1; WHAT=${2:-all}
if [ "$WHAT" = quick ]; then
    ONLY=${3:-}; MS=${4:-1 2 3 4 5 6 7 8}; TO=${TICKS_TO:-4500}
else
    ONLY=.; MS=${3:-1 2 3 4 5 6 7 8}; TO=${TICKS_TO:-9000}
fi
[ -n "$P" ] || { sed -n '2,28p' "$0"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
cd "$REPO"
mkdir -p build/remote
SHADOW_LIST=build/remote/$P.shadow.list
LOCKSTEP_LIST=build/remote/$P.lockstep.list
SUMMARY=build/remote/$P.summary
export RUNNER_SLOTS=${RUNNER_SLOTS:-8}
# The runner's system, asked once for every remote.sh call of the round
export RUNNER_OS=${RUNNER_OS:-$("$HERE/remote.sh" os)}
if [ "$RUNNER_OS" = linux ]; then EXE=${EXE:-Release/driving_standalone_test}; else EXE=${EXE:-'Release\driving.exe'}; fi
case "$EXE" in
    Release64*) export RUNNER_PRESET=${RUNNER_PRESET:-windows-x64} ;;   # the 64-bit build (remote.sh build)
esac
case "$EXE" in
    *standalone*) TARGETS=${TARGETS:-driving_standalone_test}; ONLY="" ;;   # no shadows in the standalone build
    *) TARGETS=${TARGETS:-driving drivinginject} ;;
esac
K=${SHARDS:-1}

"$HERE/remote.sh" sync > /dev/null
"$HERE/remote.sh" build $TARGETS | grep -E " error |error:|^FAILED" && exit 1

# The dump ticks up to the last, which is always one of them (drive_game stops at "the last")
TICKS=$(echo 500 1000 1500 2000 2500 3000 3500 4000 4500 5000 5500 6000 6500 7000 8000 9000 |
        tr ' ' '\n' | awk -v to="$TO" '$1 < to' | tr '\n' ',')$TO
SHADOWS=""
[ -z "$ONLY" ] || SHADOWS=$(grep -rhoE '"NIGHTFIRE_[A-Z0-9]*SHADOW[A-Z0-9]*"' src/driving src/inject_driving.cpp | tr -d '"' |
                            sort -u | grep -iE "$ONLY" | grep -vE "${SKIP:-^$}" || true)
# Shard s of k: every kth variable, from the sth
shard() { echo "$SHADOWS" | awk -v k="$K" -v s="$1" 'NF && (NR - 1) % k == s - 1 { print $0 "=1" }' | tr '\n' ';' | sed 's/;$//'; }
shard_name() { if [ "$K" = 1 ]; then echo "${P}_shadow_m$1"; else echo "${P}_shadow_m$1_s$2"; fi; }
: > "$SHADOW_LIST"
: > "$LOCKSTEP_LIST"
for m in $MS; do
    if [ "$WHAT" != lockstep ] && [ -n "$SHADOWS" ]; then
        for s in $(seq 1 "$K"); do
            echo "$(shard_name "$m" "$s") -Exe '$EXE' -GameArgs '-mission $m' -GameEnv '$(shard "$s")' -DumpAfterMs 20000 -StopPattern 'dumping frame' -StartupWaitMs 3000 -TailWaitMs 3600000" >> "$SHADOW_LIST"
        done
    fi
    if [ "$WHAT" != shadow ]; then
        for h in idle accelerate; do
            hold=""; [ $h = accelerate ] && hold="-GameHold accelerate"
            echo "${P}_m${m}_$h -Exe '$EXE' -GameArgs '-mission $m' $hold -GameEnv 'NIGHTFIRE_LOCKSTEP=1;NIGHTFIRE_DUMP_TICKS=$TICKS;NIGHTFIRE_SNDTRACE=1;NIGHTFIRE_SNDLOCKSTEP=1${ENV:+;$ENV}' -StopPattern 'the last' -StartupWaitMs 3000 -TailWaitMs 1200000" >> "$LOCKSTEP_LIST"
        done
    fi
done
for list in "$SHADOW_LIST" "$LOCKSTEP_LIST"; do
    [ -s "$list" ] || continue
    "$HERE/parallel.sh" "$list" || echo "!! not every run finished: see the summary"
done

: > "$SUMMARY"
for m in $MS; do
    if [ "$WHAT" != lockstep ] && [ -n "$SHADOWS" ]; then
        logs=""; missing=0
        for s in $(seq 1 "$K"); do
            L=build/remote/$(shard_name "$m" "$s")/drive.log
            if [ -f "$L" ]; then logs="$logs $L"; else missing=$((missing + 1)); fi
        done
        D=build/remote/${P}_shadow_m$m.differ
        [ "$K" = 1 ] && D=build/remote/${P}_shadow_m$m/drive.log.differ
        if [ -z "$logs" ]; then
            echo "== shadows, mission $m: NO LOG" >> "$SUMMARY"
        else
            # A summary line names its differences or says FAILED. Passes: "0 differ", "0 of n cases differ", "same,
            # n NaN-only differences" (the math shadows) and a "differ by kind:" list of zeros
            cat $logs | grep -aE ' differ|FAILED' | grep -av ' 0 differ\|: 0 of [0-9]* cases differ\|: same, [0-9]* NaN-only' |
                awk '/differ by kind:/ { s = $0; sub(/.*differ by kind:/, "", s); if (s ~ / [1-9][0-9]*(,|$)/) print; next } { print }' > "$D" || true
            # A shadow that could not run at all says "- not run" (its summary still reads "0 differ")
            cat $logs | grep -a ' - not run' >> "$D" || true
            dumped=0
            for L in $logs; do grep -aq 'dumping frame' "$L" && dumped=$((dumped + 1)); done
            runs=""
            [ "$K" = 1 ] || runs=" ($K runs$([ $missing = 0 ] || echo ", $missing with NO LOG"))"
            echo "== shadows, mission $m$runs: $(cat $logs | grep -a -c ' differ') summaries, $(wc -l < "$D") with differences, $(cat $logs | grep -a -c 'fault at') loader faults ($(cat $logs | grep -a -c 'guarded fault at') in one line), frame dumped: $dumped of $K" >> "$SUMMARY"
            cat "$D" >> "$SUMMARY"
            cat $logs | grep -a 'faulted\|with faults\|Exception\|crash\|not handled' | sort | uniq -c | head -20 >> "$SUMMARY" || true
        fi
    fi
    if [ "$WHAT" != shadow ]; then
        for h in idle accelerate; do
            D=build/remote/${P}_m${m}_$h
            if [ -n "$BASE" ]; then
                python "$HERE/compare.py" "${BASELINES:-build/baselines}/${BASE}_m${m}_$h" "$D" "$TO" >> "$SUMMARY" || true
            else
                echo "${P}_m${m}_$h: $(ls "$D" | grep -c 'bmp$') frames, $(wc -l < "$D/sndtrace.log" 2>/dev/null || echo no) trace lines" >> "$SUMMARY"
            fi
        done
    fi
done
cat "$SUMMARY"
