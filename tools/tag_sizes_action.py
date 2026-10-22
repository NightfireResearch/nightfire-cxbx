"""Tags the action engine's globals global_coverage.py cannot size with the size the compiler gives their type.

    python tools/tag_sizes_action.py --list     what it would tag
    python tools/tag_sizes_action.py            tag them

A global `#define Name (*(T *)0x...)`, `(*(T (*)[n])0x...)` or `U32_AT(0x...)` whose type has no static_assert of its
size is sized by clang (tools/xbe_layout.py: the file is compiled with a probe of the type, so array lengths written
as macros are evaluated too) and gets `// XBE_GLOBAL(address, size)` on the line before. `((T *)0x...)`, the first
of an array of unknown length, is listed for a size by hand.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import own_globals_action as o  # noqa: E402
import xbe_layout  # noqa: E402


def unknown():
    out = subprocess.run([sys.executable, os.path.join(HERE, 'global_coverage.py')], capture_output=True, text=True,
                         cwd=REPO).stdout
    found, section = [], ''
    for line in out.splitlines():
        if not line.startswith(' '):
            section = line
            continue
        m = re.match(r'\s+(\w+)\s+([0-9a-f]{8})\s+(\S+):(\d+)$', line)
        if section.startswith('Unknown size') and m:
            found.append((m.group(1), int(m.group(2), 16), m.group(3), int(m.group(4))))
    return found


def main():
    listing = '--list' in sys.argv
    by_hand, edits = [], {}
    for name, address, path, line in unknown():
        text = open(os.path.join(REPO, path), encoding='utf-8').read().splitlines()
        d = o.DEFINE.match(text[line - 1])
        form = None
        if d and d.group(2) == name:
            for regex, take in o.FORMS:
                f = regex.match(d.group(3))
                if f:
                    form = take(f)
                    break
        if not form or form[1] == '[]' or int(form[2], 16) != address:
            by_hand.append('%s (%s:%d): %s' % (name, path, line, text[line - 1].strip()))
            continue
        type_, dims = re.sub(r'\s+', ' ', form[0]).strip(), form[1]
        source = os.path.splitext(path)[0] + '.cpp' if path.endswith('.h') else path
        if not os.path.exists(os.path.join(REPO, source)):
            by_hand.append('%s (%s:%d): no %s to size it in' % (name, path, line, source))
            continue
        try:
            size = xbe_layout.element_size(source, type_ + dims)
        except xbe_layout.Unwritable as e:
            size = None
        if not size:
            by_hand.append('%s (%s:%d): no size for %s%s' % (name, path, line, type_, dims))
            continue
        print('%-28s 0x%08x 0x%-6x %s:%d  %s%s' % (name, address, size, path, line, type_, dims))
        edits.setdefault(path, []).append((line, '%s// XBE_GLOBAL(0x%08x, 0x%x)' % (d.group(1), address, size)))
    for s in by_hand:
        print('by hand:', s)
    if listing:
        return
    for path, changes in edits.items():
        full = os.path.join(REPO, path)
        raw = open(full, 'rb').read().decode('utf-8')
        nl = '\r\n' if '\r\n' in raw else '\n'
        lines = raw.split(nl)
        for line, tag in sorted(changes, reverse=True):
            lines[line - 1:line - 1] = [tag]
        open(full, 'wb').write(nl.join(lines).encode('utf-8'))
    print('%d tagged in %d files, %d by hand' % (sum(len(c) for c in edits.values()), len(edits), len(by_hand)))


if __name__ == '__main__':
    main()
