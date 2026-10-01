"""Shorthand names for SGI _Rb_tree instantiations whose names the sheet cut (the user's convention, 25 Sept 2026).

    "_Rb_tree<MapKey, pair<MapKey, MapElement<ActWeaponDatabase::Wea"  -> "_Rb_tree<MapKey, ActWeaponDatabase::WeaponInfo>"
    "_Rb_tree<LString, pair<LString, DebugItem *>, _Select1st<pair<L"  -> "_Rb_tree<LString, DebugItem *>"
    "_Rb_tree<LString, LString, _Identity<LString>, less<LString>, a"  -> "_Rb_tree<LString, LString>"   (a set)

<Key, Mapped>: the key, and the map's value type (pair<Key, V> -> V, MapElement<X> -> X; a set's value is its key).
A mapped type the cut left unfinished is completed from a unique longer name among nearby sheet rows; otherwise
None. The kept text goes in the plate comment.
"""

import re


def _args(text):
    """Top-level template arguments of 'Name<a, b<c, d>, e' (the last may be unfinished)."""
    inside = text[text.index("<") + 1:]
    out, depth, cur = [], 0, ""
    for ch in inside:
        if ch == "<":
            depth += 1
        elif ch == ">":
            if depth == 0:
                out.append(cur.strip())
                return out, True
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        cur += ch
    out.append(cur.strip())
    return out, False


def _inner(arg):
    m = re.match(r"^(pair|MapElement)<(.*)$", arg)
    if not m:
        return arg, True
    args, closed = _args(arg)
    return (args[1] if m.group(1) == "pair" and len(args) > 1 else args[0]), closed


def rb_tree(cut_name, nearby_names):
    if not cut_name.startswith("_Rb_tree<"):
        return None
    args, _ = _args(cut_name)
    if len(args) < 2:
        return None
    key, value = args[0], args[1]
    if not value.startswith("pair<"):
        mapped, complete = value, True            # a set: the value is the key
    else:
        mapped, complete = _inner(value)
        if mapped.startswith("MapElement<"):
            mapped, complete = _inner(mapped)
    if not complete or mapped.endswith(",") or not mapped:
        prefix = mapped.rstrip(", ")
        cands = {m.group(0) for n in nearby_names for m in re.finditer(re.escape(prefix) + r"[\w:]*", n)}
        cands = {c for c in cands if len(c) > len(prefix)}
        if len(cands) != 1:
            return None
        mapped = cands.pop()
    return f"_Rb_tree<{key}, {mapped}>"
