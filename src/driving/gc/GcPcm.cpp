#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The store of the GameCube's bank samples as PCM (GcPcm.h). Banks are kept by their contents for as long as the game
// runs: every bank on the disc is 80 MB of PCM all told, and a bank loaded again finds its samples still here. The
// sound outputs' threads read what the loading thread keeps, under one lock.

#include "GcPcm.h"
#include "GcBytes.h"
#include "../../common/sound/xadpcm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <mutex>
#include <utility>

namespace {

constexpr uint32_t kBlockBytes = XADPCM_BLOCK_BYTES_PER_CHANNEL;
constexpr uint32_t kBlockSamples = XADPCM_SAMPLES_PER_BLOCK;

struct Bank {
    std::vector<GcPcmSample> samples;
    std::vector<bool> shown;
};

std::mutex Lock;
std::map<uint64_t, uint32_t> NumbersByContents;
std::map<uint32_t, Bank> Banks;
const bool Show = getenv("NIGHTFIRE_GC_PCM_SHOW") != NULL;

uint64_t ContentsKey(const uint8_t *file, uint32_t size) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < size; i++)
        hash = (hash ^ file[i]) * 0x100000001b3ull;
    return hash ^ size;
}

}  // namespace

uint32_t GcPcm_BankNumber(const uint8_t *file, uint32_t size) {
    if (size < 4 || memcmp(file, "BNKb", 4) != 0)
        return 0;
    uint64_t key = ContentsKey(file, size);
    std::lock_guard<std::mutex> hold(Lock);
    auto found = NumbersByContents.find(key);
    if (found != NumbersByContents.end())
        return found->second;
    uint32_t number = uint32_t(NumbersByContents.size()) + 1;
    NumbersByContents[key] = number;
    return number;
}

void GcPcm_Pad(std::vector<int16_t> *pcm, int32_t loopStart) {
    size_t length = pcm->size();
    size_t padded = (length + kBlockSamples - 1) / kBlockSamples * kBlockSamples;
    if (loopStart < 0 || size_t(loopStart) >= length) {
        pcm->resize(padded, 0);
        return;
    }
    pcm->reserve(padded);
    for (size_t i = length; i < padded; i++)
        pcm->push_back((*pcm)[loopStart + (i - length) % (length - loopStart)]);
}

void GcPcm_WriteStandIn(uint8_t *data, uint32_t blocks, uint32_t bank, uint32_t sample) {
    memset(data, 0, blocks * kBlockBytes);
    for (uint32_t block = 0; block < blocks; block++) {
        uint8_t *b = data + block * kBlockBytes;
        GcPutLe32(b, GC_PCM_TAG);
        GcPutLe32(b + 4, bank << 16 | sample);
        GcPutLe32(b + 8, block);
    }
}

void GcPcm_Keep(uint32_t bank, const char *path, std::vector<GcPcmSample> &&samples) {
    std::lock_guard<std::mutex> hold(Lock);
    if (Banks.count(bank) != 0)
        return;
    if (Show) {
        size_t values = 0;
        for (const GcPcmSample &s : samples)
            values += s.pcm.size();
        printf("[gcpcm] bank %u: %s, %u samples, %.1f MB of PCM\n", bank, path, unsigned(samples.size()),
               values * 2 / 1048576.0);
    }
    Bank &kept = Banks[bank];
    kept.shown.assign(samples.size(), false);
    kept.samples = std::move(samples);
}

size_t GcPcm_Decode(const void *adpcm, size_t bytes, int channels, int16_t *pcm, size_t values) {
    if (channels != 1)
        return XAdpcm_Decode(adpcm, bytes, channels, pcm, values);
    const uint8_t *block = static_cast<const uint8_t *>(adpcm);
    size_t written = 0;
    for (; bytes >= kBlockBytes && written + kBlockSamples <= values; block += kBlockBytes, bytes -= kBlockBytes) {
        if (GcLe32(block) != GC_PCM_TAG) {
            written += XAdpcm_Decode(block, kBlockBytes, 1, pcm + written, kBlockSamples);
            continue;
        }
        uint32_t bank = GcLe32(block + 4) >> 16, sample = GcLe32(block + 4) & 0xffff, number = GcLe32(block + 8);
        std::lock_guard<std::mutex> hold(Lock);
        auto found = Banks.find(bank);
        const GcPcmSample *s = NULL;
        if (found != Banks.end() && sample < found->second.samples.size())
            s = &found->second.samples[sample];
        if (s != NULL && (number + 1) * kBlockSamples <= s->pcm.size())
            memcpy(pcm + written, &s->pcm[number * kBlockSamples], kBlockSamples * sizeof(int16_t));
        else
            memset(pcm + written, 0, kBlockSamples * sizeof(int16_t));
        if (Show && s != NULL && number == 0 && !found->second.shown[sample]) {
            found->second.shown[sample] = true;
            printf("[gcpcm] bank %u sample %u: %u frames, fingerprint %08x\n", bank, sample, s->frames,
                   GcPcm_Fingerprint(s->pcm.data(), s->frames));
        }
        written += kBlockSamples;
    }
    return written;
}

uint32_t GcPcm_Fingerprint(const int16_t *pcm, size_t values) {
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = 0; i < values; i++) {
        hash = (hash ^ uint8_t(pcm[i])) * 0x01000193u;
        hash = (hash ^ uint8_t(uint16_t(pcm[i]) >> 8)) * 0x01000193u;
    }
    return hash;
}
