#!/usr/bin/env python3
"""Regenerate the driving engine's library entry-point tables.

The seams stand in front of the XBE's statically linked libraries - D3D8 for graphics, DirectSound for audio -
patching every entry point at its own address so that the game's code above them runs unchanged. Each patch
needs two facts about the function it replaces: where it is, and how many bytes of arguments it pops, because
a __stdcall function that returns without cleaning up the caller's stack corrupts it.

Both come out of the binary rather than out of a header nobody has:

  - the addresses and names from tools/functions_driving.json, which is exported from Ghidra;
  - the stack-argument size from the first RET reached by disassembling the function linearly from its entry
    point. Every RET in a __stdcall function pops the same amount, so the first one is the answer, and a
    linear sweep from a known instruction boundary stays on instruction boundaries.

Run it after re-syncing the JSON, or after Ghidra learns a name a table does not have yet:

    python tools/xbe_entry_points.py
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

# One table per seam: the Ghidra namespaces it covers, and where the table goes. XGRAPHC rides with D3D8
# because XGSetTextureHeader and its siblings build Xbox texture headers the backend has to know about, and
# they are reached the same way.
TABLES = (
    (("D3D8", "XGRAPHC"), os.path.join(ROOT, "src", "driving", "gfx", "d3d8Entries.inc"),
     "D3D8 and XGRAPHC", "src/driving/gfx/d3dSeam.cpp"),
    (("DSOUND",), os.path.join(ROOT, "src", "driving", "sound", "dsoundEntries.inc"),
     "DirectSound", "src/driving/sound/dsndSeam.cpp"),
)


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


def sweep_for_ret(md, data, sections, address, follow_jumps):
    """The first RET reached from `address`, or None. Optionally following unconditional jumps."""
    seen = set()
    for _ in range(32):
        if address in seen:
            return None
        seen.add(address)
        code = read_at(data, sections, address, 0x2000)
        if not code:
            return None

        followed = False
        for insn in md.disasm(code, address):
            if insn.mnemonic == "ret":
                return int(insn.op_str, 0) if insn.op_str else 0
            if insn.mnemonic == "retf":
                return None
            if follow_jumps and insn.mnemonic == "jmp":
                try:
                    address = int(insn.op_str, 0)
                except ValueError:
                    return None   # a jump through a register or memory is not something to follow blind
                followed = True
                break
        if not followed:
            return None
    return None


def stack_bytes(md, data, sections, address):
    """How many bytes of arguments the function at `address` pops, or None if it could not be read.

    Sweeping from the entry point and taking the first RET works because every RET in a __stdcall function
    pops the same amount. The subtlety is what to do with an unconditional jump, and both answers are wrong
    on their own:

      - sweeping *past* one walks out of a thunk and into the next function. D3DDevice_SetTile is two
        instructions - a call and a jump to D3D_SetTileNoWait - so reading on gave the count of the function
        after it, and Get2DSurfaceDesc is a bare jump with the same problem;
      - *following* one can leave the function by another door, and a few of these have long internal jump
        chains that end somewhere this cannot follow.

    So it follows jumps first, which is right for a thunk or a tail call, and falls back to ignoring them,
    which is right for a function whose own control flow it wandered into. Both are heuristics; the seam
    checks each answer against what its replacement actually pops, which is what catches the rest.
    """
    followed = sweep_for_ret(md, data, sections, address, True)
    if followed is not None:
        return followed
    return sweep_for_ret(md, data, sections, address, False)


def main():
    data = open(XBE, "rb").read()
    sections = load_sections(data)
    symbols = json.load(open(SYMBOLS, encoding="utf-8"))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    for namespaces, output, description, seam in TABLES:
        entries = []
        for symbol in symbols:
            if symbol["namespace"] not in namespaces:
                continue
            address = int(symbol["address"], 16)
            entries.append((address, symbol["name"].split("::")[-1],
                            stack_bytes(md, data, sections, address)))
        entries.sort()

        os.makedirs(os.path.dirname(output), exist_ok=True)
        with open(output, "w", newline="\n") as out:
            out.write("// Generated by tools/xbe_entry_points.py - do not edit.\n"
                      "//\n"
                      "// The driving engine's %s entry points: name, address and the number of bytes of\n"
                      "// arguments each pops on return. See %s for what is done with them.\n"
                      "//\n"
                      "// XBE_ENTRY(name, address, stackBytes)\n"
                      "// XBE_ENTRY_UNKNOWN_STACK(name, address) - no RET found by a linear sweep, so the seam\n"
                      "//   cannot substitute a stub that returns; read these by hand before replacing them.\n\n"
                      % (description, seam))
            for address, name, size in entries:
                if size is None:
                    out.write("XBE_ENTRY_UNKNOWN_STACK(%s, 0x%08xu)\n" % (name, address))
                else:
                    out.write("XBE_ENTRY(%s, 0x%08xu, %d)\n" % (name, address, size))

        unknown = [e for e in entries if e[2] is None]
        print("%d %s entry points written to %s%s" %
              (len(entries), description, os.path.relpath(output, ROOT),
               "" if not unknown else " (%d without a readable RET: %s)" %
               (len(unknown), ", ".join(e[1] for e in unknown))))


if __name__ == "__main__":
    main()
