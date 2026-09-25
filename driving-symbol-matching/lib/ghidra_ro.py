"""Read-only client for the Ghidra MCP server's HTTP endpoint.

Only GET requests to the endpoints in READ_ONLY are allowed; anything else raises before a request is made.
Writes to Ghidra go through apply.py alone, which snapshots first and runs dry by default.
"""

import http.client
import json
import os
import threading
import time
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
    return request("GET", f"/{endpoint}?{urllib.parse.urlencode(params)}", timeout=timeout)


_local = threading.local()


def request(method, path, body=None, headers=None, timeout=120):
    """One kept-alive connection per thread: a new socket per request ran Windows out of them (WinError
    10055) after tens of thousands of calls. Reconnects and retries a few times on a socket error."""
    host = urllib.parse.urlparse(BASE)
    for attempt in range(4):
        conn = getattr(_local, "conn", None)
        if conn is None:
            conn = _local.conn = http.client.HTTPConnection(host.hostname, host.port, timeout=timeout)
        try:
            conn.request(method, path, body=body, headers=headers or {})
            r = conn.getresponse()
            data = r.read().decode("utf-8")
            if r.getheader("Connection", "").lower() == "close":
                conn.close()
                _local.conn = None
            return data
        except (OSError, http.client.HTTPException):
            conn.close()
            _local.conn = None
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)


def get_json(endpoint, **params):
    return json.loads(get(endpoint, **params))


def functions(program):
    """[(address, name)] for every function, address-sorted."""
    out = get_json("list_functions_enhanced", program=program, limit=50000)["functions"]
    return sorted((int(f["address"], 16), f["name"]) for f in out)


def thunks(program):
    """Addresses of thunk functions. A thunk shows its target's name unless given its own."""
    out = get_json("list_functions_enhanced", program=program, limit=50000)["functions"]
    return {int(f["address"], 16) for f in out if f.get("isThunk")}


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
    return get("decompile_function", program=program, address=f"0x{address:x}")


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
