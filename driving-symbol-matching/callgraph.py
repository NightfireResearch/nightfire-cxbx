"""Candidate Xbox names from the call graph: the user's caller-chain method, run from every anchor at once.

    python callgraph.py            -> data/callgraph-candidates.json, results/callgraph.md

Anchors: Xbox functions whose qualified name is carried by exactly one PS2 function (a scalar deleting destructor
counts as the PS2 destructor). For each anchor pair, the distinct direct call targets on both sides, in first-call
order (disassembly; indirect calls are skipped). Targets named alike on both sides are fixed points; between two
fixed points, when the Xbox side has exactly one target and the PS2 side exactly one, and the Xbox one is unnamed
and the PS2 one's name is not yet on the Xbox, they are proposed as a pair ("1:1"). Runs of equal length n > 1 are
proposed pairwise in order ("n:n", weaker). Every anchor proposing a pair is a vote; a pair whose Xbox or PS2
function is proposed with a different partner elsewhere is marked as conflicting.
Read-only; call lists are cached in data/callgraph-calls.json (delete it after renames to refresh).
"""

import collections
import json
import os
import re
from concurrent.futures import ThreadPoolExecutor

from lib import features_cache, ghidra_ro as g

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "data", "callgraph-calls.json")


def unnamed(n):
    return n is None or n.split("::")[-1].startswith("FUN_")


def calls(program, address):
    text = g.get("disassemble_function", program=program, address=f"0x{address:x}")
    out = []
    for line in text.splitlines():
        m = re.search(r"\b(?:CALL|jal)\s+0x([0-9a-f]+)\s*$", line)
        if m:
            a = int(m.group(1), 16)
            if a not in out:
                out.append(a)
    return out


def names(program):
    q = g.qualified_names(program)
    return {a: q.get(a, n) for a, n in g.functions(program)}


def main():
    xn, pn = names(g.XBOX), names(g.PS2)
    pcount = collections.Counter(pn.values())
    p_by = {n: a for a, n in pn.items() if pcount[n] == 1}

    def ps2_of(xname):
        if xname in p_by:
            return p_by[xname]
        if xname.endswith("::scalar_deleting_destructor"):
            cls = xname.rsplit("::", 1)[0]
            return p_by.get(f"{cls}::~{cls.split('::')[-1]}")
        return None

    xcount = collections.Counter(xn.values())
    anchors = [(a, ps2_of(n)) for a, n in xn.items() if not unnamed(n) and xcount[n] == 1 and ps2_of(n)]
    cache = {}
    if os.path.exists(CACHE):
        with open(CACHE) as f:
            cache = json.load(f)
    todo = [(prog, a) for x, p in anchors for prog, a in ((g.XBOX, x), (g.PS2, p)) if f"{prog}:{a:x}" not in cache]

    def one(pa):
        try:
            return f"{pa[0]}:{pa[1]:x}", calls(*pa)
        except Exception:
            return f"{pa[0]}:{pa[1]:x}", []
    with ThreadPoolExecutor(6) as pool:
        for k, v in pool.map(one, todo):
            cache[k] = v
    with open(CACHE, "w") as f:
        json.dump(cache, f)

    xnames_set = set(xn.values())
    votes = collections.defaultdict(list)
    for x, p in anchors:
        cx, cp = cache[f"{g.XBOX}:{x:x}"], cache[f"{g.PS2}:{p:x}"]
        nx = [xn.get(t) for t in cx]
        np_ = [pn.get(t) for t in cp]
        fixed = []
        for i, n in enumerate(nx):
            if n and not unnamed(n) and np_.count(n) == 1:
                j = np_.index(n)
                if not fixed or (i > fixed[-1][0] and j > fixed[-1][1]):
                    fixed.append((i, j))
        bounds = [(-1, -1)] + fixed + [(len(nx), len(np_))]
        for (i0, j0), (i1, j1) in zip(bounds, bounds[1:]):
            sx, sp = list(range(i0 + 1, i1)), list(range(j0 + 1, j1))
            if not sx or len(sx) != len(sp):
                continue
            kind = "1:1" if len(sx) == 1 else f"{len(sx)}:{len(sx)}"
            for i, j in zip(sx, sp):
                if unnamed(nx[i]) and np_[j] and not unnamed(np_[j]) and np_[j] not in xnames_set and nx[i] is not None:
                    votes[(cx[i], cp[j])].append({"anchor": xn[x], "kind": kind})
    by_x, by_p = collections.defaultdict(set), collections.defaultdict(set)
    for x, p in votes:
        by_x[x].add(p)
        by_p[p].add(x)
    fx, fp = features_cache.load(g.XBOX), features_cache.load(g.PS2)
    out = []
    for (x, p), vs in votes.items():
        a, b = fx.get(x, {}), fp.get(p, {})
        sx = {s for s in a.get("strings", []) if s.strip()}
        sp = {s for s in b.get("strings", []) if s.strip()}
        out.append({"xbox": f"0x{x:08x}", "ps2": f"0x{p:08x}", "xbox_name": xn.get(x), "ps2_name": pn[p],
                    "votes": len(vs), "one_to_one": sum(v["kind"] == "1:1" for v in vs),
                    "anchors": sorted({v["anchor"] for v in vs})[:6],
                    "conflict": len(by_x[x]) > 1 or len(by_p[p]) > 1,
                    "size": round((a.get("insns") or 0) / max(1, b.get("insns") or 1), 2),
                    "shared_strings": len(sx & sp), "strings_both": bool(sx and sp)})
    out.sort(key=lambda r: (r["conflict"], -r["one_to_one"], -r["votes"]))
    with open(os.path.join(HERE, "data", "callgraph-candidates.json"), "w") as f:
        json.dump(out, f, indent=1)
    c = collections.Counter(("conflict" if r["conflict"] else "clean",
                             "1:1" if r["one_to_one"] else "n:n only",
                             "2+ votes" if r["votes"] > 1 else "1 vote") for r in out)
    lines = [f"# Call-graph candidates: {len(anchors)} anchors, {len(out)} candidate pairs", ""] + \
            [f"- {k}: {v}" for k, v in sorted(c.items())]
    with open(os.path.join(HERE, "results", "callgraph.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
