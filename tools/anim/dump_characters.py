"""Exports every character in the game as glTF, with the animations in shared per-skeleton libraries.

  python dump_characters.py [outdir]          (default: <repo>/dump_characters)

outdir/characters/<skin>_<name>.gltf (+ .bin)  every skin (hashcode type 0x05) in the game: skeleton, meshes,
                                                facial shape keys, rigid parts, attachment points, materials - no
                                                animations. Exported from the first level that holds the skin.
outdir/animations/skeleton_NN.gltf (+ .bin)   one per skeleton: the skeleton at its default pose, with every
                                                body sequence of that skeleton in the whole game (all levels,
                                                duplicates removed), one animation each, seq_<hashcode>.
outdir/animations/facial.gltf (+ .bin)        Bond's body with its 22 shape keys and every facial sequence in
                                                the game, face_<hashcode>.
outdir/textures/*.png                           every texture the characters use, written once.
outdir/index.md                                 what is where, and how to put a library animation on a character.

Duplicating every animation into every character would take about 42 GB (the 66 skins of the main human
skeleton share 785 sequences); shared, the whole dump is about a gigabyte. In Blender an action drives bones by
name and every character of a skeleton names its bones alike (bone_NN), so an action from a skeleton's library
plays on any character built on that skeleton.
"""
import collections
import glob
import os
import sys
import time
import zlib

import bundle
import decode_seq
import export_gltf
import parse_skeleton
import parse_skin
import parse_tex
from nfpaths import LVL, REPO


class AllSequences:
    """Every sequence in the game, deduplicated by hashcode (the first copy wins), shaped like export_gltf.Level
    for the library exporter."""

    def __init__(self):
        self.name = 'all levels'
        self.skel_table = parse_skeleton.load_table()
        paths = sorted(glob.glob(os.path.join(LVL, '07*.bin')))
        self.seq_skels = decode_seq.load_skeletons(paths[0])   # identical in every level (spec-load.md)
        self.body = collections.defaultdict(dict)
        self.facial = {}
        for fn in paths:
            d = open(fn, 'rb').read()
            for typ, name, off, ln in bundle.walk(d):
                n = name.lower()
                if not n.startswith('04'):
                    continue
                h = int(n[:8], 16)
                try:
                    q = decode_seq.Sequence(d[off:off + ln], n[:8])
                except Exception:
                    continue
                if q.isMorph:
                    self.facial.setdefault(h, q)
                else:
                    self.body[q.skelNum].setdefault(h, q)

    def sequences(self, skel_num, morph):
        src = self.facial if morph else self.body.get(skel_num, {})
        return sorted(src.items())


class TextureDir:
    """Writes each texture once, named after it and a checksum of its pixels, and returns its URI relative to the
    glTF files (which live one folder down)."""

    def __init__(self, root):
        self.dir = os.path.join(root, 'textures')
        os.makedirs(self.dir, exist_ok=True)
        self.written = {}

    def __call__(self, tex, w, h, px):
        key = (tex.name, w, h, zlib.crc32(px))
        if key not in self.written:
            name = '%s_%08x.png' % (parse_tex.safe(tex.name), key[3])
            path = os.path.join(self.dir, name)
            if not os.path.exists(path):
                with open(path, 'wb') as f:
                    f.write(export_gltf.png_bytes(w, h, px))
            self.written[key] = name
        return '../textures/' + self.written[key]


def character_name(lv, s, h):
    for hh in s['riggedHash'] + s['discreteHash']:
        f = lv.mesh_for(hh)
        if f is not None and f[1].name:
            return '%08x_%s' % (h, parse_tex.safe(f[1].name))
    return '%08x' % h


def library_skin(skel_num, skins, skel_table):
    """A skin-shaped dict for a skeleton's library: every bone any skin of this skeleton uses, with the parent
    those skins agree on, at the skeleton's default offsets (scale 1)."""
    nb = skel_table[skel_num]['numBones']
    bones = [0] * nb
    for s in skins:
        for b, v in enumerate(s['bones']):
            if v & 0x80:
                if bones[b] and bones[b] != v:
                    raise ValueError('skeleton %d bone %d: skins disagree on its parent (%02x, %02x)' % (
                        skel_num, b, bones[b], v))
                bones[b] = v
    for b in range(nb):   # a used bone's parent must be a joint too
        p = bones[b] & 0x7f
        if bones[b] and p != 0x7f and not bones[p] & 0x80:
            raise ValueError('skeleton %d bone %d has an unused parent %d' % (skel_num, b, p))
    return dict(hash=skel_num, scale=(1.0, 1.0, 1.0), numBones=nb, skel=skel_num, bones=bones, bind=[],
                riggedHash=[], discreteHash=[], discreteBone=[], datums=[], numMorphs=0, numDatums=0,
                numDiscrete=0, numRigged=0)


def main(argv):
    out = os.path.abspath(argv[0]) if argv else os.path.join(REPO, 'dump_characters')
    for sub in ('characters', 'animations'):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    textures = TextureDir(out)
    t0 = time.time()
    log_lines = []

    def log(msg):
        log_lines.append(msg.strip())

    # --- characters, each from the first level that holds it
    rows = []
    skins_by_skel = collections.defaultdict(list)
    done = set()
    for fn in sorted(glob.glob(os.path.join(LVL, '07*.bin'))):
        lv = None
        names = [n for t, n, o, l in bundle.walk(open(fn, 'rb').read()) if n.lower().startswith('05')]
        todo = [int(n[:8], 16) for n in names if int(n[:8], 16) not in done]
        if not todo:
            continue
        lv = export_gltf.Level(fn)
        for h in sorted(todo):
            done.add(h)
            s = lv.skin(h)
            skins_by_skel[s['skel']].append(s)
            name = character_name(lv, s, h)
            ex = export_gltf.Exporter(lv, h, log=lambda m, h=h: log('%08x: %s' % (h, m)), image_writer=textures)
            ex.shared_rest = True
            ex.run(anims=False)
            ex.g.write_gltf(os.path.join(out, 'characters', name + '.gltf'))
            meshes = [m['name'] for m in ex.g.j['meshes']]
            rows.append((name, s['skel'], lv.name, sum(1 for b in s['bones'] if b & 0x80), s['numMorphs'],
                         tuple(round(c, 2) for c in s['scale']), meshes))
            print('%-40s skeleton %2d, from %s, %d meshes' % (name, s['skel'], lv.name, len(meshes)), flush=True)

    # --- animation libraries
    allseq = AllSequences()
    libs = []
    for skel_num in sorted(skins_by_skel):
        seqs = allseq.sequences(skel_num, False)
        s = library_skin(skel_num, skins_by_skel[skel_num], allseq.skel_table)
        ex = export_gltf.Exporter(allseq, skel_num, log=lambda m, k=skel_num: log('skeleton %d: %s' % (k, m)), skin=s)
        n, _ = ex.run(facial=False)
        ex.g.j['nodes'][0]['name'] = 'skeleton_%02d' % skel_num
        ex.g.j['skins'][0]['name'] = 'skeleton_%02d' % skel_num
        path = os.path.join(out, 'animations', 'skeleton_%02d.gltf' % skel_num)
        ex.g.write_gltf(path)
        mb = os.path.getsize(path[:-5] + '.bin') / 1e6
        libs.append((skel_num, n, len(seqs), mb, [r[0] for r in rows if r[1] == skel_num]))
        print('skeleton_%02d: %d of %d sequences, %.1f MB' % (skel_num, n, len(seqs), mb), flush=True)

    # --- facial library: Bond's head carries the shape keys the actions drive
    lv = export_gltf.Level(os.path.join(LVL, '07000001.bin'))
    ex = export_gltf.Exporter(lv, 0x05000007, log=lambda m: log('facial: %s' % m), image_writer=textures)
    ex.shared_rest = True
    ex.run(anims=False)
    faces = 0
    for h, q in allseq.sequences(0, True):
        ex.facial_animation(h, q)
        faces += 1
    ex.g.write_gltf(os.path.join(out, 'animations', 'facial.gltf'))
    print('facial: %d sequences' % faces, flush=True)

    write_index(out, rows, libs, faces, log_lines)
    total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(out) for f in fs)
    print('%d characters, %d animation libraries, %d facial sequences, %d textures, %.2f GB, %.0f s' % (
        len(rows), len(libs), faces, len(os.listdir(textures.dir)), total / 1e9, time.time() - t0))
    return 0


def write_index(out, rows, libs, faces, log_lines):
    L = ['# Nightfire characters (glTF)', '',
         'Written by `tools/anim/dump_characters.py` (docs/anim/gltf-export.md). Coordinates are the game\'s: y up, '
         'characters facing +z, about a metre to a unit. Animations are authored at 60 frames a second.', '',
         '## Using a library animation on a character (Blender)', '',
         '1. Set the scene to 60 fps. Import the character (`characters/...gltf`) and the library of its skeleton '
         '(`animations/skeleton_NN.gltf`, see the table) - `blender --python tools/anim/blender_import.py -- '
         '<character> <library>` does both and widens the shape-key ranges.',
         '2. Select the character\'s armature; in the Dope Sheet\'s Action Editor pick an action `seq_<hashcode>` '
         '(and, in Blender 4.4 and later, its slot). Bones are matched by name, and every character of a skeleton '
         'names its bones alike.',
         '3. Facial animations (`face_<hashcode>`, in `animations/facial.gltf`) are shape-key actions: pick one '
         'for a head\'s shape keys in the Shape Key Editor. All heads have the same 22 shape keys.', '',
         'A library action plays each bone\'s rotation exactly. Translations are the skeleton\'s defaults, so a '
         'character whose own bind pose differs from them (by up to about a centimetre) keeps its own bone lengths '
         '- see docs/anim/gltf-export.md.', '',
         '## Animation libraries', '',
         '| Library | Sequences | Size | Characters |', '|---|---|---|---|']
    for k, n, total, mb, chars in libs:
        L.append('| animations/skeleton_%02d.gltf | %d%s | %.1f MB | %s |' % (
            k, n, '' if n == total else ' (of %d; the rest do not fit the skeleton)' % total, mb, ', '.join(chars)))
    L += ['| animations/facial.gltf | %d facial | | any head (22 shape keys) |' % faces, '',
          '## Characters', '', '| File | Skeleton | Level | Bones | Shape keys | Scale | Meshes |',
          '|---|---|---|---|---|---|---|']
    for name, sk, lvl, nb, morphs, scale, meshes in rows:
        L.append('| characters/%s.gltf | %d | %s | %d | %d | %s | %s |' % (
            name, sk, lvl, nb, morphs, scale[0] if len(set(scale)) == 1 else scale, ', '.join(meshes)))
    if log_lines:
        L += ['', '## Notes from the export', ''] + ['- ' + x for x in log_lines]
    with open(os.path.join(out, 'index.md'), 'w') as f:
        f.write('\n'.join(L) + '\n')


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
