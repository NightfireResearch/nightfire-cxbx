"""Turn Version Tracking review verdicts (vt_review.py packets, reviewed) into a rename list for vt_plates.py.

    python vt_verdicts.py results/vt-accepted-007.json results/vt-review-trial/agent-*.json

Accepts and their accepted "intermediates" (unnamed functions placed along an agreeing caller chain) become
[{"xbox", "name", "ps2", "why"}]. Dropped, and listed: anything whose Xbox function is no longer unnamed, the
same Xbox address given two names, a name given to two Xbox addresses, and PS2 names that don't exist.
Read-only towards Ghidra.
"""

import collections
import json
import sys

from lib import ghidra_ro as g


def main():
    out_path, sources = sys.argv[1], sys.argv[2:]
    q = g.qualified_names(g.XBOX)
    xn = {a: q.get(a, n) for a, n in g.functions(g.XBOX)}
    qp = g.qualified_names(g.PS2)
    pn = {a: qp.get(a, n) for a, n in g.functions(g.PS2)}
    items, counts = [], collections.Counter()
    for path in sources:
        with open(path) as f:
            for v in json.load(f)["verdicts"]:
                counts[v["verdict"]] += 1
                if v["verdict"] != "accept":
                    continue
                items.append({"xbox": v["xbox"], "name": v["name"], "ps2": v["ps2"],
                              "why": f"Version Tracking candidate, body review: {v['why']}"})
                for m in v.get("intermediates") or []:
                    counts["intermediate"] += 1
                    items.append({"xbox": m["xbox"], "name": m["name"], "ps2": m.get("ps2"),
                                  "why": f"on the caller chain of {v['name']}, body review: {m['why']}"})
    by_x = collections.defaultdict(set)
    by_n = collections.defaultdict(set)
    for it in items:
        by_x[int(it["xbox"], 16)].add(it["name"])
        by_n[it["name"]].add(int(it["xbox"], 16))
    keep, dropped, seen = [], [], set()
    for it in items:
        a = int(it["xbox"], 16)
        why = None
        if a not in xn:
            why = "no Xbox function there"
        elif not xn[a].split("::")[-1].startswith("FUN_"):
            why = f"Xbox already named {xn[a]}"
        elif len(by_x[a]) > 1:
            why = f"two names proposed: {sorted(by_x[a])}"
        elif len(by_n[it["name"]]) > 1:
            why = "same name for several Xbox functions"
        elif it.get("ps2") and pn.get(int(it["ps2"], 16)) != it["name"]:
            why = f"PS2 {it['ps2']} is {pn.get(int(it['ps2'], 16))}, not {it['name']}"
        if why:
            dropped.append((it, why))
        elif a not in seen:
            seen.add(a)
            keep.append(it)
    with open(out_path, "w") as f:
        json.dump(keep, f, indent=1)
    print(f"{dict(counts)} -> {len(keep)} renames in {out_path}")
    for it, why in dropped:
        print(f"  dropped {it['xbox']} {it['name']}: {why}")


if __name__ == "__main__":
    main()
