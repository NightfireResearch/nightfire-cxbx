"""Names for MSVC's dynamic initializers of globals (and the destructors they register with atexit) in Driving.xbe.
Read-only; writes a batch for apply.py.

    python xbox_init.py <batch id>

The C runtime's initializer table (called by _cinit) sits at the start of .data; every unnamed function at the
end of .text that is referenced only from there is a dynamic initializer: it assigns a global, constructs a global
object, or fills a global array. Named after the first global it assigns or takes the address of, as the
decompiler spells it (DAT_<address> while the global is still unnamed):
    <global>_dynamic_initializer
    <global>_dynamic_atexit_destructor      (the function it passes to atexit, if unnamed)
"""

import collections
import json
import os
import re
import sys

from lib import ghidra_ro as g

HERE = os.path.dirname(os.path.abspath(__file__))
TAIL = (0x1503b0, 0x15d370)
TABLE = (0x1b3000, 0x1b5000)   # the __xc_a .. __xc_z initializer table and its neighbours at the start of .data


def unnamed(n):
    return n is None or n.split("::")[-1].startswith("FUN_")


def global_of(text):
    body = text.split("{", 1)[1] if "{" in text else text
    for pat in (r"&\s*_?([A-Za-z_][\w:]*)", r"^\s*_?([A-Za-z_][\w:]*)(?:\[[^\]]*\])?\s*=(?!=)"):
        for m in re.finditer(pat, body, re.M):
            name = m.group(1)
            if re.match(r"(uVar|iVar|puVar|piVar|pcVar|fVar|dVar|lVar|bVar|cVar|local_|param_|in_|unaff_|extraout)", name):
                continue
            if name in ("return", "if", "while", "do", "this"):
                continue
            return name
    return None


def main():
    batch_id = sys.argv[1]
    q = g.qualified_names(g.XBOX)
    xn = {a: q.get(a, n) for a, n in g.functions(g.XBOX)}
    xnames = set(xn.values())
    items, skipped = [], collections.Counter()
    for a in sorted(a for a in xn if TAIL[0] <= a < TAIL[1] and unnamed(xn[a])):
        refs = [int(m.group(1), 16) for m in re.finditer(r"From ([0-9a-f]{8})", g.get("get_xrefs_to", program=g.XBOX, address=hex(a)))]
        if not refs or not all(TABLE[0] <= r < TABLE[1] for r in refs):
            continue
        text = g.decompile(g.XBOX, a)
        glob = global_of(text)
        if not glob:
            skipped["no global found"] += 1
            continue
        base = glob.replace("::", "__")
        name = f"{base}_dynamic_initializer"
        if name in xnames:
            name += f"_{a:x}"
        xnames.add(name)
        items.append({"xbox": f"0x{a:08x}", "expect": xn[a], "name": name, "create": False, "allow_duplicate": False,
                      "evidence": [f"MSVC dynamic initializer: listed in the C runtime's initializer table (from {refs[0]:#x}, "
                                   f"called by _cinit); initializes {glob}"]})
        for m in re.finditer(r"atexit\((?:\(\w+ \*\))?\s*(FUN_([0-9a-f]{8}))\)", text):
            d = int(m.group(2), 16)
            if d in xn and unnamed(xn[d]):
                dn = f"{base}_dynamic_atexit_destructor"
                if dn in xnames:
                    dn += f"_{d:x}"
                xnames.add(dn)
                items.append({"xbox": f"0x{d:08x}", "expect": xn[d], "name": dn, "create": False, "allow_duplicate": False,
                              "evidence": [f"registered with atexit by {name} ({a:#x}): destroys {glob} at exit"]})
    path = os.path.join(HERE, "results", "batches", f"batch-{batch_id}.json")
    with open(path, "w") as f:
        json.dump({"batch": batch_id, "program": "Driving.xbe",
                   "note": "MSVC dynamic initializers of globals and their atexit destructors (xbox_init.py).", "items": items}, f, indent=1)
    print(f"{path}: {len(items)} items ({sum('atexit' in i['name'] for i in items)} atexit destructors); skipped {dict(skipped)}")


if __name__ == "__main__":
    main()
