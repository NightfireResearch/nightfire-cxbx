#!/usr/bin/env python3
"""Measure every function's calling convention from the binary.

Ghidra's calling conventions cannot be trusted for this game: most driving-engine functions are "unknown",
and some of the labelled ones are wrong (UFileLoader::FileLoad is labelled __stdcall and pops nothing). A
replacement patched over a function has to honour the convention its callers use, so the injection table
checks each tagged declaration against what the original actually does - at compile time, through
src/common/xbeAbi.h. This tool produces what it checks against. See docs/driving-injection-framework.md.

For every function in tools/functions_<side>.json it records:

  pops     the bytes every RET pops (0 for __cdecl; the argument bytes for __stdcall, __thiscall and the
           stack part of __fastcall), or null when no RET is reachable or two RETs disagree;
  regs_in  registers read before they are written on some path from the entry point - "ecx" is how `this`
           (or a __fastcall first argument) looks from inside, "edx" a __fastcall second argument, and the rest
           register arguments of the kind link-time code generation makes;
  bytes    the first eight bytes, for checking a patch site before writing to it.

The walk follows the function's own control flow, jump tables included. A jump out of the function is a
tail call; a call or tail call to another function contributes that function's facts, resolved by iterating
until nothing changes - so a method that hands its own `this` straight to another method still reads ECX.
Some idioms are not reads: `push ecx`, which MSVC uses to reserve a stack slot; `xor`/`sub`/`sbb reg, reg`
and `or reg, -1`, whose results do not depend on the register; and `lea ecx, [ecx]` / `mov edi, edi`,
alignment padding. `first_read` records where each register in regs_in was first read, so that any result
can be checked by looking at one instruction.

The output is committed (tools/abi_<side>.json) because CI builds without the disc:

    python tools/abi_facts.py            # both engines
    python tools/abi_facts.py driving
"""

import json
import os
import sys

try:
    import capstone
    from capstone import x86
except ImportError:
    sys.exit("capstone is needed: python -m pip install capstone")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbe_entry_points import load_sections, read_at

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIDES = {"action": "default.xbe", "driving": "Driving.xbe"}

# Register families: a read of CL is a read of ECX.
FAMILY = {}
for full, parts in (("eax", "eax ax al ah"), ("ecx", "ecx cx cl ch"), ("edx", "edx dx dl dh"),
                    ("ebx", "ebx bx bl bh"), ("esi", "esi si"), ("edi", "edi di")):
    for part in parts.split():
        FAMILY[part] = full
TRACKED = ("eax", "ecx", "edx", "ebx", "esi", "edi")
CALLER_SAVED = ("eax", "ecx", "edx")     # what a call leaves undefined

MAX_INSNS = 20000   # per function; the largest real ones are a few thousand


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.sections = load_sections(self.data)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.cache = {}

    def insn(self, address):
        if address not in self.cache:
            code = read_at(self.data, self.sections, address, 16)
            decoded = next(self.md.disasm(code, address, 1), None) if code else None
            self.cache[address] = decoded
        return self.cache[address]

    def u32(self, address):
        raw = read_at(self.data, self.sections, address, 4)
        return int.from_bytes(raw, "little") if len(raw) == 4 else None


def reg_names(md, regs):
    return {FAMILY[md.reg_name(r)] for r in regs if md.reg_name(r) in FAMILY}


def jump_table_targets(image, insn, start, end):
    # jmp dword ptr [reg*4 + table]: read entries while they land inside the function.
    op = insn.operands[0]
    if op.type != x86.X86_OP_MEM or op.mem.scale != 4 or op.mem.disp == 0:
        return None
    targets = []
    for i in range(1024):
        target = image.u32(op.mem.disp + i * 4)
        if target is None or not (start <= target < end):
            break
        targets.append(target)
    return targets or None


def analyse(image, start, end, known):
    """One pass over one function. `known` maps other functions' addresses to their facts so far."""
    pops = set()
    regs_in = set()
    first_read = {}   # register -> the address of the first read found, for a person checking a result
    complete = True
    # State: the tracked registers written so far on this path. Bounded by visiting each (address, state) once.
    work = [(start, frozenset())]
    seen = set()
    steps = 0
    while work:
        address, written = work.pop()
        while True:
            if (address, written) in seen:
                break
            seen.add((address, written))
            steps += 1
            if steps > MAX_INSNS:
                return None, set(), {}, False
            insn = image.insn(address)
            if insn is None:
                complete = False
                break
            mnemonic = insn.mnemonic
            next_address = address + insn.size

            reads, writes = insn.regs_access()
            read = reg_names(image.md, reads)
            wrote = reg_names(image.md, writes)
            ops = insn.operands
            if mnemonic in ("xor", "sub", "sbb") and len(ops) == 2 \
                    and ops[0].type == ops[1].type == x86.X86_OP_REG and ops[0].reg == ops[1].reg:
                read = set()                       # xor/sub reg, reg is 0; sbb reg, reg is 0 or -1 from the carry
            if mnemonic in ("or", "and") and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                    and ops[1].type == x86.X86_OP_IMM:
                ones = (1 << (8 * ops[0].size)) - 1
                if (mnemonic == "or" and ops[1].imm & ones == ones) or (mnemonic == "and" and ops[1].imm & ones == 0):
                    read = set()                   # or reg, -1 / and reg, 0 (any width): a constant
            if mnemonic == "push" and len(ops) == 1 and ops[0].type == x86.X86_OP_REG:
                read = set()                       # saving a register, or reserving a stack slot with push ecx
            if mnemonic in ("lea", "mov") and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and (
                    (ops[1].type == x86.X86_OP_REG and ops[1].reg == ops[0].reg) or
                    (ops[1].type == x86.X86_OP_MEM and ops[1].mem.base == ops[0].reg and ops[1].mem.index == 0
                     and ops[1].mem.disp == 0)):
                read, wrote = set(), set()         # lea ecx, [ecx] / mov edi, edi: alignment padding
            for r in (read & set(TRACKED)) - written:
                if r not in regs_in:
                    regs_in.add(r)
                    first_read[r] = address
            written = written | frozenset(wrote & set(TRACKED))

            if mnemonic == "ret":
                pops.add(ops[0].imm if ops else 0)
                break
            if mnemonic in ("int3", "hlt", "ud2", "retf", "iret", "iretd"):
                break

            if mnemonic == "call":
                if ops and ops[0].type == x86.X86_OP_IMM and ops[0].imm in known:
                    callee = known[ops[0].imm]
                    for r in {r for r in callee["regs_in"] if r in CALLER_SAVED} - written - regs_in:
                        regs_in.add(r)
                        first_read[r] = address
                written = written | frozenset(CALLER_SAVED)
                address = next_address
                continue

            if mnemonic == "jmp":
                if ops and ops[0].type == x86.X86_OP_IMM:
                    target = ops[0].imm
                    if start <= target < end:
                        address = target
                        continue
                    # A tail call: the callee returns to our caller, so its facts are ours on this path.
                    callee = known.get(target)
                    if callee is None or callee["pops"] is None:
                        complete = False
                    else:
                        pops.add(callee["pops"])
                        for r in set(callee["regs_in"]) - written - regs_in:
                            regs_in.add(r)
                            first_read[r] = address
                    break
                targets = jump_table_targets(image, insn, start, end)
                if targets is None:
                    complete = False
                    break
                for target in targets:
                    work.append((target, written))
                break

            if insn.group(x86.X86_GRP_JUMP):   # conditional
                if ops and ops[0].type == x86.X86_OP_IMM and start <= ops[0].imm < end:
                    work.append((ops[0].imm, written))
                elif ops and ops[0].type == x86.X86_OP_IMM:
                    complete = False               # a conditional tail call; rare
                address = next_address
                continue

            address = next_address

    if len(pops) == 1:
        return pops.pop(), regs_in, first_read, complete
    return None, regs_in, first_read, complete and not pops


def facts_for(side):
    xbe = os.path.join(ROOT, "disc", SIDES[side])
    symbols = json.load(open(os.path.join(ROOT, "tools", f"functions_{side}.json"), encoding="utf-8"))
    image = Image(xbe)

    starts = sorted({int(s["address"], 16) for s in symbols})
    bounds = {a: (starts[i + 1] if i + 1 < len(starts) else a + 0x10000) for i, a in enumerate(starts)}

    known = {}
    for iteration in range(8):
        changed = 0
        for address in starts:
            pops, regs_in, first_read, complete = analyse(image, address, bounds[address], known)
            entry = {"pops": pops, "regs_in": sorted(regs_in), "complete": complete,
                     "first_read": {r: "0x%08x" % a for r, a in sorted(first_read.items())}}
            if known.get(address) != entry:
                known[address] = entry
                changed += 1
        print(f"  {side}: pass {iteration + 1}, {changed} functions changed")
        if changed == 0:
            break

    out = {}
    for address in starts:
        entry = dict(known[address])
        entry["bytes"] = read_at(image.data, image.sections, address, 8).hex()
        out["0x%08x" % address] = entry
    return out


def main():
    sides = sys.argv[1:] or list(SIDES)
    for side in sides:
        facts = facts_for(side)
        path = os.path.join(ROOT, "tools", f"abi_{side}.json")
        with open(path, "w", newline="\n") as f:
            json.dump(facts, f, indent=0, sort_keys=True)
            f.write("\n")
        known = sum(1 for v in facts.values() if v["pops"] is not None)
        ecx = sum(1 for v in facts.values() if "ecx" in v["regs_in"])
        print(f"{side}: {len(facts)} functions, {known} with a known pop count, {ecx} reading ECX on entry"
              f" -> {os.path.relpath(path, ROOT)}")


if __name__ == "__main__":
    main()
