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
    # Selects one function out of our own overloads by its type, and gives its address.
    return_type, types = signature
    return f"XbeAddress(XbeOverload<{return_type}({', '.join(types)})>::Of(&{name}))"


def generate_auto_inject(side, ghidra_funcs):
    injections = []

    autoinjects = gather_functions_with_tag("AUTOINJECT", side=side)
    autoltcg = gather_functions_with_tag("AUTOLTCG", side=side)
    func_ats = gather_functions_with_tag("FUNC_AT", True, side)

    # A name our own code defines more than once has to be selected by type in the generated table, or
    # "&Name" does not compile - or, worse, picks whichever the compiler likes.
    defined = [f[1] for f in autoinjects + autoltcg + func_ats]
    overloaded = {n for n in defined if defined.count(n) > 1}

    def target(f):
        if f[1] not in overloaded:
            return f"(size_t)&{f[1]}"
        assert f[2] is not None, f"{f[1]} is defined more than once, and this definition's line could not be read"
        return overload_expression(f[1], f[2])

    for f in autoinjects:
        mf = match_tag("AUTOINJECT", f[1], f[2], ghidra_funcs)
        assert mf['has_custom_variable_storage'] == False, f"Function {f[1]} has custom variable storage, to inject this you need to write an assembly wrapper. See View_CaptureScene for an example."
        injections.append((mf['address'], f[1], target(f)))

    for f in autoltcg:
        # "AUTOLTCG" is an acknowledgement that the function is aware of and correctly handles the custom variable storage, so no need to check for it
        # Eventually, we could fix this preprocess script to generate a wrapper automatically, but for now that's up to the code
        mf = match_tag("AUTOLTCG", f[1], f[2], ghidra_funcs)
        injections.append((mf['address'], f[1], target(f)))

    # FUNC_AT(x) with a given address
    for f in func_ats:
        injections.append((f[0], f[1], target(f)))

    # Save the injections to a file
    output_file = f"src/{side}/autogenerated_injections.inc"
    with open(output_file, 'w') as file:
        file.write(generate_injections(injections))
    print(f"Autogenerated injections saved to {output_file}")

    return injections

def generate_auto_funcs(side, ghidra_funcs):
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
    for address, function_name, target in injections:
        if not address.startswith("0x"):
            address = f"0x{address}"
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
