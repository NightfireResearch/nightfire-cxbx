"""Names for MSVC's compiler-generated exception-handling code at the end of Driving.xbe's .text. Read-only; writes a
batch for apply.py.

    python xbox_eh.py <batch id>     -> results/batches/batch-<id>.json

Each C++ function with objects to destroy on unwind gets:
  - a frame handler: `MOV EAX, <FuncInfo>; JMP __CxxFrameHandler`, whose address the owner's prologue pushes;
  - a FuncInfo (magic 0x1993052x, maxState, pUnwindMap, ...) whose unwind map is maxState entries of
    {toState, action}; each non-null action is an unwind funclet (destroy one local, then return).
Names (human-readable, after the one already in the database, AUltraLite__AUltraLite_Frame_Handler):
    <Owner>_Frame_Handler         the owner's qualified name with "::" as "__"
    <Owner>_Unwind_<state>        the funclet for that unwind state
Handlers the linker shares between several owners (identical FuncInfo) take the first owner by address and list
the rest in the plate comment. Owners that are still unnamed are skipped (re-run after more names).
"""

import collections
import json
import os
import re
import struct
import sys

from lib import ghidra_ro as g

HERE = os.path.dirname(os.path.abspath(__file__))
TAIL = (0x1503b0, 0x15d370)
CXX_FRAME_HANDLER = 0x131dc7


def read(address, length):
    d = g.get_json("read_memory", program=g.XBOX, address=f"0x{address:x}", length=length)
    return bytes(d["data"]) if "data" in d else bytes.fromhex(d["hex"])


def unnamed(n):
    return n is None or n.split("::")[-1].startswith("FUN_")


def main():
    batch_id = sys.argv[1]
    q = g.qualified_names(g.XBOX)
    xn = {a: q.get(a, n) for a, n in g.functions(g.XBOX)}
    starts = sorted(xn)
    import bisect

    def owner_of(addr):
        k = bisect.bisect_right(starts, addr) - 1
        return starts[k] if k >= 0 else None

    items, skipped = [], collections.Counter()
    named_funclets = {}
    for h in [a for a in starts if TAIL[0] <= a < TAIL[1]]:
        dis = [l.split(":", 1)[1].strip() for l in g.get("disassemble_function", program=g.XBOX, address=hex(h)).splitlines() if ":" in l]
        if len(dis) != 2 or not dis[0].startswith("MOV EAX,0x") or not dis[1].startswith(f"JMP 0x{CXX_FRAME_HANDLER:08x}"):
            continue
        funcinfo = int(dis[0].split(",")[1], 16)
        refs = [int(m.group(1), 16) for m in re.finditer(r"From ([0-9a-f]{8})", g.get("get_xrefs_to", program=g.XBOX, address=hex(h)))]
        owners = sorted({owner_of(r) for r in refs if r < TAIL[0]} - {None})
        if not owners:
            skipped["handler with no owner"] += 1
            continue
        named = [o for o in owners if not unnamed(xn[o])]
        if not named:
            skipped["owner unnamed"] += 1
            continue
        o = named[0]
        base = xn[o].replace("::", "__").replace(" ", "_")
        others = [xn[x] for x in owners if x != o]
        magic, max_state, p_unwind = struct.unpack("<III", read(funcinfo, 12))
        if magic & 0xfffffff0 != 0x19930520 or not 0 < max_state < 256:
            skipped["bad FuncInfo"] += 1
            continue
        if unnamed(xn[h]):
            items.append({"xbox": f"0x{h:08x}", "expect": xn[h], "name": f"{base}_Frame_Handler", "create": False,
                          "allow_duplicate": False, "also": [f"{n}_Frame_Handler".replace("::", "__") for n in others],
                          "evidence": [f"MSVC C++ exception frame handler: MOV EAX, FuncInfo {funcinfo:#x}; JMP __CxxFrameHandler; "
                                       f"installed by {xn[o]} ({o:#x})" + (f" and, linker-shared, by {', '.join(others)}" if others else "")]})
        umap = read(p_unwind, 8 * max_state)
        for state in range(max_state):
            _to, action = struct.unpack_from("<iI", umap, 8 * state)
            if not action or action in named_funclets or not unnamed(xn.get(action)):
                continue
            if action not in xn:
                skipped["funclet is not a function"] += 1
                continue
            named_funclets[action] = True
            items.append({"xbox": f"0x{action:08x}", "expect": xn[action], "name": f"{base}_Unwind_{state}", "create": False,
                          "allow_duplicate": False,
                          "evidence": [f"MSVC unwind funclet: entry {state} of the unwind map of {xn[o]}'s FuncInfo {funcinfo:#x} "
                                       f"(frame handler {h:#x}); destroys a local when unwinding from state {state}"]})
    # A name the linker shares (one funclet in two owners' maps) was taken once; same-name clashes get the address.
    seen = collections.Counter(it["name"] for it in items)
    for it in items:
        if seen[it["name"]] > 1:
            it["name"] += f"_{int(it['xbox'], 16):x}"
    path = os.path.join(HERE, "results", "batches", f"batch-{batch_id}.json")
    with open(path, "w") as f:
        json.dump({"batch": batch_id, "program": "Driving.xbe",
                   "note": "Compiler-generated exception handling: frame handlers and unwind funclets (xbox_eh.py).",
                   "items": items}, f, indent=1)
    print(f"{path}: {len(items)} items "
          f"({sum(i['name'].endswith('_Frame_Handler') for i in items)} handlers); skipped {dict(skipped)}")


if __name__ == "__main__":
    main()
