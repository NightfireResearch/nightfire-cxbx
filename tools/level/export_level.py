"""Exports an action-engine level (a 07xxxxxx level bundle) as a binary glTF scene for Blender.

  python export_level.py 07000001 [-o level.glb] [--collision] [--portals] [--no-characters] [--no-textures]
  python export_level.py --list                      the level bundles, their map file and placement counts

What goes in (docs/level/, tools/level/types_*.json):
- the static world: every drawable static cel - rooms (0xc047 / 0xc023, already in world space) and objects
  (0x8019 ...) placed with T(pos) Rz Ry Rx(rot), as View_DrawCelGlist draws them - one node each, sharing a mesh
  per entity (group "rooms" and "static");
- every dynamic object the game creates (the map file's placements with (type & 0xa000) == 0), one node each in a
  group per type, named after its type, placement index and the switch channels it writes and reads, with every
  decoded key as its own custom property: its placement glist as a mesh where the game draws it, characters
  (drones, trees) as their skin's meshes in the bind pose with the held weapon, trigger and AI volumes as wire
  boxes, markers as empties;
- lights (the map's radiators and Light placements) as KHR_lights_punctual point lights;
- paths (door and camera paths, 0x19) and AI paths (0x05) as lines;
- optionally the collision (--collision) and the portals (--portals).

Axes are the game's, which are glTF's (right-handed, y up); Blender's importer turns y up into z up. Faces are
flipped as for characters (the game's normals and winding face inward; export_gltf.py).
"""
import argparse
import glob
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'anim'))

import export_gltf as eg     # noqa: E402
import parse_gfx             # noqa: E402
import parse_skeleton        # noqa: E402
import parse_skin            # noqa: E402
import parse_tex             # noqa: E402
import parse_level as pl     # noqa: E402
from nfpaths import LVL      # noqa: E402

FLOAT, UBYTE, USHORT, UINT = eg.FLOAT, eg.UBYTE, eg.USHORT, eg.UINT

ARROW_OUT, ARROW_IN, ARROW_BOTH = '→', '←', '↔'


def load_types():
    types = {}
    for group in ('actors', 'logic', 'dressing'):
        d = json.load(open(os.path.join(HERE, 'types_%s.json' % group)))
        for tid, t in d.items():
            if not tid.startswith('_') and isinstance(t, dict):
                t = dict(t, group=group)
                types[int(tid)] = t
    return types


TYPES = load_types()


# ---------------------------------------------------------------------------------------------------------------
# Transforms: 4x4 row-major matrices for column vectors (parse_level's convention) -> glTF column-major lists
# ---------------------------------------------------------------------------------------------------------------

def obj_rot(rot):
    """View_RotTransMatrix 0xd92e0, the orientation Control_CreateObjEx gives an object (engine rows = images of
    x, y, z; here as the columns of a column-vector matrix)."""
    cx, sx = math.cos(rot[0]), math.sin(rot[0])
    cy, sy = math.cos(rot[1]), math.sin(rot[1])
    cz, sz = math.cos(rot[2]), math.sin(rot[2])
    X = (cz * cy, sz, -cz * sy)
    Y = (sy * sx - sz * cy * cx, cz * cx, cy * sx + sz * sy * cx)
    Z = (sy * cx + sz * cy * sx, -cz * sx, cy * cx - sz * sy * sx)
    return [[X[0], Y[0], Z[0]], [X[1], Y[1], Z[1]], [X[2], Y[2], Z[2]]]


def gl_matrix(m):
    return [m[r][c] for c in range(4) for r in range(4)]


def is_identity(m):
    return all(abs(m[r][c] - (1.0 if r == c else 0.0)) < 1e-9 for r in range(4) for c in range(4))


def fmt_value(field, v):
    t = field.get('type', 'u32')
    if t == 'f32':
        return round(struct.unpack('<f', struct.pack('<I', v))[0], 6)
    if t == 'hash':
        return '0x%08x' % v
    if t == 's32':
        return v - (1 << 32) if v & 0x80000000 else v
    return v


# ---------------------------------------------------------------------------------------------------------------
# The exporter
# ---------------------------------------------------------------------------------------------------------------

class LevelExporter(eg.Exporter):
    """Reuses export_gltf.Exporter's mesh, material and texture code, with a level instead of a skin."""

    def __init__(self, path, log=print, textures=True, name=None, gltf=None):
        """path: a bundle file, or its bytes with `name` (the level hashcode); gltf: a Gltf to add to (another
        exporter's, so two levels share one file)"""
        # (Exporter.__init__ wants a skin; set what build_mesh and the material code use instead)
        self.log = log
        self.g = gltf or eg.Gltf()
        self.materials, self.images = {}, {}
        self.image_writer = None
        self.vertex_ids = False
        self.shared_rest = False
        self.morph_nodes = []
        self.textures = textures
        self.path = path
        data = path if isinstance(path, (bytes, bytearray)) else open(path, 'rb').read()
        self.level = pl.Level(data, name=name or os.path.basename(path)[:8].lower())
        self.tex = parse_tex.Level(data, name=self.level.name)
        packs01 = [pk for pk in self.tex.packs]
        files = self.level.files
        if [f.name for f in files] != [pk.name for pk in packs01]:
            raise ValueError('graphics packs of the level and of the texture reader disagree')
        self.texpack = {id(f): pk for f, pk in zip(files, packs01)}
        self.mesh_cache = {}
        self.groups = {}
        self.skel_table = parse_skeleton.load_table()
        import bundle
        self.skins = {int(n[:8], 16): data[o:o + l] for t, n, o, l in bundle.walk(data) if n.lower().startswith('05')}
        self.character_cache = {}
        self.stats = {}

    def material_for(self, pk, st):
        if not self.textures:
            st = dict(st, tex0=0xffff)
        return eg.Exporter.material_for(self, pk, st)

    # --- helpers
    def group(self, *names):
        """a group node (empty) under the root, nested by name"""
        key = names
        if key in self.groups:
            return self.groups[key]
        parent = self.group(*names[:-1]) if len(names) > 1 else self.root
        n = self.g.node(name=names[-1])
        self.g.j['nodes'][parent].setdefault('children', []).append(n)
        self.groups[key] = n
        return n

    def add(self, parent, **kw):
        n = self.g.node(**kw)
        self.g.j['nodes'][parent].setdefault('children', []).append(n)
        return n

    def count(self, what):
        self.stats[what] = self.stats.get(what, 0) + 1

    def mesh_of(self, f, ent):
        """the glTF mesh of an entity (built once, shared by every placement of it), or None"""
        key = (id(f), ent.index)
        if key not in self.mesh_cache:
            m = ent.mesh
            if m is None or m.empty:
                self.mesh_cache[key] = None
            else:
                mi, _ = self.build_mesh(self.texpack[id(f)], ent, False, '%s_%s_%d' % (f.name, ent.name or 'mesh',
                                                                                      ent.index))
                self.mesh_cache[key] = mi
        return self.mesh_cache[key]

    def line_mesh(self, name, segments, colour):
        """a LINES mesh from [(a, b)] point pairs, with an unlit colour material"""
        g = self.g
        pts = [p for s in segments for p in s]
        if not pts:
            return None
        mat_key = ('lines',) + tuple(colour)
        if mat_key not in self.materials:
            self.materials[mat_key] = g.add('materials', dict(
                name='lines_%02x%02x%02x' % tuple(int(c * 255) for c in colour),
                pbrMetallicRoughness=dict(baseColorFactor=list(colour) + [1.0], metallicFactor=0.0,
                                          roughnessFactor=1.0),
                extensions=dict(KHR_materials_unlit={})))
            used = g.j.setdefault('extensionsUsed', [])
            if 'KHR_materials_unlit' not in used:
                used.append('KHR_materials_unlit')
        acc = g.accessor('f', FLOAT, 'VEC3', [tuple(p) for p in pts], eg.ARRAY_BUFFER, minmax=True)
        return g.add('meshes', dict(name=name, primitives=[dict(attributes=dict(POSITION=acc), mode=1,
                                                                material=self.materials[mat_key])]))

    def box_mesh(self, bbmin, bbmax, colour):
        key = ('box', tuple(round(c, 5) for c in bbmin), tuple(round(c, 5) for c in bbmax), tuple(colour))
        if key not in self.mesh_cache:
            x0, y0, z0 = bbmin
            x1, y1, z1 = bbmax
            c = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1),
                 (x0, y1, z1)]
            edges = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)]
            self.mesh_cache[key] = self.line_mesh('box', [(c[a], c[b]) for a, b in edges], colour)
        return self.mesh_cache[key]

    # --- characters: a skin's meshes in the bind pose, rigid parts at their bones, a weapon at a datum
    def character(self, skin_hash):
        if skin_hash in self.character_cache:
            return self.character_cache[skin_hash]
        out = None
        if skin_hash in self.skins:
            s = parse_skin.parse(self.skins[skin_hash], self.skel_table)
            bind = [[-c for c in t] for t, q in s['bind']] if s['bind'] else \
                parse_skin.world_bind(s, self.skel_table[s['skel']])
            parts = []
            for rh in s['riggedHash']:
                hit = self.level.hashtable.get(rh)
                if hit is None and rh & 0x00100000:
                    continue          # a first-person sleeve: never on a placed character
                if hit is not None:
                    mi = self.mesh_of(*hit)
                    if mi is not None:
                        parts.append((mi, None, hit[1].name))
            for i, dh in enumerate(s['discreteHash']):
                hit = self.level.hashtable.get(dh)
                if hit is not None:
                    mi = self.mesh_of(*hit)
                    if mi is not None:
                        parts.append((mi, list(bind[s['discreteBone'][i]]), hit[1].name))
            out = dict(skin=s, bind=bind, parts=parts)
        self.character_cache[skin_hash] = out
        return out

    def place_character(self, parent, skin_hash, weapon=None):
        ch = self.character(skin_hash)
        if ch is None:
            return False
        for mi, t, name in ch['parts']:
            self.add(parent, name=name or 'part', mesh=mi, translation=t)
        if weapon is not None:
            gfx, datum_id = weapon
            hit = self.level.hashtable.get(gfx)
            d = next((d for d in ch['skin']['datums'] if d['id'] == datum_id), None)
            if hit is not None and d is not None and 0 <= d['bone'] < len(ch['bind']):
                mi = self.mesh_of(*hit)
                if mi is not None:
                    b = ch['bind'][d['bone']]
                    self.add(parent, name='weapon_%s' % hit[1].name, mesh=mi,
                             translation=[b[k] + d['t'][k] for k in range(3)], rotation=list(d['q']))
        return True

    # --- the static world
    def export_static(self, collision, link=True):
        rooms_linked = False
        if link:
            try:
                self.level.link_rooms()
                rooms_linked = True
            except Exception as e:
                self.log('  room linking failed: %s' % e)
        for c in self.level.static_cels():
            self.export_cel(c, collision, rooms_linked)

    def export_cel(self, c, collision=False, rooms_linked=False, parent=None):
        """one static cel under `parent` (default: the "rooms" or "static" group); its node, or None"""
        if not c.drawable:
            self.count('static not drawable')
            return None
        mi = self.mesh_of(c.file, c.entity)
        if parent is None:
            parent = self.group('rooms') if c.is_room else self.group('static')
        m = c.world
        extras = dict(placement=c.placement.index, file=c.file.name, entity=c.entity.name,
                      type='0x%x' % (c.placement.type & 0xffff))
        if c.is_room:
            extras.update(ambient=list(c.ambient), switchChannel=c.switch_channel, musicEvent=c.music_event)
        elif rooms_linked:
            if c.room is not None:
                extras['room'] = c.room.entity.name
        if c.eflags & 0x8:
            extras['billboard'] = True
        node = self.add(parent, name=c.entity.name or 'cel', mesh=mi,
                        matrix=None if is_identity(m) else gl_matrix(m), extras=extras)
        self.count('rooms' if c.is_room else 'static objects')
        if collision:
            coll = c.file.collision(c.entity)
            if coll is not None:
                tris = coll.triangles()
                key = ('coll', id(c.file), c.entity.index)
                if key not in self.mesh_cache:
                    self.mesh_cache[key] = self.collision_mesh(c.entity.name, tris)
                if self.mesh_cache[key] is not None:
                    mc = c.world_collision
                    self.add(self.group('collision'), name='coll_%s' % (c.entity.name or 'cel'),
                             mesh=self.mesh_cache[key], matrix=None if is_identity(mc) else gl_matrix(mc))
        return node

    def collision_mesh(self, name, tris):
        pts, idx = [], []
        for t in tris:
            vs = t['verts'] if isinstance(t, dict) else t[:3]
            base = len(pts)
            pts += [tuple(v) for v in vs]
            idx += [base, base + 2, base + 1]
        if not pts:
            return None
        g = self.g
        key = ('collision material',)
        if key not in self.materials:
            self.materials[key] = g.add('materials', dict(name='collision', doubleSided=True,
                                                          pbrMetallicRoughness=dict(baseColorFactor=[0.9, 0.3, 0.9, 0.35],
                                                                                    metallicFactor=0.0),
                                                          alphaMode='BLEND'))
        pa = g.accessor('f', FLOAT, 'VEC3', pts, eg.ARRAY_BUFFER, minmax=True)
        ia = g.accessor('I', UINT, 'SCALAR', idx, eg.ELEMENT_ARRAY_BUFFER)
        return g.add('meshes', dict(name='coll_%s' % name, primitives=[dict(attributes=dict(POSITION=pa), indices=ia,
                                                                             material=self.materials[key])]))

    # --- dynamic objects
    def object_matrix(self, plc, t, ent):
        tid = plc.type & 0xff
        rot = list(plc.rot)
        if tid == 62:                       # Wire: rot.z negated before the matrix is built
            rot[2] = -rot[2]
        if tid == 42:                       # SkyObject: RotMatrix; at the camera (origin) when glist flag 0x2
            pos = (0.0, 0.0, 0.0) if ent.flags & 0x2 else plc.pos
            return pl.matrix(pl.rot_xyz(rot), pos)
        if tid == 242:                      # Emitter: pos + quat (rot unused)
            return pl.matrix(pl.quat_matrix(plc.quat), plc.pos)
        if tid == 219 and plc.path_frames:  # Door: the closed pose comes from its path
            frames = self.path_frames(plc.path_index)
            if frames:
                movement = plc.dword(0)
                k = (len(frames) - 1) // 2 if movement == 0 else 0
                f = frames[min(k, len(frames) - 1)]
                return pl.matrix(pl.quat_matrix(f['quat']), f['pos'])
        return pl.matrix(obj_rot(rot), plc.pos)

    def path_frames(self, index):
        blocks = self.level.map.blocks.get(0x19, []) if self.level.map else []
        if 0 <= index < len(blocks):
            return pl.decode_path(self.level.map.data, blocks[index])
        return None

    def object_properties(self, plc, t, tid, ent):
        """(extras, label). Flat custom properties, one per decoded key: Blender shows each as its own typed field
        (a nested dict comes in as one JSON-looking group). Names starting with "_" are hidden in Blender's panel:
        _channelsIn / _channelsOut / _channelsUsed feed blender_import_level.py's switch-channel collections. The
        label names the object after its type and placement, with the channels it writes and reads."""
        fields = {f_['key']: f_ for f_ in t.get('fields', [])}
        extras = {'placementType': '%s (%d)' % (t['name'], tid), 'placementIndex': plc.index}
        if ent.name:
            extras['glist'] = ent.name
        if ent.hash != 0xffffffff:
            extras['glistHash'] = '0x%08x' % ent.hash
        ch_in, ch_out, ch_use = set(), set(), set()
        for k, v in sorted(plc.kv):
            fd = fields.get(k)
            pname = fd['name'] if fd else 'key%d' % k
            if pname in extras:
                pname = 'key%d_%s' % (k, pname)
            extras[pname] = fmt_value(fd or {}, v)
            if fd and fd.get('type') == 'switch' and v & 0xff:
                role = fd.get('channelRole', '')
                (ch_in if role == 'in' else ch_out if role == 'out' else ch_use).add(v & 0xff)
        if plc.path_frames:
            extras.update(pathIndex=plc.path_index, pathFrames=plc.path_frames, pathRate=plc.path_rate)
        label = '%s %d' % (t['name'], plc.index)
        for key, arrow, chans in (('_channelsOut', ARROW_OUT, ch_out), ('_channelsIn', ARROW_IN, ch_in),
                                  ('_channelsUsed', ARROW_BOTH, ch_use)):
            if chans:
                extras[key] = sorted(chans)
                label += ' %s ch %s' % (arrow, ','.join(str(c) for c in sorted(chans)))
        return extras, label

    def export_objects(self, characters):
        objs = self.level.dynamic_objects()
        sky_slots = {}
        for plc, (f, ent) in objs:
            tid = plc.type & 0xff
            if tid == 42:
                sky_slots[plc.dword(0)] = plc          # a later placement in a slot replaces the earlier one
        for plc, (f, ent) in objs:
            self.export_object(plc, f, ent, characters, sky_slots)

    def export_object(self, plc, f, ent, characters=True, sky_slots=None, parent=None):
        """one dynamic object under `parent` (default: its category and type group); its node, or None when the
        game creates nothing for it"""
        weapon_table = TYPES.get(15, {}).get('weapon_gfx', {})
        tid = plc.type & 0xff
        t = TYPES.get(tid)
        if t is None:
            self.count('unknown type %d' % tid)
            return None
        kind = t.get('visual', {}).get('kind', 'none') if isinstance(t.get('visual'), dict) else 'none'
        if tid in (255, 246, 247, 221):
            return None                           # the game does nothing with these
        if tid == 42 and sky_slots is not None and sky_slots.get(plc.dword(0)) is not plc:
            return None
        if tid == 15 and plc.dword(0) == 0x05000000:
            return None                           # the editor's template drone
        extras, name = self.object_properties(plc, t, tid, ent)
        m = self.object_matrix(plc, t, ent)
        category = {'volume': 'volumes', 'skin': 'characters', 'marker': 'markers', 'light': 'lights',
                    'sky': 'sky'}.get(kind, 'objects')
        if parent is None:
            parent = self.group(category, t['name'])
        node_kw = dict(name=name, matrix=None if is_identity(m) else gl_matrix(m), extras=extras)
        if kind == 'skin' and characters:
            skin_hash = plc.dword(0) if tid == 15 else (0x05000003 if tid == 243 else None)
            if tid == 15 and skin_hash == 0x050000b1 and skin_hash not in self.skins:
                skin_hash = 0x0500008c            # maybeFixupSkinHashcode (EvilBase)
            node = self.add(parent, **node_kw)
            weapon = None
            if tid == 15:
                wt = weapon_table.get(str(plc.dword(9)))
                if wt and wt.get('gfx'):
                    weapon = (int(wt['gfx'], 16), wt.get('whichBone'))
            if skin_hash is None or not self.place_character(node, skin_hash, weapon):
                self.count('characters without a skin in this level')
            self.count('characters')
        elif kind == 'volume':
            sc = plc.scale if tid in (245, 248) else (1.0, 1.0, 1.0)
            mi = self.box_mesh(ent.bbmin, ent.bbmax, (1.0, 0.6, 0.1) if tid not in (245, 248) else (0.2, 0.6, 1.0))
            node = self.add(parent, mesh=mi, scale=list(sc) if tuple(sc) != (1.0, 1.0, 1.0) else None,
                            **node_kw)
            self.count('volumes')
        elif kind in ('glist', 'sprite', 'sky', 'animated'):
            mi = self.mesh_of(f, ent)
            if ent.flags & 0x8010:
                # glist flag 0x10 (hidden) or 0x8000 (never drawn): the game draws nothing here - often an
                # editor-coloured marker (Destroy volumes use a red "Portal_Red" box). Kept, but invisible.
                extras['hidden'] = True
                node_kw['extensions'] = dict(KHR_node_visibility=dict(visible=False))
                used = self.g.j.setdefault('extensionsUsed', [])
                if 'KHR_node_visibility' not in used:
                    used.append('KHR_node_visibility')
                self.count('hidden objects')
            node = self.add(parent, mesh=mi, **node_kw)
            self.count('objects with a mesh' if mi is not None else 'objects without geometry')
        else:
            node = self.add(parent, **node_kw)
            self.count('markers and logic')
        if tid == 78:
            self.light(parent, name + ' light', plc.pos, [plc.dword(k) / 255.0 for k in (1, 2, 3)],
                       max(plc.as_float(0), 0.0) or None)
        return node

    # --- lights, paths, portals
    def light(self, parent, name, pos, colour, rng):
        g = self.g
        ext = g.j.setdefault('extensions', {}).setdefault('KHR_lights_punctual', {'lights': []})
        used = g.j.setdefault('extensionsUsed', [])
        if 'KHR_lights_punctual' not in used:
            used.append('KHR_lights_punctual')
        li = dict(type='point', color=[min(max(c, 0.0), 1.0) for c in colour], intensity=20.0)
        if rng:
            li['range'] = rng
        ext['lights'].append(li)
        self.add(parent, name=name, translation=list(pos),
                 extensions=dict(KHR_lights_punctual=dict(light=len(ext['lights']) - 1)))
        self.count('lights')

    def export_lights(self):
        if not self.level.map:
            return
        for b in self.level.map.blocks.get(0x26, []):
            for i, r in enumerate(pl.decode_radiators(self.level.map.data, b)):
                self.light(self.group('lights', 'radiators'), 'radiator %d' % i, r['pos'], r['colour'], r['range'])

    def export_paths(self):
        if not self.level.map:
            return
        data = self.level.map.data
        for i, b in enumerate(self.level.map.blocks.get(0x19, [])):
            fr = pl.decode_path(data, b)
            segs = [(fr[k]['pos'], fr[k + 1]['pos']) for k in range(len(fr) - 1)]
            mi = self.line_mesh('path_%d' % i, segs, (0.1, 0.9, 0.3))
            if mi is not None:
                self.add(self.group('paths', 'object paths'), name='path %d' % i, mesh=mi,
                         extras=dict(frames=len(fr)))
                self.count('paths')
        for b in self.level.map.blocks.get(0x05, []):
            ai = pl.decode_ai_paths(data, b)
            for p in ai.get('paths', []):
                pos = {n['index']: n['pos'] for n in p['nodes']}
                idx = [n['pos'] for n in p['nodes']]
                segs = []
                for a, c in p['links']:
                    pa = pos.get(a, idx[a] if a < len(idx) else None)
                    pc = pos.get(c, idx[c] if c < len(idx) else None)
                    if pa is not None and pc is not None:
                        segs.append((pa, pc))
                mi = self.line_mesh('ai_%s' % p['name'], segs, (0.9, 0.2, 0.2) if p['bound'] else (0.9, 0.9, 0.2))
                if mi is not None:
                    self.add(self.group('paths', 'AI bounds' if p['bound'] else 'AI routes'), name=p['name'] or 'ai',
                             mesh=mi)
                    self.count('AI paths')

    def export_portals(self):
        if not self.level.map:
            return
        for b in self.level.map.blocks.get(0x21, []):
            for p in pl.decode_portals(self.level.map.data, b):
                q = p['quad']
                mi = self.line_mesh('portal', [(q[k], q[(k + 1) % 4]) for k in range(4)], (0.3, 0.3, 1.0))
                self.add(self.group('portals'), name='portal %d' % p['index'], mesh=mi,
                         extras=dict(celA=p['celA'], celB=p['celB'], flags=p['flags']))
                self.count('portals')

    def run(self, collision=False, portals=False, characters=True):
        self.root = self.g.node(name='level_%s' % self.level.name,
                                extras=dict(level=self.level.name, mapFile=self.level.map.name if self.level.map else ''))
        self.g.j['scenes'][0]['nodes'].append(self.root)
        self.export_static(collision)
        self.export_objects(characters)
        self.export_lights()
        self.export_paths()
        if portals:
            self.export_portals()
        return self.stats


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level', nargs='?', help='level bundle, e.g. 07000001')
    ap.add_argument('-o', '--out')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--collision', action='store_true', help='add the collision meshes')
    ap.add_argument('--portals', action='store_true', help='add the portals as outlines')
    ap.add_argument('--no-characters', action='store_true', help='drones as empties instead of their meshes')
    ap.add_argument('--no-textures', action='store_true')
    a = ap.parse_args(argv)
    if a.list:
        for fn in sorted(glob.glob(os.path.join(LVL, '07*.bin'))):
            lv = pl.Level(fn)
            n_dyn = len(lv.dynamic_objects())
            print('%s map %s, %d files, %d static cels, %d objects' % (
                lv.name, lv.map.name if lv.map else '-', len(lv.files), len(lv.static_cels()), n_dyn))
        return 0
    if not a.level:
        ap.error('give a level bundle, or --list')
    path = os.path.join(LVL, a.level.lower().replace('.bin', '') + '.bin')
    ex = LevelExporter(path, textures=not a.no_textures)
    if ex.level.map is None:
        print('%s has no map file (bundle entry type 1), so it is not a level - nothing written' % a.level)
        return 1
    stats = ex.run(collision=a.collision, portals=a.portals, characters=not a.no_characters)
    if not stats:
        print('%s places nothing (a front-end bundle, not a level) - nothing written' % a.level)
        return 1
    out = a.out or '%s.glb' % a.level.lower()
    ex.g.write_glb(out)
    print('%s: %s, %.1f MB' % (out, ', '.join('%s %d' % kv for kv in sorted(stats.items())), os.path.getsize(out) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
