#!/usr/bin/env python3
"""Regenerate src/driving/gfx/d3d8Entries.inc - the driving engine's D3D8 entry-point table.

The driving engine reaches Microsoft's statically linked D3D8 at 108 entry points (plus XGRAPHC's five), and
the graphics seam patches each one at its own address so that EAGL's code above it can run unchanged. Each
patch needs two facts about the function it replaces: where it is, and how many bytes of arguments it pops,
because a __stdcall function that returns without cleaning up the caller's stack corrupts it.

Both come out of the binary rather than out of a header nobody has:

  - the addresses and names from tools/functions_driving.json, which is exported from Ghidra;
  - the stack-argument size from the first RET reached by disassembling the function linearly from its entry
    point. Every RET in a __stdcall function pops the same amount, so the first one is the answer, and a
    linear sweep from a known instruction boundary stays on instruction boundaries.

Run it after re-syncing the JSON, or after Ghidra learns a name this table does not have yet:

    python tools/d3d8_entry_points.py
"""

import json
import os
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("capstone is needed to read the stack-argument sizes: python -m pip install capstone")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XBE = os.path.join(ROOT, "disc", "Driving.xbe")
SYMBOLS = os.path.join(ROOT, "tools", "functions_driving.json")
OUTPUT = os.path.join(ROOT, "src", "driving", "gfx", "d3d8Entries.inc")

# The libraries the seam stands in front of. XGRAPHC is included because XGSetTextureHeader and its siblings
# build Xbox texture headers the backend has to know about, and they are reached the same way.
NAMESPACES = ("D3D8", "XGRAPHC")


def load_sections(data):
    base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x11C)[0]
    headers = struct.unpack_from("<I", data, 0x120)[0] - base
    sections = []
    for i in range(count):
        flags, va, vs, ra, rs = struct.unpack_from("<IIIII", data, headers + i * 0x38)
        sections.append((va, vs, ra, rs))
    return sections


def read_at(data, sections, address, length):
    for va, vs, ra, rs in sections:
        if va <= address < va + vs:
            offset = ra + (address - va)
            available = max(0, min(length, rs - (address - va)))
            return data[offset:offset + available]
    return b""


def stack_bytes(md, data, sections, address):
    """How many bytes of arguments the function at `address` pops, or None if its end was not reached.

    Sweeping linearly from the entry point and taking the first RET works because every RET in a __stdcall
    function pops the same amount. The one thing it must not do is sweep *past* an unconditional jump: several
    of these entry points are one-instruction thunks that jump to the real function, and the bytes after the
    jump belong to the next function entirely - which is how Get2DSurfaceDesc came out as "pops 8" when the
    function it forwards to pops 12, a four-byte hole that corrupted the caller's stack. So a jump to a fixed
    address is followed rather than stepped over.
    """
    for _ in range(4):   # more than one thunk hop in a row would be unusual; a loop would not be a thunk
        code = read_at(data, sections, address, 0x2000)
        if not code:
            return None
        first = True
        followed = False
        for insn in md.disasm(code, address):
            if insn.mnemonic == "ret":
                return int(insn.op_str, 0) if insn.op_str else 0
            if insn.mnemonic == "retf":
                return None
            if first and insn.mnemonic == "jmp":
                # A jump as the whole first instruction is a thunk, and the answer is at the other end. A jump
                # anywhere else is the function's own control flow - stepping over it is what finds the RET,
                # and following it would leave the function.
                try:
                    address = int(insn.op_str, 0)
                except ValueError:
                    return None   # jmp through a register or memory: not something to follow blind
                followed = True
                break
            first = False
        if not followed:
            return None
    return None


def main():
    data = open(XBE, "rb").read()
    sections = load_sections(data)
    symbols = json.load(open(SYMBOLS, encoding="utf-8"))

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    entries = []
    for symbol in symbols:
        if symbol["namespace"] not in NAMESPACES:
            continue
        address = int(symbol["address"], 16)
        entries.append((address, symbol["namespace"], symbol["name"].split("::")[-1],
                        stack_bytes(md, data, sections, address)))

    entries.sort()

    unknown = [e for e in entries if e[3] is None]
    with open(OUTPUT, "w", newline="\n") as out:
        out.write("// Generated by tools/d3d8_entry_points.py - do not edit.\n"
                  "//\n"
                  "// The driving engine's D3D8 and XGRAPHC entry points: name, address and the number of bytes of\n"
                  "// arguments each pops on return. See src/driving/gfx/d3dSeam.cpp for what is done with them.\n"
                  "//\n"
                  "// D3D8_ENTRY(name, address, stackBytes)\n"
                  "// D3D8_ENTRY_UNKNOWN_STACK(name, address) - no RET found by a linear sweep, so the seam cannot\n"
                  "//   substitute a stub that returns; these have to be read by hand before they can be replaced.\n\n")
        for address, namespace, name, size in entries:
            if size is None:
                out.write("D3D8_ENTRY_UNKNOWN_STACK(%s, 0x%08xu)\n" % (name, address))
            else:
                out.write("D3D8_ENTRY(%s, 0x%08xu, %d)\n" % (name, address, size))

    print("%d entry points written to %s" % (len(entries), os.path.relpath(OUTPUT, ROOT)))
    if unknown:
        print("%d without a readable RET: %s" % (len(unknown), ", ".join(e[2] for e in unknown)))


if __name__ == "__main__":
    main()
