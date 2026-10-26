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

// ---- music

// A PS2 track: two channels of SPU ADPCM at 32 kHz, 128 bytes (8 blocks, 224 samples) of the left, then 128 of the
// right. As the Xbox's code sees it: two channels of Xbox ADPCM's size, 72-byte blocks of 64 samples, at the same
// rate (SFX_MUSIC_FREQUENCY).
#define MUSIC_PAIR_BYTES 256
#define MUSIC_PAIR_SAMPLES 224
#define MUSIC_XBOX_BLOCK_BYTES (2 * XADPCM_BLOCK_BYTES_PER_CHANNEL)

uint32_t Ps2Music_ToXboxBytes(uint32_t ps2Bytes) {
    uint64_t samples = (uint64_t)ps2Bytes * MUSIC_PAIR_SAMPLES / MUSIC_PAIR_BYTES;
    return (uint32_t)(samples * MUSIC_XBOX_BLOCK_BYTES / XADPCM_SAMPLES_PER_BLOCK);
}

void Ps2Music_Decode(const uint8_t *ssd, size_t bytes, std::vector<int16_t> &stereo) {
    size_t pairs = bytes / MUSIC_PAIR_BYTES;
    stereo.assign(pairs * MUSIC_PAIR_SAMPLES * 2, 0);
    int hist[2][2] = {};
    int16_t block[SPU_BLOCK_SAMPLES];
    for (size_t p = 0; p < pairs; p++) {
        for (int c = 0; c < 2; c++) {
            const uint8_t *chunk = ssd + p * MUSIC_PAIR_BYTES + c * (MUSIC_PAIR_BYTES / 2);
            for (int b = 0; b < MUSIC_PAIR_SAMPLES / SPU_BLOCK_SAMPLES; b++) {
                DecodeBlock(chunk + b * SPU_BLOCK_BYTES, &hist[c][0], &hist[c][1], block);
                for (int n = 0; n < SPU_BLOCK_SAMPLES; n++)
                    stereo[((p * MUSIC_PAIR_SAMPLES) + b * SPU_BLOCK_SAMPLES + n) * 2 + c] = block[n];
            }
        }
    }
}
