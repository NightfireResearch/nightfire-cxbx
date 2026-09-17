# Driving engine: state of knowledge and project plan

Companion to `cxbx-removal-plan.md` (the action engine). Written September 2026 after a first survey of
`Driving.xbe` in Ghidra (`/Xbox_EU/Driving.xbe`, 8959 functions), the repository's driving code
(`src/driving`, `src/inject_driving.cpp`) and the PS2 symbol spreadsheet. Facts below cite addresses in the
Xbox EU build so they can be re-checked; nothing here has been verified at runtime yet.

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

Proposed fix (small, and the first thing to do): replace `Timer_Init`/`TIMER_ontick`/`RealClock_*` with our
own implementation that derives `Clock` from `QueryPerformanceCounter` (ticks elapsed at 1000/freqHz ms,
sampled when `Scheduler::Run` is entered, or a `timeBeginPeriod(1)` thread if other timer tasks need
callbacks), and restore the original `Scheduler::Run` semantics with the catch-up capped (for example run
at most 4 ticks per loop and never drop time). Then pace the loop: either sleep to the next tick edge in
`Scheduler::Run` when `dt == 0`, or rely on Present pacing once the D3D9 backend is in use. Keep the
`timeScale` and cinematic-skipping paths intact.

## 3. Lens flares

`RLensFlareManager::TestFlares` (`0x9e720`) draws a 16x16 test quad per flare inside an NV2A visibility test
(`FUN_000e7c60`/`FUN_000e7c80` wrap `D3DDevice_BeginVisibilityTest`/`EndVisibilityTest`, index 0..15
ring). `DrawFlares` (`0x9e540`) spins on `D3DDevice_GetVisibilityTestResult` until the result is ready and
computes intensity as `(visiblePixels - 256) / 256`, so it assumes a 256-pixel quad at native resolution.

Under CXBX the visibility test is answered with a host occlusion query at the host's render resolution, so
the pixel count scales with the render-scale squared and the flare's brightness/size explodes. The current
patch NOPs the `DrawFlares` call. Two fixes, in order of effort:

1. In CXBX mode, hook `FUN_000e7ca0` (the result wrapper) and divide the count by (host width / 640) x
   (host height / 480), read from the present parameters. Restores flares immediately.
2. In a native D3D9 backend, implement the visibility tests with `D3DQUERYTYPE_OCCLUSION` at the game's own
   resolution; the count is then exact, and the spin-wait becomes a `GetData(FLUSH)` wait.

Also note the spin-wait itself: with a slow emulated query it stalls the frame; that is one of the
"performance" symptoms.

## 4. Crashes and performance: how to find out

Nothing in the survey pinpoints the driving-level crashes; they need data, not theory.

- **Crash capture**: add a vectored exception handler to the injected DLL that logs EIP, registers, the
  faulting address and a stack walk, symbolised from `tools/functions_driving.json` (nearest function
  below each return address). Write it to `driving_crash.log`. Every crash report then names the game
  function, which is what makes the rest tractable.
- **Sampling profiler**: a thread in the DLL that every 1 ms suspends the game's main thread, reads EIP
  (`GetThreadContext`), resumes it and histograms by function (same symbolisation). Dump the top 50 at
  level end. This answers "where does the time go" for free, without host tools that cannot see XBE symbols.
- Candidate hot spots to expect: `D3DDevice_Begin`/`SetVertexData2f`/`4f`/`End` immediate-mode calls (per
  vertex HLE overhead), `D3DDevice_RunPushBuffer` (EAGL submits precompiled NV2A command streams - see 6.1),
  `BlockOnFence`/`IsBusy`/visibility-result spins, and the sound driver thread contending with CXBX's
  thread emulation.
- Candidate crash sources: the 36 MB `UMemory` heap (`UMemory::Init(0x2400000)`) under a different memory
  layout, async big-file streaming (`UFileLoader` request lists, `SYNCTASK`), and the `Event` buffer
  (`EventManager`, 32 KB ring at `0x1e47d4`) overflowing when the simulation catches up many ticks at once
  (the timekeeping fix in section 2 may remove a class of crashes by itself).

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

### 6.2 Audio

63 DSOUND entry points behind EA's `SND*` platform layer (`SNDPLATFORM_init`, `SNDPLATFORM_playtimbre`,
`SNDVOICEI_*`, `SNDSTRM_*`, `SNDDRV_thread`) - a cleaner boundary than the action engine's, since the EA
layer is already an abstraction with its own voice allocator and mixer (`AMix`). Seam the `SNDPLATFORM_*`
functions, then reuse the XAudio2 backend from the action engine (voices, mixbins, I3DL2 listener,
streams). The driver thread and its mutexes (`SNDI_mutex*`, `THREAD_*`) become Win32 threads/critical
sections.

### 6.3 Runtime, kernel, loader

Identical shape to the action plan, plus: the multimedia timer (section 2), Xbox events
(`NtCreateEvent/SetEvent/PulseEvent/WaitForMultipleObjects`), kernel timers (`KeSetTimerEx`),
`KeTickCount`/`KeQueryInterruptTime` reads, and `UFileLoader`'s big-file streaming on `NtReadFile`.
The loader from the action plan should load either XBE; the action-to-driving hand-off (launch data page,
`ReturnToAction`) then becomes an in-process transition between two modules rather than a process relaunch.

## 7. Suggested order of work

1. Crash logger and sampling profiler in the DLL (section 4). Cheap, and every later step benefits.
2. Timekeeping: native `Clock` source and the original scheduler semantics with a catch-up cap (section 2).
   Test: consistent game speed at any frame rate, no freezes after loads.
3. Lens flares via the result-scaling hook (section 3, option 1).
4. Symbol alignment tool and a first propagation pass (section 5). This is the enabler for everything in
   section 6 and can run in parallel with 1-3.
5. Push-buffer investigation (section 6.1) to size the graphics work.
6. D3D8 seam and D3D9 backend extension; DSOUND seam and XAudio2 backend; runtime/kernel; loader.
