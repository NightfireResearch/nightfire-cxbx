"""Review packets for Version Tracking candidates (results/vt-triage.json). Read-only.

    python vt_review.py --queue new A 60 [seed]    # data/vt-review-queue.json: a random sample of that kind/tier
    python vt_review.py 0 20                        # packets for queue items 0..19

Each packet: the pair and its triage evidence; both decompiled bodies; both callee lists; and both sides' caller
chains, walked upwards (up to 3 levels) until a function named on both sides, as done by hand: if the chains
meet the same named function by the same route, the pair and the unnamed functions along the route are placed
by the call graph even where inlining has changed the bodies.
"""

import json
import os
import random
import sys

from lib import ghidra_ro as g

HERE = os.path.dirname(os.path.abspath(__file__))
_names = {}


def names(program):
    if program not in _names:
        q = g.qualified_names(program)
        _names[program] = {a: q.get(a, n) for a, n in g.functions(program)}
    return _names[program]


def unnamed(n):
    return n is None or n.split("::")[-1].startswith("FUN_")


def body(program, address, lines):
    text = g.decompile(program, address)
    keep, in_comment = [], False
    for l in text.splitlines():
        s = l.strip()
        if in_comment or s.startswith("/*"):           # plate and warning comments
            in_comment = "*/" not in s
            continue
        if s and "unaff_" not in l:
            keep.append(l)
    return "\n".join(keep[:lines]) + ("\n    ..." if len(keep) > lines else "")


def chains(program, address, other_names, depth=3, limit=8):
    """Caller paths upwards until a function whose name the other program also has."""
    nm = names(program)
    out, frontier = [], [[address]]
    for _ in range(depth):
        nxt = []
        for path in frontier:
            for a, _n in g.callers(program, path[-1])[:12]:
                if a in path:
                    continue
                p = path + [a]
                n = nm.get(a)
                if n and not unnamed(n) and n in other_names:
                    out.append(p)
                else:
                    nxt.append(p)
        frontier = nxt[:40]
        if len(out) >= limit:
            break
    fmt = lambda p: " <- ".join(f"{nm.get(a, '?')}@{a:x}" for a in p[1:])
    return [fmt(p) for p in out[:limit]] or ["(no chain to a function named on both sides within 3 levels)"]


def packet(r, i):
    x, p = int(r["xbox"], 16), int(r["ps2"], 16)
    xn, pn = names(g.XBOX), names(g.PS2)
    xset, pset = set(xn.values()), set(pn.values())
    callee = lambda prog, a: ", ".join(sorted({n for _, n in g.callees(prog, a)}))[:400]
    return "\n".join([
        f"## item {i}: Xbox {r['xbox']} ({r['xbox_name']})  <-  PS2 {r['ps2']} {r['ps2_name']}",
        f"triage: tier {r['tier']}, PS2 distance to Xbox neighbours {r['distance']}, + {', '.join(r['positive']) or '-'}, "
        f"size ratio {r['size']}; VT: {'; '.join(r['correlators'])}",
        f"Xbox callers up to a shared name: " + " | ".join(chains(g.XBOX, x, pset)),
        f"PS2 callers up to a shared name:  " + " | ".join(chains(g.PS2, p, xset)),
        f"Xbox callees: {callee(g.XBOX, x)}", f"PS2 callees:  {callee(g.PS2, p)}",
        "--- Xbox", body(g.XBOX, x, 30), "--- PS2", body(g.PS2, p, 30), ""])


def main():
    if sys.argv[1] == "--queue":
        kind, tier, n = sys.argv[2], sys.argv[3], int(sys.argv[4])
        seed = int(sys.argv[5]) if len(sys.argv) > 5 else 1
        with open(os.path.join(HERE, "results", "vt-triage.json")) as f:
            rows = [r for r in json.load(f) if r["kind"] == kind and r["tier"] == tier]
        random.Random(seed).shuffle(rows)
        with open(os.path.join(HERE, "data", "vt-review-queue.json"), "w") as f:
            json.dump(rows[:n] + rows[n:], f, indent=1)   # the whole tier, shuffled; the trial takes the first n
        print(f"{len(rows)} {kind}/{tier} candidates queued (shuffled, seed {seed})")
        return
    with open(os.path.join(HERE, "data", "vt-review-queue.json")) as f:
        q = json.load(f)
    lo, hi = int(sys.argv[1]), int(sys.argv[2])
    for i in range(lo, min(hi, len(q))):
        print(packet(q[i], i))
        print("=" * 100)


if __name__ == "__main__":
    main()
