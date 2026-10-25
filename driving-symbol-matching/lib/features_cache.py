"""Per-function features (callees, strings, instruction count) for a whole program, cached in data/.

The cache is keyed by address and refreshed on request; names in it go stale after renames, strings don't.
"""

import json
import os
from concurrent.futures import ThreadPoolExecutor

from lib import ghidra_ro as g

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(program, refresh=False):
    path = os.path.join(HERE, "data", f"features-{program}.json")
    if os.path.exists(path) and not refresh:
        with open(path) as f:
            return {int(k, 16): v for k, v in json.load(f).items()}
    addrs = [a for a, _ in g.functions(program)]

    def one(a):
        try:
            f = g.features(program, a)
            return a, {"strings": f.get("string_constants", []), "callees": f.get("callee_names", []),
                       "insns": f.get("instruction_count")}
        except Exception:
            return a, None

    with ThreadPoolExecutor(6) as pool:
        out = {a: v for a, v in pool.map(one, addrs) if v is not None}
    with open(path, "w") as f:
        json.dump({f"0x{a:08x}": v for a, v in out.items()}, f)
    return out
