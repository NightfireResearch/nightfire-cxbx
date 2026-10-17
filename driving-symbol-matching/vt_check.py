"""Check Xbox names that came from Version Tracking (PS2 -> Xbox), and propose more pairs from their calls.

    python vt_check.py <old Xbox snapshot> <new Xbox snapshot>

Pairs: Xbox functions whose qualified name changed between the two snapshots, paired with the PS2 function of
the same qualified name (when several PS2 functions share it, the best-scoring one). Each pair is scored on:
  strings  - shared string constants (Jaccard), when either side has any
  callees  - named callees present on both sides, over those named on the Xbox side
  size     - Xbox / PS2 instruction count (MSVC x86 vs GCC R5900: roughly 0.6-1.3 is usual)
Proposals: in each checked pair, the distinct direct call targets in first-call order on both sides. Targets
already named alike are anchors; between two anchors, one unnamed Xbox target facing one PS2 target whose name
the Xbox doesn't have yet is proposed. A proposal is kept with the number of pairs that voted for it.
Writes results/vt-check.md and results/vt-proposals.json. Read-only towards Ghidra.
"""

import collections
import json
import os
import re
import sys
from concurrent.futures import ThreadPoolExecutor

from lib import features_cache, ghidra_ro as g

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path):
    with open(path) as f:
        return {int(x["address"], 16): x for x in json.load(f)["functions"]}


def calls(program, address):
    """Distinct direct call targets, in first-call order."""
    text = g.get("disassemble_function", program=program, address=f"0x{address:x}")
    out = []
    for line in text.splitlines():
        m = re.search(r"\b(?:CALL|jal)\s+0x([0-9a-f]+)\s*$", line)
        if m:
            a = int(m.group(1), 16)
            if a not in out:
                out.append(a)
    return out


def score(fx, fp):
    sx, sp = set(fx.get("strings", [])), set(fp.get("strings", []))
    strings = len(sx & sp) / len(sx | sp) if sx | sp else None
    cx = {c for c in fx.get("callees", []) if not c.startswith("FUN_")}
    cp = set(fp.get("callees", []))
    callees = len(cx & cp) / len(cx) if cx else None
    size = (fx.get("insns") or 0) / max(1, fp.get("insns") or 1)
    return strings, callees, size


def verdict(s):
    strings, callees, size = s
    good = [v for v in (strings, callees) if v is not None]
    if not good:
        return "no evidence" if not 0.5 <= size <= 1.6 else "size only"
    if min(good) >= 0.5 and 0.4 <= size <= 2.0:
        return "ok"
    if max(good) >= 0.5:
        return "mixed"
    return "doubtful"


def main():
    old, new = load(sys.argv[1]), load(sys.argv[2])
    ps2_snap = sorted(p for p in os.listdir(os.path.join(HERE, "data", "snapshots")) if p.startswith("DRIVING.ELF"))[-1]
    ps2 = load(os.path.join(HERE, "data", "snapshots", ps2_snap))
    fx, fp = features_cache.load(g.XBOX), features_cache.load(g.PS2)
    ps2_by_q = collections.defaultdict(list)
    for a, f in ps2.items():
        ps2_by_q[f["qualified"]].append(a)
    xbox_names = {f["qualified"] for f in new.values()}

    pairs, lines = [], ["# Version Tracking names on Driving.xbe, checked against DRIVING.ELF", "",
                        "| Xbox | was | now | PS2 | strings | callees | size | verdict |", "|---|---|---|---|---|---|---|---|"]
    for a, f in sorted(new.items()):
        o = old.get(a)
        if o is not None and o["qualified"] == f["qualified"]:
            continue
        cands = ps2_by_q.get(f["qualified"], [])
        if not cands:
            if not f["name"].startswith("FUN_"):
                lines.append(f"| {a:08x} | {o['qualified'] if o else '(new)'} | {f['qualified']} | none of this name | | | | no PS2 namesake |")
            continue
        best = max(cands, key=lambda p: (verdict(score(fx.get(a, {}), fp.get(p, {}))) == "ok",
                                         sum(v or 0 for v in score(fx.get(a, {}), fp.get(p, {}))[:2])))
        s = score(fx.get(a, {}), fp.get(best, {}))
        v = verdict(s)
        pairs.append((a, best, v))
        fmt = lambda x: "-" if x is None else f"{x:.2f}"
        lines.append(f"| {a:08x} | {o['qualified'] if o else '(new)'} | {f['qualified']} | {best:08x}"
                     f"{' (of %d)' % len(cands) if len(cands) > 1 else ''} | {fmt(s[0])} | {fmt(s[1])} | {s[2]:.2f} | {v} |")

    # Proposals from the call lists of the pairs that check out.
    good = [(a, p) for a, p, v in pairs if v in ("ok", "size only")]
    with ThreadPoolExecutor(4) as pool:
        lists = list(pool.map(lambda ap: (ap, calls(g.XBOX, ap[0]), calls(g.PS2, ap[1])), good))
    votes = collections.defaultdict(list)
    for (a, p), cx, cp in lists:
        nx = [new[t]["qualified"] if t in new else None for t in cx]
        np_ = [ps2[t]["qualified"] if t in ps2 else None for t in cp]
        anchors = [(i, np_.index(n)) for i, n in enumerate(nx)
                   if n and not n.startswith("FUN_") and n in np_ and np_.count(n) == 1]
        bounds = [(-1, -1)] + [ab for ab in anchors] + [(len(nx), len(np_))]
        for (i0, j0), (i1, j1) in zip(bounds, bounds[1:]):
            if i1 <= i0 or j1 <= j0:
                continue
            gx = [cx[i] for i in range(i0 + 1, i1) if nx[i] and nx[i].startswith("FUN_")]
            gp = [cp[j] for j in range(j0 + 1, j1)
                  if np_[j] and not np_[j].startswith("FUN_") and np_[j] not in xbox_names]
            ox = [i for i in range(i0 + 1, i1)]
            op = [j for j in range(j0 + 1, j1)]
            if len(gx) == 1 and len(gp) == 1 and len(ox) == 1 and len(op) == 1:
                votes[(gx[0], gp[0])].append(f"{a:08x}")
    props = []
    by_x = collections.defaultdict(set)
    for (x, p) in votes:
        by_x[x].add(p)
    for (x, p), who in sorted(votes.items()):
        s = score(fx.get(x, {}), fp.get(p, {}))
        props.append({"xbox": f"0x{x:08x}", "ps2": f"0x{p:08x}", "name": ps2[p]["qualified"], "votes": who,
                      "conflict": len(by_x[x]) > 1, "strings": s[0], "callees": s[1], "size": round(s[2], 2),
                      "verdict": verdict(s)})
    with open(os.path.join(HERE, "results", "vt-proposals.json"), "w") as f:
        json.dump(props, f, indent=1)
    c = collections.Counter(v for _, _, v in pairs)
    lines[1:1] = [f"{len(pairs)} pairs: {dict(c)}. {len(props)} call-order proposals "
                  f"({collections.Counter(p['verdict'] for p in props)}) in results/vt-proposals.json.", ""]
    with open(os.path.join(HERE, "results", "vt-check.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(lines[1])


if __name__ == "__main__":
    main()
