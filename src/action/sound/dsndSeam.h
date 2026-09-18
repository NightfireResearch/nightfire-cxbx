#ifndef DSNDSEAM_H_
#define DSNDSEAM_H_

#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// "Thin seam" reimplementation of Eurocom's own audio wrapper layer - the dsnd*/xbox*Sound/SFX* functions at
// 0x000e0f00-0x000e1e40 that sit directly on top of the statically linked DSOUND library CXBX still emulates.
// This is the audio counterpart of Direct3D/d3dSeam.cpp and follows the same method (see
// docs/cxbx-removal-plan.md, stage A): every function declared here still calls the same, completely untouched
// DSOUND:: entry points the original did, through the DSoundSeamTraced dispatch in dsndSeam.cpp, so CXBX's own
// DirectSound emulation keeps working exactly as it does today. This is deliberately NOT yet the switch to a
// native audio backend.
//
// The point of the exercise is to make the library boundary explicit and testable: after this, the game side of
// the audio seam is our code, and the only thing left inside CXBX is the public DSOUND entry points listed at
// the top of dsndSeam.cpp. A native XAudio2 backend can then be attached to those, exactly as d3d9Backend.cpp is
// attached to the D3D8 entry points.
//
// Calling conventions here were cross-checked against raw disassembly, not just Ghidra's decompile:
//  - no function in 0x000e0f00-0x000e1e40 ends in a "RET <imm>" (verified by an instruction search over the
//    whole range), so all of them leave stack cleanup to the caller - __cdecl, whatever Ghidra's own
//    calling_convention field says for the ones it guessed __stdcall for (those all take no arguments, so the
//    guess was unobservable either way);
//  - the single exception is dsndCreateSoundBufferWithSomeDefaultSettings, which takes its channel count in
//    EDX and its 3D flag on the stack, and still leaves cleanup to the caller - a hidden custom convention of
//    the same class as D3DDevice_SetRenderState_Simple in the D3D seam. It needs the naked entry trampoline in
//    dsndSeam.cpp; do not call it directly from new C++ code.
//
// The same applies in the other direction, to the DSOUND entry points the seam *calls*: a typedef with the
// wrong parameter count silently unbalances the stack, because these are all callee-cleans __stdcall. Every
// one of them has had its RET immediate read out of the image and checked against its typedef (Ghidra had
// DirectSoundCreate one parameter short, which corrupted xboxInitSound's own frame). Check any new one the
// same way rather than trusting the reported prototype.
// ---------------------------------------------------------------------------------------------------------------

// Xbox DirectSound objects. Opaque to the seam - only the DSOUND library ever looks inside them.
typedef struct DSoundObject DSoundObject;   // DIRECTSOUND       in Ghidra
typedef struct DSoundBuffer DSoundBuffer;   // DIRECTSOUNDBUFFER
typedef struct DSoundStream DSoundStream;   // DIRECTSOUNDSTREAM

#pragma pack(push, 1)

// Plain WAVEFORMATEX, plus the Xbox ADPCM extension (WAVE_FORMAT_XBOX_ADPCM = 0x69: 4 bits per sample, 64
// samples per 36-byte block per channel). Offsets confirmed against the stack-frame layout of
// dsndCreateSoundBufferWithSomeDefaultSettings.
struct WAVEFORMATEX_Xbox {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
};
struct XBOXADPCMWAVEFORMAT_Xbox {
    WAVEFORMATEX_Xbox wfx;
    uint16_t wSamplesPerBlock;
};

struct DSMIXBINVOLUMEPAIR_Xbox {
    uint32_t dwMixBin;
    int32_t  lVolume;   // hundredths of a dB, -10000 (silence) .. 0 (unattenuated)
};
struct DSMIXBINS_Xbox {
    uint32_t dwMixBinCount;
    DSMIXBINVOLUMEPAIR_Xbox *lpMixBinVolumePairs;
};

struct DSBUFFERDESC_Xbox {
    uint32_t dwSize;          // always 24 (sizeof this struct)
    uint32_t dwFlags;         // DSBCAPS_*
    uint32_t dwBufferBytes;
    WAVEFORMATEX_Xbox *lpwfxFormat;
    DSMIXBINS_Xbox *lpMixBins;
    uint32_t dwInputMixBin;
};

// I3DL2 (reverb) per-source parameters. Only lRoom is ever set by the game.
struct DSI3DL2BUFFER_Xbox {
    int32_t lDirect;
    int32_t lDirectHF;
    int32_t lRoom;
    int32_t lRoomHF;
    float   flRoomRolloffFactor;
    int32_t lObstruction;          // DSI3DL2OBSTRUCTION: lObstruction + flObstructionLFRatio
    float   flObstructionLFRatio;
    int32_t lOcclusion;            // DSI3DL2OCCLUSION: lOcclusion + flOcclusionLFRatio
    float   flOcclusionLFRatio;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------------------------------------------
// Backend selection. "AudioBackend=cxbx" in settings.ini (the default) leaves every DSOUND entry point going to
// CXBX's HLE exactly as before. The native backend is not written yet - the enum and the dispatch exist now so
// that the bring-up can be driven by the DSound_BackendMissing accounting, the way the D3D9 one was.
// ---------------------------------------------------------------------------------------------------------------
enum { AUDIO_BACKEND_CXBX = 0, AUDIO_BACKEND_XAUDIO2 = 1 };
extern int g_audioBackend;

// Counts (and logs once) a DSOUND entry point the selected native backend does not implement yet.
void DSound_BackendMissing(const char *entryPoint);

// ---------------------------------------------------------------------------------------------------------------
// The game side of the seam. Names are Ghidra's, so the AUTOINJECT table can find them by name. Three are
// injected by address (FUNC_AT) and named properly here instead: the two that were still FUN_000exxxx when this
// was written, and the per-frame voice update, whose Ghidra name is shared with an unrelated function.
// ---------------------------------------------------------------------------------------------------------------

// Boot-time setup (called once from main): creates the one DirectSound object, turns on full HRTF, downloads the
// I3DL2 reverb DSP image, creates all 192 sound buffers and builds the 0..100 -> hundredths-of-a-dB volume table.
void xboxInitSound(void);

// Creates the 192 static buffers xboxInitSound needs: per voice slot 0..63, one mono 2D, one stereo 2D and one
// mono 3D buffer, all Xbox ADPCM at 44032 Hz, with the game's fixed mixbin/headroom/rolloff setup.
void __cdecl xboxCreateSoundBuffers(void);

// Creates one Xbox ADPCM sound buffer with the game's default settings (44032 Hz, 4 bits/sample, 64 samples per
// block), 3D-capable if numChannels == 1 and is3d != 0. Returns the new buffer, 0 on failure.
//
// NOTE: naked entry trampoline - the original takes numChannels in EDX. Never call this from new C++ code; call
// dsndCreateSoundBuffer() below instead.
void dsndCreateSoundBufferWithSomeDefaultSettings(void);
uint32_t dsndCreateSoundBuffer(int numChannels, char is3d); // the real implementation behind the trampoline

// The 3D listener. SetListenerPosition/SetListenerOrientation also cache their values in AudioSystem,
// because dsndUpdateVoices reads them back to park non-positioned 3D voices on top of the listener.
// SetListenerVelocity only ever zeroes the velocity (the game never uses Doppler).
void __cdecl SetListenerPosition(float x, float y, float z);
void __cdecl SetListenerVelocity(void);
void __cdecl SetListenerOrientation(float xDir, float yDir, float zDir, float xUp, float yUp, float zUp);

// Voice allocation. A "voice" is one of 64 slots in AudioSystem.maybeVoices; dsndGetVoice picks the first free
// one, binds the matching pre-made buffer (mono 2D / stereo 2D / mono 3D), points it at the caller's own sample
// data (SetBufferData references that memory, it is not copied) and marks the slot in use. Returns the slot
// index, or -1 if nothing is free or the arguments are unusable.
int __cdecl dsndGetVoice(void *data, int size, uint32_t frequency, int numChannels, bool loop, char is3d);

// Per-voice state. All of these no-op on an out-of-range slot or a slot that is not in use, exactly like the
// original; the 3D ones additionally no-op on a 2D voice, and dsndSetPan the other way round.
bool __cdecl dsndIsPlaying(uint32_t channel);
void __cdecl dsndMarkActive(uint32_t channel);      // "keep this slot allocated even once it stops playing"
void __cdecl dsndMarkInactive(uint32_t channel);    // release that hold, so the slot can be freed
void __cdecl psiSampleUnPause(uint32_t channel);    // play (also clears a pending stop/pause)
void __cdecl dsndSamplePause(uint32_t channel, uint8_t pause); // FUN_000e1400: stop, optionally as a pause
void __cdecl dsndBufferSetVolume(uint32_t channel, int volume); // volume is 0..100, via the lookup table
void __cdecl dsndSetFrequency(uint32_t channel, int frequency);
void __cdecl dsndSetLoopRegion(uint32_t channel, int loopStart); // rounded down to an ADPCM block boundary
void __cdecl dsndSetPan(uint32_t channel, int leftRight, int frontBack); // 2D voices only, -100..100 each
void *__cdecl dsndGetData(uint32_t channel);
uint32_t __cdecl psiStreamGetPlayPos(uint32_t channel);
void __cdecl dsndWriteVoiceData(uint32_t channel, int offset, const void *src, uint32_t length); // FUN_000e18a0

// Per-voice 3D state (3D voices only).
void __cdecl dsndSetPosition(uint32_t channel, float x, float y, float z);
void __cdecl dsndClearVelocity(uint32_t channel);
void __cdecl dsndSetDistances(uint32_t channel, float minDist, float maxDist);
void __cdecl dsndSetI3DL2Source(uint32_t channel, float volume); // reverb send, 0..100 into the volume table
void __cdecl maybeXboxSFXCalculate3D(uint32_t channel, uint8_t positioned); // 0 = follow the listener

// Streams (music and video audio). The streams themselves are created by the XMV decoder library and by
// maybeSFXCreateStreamForVideo, neither of which is reimplemented yet.
void __cdecl BackgroundMovieSetupMix(DSoundStream *stream); // 5.1 mixbins with the centre channel muted
void __cdecl dsndStreamSetVolume(DSoundStream *stream, int volume);

// Called once per frame: pushes deferred 3D settings, services DSOUND's own work queue, then starts/stops
// voices according to the per-slot request flags and frees the slots that have finished playing.
//
// 0x000e19c0. Ghidra calls this SFXUpdate, but it also calls the (entirely unrelated) high-level SFX system
// update at 0x000cad40 SFXUpdate - so this one is injected by address and named for what it actually does,
// rather than letting an AUTOINJECT by name patch both.
void dsndUpdateVoices(void);

// Level change / shutdown: parks the listener, drops every buffer's reference to the game's sample data (which
// is about to be freed), stops every voice through one last dsndUpdateVoices, and clears the voice table.
void __cdecl maybeSoundShutdown(void);

#endif // DSNDSEAM_H_
