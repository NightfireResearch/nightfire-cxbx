"""Windowed review packets for Driving.xbe: unnamed Xbox functions between two named ones, against the PS2 names
not yet on the Xbox in the matching PS2 range. Read-only.

    python xbox_windows.py --build                  # data/xbox-windows.json from live names
    python xbox_windows.py --sample 30 [seed]       # data/xbox-window-queue.json: windows to review
    python xbox_windows.py 0 15                     # packets for queue windows 0..14

A window: consecutive named Xbox functions A and B whose PS2 counterparts lie at most 60 PS2 functions apart
(the linker keeps an object file's code together, in another order). Listed: every Xbox function between A and B
(unnamed ones with callers, a caller chain up to a function named on both platforms, callees and a short body),
and every PS2 function between A' and B' whose name the Xbox doesn't have (callers, callees, short body).
"""

import collections
import json
import os
import random
import sys

from lib import ghidra_ro as g
from vt_review import body, chains, names, unnamed

HERE = os.path.dirname(os.path.abspath(__file__))
GAME_END = 0x1503b0   # after this, .text holds compiler-generated handlers, then the XDK sections


def build():
    xn, pn = names(g.XBOX), names(g.PS2)
    pc = collections.Counter(pn.values())
    pby = {n: a for a, n in pn.items() if pc[n] == 1}

    def ps2(n):
        if n in pby:
            return pby[n]
        if n.endswith("::scalar_deleting_destructor"):
            c = n.rsplit("::", 1)[0]
            return pby.get(f"{c}::~{c.split('::')[-1]}")
    pa = sorted(pn)
    prank = {a: i for i, a in enumerate(pa)}
    xnames = set(xn.values())
    game = sorted((a, n) for a, n in xn.items() if a < GAME_END)
    anch = [(i, ps2(n)) for i, (a, n) in enumerate(game) if not unnamed(n) and ps2(n)]
    out = []
    for (i0, p0), (i1, p1) in zip(anch, anch[1:]):
        gap = [game[k][0] for k in range(i0 + 1, i1) if unnamed(game[k][1])]
        lo, hi = sorted((prank[p0], prank[p1]))
        if not gap or hi - lo > 60:
            continue
        cand = [pa[k] for k in range(lo + 1, hi) if not unnamed(pn[pa[k]]) and pn[pa[k]] not in xnames]
        if cand:
            out.append({"x0": game[i0][0], "x1": game[i1][0], "p0": p0, "p1": p1,
                        "xbox": [x for x in range(0)] or gap, "ps2": cand})
    with open(os.path.join(HERE, "data", "xbox-windows.json"), "w") as f:
        json.dump(out, f)
    print(f"{len(out)} windows, {sum(len(w['xbox']) for w in out)} unnamed Xbox functions, "
          f"{sum(len(w['ps2']) for w in out)} PS2 names available")


def packet(w, i):
    xn, pn = names(g.XBOX), names(g.PS2)
    xset, pset = set(xn.values()), set(pn.values())
    cal = lambda prog, a: ", ".join(sorted({n for _, n in g.callers(prog, a)}))[:200]
    cee = lambda prog, a: ", ".join(sorted({n for _, n in g.callees(prog, a)}))[:250]
    out = [f"## window {i}: Xbox {w['x0']:#x} {xn[w['x0']]} .. {w['x1']:#x} {xn[w['x1']]}   "
           f"(PS2 {w['p0']:#x} .. {w['p1']:#x})", "", "Xbox functions in the window, address order:"]
    lo, hi = w["x0"], w["x1"]
    for a in sorted(a for a in xn if lo < a < hi):
        if not unnamed(xn[a]):
            out.append(f"  {a:#x} (named) {xn[a]}")
            continue
        out += [f"  {a:#x} UNNAMED", f"     callers: {cal(g.XBOX, a)}", f"     chain: " + " | ".join(chains(g.XBOX, a, pset)[:3]),
                f"     callees: {cee(g.XBOX, a)}", "     " + body(g.XBOX, a, 14).replace("\n", "\n     ")]
    out += ["", "PS2 functions in the matching range whose names the Xbox doesn't have yet:"]
    for p in w["ps2"]:
        out += [f"  {p:#x} {pn[p]}", f"     callers: {cal(g.PS2, p)}", f"     callees: {cee(g.PS2, p)}",
                "     " + body(g.PS2, p, 12).replace("\n", "\n     ")]
    return "\n".join(out)


def main():
    if sys.argv[1] == "--build":
        build()
        return
    if sys.argv[1] == "--sample":
        with open(os.path.join(HERE, "data", "xbox-windows.json")) as f:
            ws = json.load(f)
        seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
        ws = [w for w in ws if len(w["xbox"]) <= 8 and len(w["ps2"]) <= 20]   # trial: moderate windows
        random.Random(seed).shuffle(ws)
        with open(os.path.join(HERE, "data", "xbox-window-queue.json"), "w") as f:
            json.dump(ws[:int(sys.argv[2])], f)
        print(f"{min(len(ws), int(sys.argv[2]))} windows queued of {len(ws)} moderate ones")
        return
    with open(os.path.join(HERE, "data", "xbox-window-queue.json")) as f:
        q = json.load(f)
    for i in range(int(sys.argv[1]), min(int(sys.argv[2]), len(q))):
        print(packet(q[i], i))
        print("\n" + "=" * 100 + "\n")


if __name__ == "__main__":
    main()
