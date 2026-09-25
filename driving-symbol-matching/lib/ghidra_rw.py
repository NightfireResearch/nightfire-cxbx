"""Writes to Ghidra. Only apply.py imports this.

A short whitelist of POST endpoints; every call names its program (Driving.xbe or DRIVING.ELF) explicitly.
"""

import json
import urllib.parse
import urllib.request

from lib.ghidra_ro import BASE, PS2, XBOX

WRITE = {
    "create_function",
    "rename_function_by_address",
    "set_plate_comment",
}


def post(endpoint, body, program=XBOX, timeout=120):
    if endpoint not in WRITE:
        raise PermissionError(f"{endpoint} is not in the write list")
    if program not in (XBOX, PS2):
        raise PermissionError("only Driving.xbe and DRIVING.ELF are written to")
    url = f"{BASE}/{endpoint}?{urllib.parse.urlencode({'program': program})}"
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        text = r.read().decode("utf-8")
    try:
        return json.loads(text)
    except ValueError:
        return {"text": text}
