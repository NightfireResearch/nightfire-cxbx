import re
from dataclasses import dataclass
from typing import Optional, List


@dataclass
class Symbol:
    num: int
    value: int
    size: int
    type: str
    bind: str
    vis: str
    ndx: str
    name: str


@dataclass
class FunctionSymbol(Symbol):
    args: Optional[str]


@dataclass
class ObjectSymbol(Symbol):
    pass


SYMBOL_LINE_RE = re.compile(
    r"""
    ^\s*
    (\d+):                    # Num
    \s+([0-9a-fA-F]+)          # Value
    \s+(\d+)                  # Size
    \s+(\w+)                  # Type
    \s+(\w+)                  # Bind
    \s+(\w+)                  # Vis
    \s+(\S+)                  # Ndx
    \s+(.*)$                  # Name (rest of line)
    """,
    re.VERBOSE,
)


FUNC_NAME_RE = re.compile(
    r"""
    ^([^(]+)                  # function name
    (?:\((.*)\))?$            # optional argument list
    """,
    re.VERBOSE,
)


def parse_symtab(text: str):
    functions: List[FunctionSymbol] = []
    objects: List[ObjectSymbol] = []
    other: List[Symbol] = []

    for line in text.splitlines():
        m = SYMBOL_LINE_RE.match(line)
        if not m:
            continue

        num = int(m.group(1))
        value = int(m.group(2), 16)
        size = int(m.group(3))
        typ = m.group(4)
        bind = m.group(5)
        vis = m.group(6)
        ndx = m.group(7)
        name = m.group(8).strip()

        base = Symbol(
            num=num,
            value=value,
            size=size,
            type=typ,
            bind=bind,
            vis=vis,
            ndx=ndx,
            name=name,
        )

        if typ == "FUNC":
            fn_match = FUNC_NAME_RE.match(name)
            fn_name = fn_match.group(1) if fn_match else name
            args = fn_match.group(2) if fn_match else None

            functions.append(
                FunctionSymbol(
                    num=num,
                    value=value,
                    size=size,
                    type=typ,
                    bind=bind,
                    vis=vis,
                    ndx=ndx,
                    name=fn_name,
                    args=args,
                )
            )

        elif typ == "OBJECT":
            objects.append(ObjectSymbol(**base.__dict__))

        else:
            other.append(base)

    return functions, objects, other
