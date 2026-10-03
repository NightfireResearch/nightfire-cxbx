"""Generates the driving engine's static initialisers (src/driving/engine/StaticInitTable.cpp) from Driving.xbe.

    python tools/static_init_driving.py            write src/driving/engine/StaticInitTable.cpp
    python tools/static_init_driving.py --check    fail if it is out of date

The XBE's C++ initialiser table (__xc_a..__xc_z, 0x001b3db0-0x001b4904) lists 720 dynamic initialisers in .text
(0x00156c00-0x0015cd20) and DirectSound's four. The C runtime's _cinit walked it; our startup
(src/driving/platform/XboxStartup.cpp) calls RunStaticInitialisers instead, which does what each entry did, in
table order - the order matters where one entry reads what an earlier one wrote, and for constructors that link
objects into lists. This script reads every entry and writes it out as C++:

  - float and double arithmetic (585): one x87 operation on two constants or globals, stored. Each is a single
    rounding of exact operands, so plain C++ arithmetic gives the same bits. Constants from .rdata become
    literals; globals (.data) are read where they live, since some are written by earlier entries.
  - zero fills, copies and stores of constants;
  - ColourConvertXBoxToPS2 of a constant colour, and float products converted with __ftol2;
  - constructors of global objects, with the destructor registered with the C runtime's atexit as the original
    did. Constructors and destructors we own are called directly (TARGETS below); the rest by address. The C
    runtime's exit never runs (nothing calls it), so the destructors never do either: they are registered so that
    the runtime's heap sees the same allocations as before.
  - the driving engine's weapon table (0x001c94d8, 32 records), which one 4.4 KB initialiser built a store at a
    time, as a typed table (WeaponDefinition in src/driving/engine/StaticInit.h).

An entry of any other shape stops the script: it is a new kind, to be added here or to HAND below.
"""
import os
import re
import struct
import sys

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import global_coverage as gc   # noqa: E402

gc.use_engine("driving")
OUT = os.path.join(REPO, "src", "driving", "engine", "StaticInitTable.cpp")

TABLE, TABLE_END = 0x001b3db0, 0x001b4904
TEXT_INIT, TEXT_INIT_END = 0x00156c00, 0x0015cd20
RDATA, DATA = 0x00189be0, 0x001b3da0
DSOUND = (0x0017ac40, 0x00183aa4)
ATEXIT, VEC_CTOR, VEC_DTOR, FTOL, COLOUR = 0x00132a7b, 0x0013326e, 0x0013332e, 0x001321f8, 0x00075c40
WEAPONS, WEAPON_SIZE, WEAPON_COUNT, WEAPON_INIT = 0x001c94d8, 0x54, 32, 0x00158ef0

# Functions the initialisers and their destructors call, as C++. "this" functions are __thiscall: an object of
# ours is called as a method; an original takes (object, 0, args...) through __fastcall (the int is EDX). Where the
# original's address is a thunk to a function we own, the call goes to ours.
TARGETS = {
    # ours
    0x0004f320: ("method", "ActionQueue", "Construct"),
    0x0004f1b0: ("method", "ActionQueue", "Destruct"),
    0x000ef480: ("method", "EAGL::GeoPrimState", "Construct"),
    0x000ef490: ("method", "EAGL::GeoPrimState", "Destruct"),
    0x0011bbe0: ("method", "EAGL::GeoPrimState", "Destruct"),   # a thunk to 0x000ef490
    0x000e9760: ("method", "EAGL::DynamicModel", "Construct"),
    0x000ea720: ("method", "EAGL::DynamicModel", "Destruct"),
    0x000f0f60: ("method", "EAGL::RenderMethod", "Construct"),
    0x000f12c0: ("method", "EAGL::RenderMethod", "Destruct"),
    0x000f3c70: ("method", "SymbolPool", "Construct"),
    0x000f41d0: ("method", "SymbolPool", "Destruct"),
    0x000e6390: ("method", "ConstructorPool", "Construct"),
    0x000f3bf0: ("method", "ConstructorPool", "Destruct"),
    0x000f3ab0: ("method", "RuntimeAllocConstructorPool", "Destruct"),
    0x000f4f90: ("method", "EAGL::ProfilerRegion", "Construct"),
    0x000f5010: ("method", "EAGL::ProfilerRegion", "UnlinkThunk"),
    0x000f5480: ("method", "EAGL::ProfilerTimer", "Enter"),
    0x000f4f30: ("method", "EAGL::ProfilerTimer", "Destruct"),
}
# Argument types of the originals' stack arguments, where they are not plain words.
ARG_TYPES = {
    0x0008d420: ["float", "float"],                     # RRenderWorldCulling(near, far)
}
# Our ports' argument types for the constructors called with arguments.
METHOD_ARGS = {
    ("ActionQueue", "Construct"): ["char *"],
    ("EAGL::ProfilerRegion", "Construct"): ["const char *", "uint32_t"],
    ("EAGL::RenderMethod", "Construct"): ["EAGL::Packet *", "int", "const void *", "const void **",
                                          "EAGL::VertexShader **", "const uint8_t **", "EAGL::PixelShader **", "const char **",
                                          "uint32_t", "const char *"],
}
# Entries written by hand in src/driving/engine/StaticInit.cpp, by address: the C++ statement for each.
HAND = {
    WEAPON_INIT: "InitWeaponDefinitions();",
}
# Destructors written by hand there, by address.
HAND_DTORS = {
    0x0015d080: "DestroyGlobal_0023e1b0();",
}
FAST_FREE = 0x001147d0
EMPTY = 0x000d3580   # dummyNullFunction: a bare RET, the destructor of objects that need none

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
nonzero, read, entry = gc.read_xbe()
NAMES = {}


def u32(address):
    return struct.unpack("<I", read(address, 4))[0]


def f32(address):
    return struct.unpack("<f", read(address, 4))[0]


def f64(address):
    return struct.unpack("<d", read(address, 8))[0]


def instructions(address, limit=0x2000):
    """The function's instructions, to its first RET (initialisers and their destructors have no other exits) or
    a JMP out of it (a destructor thunk's tail call)."""
    out = []
    for insn in md.disasm(read(address, limit), address):
        out.append(insn)
        if insn.mnemonic == "ret":
            break
        if insn.mnemonic == "jmp" and insn.operands[0].type == X86_OP_IMM and \
                not address <= insn.operands[0].imm < address + 0x100:
            break
    return out


def float_literal(value, double):
    """The shortest decimal that reads back as exactly this float (or double)."""
    if value == int(value) and abs(value) < 1e9:
        text = "%d.0" % int(value)
    else:
        for digits in range(1, 18):
            text = "%.*g" % (digits, value)
            back = float(text)
            if (back == value) if double else (struct.pack("<f", back) == struct.pack("<f", value)):
                break
    return text if double else text + "f"


def operand(address, double):
    """A float operand: a literal when it is a constant in .rdata, otherwise the global."""
    if RDATA <= address < DATA:
        return float_literal(f64(address) if double else f32(address), double)
    return "%s(0x%08x)" % ("DOUBLE_AT" if double else "FLOAT_AT", address)


def hexw(value):
    return "0x%08x" % value


def callee(target):
    """C++ spelling of a call target that is not one of ours: a name for an original, defined by address."""
    if target not in NAMES:
        name = gc_name(target)
        NAMES[target] = name
    return NAMES[target]


FUNCTION_NAMES = {}


def gc_name(target):
    if not FUNCTION_NAMES:
        import json
        for f in json.load(open(gc.FUNCTIONS)):
            FUNCTION_NAMES[int(f["address"], 16)] = f["name"]
    name = FUNCTION_NAMES.get(target) or "FUN_%08x" % target
    # Class<Outer::T>::~Class -> Class_T_Destruct
    m = re.match(r"^(\w+)<(?:\w+::)*(\w+)>::~(\w+)$", name)
    if m and m.group(1) == m.group(3):
        return "%s_%s_Destruct" % (m.group(1), m.group(2))
    m = re.match(r"^(\w+)::~(\w+)$", name)
    if m and m.group(1) == m.group(2):
        return m.group(1) + "_Destruct"
    m = re.match(r"^(\w+)::(\w+)$", name)
    if m and m.group(1) == m.group(2):
        return m.group(1) + "_Construct"
    name = re.sub(r"\W", "_", name)
    return name


def this_call(target, obj, args=()):
    """A __thiscall to target with `this` = obj (an address, or C++ text), and stack arguments (raw words, first
    argument first)."""
    obj_text = hexw(obj) if isinstance(obj, int) else obj
    if target in TARGETS:
        _, cls, method = TARGETS[target]
        types = METHOD_ARGS.get((cls, method), [])
        assert len(types) == len(args), (cls, method)
        return "((%s *)%s)->%s(%s)" % (cls, obj_text, method, ", ".join(map(argument, types, args)))
    types = ARG_TYPES.get(target, ["uint32_t"] * len(args))
    THIS_CALLS.setdefault(target, types)
    return "%s((void *)%s, 0%s)" % (callee(target), obj_text, "".join(", " + argument(t, a) for t, a in zip(types, args)))


THIS_CALLS = {}
ERASERS = set()
RETURNS_POINTER = set()   # a list's constructor helper, returning the head node it allocates


def argument(type_, value):
    """A stack argument's word as C++ of the parameter's type."""
    if type_ == "float":
        return float_literal(struct.unpack("<f", struct.pack("<I", value))[0], False)
    if type_.endswith("*"):
        if value == 0:
            return "NULL"
        text = read(value, 64).split(b"\0")[0] if "char" in type_ else b""
        printable = text and all(32 <= c < 127 for c in text) and b'"' not in text
        if printable and RDATA <= value < DATA:   # a string constant: ours
            return '"%s"' % text.decode() if type_.startswith("const") else '(%s)"%s"' % (type_, text.decode())
        comment = ' /* "%s" */' % text.decode() if printable else ""
        return "(%s)%s%s" % (type_, hexw(value), comment)
    if type_ in ("int", "int32_t"):
        return str(value - (1 << 32) if value >= 0x80000000 else value)
    return hexw(value)


def destructor(address):
    """The C++ body of a destructor an initialiser registers with atexit."""
    if address in HAND_DTORS:
        return HAND_DTORS[address]
    ins = instructions(address)
    ops = [(i.mnemonic, i.operands) for i in ins]
    # a std::list's: erase(begin, end), free the head node, clear head and size (see DestroyNodeList)
    if [i.mnemonic for i in ins] == ["push", "mov", "push", "xor", "cmp", "jne", "xor", "jmp", "mov", "push", "push",
                                     "lea", "push", "mov", "call", "mov", "cmp", "je", "push", "push", "call", "add",
                                     "mov", "mov", "pop", "pop", "ret"] and ins[20].operands[0].imm == FAST_FREE:
        list_address, erase, node = ins[13].operands[1].imm, ins[14].operands[0].imm, ins[18].operands[0].imm
        assert ins[1].operands[1].mem.disp == list_address + 4
        ERASERS.add(erase)
        return "DestroyNodeList((NodeList *)0x%08x, %s, 0x%x);" % (list_address, callee(erase), node)
    # mov ecx, obj ; jmp dtor
    if len(ins) == 2 and ins[0].mnemonic == "mov" and ins[1].mnemonic == "jmp":
        if ins[1].operands[0].imm == EMPTY:
            return ""
        return this_call(ins[1].operands[0].imm, ins[0].operands[1].imm) + ";"
    if len(ins) == 1 and ins[0].mnemonic == "ret":
        return ""
    # mov dword [obj], vtable ; ret: an object initialised in .data whose destructor only resets its vtable
    if [m for m, _ in ops] == ["mov", "ret"] and ins[0].operands[0].type == X86_OP_MEM:
        return "U32_AT(0x%08x) = 0x%08x;" % (ins[0].operands[0].mem.disp, ins[0].operands[1].imm)
    # push dtor ; push count ; push size ; push array ; call ??_M ; ret
    if [m for m, _ in ops] == ["push"] * 4 + ["call", "ret"] and ins[4].operands[0].imm == VEC_DTOR:
        dtor, count, size, array = [i.operands[0].imm for i in ins[:4]]
        if dtor == EMPTY:
            return ""
        return "for (int i = %d; i >= 0; i--) %s;" % (count - 1, this_call(dtor, "(0x%08x + i * 0x%x)" % (array, size)))
    return None


def emit(address):
    """The C++ for one initialiser, as a list of lines."""
    if address in HAND:
        return [HAND[address]]
    ins = instructions(address)
    mn = [i.mnemonic for i in ins]
    # fld a ; f<op> b|st(0) ; fstp d ; ret
    if len(ins) == 4 and mn[0] == "fld" and mn[2] == "fstp" and mn[3] == "ret" and \
            mn[1] in ("fadd", "fsub", "fmul", "fdiv"):
        double = ins[0].operands[0].size == 8
        a = ins[0].operands[0].mem.disp
        d = ins[2].operands[0].mem.disp
        op = {"fadd": "+", "fsub": "-", "fmul": "*", "fdiv": "/"}[mn[1]]
        left = operand(a, double)
        right = left if ins[1].operands[0].type == X86_OP_REG else operand(ins[1].operands[0].mem.disp, double)
        return ["%s(0x%08x) = %s %s %s;" % ("DOUBLE_AT" if double else "FLOAT_AT", d, left, op, right)]
    # fld a ; f<op> b ; call __ftol2 ; mov [d], eax ; ret
    if len(ins) == 5 and mn[0] == "fld" and mn[2] == "call" and ins[2].operands[0].imm == FTOL:
        op = {"fmul": "*", "fdiv": "/"}[mn[1]]
        a, b, d = ins[0].operands[0].mem.disp, ins[1].operands[0].mem.disp, ins[3].operands[0].mem.disp
        return ["I32_AT(0x%08x) = Ftol((double)%s %s %s);" % (d, operand(a, False), op, operand(b, False))]
    # push colour ; call ColourConvertXBoxToPS2 ; add esp, 4 ; mov [d], eax ; ret
    if mn == ["push", "call", "add", "mov", "ret"] and ins[1].operands[0].imm == COLOUR:
        colour = ins[0].operands[0].imm & 0xffffffff
        return ["U32_AT(0x%08x) = ColourConvertXBoxToPS2(0x%08x);" % (ins[3].operands[0].mem.disp, colour)]
    # mov dword [d], imm ; ret
    if mn == ["mov", "ret"]:
        d, v = ins[0].operands[0].mem.disp, ins[0].operands[1].imm & 0xffffffff
        return ["U32_AT(0x%08x) = 0x%08x;" % (d, v)]
    # mov eax, [s] ; mov [d], eax ; ret   (and the four-word copy)
    if all(m == "mov" for m in mn[:-1]) and mn[-1] == "ret" and \
            all(i.operands[0].type in (X86_OP_REG, X86_OP_MEM) for i in ins[:-1]):
        regs, out = {}, []
        for i in ins[:-1]:
            dst, src = i.operands
            if dst.type == X86_OP_REG:
                regs[dst.reg] = src.mem.disp
            else:
                out.append("U32_AT(0x%08x) = U32_AT(0x%08x);" % (dst.mem.disp, regs[src.reg]))
        return out
    # push edi ; mov ecx, n ; xor eax, eax ; mov edi, d ; rep stosd ; pop edi ; ret
    if mn == ["push", "mov", "xor", "mov", "rep stosd", "pop", "ret"]:
        n, d = ins[1].operands[1].imm, ins[3].operands[1].imm
        return ["memset((void *)0x%08x, 0, 0x%x);" % (d, n * 4)]
    # push esi ; push edi ; mov ecx, n ; mov esi, s ; mov edi, d ; [push dtor] ; rep movsd ; [call atexit ...]
    if mn[:5] == ["push", "push", "mov", "mov", "mov"] and "rep movsd" in mn:
        n, s, d = ins[2].operands[1].imm, ins[3].operands[1].imm, ins[4].operands[1].imm
        out = ["memcpy((void *)0x%08x, %s, 0x%x);" % (d, source_block(s, n * 4), n * 4)]
        if "call" in mn:
            push = ins[mn.index("rep movsd") - 1] if mn[mn.index("rep movsd") - 1] == "push" else ins[5]
            out.append(atexit_line(push.operands[0].imm))
        return out
    # mov eax, d ; mov ecx, n ; lea ; [xor edx, edx ; lea] ; mov [eax+k], 0|edx ... ; add eax, stride ; dec ; jne
    if mn[0] == "mov" and mn[1] == "mov" and "dec" in mn and "jne" in mn:
        base, count = ins[0].operands[1].imm, ins[1].operands[1].imm
        stores = [i for i in ins if i.mnemonic == "mov" and i.operands[0].type == X86_OP_MEM]
        stride = [i for i in ins if i.mnemonic == "add"][0].operands[1].imm
        offsets = [s.operands[0].mem.disp for s in stores]
        if offsets == list(range(min(offsets), min(offsets) + 4 * len(offsets), 4)) and stride == 4 * len(offsets):
            return ["memset((void *)0x%08x, 0, 0x%x);" % (base + min(offsets), count * stride)]
        assert len(offsets) == 1 and offsets[0] == 0
        return ["for (int i = 0; i < %d; i++) U32_AT(0x%08x + i * 0x%x) = 0;" % (count, base, stride)]
    # constructors: [push args] ; mov ecx, obj ; [mov [x], imm] ; call ctor ; [push dtor ; mov [o+4], eax ; ...] ;
    # push dtor ; call atexit ; pop ecx ; ret
    calls = [i for i in ins if i.mnemonic == "call"]
    if calls and calls[-1].operands[0].imm == ATEXIT:
        return constructor(ins)
    raise SystemExit("0x%08x: an initialiser of a shape this script does not know:\n  %s" %
                     (address, "\n  ".join("%s %s" % (i.mnemonic, i.op_str) for i in ins)))


def source_block(address, size):
    """The source of a block copy: the global, or a literal for a constant block in .rdata."""
    if DATA <= address:
        return "(const void *)0x%08x" % address
    floats = struct.unpack("<%df" % (size // 4), read(address, size))
    BLOCKS.append((address, floats))
    return "kRdata_%08x" % address


BLOCKS = []


def atexit_line(dtor):
    body = destructor(dtor)
    if body is None:
        raise SystemExit("0x%08x: a destructor of a shape this script does not know" % dtor)
    return "CrtAtExit([] { %s });" % body if body else "CrtAtExit([] {});"


def constructor(ins):
    out = []
    pushed, ecx, i = [], None, 0
    calls = [k for k, x in enumerate(ins) if x.mnemonic == "call"]
    # everything before the atexit's push
    atexit_at = calls[-1]
    dtor = ins[atexit_at - 1].operands[0].imm if ins[atexit_at - 1].mnemonic == "push" else None
    if dtor is None:   # push dtor ; mov [x], eax ; mov [y], 0 ; call atexit
        k = atexit_at - 1
        while ins[k].mnemonic != "push":
            k -= 1
        dtor = ins[k].operands[0].imm
    result_stores = []
    for k, x in enumerate(ins[:atexit_at]):
        if x.mnemonic == "push" and x.operands[0].type == X86_OP_IMM and x.operands[0].imm == dtor:
            continue
        if x.mnemonic == "push":
            pushed.append(x.operands[0].imm & 0xffffffff)
        elif x.mnemonic == "mov" and x.operands[0].type == X86_OP_REG and x.operands[0].reg == capstone.x86.X86_REG_ECX:
            ecx = x.operands[1].imm
        elif x.mnemonic == "mov" and x.operands[0].type == X86_OP_MEM:
            src = x.operands[1]
            if src.type == X86_OP_IMM:
                if k < calls[0]:
                    out.append("U32_AT(0x%08x) = 0x%08x;" % (x.operands[0].mem.disp, src.imm & 0xffffffff))
                else:
                    result_stores.append("U32_AT(0x%08x) = 0x%08x;" % (x.operands[0].mem.disp, src.imm & 0xffffffff))
            else:
                result_stores.append(("result", x.operands[0].mem.disp))
        elif x.mnemonic == "call":
            target = x.operands[0].imm
            if target == VEC_CTOR:
                array, size, count, ctor, vdtor = pushed[-1], pushed[-2], pushed[-3], pushed[-4], pushed[-5]
                out.append("for (int i = 0; i < %d; i++) %s;" % (count, this_call(ctor, "(0x%08x + i * 0x%x)" % (array, size))))
            else:
                args = list(reversed(pushed))
                call = this_call(target, ecx, args)
                if any(isinstance(r, tuple) for r in result_stores) or k + 1 < atexit_at and \
                        any(y.mnemonic == "mov" and y.operands[1].type == X86_OP_REG for y in ins[k + 1:atexit_at]):
                    # the call's result is stored (a list's head node): mov [o+4], eax
                    for y in ins[k + 1:atexit_at]:
                        if y.mnemonic == "mov" and y.operands[0].type == X86_OP_MEM and y.operands[1].type == X86_OP_REG:
                            call = "PTR_AT(0x%08x) = %s" % (y.operands[0].mem.disp, call)
                            RETURNS_POINTER.add(target)
                out.append(call + ";")
            pushed = []
    out += [r for r in result_stores if isinstance(r, str)]
    out.append(atexit_line(dtor))
    return out


def weapon_table():
    """The weapon table after its initialiser: the XBE's .data (record 0 is initialised data) under the
    initialiser's stores, which this emulates (immediates through registers)."""
    table = bytearray((read(WEAPONS, WEAPON_SIZE * WEAPON_COUNT) or b"").ljust(WEAPON_SIZE * WEAPON_COUNT, b"\0"))
    regs = {}
    for x in instructions(WEAPON_INIT, 0x1200):
        if x.mnemonic == "xor":
            regs[x.operands[0].reg] = 0
        elif x.mnemonic == "mov" and x.operands[0].type == X86_OP_REG:
            src = x.operands[1]
            regs[x.operands[0].reg] = regs[src.reg] if src.type == X86_OP_REG else src.imm & 0xffffffff
        elif x.mnemonic == "mov":
            dst, src = x.operands
            if src.type == X86_OP_IMM:
                value = src.imm & 0xffffffff
            else:
                full = {capstone.x86.X86_REG_AL: capstone.x86.X86_REG_EAX, capstone.x86.X86_REG_CL: capstone.x86.X86_REG_ECX,
                        capstone.x86.X86_REG_DL: capstone.x86.X86_REG_EDX, capstone.x86.X86_REG_BL: capstone.x86.X86_REG_EBX}
                value = regs[full.get(src.reg, src.reg)]
            offset = dst.mem.disp - WEAPONS
            assert 0 <= offset < len(table), hex(dst.mem.disp)
            if dst.size == 1:
                table[offset] = value & 0xff
            else:
                table[offset:offset + 4] = struct.pack("<I", value)
        elif x.mnemonic not in ("push", "pop", "ret", "nop"):
            raise SystemExit("weapon table: unexpected %s %s" % (x.mnemonic, x.op_str))
    return table


def c_string(address):
    text = read(address, 64).split(b"\0")[0].decode()
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def weapon_lines():
    table = weapon_table()
    lines = []
    for r in range(WEAPON_COUNT):
        base = r * WEAPON_SIZE
        assert table[base + 0x39: base + 0x3c] == b"\0\0\0", r
        (wid, name, t0, t1, t2, flags, u18, u1c, u20, x0, x1, x2, x3, u34, u38, u3c, u40, asset, u48, u4c, u50) = \
            struct.unpack_from("<II3IIIII4iIBxxxIII3I", table, base)

        def num(v):
            return str(v - (1 << 32) if v >= 0x80000000 else v)
        lines.append("    { %d, %s, { 0x%x, 0x%x, 0x%x }, %s, %s, %s, %s, { %s, %s, %s, %s }, %s, %d, %s, %s, %s, "
                     "{ %s, %s, %s } }," %
                     (wid, c_string(name), t0, t1, t2, num(flags), num(u18), num(u1c), num(u20), num(x0), num(x1),
                      num(x2), num(x3), num(u34), u38, num(u3c), num(u40), c_string(asset) if asset else "NULL",
                      num(u48), num(u4c), num(u50)))
    return lines


def main():
    words = struct.unpack("<%dI" % ((TABLE_END - TABLE) // 4), read(TABLE, TABLE_END - TABLE))
    body = []
    for address in words:
        if address in (0, 0xffffffff):
            continue
        if DSOUND[0] <= address < DSOUND[1]:
            body.append("    // 0x%08x: DirectSound's, left out (every DSOUND entry point is the seam's)" % address)
            continue
        assert TEXT_INIT <= address < TEXT_INIT_END, hex(address)
        lines = emit(address)
        body.append("    %-100s // 0x%08x" % (lines[0], address))
        body += ["    " + line for line in lines[1:]]

    head = ['// Generated by tools/static_init_driving.py from Driving.xbe - do not edit; change the script and rerun it.',
            '//',
            '// The driving engine\'s C++ static initialisers, in place of the initialiser table (0x001b3db0) the C runtime',
            '// walked: every entry, in table order, each commented with the original it replaces. The script\'s docstring',
            '// says how each kind is turned into C++.',
            '',
            '#include <stdint.h>',
            '#include <string.h>',
            '',
            '#include "../../helpers.h"',
            '#include "../platform/X87.h"',
            '#include "StaticInit.h"',
            '#include "ActionQueue.hpp"',
            '#include "../eagl/GeoPrimState.h"',
            '#include "../eagl/Model.h"',
            '#include "../eagl/RenderMethod.h"',
            '#include "../eagl/Loader.h"',
            '#include "../eagl/Profiler.h"',
            '',
            '// ---- the originals the initialisers still call, by address',
            '',
            '#define CrtAtExit ((int (__cdecl *)(void (__cdecl *)(void)))0x%08x)   // atexit' % ATEXIT,
            '#define ColourConvertXBoxToPS2 ((uint32_t (__cdecl *)(uint32_t))0x%08x)' % COLOUR]
    for target in sorted(NAMES):
        if target in ERASERS:
            head.append("#define %s ((void (__fastcall *)(NodeList *, int, void **, void *, void *))0x%08x)   "
                        "// erase(result, first, last)" % (NAMES[target], target))
            continue
        types = THIS_CALLS.get(target, [])
        head.append("#define %s ((%s (__fastcall *)(void *, int%s))0x%08x)" %
                    (NAMES[target], "void *" if target in RETURNS_POINTER else "void",
                     "".join(", " + t for t in types), target))
    head.append("")
    if BLOCKS:
        head.append("// ---- constant blocks the initialisers copied from .rdata")
        head.append("")
        for address, floats in BLOCKS:
            head.append("static const float kRdata_%08x[%d] = {" % (address, len(floats)))
            for k in range(0, len(floats), 4):
                head.append("    " + ", ".join(float_literal(f, False) for f in floats[k:k + 4]) + ",")
            head.append("};")
        head.append("")
    head.append("// ---- the weapon table (0x%08x), as its initialiser (0x%08x) left it" % (WEAPONS, WEAPON_INIT))
    head.append("")
    head.append("const WeaponDefinition kWeaponDefinitions[%d] = {" % WEAPON_COUNT)
    head += weapon_lines()
    head.append("};")
    head.append("")
    head.append("void RunStaticInitialisers(void) {")
    text = "\n".join(head + body + ["}", ""])

    if "--check" in sys.argv:
        current = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if current != text:
            raise SystemExit("%s is out of date: run python tools/static_init_driving.py" % os.path.relpath(OUT, REPO))
        print("up to date")
        return
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", os.path.relpath(OUT, REPO), "-", len(body), "entries")


if __name__ == "__main__":
    main()
