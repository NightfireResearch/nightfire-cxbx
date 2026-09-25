#include "xadpcm.h"

// ---------------------------------------------------------------------------------------------------------------
// Xbox ADPCM decoder - see xadpcm.h for the block layout and for the samples-per-block question. Written from
// the IMA ADPCM algorithm rather than adapted from an existing decoder, deliberately: CXBX-Reloaded's own
// XADPCM.h is GPLv2 and this project should not inherit that.
//
// The two tables below are the IMA ADPCM step and index tables. They are part of the codec definition - they
// have to match the hardware bit for bit, so there is nothing to choose here.
// ---------------------------------------------------------------------------------------------------------------

static const int16_t kStepTable[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

// One channel's running decoder state. The predictor is kept as int to make the clamp below unambiguous.
struct XAdpcmState {
    int predictor;
    int stepIndex;
};

static inline int ClampInt(int value, int low, int high) {
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

// Applies one 4-bit code to a channel's state and returns the sample it produces. Bits 0..2 are the magnitude
// (as step/8 + step/2 + step/4 + step, selected by the bits) and bit 3 is the sign.
static inline int16_t DecodeNibble(unsigned code, XAdpcmState *state) {
    int step = kStepTable[state->stepIndex];

    int delta = step >> 3;
    if (code & 4)
        delta += step;
    if (code & 2)
        delta += step >> 1;
    if (code & 1)
        delta += step >> 2;
    if (code & 8)
        delta = -delta;

    state->predictor = ClampInt(state->predictor + delta, -32768, 32767);
    state->stepIndex = ClampInt(state->stepIndex + kIndexTable[code], 0, 88);
    return (int16_t)state->predictor;
}

size_t XAdpcm_BlockCount(size_t srcBytes, int channels) {
    if (channels < 1)
        return 0;
    return srcBytes / XAdpcm_BlockBytes(channels);
}

size_t XAdpcm_DecodedSamplesPerChannel(size_t srcBytes, int channels) {
    return XAdpcm_BlockCount(srcBytes, channels) * XADPCM_SAMPLES_PER_BLOCK;
}

size_t XAdpcm_DecodedValueCount(size_t srcBytes, int channels) {
    if (channels < 1)
        return 0;
    return XAdpcm_DecodedSamplesPerChannel(srcBytes, channels) * (size_t)channels;
}

size_t XAdpcm_Decode(const void *src, size_t srcBytes, int channels, int16_t *dst, size_t dstValueCapacity) {
    if (src == NULL || dst == NULL || channels < 1 || channels > 2)
        return 0;

    const uint8_t *in = (const uint8_t *)src;
    size_t blocks = XAdpcm_BlockCount(srcBytes, channels);
    size_t written = 0;

    for (size_t block = 0; block < blocks; block++) {
        // Both channels' 4-byte headers come first, in channel order.
        XAdpcmState state[2];
        for (int c = 0; c < channels; c++) {
            state[c].predictor = (int16_t)(uint16_t)(in[0] | (in[1] << 8));
            state[c].stepIndex = ClampInt((int16_t)(uint16_t)(in[2] | (in[3] << 8)), 0, 88);
            in += 4;
        }

        // Then eight 4-byte groups per channel, interleaved per group. Each group is 8 nibbles, low nibble of
        // the least significant byte first. The samples of a group are emitted interleaved across channels, so
        // a group has to be decoded per channel into a scratch buffer before anything is written out.
        for (int group = 0; group < 8; group++) {
            int16_t groupSamples[2][8];
            for (int c = 0; c < channels; c++) {
                uint32_t codes = (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
                                 ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
                in += 4;
                for (int n = 0; n < 8; n++) {
                    groupSamples[c][n] = DecodeNibble(codes & 0xf, &state[c]);
                    codes >>= 4;
                }
            }
            for (int n = 0; n < 8; n++) {
                for (int c = 0; c < channels; c++) {
                    if (written >= dstValueCapacity)
                        return written;
                    dst[written++] = groupSamples[c][n];
                }
            }
        }
    }

    return written;
}

size_t XAdpcm_ByteOffsetToSample(size_t byteOffset, int channels) {
    if (channels < 1)
        return 0;
    return (byteOffset / XAdpcm_BlockBytes(channels)) * XADPCM_SAMPLES_PER_BLOCK;
}

size_t XAdpcm_SampleToByteOffset(size_t sampleIndex, int channels) {
    if (channels < 1)
        return 0;
    return (sampleIndex / XADPCM_SAMPLES_PER_BLOCK) * XAdpcm_BlockBytes(channels);
}
