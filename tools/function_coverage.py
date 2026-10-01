#!/usr/bin/env python3
"""How much of the action engine's code is ours, by subsystem.

Every function in tools/functions_action.json is
  - REPLACED: its entry jumps to ours (AUTOINJECT, FUNC_AT, ...);
  - DEAD: not replaced, but nothing that can still run reaches it any more (typically something only replaced
    code called);
  - LIVE: anything else - original code that still runs.
Replaced and dead together are what no longer needs reimplementing: "done".

Liveness is global_coverage.py's (CallGraph), with one difference for the XDK libraries: a pointer to a library
function counts only when it sits in the game's own data. Pointers inside the libraries' own code and tables (D3D's
render-state jump table, DSOUND's COM vtables) only matter if the library is entered from outside, and the call
edges already follow that. Without it, every library looks live. global_coverage.py stays conservative, as owning a
global wants.

Subsystems come from tools/subsystems_action.txt (address ranges). Sizes are bytes to the next function, so they
include padding; Ghidra also leaves large stretches of DSOUND and XMV undefined, so those are undercounted.

    python tools/function_coverage.py                 # summary by subsystem
    python tools/function_coverage.py ai.drones       # that subsystem's live functions, largest first
    python tools/function_coverage.py --markdown      # the summary as markdown tables (docs/function-coverage.md)
"""

import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import global_coverage as gc

SUBSYSTEMS = os.path.join(gc.ROOT, "tools", "subsystems_action.txt")

GROUPS = [   # report order, and what each group means
    ("ai", "AI: drones (single-player), bots (multiplayer), navigation"),
    ("player", "Player, weapons, bullets, statistics"),
    ("objects", "World objects: doors, vehicles, triggers, switches, pickups..."),
    ("effects", "Effects: particles, weather, debris"),
    ("mp", "Multiplayer rules and scenarios"),
    ("engine", "Engine core"),
    ("render", "Rendering, game side: views, portal visibility, sprites, fonts"),
    ("loader", "Level loading"),
    ("ui", "UI: front end and HUD"),
    ("audio", "Audio, game side"),
    ("platform", "Eurocom's Xbox layer (psi/xbox/d3d/dsnd)"),
    ("lib", "XDK libraries"),
]
SURFACE = {"platform", "lib"}   # below the surface: replaced by our seams, or needed only by what is


def read_subsystems():
    ranges = []
    for line in open(SUBSYSTEMS, encoding="utf-8"):
        line = line.split("#", 1)[0].strip() if line.lstrip().startswith("#") else line.strip()
        if not line:
            continue
        address, subsystem, module = line.split(None, 2)
        ranges.append((int(address, 16), subsystem, module))
    ranges.sort()
    return ranges


def analyse():
    graph = gc.CallGraph()
    ranges = read_subsystems()
    lib_lo = min(a for a, s, _ in ranges if s.startswith("lib."))
    end = max(a for a, s, _ in ranges if s == "end")
    rdata = (0x0015d160, 0x00163100)

    def in_lib(a):
        return lib_lo <= a < end and not any(s == "engine.static" and lo <= a < hi
                                             for (lo, s, _), (hi, _, _) in zip(ranges, ranges[1:]))

    def counts(function, holder):
        return not (in_lib(function) and (in_lib(holder) or rdata[0] <= holder < rdata[1]))
    live = graph.live(counts, {f for f in graph.no_caller if not in_lib(f)})

    addrs = sorted(graph.funcs)
    out = []
    import bisect
    starts = [r[0] for r in ranges]
    for i, a in enumerate(addrs):
        r = bisect.bisect_right(starts, a) - 1
        if r < 0 or ranges[r][1] == "end":
            continue
        nxt = min(addrs[i + 1] if i + 1 < len(addrs) else a + 16, ranges[r + 1][0])
        status = "REPLACED" if a in graph.replaced else "LIVE" if a in live else "DEAD"
        out.append({"address": a, "name": graph.funcs[a], "subsystem": ranges[r][1], "module": ranges[r][2],
                    "size": nxt - a, "status": status})
    return out


def tally(fs):
    t = {"n": len(fs), "bytes": sum(f["size"] for f in fs)}
    for s in ("REPLACED", "DEAD", "LIVE"):
        t[s] = sum(1 for f in fs if f["status"] == s)
        t[s + "_bytes"] = sum(f["size"] for f in fs if f["status"] == s)
    t["done"] = t["REPLACED"] + t["DEAD"]
    t["done_bytes"] = t["REPLACED_bytes"] + t["DEAD_bytes"]
    return t


def pct(a, b):
    return 100.0 * a / b if b else 0.0


def summary(fs, markdown):
    by_sub = defaultdict(list)
    for f in fs:
        by_sub[f["subsystem"]].append(f)
    if markdown:
        head = "| Subsystem | Functions | Replaced | Dead | Live | Done | KB | Done (bytes) |\n|---|--:|--:|--:|--:|--:|--:|--:|"
        def row(name, t, bold=False):
            b = "**" if bold else ""
            return "| %s%s%s | %d | %d | %d | %d | %s%.0f%%%s | %.0f | %.0f%% |" % (
                b, name, b, t["n"], t["REPLACED"], t["DEAD"], t["LIVE"], b, pct(t["done"], t["n"]), b,
                t["bytes"] / 1024, pct(t["done_bytes"], t["bytes"]))
    else:
        head = "%-20s %6s %6s %6s %6s %6s %8s %7s" % ("subsystem", "funcs", "repl", "dead", "live", "done", "KB", "done(B)")
        def row(name, t, bold=False):
            return "%-20s %6d %6d %6d %6d %5.0f%% %8.0f %6.0f%%" % (
                ("" if bold else "  ") + name, t["n"], t["REPLACED"], t["DEAD"], t["LIVE"], pct(t["done"], t["n"]),
                t["bytes"] / 1024, pct(t["done_bytes"], t["bytes"]))

    lines = [head]
    for group, desc in GROUPS:
        subs = sorted(s for s in by_sub if s.split(".")[0] == group)
        if not subs:
            continue
        gfs = [f for s in subs for f in by_sub[s]]
        lines.append(row(group, tally(gfs), True))
        if len(subs) > 1:
            for s in subs:
                lines.append(row(s, tally(by_sub[s])))
    above = [f for f in fs if f["subsystem"].split(".")[0] not in SURFACE]
    below = [f for f in fs if f["subsystem"].split(".")[0] in SURFACE]
    lines.append(row("game code (above)", tally(above), True))
    lines.append(row("platform + libraries", tally(below), True))
    lines.append(row("everything", tally(fs), True))
    print("\n".join(lines))


def detail(fs, subsystem):
    sel = [f for f in fs if f["subsystem"] == subsystem or f["subsystem"].split(".")[0] == subsystem]
    if not sel:
        sys.exit("no subsystem %s (see tools/subsystems_action.txt)" % subsystem)
    t = tally(sel)
    print("%s: %d functions, %d replaced, %d dead, %d live (%.0f KB still original)" % (
        subsystem, t["n"], t["REPLACED"], t["DEAD"], t["LIVE"], t["LIVE_bytes"] / 1024))
    by_mod = defaultdict(list)
    for f in sel:
        by_mod[f["module"]].append(f)
    for mod, mfs in by_mod.items():
        mt = tally(mfs)
        print("\n  %s: %d/%d done, %.1f KB live" % (mod, mt["done"], mt["n"], mt["LIVE_bytes"] / 1024))
        for f in sorted((f for f in mfs if f["status"] == "LIVE"), key=lambda f: -f["size"]):
            print("    %08x %6d  %s" % (f["address"], f["size"], f["name"]))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    fs = analyse()
    if args:
        for a in args:
            detail(fs, a)
    else:
        summary(fs, "--markdown" in sys.argv)


if __name__ == "__main__":
    main()
