"""Exports the differences between two builds of a level (diff_level.py) as one binary glTF scene.

  python export_level_diff.py OLD.bin NEW.bin -o diff.glb [--old-label pre-release] [--new-label retail]
                              [--report OUT_PREFIX] [--tol 4.0]

One root node "diff_<level>", then a group per kind of difference, each holding the export_level.py groups
(rooms, static, objects/<Type>, characters/<Type>, volumes/<Type>, markers/<Type>, paths):

| Group | Colour | What |
|---|---|---|
| unchanged | grey | NEW's static cels and objects that match OLD's in place, keys and geometry |
| added in NEW | green | NEW's cels, objects, object paths and AI paths that OLD does not have |
| removed from OLD | red | OLD's that NEW does not have |
| moved or changed | yellow | NEW's matched objects and cels that moved, turned or changed keys, where NEW has them |
| moved or changed/OLD positions | pale yellow, translucent | the same, where OLD had them, with a line to NEW's |
| changed geometry/NEW, changed geometry/OLD | cyan, magenta | both versions of each cel whose entity's shape changed |

Every node keeps export_level.py's properties; changed ones add "diff" (what changed) and "diffKeys" (the keys,
old -> new). The root's extras hold the counts. Everything is drawn untextured in its group's colour.
blender_import_level.py turns the groups into collections; blender_render_level.py renders the comparison.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'anim'))

import diff_level as dl            # noqa: E402
import export_level as el          # noqa: E402

GREY = (0.55, 0.55, 0.55, 1.0)
GREEN = (0.1, 0.85, 0.2, 1.0)
RED = (0.95, 0.15, 0.1, 1.0)
YELLOW = (1.0, 0.85, 0.0, 1.0)
GHOST = (1.0, 0.95, 0.5, 0.35)
CYAN = (0.0, 0.8, 0.9, 1.0)
MAGENTA = (0.9, 0.2, 0.9, 1.0)
SHAPE_CHANGES = ('shape', 'mesh added', 'mesh removed')


class DiffExporter:
    def __init__(self, rep, old, new, characters=True):
        self.rep, self.old, self.new = rep, old, new
        self.characters = characters
        self.en = el.LevelExporter(new.data, name=new.name, textures=False, log=lambda *a: None)
        self.eo = el.LevelExporter(old.data, name=old.name, textures=False, log=lambda *a: None, gltf=self.en.g)
        self.g = self.en.g
        self.tinted = {}
        self.colour_materials = {}
        self.added = 'added in %s' % new.label
        self.removed = 'removed from %s' % old.label
        self.changed = 'moved or changed'
        self.ghosts = (self.changed, '%s positions' % old.label)

    # --- colours
    def colour_material(self, rgba):
        if rgba not in self.colour_materials:
            m = dict(name='diff_%02x%02x%02x' % tuple(int(c * 255) for c in rgba[:3]), doubleSided=True,
                     pbrMetallicRoughness=dict(baseColorFactor=list(rgba), metallicFactor=0.0, roughnessFactor=1.0))
            if rgba[3] < 1.0:
                m['alphaMode'] = 'BLEND'
            self.colour_materials[rgba] = self.g.add('materials', m)
        return self.colour_materials[rgba]

    def tint(self, node, rgba):
        """every mesh under node drawn in one colour (a copy of the mesh, sharing its vertex data)"""
        if node is None:
            return
        n = self.g.j['nodes'][node]
        if 'mesh' in n:
            key = (n['mesh'], rgba)
            if key not in self.tinted:
                src = self.g.j['meshes'][n['mesh']]
                prims = []
                for p in src['primitives']:
                    q = dict(p, material=self.colour_material(rgba),
                             attributes={k: v for k, v in p['attributes'].items() if k != 'COLOR_0'})
                    prims.append(q)
                self.tinted[key] = self.g.add('meshes', dict(name=src.get('name', 'mesh'), primitives=prims))
            n['mesh'] = self.tinted[key]
        for c in n.get('children', []):
            self.tint(c, rgba)

    def mark(self, node, what, keys=None):
        if node is None:
            return
        ex = self.g.j['nodes'][node].setdefault('extras', {})
        ex['diff'] = what
        if keys:
            ex['diffKeys'] = '; '.join('%s: %s -> %s' % (k['name'], k['old'], k['new']) for k in keys)

    # --- the scene
    def run(self):
        st, dy = self.rep.get('static'), self.rep.get('dynamic')
        summary = dict(level=self.rep['level'], old=self.old.label, new=self.new.label)
        if dy:
            summary.update(objectsUnchanged=dy['unchanged'], objectsChanged=len(dy['changed']),
                           objectsAdded=len(dy['added']), objectsRemoved=len(dy['removed']))
        if st:
            summary.update(celsAdded=len(st['added']), celsRemoved=len(st['removed']), celsMoved=len(st['moved']),
                           celsGeometry=len(st['geometry']))
        self.root = self.g.node(name='diff_%s' % self.rep['level'], extras=summary)
        self.g.j['scenes'][0]['nodes'].append(self.root)
        self.en.root = self.eo.root = self.root
        self.eo.groups = self.en.groups
        for name in ('unchanged', self.added, self.removed, self.changed):
            self.en.group(name)
        if st:
            self.static(st)
        if dy:
            self.dynamic(dy)
        self.paths()
        return summary

    def static(self, st):
        a, b = self.eo.level.static_cels(), self.en.level.static_cels()
        geom = {(g['file'], g['entity']): g['change'] for g in self.rep['entities']['geometry']}
        for i, j in st['_pairs']:
            ca, cb = a[i], b[j]
            key = (cb.file.name, cb.entity.name or '#%d' % cb.entity.index)
            d = dl.dist(ca.pos, cb.pos)
            r = dl.angle_diff(ca.rot, cb.rot)
            if geom.get(key) in SHAPE_CHANGES:
                self.tint(self.eo.export_cel(ca, prefix=('changed geometry', self.old.label)), MAGENTA)
                node = self.en.export_cel(cb, prefix=('changed geometry', self.new.label))
                self.tint(node, CYAN)
                self.mark(node, geom[key])
            elif d > dl.MOVE_EPS or r > dl.ROT_EPS or ca.placement.keys != cb.placement.keys:
                node = self.en.export_cel(cb, prefix=(self.changed,))
                self.tint(node, YELLOW)
                self.mark(node, 'moved %.2f, turned %.1f deg' % (d, r * 57.29578) if d > dl.MOVE_EPS or
                          r > dl.ROT_EPS else 'keys')
                if d > dl.MOVE_EPS or r > dl.ROT_EPS:
                    self.tint(self.eo.export_cel(ca, prefix=self.ghosts), GHOST)
                    self.move_line(ca.centre, cb.centre, 'cel %s' % key[1])
            else:
                self.tint(self.en.export_cel(cb, prefix=('unchanged',)), GREY)
        for j in st['_added']:
            self.tint(self.en.export_cel(b[j], prefix=(self.added,)), GREEN)
        for i in st['_removed']:
            self.tint(self.eo.export_cel(a[i], prefix=(self.removed,)), RED)

    def sky_slots(self, objs):
        slots = {}
        for plc, r in objs:
            if plc.type & 0xff == 42:
                slots[plc.dword(0)] = plc
        return slots

    def dynamic(self, dy):
        oa, ob = self.eo.level.dynamic_objects(), self.en.level.dynamic_objects()
        sa, sb = self.sky_slots(oa), self.sky_slots(ob)
        xa, xb = dl.placement_extras(self.old, oa), dl.placement_extras(self.new, ob)
        matched_a, matched_b = set(), set()
        for i, j, amb in dy['_pairs']:
            matched_a.add(i)
            matched_b.add(j)
            (pa, (fa, ea)), (pb, (fb, eb)) = oa[i], ob[j]
            d = dl.dist(pa.pos, pb.pos)
            r = dl.angle_diff(pa.rot, pb.rot)
            kd = dl.key_diff(pa, pb)
            other = xa[i] != xb[j]
            moved = d > dl.MOVE_EPS or r > dl.ROT_EPS
            if not (moved or kd or other):
                self.tint(self.en.export_object(pb, fb, eb, self.characters, sb, prefix=('unchanged',)), GREY)
                continue
            node = self.en.export_object(pb, fb, eb, self.characters, sb, prefix=(self.changed,))
            self.tint(node, YELLOW)
            what = []
            if d > dl.MOVE_EPS:
                what.append('moved %.2f' % d)
            if r > dl.ROT_EPS:
                what.append('turned %.1f deg' % (r * 57.29578))
            if kd:
                what.append('keys')
            if other:
                what.append('glist or path')
            self.mark(node, ', '.join(what) + (' (ambiguous match)' if amb is True else ''), kd)
            if moved:
                ghost = self.eo.export_object(pa, fa, ea, self.characters, sa, prefix=self.ghosts)
                self.tint(ghost, GHOST)
                self.move_line(pa.pos, pb.pos, dl.plc_label(pb))
        for j, (pb, (fb, eb)) in enumerate(ob):
            if j not in matched_b:
                self.tint(self.en.export_object(pb, fb, eb, self.characters, sb, prefix=(self.added,)), GREEN)
        for i, (pa, (fa, ea)) in enumerate(oa):
            if i not in matched_a:
                self.tint(self.eo.export_object(pa, fa, ea, self.characters, sa, prefix=(self.removed,)), RED)

    def move_line(self, a, b, name):
        mi = self.en.line_mesh('move', [(tuple(a), tuple(b))], YELLOW[:3])
        if mi is not None:
            self.en.add(self.en.group(self.changed, 'moves'), name='move %s' % name, mesh=mi)

    def paths(self):
        """object paths by index and AI paths by name: NEW's added or changed ones green, OLD's removed or
        changed ones red"""
        rep = self.rep
        if 'paths' not in rep:
            return
        changed = set(c['index'] for c in rep['paths']['changed'])
        pa, pb = self.eo.level.paths(), self.en.level.paths()
        for idx, frames, side, grp, col in [(i, f, self.en, self.added, GREEN) for i, f in enumerate(pb)
                                            if i in changed or i >= len(pa)] + \
                                           [(i, f, self.eo, self.removed, RED) for i, f in enumerate(pa)
                                            if i in changed or i >= len(pb)]:
            segs = [(frames[k]['pos'], frames[k + 1]['pos']) for k in range(len(frames) - 1)]
            mi = side.line_mesh('path_%d' % idx, segs, col[:3])
            if mi is not None:
                side.add(side.group(grp, 'paths', 'object paths'), name='path %d' % idx, mesh=mi)
        ai = rep['ai_paths']
        want_new = set(ai['added']) | set(c['name'] for c in ai['changed'])
        want_old = set(ai['removed']) | set(c['name'] for c in ai['changed'])
        for side, want, grp, col in ((self.en, want_new, self.added, GREEN), (self.eo, want_old, self.removed, RED)):
            for p in (side.level.ai_paths() or {}).get('paths', []):
                if p['name'] not in want:
                    continue
                idx = [n['pos'] for n in p['nodes']]
                segs = [(idx[x], idx[y]) for x, y in p['links'] if x < len(idx) and y < len(idx)]
                mi = side.line_mesh('ai_%s' % p['name'], segs, col[:3])
                if mi is not None:
                    side.add(side.group(grp, 'paths', 'AI bounds' if p['bound'] else 'AI routes'),
                             name=p['name'] or 'ai', mesh=mi)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--old-label', default='old')
    ap.add_argument('--new-label', default='new')
    ap.add_argument('--report', help='also write diff_level.py\'s REPORT.json and REPORT.md')
    ap.add_argument('--tol', type=float, default=dl.TOL)
    ap.add_argument('--no-characters', action='store_true', help='drones as empties instead of their meshes')
    a = ap.parse_args(argv)
    rep, old, new = dl.compare(a.old, a.new, a.old_label, a.new_label, a.tol)
    if a.report:
        with open(a.report + '.json', 'w', newline='\n') as f:
            json.dump(dl.public(rep), f, indent=1)
        with open(a.report + '.md', 'w', newline='\n', encoding='utf-8') as f:
            f.write(dl.markdown(rep))
    if 'static' not in rep:
        print('%s: %s - no scene written' % (rep['level'], '; '.join(rep['skipped'])))
        return 1
    ex = DiffExporter(rep, old, new, characters=not a.no_characters)
    summary = ex.run()
    ex.g.write_glb(a.out)
    print('%s: %s, %.1f MB' % (a.out, ', '.join('%s %s' % kv for kv in summary.items()),
                               os.path.getsize(a.out) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
