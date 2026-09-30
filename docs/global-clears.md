# Global clears in the action engine

A memset of a constant address says exactly where a global struct or array starts and how big it is, because
MSVC inlines `memset(&thing, 0, sizeof(thing))` as `MOV EDI, &thing / MOV ECX, sizeof/4 / REP STOSD`. That
matters for the long-term plan: a global can move into the DLL (its `#define` at the game's address becomes a
definition) once every function that touches it is ours, and that can only be checked if our code reaches it
through a type of the right size rather than through `U32_AT(address)` somewhere in the middle of it.

`tools/global_clears.py` finds every such clear in the XBE (71 regions from 80 sites, September 2026) and the
hex literals in `src/action` that fall inside each one. `--overlaps` lists only the regions where our code
has more than the one definition. At the time of writing that is three, and in each the extra hits are
comments.

## What was fixed

Raw addresses inside a cleared region, now reached through the type:

- **Gfx** (`0x002c5750`, 0x39ce4 bytes): 95 `U32_AT`/`FLOAT_AT`/pointer-cast macros across `d3dSeam.cpp`
  and `game.cpp`. They are now fields of `GraphicsSystem` (`src/action/engine/Direct3D/GraphicsSystem.h`),
  which accounts for every byte except one block at +0x1894 that nothing reimplemented touches. The texture,
  vertex-buffer, index-buffer and overlay-quad slot tables, the immediate-mode buffers, and the light,
  fog and matrix state are all inside it. Several of the old comments said "not in the Gfx struct"; they
  are, Ghidra just had no field there.
- **Switch channels**: `Mission.cpp`'s mission flags (`U8_AT(0x001df19b)` and six more) are
  `switch_channels[0x63]` and so on, and the two times are `switch_channels_time[0x63/0x64]`. These are
  the hard-coded channels in [switch-channels.md](switch-channels.md).
- **glb_viewer_6** is `glb_viewer[6]`.
- **NPC globals** (`0x001e5630`, 0x142c bytes, cleared by `Drone_LevelReset`): the hostage count was a
  `U32_AT` at +0x19c. It is now `NPCGlobals.hostagesSaved`, in a struct of the cleared size (`NDrone2.h`).
- **Pointer-typed defines that were really arrays**: `Tanks` (8 pointers), `StringHeapLock` (a byte lock
  count per heap string, 256), `MusicEventList` (64 dwords), `MemStats` (7 dwords). Also `Tex`, which is
  2048 `TextureInfo*`: every original reader loads `[index*4 + 0x2abe80]`, while `Woman.cpp`'s WIP code
  indexed through the first pointer instead. `TextureInfo` is 0x58 bytes, the default entry that
  `maybePsiResetResources` clears at `0x002ae4f8` and points `Tex[0]` at.

The object code of every changed file was compared before and after with `dumpbin /disasm`. Everything
except `d3dSeam.cpp` and the WIP `Woman.cpp` function compiles to identical instructions. The five
`d3dSeam.cpp` functions that differ read and write the same addresses, with different register allocation
and one bound written as an index instead of an address.

## The regions

"Ours" is how `src/action` reaches the region. A blank means no reimplemented code touches it. A clearing
function marked * is reimplemented.

| Address | Bytes | Cleared by | Ghidra | Ours |
|---|---|---|---|---|
| 001d6a44 | 0x1c | AnimPreLoadInit | AnimFileSizes (AnimSizes) | |
| 001d7578 | 0x200 | AnimPreLoadInit | SkeletonTable (SkeletonInfo*[128]) | `SkeletonTable` |
| 001d7e90 | 0xa50 | bootup_bootup* | PTPDATA (sNightFireShared_tag) | `PTPDATA`, size asserted |
| 001dc980 | 0x20 | Car_Init* | Tanks (undefined4) | `Tanks[8]` |
| 001dee38 | 0x100 | Init_SwitchChannels* | switch_channels_prev (undefined[256]) | `switch_channels_prev` |
| 001def38 | 0x200 | Init_SwitchChannels* | switch_channels_MusicVars (short[256]) | `switch_channels_MusicVars` |
| 001df138 | 0x100 | Init_SwitchChannels* | switch_channels (undefined[256]) | `switch_channels` |
| 001df238 | 0x100 | Init_SwitchChannels* | switch_channels_hold (undefined[256]) | `switch_channels_hold` |
| 001df428 | 0x400 | Init_SwitchChannels* | switch_channels_time (undefined4[256]) | `switch_channels_time` |
| 001df8c0 | 0xc8 | Door_Init, Doors_Calc | DoorGroupsStates (undefined2) | |
| 001e5630 | 0x142c | Drone_LevelReset | NPCGlobals (bool), 85 items inside | `NPCGlobals` |
| 001e6184 | 0x800 | FUN_00030e70 | untyped (inside NPCGlobals, +0xb54) | |
| 001e6984 | 0x28 | Drone_PostLoad_Init | untyped (inside NPCGlobals, +0x1354) | |
| 001ee288 | ? | NDrone2_SeenAndAttacking | MusicVars +0x28 | |
| 001f6568 | 0x18 | bootup_bootup* and two others | GlobalVars (GlobalVars_t) | `GlobalVars` |
| 001f6580 | 0x58 | bootup_bootup* | GameState | `GameState`, size asserted |
| 001f65dc | 0x30 | bootup_bootup* | CheatInfo (CheatInfo_t) | `CheatInfo` |
| 001f661c | 0x2c | ResetMap_GameInit | glb_viewer (viewer_tag*[11]) | `glb_viewer` |
| 001fe6d0 | 0x158 | Input_Init | PlayerInputs[0] (PlayerInput_tag[4]) | `PlayerInputs`, size asserted |
| 001fe6e4 | 0xa0 | Input_ClearAllActions, Input_Update | PlayerInputs[0].fChannels | |
| 001fec80 | 0x100 | Txt_LanguageInit* | StringHeapLock (undefined4) | `StringHeapLock[256]` |
| 00219790 | 0x1000 | LS_LoadFromBuffer | InBufData (undefined1) | |
| 00223a60 | 0x1c | Mem_Init* | MemStats (undefined4) | `MemStats[7]` |
| 00224558 | 0x94 | Menu_GetFirstControl | backup.135 (undefined4) | |
| 00225080 | 0x34 | Menu_GetLevelBonuses | LevelBonusesForMenu (LevelBonuses) | |
| 00245240 | 0x40 | P_MPJOIN_Handler | untyped, 18 items including mpjoin | |
| 0025f1d0 | 0x938 | MenuManager_Init | manager (M_MANAGER[5]) | `manager` - see below |
| 0025fe38 | 0x23c | bootup_bootup* | MPSettings (MPSettings_t) | `MPSettings`, size asserted |
| 00260078 | 0x1600 | MP_Init | MPpickups (MP_PICKUP[2]) | |
| 00261678 | 0x110 (0x44) | MP_Init (MP_objectBeingDeleted*) | GoldenEye (GoldenEyeStruct; keys[0]) | `GoldenEye`, size asserted |
| 00261790 | 0x2e0 | MP_Init | ProtectionPlaces (SpawnPlace[8]) | `ProtectionPlaces` |
| 00261a70 | 0x88 | MP_Init | EsponageBase (MP_OBJ_EXT[2]) | `EsponageBase` |
| 00261af8 | 0x44 | MP_Init | Demolition (MP_OBJ_EXT) | `Demolition` |
| 00261b40 | 0x44 | MP_Init | Protection (MP_OBJ_EXT) | `Protection` |
| 00261b88 | 0x44 | MP_Init, MP_objectBeingDeleted* | Hill (MP_OBJ_EXT) | `Hill` |
| 00261bd0 | 0x88 (0x44) | MP_Init (MP_objectBeingDeleted*) | Bases (MP_OBJ_EXT[2]) | `Bases` |
| 00261d58 | 0x700 | MP_Init | SpawnPoints (MPSpawnPoint[64]) | `SpawnPoints` |
| 00262458 | 0x2e0 | MP_Init | BluePrints (SpawnPlace[8]) | |
| 00262738 | 0x230 (0x1e0) | bootup_bootup*, MP_Init | MPGame (MPGameStruct; players) | `MPGame`, size asserted |
| 00262948 | 0x20 | MP_Start | MPGame.radar_related | |
| 00262978 | 0x5c0 | MP_Init | GoldenEyeSpawns (SpawnPlace[16]) | |
| 00262f40 | 0x2e0 | MP_Init | DemolitionPlaces (SpawnPlace[8]) | `DemolitionPlaces` |
| 002633d8 | 0x220 (0x44) | MP_Init (MP_objectBeingDeleted*) | Uplinks (MP_OBJ_EXT[8]) | `Uplinks` |
| 00263640 | 0x100 | MP_Init | MPObjects (obj_tag*[64]) | `MPObjects` |
| 00263740 | 0x88 | MP_Init | Flags (MP_OBJ_EXT[2]) | `Flags` |
| 00263998 | 0x1000 | InitDrops | DropState (undefined1) | |
| 00278e70 | 0x230 | PlrStat_Init, PlrStat_ResetForMission* | PlrMissionStats (PlayerMissionStats[10]) | `PlrMissionStats` |
| 0029a14c | 0x30 | Sound_LoadMapSounds | untyped | |
| 0029a180 | 0x100 | Sound_Init, UpdateMusicalEvents | MusicEventList (undefined4[64]) | `MusicEventList[64]` |
| 0029b2d4 | 0x80 | Inflate_huffman | untyped | |
| 0029d7a0 | 0x444 | Env_Reset | untyped | |
| 002a0e68 | 0x2000 | maybePsiResetResources | d3dGeometryObjs (ModelData*[2048]) | |
| 002abe80 | 0x2000 | maybePsiResetResources | Tex (undefined4) | `Tex[2048]` (Woman.cpp, WIP) |
| 002ade8c | 0x3c | Graphics_Init_LowLevel | _MATRIX_002ade8c | |
| 002adf88 | 0x300 | maybePsiResetResources | untyped | |
| 002ae4f8 | 0x58 | maybePsiResetResources | untyped - the default TextureInfo | |
| 002ae598 | 0x14c0 | xboxInitSound* | AudioSystem | `AudioSys`, size asserted |
| 002ae8c0 | 0x1000 | maybeSoundShutdown* | AudioSystem.maybeVoices | through `AudioSys` |
| 002b0328 | 0x100 | xboxInitTextures | untyped | |
| 002b0c28 | 0x13a40 | FS_Init* | FileSystem | `FileSystem`, size asserted |
| 002c54e8 | 0x40 | FUN_000e3340 | untyped | |
| 002c5528 | 0x20 | FUN_000e3390 | untyped | |
| 002c5750 | 0x39ce4 | xboxInitGraphics* | Gfx (GraphicsSystem, 0x39ce5) | **owned**: our `Gfx` (d3dSeam.cpp), tagged `XBE_GLOBAL` |
| 002c6f84 | 0x54 | d3dSetup* | Gfx +0x1834..0x1887, the 21 render-state caches | through `Gfx` |
| 002ff498 | 0x2a4 | xboxInitInputDevices* | XboxInputs | `XboxInputs`, size asserted |
| 002ff73c | 0x34 | maybeBackgroundMovieCleanup | untyped, 9 items including fmvDecoder | |
| 002ff778 | 0xc00 | GetPTPData | LaunchInfoData (undefined4) | |

The clears the scan skips: `logError` fills a buffer with spaces, `UpdateDrops` copies rather than clears,
and the CRT's own clears are left out.

`manager` is 5 x `M_MANAGER`, so `M_MANAGER` is 0x1d8 bytes. `dev` still declares it as 0x1c0; the fix is on
the `ui-reimplementation` branch.

## For Ghidra

Applied through the MCP (30 Sept 2026): MPpickups `MP_PICKUP[64]`, Tanks `obj_tag *[8]`, StringHeapLock
`byte[256]`, MemStats `undefined4[7]`, and sized arrays for the named-but-undersized DoorGroupsStates
(`undefined2[100]`), InBufData and DropState (`undefined1[4096]`), backup.135 (`undefined4[37]`) and
LaunchInfoData (`undefined4[768]`).

The struct work goes through `ghidra/NightfireStructs.py`, because this MCP server's struct tools rename
fields (Hungarian prefixes) and cannot rename them back. It reads
`driving-symbol-matching/results/structs/action-global-clears.json` when default.xbe is open:

- **GraphicsSystem** rebuilt at 0x39ce4 bytes from `GraphicsSystem.h`, keeping Ghidra's own names where it
  had them. The overlay-quad, vertex-buffer and index-buffer slot tables become arrays of new
  `D3DOverlayQuadSlot`, `D3DVertexBufferSlot` and `D3DIndexBufferSlot` types. The `float[16]` at +0x39b38 is
  gone: the light block is a `GfxLightConstants` at +0x39b2c. `characterLightIntensity` at +0x39c28 was the
  start of shader constant 0x75, which holds the intensity in its third float.
- **D3DTexture** gains refCount, nonSwizzled and mipChainBytes.
- **NPCGlobals_t** (0x142c bytes) applied at NPCGlobals, with NDrone2List, NumDrones, the cover nodes and
  hostagesSaved as fields.
- **TextureInfo** (0x58 bytes), applied as `Tex` (`TextureInfo *[2048]`) and at 0x002ae4f8
  (`DefaultTextureInfo`).

The same file also types the regions that were untyped. Each turned out to be one struct or array, or a few
adjacent arrays; none is unrelated globals cleared together:

- **NPCGlobals_t +0xb50 / +0xb54** (001e6180, 001e6184): `numAwarePoints` and `awarePoints`, a
  `DroneAwarePoint_t[64]` (0x20 each: type, obj, pos, cel, radius). FUN_00030e70 is the PS2 build's
  `Drone_BuildDynamicAwarePoints`: called from `Drone_InitComms`, it rebuilds the list every frame from
  occluders and live explosive projectiles. `DroneFunc_HandleExplosives` sends message 0x21 (flee) to drones
  within the radius of an explosive that can see it.
- **NPCGlobals_t +0x1354** (001e6984): one `AIEmitter_tag`, `safetyEmitter`. `Drone_PostLoad_Init` allocates
  it, and `DroneMove_FindSafetyFromScaryPosition` uses it as scratch to find a safe spot.
- **00245240**: `mp_join_slots`, `MPJoinSlot[4]` (joined, ready, team, skin), one per controller on the
  multiplayer join page. The "mpjoin" label was slot 0's `ready`. The field at +0xc is the controller port:
  `Menu_AllJoinedPlayersReady` writes it, and `P_MPCONFIRM` copies it into `PlayerInputs[].controllerPort`.
- **0029a14c**: `GMapSoundActive`, `char[50]`, one "playing" flag per map sound (HandleMapSoundAllocation).
  The clear is 0x32 bytes: 12 dwords and a word. The name, and `GMapSoundHandle` (`DYNAMICSOUNDS *[50]` at
  00299a20), come from the PS2 build.
- **0029b2d4**: elements 1..32 of `huffman_bl_count`, which is `uint[33]` from 0029b2d0. `Inflate_huffman`
  is zlib's inflate_table with MAX_BITS 32: it zeroes count[1..32] and counts 4-bit code lengths.
- **0029d7a0**: `SkyObjList`, `SkyObj[21]` (0x34 each). `View_AddSkyObj` (placement type 42) fills a slot:
  the sky model and its lightning-flash variant, position, rotation, and the flash and thunder timers.
  `View_DrawSky` draws them, and `Env_Reset` clears them per level.
- **002adf88**: `ParticleOverlayRing`, `ParticleOverlayBuffer[64]` (overlay slot, vertices, frames to live),
  with its write index `ParticleOverlayRingNext` at 002adf80. `psiDrawParticleList` puts each particle
  batch's vertices in an overlay buffer that must outlive the frame. FUN_000dcc60, called every frame from
  `psiPreDraw`, frees each one three frames later. The PS2 build has no such ring.
- **002b0328**: `FontCharToGlyph`, `uchar[256]`, filled with 0xff ("no glyph") rather than zero. Then
  `FontGlyphU` and `FontGlyphV` (`int[256]` each) follow, and `FontTexture` at 002b0324: the built-in 6x6
  font that `ShowFatalErrorScreen` draws with.
- **002c54e8** and **002c5528**: `SaveNameWide` (`wchar_t[32]`) and `SaveNameAscii` (`char[32]`), the static
  result buffers of FUN_000e3340 and FUN_000e3390, which convert save names between ASCII and wide for the
  "u:\" save-game wrappers and `SaveEnum_GetNextEntry`. They have nothing to do with the vertex shader
  handles that follow them.
- **002ff73c**: `BackgroundMovie`, a `BackgroundMovieState` (0x34 bytes): the double-buffered 640x480 XMV
  player (frame memory, texture slots, surfaces, decode and display index, `fmvDecoder`, `isPlaying`, the
  sound stream). Typing it turns the `fmvDecoder` and `MovieSoundStreamHandle` labels into fields.
  `maybeDecodeMpgAudio` is really the per-frame "decode and draw the movie" function.

Function names applied in Ghidra (30 Sept 2026), each PS2 match checked against its body: FUN_00030e70 = `Drone_BuildDynamicAwarePoints`,
FUN_0004a620 = `AINetwork_SetLinksFlagInCircle`, FUN_0004a530 = `AINetwork_ClearLinkFlags`, FUN_0004a900 =
`AINetwork_InitEmitter2` and FUN_0004a070 = `AINetwork_Emitter_GetNodeAtDistance`, all from the PS2 build.
These are invented, with "INVENTED NAME" plate comments: FUN_00030e10 `Drone_AddDynamicAwarePoint`, FUN_000dcc60 `psiAgeParticleOverlayRing`,
FUN_000e3340 `AsciiToWideSaveName` and FUN_000e3390 `WideToAsciiSaveName`.
