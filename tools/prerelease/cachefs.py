"""The action engine's hard-disk cache of its file archive, z:\\cached_filesys.dat (FS_Init 0xe2a40,
FS_StateMachineIterate 0xe2d90). docs/prerelease-cached-filesys.md has the format.

    read_index(path)            -> (header words, [Entry]) of a cache or of filesys.d00 (the same 0x13800-byte block)
    index_crc_ok(path)          the cache header's +0x04 CRC
    find_sections(f, size)      offsets of every EDL section start (0x800-aligned) in the file
    read_section(f, start, limit[, max_blocks])
                                -> (bytes, blocks decoded, blocks in the header, complete); stops at a block
                                   whose header is gone or that fails to decode; a block that crosses `limit`
                                   is decoded as far as its bytes go
    name_crc(name)              the archive's key: the zlib CRC-32 of the lower-cased path, '\\' separators
"""
import collections
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'anim'))
import edl  # noqa: E402

INDEX_SIZE = 0x13800
ENTRY_SIZE = 0x26
SECTOR = 0x800
SECTION_MAGIC = 0x0443584b
PARTIAL_TAIL = 0x200

FLAG_CACHED = 1
FLAG_COMPRESSED = 2
FLAG_NEVER_CACHED = 4

_need = edl.Bits.need


def _need_bounded(self, k):
    # edl.Bits reads zeros past the end, which an overwritten block can turn into an endless loop
    if self.p > len(self.d) + 8:
        raise EOFError
    _need(self, k)


edl.Bits.need = _need_bounded

Entry = collections.namedtuple('Entry', 'name_crc data_crc verified_at offset stored_len length flags filesys_idx '
                                        'offset_in_part')


def name_crc(name):
    return zlib.crc32(name.lower().replace('/', '\\').encode())


def read_index(path):
    with open(path, 'rb') as f:
        h = f.read(INDEX_SIZE)
    words = struct.unpack_from('<8I', h, 0)
    n = struct.unpack_from('<i', h, 0x18)[0]
    out = []
    for i in range(n):
        crc, dcrc, ver, lo, hi, slen, ln, fl, part, inpart = struct.unpack_from('<7IHII', h, 0x20 + i * ENTRY_SIZE)
        out.append(Entry(crc, dcrc, ver, lo | hi << 32, slen, ln, fl, part, inpart))
    return words, out


def index_crc_ok(path):
    with open(path, 'rb') as f:
        h = bytearray(f.read(INDEX_SIZE))
    stored = struct.unpack_from('<I', h, 4)[0]
    h[4:8] = bytes(4)
    return zlib.crc32(bytes(h)) == stored


def find_sections(f, size):
    magic = struct.pack('<I', SECTION_MAGIC)
    out = []
    step = 1 << 24
    for base in range(0, size, step):
        f.seek(base)
        d = f.read(step)
        for j in range(0, len(d), SECTOR):
            if d[j:j + 4] == magic:
                out.append(base + j)
    return out


def read_section(f, start, limit, max_blocks=None):
    f.seek(start)
    ident, nblocks = struct.unpack('<II', f.read(8))
    p = start + 8
    out = bytearray()
    st = edl.HuffState()
    done = 0
    for _ in range(nblocks if max_blocks is None else min(nblocks, max_blocks)):
        f.seek(p)
        hd = f.read(12)
        if len(hd) < 12 or hd[:3] != b'EDL':
            break
        algo = hd[3] & 0x7f
        csz, dsz = struct.unpack_from('<II', hd, 4)
        cut = p + csz > limit
        f.seek(p)
        blk = f.read(limit - p if cut else csz)
        before = len(out)
        try:
            if algo == 1:
                edl.inflate_huffman(blk, st, out)
            elif algo == 2:
                out += edl.inflate_bitwise(blk)
            else:
                out += blk[12:12 + dsz]
        except (EOFError, IndexError, KeyError, TypeError, ValueError):
            if cut and algo == 1:
                # the block's start is intact: keep what it gave, less the tail decoded from the zero fill
                del out[max(before, len(out) - PARTIAL_TAIL):]
            else:
                del out[before:]
            break
        if cut or len(out) - before != dsz:
            del out[before:]
            break
        p += csz
        done += 1
    return bytes(out), done, nblocks, done == nblocks
