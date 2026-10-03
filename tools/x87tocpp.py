"""Symbolic x87 -> C++ translator for straight-line maths code in Driving.xbe (bit-exact ports).

Runs a listing over a symbolic machine: integer registers hold symbolic values (an argument pointer, or the float
bits copied from memory), memory is a map of (base, offset) -> float variable, the x87 stack holds names of double
values. Every x87 arithmetic instruction becomes one double statement, every float store a (float) rounding, in the
original's order - which is what makes a C++ port of PC=53 x87 code bit-exact (docs/driving/maths.md 3.2).

Used for Determinant4x4/Inverse (src/driving/platform/RealMath.cpp) and EAGL::Transform's Invert, 3x3
determinant, quaternion conversion and BuildSQT (src/driving/eagl/Transform.cpp); the cases at the bottom are the
calls that produced them. Straight-line code only: branch by hand around it. With live=True, loads and stores of
non-stack memory happen where the instruction does them and integer copies are snapshotted at the MOV, so a source
that aliases the destination behaves as in the original - always shadow-test the in-place variant.

    python tools/x87tocpp.py edet|equat|einv|esqt|det|inv
"""
import re
import sys
import capstone

sys.path.insert(0, r'Q:\nightfire-cxbx\tools')
import global_coverage as gc

gc.use_engine("driving")
nonzero, read, entry = gc.read_xbe()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

CONSTS = {0x189de8: '1.0', 0x189dec: '0.0', 0x189eb0: '0.5', 0x189e00: '2.0'}


class T:
    def __init__(self, args, out_lines):
        self.regs = {}
        self.sp = 0
        self.mem = {}           # ('stack', off) or (base, off) -> name of a float (or symbolic pointer)
        self.st = []            # x87 stack, st[0] = top; names of double expressions
        self.out = out_lines
        self.n = 0
        self.args = args        # stack offset -> name, e.g. {4: 'm'}
        self.stores = []        # (base, off, name) stores to non-stack memory in order
        self.live = False       # read and write non-stack memory where the instruction does

    def tmp(self, expr, kind='double'):
        self.n += 1
        name = ('d%d' if kind == 'double' else 'f%d') % self.n
        self.out.append('    %s %s = %s;' % (kind, name, expr))
        return name

    def loc(self, op):
        m = re.match(r'dword ptr \[(0x[0-9a-f]+)\]$', op)
        if m:
            return ('abs', int(m.group(1), 16))
        m = re.match(r'dword ptr \[(\w+)(?: ([+-]) (0x[0-9a-f]+|\d+))?\]$', op)
        if m:
            reg, sign, off = m.group(1), m.group(2), m.group(3)
            off = int(off, 0) if off else 0
            if sign == '-':
                off = -off
            if reg == 'esp':
                return ('stack', self.sp + off)
            base = self.regs[reg]
            return (base, off)
        m = re.match(r'dword ptr \[(0x[0-9a-f]+)\]$', op)
        if m:
            return ('abs', int(m.group(1), 16))
        raise ValueError(op)

    def value(self, loc):
        if loc[0] == 'abs':
            a = loc[1]
            if a in CONSTS:
                return CONSTS[a]
            raw = read(a, 4)
            import struct
            return '(double)FloatBits(0x%08x)' % struct.unpack('<I', raw)[0]
        if loc in self.mem:
            v = self.mem[loc]
            return '(double)' + v if not v.startswith('(') else v
        if loc[0] == 'stack' and loc[1] in self.args:
            return '(double)' + self.args[loc[1]]
        if loc[0] != 'stack':
            if self.live:
                return self.tmp('(double)%s[%d]' % (loc[0], loc[1] // 4))
            return '(double)%s[%d]' % (loc[0], loc[1] // 4)
        raise ValueError('uninitialised %r' % (loc,))

    def sti(self, op):
        m = re.match(r'st\((\d)\)$', op)
        return int(m.group(1)) if m else None

    def step(self, mn, ops):
        st = self.st
        if mn == 'mov':
            dst, src = [o.strip() for o in ops.split(',')]
            if dst in ('eax', 'ecx', 'edx', 'ebx', 'esi', 'edi', 'ebp'):
                if src.startswith('dword ptr'):
                    l = self.loc(src)
                    if l[0] == 'stack' and l[1] in self.args:
                        self.regs[dst] = self.args[l[1]]
                    elif l in self.mem:
                        self.regs[dst] = ('val', self.mem[l])
                    elif l[0] not in ('stack', 'abs'):
                        if self.live:   # the word as it is now, not when a later instruction reads the copy
                            self.regs[dst] = ('val', self.tmp('%s[%d]' % (l[0], l[1] // 4), 'float'))
                        else:
                            self.regs[dst] = ('val', '%s[%d]' % (l[0], l[1] // 4))
                    else:
                        raise ValueError(src)
                elif src.startswith('0x') or src.isdigit():
                    self.regs[dst] = ('imm', int(src, 0))
                else:
                    self.regs[dst] = self.regs[src]
                return
            l = self.loc(dst)
            v = self.regs[src] if src in self.regs else ('imm', int(src, 0))
            if v[0] == 'val':
                self.mem[l] = v[1]
                if l[0] != 'stack':
                    self.stores.append((l, v[1]))
            elif v[0] == 'imm':
                name = 'FloatBits(0x%08x)' % v[1]
                self.mem[l] = '(%s)' % name
                if l[0] != 'stack':
                    self.stores.append((l, '(float)' + name))
            else:
                raise ValueError('pointer stored')
            return
        if mn == 'xor':
            a, b = [o.strip() for o in ops.split(',')]
            if a == b:
                self.regs[a] = ('imm', 0)
                return
        if mn == 'sub' and ops.startswith('esp'):
            self.sp -= int(ops.split(',')[1], 0)
            return
        if mn == 'add' and ops.startswith('esp'):
            self.sp += int(ops.split(',')[1], 0)
            return
        if mn == 'push':
            self.sp -= 4
            if ops in self.regs:
                self.mem[('stack', self.sp)] = self.regs[ops]
            return
        if mn == 'pop':
            self.sp += 4
            return
        if mn in ('fld',):
            i = self.sti(ops)
            if i is not None:
                st.insert(0, st[i])
            else:
                st.insert(0, self.value(self.loc(ops)))
            return
        if mn == 'fld1':
            st.insert(0, '1.0')
            return
        if mn == 'fldz':
            st.insert(0, '0.0')
            return
        if mn == 'fchs':
            st[0] = self.tmp('-%s' % st[0])
            return
        if mn == 'fxch':
            i = self.sti(ops) if ops else 1
            st[0], st[i] = st[i], st[0]
            return
        if mn in ('fstp', 'fst'):
            i = self.sti(ops)
            if i is not None:
                st[i] = st[0]
            else:
                l = self.loc(ops)
                f = self.tmp('(float)%s' % st[0], 'float')
                if l[0] != 'stack' and self.live:
                    self.out.append('    %s[%d] = %s;' % (l[0], l[1] // 4, f))
                else:
                    self.mem[l] = f
                    if l[0] != 'stack':
                        self.stores.append((l, f))
            if mn == 'fstp':
                st.pop(0)
            return
        arith = {'fadd': '+', 'fsub': '-', 'fmul': '*', 'fdiv': '/', 'fsubr': '-r', 'fdivr': '/r'}
        base = mn[:-1] if mn.endswith('p') and mn[:-1] in arith else mn
        if base in arith:
            op = arith[base]
            pop = mn != base
            parts = [o.strip() for o in ops.split(',')] if ops else []
            if not pop and len(parts) == 1 and self.sti(parts[0]) is None:      # ST0 op= mem
                a, b = st[0], self.value(self.loc(parts[0]))
                dst = 0
            elif not pop and len(parts) == 1:                                   # ST0 = ST0 op ST(i)
                a, b, dst = st[0], st[self.sti(parts[0])], 0
            elif not pop and len(parts) == 2:
                d, s = self.sti(parts[0]), self.sti(parts[1])
                a, b, dst = st[d], st[s], d
            elif pop:                                                           # ST(i) = ST(i) op ST0; pop
                i = self.sti(parts[0]) if parts else 1
                a, b, dst = st[i], st[0], i
            else:
                raise ValueError(mn + ' ' + ops)
            if op.endswith('r'):
                a, b = b, a
                op = op[0]
            st[dst] = self.tmp('%s %s %s' % (a, op, b))
            if pop:
                st.pop(0)
            return
        if mn == 'fsqrt':
            st[0] = self.tmp('sqrt(%s)' % st[0])
            return
        raise ValueError('unhandled %s %s' % (mn, ops))


def translate(start, end, args, init_regs=None, init_sp=0, pre=None, live=False):
    lines = []
    t = T(args, lines)
    t.live = live
    t.sp = init_sp
    if init_regs:
        t.regs.update(init_regs)
    if pre:
        pre(t)
    for ins in md.disasm(read(start, end - start), start):
        if ins.mnemonic in ('nop', 'int3'):
            continue
        if ins.mnemonic == 'ret':
            break
        t.step(ins.mnemonic, ins.op_str)
    return t, lines


def emit(t, lines, ret=True):
    print(chr(10).join(lines))
    for l, v in t.stores:
        print('    %s[%d] = %s;' % (l[0], l[1] // 4, v))
    print('    // x87 stack left: %r' % t.st)

if __name__ == '__main__':
    which = sys.argv[1]
    if which == 'edet':     # 0x000f13b0, EAX = matrix
        t, lines = translate(0xf13b0, 0xf148c, {}, init_regs={'eax': 'm'})
        emit(t, lines)
    elif which == 'edet3':  # Determinant's 3x3 case, 0x000f27b8..0x000f2807
        t, lines = translate(0xf27b8, 0xf2807, {}, init_regs={}, init_sp=-0x60, pre=lambda t: t.regs.update({}))
        emit(t, lines)
    elif which == 'equat':  # 0x000f3090 quaternion to 3x3
        t, lines = translate(0xf3090, 0xf315b, {4: 'q', 8: 'out'})
        emit(t, lines)
    elif which == 'einv':   # Invert from 0x000f235c: ECX = src, ST0 = det
        t, lines = translate(0xf235c, 0xf2780, {4: 'm', 8: 'o'}, init_regs={'ecx': 'm'}, init_sp=-0x54,
                             pre=lambda t: setattr(t, 'st', ['det']), live=True)
        emit(t, lines)
    elif which == 'esqt':   # BuildSQT's arithmetic: ECX = this
        t, lines = translate(0xf8780, 0xf887b, {0x4: 'sx', 0x8: 'sy', 0xc: 'sz', 0x10: 'qx', 0x14: 'qy', 0x18: 'qz',
                             0x1c: 'qw'}, init_regs={'ecx': 'o'}, live=True)
        emit(t, lines)
    if which == 'det':
        t, lines = translate(0x114f00, 0x114fe0, {4: 'm'})
        print('\n'.join(lines))
        print('    return %s;   // ST0, stack depth %d' % (t.st[0], len(t.st)))
    elif which == 'inv':
        # From 0x11500e: ECX = m, ST0 = det (the call's result), esp as after "sub esp, 0x54" (the push popped)
        def pre(t):
            t.st = ['det']
        t, lines = translate(0x11500e, 0x115440, {}, init_regs={'ecx': 'm'}, init_sp=-0x58, pre=pre)
        print('\n'.join(lines))
        for l, v in t.stores:
            print('    o[%d] = %s;' % (l[1] // 4, v))
        print('    // x87 stack left: %r' % t.st)
