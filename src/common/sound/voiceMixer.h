#ifndef COMMON_SOUND_VOICEMIXER_H_
#define COMMON_SOUND_VOICEMIXER_H_

#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// A voice mixer of ours feeding one SDL3 audio stream (NIGHTFIRE_SDL): the sound output of both engines' SDL builds
// (src/driving/sound/sdlAudioDriving.cpp, src/action/sound/sdlAudioAction.cpp).
//
// A voice plays samples it reads in place, as the output needs them: PCM (8- or 16-bit) or Xbox ADPCM, decoded a
// block at a time, mono or stereo, at its own rate by linear interpolation, with a volume and a gain onto each side
// of the stereo output. Its play cursor is the start of the chunk it reached last; memory rewritten behind that
// cursor is picked up within a chunk.
//
// Threads: the games' threads call everything below; the output's own thread asks for audio as it needs it. One
// lock covers the voice state.
// ---------------------------------------------------------------------------------------------------------------

struct MixerVoice;

// Starts the output (stereo at 48 kHz). False if there is no audio device, in which case every call below is a
// no-op.
bool VoiceMixer_Start(void);

// SDL's id of the output device, or 0: the movie players bind streams of their own to it (common/fmv/FmvAudioSdl.cpp)
uint32_t VoiceMixer_Device(void);

// A voice for samples of this format. formatTag is WAVE_FORMAT_PCM or WAVE_FORMAT_XBOX_ADPCM (0x69). NULL if the
// output did not start or the format is neither.
MixerVoice *VoiceMixer_CreateVoice(uint32_t sampleRate, uint16_t formatTag, uint16_t channels,
                                   uint16_t blockAlign, uint16_t bitsPerSample);
void VoiceMixer_Release(MixerVoice *voice);

// The samples, by pointer, read as they stand whenever a chunk is mixed. NULL unbinds. Stops the voice.
void VoiceMixer_SetData(MixerVoice *voice, const void *data, uint32_t bytes);

// Play resumes where Stop left the cursor (or SetPosition put it); a one-shot that reaches its end stops and plays
// from the start next time.
void VoiceMixer_Play(MixerVoice *voice, bool looping);
void VoiceMixer_Stop(MixerVoice *voice);
bool VoiceMixer_IsPlaying(MixerVoice *voice);

// Positions in bytes of the voice's own format. The play cursor is the start of the chunk now sounding; the write
// cursor is where the next chunk will be read from.
void VoiceMixer_GetPosition(MixerVoice *voice, uint32_t *playCursor, uint32_t *writeCursor);
void VoiceMixer_SetPosition(MixerVoice *voice, uint32_t bytes);

// A loop region in bytes; length 0 means the whole buffer.
void VoiceMixer_SetLoopRegion(MixerVoice *voice, uint32_t startBytes, uint32_t lengthBytes);

// An amplitude, the playback rate in Hz (four times the voice's rate at most, 1/2000 of it at least), and the
// amplitude each side of the output gets
void VoiceMixer_SetVolume(MixerVoice *voice, float amplitude);
void VoiceMixer_SetFrequency(MixerVoice *voice, uint32_t hz);
void VoiceMixer_SetGains(MixerVoice *voice, float left, float right);

// One line of what the mixer has been doing since the last call: chunks mixed, how many carried sound, voices
// alive and playing, and the three busiest. So that a headless run can say whether audio is flowing.
void VoiceMixer_Report(void);

#endif // COMMON_SOUND_VOICEMIXER_H_
