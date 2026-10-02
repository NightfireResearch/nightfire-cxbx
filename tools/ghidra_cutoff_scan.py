#!/usr/bin/env python3
"""Finds functions Ghidra has cut short at a call: a CALL that is the last instruction of its function's body.

That is what a callee wrongly marked no-return does, and what a stale CALL_RETURN flow override left on a call site
does: Ghidra stops following the code after the call, so the rest of the function falls out of it - out of the
decompile's function body, the exports (tools/functions_*.json, xrefs_*.json) and every tool built on them.
docs/ghidra-flow-overrides.md has the history and how to repair one.

Needs the Ghidra MCP server's HTTP endpoint (the 007Nightfire project open, both XBEs loaded) and disc/*.xbe for
the bytes after each call.

    python tools/ghidra_cutoff_scan.py                 # both engines
    python tools/ghidra_cutoff_scan.py --program default.xbe
"""

import json
import os
import re
import struct
import sys
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 'http://127.0.0.1:8089'
PROGRAMS = {'default.xbe': '/Xbox_EU/default.xbe', 'Driving.xbe': '/Xbox_EU/Driving.xbe'}


def get(path, **query):
    url = BASE + path + '?' + urllib.parse.urlencode(query)
    return urllib.request.urlopen(url, timeout=120).read().decode('utf-8', 'replace')


def xbe_reader(path):
    data = open(path, 'rb').read()
    base = struct.unpack_from('<I', data, 0x104)[0]
    count = struct.unpack_from('<I', data, 0x11c)[0]
    headers = struct.unpack_from('<I', data, 0x120)[0] - base
    sections = [struct.unpack_from('<IIIII', data, headers + i * 56)[1:5] for i in range(count)]

    def read(address, size):
        for va, _, raw, raw_size in sections:
            if va <= address < va + raw_size:
                return data[raw + address - va:raw + address - va + size]
        return b''
    return read


def scan(name, program):
    read = xbe_reader(os.path.join(ROOT, 'disc', name))
    result = json.loads(get('/search_instructions', mnemonic='CALL', limit=50000, program=program))
    calls = result['matches']
    if result.get('truncated'):
        print('warning: more than 50000 calls in %s; the scan is incomplete' % name)
    bodies, cut, outside = {}, [], 0
    for call in calls:
        address = int(call['address']['address'], 16)
        function = call.get('function')
        if not function:
            outside += 1
            continue
        if function not in bodies:
            text = get('/get_function_by_address', address='0x%08x' % address, program=program)
            m = re.search(r'Body: ([0-9a-f]+) - ([0-9a-f]+)', text)
            bodies[function] = int(m.group(2), 16) if m else None
        if bodies[function] is not None and address + call['length'] - 1 == bodies[function]:
            cut.append((address, function, call['operands'], read(address + call['length'], 8).hex()))
    print('== %s: %d calls, %d outside any function, %d ending their function' % (name, len(calls), outside, len(cut)))
    for address, function, target, after in cut:
        print('  %08x  %-40s calls %-12s then %s' % (address, function[:40], target, after))
    return len(cut)


def main():
    names = list(PROGRAMS)
    if '--program' in sys.argv:
        names = [sys.argv[sys.argv.index('--program') + 1]]
    found = sum(scan(n, PROGRAMS[n]) for n in names)
    # A call that genuinely does not return (an exit, a fatal error) is followed by padding or another function;
    # code after it, as in every case found so far, means the function was cut short.
    sys.exit(1 if found else 0)


if __name__ == '__main__':
    main()
