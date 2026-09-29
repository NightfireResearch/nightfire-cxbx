"""Compare frames the game decoded (Release/anim_capture.bin, written by the AnimFrameCopy shadow test) with
decode_seq.py's decode of the same sequence and frame. Reports exact / near / off counts per kind."""
import os
import struct
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import decode_seq as ds

from nfpaths import LVL, REPO
cap = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'Release', 'anim_capture.bin')
data = open(cap, 'rb').read()

seqs = {}
for name, b in ds.iter_bundles(LVL):
    seqs[int(name[:8], 16)] = b
skels = ds.load_skeletons()

stats = Counter()
worst = []
p = 0
HDR = struct.Struct('<IfBBH3f3II')
while p + HDR.size <= len(data):
    h, frame, skel, morph, n, sx, sy, sz, u0, u1, u2, nbytes = HDR.unpack_from(data, p)
    p += HDR.size
    if p + nbytes > len(data):
        break
    out = struct.unpack_from('<%df' % (nbytes // 4), data, p)
    raw = data[p:p + nbytes]
    p += nbytes
    kind = 'morph' if morph else 'bones'
    if h not in seqs:
        stats[kind + ' missing seq'] += 1
        continue
    seq = ds.Sequence(seqs[h], '%08x' % h)
    used_mask = u0 | (u1 << 32) | (u2 << 64)
    if morph:
        mine = seq.frame(frame)
        game = list(out[:n])
        pairs = list(zip(game, mine))
    else:
        sk = skels[skel]
        used = [bool((used_mask >> b) & 1) for b in range(sk.numBones)]
        trans, quat = seq.frame(frame, sk, (sx, sy, sz), used)
        pairs = []
        for b in range(sk.numBones):
            if not used[b]:
                continue   # the game leaves these untouched: whatever was in the buffer
            pairs += list(zip(out[b * 3:b * 3 + 3], trans[b]))
            q0 = n * 3 + b * 4
            pairs += list(zip(out[q0:q0 + 4], quat[b]))
    exact = all(struct.pack('<f', a) == struct.pack('<f', b) for a, b in pairs)
    err = max((abs(a - b) for a, b in pairs), default=0.0)
    if exact:
        stats[kind + ' exact'] += 1
    elif err < 1e-5:
        stats[kind + ' last-bit'] += 1
    else:
        stats[kind + ' OFF'] += 1
        worst.append((err, '%08x' % h, frame))
    stats[kind + ' ' + ('skin' if used_mask != (1 << 96) - 1 else 'no skin')] += 1

for k in sorted(stats):
    print('%-24s %d' % (k, stats[k]))
for e in sorted(worst, reverse=True)[:10]:
    print('OFF', e)
