#!/usr/bin/env python3
"""Ports declared to return bool (or another byte type) whose callers in the original code read all of EAX.

MSVC returns a bool in AL and leaves the rest of EAX as it was. The game's own functions that return a flag often
return a full-register 0 or 1 (`setne al; movzx eax, al`, or `xor eax, eax` / `mov eax, 1`), and their callers may
test all of EAX (`test eax, eax; jle`). A port declared `bool` then hands those callers whatever was in EAX's upper
bytes - a miss can read as a hit. The build's ABI check (src/common/xbeAbi.h) checks arguments and popped bytes,
not the width of the result, so this scan does: for every FUNC_AT'd definition whose return type is a byte, it
disassembles each live original caller (direct calls; calls through vtables and pointers are not followed) after
the call and reports any that reads EAX, AX or AH before writing EAX. Such a port must return int.

    python tools/narrow_returns.py            # the driving engine
    python tools/narrow_returns.py --check    # exit status 1 if anything is found

Found CheckHitWorld/StepCheckHitWorld (4 October 2026: AI cars reacting to collisions that never happened).
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

NARROW = r"(bool|uint8_t|int8_t|BOOL8|char|unsigned char|signed char)"


def narrow_ports():
    """FUNC_AT address -> (file, definition line) for definitions returning a byte type."""
    found = {}
    for path in gc.source_files():
        if not path.endswith(".cpp"):
            continue
        lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
        for i in range(len(lines) - 1):
            m = re.match(r"\s*//\s*FUNC_AT\((0x[0-9a-fA-F]+)\)", lines[i])
            if m and re.match(r"\s*(static\s+)?(inline\s+)?" + NARROW + r"\s", lines[i + 1]):
                found[int(m.group(1), 16)] = (gc.rel(path), lines[i + 1].strip())
    return found


def reads_full_eax(instructions):
    """The first use of EAX after a call: the instruction text if it reads more than AL, else None."""
    for x in instructions:
        if x.mnemonic in ("call", "ret", "jmp"):
            return None
        ops = x.op_str
        if x.mnemonic == "xor" and ops == "eax, eax":
            return None                                   # zeroed: the result is dropped
        if x.mnemonic in ("mov", "lea", "pop", "movzx", "movsx") and ops.startswith("eax,"):
            return None                                   # EAX overwritten (movzx eax, al reads AL only)
        if x.mnemonic == "fnstsw":
            return None                                   # writes AX
        if re.search(r"\b(eax|ax|ah)\b", ops):
            return "%s %s" % (x.mnemonic, ops)
        if re.search(r"\bal\b", ops):
            return None
    return None


def main():
    gc.use_engine("driving")
    nonzero, read, entry = gc.read_xbe()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    ports = narrow_ports()
    funcs = {int(f["address"], 16): f["name"] for f in json.load(open(gc.FUNCTIONS))}
    addrs = sorted(funcs)
    status = {f["address"]: f["status"] for f in fc.analyse(driving=True)}
    callers = {}
    for to, frm, _, kind in json.load(open(gc.XREFS)):
        if kind == "C" and to and frm and int(to, 16) in ports:
            callers.setdefault(int(to, 16), []).append(int(frm, 16))
    found = []
    for target, sites in sorted(callers.items()):
        for site in sites:
            owner = addrs[bisect.bisect_right(addrs, site) - 1]
            if status.get(owner) != "LIVE":
                continue                                   # the caller is ours, or never runs
            ins = list(md.disasm(read(site, 64), site))
            if not ins or ins[0].mnemonic != "call":
                continue
            use = reads_full_eax(ins[1:12])
            if use:
                found.append((target, site, funcs.get(owner, "?"), use))
    print("%d ports return a byte type; %d have live original callers" % (len(ports), len(callers)))
    for target, site, owner, use in found:
        print("  %08x %s (%s)\n      caller %08x in %s: %s" % (target, ports[target][1][:80], ports[target][0], site,
                                                          owner, use))
    if not found:
        print("none of their callers reads more than AL")
    if "--check" in sys.argv and found:
        sys.exit(1)


if __name__ == "__main__":
    main()
