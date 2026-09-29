"""Level bundle (07xxxxxx.bin) directory walker - LoaderLoad 0xbe910 / FUN_000be630 / LoaderProcess 0xbe7b0.
Header 0x20 bytes: u32 dirSize, u32 numFiles, ... ; then dirSize bytes of entries {u32 len; u8 type; char name[]};
then the files' data back to back in entry order (each len bytes).
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


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    c = collections.Counter()
    for typ, name, off, ln in walk(d):
        c[typ] += 1
        if len(sys.argv) > 2 and str(typ) in sys.argv[2].split(','):
            print(typ, name, hex(off), ln)
    print(sorted(c.items()))
