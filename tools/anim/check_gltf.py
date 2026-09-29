"""Checks an export_gltf.py file in Blender: imports it (blender_probe.py), poses it at a frame of one of its
animations, and compares every skinned vertex with the game's own skinning of the same frame (pose_ref.py, the
maths psiBuildMatrixPalette uses, and the vertex shader's two-bone blend).

  python check_gltf.py <skin> <file.glb> <sequence hashcode> <frame> [--bundle 07000001] [--render out.png]
"""
import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

import export_gltf
import pose_ref

BLENDER = os.environ.get('BLENDER', r'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe')


def game_posed(lv, skin_hash, seq_hash, frame, face_hash=None):
    """{mesh name: [posed vertex]} computed the game's way, the root's translation included (as the object's
    movement would be)."""
    s = lv.skin(skin_hash)
    q = next(q for h, q in lv.sequences(s['skel'], False) if h == seq_hash)
    skel = lv.seq_skels[q.skelNum]
    trans, quats = q.frame(frame, skel, s['scale'])
    weights = None
    if face_hash is not None:
        fq = next(q for h, q in lv.sequences(s['skel'], True) if h == face_hash)
        weights = fq.frame(frame)
    ident = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    bind_t = [bt for bt, _ in s['bind']] if s['bind'] else None
    bone, pal = pose_ref.build_palette(s['bones'], trans, quats, ident, [0, 0, 0], bind_t)
    out = {}
    for rh in s['riggedHash']:
        found = lv.mesh_for(rh)
        if found is None or found[1].mesh.empty:
            continue
        ent = found[1]
        sw, _ = ent.mesh.skin_weights()
        vs = []
        targets = ent.mesh.morph_targets if weights is not None else []
        for i, v in enumerate(ent.mesh.vertices):
            pos = list(v['pos'])
            if targets and i < ent.mesh.morphVtxCnt:
                # the vertex shader: pos += sum over targets of delta * weight / 32767, before skinning
                for t, w in zip(targets, weights):
                    for k in range(3):
                        pos[k] += t[i][k] * w / 32767.0
            if sw[i] is None:
                vs.append(None)
                continue
            (ba, wa), (bb, wb) = sw[i]
            wa = min(max(wa, 0.0), 1.0)
            pa = pose_ref.xform(pal[ba][0], pal[ba][1], pos)
            pb = pose_ref.xform(pal[bb][0], pal[bb][1], pos)
            vs.append([wa * pa[k] + (1 - wa) * pb[k] for k in range(3)])
        out['%s_%08x' % (ent.name or 'body', ent.hash)] = vs
    # rigid parts: drawn with their bone's world matrix, the vertices being bone-local
    for i, dh in enumerate(s['discreteHash']):
        found = lv.mesh_for(dh)
        b = s['discreteBone'][i]
        if found is None or found[1].mesh.empty or bone[b] is None:
            continue
        ent = found[1]
        out['%s_%08x' % (ent.name or 'part', ent.hash)] = [pose_ref.xform(bone[b][0], bone[b][1], v['pos'])
                                                           for v in ent.mesh.vertices]
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('skin')
    ap.add_argument('glb')
    ap.add_argument('seq')
    ap.add_argument('frame', type=int)
    ap.add_argument('--bundle')
    ap.add_argument('--render')
    ap.add_argument('--face', help='also play this facial sequence on the shape keys')
    ap.add_argument('--library', help='take the animation from this library (dump_characters.py) instead')
    ap.add_argument('--no-guess', action='store_true', help="import with Blender's guess_original_bind_pose off")
    a = ap.parse_args(argv)
    h, sh = int(a.skin, 16), int(a.seq, 16)
    path = os.path.join(export_gltf.LVL, a.bundle + '.bin') if a.bundle else export_gltf.find_bundle(h)
    lv = export_gltf.Level(path)

    probe = os.path.join(tempfile.gettempdir(), 'nf_gltf_probe.json')
    cmd = [BLENDER, '-b', '--factory-startup', '--python', os.path.join(os.path.dirname(__file__), 'blender_probe.py'),
           '--', os.path.abspath(a.glb), probe, 'seq_%08x' % sh, str(a.frame), os.path.abspath(a.render) if a.render else '-']
    if a.face:
        cmd.append('face_%08x' % int(a.face, 16))
    env = dict(os.environ)
    if a.library:
        env['NF_PROBE_LIBRARY'] = os.path.abspath(a.library)
    env['NF_PROBE_GUESS'] = '0' if a.no_guess else '1'
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0 or not os.path.exists(probe):
        print(r.stdout[-3000:], r.stderr[-3000:])
        return 1
    res = json.load(open(probe))
    print('Blender: %d bones, %d materials, %d images, %d empties, %d actions' % (
        res['bones'], res['materials'], res['images'], res['empties'], len(res['actions'])))
    for m in res['meshes']:
        print('  mesh %s: %d verts, %d faces, %d shape keys, %d materials%s' % (
            m['name'], m['verts'], m['polys'], len(m['shape_keys']), len(m['materials']),
            ', on bone %s' % m['parent_bone'] if m['parent_bone'] else ''))
    print('action %s, frame range %s' % (res.get('action'), res.get('frame_range')))

    want = game_posed(lv, h, sh, a.frame, int(a.face, 16) if a.face else None)
    if a.face:
        print('face action %s' % res.get('face_action'))
    worst = 0.0
    compared = 0
    for name, vs in res.get('posed', {}).items():
        ref = want.get(name)
        if ref is None:
            print('  no game reference for Blender mesh %s' % name)
            continue
        # Blender's importer builds the mesh one primitive (material) at a time and may reorder vertices, so each
        # game vertex is matched with the Blender vertices at the same rest (bind) position
        errs, unmatched = [], 0
        ids = res.get('vids', {}).get(name)
        if ids is not None:
            # exact: Blender kept the game's vertex index as an attribute
            for bi, gi in enumerate(ids):
                if ref[gi] is not None:
                    errs.append(math.dist(vs[bi], ref[gi]))
            unmatched = sum(1 for gi in range(len(ref)) if ref[gi] is not None) - len(set(ids))
        else:
            # Blender's importer builds the mesh one primitive (material) at a time and may reorder vertices, so
            # each game vertex is matched with the Blender vertices at the same rest (bind) position
            rest = res['rest'][name]
            by_rest = {}
            for i, p in enumerate(rest):
                by_rest.setdefault(tuple(round(c, 5) for c in p), []).append(i)
            verts = lv.mesh_for(int(name[-8:], 16))[1].mesh.vertices
            for gi, q in enumerate(ref):
                if q is None:
                    continue
                cands = by_rest.get(tuple(round(c, 5) for c in verts[gi]['pos']))
                if not cands:
                    unmatched += 1
                    continue
                errs.append(min(math.dist(vs[i], q) for i in cands))
        if unmatched:
            print('  %s: %d game vertices not found in Blender - counted as a failure' % (name, unmatched))
            worst = float('inf')
        if not errs:
            print('  %s: nothing compared' % name)
            worst = float('inf')
            continue
        compared += len(errs)
        errs.sort()
        worst = max(worst, errs[-1])
        print('  %s: %d vertices, median error %.2e, max %.2e' % (name, len(errs), errs[len(errs) // 2], errs[-1]))
    print('RESULT: %s (%d vertices, worst %.2e)' % ('MATCH' if compared and worst < 1e-3 else 'MISMATCH', compared,
                                                    worst))
    return 0 if compared and worst < 1e-3 else 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
