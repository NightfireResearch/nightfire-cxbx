# FMV playback in the driving engine

A scoping study, written October 2026, for moving the driving engine's movies onto FFmpeg the way the action
engine's were moved (`docs/fmv.md`, `src/action/engine/Fmv.cpp`). It has since been implemented, in
`src/driving/engine/PlayMPC.cpp`; [As built](#as-built) at the end says where that differs from the plan below. Addresses
are `Driving.xbe`'s. Each claim says what it rests on: **disassembly** (read with capstone from
`disc/Driving.xbe`), **decompile** (Ghidra), **file** (the data on the disc), **test** (run here), or
**inference**. Names marked "(invented name)" are not in Ghidra or the PS2 symbols.

The short version: the movies already play today, through EA's own player code, and nothing about them is
broken. Replacing them is a choice, not a fix. If it is made, the cut point is EA's `PlayMPC` class: four
`__thiscall` methods with two callers, which own the whole movie from file name to last frame. FFmpeg decodes
the video as it stands and the audio once our code, not FFmpeg's demuxer, splits the file into packets.

## The movies

12 movies in four languages, 48 files, 1.1 GB, all inside `driving/misc.viv` (an EA BIG archive, "BIGF",
126 entries) under `pal\eng\`, `pal\fre\`, `pal\ger\` and `pal\spa\` (**file**). Nothing is loose on the disc.
Next to them are ten subtitle files, `data\loading\*.sub` (**file**).

Every movie is the same format (**file**, **test**):

- An EA chunked stream: 8-byte chunks, a four-character tag then a little-endian size including the header.
- Video: EA Madcow (MAD), 640x480, 25 fps (40 ms per frame in each frame header). One chunk per frame:
  `MADk` (intra), `MADm` (predicted), `MADe` (predicted, never used as a reference). The first chunk is a
  `MADk`.
- Audio: one `SCHl` header chunk, *after* the first video chunk, then one `SCDl` chunk per frame (1,932
  samples, which is 40.25 ms), a `SCCl` count and a `SCEl` end. The header says 6 channels, 48 kHz,
  "revision2" 10, which is EA-XA R1 ADPCM: 5.1 audio. The video and audio durations agree to within 0.2 s in
  every file.

| movie | frames | length | eng size | played by |
|---|---|---|---|---|
| `paris_intro.mad` | 1304 | 52.2 s | 22.9 MB | `RunTheGame`, before `paris_mis01` |
| `paris2.mad` | 1577 | 63.1 s | 33.5 MB | not named in the XBE; presumably an `ESetVideo` event (inference) |
| `paris_outro.mad` | 719 | 28.8 s | 15.3 MB | `RunTheGame`, after `paris_mis01` |
| `island_intro2.mad` | 1103 | 44.1 s | 20.7 MB | before `uw_mis11` |
| `island_outro.mad` | 523 | 20.9 s | 9.8 MB | after `uw_mis11` |
| `snow_intro.mad` | 663 | 26.5 s | 10.2 MB | before `snow1a_mis3` |
| `snow_outro.mad` | 806 | 32.2 s | 14.8 MB | after `snow1a_mis3` |
| `alps_intro.mad` | 768 | 30.7 s | 13.1 MB | before `snow2a_mis4` |
| `ja_intro.mad` | 225 | 9.0 s | 3.9 MB | before `junglea_mis13a` (no subtitles) |
| `ja_outro.mad` | 561 | 22.4 s | 9.0 MB | after `junglea_mis13a` |
| `jc_intro.mad` | 197 | 7.9 s | 3.4 MB | before `junglec_mis13c` |
| `jc_outro.mad` | 76 | 3.0 s | 1.7 MB | after `junglec_mis13c` |

The other languages have the same frame counts and audio format; only the audio differs (**file**).
`jungleb_mis13b` and `race` have no movies (**decompile**). `paris2.mad` is the only movie the XBE does not
name, and `paris2.sub` exists, so it is the likely `ESetVideo` movie; the mission data that would say so was not
found in the `.viv` files in plain text (it is probably compressed).

Subtitles are keyed by **frame number**: `FRAME_360=8127` shows locale string 8127 from frame 360, `=0` clears
it (**file**). Some of `paris2.sub`'s cues lie past the movie's last frame.

The `.asf` streams (`"%s.asf"`, `AStream`) and the `.mus` files are the music system and have nothing to do with
the movies, except that both use EA's `SNDSTRM` streaming layer underneath. The string
`"Unable to re-open movie cache file"` is not ours either: it is in the XDK's `D3D` section (0x00171d40, next to
`xbmovie.dat`), the devkit's screen-capture feature.

### What FFmpeg makes of them

Tested with FFmpeg 8.0 on the extracted files (**test**):

- **Video decodes.** `ffprobe` reports `ea` / `mad (eamad)`, 640x480, `yuv420p`, 25 fps, and a full
  `ffmpeg -f null` decode of `jc_outro` and `paris2` runs clean (76 and 1577 frames).
- **Audio is dropped by the demuxer**, not the decoder. FFmpeg's `ea` demuxer parses the header correctly and
  then refuses it: "Unsupported number of channels: 6" (`libavformat/electronicarts.c:546`, a hard limit of 2).
  The `adpcm_ea_r1` decoder itself accepts up to 6 channels (`libavcodec/adpcm.c:268-273`). Rewriting a copy
  with two of the six channels (header patched to 2, the per-channel offset table rebuilt) decodes all three
  pairs to smooth, plausible audio of the right length. Channel 2 is loud and the other pairs carry less,
  channel 3 has almost no high-frequency content, so the order is very likely the usual L, R, C, LFE, Ls, Rs
  (inference).
- The demuxer packetises exactly as a hand-written walker would: a video packet is the whole chunk *including*
  its 8-byte header, an audio packet is the `SCDl` body after the header, whose first 4 bytes are the sample
  count (FFmpeg source).

So the replacement should walk the chunks itself and use only libavcodec: `eamad` and `adpcm_ea_r1`, with the
audio codec context set up from the `SCHl` values (6 channels, 48 kHz). That needs no FFmpeg patch, and the
format is a dozen lines. Our FFmpeg build (`tools/fmv/build_ffmpeg.sh`) enables only the XMV demuxer and the
WMV2/Xbox ADPCM decoders, so it would gain `--enable-decoder=eamad,adpcm_ea_r1` (`eamad` pulls in
`aandcttables blockdsp bswapdsp`, all LGPL).

## The game-side interface

There is no per-frame "advance the movie" call from the game: **a movie is one blocking call** that runs its own
render loop until the movie ends or is skipped (**decompile**, **disassembly**).

```
RunTheGame 0x0005aa80 (intro before the main loop, outro after)     ESetVideo::~ESetVideo 0x00044970 (mission event)
        \                                                                   /
         PlayLevelMovie (invented name) 0x0005a6b0                        /   (same body, inlined)
              PlayMPC::PlayMPC 0x00130780 / Init 0x001307d0 / Play 0x001308a0 / ~PlayMPC 0x00130b50
                   RCMP::AV_PLAYER   0x0014c430-0x0014cd00   stream, clock, codec owner
                   frame renderer    0x0014cff0-0x0014df0f   two textures, YUV->RGB565 (MMX), the quad
                   RCMP::DECODER     0x0014df10-0x0014e0cf   codec dispatch
                   MAD codec         0x0014e0d0-0x001502ff   the video decoder
                   EA STREAM (0x0014b090-) + FILESYS (0x0010cdc0-)   file reads, from misc.viv
                   EA SNDSTRM (0x0013b990-) + SND::CEAXABLKDecf (0x00149e70-)   the audio
```

### The callers

**`RunTheGame` (0x0005aa80)** compares `MissionName` against each level name and builds two paths:
`sprintf("%s\\paris_intro.mad", GLocale::GetSubDirectory())` (the subdirectory is `pal\eng` and so on, per
the `.viv` names and the "could not open `D:\pal\eng\island_intro2.mad`" lookup in `driving-engine-plan.md`) and
`"data\\loading\\paris_intro.sub"`. The intro plays after `GLoadingScreen::Shutdown` and before
`"Entering main loop"`; the outro after the loop exits and `RRenderer::Flush`. Gates (**disassembly**, names are
inference):

| address | what it gates |
|---|---|
| byte 0x001b7057 | movies enabled at all; both callers test it |
| byte 0x001e476d | subtitles enabled; if clear the subtitle path is dropped |
| 0x00244508 / 0x00244168 | outro only: played if the first is 0 or the second non-zero (mission outcome, inference) |

**`PlayLevelMovie` (invented name; Ghidra `FUN_0005a6b0`, PS2 0x175af8, an unlisted static)** takes the movie
path on the stack (`__cdecl`, plain `ret`) and **the subtitle path in `EBX`** (0 for none), a register argument
left by whole-program optimisation (**disassembly**: `test ebx, ebx` before any write to it). It:

1. constructs a `PlayMPC` on its stack with `RRenderer::fgRenderer`'s device and render context, the pad port
   from 0x00244520, and 0;
2. `Init`s it with the path and a volume of `[0x002444f4] * 127 / 100` (a 0-100 option, as EA's 0-127);
3. makes a viewport with an orthographic projection, prints `"Play Movie"` to the loading-screen log;
4. disables `IOModule` updating, pauses `IFeedback` and `ASoundManager`, flushes every action queue, flushes the
   renderer;
5. if there is a subtitle path, `GSubtitles::LoadSubtitles` it and passes the subtitle callback (0x000e3ab0,
   not a function in Ghidra) to `Play`; then `ClearSubtitles`;
6. calls `PlayMPC::Play(viewport, fgRenderer->unk4c, callback)` - `unk4c` picks the widescreen layout below;
7. undoes all of it: deletes the viewport, destroys the player, sets `IOModule::resyncDevices`, re-enables
   updating, flushes the queues again, `ASoundManager::Resume`, `emms`.

**`ESetVideo::~ESetVideo` (0x00044970)** is the same sequence inlined, for a movie named by a mission event:
the event's +4 is the movie file name (`"%s\\%s"` with the locale subdirectory), +8 the subtitle file name
(under `data\loading\`). Driving events do their work in their destructors (see the Ghidra note on it), so this
is where a scripted movie plays. It passes the subtitle path on the stack, not in a register.

### `PlayMPC`

A 0x20-byte object on the caller's stack. All four methods are clean `__thiscall` (**disassembly**:
`ret 0x10`, `ret 0xc`, `ret 0xc`, `ret`).

| method | address | does |
|---|---|---|
| `PlayMPC(device, renderContext, padPort, flags)` | 0x00130780 | stores them, `RCMP_SYSTEM::SetREALDefaults(0x001dcd10)` (allocator 0x001dcd14 / free 0x001dcd18 / flags 0x001dcd1c) |
| `Init(path, volume, bool)` | 0x001307d0 | allocates and constructs an `RCMP::AV_PLAYER` (0x68 bytes) on the file; stores it at +0x14 and the volume at +0x1c |
| `Play(viewport, widescreen, callback)` | 0x001308a0 | the whole movie (below); destroys the player at the end |
| `~PlayMPC()` | 0x00130b50 | clears the current-player global, destroys the player if `Play` did not |

| offset | field |
|---|---|
| +0x00 | EAGL device |
| +0x04 | EAGL render context |
| +0x08 | short, cleared |
| +0x0c | frame number shown, passed to the callback |
| +0x10 | byte: skip requested |
| +0x14 | `RCMP::AV_PLAYER *` |
| +0x18 | pad port for the skip test |
| +0x1c | volume, 0-127 |

The global 0x00244798 points at the playing `PlayMPC` (set in `Play`, cleared in the destructor).

`Play` (**disassembly**):

1. `AV_PLAYER::GetFirstFrame(1, 33)` decodes frame 0 and starts the audio (33 ms is a lead added to the clock),
   `SetVol`, then creates the frame renderer (0x0014d510) for the frame and an `EAGL::DrawTextured`.
2. Places the picture: centred, scaled by screen size / 640x480. With `widescreen` set it moves it up by a
   sixth of the screen height and stretches it vertically by 4/3 - the anamorphic 16:9 layout.
3. Loop: `RenderContext::BeginFrame`, `ViewPort::BeginView`, clear, **draw the frame** (renderer slot 3), the
   subtitle callback with the frame number, `EndView`, `EndFrame` (present). Then wait, calling
   `SYNCTASK_run(0)` and `THREAD_yield(0)`, until `AV_PLAYER::IsTimeForDecode`. Then `PAD_update`, and the skip
   test: **A or B newly pressed (`analogPressed[0]`/`[1]`) or Start (`buttonsPressed & 0x10`)** on the pad at
   +0x18. Then `AV_PLAYER::GetFrame(time)` decodes the frame due; it returns 0 at the end of the file.
4. Ends when there is no frame or skip is set; destroys the renderer, the `DrawTextured` and the player.

`FRONTENDACTION_SKIPMOVIE` is in the XBE and in `FrontEnd.def` (A, B), but the player does not use the action
system: it reads the pad itself. Start also skips.

### State and timing

`RCMP::AV_PLAYER` (0x68 bytes): +0x00 audio player, +0x14 "has separate audio file" (always 0 here), +0x30 the
video stream, +0x34 its audio tap, +0x40 clock lead (33), +0x44 current frame, +0x48 time in frames, +0x4c..+0x58
clock correction, +0x5c the millisecond timer, +0x64 the `DECODER` (**decompile**).

The clock is **not frame counting**. Ghidra's `RCMP::AUDIO_PLAYER::IsAudioFinished` (0x0014c9f0, misnamed; it
takes the `AV_PLAYER`) returns elapsed milliseconds from `TIMER_gettick`, slewed towards the audio stream's
played position from `SNDSTRM_status`, with a hard correction past 264 ms of drift. `IsTimeForDecode`
(0x0014cd00) is true once `(ms + 33) * fps / 1000 >= frame + 1`. When decoding falls behind, the MAD codec's
`GetFrame` (0x0014e730) drops `MADe` frames if it is up to two frames late, and skips to the next `MADk` if
later (**decompile**).

## The decoder boundary

EA's "RCMP" movie library (`RCMP::` names from the PS2 match) sits at 0x0014c3e0-0x001502ff, about 16 KB of
`.text`, roughly 70 functions (`driving-engine-plan.md` counts 73 `RCMP` functions matched on the PS2). It is
self-contained, with a clean C++ API that only `PlayMPC` calls.

| range | part | size | notes |
|---|---|---|---|
| 0x0014c3e0-0x0014cf0f | `AV_PLAYER`, `AUDIO_PLAYER`, `AV_MS_TIMER` | 2.8 KB | streams, clock, owns the codec |
| 0x0014cff0-0x0014df0f | frame renderer and colour conversion | 4 KB | about 130 MMX instructions; `EAGL::DrawTextured::Begin` (0x0014cf10) also sits here |
| 0x0014df10-0x0014e0cf | `RCMP::DECODER`, `CHUNK`, `CODEC_IDATA` | 0.4 KB | |
| 0x0014e0d0-0x001502ff | MAD codec (`MAD_CODEC_INTERNAL`, vtable 0x001a7828) | 8.7 KB | plain C, no MMX; calls nothing but `MEM_*`, `MUTEX_*`, `sprintf`, `REAL_abortmessage`, `DECODER::GetChunk`/`ReleaseChunk` |

Below it (**decompile**, **disassembly**):

- **File reads**: `AV_PLAYER::Init` (0x0014c430) creates an EA `STREAM` (0x0014b0c0) with two filtered taps -
  chunks whose tag starts with `M` to the video tap, `SC` chunks to the audio tap - and `STREAM_queuefile`s the
  path. The stream reads through EA's asynchronous `FILESYS_open`/`FILESYS_read` (0x0010cdc0, 0x0010ceb0),
  which tries the loose file and then the open archives, so the movie is read from `misc.viv` at its offset
  (`driving-engine-plan.md`, "The movies were never broken"). Not the BIG reader in `UFileLoader`, and no
  movie cache.
- **Memory**: everything through the `RCMP_SYSTEM` allocator pointers (0x001dcd14 / 0x001dcd18, set at run
  time to 0x00114340 / 0x00113f20) and `MEM_free`.
- **Audio**: `AUDIO_PLAYER` (0x0014cd70) makes a `SNDSTRM` tap on the stream's audio tap (`SNDSTRM_createtap`,
  0x00150380). EA's sound library parses the `SCHl` (`SNDSTRMI_parseheader`, 0x0013bc20), decodes EA-XA in
  software (`SND::CEAXABLKDecf`, 0x00149e70-0x0014a1df) and mixes it, on its 100 Hz driver thread, into the six
  5.1 speaker rings that `src/driving/sound` plays. The movie has no DirectSound buffer of its own.
- **Video out**: EAGL. The renderer (vtable 0x001a7714, made by 0x0014d510 and 0x0014d340) holds two frame
  objects (0x0014cff0), each an EAGL `SHAPE` of the movie's size (`SHAPE_create(640, 480, format 16)`), plus an
  `EAGL::TAR`. Its slot 3 (0x0014d420; inside what Ghidra thinks is `FUN_0014d340`) converts the decoded
  frame into the next shape, executes the `WBINVD` at 0x0014d452, releases the decoder's frame, swaps the
  shape into the `TAR` and draws a quad with `DrawTextured::Begin` and `D3DDevice_SetVertexData*`/`End`.

## Frames and audio

**The colour conversion is on the CPU.** The MAD codec outputs planar 4:2:0 (Y plane, then U and V at half
size). The dispatcher at 0x0014dac0 is asked for mode 3, 2 bytes per pixel, and runs 0x0014d6e0 per row: MMX
lookup tables (0x001db508, 0x001dbd08, 0x001dc508) for YUV to RGB, then packing to **RGB565** with a 2x2
ordered dither whose masks (0x00248638..0x0024864c) alternate per row; 0x0014da30 sets the 565 masks for odd
modes and 555 for even ones (**disassembly**). The texture is therefore a 640x480 16-bit RGB shape, uploaded by
EAGL and drawn through our D3D seam. The GPU does no colour work.

**The audio is EA's mixer.** Six 48 kHz channels through `SNDSTRM` into the same mixer as every other sound,
started by `SNDSTRM_modifyhold(…, 0)` in `StartSound`, volume through `SNDSTRM_vol`. Note that the callers
pause `ASoundManager` first; the movie's tap still plays because it is a stream of the low-level `SND` layer,
not an `ASoundManager` sound (inference from the order of calls).

**Synchronisation** is the audio-slewed millisecond clock above. Video and audio share one read ring, so audio
that is not consumed stops video from being read - the reason the movie ran at a tenth of its speed until
`KeTickCount` was fixed (`driving-engine-plan.md`).

## What is ours already

None of the movie functions. `src/driving/autogenerated_injections.inc` has no `WriteJmpTo` in
0x0005a6b0, 0x00044970, 0x00130780-0x00130b80, the RCMP range, `STREAM_*` or `SNDSTRM_*`. What the movies
already depend on that is ours:

| ours | where | used by the movie for |
|---|---|---|
| `PAD_update`, `PAD_getdataptr` | `src/driving/platform/Pad.cpp` | the skip test |
| the `WBINVD` at 0x0014d452, patched to nops | `src/driving/platform/XboxStartup.cpp` | the frame upload, which would fault otherwise |
| `KeTickCount`, the tick source | the loader, `src/driving/platform/XboxTimer.cpp` | `TIMER_gettick`, the sound thread's pacing |
| the D3D8 seam | `src/driving/gfx/d3dSeam.cpp` | the textured quad, `Begin`/`SetVertexData*`/`End`, present |
| the DirectSound seam and XAudio2 backend | `src/driving/sound/` | the mixer rings that carry the movie's audio |

## Replacing it with FFmpeg

### Where to cut

| cut | functions | for | against |
|---|---|---|---|
| the callers | 0x0005a6b0, 0x00044970 | the real entry points | a register argument (`EBX`) on one; the pause/resume and viewport code is duplicated in both and is game logic, not movie code |
| **`PlayMPC`** | **0x00130780, 0x001307d0, 0x001308a0, 0x00130b50** | **clean `__thiscall`, two callers, owns file to last frame; nothing below it is reached by anything else** | we draw the frame ourselves |
| `RCMP::AV_PLAYER` | about 14 | keeps `PlayMPC`'s loop | its frames are MAD `FRAME` structs that the renderer consumes; tangled with `STREAM` taps and `SNDSTRM` |
| the MAD codec | its vtable (0x001a7828) | smallest change; EA audio and drawing kept | must produce EA's internal frame layout; keeps the EA stream, the MMX conversion and the audio path |

**Recommended: replace `PlayMPC`.** It is the action engine's shape again - the game-side interface, not the
codec - and the cleanest boundary in the binary: four member functions, a 0x20-byte layout we know, standard
calling conventions, and both callers (which stay original, along with their pausing, viewport and subtitle
loading) go through it. After it, nothing reaches the RCMP library, the MAD codec, the MMX converter, the
`WBINVD`, or the EA `STREAM`/`SNDSTRM` path for movies (they remain for music).

### How

A `src/driving/engine/Fmv.cpp` (or similar, in its own compilation unit), sharing what it can with the action
engine's (the DLL loader, the clock, the audio queue - worth moving to `src/common`):

- **`PlayMPC::PlayMPC` / `Init`**: keep the layout (+0x00 device, +0x04 context, +0x18 pad port, +0x1c volume).
  `Init` opens the movie: look the path up loose first, as `FILESYS` does, then in `misc.viv` (the BIG
  directory is a 16-byte big-endian header and `{offset, size, name}` entries), and give FFmpeg nothing but
  packets read through our own chunk walker at that offset. No `AVFormatContext` is needed.
- **Decoders**: `eamad` for `MADk`/`MADm`/`MADe` (whole chunk as the packet), `adpcm_ea_r1` for `SCDl` (body
  as the packet), the latter's context from the `SCHl` header (6 channels, 48 kHz).
- **Picture**: convert FFmpeg's `yuv420p` into a texture the game can draw. Simplest is to keep EAGL: a
  640x480 `SHAPE` of the same format as the original (RGB565), written by our conversion and drawn with the
  same `DrawTextured` quad, placement and widescreen stretch as `Play`. Calling those EAGL functions from C++
  needs `// AUTOGEN` declarations for `SHAPE_create`, `EAGL::TAR::SwapShape`, `DrawTextured`, and the
  `RenderContext`/`ViewPort` frame calls `Play` makes. Converting to 32-bit instead would look better than the
  dithered 565, if the shape format allows it (not checked).
- **Audio**: a 6-channel XAudio2 source voice on the driving engine's device (`xaudio2Driving.cpp` needs a small
  entry point for it; its voices are capped at 2 channels today), with an output matrix that downmixes 5.1 to
  the stereo master the same way the mixer rings are, and the volume at +0x1c.
- **Loop**: the original's - begin frame, draw, the subtitle callback with **the frame number shown**, end
  frame - paced like the action engine's: show a frame once the audio played reaches it, drop frames that
  fall behind, wall clock when there is no audio. The skip test unchanged (A, B or Start on the pad at +0x18,
  through our `PAD_*`). Keep calling `SYNCTASK_run(0)` while waiting, as the original does.
- **FFmpeg build**: add `eamad` and `adpcm_ea_r1` to `tools/fmv/build_ffmpeg.sh`. The driving executable has to
  load the DLLs at startup as the action engine's `Fmv_Init` does.

Verification: frame-compare our output against the original's (the `drive_game` harness can dump frames) on an
intro, an outro and a subtitled movie; check the length, that skipping returns to the game in the same state,
and the subtitles' timing.

## Risks and unknowns

- **FFmpeg's `eamad` against EA's decoder.** It is a reverse-engineered decoder; it decoded every frame here
  without error, but it has not been compared with the original's pictures. The comparison will also include
  EA's dithered 565, so "matches" needs a tolerance.
- **Channel order and downmix.** L, R, C, LFE, Ls, Rs is inferred from levels, not documented. The original
  mixes the six channels into six rings that our backend folds to stereo, so matching its output means using
  the same fold.
- **The archive.** Movies have to be read from inside `misc.viv`, with the engine's loose-file-first rule if
  overrides are to keep working. Reading the BIG directory ourselves is simple; going through the game's
  `FILESYS` instead would keep one source of truth but means an asynchronous API.
- **Calling EAGL from C++.** The draw path needs half a dozen original EAGL functions declared with correct
  conventions and layouts (`driving-injection-framework.md`). `DrawTextured` is 0xa0 bytes on the stack in
  `Play`.
- **Ghidra's view of the area is unreliable.** The renderer's methods (0x0014d420-0x0014d4ff) are inside a
  mis-sized function, the `WBINVD` was found by its fault, `IsAudioFinished` is a clock, and `PlayMPC::Play`'s
  decompile loses its stack arguments. Read the disassembly.
- **Sound state.** The callers pause `ASoundManager` but the original's audio is a low-level stream that plays
  regardless; ours will bypass EA's mixer entirely, which is simpler, but anything that expected the mixer to
  be busy during a movie (none known) would notice.
- **Threads.** The original depends on EA's file thread and the 100 Hz sound driver thread. Ours depends on
  neither, which removes a class of problems (the `KeTickCount` one) rather than adding one.
- **`paris2.mad`**: which event plays it, and from which mission script, is not confirmed.
- **Nothing is broken now.** The case for doing this is consistency with the action engine, dropping the last
  MMX-dependent and EA-thread-dependent code from the movie path, and a picture without 565 dithering - not a
  bug.

## Effort

About two to three days: half a day for the BIG lookup, chunk walker and decoders (largely reused from
`src/action/engine/Fmv.cpp`), a day for the drawing through EAGL and the `PlayMPC` layout checks, half a day for
the 6-channel voice and its downmix, and the rest for frame comparison, skip and subtitle checks against the
original. The FFmpeg build change is small. The biggest uncertainty is the EAGL drawing, which has no
precedent in `src/driving` yet.

## As built

`PlayMPC::Init` and `PlayMPC::Play` are ours (`src/driving/engine/PlayMPC.cpp`, `AUTOINJECT`, ABI-checked: `this` in
`ECX`, 12 bytes popped each). The constructor and destructor stay original: our `Init` leaves the player pointer at
+0x14 null, so the destructor has nothing to free. Nothing reaches the RCMP library, the MAD codec, the MMX converter
or EA's `STREAM`/`SNDSTRM` path for movies any more.

Where it differs from the plan above:

- **The picture goes to the backend, not EAGL.** `D3D9_DrawMovieFrame` (`src/common/gfx/d3d9Backend.cpp`) uploads the
  frame to a texture of its own and draws one quad, saving and restoring the device state around it. The frame around
  the picture is still the game's own, through `AUTOGEN` calls (`src/driving/render/RenderState.hpp`):
  `RenderContext::BeginFrame`/`EndFrame`/`GetSize`, `ViewPort::BeginView`/`ClearViewPort(7)`/`EndView`, and
  `0x000e4340`, which turned out to set the viewport's rectangle and depth range (`SetRect(0, 0, width, height, 0.01,
  1.0)`). The subtitle callback draws between them as before. The colour is 32-bit, converted from FFmpeg's 4:2:0
  (BT.601), with no 565 dithering.
- **The audio header comes after the first video chunk**, so `Play` reads ahead until it has seen the `SCHl` (or 16
  video packets) before starting the clock. A late header would have created a voice after the clock started. The
  header begins `PT`, the platform (7) and a zero, not `PT\0\0`. FFmpeg's demuxer checks only the `P`.
- **The shared parts are in `src/common/fmv/`:** `Ffmpeg.cpp` loads the DLLs, for both engines. `FmvAudio.cpp` is the
  audio queue and the clock (samples played, with the wall clock once the audio has run out), and the action engine's
  player now uses it too. The 5.1 voice is folded to stereo with `DrivingAudio_StereoFold`, the gains the mixer rings
  get.
- **The FFmpeg build** gained `eamad` and `adpcm_ea_r1`. `avcodec-62.dll` grew by 14 KB.

Checked in the running game. `jc_intro` (mission 7) plays at 25 fps on the audio clock: 6 channels at 48 kHz, half a
second queued throughout. It ends on time, and the level loads after it. With subtitles turned on, the caller's
callback (0x000e3ab0) draws its lines at the `.sub` file's frames over our picture. Subtitles are off by default (byte
0x001e476d), and then the caller passes no callback, as before. START during `paris_intro` (mission 1) skips it and
the level loads. The volume is EA's 0-127 taken as a linear amplitude, since the curve EA's mixer applies is not
mapped. The frames have not been compared pixel by pixel with EA's decoder.
