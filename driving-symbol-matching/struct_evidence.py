"""Evidence for a class's Xbox layout, gathered mechanically so a reviewer can decide the struct from a summary
instead of reading every function. Read-only.

    python struct_evidence.py WWorldPos WCollider Simulation     -> results/struct-evidence/<Class>.md

Per class:
  - methods on both platforms (Xbox namespace, PS2 namespace) with the sheet's signatures and Ghidra conventions;
  - sizes: FastAlloc sizes under the class's tag (tools/alloc_sizes_driving.json) and the FastFree/delete size in
    its deleting destructor;
  - vtable: the Xbox vtable(s) its constructors store, slot count; PS2 "virtual table" sheet row;
  - base class: another class's constructor called on `this` first in the constructor;
  - every this-relative memory access in its methods, from disassembly (not decompiler text), with a simple
    linear alias tracker (Xbox: ECX at entry of __thiscall members, then MOV copies; PS2: a0, then move copies):
    offset, width, read/write, float (FPU / lwc1-swc1) or not, LEA (address taken: an embedded member or array),
    pointer hints (a field loaded into ECX/a0 right before a call: the callee's class), and which methods;
  - both platforms' offset tables side by side, so the shift (vptr, bool width, vector padding) can be read off;
  - accessors: short methods whose only this-access is one field.
Limits: the alias tracking is linear (no control flow), accesses from non-member functions aren't collected, and
64-bit fields show up on Xbox as two dword accesses.
"""

import collections
import json
import os
import re
import sys

from lib import ghidra_ro as g
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
X86_MEM = re.compile(r"(byte|word|dword|qword|float|double|tword|xmmword) ptr \[(E[A-Z]{2})(?: \+ (0x[0-9a-f]+|\d+))?\]")
X86_REGS = ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP")
WIDTH = {"byte": 1, "word": 2, "dword": 4, "qword": 8, "float": 4, "double": 8, "tword": 10, "xmmword": 16}
MIPS_MEM = re.compile(r"^_?(lb|lbu|lh|lhu|lw|lwu|ld|lq|lwc1|sb|sh|sw|sd|sq|swc1|lwl|lwr|ldl|ldr|sdl|sdr|swl|swr)\s+(\w+),(-?0x[0-9a-f]+|-?\d+)\((\w+)\)")
MIPS_WIDTH = {"lb": 1, "lbu": 1, "sb": 1, "lh": 2, "lhu": 2, "sh": 2, "lw": 4, "lwu": 4, "sw": 4, "lwc1": 4, "swc1": 4,
              "ld": 8, "sd": 8, "lq": 16, "sq": 16, "lwl": 4, "lwr": 4, "swl": 4, "swr": 4, "ldl": 8, "ldr": 8,
              "sdl": 8, "sdr": 8}
MIPS_CLOBBER = {"v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "at", "ra"}


def dis(program, a):
    out = []
    for line in g.get("disassemble_function", program=program, address=hex(a)).splitlines():
        if ":" in line:
            addr, ins = line.split(":", 1)
            out.append((addr.strip(), ins.strip()))
    return out


def names(program):
    q = g.qualified_names(program)
    return {a: q.get(a, n) for a, n in g.functions(program)}


def x86_accesses(a, xn, require_ecx=False):
    """[(offset, width, rw, float, lea, hint)] for this-relative accesses; ECX = this at entry.
    With require_ecx, None when ECX isn't read before it is written or a call is made (not a member)."""
    this = {"ECX"}
    acc = []
    ins = dis(g.XBOX, a)
    for i, (addr, s) in enumerate(ins):
        op, _, args = s.partition(" ")
        dst, _, src = args.partition(",")
        if require_ecx and "ECX" in args:
            require_ecx = False
            if dst.strip() == "ECX" and op in ("MOV", "LEA", "XOR", "POP", "MOVZX", "MOVSX") and "ECX" not in src:
                return None   # ECX written before it is read: not a member
        elif require_ecx and op == "CALL":
            return None
        for m in X86_MEM.finditer(args):
            reg = m.group(2)
            if reg not in this:
                continue
            off = int(m.group(3), 0) if m.group(3) else 0
            in_dst = m.start() < len(dst)
            fpu = op.startswith("F")
            rw = "W" if (in_dst and op not in ("CMP", "TEST", "PUSH")) or op in ("FST", "FSTP", "FISTP", "FIST") else "R"
            if in_dst and op in ("ADD", "SUB", "INC", "DEC", "AND", "OR", "XOR", "SHL", "SHR", "NEG", "NOT"):
                rw = "RW"
            hint = None
            if rw == "R" and dst.strip() in X86_REGS and not fpu:
                r = dst.strip()
                for j in range(i + 1, min(i + 6, len(ins))):
                    o2, _, a2 = ins[j][1].partition(" ")
                    if o2 == "CALL" and r == "ECX":
                        hint = callee_name(a2, xn)
                        break
                    if a2.startswith("ECX," + r) and o2 == "MOV":
                        r = "ECX"
                        continue
                    if a2.split(",")[0].strip() == r and o2 not in ("TEST", "CMP", "PUSH"):
                        break
            acc.append((off, WIDTH[m.group(1)], rw, fpu, False, hint))
        if op == "LEA":
            m = re.match(r"(E[A-Z]{2}),\[(E[A-Z]{2})(?: \+ (0x[0-9a-f]+|\d+))?\]", args)
            if m and m.group(2) in this:
                off = int(m.group(3), 0) if m.group(3) else 0
                hint = None
                if m.group(1) == "ECX":
                    for j in range(i + 1, min(i + 4, len(ins))):
                        o2, _, a2 = ins[j][1].partition(" ")
                        if o2 == "CALL":
                            hint = callee_name(a2, xn)
                            break
                if off:
                    acc.append((off, 0, "LEA", False, True, hint))
        # alias tracking
        d = dst.strip()
        if op == "MOV" and d in X86_REGS and src.strip() in this:
            this.add(d)
        elif d in X86_REGS and op in ("MOV", "LEA", "POP", "XOR", "MOVZX", "MOVSX", "ADD", "SUB", "AND", "OR", "INC", "DEC", "IMUL", "SHL", "SHR", "SAR"):
            if not (op == "MOV" and src.strip() in this):
                this.discard(d)
        elif op == "CALL":
            this -= {"EAX", "ECX", "EDX"}
        if not this:
            break
    return acc


def mips_accesses(a, pn):
    acc = []
    ins = dis(g.PS2, a)
    # A method returning a struct by value gets the result buffer in a0 (returned in v0) and `this` in a1.
    body = [s.lstrip("_") for _, s in ins]
    by_value = any(re.match(r"(move|daddu|addu|or)\s+v0,a0(,zero)?$", s) for s in body) and any(re.search(r"\(a1\)$", s) for s in body)
    this = {"a1"} if by_value else {"a0"}
    for i, (addr, s) in enumerate(ins):
        s2 = s.lstrip("_")
        m = MIPS_MEM.match(s2)
        if m and m.group(4) in this and m.group(1) not in ("lwl", "ldl", "swl", "sdl"):
            op, rt, off = m.group(1), m.group(2), int(m.group(3), 0)
            store = op.startswith("s")
            hint = None
            if not store and rt == "a0":
                for j in range(i + 1, min(i + 5, len(ins))):
                    t = ins[j][1].lstrip("_")
                    if t.startswith("jal "):
                        hint = callee_name(t.split()[1], pn, g.PS2)
                        break
            acc.append((off, MIPS_WIDTH[op], "W" if store else "R", op.endswith("c1"), False, hint))
        parts = re.split(r"[\s,]+", s2)
        op = parts[0]
        dst = parts[1] if len(parts) > 1 else ""
        if op in ("move", "daddu", "addu", "or") and len(parts) >= 3 and parts[2] in this and (len(parts) == 3 or parts[3] == "zero"):
            this.add(dst)
        elif op in ("addiu", "daddiu") and len(parts) >= 4 and parts[2] in this:
            try:
                off = int(parts[3], 0)
                if off and dst not in this:
                    acc.append((off, 0, "LEA", False, True, None))
            except ValueError:
                pass
            if dst in this and parts[2] != dst:
                this.discard(dst)
        elif op.startswith("jal"):
            this -= MIPS_CLOBBER
        elif dst in this and not op.startswith(("s", "b", "j")) and op not in ("nop",):
            this.discard(dst)
        if not this:
            break
    return acc


def callee_name(arg, nm, program=None):
    m = re.search(r"0x([0-9a-f]+)", arg)
    if not m:
        return None
    return nm.get(int(m.group(1), 16))


def table(acc_by_method):
    """{offset: {"widths": set, "rw": set, "float": bool, "lea": bool, "hints": set, "methods": set}}"""
    t = collections.defaultdict(lambda: {"widths": set(), "rw": set(), "float": False, "lea": False, "hints": set(), "methods": set()})
    for meth, accs in acc_by_method.items():
        for off, w, rw, fl, lea, hint in accs:
            e = t[off]
            if w:
                e["widths"].add(w)
            e["rw"].add(rw)
            e["float"] |= fl
            e["lea"] |= lea
            if hint:
                e["hints"].add(hint)
            e["methods"].add(meth)
    return t


def fmt(t):
    lines = []
    for off in sorted(t):
        e = t[off]
        kind = ("float " if e["float"] else "") + ("addr-taken " if e["lea"] else "")
        ms = sorted(e["methods"])
        lines.append(f"  +0x{off:03x}  w{sorted(e['widths']) or '-'} {'/'.join(sorted(e['rw']))} {kind}"
                     f"{('-> ' + ', '.join(sorted(e['hints']))[:120] + ' ') if e['hints'] else ''}"
                     f"[{len(ms)}: {', '.join(m.split('::')[-1] for m in ms[:6])}{'…' if len(ms) > 6 else ''}]")
    return lines


def main():
    classes = [a for a in sys.argv[1:] if not a.startswith("--")]
    xn, pn = names(g.XBOX), names(g.PS2)
    with open(os.path.join(ROOT, "tools", "alloc_sizes_driving.json")) as f:
        alloc = json.load(f)
    with open(os.path.join(HERE, "data", "vtables.json")) as f:
        vts = json.load(f)
    rows = Index().rows
    out_dir = os.path.join(HERE, "results", "struct-evidence")
    os.makedirs(out_dir, exist_ok=True)
    for cls in classes:
        leaf = cls.split("::")[-1]
        xm = sorted((a, n) for a, n in xn.items() if n.startswith(cls + "::") and "::" not in n[len(cls) + 2:])
        pm = sorted((a, n) for a, n in pn.items() if n.startswith(cls + "::") and "::" not in n[len(cls) + 2:])
        sheet = [r["name"] for r in rows if r["name"].startswith(cls + "::") or r["name"].startswith(cls + " ")]
        lines = [f"# {cls}", ""]
        # sizes
        sizes = alloc.get(cls) or alloc.get(leaf)
        lines.append(f"FastAlloc/constructed sizes under its tag: {sizes}")
        for a, n in xm:
            if n.endswith("scalar_deleting_destructor"):
                ins = dis(g.XBOX, a)
                for k, (_, s) in enumerate(ins):
                    if s.startswith("CALL"):
                        pushes = [x[1] for x in ins[max(0, k - 3):k] if x[1].startswith("PUSH 0x")]
                        if pushes:
                            lines.append(f"deleting destructor {a:#x} frees/deletes with size {pushes[-1].split()[1]} "
                                         f"(call to {callee_name(s, xn)})")
        # vtable and base
        ctors = [a for a, n in xm if n.endswith("::" + leaf)]
        starts = sorted(xn)
        import bisect
        def fend(a):
            i = bisect.bisect_right(starts, a)
            return starts[i] if i < len(starts) else a + 0x1000
        own = [(c, fend(c)) for c in ctors + [a for a, n in xm if "~" in n]]
        stored = [v for v in vts["xbox"] if any(any(c <= int(str(st), 0) < e for c, e in own) for st in v.get("stores", []))]
        for v in stored:
            lines.append(f"Xbox vtable {v['address']} ({len(v['slots'])} slots) stored by its constructor")
        if not stored:
            lines.append("No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)")
        ps2vt = [r for r in sheet if r.endswith(" virtual table")]
        lines.append(f"PS2 sheet virtual table row: {ps2vt[:1] or 'none'}")
        for c in ctors:
            first = [s for _, s in dis(g.XBOX, c) if s.startswith("CALL")][:3]
            lines.append(f"constructor {c:#x} first calls: {[callee_name(s, xn) for s in first]}")
        # methods
        lines += ["", f"Xbox methods ({len(xm)}):"]
        info = {}
        for a, n in xm:
            sig = g.function_info(g.XBOX, a).get("signature", "")
            info[a] = sig
            lines.append(f"  {a:#x} {sig[:110]}")
        lines += ["", f"PS2 methods ({len(pm)}):"] + [f"  {a:#x} {n}" for a, n in pm]
        lines += ["", "Sheet rows:"] + [f"  {s}" for s in sheet[:80]]
        # accesses
        xacc = {}
        for a, n in xm:
            known = "__thiscall" in info[a] or "this" in info[a] or n.endswith("::" + leaf) or "~" in n
            r = x86_accesses(a, xn, require_ecx=not known)
            if r is not None:
                xacc[n + f"@{a:x}"] = r
        lines += ["", f"Xbox methods treated as members ({len(xacc)} of {len(xm)}; untyped ones count when ECX is read "
                  "before it is written): " + ", ".join(sorted({k.split('@')[0].split('::')[-1] for k in xacc}))]
        pacc = {n + f"@{a:x}": mips_accesses(a, pn) for a, n in pm}
        xt, pt = table(xacc), table(pacc)
        lines += ["", "Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):"] + fmt(xt)
        lines += ["", "PS2 this-relative accesses (PS2 offsets):"] + fmt(pt)
        # accessors
        acc_lines = []
        for meth, accs in xacc.items():
            a = int(meth.split("@")[1], 16)
            if len(dis(g.XBOX, a)) <= 8 and len({o for o, *_ in accs}) == 1:
                o, w, rw, fl, lea, _ = accs[0]
                acc_lines.append(f"  {meth.split('@')[0].split('::')[-1]}: {rw} +0x{o:x} w{w}{' float' if fl else ''}")
        lines += ["", "Short single-field methods (accessor candidates; check they touch this, not a pointee):"] + acc_lines
        path = os.path.join(out_dir, f"{cls.replace('::', '_')}.md")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{path}: {len(xm)} Xbox / {len(pm)} PS2 methods, {len(xt)} / {len(pt)} offsets")


if __name__ == "__main__":
    main()
