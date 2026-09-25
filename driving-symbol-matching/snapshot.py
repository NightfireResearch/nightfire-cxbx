"""Record every function's name, signature, body and plate comment in a program, before any write.

    python snapshot.py                  # Driving.xbe -> data/snapshots/Driving.xbe-<time>.json
    python snapshot.py DRIVING.ELF

apply.py refuses to run without a snapshot newer than its last write, and can restore names and
signatures from one. It is a record of what Ghidra held, not a substitute for a copy of the Ghidra project.
"""

import json
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

from lib import ghidra_ro as g

HERE = os.path.dirname(os.path.abspath(__file__))


def one(program, address):
    info = g.function_info(program, address)
    plate = g.get_json("get_plate_comment", program=program, address=f"0x{address:x}")
    return {
        "address": f"0x{address:08x}",
        "function": info.get("function"),
        "signature": info.get("signature"),
        "body": info.get("body"),
        "plate": plate.get("comment"),
    }


def main():
    program = sys.argv[1] if len(sys.argv) > 1 else g.XBOX
    funcs = g.functions(program)
    t0 = time.time()
    with ThreadPoolExecutor(4) as pool:
        out = list(pool.map(lambda f: one(program, f[0]), funcs))
    qualified = g.qualified_names(program)
    thunks = g.thunks(program)
    for (addr, name), rec in zip(funcs, out):
        rec["name"] = name
        rec["thunk"] = addr in thunks
        rec["qualified"] = qualified.get(addr, name)
    folder = os.path.join(HERE, "data", "snapshots")
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, f"{program}-{time.strftime('%Y%m%d-%H%M%S')}.json")
    with open(path, "w") as f:
        json.dump({"program": program, "taken": time.ctime(), "functions": out}, f, indent=0)
    named = sum(1 for r in out if not r["name"].startswith("FUN_"))
    print(f"{path}: {len(out)} functions, {named} named, {len(qualified)} in namespaces,"
          f" {sum(1 for r in out if r['plate'])} plate comments,"
          f" {time.time() - t0:.0f}s")


if __name__ == "__main__":
    main()
