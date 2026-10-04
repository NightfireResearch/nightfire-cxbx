# The driving engine's core and data layers (engine.core, engine.data)

The bottom of the game's own engine tier: what every gameplay system stands on. Ported on 3-4 October 2026, after
the platform tier below it (EAGL, the sound library, files, maths, memory blocks, XAPI) was already ours. Every
address is Driving.xbe's (Ghidra program `/Xbox_EU/Driving.xbe`); names are Ghidra's, or the PS2 build's
(`/PS2_EU_51258/DRIVING.ELF`) where Ghidra's Xbox name was missing or wrong, or marked invented in the source.

**Status: done.** `engine.core` 211 of 213 functions replaced or dead, `engine.data` 529 of 554; 100% of both by
bytes. The few left live are exception-handling funclets of functions in other subsystems that sit, by address,
among these (`tools/function_coverage.py --driving --why engine.core engine.data`). Checked by shadow tests against
the originals (below) and by lockstep runs of missions 1-8, every dumped frame identical to the pre-port baseline.

---------------------------------------------------------------------------------------------------------------

## 1. engine.core

### Memory (`src/driving/engine/UMemory.hpp/.cpp`)
`UMemory` is the game's allocator front end over EA's MEM library (`src/driving/platform/RealMemory.cpp`). Every
allocation goes through a 12-slot function table (`MemoryFunctions`, at 0x001d4894 after `UMemory::Init`; two
earlier tables are used before it): `Alloc`/`Free` (zero-filled, named, class-numbered blocks), the fixed-size
pools behind `FastAlloc`/`FastFree` (the sized delete every deleting destructor calls; `FastPool`, carved from
0x4080-byte "FastBlock"s), `NewClass`/`DeleteClass`, and the game's `operator new`/`delete`/`new[]`
(`__builtin_new` etc., renamed `OperatorNew`... because clang reserves `__builtin_*`). Allocation sizes, names,
fills and order are the original's exactly: lockstep testing depends on an identical heap.

### Containers and utilities
- `URefCounter.h/.cpp`: the game's named reference-counted maps, one compiled copy per type (models, textures,
  weapons, CARP files, texture contexts, audio mix/stream/fader/bank/engine). Written once (`URefCounterMap`, a
  thin `URefCounter<T>`) with a plain entry class per instantiation.
- `UGroup.h/.cpp`: data groups - the tagged, nested, offset-relative containers every CARP and gallery file is
  made of (lookup by tag, count by type, breadth-first processing, offset resolution, deserialisation).
- `USingleton.h/.cpp`: the singleton manager that resets and kills the game's managers at start-up and clean-up
  (`USingletonManager`, the function-local static at 0x001e47c0 that `SingletonManager()` makes, with its vector's
  push_back/_Insert_n/_Xlen and the base class's deleting destructor).
- `RbTree.h`: the layout every `std::map`/`set` in the engine shares (Dinkumware's `_Tree`: 12-byte tree, head
  node, links/value/colour/isNil nodes). The algorithms stay with their owners - the core's compiled copies
  (`CoreContainers`), the reference counters (`URefCounter`), the data layer's name and resolver maps
  (`data/Tree.h`) and the attribute system's seven trees (`data/AttributeContainers`) - because each copy calls
  different compiled helpers.
- `CoreContainers.h/.cpp`: compiled `std` map/set/vector instances the game uses from many places.
- `CoreFoundation.h/.cpp`: the video-mode accessors, `AssertMessage`, the empty stub functions, the STL's
  length_error throw and the `[core]` untested warning.

### Scheduler, clock, randomness (`src/driving/Scheduler.cpp`, `Schedule.hpp`, `engine/SimRandom.cpp`)
The scheduler runs the game: four schedules (per frame, sim rate, half and quarter rate), each with priority
buckets of tasks registered by event type. `Scheduler::Run` keeps its one documented departure (the carried tick
fraction, so slow and fast motion run at the rate asked). `SimRandom` is the deterministic generator the AI uses;
`Noise` the gradient noise behind camera shake, particles and weapons (`Noise1` returns the unrounded double the
original left on the x87 stack).

### The game loop (`src/driving/engine/GameLoop.cpp`)
`main` (`GameMain`, called from `preMain`), `Bond_StartUpSystem`/`CleanUp` (memory, singletons, tuning and
attribute databases, the loading screen), `GameLoop_StartUp`/`MainGameLoop`/`CleanUp`, `RunTheGame` (intro movie,
the mission, outro movie), the launch page shared with the action engine (`LaunchPage`: mission hand-over,
`ReturnToAction`), `OptionParser`, the disc-error screen.

## 2. engine.data

### Tuning files (`src/driving/data/Tuning.cpp`, `DebugVariables.cpp`, `StdStreams.cpp`)
Systems open a tuning database with `DTuningDBMgr::LoadDatabase("Render:Fog", level)`, which reads
`data\tuning\Render\Fog\<level>.tun` (or `default.tun`) from the mission's archive, then name each value with a
`dbattrib_*` call that finds its "name value" line and parses it through the C++ library's `istrstream >> T`.
On the PS2 each call also built a debug-menu variable; the Xbox keeps only `dbindex`'s index variable. More than
half of this range is the MSVC 7 (Dinkumware) stream library compiled into the unit - string streams, number
parsing and formatting, facets - ported faithfully because it is what turns tuning text into values
(`namespace GameStd`). `IniFiles.cpp` reads `data/render/camera.ini` through DAFI for the camera loader.

### The attribute system (`src/driving/data/Attribute*.cpp`)
Named, typed tuning values the game's classes look up by key: vehicle handling (a 0x100-byte car physics
struct), smackable props, per-mission world settings, sentry guns. `data\sim\attrib\attrib.dir` (in each
mission's archive) lists every collection; each is an INI file `data\sim\attrib\<class>\<name>.atr` read with
DAFI the first time it is used, in three passes (the track's section, the collection's own, `[default]`). Keys are
`KEY[.b|.i|.u|.f|.v|.m|.s|.r]=value`; a key registered as an extension field is parsed straight into the
collection's struct. A lookup that misses falls back to the class's "default" collection. Seven `std::map`/`set`
instantiations and a sorted string store sit underneath.

### CARP level data (`src/driving/data/Carp.cpp`, `RCARPFile.cpp`, `SymbolTable.cpp`, `UData.cpp`)
CARP files (`.crp`) are data groups holding a world's or object's data. `RCARPFile::Resolve` adds the file's
groups to the "CARP" namespace, opens its texture files as "TEX0".."TEX9", builds the EAGL model loader as the
"EAGL" namespace, then `CARP::ResolveSymbolicReferences` makes two passes: 'rs' records' names are looked up in
the symbol table and swapped in, and each record type with a resolver has its tag references turned into
addresses (in the parent group, then 'Shar'; -1 means the parent). `USymbolTable` is a case-insensitive multimap
of namespaces ("NS::rest"); names it cannot answer go to `RegisterCallback`'s slots (`RegisterSymbols` maps
"GAME::"/"EAGL::" names to the renderer's live data). `PathInfo` evaluates the AI spline paths (linear, spline,
matrix; bit-exact x87).

### DAFI (`src/driving/data/Dafi.cpp`)
The INI parser under the attribute system, `IniFiles`, the loading screen, the camera loader and the sound
manager.

### File loading (`src/driving/engine/UFileLoader.cpp`)
The game's file front end over FILESYS: the mission archive (`StartUsingBigFile`), existence and size queries,
loads into a given buffer, shape loads, the request log. With `DumpFiles=on` in settings.ini (off by default; the
action engine reads the same key) `FileLoad` also saves each file it loads under `dump_driving\`
(`src/driving/devtools/FileDump.cpp`).

## 3. Tests

Each shadow test runs the original and the port side by side on the same inputs and compares byte for byte; all
are off unless their environment variable is set.

| Variable | Where it runs | What it covers |
|---|---|---|
| `NIGHTFIRE_COREUTILSHADOW` | injection time | UMemory (6000 random operations in a scratch arena), every URefCounter instantiation, the containers, UGroup over every data-group file in the archives |
| `NIGHTFIRE_CORELOOPSHADOW` | injection time | schedule scripts (task add/remove/run, callbacks that change the lists mid-run), scheduler init/reset/shutdown, SimRandom, Noise (bit for bit), MissionNumToString, OptionParser |
| `NIGHTFIRE_CARPSHADOW` | first simulation tick | DAFI over every `.atr`/`.ini` (shipped and perturbed), symbol tables, StringToNumber, every `.crp` resolved both ways and compared whole, AI spline evaluation |
| `NIGHTFIRE_ATTRIBSHADOW` | first simulation tick | the parsers, values, trees, string store, and the disc's whole attribute database (162 collections) built side by side and compared |
| `NIGHTFIRE_DBVARSHADOW` | first simulation tick | the stream library's parsing and formatting, every `dbattrib_` type on every key of every `.tun`, LoadDatabase, IniFiles |

The first-tick shadows are called from `Teleport_Tick`'s first call (`src/driving/devtools/Teleport.cpp`): the
mission archive is closed by then, so the ones that read it reopen it around their file tests.

`NIGHTFIRE_RESTORE_RANGES=lo-hi[,lo-hi...]` (`src/driving/devtools/RestoreRanges.cpp`) puts the originals back
in those address ranges at start-up - how the one real crash of this port (a misread rule-type table in the CARP
'Rule' resolver, which crashed missions 1 and 6) was bisected to its function without rebuilding.

## 4. Departures and kept quirks

Departures, none observable: a few bytes the original left as stack garbage are zeroed (padding in task records,
key buffers past the terminator in reference counters, three bytes above an inline bool in attribute values,
missing colour channels in `ParseData_Colour`); handlers registered with the platform point at our functions; two
functions that returned the address of their own dead stack frame on paths the disc never takes return a static
buffer instead.

Kept as the original has them: `OptionParser` leaks a key copy in one case; DAFI loops forever on a key line
without '=' and takes the next line as an empty value's value; the attribute system never rereads a collection
whose last reference went; `DTuningFile` never frees its text; a few `delete`s that should be `delete[]`. Code no
shipped data reaches is ported but marked provisional (a one-time `[core]`/`[data]` untested warning): STL
length/iterator throws, iterator erases, matrix and symbol attribute keys, encrypted ini files.
