"""Read-only client for the Ghidra MCP server's HTTP endpoint.

Only GET requests to the endpoints in READ_ONLY are allowed; anything else raises before a request is made.
Writes to Ghidra go through apply.py alone, which snapshots first and runs dry by default.
"""

import json
import os
import urllib.parse
import urllib.request

BASE = os.environ.get("GHIDRA_HTTP", "http://127.0.0.1:8089")

XBOX = "Driving.xbe"
PS2 = "DRIVING.ELF"

READ_ONLY = {
    "list_open_programs",
    "list_functions_enhanced",
    "search_functions_enhanced",
    "get_function_by_address",
    "get_function_signature",
    "get_function_variables",
    "get_function_callees",
    "get_function_callers",
    "get_xrefs_to",
    "get_xrefs_from",
    "get_plate_comment",
    "decompile_function",
    "disassemble_function",
    "list_strings",
    "list_namespaces",
    "list_class_members",
    "read_memory",
}


def get(endpoint, timeout=120, **params):
    if endpoint not in READ_ONLY:
        raise PermissionError(f"{endpoint} is not in the read-only list")
    if "program" not in params:
        raise ValueError("pass program explicitly: omitting it targets whichever program is current")
    url = f"{BASE}/{endpoint}?{urllib.parse.urlencode(params)}"
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read().decode("utf-8")


def get_json(endpoint, **params):
    return json.loads(get(endpoint, **params))


def functions(program):
    """[(address, name)] for every function, address-sorted."""
    out = get_json("list_functions_enhanced", program=program, limit=50000)["functions"]
    return sorted((int(f["address"], 16), f["name"]) for f in out)


def function_info(program, address):
    """Name, signature and body range, parsed from get_function_by_address."""
    text = get("get_function_by_address", program=program, address=f"0x{address:x}")
    info = {}
    for line in text.splitlines():
        key, _, value = line.partition(": ")
        info[key.strip().lower()] = value.strip()
    return info


def callees(program, address):
    return _at_list(get("get_function_callees", program=program, address=f"0x{address:x}"))


def callers(program, address):
    return _at_list(get("get_function_callers", program=program, address=f"0x{address:x}"))


def _at_list(text):
    out = []
    for line in text.splitlines():
        name, sep, addr = line.rpartition(" @ ")
        if sep:
            out.append((int(addr, 16), name))
    return out


def features(program, address):
    """Instruction and block counts, callee names, string constants and immediates for one function."""
    return get_json("get_function_signature", program=program, address=f"0x{address:x}")


def decompile(program, address):
    return get_json("decompile_function", program=program, address=f"0x{address:x}")["result"]


def qualified_names(program):
    """{address: 'Namespace::Name'} for every function that sits in a namespace (list_functions omits them)."""
    out = {}
    for ns in get("list_namespaces", program=program, limit=100000).splitlines():
        ns = ns.strip()
        if not ns:
            continue
        members = get_json("list_class_members", program=program, class_name=ns, limit=100000)
        for m in members.get("members", []):
            if m.get("matched_by") in ("namespace", "both"):
                out[int(m["address"], 16)] = m["name"]
    return out
