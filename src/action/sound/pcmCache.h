#ifndef ACTION_SOUND_PCMCACHE_H_
#define ACTION_SOUND_PCMCACHE_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The sound backends' decoded PCM (xaudio2Backend.cpp, sdlAudioAction.cpp).
//
// The same sound bank data is bound to a buffer many times over a session (the inventory counted ~10 rebinds per
// buffer), and decoding a 3.3 MB ADPCM bank is not something to do per bind. Keyed on (data pointer, byte count,
// a sample of its contents); decoding every distinct buffer seen in a whole session came to about 17 MB, so an entry
// goes only when the cache is full, and never while a buffer still plays from it.
// ---------------------------------------------------------------------------------------------------------------

// The decoded samples of a buffer's ADPCM (values: samples times channels), or NULL
const int16_t *PcmCache_Decode(const void *adpcm, uint32_t adpcmBytes, int channels, size_t *values);

// The game has overwritten part of a buffer's ADPCM in place (dsndWriteVoiceData: streamed music): its decode is
// patched to match
void PcmCache_Written(const void *adpcm, uint32_t offset, uint32_t length);

// Whether a buffer is bound to these samples, so that they are not freed while they play. The backend's.
bool PcmCache_InUse(const int16_t *pcm);

#endif // ACTION_SOUND_PCMCACHE_H_
