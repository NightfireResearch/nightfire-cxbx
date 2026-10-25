"""Initialisers for the game's tables, from the XBE's bytes and our structures' layouts.

own_globals_action.py --initial uses it for a global whose type is a structure, or an array of structures or of
scalars. The layouts are the compiler's: clang (the Linux cross build's, in the nf-cross Docker image) compiles the
file that defines the global with -fdump-record-layouts-canonical, which gives each record's fields with their offsets
and canonical types; the flags are the Linux build's (build/linux/compile_commands.json, from `cmake --preset linux`).

Each structure is written with designated initialisers in declaration order, its zero fields left out; an array
positionally, its trailing zeros left out. Numbers are decimal up to 9999 or when round, else hex; floats in the fewest
digits that give back their bits; an enum (any type the dump does not resolve to a builtin) as a cast of its value. A
pointer is NULL, a number below the image (cast), a function of ours (the injection table), a global of ours or still
at its address (by its tag or macro: at its start, or inside a table the caller names), or a string in .rdata (the
literal); any other pointer stops the initialiser, and says why, as does the linker's shared empty function.

    python tools/xbe_layout.py SOURCE TYPE ADDRESS [COUNT]   print one initialiser
"""
import json
import os
import re
import struct
import subprocess

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
COMPILE_COMMANDS = os.path.join(REPO, 'build', 'linux', 'compile_commands.json')
SHARED_EMPTY = 0x000e0ec0   # the bare ret the linker shares between the game's empty functions

BUILTINS = {
    'char': ('b', 1), 'signed char': ('b', 1), 'unsigned char': ('B', 1), 'bool': ('?', 1), '_Bool': ('?', 1),
    'short': ('h', 2), 'unsigned short': ('H', 2), 'int': ('i', 4), 'unsigned int': ('I', 4),
    'long': ('i', 4), 'unsigned long': ('I', 4), 'long long': ('q', 8), 'unsigned long long': ('Q', 8),
    'float': ('f', 4), 'double': ('d', 8),
}


class Unwritable(Exception):
    pass


_cache = {}


def records(source, types=()):
    """{record name: (size, [(offset, type, field name)])} for every record laid out in the source file, with the
    layouts of `types` asked for (a record the file only points to is not laid out otherwise)"""
    source = os.path.normpath(source)
    key = (source, tuple(sorted(set(types))))
    if key in _cache:
        return _cache[key]
    cc = json.load(open(COMPILE_COMMANDS))
    rel = os.path.relpath(os.path.join(REPO, source), REPO).replace(os.sep, '/')
    entry = next((c for c in cc if c['file'].endswith('/' + rel)), None)
    if entry is None:
        raise Unwritable('%s is not in %s' % (rel, COMPILE_COMMANDS))
    command = entry.get('command') or ' '.join(entry['arguments'])
    command = re.sub(r'\s-o\s+\S+', '', command)
    command = re.sub(r'\s-c\s+\S+', ' ', command)
    probe = os.path.join(REPO, 'build', 'layout_probe.cpp')
    with open(probe, 'w', newline='\n') as f:
        f.write('#include "/src/%s"\n' % rel)
        for i, t in enumerate(key[1]):
            # a record of one field: its line gives the type's canonical name, and the type is laid out
            m = re.match(r'^(.*?)((?:\[[^\]]*\])*)$', t)
            f.write('struct layout_probe_%d { %s value%s; };\nchar layout_probe_size_%d[sizeof(layout_probe_%d)];\n'
                    % (i, m.group(1), m.group(2), i, i))
    command += ' /src/build/layout_probe.cpp'
    command += ' -fsyntax-only -Xclang -fdump-record-layouts-canonical'
    out = subprocess.run(['docker', 'run', '--rm', '-v', '%s:/src' % REPO, '-w', entry['directory'], 'nf-cross',
                          'bash', '-c', command + ' 2>/dev/null'],
                         capture_output=True, text=True, env=dict(os.environ, MSYS_NO_PATHCONV='1')).stdout
    found = {}
    for block in out.split('*** Dumping AST Record Layout')[1:]:
        lines = block.strip('\n').split('\n')
        head = re.match(r'\s*0 \| (?:struct|union|class) (\S+)', lines[0])
        if not head:
            continue
        size = int(re.search(r'sizeof=(\d+)', block).group(1))
        rows = []
        for line in lines[1:]:
            m = re.match(r'\s*(\d+)(:\d+-\d+)? \|( +)(\S.*?) (\w+)$', line)
            if m and '(anonymous' not in line and '(unnamed' not in line:
                rows.append((int(m.group(1)), len(m.group(3)), m.group(4), m.group(5), m.group(2) is not None))
                continue
            m = re.match(r'\s*(\d+) \|( +)((?:struct|union) .*\((?:anonymous|unnamed) at [^)]*\))\s*(\w*)$', line)
            if m:   # an anonymous member (a nameless union inside a structure): its members follow, deeper
                rows.append((int(m.group(1)), len(m.group(2)), m.group(3), m.group(4) or None, False))
        lay_out(head.group(1), size, rows, 0, 3, found)
    for i, t in enumerate(key[1]):
        probe_record = found.get('layout_probe_%d' % i)
        if probe_record:
            found['canonical:' + t] = probe_record[1][0][1]
            found['size:' + t] = probe_record[0]
    _cache[key] = found
    return found


def record_name(t):
    return re.sub(r'^(struct|union|class)\s+', '', t)


def lay_out(name, size, rows, base, depth, found):
    """A record's fields from the dump's rows (offset, indent, type, name, bit field) at one depth; a field's own
    rows, one depth further in, lay out its type if no dump of its own did (a typedef of an unnamed structure)"""
    fields = []
    i = 0
    while i < len(rows):
        off, indent, ft, fn, bits = rows[i]
        if indent != depth:
            i += 1
            continue
        j = i + 1
        while j < len(rows) and rows[j][1] > depth:
            j += 1
        fields.append((off - base, ft, fn, bits))
        if j > i + 1 and not split_array(ft):
            sub = record_name(ft)
            end = next((r[0] for r in rows[j:] if r[1] == depth), base + size)
            if sub not in found:
                lay_out(sub, end - off, rows[i + 1:j], off, depth + 2, found)
        i = j
    found.setdefault(name, (size, fields))


def split_array(t):
    m = re.match(r'^(.*?)\s*\[(\d+)\]((?:\[\d+\])*)$', t)
    if not m:
        return None
    inner = m.group(1) + m.group(3)
    return inner, int(m.group(2))


def size_of(t, recs):
    t = re.sub(r'\b(const|volatile)\b\s*', '', t).strip()
    if t.endswith('*'):
        return 4
    a = split_array(t)
    if a:
        return size_of(a[0], recs) * a[1]
    if t in BUILTINS:
        return BUILTINS[t][1]
    name = re.sub(r'^(struct|union|class|enum)\s+', '', t)
    if name in recs:
        return recs[name][0]
    return None   # an enum: the caller works it out from the layout


def c_string(text):
    """A C string literal of the bytes: printable ones as themselves, the rest as \\x escapes, the literal split where
    a hex digit follows one (as Font.cpp writes its format strings)"""
    out, escaped = '"', False
    for c in text:
        ch = chr(c)
        if 32 <= c < 127 and ch not in '"\\':
            if escaped and ch in '0123456789abcdefABCDEF':
                out += '""'
            out += ch
            escaped = False
        elif ch in '"\\':
            out += '\\' + ch
            escaped = False
        else:
            out += '\\x%02x' % c
            escaped = True
    return out + '"'


def number(value):
    """Decimal up to 9999 and for a round number (a score, a size in hundreds), hex for the rest (masks, hashcodes)"""
    if -9999 <= value <= 9999 or (value % 100 == 0 and abs(value) < 100000000):
        return str(value)
    return '-0x%x' % -value if value < 0 else '0x%x' % value


def float_literal(data, fmt):
    value = struct.unpack('<' + fmt, data)[0]
    if value != value or value in (float('inf'), float('-inf')):
        raise Unwritable('a NaN or infinity')
    for digits in range(1, 18):
        text = '%.*g' % (digits, value)
        if struct.pack('<' + fmt, float(text)) == data:
            if 'e' in text and 'e-' not in text:
                text = repr(float(text))
            if 'e' not in text and '.' not in text:
                text += '.0'
            return text + ('f' if fmt == 'f' else '')
    raise Unwritable('a float that does not round trip')


class Writer:
    def __init__(self, recs, read, names, functions, arrays=()):
        self.recs, self.read, self.names, self.functions = recs, read, names, functions
        self.arrays = arrays   # (start, size, name): a pointer inside one is &name[i], by the pointer's own type

    def pointer(self, value, t):
        if value == 0:
            return 'NULL'
        if value < 0x00010000:
            # below the image: a number the game keeps in a pointer
            return '(%s)0x%x' % (re.sub(r'^(struct|union) ', '', t), value)
        if value == SHARED_EMPTY:
            raise Unwritable("the linker's shared empty function (0x%08x): an empty function of the slot's type, by "
                             "hand" % value)
        if value in self.functions:
            return self.functions[value]
        if value in self.names:
            return self.names[value]
        for start, size, name in self.arrays:
            target = size_of(re.sub(r'\s*\*$', '', re.sub(r'\b(const|volatile)\b\s*', '', t).strip()), self.recs)
            if start <= value < start + size and target and (value - start) % target == 0:
                return '&%s[%d]' % (name, (value - start) // target)
        if re.match(r'^(const )?char \*$', t) or t == 'char *':
            text = self.read(value, 256).split(b'\0')[0]
            if text and all(32 <= c < 127 or c >= 0xf0 or c < 8 for c in text):
                literal = c_string(text)
                return literal if t.startswith('const') else '(char *)' + literal
        raise Unwritable('a %s at 0x%08x that names nothing of ours' % (t, value))

    def value(self, t, data, width=None):
        """(text, is_zero) for one value of type t"""
        bare = re.sub(r'\b(const|volatile)\b\s*', '', t).strip()
        if bare.endswith('*') or '(*)' in bare:
            v = struct.unpack('<I', data[:4])[0]
            return self.pointer(v, t), v == 0
        a = split_array(bare)
        if a:
            inner, n = a
            size = size_of(inner, self.recs) or len(data) // n
            if inner == 'char' and n == len(data):
                text = data.split(b'\0')[0]
                if all(32 <= c < 127 for c in text) and not data[len(text):].strip(b'\0'):
                    return '"%s"' % text.decode('ascii').replace('\\', '\\\\').replace('"', '\\"'), not text
            items = [self.value(inner, data[i * size:(i + 1) * size]) for i in range(n)]
            while items and items[-1][1]:
                items.pop()
            return '{' + ', '.join(i[0] for i in items) + '}', not items
        if bare in BUILTINS:
            fmt, size = BUILTINS[bare]
            if fmt in 'fd':
                if data[:size] == bytes(size):
                    return '0.0f' if fmt == 'f' else '0.0', True
                return float_literal(data[:size], fmt), False
            v = struct.unpack('<' + fmt, data[:size])[0]
            return ('true' if v else 'false') if fmt == '?' else number(v), not v
        name = re.sub(r'^(struct|union|class|enum)\s+', '', bare)
        if name in self.recs:
            size, fields = self.recs[name]
            parts = []
            if any(f[3] for f in fields):
                raise Unwritable('%s has bit fields' % name)
            if bare.startswith('union ') or len({f[0] for f in fields}) < len(fields):
                fields = fields[:1]   # a union, or a structure holding one: its first member, the whole of it
            for i, (off, ft, fn, _) in enumerate(fields):
                end = next((f[0] for f in fields[i + 1:] if f[0] > off), size)
                text, zero = self.value(ft, data[off:end])
                if not zero and fn is None:
                    raise Unwritable('%s has a nameless member that is not zero' % name)
                if not zero:
                    parts.append('.%s = %s' % (fn, text))
            return '{' + ', '.join(parts) + '}', not parts
        # an enum, as wide as its slot
        n = width or len(data)
        v = int.from_bytes(data[:n], 'little')
        return '(%s)%s' % (name, number(v)), not v


def initialiser(source, type_, count, address, read, names, functions, arrays=()):
    """The initialiser of `type_ name[count]` (count None: not an array) at address, laid out as source compiles it;
    `arrays` (start, size, name) are tables of ours a pointer may point inside"""
    recs = records(source, [base_type(type_)])
    w = Writer(recs, read, names, functions, arrays)
    probe_size = recs.get('size:' + base_type(type_))
    type_ = recs.get('canonical:' + base_type(type_), type_)
    size = size_of(type_, recs) or probe_size
    if size is None:
        raise Unwritable('no layout for %s' % type_)
    if count is None:
        return w.value(type_, read(address, size))[0]
    base, dims = re.match(r'^(.*?)((?:\[\d+\])*)$', type_).groups()
    return w.value('%s[%d]%s' % (base, count, dims), read(address, size * count))[0]   # the count outermost


def base_type(t):
    return re.sub(r'\b(const|volatile)\b\s*', '', t).strip()


def element_size(source, type_):
    recs = records(source, [base_type(type_)])
    if 'size:' + base_type(type_) in recs:
        return recs['size:' + base_type(type_)]
    return size_of(recs.get('canonical:' + base_type(type_), type_), recs)


def split_items(text):
    """The top-level items of '{a, b, ...}', and the text before the brace ('.field = ' kept with its item)"""
    depth, start, items, quoted = 0, 1, [], False
    for i, c in enumerate(text):
        if quoted:
            if c == '\\':
                continue
            if c == '"' and text[i - 1] != '\\':
                quoted = False
        elif c == '"':
            quoted = True
        elif c in '{(':
            depth += 1
        elif c in '})':
            depth -= 1
        elif c == ',' and depth == 1:
            items.append(text[start:i].strip())
            start = i + 1
    last = text[start:-1].strip()
    if last:
        items.append(last)
    return items


def wrap(text, indent, width=120):
    """An initialiser over several lines where it does not fit on one: an item a line, an item that does not fit
    itself wrapped the same way, a line deeper; a run of short numbers (a table's row) as many as fit a line"""
    if len(indent) + len(text) <= width or not text.startswith('{'):
        return text
    inner = indent + '    '
    items = split_items(text)
    lines = ['{']
    row = ''
    for item in items:
        if re.fullmatch(r'-?(0x)?[0-9a-fA-F.]+f?|\(\w+\)-?(0x)?[0-9a-fA-F]+', item):
            if row and len(inner) + len(row) + len(item) + 2 > width:
                lines.append(inner + row.rstrip())
                row = ''
            row += item + ', '
            continue
        if row:
            lines.append(inner + row.rstrip())
            row = ''
        m = re.match(r'^(\.\w+ = )(\{.*)$', item, re.S)
        if m and len(inner) + len(item) + 1 > width:
            # the table after '.field = ' wraps as if it began there
            lines.append(inner + m.group(1) + wrap(m.group(2), inner, width - len(m.group(1))) + ',')
        else:
            lines.append(inner + wrap(item, inner, width) + ',')
    if row:
        lines.append(inner + row.rstrip())
    lines.append(indent + '}')
    return '\n'.join(lines)


def main():
    """xbe_layout.py SOURCE TYPE ADDRESS [COUNT]: the initialiser of a TYPE (or COUNT of them) at ADDRESS, laid out as
    SOURCE compiles it"""
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import own_globals_action as o
    source, type_, address = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    count = int(sys.argv[4], 0) if len(sys.argv) > 4 else None
    names, functions = o.our_names()
    text = initialiser(source, type_, count, address, lambda a, n: o.xbe_bytes(a, n) or bytes(n), names, functions)
    print(wrap(text, ''))


if __name__ == '__main__':
    main()
