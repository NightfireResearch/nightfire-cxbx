# Dumps and disassembles every vertex shader in the action XBE (default.xbe), and tabulates what each one
# reads and writes. Used for the D3D9 backend work: the game's 128 main shaders are a 7-bit feature matrix
# (see the "by index bit" output), plus the immediate-mode quad shader and the point-sprite overlay shader.
#
# Usage:  pip install nv2a-vsh          (Erik Abair's assembler/disassembler, Unlicense)
#         python tools/vsh_dump.py <path to default.xbe> <output dir>
#
# Output: <dir>/vsNNN.bin (raw XDK blob: 8-byte header {0x78, 0x20, instructionCount, 0} + 16-byte
# instructions), <dir>/vsNNN.vsh (disassembly), and a table on stdout. The XDK stores each 128-bit
# instruction as its three meaningful words followed by the unused word (which the assembler sometimes
# leaves a flag in), whereas nv2a-vsh wants the unused word first.
import struct, sys, os, re, collections
from nv2a_vsh import disassemble

XBE, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
d = open(XBE, 'rb').read()
assert d[:4] == b'XBEH'
base = struct.unpack_from('<I', d, 0x104)[0]
nsec = struct.unpack_from('<I', d, 0x11c)[0]
secptr = struct.unpack_from('<I', d, 0x120)[0] - base
secs = [struct.unpack_from('<IIIII', d, secptr + i * 0x38)[1:] for i in range(nsec)]

def read(va, n):
    for vaddr, vsize, raw, rawsize in secs:
        if vaddr <= va < vaddr + vsize:
            off = va - vaddr
            assert off + n <= rawsize, "va %x beyond raw data" % va
            return d[raw + off:raw + off + n]
    raise Exception("va %x not in any section" % va)

# xboxInitGraphics' tables (see d3dSeam.cpp): 128 function-token pointers, then the immediate-mode and
# overlay shader functions, then the declarations.
funcs = list(struct.unpack('<128I', read(0x1b4d78, 128 * 4)))
blobs = [('vs%03d' % i, a) for i, a in enumerate(funcs)] + [('immediate', 0x1b4f78), ('overlay', 0x1b4fd0)]
decls = {'decl_plain': 0x1b50e4, 'decl_bit0': 0x1b5100, 'decl_bit1': 0x1b5158, 'decl_bit01': 0x1b5178,
         'decl_immediate': 0x1b51d4, 'decl_overlay': 0x1b51e8}

rows = []
for name, a in blobs:
    hdr = read(a, 4)
    assert hdr[0] == 0x78 and hdr[1] == 0x20, (name, hdr.hex())
    count = hdr[2]
    blob = read(a, 8 + 16 * count)
    open(os.path.join(OUT, name + '.bin'), 'wb').write(blob)
    dw = struct.unpack('<%dI' % (count * 4), blob[8:])
    instrs = [[0, dw[i], dw[i + 1], dw[i + 2]] for i in range(0, len(dw), 4)]
    lines = disassemble.disassemble(instrs, explain=False)
    text = '\n'.join(lines)
    open(os.path.join(OUT, name + '.vsh'), 'w').write(text)
    ins = sorted(set(int(x) for x in re.findall(r'\bv(\d+)\b', text)))
    consts = sorted(set(int(x) for x in re.findall(r'\bc\[(-?\d+)\]', text)))
    rel = sorted(set(re.findall(r'\bc\[(?:A0|a0)(?:\.x)?\s*([+-]\s*\d+)?\]', text)))
    outs = sorted(set(re.findall(r'\bo(Pos|D0|D1|Fog|Pts|B0|B1|T0|T1|T2|T3)\b', text)))
    rows.append((name, count, ins, consts, rel, outs))

for n, a in decls.items():
    toks, va = [], a
    while True:
        t = struct.unpack('<I', read(va, 4))[0]; toks.append(t); va += 4
        if t == 0xffffffff or len(toks) > 64: break
    print("%-15s %s" % (n, ' '.join('%08x' % t for t in toks)))
print("skin matrix register table (add 0x60):", struct.unpack('<53i', read(0x1b5208, 53 * 4)))
print()
print("%-10s %4s  %-30s %-52s %-8s %s" % ("shader", "ins", "inputs v#", "constants c[#]", "c[A0+]", "outputs"))
for n, cnt, ins, consts, rel, outs in rows:
    print("%-10s %4d  %-30s %-52s %-8s %s" % (n, cnt, ','.join(map(str, ins)), ','.join(map(str, consts))[:52], ','.join(rel)[:8], ','.join(outs)))

byidx = {int(r[0][2:]): r for r in rows if r[0].startswith('vs')}
print("\nwhat each shader-index bit (Gfx_MiscModeFlags) adds, relative to the same index without it:")
for bit in [1, 2, 4, 8, 16, 32, 64]:
    extra_in, extra_c, extra_out = set(), set(), set()
    for i in range(128):
        if i & bit: continue
        a, b = byidx[i], byidx[i | bit]
        extra_in |= set(b[2]) - set(a[2]); extra_c |= set(b[3]) - set(a[3]); extra_out |= set(b[5]) - set(a[5])
    print("  bit 0x%02x: inputs +%s constants +%s outputs +%s" % (bit, sorted(extra_in), sorted(extra_c), sorted(extra_out)))
