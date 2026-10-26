#include "GcSound.h"
#include "GcBytes.h"
#include "../sound/PcmBanks.h"

#include <string.h>

// ---- DSP ADPCM: frames of 8 bytes, a header byte (the predictor's pair, the scale's shift) and 14 4-bit codes

#define DSP_FRAME_BYTES 8
#define DSP_FRAME_SAMPLES 14
#define DSP_COEFFICIENTS 0x1c       // sixteen signed halfwords: eight predictor pairs
#define DSP_LOOP_FLAG 0x0c
#define DSP_LOOP_START 0x10         // a nibble address: two per frame are its header
#define DSP_HISTORY 0x40            // the first frame's two previous samples
#define DSP_NIBBLES_PER_FRAME 16

static int16_t Clamp16(int v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

bool GcSound_DecodeDsp(const uint8_t *dspHeader, const uint8_t *frames, uint32_t frameBytes, GcSoundSample *out) {
    uint32_t samples = GcBe32(dspHeader);
    out->frequency = (int)GcBe32(dspHeader + 8);
    out->loops = GcBe16(dspHeader + DSP_LOOP_FLAG) != 0;
    uint32_t loopNibble = GcBe32(dspHeader + DSP_LOOP_START);
    out->loopStart = out->loops ? (int)(loopNibble / DSP_NIBBLES_PER_FRAME * DSP_FRAME_SAMPLES +
                                        (loopNibble % DSP_NIBBLES_PER_FRAME) - 2) : -1;
    if (samples == 0 || (uint64_t)(samples + DSP_FRAME_SAMPLES - 1) / DSP_FRAME_SAMPLES * DSP_FRAME_BYTES > frameBytes)
        return false;
    int coefficients[16];
    for (int i = 0; i < 16; i++)
        coefficients[i] = (int16_t)GcBe16(dspHeader + DSP_COEFFICIENTS + i * 2);
    int history1 = (int16_t)GcBe16(dspHeader + DSP_HISTORY), history2 = (int16_t)GcBe16(dspHeader + DSP_HISTORY + 2);

    out->pcm.resize(samples);
    for (uint32_t s = 0; s < samples; s++) {
        const uint8_t *frame = frames + s / DSP_FRAME_SAMPLES * DSP_FRAME_BYTES;
        int k = (int)(s % DSP_FRAME_SAMPLES);
        int predictor = (frame[0] >> 4) & 7, scale = 1 << (frame[0] & 0xf);
        uint8_t byte = frame[1 + k / 2];
        int code = (k & 1) ? byte & 0xf : byte >> 4;
        if (code >= 8)
            code -= 16;
        int sample = ((code * scale) << 11) + 1024 + coefficients[predictor * 2] * history1 +
                     coefficients[predictor * 2 + 1] * history2;
        int16_t pcm = Clamp16(sample >> 11);
        out->pcm[s] = pcm;
        history2 = history1;
        history1 = pcm;
    }
    return true;
}

// ---- Banks

// A .SHF sample header's words (big-endian on the GameCube's disc, little on the Xbox's)
enum {
    SHF_FLAGS, SHF_ADDRESS, SHF_SIZE, SHF_FREQUENCY, SHF_REAL_SIZE, SHF_CHANNELS, SHF_BITS, SHF_PSI_HEADER,
    SHF_LOOP_OFFSET, SHF_WORDS
};
#define SHF_LOOPS 1                 // Flags bit 0

bool GcSound_DecodeBank(const uint8_t *shf, uint32_t shfBytes, const uint8_t *shg, uint32_t shgBytes,
                        const uint8_t *sbf, uint32_t sbfBytes, std::vector<GcSoundSample> &samples) {
    samples.clear();
    if (shfBytes < 4)
        return false;
    uint32_t count = GcBe32(shf);
    if (4 + (uint64_t)count * SHF_WORDS * 4 > shfBytes)
        return false;
    samples.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *h = shf + 4 + i * SHF_WORDS * 4;
        uint32_t address = GcBe32(h + SHF_ADDRESS * 4), size = GcBe32(h + SHF_SIZE * 4);
        uint32_t dsp = GcBe32(h + SHF_PSI_HEADER * 4);
        GcSoundSample &s = samples[i];
        if ((uint64_t)address + size > sbfBytes || (uint64_t)dsp + GC_DSP_HEADER_BYTES > shgBytes ||
            !GcSound_DecodeDsp(shg + dsp, sbf + address, size, &s)) {
            s.pcm.assign(1, 0);
            s.loopStart = -1;
            s.loops = false;
        }
        s.frequency = (int)GcBe32(h + SHF_FREQUENCY * 4);
        s.loops = (GcBe32(h + SHF_FLAGS * 4) & SHF_LOOPS) != 0 && s.loopStart >= 0;
    }
    return true;
}

#define XADPCM_BLOCK_BYTES 36       // one channel's block: a 4-byte header and 64 codes
#define XADPCM_BLOCK_SAMPLES 64
#define XADPCM_BITS 4

void GcSound_XboxBank(const std::vector<GcSoundSample> &samples, int bank, std::vector<uint8_t> &shf,
                      std::vector<uint8_t> &sbf) {
    shf.assign(4 + samples.size() * SHF_WORDS * 4, 0);
    GcPutLe32(shf.data(), (uint32_t)samples.size());
    sbf.clear();
    for (size_t i = 0; i < samples.size(); i++) {
        const GcSoundSample &s = samples[i];
        uint32_t blocks = ((uint32_t)s.pcm.size() + XADPCM_BLOCK_SAMPLES - 1) / XADPCM_BLOCK_SAMPLES;
        uint32_t size = blocks * XADPCM_BLOCK_BYTES;
        uint8_t *h = &shf[4 + i * SHF_WORDS * 4];
        GcPutLe32(h + SHF_FLAGS * 4, s.loops ? SHF_LOOPS : 0);
        GcPutLe32(h + SHF_SIZE * 4, size);
        GcPutLe32(h + SHF_FREQUENCY * 4, (uint32_t)s.frequency);
        GcPutLe32(h + SHF_REAL_SIZE * 4, size);
        GcPutLe32(h + SHF_CHANNELS * 4, 1);
        GcPutLe32(h + SHF_BITS * 4, XADPCM_BITS);
        // The loop's start, in the stand-in's bytes: the block it falls in
        uint32_t loopBlock = s.loops ? (uint32_t)s.loopStart / XADPCM_BLOCK_SAMPLES : 0;
        GcPutLe32(h + SHF_LOOP_OFFSET * 4, loopBlock * XADPCM_BLOCK_BYTES);
        size_t at = sbf.size();
        sbf.resize(at + size);
        PcmBanks_WriteStandIn(&sbf[at], size, bank, (int)i);
    }
}

// ---- Sounds: a count, then (hashcode, offset) pairs, then at each offset an SFXParameters and its sample pool

#define SFX_PARAMETERS_BYTES 0x44
#define SFX_SAMPLE_COUNT 0x40
#define SFX_POOL_ENTRY_BYTES 0x1c

void GcSound_ConvertSfx(uint8_t *file, uint32_t bytes) {
    if (bytes < 4)
        return;
    uint32_t count = GcBe32(file);
    if (4 + (uint64_t)count * 8 > bytes)
        return;
    std::vector<uint32_t> offsets(count);
    for (uint32_t i = 0; i < count; i++)
        offsets[i] = GcBe32(file + 4 + i * 8 + 4);
    GcSwapWords(file, 4 + count * 8);
    // SFXParameters (SFX.h): words but for the halfwords at 0x1c-0x23 and 0x34-0x3d and the bytes at 0x3e-0x3f
    static const uint8_t kHalfwords[] = { 0x1c, 0x1e, 0x20, 0x22, 0x34, 0x36, 0x38, 0x3a, 0x3c };
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *p = file + offsets[i];             // from the file's start (SFX.cpp's SFXBuffer)
        if ((uint64_t)offsets[i] + SFX_PARAMETERS_BYTES > bytes)
            continue;
        uint32_t samples = GcBe32(p + SFX_SAMPLE_COUNT);
        for (uint32_t o = 0; o < SFX_PARAMETERS_BYTES; o += 4)
            if (o < 0x1c || (o >= 0x24 && o < 0x34) || o == SFX_SAMPLE_COUNT)
                GcSwap32(p + o);
        for (uint8_t o : kHalfwords)
            GcSwap16(p + o);
        if ((uint64_t)offsets[i] + SFX_PARAMETERS_BYTES + (uint64_t)samples * SFX_POOL_ENTRY_BYTES > bytes)
            continue;
        GcSwapWords(p + SFX_PARAMETERS_BYTES, samples * SFX_POOL_ENTRY_BYTES);
    }
}

// ---- Music

static const int16_t kImaSteps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
};
static const int8_t kImaIndexSteps[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

struct ImaState {
    int predictor;
    int index;
};

static void ImaDecode(int code, ImaState *s) {
    int step = kImaSteps[s->index];
    int diff = step >> 3;
    if (code & 1) diff += step >> 2;
    if (code & 2) diff += step >> 1;
    if (code & 4) diff += step;
    if (code & 8) diff = -diff;
    s->predictor = Clamp16(s->predictor + diff);
    s->index += kImaIndexSteps[code & 7];
    s->index = s->index < 0 ? 0 : s->index > 88 ? 88 : s->index;
}

#define MUSIC_CHANNELS 2
#define MUSIC_BLOCK_BYTES (XADPCM_BLOCK_BYTES * MUSIC_CHANNELS)
#define MUSIC_BLOCK_CODE_BYTES 32   // a channel's 64 codes
#define MUSIC_GROUP_BYTES 4         // the Xbox's blocks interleave the channels four bytes at a time

void GcSound_XboxMusic(const uint8_t *ssd, uint32_t bytes, std::vector<uint8_t> &out) {
    // The channels' bytes alternate: byte 2n + c is channel c's samples 2n and 2n + 1
    uint32_t perChannel = bytes / MUSIC_CHANNELS;
    uint32_t blocks = (perChannel + MUSIC_BLOCK_CODE_BYTES - 1) / MUSIC_BLOCK_CODE_BYTES;
    out.assign((size_t)blocks * MUSIC_BLOCK_BYTES, 0);
    ImaState state[MUSIC_CHANNELS] = {};
    for (uint32_t b = 0; b < blocks; b++) {
        uint8_t *block = &out[(size_t)b * MUSIC_BLOCK_BYTES];
        for (int c = 0; c < MUSIC_CHANNELS; c++) {
            block[c * 4] = (uint8_t)state[c].predictor;
            block[c * 4 + 1] = (uint8_t)(state[c].predictor >> 8);
            block[c * 4 + 2] = (uint8_t)state[c].index;
        }
        for (uint32_t k = 0; k < MUSIC_BLOCK_CODE_BYTES; k++) {
            for (int c = 0; c < MUSIC_CHANNELS; c++) {
                uint32_t at = ((b * MUSIC_BLOCK_CODE_BYTES + k) * MUSIC_CHANNELS) + (uint32_t)c;
                uint8_t codes = at < bytes ? ssd[at] : 0;
                block[MUSIC_CHANNELS * 4 + (k / MUSIC_GROUP_BYTES) * MUSIC_GROUP_BYTES * MUSIC_CHANNELS +
                      c * MUSIC_GROUP_BYTES + k % MUSIC_GROUP_BYTES] = codes;
                ImaDecode(codes & 0xf, &state[c]);
                ImaDecode(codes >> 4, &state[c]);
            }
        }
    }
}

// A marker's position: the GameCube's counts four to a byte of its track, a byte being two samples of one channel;
// the Xbox's track takes 72 bytes for 64 samples of both
static int MusicPosition(int gcPosition) {
    if (gcPosition < 0)
        return gcPosition;
    uint64_t samples = (uint32_t)gcPosition / 4;
    return (int)(samples * MUSIC_BLOCK_BYTES / XADPCM_BLOCK_SAMPLES);
}

#define SMF_HEADER_WORDS 5          // StreamMarkerHeaderData
#define SMF_MARKER_WORDS 8          // MusicMarkerData: Pos at 1, LoopStart at 5
#define SMF_START_WORDS 13          // MusicMarkerStartData: a marker, then five words

void GcSound_ConvertMarkers(uint8_t *smf, uint32_t bytes) {
    GcSwapWords(smf, bytes);
    if (bytes < SMF_HEADER_WORDS * 4)
        return;
    int32_t starts = (int32_t)GcLe32(smf), markers = (int32_t)GcLe32(smf + 4);
    uint32_t startOffset = GcLe32(smf + 8), markerOffset = GcLe32(smf + 12);
    auto fix = [&](uint32_t at) {
        if (at + 4 <= bytes)
            GcPutLe32(smf + at, (uint32_t)MusicPosition((int32_t)GcLe32(smf + at)));
    };
    for (int32_t i = 0; i < markers; i++) {
        fix(markerOffset + i * SMF_MARKER_WORDS * 4 + 4);
        fix(markerOffset + i * SMF_MARKER_WORDS * 4 + 20);
    }
    for (int32_t i = 0; i < starts; i++) {
        fix(startOffset + i * SMF_START_WORDS * 4 + 4);
        fix(startOffset + i * SMF_START_WORDS * 4 + 20);
    }
}
