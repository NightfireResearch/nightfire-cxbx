"""Held-out test of lib/align.py: hide a share of the confidently placed rows (the sheet's own addresses and
names already in PS2 Ghidra), align the windows the remaining ones leave, and score predictions for the hidden
rows against their true addresses, by margin.

    python validate_align.py [share=0.3] [trials=3]

Prints accuracy per margin band, so a threshold can be chosen where placements are ~99.5% right.
"""

import random
import sys
from collections import Counter

from lib import features_cache
from lib.align import Scorer, align_window, windows
from lib.index import Index
from lib.ps2_infill import is_function_row

BANDS = [(-1e9, 0), (0, 1), (1, 2), (2, 3), (3, 4), (4, 6), (6, 1e9)]


def band(m):
    for lo, hi in BANDS:
        if lo <= m < hi:
            return (lo, hi)


def main():
    share = float(sys.argv[1]) if len(sys.argv) > 1 else 0.3
    trials = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    ix = Index()
    feats = features_cache.load("DRIVING.ELF")
    scorer = Scorer(ix, {a: v["strings"] for a, v in feats.items()})
    trusted = [r for r in ix.rows if is_function_row(r) and r.get("ps2_from") in ("sheet", "ghidra")]
    tally = Counter()
    for t in range(trials):
        random.seed(100 + t)
        hidden = {id(r) for r in random.sample(trusted, int(len(trusted) * share))}
        truth = {id(r): r["ps2"] for r in trusted if id(r) in hidden}
        anchor_ok = lambda r: r.get("ps2_from") in ("sheet", "ghidra") and id(r) not in hidden
        for rows, funcs in windows(ix, is_function_row, anchor_ok):
            for row, func, margin in align_window(rows, funcs, scorer):
                if id(row) not in hidden:
                    continue
                b = band(margin)
                if func is None:
                    tally[(b, "said absent")] += 1
                else:
                    tally[(b, "right" if func == truth[id(row)] else "WRONG")] += 1
    print(f"{trials} trials, {share:.0%} of {len(trusted)} trusted rows hidden each time")
    print(f"{'margin':>12} {'right':>6} {'wrong':>6} {'%right':>7} {'absent':>7}   cumulative from this band up")
    for lo, hi in reversed(BANDS):
        r, w, a = tally[((lo, hi), "right")], tally[((lo, hi), "WRONG")], tally[((lo, hi), "said absent")]
        cr = sum(tally[(b, "right")] for b in BANDS if b[0] >= lo)
        cw = sum(tally[(b, "WRONG")] for b in BANDS if b[0] >= lo)
        pct = f"{100 * r / (r + w):.1f}" if r + w else "-"
        cpct = f"{100 * cr / (cr + cw):.2f}% of {cr + cw}" if cr + cw else "-"
        print(f"{'>=' + str(lo) if hi > 1e8 else f'{lo}..{hi}':>12} {r:6d} {w:6d} {pct:>7} {a:7d}   {cpct}")


if __name__ == "__main__":
    main()
