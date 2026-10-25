# Deterministic maths

The C forms of the x87 code take their transcendentals from DetMath, which gives the same bits on every host:
32-bit and 64-bit Windows, Linux and macOS, x86 and arm64. The C forms are what both engines build when their x87
option is off (`NIGHTFIRE_X87` for the driving engine, `NIGHTFIRE_ACTION_X87` for the action engine's standalone
targets) and in every 64-bit build. Without it, the same build would give different results on different hosts. The
x87 builds are untouched: actioninject, drivinginject and the 32-bit standalone builds with the x87 forms compile
none of it.

The code is src/common/math/DetMath.cpp. The C forms reach it through src/common/math/Libm.h. CMake's
`NIGHTFIRE_DET_MATH` (on by default) chooses DetMath. Off, the C forms use the host's `<math.h>`.

## What varies between hosts, and what does not

Measured on 24 October 2026, with about a million arguments per function, on this PC (AMD Ryzen 9800X3D) and the
runner (Intel i7-9700K). The C runtime was linked statically, so that only the CPU differed:

- **The x87's transcendental instructions** (FSIN, FCOS, FPTAN, FPATAN, F2XM1, FYL2X) differ between AMD and Intel
  by one unit in the 64th significand bit, in 2-6% of results. Rounded to a float, none differed.
- **The C libraries** differ from each other in the last bit of a double, in 0.01-8% of results:
  - MSVC's UCRT against glibc;
  - the 32-bit UCRT against the 64-bit UCRT;
  - the 32-bit UCRT on AMD against the same on Intel.

  The 32-bit UCRT's sin and cos also lose their range reduction above about 2^20. glibc's double functions gave
  the same results on x86-64 and arm64.
- **SSE's estimate instructions.** RSQRTSS (and RCPSS) are only specified to within 1.5 * 2^-12, and AMD's
  answers differ from Intel's. The driving engine's ports use RSQRTSS (`VU0_rsqrt`, the unit vectors). The C builds
  take its answer from the runner's Intel table (SseC.h's `RsqrtEstimate`, through Sse.h), as arm64 always has. With
  the instruction itself, the Linux build on this PC's AMD parted from the Intel runner's sound traces at tick 1. With
  the table, it matched them to tick 9000. The x87 builds keep the instruction.
- **The basic operations** (`+ - * /`, square root, `fmod`, conversions) are exact in IEEE 754, so they give the
  same results everywhere. That holds only when each operation is rounded once to its type, and only if the
  compiler does not fuse multiplies with adds.

The game's state mostly lives in floats, so a 1-ulp difference in a double rarely survives. The action engine's 12
recordings make about 124 million transcendental calls. In 0.85% of them the UCRT and DetMath gave different
doubles, but not once a different float. The recordings play every frame on AMD and Intel, on the x87 build and on
the C builds alike (docs/action-timedemo.md). A difference that does reach a float changes the game from then on.
DetMath rules that out.

## The functions

`Sin`, `Cos`, `Tan`, `Atan`, `Atan2`, `Asin`, `Acos`, `Exp`, `Log` and `Pow`, in double. These are fdlibm's
algorithms and coefficients (FreeBSD msun), using nothing but IEEE double additions, multiplications, divisions and
square roots. Arguments above 2^20 * pi/2 are reduced in integers (Payne and Hanek's method) with a 1,280-bit table
of 2/pi.

- **Accuracy:** within 1 ulp of the UCRT over every argument range tried, huge arguments, subnormals and special
  values included.
- **NaNs:** they follow the x87's conventions. A NaN argument comes back quiet; an invalid operation gives the x87's
  default NaN (negative), where SSE2 and NEON disagree on its sign; with two NaNs, the one with the larger
  significand wins.
- **FSin, FCos and FPtan in the C forms** return an argument of 2^63 or more unchanged, as the instructions do.

tools/detmath/detprobe.cpp checks a host: each function over random, wide, huge and special arguments, written out
and compared byte for byte between hosts with tools/detmath/compare.py. It gave identical results for all of these:

- MSVC x86 (SSE2), x64, and x64 with `/arch:AVX2`;
- gcc and clang on x86-64, and gcc `-m32 -msse2 -mfpmath=sse`;
- gcc for aarch64, under emulation.

Built with fused multiply-adds (gcc's default `-ffp-contract=fast`, on x86-64 with `-march=haswell` or on aarch64),
the results differed. Apple Silicon hardware is not checked yet.

## What a host must do

- **SSE2 or NEON double arithmetic, not the x87.** DetMath.cpp refuses to compile for x87 maths. 32-bit Linux uses
  `-msse2 -mfpmath=sse`, and the mingw clang target and MSVC have SSE2 by default.
- **No fused multiply-adds.** Non-MSVC builds use `-ffp-contract=off` (CMakeLists.txt, and the macOS toolchain).
  MSVC fuses only with `/fp:contract` or `/fp:fast`. DetMath.cpp also turns contraction off itself.
- **No fast-math**, nor anything else that reassociates.
- **The default control state:** round to nearest, no flush-to-zero or denormals-as-zero, every exception masked.
  The time demos report a game thread whose state is anything else (`[demo] frame N: UNEXPECTED floating-point
  control`, src/common/fpuState.h).

What DetMath cannot change: on 32-bit x86, MSVC returns floats in ST0 and sometimes computes an expression around a
call on the x87, at 53 bits, without rounding to float even at a cast or an assignment (driving/portable.md, "The
x87 option"). A 32-bit C build can therefore still differ from a 64-bit one in the rare case of a double rounding.
The 64-bit builds are the portable reference.

## Other host differences

Besides the maths:

- **Converting an infinity or a NaN to an integer.** x86 gives the integer indefinite (the lowest value), arm64
  saturates. src/common/floatToInt.h gives x86's result everywhere, and is the plain cast on x86. It is used where
  data can make the value degenerate: `Door_SetupSwing` with a speed of 0, the simple scripts' stop frame, and a
  bot's burst wait. The driving engine's `Truncate` and `RoundToInt` (X87.h, SseC.h) already did this.
- **NaNs.** An operation that makes a NaN from numbers (0/0, infinity minus infinity) gives a negative one on x86
  and a positive one on arm64. On 32-bit x86, a signalling NaN returned in ST0 comes back quiet (X87Ops.h's
  `X87Copy`). Neither has been seen in the game's state; a NaN position would show in a time demo's checksums.
- **Directory order** is sorted on every host (docs/core-deviations.md).
- **A bot's own message** (`DRONE_MSG_BotWeaponChanged`) carries its sender as a word of the game window
  (`PtrTo32`). It was a truncated address, which never matched on macOS, where the window lies above 4 GB.
- **Movies in a time demo** end on their audio's length whether or not the host has a sound device
  (docs/action-timedemo.md).
- **The heap's layout** differs between 32-bit and 64-bit hosts (structures that hold pointers are bigger), so a
  heap that runs out does so at another moment. Nothing guards against that; normal play should not run out.

## Why not the Xbox's bits

The original ran the x87's instructions on the Xbox's Pentium III. A portable library cannot match them:

- Intel does not document how its microcode computes FSIN or FPATAN, and AMD's answers differ from Intel's in a few
  percent of cases.
- Whether a modern Intel CPU still gives the Pentium III's answers is unknown, and nothing here can check one. Only a
  test program on an Xbox could be the reference.

The x87 builds stay the reference against the original: they run the instructions themselves. The injected build's
shadow tests and the all-original runs compare our code with the original's on the same CPU, so the vendor never
enters into it. The C builds are a different game run from the x87 builds only where a difference reaches a float,
and in the recordings it never has. They play the x87 recordings as they are, with no rebase.
