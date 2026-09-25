"""Splitting qualified names without breaking template arguments.

"URefCounter<ActTextureDatabase::TextureInfo>::URefCounter" is the method URefCounter of the class
"URefCounter<ActTextureDatabase::TextureInfo>": a "::" inside <> belongs to a template argument.
ghidra/NightfireNamespaces.py carries its own copy of split() (Ghidra scripts can't import this).
"""


def split(qualified):
    """Top-level parts: 'A<B::C>::D::e' -> ['A<B::C>', 'D', 'e']."""
    parts, depth, cur, i = [], 0, "", 0
    while i < len(qualified):
        c = qualified[i]
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        if depth == 0 and qualified.startswith("::", i):
            parts.append(cur)
            cur, i = "", i + 2
            continue
        cur += c
        i += 1
    parts.append(cur)
    return parts


def bare(qualified):
    return split(qualified)[-1]


def namespace(qualified):
    """'A<B::C>::D::e' -> 'A<B::C>::D', or '' for a global name."""
    return "::".join(split(qualified)[:-1])
