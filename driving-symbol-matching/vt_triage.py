"""Triage the Version Tracking session's function matches (data/vt-matches.json, from ghidra/NightfireVTExport.py)
on evidence VT's own scores don't use. Read-only.

    python vt_triage.py

Candidates: function matches, any status, where the Xbox function is unnamed and the PS2 one named ("new"),
or where both are named differently ("differs"). For each, from live Ghidra and the feature caches:
  near     - the nearest named Xbox functions either side (named alike on PS2): how many PS2 functions lie
             between the candidate's PS2 function and theirs. MSVC keeps an object file's code together, in
             another order; for known pairs 95% have a neighbour within 34 and 99% within 243 (the sheet's
             object-file markers are too sparse to use). Within 40 agrees; over 250 contradicts.
  strings  - shared string constants.
  callees  - named callees shared (by name) / named callees on the Xbox side.
  callers  - named callers shared (by name).
  size     - Xbox / PS2 instruction count.
Tiers for "new": A = near, positive body evidence (strings, callees or callers), nothing
contradicting, and the pair is the only A candidate for either address; B = near, no body
evidence either way (small functions: read them); X = a contradiction. Writes results/vt-triage.json/.md.
"""

import bisect
import collections
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor

from lib import features_cache, ghidra_ro as g

CALIBRATE = "--calibrate" in sys.argv   # score pairs already named alike, as if new: how often a right pair fails
HERE = os.path.dirname(os.path.abspath(__file__))


def unnamed(n):
    return n is None or n.split("::")[-1].startswith("FUN_")


def main():
    with open(os.path.join(HERE, "data", "vt-matches.json")) as f:
        vt = [m for m in json.load(f)["matches"] if m["type"] == "Function"]
    fx, fp = features_cache.load(g.XBOX), features_cache.load(g.PS2)

    # Live names, qualified.
    def names(program):
        q = g.qualified_names(program)
        return {a: q.get(a, n) for a, n in g.functions(program)}
    xn, pn = names(g.XBOX), names(g.PS2)

    prank = {a: i for i, a in enumerate(sorted(pn))}

    # Xbox anchors: named functions whose name is unique on both sides.
    pcount = collections.Counter(pn.values())
    xcount = collections.Counter(xn.values())
    by_name = {n: a for a, n in pn.items() if pcount[n] == 1}
    anchors = sorted((a, prank[by_name[n]]) for a, n in xn.items()
                     if not unnamed(n) and xcount[n] == 1 and n in by_name)
    xa = [a for a, _ in anchors]

    def neighbours(a):
        k = bisect.bisect_left(xa, a)
        before = anchors[k - 1][1] if k > 0 else None
        after = anchors[k][1] if k < len(anchors) and anchors[k][0] != a else (anchors[k + 1][1] if k + 1 < len(anchors) else None)
        return before, after

    cands = []
    for m in vt:
        x, p = int(m["xbox"], 16), int(m["ps2"], 16)
        if x not in xn or p not in pn or unnamed(pn[p]):
            continue
        kind = "new" if unnamed(xn[x]) else ("same" if xn[x] == pn[p] else "differs")
        if kind == "same" and not CALIBRATE:
            continue
        if kind == "same":
            kind = "new"
        cands.append((x, p, kind, m))
    pairs = sorted({(x, p) for x, p, _, _ in cands})

    def callers(program, a):
        try:
            return {n for _, n in g.callers(program, a)}
        except Exception:
            return set()
    with ThreadPoolExecutor(6) as pool:
        cx = dict(zip(pairs, pool.map(lambda xp: callers(g.XBOX, xp[0]), pairs)))
        cp = dict(zip(pairs, pool.map(lambda xp: callers(g.PS2, xp[1]), pairs)))

    out = {}
    for x, p, kind, m in cands:
        key = (x, p)
        if key in out:
            out[key]["correlators"].append(f"{m['correlator']} ({m['status']})")
            continue
        a, b = fx.get(x, {}), fp.get(p, {})
        sx, sp = {s for s in a.get("strings", []) if s.strip()}, {s for s in b.get("strings", []) if s.strip()}
        shared_s = len(sx & sp)
        callee_x = {c for c in a.get("callees", []) if not unnamed(c)}
        shared_c = len(callee_x & set(b.get("callees", [])))
        caller_x = {n.split("::")[-1] for n in cx[key] if not unnamed(n)}
        caller_p = {n.split("::")[-1] for n in cp[key] if not unnamed(n)}
        shared_r = len(caller_x & caller_p)
        size = (a.get("insns") or 0) / max(1, b.get("insns") or 1)
        nb = [n for n in neighbours(x) if n is not None]
        dist = min(abs(prank[p] - n) for n in nb) if nb else None
        positive = []
        if shared_s:
            positive.append(f"{shared_s} shared strings")
        if shared_c:
            positive.append(f"{shared_c}/{len(callee_x)} named callees")
        if shared_r:
            positive.append(f"{shared_r} named callers")
        contra = []
        if dist is not None and dist > 250:
            contra.append(f"far from its Xbox neighbours' PS2 functions ({dist})")
        if sx and sp and not shared_s:
            contra.append("strings on both sides, none shared")
        if len(callee_x) >= 2 and not shared_c:
            contra.append("no named callee shared")
        if len(caller_x) >= 2 and len(caller_p) >= 2 and not shared_r:
            contra.append("named callers on both sides, none shared")
        if not 0.2 <= size <= 5.0:   # MSVC inlines differently: only extreme ratios count
            contra.append(f"size ratio {size:.2f}")
        out[key] = {"xbox": f"0x{x:08x}", "ps2": f"0x{p:08x}", "kind": kind, "xbox_name": xn[x], "ps2_name": pn[p],
                    "distance": dist, "positive": positive, "contra": contra,
                    "size": round(size, 2), "correlators": [f"{m['correlator']} ({m['status']})"]}

    # Tiers for new names; A must be the only A for both of its addresses.
    for r in out.values():
        near = r["distance"] is not None and r["distance"] <= 40
        r["tier"] = "X" if r["contra"] else ("A" if near and r["positive"] else ("B" if near else "C"))
    a_x = collections.Counter(r["xbox"] for r in out.values() if r.get("tier") == "A")
    a_p = collections.Counter(r["ps2"] for r in out.values() if r.get("tier") == "A")
    for r in out.values():
        if r.get("tier") == "A" and (a_x[r["xbox"]] > 1 or a_p[r["ps2"]] > 1):
            r["tier"] = "A?"   # competing A candidates
    rows = sorted(out.values(), key=lambda r: (r["kind"], r.get("tier", ""), r["xbox"]))
    if CALIBRATE:
        print("calibration on pairs already named alike:", dict(collections.Counter(r.get("tier") for r in rows if r["xbox_name"] == r["ps2_name"])))
        print(collections.Counter(c.split(" (")[0].split(" ")[0] + " " + c.split(" ")[1] for r in rows if r["xbox_name"] == r["ps2_name"] for c in r["contra"]))
        return
    with open(os.path.join(HERE, "results", "vt-triage.json"), "w") as f:
        json.dump(rows, f, indent=1)
    tiers = collections.Counter((r["kind"], r.get("tier")) for r in rows)
    lines = ["# Version Tracking candidates, triaged", "", f"{dict(tiers)}", ""]
    for r in rows:
        lines.append(f"- {r['kind']} {r.get('tier', '')} Xbox {r['xbox']} {r['xbox_name']} <- PS2 {r['ps2']} {r['ps2_name']} "
                     f"[distance {r['distance']}] + {', '.join(r['positive']) or '-'}"
                     f" / - {', '.join(r['contra']) or '-'} (size {r['size']}; {'; '.join(r['correlators'])})")
    with open(os.path.join(HERE, "results", "vt-triage.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(dict(tiers))


if __name__ == "__main__":
    main()
