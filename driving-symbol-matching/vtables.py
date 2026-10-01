"""Find every vtable on both platforms and pair PS2 vtables with Xbox ones.

    python vtables.py        -> data/vtables.json, results/vtables.md

Xbox (MSVC): a vtable is a run of code pointers in .rdata whose first word's address is stored by code (the
constructor's `mov [reg], imm32`). The run is split wherever another stored address begins.
PS2 (GCC 2.95): the sheet names them ("<Class> virtual table"). Entries are 8 bytes {0, pfn}; entry 0 must be
the class's type_info function, or the address is rejected.

Pairing: same slot count, then the slots whose functions are already matched (an Xbox function carrying a
sheet name whose PS2 address is known) must agree, and "empty" slots (PS2 <= 8 bytes, Xbox <= 3) must line up.
Read-only.
"""

import bisect
import json
import os
import re
from collections import defaultdict

from lib import ghidra_ro as g
from lib.image import Image, SECTIONS
from lib.index import Index, body_size

HERE = os.path.dirname(os.path.abspath(__file__))


def xbox_vtables(img, funcs):
    text_lo, text_hi = SECTIONS[g.XBOX][".text"]
    lo, data = img.sections[".rdata"]
    is_code = lambda v: text_lo <= v < text_hi and v in funcs
    runs = []  # (start, end) of code-pointer runs, 4-aligned
    p = (lo + 3) & ~3
    while p + 4 <= lo + len(data):
        if is_code(img.u32(p)):
            q = p
            while q + 4 <= lo + len(data) and (is_code(img.u32(q)) or text_lo <= img.u32(q) < text_hi):
                q += 4
            runs.append((p, q))
            p = q
        else:
            p += 4
    # Addresses stored by `mov r/m32, imm32` (C7 /0) anywhere in .text.
    starts = defaultdict(list)  # vtable address -> addresses of the instructions storing it
    tlo, tdata = img.sections[".text"]
    run_starts = [r[0] for r in runs]
    for m in re.finditer(rb"\xC7", tdata):
        i = m.start()
        modrm = tdata[i + 1] if i + 1 < len(tdata) else 0
        mod, rm = modrm >> 6, modrm & 7
        if (modrm >> 3) & 7:
            continue
        disp = {0: 0, 1: 1, 2: 4}.get(mod)
        if disp is None:
            continue
        if mod == 0 and rm == 5:
            disp = 4
        sib = 1 if rm == 4 else 0
        at = i + 2 + sib + disp
        if at + 4 > len(tdata):
            continue
        v = int.from_bytes(tdata[at:at + 4], "little")
        k = bisect.bisect_right(run_starts, v) - 1
        if k >= 0 and runs[k][0] <= v < runs[k][1]:
            starts[v].append(tlo + i)
    out = []
    for a, b in runs:
        cut = sorted([a] + [s for s in starts if a < s < b])
        for s, e in zip(cut, cut[1:] + [b]):
            out.append({"address": s, "stored": s in starts, "stores": starts.get(s, []),
                        "slots": [img.u32(x) for x in range(s, e, 4)]})
    return out


def ps2_vtables(img, ix):
    out = []
    for r in ix.rows:
        if not r["name"].endswith(" virtual table") or r["ps2"] is None:
            continue
        a = r["ps2"]
        try:
            head = img.read(a, 8)
        except ValueError:
            out.append({"name": r["name"], "row": r["row"], "address": a, "error": "not in a data section"})
            continue
        ti = int.from_bytes(head[4:8], "little")
        tname = ix.ps2.get(ti, {}).get("qualified", "")
        slots = []
        x = a + 8
        while True:
            e = img.read(x, 8)
            pfn = int.from_bytes(e[4:8], "little")
            if int.from_bytes(e[0:4], "little") != 0 or pfn not in ix.ps2:
                break
            slots.append(pfn)
            x += 8
        rec = {"name": r["name"], "row": r["row"], "address": a, "type_info": tname, "slots": slots}
        if int.from_bytes(head[0:4], "little") != 0 or "type_info" not in tname:
            rec["error"] = f"entry 0 is not a type_info function ({tname or hex(ti)})"
        out.append(rec)
    return out


def main():
    ix = Index()
    xi, pi = Image(g.XBOX), Image(g.PS2)
    xv = xbox_vtables(xi, ix.xbox)
    pv = ps2_vtables(pi, ix)

    # Known function pairs: an Xbox function carrying a sheet name whose PS2 address is known.
    ps2_to_xbox = {}
    for xa, i in ix.xbox_row.items():
        if ix.rows[i]["ps2"] is not None:
            ps2_to_xbox[ix.rows[i]["ps2"]] = xa

    import math

    starts_sorted = ix.xbox_addrs

    def owner(a):
        k = bisect.bisect_right(starts_sorted, a) - 1
        return starts_sorted[k] if k >= 0 else None

    # Class named by the Xbox functions that store the vtable (a named constructor or destructor).
    for v in xv:
        names = set()
        for site in v["stores"]:
            f = ix.xbox.get(owner(site))
            q = f["qualified"] if f else ""
            if "::" in q:
                cls, meth = q.rsplit("::", 1)
                if meth in (cls.split("::")[-1], "~" + cls.split("::")[-1]):
                    names.add(cls)
        v["class_by_ctor"] = sorted(names)

    def logsize(prog, a):
        f = prog.get(a)
        n = body_size(f) if f else None
        return math.log(max(n, 1)) if n else None

    def shape(ps, xs):
        """Correlation of slot-function sizes (log bytes): virtuals keep their relative sizes across compilers."""
        pts = [(logsize(ix.ps2, p), logsize(ix.xbox, x)) for p, x in zip(ps, xs)]
        pts = [(a, b) for a, b in pts if a is not None and b is not None]
        if len(pts) < 3:
            return 0.0
        ma = sum(a for a, _ in pts) / len(pts)
        mb = sum(b for _, b in pts) / len(pts)
        va = sum((a - ma) ** 2 for a, _ in pts)
        vb = sum((b - mb) ** 2 for _, b in pts)
        if va == 0 or vb == 0:
            return 0.0
        return sum((a - ma) * (b - mb) for a, b in pts) / math.sqrt(va * vb)

    by_len = defaultdict(list)
    for v in xv:
        by_len[len(v["slots"])].append(v)

    pairs = []
    for p in pv:
        if "error" in p:
            continue
        cls = p["name"][:-len(" virtual table")]
        best = []
        for v in by_len.get(len(p["slots"]), []):
            agree = clash = 0
            for ps, xs in zip(p["slots"], v["slots"]):
                if ps in ps2_to_xbox:
                    if ps2_to_xbox[ps] == xs:
                        agree += 1
                    else:
                        clash += 1
            named = cls in v["class_by_ctor"]
            other = bool(v["class_by_ctor"]) and not named
            r = shape(p["slots"], v["slots"])
            score = 10 * named - 10 * other + agree - 3 * clash + 2 * r
            best.append((score, agree, clash, round(r, 2), named, other, v["address"]))
        best.sort(reverse=True)
        c0 = best[0] if best else None
        margin = (best[0][0] - best[1][0]) if len(best) > 1 else 99
        if c0 is None:
            p["confidence"] = "none"
        elif c0[4] or (c0[1] >= 2 and c0[2] == 0 and margin >= 2):
            p["confidence"] = "certain"
        elif c0[1] >= 1 and c0[2] == 0 and margin >= 2 and not c0[5]:
            p["confidence"] = "probable"
        else:
            p["confidence"] = "unresolved"
        p["candidates"] = [{"xbox": f"0x{a:08x}", "score": round(sc, 2), "agree": ag, "clash": cl, "shape": r,
                            "ctor": nm, "other_ctor": ot} for sc, ag, cl, r, nm, ot, a in best[:3]]
        pairs.append(p)

    with open(os.path.join(HERE, "data", "vtables.json"), "w") as f:
        json.dump({"xbox": [{**v, "address": f"0x{v['address']:08x}", "slots": [f"0x{s:08x}" for s in v["slots"]]}
                            for v in xv],
                   "ps2": [{**p, "address": f"0x{p['address']:08x}", "slots": [f"0x{s:08x}" for s in p.get("slots", [])]}
                           for p in pv]}, f, indent=1)

    bad = [p for p in pv if "error" in p]
    lines = [f"# Vtables: {len(xv)} Xbox ({sum(v['stored'] for v in xv)} stored by code), {len(pv)} PS2 from the sheet"
             f" ({len(bad)} rejected)", ""]
    from collections import Counter
    lines += ["Confidence: " + ", ".join(f"{k} {n}" for k, n in Counter(p["confidence"] for p in pairs).most_common()), ""]
    lines += ["| PS2 vtable | row | slots | confidence | best Xbox | score | agree/clash | shape | named by ctor | runner-up score |",
              "|---|---|---|---|---|---|---|---|---|---|"]
    for p in sorted(pairs, key=lambda p: p["row"]):
        c = p["candidates"]
        b = c[0] if c else None
        r2 = c[1]["score"] if len(c) > 1 else ""
        if b:
            lines.append(f"| {p['name'][:-len(' virtual table')]} (0x{p['address']:08x}) | {p['row']} | {len(p['slots'])} | {p['confidence']} | "
                         f"{b['xbox']} | {b['score']} | {b['agree']}/{b['clash']} | {b['shape']} | "
                         f"{'yes' if b['ctor'] else ('OTHER' if b['other_ctor'] else '')} | {r2} |")
        else:
            lines.append(f"| {p['name'][:-len(' virtual table')]} (0x{p['address']:08x}) | {p['row']} | {len(p['slots'])} | "
                         f"none | (no Xbox vtable with this many slots) | | | | | |")
    lines += ["", "## Rejected PS2 addresses", ""] + [f"- row {p['row']} {p['name']} 0x{p['address']:08x}: {p['error']}" for p in bad]
    with open(os.path.join(HERE, "results", "vtables.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(lines[0])


if __name__ == "__main__":
    main()
