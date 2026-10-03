# The driving engine's maths library (EA maths, VU0, D3DX helpers)

What `Driving.xbe` does in the 154 functions that `tools/function_coverage.py --driving` classifies as
`platform.math`. It is written so that the library can be replaced **bit for bit**: for every function it gives
the calling convention as checked in the disassembly, the data layout, the arithmetic in the order the original
does it, and which floating-point instructions it uses, because that decides how a port can match.
Every address is Driving.xbe's (Ghidra program `/Xbox_EU/Driving.xbe`). Names are Ghidra's unless marked:
*(invented)* is a name made up here; *(PS2 name)* comes from the PS2 build's symbols (`/PS2_EU_51258/DRIVING.ELF`,
same API, VU0 implementation, so only names and intent carry over); *(D3DX)* is a Direct3D 8 extension library
function identified by its algorithm, convention and constants.

**Status (3 October 2026): ported**, bit for bit: `src/driving/platform/RealMath.cpp` (E1, E2's x87 functions,
the trig assembly), `VU0Math.cpp` (the SSE family) and `D3DXMath.cpp` (DX), checked by
`src/driving/devtools/MathShadow.cpp` (`NIGHTFIRE_MATHSHADOW=1`: 111 cases, 20,000 inputs each, every one the same
as the original). `Determinant4x4` and `Inverse` were generated statement for statement from the listing. The port
found three things this document had wrong, corrected below: `v3sub` advances `a` as well as `out` (8.4),
`Inverse` *returns* the determinant in ST0 rather than leaking it (8.1), and `BuildRotate` tests the unrounded
angle against zero (4.3).

Conventions in this document:

- "cdecl" means arguments on the stack, caller pops, plain `RET`. "stdcall" means `RET n`. *Register arguments
  are what Ghidra gets wrong*; every convention below was checked in the listing. Several functions start with
  `PUSH ECX`: that reserves a dword of stack, it is **not** an ECX argument (the coverage of such functions by
  a naive "register read before write" scan is misleading).
- `f(x)` means "rounded to float" (an `FSTP/FST float`, or any SSE single-precision result). An x87 value that is
  not marked `f()` stays in a register at the precision-control setting, which in our process is **53 bits**
  (section 3.1), so it is exactly a C++ `double`.
- `MATRIX4` is 16 floats, row-major, row-vector convention (`v' = v * M`): rows 0..2 are the basis vectors,
  row 3 the translation. `m[r][c]` is the float at `+ (4r + c) * 4`. Quaternions are `x, y, z, w` with `w` at
  `+0xc`. `VEC3` is 12 bytes (no alignment), `VEC4` 16 bytes.
- Port classes (section 3.3): **I** integer moves, **D** x87 arithmetic that C++ `double` reproduces,
  **S** SSE single precision, **M** mixed float/double temporaries, **A** needs x87 assembly.

Contents: 1 overview and counts, 2 data layouts and globals, 3 floating point (the crux), 4 function reference,
5 misfiled functions, 6 the dead transform tables at `0x001d18b8`, 7 what our code already calls, 8 quirks a port
must decide on, 9 port structure, order and tests.

---------------------------------------------------------------------------------------------------------------

## 1. Overview

The 154 addresses fall into six groups:

| group | range | count | what it is | convention | FP |
|---|---|---|---|---|---|
| **E1** EA system maths | `0x00108790..0x00108f64`, `0x0010a460..0x0010a522` | 26 | EA's portable library: trig in turns, `v3*`, `MATRIX4_*`, `random` | cdecl, floats returned in ST0 | x87 only |
| **E2** EA Xbox maths | `0x00114a60..0x00116e06` | 70 | EA's Xbox port of the PS2 `VU0_*` layer, plus `Build*`, `Extract*`, `Inverse`, `FindOBBIntersect`, `rrandom`, `BytesToCoord` | cdecl | SSE for vectors, x87 for the rest |
| **DX** D3DX maths | `0x00113264..0x00113a48` | 13 | Microsoft D3DX8 maths routines linked into `.text` (the `VU0_MATRIX4_mult`/`VU0_quattom4`/`VU0_m4toquat`/`VU0_MATRIX4_transpose` names are on D3DX code) | stdcall, return the output pointer in EAX | SSE and x87 |
| **T** dead transform pipeline | `0x00108f70..0x0010a5a1` | 8 | a software vertex transform/project/clip-code library reached only through two unreferenced function tables | cdecl, globals | x87 |
| **EA** EAGLAnim / EAGL internals | `0x00100ac0..0x001069c0` | 36 | misfiled: animation decompression and EAGL bitset/attribute helpers | mostly thiscall | x87 |
| **AN** `VU0_quatstoangvel` | `0x00011930` | 1 | misfiled: a file-local copy inside ActActor's unit | cdecl | x87 |

So the library a maths port owns is **E1 + E2 + DX = 109 functions** (95 of them reachable from outside the maths
code; the rest are internal helpers or dead). T should not be ported (section 6); EA and AN belong to other
subsystems (section 5).

By port class, over the 109: **I** 18, **S** 44, **D** 27, **M** 13, **A** 7 (section 4 gives each one; the
`VU0_MATRIX4set?rot` trio counts as I, since it only stores the results of the asm sin/cos).

Functions that **need x87 assembly** to match (class A): `sin_fractionalangle`, `cos_fractionalangle`,
`FUN_001087b0` (sincos), `FUN_001087e0` (tan), `atan_turns`, `FUN_00116d10` (Euler to quaternion, keeps an
FSIN result at 64 bits), and the FSINCOS step of `D3DXMatrixPerspectiveFovRH` (`0x001136e5`); `Inverse` too if
its x87-stack leak is to be reproduced (8.1). Everything else can be C++ (double where the original keeps x87
values, SSE intrinsics for the VU0 family).

---------------------------------------------------------------------------------------------------------------

## 2. Data layouts and globals

### 2.1 Types

| type | size | alignment the code needs | notes |
|---|---|---|---|
| `VEC3` | 12 | none | the E1 `v3*` functions; `FLD/FSTP float` per component |
| `VEC4` / `COORD4` | 16 | **16** for every SSE function that uses `MOVAPS` | the VU0 family reads and writes 16 bytes even for "xyz" operations; a 12-byte vector handed to them over-reads its neighbour |
| `QUAT` | 16 | 16 for the SSE ones, none for x87 and for `D3DXQuaternionMultiply` (`MOVUPS`) | `x y z w` |
| `MATRIX4` | 64 | 16 for `D3DXMatrixMultiply`, `MATRIX4Init`-style SSE users, `VU0_MATRIX4_vect*`, `D3DXVec3TransformCoord`'s matrix | row-major, row-vector convention |
| 3x3 rotation | 36 | none | `ExtractRotTrans` output, `maybeExtractQuatTransInternal` input: 9 floats, stride 12 |
| `D3DVIEWPORT8` | 24 | none | `DWORD X, Y, Width, Height; float MinZ, MaxZ` (`D3DXVec3Project`) |

"Keeps w": most VU0 functions that operate on x, y, z compute all four SSE lanes, then store the result and put
the destination's old `w` back (`MOV EDX,[out+0xc]` before the `MOVAPS` store, `MOV [out+0xc],EDX` after). The
old `w` is read *before* the store, so `out == in` works and `out.w` is the destination's own. A port must keep
this: callers rely on `w` surviving (and on `w` being written, by the functions that write it).

### 2.2 Globals

| address | size | owner | meaning |
|---|---|---|---|
| `0x001d187c..0x001d1893` | 6 dwords, `.data` | `random`, `seedrandom` | generator state `s0..s5` (section 4.2). The initial contents are `seedrandom(0)`: `f22d0e56 883126e9 c624dd2f 0702c49c 9e353f7d 6fdf3b64` |
| `0x002420f0` | 64, `.bss` | `MATRIX4_mult` | scratch copy of `b` when `b == out`. Not thread-safe; only the main thread uses it |
| `0x00243030..0x00243038` | 12, `.bss` | game (read by 30 functions) | a zero vector, never written. `FindOBBIntersect` copies it |
| `0x00242130..0x002421a0`, `0x001d18a0..0x001d18ac`, `0x001d1950..0x001d1987` | | dead pipeline T | section 6 |

There are **no lookup tables**: sine and cosine are the x87 `FSIN`/`FCOS` instructions, reciprocal square roots
are `RSQRTSS`. The float constants the functions load (all in `.rdata`):

| address | value | used by |
|---|---|---|
| `0x001a178c` | `0x40c90fdb` = 2pi as a float (6.2831855) | sin/cos/tan/sincos (turns to radians) |
| `0x001a1790` | `0x3e22f983` = 1/(2pi) (0.15915494) | `atan_turns` |
| `0x001a1edc` | `0x3b360b61` = 1/360 | `BuildRotate` (degrees to turns) |
| `0x00189de8` | 1.0 | many |
| `0x00189dec` | 0.0 | compares |
| `0x00189eb0` | 0.5 | quaternion extraction, `D3DXVec3Project`, perspective |
| `0x00189e00` | 2.0 | D3DX |
| `0x0018a4c0` | 0.25 | `D3DXQuaternionRotationMatrix` |
| `0x0018a4a8` | 2^32 | unsigned-to-float fix-up after `FILD` |
| `0x001a1fa0` | 2^-32 | `rrandom` |
| `0x00191c30` / `0x001a0b08` | +1e-5 / -1e-5 (`0x3727c5ac`) | `Inverse`, `0x001132d9` |
| `0x0018cb6c` | 1e-4 (`0x38d1b717`) | `FindOBBIntersect` |
| `0x00189ed8` | 0.1 | `FindOBBIntersect` |
| `0x00189fd0` | 255.0 | colour pack `0x00116370` |
| immediate `0x3b808081` | 1/255 | `BytesToCoord` |
| immediate `0x4e6e6b28` | 1e9 | `FindOBBIntersect` |

---------------------------------------------------------------------------------------------------------------

## 3. Floating point: what decides whether a port matches

### 3.1 The state the code runs under

- **x87 precision control is 53 bits** in our process: Windows' default control word (`0x027f`), and our D3D9
  device is created with `D3DCREATE_FPU_PRESERVE` (`src/common/gfx/d3d9Backend.cpp`), so D3D does not drop it to
  24 bits. The action engine confirmed this empirically (`DroneShadow.cpp`'s RecoverTime probe, and
  `docs/architecture/README.md`). Rounding is to nearest. Under PC=53 an x87 register keeps a 53-bit mantissa
  (with the wider exponent, which float inputs never need).
- **Transcendentals ignore precision control.** `FSIN`, `FCOS`, `FPTAN`, `FPATAN` and `FSINCOS` return a 64-bit
  mantissa whatever PC says (PC affects only `FADD/FSUB/FMUL/FDIV/FSQRT`). Their results also differ between Intel
  and AMD x87 implementations in the last bits.
- **MXCSR** is the default `0x1f80` (round to nearest, no FTZ/DAZ). The SSE code depends on it only in the sense
  that the port's SSE code runs under the same MXCSR, so it agrees whatever the value.
- `RSQRTSS` is an approximation (about 12 bits) whose exact output is CPU-specific (Intel and AMD differ). No
  function here refines it with a Newton step.

### 3.2 Why C++ `double` reproduces most of the x87 code exactly

All inputs are floats (24-bit mantissas). Under PC=53:

1. A product of two floats is exact in 53 bits, so the x87 product equals the C++ `double` product.
2. Every other x87 operation rounds once to 53 bits, exactly as a C++ `double` operation (SSE2 `addsd/mulsd/
   divsd/sqrtsd`) does. `FSQRT` at PC=53 is the correctly rounded double square root, as `sqrt(double)` is.
3. A final `FSTP float` rounds once more; double-to-float rounding after a single double operation is always
   the correctly rounded float result (53 >= 2*24+2), so a single x87 operation on two floats followed by a
   store is identical to the SSE float operation. Chains of two or more operations are not: there the port
   must compute in `double` and round once at the store.

So a C++ port matches bit for bit if it (a) evaluates in `double` in the **same association order** as the
listing, (b) rounds to float exactly where the original stores to a float (including stores to stack
temporaries, which several functions make in the middle of an expression), and (c) returns `double` where the
original leaves an unrounded value in ST0 (MSVC returns a `double` with `FLD qword`, which is exact; a `float`
return would round it). This is what the action engine's ports already do (`docs/driving-injection-framework.md`,
"Doing the arithmetic in double ...").

It cannot reproduce a 64-bit-mantissa value (MSVC has no 80-bit `long double`): any function that feeds an
`FSIN`/`FCOS`/`FPATAN` result into further arithmetic without first storing it as a float needs assembly. A
transcendental helper itself can be a naked function declared as returning `float`: MSVC stores the returned
ST0 with `FSTP dword` (one rounding from 64 bits, as the original's `FSTP float` does), while original callers
still find the full value in ST0.

Edge cases where C++ and x87 differ even so: `FLD float` of a **signalling NaN** quiets it (sets bit 22), so an
x87 copy (`FLD`/`FSTP`) changes an sNaN where a C++ integer copy would not; NaN payload/sign selection when two
NaNs meet differs between x87 and SSE. Neither matters for real data, but the shadow test will see them (9.3).

### 3.3 Port classes

| class | meaning | how to port |
|---|---|---|
| **I** | integer moves only (or `FLD/FSTP` copies) | plain C++. Bit exact except sNaN quieting where the original copies through the x87 |
| **D** | x87, float operands, no transcendentals | C++ `double`, same order, `(float)` at each store, return `double` where the original returns ST0 unrounded |
| **S** | SSE single precision | `_mm_*` intrinsics in the listed order (also mimics the 4-lane reads and keep-w stores). Scalar `float` C++ would match the arithmetic for MSVC /arch:SSE2 /fp:precise, but intrinsics are required for `RSQRTSS`, safer across compilers (the clang cross build), and keep the lane semantics |
| **M** | D or S with some temporaries rounded to float mid-expression | as D, with the float temporaries listed |
| **A** | transcendentals whose 64-bit result is used, or must keep the x87 stack shape | naked `__asm`, verbatim |

Compiler guard for the port file: `#pragma float_control(precise, on)` and `#pragma fp_contract(off)` (no FMA
contraction, no reassociation); the clang cross build should get `-ffp-contract=off`.

---------------------------------------------------------------------------------------------------------------

## 4. Function reference

Sizes are true sizes (first byte to the end of the last instruction), not the coverage tool's gap-to-next.
"Callers" are callers outside the maths code (the coverage tool's list, by subsystem); "maths only" means only
other maths functions reach it.

### 4.1 E1: EA system maths (x87, cdecl)

All are cdecl with plain `RET`; floats come back in ST0, EAX is garbage. None checks for zero length.

| address | name | bytes | arguments -> return | class | callers |
|---|---|---|---|---|---|
| `0x00108790` | `sin_fractionalangle` | 13 | `(float turns)` -> ST0 (64-bit) | A | 29: anim (RotateActor...), camera, render, effects, vehicles, AI, audio; maths: `BuildRotate`, `VU0_MATRIX4set?rot`, `0x00116d10` |
| `0x001087a0` | `cos_fractionalangle` | 13 | `(float turns)` -> ST0 (64-bit) | A | 25, same spread |
| `0x001087b0` | `sincos_fractionalangle` *(invented)* | 37 | `(const float *turns, float *sinOut, float *cosOut)` -> void | A | maths only (`MATRIX4_axisrotate`) |
| `0x001087e0` | `tan_fractionalangle` *(invented)* | 15 | `(float turns)` -> ST0 (64-bit) | A | `FUN_00080890` (engine.render) |
| `0x001087f0` | `atan_turns` | 17 | `(float y, float x)` -> ST0 | A | 20: AI (12), render, camera, physics, audio |
| `0x00108810` | `v3length` | 37 | `(const VEC3 *v)` -> ST0 (53-bit) | D | maths only (`v3unit`, `v3unitcrossprod`, `v3distance`) |
| `0x00108840` | `MATRIX4_multxlate` | 187 | `(const MATRIX4 *m, const VEC4 *v, MATRIX4 *out)` | D | `AIVehicle::GetRotPos` |
| `0x00108900` | `MATRIX4_multscale` | 151 | `(const MATRIX4 *m, const VEC4 *v, MATRIX4 *out)` | D (= S) | `GSystem::MATH_TransformPoints` |
| `0x001089a0` | `v3crossprod` | 5 | `JMP 0x0010a4de`: `(a, b, out)` | D | weapons `FUN_0005d3f0`, world `FUN_000beff0`, `FUN_000cf260` |
| `0x001089b0` | `v3unit` | 46 | `(const VEC3 *in, VEC3 *out)` | D | `Mine::Mine`, `FUN_0005d3f0`, `FUN_000beff0`; `MATRIX4_axisrotate` |
| `0x001089e0` | `VEC3_Dot` | 19 | `(const VEC3 *a, const VEC3 *b)` -> ST0 (53-bit), via `0x0010a4ba` | D | `WWorldMath::IntersectSegPlane` (2), our `AddModelGlare` |
| `0x00108a00` | `v3unitcrossprod` | 75 | `(a, b, VEC3 *out)` | D | `EAGL::Transform::BuildAimedTrans` |
| `0x00108a50` | `random` | 165 | `()` -> EAX `uint32` | I | `Noise::Init`, `AHorn::Play`; `rrandom` |
| `0x00108b00` | `seedrandom` | 65 | `(uint32 seed)` | I | `Noise::Init` |
| `0x00108b50` | `v3sub` | 44 | `(int n, const VEC3 *a, const VEC3 *b, VEC3 *out)` | D (= S) | 6 in engine.world (WCollisionMgr, IntersectSegPlane...), all pass `n = 1` |
| `0x00108b80` | `v3scale` | 56 | `(int n, const VEC3 *in, float s, VEC3 *out)` | D (= S) | `Mine::Mine`, `Missile::Missile`, `PHelicopter::PHelicopter`, `RigidBody::ApplyHeavyFriction`, world; `n = 1` |
| `0x00108bc0` | `v3distance` | 53 | `(const VEC3 *a, const VEC3 *b)` -> ST0 (53-bit) | D | `SRuleRange::CheckRule` |
| `0x00108c00` | `MATRIX4_mult` | 77 | `(const MATRIX4 *a, const MATRIX4 *b, MATRIX4 *out)` | D | `RPlayerCamera::UpdateADWeaponAnims`, `UpdateAutoDriveCam` |
| `0x00108ce0` | `MATRIX4_vect3mult` *(PS2 name, probable)* | 102 | `(const VEC3 *v, const MATRIX4 *m, VEC3 *out)` | D | `WCollisionMgr::CheckHitWindow` |
| `0x00108d50` | `MATRIX4_vect4mult` *(PS2 name, probable)* | 12, falls into `0x108d5c` | `(const VEC4 *v, const MATRIX4 *m, VEC4 *out)` | D | `WCollisionMgr::CheckHitWindow` (2) |
| `0x00108d5c` | `MATRIX4_rowmult` *(invented)* | 133 | **EAX** = `const float row[4]`, **ECX** = `const MATRIX4 *`, **EDX** = `float out[4]`; no stack arguments; `RET` | D | maths only (`MATRIX4_mult` 4x, `0x108d50`) |
| `0x00108e70` | `MATRIX4_axisrotate` *(PS2 name, probable)* | 245 | `(const VEC3 *axis, float turns, MATRIX4 *out)` | M | `RSkeletalObj::UpdateSpringMassSystem` |
| `0x0010a460` | `v3add_x87` *(invented)* | 45 | `(a, b, out)`, EBP frame | D (= S) | none: dead |
| `0x0010a48d` | `v3sub_x87` *(invented)* | 45 | `(a, b, out)`, EBP frame | D (= S) | maths only (`v3sub`) |
| `0x0010a4ba` | `v3dot_x87` *(invented)* | 36 | `(a, b)` -> ST0 (53-bit), EBP frame | D | maths only (`VEC3_Dot`) |
| `0x0010a4de` | `v3crossprod` (inner; Ghidra gives it the same name as `0x001089a0`) | 69 | `(a, b, out)`, EBP frame | D (not S) | maths only |

Pseudocode (`double` is an x87 register value, `float` a stored one):

```c
float sin_fractionalangle(float t)  { FLD 6.2831855f; FMUL t; FSIN; }      /* product rounded to 53 bits, FSIN 64-bit result left in ST0 */
float cos_fractionalangle(float t)  { FLD 6.2831855f; FMUL t; FCOS; }
void  sincos_fractionalangle(const float *t, float *s, float *c) { *s = f(FSIN(2pi_f * *t)); *c = f(FCOS(2pi_f * *t)); }  /* reloads *t, sin first */
float tan_fractionalangle(float t)  { FLD 2pi_f; FMUL t; FPTAN; FSTP ST0; }  /* drops the 1.0 FPTAN pushes */
float atan_turns(float y, float x)  { FLD y; FLD x; FPATAN; FMUL 0.15915494f; }   /* atan2(y, x) / 2pi, 64-bit atan, product rounded to 53 */

double v3length(const VEC3 *v)      { return sqrt((double)v->x*v->x + (double)v->y*v->y + (double)v->z*v->z); }  /* (xx + yy) + zz */
void   v3unit(const VEC3 *in, VEC3 *out) { double r = 1.0 / v3length(in); out->x = f(r * in->x); out->y = f(r * in->y); out->z = f(r * in->z); }
double v3dot_x87(a, b)              { return ((double)a.x*b.x + (double)a.y*b.y) + (double)a.z*b.z; }
void   v3crossprod(a, b, out)       { out.x = f(a.y*b.z - a.z*b.y); out.y = f(a.z*b.x - a.x*b.z); out.z = f(a.x*b.y - a.y*b.x); }  /* in double */
void   v3unitcrossprod(a, b, out)   { VEC3 t; v3crossprod(a, b, &t); v3unit(&t, out); }            /* t is float-rounded */
double v3distance(a, b)             { VEC3 d = { f(b.x-a.x), f(b.y-a.y), f(b.z-a.z) }; return v3length(&d); }
void   v3sub(int n, a, b, out)      { for (i = 0; i < n; i++) out[i] = { f(a.x-b.x), f(a.y-b.y), f(a.z-b.z) }; }  /* a and b do NOT advance (8.4) */
void   v3scale(int n, in, s, out)   { for (i = 0; i < n; i++) out[i] = in[i] * s; }   /* one multiply per component: = float multiply */

void MATRIX4_multxlate(m, v, out)   { for r: { for c < 3: out[r][c] = f(m[r][c] + (double)m[r][3]*v[c]);  out[r][3] = f(m[r][3]*v[3]); } }
void MATRIX4_multscale(m, v, out)   { out[r][c] = m[r][c] * v[c]; }
void MATRIX4_rowmult(row, m, out)   { out[c] = f(((row0*m[0][c] + row1*m[1][c]) + row2*m[2][c]) + row3*m[3][c]); }   /* all four c computed before any store */
void MATRIX4_mult(a, b, out)        { if (b == out) { copy b to 0x002420f0; b = 0x002420f0; } for r: MATRIX4_rowmult(a[r], b, out[r]); }
void MATRIX4_vect3mult(v, m, out)   { out[c] = f(((v.x*m[0][c] + v.y*m[1][c]) + v.z*m[2][c]) + m[3][c]); }   /* c = 0..2 */
```

`MATRIX4_mult` handles `out == a` (each output row reads only its own row of `a`) and `out == b` (the copy);
the copy is to a static buffer.

`MATRIX4_axisrotate` *(probable PS2 name)*. With `n = v3unit(axis)` (float), `s, c` from
`sincos_fractionalangle(&turns)` (floats), every product below in `double` unless marked:

```c
t  = 1.0 - c;                 A = n.x * t;   B = n.y * t;          /* double */
SX = f(s * n.x);  SY = f(s * n.y);  SZ = s * n.z;                   /* SZ double */
m[0][0] = f(n.x*A + c);        AY = n.y*A;
m[0][1] = f(AY + SZ);          AZ = n.z*A;   AZf = f(AZ);
m[0][2] = f(AZ - SY);          m[1][0] = f(AY - SZ);
m[1][1] = f(n.y*B + c);        BZ = n.z*B;
m[1][2] = f(SX + BZ);          m[2][0] = f(AZf + SY);               /* note: AZf, rounded, unlike m[0][2] */
m[2][1] = f(BZ - SX);          m[2][2] = f((n.z*t)*n.z + c);
m[0][3] = m[1][3] = m[2][3] = m[3][0] = m[3][1] = m[3][2] = 0;  m[3][3] = 1;
```

### 4.2 `random`, `seedrandom`, `rrandom`

State `s0..s5` at `0x001d187c, 80, 84, 88, 8c, 90` (section 2.2). `random` is an add-with-carry chain with
a counter, all 32-bit unsigned:

```c
uint32 random(void) {
    uint32 a = s4 + s5;            uint32 c = (a < s5 || a < s4);      /* carry of s4 + s5 */
    s4 = a;   a = a + s3 + c;      c = (a < s3);   s3 = a;
              a = a + s2 + c;      c = (a < s2);   s2 = a;
              a = a + s1 + c;      c = (a < s1);   s1 = a;
              s0 = s0 + a + c;
    if (++s5 == 0)                 /* the counter wrapped: ripple an increment */
        if (++s4 == 0) if (++s3 == 0) if (++s2 == 0) if (++s1 == 0) ++s0;
    return s0;                     /* EAX */
}
void seedrandom(uint32 seed) {
    s0 = seed + 0xf22d0e56;  s1 = s0 - 0x69fbe76d;  s2 = s1 + 0x3df3b646;
    s3 = s2 + 0x40dde76d;    s4 = s3 - 0x68cd851f;  s5 = s4 + 0xd1a9fbe7;
}
```

(The carry comparisons are as the listing does them: `a < s3` uses the value of `a` *after* adding `s3 + c`,
i.e. the usual "sum < addend" carry test, which misses the case `s3 = 0xffffffff, c = 1`; a port must copy the
expressions, not "fix" them into 64-bit adds.) The wrap ripple uses the values just stored.

`rrandom` (`0x00114a60`, 32 bytes, cdecl, no arguments, ST0) = `(double)(uint32)random() * 2^-32`, in [0, 1),
**unrounded** (the `FILD` + 2^32 fix-up gives an exact 32-bit integer, and the scale is exact), so a port returns
`double`. Callers: `AGun::Play`, `AWorldSound::Play`, `AVehicleWind::Play`. `random`'s other callers:
`Noise::Init` (which also seeds) and `AHorn::Play`. Nothing in physics or AI uses this generator; the state can be
owned by the port (nothing else reads `0x001d187c`).

### 4.3 E2: EA Xbox maths

All cdecl unless a register is named. "keeps w" as in 2.1.

#### Builders and extractors

| address | name | bytes | arguments | class | callers |
|---|---|---|---|---|---|
| `0x00114a80` | `BytesToCoord` (xyz) | 172 | `(uint32 rgb, const VEC4 *add, const VEC4 *mul, const VEC4 *add2, VEC4 *out)` | S (+ exact `FILD`) | `EKickAIElement`, `ESpawnSmackable` dtors, `Explosion::Simulate`, `Newton::SpawnFromEvent`, vehicles |
| `0x00114b30` | `BytesToCoord` (xyzw; same name) | 198 | `(uint32 rgba, add, mul, add2, VEC4 *out)` | S | `EKickObject` dtor, `Missile::Missile` |
| `0x00114c00` | `BuildScale` (uniform) | 21 | `(MATRIX4 *m, float s)` -> `0x116190(m, s, s, s)` | I | `ActActor::ActActor`, `RMuzzleFlash::DrawPOV` |
| `0x00114c20` | `BuildScale` (xyz; same name) | 5 | `JMP 0x00116190`: `(m, sx, sy, sz)` | I | 10: render, camera, effects, frontend |
| `0x00114c30` | `BuildTranslate` | 37 | `(MATRIX4 *m, float x, float y, float z)`: identity, row 3 = x,y,z | I | 9: render, camera, frontend |
| `0x00114c60` | `BuildRotate` | 366 | `(MATRIX4 *m, float degrees, float ax, float ay, float az)` | M | 18 (anim 8, AI, render, frontend) + our `RGlareManager` |
| `0x00114dd0` | `ExtractRotTrans` | 70 | `(const MATRIX4 *m, float rot[9], VEC3 *t)` | I | `RSceneObj::EnableTarget`; `ExtractQuatTrans` |
| `0x00114e20` | `MATRIX4_TransformPoint` | 24 | `(const MATRIX4 *m, const VEC4 *in, VEC4 *out)` -> `VU0_MATRIX4_vect3mult(in, m, out)` | S | 6 + ours |
| `0x00114e40` | `TransformPoint` | 24 | `(m, in, out)` -> `VU0_MATRIX4_vect4mult(in, m, out)` (`0x116280`) | S | 6 (render 4, events, world) |
| `0x00114e60` | `MATRIX4_RotateVector` | 24 | `(m, in, out)` -> `VU0_MATRIX4_vect3rotate(in, m, out)` | S | ours only (its original callers are ported) |
| `0x00114e80` | `OrthoInverse` | 122 | `(MATRIX4 *m)`, in place | D | 18 (world 11, AI, anim, camera, render, events) |
| `0x00114f00` | `Determinant4x4` | 224 | `(const MATRIX4 *m)` -> ST0 (53-bit) | M | maths only (`Inverse`) |
| `0x00114fe0` | `Inverse` | 1106 | `(MATRIX4 *m)`, in place | M (A to keep the leak) | `ActActor::SetupFOVConversion`, `ActActor::Draw`, `FUN_000a9680` (effects) |
| `0x00115440` | `maybeExtractQuatTransInternal` | 373 | **ECX** = `const float rot[9]`, **EDX** = `QUAT *out`; `RET`; clobbers EAX | M | maths only |
| `0x001155c0` | `ExtractQuatTrans` | 52 | `(const MATRIX4 *m, QUAT *q, VEC4 *t)`; writes `t.w = 1` | M | `DrawGroupDrawEffects` |
| `0x00115600` | `FindOBBIntersect` | 752 | `(const VEC3 *halfExt, const VEC4 *from, const VEC4 *to, VEC4 *hit)` -> AL bool | M | `PBondCar::ApplyDamage`, `PHelicopter::ApplyDamage`, `RLightning::AddRoundedBolt` |
| `0x001158f0` | `VU0_ExtractXAxis3FromQuat` | 87 | `(const QUAT *q, VEC3 *out)` | D | `SimpleRigidBody::GetRightVector` |
| `0x00115950` | `VU0_ExtractZAxis3FromQuat` | 80 | `(const QUAT *q, VEC3 *out)` | D | 6: weapons (Mine, Missile, Shell), camera, physics |
| `0x001159a0` | `VU0_SQTquattom4` | 265 | `(const VEC3 *scale, const QUAT *q, const VEC3 *t, MATRIX4 *out)` | M | `ActIKSolver`, `ActGlobalPoseOverrideArray::BlendMatrices`, `ActPoser::DoMainPose`, `RSceneObj::LocateFX` |
| `0x00116120` | `VU0_MATRIX4Init` | 105 | `(MATRIX4 *m)`: identity | I | 27 |
| `0x00116190` | `BuildScale` (third copy of the name) | 117 | `(MATRIX4 *m, float sx, float sy, float sz)` | I | `RigidBody` (4); `BuildScale` x2 |
| `0x00116210` | `VU0_v4Init` | 33 | `(QUAT *q)`: `(0, 0, 0, 1)` | I | 19 (camera 11, vehicles, physics...) |
| `0x00116370` | `VU0_v4tocolour` *(invented)* | 120 | `(const VEC4 *c, uint32 *out)` | M | `RParticleParticleCache::RenderBlock`, `FUN_000ab650` (render) |
| `0x00116440` | `VU0_MATRIX3x4_mult` | 486 | `(const MATRIX4 *a, const MATRIX4 *b, MATRIX4 *out)` | D | `Missile::SteerMissile`, `RPlayerCamera::UpdateRelativeAnimationCam`, `RigidBody` (2) |
| `0x00116b70` | `VU0_MATRIX4_3x3transpose` | 161 | `(const MATRIX4 *src, MATRIX4 *dst)` | I | `RCamera::CreateMatrix4Inv` |
| `0x00116c20` | `VU0_MATRIX4setxrot` | 78 | `(MATRIX4 *m, float turns)` | A-helper + I | 5: vehicles (PBondCar), `Sentry::RotateSRB`, camera, effects |
| `0x00116c70` | `VU0_MATRIX4setyrot` | 77 | `(MATRIX4 *m, float turns)` | A-helper + I | 14: weapons (Missile), vehicles (PBondCar physics x5), camera, physics |
| `0x00116cc0` | `VU0_MATRIX4setzrot` | 75 | `(MATRIX4 *m, float turns)` | A-helper + I | 5: `PBondCar::TwoWheelStunt`, `Sentry`, anim, `Util_PerturbVector` |
| `0x00116d10` | `VU0_EulerToQuat` *(PS2 name, probable)* | 247 | `(const VEC3 *eulerTurns, QUAT *out)` | A | `CARP::PathInfo::EvaluateSpline` |

"A-helper": calls sin/cos and only stores their results, so C++ calling the asm helpers (declared `float`)
matches.

Pseudocode:

```c
/* BytesToCoord (0x114a80) */
out.x = (float)((rgb >> 16) & 255); out.y = (float)((rgb >> 8) & 255); out.z = (float)(rgb & 255);   /* out.w untouched */
VU0_v4scale(out, 1/255.f (0x3b808081), out);           /* 3 lanes */
if (add)  VU0_v3add(out, add, out);                     /* keeps w */
if (mul)  VU0_v4multxyz(out, mul, out);                 /* 0x115ee0, keeps w */
if (add2) VU0_v3add(out, add2, out);
/* 0x114b30: out.xyzw = bytes 3,2,1,0 / 255 with the four-wide 0x115f30, 0x115af0, 0x115ec0, 0x115af0 */

/* BuildRotate (0x114c60) */
a = f(degrees * (1/360.f));                              /* FST to the argument slot */
if (degrees * (1/360.f) == 0) { VU0_MATRIX4Init(m); return; }   /* the UNROUNDED product (FCOMP of ST0): a denormal
                                                            angle rounds a to -0 but still builds a matrix; NaN goes on */
s = f(sin_fractionalangle(a));  c = f(cos_fractionalangle(a));
r = VU0_rsqrt(f((ax*ax + ay*ay) + az*az));               /* 0x115bc0: RSQRTSS, float; 0 if the bits are 0 */
nx = ax * r;   /* stays double */      ny = f(ay * r);  nz = f(az * r);
t  = 1.0 - c;  TX = t * nx; /* double */ TY = f(t * ny); TZ = f(t * nz);
SX = f(s * nx); SY = f(s * ny); SZ = f(s * nz);
m[0][0] = f(TX*nx + c);  m[0][1] = f(ny*TX + SZ);  m[0][2] = f(TX*nz - SY);
m[1][0] = f(TY*nx - SZ); m[1][1] = f(TY*ny + c);   m[1][2] = f(TY*nz + SX);
m[2][0] = f(TZ*nx + SY); m[2][1] = f(TZ*ny - SX);  m[2][2] = f(TZ*nz + c);
m[0][3] = m[1][3] = m[2][3] = m[3][0..2] = 0;  m[3][3] = 1;

/* OrthoInverse (0x114e80), in place */
swap 3x3 off-diagonals ([0][1]<->[1][0], [0][2]<->[2][0], [1][2]<->[2][1]; some via FLD/FSTP, some via MOV);
t = old row 3;  with R = the ORIGINAL rows 0..2:
row3[j] = f(-((t.x*R[j][0] + t.y*R[j][1]) + t.z*R[j][2]));   /* computed as (-(t.x*n) - t.y*n') - t.z*n'', identical */
/* m[0][3], m[1][3], m[2][3], m[3][3] untouched */

/* ExtractRotTrans (0x114dd0): rot[3r+c] = m[r][c] (r,c < 3); t = m[3][0..2]. ExtractQuatTrans: ExtractRotTrans to a local,
   maybeExtractQuatTransInternal(ECX = local, EDX = q), then t.w = 1.0f. */

/* maybeExtractQuatTransInternal: rot is 3x3 with stride 3 (R[r][c] = rot[3r+c]) */
s45 = R[1][1] + R[2][2];  tmp = f(s45);  tr = s45 + R[0][0];            /* double */
if (tr > 0) { s = sqrt(tr + 1); q.w = f(0.5*s); k = 0.5 / s;
              q.x = f((R[1][2]-R[2][1])*k); q.y = f((R[2][0]-R[0][2])*k); q.z = f((R[0][1]-R[1][0])*k); }
else { i = (R[1][1] > R[0][0]) ? 1 : 0;  if (R[2][2] > R[i][i]) i = 2;   /* ties keep the earlier index */
       i == 2: s = sqrt((R[2][2] - (R[1][1]+R[0][0])) + 1); q.z = f(0.5*s); k = (s == 0) ? s : 0.5/s;
               q.w = f((R[0][1]-R[1][0])*k); q.x = f((R[2][0]+R[0][2])*k); q.y = f((R[2][1]+R[1][2])*k);
       i == 1: s = sqrt((R[1][1] - (R[0][0]+R[2][2])) + 1); q.y = f(0.5*s); same k;
               q.w = f((R[2][0]-R[0][2])*k); q.z = f((R[2][1]+R[1][2])*k); q.x = f((R[1][0]+R[0][1])*k);
       i == 0: s = sqrt((R[0][0] - tmp) + 1); q.x = f(0.5*s); same k;          /* uses the FLOAT-rounded s45 */
               q.w = f((R[1][2]-R[2][1])*k); q.y = f((R[1][0]+R[0][1])*k); q.z = f((R[2][0]+R[0][2])*k); }
/* "k = s when s == 0": the divide is skipped, so the products are by 0 */

/* VU0_ExtractXAxis3FromQuat / Z (doublings are exact) */
X = (f(1 - (2zz + 2yy)), f(2xy + 2wz), f(2xz - 2wy));
Z = (f(2zx + 2wy), f(2zy - 2wx), f(1 - (2yy + 2xx)));

/* VU0_SQTquattom4: XX=2xx, YY=2yy, ZZ=2zz, XY=2xy, WZ=2wz stay double; ZXf=f(2zx), WXf=f(2wx), ZYf=f(2zy), WYf=f(2wy) */
m[0][0]=f((1-(ZZ+YY))*s.x); m[0][1]=f((WZ+XY)*s.y); m[0][2]=f((ZXf-WYf)*s.z); m[0][3]=0;
m[1][0]=f((XY-WZ)*s.x);     m[1][1]=f((1-(ZZ+XX))*s.y); m[1][2]=f((ZYf+WXf)*s.z); m[1][3]=0;
m[2][0]=f((WYf+ZXf)*s.x);   m[2][1]=f((ZYf-WXf)*s.y);   m[2][2]=f((1-(YY+XX))*s.z); m[2][3]=0;
m[3] = (t.x, t.y, t.z, 1);                                 /* column c is scaled by s[c] */

/* VU0_MATRIX3x4_mult: inputs copied to the stack first (aliasing safe) */
out[r][c] = f((a[r][2]*b[2][c] + a[r][1]*b[1][c]) + a[r][0]*b[0][c]);  (r, c < 3);  out[r][3] = 0;  row 3 untouched

/* VU0_MATRIX4_3x3transpose(src, dst): dst = src with the 3x3 block transposed; if src == dst only the six off-diagonal
   words are written, otherwise the rest is copied too. Integer moves. */

/* VU0_MATRIX4setxrot(m, t): c = f(cos(t)); s = sin(t); m = 0 (REP STOSD); m[0][0]=1; m[1][1]=m[2][2]=c; m[1][2]=f(s); m[2][1]=f(-s); m[3][3]=1
   setyrot: m[0][0]=m[2][2]=c; m[0][2]=f(-s); m[2][0]=f(s); m[1][1]=1; m[3][3]=1
   setzrot: m[0][0]=m[1][1]=c; m[0][1]=f(s); m[1][0]=f(-s); m[2][2]=1; m[3][3]=1   (cos is called first, then sin) */

/* VU0_v4tocolour (0x116370): calls __ftol2 (truncation) four times */
r = c.x * 255.0  /* double, passed straight to __ftol2 */;  g = f(c.y*255); b = f(c.z*255); a = f(c.w*255);
*out = (low8(trunc(r)) << 24) | (low8(trunc(g)) << 16) | (low8(trunc(b)) << 8) | low8(trunc(a));   /* no clamping */
```

`Determinant4x4`, with `m0..m15` the matrix in memory order and products of two floats exact:

```c
A = m10*m15 - m14*m11;   E = m14*m9 - m10*m13;   J = m13*m11 - m15*m9;
Gf = f(m14*m8);  Hf = f(m10*m12);  If = f(m13*m8 - m12*m9);          /* three float temporaries */
T0 = ((J*m6 + E*m7) + A*m5) * m0;
Q  = ((m12*m11 - m15*m8)*m6 + (Gf - Hf)*m7) + A*m4;
W  = ((m13*m11 - m15*m8)*m5 + (m15*m9 - m13*m11)*m4) + If*m7;
X  = ((Hf - Gf)*m5 + E*m4) + If*m6;
return ((T0 - Q*m1) + W*m2) - X*m3;                                    /* double, left in ST0 */
```

`Inverse(m)`: `det = Determinant4x4(m)`; it computes only when `det >= 1e-5`, `det <= -1e-5` or `det` is NaN;
otherwise it returns with `m` unchanged **and `det` still on the x87 stack** (8.1). It copies the matrix to stack
locals, keeps column 0 (`m0, m4, m8, m12`) and `1/det` (double) in x87 registers, and builds the adjugate from
2x2 minors of the copy, **12 of which are stored as float temporaries** (`ESP+0x30..0x50`) and the rest kept in
registers; each output element is `f(+-((p + q) + r) * (1/det))`. It is long (342 instructions) and the
float/double map is irregular, so the port should be a statement-by-statement transcription of the listing
(C++ with `double` registers and `float` temporaries, or an `__asm` block), verified by the shadow test. Only
rendering uses it (FOV conversion, `ActActor::Draw`, an effect).

`FindOBBIntersect` (a segment against an axis-aligned box in the box's frame; all vectors 16-aligned, `halfExt`
read with `FLD` so unaligned is fine):

```c
dir  = VU0_v4unitxyz(to - from);                         /* VU0_v4sub, VU0_v4unitxyz: RSQRTSS, no refinement */
face[i] = (from[i] < 0) ? -halfExt[i] : halfExt[i];      /* -0, 0 and NaN take +halfExt */
diff = face - from;                                      /* VU0_v4sub */
t[i] = 0; if ((double)dir[i]*dir[i] > 1e-4f) t[i] = f(diff[i] / dir[i]);
a[i] = (t[i] < 0) ? -t[i] : t[i];                         /* -0 stays -0 */
for (n = 0; n < 3; n++) {
    big = *(VEC3 *)0x00243030;                            /* the global zero vector */
    k = index of the smallest a[] (ties: the LATER index);  big[k] = 1e9;
    *hit = from + VU0_v4scale(dir, t[k]);                 /* VU0_v4scale (3 lanes) then VU0_v3add: hit.w = from.w */
    if (|hit.x| <= halfExt.x + 0.1f && |hit.y| <= ... && |hit.z| <= ...) return true;   /* NaN passes (8.6) */
    a += big;                                             /* VU0_v3add: drop axis k */
}
return false;                                             /* *hit holds the last candidate */
```

`VU0_EulerToQuat` *(probable)* (`0x00116d10`): `c0 = f(cos(e.x)), s0 = f(sin(e.x)), c1, s1, c2 = f(...)`, then
`sin(e.z)` is **left in ST0 and used unrounded** in four products; it fills a 3x3 rotation in a stack
`MATRIX4` (only the elements `D3DXQuaternionRotationMatrix` reads) and calls `D3DXQuaternionRotationMatrix(out,
&local)`. Because of the 64-bit `sin(e.z)` it needs assembly (or at least that product chain in `__asm`).

#### The SSE family (`VU0_*`)

All cdecl, plain `RET`, 16-byte-aligned operands (`MOVAPS`) unless noted, and "keeps w" unless noted. Lane
order of horizontal sums: the code rotates with `SHUFPS 0x39`, so a 3-lane sum is `(p0 + p1) + p2` and a 4-lane
sum is `((p0 + p1) + p2) + p3`, all in float (addition order between the first two is commutative and exact).
Functions returning a float do `MOVSS [tmp]; FLD [tmp]`: the result is already float-rounded, so a C++ `float`
return matches. `0x00115bc0` and the four unit functions treat a length-squared whose **bit pattern is 0** as
"no RSQRTSS": the multiplier is then 0 and the result is the zero vector. A denormal length-squared goes to
`RSQRTSS`, which returns +inf for it.

| address | name | bytes | arguments -> return | computes | callers |
|---|---|---|---|---|---|
| `0x00115ab0` | `VU0_v3add` | 31 | `(a, b, out)` | `out.xyz = a + b`, keeps w | 82 (physics 13, camera 13, AI 11, vehicles 10...) |
| `0x00115ad0` | `VU0_v4sub` | 31 | `(a, b, out)` | `out.xyz = a - b`, keeps w | 128 + ours |
| `0x00115af0` | `VU0_v4add4` *(invented)* | 25 | `(a, b, out)` | `out = a + b`, 4 lanes stored | 14 (render, weapons, physics...) |
| `0x00115b10` | `VU0_v4sub4` *(invented)* | 25 | `(a, b, out)` | `out = a - b`, 4 lanes | 11 |
| `0x00115b30` | `v3dotprod` | 47 | `(a, b)` -> ST0 float | `(a.x*b.x + a.y*b.y) + a.z*b.z` | 91 (AI 34, vehicles 14, world 12, physics 9...) |
| `0x00115b60` | `v3dotprod` (4-lane; same name) | 54 | `(a, b)` -> ST0 float | `((xx' + yy') + zz') + ww'` | 4 (events, `Shell::ApplyWorldEffects`, camera) |
| `0x00115ba0` | `VU0_sqrt` *(invented)* | 23 | `(float x)` -> ST0 float | `SQRTSS` | 20 (physics, vehicles, camera, audio) |
| `0x00115bc0` | `VU0_rsqrt` *(invented)* | 37 | `(float x)` -> ST0 float | bits(x) == 0 ? x : `RSQRTSS(x)` | 6 (world 3, weapons, render); `BuildRotate` |
| `0x00115bf0` | `VU0_v3length` | 45 | `(v)` -> ST0 float | `SQRTSS((xx + yy) + zz)` | 48 |
| `0x00115c20` | `VU0_v3lengthsquare` *(invented)* | 41 | `(v)` -> ST0 float | `(xx + yy) + zz` | 17 (physics 7, weapons 6...) |
| `0x00115c50` | `VU0_v4lengthsquare` *(invented)* | 47 | `(v)` -> ST0 float | 4-lane | `Newton::Simulate` |
| `0x00115c80` | `VU0_v4copy` *(invented)* | 16 | `(src, dst)` | 16-byte copy | 40 (camera 18...) |
| `0x00115c90` | `MatrixCopy` | 39 | `(src, dst)` | 64-byte copy | 17 |
| `0x00115cc0` | `VU0_v4crossprodxyz` *(invented)* | 59 | `(a, b, out)` | `a.yzx*b.zxy - a.zxy*b.yzx`, keeps w | 32 (vehicles 8, camera 8, physics 5...) |
| `0x00115d00` | `VU0_v4crossprod1` *(invented)* | 61 | `(a, b, out)` | same, `out.w = 1.0` | `RDecalManager::AddDecal` |
| `0x00115d40` | `VU0_v4unitcrossprodxyz` | 110 | `(a, b, out)` | `c = cross; out.xyz = c * rsqrt((cx*cx + cy*cy) + cz*cz)`, keeps w | 28 (vehicles 7, camera 7, anim, render...) |
| `0x00115db0` | `VU0_v4unitcrossprod1` *(invented)* | 110 | `(a, b, out)` | same, `out.w = 1.0` | 3 (events, `Shell::ApplyWorldEffects`) |
| `0x00115e20` | `VU0_v4unitxyz` | 73 | `(in, out)` | `out.xyz = in * rsqrt(3-lane sum)`, keeps w | 112 + ours |
| `0x00115e70` | `VU0_v4unit` *(invented)* | 74 | `(in, out)` | `out = in * rsqrt(4-lane sum)`, 4 lanes | 6 (`RCameraMath::BuildRotationQuat`, ...) |
| `0x00115ec0` | `VU0_v4mult` *(invented)* | 25 | `(a, b, out)` | `out = b * a`, 4 lanes | maths only (`BytesToCoord` 2) |
| `0x00115ee0` | `VU0_v4multxyz` *(invented)* | 31 | `(a, b, out)` | `out.xyz = a * b`, keeps w | 6; `BytesToCoord` |
| `0x00115f00` | `VU0_v4scale` | 38 | `(in, float s, out)` | `out.xyz = in.xyz * s`; **no alignment needed, reads and writes only 12 bytes** (`MOVSS` + `MOVHPS [in+4]`) | 85 + ours |
| `0x00115f30` | `VU0_v4scale4` *(invented)* | 28 | `(in, s, out)` | `out = in * s`, 4 lanes | 21 |
| `0x00115f50` | `VU0_v4addscale` *(invented)* | 44 | `(a, b, s, out)` | `out.xyz = s * (b + a)`, keeps w | 4 (AI, render, world) |
| `0x00115f80` | `VU0_v4scaleadd` | 44 | `(a, s, b, out)` | `out.xyz = s*a + b`, keeps w | 49 + ours |
| `0x00115fb0` | `VU0_v4scaleadd4` *(invented)* | 38 | `(a, s, b, out)` | `out = s*a + b`, 4 lanes | 4 (`PBondCar::ProcessPhysics`, camera, render) |
| `0x00115fe0` | `VU0_v3distancesquare` | 50 | `(a, b)` -> ST0 float | `d = a - b; (dx*dx + dy*dy) + dz*dz` | 32 |
| `0x00116020` | `vec3distance` | 54 | `(a, b)` -> ST0 float | `SQRTSS` of the above | 46 + ours |
| `0x00116060` | `VU0_v3lengthxz` *(invented)* | 38 | `(v)` -> ST0 float | `SQRTSS(x*x + z*z)` (`SHUFPS 0x02`) | 19 (AI 11, vehicles 5...) |
| `0x00116090` | `VU0_v3distancexz` *(invented)* | 47 | `(a, b)` -> ST0 float | `SQRTSS(dx*dx + dz*dz)` | 20 (AI 16...) |
| `0x001160c0` | `VU0_v3distancesquarexz` *(invented)* | 43 | `(a, b)` -> ST0 float | `dx*dx + dz*dz` | 4 (AI, `WTriggerManager::CheckCollide`) |
| `0x001160f0` | `VU0_v3negate` *(invented)* | 41 | `(v)` in place | XOR the sign bits of x, y, z (exact, also flips NaN signs) | 4 (`PBondCar::AddWheelForces`, WCollisionMgr) |
| `0x00116240` | `VU0_MATRIX4_vect3mult` | 60 | `(in, m, out)` | `out.xyz = (x*r0 + y*r1) + (z*r2 + r3)` (**note the pairing**), keeps w | 33 + ours |
| `0x00116280` | `VU0_MATRIX4_vect4mult` *(invented)* | 68 | `(in, m, out)` | `out = ((x*r0 + y*r1) + z*r2) + w*r3`, 4 lanes | 21 |
| `0x001162d0` | `VU0_MATRIX4_vect3rotate` | 60 | `(in, m, out)` | `out.xyz = (x*r0 + y*r1) + z*r2`, keeps w | 25 |
| `0x00116310` | `VU0_MATRIX4_vect4multarray` *(invented)* | 93 | `(const VEC4 *in, m, VEC4 *out, int n)` | the vect4mult above for each element; **n = 0 loops 2^32 times** | `RigidBody::ResetObject`, `RigidBody::RigidBody` |
| `0x001163f0` | `VU0_MATRIX4_vect3multsub` *(invented)* | 70 | `(in, m, out)` | `out = ((x*r0 + y*r1) + z*r2) - r3`, 4 lanes stored | `IsVisibleAgainstCurtains` |
| `0x00116630` | `VU0_fastqslerp` | 437 | `(const QUAT *q0, const QUAT *q1, QUAT *out, float t)` | below | 10 (anim 4, camera 3, `Missile::SteerMissile`...) |
| `0x001167f0` | `VU0_v4quatrotate` | 427 | `(const QUAT *q, const VEC4 *v, VEC4 *out)` | below | `GFXGallery::DEBRIS_Draw`, `FUN_000d4f90` (frontend) |
| `0x001169a0` | `VU0_v4quatrotate_xlate` | 454 | `(q, v, const VEC4 *t, VEC4 *out)` | rotate as below, then `out.xyz += t` | `FUN_000d4f90` |

Here `r0..r3` are the matrix rows as SSE vectors and `x` means `x` broadcast to four lanes.

```c
/* VU0_fastqslerp */
d  = q1 - q0;  s = q1 + q0;            /* 4 lanes */
A  = q1 * t;   B = q0 * f(1.0 - t);    /* 1 - t on the x87, stored as float: equals the float subtraction */
if (len2_4(d) <= len2_4(s)) out = A + B; else out = A - B;     /* x87 compare of the two float sums; NaN takes A - B */
k = f(1.0 / SQRTSS(len2_4(out)));      /* x87 divide stored as float = float divide */
out = out * k;                         /* all four lanes */

/* VU0_v4quatrotate (all float, lanes as listed) */
c  = cross(q, v);                      /* keep-w cross into a stack temporary (its w is stack garbage, never used) */
t  = q.w*v + c;                        /* (q.w*v) + c */
u  = cross(t, q);
d  = (q.x*v.x + q.y*v.y) + q.z*v.z;    /* 3-lane */
p  = VU0_v4scale(q, d);                /* 3 lanes */
p  = q.w*t + p;
out.xyz = p - u;                       /* keeps out.w */
```

### 4.4 DX: the D3DX maths routines in `.text`

These are Microsoft D3DX8 functions (the Xbox D3DX, which uses SSE), linked at `0x00113264..0x00113a48`
between XAPI and EA's `MEM` code rather than in the `D3DX` section. All are **stdcall** and return the output
pointer in EAX. EA's Xbox headers evidently map their `VU0_*` API onto them, which is why four carry `VU0_`
names; note Ghidra's prototypes for `VU0_quattom4` and `VU0_m4toquat` have the two parameters swapped (the output
is the first in both).

| address | name | bytes | arguments | class | callers |
|---|---|---|---|---|---|
| `0x00113264` | `D3DXVECTOR3::operator/=` *(D3DX, inline compiled out of line)* | 31 | **thiscall**: ECX = `VEC3 *`, stack `float w`; `RET 4` | D | maths only |
| `0x00113283` | `D3DXMatrixIdentity` *(D3DX)* | 86 | `(MATRIX4 *out)`, `RET 4` | I | maths only |
| `0x001132d9` | near-equal test *(invented: `D3DX_NearlyEqual`)* | 48 | `(float a, float b)`, `RET 8` -> EAX bool | D | maths only |
| `0x00113309` | `D3DXVec3TransformCoord` *(D3DX)* | 126 | `(VEC3 *out, const VEC3 *v, const MATRIX4 *m)`, `RET 0xc`; `m` 16-aligned, `v` and `out` not | M | maths only |
| `0x00113387` | `D3DXVec3Project` *(D3DX)* | 275 | `(VEC3 *out, const VEC3 *v, const D3DVIEWPORT8 *vp, const MATRIX4 *proj, const MATRIX4 *view, const MATRIX4 *world)`, `RET 0x18` | M | `FUN_000e4d80` (EAGL) |
| `0x001134ba` | `D3DXMatrixMultiply` (Ghidra: `VU0_MATRIX4_mult`) | 254 | `(MATRIX4 *out, const MATRIX4 *a, const MATRIX4 *b)`, `RET 0xc`, all 16-aligned | S | 96 (anim 25, EAGL 16, camera 10, AI 9, render 8, vehicles 7...) |
| `0x001135b8` | `D3DXMatrixTranspose` (`VU0_MATRIX4_transpose`) | 75 | `(MATRIX4 *out, const MATRIX4 *m)`, `RET 8`; `m` 16-aligned | I | 27 (physics 10, EAGL 7...) |
| `0x00113603` | `D3DXMatrixRotationQuaternion` (`VU0_quattom4`) | 226 | `(MATRIX4 *out, const QUAT *q)`, `RET 8` | M | 27 (camera 7, physics 6...) |
| `0x001136e5` | `D3DXMatrixPerspectiveFovRH` *(D3DX)* | 148 | `(MATRIX4 *out, float fovy, float aspect, float zn, float zf)`, `RET 0x14` | A (FSINCOS) + D | `FUN_000e46d0` (EAGL) |
| `0x00113779` | `D3DXMatrixOrthoRH` *(D3DX)* | 114 | `(out, float w, float h, float zn, float zf)`, `RET 0x14` | D | `FUN_000e4870` (EAGL) |
| `0x001137eb` | `D3DXMatrixOrthoOffCenterRH` *(D3DX)* | 162 | `(out, l, r, b, t, zn, zf)`, `RET 0x1c` | D | `EAGL::ViewPort::SetOrthographic` |
| `0x0011388d` | `D3DXQuaternionRotationMatrix` (`VU0_m4toquat`) | 308 | `(QUAT *out, const MATRIX4 *m)`, `RET 8` | M | 26 (camera 5, anim 4, events 4, physics 4...) |
| `0x001139c1` | `D3DXQuaternionMultiply` *(D3DX)* | 138 | `(QUAT *out, const QUAT *q1, const QUAT *q2)`, `RET 0xc`; unaligned (`MOVUPS`) | S | 5 (events, frontend) |

```c
/* D3DXMatrixMultiply: every row computed before any store (aliasing safe) */
out[r] = ((a[r].x*b0 + a[r].y*b1) + a[r].z*b2) + a[r].w*b3;          /* SSE, b0..b3 = rows of b */

/* D3DXMatrixRotationQuaternion: X2 = 2x, Y2 = 2y (float, exact); Z2 = 2z, WX = X2*w, WY = Y2*w, WZ = Z2*w, ZZ = Z2*z (double) */
XXf=f(X2*x) XYf=f(Y2*x) XZf=f(Z2*x) YYf=f(Y2*y) YZf=f(Z2*y)
m00=f((1-YYf)-ZZ)  m01=f(XYf+WZ)  m02=f(XZf-WY)  m03=0
m10=f(XYf-WZ)      m11=f((1-XXf)-ZZ)  m12=f(YZf+WX)  m13=0             /* (1-XXf) double here ...           */
m20=f(XZf+WY)      m21=f(YZf-WX)  m22=f(f(1-XXf)-YYf)  m23=0           /* ... but rounded to float here      */
m3 = (0, 0, 0, 1)

/* D3DXQuaternionRotationMatrix */
tr = (m11 + m00) + m22;                                   /* double */
if (tr > 0) { s = 0.5*sqrt(tr + 1); w = f(s); k = 0.25/s;
              x = f((m12 - m21)*k); y = f((m20 - m02)*k); z = f((m01 - m10)*k); }
else { d0 = (m00 - m11) - m22 (kept double, and d0f = f(d0)); d1f = f((m11 - m00) - m22); d2f = f(m22 - (m11 + m00));
       i = (d0 < d1f) ? 1 : 0;   /* d0 UNROUNDED against d1f; NaN gives 1 */   if (d[i]f < d2f) i = 2;
       j = next[i], k = next[j] with next = {1, 2, 0};
       s = 0.5*sqrt(d[i]f + 1); q[i] = f(s); k4 = 0.25/s;
       q[j] = f((m[j][i] + m[i][j])*k4); q[k] = f((m[k][i] + m[i][k])*k4); q.w = f((m[j][k] - m[k][j])*k4); }

/* D3DXMatrixPerspectiveFovRH: h = f(fovy*0.5); FSINCOS(h) -> c, s both stored as float (one rounding each) */
ys = c / s (double);  m00 = f(ys / aspect);  m11 = f(ys);  Q = zf / (zn - zf) (double);
m22 = f(Q);  m23 = -1;  m32 = f(Q * zn);  everything else 0

/* D3DXMatrixOrthoRH: m00 = f(2/w); m11 = f(2/h); C = 1/(zn - zf) (double); m22 = f(C); m32 = f(C*zn); m33 = 1 */
/* D3DXMatrixOrthoOffCenterRH: A = 1/(r-l) (double); Bf = f(1/(t-b)); C = 1/(zn-zf); Cf = f(C);
   m00 = f(2*A); m11 = f(2*Bf); m22 = Cf; m30 = f(-((l+r)*A)); m31 = f(-((b+t)*Bf)); m32 = f(Cf*zn); m33 = 1   (note Cf, unlike OrthoRH) */

/* D3DXVec3TransformCoord: SSE p = ((v.x*r0 + r3) + v.y*r1) + v.z*r2; out.xyz = p.xyz;
   if (!D3DX_NearlyEqual(p.w, 1.0f)) out /= p.w   (0x113264: k = 1/w double, out[i] = f(k*out[i]))
   D3DX_NearlyEqual(a, b): d = a - b (double); return d >= -1e-5f && d <= 1e-5f   (NaN: false) */

/* D3DXVec3Project: M = world*view*proj (whichever are non-null; D3DXMatrixMultiply into a 16-aligned local; identity if none);
   D3DXVec3TransformCoord(out, v, M); if (vp):
   out.x = f(((out.x + 1.0) * (double)vp->Width) * 0.5 + (double)vp->X);      (DWORDs via FILD, +2^32 if "negative")
   out.y = f(((1.0 - out.y) * (double)vp->Height) * 0.5 + (double)vp->Y);
   out.z = f((vp->MaxZ - vp->MinZ) * out.z + vp->MinZ); */

/* D3DXQuaternionMultiply (SSE, unaligned): out = ((q1*q2.w + (q1.wzyx*q2.x ^ signs)) + (q1.zwxy*q2.y ^ signs)) + (q1.yxwz*q2.z ^ signs)
   with the sign masks built from 0x80000000 by SHUFPS 0x11, 0x50, 0x82; = D3DX's q1*q2 product (rotation q1 then q2) */
```

---------------------------------------------------------------------------------------------------------------

## 5. Misfiled functions

### 5.1 EAGLAnim and EAGL internals, `0x00100ac0..0x001069c0` (36 functions)

These are classified `platform.math` only because the class rule in `tools/subsystems_driving.txt` lists
`QuatMultXxYxZ`, which makes it a math anchor, and the unnamed functions after it inherit that. They are
EAGLAnim's (and, from `0x00106330`, EAGL's) own helpers and should move to `platform.eagl`: drop `QuatMultXxYxZ`
from the `platform.math` class rule (on PS2 it sits with `QuatMultXxQ`, `QuatMultQxZ`, `SetAnimMemoryMap` in
EAGLAnim, `0x0026ba08`) so its neighbours inherit from the EAGLAnim anchors. They are x87 code with the same
porting rules; port them with EAGLAnim.

| address | bytes | convention | what | callers |
|---|---|---|---|---|
| `0x00100ac0` `QuatMultXxYxZ` | 120 (Ghidra's body stops at `0x00100aee`; the code runs to `0x00100b37`) | cdecl `(a, b, out)` | specialised quaternion product, x87 with two float temporaries. Unfunctioned code at `0x00100b40` is another quaternion product (probably PS2 `QuatMultXxQ` or `QuatMultQxZ`) | `FnDeltaSingleQ::EvalSQTMasked` |
| `0x00101aa0` | 169 | thiscall (ECX record), `RET 4` | dequantise 16-bit quaternion/translation fields (`FILD` * scale - 1) | `FnDeltaSingleQ::EvalSQTMasked`, `0x101c90` |
| `0x00101b50` | 139 | thiscall, `RET 8` | dequantise 8-bit fields by mode | same |
| `0x00101be0` | 172 | thiscall, `RET 8` | dequantise 4-bit fields by mode | same |
| `0x00101c90` | 430 | thiscall | channel setup (calls `0x000f7c90`, `0x000fde00`, `0x00101aa0`) | same |
| `0x00101e40` | 185 | cdecl `(float t, q0, q1, out)` | lerp two quaternions and normalise (`FSQRT`, 1/len) | `FnDeltaSingleQ`, `FnDeltaQ::EvalSQTMasked` |
| `0x001021a0`, `0x00102280` | 214, 228 | thiscall, `RET 0x18` | accumulate / subtract delta streams | `FnDeltaQFast::EvalSQTMask` |
| `0x00102fb0` | 165 | thiscall, `RET 8` | 6-bit delta dequantisation | `FnDeltaQFast::UpdateNextQs(Mask)` and the four around it |
| `0x001030c0`, `0x00103170` | 167, 186 | thiscall, `RET 0x14` | as `0x1021a0`/`0x102280` for `EvalSQT` | `FnDeltaQFast::EvalSQT` |
| `0x00103230`, `0x001032e0` | 120, 188 | thiscall (`RET 0x10`, `RET`) | memory-map offsets, integer only | `FnDeltaQFast::SetAnimMemoryMap` |
| `0x00103fa0`, `0x001040a0` | 137, 116 | thiscall, `RET 4` | `FnDeltaQ` dequantise (+ `0x104030`) | `FnDeltaQ::EvalSQTMasked` |
| `0x00104030` | 111 | cdecl | normalise a 3-vector, `w = 0` (`FSQRT`) | `0x1040a0` |
| `0x00104120`, `0x00104180`, `0x00104230` | 84, 119, 106 | thiscall | `FnDeltaQ` memory-map offsets, integer | `FnDeltaQ::EvalSQTMasked` |
| `0x001046b0` | 100 | stdcall `(float, float, float)`, `RET 0xc` | frame index from a ratio (`CVTTSS2SI`) | `FnTurnBlender::EvalSQT`, `EvalVel2D` |
| `0x00104720`, `0x00105960` | 180 each | stdcall, `RET 0xc` | align two 2D directions (`FSQRT`, divide) | `FnTurnBlender`/`FnRunBlender::AlignCycleBeginEnd` |
| `0x00104920` | 159 | stdcall, `RET 0xc` | cycle phase from time | `FnTurnBlender::EvalSQT`, `EvalVel2D` |
| `0x00104eb0` | 236 | cdecl | quaternion to facing (doubled products, float temporaries) | `FnTurnBlender::AlignVel`, `BlendBeginFacing`, `BlendEndFacing`, `FnRunBlender::AlignVel` |
| `0x00105e10` | 217 | thiscall, `RET 0xc` | root quaternion of a blend (virtual calls) | `FnRunBlender::ComputeBeginRootQ`, `ComputeEndRootQ` |
| `0x00106330`, `0x00106350`, `0x00106380` | 22, 35, 64 | thiscall (`RET 4`, `RET 4`, `RET 8`) | 256-bit bitset: copy, fill with 0/~0, set/clear one bit | EAGL `FUN_000f88c0`, `FUN_000f8940`, `FUN_000f8a10` |
| `0x00106540`, `0x001065d0` | 144 each | thiscall, `RET 8` | bitset OR / AND into a result | none (dead) |
| `0x001066f0` | 21 | cdecl `(out, a, b)` | thunk to `D3DXMatrixMultiply` | 9 EAGL functions, and the table at `0x001cec7c` |
| `0x00106830`, `0x00106880`, `0x001068d0`, `0x00106930`, `0x00106980` | 68..94 | thiscall, `RET 8` | look up a record by 16-bit id (`0x001067c0`) and copy its payload | none (dead) |

### 5.2 `VU0_quatstoangvel` (`0x00011930`, 1708 bytes)

A cdecl x87 function `(COORD3 *angVelOut, const COORD4 *q0, const COORD4 *q1, float dt)` sitting between
`ActActor::DrawWeapons` and `ActActor::SpawnWeapon`, called only by `ActActor::SpawnWeapon`: a file-local copy
compiled into ActActor's unit (the PS2 build has one at `0x00109ad8` in game code too, besides the library one).
Classified `platform.math` by its `VU0` prefix; it belongs to `engine.anim` and should be ported with ActActor.
No transcendentals (largest-component selection with `FABS`, then divides), so class D.

### 5.3 D3DX

The 13 DX functions (4.4) are Microsoft's, not EA's. By the tiers in `tools/subsystems_driving.txt` they are
`sys.d3d` (D3DX), like the D3DX section at `0x0015d380`; a range line `range 00113264 sys.d3d` ending at
`0x00113a50` would file them there. They are still worth porting with this library (a `D3DXMath.cpp`), because
game and physics code call them as EA's `VU0_*` API.

---------------------------------------------------------------------------------------------------------------

## 6. The dead transform tables at `0x001d18b8`

`0x00109170`, `0x00109230`, `0x00109300`, `0x00109400`, `0x00109d90`, `0x00109e60`, `0x00109f50` (and the
clip-code helper `0x0010a530`) are 8 of about 30 routines of a **software vertex-transform library**, probably
the Xbox build of PS2 `TRANSFORM_Vertices` (`0x00239498`). They are reached only through three function tables in
`.data`, and **nothing references the tables**: a scan of every 4-byte value in the XBE finds no pointer to
`0x001d18b8`, `0x001d18f8` or `0x001d1938` (or to `0x001d1894..0x001d18b4`), and Ghidra has no code or data
reference to them. The object file was linked for other symbols in it (the EBP-framed `v3*` helpers at
`0x0010a460..0x0010a522` are in the same block, and `v3sub`/`VEC3_Dot`/`v3crossprod` call them), and the tables
came along. **Do not port; mark dead.** Most of the routines are not Ghidra functions at all (`0x00108f70`,
`0x00108ff0`, `0x001090a0`, `0x00109510`..., `0x0010a070`, `0x0010a1a0`, `0x0010a240`, `0x0010a2c0`, `0x0010a3d0`);
the coverage tool's sizes for `0x00109400` (2448) and `0x00109f50` (1296) are gaps full of them.

Tables (each entry a `void (*)(int count, ...)` cdecl routine; the two 14-entry tables are followed by two zero
dwords):

```
0x001d18b8: 1096b0 109170 109510 108f70 | 109740 109230 109570 108ff0 | 1097f0 109300 109600 1090a0 | 1098d0 109400
0x001d18f8: 1096b0 109170 109510 108f70 | 109740 109230 1099d0 109d90 | 109b60 109f50 109a80 109e60 | 109c70 10a070
0x001d1938: 10a240 10a1a0 10a3d0 0 10a2c0 0          (clip-code passes; 0x10a530 computes one vertex's code)
```

The routines walk a vertex stream (`0x00242140`, stride `0x001d18a8` = 12) optionally through an index list
(`0x00242138`) into an output stream (`0x0024213c`, stride `0x001d18ac` = 32): rotate by the 3x3 at
`*0x00242158`, add the translation at `*0x00242154`, and/or project to a 640x480 screen with
`x' = x * 319.5 * rhw + 319.5`, `y' = 239.5 - y * 319.5 * rhw`, `z' = (z - 1) * (1/499)`, `rhw = 1/z` (10000 or
100000 when z is 0); the second table's variants also copy camera-space positions to a stream at `0x00242174`.
Constants at `0x001d1950`: 0.7071, 0.7071, 0.6, 0.8, 1.0, 500.0, 0.002004, 319.5, 319.5, 319.5, 239.5, 1, 640, 480.
The clip-code routines compare floats as integers against bounds at `0x00242184..0x0024219c`, AND the codes
into `0x001d18a0` (starts 0xff) and OR them into `0x00242150`.

---------------------------------------------------------------------------------------------------------------

## 7. What our code already calls (AUTOGEN declarations to replace)

| declaration | file | original | note |
|---|---|---|---|
| `void BuildRotate(MATRIX4 *, float degrees, float x, float y, float z)` | `src/driving/render/VectorMaths.hpp` | `0x00114c60` | correct |
| `void VU0_MATRIX4_vect3mult(const void *in, const MATRIX4 *m, Vec4 *out)` | same | `0x00116240` | `in` must be 16-aligned (`camera + 0x20` is) |
| `void VU0_v4unitxyz(const Vec4 *in, Vec4 *out)` | same | `0x00115e20` | correct |
| `void VU0_v4scale(const Vec4 *in, float, Vec4 *out)` | same | `0x00115f00` | only 12 bytes read/written |
| `void VU0_v4sub(const Vec4 *a, const void *b, Vec4 *out)` | same | `0x00115ad0` | correct |
| `void VU0_v4scaleadd(const Vec4 *a, float, const Vec4 *b, Vec4 *out)` | same | `0x00115f80` | correct |
| `float vec3distance(const void *a, const void *b)` | same | `0x00116020` | the header's comment says "x87": it is SSE (`SQRTSS`), only the return goes through `FLD`. `float` is right |
| `void MATRIX4_TransformPoint(MATRIX4 *, const Glare *, Glare *)` | `src/driving/render/RGlareManager.cpp` | `0x00114e20` | wraps `0x00116240`: 16-byte `MOVAPS` on in and out, keeps out.w |
| `void MATRIX4_RotateVector(MATRIX4 *, const _VEC3 *, _VEC3 *)` | same | `0x00114e60` | wraps `0x001162d0`, 16-byte accesses as above |
| `float VEC3_Dot(const _VEC3 *, const _VEC3 *)` | same | `0x001089e0` | **returns an unrounded 53-bit value.** The original `AddModelGlare` does `CALL VEC3_Dot; FMUL [esi]; FCHS; FSTP` (`0x000a9ba6`), multiplying the unrounded dot product; our port rounds it to float first (the `float` return), so it can differ by an ulp. Declare it `double` and do the multiply in `double` |

A port puts these in `RealMath.h` and deletes the AUTOGEN lines (the AUTOGEN generator would otherwise emit bodies
that call the originals).

---------------------------------------------------------------------------------------------------------------

## 8. Quirks and bugs a port must decide on

1. **`Inverse` returns the determinant** in ST0, unrounded, on both paths (the `FCOM`s do not pop it): every
   caller discards it with `FSTP ST0`. So it is not a leak; the port returns it as a `double`.
2. **Functions that return ST0 unrounded** (`v3length`, `VEC3_Dot`, `v3distance`, `rrandom`, `Determinant4x4`,
   and the transcendental helpers): the port must return `double` (or asm), not `float`, or original callers
   that keep computing see a rounded value (our `AddModelGlare` already has this problem, section 7).
3. **CPU-dependent approximations**: `RSQRTSS` (the unit-vector functions, 112+28+6+3 callers including vehicle
   physics) and `FSIN/FCOS/FPATAN` differ between Intel and AMD. Keep them: replacing `RSQRTSS` with an exact
   `1/sqrt` would change gameplay everywhere. A consequence independent of the port: replays recorded on one CPU
   vendor can drift on another (local machine vs `vr-desktop`).
4. **`v3sub(n, ...)` advances `a` and `out`, not `b`**: the loop reuses EAX as `a`, and `v3sub_x87` leaves
   `a + 12` there. Every caller passes 1.
5. **`VU0_MATRIX4_vect4multarray(in, m, out, 0)`** loops 2^32 times (`DEC ECX; JNZ`). Only `RigidBody` calls it,
   with constant counts; keep or guard.
6. **NaN handling in comparisons** follows the x87 `FNSTSW`/`TEST AH` patterns exactly as listed (`BuildRotate`
   builds a matrix for a NaN angle; `FindOBBIntersect` accepts a NaN hit; `Inverse` inverts with a NaN
   determinant; quaternion extraction picks index 1 for NaN). A C++ port must write the comparisons so that the
   unordered case goes the same way (e.g. `!(a <= b)` rather than `a > b` where the original's branch is taken on
   unordered).
7. **Zero lengths**: E1 `v3unit` divides by zero (NaN out); the VU0 unit functions return the zero vector for an
   exact `+0` length-squared and `inf`/NaN for a denormal one; `maybeExtractQuatTransInternal` multiplies by 0
   when its square root is 0.
8. **The keep-w stores** and the 4-lane reads of 3-vectors (2.1) are part of the interface.
9. **Static scratch** in `MATRIX4_mult` (`0x002420f0`): not thread-safe. Only the main thread calls it; a port
   can use a local instead (no visible difference).
10. **Mixed-precision asymmetries** (`MATRIX4_axisrotate`'s `m[0][2]` vs `m[2][0]`, `D3DXMatrixRotationQuaternion`'s
    `m11` vs `m22`, `maybeExtractQuatTransInternal`'s rounded `s45`, the OrthoRH/OrthoOffCenterRH `C` vs `Cf`):
    these are what make a "clean" port differ by an ulp; follow the maps in section 4.

---------------------------------------------------------------------------------------------------------------

## 9. Port structure, order and tests

### 9.1 Files

- `src/driving/platform/RealMath.h`: the types (`VEC3`; `alignas(16) VEC4`/`QUAT`; `MATRIX4` from the generated
  layouts), declarations of everything game code calls, with the return types of section 8.2. Replaces the
  AUTOGEN lines in `VectorMaths.hpp` and `RGlareManager.cpp`.
- `src/driving/platform/RealMath.cpp`: E1 and E2 (except the SSE family), `random`/`seedrandom`/`rrandom` with
  their state, and the naked x87 functions (`sin_fractionalangle`, `cos_fractionalangle`, `sincos`, `tan`,
  `atan_turns`, `VU0_EulerToQuat`, and an `FSINCOS` helper for the perspective matrix). `FUNC_AT` each.
  `#pragma float_control(precise, on)`, `#pragma fp_contract(off)`.
- `src/driving/platform/VU0Math.cpp`: the SSE family with `<xmmintrin.h>` intrinsics, op for op, and the 13 D3DX
  functions (stdcall, returning the output pointer). Could move to `src/common` later if the action engine needs
  D3DX too.
- The two register-argument helpers (`0x00108d5c`, `0x00115440`) need no register-ABI replacement: every caller is
  in this library. Port them as ordinary static functions and leave the originals unpatched (unreachable once
  `MATRIX4_mult`, `0x00108d50` and `ExtractQuatTrans` are ours). The same goes for `0x00113264`, `0x00113283`,
  `0x001132d9`, `0x00113309` (only `D3DXVec3Project` calls them) and `Determinant4x4`.
- Patch sizes: every entry has room for the 5-byte jump (`0x001089a0` and `0x00114c20` are themselves 5-byte
  `JMP`s; `0x00108d50` falls through into `0x00108d5c`, which the jump at `0x00108d50` does not touch).

Leave unported: section 6 (dead), section 5.1 (EAGLAnim's port), 5.2 (ActActor's port).

### 9.2 Order

1. Class I (identity, copies, `BuildScale` x3, `BuildTranslate`, `ExtractRotTrans`, the transposes,
   `VU0_v4Init`, `VU0_v3negate`, `random`/`seedrandom`): no floating point to get wrong.
2. The five transcendental helpers as naked asm, verbatim. Everything that only stores their results can then be
   C++ (`setx/y/zrot`, `BuildRotate`, `MATRIX4_axisrotate`).
3. The SSE family (40 functions, three of them plain copies; the largest caller counts: AI, physics, vehicles,
   camera) with intrinsics.
4. E1 x87 (`v3*`, `MATRIX4_*`, `VEC3_Dot` as `double`); fix `AddModelGlare`'s use of `VEC3_Dot` at the same time.
5. E2 x87/mixed: `BuildRotate`, the axis-from-quaternion pair, `VU0_SQTquattom4`, `OrthoInverse`,
   `VU0_MATRIX3x4_mult`, `VU0_fastqslerp`, the quatrotates, `FindOBBIntersect`, `BytesToCoord`, the colour pack,
   `Extract*`, `rrandom`; then `Determinant4x4`/`Inverse` and `VU0_EulerToQuat` last.
6. D3DX: `D3DXMatrixMultiply`, `Transpose`, `RotationQuaternion`, `QuaternionRotationMatrix` (physics uses
   these), then the projection builders and `D3DXVec3Project` (EAGL only).

Risk ranking: anything vehicle or rigid-body physics reaches is integrated every frame, so a one-ulp difference
grows into a different trajectory and breaks replays: `VU0_v4unitxyz`, `VU0_v4unitcrossprodxyz`, the dot products,
`VU0_MATRIX4setyrot` (PBondCar snowmobile/submarine physics), `D3DXMatrixRotationQuaternion`, `VU0_MATRIX3x4_mult`,
`VU0_MATRIX4_vect4multarray` and `BuildScale` (RigidBody), `atan_turns` and the XZ distances (AI decisions).
Rendering-only functions (`Inverse`, the projection builders, `VU0_v4tocolour`) are low risk.

### 9.3 Shadow test: `src/driving/devtools/MathShadow.cpp`

Modelled on `FileSysShadow.cpp`: run at injection time when `NIGHTFIRE_MATHSHADOW=1`, on the main thread, before
the game starts (per the test-runner rule, run it on `vr-desktop`, not as a local game window).

- **Calling the originals**: unpatched ones by address; patched ones inside `XbeOriginalScope(address)`. Test
  leaves before composites, since an original composite inside its scope calls the *ported* leaves (e.g. original
  `BuildRotate` -> ported `sin_fractionalangle`); that is fine once the leaves pass, and it localises failures.
  Register-argument originals (`0x00108d5c`, `0x00115440`, `0x00113264`) through small naked thunks.
- **Capturing results**: outputs compared bitwise over the whole buffer, with every byte (including `w`, row 3 and
  padding) pre-filled with a sentinel, so "keeps w"/"untouched" is checked; also run in-place (`out == in`,
  `out == b`) variants. EAX for the D3DX pointer returns and the bool returns. ST0 returns captured by a naked thunk
  as an 80-bit `FSTP tbyte` and compared as 80-bit values (a ported `double` return converts exactly). The thunk
  also records the x87 TOP field (`FNSTSW`) before and after, to catch stack imbalance (`Inverse`'s leak) and checks
  that the port leaves the same depth.
- **Inputs** (e.g. 100 000 cases per function, seeded): ordinary floats (+-1e-3..1e3), unit vectors, unit
  quaternions and rotation matrices built from them, raw random bit patterns (reach NaN, inf, denormals), and a
  special list: 0, -0, +-smallest denormal, +-FLT_MIN, +-1, +-FLT_MAX, +-inf, quiet and signalling NaN, and the
  thresholds (determinants near +-1e-5, traces near 0, length-squared exactly 0 and denormal, `dir^2` near 1e-4,
  angles 0 and exact quarter turns). Counts for `v3sub`, `v3scale`, `0x00116310`: 1..3.
- **Reporting**: count mismatches per function, print the first few with inputs; report "both NaN, different
  payload/sign" separately from real mismatches (x87 vs SSE NaN propagation, sNaN quieting by `FLD`).
- **Environment check**: print the x87 control word (`FNSTCW`, expect `0x027f`) and MXCSR (expect `0x1f80`) first;
  the x87-faithful `double` ports and the SSE originals are only equal under these.
- Run it on the MSVC build and the clang cross build.

After the shadow test passes, the replays that reach vehicle physics and AI are the integration check (targeted
first, the full suite on the runner).
