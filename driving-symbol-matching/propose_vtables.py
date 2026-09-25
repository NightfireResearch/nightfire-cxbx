"""Xbox name proposals from paired vtables (run vtables.py first).

    python propose_vtables.py [--probable]   -> data/proposals-vtables.json, results/proposals-vtables.md

For every certain vtable pair (and probable ones with --probable), slot i on PS2 names slot i on Xbox. The PS2
function's name comes from its sheet row. One Xbox function reached from several slots or classes (the linker
folded identical code) collects all its names: the first by sheet row becomes the name, and the rest go in
the plate comment.

Slot-0 destructors: MSVC's slot holds the scalar deleting destructor. It gets "Class::scalar_deleting_destructor",
and the one function it calls (other than the allocator's free) is proposed as "Class::~Class".

Outcome per Xbox function:
  propose    unnamed (FUN_) now
  confirmed  already carries the first canonical name
  conflict   carries a different name, which would be replaced: needs review
  stub       folded into more than 8 names (empty or return-constant stubs): reported, not renamed
Read-only.
"""

import json
import os
import sys
from collections import defaultdict

from lib import ghidra_ro as g
from lib.index import Index, body_size, key

HERE = os.path.dirname(os.path.abspath(__file__))
STUB_LIMIT = 8
SOURCE_RANK = {"sheet": 0, "ghidra": 1, "infill-exact": 1, "infill-exact-run": 2, "infill-count": 3}


def same(have, name):
    """Already carries the name: exactly, or bare while the namespace move is still to do."""
    return bool(have) and (key(have) == name or ("::" in name and have == name.split("::")[-1]))


def canonical(name):
    """Sheet name without its argument list, spaces made Ghidra-safe: 'A::operator delete(void *' -> 'A::operator_delete'."""
    return key(name)


def main():
    with_probable = "--probable" in sys.argv
    ix = Index()
    with open(os.path.join(HERE, "data", "vtables.json")) as f:
        vt = json.load(f)

    row_by_ps2 = {r["ps2"]: r for r in ix.rows if r["ps2"] is not None}
    used = defaultdict(list)  # Xbox vtable -> PS2 vtables paired with it
    pairs = []
    for p in vt["ps2"]:
        if p.get("confidence") in ("certain",) or (with_probable and p.get("confidence") == "probable"):
            x = p["candidates"][0]["xbox"]
            used[x].append(p["name"])
            pairs.append(p)
    xbox_vt = {v["address"]: v for v in vt["xbox"]}

    claims = defaultdict(list)  # Xbox function -> [claim]
    skipped = []
    for p in pairs:
        x = p["candidates"][0]["xbox"]
        if len(used[x]) > 1:
            skipped.append(f"{p['name']}: Xbox vtable {x} is also paired with {', '.join(n for n in used[x] if n != p['name'])}")
            continue
        cls = p["name"][:-len(" virtual table")]
        for slot, (ps, xs) in enumerate(zip(p["slots"], xbox_vt[x]["slots"])):
            ps, xs = int(ps, 16), int(xs, 16)
            r = row_by_ps2.get(ps)
            if r is None:
                continue
            claims[xs].append({"name": canonical(r["name"]), "full": r["name"], "row": r["row"], "ps2": ps,
                               "from": r["ps2_from"], "class": cls, "slot": slot, "vtable_ps2": p["address"],
                               "vtable_xbox": x, "pair": p["confidence"], "truncated": r["truncated"]})

    out = []
    for xa, cl in claims.items():
        names = sorted({(c["row"], c["name"]) for c in cl})
        first = names[0][1]
        f = ix.xbox.get(xa)
        have = f["qualified"] if f else None
        size = body_size(f) if f else None
        classes = {c["class"] for c in cl}
        if len(names) > STUB_LIMIT or (len(names) > 1 and len(classes) > 1 and size is not None and size <= 6):
            outcome = "stub"  # a shared empty/constant stub: keep its name
        elif have is None:
            outcome = "propose (create function)"
        elif have.startswith("FUN_"):
            outcome = "propose"
        elif key(have) == first or (have == first.split("::")[-1] and "::" in first):
            outcome = "confirmed"  # bare name: the namespace move is still to do
        else:
            outcome = "conflict"
        weakest = max(cl, key=lambda c: SOURCE_RANK.get(c["from"], 9))
        out.append({"xbox": xa, "have": have, "name": first, "also": [n for _, n in names[1:]], "outcome": outcome,
                    "weakest_source": weakest["from"], "pair": min(c["pair"] for c in cl),
                    "evidence": [f"{c['class']} slot {c['slot']} (PS2 {c['ps2']:08x}, row {c['row']}, {c['from']})" for c in cl]})

    # Slot-0 destructors: MSVC's scalar deleting destructor, and the real destructor it calls.
    for o in list(out):
        if not o["name"].split("::")[-1].startswith("~") or o["outcome"] == "stub":
            continue
        cls = o["name"].rsplit("::", 1)[0]
        dtor = o["name"]
        o["name"] = cls + "::scalar_deleting_destructor"
        o["also"] = [n for n in o["also"]]
        o["outcome"] = ("confirmed" if same(o["have"], o["name"]) else
                        "propose" if (o["have"] or "FUN_").startswith("FUN_") else "conflict")
        o["evidence"].append("vtable slot 0 on MSVC is the scalar deleting destructor")
        # The real destructor: a callee that itself stores this class's vtable (0x1cbf0: mov [ecx], vtable;
        # jmp base). A callee shared by several classes' wrappers is a base destructor, not this one.
        stores = set()
        for c in claims[o["xbox"]]:
            if c["slot"] == 0:
                stores.update(xbox_vt[c["vtable_xbox"]]["stores"])
        for a, n in g.callees(g.XBOX, o["xbox"]):
            f = ix.xbox.get(a)
            size = body_size(f) if f else None
            if not size or not any(a <= s < a + size for s in stores):
                continue
            have = f["qualified"]
            out.append({"xbox": a, "have": have, "name": dtor, "also": [],
                        "outcome": "confirmed" if same(have, dtor) else
                                   "propose" if have.startswith("FUN_") else "conflict",
                        "weakest_source": o["weakest_source"], "pair": o["pair"],
                        "evidence": [f"called by {cls}::scalar_deleting_destructor (0x{o['xbox']:08x})"
                                     f" and stores {cls}'s vtable"]})

    out.sort(key=lambda o: o["xbox"])
    with open(os.path.join(HERE, "data", "proposals-vtables.json"), "w") as f:
        json.dump(out, f, indent=1)

    from collections import Counter
    counts = Counter(o["outcome"] for o in out)
    lines = [f"# Proposals from vtables ({len(pairs)} pairs{' incl. probable' if with_probable else ''})", "",
             "Outcomes: " + ", ".join(f"{k} {n}" for k, n in counts.most_common()), ""]
    if skipped:
        lines += ["Pairs skipped because two PS2 vtables claim one Xbox vtable:", ""] + [f"- {s}" for s in skipped] + [""]
    for outcome in ("conflict", "propose (create function)", "propose", "stub", "confirmed"):
        sel = [o for o in out if o["outcome"] == outcome]
        if not sel:
            continue
        lines += [f"## {outcome} ({len(sel)})", "", "| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |",
                  "|---|---|---|---|---|---|"]
        for o in sel:
            also = ", ".join(o["also"][:4]) + (f" +{len(o['also']) - 4}" if len(o["also"]) > 4 else "")
            lines.append(f"| {o['xbox']:08x} | {o['have'] or '(no function)'} | {o['name']} | {also} | "
                         f"{o['weakest_source']} | {'; '.join(o['evidence'][:3])}{' ...' if len(o['evidence']) > 3 else ''} |")
        lines.append("")
    with open(os.path.join(HERE, "results", "proposals-vtables.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(lines[2])


if __name__ == "__main__":
    main()
