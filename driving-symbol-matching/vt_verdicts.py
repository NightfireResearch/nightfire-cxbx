"""Turn Version Tracking review verdicts (vt_review.py packets, reviewed) into a rename list for vt_plates.py.

    python vt_verdicts.py results/vt-accepted-007.json results/vt-review-trial/agent-*.json [--exclude 0x..,0x..]

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
    exclude = set()
    args = sys.argv[1:]
    if "--exclude" in args:   # --exclude 0x14310,0x14700: Xbox addresses to leave out (weak evidence)
        k = args.index("--exclude")
        exclude = {int(x, 16) for x in args[k + 1].split(",")}
        args = args[:k] + args[k + 2:]
    out_path, sources = args[0], args[1:]
    q = g.qualified_names(g.XBOX)
    xn = {a: q.get(a, n) for a, n in g.functions(g.XBOX)}
    qp = g.qualified_names(g.PS2)
    pn = {a: qp.get(a, n) for a, n in g.functions(g.PS2)}
    items, counts = [], collections.Counter()
    for path in sources:
        with open(path) as f:
            for v in json.load(f)["verdicts"]:
                counts[v["verdict"]] += 1
                if v["verdict"] == "accept":
                    items.append({"xbox": v["xbox"], "name": v["name"], "ps2": v["ps2"],
                                  "why": f"Version Tracking candidate, body review: {v['why']}"})
                # Intermediates: functions placed along an agreeing chain, or, on a reject, the function the
                # candidate really is (or the real owner of the name).
                for m in v.get("intermediates") or []:
                    counts["intermediate"] += 1
                    where = "on the caller chain of" if v["verdict"] == "accept" else "found reviewing (rejected) candidate"
                    items.append({"xbox": m["xbox"], "name": m["name"], "ps2": m.get("ps2"),
                                  "why": f"{where} {v['name']}, body review: {m['why']}"})
    by_x = collections.defaultdict(set)
    by_n = collections.defaultdict(set)
    for it in items:
        by_x[int(it["xbox"], 16)].add(it["name"])
        by_n[it["name"]].add(int(it["xbox"], 16))
    ps2_count = collections.Counter(pn.values())
    ps2_of = collections.defaultdict(set)
    for it in items:
        ps2_of[it["name"]].add((int(it["xbox"], 16), it.get("ps2")))

    def overload(name):
        """An overloaded PS2 name, each Xbox function paired with a different PS2 overload."""
        pairs = ps2_of[name]
        return ps2_count[name] > 1 and len({p for _, p in pairs}) == len({x for x, _ in pairs}) and None not in {p for _, p in pairs}

    keep, dropped, seen = [], [], set()
    for it in items:
        a = int(it["xbox"], 16)
        why = None
        if a in exclude:
            why = "excluded by hand"
        elif a not in xn:
            why = "no Xbox function there"
        elif not xn[a].split("::")[-1].startswith("FUN_"):
            why = f"Xbox already named {xn[a]}"
        elif len(by_x[a]) > 1:
            why = f"two names proposed: {sorted(by_x[a])}"
        elif len(by_n[it["name"]]) > 1 and not overload(it["name"]):
            why = "same name for several Xbox functions"
        elif it.get("ps2") and pn.get(int(it["ps2"], 16)) != it["name"]:
            why = f"PS2 {it['ps2']} is {pn.get(int(it['ps2'], 16))}, not {it['name']}"
        if why:
            dropped.append((it, why))
        elif a not in seen:
            seen.add(a)
            if ps2_count[it["name"]] > 1:
                it["allow_duplicate"] = True   # an overloaded PS2 name
            keep.append(it)
    with open(out_path, "w") as f:
        json.dump(keep, f, indent=1)
    print(f"{dict(counts)} -> {len(keep)} renames in {out_path}")
    for it, why in dropped:
        print(f"  dropped {it['xbox']} {it['name']}: {why}")


if __name__ == "__main__":
    main()
