# The driving engine on 64-bit

Status (15 October 2026): step 3 of the roadmap in [portable.md](portable.md) is done on Windows and Linux. The x86-64
standalone test executable runs every mission exactly as the 32-bit portable-C build does: all 16 lockstep runs match
`lbase_c` in frames and sound traces to tick 9000 (7.10). Done: package 0 (7.4, 4.3), the x87 code as C behind
`NIGHTFIRE_X87` (4.4), packages 1 and 2 (7.5: the x86-64 configuration, `Ptr32<T>` and the game window), 6 (7.6:
the memory-only structures), 3a and 3b (7.7: EAGL), 8 (7.8: D3D), 4 and 5 (7.9: CARP, sound, FILESYS), and 9
(7.10: integration and bring-up), and 10 (7.11: Linux x86-64, with the game window above 4 GB as macOS needs it). It
applies to the standalone executables (`driving_standalone`, `driving_standalone_test`) only. The
injected build (driving.exe mapping Driving.xbe, drivinginject.dll, every shadow test) stays 32-bit: it shares the
original's address space by construction, and it stays the oracle.

## Summary

| What | Measured | Recommended |
|---|---:|---|
| Structures with pointer-sized members | 1,078 of 1,607 | in memory only (916): native pointers, the structures grow |
| Structures read in place from the game's files | 110 (202 pointer-sized members) | `Ptr32<T>`: the file's 32-bit word stays where it is, decoded against a 4 GB "game window" |
| Layout checks (`static_assert` on sizes and offsets, with the generated ones) | 1,375; 1,017 fail on 64-bit (clang: 1,032 lines) | a 32-bit-only form for memory structures; the 31 on file structures must hold on 64-bit too. **Done (7.6):** 1,045 are `LAYOUT_32`; clang fails 57, all but one on structures laid out as files |
| Pointers made 32-bit integers explicitly (silent on 64-bit) | 108 (63 of them vtable constants) | typed pointers; the file words through `Ptr32` |
| 32-bit integers made pointers | 48, and 72 clang warnings | the same |
| Objects allocated by a literal size / pointer arrays sized `* 4` | 26 / 6 | `sizeof` |
| Naked functions | 108: 43 x87, 59 register adaptors, 4 injected-only, 2 others | x87: C in double behind `NIGHTFIRE_X87=OFF`, done (4.4); adaptors: left out of the standalone build |
| Calling conventions | 413 `__stdcall`, 72 `__fastcall`, 7 `__thiscall` | nothing: x86-64 has one convention; only the injection's ABI templates (19 errors) need an x86-only guard |
| Third-party | FFmpeg DLLs are i686 builds | build x86-64 DLLs with the same script; SDL3, D3D9, XAudio2, XInput, dxvk-native are fine |

Decisions needed (section 9): native or 32-bit pointers in memory-only structures; how the game window is placed on
Windows; what to do with the x87 code; the 64-bit Windows toolchain; the style-guide exceptions.

## 1. How it was measured

**`tools/pointer_layout_audit.py`** (new; Python, no packages) reads the standalone target's sources (CMakeLists.txt,
as `driving_standalone` takes them: 276 sources) and every header under src/driving and src/common outside devtools,
and classifies:

- every structure: its members (pointers, function pointers, vtable pointers, handles, `size_t`/`uintptr_t`,
  embedded pointer-bearing structures, `long`), pointer-bearing or not (through members, embedded structures and
  bases), and whether a loader reads it in place from a file (its `LOADERS` table, seeded from the reading in
  section 2, plus what those structures embed) or it lives only in memory;
- every `static_assert` on a size or an offset (and `XBE_CLASS_SIZE`/`XBE_FIELD`): stays true, fails on 64-bit
  (the size of a pointer-bearing structure; an offset after a pointer-sized member, or of a pointer whose comment
  puts it at 4 mod 8), or unknown;
- pointer/integer conversions, masks, literal-sized allocations, word streams, pointer printing;
- every naked function (x87, register adaptor, injected-only, other) with its x87 instructions and how often our
  code names it; the calling-convention keywords.

`--md` prints the tables, `--list KIND` / `--file PATH` / `--struct NAME` the findings. It reads the generated
overlay fields and layout checks when a build has written them (the numbers here are with them; a fresh checkout
without them reports 1,255 checks, 846 failing).

**The 64-bit syntax check.** `--probe-script` prints a script for the nf-cross image that compiles each standalone
source for `x86_64-w64-mingw32` with `-fsyntax-only -Wshorten-64-to-32 -Wpointer-to-int-cast -Wint-to-pointer-cast`
(the mingw-w64 headers are the same for both architectures). The i686 control compiles all 276 with no error.
`--clang-log DIR` summarises the logs (distinct lines) and compares clang's failed layout checks with the tool's:

| 64-bit syntax check | Distinct lines |
|---|---:|
| failed `static_assert` | 1,032 |
| `naked` not supported on x86-64 | 104 |
| `long long`/`size_t`/`uintptr_t` narrowed implicitly | 66 |
| int to pointer (`-Wint-to-pointer-cast`, `-Wint-to-void-pointer-cast`) | 72 |
| pointer to int (errors) | 28 |
| calling-convention specialisations that collide (xbeAbi.h, xbeOverload.h, the VIRTUAL calls) | 19 |

Of clang's 1,032 failed checks the tool predicts 1,014; it calls 9 stable (sizes compared with a host type,
`sizeof(HostLock) == sizeof(CRITICAL_SECTION)`, and padding it does not compute) and 9 unknown, and flags 3 that
hold. Clang cannot see explicit casts (`uint32_t(uintptr_t(p))` compiles silently and truncates), so both are needed.

## 2. Layouts

### 2.1 In memory only (916 structures)

Objects the game allocates or holds statically and never reads from a file. On 64-bit they grow: a vtable pointer
moves everything after it by 4, pointers realign to 8. Nothing but the checks depends on their size, with three
exceptions the tool lists: 26 objects allocated by a literal size (`UMemory::FastAlloc(0x40, "WWorldPos")`, the AI
characters' 0x150-0x170), 6 arrays of pointers sized `n * 4` or `<< 2` (EAGL's symbol pool, the dynamic model's
GeoPrim and DrawArray lists, the property values), and the fixed pools: the game's heaps are fixed blocks
(`kAudioHeapSize` 0x40000, the memory classes, the scratch pad), so larger objects can exhaust one that never filled
on the Xbox.

The failing checks cluster where the structures are: the events (EventsA-D, 220), the vehicle sounds (69), the
generated overlay checks (51), the C++ library's streams (22), the mission rules, the attribute containers, EAGLAnim.

### 2.2 Read in place from the game's files

Each loader below reads a file into a buffer and uses it where it lies, writing 32-bit addresses into it.

| Loader | What it writes into the data | Produces |
|---|---|---|
| **EAGL's dynamic loader** (eagl/Loader.cpp) - every model, skeleton, animation bank and render-method library: MIPS ELF objects | section offsets, links and infos made addresses in `Elf32_Shdr` (`sections[i].offset = FileAddress(...)`); 37,605 `R_MIPS_32` relocations (`*P = A + S`) whose targets are other sections, other loaded objects, the game's resolver (`RCARPFile::RegisterSymbols`: `&VehicleBuffers.switches[n]`, the reflection's textures - our statics and heap objects), the global pool (registered shapes, our variables) | `Model` (12 pointers), `RenderMethod` (10), `TAR` (5), `GeoPrim`/`GeoPrimParam`, `DrawArray`, `LoadedVertexBuffer` (a relocated `D3DResource` header), the EAGLAnim data (`DeltaFData`, `DeltaQHeader`...), `Skeleton`, `AnimBank`; and untyped word streams mixing pointers and numbers: a model's draw list `{0xffff, GeoPrim*}`, TAR lists `{name*, n, TAR* x n}`, morph data, and its patch list `{address, value}` applied before drawing |
| **CARP** (engine/UGroup.cpp, data/UData.h, Carp.cpp, RCARPFile.cpp) - each track's and object's data groups | a record's data word, an offset from the record, made an address (`ResolveOffsets`: `item->data += (uint32_t)(uintptr_t)item`); `TagReference::value`, a tag, made an address; 'rs' names swapped for what the symbol table answers (`AdoptData`); the resolvers' and events' references | `UData`/`UGroup` (16-byte records, words already `uint32_t`), the payload records with pointer slots: `AIElement` (`Human *`, `AICharacter *`, `PhysicsObject *`... filled at run time), `AnimInfo`/`AnimKey`, `WorldArticle`, `ProcAnimState`, `WMapNode`, `WGridNode`, `MissionRuleRecord`, and the 80 event `Record`s with pointers (of 169) that each event's ResolveEvent callback resolves inside the file |
| **realgraph** (eagl/Realgraph.cpp, Tar.cpp) - SHPX, FNTX, LOCH | offsets stay offsets; a font's header holds the game's `TAR *` and shape pointer at +0x70/+0x74; TARs built over a file's image in place | `FNTXFont`, `TARAtlasEntry`, `TARSharedData` |
| **EA's sound banks** (sound/snd/Banks.cpp) | timbre tags hold addresses as `int32_t` (`int32_t(uintptr_t(&DefaultEnvelope))`); pointer arithmetic done in 32 bits as the original's ADD (`AddBytes`) | the timbre fields |
| **D3D8 resources and the push buffer** (gfx/d3dSeam.cpp, common/gfx/d3d9Backend.cpp) | the Xbox resource format: a resource's `data` word is its pixels' or vertices' address (`texture->data = (uint32_t)(uintptr_t)pixels`); the backend reads textures and streams back from words, and the D3D state tables through `uint32_t` base addresses | the relocated headers in ELF data, the seam's own resources |

The file structures' checks (31 failing) are the ones that must keep holding on 64-bit: their layout is the file's.

**What the data points at** decides the design (7.1): other parts of the same file, other loaded files, heap
objects (the game's heaps, EAGL's allocator, the seam's D3D objects), our statics (registered variables such as
`VehicleBuffers`, and the vtables of objects the loaders' constructors build), and functions (resolved symbols, the
EAGLAnim channel functions written into its signature tables). Nothing found points at a thread's stack from file data; the `Ptr32` encoder's range check
(7.1) is what will prove it.

### 2.3 Shared with other formats

The driving engine writes no save. The launch page it shares with the action engine (`LaunchPage`, 0xa4c) holds no
pointers. No other format crosses the process boundary.

### 2.4 The harness and devtools

The shadow tests run originals and exist only in the injected (32-bit) build: they need no port. The test
executable's three harness files (Teleport.cpp, SndLockstep.cpp, SndCallTrace.cpp) have one pointer-bearing
structure and nothing else the tool flags outside `#ifndef NIGHTFIRE_STANDALONE`; SndCallTrace's `call rel32`
retargeting was the same encoding on x86-64 (within one image), and since macos.md's package M3 the standalone
build names the wrappers at compile time instead. The sound trace prints no heap addresses (it already
matched across the injected and standalone builds, whose heaps differ).

### 2.5 LP64 (Linux and macOS)

`long` is 4 bytes on Windows and 8 on 64-bit Linux and macOS. 8 structure members use it (PlayMPC's `Movie`,
`HostTimeZone`, the C++ library's `Word`, `FloatIn`), and 285 lines in src/driving (mostly the host layer's Win32
signatures, `unsigned long` for DWORD). The Win32-types header of step 2 should define DWORD and friends as
`uint32_t`, not `unsigned long`.

## 3. Integer/pointer conversions

| Idiom | Count | Where |
|---|---:|---|
| vtable addresses kept as `uint32_t` constants (`kSymbolicResolverVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_...))`) | 63 | Carp, RCARPFile, SymbolTable, AnimationDatabase, Sound, VehicleSoundKinds, WeaponSound, AIEnemyRiders |
| file words: CARP's data words and references, the ELF loader, the sound banks | ~25 | section 2.2 |
| D3D seam and backend: resource `data` words, the state tables' bases, push-buffer words | 15 + 23 warnings | d3dSeam.cpp, d3d9Backend.cpp (shared with the action engine, which stays 32-bit) |
| pointers passed as `int` through game APIs: FILESYS callbacks' user data, the sound library's stream and filter handles, `ActActorDatabase::GetNewActorHandle`'s id, `RSceneObj::TriggerFX`'s point and direction | ~35 | FileSys.cpp (17 clang diagnostics), Streams/Stream.cpp, AIElements/AIHands/AIPedestrian/AICharacter (12), VehicleRender |
| addresses used as keys: SkyWater's instance key, the particle cache's `AddOrRefresh(uint32_t(uintptr_t(emitter)))`, Render.cpp's `int(uintptr_t(instance))` | 4 | truncation could make two keys equal |
| function pointers as `uint32_t`: CODA's allocators (PlayMPC), `SetFxFunc`, the sound options' vector table, EAGLAnim's channel functions | 6 | |
| a pointer's bits as a float (EAGLAnim, `std::bit_cast<float>(uint32_t(p))`: the original compares the time with a frame's address) | 1 | AnimMisc.cpp:339: a quirk that depends on the address's value; with the window at 0 the low 32 bits are the address, as on 32-bit |
| an address masked with a 32-bit constant (`& 0xffffff80`) | 1 | sound/snd/Stream.cpp:552 |
| 66 implicit narrowings (`size_t` from `strlen` into `uint32_t`, pointer differences into `int`) | 66 | mostly harmless; each wants a look |

The heap-shift tests of the standalone work (standalone.md: 1 MB and 50,000 bytes, every frame and sound line
exact) show nothing depends on where the heap is - but they kept addresses below 4 GB, so they say nothing about
truncation.

## 4. The x87 code

### 4.1 What there is

43 naked functions hold x87 arithmetic (the other 65 naked functions are register adaptors and are not x87):

| Class | Functions | Why assembly |
|---|---|---|
| Transcendentals whose 64-bit result is used | `sin_`/`cos_`/`sincos_`/`tan_fractionalangle`, `atan_turns`, `SinTimes`, `X87Sin`/`X87Cos` (x2), `X87Atan2` (x3), `SinCosFloat`, `Tangent`, `AddSine`, `GlowWave`, `NegativeSine`, `LogX87` (x2), `VehicleLogX87`, `EAGL_BuildRotate`, `PerspectivePlanes`, `EulerToQuatX87`, `Crt::CIacos`, `CIasin`, `CIpow` (29) | FSIN, FCOS, FSINCOS, FPTAN, FPATAN, FYL2X, F2XM1 return a 64-bit mantissa whatever the precision control, and the original uses it before rounding |
| An operation kept on the x87 stack around such a call | `CrtAcos` (x2), `CrtPow` (x2), `CrtAcosTimes`, `CrtAsinTimes`, `SinTurnsTimes`, `CosTurnsTimes`, `TanTurnsTimes`, `DivideByTanTurns`, `ScaledSine`, `ScaledCosine` (12) | the multiply or divide takes the unrounded result |
| Exact in C | `REAL_sqrtf` (FSQRT is correctly rounded), `X87NaN` (which NaN of two), `Crt::_fpmath`, `Crt::_clearfp` (control and status words) | none on a host without an x87 |

Instruction use (functions): FSIN 11, FPATAN 7, FCOS 6, FYL2X 4, FPTAN 3, FSCALE 2, F2XM1/FRNDINT/FSINCOS 1 each.

**Where the results go.** Not only rendering: `sin_fractionalangle` (19 call sites) and `cos_fractionalangle` (16)
are used by BondCarPhysics and BondCarModes, the collision queries, the actors and posers, Debris and the cameras;
`atan_turns` by the AI (AICharacter, AIEnemy*, AIPedestrian) and PhysicsUtil; `CrtAcosTimes` by the AI ground
vehicles, MissionRules, Sentry and WeaponManager; `NegativeSine` by the helicopter. Any change in a last bit reaches
the simulation, and the lockstep runs diverge from there - not a 1-ulp cosmetic difference: the user's 1-ulp
tolerance (memory: one-ulp-acceptable) covers rare cosmetic cases, not a whole run against its baselines.
Package 0 measured the calls and listed the callers (4.3).

The x87 transcendentals already differ between Intel and AMD in the last bits (why baselines are per machine:
lockstep-machine-baselines), so no software version can match "the x87": it can match one vendor's at most.

### 4.2 Options

| Option | Exact against the 32-bit build | Cost | Carries to Apple Silicon |
|---|---|---|---|
| **A. The same x87 instructions in x86-64 assembly** (recommended for step 3): each function's body unchanged, its arguments taken from XMM registers through memory and its result returned in XMM0 | yes, on the same CPU (x87 is fully available in 64-bit mode) | ~45 small functions, ~400 lines: one GAS (`.intel_syntax`) file for clang/GCC and one MASM file for MSVC x64 (no inline assembly there), or clang only | no |
| B. A software x87 for those instructions (extended-precision argument reduction, 64-bit-mantissa results, rounding as the hardware rounds) | no: matches neither vendor exactly | large: 1,500-2,500 lines and exhaustive tests against both vendors | yes |
| C. Portable C (`sin`, `atan2`, `log` in double) | no | small | yes |

Step 4 needs B or C anyway; A keeps step 3 testable against the existing baselines.

**Two traps for A.** (1) A 32-bit caller may store the x87 result straight to a float (`fstp dword`), rounding 64
bits to 24 once, where a stub that returns a double rounds twice (64 to 53 to 24); the two differ in rare cases.
Every caller of the 43 must be checked in the 32-bit listing (`cl /FA`), and where the result goes straight to a float
the stub returns the float. (2) The x87 control word is per thread and set to 53-bit precision only by
`Crt::_fpmath` on the game's start-up thread. Windows starts every thread at 0x027f (53 bits); Linux at 0x037f (64
bits), so on Linux - 32-bit (step 2) as well as 64-bit - every thread that runs x87 code (the game thread, the sound
thread) must set it.

**The rest of the floating point** is already SSE2: MSVC x86 builds with `/arch:SSE2` by default, so the `double`
chains the style guide prescribes compile to the same SSE2 scalar operations on x86-64. Keep FMA out (no
`-mfma`/`-march=native`, MSVC without `/fp:contract`); the 197 `#pragma fp_contract(off)` stay. Conversions
through `X87.h` (`Truncate`, `RoundToInt`, `Ftol`) use SSE intrinsics that exist on x86-64. Out-of-range
float-to-unsigned and float-to-64-bit conversions use helpers on x86 and single instructions on x86-64: a check
item for package 7.

### 4.3 Package 0: calls, callers and what C changes

**Calls.** `NIGHTFIRE_X87COUNT=1` (src/driving/devtools/X87Count.cpp) moves every call to the 43 functions (found
in the link map) to a counting stub that keeps the registers, XMM0-7 and the x87 environment; it counts per function
and per call site and keeps a few arguments. Its 16 lockstep runs matched `lbase` exactly. From the car's appearance
to tick 8,900:

| | Per tick |
|---|---:|
| all 43, by mission (idle / accelerating) | m1 1,259 / 1,259, m2 1,101 / 1,823, m3 881 / 930, m4 902 / 902, m5 1,267 / 1,425, m6 940 / 1,078, m7 1,381 / 1,471, m8 1,009 / 1,003 |
| `sin_fractionalangle`, `cos_fractionalangle` | 565 each on average (436-903): 99% of all calls |
| `atan_turns` | 9 (1-24) |
| `Tangent`, `SinCosFloat`, `PerspectivePlanes`, `X87Sin`, `X87Cos` | 3 each |
| `X87Atan2`, `Crt::CIacos`, `CrtAcos` | 1-2 |
| `tan_fractionalangle`, `CrtAcosTimes`, `AddSine`, `CrtAsinTimes`/`CIasin`, `TanTurnsTimes`, `DivideByTanTurns`, `sincos_fractionalangle`, `LogX87`, `CrtPow`/`CIpow` | under 1 |
| `_fpmath`, `_clearfp` | once each, at start-up and at a movie |
| `SinTurnsTimes`, `CosTurnsTimes`, `ScaledSine`, `SinTimes`, `NegativeSine`, `EulerToQuatX87`, `X87NaN` | never in these runs |
| `EAGL_BuildRotate`, `REAL_sqrtf` | never: nothing in the standalone build calls them (the linker drops them) |

The sine and cosine traffic is drawing and animation: about 420 calls a tick each from `ReflMapVertex` (the
reflection mapping), then `BuildRotate` (actors, poses, the riders' aim: 120-480), the `VU0_MATRIX4set*rot`
matrices and `RDebris::Draw`. The simulation's own calls (`PBondCar` physics, the AI's `atan_turns`, `CrtAcosTimes`)
are tens a tick. Folded by the linker into one function each: the three `X87Atan2`, the two `X87Sin`, `X87Cos`,
`CrtAcos`, `CrtPow`, the three logarithms, `AddSine` with `GlowWave`, `CosTurnsTimes` with `ScaledCosine`.

**Where C must round once.** The x87 rounds to 53 bits only in the operations the precision control governs (FADD,
FMUL, FDIV, FSQRT...); a function whose last operation is FSIN, FCOS, FPTAN or FPATAN, or FYL2X, hands back a
64-bit mantissa, and a caller that stores it with `fstp dword` rounds it to a float once. A C version that computes in
`double` and then converts rounds twice. From a disassembly of `driving_standalone.exe` (every call, including the
calls through function pointers and through the C wrappers that only pass ST0 on), the stores of an unrounded result
straight to a float are:

| Function (last operation) | Callers that store it to a float |
|---|---|
| `sin_fractionalangle` (FSIN) | 21: `BuildRotate`, `RCameraMath::BuildRotationMat4`, `RDebris::Draw`, `RDebrisType::Draw`, `REmp::Init`, `PBondCar::ProcessSimplePhysics`, `PBondCar::ProcessSubmarinePhysics`, `ActPoser::Rotate`, `RotateX`, `ActActor::RotateActor`, `RotateActorX`, `RPlayerCamera::SetCinematicCamera` (2), `UpdateEllipseCam`, `WCollisionMgr::SurfaceBumpHeight`, `RSkeletalObj::UpdatePosition`, `VU0_EulerToQuat` (2), `VU0_MATRIX4setxrot`, `setyrot`, `setzrot` |
| `cos_fractionalangle` (FCOS) | 21: the same less the physics, `SurfaceBumpHeight`, `UpdateEllipseCam` and one `SetCinematicCamera`, plus `RRenderWorldCulling::Construct` and `Setup2dFrustrum`, `REmp::Init` (3), `VU0_EulerToQuat` (3) |
| `X87Cos` (FCOS) | `QuatFromTo`, `ActIK::Solve` |
| `X87Atan2` (FPATAN), through `AngleBetweenVectors` | `ActIK::Solve` |
| `NegativeSine` (FSIN, FCHS) | `PHelicopter::Simulate` (2) |
| `Tangent` (FPTAN) | `WTargetPicker`: `CompareDistFromScreenPos`, `DrawTargetingSystem`, `MoveTargetingCursors`, `UpdateSelection`, `UpdateTargets` |
| inside the assembly | `sincos_fractionalangle` (2 stores), `SinCosFloat` (2), `EulerToQuatX87` (each sine and cosine, and products of them), `PerspectivePlanes` (each FPTAN, FSIN and FPATAN result) |

A second kind cannot be matched in `double` at all: the caller keeps the unrounded result on the x87 stack for one
more operation, rounded once at the end. `sin_fractionalangle` (7: `ScaledSine`, `ProcAnimSway`, `ReflMapVertex`,
`REmp::Init` (3), `RSky::Draw`), `cos_fractionalangle` (8: `ScaledCosine`, `SurfaceBumpHeight`, `CylinderSection`,
`GHud::DrawStylin`, `ProcAnimSway`, `ReflMapVertex`, `REmp::Init`, `RSky::Draw`), `CrtAcos` (`ECollision::ProcessSideA`),
`LogX87` (`AVehicle::CalculateRpm`), and inside the assembly `SinTimes`, `SinTurnsTimes`, `TanTurnsTimes`,
`DivideByTanTurns`, `CrtAcosTimes`, `CrtAsinTimes`, `ScaledSine`/`ScaledCosine`, `AddSine`/`GlowWave`, `EulerToQuatX87`.

Results the precision control has already rounded to 53 bits (`atan_turns`, `AddSine`, `CrtAcosTimes`, the `*Times`
functions, `DivideByTanTurns`, `CrtPow`) round twice on the x87 too, so their float stores need nothing special.

**Which functions C reproduces exactly** (with SSE2 `double`, no FMA):

| Bit-identical in C | Functions |
|---|---|
| yes | `REAL_sqrtf` (FSQRT and SSE `sqrtsd` are both correctly rounded; dead in the standalone build anyway), `X87NaN` (one FADD at 53 bits; C must pick the NaN as the x87 does - the larger significand), `Crt::_fpmath` and `Crt::_clearfp` (control and status words, no arithmetic) |
| no | the other 39: each depends on FSIN, FCOS, FSINCOS, FPTAN, FPATAN, FYL2X or F2XM1 (`CIpow`), whose results C's `sin`, `cos`, `tan`, `atan2`, `log` and `pow` do not reproduce (the x87 reduces arguments with a 66-bit pi and does not round correctly; it also differs between Intel and AMD) |

So the conversion changes the results of every caller above; how much shows in the lockstep runs once, before the
new baselines are made.

### 4.4 Done: the x87 code as C (15 October 2026)

**The option.** `NIGHTFIRE_X87` (CMake, ON by default; [portable.md](portable.md)) applies to drivinginject and both
standalone targets. ON compiles exactly what was there before. MSVC's per-function assembly of every changed object
file, in drivinginject and in driving_standalone_test, matches the build before the change instruction for
instruction. OFF gives each of the 43 functions a C form beside its assembly, under `#if NIGHTFIRE_X87`:

- **The trig in turns** (RealMath.cpp): `sin(double(kTwoPi) * turns)` and the like. The float functions round once.
  `SineTurns`, `CosineTurns`, `TangentTurns` and `AtanTurns` give the double for callers that keep computing, as
  section 4.3 lists. Thirteen local `reinterpret_cast`s of the float functions to double-returning pointers are now
  calls to these four.
- **The runtime's `_CIacos`, `_CIasin` and `_CIpow`** become `double CIacos(double)` and so on. They keep the
  original's domain errors, poles and overflow, but underflow is `pow`'s. `X87NaN` picks the NaN as the x87 does.
  `_fpmath` and `_clearfp` work through `<fenv.h>`.
- **The local helpers** (`X87Sin`, `CrtPow`, `LogX87`, `ScaledSine`, `NegativeSine`, `Tangent`...) keep their names
  and return types. A product the original makes on the x87 stack, such as `SinTurnsTimes`, is the same product in
  double.
- **`EAGL_BuildRotate`, `PerspectivePlanes` and `EulerToQuatX87`** are written out in C++ from their instructions. A
  value the original stores as a float is rounded there, and every other value stays double.

The OFF executable has no FSIN, FCOS, FSINCOS, FPTAN, FPATAN, FYL2X, F2XM1, FSCALE, FRNDINT or FLDCW left. What
remains is MSVC's own x87 use (returns in ST0, some multiplies there). The mingw cross build (nf-cross `linux` preset)
compiles drivinginject and both standalone targets with OFF.

**Two address orders made heap-independent (OFF only).** The particle systems' map key (7.4) becomes a count of the
systems made. Heap padding then showed one more dependency: `RVehicleParticle::UpdateAll` steps the cars' systems in
the order of a set of object pointers, which is address order again. With OFF it uses the order `UpdateCar` added
them. Both are in original-quirks.md 1.3. With them, OFF is exact under `NIGHTFIRE_HEAP_PAD` 16 and 52.

**Distance from `lbase`** (driving_standalone_test with OFF, runner, to tick 9000):

| Mission | Sound traces | Frames (pixels differing over 16 frames, idle / accelerating) |
|---|---|---|
| 1 | identical | 42,583 / 42,583 (a light and a small effect, about 4,000 px a frame from tick 3000) |
| 2 | identical | 0 / 3,514,113 (underwater bubbles and smoke behind the Mission Failed box: about 278,000 px a frame from tick 2000) |
| 3, 4 | identical | 0 / 0 |
| 5 | identical | 332,205 / 581,216 (particles in some frames; others exact) |
| 6 | identical | 0 / 760,444 |
| 7 | identical | 406,559 / 689,331 (fire and lava particles) |
| 8 | identical | 0 / 43 |

All 16 sound traces are identical to tick 9000, so nothing the simulation does changed. The C results differ from
FSIN's and FCOS's only in the last bits, and most results are stored as floats. The frames that differ do so in
particles: the new order and the C trig, through the particles' spawn angles.

**New baselines `lbase_c_*`** (build/baselines, 16 runs, the runner): from the final OFF build (round pmc1). Results:

- A second run in the other checkout and slot half matched them exactly in all 16 runs.
- `NIGHTFIRE_HEAP_PAD=16` and `=52` (rounds pmq16, pmq52) matched them exactly in all 16 runs each.
- With ON, `BASE=lbase round.sh all` (injected: 16 lockstep runs exact, shadows on all eight missions with 0
  differences) and the standalone test executable's 16 lockstep runs were exact against `lbase`. A final quick ON
  round of the standalone build after the last change was exact too.

From here on, the portable and 64-bit builds compare with `lbase_c` (BASE=lbase_c with
`EXE='Release\driving_standalone_test.exe'` and `RUNNER_CMAKE_ARGS=-DNIGHTFIRE_X87=OFF`). Package 7 (x87 on
x86-64) is no longer needed. Package 9 compares the 64-bit build with `lbase_c`.

## 5. Calling conventions and the naked adaptors

On x86-64 there is one calling convention: MSVC and clang accept `__stdcall`, `__fastcall`, `__thiscall` and ignore
them. The game's uses are consistent on both sides of every call (the seams' Win32-style signatures, the register
ports' `__fastcall (this, int, ...)` with a dummy EDX, vtable slots called through typed pointers), so nothing changes
meaning. Two things fail to compile:

- **The injection's ABI templates** (src/common/xbeAbi.h, xbeOverload.h): specialisations by calling convention
  collide when the conventions are the same type (19 errors, including the 7 generated `VIRTUAL(n)` calls in
  autogenerated_functions.inc that instantiate `XbeAbi`). Guard the convention specialisations with
  `#if defined(_M_IX86) || defined(__i386__)`; `XbeVirtual` itself (a vtable slot read into a member-function
  pointer) already allows for both pointer sizes.
- **The naked adaptors**: 59 register-argument adaptors (`// AUTOLTCG`, the `*Regs`, `*Eax` entries) and 2 in
  XboxTimer exist for the originals that call into our ports; our code never names one (the tool's count is 0 for
  each). The standalone build compiles them but nothing reaches them. Put them under `#ifndef NIGHTFIRE_STANDALONE`
  as the seams' patching already is.

## 6. Third-party pieces

| Piece | 64-bit |
|---|---|
| FFmpeg (third_party/ffmpeg-prebuilt) | the committed DLLs are i686 (`--arch=x86`); tools/fmv/build_ffmpeg.sh builds x86-64 ones with `x86_64-w64-mingw32-` and `--arch=x86_64` (and Linux/macOS shared libraries for later) |
| SDL3 | built from source at release-3.4.18: any architecture |
| Direct3D 9, XAudio2, XInput (Windows) | native x64 DLLs |
| dxvk-native (Linux) | its main target is x86-64 |
| xadpcm, the shader translators, FXAA (src/common) | ours; the backend's `uint32_t` addresses are section 3's |
| D3DCompile | devtools only |

## 7. Design

### 7.1 The game window and `Ptr32<T>`

Every pointer the game's files hold stays a 32-bit word in place. A word means an offset from one base, the
**game window**: a 4 GB range that holds everything such a word may point at - the game's heaps and contiguous
memory, EAGL's allocator, the seams' D3D and DirectSound objects, the game's thread stacks, and the statics the data
names.

- `Ptr32<T>` (one small header): stores `uint32_t`; converts to and from `T *` (`base + word`), with `->`, `*`,
  `[]`, comparison and arithmetic; encoding checks the range in the test build and stops with the address if it is
  outside the window. `PtrFrom32<T>(word)` / `PtrTo32(p)` for the untyped word streams (draw lists, TAR lists,
  patch lists, push-buffer and resource words, `TagReference::value`, `UData::data`).
- **On 32-bit, `Ptr32<T>` is `T *`** (`template <class T> using Ptr32 = T *;`, the decode functions casts): the
  oracle build's code does not change, which the per-function assembly diff proves package by package.
- **The window's base** is 0 where the low 4 GB is available: Windows (either `/LARGEADDRESSAWARE:NO`, which keeps the
  whole process below 2 GB, or the host layer placing its memory low - `HostAllocatePages` already allocates from the
  lowest free address) and Linux (a non-PIE executable at 0x400000, `mmap` with low hints for the heaps, game threads'
  stacks given through `pthread_attr_setstack`). Then decoding is a zero-extension. macOS (step 4) maps nothing below
  4 GB (`__PAGEZERO`), so there the base is the start of a reservation made next to the executable, and statics the data
  names must lie within 4 GB of it: a run-time base, one addition per decode.
- **The host layer owns the window**: `HostHeapCreate`/`HostHeapAlloc`, `HostAllocatePages` and thread creation
  allocate inside it, and the standalone executable's global `operator new` (the seams' objects) uses a window heap.

Alternative considered: **re-laying the data out at load** into native structures. The ELF relocations do not know
the types they patch (37,605 words in data whose layout is known only to its consumers), CARP's resolvers walk
records by stride, and the word streams have no types at all: every format would need a converter and a writer of
every structure the loaders and their constructors produce, and the converted data would no longer be the file's -
far more work and far more risk than decoding words.

### 7.2 Memory-only structures

Native pointers: the 916 structures grow, which is what natural C++ does on 64-bit. Their 32-bit layout stays
checked where it matters - the 32-bit build, which is the oracle - by giving the checks a form that applies only
there (`static_assert(NF_LAYOUT32(sizeof(Model) == 0xd8), ...)`, or a `LAYOUT_SIZE`/`LAYOUT_FIELD` pair beside
`XBE_CLASS_SIZE`/`XBE_FIELD`, generated ones included). File structures keep unconditional checks.

The cost of native: allocation sizes differ from the original's (style guide 1: "same allocations"), so behaviour
that depends on sizes - an uninitialised read whose stale contents depend on which block was reused, a fixed pool
that now fills - can differ. Package 0 tests this on the 32-bit build before anything is ported: a
`NIGHTFIRE_HEAP_PAD=<bytes>` mode, beside `NIGHTFIRE_HEAP_SHIFT`, that grows every game allocation, run in lockstep
against the baselines. Exact means native growth is safe for behaviour; then only pool sizes need watching (a
high-water report per pool).

**Result (15 October 2026, 7.4):** the simulation and the sound do not depend on allocation sizes, but one thing
drawn does: the particle systems' map is keyed by emitter address, so its order - and the order particles are
spawned and drawn in - follows the heap's layout. Native pointers stay the plan; that key needs a stable value first.

The other way, `Ptr32` in every game structure, keeps every layout, check and allocation identical to 32-bit and
needs no pool work, at the price of about 1,700 members retyped and code that reads less like the original's. It is
the fallback if the pad test fails and the cause cannot be fixed.

### 7.3 Conversions

The 63 vtable constants become `const void *` (or `Vtable *`); APIs that pass pointers as `int` take `intptr_t` or
the pointer type (FILESYS user data, the sound library's handles, actor ids, TriggerFX); address keys become
`uintptr_t`; function-pointer words in data (CODA, the sound vectors) are `Ptr32` or typed; the mask uses
`~uintptr_t(0x7f)`; literal allocation sizes become `sizeof`. Where the original's 32-bit arithmetic is the point
(the banks' `AddBytes`, CARP's relative offsets), it stays 32-bit on the word, then decodes.

### 7.4 Package 0: heap padding

`NIGHTFIRE_HEAP_PAD=<bytes>` (src/driving/devtools/HeapPad.cpp, drivinginject and driving_standalone_test) asks every
allocator the game uses for that many bytes more: the process heap (`HostHeapAlloc`/`HostHeapReAlloc`: XAPI's
RtlAllocateHeap, the C runtime's malloc/calloc/realloc, EAGL's allocator), EA's block allocator (`MEM_alloc`,
`MEM_allocz`, `MEM_allocalign`, `MEM_resize`: every memory class, and through it UMemory's operator new, new[] and the
fast pools' blocks), UMemory's size-class lists (`FastAlloc`/`FastFree`, both ends), the event queue (`Event::operator
new`/`delete` and `Event::Delete`, which inlines the base class's delete) and the sound heap (`SNDMEMI_alloc`). Contiguous
memory (textures, vertex buffers, the classes' own memory) is not padded. Every call and stored address of these
(rel32 calls and the base relocations, devtools/CodeRetarget.cpp) is moved to a wrapper: 1,688 references.

Lockstep rounds on the runner against `lbase` (every mission, idle and accelerating, to tick 9000):

| Run | Sound traces | Frames |
|---|---|---|
| pad 0 (the same build, control, mission 5 idle) | identical | exact |
| `NIGHTFIRE_HEAP_PAD=16` | 16 of 16 identical | 14 of 16 exact; mission 5 idle and accelerating differ in 5 of 16 frames each |
| `NIGHTFIRE_HEAP_PAD=52` | 16 of 16 identical | 8 of 16 exact; missions 1, 2 accelerating, 5, 6 accelerating, 7, 8 accelerating differ |

No pool ran out (no padded allocation answered NULL; the event queue never passed 4 KB of its 32 KB). Where the
differing frames were looked at (missions 2, 5 and 6) they differ only in particles - mist and snow spray, bubbles,
smoke - and on mission 5 later frames are exact again; with the sound traces identical, nothing the simulation does
changed. The cause is `RParticleSystemManager`'s map of live
systems (render/Particles.cpp), keyed by `uint32_t(uintptr_t(emitter))` (game/EffectsGallery.cpp `AddOrRefresh`):
`UpdateSpawnAllSystems` walks it in key order, so the order systems spawn into the shared particle cache, and so the
order translucent particles are drawn in, is the emitters' address order. `NIGHTFIRE_PARTICLE_ORDER=1` (HeapPad.cpp)
prints, per tick, a hash of the map's systems in order and a hash of them as a set: on mission 5 with pad 16 the sets
match the control run's on all 8,217 ticks and the order differs on 4,831. HEAP_SHIFT did not show it because a uniform
shift keeps the addresses' order.

A 64-bit build with native pointers lays the heap out differently, so its particles would differ from the 32-bit
build's the same way. Before package 9's exact comparison, the key wants a value that does not depend on the heap - a
number given to each emitter when it is made, say - in both builds; that changes the 32-bit frames once, so it belongs
with the x87 conversion's new baselines. Done with `NIGHTFIRE_X87=OFF`, together with the cars' particle set (4.4). The other address keys of section 3 (the broken windows' instance key,
WRender's instance pointers into one array) are lookups or keep their order.

### 7.5 Done: packages 1 and 2 (15 October 2026)

**The x86-64 configuration.** The CMake preset `windows-x64` (MSVC, x64, `build/windows-x64`; its build preset builds
`driving_standalone` and `driving_standalone_test` only) forces `NIGHTFIRE_X87` off and marks the loaders and the
injection DLLs `EXCLUDE_FROM_ALL`. The standalone executables link at `/BASE:0x400000 /DYNAMICBASE:NO
/HIGHENTROPYVA:NO` (GNU ld: `--image-base`, `--disable-dynamicbase`, `--disable-high-entropy-va`), so their statics
and vtables lie in the window; MSVC warns (LNK4281) about the low base. The clang x86-64 mingw build is the probe
(`--probe-script`): it now passes `-DNIGHTFIRE_X87=0` and uses the x86-64 mingw-w64 sysroot when it is installed
(`g++-mingw-w64-x86-64`), and `--probe-count DIR` prints one line. CI's `probe64` job runs it and writes the count to
the job summary without failing.

**Guards.** The 59 register adaptors and XboxTimer's two RDTSC stubs are under `#ifndef NIGHTFIRE_STANDALONE`. The
x87 call counter (devtools/X87Count.cpp, in driving_standalone_test) is x86-32 only, with empty hooks otherwise. In
xbeAbi.h the `XbeAbi` specialisations and `XBE_ABI_CHECK` are x86-32 only (on x86-64 the check is empty), and in
xbeOverload.h so are the `__stdcall` and `__fastcall` overloads of `Of`. Host.h's `HostUlongPtr` is ULONG_PTR/DWORD_PTR
(unsigned long except on Win64) for the timer callback and `HostRaiseException`. On Win64 a `HostLock` holds a pointer
to a critical section of its own, since 40 bytes do not fit the 24 the game's records keep for it. The crash
reporter's registers are `uintptr_t`, read from Win64's context (still printed as their low halves).

**FFmpeg.** `tools/fmv/build_ffmpeg.sh windows-x86_64` builds the same release and configuration with
`--arch=x86_64 --cross-prefix=x86_64-w64-mingw32-` into `third_party/ffmpeg-prebuilt/bin-x64` (2.4 MB, committed as
the i686 DLLs are; the headers are the same, `ffversion.h`'s version string aside). The 64-bit standalone targets copy
them beside themselves.

**`Ptr32<T>`** (platform/Ptr32.h): `T *` on a 32-bit host. On a 64-bit host it is a class holding the `uint32_t`, with
conversions both ways, `->`, the increments and an explicit cast to a derived class's pointer
(`(PVehicle *)element->object`); `*`, `[]`, `+`, `-` and the comparisons work through the conversion. `PtrFrom32` and
`PtrTo32` do the same for untyped words. The base is the constant 0 (`kGameWindowBase`). Encoding an address at or
above 4 GB stops the game (`GameWindowOutOfRange`: the address, a debug break, abort) in every build, not only the test
build. A run-time base for macOS would add one addition per decode and keep null as the word 0.

**The game window** (platform/GameWindow.cpp, 64-bit hosts only). `HostReserveLow` reserves address space at the
lowest free address below 4 GB and `HostCommit` commits it (HostWin32: `VirtualQuery`/`VirtualAlloc`; HostPosix:
`mmap` with `MAP_FIXED_NOREPLACE` at 64 KB steps, then `mprotect`). GameWindow.cpp implements Host.h's heap functions
over one 1 GB arena reserved at or above 0x20000000 (contiguous memory keeps its place below 0x10000000), committed in
1 MB steps. Blocks have a 16-byte header and boundary tags; free blocks sit in lists by size (exact below 1 KB, four per
power of two above), taken first fit, merged on free, with the untouched top used last; one lock. It keeps HeapSize's
answer (the size asked for), HEAP_ZERO_MEMORY, HEAP_REALLOC_IN_PLACE_ONLY and a heap's maximum (live blocks may not
pass it). So the process heap, XAPI's heaps, the C runtime's malloc, the memory classes, UMemory, EAGL's allocator and
the sound heap all allocate in the window. `HostAllocatePages` stays below 4 GB (where asked if there is room, else
anywhere below 4 GB). The standalone executable's global `operator new` and `delete` use a window heap of their own.
A 32-bit host keeps the system's heaps (its code is unchanged). Not placed: the game threads' stacks (nothing found
points at one from file data, and the encoder's check would stop the first store that did) and what Direct3D, XAudio2
and the C library allocate for themselves (package 8 decides which of the seam's D3D objects the data holds).

`gamewindow_test` (devtools/GameWindowTest.cpp, `EXCLUDE_FROM_ALL`, both architectures) checks `Ptr32`'s layout and
operations, then runs a random mix of allocations, frees and reallocations (sizes to 1 MB; contents, sizes, zeroing and
in-place-only checked), fills a heap with a maximum, and checks that pages, `operator new`'s blocks and the statics are
below 4 GB. x64: 1,000,000 operations, 1,000,024 checks, 0 differ; Win32 (the system heap): 300,000 operations, 0
differ. A heap that forgets to unlink a merged neighbour crashes it.

**The 110 file structures** (2.2, the tool's `LOADERS` set): their 202 pointer members are `Ptr32<T>` (`void **p`
becomes `Ptr32<void *> p`). Three uses needed more than the type: `Model::RemoveChild`'s list of member addresses
(`Ptr32<Model> *lists[3]`), `RenderMethod::GetVertexShaderTable` and `GetPixelShaderTable` (unreferenced; they return
the member's address, now `Ptr32<...> *`), and a redundant `reinterpret_cast` in `ESetAvoidZone::Process`. The
loaders are unchanged: they still write their words through 32-bit casts (packages 3a-5 and 8). All 31 layout checks on
file structures now hold on x86-64; the tool classifies `Ptr32` members as 32-bit.

**The 32-bit build is unchanged.** Every object file of drivinginject, driving_standalone and driving_standalone_test,
compared with the build of 7f225f5 section by section (code by bytes and relocations, data variable by variable):
drivinginject 393 of 393 identical (GameWindow.obj is new and empty on 32-bit). The standalone executables lose the
adaptors, the static bodies only they called and AnimDeltaQ's warned-once flag, and five functions differ because MSVC
now inlines a callee whose address an adaptor took (`WCollisionMgr::FindFaceInTriStrip`, `stretch`,
`RHighLevelLightManager::AddDynamicLightEffect`, `RLightManager::Construct`, `STREAM_cancelrequest`); everything else
is identical. (MSVC orders a file's .bss and .data variables differently when an included header changes; the
variables and their contents are the same.) On the runner
(rounds p64a, p64s, p64c, to tick 9000): the injected build against `lbase` (16 lockstep runs exact, shadows on all
eight missions with 0 differences), driving_standalone_test against `lbase` (16 of 16 exact) and, with
`NIGHTFIRE_X87=OFF`, against `lbase_c` (16 of 16 exact). The mingw cross build (`linux` preset) builds every target,
both standalone executables included.

**The x86-64 counts.** The probe's distinct error lines: before, 1,112 (1,032 failed `static_assert`s, 61 `naked`, 19
calling-convention templates); after, 993, every one a failed `static_assert` on a memory-only structure (package 6).
MSVC x64 likewise stops only on static assertions (C2338, then C1003 after 100 in 92 files, so the clang count is the
measure). Warnings left (clang): 66 narrowings, 57 int-to-pointer and 22 pointer-to-int casts, 21 through `void *`,
76 enum conversions. `pointer_layout_audit.py`: 202 `Ptr32` members; file structures with pointer-sized members 0
(110 before).

**What remains, by package.** 3a, 3b: the ELF loader's section and symbol words and its relocations (its
`(uint32_t)(uintptr_t)` casts, MSVC's C4311/C4312 warnings), the word streams of Model.cpp, the render methods and the
EAGLAnim data. 4: CARP's data words and references, the event `Record`s' resolution, the data layer's vtable constants.
5: realgraph, the sound banks' timbre words and `AddBytes`, FILESYS user data, stream and filter handles, actor ids,
address keys. 6: the 993 failing checks (the 32-bit-only form), literal allocation sizes, `* 4` pointer arrays, the
remaining vtable constants, the narrowings. 8: the D3D seam and backend's resource, state-table and push-buffer words.
9: the first boot, and the crash reporter's 64-bit register report. With the failing checks disabled (a forced
include defining `static_assert` away), the clang x86-64 build of both standalone executables already compiles and
links (image base 0x400000, no dynamic base); nothing has run.

### 7.6 Done: package 6, the memory-only structures (15 October 2026)

**The check form.** `LAYOUT_32(condition, message)` in src/common/xbeClass.h is
`static_assert(sizeof(void *) != 4 || (condition), message)`: the condition still compiles on 64-bit, and holds on
32-bit only. `XBE_CLASS_SIZE` and `XBE_FIELD` expand to it, since an overlay class is one of the game's objects in
memory; that covers the generated checks (autogenerated_layout_checks.inc) without touching the generator. The one
overlay class a file embeds, `MATRIX4` (in `AIElement`), stays pinned by `AIElement`'s own checks, which are
unconditional. 848 plain `static_assert`s became `LAYOUT_32` (179 headers and sources gained the include), so
1,045 checks are 32-bit-only. The style guide names the form (section 3).

**What stays unconditional.** The 31 checks on the `LOADERS` structures, and the ones found while doing this, now in
the audit tool's `LOADERS` table: CARP records read straight from a group's data (`WTrigger`, `WCollisionInstance`,
`WMapHeader`, `ArticleEffect`); EAGL data read through file layouts (`PushBufferPacket`, `TARAtlas`, `TARSlot`,
`TARSymbolData`, `DeltaChanData`, `KeyChanData`, `CompoundData`, `AttributeEntry`, `DynamicModel`); the game's own
GeoPrims, which EAGL draws as its own (`GainGeoPrim`, `EmpGeoPrim`, `SimpleGeoPrim`, `TexturedGeoPrim`,
`GlowGeoPrim`); the built-in fonts (`LinkedFontFile`, laid out as font files). Also unconditional:
`sizeof(CRITICAL_SECTION) <= sizeof(HostLock)` (HostWin32.cpp; on Win64, where packages 1-2 made the lock hold a
pointer to its critical section, the check is that the pointer fits), and a new check that the window map's nodes are the size the core's compiled find
sees them as (SkyWater.cpp).

**Conversions.**

- The 71 vtable constants are `void **const kXVtable = Vtable_X;` (the 63 truncating ones, and 8 `uintptr_t` ones in
  render/). `ABaseSound::vtable` and `AEngine::vtable` were `uint32_t` and are `void **`; `TreeThrow` and
  SymbolTable's `SetVtable` take the pointer. The shadow tests that compared or forged those members were adjusted.
- Allocations by literal size use `sizeof`: the 22 game objects (the camera's parts, the pad device and its
  mapping, the AI characters, the car's action queue, the ray shells, the mixer's filters), the stretch filter as
  `sizeof(SFilterStretch) + 1020 * sizeof(float)` (the struct is 0x838 of the original's 0x1828), STREAM's and the
  packet player's layouts (`STREAM_overhead`, `STREAM_create`, `SNDPKTPLAY_overhead`/`create`, the stream
  record's `kStateSize`/`kRequestSize`), the reverb's node sizes table (`PanReverbTables.nodeSizes`) and its
  routing and node arrays, EAGL's property value arrays. STREAM's ring alignment masks with `~uintptr_t(0x7f)`.
  The mixer's two float buffers (0x40bc, 0x840) hold no pointers and keep their sizes.
- Pointers passed as integers: the actor id (`ActActorDatabase::GetNewActorHandle`, `ActActor::Construct`,
  `callbackId`, the transform callback) is `void *`; the culling callbacks' items (`CullNextItem`, `CullSetItem`,
  and the world's, the actors', the scene objects' and GFX's callbacks) are `intptr_t`, the world's draw key
  `uintptr_t`; `THREAD_create`'s unused stack argument is `void *`; `RSceneObj::TriggerFX`'s unread point and
  direction are `const Coord3 *`; the camera modes' method address (`CameraModeInfo::update`) is `uintptr_t`; CODA's
  allocator settings (`RcmpAllocate`, `RcmpFree`) are function pointers.
- The window map's find (`CollisionInstanceMap::Find`) is keyed by `uintptr_t`, and its value laid out as the map's
  real one (the key, then a tree), so the nodes agree on 64-bit too.
- The two `%08x` prints of pointers use `PRIxPTR` (the same string on 32-bit).

**Result.** The per-function assembly of every object file of drivinginject, driving_standalone,
driving_standalone_test and driving.exe, against the objects built before the change (MSVC, 969 objects, 41,133
functions): every function identical except

- functions whose only change is a mangled name, because a parameter's type changed (`TreeThrow`,
  `GetNewActorHandle`, `ActActor::Construct`, `THREAD_create`, `RSceneObj::TriggerFX` and their `XbeAddress`
  instantiations), and the 133 callers whose only change is calling those names;
- `PlayMPC::Construct`, whose stores name the retyped `RcmpAllocate`/`RcmpFree` (same instructions);
- `TreeThrow` (an untested throw path): the same loads, stores and calls with its registers allocated
  differently;
- two shadow-test functions (devtools) that store the retyped callback and id.

Data sections: identical but for relocations naming renamed functions and, in two drivinginject objects, the order
of their sections. The 64-bit syntax check (`--probe-script`, 277 sources, distinct lines), before and after:

| Category | Before | After |
|---|---:|---:|
| failed `static_assert` | 1,032 | 57 (51 on file structures, 4 built-in font, `TARSymbolData`, `HostLock`) |
| `-Wshorten-64-to-32` | 66 | 53 |
| pointer to int (`-Wpointer-to-int-cast`, `-Wvoid-pointer-to-int-cast`) | 28 | 16 |
| int to pointer (`-Wint-to-pointer-cast`, `-Wint-to-void-pointer-cast`) | 72 | 70 |
| calling-convention templates and their cascade | 28 | 19 |
| `naked` (package 1) | 61 | 61 |

**Left for other packages.**

- 3a/3b (EAGL): 29 of the failing checks (models, render methods and packets, TARs, the anim data, `DynamicModel`
  and the game's GeoPrims, whose layout must follow `GeoPrim`'s, `TARSymbolData`); the dynamic model's and the
  symbol pool's `* 4` arrays (Model.cpp, Loader.cpp); about 30 int/pointer casts in Loader.cpp and Model.cpp;
  EAGLAnim's function words and its pointer-as-float (AnimMisc.cpp).
- 4 (CARP and events): 22 of the failing checks (`AIElement`, `AICommand`, `AnimKey`/`AnimInfo`,
  `MissionRuleRecord`, the world's records); `UData`/`UGroup`/`Carp.cpp` words; `RSceneObj`'s article word; the
  events' and tasks' data passed as `uint32_t` (`RegisterEvent` callbacks, `Schedule::AddTask`'s static data,
  `TaskRecord`), which carry record pointers.
- 5 (realgraph, sound, FILESYS): 5 of the failing checks (`FNTXFont`, the built-in fonts); FILESYS's user data (28
  diagnostics in FileSys.cpp); the sound library's stream and filter handles (Stream.cpp, Streams.cpp), the banks'
  words, `SetFxFunc` and the options' vector table.
- 2 (host layer): `new[]`'s emulated cookie in RuntimeAlloc.cpp
  (an `int` count before an array of pointer-bearing records) misaligns them on 64-bit.
- 8 (D3D): d3dSeam.cpp and d3d9Backend.cpp.
- 10 (LP64): the `long` members (PlayMPC's movie offsets, iostream's word, `FloatIn`, `HostTimeZone`) are right on
  Windows x64.
- Harmless and kept: pointer differences and `strlen` results into `int` (most of the 53 narrowings), the
  particle key of the `NIGHTFIRE_X87=ON` build, `DTuningFile::ParseData_Colour`'s use of a pointer's low bits as
  the original does, `NumGet::DoGetPointer`'s `strtoul`.

**With packages 1 and 2** (driving-64, both merged with the Linux port): the probe's distinct error lines are 18, every
one a failed `static_assert` on a structure package 6 added to the `LOADERS` table (`WTrigger`, `WCollisionInstance`,
`WMapHeader`, `ArticleEffect`, `PushBufferPacket`, `DynamicModel`, the anim data, the game's GeoPrims), whose pointer
members become `Ptr32` in packages 3a and 4. MSVC x64 fails the same 18 and, now that it gets past them, the two
`_mm_empty()` calls (GameLoop.cpp, EventsD.cpp), an MMX intrinsic x64 does not have.

### 7.7 Done: packages 3a and 3b, EAGL's loader, models, render methods and anim data (15 October 2026)

**The loader** (Loader.cpp). `FileAddress` encodes with `PtrTo32`. The section table, `.symtab`, `.strtab`, the
relocations and their targets decode with `PtrFrom32`, and so do the symbol values `GetAddr`, `GetSymbol` and
`EAGL_LookupInObject` answer. A resolved external's value is `PtrTo32(address)` less section 1's base, and the
R_MIPS_26 range check uses `PtrTo32(P + 1)`. Every address the resolvers answer (other objects, the game's resolver,
the global pool) is therefore range-checked when it is written. The symbol pool's table and the destructor list are
sized with `sizeof`.

**Word streams** (Model.cpp). The draw list's GeoPrims, the TAR lists' names and TARs, the patch list's addresses,
the morph groups' names and the morph records' targets are decoded with `PtrFrom32`. A morph target's arithmetic
stays in 32 bits on the word, as the original does it. `TARList::tars` is a `Ptr32<TAR> *` into the list. Arrays of
file words are `Ptr32<Ptr32<T>>` (`Model::geometryNames`, `MorphWeights::pointers`, `RenderMethod`'s `vsMicrocode`,
`psDefinitions` and `paramNames`, `AnimBank`'s `anims` and `names`). That changes the static render methods'
description arrays too (DirectorQueue, Decals, Gain, Materials, PostProcessing). The shader tables
(`vertexShaders`, `pixelShaders`) and `DynamicModel`'s GeoPrim and DrawArray lists hold `Ptr32` elements, so their
`* 4` allocations stay right. DrawArray's user variables are sized with `sizeof`.

**Structures laid over file data that still held native pointers**, now `Ptr32`: `PushBufferPacket::pushBuffer`,
`StaticVertexBuffer::buffer` (opcode 9 reads one inside parameter data), `TARExtension::tar` (laid over a TAR's
+0x48), `DeltaChanData::info`, `KeyChanData::info` and `times`, `CompoundData::attributes` and `subs`,
`AttributeEntry::pointer`, and `DynamicModel`'s lists.

**Resource words and the rest.** A loaded vertex buffer's data word (`offset - PtrTo32(base)`), the user vertex
buffer's physical address, `IndexBuffer::Lock` and the render-target SHAPE's data offset now go through
`PtrTo32`/`PtrFrom32`. An opcode-11 packet's all-ones slots are `PtrFrom32<T>(0xffffffff)`. `ModelShapeImage`'s data
offsets are added signed. EAGLAnim's channel-function words and its pointer-as-float go through
`PtrTo32`/`PtrFrom32`, and RUNTIME_ALLOC's TAR count travels in its context as `intptr_t`.

**Result.** Every object file of drivinginject, driving_standalone and driving_standalone_test (394, 281 and 287)
matches the 0cdc670 build exactly: code bytes, relocations and data. The probe's distinct lines on this branch go
from 993 to 985 errors and from 242 to 207 warnings. The 8 errors fixed are file-structure checks. What is left in
eagl/ is 7 `size_t` narrowings (strlen sizes, the anim pool's pointer difference). Every failure left is a check on a
memory-only structure, which package 6's `LAYOUT_32` removes. MSVC x64 (static assertions forced off) compiles every
source but the two `_mm_empty` ones, with no C4311/C4312/C4302 left in eagl/, render/ or camera/. The patch applies
cleanly onto driving-64.

**Left.** From package 6's list of unconditional checks: the game's own GeoPrims (`GainGeoPrim`, `EmpGeoPrim`,
`SimpleGeoPrim`, `TexturedGeoPrim`, `GlowGeoPrim`) and `TARAtlas`/`TARSlot`/`TARSymbolData`. Those need checking
against driving-64 once this is merged. Also not done: the loader digest (every disc object loaded on 64-bit and
compared with the 32-bit build's), which belongs with package 9's first boot.

### 7.8 Done: package 8, the D3D seam and backend (15 October 2026)

**What the words hold.** A resource header's `data` word (`D3DResource`, the backend's `XboxPixelContainer`,
`XboxSurface`) is an address in a 32-bit word: the relocated headers in EAGL's files, and the headers the seam makes
itself. The backend's vertex and index buffers read it at word 1, surfaces keep their texture at `Parent` (word 5).
These go through `PtrTo32`/`PtrFrom32` now: the seam's encodes (`D3DResource_Register`'s base, which the backend
adds to the word in 32 bits as the original does; each Create's `data`), its decodes (the locks, the palette), and
the backend's decodes, all through its one `XboxDataPointer` (the backend includes driving/platform/Ptr32.h; on the
action engine's 32-bit build that is the same cast). No push-buffer word holds an address: `D3DPushBufferCursor` is
an ordinary pointer and `RunPushBuffer` is not implemented.

**Where the seam's resources live.** A TAR keeps its texture and palette in `Ptr32` members (Tar.h), and every header's
`data` word holds its memory's address, so on a 64-bit host the seam allocates headers, pixels, palettes and vertex and
index data from a window heap of its own (`ResourceAlloc`: `HostHeapAlloc` with zeroing; `calloc` on 32-bit). The
backend's own surface objects (`GetSurfaceLevel2`, the stand-ins) are not placed: nothing found keeps one in file
data, and `PtrTo32` would stop the first store that did.

**Fixed for x86-64.**
- The state tables' bases (`g_xboxTextureStateTable`, `g_xboxRenderStateTable`) and the action engine's overlay table
  bounds are `uintptr_t`, not `uint32_t` (the same type on 32-bit): the 26 int-to-pointer warnings of the two files.
- `D3DDevice_CreateVertexShader` passed the game's DWORD handle to the backend as a `void **`, which on x86-64 writes 8
  bytes over the next member of `EAGL::VertexShader`; the handle (a tagged index) now comes back through a local and
  is stored as 32 bits, on success only, as before.
- `GetSurfaceLevel2` strips the top nibble of the texture's word as D3D8 does; the window's heaps lie above 28 bits,
  so on a 64-bit host the mask keeps the whole word (`kSurfaceDataMask`).

Left as they are, correct on both: the vertex shader handles' casts (tagged indices, not addresses), and
`XGSetVertexBufferHeader`'s `& 0x7fffffff` on the word EAGL computes (`FUN_000f6d50`), right while that memory lies
below 2 GB (the window's heap arena ends near 0x60000000).

**32-bit unchanged.** Object diff against the build before the change (code bytes and relocations per section, data by
variable): actioninject 134 of 134 objects identical (so the action engine needs no replays); drivinginject,
driving_standalone and driving_standalone_test identical except `D3DDevice_CreateVertexShader` in d3dSeam.obj, the
handle's local. On the runner (rounds p8a, p8s, p8c, to tick 9000): the injected build against `lbase` (16 lockstep
runs exact, shadows on all eight missions with 0 differences), driving_standalone_test against `lbase` (16 of 16
exact) and, with `NIGHTFIRE_X87=OFF`, against `lbase_c` (16 of 16 exact).

**x86-64.** The probe: 993 error lines before and after (all package 6's failed checks; the two in d3dSeam.cpp's
translation unit are `D3DTile` and RealSystem.h's `SyncTask`), warning lines 242 before, 217 after; none left in
src/common/gfx or src/driving/gfx. MSVC x64 compiles the backend, the shader translators, FXAA and texture replacement
with no diagnostic; d3dSeam.cpp stops only on D3D8.h's `D3DTile` check (package 6).

### 7.9 Done: packages 4 and 5, CARP, task data, sound and FILESYS (15 October 2026)

**CARP.** A record's data word and a `TagReference` are window words: `UData::Data` and `AdoptData`,
`UGroup::GetArray` and `ResolveOffsets`, and `TagReference::Construct` decode and encode them with
`PtrFrom32`/`PtrTo32`. A relative offset still becomes an address by the original's 32-bit add on the word
(`item->data += PtrTo32(item)`). The readers of resolved references (`model`, `physicsAttributes`, `articleDesc`,
`path`, the triggers of `EFireRandomTrigger`, the mission rules' element words, the HUD's button message) decode with
`PtrFrom32<T>`. The payload records' pointer slots and the 80 event `Record`s were already `Ptr32` (package 2), and
the events' resolvers write them as `TagReference`s, so nothing more was needed there.
`EScheduleEvent::Record::staticData` is a reference too (event 81's resolver is `EDeactivateAIElement`'s, which
resolves the word at +4), now a `Ptr32<const void>`.

**Task data.** A scheduler task's data is a pointer: an event list's data or a heap record (`EPathSFX`'s update). So
`TaskCallback`, `Schedule::AddTask` and `TaskRecord::staticData` take `const void *`, as the event callbacks already
did. `TaskRecord` grows on 64-bit (a memory structure: its size check is `LAYOUT_32`).

**Package 5.** The banks' `AddBytes` adds on window words (`PtrFrom32(PtrTo32(p) + n)`), and the envelope tag's
default is `PtrTo32(&DefaultEnvelope)`. FILESYS passes user data, an op's name, buffer or archive, and the match
functions' argument as `intptr_t` (`int` on 32-bit, so the code is the same), and the atomic functions return
`intptr_t`. The sort key of its queues, the node's address, is `PtrTo32(node)`. The STREAM's user data is `intptr_t`.
A filter description is a `const SFilterDesc *` through `SNDCTRL_filteradd` and `SNDPLATFORM_filteradd`. `SetFxFunc`
and the sound options' vector table are typed function pointers (`SysOpts` grows on 64-bit; its check is `LAYOUT_32`).
`SNDSTRM_setgreedylevel`'s stream (the original passes the STREAM's address where it wants an index, and is refused)
is `PtrTo32` of it. realgraph needed nothing beyond package 2's `Ptr32` slots. Package 6 took the vtable constants,
the actor ids, the address keys (SkyWater, WRender, the cull items), the stream ring's mask, PlayMPC's allocators and
Feedback's stack; both packages gave `RSceneObj::TriggerFX` the `const Coord3 *` arguments of `RVehicle`'s override.

**The CARP records package 6 found** (in the tool's `LOADERS` table): `WTrigger::events`, `WCollisionInstance`'s
`article`/`articleGroup`, `WMapHeader::root` and `ArticleGfxEffect::reference` are `Ptr32`, and so is what a
collider's stamp points at (`ColliderStamp::source`, the address of an instance's `article`). The emitter key in
EffectsGallery is only used with `NIGHTFIRE_X87=1`, which a 64-bit build never has.

**The 32-bit build is unchanged.** Every object file of drivinginject, driving_standalone and driving_standalone_test
was compared with the build of 0cdc670 (p64_objdiff's comparison, renamed symbols mapped). 958 of 967 are identical.
The others differ only in symbol names: the signatures above, and the devtools fakes that follow them. One more
difference: in `ESetRenderVariation::Process`, `RSceneObj::GetViewDrawList` and `UseArticle`, the inlined
`UData::Data` swaps one `lea`'s operands (`[edi+ecx]` becomes `[ecx+edi]`), which computes the same address. The
runner rounds wait for the combined round after the 64-bit packages are integrated.

The world records' change, after the merge with driving-64, leaves the Collider and CollisionManager objects
identical.

**The x86-64 counts.** The probe, on this package alone (from 0cdc670): 993 to 995 distinct error lines (`TaskRecord`
and `SysOpts`, now `LAYOUT_32`), 242 to 212 warning lines. Merged with driving-64 (packages 1, 2 and 6): 11 error
lines, every one a check on an EAGL structure or one of the game's GeoPrims (packages 3a, 3b), and 182 warning lines.
Every int/pointer cast it still reports is in EAGL (Loader.cpp, Model.cpp, RuntimeAlloc.cpp), the D3D seam and
backend, or `NumGet::DoGetPointer` (StdStreams.cpp). The movie's two `_mm_empty` calls (GameLoop.cpp, EventsD.cpp:
EMMS after the player) are x86-32 only: MSVC has no MMX intrinsics on x86-64, and nothing there uses MMX. MSVC x64,
with static assertions disabled, then compiles and links both standalone executables, merged with driving-64 too
(LNK4281 aside, the low base); nothing has run.

### 7.10 Done: integration and package 9, the bring-up (15 October 2026)

**Integration.** driving-64 (packages 1, 2, 6 and dev) merged driving-64-eagl (3a, 3b), driving-64-d3d (8) and
driving-64-carp (4, 5). Only this document conflicted (each branch had added its section as 7.6 or 7.7); the sections
are kept in the order merged, 7.6 to 7.9. The one check left failing on x86-64 was package 6's list of the game's
GeoPrims, which EAGL draws as its own: `GainGeoPrim`, `EmpGeoPrim`, `SimpleGeoPrim`, `TexturedGeoPrim` and
`GlowGeoPrim` now hold their render method as a `Ptr32`, as `GeoPrim` does (`TARAtlas`, `TARSlot` and `TARSymbolData`
already held). Then the windows-x64 preset builds both standalone executables with 0 errors (MSVC: 33 C4267 and 26
C4244 narrowings, pointer differences and `strlen` sizes; one C4312, `NumGet::DoGetPointer`'s `strtoul`, as 7.6 says),
and the clang probe reports 0 error lines and 122 warning lines.

**The 32-bit build is unchanged.** Every object file of drivinginject, driving.exe, actioninject,
driving_standalone and driving_standalone_test (x87 on and off), compared with the build of 7d096e7 (p64_objdiff's
comparison, anonymous namespaces normalised): actioninject and driving.exe identical; the rest identical except the
packages' documented differences (renamed signatures and the callers naming them, `D3DDevice_CreateVertexShader`'s
local, the swapped `lea` operands of 7.9), the crash reporter's new formats, and `RSceneObj::GetRenderOffset`'s
mangled name (same code). On the runner, with the merge (rounds p9a, p9a2, p9s, p9c): the injected build against
`lbase` (16 lockstep runs exact, shadows on all eight missions with 0 differences), driving_standalone_test against
`lbase` (16 of 16) and with `NIGHTFIRE_X87=OFF` against `lbase_c` (16 of 16). The final state was checked the same way
(rounds p9fa, p9fs, p9fc).

**Running the 64-bit build on the runner.** `RUNNER_PRESET=windows-x64 tools/runner/remote.sh build` configures and
builds the preset in the runner's checkout and installs the executables, maps, PDBs and the x86-64 FFmpeg DLLs into
`Release64\` beside `Release\`; `slots` copies it into each slot. Runs name `-Exe 'Release64\driving_standalone_test.exe'`
and keep `Release\` as the working folder (settings.ini, the dumps, the traces); the disc is `..\disc` from the
executable as before. `round.sh` picks the preset itself for an `EXE` under `Release64\`:

```
EXE='Release64\driving_standalone_test.exe' BASE=lbase_c tools/runner/round.sh p64r lockstep
```

`compare.py` masks one thing in the sound traces: `SNDSTRM_overhead`'s answer and `SNDSTRM_create`'s memory size,
which are sizes of the stream library's pointer-bearing records (0xd38 on 32-bit, 0x103c on x86-64). Nothing else
differs.

**Crash forensics.** The standalone crash reporter prints addresses, registers and stack words at the host's width,
with r8-r15 on x86-64 (Win64's context and Linux's), and scans the stack in pointer-sized steps; `tools/symbolise.py`
reads 16-digit addresses and MSVC x64 maps. The x86-64 standalone executables are linked with a PDB (`/Zi`, `/DEBUG`
with `/OPT:REF /OPT:ICF`, so the code is as without it), so a `crash.dmp` from the runner opens in cdb with source lines
and locals. That is what found most of the faults below.

**What the first boots found.** Nothing stored an address above 4 GB: the window held. Every fault was one of these
kinds, and each fix leaves the 32-bit build's code as it was:

| Kind | Where |
|---|---|
| A method called through a plain function pointer (a vtable slot read as data, or a method's address) as `__fastcall (this, int, ...)`: on x86 the dummy is EDX and lines up with `__thiscall`; on x86-64 it takes the first argument's register, so every argument moved one along | `AnimVCall` (the anim banks: no bank loaded), the UGroup processors (every CARP resolution), the particle systems' destroy, update and create, the deleting destructors (particles, state manager, texture contexts), the window map's rotations, the article map's find, the static lists' erase. platform/MethodPointer.h's `METHOD_CALL`, `METHOD_EDX` and `METHOD_EDX_ARG` give the pointer and its calls each architecture's form; pointers to methods with no arguments keep their plain `int` |
| A size or pool still written as the 32-bit layout's number, so x86-64's larger objects overran their blocks or two objects shared one | the EAGL symbol entries (`n + 5`), the anim pool's per-type object sizes (and the pool, scaled with them), FILESYS's block (its op records ran into the next allocation, the font's vertex shader), the rigid body pool, the mixer's unpacker nodes, the weapon manager, the cameras, the anim handles, targetables, voices, the action queue, the gallery, OBBs, the AI commands, the input devices and their mappings (freed with a literal size, into the wrong pool list), the lightning segments. All `sizeof` now, with `LAYOUT_32` checks on the original numbers where they were tables |
| An owner found as `this - 4` | `RStateNamespace::Owner` and EAGL's private parts (`RenderContextPrivate`, `TextureRenderContextPrivate`, `DevicePrivate`, `ViewPortPrivate`): `offsetof` the member |
| A layout used through raw offsets | EA's block allocator (RealMemory.cpp): its header links and the class record's are 32-bit words in the window (`Ptr32`), and `MEM_size` reads 32 bits; the C++ library's virtual-base offsets (the istrstream and ostrstream vbtables, the vbase destructors): their own vbtables on a 64-bit host, the class block keeping the original's words, which negative characters read; the water's UV scale and scroll names, which counted 16-byte steps from RWater's base (mission 2's reflection of the surface) |
| File data still read through native pointers | `StreamEventData` (the stream events' type and name), `WorldModel` (an article's model group, info and path data), `EventSceneRef`, the camera's scene-object description; `WorldSceneObject` keeps its proc-anim state in a word of its matrix, now a `Ptr32` |
| A float method read as a double (x87's ST0) | the wheels' road height (the car's simple shadow) and the car's speed (the debug view): `X87Float` (X87.h) is a double on x86 and the float on x86-64; the base `RSceneObj::GetRenderOffset` answers a double like `RVehicle`'s, so slot 9 has one type |

**Result** (round p64r2, the runner, to tick 9000): all 16 lockstep runs of the x86-64 test executable are exact
against `lbase_c`, frames and sound traces. The only intermediate differences were the car shadows' height (mission
1) and mission 2's water reflection, both found by comparing the backend's per-draw traces (`d3d9_trace_<frame>.log`,
addresses masked) and render-target dumps of the two builds at the same tick. No floating-point difference showed: the
32-bit portable-C build's remaining x87 use (float returns and some multiplies) gives the same results as x86-64's SSE
here, so `lbase_c` needed no regeneration.

**Not done.** The loader digest (section 9) was not built: the sound traces were identical from the first full run and
every frame difference was found from the draw traces, so it was not needed to reach exactness. The per-tick state
trace was not needed either. Not checked: the sound heap's and the other fixed pools' high-water marks on x86-64 (no
allocation failed in the 16 runs), a throw through the C++ library's own exception records (`ThrowInfo` holds 32-bit
layouts; no throw is reached in these runs), and Linux x86-64 (package 10, 7.11).

### 7.11 Done: package 10, Linux x86-64, and a game window above 4 GB (macos.md M1, 15 October 2026)

**The preset.** `linux-x64` (cmake/toolchain-linux-x64.cmake): clang, Ninja, `NIGHTFIRE_SDL` on, x87 off as on every
64-bit host, SDL3 by the same FetchContent, dxvk-native's x86-64 libraries from the same release (`lib/` beside
`lib32/`), and FFmpeg from `tools/fmv/build_ffmpeg.sh linux-x86_64 <prefix>` (gcc, no `-m32`, no stack realignment).
x86-64 has one ABI, so the two-ABI split of the i386 build (cmake/xbox-abi-linux.cmake, `libdrivinghost.so`) is not
used: every source, the host layer, SDL's window and sound, FFmpeg's loader and the D3D9 backend included, is
ordinary Linux code linked into the executable, as on Windows x64. MSVC's `<intrin.h>` still comes from
src/common/linux. The executable is position-independent code linked at a fixed address (`-fPIE` with `-no-pie`), so
that it can lie in a window above 4 GB.

```
sh tools/fmv/build_ffmpeg.sh linux-x86_64 ~/ffmpeg-x64
cmake --preset linux-x64 -DNF_FFMPEG_LIB_DIR=$HOME/ffmpeg-x64/lib    # -DNIGHTFIRE_WINDOW_BASE=0x100000000 for 4 GB
cmake --build --preset linux-x64                                       # driving_standalone, driving_standalone_test
```

SDL's windows and sound need the x86-64 development packages (portable.md's list without `:i386`); a headless-only
machine without them (this PC's WSL2, where nothing could be installed) configures with `-DSDL_UNIX_CONSOLE_BUILD=ON`
and keeps SDL's offscreen video and dummy audio, which is all lockstep runs use.

**LP64.** `long` is 8 bytes on Linux. Win32Types.h's `DWORD`/`ULONG` and `LONG` are `uint32_t` and `int32_t` there
(`unsigned long` and `long` on 32-bit hosts, as before), the pointer-sized ones 64-bit Windows'. Host.h speaks
`HostDword` and `HostLong`, which are `unsigned long` and `long` on Windows and on 32-bit hosts (so the Windows and
i386 objects are unchanged) and the fixed-width types on LP64; HostPosix, KernelImports' kernel calls, the game
window's heap functions and the C++ library's `_Init_locks` count follow. The C runtime's `atol` and `strtol`
accumulate in 32 bits and now sign-extend their result into the `long` they return (on LP64 a negative number had
come back as a large positive one). The other `long`s in game code are locals or memory-only members holding values
that fit (PlayMPC's movie offsets, iostream's word, `FloatIn`), so nothing else changed.

**HostLock.** A pthread mutex is 40 bytes on x86-64; as on Win64, the lock's 24 bytes hold a pointer to a recursive
mutex of its own from the C library's heap, never freed.

**`operator new`.** The executable's window heap serves the shared libraries too (dxvk, lavapipe, libstdc++), since an
ELF executable's definition interposes: hiding it was tried, and libstdc++'s own `std::string` code then allocated
with its `new` what the game's inline code freed with ours.

**A non-zero window base (`NIGHTFIRE_WINDOW_BASE`).** A CMake setting, 0 by default, `0x100000000` as macOS needs it.
`kGameWindowBase` stays a compile-time constant (platform/Ptr32.h): with base 0 `PtrFrom32`/`PtrTo32` compile to what
they did; with another base a decode adds it and an encode subtracts it, and null is the word 0 both ways (a select
each way). The window's first 64 KB is never allocated, so no object has the word 0. The image is linked 4 MB into
the window (`/BASE:0x100400000` on Windows, `--image-base` on Linux), so the words of the executable's statics are the
same as with base 0. `HostAllocatePages`' range and `HostReserveLow`'s lowest address are offsets in the window. The
game window's set-up stops the game if the executable is not inside it. Found and fixed by running with the base at
4 GB, where a word decoded as a plain address points 4 GB too low:

| What | Where |
|---|---|
| The game's data holds addresses on its threads' stacks (a sprite's vertex, colour and UV arrays go into an EAGL request's parameters, which are `Ptr32` words) | every thread HostCreateThread starts has its stack in the window: on Linux (and the multimedia timer's thread) a mapping from offset 0x10000000 given with `pthread_attr_setstack`; on Windows, which places a new stack at the lowest free address, every free range below the window is reserved while the thread is made. With base 0 on Linux this was already wrong (stacks lie near 0x7fff...), so it is needed for package 10 itself |
| A resolver's answer is an address or a number (the animation banks' `event.` names resolve to event ids) | EAGL's loader encodes it with `AnswerTo32` (below 4 GB, with the window above it, it is the number); `ActManager::SymbolResolver` returns `intptr_t`, since it is called as a pointer-returning resolver |
| A `Ptr32` passed through `...` is the 4-byte word, not a pointer (`Crt::sprintf` of a stream event's name) | `ValidateStreams` casts to `const char *`; a build with a non-trivial `Ptr32` copy constructor (clang refuses such a class through `...`) found no other site |
| A word decoded with a plain cast | `RSceneObj::ModelGroup` (the article's model `TagReference`) uses `PtrFrom32` |
| lld relaxes a GOT load (`add foo@GOTPCREL(%rip), %reg`) into a 32-bit immediate address in a non-PIE executable, without checking that the address fits | `-Wl,--no-relax` when the base is not 0 |
| The C library's non-PIC start-up files (crt1.o, crtbegin.o) hold 32-bit absolute addresses | linked with `-nostartfiles` and the PIC ones (Scrt1.o, crti.o, crtbeginS.o; crtendS.o, crtn.o) |

Left as it is: `SNDPLATFORM`'s reverb mode 1 reads an effects bus's `delay` (a number) as a description pointer, as
the original does; no run selects mode 1. `gamewindow_test` now runs its checks on a thread of the host's (the
process's first thread's stack need not be in the window) and also checks null both ways, the window's first 64 KB
and a thread's stack.

**Tests.**

- `gamewindow_test 1000000`: Windows x64 base 0 and base 4 GB, Linux x86-64 base 0 and base 4 GB, 1,000,029 checks,
  0 differ (image at 0x407000 / 0x100408000 on Windows, 0x40b648 / 0x10040b7a8 on Linux); Win32 300,000 operations,
  0 differ.
- Windows x64 on the runner, to tick 9000 against `lbase_c`: base 4 GB (round m1w4, the executable at 0x100400000)
  and base 0 (round m1w0) both 16 of 16 exact, frames and sound traces.
- The 32-bit build: every object of drivinginject, driving_standalone and driving_standalone_test compared with the
  build of 60f3014 (objdiff_anon): identical except `RSceneObj::GetViewDrawList`, whose inlined `ModelGroup` swaps one
  `lea`'s operands (`[edi+ecx]` for `[ecx+edi]`), as in 7.9.
- Linux, this PC's WSL2 (lavapipe, 640x480, 2 slots, `RUNNER_HOST=wsl RUNNER_PRESET=linux-x64`): new baselines
  `lwsl_c_*` from the i386 build with `NIGHTFIRE_X87=OFF` (round lwsl_c), then the x86-64 build against them, to tick
  9000: base 0 (round lx64w0) 16 of 16 exact, frames and sound traces; base 4 GB (round lx64w4, the executable at
  0x100400000) 16 of 16 exact (three runs timed out at 20 minutes while WSL2 was overloaded by other processes, exact as far as they went, and were exact when run again). So the i386 and x86-64 builds simulate and draw alike on one machine, although glibc's
  i386 and x86-64 `sin`, `cos`, `atan2`, `log`, `pow`, `acos`, `asin` and `exp` differ in the last bit for about one
  argument in a thousand (measured, 200,000 random arguments each): as with the x87 conversion (4.4), the game's
  results are not sensitive to those bits in these runs.
- Built: MSVC Win32 (drivinginject, driving, both standalone executables, gamewindow_test), MSVC x64 (both standalone
  executables, gamewindow_test, both bases), clang linux-i386 and linux-x64 (both standalone executables, both bases),
  the mingw cross build (nf-cross, `linux` preset, every target).

**Not done.** A windowed Linux x86-64 run (no x86-64 X11/Wayland development packages in this WSL2, and game windows
need the user). The x86-64 runs were slower than the i386 ones (8-12 minutes against 4-7), but
WSL2's CPU was then shared with other processes' lavapipe runs (a load average near 100), and the reruns on an idle
WSL2 took 5 minutes, as the i386 build's do.

## 8. Work packages

In order; each keeps the Windows 32-bit build unchanged (Ptr32 is `T *` there) and is tested by it. Sizes: S < 1 day of
agent time, M 1-2, L 3+.

| # | Package | Size | Test |
|---|---|---|---|
| 0 | **Pre-checks on 32-bit**: `NIGHTFIRE_HEAP_PAD` (devtools, beside HEAP_SHIFT) in lockstep on all missions; an x87 call counter (devtools, calls per tick per function) on one mission; the float-store trap's caller list from the 32-bit listings. **Done 15 October 2026** (4.3, 7.4): the particle map's address key is the one size-dependent behaviour | S | runner round, lockstep 16/16 against lbase |
| 1 | **Scaffolding**: an x86-64 configuration for the standalone targets only (MSVC x64 and the nf-cross clang); `NIGHTFIRE_STANDALONE` guards round the 61 adaptors; x86-only guards in xbeAbi.h/xbeOverload.h; x86-64 FFmpeg DLLs; the probe (`--probe-script`) in CI, its counts falling to 0 as the packages land. **Done 15 October 2026** (7.5) | M | 32-bit: asm diff identical, lockstep exact; 64-bit: the probe's counts |
| 2 | **`Ptr32<T>` and the game window** in the host layer (heaps, pages, thread stacks, `operator new`), the range check, `PtrFrom32`/`PtrTo32`. **Done 15 October 2026** (7.5), with the 110 file structures' members retyped; thread stacks not placed | M | 32-bit: unchanged; 64-bit: a unit test of the window and the encoder |
| 3a | **EAGL loader and models**: Loader.cpp (sections, relocations, symbol values), `Model`, `TAR`, `GeoPrim`, `DrawArray`, `LoadedVertexBuffer`, the word streams of Model.cpp. **Done 15 October 2026** (7.7) | L | 32-bit asm diff and LoaderShadow (injected build); 64-bit: loading every object on the disc (a devtools mode of the test executable comparing a digest of each loaded object with the 32-bit one's, pointers canonicalised) |
| 3b | **Render methods and EAGLAnim data**: `RenderMethod`, the packets, the anim banks and delta data, skeletons. **Done 15 October 2026** (7.7) | M | same |
| 4 | **CARP**: `UData`/`UGroup`/`TagReference` decoding, the payload records (`AIElement`, `AnimInfo`, `WorldArticle`, ...), the 80 event `Record`s, the vtable constants of the data layer | L | the same digest over each track's resolved CARP. **Done 15 October 2026** (7.9), the vtable constants with package 6 |
| 5 | **realgraph, sound banks and streams, FILESYS and the int handles** (FNTX slots, timbre words, `AddBytes`, stream/filter handles, FILESYS user data, actor ids, address keys) | M | 32-bit asm diff; 64-bit sound traces. **Done 15 October 2026** (7.9); actor ids and address keys with package 6 |
| 6 | **Memory-only structures**: the 32-bit-only check form (and the generator's), the literal allocation sizes, the `* 4` arrays, the remaining vtable constants, the 66 narrowings - by directory, in two or three agents. **Done 15 October 2026** (7.6) | L | 32-bit asm diff (nothing may change), checks still all true on 32-bit |
| 7 | ~~x87 on x86-64~~: replaced by the C forms (`NIGHTFIRE_X87=OFF`, **done 15 October 2026**, 4.4); left: the conversion edge cases (out-of-range float-to-unsigned and float-to-64-bit) | S | lockstep against `lbase_c` |
| 8 | **The D3D seam and backend**: the state tables' bases, resource `data` words, push-buffer and stream words as window words; kept working for the action engine's 32-bit build. **Done 15 October 2026** (7.8) | M | 32-bit lockstep frames (driving) and the action engine's menu replays |
| 9 | **Bring-up**: first boot of the 64-bit test executable, then lockstep on all missions against the 32-bit standalone's baselines on the same machine; a per-tick state trace (rigid bodies, AI elements; canonical, no addresses) to find the first divergent tick. **Done 15 October 2026** (7.10): 16 of 16 exact against `lbase_c` | M | the 16 lockstep runs, frames and sound traces |
| 10 | **Linux x86-64** (after step 2): the LP64 `long`s, the Win32-types header with fixed-width types, the runner's Linux baselines. **Done 15 October 2026** (7.11), with macos.md's M1 (the window at 4 GB) | S-M | Linux lockstep |

Packages 3a-5 are independent once 2 has landed; 6 and 8 can run beside them. Two agents at a time fit them
(3a + 4, then 3b + 5, then 6 + 8).

## 9. Testing

- **The oracle**: the Windows 32-bit build. Every package must leave it unchanged - per-function assembly diff
  (`cl /O2 /FA`, as the style cleanups did) and a lockstep round against `lbase` - since `Ptr32` is a plain pointer
  there.
- **The 64-bit build against it**: lockstep frames and sound traces on the same machine (the 32-bit standalone
  test executable with `NIGHTFIRE_X87=OFF` has its own baselines, `lbase_c`, on the runner (4.4); this PC would need
  its own). Exact is the goal; the OFF build is already exact under heap padding.
- **Before boot**: the probe's counts and the tool's (`--list assert.size`, `cast.`) as the burn-down; the loader
  digests of packages 3-4 compare every loaded object and resolved CARP file with the 32-bit build's, offline, with
  no game window.
- **Divergence**: the per-tick state trace localises the first tick and object; the shadows stay available in the
  32-bit build for any function whose port is suspected.

## 10. Decisions for the user

1. **Memory-only structures**: native pointers (natural C++; allocations differ from the original's sizes - a
   documented departure, subject to package 0's pad test) or `Ptr32` everywhere (identical layouts and allocations;
   ~1,700 members retyped). Recommended: native.
2. **The window on Windows**: `/LARGEADDRESSAWARE:NO` (the whole process below 2 GB; nothing to place, but every DLL
   and driver shares 2 GB, as a 32-bit process shares 2-4 GB) or the host layer placing the game's memory low (more
   code, no limit on the rest). Recommended: the host layer, which is also what Linux needs.
3. **The x87 code on x86-64**: A (assembly, exact, recommended), B (software x87 now) or C (portable C now, new
   baselines and no lockstep against the 32-bit build).
4. **The 64-bit Windows toolchain**: MSVC x64 with a MASM file for the x87 stubs, clang-cl, or 64-bit only through
   clang (mingw on Windows, Linux).
5. **Style-guide exceptions**: `Ptr32<T>` members in file structures and the 32-bit-only check form in game code;
   both are new forms the style guide should name.
6. **The bar for step 3**: exact lockstep against the 32-bit build on the same machine (recommended), or "runs and
   plays" with new 64-bit baselines.

### Decided (15 October 2026)

- In-memory structures: **native pointers**, with their layout checks made 32-bit-only (a documented exception to the
  style guide's same-allocation-sizes rule; package 0's heap-padding lockstep checks first that nothing depends on them).
- x87: **superseded the same day** - the user is not concerned by last-bit differences (the original's own platforms
  differed), only by keeping validation exact. So the x87 code becomes portable C, as Apple Silicon needs anyway, in a
  step of its own **on the 32-bit build, before the 64-bit work**: convert, measure the divergence against the x87
  baselines, make new baselines from the converted build once, and from then on every step (64-bit included) is exact
  against those. No MASM stubs.
- The bar for step 3: **exact lockstep against the 32-bit standalone build** on the same machine.
- 64-bit Windows toolchain: **MSVC x64** (no MASM needed once the x87 code is C).
- Not asked, taken as recommended: the host layer allocates the game window low (decision 2); `Ptr32<T>` members and the
  32-bit-only check form become named exceptions in the style guide (decision 5).
