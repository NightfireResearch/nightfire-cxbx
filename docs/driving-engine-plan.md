# Driving engine: state of knowledge and project plan

Companion to `cxbx-removal-plan.md` (the action engine). Written September 2026 after a first survey of
`Driving.xbe` in Ghidra (`/Xbox_EU/Driving.xbe`, 8959 functions), the repository's driving code
(`src/driving`, `src/inject_driving.cpp`) and the PS2 symbol spreadsheet. Facts below cite addresses in the
Xbox EU build so they can be re-checked; nothing here has been verified at runtime yet.

Revised after the action engine stopped needing CXBX at all. Section 0 is what that changes, and it changes
enough that the order of work at the end is different: several items in the original plan were workarounds
for CXBX's behaviour, and are now worth skipping rather than doing.

## 0. What the standalone loader changes

The action engine now runs with no emulator in the process: `action.exe` (`src/loader/`) maps `default.xbe`
itself, resolves its kernel imports and runs it. Most of that machinery is not specific to the action engine,
so the driving engine inherits it. What follows is measured rather than assumed - `disc/Driving.xbe` was
surveyed statically for the numbers below, and they can be re-derived without opening Ghidra
(`python tools/survey_xbe.py disc/Driving.xbe`).

**What comes for free.**

- *The loader.* `Driving.xbe` has the same image base (`0x10000`) and is smaller than the action XBE
  (`0x244f40`, 2.3 MB, against `0x2fb660`), so the reservation array in `src/loader/reserve.cpp` already
  spans it. Loading it should be a matter of pointing the loader at a different file.
- *The dual-header trick.* The loader keeps its own PE headers and the XBE's headers both live at `0x10000`
  (see the action plan, 4.3). That is generic; the driving engine reads its own header the same way.
- *Kernel imports.* 100, against the action engine's 96, and the same file/event/timer/memory shape. The
  twelve implemented in `src/loader/kernel.cpp` are shared, and anything missing announces itself by name
  and by the address that called it instead of crashing.
- *Diagnostics.* The loader's fault handler prints the faulting address, what it touched, and a call stack
  walked from the frame pointers. That is most of section 4's proposed "crash capture" already built, and it
  wants only symbolisation from `tools/functions_driving.json` to be exactly what that section asked for.
  `tools/drive_game.ps1` drives the menus with synthetic input to reach a level unattended.

**The FS-segment problem is the same size here, which was not obvious.** Game code reaches the Xbox KPCR
through FS, and a Win32 thread has a TEB there instead; this is the one genuine incompatibility in the
action engine's startup. `Driving.xbe` makes 2987 FS accesses against the action engine's 40, which looks
alarming until they are broken down by offset:

| Offset | Meaning on Xbox | Driving | Action | Status |
| --- | --- | ---:| ---:| --- |
| `+0x00` | SEH exception list | 2956 | 10 | identical on Win32 - **nothing to do** |
| `+0x04` | the TLS array | 6 | 6 | the incompatible one; Win32 keeps the stack base here |
| `+0x20` | `KPCR.Prcb` | 8 | 6 | Win32 has the process id |
| `+0x24` | current IRQL | 7 | 8 | Win32 has the thread id |
| `+0x28` | current `KTHREAD` | 8 | 8 | Win32 has `ActiveRpcHandle`, which is free |
| `+0x58` | (unidentified) | 2 | 2 | dormant in the action engine |

So the whole difference is `FS:[0x00]`, which is a C++ codebase using structured exception handling
everywhere and costs nothing. **The part that needs work is 31 sites against the action engine's 30** - the
same handful of functions, and the same fixes should apply: replace the five or so functions that read
`FS:[0x04]` rather than trying to repoint it (Win32 validates every SEH frame against the stack base kept
there), and patch out the `FS:[0x20]` notification hook. Budget a day, not a month.

**The hazard: standalone, DSOUND drives real hardware.** This is the one thing that is strictly harder
without CXBX, and it is worth understanding before starting. The XBE statically links Microsoft's DirectSound,
whose lower half programs the MCPX audio registers at `0xfe80xxxx` and spins on them. Under CXBX those calls
were replaced wholesale by CXBX's HLE. Standalone they run for real against unmapped memory, so **any**
DSOUND call that is not intercepted faults or hangs - and a zero-filled page would hang rather than fault,
because the waits are `do {} while ((reg & ~3) < 4)`.

The action engine hit this through exactly one gap: the XMV decoder calls `DirectSoundCreateStream` directly
rather than through any game function the seam had replaced, so it reached the real library. The fix was to
hook those entry points at their own addresses (`src/action/sound/dsndStream.cpp`).

For the driving engine this is not an edge case but the main event: 63 DSOUND entry points under EA's `SND`
layer. The seam has to be complete before the engine will boot standalone at all, which moves audio from
"section 6.2, after graphics" to "the thing that decides whether anything runs". The good news is that the
`SNDPLATFORM_*` boundary is above DirectSound, so a complete seam there means no DSOUND entry point is ever
reached - and the action engine's XAudio2 backend, including the stream implementation, is already written
and is reusable.

**Two smaller things to expect,** both already solved once:

- *Privileged instructions.* An Xbox title runs in ring 0. `WBINVD` (cache flush before the GPU reads memory)
  raises `0xc0000096` on Windows; the action engine had three, patched to nops because nothing reads game
  memory behind its back any more. Expect the same, plus possibly `CLI`/`STI` in the EA sound driver thread.
- *The physical-memory alias.* `0x80000000 | address` is the Xbox's uncached view of RAM. The action engine
  funnels every use through one helper that decides once, by probing whether the alias is mapped, whether to
  apply it; do the same here rather than scattering the decision.

**Naming.** The CXBX-hosted launchers are `action_cxbx.exe` and `driving_cxbx.exe`; the standalone loader is
`action.exe`. `driving_cxbx.exe` carries the suffix even though its standalone sibling does not exist yet,
because the suffix is what says it still needs an emulator - a plain `driving.exe` sitting beside a
standalone `action.exe` would quietly imply otherwise. `driving.exe` is reserved for the standalone build
when there is one. The injected DLLs keep their plain names (`actioninject`, `drivinginject`): they are not
specific to a host, and `actioninject.dll` is already loaded unchanged by both.

**And one correction to section 6.3 below:** the action-to-driving hand-off cannot become an in-process
transition. Both XBEs are linked to base `0x10000`, and the loader gets that address by *being* the image
there - so only one XBE can be mapped at a time, and that is not a limitation a cleverer loader removes. The
hand-off stays a process relaunch: the loader re-executes itself with the other XBE, carrying the launch
data page across in a file, which is what `psiLaunch.bin` already does.

## 1. What the driving engine is

- A separate XBE (`Driving.xbe`, 1.9 MB, image base `0x10000`, `.text` `0x11000`-`0x15d370`), built from
  `D:\ToBurn\BondXbox\Final\BondXBOX.exe` (string at `0x10682`). The PS2 map comes from the sibling tree
  `D:\ToBurn\BondPS2\PS2_EE_Release`, so the two builds share source and, importantly, **link order**.
- It is EA's **EAGL** engine (EA Graphics Library, the Burnout / Need for Speed lineage; strings
  `EAGL::Device new`, `EAGL::ViewPort::gpModelViewProjectionMatrix`, `eaglrm.o` = render methods) with the
  Bond game layer on top: `PBondCar`, `SMissionManager`, `AIGroundVehicle`, `RPlayerCamera`, `GHud`,
  `WRoadNetwork`, `Simulation`, `PhysicsObject`, `RigidBody` and so on. Reburn3 (the project this repository is
  modelled on) targets a later EAGL, so its findings on EAGL structures are worth cross-reading.
- Statically linked libraries by XBE section: `D3D` (108 D3D8 entry points), `D3DX` (matrix helpers and a JPEG
  decoder), `XGRPH`, `DSOUND` (63 entry points), `XPP`, `DOLBY` (Dolby Digital encoder; irrelevant on PC),
  plus XAPILIB (39). Kernel imports: 100 (`list_imports`), the usual file/event/timer/memory set.
- Audio is EA's own `SND*` library on top of DirectSound (`SNDPLATFORM_init`, `SNDDRV_thread`, `SNDBANK_play`,
  `AMix`, `ASoundManager`, `AStream`), running its own driver thread (`THREAD_create` -> `CreateThread`).
- Startup: `entry` -> XAPI -> `main` (`0x5a1b0`): parses `-ntsc/-pal/-pal60/-T<track>` style arguments,
  `Bond_StartUpSystem`, `GameLoop_MainGameLoop` -> `RunTheGame` (`0x5aa80`), then `ReturnToAction` (the
  `XLaunchNewImage` hand-back). `ApplicationMemoryHeapConfig` (`0x59920`) reads launch info, sets the video
  mode, starts the timer at the video refresh rate and creates a 36 MB `UMemory` heap.
- Main loop (`RunTheGame`): tasks are registered on two schedules, `s_SimRate` (`ESimFrameUpdate`,
  `EAnimUpdate`, `EAIUpdate`, `ESimEndFrame`, `ECameraUpdate`) and `s_oncePerGameLoop` (`ERenderFrame`,
  `EAudioUpdate`); then `while (Sim.state != 2) { drain ActionQueue; SYNCTASK_run(0); Scheduler::Run(); }`.
  There is no sleep or vsync wait in the loop itself.

## 2. Timekeeping, and why it misbehaves under CXBX

This is the mechanism behind the "slowdowns / timekeeping errors" and it is fully understood now:

- `Timer_Init(freqHz)` (`0x10ae50`) calls `timeSetEvent(1000 / freqHz, ...)` with `freqHz` = the video
  refresh rate (50 or 60). The callback `TIMER_ontick` (`0x10ae10`) runs a task list that includes
  `RealClock_InterruptHandler` (`0x5b9f0`), which does `Clock++` (`Clock` at `0x1e5204`) and a divide-by-2
  counter. So `Clock` is meant to be a 50/60 Hz wall-clock tick delivered from a timer thread.
- `Scheduler::Run` (`0x5ba80`) computes `dt = (Clock - lastTickCount) * timeScale`. If `dt` is 0 it does
  nothing; if `dt < 12` it runs every schedule once per elapsed tick (8 priority passes each) and the
  once-per-loop schedule on the last tick; **if `dt >= 12` it skips simulation entirely** and just resets
  `lastTickCount`. (The commented "weird 12" in `inject_driving.cpp` is this stall guard.)
- Consequences under emulation: CXBX implements `timeSetEvent` with a host timer whose granularity and
  jitter are worse than the Xbox's; ticks arrive in bursts, so the simulation alternates between doing
  nothing and catching up several ticks per frame, and any frame longer than 12 ticks (200 ms at 60 Hz,
  common during loads or CXBX hitches) drops time on the floor.
- The repository's current workaround (`src/driving/Scheduler.cpp`, injected over `0x5ba80`) ignores `Clock`
  and runs exactly one simulation tick per loop iteration, which trades jitter for a game speed tied to the
  frame rate.

**Standalone this changes shape, and for the better.** `timeSetEvent` here is XAPI's, statically linked in
the XBE, and it is built on the Xbox kernel's timers - so standalone it lands on the loader's kernel
implementations, which are ours to write. The tick source becomes a Win32 waitable timer or a
`timeBeginPeriod(1)` thread of our choosing, with no emulation in between, and the jitter this section
blames CXBX for should simply not be there. Confirm that before patching anything in the game: if a native
`KeSetTimerEx` delivers ticks evenly, the scheduler's original semantics may need no change at all, and the
current workaround in `src/driving/Scheduler.cpp` can be deleted rather than replaced.

If it does still need work, the fix below stands.

Proposed fix: replace `Timer_Init`/`TIMER_ontick`/`RealClock_*` with our
own implementation that derives `Clock` from the host's `QueryPerformanceCounter` (ticks elapsed at 1000/freqHz ms,
sampled when `Scheduler::Run` is entered, or a `timeBeginPeriod(1)` thread if other timer tasks need
callbacks), and restore the original `Scheduler::Run` semantics with the catch-up capped (for example run
at most 4 ticks per loop and never drop time). Then pace the loop: either sleep to the next tick edge in
`Scheduler::Run` when `dt == 0`, or rely on Present pacing once the D3D9 backend is in use. Keep the
`timeScale` and cinematic-skipping paths intact. Note *the host's*: the XBE has a function of that name
too, and it is the one at fault - see 2.1.

### 2.1 The clock runs fast standalone, and it is not emulation's fault

Found in September 2026 while chasing a glow that pulsed at the wrong speed in the action engine. It applies
here unchanged, and it is the kind of thing that will be misattributed to the section above if you meet it
cold.

The console's CPU clock is baked into both binaries. Two places read the cycle counter and convert it using
733.333 MHz, the Xbox's own speed:

- `timestamp()` (action `0x000e8f20`) computes `rdtsc * 3 / 2200 * 0.001` to get milliseconds. 2200/3 is
  733.333.
- `XAPILIB::QueryPerformanceCounter` returns the raw counter, and the `QueryPerformanceFrequency` sitting
  immediately after it returns the literal `0x2bb5c755` = 733,333,333.

On hardware the pair is self-consistent. Executed on a host CPU, `rdtsc` returns the host's counter while
the divisor still insists the machine is a 733 MHz Xbox, so every interval derived from it passes too fast
by the ratio of the two clock speeds - measured at **6.41x** on a 4.7 GHz part, and a different number on
every machine it runs on.

CXBX never showed this, because it rewrites every `rdtsc` in the image and emulates it at the Xbox's rate
(`Cxbx-Reloaded/src/core/kernel/support/PatchRdtsc.cpp`). The standalone loader executes the instruction
natively. **This is a decoupling regression, not an emulation artefact, and it will appear in the driving
engine the moment it stops going through CXBX** - as timing that runs fast, which is exactly what section 2
above teaches you to blame on CXBX's timer jitter. Do not make that attribution by reflex.

The driving binary carries the same code. Byte-identical searches of the two XBEs on disc:

| | action `default.xbe` | `Driving.xbe` |
|---|---|---|
| `QueryPerformanceCounter` body | 1 | 1 |
| `QueryPerformanceFrequency` body, with the 733,333,333 literal | 1 | 1 |
| raw `0f 31` byte sequences | 5 | 13 |

`XAPILIB::QueryPerformanceCounter` is at `0x0014bee0` in the driving symbols. The raw byte counts are an
upper bound rather than a site count - some `0f 31` runs are data, which is why CXBX's patcher carries a
false-positive filter - so expect fewer real sites than 13, but more than the action engine's three.

**What was done in the action engine, to copy rather than rediscover.** `timestamp()` and the
`QueryPerformance*` pair were replaced with injected versions built on the host's own
`QueryPerformanceCounter`/`QueryPerformanceFrequency` (see `src/action/game.cpp`, which carries the full
reasoning). Three things worth carrying over:

- The counter pair is patched **by address** (`FUNC_AT`) rather than by name, because the names collide with
  the Win32 functions being called inside them.
- Replacing *both* halves is what matters. Consistency between them is the only property the callers depend
  on - none assumes a particular frequency, they all ask for it. The action engine's caller of record is the
  XMV video decoder, which stores frequency/1000 as ticks-per-millisecond at creation and divides counter
  deltas by it to decide when each frame of a background movie is due.
- Verify by measurement, not inspection. The action engine's check was the game's own `psiGetTimeIn100ths`,
  which should advance 100 per second: it read 641 before the fix and exactly 100 after.

The action engine's third `rdtsc` site, a bare wrapper used by the XBE's NV2A driver to timestamp vblank
interrupts and predict the next one, needed no fix - that layer talks to real graphics registers and never
runs behind a native backend. Expect the same to be true of the driving engine's equivalents, but check what
each site is for before assuming it.

## 3. Lens flares

`RLensFlareManager::TestFlares` (`0x9e720`) draws a 16x16 test quad per flare inside an NV2A visibility test
(`FUN_000e7c60`/`FUN_000e7c80` wrap `D3DDevice_BeginVisibilityTest`/`EndVisibilityTest`, index 0..15
ring). `DrawFlares` (`0x9e540`) spins on `D3DDevice_GetVisibilityTestResult` until the result is ready and
computes intensity as `(visiblePixels - 256) / 256`, so it assumes a 256-pixel quad at native resolution.

Under CXBX the visibility test is answered with a host occlusion query at the host's render resolution, so
the pixel count scales with the render-scale squared and the flare's brightness/size explodes. The current
patch NOPs the `DrawFlares` call. Two fixes, in order of effort:

1. In a native D3D9 backend, implement the visibility tests with `D3DQUERYTYPE_OCCLUSION` at the game's own
   resolution; the count is then exact, and the spin-wait becomes a `GetData(FLUSH)` wait.
2. In CXBX mode, hook `FUN_000e7ca0` (the result wrapper) and divide the count by (host width / 640) x
   (host height / 480), read from the present parameters. Restores flares immediately - but this is a
   correction for an emulator that is being removed, so do it only if CXBX-hosted driving has to look right
   in the meantime. These two were the other way round before the standalone loader existed.

Also note the spin-wait itself: with a slow emulated query it stalls the frame; that is one of the
"performance" symptoms.

## 4. The crash: DirectSound buffer pool exhaustion in the EA sound layer

Earlier debugging traced the driving-level crash to a NULL pointer inside the audio system, and NULL checks
added to a custom CXBX build did not help. The static reading explains both: the NULL is manufactured and
dereferenced in the game's own EA `SND` layer, above anything CXBX can guard.

How the layer works (`SNDPLATFORM_init`, `0x13dc50`):

- At start-up it creates **180 DirectSound buffers** in two pools: 152 in pool 0 and 28 in pool 1
  (`NUM_SND_BUFFERS1/2`, list heads `LList_maybeFreeDsndBuffers[2]`, active lists
  `Llist_MaybeActiveDsndBuffers[2]`, each via `dsndCreateBufferAndMixBins`). Pool 1 is for looping/streamed
  timbres (`patchHeader->field18_0x13 == 20` selects it in `SNDPLATFORM_playtimbre`).
- A 100 Hz driver thread (`SNDDRV_thread`, `0x13d980`, paced with `getTickCount`/`SleepMilliseconds`
  under `SNDI_mutex*`) runs `SNDSYSI_100hzserver` -> `iSNDserve` (`0x13e530`), the only place that polls
  `IDirectSoundBuffer_GetStatus` and returns finished buffers to the free lists. `SNDPLATFORM_stop`
  (`0x13de50`) returns them on explicit stops.
- `SNDPLATFORM_playtimbre` (`0x142b10`) pops a buffer from the pool's free list for every platform voice
  of the timbre. If that list is empty it first calls `FUN_0013d550`, which **borrows from the other pool**:
  pops a node there, `Release`s its DirectSound buffer and creates a replacement of the wanted type.

The two NULL paths, both reachable only when the pools run dry:

1. If **both** free lists are empty, `FUN_0013d550` picks pool index `-1` and pops from memory in front of
   the array; the returned "node" is garbage or NULL and `node->dsndBufferObj` is dereferenced. This is the
   "all slots filled" case.
2. If the replacement `IDirectSound_CreateSoundBuffer` inside the borrow fails (CXBX's HLE can fail where
   the hardware never did), `node->dsndBufferObj` becomes NULL and the next `SetBufferData`/`Play` on it
   crashes.

Why the pools run dry under CXBX but not on hardware: buffers are only reclaimed when `GetStatus` reports
them stopped, from the 100 Hz thread. CXBX's DirectSound emulation reports playback status from a host
buffer whose position and completion do not track the Xbox's (the action engine's movie stutter had the
same root: emulated status/timing), and CXBX's thread scheduling can starve the 10 ms driver loop. Either
way finished one-shot sounds stay "playing", the free lists drain over minutes of play, and the crash
lands when a busy moment needs more voices than are left. The fact that it triggers "eventually" on
driving levels, not immediately, fits a leak rather than a hard limit.

What to do, in order:

1. **Instrument** (one afternoon): hook `SNDLINKI_pop` (`0x13f110`) / `SNDLINKI_push` (`0x13f0b0`) to log
   both free-list lengths once a second and on every pop that returns NULL; hook `FUN_0013d550` to log
   borrows and a NULL result from `dsndCreateBufferAndMixBins`; count `iSNDserve` iterations per
   second to see whether the driver thread keeps its 100 Hz. Add the generic crash logger below so the
   next crash names its function. If the free lists trend down over a level, the leak is confirmed.
2. **Contain**: make `FUN_0013d550` and the pop site in `SNDPLATFORM_playtimbre` fail soft -
   when no buffer is available, steal the oldest active buffer of the pool (stop it and reuse it) instead
   of borrowing from an empty neighbour, and treat a NULL from buffer creation as "voice unavailable"
   (`SNDVOICEI_free` the voice and return). This turns the crash into a dropped sound.
3. **Cure**: the native audio backend (section 6.2), where buffer status is exact and creation cannot fail.

**This diagnosis now argues for skipping straight to the cure.** Every symptom in this section is downstream
of CXBX reporting playback status from a host buffer that does not track the Xbox's, and the audio seam has
to be written anyway before the driving engine will boot standalone at all (section 0). Containment is a
fix for a host that is being removed. The instrumentation in step 1 is still worth having - it is how the
diagnosis gets confirmed rather than assumed, and it keeps its value against the native backend, where the
same free lists should simply never drain.

Performance and other crashes still need data:

- **Crash capture**: a vectored exception handler in the injected DLL that logs EIP, registers, the
  faulting address and a stack walk, symbolised from `tools/functions_driving.json` (nearest function
  below each return address), to `driving_crash.log`.
- **Sampling profiler**: a thread in the DLL that every 1 ms suspends the game's main thread, reads EIP
  (`GetThreadContext`), resumes it and histograms by function (same symbolisation). Dump the top 50 at
  level end. This answers "where does the time go" without host tools that cannot see XBE symbols.
- Candidate hot spots to expect: `D3DDevice_Begin`/`SetVertexData2f`/`4f`/`End` immediate-mode calls (per
  vertex HLE overhead), `D3DDevice_RunPushBuffer` (EAGL submits precompiled NV2A command streams - see 6.1),
  `BlockOnFence`/`IsBusy`/visibility-result spins, and the sound driver thread contending with CXBX's
  thread emulation.
- Other candidate crash sources, lower priority: the 36 MB `UMemory` heap (`UMemory::Init(0x2400000)`),
  async big-file streaming (`UFileLoader` request lists, `SYNCTASK`), and the `Event` buffer
  (`EventManager`, 32 KB ring at `0x1e47d4`) overflowing when the simulation catches up many ticks at once
  (the timekeeping fix in section 2 may remove that class by itself).

## 5. Symbols: making the driving binary readable

### 5.1 Current coverage

- Ghidra: 8959 functions; `tools/functions_driving.json` exports 7982 of them, 2118 with real names (the
  rest `FUN_*`). Naming density by 64 KB region ranges from good (`0x40000`-`0x4ffff`: 41 unnamed of 585)
  to almost none (`0x150000`+: library code).
- The PS2 spreadsheet (`offset, size, method_name, known_func_address, known_offset, guess_offset,
  guess_func_address, note`): 12719 rows, 529 `.obj` module markers in link order (212 distinct modules),
  3256 rows with a human-reviewed Xbox address, 492 with a guess only. Large classes with no matches yet:
  `EAGLAnim` (667 symbols), `EAGL` (578), `EAGLInternal` (160), `GHud` (150), `RPlayerCamera` (144),
  `AIGroundVehicle` (114), `AICharacter*` (150+), `RCMP` (73).
- Agent Under Fire symbols have been transposed with Ghidra's similarity tools (unverified coverage).

### 5.2 Suggested tooling: link-order alignment

Because both builds come from one source tree with one link order, the PS2 symbol sequence and the Xbox
function sequence are two orderings of nearly the same list, with local insertions and deletions
(platform-specific objects, inlining differences, VU0 code on PS2). That is a sequence-alignment problem,
and the 3256 confirmed matches are anchors. Build `tools/driving_symbol_align.py` that:

1. Loads the sheet CSV and `functions_driving.json` (plus, from Ghidra, per-function features: size,
   number of call sites, callees, referenced strings, whether it is referenced from a vtable).
2. Runs a monotonic alignment (dynamic programming with a gap penalty) between consecutive anchors,
   scoring candidate pairs on size ratio, callee-count similarity, and **string anchors**: the EA allocator
   takes a name string per allocation (`"EAGL::DynamicLoader new"`, `"EventBuffer"`), event classes have
   `GetEventName` returning a literal, and there are printf formats and file paths everywhere. A function
   that references `"%s\\paris_intro.mad"` on both platforms is a match regardless of code differences.
3. Propagates matches through **vtables**: PS2 vtables carry `type_info` names; once one virtual of a class
   is matched, the Xbox vtable (same slot order) names every other virtual of the class. This alone should
   cover most of `GHud`, `RPlayerCamera`, the `E*` event classes and the AI hierarchy.
4. Propagates through the **call graph**: if `A` and `B` are matched and PS2 `A` calls `X` at the same
   position Xbox `A'` calls `X'`, propose `X = X'`.
5. Emits proposals with a confidence and the evidence, for review in the sheet (new column), and an
   importer that writes accepted names into Ghidra (extend `ghidra/NightfireSync.py`, which currently only
   exports).

Expect this to lift named coverage from ~2100 to well over 5000 functions in a few iterations; the EAGL
library half of the binary is where AUF symbols help most, since that engine version is closer to AUF's.

### 5.3 Conventions

Keep the action-side conventions: rename in Ghidra, re-sync the JSON, cite addresses in comments. Add a
`docs/driving-symbols.md` glossary as classes become understood (scheduler, event manager, UMemory,
UFileLoader are already partly documented in `src/driving`).

## 6. Removing CXBX from the driving engine

Same method as the action engine (`cxbx-removal-plan.md` section 2), same order, with these differences.

### 6.1 Graphics: broader than the action engine

The action engine used 41 D3D8 entry points and its own vertex shaders; the driving engine reaches 108,
and its usage is a different shape:

- **Fixed-function transforms** (`SetTransform`, `D3D_UpdateProjectionViewportTransform`) alongside
  vertex shaders (`CreateVertexShader`, `LoadVertexShader`, `SelectVertexShader`, constants) - the D3D9
  backend's translator can be reused for the shader side; the fixed-function side needs the world/view/
  projection state and the fixed-function vertex pipeline (D3D9 still has it).
- **Pixel shaders** (`CreatePixelShader`, `SetPixelShader`, `SetPixelShaderConstant`): NV2A register
  combiner programs, which the action engine never used. These need a translator to ps_1.x/ps_2_0 or to
  fixed-function stage states where they are simple. CXBX's `PixelShader.cpp` is a reference for the
  combiner semantics (read, do not copy).
- **Immediate mode** (`Begin`/`SetVertexData2f`/`4f`/`SetVertexDataColor`/`End`) for HUD and debug drawing.
- **Push buffers** (`RunPushBuffer`, `D3DDevice_MakeSpace`, `D3D_SetFence`): EAGL "render methods"
  (`eaglrm.o`) may be precompiled NV2A command streams. Find out first how much geometry goes this way
  (xrefs of `D3DDevice_RunPushBuffer` and what builds the buffers); if it is the main path, the backend
  needs an NV2A command interpreter for the subset used, which is the single biggest risk item in this
  plan. If it is only used for a few effects, it can be reimplemented per call site.
- Visibility tests (section 3), fences (`InsertFence`/`BlockOnFence`), `CopyRects`, palettes
  (`CreatePalette2`/`SetPalette` - P8 textures), tiles/scissors/screen-space offset, `PersistDisplay`,
  `SetTile`, `GetGammaRamp`, stencil states, `TextureFactor`, `BumpEnv`, `ColorKey`, `LineWidth`,
  `FillMode`, `Dxt1NoiseEnable`.
- `D3DX` section: matrix functions and a JPEG decoder (loading screens?) - plain C, reimplement or map to
  a small library.

Plan: sweep the callers of all 108 entry points (`get_bulk_xrefs`), reimplement the EAGL device layer
functions that call them (`EAGL::Device`, `EAGL::GeoPrimState`, `RRenderer`, `SimpleDraw`, `RStateManager`)
as the seam, trace a level, then extend `d3d9Backend.cpp` (shared with the action engine, behind the same
`GraphicsBackend` switch) with the missing features in the order the trace demands.

### 6.2 Audio - do this first, not third

63 DSOUND entry points behind EA's `SND*` platform layer (`SNDPLATFORM_init`, `SNDPLATFORM_playtimbre`,
`SNDVOICEI_*`, `SNDSTRM_*`, `SNDDRV_thread`) - a cleaner boundary than the action engine's, since the EA
layer is already an abstraction with its own voice allocator and mixer (`AMix`). Seam the `SNDPLATFORM_*`
functions, then reuse the XAudio2 backend from the action engine (voices, mixbins, I3DL2 listener,
streams). The driver thread and its mutexes (`SNDI_mutex*`, `THREAD_*`) become Win32 threads/critical
sections.

The seam has to be **complete** rather than merely good, because anything that slips through reaches the
XBE's real DirectSound and its hardware registers (section 0). The action engine's experience is the warning:
its seam covered every game-side call and still missed the video decoder, which calls DirectSound directly.
Here, check the same way - find every caller of the 63 entry points, and treat any that is not inside the
`SND*` layer as a hole to hook at the entry point itself.

### 6.3 Runtime, kernel, loader

Identical shape to the action plan, plus: the multimedia timer (section 2), Xbox events
(`NtCreateEvent/SetEvent/PulseEvent/WaitForMultipleObjects`), kernel timers (`KeSetTimerEx`),
`KeTickCount`/`KeQueryInterruptTime` reads, and `UFileLoader`'s big-file streaming on `NtReadFile`.
The loader from the action plan should load either XBE - `Driving.xbe` is smaller than the action XBE and
has the same base, so the existing reservation covers it. The hand-off stays a **process relaunch**, though:
both XBEs are linked at `0x10000` and the loader gets that address by being the image there, so two of them
cannot be mapped at once. The loader re-executes itself with the other XBE and the launch data page travels
in a file, as `psiLaunch.bin` already does. (An earlier version of this plan expected an in-process
transition; that is not possible, and it is not a limitation a better loader removes.)

## 7. Suggested order of work

Reordered now that the standalone loader exists. The principle that changed: the original order front-loaded
fixes for CXBX's behaviour - scaling the visibility-test result, containing the sound-buffer leak, replacing
the game's clock - and each of those is a correction for a host that is being removed. Going standalone first
makes several of them unnecessary rather than merely earlier.

The risk of going standalone first is that nothing runs until the audio seam is complete, so there is a
longer stretch with no playable build than the old order had. That is the trade, and it is worth it because
the audio seam is on the critical path either way.

1. **Boot `Driving.xbe` under the loader and see where it stops.** Point the loader at the other XBE, run,
   read what the kernel stub table names, implement that, repeat - the same loop that took the action engine
   from nothing to the main menu. Expect it to get as far as audio initialisation and then hit DirectSound
   reaching for hardware. Cheap, and it turns the rest of this list from estimates into a queue.
2. **The FS and startup work** (section 0): 31 sites, the same five-ish functions as the action engine.
   Needed before anything runs, and well understood now.
3. **The `SNDPLATFORM_*` seam and the XAudio2 backend** (section 6.2). The backend, including streams, is
   already written for the action engine. This is what makes the engine boot, and it is also the cure for
   the crash in section 4 - which is why the instrumentation there is worth doing as part of this rather
   than before it.
4. **Timekeeping** (section 2), measured rather than assumed: with our own timer implementation underneath,
   check whether the original scheduler semantics behave before changing them.
5. **Symbol alignment tool** (section 5). Unchanged, still the enabler for the graphics work, and still
   parallelisable with everything above.
6. **Push-buffer investigation** (section 6.1) to size the graphics work, then the D3D8 seam and the D3D9
   backend extension - including the visibility tests, which fix the lens flares properly (section 3).
7. **Profiling** (section 4) once it runs standalone, where the numbers mean something. Under CXBX they
   mostly measured CXBX.

Keeping the CXBX path working in parallel, as the action engine did, is still worth it for as long as it is
free: it is the only way to bisect "did we break this or was it always broken". It stops being free at the
point where a change has to be conditional on the host, which is the same judgement the action engine's
`Xbox_RunningStandalone()` records.
