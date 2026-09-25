"""Give PS2 addresses to sheet rows that lack one, where the retail layout leaves no doubt.

Between two rows with known PS2 addresses (A, B), take the sheet rows in between and the PS2 functions in
between. If the counts are equal and every function's retail size equals the sheet size, the rows map one to
one, in order ("exact"). If the counts are equal but some sizes differ, the mapping is kept only as "count"
(weaker; the retail compiler can shrink a function). The known rows' own sizes must also match (retail
distance A->next function), so a sheet size that swallows unlisted static functions (deleteSysFiles) breaks
the run instead of shifting it.

A window whose retail span is longer than its sheet span, or under 60% of it, straddles a linker
discontinuity (the sheet lists EA's sound library twice) and is skipped.

Rows are the sheet's function rows only; data symbols (vtables, type_info nodes) are skipped.
"""

import bisect

# Data symbols. "global constructors/destructors keyed to X" are real functions (GCC static initialisers)
# and must stay in, or a walk past one shifts every name after it.
DATA_WORDS = (" virtual table", " type_info node")


def is_function_row(r):
    return r["sym"] is not None and not any(w in r["name"] for w in DATA_WORDS)


def infill(ix):
    """{row index: (ps2 address, 'exact' | 'count')} for rows without an address."""
    rows = ix.rows
    addrs = ix.ps2_addrs
    idx = [i for i, r in enumerate(rows) if is_function_row(r)]
    out = {}
    known = [k for k, i in enumerate(idx) if rows[i]["ps2"] is not None]
    for ka, kb in zip(known, known[1:]):
        ia, ib = idx[ka], idx[kb]
        a, b = rows[ia]["ps2"], rows[ib]["ps2"]
        if b <= a:
            continue
        # The two known rows must bound one stretch of code, not straddle a place where the linker put
        # things in a different order: retail can be smaller than the symbol build, never larger, and not
        # wildly smaller.
        span_sheet = rows[ib]["sym"] - rows[ia]["sym"]
        span_retail = b - a
        if not (0.6 * span_sheet - 0x100 <= span_retail <= span_sheet):
            continue
        between_rows = idx[ka + 1:kb]
        if not between_rows:
            continue
        lo, hi = bisect.bisect_right(addrs, a), bisect.bisect_left(addrs, b)
        between_funcs = addrs[lo:hi]
        # Sizes: the retail size of each function is the distance to the next function start.
        starts = [a] + between_funcs + [b]
        retail = [y - x for x, y in zip(starts, starts[1:])]
        sheet = [rows[i]["size"] for i in [ia] + between_rows]
        if len(between_rows) != len(between_funcs):
            # Counts differ (an unlisted static function, or one inlined away). Keep only the runs that
            # match size for size, walking forward from A and backward from B.
            n = 0
            while n < min(len(between_rows), len(between_funcs)) and sheet[n] == retail[n]:
                n += 1
            for i, f in zip(between_rows[:n], between_funcs[:n]):
                out[i] = (f, "exact-run")
            m = 0
            while (m < min(len(between_rows), len(between_funcs)) - n
                   and rows[between_rows[-1 - m]]["size"] == retail[-1 - m]):
                m += 1
            for k in range(m):
                out[between_rows[-1 - k]] = (between_funcs[-1 - k], "exact-run")
            continue
        exact = all(s == r for s, r in zip(sheet, retail))
        if not exact and any(s is None or r > s for s, r in zip(sheet, retail)):
            continue  # retail bigger than the symbol build: not the same layout
        for i, f in zip(between_rows, between_funcs):
            out[i] = (f, "exact" if exact else "count")
    return out
