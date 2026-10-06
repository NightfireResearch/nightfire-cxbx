# Style guide for reimplemented game code

The goal: our C++ should read as close as possible to what the game's own programmers wrote, while doing exactly
what the shipped binary does. A reader should see the game, not the decompiler, the compiler or our tooling.

This applies to both engines (`src/action/`, `src/driving/`), to new ports and to cleanups of old ones.

---------------------------------------------------------------------------------------------------------------

## 1. Behaviour first, then readability

- **Exact behaviour is not negotiable.** Same results bit for bit (floats included), same memory writes and
  layouts, same allocations (sizes, order, allocator), same calls in the same order, same random-number draws.
  Lockstep runs and shadow tests depend on it.
- **Within that, write what a human would have written.** Where two forms behave identically, choose the
  natural one. Where the natural form would change a result, keep the exact form and say why in a short comment.
- **Deliberate departures are rare and documented.** If we choose to differ (a fix, a PC concern), say so at the
  function, and in the subsystem's doc. Never silently "improve" game logic.

## 2. Strip compiler and decompiler artefacts

What Ghidra and MSVC produce is not what anyone wrote. Undo it wherever behaviour allows:

- **Control flow:** rebuild the loops, `if`/`else` chains and `switch`es the source had. Undo loop inversion,
  rotated loops, duplicated tails, jump-table indexing (`switch` on the value), `goto`s from flattened flow,
  conditions split by the optimiser, and common-subexpression temporaries. Keep side effects in their order.
- **Arithmetic:** write `x / 4`, not shifts and fix-ups; `x % n`, not the multiply-by-reciprocal; `a * 3`, not
  `lea`-style sums - unless the exact form matters (signedness, overflow, a float chain).
- **Inlined helpers:** where the compiler inlined an obvious helper (a `strlen` loop, `memcpy`, a vector length,
  an STL member), write the call or the helper again, if it gives the same writes in the same order.
- **Register and stack plumbing:** no variables named after registers or stack slots, no `local_` / `param_`
  / `uVar` / `extraout_` names, no casts the decompiler added to make its types fit.
- **Types:** use the type the value is (`bool` for a flag the code only tests, an enum for a set of named values,
  a struct pointer for an object) - subject to the exceptions in section 6.
- **No casts the language already does.** If the implicit conversion gives the right result on every compiler and
  target we build for (MSVC x86, clang i686-w64-mingw32), leave the cast out: `int deaths = player.deaths;`, not
  `int deaths = static_cast<int32_t>(player.deaths);`. Prefer typed pointers so casts aren't needed in the first
  place. A cast stays only where it changes the result - an unsigned value converted to `double` through
  `int32_t` because the original loads it signed, a `double` that starts an x87 chain - and then it's the
  narrowest honest one, with a short comment if the reason isn't obvious.

## 3. Data: structures, not pointer maths

- **Real layouts.** Every game object and record is a struct sized to the real thing, with `static_assert`s on
  its size and key offsets. Unknown fields are still fields: `unknown2C` (by offset). Arrays inside records are
  array fields.
- **Index arrays as arrays:** `voices[i].pitch`, never `*(float *)(base + i * 0x88 + 0x14)`.
- **Globals by name:** the `TYPE_AT` macros in `src/helpers.h` (`U32_AT`, `FLOAT_AT`, `PTR_AT`...), each global
  named once near the top of the file (`#define Clock U32_AT(0x001e5204)`); a block of globals that is one record
  becomes a struct mapped once. No local accessor helpers, no address constants sprinkled through code.
- **Classes stay the game's.** Methods are members of a class whose layout matches the object. Constructors and
  destructors are `Construct()` / `Destruct()` methods; the vtable stays the game's (first word), so our classes
  declare no `virtual` - see `docs/driving-injection-framework.md` for `VIRTUAL(n)` and overlay classes.
- **One definition per type.** Before defining a struct, look for it; shared records live in one header.
- **Globals we own become our variables.** Once every reader and writer of a global is ours, it stops being a
  `#define` at the game's address and becomes a definition of our own; the original address stays only in an
  `// XBE_GLOBAL(address, size)` tag above it, for the tools. Do it as you go where it's plainly safe (a static
  only one compilation unit uses - in the action engine, the `name.NNN` statics the compiler numbered per file),
  and leave wider ones (the Gfx struct, anything shared across subsystems) to the sweeps in
  `docs/global-coverage.md`, which check ownership with `tools/global_coverage.py` and then in game. It should
  never hold up a port.

## 4. Constants and names

- **Enums for related magic numbers** - tags, states, opcodes, modes - so switches read as names. Unknown members
  get neutral names (`kTag98`), never invented meanings.
- **Our own literals:** `1.0f`, not a load of the original's `.rdata` constant. Read the constant's bytes (not
  Ghidra's rounded decompile) and guard anything non-obvious: `static_assert(std::bit_cast<uint32_t>(k) == ...)`.
  Values the game writes at run time are globals, not constants.
- **Names** come from Ghidra, the PS2 build or the user's spreadsheets. A name we invent says so in a comment.

## 5. Comments: terse, and only what the code shows

- **Game code carries short comments.** Explain the non-obvious - a quirk kept on purpose, why a form must stay
  exact, a register convention - in a line or two. Longer explanation belongs in `docs/`.
- **Only what the code (or the original binary) shows.** No game content the code does not demonstrate: no
  story, places, characters, or what a level "is". A name in the code is used as the name (`HT_Level_EvilSilo` is
  "EvilSilo"); a list is described by how the code uses it ("the hints shared by every level"), not by what its
  text probably says. A content claim needs a source actually read (the decoded text, the original's strings,
  the user's sheets).
- **The same when finishing or rebasing someone else's code:** don't embellish its comments.
- **No narration** of what the next line obviously does, and no history ("this used to...") in game code.

## 6. Floating point and the ABI: where exactness wins

- **x87 chains** (values kept on the FPU stack across operations) are written in `double`, in the original's
  order, with `(float)` at every store to a float. A single `+ - * /` or `sqrt` on float operands stored straight
  to a float may be written in plain `float` (double rounding is innocuous there). No reassociation, no FMA
  (`#pragma fp_contract(off)` in float-heavy files). Comparisons keep their NaN behaviour.
- **Conversions:** `src/driving/platform/X87.h` (`Truncate`, `RoundToInt`, `Ftol` for `__ftol2`). An integer the
  original loads with `fild` is signed - convert through `int32_t`.
- **Returns:** a function that leaves an unrounded value in ST0 returns `double`. A flag the original returns as
  a full-register 0/1 must return `int`, not `bool`, if any caller tests all of EAX - `tools/narrow_returns.py`
  checks every byte-returning port against its callers.
- **Byte copies:** where the original moves a byte raw, don't let a `bool` normalise it; where it copies a block
  with padding, copy the block.
- **Uninitialised stack the original leaves in a record** is not reproduced; write zero and note it if a test
  must mask it.

## 7. Tooling in game code: minimal and unobtrusive

- **One tag line above a definition** is all the injection needs: `// FUNC_AT(0xADDR)` (or `// AUTOINJECT`,
  `// AUTOLTCG` for a register-argument adaptor, `// AUTOGEN` above a declaration of an original we still call).
  The definition line keeps the shape the preprocessor parses: fully qualified, `T* Name(` for pointer returns,
  no function-pointer types spelled out on it. Details: `docs/driving-injection-framework.md`.
- **Register-argument originals** get a small naked adaptor named as Ghidra names the function, over a plain C++
  core; nothing else in the code should know about registers.
- **Calls to originals not yet ported:** one named macro per callee near the top of the file, cast and address on
  one line (the coverage tools read that line); calls to anything already ported go to ours, directly.
- **No settings checks, logging or test branches inside reimplemented functions.** A devtools hook that must sit
  in game code (rare) is one guarded call into a devtools file, with a comment saying why it can't sit outside.
- **Data no shipped content reaches** is still ported faithfully, marked provisional with a one-time "untested"
  warning macro, not a synthetic test.
- **Action engine:** carry the GameCube build's debug checks (`docs/gamecube-checks.md`) as `NF_WARN` /
  `NF_ASSERT`, tagged with their source.

## 8. Tests live apart from game code

- **Shadow tests, harnesses, dumps and probes** go in their own compilation units under `src/<engine>/devtools/`,
  never inside a reimplemented function. A harness stands in front of a patched function from outside
  (`XbeOriginal_Redirect`, `XbeOriginal_Restore` in `src/common/xbeOriginal.h`) and is called from `Inject()` or a
  devtools hook.
- **Off by default**, behind an environment variable (`NIGHTFIRE_<NAME>SHADOW=1`) or a menu-script step; one
  summary line (`[tag] ...: N cases, M checks, D differ`).
- **Types in an anonymous namespace** in each devtools file (two shadows' same-named structs once merged and
  corrupted the heap).
- **A shadow must be able to see a difference:** check one plausible mutation would be caught. Compare by value,
  not by heap address; restore every byte of state a call writes between the original's run and ours.
- Developer conveniences that are not tests (file dumps, frame dumps, teleports) are settings in `settings.ini`,
  off by default, implemented in devtools files.

## 9. Files and layout

- One subsystem per file or small group of files, named after the game's classes or modules; each injected
  function declared in the header named like its `.cpp`.
- Both builds must compile: MSVC and clang for i686-w64-mingw32 (no MSVC-only library calls, `constexpr` for
  constants used in `static_assert`, inline assembly only as MSVC `__asm` blocks in naked adaptors).
- Preserve a file's existing line endings.
