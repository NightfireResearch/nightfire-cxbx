"""Turn reviewed "differing name" verdicts (vt_review.py packets of kind differs) into a rename list for vt_plates.py.

    python vt_renames.py results/vt-renames-012.json results/vt-review-d1/agent-*.json

"accept" verdicts rename a named Xbox function; "intermediate" entries rename or name the partner (a swapped pair's
other half, or an unnamed function). Kept out, and listed: renames whose current name src/driving uses (the C++
references functions by name: those need a source edit first, the user's call), functions whose name changed since
review, and conflicting proposals. Read-only towards Ghidra.
"""

import collections
import json
import os
import subprocess
import sys

from lib import ghidra_ro as g

HERE = os.path.dirname(os.path.abspath(__file__))


def used_in_src(name):
    flags = ["-F"] if "::" in name else ["-w", "-F"]
    r = subprocess.run(["git", "grep", "-n", *flags, name, "--", "src/driving"], cwd=os.path.dirname(HERE),
                       capture_output=True, text=True)
    return r.stdout.splitlines()


def main():
    out_path, sources = sys.argv[1], sys.argv[2:]
    q = g.qualified_names(g.XBOX)
    xn = {a: q.get(a, n) for a, n in g.functions(g.XBOX)}
    items, counts = [], collections.Counter()
    for path in sources:
        with open(path) as f:
            for v in json.load(f)["verdicts"]:
                counts[v["verdict"]] += 1
                if v["verdict"] == "accept":
                    items.append({"xbox": v["xbox"], "name": v["name"], "ps2": v.get("ps2"), "was": v.get("current"),
                                  "why": f"differing-name review: {v['why']}"})
                for m in v.get("intermediates") or []:
                    counts["intermediate"] += 1
                    items.append({"xbox": m["xbox"], "name": m["name"], "ps2": m.get("ps2"), "was": None,
                                  "why": f"found reviewing {v.get('current')} / {v.get('name')}: {m['why']}"})
    by_x = collections.defaultdict(set)
    for it in items:
        by_x[int(it["xbox"], 16)].add(it["name"])
    keep, held, seen = [], [], set()
    for it in items:
        a = int(it["xbox"], 16)
        cur = xn.get(a)
        why = None
        if cur is None:
            why = "no Xbox function there"
        elif cur == it["name"]:
            why = "already has that name"
        elif it["was"] and cur != it["was"]:
            why = f"renamed since review: now {cur}"
        elif len(by_x[a]) > 1:
            why = f"conflicting proposals {sorted(by_x[a])}"
        elif not cur.split("::")[-1].startswith("FUN_") and used_in_src(cur):
            why = f"src/driving uses {cur}: {used_in_src(cur)[:2]}"
        if why:
            held.append((it, why))
        elif a not in seen:
            seen.add(a)
            it["why"] += f" (was {cur})" if not cur.split("::")[-1].startswith("FUN_") else ""
            keep.append({k: it[k] for k in ("xbox", "name", "ps2", "why")})
    with open(out_path, "w") as f:
        json.dump(keep, f, indent=1)
    with open(out_path.replace(".json", "-held.md"), "w", encoding="utf-8") as f:
        f.write("# Renames held back\n\n" + "\n".join(f"- {it['xbox']} {xn.get(int(it['xbox'], 16))} -> {it['name']}: {why}"
                                                      for it, why in held) + "\n")
    print(f"{dict(counts)} -> {len(keep)} renames in {out_path}; {len(held)} held")
    for it, why in held:
        print(f"  held {it['xbox']} -> {it['name']}: {why}")


if __name__ == "__main__":
    main()
