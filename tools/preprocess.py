# Credits: Nightfire Research Team - (2024 - 2025)

import datetime
import json
import os
import re

# Sub-tasks defined in other files for readability
from uihandler import generate_handler_switch

generate_handler_switch()

def parse_signature(line, function_name):
    # The tagged definition's return type and parameter types, names stripped - enough to write the
    # function's type, for picking one overload out of several (see XbeOverload in src/common/xbeOverload.h).
    # Returns None when the line is not a one-line definition this can read, and the caller says so.
    start = line.find(function_name + "(")
    if start < 0:
        return None
    return_type = line[:start].strip()
    for word in ("static", "inline", "extern", "virtual"):
        return_type = re.sub(rf'\b{word}\b', '', return_type).strip()
    depth, open_at = 0, start + len(function_name)
    for i in range(open_at, len(line)):
        depth += {"(": 1, ")": -1}.get(line[i], 0)
        if depth == 0:
            params_text = line[open_at + 1:i]
            break
    else:
        return None
    types = []
    for param in split_params(params_text):
        param = param.split("=")[0].strip()
        if param in ("", "void"):
            continue
        types.append(strip_param_name(param))
    return return_type, types


def split_params(text):
    # Split on top-level commas only - a template argument or function-pointer parameter has its own.
    parts, depth, current = [], 0, ""
    for ch in text:
        if ch in "(<":
            depth += 1
        elif ch in ")>":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += ch
    parts.append(current)
    return parts


TYPE_WORDS = {"void", "bool", "char", "short", "int", "long", "float", "double", "signed", "unsigned"}
QUALIFIERS = {"const", "volatile", "struct", "class", "enum"}

def strip_param_name(param):
    # "char *rawPath" -> "char *", "bool param_3" -> "bool", "unsigned int" -> itself (no name), "const Foo"
    # -> itself (the last word is the type).
    m = re.match(r'^(.*?[\s\*&])\s*([A-Za-z_]\w*)$', param)
    if m is None or m.group(2) in TYPE_WORDS | QUALIFIERS:
        return param
    prefix = m.group(1).strip()
    if prefix == "" or all(word in QUALIFIERS for word in prefix.split()):
        return param
    return prefix


def count_ghidra_params(ghidra_func):
    # Ghidra lists "this" among a method's parameters (as "... this@ECX:4 (auto)"); a C++ definition does not.
    return len([p for p in ghidra_func['parameters'] if ' this@' not in p])


def match_tag(tag, name, signature, ghidra_funcs):
    # One Ghidra function per tag. A C++ name can belong to several - overloads, which Ghidra keeps under one
    # name - so several candidates are narrowed by parameter count, and anything still ambiguous is refused:
    # patching one definition over two functions is how UFileLoader::FileLoad came to read an argument its
    # callers never pushed. See docs/driving-injection-framework.md.
    matching = [x for x in ghidra_funcs if x['name'] == name and x['is_thunk'] == False]
    if not matching:
        # Usually a rename in Ghidra since the tag was written - including Ghidra moving a class's methods to a
        # "Class_conflict1" namespace when a second type of that name appears. Say where the name went.
        short = name.split("::")[-1]
        elsewhere = [f"{x['name']} ({x['address']})" for x in ghidra_funcs if x['name'].split("::")[-1] == short]
        hint = (" - functions with that name elsewhere: " + ", ".join(elsewhere[:6])) if elsewhere else ""
        assert False, f"{tag} {name}: no function of that name in Ghidra's export{hint}"
    if len(matching) == 1:
        return matching[0]
    candidates = ", ".join(f"{x['address']} ({count_ghidra_params(x)} parameters)" for x in matching)
    assert signature is not None, (f"{tag} {name}: several functions have this name ({candidates}) and the "
                                   f"tagged line could not be read to tell them apart - use FUNC_AT(address)")
    by_count = [x for x in matching if count_ghidra_params(x) == len(signature[1])]
    assert len(by_count) == 1, (f"{tag} {name}: {len(signature[1])} parameters matches {len(by_count)} of the "
                                f"functions with this name ({candidates}) - use FUNC_AT(address) to say which")
    return by_count[0]


def overload_expression(name, signature):
    # Selects one function out of our own overloads by its type.
    return_type, types = signature
    return f"XbeOverload<{return_type}({', '.join(types)})>::Of(&{name})"


def load_abi_facts(side):
    # Measured from the binary by tools/abi_facts.py; see src/common/xbeAbi.h for what is checked.
    path = f"tools/abi_{side}.json"
    if not os.path.exists(path):
        print(f"{path} not found - the {side} injection table will not be checked against the binary "
              f"(run tools/abi_facts.py)")
        return None
    return json.load(open(path, 'r'))


def abi_check(abi, address, name, pointer_expression):
    # The compile-time check that a replacement's declaration honours the original's calling convention.
    if abi is None:
        return None
    facts = abi.get("0x%08x" % int(address, 16))
    if facts is None:
        return f"// no measured facts for {name} at {address}"
    what = f"\"{name} at 0x{int(address, 16):08x}\""
    others = [r for r in facts["regs_in"] if r not in ("ecx", "edx")]
    if others:
        at = ", ".join(f"{r} at {facts['first_read'][r]}" for r in others)
        return (f"static_assert(false, {what} \": the original reads {at} on entry - arguments in registers no "
                f"C++ convention uses. Write an adaptor and tag it AUTOLTCG (see View_CaptureScene)\");")
    pops = -1 if facts["pops"] is None else facts["pops"]
    ecx = "true" if "ecx" in facts["regs_in"] else "false"
    edx = "true" if "edx" in facts["regs_in"] else "false"
    return f"XBE_ABI_CHECK(decltype({pointer_expression}), {pops}, {ecx}, {edx}, {what});"


def generate_auto_inject(side, ghidra_funcs):
    injections = []

    autoinjects = gather_functions_with_tag("AUTOINJECT", side=side)
    autoltcg = gather_functions_with_tag("AUTOLTCG", side=side)
    func_ats = gather_functions_with_tag("FUNC_AT", True, side)

    # A name our own code defines more than once has to be selected by type in the generated table, or
    # "&Name" does not compile - or, worse, picks whichever the compiler likes.
    defined = [f[1] for f in autoinjects + autoltcg + func_ats]
    overloaded = {n for n in defined if defined.count(n) > 1}

    abi = load_abi_facts(side)

    def pointer(f):
        if f[1] not in overloaded:
            return f"&{f[1]}"
        assert f[2] is not None, f"{f[1]} is defined more than once, and this definition's line could not be read"
        return overload_expression(f[1], f[2])

    def target(f):
        # XbeAddress rather than (size_t)&: it also takes a member function's address, which a cast cannot.
        return f"XbeAddress({pointer(f)})"

    for f in autoinjects:
        mf = match_tag("AUTOINJECT", f[1], f[2], ghidra_funcs)
        assert mf['has_custom_variable_storage'] == False, f"Function {f[1]} has custom variable storage, to inject this you need to write an assembly wrapper. See View_CaptureScene for an example."
        injections.append((mf['address'], f[1], target(f), abi_check(abi, mf['address'], f[1], pointer(f))))

    for f in autoltcg:
        # "AUTOLTCG" is an acknowledgement that the function is aware of and correctly handles the custom variable storage, so no need to check for it
        # Eventually, we could fix this preprocess script to generate a wrapper automatically, but for now that's up to the code
        mf = match_tag("AUTOLTCG", f[1], f[2], ghidra_funcs)
        injections.append((mf['address'], f[1], target(f), None))

    # FUNC_AT(x) with a given address
    for f in func_ats:
        address = f[0] if f[0].startswith("0x") else f"0x{f[0]}"
        injections.append((address, f[1], target(f), abi_check(abi, address, f[1], pointer(f))))

    # Save the injections to a file
    output_file = f"src/{side}/autogenerated_injections.inc"
    with open(output_file, 'w') as file:
        file.write(generate_injections(injections))
    print(f"Autogenerated injections saved to {output_file}")

    # The table is compiled in inject_<side>.cpp, which has to see each replacement's declaration - for a
    # method, its class. So the companion header of every file carrying a tag (Foo.cpp -> Foo.h or Foo.hpp)
    # is included for it, through autogenerated_injection_includes.inc at the top of that file.
    headers = []
    for f in autoinjects + autoltcg + func_ats:
        base = os.path.splitext(f[3])[0]
        for ext in (".h", ".hpp"):
            if os.path.exists(base + ext):
                include = os.path.relpath(base + ext, "src").replace("\\", "/")
                if include not in headers:
                    headers.append(include)
                break
    write_if_changed(f"src/{side}/autogenerated_injection_includes.inc",
                     "// This file is autogenerated by tools/preprocess.py. Do not modify.\n"
                     "// The headers declaring the functions in autogenerated_injections.inc.\n"
                     + "".join(f'#include "{h}"\n' for h in sorted(headers)))

    return injections

CONVENTIONS = ("__cdecl", "__stdcall", "__fastcall", "__thiscall")


def gather_declaration_tags(tag_name, side):
    # AUTOGEN tags for the driving engine: above a declaration, in a .cpp or a header, possibly inside a class.
    # Returns (file, line number, declaration line, enclosing class or None, address or None) for each. The
    # enclosing class comes from tracking "class X {" / "struct X {" scopes by their braces, which is enough for
    # the one-class-per-header style this code uses.
    found = []
    exclude = {"driving" if side == "action" else "action"}
    for root, dirs, files in os.walk("src"):
        [dirs.remove(d) for d in list(dirs) if d in exclude]
        for name in files:
            if not name.endswith(('.c', '.cpp', '.h', '.hpp')):
                continue
            path = os.path.join(root, name)
            lines = open(path, 'r', encoding='utf-8', errors='replace').readlines()
            scopes, depth, pending = [], 0, None
            for i, line in enumerate(lines):
                code = line.split("//")[0]
                tag = re.search(rf'// {tag_name}(?:\((\w+)\))?\s*$', line)
                if tag and i + 1 < len(lines):
                    enclosing = scopes[-1][0] if scopes else None
                    found.append((path, i + 1, lines[i + 1].strip(), enclosing, tag.group(1)))
                m = re.match(r'^\s*(?:class|struct)\s+(\w+)\b[^;(]*$', code)
                if m:
                    pending = m.group(1)
                for ch in code:
                    if ch == "{":
                        depth += 1
                        if pending is not None:
                            scopes.append((pending, depth))
                            pending = None
                    elif ch == "}":
                        if scopes and scopes[-1][1] == depth:
                            scopes.pop()
                        depth -= 1
    return found


def class_vtables(vtables, cls, seen=None):
    # Every vtable an object of `cls` can have: its own, and those of every class derived from it, transitively
    # (tools/vtables.py says how each is found).
    seen = seen if seen is not None else set()
    if cls in seen or cls not in vtables["classes"]:
        return set()
    seen.add(cls)
    entry = vtables["classes"][cls]
    found = set(entry["own"]) | set(entry["derived_vtables"])
    for sub in entry["derived_classes"]:
        found |= class_vtables(vtables, sub, seen)
    return found


def virtual_body(where, path, cls, is_member, short_name, qualified, bare_return, convention, types, slot,
                 vtables, abi, function_names, side, includes):
    # // VIRTUAL(n): a method called through the object's own vtable, slot n, so that it reaches the override of
    # whatever class the object really is. Checked against every implementation that can be in that slot: the
    # slot in each vtable of the class and of the classes derived from it (tools/vtables_<side>.json), one ABI
    # check per distinct convention found. Pure-virtual stubs are skipped - they are never called.
    assert is_member, f"{where}: VIRTUAL declares a non-static method of an overlay class"
    assert path.endswith(('.h', '.hpp')), f"{where}: a class's VIRTUAL declarations belong in its header"
    assert slot is not None and slot.isdigit(), f"{where}: VIRTUAL wants its vtable slot, as VIRTUAL(1)"
    include = os.path.relpath(path, f"src/{side}").replace("\\", "/")
    if include not in includes:
        includes.append(include)
    slot = int(slot)
    overload = f"XbeOverload<{bare_return}({', '.join(types)})>::Of(&{qualified})"
    checks = []
    if vtables is None:
        print(f"  warning: {where}: tools/vtables_{side}.json not found - VIRTUAL({slot}) {qualified} unchecked "
              f"(run tools/vtables.py)")
    else:
        impls = {}
        for vt in sorted(class_vtables(vtables, cls)):
            entries = vtables["vtables"].get(vt, [])
            if slot >= len(entries):
                print(f"  warning: {where}: vtable {vt} of {cls} or a class derived from it has only "
                      f"{len(entries)} slots; VIRTUAL({slot}) cannot be right for it")
                continue
            name = function_names.get(entries[slot], "")
            if "pure_virtual" in name or "purecall" in name:
                continue
            impls[entries[slot]] = name
        if not impls:
            print(f"  warning: {where}: no implementations of {cls}'s slot {slot} found - VIRTUAL({slot}) "
                  f"{qualified} unchecked")
        by_facts = {}
        for address, name in sorted(impls.items()):
            facts = abi.get(address) if abi else None
            key = json.dumps(facts and {k: facts[k] for k in ("pops", "regs_in")}, sort_keys=True)
            by_facts.setdefault(key, []).append((address, name))
        for group in by_facts.values():
            address, name = group[0]
            what = (f"{qualified} (VIRTUAL({slot})) against {name or address}"
                    + (f" and {len(group) - 1} other overrides of the same convention" if len(group) > 1 else ""))
            check = abi_check(abi, address, what, overload)
            if check:
                checks.append(check)
    pointer_type = f"decltype({overload})"
    params = ", ".join(f"{t} a{i}" for i, t in enumerate(types))
    args = ", ".join(f"a{i}" for i in range(len(types)))
    return (f"// {where} - slot {slot} of the object's vtable\n"
            f"{bare_return} {convention + ' ' if convention else ''}{qualified}({params}) {{\n"
            + "".join(f"    {c}\n" for c in checks)
            + f"    return (this->*XbeVirtual<{pointer_type}>(this, {slot}))({args});\n}}\n")


def generate_declared_funcs(side, ghidra_funcs):
    # AUTOGEN from our declaration rather than Ghidra's types (docs/driving-injection-framework.md, section 5).
    # The author writes the declaration - in its class, for a method - and this generates only the body, which
    # calls the original at its address through a pointer of the declaration's own type: a member-function
    # pointer through `this` for a method, a plain one otherwise. The ABI check of step 2 goes in the body.
    abi = load_abi_facts(side)
    vtables_path = f"tools/vtables_{side}.json"
    vtables = json.load(open(vtables_path, 'r')) if os.path.exists(vtables_path) else None
    function_names = {x['address'].lower(): x['name'] for x in ghidra_funcs}
    includes, bodies, names = [], [], []
    tagged = [("AUTOGEN",) + t for t in gather_declaration_tags("AUTOGEN", side)]
    tagged += [("VIRTUAL",) + t for t in gather_declaration_tags("VIRTUAL", side)]
    for tag, path, line_number, declaration, enclosing, address in tagged:
        where = f"{path}:{line_number}".replace("\\", "/")
        assert declaration.endswith(";"), f"{where}: {tag} wants a one-line declaration ending in ';'"
        chunks = [x for x in declaration.split(" ") if "(" in x]
        assert chunks, f"{where}: could not find the function name in '{declaration}'"
        short_name = chunks[-1].split("(")[0].lstrip("*&")
        signature = parse_signature(declaration, short_name)
        assert signature is not None, f"{where}: could not read the declaration '{declaration}'"
        return_type, types = signature
        is_static = re.search(r'\bstatic\b', declaration) is not None
        is_member = enclosing is not None and not is_static
        qualified = f"{enclosing}::{short_name}" if enclosing else short_name

        convention = next((c for c in CONVENTIONS if re.search(rf'\b{c}\b', return_type)), "")
        bare_return = re.sub(r'\b(' + "|".join(CONVENTIONS) + r')\b', '', return_type).strip()

        if tag == "VIRTUAL":
            bodies.append(virtual_body(where, path, enclosing, is_member, short_name, qualified, bare_return,
                                       convention, types, address, vtables, abi, function_names, side, includes))
            names.append(qualified)
            continue

        if address is None:
            address = match_tag("AUTOGEN", qualified, signature, ghidra_funcs)['address']
        elif not address.startswith("0x"):
            address = f"0x{address}"
        address = "0x%08x" % int(address, 16)

        if path.endswith(('.h', '.hpp')):
            include = os.path.relpath(path, f"src/{side}").replace("\\", "/")
            if include not in includes:
                includes.append(include)
        else:
            assert enclosing is None, f"{where}: a class's AUTOGEN declarations belong in its header"

        pointer_type = f"decltype(XbeOverload<{bare_return}({', '.join(types)})>::Of(&{qualified}))"
        params = ", ".join(f"{t} a{i}" for i, t in enumerate(types))
        args = ", ".join(f"a{i}" for i in range(len(types)))
        check = abi_check(abi, address, qualified, f"XbeOverload<{bare_return}({', '.join(types)})>::Of(&{qualified})")
        call = (f"(this->*XbeOriginal<{pointer_type}>({address}))({args})" if is_member
                else f"XbeOriginal<{pointer_type}>({address})({args})")
        bodies.append(f"// {where}\n"
                      f"{bare_return} {convention + ' ' if convention else ''}{qualified}({params}) {{\n"
                      + (f"    {check}\n" if check else "")
                      + f"    return {call};\n}}\n")
        names.append(qualified)

    output = "// This file is autogenerated by tools/preprocess.py. Do not modify.\n"
    output += ("// Bodies for the functions declared with // AUTOGEN - each calls the original at its address - and\n"
               "// // VIRTUAL(n) - each calls slot n of the object's own vtable.\n\n")
    output += f"#include \"{side}helpers.h\"\n#include \"../common/xbeAbi.h\"\n#include \"../common/xbeOverload.h\"\n"
    output += "".join(f"#include \"{inc}\"\n" for inc in includes) + "\n"
    output += "\n".join(bodies)
    with open(f"src/{side}/autogenerated_functions.inc", 'w') as file:
        file.write(output)
    return names


# ---------------------------------------------------------------------------------------------------------------
# Overlay class layouts from Ghidra (docs/driving-injection-framework.md, step 4).
#
# A class writes XBE_FIELDS(GhidraStructName) in its body and gets that structure's fields, typed, at Ghidra's
# offsets: src/<side>/autogenerated_layouts.h defines the macro, and autogenerated_layout_checks.inc asserts
# the size and every field's offset. The structures come from tools/structs_<side>.json, which
# ghidra/NightfireSync.py exports. Gaps become padding; a field the compiler would not place at Ghidra's offset
# unaided (misaligned, a bit-field, an embedded structure) becomes a byte array of the right size, commented
# with Ghidra's type.
# ---------------------------------------------------------------------------------------------------------------

GHIDRA_BASIC = {
    "undefined": ("uint8_t", 1), "undefined1": ("uint8_t", 1), "undefined2": ("uint16_t", 2),
    "undefined4": ("uint32_t", 4), "undefined8": ("uint64_t", 8), "byte": ("uint8_t", 1), "sbyte": ("int8_t", 1),
    "uchar": ("uint8_t", 1), "char": ("char", 1), "bool": ("bool", 1), "short": ("int16_t", 2),
    "ushort": ("uint16_t", 2), "word": ("uint16_t", 2), "int": ("int32_t", 4), "uint": ("uint32_t", 4),
    "dword": ("uint32_t", 4), "long": ("int32_t", 4), "ulong": ("uint32_t", 4), "longlong": ("int64_t", 8),
    "ulonglong": ("uint64_t", 8), "qword": ("uint64_t", 8), "float": ("float", 4), "double": ("double", 8),
    "pointer": ("void *", 4), "void": ("void", 0), "code": ("void", 0),
}
CPP_KEYWORDS = {"class", "struct", "union", "new", "delete", "this", "default", "template", "typename", "operator",
                "private", "public", "protected", "virtual", "friend", "namespace", "register", "auto", "switch",
                "case", "return", "int", "char", "float", "double", "bool", "void", "long", "short", "signed",
                "unsigned", "const", "static", "enum", "goto", "if", "else", "for", "while", "do", "break",
                "continue", "sizeof", "true", "false", "inline", "volatile", "export", "explicit", "mutable"}


def identifier(name):
    name = re.sub(r'\W', '_', name)
    if not name or name[0].isdigit():
        name = "_" + name
    return name + "_" if name in CPP_KEYWORDS else name


def translate_type(ghidra_type, structs, forward, embedded):
    # (C++ element type, element size, array suffix, element alignment) for a Ghidra display type, or None when
    # it has no C++ spelling this can trust - the caller then uses bytes. A structure embedded by value that the
    # export has is generated as a type of its own (added to `embedded`), so glares[128] is a Glare array rather
    # than 8192 bytes.
    t = ghidra_type.strip()
    dims = ""
    m = re.match(r'^(.*?)((?:\[\d+\])+)$', t)
    if m:
        t, dims = m.group(1).strip(), m.group(2)
    if t.endswith("*"):
        base = t[:-1].strip()
        stars = "*"
        while base.endswith("*"):
            base, stars = base[:-1].strip(), stars + "*"
        if base in GHIDRA_BASIC:
            return f"{GHIDRA_BASIC[base][0]} {stars}", 4, dims, 4
        if re.match(r'^[A-Za-z_]\w*$', base):
            forward.add(base)
            return f"{base} {stars}", 4, dims, 4
        return f"void {stars}", 4, dims, 4   # a template or other unspellable name: the pointer is still 4 bytes
    if t in GHIDRA_BASIC and GHIDRA_BASIC[t][1] > 0:
        size = GHIDRA_BASIC[t][1]
        return GHIDRA_BASIC[t][0], size, dims, min(size, 8)
    if t in structs and re.match(r'^[A-Za-z_]\w*$', t) and structs[t]["size"] > 1:
        embedded.add(t)
        return t, structs[t]["size"], dims, None   # alignment: worked out once the structure's fields are
    return None


def generate_layouts(side):
    uses = []   # (Ghidra structure name, file, our class name, keyword)
    declared_base = {}   # our class name -> the base class it declares, if any
    for root, dirs, files in os.walk(f"src/{side}"):
        for name in files:
            if not name.endswith(('.h', '.hpp')):
                continue
            path = os.path.join(root, name)
            text = open(path, 'r', encoding='utf-8', errors='replace').read()
            for m in re.finditer(r'\b(class|struct)\s+(\w+)\s*(?::\s*(?:public\s+)?(\w+)\s*)?\{[^{}]*?\bXBE_FIELDS\((\w+)\)',
                                 text):
                uses.append((m.group(4), path, m.group(2), m.group(1)))
                declared_base[m.group(2)] = m.group(3)

    header = ["// This file is autogenerated by tools/preprocess.py from tools/structs_%s.json. Do not modify." % side,
              "// Each XBE_FIELDS_<name> is a Ghidra structure's fields, for an overlay class (src/common/xbeClass.h).",
              "// Structures embedded by value in those are defined here as plain structs of their own.",
              "#pragma once", "", "#include <stdint.h>", "", "#define XBE_FIELDS(name) XBE_FIELDS_##name", ""]
    checks = ["// This file is autogenerated by tools/preprocess.py. Do not modify.",
              "// Every overlay class's size and field offsets, against the Ghidra structure its fields came from.", "",
              '#include "../common/xbeClass.h"', '#include "autogenerated_layouts.h"', '#include <type_traits>']
    if not uses:
        write_if_changed(f"src/{side}/autogenerated_layouts.h", "\n".join(header) + "\n")
        write_if_changed(f"src/{side}/autogenerated_layout_checks.inc", "\n".join(checks) + "\n")
        return 0

    path = f"tools/structs_{side}.json"
    assert os.path.exists(path), f"XBE_FIELDS is used but {path} does not exist - run ghidra/NightfireSync.py"
    all_structs = json.load(open(path, 'r'))
    by_name = {}
    for s in all_structs:
        by_name.setdefault(s["name"], []).append(s)
    structs = {n: v[0] for n, v in by_name.items() if len(v) == 1}
    sizes_path = f"tools/alloc_sizes_{side}.json"
    alloc_sizes = json.load(open(sizes_path, 'r')) if os.path.exists(sizes_path) else {}
    overlays = {cls: keyword for _, _, cls, keyword in uses}
    overlay_sources = {ghidra_name for ghidra_name, _, _, _ in uses}
    forward = set()
    alignments = {}

    def base_of(ghidra_name):
        # Ghidra's convention for single inheritance - the one its class recovery uses - is a first field at offset
        # 0, named super_<Base>, of the base structure's type. That field is not a member in C++: it is the base
        # class, whose own fields fill those bytes.
        fields = structs[ghidra_name]["fields"]
        first = min(fields, key=lambda f: f["offset"]) if fields else None
        if first and first["offset"] == 0 and first["name"] == "super_" + first["type"] and first["type"] in structs:
            return first["type"], first["size"]
        return None, 0

    def fields_of(ghidra_name, cls, embedded):
        # The field declarations and offset checks for one structure, its alignment, and its base class (see
        # base_of). Fields that name an embedded structure add it to `embedded`, for the caller to generate too.
        s = structs[ghidra_name]
        base, base_size = base_of(ghidra_name)
        lines, cursor, used_names, align = [], base_size, set(), (alignment_of(base) if base else 1)
        checks_for = [f"XBE_CLASS_SIZE({cls}, 0x{s['size']:x});"]
        for f in sorted(s["fields"], key=lambda f: f["offset"]):
            if f["offset"] < cursor:
                continue   # overlapping components (a union-like view): the first one wins
            if f["offset"] > cursor:
                lines.append(f"uint8_t _pad_0x{cursor:x}[{f['offset'] - cursor}];")
            name = identifier(f["name"])
            while name in used_names:
                name += "_"
            used_names.add(name)
            found = set()
            translated = None if f["bitfield"] else translate_type(f["type"], structs, forward, found)
            if translated is not None:
                ctype, element, dims, element_align = translated
                if element_align is None:   # an embedded structure: its own fields decide
                    element_align = alignment_of(ctype)
                count = 1
                for d in re.findall(r'\d+', dims):
                    count *= int(d)
                if element == 0 or element * count != f["size"] or f["offset"] % element_align != 0:
                    translated, found = None, set()
            if translated is None:
                lines.append(f"uint8_t {name}[{f['size']}]; /* {f['type']} */")
            else:
                embedded |= found
                align = max(align, element_align)
                sep = "" if ctype.endswith("*") else " "
                lines.append(f"{ctype}{sep}{name}{dims};")
            checks_for.append(f"XBE_FIELD({cls}, {name}, 0x{f['offset']:x});")
            cursor = f["offset"] + f["size"]
        if cursor < s["size"]:
            lines.append(f"uint8_t _pad_0x{cursor:x}[{s['size'] - cursor}];")
        return lines, checks_for, align, base

    def alignment_of(ghidra_name):
        # A structure aligns like its strictest field; a structure whose size is not a multiple of that gets
        # padded by the compiler, which the size check then reports.
        if ghidra_name not in alignments:
            alignments[ghidra_name] = 1          # a guard against a structure that contains itself
            alignments[ghidra_name] = fields_of(ghidra_name, ghidra_name, set())[2]
        return alignments[ghidra_name]

    macros, overlay_checks, generated_checks, generated_defs = [], [], [], []
    pending = []
    for ghidra_name, file, cls, keyword in uses:
        assert ghidra_name in structs, (f"{file}: XBE_FIELDS({ghidra_name}) - "
                                        f"{len(by_name.get(ghidra_name, []))} Ghidra structures of that name")
        s = structs[ghidra_name]
        assert s["size"] > 1 or s["fields"], (f"{file}: XBE_FIELDS({ghidra_name}) - Ghidra's {ghidra_name} is a "
                                             f"{s['size']}-byte placeholder with no fields; define it in Ghidra and re-sync")
        embedded = set()
        lines, checks_for, _, base = fields_of(ghidra_name, cls, embedded)
        pending += sorted(embedded)

        # Inheritance: Ghidra's structure and our declaration must agree on the base. The base is either another
        # overlay class (named as its own class, which may differ from the Ghidra structure's name) or, if we have
        # none for it, generated like an embedded structure.
        wanted = None
        if base is not None:
            base_overlays = [c for g, _, c, _ in uses if g == base]
            if base_overlays:
                wanted = base_overlays[0]
            else:
                wanted = base
                pending.append(base)
        declared = declared_base.get(cls)
        assert declared == wanted, (
            f"{file.replace(chr(92), '/')}: {cls} " + (f"declares base class {declared}" if declared else "declares no base class")
            + f", but Ghidra's {ghidra_name} " + (f"begins with super_{base}, so it should derive from {wanted}"
                                                  if base else "has no super_ field, so it derives from nothing"))
        if base is not None:
            checks_for.append(f"static_assert(std::is_base_of_v<{wanted}, {cls}>, \"{cls} must derive from {wanted}\");")

        # The binary's own size for the class, from the allocations made under its name (tools/alloc_sizes.py).
        # Ghidra's structure often stops at its last known field; the game's allocation does not. Where the
        # game allocates more, the class is padded out to the real size - so sizeof is right, and the unknown
        # tail is visible - and checked against that instead.
        size = s["size"]
        measured = alloc_sizes.get(ghidra_name, {})
        exact = measured.get("allocated", [])
        if len(exact) == 1 and exact[0] > size:
            lines.append(f"uint8_t _beyond_ghidra_0x{size:x}[{exact[0] - size}]; "
                         f"/* the game allocates 0x{exact[0]:x}; Ghidra's structure ends at 0x{size:x} */")
            print(f"  {ghidra_name}: Ghidra's structure is 0x{size:x} bytes, the game allocates 0x{exact[0]:x} - "
                  f"padded to the game's size")
            size = exact[0]
            checks_for[0] = f"XBE_CLASS_SIZE({cls}, 0x{size:x});   /* the game's allocation size */"
        elif len(exact) == 1 and exact[0] < size:
            print(f"  warning: {ghidra_name}: Ghidra's structure is 0x{size:x} bytes but the game allocates only "
                  f"0x{exact[0]:x} - fields past 0x{exact[0]:x} cannot be right")
        elif not exact and measured.get("constructed") and max(measured["constructed"]) > size \
                and size not in measured["constructed"]:
            print(f"  warning: {ghidra_name}: Ghidra's structure is 0x{size:x} bytes, and allocations its constructor "
                  f"runs on are 0x{min(measured['constructed']):x} or more - it may be short")
        macros.append(f"// {ghidra_name}: {size} bytes, for {cls} ({file.replace(chr(92), '/')})\n"
                      f"#define XBE_FIELDS_{ghidra_name} \\\n    "
                      + " \\\n    ".join(lines) + "\n")
        include = f'#include "{os.path.relpath(file, f"src/{side}").replace(chr(92), "/")}"'
        if include not in checks:
            checks.append(include)
        overlay_checks.append("\n".join(checks_for))

    # Structures embedded by value, generated as plain structs, each after the ones it contains. One that is also
    # an overlay class's source cannot be both - the overlay is ours, the embedded one generated - so it is refused.
    done, order = set(), []

    def visit(name, chain):
        if name in done:
            return
        assert name not in chain, f"structure {name} contains itself: {' -> '.join(chain + [name])}"
        assert name not in overlay_sources and name not in overlays, (
            f"{name} is used both by XBE_FIELDS in an overlay class and embedded by value in another Ghidra "
            f"structure; the embedded use needs the generated struct. Drop the overlay, or make the containing field "
            f"a pointer in Ghidra")
        embedded = set()
        lines, checks_for, _, base = fields_of(name, name, embedded)
        for inner in sorted(embedded | ({base} if base else set())):
            visit(inner, chain + [name])
        done.add(name)
        order.append((name, lines, checks_for, base))

    for name in pending:
        visit(name, [])
    for name, lines, checks_for, base in order:
        macros.append(f"// {name}: {structs[name]['size']} bytes, embedded by value or a base\n"
                      f"#define XBE_FIELDS_{name} \\\n    " + " \\\n    ".join(lines) + "\n")
        generated_defs.append(f"struct {name}{' : ' + base if base else ''} {{ XBE_FIELDS_{name} }};")
        generated_checks.append("\n".join(checks_for))

    header += [f"{overlays.get(n, 'struct')} {n};" for n in sorted(forward) if n not in done] + [""] + macros
    header += ["// Embedded structures, in dependency order."] + generated_defs + [""]
    write_if_changed(f"src/{side}/autogenerated_layouts.h", "\n".join(header))
    write_if_changed(f"src/{side}/autogenerated_layout_checks.inc",
                     "\n".join(checks) + "\n\n" + "\n\n".join(generated_checks + overlay_checks) + "\n")
    if order:
        print(f"Generated {len(order)} embedded or base structures: {', '.join(n for n, _, _, _ in order)}")
    return len(uses)


def write_if_changed(path, text):
    # A header included everywhere: rewriting it unchanged would rebuild everything that includes it.
    if os.path.exists(path) and open(path, 'r').read() == text:
        return
    with open(path, 'w') as f:
        f.write(text)


def generate_auto_funcs(side, ghidra_funcs):
    if side == "driving":
        return generate_declared_funcs(side, ghidra_funcs)
    # For all functions in the source code which are:
    # - Tagged with an "AUTOGEN" comment
    # Create a wrapper function that calls the original function via function pointer
    # Note - we could do this for all functions that haven't been injected, but it's more explicit to only do it for functions that are tagged
    auto_funcs = gather_functions_with_tag("AUTOGEN", side=side)
    helpers = f"#include \"{side}helpers.h\""
    output_file = f"// This file is autogenerated. Do not modify.\n{helpers}\n\n"
    for f in auto_funcs:

        matching_func = [x for x in ghidra_funcs if x['name'] == f[1] and x['is_thunk'] == False]
        assert len(matching_func) == 1, f"Function {f[1]} duplicated or not found (qty is {len(matching_func)})"
        gf = matching_func[0]
        assert gf['has_custom_variable_storage'] == False, f"Function {f[1]} has custom variable storage, cannot be autogenerated yet"


        param_types = ", ".join(gf['param_types']) if gf['param_types'] else "void"
        addr = gf['address']
        arg_list = []
        arg_names = []
        for i, type in enumerate(gf['param_types']):
            arg_list.append(f"{type} arg{i}")
            arg_names.append(f"arg{i}")

        al = ", ".join(arg_list)
        an = ", ".join(arg_names)
        # Ghidra sometimes reports "unknown" for functions it hasn't confidently analyzed a calling convention
        # for (even when every parameter is plain stack storage) - treat that the same as "default" rather than
        # emitting "unknown" as if it were a real keyword, which produces invalid C++.
        cc = "" if gf['calling_convention'] in ("default", "unknown") else gf['calling_convention']

        if "_Handler" in gf['name']:
            cc = "__cdecl" # HACKHACKHACK not sure if these are all cdecl or stdcall

        output_file += f"""
    {gf['return_type']} {cc} {gf['name']}({al}) {{
        return reinterpret_cast<{gf['return_type']} (*)({param_types})>({addr})({an});
    }}
    """
    with open(f"src/{side}/autogenerated_functions.inc", 'w') as file:
        file.write(output_file)

    return auto_funcs

def generate(side):
    ghidra_funcs = json.load(open(f"tools/functions_{side}.json", 'r'))

    # For all functions in the C/C++ codebase which are either:
    # - Tagged with a "FUNC_AT" comment and an address
    # - Tagged with an "AUTOINJECT" comment
    # Generate an injection entry

    injections = generate_auto_inject(side, ghidra_funcs)
    uninjectable = gather_functions_with_tag("UNINJECTABLE", side=side)

    # UNINJECTABLE marks a function reimplemented but not patched in - only counted, never looked up, so a
    # misspelled or since-renamed name would be counted all the same. Warn about any Ghidra does not know, with
    # the same "that name elsewhere" hint as a stale AUTOINJECT tag.
    known_names = {x['name'] for x in ghidra_funcs}
    for f in uninjectable:
        if f[1] in known_names:
            continue
        short = f[1].split("::")[-1]
        elsewhere = [f"{x['name']} ({x['address']})" for x in ghidra_funcs if x['name'].split("::")[-1] == short]
        hint = (" - functions with that name elsewhere: " + ", ".join(elsewhere[:6])) if elsewhere else ""
        print(f"  warning: UNINJECTABLE {f[1]} ({f[3].replace(chr(92), '/')}): no function of that name in Ghidra's export{hint}")

    # Print statistics
    num_injecions = len(injections)
    num_uninjectable = len(uninjectable)
    total_funcs = len(ghidra_funcs)
    ratio_complete = (num_injecions + num_uninjectable) / total_funcs

    print(f"Found {num_injecions} injections, {num_uninjectable} uninjectable but implemented, of {total_funcs} known functions ({ratio_complete*100:.2f}%)")

    layouts = generate_layouts(side)
    if layouts:
        print(f"Generated the fields of {layouts} overlay classes from tools/structs_{side}.json")

    auto_funcs = generate_auto_funcs(side, ghidra_funcs)
    auto_func_count = len(auto_funcs)
    ratio_auto = auto_func_count / total_funcs

    print(f"Found {auto_func_count} auto gen'd for {total_funcs} known functions ({ratio_auto*100:.2f}%)")

def gather_functions_with_tag(tag_name=None, has_params=False, side="action"):
    functions = []
    exclude = { "driving" if side == "action" else "action" }
    for root, dirs, files in os.walk("src"):
        # We only ever want to get the data for one of the two sides
        [dirs.remove(d) for d in list(dirs) if d in exclude]
        for file in files:
            if not file.endswith(('.c', '.cpp')):
                continue

            file_path = os.path.join(root, file)
            with open(file_path, 'r') as file:
                lines = file.readlines()
                for i in range(len(lines)):
                    line = lines[i]
                    if has_params:
                        match = re.search(rf'// {tag_name}\((\w+)\)', line)
                    else:
                        match = re.search(rf'// {tag_name}', line)
                    if match:
                        address = match.group(1) if has_params else None
                        next_line = lines[i+1].strip()

                        # We expect a function declaration, eg "void func_name(int a, int b) {"
                        # We want to extract the function name, eg "func_name" - ie the token preceding the last "(" character
                        # It must specifically be the last "(" character, as we may have other "(" characters in the function signature (eg "declspec(naked)")

                        chunks = next_line.split(" ")
                        chunks_with_brackets = [x for x in chunks if "(" in x]
                        function_name = chunks_with_brackets[-1].split("(")[0]
                        functions.append((address, function_name, parse_signature(next_line, function_name), file_path))
    return functions

def generate_injections(injections):
    header = "// This file is autogenerated. Do not modify.\n"
    header += f"// Generated on {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n"
    lines = []
    for address, function_name, target, check in injections:
        if not address.startswith("0x"):
            address = f"0x{address}"
        if check is not None:
            lines.append(check)
        lines.append(f"WriteJmpTo({address}, {target});  // {function_name}")
    return header + "\n".join(lines)

generate("action")
generate("driving")

# For all types in the Ghidra database where:
# - There are fields
# Create a packed struct definition

# TODO: This


# For all enums in the Ghidra database
# Create an enum definition

# TODO: This
