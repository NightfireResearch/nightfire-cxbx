# EA's sound library in the driving engine (SND, MIX, SFILTER, STREAM, the DirectSound driver)

A map of EA's sound library as linked into `Driving.xbe`, written to plan replacing it. Every address is
Driving.xbe's (Ghidra program `/Xbox_EU/Driving.xbe`). Names are Ghidra's unless marked *(invented)* or
*(candidate)*; *PS2: X* means the name comes from the PS2 build (`/PS2_EU_51258/DRIVING.ELF`), which links the same
library with a PS2 back end (IOP/SPU, VU0 scratchpad), so names, signatures and intent cross over but the platform
layer does not. *MW: X* means a name or layout from the Need for Speed Most Wanted (2005) decompilation
(`src/Speed/Indep/Libs/snd/9/`, SND 9.06, `docs/driving/eagl-prior-art.md`), several major versions newer than
this build: used for names and ideas only, every layout here was read from the Xbox code.

**Status (3 October 2026): nothing ported.** 305 functions, 63.7 KB, filed `platform.sound` by
`tools/subsystems_driving.txt`; 295 live (63 KB), 10 dead (0.4 KB), 0 replaced. The DirectSound library underneath
is ours already (`src/driving/sound/dsndSeam.cpp` replaces the 20 entry points EA's code calls and stubs the rest,
`xaudio2Driving.cpp` plays them), and so are the threads, mutexes, files, memory and timers the library calls
(`platform.system`, `platform.files`, `sys.xapi`: section 5). The movie player that used SNDSTRM's tap and STREAM's
memory queue is ours too (`src/driving/engine/PlayMPC.cpp`), which is what killed the ten dead functions.

Conventions are `docs/driving/eagl.md`'s: "thiscall" is MSVC's (ECX, callee pops), "cdecl" caller pops; sizes are
bytes to the next Ghidra function (padding and gap code included); "live" is `tools/function_coverage.py`'s
(reachable from a root, every unreferenced function a root). New here: **"data-dead"** means live in the graph but
never reached with the shipped disc's data (section 3.8 says how that was measured).

Contents: 1 overview, 2 data layouts and globals, 3 how it works (threads, the 100 Hz server, voices, banks,
streams, mixing, the decoder, what the disc uses), 4 module reference, 5 the interface downward, 6 the interface
upward and what is dead, 7 patching hazards, 8 quirks a port must decide on, 9 port structure, order and tests.

---------------------------------------------------------------------------------------------------------------

## 1. Overview

### 1.1 What it is

EA's portable sound library (the strings say `SNDAUTHOR Adamchan`, `0x001d9d47`), Xbox back end. Layers, top down:

- **SND API** (`SNDBANK_play`, `SNDvol`, `SND3dpos`, `SNDpitchmult`, `SNDstop`, `SNDfxinitbus`, `SNDSTRM_*`): what the
  game's audio classes (`ASystem`, `ABank`, `AVoice`, `AStream`, `AFX`, filed `engine.audio`) call.
- **Voice manager** (`SNDVOICEI_*`, `iSND*`, `SNDSYSI_100hzserver`): 224 logical voices, priority stealing,
  envelopes, LFOs, volume/pitch/pan arithmetic, run every 10 ms.
- **Banks** (`SNDbankadd`, `SNDBANKI_*`, `SNDI_parsetimbre`): `BNKl` version 5 sample banks, tagged `PT` headers.
- **Streams**: `SNDSTRM*` (request queue, `SCHl`/`SCDl` chunk parser), `SNDPKTPLAY*` (packet player), `STREAM_*`
  (the file side: a ring buffer filled through FILESYS on its worker thread).
- **Platform driver** (`SNDPLATFORM_*`, `SNDDRV_*`, `dsnd*`): the Xbox back end. It has two voice paths: a pool of
  180 DirectSound buffers that play bank samples in hardware, and EA's **software mixer** whose output is six
  looping PCM DirectSound buffers, one per 5.1 speaker.
- **Software mixer** (`MIX_*`, `MIXI_*`, `SNDMIX*`, `mixc`) and its **filter graph** (`SFILTER_*`): per-voice pull
  chains of unpacker (decoder) -> resampler -> low/high pass -> time stretch, mixed into float accumulators, an
  optional software reverb bus, and a float -> 16-bit stage per speaker.
- **Decoders**: EA-XA (`SND::CEAXABLKDecf`, `SND::decodexac`), EA MicroTalk (`decodemut`, `readsamples`,
  `FUN_00146f00`), 16-bit PCM (`decode16x87`).

### 1.2 Modules

The library is one address range (`0x0013b7b0..0x0014bee0`) plus five dead SNDSTRM/STREAM helpers linked late
(`0x00150360..0x001503d0`) and six exception funclets (`0x00156ba0..0x00156c00`), but within it the modules
interleave (the platform driver's functions sit in four places), so they are grouped by function here
(`snd_mods.py`-style classification; the coverage tool files them all under one name):

| module | where (first..last byte) | funcs | KB | dead now | data-dead (KB) | named |
|---|---|---|---|---|---|---|
| A system: init/restore, the 100 Hz server, SNDMEMI heap, SNDLINKI lists, mutex, random | `0x0013b7b0..0x00142ee0` | 35 | 2.9 | 1 | 0 | 35 |
| B voice manager and control API: SNDVOICEI, `iSNDcalc*`, `SNDvol`/`SND3dpos`/..., fx bus, render-mode choice | `0x0013c5c0..0x00142ea0` | 27 | 4.5 | 0 | 0 | 27 |
| C banks: `SNDbank*`, `SNDBANKI_*`, `SNDI_parsetimbre`, tag reader | `0x0013cc20..0x00144af0` | 16 | 4.0 | 0 | 0 | 16 |
| D streams, SND side: `SNDSTRM*`, `SNDSTRMI*`, `SNDPKTPLAY*`, `SNDI_patchtohdr` | `0x0013b990..0x001503b0` | 44 | 7.1 | 3 | 0 | 44 |
| E STREAM, the file side | `0x0014aba0..0x0014bee0`, `0x001503b0..0x001503e0` | 30 | 4.9 | 6 | 0 | 19 |
| F platform driver: `SNDPLATFORM_*`, `SNDDRV_*`, `dsnd*`, the buffer pools | `0x0013d430..0x00144a30` | 29 | 8.2 | 0 | 1 (0.7) | 27 |
| G software mixer, software reverb | `0x001413e0..0x0014a2e0` | 54 | 10.1 | 0 | 0 | 32 |
| H SFILTER graph: unpackers, resampler, LPF/HPF, time stretch, output stage | `0x001436c0..0x00146bb0`, funclets | 56 | 8.4 | 0 | 27 (4.2) | 46 |
| I decoders: EA-XA, MicroTalk, PCM16 | `0x00146bb0..0x0014a260` | 14 | 13.7 | 0 | 7 (12.6) | 12 |
| **total** | | **305** | **63.7** | **10** | **35 (17.6)** | **258** |

"Data-dead" by module (section 3.8): F `FUN_00142150` (the hardware packet-voice feeder); H the MicroTalk, PCM16 and
non-packet EA-XA unpackers and the whole time stretch; I `decodemut`, `initmut`, `readsamples`, `FUN_00146e00`,
`FUN_00146f00`, `decode16x87`, `CEAXABLKDecf::GetState`. Together with the 10 dead that is 45 functions, 18.0 KB, about 28% of the bytes - most
of it the 9,440-byte `FUN_00146f00`.

"named": not `FUN_`. Ghidra plates with `[symbol-matching]` blocks are common here (most SND/MIX/SFILTER names came
from PS2 Version Tracking reviews); the 47 `FUN_` functions are the reverb building blocks, the STREAM internals, the
time-stretch helpers and the MicroTalk internals (4.7-4.9 give candidates).

### 1.3 How the parts relate

```
 engine.audio: ASystem, ABank, AVoice, AStream, AFX           (main thread, under SNDSYS_entercritical)
   |  SNDSYSI_init, SNDbankadd, SNDBANK_play, SNDvol/3dpos/pitchmult/fxlevel/stop, SNDSTRM_create/queuefile/...,
   |  SNDfxinitbus/SNDfxmasterlevel
   v
 B voice manager (224 voices) <---- A SNDSYSI_100hzserver (SND thread, every 10 ms)
   |      ^                              |-> iSNDserve: GetStatus on hardware buffers, frees finished voices
 C banks  |  D streams: SNDSTRMI_service (main thread, SYNCTASK) -> SNDPKTPLAY packets
   |      |      ^ STREAM_get/release        E STREAM ring <- FILESYS worker thread (open/read callbacks)
   v      v
 F platform driver
   |-- voices 0..191, render mode 0x420 ("hardware"): 152 ADPCM + 28 PCM pooled DirectSound buffers
   |     SetBufferData(sample pointer in the bank), Play, SetFrequency, SetMixBinVolumes_8, SetVolume, SetFilter
   |-- voices 192..223, render mode 0x24 ("main CPU"): G software mixer
   |     MIX voice = H filter chain [unpacker (I decoder) -> rsf resampler -> LPF -> HPF -> (time stretch)]
   |     -> mixc into 6 float accumulators -> (software reverb) -> SFILTER_ft24_32 -> six 16-bit rings
   |     dsndMixProcess (SND thread) writes 20 ms ahead of the rings' write cursor
   v
 DirectSound entry points (our seam) -> XAudio2 (xaudio2Driving.cpp)
```

Measured: which path a sound takes is decided by the render-mode word in its `PT` header (tag `0x8c`), matched
against the two modes the game configures (`ASystem::SetOpts`: `0x420` then `0x24`). Every bank sample on the disc
says `0x420`, every stream says `0x24` (3.8). So **sound effects play as hardware buffers and music and speech go
through the software mixer**; the software mixer never sees a bank sample and the buffer pools never see a stream.

### 1.4 What MW (SND 9.06) confirmed, and how firmly

| item | MW source | checked against Driving.xbe | confidence |
|---|---|---|---|
| `PT` tag stream (`SNDI_gettag`/`getb`) | `sndcmn.h`, `spat2hdr.c` | same encoding: id byte, length byte (0xff = 4-byte length follows), big-endian value, 0xfc padding, 0xfd/0xfe markers, 0xff end | firm |
| sample representation codes | `sndo.h` `SND_SR_*`: 4 MT10, 7/8 S16 big/little, 9 S8, 10 EAXA, 16 Layer3, 22 MT5 | `MIX_playinit`'s switch uses 4, 7, 8, 9, 10, 14, 15, 16, 0x40; `SNDPLATFORM_playtimbre` uses 20 (0x14) = Xbox ADPCM (the vgmstream EA codec table agrees) | firm for 4, 7-10, 20 |
| `SNDIPATCHHEADER` | 0xfc bytes in SND 9 | Xbox `SNDI_parsetimbre` writes up to +0xb4 and per-channel tables at +0x84..+0xa8; the field order (numchan +3, samplerep +0x13, rendermode +0x18, samplerate +0x56, frames +0x58, loop +0x5c/+0x60, sample offsets +0x68) matches MW's first 0x80 bytes | first 0x80 bytes good, rest differs |
| `SFILTERNODE` | `sfilter.h`: 0x1c, `filterfn` +0, `restorefn` +4, next +8, priority +0x18, requester +0x1a | Xbox filters: process at +0, restore at +4, upstream at +8, priority +0x18 (0xf0 unpacker, 200 time stretch), requester byte +0x1a passed as the 5th argument; the six output-stage nodes are 0x1c apart | firm |
| `SNDLINKLIST` | 0xc: head, tail, count | Xbox lists at `0x00244c48`/`0x00244c60` are 0xc each, count at +8 | firm |
| `SNDSYSOPTS`/`SNDGLOBALSTATE` | `sndcmn.h` | the Xbox keeps the same idea as separate globals (`0x00244cc8..0x00244fbc`); offsets do not carry over | names only |
| `CHANPUB` (MW voice) | 0x90 | Xbox `SNDVOICEI` is 0x88 with a different order | names only |

---------------------------------------------------------------------------------------------------------------

## 2. Data layouts and globals

Ghidra has structs for most of these (`SNDVOICEI`, `SNDLINKNODE`, `SNDPLATFORMVOICE`, `MIX`, `MIXRELATED`, `FXBUS`,
`SNDPKTPLAY`, `SNDSTRMI`, `STREAM`, `STREAMINTERNAL`, `SFILTER`, `UNPACKSTATEcommon`, in
`tools/structs_driving.json`); where they are wrong that is said.

### 2.1 Logical voice `SNDVOICEI` (0x88), array at `sndvoicei_buffer` (`0x00244f3c`)

224 of them (`NUM_VOICES`, `0x00244ed8` = 192 hardware + 0 + 32 software), allocated by `SNDSYSI_init` from the
sound heap. A sound of n channels takes n consecutive-in-the-sort voices; the lowest is the master.

| off | meaning |
|---|---|
| +0x00 | handle (voice index OR'd with a generation counter `0x00245370` += 0x100); -1 on a slave voice |
| +0x04 | platform voice indices of the sound's channels (up to 6 shorts; Ghidra: `maybePlatformVoiceIdxs`) |
| +0x10, +0x12 | patch number, bank handle (0xffff for a stream) |
| +0x14 | total frames; +0x18 sustain (loop) end |
| +0x1c | azimuth (pan + play option), +0x1e elevation |
| +0x20 | sample rate (u16) |
| +0x22 | sample representation (20 = Xbox ADPCM, 10 = EA-XA ...) |
| +0x23 | channel count |
| +0x24 | flags = the chosen render mode: `0x400` hardware buffer, `0x4` main-CPU mixer (the code tests `& 0x410`, `& 0x400`, `& 4` interchangeably) |
| +0x28 | master voice index on a slave, -1 on the master |
| +0x2a | priority (0..100; 101 = streams, never stolen by a lower one) |
| +0x2c | allocation tick (`0x00244edc`, the server's tick counter): the steal order |
| +0x30..+0x44 | fade (per-tick step, target, current, 16.16) and envelope (per-tick step, current, ticks left) |
| +0x48 | built-in volume (patch vol x velocity / 127) |
| +0x49 | final volume 0..127 (`iSNDcalcvol`) |
| +0x4a | built-in azimuth |
| +0x4c | per-channel azimuth offsets |
| +0x58..+0x5a | envelope count, current envelope, release envelope |
| +0x5b..+0x5d | programmed volume, dry level, fx level |
| +0x5e | fx send level (short) |
| +0x60 | velocity/pitch-bend byte |
| +0x61..+0x64 | LFO lengths and positions (volume, pitch) |
| +0x65 | in use |
| +0x66 | time multiplier |
| +0x68..+0x78 | envelope table, volume scaling table, bend table, volume LFO, pitch LFO pointers (into the bank) |
| +0x7c | pitch LFO depth; +0x7e bend range x 100 |
| +0x80 | detune in cents |
| +0x82 | detune as a 4.12 multiplier (`iSNDdetunetolinear`, cached, 0 = recompute) |
| +0x84 | programmed pitch multiplier (4.12) |
| +0x86 | final pitch multiplier = +0x84 x +0x82 >> 12 |

### 2.2 Platform voice `SNDPLATFORMVOICE` (0x18), array at `sndPlatformVoices` (`0x00244c80`)

+0x00..+0x0b six per-speaker gains (shorts; the speaker-volume table output of `SNDI_aztospkrvol`, high byte used
as a 0..127 index into the volume table), +0x0c the hardware buffer node (2.3), +0x10 playing flag, +0x14 looping
flag (the Play flag to resend on resume).

### 2.3 Hardware buffer node (0x30; Ghidra calls it `SNDLINKNODE`)

`SND_BUFFER_LIST` (`0x00244c44`): 180 nodes from the sound heap (0x21c0 bytes). +0x00/+0x04 next/prev (Ghidra's
`tail`/`head` are the wrong way round), +0x08 the DirectSound buffer, +0x0c platform voice, +0x0e pool (0 ADPCM,
1 PCM), +0x10 sample memory owned by the node (packet voices only), +0x14 ring size, +0x18 ring write position,
+0x1c..+0x2c packet-voice state, +0x20 packet player index (-1 for a bank sample).

Lists: `LList_maybeFreeDsndBuffers[2]` (`0x00244c60`) and `Llist_MaybeActiveDsndBuffers[2]` (`0x00244c48`), 0xc
each (head, tail, count); pool sizes `NUM_SND_BUFFERS1` = 152 (`0x00244c78`), `NUM_SND_BUFFERS2` = 28
(`0x00244c7c`). Buffers are 48 kHz mono, pool 0 Xbox ADPCM (`wFormatTag` 0x69, 36-byte blocks of 64 samples),
pool 1 16-bit PCM, made by `dsndCreateBufferAndMixBins` with seven mix bins (FC, FR, BR, BL, FL, LFE at 0 dB; the
first FX send at -100 dB).

### 2.4 Software mixer

| address | name | meaning |
|---|---|---|
| `0x00244c88..0x00244cb4` | `dsndMixes` | six DirectSound ring buffers, then six ring memory pointers (Ghidra's `MIXRELATED` is 0x1c; it is really 0x30) |
| `0x00244cb8` | | ring length in frames: 48000 x 50 ms rounded down to 16 = 2400 (4800 bytes each) |
| `0x00244cbc` | | mix lead: 20 ms = 960 frames |
| `0x00244cc0` | | next frame to mix in the rings |
| `0x00245990` | `sndmix` | output rate (48000) |
| `0x00245994` | `NUM_MIXES` | byte 0 = MIX voices (32), byte 1 = output channels (6) |
| `0x00245998` | `MIX_VOICE_FREE_FUNC` | `SNDDRV_mixvoicefree` |
| `0x0024599c` | | output step per slice in bytes (0x400 = 512 frames x 2) |
| `0x0024599f` | | reverb state: 0 off, 1 "fx2" (mode 10), 2 standard |
| `0x002459a0` | | FX init function (`SNDMIXI_fxinit`) |
| `0x002459a4` | `unpackerInitFuncs[32]` | slot 0: the per-slice FX process hook; slots 4-6 PCM16 (plain, loop, packet), 7-9 EA-XA, 10/12 MicroTalk (no 11: no looping MicroTalk) |
| `0x00245a28` | `unpackerInitSizes[32]` | the matching state sizes (0x2c..0xd74) |
| `0x00245ab0`, `0x00245ab4` | | two 0x40bc-byte scratch allocations; `0x00245ab8`/`0x00245abc` their 64-byte-aligned float buffers |
| `0x00245ac0..` / `0x00245ad8..` | | six 0x840-byte channel accumulators, raw and aligned |
| `0x00245af0..` | | per channel: gain still ramping to zero |
| `0x00245b0c..`, `0x00245b24..` | | per channel: output filter list, optional master filter (master low pass) |
| `0x00245b3c..` | | six output-stage `SFILTERNODE`s (0x1c each), process = `SFILTER_ft24_32` |
| `0x00245be4` | `MixList` | 32 `MIX` (0x60): +0 state (1 initialised, 2 playing), +1 gains changed, +4/+0x1c six current/target dry gains, +0x34/+0x38 fx send current/target, +0x3c last sample, +0x40 filter chain head, +0x48 unpacker, +0x4c time stretch, +0x50 resampler, +0x58 LPF, +0x5c HPF |
| `0x00245be8` | | the mix function pointer: `mixc` |
| `0x00245bf0..0x00245dc8` | | software reverb taps (delay lines sized by `findprime`) |
| `0x00245dd0` | `sndfx` | the FX send accumulator (to `0x002475d0`) |
| `0x002475f4`, `0x002475f8` | `CODA_New`, `CODA_Delete` | the decoders' allocator: the thunks `0x00141860`/`0x00141870` to `SNDMEMI_alloc`/`free` |

### 2.5 Streams

`sndss[32]` (`0x00244ba8`, `NUM_STREAMS` = 16 at `0x00244d0f`): `SNDSTRMI` (0x138 + 0x28 per extra request,
memory supplied by `AStreamPriv`, which asks for `SNDSTRM_overhead(4, 15)` plus its buffer) holding the request
lists, the packet player index, the STREAM, the play options and two 26-dword patch-header copies (current, next:
a new header that differs restarts the player). `sndpps` (`0x002452e4`): packet players (`SNDPKTPLAY`, 0x48+: status
+0, release/frames callbacks +0x34/+0x38 and context +0x3c, format +0x40). Packet callbacks are queued at
`0x00244fe0`/`0x00244fe4` and delivered by `SNDPKTPLAYI_flushcallbackdata` after each mix tick.

STREAM (`STREAM_create`): a `STREAMINTERNAL` header tagged `STRM` (`0x4d525453`), its own MUTEX at +4, request
records (0x124 each: file name at +0x14, size +0x118, state +4: 1 queued, 2 reading, 3 done, 4 cancelled), handle
records (0xc), reader records (0x10) and a ring of chunks aligned to 128 bytes (chunk header `{size | reader << 24}`,
-1 = wrap, -2 = skip); read size 0x800, 0x1000 or 0x2000 by buffer size; greedy level = buffer / 3.

### 2.6 Other globals

| address | name | meaning |
|---|---|---|
| `0x00244c30` | `systaskadded` | SYNCTASK registered once |
| `0x00244c38` | | the SND thread's next deadline (ms) |
| `0x00244c3c`, `0x00244c3d` | `SNDDRV_isRunning`, `SNDDRV_shouldContinueRunning` | thread state |
| `0x00244c40` | `p_setfx_func` | written by `SNDPLATFORM_fxinit`, never read |
| `0x00244c84` | `maybeIDirectSound` | the device |
| `0x00244cc8..0x00244d78` | `minSampleRate`, `maxSampleRate`, `platformSampleRate` (`0x00244cf2`, 48000), `NUM_BANKS` (`0x00244cf0`), `mixVoiceOffset2` (`0x00244cfa`, 192 hardware voices), `mixVoiceOffset1` (`0x00244cf9`, 0), `0x00244cf6` (32 mixer voices), `0x00244d10` (output mode: `SetOpts` writes 1 or 2; caps say 5), `0x00244d15` render-mode count (2), `0x00244d18` render modes (`0x420`, `0x24`) | the options (MW: `SNDSYSCAP`/`SNDSYSSET`), saved copy at `0x00244de8` |
| `0x00244dd0` | `sndopts2` | the vector table (MW: `SNDSYSVEC`), filled with `dummyNullFunction` |
| `0x00244ed0` | `SNDSYS_is_inited` | |
| `0x00244ed1` | | master volume (0x7f) |
| `0x00244ed3` | | critical-section nesting count (incremented, never decremented: cosmetic) |
| `0x00244ed4`, `0x00244ee0` | `numSndSysCallbacks`, `sndSysCallbacks` | 100 Hz clients: never registered |
| `0x00244ed5`, `0x00244ef8` | `numSNDserverClients`, `SNDserverclients` | main-thread clients: `SNDSTRMI_service` while any stream exists |
| `0x00244ed6`, `0x00244f10` | | user-data clients: never registered |
| `0x00244edc` | | server tick counter |
| `0x00244f2c`, `0x00244f38` | `SNDbank_on_exit_func`, `SNDSTRM_on_exit_func` | |
| `0x00244f40` | `sndbanki_buffer` | banks (8 bytes each: header pointer, flag) |
| `0x00244f44` | `FXBUS[2]` | per bus (0 main CPU, 1 hardware): mode, master level, delay, feedback |
| `0x00244f6c` | `pSndHeap` | the SNDMEMI heap (256 KB "Audio Heap" from `UMemory::Alloc` in `ASystem::ASystem`) |
| `0x00244f7c..0x00244fb6` | | speaker azimuth tables for 2/4/5.1 output |
| `0x00244fb8`, `0x00244fbc` | `SNDDRVPreFrameCb`, `SNDDRVPostFrameCb` | read by the SND thread, never written |
| `0x00244fc0` | `SoundMutex` | the CRITICAL_SECTION (`RtlInitializeCriticalSection`, kernel imports `0x00189cf0`/`0x00189c40`/`0x00189c3c`) |
| `0x00245364..0x00245370` | | `SNDVOICEI_alloc` scratch and the handle generation |

Constant tables: volume table `0x001a71e0` (128 shorts, hundredths of a dB: -10000, -4200, -3700 ... -7, 0),
ring mix bins `0x001a72e0` (2, 1, 5, 4, 0, 3: ring i feeds FC, FR, BR, BL, FL, LFE), DirectSound low-pass
coefficients `0x001a7350` (256 shorts, index = cutoff >> 5), the effects image `0x001a3e80` (0x3360 bytes: the
size `SNDPLATFORM_init` passes; `docs/audio-inventory.md` derived 13,472 for the driving image from its container,
which does not agree and is worth a second look), I3DL2 listener presets from `0x001d98d8` (23, chosen by
`DirectSound_SetListenerRelated`), the default FX description `fxdefault` `0x001d98c0`, the EA-XA predictor
coefficients `0x001da9b8`/`0x001da9c8` (4 floats each: 0, 0.9375, 1.796875, 1.53125 and 0, 0, -0.8125, -0.859375)
and shift tables `0x001da9d8` (16 x 16 floats: nibble << (12 - shift)).

---------------------------------------------------------------------------------------------------------------

## 3. How it works

### 3.1 Threads and locks

| thread | runs | lock |
|---|---|---|
| **SND driver thread** `SNDDRV_thread` (`0x0013d980`), made by `SNDPLATFORM_init` with `CreateThread` (stack 0x7d000), `SetThreadPriority` 15 (time critical) | every 10 ms: `SNDSYSI_100hzserver`, `dsndMixProcess` (the whole software mix: decoding, filtering, mixing into the rings), `SNDPKTPLAYI_flushcallbackdata` (which can reach `STREAM_release`) | holds `SoundMutex` for the whole tick |
| **main thread** (game) | the A* classes' SND calls; `SNDSTRMI_service` through `SYNCTASK_run` -> `SNDREAL_systemtask` -> `SNDSYS_service` (registered by `SNDSYS_vectortoreal` from `ASystem::ASystem`) | `SNDSYS_entercritical` = `SoundMutex` |
| **FILESYS worker** (ours, `src/driving/platform/FileSys.cpp`) | STREAM's completion callbacks `FUN_0014aee0` (open done), `FUN_0014af10` (close done), `FUN_0014b660` (read done) -> `FUN_0014b730` (next read), `FUN_0014af50` (next request) | the STREAM's own MUTEX (+4), not `SoundMutex` |
| XAudio2's thread (ours) | reads the rings and the bank samples the game handed over by pointer | none: the rings are read live, as the console's hardware read them |

The pacing: `nextDeadline += 10; sleep(nextDeadline - getTickCount())`, a negative wait clamped to 1 ms - after a
stall the thread runs back-to-back ticks a millisecond apart until it has caught up. `getTickCount` reads
`KeTickCount`, which the loader advances (`docs/driving-engine-plan.md` 0.1: a stopped clock once slowed the server to
3 Hz). None of the sound threads are in lockstep (`NIGHTFIRE_LOCKSTEP` fixes the simulation per frame only).

### 3.2 The 100 Hz server

`SNDSYSI_100hzserver` (`0x0013b7b0`): tick counter += 1; `iSNDserve` (`0x0013e530`): for every node on the two
active hardware lists whose voice is a master, a bank voice asks `IDirectSoundBuffer_GetStatus` and, if not playing,
is collected; a packet voice runs `FUN_00142150` (refills its ring, 3.5); the collected voices are
`SNDPLATFORM_stop`ped (buffer back to the free list tail, voice freed). Then the (empty) 100 Hz client list, then for
every voice in use: pitch LFO step -> `iSNDcalcpitch` + `SNDPLATFORM_setpitch`; volume LFO step; fade step (a fade
below zero stops the voice); envelope step (next segment from the envelope table when the current one runs out, stop
after the last); if anything changed `iSNDcalcvol` + `SNDPLATFORM_setvol`. All integer arithmetic (16.16 fixed
point), no FPU.

### 3.3 Voices: allocation, volume, pitch, pan

`SNDBANKI_playtimbre`/`SNDPKTPLAY_start` choose a render mode (`SNDI_validrendermode`: the first configured mode
whose location bits (`& 0x71c`) and type bits (`& 0xe0`) intersect the patch's; stereo needs bit 0x80 clear), get
its voice range (`SNDPLATFORM_getvoicerange`: `0x400` -> 0..191, `0x10` -> none, `4` -> 192..223) and call
`SNDVOICEI_alloc` (`0x0013fb70`): first free voices oldest-first, then steal the lowest priority below the caller's
(equal priority only if `0x00244d04`), oldest first; the chosen voices are bubble-sorted and the lowest becomes the
master; a stolen voice is `SNDstop`ped. Then the voice record is filled from the patch header and play options
(random pan/detune/volume through `randrange`/`iSNDrandom`, a linear-congruential generator seeded by
`SNDI_randomseeed`).

- `iSNDcalcvol`: fade x envelope x built-in x master / 0x1f417f, then optional volume-LFO and volume-scaling tables:
  a 0..127 volume.
- `iSNDcalcpitch`: cents = detune + bend + pitch LFO; `iSNDdetunetolinear` (table) -> 4.12; x programmed multiplier.
- Hardware voice (`SNDPLATFORM_setvol`/`setpitch`/`set3dpos`/`setfxlevel`/`lowpass`): volume
  `IDirectSoundBuffer_SetVolume(volTable[ftol(vol x prog / 127 + 0.5)])` (x87); frequency
  `ftol(rate x pitch / 4096)` clamped to 188..191983 Hz; pan by `SetMixBinVolumes_8` with six bins (FC, FR, BR, BL, FL
  from the speaker gains of `SNDI_aztospkrvol`, LFE -10000), fx send by `SetMixBinVolumes_8` on bin 0x0b; low pass by
  `SetFilter` with the coefficient table. A pitch multiplier of 0 is a pause: Stop, and Play again when it returns.
- Mixer voice: `SNDMIX_setdrygain(voice, speaker, gain x vol x prog x 1.89e-9)`, `MIX_setpitch((rate << 16) / 48000
  x pitch >> 12)`, `MIX_setfxlevel`, `MIX_setlowpass(cutoff / 24000)`, `MIX_sethighpass`, `MIX_settimemult`.

### 3.4 Banks

`ABank::ABank` loads `data\audio\...\*.bnk` with `UFileLoader::FileLoadz` (refpack-compressed on disc, `10 FB`) and
calls `SNDbankadd` (returns the bank handle; result 7 makes the game keep only the header, `SNDbankheadercopy`).
A `BNKl` v5 file: `"BNKl"`, u16 version 5, u16 patch count, u32 header size, u32 total size, then a table of
self-relative u32 offsets to `PT` patch headers (0 = empty slot), then the sample data. `SNDBANK_play` ->
`SNDBANKI_playpatch` (`0x001405d0`: `SNDI_parsetimbre` the patch's tag stream into a `SNDIPATCHHEADER` on the stack,
key and velocity ranges, user-data callback, timbre loop) -> `SNDBANKI_playtimbre` -> `SNDPLATFORM_playtimbre`
(`0x00142b10`), which for a hardware voice pops a node from pool 0 (ADPCM, `samplerep` 20) or pool 1, borrowing
from the other pool through `FUN_0013d550` when empty (Release the borrowed node's buffer and create one of the
other format), `SetBufferData(bank base + sample offset, bytes)` - the console's hardware read the sample **in place
in the bank** - `SetLoopRegion` for a looping sample (start and length rounded to 64-sample ADPCM blocks), position
0, pitch, pan, fx, volume, low pass, `Play(looping)`.

### 3.5 Streams

`AStreamPriv` creates a stream (`SNDSTRM_create(opts, 4 requests, 15 chunks, memory)` -> `SNDSTRMI_create`:
`SNDPKTPLAY_create` + `STREAM_create`, registers `SNDSTRMI_service` as a main-thread server client); `AStream::Play`
queues a file (`SNDSTRM_queuefile` -> `STREAM_queuefile`: the FILESYS worker opens it and fills the ring) and polls
`SNDSTRM_status`/`requeststatus`. Every main-thread tick `SNDSTRMI_service` pulls up to 10 chunks with `STREAM_get`:
an `SCHl` header goes to `SNDSTRMI_parseheader` (`SNDI_patchtohdr`; a format change restarts the player;
`SNDPKTPLAY_start` allocates the voice(s) with priority 101 and `SNDPLATFORM_packetplay`), an `SCDl` data chunk to
`SNDSTRMI_parsedata` -> `SNDPKTPLAY_submit` (per channel: the chunk's data offset), anything else is released.

Playback is on the SND thread: the mixer voice's unpacker (`SFILTER_unpackxapf`) takes packets with
`SNDPKTPLAYI_get`, decodes them, and reports consumed frames with `SNDPKTPLAYI_freeframes`; the frames and release
callbacks are queued and delivered after the mix (`SNDSTRMI_framescallback`, `SNDSTRMI_releasecallback` ->
`STREAM_release`), which is how stream position, and through it the game's "has this line finished" tests, advance.

`SNDPLATFORM_packetplay`'s hardware branch (a stream on a hardware voice: a looping DirectSound ring of ADPCM or
PCM the size of 30 ms of samples, refilled by `FUN_00142150` from the 100 Hz server) exists but is never taken
(3.8).

### 3.6 The software mixer

`dsndMixInit` (`0x0013d5e0`, from `SNDPLATFORM_init`): `MIX_create` (32 voices, 6 outputs, 48 kHz: buffers 2.4, the
unpacker tables, the output nodes `SFILTER_ft16init`), then six DirectSound buffers of 2400 frames of 16-bit mono PCM
with ring memory from the sound heap, each routed to one speaker (`SetMixBins`, the others at -100 dB), volume 0,
Play looping - started once, never stopped.

Each tick `dsndMixProcess` (`0x0013d820`) reads ring 0's **write** cursor, sets the target 960 frames (20 ms) past
it, and mixes from the last position to the target (in two parts across the wrap) through `MIX_audio`, which cuts it
into slices of at most 512 frames for `MIX_audioslice` (`0x00141db0`):

1. clear the six float accumulators; finish any gain ramps to zero;
2. for every playing MIX voice: on a gain change, `SNDMIXI_volramp` pulls 16 frames and ramps the gains across them
   (`MIXI_interpolatemix`); then pull the rest from the voice's filter chain head into the scratch buffer (a negative
   count ends the voice: `MIX_stop` + `SNDDRV_mixvoicefree`), and `mixc` it into each accumulator with a non-zero gain
   and into `sndfx` with the fx send;
3. run the FX hook (`unpackerInitFuncs[0]`: `SNDMIXI_fxadd`, or `0x001437e0` in mode 10) over the slice;
4. per speaker: the optional master low pass, then `SFILTER_ft24_32`: float + 12582912.0 (1.5 x 2^23), the low bits
   as the sample, clipped to -32768..32767, into the ring.

The chain per voice (`MIX_playinit` `0x00141910`, `MIX_setpitch`, `MIX_setlowpass`, `MIX_sethighpass`), ordered by
priority: unpacker (picked by sample representation x {bank, bank looping, packet}) -> time stretch (only with a
patch's time-stretch data) -> `SFILTER_rsf` resampler (4-sample history, 16.16 phase; `0x10000` = pass through; the
kernel `FUN_001462b0` is SSE assembly) -> `SFILTER_lpfRC` (one-pole) -> `SFILTER_hpfFIR8` (8-tap FIR designed by
`FUN_00146930`). Each node's process function is `fn(node, frames, out, scratch, requester)` and pulls its upstream
node (+8) first.

Software reverb (bus 0, `AFX::Update` -> `SNDfxinitbus` -> `SNDPLATFORM_fxinit` -> `SNDDRV_setfx`): mode 0 off; mode
1 uses the bus's own description, others `fxdefault`; description byte +2 = 10 selects the "fx2" network
(`SNDMIXI_initfx2`/`SNDMIXI_initfx`, the never-made `0x001437e0` as the hook), otherwise `MIX_initreverb` builds up to
three groups of comb taps with prime delay lengths (`findprime`) and `SNDMIXI_fxadd` is the hook. Bus 1 (hardware)
only picks an I3DL2 preset for `IDirectSound_SetI3DL2Listener` (`DirectSound_SetListenerRelated`, 23 cases).

### 3.7 The EA-XA decoder

`SND::CEAXABLKDecf` (0xa8, thiscall): `Feed(data, bytes, frames)` gives it a packet's channel data, `SetState`
sets the two predictor history samples from the packet's first two shorts (`SFILTER_unpackxapf` reads them as
`(float)short`), `Decode(&out, frames)` copies any samples left from a partial block, decodes whole blocks with
`SND::decodexac` (`0x00149d80`) straight into the output and the remainder into an internal 28-sample buffer.
`decodexac` is hand-written SSE scalar assembly (it pushes and pops every register; Ghidra shows it as `__fastcall`,
wrongly): per 15-byte block, byte 0 = coefficient index (high nibble) and shift (low nibble), then 14 bytes = 28
nibbles, `s = s1 x k1[i] + table[shift][nibble] + s2 x k2[i]` in single precision, in exactly that order, output
as float. EA-XA "R1"-style, 28 samples per 15 bytes per channel.

### 3.8 What the disc uses

Measured on every bank and stream on the disc (`disc/driving/*.viv`, `*.mus`, `*.spe`; refpack unpacked; the
`PT` tag streams parsed with the same rules as `SNDI_gettag`):

- **Banks**: 56 `.bnk` entries, 42 distinct names, 1129 non-empty patch headers in the distinct banks. **Every one is
  sample representation 20 (Xbox ADPCM), mono, render mode `0x420` (hardware).** Rates: 16000 (436 + loops), 18000,
  24000, 48000, 44100, 32000, 22050-ish, 11025, 8000 and many odd rates on looping samples (e.g. 23998, 15975); 174
  patches loop (tags 0x86/0x87). Tags present: 0x06, 0x07, 0x08, 0x0a, 0x0e, 0x10, 0x11, 0x13, 0x19, 0x84-0x88,
  0x8c, 0xa0. No time-stretch (0x98-0x9b) or codebook tags.
- **Streams**: 7 `.mus` files (35 music streams) and 28 `.spe` files (807 speech streams, 4 languages): all `SCHl`
  with `PT` platform 7, **EA-XA (10), 48 kHz, mono (472) or stereo (370), render mode `0x24` (main-CPU mixer)**, no
  loop tags, tag 0x14 (user data, 181) and 0x9c/0x9d (per-channel azimuth offsets, 70). No `.asf` inside the archives.
- **Movies** (48 `.mad` entries) are played by our `PlayMPC.cpp`, not by this library.

So the live data paths are: hardware ADPCM bank voices (one-shot and looping) and mixer EA-XA packet voices
(`SFILTER_unpackxapf`), resampler in pass-through or at the stream's pitch multiplier, LPF/HPF if the game asks,
the reverb if `AFX` turns bus 0 on. **Data-dead**: MicroTalk (`SFILTER_unpackmt*`, `initmut`, `decodemut`,
`readsamples`, `FUN_00146e00`, `FUN_00146f00`: 13.1 KB), PCM16 (`SFILTER_unpack{f,lf,pf}*`, `decode16x87`), EA-XA
from banks (`SFILTER_unpackxaf*`, `unpackxalf*`, `GetState`), the time stretch (`SFILTER_timestretch*`, `stretch*`,
`FUN_00143be0`, `FUN_00143e10`, `FUN_00143f20`, `FUN_00144050`), the hardware packet voice (`FUN_00142150` and the
first branch of `SNDPLATFORM_packetplay`), and pool 1 (PCM) except as a borrow source. Whether the software reverb
and the high pass run depends on `AFX` settings and play options, not on file formats; check them on the runner
before calling them live.

---------------------------------------------------------------------------------------------------------------

## 4. Module reference

`python tools/function_coverage.py --driving platform.sound --why` lists every function with its callers.

### 4.1 A system (35 functions, 2.9 KB)

`SNDSYSI_init` (`0x0013d140`: heap, options, voice and bank arrays, mutex, `SNDPLATFORM_init`, azimuth tables,
`SNDI_precalcaztospkrvol`), `SNDSYS_restore`, `SNDSYS_getopts` (-> `SNDPLATFORM_outputcaps`), `SNDSYS_setops` (->
`SNDPLATFORM_outputset`), `SNDSYS_vectortoreal` (SYNCTASK + `REAL_addexit(~ASystem)`), `SNDREAL_systemtask`,
`SNDSYS_service`, `SNDSYSI_100hzserver`, `SNDSYS_enter/leavecritical`, `SNDI_mutexalloc/lock/unlock` (kernel
critical section), `iSNDserveraddclient`/`removeclient`, SNDMEMI (`init` `0x0013f710`, `alloc` `0x0013f780`, `free`
`0x0013f880`; `0x0013f770` is a thunk to `0x001429d0`, which Ghidra calls `SNDMEMI_restore` but which returns the heap's
percentage in use *(candidate: SNDMEMI_percentused)*; `0x00141860`/`0x00141870` are jump thunks used as the decoders'
allocator), SNDLINKI (`init`, `push`, `pushtail`, `pop`, `remove`), `iSNDmulu64`/`divu64`, `SNDI_randomseeed` (sic),
`iSNDrandom`, `randrange`, `SNDstopall`, `~ASystem` (16 bytes, the exit hook). Dead: `SNDSYS_inited` (only the old
movie player asked).

### 4.2 B voice manager and control (27 functions, 4.5 KB)

`SNDVOICEI_alloc`/`free`/`get`, `iSNDserve`, `iSNDcalcvol`, `iSNDcalcpitch`, `iSNDdetunetolinear`, `iSNDpatchkey`
(handle -> voice, checking the generation), the API `SNDstop`, `SNDvol`, `SND3dpos`, `SNDpitchmult`, `SNDfxlevel`,
`SNDover`, `SNDautovol` (fade), `SNDplaysetdef` (default play options), `SNDfxinitbus`, `SNDfxmasterlevel`,
`SNDCTRLI_getfxbus`, `SNDCTRL_filteradd`, `SNDCTRL_lowpass`, `SNDI_calcfxlevel`, `SNDI_aztospkrvol`,
`SNDI_precalcaztospkrvol` (448 bytes: the azimuth -> speaker gain tables), `SNDI_pantoazimuth`,
`SNDI_validrendermode`, `SND_attrsetdef`. Integer except the gain products in the platform setters (4.6).

### 4.3 C banks (16 functions, 4.0 KB)

`SNDbankadd` -> `SNDBANKI_alloc`, `SNDbankremove` (stops the bank's voices, user-data callback), `SNDbankpatpresent`,
`SNDbankheadersize`/`headercopy` (`_memmove`), `SNDBANK_play` -> `SNDBANKI_valid`/`getppatch`/`playpatch`/
`findfreekey`/`playtimbre`/`userdatacallback`, `SNDI_parsetimbre` (`0x00140aa0`, 1488 bytes: tags 0x00..0x25 into
the low fields, 0x80..0xa7 into the sample fields, 0x98..0x9b/0xa4/0xa5 per-channel tables, 0x14 user data, 0xfe a
further timbre; defaults samplerep 8, one channel, 24000 Hz), `SNDI_gettag`, `SNDI_getb` (big-endian, sign-extends 1-3
byte values). Pure parsing: a good injection-time shadow target.

### 4.4 D streams, SND side (44 functions, 7.1 KB)

`SNDSTRM_create`/`destroy`/`purge`/`queuefile`/`modifyhold`/`status`/`requeststatus` (`iSNDmulu64`/`divu64` for
the byte -> time sums)/`vol`/`pitchmult`/`lowpass`/`3dpos`/`autovol`/`overhead`/`setgreedylevel`, `SNDSTRMI_create`/
`service`/`queue`/`parseheader`/`parsedata`/`startstream`/`isheld`/`calcdatarate`/`framescallback`/`releasecallback`/
`removerequest`/`getstreamptr`/`getrequestptr`/`destroyall`, `SNDPKTPLAY_create`/`start`/`submit`/`submitspace`/
`framesoutstanding`/`stop`/`destroy`/`overhead`, `SNDPKTPLAYI_get`/`freeframes`/`flushcallbackdata`/
`voicetopackethandle`, `SNDI_patchtohdr` (`0x0013f1c0`, the stream variant of the tag parser: copies the 0x98..0x9b,
0xa4, 0xa5 blobs into heap memory). Dead: `SNDSTRM_overheadtap`, `SNDSTRM_queuerequestid`, `SNDSTRM_createtap` (the
old movie player).

### 4.5 E STREAM, the file side (30 functions, 4.9 KB)

`STREAM_create` (`0x0014b0c0`), `STREAM_queuefile`/`queuemem`, `STREAM_get`, `STREAM_release`, `STREAM_cancelrequest`,
`STREAM_kill`, `STREAM_destroy` (`FILESYS_closesync`, waits with `THREAD_yield`/`SYNCTASK_run` unless on the worker),
`STREAM_setgreedylevel`/`setgreedystate` (`FILESYS_priorityop`), `STREAM_gettable`/`state`/`buffersize`/`overhead`,
`freerequest`, and the unnamed internals: `FUN_0014aba0` *(candidate: release bytes, re-prioritise when below the
greedy level)*, `FUN_0014ac00` *(candidate: pop a free request)*, `FUN_0014ac70` *(candidate: append a request)*,
`FUN_0014ad20` (per-chunk bookkeeping after a read), `FUN_0014aee0`/`FUN_0014af10` (open/close completion callbacks),
`FUN_0014af50` *(candidate: start the next request: open, or reuse the open file, or read)*, `FUN_0014b660` (read
completion callback), `FUN_0014b730` *(candidate: issue the next read into the ring, compacting at the wrap)*. Dead:
`STREAM_setfilter`, `STREAM_setpriority`, `STREAM_taphandle`, `STREAM_isendofstream`, `FUN_001503b0`, `FUN_001503c0`.

### 4.6 F platform driver (29 functions, 8.2 KB)

`SNDPLATFORM_init` (`0x0013dc50`: `DirectSoundCreate`, `DownloadEffectsImage(0x001a3e80, 0x3360)`, voice array, the
180 pooled buffers, `dsndMixInit`, the thread), `SNDPLATFORM_restore` (stops the thread: clears the run flag and
sleeps until it exits; releases everything), `outputcaps`/`outputset` (the option ranges: 8000-48000 Hz, 192
hardware voices, 32 mixer voices, 16 streams, modes `0x420`/`0x24`), `getvoicerange`, `stop`, `setvol`, `setpitch`,
`set3dpos`, `setfxlevel`, `lowpass`, `highpass`, `timemult`, `filteradd`, `fxinit`, `playtimbre`, `packetplay`,
`dsndCreateBufferAndMixBins`, `FUN_0013d550` *(candidate: borrow a buffer from the other pool)*, `dsndMixInit`/
`dsndMixStop`/`dsndMixProcess`, `SNDDRV_thread`, `SNDDRV_mixvoicefree`, `SNDDRV_setfx`, `SNDDRV_getmastervoice`/
`getsamplechan`, `DirectSound_SetListenerRelated` *(candidate: SNDDRV_setdsfx; it maps the bus-1 mode to one of 23
I3DL2 presets)*, `FUN_00142150` *(candidate: SNDPLATFORM_servicepacketvoice; data-dead)*. x87 only in the gain and
frequency products (`__ftol2`).

### 4.7 G software mixer (54 functions, 10.1 KB)

`MIX_create`, `MIX_destroy`, `MIX_playinit`, `MIX_play`, `MIX_stop`, `MIX_audio`, `MIX_audioslice`, `MIX_setpitch`,
`MIX_setfxlevel`, `MIX_setlowpass`, `MIX_sethighpass`, `MIX_settimemult`, `MIX_initreverb`, `MIX_restorereverb`,
`MIXI_interpolateto0`, `MIXI_interpolatemix` (x87, 97 instructions), `MIXI_reverbblock`, `MIXI_initunpack16/xa/mt`,
`SNDMIX_setdrygain`, `SNDMIX_setmasterlowpass`, `SNDMIXI_volramp`, `SNDMIXI_initfx` (992 bytes, switch table at
`0x00143370`), `SNDMIXI_initfx2`, `SNDMIXI_restorefx2`, `SNDMIXI_fxinit`, `SNDMIXI_fxadd`, `mixc` (`0x00143b40`, SSE:
`out[i] += in[i] x gain`, scalar until 16-byte aligned, then 16 floats per iteration, counting down), `SND::CODASetNew`/
`Delete`, `findprime`, and 22 unnamed FX building blocks `0x00144bc0..0x00145720`, `0x001465a0..0x00146640` that
`SNDMIXI_initfx` creates (delay lines sized through `FUN_001465a0` -> `findprime`, x87 filters `FUN_00144e50`,
`FUN_001451b0`, `FUN_00145410`; `FUN_00144e20` installs the never-made process function `0x00144d00`, `FUN_00144cc0`
the never-made restore `0x00144ca0`). PS2 has the matching `SNDMIXI_modlapifxadd`, `MIX_setwetbuffer`,
`SNDMIX_setwetgain`, `MIX_setdistortlevel` that the Xbox lacks or never made functions of (7.3).

### 4.8 H SFILTER graph (56 functions, 8.4 KB)

`SFILTER_add`, `addtofilterlist`, `remove`, `connect`, the unpackers and their inits (`unpackxapf`/`xapfinit`/
`xapfrestore` live; `unpackxaf`, `unpackxalf`, `unpackmtf`, `unpackmtpf`, `unpackf`, `unpacklf`, `unpackpf` and inits
data-dead; `SFILTER_unpackfgetframe_unpacklfgetframe` is one folded getter), `SFILTER_rsf`/`rsfinit`/`rsfsetpitch` and
the kernel `FUN_001462b0` (528 bytes of SSE assembly, `push ebp; mov ebp, esp; pushad`, seven stack arguments),
`SFILTER_lpfRC`/`createLPFRC`/`modifyLPFRC` (with `FUN_00145720`), `SFILTER_hpfFIR8`/`createHPFFIR8`/
`modifyHPFFIR8` with `FUN_001466e0` (the FIR, x87) and `FUN_00146930` (its design, x87, calls `FUN_0014a2e0`
*(candidate: cheapsqrt - PS2 has `cheapsqrt` beside `findprime`)*), `SFILTER_initSOURCE`/`createSOURCE` (the
never-made `0x001456b0` is the process function, *PS2: SFILTER_src*), `SFILTER_ft16init` and its process
`SFILTER_ft24_32` (*PS2: SFILTER_ft16*), the time stretch `SFILTER_timestretch`/`init`/`setratio`, `stretch`,
`stretchframesneeded`, `FUN_00143be0`, `FUN_00143e10`, `FUN_00143f20`, `FUN_00144050` (register arguments, 7.1;
PS2's four unnamed helpers before `stretchframesneeded` in the same order), and three SEH funclet pairs for the
`operator new` in the EA-XA inits. PS2's `SFILTER_distort*` has no Xbox counterpart: the distort level argument is
carried and ignored.

### 4.9 I decoders (14 functions, 13.7 KB)

EA-XA: `SND::CEAXABLKDecf` ctor/`operator new`/`delete`/`Feed`/`Decode`/`GetState`/`SetState`, `SND::decodexac`.
MicroTalk (data-dead): `initmut`, `decodemut` (2176 bytes, x87), `readsamples` (*PS2: readsamples*, with
`discardbits` beside it), `FUN_00146e00` (EBX), `FUN_00146f00` (9440 bytes, 2280 x87 instructions - the synthesis
filter *(candidate: PS2 mtfilter5900, unrolled)*). PCM16 (data-dead): `decode16x87` (x87 `FILD`/`FSTP` int16 ->
float).

---------------------------------------------------------------------------------------------------------------

## 5. The interface downward

### 5.1 DirectSound

platform.sound calls 19 DirectSound entry points directly, all from module F; the seam
(`src/driving/sound/dsndSeam.cpp`) replaces each of them (20 with `DSound_CRefCount_AddRef`), so nothing in this
library reaches the real DirectSound:

| entry point | callers | what |
|---|---|---|
| `DirectSoundCreate` `0x0017c259` | `SNDPLATFORM_init` | the device |
| `IDirectSound_DownloadEffectsImage` `0x0017b59d` | `SNDPLATFORM_init` | the DSP image (0x3360 bytes) |
| `DirectSoundCreateBuffer` `0x0017c2a0` | `dsndCreateBufferAndMixBins` | the 180 pooled buffers |
| `IDirectSound_CreateSoundBuffer` `0x0017c09f` | `dsndMixInit` | the six rings |
| `IDirectSound_SetI3DL2Listener` `0x0017be1b` | `DirectSound_SetListenerRelated` | the hardware bus's reverb preset (the seam ignores it) |
| `IDirectSound_Release` `0x0017ad34`, `IDirectSoundBuffer_Release` `0x0017ad4a` | `SNDPLATFORM_restore`, `dsndMixStop`, `FUN_0013d550` | |
| `IDirectSoundBuffer_SetBufferData` `0x0017be3b` | `playtimbre`, `packetplay`, `dsndMixInit` | samples by pointer, read live |
| `IDirectSoundBuffer_SetLoopRegion` `0x0017b670` | `playtimbre`, `packetplay` | |
| `IDirectSoundBuffer_SetCurrentPosition` `0x0017b6cc` | `playtimbre`, `packetplay`, `dsndMixInit` | always 0 |
| `IDirectSoundBuffer_GetCurrentPosition` `0x0017b6ac` | `dsndMixProcess` (write cursor of ring 0), `FUN_00142150` (play cursor) | the mixer's pacing |
| `IDirectSoundBuffer_GetStatus` `0x0017b690` | `iSNDserve` | voice reclaim |
| `IDirectSoundBuffer_Play` `0x0017b634`, `Stop` `0x0017b658` | `playtimbre`, `packetplay`, `setpitch` (pause/resume), `stop`, `dsndMixInit`/`dsndMixStop` | |
| `IDirectSoundBuffer_SetVolume` `0x0017b5c4` | `SNDPLATFORM_setvol`, `dsndMixInit` | |
| `IDirectSoundBuffer_SetFrequency` `0x0017b996` | `SNDPLATFORM_setpitch` | 188..191983 Hz |
| `IDirectSoundBuffer_SetMixBins` `0x0017b5fc` | `dsndCreateBufferAndMixBins`, `dsndMixInit` | |
| `IDirectSoundBuffer_SetMixBinVolumes_8` `0x0017b618` | `SNDPLATFORM_set3dpos`, `setfxlevel` | pan and fx send |
| `IDirectSoundBuffer_SetFilter` `0x0017b5e0` | `SNDPLATFORM_lowpass` | (the seam ignores it) |

None of them take register arguments (they are the XDK's `__stdcall` wrappers; the `CMcpx*` register-argument
internals below them are stubbed and unreachable). No `DirectSoundDoWork`/`CommitDeferredSettings` calls come from
this library. Two consumers read memory behind the seam's back: the six rings (rewritten every tick) and bank sample
data (read by pointer); `xaudio2Driving.cpp` streams both, 4 ms at a time.

### 5.2 Platform, files, threads, CRT (all ours already)

- XAPI/kernel: `CreateThread` `0x0010ec6a`, `SetThreadPriority` `0x0010ea0f`, `SleepMilliseconds` `0x0010e9ab`,
  `getTickCount` `0x0010e1e0`; `RtlInitializeCriticalSection`/`RtlEnterCriticalSection`/`RtlLeaveCriticalSection`
  through the import thunks at `0x00189cf0`, `0x00189c40`, `0x00189c3c` (indirect calls: not in the xref edge list).
- platform.system: `MUTEX_create`/`lock`/`unlock`, `REALMUTEX_destroy`, `MEM_clear`, `MEM_copy`, `MEM_free`,
  `memclr` (`0x0013f600`, inside the sound range but filed `platform.system` and ours), `THREAD_iscurrent`/`yield`,
  `SYNCTASK_add`/`run`, `REAL_addexit`.
- platform.files: `FILESYS_open`, `read`, `close`, `closesync`, `callbackop`, `completeop`, `priorityop` (STREAM only).
- engine.core: `dummyNullFunction`, `dummyGetNullValue` (stubs SND links against).
- CRT: `__ftol2` (six setters), `_memmove`, `_strncpy`; `___CxxFrameHandler` (funclets).

### 5.3 Floating point

3502 x87 instructions in the module, 2280 of them in `FUN_00146f00` and 195 in `decodemut` (data-dead). Live x87:
`MIXI_interpolatemix`/`interpolateto0`, the reverb blocks, `FUN_001466e0`/`FUN_00146930`, `MIX_initreverb`,
`SFILTER_modifyLPFRC`, `SFILTER_ft24_32` (one `FADD` per sample, stored as float), the platform setters' gain products.
SSE: `mixc` (packed `MULPS`/`ADDPS`), `SND::decodexac` (scalar), the resampler kernel `FUN_001462b0`. No FPU
control-word changes anywhere in the library; the x87 results depend on the SND thread's precision control, which
the thread inherits (8.8).

---------------------------------------------------------------------------------------------------------------

## 6. The interface upward, and what is dead

### 6.1 Entry points by caller

35 sound functions are called from `engine.audio` (all on the main thread):

| caller | sound entries |
|---|---|
| `ASystem::ASystem` / `SetOpts` / `Shutdown` | `SNDSYS_vectortoreal`, `SNDSYSI_init` (256 KB heap), `SNDSYS_getopts`/`setops` (render modes `0x420`, `0x24`; output mode 1 or 2), `SNDSYS_restore` |
| `ABank::ABank` / `~ABank` / `IsPatchPresent` | `SNDbankadd`, `SNDbankheadersize`, `SNDbankheadercopy`, `SNDbankremove`, `SNDbankpatpresent` |
| `AVoice::PlayVoices` / `Set` / `View::Stop` / `View::~View` | `SNDplaysetdef`, `SNDBANK_play`, `SNDvol`, `SND3dpos`, `SNDpitchmult`, `SNDfxlevel`, `SNDover`, `SNDstop`, `SNDSYS_entercritical`/`leavecritical` |
| `AStreamPriv::AStreamPriv` / `~AStreamPriv` | `SNDSTRM_overhead`, `SNDplaysetdef`, `SNDSTRM_create`, `SNDSTRM_destroy` |
| `AStream::Play` / `Next` / `FadeOut` | `SNDSTRM_queuefile`, `purge`, `modifyhold`, `status`, `requeststatus`, `vol`, `pitchmult`, `lowpass`, `3dpos`, `autovol`, the critical section |
| `AFX::Update` / `Pause` | `SNDfxinitbus`, `SNDfxmasterlevel` |

Indirect entries: `SNDREAL_systemtask` (SYNCTASK, main thread), `~ASystem` (`REAL_addexit`), `SNDDRV_thread`
(`CreateThread`), the SFILTER process/restore functions (stored in filter nodes), `unpackerInitFuncs` and its FX hook,
`MIX_VOICE_FREE_FUNC`, `mixc` through `0x00245be8`, `SNDSTRMI_service` (server client), `SNDSTRMI_framescallback`/
`releasecallback` (packet player callbacks), the STREAM FILESYS callbacks, the CODA allocator thunks.

Data the game shares with the library: the bank file memory (`ABank::data`, read in place by the hardware voices for
as long as the bank is loaded), the stream memory `AStreamPriv` allocates, `SNDPLAYOPTS` (0x18) it fills, and the
voice/stream handles. Game logic reads sound state back: `SNDover`, `SNDSTRM_status`/`requeststatus` (stream
progress) - so sound-thread timing reaches the simulation even under lockstep *(candidate: a source of lockstep
divergence when a stream's end gates a mission event; check before trusting a lockstep comparison across a speech
line)*.

### 6.2 What our code already touches

- `src/driving/sound/dsndSeam.cpp` / `dsoundEntries.inc` / `xaudio2Driving.cpp`: every DirectSound entry point (5.1);
  the seam's bookkeeping is what makes `iSNDserve`'s `GetStatus` reclaim work.
- `src/driving/engine/PlayMPC.cpp`: the movie player, which no longer calls `SNDSTRM_createtap`/`STREAM_*` (hence the
  dead functions) and makes its own XAudio2 voice.
- `src/driving/platform/FileSys.cpp`: the FILESYS worker the STREAM callbacks run on (its header comment says so).
- `src/driving/platform/XboxStartup.cpp`: leaves out DirectSound's static constructors.
- `tools/subsystems_driving.txt` already moved `0x0014a370` (SHAPE's loader) out of this range.

### 6.3 Dead code

Dead now (10, 0.4 KB): `SNDSYS_inited`, `SNDSTRM_overheadtap`, `SNDSTRM_queuerequestid`, `SNDSTRM_createtap`,
`STREAM_setfilter`, `STREAM_setpriority`, `STREAM_taphandle`, `STREAM_isendofstream`, `FUN_001503b0`, `FUN_001503c0` -
all the old movie player's. Data-dead (35, 17.6 KB): section 3.8. Never-registered hooks: the 100 Hz client list,
the user-data client list, `SNDDRVPreFrameCb`/`PostFrameCb`, `p_setfx_func` (written, never read).

---------------------------------------------------------------------------------------------------------------

## 7. Patching hazards

### 7.1 Register arguments

Checked in the listings and against `tools/abi_driving.json`:

| address | registers | what |
|---|---|---|
| `0x0014aba0` | ESI = STREAMINTERNAL, + one stack argument (bytes) | release bytes (STREAM_release, cancelrequest) |
| `0x0014ac00` | EDI = STREAMINTERNAL; returns EAX | pop a free request (queuefile, queuemem) |
| `0x0014ac70` | EDI = STREAMINTERNAL, ESI = request | append a request (queuefile, queuemem) |
| `0x0014acc0` `freerequest` | EAX = request, ECX = STREAMINTERNAL | (cancelrequest) |
| `0x0014ad20` | EAX | read bookkeeping (from `FUN_0014b660`) |
| `0x0014af50` | ESI = STREAMINTERNAL, + one stack argument (priority) | start the next request |
| `0x00143be0` | EAX | time stretch helper (data-dead) |
| `0x00143e10` | EBX, ESI | time stretch helper (data-dead) |
| `0x00143f20` | EBX | time stretch helper (data-dead) |
| `0x00144050` | EDX | time stretch helper (data-dead) |
| `0x00146e00` | EBX | MicroTalk (data-dead) |
| `0x00146f00` | EAX, ESI | MicroTalk (data-dead) |
| `SND::CEAXABLKDecf::*` `0x00149e70..0x0014a1c0` | thiscall (ECX), `RET 0/12/8/4/4` | the decoder object |

Not register arguments, though tools say so: `SND::decodexac` (one stack argument; saves and restores all seven
registers; Ghidra's `__fastcall` is wrong) and `FUN_001462b0` (`pushad`, seven stack arguments, cdecl). A port of
either keeps the cdecl stack contract; neither relies on scratch registers surviving. Several `SNDPLATFORM_*`
functions are typed `longlong` by Ghidra because EDX is left holding something: no caller reads it.

### 7.2 Thunks

`0x0013f770` (-> `0x001429d0`), `0x00141860` (-> `SNDMEMI_alloc`), `0x00141870` (-> `SNDMEMI_free`; both stored in
`CODA_New`/`CODA_Delete` by `MIX_create`), and `SFILTER_unpackfgetframe_unpacklfgetframe` (one body for two names).
Patch the target, or both.

### 7.3 Code Ghidra never made a function, and data in `.text`

Live code without a function (each reached only through a pointer stored at run time):

| address | bytes | installed by | what |
|---|---|---|---|
| `0x001437e0` | 396 | `MIX_initreverb` (`unpackerInitFuncs[0] = 0x001437e0` when the FX description's byte 2 is 10) | the "fx2" per-slice FX process *(candidate: PS2 SNDMIXI_modlapifxadd, between fxinit and fxadd in both builds)* |
| `0x00144d00` | 286 | `FUN_00144e20` (store at `0x00144e3c`) | an FX block's process function |
| `0x00144ca0` | 21 | `FUN_00144cc0`, `FUN_00144e20` | an FX block's restore: frees +0x1c (tail jump to `SNDMEMI_free`) |
| `0x001456b0` | 39 | `SFILTER_initSOURCE` (store at `0x001456e8`) | the SOURCE node's process *(PS2: SFILTER_src)* |
| `0x00145f40` | 8 | `SFILTER_unpackxafinit` (+0x24) | *(PS2: SFILTER_unpackgetframexaf)*; data-dead |

Data inside `.text`: the jump table of `SNDMIXI_initfx` at `0x00143370` (39 bytes) and the jump table plus byte
index table of `DirectSound_SetListenerRelated`'s switch at `0x00140962` (159 bytes). The coverage tool attributes
gap code to the function before it.

### 7.4 Function-pointer tables and callbacks

`unpackerInitFuncs`/`unpackerInitSizes` (`0x002459a4`/`0x00245a28`, slot 0 doubling as the FX hook), the SFILTER nodes'
process (+0), restore (+4) and kernel (+0x24, the resampler's `FUN_001462b0`) pointers, `mixc` through `0x00245be8`,
the FX init pointer `0x002459a0`, `MIX_VOICE_FREE_FUNC`, `CODA_New`/`CODA_Delete`, the packet players' release/frames
callbacks, `SNDserverclients`, the on-exit functions, `SNDDRVPre/PostFrameCb`, `sndopts2`, the FILESYS callbacks of
STREAM, the SYNCTASK and `REAL_addexit` registrations, `CreateThread(SNDDRV_thread)`. Patching an entry point does
not catch calls through these: replacing a filter means replacing the pointer its init stores (port the init with
it), and replacing `mixc` alone means patching its entry, since `MIX_create` stores the address.

### 7.5 Threads touching shared state

- The SND thread holds `SoundMutex` for a whole tick and calls deep into modules B, D, F, G, H, I; the main thread
  enters the same state through the API under the same mutex. A ported function called from both (e.g. `SNDstop`
  from `AVoice` and from the server, `iSNDcalcvol`, `SNDVOICEI_free`, `SNDMEMI_alloc`/`free`) must stay reentrant
  under that lock and must not take any other lock in a different order.
- STREAM state is touched by the main thread (`STREAM_get`), the SND thread (`STREAM_release` from the packet
  callbacks) and the FILESYS worker (the read callbacks), serialised only by the STREAM MUTEX - and only in parts:
  `STREAM_get` updates `pData` outside the mutex. Keep the original's exact locking when porting E.
- The rings and bank samples are read by XAudio2's thread with no lock (5.1).
- **Shadow tests that swap the original's bytes back (`common/xbeOriginal.h`) are process-wide and only safe on the
  only thread that calls the function.** Everything in G, H, I is called only on the SND thread (from inside
  `dsndMixProcess`), so a scope on that thread is safe for them; anything in A-D is reachable from two threads. `memclr`
  (ours) is called from all of them.

### 7.6 SEH

`SFILTER_unpackxapfinit`/`xalfinit`/`xafinit` set up an MSVC exception frame around `operator new` +
constructor (funclets `0x00156ba0..0x00156c00`). A port drops them (EA's `CODA_New` returns NULL, it does not throw).

---------------------------------------------------------------------------------------------------------------

## 8. Quirks a port must decide on

1. **Two render paths.** Banks play as hardware buffers (decoded by our backend's `xadpcm.cpp`), streams through EA's
   mixer into the rings. A port can keep the split (smallest behaviour change; the seam's `GetStatus` timing then still
   drives voice reclaim) or route everything through one mixer. Keeping it is the only way to compare step by step.
2. **The mixer runs 20 ms ahead of a write cursor it reads back** and writes memory nobody is told about. As long as
   the DirectSound seam stays underneath, keep that contract (and the 2400-frame rings); a final port could pull from
   XAudio2's callback instead, but the stream position the game reads (`SNDSTRM_status`) is paced by mixer consumption,
   so the pacing must stay 48 kHz real time.
3. **The SND thread's catch-up**: a negative wait sleeps 1 ms, so after a stall up to N ticks run nearly back to back.
   Keep (it is what keeps streams fed), or replace with "mix to the cursor" which is equivalent for the mixer but not
   for the 100 Hz envelopes and fades.
4. **The whole tick under `SoundMutex`.** Main-thread audio calls block for a full mix. Keep; shortening it changes
   which tick a main-thread change lands in.
5. **Voice reclaim by polling `GetStatus`** (10 ms latency, the cause of the old CXBX pool exhaustion,
   `docs/driving-engine-plan.md` section 4). And the borrow `FUN_0013d550` pops from a possibly empty list and
   dereferences the result: the original crashes when both pools are empty. A port should fail soft (drop the sound)
   and say so once.
6. **Pitch clamps**: hardware frequency 188..191983 Hz; a packet hardware voice caps the multiplier at 0x2000; a
   multiplier of 0 pauses (Stop, later Play with the saved loop flag).
7. **EA-XA arithmetic order**: `(s1 x k1 + table) + s2 x k2`, single precision, SSE scalar; the predictor history is
   reset from each packet's two leading shorts. Bit-exact in C with `float` and SSE2 code generation, provided the
   compiler does not contract into FMA (MSVC does not by default; keep `/fp:precise`, no `/fp:contract`).
8. **x87 precision control**: `MIXI_interpolatemix`, the reverb, the FIR design, `SFILTER_ft24_32` and the gain
   products run on the x87 and round to the thread's precision control before each `FSTP dword`. Windows starts a
   thread at 53-bit precision; what the Xbox's XAPI set is unchecked. Read the control word inside the original's
   SND thread once (a one-line probe) before choosing; port these functions either in a `/arch:IA32` compilation unit
   with the same expression order, or in assembly (class D/A of `docs/driving/maths.md` 3.3).
9. **`SFILTER_ft24_32`'s conversion** is the 1.5 x 2^23 magic-number trick on the x87 with an explicit clip; it rounds
   to nearest even under the default rounding mode. Keep it, not a `lrintf`.
10. **`unpackerInitFuncs[0]` is the FX hook**, not an unpacker; MicroTalk has no looping variant (slot 11 empty: a
    looping MicroTalk bank sample would play silence); sample representations 9 (8-bit), 14, 15, 16 (Layer 3) and
    0x40 have no unpacker at all. None of these occur on the disc; a port can assert.
11. **The data-dead decoders** (MicroTalk 13 KB, PCM16, bank EA-XA, time stretch): nothing on the disc reaches them.
    Port provisionally with a loud first-call warning (as EAGL's step 4 did), or leave them out and assert; the user
    decided "provisional, untested" for EAGL's equivalent.
12. **Reverb**: the hardware bus only picks one of 23 I3DL2 presets (our seam drops it); the software bus is a real
    comb network on the music and speech when `AFX` enables it. The port of G has to reproduce the software one exactly;
    the hardware preset is a backend question (`docs/audio-inventory.md`, "Reverb").
13. **Names to fix in Ghidra when the user reviews**: `SFILTER_ft24_32` is *PS2: SFILTER_ft16*; `0x001429d0` is not
    `SNDMEMI_restore`; `DirectSound_SetListenerRelated` is EA's, not DirectSound's; `SNDLINKNODE`'s `tail`/`head` are
    next/prev and the struct is the hardware buffer node; `MIXRELATED` is 0x30; `SNDSYS_setops`/`SNDI_randomseeed` are
    spelled as in the binary's symbols.
14. **SNDMEMI heap layout**: allocation addresses reach DirectSound only for the rings and packet-voice memory; the
    port of A can change the allocator once every allocation's owner is ours, not before (the original's frees go to
    the original's free list).

---------------------------------------------------------------------------------------------------------------

## 9. Port structure, order and tests

### 9.1 Shape

`src/driving/sound/snd/`, by module, each its own compilation unit with its harness in `src/driving/devtools/`
(`SndShadow.cpp` and friends: test code stays out of the reimplemented functions): `Decode.cpp` (EA-XA, the
data-dead decoders separately in `DecodeUnused.cpp`), `Filters.cpp`, `Mixer.cpp`, `Reverb.cpp`, `Tags.cpp`
(`gettag`/`getb`/`parsetimbre`/`patchtohdr`), `Voices.cpp`, `Banks.cpp`, `Streams.cpp`, `Stream.cpp` (file side),
`Platform.cpp`, `System.cpp`. Globals stay at their addresses (`static_assert`ed layouts, the way `GraphicsSystem.h`
owns graphics globals) until every reader is ours. Calls down go to the DirectSound entry points (the seam), not to
`xaudio2Driving.cpp`, so every step can be compared with the original running on the same seam; collapsing the seam
is the last step.

### 9.2 Pure DSP versus stateful control

| kind | functions | best test |
|---|---|---|
| **pure DSP, SND thread only** | `SND::decodexac`, `CEAXABLKDecf::*`, `mixc`, `MIXI_interpolatemix`, `MIXI_interpolateto0`, `MIXI_reverbblock`, `SFILTER_ft24_32`, `SFILTER_lpfRC`/`FUN_00145720`, `SFILTER_hpfFIR8`/`FUN_001466e0`, `SFILTER_rsf`/`FUN_001462b0`, the FX blocks and `0x001437e0`/`0x00144d00`, `SNDMIXI_fxadd`, (data-dead: `decodemut`, `readsamples`, `FUN_00146e00`/`f00`, `decode16x87`, time stretch) | injection-time shadow on disc audio and random buffers; then live dual-run on the SND thread |
| **pure functions of a record** | `SNDI_gettag`, `SNDI_getb`, `SNDI_parsetimbre`, `SNDI_patchtohdr`, `iSNDcalcvol`, `iSNDcalcpitch`, `iSNDdetunetolinear`, `SNDI_aztospkrvol`, `SNDI_precalcaztospkrvol`, `SNDI_pantoazimuth`, `SNDI_validrendermode`, `SNDPLATFORM_getvoicerange`, filter designs (`modifyLPFRC`, `FUN_00146930`), `findprime`, `SNDLINKI_*` | injection-time shadow, exhaustive or over every header on the disc |
| **stateful control** | SNDVOICEI, the SND API, banks, `SNDSYSI_100hzserver`, `iSNDserve`, SNDSTRM/SNDPKTPLAY, STREAM, `MIX_*` state management, `SNDPLATFORM_*`, `dsnd*`, SNDMEMI, init/restore | snapshot-and-replay of the whole sound state, and scoped DirectSound call traces (9.4) |

### 9.3 Order

| step | what | funcs / KB | test |
|---|---|---|---|
| 0 | housekeeping: create the five functions of 7.3 in Ghidra and add the candidates for the user's review; fix `MIXRELATED`/`SNDLINKNODE`; a one-line probe of the SND thread's FPU control word (8.8) | - | - |
| 1 | tag reading and header parsing: `SNDI_getb`, `gettag`, `parsetimbre`, `patchtohdr`, `SND_attrsetdef`, `SNDI_validrendermode`, `getvoicerange` | 7 / 3.1 | shadow at injection: every `PT` header on the disc (1129 bank patches, 842 stream headers, every language), parsed by both into separate buffers; the header bytes, the heap blobs `patchtohdr` copies and the returned cursor compared; plus perturbed copies (lengths 0xff, unknown tags, 0xfc padding, 0xfe) for the branches the disc does not take |
| 2 | EA-XA: `CEAXABLKDecf`, `decodexac` | 8 / 1.1 | shadow at injection: decode every `SCDl` chunk of every `.mus`/`.spe` (both channels, all languages) with both, in random `Decode` request sizes (1..2000 frames, so the partial-block path runs), compare floats bit for bit; then live dual-run inside `SFILTER_unpackxapf` on the SND thread |
| 3 | pure voice arithmetic: `iSNDcalcvol`, `iSNDcalcpitch`, `iSNDdetunetolinear`, `SNDI_aztospkrvol`, `precalcaztospkrvol`, `pantoazimuth`, `randrange`/`iSNDrandom`/`SNDI_randomseeed`, `iSNDmulu64`/`divu64`, `findprime`, `SNDLINKI_*` | ~16 / 1.8 | shadow at injection: random voice records (and every field combination that selects a branch), random azimuths 0..0xffff, compare the record bytes; `precalc` compares its tables |
| 4 | filters and the output stage: `SFILTER_lpfRC`/create/modify, `hpfFIR8`/create/modify with `FUN_001466e0`/`FUN_00146930`/`FUN_0014a2e0`, `rsf`/`rsfinit`/`rsfsetpitch` with `FUN_001462b0` (assembly or SSE intrinsics), `SFILTER_ft24_32`/`ft16init`, `SOURCE`, `add`/`addtofilterlist`/`remove`/`connect` | ~25 / 4.5 | shadow at injection, one node at a time with a fake upstream node that replays a recorded buffer (decoded disc speech, white noise, full-scale square waves for the clip), over pitches 0x4000..0x20000 and cutoffs 0..24000; compare outputs and node state bit for bit; then live dual-run per node on the SND thread (snapshot node + upstream output, run both, compare) |
| 5 | the mixer core and software reverb: `mixc`, `MIXI_*`, `SNDMIXI_volramp`, `MIX_*`, `SNDMIX_*`, `SNDMIXI_*`, the FX blocks and the two never-made process functions, `unpackxapf`/init/restore | ~60 / 12 | offline harness: build a mixer state (MIX_create, a set of packet voices fed from disc streams with fixed gains, pitches, fx sends, reverb on/off in each mode), run N slices with the original, restore the snapshot (globals `0x00245978..0x00247600`, MixList, the sound heap), run the port, compare the six rings and `sndfx` byte for byte. Live: dual-run `dsndMixProcess` once per tick on the SND thread with the same snapshot/restore (about 300 KB copied per tick: fine at 100 Hz), counting differing ticks |
| 6 | streams: `SNDSTRM*`, `SNDSTRMI*`, `SNDPKTPLAY*`; then STREAM (register arguments, three threads) | ~70 / 12 | offline harness with a synchronous fake FILESYS: play each `.mus` and a sample of `.spe` files through both versions, log the sequence of packets submitted (hashes), frames/release callbacks, `STREAM_get`/`release` results and status values; the sequences must match. In game: no dual-run (three threads); instead a timing-independent trace - the decoded PCM of every stream packet in order, hashed per stream - from the original and the port on the runner over a mission with music and speech, compared |
| 7 | banks and the voice manager: `SNDbank*`, `SNDBANKI_*`, `SNDVOICEI_*`, the SND API, `iSNDserve`, `SNDSYSI_100hzserver` | ~45 / 8 | offline: a scripted sequence of API calls (play every patch of a bank, steal, stop, fade, pitch, pan, envelopes run by calling the server N times) against both, comparing voice tables and the scoped DirectSound trace (9.4). In game: scoped traces per main-thread entry (`SNDBANK_play` etc.) compared live by running both on snapshots, main thread, under lockstep with the SND thread paused for the duration (take `SoundMutex` around the pair, which the API does anyway) |
| 8 | platform driver: `SNDPLATFORM_*`, `dsnd*`, `SNDDRV_*`, the pools and the borrow (fail soft), `DirectSound_SetListenerRelated` | 28 / 7.5 | scoped DirectSound trace per call (9.4) against the original on snapshots; then the runner: frame-timing logs, `[xa2]` chunk counts, a recording of the output |
| 9 | system: `SNDSYSI_init`, `SNDSYS_*`, SNDMEMI, mutex, the thread, restore | ~30 / 2.5 | boot and shutdown traces; whole-mission runs on the runner with the sound-state checksums of step 5 logged per tick |
| 10 | data-dead: MicroTalk, PCM16, bank EA-XA, time stretch, the hardware packet voice | ~35 / 17.6 | none from the disc; provisional ports with a first-call warning, or left out with an assert (the user's call, as for EAGL step 4). A synthetic shadow is possible for PCM16 and bank EA-XA (re-wrap disc data in a fake `PT` header) and for the time stretch (inject a 0x98 tag), not for MicroTalk (no encoder) |
| 11 | collapse the DirectSound seam: the platform driver talks to the backend directly | - | listening tests plus the step 6 PCM hashes |

About 260 live functions and 46 KB remain once the data-dead are set aside; steps 1-5 (about 115 functions, 23 KB)
are testable without the game running and are where to start.

### 9.4 Test tools this needs

- **Injection-time shadows** like `LoaderShadow.cpp`/`AnimShadow.cpp`: run at startup over disc data, original and
  port on separate copies, byte comparison, summary line. Steps 1-5.
- **Live dual-run on the SND thread**: the existing `SkelShadow.cpp` pattern (snapshot, run port, restore, run
  original under `XbeOriginalScope`, compare) works for G/H/I because only the SND thread calls them; keep the summary
  printing off that thread (queue counts, print from the main thread), and never hold the swap across a call that can
  reach a function the main thread also calls. For anything reachable from two threads, use a detour trampoline that
  runs the original's relocated prologue instead of swapping bytes, or run the pair inside `SoundMutex` on the main
  thread with the SND thread blocked.
- **Snapshot/restore of the sound state**: the globals `0x00244ba8..0x00244fc0` and `0x00244fe0..0x00247600` plus the
  256 KB sound heap (its address in `pSndHeap`) and the six rings. Bank and stream memory are read-only for the library
  apart from the stream ring.
- **Scoped DirectSound trace**: a thread-local flag set around an entry call makes the seam log every DirectSound call
  that call makes (entry, buffer identity as a pool/index, arguments); compare the original's and the port's lists.
  This is the sound equivalent of EAGL's planned D3D8 call-trace comparison and covers B, C, F without listening.
- **Lockstep caveat**: `NIGHTFIRE_LOCKSTEP=1` makes the simulation deterministic per frame, but the SND and FILESYS
  threads are free-running, and stream status feeds game logic (6.1). Use lockstep runs to make the main-thread call
  sequence reproducible, not to expect identical audio output timing; compare audio by content (PCM hashes per stream,
  per-voice trace), not by wall-clock position. Frame dumps say nothing about sound; the `[xa2]` line and an output
  recording on the runner are the in-game checks.

### 9.5 Risks

- **Bit-exact DSP** depends on SSE-vs-x87 choices per function and on the SND thread's precision control (8.7, 8.8).
- **Threads**: three threads and two lock kinds; a port that changes lock scope changes which tick changes land in.
- **Stream pacing reaches game logic** (6.1): a port that changes how fast packets are consumed changes when speech
  "finishes", which can move mission events.
- **Hidden callers**: the filter-node pointers, the unpacker table, `mixc`'s pointer, the packet callbacks, the
  STREAM FILESYS callbacks and the five never-made functions (7.3, 7.4).
- **Data-driven reach**: render mode and sample representation pick paths; the disc uses two of each. Report unknown
  ones rather than ignore them.
