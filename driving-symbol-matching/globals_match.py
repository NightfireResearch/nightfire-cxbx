"""Name the PS2 symbol sheet's data symbols (globals, class statics) on DRIVING.ELF and pair them with Driving.xbe.
Read-only: writes JSON for ghidra/NightfireGlobals.py.

    python globals_match.py --refs          # cache every function's data references, both builds (slow, once)
    python globals_match.py --calibrate     # sheet address -> ELF address        results/globals/ps2-map.json
    python globals_match.py --pair          # ELF address -> Xbox address          results/globals/xbox-pairs.json
    python globals_match.py --emit KEY OBJ...   # results/globals/KEY.json (labels for both programs), for the
                                                # sheet object files given ("all" for everything that passed)

Why: the sheet's data addresses come from a slightly different link than DRIVING.ELF; the offset to the ELF is
near-constant within an object file but drifts across the binary and differs per section, so DRIVING.ELF has
almost no named data. Step 1 finds the offset per block of neighbouring symbols (the one that lands most sheet
symbols on addresses code really references) and snaps each symbol to the nearest referenced address. Step 2
takes every function named identically on both builds, lists the distinct data addresses each references in
order, and where the counts agree pairs them position by position; votes over all such pairs give the Xbox
address of each ELF symbol. Only votes whose ELF side is exactly a resolved symbol's start count.
"""

import bisect
import collections
import json
import os
import re
import sys
from concurrent.futures import ThreadPoolExecutor

from lib import ghidra_ro as g
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = os.path.join(HERE, "data", "globals-refs.json")
OUT = os.path.join(HERE, "results", "globals")
XBOX_DATA = (0x189be0, 0x24b5bc)      # .rdata + .data (with .bss)
PS2_DATA = (0x326000, 0x4279b4)       # .data .rodata .lit4 .sdata .sbss .bss
PS2_GP = 0x3cfdf0


def dis(program, a):
    return [l.split(":", 1)[1].strip() for l in g.get("disassemble_function", program=program, address=hex(a)).splitlines()
            if ":" in l]


def xbox_refs(a):
    out = []
    for s in dis(g.XBOX, a):
        if s.startswith(("CALL", "J")):
            continue
        for m in re.finditer(r"0x([0-9a-f]{5,8})", s):
            v = int(m.group(1), 16)
            if XBOX_DATA[0] <= v < XBOX_DATA[1]:
                out.append(v)
    return out


MIPS_MEM = re.compile(r"^(\w+)\s+(\w+),(-?0x[0-9a-f]+|-?\d+)\((\w+)\)$")


def ps2_refs(a):
    """Data addresses from lui/addiu and lui/load-store pairs, gp-relative accesses, and through a register
    holding a full address (base + offset)."""
    hi, full, out = {}, {}, []
    for s in dis(g.PS2, a):
        s = s.lstrip("_")
        p = re.split(r"[\s,]+", s)
        op = p[0]
        m = MIPS_MEM.match(s)
        if m:
            base, off = m.group(4), int(m.group(3), 0)
            if base == "gp":
                out.append(PS2_GP + off)
            elif base in hi:
                out.append(hi[base] + off)
            elif base in full:
                out.append(-(full[base] + off))   # interior access: negative = not a direct reference
            dst = m.group(2) if not op.startswith("s") else None
        elif op == "lui" and len(p) >= 3:
            hi[p[1]] = int(p[2], 0) << 16
            full.pop(p[1], None)
            continue
        elif op in ("addiu", "daddiu") and len(p) >= 4 and (p[2] in hi or p[2] == "gp"):
            v = (PS2_GP if p[2] == "gp" else hi[p[2]]) + int(p[3], 0)
            out.append(v)
            hi.pop(p[1], None)
            full[p[1]] = v
            continue
        else:
            dst = p[1] if len(p) > 1 and not op.startswith(("b", "j", "s", "nop")) else None
        if dst:
            hi.pop(dst, None)
            full.pop(dst, None)
        if op.startswith("jal"):
            for r in list(hi) + list(full):
                if r.startswith(("a", "v", "t")):
                    hi.pop(r, None)
                    full.pop(r, None)
    return [v for v in out if PS2_DATA[0] <= abs(v) < PS2_DATA[1]]


def build_refs():
    out = {}
    for program, fn in ((g.XBOX, xbox_refs), (g.PS2, ps2_refs)):
        addrs = [a for a, _ in g.functions(program)]
        with ThreadPoolExecutor(8) as ex:
            res = list(ex.map(lambda a: (a, fn(a)), addrs))
        out[program] = {f"{a:x}": list(dict.fromkeys(r)) for a, r in res if r}
        print(f"{program}: {len(addrs)} functions, {len(out[program])} with data references")
    with open(REFS, "w") as f:
        json.dump(out, f)


def sheet_data():
    rows = []
    for r in Index().rows:
        n = r["name"]
        if r.get("sym") and r.get("size") and "(" not in n and " " not in n:
            rows.append({"name": n, "sheet": r["sym"], "size": r["size"], "obj": r.get("obj")})
    return sorted(rows, key=lambda r: r["sheet"])



def norm(x):
    return re.sub(r"[^a-z0-9]", "", x.lower())


def anchors(refs_all, by_name_q, syms):
    """(sheet symbol, ELF address) pairs pinned by name: a PS2 function C::[Get|Set|Is|Has|Enable|Num...]X, or
    C::Get/TheApp/Instance/Init/Shutdown, that touches exactly one distinct direct data address, and a sheet
    static of C named [f|fg|k|g|s]X (or fg*/fObj/fgThis/fTheApp for the singleton getters)."""
    by_cls = collections.defaultdict(list)
    for s in syms:
        if "::" in s["name"]:
            c, leaf = s["name"].rsplit("::", 1)
            by_cls[c].append((leaf, s))
    out = {}
    single = {"Get", "TheApp", "Instance", "GetInstance", "Init", "Shutdown", "Construct", "Destruct"}
    for name, a in by_name_q.items():
        if "::" not in name:
            continue
        c, leaf = name.rsplit("::", 1)
        if c not in by_cls:
            continue
        rs = [v for v in dict.fromkeys(refs_all.get(a, [])) if v > 0 and v % 4 == 0]
        if len(rs) != 1:
            continue
        m = re.match(r"(Get|Set|Is|Has|Enable|Disable|Inc|Dec|Reset)?(.*)$", leaf)
        stem = norm(m.group(2) or "")
        hits = []
        for sleaf, s in by_cls[c]:
            sl = norm(re.sub(r"^(fg|f|k|g|s)(?=[A-Z])", "", sleaf))
            if leaf in single and (sleaf.startswith("fg") or sleaf in ("fObj", "fgThis", "fTheApp", "fInstance")):
                hits.append(s)
            elif stem and sl == stem:
                hits.append(s)
        if len(hits) == 1:
            out.setdefault(hits[0]["name"], set()).add(rs[0])
    return {n: next(iter(v)) for n, v in out.items() if len(v) == 1}


def calibrate():
    """Offsets are scored per symbol against the references of the functions that should own it (its sheet
    object file's functions and its class's methods): all-program references are too dense (strings, constants)
    and any offset scores."""
    with open(REFS) as f:
        raw = json.load(f)[g.PS2]
    # direct references only (lui pairs, gp-relative), 4-aligned (unaligned ones are mostly strings)
    refs = {int(k, 16): [v for v in lst if v > 0 and v % 4 == 0] for k, lst in raw.items()}
    R = sorted({v for lst in refs.values() for v in lst})
    q = g.qualified_names(g.PS2)
    by_name = collections.defaultdict(list)
    for a, n in g.functions(g.PS2):
        by_name[q.get(a, n)].append(a)
    obj_funcs, cls_funcs = collections.defaultdict(set), collections.defaultdict(set)
    for r in Index().rows:
        if "(" in r["name"] and r.get("obj"):
            for a in by_name.get(r["name"].split("(")[0], [])[:1]:
                obj_funcs[r["obj"]].add(a)
    for n, addrs in by_name.items():
        if "::" in n:
            cls_funcs[n.rsplit("::", 1)[0]].update(addrs)
    owner_cache = {}

    def owner_refs(sym):
        key = (sym["obj"], sym["name"].rsplit("::", 1)[0] if "::" in sym["name"] else None)
        if key not in owner_cache:
            fs = obj_funcs.get(key[0], set()) | cls_funcs.get(key[1], set())
            owner_cache[key] = sorted({v for a in fs for v in refs.get(a, [])})
        return owner_cache[key]

    def near(lst, t, tol):
        i = bisect.bisect_left(lst, t - tol)
        best = None
        while i < len(lst) and lst[i] <= t + tol:
            if best is None or abs(lst[i] - t) < abs(best - t):
                best = lst[i]
            i += 1
        return best

    Rset = set(R)

    def score(block, d):
        """+2 per symbol whose start is a (direct, aligned) reference anywhere in the program; -2 per reference in
        the block's span (widened by 0x40) that maps to no sheet symbol at all. Uniform runs of referenced
        globals are ambiguous inside; the edges decide."""
        sc = sum(2 for s in block if s["sheet"] - d in Rset)
        lo, hi = block[0]["sheet"] - d - 0x40, block[-1]["sheet"] + block[-1]["size"] - d + 0x40
        for r in R[bisect.bisect_left(R, lo):bisect.bisect_left(R, hi)]:
            a = r + d
            i = bisect.bisect_right(all_starts, a) - 1
            if not (i >= 0 and a < syms[i]["sheet"] + max(syms[i]["size"], 4)):
                sc -= 2
        return sc

    ambiguous = [False]

    def search(block, prev):
        """Shortlist: exact offsets that land at least two symbols (or one, for tiny blocks) on an owner
        reference, most first; pick by layout consistency, then hit count, then closeness to the previous
        block's offset."""
        exact = collections.Counter()
        for s in block:
            for r in owner_refs(s):
                if 0 < s["sheet"] - r < 0x300000:
                    exact[s["sheet"] - r] += 1
        if not exact:
            return None, 0
        need = 2 if len(block) > 2 else 1
        cands = [d for d, c in exact.most_common(60) if c >= need]
        if prev:
            cands += [prev + k for k in range(-0x40, 0x44, 4)]
        if not cands:
            return None, 0
        cands = set(cands)
        best = max(score(block, d) for d in cands)
        good = [d for d in cands if score(block, d) >= best - 2]
        # tie-break: symbols landing exactly on an address their own object's / class's functions reference
        own = {c: sum(1 for x in block if (x["sheet"] - c) in set(owner_refs(x))) for c in good}
        top = max(own.values())
        winners = [c for c in good if own[c] == top]
        d = min(winners, key=lambda d: (abs(d - prev) if prev else 0, -score(block, d), -exact[d]))
        ambiguous[0] = top == 0 or len({w for w in winners if abs(w - d) >= 4}) > 0
        if os.environ.get("GM_DEBUG") and any(os.environ["GM_DEBUG"] in (x["obj"] or "") for x in block):
            for c in sorted(set(cands), key=lambda c: -score(block, c))[:6]:
                print(f"  {block[0]['obj']} d={c:#x} score={score(block, c)} exact={exact[c]}")
        return d, exact[d]

    syms = sheet_data()
    all_starts = [x["sheet"] for x in syms]
    by_name_q = {n: v[0] for n, v in by_name.items() if len(v) == 1}
    anc = anchors({int(k, 16): v for k, v in raw.items()}, by_name_q, syms)
    print(f"{len(anc)} anchors from accessor names")
    blocks, cur = [], []
    for s in syms:
        if cur and (s["obj"] != cur[-1]["obj"] or s["sheet"] - cur[-1]["sheet"] > 0x800 or len(cur) >= 40):
            blocks.append(cur)
            cur = []
        cur.append(s)
    if cur:
        blocks.append(cur)

    # Pass 1: blocks with name anchors. Pass 2: the rest, choosing among the best-scoring offsets the one
    # nearest the closest anchored block's offset (offsets drift slowly with address).
    fixed = {}
    for i, block in enumerate(blocks):
        ad = collections.Counter(s["sheet"] - anc[s["name"]] for s in block if s["name"] in anc)
        if ad:
            fixed[i] = ad.most_common(1)[0][0]
    fixed_idx = sorted(fixed)

    def reference(i):
        j = bisect.bisect_left(fixed_idx, i)
        near_ = [fixed_idx[k] for k in (j - 1, j) if 0 <= k < len(fixed_idx)]
        if not near_:
            return None
        k = min(near_, key=lambda k: abs(blocks[k][0]["sheet"] - blocks[i][0]["sheet"]))
        return fixed[k] if abs(blocks[k][0]["sheet"] - blocks[i][0]["sheet"]) < 0x40000 else None

    out = []
    for i, block in enumerate(blocks):
        if i in fixed:
            d, tag, ok = fixed[i], "anchored", True
        else:
            ref = reference(i)
            d, hits = search(block, ref)
            tag = f"{hits}/{len(block)}" + (f" ref {ref:#x}" if ref else "")
            ok = d is not None and hits >= 2 and not ambiguous[0] and (ref is None or abs(d - ref) <= 0x400)
            if ambiguous[0]:
                tag += " ambiguous"
        taken = set()
        starts = {x["sheet"] - d for x in block} if d is not None else set()
        for s in block:
            e = dict(s, delta=d, block_score=tag, ps2=None, how=None)
            if ok:
                t = s["sheet"] - d
                r = near([v for v in owner_refs(s) if v == t or v not in starts], t, 4)
                if r is None and t in Rset:
                    r = t
                if r is not None and r not in taken:
                    e["ps2"], e["how"] = r, ("exact" if r == t else f"snapped {r - t:+d}")
                else:
                    e["ps2"], e["how"] = t, "predicted (no reference at its start)"
                taken.add(e["ps2"])
            out.append(e)
    # One claim per ELF address: anchored beats unanchored, exact beats snapped beats predicted
    rank = lambda e: (e["block_score"] != "anchored", not e["how"].startswith("exact"), e["how"].startswith("predicted"))
    claims = collections.defaultdict(list)
    for e in out:
        if e["how"]:
            claims[e["ps2"]].append(e)
    for lst in claims.values():
        lst.sort(key=rank)
        for e in lst[1:]:
            e["how"], e["ps2"] = f"conflict (lost {e['ps2']:#x} to {lst[0]['name']})", None
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "ps2-map.json"), "w") as f:
        json.dump(out, f, indent=0)
    c = collections.Counter((e["how"] or "block not calibrated").split(" ")[0] for e in out)
    print(f"{len(out)} sheet data symbols in {len(blocks)} blocks: {dict(c)}")


def pair():
    with open(REFS) as f:
        refs = json.load(f)
    with open(os.path.join(OUT, "ps2-map.json")) as f:
        pmap = json.load(f)
    trusted = lambda e: e["ps2"] and e["how"].startswith(("exact", "snapped")) and         ("--unanchored" in sys.argv or e["block_score"] == "anchored")
    by_ps2 = {e["ps2"]: e for e in pmap if e["how"] and trusted(e)}

    def names(program):
        q = g.qualified_names(program)
        n = collections.defaultdict(list)
        for a, b in g.functions(program):
            nm = q.get(a, b)
            if not re.match(r"(FUN|thunk_FUN|LAB)_", nm.split("::")[-1]):
                n[nm].append(a)
        return {k: v[0] for k, v in n.items() if len(v) == 1}

    xn, pn = names(g.XBOX), names(g.PS2)
    votes = collections.defaultdict(collections.Counter)
    used = skipped = 0
    for nm, xa in xn.items():
        pa = pn.get(nm)
        if pa is None:
            continue
        X = refs[g.XBOX].get(f"{xa:x}", [])
        P = list(dict.fromkeys(abs(v) for v in refs[g.PS2].get(f"{pa:x}", [])))
        if not X or not P:
            continue
        if len(X) != len(P):
            skipped += 1
            continue
        used += 1
        for x, p in zip(X, P):
            if p in by_ps2:
                votes[p][x] += 1
    claims = {}
    for p, c in votes.items():
        (x, v), *rest = c.most_common(2) + [(None, 0)]
        second = rest[0][1] if rest else 0
        if v >= 2 and v > 2 * second:
            if x not in claims or claims[x][1] < v:
                claims[x] = (p, v, second)
    out = []
    for x, (p, v, second) in sorted(claims.items()):
        e = by_ps2[p]
        out.append({"xbox": x, "ps2": p, "name": e["name"], "size_ps2": e["size"], "obj": e["obj"],
                    "votes": v, "runner_up": second})
    with open(os.path.join(OUT, "xbox-pairs.json"), "w") as f:
        json.dump(out, f, indent=0)
    print(f"{used} function pairs used ({skipped} skipped, reference counts differ); "
          f"{len(votes)} ELF symbols voted; {len(out)} paired to Xbox")


def emit(key, objs):
    with open(os.path.join(OUT, "ps2-map.json")) as f:
        pmap = json.load(f)
    with open(os.path.join(OUT, "xbox-pairs.json")) as f:
        pairs = json.load(f)
    pick = (lambda o: True) if objs == ["all"] else (lambda o: o in objs)
    ps2 = [{"address": f"0x{e['ps2']:08x}", "name": e["name"],
            "comment": f"sheet 0x{e['sheet']:x}, {e['size']} bytes; {e['how']}; block {e['block_score']}"}
           for e in pmap if e["ps2"] and e["how"].startswith(("exact", "snapped", "predicted"))
           and e["block_score"] == "anchored" and pick(e["obj"])]
    xb = [{"address": f"0x{e['xbox']:08x}", "name": e["name"],
           "comment": f"PS2 0x{e['ps2']:x} ({e['size_ps2']} bytes on PS2); {e['votes']} votes"}
          for e in pairs if pick(e["obj"])]
    for program, labels in ((g.PS2, ps2), (g.XBOX, xb)):
        path = os.path.join(OUT, f"{key}-{'xbox' if program == g.XBOX else 'ps2'}.json")
        with open(path, "w") as f:
            json.dump({"program": program, "labels": labels}, f, indent=1)
        print(f"{path}: {len(labels)} labels")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "--refs":
        build_refs()
    elif a[0] == "--calibrate":
        calibrate()
    elif a[0] == "--pair":
        pair()
    elif a[0] == "--emit":
        emit(a[1], a[2:])
