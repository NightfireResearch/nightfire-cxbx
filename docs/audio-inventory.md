# DSOUND entry-point inventory

Stage A step 2 of `cxbx-removal-plan.md`: what the game actually asks DirectSound to do, measured rather
than reasoned about. This is the spec the native audio backend has to satisfy.

Captured with `DSNDSEAM_TRACE` set to 1 in `src/action/sound/dsndSeam.cpp` (it is 0 in the committed tree -
the tracing has a real per-call cost), over a single session of about 16,500 frames / 5.5 minutes: attract
movie, front end, two FMVs and a couple of levels, with weapons fired and music playing. The raw log lands
in `dsndSeam_trace.log` in the working directory the game is launched from.

## The boundary is complete

**29 distinct entry points, and they are exactly the 29 the seam has typedefs for** - nothing appeared that
`dsndSeam.cpp` does not already route, and nothing it routes went unused. For the paths played, the game
side of the audio seam is closed.

## Call volume

Counts are for the whole session; the per-frame column is the number that matters, because it is what the
backend has to sustain every frame.

| Entry point | Calls | Per frame | Notes |
|---|---:|---:|---|
| `DirectSoundCreate` | 1 | - | boot only |
| `DirectSoundUseFullHRTF` | 1 | - | boot only |
| `IDirectSound_DownloadEffectsImage` | 1 | - | the 0x6168-byte I3DL2 reverb DSP image |
| `IDirectSound_CreateSoundBuffer` | 192 | - | **boot only** - the buffer set is static for the whole process |
| `IDirectSoundBuffer_SetMixBins` | 192 | - | boot only, one per buffer |
| `IDirectSoundBuffer_SetHeadroom` | 192 | - | boot only; always 0 (3D) or 600 (2D) |
| `IDirectSoundBuffer_SetRolloffCurve` | 64 | - | boot only, 3D buffers; always the same 5-point curve at `0x19a9a8` |
| `IDirectSoundBuffer_SetBufferData` | 1,952 | 0.1 | ~10 rebinds per buffer over the session |
| `IDirectSoundBuffer_Play` | 410 | 0.02 | |
| `IDirectSoundBuffer_Stop` | 98 | 0.01 | |
| `IDirectSoundBuffer_SetCurrentPosition` | 416 | 0.03 | always to 0 |
| `IDirectSoundBuffer_SetLoopRegion` | 826 | 0.05 | |
| `DirectSoundDoWork` | 16,500 | 1.0 | once per frame |
| `IDirectSound_CommitDeferredSettings` | 16,500 | 1.0 | once per frame |
| `IDirectSound_SetPosition` / `SetVelocity` / `SetOrientation` | 18,164 each | 1.1 each | the listener |
| `IDirectSoundBuffer_GetStatus` | 127,393 | 7.7 | |
| `IDirectSoundBuffer_SetVolume` | 118,961 | 7.2 | |
| `IDirectSoundBuffer_SetFrequency` | 111,399 | 6.8 | |
| `IDirectSoundBuffer_SetI3DL2Source` | 92,228 | 5.6 | |
| `IDirectSoundBuffer_SetMinDistance` | 92,006 | 5.6 | |
| `IDirectSoundBuffer_SetMaxDistance` | 184,012 | 11.2 | twice per `dsndSetDistances`, by design |
| `IDirectSoundBuffer_SetPosition` / `SetVelocity` | 91,716 each | 5.6 each | |
| `IDirectSoundBuffer_SetMixBinVolumes_8` | 19,267 | 1.2 | 2D panning |
| `IDirectSoundBuffer_GetCurrentPosition` | 15,102 | 0.9 | `psiStreamGetPlayPos` |
| `IDirectSoundStream_SetMixBins` / `SetVolume` | 5 each | - | FMV streams, volume -68 |

## Argument ranges

- **Frequency**: 27 distinct values, **22,050 to 44,100 Hz**. Against the 44,032 Hz the buffers are created
  at, that is a ratio of **0.501 to 1.002** - so XAudio2's default maximum frequency ratio of 2.0 is ample
  and no special voice-creation flag is needed. Note the top of the range is slightly *above* the buffer's
  own format rate.
- **Volume**: hundredths of a dB through `AudioSystem.VolumeLookupTable`, so 0 down to -10000.
- **Min distance**: only 2, 4, 10, 20, 404. **Max distance**: 15, 30, 40, 100, 200, 500, and the sentinel
  999999 that `dsndSetDistances` writes first so the real value can never be rejected for exceeding the old
  maximum. A small enough set to precompute distance curves for.
- **Velocity is always (0,0,0)**, for both voices and the listener. **There is no Doppler in this game** -
  the backend can drop velocity entirely.
- **Position**: world units in the tens to low thousands; the listener sits in the same space. Orientation
  arrives as a normalised front vector plus a top vector.
- **Play flags**: `0x1` (`DSBPLAY_LOOPING`) or 0.
- **Loop regions**: start 0 with length 0 (loop the whole buffer) in the common case, plus two large
  non-zero starts, 0xa9038 and 0x83538 - both exact multiples of 36, which is a useful independent
  confirmation that loop points really are ADPCM byte offsets on block boundaries.
- **Buffer sizes**: 23 distinct non-zero, the largest **3,461,256 bytes** (3.3 MB) of ADPCM. Decoding every
  distinct buffer seen in this session to 16-bit PCM would come to about **17 MB**, so caching decoded PCM
  by (pointer, size) is affordable without an eviction policy, at least for a session like this one.

## What this tells the backend

1. **Diff everything.** The game re-sends a voice's entire 3D parameter set every frame whether or not it
   changed - roughly 5 to 8 voices active at a time, each getting position, velocity, min/max distance,
   I3DL2 send, volume and frequency re-pushed. Around 45 redundant calls per frame. The backend should cache
   the last value per voice and only touch XAudio2/X3DAudio when something actually differs, then run
   `X3DAudioCalculate` once per active voice in `DoWork`.
2. **The buffer set is static.** 192 buffers created at boot and never released, so a source voice per slot
   can be created once, lazily, and kept. What changes is the data bound to them.
3. **`GetStatus` is the hottest call in the whole seam** (7.7 per frame) and drives voice-slot recycling in
   `dsndUpdateVoices`. It must be cheap and it must report "stopped" at the right frame, or slots will
   either leak or be recycled while still audible.
4. **No Doppler, and a small distance-parameter set** - the 3D side is simpler than the API surface suggests.
5. **The decoded-PCM cache is the main memory cost** at roughly 17 MB, and `dsndWriteVoiceData` can
   overwrite a buffer's sample data in place while it plays (the streaming path), so a cache keyed on
   (pointer, size) alone will go stale there. That path needs either invalidation or bypassing.

## The gap: FMV stream audio

`DirectSoundCreateStream`, `IDirectSoundStream_Process`, `IDirectSoundStream_Pause`,
`IDirectSound_SynchPlayback` and `IDirectSound_Release` **never appear in the trace**, even though two FMVs
played. That is not because they are unused - it is because the XMV decoder library calls them directly,
rather than through any game function the seam has replaced. All the seam sees of an FMV stream is the
`IDirectSoundStream_SetMixBins` and `SetVolume` calls that `BackgroundMovieSetupMix` and
`dsndStreamSetVolume` make on a stream somebody else created (5 calls each, which is the FMVs played).

So a native backend will be **silent for FMV audio** until those entry points are hooked at their own
addresses, the way `d3dSeam.cpp` hooks the four D3D8 entry points whose callers live inside the video
decoder. That cannot be done in cxbx mode - a `FUNC_AT` hook on a library entry point has nowhere to
forward to - so it has to land together with the native backend, behind the `AudioBackend` switch.

## Also worth noting

`IDirectSoundBuffer_SetBufferData` is called with data pointers like `0x824b54f4`, `0x837796ac` and
`0x83b7f360`: sound bank data reaches DirectSound through the **Xbox physical-memory alias at
`0x80000000`**, not as a plain pointer. Under CXBX that alias exists and the decoder can read straight
through it, but it is another user of the mapping that `cxbx-removal-plan.md` section 4.4 wants to remove -
add it to the audit there.
