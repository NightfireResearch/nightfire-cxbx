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

## Streams belong to CXBX, not to the seam

`DirectSoundCreateStream`, `IDirectSoundStream_Process`, `IDirectSoundStream_Pause`,
`IDirectSound_SynchPlayback` and `IDirectSound_Release` **never appear in the trace**, even though two FMVs
played. That is not because they are unused - it is because the XMV decoder library calls them directly,
rather than through any game function the seam has replaced. All the seam sees of an FMV stream is the
`IDirectSoundStream_SetMixBins` and `SetVolume` calls that `BackgroundMovieSetupMix` and
`dsndStreamSetVolume` make on a stream somebody else created (5 calls each, which is the FMVs played).

The consequence is **not** that FMV audio goes silent under a native backend, which is what an earlier
version of this document claimed and testing disproved. CXBX's patches on the DSOUND entry points are
installed regardless of what the seam does, so the decoder's calls still land in CXBX's HLE and CXBX still
plays those streams. In native mode two audio systems therefore run side by side: ours for the game's
voices, CXBX's for the decoder's streams. FMV audio is audible.

What this does mean:

- The **stream setters the game itself makes have to be passed through to the DSOUND library** even in
  native mode, because the stream object they act on is CXBX's, not the backend's. Attaching them to a
  native backend, or leaving them unattached, drops the game's own volume and mixbin routing and leaves the
  stream playing at whatever CXBX defaulted to - which is what wrong music levels look like. See
  `DSoundSeamPassThrough` in `dsndSeam.cpp`.
- Streams only become the backend's problem when the stream entry points are hooked at their own addresses,
  the way `d3dSeam.cpp` hooks the four D3D8 entry points whose callers live inside the video decoder. That
  cannot be done in cxbx mode - a `FUNC_AT` hook on a library entry point has nowhere to forward to - so it
  has to land behind the `AudioBackend` switch, and it is not needed at all until CXBX itself goes away.

## Two long-standing bugs, and why they are the same bug

Both of these were seen under CXBX long before any of this work, and are worth recording because the cause
turned out to be a single line in CXBX rather than anything in the game.

Reported symptoms:

1. The music system would fairly often get stuck looping the first few hundred milliseconds of a track,
   persisting until the game was restarted, with no obvious trigger.
2. Pausing and resuming would restart a stream from its beginning rather than continuing - most visibly the
   villain's speech in the final level.

`CXBX-Reloaded`'s `IDirectSoundBuffer_Stop` rewinds the play cursor:

```cpp
// TODO : Test Stop (emulated via Stop + SetCurrentPosition(0)) :
hRet = pThis->EmuDirectSoundBuffer8->Stop();
pThis->EmuDirectSoundBuffer8->SetCurrentPosition(0);
```

That is symptom 2 on its own: every Stop/Play pair restarts from zero. The game expects DirectSound's actual
behaviour, where Stop retains the position and Play resumes - and its own code proves that is the intent,
because `dsndSamplePause` deliberately keeps the voice slot allocated (the paused flag exists for exactly
that) so it can be resumed later.

Symptom 1 then follows from symptom 2 through `SFXUpdateStreams`. When the streamer underruns, the data end
marker check pauses the voice (`psiSamplePause`) and sets `ShutDown`; when more data has been transferred it
resumes (`psiSampleUnPause`). With a rewinding Stop that resume replays the opening chunk, and since the next
write offset is computed from the play position it is now out of step and underruns again almost immediately.
Pause, rewind, replay, repeat. It presents as an unpredictable trigger because it is an underrun race, and it
never recovers because the timeout that would otherwise stop a wedged stream is explicitly skipped for music
streams (`if (200 < TimedOut && MusicStream == 0)`).

The XAudio2 backend implements the retain-and-resume semantics, so neither symptom should occur in native
mode. Note that this makes native mode *more* correct than the cxbx baseline rather than merely equal to it,
which is worth remembering when A/B-ing the two: a difference is not automatically a regression. The cxbx
path could be made to behave the same way by having the seam save the position before `Stop` and restore it
after the following `Play`, at the cost of no longer having an unmodified baseline to bisect against.

## Open question: is the 3D path too hot relative to 2D?

Reported while listening to the XAudio2 backend: close 3D sounds (shell casings, bodies hitting the floor)
possibly louder than remembered, and the music track possibly slightly quieter. Both impressions are
uncertain, and concentrated listening does surface roughness that was always there - but they are consistent
with a single cause, which is why they are recorded together.

The 3D and 2D paths do not have the same gain structure:

- The game gives 3D buffers **0** headroom and 2D buffers **600** (6 dB), so 3D sits 6 dB hotter than 2D by
  design. Music is a stereo 2D voice, so it is on the quiet side of that split.
- The 3D sounds in question are *close*. Minimum distances in use are 2, 4, 10 and 20 units, and inside the
  minimum distance the curve is flat at full volume - correct DirectSound semantics, but it means these
  particular sounds play at maximum gain through a completely clean path.
- On the Xbox the same voice was routed to six mixbins at once (four cross-talk bins, front centre, I3DL2)
  and went through the cross-talk cancellation network, which has a gain structure the backend does not
  reproduce at all - X3DAudio owns the matrix for 3D voices instead.

Ruled out: headroom itself is not the difference between the two modes, because CXBX applies it the same way
(`volume - headroom`). If the 3D/2D balance really does differ between `cxbx` and `xaudio2`, it has to come
from the distance model - CXBX's DirectSound3D against X3DAudio plus our translated curve - and close range
is exactly where those diverge most.

How to settle it: find a spot with both casings and music, flip `AudioBackend`, and listen twice. Same
relative balance in both modes means the effect is pre-existing. If 3D is hotter under `xaudio2`, the fix
belongs in the distance curve, not in an unexplainable gain trim. Note also that CXBX is not a clean
reference - its DirectSound has at least one outright bug (see the rewinding `Stop` above) - so "different
from CXBX" does not by itself mean "wrong".

## Reverb: CXBX never implemented it either

Worth knowing before treating the missing I3DL2 reverb as a regression. In CXBX-Reloaded:

- `IDirectSoundBuffer_SetI3DL2Source` is `LOG_NOT_SUPPORTED(); return DS_OK;` - a complete no-op.
- `CDirectSound_DownloadEffectsImage` is `LOG_INCOMPLETE()`; it fabricates an ImageDesc structure for callers
  that ask for one and does nothing with the image itself.

So **no reverb has ever been heard in this project**, in either mode, and the dry 3D voices in the XAudio2
backend are not a step backwards from the cxbx baseline. Implementing it would make native mode more
faithful to real hardware than the baseline has ever been - which also means there is no local reference to
check the result against.

## Also worth noting

`IDirectSoundBuffer_SetBufferData` is called with data pointers like `0x824b54f4`, `0x837796ac` and
`0x83b7f360`: sound bank data reaches DirectSound through the **Xbox physical-memory alias at
`0x80000000`**, not as a plain pointer. Under CXBX that alias exists and the decoder can read straight
through it, but it is another user of the mapping that `cxbx-removal-plan.md` section 4.4 wants to remove -
add it to the audit there.
