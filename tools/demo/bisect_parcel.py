"""Bisects a parcel of ported functions down to the one that makes a time demo differ (docs/action-timedemo.md).

When a parcel of ports makes a demo stop matching, and the demo matches again with every one of them restored to
the original (NIGHTFIRE_ORIGINAL), this finds which port it is: each step leaves half of the suspects ported and
the rest of the parcel restored, and plays the demo to its first difference with tools/demo/bisect_originals.ps1.

    python tools/demo/bisect_parcel.py "tests\\demos\\action\\CastleCourtyard-*.nfdemo" build/parcel_addresses.txt

The file is the parcel's addresses, comma-separated (0x00049d70,0x00047a10,...). Prints each step and the culprit.
It assumes one culprit. If both halves match on their own, the difference needs ports from both: the list is
printed for a look by hand. A port that calls another port by its C++ name goes round its original's address, so
restoring the callee does not take it out of play; the culprit can then be the caller.
"""

import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def matches(demo, everything, ported):
    restored = [a for a in everything if a not in ported]
    out = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                          os.path.join(REPO, 'tools', 'demo', 'bisect_originals.ps1'), '-Demo', demo,
                          '-Originals', ','.join(restored)], capture_output=True, text=True, cwd=REPO).stdout
    ok = 'every frame matched' in out
    last = out.strip().splitlines()[-1] if out.strip() else ''
    print(f'  {len(ported):3} ported: {"match" if ok else "DIFFERS"}  {last}', flush=True)
    return ok


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    demo = sys.argv[1]
    everything = [a for a in open(sys.argv[2]).read().replace('\n', ',').split(',') if a.strip()]
    suspects = list(everything)
    while len(suspects) > 1:
        first, second = suspects[:len(suspects) // 2], suspects[len(suspects) // 2:]
        if not matches(demo, everything, first):
            suspects = first
        elif not matches(demo, everything, second):
            suspects = second
        else:
            print('both halves match on their own: the difference needs ports from both of', suspects)
            return 1
    print('culprit:', suspects[0])
    return 0


if __name__ == '__main__':
    sys.exit(main())
