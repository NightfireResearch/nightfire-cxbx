# Functions Ghidra cut short at a call

In October 2026 several functions turned out to be much longer than Ghidra showed them. Each ended, in Ghidra, at a
call, and the code after the call was outside any function. The decompile, the function's body, the exports
(`tools/functions_*.json`, `xrefs_*.json`) and everything built on them saw only the part before the call. Code
reimplemented from those decompiles was missing the rest.

## The two causes

Ghidra stops following a function's code at a call when it believes the call does not return:

- **The callee is marked no-return** (`noreturn` in its signature). Every call to it then ends its caller.
- **The call has a flow override** (in the listing: `-- Flow Override: CALL_RETURN (CALL_TERMINATOR)`), which treats
  that one call as a tail call. Ghidra's own "Non-Returning Functions - Discovered" analyzer adds these to the call
  sites of a function it has marked no-return when its "Repair Flow Damage" option is on, and clearing the callee's
  flag afterwards does not remove them.

A genuinely non-returning call (an exit, a fatal error loop) is followed by padding or by another function. Code
after it is the sign of a cut-short function.

## What was found

`python tools/ghidra_cutoff_scan.py` lists every call that is the last instruction of its function's body, with the
bytes after it.

### Action engine (default.xbe): fixed

All five calls to `d3dBindBuffers` (0x000e4990) carried `CALL_RETURN` overrides, although `d3dBindBuffers` itself
returns normally and is not marked no-return: left over, most likely, from a time it was. They cut short:

| Function | What was hidden |
|---|---|
| `RecurseAndDrawBoxes` (0x000dd4c0) | five sixths of it: the whole per-strip loop. It is the game's model renderer (skinning, render state, morph streams, the draws), not the small buffer-binding helper Ghidra showed |
| `d3dResetRenderTargetAndBuffers` (0x000e5fc0) | the stream reset and the wait for the GPU to go idle |
| `FUN_000e64c0` | all of it after the bind: it frees a vertex buffer (now `D3D_ReleaseVertexBuffer`) |
| `maybeResetRenderState` (0x000e6550) | the stream reset and clearing the border-colour texture |
| `maybeD3dShutdown` (0x000e7370) | the level-change teardown: every texture, vertex buffer and index buffer freed |

The last one had a real cost. Our `maybeD3dShutdown` was written from the cut-short view, so it never freed
anything, and the D3D tables filled up after four or five level loads (grey textures, then a crash). That was taken
for a leak in the game itself, and a teardown of our own (`d3dReleaseLevelResources`) was added to make up for it.
The original did free everything; our port now does what it does, and the workaround is gone. The same view had hidden
that our index-buffer allocator tested the wrong field for a free slot (the original tests the pointer the teardown
clears). `tools/ui/scripts/level_reloads.txt` loads a level nine times and checks the tables empty each time.

The overrides were cleared and the five functions repaired in Ghidra (below); the scan now finds none in
default.xbe.

### Driving engine (Driving.xbe): still open

`FUN_0017891e` is marked no-return, but it returns normally (`RET 0x10`); so does the one function it calls
(0x00178912, a `bsf` and a `ret`). It computes the bit masks for texture swizzling (the nv2a interleaves the bits of
x and y in a texel's address; the masks are the familiar 0x55555555 and 0xaaaaaaaa patterns). Its six callers are cut
short, each with a `CALL_RETURN` override on the call:

| Function | Called from |
|---|---|
| `FUN_00178992`, `FUN_00178a92`, `FUN_00178b90` | `XGSwizzleRect` (0x0017982a) |
| `FUN_00178c89`, `FUN_00178d6c`, `FUN_00178e7e` | `XGUnswizzleRect` (0x00179f7b) |

These are the per-format inner loops of the two XGRPH library functions that convert between a plain raster and the
swizzled layout (hand-written MMX, with a block of NOPs in the middle of the loop). Ghidra shows each as its first
dozen instructions; the loop and the return are outside any function.

**What it means for the driving engine:**

- **Nothing at run time.** Our driving seam replaces `XGSwizzleRect` and `XGUnswizzleRect` at their entry points
  (`Seam_XGSwizzleRect` / `Seam_XGUnswizzleRect` in `src/driving/gfx/d3dSeam.cpp`, through `d3d8Entries.inc`) with
  its own swizzle copy, written from the format rather than from these decompiles. The cut-short helpers are never
  reached.
- **Ghidra misleads anyone reading them.** The decompiles of the six helpers stop before the loop, and those of
  `XGSwizzleRect` / `XGUnswizzleRect` show calls into functions that appear to end there. Use the original bytes
  (or our seam) as the reference for swizzling, not these decompiles. Our seam ignores `XGSwizzleRect`'s destination
  point, on the grounds (its comment says) that only the whole-surface form is used.
- **The exports are short too.** About 1.4 KB of code sits outside any function in `tools/functions_driving.json`
  and `xrefs_driving.json`, and their references read as data to any tool that walks those (coverage, ownership).
  None of it touches game state, but a reference from it would be misfiled.
- **The analyzer keeps it that way.** Clearing the flag and the overrides and repairing the six functions works for
  a few seconds: the "Non-Returning Functions - Discovered" analyzer (on for Driving.xbe, threshold 3, "Repair Flow
  Damage" on) then marks `FUN_0017891e` no-return again and puts the overrides back. Its evidence is circular: the
  bytes after the calls are not disassembled because the callers are cut short, and undisassembled bytes after a
  call are what it takes as a sign of no-return. With six callers and a threshold of 3 it wins every time.

**To fix it:** turn off "Non-Returning Functions - Discovered" (or just its "Repair Flow Damage") in Driving.xbe's
Analysis Options, then repair as below, all six in one go. Once none of the callers is cut short the analyzer has no
evidence left, so it should not come back if the option is turned on again.

## Repairing a function in Ghidra

1. Clear the flow override on the call (right-click the call, Modify Instruction Flow, Default; or the MCP tool
   `clear_instruction_flow_override`), and untick No Return on the callee if it is wrongly marked.
2. Run Clear Flow and Repair over the function, from its entry to the end of its `ret`. A seed range that runs on
   into the padding after the `ret` pulls the padding into the body; a code region never disassembled may need a
   second pass seeded on just that region.
3. Restore the function's signature: the repair resets it to `undefined name(void)`.
4. Sync, so the exports pick up the new bodies, and re-run `tools/ghidra_cutoff_scan.py`.

Repairing changes only the function's body in Ghidra's database. The decompiler follows control flow rather than
the recorded body, so a function can decompile in full while its body is still short: check the body (the function's
address range), not the decompile.
