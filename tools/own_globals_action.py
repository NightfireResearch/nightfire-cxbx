#!/usr/bin/env python3
"""Turns the action engine's address macros into globals of our own, for the build that loads no XBE.

A global no live game code touches (tools/global_coverage.py: "ready to own") and whose bytes in the XBE are all zero
can be an ordinary zero-initialised definition. Each one's

    #define Name U32_AT(0x00279284)            // a comment

becomes, in a .cpp,

    // XBE_GLOBAL(0x00279284, 0x4)
    uint32_t Name;            // a comment

and in a header `extern uint32_t Name;`, with the tagged definition in the .cpp of the same name beside it. The size
in the tag is the one global_coverage.py worked out. The all-original runs point the originals at it through the DLL's
map (src/action/devtools/AllOriginal.cpp); `relocate` would do so in every run, but needs the definition declared in
a header the injector includes, which a file's own global is not.

A global with initial data in the XBE (--initial) gets them as its initialiser, read from disc/default.xbe: a scalar
(`FLOAT_AT`, `I32_AT` and the like) its value, a float written with the fewest digits that give back the same bits, and
a `char` array its text when the bytes are a string and zeros. Anything else is left for a definition by hand.

    python tools/own_globals_action.py --list     # what it would do
    python tools/own_globals_action.py            # do it
    python tools/own_globals_action.py Name ...   # only these
    python tools/own_globals_action.py --initial  # the globals with initial data instead
    python tools/own_globals_action.py --into=src/action/game/drone/DroneInit.cpp ...   # a header's definitions there
"""
import os
import re
import struct
import subprocess
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

AT_TYPES = {'I32_AT': 'int32_t', 'I16_AT': 'int16_t', 'I8_AT': 'int8_t', 'U32_AT': 'uint32_t',
            'U16_AT': 'uint16_t', 'U8_AT': 'uint8_t', 'FLOAT_AT': 'float', 'PTR_AT': 'void *'}
ADDRESS = r'\(?\s*(0x[0-9a-fA-F]+)\s*\)?'
DEFINE = re.compile(r'^(\s*)#\s*define\s+(\w+)\s+(.*?)\s*(//.*)?$')
FORMS = [
    # U32_AT(0x...)
    (re.compile(r'^(\w+_AT)\s*\(\s*(0x[0-9a-fA-F]+)\s*\)$'), lambda m: (AT_TYPES.get(m.group(1)), '', m.group(2))),
    # (*(T(*)[n])0x...) and (*(T *(*)[n])0x...)
    (re.compile(r'^\(\s*\*\s*\(\s*([\w\s]+?)\s*(\**)\s*\(\s*\*\s*\)\s*(\[[^\]]+\](?:\[[^\]]+\])*)\s*\)\s*' + ADDRESS +
                r'\s*\)$'),
     lambda m: (m.group(1) + (' ' + m.group(2) if m.group(2) else ''), m.group(3), m.group(4))),
    # (*(T*)0x...), (*((T*)0x...)), (*(T**)(0x...))
    (re.compile(r'^\(\s*\*\s*\(?\s*\(\s*([\w\s]+?)\s*(\*+)\s*\)\s*' + ADDRESS + r'\s*\)?\s*\)$'),
     lambda m: (m.group(1) + (' ' + m.group(2)[1:] if len(m.group(2)) > 1 else ''), '', m.group(3))),
]


def ready(initial=False):
    """[(name, address, size, path, line)] for the globals global_coverage.py calls ready to own, with no initial data
    (or only those with)"""
    out = subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'global_coverage.py'), '--all'],
                         capture_output=True, text=True, cwd=REPO).stdout
    section, found = None, []
    for line in out.splitlines():
        if not line.startswith(' '):
            section = line
            continue
        if not section.startswith('Ready to own') or ('has initial data' in line) != initial:
            continue
        m = re.match(r'\s+(\w+)\s+([0-9a-f]{8})\s+\+(0x[0-9a-f]+)\s+(\S+):(\d+)', line)
        if m:
            found.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4), int(m.group(5))))
    return found


def xbe_bytes(address, size):
    d = open(os.path.join(REPO, 'disc', 'default.xbe'), 'rb').read()
    base = struct.unpack_from('<I', d, 0x104)[0]
    count = struct.unpack_from('<I', d, 0x11c)[0]
    at = struct.unpack_from('<I', d, 0x120)[0] - base
    for i in range(count):
        flags, va, vsize, raw, rawsize, name = struct.unpack_from('<6I', d, at + i * 56)
        if va <= address and address + size <= va + vsize:
            data = d[raw + address - va:raw + min(address - va + size, rawsize)]
            return data + bytes(size - len(data))
    return None


def float_literal(word):
    value = struct.unpack('<f', word)[0]
    if value != value or value in (float('inf'), float('-inf')):
        return None
    for digits in range(1, 10):
        text = '%.*g' % (digits, value)
        if struct.pack('<f', float(text)) == word:
            if 'e' in text and 'e-' not in text:
                text = repr(float(text))   # 6e+01 as 60.0
            if 'e' not in text and '.' not in text:
                text += '.0'
            return text + 'f'
    return None


SCALARS = {'float': ('<f', None), 'int32_t': ('<i', 4), 'uint32_t': ('<I', 4), 'int16_t': ('<h', 2),
           'uint16_t': ('<H', 2), 'int8_t': ('<b', 1), 'uint8_t': ('<B', 1), 'int': ('<i', 4), 'uint': ('<I', 4)}


def initialiser(type_, dims, address, size):
    """The initialiser for a definition from the XBE's bytes, or None when it is not a scalar or a string"""
    data = xbe_bytes(address, size)
    if data is None:
        return None
    if not dims and type_ == 'float' and size == 4:
        return float_literal(data)
    if not dims and type_ in SCALARS and SCALARS[type_][1] == size:
        return str(struct.unpack(SCALARS[type_][0], data)[0])
    if type_ == 'char' and re.fullmatch(r'\[(\d+)\]', dims) and int(dims[1:-1]) == size:
        text = data.split(b'\0')[0]
        if data[len(text):].strip(b'\0') or not all(32 <= c < 127 and c not in b'"\\' for c in text):
            return None
        return '"%s"' % text.decode('ascii')
    return None


def declaration(type_, name, dims):
    star = type_.endswith('*')
    return '%s%s%s%s' % (type_, '' if star else ' ', name, dims)


def parse(line, name, address):
    m = DEFINE.match(line)
    if not m or m.group(2) != name:
        return None
    for form, take in FORMS:
        f = form.match(m.group(3))
        if f:
            type_, dims, at = take(f)
            if type_ is None or int(at, 16) != address:
                return None
            type_ = re.sub(r'\s+', ' ', type_).strip()
            return m.group(1), declaration(type_, name, dims), m.group(4) or ''
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    listing = '--list' in sys.argv
    initial = '--initial' in sys.argv
    into = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--into=')), None)
    todo = [g for g in ready(initial) if not args or g[0] in args]
    edits, skipped = {}, []
    for name, address, size, path, line in todo:
        full = os.path.join(REPO, path)
        text = open(full, encoding='utf-8').read().splitlines()
        parsed = parse(text[line - 1], name, address)
        if parsed is None:
            skipped.append('%s (%s:%d): %s' % (name, path, line, text[line - 1].strip()))
            continue
        indent, decl, comment = parsed
        if initial:
            m = re.match(r'^(.*?)\s*\**\s*' + name + r'(\[[^\]]*\])?$', decl)
            value = initialiser(m.group(1), m.group(2) or '', address, size) if m else None
            if value is None:
                skipped.append('%s (%s:%d): no initialiser for %s' % (name, path, line, decl))
                continue
            init_decl = decl + ' = ' + value
        else:
            init_decl = decl
        tag = '%s// XBE_GLOBAL(0x%08x, 0x%x)' % (indent, address, size)
        definition = indent + init_decl + ';' + ('  ' + comment if comment else '')
        if path.endswith('.h'):
            source = into or os.path.splitext(path)[0] + '.cpp'
            if not os.path.exists(os.path.join(REPO, source)):
                skipped.append('%s (%s:%d): no %s for the definition' % (name, path, line, source))
                continue
            edits.setdefault(path, []).append(('replace', line, [indent + 'extern ' + decl + ';' +
                                                                ('  ' + comment if comment else '')]))
            edits.setdefault(source, []).append(('add', 0, [tag, init_decl + ';']))
        else:
            edits.setdefault(path, []).append(('replace', line, [tag, definition]))
        if listing:
            print('%-36s %s:%d  %s' % (name, path, line, init_decl))
    for s in skipped:
        print('skipped:', s)
    print('%d globals, %d skipped, %d files' % (len(todo) - len(skipped), len(skipped), len(edits)))
    if listing:
        return
    for path, changes in edits.items():
        full = os.path.join(REPO, path)
        raw = open(full, 'rb').read()
        crlf = b'\r\n' in raw
        lines = raw.decode('utf-8').splitlines()
        for kind, line, new in sorted((c for c in changes if c[0] == 'replace'), key=lambda c: -c[1]):
            lines[line - 1:line] = new
        added = [l for c in changes if c[0] == 'add' for l in c[2]]
        if added:
            # after the last #include, with a blank line either side
            at = max((i for i, l in enumerate(lines) if l.startswith('#include')), default=-1) + 1
            lines[at:at] = [''] + added
        out = ('\r\n' if crlf else '\n').join(lines) + ('\r\n' if crlf else '\n')
        open(full, 'wb').write(out.encode('utf-8'))


if __name__ == '__main__':
    main()
