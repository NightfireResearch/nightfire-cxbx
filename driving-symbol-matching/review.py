"""Evidence for deciding one sheet row by reading function bodies.

    python review.py 3132            # one row
    python review.py --queue 0 15    # items 0..14 of data/review-queue.json

For each row: the sheet row and its neighbours (with where they are placed), the candidate PS2 function (size
against the sheet's, callers, callees, strings, decompiled body), the PS2 functions either side of it, and the
Agent Under Fire function of the same name, demangled and decompiled, when there is one. Read-only.
"""

import json
import os
import re
import sys

from lib import features_cache, ghidra_ro as g, sheet
from lib.cw_demangle import demangle
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))
_ix = _fe = _auf = None


def _load():
    global _ix, _fe, _auf
    if _ix is None:
        _ix = Index()
        _fe = features_cache.load(g.PS2)
        _auf = {}
        with open(os.path.join(HERE, "data", "auf-functions.json")) as f:
            for a, n in json.load(f):
                d = demangle(n)
                key = (d[0] if d else n).replace(" ", "_")
                _auf.setdefault(key, []).append((a, n, d))
    return _ix, _fe, _auf


def body(program, address, lines=45):
    text = g.decompile(program, address)
    keep = [l for l in text.splitlines() if l.strip() and "unaff_" not in l
            and not re.match(r"^\s+(undefined\d*|undefined8|ulong|long|uint|int|char|float|bool)\s+\w+(\s*\[\d+\])?;$", l)]
    return "\n".join(keep[:lines]) + ("\n    ..." if len(keep) > lines else "")


def packet(row_number, candidate=None, note=""):
    ix, fe, auf = _load()
    i = next(k for k, r in enumerate(ix.rows) if r["row"] == row_number)
    r = ix.rows[i]
    a = candidate if candidate is not None else r["ps2"]
    out = [f"## row {r['row']}: {r['name']}   (sheet size 0x{r['size'] or 0:x}, colour {r['colour']}) {note}", "", "Sheet neighbours:"]
    for k in range(max(0, i - 3), min(len(ix.rows), i + 4)):
        n = ix.rows[k]
        where = f"0x{n['ps2']:08x} ({n['ps2_from']})" if n["ps2"] is not None else "unplaced"
        out.append(f"  {'>' if k == i else ' '} row {n['row']} 0x{n['size'] or 0:x} {n['name'][:70]} @ {where}")
    if a is None:
        return "\n".join(out + ["", "(no candidate)"])
    addrs = ix.ps2_addrs
    k = addrs.index(a)
    size = addrs[k + 1] - a
    f = fe.get(a, {})
    out += ["", f"Candidate 0x{a:08x} [{ix.ps2[a]['qualified']}], retail size 0x{size:x}",
            f"  PS2 neighbours: " + ", ".join(f"0x{x:x} {ix.ps2[x]['qualified'][:40]} (0x{y - x:x})"
                                             for x, y in zip(addrs[k - 2:k + 3], addrs[k - 1:k + 4]) if x != a),
            f"  callers: {g.get('get_xrefs_to', program=g.PS2, address=hex(a)).replace(chr(10), ' | ')[:300]}",
            f"  callees: {sorted(set(f.get('callees', [])))[:12]}", f"  strings: {f.get('strings', [])[:6]}", "",
            body(g.PS2, a)]
    key = (sheet.complete_name(r["name"]) or sheet.base_name(r["name"])).replace(" ", "_")
    hits = auf.get(key, [])
    if hits:
        aa, mangled, d = hits[0]
        sig = f"{d[0]}({d[1]}){' const' if d and d[2] else ''}" if d else mangled
        out += ["", f"AUF namesake 0x{aa:08x} {sig}" + (f"  (+{len(hits) - 1} more)" if len(hits) > 1 else ""),
                body(g.AUF, aa, 35)]
    else:
        out += ["", "AUF: no function of this name"]
    return "\n".join(out)


if __name__ == "__main__":
    if sys.argv[1] == "--queue":
        with open(os.path.join(HERE, "data", "review-queue.json")) as f:
            q = json.load(f)
        lo, hi = int(sys.argv[2]), int(sys.argv[3])
        for item in q[lo:hi]:
            print(packet(item["row"], int(item["ps2"], 16), f"[{item['kind']}, margin {item['margin']}]"))
            print("\n" + "-" * 100 + "\n")
    else:
        print(packet(int(sys.argv[1])))
