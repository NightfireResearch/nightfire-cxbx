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
specific to a host, and `actioninject.dll` is already loaded unchanged by both. (`driving.exe` now exists - see 0.1 - and is the same loader binary as `action.exe` with two default file names changed.)

**And one correction to section 6.3 below:** the action-to-driving hand-off cannot become an in-process
transition. Both XBEs are linked to base `0x10000`, and the loader gets that address by *being* the image
there - so only one XBE can be mapped at a time, and that is not a limitation a cleverer loader removes. The
hand-off stays a process relaunch: the loader re-executes itself with the other XBE, carrying the launch
data page across in a file, which is what `psiLaunch.bin` already does.

## 0.1 Status: what runs standalone today

Measured rather than predicted - every line below came out of a run. `driving.exe` (the loader, built from
the same sources as `action.exe` with `IS_DRIVING` choosing the two default file names) maps `Driving.xbe`,
loads `drivinginject.dll`, and the engine now **loads a level and enters its game loop**:

```
[timer] tick every 20 ms (timer 16), callback at 0x0010ae10
[d3d9] device created on window (640x480 backbuffer, 32-bit float reversed depth, paced to 50 Hz)
(00:00:01) 35.58MB - Init File System
-------- Loading file data\track\uw_mis11.crp ... (6256128 bytes)
(00:00:03) 28.47MB - Entering GameLoop
(00:00:03) 28.44MB - Init Game Render
[d3d9] shader 5 translated: 8 NV2A instructions, inputs 0x0007, outputs 0x1009, streams 0x007
```

So: process startup, the C runtime, the launch-data read, `main`, the 36 MB heap, the file system, the
async loader, controllers, the scheduler, the underwater level's data, and the first vertex shaders through
the D3D9 backend's translator. It stops in the renderer's first frames, in two places (below).

### What it took

- **The loader's kernel**, one import at a time as its stub table named them: `NtClose`, the event and wait
  family (`NtCreateEvent`/`SetEvent`/`ClearEvent`/`PulseEvent`/`WaitForSingleObject(Ex)`/
  `WaitForMultipleObjectsEx`, `KeDelayExecutionThread`), thread control (`NtResumeThread`,
  `NtSuspendThread`, `NtYieldExecution`), `ExQueryNonVolatileSetting` answering PAL-I and English, and a
  file system (`src/loader/file.cpp`): `NtCreateFile`, `NtOpenFile`, `NtReadFile`, `NtWriteFile`, the two
  information classes the game asks for, and `RtlInitAnsiString` beside them. Paths resolve through
  `src/common/xboxPath.cpp`, which moved out of the action engine so the loader could share it.
- **The startup replacement**, `src/driving/platform/XboxStartup.cpp`: `mainXapiStartup`, `XapiInitProcess`,
  the last-error pair, the CRT's per-thread data, the seven `FS:[0x20]` notification hooks and nine `WBINVD`
  sites. It is the action engine's file transposed - the XAPI in the two XBEs decompiles identically - and it
  carries the address correspondence table, so the next person does not re-derive it. `XInitDevices`,
  XAPI's `GetCurrentThreadId` and `SetThreadPriority` go the same way: all three reach for something a Win32
  thread does not have, or for a USB stack that is not there.
- **The tick source**, `src/driving/platform/XboxTimer.cpp`: `timeSetEvent` (`0x0010eed3`) becomes winmm's,
  with `timeBeginPeriod(1)`. XAPI's own is a thread waiting on sixty-four kernel timer objects; implementing
  the dispatcher under it would have meant reproducing Xbox timers and DPCs to deliver a callback Windows
  delivers itself. This is the "timer of our choosing" section 2 asks for, and what step 4 should now measure
  `Scheduler::Run` against.
- **Controllers**, `src/driving/platform/XboxInput.cpp`: XAPI's seven input functions become Win32 XInput.
  The two APIs are the same API twice - identical digital bits, identical sticks - with two differences: the
  Xbox's face buttons are analogue (a pressed Win32 button becomes 255) and its black and white buttons take
  the shoulder bits. `IOModule` then runs unchanged and reads real pads. There is no keyboard fallback yet;
  the action engine's (a synthesised pad on port 0) is the model.
- **The graphics seam**, `src/driving/gfx/`, described below.

### Two traps worth knowing about

**Critical sections that were never initialised, and ones that were.** An Xbox `RTL_CRITICAL_SECTION` can be
built by the compiler - a static one is simply laid out unlocked in `.data` - and XAPI's multimedia timer has
one at `0x001d2f48`. Those bytes mean something else to Win32, so `EnterCriticalSection` waited on a handle
that was not one and the boot hung. The loader now initialises any section it has not seen before, on first
use. The second half of that is subtler: `RtlInitializeCriticalSection` must *always* initialise, even for an
address already seen, because the game frees and re-allocates blocks that contain sections, and the new owner
zeroes the memory first. Skipping that left a zeroed section whose first contended wait faulted inside ntdll,
a long way from the cause.

**A kernel ordinal that was wrong.** `tools/gen_kernel_ordinals.py` took its names from Cxbx-Reloaded's
`EXPORTNUM` annotations, and the header annotates `NtProtectVirtualMemory` with `EXPORTNUM(205)` - which is
`NtPulseEvent`'s number. The loader's own table had copied the mistake, so a call to `NtPulseEvent` would
have landed in `NtProtectVirtualMemory`. The generator now reads Cxbx's kernel thunk array, which is indexed
by ordinal and therefore cannot disagree with itself; that also fixed nine other names.

### The graphics seam, and why it is at the D3D8 entry points

Section 7 step 1 expected the boot to "get as far as audio initialisation and then hit DirectSound reaching
for hardware". It does not: the renderer comes first. `RRenderer` (`0x0007d2a0`) calls EAGL's device creation
(`FUN_000e6c00`), which calls `D3D8::Direct3D_CreateDevice` (`0x00169480`), which calls
`D3D8::CMiniport_InitHardware` (`0x0016fcad`) - the nv2a miniport, with `KeInitializeDpc`,
`HalGetInterruptVector`, `KeConnectInterrupt`, `HalReadWritePCISpace` and an `out` to port `0x80c0`.

The action engine's seam sits *above* D3D8, reimplementing Eurocom's thin wrappers. That does not transpose:
the driving engine's equivalent layer is EAGL, which is most of the binary, largely unnamed, and reaches D3D8
from everywhere. So the seam is at the library boundary instead - every D3D8 and XGRAPHC entry point patched
at its own address, EAGL running exactly as built. Three things make that work:

- the backend's API already mirrors D3D8 entry point for entry point, because it was written against those
  semantics for the action engine. It moved to `src/common/gfx/` and is now shared; its only ties to the
  action engine were a settings read and a streaming report, which became `common/gfx/backendHost.h`;
- `tools/d3d8_entry_points.py` generates the table of all 113 entry points with the stack-argument size each
  one pops, read out of the binary by disassembling from the entry point to its first `RET`. That tool has to
  *follow* a first-instruction jump rather than sweep past it: `Get2DSurfaceDesc` is a one-instruction thunk
  to a function that pops twelve bytes, and reading the eight bytes of the next function put a four-byte hole
  in the caller's stack;
- anything not implemented yet gets a stub that reports itself once, cleans up the caller's stack from that
  table and returns zero, so a single run names everything the game reaches. The count and the names are
  printed alongside the backend's frame timing.

**The uncached alias is not a problem here, which was worth checking.** Section 0 warns about `0x80000000 |
address`, the Xbox's uncached view of RAM. Every site in `Driving.xbe` that sets that bit on an address is
inside the D3D8 library this seam replaces - fifteen of them, fourteen in D3D8 and one a flag on a physics
slot index. EAGL never does it, so the seam's `Lock` functions can hand back ordinary pointers and no alias
has to exist.

### Where it is now

It runs: the underwater level loads, the game reaches its main loop and holds fifty frames a second with
about 195 draws in each, the menus are textured and take input from a pad or from the keyboard standing in
for one, and the intro movie plays from inside `misc.viv` at its own 25 fps. Several things were in the way,
and each was a different kind of wrong - they are written up in the commits, but the two worth knowing about
here are:

- **the shared backend was reading the action engine's addresses.** It reads D3D8's own deferred state -
  texture stage operations, filters, fog - back out of the XBE at draw time, and those tables are at
  different addresses in the two builds. Reading the action engine's landed in the middle of the driving
  build's XAPI, so every filter mode and colour operation was whatever happened to be in that code. It
  looked like untextured geometry and it ended as a crash inside the display driver, compiling a shader for
  the nonsense. The addresses are the engine's to set now (`g_xboxTextureStateTable`), read out of the two
  functions that write them;
- **an Xbox title's memory is all executable, and this one means it.** EAGL compiles each model's render
  method into allocated memory and calls it. Under DEP the first model drawn faults. The loader is linked
  `/NXCOMPAT:NO` and its memory shims hand out executable pages.

**The seam checks its own replacements now.** Two of them popped the wrong number of argument bytes, which
is silent until the caller returns into whatever was left on the stack - one arrived as a jump into the
middle of a vertex buffer, two calls later. Every replacement declares what it pops and the seam compares it
against the generated table at install time; that check found the second one immediately, and a bug in the
generator behind it.

### Textures, movies, and a stopped clock

Three things looked like three problems and were not.

**The textures were missing because the D3D8 state tables start empty.** Everything drew as flat squares -
menus, the briefing screen, the HUD. The backend reads D3D8's deferred texture stage state back out of the
XBE at draw time, and in the image those tables are all zero: it is the real `Direct3D_CreateDevice` that
fills them with the defaults, and this seam replaces it. Stage 0's colour operation was therefore
`D3DTOP_DISABLE`, which is exactly "ignore the texture". `InitialiseD3D8State` in the seam now writes the
defaults the library would have written - wrap addressing, linear filtering, modulate on stage 0, disable
above - right after the device is created, and the briefing screen came up fully textured.

**The movies were never broken.** They are not on the disc as loose `.mad` files, they are inside `misc.viv`,
and the engine's own file system looks for the loose file first and falls back to the open archives - so the
"could not open `D:\pal\eng\island_intro2.mad`" that led to a patched-around FMV path was the *normal*
first half of a lookup that then succeeds. The reads land at offset `0x75F6880` of `misc.viv`, which is where
that movie is. The patch and the stream shim written for it were removed; nothing was wrong with the paths.

**But the movie played at a tenth of a frame a second, and the reason was a kernel variable.** The chain is
worth writing down, because nothing about the symptom pointed at the cause:

- `KeTickCount` is kernel ordinal 156, and it is *data*: the XBE's import thunk holds the address of a
  variable the console's kernel increments every millisecond, not the address of a routine. The loader
  resolved it like every other unimplemented ordinal, to a reporting stub - and because a stub is only
  reported when it is *called*, a variable that is only ever read said nothing at all. `getTickCount()`
  returned the first four bytes of a `push` instruction, forever.
- EA's sound driver thread paces itself with `sleep(nextDeadline - getTickCount())` and `nextDeadline += 10`.
  With the clock stopped the deadline runs away from it: every iteration sleeps ten milliseconds longer than
  the last. The 100 Hz sound server was down to two or three hertz within a minute.
- That server is what drains the mixer's ring buffer, which is 50 ms long. Asked three times a second, it
  can see at most one ring's worth of movement per ask, so the game believed about 3 kB/s of audio had been
  consumed where the truth was 96 kB/s.
- The movie's streaming is paced by audio consumption: video and audio chunks share one ring, and ring space
  is reclaimed in order, so the unconsumed audio at the tail held everything behind it. The player spun in
  `GetRCMPChunk` waiting for a video chunk that could not be read until the audio in front of it was freed.

`KeTickCount` is now a real counter advanced by a thread in the loader. The mixer runs at 100 Hz, consumes
96000 bytes a second, and the intro movie plays at a steady 25 fps; the menus behind it hold 50 fps with
19 ms a frame to spare. The other data exports the XBE imports - `XboxHardwareInfo`, `LaunchDataPage`,
`ExEventObjectType`, `PsThreadObjectType`, `HalDiskCachePartitionCount`, `XboxKrnlVersion` - still resolve to
stubs and are still read as though they were data. None has caused trouble yet, but they are all the same
shape of trap.

**There is a sampling profiler now** (`src/common/xbeProfiler.cpp`), because none of the above was findable
any other way: the XBE is mapped by hand, so no Windows profiler can see into it. It suspends every thread in
the process a thousand times a second, records EIP, and prints the hottest addresses per thread - raw
addresses for the XBE, which are the ones Ghidra shows, and `module!export+offset` for anything else. Turn
it on with `Profile=on` under `[Settings]` in `settings.ini`, the same file the action engine's settings live
in; the file is read once, so it costs a compare a frame otherwise.

### The world was drawing from a single vertex

The level loaded, the game ran at fifty frames a second with about two hundred draws in each, the HUD and the
menus were right - and the 3D world was not there at all, just the water's blue fog. Three things were in the
way, and the shape of each is worth keeping, because none of them said anything in a log.

**The shared backend's vertex shader table was too small.** 160 slots, and the driving engine creates about
196. Past the end `CreateVertexShader` returned a failure the game ignores, so it kept whatever handle it had,
and `PrepareShaderDraw` dropped every draw that would have used one of the missing shaders - a third of the
frame's draws, counted but not explained. The table holds 512 now and says so when it fills.

**Vertex type `0x25` (SHORT2) was missing from the declaration translator**, which failed two more shaders
outright. The Xbox type byte is `(count << 4) | kind`, so the neighbours of a missing entry name it exactly.

**And the one that actually hid the world: an Xbox vertex buffer object has no length in it.** It is three
words - Common, Data, Lock - because the console's hardware reads the game's own memory and nothing needs to
know where the buffer ends. The backend was reading a byte size out of word 5, which is where the *action*
engine's own buffer slots keep one; EAGL's headers keep nothing there, so word 5 was whatever the heap had
put after the object. It read 12. Every world draw therefore uploaded one vertex and drew the whole level
from it, which is why disabling depth, culling and alpha changed nothing: there was nothing to reject.

The size now comes from the draw - the highest vertex index it will read, times the stream's stride - which
is exact, cannot over-read the game's allocation, and does not care which engine made the buffer. The words
above are kept only as a floor for draws that do not know their own extent.

With that the level draws: the sunken tanker, the water surface, the wreckage. Dark and flat, because the
materials are the register combiners that are still ahead.

### What the world looked like after that, and what was wrong with it

Geometry on screen is not the same as geometry right. Four more things, each found by looking rather than
reasoning:

- **The HUD text went black** when the texture stage defaults went in. The font is `X_D3DFMT_LIN_A8` -
  alpha only - and an A8 texture has no colour in it: sampling one gives RGB zero, on the NV2A as on D3D9.
  The game draws its text with a combiner that takes the colour from elsewhere and only the coverage from
  the font, and the combiners are not translated, so the fixed-function fallback painted black. A colour
  argument naming a texture that has no colour now falls back to the diffuse.
- **Nothing was depth-sorted.** The translated shaders write depth from w, mapped through the near/far the
  game gives `SetDepthClipPlanes` - which the driving engine's D3D8 does not even export. With the defaults
  that mapping is a constant: every vertex in the level came out at the same depth and the scene drew in
  submission order. When the planes are never set, the shader's own z is used instead.
- **A one-component short read two bytes of the next vertex.** `X_D3DVSDT_SHORT1` is two bytes; the table
  had it as four, and D3D9's smallest vertex element is four bytes anyway. The declaration is sized right
  now, and the translator puts the NV2A's (0,0,0,1) defaults back for anything the declaration does not
  give, so a neighbour's bytes cannot arrive as this vertex's .y. The backend says so when a declaration
  reads past its stream's stride, which is how this was found.
- **The vertex buffers went stale.** On the console the hardware reads the game's own memory, so a buffer
  the game rewrites is simply rewritten - there is no upload and therefore no moment at which the game has
  to announce a change. EAGL rewrites plenty of them, and the host copy was taken once and kept: the level
  drew last time's vertices, in the wrong colours and stretched into the shapes that made it look spiky.
  The copy is refreshed on each buffer's first use in a frame. Textures have a real signal for this
  (`D3D9_NotifyTextureModified`, which the action engine's D3D8 calls); nothing calls anything here.

After those four the world was still made of triangles that reached across the screen - "spiky", with the
right objects moving in the right places. That was the next section's problem.

### The level was drawing the level file's header

Three tools before the finding, because guessing had stopped working:

- **every translated shader is written to `d3d9_shaders.log`** - the declaration tokens as the game gave
  them, the D3D9 elements they became, the raw microcode and the HLSL. It showed EAGL's vertex layout at
  once: one stream per attribute (a FLOAT3 position stream, SHORT2 coordinate streams, a D3DCOLOR stream),
  never interleaved, and every program ending in the XDK's standard viewport epilogue on `c[58]`/`c[59]`,
  so the translator's assumptions all held;
- **`DumpEvery=N` in `settings.ini`** dumps every Nth frame as before (it was a compile-time constant, and
  a rebuild each way), and now also **traces that frame's draws** to `d3d9_trace_<frame>.log`: shader,
  primitive, vertex range, each stream's buffer object and the memory it points at, the extents of the
  positions the draw actually reads, and the first transform constants;
- and the trace was decisive. Every stream of every world draw pointed at the same address, and the bytes
  there were `7f 45 4c 46` - the ELF header of the render-method object file that begins the level pack.
  The first "position" of the sunken tanker was `(13073.4, 9e-41, 0)`.

**`D3DResource_Register` adds the base to the Data word; the backend was storing it.** A resource built
inside a loaded file carries the *offset* of its data from the file's start in its Data word, and
registering it against the file's address in memory turns that into a pointer - that is what the original
does (`0x001693a0`: `Data += base`, then masked to 28 bits for anything but a push buffer). The action
engine only ever registers headers it has just zeroed, so for it "store" and "add" are the same operation,
and the shared backend had done the former for as long as it had one engine. EAGL registers every static
vertex buffer in a level pack this way (`VertexBufferConstructor`, `0x000f0ee0`, is one of three callers),
so all of them read from byte zero of the pack, and the level was that header drawn a few thousand times
through the right object matrices. One line; the car, the seabed and the cliffs appeared.

Two smaller things fell out of the same investigation:

- **`XGSetVertexBufferHeader` is implemented** (it was the one dropped draw a frame). Its one caller,
  `FUN_000f6d50`, passes its pointer *minus* `0x80000000` - which on the console strips the uncached alias
  to reach the physical address, and on a Win32 pointer sets bit 31 instead. So the claim above that EAGL
  never touches bit 31 was one site short; the seam masks it off, which is right either way;
- **EAGL's dynamic vertex buffer is three Xbox buffers behind one object**, rotated on each lock
  (`FUN_000f6d00`), and its stream-binding routine copies each draw's CPU-side array into the current one
  just before the draw (`FUN_000f6890`). A host copy refreshed once a frame therefore serves the first draw
  of a frame and feeds later ones stale vertices. That was suspected of the spikes first and was not them,
  but it is real, so the driving engine's draws now copy exactly the range they read through a dynamic
  vertex ring at the draw - the same arrangement the index ring already used (`g_streamsVolatile`, set by
  the seam; the action engine keeps its cached copies). It costs about a megabyte of memcpy a frame.

### The register combiners

With the geometry right, the materials were the fixed-function fallback: the world in fogged washes, the
car's rear panel a rainbow. The game's materials are NV2A pixel shaders - register combiner programs, 197 of
them created in a run - and the seam had been accepting and ignoring them. They are translated now
(`src/common/gfx/nv2aPixelShader.cpp`), and the level looks like the game.

What one of these is, for the next reader: not a program but 240 bytes of register values, the XDK's
`D3DPIXELSHADERDEF`, which the console's D3D8 copies straight into the push buffer when the shader is set
(`D3DDevice_SetPixelShader`, `0x0016af60`, is a word-for-word copy). Up to eight combiner stages, each
computing `A*B` and `C*D` and their sum or a mux on the RGB and alpha halves of a few registers (`r0`, `r1`,
the four texture results, the two vertex colours, two constants and fog), with a mapping on every input and
a scale on every output; then a final combiner doing `A*B + (1-A)*C + D`. The translator writes the same
arithmetic as ps_2_0 HLSL (ps_2_b when a long program needs the room), reads all of a stage's inputs before
writing any of its outputs because the halves run in parallel, and clamps where the hardware clamps.

Three decisions worth knowing about:

- **Fog is the host's.** The NV2A applies fog only where the final combiner does, and ps_2_0 cannot read
  the fog factor. 36 of the 61 custom final combiners here are the standard `fog.a * r0 + (1 - fog.a) *
  fog.rgb`, and the other 136 shaders leave the final combiner at its default, which the runtime fills in
  with the same blend. In both cases the program leaves fog out and D3D9's post-shader fog, driven by the
  vertex shader's `oFog`, does that exact blend; for any other final combiner the host's fog is turned off,
  as the hardware would have it. A program that reads the fog register anywhere else sees its colour with a
  factor of one.
- **Constants come from two places.** A stage's constant is the literal in the definition unless its
  mapping nibble names one of the sixteen `SetPixelShaderConstant` registers, which the original checks per
  stage as it writes (`0x0016b160`); the backend builds the block the same way at each draw. Nearly every
  mapping here is "none"; the car's paint is one that is not.
- **The stages are the program's.** A translated shader binds Xbox stage *n* to sampler *n* and reads
  coordinate set *n*, with none of the packing the fixed-function fallback does, and the bump-environment
  matrices (`SetTextureState_BumpEnv`, which the seam used to drop) are read out of the deferred texture
  state table where the original puts them. `X_D3DTSS_COLORSIGN` expands the channels the game declared
  signed, which is how the bump maps arrive.

What the game uses, from a dump of all 197 (`tools/nv2a_psh_dump.py --summary` on
`d3d9_pixel_shaders.log`, which the backend writes with the HLSL each became): texture modes PROJECT2D,
PASSTHRU, BUMPENVMAP (nine) and one dependent-AR read; dot products in 35 stages, output scaling in 67,
combiner writes to the texture registers in 37, a single mux. All translated. The modes the game does not
use - the cube and 3D projections, the DOT_* reflection family, clip planes - sample as 2D and say so in
the log, so a shader that turns up later in another level names itself rather than drawing black.

The pause menu's missing backdrop went with it: the panel is drawn through a combiner, and had been coming
out invisible through the fallback. So did the loading-screen images.

**And the text went black, which corrected an earlier correction.** The fonts are alpha-only textures, and
the text combiner is `r0 = v0 * t3`: the vertex colour times the texture's colour. On the NV2A an A8 texture
samples as (1, 1, 1, a) - xemu's format table swizzles it that way - so the colour is the vertex's. On D3D9 an
A8 samples as black, which is what the translated combiner then drew. The earlier "an alpha-only texture has
no colour on the NV2A either" (above) was wrong; the fixed-function hack it justified, taking the colour from
the diffuse, happened to give the right answer for the fixed-function path and was removed. A8 textures are
widened to A8L8 with a white luminance on upload, and every path gets white.

Two things were added while looking for what the combiners had left: `CheckVertices=on` in `settings.ini`
reads every draw's positions out of the game's memory before the draw and, for the first one that is not a
number or is off any level's scale, logs the draw and its streams and dumps that frame - it exists for a
glitch that lasts one frame, which no periodic dump catches (a minute of driving has not yet produced one,
so whatever those are, they are not positions out of range); and the dumped frame's textures are written
out beside it, decoded, with the header words in each file's name. `tools/drive_game.ps1` can now hold a
key (`-HoldKey w -HoldDelayMs 20000 -HoldMs 60000` drives the car for a minute), and can wait for a line
in the game's log before it does: `-HoldKey enter -HoldAfterPattern "draw mix: [1-9][0-9][0-9]? indexed"`
holds START once the level is drawing, which is how the pause menu is reached on every run rather than
when the load happens to take the expected time. It also re-asserts the foreground before every key, with
the ALT tap Windows requires of a process that does not own it.

**The pause menu's video window.** A 128x128 linear texture is bound at stage 3 of a quad in every frame,
in the level and in the menu, and its memory is a contiguous allocation the game made and writes into
directly - no lock the seam could see. The console's GPU reads such memory live; the host copy was taken
once, at first bind, and kept, so it showed whatever the memory held then: nothing here, noise on another
machine ("static" in the pause menu). Linear textures are now uploaded again on their first bind in each
frame where `g_streamsVolatile` is set, which is the same policy the vertex buffers needed and for the
same reason. What the game writes there is another matter: in these runs it writes nothing - the window
stays empty - and the likeliest reason is that it is a streamed video paced, like the intro movie, by the
audio path, which is still silent (section 0.1, "Sound"). A lock of the backbuffer stand-in now reads the
real backbuffer back, in case a screen copy is what fills it; nothing has locked it yet.

### What is left

In the order the frame counter puts them:

1. **Stencil and fill mode**, accepted and dropped by the seam, and the visibility tests behind the lens
   flares (section 3).
2. **Sound.** The seam is silent: it creates buffers, times them and reports them finished, but plays
   nothing. The action engine's XAudio2 backend is written and the formats here are ones it handles - 48 kHz
   mono, PCM or Xbox ADPCM - so this is wiring rather than invention. The timing model matters more than it
   looks: the movie above is paced by it.
3. **The clock runs fast** (section 2.1): the game's own log timestamps advance about six times real time,
   which is the 733 MHz constant baked into `timestamp()` and the `QueryPerformance*` pair. The action
   engine's fix transposes. Note this is a different clock from `KeTickCount` above - the game has both, and
   only the second one paced the movie.

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

Reordered now that the standalone loader exists. Steps 1 and 2 are done; 0.1 says what that bought and what it corrected about the order below. The principle that changed: the original order front-loaded
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
