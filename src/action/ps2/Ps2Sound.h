#ifndef PS2_PS2SOUND_H_
#define PS2_PS2SOUND_H_

// The PS2's sound (docs/ps2-data.md): the SPU's ADPCM (16-byte blocks of 28 samples: a shift and filter byte, a flags
// byte, 14 bytes of nibbles), decoded to PCM, which the sound backends play as it is (no re-encoding: the game holds
// stand-ins of Xbox ADPCM's size, PcmBanks.h). No game state: the offline checker uses it too.

#include <stddef.h>
#include <stdint.h>
#include <vector>

#define SPU_BLOCK_BYTES 16
#define SPU_BLOCK_SAMPLES 28
#define SPU_FLAG_LOOP_END 0x01      // the last block of a sound (or of its loop)
#define SPU_FLAG_LOOP_REPEAT 0x02   // with LOOP_END: go back to the loop start
#define SPU_FLAG_LOOP_START 0x04

// The rate the SPU plays at for a pitch word (4096 is 48 kHz), snapped to the usual rate within 1% (1882 is 22,050 Hz)
uint32_t Ps2Spu_PitchToHz(uint32_t pitch);

// Decodes a mono sound of SPU ADPCM, bytes of it, up to the block its end flag marks, into pcm (room for
// bytes / 16 * 28 samples). The samples written; *loopStart the first sample of the block flagged as the loop's start
// (-1 for none).
size_t Ps2Spu_Decode(const uint8_t *spu, size_t bytes, int16_t *pcm, long *loopStart);

// The bytes Xbox ADPCM takes for this many mono samples (whole 36-byte blocks): a stand-in's size (PcmBanks.h)
size_t Ps2Spu_XAdpcmBytes(size_t samples);

// ---- music: a track's .SSD (two channels of SPU ADPCM at 32 kHz, interleaved 128 bytes at a time) seen as the Xbox's
// (two channels of Xbox ADPCM's size at the same rate), which the backends play as its PCM (PcmBanks.h)

// A byte offset or size in a PS2 track as the Xbox's track of the same samples would have it: the .SMF markers'
// positions and the file's size
uint32_t Ps2Music_ToXboxBytes(uint32_t ps2Bytes);

// A whole track decoded: its PCM, stereo, interleaved
void Ps2Music_Decode(const uint8_t *ssd, size_t bytes, std::vector<int16_t> &stereo);

#endif // PS2_PS2SOUND_H_
