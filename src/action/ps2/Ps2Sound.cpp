#include "Ps2Sound.h"
#include "../../common/sound/xadpcm.h"

#include <stdint.h>
#include <string.h>
#include <vector>

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

// One block's 28 samples, the filter's history in s1 and s2
static void DecodeBlock(const uint8_t *block, int *s1, int *s2, int16_t *pcm) {
    int shift = block[0] & 0xf;
    int filter = block[0] >> 4;
    if (filter > 4)
        filter = 0;
    for (int n = 0; n < SPU_BLOCK_SAMPLES; n++) {
        int nibble = (block[2 + n / 2] >> ((n & 1) * 4)) & 0xf;
        int value = (int)((int16_t)(nibble << 12) >> shift);
        value += (*s1 * kFilters[filter][0] + *s2 * kFilters[filter][1] + 32) >> 6;
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        pcm[n] = (int16_t)value;
        *s2 = *s1;
        *s1 = value;
    }
}

size_t Ps2Spu_Decode(const uint8_t *spu, size_t bytes, int16_t *pcm, long *loopStart) {
    int s1 = 0, s2 = 0;
    size_t written = 0;
    *loopStart = -1;
    for (size_t at = 0; at + SPU_BLOCK_BYTES <= bytes; at += SPU_BLOCK_BYTES) {
        const uint8_t *block = spu + at;
        uint8_t flags = block[1];
        if ((flags & SPU_FLAG_LOOP_START) && *loopStart < 0)
            *loopStart = (long)written;
        DecodeBlock(block, &s1, &s2, pcm + written);
        written += SPU_BLOCK_SAMPLES;
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

// The usual IMA encoder's code for a difference: its sign, then the step, half and quarter steps it holds
static unsigned QuantisedCode(int diff, int stepIndex) {
    int step = kStepTable[stepIndex];
    unsigned code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) { code |= 4; diff -= step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; }
    if (diff >= step >> 2) code |= 1;
    return code;
}

// One sample's code - whichever of the 16 lands nearest, looking one sample ahead (at the usual encoder's code for
// it) so as not to take a code whose step leaves the next sample out of reach - and the state advanced exactly as the
// decoder will advance it
static unsigned EncodeSample(int sample, int nextSample, int *predictor, int *stepIndex) {
    unsigned best = 0;
    int64_t bestError = INT64_MAX;
    for (unsigned code = 0; code < 16; code++) {
        int value = DecodedSample(code, *predictor, *stepIndex);
        int index = *stepIndex + kIndexTable[code];
        index = index < 0 ? 0 : index > 88 ? 88 : index;
        int64_t error = (int64_t)(value - sample) * (value - sample);
        int value2 = DecodedSample(QuantisedCode(nextSample - value, index), value, index);
        int64_t nextError = (int64_t)(value2 - nextSample) * (value2 - nextSample);
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

// ---- music

// A PS2 track: two channels of SPU ADPCM at 32 kHz, 128 bytes (8 blocks, 224 samples) of the left, then 128 of the
// right. The Xbox's: two channels of Xbox ADPCM at 44.1 kHz, 72-byte blocks of 64 samples each.
#define MUSIC_PAIR_BYTES 256
#define MUSIC_PAIR_SAMPLES 224
#define MUSIC_PS2_RATE 32000
#define MUSIC_XBOX_RATE 44100
#define MUSIC_XBOX_BLOCK_BYTES (2 * XADPCM_BLOCK_BYTES_PER_CHANNEL)
#define MUSIC_WARM_BLOCKS 8        // encoded first and dropped, so that a piece starts in the state the music before left

uint32_t Ps2Music_ToXboxBytes(uint32_t ps2Bytes) {
    uint64_t samples = (uint64_t)ps2Bytes * MUSIC_PAIR_SAMPLES / MUSIC_PAIR_BYTES;
    uint64_t xboxSamples = samples * MUSIC_XBOX_RATE / MUSIC_PS2_RATE;
    return (uint32_t)(xboxSamples * MUSIC_XBOX_BLOCK_BYTES / XADPCM_SAMPLES_PER_BLOCK);
}

void Ps2Music_XboxBlocks(uint32_t firstBlock, uint32_t count, uint32_t ps2Bytes, Ps2ReadFn read, void *context,
                         uint8_t *out) {
    // The output samples, a block before the first to start the encoder off, and the source samples they need
    uint64_t warm = firstBlock < MUSIC_WARM_BLOCKS ? firstBlock : MUSIC_WARM_BLOCKS;
    uint64_t x0 = (firstBlock - warm) * XADPCM_SAMPLES_PER_BLOCK;
    uint64_t x1 = ((uint64_t)firstBlock + count) * XADPCM_SAMPLES_PER_BLOCK;
    uint64_t s0 = x0 * MUSIC_PS2_RATE / MUSIC_XBOX_RATE;
    uint64_t s1 = x1 * MUSIC_PS2_RATE / MUSIC_XBOX_RATE + 2;
    uint64_t pair0 = s0 / MUSIC_PAIR_SAMPLES;
    pair0 = pair0 > 0 ? pair0 - 1 : 0;              // a pair before, for the filters' history
    uint64_t pair1 = s1 / MUSIC_PAIR_SAMPLES + 1;
    uint64_t pairs = (uint64_t)ps2Bytes / MUSIC_PAIR_BYTES;
    if (pair1 > pairs)
        pair1 = pairs;

    std::vector<int16_t> pcm[2];
    uint64_t base = pair0 * MUSIC_PAIR_SAMPLES;
    if (pair1 > pair0) {
        std::vector<uint8_t> spu((size_t)(pair1 - pair0) * MUSIC_PAIR_BYTES);
        if (!read(context, (uint32_t)(pair0 * MUSIC_PAIR_BYTES), (uint32_t)spu.size(), spu.data()))
            memset(spu.data(), 0, spu.size());
        int hist[2][2] = {};
        for (int c = 0; c < 2; c++)
            pcm[c].resize((size_t)(pair1 - pair0) * MUSIC_PAIR_SAMPLES);
        for (uint64_t p = 0; p < pair1 - pair0; p++) {
            for (int c = 0; c < 2; c++) {
                const uint8_t *chunk = spu.data() + p * MUSIC_PAIR_BYTES + c * (MUSIC_PAIR_BYTES / 2);
                for (int b = 0; b < MUSIC_PAIR_SAMPLES / SPU_BLOCK_SAMPLES; b++)
                    DecodeBlock(chunk + b * SPU_BLOCK_BYTES, &hist[c][0], &hist[c][1],
                                &pcm[c][(size_t)(p * MUSIC_PAIR_SAMPLES + b * SPU_BLOCK_SAMPLES)]);
            }
        }
    }
    // Resampled to 44.1 kHz, between neighbouring samples
    auto source = [&](int c, uint64_t s) -> int {
        return s >= base && s - base < pcm[c].size() ? pcm[c][(size_t)(s - base)] : 0;
    };
    size_t samples = (size_t)(x1 - x0);
    std::vector<int16_t> resampled[2];
    for (int c = 0; c < 2; c++) {
        resampled[c].resize(samples + 1);
        for (size_t k = 0; k <= samples; k++) {
            uint64_t num = (x0 + k) * MUSIC_PS2_RATE;
            uint64_t s = num / MUSIC_XBOX_RATE;
            int frac = (int)(num % MUSIC_XBOX_RATE);
            int a = source(c, s), b = source(c, s + 1);
            resampled[c][k] = (int16_t)(a + (int)((int64_t)(b - a) * frac / MUSIC_XBOX_RATE));
        }
    }
    // Encoded: each block both channels' headers, then their eight 4-byte groups in turn
    int predictor[2] = {}, stepIndex[2] = {};
    for (uint64_t block = 0; block < warm + count; block++) {
        uint8_t local[MUSIC_XBOX_BLOCK_BYTES];
        uint8_t *dst = block >= warm ? out + (block - warm) * MUSIC_XBOX_BLOCK_BYTES : local;
        memset(dst, 0, MUSIC_XBOX_BLOCK_BYTES);
        for (int c = 0; c < 2; c++) {
            dst[c * 4 + 0] = (uint8_t)predictor[c];
            dst[c * 4 + 1] = (uint8_t)(predictor[c] >> 8);
            dst[c * 4 + 2] = (uint8_t)stepIndex[c];
        }
        for (int n = 0; n < XADPCM_SAMPLES_PER_BLOCK; n++) {
            size_t k = (size_t)(block * XADPCM_SAMPLES_PER_BLOCK + n);
            for (int c = 0; c < 2; c++) {
                unsigned code = EncodeSample(resampled[c][k], resampled[c][k + 1], &predictor[c], &stepIndex[c]);
                dst[8 + (n / 8) * 8 + c * 4 + (n % 8) / 2] |= (uint8_t)(code << ((n & 1) * 4));
            }
        }
    }
}
