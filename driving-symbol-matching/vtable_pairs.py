"""Line a class's PS2 vtable up with its Xbox vtable, slot by slot.

    python vtable_pairs.py AICharacterBond [xbox_vtable]   -> results/vtable-AICharacterBond.md

PS2 (GCC 2.95): 8-byte entries {0, pfn}; entry 0 is the class's type_info function, virtuals start at 1.
Xbox (MSVC):    4-byte pointers; virtuals start at 0.
The PS2 vtable address comes from the sheet ("<Class> virtual table"); the Xbox one from
tools/vtables_driving.json unless given. Read-only.
"""

import json
import os
import struct
import sys

from lib import ghidra_ro as g
from lib.index import Index, body_size

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def read(program, address, length):
    return bytes.fromhex(g.get_json("read_memory", program=program, address=f"0x{address:x}", length=length)["hex"])


def slots(program, address, stride, first, funcs, limit=128):
    raw = read(program, address, stride * limit)
    out = []
    for i in range(limit):
        entry = raw[i * stride:(i + 1) * stride]
        if stride == 8:
            if struct.unpack_from("<I", entry, 0)[0] != 0:
                break
            p = struct.unpack_from("<I", entry, 4)[0]
        else:
            p = struct.unpack_from("<I", entry, 0)[0]
        if p not in funcs and not (min(funcs) <= p <= max(funcs)):
            break  # past the end: not a pointer into code
        out.append(p)
    return out[first:]


def main():
    cls = sys.argv[1]
    ix = Index()
    ps2_vt = next(r["ps2"] for r in ix.rows if r["name"] == f"{cls} virtual table")
    if len(sys.argv) > 2:
        xb_vt = int(sys.argv[2], 16)
    else:
        with open(os.path.join(ROOT, "tools", "vtables_driving.json")) as f:
            xb_vt = int(json.load(f)["classes"][cls]["own"][0], 16)

    ps2 = slots(g.PS2, ps2_vt, 8, 1, ix.ps2)
    xb = slots(g.XBOX, xb_vt, 4, 0, ix.xbox)
    out = [f"# {cls}: PS2 vtable 0x{ps2_vt:08x} ({len(ps2)} slots), Xbox 0x{xb_vt:08x} ({len(xb)} slots)", "",
           "| slot | PS2 | PS2 name | PS2 size | Xbox | Xbox name | Xbox size |", "|---|---|---|---|---|---|---|"]
    for i in range(max(len(ps2), len(xb))):
        p = ps2[i] if i < len(ps2) else None
        x = xb[i] if i < len(xb) else None
        pf, xf = ix.ps2.get(p), ix.xbox.get(x)
        out.append("| {} | {} | {} | {} | {} | {} | {} |".format(
            i,
            f"{p:08x}" if p else "", pf["qualified"] if pf else ("(not a function in Ghidra)" if p else ""), f"{body_size(pf):x}" if pf else "",
            f"{x:08x}" if x else "", xf["qualified"] if xf else ("(not a function in Ghidra)" if x else ""), f"{body_size(xf):x}" if xf else ""))
    path = os.path.join(HERE, "results", f"vtable-{cls}.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(path)


if __name__ == "__main__":
    main()
