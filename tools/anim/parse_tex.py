#!/usr/bin/env python3
"""Textures and materials in the action engine's graphics packs (01xxxxxx files) - see docs/anim/spec-texture.md.

A graphics pack is a run of level blocks, each headed by a u32 whose top byte is the block id and whose low 24
bits are the block size, header included (parsemap_parsenextblock 0xa6a80). The texture blocks:

  0x10  texture header list  12-byte records {u8 flags, u8 ?, u16 width-1, u16 height-1, u8 numFrames, u8 fps,
                             u32 hashcode (0x03xxxxxx or -1)}; the loader overwrites the hashcode with a pointer
                             to the texture's data block (parsemap_handle_block_id case 0x10, 0xa6110)
  0x0f  "palette" list       8-byte records {u32 ?, u32 hashcode}, one per texture; nothing on Xbox reads them
  0x18  texture data         one per header record, in order: a 'KXT\\6' texture (header, then the pixels of
                             every frame and mip level) or a 'TD' duplicate that reuses the pixels of a texture
                             loaded earlier in the level (by the level-wide load index in its low 16 bits)

Primitives in the pack's KXE meshes name textures by their index into the pack's 0x10 list (tex0 = base texture,
tex1 = second-stage texture; 0xffff = none). psiCreateEntityGfx 0xde220 rebases them onto the level-wide list.

usage:
  python parse_tex.py list   [bundle 07xxxxxx | pack file ...]     list every texture of the packs
  python parse_tex.py png    OUTDIR [--bundle 07xxxxxx] [--pack 01xxxxxx] [--name substr] [--frames] [--mips]
  python parse_tex.py validate [--png OUTDIR]     decode every distinct texture in every bundle and the dump
  python parse_tex.py skin   05xxxxxx [--png OUTDIR]   the textures and materials of a skin's meshes
With no bundle given, 'list' and 'png' use the game's dump folder (TD duplicates are then resolved through the
level bundle that holds an identical copy of the pack).
"""
import glob
import hashlib
import os
import re
import struct
import sys
import zlib
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bundle            # noqa: E402
try:
    import parse_gfx     # the container walk and the mesh parser (spec-mesh.md)
except ImportError:      # pragma: no cover
    parse_gfx = None
from nfpaths import DUMP, LVL   # noqa: E402

KXT_MAGIC = 0x0654584B   # 'KXT\6'
KXE_MAGIC = 0x0645584B   # 'KXE\6' (mesh)
TD_TAG = 0x4454          # 'TD' in the top half of the first word

# ---------------------------------------------------------------------------------------------------------------
# Level blocks
# ---------------------------------------------------------------------------------------------------------------


def blocks(d):
    """Yield (offset, id, size) for every level block of a pack, up to and including the end block 0x1d.
    The container walk is parse_gfx.walk (docs/anim/spec-mesh.md); the fallback below is the same walk."""
    if parse_gfx is not None:
        for b in parse_gfx.walk(d):
            yield b.off, b.type, b.size
        return
    p = 4
    while p + 4 <= len(d):
        h = struct.unpack_from('<I', d, p)[0]
        t, s = h >> 24, h & 0xffffff
        yield p, t, s
        if t == 0x1d or s < 4:
            return
        p += s


# ---------------------------------------------------------------------------------------------------------------
# Formats. formatType (KXT +0x20) and alphaBits (+0x14) -> RegisterTexture's type -> Xbox D3DFORMAT
# (psiCreateMapTextures 0xddf90, RegisterTexture 0xe4400)
# ---------------------------------------------------------------------------------------------------------------

XFMT = {0x02: 'A1R5G5B5', 0x04: 'A4R4G4B4', 0x06: 'A8R8G8B8', 0x07: 'X8R8G8B8', 0x0b: 'P8', 0x0c: 'DXT1',
        0x0e: 'DXT3', 0x0f: 'DXT5', 0x10: 'LIN_A1R5G5B5', 0x12: 'LIN_A8R8G8B8', 0x1d: 'LIN_A4R4G4B4',
        0x1e: 'LIN_X8R8G8B8', 0x24: 'A16B16G16R16'}


def register_type(formatType, alphaBits):
    """psiCreateMapTextures: the type number it passes to RegisterTexture."""
    if formatType == 0:
        return 1 if alphaBits == 4 else (2 if alphaBits == 8 else 0)
    return {2: 4, 3: 5, 4: 6, 5: 7}.get(formatType, 3)


def d3d_format(rtype, linear):
    """RegisterTexture: its type number (and the linear flag) -> Xbox D3DFORMAT."""
    return {1: 0x1d if linear else 0x04, 2: 0x12 if linear else 0x06, 3: 0x0c, 4: 0x0e, 5: 0x0e, 6: 0x0f,
            7: 0x0f, 8: 0x0b, 9: 0x24, 10: 0x1e if linear else 0x07}.get(rtype, 0x10 if linear else 0x02)


def level_size(fmt, w, h):
    if fmt in (0x0c, 0x0e, 0x0f):
        w, h = max(w, 4), max(h, 4)
        return w * h // (2 if fmt == 0x0c else 1)
    return w * h * (4 if fmt in (0x06, 0x07, 0x12, 0x1e) else 2)


# ---------------------------------------------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------------------------------------------


class Texture:
    """One entry of a pack's texture list. KXT header (the 0x18 block's data, offsets from the magic):
    +0x00 u32 'KXT\\6' (or u16 loadIndex, 'TD')   +0x04 u32 hashcode 0x03xxxxxx or -1
    +0x08 u32 frameSize (bytes per frame, including up to 127 bytes of slack for 128-byte alignment)
    +0x0c u32 width   +0x10 u32 height   +0x14 u32 alphaBits (0, 1, 4, 8)   +0x18 u32 mip levels
    +0x1c u32 linear (1 = not swizzled, one level)   +0x20 u32 formatType (0 raw, 1 DXT1, 2/3 DXT3, 4/5 DXT5)
    +0x24 u32 numFrames   +0x28 u32 fps (runtime: game ticks per frame)   +0x2c u32 ?   +0x30 u32 ?
    +0x34 char[32] name   +0x54 u32 handle[numFrames] (runtime: D3D texture slots)
    then numFrames * frameSize bytes of pixels (each frame: every mip level back to back)."""

    def __init__(self, d, pack, index, off, bsize, bmp):
        self.pack, self.index, self.off, self.bsize = pack, index, off, bsize
        (self.bmpFlags, self.bmpByte1, wm1, hm1, self.bmpFrames, self.bmpFps, self.bmpHash) = bmp
        self.bmpW, self.bmpH = wm1 + 1, hm1 + 1
        h = struct.unpack_from('<13I', d, off)
        self.magic = h[0]
        (self.hash, self.frameSize, self.width, self.height, self.alphaBits, self.levels, self.linear,
         self.formatType, self.numFrames, self.fps, self.u2c, self.u30) = h[1:13]
        self.name = d[off + 0x34:off + 0x54].split(b'\0')[0].decode('latin-1')
        if self.magic == KXT_MAGIC:
            self.kind = 'KXT'
        elif self.magic >> 16 == TD_TAG:
            self.kind = 'TD'
        else:
            self.kind = '?'
        self.tdIndex = self.magic & 0xffff if self.kind == 'TD' else None
        self.dataOff = off + 0x54 + 4 * self.numFrames
        self.source = self if self.kind == 'KXT' else None   # TD: resolved by Level
        self.d = d

    # the pixels' description comes from the texture that owns them (for a TD, its source)
    def fmt(self):
        s = self.source or self
        return d3d_format(register_type(s.formatType, s.alphaBits), s.linear)

    def fmt_name(self):
        return XFMT.get(self.fmt(), '0x%02x' % self.fmt())

    def mip_count(self):
        s = self.source or self
        return 1 if (s.linear or s.levels < 1) else s.levels

    def check(self):
        """Byte accounting of the block: header, handles, frames, then 0-3 bytes of padding."""
        errs = []
        if self.kind == 'KXT':
            used = 4 + 0x54 + self.numFrames * 4 + self.numFrames * self.frameSize
            if not 0 <= self.bsize - used <= 3:
                errs.append('block size %d, accounted %d' % (self.bsize, used))
            need = sum(level_size(self.fmt(), max(self.width >> i, 1), max(self.height >> i, 1))
                       for i in range(self.mip_count()))
            if not need <= self.frameSize <= need + 127:
                errs.append('frame size %d for %d bytes of mips' % (self.frameSize, need))
        elif self.kind == 'TD':
            if self.bsize != 4 + 0x54 + 4 * self.numFrames:
                errs.append('TD block size %d' % self.bsize)
        else:
            errs.append('unknown magic %08x' % self.magic)
        return errs

    def pixels(self, frame=0):
        """Raw bytes of one frame (all levels) - from the source texture for a TD."""
        s = self.source
        if s is None:
            raise ValueError('%s: unresolved TD duplicate (load index %d)' % (self.name, self.tdIndex))
        o = s.dataOff + (frame % s.numFrames) * s.frameSize
        return s.d[o:o + s.frameSize]

    def decode(self, frame=0, level=0):
        """-> (width, height, RGBA bytes) of one mip level of one frame."""
        s = self.source
        if s is None:
            raise ValueError('%s: unresolved TD duplicate (load index %d)' % (self.name, self.tdIndex))
        fmt = self.fmt()
        raw = self.pixels(frame)
        p = 0
        for i in range(level):
            p += level_size(fmt, max(s.width >> i, 1), max(s.height >> i, 1))
        w, h = max(s.width >> level, 1), max(s.height >> level, 1)
        return (w, h) + (decode_level(raw, p, fmt, w, h),)

    def describe(self):
        s = self.source or self
        t = '%3d %-3s %-26s %4dx%-4d %-12s mips %d' % (self.index, self.kind, self.name[:26], s.width, s.height,
                                                       self.fmt_name(), self.mip_count())
        if s.numFrames > 1:
            t += ' frames %d @%dfps' % (s.numFrames, s.fps)
        if self.hash != 0xffffffff:
            t += ' hash %08x' % self.hash
        if self.kind == 'TD':
            t += ' -> ' + ('%s#%d %s' % (s.pack, s.index, s.name) if self.source else 'load index %d' % self.tdIndex)
        return t


class Pack:
    """One graphics pack: its texture list and its meshes (KXE blocks, with the entity hashcode that follows)."""

    def __init__(self, d, name=''):
        self.d, self.name = d, name
        self.textures, self.meshes = [], {}
        self.hasEntities = False
        bmp, pal, data = [], [], []
        pending = None
        for p, t, s in blocks(d):
            if t == 0x10:
                bmp = [struct.unpack_from('<BBHHBBI', d, p + 4 + 12 * i) for i in range((s - 4) // 12)]
            elif t == 0x0f:
                pal = [struct.unpack_from('<II', d, p + 4 + 8 * i) for i in range((s - 4) // 8)]
            elif t == 0x18:
                data.append((p + 4, s))
            elif t == 0x0d:
                pending = p + 4
            elif t in (0x04, 0x1f):
                self.hasEntities = True
                h = struct.unpack_from('<I', d, p + 4)[0]
                if pending is not None and struct.unpack_from('<I', d, pending)[0] == KXE_MAGIC:
                    self.meshes[h] = pending
                pending = None
        self.pal = pal
        self.nHeaders = len(bmp)
        for i, (o, s) in enumerate(data):
            self.textures.append(Texture(d, name, i, o, s, bmp[i] if i < len(bmp) else (0,) * 6 + (0xffffffff,)))

    def check(self):
        errs = []
        if not (self.nHeaders == len(self.pal) == len(self.textures)):
            errs.append('%d headers, %d palette records, %d data blocks' % (self.nHeaders, len(self.pal),
                                                                            len(self.textures)))
        for t in self.textures:
            errs += ['%s#%d %s: %s' % (self.name, t.index, t.name, e) for e in t.check()]
        return errs

    def prims(self, meshHash):
        """[(tex0, tex1, stripLen-2, stateFlags, changeMask, bonePalette)] of a mesh (KXE layout in
        spec-facial.md 2.3 / spec-mesh.md)."""
        o = self.meshes[meshHash]
        if parse_gfx is not None:
            return [(q.tex0, q.tex1, q.count, q.flags, q.change, q.palette)
                    for q in parse_gfx.parse_mesh(self.d, o).prims]
        d = self.d
        vtx, idx, nprim = struct.unpack_from('<3I', d, o + 8)
        pal = struct.unpack_from('<I', d, o + 0x1c)[0]
        p = o + 0x60 + vtx * (0x20 if pal else 0x1c) + 3 + idx * 2 + 3
        return [struct.unpack_from('<HHIHBB', d, p + 12 * i) for i in range(nprim)]


class Level:
    """The 01 packs of one level bundle in load order, with the level-wide texture numbering the TD duplicates
    use: psiCreateMapTextures runs once per pack, at its first entity block, and gives the pack's textures the
    next NumXboxTexLoaded values (starting at 1 after maybePsiResetResources); a TD's low 16 bits + 1 is one of
    those values. Packs without an entity block never create their textures (and take no numbers)."""

    def __init__(self, path, name=None):
        if isinstance(path, (bytes, bytearray)):
            D, self.name = bytes(path), name
        else:
            D = open(path, 'rb').read()
            self.name = name or os.path.basename(path)[:-4]
        self.packs = []
        self.byGlobal = {}
        n = 1
        for typ, name, off, ln in bundle.walk(D):
            if not name.lower().startswith('01'):
                continue
            pk = Pack(D[off:off + ln], name.lower())
            pk.bundleType = typ
            pk.base = n if pk.hasEntities else None
            if pk.hasEntities:
                for t in pk.textures:
                    self.byGlobal[n + t.index] = t
                n += len(pk.textures)
            self.packs.append(pk)
        self.unresolved = 0
        for pk in self.packs:
            for t in pk.textures:
                if t.kind == 'TD':
                    src = self.byGlobal.get(t.tdIndex + 1)
                    while src is not None and src.kind == 'TD':
                        src = src.source
                    t.source = src
                    if src is None:
                        self.unresolved += 1


# ---------------------------------------------------------------------------------------------------------------
# Pixel decoding
# ---------------------------------------------------------------------------------------------------------------

_morton_cache = {}


def swizzle_table(w, h):
    """offset (in texels) of texel (x, y) in an Xbox swizzled level: x and y bits interleaved from bit 0, x first,
    the larger dimension's surplus bits on top (d3d9Backend.cpp Unswizzle / d3dComputeSwizzleMasks)."""
    key = (w, h)
    if key in _morton_cache:
        return _morton_cache[key]
    xs, ys = [], []
    bit, size = 1, 1
    mx = my = 0
    while True:
        anyb = False
        if size < w:
            mx |= bit
            bit <<= 1
            anyb = True
        if size < h:
            my |= bit
            bit <<= 1
            anyb = True
        size <<= 1
        if not anyb:
            break
    v = 0
    for _ in range(w):
        xs.append(v)
        v = (v - mx) & mx
    v = 0
    for _ in range(h):
        ys.append(v)
        v = (v - my) & my
    tab = [yo + xo for yo in ys for xo in xs]
    _morton_cache[key] = tab
    return tab


def _565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def _dxt_colours(c0, c1, four):
    a, b = _565(c0), _565(c1)
    if four or c0 > c1:
        return [a + (255,), b + (255,), tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)]
    return [a + (255,), b + (255,), tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]


def decode_level(raw, p, fmt, w, h):
    out = bytearray(w * h * 4)
    if fmt in (0x0c, 0x0e, 0x0f):
        bw, bh = max(w, 4) // 4, max(h, 4) // 4
        bsz = 8 if fmt == 0x0c else 16
        for by in range(bh):
            for bx in range(bw):
                q = p + (by * bw + bx) * bsz
                alpha = None
                if fmt == 0x0e:
                    a64 = int.from_bytes(raw[q:q + 8], 'little')
                    alpha = [((a64 >> (4 * i)) & 15) * 17 for i in range(16)]
                    q += 8
                elif fmt == 0x0f:
                    a0, a1 = raw[q], raw[q + 1]
                    ai = int.from_bytes(raw[q + 2:q + 8], 'little')
                    if a0 > a1:
                        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                    else:
                        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                    alpha = [pal[(ai >> (3 * i)) & 7] for i in range(16)]
                    q += 8
                c0, c1, bits = struct.unpack_from('<HHI', raw, q)
                cols = _dxt_colours(c0, c1, fmt != 0x0c)
                for py in range(4):
                    y = by * 4 + py
                    if y >= h:
                        break
                    for px in range(4):
                        x = bx * 4 + px
                        if x >= w:
                            continue
                        k = py * 4 + px
                        c = cols[(bits >> (2 * k)) & 3]
                        o = (y * w + x) * 4
                        out[o:o + 3] = bytes(c[:3])
                        out[o + 3] = c[3] if alpha is None else alpha[k]
        return bytes(out)
    bpp = 4 if fmt in (0x06, 0x07, 0x12, 0x1e) else 2
    linear = fmt in (0x10, 0x12, 0x1d, 0x1e)
    if linear:
        pitch = (w * bpp + 63) & ~63
        order = [y * pitch // bpp + x for y in range(h) for x in range(w)]
    else:
        order = swizzle_table(w, h)
    for i, t in enumerate(order):
        q = p + t * bpp
        o = i * 4
        if bpp == 4:
            b, g, r, a = raw[q:q + 4]
            if fmt in (0x07, 0x1e):
                a = 255
        else:
            v = raw[q] | raw[q + 1] << 8
            if fmt in (0x04, 0x1d):
                a, r, g, b = (v >> 12) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17
            else:
                a = 255 if v >> 15 else 0
                r, g, b = (v >> 10 & 31) << 3, (v >> 5 & 31) << 3, (v & 31) << 3
                r, g, b = r | r >> 5, g | g >> 5, b | b >> 5
        out[o] = r
        out[o + 1] = g
        out[o + 2] = b
        out[o + 3] = a
    return bytes(out)


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def safe(s):
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', s) or 'unnamed'


def png_name(t, frame=None, level=None):
    n = '%s_%03d_%s' % (t.pack, t.index, safe(t.name))
    if frame is not None:
        n += '_f%02d' % frame
    if level:
        n += '_m%d' % level
    return n + '.png'


def export(t, outdir, frames=False, mips=False):
    """Write a texture's PNG(s); returns the file names."""
    s = t.source
    names = []
    for f in (range(s.numFrames) if frames and s.numFrames > 1 else [None]):
        for lv in (range(t.mip_count()) if mips else [0]):
            w, h, px = t.decode(f or 0, lv)
            nm = png_name(t, f, lv)
            write_png(os.path.join(outdir, nm), w, h, px)
            names.append(nm)
    return names


# ---------------------------------------------------------------------------------------------------------------
# Materials: a primitive's stateFlags as the draw loop applies them (RecurseAndDrawBoxes 0xdd4c0, tail 0xdd56d)
# ---------------------------------------------------------------------------------------------------------------

BLEND = {0: 'alpha', 1: 'additive', 2: 'subtractive', 3: 'alpha'}


def material(stateFlags):
    """Render state a primitive sets (each part only when its changeMask bit is set; the draw loop keeps the
    previous primitive's state otherwise - see effective_states)."""
    f = stateFlags
    return dict(morph=bool(f & 1), blend=BLEND[(f >> 1) & 3], doubleSided=bool(f & 8),
                alphaRef=0xb4 if f & 0x10 else 1, zWrite=bool(f & 0x20),
                wrapU=bool(f & 0x80), wrapV=bool(f & 0x100))


def effective_states(prims):
    """Walk a mesh's primitives as the draw loop does and return, per primitive, the state in force: each group of
    render states is only re-sent when the primitive's changeMask bit for it is set. Groups never set by the mesh
    are None (they inherit whatever was drawn before - in practice the defaults of maybeResetRenderState)."""
    cur = dict(blend=None, tex1=None, doubleSided=None, alphaRef=None, zWrite=None, wrapU=None, wrapV=None,
               morph=None)
    out = []
    for (t0, t1, n, f, chg, pal) in prims:
        m = material(f)
        if chg & 0x02:
            cur['blend'] = m['blend']
        if chg & 0x04:
            cur['tex1'] = t1
        if chg & 0x08:
            cur['doubleSided'] = m['doubleSided']
        if chg & 0x10:
            cur['alphaRef'] = m['alphaRef']
        if chg & 0x20:
            cur['zWrite'] = m['zWrite']
        if chg & 0x40:
            cur['wrapU'], cur['wrapV'] = m['wrapU'], m['wrapV']
        if chg & 0x01:
            cur['morph'] = m['morph']
        out.append(dict(cur, tex0=t0))
    return out


# ---------------------------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------------------------


def bundle_paths():
    return sorted(glob.glob(os.path.join(LVL, '07*.bin')))


def dump_packs():
    """The dump folder's 01 packs, each resolved through a level bundle holding a byte-identical copy."""
    files = sorted(glob.glob(os.path.join(DUMP, '01*.bin')))
    want = {}
    for fn in files:
        d = open(fn, 'rb').read()
        want[hashlib.md5(d).hexdigest()] = os.path.basename(fn)[:-4].lower()
    found = {}
    for bp in bundle_paths():
        lv = None
        D = open(bp, 'rb').read()
        for typ, name, off, ln in bundle.walk(D):
            if name.lower().startswith('01'):
                k = hashlib.md5(D[off:off + ln]).hexdigest()
                if k in want and k not in found:
                    lv = lv or Level(bp)
                    found[k] = next(p for p in lv.packs if p.name == name.lower() and
                                    hashlib.md5(p.d).hexdigest() == k)
        if len(found) == len(want):
            break
    packs = []
    for fn in files:
        d = open(fn, 'rb').read()
        k = hashlib.md5(d).hexdigest()
        packs.append(found.get(k) or Pack(d, want[k]))
    return packs


def select_packs(argv):
    bun = [a for a in argv if re.fullmatch(r'(?i)07[0-9a-f]{6}(\.bin)?', os.path.basename(a))]
    files = [a for a in argv if a not in bun]
    if bun:
        out = []
        for b in bun:
            p = b if os.path.exists(b) else os.path.join(LVL, os.path.basename(b)[:8] + '.bin')
            out += Level(p).packs
        return out
    if files:
        return [Pack(open(f, 'rb').read(), os.path.basename(f)[:-4].lower()) for f in files]
    return dump_packs()


def opt(argv, name, default=None):
    if name in argv:
        i = argv.index(name)
        v = argv[i + 1]
        del argv[i:i + 2]
        return v
    return default


def cmd_list(argv):
    for pk in select_packs(argv):
        print('%s  %d textures%s, %d meshes' % (pk.name, len(pk.textures),
                                                '' if pk.hasEntities else ' (no entities: never created)',
                                                len(pk.meshes)))
        for t in pk.textures:
            print('   ' + t.describe())
        for e in pk.check():
            print('   ERROR ' + e)


def cmd_png(argv):
    outdir = argv.pop(0)
    frames, mips = '--frames' in argv, '--mips' in argv
    argv = [a for a in argv if a not in ('--frames', '--mips')]
    bun, pack, sub = opt(argv, '--bundle'), opt(argv, '--pack'), opt(argv, '--name')
    os.makedirs(outdir, exist_ok=True)
    packs = Level(os.path.join(LVL, bun[:8] + '.bin')).packs if bun else select_packs(argv)
    n = 0
    for pk in packs:
        if pack and pk.name != pack.lower():
            continue
        for t in pk.textures:
            if sub and sub.lower() not in t.name.lower():
                continue
            if t.source is None:
                print('skip %s#%d %s: unresolved TD' % (pk.name, t.index, t.name))
                continue
            n += len(export(t, outdir, frames, mips))
    print('%d PNGs written to %s' % (n, outdir))


def cmd_validate(argv):
    outdir = opt(argv, '--png')
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    fmts, kinds, errs, done = collections.Counter(), collections.Counter(), [], {}
    distinct = collections.Counter()
    unresolved = npacks = ntex = 0
    for bp in bundle_paths():
        lv = Level(bp)
        unresolved += lv.unresolved
        for pk in lv.packs:
            npacks += 1
            errs += ['%s/%s' % (lv.name, e) for e in pk.check()]
            for t in pk.textures:
                ntex += 1
                kinds[t.kind] += 1
                fmts[t.fmt_name()] += 1
                if t.source is None or t.kind != 'KXT':
                    continue
                key = hashlib.md5(t.pixels(0) + struct.pack('<5I', t.width, t.height, t.fmt(), t.levels,
                                                             t.numFrames)).hexdigest()
                if key in done:
                    continue
                distinct[t.fmt_name()] += 1
                try:
                    for f in range(t.numFrames):
                        for lv_ in range(t.mip_count()):
                            w, h, px = t.decode(f, lv_)
                            assert len(px) == w * h * 4
                    done[key] = True
                    if outdir:
                        w, h, px = t.decode(0, 0)
                        nm = png_name(t)
                        if os.path.exists(os.path.join(outdir, nm)):   # same pack/index/name, other content
                            nm = nm[:-4] + '_' + key[:6] + '.png'
                        write_png(os.path.join(outdir, nm), w, h, px)
                except Exception as e:  # noqa: BLE001
                    errs.append('%s/%s#%d %s: decode failed: %s' % (lv.name, pk.name, t.index, t.name, e))
                    done[key] = False
    print('%d packs in %d bundles, %d texture entries: %s' % (npacks, len(bundle_paths()), ntex, dict(kinds)))
    print('formats (entries, TD counted as their source):', dict(fmts))
    print('distinct KXT textures decoded (every frame, every mip):', dict(distinct), 'total', len(done),
          'ok', sum(done.values()))
    print('TD duplicates unresolved:', unresolved)
    dp = dump_packs()
    dn = sum(len(p.textures) for p in dp)
    dres = sum(1 for p in dp for t in p.textures if t.source is not None)
    dfmt = collections.Counter(t.fmt_name() for p in dp for t in p.textures)
    print('dump: %d packs, %d textures, %d with pixels resolved; formats %s' % (len(dp), dn, dres, dict(dfmt)))
    for p in dp:
        errs += ['dump/%s' % e for e in p.check()]
    print('%d errors' % len(errs))
    for e in errs[:50]:
        print('  ' + e)


def find_skin(h):
    for fn in [os.path.join(DUMP, '%08X.bin' % h), os.path.join(DUMP, '%08x.bin' % h),
               os.path.join(LVL, 'skin', '%08x.bin' % h)]:
        if os.path.exists(fn):
            return open(fn, 'rb').read()
    for bp in bundle_paths():
        D = open(bp, 'rb').read()
        for typ, name, off, ln in bundle.walk(D):
            if name.lower() == '%08x' % h:
                return D[off:off + ln]
    raise SystemExit('skin %08x not found' % h)


def cmd_skin(argv):
    import parse_skeleton
    import parse_skin
    outdir = opt(argv, '--png')
    h = int(argv[0], 16)
    s = parse_skin.parse(find_skin(h), parse_skeleton.load_table())
    meshes = [('rigged', m) for m in s['riggedHash']] + [('discrete', m) for m in s['discreteHash']]
    # the mesh hashes: find which level bundle has the skin and its meshes
    for bp in bundle_paths():
        D = open(bp, 'rb').read()
        if not any(name.lower() == '%08x' % h for _, name, _, _ in bundle.walk(D)):
            continue
        lv = Level(bp)
        print('skin %08x, level %s' % (h, lv.name))
        for role, mh in meshes:
            pk = next((p for p in lv.packs if mh in p.meshes), None)
            if pk is None:
                print('  %s mesh %08x: not in this level' % (role, mh))
                continue
            prims = pk.prims(mh)
            print('  %s mesh %08x in pack %s, %d primitives' % (role, mh, pk.name, len(prims)))
            used = collections.OrderedDict()
            for st in effective_states(prims):
                for slot in ('tex0', 'tex1'):
                    i = st[slot]
                    if i is None or i == 0xffff:
                        continue
                    key = (slot, i, st['blend'], st['doubleSided'], st['alphaRef'], st['zWrite'],
                           st['wrapU'], st['wrapV'])
                    used[key] = used.get(key, 0) + 1
            for (slot, i, bl, ds, ar, zw, wu, wv), cnt in used.items():
                t = pk.textures[i] if i < len(pk.textures) else None
                desc = t.describe().strip() if t else 'index %d out of range' % i
                print('    %s x%-3d %s | blend %s, 2-sided %s, alphaRef %s, zWrite %s, wrap %s/%s'
                      % (slot, cnt, desc, bl, ds, ar, zw, wu, wv))
                if outdir and t is not None and t.source is not None:
                    os.makedirs(outdir, exist_ok=True)
                    export(t, outdir)
        return
    print('skin %08x is in no level bundle' % h)


def main(argv):
    if not argv:
        print(__doc__)
        return
    cmd, rest = argv[0], argv[1:]
    {'list': cmd_list, 'png': cmd_png, 'validate': cmd_validate, 'skin': cmd_skin}[cmd](rest)


if __name__ == '__main__':
    main(sys.argv[1:])
