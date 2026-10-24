"""Side-by-side evidence for one Xbox address range and the sheet rows its named functions point at.

    python region.py 0x59900 0x5bb00 [margin]   -> results/region-00059900-0005bb00.md

The sheet window runs from the first to the last row an already-named Xbox function in the range maps to,
widened by `margin` rows (default 6) each side. Read-only: nothing is written to Ghidra.
"""

import os
import sys

from lib import ghidra_ro as g
from lib.index import Index, body_size

HERE = os.path.dirname(os.path.abspath(__file__))


def fmt_features(program, address):
    try:
        f = g.features(program, address)
    except Exception as e:  # a PS2 address with no function, say
        return f"({e.__class__.__name__})"
    parts = [f"{f.get('instruction_count', '?')}i/{f.get('basic_block_count', '?')}b"]
    callees = [c for c in f.get("callee_names", [])]
    if callees:
        parts.append("calls " + ", ".join(callees[:8]) + (" ..." if len(callees) > 8 else ""))
    strings = f.get("string_constants", [])
    if strings:
        parts.append("str " + "; ".join(repr(s)[:40] for s in strings[:4]))
    return " | ".join(parts)


def main():
    lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
    margin = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    ix = Index()

    xs = [a for a in ix.xbox_addrs if lo <= a < hi]
    anchor_rows = [ix.xbox_row[a] for a in xs if a in ix.xbox_row]
    out = [f"# Region 0x{lo:08x}-0x{hi:08x}", ""]

    out += ["## Xbox", "", "| address | size | name | sheet row | evidence |", "|---|---|---|---|---|"]
    for a in xs:
        f = ix.xbox[a]
        row = ix.rows[ix.xbox_row[a]]["row"] if a in ix.xbox_row else ""
        out.append(f"| {a:08x} | {body_size(f) or '?':x} | {f['qualified']} | {row} | {fmt_features(g.XBOX, a)} |"
                   if isinstance(body_size(f), int) else
                   f"| {a:08x} | ? | {f['qualified']} | {row} | {fmt_features(g.XBOX, a)} |")

    out += ["", "## Sheet window", ""]
    if not anchor_rows:
        out.append("(no named Xbox function in this range maps to a sheet row)")
    else:
        first, last = max(0, min(anchor_rows) - margin), min(len(ix.rows) - 1, max(anchor_rows) + margin)
        mapped = {i: a for a, i in ix.xbox_row.items()}
        out += ["| row | colour | PS2 | from | sheet size | name | on Xbox | PS2 evidence |",
                "|---|---|---|---|---|---|---|---|"]
        for i in range(first, last + 1):
            r = ix.rows[i]
            ps2 = f"{r['ps2']:08x}" if r["ps2"] is not None else ""
            xb = f"{mapped[i]:08x}" if i in mapped else ""
            ev = fmt_features(g.PS2, r["ps2"]) if r["ps2"] is not None else ""
            size = f"{r['size']:x}" if r.get("size") is not None else ""
            name = r["name"].replace("|", "/") + (" ..." if r["truncated"] else "")
            out.append(f"| {r['row']} | {r['colour'] or ''} | {ps2} | {r['ps2_from'] or ''} | {size} | {name} | {xb} | {ev} |")

    os.makedirs(os.path.join(HERE, "results"), exist_ok=True)
    path = os.path.join(HERE, "results", f"region-{lo:08x}-{hi:08x}.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(path)


if __name__ == "__main__":
    main()
