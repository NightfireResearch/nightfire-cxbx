#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The GameCube's sound banks (BNKb) as the Xbox's (BNKl) (docs/gc-driving.md, "Sound"). The container is the
// Xbox's, big-endian: a header, then each patch's offset from its own word to its "PT" header. The PT headers' tags
// are the same on both (each value big-endian), but a GameCube timbre's samples are DSP ADPCM (sample representation
// 0x12: frames of eight bytes, a predictor and scale byte and fourteen 4-bit samples), its coefficients in tag 0x8f
// (sixteen 16-bit values, then a byte), and the bank holds its data where tag 0x88 (per channel 0x88, 0x89 ...) says.
// Each sample is decoded to PCM and encoded as Xbox ADPCM (0x14: 36-byte blocks of 64 samples a channel, the IMA
// steps), and the bank rebuilt round the new data: its headers, then the samples, each timbre's tags as the GameCube
// has them but the representation, the offsets and the coefficients, with the Xbox's render mode.

#include "GcConvert.h"
#include "GcBytes.h"

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace {

// The PT tags this reads or writes (snd/Banks.h's)
enum : uint8_t {
    kTagChannels = 0x82,
    kTagFrames = 0x85,
    kTagSampleData = 0x8a,
    kTagRenderMode = 0x8c,
    kTagDspCoefficients = 0x8f,
    kTagSampleRep = 0xa0,
    kTagPadding = 0xfc,
    kTagMarker = 0xfd,
    kTagNextTimbre = 0xfe,
    kTagEnd = 0xff,
};

constexpr uint8_t kSampleOffsetTags[6] = { 0x88, 0x89, 0x94, 0x95, 0xa2, 0xa3 };
constexpr uint8_t kRepXboxAdpcm = 0x14;
constexpr uint16_t kXboxRenderMode = 0x0420;   // the Xbox banks' timbres' tag 0x8c
constexpr uint32_t kBankHeaderBytes = 0x14;
constexpr uint8_t kXboxPlatform = 7;
constexpr int kDspFrameSamples = 14;
constexpr int kDspFrameBytes = 8;
constexpr int kAdpcmBlockSamples = 64;
constexpr int kAdpcmBlockBytes = 36;

const int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
const int8_t kIndexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

int Clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

// One 4-bit code for a sample, the state moved on as the decoder will move it
unsigned EncodeNibble(int sample, int *predictor, int *index) {
    int step = kStepTable[*index];
    int difference = sample - *predictor;
    unsigned code = 0;
    if (difference < 0) {
        code = 8;
        difference = -difference;
    }
    int delta = step >> 3;
    if (difference >= step) {
        code |= 4;
        difference -= step;
        delta += step;
    }
    if (difference >= step >> 1) {
        code |= 2;
        difference -= step >> 1;
        delta += step >> 1;
    }
    if (difference >= step >> 2) {
        code |= 1;
        delta += step >> 2;
    }
    *predictor = Clamp(*predictor + ((code & 8) ? -delta : delta), -32768, 32767);
    *index = Clamp(*index + kIndexTable[code], 0, 88);
    return code;
}

// PCM as mono Xbox ADPCM: each block the predictor and step index, then 64 codes in groups of eight, the low
// nibble first (src/common/sound/xadpcm.cpp's layout)
void EncodeXboxAdpcm(const std::vector<int16_t> &pcm, std::vector<uint8_t> *out) {
    int predictor = 0, index = 0;
    for (size_t block = 0; block * kAdpcmBlockSamples < pcm.size(); block++) {
        uint8_t header[4] = { uint8_t(predictor), uint8_t(predictor >> 8), uint8_t(index), 0 };
        out->insert(out->end(), header, header + 4);
        for (int group = 0; group < 8; group++) {
            uint32_t codes = 0;
            for (int n = 0; n < 8; n++) {
                size_t i = block * kAdpcmBlockSamples + group * 8 + n;
                codes |= EncodeNibble(i < pcm.size() ? pcm[i] : 0, &predictor, &index) << (n * 4);
            }
            uint8_t bytes[4] = { uint8_t(codes), uint8_t(codes >> 8), uint8_t(codes >> 16), uint8_t(codes >> 24) };
            out->insert(out->end(), bytes, bytes + 4);
        }
    }
}

struct Tag {
    uint8_t id;
    std::vector<uint8_t> value;   // empty for a bare tag
    bool bare;
};

uint32_t Value(const Tag &tag) {
    uint32_t v = 0;
    for (uint8_t b : tag.value)
        v = v << 8 | b;
    return v;
}

void PutTag(std::vector<uint8_t> *out, uint8_t id, uint32_t value, int bytes) {
    out->push_back(id);
    out->push_back(uint8_t(bytes));
    for (int i = bytes - 1; i >= 0; i--)
        out->push_back(uint8_t(value >> (i * 8)));
}

int SampleOffsetChannel(uint8_t id) {
    for (int ch = 0; ch < 6; ch++)
        if (kSampleOffsetTags[ch] == id)
            return ch;
    return -1;
}

}  // namespace

std::vector<int16_t> Gc_DecodeDsp(const uint8_t *frames, uint32_t bytes, uint32_t samples, const uint8_t *coefficients) {
    std::vector<int16_t> pcm;
    pcm.reserve(samples);
    int history1 = 0, history2 = 0;
    for (uint32_t frame = 0; pcm.size() < samples && (frame + 1) * kDspFrameBytes <= bytes; frame++) {
        const uint8_t *f = frames + frame * kDspFrameBytes;
        int predictor = f[0] >> 4 & 7, scale = 1 << (f[0] & 15);
        int c1 = int16_t(GcBe16(coefficients + predictor * 4)), c2 = int16_t(GcBe16(coefficients + predictor * 4 + 2));
        for (int i = 0; i < kDspFrameSamples && pcm.size() < samples; i++) {
            int nibble = (i & 1) ? f[1 + i / 2] & 15 : f[1 + i / 2] >> 4;
            if (nibble >= 8)
                nibble -= 16;
            int value = Clamp((nibble * scale * 2048 + 1024 + c1 * history1 + c2 * history2) >> 11, -32768, 32767);
            pcm.push_back(int16_t(value));
            history2 = history1;
            history1 = value;
        }
    }
    pcm.resize(samples, 0);
    return pcm;
}

bool Gc_ConvertBank(const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted) {
    if (size < kBankHeaderBytes || memcmp(file, "BNKb", 4) != 0)
        return false;
    uint32_t patches = GcBe16(file + 6);
    if (kBankHeaderBytes + patches * 4 > size)
        return false;
    std::vector<std::vector<uint8_t>> headers(patches);
    std::vector<uint8_t> samples;
    // The new samples' offsets are written once the headers' size is known: (header, place in it, sample offset)
    struct Fixup {
        uint32_t patch, at, sample;
    };
    std::vector<Fixup> fixups;
    for (uint32_t patch = 0; patch < patches; patch++) {
        uint32_t offset = GcBe32(file + kBankHeaderBytes + patch * 4);
        if (offset == 0)
            continue;
        uint32_t p = kBankHeaderBytes + patch * 4 + offset;
        if (p + 4 > size || file[p] != 'P' || file[p + 1] != 'T')
            continue;
        std::vector<uint8_t> &out = headers[patch];
        out.push_back('P');
        out.push_back('T');
        out.push_back(kXboxPlatform);
        out.push_back(file[p + 3]);
        p += 4;
        // Timbre by timbre: its tags, then (at the next timbre or the end) its samples
        std::vector<Tag> timbre;
        auto flush = [&]() {
            uint32_t channels = 1, frames = 0;
            uint32_t dataOffsets[6] = {};
            const uint8_t *coefficients[6] = {};
            int coefficientCount = 0;
            for (const Tag &t : timbre) {
                if (t.id == kTagChannels && !t.bare)
                    channels = Value(t);
                else if (t.id == kTagFrames && !t.bare)
                    frames = Value(t);
                else if (t.id == kTagDspCoefficients && t.value.size() >= 32 && coefficientCount < 6)
                    coefficients[coefficientCount++] = t.value.data();
                else if (SampleOffsetChannel(t.id) >= 0 && !t.bare)
                    dataOffsets[SampleOffsetChannel(t.id)] = Value(t);
            }
            channels = (std::min)(channels, 6u);
            bool hasRenderMode = false;
            for (const Tag &t : timbre) {
                if (t.bare) {
                    if (t.id != kTagPadding)
                        out.push_back(t.id);
                    continue;
                }
                int channel = SampleOffsetChannel(t.id);
                if (t.id == kTagDspCoefficients || t.id == kTagSampleData) {
                    continue;
                } else if (t.id == kTagSampleRep) {
                    PutTag(&out, kTagSampleRep, kRepXboxAdpcm, 1);
                } else if (channel >= 0 && uint32_t(channel) < channels && frames > 0) {
                    const uint8_t *coef = coefficients[channel] != NULL ? coefficients[channel] : coefficients[0];
                    uint32_t dspBytes = (frames + kDspFrameSamples - 1) / kDspFrameSamples * kDspFrameBytes;
                    uint32_t at = dataOffsets[channel];
                    while (samples.size() % 4 != 0)
                        samples.push_back(0);
                    uint32_t sampleOffset = uint32_t(samples.size());
                    if (coef != NULL && at < size) {
                        std::vector<int16_t> pcm = Gc_DecodeDsp(file + at, (std::min)(dspBytes, size - at), frames, coef);
                        EncodeXboxAdpcm(pcm, &samples);
                    } else {
                        samples.insert(samples.end(), (frames + kAdpcmBlockSamples - 1) / kAdpcmBlockSamples *
                                                          kAdpcmBlockBytes, 0);
                    }
                    fixups.push_back({ patch, uint32_t(out.size()) + 2, sampleOffset });
                    PutTag(&out, t.id, 0, 4);
                } else {
                    if (t.id == kTagRenderMode)
                        hasRenderMode = true;
                    out.push_back(t.id);
                    out.push_back(uint8_t(t.value.size()));
                    out.insert(out.end(), t.value.begin(), t.value.end());
                }
            }
            if (!hasRenderMode && frames > 0)
                PutTag(&out, kTagRenderMode, kXboxRenderMode, 2);
            timbre.clear();
        };
        while (p < size) {
            uint8_t id = file[p++];
            if (id == kTagEnd) {
                flush();
                out.push_back(kTagEnd);
                break;
            }
            if (id >= kTagPadding) {
                if (id == kTagNextTimbre)
                    flush();
                timbre.push_back({ id, {}, true });
                continue;
            }
            if (p >= size)
                break;
            uint32_t length = file[p++];
            if (p + length > size)
                break;
            timbre.push_back({ id, std::vector<uint8_t>(file + p, file + p + length), false });
            p += length;
        }
    }

    // The bank: its header and offset table, the PT headers (each on a 4-byte boundary), then the samples
    std::vector<uint8_t> &out = *converted;
    out.assign(kBankHeaderBytes + patches * 4, 0);
    std::vector<uint32_t> places(patches, 0);
    for (uint32_t patch = 0; patch < patches; patch++) {
        if (headers[patch].empty())
            continue;
        while (out.size() % 4 != 0)
            out.push_back(0);
        places[patch] = uint32_t(out.size());
        out.insert(out.end(), headers[patch].begin(), headers[patch].end());
    }
    while (out.size() % 16 != 0)
        out.push_back(0);
    uint32_t headerSize = uint32_t(out.size());
    for (const Fixup &f : fixups) {
        uint32_t value = headerSize + f.sample;
        uint8_t *v = &out[places[f.patch] + f.at];
        v[0] = uint8_t(value >> 24);
        v[1] = uint8_t(value >> 16);
        v[2] = uint8_t(value >> 8);
        v[3] = uint8_t(value);
    }
    out.insert(out.end(), samples.begin(), samples.end());
    memcpy(out.data(), "BNKl", 4);
    GcPutLe16(&out[4], 5);
    GcPutLe16(&out[6], uint16_t(patches));
    GcPutLe32(&out[8], headerSize);
    GcPutLe32(&out[12], uint32_t(samples.size()));
    GcPutLe32(&out[16], 0);
    for (uint32_t patch = 0; patch < patches; patch++) {
        uint32_t word = kBankHeaderBytes + patch * 4;
        GcPutLe32(&out[word], places[patch] != 0 ? places[patch] - word : 0);
    }
    return true;
}
