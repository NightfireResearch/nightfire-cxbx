# Audio, level loading, saves and scripting

Four engine services the action engine (`default.xbe`) builds the game on: the game side of audio, the level
loader, the save/mission-state blocks, and the cutscene/level script player. Reviewed from the Xbox binary in
Ghidra (decompiles, xrefs and table bytes; every address here is the Xbox one), with the PS2 build's names as a
guide, and against our code in `src/action`. Status counts come from the coverage list of 1 Oct 2026 (R = ours,
D = dead now, L = live original).

Evidence is marked where it matters: **(verified)** means read from a decompile, disassembly or table bytes for
this document; **(inference)** means reasoned from those and not tested; names marked *(invented name)* are not
in Ghidra or the PS2 symbols.

| part | functions | ours | dead | live | done by bytes |
|---|---:|---:|---:|---:|---:|
| audio, game side (0xc4c40-0xcc4c0) | 137 | 14 | 1 | 122 | 4% |
| platform sound (psiSFX/dsnd, 0xe09c0-0xe1e70) | 57 | 28 | 0 | 29 | 75% |
| FMV playback (0xe8a00-0xe8e30) | 5 | 0 | 0 | 5 | 0% |
| loader (ResetMap, Loader, parsemap, psiLoadData group) | 58 | 22 | 3 | 33 | 51% |
| platform files (FS, error screen, save files) | 43 | 7 | 8 | 28 | 48% |
| save/mission state (LS_*, 0x6eb20-0x6fa30) | 20 | 0 | 0 | 20 | 0% |
| scripting (Script_*/SP_*, 0xc1190-0xc4c40) | 57 | 29 | 1 | 27 | 40% |

Two filing errors in the subsystem map, since fixed in `tools/subsystems_action.txt` (the counts in this document
are from before the fix): the three `SS_*` functions and `FUN_000cb2a0` are **not audio** - SS is the "simple script" path-animated object (placement type 226,
`SS_Create` 0xcb180; see the plate comment and `objects-logic.md` on the `blender-exports` branch) - and
`FUN_000c4b70`/`FUN_000c4bc0`, filed under scripting, are audio (they keep a recently-started-sound list that
`SFXStart3D` writes and `SFXUpdate` counts down).

## Where they sit

```
GameFlow_Main (0x6aca0)
  state 3 "loading"  -> ResetMap_Load (0xbfb60) ... LoaderLoad x N -> LoaderProcess -> parsemap / Script_Load / AnimLoadFile / MenuManager_Load
  state 6 "movie"    -> psiStartBackgroundMovie -> BackgroundMoviePlayFile (XMV decoder) ... maybeDecodeMpgAudio per frame
  state 4 "in game"  -> Game_Run (0x6aa90) -> ... SP_Update -> Script_Update -> Script_Run / Script_Interp
                                              -> Sound_UpdateListeners -> HandleMapSoundAllocation, Sound_UpdateSounds, SFXUpdate
                                                                                         SFXUpdate -> UpdateMusicalEvents -> UpdateMFX_<level>
  front end          -> Menu_UpdateMessageBox (0x757d0) -> LS_Save / LS_Load / Menu_EnumSaves -> psiSaveData / psiLoadData
                                                        -> XBox_DoSaveFlow (0x8f3f0, the message-box UI)
```

The game-flow state numbers (3, 4, 6, 9 for a driving level) are read from `ResetMap_LevelToLoad` and
`ResetMap_Load` **(verified)**; their names here are descriptive.

---

## 1. Audio, game side

### Purpose

Everything between gameplay code ("play this sound here", "the player is under attack") and the platform voice
layer: a handle layer for gameplay, a Eurocom sound engine (SFX items, voice handles, banks, reverb environments,
listeners), a streaming engine for music and speech, and per-level music logic.

### How it works

Three layers, top to bottom **(verified)**:

1. **`Sound_*` - gameplay handles.** A `DYNAMICSOUNDS` (0x48 bytes; `src/action/sound/Sound.h`) from a pool of
   0x100 on `DynamicSoundList` (DLIST at 0x29a128, set up by `Sound_Init` 0xcb700). `Sound_Play3D`/`Sound_PlayExt`
   only fill a record with `playbackState` 0; nothing is heard until `Sound_UpdateSounds` (0xcc0c0) next frame calls
   `Sound_ReqestPlaySfx` (0xcb580, record in EDI), which culls distant 3D sounds (beyond 1.1 x outer radius, single
   player only, for sounds flagged in `SFXOutputData`) and calls `SFXStart`/`SFXStart3D`. States: 0 requested,
   1 playing, 2 stop requested, 3 freed. `SFXOutputData` (0x60d entries of 0x18 at 0x182b80) gives per-id default
   radii, an alertness value (drones hear sounds through it), subtitle duration and loop flags.
   `Sound_DoSubtitle` (0xcbd10) raises subtitles. Map ambient sounds (level block 0x28) are `Sound_LoadMapSounds`
   and `HandleMapSoundAllocation`, both ours.
2. **`SFX*` / `ES_*` - the Eurocom sound engine.** An `SFXItem` list (`BaseSFXActive`, free list at 0x27aedc).
   `SFXStart3D` (0xcaa90) linearly searches every loaded bank's `SFXHeader` table for the hash, links an item and
   calls `maybeDXSoundPlay` (0xca050) / `StartSample` (0xc9e20), which take voice handles
   (`ES_RequestVoiceHandle` 0xc97f0, `ES_VoiceHandleData`). Items may be multi-sample, polyphonic or cued
   (`CuedSamples`, `SetCueTimer`). `SFXUpdate` (0xcad40) runs once a frame: environment (`SFXUpdateEnvironment`),
   ducking (`ES_UpdateDucker` 0xc8d00), music (`UpdateMusicalEvents`), fades, streams (`SFXUpdateStreams`), then
   per item: end detection (`HasSampleEnded`), cue advance, 3D position (`ES_GetMyPos` 0xc5760) and
   `psiUpdateSound` per voice, NIS pause/unpause, deletion (`KillSFX`). SFX id 0x172 is a pseudo-sound: starting it
   sets `SfxNisMusicTrigger` and plays nothing **(verified)**.
3. **Banks.** `SFXInitialise` (0xc9d60) reads `SBINFO.SBI` into `SBInfoList` (0x96 bank ids). `SFXLoadCorrectBank`
   (0xcbdf0, at level load) maps the level hashcode to one bank index by a hard-coded switch (all multiplayer maps
   share bank 0x16; the menu alternates 0xc/0x38) and calls `LoadSoundBank` (0xc5440), which loads
   `<path><language>/SB_<n/8>/SB_<n>.SFX/.SHF/.SBF` (`.SHG` is formatted but not loaded) through `psiSFXLoadFile`
   and relocates the sample headers. Language banks: `UpdateLanguageName`, `SFXSetLanguage`.

**Listeners and environments.** `Sound_UpdateListeners` (0xcc1c0, called from `Game_Run`) sets one listener per
viewer (split screen: up to four), or viewer 4 when a cutscene camera is active; writes `GlobalListenerPos`
(0x29a140); takes the reverb environment from the listener's room cel (`cel+0x88`, with flag 0x10000000 or an index
above 0x13 passing 1 as the second argument - underwater, by inference) into `SFXSetEnvironment`; then runs
`HandleMapSoundAllocation`, `Sound_UpdateSounds` and `SFXUpdate`. At the DSOUND boundary the environment is only a
per-voice I3DL2 send level; the game never sets a listener reverb (docs/audio-inventory.md).

**Streams.** `Streams[]` (records of 0x3fc bytes ending at 0x298e74), opened by `SFXInitialiseStream` (0xc8fa0)
through a 64-entry request ring, fed by `ES_StreamAsyncReadFile` -> `psiAsyncReadFile` from
`<language>/STREAMS/STREAMS.BIN` (speech) and the `MUSIC\` files, played through `psiStream*`. Music streams carry
**markers** (`BaseMusicMarker`, `BaseMusicMarkerStart`; positions in 0x48-byte blocks): `UpdateMusicMarkers`
(0xc6970), `ES_CheckFileBuffersForJumpMarkers` (0xc6db0) and `SFXJumpToMusicMarkerUpdate` (0xc8b40) implement
"jump to section N at the next legal marker", with an instant variant. `ES_CheckEndMarker` pauses a voice on
underrun; `SFXUpdateStreams` (0xc9030, 1984 bytes) is the read/transfer pump.

**Music logic - data table plus hand-coded per-level code.** The question was whether the 21 `UpdateMFX_*`
functions are data-driven. They are **hand-coded** **(verified)**: each is a small state machine of its own reading
`MusicEventList` slots and private counters (`MusicTemp1..9`, `WhichAttack_*`) and writing `MusicLastJumpRequest`
(a marker section number, e.g. 3, 5, 7, 9, 0xb, 0xe in `UpdateMFX_02MayhewA`). Underneath is data:

- `MusicEventList` (0x40 dwords at 0x29a180) is the input. `Music_Event(id, value)` (ours) is called by 23
  functions: `MusicTrigger_Update` (placement type 251 sends `Music_Event(k2, 2)` for 30 frames), triggers, drone
  alarm and sight checks, `Mission_*`, the player (death, creeping, wires) and the script player. Slots read so
  far: 0 the level hashcode and 1 "start" (both set by `SFXLoadCorrectBank`), 2 combat (2 = forced), 3 and 8 fade
  down, 6/7 jump to the table's end sections, 10 a `MusicVars` override, 11/12 a script hashcode (e.g. 0x6000071,
  0x6000087 - NIS scripts by the 0x06 prefix). `UpdateMusicalEvents` (0xca620) clears the list every frame after a
  copy to `BackupEventList`/`LastEventList`, so events are one-frame pulses unless resent.
- `MapMusicData` - a table at 0x181cd0, 35 entries of 0x4c bytes **(verified bounds)**: +0 level hashcode,
  +4 track, +8 start section, +0xc/+0x10 end-section jumps, +0x1c/+0x20 under-attack timers. Selected when slot 0
  changes; starts the track with `SFXStartMusic` (0xca5e0).
- `MusicVars` presets - 12 entries of 0x10 at 0x181c10 (`FUN_000c4d20`), defaulting to {2, 0, 8, 25.0}: ducking
  or combat-intensity parameters by the look of their use (inference).
- `UpdateMusicalEvents` dispatches to the level's `UpdateMFX_*` by `MapMusicData[+0]` with a switch over 21 single
  player levels; multiplayer and other levels have no per-level function.

### Connections

Loader (`Sound_Kill`/`Sound_Init`/`SFXLoadCorrectBank` in `ResetMap_Load`; map sounds from parsemap block 0x28);
drones (alertness, `Sound_ZeroAlertness`, `Music_Event` from alarm/sight); scripts (`Script_SoundStart` plays
2D or 3D sounds bound to a stream's object, `ScriptCam` changes 3D culling, NIS flags pause SFX and fade music);
saves (music/SFX volume and SFX mode are in the GSET block); file system (`SFXSuspendFileAccess` around loadable
loads, so streams do not compete with a blocking load); the platform layer below.

### The platform boundary (summary only)

`psi*` (0xe09c0-0xe0ed0, thin, still original) calls `dsnd*`/`xbox*` (0xe0f00-0xe1d80, ours), which is the XAudio2
seam in `src/action/sound/dsndSeam.cpp` with `xaudio2Backend.cpp`. 29 DirectSound entry points, 192 static
buffers, ADPCM decoded to PCM. Movie audio has its own XAudio2 voice in the FFmpeg player (`engine/Fmv.cpp`). Detail and measured call
volumes: docs/audio-inventory.md. Note the name clash: the platform function at 0xe19c0 is also called `SFXUpdate`
in Ghidra (it is the dsnd voice update), as is the engine one at 0xcad40.

### FMV path

`ResetMap_Load` treats hashcodes 0x071xxxxx (intros), 0x072xxxxx and 0x073xxxxx (outros) as movies: it pushes game
flow state 6 and calls `psiStartBackgroundMovie` (ours) -> `BackgroundMoviePlayFile` (0xe8a90): two 640x480
textures over 0x12c000-byte frame buffers, `maybeCreateVideoDecoder` on `<30_fps|25_fps>\%08x.xmv`, an audio track
chosen by language (0 default, 1/2/3 for languages 2, 3, 6), `maybeSFXCreateStreamForVideo` and
`BackgroundMovieSetupMix`, volume `MovieVolume`. Per frame `maybeDecodeMpgAudio` (0xe8cf0, misnamed: it decodes a
video frame, calls `DirectSoundDoWork` and draws the YUV quad). The folder test is `Graphics_IsPalI() ? "30_fps" :
"25_fps"` as decompiled, which reads backwards; either the getter is misnamed or the folders are - unchecked.

**Since replaced:** the five functions are now `engine/Fmv.cpp`, an FFmpeg player, and the XMV library (60 functions,
157 KB) is no longer reached; see `docs/fmv.md`. The FMV subtitles are script-player scripts on the game's own
clock, as before. What follows is the assessment from before the replacement. The decoder was Microsoft's XMV
library linked into the XBE. It ran natively because `d3dSeam.cpp` hooked the four D3D8 entry points it called
and `dsndStream.cpp` hooked the DirectSound stream entry points. Replacing it would take: a host XMV demuxer and decoder (FFmpeg has an XMV demuxer and WMV2-family video and ADPCM/WMA audio
decoders - external knowledge, not tried on these files), or an offline transcode of the movies to a common format
plus a small player; either way a replacement for the five `BackgroundMovie*` functions, the subtitle scripts
(below) kept in step with the new clock, and `psiMovieFinished`/skip handling. Low value while the hooked decoder
works; high value only if the XBE code must go entirely.

### Reimplemented vs live

Ours: the 14 `Sound_*`/`Music_Event` handle functions (25 in the group, 3.5 KB). Live: all of `SFX*` (38, 8.5 KB),
`ES_*` (20, 4.1 KB), `UpdateMFX_*` (21, 6.0 KB), and 30 unnamed/other functions (7.9 KB). `Sound_Play` is dead (its
callers are ours and use our copy).

### Understood vs unknown

Well understood: the handle layer, request deferral, listeners and map sounds, bank loading, the music dispatch
and table shapes, the stream/marker mechanism in outline.

Unknown or uncertain:

- **The meaning of most `MusicEventList` slots** and of the private music counters; which game event sends which
  slot. Needed before the `UpdateMFX_*` functions can be named or tested by intent rather than by output.
- **File formats**: `SBINFO.SBI`, the SFX/SHF/SBF bank trio (Ghidra has `SFXHeader`, `SFXParameters`,
  `SampleHeaderData` structs, partly typed), `STREAMS.BIN` and the music marker layout. No tool reads them yet.
- **Stream timing**: `SFXUpdateStreams` is an underrun race by construction (docs/audio-inventory.md records the
  looping-music bug it caused under a rewinding `Stop`); a reimplementation can change timing and reintroduce it.
- `FUN_000c4b70`/`c4bc0` (recently-started list with countdowns) - purpose inferred as repeat suppression.
- The custom register conventions: `Sound_ReqestPlaySfx` (EDI), and likely more among the ES helpers.
- `SFXOutputData` field names past the radii are guesses (`maybeTracked3d`, `alertnessRelated`).

### Suggested order

1. Getters/setters and leaves (`SFXGet/SetVolume`, `SFXGet/SetMode`, `GetSFXLanguage`, `FindMusicTrackNumber`,
   `FUN_000c4d20`, `IsUnderWaterSFX`, `Sound_IsFinnished`, `Sound_Stop_Ref`, `Sound_Playing_Ref`) - shadow-testable
   per call, and they unblock the rest.
2. `Sound_UpdateSounds`, `Sound_ReqestPlaySfx` (EDI shim), `Sound_UpdateListeners` - completes the handle layer,
   which is ours apart from these.
3. `UpdateMusicalEvents` and the 21 `UpdateMFX_*`: pure logic over globals, no I/O; shadow by snapshotting the
   music globals around each call during replays. Port them as they are (hand-coded); a data-driven rewrite is a
   later, optional step.
4. The SFX item lifecycle (`SFXStart3D`, `StartSample`, `maybeDXSoundPlay`, `SFXUpdate`, `KillSFX`, cue logic),
   with the bank loader and a bank-format reader in `tools/`.
5. Streams (`ES_*`, `SFXUpdateStreams`, markers) last: most timing-sensitive, and the platform below is already
   ours, so there is the least to gain.

---

## 2. Level loading

### Purpose

From "load level X" to a playable level: game-flow hand-over, memory and subsystem reset, archive loading, map
parsing, object creation and post-load fix-ups. The level **data** format is documented on the `blender-exports`
branch: `docs/level/README.md` and `docs/level/spec-world.md` (`git show blender-exports:docs/level/README.md`);
it is not repeated here.

### The sequence

All **(verified)** from decompiles; R marks our functions.

1. `ResetMap_LevelToLoad(hash, warmReset, bypassFmv)` (0xbddf0, R): only from game-flow states 2, 7, 8, 0xd, 0xe.
   Driving levels push state 9 (the other engine). Multiplayer maps set `maybeIsMultiplayerMapLoading` and
   `MP_setLoadingSkins`. Unless bypassed, each single-player level hash is swapped for its intro movie
   (0x070000nn -> 0x071000nn). Sets `GameState.NextLevelHashcode`, pushes state 3. Also reached from scripts
   (`Script_EventHandler` event 0x12, after `Player_RamSave`).
2. `ResetMap_Load` (0xbfb60, L), a state machine in `loadState`, one step per frame from `GameFlow_Main`:
   - **step 0**: reset resources, menus, sound, memory (`Mem_Init`) and the hash table; unless going to the menu,
     `ResetMap_GameInit` and a blocking load of archive **0x07000500**; `LoadWoman`. A level (no bits in
     0x00ff0000) goes to step 3. A movie looks up its subtitle script (0x0600095a-0x0600097a, by a switch) and, if
     subtitles are on and the script is in the hash table, `SP_Create`s it; then state 6 and the movie; step 1.
   - **steps 1-2**: after the movie, `maybeGetNextMissionInSequence` (0xbdcd0) maps 0x072xxxxx to the next intro
     and outros to 0x07000048 (the menu-pre level); step 3, or back to 0 for another movie.
   - **step 3**: `ResetMap_GenLoadScreen` (level image and hint text from `ResetMap_LevelCode2Img`, a 3 KB switch),
     then the reset again (`Mem_Init`, `hashtable_initialise`, `Sound_Init`, `ResetMap_GameInit` - about 40
     subsystem inits, `Mission_Init` for real levels), `SFXLoadCorrectBank`, `psiLight_Create`, then
     `LoaderLoad(1, hash)` until it returns false; then rooms and portals (`build_link_objects_to_rooms`,
     `build_LinkDoors2Portals`), the dynamic map data, `AIPath_BindNodes`, `Mem_Shrink` (dead stub),
     `ReadTuningVars` (R), `Player_Start`, `MP_Start`, `Drone_PostLoad_Init`, `MP_PostLoad_Init`,
     `Cameras_PostLoad`, `hashtable_create_sections`, and pushes state 4 (in game).
3. `LoaderLoad` (0xbe910, R; `src/action/engine/Loader.cpp`): opens `<hash>.bin` through `psiFileOpen` (R), which
   loads the whole archive from the filesys packs into memory in one blocking call; then header (0x20 bytes),
   directory entries (size, type byte, name ending in the hex hashcode), and one batch (the per-batch limit is
   0xffffffff). `LoaderProcess` (R) dispatches by type: 1 map (`parsemap_parsemap` with creation on), 0/2/0xb
   graphics packs (creation off), 3-5 animations, 6 skeletons, 7 **scripts** (`Script_Load`), 8 menus, 0xc
   "loadable" records. Parsemap can ask to be called again for the same file.
4. `parsemap_parsemap` (0xa6b20, R) walks 24-bit-size/8-bit-id blocks via `parsemap_parsenextblock` (0xa6a80) and
   `parsemap_handle_block_id` (0xa6820, L): 4/0x1f entity params, 5 AI paths, 0xe map header, 0xf-0x18/0x29
   palettes and textures, 0x19 paths, 0x1a placements (static, then dynamic objects via
   `parsemap_create_dynamic_objects`, R), 0x1c discard point, 0x1d end, 0x21 portals, 0x26 light radiators, 0x27
   LOD, 0x28 map sounds, 0x2c hash list, 0x2e/0x2f collision, 0x30 particles.

**File system underneath.** `FS_Init` (0xe2a40, R; `src/action/engine/FS.cpp`) opens `d:\eurocom\filesys.dNN` and
reads the directory (0x28-byte header, 0x26-byte entries sorted by CRC32 of the normalised path) from pack 0.
`FS_AllocateAndLoadBlocking` (0xdc990, L) gets the size, `Mem_Malloc`s, `FS_LoadFileIfReady` (0xe3280, L) and spins
`FS_StateMachineIterate` (R) with `ShowLoadProgressScreen` (the loading dots, ours since f78c47c). The state machine
reads, checks the data CRC (fatal on mismatch), and EDL-decompresses in place when flag 2 is set
(`maybeEDL_DecompressSection`, R; the destination is offset so the compressed data sits at the end of the buffer).
The original's hard-disk cache states (5-9, a `Z:` cache) are skipped in ours. `FUN_000e3100` opens one of 16
async handles into the packs for streaming reads *(invented name: FS_OpenStreamHandle)*. Inflate/EDL
(0xd3a30-0xd4c50) is ours except `Inflate_huffman` (0xd3a80, 2.2 KB).

### Reimplemented vs live

Ours: `LoaderLoad`, `LoaderProcess`, `isLoadable`, `ResetMap_LevelToLoad`, `Reset_MapLoadSettings`,
`ReadTuningVars`, `parsemap_parsemap`, the dynamic-object creator, entity params and collision blocks, `psiFileOpen`,
`psiFileLoad` (with a `patch/` override), `FS_Init`, `FS_StateMachineIterate`, `FS_GetFileSize`,
`FS_MatchFilenameToHeader`, the loading-screen and timer helpers, and the EDL/Inflate code. Live: `ResetMap_Load`,
`ResetMap_GameInit`, `ResetMap_LevelCode2Img`, `ResetMap_GenLoadScreen`, `LoadScreen_Draw`, 16 of the 20 parsemap
functions, `FS_AllocateAndLoadBlocking`, `FS_LoadFileIfReady`, `LoadWoman`/`psiDecompressWoman`, `Inflate_huffman`.

### Understood vs unknown

Well understood: the whole call order above, the archive and directory format, block ids, the FS directory and
state machine, EDL.

Unknown or uncertain:

- **What archive 0x07000500 is for.** It is loaded in step 0, right before the movie path looks up the FMV
  subtitle scripts, so it very likely holds them (inference). For a level, step 3 then calls `Mem_Init` and
  `hashtable_initialise` again before loading the level - so whatever 0x07000500 loaded is thrown away, unless
  `Mem_Init` preserves it. Not checked; matters for anyone reordering the reset.
- **Loadables.** `LoadableLoad` (0xbff90) and `LoadableReload` (0xbffd0) only delete hash type 0x04000000 and
  recreate the hash sections; they load nothing. `SP_LoadScript` calls `LoadableLoad` for scripts flagged
  loadable (type 0xc records), suspending SFX file access around it. On the PS2 this presumably streamed data in
  mid-level; on the Xbox it looks vestigial (inference).
- The archive variant (hash bits 20-23 = variant + 7): `ResetMap_Load` always passes 0; whether anything else does
  is not checked.
- `Mem_Shrink` is a 16-byte stub on the Xbox, so the discard block (0x1c) is never actually reclaimed.
- `FS_StateMachineIterate`'s reaping order was found by a hang in the clang build (see the comment in `FS.cpp`);
  it has not been compared instruction by instruction with 0xe2d90.
- Our `psiFileLoad` writes **every multi-file-mode load to `dump/`** (`dumpToFile` in `psiFile.cpp`) - a debugging
  leftover on the live path, not part of the original.

### Suggested order

1. `ResetMap_Load` and `ResetMap_GameInit`: orchestration only (every callee stays original), but they are the
   spine of loading, and owning them lets the reset order be documented in code. Test: level-load replays, compare
   the heap and hash table after load.
2. The remaining parsemap block handlers (palettes, textures, portals, LOD, hash list, static map data) - small and
   data-only; check by dumping the parsed structures per level against the original.
3. `FS_AllocateAndLoadBlocking`, `FS_LoadFileIfReady`, `Inflate_huffman`: finishes the file path (and the CRC/EDL
   path is already ours).
4. `ResetMap_LevelCode2Img`/`GenLoadScreen`/`LoadScreen_Draw` and `LoadWoman`: cosmetic, any time.

---

## 3. Save and mission state

### Purpose

The Codename (profile) save: player settings, campaign progress and scores, multiplayer settings, global options,
cheats and bonus unlocks, as a set of labelled blocks of bit-packed fields.

### How it works

All **(verified)** unless marked.

- **Block table** `SaveIFFBlocks` at 0x17c238, `NumSaveBlocks` = 6 (0x17c230). Each entry is 0x10 bytes:
  {mask bit, label, make function, load function}.

  | bit | label | make / load | contents |
  |---|---|---|---|
  | 0x01 | `PLRS` | `LS_MakePlrSettings` 0x6ed30 / `LS_LoadPlrSettings` 0x6eee0 | per player: inverted look (1 bit), control style (4), driving control style (4), auto-aim SP and MP, manual-aim toggle, auto-switch weapons, crouch toggle, vibration, crosshair off (1 each), flashing objects (2), HUD (1) |
  | 0x02 | `MSSN` | `FUN_0006f110` *(invented name: LS_MakeMission)* / `LS_LoadMission` 0x6f230 | the Nightfire progress word (32), score-table count (8), then per entry a 32-bit score and 4-bit Bond moments; the weapon-upgrade flag (1) |
  | 0x04 | `MPSG` | `LS_MakeMPSettings` 0x6f450 / `LS_LoadMPSettings` 0x6f500 | per player: one flag (1) and the health modifier (32) |
  | 0x08 | `GSET` | `LS_MakeGlobalSettings` 0x6f550 / `LS_LoadGlobalSettings` 0x6f720 | music volume (7), SFX volume (7), language (7), subtitles (1), SFX mode (32), widescreen (1), split-screen layout (32), seven reserved zero words, a 16-bit 4 (a version, by inference) |
  | 0x10 | `CHET` | `LS_MakeCheats` 0x6f000 / `LS_LoadCheats` 0x6f0c0 | immortal, all weapons, unlimited ammo (1 each) |
  | 0x20 | `BNUS` | `LS_MakeBonus` 0x6f340 / `LS_LoadBonus` 0x6f3f0 | a 64-bit value from `FUN_0007c660(0)` - the bonus/unlock bits, by inference |

- **Block format**: 4-byte label, 4-byte little-endian length in bytes (header included), then fields packed
  LSB-first by `BIN_PushBits` from bit 0x40; read back with `BIN_PullBits_U32/U8`. Each make function builds its
  block in a static 0x1000-byte buffer (0x21d798 upwards).
- **Buffers**: `DstData` (0x215790) and `InBufData` (0x219790), four 0x1000-byte slots each, indexed by a slot
  number from the menu (the controller/player, by inference). `LS_GetSaveStuff(mask, slot)` (0x6ec10) builds the
  outgoing save: blocks whose bit is in `mask` are made fresh; the others are **copied from the last loaded
  buffer** (`LS_FindBlockByLabel` 0x6eb30 walks `InBufData` by label/length). So a partial save preserves what it
  does not rewrite. The whole save must fit in 0x1000 bytes; nothing checks it.
- **Flow**: `LS_Save(codename, slot, mask)` (0x6f880) and `LS_Load(codename, slot, mask)` (0x6f960) are polled
  state machines (`Save_State` at 0x21d794, `Load_State` beside it; 0 idle, 1/2 busy, 7 and 8 the two outcomes);
  `LS_FlushStates` resets both. They call `psiSaveData`/`psiLoadData` (ours) and poll `FUN_000dfc30` /
  `psiLoadingData` for completion; on load completion `LS_GetLoadStuff` calls the load function of each block in
  `mask`. The only caller is `Menu_UpdateMessageBox` (0x757d0), which drives operations 0 load, 1 save, 2/4 enumerate
  and 3 delete from the `ls` struct (codename at 0x17d558) and hands each state to `XBox_DoSaveFlow` (0x8f3f0, a
  2 KB UI flow of message boxes, buttons and text; no file code of its own).
- **The file**: on the Xbox, `psiSaveData` -> `maybeSaveFileRelated` wrote `u:\<codename folder>\` with a
  `SaveMeta.xbx`, a 29-byte header, an XOR scramble and `XCalculateSignature`. Ours (`src/action/engine/psiSave.cpp`)
  writes the raw block buffer to `saves/<codename>.dat` and enumerates that folder; Codenames are 8 characters (the
  enumeration cache has 9-byte slots).

### Connections

Front end (Codename screens, options, cheats and bonus pages), `PlayerInputs`, `MPSettings`, `PlrStats` score
tables, `Menu_Get/SetNightfireStatus`, sound volumes and mode, `CheatInfo`, language. Separate from it:
`Player_RamSave` (mid-mission carry-over between linked levels, used by script event 0x12) - not a disc save.

### Reimplemented vs live

LS: 0 of 20. Platform save code: `psiSaveData`, `psiLoadData`, `psiStartSaveEnum`, `psiGetNextSaveName`,
`psiEndSaveEnum`, `SaveEnum_GetNextEntry` are ours; `maybeSaveFileRelated`, `WideToAsciiSaveName` and several
helpers are dead as a result; `XBox_DoSaveFlow`, `psiGetNextSave`, `MaybeFileWriteContents` and the `u:\` open/close
helpers are live.

### Understood vs unknown

Well understood: the table, block format, every field written, the buffer scheme. Unknown or uncertain:

- **The masks the menus pass** (`Menu_UpdateMessageBox`'s `param_2`): which screens save which blocks.
- The mapping from `psiInternalLoadingDataState` (our 1-5, 10, 11) to what `FUN_000dfc30`/`psiLoadingData` return
  (2 and 4 drive LS states 7 and 8). Our codes were matched to the original's writers, not to these readers; worth a
  check before LS is ported.
- `FUN_0007c660`'s 64-bit value, and the `MPSG` flag.
- The delete operation: case 3 of `Menu_UpdateMessageBox` shows only a profiling hook where `psiDeleteData` would be,
  then polls `psiDeletingData`. Whether Codename deletion works at all on the Xbox build, and in ours, is unchecked.
- Our save files are not compatible with real Xbox saves (no header, scramble or signature) - by design.

### Suggested order

The LS module is the easiest self-contained target in this document: 20 small, pure functions over a fixed table,
no custom conventions found, and an exact oracle. Port all of it at once, generate the block table from the XBE
bytes, and shadow-test by building every block from the same globals with both versions and comparing bytes (and
round-tripping load -> make). Then `XBox_DoSaveFlow` with the menu work (docs/ui).

---

## 4. Scripting

### Purpose

Keyframed scripts ("NIS" cutscenes and level animations): a script is a set of up to 62 parallel **streams**, each
driving an entity, an animation, a sprite, a camera, a sound, a light or a nested script along keyframes, with
timed commands and events. There is no general-purpose scripting language (as for the drones, docs/drone/README.md).

### How it works

- **Loading** (two phases in one function) **(verified)**. `Script_Load` (0xc1550) only accepts hashcodes
  0x06xxxxxx. Called by `LoaderProcess` for archive type 7 with a file buffer, it parses the asset into a 0x214-byte
  header *(invented name: script header)* and stores it in the hash table: a 16-bit tag that must be 0x1c (else the
  hash maps to 0xffffffff, "bad"), four 16-bit fields (camera-NIS flag, stream count <= 0x3e, two counts of which
  the last is the frame count), then per stream {16-bit, 16-bit, length-prefixed data}, then a dword and an end
  offset. Called later without a buffer (from `SP_LoadScript`), it allocates a 0xab0-byte `SCRIPTINFO`
  (`src/action/engine/Script.h`) and initialises one `SSTREAM` (0x28 bytes) per stream from the header: the two
  16-bit fields go to +0x1c/+0x1e (keyframe parameters), the data pointer to `streamBuffer`/`streamBufferStart`,
  parent index +0x25 = -1. Ghidra splits this function: `Script_Load` is 25 bytes and `Script_Load2` (0xc1569) is
  the shared body with a garbled signature (inference from the identical code).
- **Playing**. A `ScriptPlayer` object (`SP_*`, `src/action/game/obj/ScriptPlayer.cpp`, ours) owns up to four
  scripts and switches between them by trigger (switch channel, bullet hit, touch, timer). `SP_LoadScript`
  (0xc3e20, L) instantiates or rewinds, then `Script_Play` and `Script_Update` according to the play mode (1, 8 =
  reverse, others = show first frame and stop). `Script_Update` (R) advances time by `FRAME_RATE_MUL` and calls
  `Script_Run` per stream.
- **`Script_Run`** (0xc3330, L) **(verified)**: interpolates the stream if its time is within range
  (`Script_Interp`), then executes byte commands while the stream's time has passed its wait point. Commands
  (`ScriptCmd` enum in Ghidra): EndScript, wait (`SetSomeFloat`), a frame mark that sets the at-end flag,
  Entity/Anim/Camera/Sprite/Sound/Light/SubScript Start and End, EventHandler, FadeStart (`Camera_SetFade`),
  TextStart (`Text_AddMsg` with a text id). End commands call `Script_KillStream` (R); end of script calls
  `Script_StreamEnd` and loops when playing in reverse.
- **`Script_Interp`** (0xc3950, L) per stream type: entity - `Script_GetInterp` (0xc21b0) into a matrix, scaled,
  composed with parent streams (`FUN_000c2eb0`); anim - drive the object's animation frame from stream time, with
  footstep effects by ray cast and a special case creating ninja eyes for hashcode 0x0500001e; camera - interpolate
  and `Script_UpdateCamera`; sound - kill when finished, fade with the script fade, follow the bound object;
  light - position and colour; subscript - transform and recurse into `Script_Update`.
- **Keyframes**: 0x30-byte records with the time at +0x2c (binary search in `FUN_000c11b0`), position, a
  quaternion slerped by `Quat_Slerp_Acc`, and two scalar channels at +0x24/+0x28. Linear (`FUN_000c1210`) and
  Catmull-Rom (`FUN_000c12b0` computes the four Catmull-Rom weights; `FUN_000c1480` evaluates with `Spline_Eval3D`)
  modes. `SS_Update` (the simple-script object) uses the separate `KeyFrame_Interp`/`Spline_Interp`.
- **Events** (`Script_EventHandler` 0xc2990, L; script in ESI) **(verified)**: an id and up to 8 dword arguments.
  3 jump to frame, 4 disable the player and pause the mission timer, 5 enable drones, 6 save the player's (or
  bound object's) matrix to restore control at the end, 7 re-enable drones on free, 8 `Drone_CoderCreate` at the
  object, 9 `Player_Cam2Mode`, 0xa break effect, 0xb set held weapon, 0xc restore-on-kill flag, 0xd/0x11 write
  switch channels (0x11 conditionally), 0xe looping-sound flag, 0xf call the script player's callback, 0x10 FFwd
  kills camera, 0x12 `Player_RamSave` and `ResetMap_LevelToLoad(arg, warm)` - the level-to-level hand-over - and 0x13
  claim `ScriptCam`.
- **How scripts drive the rest**: cameras through the camera stream and `ScriptCam` (0x1f6678, also read by sound
  culling); entities by writing object matrices and render flags; music through `Music_Event` from `Script_Play`,
  `Script_CameraStart`, `Script_KillStream`, `Script_FFwd` and `Script_SubScriptStart` (slots 11/12 carry the
  script's hashcode, by inference from `UpdateMFX_01CastleB` testing 0x6000071); sounds through `Script_SoundStart`
  (0xc27b0: 2D, or 3D at the stream's object). Script creators outside the script player: `ResetMap_Load` (FMV
  subtitles), `Switch_Create`, `FuseBox_Create`, `Copter_Create`, `MP_CreateObject`; `Script_Update` is also called
  by gun turrets, explosions, the space missiles and lasers, and the front end's `P_NIS_Handler`.

The **UI Script module** at 0x96960-0x97370 (`Script_PrepareScripting`, `Script_AddKeyFrame`, `Script_RunFrame`,
`Script_InterpolateSpline/Line` and others; 15 functions, all live, filed under ui.frontend) is a separate,
smaller keyframe animator for menu items. It shares the `Script_` prefix - `Script_Stop` exists at both 0xc26f0 and
0x96d70 - which any name-based tooling has to allow for.

### Reimplemented vs live

Ours: the script player (all `SP_*` except `SP_LoadScript`, which is a naked shim to the original, and the dead
`SP_Hit`), and `Script_Init`, `Play`, `IsPlaying`, `Update`, `Free`, `FFwd`, `KillStream`, `CameraStart`,
`GetObj`, `HideObj`, `RemoveObj`, `SetColour`, `SetPosRot`, `IsDeathNIS`. Live: the loader (`Script_Load`), the
interpreter (`Script_Run`, `Script_Interp`, `Script_GetInterp`), the other `*Start` functions, `Script_EventHandler`,
`Script_StreamEnd`, `Script_CreateEntity` (1.2 KB), `Script_FreeFromHashTable`, `Script_Set2Start`, `Script_Stop`,
and the six keyframe helpers.

### Understood vs unknown

Well understood: the asset container, the command set, stream types, the event table, keyframe interpolation,
who plays scripts.

Unknown or uncertain:

- **The per-stream data layout inside each command** (what `Script_EntityStart`, `Script_CreateEntity` and
  `Script_AnimStart` read), and the two 16-bit keyframe parameters at `SSTREAM+0x1c/+0x1e` and the remaining
  `SSTREAM` fields (`field0`, `field1`, `field3`; Script.h documents what is known).
- The header fields at +0x20a/+0x20c, the trailing dword, and `SCRIPTINFO+0x84`.
- `ScriptCmd_DoSomeThingFromAWord`'s word argument, and the precise meaning of the at-end flag (0x2).
- No tool decodes script assets yet; there is no list of which levels use which events. A `tools/script/` dumper
  is the prerequisite for testing the interpreter by data rather than by replay.
- Custom conventions: `Script_EventHandler` (ESI), `Script_CameraStart` (EAX/ESI, already shimmed), the keyframe
  helpers (EBX, EAX, ESI, EDI) and `FUN_000c2eb0` (EAX/EDI). Each needs its callers covered or a naked shim.

### Suggested order

1. A script asset dumper (offline, from the level archives' type-7 entries), listing streams, commands and events.
2. The keyframe helpers and `Script_GetInterp`: pure maths, shadow-testable per call (compare output matrices).
3. `Script_Load` (both phases; compare the 0x214-byte header and the fresh `SCRIPTINFO` byte for byte), `Set2Start`,
   `Stop`, `StreamEnd`, `FreeFromHashTable`.
4. `Script_Run`, `Script_Interp` and the `*Start` functions together with `Script_EventHandler`, tested on NIS
   replays (Castle and Mayhew intros) by dumping each stream's object matrix per frame.
5. `Script_CreateEntity` last: it creates objects and so touches the object and animation systems.

---

## Cross-cutting risks

- **Hand-written per-level code in three places** (`UpdateMFX_*`, `SFXLoadCorrectBank`, `ResetMap_LevelCode2Img`,
  plus the FMV and subtitle switches in `ResetMap_LevelToLoad`/`ResetMap_Load`/`maybeGetNextMissionInSequence`): all
  keyed by level hashcode. Generating these switches from the XBE into one level table would remove a class of
  transcription errors.
- **One-frame event pulses**: `MusicEventList` is cleared every frame and sound requests are deferred a frame;
  reordering calls inside `Game_Run` changes behaviour silently.
- **Unbounded fixed buffers**: the 0x1000-byte save, the `sfxItems` copy at the end of `SFXUpdate` (the decompile's
  own comment), map sounds (now guarded in ours), the 9-byte Codename cache slots.
- **Shared names** (`SFXUpdate` x2, `Script_Stop` x2) in tooling that resolves functions by name.
