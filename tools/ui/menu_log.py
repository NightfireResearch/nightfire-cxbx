"""Names the hashcodes and message types in a MenuProbe log (settings.ini MenuLog=on; src/action/devtools/MenuProbe.cpp).

  python menu_log.py run.log            # the [menu] and [menuscript] lines, with names
  python menu_log.py run.log --pages    # just the page changes (message 0x4c, "entered from")

Names come from src/action/assets.h (the HASHCODE enum: P_*, C_*, SUB_*) and the MessageType enum in
src/action/ui/ui.h, so they follow the code as it is renamed.
"""
import os
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


def enum_names(path, enum):
    """value -> name for one 'typedef enum { ... } <enum>;' in a C header"""
    text = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'typedef enum\s*\{(.*?)\}\s*' + enum + r'\s*;', text, re.S)
    names = {}
    if not m:
        return names
    for name, val in re.findall(r'^\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)', m.group(1), re.M):
        names.setdefault(int(val, 0), name)
    return names


def main():
    hashes = enum_names(os.path.join(REPO, 'src', 'action', 'assets.h'), 'HASHCODE')
    msgs = {v: n.replace('MessageType_', '') for v, n in
            enum_names(os.path.join(REPO, 'src', 'action', 'ui', 'ui.h'), 'MessageType').items()}
    pages_only = '--pages' in sys.argv
    hexre = re.compile(r'0x(40|10)[0-9a-f]{6}')

    def name(m):
        v = int(m.group(0), 16)
        return '%s(%s)' % (hashes[v], m.group(0)) if v in hashes else m.group(0)

    for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
        if line.startswith('[menuscript]'):
            print(line.rstrip())
            continue
        m = re.match(r'\[menu\] f=(\d+) m(\d+) (0x[0-9a-f]+) msg=0x([0-9a-f]+) a=(0x[0-9a-f]+) b=(0x[0-9a-f]+) -> (-?\d+)', line)
        if not m:
            continue
        frame, mgr, ctl, msg, a, b, res = m.groups()
        msg = int(msg, 16)
        if pages_only and msg != 0x4c:
            continue
        mname = msgs.get(msg, '?')
        print('f=%-6s m%s %-40s %-26s a=%s b=%s -> %s' % (
            frame, mgr, hexre.sub(name, ctl), '0x%02x %s' % (msg, mname), a, hexre.sub(name, b), res))


if __name__ == '__main__':
    main()
