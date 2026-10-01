"""Place unplaced sheet rows by alignment (lib/align.py), and audit already-placed ones.

    python place_align.py [threshold=3]  -> results/ps2-align-placements.json, results/ps2-audit.md

Placements: rows with no PS2 address (or only an exact-run/count guess) that the alignment puts on a function
with margin >= threshold, where that function has no sheet row yet. validate_align.py measured margin >= 3 at
~99.5% right. The index reads the placements file (source "align"); make_ps2_batch.py turns them into a batch.

Audit: rows whose sheet or PS2 Ghidra address the alignment, with that row hidden, confidently places
elsewhere - a wrong address in the sheet, a mis-named PS2 function, or overloads. For review; nothing uses it.
"""

import json
import os
import random
import sys
from collections import defaultdict

from lib import features_cache
from lib.align import Scorer, align_window, windows
from lib.index import Index
from lib.ps2_infill import is_function_row

HERE = os.path.dirname(os.path.abspath(__file__))
ANCHORS = ("sheet", "ghidra", "resolved", "infill-exact", "infill-count")


def main():
    threshold = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
    ix = Index(use_align=False)
    feats = features_cache.load("DRIVING.ELF")
    scorer = Scorer(ix, {a: v["strings"] for a, v in feats.items()})
    taken = {r["ps2"] for r in ix.rows if r.get("ps2_from") in ANCHORS}

    placed, agree_run, disagree_run = {}, 0, []
    for rows, funcs in windows(ix, is_function_row, lambda r: r.get("ps2_from") in ANCHORS):
        for row, func, margin in align_window(rows, funcs, scorer):
            if func is None or margin < threshold or func in taken:
                continue
            if row.get("ps2_from") == "infill-exact-run":
                if func == row["ps2"]:
                    agree_run += 1
                else:
                    disagree_run.append((row, func, margin))
            placed[row["row"]] = {"row": row["row"], "name": row["name"], "ps2": f"0x{func:08x}",
                                  "margin": round(margin, 2), "was": row.get("ps2_from")}
    # One function, one row.
    by_func = defaultdict(list)
    for p in placed.values():
        by_func[p["ps2"]].append(p)
    dup = {k for k, v in by_func.items() if len(v) > 1}
    kept = [p for p in placed.values() if p["ps2"] not in dup]
    with open(os.path.join(HERE, "results", "ps2-align-placements.json"), "w") as f:
        json.dump({"threshold": threshold, "placements": sorted(kept, key=lambda p: p["row"])}, f, indent=0)
    print(f"placed {len(kept)} rows at margin >= {threshold} ({len(dup)} functions claimed twice, dropped); "
          f"exact-run guesses: {agree_run} confirmed, {len(disagree_run)} contradicted")

    # Audit: hide each trusted row in turn, in 5 random partitions, and see where the alignment puts it.
    # Settled rows ("resolved") stay as anchors and are never re-guessed.
    trusted = [r for r in ix.rows if is_function_row(r) and r.get("ps2_from") in ("sheet", "ghidra")]
    doubts = {}
    for t in range(5):
        random.seed(500 + t)
        hidden = {id(r) for r in random.sample(trusted, len(trusted) // 5)}
        ok = lambda r: r.get("ps2_from") in ANCHORS and id(r) not in hidden
        for rows, funcs in windows(ix, is_function_row, ok):
            for row, func, margin in align_window(rows, funcs, scorer):
                if id(row) in hidden and func is not None and func != row["ps2"] and margin >= threshold:
                    doubts[row["row"]] = (row, func, margin)
    lines = ["# PS2 audit: known addresses the alignment disputes", "",
             "Each row was hidden and re-placed by alignment with margin >= threshold, somewhere else. Causes seen: a",
             "wrong address in the sheet, a mis-named PS2 function, overloads (two constructors). For review only.", "",
             "| row | colour | sheet name | known at | from | sheet size | retail size there | alignment says | its name now | retail size | margin |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for n, (row, func, margin) in sorted(doubts.items()):
        lines.append(f"| {n} | {row['colour'] or ''} | {row['name'].replace('|', '/')} | {row['ps2']:08x} | {row['ps2_from']} | "
                     f"{row['size']:x} | {scorer.retail_size.get(row['ps2'], 0):x} | {func:08x} | {ix.ps2[func]['qualified']} | "
                     f"{scorer.retail_size.get(func, 0):x} | {margin:.1f} |")
    with open(os.path.join(HERE, "results", "ps2-audit.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"audit: {len(doubts)} known addresses disputed -> results/ps2-audit.md")


if __name__ == "__main__":
    main()
