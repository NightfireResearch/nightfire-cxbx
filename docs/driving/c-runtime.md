# The driving engine's C runtime

The C and C++ runtime the XDK linked into Driving.xbe in 2002 (`sys.crt`, 0x00130b80-0x0013b7b0): the multithreaded
C library (the heap, the exit list, the locks, per-thread state, the printf and scanf engines with their float
conversions, strtol and atol, qsort, rand, the character classes, the x87 helpers) and the non-template half of
Dinkumware's C++ library (MSVC 7) that the game's string streams stand on (the locks and mutexes, the global locale
and the facet list, `_Locinfo`, ctype's tables, `ios_base`, `strstreambuf`, `_Stod`, `std::exception`, the throw
and the array iterators). Ported on 12 October 2026 by two packages, then merged (`src/driving/platform/`). It is the
last original code our code called: with it nothing of the game's runs but the exception funclets of functions we
replaced. Every address is Driving.xbe's.

**Status: ported and tested (13 October 2026).** 70 entries, all `FUNC_AT`: 28 of package A (the C library: sprintf,
vsprintf, `_vsnprintf`, sscanf, atof, atol, the helpers the streams hold - localeconv, strtol, strtoul, `_errno`,
`_strtoi64`, `_strtoui64` -, tolower, isdigit, isalnum, qsort, rand, malloc, free, calloc, atexit, `_clearfp`,
`_CIacos`, `_CIasin`, `_CIpow`, `__mtinitlocks`, `_mtdeletelocks`, `_amsg_exit`), 39 of package B (the C++ library)
and, from the integration, three the startup and the fatal stop still reached: `_onexitinit` 0x00132a1b (the exit
list), `_fpmath` 0x00131d28 (the x87's precision) and `_exit` 0x0013438b. What the entries reach is ported untagged
as far as they need it: strtod 0x00135196, realloc 0x00134815, `_msize` 0x001369cc, `_lock`/`_unlock`, the 12-byte
long double arithmetic and both float conversions, the printf and scanf engines (`_output`, `_input`). `sys.crt` is
ours by functions: 74 replaced, 338 dead, nothing live. Two shadow tests compare the ports with the originals: both
clean in game on all eight missions, and the lockstep runs exact on all sixteen (see what the testing taught). Since (13 October 2026, docs/driving/standalone.md): `_purecall`
0x00131d35 (Ghidra: `__pure_virtual`, `_amsg_exit(25)`), which 91 slots of our vtables hold, and the kernel's critical
sections and KeBugCheck through platform/KernelImports.cpp's named wrappers. Since 14 October 2026 (the standalone executable, standalone.md) the C++
library's last 15 virtual functions are ported too (untested in game: nothing the game does reaches them):
strstreambuf's overflow, pbackfail, underflow, seekoff and seekpos (and fpos(streamoff) 0x00131488), the deleting
destructors of `_Locimp`, runtime_error, ios_base::failure, exception, bad_cast and the ostream and ostrstream ios
entries, with the destructors and copy constructors they and the throw information name (runtime_error, failure,
bad_cast, type_info), exception::what, runtime_error::what and failure's `_Doraise`. The throw information of the
four types thrown (length_error, out_of_range, bad_cast, ios_base::failure) is ours as typed records (`Crt::ThrowInfo`,
`CatchableType`, `TypeDescriptor`, CRuntimeStd.cpp, checked against the XBE by driving_data_check.py), and the
lock table, its static sections, the library's four locks, `__lconv`'s table and `_XcptActTab` are owned globals.

## Why the game's and not ours

Our DLL links a modern runtime of its own. Its names are the same, its answers are not always:

- **Parsing floats.** The 2002 runtime gathers up to 24 significant digits into a 12-byte long double (a 16-bit
  extension below a 64-bit mantissa), multiplies by powers of ten from its own tables of 12-byte constants
  (`__strgtold12`) and rounds that to a double or a float (`__ld12cvt`), rounding a tie down. Neither step is
  correctly rounded, so a modern strtod can answer one ulp away - and a tuning value one ulp off changes the
  simulation. atof, strtod, sscanf's `%f` and the streams' `_Stod` all go through the port of that path
  (CRuntimeFloat.cpp).
- **Printing floats.** `%e`, `%f` and `%g` turn the double into up to 17 decimal digits the same way round
  (`$I10_OUTPUT`) and the layouts round those again, so ties and long precisions can differ from ours; `%e` prints
  three exponent digits. Every format string our code passes was surveyed; the float ones (the streams' `num_put`,
  RoadNav's debug line, DebugAudio's target labels, the loading screen's log, the debug channels and FONT_drawtextfa,
  which take a caller's format) go through the port.
- **rand is per thread.** Its seed is the per-thread block's `randomSeed` (+0x14 of the 0x84-byte `_tiddata`,
  XboxStartup.cpp's Xbox_getptd), 1 when a thread's block is made; nothing in the game calls srand (there is none in
  the XBE). The main thread (SimRandom, PhysicsUtil, the noise) and the sound thread (ASoundManager) each draw their
  own sequence, with the original's generator (seed * 214013 + 2531011, bits 16-30).
- **qsort is not stable.** Equal elements land in the order its quicksort leaves them (the median of three, the
  partitions on a stack, runs of eight or fewer to a selection sort), and the comparator sees the original's pairs
  in the original's order: AIElements' range sort, the symbol tables and Targeting depend on both.
- **The heap.** malloc, calloc, realloc, free and `_msize` are the runtime's layer over the process heap
  (XboxStartup.h's ProcessHeapHandle, a Win32 heap since XAPI's heap became Win32's): malloc rounds every request up
  to 16 bytes (the runtime's `__active_heap` is never set, and its test is "not the system heap"), calloc and realloc
  do not, a request above 0xffffffe0 fails at once, and the new handler is never set. EAGL's `eagl_alloc`, the
  per-thread blocks, the exit list and the C++ library's copies all come from it as before.
- **atol wraps** on overflow where ours saturates; strtol and its forms set the game's errno (the streams read it).

What our code calls of our own DLL's runtime it calls only where the answer is the same for anything the game can
give it: memcpy, memset, strlen, strcmp, strcpy, strncpy, strchr, `_stricmp` and its kin (C locale, ASCII), strtok
(the same tokens; ours keeps its place per thread, as the original's does), `sprintf` and
`snprintf` of integers and strings only (GameLoop's paths, the HUD's numbers), sscanf of hex (UData's `%04x`) - and
the D3D and DSOUND seams' own objects, which the libraries they replace never took from the C runtime's heap.

## Who reaches it

Everything that called the game's runtime by address now calls the port directly: the streams (StdStreams.cpp,
StdLocale.cpp, DebugVariables.cpp: the locale, ios_base, strstreambuf and the facets' helpers, `_Stod`, strtol,
errno, localeconv, sprintf), the data layer (Tuning's sprintf and sscanf, IniFiles' and AttributeParsers' atof and
atol, SymbolTable's qsort and atol), the cameras' ini loader, the audio (Index's, Mix's and SoundGroup's sscanf, the
sound manager's rand and sprintf), the anim database and characters (tolower), SimRandom and PhysicsUtil (rand),
InputConfig (atol, sscanf), StateManager (isalnum, atof, sscanf), AIElements and Targeting (qsort), LoadingScreen and
Subtitles (atol), GameLoop and EventsD (`_clearfp`, free), CoreFoundation (vsprintf), EAGL (`eagl_alloc`/`eagl_free`,
the profiler's `_vsnprintf`, RuntimeAlloc's atof, atol and sscanf), the STL's throws (AttributeContainers,
SymbolTable, CoreFoundation, RigidBody: `_CxxThrowException`), the `eh vector` iterators (CoreContainers, the AI's
vehicles, BondCarBasics, SoundGroup), atexit (the static initialisers and the function-local statics), and the x87
helpers (IK and EventsA's acos, CameraSpline's acos and asin, Weapon and VehicleRender's pow: naked wrappers that
call `Crt::CIacos` and its kin). The startup calls `Xbox_mtinit`, `Crt::_fpmath` and `Crt::_onexitinit` itself,
and the static initialisers' `_Init_locks` and `_Init_atexit` objects are constructed and destroyed through the
ports. The game's printf (0x00132192) has been ours from the start (logging.cpp's dbg_wprintf, the host's printf:
debug output); its by-address callers now call it directly.

## Layout

| File | What |
|---|---|
| `platform/CRuntime.h` | every C library entry, in `namespace Crt` (the names are our DLL's runtime's too) |
| `platform/CRuntimeInternal.h` | what the runtime's files share: the per-thread block, errno values, the character classes, the float conversions' interface, the exit list for the shadow |
| `platform/CRuntime.cpp` | the heap, `_onexitinit`, atexit, the locks, `_amsg_exit`, `_exit`, rand, errno, the character classes, localeconv, qsort, the x87 helpers, `_fpmath`, `_clearfp` |
| `platform/CRuntimeFormat.cpp`, `CRuntimeParse.cpp` | the printf family over `_output`; sscanf over `_input`, atof, atol, strtod, strtol and its forms |
| `platform/CRuntimeFloat.cpp` | the 12-byte long double: its arithmetic, the power-of-ten tables, text to long double (`__strgtold12`), long double to double and float (`__ld12cvt`), double to digits (`$I10_OUTPUT`), the `%e/%f/%g` layouts |
| `platform/CRuntimeStd.h/.cpp` | the C++ library (`namespace GameStd`): the locks, `_Mutex`, `_Init_locks`, `_Init_atexit`, the locale and facets, `_Locinfo`, ctype's tables, ios_base, strstreambuf, the strstreams' destructors, `_Stod`/`_Stof`, std::exception, bad_cast, runtime_error, and (`namespace Crt`) the throw and the `eh vector` iterators. Members of the stream classes in data/StdStreams.h (Mutex, Exception, Locale, Facet, IosBase...) are defined here |

The globals only the runtime touches are ours: the exit list (0x0024b5b0/0x0024b5b4), doexit's flags (0x002448f4,
0x002448f8), the new handler and its mode, `_adbgmsg`, `__lconv` (0x001d8874, still pointing at the game's table),
`_pctype` (0x001d8a00, likewise), the process heap's handle (0x0024b218), and the C++ library's state: `_Init_cnt`
(0x001d8720), the facet list, the global and classic locales (0x0024482c-0x00244840), `_Stdstr` and `_Stdopens`
with the slots past them (0x00244854, StandardStreamTable), `_Atcount` and `_Atfuns` (0x001d8770, 0x002448ac), the
locale handles and code page (0x00244ac0, 0x00244ad8). Since the standalone executable (14 October 2026) the rest are ours too: the runtime's lock
table and its static critical sections (0x001d8de0, 0x00244920) and the library's four locks (0x002447b8), shared
with the shadows' originals (tools/owned_globals.py's SINGLE_COPY: they take our locks), `__lconv`'s table
(0x001d8844), `_aexit_rtn` (0x001d8b50), and the exception-action table the per-thread block points at (0x001d92b0,
`_XcptActTab`, which the old notes called multibyte information: `_freeptd` frees the field unless it is that
table). `__decimal_point` (0x001d8a0c) was already ours.

## The startup and the exit

`mainXapiStartup` (XboxStartup.cpp) stands in for `_rtinit` and `_cinit`: of their tables it runs what still
matters, by name - the per-thread data (Xbox_mtinit, the original's `_mtinit` moved onto a Win32 TLS slot),
`_fpmath` and `_onexitinit` - and leaves out the rest (see the deviations). `_fpmath` sets the x87's precision to 53
bits (`_controlfp(_PC_53, _MCW_PC)`, the other control bits as they were) and clears its exceptions. The exit list
`_onexitinit` makes (32 entries, grown by atexit as the original grows it) is never run: only `exit()` would, and
nothing calls it. The one way out is the fatal stop: `_amsg_exit` writes its banner and message to the debugger and
calls `_exit` through `_aexit_rtn` (0x001d8b50), which takes the exit lock, sets doexit's flags and calls the
kernel's KeBugCheck(0).

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_CRUNTIMESHADOWA` | injection (`CRuntimeShadowA_RunHeap`) and the first tick (`CRuntimeShadowA_Run`) | the heap: an allocation script (every awkward size, overflowing callocs, reallocs, frees, `_msize`) on a fresh heap lent as the process heap to both sides, compared by offset, size and zeroing. Then: every format string our code passes and random ones over every conversion, flag, width, precision and size with edge values (0, -0, denormals, the largest doubles, halfway ties, INT_MIN, NaN, infinities, long strings, precisions over 163), through sprintf, vsprintf and `_vsnprintf` at short counts; every numeric token in the driving archives' text files and random numerals through atof, strtod, atol, strtol and its forms and sscanf (the result's bits, the end, errno, the count); qsort on arrays with many ties, every comparison recorded; rand on two threads; atexit on lent lists, full and not; the character classes over -128..256; localeconv, errno, the x87 helpers (the 80-bit answer), `_clearfp` |
| `NIGHTFIRE_CRUNTIMESHADOWB` | the first tick | the C++ library: `_Lockit`, `_Mutex`, `_Init_locks`, `_Init_atexit`, `_Getfacet`, locale() and the first `locale::_Init`, `_Register` and the exit functions, `_Locinfo`, `_Getctype`, `_Getcvt`, `_Tolower`, `_Toupper`, ios_base's `_Init`, clear (with and without throws), `_Addstd` and destructor, strstreambuf's `_Init` and destructor, ostrstream's constructor and destructor, istrstream's destructor, `_Stod`/`_Stof` on random numerals with powers of ten, std::exception, bad_cast, the throw, `_Xran`, `_Xlen`, uncaught_exception and the `eh vector` iterators; each described by the object, the library's globals, the kernel critical-section calls made and the callbacks |

Each shadow swaps the originals back in for their side (common/xbeOriginal.h). The globals the integration made ours
are set by the shadows on both copies, ours for the port and the game's for the original, which nothing lends
(tools/owned_globals.py's SKIP): shadow B names each through the side running, shadow A the exit list and the
process heap. Before the integration, headless harnesses of both packages (806,118 cases for A, 7,729 for B) found no
differences. The other shadows that hook the runtime's rand (AudioMgrShadow, CoreLoopShadow, GeomShadow,
RenderFxShadow) or printf hook our entry too, since our callers call it directly.

In game: everything above runs from the first frame - the tuning, ini, CARP and attribute parsing (atof, sscanf,
the streams' `_Stod`, atol), the debug variables' ostrstream, the per-frame rand of the simulation and the sound
thread, the AI's and Targeting's qsorts, the heap under EAGL, the static initialisers' atexit and `_Init_locks`, the
startup's `_fpmath`. The lockstep runs (all eight missions, exact frames and sound traces) are the test that matters:
a parse one ulp off, a rand drawn on the wrong thread or a sort in another order shows there. Only the shadows reach:
the throws (nothing the game does throws; the originals' reach the loader's RtlRaiseException), the fatal stop, `_vsnprintf` past its count, the exit list's growth past
32 entries, standard streams.

## What the port taught

(To come.)

## What the testing taught

The first rounds (crt1, crt2) had all 16 lockstep runs exact and shadow A clean, but shadow B reported about 15
differences in `ios_base::_Init` and one in `ostrstream` per mission, and then no shadow run reached its frame dump:
the first throw by an original stopped the game. Every difference was the harness's or the loader's; the port needed
no change.

- **The throws (the loader).** The original `_CxxThrowException` calls XAPI's RaiseException (0x0010ea87), which hands
  its record to the kernel's RtlRaiseException (ordinal 302). The loader had no ordinal 302, so its stub reported
  "unimplemented kernel import ... called from 0x0010ead8" and ended the process. src/loader/kernel.cpp now passes
  the record, unchanged, to ntdll's RtlRaiseException; the game's own frame handlers (`__CxxFrameHandler`
  0x00131dc7) then unwind their frames when shadow B's handler catches the exception. Shadow B compares every throw
  case in game.
- **Originals reaching ports (`ios_base::_Init`).** The original `_Init` calls `locale::locale` (0x001310bf) by
  address, and that is our port there, which reads our global locale rather than the case's (lent to the game's copy
  only): the original's locale came out the live heap one and the case's reference count stayed put. The case swaps
  the original `locale::locale` in as well.
- **Freed memory (`ostrstream`).** The case described the buffer's locale after the destructor had freed it
  (`~basic_streambuf` deletes it and leaves the pointer), so each side read whatever the heap had left there. It now
  reports only that the pointer is left.
- **The allocator's locks (`ios_base::clear`).** Once the throws ran, one case in 256 showed an extra
  critical-section enter and leave on the original's side: the memory class a string copy (0x000136d0, ours) took its
  block from was a locked one, which depends on the heap's state, not on the code compared. Shadow B records only the
  library's four locks and sections set up during the side (a `_Mutex`'s).
- `NIGHTFIRE_CRUNTIMESHADOWB=3` names each case before it runs, which found the last one (case 641).

The final round (crt3, on the runner, 13 October 2026): all 16 lockstep runs exact against the runner's baselines,
shadow A clean (498 heap cases; 806,118 others), shadow B clean (7,729 cases, the throws among them) on every
mission, and every shadow run reached its frame dump, apart from mission 5's once: SndStreamFileShadow's known case 52
went on to fault on both sides and the game stopped there; run again, mission 5 finished clean. (That case, and
SndSystemShadow's case 0, were harness problems, fixed since: sound.md 9.6.)

## Known deviations

- The startup leaves out four of the runtime's initialisers: low-level I/O's handle table (`_ioinit` 0x00139acd,
  0x500 bytes from the heap), stdio's stream table (`_initstdio` 0x0013594c, from the heap too), the multibyte code
  page (`__initmbctable` 0x00139a7b) and the C++ unhandled-exception filter's installation (0x001363ba) with the
  terminator that removes it (0x001363cc). Their state is read only by the runtime's stdio, low-level I/O and
  multibyte functions, which nothing runs, and by XAPI's thread start, which our startup does not use. The process
  heap so lacks two early blocks; its addresses differ from run to run anyway (a Win32 heap). `_fpmath` leaves out
  `_cfltcvt_init` (0x00131cf0): the conversion pointers it sets are read only by the runtime's own printf and scanf
  cores, and ours call the conversions directly.
- `_exit` does not walk the terminator table: its one entry was that filter's removal.
- `_Atexit` (0x00131cb0) ends in `_amsg_exit(10)` where the original calls abort (0x00135245) when its ten slots are
  full; only two functions are ever registered.
- `_NMSG_WRITE` (0x001392ac) compares one entry past its message table for a code not in it; every code passed is.
- The `eh vector constructor iterator` (0x0013326e) would destroy the elements made so far if a constructor threw;
  none does, and our C++ has no unwinding.
- The debug output (printf 0x00132192, the debug channels' devices) is the host's: the text a debug line formats is
  the game's (the channels' and the loading screen's formatting go through the port), how it is written is not.

## Odd things in the original, kept

- The character classes (isdigit, isalnum, isspace, tolower) index `_pctype` with whatever int they are given, so a
  value outside -1..255 reads past the table; the C++ library's `_Tolower` and `_Toupper` ignore the table they are
  handed and test the C locale's classes; setlocale knows only "C".
- malloc rounds to 16 bytes and calloc and realloc do not (`__active_heap` is 0, neither of the values their tests
  look for); the mutex's allocation is not checked; a failed copy of an exception's message leaves no message but
  still one to free.
- `__ld12cvt` rounds a tie down; `%e` prints three exponent digits and `%p` eight upper-case hex digits; 'L' is
  accepted and ignored; a float precision over 163 digits takes a buffer from the heap; `_vsnprintf` writes no null
  when the text fills the count exactly; atol wraps; strtol leaves the end at the text when no digit came.
- qsort's order of equal elements, and its comparisons, are its own.
- `_CIacos`, `_CIasin` and `_CIpow` answer on the x87 unrounded, through FPATAN, FYL2X and F2XM1 as the runtime has
  them; `_Stod` keeps its value at the x87's exponent range through the scaling loop (a power of two here) and rounds
  it once, at the caller's store.
- `_Addstd` does not stop at eight standard streams: a ninth is written to the slots after `_Stdstr` and `_Stdopens`.
- Values that never change: srand is never called (each thread's rand starts at 1), the new handler and its mode
  stay 0, the locale handles and code page stay the C locale's, the exit list is filled and never run, `_exit`
  ignores its code.

All of these are in docs/driving/original-quirks.md with their addresses.
