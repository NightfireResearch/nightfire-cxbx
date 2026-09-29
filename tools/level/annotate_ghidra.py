"""Writes a plate comment on every placement Create function in default.xbe (through the Ghidra MCP server's HTTP
endpoint): which placement type(s) it makes, how parsemap_create_dynamic_objects calls it, its level_tag layout
(the level_tag_<Type> structures from make_ghidra_json.py) and what it looks like in the world.

  python annotate_ghidra.py [--dry-run]

An existing plate comment is kept: the new text is appended below it (and not added twice).
Note: the server ignores its own dry_run flag, so --dry-run here only prints.
"""
import json
import os
import sys
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.environ.get('GHIDRA_HTTP', 'http://127.0.0.1:8089')
PROGRAM = '/Xbox_EU/default.xbe'
MARK = 'PLACEMENT CREATE:'


def get(ep, **kw):
    kw['program'] = PROGRAM
    return json.loads(urllib.request.urlopen('%s/%s?%s' % (BASE, ep, urllib.parse.urlencode(kw))).read().decode())


def post(ep, body):
    body['program'] = PROGRAM
    req = urllib.request.Request('%s/%s' % (BASE, ep), data=json.dumps(body).encode(),
                                 headers={'Content-Type': 'application/json'})
    return json.loads(urllib.request.urlopen(req).read().decode())


def text_for(entries):
    lines = [MARK]
    for group, tid, t in entries:
        keys = ', '.join('k%d %s' % (f['key'], f['name']) for f in sorted(t.get('fields', []), key=lambda f: f['key']))
        vis = t.get('visual', {})
        vis = '%s: %s' % (vis.get('kind', '?'), vis.get('detail', '')) if isinstance(vis, dict) else str(vis)
        lines.append('Placement type %s (%s). Called: %s' % (tid, t['name'], t.get('call', '?')))
        lines.append('  level_tag layout: level_tag_%s%s' % (t['name'], ' - keys ' + keys if keys else ' (no keys)'))
        lines.append('  In the world: %s' % vis[:300])
        lines.append('  Confidence %s. docs/level/objects-%s.md' % (t.get('confidence', '?'), group))
    return '\n'.join(lines)


def main(argv):
    dry = '--dry-run' in argv
    by_addr = {}
    for group in ('actors', 'logic', 'dressing'):
        d = json.load(open(os.path.join(HERE, 'types_%s.json' % group)))
        for tid, t in d.items():
            if tid.startswith('_') or not isinstance(t, dict):
                continue
            addr = (t.get('create') or '').lower()
            if not addr.startswith('0x') or int(addr, 16) == 0:
                continue
            by_addr.setdefault(addr, []).append((group, tid, t))
    done = 0
    for addr, entries in sorted(by_addr.items()):
        new = text_for(entries)
        old = get('get_plate_comment', address=addr).get('comment') or ''
        if old.strip() == 'test':          # left by a probe of the endpoint
            old = ''
        if MARK in old:
            old = old[:old.index(MARK)].rstrip()
        comment = (old + '\n\n' + new) if old else new
        if dry:
            print(addr, comment[:200].replace('\n', ' | '))
            continue
        r = post('set_plate_comment', dict(address=addr, comment=comment))
        if r.get('status') != 'success':
            print('FAILED', addr, r)
        else:
            done += 1
    print('%d Create functions annotated%s' % (done, ' (dry run)' if dry else ''))


if __name__ == '__main__':
    main(sys.argv[1:])
