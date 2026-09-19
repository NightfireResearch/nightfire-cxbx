# Removing CXBX from the action engine: project plan

Status as of September 2026, written for whoever (human or agent) picks up the next stages. Read this
alongside `src/action/engine/Direct3D/d3dSeam.cpp` and `d3d9Backend.cpp`, which are the worked example of
the pattern every stage below repeats.

## 1. Where things stand

**The action engine now runs without cxbx-reloaded.** `action.exe` maps `default.xbe` itself, resolves its
kernel imports, loads `actioninject.dll` and runs the game in its own process - no emulator anywhere in it.
Tested as far as: boots, opens its window, reaches the main menu, plays video and audio, takes controller
input, and loads into a mission.

The CXBX path still works and is still the reference. `action_cxbx.exe` launches the game under
`cxbxr-ldr.exe` exactly as before, and everything added for the standalone loader is conditional on CXBX not
being in the process (`Xbox_RunningStandalone()`, which tests for `cxbxr-emu.dll`), so a regression can always
be bisected against a hosted run. Nothing in this section's original arrangement has been removed.

Either way, the game is the original x86 code with roughly 10% of its functions reimplemented;
`tools/preprocess.py` turns the `AUTOINJECT`/`FUNC_AT` tags into the patch table. What has changed is who
provides the things underneath it.

What replaces CXBX, each one a "seam" that replaces a library boundary with our own code:

| Subsystem | Seam | Notes |
|---|---|---|
| Graphics | `Direct3D/d3dSeam.cpp` + `d3d9Backend.cpp` | Every D3D8/XGRAPHC entry point goes through `D3DSeamTraced` dispatch; with `GraphicsBackend=d3d9` nothing in the D3D8 library runs. NV2A vertex programs are translated to HLSL at runtime. |
| Audio | `sound/dsndSeam.cpp` + `xaudio2Backend.cpp` | 2D and 3D voices, Xbox ADPCM, the mixbins collapsed onto stereo, X3DAudio with the game's own rolloff curve, I3DL2 reverb behind a submix. |
| FMV audio | `sound/dsndStream.cpp` | The DirectSound stream path, which the video decoder calls directly rather than through any game function (4.4a). |
| Input | `engine/psiInput.cpp` | Direct XInput; CXBX's controller emulation unused. |
| Settings (EEPROM) | `engine/XboxSettings.cpp` | `settings.ini` replaces `ExQueryNonVolatileSetting`. |
| Saves | `engine/psiSave.cpp` | Plain files under `saves/`. |
| Files | `engine/psiFile.cpp`, `engine/FS.cpp`, `engine/XboxFile.cpp`, `engine/XboxPaths.cpp` | Drive letters map to host paths; the XAPI file calls are a Win32 layer of our own. |
| Startup | `engine/XboxStartup.cpp` | Process heap, the XAPI initialiser table, the CRT's per-thread data, and the last-error pair (4.2). |
| Loader and memory map | `src/loader/` | Maps the XBE at `0x10000`, resolves the kernel thunks, creates the window, runs the message pump (4.3). |
| Relaunch | `common/launchInfo.cpp` | `XLaunchNewImageA`/`XGetLaunchInfo` replaced (the driving engine is a second XBE, `inject_driving.cpp`). |

19 of the XBE's 96 kernel import ordinals are implemented in `src/loader/kernel.cpp`; the rest resolve to a
stub that names itself and its caller and stops, which is how those 19 were found.

### Is stage B finished?

The engine runs without CXBX, which was the goal, and every numbered step of the plan below has been done.
But **no**, not in the sense of "there is nothing left to find", and it is worth being precise about why,
because the design deliberately trades completeness for a queue.

*What has actually been exercised*: boot, the menus, the attract movies, audio, controller and keyboard
input, loading into a mission. That is one path. A full mission played through, saves, multiplayer, the
later levels, pause and resume - none of that has been run standalone even once.

*What is known to be missing*, in rough order of how likely it is to matter:

| Gap | Where | Consequence |
| --- | --- | --- |
| 77 of 96 kernel imports | `src/loader/kernel.cpp` | Any code path not yet walked may need one. It stops with the name and the caller, so each is minutes of work - but the list is not closed until the game has been played through. |
| 12 of the 16 DirectSound stream vtable slots | `sound/dsndStream.cpp` | Same shape: the decoder only uses four. An unused slot reports itself and stops rather than corrupting the stack. |
| `DirectSoundUseFullHRTF`, `IDirectSound_DownloadEffectsImage` | `sound/dsndSeam.cpp` | Counted and ignored. The second is the I3DL2 reverb image, whose effect nobody has yet confirmed is audible at all. |
| The physical-memory alias outside graphics (4.4) | `sound/dsndSeam.cpp`, unaudited | Sound-bank data reaches `SetBufferData` as an alias pointer, and `0xF0000000` (write-combined) has not been swept for. |
| Engine switching is a manual restart | `common/launchInfo.cpp` | Pre-existing, not a regression: `XLaunchNewImageA` has always written `psiLaunch.bin` and then stopped, on both hosts. Standalone it could be automated by the loader re-executing itself with the other XBE - but not by mapping both, since they share a base address. |
| The driving engine (4.5) | - | Untouched. See `docs/driving-engine-plan.md`. |

*Smaller things deliberately left*, each documented where it lives: `Xbox_freeptd` drops the CRT's
per-thread block instead of unpicking it, leaking about 132 bytes per thread that exits (it is currently
unreachable); pool allocations do not reproduce the Xbox's page alignment for blocks of a page or more;
the ADPCM decode hitch on first play of a large sound; and the 3D-versus-2D gain balance, which was an open
question in `docs/audio-inventory.md` before any of this and still is.

The honest summary is that the hard, unbounded parts - the address range, the headers, the FS segment, the
startup, the audio hardware - are done and understood, and what remains is a queue of small, self-announcing
items that a playthrough will produce.

## 2. The method (unchanged from the graphics work)

1. **Find the boundary.** Sweep callers of every entry point of the library (Ghidra
   `get_bulk_xrefs` over the addresses from `functions_action.json`). The set of *game* functions that
   call the library is the seam. For D3D8 that was 41 entry points behind ~60 game functions; for DSOUND it
   is 36 public entry points behind about 25 game functions (section 3.1).
2. **Reimplement the game side of the seam** as `AUTOINJECT` functions that still call the original
   library through typed function-pointer macros. Test in CXBX mode until behaviour is identical. This
   is the step that makes the boundary explicit and testable.
3. **Add a backend switch** (`settings.ini`) and a native backend behind the same macros, with a
   `BackendMissing` counter for anything not yet implemented, so the bring-up is driven by what the game
   actually calls. Log to a file; add periodic diagnostics you can look at yourself (the D3D9 backend's
   BMP dumps were what solved the shadow bug).
4. **Never refactor for its own sake**: keep the original call structure and data layout; the value is
   in the seam being complete and robust, not tidy.

## 3. Stage A: audio

### 3.1 The boundary

The game's audio layer is small and self-contained: the `dsnd*`/`xbox*Sound`/`SFX*` functions at
`0x000e0f00`-`0x000e1e40` (all named in `functions_action.json`), plus the video path. Public DSOUND
entry points and their game-side callers (from `get_bulk_xrefs`):

| DSOUND entry point | Game callers |
|---|---|
| `DirectSoundCreate`, `DirectSoundUseFullHRTF`, `IDirectSound_DownloadEffectsImage` | `xboxInitSound` (0xe1ae0) |
| `IDirectSound_CreateSoundBuffer` | `xboxCreateSoundBuffers` (0xe10c0), `dsndCreateSoundBufferWithSomeDefaultSettings` (0xe0f00) |
| `IDirectSoundBuffer_SetBufferData/SetFrequency/SetLoopRegion/SetCurrentPosition` | `dsndGetVoice` (0xe1bb0), `maybeSoundShutdown` (0xe1d80) |
| `IDirectSoundBuffer_Play/Stop/GetStatus/GetCurrentPosition` | 0xe1a77, 0xe1a5b, 0xe1aa4, 0xe188f (the voice update loop) |
| `IDirectSoundBuffer_SetVolume/SetHeadroom/SetMixBins/SetMixBinVolumes` | 0xe1484, 0xe1332-0xe1350, 0xe12ad-0xe1325, 0xe1844 |
| `IDirectSoundBuffer_SetPosition/SetVelocity/SetMinDistance/SetMaxDistance/SetRolloffCurve/SetI3DL2Source` | 0xe1568, 0xe159f, 0xe150c, 0xe14f9, 0xe1220, 0xe1627 (`dsndSetDistances`, `dsndSetI3DL2Source`, position updates) |
| `IDirectSound_SetPosition/SetVelocity/SetOrientation/CommitDeferredSettings` | 0xe0fde-0xe10b7, 0xe1dae-0xe1dc2 (listener), `SFXUpdate` (0xe19c0) |
| `DirectSoundDoWork` | `SFXUpdate`, `maybeDecodeMpgAudio` (0xe8cf0), `maybeXmvDecoderUpdate` (0x130624) |
| `DirectSoundCreateStream`, `IDirectSoundStream_Pause/SetVolume/SetMixBins`, `IDirectSound_SynchPlayback`, `IDirectSound_Release` | `maybeSFXCreateStreamForVideo` (0x130488), 0xe197f/0xe19ae (`dsndStreamSetVolume`), and the XMV decoder library itself (0x1305b3, 0x13097e-0x130998) |

Everything at `0x0011xxxx` is inside the DSOUND library and can be ignored once the boundary is ours.

### 3.2 What the game actually does with it

From `xboxInitSound`/`xboxCreateSoundBuffers`/`dsndGetVoice` (Ghidra decompiles are clean):

- One `DirectSound` object, full HRTF, and an effects image (`effectsImage`, the I3DL2 reverb DSP
  program) downloaded at startup.
- **192 static buffers**, created once: 64 mono 2D, 64 stereo 2D (`dsndCreateSoundBufferWithSomeDefaultSettings(2)`),
  64 mono 3D (`DSBCAPS_CTRL3D`, rolloff curve `fRolloffCurve` with 5 points). All
  **Xbox ADPCM** (`WAVE_FORMAT_XBOX_ADPCM`, 44032 Hz, 64 samples per 36-byte block, 4 bits per sample).
- Mixbins: 3D voices go to the four cross-talk bins + front centre + I3DL2; 2D voices to the six
  5.1 bins with centre muted. Headroom 0 for 3D, 600 for 2D. `AudioSystem.VolumeLookupTable` maps
  0..100 to hundredths of dB (-10000..0).
- **A voice is a slot 0..63** (`AudioSystem.maybeVoices`, 0x40 bytes each): `dsndGetVoice(data, size,
  frequency, channels, loop, is3d)` picks a free slot, binds one of the three pre-made buffers, calls
  `SetBufferData` (the buffer *references* the game's memory, no copy), `SetFrequency`, `SetLoopRegion`,
  `SetCurrentPosition(0)`. Per-frame `SFXUpdate` commits deferred 3D settings and calls `DoWork`.
- **Streams** are used for music and video audio (`maybeSFXCreateStreamForVideo`, `ES_*` in the
  `SFX*` family): `DirectSoundCreateStream` with a packet callback, `Pause`, `SetVolume`, `SetMixBins`,
  `SynchPlayback`. `maybeSFXStreamCallback` uses `QueryPerformanceCounter` for A/V sync.
- The XMV decoder (Microsoft code linked into the XBE, `maybeXmvDecoder*` at 0x1304xx-0x1309xx) creates
  its own stream and calls `DoWork`/`SynchPlayback`/`Release` directly. It cannot be reimplemented
  function-by-function (it is a large opaque library), so its DSOUND calls must be satisfied at the
  entry-point level, exactly like the D3D8 entry points were.

### 3.3 Plan

1. **Seam the game side** (CXBX mode, no behaviour change). **DONE** for the dsnd layer proper - all 28
   functions in `0xe0f00`-`0xe1e40` are reimplemented in `src/action/sound/dsndSeam.cpp`, calling the
   DSOUND entry points through a `DSoundSeamTraced` dispatch modelled on `D3DSeamTraced` (one wrapper
   type, since every public DSOUND entry point is plain `__stdcall`). `AudioSystem` is mirrored as
   `XboxAudioSystem` at `0x002ae598` with `static_assert`ed offsets, and its voice flag bits are named
   (`VOICE_REQUEST_PLAY`/`_STOP`/`KEEP_ALIVE`/`PLAYING`/`STARTED`/`PAUSED`/`IN_USE`). Things worth
   knowing before touching this again:
   - `dsndCreateSoundBufferWithSomeDefaultSettings` (`0xe0f00`) takes its channel count in **EDX** and
     its 3D flag on the stack, and still leaves stack cleanup to the caller (plain `RET`). Ghidra calls
     it `__fastcall`, which as compiled would be a callee-cleans `RET 4` - so it needs the `AUTOLTCG`
     naked entry trampoline it has, not a plain `AUTOINJECT`. Every other function in the range ends in
     a bare `RET` (verified by an instruction search over the whole range), i.e. `__cdecl`, whatever
     Ghidra's `calling_convention` field claims.
   - Ghidra has **two** unrelated functions called `SFXUpdate`: the high-level SFX-system update at
     `0xcad40` and the per-frame DirectSound voice update at `0xe19c0`. An `AUTOINJECT` by name patches
     both, so the seam's version is called `dsndUpdateVoices` and injected with `FUNC_AT(000e19c0)`.
     Worth disambiguating in Ghidra at some point (as `d3dSetTexture` was).
   - The 3D setters really take `float`s; Ghidra infers `int` for `IDirectSoundBuffer_SetPosition` and
     friends. Confirmed from the call site (`0xe19de`-`0xe1a16` `FSTP`s computed floats into the
     argument slots).
   - `FUN_000e1400` is a pause/stop request (`dsndSamplePause`) and `FUN_000e18a0` writes new sample
     data into the memory a voice's buffer is already referencing (`dsndWriteVoiceData`); both are
     injected with `FUNC_AT` rather than renamed in `functions_action.json`.
   Still to seam on the game side: `maybeDecodeMpgAudio` (`0xe8cf0`) and `maybeSFXCreateStreamForVideo`
   (`0x130488`). Both sit inside the FMV path rather than the dsnd layer - `maybeDecodeMpgAudio`'s
   decompile has the decompiler confusing a local with the return address - and between them they only
   add `DirectSoundDoWork` and `DirectSoundCreateStream` to the seam's surface, so they were left for
   the stream work in step 3 rather than risking the video path now.
2. **Inventory with tracing** through a whole mission plus menus and an FMV. **DONE** - written up in
   `docs/audio-inventory.md`, from a 16,500-frame session with `DSNDSEAM_TRACE` set to 1. Headlines: the
   29 entry points the seam routes are exactly the 29 the game uses, so the boundary is closed; frequencies
   span 22,050-44,100 Hz (a ratio of 0.5-1.0 against the buffers own 44,032 Hz, so nothing exotic is needed
   of XAudio2); velocity is always zero, i.e. no Doppler anywhere; the 192 buffers are created at boot and
   never released; the game re-pushes every voice parameter every frame whether or not it changed, so the
   backend must diff; and `GetStatus` at 7.7 calls per frame is the hottest call in the seam and drives
   voice recycling. The one real gap: the XMV decoder calls the stream entry points directly, so none of
   them appear. FMV audio is *not* silent in native mode as a result - CXBX's patches on those entry points
   are installed whatever the seam does, so it keeps servicing the decoder's streams - but the game's own
   stream setters have to be passed through to DSOUND rather than handled by a backend, and streams only
   become the backend's problem once those entry points are hooked. See that document for the detail.
3. **Native backend** (`AudioBackend=xaudio2` in `settings.ini`, default `cxbx` - the setting, the
   `g_audioBackend` switch and the `DSound_BackendMissing` accounting already exist, there is just no
   backend behind them yet, so selecting `xaudio2` today means silence). Recommended
   host API: **XAudio2** (ships with Windows 10+, `xaudio2.h`, no redistributable), with X3DAudio for
   the 3D voices and the built-in reverb XAPO standing in for I3DL2. Mapping:
   - Xbox ADPCM to 16-bit PCM on `SetBufferData`. **DONE**: `src/action/sound/xadpcm.cpp`, written from the
     IMA algorithm rather than adapted from CXBX's `XADPCM.h` (which is GPLv2 - do not copy it into this
     project; it is fine to read). `tools/xadpcm_test.ps1` is the offline check: hand-computed vectors for
     nibble order, header endianness, sign and saturation, an encode/decode round-trip over a sine sweep
     (22.5 dB SNR), and the byte-offset/sample-index arithmetic the backend needs for loop regions and play
     cursors. It also has a mode that decodes a real blob to a .wav.
     One thing no offline test can settle: a 36-byte block decodes to **64** samples here (the header
     predictor seeds the state and is not emitted), because that is what the game's own wave format asserts -
     `wSamplesPerBlock` is 64 and `nAvgBytesPerSec = nSamplesPerSec * 36 / 64` only balances at 64. Luigi
     Auriemma's decoder and CXBX's use of it emit 65 (the MS IMA ADPCM reading, where the header predictor is
     the block's first sample), which stretches every buffer by 1.6%. If pitch or FMV A/V sync looks off by
     about that much, `XADPCM_SAMPLES_PER_BLOCK` is the single constant to question.
     Still to do: cache the decoded PCM per (data pointer, size), since the same sound bank data is re-bound
     many times, and watch for the game overwriting it in place through `dsndWriteVoiceData`.
   - One `IXAudio2SourceVoice` per Xbox buffer slot (192), created lazily with the slot's format
     (mono/stereo, decoded PCM at 44032 Hz; frequency changes via `SetFrequencyRatio`).
   - Loop regions to XAudio2 `LoopBegin/LoopLength`; `GetCurrentPosition`/`GetStatus` from
     `GetState`.
   - Mixbins: collapse to a stereo (or 5.1 if present) mastering voice with per-voice output matrix;
     `SetMixBinVolumes`/`SetHeadroom` become matrix scaling. I3DL2 source parameters map to the reverb
     XAPO send level; start with dry only and add reverb once everything else works.
   - 3D: keep the listener/emitter state in our own structs, run `X3DAudioCalculate` in `DoWork`
     (which the game already calls every frame) and apply the results; the rolloff curve maps to a
     custom `X3DAUDIO_DISTANCE_CURVE`.
   - Streams: `DirectSoundCreateStream` gives a packet-based API (`Process` with `XMEDIAPACKET`s,
     completion status words). Map to a source voice with `SubmitSourceBuffer` and the
     `OnBufferEnd` callback writing the packet's status word. The XMV decoder's packets are PCM.
   - `DoWork` is the natural place for all deferred work; `CommitDeferredSettings` can be a no-op if
     3D settings are applied immediately.
4. **Verification**: an FMV with audio (A/V sync depends on the stream position reporting), menu
   sounds, a level with music, distance attenuation on a 3D sound, pausing.
5. **Cleanup**: once the backend is complete, the `DSOUND::` code at `0x0011xxxx` is dead in native
   mode, like the D3D8 library is now.

Risks: `SetBufferData` referencing game memory that the game later frees or reuses (the D3D texture
cache had exactly this problem; hash or copy on bind); stream timing for the FMV decoder (it measures
elapsed time itself and expects `GetStatus` positions to advance in real time).

## 4. Stage B: the remaining CXBX dependencies

Once graphics and audio are native, everything left is the process environment. Suggested order:

### 4.1 Inventory the kernel and XAPI surface actually reached by game code

**Measured** by `tools/kernel_imports.py`, which reads the kernel thunk table out of the XBE header, names
the ordinals from Cxbx-Reloaded's `EXPORTNUM` annotations, finds call sites by scanning for the two encodings
that can reach an import thunk (`FF 15` and `FF 25`), and attributes each to its containing function via
`functions_action.json`. Rerun it after any Ghidra sync; it needs no Ghidra connection of its own.

**96 imports.** Only **15** are called from a function anyone has named, and they fall into four groups:

| group | imports |
|---|---|
| File I/O | `NtCreateFile`, `NtOpenFile`, `NtReadFile`, `NtWriteFile`, `NtClose`, `NtQueryInformationFile`, `NtSetInformationFile`, `NtFlushBuffersFile`, `NtWaitForSingleObject`, `RtlInitAnsiString` |
| Memory | `MmAllocateContiguousMemoryEx` (`allocateContiguous`), `NtFreeVirtualMemory` (`DoNtFreeVirtualMemory`) |
| Timing | `KeDelayExecutionThread` (`maybeSleepMillis`) |
| CRT locking | `RtlEnterCriticalSection`, `RtlLeaveCriticalSection` |

Two things that changes about the plan below. **The file path dominates**: ten of the fifteen are file I/O, so
4.1 is mostly one job rather than a broad sweep. And `Rtl*CriticalSection` is not the streamer as assumed -
its named callers are the statically linked CRT's own stdio locking (`__lock_file`, `__unlock_file`,
`__getstream` at `0x000f0xxx`-`0x000f3xxx`), which puts the CRT at roughly `0xf0000`-`0xf5000` and makes it
4.2's problem, not 4.1's.

One correction to the sketch below: `FUN_0010a6b0` is **not** "async reads for the streamer/decoder". It is a
screen-capture path inside the D3D8 library - it works off `D3D_g_pDevice`, calls `GetBackBuffer2` and
`D3D_KickOffAndWaitForIdle`, and its strings are "Unable to re-open movie cache file" and "Wait for image
write timed out". It writes captured frames to a cache file, and with `GraphicsBackend=d3d9` the D3D8 device
is never created, so it does not run at all.

**The file path is now done.** `src/action/engine/XboxFile.cpp` replaces eleven functions, all of which turned
out to be Win32 calls under other names:

| game function | Win32 |
|---|---|
| `createFile` | `CreateFileA` |
| `readFromFileBlocking` | `ReadFile` |
| `FileWrite` | `WriteFile` |
| `GetOverlappedResult` | `GetOverlappedResult` |
| `getFileSize_LargeInteger` | `GetFileSizeEx` |
| `querySetSomeInfo` | `SetFilePointer` |
| `FUN_000e9731` | `SetFilePointerEx` |
| `setSomeInfo` | `SetEndOfFile` |
| `file_flush` | `FlushFileBuffers` |
| `MaybeFileCreateNew` | `DeleteFile` |
| `DoNtClose` | `CloseHandle` |

`tools/kernel_imports.py` now discounts call sites inside functions the project injects over, so it can be
used as a progress meter. After this work it reports **5** imports still reached from live named code, down
from 15, and none of them are file I/O:

| import | live named caller | belongs to |
|---|---|---|
| `RtlEnterCriticalSection`, `RtlLeaveCriticalSection` | the CRT's stdio locking (`__lock_file`, `__getstream`) | 4.2 |
| `KeDelayExecutionThread` | `maybeSleepMillis` | 4.2 |
| `MmAllocateContiguousMemoryEx` | `allocateContiguous` | 4.4 |
| `NtFreeVirtualMemory` | `DoNtFreeVirtualMemory` | 4.4 |

Two caveats on the measurement. There is **no address at which game code stops and the libraries begin** -
the linker interleaved them, and XAPILIB functions alone run from `0x000e9a24` to `0x001588db` - so the tool
lists callers rather than classifying them, and "called from a named function" is a heuristic for ordering
the reading, not a verdict. And the scan only finds *direct* indirect calls through the thunk table: it
misses data imports (`LaunchDataPage`, `XboxHardwareInfo`, `XboxKrnlVersion`, `ExEventObjectType`) and
anything called through a register loaded earlier, which is why `ExQueryNonVolatileSetting` - known to be
used, since `XboxSettings.cpp` replaced it - shows no call site.

The remaining 63 imports are reached only from unnamed or library-namespaced code: the `Av*` and
`KeConnectInterrupt`/`KeInitializeInterrupt` display and interrupt plumbing, `Mm*` physical memory,
`Ke*Dpc`/`Ke*Timer` deferred work, and the `Nt*` volume and directory calls. Those go with the libraries,
but the list is worth re-reading once more functions are named, since "unnamed" is the only thing separating
them from the fifteen above.

The original sketch of this section follows, now largely confirmed:

| Kernel / XAPI function | Game callers | What to do |
|---|---|---|
| `NtCreateFile`, `NtReadFile`, `NtClose`, `NtQueryInformationFile` | `createFile` (0xe9899), `readFromFileBlocking` (0xe93f0), `FUN_0010a6b0` (async reads for the streamer/decoder), `FUN_000e9f4d` | Extend `psiFile.cpp`/`FS.cpp` so every game file read goes through Win32 (the async path needs an overlapped or thread-backed equivalent). |
| `QueryPerformanceCounter` (XAPI, `rdtsc`-based on Xbox) | `maybeSFXStreamCallback`, `maybeXmvDecoderUpdate`, `FUN_0010a6b0` | Route to Win32 `QueryPerformanceCounter` with the Xbox tick rate the callers assume (733 MHz nominal; check the divisor constants at `DAT_001571e8`). |
| `KeQuerySystemTime` | several `FUN_000e*` / `FUN_0011*` | `GetSystemTimeAsFileTime` (same 100 ns FILETIME units). |
| `CreateThread` (XAPI, over `PsCreateSystemThreadEx`) | `entry` only | One worker thread created at startup; find its thread function and give it a Win32 thread. |
| `MmAllocateContiguousMemory(Ex)`/`MmFreeContiguousMemory` | `allocateContiguous` (0xeae06) | Aligned host allocation. Only D3D8 needed physical addresses; with the D3D9 backend the one remaining alias use is `D3D_UncachedAliasOf` in `d3dLockSurface` (movie frames), which should read the texture's real Data pointer instead. |
| `ExQueryNonVolatileSetting` | (already replaced) | Done. |
| `XLaunchNewImageA`, `XGetLaunchInfo`, `HalReturnToFirmware`, `XapiBootToDash` | `SetLaunchInfoAndLaunch`, `WriteStateFileAndLaunch`, `GetPTPData` | Done for launch info; relaunching the driving engine standalone is its own project (it is a separate XBE with its own libraries). |
| `RtlEnterCriticalSection` etc., `KeDelayExecutionThread`, `NtSetEvent`, `KeWaitForSingleObject` | streamer / decoder | Win32 critical sections, `Sleep`, events. |
| `XcSHA*`, `XboxHDKey`, `XboxSignatureKey` | save-game signing | Already irrelevant (saves are plain files); stub. |

### 4.2 Replace the XAPI/CRT startup - DONE

Implemented in `src/action/engine/XboxStartup.cpp`. `mainXapiStartup` is replaced wholesale: process
initialisation cut down to the process heap and the XAPI initialiser table, then `_rtinit`, `_cinit`, `main`.

What had to change, and the one thing that forced it: **game code reaches the Xbox KPCR through FS**, and a
Win32 thread has a TEB there instead. Only 38 instructions in the whole image touch FS, and they fall into
five groups:

| Offset | Xbox meaning | Win32 meaning | What was done |
| --- | --- | --- | --- |
| `FS:[0x00]` | SEH exception list | the same | nothing - it already works, 7 sites |
| `FS:[0x04]` | the TLS array | `StackBase` | replaced the 5 functions that use it (below) |
| `FS:[0x20]` | `KPCR.Prcb` | process id | the allocation-notification hook, patched to `xor eax,eax` at 5 sites |
| `FS:[0x24]` | current IRQL | thread id | only ever compared against 2; the functions reading it are replaced or dormant |
| `FS:[0x28]` | current `KTHREAD` | `ActiveRpcHandle` | same functions as `FS:[0x04]` |

`FS:[0x04]` cannot simply be repointed: Win32 keeps the stack base there and exception dispatch validates
every SEH frame against it, so writing a TLS array pointer over it would break the SEH the game uses
everywhere. The five functions that read it are replaced instead - `GetLastError` and `SetLastError` become
the Win32 ones, and the CRT's `_getptd`/`_freeptd`/`_mtinit` keep their original behaviour but hold the
`_ptiddata` block in a Win32 TLS slot. The block is still allocated with the *game's* `calloc`, because the
game's `free` is what releases it.

Two things are deliberately not called: `FUN_000eddfc` patches the running kernel image, and there is no
kernel image to patch; and the drive mounting in `XapiInitProcess` is redundant now that `XboxPaths.cpp` maps
drive letters to host paths directly.

Also patched out: `WBINVD` at three sites. An Xbox title runs in ring 0 and flushes the cache itself before
the GPU reads memory; the instruction is privileged on Windows and raises `0xc0000096`. There is nothing to
flush, because the D3D9 backend copies rather than letting hardware read game memory.

### 4.3 Own loader - DONE

`src/loader/`, built as `action.exe` - the standalone loader is now the canonical way to run the game, and
the CXBX launcher is the one carrying a suffix. It maps the XBE, resolves the kernel thunks, loads
`actioninject.dll` unchanged, creates the render window and calls the entry point.

**Getting the address range was the hard part, and the answer is not VirtualAlloc.** `0x00010000` is above
the system minimum but is never free: the kernel has already put something there before the first instruction
of the process runs, and reserving it from a parent into a `CREATE_SUSPENDED` child fails identically, because
this is not a race that starting earlier wins. The answer is to *be* the image - the loader is linked
`/BASE:0x10000 /FIXED /DYNAMICBASE:NO` with a 0x340000-byte array first in `.text` (`src/loader/reserve.cpp`),
so the kernel maps it across the XBE's whole range before the process exists, and the XBE is copied over the
top. This is what `cxbxr-ldr.exe` does too, which is worth knowing before trying anything cleverer.

Two consequences of being the image:

- **`/SAFESEH:NO` is required.** The mapped XBE ends up inside the loader's image range, so every SEH handler
  the game registers looks to Windows like one of the loader's own and would be rejected for not being in its
  table of safe handlers.
- **Both sets of headers have to live at `0x10000` at once.** The game reads its own XBE header constantly
  (heap reserve/commit at `0x10134`/`0x10138`, thread stack size at `0x10130`, the certificate through
  `0x10118`, the section table through `0x10120` - 25 references in all), and Windows reads the main image's
  PE headers through the PEB whenever a DLL initialises. With `MZ` gone, `RtlImageNtHeader` returns null and
  `user32`, `d3d9` and `rpcrt4` all fault during init - which surfaces only as `LoadLibrary` failing with
  error 1114. They coexist because each needs only a few bytes in a fixed place: the XBE headers go down
  whole, `MZ` and `e_lfanew` are stamped back over the `XBEH` magic and the digital signature (neither of
  which anything reads), and the PE headers proper are parked past the XBE's own - 0x62c spare bytes against
  the 0x198 they need.

Sections are mapped writable regardless of what the XBE says, because the whole decompilation works by
patching game code in place.

Nineteen kernel import ordinals are implemented, of 96: virtual memory (4), contiguous memory (4), pool
memory (4), critical sections (6 ordinals over 4 functions), and `PsCreateSystemThreadEx`. The rest resolve to a generated stub that names
the import and the address that called it, then stops. That stub table is what produced the list - run, read
the name, implement it, run again - and it is worth keeping for the same reason.

`PsCreateSystemThreadEx` ignores the `SystemRoutine` it is given (`XapiThreadStartup`), which exists to build
the Xbox TLS block through the KPCR; the thread starts directly at `StartRoutine`, which is shaped exactly
like a Win32 thread proc.

### 4.4 Remove the physical-memory alias - DONE for graphics

`D3D_UncachedAliasOf` in `d3dSeam.cpp` now decides once, from the first address that passes through it,
whether the `0x8xxxxxxx` alias is actually mapped, and returns the plain address when it is not. Under
Standalone, resource memory comes from `MmAllocateContiguousMemoryEx`, which is a plain `VirtualAlloc`, so a
resource's `Data` word is already the address the CPU should use. The symptom before this was the XMV decoder
writing a frame to `0x8b042700` when the surface it had locked was at `0x0b042700`.

Probing beats a build-time switch or a "is CXBX loaded" test because it checks the thing that actually
matters. Still outstanding: the sound-bank path (`IDirectSoundBuffer_SetBufferData` receives an alias
pointer - see `docs/audio-inventory.md`), and an audit for `0xF0000000` write-combined alias users.

### 4.4a The DirectSound stream path - DONE

Not in the original plan, and forced by the rest of it. The XMV decoder does not reach DirectSound through
any game function the audio seam replaces - it calls DSOUND's own exports - so under CXBX those calls landed
in CXBX's HLE. Standalone they reach the XBE's real DirectSound, which drives the MCPX audio hardware
directly: mixer registers at `0xfe80xxxx`, read in spin loops. That is unbacked memory here, so the first
movie with an audio track faulted, and it is what both crashes reported against the first standalone build
turned out to be.

`src/action/sound/dsndStream.cpp` implements the stream on XAudio2 instead: `DirectSoundCreateStream` returns
an object with our own vtable, so everything the decoder does goes to us. Packets complete on XAudio2's
buffer-end callback rather than on submission, because the decoder paces video against audio completion.

Two things worth keeping in mind for anything similar:

- **The hooks are installed by hand, conditionally.** `AUTOINJECT` and `FUNC_AT` patch unconditionally, and
  every one of these replacements would be wrong under CXBX. The same now goes for the startup work in 4.2:
  it is all installed from `Inject_XboxStartup` behind `Xbox_RunningStandalone()`, which tests for
  `cxbxr-emu.dll` in the process.
- **Vtable slots need their `RET` immediates checked too.** `CDirectSoundStream_Process` ends `RET 0xc` - it
  takes three parameters and reads two. Declaring it with two cost a debugging cycle, with the decoder
  returning into a corrupted frame a long way from the call. The standing warning in section 5 is not only
  about exported entry points.

### 4.5 Driving engine

Out of scope for the above but the same method applies; it is C++-heavy with its own D3D8/DSOUND
copies (`inject_driving.cpp` is the current foothold). Do it only after the action engine runs
standalone, and reuse the D3D9 and audio backends as libraries.

## 5. Practical notes for agents

- **Build** (from a PowerShell prompt in the repository root; the output is `Release/actioninject.dll`):
  `& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe" nightfiRE.sln /t:actioninject /p:Configuration=Release /p:Platform=Win32 /m /v:m`.
  `tools/preprocess.py` runs as a pre-build step and needs a function name in `tools/functions_action.json`
  for every `AUTOINJECT` (or use `FUNC_AT(<address>)`).
- **The tag must be the line immediately above the declaration.** `preprocess.py` reads the *next* line after
  an `AUTOINJECT`/`FUNC_AT` comment and takes the token before the last `(` as the function name. A comment
  between the two makes it fail with `IndexError: list index out of range`, several frames from anything that
  names the file. Put the explanation above the tag, not below it.
- **Running the game**: build the `action` target, then run `Release/action.exe` with the working directory
  set to `Release` (it looks for `../disc/default.xbe`). `action_cxbx.exe` is the old CXBX-hosted launcher,
  kept as a reference to bisect against. It writes everything to stdout, so
  redirecting it to a file is the easiest way to read a whole boot. An unimplemented kernel import prints its
  name and the address that called it and then exits; a fault prints the faulting address, the address it
  touched, that page's state, and a call stack walked from the frame pointers - usually enough to name the
  cause in Ghidra without attaching a debugger.
- **Working out why the frame rate is what it is**: set `PerfLog=on` in `settings.ini`. Every few seconds it
  prints the frame rate with the time split into working and waiting, the cost per draw call, the per-frame
  draw/upload/allocation counts, and how the streaming reads completed. The split is the useful part: if the
  pacer is waiting, the rate is simply the one it was asked for; if it never waits, the game is behind and the
  counters say on what.

  Cost per draw call is the number to compare between machines, because it is near-constant for a given
  graphics stack. Native D3D9 on Windows measures about 2.5 us. Under Wine on macOS, with WineD3D translating
  to OpenGL, it measured about 147 us - roughly 70 times worse - which makes a 2000-draw mission run at 3 fps
  while an 85-draw multiplayer map is comfortably at 50. That was worth knowing because it looks like a
  single-player bug and is not one: the per-draw cost is identical in both, and only the draw count differs.
  The fix there is the graphics stack (DXVK, so D3D9 goes to Vulkan/MoltenVK rather than OpenGL), not this
  repository - though fewer draw calls would help every host.
- **Sizing up an XBE before touching it**: `python tools/survey_xbe.py disc/default.xbe` prints its base and
  size, its kernel import count, and its FS-segment accesses broken down by offset - which is the quickest
  way to see how much of the startup incompatibility applies. It decodes displacements rather than matching
  byte patterns, because the executable sections contain data and a bare two-byte match is mostly noise.
- **Reproducing something that is several menus in**, without a person at the keyboard:
  `tools/drive_game.ps1 -Keys enter,enter,enter`. It launches the loader, brings its window to the front,
  presses keys at it and reports any fault. Both crashes found after the loader first booted - starting a
  mission, and opening the codename screen - needed this to reproduce and then to re-check after each fix.
  Keys must go in with `keybd_event`, not posted messages, because the game reads input with
  `GetAsyncKeyState`; the script's header explains the rest and lists the key names.
- **Check the `RET` immediate of every library entry point you call**, against the parameter count of the
  typedef you write for it. These are all callee-cleans `__stdcall`, so a typedef one parameter short
  unbalances the stack by 4 bytes with no crash at the call itself - the *calling* function returns to
  garbage. Ghidra had `DSOUND::DirectSoundCreate` down as 2 parameters when it is 3 (`RET 0xc`), which cost a
  debugging cycle in stage A: the symptom was a black window and the engine wandering off into the
  error/relaunch path several frames later, with nothing wrong at or near the actual call. Reading the last
  bytes of each entry point out of the image (`read_memory`, look for `c2 imm16` / `c3`) checks all of them
  cheaply, and cross-checking `Cxbx-Reloaded/src/core/hle/` - which is checked out in this tree - gives the
  intended signature for free.
- **XAPI looks like Win32, but its state lives in the XBE.** The Xbox's XAPI is a Win32 clone, and at the
  call site the two are often identical - `createFile` *is* `CreateFileA`, `readFromFileBlocking` *is*
  `ReadFile`, and the struct the game calls `IO_STATUS_BLOCK` *is* a Win32 `OVERLAPPED`. That makes replacing
  it far easier than it looks, but two differences bite, and both cost a test cycle in stage B step 4.1:
  - **The last-error value is separate storage.** `XAPILIB::SetLastError`/`GetLastError` read and write a slot
    in the XBE's own TLS block (`*(TLS[_tls_index] + 4)`), not the TEB slot Win32's `SetLastError` writes. Any
    replacement has to mirror its result into the XBE's slot, or code that distinguishes cases by error code
    silently takes the wrong branch. `maybeReadFile` tells "the read is in flight" from "the read failed"
    purely by testing for `ERROR_IO_PENDING`, so reading a stale zero sent it straight to
    `FS_FatalErrorHandler` - the "disc may be dirty or damaged" screen, which never returns.
  - **Return widths.** These functions return a full 32-bit `EAX` and their callers test all of it; a C++
    `bool` return only sets `AL` under MSVC and leaves the top 24 bits as whatever was in the register. A
    Ghidra decompile shows this as `CONCAT31(extraout_var, result) != 0` in the *caller* - worth reading the
    caller, not just the callee, before picking a return type.
- **Parameter counts come from the `RET` immediate, not the decompiled prototype.** Ghidra reported six
  parameters for `createFile`; it is `RET 0x1c` and its call site pushes seven dwords with no caller cleanup.
  The seventh is `hTemplateFile`, which is what makes it exactly `CreateFileA`. The same check caught
  `DSOUND::DirectSoundCreate` being one short in stage A - see the note above.
- **Verify offline where possible**: `tools/vsh_translate_test.ps1` compiles all 130 shaders without
  the game; an ADPCM decoder should get the same treatment (decode a sound bank file and compare against
  a known-good decode).
- **Diagnostics beat reasoning**: the shadow bug, the movie bug and the vanishing-chunk bug were each
  solved by a log line or an image dump, after theory had run dry. Budget for adding instrumentation
  early.
- **Commit only when the user has tested**, never `git add -A`, and keep `tools/functions_action.json`
  changes (Ghidra syncs) in their own commits.
- **Ghidra names are the map**: when a function's role becomes clear, rename it in
  `functions_action.json` (the sync script round-trips it) and reference addresses in comments so the
  next reader can find it in Ghidra.
