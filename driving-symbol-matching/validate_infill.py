"""Held-out test of lib/ps2_infill.py: hide a share of the known PS2 addresses, re-run the infill, and score
its predictions for the hidden rows against their true addresses.

    python validate_infill.py [share=0.3] [trials=5]

Known = the sheet's own addresses and names already in PS2 Ghidra. A prediction is "ambiguous" when the
predicted function sits in a run of 3+ neighbours of the same size (where a dropped function could shift
names without any size changing).
"""

import random
import sys
from collections import Counter

from lib.index import Index
from lib.ps2_infill import infill, is_function_row


def main():
    share = float(sys.argv[1]) if len(sys.argv) > 1 else 0.3
    trials = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    ix = Index()
    # Start from sheet + Ghidra addresses only (drop the index's own infill).
    for r in ix.rows:
        if (r.get("ps2_from") or "").startswith("infill"):
            r["ps2"], r["ps2_from"] = None, None
    known = [i for i, r in enumerate(ix.rows) if r["ps2"] is not None and is_function_row(ix.rows[i])]
    sizes = {a: b - a for a, b in zip(ix.ps2_addrs, ix.ps2_addrs[1:])}
    pos = {a: k for k, a in enumerate(ix.ps2_addrs)}

    def ambiguous(a):
        k = pos[a]
        s = sizes.get(a)
        run = 1
        for d in (-1, 1):
            j = k + d
            while 0 <= j < len(ix.ps2_addrs) and sizes.get(ix.ps2_addrs[j]) == s:
                run += 1
                j += d
        return run >= 3

    total = Counter()
    for t in range(trials):
        random.seed(t)
        hidden = set(random.sample(known, int(len(known) * share)))
        truth = {i: ix.rows[i]["ps2"] for i in hidden}
        for i in hidden:
            ix.rows[i]["ps2"] = None
        pred = infill(ix)
        for i in hidden:
            if i in pred:
                a, how = pred[i]
                amb = "ambiguous" if ambiguous(a) else "distinct"
                total[(how, amb, "right" if a == truth[i] else "WRONG")] += 1
            else:
                total[("not predicted",)] += 1
        for i in hidden:
            ix.rows[i]["ps2"] = truth[i]
    print(f"{trials} trials, {share:.0%} of {len(known)} known rows hidden each time")
    for how in ("exact", "exact-run", "count"):
        for amb in ("distinct", "ambiguous"):
            r, w = total[(how, amb, "right")], total[(how, amb, "WRONG")]
            if r + w:
                print(f"  {how:9s} {amb:9s} {r:5d} right {w:4d} wrong  ({100 * r / (r + w):.1f}% right)")
    print(f"  not predicted {total[('not predicted',)]}")


if __name__ == "__main__":
    main()
