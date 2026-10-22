# The action engine on 64-bit

Step 4 of the plan: action_standalone (docs/action-standalone.md) as a 64-bit executable, done the way the driving
engine's was (docs/driving/64-bit.md) and reusing what that built: `Ptr32<T>` for the structures read in place from
the game's files, decoded against a low game window; native pointers in the structures that live only in memory; the
x87 code as C. Started 22 October 2026.

## First measurement (22 October 2026)

`cmake --preset windows-x64` and a build of action_standalone: 20,973 errors before MSVC gave up on 205 files (C1003,
a hundred errors in one file), so the real counts are larger. By kind:

| Kind | Count (MSVC, capped) | Where |
|---|---:|---|
| Layout checks failing (C2338) | 20,405 | engine/Anim.h 11,110, AINetwork.h 4,242, Collide.h 2,424, Script.h 2,212, util/Stack.h, DList.h, Text.h, celglist.h, parsemap.h: the file structures and the ones they embed (each header's checks counted once per file that includes it) |
| Inline and naked assembly (C4235, C2485, C7732) | 34 shown | the x87 operations (math/X87Ops.cpp, CRuntime.cpp's `_ftol2`) and the register adaptors |
| Names the failed declarations left undefined (C2065, C2146, C3861, C2039) | 323 | knock-on |

240 naked functions outside devtools, none of them guarded: 20 x87 operations (X87Ops.cpp), the C runtime's `_ftol2`,
four in math.cpp, and about 215 register adaptors through which the original's code calls a port (`Script_Run` pushes
EBX and calls `_Script_Run`; our code calls `_Script_Run`). The adaptors exist for the injected build only.

## Packages

| # | What | Check |
|---|---|---|
| 1 | **Scaffolding**: the register adaptors under `#ifndef NIGHTFIRE_STANDALONE` (their declarations too), so the standalone build has none; action_standalone(_test) in the windows-x64 preset | 32-bit: the standalone build links without them, 13 demos; the injected build unchanged (suites, demos) |
| 2 | **The x87 code as C** (`NIGHTFIRE_X87=0`): `Extended` and its operations in C (double, as the driving engine's C forms), `_ftol2`, math.cpp; the shared C runtime already has its C forms. The 32-bit injected build keeps the x87 forms (exact against the originals and the recordings) | a 32-bit action_standalone_test with X87=0, its demos rebased once into a second set of checksums (the C baseline), as the driving engine's lbase_c |
| 3 | **`Ptr32<T>` and the game window**: Ptr32.h shared with the driving engine; the window is the low memory the standalone kernel already gives (MmAllocateContiguousMemoryEx below 0x10000000) and the game's heaps | 32-bit: unchanged code (Ptr32 is `T *`) |
| 4 | **File structures**: anims, AI network, collision, scripts, levels and models (the loaders and every structure they read in place, and what those embed) retyped to Ptr32 | 32-bit unchanged; 64-bit loads every level |
| 5 | **Memory-only structures**: their layout checks 32-bit only; literal allocation sizes as `sizeof`; pointer/integer conversions | 32-bit unchanged |
| 6 | **The D3D and DirectSound seams, the backends** in 64-bit | 64-bit boots to the menus |
| 7 | **Bring-up**: the 13 recordings on 64-bit action_standalone_test, exact against the C baseline on the same machine | 13 of 13 |

After it: Linux, macOS and arm64, for which the action engine's window, input and sound (Win32, XInput, XAudio2 here)
go through a host layer as the driving engine's do.

## Done: packages 1 to 5 (22 October 2026)

**1. Adaptors.** The 212 register adaptors are `#ifndef NIGHTFIRE_STANDALONE` with their comments and tags. Five that
our code calls with ordinary arguments have C forms in the standalone build: `Quat_QuatToMat`, `Vec_Normalise`,
`Vec_NormaliseLen`, `maybeAtan2` (adaptors that keep ECX and EDX for the original's callers) and
`MenuManager_GetStatus`. `Game_Draw` called the `View_CaptureScene` adaptor, which reads its viewer from EAX, with
the viewer on the stack; it calls `_View_CaptureScene`.

**2. x87.** `NIGHTFIRE_ACTION_X87` (CMake, on; forced off on 64-bit) is the standalone targets' `NIGHTFIRE_X87`;
actioninject keeps the instructions. Off, `Extended` is a double and every operation the C one (math/X87Ops.cpp),
`CIAsin` and the drones' route turn too, and the shared C runtime takes its C forms. The 13 recordings rebased onto a
32-bit build with it off (tools/demo/rebase_demos.sh) differ from the x87 recordings in no frame or a handful: the
C forms change almost nothing.

**3, 4. File structures.** Five loaders write addresses into the files they read: AIPath_Parse (`MAPNODE_tag.cel`,
`AIBoundLink_tag.nextA`/`nextB`), the skins (hashcodes made glists, `sAnimSkin_tag::riggedBody`/`discreteObj` point at
those words), the sequences (`sAnimRawSeq_tag.trackData`/`cursor`), the map (`bmpHeaderEntry.textureInfo`,
`ModelData.primitives`/`afterPrimitives`, `PALHEADER_tag.data`, a model's address in `celglist_tag.geom_idx` until it
is registered) and the sound banks (`SampleHeaderData.Address`/`psi_SampleHeader`). Those fields are `Ptr32`
(driving/platform/Ptr32.h: `T *` on 32-bit, a word of the low game window on 64-bit); every other file structure has
no address in it. `level_tag.spline` is a word too: the twenty placement structures lay their keys out at 0x2c after
it. The heap keeps the game's layout: `MemBlock.prev` is a word, so every block and allocation is the size it is on
32-bit, and the heap's words (`PtrHeap`, `QuickBlock`) decode through the window. The executable is linked at
0x400000 without ASLR on 64-bit (`nf_game_window`), and `GameWindowOutOfRange` is StandaloneMain.cpp's.

**5. Memory-only structures.** The 787 layout checks a 64-bit compile fails are `LAYOUT_32` (common/xbeClass.h): none
of them on a file structure. Integers holding addresses became the right types: the open file (`PsiOpenFile.data`,
`maybePsiFileRead`), `FileLastBlock`/`FileDiscard` (Mem_Shrink took the latter's address as a pointer's), the text
bank's address arithmetic and allocation, `CIRBUF_INFO`'s words (the received messages), the drones'
`stateParam` and `Drone_SM_SetState`'s argument (impacts), `MP_sendBotMessage`'s `extra`, `psiAsyncCreateBuffer`'s
result, the menu messages' arguments and answers (`intptr_t`/`uintptr_t` through every handler, control, send function
and tools/uihandler.py's generated switch; the same types on 32-bit) and `lastScriptResult`. GameFlow read the
cutscene viewer through offsets on an integer; it names `fadeState` and `fadeRate`. The D3D seam's two texture words
are `PtrTo32`; on x86-64 its calls have the one convention.

The probe (a 64-bit clang syntax check of every standalone source, in the nf-cross image) has no errors left and no
pointer-to-integer casts; MSVC x64 builds and links action_standalone and action_standalone_test.
