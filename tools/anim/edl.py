"""Eurocom filesys.dNN reader + EDL decompressor (port of Xbox Inflate_huffman 0xd3a80 / Inflate_bitwise /
Inflate_directcopy, maybeEDL_DecompressSection 0xe8e30). Research helper, read-only on the disc image.

usage: python edl.py <name-in-archive> <outfile>      e.g. python edl.py 07000001.bin lvl.bin
"""
import struct, sys, zlib

from nfpaths import DISC

LEN_BASE = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 255]
LEN_EXTRA = [0]*8 + [1]*4 + [2]*4 + [3]*4 + [4]*4 + [5]*4 + [0]
DIST_BASE = [0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072,
             4096, 6144, 8192, 12288, 16384, 24576]
DIST_EXTRA = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]


class Bits:
    def __init__(self, data, pos):
        self.d = data; self.p = pos; self.buf = 0; self.n = 0

    def need(self, k):
        while self.n < k:
            self.buf |= self.d[self.p] << self.n if self.p < len(self.d) else 0
            self.p += 1; self.n += 8

    def get(self, k):
        if k == 0:
            return 0
        self.need(k)
        v = self.buf & ((1 << k) - 1)
        self.buf >>= k; self.n -= k
        return v


def build(lengths):
    """canonical huffman -> dict keyed by (len, code) read MSB-first; returns fast table for 15-bit peek"""
    syms = sorted((l, s) for s, l in enumerate(lengths) if l)
    table = {}
    code = 0; curlen = syms[0][0] if syms else 0
    i = 0
    while i < len(syms):
        l, s = syms[i]
        while curlen < l:
            code <<= 1; curlen += 1
        # reversed code (LSB-first stream)
        r = int(format(code, '0%db' % l)[::-1], 2)
        table[(l, r)] = s
        code += 1; i += 1
    # flatten into a peek table of maxlen bits
    maxlen = max((l for l, _ in table), default=1)
    flat = [None] * (1 << maxlen)
    for (l, r), s in table.items():
        step = 1 << l
        for x in range(r, 1 << maxlen, step):
            flat[x] = (s, l)
    return flat, maxlen


def decode(bits, tab):
    flat, maxlen = tab
    bits.need(maxlen)
    s, l = flat[bits.buf & ((1 << maxlen) - 1)]
    bits.buf >>= l; bits.n -= l
    return s


class HuffState:
    def __init__(self):
        self.prevlen = 0  # local_8: the last explicit code length, persists across symbols/trees/blocks
        self.lit = None; self.dist = None


def inflate_huffman(src, st, out):
    # out is the whole section's output so far: matches may reach back into earlier EDL blocks
    b = Bits(src, 12)
    st.prevlen = 0  # local_8 = 0 at function entry
    while True:
        if b.get(1) == 0:
            n = b.get(15)
            for _ in range(n):
                out.append(b.get(8))
        else:
            for which in (1, 0):  # 1 = literal/length tree (10-bit root), 0 = distance tree (8-bit root)
                cnt = b.get(9)
                if cnt:
                    lens = []
                    for _ in range(cnt):
                        if b.get(1):
                            st.prevlen = b.get(4)
                        lens.append(st.prevlen)
                    t = build(lens)
                    if which: st.lit = t
                    else: st.dist = t
            while True:
                s = decode(b, st.lit)
                if s < 256:
                    out.append(s); continue
                if s == 256:
                    break
                k = s - 257
                ln = LEN_BASE[k] + 3 + b.get(LEN_EXTRA[k])
                ds = decode(b, st.dist)
                dist = DIST_BASE[ds] + b.get(DIST_EXTRA[ds]) + 1
                start = len(out) - dist
                for i in range(ln):
                    out.append(out[start + i])
        if b.get(1):
            return out


def inflate_bitwise(src):
    # port of the repo's Inflate_bitwise (LZ with 1-bit flags); starts after 5 words
    b = Bits(src, 20)
    out = bytearray()
    while True:
        while b.get(1) == 0:
            out.append(b.get(8))
        if b.get(1) == 0:
            ln = 2 + b.get(1)
        else:
            x = b.get(2)
            if x == 3:
                for _ in range(b.get(4)):
                    out.append(b.get(8))
                continue
            ln = x + 3
        lo = b.get(8); hi = 0
        if b.get(1):
            hi = 2 + b.get(2) if b.get(1) else b.get(1)
        d = (hi << 8) | lo
        if d == 0:
            return bytes(out)
        d += 1
        s = len(out) - d
        for i in range(ln):
            out.append(out[s + i])


def decompress_section(data):
    ident, nblocks = struct.unpack_from('<II', data, 0)
    assert ident == 0x443584b, hex(ident)
    p = 8; out = bytearray(); st = HuffState()
    for _ in range(nblocks):
        assert data[p:p + 3] == b'EDL', data[p:p + 4]
        algo = data[p + 3] & 0x7f
        csz, dsz = struct.unpack_from('<II', data, p + 4)
        blk = data[p:p + csz]
        before = len(out)
        if algo == 1:
            inflate_huffman(blk, st, out)
        elif algo == 2:
            out += inflate_bitwise(blk)
        else:
            out += blk[12:12 + dsz]
        assert len(out) - before == dsz, (len(out) - before, dsz)
        p += csz
    return bytes(out)


def entries():
    h = open(DISC % 0, 'rb').read(0x13800)
    n = struct.unpack_from('<i', h, 0x18)[0]
    return {e[0]: e for e in (struct.unpack_from('<7IBBII', h, 0x20 + i * 0x26) for i in range(n))}


def read_file(name):
    e = entries()[zlib.crc32(name.lower().replace('/', chr(92)).encode())]
    with open(DISC % e[9], 'rb') as f:
        f.seek(e[10]); d = f.read(e[5])
    if e[7] & 2:
        d = decompress_section(d)
    return d[:e[6]]


if __name__ == '__main__':
    d = read_file(sys.argv[1])
    open(sys.argv[2], 'wb').write(d)
    print(len(d))
