"""Finds every instruction in an XBE that refers to a block of data, for moving that data into our source.

A table the game's code uses in place (the menu's M_ITEM lists, say) can be reimplemented as an array of ours
only if the original code still being run uses it too: otherwise the game writes unlock flags into its own copy
while our handlers read ours. So the injector points every reference at our array instead (XbeRelocate,
src/common/xbeRelocate.h); this script finds the references, once, from the binary.

  python tools/data_refs.py action      # reads tools/relocations_action.json, writes tools/data_refs_action.json

relocations_<side>.json: [{"name": "sp_level", "address": "0x17c580", "size": 288}, ...] - name is the array in
our source (tagged // RELOCATE above its definition), size its size in bytes.

Method: every dword in the code sections whose value falls inside a block is a candidate; each is kept only if
decoding the containing function (from its entry, in functions_<side>.json) finds an instruction with that dword
as its displacement or immediate at exactly that place. Candidates that do not decode are listed, never
patched: a stray match in data or inside another instruction must not be rewritten.
"""
import bisect
import json
import os
import struct
import sys

import capstone
from capstone import x86

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
XBE = {'action': 'default.xbe', 'driving': 'Driving.xbe'}


def sections(d):
    base = struct.unpack_from('<I', d, 0x104)[0]
    count = struct.unpack_from('<I', d, 0x11c)[0]
    at = struct.unpack_from('<I', d, 0x120)[0] - base
    out = []
    for i in range(count):
        flags, va, vsize, raw, rawsize, name = struct.unpack_from('<6I', d, at + i * 56)
        out.append((d[name - base:d.index(b'\0', name - base)].decode(), flags, va, raw, rawsize))
    return out


def main():
    side = sys.argv[1] if len(sys.argv) > 1 else 'action'
    d = open(os.path.join(REPO, 'disc', XBE[side]), 'rb').read()
    blocks = json.load(open(os.path.join(REPO, 'tools', 'relocations_%s.json' % side)))
    funcs = sorted(int(f['address'], 16) for f in json.load(open(os.path.join(REPO, 'tools', 'functions_%s.json' % side))))
    code = [s for s in sections(d) if s[1] & 0x4]   # executable

    def read(va, n):
        for name, flags, sva, raw, size in sections(d):
            if sva <= va < sva + size:
                return d[raw + va - sva:raw + va - sva + n]
        return b''

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    result, problems = {}, []
    for b in blocks:
        lo = int(b['address'], 16)
        hi = lo + b['size']
        sites = []
        for name, flags, sva, raw, size in code:
            blob = d[raw:raw + size]
            for off in range(len(blob) - 3):
                v = struct.unpack_from('<I', blob, off)[0]
                if not lo <= v < hi:
                    continue
                site = sva + off
                i = bisect.bisect_right(funcs, site) - 1
                ok = False
                if i >= 0:
                    start = funcs[i]
                    body = read(start, site - start + 16)
                    for insn in md.disasm(body, start):
                        if insn.address > site:
                            break
                        if insn.address + insn.size <= site:
                            continue
                        rel = site - insn.address
                        ok = (insn.disp_size == 4 and insn.disp_offset == rel) or \
                             (insn.imm_size == 4 and insn.imm_offset == rel)
                        break
                if ok:
                    sites.append(site)
                else:
                    problems.append('%s: 0x%x (value 0x%x, in %s) does not decode as an operand - left alone' % (b['name'], site, v, name))
        result[b['name']] = ['0x%x' % s for s in sites]
        print('%s 0x%x..0x%x: %d references' % (b['name'], lo, hi, len(sites)))
    for p in problems:
        print('  ' + p)
    out = os.path.join(REPO, 'tools', 'data_refs_%s.json' % side)
    json.dump(result, open(out, 'w'), indent=1)
    print('wrote', out)


if __name__ == '__main__':
    main()
