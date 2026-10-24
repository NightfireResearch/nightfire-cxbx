#include "Ps2Sound.h"
#include "../../common/sound/xadpcm.h"

#include <stdint.h>
#include <string.h>

uint32_t Ps2Spu_PitchToHz(uint32_t pitch) {
    static const uint32_t kRates[] = { 8000, 11025, 12000, 16000, 19200, 22050, 24000, 32000, 44100, 48000 };
    uint32_t hz = (uint32_t)(((uint64_t)pitch * 48000 + 2048) / 4096);
    for (uint32_t rate : kRates) {
        if (hz * 100 >= rate * 99 && hz * 100 <= rate * 101)
            return rate;
    }
    return hz;
}

// The SPU's prediction filters, in 64ths: sample = nibble << 12 >> shift, plus s1 * f0 + s2 * f1
static const int kFilters[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

size_t Ps2Spu_Decode(const uint8_t *spu, size_t bytes, int16_t *pcm, long *loopStart) {
    int s1 = 0, s2 = 0;
    size_t written = 0;
    *loopStart = -1;
    for (size_t at = 0; at + SPU_BLOCK_BYTES <= bytes; at += SPU_BLOCK_BYTES) {
        const uint8_t *block = spu + at;
        int shift = block[0] & 0xf;
        int filter = block[0] >> 4;
        if (filter > 4)
            filter = 0;
        uint8_t flags = block[1];
        if ((flags & SPU_FLAG_LOOP_START) && *loopStart < 0)
            *loopStart = (long)written;
        for (int n = 0; n < SPU_BLOCK_SAMPLES; n++) {
            int nibble = (block[2 + n / 2] >> ((n & 1) * 4)) & 0xf;
            int value = (int)((int16_t)(nibble << 12) >> shift);
            value += (s1 * kFilters[filter][0] + s2 * kFilters[filter][1] + 32) >> 6;
            if (value > 32767) value = 32767;
            if (value < -32768) value = -32768;
            pcm[written++] = (int16_t)value;
            s2 = s1;
            s1 = value;
        }
        if (flags & SPU_FLAG_LOOP_END)
            break;
    }
    return written;
}

size_t Ps2Spu_XAdpcmBytes(size_t samples) {
    return (samples + XADPCM_SAMPLES_PER_BLOCK - 1) / XADPCM_SAMPLES_PER_BLOCK * XADPCM_BLOCK_BYTES_PER_CHANNEL;
}

// The IMA step and index tables of xadpcm.cpp: the codec's own
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
static const int8_t kIndexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

// The sample the decoder makes of a code from a state
static int DecodedSample(unsigned code, int predictor, int stepIndex) {
    int step = kStepTable[stepIndex];
    int delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    if (code & 8) delta = -delta;
    int next = predictor + delta;
    return next < -32768 ? -32768 : next > 32767 ? 32767 : next;
}

// One sample's code - whichever of the 16 lands nearest, looking one sample ahead so as not to take a code whose
// step leaves the next sample out of reach - and the state advanced exactly as the decoder will advance it
static unsigned EncodeSample(int sample, int nextSample, int *predictor, int *stepIndex) {
    unsigned best = 0;
    int64_t bestError = INT64_MAX;
    for (unsigned code = 0; code < 16; code++) {
        int value = DecodedSample(code, *predictor, *stepIndex);
        int index = *stepIndex + kIndexTable[code];
        index = index < 0 ? 0 : index > 88 ? 88 : index;
        int64_t error = (int64_t)(value - sample) * (value - sample);
        int64_t nextError = INT64_MAX;
        for (unsigned code2 = 0; code2 < 16; code2++) {
            int value2 = DecodedSample(code2, value, index);
            int64_t e = (int64_t)(value2 - nextSample) * (value2 - nextSample);
            if (e < nextError)
                nextError = e;
        }
        if (error + nextError < bestError) {
            bestError = error + nextError;
            best = code;
        }
    }
    *predictor = DecodedSample(best, *predictor, *stepIndex);
    int index = *stepIndex + kIndexTable[best];
    *stepIndex = index < 0 ? 0 : index > 88 ? 88 : index;
    return best;
}

size_t Ps2Spu_EncodeXAdpcm(const int16_t *pcm, size_t samples, uint8_t *out) {
    int predictor = 0, stepIndex = 0;
    size_t bytes = Ps2Spu_XAdpcmBytes(samples);
    for (size_t first = 0, at = 0; at < bytes; first += XADPCM_SAMPLES_PER_BLOCK, at += XADPCM_BLOCK_BYTES_PER_CHANNEL) {
        uint8_t *block = out + at;
        block[0] = (uint8_t)predictor;
        block[1] = (uint8_t)(predictor >> 8);
        block[2] = (uint8_t)stepIndex;
        block[3] = 0;
        memset(block + 4, 0, XADPCM_BLOCK_BYTES_PER_CHANNEL - 4);
        for (int n = 0; n < XADPCM_SAMPLES_PER_BLOCK; n++) {
            size_t i = first + n;
            int sample = samples == 0 ? 0 : pcm[i < samples ? i : samples - 1];
            int next = samples == 0 ? 0 : pcm[i + 1 < samples ? i + 1 : samples - 1];
            unsigned code = EncodeSample(sample, next, &predictor, &stepIndex);
            block[4 + n / 2] |= (uint8_t)(code << ((n & 1) * 4));
        }
    }
    return bytes;
}
