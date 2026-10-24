"""Which action engine functions, and which of their blocks, the time demos reach (docs/action-timedemo.md).

A function the demos never call is not tested by them, and neither is a branch of it they never take. Before
porting a parcel of functions, this says what the recordings exercise, so the rest gets a shadow test or a replay
of its own. Each demo is played under cdb with a one-shot breakpoint on every address of interest in default.xbe,
so a hit costs one stop and a demo plays at nearly full speed.

    python tools/demo/demo_coverage.py Script_Load Script_Run 0x000c3950 ...
    python tools/demo/demo_coverage.py --file names.txt --demos "tests/demos/action/Castle*.nfdemo"
    python tools/demo/demo_coverage.py --blocks --file names.txt

Functions are names from tools/functions_action.json or addresses. Without --blocks, each function's entry; with
it, every basic block of the original's code, found by following its branches (and switch tables) from the entry
through disc/default.xbe with capstone. Either way the functions run as the original (NIGHTFIRE_ORIGINAL), so a
parcel can be measured after it is ported too. The cdb logs stay in build/demoplay/coverage_<demo>.log. Windows
only (cdb and action.exe); the demos need a processor of the vendor they were recorded on
(tests/demos/action/README.md).
"""

import argparse
import bisect
import glob
import json
import os
import re
import struct
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CDB = r'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'


def load_functions():
    functions = json.load(open(os.path.join(REPO, 'tools', 'functions_action.json')))
    by_name = {}
    for f in functions:
        by_name.setdefault(f['name'], int(f['address'], 16))
    by_address = {int(f['address'], 16): f['name'] for f in functions}
    return by_name, by_address


def resolve(items, by_name, by_address):
    out = []
    for item in items:
        if re.fullmatch(r'(0x)?[0-9a-fA-F]+', item) and item not in by_name:
            address = int(item, 16)
            out.append((address, by_address.get(address, item)))
        elif item in by_name:
            out.append((by_name[item], item))
        else:
            print(f'{item}: not a function in tools/functions_action.json', file=sys.stderr)
    return out


class Xbe:
    """default.xbe's sections, to read the original code by virtual address"""

    def __init__(self, path):
        self.data = open(path, 'rb').read()
        base = struct.unpack_from('<I', self.data, 0x104)[0]
        count = struct.unpack_from('<I', self.data, 0x11c)[0]
        headers = struct.unpack_from('<I', self.data, 0x120)[0] - base
        self.sections = []
        for i in range(count):
            _, vaddr, vsize, raddr, rsize = struct.unpack_from('<IIIII', self.data, headers + i * 0x38)
            self.sections.append((vaddr, vsize, raddr, rsize))

    def read(self, address, size):
        for vaddr, vsize, raddr, rsize in self.sections:
            if vaddr <= address < vaddr + rsize:
                offset = raddr + address - vaddr
                return self.data[offset:offset + min(size, vaddr + rsize - address)]
        return b''


def blocks_of(xbe, start, end):
    """The basic blocks of the function at [start, end): their first addresses, by following its control flow"""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    leaders = {start}
    todo = [start]
    seen = set()
    while todo:
        at = todo.pop()
        while start <= at < end and at not in seen:
            seen.add(at)
            code = xbe.read(at, 16)
            instruction = next(md.disasm(code, at), None)
            if instruction is None:
                break
            next_at = at + instruction.size
            group = instruction.groups
            if capstone.CS_GRP_RET in group or instruction.mnemonic == 'int3':
                break
            if capstone.CS_GRP_JUMP in group:
                operand = instruction.operands[0]
                if operand.type == capstone.x86.X86_OP_IMM:
                    target = operand.imm
                    if start <= target < end:
                        leaders.add(target)
                        todo.append(target)
                elif operand.type == capstone.x86.X86_OP_MEM and operand.mem.scale == 4 and operand.mem.base == 0:
                    table = operand.mem.disp   # a switch: jmp [reg*4 + table]
                    for i in range(1024):
                        entry = xbe.read(table + i * 4, 4)
                        if len(entry) < 4:
                            break
                        target = struct.unpack('<I', entry)[0]
                        if not start <= target < end:
                            break
                        leaders.add(target)
                        todo.append(target)
                if instruction.mnemonic == 'jmp':
                    break
                leaders.add(next_at)   # a conditional branch falls through to a new block
            at = next_at
    return sorted(leaders)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('functions', nargs='*')
    parser.add_argument('--file', help='a file of function names or addresses, one per line')
    parser.add_argument('--blocks', action='store_true', help='every basic block, not just the entry')
    parser.add_argument('--demos', default='tests/demos/action/*.nfdemo')
    parser.add_argument('--timeout', type=int, default=1800, help='seconds per demo')
    args = parser.parse_args()

    items = list(args.functions)
    if args.file:
        items += [line.split('#')[0].strip() for line in open(args.file) if line.split('#')[0].strip()]
    by_name, by_address = load_functions()
    targets = resolve(items, by_name, by_address)
    if not targets:
        return 1

    # The addresses to watch, each with the function it belongs to
    points = {}
    if args.blocks:
        starts = sorted(by_address)
        xbe = Xbe(os.path.join(REPO, 'disc', 'default.xbe'))
        for address, label in targets:
            following = starts[bisect.bisect_right(starts, address):]
            end = following[0] if following else address + 0x10000
            for block in blocks_of(xbe, address, end):
                points[block] = (address, label)
    else:
        points = {address: (address, label) for address, label in targets}

    log_dir = os.path.join(REPO, 'build', 'demoplay')
    os.makedirs(log_dir, exist_ok=True)
    script = os.path.join(log_dir, 'coverage.cdb')
    with open(script, 'w') as f:
        # The breakpoints go in once actioninject.dll has loaded: the XBE is mapped by then
        f.write('sxe ld:actioninject\ng\n')
        for point in sorted(points):
            f.write(f'bp /1 0x{point:08x} ".echo HIT 0x{point:08x}; gc"\n')
        f.write('sxd av\nsxd ld\ng\nq\n')

    release = os.path.join(REPO, 'Release')
    hits = {point: set() for point in points}
    demos = sorted(glob.glob(os.path.join(REPO, args.demos)))
    originals = ','.join(f'0x{address:x}' for address, _ in targets)
    for demo in demos:
        name = os.path.splitext(os.path.basename(demo))[0]
        log = os.path.join(log_dir, f'coverage_{name}.log')
        env = dict(os.environ, NIGHTFIRE_DEMO_PLAY=demo, NIGHTFIRE_DEMO_FAST='4', NIGHTFIRE_ORIGINAL=originals)
        env.pop('NIGHTFIRE_DEMO_RECORD', None)
        env.pop('NIGHTFIRE_DEMO_STOP', None)
        try:
            subprocess.run([CDB, '-cf', script, '-logo', log, os.path.join(release, 'action.exe')], cwd=release,
                           env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print(f'{name}: timed out', file=sys.stderr)
        text = open(log, errors='replace').read()
        found = set(int(a, 16) for a in re.findall(r'^HIT 0x([0-9a-f]{8})', text, re.M))
        for point in found & set(points):
            hits[point].add(name.split('-')[0])
        print(f'{name}: {len(found)} of {len(points)} reached', file=sys.stderr)

    reached_functions = 0
    for address, label in targets:
        mine = sorted(p for p, (a, _) in points.items() if a == address)
        where = sorted(set().union(*(hits[p] for p in mine)))
        reached_functions += bool(where)
        if args.blocks:
            missed = [p for p in mine if not hits[p]]
            line = f'0x{address:08x} {label:40} {len(mine) - len(missed):3}/{len(mine):3} blocks'
            if missed and len(missed) < len(mine):
                line += '  missed ' + ' '.join(f'{p:x}' for p in missed)
            elif not where:
                line += '  NOT REACHED'
            print(line)
        else:
            print(f'0x{address:08x} {label:40} {", ".join(where) if where else "NOT REACHED"}')
    if args.blocks:
        print(f'{sum(1 for p in points if hits[p])} of {len(points)} blocks, '
              f'{reached_functions} of {len(targets)} functions reached by {len(demos)} demos')
    else:
        print(f'{reached_functions} of {len(targets)} reached by {len(demos)} demos')
    return 0


if __name__ == '__main__':
    sys.exit(main())
