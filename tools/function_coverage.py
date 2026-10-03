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

The C runtime (lib.crt) is the one library not worked on directly: game code calls sprintf, memcpy and malloc
everywhere, and it goes away by itself as that code becomes ours, which our compiler's runtime then serves. XAPI
code that only the C runtime reaches (the heap under malloc, RaiseException under the exception handling) is
counted with it, under the module "XAPI under the C runtime".

    python tools/function_coverage.py                 # summary by subsystem
    python tools/function_coverage.py ai.drones       # that subsystem's live functions, largest first
    python tools/function_coverage.py --markdown      # the summary as markdown tables (docs/function-coverage.md)
    python tools/function_coverage.py --why platform  # each live function there, what keeps it live: the live
                                                      # functions calling it, and any root (our code calls it,
                                                      # our code or data holds its address, the XBE entry point)

--driving does the same for the driving engine (Driving.xbe), with tools/subsystems_driving.txt: four tiers (game,
engine, platform, sys), address ranges for the libraries and class-name rules for the rest. Its seams' generated
entry tables count as replaced, and its AUTOGEN bodies give the originals it still calls.

    python tools/function_coverage.py --driving                  # summary by tier and subsystem
    python tools/function_coverage.py --driving game.ai          # one subsystem's live functions, by class
    python tools/function_coverage.py --driving --unclassified   # named functions no class rule matches
"""

import json
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import global_coverage as gc

SUBSYSTEMS = os.path.join(gc.ROOT, "tools", "subsystems_action.txt")
SUBSYSTEMS_DRIVING = os.path.join(gc.ROOT, "tools", "subsystems_driving.txt")

DRIVING_GROUPS = [   # report order, top down
    ("game", "Gameplay: AI, mission events, missions, vehicles, weapons, HUD and front end, gameplay audio and effects"),
    ("engine", "Game engine: animation, rendering, cameras, physics, world, audio, input, data, core loop, utilities"),
    ("platform", "EA's platform layer: EAGL, sound library, files, memory, threads, timers, maths, movies"),
    ("sys", "System APIs: XDK libraries (D3D, DSOUND, XPP), XAPI and kernel thunks, the C/C++ runtime"),
]
DRIVING_RDATA = (0x00189BE0, 0x001B3D98)
# A class whose functions span more than this is one whose inline copies are scattered through the binary (EAGL, std,
# VU0...): its functions do not say which subsystem the unnamed code around them belongs to
ANCHOR_SPAN = 0x8000

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


def class_key(name):
    """The class a driving engine function belongs to, for the class rules (see tools/subsystems_driving.txt); None
    for an unnamed one"""
    if name.startswith(("FUN_", "thunk_FUN", "LAB_")):
        return None
    if "::" in name:
        return name.split("::")[0]
    if "__" in name.lstrip("_"):
        return name.lstrip("_").split("__")[0]
    return name.split("_")[0] or name


def read_driving_map():
    ranges, rules = [], []
    for line in open(SUBSYSTEMS_DRIVING, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        kind, rest = line.split(None, 1)
        if kind == "range":
            address, subsystem, desc = rest.split(None, 2)
            ranges.append((int(address, 16), subsystem, desc))
        elif kind == "class":
            regex, subsystem = rest.rsplit(None, 1)
            rules.append((re.compile(regex), subsystem))
        elif kind == "float":
            FLOATING.append(re.compile(rest))
        elif kind == "func":
            address, subsystem = rest.split()[:2]
            FUNC_OVERRIDES[int(address, 16)] = subsystem
    ranges.sort()
    return ranges, rules


FLOATING = []   # classes that never set the neighbour (subsystems_driving.txt's "float" lines)
FUNC_OVERRIDES = {}   # address -> subsystem, for one function no rule can single out ("func" lines)


def classify_driving(funcs):
    """(address, subsystem, module, range index) for every driving engine function, and the ranges"""
    import bisect
    ranges, rules = read_driving_map()
    starts = [r[0] for r in ranges]
    addrs = sorted(funcs)
    keys = {a: class_key(funcs[a]) for a in addrs}
    span = {}
    for a in addrs:
        k = keys[a]
        if k is not None:
            lo, hi = span.get(k, (a, a))
            span[k] = (min(lo, a), max(hi, a))
    named = [a for a in addrs if keys[a] is not None]
    same_as_neighbour = set()
    for p, q in zip(named, named[1:]):
        if keys[p] == keys[q]:
            same_as_neighbour.update((p, q))

    def rule(k):
        for regex, subsystem in rules:
            if regex.search(k):
                return subsystem
        return None

    out = []
    anchor, anchor_range = None, None
    for a in addrs:
        r = bisect.bisect_right(starts, a) - 1
        if r < 0 or ranges[r][1] == "end":
            continue
        rs = ranges[r][1]
        if r != anchor_range:
            anchor, anchor_range = None, r
        if rs.startswith("sys.") or rs == "engine.static":   # fixed ranges
            out.append((a, rs, ranges[r][2], r))
            continue
        k = keys[a]
        subsystem = rule(k) if k is not None else None
        if subsystem is not None and (span[k][1] - span[k][0] <= ANCHOR_SPAN or a in same_as_neighbour) and \
                not any(regex.search(k) for regex in FLOATING):
            anchor = subsystem
        if subsystem is None:
            subsystem = anchor or rs
        out.append((a, FUNC_OVERRIDES.get(a, subsystem), k or "(unnamed)", r))
    return out, ranges


def analyse(want_graph=False, driving=False):
    if driving:
        gc.use_engine("driving")
    graph = gc.CallGraph()
    if driving:
        classified, ranges = classify_driving(graph.funcs)
        sub_of = {a: s for a, s, _, _ in classified}
        rdata = DRIVING_RDATA
        import bisect
        range_starts = [r[0] for r in ranges]

        def in_lib(a):
            # a function by its own classification; any other address (a holder inside a library's code) by its range
            if a in sub_of:
                return sub_of[a].startswith("sys.")
            r = bisect.bisect_right(range_starts, a) - 1
            return r >= 0 and ranges[r][1].startswith("sys.")
    else:
        ranges = read_subsystems()
        lib_lo = min(a for a, s, _ in ranges if s.startswith("lib."))
        end = max(a for a, s, _ in ranges if s == "end")
        rdata = (0x0015d160, 0x00163100)

        def in_lib(a):
            return lib_lo <= a < end and not any(s == "engine.static" and lo <= a < hi
                                                 for (lo, s, _), (hi, _, _) in zip(ranges, ranges[1:]))

    def counts(function, holder):
        return not (in_lib(function) and (in_lib(holder) or rdata[0] <= holder < rdata[1]))
    no_caller = {f for f in graph.no_caller if not in_lib(f)}
    live = graph.live(counts, no_caller)

    # What is live without passing through the C runtime: XAPI functions outside it belong with the C runtime
    crt_name, xapi_name = ("sys.crt", "sys.xapi") if driving else ("lib.crt", "lib.xapi")
    crt = [(lo, hi) for (lo, s, _), (hi, _, _) in zip(ranges, ranges[1:]) if s == crt_name]
    in_crt = lambda a: any(lo <= a < hi for lo, hi in crt)
    saved = graph.replaced
    graph.replaced = saved | {a for a in graph.funcs if in_crt(a)}
    live_without_crt = graph.live(counts, no_caller)
    graph.replaced = saved

    addrs = sorted(graph.funcs)
    out = []
    import bisect
    starts = [r[0] for r in ranges]
    assigned = {a: (s, m) for a, s, m, _ in classified} if driving else {}
    for i, a in enumerate(addrs):
        r = bisect.bisect_right(starts, a) - 1
        if r < 0 or ranges[r][1] == "end":
            continue
        nxt = min(addrs[i + 1] if i + 1 < len(addrs) else a + 16, ranges[r + 1][0])
        status = "REPLACED" if a in graph.replaced else "LIVE" if a in live else "DEAD"
        subsystem, module = assigned[a] if driving else (ranges[r][1], ranges[r][2])
        if subsystem == xapi_name and status == "LIVE" and a not in live_without_crt:
            subsystem, module = crt_name, "XAPI under the C runtime"
        out.append({"address": a, "name": graph.funcs[a], "subsystem": subsystem, "module": module,
                    "size": nxt - a, "status": status})
    if want_graph:
        return out, graph
    return out


def why(fs, graph, subsystem):
    """Each live function in a subsystem, and what keeps it live"""
    by = {f["address"]: f for f in fs}
    callers = defaultdict(set)
    for a, cs in graph.edges.items():
        for c in cs:
            callers[c].add(a)
    sel = sorted((f for f in fs if f["status"] == "LIVE" and
                  (f["subsystem"] == subsystem or f["subsystem"].split(".")[0] == subsystem)), key=lambda f: f["address"])
    if not sel:
        sys.exit("nothing live in %s" % subsystem)
    for f in sel:
        a = f["address"]
        roots = []
        if a == graph.entry:
            roots.append("XBE entry point")
        if a in graph.autogen:
            roots.append("our code calls it")
        if a in graph.ours_points_at:
            roots.append("our code holds its address")
        if graph.pointed_at.get(a):
            roots.append("data at " + ", ".join("%08x" % h for h in sorted(graph.pointed_at[a])[:3]))
        live_callers = sorted(c for c in callers[a] if c in by and by[c]["status"] == "LIVE")
        names = ["%s (%s)" % (by[c]["name"], by[c]["subsystem"]) for c in live_callers]
        print("%08x %5d  %-36s %s" % (a, f["size"], f["name"], "; ".join(roots + names) or "no known caller"))


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


def summary(fs, markdown, driving=False):
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
    for group, desc in (DRIVING_GROUPS if driving else GROUPS):
        subs = sorted(s for s in by_sub if s.split(".")[0] == group)
        if not subs:
            continue
        gfs = [f for s in subs for f in by_sub[s]]
        lines.append(row(group, tally(gfs), True))
        if len(subs) > 1:
            for s in subs:
                lines.append(row(s, tally(by_sub[s])))
    if driving:
        tier = lambda f: f["subsystem"].split(".")[0]
        lines.append(row("game + engine", tally([f for f in fs if tier(f) in ("game", "engine")]), True))
        lines.append(row("platform + system", tally([f for f in fs if tier(f) in ("platform", "sys")]), True))
        lines.append(row("  without the C runtime", tally([f for f in fs if tier(f) in ("platform", "sys") and
                                                           f["subsystem"] != "sys.crt"]), True))
        lines.append(row("everything", tally(fs), True))
        print("\n".join(lines))
        return
    above = [f for f in fs if f["subsystem"].split(".")[0] not in SURFACE]
    below = [f for f in fs if f["subsystem"].split(".")[0] in SURFACE]
    lines.append(row("game code (above)", tally(above), True))
    lines.append(row("platform + libraries", tally(below), True))
    lines.append(row("  without the C runtime", tally([f for f in below if f["subsystem"] != "lib.crt"]), True))
    lines.append(row("everything", tally(fs), True))
    print("\n".join(lines))


def detail(fs, subsystem):
    sel = [f for f in fs if f["subsystem"] == subsystem or f["subsystem"].split(".")[0] == subsystem]
    if not sel:
        sys.exit("no subsystem %s (see tools/subsystems_action.txt or subsystems_driving.txt)" % subsystem)
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


def unclassified():
    """Named driving engine functions in classified ranges that no class rule matches, by class, largest first"""
    gc.use_engine("driving")
    funcs = {int(f["address"], 16): f["name"] for f in json.load(open(gc.FUNCTIONS))}
    classified, ranges = classify_driving(funcs)
    _, rules = read_driving_map()
    counts = defaultdict(list)
    for a, s, k, r in classified:
        rs = ranges[r][1]
        if k == "(unnamed)" or rs.startswith("sys.") or rs == "engine.static":
            continue
        if not any(regex.search(k) for regex, _ in rules):
            counts[k].append((a, s))
    for k, items in sorted(counts.items(), key=lambda kv: -len(kv[1])):
        print("%4d  %-40s e.g. %08x -> %s" % (len(items), k, items[0][0], items[0][1]))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    driving = "--driving" in sys.argv
    if "--unclassified" in sys.argv:
        unclassified()
        return
    if "--why" in sys.argv:
        fs, graph = analyse(want_graph=True, driving=driving)
        for a in args:
            why(fs, graph, a)
        return
    fs = analyse(driving=driving)
    if args:
        for a in args:
            detail(fs, a)
    else:
        summary(fs, "--markdown" in sys.argv, driving)


if __name__ == "__main__":
    main()
