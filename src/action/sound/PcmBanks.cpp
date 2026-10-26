#include "PcmBanks.h"
#include "../../common/sound/xadpcm.h"

#include <stdio.h>
#include <string.h>
#include <map>

#define PCM_KEPT_BYTES (128u << 20)        // the banks' PCM kept before the oldest goes
#define MUSIC_CHANNELS 2

struct Bank {
    std::vector<PcmBankSample> samples;
    uint32_t used;                          // when it was last kept or played, to give the oldest back
};
static std::map<int, Bank> Banks;
static uint32_t BankClock;

static uint32_t MusicKey;
static std::vector<int16_t> Music;

static uint32_t Le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void PutLe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void PcmBanks_Pad(std::vector<int16_t> &pcm, int loopStart) {
    size_t n = pcm.size();
    size_t padded = (n + XADPCM_SAMPLES_PER_BLOCK - 1) / XADPCM_SAMPLES_PER_BLOCK * XADPCM_SAMPLES_PER_BLOCK;
    if (padded == 0)
        padded = XADPCM_SAMPLES_PER_BLOCK;
    bool loops = loopStart >= 0 && (size_t)loopStart < n;
    for (size_t k = n; k < padded; k++)
        pcm.push_back(loops ? pcm[loopStart + (k - n) % (n - loopStart)] : 0);
}

void PcmBanks_WriteStandIn(uint8_t *data, uint32_t bytes, int bank, int sample) {
    memset(data, 0, bytes);
    if (bytes < 8)
        return;
    PutLe32(data, PCM_BANKS_SAMPLE_TAG);
    PutLe32(data + 4, (uint32_t)bank << 16 | (uint32_t)sample);
}

void PcmBanks_Keep(int bank, std::vector<PcmBankSample> &&samples) {
    Bank &kept = Banks[bank];
    kept.samples = std::move(samples);
    kept.used = ++BankClock;
    size_t total = 0;
    for (auto &b : Banks)
        for (PcmBankSample &s : b.second.samples)
            total += s.pcm.size() * sizeof(int16_t);
    while (total > PCM_KEPT_BYTES && Banks.size() > 1) {
        auto oldest = Banks.begin();
        for (auto it = Banks.begin(); it != Banks.end(); ++it)
            if (it->second.used < oldest->second.used)
                oldest = it;
        if (oldest->first == bank)
            break;
        for (PcmBankSample &s : oldest->second.samples)
            total -= s.pcm.size() * sizeof(int16_t);
        Banks.erase(oldest);
    }
}

const int16_t *PcmBanks_SamplePcm(const void *data, uint32_t bytes, int channels, size_t *values) {
    const uint8_t *p = (const uint8_t *)data;
    if (data == NULL || bytes < 8 || channels != 1 || Le32(p) != PCM_BANKS_SAMPLE_TAG)
        return NULL;
    uint32_t key = Le32(p + 4);
    auto found = Banks.find((int)(key >> 16));
    if (found == Banks.end() || (key & 0xffff) >= found->second.samples.size())
        return NULL;
    found->second.used = ++BankClock;
    const PcmBankSample &sample = found->second.samples[key & 0xffff];
    static int told;
    if (told < 8) {
        told++;
        printf("[sound] bank %u's sample %u to the mixer as PCM: %u samples at %d Hz, fingerprint %08x\n", key >> 16,
               key & 0xffff, (unsigned)sample.pcm.size(), sample.frequency,
               PcmBanks_Fingerprint(sample.pcm.data(), sample.pcm.size()));
    }
    *values = sample.pcm.size();
    return sample.pcm.data();
}

uint32_t PcmBanks_KeepMusic(std::vector<int16_t> &&pcm) {
    Music = std::move(pcm);
    printf("[sound] music to the mixer as PCM: %u stereo samples, fingerprint %08x\n", (unsigned)(Music.size() / 2),
           PcmBanks_Fingerprint(Music.data(), Music.size()));
    return ++MusicKey;
}

uint32_t PcmBanks_Fingerprint(const int16_t *pcm, size_t values) {
    uint32_t h = 2166136261u;       // FNV-1a over the samples' bytes
    const uint8_t *p = (const uint8_t *)pcm;
    for (size_t i = 0; i < values * sizeof(int16_t); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

void PcmBanks_WriteMusic(uint32_t key, uint32_t firstBlock, uint32_t count, uint8_t *out) {
    size_t blockBytes = XAdpcm_BlockBytes(MUSIC_CHANNELS);
    memset(out, 0, count * blockBytes);
    for (uint32_t b = 0; b < count; b++) {
        uint8_t *block = out + b * blockBytes;
        PutLe32(block, PCM_BANKS_MUSIC_TAG);
        PutLe32(block + 4, key);
        PutLe32(block + 8, firstBlock + b);
    }
}

size_t PcmBanks_Decode(const void *adpcm, size_t bytes, int channels, int16_t *pcm, size_t values) {
    size_t blockBytes = XAdpcm_BlockBytes(channels);
    size_t blockValues = (size_t)XADPCM_SAMPLES_PER_BLOCK * channels;
    const uint8_t *src = (const uint8_t *)adpcm;
    size_t written = 0;
    for (size_t at = 0; at + blockBytes <= bytes && written + blockValues <= values; at += blockBytes) {
        const uint8_t *block = src + at;
        if (channels == MUSIC_CHANNELS && Le32(block) == PCM_BANKS_MUSIC_TAG) {
            // (a block of a track since replaced is silent)
            size_t first = (size_t)Le32(block + 8) * blockValues;
            bool current = Le32(block + 4) == MusicKey;
            for (size_t k = 0; k < blockValues; k++)
                pcm[written + k] = current && first + k < Music.size() ? Music[first + k] : 0;
            written += blockValues;
        } else {
            written += XAdpcm_Decode(block, blockBytes, channels, pcm + written, values - written);
        }
    }
    return written;
}
