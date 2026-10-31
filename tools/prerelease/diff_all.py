"""Compares every bundle extract.py found with the next build of its level, and that with retail.

  python diff_all.py BUNDLES OUT [--lvl DIR] [--blender EXE] [--only 07000005,...]

For each level the builds are chained oldest first: the earlier builds' copies, then the pre-release build's,
then retail (--lvl, default NF_ANIM_LVL). Earlier copies of one level are ordered by how many of their sub-files
are identical to the pre-release copy (or to retail when there is none): fewest first. Each step writes

    OUT/reports/<level>_<from>_vs_<to>.json and .md      tools/level/diff_level.py
    OUT/glb/<level>_<from>_vs_<to>.glb                   tools/level/export_level_diff.py (levels with a map file)
    OUT/renders/<level>_<from>_vs_<to>_top.png           tools/level/blender_render_level.py, with --blender

and OUT/summary.json holds every step's headline counts.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEVEL_TOOLS = os.path.join(HERE, '..', 'level')
sys.path.insert(0, LEVEL_TOOLS)
sys.path.insert(0, os.path.join(HERE, '..', 'anim'))
import bundle                  # noqa: E402
import diff_level as dl        # noqa: E402
import export_level_diff as ed  # noqa: E402
from nfpaths import LVL        # noqa: E402


def entries(path):
    d = bundle.trim(open(path, 'rb').read())[0]
    return {(t, n.lower()): d[o:o + l] for t, n, o, l in bundle.walk(d)}


def chains(manifest, bundles, lvl):
    by_level = {}
    for r in manifest:
        by_level.setdefault(r['level'], []).append(r)
    out = {}
    for level, recs in sorted(by_level.items()):
        pre = [r for r in recs if r['build'] == 'prerelease']
        old = [r for r in recs if r['build'] == 'older']
        retail = os.path.join(lvl, level + '.bin')
        ref = entries(os.path.join(bundles, pre[0]['file']) if pre else retail)
        def same(r):
            e = entries(os.path.join(bundles, r['file']))
            return sum(1 for k, v in e.items() if ref.get(k) == v)
        old.sort(key=same)
        steps = [(os.path.join(bundles, r['file']), 'old_%s' % r['offset'][2:], 'older build (0x%s)' % r['offset'][2:])
                 for r in old]
        steps += [(os.path.join(bundles, r['file']), 'pre', 'pre-release') for r in pre]
        steps.append((retail, 'retail', 'retail'))
        out[level] = steps
    return out


def headline(rep):
    h = dict(level=rep['level'], name=rep['level_name'], old=rep['old']['label'], new=rep['new']['label'],
             skipped=rep['skipped'], missing=len(rep['old']['missing']))
    sf = rep['subfiles']
    h['subfiles'] = dict(only_old=len(sf['only_old']), only_new=len(sf['only_new']),
                         real=sum(1 for c in sf['changed'] if not c['trivial']),
                         trivial=sum(1 for c in sf['changed'] if c['trivial']), same=sf['same'])
    if 'dynamic' in rep:
        dy = rep['dynamic']
        h['dynamic'] = dict(old=dy['old_count'], new=dy['new_count'], unchanged=dy['unchanged'],
                            changed=len(dy['changed']), added=len(dy['added']), removed=len(dy['removed']),
                            ambiguous=dy['ambiguous'], interchangeable=dy['interchangeable'],
                            moved=sum(1 for c in dy['changed'] if 'moved' in c['what']),
                            keys=sum(1 for c in dy['changed'] if c['keys']),
                            per_type={t: c for t, c in dy['per_type'].items()
                                      if c.get('old', 0) != c.get('unchanged', 0) or
                                      c.get('new', 0) != c.get('unchanged', 0)},
                            channels=len(dy['channels']))
        st = rep['static']
        h['static'] = dict(old=st['old_count'], new=st['new_count'], rooms=[st['old_rooms'], st['new_rooms']],
                           added=len(st['added']), removed=len(st['removed']), moved=len(st['moved']),
                           keys=len(st['keys']), geometry=len(st['geometry']))
        h['ai_paths'] = dict(added=len(rep['ai_paths']['added']), removed=len(rep['ai_paths']['removed']),
                             changed=len(rep['ai_paths']['changed']))
        h['paths'] = dict(old=rep['paths']['old_count'], new=rep['paths']['new_count'],
                          changed=len(rep['paths']['changed']))
        h['lights'] = {k: len(rep['lights'][k]) for k in ('added', 'removed', 'changed')}
        h['portals'] = [rep['portals']['old_count'], rep['portals']['new_count'], rep['portals']['changed_in_common']]
    if 'entities' in rep:
        h['geometry'] = sum(1 for g in rep['entities']['geometry'] if g['change'] in ed.SHAPE_CHANGES)
        h['textures'] = {k: len(rep['textures'][k]) for k in ('added', 'removed', 'changed')}
    return h


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bundles', help="extract.py's output folder (with manifest.json)")
    ap.add_argument('out')
    ap.add_argument('--lvl', default=LVL)
    ap.add_argument('--blender', help='blender executable, for the top-down renders')
    ap.add_argument('--only', help='comma-separated level hashcodes')
    a = ap.parse_args(argv)
    manifest = json.load(open(os.path.join(a.bundles, 'manifest.json')))
    for sub in ('reports', 'glb', 'renders'):
        os.makedirs(os.path.join(a.out, sub), exist_ok=True)
    summary = []
    for level, steps in chains(manifest, a.bundles, a.lvl).items():
        if a.only and level not in a.only.split(','):
            continue
        for (pa, ta, la), (pb, tb, lb) in zip(steps, steps[1:]):
            stem = '%s_%s_vs_%s' % (level, ta, tb)
            rep, old, new = dl.compare(pa, pb, la, lb)
            with open(os.path.join(a.out, 'reports', stem + '.json'), 'w', newline='\n') as f:
                json.dump(dl.public(rep), f, indent=1)
            with open(os.path.join(a.out, 'reports', stem + '.md'), 'w', newline='\n', encoding='utf-8') as f:
                f.write(dl.markdown(rep))
            h = headline(rep)
            h['stem'] = stem
            if 'static' in rep:
                glb = os.path.join(a.out, 'glb', stem + '.glb')
                ex = ed.DiffExporter(rep, old, new)
                ex.run()
                ex.g.write_glb(glb)
                h['glb'] = glb
                if a.blender:
                    subprocess.run([a.blender, '-b', '--factory-startup', '--python',
                                    os.path.join(LEVEL_TOOLS, 'blender_render_level.py'), '--', glb,
                                    os.path.join(a.out, 'renders', stem)], capture_output=True)
                    h['render'] = os.path.join(a.out, 'renders', stem + '_top.png')
            summary.append(h)
            print(stem, json.dumps({k: h[k] for k in ('dynamic', 'static', 'subfiles') if k in h})[:300], flush=True)
    with open(os.path.join(a.out, 'summary.json'), 'w', newline='\n') as f:
        json.dump(summary, f, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
