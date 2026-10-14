# The driving engine on 64-bit

Status: plan (15 October 2026), step 3 of the roadmap in [portable.md](portable.md): an audit and a design, no game
code changed. It applies to the standalone executables (`driving_standalone`, `driving_standalone_test`) only. The
injected build (driving.exe mapping Driving.xbe, drivinginject.dll, every shadow test) stays 32-bit: it shares the
original's address space by construction, and it stays the oracle.

## Summary

| What | Measured | Recommended |
|---|---:|---|
| Structures with pointer-sized members | 1,078 of 1,607 | in memory only (916): native pointers, the structures grow |
| Structures read in place from the game's files | 110 (202 pointer-sized members) | `Ptr32<T>`: the file's 32-bit word stays where it is, decoded against a 4 GB "game window" |
| Layout checks (`static_assert` on sizes and offsets, with the generated ones) | 1,375; 1,017 fail on 64-bit (clang: 1,032 lines) | a 32-bit-only form for memory structures; the 31 on file structures must hold on 64-bit too |
| Pointers made 32-bit integers explicitly (silent on 64-bit) | 108 (63 of them vtable constants) | typed pointers; the file words through `Ptr32` |
| 32-bit integers made pointers | 48, and 72 clang warnings | the same |
| Objects allocated by a literal size / pointer arrays sized `* 4` | 26 / 6 | `sizeof` |
| Naked functions | 108: 43 x87, 59 register adaptors, 4 injected-only, 2 others | x87: the same instructions in x86-64 assembly (exact against the 32-bit build on the same CPU); adaptors: left out of the standalone build |
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
retargeting is the same encoding on x86-64 (within one image). The sound trace prints no heap addresses (it already
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
The number of calls per tick is not measured yet (package 0 counts them on the runner).

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
| SDL3 | built from source at release-3.2.30: any architecture |
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

The other way, `Ptr32` in every game structure, keeps every layout, check and allocation identical to 32-bit and
needs no pool work, at the price of about 1,700 members retyped and code that reads less like the original's. It is
the fallback if the pad test fails and the cause cannot be fixed.

### 7.3 Conversions

The 63 vtable constants become `const void *` (or `Vtable *`); APIs that pass pointers as `int` take `intptr_t` or
the pointer type (FILESYS user data, the sound library's handles, actor ids, TriggerFX); address keys become
`uintptr_t`; function-pointer words in data (CODA, the sound vectors) are `Ptr32` or typed; the mask uses
`~uintptr_t(0x7f)`; literal allocation sizes become `sizeof`. Where the original's 32-bit arithmetic is the point
(the banks' `AddBytes`, CARP's relative offsets), it stays 32-bit on the word, then decodes.

## 8. Work packages

In order; each keeps the Windows 32-bit build unchanged (Ptr32 is `T *` there) and is tested by it. Sizes: S < 1 day of
agent time, M 1-2, L 3+.

| # | Package | Size | Test |
|---|---|---|---|
| 0 | **Pre-checks on 32-bit**: `NIGHTFIRE_HEAP_PAD` (devtools, beside HEAP_SHIFT) in lockstep on all missions; an x87 call counter (devtools, calls per tick per function) on one mission; the float-store trap's caller list from the 32-bit listings | S | runner round, lockstep 16/16 against lbase |
| 1 | **Scaffolding**: an x86-64 configuration for the standalone targets only (MSVC x64 and the nf-cross clang); `NIGHTFIRE_STANDALONE` guards round the 61 adaptors; x86-only guards in xbeAbi.h/xbeOverload.h; x86-64 FFmpeg DLLs; the probe (`--probe-script`) in CI, its counts falling to 0 as the packages land | M | 32-bit: asm diff identical, lockstep exact; 64-bit: the probe's counts |
| 2 | **`Ptr32<T>` and the game window** in the host layer (heaps, pages, thread stacks, `operator new`), the range check, `PtrFrom32`/`PtrTo32` | M | 32-bit: unchanged; 64-bit: a unit test of the window and the encoder |
| 3a | **EAGL loader and models**: Loader.cpp (sections, relocations, symbol values), `Model`, `TAR`, `GeoPrim`, `DrawArray`, `LoadedVertexBuffer`, the word streams of Model.cpp | L | 32-bit asm diff and LoaderShadow (injected build); 64-bit: loading every object on the disc (a devtools mode of the test executable comparing a digest of each loaded object with the 32-bit one's, pointers canonicalised) |
| 3b | **Render methods and EAGLAnim data**: `RenderMethod`, the packets, the anim banks and delta data, skeletons | M | same |
| 4 | **CARP**: `UData`/`UGroup`/`TagReference` decoding, the payload records (`AIElement`, `AnimInfo`, `WorldArticle`, ...), the 80 event `Record`s, the vtable constants of the data layer | L | the same digest over each track's resolved CARP |
| 5 | **realgraph, sound banks and streams, FILESYS and the int handles** (FNTX slots, timbre words, `AddBytes`, stream/filter handles, FILESYS user data, actor ids, address keys) | M | 32-bit asm diff; 64-bit sound traces |
| 6 | **Memory-only structures**: the 32-bit-only check form (and the generator's), the literal allocation sizes, the `* 4` arrays, the remaining vtable constants, the 66 narrowings - by directory, in two or three agents | L | 32-bit asm diff (nothing may change), checks still all true on 32-bit |
| 7 | **x87 on x86-64**: the 43 functions as x86-64 stubs (option A), float-returning variants where package 0 found float stores, the control word on every game thread, the conversion edge cases | M | a host-side test comparing each stub with the 32-bit function on a sweep of inputs (same machine), then lockstep |
| 8 | **The D3D seam and backend**: the state tables' bases, resource `data` words, push-buffer and stream words as window words; kept working for the action engine's 32-bit build | M | 32-bit lockstep frames (driving) and the action engine's menu replays |
| 9 | **Bring-up**: first boot of the 64-bit test executable, then lockstep on all missions against the 32-bit standalone's baselines on the same machine; a per-tick state trace (rigid bodies, AI elements; canonical, no addresses) to find the first divergent tick | M | the 16 lockstep runs, frames and sound traces |
| 10 | **Linux x86-64** (after step 2): the LP64 `long`s, the Win32-types header with fixed-width types, the runner's Linux baselines | S-M | Linux lockstep |

Packages 3a-5 are independent once 2 has landed; 6 and 8 can run beside them. Two agents at a time fit them
(3a + 4, then 3b + 5, then 6 + 8).

## 9. Testing

- **The oracle**: the Windows 32-bit build. Every package must leave it unchanged - per-function assembly diff
  (`cl /O2 /FA`, as the style cleanups did) and a lockstep round against `lbase` - since `Ptr32` is a plain pointer
  there.
- **The 64-bit build against it**: lockstep frames and sound traces on the same machine (the 32-bit standalone
  test executable matches `lbase` on the runner already, so `lbase` serves; this PC uses `lloc`). Exact is the goal
  with x87 option A and either 7.2 option, provided package 0's pad test is exact.
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
- x87 on x86-64: **assembly stubs** with the same instructions (exact against the 32-bit build on the same CPU).
- The bar for step 3: **exact lockstep against the 32-bit standalone build** on the same machine.
- 64-bit Windows toolchain: **MSVC x64 plus a MASM file** for the stubs.
- Not asked, taken as recommended: the host layer allocates the game window low (decision 2); `Ptr32<T>` members and the
  32-bit-only check form become named exceptions in the style guide (decision 5).
