"""Generates the weapon definition table (weapon_data, 0x18cfa0) from the XBE, as src/action/game/WeaponTable.inc.

    python tools/weapon_table.py            write src/action/game/WeaponTable.inc
    python tools/weapon_table.py --check    fail if it is out of date
    python tools/weapon_table.py --ghidra   write the struct for Ghidra (driving-symbol-matching/results/structs/
                                            action-weapons.json, applied by ghidra/NightfireStructs.py)

Needs disc/default.xbe. The table's contents are the game's once its static constructor WeaponDataTableInit
(0x000f5530) has run: entries 0-0x34 are initialised data in the XBE, the rest are 44 KB of stores in the constructor,
which this emulates (and which was checked byte for byte against a dump of the running game's table). Twelve of the
constructor's stores are computed from the frame rate rather than constant; those become code in the generated
WeaponTable_ApplyFrameRate, as the original computed them, not constants.

The fields are named and typed by weapon_definition_tag in src/action/game.h - rename or retype a field there and
regenerate. Each entry is commented with its weapon, from the user's WeaponData spreadsheet (dumped from the PS2
build; its weapon ids and names are the reference). docs/weapons.md describes the fields.
"""
import os
import re
import struct
import sys

import capstone
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_OP_REG

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
XBE = os.path.join(REPO, 'disc', 'default.xbe')
GAME_H = os.path.join(REPO, 'src', 'action', 'game.h')
OUT = os.path.join(REPO, 'src', 'action', 'game', 'WeaponTable.inc')
GHIDRA_JSON = os.path.join(REPO, 'driving-symbol-matching', 'results', 'structs', 'action-weapons.json')

TABLE, ENTRY, COUNT = 0x0018cfa0, 0x10c, 115
CTOR, CTOR_END = 0x000f5530, 0x00100469
FRAME_RATE_WORD, FRAME_RATE_FLOAT = 0x0017c0f4, 0x0017c0f8

# Each variant's weapon and fire mode, from the WeaponData spreadsheet's lookup columns (empty: none given there)
NAMES = [
    '',  # 0
    '',  # 1
    'Wolfram PP7, Semi',  # 2
    'Wolfram PP7, Silenced',  # 3
    'Wolfram PP7, Semi',  # 4
    'Wolfram PP7, Silenced',  # 5
    'Wolfram P2K, Semi',  # 6
    'Wolfram P2K, Silenced',  # 7
    'Wolfram P2K, Semi',  # 8
    'Wolfram P2K, Silenced',  # 9
    'Kowloon Type 40, Semi',  # 10
    'Kowloon Type 40, Burst',  # 11
    'Kowloon Type 80, Auto',  # 12
    'Kowloon Type 40, Semi',  # 13
    'Raptor Magnum, Semi',  # 14
    'Raptor Magnum, Semi',  # 15
    'Raptor Magnum .50, Semi',  # 16
    'Delta Repeater, Single',  # 17
    'Storm M32, Auto',  # 18
    'Storm M32, Semi',  # 19
    'Deutsche M9K, Silenced',  # 20
    'Deutsche M9K, Burst',  # 21
    'SG5 Commando, Burst',  # 22
    'SG5 Commando, Semi',  # 23
    'SG5 Commando, Burst',  # 24
    'SG5 Commando, Auto',  # 25
    'AIMS-20, Burst',  # 26
    'AIMS-20, Grenade',  # 27
    'Frinesi Auto 12, Pump',  # 28
    'Frinesi Auto 12, Auto',  # 29
    'Winter Tactical Sniper, Single',  # 30
    'Winter Tactical Sniper, Armour Piercing',  # 31
    'Winter Tactical Sniper, Single',  # 32
    'Winter Tactical Sniper, Armour Piercing',  # 33
    'Winter Tactical Sniper, Single',  # 34
    'Winter Tactical Sniper, Armour Piercing',  # 35
    'Winter Covert Sniper, Single',  # 36
    'Winter Covert Sniper, Armour Piercing',  # 37
    'Winter Covert Sniper, Single',  # 38
    'Winter Covert Sniper, Armour Piercing',  # 39
    'Winter Covert Sniper, Single',  # 40
    'Winter Covert Sniper, Armour Piercing',  # 41
    'Militek MGL, Grenade',  # 42
    'Militek MGL, Delay',  # 43
    'AT-420 Sentinel, Guided',  # 44
    'AT-420 Sentinel, Unguided',  # 45
    'AT-600 Scorpion, Heatseeker',  # 46
    'AT-600 Scorpion, Unguided',  # 47
    'Torpedo Launcher, Guided',  # 48
    'Torpedo Launcher, Unguided',  # 49
    'Phoenix Samurai, Overcharge',  # 50
    'Phoenix Samurai, Beam',  # 51
    'Frag Grenade, Grenade',  # 52
    'Stun Grenade, Grenade',  # 53
    'Smoke Grenade, Grenade',  # 54
    'Remote Mine, Grenade',  # 55
    'Remote Mine, Detonate',  # 56
    'Remote Mine, Detonate',  # 57
    'Laser Tripbomb',  # 58
    'Satchel Charge',  # 59
    'Satchel Charge',  # 60
    'Satchel Charge',  # 61
    'Satchel Charge',  # 62
    'Satchel Charge',  # 63
    'Satchel Charge',  # 64
    'Q-Pen',  # 65
    'Golden Gun, Single',  # 66
    'Korsakov K5, Dart',  # 67
    'Korsakov K5, Dart',  # 68
    "Oddjob's Hat",  # 69
    'Flare Gun',  # 70
    '',  # 71
    '',  # 72
    '',  # 73
    'Stunner',  # 74
    'Stunner',  # 75
    'Stunner',  # 76
    'Stunner',  # 77
    'Laser',  # 78
    'Laser',  # 79
    'Grapple',  # 80
    'Grapple',  # 81
    'Phoenix Ronin, Deploy',  # 82
    'Phoenix Ronin, Activate',  # 83
    'Micro-Camera',  # 84
    'Micro-Camera',  # 85
    'Decryptor',  # 86
    'Decryptor',  # 87
    'Q-Worm',  # 88
    'Shaver',  # 89
    'Shaver, Detonate',  # 90
    'Shaver',  # 91
    'Shaver, Detonate',  # 92
    '',  # 93
    '',  # 94
    '',  # 95
    '',  # 96
    '',  # 97
    '',  # 98
    '',  # 99
    '',  # 100
    '',  # 101
    '',  # 102
    '',  # 103
    '',  # 104
    'Smoke Grenade, Grenade',  # 105
    'Laser, Burst',  # 106
    '',  # 107
    'Satchel Charge',  # 108
    'AT-600 Scorpion, Heatseeker',  # 109
    'Laser, Burst',  # 110
    'Phoenix Samurai, Beam',  # 111
    '',  # 112
    '',  # 113
    '',  # 114
]


def read_xbe():
    x = open(XBE, 'rb').read()
    base, = struct.unpack_from('<I', x, 0x104)
    nsec, = struct.unpack_from('<I', x, 0x11c)
    sh, = struct.unpack_from('<I', x, 0x120)
    secs = []
    for s in range(nsec):
        o = sh - base + s * 0x38
        fl, va, vs, ra, rs, na = struct.unpack_from('<6I', x, o)
        secs.append((va, vs, ra, rs))

    def image(a, n):
        for va, vs, ra, rs in secs:
            if va <= a < va + vs:
                off = a - va
                return (x[ra + off:ra + min(off + n, rs)] + b'\0' * n)[:n]
        raise ValueError(hex(a))
    return image


def emulate(image):
    """The table after the constructor, and the computed stores: (entry, offset, size, kind, factor, divisor)"""
    mem = {}

    def read(a, n):
        b = bytearray(image(a, n))
        for i in range(n):
            if a + i in mem:
                b[i] = mem[a + i]
        return bytes(b)

    def write(a, data):
        assert TABLE <= a and a + len(data) <= TABLE + ENTRY * COUNT, 'a store outside the table at %#x' % a
        for i, v in enumerate(data):
            mem[a + i] = v

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    word = {}           # register -> (value, factor)
    fpu = []            # (value, factor, divisor)
    computed = []
    for ins in md.disasm(image(CTOR, CTOR_END - CTOR), CTOR):
        m, ops = ins.mnemonic, ins.operands
        if m in ('push', 'pop', 'ret') or ins.op_str == 'ebp, esp':
            continue
        if m == 'mov' and ops[0].type == X86_OP_MEM and ops[1].type == X86_OP_IMM:
            size = ops[0].size
            write(ops[0].mem.disp, (ops[1].imm & ((1 << (8 * size)) - 1)).to_bytes(size, 'little'))
        elif m == 'mov' and ops[0].type == X86_OP_MEM and ops[1].type == X86_OP_REG:
            a, size = ops[0].mem.disp, ops[0].size
            v, factor = word[ins.reg_name(ops[1].reg)[-2:][0]]
            write(a, (v & ((1 << (8 * size)) - 1)).to_bytes(size, 'little'))
            computed.append(((a - TABLE) // ENTRY, (a - TABLE) % ENTRY, size, 'word', factor, None))
        elif m == 'movzx':
            assert ops[1].mem.disp == FRAME_RATE_WORD and ops[1].size == 2
            word[ins.reg_name(ops[0].reg)[-2:][0]] = (int.from_bytes(read(FRAME_RATE_WORD, 2), 'little'), 1)
        elif m == 'imul':
            r = ins.reg_name(ops[0].reg)[-2:][0]
            v, f = word[ins.reg_name(ops[1].reg)[-2:][0]]
            word[r] = (v * ops[2].imm, f * ops[2].imm)
        elif m == 'fld':
            assert ops[0].mem.disp == FRAME_RATE_FLOAT
            fpu.append((struct.unpack('<f', read(FRAME_RATE_FLOAT, 4))[0], None, None))
        elif m == 'fmul':
            v, _, _ = fpu.pop()
            k = struct.unpack('<d', read(ops[0].mem.disp, 8))[0]
            fpu.append((v * k, k, None))
        elif m == 'fdiv':
            v, k, _ = fpu.pop()
            d = struct.unpack('<d', read(ops[0].mem.disp, 8))[0]
            fpu.append((v / d, k, d))
        elif m == 'fstp':
            v, k, d = fpu.pop()
            a = ops[0].mem.disp
            write(a, struct.pack('<f', v))
            computed.append(((a - TABLE) // ENTRY, (a - TABLE) % ENTRY, 4, 'float', k, d))
        else:
            raise SystemExit('WeaponDataTableInit: %s %s at %#x is not handled' % (m, ins.op_str, ins.address))
    return read(TABLE, ENTRY * COUNT), computed


TYPES = {   # C type -> (struct format, how it is written)
    'short': ('h', 'int'), 'ushort': ('H', 'int'), 'char': ('b', 'int'), 'int8_t': ('b', 'int'), 'uchar': ('B', 'int'),
    'bool': ('B', 'bool'), 'byte': ('B', 'int'), 'undefined': ('B', 'int'), 'undefined1': ('B', 'int'),
    'uint8_t': ('B', 'int'), 'int16_t': ('h', 'int'), 'uint16_t': ('H', 'int'), 'float': ('f', 'float'),
    'int': ('i', 'int'), 'int32_t': ('i', 'int'), 'uint': ('I', 'hex'), 'uint32_t': ('I', 'hex'), 'undefined4': ('I', 'hex'),
    'HASHCODE': ('I', 'enum'), 'Action_TranslatedText': ('I', 'enum'),
    'pointer': ('I', 'pointer'),   # a function pointer field: always null in the table, so never written
}


def struct_fields(with_comments=False):
    """weapon_definition_tag's fields from game.h: (offset, name, C type, count[, comment])"""
    h = open(GAME_H, encoding='utf-8').read()
    end = h.index('} weapon_definition_tag;')
    body = h[h.rindex('typedef struct', 0, end):end]
    body = body[body.index('{') + 1:]
    fields, off = [], 0
    for line in body.split('\n'):
        comment = line.split('//', 1)[1].strip() if '//' in line else ''
        comment = re.sub(r'^0x[0-9a-f]{3}\s*', '', comment)       # the offset, which Ghidra shows anyway
        line = re.sub(r'/\*.*?\*/', '', line).split('//')[0].strip()
        fp = re.match(r'\w+\s*\(\*\s*(\w+)\)\s*\(.*\);$', line)
        m = re.match(r'(\w+)\s+(\w+)(?:\[(\d+)\])?;$', line)
        if fp:
            t, name, n = 'pointer', fp.group(1), 1
        elif m:
            t, name, n = m.group(1), m.group(2), int(m.group(3) or 1)
        else:
            continue
        if t not in TYPES:
            raise SystemExit('weapon_definition_tag.%s: type %s is not known to tools/weapon_table.py' % (name, t))
        if with_comments:
            if t == 'pointer':
                comment = (line + ' ' + comment).strip()       # Ghidra gets a void *; keep the signature
            fields.append((off, name, t, n, comment))
        else:
            fields.append((off, name, t, n))
        off += struct.calcsize('<' + TYPES[t][0]) * n
    assert off == ENTRY, 'weapon_definition_tag adds up to %#x bytes, not %#x' % (off, ENTRY)
    return fields


def literal(t, v):
    kind = TYPES[t][1]
    if kind == 'float':
        for digits in range(6, 10):
            s = '%.*g' % (digits, v)
            if struct.pack('<f', float(s)) == struct.pack('<f', v):
                break
        if 'e' not in s and '.' not in s and 'inf' not in s and 'nan' not in s:
            s += '.0'
        return s + 'f'
    if kind == 'bool':
        return 'true' if v else 'false'
    if kind == 'enum':
        return '(%s)0x%08x' % (t, v)
    if kind == 'hex':
        return '0x%08x' % v
    return str(v)


def generate():
    image = read_xbe()
    data, computed = emulate(image)
    fields = struct_fields()
    by_offset = {o: name for o, name, t, n in fields}
    out = ['// THIS FILE IS GENERATED by tools/weapon_table.py from disc/default.xbe. DO NOT MODIFY.',
           '// The weapon definitions (weapon_data, 0x0018cfa0) as WeaponDataTableInit (0x000f5530) leaves them: every',
           '// non-zero field by name, an entry per weapon variant. Fields are those of weapon_definition_tag in game.h;',
           '// docs/weapons.md says what each one is. Entry names are from the WeaponData spreadsheet (PS2 dump).',
           '',
           'static const weapon_definition_tag WeaponTableData[%d] = {' % COUNT]
    for e in range(COUNT):
        base = e * ENTRY
        out.append('    {   // %d%s' % (e, ': ' + NAMES[e] if NAMES[e] else ''))
        for o, name, t, n in fields:
            fmt = '<' + TYPES[t][0]
            size = struct.calcsize(fmt)
            vals = [struct.unpack_from(fmt, data, base + o + i * size)[0] for i in range(n)]
            if not any(vals):
                continue
            assert TYPES[t][1] != 'pointer', 'entry %d: the function pointer %s is not null' % (e, name)
            if n == 1:
                out.append('        .%s = %s,' % (name, literal(t, vals[0])))
            else:
                out.append('        .%s = { %s },' % (name, ', '.join(literal(t, v) for v in vals)))
        out.append('    },')
    out.append('};')
    out.append('')
    out.append('// The constructor\'s stores that depend on the frame rate, computed as it computed them: a duration of whole')
    out.append('// seconds in frames (the frame rate word times the seconds), and per-second rates per frame (_FRAME_RATE times')
    out.append('// the rate over 60, in double precision as the x87 did). The table above holds them at 60 fps.')
    out.append('static void WeaponTable_ApplyFrameRate(weapon_definition_tag *table) {')
    out.append('    uint16_t frames = *(const uint16_t *)0x%08x;    // FRAME_RATE_INT\'s low word' % FRAME_RATE_WORD)
    out.append('    float frameRate = _FRAME_RATE;')
    for e, o, size, kind, k, d in computed:
        name = by_offset.get(o)
        assert name is not None, 'a computed store at entry %d +%#x is not at a field\'s start' % (e, o)
        if kind == 'word':
            out.append('    table[%d].%s = (int16_t)(frames * %d);' % (e, name, k))
        else:
            out.append('    table[%d].%s = (float)((double)frameRate * %r / %r);' % (e, name, k, d))
    out.append('}')
    return '\n'.join(out) + '\n'


GHIDRA_TYPES = {'int8_t': 'char', 'uint8_t': 'uchar', 'int16_t': 'short', 'uint16_t': 'ushort', 'int32_t': 'int',
                'uint32_t': 'uint', 'pointer': 'void *'}


def ghidra_json():
    """weapon_definition_tag for ghidra/NightfireStructs.py, and the table retyped as an array of it"""
    import json
    fields = []
    for o, name, t, n, comment in struct_fields(with_comments=True):
        if name.startswith('_pad'):
            continue                                    # left undefined, as padding is
        gt = GHIDRA_TYPES.get(t, t) + ('[%d]' % n if n > 1 else '')
        f = {'offset': o, 'type': gt, 'name': name}
        if comment:
            f['comment'] = comment
        fields.append(f)
    doc = {'program': 'default.xbe',
           'types': [{'name': 'weapon_definition_tag', 'size': ENTRY, 'fields': fields,
                      'comment': 'One weapon variant; weapon_data[115] at 0x0018cfa0. Generated from src/action/game.h '
                                 'by tools/weapon_table.py --ghidra; docs/weapons.md describes every field.'}],
           'data': [{'address': '0x%x' % TABLE, 'type': 'weapon_definition_tag[%d]' % COUNT, 'name': 'weapon_data'}]}
    return json.dumps(doc, indent=1) + '\n'


def main():
    if '--ghidra' in sys.argv:
        open(GHIDRA_JSON, 'w', encoding='utf-8', newline='\n').write(ghidra_json())
        print('wrote', os.path.relpath(GHIDRA_JSON, REPO))
        return
    text = generate()
    if '--check' in sys.argv:
        current = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if current.replace('\r\n', '\n') != text:
            sys.exit('%s is out of date: run python tools/weapon_table.py' % os.path.relpath(OUT, REPO))
        print('up to date')
        return
    open(OUT, 'w', encoding='utf-8', newline='\n').write(text)
    print('wrote', os.path.relpath(OUT, REPO))


if __name__ == '__main__':
    main()
