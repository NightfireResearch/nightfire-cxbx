# The GameCube build's debug overlays

The GameCube action executable (`/GameCube_US/Nightfire.elf` in Ghidra) keeps a developers' **debug tweak menu**
and the overlays it switches on: a heap map, heap and render statistics, a texture viewer, GPU performance counters,
a list of the entities drawn, and a frame drawn with the coordinates cheat. The Xbox and PS2 builds have none of it.
This lists what each overlay shows, how the GameCube turns it on, the data it needs and whether our engine has that
data, and what we reimplemented (`src/action/devtools/GcDebugOverlays.cpp`).

Addresses are the GameCube's unless marked. Names starting `Debug_`, and `Mem_DrawMap` / `Mem_CountBlocks`, are
invented (each has an "INVENTED NAME" plate comment in Ghidra); `GXPeekARGB` / `GXPokeARGB` are the Dolphin SDK's.

## How it is turned on: it isn't

Every overlay is drawn from the GameCube's `psiPreDraw` (0x8002d8a0), each behind a byte or word flag in `.sdata`
that is 0 in the ELF. The only code that writes those flags is the tweak menu itself, through the variable pointers
in its table, and the menu is drawn (`Debug_TweakMenu` 0x8000b0a4) only while `dbg_TweakMenuOn` (0x8030e24a) is
set. **No instruction in the retail ELF writes 0x8030e24a** (it is read once, in `psiPreDraw`; nothing loads its
address). So on a retail disc the whole system is dormant: it can be reached only by writing memory (a debugger,
an emulator's memory editor, a cheat device). There is no button combination, cheat code or menu page for it.

The one exception is the safe frame, which follows the developers' cheat page's coordinates cheat
(`switch_SHOW_COORDS`); that page is itself unreachable on a retail disc (docs/ui/framework.md, page 0x40000007).

### The tweak table

`DebugTweakTable` (0x801bb040, "------TOP-------" to "-----BOTTOM-----" at 0x801bfaa0, then "Root Table"):
0x88-byte records:

| Offset | Field |
|---|---|
| 0x00 | name, `char[0x40]` |
| 0x40 | type byte (0 a byte flag; 0x14 a one-shot button; 3, 5, 6, 8, 0x0b numeric types; type names "bool8", "bool16", ... "f32", "vecf32", ... at 0x801ba610, 0x28 apart) |
| 0x44 | the variable's address |
| 0x50 | minimum (`double`) |
| 0x58 | maximum (`double`) |
| 0x60 | a sub-table, for entries that open one |

About 180 entries, in groups: memory card / language / DVD tests; streaming and sound mixing; **Frame Skip, Show
Textures, Textures Anim, Reset Malloc Stats, Reset Show GList, Gamma ScreenGrab, Enable Movie** (frame grabbing:
"GRABBING AT    FPS", `c:\Grabs\`), **Font Switch, Don't PSI Gfx, Max Entities Display, Show Ent.Names From, Draw
Entities, Draw Sprites, Enable Mem Stats, Display Mem Map, Mem Map Cursor, Mem Map Size, Display Stats, Display
Perf, Reset Hi Stats, Accurate GX Counter, Camera Translate**; anim cache and skinning switches; a particle editor
(Particle Export / Enable and its V/C/G parameters); video filter, VI/XFB/EFB sizes, texture filtering, wireframe,
clip planes, clear colour; lightmap and bump map; fog; lights; two pads' raw readouts. The menu is driven from a debug
pad state at 0x802204a4 (sticks move the menu windows, buttons step into sub-tables and change values;
`Debug_TweakMenu` → 0x80008698).

The variables of the entries below (all 0 / off in the ELF unless given):

| Entry | Variable | Type, range |
|---|---|---|
| Show Textures | `dbg_ShowTextures` 0x8030e06c | s32, -1..2048 (-1 off) |
| Textures Anim | 0x8030e34c | s32, -1..256 (-1 off) |
| Reset Malloc Stats | 0x8030e062 | button |
| Show Ent.Names From | 0x8030e060 | s16, -1..384 |
| Enable Mem Stats | `dbg_EnableMemStats` 0x8030e6f0 | flag |
| Display Mem Map | `dbg_DisplayMemMap` 0x8030e063 | flag |
| Mem Map Cursor | `dbg_MemMapCursor` 0x801c179c (x), 0x801c17a0 (y) | 0..640; 64, 64 |
| Mem Map Size | `dbg_MemMapSize` 0x801c17a8 (w), 0x801c17ac (h) | 32..512; 128, 384 |
| Display Stats | `dbg_DisplayStats` 0x8030e249 | flag |
| Display Perf | `dbg_DisplayPerf` 0x8030e24b | flag |
| Reset Hi Stats | 0x8030e2d1 | button |

## The overlays

### 1. Heap map - "Display Mem Map"

`Debug_DrawMemMap` (0x8000b130), from `psiPreDraw`, calls `Mem_DrawMap` (0x800574f0) and then draws the cursor and
a label.

- **Map.** The game heap (`PtrHeap`, `HeapByteSize`: 0x13b3000 bytes on the GameCube) as a grid of
  `w x h` cells (Mem Map Size, each rounded down to a multiple of 4; default 128 x 384) at screen (32, 32), each cell
  `HeapByteSize / (w*h)` bytes (about 420 on the GameCube). It walks the heap blocks in address order and, for each,
  pokes its colour into every cell from `offset / cellBytes` to `ceil(end / cellBytes)` with `GXPokeARGB` (straight
  into the frame buffer), so a later block wins a shared cell.
- **Colours.** Free blocks 0xff000000 (black). A used block takes `MemMapColours[type]` (0x801a7328, 80 words,
  type = the block's flags >> 8, a type over 0x4f counting as 0). Almost every entry is 0x80808080 (grey); only the
  GameCube's own types stand out: malloc_xyzmiscqzyx white, malloc_Gamecube red, malloc_anim_seq_hdr green,
  malloc_physics purple, malloc_load_data blue, and the GC_* types (anim cache white, display lists magenta, skins
  cyan, PC lists yellow, big buffer blue, anim matrices pale yellow, card dark red, USB2EXI green, textures pink,
  sound sea green).
- **Cursor.** Mem Map Cursor (x, y) is a cell of the grid; the byte it stands for is
  `(x + y*w) * cellBytes`. The block containing that byte flashes - four frames in eight (`psiPreDraw`'s frame
  counter & 4) its colour becomes `(~colour & 0xf0f0f0f0) >> 3`. The cursor itself is five pixels in a plus, each
  inverted in place (`GXPeekARGB`, `~c | 0xff000000`, `GXPokeARGB`).
- **Label**, 4 pixels right and down of the cursor, in the 8-pixel debug font: the block's type name (from a copy of
  the type names in the tweak data, 0x801bfb7c, 90 bytes apiece), then "0x" and the cursor's address in hex, then the
  block's size in decimal.

The cursor block found is the previous frame's (the draw uses the cursor before this frame's copy is taken).

### 2. Heap counts and render statistics - "Enable Mem Stats", "Display Stats"

`Mem_CountBlocks` (0x800573a8) does nothing unless Enable Mem Stats is on. Then it walks the heap and returns the
free blocks' count and bytes and the used blocks' count and bytes, and fills per-type counts and bytes (80 each, at
0x8027c69c and 0x8027c7dc; nothing displays these two).

"Display Stats" (`psiPreDraw`: `Debug_DrawTimings` 0x8002d34c, then `Debug_DrawStats` 0x8002cc48) draws, on a
640-pixel line (x scaled by the screen width / 640):

- timings: frame and draw times with their highs (doubles 0x8030f518-0x8030f540; Reset Hi Stats clears the highs;
  values over 10 reset), coloured when the frame runs long; "LC Loads :" / "LC Stores:" (locked cache DMA counts);
- "SkVerts", "Verts", "3D Tris", "Strips": vertices skinned and drawn, triangles and strips this frame;
- "ARAM:" (bytes in ARAM), "CMPR:", "RGBA8:", "RGB5A3:" (textures loaded in each GX format, counted by
  `psiCreateMapTextures` 0x8002fd0c), "Textures:" (texture bytes) and "-S:" (of those, the static ones: loaded while
  the malloc type is 1), "Entities:" (entity graphics), "+DL:" (display-list buffer used), "-S:" (static entity
  bytes), two further counts (0x8030e27c, entity primitives; 0x8030f448);
- **"FC    FB    AC    AB"** at (442, 218) and the four numbers from `Mem_CountBlocks` under it at y 228: free
  count, free bytes, allocated count, allocated bytes.

### 3. Texture viewer - "Show Textures", "Textures Anim"

`Debug_ShowTextures` (0x8000b414), from `psiPreDraw`, while Show Textures is 0 or more and below the number of
texture headers loaded (0x8030e284):

- sorts the loaded texture records (0x80233fc0, 0x24 bytes each; "DT" duplicates left out) by their pixel bytes
  (all frames), largest first, keeping load order among equals (an insertion sort into a list it `Mem_Malloc`s on
  the game heap every frame, with type 0x16);
- shows 12 of them from index Show Textures, a 64-pixel row each (row `i` at y = 64i): at (32, y+32) the texture's
  name (inline in the GameCube header at +4), "Mips:" and the level count at (80, y+40), width and height at
  (32, y+48) and (64, y+48), bytes at (32, y+56), the GX format's name ("GX_TF_I4" ... "GX_TF_CMPR", 0x801c74d0) at
  (32, y+64);
- draws the texture itself at its own size at y+32 and x = `128 + 48i + 64 sin(a + 96 + y)` degrees, `a` growing
  by 0.07 per texture drawn (wrapping at 360), so the column sways; an animated texture shows frame
  `(frames drawn / 32) % frame count`.

"Textures Anim", when not -1, forces every animated texture in the scene to that frame (read by 0x8002e7b4 and
`maybe_psiDrawClonedObject` when they pick the frame).

### 4. GPU performance counters - "Display Perf"

`Debug_ReadPerfCounters` (0x8000c2a8) and `Debug_DrawPerfCounters` (0x8000c3e8, at (0x24, 0x38)): the GX hardware
metrics - setup/transform counters "0_VERTICES" ... "0_TRIS_2CLR" and texture/vertex-cache counters "1_TEXELS" ...
"1_CLOCKS" (one of each sampled per frame, cycling through 26 and 22), pixel counts "top_pixels_in" ... "copy_clks",
memory requests "cp_req" ... "fi_req", and vertex cache hit counts "GX_VC_POS CMS" ... "GX_VC_TEX1 CMS". They come
from the Flipper's counters through the GX metric calls (0x80187ea8, 0x801881e8, 0x8018805c, 0x801882f8).

### 5. Entity names - "Show Ent.Names From"

`Debug_ShowEntityNames` (0x8000af64, from `Debug_TweakMenu`): from index N, up to 144 of the entities drawn this frame
(the list at 0x80244fcc, count 0x8030e2b4), index and name, in three columns of 48 lines; "Entity Geometry Mem" and
its total (0x8030e2b8) at (220, 440).

### 6. Safe frame with the coordinates cheat

`Debug_DrawSafeFrame` (0x8002c9ec), from `psiPreDraw` every frame: while `switch_SHOW_COORDS` (0x8030e650, written
by `C_CHCHCOORDS_Handler` 0x80121a04, the developers' cheat page's checkbox) is set, a one-pixel 0x80ffffff rectangle
poked into the frame buffer: x 8 to 632 at y 40 and y 440, y 40 to 440 at x 8 and x 632. The cheat's HUD readout
("%.2f %.2f %.2f\nFPS %.2f", 0x800e9ba4) is in the Xbox build too; the frame is not.

### 7. Other tools in the table

- **Particle Export** (`Debug_ParticleExport` 0x8000ac80): writes the particle editor's parameters as C to
  `c:\particle_gen.txt` on the host.
- **Reset Malloc Stats** (`Debug_ResetMallocStats` 0x8000b10c): a loop over the 80 types with an empty body - the
  reset itself is compiled out.
- Frame grabbing ("Enable Movie", "Gamma ScreenGrab"), and rendering switches (wireframe, filters, fog, clip planes,
  draw entities / sprites) that act inside the GameCube's renderer.

## The PS2 and the Xbox

- **PS2 ACTION.ELF** (symbolised): no tweak table, none of the overlay strings, and its `psiPreDraw` (0x121bf0)
  calls no debug drawing. `Mem_PrintAllInfo` (0x1d0588) still walks the heap for its free-block sizes but prints
  nothing. Only the memnames table remains.
- **Xbox default.xbe**: none of the tweak or overlay strings; `psiPreDraw` (ours in `engine/psiGraphics.cpp`)
  draws nothing of the kind. What survives is data: memnames (`Mem_Info`, `MemInfo`), and
  the texture statistics `psiCreateMapTextures` still counts - per-format counts and bytes (formatType 1, DXT2-5,
  8-bit, other) and `EntityGfxStat_2adf20` from `psiCreateEntityGfx` (`engine/psiGraphics.cpp`) - which nothing reads:
  the Xbox's own "Display Stats" inputs, with the display compiled out.

## Our engine: data and recommendations

Per docs/modding.md these are not part of the shipped Xbox game, so they belong in a built-in first-party module
(a developers' module, off in both profiles). Until the module framework exists they live in a devtools file behind
settings.ini switches, as below; when it exists, `GcDebugOverlays_Draw` becomes the module's "after psiPostDraw's
blur" hook and the one call in `psiPostDraw` goes.

| Overlay | Data in our engine | State |
|---|---|---|
| Heap map | yes: the same heap and block headers (`memory.h` `MemBlock`, `PtrHeap`, `HeapByteSize`, 40 MB) | **implemented** |
| Heap counts (FC FB AC AB) | yes | **implemented** |
| Texture viewer | mostly: `Tex[]` up to `NumXboxTexLoaded`, width, height, levels, format, bytes, frames. No names: the Xbox headers have none (the GameCube's are inline at +4), so a row shows the `Tex[]` index | **implemented** |
| Safe frame | yes: `switch_SHOW_COORDS` | **implemented** |
| Rest of Display Stats | partly: the Xbox's texture counters and `EntityGfxStat_2adf20` (static in psiGraphics.cpp: export them), view counts (`Tots`). ARAM, GX formats, display lists and the locked cache are GameCube-only | recommend: a later devtool with the Xbox's own counters under the GameCube's layout |
| Textures Anim | yes, but it changes what the game draws: it would need a hook in the texture frame choice | recommend: a module hook (replace), not a devtool |
| Entity names | partly: the drawn objects' celglist names (as MenuProbe's `objlog` / `gfxinfo` find them); no per-frame list is kept | possible later |
| GPU performance counters | no: Flipper hardware | not portable; the D3D9/SDL GPU backends' own timing (PerfLog) is the counterpart |
| Tweak menu | no: the variables are the GameCube renderer's | not worth porting as such; the module settings page will cover our own switches |

## What we implemented

`src/action/devtools/GcDebugOverlays.cpp`, called from `psiPostDraw` just before the swap (one devtools call in game
code, as with `Devtools_Tick`; `standalone/NoDevtools.cpp` answers it with nothing, so the standalone build has none
of this). Off unless settings.ini's `[Settings]` asks:

| Setting | Effect |
|---|---|
| `GcMemMap=on` | the heap map at (32, 32) with cursor and label |
| `GcMemMapSize=w,h` | default 128,384 (rounded down to fours) |
| `GcMemMapCursor=x,y` | default 64,64. In the game, Ctrl+arrow keys move it a cell, Ctrl+Shift+arrows eight |
| `GcMemMapColours=all` | a colour per type instead of the GameCube's table, which leaves every Xbox type grey |
| `GcMemStats=on` | "FC FB AC AB" and the four numbers, where Display Stats has them |
| `GcShowTextures=N` | the texture viewer from the Nth largest texture; Ctrl+PageUp / Ctrl+PageDown move 12 |
| `GcSafeFrame=on` | the frame, while the coordinates cheat is on (MenuProbe: `cheat coords`) |

Differences from the GameCube, all from drawing through the game's sprites rather than poking the frame buffer:

- The map's cells are drawn as untextured sprites (texture 0, as `psiFadeView` draws), a run of a block's cells per
  row; the cursor is a white plus, not inverted pixels. The GameCube's colours' alpha is ignored, as the frame buffer
  ignores it.
- Text is the game's font `FontTable[1]` scaled to 8-pixel lines; it has no underscore, so type names show
  a space for it.
- Nothing is allocated from the game heap (the GameCube's texture viewer took its sort list from it every frame) and
  no game state is written, apart from the font code's own statics while text is drawn.
- The texture viewer's rows name the `Tex[]` index; formats are the Xbox's (A4R4G4B4, A8R8G8B8, A1R5G5B5, DXT1-5);
  bytes are `frameBytes * numFrames`. An animated texture runs at the game's sprite animation rate.
- The highlighted block is this frame's cursor block, not the previous frame's.

With nothing switched on, `GcDebugOverlays_Draw` reads settings.ini once and returns. The build is not byte-identical
to one without the feature: `psiPostDraw` gains the call, `NumXboxTexLoaded` (psiGraphics.cpp) is no longer static,
and `MemBlock` moved from memory.cpp to memory.h (no code change). Game behaviour is unchanged.

### Checked on the test runner

Runner slot 11, `action.exe -level CastleCourtyard`, frames by `DumpEvery=300`:

- heap map and counts, GameCube colours: about 10.5 MB free at the low end of the heap (black), the rest grey;
  2400 used blocks, 30,751,456 bytes; 85 free blocks, 11,191,572 bytes;
- heap map with `GcMemMapColours=all`: nearly all of the used heap is one type, **malloc_Xbox** (the Xbox layer's
  allocations, among them the files `psiFile.cpp` loads and the sound buffers of `allocateXboxSpecialMemory`), with the other types in a thin band
  above the free space;
- texture viewer: the largest textures first (512 x 256 and 256 x 256 A8R8G8B8), drawn at full size, swaying;
- safe frame: the rectangle and the coordinates readout together, after a MenuProbe `cheat coords`.

## Ghidra (GameCube) names added

Functions: `GXPokeARGB` 0x80180e4c, `GXPeekARGB` 0x80180e28 (SDK), `C_CHCHCOORDS_Handler` 0x80121a04 (the handler
table's 0x1000001a, as the Xbox names it); invented, with plate comments: `Mem_DrawMap` 0x800574f0,
`Mem_CountBlocks` 0x800573a8, `Debug_DrawMemMap` 0x8000b130, `Debug_ShowTextures` 0x8000b414, `Debug_DrawStats`
0x8002cc48, `Debug_DrawTimings` 0x8002d34c, `Debug_ReadPerfCounters` 0x8000c2a8, `Debug_DrawPerfCounters`
0x8000c3e8, `Debug_TweakMenu` 0x8000b0a4, `Debug_ShowEntityNames` 0x8000af64, `Debug_ParticleExport` 0x8000ac80,
`Debug_ResetMallocStats` 0x8000b10c, `Debug_DrawSafeFrame` 0x8002c9ec.
Data: `dbg_TweakMenuOn` 0x8030e24a, `dbg_DisplayStats` 0x8030e249, `dbg_DisplayPerf` 0x8030e24b,
`dbg_EnableMemStats` 0x8030e6f0, `dbg_DisplayMemMap` 0x8030e063, `dbg_ShowTextures` 0x8030e06c, `dbg_MemMapCursor`
0x801c179c, `dbg_MemMapSize` 0x801c17a8, `MemMapColours` 0x801a7328, `DebugTweakTable` 0x801bb040,
`switch_SHOW_COORDS` 0x8030e650.
