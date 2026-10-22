#!/bin/sh
# Rebases action engine time demos onto the code of an executable (docs/action-timedemo.md, "Rebasing a recording"):
# each recording's input played into the game, the checksums and sound answers taken anew, written to OUTDIR under
# the same name. One at a time, at the game's own pace. Run from the checkout; the executable runs from Release/.
#
#   tools/demo/rebase_demos.sh Release/action_standalone_test_c.exe tests/demos/action-c        # every recording
#   tools/demo/rebase_demos.sh Release/action_standalone_test_c.exe tests/demos/action-c tests/demos/action/TowerA-*.nfdemo
#
# Each one's log is build/rebase/<name>.log; the line after it says where the new game first parted from the old.

exe=$1
out=$2
[ -n "$exe" ] && [ -n "$out" ] || { echo "usage: $0 EXE OUTDIR [recording ...]"; exit 2; }
shift 2
[ $# -gt 0 ] || set -- tests/demos/action/*.nfdemo
repo=$(pwd)
mkdir -p "$out" build/rebase
for demo in "$@"; do
    name=$(basename "$demo" .nfdemo)
    (cd Release && NIGHTFIRE_DEMO_PLAY="$repo/$demo" NIGHTFIRE_DEMO_REBASE="$repo/$out/$name.nfdemo" \
        "$repo/$exe" > "$repo/build/rebase/$name.log" 2>&1)
    echo "$name: exit $? - $(grep -a "\[demo\].*\(differ\|part\|first\)" "build/rebase/$name.log" | tail -1)"
done
