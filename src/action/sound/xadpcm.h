#ifndef XADPCM_H_
#define XADPCM_H_

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------
// Xbox ADPCM (WAVE_FORMAT_XBOX_ADPCM, 0x0069) to 16-bit PCM. Every sound buffer the game creates is in this
// format (see xboxCreateSoundBuffers in dsndSeam.cpp: 44032 Hz, 4 bits per sample, 64 samples per block), so the
// native audio backend has to decode it before it can hand anything to a host API.
//
// The codec is IMA ADPCM with an Xbox-specific block layout. Per channel, one block is 36 bytes:
//
//   offset 0  int16  initial predictor   (little-endian)
//   offset 2  int16  initial step index  (little-endian, clamped to 0..88)
//   offset 4  8 x uint32 of 8 nibbles each, low nibble first, least significant byte first
//
// For stereo, blocks are 72 bytes: the two 4-byte headers first (channel 0 then 1), then the eight 4-byte
// nibble groups interleaved per channel (ch0 group 0, ch1 group 0, ch0 group 1, ...).
//
// SAMPLES PER BLOCK - the one thing here that could not be settled offline, so it is a single named constant.
// The game's own wave format says 64 (wSamplesPerBlock = 64, and nAvgBytesPerSec = nSamplesPerSec * 36 / 64,
// which only balances at 64), so the 4-byte header's predictor is the state the first nibble is applied to and
// is NOT itself emitted: 32 bytes of nibbles -> 64 samples. That also keeps blocks independently decodable,
// which the hardware needs in order to start playing from an arbitrary loop point.
//
// Note that this differs from Microsoft's other IMA ADPCM (format tag 0x0011), where the header predictor *is*
// the block's first sample and a 36-byte block therefore yields 65. Luigi Auriemma's decoder and CXBX's use of
// it both take that reading, which stretches every buffer by one sample per block - 1.6% flat, inaudible in
// practice but wrong against the declared format, and it would drift the FMV A/V sync that measures its own
// elapsed time. If pitch or stream timing ever looks off by ~1.6%, this constant is the thing to question.
// ---------------------------------------------------------------------------------------------------------------

#define XADPCM_BLOCK_BYTES_PER_CHANNEL 36
#define XADPCM_SAMPLES_PER_BLOCK       64

// Bytes one block occupies / samples (per channel) one block decodes to, for a given channel count.
static inline size_t XAdpcm_BlockBytes(int channels) {
    return (size_t)XADPCM_BLOCK_BYTES_PER_CHANNEL * (size_t)channels;
}

// How many whole blocks are in srcBytes. A trailing partial block is ignored (the hardware would not play it,
// and every buffer the game binds is a whole number of blocks).
size_t XAdpcm_BlockCount(size_t srcBytes, int channels);

// Samples per channel, and total int16 values (samples * channels), that srcBytes decodes to.
size_t XAdpcm_DecodedSamplesPerChannel(size_t srcBytes, int channels);
size_t XAdpcm_DecodedValueCount(size_t srcBytes, int channels);

// Decodes srcBytes of Xbox ADPCM into interleaved 16-bit PCM. dst must have room for
// XAdpcm_DecodedValueCount(srcBytes, channels) int16 values; dstValueCapacity is checked and decoding stops
// early rather than overrunning. channels must be 1 or 2 (all the game ever creates). Returns the number of
// int16 values written.
size_t XAdpcm_Decode(const void *src, size_t srcBytes, int channels, int16_t *dst, size_t dstValueCapacity);

// Position conversions. The game speaks in ADPCM byte offsets on both sides of the seam - dsndSetLoopRegion
// rounds a loop point down to a block boundary in bytes, and psiStreamGetPlayPos expects
// IDirectSoundBuffer_GetCurrentPosition to report an ADPCM byte offset - so the backend needs to map between
// those and the decoded PCM sample index it actually plays from. Both round down to a block boundary.
size_t XAdpcm_ByteOffsetToSample(size_t byteOffset, int channels);
size_t XAdpcm_SampleToByteOffset(size_t sampleIndex, int channels);

#endif // XADPCM_H_
