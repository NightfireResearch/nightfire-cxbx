#!/usr/bin/env python3
"""Export the level collision geometry of a Nightfire driving-engine track (.crp) to Wavefront OBJ.

    python tools/carp_collision.py <file.crp> <out.obj> [--list] [--query X,Y,Z ...]

The .crp is a serialised EAGL UGroup tree ('CARP'); docs/driving-collision.md has the format and the
code addresses behind every field used here. In short:

  CARP (root group)
    CDat group      collision data (WCollisionMgr::Init 0xc55b0, WGrid::Init 0xc5fc0)
      'CGrd'        the WGrid itself (origin, cell size, rows, cols)
      'ci'          WCollisionInstance[]  (64 bytes each)
      'co'          WCollisionObject[]    (48 bytes each: OBB and cylinder objects)
      'cn'+index    WGridNode             (per-cell lists of instance/trigger/object indices)
      'de'+n        dynamic grid elements (instance or object indices placed at run time)
      'sr'+n        symbolic references "CARP::<article>" used by ci +0x1c
    Arti groups     one per article; data 'ca'+n = collision geometry of that article (local space)
    Map  group      'in' = CARP::Instance[] (render instances, 64 bytes), used by 'co' +0x24

Each collision instance stores the WORLD->LOCAL matrix of its article (MakeMatrix 0xbe8b0); the
geometry is converted to world space with its orthonormal inverse, as FUN_000beeb0 does for the
faces it returns. Triangle strips are wound as the engine's one-sided point-in-triangle tests
expect (FUN_000bef30), which makes walkable ground face +Y.

--list prints a summary and does not need <out.obj> (pass '-' or anything). --query prints what
WCollisionMgr::GetWorldHeightAtPoint (0xbf210) would return at a point, from the file's data alone
(dynamic elements that move at run time are taken at their load-time placement).

Standard library only.
"""

import argparse
import math
import os
import struct
import sys

# ---------------------------------------------------------------------------------------------
# UGroup (EAGL serialised tree). Entries are 16 bytes: tag, flags, count, offset.
#   group entry:  flags>>5 = child groups, +8 = data entries, +0xc = offset of the child array in
#                 16-byte units, relative to the entry when flags&2 (UGroup::GetArray 0x117940).
#                 Children first, then the data entries (UGroup::DataLocateTag 0x117b50).
#   data entry:   flags>>8 = size in bytes, flags&1 = tag carries an index in its low 16 bits,
#                 +8 = count/element index, +0xc = byte offset relative to the entry when flags&2
#                 (UGroup::ResolveOffsets 0x117cb0).
# ---------------------------------------------------------------------------------------------


def tag(s):
    return struct.unpack('>I', s.encode('latin1'))[0]


def tagname(t):
    b = struct.pack('>I', t)
    if t & 0xffff and not all(32 <= c < 127 for c in b[2:]):
        return '%s+%d' % (b[:2].decode('latin1'), t & 0xffff)
    return b.decode('latin1').rstrip('\0 ') or '?'


class Data:
    __slots__ = ('tag', 'flags', 'count', 'ptr')

    def __init__(self, t, f, c, p):
        self.tag, self.flags, self.count, self.ptr = t, f, c, p

    @property
    def size(self):
        return self.flags >> 8


class Group:
    def __init__(self, buf, off):
        self.buf, self.off = buf, off
        self.tag, self.flags, self.ndata, rel = struct.unpack_from('<4I', buf, off)
        self.base = off + rel * 16 if self.flags & 2 else rel
        self.ngroups = self.flags >> 5

    def groups(self):
        return [Group(self.buf, self.base + i * 16) for i in range(self.ngroups)]

    def data(self):
        out = []
        for i in range(self.ndata):
            eo = self.base + (self.ngroups + i) * 16
            t, f, c, rel = struct.unpack_from('<4I', self.buf, eo)
            out.append(Data(t, f, c, (eo + rel) if f & 2 else rel))
        return out

    def find(self, t):
        for e in self.data():
            if e.tag == t:
                return e
        return None


def cstr(buf, p):
    return buf[p:buf.index(b'\0', p)].decode('latin1')


# ---------------------------------------------------------------------------------------------
# Surfaces. Face byte +0xc of a strip vertex's w word (copied to WWorldPos +0x2c) indexes the car
# tables friction[]/lateralLoss[] registered in InitializeBondCarGlobals (0x61b00..): friction[k]
# lives at 0x1c3728 + 4*k. 11, 14 and 15 have no registered name.
# ---------------------------------------------------------------------------------------------
SURFACES = {0: 'NODRIVE', 1: 'PAVED', 2: 'GRAVEL', 3: 'GRASS', 4: 'COBBLE', 5: 'DIRT', 6: 'WATER',
            7: 'WOOD', 8: 'ICE', 9: 'SNOW', 10: 'PAVED_ROUGH', 12: 'RAILROAD', 13: 'METAL'}

# WCollisionMgr ctor (0xc5500) sets its face mask (+0x2c) to 0xf0; faces whose flag byte shares a
# bit with it are ignored by the manager's queries. Flag 0x04 is skipped by point queries.
MGR_MASK = 0xf0


def surface_name(s):
    return 'surf%02d_%s' % (s, SURFACES.get(s, 'UNKNOWN'))


def mat_rows_inverse_apply(rows, t, p):
    """world = row_i . (p - t): the inverse of local = p*R + t for orthonormal R (OrthoInverse
    0x114e80 then VU0_MATRIX4_vect3mult 0x116240, row-vector convention)."""
    d0, d1, d2 = p[0] - t[0], p[1] - t[1], p[2] - t[2]
    return tuple(r[0] * d0 + r[1] * d1 + r[2] * d2 for r in rows)


def mat_rows_apply(rows, t, p):
    """local = p.x*row0 + p.y*row1 + p.z*row2 + t (VU0_MATRIX4_vect3mult)."""
    return tuple(p[0] * rows[0][j] + p[1] * rows[1][j] + p[2] * rows[2][j] + t[j] for j in range(3))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


# ---------------------------------------------------------------------------------------------
# Collision article ('ca' data in an Arti group), local space
#   +0x00 u16 strip count        +0x02 u16 barrier offset (from +0x20)   +0x04 u16 barrier count
#   +0x06..+0x1f not read by the collision code
#   +0x20 WCollisionStripSphere[count]: float cx,cy,cz; u16 radius*16; u16 strip offset (from +0x20)
#   strip: vertices of 16 bytes (x,y,z,w). v0.w = u32 vertex count; v1.w = u32 strip flags
#          (bit0: first triangle wound the other way, bit1: two-sided). For triangle i
#          (vertices i..i+2) the attributes live in v[i+2].w: u8 surface, u8 flags, u16 radius*16.
#   barrier (32 bytes): float x0,ymin,z0; u8 surface,u8 flags,u16 ?; float x1,ymax,z1, float 1/len
# ---------------------------------------------------------------------------------------------


class Article:
    def __init__(self, buf, p, size):
        self.p, self.size = p, size
        self.nstrips, self.boff, self.nbarriers = struct.unpack_from('<3H', buf, p)
        self.spheres = []   # (centre, radius, strip offset)
        self.strips = []    # (flags, [(x,y,z)], [(surface, flags, radius)] per triangle)
        for s in range(self.nstrips):
            cx, cy, cz, r16, off = struct.unpack_from('<3fHH', buf, p + 0x20 + s * 16)
            self.spheres.append(((cx, cy, cz), r16 / 16.0, off))
            sp = p + 0x20 + off
            n = struct.unpack_from('<I', buf, sp + 0xc)[0]
            sflags = struct.unpack_from('<I', buf, sp + 0x1c)[0]
            verts = [struct.unpack_from('<3f', buf, sp + k * 16) for k in range(n)]
            attrs = [struct.unpack_from('<BBH', buf, sp + k * 16 + 0xc) for k in range(2, n)]
            self.strips.append((sflags, verts, attrs))
        self.barriers = []
        for b in range(self.nbarriers):
            bp = p + 0x20 + self.boff + b * 32
            x0, y0, z0 = struct.unpack_from('<3f', buf, bp)
            surf, bflags, extra = struct.unpack_from('<BBH', buf, bp + 0xc)
            x1, y1, z1, inv_len = struct.unpack_from('<4f', buf, bp + 0x10)
            self.barriers.append(((x0, y0, z0), (x1, y1, z1), surf, bflags, inv_len))

    def triangles(self):
        """Yield (a, b, c, surface, flags, strip_flags): wound so that the engine's one-sided
        XZ test accepts the triangle from above, i.e. (b-a)x(c-a) has +Y for walkable faces."""
        for sflags, verts, attrs in self.strips:
            parity = sflags & 1
            for i in range(len(verts) - 2):
                a, b, c = verts[i], verts[i + 1], verts[i + 2]
                if parity:
                    a, b = b, a
                surf, fl, _ = attrs[i]
                yield a, b, c, surf, fl, sflags
                parity ^= 1


class Track:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.buf = buf = f.read()
        self.root = Group(buf, 0)
        if self.root.tag != tag('CARP'):
            raise SystemExit('%s: not a CARP file (magic %r)' % (path, buf[:4]))
        self.top = self.root.groups()
        byname = {}
        for g in self.top:
            byname.setdefault(tagname(g.tag), []).append(g)
        self.cdat = byname['CDat'][0]
        self.mapg = byname.get('Map', [None])[0]
        # articles by name ('Name' data of each Arti group)
        self.articles = {}
        for g in byname.get('Arti', []):
            n = g.find(tag('Name'))
            if n:
                self.articles[cstr(buf, n.ptr)] = g
        self._article_cache = {}
        data = self.cdat.data()
        self.sr = {e.tag & 0xffff: cstr(buf, e.ptr) for e in data if e.tag >> 16 == 0x7372}
        # grid
        g = self.cdat.find(tag('CGrd'))
        ox, oy, oz, ow, self.cell, self.inv_cell, self.rows, self.cols = struct.unpack_from(
            '<6f2I', buf, g.ptr)
        self.origin = (ox, oy, oz)
        self.nodes = {}
        for e in data:
            if e.tag >> 16 == 0x636e:   # 'cn': node index in +8 (WGrid::Init stores nodes[e+8])
                self.nodes[e.count] = e.ptr
        # dynamic elements 'de': u16 index, u16 ?, u32 kind (0 instance, 2 object)
        self.dynamic = []
        for e in data:
            if e.tag >> 16 == 0x6465:
                idx, _, kind = struct.unpack_from('<HHI', buf, e.ptr)
                self.dynamic.append((kind, idx))
        # collision instances
        ci = self.cdat.find(tag('ci\0\0'))
        self.instances = []
        for i in range(ci.count if ci else 0):
            self.instances.append(Instance(self, i, ci.ptr + i * 64))
        # collision objects
        co = self.cdat.find(tag('co\0\0'))
        self.objects = []
        rinst = self.mapg.find(tag('in  ')) if self.mapg else None
        if rinst is None and self.mapg:
            for e in self.mapg.data():
                if e.tag >> 16 == 0x696e:
                    rinst = e
                    break
        self.render_instances = rinst
        for i in range(co.count if co and co.size else 0):
            self.objects.append(CObject(self, i, co.ptr + i * 48))

    def article(self, name, which=0):
        key = (name, which)
        if key not in self._article_cache:
            g = self.articles.get(name)
            art = None
            if g is not None:
                for e in g.data():
                    if e.tag == (0x63610000 | which):
                        art = Article(self.buf, e.ptr, e.size)
                        break
            self._article_cache[key] = art
        return self._article_cache[key]

    def article_ca_count(self, name):
        g = self.articles.get(name)
        return 0 if g is None else sum(1 for e in g.data() if e.tag >> 16 == 0x6361)

    def render_matrix(self, idx):
        p = self.render_instances.ptr + idx * 64
        rows = [struct.unpack_from('<3f', self.buf, p + k * 16) for k in range(3)]
        return rows, struct.unpack_from('<3f', self.buf, p + 0x30)

    def node_list(self, node_index, kind):
        """WGridNode lists (FUN_000bfee0): u8 count[kind] at +4, u16 offset[kind] at +8,
        u16 indices at node + 0x10 + offset."""
        p = self.nodes.get(node_index)
        if p is None:
            return []
        n = self.buf[p + 4 + kind]
        off = struct.unpack_from('<H', self.buf, p + 8 + kind * 2)[0]
        return list(struct.unpack_from('<%dH' % n, self.buf, p + 0x10 + off))

    def cell_of(self, x, z):
        """WGrid::RangeCheckROWCOL (0xc5710): col from x, row from z, clamped."""
        col = int((x - self.origin[0]) * self.inv_cell)
        row = int((z - self.origin[2]) * self.inv_cell)
        col = min(max(col, 0), self.cols - 1)
        row = min(max(row, 0), self.rows - 1)
        return row * self.cols + col

    def height_at(self, p):
        """WCollisionMgr::GetWorldHeightAtPoint (0xbf210) -> WWorldPos::FindClosestFace(p, true)
        (0xd31f0) -> GetInstanceList (0xc4510, radius 0) -> FindFaceInCInst point overload
        (0xbf4c0) per instance -> GetPlaneY (0xd2be0). Returns (height, instance, face) or None."""
        cands = set(self.node_list(self.cell_of(p[0], p[2]), 0))
        cands.update(idx for kind, idx in self.dynamic if kind == 0)
        best = None
        for i in sorted(cands):
            inst = self.instances[i]
            art = inst.article()
            if art is None:
                continue
            c = inst.position()
            dx, dz = abs(c[0] - p[0]), abs(c[2] - p[2])
            if max(dx, dz) + 0.25 * min(dx, dz) >= inst.radius:
                continue
            hit = inst.find_face(p)
            if hit and (best is None or hit[0] < best[0]):
                best = hit + (inst,)
        if best is None:
            return None
        dist, tri_world, inst = best
        a, b, c = tri_world
        n = cross(sub(b, a), sub(a, c))       # FUN_0005d3f0: (v1-v0) x (v0-v2), forced +Y
        ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        n = (0.0, 1.0, 0.0) if ln == 0 else (n[0] / ln, n[1] / ln, n[2] / ln)
        if n[1] < 0:
            n = (-n[0], -n[1], -n[2])
        if n[1] == 0:
            h = a[1]
        else:
            h = a[1] - ((p[0] - a[0]) * n[0] + (p[2] - a[2]) * n[2]) / n[1]
        return h, inst, tri_world


class Instance:
    """WCollisionInstance, 64 bytes (WCollisionMgr 'ci')."""

    def __init__(self, track, index, p):
        self.track, self.index, self.p = track, index, p
        buf = track.buf
        f = struct.unpack_from('<16f', buf, p)
        self.row0 = f[0:3]
        self.half_x = f[3]
        self.half_y = f[5]
        self.flags, self.b19, self.render_index = struct.unpack_from('<BBH', buf, p + 0x18)
        self.ref = struct.unpack_from('<I', buf, p + 0x1c)[0]
        self.row2 = f[8:11]
        self.half_z = f[11]
        self.t = f[12:15]
        self.radius = f[15]
        if self.flags & 3:
            self.row1 = cross(self.row2, self.row0)       # FUN_00115cc0(row2,row0)
        else:
            self.row1 = (0.0, 1.0, 0.0)
        self.rows = (self.row0, self.row1, self.row2)
        name = track.sr.get(self.ref & 0xffff, '') if self.ref >> 16 == 0x7372 else ''
        self.article_name = name[6:] if name.startswith('CARP::') else name

    def article(self):
        return self.track.article(self.article_name)

    def to_world(self, v):
        return mat_rows_inverse_apply(self.rows, self.t, v)

    def to_local(self, v):
        return mat_rows_apply(self.rows, self.t, v)

    def position(self):
        """WCollisionInstance::CalcPosition (0xbe810)."""
        return self.to_world((0.0, 0.0, 0.0))

    def find_face(self, p):
        """Point overload of FindFaceInCInst (FUN_000bf4c0) with FUN_000bef30 per strip."""
        art = self.article()
        lp = self.to_local(p)
        if not (-self.half_x <= lp[0] <= self.half_x and -self.half_z <= lp[2] <= self.half_z):
            return None
        best = None
        for (centre, r, _), (sflags, verts, attrs) in zip(art.spheres, art.strips):
            dx, dz = centre[0] - lp[0], centre[2] - lp[2]
            if dx * dx + dz * dz >= r * r:
                continue
            tri = first_tri_in_strip(lp, sflags, verts, attrs)
            if tri is None:
                continue
            ymin = min(tri[0][1], tri[1][1], tri[2][1])
            if self.flags & 1:
                d = ymin - (lp[1] - 0.5)
            else:
                d = (lp[1] + 0.5) - ymin
            if 0.0 < d and (best is None or d < best[0]):
                best = (d, tri)
        if best is None:
            return None
        return best[0], tuple(self.to_world(v) for v in best[1])


def _edge(p, a, b):
    # (a.x-b.x)*(p.z-b.z) - (a.z-b.z)*(p.x-b.x), as in FUN_000bed40/FUN_000becb0/FUN_000bebd0
    return (a[0] - b[0]) * (p[2] - b[2]) - (a[2] - b[2]) * (p[0] - b[0])


def first_tri_in_strip(lp, sflags, verts, attrs):
    """FUN_000bef30: the first triangle of the strip containing lp in XZ, honouring the per-face
    skip bit 0x04 and the manager mask. Vertex order returned is the strip order."""
    parity = sflags & 1
    for i in range(len(verts) - 2):
        v0, v1, v2 = verts[i], verts[i + 1], verts[i + 2]
        fl = attrs[i][1]
        cur = parity
        parity ^= 1
        if fl & 4 or fl & MGR_MASK:
            continue
        e0, e1, e2 = _edge(lp, v0, v1), _edge(lp, v1, v2), _edge(lp, v2, v0)
        if sflags & 2:
            inside = (e0 >= 0 and e1 >= 0 and e2 >= 0) or (e0 <= 0 and e1 <= 0 and e2 <= 0)
        elif cur == 0:
            inside = e0 >= 0 and e1 >= 0 and e2 >= 0
        else:
            inside = e0 <= 0 and e1 <= 0 and e2 <= 0
        if inside:
            return (v0, v1, v2)
    return None


class CObject:
    """WCollisionObject, 48 bytes (WCollisionMgr 'co'): OBB (byte +0x20 == 0) or cylinder."""

    def __init__(self, track, index, p):
        buf = track.buf
        self.index = index
        self.pos = struct.unpack_from('<3f', buf, p)
        self.radius = struct.unpack_from('<f', buf, p + 0xc)[0]
        self.half = struct.unpack_from('<3f', buf, p + 0x10)
        self.cylinder = buf[p + 0x20] != 0
        self.render_index = struct.unpack_from('<H', buf, p + 0x24)[0]
        self.rows = None
        if track.render_instances is not None:
            self.rows, _ = track.render_matrix(self.render_index)


# ---------------------------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------------------------

SURF_COLOURS = {0: (0.8, 0.1, 0.1), 1: (0.4, 0.4, 0.4), 2: (0.6, 0.5, 0.3), 3: (0.2, 0.7, 0.2),
                4: (0.5, 0.4, 0.4), 5: (0.5, 0.35, 0.2), 6: (0.1, 0.3, 0.9), 7: (0.6, 0.4, 0.1),
                8: (0.7, 0.9, 1.0), 9: (0.95, 0.95, 0.95), 10: (0.3, 0.3, 0.3), 12: (0.4, 0.2, 0.1),
                13: (0.6, 0.65, 0.7)}


class RightHanded:
    """The game's coordinates are Direct3D's, left-handed; OBJ is right-handed by convention, so an importer shows
    the level mirrored. This writes through with Z negated on every vertex and every face's vertex order reversed,
    which mirrors the geometry back and keeps the faces' normals pointing the same way: the vertices are (x, y, -z)
    of the game's. The bounding box printed and --query stay in the game's coordinates."""

    def __init__(self, f):
        self.f = f

    def write(self, text):
        out = []
        for line in text.split('\n'):
            if line.startswith('v '):
                x, y, z = line[2:].split()
                line = 'v %s %s %.4f' % (x, y, -float(z))
            elif line.startswith('f '):
                line = 'f ' + ' '.join(reversed(line[2:].split()))
            out.append(line)
        self.f.write('\n'.join(out))


def write_obj(track, out_path, include_masked=True, game_coords=False):
    base = os.path.splitext(out_path)[0]
    mtl_path = base + '.mtl'
    used = set()
    vcount = 0
    dynamic = set(idx for kind, idx in track.dynamic if kind == 0)
    stats = {'instances': 0, 'faces': 0, 'vertices': 0, 'barriers': 0, 'objects': 0,
             'bbox': [[1e30] * 3, [-1e30] * 3], 'no_article': 0}
    with open(out_path, 'w', newline='\n') as raw:
        f = raw if game_coords else RightHanded(raw)
        f.write('# collision geometry of %s (tools/carp_collision.py)\n' % os.path.basename(
            track_path_of(track)))
        f.write('# world space, Y up; %s\n' % ("the game's own left-handed coordinates" if game_coords else
                "right-handed: Z is negated from the game's (--game-coords for the raw ones)"))
        f.write('# Materials: surfNN_<surface>[_fXX face flag byte]'
                '[_2s two-sided strip, facing undefined]; barrier_bXX_fYY; object_obb/cylinder\n')
        f.write('# objects: ciNNNN_<article>[_dyn = moved at run time], ciNNNN_<article>_barriers,'
                ' coNNNN_obb, coNNNN_cyl\n')
        f.write('mtllib %s\n' % os.path.basename(mtl_path))
        for inst in track.instances:
            art = inst.article()
            if art is None:
                stats['no_article'] += 1
                continue
            stats['instances'] += 1
            dyn = '_dyn' if inst.index in dynamic else ''
            if art.strips:
                f.write('o ci%04d_%s%s\n' % (inst.index, inst.article_name, dyn))
            # one vertex per strip vertex, triangles reference them
            cur_mat = None
            for sflags, verts, attrs in art.strips:
                first = vcount + 1
                for v in verts:
                    w = inst.to_world(v)
                    f.write('v %.4f %.4f %.4f\n' % w)
                    for k in range(3):
                        stats['bbox'][0][k] = min(stats['bbox'][0][k], w[k])
                        stats['bbox'][1][k] = max(stats['bbox'][1][k], w[k])
                vcount += len(verts)
                stats['vertices'] += len(verts)
                parity = sflags & 1
                for i in range(len(verts) - 2):
                    surf, fl, _ = attrs[i]
                    a, b, c = first + i, first + i + 1, first + i + 2
                    if parity:
                        a, b = b, a
                    parity ^= 1
                    if not include_masked and (fl & MGR_MASK):
                        continue
                    mat = surface_name(surf) + ('_f%02x' % fl if fl else '') + (
                        '_2s' if sflags & 2 else '')
                    if mat != cur_mat:
                        f.write('usemtl %s\n' % mat)
                        cur_mat = mat
                        used.add((mat, surf))
                    f.write('f %d %d %d\n' % (a, b, c))
                    stats['faces'] += 1
            if art.barriers:
                f.write('o ci%04d_%s%s_barriers\n' % (inst.index, inst.article_name, dyn))
                cur_mat = None
                for p0, p1, surf, fl, _ in art.barriers:
                    mat = 'barrier_b%02x_f%02x' % (surf, fl)
                    if mat != cur_mat:
                        f.write('usemtl %s\n' % mat)
                        cur_mat = mat
                        used.add((mat, None))
                    quad = [(p0[0], p0[1], p0[2]), (p1[0], p0[1], p1[2]),
                            (p1[0], p1[1], p1[2]), (p0[0], p1[1], p0[2])]
                    for q in quad:
                        f.write('v %.4f %.4f %.4f\n' % inst.to_world(q))
                    f.write('f %d %d %d %d\n' % (vcount + 1, vcount + 2, vcount + 3, vcount + 4))
                    vcount += 4
                    stats['barriers'] += 1
        for obj in track.objects:
            stats['objects'] += 1
            rows = obj.rows or ((1, 0, 0), (0, 1, 0), (0, 0, 1))
            hx, hy, hz = obj.half
            if obj.cylinder:
                # GetClosestIntersectingCylObject (0xc0440): circle of radius +0xc at (x,z),
                # y from +0x04 to +0x04 + 2*(+0x14)
                f.write('o co%04d_cyl\nusemtl object_cylinder\n' % obj.index)
                used.add(('object_cylinder', None))
                n = 12
                for k in range(n):
                    a = 2 * math.pi * k / n
                    x = obj.pos[0] + obj.radius * math.cos(a)
                    z = obj.pos[2] + obj.radius * math.sin(a)
                    f.write('v %.4f %.4f %.4f\nv %.4f %.4f %.4f\n' % (
                        x, obj.pos[1], z, x, obj.pos[1] + 2 * hy, z))
                for k in range(n):
                    a0, a1 = vcount + 1 + 2 * k, vcount + 1 + 2 * ((k + 1) % n)
                    f.write('f %d %d %d %d\n' % (a0, a0 + 1, a1 + 1, a1))
                f.write('f %s\n' % ' '.join(str(vcount + 2 + 2 * k) for k in range(n)))
                f.write('f %s\n' % ' '.join(str(vcount + 1 + 2 * k) for k in reversed(range(n))))
                vcount += 2 * n
            else:
                # GetOBBObjectIntersection (0xbf8c0): box +-half extents (+0x10) in the frame of
                # CARP::Instance[+0x24] rotation with translation +0x00 (WCollisionObject::MakeMatrix)
                f.write('o co%04d_obb\nusemtl object_obb\n' % obj.index)
                used.add(('object_obb', None))
                corners = []
                for sx in (-1, 1):
                    for sy in (-1, 1):
                        for sz in (-1, 1):
                            corners.append(mat_rows_apply(rows, obj.pos, (sx * hx, sy * hy, sz * hz)))
                for c in corners:
                    f.write('v %.4f %.4f %.4f\n' % c)
                b = vcount
                for q in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4),
                          (1, 5, 7, 3)):
                    f.write('f %s\n' % ' '.join(str(b + 1 + k) for k in q))
                vcount += 8
    with open(mtl_path, 'w', newline='\n') as f:
        for mat, surf in sorted(used, key=lambda x: x[0]):
            if mat.startswith('barrier'):
                col = (1.0, 0.8, 0.0)
            elif mat.startswith('object'):
                col = (0.9, 0.2, 0.9)
            else:
                col = SURF_COLOURS.get(surf, (1.0, 0.0, 1.0))
            f.write('newmtl %s\nKd %.3f %.3f %.3f\n' % ((mat,) + col))
            if mat.startswith('surf') and '_f' in mat:
                f.write('d 0.5\n')   # face flag bits set: skipped by some queries
            f.write('\n')
    return stats


def track_path_of(track):
    return getattr(track, 'path', '?')


def summary(track):
    buf = track.buf
    print('CARP groups (%d):' % len(track.top))
    counts = {}
    for g in track.top:
        counts[tagname(g.tag)] = counts.get(tagname(g.tag), 0) + 1
    print('  ' + ', '.join('%s x%d' % kv for kv in sorted(counts.items())))
    for g in track.top:
        name = tagname(g.tag)
        if name in ('Arti', 'Base', 'Name'):
            continue
        kinds = {}
        for e in g.data():
            k = tagname(e.tag & 0xffff0000 if e.flags & 1 else e.tag)
            kinds.setdefault(k, [0, 0])
            kinds[k][0] += 1
            kinds[k][1] += e.size
        print('  group %-4s: %s' % (name, ', '.join('%s x%d (%d B)' % (k, v[0], v[1])
                                                      for k, v in sorted(kinds.items()))))
    print('top-level data: %s' % ', '.join(tagname(e.tag) for e in track.root.data()))
    print('grid: origin (%.1f, %.1f, %.1f), cell %.2f, %d rows (z) x %d cols (x), %d non-empty nodes'
          % (track.origin + (track.cell, track.rows, track.cols, len(track.nodes))))
    print('      covers x %.1f..%.1f, z %.1f..%.1f' % (
        track.origin[0], track.origin[0] + track.cols * track.cell,
        track.origin[2], track.origin[2] + track.rows * track.cell))
    kinds = [0, 0, 0, 0]
    for n in track.nodes.values():
        for k in range(4):
            kinds[k] += buf[n + 4 + k]
    print('      node list entries by kind: 0 instances %d, 1 triggers (Map/Trgr) %d, 2 objects %d,'
          ' 3 road segments (RNgp/rs) %d'
          % tuple(kinds))
    print('dynamic elements: %d (%d instances, %d objects)' % (
        len(track.dynamic), sum(1 for k, _ in track.dynamic if k == 0),
        sum(1 for k, _ in track.dynamic if k == 2)))
    arts = set(i.article_name for i in track.instances)
    nstrips = ntris = nbar = 0
    missing = 0
    alt = 0
    for i in track.instances:
        a = i.article()
        if a is None:
            missing += 1
            continue
        nstrips += a.nstrips
        ntris += sum(len(v) - 2 for _, v, _ in a.strips)
        nbar += a.nbarriers
    for n in arts:
        if track.article_ca_count(n) > 1:
            alt += 1
    flagc = {}
    for i in track.instances:
        flagc[i.flags] = flagc.get(i.flags, 0) + 1
    print('collision instances: %d using %d articles (%d without geometry, %d articles with '
          'alternative ca+n geometry); instance flags %s'
          % (len(track.instances), len(arts), missing, alt,
             ', '.join('0x%x x%d' % kv for kv in sorted(flagc.items()))))
    print('  world strips %d, triangles %d, barriers %d' % (nstrips, ntris, nbar))
    surf = {}
    fl = {}
    for i in track.instances:
        a = i.article()
        if a is None:
            continue
        for *_, s, f, _sf in a.triangles():
            surf[s] = surf.get(s, 0) + 1
            fl[f] = fl.get(f, 0) + 1
    print('  triangles by surface: %s' % ', '.join('%s %d' % (surface_name(k), v)
                                                   for k, v in sorted(surf.items())))
    print('  triangles by face flags: %s' % ', '.join('0x%02x %d' % kv for kv in sorted(fl.items())))
    ncyl = sum(1 for o in track.objects if o.cylinder)
    print('collision objects: %d (%d OBB, %d cylinder)' % (len(track.objects),
                                                          len(track.objects) - ncyl, ncyl))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('crp')
    ap.add_argument('obj', nargs='?', default=None)
    ap.add_argument('--list', action='store_true', help='print a summary of chunks and counts')
    ap.add_argument('--query', action='append', default=[], metavar='X,Y,Z',
                    help='print the emulated GetWorldHeightAtPoint result at a point')
    ap.add_argument('--game-coords', action='store_true',
                    help="write the game's own left-handed coordinates (an importer shows them mirrored)")
    ap.add_argument('--skip-masked', action='store_true',
                    help='leave out faces the collision manager ignores (flag & 0xf0)')
    args = ap.parse_args(argv)
    track = Track(args.crp)
    track.path = args.crp
    if args.list:
        summary(track)
    for q in args.query:
        p = tuple(float(x) for x in q.split(','))
        r = track.height_at(p)
        if r is None:
            print('query %s: no face below' % (p,))
        else:
            h, inst, tri = r
            print('query %s: height %.3f (%.3f below) instance ci%04d %s' % (
                p, h, p[1] - h, inst.index, inst.article_name))
    if args.obj and args.obj != '-':
        st = write_obj(track, args.obj, include_masked=not args.skip_masked, game_coords=args.game_coords)
        lo, hi = st['bbox']
        print('wrote %s: %d instances, %d faces, %d vertices, %d barriers, %d objects'
              % (args.obj, st['instances'], st['faces'], st['vertices'], st['barriers'],
                 st['objects']))
        print('bbox min (%.1f, %.1f, %.1f) max (%.1f, %.1f, %.1f)' % (tuple(lo) + tuple(hi)))
    elif not args.list and not args.query:
        ap.error('give <out.obj>, --list or --query')


if __name__ == '__main__':
    main()
