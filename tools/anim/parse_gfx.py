"""Nightfire graphics packs (01xxxxxx.bin, the "parsemap" block format) and their Xbox meshes (KXE\\x06).

Layout from the Xbox loader: parsemap_parsemap 0xa6b20 -> parsemap_parsenextblock 0xa6a80 ->
parsemap_handle_block_id 0xa6820 (block handlers listed in BLOCK_NAMES), psiCreateEntityGfx 0xde220 (mesh),
RecurseAndDrawBoxes 0xdd4c0 (the draw loop that gives the primitive fields their meaning). Spec: docs/anim/spec-mesh.md.

FILE      u32 0 (BinFileVersion 0: blocks follow each other; version 1, a table of block offsets, never occurs)
          then blocks, until the END block (0x1d); then zero padding to a multiple of 32 (bundle entry padding)
BLOCK     u32 header = type << 24 | size   (size includes the 4-byte header), then size-4 bytes of payload

Importable API (everything takes the whole file as bytes):
    walk(data)                  -> [Block(type, off, size)]  payload = data[off+4 : off+size]
    parse_pack(data)            -> Pack: header, entities (each with its Mesh or None), texture list, LOD, ...
    parse_mesh(data, off)       -> Mesh  (off = offset of the 'KXE\\x06' magic = block offset + 4)
    Mesh.vertices / triangles() / morph_targets / palettes / prims / skin_weights(vertex, prim palette)
    decode_normal(u32)          -> (x, y, z)  NORMPACKED3
    iter_packs(lvl_dir, dump)   -> (bundle, entry type, name, bytes) for every 01 file of every bundle / the dump

CLI:  python parse_gfx.py                   validate every 01 file in build/anim/lvl/07*.bin and the dump
      python parse_gfx.py -v                also list block counts, per-file problems
      python parse_gfx.py --skins           decode and check the skinning of every rigged skin, per bundle
      python parse_gfx.py --obj HASH OUTDIR [--bundle 07xxxxxx] [--skin 05xxxxxx]
                                            write entity HASH (e.g. 020004c2) as OBJ (bind pose; a discrete part
                                            is moved to its bone when --skin names the skin that owns it)
"""
import collections
import glob
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nfpaths import LVL, DUMP  # noqa: E402

# ---------------------------------------------------------------------------------------------------------------
# Container
# ---------------------------------------------------------------------------------------------------------------
BLOCK_NAMES = {
    0x04: 'entity_params',     # parsemap_block_entity_params 0xa6180 -> psiCreateEntityGfx; also 0x1f (unused)
    0x05: 'ai_paths',          # AIPath_Parse (level map files only)
    0x0d: 'model',             # KXE\x06 mesh: pCurrCelList->gfx = payload (handle_block_id case 0xd)
    0x0e: 'map_header',        # parsemap_block_map_header 0xa5eb0
    0x0f: 'palette_header',    # parsemap_block_palette_header 0xa6030 (texture hash table, 8 bytes each)
    0x10: 'bmp_header',        # case 0x10: texture descriptors, 12 bytes each
    0x18: 'texture_data',      # parsemap_block_texture_data_Xbox 0xa6110 (KXT\x06 / 'TD' texture payloads)
    0x19: 'path',              # case 0x19: path data, only kept for map files (param 1)
    0x1a: 'map_data',          # parsemap_block_map_data_static 0xa6550 / _dynamic 0xa66f0: placements
    0x1c: 'memory_discard',    # case 0x1c: marks where the discardable part starts (no payload)
    0x1d: 'end',               # case 0x1d: stop parsing (no payload)
    0x21: 'portals',           # parsemap_block_portal_data 0xa6260
    0x26: 'ambient_radiators',  # Light_SetAmbientRadiators (map files only)
    0x27: 'lod',               # parsemap_block_LOD 0xa5fe0: per entity, the glist it switches to (damaged/off state)
    0x28: 'map_sounds',        # Sound_LoadMapSounds (map files only)
    0x2c: 'hashlist',          # parsemap_block_hashlist 0xa5f80: hashcodes to load if not yet loaded
    0x2e: 'coll_data',         # parsemap_block_Coll_Data_New 0xa63f0 (also 0x2f)
    0x30: 'particles',         # parsemap_block_particles 0x68020
}

Block = collections.namedtuple('Block', 'type off size')


def walk(data):
    """All blocks of a graphics pack, in file order, END block included. Raises ValueError on a broken chain."""
    if struct.unpack_from('<I', data, 0)[0] != 0:
        raise ValueError('BinFileVersion %d not supported' % struct.unpack_from('<I', data, 0)[0])
    out = []
    p = 4
    while True:
        if p + 4 > len(data):
            raise ValueError('ran off the end at 0x%x without an END block' % p)
        w = struct.unpack_from('<I', data, p)[0]
        t, sz = w >> 24, w & 0xffffff
        if sz < 4 or p + sz > len(data):
            raise ValueError('bad block size 0x%x at 0x%x' % (sz, p))
        out.append(Block(t, p, sz))
        p += sz
        if t == 0x1d:
            return out


def payload(data, blk):
    return data[blk.off + 4:blk.off + blk.size]


def cstr(b):
    return b.split(b'\0')[0].decode('latin-1')


# ---------------------------------------------------------------------------------------------------------------
# Mesh (block 0x0d)
# ---------------------------------------------------------------------------------------------------------------
KXE_MAGIC = 0x0645584B          # 'KXE\x06'
RIGID_STRIDE, SKIN_STRIDE = 0x1c, 0x20
PALETTE_SIZE = 0x36             # 54 bytes: up to 53 bone indices, 0xff-terminated (d3dSetSkinMatrix: slot < 0x35)

# SkinRegTable 0x1b5208: slot -> D3D constant register (-96..95); +96 = the byte stored in the vertex
SKIN_REG_TABLE = [-96 + 3 * s for s in range(19)] + [-93 + 3 * s for s in range(19, 31)] + \
                 [-63 + 3 * s for s in range(31, 53)]
BYTE_TO_SLOT = {r + 96: s for s, r in enumerate(SKIN_REG_TABLE)}


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def decode_normal(u):
    """D3DVSDT_NORMPACKED3: x = signed bits 0-10 / 1023, y = signed bits 11-21 / 1023, z = signed bits 22-31 / 511"""
    return (sext(u & 0x7ff, 11) / 1023.0, sext((u >> 11) & 0x7ff, 11) / 1023.0, sext(u >> 22, 10) / 511.0)


def decode_bones(u, gpu=False):
    """The skinned vertex's 4th dword (v4, D3DCOLOR), memory bytes b0 b1 b2 b3.
    Shader #2: regA = 256*B - 96, regB = 256*G - 96, wA = R/256 + A (v4 = (R,G,B,A) = (b2,b1,b0,b3)/255)
    -> slotA = BYTE_TO_SLOT[b0], slotB = BYTE_TO_SLOT[b1], wA = u16 / 65535 with u16 = b3*256 + b2, wB = 1 - wA.
    The GPU actually computes u16 / 65280 (so 0xffff gives 1.0039 and wB = -0.0039); gpu=True returns that.
    Returns (slotA, slotB, wA); a slot is None when the byte is not a palette register."""
    b0, b1, b2, b3 = u & 0xff, (u >> 8) & 0xff, (u >> 16) & 0xff, u >> 24
    return BYTE_TO_SLOT.get(b0), BYTE_TO_SLOT.get(b1), (b3 * 256 + b2) / (65280.0 if gpu else 65535.0)


class Prim:
    """12-byte strip record (RecurseAndDrawBoxes 0xdd4c0; psiCreateEntityGfx remaps tex0/tex1 to global Tex[] ids)."""
    __slots__ = ('tex0', 'tex1', 'count', 'flags', 'change', 'palette', 'first')

    def __init__(self, raw, first):
        self.tex0, self.tex1, self.count, self.flags, self.change, self.palette = raw
        self.first = first          # first index of the strip in the index buffer

    # state bits (flags), each applied only when the matching change bit is set; otherwise the state stays as the
    # previous primitive (or draw) left it
    def describe(self):
        f, c = self.flags, self.change
        s = []
        if c & 0x01: s.append('morph=%d' % (f & 1))
        if c & 0x02: s.append('blend=%s' % ('alpha', 'add', 'sub', 'alpha')[(f >> 1) & 3])
        if c & 0x04: s.append('tex1=%s' % ('none' if self.tex1 == 0xffff else self.tex1))
        if c & 0x08: s.append('cull=%s' % ('none' if f & 8 else 'ccw'))
        if c & 0x10: s.append('alpharef=%d' % (180 if f & 0x10 else 1))
        if c & 0x20: s.append('zwrite=%d' % ((f >> 5) & 1))
        if c & 0x40: s.append('wrapU=%d wrapV=%d' % ((f >> 7) & 1, (f >> 8) & 1))
        return ' '.join(s)


class Mesh:
    """psiCreateEntityGfx 0xde220 layout; offsets relative to the magic (the block payload).

    0x00 u32 magic 'KXE\\x06'        0x04 u32 hashcode (= the entity's; 0xffffffff for unnamed level geometry)
    0x08 u32 vtxCnt                  0x0c u32 idxCnt                 0x10 u32 primCnt
    0x14 u32 stripTriCnt  (= sum of prim counts: triangles drawn, degenerate ones included; 0 = no mesh)
    0x18 u32 degenerateTriCnt (triangles with a repeated index; +0x14 - +0x18 = real triangles, psiGetTriList)
    0x1c u32 paletteCnt   (bone palettes; non-zero = skinned, 32-byte vertices)
    0x20 u32 exporter flags (never read by the game)
    0x24 u32 morphTargetCnt          0x28 u32 morphVtxCnt (the first morphVtxCnt vertices have deltas)
    0x2c char[32] name (Maya node)   0x4c..0x5f five runtime words, zero on disk:
         vtxBuffer handle, idxBuffer handle, prims*, palettes*, morph stream base
    0x60 sections, each starting where the previous one's data + 3 bytes ended (the loader moves vertex, index
         and morph data up to a 4-byte boundary in place, into those 3 bytes):
         vertices  vtxCnt * 28|32   + 3      indices  idxCnt * u16 + 3
         prims     primCnt * 12              palettes paletteCnt * 0x36
         morphs    morphTargetCnt * morphVtxCnt * s16[3] + 3   (only when both counts are non-zero)
    then 0xff up to a 4-byte boundary = the end of the block.
    """

    def __init__(self, data, off):
        self.data = data
        self.off = off
        (self.magic, self.hash, self.vtxCnt, self.idxCnt, self.primCnt, self.stripTriCnt, self.degenCnt,
         self.paletteCnt, self.exportFlags, self.morphCnt, self.morphVtxCnt) = struct.unpack_from('<11I', data, off)
        if self.magic != KXE_MAGIC:
            raise ValueError('no KXE magic at 0x%x' % off)
        self.name = cstr(data[off + 0x2c:off + 0x4c])
        self.runtime = struct.unpack_from('<5I', data, off + 0x4c)
        self.skinned = self.paletteCnt != 0
        self.stride = SKIN_STRIDE if self.skinned else RIGID_STRIDE
        p = off + 0x60
        self.vtxOff = p
        p += self.vtxCnt * self.stride + 3
        self.idxOff = p
        p += self.idxCnt * 2 + 3
        self.primOff = p
        p += self.primCnt * 12
        self.palOff = p
        p += self.paletteCnt * PALETTE_SIZE
        self.morphOff = p
        if self.morphCnt and self.morphVtxCnt:
            p += self.morphCnt * self.morphVtxCnt * 6 + 3
        self.end = p
        self._verts = None

    @property
    def empty(self):
        """psiCreateEntityGfx makes no geometry unless vtxCnt, idxCnt, primCnt and +0x14 are all non-zero"""
        return not (self.vtxCnt and self.idxCnt and self.primCnt and self.stripTriCnt)

    # --- raw sections
    def raw_vertex(self, i):
        o = self.vtxOff + i * self.stride
        if self.skinned:
            return struct.unpack_from('<3fIIffI', self.data, o)
        return struct.unpack_from('<3fIIff', self.data, o) + (None,)

    @property
    def vertices(self):
        """[dict(pos, normal, colour(r,g,b,a 0..255), uv, bones(raw u32 or None))]"""
        if self._verts is None:
            vs = []
            for i in range(self.vtxCnt):
                x, y, z, n, c, u, v, bw = self.raw_vertex(i)
                vs.append(dict(pos=(x, y, z), normal=decode_normal(n), normal_raw=n,
                               colour=((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, c >> 24), uv=(u, v), bones=bw))
            self._verts = vs
        return self._verts

    @property
    def indices(self):
        return struct.unpack_from('<%dH' % self.idxCnt, self.data, self.idxOff) if self.idxCnt else ()

    @property
    def prims(self):
        out = []
        first = 0
        for i in range(self.primCnt):
            pr = Prim(struct.unpack_from('<HHIHBB', self.data, self.primOff + 12 * i), first)
            out.append(pr)
            first += pr.count + 2
        return out

    @property
    def palettes(self):
        """[[skin bone index, ...]] per palette (the prim's palette byte is 1-based; 0 = keep the loaded one)"""
        out = []
        for i in range(self.paletteCnt):
            raw = self.data[self.palOff + i * PALETTE_SIZE:self.palOff + (i + 1) * PALETTE_SIZE]
            out.append(list(raw[:raw.index(0xff)]) if 0xff in raw else list(raw))
        return out

    @property
    def morph_targets(self):
        """[[(dx, dy, dz) int16 for vertex 0..morphVtxCnt-1] per target]; displacement = delta * weight / 32767"""
        out = []
        if not (self.morphCnt and self.morphVtxCnt):
            return out
        for t in range(self.morphCnt):
            base = self.morphOff + t * self.morphVtxCnt * 6
            out.append([struct.unpack_from('<3h', self.data, base + 6 * i) for i in range(self.morphVtxCnt)])
        return out

    # --- strips -> triangles
    def triangles(self, keep_degenerate=False):
        """[(a, b, c, prim_index)] in D3D order: strip triangle k of a primitive is (i[k], i[k+1], i[k+2]) for even k
        and (i[k+1], i[k], i[k+2]) for odd k, every primitive being its own DrawIndexedVertices(TRIANGLESTRIP).
        Degenerate triangles (a repeated index) are dropped, as psiGetTriList does. With this order the geometric
        normal cross(b-a, c-a) points to the same side as the stored vertex normals (checked on every mesh), so
        for Blender keep the order as it is (see spec-mesh.md on handedness)."""
        idx = self.indices
        out = []
        for pi, pr in enumerate(self.prims):
            s = idx[pr.first:pr.first + pr.count + 2]
            for k in range(pr.count):
                a, b, c = s[k], s[k + 1], s[k + 2]
                if k & 1:
                    a, b = b, a
                if keep_degenerate or (a != b and b != c and a != c):
                    out.append((a, b, c, pi))
        return out

    # --- skinning
    def vertex_palettes(self):
        """For each vertex, the set of palette numbers (1-based, after carrying palette 0 forward) of the primitives
        that draw it."""
        idx = self.indices
        cur = 0
        use = [set() for _ in range(self.vtxCnt)]
        for pr in self.prims:
            if pr.palette:
                cur = pr.palette
            for i in idx[pr.first:pr.first + pr.count + 2]:
                use[i].add(cur)
        return use

    def skin_weights(self):
        """Per vertex [(skinBone, weight), (skinBone, weight)] via slot -> palette; None where undecodable.
        Also returns a list of problems (vertices whose palettes disagree, bytes outside the register table)."""
        pals = self.palettes
        use = self.vertex_palettes()
        out = []
        problems = []
        for i, v in enumerate(self.vertices):
            sa, sb, wa = decode_bones(v['bones'])
            res = set()
            for p in use[i]:
                if p == 0 or p > len(pals) or sa is None or sb is None or sa >= len(pals[p - 1]) or \
                        sb >= len(pals[p - 1]):
                    res.add(None)
                else:
                    res.add((pals[p - 1][sa], pals[p - 1][sb]))
            if len(res) != 1 or None in res:
                problems.append((i, '%08x' % v['bones'], sorted(use[i]), res))
                out.append(None)
                continue
            ba, bb = res.pop()
            out.append([(ba, wa), (bb, 1.0 - wa)])
        return out, problems


def parse_mesh(data, off):
    return Mesh(data, off)


def validate_mesh(m, blk=None):
    """Checks that the mesh accounts for every byte of its block. Returns a list of error strings."""
    errs = []
    d = m.data
    end = blk.off + blk.size if blk else None
    if any(m.runtime):
        errs.append('runtime words not zero')
    if m.end > len(d) or (end is not None and m.end > end):
        return errs + ['sections overrun the block (end 0x%x, block end 0x%x)' % (m.end, end or len(d))]
    pad_to = (m.end + 3) & ~3
    if end is not None and pad_to != end:
        errs.append('block ends at 0x%x, sections at 0x%x' % (end, m.end))
    if any(b != 0xff for b in d[m.end:pad_to]):
        errs.append('end padding not 0xff')
    if (m.morphCnt == 0) != (m.morphVtxCnt == 0):
        errs.append('morph counts inconsistent %d/%d' % (m.morphCnt, m.morphVtxCnt))
    if m.empty:
        if m.vtxCnt or m.idxCnt or m.primCnt:
            errs.append('empty mesh with some counts non-zero')
        return errs
    prims = m.prims
    if sum(p.count + 2 for p in prims) != m.idxCnt:
        errs.append('strips use %d of %d indices' % (sum(p.count + 2 for p in prims), m.idxCnt))
    if sum(p.count for p in prims) != m.stripTriCnt:
        errs.append('+0x14 %d != sum of strip lengths %d' % (m.stripTriCnt, sum(p.count for p in prims)))
    tris = m.triangles(keep_degenerate=True)
    deg = sum(1 for a, b, c, _ in tris if a == b or b == c or a == c)
    # +0x18 is only a statistic (render counters; psiGetTriList's allocation): it matches the index data in 93% of
    # the meshes, so a mismatch is reported as a note, not an error
    m.degen_mismatch = deg - m.degenCnt
    idx = m.indices
    if idx and max(idx) >= m.vtxCnt:
        errs.append('index %d >= vtxCnt %d' % (max(idx), m.vtxCnt))
    for p in prims:
        if p.palette > m.paletteCnt:
            errs.append('prim palette %d > paletteCnt %d' % (p.palette, m.paletteCnt))
            break
    if m.skinned and prims[0].palette == 0:
        errs.append('first prim of a skinned mesh has no palette')
    for i, pal in enumerate(m.palettes):
        raw = d[m.palOff + i * PALETTE_SIZE:m.palOff + (i + 1) * PALETTE_SIZE]
        if 0xff not in raw:
            errs.append('palette %d not terminated' % i)
        elif any(b != 0xff for b in raw[raw.index(0xff):]):
            errs.append('palette %d: bytes after the terminator are not 0xff' % i)
    if m.morphCnt:
        morph_on = False
        for p in prims:
            if p.change & 1:
                morph_on = bool(p.flags & 1)
            seg = idx[p.first:p.first + p.count + 2]
            if morph_on and max(seg) >= m.morphVtxCnt:
                errs.append('morph strip uses vertex %d >= morphVtxCnt' % max(seg))
                break
    return errs


# ---------------------------------------------------------------------------------------------------------------
# Whole pack
# ---------------------------------------------------------------------------------------------------------------
class Entity:
    """Block 0x04 (parsemap_block_entity_params 0xa6180) -> celglist_tag (0x3c bytes, pCelList[i]).
    +0x04 u32 hashcode (0x02xxxxxx, or 0xffffffff: reachable only by index from map placements)
    +0x08 u32 flags -> celglist+0x38     +0x0c f32 sphere[4] (centre xyz, radius) -> +0x0c
    +0x1c f32 bboxMin[3] -> +0x1c        +0x28 f32 bboxMax[3] -> +0x28
    +0x34 char name[] NUL-terminated, 0xff-padded to 4 -> celglist+0x00 points at it"""

    def __init__(self, data, blk, index):
        o = blk.off
        self.index = index
        self.block = blk
        self.hash, self.flags = struct.unpack_from('<II', data, o + 4)
        self.sphere = struct.unpack_from('<4f', data, o + 0xc)
        self.bbmin = struct.unpack_from('<3f', data, o + 0x1c)
        self.bbmax = struct.unpack_from('<3f', data, o + 0x28)
        raw = data[o + 0x34:o + blk.size]
        self.name = cstr(raw)
        self.name_pad = raw[raw.index(b'\0') + 1:] if b'\0' in raw else b'?'
        self.mesh = None
        self.mesh_block = None
        self.coll_block = None
        self.lod = -1
        self.lod_word = None


class Pack:
    def __init__(self, data):
        self.data = data
        self.blocks = walk(data)
        self.errors = []
        self.entities = []
        bl = self.blocks
        types = [b.type for b in bl]
        mh = [b for b in bl if b.type == 0x0e]
        if len(mh) != 1 or bl[0].type != 0x0e:
            self.errors.append('map header missing or not first')
            return
        self.hash, self.entityCnt, self.pathCnt, self.textureCnt = struct.unpack_from('<4I', data, mh[0].off + 4)
        # textures (the texture tool decodes these; here only their counts are checked)
        bmp = [b for b in bl if b.type == 0x10]
        pal = [b for b in bl if b.type == 0x0f]
        self.bmp_header = bmp[0] if bmp else None
        self.palette_header = pal[0] if pal else None
        self.texture_blocks = [b for b in bl if b.type == 0x18]
        if bmp and (bmp[0].size - 4) % 12:
            self.errors.append('bmp header size not a multiple of 12')
        if bmp and (bmp[0].size - 4) // 12 != self.textureCnt:
            self.errors.append('bmp header count %d != map header %d' % ((bmp[0].size - 4) // 12, self.textureCnt))
        if pal and (pal[0].size - 4) % 8:
            self.errors.append('palette header size not a multiple of 8')
        # entities: [0x2e coll] 0x0d model 0x04 params, in order; each 0x04 closes one celglist
        pend_mesh = pend_coll = None
        for b in bl:
            if b.type == 0x0d:
                if pend_mesh is not None:
                    self.errors.append('two model blocks for one entity at 0x%x' % b.off)
                pend_mesh = b
            elif b.type in (0x2e, 0x2f):
                pend_coll = b
            elif b.type in (0x04, 0x1f):
                e = Entity(data, b, len(self.entities))
                if pend_mesh is not None:
                    e.mesh_block = pend_mesh
                    try:
                        e.mesh = Mesh(data, pend_mesh.off + 4)
                    except ValueError as ex:
                        self.errors.append('entity %d: %s' % (e.index, ex))
                e.coll_block = pend_coll
                pend_mesh = pend_coll = None
                self.entities.append(e)
        if pend_mesh is not None or pend_coll is not None:
            self.errors.append('model or collision block after the last entity')
        if len(self.entities) != self.entityCnt:
            self.errors.append('%d entities, map header says %d' % (len(self.entities), self.entityCnt))
        # "LOD": one {s32 entity index (-1 none), f32 (1.0 when set, 99999.0 when not; not read)} per entity ->
        # celglist+0x34 = &pCelList[index]. In the data it links a state to its alternative (KeypadLocked ->
        # KeypadUnlocked, Wire -> WireBurned, tv -> tv_damaged), not a lower level of detail
        lod = [b for b in bl if b.type == 0x27]
        if lod:
            if lod[0].size != 4 + 8 * self.entityCnt:
                self.errors.append('LOD block size %d for %d entities' % (lod[0].size, self.entityCnt))
            else:
                for i, e in enumerate(self.entities):
                    e.lod, e.lod_word = struct.unpack_from('<iI', data, lod[0].off + 4 + 8 * i)
        self.hashlist = []
        for b in bl:
            if b.type == 0x2c:
                self.hashlist += list(struct.unpack_from('<%dI' % ((b.size - 4) // 4), data, b.off + 4))
        paths = types.count(0x19)
        if paths != self.pathCnt:
            self.errors.append('%d path blocks, header says %d' % (paths, self.pathCnt))
        # placements (0x1a): records of 0x4c + 8*n bytes must tile the block exactly
        for b in bl:
            if b.type == 0x1a:
                p, e = b.off + 4, b.off + b.size
                while p < e:
                    n = struct.unpack_from('<I', data, p + 0x48)[0]
                    p += 0x4c + 8 * n
                if p != e:
                    self.errors.append('placement records do not tile block at 0x%x' % b.off)
            elif b.type == 0x21:
                n = struct.unpack_from('<I', data, b.off + 4)[0]
                if b.size != 8 + 56 * n:
                    self.errors.append('portal block size %d for %d portals' % (b.size, n))
            elif b.type in (0x2e, 0x2f):
                # Coll_Data_New: u32 version (5: 16 more header bytes), u16 nBoxes, u16 nB, u16 nC, then
                # nC*64 + nBoxes*48 + nB*8 bytes, then the rest of the block (collision geometry, not decoded here)
                ver, nbox, nb, nc = struct.unpack_from('<IHHH', data, b.off + 4)
                if 0x10 + (0x10 if ver == 5 else 0) + nc * 64 + nbox * 48 + nb * 8 > b.size:
                    self.errors.append('collision block at 0x%x too small for its counts' % b.off)
            elif b.type in (0x1c, 0x1d) and b.size != 4:
                self.errors.append('marker block 0x%x with payload' % b.type)
        # the tail after END must be zero padding shorter than 32 bytes
        end = bl[-1].off + bl[-1].size
        tail = data[end:]
        if any(tail) or len(tail) >= 32:
            self.errors.append('tail after END: %d bytes, non-zero %s' % (len(tail), any(tail)))
        self.by_hash = {e.hash: e for e in self.entities if e.hash != 0xffffffff}

    def validate(self):
        errs = list(self.errors)
        for e in self.entities:
            if e.name_pad and any(c != 0xff for c in e.name_pad):
                errs.append('entity %d name padding not 0xff' % e.index)
            if e.mesh is None:
                errs.append('entity %d has no model block' % e.index)
                continue
            if e.mesh.hash != e.hash:
                errs.append('entity %d hash %08x, its model says %08x' % (e.index, e.hash, e.mesh.hash))
            for x in validate_mesh(e.mesh, e.mesh_block):
                errs.append('entity %d %08x %s: %s' % (e.index, e.hash, e.name, x))
        return errs


def parse_pack(data):
    return Pack(data)


# ---------------------------------------------------------------------------------------------------------------
# Data sources
# ---------------------------------------------------------------------------------------------------------------
def iter_packs(lvl=LVL, dump=DUMP, bundles=True, dumped=True):
    """Yield (source, bundle entry type or None, NAME, bytes) for every 01 file: all bundles, then the dump."""
    import bundle
    if bundles:
        for fn in sorted(glob.glob(os.path.join(lvl, '07*.bin'))):
            d = open(fn, 'rb').read()
            for typ, name, off, ln in bundle.walk(d):
                if name.lower().startswith('01'):
                    yield os.path.basename(fn)[:8], typ, name.lower(), d[off:off + ln]
    if dumped:
        for fn in sorted(glob.glob(os.path.join(dump, '01*.bin'))):
            yield 'dump', None, os.path.basename(fn)[:8].lower(), open(fn, 'rb').read()


def bundle_contents(fn):
    """{'packs': [(name, bytes)], 'skins': {hash: bytes}} for one level bundle"""
    import bundle
    d = open(fn, 'rb').read()
    packs, skins = [], {}
    for typ, name, off, ln in bundle.walk(d):
        if name.lower().startswith('01'):
            packs.append((name.lower(), d[off:off + ln]))
        elif name.lower().startswith('05'):
            skins[int(name, 16)] = d[off:off + ln]
    return dict(packs=packs, skins=skins)


# ---------------------------------------------------------------------------------------------------------------
# Skinning check
# ---------------------------------------------------------------------------------------------------------------
SLEEVE_TABLE = [0x020009d0, 0x02000900, 0x020001f7, 0x02000929, 0x020009e6, 0x020009ff, 0x02000a14, 0x02000a13]


def bind_positions(s, skel):
    import parse_skin
    return parse_skin.world_bind(s, skel)


def seg_dist(p, a, b):
    ab = [b[k] - a[k] for k in range(3)]
    l2 = sum(x * x for x in ab)
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, sum((p[k] - a[k]) * ab[k] for k in range(3)) / l2))
    return math.dist(p, [a[k] + t * ab[k] for k in range(3)])


def bone_distance_fn(s, bindpos):
    """distance from a point to bone b = to the segments joint(b) -> joint(child) for its used children
    (to the joint itself for a leaf bone)"""
    used = [b for b, v in enumerate(s['bones']) if v & 0x80]
    kids = collections.defaultdict(list)
    for b in used:
        par = s['bones'][b] & 0x7f
        if par != 0x7f:
            kids[par].append(b)

    def dist(p, b):
        if not kids[b]:
            return math.dist(p, bindpos[b])
        return min(seg_dist(p, bindpos[b], bindpos[c]) for c in kids[b])
    return used, dist


def check_skin(s, skel, mesh):
    """Decode every vertex's bones and weight and test them against the skin: bones used by the skin, weight in
    [0,1], and in bind pose the vertex lies near its dominant bone (bone = segment from its joint to its children's
    joints; joint = -inverse bind translation). Returns (Counter, info)."""
    import parse_skin
    st = collections.Counter()
    info = {}
    bindpos = [[-c for c in t] for t, q in s['bind']] if s['bind'] else parse_skin.world_bind(s, skel)
    used, bdist = bone_distance_fn(s, bindpos)
    verts = mesh.vertices
    sw, problems = mesh.skin_weights()
    st['vertices'] = len(verts)
    st['undecodable'] = len(problems)
    info['problems'] = problems[:5]
    dists = []
    for i, w in enumerate(sw):
        if w is None:
            continue
        (ba, wa), (bb, wb) = w
        if not (0.0 <= wa <= 1.0):
            st['weight out of [0,1]'] += 1
        if wa not in (0.0, 1.0):
            st['blended'] += 1
        if ba >= len(bindpos) or bb >= len(bindpos):
            st['bone out of range'] += 1
            continue
        if ba not in used or (wb > 0 and bb not in used):
            st['bone not used by skin'] += 1
            continue
        p = verts[i]['pos']
        dom = ba if wa >= 0.5 else bb
        d = bdist(p, dom)
        dists.append(d)
        rank = sum(1 for b in used if bdist(p, b) < d)
        if rank > 2:
            st['dominant bone not among 3 nearest'] += 1
    if dists:
        dists.sort()
        info['dist'] = (dists[len(dists) // 2], dists[int(len(dists) * .95)], dists[-1])
    # size of the model, to judge the distances
    ys = [v['pos'][1] for v in verts]
    info['height'] = max(ys) - min(ys) if ys else 0
    return st, info


def check_discrete(s, skel, mesh, bone):
    """A rigid part is drawn with boneMtxs[bone] (AnimObjectUpdate copies it to the child object's matrix), so its
    vertices should be bone-local: in bind pose (rotation identity) model position = v + joint(bone). Compare that
    hypothesis with 'the vertices are already in model space'."""
    import parse_skin
    bindpos = [[-c for c in t] for t, q in s['bind']] if s['bind'] else parse_skin.world_bind(s, skel)
    used, bdist = bone_distance_fn(s, bindpos)
    jp = bindpos[bone]
    loc, mod, near = [], [], collections.Counter()
    for v in mesh.vertices:
        p = v['pos']
        q = [p[k] + jp[k] for k in range(3)]
        dl = {b: bdist(q, b) for b in used}
        nb = min(dl, key=dl.get)
        near[nb] += 1
        loc.append(dl[nb])
        mod.append(min(bdist(p, b) for b in used))
    loc.sort(); mod.sort()
    # simpler, stronger: distance of the vertices from the part's own joint under each hypothesis
    own_l = sorted(math.dist(v['pos'], (0, 0, 0)) for v in mesh.vertices)
    own_m = sorted(math.dist(v['pos'], jp) for v in mesh.vertices)
    fam = {bone, s['bones'][bone] & 0x7f} | {b for b in used if s['bones'][b] & 0x7f == bone}
    top = near.most_common(1)[0][0]
    return dict(local=loc[len(loc) // 2], model=mod[len(mod) // 2], own=top in fam,
                own_local=own_l[len(own_l) // 2], own_model=own_m[len(own_m) // 2], joint_dist=math.dist(jp, (0, 0, 0)),
                nearest=near.most_common(3))


def posed_stretch(s, mesh, seqs, skel_seq, nseq=3):
    """Pose the mesh with the middle frame of up to nseq sequences (pose_ref.build_palette, then
    v' = wA*(v*R_A + t_A) + wB*(v*R_B + t_B)) and return the worst median / 99th percentile / max ratio of posed
    to bind edge length. A wrong bone or weight decode tears the mesh (99% ratios of 10-200)."""
    import pose_ref
    ident = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    sw, _ = mesh.skin_weights()
    if any(w is None for w in sw):
        return None
    V = [v['pos'] for v in mesh.vertices]
    edges = {tuple(sorted(x)) for a, b, c, _ in mesh.triangles() for x in ((a, b), (b, c), (a, c))}
    used = [bool(b & 0x80) for b in s['bones']]
    bind_t = [bt for bt, _ in s['bind']]
    worst = (0, 0, 0)
    for q in seqs[:nseq]:
        trans, quats = q.frame(max(1, q.numFrames // 2), skel_seq, s['scale'], used)
        trans = [t if t else [0, 0, 0] for t in trans]
        quats = [x if x else [0, 0, 0, 1] for x in quats]
        trans[0] = [0, 0, 0]
        _, pal = pose_ref.build_palette(s['bones'], trans, quats, ident, [0, 0, 0], bind_t)
        P = []
        for i, v in enumerate(V):
            (ba, wa), (bb, wb) = sw[i]
            pa = pose_ref.xform(pal[ba][0], pal[ba][1], v)
            pb = pose_ref.xform(pal[bb][0], pal[bb][1], v) if wb else pa
            P.append([wa * pa[k] + wb * pb[k] for k in range(3)])
        r = sorted(math.dist(P[a], P[b]) / max(1e-6, math.dist(V[a], V[b])) for a, b in edges)
        cur = (r[len(r) // 2], r[int(len(r) * .99)], r[-1])
        worst = tuple(max(worst[k], cur[k]) for k in range(3))
    return worst


def bundle_sequences(fn):
    """{skeleton number: [decode_seq.Sequence]} (bone sequences only) and the bundle's skeletons"""
    import bundle, decode_seq
    d = open(fn, 'rb').read()
    out = collections.defaultdict(list)
    for typ, name, off, ln in bundle.walk(d):
        if name.lower().startswith('04'):
            try:
                q = decode_seq.Sequence(d[off:off + ln], name)
            except Exception:
                continue
            if not q.isMorph:
                out[q.skelNum].append(q)
    return out, decode_seq.load_skeletons(fn)


def skins_report(lvl=LVL, verbose=False, posed=False):
    import parse_skin, parse_skeleton
    skels = parse_skeleton.load_table()
    seen = {}
    tot = collections.Counter()
    for fn in sorted(glob.glob(os.path.join(lvl, '07*.bin'))):
        bc = bundle_contents(fn)
        ents = {}
        for name, d in bc['packs']:
            for e in Pack(d).entities:
                if e.hash != 0xffffffff:
                    ents.setdefault(e.hash, e)
        sleeve = next((ents[h] for h in SLEEVE_TABLE if h in ents), None)
        if posed:
            seqs, seq_skels = bundle_sequences(fn)
        for h, d in sorted(bc['skins'].items()):
            s = parse_skin.parse(d, skels)
            for i, rh in enumerate(s['riggedHash']):
                e = ents.get(rh)
                label = '%08x' % rh
                if e is None and rh & 0x00100000:
                    e = sleeve
                    label += '->sleeve %08x' % (e.hash if e else 0)
                key = (h, label, e.mesh.hash if e and e.mesh else None,
                       (e.mesh.vtxCnt, e.mesh.idxCnt) if e and e.mesh else None)
                if key in seen:
                    continue
                if e is None or e.mesh is None or e.mesh.empty:
                    seen[key] = 'missing'
                    tot['rigged mesh missing'] += 1
                    print('%s skin %08x rigged %s: mesh not in bundle' % (os.path.basename(fn)[:8], h, label))
                    continue
                m = e.mesh
                if not m.skinned:
                    tot['rigged mesh not skinned'] += 1
                    seen[key] = 'rigid'
                    print('%s skin %08x rigged %s (%s): mesh has no palettes' % (os.path.basename(fn)[:8], h,
                                                                                label, m.name))
                    continue
                st, info = check_skin(s, skels[s['skel']], m)
                seen[key] = st
                tot['rigged meshes checked'] += 1
                bad = st['undecodable'] + st['weight out of [0,1]'] + st['bone not used by skin'] + \
                    st['bone out of range']
                tot['rigged meshes clean'] += bad == 0
                for k, v in st.items():
                    tot[k] += v
                dd = info.get('dist', (0, 0, 0))
                if posed and s['bind'] and seqs.get(s['skel']):
                    ps = posed_stretch(s, m, seqs[s['skel']], seq_skels[s['skel']])
                    if ps:
                        tot['posed: meshes'] += 1
                        tot['posed: 99% edge stretch < 2.5'] += ps[1] < 2.5
                        tot['posed: max edge stretch < 15'] += ps[2] < 15
                        info['posed'] = ps
                        if verbose or ps[1] >= 2.5:
                            print('%s skin %08x %s posed: edge stretch median %.3f 99%% %.3f max %.3f' % (
                                os.path.basename(fn)[:8], h, m.name, ps[0], ps[1], ps[2]))
                if verbose or bad:
                    print('%s skin %08x skel %2d rigged %-26s %-28s v=%5d pal=%2d undec=%d wbad=%d unused=%d '
                          'blend=%4d notnear3=%3d h=%.2f dist med %.3f 95%% %.3f max %.3f' % (
                              os.path.basename(fn)[:8], h, s['skel'], label, m.name[:28], m.vtxCnt, m.paletteCnt,
                              st['undecodable'], st['weight out of [0,1]'], st['bone not used by skin'],
                              st['blended'], st['dominant bone not among 3 nearest'], info['height'], dd[0], dd[1],
                              dd[2]))
                    for pr in info.get('problems', []):
                        print('      problem vertex', pr)
            for bone, dh in zip(s['discreteBone'], s['discreteHash']):
                e = ents.get(dh)
                key = (h, 'd%08x' % dh, bone)
                if key in seen:
                    continue
                seen[key] = 1
                if e is None or e.mesh is None or e.mesh.empty:
                    tot['discrete mesh missing'] += 1
                    print('%s skin %08x discrete %08x on bone %d: %s' % (
                        os.path.basename(fn)[:8], h, dh, bone, 'not in bundle' if e is None else 'empty mesh %s' % (
                            e.mesh.name if e.mesh else '')))
                    continue
                tot['discrete meshes checked'] += 1
                if e.mesh.skinned:
                    tot['discrete mesh skinned'] += 1
                r = check_discrete(s, skels[s['skel']], e.mesh, bone)
                if r['joint_dist'] < 0.05:
                    tot['discrete: joint at the origin (hypotheses indistinguishable)'] += 1
                else:
                    tot['discrete: vertices nearer the joint as bone-local'] += r['own_local'] < r['own_model']
                    tot['discrete: bone-local nearest bone is own/parent/child'] += r['own']
                if verbose or (r['joint_dist'] >= 0.05 and r['own_local'] >= r['own_model']):
                    print('%s skin %08x skel %2d discrete %08x %-22s bone %2d v=%4d joint |%.3f|: median |v| %.3f, '
                          '|v - joint| %.3f; nearest bone to v+joint %s (median %.3f; model-space %.3f)' % (
                              os.path.basename(fn)[:8], h, s['skel'], dh, e.mesh.name[:22], bone, e.mesh.vtxCnt,
                              r['joint_dist'], r['own_local'], r['own_model'], r['nearest'], r['local'], r['model']))
    print(dict(tot))
    return tot


# ---------------------------------------------------------------------------------------------------------------
# OBJ export (sanity artefact)
# ---------------------------------------------------------------------------------------------------------------
def write_obj(mesh, path, offset=(0, 0, 0), groups_by='bone', skin_weights=None):
    tris = mesh.triangles()
    with open(path, 'w') as f:
        f.write('# %s %08x vtx %d tris %d palettes %d\n' % (mesh.name, mesh.hash, mesh.vtxCnt, len(tris),
                                                          mesh.paletteCnt))
        for v in mesh.vertices:
            x, y, z = (v['pos'][k] + offset[k] for k in range(3))
            f.write('v %.6f %.6f %.6f\n' % (x, y, z))
        for v in mesh.vertices:
            f.write('vt %.6f %.6f\n' % (v['uv'][0], 1.0 - v['uv'][1]))
        for v in mesh.vertices:
            f.write('vn %.5f %.5f %.5f\n' % v['normal'])
        byprim = collections.defaultdict(list)
        for a, b, c, pi in tris:
            byprim[pi].append((a, b, c))
        prims = mesh.prims
        for pi in sorted(byprim):
            f.write('g prim%d_tex%d\n' % (pi, prims[pi].tex0))
            for a, b, c in byprim[pi]:
                f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (a + 1, a + 1, a + 1, b + 1, b + 1, b + 1, c + 1, c + 1,
                                                          c + 1))


def find_entity(h, bundle_name=None, lvl=LVL):
    for src, typ, name, d in iter_packs(lvl, dumped=False):
        if bundle_name and src.lower() != bundle_name.lower():
            continue
        try:
            p = Pack(d)
        except ValueError:
            continue
        if h in p.by_hash and p.by_hash[h].mesh:
            return src, name, p.by_hash[h]
    return None


# ---------------------------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------------------------
def winding_check(m):
    """+1 / -1 votes: does cross(b-a, c-a) of each triangle agree with the mean of its vertex normals?"""
    vs = m.vertices
    agree = disagree = 0
    for a, b, c, _ in m.triangles():
        pa, pb, pc = vs[a]['pos'], vs[b]['pos'], vs[c]['pos']
        u = [pb[k] - pa[k] for k in range(3)]
        w = [pc[k] - pa[k] for k in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        vn = [vs[a]['normal'][k] + vs[b]['normal'][k] + vs[c]['normal'][k] for k in range(3)]
        dot = sum(n[k] * vn[k] for k in range(3))
        if dot > 0:
            agree += 1
        elif dot < 0:
            disagree += 1
    return agree, disagree


def validate_all(verbose=False):
    files = ok = 0
    blocks = collections.Counter()
    bytes_by = collections.Counter()
    meshes = collections.Counter()
    wind = [0, 0]
    nlen = []
    stats = collections.Counter()
    seen_mesh = set()
    for src, typ, name, d in iter_packs():
        files += 1
        try:
            p = Pack(d)
        except ValueError as ex:
            print('%s %s: %s' % (src, name, ex))
            continue
        for b in p.blocks:
            blocks[b.type] += 1
            bytes_by[b.type] += b.size
        bytes_by['file header'] += 4
        bytes_by['tail padding'] += len(d) - (p.blocks[-1].off + p.blocks[-1].size)
        errs = p.validate()
        ok += not errs
        if errs:
            print('%s %s (type %s): %d problems' % (src, name, typ, len(errs)))
            for e in errs[:8]:
                print('    ' + e)
        for e in p.entities:
            m = e.mesh
            if m is None:
                continue
            meshes['total'] += 1
            key = (m.hash, m.name, m.vtxCnt, m.idxCnt, d[m.off:m.end][:4096])
            if key in seen_mesh:
                continue
            seen_mesh.add(key)
            meshes['distinct'] += 1
            if m.empty:
                meshes['empty'] += 1
                continue
            meshes['skinned' if m.skinned else 'rigid'] += 1
            if m.morphCnt:
                meshes['with morphs'] += 1
            stats['export flags %x' % m.exportFlags] += 1
            a, dd = winding_check(m)
            wind[0] += a
            wind[1] += dd
            for v in m.vertices:
                x, y, z = v['normal']
                nlen.append(math.sqrt(x * x + y * y + z * z))
            for pr in m.prims:
                stats['change %02x' % pr.change] += 1
                if pr.tex0 == 0xffff:
                    stats['tex0 none'] += 1
                if m.skinned and pr.palette == 0:
                    stats['skinned prim with palette 0 (carry)'] += 1
            if m.degen_mismatch:
                stats['+0x18 differs from the degenerate triangles in the strips'] += 1
                if m.degen_mismatch > 0:
                    stats['  ... by more (psiGetTriList would write past its buffer)'] += 1
            if m.prims[0].change != 0x7b:
                stats['first prim change mask != 0x7b'] += 1
    print('%d/%d graphics packs validate (every byte of every block accounted for)' % (ok, files))
    print('blocks: ' + ', '.join('%02x %s %d' % (t, BLOCK_NAMES.get(t, '?'), n) for t, n in sorted(blocks.items())))
    print('bytes:  ' + ', '.join('%s %d' % ('%02x' % t if isinstance(t, int) else t, n)
                                 for t, n in sorted(bytes_by.items(), key=lambda x: str(x[0]))))
    print('meshes: %s' % dict(meshes))
    print('winding (distinct non-empty meshes): %d triangles agree with the vertex normals, %d disagree' % tuple(wind))
    if nlen:
        nlen.sort()
        print('normal length min %.3f median %.3f max %.3f' % (nlen[0], nlen[len(nlen) // 2], nlen[-1]))
    if verbose:
        for k, v in sorted(stats.items()):
            print('   %s: %d' % (k, v))
    return ok == files


def main(argv):
    if '--skins' in argv:
        skins_report(verbose='-v' in argv, posed='--posed' in argv)
        return
    if '--obj' in argv:
        i = argv.index('--obj')
        h = int(argv[i + 1], 16)
        out = argv[i + 2]
        bname = argv[argv.index('--bundle') + 1] if '--bundle' in argv else None
        r = find_entity(h, bname)
        if not r:
            sys.exit('entity %08x not found' % h)
        src, name, e = r
        os.makedirs(out, exist_ok=True)
        off = (0, 0, 0)
        if '--skin' in argv:
            import parse_skin, parse_skeleton
            skels = parse_skeleton.load_table()
            sh = int(argv[argv.index('--skin') + 1], 16)
            bc = bundle_contents(os.path.join(LVL, src + '.bin'))
            s = parse_skin.parse(bc['skins'][sh], skels)
            if h in s['discreteHash']:
                b = s['discreteBone'][s['discreteHash'].index(h)]
                off = tuple(parse_skin.world_bind(s, skels[s['skel']])[b])
                print('discrete part on bone %d, moved by %s' % (b, off))
        fn = os.path.join(out, '%08x.obj' % h)
        write_obj(e.mesh, fn, off)
        print('%s %s entity %d %08x %s: %d vertices, %d triangles -> %s' % (
            src, name, e.index, h, e.mesh.name, e.mesh.vtxCnt, len(e.mesh.triangles()), fn))
        return
    ok = validate_all('-v' in argv)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main(sys.argv[1:])
