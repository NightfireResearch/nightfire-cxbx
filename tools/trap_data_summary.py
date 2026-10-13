#!/usr/bin/env python3
"""NIGHTFIRE_TRAP_DATA's log (src/driving/devtools/StandaloneTraps.cpp) grouped by global: what of Driving.xbe's
.rdata/.data/.bss our code still reads and writes, and from where (docs/driving/standalone.md).

    python tools/trap_data_summary.py trap_data.txt [Release/drivinginject.map]

Each line of the log is one distinct (code address, data address, kind) with a count; the code address is our DLL's,
or for an access made inside the runtime or the host (memcpy, d3d9) the first return address into our DLL ("via").
Data addresses are named by the globals our source defines at the game's addresses (tools/global_coverage.py reads
them) and the owned ones' tags; code addresses by the DLL's map, or tools/functions_driving.json inside the XBE.
"""
import bisect
import json
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import global_coverage as gc   # noqa: E402

KINDS = {0: "R", 1: "W", 8: "X"}


def load_map(path):
    """(sorted addresses, names) of the DLL's public symbols at its preferred base, and that base."""
    base, symbols = 0x10000000, []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*Preferred load address is ([0-9a-fA-F]+)", line)
        if m:
            base = int(m.group(1), 16)
        m = re.match(r"\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+f\s", line)
        if m:
            symbols.append((int(m.group(2), 16), m.group(1)))
    symbols.sort()
    return [a for a, _ in symbols], [n for _, n in symbols], base


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    log = sys.argv[1]
    map_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "Release", "drivinginject.map")
    starts, names, preferred = load_map(map_path) if os.path.exists(map_path) else ([], [], 0x10000000)
    gc.use_engine("driving")
    funcs = sorted((int(f["address"], 16), f["name"]) for f in json.load(open(gc.FUNCTIONS)))
    fstarts = [a for a, _ in funcs]
    globals_, _ = gc.read_source()
    ranges = sorted({(g["address"], g["address"] + (g["size"] or 4), g["name"] + (" (ours)" if g["owned"] else ""))
                     for g in globals_})
    rstarts = [r[0] for r in ranges]

    ours = 0
    accesses = []
    for line in open(log, encoding="utf-8", errors="replace"):
        m = re.search(r"drivinginject at ([0-9a-f]+)", line)
        if m:
            ours = int(m.group(1), 16)
            continue
        parts = line.split()
        if len(parts) != 5 or line.startswith("#"):
            continue
        code, via, data = (int(p, 16) for p in parts[:3])
        kind, count = int(parts[3]), int(parts[4])
        accesses.append((code, via, data, kind, count))

    def code_name(address):
        if address == 0:
            return "?"
        if address < 0x00254f40:
            i = bisect.bisect_right(fstarts, address) - 1
            return "XBE %s+0x%x" % (funcs[i][1], address - funcs[i][0]) if i >= 0 else "XBE 0x%08x" % address
        if ours and starts and 0 <= address - ours < 0x01000000:   # inside the DLL (it is under 16 MB)
            at = address - ours + preferred
            i = bisect.bisect_right(starts, at) - 1
            if i >= 0:
                return "%s+0x%x" % (names[i], at - starts[i])
        return "0x%08x" % address

    def global_name(address):
        i = bisect.bisect_right(rstarts, address) - 1
        for j in range(i, max(i - 64, -1), -1):   # the innermost of overlapping ranges comes last
            if ranges[j][0] <= address < ranges[j][1]:
                return ranges[j][2]
        return "(no global, page 0x%08x)" % (address & ~0xfff)

    groups = defaultdict(lambda: {"count": 0, "sites": defaultdict(int), "kinds": set()})
    for code, via, data, kind, count in accesses:
        group = global_name(data)
        site = code_name(code) + (" via " + code_name(via) if via else "")
        g = groups[group]
        g["count"] += count
        g["sites"][site] += count
        g["kinds"].add(KINDS.get(kind, str(kind)))
    print("%d distinct accesses, %d globals" % (len(accesses), len(groups)))
    for group, g in sorted(groups.items(), key=lambda kv: -len(kv[1]["sites"])):
        print("\n%s  [%s] %d accesses from %d sites" % (group, "".join(sorted(g["kinds"])), g["count"], len(g["sites"])))
        for site, n in sorted(g["sites"].items(), key=lambda kv: -kv[1])[:8]:
            print("    %8d  %s" % (n, site))


if __name__ == "__main__":
    main()
