# Removing CXBX from the action engine: project plan

Status as of September 2026, written for whoever (human or agent) picks up the next stages. Read this
alongside `src/action/engine/Direct3D/d3dSeam.cpp` and `d3d9Backend.cpp`, which are the worked example of
the pattern every stage below repeats.

## 1. Where things stand

The action engine (`default.xbe`) still runs *inside* cxbx-reloaded's process: the launcher starts
`cxbxr-ldr.exe /load default.xbe /hwnd <our window>`, then injects `actioninject.dll`, which patches
game functions at their fixed addresses (`tools/preprocess.py` turns the `AUTOINJECT`/`FUNC_AT` tags
into the patch table). Roughly 10% of the game's functions are reimplemented; the rest is the original
x86 code running natively. CXBX provides everything the XBE expects from the Xbox: the loader, the memory
map, the kernel (files, threads, timers, memory), and the high-level emulation of the statically linked
Microsoft libraries (D3D8, DSOUND, XAPILIB, XGRAPHC).

What already bypasses CXBX, each one a "seam" that replaces a library boundary with our own code:

| Subsystem | Seam | Notes |
|---|---|---|
| Graphics | `Direct3D/d3dSeam.cpp` + `d3d9Backend.cpp` | Every D3D8/XGRAPHC entry point goes through `D3DSeamTraced` dispatch; with `GraphicsBackend=d3d9` nothing in the D3D8 library runs. NV2A vertex programs are translated to HLSL at runtime. |
| Input | `engine/psiInput.cpp` | Direct XInput; CXBX's controller emulation unused. |
| Settings (EEPROM) | `engine/XboxSettings.cpp` | `settings.ini` replaces `ExQueryNonVolatileSetting`. |
| Saves | `engine/psiSave.cpp` | Plain files under `saves/`. |
| Files | `engine/psiFile.cpp`, `engine/FS.cpp` | `psiFileOpen` and friends read from the extracted disc; the XMV decoder's and the audio streamer's own file reads still go through XAPI/NT (see 3.3). |
| Relaunch | `common/launchInfo.cpp` | `XLaunchNewImageA`/`XGetLaunchInfo` replaced (the driving engine is a second XBE, `inject_driving.cpp`). |

Still supplied by CXBX, in dependency order (each later item needs the earlier ones gone first):

1. **Audio**: the DSOUND library (110 functions in the XBE, `DSOUND::` in `tools/functions_action.json`),
   HLE'd by CXBX onto host DirectSound.
2. **XAPI runtime**: threads, `QueryPerformanceCounter`, overlapped file I/O, `XapiInitProcess`,
   `mainXapiStartup`/`_cinit` (CRT init), thread-notify routines.
3. **Kernel**: the ~95 `xboxkrnl.exe` imports (list below), of which the game itself reaches only a
   handful directly; most are used by the libraries above.
4. **Loader and memory map**: CXBX maps the XBE at its link address (`0x10000` base, sections at their
   virtual addresses), reserves the Xbox physical-memory alias at `0x80000000`, and provides the
   `MmAllocateContiguousMemory` pool the game's `allocateContiguous` draws GPU memory from.
5. **Window and process**: the D3D9 device is created on CXBX's `CxbxRender` child window.

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

### 4.2 Replace the XAPI/CRT startup

`entry` -> `mainXapiStartup` -> `XapiInitProcess` (heap, `XMountUtilityDrive`, TLS, `_cinit`, thread
notify routines) -> the game's `main`. Reimplement `entry` as our own `DllMain`-driven start: run the
CRT initialisers the XBE's `_cinit` table lists (they are plain function pointers in the image), set
up the game's heap (`Mem_Init` is already understood), then call `main`. At this point nothing in
XAPILIB runs.

### 4.3 Own loader

Replace `cxbxr-ldr.exe` with our own executable that:

1. Reserves the address range the XBE needs (`0x10000` base through the end of the last section; the
   XBE header and section table are simple, `tools/vsh_dump.py` walks them in a dozen lines of Python) -
   this needs a host executable whose own image and heap stay out of that range (link the loader high,
   or make it a tiny stub that maps the XBE before the CRT allocates).
2. Maps each section from `default.xbe` at its virtual address with the right protection, and zeroes
   the `.bss`-style sections.
3. Fills the kernel import thunk table (the XBE's `KernelThunk` array, resolved by ordinal) with our
   implementations from 4.1, and patches the library entry points with the seam backends, exactly as
   `Inject()` does today (`WriteJmpTo`, `WriteMemory`).
4. Creates the window, runs the game's main loop on the main thread, and pumps messages
   (`wndproc.cpp` already has the window side).

The injected DLL and the loader can share one codebase: today's `actioninject.dll` becomes a static
part of the loader. Keep the DLL-into-CXBX path working until the standalone one is at parity, behind
the same settings switches, so regressions can always be bisected against CXBX.

### 4.4 Remove the physical-memory alias

CXBX maps Xbox physical memory at `0x80000000` and the seam still relies on it in three places
(`D3D_UncachedAliasOf` in `d3dLockSurface`; the `Data | 0x80000000` sentinel convention in the D3D9
backend for psiBlurScreen; and sound bank data, which reaches `IDirectSoundBuffer_SetBufferData` as an
alias pointer - see `docs/audio-inventory.md`). Both become plain pointers once D3D8 is gone for good. Audit
`tools/functions_action.json` for other `0x8xxxxxxx`/`0xFxxxxxxx` constant users (the write-combined
alias `0xF0000000` is the other one) before removing the mapping.

### 4.5 Driving engine

Out of scope for the above but the same method applies; it is C++-heavy with its own D3D8/DSOUND
copies (`inject_driving.cpp` is the current foothold). Do it only after the action engine runs
standalone, and reuse the D3D9 and audio backends as libraries.

## 5. Practical notes for agents

- **Build** (from a PowerShell prompt in the repository root; the output is `Release/actioninject.dll`):
  `& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe" nightfiRE.sln /t:actioninject /p:Configuration=Release /p:Platform=Win32 /m /v:m`.
  `tools/preprocess.py` runs as a pre-build step and needs a function name in `tools/functions_action.json`
  for every `AUTOINJECT` (or use `FUNC_AT(<address>)`).
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
