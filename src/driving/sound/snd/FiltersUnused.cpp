#include "FiltersUnused.h"

#include <stdint.h>
#include <string.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// The SFILTER unpackers and filters no data on the disc reaches (docs/driving/sound.md 3.8, 4.8): every bank
// sample is Xbox ADPCM on a hardware voice and every stream EA-XA on the mixer, so the bank EA-XA unpackers
// (xaf, xalf), MicroTalk (mtf, mtpf), PCM16 (f, lf, pf) and the time stretch (no patch has tag 0x98) never run.
// Ported from the listing like Filters.cpp - x87 in double in the original's order, float roundings at float
// stores, the x87 stack's values unrounded - and marked provisional: each warns once when it first runs.
// devtools/SndFilterShadow.cpp exercises the PCM16, bank EA-XA and time-stretch ones on synthetic data; MicroTalk
// has no encoder to make data with.
//
// The time stretch's four helpers take register arguments (sound.md 7.1): naked adaptors under Ghidra's names
// (AUTOLTCG) move them into calls to the C++ cores, which the ports call directly.
// ---------------------------------------------------------------------------------------------------------------

using namespace SND;

namespace {

inline float F32(uint32_t address) {
    return *(const float *)(uintptr_t)address;
}

inline double F64(uint32_t address) {
    return *(const double *)(uintptr_t)address;
}

const uint32_t kOne = 0x00189de8u;           // 1.0f
const uint32_t kZero = 0x00189decu;          // 0.0f
const uint32_t kMinusOne = 0x0018a134u;      // -1.0f
const uint32_t kRatioScale = 0x001a7300u;    // 1/4096
const uint32_t kRatioMin = 0x0018e9f8u;      // 0.5 (double)
const uint32_t kRatioMax = 0x001a7558u;      // 2.0 (double)

const uint32_t kUnpackXaf = 0x00145ed0u, kXapfRestore = 0x00145bf0u, kGetFrameXaf = 0x00145f40u,
               kUnpackXalf = 0x00145cc0u, kUnpackMtpf = 0x00145830u, kUnpackMtf = 0x001459c0u,
               kUnpackPf = 0x00146000u, kUnpackLf = 0x00146130u, kGetFrame = 0x001461e0u, kUnpackF = 0x001461f0u,
               kTimeStretch = 0x00144340u;

inline void CopyBits(void *to, const void *from) {
    memcpy(to, from, 4);
}

// REP MOVSD then REP MOVSB over 'bytes', forwards
inline void CopyRep(void *to, const void *from, uint32_t bytes) {
    uint32_t *d = (uint32_t *)to;
    const uint32_t *s = (const uint32_t *)from;
    for (uint32_t i = 0; i < (bytes >> 2); i++)
        *d++ = *s++;
    uint8_t *db = (uint8_t *)d;
    const uint8_t *sb = (const uint8_t *)s;
    for (uint32_t i = 0; i < (bytes & 3); i++)
        *db++ = *sb++;
}

inline int Pull(SFilterNode *node, int frames, float *a, float *b) {
    SFilterNode *up = node->input;
    return ((SFilterProcess)(uintptr_t)up->process)(up, frames, a, b, node->requester);
}

inline void Memclr(void *data, int bytes) {
    ((void (*)(void *, int))0x0013f600u)(data, bytes);
}

inline float *Advance(float *p, int32_t elements) {
    return (float *)(uintptr_t)((uint32_t)(uintptr_t)p + (uint32_t)elements * 4u);
}

inline const float *Advance(const float *p, int32_t elements) {
    return (const float *)(uintptr_t)((uint32_t)(uintptr_t)p + (uint32_t)elements * 4u);
}

inline uint8_t *At(void *node, uint32_t offset) {
    return (uint8_t *)node + offset;
}

inline uint32_t &U32At(void *node, uint32_t offset) {
    return *(uint32_t *)At(node, offset);
}

inline uint16_t &U16At(void *node, uint32_t offset) {
    return *(uint16_t *)At(node, offset);
}

// Module I's decoders and module D's packet player, at their addresses
inline int DecoderDecode(CEAXABLKDecf *decoder, float **out, int frames) {
    return ((int (__fastcall *)(CEAXABLKDecf *, int, float **, int))0x00149ec0u)(decoder, 0, out, frames);
}

inline int DecoderFeed(CEAXABLKDecf *decoder, const void *data, int bytes, int frames) {
    return ((int (__fastcall *)(CEAXABLKDecf *, int, const void *, int, int))0x00149e90u)(decoder, 0, data, bytes,
                                                                                          frames);
}

inline void DecoderSetState(CEAXABLKDecf *decoder, float *state) {
    ((void (__fastcall *)(CEAXABLKDecf *, int, float *))0x0014a1c0u)(decoder, 0, state);
}

inline float *DecoderGetState(CEAXABLKDecf *decoder, float *state) {
    return ((float *(__fastcall *)(CEAXABLKDecf *, int, float *))0x0014a190u)(decoder, 0, state);
}

inline CEAXABLKDecf *NewDecoder() {
    void *memory = ((void *(*)(uint32_t))0x00149e50u)(0xa8);   // SND::CEAXABLKDecf::operator new
    if (memory == NULL)
        return NULL;
    return ((CEAXABLKDecf *(__fastcall *)(void *, int))0x00149e70u)(memory, 0);
}

inline void InitMut(const void *data, void *state) {
    ((void (*)(const void *, void *))0x001493e0u)(data, state);
}

inline void DecodeMut(void *state) {
    ((void (*)(void *))0x00149500u)(state);
}

inline void Decode16(int count, const int16_t *src, float *dst) {
    ((void (*)(int, const int16_t *, float *))0x0014a1e0u)(count, src, dst);
}

inline int GetMasterVoice(int voice) {
    return ((int (*)(int))0x00142420u)(voice);
}

inline int VoiceToPacketHandle(int voice) {
    return ((int (*)(int))0x001457e0u)(voice);
}

inline int GetSampleChan(int voice) {
    return ((int (*)(int))0x00142460u)(voice);
}

inline void FreeFrames(int player, int channel, int frames) {
    ((void (*)(int, int, int))0x0013ef80u)(player, channel, frames);
}

inline void *GetPacket(int player, int channel, int *frames, int *other) {
    return ((void *(*)(int, int, int *, int *))0x0013ee00u)(player, channel, frames, other);
}

inline float *OutputBuffer(SFilterStretch *node) {
    return (float *)At(node, 0x838);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// EA-XA bank samples
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00145ed0)
int SFILTER_unpackxaf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackxaf");
    (void)scratch;
    (void)requester;
    SFilterXAF *n = (SFilterXAF *)node;
    if (n->position >= n->frames)
        return -1;
    int got = DecoderDecode(n->decoder, &out, frames);
    n->position += got;
    out = Advance(out, got);
    if (got < frames) {
        int rest = frames - got;
        Memclr(out, rest * 4);
        got += rest;
    }
    return got;
}

// The frame getter xafinit installs (PS2: SFILTER_unpackgetframexaf); never made a function (sound.md 7.3).
// FUNC_AT(0x00145f40)
int SFILTER_unpackgetframexaf(SND::SFilterXAF *node) {
    SND_UNTESTED("SFILTER_unpackgetframexaf");
    return node->position;
}

// FUNC_AT(0x00145f50)
void SFILTER_unpackxafinit(SND::SFilterXAF *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackxafinit");
    node->node.process = kUnpackXaf;
    node->node.restore = kXapfRestore;
    node->data = (const uint8_t *)info->data;
    node->frames = info->frames;
    node->position = 0;
    node->decoder = NewDecoder();
    info->getFrame = kGetFrameXaf;
    DecoderFeed(node->decoder, node->data, node->frames * 4, node->frames);
}

// The first call feeds the decoder up to the loop start's block; at the end of the data it feeds again from the
// loop start's block to the loop end, the first time saving the predictor state there (and keeping the samples
// before the loop start that block decodes), later restoring it and decoding those samples into a scratch buffer.
// FUNC_AT(0x00145cc0)
int SFILTER_unpackxalf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackxalf");
    (void)scratch;
    (void)requester;
    SFilterXALF *n = (SFilterXALF *)node;
    int total = 0;
    if (n->fed == 0) {
        int blocks = n->loopStart / 28 * 28;
        DecoderFeed(n->decoder, n->data, blocks * 4, blocks);
        n->fed = 1;
    }
    int remaining = frames;
    if (remaining == 0)
        return total;
    do {
        int got = DecoderDecode(n->decoder, &out, remaining);
        out = Advance(out, got);
        n->position += got;
        total += got;
        if (got < remaining) {
            remaining -= got;
            int start = n->loopStart;
            int into = start % 28;
            int count = n->loopEnd - start + into + 1;
            start -= into;
            n->position = start;
            DecoderFeed(n->decoder, n->data + start / 28 * 15, count * 4, count);
            if (n->stateSaved == 0) {
                float state[2];
                float *saved = DecoderGetState(n->decoder, state);
                CopyBits(&n->loopState[1], &saved[1]);
                CopyBits(&n->loopState[0], &saved[0]);
                got = DecoderDecode(n->decoder, &out, remaining);
                out = Advance(out, got);
                total += got;
                n->position += got;
                n->stateSaved = 1;
            } else {
                float state[2];
                CopyBits(&state[1], &n->loopState[1]);
                CopyBits(&state[0], &n->loopState[0]);
                DecoderSetState(n->decoder, state);
                float skipped[28];
                float *to = skipped;
                n->position += DecoderDecode(n->decoder, &to, into);
                continue;   // the original's jump past the subtraction
            }
        }
        remaining -= got;
    } while (remaining != 0);
    return total;
}

// FUNC_AT(0x00145e40)
void SFILTER_unpackxalfinit(SND::SFilterXALF *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackxalfinit");
    node->node.process = kUnpackXalf;
    node->node.restore = kXapfRestore;
    node->data = (const uint8_t *)info->data;
    node->frames = info->frames;
    node->position = 0;
    node->stateSaved = 0;
    node->fed = 0;
    node->loopStart = info->loopStart;
    node->loopEnd = info->loopEnd;
    node->decoder = NewDecoder();
    info->getFrame = kGetFrameXaf;
}

// ---------------------------------------------------------------------------------------------------------------
// MicroTalk. The packet variant's node: +0x1c packet, +0x20 its frames, +0x24 packet player, +0x28 frames to
// report, +0x2c (u16) frames taken from the packet, +0x2e (u16) samples left in the buffer, +0x30 decoder state,
// the 432-sample buffer ending at +0xd70, +0xd70 (u8) channel. The bank variant's: +0x1c frames, +0x20 position,
// +0x24 samples left, +0x28 decoder state, the buffer ending at +0xd68.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00145830)
int SFILTER_unpackmtpf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackmtpf");
    (void)scratch;
    (void)requester;
    if (U32At(node, 0x28) != 0) {
        FreeFrames((int)U32At(node, 0x24), *At(node, 0xd70), (int)U32At(node, 0x28));
        U32At(node, 0x28) = 0;
    }
    int remaining = frames;
    if (remaining <= 0)
        return (int)U32At(node, 0x28);
    do {
        if ((int)U16At(node, 0x2c) >= (int)U32At(node, 0x20)) {
            int other;
            const uint8_t *packet = (const uint8_t *)GetPacket((int)U32At(node, 0x24), *At(node, 0xd70),
                                                               (int *)At(node, 0x20), &other);
            U32At(node, 0x1c) = (uint32_t)(uintptr_t)packet;
            U16At(node, 0x2c) = 0;
            if (packet == NULL) {
                if (U32At(node, 0x28) != 0)
                    Memclr(out, remaining * 4);
                U32At(node, 0x20) = 0;
                return (int)U32At(node, 0x28);
            }
            if (packet[0] != 0) {
                U16At(node, 0x2e) = 0;
                InitMut(packet + 1, At(node, 0x30));
            } else {
                U32At(node, 0x34) = packet[1];
                U32At(node, 0x38) = 8;
                U32At(node, 0x30) = (uint32_t)(uintptr_t)(packet + 2);
            }
        }
        if (U16At(node, 0x2e) == 0) {
            DecodeMut(At(node, 0x30));
            U16At(node, 0x2e) = 0x1b0;
        }
        int count = (int)U32At(node, 0x20) - (int)U16At(node, 0x2c);
        if (remaining < count)
            count = remaining;
        int left = (int)U16At(node, 0x2e);
        if (left < count)
            count = left;
        CopyRep(out, At(node, (uint32_t)(0x35c - left) * 4u), (uint32_t)count * 4u);
        U16At(node, 0x2c) = (uint16_t)(U16At(node, 0x2c) + count);
        U16At(node, 0x2e) = (uint16_t)(U16At(node, 0x2e) - count);
        out = Advance(out, count);
        remaining -= count;
        U32At(node, 0x28) += (uint32_t)count;
    } while (remaining > 0);
    return (int)U32At(node, 0x28);
}

// FUNC_AT(0x00145970)
void SFILTER_unpackmtpfinit(SND::SFilterNode *node, const SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackmtpfinit");
    node->process = kUnpackMtpf;
    U32At(node, 0x24) = (uint32_t)VoiceToPacketHandle(GetMasterVoice(info->voice));
    *At(node, 0xd70) = (uint8_t)GetSampleChan(info->voice);
    U32At(node, 0x1c) = 0;
    U16At(node, 0x2c) = 0;
    U32At(node, 0x20) = 0;
    U16At(node, 0x2e) = 0;
    U32At(node, 0x28) = 0;
}

// FUNC_AT(0x001459c0)
int SFILTER_unpackmtf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackmtf");
    (void)scratch;
    (void)requester;
    uint32_t position = U32At(node, 0x20), total = U32At(node, 0x1c);
    if (!(position < total))
        return -1;
    int want = frames;
    position += (uint32_t)want;
    U32At(node, 0x20) = position;
    int over = (int)(position - total);
    if (over > 0)
        want -= over;
    else
        over = 0;
    int count = (int)U32At(node, 0x24);
    if (want > 0) {
        for (;;) {
            if (want < count)
                count = want;
            CopyRep(out, At(node, (uint32_t)(0x35a - (int)U32At(node, 0x24)) * 4u), (uint32_t)count * 4u);
            out = Advance(out, count);
            U32At(node, 0x24) -= (uint32_t)count;
            want -= count;
            if (want <= 0)
                break;
            DecodeMut(At(node, 0x28));
            count = 0x1b0;
            U32At(node, 0x24) = 0x1b0;
        }
    }
    if (over > 0) {
        uint32_t *o = (uint32_t *)out;
        for (uint32_t i = 0; i < (uint32_t)over; i++)
            o[i] = 0;
    }
    return 1;
}

// FUNC_AT(0x00145a70)
void SFILTER_unpackmtfinit(SND::SFilterNode *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackmtfinit");
    node->process = kUnpackMtf;
    U32At(node, 0x1c) = (uint32_t)info->frames;
    U32At(node, 0x20) = 0;
    info->getFrame = kGetFrame;
    InitMut(info->data, At(node, 0x28));
    U32At(node, 0x24) = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// PCM16
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00146000)
int SFILTER_unpackpf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackpf");
    (void)scratch;
    (void)requester;
    SFilterPF *n = (SFilterPF *)node;
    if (n->pending != 0) {
        FreeFrames(n->packetPlayer, n->channel, n->pending);
        n->pending = 0;
    }
    int remaining = frames;
    if (remaining <= 0)
        return n->pending;
    do {
        if (n->position >= n->packetFrames) {
            int packetFrames, other;
            const int16_t *packet = (const int16_t *)GetPacket(n->packetPlayer, n->channel, &packetFrames, &other);
            n->packet = packet;
            if (packet == NULL) {
                if (n->pending != 0)
                    Memclr(out, remaining * 4);
                return n->pending;
            }
            n->position = 0;
            n->packetFrames = packetFrames;
        }
        int at = n->position;
        int count = n->packetFrames - at;
        if (remaining < count)
            count = remaining;
        if (n->decode != 0)
            Decode16(count, n->packet + at, out);
        n->position += count;
        remaining -= count;
        n->pending += count;
        out = Advance(out, count);
    } while (remaining > 0);
    return n->pending;
}

// FUNC_AT(0x001460e0)
void SFILTER_unpackpfinit(SND::SFilterPF *node, const SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackpfinit");
    node->node.process = kUnpackPf;
    node->packetPlayer = VoiceToPacketHandle(GetMasterVoice(info->voice));
    node->channel = (uint8_t)GetSampleChan(info->voice);
    node->packet = NULL;
    node->packetFrames = -1;
    node->position = 0;
    node->decode = (uint8_t)info->flag;
    node->pending = 0;
}

// FUNC_AT(0x00146130)
int SFILTER_unpacklf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpacklf");
    (void)scratch;
    (void)requester;
    SFilterLF *n = (SFilterLF *)node;
    int remaining = frames;
    if (remaining > 0) {
        do {
            uint32_t at = n->position;
            int count = (int)(n->loopEnd - at + 1);
            if (remaining < count)
                count = remaining;
            if (n->decode != 0)
                Decode16(count, n->data + at, out);
            uint32_t next = n->position + (uint32_t)count;
            remaining -= count;
            n->position = next;
            out = Advance(out, count);
            if (next > n->loopEnd)
                n->position = n->loopStart;
        } while (remaining > 0);
    }
    return 1;
}

// FUNC_AT(0x001461a0)
void SFILTER_unpacklfinit(SND::SFilterLF *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpacklfinit");
    node->node.process = kUnpackLf;
    node->data = (const int16_t *)info->data;
    node->position = 0;
    node->loopStart = (uint32_t)info->loopStart;
    node->loopEnd = (uint32_t)info->loopEnd;
    node->decode = info->flag;
    info->getFrame = kGetFrame;
}

// One body for the PCM16 bank unpackers' and MicroTalk's frame getters (+0x20)
// FUNC_AT(0x001461e0)
int SFILTER_unpackfgetframe_unpacklfgetframe(SND::SFilterNode *node) {
    SND_UNTESTED("SFILTER_unpackfgetframe_unpacklfgetframe");
    return (int)U32At(node, 0x20);
}

// FUNC_AT(0x001461f0)
int SFILTER_unpackf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackf");
    (void)scratch;
    (void)requester;
    SFilterF *n = (SFilterF *)node;
    uint32_t at = n->position, end = n->frames;
    if (!(at < end))
        return -1;
    uint32_t next = at + (uint32_t)frames;
    n->position = next;
    if (next < end) {
        if (n->decode != 0)
            Decode16(frames, n->data + at, out);
        return 1;
    }
    int count = (int)(end - at);
    if (n->decode != 0)
        Decode16(count, n->data + at, out);
    Memclr(Advance(out, count), (frames - count) * 4);
    return 1;
}

// FUNC_AT(0x00146280)
void SFILTER_unpackfinit(SND::SFilterF *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackfinit");
    node->node.process = kUnpackF;
    node->data = (const int16_t *)info->data;
    node->position = 0;
    node->frames = (uint32_t)info->frames;
    node->decode = info->flag;
    info->getFrame = kGetFrame;
}

// ---------------------------------------------------------------------------------------------------------------
// The time stretch: input in windows, each next window crossfaded in at a shift the patch's stretch data and the
// accumulated timing error choose (a WSOLA-style stretch driven by precomputed offsets)
// ---------------------------------------------------------------------------------------------------------------

// The crossfade of a window: out[i - s] = (1 - g) a[i - s] + g a[i] for i = s..window-1, then
// out[window - s + i] = (1 - g) a[window - s + i] + g b[i] for i < s, s = |shift|; g runs from 1 down (shift > 0)
// or 0 up by 1/window, kept unrounded on the x87 stack.
void SndStretch_Crossfade(int shift, const float *a, const float *b, float *out, int window) {
    double g;
    float step;
    int s = shift;
    if (s > 0) {
        g = (double)F32(kOne);
        step = (float)((double)F32(kMinusOne) / (double)window);
    } else {
        g = (double)F32(kZero);
        s = -s;
        step = (float)((double)F32(kOne) / (double)window);
    }
    int first = window - s;
    for (int i = s; i < window; i++) {
        double v = ((double)F32(kOne) - g) * (double)a[i - s] + g * (double)a[i];
        out[i - s] = (float)v;
        g = g + (double)step;
    }
    for (int i = 0; i < s; i++) {
        double v = ((double)F32(kOne) - g) * (double)a[first + i] + g * (double)b[i];
        out[first + i] = (float)v;
        g = g + (double)step;
    }
}

// AUTOLTCG
__declspec(naked) void FUN_00143be0() {
    __asm {
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push eax
        call SndStretch_Crossfade
        add esp, 20
        ret
    }
}

// The next segment's shift from the data byte and the error: 0 (one byte consumed) when keeping the window as it
// is leaves the smaller error, else the byte's offset (+ or -, two bytes consumed when stretching below 1).
int SndStretch_NextShift(const uint8_t **data, float *error, int window, float ratio) {
    bool below = (double)ratio < (double)F32(kOne);
    double w = (double)window;
    double rw = (double)ratio * w;
    const uint8_t *p = *data;
    int c = window / 2 + p[0];
    float d = (float)((w + (double)*error) - rw);
    double e;
    if (below)
        e = ((double)(window * 2 - c) + (double)*error) - (rw + rw);
    else
        e = ((double)(c + window) + (double)*error) - rw;
    double ad = (double)d;
    if (ad < (double)F32(kZero))
        ad = -ad;
    double ae = e;
    if (ae < (double)F32(kZero))
        ae = -ae;
    if (ad < ae) {
        *data = p + 1;
        *error = d;
        return 0;
    }
    if (below) {
        *data = p + 2;
        *error = (float)e;
        return -c;
    }
    *data = p + 1;
    *error = (float)e;
    return c;
}

// AUTOLTCG
__declspec(naked) void FUN_00143e10() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push esi
        push ebx
        call SndStretch_NextShift
        add esp, 16
        ret
    }
}

// One segment of input into the output buffer at the shift: answers 0.
int SndStretch_Segment(SND::SFilterStretch *node, const float **in, int shift) {
    const float *a, *b;
    if (node->pending > 0) {
        a = node->held;
        b = *in;
    } else {
        a = *in;
        b = Advance(*in, node->window);
    }
    float *outBuffer = OutputBuffer(node);
    if (shift == 0) {
        CopyRep(outBuffer, a, (uint32_t)node->window * 4u);
        int window = node->window;
        node->remaining -= window;
        node->available = window;
        *in = b;
        node->pending = 0;
        node->readPosition = 0;
        return 0;
    }
    if (shift > 0) {
        CopyRep(outBuffer, a, (uint32_t)shift * 4u);
        SndStretch_Crossfade(shift, a, b, Advance(outBuffer, shift), node->window);
        int window = node->window;
        node->available = window + shift;
        node->remaining -= window;
        *in = b;
        node->pending = 0;
        node->readPosition = 0;
        return 0;
    }
    SndStretch_Crossfade(shift, a, b, outBuffer, node->window);
    int window = node->window;
    CopyRep(Advance(outBuffer, window), Advance(b, -shift), (uint32_t)(window + shift) * 4u);
    window = node->window;
    node->available = window * 2 + shift;
    node->remaining -= window * 2;
    *in = Advance(b, window);
    node->pending = 0;
    node->readPosition = 0;
    return 0;
}

// AUTOLTCG
__declspec(naked) void FUN_00143f20() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        call SndStretch_Segment
        add esp, 12
        ret
    }
}

// Up to *frames of what the output buffer holds, into *out; both advanced. Answers the frames taken.
int SndStretch_Drain(SND::SFilterStretch *node, int *frames, float **out) {
    const float *from = Advance((const float *)OutputBuffer(node), node->readPosition);
    int count = *frames;
    if (count > node->available)
        count = node->available;
    CopyRep(*out, from, (uint32_t)count * 4u);
    node->readPosition += count;
    node->available -= count;
    *frames -= count;
    *out = Advance(*out, count);
    return count;
}

// AUTOLTCG
__declspec(naked) void FUN_00144050() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edx
        call SndStretch_Drain
        add esp, 12
        ret
    }
}

// With no input left it passes the upstream node's answer for 200 frames (into 'scratch') straight back - the
// original's, not a typo here. Otherwise pulls the frames the stretch needs (unless the packet player has fewer
// outstanding: then nothing this time), stretches, and clears what it could not fill.
// FUNC_AT(0x00144340)
int SFILTER_timestretch(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_timestretch");
    (void)requester;
    SFilterStretch *n = (SFilterStretch *)node;
    if (n->remaining == 0)
        return Pull(node, 200, out, scratch);
    int need = stretchframesneeded(n, frames);
    if (n->packetPlayer >= 0) {
        if (need > ((int (*)(int))0x0013edc0u)(n->packetPlayer))   // SNDPKTPLAY_framesoutstanding
            return 0;
    }
    if (need > 0) {
        int got = Pull(node, need, out, scratch);
        if (got <= 0)
            return got;
    }
    int got = stretch(n, frames, scratch, out);
    if (got < frames)
        Memclr(Advance(out, got), (frames - got) * 4);
    return frames;
}

// blob: tag 0x98's data - byte 1 half the window, bytes 2..5 the input frames (big-endian), the shifts from 6
// FUNC_AT(0x001443f0)
int SFILTER_timestretchinit(SND::SFilterStretch *node, const uint8_t *blob, int voice) {
    SND_UNTESTED("SFILTER_timestretchinit");
    node->node.process = kTimeStretch;
    node->node.restore = 0;
    if (voice >= 0)
        node->packetPlayer = VoiceToPacketHandle(GetMasterVoice(voice));
    else
        node->packetPlayer = -1;
    node->data = blob + 6;
    uint32_t one = 0x3f800000u;
    CopyBits(&node->ratio, &one);
    memset(&node->error, 0, 4);
    node->window = blob[1] * 2;
    node->remaining = ((int (*)(const uint8_t *, int))0x00144a90u)(blob + 2, 4);   // SNDI_getb
    node->pending = 0;
    node->available = 0;
    node->readPosition = 0;
    return 0;
}

// ratio: 4.12 fixed point, clamped to 0.5..2
// FUNC_AT(0x00144300)
void SFILTER_timestretchsetratio(SND::SFilterStretch *node, int ratio) {
    SND_UNTESTED("SFILTER_timestretchsetratio");
    double f = (double)ratio * (double)F32(kRatioScale);
    node->ratio = (float)f;
    if (f < F64(kRatioMin)) {
        uint32_t half = 0x3f000000u;
        CopyBits(&node->ratio, &half);
        return;
    }
    if (f > F64(kRatioMax)) {
        uint32_t two = 0x40000000u;
        CopyBits(&node->ratio, &two);
    }
}

// FUNC_AT(0x001441a0)
int stretch(SND::SFilterStretch *node, int frames, float *in, float *out) {
    SND_UNTESTED("stretch");
    int remaining = frames;
    float *o = out;
    const float *input = in;
    int total = SndStretch_Drain(node, &remaining, &o);
    if (remaining == 0)
        return total;
    int shift = 0;
    if (remaining <= 0)
        return total;
    for (;;) {
        if (node->remaining < node->window)
            break;
        if (node->remaining < node->window * 2)
            shift = 0;
        else
            shift = SndStretch_NextShift(&node->data, &node->error, node->window, node->ratio);
        SndStretch_Segment(node, &input, shift);
        const float *from = Advance((const float *)OutputBuffer(node), node->readPosition);
        int count = node->available;
        if (!(remaining > count))
            count = remaining;
        CopyRep(o, from, (uint32_t)count * 4u);
        node->readPosition += count;
        o = Advance(o, count);
        node->available -= count;
        total += count;
        remaining -= count;
        if (remaining <= 0)
            break;
    }
    if (shift > 0) {
        CopyRep(node->held, input, (uint32_t)node->window * 4u);
        node->pending = node->window;
        input = Advance(input, node->window);
    }
    if (remaining > 0) {
        int count = node->remaining;
        if (!(remaining > count))
            count = remaining;
        CopyRep(o, input, (uint32_t)count * 4u);
        node->remaining -= count;
        total += count;
    }
    return total;
}

// The input frames the next 'frames' of output will take, beyond what is held: the stretch's walk run ahead on
// copies of the data pointer and the error.
// FUNC_AT(0x001440b0)
int stretchframesneeded(SND::SFilterStretch *node, int frames) {
    SND_UNTESTED("stretchframesneeded");
    if (frames <= node->available)
        return 0;
    int want = frames - node->available;
    const uint8_t *data = node->data;
    float error;
    CopyBits(&error, &node->error);
    int remaining = node->remaining;
    int total = 0;
    int shift = 0;
    if (want > 0) {
        int window = node->window;
        for (;;) {
            if (remaining < window)
                break;
            int consumed, produced;
            if (remaining < window * 2) {
                shift = 0;
                consumed = window;
                produced = window;
            } else {
                shift = SndStretch_NextShift(&data, &error, window, node->ratio);
                if (shift >= 0) {
                    consumed = window;
                    produced = window + shift;
                } else {
                    consumed = window * 2;
                    produced = shift + window * 2;
                }
            }
            total += consumed;
            remaining -= consumed;
            if (want <= produced) {
                want = 0;
                break;
            }
            want -= produced;
            if (want <= 0)
                break;
        }
        if (shift > 0)
            total += window;
        if (want > 0) {
            if (want > remaining)
                total += remaining;
            else
                total += want;
        }
    }
    if (total <= node->pending)
        return 0;
    return total - node->pending;
}
