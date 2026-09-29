"""Exports a character (a skin, hashcode type 0x05) from a level bundle as a binary glTF (.glb) for Blender.

What goes in:
- the skeleton: one joint per bone the skin uses, in its hierarchy, at the skin's bind pose (every bone's rest
  rotation is the identity; its position is minus the skin's inverse bind translation);
- the skin's rigged meshes, skinned (two bones per vertex, JOINTS_0/WEIGHTS_0), with normals, UVs and vertex
  colours; a sleeve hashcode (bit 0x100000) becomes the level's first sleeve mesh, as AnimObjectSetSleeveType's
  default would;
- facial morph targets (22 on heads) as glTF morph targets, named in extras.targetNames (Blender shape keys);
- the rigid parts, each a mesh node under the joint it follows (their vertices are bone-local);
- the attachment points (datums) as empty nodes under their bones, named datum_<id>;
- textures (embedded PNGs) and materials from the strips' texture and render-state flags;
- animations: every bone sequence of the skin's skeleton (or those asked for), sampled at every frame at 60 fps
  (the game interpolates linearly between whole frames, so LINEAR sampling reproduces it), and, for a skin with
  morph targets, the facial sequences as morph-weight animations.

The maths is the game's (docs/anim/spec-pose.md): a bone's world transform is its parent's times its local
(translation, rotation), which is glTF's node TRS; the skinning matrix is the bone's world matrix times a
translation by the skin's inverse bind translation, which is the joint's inverse bind matrix. The game's axes are
glTF's already (right-handed, y up, characters facing +z), and glTF's UV origin is the top left like Direct3D's,
so nothing is converted; Blender's importer turns y up into z up itself.

usage:
  python export_gltf.py --list [--bundle 07000001]           skins in a bundle, their meshes and sequence counts
  python export_gltf.py 05000007 [--bundle 07000001] [-o out.glb] [--seq 04000430,...] [--max-anims N]
                        [--no-anims] [--no-facial] [--in-place]
"""
import argparse
import glob
import json
import math
import os
import struct
import sys
import zlib

import bundle
import decode_seq
import parse_gfx
import parse_skeleton
import parse_skin
import parse_tex
from nfpaths import LVL

FPS = 60.0

# Facial morph channels (spec-facial.md 2.3). 10-14 are confirmed from the code; the rest are guesses from where
# each target moves the face.
MORPH_NAMES = ['mouth_0', 'mouth_1', 'mouth_2', 'mouth_3', 'mouth_4', 'mouth_5', 'mouth_6', 'mouth_7', 'mouth_8',
               'mouth_9', 'blink', 'eyes_up', 'eyes_down', 'eyes_side_a', 'eyes_side_b', 'brows_raise',
               'cheeks_16', 'cheeks_17', 'brow_single', 'cheeks_19', 'cheeks_20', 'jaw_open']

FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
REPEAT, CLAMP = 10497, 33071


# ---------------------------------------------------------------------------------------------------------------
# A minimal glTF 2.0 writer
# ---------------------------------------------------------------------------------------------------------------

class Gltf:
    def __init__(self):
        self.bin = bytearray()
        self.j = dict(asset=dict(version='2.0', generator='nightfire-cxbx tools/anim/export_gltf.py'),
                      scene=0, scenes=[dict(nodes=[])], nodes=[], meshes=[], materials=[], textures=[], images=[],
                      samplers=[], skins=[], animations=[], accessors=[], bufferViews=[], buffers=[])

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = dict(buffer=0, byteOffset=len(self.bin), byteLength=len(data))
        if target:
            v['target'] = target
        self.bin += data
        self.j['bufferViews'].append(v)
        return len(self.j['bufferViews']) - 1

    def accessor(self, fmt, comp, typ, rows, target=None, minmax=False, normalized=False):
        """rows: a list of tuples (or of scalars for SCALAR)."""
        flat = [x for r in rows for x in r] if typ != 'SCALAR' else list(rows)
        data = struct.pack('<%d%s' % (len(flat), fmt), *flat)
        a = dict(bufferView=self.view(data, target), componentType=comp, count=len(rows), type=typ)
        if normalized:
            a['normalized'] = True
        if minmax and rows:
            if typ == 'SCALAR':
                a['min'], a['max'] = [min(rows)], [max(rows)]
            else:
                n = len(rows[0])
                a['min'] = [min(r[k] for r in rows) for k in range(n)]
                a['max'] = [max(r[k] for r in rows) for k in range(n)]
        self.j['accessors'].append(a)
        return len(self.j['accessors']) - 1

    def node(self, **kw):
        self.j['nodes'].append({k: v for k, v in kw.items() if v is not None})
        return len(self.j['nodes']) - 1

    def add(self, key, item):
        self.j[key].append(item)
        return len(self.j[key]) - 1

    def write_gltf(self, path):
        """Text glTF: path (.gltf) plus its buffer beside it as <name>.bin."""
        while len(self.bin) % 4:
            self.bin.append(0)
        bin_name = os.path.splitext(os.path.basename(path))[0] + '.bin'
        self.j['buffers'] = [dict(byteLength=len(self.bin), uri=bin_name)]
        j = {k: v for k, v in self.j.items() if v != []}
        with open(os.path.join(os.path.dirname(path) or '.', bin_name), 'wb') as f:
            f.write(bytes(self.bin))
        with open(path, 'w') as f:
            json.dump(j, f, separators=(',', ':'))

    def write_glb(self, path):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.j['buffers'] = [dict(byteLength=len(self.bin))]
        j = {k: v for k, v in self.j.items() if v != []}
        js = json.dumps(j, separators=(',', ':')).encode()
        js += b' ' * ((-len(js)) % 4)
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, total))
            f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
            f.write(struct.pack('<II', len(self.bin), 0x004E4942) + bytes(self.bin))


def png_bytes(w, h, rgba):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def mat4_translation(t):
    """column-major 4x4 translation"""
    return (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1)


def qmul(a, b):
    """Hamilton product a (x) b, quaternions x, y, z, w"""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + bw * ax + ay * bz - az * by, aw * by + bw * ay + az * bx - ax * bz,
            aw * bz + bw * az + ax * by - ay * bx, aw * bw - ax * bx - ay * by - az * bz)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qrot(q, v):
    """v rotated by q"""
    x, y, z, _ = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qconj(q))
    return (x, y, z)


def qmat3(q):
    """column-major 3x3 of q, as the 9 numbers of the columns"""
    x, y, z, w = q
    return (1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y),
            2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x),
            2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y))


def shortest_arc(u, v):
    """the rotation taking unit vector u to unit vector v (half-turn about z when they are opposite)"""
    d = sum(u[k] * v[k] for k in range(3))
    if d < -0.999999:
        return (0.0, 0.0, 1.0, 0.0)
    c = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    q = (c[0], c[1], c[2], 1.0 + d)
    n = math.sqrt(sum(x * x for x in q))
    return tuple(x / n for x in q)


_HIERARCHY = None


def canonical_hierarchy():
    """{skeleton: [parent bone, -1 for a root, None where no skin uses the bone]}, the same for every skin of a
    skeleton (checked), gathered from every skin in the game and cached in build/anim/skeleton_hierarchy.json."""
    global _HIERARCHY
    if _HIERARCHY is not None:
        return _HIERARCHY
    cache = os.path.join(LVL, '..', 'skeleton_hierarchy.json')
    if os.path.exists(cache):
        _HIERARCHY = {int(k): v for k, v in json.load(open(cache)).items()}
        return _HIERARCHY
    table = parse_skeleton.load_table()
    out = {}
    for fn in sorted(glob.glob(os.path.join(LVL, '07*.bin'))):
        d = open(fn, 'rb').read()
        for typ, name, off, ln in bundle.walk(d):
            if not name.lower().startswith('05'):
                continue
            sk = parse_skin.parse(d[off:off + ln], table)
            par = out.setdefault(sk['skel'], [None] * sk['numBones'])
            for b, v in enumerate(sk['bones']):
                if not v & 0x80:
                    continue
                pb = -1 if v & 0x7f == 0x7f else v & 0x7f
                if par[b] is not None and par[b] != pb:
                    raise ValueError('skeleton %d bone %d: skins disagree on its parent' % (sk['skel'], b))
                par[b] = pb
    with open(cache, 'w') as f:
        json.dump(out, f)
    _HIERARCHY = out
    return out


def bone_frames(skel_num, skel_table):
    """A rest orientation for every bone of a skeleton, pointing its local +y at its child - the axis Blender's
    importer draws a bone along. The game's bones all rest unrotated, which Blender draws as every bone pointing
    straight up. The frame depends only on the skeleton (its default offsets and the hierarchy all its skins share),
    so a character and its skeleton's animation library agree. The child aimed at is the next bone when that is a
    child (the chains are numbered in order), else the average of the children; a bone with no children, or no
    usable direction, keeps its parent's frame."""
    parents = canonical_hierarchy().get(skel_num)
    offs = skel_table[skel_num]['offsets']
    nb = len(offs)
    ident = (0.0, 0.0, 0.0, 1.0)
    if parents is None:
        return [ident] * nb
    kids = [[c for c in range(nb) if parents[c] == b] for b in range(nb)]
    below = [0] * nb   # how many bones hang below each one
    for b in reversed(range(nb)):
        below[b] = sum(1 + below[c] for c in kids[b])
    length = [math.sqrt(sum(x * x for x in o)) for o in offs]
    frames = [ident] * nb
    for b in range(nb):   # parents precede children
        if parents[b] is None:
            continue
        inherit = frames[parents[b]] if parents[b] >= 0 else ident
        # The next bone is not the limb's continuation when it is a short helper beside another child that is at
        # least four times as far and carries more of the chain: the human elbow (bone 32: a 5 cm helper, 33,
        # beside the wrist, 35), and a few bones whose next child sits on top of them.
        better = [c for c in kids[b] if c != b + 1 and b + 1 in kids[b] and length[c] >= 4 * length[b + 1] and
                  below[c] > below[b + 1]]
        if better:
            d = offs[max(better, key=lambda c: below[c])]
        elif b + 1 in kids[b]:
            d = offs[b + 1]
        elif kids[b]:
            d = [sum(offs[c][k] for c in kids[b]) / len(kids[b]) for k in range(3)]
        else:
            d = None
        n = math.sqrt(sum(x * x for x in d)) if d is not None else 0.0
        frames[b] = shortest_arc((0.0, 1.0, 0.0), tuple(x / n for x in d)) if n > 1e-4 else inherit
    return frames


def normalise(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v) if n > 1e-12 else (0.0, 1.0, 0.0)


# ---------------------------------------------------------------------------------------------------------------
# The level's contents
# ---------------------------------------------------------------------------------------------------------------

class Level:
    """One level bundle: its skins, sequences, skeletons, and every named entity's mesh with the texture pack that
    owns it (textures resolved level-wide, see parse_tex.Level)."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)[:8]
        data = open(path, 'rb').read()
        self.skins, self.seqs = {}, []
        for typ, name, off, ln in bundle.walk(data):
            n = name.lower()
            if n.startswith('05'):
                self.skins[int(n[:8], 16)] = data[off:off + ln]
            elif n.startswith('04'):
                self.seqs.append((int(n[:8], 16), data[off:off + ln]))
        self.skel_table = parse_skeleton.load_table()
        self.seq_skels = decode_seq.load_skeletons(path)
        self.tex = parse_tex.Level(path)
        self.entities = {}
        for pk in self.tex.packs:
            for e in parse_gfx.Pack(pk.d).entities:
                # entities without geometry are kept too: some rigid parts are placeholders (Pulse_1..20,
                # Bolt_datum1_object) that mark a place on a bone for effects
                if e.hash != 0xffffffff and e.mesh is not None:
                    if e.hash not in self.entities or (self.entities[e.hash][1].mesh.empty and not e.mesh.empty):
                        self.entities[e.hash] = (pk, e)
        self.sleeve = next((h for h in parse_gfx.SLEEVE_TABLE if h in self.entities), None)

    def skin(self, h):
        return parse_skin.parse(self.skins[h], self.skel_table)

    def mesh_for(self, h):
        if h not in self.entities and h & 0x00100000 and self.sleeve is not None:
            h = self.sleeve
        return self.entities.get(h)

    def sequences(self, skel_num, morph):
        out = []
        for h, b in self.seqs:
            try:
                q = decode_seq.Sequence(b, '%08x' % h)
            except Exception:
                continue
            if q.isMorph != morph:
                continue
            if not morph and q.skelNum != skel_num:
                continue
            out.append((h, q))
        return out


# ---------------------------------------------------------------------------------------------------------------
# The exporter
# ---------------------------------------------------------------------------------------------------------------

class Exporter:
    def __init__(self, level, skin_hash, log=print, skin=None, image_writer=None):
        """skin: a parse_skin-style dict to use instead of the level's (the animation libraries' skeletons);
        image_writer(texture, w, h, rgba) -> uri: write textures as external files instead of embedding them."""
        self.lv, self.h, self.log = level, skin_hash, log
        self.s = skin if skin is not None else level.skin(skin_hash)
        self.image_writer = image_writer
        self.vertex_ids = False   # set for checks: adds a _VID attribute (the game's vertex index) to every mesh
        # shared_rest: rest the joints at the skeleton's defaults instead of the skin's bind, so that animations
        # from another skin of the skeleton fit (dump_characters.py). Off, the export is exact for its own
        # animations; on, a vertex blended between two bones can be off by a few millimetres (build_skeleton).
        self.shared_rest = False
        self.g = Gltf()
        self.materials = {}
        self.images = {}
        self.morph_nodes = []

    # --- skeleton
    def build_skeleton(self, root):
        s = self.s
        nb = s['numBones']
        if s['bind']:
            self.bindpos = [tuple(-c for c in t) for t, q in s['bind']]
        else:
            self.bindpos = [tuple(p) for p in parse_skin.world_bind(s, self.lv.skel_table[s['skel']])]
        # Where the joints rest and bind. By default at the skin's own bind positions, which is exact: the game
        # skins with them. With shared_rest, at the skeleton's default offsets times the skin's scale - where the
        # game animates from (the translation of every bone without animated translation, AnimFrameCopy) - so that
        # an animation made on another skin of the same skeleton lands on this one. The skin's bind positions differ
        # from those by up to about a centimetre; build_mesh moves the vertices to match, which is exact for a
        # vertex on one bone and leaves a vertex blended between two bones whose offsets and rotations both differ
        # a few millimetres out (the worst case seen, 3 mm, is a 50/50 foot vertex).
        if self.shared_rest:
            self.restpos = [tuple(p) for p in parse_skin.world_bind(s, self.lv.skel_table[s['skel']])]
        else:
            self.restpos = list(self.bindpos)
        used = [b for b in range(nb) if s['bones'][b] & 0x80]
        # Each joint's frame is the game bone's rotated by frames[b] (bone_frames): the node transforms, the
        # inverse bind matrices and every animation key are written in those frames, so the skinning is unchanged
        # while Blender draws the bones along the limbs.
        self.frames = bone_frames(s['skel'], self.lv.skel_table)
        self.joint_of = {}
        self.joint_node = {}
        for b in used:   # parents precede children (checked by parse_skin)
            p = s['bones'][b] & 0x7f
            while p != 0x7f and not s['bones'][p] & 0x80:
                p = s['bones'][p] & 0x7f
            fp = self.frames[p] if p != 0x7f else (0.0, 0.0, 0.0, 1.0)
            t = self.restpos[b] if p == 0x7f else tuple(self.restpos[b][k] - self.restpos[p][k] for k in range(3))
            n = self.g.node(name='bone_%02d' % b, translation=list(qrot(qconj(fp), t)),
                            rotation=list(qmul(qconj(fp), self.frames[b])))
            self.joint_node[b] = n
            self.joint_of[b] = len(self.joint_of)
            parent = root if p == 0x7f else self.joint_node[p]
            self.g.j['nodes'][parent].setdefault('children', []).append(n)
        self.used = used
        ibms = []
        for b in used:   # inverse of (translate to the rest position, then rotate by the frame)
            inv = qconj(self.frames[b])
            r = qmat3(inv)
            t = qrot(inv, [-c for c in self.restpos[b]])
            ibms.append(r[0:3] + (0.0,) + r[3:6] + (0.0,) + r[6:9] + (0.0,) + tuple(t) + (1.0,))
        ibm = self.g.accessor('f', FLOAT, 'MAT4', ibms)
        roots = [b for b in used if s['bones'][b] & 0x7f == 0x7f]
        self.skin_index = self.g.add('skins', dict(name='skin_%08x' % self.h, joints=[self.joint_node[b] for b in used],
                                                    inverseBindMatrices=ibm, skeleton=self.joint_node[roots[0]]))

    # --- materials
    def image_for(self, tex):
        key = id(tex.source) if tex.source is not None else None
        if key is None:
            return None, None
        if key not in self.images:
            w, h, px = tex.decode(0, 0)
            alphas = set(px[3::4])
            kind = 'opaque' if alphas <= {255} else ('mask' if alphas <= {0, 255} else 'blend')
            if self.image_writer is not None:
                img = self.g.add('images', dict(name=tex.name, uri=self.image_writer(tex, w, h, px)))
            else:
                iv = self.g.view(png_bytes(w, h, px))
                img = self.g.add('images', dict(name=tex.name, mimeType='image/png', bufferView=iv))
            self.images[key] = (img, kind)
        return self.images[key]

    def material_for(self, pk, st):
        tex = None
        if st['tex0'] is not None and st['tex0'] != 0xffff and st['tex0'] < len(pk.textures):
            tex = pk.textures[st['tex0']]
        wrap_u = REPEAT if st['wrapU'] in (None, True) else CLAMP
        wrap_v = REPEAT if st['wrapV'] in (None, True) else CLAMP
        key = (id(tex.source) if tex is not None and tex.source is not None else None, st['blend'],
               bool(st['doubleSided']), st['alphaRef'], wrap_u, wrap_v)
        if key in self.materials:
            return self.materials[key]
        m = dict(name=tex.name if tex is not None else 'untextured',
                 pbrMetallicRoughness=dict(metallicFactor=0.0, roughnessFactor=1.0),
                 doubleSided=bool(st['doubleSided']))
        extras = dict(blend=st['blend'], alphaRef=st['alphaRef'])
        if tex is not None:
            img, kind = self.image_for(tex)
            if img is not None:
                smp = self.g.add('samplers', dict(wrapS=wrap_u, wrapT=wrap_v))
                ti = self.g.add('textures', dict(source=img, sampler=smp))
                m['pbrMetallicRoughness']['baseColorTexture'] = dict(index=ti)
                # The game blends every strip with its texture's alpha (additive and subtractive strips aside) and
                # alpha-tests at 1/255, or 180/255 when the strip says so. Textures whose alpha is all opaque are
                # opaque; on/off alpha is a mask; anything else blends, unless the strip tests at 180.
                if st['blend'] in ('additive', 'subtractive'):
                    m['alphaMode'] = 'BLEND'
                elif kind == 'mask':
                    m['alphaMode'], m['alphaCutoff'] = 'MASK', 0.5
                elif kind == 'blend':
                    if st['alphaRef'] == 0xb4:
                        m['alphaMode'], m['alphaCutoff'] = 'MASK', 180 / 255.0
                    else:
                        m['alphaMode'] = 'BLEND'
                extras['texture'] = '%s#%d' % (tex.pack, tex.index)
        m['extras'] = extras
        self.materials[key] = self.g.add('materials', m)
        return self.materials[key]

    # --- meshes
    def build_mesh(self, pk, ent, skinned, name):
        m = ent.mesh
        g = self.g
        verts = m.vertices
        prim_tuples = [(q.tex0, q.tex1, q.count, q.flags, q.change, q.palette) for q in m.prims]
        states = parse_tex.effective_states(prim_tuples)
        groups = {}
        for a, b, c, pi in m.triangles():
            mat = self.material_for(pk, states[pi])
            # The game stores its normals pointing into the mesh, and its strip order winds the same way
            # (docs/anim/spec-mesh.md). glTF wants counter-clockwise faces and normals facing out, so both flip.
            groups.setdefault(mat, []).extend((a, c, b))
        positions = [v['pos'] for v in verts]
        if skinned:
            # The joints rest (and bind) at the skeleton's defaults, while the game binds the mesh at the skin's own
            # bind positions. What the game draws at the rest pose is each vertex moved by its bones' (weighted)
            # difference between the two, so that is the rest mesh: a single-bone vertex then skins exactly as in
            # the game, and a two-bone one to within a second-order term (both bones' offsets differing and their
            # rotations differing). Blender binds a mesh at the armature's rest, so this is also what it can hold.
            sw0, _ = m.skin_weights()
            moved = []
            for i, p in enumerate(positions):
                if sw0[i] is None:
                    moved.append(p)
                    continue
                (ba, wa), (bb, _) = sw0[i]
                wa = min(max(wa, 0.0), 1.0)
                da = [self.restpos[ba][k] - self.bindpos[ba][k] for k in range(3)]
                db = [self.restpos[bb][k] - self.bindpos[bb][k] for k in range(3)]
                moved.append(tuple(p[k] + wa * da[k] + (1 - wa) * db[k] for k in range(3)))
            positions = moved
        attrs = dict(
            POSITION=g.accessor('f', FLOAT, 'VEC3', positions, ARRAY_BUFFER, minmax=True),
            NORMAL=g.accessor('f', FLOAT, 'VEC3', [tuple(-c for c in normalise(v['normal'])) for v in verts],
                              ARRAY_BUFFER),
            TEXCOORD_0=g.accessor('f', FLOAT, 'VEC2', [v['uv'] for v in verts], ARRAY_BUFFER))
        colours = [v['colour'] for v in verts]
        if any(c != (255, 255, 255, 255) for c in colours):
            attrs['COLOR_0'] = g.accessor('B', UBYTE, 'VEC4', colours, ARRAY_BUFFER, normalized=True)
        if self.vertex_ids:
            attrs['_VID'] = g.accessor('f', FLOAT, 'SCALAR', [float(i) for i in range(m.vtxCnt)], ARRAY_BUFFER)
        if skinned:
            sw, problems = m.skin_weights()
            root_joint = 0
            joints, weights = [], []
            bad = 0
            for i in range(m.vtxCnt):
                if sw[i] is None:
                    bad += 1
                    joints.append((root_joint, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
                    continue
                (ba, wa), (bb, wb) = sw[i]
                wa = min(max(wa, 0.0), 1.0)
                wb = 1.0 - wa
                ja, jb = self.joint_of[ba], self.joint_of[bb]
                if ja == jb or wb == 0.0:
                    joints.append((ja, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
                else:
                    joints.append((ja, jb, 0, 0))
                    weights.append((wa, wb, 0.0, 0.0))
            if bad:
                self.log('  %s: %d vertices without a decodable bone pair (none drawn by any strip?)' % (name, bad))
            big = len(self.joint_of) > 255
            attrs['JOINTS_0'] = g.accessor('H' if big else 'B', USHORT if big else UBYTE, 'VEC4', joints,
                                           ARRAY_BUFFER)
            attrs['WEIGHTS_0'] = g.accessor('f', FLOAT, 'VEC4', weights, ARRAY_BUFFER)
        targets = []
        if m.morphCnt and m.morphVtxCnt:
            zero = (0.0, 0.0, 0.0)
            for deltas in m.morph_targets:
                rows = [tuple(c / 32767.0 for c in d) for d in deltas] + [zero] * (m.vtxCnt - m.morphVtxCnt)
                targets.append(dict(POSITION=g.accessor('f', FLOAT, 'VEC3', rows, ARRAY_BUFFER, minmax=True)))
        prims = []
        for mat, idx in groups.items():
            big = m.vtxCnt > 65535
            p = dict(attributes=attrs, material=mat,
                     indices=g.accessor('I' if big else 'H', UINT if big else USHORT, 'SCALAR', idx,
                                        ELEMENT_ARRAY_BUFFER))
            if targets:
                p['targets'] = targets
            prims.append(p)
        mesh = dict(name=name, primitives=prims)
        if targets:
            names = MORPH_NAMES if len(targets) == len(MORPH_NAMES) else ['morph_%02d' % i for i in
                                                                            range(len(targets))]
            mesh['extras'] = dict(targetNames=names)
            mesh['weights'] = [0.0] * len(targets)
        return g.add('meshes', mesh), bool(targets)

    # --- animations
    def bone_animation(self, h, q, in_place):
        skel = self.lv.seq_skels.get(q.skelNum)
        if skel is None or q.numTracks != skel.num_tracks():
            self.log('  skipping %08x: %d tracks, skeleton %d wants %s' % (
                h, q.numTracks, q.skelNum, skel.num_tracks() if skel else '?'))
            return False
        frames = q.all_frames(skel, self.s['scale'])
        n = len(frames)
        g = self.g
        times = g.accessor('f', FLOAT, 'SCALAR', [i / FPS for i in range(n)], minmax=True)
        time0 = None
        samplers, channels = [], []
        for b in self.used:
            node = self.joint_node[b]
            fb = self.frames[b]
            p = self.s['bones'][b] & 0x7f
            fp_inv = qconj(self.frames[p]) if p != 0x7f else (0.0, 0.0, 0.0, 1.0)
            rots, prev = [], None
            for trans, quats in frames:
                x, y, z, w = quats[b]
                l = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
                # the game's local rotation, re-expressed between the parent's and this joint's frames
                r = qmul(qmul(fp_inv, (x / l, y / l, z / l, w / l)), fb)
                if prev is not None and sum(r[k] * prev[k] for k in range(4)) < 0:
                    r = tuple(-c for c in r)       # keep neighbouring keys in one hemisphere for slerp
                rots.append(r)
                prev = r
            trs = [tuple(tr[b]) for tr, qs in frames]
            if b == 0 and in_place:
                trs = [(0.0, t[1], 0.0) for t in trs]
            trs = [qrot(fp_inv, t) for t in trs]
            out_r = g.accessor('f', FLOAT, 'VEC4', rots)
            samplers.append(dict(input=times, output=out_r, interpolation='LINEAR'))
            channels.append(dict(sampler=len(samplers) - 1, target=dict(node=node, path='rotation')))
            if all(t == trs[0] for t in trs):
                if time0 is None:
                    time0 = g.accessor('f', FLOAT, 'SCALAR', [0.0], minmax=True)
                out_t = g.accessor('f', FLOAT, 'VEC3', trs[:1])
                samplers.append(dict(input=time0, output=out_t, interpolation='LINEAR'))
            else:
                out_t = g.accessor('f', FLOAT, 'VEC3', trs)
                samplers.append(dict(input=times, output=out_t, interpolation='LINEAR'))
            channels.append(dict(sampler=len(samplers) - 1, target=dict(node=node, path='translation')))
        g.add('animations', dict(name='seq_%08x' % h, samplers=samplers, channels=channels,
                                 extras=dict(frames=n, fps=FPS, skeleton=q.skelNum)))
        return True

    def facial_animation(self, h, q):
        n_targets = self.s['numMorphs']
        if q.numElems != n_targets:
            return
        frames = q.all_frames()
        g = self.g
        times = g.accessor('f', FLOAT, 'SCALAR', [i / FPS for i in range(len(frames))], minmax=True)
        out = g.accessor('f', FLOAT, 'SCALAR', [w for f in frames for w in f[:n_targets]])
        channels = [dict(sampler=0, target=dict(node=n, path='weights')) for n in self.morph_nodes]
        g.add('animations', dict(name='face_%08x' % h, samplers=[dict(input=times, output=out,
                                                                     interpolation='LINEAR')],
                                 channels=channels, extras=dict(frames=len(frames), fps=FPS)))

    # --- everything
    def run(self, seq_filter=None, max_anims=None, facial=True, anims=True, in_place=False):
        s, g = self.s, self.g
        root = g.node(name='skin_%08x' % self.h, extras=dict(level=self.lv.name, skeleton=s['skel'],
                                                             skinScale=list(s['scale'])))
        g.j['scenes'][0]['nodes'].append(root)
        self.build_skeleton(root)
        for i, rh in enumerate(s['riggedHash']):
            found = self.lv.mesh_for(rh)
            if found is None:
                self.log('  rigged mesh %08x is not in this level' % rh)
                continue
            pk, ent = found
            if ent.mesh.empty:
                self.log('  rigged mesh %08x (%s) has no geometry' % (rh, ent.name))
                continue
            if not ent.mesh.skinned:
                self.log('  rigged mesh %08x is not skinned; skipped' % rh)
                continue
            mi, morph = self.build_mesh(pk, ent, True, '%s_%08x' % (ent.name or 'body', ent.hash))
            n = g.node(name='%s_%08x' % (ent.name or 'body', ent.hash), mesh=mi, skin=self.skin_index)
            g.j['nodes'][root].setdefault('children', []).append(n)
            if morph:
                self.morph_nodes.append(n)
        for i, dh in enumerate(s['discreteHash']):
            found = self.lv.mesh_for(dh)
            if found is None:
                self.log('  rigid part %08x is not in this level' % dh)
                continue
            pk, ent = found
            bone = s['discreteBone'][i]
            if bone not in self.joint_node:
                self.log('  rigid part %08x follows bone %d, which the skin does not use' % (dh, bone))
                continue
            if ent.mesh.empty:
                n = g.node(name='%s_%08x' % (ent.name or 'part', ent.hash),   # a placeholder: an empty
                           rotation=list(qconj(self.frames[bone])))
            else:
                mi, _ = self.build_mesh(pk, ent, False, '%s_%08x' % (ent.name or 'part', ent.hash))
                n = g.node(name='%s_%08x' % (ent.name or 'part', ent.hash), mesh=mi,
                           rotation=list(qconj(self.frames[bone])))
            g.j['nodes'][self.joint_node[bone]].setdefault('children', []).append(n)
        for d in s['datums']:
            if d['bone'] in self.joint_node:
                inv = qconj(self.frames[d['bone']])
                n = g.node(name='datum_%d' % d['id'], translation=list(qrot(inv, d['t'])),
                           rotation=list(qmul(inv, tuple(d['q']))))
                g.j['nodes'][self.joint_node[d['bone']]].setdefault('children', []).append(n)
        count = 0
        if anims:
            for h, q in self.lv.sequences(s['skel'], morph=False):
                if seq_filter and h not in seq_filter:
                    continue
                if max_anims is not None and count >= max_anims:
                    break
                if self.bone_animation(h, q, in_place):
                    count += 1
        faces = 0
        if anims and facial and self.morph_nodes:
            for h, q in self.lv.sequences(s['skel'], morph=True):
                if seq_filter and h not in seq_filter:
                    continue
                self.facial_animation(h, q)
                faces += 1
        return count, faces


def find_bundle(skin_hash):
    for fn in sorted(glob.glob(os.path.join(LVL, '07*.bin'))):
        for typ, name, off, ln in bundle.walk(open(fn, 'rb').read()):
            if name.lower().startswith('%08x' % skin_hash):
                return fn
    return None


def list_skins(path):
    lv = Level(path)
    for h in sorted(lv.skins):
        s = lv.skin(h)
        meshes = []
        for rh in s['riggedHash']:
            f = lv.mesh_for(rh)
            meshes.append(('%08x %s%s' % (f[1].hash, f[1].name, ' (no geometry)' if f[1].mesh.empty else ''))
                          if f else '%08x missing' % rh)
        nseq = len(lv.sequences(s['skel'], False))
        print('%08x skeleton %2d, %2d bones used, morphs %2d, %d rigid parts, %d datums, %3d sequences: %s' % (
            h, s['skel'], sum(1 for b in s['bones'] if b & 0x80), s['numMorphs'], s['numDiscrete'],
            s['numDatums'], nseq, ', '.join(meshes) or 'no rigged mesh'))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('skin', nargs='?', help='skin hashcode, e.g. 05000007')
    ap.add_argument('--bundle', help='level bundle, e.g. 07000001 (default: the first holding the skin)')
    ap.add_argument('-o', '--out', help='output .glb (default: <skin>.glb)')
    ap.add_argument('--list', action='store_true', help='list the skins in the bundle')
    ap.add_argument('--seq', help='comma-separated sequence hashcodes to include (bone or facial)')
    ap.add_argument('--max-anims', type=int, help='at most this many bone sequences')
    ap.add_argument('--no-anims', action='store_true')
    ap.add_argument('--no-facial', action='store_true')
    ap.add_argument('--in-place', action='store_true', help='zero the root bone\'s horizontal motion')
    ap.add_argument('--vertex-ids', action='store_true', help='add a _VID attribute per vertex (for check_gltf.py)')
    a = ap.parse_args(argv)
    path = None
    if a.bundle:
        path = os.path.join(LVL, a.bundle.lower().replace('.bin', '') + '.bin')
    if a.list:
        list_skins(path or os.path.join(LVL, '07000001.bin'))
        return 0
    if not a.skin:
        ap.error('give a skin hashcode, or --list')
    h = int(a.skin, 16)
    path = path or find_bundle(h)
    if path is None:
        print('skin %08x is in no level bundle' % h)
        return 1
    lv = Level(path)
    if h not in lv.skins:
        print('skin %08x is not in %s' % (h, path))
        return 1
    seqs = {int(x, 16) for x in a.seq.split(',')} if a.seq else None
    ex = Exporter(lv, h)
    ex.vertex_ids = a.vertex_ids
    n, f = ex.run(seq_filter=seqs, max_anims=a.max_anims, facial=not a.no_facial, anims=not a.no_anims,
                  in_place=a.in_place)
    out = a.out or '%08x.glb' % h
    ex.g.write_glb(out)
    print('%s: skin %08x from %s, %d joints, %d meshes, %d materials, %d bone and %d facial animations, %.1f MB' % (
        out, h, lv.name, len(ex.used), len(ex.g.j['meshes']), len(ex.g.j['materials']), n, f,
        os.path.getsize(out) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
