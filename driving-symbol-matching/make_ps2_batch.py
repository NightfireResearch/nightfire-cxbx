"""Build a DRIVING.ELF batch from sheet rows the index has placed, for apply.py.

    python make_ps2_batch.py P003 infill-exact infill-count     -> results/batches/batch-P003.json (+ -held.md)

Takes rows whose PS2 address came from the given sources, where PS2 Ghidra still has FUN_ there (a row whose
function is already named is never touched). Names are made Ghidra-safe the way PS2 Ghidra already spells them:
"operator new" -> operator_new, "operator delete []" -> operator_delete_array, "X type_info function" ->
X_type_info_function. Held back and listed: names the sheet cut short, templates, static initialisers, and
thunks. A name the sheet gives to several rows (overloads) may repeat. Read-only.
"""

import json
import os
import re
import sys
from collections import Counter

from lib import sheet
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))


def ghidra_name(row_name):
    """(qualified Ghidra-safe name, None) or (None, reason to hold)."""
    name = row_name
    if name.startswith(("global constructors keyed to", "global destructors keyed to")):
        return None, "static initialiser (no naming convention agreed yet)"
    if name.endswith(" type_info function"):
        cls = name[:-len(" type_info function")]
        if "<" in cls:
            return None, "template"
        return cls + "_type_info_function", None
    full = sheet.complete_name(name)
    if full is None:
        return None, "name cut short by the sheet's 63 characters"
    if "<" in full:
        return None, "template"
    full = re.sub(r"operator (new|delete) \[\]$", r"operator_\1_array", full)
    full = re.sub(r"operator (new|delete)$", r"operator_\1", full)
    full = re.sub(r"operator\s+(\S+)$", r"operator\1", full)
    if " " in full:
        return None, "name has spaces Ghidra can't take"
    return full, None


def main():
    batch_id, sources = sys.argv[1], set(sys.argv[2:])
    ix = Index()
    base_counts = Counter(sheet.base_name(r["name"]) for r in ix.rows)
    items, held = [], []
    for r in ix.rows:
        if r.get("ps2_from") not in sources:
            continue
        a = r["ps2"]
        f = ix.ps2[a]
        if not f["name"].startswith("FUN_"):
            continue
        if f.get("thunk"):
            held.append((r, a, "thunk"))
            continue
        name, why = ghidra_name(r["name"])
        if name is None:
            held.append((r, a, why))
            continue
        items.append({"xbox": f"0x{a:08x}", "expect": f["name"], "name": name, "sheet_name": r["name"],
                      "create": False, "allow_duplicate": base_counts[sheet.base_name(r["name"])] > 1,
                      "evidence": [f"sheet row {r['row']}, placed by {r['ps2_from']}"]})
    path = os.path.join(HERE, "results", "batches", f"batch-{batch_id}.json")
    with open(path, "w") as f:
        json.dump({"batch": batch_id, "program": "DRIVING.ELF",
                   "note": f"Sheet names for PS2 functions placed by {', '.join(sorted(sources))}. 'xbox' is the PS2 address.",
                   "items": items}, f, indent=1)
    lines = [f"# Batch {batch_id}: held back", "", "| row | sheet name | PS2 | why |", "|---|---|---|---|"]
    lines += [f"| {r['row']} | {r['name'].replace('|', '/')} | {a:08x} | {why} |" for r, a, why in held]
    with open(path.replace(".json", "-held.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{path}: {len(items)} items; held {len(held)}: {dict(Counter(w for _, _, w in held))}")


if __name__ == "__main__":
    main()
