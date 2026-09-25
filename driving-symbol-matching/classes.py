"""Which Ghidra namespaces are C++ classes, from the symbol file, for ghidra/NightfireClasses.py to convert.

    python classes.py        -> results/class-namespaces.json, results/class-namespaces.md

A namespace path P is a class when the sheet has any of: "P virtual table", "P type_info function/node", a
constructor P::X or destructor P::~X (X = P's last part), or a const method "P::f(...) const". It is a plain
namespace (EAGL, EAGLAnim, CARP) when it only ever prefixes other names and has none of those. Anything else is
undecided and left alone. Only namespaces that exist in the program and aren't classes already are listed.
Read-only.
"""

import json
import os
import re
from collections import defaultdict

from lib import ghidra_ro as g, sheet
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))


def evidence_from_sheet(rows):
    ev = defaultdict(set)
    for r in rows:
        name = r["name"]
        for suffix, why in ((" virtual table", "vtable"), (" type_info function", "type_info"),
                            (" type_info node", "type_info")):
            if name.endswith(suffix):
                ev[name[:-len(suffix)].split("::", 1)[-1] if name.startswith("RShadowMap::") else name[:-len(suffix)]].add(why)
        full = sheet.complete_name(name)
        if not full or "::" not in full or "<" in full:
            continue
        path, meth = full.rsplit("::", 1)
        last = path.split("::")[-1]
        if meth == last:
            ev[path].add("constructor")
        elif meth == "~" + last:
            ev[path].add("destructor")
        elif re.search(r"\)\s*const\s*$", name):
            ev[path].add("const method")
        else:
            ev[path].add("member")
        parts = path.split("::")
        for k in range(1, len(parts)):
            ev["::".join(parts[:k])].add("prefix")
    return ev


def census(program):
    """Full paths of the program's classes, from the census ghidra/NightfireClasses.py writes, or None."""
    path = os.path.join(HERE, "results", f"namespace-census-{program}.json")
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return set(json.load(f)["classes"])


def main():
    ix = Index()
    ev = evidence_from_sheet(ix.rows)
    strong = {"vtable", "type_info", "constructor", "destructor", "const method"}
    out, report = {}, []
    for program in (g.XBOX, g.PS2):
        paths = sorted({n.rsplit("::", 1)[0] for n in g.qualified_names(program).values() if "::" in n})
        convert = []
        for p in paths:
            e = ev.get(p, set())
            kind = "class" if e & strong else ("namespace" if e == {"prefix"} else "undecided")
            if kind == "class":
                # The MCP server only sees top-level classes; the script's census sees nested ones too.
                known = census(program)
                if known is not None:
                    is_class = p in known
                else:
                    info = g.get_json("list_class_members", program=program, class_name=p.split("::")[-1], limit=1)
                    is_class = "::" not in p and info.get("class_namespace_exists")
                if is_class:
                    kind = "class (already)"
                else:
                    convert.append({"path": p, "evidence": sorted(e & strong)})
            report.append((program, p, kind, sorted(e)))
        out[program] = convert
    with open(os.path.join(HERE, "results", "class-namespaces.json"), "w") as f:
        json.dump(out, f, indent=1)
    lines = ["# Namespaces: class or not", "",
             "From the symbol file. `class` ones are converted by ghidra/NightfireClasses.py; `undecided` ones are left.", ""]
    for program in (g.XBOX, g.PS2):
        rows = [r for r in report if r[0] == program]
        counts = defaultdict(int)
        for r in rows:
            counts[r[2]] += 1
        lines += [f"## {program}: " + ", ".join(f"{k} {n}" for k, n in sorted(counts.items())), "",
                  "| namespace | kind | sheet evidence |", "|---|---|---|"]
        lines += [f"| {p} | {k} | {', '.join(e)} |" for _, p, k, e in rows if k != "class"] + [""]
    with open(os.path.join(HERE, "results", "class-namespaces.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print({p: len(v) for p, v in out.items()}, "to convert")
    for program in (g.XBOX, g.PS2):
        print(program, dict((k, sum(1 for r in report if r[0] == program and r[2] == k)) for k in
                            ("class", "class (already)", "namespace", "undecided")))


if __name__ == "__main__":
    main()
