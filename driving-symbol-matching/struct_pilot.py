"""Build results/structs/<name>.json for ghidra/NightfireStructs.py from a layout file (a sub-agent's derived
layout, results/struct-pilot-<name>.json) and the PS2 sheet's signatures for the class's methods. Read-only.

    python struct_pilot.py rsceneobj RSceneObj

Types: the classes (a subclass gets its base as a first field named super_<Base>) and a <Class>_vtbl struct of
named slots. Prototypes: every named Xbox method <Class>::<m> whose sheet signature is complete (not cut at 63
characters) and unambiguous (one sheet row for the name); parameter types from the sheet (references become
pointers, "unsigned int" -> uint, unknown enum types -> int). A method that reads ECX before writing it, sits in a
vtable, or is listed in FORCE_MEMBER is a __thiscall member (its 'this' comes from the class namespace); the rest are
static members, __cdecl.
"""

import collections
import json
import os
import re
import sys

from lib import ghidra_ro as g
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))
FORCE_MEMBER = {"RenderEffect", "ResolveData", "ResolveObjectData", "GetRenderOffset"}   # members whose first
# instructions don't touch ECX (a jump to a shared body, or ECX kept for later)
KNOWN = {"char", "bool", "float", "int", "uint", "ushort", "byte", "MATRIX4", "COORD3", "COORD4", "CARP::MapNode",
         "CARP::Instance", "CARP::Effect", "CARP::BaseDesc", "UGroup", "RSceneObj", "CachedDrawInfo", "PhysicsObject",
         "RCARPFile", "WTargetable"}


def reads_ecx_first(address):
    for line in g.get("disassemble_function", program=g.XBOX, address=hex(address)).splitlines()[:40]:
        ins = line.split(":", 1)[1].strip() if ":" in line else ""
        if not ins:
            continue
        op, _, args = ins.partition(" ")
        dst, _, src = args.partition(",")
        if "ECX" in src or ("ECX" in dst and ("[" in dst or op in ("PUSH", "TEST", "CMP"))):
            return True
        if dst.strip() == "ECX" and op in ("MOV", "LEA", "POP", "XOR", "MOVZX", "MOVSX"):
            return False
        if op in ("CALL", "RET", "JMP"):
            return False
    return False


def ctype(p):
    p = p.strip().replace("const ", "")
    ptr = p.count("*") + p.count("&")
    base = re.sub(r"[\s*&]+$", "", p).strip()
    base = {"unsigned int": "uint", "unsigned short": "ushort", "unsigned char": "byte"}.get(base, base)
    if base not in KNOWN:
        base = "int" if ptr == 0 else "void"
    return base + " *" * ptr if ptr else base


def main():
    key, cls = sys.argv[1], sys.argv[2]
    with open(os.path.join(HERE, "results", f"struct-pilot-{key}.json")) as f:
        lay = json.load(f)
    types = []
    for c in lay["classes"]:
        fields = []
        if c.get("base"):
            fields.append({"offset": 0, "type": c["base"], "name": f"super_{c['base']}"})
        for fl in c["fields"]:
            t = fl["type"].replace("const ", "")
            if "(bitfield)" in t:
                t = "byte"
            fields.append({"offset": int(fl["offset"], 16), "type": t, "name": fl["name"],
                           "comment": f"{fl['confidence']}: {fl['evidence']}"[:250]})
        types.append({"name": c["name"], "size": c["size"], "fields": fields,
                      "comment": f"Derived by the struct pilot (results/struct-pilot-{key}.json)"})
    slots = [s for s in lay["vtable_slots"] if s["class"] == cls]
    if slots:
        vt = [{"offset": 4 * s["slot"], "type": "void *", "name": re.sub(r"\W.*", "", s["name"].split(" ")[0])}
              for s in sorted(slots, key=lambda s: s["slot"])]
        types.insert(0, {"name": f"{cls}_vtbl", "size": 4 * (max(s["slot"] for s in slots) + 1), "fields": vt})

    rows = collections.defaultdict(list)
    for r in Index().rows:
        if r["name"].startswith(cls + "::"):
            rows[re.sub(r"\(.*", "", r["name"])].append(r)
    q = g.qualified_names(g.XBOX)
    xn = {a: q.get(a, n) for a, n in g.functions(g.XBOX)}
    protos, skipped = [], collections.Counter()
    virtual = {int(s["function"], 16) for s in lay["vtable_slots"]}
    cur_sig = {}
    skip = {int(x, 16) for x in lay.get("skip_prototypes", [])}
    for a, n in sorted(xn.items()):
        if not n.startswith(cls + "::") or a in skip:
            continue
        rs = rows.get(n, [])
        if n.endswith("::scalar_deleting_destructor"):
            protos.append({"address": f"0x{a:08x}", "calling_convention": "__thiscall", "return": f"{cls} *",
                           "params": [{"name": "flags", "type": "uint"}]})
            continue
        if len(rs) != 1:
            skipped["no or several sheet rows"] += 1
            continue
        r = rs[0]
        if r["truncated"]:
            skipped["sheet signature cut"] += 1
            continue
        m = re.search(r"\((.*)\)", r["name"])
        params = [x for x in m.group(1).split(",") if x.strip() and x.strip() != "void"] if m else []
        member = reads_ecx_first(a) or a in virtual or n.split("::")[-1] in FORCE_MEMBER
        protos.append({"address": f"0x{a:08x}", "calling_convention": "__thiscall" if member else "__cdecl",
                       "params": [{"name": f"param_{i + 1}", "type": ctype(p)} for i, p in enumerate(params)],
                       "sheet": r["name"]})
    out = os.path.join(HERE, "results", "structs")
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, f"{key}.json")
    with open(path, "w") as f:
        json.dump({"program": "Driving.xbe", "types": types, "prototypes": protos}, f, indent=1)
    print(f"{path}: {len(types)} types, {len(protos)} prototypes "
          f"({sum(p['calling_convention'] == '__thiscall' for p in protos)} __thiscall); skipped {dict(skipped)}")


if __name__ == "__main__":
    main()
