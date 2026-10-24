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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbe_layout  # noqa: E402

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

AT_TYPES = {'I32_AT': 'int32_t', 'I16_AT': 'int16_t', 'I8_AT': 'int8_t', 'U32_AT': 'uint32_t',
            'U16_AT': 'uint16_t', 'U8_AT': 'uint8_t', 'FLOAT_AT': 'float', 'PTR_AT': 'void *'}
ADDRESS = r'\(?\s*(0x[0-9a-fA-F]+)\s*\)?'
DEFINE = re.compile(r'^(\s*)#\s*define\s+(\w+)\s+(.*?)\s*(//.*)?$')
FORMS = [
    # U32_AT(0x...)
    (re.compile(r'^(\w+_AT)\s*\(\s*(0x[0-9a-fA-F]+)\s*\)$'), lambda m: (AT_TYPES.get(m.group(1)), '', m.group(2))),
    # (*(T(*))0x...): a pointer to one T, written oddly
    (re.compile(r'^\(\s*\*\s*\(\s*([\w\s]+?)\s*\(\s*\*\s*\)\s*\)\s*' + ADDRESS + r'\s*\)$'),
     lambda m: (m.group(1), '', m.group(2))),
    # (*(T(*)[n])0x...) and (*(T *(*)[n])0x...)
    (re.compile(r'^\(\s*\*\s*\(\s*([\w\s]+?)\s*(\**)\s*\(\s*\*\s*\)\s*(\[[^\]]+\](?:\[[^\]]+\])*)\s*\)\s*' + ADDRESS +
                r'\s*\)$'),
     lambda m: (m.group(1) + (' ' + m.group(2) if m.group(2) else ''), m.group(3), m.group(4))),
    # ((T *)0x...): the first of an array, as long as the global's size
    (re.compile(r'^\(\s*\(\s*([\w\s]+?)\s*\*\s*\)\s*' + ADDRESS + r'\s*\)$'),
     lambda m: (m.group(1), '[]', m.group(2))),
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


def address_of(name, define):
    """How an address is written by a macro's name: the macro itself when it is a pointer, &name when it is the
    object (*(T *)address)"""
    return '&' + name if define and define.group(3).lstrip().startswith('(*') else name


def our_names():
    """{address: name} for the globals our source tags or still names by address, and {address: name} for the
    functions the injection table puts ours at"""
    names, functions = {}, {}
    for root, _, files in os.walk(os.path.join(REPO, 'src', 'action')):
        if 'devtools' in root:
            continue
        for f in files:
            if not f.endswith(('.cpp', '.h', '.inc')):
                continue
            lines = open(os.path.join(root, f), encoding='utf-8', errors='replace').read().splitlines()
            for i, l in enumerate(lines):
                tag = re.match(r'\s*// XBE_GLOBAL\((0x[0-9a-fA-F]+),', l)
                if tag and i + 1 < len(lines):
                    m = re.search(r'(\w+)\s*(\[[^=;]*\])?\s*(=|;)', lines[i + 1])
                    d = re.match(r'\s*#\s*define\s+(\w+)', lines[i + 1])
                    if d:
                        names.setdefault(int(tag.group(1), 16), address_of(d.group(1), DEFINE.match(lines[i + 1])))
                    elif m:
                        names.setdefault(int(tag.group(1), 16), m.group(1) if m.group(2) else '&' + m.group(1))
                    continue
                d = DEFINE.match(l)
                if d:
                    a = re.search(r'\)\s*\(?\s*(0x[0-9a-fA-F]{8})\s*\)?\s*\)?$', d.group(3))
                    if a:
                        names.setdefault(int(a.group(1), 16), address_of(d.group(2), d))
    for l in open(os.path.join(REPO, 'src', 'action', 'autogenerated_injections.inc')):
        m = re.search(r'WriteJmpTo\(0x([0-9a-f]+), XbeAddress\(&([\w:]+)\)', l)
        if m:
            functions.setdefault(int(m.group(1), 16), m.group(2))
    return names, functions


def table_initialiser(source, type_, dims, address, size, indent):
    """The initialiser of a structure or an array from the XBE (xbe_layout.py), its dims ('[]' for as many as fit) and
    an error"""
    try:
        elem = xbe_layout.element_size(source, type_)
        if elem is None:
            return None, dims, 'no layout for ' + type_
        sizes = re.findall(r'\[([^\]]*)\]', dims)
        if len(sizes) > 1:
            inner = [int(s, 0) for s in sizes[1:]]   # the inner sizes must be numbers
            for n in inner:
                elem *= n
            type_ = type_ + ''.join('[%d]' % n for n in inner)
        count = None
        if sizes:
            if size % elem:
                return None, dims, '0x%x bytes is not a whole number of %s' % (size, type_)
            count = size // elem
            if dims == '[]':
                dims = '[%d]' % count
        elif elem != size:
            return None, dims, '%s is 0x%x bytes, the global 0x%x' % (type_, elem, size)
        names, functions = our_names()
        text = xbe_layout.initialiser(source, type_, count, address, lambda a, n: xbe_bytes(a, n) or bytes(n),
                                      names, functions)
        return xbe_layout.wrap(text, indent), dims, None
    except xbe_layout.Unwritable as e:
        return None, dims, str(e)
    except (ValueError, struct.error) as e:
        return None, dims, str(e)


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


LENGTHS = {}   # --any NAME=LENGTH: the length of a ((T *)0x...) table, as the definition writes it


def named(names):
    """[(name, address, size, path, line)] for the named macros wherever src/action defines them, whatever
    global_coverage.py makes of them (--any): the size the compiler gives the type (NAME=LENGTH gives the length of
    a table the macro only points at)"""
    for n in list(names):
        if '=' in n:
            name, length = n.split('=', 1)
            LENGTHS[name] = length
            names.append(name)
    found = []
    for root, _, files in os.walk(os.path.join(REPO, 'src', 'action')):
        if 'devtools' in root:
            continue
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            path = os.path.relpath(os.path.join(root, f), REPO).replace(os.sep, '/')
            for i, l in enumerate(open(os.path.join(root, f), encoding='utf-8', errors='replace').read().splitlines()):
                d = DEFINE.match(l)
                if not d or d.group(2) not in names:
                    continue
                for form, take in FORMS:
                    m = form.match(d.group(3))
                    if m:
                        type_, dims, at = take(m)
                        source = os.path.splitext(path)[0] + '.cpp' if path.endswith('.h') else path
                        if dims == '[]' and d.group(2) in LENGTHS:
                            dims = '[%s]' % LENGTHS[d.group(2)]
                        if dims == '[]' or not os.path.exists(os.path.join(REPO, source)):
                            break
                        size = xbe_layout.element_size(source, re.sub(r'\s+', ' ', type_).strip() + dims)
                        if size:
                            found.append((d.group(2), int(at, 16), size, path, i + 1))
                        break
    return found


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    listing = '--list' in sys.argv
    initial = '--initial' in sys.argv
    into = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--into=')), None)
    todo = named(args) if '--any' in sys.argv else [g for g in ready(initial) if not args or g[0] in args]
    edits, skipped = {}, []
    for name, address, size, path, line in todo:
        full = os.path.join(REPO, path)
        text = open(full, encoding='utf-8').read().splitlines()
        parsed = parse(text[line - 1], name, address)
        if parsed is None:
            skipped.append('%s (%s:%d): %s' % (name, path, line, text[line - 1].strip()))
            continue
        indent, decl, comment = parsed
        source = into or os.path.splitext(path)[0] + '.cpp' if path.endswith('.h') else path
        m = re.match(r'^(.*?)\s*' + name + r'((?:\[[^\]]*\])*)$', decl)
        if initial or ('--any' in sys.argv and (xbe_bytes(address, size) or bytes(1)).strip(b'\0')):
            value = initialiser(m.group(1).strip(), m.group(2), address, size) if m else None
            if value is None and m:
                value, dims, error = table_initialiser(source, m.group(1).strip(), m.group(2), address, size, indent)
                if value is None:
                    skipped.append('%s (%s:%d): %s' % (name, path, line, error))
                    continue
                decl = declaration(m.group(1).strip(), name, dims)
            if value is None:
                skipped.append('%s (%s:%d): no initialiser for %s' % (name, path, line, decl))
                continue
            init_decl = decl + ' = ' + value
        else:
            if m and m.group(2) == '[]':
                elem = xbe_layout.element_size(source, m.group(1).strip())
                if not elem or size % elem:
                    skipped.append('%s (%s:%d): the size of %s' % (name, path, line, m.group(1)))
                    continue
                decl = declaration(m.group(1).strip(), name, '[%d]' % (size // elem))
            init_decl = decl
        if name in LENGTHS:   # the length as given, not its value
            init_decl = re.sub(r'\b%s\[\d+\]' % name, '%s[%s]' % (name, LENGTHS[name]), init_decl, count=1)
            decl = re.sub(r'\b%s\[\d+\]' % name, '%s[%s]' % (name, LENGTHS[name]), decl, count=1)
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
            if line >= 2 and new and lines[line - 2].strip() == new[0].strip():
                new = new[1:]   # the tag is already on the line before
            elif line >= 2 and new and new[0].lstrip().startswith('extern ') and \
                    lines[line - 2].lstrip().startswith('// XBE_GLOBAL('):
                line -= 1   # a header's tag goes with the definition, not the extern
                lines[line - 1:line + 1] = [lines[line]]
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
