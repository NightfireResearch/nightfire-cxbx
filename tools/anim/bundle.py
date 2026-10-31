"""Level bundle (07xxxxxx.bin) directory walker - LoaderLoad 0xbe910 / FUN_000be630 / LoaderProcess 0xbe7b0.
Header 0x20 bytes: u32 dirSize, u32 numFiles, ... ; then dirSize bytes of entries {u32 len; u8 type; char name[]};
then the files' data back to back in entry order (each len bytes).
walk_dir() and trim() read bundles whose end is missing (old copies recovered from cached_filesys.dat).
"""
import struct, sys, collections


def walk(d):
    dirsize, n = struct.unpack_from('<II', d, 0)
    p = 0x20; data = 0x20 + dirsize
    for _ in range(n):
        ln = struct.unpack_from('<I', d, p)[0]; typ = d[p + 4]
        e = d.index(b'\0', p + 5); name = d[p + 5:e].decode(); p = e + 1
        yield typ, name, data, ln
        data += ln


def walk_dir(d):
    """the directory alone, tolerating a cut-off directory or data: [(type, name, offset, len)], complete"""
    dirsize, n = struct.unpack_from('<II', d, 0)
    out = []
    p = 0x20; data = 0x20 + dirsize
    for _ in range(n):
        if p + 5 > min(len(d), 0x20 + dirsize) or b'\0' not in d[p + 5:0x20 + dirsize]:
            return out, False
        ln = struct.unpack_from('<I', d, p)[0]; typ = d[p + 4]
        e = d.index(b'\0', p + 5); name = d[p + 5:e].decode('latin-1'); p = e + 1
        out.append((typ, name, data, ln))
        data += ln
    return out, True


def trim(d):
    """A cut-off bundle rebuilt from the entries whose data is all there (a recovered directory with a partial
    payload): (bundle bytes, [(type, name, len) missing])"""
    ents, _ = walk_dir(d)
    keep = [e for e in ents if e[2] + e[3] <= len(d)]
    missing = [(t, n, l) for t, n, o, l in ents if o + l > len(d)]
    dirb = b''.join(struct.pack('<IB', l, t) + n.encode('latin-1') + b'\0' for t, n, o, l in keep)
    head = struct.pack('<II', len(dirb), len(keep)) + d[8:0x20]
    return head + dirb + b''.join(d[o:o + l] for t, n, o, l in keep), missing


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    c = collections.Counter()
    for typ, name, off, ln in walk(d):
        c[typ] += 1
        if len(sys.argv) > 2 and str(typ) in sys.argv[2].split(','):
            print(typ, name, hex(off), ln)
    print(sorted(c.items()))
