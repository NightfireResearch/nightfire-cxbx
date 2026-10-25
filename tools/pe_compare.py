"""Compares two builds of a PE file section by section (raw bytes): the check that a change leaves a build's code and
data as they were (docs/ps2-data.md, the default build against the PS2 data switch). Differences inside the debug
directory (its timestamps) are reported apart and do not count. Exits 1 if anything else differs.

  python tools/pe_compare.py before/action_standalone_test.exe Release/action_standalone_test.exe
"""
import struct, sys

def load(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsize = struct.unpack_from('<H', d, pe + 20)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    dirs = pe + 24 + (96 if magic == 0x10b else 112)
    dbg_rva, dbg_size = struct.unpack_from('<II', d, dirs + 6 * 8)
    at = pe + 24 + optsize
    out = {}
    for i in range(nsec):
        name = d[at:at+8].rstrip(b'\0').decode()
        vsize, va, rawsize, rawptr = struct.unpack_from('<IIII', d, at + 8)
        out[name] = (d[rawptr:rawptr + rawsize], va)
        at += 40
    return out, (dbg_rva, dbg_rva + dbg_size)

(a, dbg), (b, _) = load(sys.argv[1]), load(sys.argv[2])
same = True
for name in sorted(set(a) | set(b)):
    x, va = a.get(name, (b'', 0)); y, _ = b.get(name, (b'', 0))
    if len(x) != len(y):
        same = False
        print('%-8s DIFFERS in size: %d vs %d' % (name, len(x), len(y)))
        continue
    diffs = [i for i in range(len(x)) if x[i] != y[i]]
    inDebug = [i for i in diffs if dbg[0] <= va + i < dbg[1]]
    other = len(diffs) - len(inDebug)
    if other:
        same = False
    print('%-8s %s (%d bytes)%s' % (name, 'identical' if other == 0 else 'DIFFERS: %d bytes' % other, len(x),
                                    ', %d bytes in the debug directory' % len(inDebug) if inDebug else ''))
sys.exit(0 if same else 1)
