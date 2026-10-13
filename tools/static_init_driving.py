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
  - ColourConvertXBoxToPS2 of a constant colour (into our variable, by name, when the global is ours: the script
    does not follow the conversion's value, so a later copy of it is by name too), and float products converted
    with __ftol2;
  - constructors of global objects, with the destructor registered with the C runtime's atexit as the original
    did. Constructors and destructors we own are called directly (TARGETS below); the rest by address. The C
    runtime's exit never runs (nothing calls it), so the destructors never do either: they are registered so that
    the runtime's heap sees the same allocations as before.
  - the driving engine's weapon table (0x001c94d8, 32 records), which one 4.4 KB initialiser built a store at a
    time, as a typed table (WeaponRecord in src/driving/game/WeaponManager.h).

An entry of any other shape stops the script: it is a new kind, to be added here or to HAND below.

Globals we own (an XBE_GLOBAL tag above our own definition, see docs/global-coverage.md) are read from the source
each run. An object of ours the initialisers construct is named in NAMED below and constructed there. Any other
store into an owned global is left out, with a comment giving the value it stored: the owner's definition starts
the variable with that value (the script checks the initialisers it can read and lists the rest). An owned global
an initialiser reads is replaced by the value it holds at that point, which the script follows from the XBE's data
and the earlier entries. Rerun the script whenever globals the table touches become ours.
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
    0x00117330: ("method", "FileNameList", "Construct"),
    0x00117350: ("method", "FileNameList", "Destruct"),
    0x00119e40: ("method", "CARP::ResolverMap", "Construct"),
    0x00119e80: ("method", "CARP::ResolverMap", "Destruct"),
    0x00119f50: ("method", "StringToNumber", "Construct"),
    0x0011a020: ("method", "StringToNumber", "Destruct"),
    0x0008d420: ("method", "RRenderWorldCulling", "Construct"),
    0x0011c210: ("method", "UVolatileMaterial", "Construct"),
    0x00037640: ("method", "DAudioTargetList", "Construct"),
    0x00037660: ("method", "DAudioTargetList", "Destruct"),
    0x00037940: ("method", "GameStd::Locale", "Destruct"),
    0x000b4fe0: ("method", "Simulation", "Construct"),
    0x000b5310: ("method", "Simulation", "Destruct"),
    0x000b8490: ("method", "MissionList", "BuyHead"),           # static: it does not read `this`
    0x000e2c20: ("method", "LoadScreenStringList", "NewHead"),  # static: it does not read `this`
    0x0002bd40: ("method", "AIGroundVehicle", "Construct"),
    0x0002be70: ("method", "AIGroundVehicle", "Destruct"),
    0x00036a00: ("method", "AIZone", "Construct"),
    0x00130c99: ("method", "GameStd::InitLocks", "Construct"),
    0x00130cc7: ("method", "GameStd::InitLocks", "Destruct"),
    0x00131cd0: ("method", "GameStd::InitAtExit", "Destruct"),   # a thunk to the destructor
    0x00013540: ("method", "PointerList", "Destruct"),
    0x0007c6e0: ("method", "TextureDofState", "Construct"),
    0x0007c7b0: ("method", "TextureDofState", "Destruct"),
    0x000c5990: ("method", "WGridMoverList", "BuyHead"),
    0x000cebc0: ("method", "WTargetPicker", "Construct"),
    0x0011cc10: ("method", "MixList", "Construct"),
    0x00124710: ("method", "AVoiceMap", "Construct"),
    0x00124750: ("method", "AVoiceMap", "Destruct"),
    0x00125db0: ("method", "ABank", "Construct"),
    0x00125ef0: ("method", "ABank", "Destruct"),
}
# The lists' erase(first, last) the destructors hand DestroyNodeList, by address: ours, called as the original was
# (this in ECX through the __fastcall pointer, the rest on the stack, popped by the callee).
ERASER_TARGETS = {
    0x00037160: "DAudioTargetList::Erase",
    0x000ce900: "PointerList::Erase",
    0x000e2c80: "LoadScreenStringList::Erase",
}
# Objects the initialisers construct that our code names, by address: the name and its class. A constructor or
# destructor of that class is called on the name; for the globals we own (XBE_GLOBAL) this is what makes the
# initialisers write our variable rather than the game's copy.
NAMED = {
    0x001e23e0: ("gActionNames", "StringToNumber"),            # engine/InputConfig.cpp
    0x001e4590: ("gUpdateMethodNames", "StringToNumber"),      # engine/InputConfig.cpp
    0x001e4870: ("GlobalActionQueue", "ActionQueue"),          # engine/GameLoop.cpp
    0x00243508: ("RequestedFiles", "FileNameList"),            # engine/UFileLoader.cpp
    0x00243514: ("FilesInBigFile", "FileNameList"),
    0x002434e4: ("FilesNotInBigFile", "FileNameList"),
    0x00243580: ("CARP::Resolvers", "CARP::ResolverMap"),      # data/Carp.cpp
    0x001f2c80: ("fgWorldCulling", "RRenderWorldCulling"),     # render/WorldCulling.h
    0x001e2278: ("DAudioTargets", "DAudioTargetList"),         # game/DebugAudio.cpp
    0x00200d30: ("EmpSphereState", "EAGL::GeoPrimState"),      # game/EmpEffect.cpp
    0x00200e30: ("EmpRingState", "EAGL::GeoPrimState"),
    0x002012a0: ("MissileStreakState", "EAGL::GeoPrimState"),  # game/WeaponEffects.cpp
    0x002016d0: ("MuzzleFlashPOVMaterial", "UVolatileMaterial"),
    0x00233ff0: ("fgSimulation", "Simulation"),                # game/Simulation.h
    0x0024483c: ("GameStd::ClassicLocale", "GameStd::Locale"),  # platform/CRuntimeStd.cpp
    0x001ff7f0: ("DecalState", "EAGL::GeoPrimState"),          # render/Decals.h
    0x00209cc0: ("GlareMaterial", "UVolatileMaterial"),        # render/RGlareManager.hpp
    0x00230540: ("SpriteMaterial", "UVolatileMaterial"),       # render/ParticleCache.h
    0x002305a0: ("StreakMaterial", "UVolatileMaterial"),
    0x0023b3ac: ("GridMovers", "WGridMoverList"),              # world/Grid.h
    0x0023e1b0: ("TargetPicker", "WTargetPicker"),             # world/Targeting.h
    0x0023fb8c: ("GlobalPool", "SymbolPool"),                  # eagl/EaglGlobals.h
    0x0023fbb8: ("TheRuntimeAllocPool", "RuntimeAllocConstructorPool"),
    0x0023fbe0: ("TheConstructorPool", "ConstructorPool"),
    0x002400f8: ("FontState", "EAGL::GeoPrimStateExtension"),  # eagl/EaglFont.h
    0x00240560: ("FrameRegion", "EAGL::ProfilerRegion"),      # eagl/Profiler.h
    0x00241460: ("TextureDofMethod", "EAGL::RenderMethod"),    # camera/DirectorQueue.h
    0x00242de8: ("TexturedParentMethod", "EAGL::RenderMethod"),  # render/Decals.h
    0x00242e1c: ("EmpParentMethod", "EAGL::RenderMethod"),
    0x00242e50: ("GainParentMethod", "EAGL::RenderMethod"),    # render/Gain.h
    0x00242e84: ("BondPostGlare", "EAGL::RenderMethod"),       # render/PostProcessing.h
    0x0024b07c: ("MatDebugMethod", "EAGL::RenderMethod"),      # render/Materials.h
    0x0024b0b0: ("MatVolatileTextureMethod", "EAGL::RenderMethod"),
    0x00243954: ("Mixes", "MixList"),                          # audio/Mix.h
    0x00243a9c: ("VoiceInfoMap", "AVoiceMap"),                 # audio/Voice.h
    0x00243aa8: ("ActiveViews", "AVoiceViewList"),
}
# Owned arrays of objects the initialisers construct (a vector constructor) and destroy: the name and the class.
NAMED_OBJECT_ARRAYS = {
    0x001e1f20: ("AIZoneData.zones", "AIZone"),                   # game/AIElements.h
    0x001f5f10: ("BulletStreakMaterials", "UVolatileMaterial"),   # game/WeaponEffects.cpp
    0x002014f0: ("MuzzleFlashMaterials", "UVolatileMaterial"),
    0x00233e20: ("TrackMaterials", "UVolatileMaterial"),          # game/TyreTrack.cpp
}
# Owned arrays of records that a loop of stores clears, by the first store's address: the name, and the field
# each store writes.
NAMED_ARRAYS = {
    0x001e4488: ("gFeedbackPorts", "users"),                   # engine/Feedback.cpp, FeedbackPort[4]
    0x001e84fc: ("RayShellFlashes", "ownerSig"),               # game/Shell.cpp, RayShellFlash[8] (the field at +0xc)
    0x001e8570: ("ActiveRayShells", "ownerSignature"),         # game/Shell.cpp, ActiveRayShell[64]
}
# Block copies into owned globals, by destination address: the C++ lvalue copied into.
NAMED_BLOCKS = {
    0x001ddab0: "HandsStatics.baseTransform",                 # game/AIHands.h
}
# Stores into fields of owned globals that start as zero, by address: the C++ lvalue. They are made in table order,
# as the original made them, rather than left to the owner's initialiser.
NAMED_STORES = {
    0x001e1f1c: "AIZoneData.unknown34",                       # game/AIElements.h
    0x001dda9c: "HandsStatics.unknown2c[0]",                  # game/AIHands.h
    0x001ddaa0: "HandsStatics.unknown2c[1]",
    0x001ddaa4: "HandsStatics.unknown2c[2]",
    0x001ddaa8: "HandsStatics.unknown2c[3]",
    0x001ddaac: "HandsStatics.unknown2c[4]",
    0x001ddb2c: "HandsStatics.cameraOffset.w",
    0x00233fb8: "RigidPlayerGravity",                         # physics/RigidBody.h
    0x0023b3b0: "GridMovers.head",                            # world/Grid.h
    0x00243aac: "ActiveViews.head",                           # audio/Voice.h
    0x0023b3b4: "GridMovers.size",
    0x00243ab0: "ActiveViews.size",
    0x00243958: "Mixes.head",                                 # audio/Mix.h
    0x0024395c: "Mixes.size",
}
# Argument types of the originals' stack arguments, where they are not plain words.
ARG_TYPES = {
    0x0008d420: ["float", "float"],                     # RRenderWorldCulling(near, far)
}
# Our ports' argument types for the constructors called with arguments.
METHOD_ARGS = {
    ("ActionQueue", "Construct"): ["char *"],
    ("EAGL::ProfilerRegion", "Construct"): ["const char *", "uint32_t"],
    ("StringToNumber", "Construct"): ["StringToNumberEntry *"],
    ("RRenderWorldCulling", "Construct"): ["float", "float"],
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
OWNED = [g for g in gc.read_source()[0] if g["owned"]]
WRITTEN = {}    # address -> byte: what the entries so far have stored
LEFT_OUT = []   # (global, the value it starts with as C++ text, the value's bytes)


def owned_global(address, size):
    """The owned global the range falls in, or None; a range that only partly covers one is a new case."""
    for g in OWNED:
        if g["address"] < address + size and address < g["address"] + g["size"]:
            if not (g["address"] <= address and address + size <= g["address"] + g["size"]):
                raise SystemExit("0x%08x+0x%x covers part of %s: a case this script does not know" %
                                 (address, size, g["name"]))
            return g
    return None


def value_bytes(address, size):
    """The bytes at the address when the entry runs: the last entry's store, or the XBE's data; None if an entry
    the script does not follow wrote them."""
    initial = (read(address, size) or b"").ljust(size, b"\0")
    data = [WRITTEN.get(address + k, initial[k]) for k in range(size)]
    return None if None in data else bytes(data)


def known(data, what):
    if data is None:
        raise SystemExit("%s: a value this script does not follow" % what)
    return data


def store(address, data, size=4):
    for k in range(size if data is None else len(data)):
        WRITTEN[address + k] = None if data is None else data[k]


def left_out(g, text, data):
    """The comment standing for a store into an owned global."""
    LEFT_OUT.append((g, text, data))
    return "// %s = %s: ours (%s)" % (g["name"], text, g["file"].replace("src/driving/", ""))


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
    """A float operand: a literal when it is a constant in .rdata or a global we own (the value it holds then),
    otherwise the global."""
    if RDATA <= address < DATA or owned_global(address, 8 if double else 4):
        known(value_bytes(address, 8 if double else 4), "0x%08x" % address)
        return float_literal(operand_value(address, double), double)
    return "%s(0x%08x)" % ("DOUBLE_AT" if double else "FLOAT_AT", address)


def operand_value(address, double):
    data = value_bytes(address, 8 if double else 4)
    return None if data is None else struct.unpack("<d" if double else "<f", data)[0]


def float_bytes(value, double):
    return struct.pack("<d" if double else "<f", value)


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


def array_element(array, size, count):
    """The owned array of objects at `array` (NAMED_OBJECT_ARRAYS), or None for one at the game's address."""
    if array in NAMED_OBJECT_ARRAYS:
        return NAMED_OBJECT_ARRAYS[array]
    if owned_global(array, size * count):
        raise SystemExit("0x%08x: objects of %s, which is ours: add it to NAMED_OBJECT_ARRAYS" %
                         (array, owned_global(array, size * count)["name"]))
    return None


def this_call(target, obj, args=(), element=None):
    """A __thiscall to target with `this` = obj (an address, or C++ text), and stack arguments (raw words, first
    argument first). `element` names element i of an owned array of objects (NAMED_OBJECT_ARRAYS) instead."""
    obj_text = hexw(obj) if isinstance(obj, int) else obj
    if isinstance(obj, int) and obj in NAMED:
        obj_text = "&" + NAMED[obj][0]
    if isinstance(obj, int) and obj not in NAMED and owned_global(obj, 1):
        raise SystemExit("0x%08x: a call on %s, which is ours: add it to NAMED" % (obj, owned_global(obj, 1)["name"]))
    if target in TARGETS:
        _, cls, method = TARGETS[target]
        types = METHOD_ARGS.get((cls, method), [])
        assert len(types) == len(args), (cls, method)
        arguments = ", ".join(map(argument, types, args))
        if element is not None:
            name, named_class = element
            if named_class == cls:
                return "%s[i].%s(%s)" % (name, method, arguments)
            return "((%s *)&%s[i])->%s(%s)" % (cls, name, method, arguments)
        if isinstance(obj, int) and obj in NAMED:
            name, named_class = NAMED[obj]
            if named_class == cls:
                return "%s.%s(%s)" % (name, method, arguments)
            return "((%s *)&%s)->%s(%s)" % (cls, name, method, arguments)
        return "((%s *)%s)->%s(%s)" % (cls, obj_text, method, arguments)
    if element is not None:
        raise SystemExit("an original called on the owned array %s: a case this script does not know" % element[0])
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
        if list_address in NAMED:
            return "DestroyNodeList((NodeList *)&%s, %s, 0x%x);" % (NAMED[list_address][0], callee(erase), node)
        if owned_global(list_address, 12):
            raise SystemExit("0x%08x: a list of %s, which is ours: add it to NAMED" %
                             (list_address, owned_global(list_address, 12)["name"]))
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
        return "for (int i = %d; i >= 0; i--) %s;" % (count - 1, this_call(dtor, "(0x%08x + i * 0x%x)" % (array, size),
                                                                            element=array_element(array, size, count)))
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
        b = a if ins[1].operands[0].type == X86_OP_REG else ins[1].operands[0].mem.disp
        left, right = operand(a, double), operand(b, double)
        x, y = operand_value(a, double), operand_value(b, double)
        data = None if x is None or y is None else \
            float_bytes({"+": x + y, "-": x - y, "*": x * y, "/": x / y}[op], double)
        g = owned_global(d, 8 if double else 4)
        store(d, data, 8 if double else 4)
        if g and d in NAMED_STORES:
            return ["%s = %s %s %s;" % (NAMED_STORES[d], left, op, right)]
        if g:
            known(data, g["name"])
            return [left_out(g, float_literal(struct.unpack("<d" if double else "<f", data)[0], double), data)]
        return ["%s(0x%08x) = %s %s %s;" % ("DOUBLE_AT" if double else "FLOAT_AT", d, left, op, right)]
    # fld a ; f<op> b ; call __ftol2 ; mov [d], eax ; ret
    if len(ins) == 5 and mn[0] == "fld" and mn[2] == "call" and ins[2].operands[0].imm == FTOL:
        op = {"fmul": "*", "fdiv": "/"}[mn[1]]
        a, b, d = ins[0].operands[0].mem.disp, ins[1].operands[0].mem.disp, ins[3].operands[0].mem.disp
        x, y = operand_value(a, False), operand_value(b, False)
        data = None if x is None or y is None else struct.pack("<i", int(x * y if op == "*" else x / y))
        g = owned_global(d, 4)
        store(d, data)
        if g:
            known(data, g["name"])
            return [left_out(g, str(struct.unpack("<i", data)[0]), data)]
        return ["I32_AT(0x%08x) = Ftol((double)%s %s %s);" % (d, operand(a, False), op, operand(b, False))]
    # push colour ; call ColourConvertXBoxToPS2 ; add esp, 4 ; mov [d], eax ; ret
    if mn == ["push", "call", "add", "mov", "ret"] and ins[1].operands[0].imm == COLOUR:   # ours, render/Draw.h
        colour = ins[0].operands[0].imm & 0xffffffff
        g = owned_global(ins[3].operands[0].mem.disp, 4)
        store(ins[3].operands[0].mem.disp, None)   # not followed
        if g:   # ours: converted into our variable, in its place in the order
            return ["%s = ColourConvertXBoxToPS2(0x%08x);" % (g["name"], colour)]
        return ["U32_AT(0x%08x) = ColourConvertXBoxToPS2(0x%08x);" % (ins[3].operands[0].mem.disp, colour)]
    # mov dword [d], imm ; ret
    if mn == ["mov", "ret"]:
        d, v = ins[0].operands[0].mem.disp, ins[0].operands[1].imm & 0xffffffff
        g = owned_global(d, 4)
        store(d, struct.pack("<I", v))
        if g and d in NAMED_STORES:
            return ["%s = 0x%08x;" % (NAMED_STORES[d], v)]
        if g:
            return [left_out(g, "0x%08x" % v, struct.pack("<I", v))]
        return ["U32_AT(0x%08x) = 0x%08x;" % (d, v)]
    # mov eax, [s] ; mov [d], eax ; ret   (and the four-word copy)
    if all(m == "mov" for m in mn[:-1]) and mn[-1] == "ret" and \
            all(i.operands[0].type in (X86_OP_REG, X86_OP_MEM) for i in ins[:-1]):
        regs, out = {}, []
        for i in ins[:-1]:
            dst, src = i.operands
            if dst.type == X86_OP_REG:
                regs[dst.reg] = src.mem.disp
                continue
            source = regs[src.reg]
            data = value_bytes(source, 4)
            g = owned_global(dst.mem.disp, 4)
            store(dst.mem.disp, data)
            source_global = owned_global(source, 4)
            if g and dst.mem.disp in NAMED_STORES:
                out.append("%s = %s;" % (NAMED_STORES[dst.mem.disp], source_global["name"] if source_global else
                                         "U32_AT(0x%08x)" % source))
            elif data is None and source_global:   # a value not followed (a colour converted): copied by name
                out.append("%s = %s;" % (g["name"] if g else "U32_AT(0x%08x)" % dst.mem.disp, source_global["name"]))
            elif g:
                out.append(left_out(g, "0x%08x" % struct.unpack("<I", known(data, g["name"]))[0], data))
            elif source_global:
                known(data, source_global["name"])
                out.append("U32_AT(0x%08x) = 0x%08x;   // %s's value" % (dst.mem.disp, struct.unpack("<I", data)[0],
                                                                          owned_global(source, 4)["name"]))
            else:
                out.append("U32_AT(0x%08x) = U32_AT(0x%08x);" % (dst.mem.disp, source))
        return out
    # push edi ; mov ecx, n ; xor eax, eax ; mov edi, d ; rep stosd ; pop edi ; ret
    if mn == ["push", "mov", "xor", "mov", "rep stosd", "pop", "ret"]:
        n, d = ins[1].operands[1].imm, ins[3].operands[1].imm
        return zero_fill(d, n * 4)
    # push esi ; push edi ; mov ecx, n ; mov esi, s ; mov edi, d ; [push dtor] ; rep movsd ; [call atexit ...]
    if mn[:5] == ["push", "push", "mov", "mov", "mov"] and "rep movsd" in mn:
        n, s, d = ins[2].operands[1].imm, ins[3].operands[1].imm, ins[4].operands[1].imm
        if (owned_global(d, n * 4) and d not in NAMED_BLOCKS) or owned_global(s, n * 4):
            raise SystemExit("0x%08x: a block copy to or from an owned global: add it to NAMED_BLOCKS" % d)
        store(d, value_bytes(s, n * 4), n * 4)
        target = "&" + NAMED_BLOCKS[d] if d in NAMED_BLOCKS else "(void *)0x%08x" % d
        out = ["memcpy(%s, %s, 0x%x);" % (target, source_block(s, n * 4), n * 4)]
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
            return zero_fill(base + min(offsets), count * stride)
        assert len(offsets) == 1 and offsets[0] == 0
        for i in range(count):
            store(base + i * stride, b"\0\0\0\0")
        if base in NAMED_ARRAYS:
            name, field = NAMED_ARRAYS[base]
            return ["for (int i = 0; i < %d; i++) %s[i].%s = 0;" % (count, name, field)]
        if owned_global(base, count * stride):
            raise SystemExit("0x%08x: stores into an owned array not in NAMED_ARRAYS" % base)
        return ["for (int i = 0; i < %d; i++) U32_AT(0x%08x + i * 0x%x) = 0;" % (count, base, stride)]
    # constructors: [push args] ; mov ecx, obj ; [mov [x], imm] ; call ctor ; [push dtor ; mov [o+4], eax ; ...] ;
    # push dtor ; call atexit ; pop ecx ; ret
    calls = [i for i in ins if i.mnemonic == "call"]
    if calls and calls[-1].operands[0].imm == ATEXIT:
        return constructor(ins)
    raise SystemExit("0x%08x: an initialiser of a shape this script does not know:\n  %s" %
                     (address, "\n  ".join("%s %s" % (i.mnemonic, i.op_str) for i in ins)))


def zero_fill(address, size):
    g = owned_global(address, size)
    store(address, bytes(size))
    if g:
        return [left_out(g, "zero" if size == g["size"] else "zero (0x%x bytes)" % size, bytes(size))]
    return ["memset((void *)0x%08x, 0, 0x%x);" % (address, size)]


def check_left_out():
    """Every left-out store's value against the owner's initialiser, where the script can read it."""
    unchecked = []
    for g, text, data in LEFT_OUT:
        line = open(os.path.join(REPO, g["file"]), encoding="utf-8").read().splitlines()[g["line"] - 1]
        m = re.search(r"=\s*([^;{]+);", line.split("//")[0])
        if data == bytes(len(data)) and not m:
            continue   # a zero store into a zero-initialised variable
        try:
            expression = re.sub(r"(\d+\.\d*(?:e[-+]?\d+)?|\d*\.\d+(?:e[-+]?\d+)?)f\b", r"_f(\1)", m.group(1))
            value = eval(expression, {"_f": lambda v: struct.unpack("<f", struct.pack("<f", v))[0]})
        except Exception:
            unchecked.append("%s (%s:%d)" % (g["name"], g["file"], g["line"]))
            continue
        size = len(data)
        if isinstance(value, float) and size in (4, 8):
            ok = float_bytes(value, size == 8) == data or struct.pack("<i", int(value)) == data and \
                "." not in m.group(1)
        else:
            ok = struct.pack("<I", int(value) & 0xffffffff) == data
        if not ok:
            raise SystemExit("%s (%s:%d) starts as %s, not %s as the initialiser it stands for left it" %
                             (g["name"], g["file"], g["line"], m.group(1).strip(), text))
    if unchecked:
        print("left out, initialisers not checked:", ", ".join(unchecked))


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
    return "Crt::atexit([] { %s });" % body if body else "Crt::atexit([] {});"


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
                disp = x.operands[0].mem.disp
                if disp in NAMED_STORES:
                    line = "%s = 0x%08x;" % (NAMED_STORES[disp], src.imm & 0xffffffff)
                elif owned_global(disp, 4):
                    raise SystemExit("0x%08x: a store into %s, which is ours: add it to NAMED_STORES" %
                                     (disp, owned_global(disp, 4)["name"]))
                else:
                    line = "U32_AT(0x%08x) = 0x%08x;" % (disp, src.imm & 0xffffffff)
                if k < calls[0]:
                    out.append(line)
                else:
                    result_stores.append(line)
            else:
                result_stores.append(("result", x.operands[0].mem.disp))
        elif x.mnemonic == "call":
            target = x.operands[0].imm
            if target == VEC_CTOR:
                array, size, count, ctor, vdtor = pushed[-1], pushed[-2], pushed[-3], pushed[-4], pushed[-5]
                out.append("for (int i = 0; i < %d; i++) %s;" % (count, this_call(ctor, "(0x%08x + i * 0x%x)" % (array, size),
                                                                                  element=array_element(array, size, count))))
            else:
                args = list(reversed(pushed))
                call = this_call(target, ecx, args)
                if any(isinstance(r, tuple) for r in result_stores) or k + 1 < atexit_at and \
                        any(y.mnemonic == "mov" and y.operands[1].type == X86_OP_REG for y in ins[k + 1:atexit_at]):
                    # the call's result is stored (a list's head node): mov [o+4], eax
                    for y in ins[k + 1:atexit_at]:
                        if y.mnemonic == "mov" and y.operands[0].type == X86_OP_MEM and y.operands[1].type == X86_OP_REG:
                            disp = y.operands[0].mem.disp
                            if disp in NAMED_STORES:
                                call = "%s = %s" % (NAMED_STORES[disp], call)
                            elif owned_global(disp, 4):
                                raise SystemExit("0x%08x: a result stored into %s, which is ours: add it to "
                                                 "NAMED_STORES" % (disp, owned_global(disp, 4)["name"]))
                            else:
                                call = "PTR_AT(0x%08x) = %s" % (disp, call)
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
        lines.append("    { %d, %s, 0x%x, 0x%x, 0x%x, %s, %s, %s, %s, { %s, %s, %s, %s }, %s, %d, %s, %s, %s, "
                     "%s, %s, %s }," %
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
            '#include "../data/Carp.h"',
            '#include "../data/StdStreams.h"',
            '#include "../data/SymbolTable.h"',
            '#include "../game/AIElements.h"',
            '#include "../game/AIVehicle.h"',
            '#include "../game/DebugAudio.h"',
            '#include "../game/EmpEffect.h"',
            '#include "../game/Girl.h"',
            '#include "../game/Hud.h"',
            '#include "../game/LoadingScreen.h"',
            '#include "../game/MissionManager.h"',
            '#include "../game/Shell.h"',
            '#include "../game/Simulation.h"',
            '#include "../game/TyreTrack.h"',
            '#include "../game/WeaponEffects.h"',
            '#include "../render/Draw.h"',
            '#include "../render/Materials.h"',
            '#include "../render/WorldCulling.h"',
            '#include "Feedback.h"',
            '#include "FileNameList.h"',
            '#include "GameLoop.h"',
            '#include "InputConfig.h"',
            '#include "../platform/CRuntime.h"',
            '#include "../platform/CRuntimeStd.h"',
            '#include "../audio/Bank.h"',
            '#include "../audio/Mix.h"',
            '#include "../audio/Voice.h"',
            '#include "../camera/DirectorQueue.h"',
            '#include "../eagl/EaglFont.h"',
            '#include "../eagl/EaglGlobals.h"',
            '#include "../game/AIHands.h"',
            '#include "../physics/RigidBody.h"',
            '#include "../render/Decals.h"',
            '#include "../render/Gain.h"',
            '#include "../render/ParticleCache.h"',
            '#include "../render/PostProcessing.h"',
            '#include "../render/RGlareManager.hpp"',
            '#include "../world/Grid.h"',
            '#include "../world/Targeting.h"',
            '#include "../../common/xbeOverload.h"',
            '',
            '// ---- the originals the initialisers still call, by address',
            '']
    for target in sorted(NAMES):
        if target in ERASER_TARGETS:
            head.append("#define %s reinterpret_cast<NodeListErase>(XbeAddress(&%s))" %
                        (NAMES[target], ERASER_TARGETS[target]))
            continue
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
    head.append("const WeaponRecord kWeaponDefinitions[kWeaponCount] = {")
    head += weapon_lines()
    head.append("};")
    head.append("")
    head.append("void RunStaticInitialisers(void) {")
    text = "\n".join(head + body + ["}", ""])
    check_left_out()

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
