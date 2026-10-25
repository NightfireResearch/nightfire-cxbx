"""compare.py reference other...: DetMath probe outputs (tools/detmath/detprobe.cpp) compared byte for byte with the first."""
import os
import sys

ref = sys.argv[1]
names = sorted(os.listdir(ref))
for other in sys.argv[2:]:
    if not os.path.isdir(other):
        print(f'{other}: missing')
        continue
    bad = []
    for n in names:
        p = os.path.join(other, n)
        if not os.path.exists(p) or open(p, 'rb').read() != open(os.path.join(ref, n), 'rb').read():
            a = open(os.path.join(ref, n), 'rb').read()
            b = open(p, 'rb').read() if os.path.exists(p) else b''
            diff = sum(1 for i in range(0, min(len(a), len(b)), 8) if a[i:i + 8] != b[i:i + 8])
            bad.append(f'{n[:-4]} ({diff} differ)')
    print(f'{other}: ' + ('identical to ' + ref if not bad else 'DIFFERS: ' + ', '.join(bad)))
