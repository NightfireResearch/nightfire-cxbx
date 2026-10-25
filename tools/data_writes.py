#!/usr/bin/env python3
"""Which of the action engine's globals are constants: the original never writes them (Ghidra's references) and no
run changed them (NIGHTFIRE_DATA_WRITES files, src/action/devtools/DataWrites.h). docs/action-standalone.md.

    python tools/data_writes.py RUN...            every global still at the game's address, by class
    python tools/data_writes.py RUN... --owned    the owned globals whose game copy a run changed: game code (or a raw
                                                  address of ours) still writing the game's copy

The classes, for a global still at the game's address:

  constant     initial data in the XBE, no write reference, and no run changed a byte of it: a table to move into our
               source as an initialiser, tagged `readonly` (tools/global_coverage.py) while originals still read theirs
  zero         the same with no initial data: all zero in every run (dead, or written only where no run went)
  pointer      no write reference and unchanged, but code takes its address (a 'D' reference), so a write through
               the pointer is possible where no run went: read each use before calling it a constant
  written      a run changed it, or the original writes it: state, which stays at the game's address until the
               all-original runs are done (or moves with every original reference relocated to ours)
  unsized      no size known (tag it with // XBE_GLOBAL(address, size)); classed by its first word
"""
import bisect
import json
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import global_coverage as gc   # noqa: E402


def read_runs(paths):
    """Every changed range in any run, as sorted (start, end, first frame, run)."""
    ranges = []
    for path in paths:
        for line in open(path):
            if line.startswith("#") or not line.strip():
                continue
            start, end, frame = line.split()
            ranges.append((int(start, 16), int(end, 16), int(frame), os.path.basename(path)))
    ranges.sort()
    return ranges


def changed(ranges, starts, address, size):
    """The runs' ranges that overlap [address, address + size)."""
    i = max(bisect.bisect_right(starts, address) - 1, 0)
    out = []
    while i < len(ranges) and ranges[i][0] < address + size:
        if ranges[i][1] > address:
            out.append(ranges[i])
        i += 1
    return out


def main():
    args = sys.argv[1:]
    runs = [a for a in args if not a.startswith("--")]
    if not runs:
        sys.exit(__doc__)
    ranges = read_runs(runs)
    starts = [r[0] for r in ranges]
    globals_, _ = gc.read_source()
    nonzero, _, _ = gc.read_xbe()
    xrefs = sorted((int(t, 16), k) for t, f, fn, k in json.load(open(gc.XREFS)))
    targets = [x[0] for x in xrefs]

    def kinds(address, size):
        i, j = bisect.bisect_left(targets, address), bisect.bisect_left(targets, address + size)
        return Counter(k for _, k in xrefs[i:j])

    if "--owned" in args:
        for g in sorted((g for g in globals_ if g["owned"]), key=lambda g: g["address"]):
            hits = changed(ranges, starts, g["address"], g["size"] or 4)
            if hits:
                more = " +%d" % (len(hits) - 3) if len(hits) > 3 else ""
                print("%-32s %08x +%-6x %-5s %s:%d  %s%s" % (
                    g["name"], g["address"], g["size"] or 0, "ro" if g.get("readonly") else "", g["file"], g["line"],
                    ", ".join("%08x-%08x f%d %s" % h for h in hits[:3]), more))
        return

    rows = []
    seen = set()
    for g in globals_:
        if g["owned"] or (g["name"], g["address"]) in seen:
            continue
        seen.add((g["name"], g["address"]))
        size = g["size"] or 0
        k = kinds(g["address"], max(size, 1))
        hits = changed(ranges, starts, g["address"], max(size, 4))
        if not size:
            cls = "unsized"
        elif hits or k["W"]:
            cls = "written"
        elif k["D"]:
            cls = "pointer"
        elif nonzero(g["address"], size):
            cls = "constant"
        else:
            cls = "zero"
        first = min((h[2] for h in hits), default=None)
        rows.append((cls, g, k, first))

    order = ["constant", "zero", "pointer", "written", "unsized"]
    counts = Counter(r[0] for r in rows)
    print("%d globals still at the game's address, over %d runs: %s" % (
        len(rows), len(runs), ", ".join("%s %d" % (c, counts[c]) for c in order)))
    for cls in order:
        print("\n%s:" % cls)
        for _, g, k, first in sorted((r for r in rows if r[0] == cls), key=lambda r: (r[1]["file"], r[1]["address"])):
            print("  %-32s %08x +%-6x %-18s %-10s %s:%d" % (
                g["name"], g["address"], g["size"] or 0, " ".join("%s%d" % (c, n) for c, n in sorted(k.items())),
                "" if first is None else "frame %d" % first, g["file"], g["line"]))


if __name__ == "__main__":
    main()
