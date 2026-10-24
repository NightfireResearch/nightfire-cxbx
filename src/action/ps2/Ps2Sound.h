#ifndef PS2_PS2SOUND_H_
#define PS2_PS2SOUND_H_

// The PS2's sound (docs/ps2-data.md): the SPU's ADPCM (16-byte blocks of 28 samples: a shift and filter byte, a flags
// byte, 14 bytes of nibbles), which our sound code cannot play, made the Xbox's ADPCM (src/common/sound/xadpcm.h:
// 36-byte blocks of 64 samples), which it can. No game state: the offline checker uses it too.

#include <stddef.h>
#include <stdint.h>

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

// The bytes Ps2Spu_EncodeXAdpcm writes for this many mono samples: whole 36-byte blocks
size_t Ps2Spu_XAdpcmBytes(size_t samples);

// Encodes mono 16-bit samples as Xbox ADPCM (IMA steps, each block starting from the state the last ended in, the
// last block padded with the last sample); the bytes written
size_t Ps2Spu_EncodeXAdpcm(const int16_t *pcm, size_t samples, uint8_t *out);

#endif // PS2_PS2SOUND_H_
