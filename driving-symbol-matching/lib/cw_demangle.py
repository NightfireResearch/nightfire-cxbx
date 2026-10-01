"""Demangle Metrowerks CodeWarrior (cfront-style) names, as in Agent Under Fire's GameCube driving.elf.

    GetRotPos__23AICharacterEnemySunroofFiR7MATRIX4R6COORD3
        -> ("AICharacterEnemySunroof::GetRotPos", "int, MATRIX4 &, COORD3 &", False)
    __ct__8IODeviceFii  -> ("IODevice::IODevice", "int, int", False)
    Update__15WTriggerManagerCFv -> ("WTriggerManager::Update", "void", True)   (const)

Parameter types are written the way the PS2 symbol file writes them ("MATRIX4 &", "char *", "unsigned int"),
so the two can be compared. Anything not understood returns None rather than a guess.
"""

import re

BUILTIN = {"v": "void", "b": "bool", "c": "char", "s": "short", "i": "int", "l": "long", "x": "long long",
           "f": "float", "d": "double", "r": "long double", "w": "wchar_t", "e": "..."}
SPECIAL = {"__ct": None, "__dt": "~", "__nw": "operator new", "__dl": "operator delete", "__nwa": "operator new []",
           "__dla": "operator delete []", "__as": "operator=", "__eq": "operator==", "__ne": "operator!=",
           "__vc": "operator[]", "__cl": "operator()", "__pl": "operator+", "__mi": "operator-", "__ml": "operator*",
           "__dv": "operator/", "__lt": "operator<", "__gt": "operator>", "__le": "operator<=", "__ge": "operator>=",
           "__apl": "operator+=", "__ami": "operator-=", "__amu": "operator*=", "__adv": "operator/=",
           "__rf": "operator->", "__nt": "operator!", "__aa": "operator&&", "__oo": "operator||"}


class _Reader:
    def __init__(self, s):
        self.s, self.i = s, 0

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def take(self, n=1):
        out = self.s[self.i:self.i + n]
        self.i += n
        return out

    def number(self):
        m = re.match(r"\d+", self.s[self.i:])
        if not m:
            raise ValueError("number expected")
        self.i += len(m.group(0))
        return int(m.group(0))

    def name(self):
        n = self.number()
        return self.take(n)

    def qualified(self):
        """Q<n><len><name>... or <len><name>: returns 'A::B'."""
        if self.peek() == "Q":
            self.take()
            count = int(self.take())
            return "::".join(self.name() for _ in range(count))
        return self.name()


def _type(r, previous):
    quals = []
    while r.peek() in ("C", "V", "U", "S"):
        q = r.take()
        quals.append({"C": "const", "V": "volatile", "U": "unsigned", "S": "signed"}[q])
    c = r.peek()
    if c == "P" or c == "R":
        r.take()
        inner = _type(r, previous)
        text = f"{inner} {'*' if c == 'P' else '&'}".replace("* *", "**")
    elif c == "A":
        r.take()
        n = r.number()
        if r.take() != "_":
            raise ValueError("array")
        text = f"{_type(r, previous)} [{n}]"
    elif c == "F":
        r.take()
        params = _params(r, stop="_")
        r.take()  # "_"
        ret = _type(r, previous)
        text = f"{ret} (*)({params})"
        return text
    elif c == "M":
        r.take()
        cls = r.qualified()
        inner = _type(r, previous)
        text = f"{inner} {cls}::*"
    elif c.isdigit() or c == "Q":
        text = r.qualified()
    elif c in BUILTIN:
        r.take()
        text = BUILTIN[c]
    else:
        raise ValueError(f"type {c!r}")
    q = [x for x in quals if x in ("unsigned", "signed")]
    cv = [x for x in quals if x in ("const", "volatile")]
    base = " ".join(q + [text]) if q else text
    return " ".join(cv + [base]) if cv else base


def _params(r, stop=""):
    out = []
    while r.i < len(r.s) and r.peek() != stop:
        c = r.peek()
        if c == "T":          # repeat the n-th parameter (1-based)
            r.take()
            out.append(out[int(r.take()) - 1])
        elif c == "N":        # N<count><index>: repeat a parameter count times
            r.take()
            count, index = int(r.take()), int(r.take())
            out.extend([out[index - 1]] * count)
        else:
            out.append(_type(r, out))
    return ", ".join(out) if out else "void"


def demangle(mangled):
    """(qualified name, parameter list, const) or None."""
    s = mangled
    # The separator is the first "__" after the name that starts a class spec (a digit or Q) or F.
    start = 2 if s.startswith("__") else 0
    for m in re.finditer(r"__(?=[0-9QF])", s[start:]):
        cut = start + m.start()
        func, rest = s[:cut], s[cut + 2:]
        try:
            r = _Reader(rest)
            cls = r.qualified() if rest[:1] != "F" else ""
            const = False
            if r.peek() == "C":
                r.take()
                const = True
            if r.take() != "F":
                continue
            params = _params(r)
            if r.i != len(rest):
                continue
        except (ValueError, IndexError):
            continue
        if func in SPECIAL:
            last = cls.split("::")[-1].split("<")[0]
            special = SPECIAL[func]
            func = last if special is None else (special + last if special == "~" else special)
        return (f"{cls}::{func}" if cls else func), params, const
    return None
