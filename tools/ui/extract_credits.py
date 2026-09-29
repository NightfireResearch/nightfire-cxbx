"""Extracts the action engine's credits list from Menu_SetupCredits (0x76470) into data/credits.csv.

Menu_SetupCredits is ~25 KB of straight-line code that fills a 578-entry CreditsEntry array on the stack
(12 bytes each: char *txt_left, char *txt_right, byte modifiers_left, byte modifiers_right, byte centred)
and copies it once into a heap block. A text is either a literal string pointer (mov [esp+x], imm32, or
through edi/esi loaded with one) or the result of Txt_BindLabel(label, 0) (push 0; push label; call 0x6d460;
mov [esp+x], eax). This script symbolically executes the function with capstone: it tracks esp across the
pushes and `add esp, n`, the registers ebx (= 0), esi, edi and eax (= the last call's label), and records
every byte/dword stored into the array.

  python tools/ui/extract_credits.py                      # disc/default.xbe -> data/credits.csv
  python tools/ui/extract_credits.py --bank UKTxt.Dat     # also fills the comment column with the English text

Checks (the script fails if any is broken): every entry's five fields are written exactly once, the number of
Txt_BindLabel calls equals the number of label cells, every label's second argument is 0, every immediate that
points into the XBE's data is a string used by an entry, the entry count matches the 0x242 written to
*numLines_out and the 0x1b18-byte stack block.
"""
import argparse
import csv
import os
import struct
import sys

import capstone
from capstone import x86

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

FUNC_START = 0x76470
FUNC_END = 0x7c659          # one past the second `ret`
TXT_BIND_LABEL = 0x6d460
CHKSTK = 0xee460            # __alloca_probe for the 0x1b18-byte frame
ALLOC = 0x72d40             # the heap copy, reached only on the first call
ENTRY_SIZE = 12
LOCALS_BASE = 0xc           # the array starts at [esp+0xc] once ebx/esi/edi are pushed (lea esi,[esp+0xc])
COPY_START = 0x7c5fe       # jne over the one-time heap copy; the stores end here
BLANK_ADDR = 0x160d8c       # " ", the pooled blank used for empty cells


class XBE:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        self.base = struct.unpack_from('<I', self.d, 0x104)[0]
        n = struct.unpack_from('<I', self.d, 0x11c)[0]
        sh = struct.unpack_from('<I', self.d, 0x120)[0] - self.base
        self.secs = []
        for i in range(n):
            _flags, va, vs, ra, rs = struct.unpack_from('<5I', self.d, sh + 56 * i)
            self.secs.append((va, vs, ra, rs))

    def off(self, va):
        for v, _vs, r, rs in self.secs:
            if v <= va < v + rs:
                return r + va - v
        return None

    def read(self, va, n):
        o = self.off(va)
        return self.d[o:o + n]

    def cstr(self, va):
        o = self.off(va)
        return self.d[o:self.d.index(b'\0', o)]


class Label:
    def __init__(self, label, arg2, call_addr):
        self.label, self.arg2, self.call_addr = label, arg2, call_addr


def emulate(xbe):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    regs = {}
    stack = []
    dep = 0                  # bytes pushed below the frame's resting esp
    started = False
    cells = {}               # (offset, size) -> value (int, or Label)
    calls = []
    immediates = set()       # every imm32 that points into the XBE (strings)
    count_out = None
    alloc_size = None
    for ins in md.disasm(xbe.read(FUNC_START, FUNC_END - FUNC_START), FUNC_START):
        m, ops = ins.mnemonic, ins.operands
        for o in ops if m not in ('call', 'jne', 'jmp') else ():
            if o.type == x86.X86_OP_IMM and xbe.off(o.imm) is not None and o.imm > 0x10000:
                immediates.add(o.imm)
        if not started:
            # prologue: mov eax,0x1b18 / call __chkstk / push ebx,esi,edi / xor ebx,ebx
            if m == 'mov' and ins.op_str == 'eax, 0x1b18':
                alloc_size = 0x1b18
            if m == 'xor' and ins.op_str == 'ebx, ebx':
                regs['ebx'] = 0
                started = True
            continue
        if m == 'push':
            o = ops[0]
            stack.append(o.imm if o.type == x86.X86_OP_IMM else regs.get(ins.op_str))
            dep += 4
        elif m == 'pop':                              # only the epilogue pops (edi, esi, ebx)
            if stack:
                stack.pop()
            dep -= 4
        elif m == 'call':
            target = ops[0].imm
            args = stack[::-1]
            if target == TXT_BIND_LABEL:
                regs['eax'] = Label(args[0], args[1], ins.address)
                calls.append(regs['eax'])
            elif target == ALLOC:
                regs['eax'] = None
            else:
                raise SystemExit('unexpected call to 0x%x at 0x%x' % (target, ins.address))
        elif m == 'add' and ins.op_str.startswith('esp'):
            n = ops[1].imm
            del stack[len(stack) - n // 4:]
            dep -= n
        elif m == 'mov':
            d, s = ops
            if s.type == x86.X86_OP_IMM:
                v = s.imm
            elif s.type == x86.X86_OP_REG:
                v = regs.get(ins.reg_name(s.reg))
                if ins.reg_name(s.reg) == 'bl':
                    v = regs['ebx'] & 0xff
            else:
                v = None
            if d.type == x86.X86_OP_MEM and ins.reg_name(d.mem.base) == 'esp' and d.mem.index == 0:
                off = d.mem.disp - dep - LOCALS_BASE
                if off < 0 or ins.address >= COPY_START:
                    continue
                key = (off, d.size)
                if key in cells:
                    raise SystemExit('cell 0x%x written twice (0x%x)' % (off, ins.address))
                cells[key] = v
            elif d.type == x86.X86_OP_MEM and d.mem.base != 0 and ins.reg_name(d.mem.base) in ('eax', 'ecx'):
                if s.type == x86.X86_OP_IMM:          # mov dword ptr [eax/ecx], 0x242  (*numLines_out)
                    if count_out not in (None, s.imm):
                        raise SystemExit('two different line counts')
                    count_out = s.imm
            elif d.type == x86.X86_OP_REG:
                regs[ins.reg_name(d.reg)] = v
        elif m in ('cmp', 'nop', 'jne', 'rep movsd', 'lea', 'ret'):
            pass
        elif m == 'movsd' or m.startswith('rep'):
            pass
        else:
            raise SystemExit('unhandled %s %s at 0x%x' % (m, ins.op_str, ins.address))
    return cells, calls, immediates, count_out, alloc_size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--xbe', default=os.path.join(ROOT, 'disc', 'default.xbe'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'data', 'credits.csv'))
    ap.add_argument('--bank', help='a text bank (UKTxt.Dat) to put the English label text in the comment column')
    a = ap.parse_args()

    xbe = XBE(a.xbe)
    bank = None
    if a.bank:
        sys.path.insert(0, HERE)
        from text_bank import TextBank
        bank = TextBank(open(a.bank, 'rb').read())

    cells, calls, immediates, count_out, alloc_size = emulate(xbe)
    n = count_out
    assert n and alloc_size == n * ENTRY_SIZE, (n, alloc_size)
    layout = [(0, 4), (4, 4), (8, 1), (9, 1), (10, 1)]
    assert len(cells) == n * len(layout), 'cells %d, expected %d' % (len(cells), n * len(layout))

    rows = []
    used_labels = 0
    used_strings = set()
    for e in range(n):
        b = e * ENTRY_SIZE
        f = [cells[(b + o, s)] for o, s in layout]
        row = {'index': e, 'centred': f[4], 'left_style': f[2], 'right_style': f[3]}
        comment = []
        for side, v in (('left', f[0]), ('right', f[1])):
            row[side + '_label'] = ''
            row[side + '_text'] = ''
            if isinstance(v, Label):
                assert v.arg2 == 0, 'label 0x%x at 0x%x has arg2 %r' % (v.label, v.call_addr, v.arg2)
                used_labels += 1
                row[side + '_label'] = '0x%08x' % v.label
                if bank:
                    comment.append('%s: %s' % (side, bank.get(v.label)))
            elif v == 0:
                pass                                   # NULL: the right half of a centred line
            else:
                used_strings.add(v)
                row[side + '_text'] = xbe.cstr(v).decode('cp1252')
        row['comment'] = ' | '.join(comment)
        rows.append(row)

    assert used_labels == len(calls), 'labels used %d, Txt_BindLabel calls %d' % (used_labels, len(calls))
    unused = immediates - used_strings
    assert not unused, 'string pointers never stored: ' + ' '.join(hex(u) for u in sorted(unused))

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    cols = ['index', 'centred', 'left_style', 'left_label', 'left_text',
            'right_style', 'right_label', 'right_text', 'comment']
    with open(a.out, 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, cols, quoting=csv.QUOTE_ALL, lineterminator='\n')
        w.writeheader()
        w.writerows(rows)
    print('%d lines, %d Txt_BindLabel calls, %d distinct literal strings -> %s'
          % (n, len(calls), len(used_strings), a.out))


if __name__ == '__main__':
    main()
