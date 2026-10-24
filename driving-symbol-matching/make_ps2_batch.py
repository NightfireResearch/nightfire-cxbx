"""Build a DRIVING.ELF batch from sheet rows the index has placed, for apply.py.

    python make_ps2_batch.py P003 infill-exact infill-count     -> results/batches/batch-P003.json (+ -held.md)

Takes rows whose PS2 address came from the given sources, where PS2 Ghidra still has FUN_ there (a row whose
function is already named is never touched). Names are made Ghidra-safe the way PS2 Ghidra already spells them:
"operator new" -> operator_new, "operator delete []" -> operator_delete_array, "X type_info function" ->
X_type_info_function, "global constructors keyed to K" -> K_global_ctors, templates keep their arguments with
spaces as underscores (the user's conventions, 25 Sept 2026). Held back and listed: names the sheet cut inside
the name itself (flag for AUF or a manual check) and thunks. A name the sheet gives to several rows (overloads) may repeat. Read-only.
"""

import json
import os
import re
import sys
from collections import Counter

from lib import sheet
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))


def ghidra_safe(name):
    """Spaces become underscores, as Ghidra's demangler writes template arguments."""
    return re.sub(r"\s+", "_", name.strip())


def complete_cut(name, truncated):
    """The part before any argument list, or None when the sheet's 63-character cut fell inside the name and
    the constructor/destructor rule can't complete it."""
    if not truncated or "(" in name:
        return sheet.base_name(name)
    return sheet.complete_name(name, truncated=True)


def ghidra_name(row_name, truncated=None):
    """(qualified Ghidra-safe name, None) or (None, reason to hold)."""
    name = row_name
    truncated = len(name) >= 63 if truncated is None else truncated
    m = re.match(r"^global (constructors|destructors) keyed to (.*)$", name)
    if m:
        # Convention (user, 25 Sept 2026): "<key>_global_ctors" / "_global_dtors" in the key's namespace,
        # like the existing EAGL::DynamicLoader::ModelType_global_ctors.
        key = complete_cut(m.group(2), truncated)
        if key is None:
            return None, "name cut short by the sheet's 63 characters"
        return ghidra_safe(key) + ("_global_ctors" if m.group(1) == "constructors" else "_global_dtors"), None
    if name.endswith(" type_info function"):
        return ghidra_safe(name[:-len(" type_info function")]) + "_type_info_function", None
    full = sheet.complete_name(name)
    if full is None:
        return None, "name cut short by the sheet's 63 characters"
    # A template function carries its return type in the symbol file: "bool lexicographical_compare<...>".
    full = re.sub(r"^(?:[\w:]+\s*\**\s+)+(?=[\w:~]+<)", "", full)
    full = re.sub(r"operator (new|delete) \[\]$", r"operator_\1_array", full)
    full = re.sub(r"operator (new|delete)$", r"operator_\1", full)
    full = re.sub(r"operator\s+(\S+)$", r"operator\1", full)
    # Convention (user, 25 Sept 2026): templates keep their arguments, spaces as underscores.
    return ghidra_safe(full), None


def main():
    batch_id, sources = sys.argv[1], set(sys.argv[2:])
    ix = Index()
    completions = {}
    path = os.path.join(HERE, "results", "cut-name-completions.json")
    if os.path.exists(path):   # complete_cut.py: names the sheet cut, completed where not in doubt
        with open(path) as f:
            completions = {c["row"]: c for c in json.load(f)}
    base_counts = Counter(sheet.base_name(r["name"]) for r in ix.rows)
    items, held = [], []
    for r in ix.rows:
        if r.get("ps2_from") not in sources:
            continue
        a = r["ps2"]
        f = ix.ps2.get(a)
        if f is None:
            continue  # a data symbol (a variable): the sheet's own address, not a function

        if not f["name"].startswith("FUN_"):
            continue
        if f.get("thunk"):
            held.append((r, a, "thunk"))
            continue
        done = completions.get(r["row"])
        name, why = ghidra_name(done["complete"], False) if done else ghidra_name(r["name"], r["truncated"])
        if name is None:
            held.append((r, a, why))
            continue
        items.append({"xbox": f"0x{a:08x}", "expect": f["name"], "name": name, "sheet_name": r["name"],
                      "create": False, "allow_duplicate": base_counts[sheet.base_name(r["name"])] > 1,
                      "evidence": [f"sheet row {r['row']}, placed by {r['ps2_from']}"]
                                  + ([f"name cut at 63 characters, completed by rule '{done['rule']}'"] if done else [])})
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
