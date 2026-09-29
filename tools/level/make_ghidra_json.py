"""Writes driving-symbol-matching/results/structs/action-level.json for ghidra/NightfireStructs.py (run it on
default.xbe): the placement record, the generic level_tag every Create function receives, and one
level_tag_<Type> per placement type with its keys named (from types_*.json, the object research).

A level_tag is 0xB0 bytes: a 0x2C-byte header, then the placement's {key, value} parameters written as dwords at
0x2C + 4*key (parsemap_block_map_data_dynamic 0xa66f0).
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'driving-symbol-matching', 'results', 'structs', 'action-level.json')

TYPE_MAP = {'u32': 'uint', 's32': 'int', 'f32': 'float', 'bool': 'uint', 'hash': 'HASHCODE', 'switch': 'uint',
            'enum': 'uint', 'u16': 'ushort', 's16': 'short', 'u8': 'uchar', 's8': 'char'}

HEADER = [
    (0x02, 'ushort', 'pathFrames', 'placement +0x40: frame count of the path; the header is filled only when non-zero'),
    (0x04, 'ushort', 'pathRate', 'placement +0x42'),
    (0x08, 'void *', 'path', 'm_paths[placement +0x44] (a 0x19 block: 28-byte {vec3 pos, quat} frames)'),
]


def ident(s):
    s = re.sub(r'[^A-Za-z0-9_]', '_', s)
    return s if not s[:1].isdigit() else '_' + s


def main():
    types = [dict(name='TARGET_PLACEMENT', size=0x4c,
                  comment='A placement record (block 0x1a), 0x4C bytes followed by paramCount PlacementParams. '
                          'docs/level/spec-world.md.',
                  fields=[dict(offset=0x00, type='ushort', name='celEntityNum',
                               comment='entity index in the same file, used when glistHashcode is -1'),
                          dict(offset=0x04, type='HASHCODE', name='glistHashcode', comment='-1: pCelList[celEntityNum]'),
                          dict(offset=0x08, type='uint', name='placementType',
                               comment='0x8000 static cel (0xc047 room, 0xc023 underwater room, 0x8019 object, '
                                       '0x8017 water); 0x2000 without 0x8000 ignored; else Place_* object type'),
                          dict(offset=0x0c, type='_VECTOR', name='pos', comment=''),
                          dict(offset=0x18, type='_VECTOR', name='rot',
                               comment='Euler; static cels draw RotMatrixZYX, objects View_RotTransMatrix'),
                          dict(offset=0x24, type='quaternion_tag', name='quat',
                               comment='the INVERSE of the rot orientation (x y z w)'),
                          dict(offset=0x34, type='_VECTOR', name='scale',
                               comment='per-axis scale (AI volumes); (1,1,1) on every static'),
                          dict(offset=0x40, type='ushort', name='pathFrames', comment='non-zero: uses a path'),
                          dict(offset=0x42, type='ushort', name='pathRate', comment=''),
                          dict(offset=0x44, type='ushort', name='pathIndex', comment='index into m_paths'),
                          dict(offset=0x48, type='uint', name='paramCount', comment='PlacementParams that follow')]),
             dict(name='PlacementParam', size=8, comment='One {key, value} parameter of a placement record.',
                  fields=[dict(offset=0, type='uint', name='key', comment='value goes to level_tag +0x2C + 4*key'),
                          dict(offset=4, type='uint', name='value', comment='')]),
             dict(name='level_tag', size=0xb0,
                  comment='What every placement Create function receives (parsemap_block_map_data_dynamic 0xa66f0):'
                          ' zeroed, a 0x2C-byte header, then the placement parameters as dwords at 0x2C + 4*key. '
                          'Per-type layouts: level_tag_<Type>.',
                  fields=[dict(offset=o, type=t, name=n, comment=c) for o, t, n, c in HEADER] +
                         [dict(offset=0x2c, type='uint[33]', name='param', comment='param[key]')])]
    seen = set()
    for group in ('actors', 'logic', 'dressing'):
        d = json.load(open(os.path.join(HERE, 'types_%s.json' % group)))
        for tid, t in sorted(d.items(), key=lambda kv: kv[0]):
            if tid.startswith('_') or not isinstance(t, dict) or not t.get('fields'):
                continue
            name = 'level_tag_' + ident(t['name'])
            if name in seen:
                continue
            seen.add(name)
            fields = [dict(offset=o, type=ty, name=n, comment=c) for o, ty, n, c in HEADER]
            used = set()
            for f in sorted(t['fields'], key=lambda f: f['key']):
                off = 0x2c + 4 * int(f['key'])
                if off in used or off + 4 > 0xb0:
                    continue
                used.add(off)
                ty = TYPE_MAP.get(f.get('type', 'u32'), 'uint')
                note = f.get('note', '')
                if f.get('refers'):
                    note = '%s (refers: %s)' % (note, f['refers'])
                fields.append(dict(offset=off, type=ty, name=ident(f['name']), comment='key %d. %s' % (f['key'], note)))
            types.append(dict(name=name, size=0xb0, fields=fields,
                              comment='Placement type %s (%s) as %s reads it; confidence %s. %s' % (
                                  tid, t['name'], t.get('create_name', '?'), t.get('confidence', '?'),
                                  t.get('transform', ''))[:1000]))
    doc = dict(program='default.xbe', types=types, prototypes=[])
    with open(OUT, 'w') as f:
        json.dump(doc, f, indent=1)
    print('wrote %d types to %s' % (len(types), OUT))


if __name__ == '__main__':
    main()
