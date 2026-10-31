"""Compares two builds of an action-engine level bundle (07xxxxxx.bin): what changed between OLD and NEW.

  python diff_level.py OLD.bin NEW.bin [-o OUT_PREFIX] [--old-label pre-release] [--new-label retail]

Writes OUT_PREFIX.json (everything) and OUT_PREFIX.md (a readable summary); without -o, prints the summary.

- A bundle whose end is missing (bundle.trim) is read from the entries that are all there; what is missing is
  listed, and comparisons that need a missing file (the map file, last in every bundle) are skipped.
- Dynamic placements (the map file's, Level.dynamic_objects) are matched per placement type: first by nearest
  position within --tol (score = distance + KEY_WEIGHT per differing key, glist and path), then the remaining
  ones with identical keys and glist anywhere within FAR, then those within NEAR x --tol that differ in at most
  NEAR_KEYS keys. Each pair is unchanged, or moved / rotated / keys
  changed (any of them); the rest are added (NEW only) or removed (OLD only). A match is ambiguous when another
  candidate scored within AMBIGUOUS of the chosen one.
- Static cels are matched by (file, entity name), nearest position first; geometry is compared per entity by the
  hash of its mesh's vertex and index data, then its strips (materials); textures by name and pixel hash;
  collision per entity and portals by index.
- Object paths by index, AI paths by name, radiators and map sounds by nearest position.
- Sub-files present in only one bundle, and changed ones by kind. A change is trivial when it only touches the
  bytes listed in TRIVIAL (sequences' bounding sphere) or, in a graphics pack, when its placements and textures
  differ only in bytes the game does not read (block_signature) and fewer than TRIVIAL_PACK_BYTES bytes differ
  in its other blocks (the texture list headers aside), none resized.
"""
import argparse
import collections
import json
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'anim'))
import bundle              # noqa: E402
import parse_gfx           # noqa: E402
import parse_tex           # noqa: E402
import parse_level as pl   # noqa: E402

TOL = 4.0
FAR = 1000.0
KEY_WEIGHT = 0.5
AMBIGUOUS = 0.05
NEAR = 4
NEAR_KEYS = 2
MOVE_EPS = 1e-3
ROT_EPS = 1e-3
TRIVIAL_PACK_BYTES = 64

KINDS = {0: 'graphics pack', 1: 'map file', 2: 'graphics pack', 3: 'skin', 4: 'animation sequence',
         5: 'animation script', 6: 'skeleton', 7: 'game script', 8: 'menu file', 11: 'graphics pack',
         12: 'loadable file'}
PACK_TYPES = (0, 1, 2, 11)
# blocks whose change is reported on its own (placements, model, texture data, collision, paths, portals)
REAL_BLOCKS = {0x1a: 'placements', 0x0d: 'meshes', 0x18: 'textures', 0x2e: 'collision', 0x2f: 'collision',
               0x19: 'paths', 0x21: 'portals', 0x05: 'AI paths', 0x26: 'radiators', 0x28: 'map sounds'}
# the texture list's headers (0x10) and palette records (0x0f): they differ between builds of identical textures
TEXTURE_HEADERS = (0x0f, 0x10)
TRIVIAL = {4: [(0x10, 0x20, 'bounding-sphere floats')]}


def load_types():
    types = {}
    for group in ('actors', 'logic', 'dressing'):
        for tid, t in json.load(open(os.path.join(HERE, 'types_%s.json' % group))).items():
            if not tid.startswith('_') and isinstance(t, dict):
                types[int(tid)] = t
    return types


TYPES = load_types()


def type_name(tid):
    return TYPES.get(tid, {}).get('name') or pl.PLACE_NAMES.get(tid, 'type%d' % tid)


def fmt_value(field, v):
    t = field.get('type', 'u32')
    if t == 'f32':
        return round(struct.unpack('<f', struct.pack('<I', v))[0], 6)
    if t == 'hash':
        return '0x%08x' % v
    if t == 's32':
        return v - (1 << 32) if v & 0x80000000 else v
    return v


def crc(b):
    return '%08x' % zlib.crc32(b)


def vec(v):
    return [round(x, 4) for x in v]


def dist(a, b):
    return math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))


def angle_diff(a, b):
    return max(abs((a[k] - b[k] + math.pi) % (2 * math.pi) - math.pi) for k in range(3))


# ---------------------------------------------------------------------------------------------------------------
# One side
# ---------------------------------------------------------------------------------------------------------------

class Side:
    def __init__(self, path, label):
        self.path, self.label = path, label
        data = open(path, 'rb').read()
        self.name = os.path.basename(path)[:8].lower()
        ents, self.dir_complete = bundle.walk_dir(data)
        self.data, missing = bundle.trim(data)
        self.missing = [dict(type=t, kind=KINDS.get(t, 'type %d' % t), name=n, len=l) for t, n, l in missing]
        self.entries = {(t, n.lower()): self.data[o:o + l] for t, n, o, l in bundle.walk(self.data)}
        self.errors = []
        self.level = None
        self.tex = None
        try:
            self.level = pl.Level(self.data, name=self.name)
        except Exception as e:
            self.errors.append('level: %s' % e)
        if self.level is not None:
            try:
                self.tex = parse_tex.Level(self.data, name=self.name)
            except Exception as e:
                self.errors.append('textures: %s' % e)
        self.has_map = self.level is not None and self.level.map is not None

    def summary(self):
        return dict(path=self.path, label=self.label, entries=len(self.entries), missing=self.missing,
                    directory_complete=self.dir_complete, map_file=self.level.map.name if self.has_map else None,
                    errors=self.errors)


# ---------------------------------------------------------------------------------------------------------------
# Sub-files
# ---------------------------------------------------------------------------------------------------------------

def diff_bytes(a, b):
    """[(start, end)] of the differing byte runs over the common length"""
    runs = []
    n = min(len(a), len(b))
    i = 0
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            runs.append((i, j))
            i = j
        else:
            i += 1
    return runs


def block_signature(d, blk):
    """the block's bytes as the game reads them: placements without their padding (+0x02, +0x46), textures by
    their header and top mip level (the smaller levels differ by a byte or two between builds), portals without
    their padding; other blocks as they are"""
    if blk.type == 0x1a:
        return repr([(p.ent, p.glist_hash, p.type, p.pos, p.rot, p.quat, p.scale, p.path_frames, p.path_rate,
                      p.path_index, p.kv) for p in pl.decode_placements(d, blk)]).encode()
    if blk.type == 0x18:
        o = blk.off + 4
        h = struct.unpack_from('<13I', d, o)
        name = d[o + 0x34:o + 0x54].split(b'\0')[0]
        if h[0] != parse_tex.KXT_MAGIC:
            return name          # a TD duplicate: its load index follows the level's texture numbering
        frame_size, w, ht, alpha, levels, linear, ftype, frames = h[2:10]
        fmt = parse_tex.d3d_format(parse_tex.register_type(ftype, alpha), linear)
        need = parse_tex.level_size(fmt, w, ht)
        data = o + 0x54 + 4 * frames
        return repr((h[1], w, ht, alpha, levels, linear, ftype, frames, h[10], name)).encode() + b''.join(
            d[data + k * frame_size:data + k * frame_size + need] for k in range(frames))
    if blk.type == 0x21:
        return repr([(p['quad'], p['celA'], p['celB'], p['flags']) for p in pl.decode_portals(d, blk)]).encode()
    return d[blk.off:blk.off + blk.size]


def pack_change(a, b):
    """what changed in a graphics pack, by block type"""
    try:
        ba, bb = parse_gfx.walk(a), parse_gfx.walk(b)
    except Exception as e:
        return dict(error=str(e)), False
    seq_a = [x.type for x in ba]
    seq_b = [x.type for x in bb]
    out = collections.Counter()
    resized = False
    real = set()
    if seq_a == seq_b:
        for x, y in zip(ba, bb):
            pa, pb = a[x.off:x.off + x.size], b[y.off:y.off + y.size]
            if pa != pb:
                if len(pa) != len(pb):
                    resized = True
                n = sum(e - s for s, e in diff_bytes(pa, pb)) + abs(len(pa) - len(pb))
                out[x.type] += n
                if x.type in REAL_BLOCKS and block_signature(a, x) != block_signature(b, y):
                    real.add(REAL_BLOCKS[x.type])
    else:
        resized = True
        ca, cb = collections.Counter(seq_a), collections.Counter(seq_b)
        for t in set(ca) | set(cb):
            if ca[t] != cb[t]:
                out[t] += 1
                if t in REAL_BLOCKS:
                    real.add(REAL_BLOCKS[t])
        for t in REAL_BLOCKS:
            pa = b''.join(block_signature(a, x) for x in ba if x.type == t)
            pb = b''.join(block_signature(b, x) for x in bb if x.type == t)
            if pa != pb:
                real.add(REAL_BLOCKS[t])
    total = sum(out.values())
    other = sum(n for t, n in out.items() if t not in REAL_BLOCKS and t not in TEXTURE_HEADERS)
    trivial = not real and other < TRIVIAL_PACK_BYTES and not (resized and other)
    return dict(blocks={'0x%02x' % t: n for t, n in sorted(out.items())}, resized=resized,
                changed=sorted(real), bytes=total), trivial


def diff_entries(old, new):
    only_old = sorted(k for k in old.entries if k not in new.entries)
    only_new = sorted(k for k in new.entries if k not in old.entries)
    changed = []
    same = 0
    for k in sorted(set(old.entries) & set(new.entries)):
        a, b = old.entries[k], new.entries[k]
        if a == b:
            same += 1
            continue
        t, n = k
        rec = dict(type=t, kind=KINDS.get(t, 'type %d' % t), name=n, old_len=len(a), new_len=len(b))
        trivial = False
        if t in PACK_TYPES:
            info, trivial = pack_change(a, b)
            rec.update(info)
        else:
            runs = diff_bytes(a, b)
            rec['bytes'] = sum(e - s for s, e in runs) + abs(len(a) - len(b))
            for lo, hi, what in TRIVIAL.get(t, []):
                if len(a) == len(b) and runs and all(lo <= s and e <= hi for s, e in runs):
                    trivial = True
                    rec['why'] = what
        rec['trivial'] = trivial
        changed.append(rec)
    by_kind = collections.defaultdict(lambda: dict(only_old=0, only_new=0, same=0, trivial=0, real=0))
    for t, n in only_old:
        by_kind[KINDS.get(t, 'type %d' % t)]['only_old'] += 1
    for t, n in only_new:
        by_kind[KINDS.get(t, 'type %d' % t)]['only_new'] += 1
    for k in set(old.entries) & set(new.entries):
        if old.entries[k] == new.entries[k]:
            by_kind[KINDS.get(k[0], 'type %d' % k[0])]['same'] += 1
    for r in changed:
        by_kind[r['kind']]['trivial' if r['trivial'] else 'real'] += 1
    return dict(same=same, by_kind=dict(sorted(by_kind.items())),
                only_old=[dict(type=t, kind=KINDS.get(t, 'type %d' % t), name=n, len=len(old.entries[(t, n)]))
                          for t, n in only_old],
                only_new=[dict(type=t, kind=KINDS.get(t, 'type %d' % t), name=n, len=len(new.entries[(t, n)]))
                          for t, n in only_new],
                changed=changed)


# ---------------------------------------------------------------------------------------------------------------
# Dynamic placements
# ---------------------------------------------------------------------------------------------------------------

def plc_label(plc):
    return '%s %d' % (type_name(plc.type & 0xff), plc.index)


def plc_keys(plc):
    """{key name: formatted value} with the types_*.json names"""
    fields = {f['key']: f for f in TYPES.get(plc.type & 0xff, {}).get('fields', [])}
    out = {}
    for k, v in sorted(plc.kv):
        fd = fields.get(k)
        out[fd['name'] if fd else 'key%d' % k] = fmt_value(fd or {}, v)
    return out


def plc_channels(plc):
    """[(channel, role, key name)] of the switch-channel keys that hold a channel"""
    out = []
    for f in TYPES.get(plc.type & 0xff, {}).get('fields', []):
        if f.get('type') == 'switch':
            v = plc.keys.get(f['key'], 0) & 0xff
            if v:
                out.append((v, f.get('channelRole') or 'uses', f['name']))
    return out


def key_diff(a, b):
    fields = {f['key']: f for f in TYPES.get(b.type & 0xff, {}).get('fields', [])}
    ka, kb = a.keys, b.keys
    out = []
    for k in sorted(set(ka) | set(kb)):
        if ka.get(k) != kb.get(k):
            fd = fields.get(k, {})
            rec = dict(key=k, name=fd.get('name', 'key%d' % k),
                       old=None if k not in ka else fmt_value(fd, ka[k]),
                       new=None if k not in kb else fmt_value(fd, kb[k]))
            if fd.get('type') == 'switch':
                rec['channelRole'] = fd.get('channelRole') or 'uses'
            out.append(rec)
    return out


def pair_score(a, b):
    d = dist(a.pos, b.pos)
    n = sum(1 for k in set(a.keys) | set(b.keys) if a.keys.get(k) != b.keys.get(k))
    n += a.glist_hash != b.glist_hash
    n += (a.path_frames, a.path_index) != (b.path_frames, b.path_index)
    return d + KEY_WEIGHT * n, d


def match_placements(old, new, tol):
    """[(i_old, j_new, ambiguous)], unmatched old, unmatched new, per type"""
    pairs = []
    by_t_old, by_t_new = collections.defaultdict(list), collections.defaultdict(list)
    for i, p in enumerate(old):
        by_t_old[p.type & 0xff].append(i)
    for j, p in enumerate(new):
        by_t_new[p.type & 0xff].append(j)
    for t in set(by_t_old) | set(by_t_new):
        io, jn = by_t_old.get(t, []), by_t_new.get(t, [])
        cand = []
        for i in io:
            for j in jn:
                s, d = pair_score(old[i], new[j])
                if d <= tol:
                    cand.append((s, d, i, j))
        cand.sort()
        used_i, used_j = set(), set()
        scores_i = collections.defaultdict(list)
        scores_j = collections.defaultdict(list)
        for s, d, i, j in cand:
            scores_i[i].append(s)
            scores_j[j].append(s)
        for s, d, i, j in cand:
            if i in used_i or j in used_j:
                continue
            # another candidate for either side within AMBIGUOUS of this score, still free when it was chosen
            rivals = [x for x in cand if x[0] <= s + AMBIGUOUS and (x[2] == i) != (x[3] == j)
                      and x[2] not in used_i and x[3] not in used_j]
            identical = all(old[x[2]].keys == new[x[3]].keys and dist(old[x[2]].pos, new[x[3]].pos) < MOVE_EPS
                            for x in rivals) if rivals else False
            used_i.add(i)
            used_j.add(j)
            pairs.append((i, j, 'interchangeable' if rivals and identical else bool(rivals)))
        # far moves: same keys and glist, one candidate each way
        rest_i = [i for i in io if i not in used_i]
        rest_j = [j for j in jn if j not in used_j]
        sig = lambda p: (tuple(sorted(p.kv)), p.glist_hash)
        so = collections.defaultdict(list)
        sn = collections.defaultdict(list)
        for i in rest_i:
            so[sig(old[i])].append(i)
        for j in rest_j:
            sn[sig(new[j])].append(j)
        for k, li in so.items():
            lj = sn.get(k, [])
            if len(li) == 1 and len(lj) == 1 and dist(old[li[0]].pos, new[lj[0]].pos) <= FAR:
                pairs.append((li[0], lj[0], False))
                used_i.add(li[0])
                used_j.add(lj[0])
        # longer moves with a key or two changed: nearest first, within NEAR_KEYS
        cand = []
        for i in io:
            for j in jn:
                if i not in used_i and j not in used_j:
                    s, d = pair_score(old[i], new[j])
                    if d <= NEAR * tol and s - d <= KEY_WEIGHT * NEAR_KEYS:
                        cand.append((s, d, i, j))
        for s, d, i, j in sorted(cand):
            if i not in used_i and j not in used_j:
                used_i.add(i)
                used_j.add(j)
                pairs.append((i, j, False))
    mi = set(p[0] for p in pairs)
    mj = set(p[1] for p in pairs)
    return pairs, [i for i in range(len(old)) if i not in mi], [j for j in range(len(new)) if j not in mj]


def plc_record(plc):
    return dict(label=plc_label(plc), type=type_name(plc.type & 0xff), index=plc.index, pos=vec(plc.pos),
                rot=vec(plc.rot), keys=plc_keys(plc), glist='0x%08x' % plc.glist_hash,
                channels=[dict(ch=c, role=r, key=n) for c, r, n in plc_channels(plc)])


def diff_dynamic(old, new, tol):
    a = [p for p, r in old.level.dynamic_objects()]
    b = [p for p, r in new.level.dynamic_objects()]
    pairs, rem, add = match_placements(a, b, tol)
    out = dict(old_count=len(a), new_count=len(b), unchanged=0, ambiguous=0, interchangeable=0,
               changed=[], added=[plc_record(b[j]) for j in add], removed=[plc_record(a[i]) for i in rem])
    per_type = collections.defaultdict(collections.Counter)
    for p in a:
        per_type[type_name(p.type & 0xff)]['old'] += 1
    for p in b:
        per_type[type_name(p.type & 0xff)]['new'] += 1
    for j in add:
        per_type[type_name(b[j].type & 0xff)]['added'] += 1
    for i in rem:
        per_type[type_name(a[i].type & 0xff)]['removed'] += 1
    ids = {}
    for i, j, amb in sorted(pairs, key=lambda x: x[1]):
        pa, pb = a[i], b[j]
        tn = type_name(pb.type & 0xff)
        ids[('old', i)] = ids[('new', j)] = len(ids) // 2
        if amb == 'interchangeable':
            out['interchangeable'] += 1
        elif amb:
            out['ambiguous'] += 1
        d = dist(pa.pos, pb.pos)
        r = angle_diff(pa.rot, pb.rot)
        kd = key_diff(pa, pb)
        what = []
        if d > MOVE_EPS:
            what.append('moved')
        if r > ROT_EPS:
            what.append('rotated')
        if kd:
            what.append('keys')
        if pa.glist_hash != pb.glist_hash:
            what.append('glist')
        if (pa.path_frames, pa.path_rate, pa.path_index) != (pb.path_frames, pb.path_rate, pb.path_index):
            what.append('path')
        if any(abs(pa.scale[k] - pb.scale[k]) > 1e-4 for k in range(3)):
            what.append('scale')
        if not what:
            out['unchanged'] += 1
            per_type[tn]['unchanged'] += 1
            continue
        for w in what:
            per_type[tn][w] += 1
        rec = dict(label=plc_label(pb), old_label=plc_label(pa), type=tn, what=what, old_pos=vec(pa.pos),
                   new_pos=vec(pb.pos), distance=round(d, 4), rotation=round(math.degrees(r), 2), keys=kd,
                   ambiguous=amb)
        if 'glist' in what:
            rec['glist'] = ['0x%08x' % pa.glist_hash, '0x%08x' % pb.glist_hash]
        if 'path' in what:
            rec['path'] = [[pa.path_frames, pa.path_rate, pa.path_index], [pb.path_frames, pb.path_rate, pb.path_index]]
        out['changed'].append(rec)
    out['per_type'] = {k: dict(v) for k, v in sorted(per_type.items())}
    out['channels'] = diff_channels(old, new, a, b, ids)
    out['_pairs'] = pairs
    return out


def channel_users(side, plcs, ids, which):
    users = collections.defaultdict(dict)
    for i, p in enumerate(plcs):
        for c, role, key in plc_channels(p):
            oid = ids.get((which, i), '%s%d' % (which, i))
            users[c][(oid, key)] = (plc_label(p), role)
    for c in side.level.static_cels():
        if c.is_room and c.switch_channel:
            users[c.switch_channel][('room ' + c.entity.name, 'switchChannel')] = ('room %s' % c.entity.name, 'in')
    return users


def diff_channels(old, new, a, b, ids):
    """per channel, the users only one build has: (object, its key, read/write)"""
    uo = channel_users(old, a, ids, 'old')
    un = channel_users(new, b, ids, 'new')
    out = []
    for c in sorted(set(uo) | set(un)):
        o, n = uo.get(c, {}), un.get(c, {})
        gone = [dict(object=o[k][0], key=k[1], role=o[k][1]) for k in o if k not in n or n[k][1] != o[k][1]]
        came = [dict(object=n[k][0], key=k[1], role=n[k][1]) for k in n if k not in o or o[k][1] != n[k][1]]
        if gone or came:
            out.append(dict(channel=c, old_users=len(o), new_users=len(n), removed=gone, added=came,
                            new_writers=sum(1 for v in n.values() if v[1] in ('out', 'inout')),
                            new_readers=sum(1 for v in n.values() if v[1] in ('in', 'inout'))))
    return out


# ---------------------------------------------------------------------------------------------------------------
# Static world
# ---------------------------------------------------------------------------------------------------------------

def mesh_hashes(f, e, texnames):
    """shape (positions and indices), shading (normals, UVs, vertex colours) and strips (with their textures by
    name, so a renumbered texture list does not count)"""
    m = e.mesh
    if m is None or m.empty:
        return None
    vs = m.vertices
    shape = crc(struct.pack('<%df' % (3 * len(vs)), *[c for v in vs for c in v['pos']]) +
                f.data[m.idxOff:m.idxOff + m.idxCnt * 2])
    shading = crc(repr([(v['normal'], v['uv'], v['colour']) for v in vs]).encode())
    tn = lambda i: texnames[i] if i < len(texnames) else i
    strips = crc(repr([(tn(q.tex0), tn(q.tex1), q.count, q.flags, q.change, q.palette) for q in m.prims]).encode())
    return dict(shape=shape, shading=shading, strips=strips, verts=m.vtxCnt, tris=m.stripTriCnt - m.degenCnt)


def texture_names(side):
    """{pack name: [texture name per index]}"""
    if side.tex is None:
        return {}
    return {pk.name: [t.name.lower() for t in pk.textures] for pk in side.tex.packs}


def entity_key(f, e):
    return (f.name, e.name) if e.name else (f.name, '#%d' % e.index)


def diff_entities(old, new):
    """per entity present in both: geometry, strips, bounds and collision changes"""
    ea = {entity_key(f, e): (f, e) for f in old.level.files for e in f.entities}
    eb = {entity_key(f, e): (f, e) for f in new.level.files for e in f.entities}
    files_a = set(f.name for f in old.level.files)
    files_b = set(f.name for f in new.level.files)
    tex_a = texture_names(old)
    tex_b = texture_names(new)
    geom, coll = [], []
    for k in sorted(set(ea) & set(eb)):
        (fa, a), (fb, b) = ea[k], eb[k]
        ha, hb = mesh_hashes(fa, a, tex_a.get(fa.name, [])), mesh_hashes(fb, b, tex_b.get(fb.name, []))
        rec = None
        if (ha is None) != (hb is None):
            rec = dict(change='mesh added' if ha is None else 'mesh removed')
        elif ha and ha['shape'] != hb['shape']:
            rec = dict(change='shape', verts=[ha['verts'], hb['verts']], tris=[ha['tris'], hb['tris']])
        elif ha and ha['shading'] != hb['shading']:
            rec = dict(change='normals, UVs or vertex colours')
        elif ha and ha['strips'] != hb['strips']:
            rec = dict(change='strips or textures')
        if rec is not None:
            rec.update(file=k[0], entity=k[1], old_bounds=[vec(a.bbmin), vec(a.bbmax)],
                       new_bounds=[vec(b.bbmin), vec(b.bbmax)])
            geom.append(rec)
        ca = fa.data[a.coll_block.off:a.coll_block.off + a.coll_block.size] if a.coll_block else None
        cb = fb.data[b.coll_block.off:b.coll_block.off + b.coll_block.size] if b.coll_block else None
        if ca != cb:
            coll.append(dict(file=k[0], entity=k[1],
                             change='added' if ca is None else 'removed' if cb is None else 'changed'))
    # entities of a file both builds have, present in one only
    only = lambda x, y, fy: [dict(file=k[0], entity=k[1]) for k in sorted(set(x) - set(y)) if k[0] in fy]
    return dict(geometry=geom, collision=coll, only_old=only(ea, eb, files_b), only_new=only(eb, ea, files_a))


def cel_record(c):
    return dict(file=c.file.name, entity=c.entity.name or '#%d' % c.entity.index, room=c.is_room,
                type='0x%x' % (c.placement.type & 0xffff), pos=vec(c.pos), rot=vec(c.rot),
                centre=vec(c.centre), radius=round(c.radius, 3))


def diff_cels(old, new, geom_changed):
    a, b = old.level.static_cels(), new.level.static_cels()
    files_a = set(f.name for f in old.level.files)
    files_b = set(f.name for f in new.level.files)
    by_a, by_b = collections.defaultdict(list), collections.defaultdict(list)
    for i, c in enumerate(a):
        by_a[(c.file.name, c.entity.name or '#%d' % c.entity.index)].append(i)
    for j, c in enumerate(b):
        by_b[(c.file.name, c.entity.name or '#%d' % c.entity.index)].append(j)
    pairs = []
    for k in set(by_a) & set(by_b):
        cand = sorted((dist(a[i].pos, b[j].pos), i, j) for i in by_a[k] for j in by_b[k])
        ui, uj = set(), set()
        for d, i, j in cand:
            if i not in ui and j not in uj:
                ui.add(i)
                uj.add(j)
                pairs.append((i, j))
    mi = set(p[0] for p in pairs)
    mj = set(p[1] for p in pairs)
    # a cel of a file only one build has is reported with that file, not as added or removed here
    removed = [i for i in range(len(a)) if i not in mi and a[i].file.name in files_b]
    added = [j for j in range(len(b)) if j not in mj and b[j].file.name in files_a]
    moved, keys, geom = [], [], []
    for i, j in pairs:
        ca, cb = a[i], b[j]
        d = dist(ca.pos, cb.pos)
        r = angle_diff(ca.rot, cb.rot)
        if d > MOVE_EPS or r > ROT_EPS:
            moved.append(dict(cel_record(cb), old_pos=vec(ca.pos), distance=round(d, 4),
                              rotation=round(math.degrees(r), 2)))
        if ca.placement.keys != cb.placement.keys:
            keys.append(dict(cel_record(cb), old_keys={str(k): v for k, v in sorted(ca.placement.kv)},
                             new_keys={str(k): v for k, v in sorted(cb.placement.kv)}))
        k = (cb.file.name, cb.entity.name or '#%d' % cb.entity.index)
        if k in geom_changed:
            geom.append(dict(cel_record(cb), change=geom_changed[k]))
    return dict(old_count=len(a), new_count=len(b), old_rooms=sum(c.is_room for c in a),
                new_rooms=sum(c.is_room for c in b), unchanged=len(pairs) - len(moved),
                added=[cel_record(b[j]) for j in added], removed=[cel_record(a[i]) for i in removed],
                moved=moved, keys=keys, geometry=geom, _pairs=pairs, _added=added, _removed=removed)


def texture_table(side):
    out = {}
    if side.tex is None:
        return out
    for pk in side.tex.packs:
        for t in pk.textures:
            if t.kind != 'KXT':
                continue
            top = parse_tex.level_size(t.fmt(), t.width, t.height)
            px = b''.join(t.d[t.dataOff + k * t.frameSize:t.dataOff + k * t.frameSize + top]
                          for k in range(t.numFrames))
            out.setdefault('%s/%s' % (pk.name, t.name.lower()), dict(pack=pk.name, hash=crc(px),
                                                                     size=[t.width, t.height], frames=t.numFrames))
    return out


def diff_textures(old, new):
    a, b = texture_table(old), texture_table(new)
    return dict(old_count=len(a), new_count=len(b),
                added=[dict(name=k, **b[k]) for k in sorted(set(b) - set(a))],
                removed=[dict(name=k, **a[k]) for k in sorted(set(a) - set(b))],
                changed=[dict(name=k, old=a[k], new=b[k]) for k in sorted(set(a) & set(b))
                         if a[k]['hash'] != b[k]['hash']])


def diff_portals(old, new):
    """by index, with the rooms each joins by entity name"""
    def portals(side):
        m = side.level.map
        ent = lambda i: m.entities[i].name if i < len(m.entities) else '#%d' % i
        return [dict(p, rooms=(ent(p['celA']), ent(p['celB']))) for b in m.blocks.get(0x21, [])
                for p in pl.decode_portals(m.data, b)]
    pa, pb = portals(old), portals(new)
    changed = []
    for x, y in zip(pa, pb):
        mv = max(dist(x['quad'][k], y['quad'][k]) for k in range(4))
        if x['rooms'] != y['rooms'] or x['flags'] != y['flags'] or mv > MOVE_EPS:
            changed.append(dict(index=x['index'], old_rooms=x['rooms'], new_rooms=y['rooms'],
                                flags=[x['flags'], y['flags']], max_move=round(mv, 4)))
    return dict(old_count=len(pa), new_count=len(pb), changed_in_common=len(changed), changed=changed)


# ---------------------------------------------------------------------------------------------------------------
# Paths, AI paths, lights, sounds
# ---------------------------------------------------------------------------------------------------------------

def diff_paths(old, new):
    pa, pb = old.level.paths(), new.level.paths()
    changed = []
    for i, (x, y) in enumerate(zip(pa, pb)):
        if len(x) != len(y):
            changed.append(dict(index=i, frames=[len(x), len(y)]))
        else:
            m = max([dist(x[k]['pos'], y[k]['pos']) for k in range(len(x))] or [0.0])
            q = max([max(abs(x[k]['quat'][c] - y[k]['quat'][c]) for c in range(4)) for k in range(len(x))] or [0.0])
            if m > MOVE_EPS or q > 1e-4:
                changed.append(dict(index=i, frames=[len(x), len(y)], max_move=round(m, 4)))
    return dict(old_count=len(pa), new_count=len(pb), changed=changed)


def diff_ai_paths(old, new):
    a, b = old.level.ai_paths() or {}, new.level.ai_paths() or {}
    ma = {p['name']: p for p in a.get('paths', [])}
    mb = {p['name']: p for p in b.get('paths', [])}
    changed = []
    for k in sorted(set(ma) & set(mb)):
        x, y = ma[k], mb[k]
        if [n['pos'] for n in x['nodes']] != [n['pos'] for n in y['nodes']] or x['links'] != y['links']:
            mv = [dist(p['pos'], q['pos']) for p, q in zip(x['nodes'], y['nodes'])]
            changed.append(dict(name=k, bound=y['bound'], nodes=[len(x['nodes']), len(y['nodes'])],
                                links=[len(x['links']), len(y['links'])], max_move=round(max(mv or [0.0]), 4)))
    return dict(old_count=len(ma), new_count=len(mb), added=sorted(set(mb) - set(ma)),
                removed=sorted(set(ma) - set(mb)), changed=changed)


def match_points(a, b, tol, same):
    """greedy nearest pairs within tol: [(i, j)], unmatched i, unmatched j"""
    cand = sorted((dist(a[i]['pos'], b[j]['pos']), i, j) for i in range(len(a)) for j in range(len(b))
                  if dist(a[i]['pos'], b[j]['pos']) <= tol)
    ui, uj, pairs = set(), set(), []
    for d, i, j in cand:
        if i not in ui and j not in uj:
            ui.add(i)
            uj.add(j)
            pairs.append((i, j))
    return pairs, [i for i in range(len(a)) if i not in ui], [j for j in range(len(b)) if j not in uj]


def diff_points(a, b, tol, fields):
    pairs, ri, aj = match_points(a, b, tol, None)
    changed = []
    for i, j in pairs:
        x, y = a[i], b[j]
        diffs = [f for f in fields if x[f] != y[f]]
        d = dist(x['pos'], y['pos'])
        if diffs or d > MOVE_EPS:
            changed.append(dict(pos=vec(y['pos']), old_pos=vec(x['pos']), distance=round(d, 4),
                                fields={f: [x[f], y[f]] for f in diffs}))
    return dict(old_count=len(a), new_count=len(b), unchanged=len(pairs) - len(changed), changed=changed,
                added=[vec(b[j]['pos']) for j in aj], removed=[vec(a[i]['pos']) for i in ri])


def diff_lights(old, new, tol):
    ra = [dict(r, colour=vec(r['colour']), range=round(r['range'], 3)) for r in old.level.radiators()]
    rb = [dict(r, colour=vec(r['colour']), range=round(r['range'], 3)) for r in new.level.radiators()]
    return diff_points(ra, rb, tol, ('colour', 'range'))


def diff_sounds(old, new, tol):
    sa = [dict(s, radius_outer=round(s['radius_outer'], 3)) for s in old.level.map_sounds()]
    sb = [dict(s, radius_outer=round(s['radius_outer'], 3)) for s in new.level.map_sounds()]
    return diff_points(sa, sb, tol, ('sfx_id', 'radius_outer'))


# ---------------------------------------------------------------------------------------------------------------
# The whole comparison
# ---------------------------------------------------------------------------------------------------------------

def compare(old_path, new_path, old_label='old', new_label='new', tol=TOL):
    old, new = Side(old_path, old_label), Side(new_path, new_label)
    level = int(new.name, 16) if all(c in '0123456789abcdef' for c in new.name) else None
    rep = dict(level=new.name, level_name=pl.LEVEL_NAMES.get(level, ''), old=old.summary(), new=new.summary(),
               skipped=[])
    rep['subfiles'] = diff_entries(old, new)
    if old.level is None or new.level is None:
        rep['skipped'].append('a bundle could not be parsed: everything but the sub-file comparison')
        return rep, old, new
    try:
        ents = diff_entities(old, new)
    except Exception as e:
        ents = dict(geometry=[], collision=[], only_old=[], only_new=[], error=str(e))
    rep['entities'] = ents
    rep['textures'] = diff_textures(old, new)
    geom_changed = {(g['file'], g['entity']): g['change'] for g in ents['geometry']}
    if not (old.has_map and new.has_map):
        rep['skipped'].append('no map file in %s: dynamic placements, static cels, portals, paths, AI paths, '
                              'lights and sounds' % (old.label if not old.has_map else new.label))
        return rep, old, new
    rep['static'] = diff_cels(old, new, geom_changed)
    rep['dynamic'] = diff_dynamic(old, new, tol)
    rep['portals'] = diff_portals(old, new)
    rep['paths'] = diff_paths(old, new)
    rep['ai_paths'] = diff_ai_paths(old, new)
    rep['lights'] = diff_lights(old, new, tol)
    rep['sounds'] = diff_sounds(old, new, tol)
    return rep, old, new


def public(rep):
    """the report without the internal match lists"""
    if isinstance(rep, dict):
        return {k: public(v) for k, v in rep.items() if not k.startswith('_')}
    if isinstance(rep, list):
        return [public(v) for v in rep]
    return rep


# ---------------------------------------------------------------------------------------------------------------
# Markdown
# ---------------------------------------------------------------------------------------------------------------

def fmt_keys(kd):
    parts = []
    for k in kd:
        role = {'in': ', read', 'out': ', written', 'inout': ', read and written'}.get(k.get('channelRole'), '')
        parts.append('%s%s %s -> %s' % (k['name'], role, k['old'], k['new']))
    return '; '.join(parts)


def fmt_pos(p):
    return '(%.2f, %.2f, %.2f)' % tuple(p)


def short_keys(keys, limit=6):
    items = ['%s=%s' % kv for kv in keys.items()]
    return ', '.join(items[:limit]) + (', ...' if len(items) > limit else '')


def markdown(rep, cap=400):
    o, n = rep['old']['label'], rep['new']['label']
    L = ['# %s %s: %s against %s' % (rep['level'], rep['level_name'], o, n), '']
    L.append('- %s: `%s`, %d sub-files%s' % (o, os.path.basename(rep['old']['path']), rep['old']['entries'],
                                            ', %d missing (cut off)' % len(rep['old']['missing'])
                                            if rep['old']['missing'] else ''))
    L.append('- %s: `%s`, %d sub-files%s' % (n, os.path.basename(rep['new']['path']), rep['new']['entries'],
                                            ', %d missing (cut off)' % len(rep['new']['missing'])
                                            if rep['new']['missing'] else ''))
    for side in ('old', 'new'):
        for e in rep[side]['errors']:
            L.append('- %s could not be read: %s' % (rep[side]['label'], e))
    for s in rep['skipped']:
        L.append('- not compared: %s' % s)
    if rep['old']['missing']:
        c = collections.Counter(m['kind'] for m in rep['old']['missing'])
        L.append('- missing from %s: %s' % (o, ', '.join('%d %s' % (v, k) for k, v in sorted(c.items()))))
    L.append('')

    def lst(title, rows):
        if not rows:
            return
        L.append(title)
        L.append('')
        for r in rows[:cap]:
            L.append('- ' + r)
        if len(rows) > cap:
            L.append('- ... %d more in the JSON report' % (len(rows) - cap))
        L.append('')

    dy = rep.get('dynamic')
    if dy:
        L.append('## Dynamic placements')
        L.append('')
        L.append('%d in %s, %d in %s: %d unchanged, %d changed, %d added, %d removed. %d matches were ambiguous, '
                 '%d between identical placements.' % (dy['old_count'], o, dy['new_count'], n, dy['unchanged'],
                                                       len(dy['changed']), len(dy['added']), len(dy['removed']),
                                                       dy['ambiguous'], dy['interchangeable']))
        L.append('')
        cols = ('old', 'new', 'unchanged', 'moved', 'rotated', 'keys', 'glist', 'path', 'added', 'removed')
        rows = [(t, c) for t, c in dy['per_type'].items() if c.get('old', 0) != c.get('unchanged', 0) or
                c.get('new', 0) != c.get('unchanged', 0)]
        if rows:
            L.append('| type | ' + ' | '.join(cols) + ' |')
            L.append('|---' * (len(cols) + 1) + '|')
            for t, c in rows:
                L.append('| %s | %s |' % (t, ' | '.join(str(c.get(k, 0)) for k in cols)))
            L.append('')
        lst('### Added in %s' % n, ['%s at %s: %s' % (r['label'], fmt_pos(r['pos']), short_keys(r['keys']))
                                    for r in dy['added']])
        lst('### Removed (in %s only)' % o, ['%s at %s: %s' % (r['label'], fmt_pos(r['pos']), short_keys(r['keys']))
                                            for r in dy['removed']])
        mv = sorted([r for r in dy['changed'] if 'moved' in r['what'] or 'rotated' in r['what']],
                    key=lambda r: -r['distance'])
        lst('### Moved or rotated', ['%s (%s in %s): %s%s%s' % (
            r['label'], r['old_label'], o, 'moved %.2f from %s to %s' % (r['distance'], fmt_pos(r['old_pos']),
                                                                           fmt_pos(r['new_pos']))
            if 'moved' in r['what'] else 'in place', ', turned %.1f deg' % r['rotation'] if 'rotated' in r['what']
            else '', ' (ambiguous match)' if r['ambiguous'] is True else '') for r in mv])
        common = collections.Counter((r['type'], k['name'], str(k['old']), str(k['new']))
                                     for r in dy['changed'] for k in r['keys'])
        lst('### Key changes made to several placements', ['%s %s %s -> %s: %d placements' % (t, k, a, b, c)
                                                           for (t, k, a, b), c in common.most_common() if c >= 3])
        kc = [r for r in dy['changed'] if r['keys'] or 'glist' in r['what'] or 'path' in r['what']]
        lst('### Keys changed', ['%s (%s in %s): %s%s%s' % (
            r['label'], r['old_label'], o, fmt_keys(r['keys']),
            '; glist %s -> %s' % tuple(r['glist']) if 'glist' in r else '',
            '; path (frames, rate, index) %s -> %s' % tuple(r['path']) if 'path' in r else '') for r in kc])
        ch = []
        for c in dy['channels']:
            role = lambda r: {'in': 'reads', 'out': 'writes', 'inout': 'reads and writes'}.get(r, 'uses')
            parts = []
            if c['added']:
                parts.append('only in %s: %s' % (n, '; '.join('%s %s it (%s)' % (u['object'], role(u['role']), u['key'])
                                                         for u in c['added'])))
            if c['removed']:
                parts.append('only in %s: %s' % (o, '; '.join('%s %s it (%s)' % (u['object'], role(u['role']),
                                                                            u['key']) for u in c['removed'])))
            note = ''
            if c['new_users'] and not c['new_writers']:
                note = ' - no placement writes it in %s' % n
            ch.append('channel %d (%d users in %s, %d in %s): %s%s' % (c['channel'], c['old_users'], o, c['new_users'],
                                                                     n, ' | '.join(parts), note))
        lst('### Switch-channel wiring', ch)
    st = rep.get('static')
    if st:
        L.append('## Static world')
        L.append('')
        L.append('%d static cels (%d rooms) in %s, %d (%d rooms) in %s: %d unchanged in place, %d moved, %d added, '
                 '%d removed, %d with changed keys, %d whose geometry changed.' % (
                     st['old_count'], st['old_rooms'], o, st['new_count'], st['new_rooms'], n, st['unchanged'],
                     len(st['moved']), len(st['added']), len(st['removed']), len(st['keys']), len(st['geometry'])))
        L.append('')
        cel = lambda r: '%s %s/%s at %s' % ('room' if r['room'] else 'cel', r['file'], r['entity'], fmt_pos(r['centre']))
        lst('### Cels added in %s' % n, [cel(r) for r in st['added']])
        lst('### Cels removed (in %s only)' % o, [cel(r) for r in st['removed']])
        lst('### Cels moved', ['%s: moved %.2f, turned %.1f deg' % (cel(r), r['distance'], r['rotation'])
                               for r in st['moved']])
        lst('### Cels with changed keys', ['%s: %s -> %s' % (cel(r), r['old_keys'], r['new_keys']) for r in st['keys']])
    en = rep.get('entities')
    if en:
        g = en['geometry']
        lst('### Changed geometry (per entity)', ['%s/%s: %s%s' % (
            r['file'], r['entity'], r['change'], ', vertices %d -> %d, triangles %d -> %d' % (
                r['verts'][0], r['verts'][1], r['tris'][0], r['tris'][1]) if r['change'] == 'shape' else '')
            for r in g])
        lst('### Collision changed', ['%s/%s %s' % (r['file'], r['entity'], r['change']) for r in en['collision']])
        lst('### Entities only in %s (files both have)' % n, ['%s/%s' % (r['file'], r['entity']) for r in en['only_new']])
        lst('### Entities only in %s (files both have)' % o, ['%s/%s' % (r['file'], r['entity']) for r in en['only_old']])
    tx = rep.get('textures')
    if tx:
        lst('### Textures', ['added: %s (%s, %dx%d)' % (r['name'], r['pack'], r['size'][0], r['size'][1])
                             for r in tx['added']] +
            ['removed: %s (%s)' % (r['name'], r['pack']) for r in tx['removed']] +
            ['changed: %s (%dx%d -> %dx%d)' % (r['name'], r['old']['size'][0], r['old']['size'][1],
                                               r['new']['size'][0], r['new']['size'][1]) for r in tx['changed']])
    if rep.get('portals'):
        p = rep['portals']
        L.append('Portals: %d -> %d, %d of those in common changed.' % (p['old_count'], p['new_count'],
                                                                        p['changed_in_common']))
        L.append('')
    if rep.get('paths'):
        L.append('## Paths, AI paths, lights, sounds')
        L.append('')
        p, a = rep['paths'], rep['ai_paths']
        L.append('- object paths: %d -> %d, %d changed' % (p['old_count'], p['new_count'], len(p['changed'])))
        L.append('- AI paths: %d -> %d, %d added, %d removed, %d changed' % (
            a['old_count'], a['new_count'], len(a['added']), len(a['removed']), len(a['changed'])))
        for k, title in (('lights', 'radiators'), ('sounds', 'map sounds')):
            x = rep[k]
            L.append('- %s: %d -> %d, %d changed, %d added, %d removed' % (
                title, x['old_count'], x['new_count'], len(x['changed']), len(x['added']), len(x['removed'])))
        L.append('')
        lst('### AI paths', ['added: %s' % s for s in a['added']] + ['removed: %s' % s for s in a['removed']] +
            ['changed: %s (nodes %d -> %d, links %d -> %d, furthest node move %.2f)' % (
                r['name'], r['nodes'][0], r['nodes'][1], r['links'][0], r['links'][1], r['max_move'])
             for r in a['changed']])
    sf = rep['subfiles']
    L.append('## Sub-files')
    L.append('')
    L.append('| kind | same | trivial change | real change | only in %s | only in %s |' % (o, n))
    L.append('|---|---|---|---|---|---|')
    for k, c in sf['by_kind'].items():
        L.append('| %s | %d | %d | %d | %d | %d |' % (k, c['same'], c['trivial'], c['real'], c['only_old'],
                                                     c['only_new']))
    L.append('')
    lst('### Only in %s' % o, ['%s %s (%d bytes)' % (r['kind'], r['name'], r['len']) for r in sf['only_old']])
    lst('### Only in %s' % n, ['%s %s (%d bytes)' % (r['kind'], r['name'], r['len']) for r in sf['only_new']])
    real = [r for r in sf['changed'] if not r['trivial']]
    lst('### Real changes', ['%s %s: %d -> %d bytes%s' % (
        r['kind'], r['name'], r['old_len'], r['new_len'],
        ' (%s)' % ', '.join(r['changed']) if r.get('changed') else
        ' (%d bytes differ)' % r['bytes'] if 'bytes' in r else '') for r in real])
    return '\n'.join(L) + '\n'


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('-o', '--out', help='writes OUT.json and OUT.md')
    ap.add_argument('--old-label', default='old')
    ap.add_argument('--new-label', default='new')
    ap.add_argument('--tol', type=float, default=TOL, help='match distance for placements (default %g)' % TOL)
    a = ap.parse_args(argv)
    rep, _, _ = compare(a.old, a.new, a.old_label, a.new_label, a.tol)
    md = markdown(rep)
    if a.out:
        with open(a.out + '.json', 'w', newline='\n') as f:
            json.dump(public(rep), f, indent=1)
        with open(a.out + '.md', 'w', newline='\n', encoding='utf-8') as f:
            f.write(md)
        print('%s.json, %s.md' % (a.out, a.out))
    else:
        sys.stdout.write(md)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
