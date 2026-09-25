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
    assert len(matching) >= 1, f"{tag} {name}: no function of that name in Ghidra's export"
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


def generate_declared_funcs(side, ghidra_funcs):
    # AUTOGEN from our declaration rather than Ghidra's types (docs/driving-injection-framework.md, section 5).
    # The author writes the declaration - in its class, for a method - and this generates only the body, which
    # calls the original at its address through a pointer of the declaration's own type: a member-function
    # pointer through `this` for a method, a plain one otherwise. The ABI check of step 2 goes in the body.
    abi = load_abi_facts(side)
    includes, bodies, names = [], [], []
    for path, line_number, declaration, enclosing, address in gather_declaration_tags("AUTOGEN", side):
        where = f"{path}:{line_number}".replace("\\", "/")
        assert declaration.endswith(";"), f"{where}: AUTOGEN wants a one-line declaration ending in ';'"
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
    output += "// Bodies for the functions declared with // AUTOGEN: each calls the original at its address.\n\n"
    output += f"#include \"{side}helpers.h\"\n#include \"../common/xbeAbi.h\"\n#include \"../common/xbeOverload.h\"\n"
    output += "".join(f"#include \"{inc}\"\n" for inc in includes) + "\n"
    output += "\n".join(bodies)
    with open(f"src/{side}/autogenerated_functions.inc", 'w') as file:
        file.write(output)
    return names


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

    # Print statistics
    num_injecions = len(injections)
    num_uninjectable = len(uninjectable)
    total_funcs = len(ghidra_funcs)
    ratio_complete = (num_injecions + num_uninjectable) / total_funcs

    print(f"Found {num_injecions} injections, {num_uninjectable} uninjectable but implemented, of {total_funcs} known functions ({ratio_complete*100:.2f}%)")

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
                        functions.append((address, function_name, parse_signature(next_line, function_name)))
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
