#!/usr/bin/env python3
"""decode_seq.py - 007 Nightfire (Eurocom action engine, Xbox) animation sequence decoder.

Decodes a 0x04xxxxxx.bin sequence file into per-frame, per-bone local (parent-relative) rotation quaternions and
translations, reproducing AnimFrameCopy (Xbox 0x13e50) / FUN_00013c70 (0x13c70) / AnimFrameSet (0x15f80) /
AnimFrameBlend (0x12b40) / Quat_Slerp_Acc (0xd5e90).  See spec-seq.md next to this file for the format.

File layout (little endian):
  0x00 u32  hashcode (== file name)           | at runtime: ptr to track data (file+0x40)
  0x04 u32  0                                 | at runtime: byte size of the track data
  0x08 u32  0                                 | at runtime: decode cursor (= +0x00 at every AnimFrameCopy)
  0x0c u32  flags: bits0-2 distance table (walk/run cycles; the loader also forces 7 for a fixed list of hashes),
                   bit3 = morph/facial sequence (one scalar track per element, no skeleton); only low nibble kept
  0x10 f32[4] bounding sphere? centre xyz + radius (copied to seq+0x54..0x60 and to sAnimObject+0x10..0x1c)
  0x20 f32[6] bone-0 translation box: (minX,minY,maxZ),(maxX,maxY,minZ) -> seq+0x3c..0x50
  0x38 s16  numFrames (frames are numbered 1..numFrames, authored at 60 frames per second)
  0x3a s16  numTracks (u8 at 0x3a is also read as the morph element count)
  0x3c u8   skeleton number (index into SkeletonTable; SKELnnnn entry of the level bundle)
  0x3d u8   0
  0x3e u8   copy of 0x3c in every dumped file (-> seq+0x8c, never read)
  0x3f u8   0
  0x40 ...  numTracks tracks, then zero padding to a 32-byte multiple.

Track: u32-aligned stream of 16-bit words, read as u32 pairs (low half first).
  word 0      = byte size of the whole track (incl. this header u32)
  then keys   = u16 [ len:10 (bits 6..15) | mask:6 (bits 0..5) ]; after the u32 that holds a key's word come the
                float coefficients of that key (one f32 per set mask bit, c0 first).  When the key word is the
                high half of a u32 the coefficients follow those of the previous key.
  A key covers frames: keys are consumed until sum(len) >= t where t = frame-1.  Keys cover t in (prevSum, sum]
  (the first also covers t = 0).  The value is the quintic polynomial c0 + c1 t + c2 t^2 + ... + c5 t^5 evaluated
  at the ABSOLUTE t = frame - 1 (not segment-relative).

Tracks per bone (bone order 0..numBones-1 of the skeleton):
  if bone's bit set in skeleton translation mask:  tx, ty, tz, qx, qy, qz   (6 tracks)
  else:                                            qx, qy, qz               (3 tracks; t = skeleton default)
  qw = +sqrt(1-x2-y2-z2) (0 if <= 0); if the qz value >= 2.0 then qz -= 4 and qw is negative.
  Translations are multiplied by the skin scale (Glb_SkinScale) in game; bone 0's is then divided back (raw).
Morph sequences (flags bit 3): numElems (u8 at 0x3a) tracks, each one scalar weight per frame.

usage:
  python decode_seq.py <seq.bin> [--frame F] [--skel-bundle 07000001.bin] [--csv out.csv]
  python decode_seq.py --validate <dumpdir>        (parse + check every 04*.bin)
"""
import argparse
import glob
import math
import os
import struct
import sys
from collections import Counter


HERE = os.path.dirname(os.path.abspath(__file__))
from nfpaths import LVL
DEFAULT_BUNDLE = os.path.join(LVL, '07000001.bin')
_f = struct.Struct('<f')


def F32(x):
    """round a Python float to IEEE single precision (what every fstp float does)"""
    return _f.unpack(_f.pack(x))[0]


# ----------------------------------------------------------------------------------------------------------------
# Skeletons (loader type 6 "SKELnnnn" entries of a level bundle; AnimSkeletonProcess 0x11050)
#   0x00 s16 skeleton number   0x02 u8 numBones   0x03 u8 number of translation-animated bones (== popcount of mask)
#   0x04 u32[3] translation-animated bitmask (bone n -> word n>>5, bit n&31)
#   0x10 f32[numBones][3] default (bind) local translation per bone
# ----------------------------------------------------------------------------------------------------------------
class Skeleton:
    def __init__(self, b):
        self.num, self.numBones, self.numTransBones = struct.unpack_from('<hBB', b, 0)
        m = struct.unpack_from('<3I', b, 4)
        self.transMask = m[0] | (m[1] << 32) | (m[2] << 64)
        self.defTrans = [struct.unpack_from('<3f', b, 0x10 + i * 12) for i in range(self.numBones)]

    def has_trans(self, bone):
        return (self.transMask >> bone) & 1

    def num_tracks(self):
        return 3 * self.numBones + 3 * bin(self.transMask).count('1')


def walk_bundle(d):
    """Level bundle (07xxxxxx.bin) directory: header {u32 dirSize, u32 numFiles, ...} 0x20 bytes, entries
    {u32 len; u8 type; char name[]}, then the files back to back (from bundle.py by the loader agent)."""
    dirsize, n = struct.unpack_from('<II', d, 0)
    p = 0x20
    data = 0x20 + dirsize
    for _ in range(n):
        ln = struct.unpack_from('<I', d, p)[0]
        typ = d[p + 4]
        e = d.index(b'\0', p + 5)
        name = d[p + 5:e].decode()
        p = e + 1
        yield typ, name, data, ln
        data += ln


def load_skeletons(path=DEFAULT_BUNDLE):
    sk = {}
    if not path or not os.path.exists(path):
        return sk
    d = open(path, 'rb').read()
    for typ, name, off, ln in walk_bundle(d):
        if typ == 6:
            s = Skeleton(d[off:off + ln])
            sk[s.num] = s
    return sk


# ----------------------------------------------------------------------------------------------------------------
# Sequence file
# ----------------------------------------------------------------------------------------------------------------
class Key:
    __slots__ = ('length', 'mask', 'end', 'coef', 'word_off', 'coef_off')


class Track:
    __slots__ = ('offset', 'size', 'keys', 'pad_half', 'end')


class Sequence:
    HDR = 0x40

    def __init__(self, data, name=''):
        self.name = name
        self.data = data
        b = data
        (self.hashcode, self.rt_ptr, self.rt_size, self.flags) = struct.unpack_from('<IIII', b, 0)
        self.sphere = struct.unpack_from('<4f', b, 0x10)
        self.extra6 = struct.unpack_from('<6f', b, 0x20)
        self.numFrames, self.numTracks = struct.unpack_from('<hh', b, 0x38)
        self.numElems = b[0x3a]
        self.skelNum, self.b3d, self.b3e, self.b3f = struct.unpack_from('<4B', b, 0x3c)
        self.isMorph = bool(self.flags & 8)
        self.distFlags = self.flags & 7
        self.tracks = []
        self._parse_tracks()

    def _parse_tracks(self):
        """AnimProcessSeqData (0x138e0) walk, which also yields every coefficient."""
        b = self.data
        off = self.HDR
        last = self.numFrames - 1
        for _ in range(self.numTracks):
            tr = Track()
            tr.offset = off
            w0 = struct.unpack_from('<I', b, off)[0]
            tr.size = w0 & 0xffff
            cur = off + 4
            half = w0 >> 16          # the key word pending in the high half
            have_half = True
            acc = 0
            tr.keys = []
            while True:
                k = Key()
                if have_half:
                    kw = half
                    k.word_off = cur - 2
                    have_half = False
                else:
                    ww = struct.unpack_from('<I', b, cur)[0]
                    k.word_off = cur
                    cur += 4
                    kw = ww & 0xffff
                    half = ww >> 16
                    have_half = True
                k.length = kw >> 6
                k.mask = kw & 0x3f
                acc += k.length
                k.end = acc
                k.coef_off = cur
                c = [0.0] * 6
                m = k.mask
                for i in range(6):
                    if m & (1 << i):
                        c[i] = struct.unpack_from('<f', b, cur)[0]
                        cur += 4
                k.coef = c
                tr.keys.append(k)
                if acc >= last:
                    break
            # a pending high half-word is padding (ProcessSeqData skips it)
            tr.pad_half = half if have_half else None
            tr.end = cur
            self.tracks.append(tr)
            off += tr.size
        self.tracks_end = off

    # -- byte accounting --------------------------------------------------------------------------------------
    def check(self):
        """Returns a list of problems (empty == every byte accounted for)."""
        p = []
        b = self.data
        if self.hashcode != int(os.path.basename(self.name)[:8], 16) if self.name else False:
            p.append('hashcode != file name')
        if self.rt_ptr or self.rt_size:
            p.append('runtime words nonzero')
        if self.flags & ~0xf:
            p.append('flags high bits %x' % self.flags)
        pos = self.HDR
        for i, tr in enumerate(self.tracks):
            if tr.offset != pos:
                p.append('track %d offset' % i)
            if tr.end != tr.offset + tr.size:
                # allow one trailing pad half-word? (never happens in the dump)
                p.append('track %d size %d != parsed %d' % (i, tr.size, tr.end - tr.offset))
            if tr.pad_half:
                p.append('track %d unused half-word %04x nonzero' % (i, tr.pad_half))
            pos = tr.offset + tr.size
        tail = b[pos:]
        if any(tail):
            p.append('nonzero tail')
        if len(b) % 32:
            p.append('file size not a multiple of 32')
        if len(tail) >= 32:
            p.append('tail >= 32 bytes')
        return p

    # -- evaluation --------------------------------------------------------------------------------------------
    @staticmethod
    def _powers(frame):
        t = float(frame - 1)
        # x87: t^k chain kept in extended precision, each stored as float32. t is an integer so t^k is exact
        # in Python ints; round once to float32.
        ti = frame - 1
        return [F32(1.0), F32(t), F32(ti ** 2), F32(ti ** 3), F32(ti ** 4), F32(ti ** 5)]

    @staticmethod
    def _eval_key(k, pw):
        """FUN_00013c70: r = c0; r = f32(r + P1*c1); ... five Vec4_MulAdd steps, each rounded to float32."""
        c = k.coef
        r = F32(c[0])
        for i in range(1, 6):
            r = F32(float(pw[i]) * float(F32(c[i])) + float(r))
        return r

    def clamp_frame(self, frame, start=1, end=None):
        """AnimFrameCopy: frame = (float)(int)frameArg; clamp to [seq+0x86 start, seq+0x88 end]."""
        end = self.numFrames if end is None else end
        f = int(frame)                 # __ftol2 truncation
        if f < start:
            f = start
        if f > end:
            f = end
        return f

    def track_value(self, tr, frame):
        t = frame - 1
        for k in tr.keys:
            if t <= k.end:
                return self._eval_key(k, self._powers(frame))
        return self._eval_key(tr.keys[-1], self._powers(frame))  # unreachable for valid files

    def track_values_all(self, tr):
        """Every integer frame 1..numFrames (fast path)."""
        out = [0.0] * self.numFrames
        ki = 0
        keys = tr.keys
        for f in range(1, self.numFrames + 1):
            t = f - 1
            while t > keys[ki].end:
                ki += 1
            out[f - 1] = self._eval_key(keys[ki], self._powers(f))
        return out

    # -- frame decode (AnimFrameCopy) -------------------------------------------------------------------------
    def frame(self, frame, skel=None, skin_scale=(1.0, 1.0, 1.0), skin_used=None):
        """Returns (trans[nb][3], quat[nb][4] as x,y,z,w) for integer frame (clamped) - exactly the 28*nb byte
        buffer AnimFrameCopy writes (nb vec3 then nb quats), with bone 0's translation divided back by the skin
        scale as the game does.  For a morph sequence returns a list of numElems weights.
        skin_used: optional list of per-bone booleans (skin->boneHierarchy[n] & 0x80); unused bones are left
        untouched by the game - returned here as None."""
        f = self.clamp_frame(frame)
        vals = [self.track_value(tr, f) for tr in self.tracks]
        return self._assemble(vals, skel, skin_scale, skin_used)

    def all_frames(self, skel=None, skin_scale=(1.0, 1.0, 1.0)):
        cols = [self.track_values_all(tr) for tr in self.tracks]
        res = []
        for i in range(self.numFrames):
            res.append(self._assemble([c[i] for c in cols], skel, skin_scale, None))
        return res

    def _assemble(self, vals, skel, skin_scale, skin_used):
        if self.isMorph:
            return [float(v) for v in vals[:self.numElems]]
        if skel is None:
            raise ValueError('skeleton %d needed' % self.skelNum)
        sx, sy, sz = (F32(s) for s in skin_scale)
        nb = skel.numBones
        trans = [None] * nb
        quat = [None] * nb
        ti = 0
        for n in range(nb):
            used = True if skin_used is None else skin_used[n]
            if skel.has_trans(n):
                tx, ty, tz = vals[ti], vals[ti + 1], vals[ti + 2]
                ti += 3
                if used:
                    trans[n] = [F32(tx * sx), F32(ty * sy), F32(tz * sz)]
            elif used:
                d = skel.defTrans[n]
                trans[n] = [F32(F32(d[0]) * sx), F32(F32(d[1]) * sy), F32(F32(d[2]) * sz)]
            qx, qy, qz = vals[ti], vals[ti + 1], vals[ti + 2]
            ti += 3
            if used:
                neg = qz >= F32(2.0)
                if neg:
                    qz = F32(float(qz) - 4.0)
                s = 1.0 - (float(qx) * float(qx) + float(qy) * float(qy) + float(qz) * float(qz))
                w = 0.0 if s <= 0.0 else math.sqrt(s)
                if neg and s > 0.0:
                    w = -w
                quat[n] = [F32(qx), F32(qy), F32(qz), F32(w)]
        if trans[0] is not None:   # Vec_Mul(out[0], Glb_InvSkinScale)
            trans[0] = [F32(trans[0][0] * F32(1.0 / sx)), F32(trans[0][1] * F32(1.0 / sy)),
                        F32(trans[0][2] * F32(1.0 / sz))]
        return trans, quat

    # -- fractional frame (AnimFrameSet + AnimFrameBlend) -----------------------------------------------------
    def pose(self, frame, skel, accurate=True, skin_scale=(1.0, 1.0, 1.0)):
        """AnimFrameSet: a = frame floor, b = min(frame+1, end); t = frame - (int)frame;
        |t| <= 0.0002 -> a; t == 1 -> b; else trans lerp (moveTowardsValue) and quats Quat_Slerp_Acc
        (accurate=True, near objects) or Quat_Slerp (step at 0.5, objects further than 0xf0 in every viewport)."""
        t = float(F32(frame - int(frame)))
        if -0.0002 <= t <= 0.0002:
            return self.frame(frame, skel, skin_scale)
        nxt = float(F32(frame + 1.0))
        if nxt > self.numFrames:
            nxt = float(self.numFrames)
        if t == 1.0:
            return self.frame(nxt, skel, skin_scale)
        a = self.frame(frame, skel, skin_scale)
        b = self.frame(nxt, skel, skin_scale)
        if self.isMorph:
            return a
        return blend(a, b, t, accurate)


def move_towards(cur, target, speed):
    """moveTowardsValue 0x24000"""
    v = speed * (target - cur) + cur
    if target - cur < 0.0:
        return v if target <= v else target
    return v if v <= target else target


def slerp_acc(t, a, b):
    """Quat_Slerp_Acc 0xd5e90 (a = start, b = end), x,y,z,w; result not renormalised."""
    d = a[3] * b[3] + a[1] * b[1] + a[2] * b[2] + a[0] * b[0]
    neg = d < 0.0
    if neg:
        d = -d
    if 1.0 - d <= 0.01:
        sb = t
        sa = 1.0 - t
    else:
        th = math.acos(d)
        inv = 1.0 / math.sin(th)
        sb = math.sin(th * t) * inv
        sa = math.sin(th - th * t) * inv
    if neg:
        sa = -sa
    return [F32(sb * b[i] + sa * a[i]) for i in range(4)]


def blend(a, b, t, accurate=True):
    """AnimFrameBlend 0x12b40 without a bone list: 0 -> a, 1 -> b, else per bone trans moveTowardsValue and quat
    slerp.  None entries (skin-unused bones) stay None."""
    ta, qa = a
    tb, qb = b
    if t == 0.0:
        return a
    if t == 1.0:
        return b
    tr, q = [], []
    for n in range(len(ta)):
        if ta[n] is None or tb[n] is None:
            tr.append(ta[n]); q.append(qa[n]); continue
        tr.append([F32(move_towards(float(ta[n][i]), float(tb[n][i]), t)) for i in range(3)])
        if accurate:
            q.append(slerp_acc(t, qa[n], qb[n]))
        else:
            q.append(list(qa[n]) if t < 0.5 else list(qb[n]))
    return tr, q


def root_motion(seq, skel):
    """Per-frame root (bone 0) raw translation, as AnimGetFrameTransRaw 0x15e40 returns; AnimSeqTick turns the
    frame-to-frame difference into object movement and zeroes bone 0's translation in the pose."""
    return [fr[0][0] for fr in seq.all_frames(skel)]


def distance_table(seq, skel, axes=4, loop=False):
    """AnimDistanceTableCreate (Xbox FUN_00016950, PS2 0x1c4798); AnimSeqSet uses axes=4 (z only), loop=0.
    Returns [total, n, 0, cum1..cumn]."""
    n = seq.numFrames - 1 if loop else seq.numFrames
    out = [0.0, float(n), 0.0]
    tot = F32(0.0)
    for i in range(2, n + 2):
        j = i
        if not loop and n < i:
            j = 2
        a = seq.frame(j, skel)[0][0]
        b = seq.frame(j - 1, skel)[0][0]
        d = [float(F32(a[k] - b[k])) for k in range(3)]
        for k in range(3):
            if not (axes >> k) & 1:
                d[k] = 0.0
        m = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
        if m < 1e-5:
            m = 0.01
        tot = F32(tot + F32(m))
        out.append(float(tot))
    out[0] = float(tot)
    return out


# ----------------------------------------------------------------------------------------------------------------
def iter_dump(dumpdir):
    for fn in sorted(glob.glob(os.path.join(dumpdir, '04*.bin'))):
        yield os.path.basename(fn), open(fn, 'rb').read()


def iter_bundles(lvldir):
    """every 04xxxxxx entry of every level bundle, deduplicated by (name, content)"""
    import hashlib
    seen = {}
    for bp in sorted(glob.glob(os.path.join(lvldir, '07*.bin'))):
        d = open(bp, 'rb').read()
        for typ, name, off, ln in walk_bundle(d):
            if not name.startswith('04'):
                continue
            b = d[off:off + ln]
            h = hashlib.sha1(b).hexdigest()
            if name in seen:
                if seen[name] != h:
                    print('NOTE %s differs between bundles (%s)' % (name, os.path.basename(bp)))
                continue
            seen[name] = h
            yield name, b


def validate(source, bundle, flicker_deg=90.0):
    skels = load_skeletons(bundle)
    stats = Counter()
    masks = Counter()
    keylens = Counter()
    skelcount = Counter()
    frames = []
    bad = []
    worst_norm = 0.0
    n_over = 0
    n_quats = 0
    worst_ang = (0.0, '')
    worst_jump = (0.0, '')
    flick = Counter()
    total_bytes = 0
    for name, b in source:
        total_bytes += len(b)
        try:
            s = Sequence(b, name)
        except Exception as e:  # noqa
            bad.append((name, 'parse: %r' % e)); continue
        probs = s.check()
        frames.append(s.numFrames)
        for tr in s.tracks:
            for k in tr.keys:
                masks[k.mask] += 1
                keylens[k.length] += 1
        if s.isMorph:
            stats['morph'] += 1
            if s.numElems != s.numTracks:
                probs.append('morph elems %d != tracks %d' % (s.numElems, s.numTracks))
        else:
            stats['bone'] += 1
            sk = skels.get(s.skelNum)
            skelcount[s.skelNum] += 1
            if sk is None:
                probs.append('no skeleton %d' % s.skelNum)
            elif sk.num_tracks() != s.numTracks:
                probs.append('tracks %d != skeleton %d expects %d' % (s.numTracks, s.skelNum, sk.num_tracks()))
            else:
                fr = s.all_frames(sk)
                prevq = prevt = None
                for fi, (tr, q) in enumerate(fr):
                    for qq in q:
                        nn = math.sqrt(sum(float(x) ** 2 for x in qq))
                        n_quats += 1
                        if nn - 1.0 > 1e-3:
                            n_over += 1
                        worst_norm = max(worst_norm, abs(nn - 1.0))
                    if prevq is not None:
                        for n in range(len(q)):
                            d = abs(sum(float(q[n][i]) * float(prevq[n][i]) for i in range(4)))
                            ang = 2 * math.degrees(math.acos(min(1.0, d)))
                            if ang > flicker_deg:
                                flick[name] += 1
                            if ang > worst_ang[0]:
                                worst_ang = (ang, '%s f%d bone %d' % (name, fi + 1, n))
                        for n in range(1, len(tr)):
                            j = max(abs(float(tr[n][i]) - float(prevt[n][i])) for i in range(3))
                            if j > worst_jump[0]:
                                worst_jump = (j, '%s f%d bone %d' % (name, fi + 1, n))
                    prevq = q
                    prevt = tr
        if probs:
            bad.append((name, '; '.join(probs)))
        else:
            stats['clean'] += 1
    print('sequences', len(frames), 'bytes', total_bytes, dict(stats))
    print('frames: min %d max %d  total %d' % (min(frames), max(frames), sum(frames)))
    print('skeletons used (bone seqs):', dict(sorted(skelcount.items())))
    print('key masks:', dict(sorted(masks.items())))
    print('key lengths: max %d, top %s' % (max(keylens), keylens.most_common(8)))
    print('quaternions %d, |q|-1 > 1e-3: %d (x2+y2+z2 > 1 so w clamped to 0), max ||q|-1| %.3g' %
          (n_quats, n_over, worst_norm))
    print('largest frame-to-frame rotation: %.2f deg at %s' % worst_ang)
    print('largest frame-to-frame non-root translation step: %.4f at %s' % worst_jump)
    print('frame steps > %g deg: %d in %d sequences; worst: %s' %
          (flicker_deg, sum(flick.values()), len(flick), flick.most_common(8)))
    for n, p in bad:
        print('PROBLEM', n, p)
    return not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file', nargs='?')
    ap.add_argument('--validate', metavar='DUMPDIR', help='validate every 04*.bin in a folder')
    ap.add_argument('--validate-bundles', metavar='LVLDIR', help='validate every 04 entry of every 07*.bin bundle')
    ap.add_argument('--skel-bundle', default=DEFAULT_BUNDLE)
    ap.add_argument('--frame', type=float)
    ap.add_argument('--csv')
    a = ap.parse_args()
    if a.validate:
        sys.exit(0 if validate(iter_dump(a.validate), a.skel_bundle) else 1)
    if a.validate_bundles:
        sys.exit(0 if validate(iter_bundles(a.validate_bundles), a.skel_bundle) else 1)
    if not a.file:
        ap.error('file or --validate needed')
    s = Sequence(open(a.file, 'rb').read(), os.path.basename(a.file))
    print('hash %08x flags %x frames %d tracks %d skel %d morph %s sphere %s' %
          (s.hashcode, s.flags, s.numFrames, s.numTracks, s.skelNum, s.isMorph, s.sphere))
    print('problems:', s.check() or 'none')
    sk = None if s.isMorph else load_skeletons(a.skel_bundle).get(s.skelNum)
    if a.frame is not None:
        r = s.pose(a.frame, sk)
        if s.isMorph:
            print(r)
        else:
            for n, (t, q) in enumerate(zip(*r)):
                print(n, t and [round(float(x), 5) for x in t], q and [round(float(x), 5) for x in q])
    if a.csv:
        with open(a.csv, 'w') as f:
            if s.isMorph:
                f.write('frame,' + ','.join('w%d' % i for i in range(s.numElems)) + '\n')
                for fi, w in enumerate(s.all_frames()):
                    f.write('%d,' % (fi + 1) + ','.join('%.7g' % x for x in w) + '\n')
            else:
                f.write('frame,bone,tx,ty,tz,qx,qy,qz,qw\n')
                for fi, (tr, q) in enumerate(s.all_frames(sk)):
                    for n in range(sk.numBones):
                        f.write('%d,%d,' % (fi + 1, n) + ','.join('%.7g' % float(x) for x in list(tr[n]) + list(q[n])) + '\n')


if __name__ == '__main__':
    main()
