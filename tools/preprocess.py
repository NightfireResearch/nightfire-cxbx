# Credits: Nightfire Research Team - (2024 - 2025)

import datetime
import json
import os
import re
import struct
from collections import defaultdict
from pathlib import Path

# Sub-tasks defined in other files for readability
from uihandler import generate_handler_switch

generate_handler_switch()

def f32(value):
    """Convert float to 32-bit representation for JSON."""
    return struct.unpack("<f", struct.pack("<f", value))[0]

def get_function_match_ratio(func_name, injections, uninjectable):
    """
    Determine match percentage for a function:
    - 100% if injected (AUTOINJECT/AUTOLTCG/FUNC_AT) or uninjectable
    - 0% otherwise (AUTOGEN or not implemented)
    """
    injection_names = [inj[1] for inj in injections]
    uninjectable_names = [uninj[1] for uninj in uninjectable]

    if func_name in injection_names or func_name in uninjectable_names:
        return 100.0
    return 0.0

def map_functions_to_units(ghidra_funcs, injections, uninjectable, side):
    """
    Group functions by their source file (compilation unit).
    Returns: dict mapping unit name -> list of function dicts
    """
    units = defaultdict(list)

    for func in ghidra_funcs:
        if func['is_thunk']:
            continue

        # Find which source file defines this function
        source_file = find_source_file_for_function(func['name'], side)
        if not source_file:
            source_file = "Unknown"

        # Calculate match ratio
        ratio = get_function_match_ratio(func['name'], injections, uninjectable)

        # Estimate size (we don't have exact size, so use 0x10 as placeholder)
        # In a real scenario, you'd extract this from the binary or symbols
        size = 0x10  # Placeholder

        units[source_file].append({
            "name": func['name'],
            "address": int(func['address'], 16),
            "size": size,
            "ratio": ratio,
        })

    return units

def find_source_file_for_function(func_name, side):
    """
    Search source files to find where a function is defined.
    Returns relative path like "action/game/obj/Light.cpp"
    """
    exclude = {"driving" if side == "action" else "action"}

    for root, dirs, files in os.walk("src"):
        [dirs.remove(d) for d in list(dirs) if d in exclude]
        for file in files:
            if not file.endswith(('.c', '.cpp')):
                continue

            file_path = os.path.join(root, file)
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    # Look for function definition patterns
                    patterns = [
                        rf'\b{re.escape(func_name)}\s*\(',  # Basic pattern
                        rf'// AUTOINJECT\s*\n.*\b{re.escape(func_name)}\s*\(',  # After AUTOINJECT
                        rf'// AUTOGEN\s*\n.*\b{re.escape(func_name)}\s*\(',  # After AUTOGEN
                    ]
                    for pattern in patterns:
                        if re.search(pattern, content):
                            # Return path relative to src/
                            rel_path = os.path.relpath(file_path, "src")
                            return rel_path.replace("\\", "/")
            except:
                pass

    return None

def calculate_measures(functions, total_units=1):
    """Calculate statistics for a group of functions."""
    if not functions:
        return {"total_units": total_units}

    total_code = sum(f["size"] for f in functions)
    matched = [f for f in functions if f["ratio"] == 100.0]
    matched_code = sum(f["size"] for f in matched)
    fuzzy = sum(f["ratio"] * f["size"] for f in functions)

    measures = {"total_units": total_units}

    if total_code:
        measures.update(
            total_code=str(total_code),
            matched_code=str(matched_code),
            fuzzy_match_percent=f32(fuzzy / total_code),
            matched_code_percent=f32(matched_code / total_code * 100),
        )

    if functions:
        measures.update(
            total_functions=len(functions),
            matched_functions=len(matched),
            matched_functions_percent=f32(len(matched) / len(functions) * 100),
        )

    return measures

def generate_decomp_report(side, ghidra_funcs, injections, uninjectable):
    """Generate decomp.dev report.json in objdiff v2 format."""
    units_data = map_functions_to_units(ghidra_funcs, injections, uninjectable, side)

    units = []
    for unit_name, functions in sorted(units_data.items()):
        functions.sort(key=lambda f: f["address"])

        units.append({
            "name": unit_name,
            "measures": calculate_measures(functions),
            "sections": [],
            "functions": [
                {
                    "name": f["name"],
                    "size": str(f["size"]),
                    "metadata": {"virtual_address": hex(f["address"])},
                    "fuzzy_match_percent": f32(f["ratio"]),
                }
                for f in functions
            ],
            "metadata": {"module_name": unit_name},
        })

    # Calculate overall measures
    all_functions = [f for funcs in units_data.values() for f in funcs]

    report = {
        "measures": calculate_measures(all_functions, len(units)),
        "units": units,
        "version": 2,
    }

    # Write report
    output_path = Path(f"report_{side}.json")
    output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    measures = report["measures"]
    if "matched_functions" in measures:
        print(f"[{side}] {measures['matched_functions']}/{measures['total_functions']} functions matched ({measures['matched_functions_percent']:.1f}%)")
    print(f"[{side}] Wrote {output_path}")

    return report

def generate_auto_inject(side, ghidra_funcs):
    injections = []

    # Look up addresses and append to injections list

    autoinjects = gather_functions_with_tag("AUTOINJECT", side=side)
    for f in autoinjects:
        matching_func = [x for x in ghidra_funcs if x['name'] == f[1] and x['is_thunk'] == False]
        assert len(matching_func) >= 1,f"Function {f[1]} not found, qty is {len(matching_func)}"
        
        # Thunked functions can appear multiple times, so we need to inject them in all places
        for mf in matching_func:
            assert mf['has_custom_variable_storage'] == False, f"Function {f[1]} has custom variable storage, to inject this you need to write an assembly wrapper. See View_CaptureScene for an example."
            addr = mf['address']
            injections.append((addr, f[1],))

    autoltcg = gather_functions_with_tag("AUTOLTCG", side=side)
    for f in autoltcg:
        matching_func = [x for x in ghidra_funcs if x['name'] == f[1] and x['is_thunk'] == False]
        assert len(matching_func) >= 1,f"Function {f[1]} not found, qty is {len(matching_func)}"
        
        # Thunked functions can appear multiple times, so we need to inject them in all places
        for mf in matching_func:
            # "AUTOLTCG" is an acknowledgement that the function is aware of and correctly handles the custom variable storage, so no need to check for it
            # Eventually, we could fix this preprocess script to generate a wrapper automatically, but for now that's up to the code
            addr = mf['address']
            injections.append((addr, f[1],))
    

    # FUNC_AT(x) with a given address
    injections.extend(gather_functions_with_tag("FUNC_AT", True, side))

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
        cc = "" if gf['calling_convention'] == "default" else gf['calling_convention']

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

    # Generate decomp.dev report
    generate_decomp_report(side, ghidra_funcs, injections, uninjectable)

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
                        functions.append((address, function_name))
    return functions

def generate_injections(function_names):
    header = "// This file is autogenerated. Do not modify.\n"
    header += f"// Generated on {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n"
    injections = []
    for address, function_name in function_names:
        if not address.startswith("0x"):
            address = f"0x{address}"
        injection = f"WriteJmpTo({address}, (size_t)&{function_name});" if address else f"CreateWrapperFunction(&{function_name});"
        injections.append(injection)
    return header + "\n".join(injections)

generate("action")
generate("driving")

# For all types in the Ghidra database where:
# - There are fields
# Create a packed struct definition

# TODO: This


# For all enums in the Ghidra database
# Create an enum definition

# TODO: This
