#ifndef DRIVING_SOUND_DRIVINGAUDIO_H_
#define DRIVING_SOUND_DRIVINGAUDIO_H_

#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The driving engine's sound output, behind the DirectSound seam (dsndSeam.cpp). Two bodies: xaudio2Driving.cpp
// (XAudio2 source voices) and sdlAudioDriving.cpp (a voice mixer of ours feeding an SDL3 audio stream, with
// NIGHTFIRE_SDL).
//
// Everything the game plays is a DirectSound buffer whose samples live in the game's own memory and are read
// by the console's audio hardware as it goes. Two kinds matter here, and this backend treats them as one:
//
//  - EA's software mixer (dsndMixInit, 0x0013d5e0) makes six looping 48 kHz mono PCM buffers of 50 ms, one
//    per 5.1 speaker, plays them once at start-up and never stops them. Its 100 Hz thread mixes every sound
//    in the game into them on the CPU, 20 ms at a time, ahead of the play cursor it reads back through
//    GetCurrentPosition. Nothing tells DirectSound the memory changed; the hardware just reads it.
//  - The 180 pooled buffers (dsndCreateBufferAndMixBins, 0x0013d430): 48 kHz mono, Xbox ADPCM or PCM, given
//    their sample data by pointer, played once or looping, pitched with SetFrequency, mixed with per-bin
//    volumes.
//
// So a voice here streams: it reads the next few milliseconds out of the game's memory as the output needs
// them, decoding ADPCM on the way, and reports as its play cursor the start of the chunk it reached last. A
// buffer the game rewrites behind that cursor is picked up within a chunk, well inside the mixer's lead, and
// the output runs at 48 kHz so the mixer's rings are never resampled.
//
// The output is stereo. A buffer's mixbins - the 5.1 speaker each one feeds, at what level - become an output
// matrix onto that pair, which is how the six mixer rings land on left and right and how a pooled sound gets
// its pan. Nothing in this library does 3D: the driving engine's DirectSound has no 3D voice entry points at
// all, EA positions sounds itself through the bin volumes.
//
// Threads: the game's threads call everything below; the output's own thread asks for audio as it needs it. One
// lock covers the voice state.
// ---------------------------------------------------------------------------------------------------------------

struct DrivingVoice;

// Starts the output. False if there is no audio device, in which case every call below is a no-op and the seam
// keeps its silent bookkeeping.
bool DrivingAudio_Start(void);

// The output device (stereo at 48 kHz), or none if audio did not start: the XAudio2 device, or SDL's audio device
// id. The movie player (src/driving/engine/PlayMPC.cpp) plays its own 5.1 stream on it (src/common/fmv/FmvAudio.h),
// folded to stereo with the same gains as the mixbins below (DrivingAudio_StereoFold).
#ifdef NIGHTFIRE_SDL
uint32_t DrivingAudio_GetDevice(void);
#else
struct IXAudio2;
IXAudio2 *DrivingAudio_GetDevice(void);
#endif

// Where each of the six 5.1 speakers (FL, FR, C, LFE, BL, BR) lands on the stereo output: matrix[speaker * 2] left,
// matrix[speaker * 2 + 1] right - the levels the six mixer rings get, which is how the game's own audio sounds.
void DrivingAudio_StereoFold(float matrix[12]);

// A voice for a buffer of this format. formatTag is WAVE_FORMAT_PCM or WAVE_FORMAT_XBOX_ADPCM (0x69).
DrivingVoice *DrivingAudio_CreateVoice(uint32_t sampleRate, uint16_t formatTag, uint16_t channels,
                                       uint16_t blockAlign, uint16_t bitsPerSample);
void DrivingAudio_Release(DrivingVoice *voice);

// The buffer's samples, by pointer into the game's memory, which is read as it stands whenever a chunk is
// submitted. NULL unbinds.
void DrivingAudio_SetData(DrivingVoice *voice, const void *data, uint32_t bytes);

void DrivingAudio_Play(DrivingVoice *voice, bool looping);
void DrivingAudio_Stop(DrivingVoice *voice);
bool DrivingAudio_IsPlaying(DrivingVoice *voice);

// Positions in bytes of the buffer's own format, as DirectSound reports and takes them. The play cursor is
// the start of the chunk now sounding; the write cursor is where the next chunk will be read from.
void DrivingAudio_GetPosition(DrivingVoice *voice, uint32_t *playCursor, uint32_t *writeCursor);
void DrivingAudio_SetPosition(DrivingVoice *voice, uint32_t bytes);

// A loop region in bytes; length 0 means the whole buffer.
void DrivingAudio_SetLoopRegion(DrivingVoice *voice, uint32_t startBytes, uint32_t lengthBytes);

// Volume in hundredths of a decibel, -10000 (silence) to 0; playback rate in Hz.
void DrivingAudio_SetVolume(DrivingVoice *voice, int32_t hundredthsDb);
void DrivingAudio_SetFrequency(DrivingVoice *voice, uint32_t hz);

// The mixbins the voice feeds, each at a level in hundredths of a decibel. SetMixBins replaces the set;
// SetMixBinVolumes changes the levels of bins already in it.
void DrivingAudio_SetMixBins(DrivingVoice *voice, const uint32_t *bins, const int32_t *volumes, uint32_t count);
void DrivingAudio_SetMixBinVolumes(DrivingVoice *voice, const uint32_t *bins, const int32_t *volumes, uint32_t count);

// One line of what the backend has been doing since the last call: chunks streamed, how many carried sound,
// voices alive and playing. Printed with the frame timing, so that a headless run can say whether audio is
// flowing without anyone listening.
void DrivingAudio_Report(void);

#endif // DRIVING_SOUND_DRIVINGAUDIO_H_
