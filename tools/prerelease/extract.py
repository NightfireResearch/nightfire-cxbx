"""Finds, decompresses and names the level bundles in a cached_filesys.dat (docs/prerelease-cached-filesys.md).

  python extract.py CACHE OUTDIR [--lvl DIR] [--disc PATTERN]

- the files the cache's own build cached (index flag 1) whose stored bytes pass their CRC: the bundles among them
  are written as OUTDIR/<level>_pre.bin;
- every other EDL section that decompresses to a bundle (an earlier build's copy, found by content): written as
  OUTDIR/<level>_old_<cache offset>.bin. A section another one overwrote is decoded up to the first block it lost,
  and written as it is (bundle.trim() reads it);
- OUTDIR/manifest.json: one record per bundle, with its retail match, completeness and build group.

A bundle is named after the retail level (--lvl, default NF_ANIM_LVL) whose map file (entry type 1) has the same
name; failing that, the one whose directory overlaps it most. The build group is the size of its menu file
08000001, which differs between builds.
"""
import argparse
import json
import os
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'anim'))
import bundle         # noqa: E402
import cachefs        # noqa: E402
from nfpaths import DISC, LVL  # noqa: E402

BUNDLE_NAMES = ['%08x.bin' % h for h in range(0x07000000, 0x07001000)]
MENU_FILE = '08000001'


def directory(d):
    """(entries, complete) if d starts like a bundle, else None"""
    if len(d) < 0x20:
        return None
    try:
        ents, complete = bundle.walk_dir(d)
    except Exception:
        return None
    if len(ents) < 2 or not all(0 < len(n) < 32 and n.isalnum() for t, n, o, l in ents):
        return None
    return ents, complete


def retail_directories(lvl):
    out = {}
    for fn in sorted(os.listdir(lvl)):
        if fn.startswith('07') and fn.endswith('.bin'):
            with open(os.path.join(lvl, fn), 'rb') as f:
                d = f.read()
            out[fn[:8]] = [(t, n.lower()) for t, n, o, l in bundle.walk(d)]
    return out


def match_retail(ents, retail):
    names = set((t, n.lower()) for t, n, o, l in ents)
    maps = [n.lower() for t, n, o, l in ents if t == 1]
    best = max(retail, key=lambda k: len(names & set(retail[k])) / len(names | set(retail[k])))
    jac = {k: len(names & set(v)) / len(names | set(v)) for k, v in retail.items()}
    for k, v in retail.items():
        if maps and any(t == 1 and n == maps[-1] for t, n in v):
            return k, 'map file', jac[k]
    return best, 'directory', jac[best]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cache')
    ap.add_argument('out')
    ap.add_argument('--lvl', default=LVL, help='the retail bundles (extract_all.py)')
    ap.add_argument('--disc', default=DISC, help='filesys.dNN pattern, for the retail index')
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    size = os.path.getsize(a.cache)
    words, index = cachefs.read_index(a.cache)
    print('index: %d entries, boot count %d, CRC %s' % (len(index), words[2],
                                                         'valid' if cachefs.index_crc_ok(a.cache) else 'bad'))
    names = {cachefs.name_crc(n): n for n in BUNDLE_NAMES}
    _, retail_index = cachefs.read_index(a.disc % 0)
    retail_crc = {e.name_crc: e.data_crc for e in retail_index}
    retail = retail_directories(a.lvl)

    f = open(a.cache, 'rb')
    cached = {}
    for e in index:
        if e.flags & cachefs.FLAG_CACHED:
            f.seek(e.offset)
            if zlib.crc32(f.read(e.stored_len)) == e.data_crc:
                cached[e.offset] = e
    starts = cachefs.find_sections(f, size)
    print('%d files cached by this build, %d EDL sections' % (len(cached), len(starts)))

    manifest = []
    for i, s in enumerate(starts):
        limit = starts[i + 1] if i + 1 < len(starts) else size
        e = cached.get(s)
        if e is not None and e.name_crc not in names:
            continue
        first, _, _, _ = cachefs.read_section(f, s, limit, 1) if e is None else (None, 0, 0, True)
        if e is None and directory(first) is None:
            continue
        d, done, nblocks, complete = cachefs.read_section(f, s, limit)
        if e is not None:
            d = d[:e.length]
        dr = directory(d)
        if dr is None:
            continue
        ents, dir_complete = dr
        level, how, jac = match_retail(ents, retail)
        if e is not None:
            level, how = names[e.name_crc][:8], 'index entry'
        tag = 'pre' if e is not None else 'old_%08x' % s
        fn = '%s_%s.bin' % (level, tag)
        with open(os.path.join(a.out, fn), 'wb') as g:
            g.write(d)
        present = [x for x in ents if x[2] + x[3] <= len(d)]
        menu = [x[3] for x in ents if x[1].lower() == MENU_FILE]
        rec = dict(file=fn, level=level, build='prerelease' if e is not None else 'older', offset='0x%08x' % s,
                   matched_by=how, jaccard=round(jac, 4), blocks=done, blocks_total=nblocks, complete=complete,
                   directory_complete=dir_complete, entries=len(ents), entries_present=len(present),
                   size=len(d), menu_size=menu[0] if menu else None)
        if e is not None:
            rec['same_as_retail'] = retail_crc.get(e.name_crc) == e.data_crc
        manifest.append(rec)
        print('%s  %s  blocks %d/%d  entries %d/%d  %s %.3f  menu %s' % (
            rec['offset'], fn, done, nblocks, len(present), len(ents), how, jac, rec['menu_size']), flush=True)
    with open(os.path.join(a.out, 'manifest.json'), 'w', newline='\n') as g:
        json.dump(manifest, g, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
