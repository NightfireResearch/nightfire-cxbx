"""Parse Nightfire skin files (hashcode type 0x05, "%8.8x.bin"). Byte layout from AnimProcessSkinData
(Xbox 0x144c0, PS2 0x1bdc28); needs the skeleton table (parse_skeleton.py) for numBones.

  +0x00 u32  hashcode (own)
  +0x04 f32  skinScale[3]
  +0x10 u8   numRigged       (skin+0x28)  rigged (skinned) body celglists
  +0x11 u8   numDiscrete     (skin+0x29)  rigid per-bone celglists
  +0x12 u8   numMorphs       (skin+0x2a)
  +0x13 u8   numDatums       (skin+0x2b)
  +0x14 u8   skeletonNum     (skin+0x2c)
  +0x15 u8   bone[numBones]  (skin+0x20)  bit7 = bone used by this skin, bits0-6 = parent bone (0x7f = root)
        u8   discreteBone[numDiscrete]    (skin+0x0c, only if numDiscrete)
        align 4 (only if numDiscrete)
        u32  discreteHash[numDiscrete]    (-> skin+0x08 celglist*[], 0xffffffff -> NULL)
        align 4 (only if numRigged)
        u32  riggedHash[numRigged]        (-> skin+0x04 celglist*[]; 0xffffffff -> NULL; bit 0x100000 -> sleeve)
        {vec3 t; quat q(xyzw)}[numBones]  28 bytes each, only if numRigged (-> skin+0x24 _MATRIX[numBones])
        datum[numDatums]  36 bytes: {s32 id; s32 bone; vec3 t; quat q(xyzw)}   (skin+0x1c) - NOT aligned
        zero padding to a multiple of 32

usage: python parse_skin.py [-v] [files...]   default: the dump folder's 05*.bin and lvl/skin/*.bin
"""
import struct, sys, glob, os, math
import parse_skeleton

HERE = os.path.dirname(os.path.abspath(__file__))
from nfpaths import DUMP, LVL


def parse(d, skels):
    s = {}
    p = 0
    s['hash'], = struct.unpack_from('<I', d, 0)
    s['scale'] = struct.unpack_from('<3f', d, 4)
    s['numRigged'], s['numDiscrete'], s['numMorphs'], s['numDatums'], s['skel'] = d[0x10:0x15]
    nb = skels[s['skel']]['numBones']
    s['numBones'] = nb
    p = 0x15
    s['bones'] = list(d[p:p + nb]); p += nb
    s['discreteBone'] = []; s['discreteHash'] = []; s['riggedHash'] = []; s['bind'] = []; s['datums'] = []
    s['padding_used'] = []
    if s['numDiscrete']:
        s['discreteBone'] = list(d[p:p + s['numDiscrete']]); p += s['numDiscrete']
        a = (-p) & 3; s['padding_used'].append(d[p:p + a]); p += a
        s['discreteHash'] = list(struct.unpack_from('<%dI' % s['numDiscrete'], d, p)); p += 4 * s['numDiscrete']
    if s['numRigged']:
        a = (-p) & 3; s['padding_used'].append(d[p:p + a]); p += a
        s['riggedHash'] = list(struct.unpack_from('<%dI' % s['numRigged'], d, p)); p += 4 * s['numRigged']
        for i in range(nb):
            v = struct.unpack_from('<7f', d, p); p += 28
            s['bind'].append((v[:3], v[3:]))
    for i in range(s['numDatums']):
        v = struct.unpack_from('<ii7f', d, p); p += 36
        s['datums'].append(dict(id=v[0], bone=v[1], t=v[2:5], q=v[5:9]))
    s['end'] = p
    s['tail'] = d[p:]
    return s


def world_bind(s, skel):
    """world position of each bone in bind pose = sum of skeleton offsets * scale along the skin's parent chain"""
    out = []
    for b in range(s['numBones']):
        par = s['bones'][b] & 0x7f
        o = [skel['offsets'][b][k] * s['scale'][k] for k in range(3)]
        if par == 0x7f:
            out.append(o)
        else:
            out.append([out[par][k] + o[k] for k in range(3)])  # parents always precede children (checked)
    return out


def check(d, skels):
    errs = []; notes = []
    s = parse(d, skels)
    if s['end'] > len(d): errs.append('overrun by %d' % (s['end'] - len(d)))
    if any(s['tail']): errs.append('nonzero tail %s' % s['tail'][:16].hex())
    if len(d) % 32 or len(d) - s['end'] >= 32: errs.append('tail length %d (file %d)' % (len(d) - s['end'], len(d)))
    if any(any(x) for x in s['padding_used']): errs.append('nonzero alignment bytes')
    for b, v in enumerate(s['bones']):
        par = v & 0x7f
        if par != 0x7f and par >= b: errs.append('bone %d parent %d not earlier' % (b, par))
    if s['bones'] and s['bones'][0] & 0x7f != 0x7f: errs.append('bone 0 not root')
    roots = [b for b, v in enumerate(s['bones']) if v & 0x7f == 0x7f]
    if roots != [0]: notes.append('roots %s' % roots)
    unused = [b for b, v in enumerate(s['bones']) if not v & 0x80]
    if unused: notes.append('unused bones %s' % unused)
    for b in s['discreteBone']:
        if b >= s['numBones']: errs.append('discrete bone %d out of range' % b)
    for h in s['discreteHash'] + s['riggedHash']:
        if h != 0xffffffff and h >> 24 != 0x02: errs.append('hash %08x not type 02' % h)
    for dt in s['datums']:
        if not (-1 <= dt['bone'] < s['numBones']): errs.append('datum bone %d' % dt['bone'])
        n = math.sqrt(sum(x * x for x in dt['q']))
        if abs(n - 1) > 1e-3: errs.append('datum quat norm %.4f' % n)
    if s['bind']:
        wb = world_bind(s, skels[s['skel']])
        worst = 0
        for b, (t, q) in enumerate(s['bind']):
            n = math.sqrt(sum(x * x for x in q))
            if abs(n - 1) > 1e-3: errs.append('bind quat %d norm %.4f' % (b, n))
            # inverse bind: rotating the world bind position by q and adding t should give ~0
            x, y, z, w = q
            px, py, pz = wb[b]
            # row-vector convention of Quat_QuatTransToMat: v' = v*R + t
            m = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
            r = [px * m[0] + py * m[3] + pz * m[6] + t[0], px * m[1] + py * m[4] + pz * m[7] + t[1],
                 px * m[2] + py * m[5] + pz * m[8] + t[2]]
            if s['bones'][b] & 0x80:   # only bones the skin uses
                worst = max(worst, max(abs(c) for c in r))
        s['invbind_residual'] = worst
        if worst > 1e-3:
            notes.append('inverse bind differs from the skeleton bind pose by up to %.4f (skin-specific bind)' % worst)
    return s, errs, notes


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    verbose = '-v' in sys.argv
    files = args or sorted(glob.glob(DUMP + '/05*.bin')) + sorted(glob.glob(LVL + '/skin/*.bin'))
    skels = parse_skeleton.load_table()
    ok = 0; byname = {}
    for f in files:
        d = open(f, 'rb').read()
        s, errs, notes = check(d, skels)
        ok += not errs
        name = os.path.basename(f)
        key = '%08x' % s['hash']
        if key in byname and byname[key] != d: notes.append('DIFFERS from another copy')
        byname.setdefault(key, d)
        if verbose or errs:
            print('%s %08x skel=%d bones=%d rigged=%d discrete=%d morphs=%d datums=%d scale=%s size=%d end=%d%s' % (
                name, s['hash'], s['skel'], s['numBones'], s['numRigged'], s['numDiscrete'], s['numMorphs'],
                s['numDatums'], tuple(round(x, 4) for x in s['scale']), len(d), s['end'],
                ' invbind_resid=%.2g' % s['invbind_residual'] if 'invbind_residual' in s else ''))
            if verbose:
                print('   bones', ' '.join('%02x' % b for b in s['bones']))
                if s['discreteHash']:
                    print('   discrete', list(zip(s['discreteBone'], ['%08x' % h for h in s['discreteHash']])))
                if s['riggedHash']:
                    print('   rigged', ['%08x' % h for h in s['riggedHash']])
                for dt in s['datums']:
                    print('   datum id=%d bone=%d t=%s q=%s' % (dt['id'], dt['bone'], tuple(round(x, 4) for x in dt['t']),
                                                            tuple(round(x, 4) for x in dt['q'])))
            for e in errs: print('   ERROR', e)
            for n in notes: print('   note', n)
    print('%d/%d skin files parse cleanly (every byte accounted for); %d distinct skins' % (ok, len(files), len(byname)))


if __name__ == '__main__':
    main()
