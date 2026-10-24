#!/usr/bin/env python3
"""Original callers that keep a value in ECX or EDX across a call to one of our ports.

ECX and EDX are scratch registers in every C calling convention, so a C++ port may change them. The game was linked
with whole-program optimisation, though: where MSVC knew that a callee left ECX or EDX alone, it could keep a value
there across the call. If that callee is now ours, the caller reads whatever our code left. Parcel 7's shadow test
found one such caller (DroneVision_LogSeenOpponent keeps the drone in ECX across NDrone2_ObjectIsArmed, 0x64be0); in
the game it is harmless only because that caller is ours too.

For every port (FUNC_AT, and in the action engine AUTOINJECT and AUTOLTCG by name), this disassembles each live
original caller after the call and reports any that reads ECX or EDX (or a part of them) before writing it,
following the code straight on through conditional jumps and stopping at a jump, call or return - where the original
callee (with everything it calls) never writes that register, so the caller may rely on it. A report is a
candidate to check by hand: the read may be on a path that wrote the register first. A port whose callers are
reported must preserve the register (an AUTOLTCG adaptor that saves it), or the caller must be ported too.

    python tools/caller_saved.py            # the driving engine
    python tools/caller_saved.py --action   # the action engine
    python tools/caller_saved.py --check    # exit status 1 if anything is found
"""
import bisect
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import global_coverage as gc   # noqa: E402
import function_coverage as fc   # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_32   # noqa: E402
from capstone import x86_const as X   # noqa: E402

FAMILIES = {
    "ecx": {X.X86_REG_ECX, X.X86_REG_CX, X.X86_REG_CL, X.X86_REG_CH},
    "edx": {X.X86_REG_EDX, X.X86_REG_DX, X.X86_REG_DL, X.X86_REG_DH},
}
WHOLE = {"ecx": X.X86_REG_ECX, "edx": X.X86_REG_EDX}


def ports(by_name=None):
    """Port address -> (file, definition line), for every tagged definition."""
    found = {}
    for path in gc.source_files():
        if not path.endswith(".cpp"):
            continue
        lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
        for i in range(len(lines) - 1):
            m = re.match(r"\s*//\s*FUNC_AT\((0x[0-9a-fA-F]+)\)", lines[i])
            if m:
                found[int(m.group(1), 16)] = (gc.rel(path), lines[i + 1].strip())
                continue
            if by_name is None or not re.match(r"\s*//\s*(AUTOINJECT|AUTOLTCG)\b", lines[i]):
                continue
            for j in range(i + 1, min(i + 4, len(lines))):
                if lines[j].strip().startswith("//"):
                    continue
                d = re.search(r"\b(?:Asm_)?(\w+)\s*\(", lines[j])   # an entry in inject/ patches its name
                if d and d.group(1) in by_name:
                    found[by_name[d.group(1)]] = (gc.rel(path), lines[j].strip())
                break
    return found


def first_read(instructions):
    """Per register, the first instruction that reads it before anything writes it all."""
    live = dict(FAMILIES)
    reads = {}
    for x in instructions:
        if x.mnemonic in ("call", "ret", "jmp") or x.mnemonic.startswith("int"):
            break
        try:
            regs_read, regs_written = x.regs_access()
        except Exception:
            break
        for name in list(live):
            if set(regs_read) & live[name]:
                # `xor cl, cl` and the like read nothing; `lea ecx, [ecx]` is padding
                ops = x.op_str.split(", ")
                if x.mnemonic in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1]:
                    del live[name]
                    continue
                if x.mnemonic == "lea" and len(ops) == 2 and ops[1] == "[%s]" % ops[0]:
                    continue
                reads[name] = "%08x: %s %s" % (x.address, x.mnemonic, x.op_str)
                del live[name]
            elif set(regs_written) & live[name]:
                del live[name]                         # written (a part written counts: it is not the value kept)
        if not live:
            break
    return reads


def clobbers(md, read, addrs, funcs):
    """Function address -> the registers of FAMILIES its original body, or anything it calls, may write. A call
    through a register or memory, or a body that cannot be read, counts as writing both."""
    body_writes, calls = {}, {}
    for i, start in enumerate(addrs):
        end = addrs[i + 1] if i + 1 < len(addrs) else start + 0x1000
        writes, targets = set(), set()
        try:
            code = read(start, min(end - start, 0x4000))
        except Exception:
            body_writes[start], calls[start] = set(FAMILIES), set()
            continue
        for x in md.disasm(code, start):
            try:
                _, regs_written = x.regs_access()
            except Exception:
                writes |= set(FAMILIES)
                continue
            for name, family in FAMILIES.items():
                if set(regs_written) & family:
                    writes.add(name)
            if x.mnemonic == "call" or (x.mnemonic == "jmp" and x.operands and x.operands[0].type == X.X86_OP_IMM
                                        and not start <= x.operands[0].imm < end):
                op = x.operands[0]
                if op.type == X.X86_OP_IMM:
                    targets.add(op.imm)
                else:
                    writes |= set(FAMILIES)
        body_writes[start], calls[start] = writes, targets
    result = {a: set(w) for a, w in body_writes.items()}
    changed = True
    while changed:
        changed = False
        for a in addrs:
            for t in calls[a]:
                extra = result.get(t, set(FAMILIES)) - result[a]
                if extra:
                    result[a] |= extra
                    changed = True
    return result


def main():
    action = "--action" in sys.argv
    if not action:
        gc.use_engine("driving")
    nonzero, read, entry = gc.read_xbe()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    funcs = {int(f["address"], 16): f["name"] for f in json.load(open(gc.FUNCTIONS))}
    found_ports = ports({name: address for address, name in funcs.items()} if action else None)
    addrs = sorted(funcs)
    status = {f["address"]: f["status"] for f in fc.analyse(driving=not action)}
    callers = {}
    for to, frm, _, kind in json.load(open(gc.XREFS)):
        if kind == "C" and to and frm and int(to, 16) in found_ports:
            callers.setdefault(int(to, 16), []).append(int(frm, 16))
    clobbered = clobbers(md, read, addrs, funcs)
    found = []
    for target, sites in sorted(callers.items()):
        for site in sorted(set(sites)):
            owner = addrs[bisect.bisect_right(addrs, site) - 1]
            if status.get(owner) != "LIVE":
                continue
            ins = list(md.disasm(read(site, 96), site))
            if not ins or ins[0].mnemonic != "call":
                continue
            for name, use in sorted(first_read(ins[1:24]).items()):
                if name in clobbered.get(target, set(FAMILIES)):
                    continue                           # the original changes it too: the caller cannot rely on it
                found.append((target, site, funcs.get(owner, "?"), name, use))
    print("%d ports; %d have live original callers" % (len(found_ports), len(callers)))
    for target, site, owner, name, use in found:
        print("  %08x %s (%s)\n      caller %08x in %s reads %s: %s" % (
            target, funcs.get(target, "?"), found_ports[target][0], site, owner, name, use))
    if not found:
        print("no live original caller reads ECX or EDX after calling a port")
    if "--check" in sys.argv and found:
        sys.exit(1)


if __name__ == "__main__":
    main()
