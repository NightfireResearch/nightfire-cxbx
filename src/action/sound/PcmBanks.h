#ifndef ACTION_SOUND_PCMBANKS_H_
#define ACTION_SOUND_PCMBANKS_H_

// Sound the backends play as the PCM it was decoded to, for the data builds whose sound is not Xbox ADPCM (the PS2's,
// the GameCube's: NIGHTFIRE_DATA_PCM_BANKS). The game's banks and music hold stand-ins as long as the Xbox ADPCM of
// the same samples would be, so everything the game reckons in bytes is as the Xbox's; the backends' decode
// (pcmCache.cpp) takes a stand-in's samples from here instead of decoding it, so nothing is re-encoded.
//
// - A bank sample's stand-in starts with PCM_BANKS_SAMPLE_TAG and a word of its bank (high half) and number.
// - A music track's stand-in is stereo blocks of Xbox ADPCM's size (72 bytes, 64 samples), each starting with
//   PCM_BANKS_MUSIC_TAG, the track's key and the block's number.

#include <stddef.h>
#include <stdint.h>
#include <vector>

#define PCM_BANKS_SAMPLE_TAG 0x4d435042u    // "BPCM"
#define PCM_BANKS_MUSIC_TAG 0x4d434d4du     // "MMCM"

// A bank sample's PCM (mono), padded to whole Xbox ADPCM blocks as its stand-in is long, and its rate
struct PcmBankSample {
    std::vector<int16_t> pcm;
    int frequency;
};

// Pads a sample to whole blocks: from its loop's start round (loopStart in samples), or with silence (-1)
void PcmBanks_Pad(std::vector<int16_t> &pcm, int loopStart);

// The stand-in for a sample, at data (its Xbox ADPCM size, at least 8 bytes): zeros but for the tag
void PcmBanks_WriteStandIn(uint8_t *data, uint32_t bytes, int bank, int sample);

// Keeps a bank's samples, in place of any it had; the banks used longest ago are given back past 128 MB
void PcmBanks_Keep(int bank, std::vector<PcmBankSample> &&samples);

// A sample's PCM when data is a stand-in (its values, mono), else NULL
const int16_t *PcmBanks_SamplePcm(const void *data, uint32_t bytes, int channels, size_t *values);

// Keeps a music track's PCM (stereo, interleaved), in place of the last; its key, which its blocks carry
uint32_t PcmBanks_KeepMusic(std::vector<int16_t> &&pcm);

// The stand-in blocks firstBlock to firstBlock + count of the track with this key, at out
void PcmBanks_WriteMusic(uint32_t key, uint32_t firstBlock, uint32_t count, uint8_t *out);

// A fingerprint of PCM (FNV-1a over its bytes), for checking what the backends get against a decode offline
uint32_t PcmBanks_Fingerprint(const int16_t *pcm, size_t values);

// Decodes Xbox ADPCM as XAdpcm_Decode does, the music's stand-in blocks taken from their track
size_t PcmBanks_Decode(const void *adpcm, size_t bytes, int channels, int16_t *pcm, size_t values);

#endif // ACTION_SOUND_PCMBANKS_H_
