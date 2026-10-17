"""Place sheet rows on PS2 functions by aligning each window between confidently placed rows.

A window runs from one placed row (A) to the next (B), where A < B in retail and the retail span fits the sheet
span (retail no longer, and at least 60%). Inside it, rows and retail functions are aligned in order; each step
pairs a row with a function, skips a row (absent from retail: inlined or removed) or skips a function (a static
the symbol file doesn't list).

Pair score, a log-likelihood ratio:
  size    retail == sheet: log(P_same / P(size)) - an exact match is worth more the rarer its size is;
          retail < sheet: a mild penalty growing as it shrinks; retail > sheet: heavy (retail is never bigger)
  string  an allocator string "Class new" / "Class new[]" in the function: strongly for a row of that class
Each row's confidence is the margin between the best alignment through its chosen function and the best one
that places it anywhere else or nowhere (forward/backward passes). validate_align.py sets the threshold.
"""

import bisect
import math
import re
from collections import Counter

NEG = -1e9
P_SAME_SIZE = 0.68          # measured: known pairs whose retail size equals the sheet size
SKIP_ROW = -2.0
SKIP_FUNC = -2.0


class Scorer:
    def __init__(self, ix, strings=None):
        self.ix = ix
        addrs = ix.ps2_addrs
        self.retail_size = {a: b - a for a, b in zip(addrs, addrs[1:])}
        counts = Counter(self.retail_size.values())
        total = sum(counts.values())
        self.p_size = {s: c / total for s, c in counts.items()}
        self.strings = strings or {}      # PS2 address -> [string constants]

    def pair(self, row, func):
        s, r = row["size"], self.retail_size.get(func)
        if s is None or r is None:
            return 0.0
        if r == s:
            score = math.log(P_SAME_SIZE / max(self.p_size.get(r, 1e-4), 1e-4))
        elif r > s:
            score = -8.0 if r - s > 8 else -3.0
        else:
            ratio = r / s
            score = -0.5 if ratio >= 0.7 else (-1.5 if ratio >= 0.4 else -3.0)
        for text in self.strings.get(func, ()):
            m = re.match(r"^([A-Za-z_][\w:]*) new(\[\])?$", text)
            if not m:
                continue
            cls = m.group(1)
            name = row["name"]
            if name.startswith(cls + "::operator new"):
                score += 8.0
            elif "operator new" in name:
                score -= 8.0
        return score


def align_window(rows, funcs, scorer):
    """rows, funcs: the window's rows and retail functions, in order. Returns [(row, func or None, margin)]."""
    m, n = len(rows), len(funcs)
    P = [[scorer.pair(rows[i], funcs[j]) for j in range(n)] for i in range(m)]
    F = [[NEG] * (n + 1) for _ in range(m + 1)]          # best score of rows[:i] against funcs[:j]
    F[0][0] = 0.0
    for i in range(m + 1):
        for j in range(n + 1):
            if i == 0 and j == 0:
                continue
            best = NEG
            if i and j:
                best = max(best, F[i - 1][j - 1] + P[i - 1][j - 1])
            if i:
                best = max(best, F[i - 1][j] + SKIP_ROW)
            if j:
                best = max(best, F[i][j - 1] + SKIP_FUNC)
            F[i][j] = best
    B = [[NEG] * (n + 1) for _ in range(m + 1)]          # best score of rows[i:] against funcs[j:]
    B[m][n] = 0.0
    for i in range(m, -1, -1):
        for j in range(n, -1, -1):
            if i == m and j == n:
                continue
            best = NEG
            if i < m and j < n:
                best = max(best, B[i + 1][j + 1] + P[i][j])
            if i < m:
                best = max(best, B[i + 1][j] + SKIP_ROW)
            if j < n:
                best = max(best, B[i][j + 1] + SKIP_FUNC)
            B[i][j] = best
    out = []
    for i in range(m):
        through = [F[i][j] + P[i][j] + B[i + 1][j + 1] for j in range(n)]
        skip = max(F[i][j] + SKIP_ROW + B[i + 1][j] for j in range(n + 1))
        if not through or skip >= max(through):
            best_alt = max(through) if through else NEG
            out.append((rows[i], None, skip - best_alt))
            continue
        j = max(range(n), key=lambda k: through[k])
        alt = max([skip] + [t for k, t in enumerate(through) if k != j])
        out.append((rows[i], funcs[j], through[j] - alt))
    return out


def windows(ix, is_function_row, anchor_ok):
    """Yield (rows, funcs) for every well-formed window between anchors (rows for which anchor_ok(row))."""
    rows = ix.rows
    idx = [k for k, r in enumerate(rows) if is_function_row(r)]
    known = [k for k in idx if anchor_ok(rows[k])]
    pos = {k: n for n, k in enumerate(idx)}
    for a, b in zip(known, known[1:]):
        between = idx[pos[a] + 1:pos[b]]
        if not between:
            continue
        ra, rb = rows[a], rows[b]
        if rb["ps2"] <= ra["ps2"]:
            continue
        span_s, span_r = rb["sym"] - ra["sym"], rb["ps2"] - ra["ps2"]
        if not (0.6 * span_s - 0x100 <= span_r <= span_s):
            continue
        lo = bisect.bisect_right(ix.ps2_addrs, ra["ps2"])
        hi = bisect.bisect_left(ix.ps2_addrs, rb["ps2"])
        funcs = ix.ps2_addrs[lo:hi]
        if len(between) * max(len(funcs), 1) > 400000:
            continue  # too big to align (a missed anchor); left for block placement
        yield [rows[k] for k in between], funcs
