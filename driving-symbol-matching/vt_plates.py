"""Build an Xbox batch that undoes Version Tracking's plate-comment markup, plus hand-checked name fixes.

    python vt_plates.py 004 <Xbox snapshot before VT> <Xbox snapshot after VT> [fixes.json]

VT copies the PS2 plate onto the Xbox function: PS2 notes, and the PS2 [symbol-matching] block with its PS2
batch and sheet evidence. For every function whose plate changed between the two snapshots:
  - the old plate had our Xbox block: restore the old plate exactly;
  - the new plate carries a copied PS2 block: the old plate's own text (usually none) plus an Xbox block,
    whose evidence is the VT match to the PS2 function of the same name;
  - otherwise (a copied PS2 note only): restore the old plate.
fixes.json: [{"xbox", "name", "why", "ps2"?}] renames; they take the same plate treatment, with their own block.
Read-only towards Ghidra; apply.py does the writing.
"""

import collections
import json
import os
import re
import sys

from lib import names

HERE = os.path.dirname(os.path.abspath(__file__))
BEGIN, END = "[symbol-matching]", "[/symbol-matching]"


def load(path):
    with open(path) as f:
        return {int(x["address"], 16): x for x in json.load(f)["functions"]}


def outside(plate):
    """The plate without any managed block."""
    plate = plate or ""
    while BEGIN in plate and END in plate:
        head, rest = plate.split(BEGIN, 1)
        plate = head.rstrip() + ("\n\n" if head.strip() else "") + rest.split(END, 1)[1].lstrip()
    return plate.strip()


def field(plate, key):
    m = re.search(r"^\s*" + re.escape(key) + r": (.*)$", plate or "", re.M)
    return m.group(1).strip() if m else None


def main():
    batch_id, old_path, new_path = sys.argv[1:4]
    fixes = {}
    if len(sys.argv) > 4:
        with open(sys.argv[4]) as f:
            fixes = {int(x["xbox"], 16): x for x in json.load(f)}
    old, new = load(old_path), load(new_path)
    ps2_path = sorted(p for p in os.listdir(os.path.join(HERE, "data", "snapshots")) if p.startswith("DRIVING.ELF"))[-1]
    ps2 = load(os.path.join(HERE, "data", "snapshots", ps2_path))
    ps2_by_q = collections.defaultdict(list)
    for a, f in ps2.items():
        ps2_by_q[f["qualified"]].append(a)

    items, kinds = [], collections.Counter()
    for a in sorted(set(new) | set(fixes)):
        f, o = new[a], old.get(a) or {}
        now, was = f.get("plate") or "", o.get("plate") or ""
        fix = fixes.get(a)
        if now == was and fix is None:
            continue
        name = fix["name"] if fix else f["qualified"]
        it = {"xbox": f"0x{a:08x}", "expect": f["qualified"], "name": name, "rename": fix is not None,
              "create": False, "allow_duplicate": fix is None}
        if fix:
            ps2_a = fix.get("ps2")
            sym = field(now, "Symbol file name") if BEGIN in now and f["qualified"] == name else None
            it.update(plate_base=outside(was), evidence=[fix["why"]] + ([f"PS2 {ps2_a}"] if ps2_a else []))
            if sym:
                it["sheet_name"] = sym
            kinds["rename"] += 1
        elif BEGIN in was:
            it["plate_exact"] = was
            kinds["restore Xbox plate"] += 1
        elif BEGIN in now:
            cands = ps2_by_q.get(f["qualified"], [])
            where = ", ".join(f"0x{p:08x}" for p in cands) or "no PS2 function of this name"
            it.update(plate_base=outside(was), sheet_name=field(now, "Symbol file name") or f["qualified"],
                      evidence=[f"Version Tracking match (accepted by hand) to PS2 {where}"])
            if it["sheet_name"] == name:
                del it["sheet_name"]
            kinds["VT block"] += 1
        else:
            it["plate_exact"] = was
            kinds["restore (copied PS2 note)"] += 1
        items.append(it)
    path = os.path.join(HERE, "results", "batches", f"batch-{batch_id}.json")
    with open(path, "w") as f:
        json.dump({"batch": batch_id, "program": "Driving.xbe",
                   "note": "Undo Version Tracking's copied PS2 plates; hand-checked name fixes.", "items": items}, f, indent=1)
    print(f"{path}: {len(items)} items {dict(kinds)}")


if __name__ == "__main__":
    main()
