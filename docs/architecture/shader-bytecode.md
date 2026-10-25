# Shader bytecode without an HLSL compiler

Status: done for the vertex and pixel shader translators and FXAA (October 2026), the first step of
[the portable build](../driving/portable.md). Shared by both engines.

The D3D9 backend (`src/common/gfx/d3d9Backend.cpp`) used to translate the game's NV2A vertex programs and
register-combiner pixel shaders to HLSL text and compile it with `D3DCompile` at first use. DXVK's native build has
no shader compiler, so the translators now write D3D9 shader tokens themselves. The frames the game draws are
compared bit for bit against baselines made with the compiled HLSL, so the bytecode does not just compute the same
thing: it rounds where Microsoft's compiler rounded.

## The pieces

| File | What it does |
|---|---|
| `src/common/gfx/shaderBuilder.h/.cpp` | The emitter. A program is built as scalar values (inputs, constants, literals, operations on values) and outputs; `Assemble` writes vs_2_0 or ps_2_0 tokens. |
| `src/common/gfx/nv2aVertexShader.h/.cpp` | NV2A vertex program decoding (moved from the backend) and its translation through the builder, with the prologue (declaration defaults, NORMPACKED3 unpacking) and epilogue (clip-space position, depth from w, the fog table, linear texture scaling) the HLSL had. |
| `src/common/gfx/nv2aPixelShader.cpp` | The combiner translation through the builder: texture stages, eight general combiners, the final combiner. |
| `src/common/gfx/fxaa.hlsl`, `fxaaBytecode.h`, `tools/gfx/fxaa_bytecode.py` | FXAA's HLSL is ours: compiled once by the script (D3DCompile, the arguments `fxaa.cpp` used at run time) and committed. |
| `src/common/gfx/devtools/shaderHlsl.cpp` | The two HLSL translators as they were, kept only as the reference for the check. |
| `src/common/gfx/devtools/shaderCheck.cpp`, `shaderCheckMain.cpp` | The check (below), in the game and as the `shadercheck` tool. |

## The emitter

**Values.** Every value is a node: a leaf (`v#`, `t#`, `c#`, `c[a0.x + n]`, a literal) or an operation (add, mul,
mad, dp3, dp4, min, max, slt, sge, cmp, rcp, rsq, frc, abs, exp, log, saturate, lit, texld, nrm) on other values,
with a negate flag on each use. Nodes are hash-consed, so a common subexpression is one node. A translator works
lane by lane: the NV2A program's registers become arrays of values, an instruction's masked write replaces some of
them, and what the program outputs at the end is the graph that is lowered. Dead code never reaches the output.

**Lowering** (`Assemble`) walks the live operations in the order they were made (the source program's order),
gathering later operations of the same kind whose operands come from the same registers into one vector instruction
under a write mask. Values live in components of the twelve temporaries from definition to last use; a pixel
shader keeps lanes in their own components (ps_2_0 swizzles are the identity or a replicate), a vertex shader puts
them anywhere. An operand whose lanes are not together in one register is gathered by movs, and the copies stay
for later readers until their components are needed (copies of leaves, or of values held twice, are evicted
first; when nothing fits, a register's few values are moved to make a whole one free). Literals are packed into
def'd constants, in registers the program does not read (past c191 when it reads relative to a0, and never the
backend's c200-c204). Read-port limits are kept: per instruction a vertex shader reads one constant and one input
register, a pixel shader two constants, one colour and one texture coordinate register; anything over is moved to a
temporary first. Relative constant reads set `a0.x` with `mova` when it holds another address. A pixel shader's
colour goes to `oC0` whole from one register.

What the D3D9 runtime's validator refuses (each found when it returned D3DERR_INVALIDCALL; `shadercheck` now puts
every program to it on a NULLREF device): an `lrp` or `nrm` writing a register it reads (they are macros); a
pixel shader instruction reading two different constant registers; a `dcl t#`/`dcl v#` mask that is not .x, .xy,
.xyz or .xyzw; and a pixel shader whose texlds come in more phases than ps_2_0 allows. The runtime counts a phase
for a texld that follows arithmetic, and tracks how deep in texture reads each register is per whole register (a
partial write keeps the deeper of old and new), so the lowering issues the coordinates and texlds of one depth of
dependent reads before the arithmetic on their results, as the compiler did, and keeps a value out of registers
deeper than itself.

Instruction order, register allocation and swizzles cannot change a result. What can is the arithmetic itself,
and that follows the compiler.

## Rounding as the compiler rounded

Each rule below was found by comparing the bytecode with what `D3DCompile` (d3dcompiler_47 10.0.26100.9168, the
same build on this PC and the runner) made of the old HLSL, and is implemented in `shaderBuilder.cpp` or the
translators:

- **Fusion is per vector instruction.** HLSL `a * b + c` (and the NV2A MAD, which the HLSL wrote that way) is a
  multiply then an add; the compiler fuses them into a mad after packing lanes into instructions. So the add's live
  lanes of one source operation that read the same registers are one instruction, and it becomes a mad only if one
  operand of every lane is a product of one vector multiply - the left operand's when both are. `R.x = a*b; R.y =
  c*d; M = R + C` does not fuse; a lane whose product was folded to zero (`R.z = x - x`) stops the others fusing;
  lanes that all feed one vector operation stay one instruction even when their operands' registers differ. A
  lane of a vector operation stays an add with zero (the compiler keeps the vector whole). The builder tags each
  node with the source operation it came from (`BeginVector`/`Lane`/`EndVector`) for this.
- **Products packed together.** The compiler also packs one statement's product into an instruction with another
  statement's product of the same operands (`mul r1.zw, r0.w, c27.xyzy`); a product packed so is no longer one
  multiply it can fuse, and with both operands of a sum products, it fuses the other. Modelled narrowly: a left
  product with the same first operand and a second from the same constant register as another operation's product
  is not fused.
- **floor** is an intrinsic the compiler expands after fusing, so `x - frc(x)` never fuses (`Floor` marks its add).
- **lrp.** `mad(t, b - a, a)` with the difference used only there is `lrp t, b, a`; HLSL's `lerp` and its
  `cond ? x : y` on 0/1 conditions both end up there. `a*b + (1-a)*c` is recognised as a lerp too.
- **nrm.** `x * rsq(dot(x, x))` is `nrm` (whose product then never fuses), but only when the vector sits in .xyz
  of the compiler's temporary; otherwise it stays dp3, rsq, mul.
- **dp term order.** The compiler reads a dp's operands in the order of the components they sit in, whatever their
  order in the source. The lowering models where the compiler keeps the values of each NV2A register (in the
  register's temporary, in their own lane unless an older value still needed - even by the instruction itself -
  holds it, else the lowest free component) and sorts the terms the same way.
- **Ranges.** Interval ranges on every node (pixel colour inputs 0..1, saturate, frc, sge 0..1, products,
  squares...): a saturate, max or min that cannot change its operand is dropped, which lets a product beneath it
  fuse.
- **Algebra the compiler does:** `x - x` is 0, `(p + q) - p` is q, `x*1`, `x+0` (outside vector operations),
  `x*0`, literal arithmetic. Division is a multiply by the reciprocal; `exp(x)` is `exp(x * log2(e))`.
- **The templates** (RCC's clamps, EXP and LOG, NORMPACKED3 unpacking, the depth and fog epilogue) are written in
  the forms the compiler produced for the HLSL (`1 - 2z` as one mad, `(d*d)*(z*z)` for the exp2 fog,
  `sge(-t*t, t*t)` for `mode == n`, the fmod with its sign by lrp...), read from its disassembly.

Where the old HLSL failed to compile (a write to R12, a read past c191), translation fails the same way.

## The check

`NIGHTFIRE_SHADERCHECK=1` runs, for every program the game translates, the old HLSL translator and `D3DCompile`
beside the new translator, and compares the two programs in a small vs_2_0/ps_2_0 interpreter on 384 sets of random
inputs and constants (often small integers, so that every select and clamp is taken; texture reads return a hash of
the coordinate bits). Every output component either writes is compared bit for bit (+0/-0 and NaN/NaN equal). The
interpreter rounds once per instruction and fuses mad, so the same arithmetic in the same order is the only way to
agree: a wrong instruction, an unfused product, a swapped dp term or a constant one bit off all show. A difference
is reported with where the two programs part: both are also run symbolically, and the first operation at which
their expression trees differ is printed (`ours: mad(...)`, `reference: add(mul(...), ...)`), with both
disassemblies. One line per program goes to the backend log, the reports to `d3d9_shadercheck.log`. The check is
not in the standalone build.

`shadercheck` (CMake target, `EXCLUDE_FROM_ALL`) does the same without the game, over `d3d9_shaders.log` and
`d3d9_pixel_shaders.log` files (the backend dumps every program it translates there, declarations included) and
over the XBEs' vertex programs (found by their header; each with all-FLOAT4 inputs and with every logged
declaration that declares exactly the registers it reads); each program is also created on a NULLREF device, so
the runtime's validator sees it as in the game; `fxaa.hlsl` is compiled and compared with the blob; a `.bin` of
raw tokens is only validated. `-v` lists every program, `-d` adds the disassembly.

## Results (14 October 2026)

- `shadercheck` over every shader log on disk (59 programs) and the logs of a full lockstep round (294 more,
  all eight missions): every program the game translated computes what the compiled HLSL computed, and the
  runtime accepts all of them. The XBEs' 190 vertex programs all agree with all-FLOAT4 inputs. Paired with every
  logged declaration of exactly their inputs, 106 of 298 pairings (4 driving, 102 action) still differ in a last
  place - how the compiler treats the literal lanes of a partly declared input inside a vector operation, which
  is not modelled - but these are pairings no log has shown the game making. FXAA's blob is byte-identical to what D3DCompile makes of `fxaa.hlsl`.
- Driving, `BASE=lbase tools/runner/round.sh p3 lockstep` on the runner: 16 of 16 runs pixel-exact (16 frames each,
  to tick 9000), sound traces identical.
- Action, the weapons replay: with `NIGHTFIRE_SHADERCHECK=1`, 11 shaders checked, none differ, shaders 20 and 22
  translated; the screenshots differ from a run of the base commit by as much as two runs of the base commit
  differ from each other (the replays are timed by the wall clock).

Instruction counts are within about 20% of the compiler's (vertex shaders up to 216 of vs_2_0's 256, pixel shaders
up to 44 of ps_2_0's 64).
