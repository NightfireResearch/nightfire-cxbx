"""Parse Nightfire skeleton records (level-bundle dir entries of type 6, named SKELnnnn).
Layout from AnimSkeletonProcess (Xbox 0x11050) and AnimFrameCopy (Xbox 0x13e50):
  +0x00 s16  skeletonIdx          -> SkeletonTable[idx] = record  (0x1d7578, 128 slots)
  +0x02 u8   numBones
  +0x03 u8   unknown (always 1 in the data; no reader found on Xbox)
  +0x04 u32  transMask[3]          bit b = bone b has animated translation in sequences
                                   (AnimFrameCopy: TEST [skel + 4 + (b>>5)*4], 1<<(b&31)); up to 96 bones
  +0x10 vec3 boneOffset[numBones]  default local translation (bind pose, relative to parent, unscaled);
                                   used by AnimFrameCopy for bones whose mask bit is clear, times Glb_SkinScale
  then zero padding to a multiple of 32 bytes (bundle entries are 32-byte padded)

usage: python parse_skeleton.py [files...]      (default: lvl/skel/*.bin)
"""
import struct, sys, glob, os
from nfpaths import LVL


def parse(d):
    idx, nb, b3 = struct.unpack_from('<hBB', d, 0)
    mask = struct.unpack_from('<3I', d, 4)
    offs = [struct.unpack_from('<3f', d, 16 + 12 * i) for i in range(nb)]
    end = 16 + 12 * nb
    pad = d[end:]
    return dict(idx=idx, numBones=nb, byte3=b3, mask=mask, offsets=offs, end=end, pad=pad)


def check(d):
    s = parse(d)
    errs = []
    if s['end'] > len(d): errs.append('truncated')
    if any(s['pad']): errs.append('nonzero padding')
    if len(d) % 32: errs.append('size not 32-aligned')
    if len(d) - s['end'] >= 32: errs.append('more than 31 bytes of padding')
    if s['numBones'] > 96: errs.append('numBones > 96 (mask too small)')
    hi = [b for b in range(96) if s['mask'][b >> 5] >> (b & 31) & 1 and b >= s['numBones']]
    if hi: errs.append('mask bits beyond numBones %s' % hi)
    return s, errs


def load_table(files=None):
    """skeletonIdx -> parsed skeleton, checking every copy across levels is identical"""
    t = {}
    for f in files or sorted(glob.glob(os.path.join(LVL, 'skel', '*.bin'))):
        d = open(f, 'rb').read()
        s = parse(d)
        if s['idx'] in t and t[s['idx']]['raw'] != d:
            t[s['idx']].setdefault('variants', []).append(f)
        else:
            s['raw'] = d; s['file'] = f
            t.setdefault(s['idx'], s)
    return t


if __name__ == '__main__':
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(LVL, 'skel', '*.bin')))
    ok = 0
    for f in files:
        d = open(f, 'rb').read()
        s, errs = check(d)
        name = os.path.basename(f)
        if 'SKEL' in name and int(name.split('SKEL')[1][:4]) != s['idx']:
            errs.append('name/idx mismatch')
        ok += not errs
        if errs or '-v' in sys.argv:
            print(name, s['idx'], s['numBones'], s['byte3'], ['%08x' % m for m in s['mask']], errs)
    t = load_table(files)
    print('%d/%d skeleton records clean; %d distinct skeleton indices' % (ok, len(files), len(t)))
    for i in sorted(t):
        s = t[i]
        bits = [b for b in range(s['numBones']) if s['mask'][b >> 5] >> (b & 31) & 1]
        print('  skel %3d: %3d bones, byte3=%d, translated bones %s%s' % (
            i, s['numBones'], s['byte3'], bits, '  VARIANTS: %d' % len(s['variants']) if 'variants' in s else ''))
