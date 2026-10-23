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
it. The heap's headers keep the game's layout: `MemBlock.prev` is a word, and the heap's words (`PtrHeap`,
`QuickBlock`) decode through the window. The allocations do not: about 30 sites ask for structures that hold pointers,
so on 64-bit the blocks are bigger and lie elsewhere (see the heap addresses below). The executable is linked at
0x400000 without ASLR on 64-bit (`nf_game_window`), and `GameWindowOutOfRange` is the driving engine's
(GameWindow.cpp, linked with the host layer, docs/action-portable.md), whose debug break reaches StandaloneMain.cpp's
crash report.

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

## Bring-up (22 October 2026)

The 64-bit action_standalone_test against the C baseline (the 13 recordings rebased onto a 32-bit build with
`NIGHTFIRE_ACTION_X87=OFF`, on the runner). What stopped it, in the order it was found:

| Found | Now |
|---|---|
| The heap walk looped: lists and allocations sized for 32-bit structures that hold pointers (InputEventList's 10 entries of 0x18, HitHeap's 0x50, ForcedList's 0xe4, the sensor and switch lists, a cel's 0x18 COLLDATA_tag, the rooms' `count * 4 + 4`) overran into the next block's header | `sizeof` (the same numbers on 32-bit) |
| The dynamic placements read the map's path table before it is filled (the original's: on 32-bit too the entries are stale and those placements are not made); a stale entry stopped the window check | m_paths a table of words, the size it is on the Xbox, copied unread |
| The placement structures laid over level_tag (doors, triggers, sensors, SS, hurt volumes, locks, copters, third cameras) held their spline as a `void *`, which moved every key after it: a door that starts open did not, and its channel stayed clear | `Ptr32<void>`, their checks unconditional again |
| A cel heads its objects' list as an object would (the first object's prevInCel is the cel): cel_tag's first 8 bytes were bytes where an object has two pointers, so unlinking the first object wrote past the cel's firstObject, and the lists turned into loops | two pointer slots; checks that the cel's firstObject, headPrev and attachments lie where an object's nextInCel, prevInCel and attachments do |
| psiAsyncReadFile added `start - byteOffset` (unsigned 32-bit, negative) to the buffer: 4 GB on | the difference signed |
| The D3D seam's slots are the Xbox's resource headers, which the backend reads as words (`Data`); their `void *` members moved them | `Ptr32`, their checks unconditional; D3D_UncachedAliasOf and the index binding decode words |
| psiDrawSkinObjectMatrix, psiGetTriList and a shadow test reached an object's fields through 32-bit offsets | the fields by name |
| DYNAMICSOUNDS spelt its list node's prev as four bytes of padding | a `prev` pointer, checked against LLNODE_tag's |
| NPCGlobals' drone list is an LLISTINFO_tag laid over NDrone2List onwards; its tail and entry size were padding, so on 64-bit the count the list kept was not NDrone2Count, and the drones never looked for each other (DroneVision_ProcessDroneSight) | the list's four fields named, checked against LLISTINFO_tag's |
| psiUseCamera read the viewer through a structure of its own, with the view matrix at 0x108: on 64-bit it read other bytes, so every character seemed far from the camera and its shadow (whose ground test takes a collision stamp) was not drawn; the stamp fell two a frame behind | viewer_tag |
| psiLight_SetLights read a light through a structure of its own, from 8 bytes in (its list node's two pointers) | light_tag, its 0x34 named `intensity` |
| The text box reached its label's sprite through a structure of its own, the text at 0x18 | sprite |
| Compare_MenuCtrl sorted Menu_FindControl's neighbours by a short at 4, the cost on 32-bit; on 64-bit the middle of the control's pointer, so the keypad's selection went to the wrong keys | MENU_NEIGHBOUR (ui/Menu.h) |
| View_AddSprite's glyph dump (DumpTextures) read the font and texture records through offsets | FONT_tag, CHARDATA_tag, TextureInfo |

The tools of the bring-up, in action_standalone_test: `NIGHTFIRE_DEMO_DUMP_OBJECTS=<frame>` also prints the switch
channels; `NIGHTFIRE_DEMO_WATCH_CHANNEL=<n>`, `NIGHTFIRE_DEMO_WATCH_OBJECT=<frame>,<n>` and
`NIGHTFIRE_DEMO_WATCH_DRONE=<frame>,<n>` put a hardware watchpoint on a channel, an object's curState or a drone's
next state and report each write with the code addresses on the stack; StandaloneMain.cpp's crash report and
`NIGHTFIRE_REPORT_AFTER` read RIP, and GameWindowOutOfRange's break is reported with the stack.

## Heap addresses

Drone_Update gave Env_Breath the drone's creation frame plus `(address & 0xfff) * 0x80`: each drone breathes on its
own phase of a two-second cycle, set by where its object lies in the heap. A breath makes a smoke object and draws
from the game's random generator (Gas_Init), so a breath on another frame changes the game from then on. On 64-bit
the objects lie elsewhere (the allocations are bigger), and the CastleExterior recordings parted from frame 58.

A shadow of the Xbox heap (3e786088) gave every host the Xbox's addresses: the game's allocator run again with each
allocation's Xbox size, all 13 recordings then matching on 64-bit. It was taken out on 23 October 2026: the phase now
comes from a hash of the drone's state machine id, the same on every host (docs/core-deviations.md), and the
recordings in which drones breathe were rebased (tests/demos/action/README.md). With that, the repository's set
(recorded on the x87 build) also matches every frame on the C builds: Linux x86-64 and macOS arm64 play it as it is,
so the C baseline set rebased into build/demos-c is no longer needed.

