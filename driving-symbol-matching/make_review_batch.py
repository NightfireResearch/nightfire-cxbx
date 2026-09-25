"""Turn accepted body-review verdicts into a DRIVING.ELF batch.

    python make_review_batch.py P011 results/review-verdicts.json results/review-trial/agent-1.json ...

For each "accept": the row's Ghidra-safe name (make_ps2_batch.ghidra_name, with cut-name completions), or for a
cut _Rb_tree row the shorthand _Rb_tree<Key, Mapped> (lib/shorthand.py) plus the method the body showed.
Only functions still unnamed (FUN_) are included. Accepted addresses are also written to
results/ps2-name-resolutions.json as "address" entries, so the index and alignment treat them as settled.
Rejects and defers are listed in results/batches/batch-<id>-held.md. Read-only towards Ghidra.
"""

import json
import os
import sys

from lib import names, sheet
from lib.index import Index
from lib.shorthand import rb_tree
from make_ps2_batch import ghidra_name, ghidra_safe

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    batch_id, sources = args[0], args[1:]
    verdicts = []
    for path in sources:
        with open(path) as f:
            data = json.load(f)
        verdicts += data["verdicts"] if isinstance(data, dict) else data
    ix = Index()
    by_row = {r["row"]: (k, r) for k, r in enumerate(ix.rows)}
    with open(os.path.join(HERE, "results", "cut-name-completions.json")) as f:
        completions = {c["row"]: c for c in json.load(f)}

    from collections import Counter
    base_counts = Counter(sheet.base_name(r["name"]) for r in ix.rows)   # overloads may share a name
    items, held, settle = [], [], []
    for v in verdicts:
        k, r = by_row[v["row"]]
        a = int(v["ps2"], 16) if v.get("ps2") else 0
        if v["verdict"] != "accept" or not a:
            held.append((r, a, f"{v['verdict']}: {v['why']}"))
            continue
        if r["name"].startswith("_Rb_tree<") and r["truncated"]:
            near = [x["name"] for x in ix.rows[max(0, k - 15):k + 15]]
            cls = rb_tree(r["name"], near)
            if cls is None or not v.get("method"):
                held.append((r, a, "accepted, but the _Rb_tree shorthand or the method is unknown"))
                continue
            name = ghidra_safe(cls) + "::" + v["method"]
        else:
            done = completions.get(r["row"])
            name, why = ghidra_name(done["complete"], False) if done else ghidra_name(r["name"], r["truncated"])
            ns = names.namespace(sheet.base_name(r["name"]))
            if name is None and v.get("method") and ns and ns.count("<") == ns.count(">"):
                name = ghidra_safe(ns) + "::" + v["method"]   # only when the kept class name is whole
            if name is None:
                held.append((r, a, f"accepted, but {why}"))
                continue
        settle.append({"row": r["row"], "ps2": f"0x{a:08x}", "verdict": "address", "why": "body review: " + v["why"]})
        f = ix.ps2.get(a)
        if f is None or not f["name"].startswith("FUN_"):
            continue
        items.append({"xbox": f"0x{a:08x}", "expect": f["name"], "name": name, "sheet_name": r["name"],
                      "create": False, "allow_duplicate": base_counts[sheet.base_name(r["name"])] > 1,
                      "evidence": [f"sheet row {r['row']}, body review: {v['why']}"]})

    path = os.path.join(HERE, "results", "batches", f"batch-{batch_id}.json")
    with open(path, "w") as f:
        json.dump({"batch": batch_id, "program": "DRIVING.ELF",
                   "note": "Names accepted by reading function bodies (review.py). 'xbox' is the PS2 address.",
                   "items": items}, f, indent=1)
    with open(path.replace(".json", "-held.md"), "w", encoding="utf-8") as f:
        f.write("\n".join([f"# Batch {batch_id}: not applied", "", "| row | sheet name | PS2 | why |", "|---|---|---|---|"]
                          + [f"| {r['row']} | {r['name'].replace('|', '/')} | {a:08x} | {w} |" for r, a, w in held]) + "\n")
    res_path = os.path.join(HERE, "results", "ps2-name-resolutions.json")
    with open(res_path) as f:
        res = json.load(f)
    have = {d["row"] for d in res["resolutions"]}
    res["resolutions"] += [s for s in settle if s["row"] not in have]
    print(f"{path}: {len(items)} items, {len(held)} held; {len([s for s in settle if s['row'] not in have])} addresses to settle")
    return res, res_path


if __name__ == "__main__":
    res, res_path = main()
    if "--settle" in sys.argv:
        with open(res_path, "w") as f:
            json.dump(res, f, indent=1)
