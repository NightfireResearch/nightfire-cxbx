#!/usr/bin/env python3
"""parse_facial.py - 007 Nightfire (Eurocom action engine, Xbox) facial animation data.

Facial animation = 22 vertex morph targets (blend shapes) per head, weights driven by "morph sequences"
(0x04 files with header flag bit 3) that are played by ordinary animation scripts (0x06 files).
See spec-facial.md next to this file.

What it parses and validates (every byte accounted for):
  1. Morph sequences  04*.bin with (flags@0x0c & 8): header + N scalar tracks (N = u8@0x3a, 22 in every file)
     + zero padding.  Evaluates every channel at every frame exactly like AnimFrameCopy (0x13e50) /
     FUN_00013c70 (0x13c70): quintic polynomial in the ABSOLUTE t = frame-1.
  2. Facial scripts   06*.bin whose play-sequence commands reference a morph sequence.
  3. Morph targets    in level files 01*.bin: every Xbox ModelData block (magic 0x0645584B "KXE\\x06") with
     morphTargetCount (+0x24) != 0: 22 x morphVtxCount x SHORT3 deltas, validated by walking the whole block
     (vertices, indices, primitives, bone palettes, morph deltas) and checking only 0xFF alignment padding
     follows before the next level block header.
  4. Skins            05*.bin: numMorphs (file byte 0x12 -> sAnimSkin_tag+0x2a) and the rigged model hashcode,
     cross-referenced to the ModelData found in 3.

usage:
  python parse_facial.py [dumpdir]                    summary + validation of everything
  python parse_facial.py --bundles [lvldir]           same over every decompressed level bundle lvl/07*.bin
  python parse_facial.py [dumpdir] --weights 04000076 print the per-frame weights of one morph sequence
  python parse_facial.py [dumpdir] --csv out.csv --seq 04000076   same, as CSV
  python parse_facial.py [dumpdir] --obj outdir       write each head's morph targets as OBJ files
                                                       (base mesh + base+delta*1.0 per target)
"""
import glob
import os
import struct
import sys

from nfpaths import DUMP as DEFAULT_DUMP, LVL
NUM_MORPH_WEIGHTS = 22          # AnimScriptNew: AnimSeqNew(0x58) -> 22 floats
MODEL_MAGIC = 0x0645584B
_f = struct.Struct('<f')


def f32(x):
    return _f.unpack(_f.pack(x))[0]


def popcount(x):
    return bin(x).count('1')


# ---------------------------------------------------------------------------------------------------------------
# 1. Morph sequences
# ---------------------------------------------------------------------------------------------------------------
class MorphSeq:
    """0x04 sequence file with flags bit 3 (morph).

    0x00 u32 hashcode              0x04 u32 0      0x08 u32 0      0x0c u32 flags (0x08)
    0x10 f32[4] bounding sphere of the head (centre xyz, radius) - unused for morph seqs
    0x20 f32[6] (all 0)
    0x38 s16 numFrames             0x3a s16 numTracks (= number of morph channels, 22)
    0x3c u8 skeleton number the face was authored on (0 = male heads, 1 = female heads; overwritten at
            runtime by the skin's skeleton in FUN_00017960)   0x3d u8 0   0x3e u8 copy of 0x3c   0x3f u8 0
    0x40 tracks, then zero padding to a multiple of 32 bytes.
    """

    def __init__(self, b, name=''):
        self.name = name
        self.raw = b
        (self.hash, self.z4, self.z8, self.flags) = struct.unpack_from('<4I', b, 0)
        self.hdrFloats = struct.unpack_from('<10f', b, 0x10)
        self.numFrames, self.numTracks = struct.unpack_from('<hh', b, 0x38)
        self.skel, self.b3d, self.b3e, self.b3f = struct.unpack_from('<4B', b, 0x3c)
        self.tracks = []
        off = 0x40
        for _ in range(self.numTracks):
            tr, size = self._parse_track(b, off)
            self.tracks.append(tr)
            off += size
        self.end = off

    def _parse_track(self, b, off):
        """returns ([(endT, coeffs[6])...], sizeBytes).  Walks keys until the accumulated length reaches
        numFrames-1, like AnimProcessSeqData (0x138e0) does, and checks the size word."""
        w0 = struct.unpack_from('<I', b, off)[0]
        size = w0 & 0xffff
        cur = off + 4
        pending_hi = w0 >> 16        # parity 1: next key is the high half of the last u32 read
        use_hi = True
        acc = 0
        keys = []
        last = self.numFrames - 1
        while True:
            if use_hi:
                kw = pending_hi
            else:
                w = struct.unpack_from('<I', b, cur)[0]
                cur += 4
                kw = w & 0xffff
                pending_hi = w >> 16
            use_hi = not use_hi
            acc += kw >> 6
            mask = kw & 0x3f
            coeffs = [0.0] * 6
            for i in range(6):
                if mask & (1 << i):
                    coeffs[i] = struct.unpack_from('<f', b, cur)[0]
                    cur += 4
            keys.append((acc, coeffs, mask))
            if acc >= last:
                break
        # a track ends u32-aligned: if the last key used the low half, the high half is padding
        if cur - off != size:
            raise ValueError('%s: track at 0x%x: walked %d bytes, size word says %d' % (self.name, off, cur - off, size))
        return keys, size

    def value(self, track, frame):
        """weight of channel `track` at integer frame `frame` (1..numFrames), as AnimFrameCopy computes it"""
        t = int(frame)
        t = max(1, min(t, self.numFrames)) - 1   # clamp to [seq+0x86 (=1), numFrames], then t = frame-1
        for endT, c, _ in self.tracks[track]:
            if t <= endT:
                break
        # FUN_00013c70: Vec4_MulAdd with t^1..t^5 (the powers are stored as floats)
        tp = [1.0, f32(t), f32(t * t), f32(t ** 3), f32(t ** 4), f32(t ** 5)]
        v = 0.0
        for k in range(6):
            v = f32(v + f32(c[k] * tp[k]))
        return v

    def weights(self, frame):
        return [self.value(i, frame) for i in range(self.numTracks)]


def is_morph_seq(b):
    return len(b) >= 0x40 and (b[0x0c] & 8) != 0


# ---------------------------------------------------------------------------------------------------------------
# 2. Scripts (only the parts needed to find the facial ones; spec-script.md has the full format)
# ---------------------------------------------------------------------------------------------------------------
def script_seq_refs(b):
    """0x06 script: +4 s16 numFrames, +6 u16 (low byte = command count), commands from +8.
    command u16: low byte = type, high byte = extra u16 count.  Sizes (in u16): type 0 -> hi+4
    (type, startFrame, endFrame, seqHash&0xffff), types 1/2/4 -> hi+3, other -> hi (AnimProcessScriptCmds)."""
    numFrames, cnt = struct.unpack_from('<hH', b, 4)
    p = 8
    refs = []
    for _ in range(cnt & 0xff):
        w = struct.unpack_from('<H', b, p)[0]
        t, hi = w & 0xff, w >> 8
        if t == 0:
            st, en, s = struct.unpack_from('<HHH', b, p + 2)
            refs.append((st, en, 0x04000000 | s))
            sz = hi + 4
        elif t in (1, 2, 4):
            sz = hi + 3
        else:
            sz = hi
        p += sz * 2
    return numFrames, refs


# ---------------------------------------------------------------------------------------------------------------
# 3. Xbox ModelData (level-file geometry) and its morph targets
# ---------------------------------------------------------------------------------------------------------------
class ModelData:
    """psiCreateEntityGfx (0xde220) layout, file offsets relative to the magic:
    0x00 u32 magic 0x0645584B    0x04 u32 gfx hashcode (0x02xxxxxx)
    0x08 u32 vtxCnt              0x0c u32 idxCnt            0x10 u32 primCnt
    0x14 u32 ?                   0x18 u32 ?                 0x1c u32 bonePaletteCnt (0 = unskinned)
    0x20 u32 ? (0x2031)          0x24 u32 morphTargetCnt    0x28 u32 morphVtxCnt
    0x2c char[32] mesh name      0x4c..0x5f runtime (vtxBuffers, idxBuffer, prims ptr, palettes ptr,
                                                        morph stream base index) - zero on disk
    0x60 vertices   vtxCnt * (0x20 if skinned else 0x1c)  + 3 slack bytes   (data first, slack last: the
    ...  indices    idxCnt * u16                          + 3 slack          loader moves each section up to
    ...  prims      primCnt * 12                                             4-byte alignment in place)
    ...  palettes   bonePaletteCnt * 0x36
    ...  morph      morphTargetCnt * morphVtxCnt * SHORT3 (6 bytes) + 3 slack
    """

    def __init__(self, b, o):
        self.o = o
        h = struct.unpack_from('<11I', b, o)
        (self.magic, self.hash, self.vtxCnt, self.idxCnt, self.primCnt, self.u14, self.u18,
         self.palCnt, self.u20, self.morphCnt, self.morphVtxCnt) = h
        self.name = b[o + 0x2c:o + 0x4c].split(b'\0')[0].decode('latin-1')
        self.stride = 0x20 if self.palCnt else 0x1c
        p = o + 0x60
        self.vtxOff = p
        p += self.vtxCnt * self.stride + 3
        self.idxOff = p
        p += self.idxCnt * 2 + 3
        self.primOff = p
        p += self.primCnt * 12
        self.palOff = p
        p += self.palCnt * 0x36
        self.morphOff = p
        if self.morphCnt and self.morphVtxCnt:
            p += self.morphCnt * self.morphVtxCnt * 6 + 3
        self.end = p
        self.b = b

    def positions(self):
        return [struct.unpack_from('<3f', self.b, self.vtxOff + i * self.stride) for i in range(self.vtxCnt)]

    def indices(self):
        return struct.unpack_from('<%dH' % self.idxCnt, self.b, self.idxOff)

    def prims(self):
        """12-byte primitive: u16 tex0, u16 tex1 (0xffff none), u32 stripLen-2, u16 state flags
        (bit0 = morph on), u8 state-change mask (bit0 = (re)bind morph streams), u8 bone palette (1-based)"""
        return [struct.unpack_from('<HHIHBB', self.b, self.primOff + i * 12) for i in range(self.primCnt)]

    def morph_deltas(self, target):
        base = self.morphOff + target * self.morphVtxCnt * 6
        return [struct.unpack_from('<3h', self.b, base + i * 6) for i in range(self.morphVtxCnt)]

    def validate(self):
        errs = []
        b = self.b
        # after the block: only 0xFF padding to a u32 boundary, then a level block header or EOF
        q = self.end
        while q < len(b) and q % 4 and b[q] == 0xff:
            q += 1
        if q < len(b) and q % 4:
            errs.append('non-0xFF byte %02x before alignment at 0x%x' % (b[q], q))
        # every strip index in range; morph-enabled primitives only use vertices < morphVtxCnt
        idx = self.indices()
        s = 0
        morph_on = False
        for (t0, t1, n, fl, chg, pal) in self.prims():
            if chg & 1:
                morph_on = bool(fl & 1)
            seg = idx[s:s + n + 2]
            if max(seg) >= self.vtxCnt:
                errs.append('index out of range')
            if morph_on and max(seg) >= self.morphVtxCnt:
                errs.append('morph primitive uses vertex %d >= morphVtxCnt %d' % (max(seg), self.morphVtxCnt))
            s += n + 2
        if s != self.idxCnt:
            errs.append('strips use %d of %d indices' % (s, self.idxCnt))
        return errs


def find_models(b):
    out = []
    i = b.find(struct.pack('<I', MODEL_MAGIC))
    while i >= 0:
        out.append(ModelData(b, i))
        i = b.find(struct.pack('<I', MODEL_MAGIC), i + 4)
    return out


# ---------------------------------------------------------------------------------------------------------------
def load_dir(d):
    files = {}
    for fn in glob.glob(os.path.join(d, '*.bin')):
        base = os.path.basename(fn)[:-4]
        try:
            h = int(base, 16)
        except ValueError:
            continue
        files[h] = open(fn, 'rb').read()
    return files


def load_bundles(d):
    """Walk every decompressed level bundle lvl/07xxxxxx.bin (see bundle.py / spec-load.md) and return
    {hash: bytes} of the files this script cares about (04 morph seqs, 05 skins, 06 scripts, and 01 graphics
    files that hold a morph-target ModelData). Also returns {hash: set(bundles)} and a list of content
    conflicts (same hash, different bytes in two bundles)."""
    files, where, conflicts, gfx = {}, {}, [], []
    for fn in sorted(glob.glob(os.path.join(d, '07*.bin'))):
        bundle = os.path.basename(fn)[:-4]
        data = open(fn, 'rb').read()
        dirsize, n = struct.unpack_from('<II', data, 0)
        pp = 0x20
        off = 0x20 + dirsize
        for _ in range(n):
            ln = struct.unpack_from('<I', data, pp)[0]
            e = data.index(bytes(1), pp + 5)
            name = data[pp + 5:e].decode('latin-1')
            pp = e + 1
            blob = data[off:off + ln]
            off += ln
            try:
                h = int(name, 16)
            except ValueError:
                continue
            t = h >> 24
            keep = ((t == 4 and is_morph_seq(blob)) or t in (5, 6) or
                    (t == 1 and any(m.morphCnt for m in find_models(blob))))
            if not keep:
                continue
            if t == 1:
                gfx.append(('%08x/%s' % (h, bundle), h, blob))   # graphics packs differ per level: keep all
            if h in files and files[h] != blob:
                conflicts.append((h, bundle))
            files.setdefault(h, blob)
            where.setdefault(h, set()).add(bundle)
    return files, where, conflicts, gfx


def main(argv):
    args = [a for a in argv[1:]]
    d = DEFAULT_DUMP
    if args and not args[0].startswith('--'):
        d = args.pop(0)
    opts = {}
    while args:
        k = args.pop(0)
        opts[k] = args.pop(0) if args and not args[0].startswith('--') else True
    if '--bundles' in opts:
        files, where, conflicts, gfx = load_bundles(opts['--bundles'] if opts['--bundles'] is not True
                                               else LVL)
        nonGfx = [c for c in conflicts if c[0] >> 24 != 1]
        print('bundles: %d distinct relevant files; same-hash content conflicts: %d in 01 graphics packs (all copies '
              'checked below), %d elsewhere%s' % (len(files), len(conflicts) - len(nonGfx), len(nonGfx),
              (' e.g. ' + ', '.join('%08x in %s' % c for c in nonGfx[:5])) if nonGfx else ''))
    else:
        files = load_dir(d)
        gfx = [('%08x' % h, h, b) for h, b in sorted(files.items()) if h >> 24 == 1]

    # ---- 1. morph sequences
    seqs = {}
    nseq_ok = 0
    nseq = 0
    for h, b in sorted(files.items()):
        if h >> 24 != 4 or not is_morph_seq(b):
            continue
        nseq += 1
        name = '%08x' % h
        try:
            s = MorphSeq(b, name)
            pad = b[s.end:]
            errs = []
            if s.hash != h:
                errs.append('hash mismatch')
            if any(pad):
                errs.append('non-zero padding')
            if len(b) % 32:
                errs.append('size not multiple of 32')
            if s.numTracks != NUM_MORPH_WEIGHTS:
                errs.append('numTracks %d != 22 (would overflow the 0x58-byte buffer if > 22)' % s.numTracks)
            if any(s.hdrFloats[4:]) or s.z4 or s.z8 or s.b3d or s.b3f or s.skel != s.b3e:
                errs.append('unexpected non-zero header field')
            if errs:
                print('SEQ %s: %s' % (name, '; '.join(errs)))
            else:
                nseq_ok += 1
            seqs[h] = s
        except Exception as e:  # noqa
            print('SEQ %s: FAILED %s' % (name, e))
    print('morph sequences: %d/%d parse cleanly (every byte accounted for)' % (nseq_ok, nseq))
    for h, s in sorted(seqs.items()):
        lo = min(min(s.weights(f)) for f in range(1, s.numFrames + 1))
        hi = max(max(s.weights(f)) for f in range(1, s.numFrames + 1))
        active = [i for i in range(s.numTracks)
                  if any(abs(s.value(i, f)) > 0.01 for f in range(1, s.numFrames + 1))]
        nkeys = sum(len(t) for t in s.tracks)
        print('  %08x frames %5d skel %d keys %4d  weight range [%6.3f, %6.3f]  active channels %s'
              % (h, s.numFrames, s.skel, nkeys, lo, hi, active))

    # ---- 2. facial scripts
    print('\nfacial scripts (0x06 files playing a morph sequence):')
    for h, b in sorted(files.items()):
        if h >> 24 != 6:
            continue
        nf, refs = script_seq_refs(b)
        m = [r for r in refs if r[2] in seqs]
        if not m:
            continue
        body = [r for r in refs if r[2] not in seqs]
        print('  %08x frames %5d  morph %s%s' % (h, nf, ', '.join('%08x[%d..%d]' % (r[2], r[0], r[1]) for r in m),
                                                 ('  + body ' + ', '.join('%08x' % r[2] for r in body)) if body else ''))

    # ---- 3. morph targets in level files
    print('\nmorph target sets in Xbox ModelData blocks (level files 01*.bin):')
    models = {}
    nmod = nmod_ok = 0
    for label, h, b in gfx:
        for m in find_models(b):
            if not m.morphCnt:
                continue
            nmod += 1
            errs = m.validate()
            if not errs:
                nmod_ok += 1
            models.setdefault(m.hash, (h, m))
            nz = sum(1 for t in range(m.morphCnt) for dlt in m.morph_deltas(t) if any(dlt))
            mx = max(abs(c) for t in range(m.morphCnt) for dlt in m.morph_deltas(t) for c in dlt)
            print('  %s @0x%06x model %08x "%s" vtx %d, %d targets x %d vtx, deltas nonzero %d, max|d| %d %s'
                  % (label, m.o, m.hash, m.name, m.vtxCnt, m.morphCnt, m.morphVtxCnt, nz, mx,
                     'OK' if not errs else 'ERR ' + '; '.join(errs)))
    print('  %d/%d morph-target blocks validate' % (nmod_ok, nmod))

    # ---- 4. skins
    print('\nskins with morphs (05*.bin, byte 0x12 = numMorphs):')
    for h, b in sorted(files.items()):
        if h >> 24 != 5:
            continue
        nm = b[0x12]
        if not nm:
            continue
        found = [(mh, models[mh][0]) for mh in models if struct.pack('<I', mh) in b]
        print('  %08x numMorphs %d rigged %d -> %s' % (h, nm, b[0x10],
              ', '.join('model %08x in %08x.bin' % f for f in found) or 'model not in dump'))

    # ---- options
    sel = opts.get('--weights') or opts.get('--seq')
    if sel:
        s = seqs[int(sel, 16)]
        rows = [[f] + s.weights(f) for f in range(1, s.numFrames + 1)]
        if '--csv' in opts:
            with open(opts['--csv'], 'w') as fp:
                fp.write('frame,' + ','.join('w%d' % i for i in range(s.numTracks)) + '\n')
                for r in rows:
                    fp.write(','.join('%g' % x for x in r) + '\n')
            print('wrote', opts['--csv'])
        else:
            for r in rows:
                print('%5d ' % r[0] + ' '.join('%6.3f' % x for x in r[1:]))
    if '--obj' in opts:
        od = opts['--obj']
        os.makedirs(od, exist_ok=True)
        for mh, (fh, m) in models.items():
            pos = m.positions()
            idx = m.indices()
            faces = []
            s = 0
            for (_, _, n, _, _, _) in m.prims():
                strip = idx[s:s + n + 2]
                for k in range(len(strip) - 2):
                    a, bb, c = strip[k], strip[k + 1], strip[k + 2]
                    if a == bb or bb == c or a == c:
                        continue
                    faces.append((a, bb, c) if k % 2 == 0 else (bb, a, c))
                s += n + 2
            for t in [-1] + list(range(m.morphCnt)):
                p = list(pos)
                if t >= 0:
                    for i, dlt in enumerate(m.morph_deltas(t)):
                        # vertex shader: pos += delta * (weight / 32767); here weight = 1
                        p[i] = tuple(p[i][k] + dlt[k] / 32767.0 for k in range(3))
                fn = os.path.join(od, '%08x_%s.obj' % (mh, 'base' if t < 0 else 'morph%02d' % t))
                with open(fn, 'w') as fp:
                    for v in p:
                        fp.write('v %f %f %f\n' % v)
                    for f in faces:
                        fp.write('f %d %d %d\n' % (f[0] + 1, f[1] + 1, f[2] + 1))
        print('wrote OBJs to', od)


if __name__ == '__main__':
    main(sys.argv)
