"""Extracts every level bundle (07xxxxxx.bin) from the disc's filesys archives into LVL (see nfpaths.py), and
pulls each bundle's skeletons (SKELnnnn, loader type 6), skins (05*) and scripts (06*) out into LVL/skel,
LVL/skin and LVL/script. Sequences (04*) stay inside the bundles; decode_seq.py reads them from there.

The archive index is keyed by the CRC of the file name, so the bundles are found by trying their names.
"""
import os
import zlib

import bundle
import edl
from nfpaths import LVL

for sub in ('skel', 'skin', 'script'):
    os.makedirs(os.path.join(LVL, sub), exist_ok=True)

index = edl.entries()
names = ['%08x.bin' % h for h in range(0x07000000, 0x07001000)
         if zlib.crc32(('%08x.bin' % h).encode()) in index]
print('%d level bundles in the archive' % len(names))

for nm in names:
    p = os.path.join(LVL, nm)
    if not os.path.exists(p):
        open(p, 'wb').write(edl.read_file(nm))
    d = open(p, 'rb').read()
    for typ, name, off, ln in bundle.walk(d):
        if typ == 6:
            sub = 'skel'
        elif name.startswith('05'):
            sub = 'skin'
        elif name.startswith('06'):
            sub = 'script'
        else:
            continue
        open(os.path.join(LVL, sub, '%s_%s.bin' % (nm[:8], name)), 'wb').write(d[off:off + ln])
    print(nm, flush=True)
