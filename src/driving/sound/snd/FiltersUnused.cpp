#include "FiltersUnused.h"
#include "Banks.h"
#include "Decode.h"
#include "SndUntested.h"

#include <stddef.h>
#include <stdint.h>

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
// (AUTOLTCG) move them into calls to the C++ cores, which the ports call directly. The decoders (module I) and
// SNDI_getb are called directly too; the packet player and the driver through the originals' addresses, where the
// shadow test puts its fakes.
// ---------------------------------------------------------------------------------------------------------------

using namespace SND;

namespace {

constexpr float kRatioScale = 1.0f / 4096;   // the time stretch's 4.12 ratio

// The process and frame-getter functions a node stores: the originals' addresses (which jump to the ports here),
// so a node's bytes are the original's
#define UnpackXafAt ((SFilterProcess)0x00145ed0)          // SFILTER_unpackxaf
#define XapfRestoreAt ((SFilterRestore)0x00145bf0)        // SFILTER_unpackxapfrestore
#define GetFrameXafAt ((SFilterGetFrame)0x00145f40)       // SFILTER_unpackgetframexaf
#define UnpackXalfAt ((SFilterProcess)0x00145cc0)         // SFILTER_unpackxalf
#define UnpackMtpfAt ((SFilterProcess)0x00145830)         // SFILTER_unpackmtpf
#define UnpackMtfAt ((SFilterProcess)0x001459c0)          // SFILTER_unpackmtf
#define UnpackPfAt ((SFilterProcess)0x00146000)           // SFILTER_unpackpf
#define UnpackLfAt ((SFilterProcess)0x00146130)           // SFILTER_unpacklf
#define GetFrameAt ((SFilterGetFrame)0x001461e0)          // SFILTER_unpackfgetframe_unpacklfgetframe
#define UnpackFAt ((SFilterProcess)0x001461f0)            // SFILTER_unpackf
#define TimeStretchAt ((SFilterProcess)0x00144340)        // SFILTER_timestretch

// ---- the originals called from here (other modules). Through the originals' addresses, which jump to the ports
// in game: the shadow test puts its recording fakes there (devtools/SndFilterShadow.cpp).
#define MemClear ((void (*)(void *, int))0x0013f600)                            // memclr
#define GetMasterVoice ((int (*)(int))0x00142420)                               // SNDDRV_getmastervoice
#define GetSampleChan ((int (*)(int))0x00142460)                                // SNDDRV_getsamplechan
#define VoiceToPacketHandle ((int (*)(int))0x001457e0)                          // SNDPKTPLAYI_voicetopackethandle
#define FreeFrames ((void (*)(int, int, int))0x0013ef80)                        // SNDPKTPLAYI_freeframes
#define GetPacket ((const void *(*)(int player, int channel, int *frames, int *other))0x0013ee00)   // SNDPKTPLAYI_get
#define FramesOutstanding ((int (*)(int))0x0013edc0)                            // SNDPKTPLAY_framesoutstanding

// The original's REP MOVSD: count floats, front to back (a float copy keeps the bits)
void CopyForward(float *to, const float *from, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        to[i] = from[i];
}

int Pull(SFilterNode *node, int frames, float *a, float *b) {
    SFilterNode *up = node->input;
    return up->process(up, frames, a, b, node->requester);
}

SND::CEAXABLKDecf *NewDecoder() {
    void *memory = SND::CEAXABLKDecf::operator new(sizeof(SND::CEAXABLKDecf));
    if (memory == NULL)
        return NULL;
    return static_cast<SND::CEAXABLKDecf *>(memory)->Construct();
}

// The time stretch's output buffer: the floats after the node
float *OutputBuffer(SFilterStretch *node) {
    return reinterpret_cast<float *>(node + 1);
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
    SFilterXAF *n = reinterpret_cast<SFilterXAF *>(node);
    if (n->position >= n->frames)
        return -1;
    int got = n->decoder->Decode(&out, frames);
    n->position += got;
    out += got;
    if (got < frames) {
        int rest = frames - got;
        MemClear(out, rest * 4);
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
    node->node.process = UnpackXafAt;
    node->node.restore = XapfRestoreAt;
    node->data = static_cast<const uint8_t *>(info->data);
    node->frames = info->frames;
    node->position = 0;
    node->decoder = NewDecoder();
    info->getFrame = GetFrameXafAt;
    node->decoder->Feed(node->data, node->frames * 4, node->frames);
}

// The first call feeds the decoder up to the loop start's block; at the end of the data it feeds again from the
// loop start's block to the loop end, the first time saving the predictor state there (and keeping the samples
// before the loop start that block decodes), later restoring it and decoding those samples into a scratch buffer.
// FUNC_AT(0x00145cc0)
int SFILTER_unpackxalf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackxalf");
    (void)scratch;
    (void)requester;
    SFilterXALF *n = reinterpret_cast<SFilterXALF *>(node);
    int total = 0;
    if (n->fed == 0) {
        int blocks = n->loopStart / 28 * 28;
        n->decoder->Feed(n->data, blocks * 4, blocks);
        n->fed = 1;
    }
    int remaining = frames;
    if (remaining == 0)
        return total;
    do {
        int got = n->decoder->Decode(&out, remaining);
        out += got;
        n->position += got;
        total += got;
        if (got < remaining) {
            remaining -= got;
            int start = n->loopStart;
            int into = start % 28;
            int count = n->loopEnd - start + into + 1;
            start -= into;
            n->position = start;
            n->decoder->Feed(n->data + start / 28 * 15, count * 4, count);
            if (n->stateSaved == 0) {
                float state[2];
                const float *saved = static_cast<const float *>(n->decoder->GetState(state));
                n->loopState[1] = saved[1];
                n->loopState[0] = saved[0];
                got = n->decoder->Decode(&out, remaining);
                out += got;
                total += got;
                n->position += got;
                n->stateSaved = 1;
            } else {
                float state[2];
                state[1] = n->loopState[1];
                state[0] = n->loopState[0];
                n->decoder->SetState(state);
                float skipped[28];
                float *to = skipped;
                n->position += n->decoder->Decode(&to, into);
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
    node->node.process = UnpackXalfAt;
    node->node.restore = XapfRestoreAt;
    node->data = static_cast<const uint8_t *>(info->data);
    node->frames = info->frames;
    node->position = 0;
    node->stateSaved = 0;
    node->fed = 0;
    node->loopStart = info->loopStart;
    node->loopEnd = info->loopEnd;
    node->decoder = NewDecoder();
    info->getFrame = GetFrameXafAt;
}

// ---------------------------------------------------------------------------------------------------------------
// MicroTalk (SFilterMTPF, SFilterMTF): each hands out the decoder's 432-sample frame, the last 432 floats of
// MutState::signal, a part at a time, decoding the next frame when it runs out.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00145830)
int SFILTER_unpackmtpf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackmtpf");
    (void)scratch;
    (void)requester;
    SFilterMTPF *n = reinterpret_cast<SFilterMTPF *>(node);
    if (n->pending != 0) {
        FreeFrames(n->packetPlayer, n->channel, n->pending);
        n->pending = 0;
    }
    int remaining = frames;
    if (remaining <= 0)
        return n->pending;
    do {
        if (n->taken >= n->packetFrames) {
            int other;
            const uint8_t *packet =
                static_cast<const uint8_t *>(GetPacket(n->packetPlayer, n->channel, &n->packetFrames, &other));
            n->packet = packet;
            n->taken = 0;
            if (packet == NULL) {
                if (n->pending != 0)
                    MemClear(out, remaining * 4);
                n->packetFrames = 0;
                return n->pending;
            }
            if (packet[0] != 0) {
                n->left = 0;
                initmut(packet + 1, &n->mut);
            } else {
                // no header: the bit reader starts on the packet's second byte
                n->mut.bits = packet[1];
                n->mut.count = 8;
                n->mut.ptr = packet + 2;
            }
        }
        if (n->left == 0) {
            decodemut(&n->mut);
            n->left = 432;
        }
        int count = n->packetFrames - n->taken;
        if (remaining < count)
            count = remaining;
        int left = n->left;
        if (left < count)
            count = left;
        CopyForward(out, &n->mut.signal[756 - left], uint32_t(count));
        n->taken = uint16_t(n->taken + count);
        n->left = uint16_t(n->left - count);
        out += count;
        remaining -= count;
        n->pending += count;
    } while (remaining > 0);
    return n->pending;
}

// FUNC_AT(0x00145970)
void SFILTER_unpackmtpfinit(SND::SFilterNode *node, const SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackmtpfinit");
    SFilterMTPF *n = reinterpret_cast<SFilterMTPF *>(node);
    node->process = UnpackMtpfAt;
    n->packetPlayer = VoiceToPacketHandle(GetMasterVoice(info->voice));
    n->channel = uint8_t(GetSampleChan(info->voice));
    n->packet = NULL;
    n->taken = 0;
    n->packetFrames = 0;
    n->left = 0;
    n->pending = 0;
}

// FUNC_AT(0x001459c0)
int SFILTER_unpackmtf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackmtf");
    (void)scratch;
    (void)requester;
    SFilterMTF *n = reinterpret_cast<SFilterMTF *>(node);
    uint32_t position = n->position, total = n->frames;
    if (!(position < total))
        return -1;
    int want = frames;
    position += uint32_t(want);
    n->position = position;
    int over = int(position - total);
    if (over > 0)
        want -= over;
    else
        over = 0;
    int count = n->left;
    if (want > 0) {
        for (;;) {
            if (want < count)
                count = want;
            CopyForward(out, &n->mut.signal[756 - n->left], uint32_t(count));
            out += count;
            n->left -= count;
            want -= count;
            if (want <= 0)
                break;
            decodemut(&n->mut);
            count = 432;
            n->left = 432;
        }
    }
    if (over > 0) {
        for (uint32_t i = 0; i < uint32_t(over); i++)
            out[i] = 0.0f;
    }
    return 1;
}

// FUNC_AT(0x00145a70)
void SFILTER_unpackmtfinit(SND::SFilterNode *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackmtfinit");
    SFilterMTF *n = reinterpret_cast<SFilterMTF *>(node);
    node->process = UnpackMtfAt;
    n->frames = uint32_t(info->frames);
    n->position = 0;
    info->getFrame = GetFrameAt;
    initmut(static_cast<const uint8_t *>(info->data), &n->mut);
    n->left = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// PCM16
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00146000)
int SFILTER_unpackpf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackpf");
    (void)scratch;
    (void)requester;
    SFilterPF *n = reinterpret_cast<SFilterPF *>(node);
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
            const int16_t *packet =
                static_cast<const int16_t *>(GetPacket(n->packetPlayer, n->channel, &packetFrames, &other));
            n->packet = packet;
            if (packet == NULL) {
                if (n->pending != 0)
                    MemClear(out, remaining * 4);
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
            decode16x87(count, n->packet + at, out);
        n->position += count;
        remaining -= count;
        n->pending += count;
        out += count;
    } while (remaining > 0);
    return n->pending;
}

// FUNC_AT(0x001460e0)
void SFILTER_unpackpfinit(SND::SFilterPF *node, const SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackpfinit");
    node->node.process = UnpackPfAt;
    node->packetPlayer = VoiceToPacketHandle(GetMasterVoice(info->voice));
    node->channel = uint8_t(GetSampleChan(info->voice));
    node->packet = NULL;
    node->packetFrames = -1;
    node->position = 0;
    node->decode = uint8_t(info->flag);
    node->pending = 0;
}

// FUNC_AT(0x00146130)
int SFILTER_unpacklf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpacklf");
    (void)scratch;
    (void)requester;
    SFilterLF *n = reinterpret_cast<SFilterLF *>(node);
    int remaining = frames;
    if (remaining > 0) {
        do {
            uint32_t at = n->position;
            int count = int(n->loopEnd - at + 1);
            if (remaining < count)
                count = remaining;
            if (n->decode != 0)
                decode16x87(count, n->data + at, out);
            uint32_t next = n->position + uint32_t(count);
            remaining -= count;
            n->position = next;
            out += count;
            if (next > n->loopEnd)
                n->position = n->loopStart;
        } while (remaining > 0);
    }
    return 1;
}

// FUNC_AT(0x001461a0)
void SFILTER_unpacklfinit(SND::SFilterLF *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpacklfinit");
    node->node.process = UnpackLfAt;
    node->data = static_cast<const int16_t *>(info->data);
    node->position = 0;
    node->loopStart = uint32_t(info->loopStart);
    node->loopEnd = uint32_t(info->loopEnd);
    node->decode = info->flag;
    info->getFrame = GetFrameAt;
}

// One body for the PCM16 bank unpackers' and MicroTalk's frame getters: the position, at +0x20 in all three
// FUNC_AT(0x001461e0)
int SFILTER_unpackfgetframe_unpacklfgetframe(SND::SFilterNode *node) {
    SND_UNTESTED("SFILTER_unpackfgetframe_unpacklfgetframe");
    return int(reinterpret_cast<SFilterF *>(node)->position);
}

// FUNC_AT(0x001461f0)
int SFILTER_unpackf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    SND_UNTESTED("SFILTER_unpackf");
    (void)scratch;
    (void)requester;
    SFilterF *n = reinterpret_cast<SFilterF *>(node);
    uint32_t at = n->position, end = n->frames;
    if (!(at < end))
        return -1;
    uint32_t next = at + uint32_t(frames);
    n->position = next;
    if (next < end) {
        if (n->decode != 0)
            decode16x87(frames, n->data + at, out);
        return 1;
    }
    int count = int(end - at);
    if (n->decode != 0)
        decode16x87(count, n->data + at, out);
    MemClear(out + count, (frames - count) * 4);
    return 1;
}

// FUNC_AT(0x00146280)
void SFILTER_unpackfinit(SND::SFilterF *node, SND::UnpackInfo *info) {
    SND_UNTESTED("SFILTER_unpackfinit");
    node->node.process = UnpackFAt;
    node->data = static_cast<const int16_t *>(info->data);
    node->position = 0;
    node->frames = uint32_t(info->frames);
    node->decode = info->flag;
    info->getFrame = GetFrameAt;
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
        g = 1.0;
        step = float(-1.0 / window);
    } else {
        g = 0.0;
        s = -s;
        step = float(1.0 / window);
    }
    int first = window - s;
    for (int i = s; i < window; i++) {
        double v = (1.0 - g) * a[i - s] + g * a[i];
        out[i - s] = float(v);
        g = g + step;
    }
    for (int i = 0; i < s; i++) {
        double v = (1.0 - g) * a[first + i] + g * b[i];
        out[first + i] = float(v);
        g = g + step;
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
    bool below = ratio < 1.0f;
    double w = window;
    double rw = ratio * w;
    const uint8_t *p = *data;
    int c = window / 2 + p[0];
    float d = float((w + *error) - rw);
    double e;
    if (below)
        e = (double(window * 2 - c) + *error) - (rw + rw);
    else
        e = (double(c + window) + *error) - rw;
    double ad = d;
    if (ad < 0.0)
        ad = -ad;
    double ae = e;
    if (ae < 0.0)
        ae = -ae;
    if (ad < ae) {
        *data = p + 1;
        *error = d;
        return 0;
    }
    if (below) {
        *data = p + 2;
        *error = float(e);
        return -c;
    }
    *data = p + 1;
    *error = float(e);
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
        b = *in + node->window;
    }
    float *outBuffer = OutputBuffer(node);
    if (shift == 0) {
        CopyForward(outBuffer, a, uint32_t(node->window));
        int window = node->window;
        node->remaining -= window;
        node->available = window;
        *in = b;
        node->pending = 0;
        node->readPosition = 0;
        return 0;
    }
    if (shift > 0) {
        CopyForward(outBuffer, a, uint32_t(shift));
        SndStretch_Crossfade(shift, a, b, outBuffer + shift, node->window);
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
    CopyForward(outBuffer + window, b - shift, uint32_t(window + shift));
    window = node->window;   // read again after the copy, as the original does
    node->available = window * 2 + shift;
    node->remaining -= window * 2;
    *in = b + window;
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
    const float *from = OutputBuffer(node) + node->readPosition;
    int count = *frames;
    if (count > node->available)
        count = node->available;
    CopyForward(*out, from, uint32_t(count));
    node->readPosition += count;
    node->available -= count;
    *frames -= count;
    *out += count;
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
    SFilterStretch *n = reinterpret_cast<SFilterStretch *>(node);
    if (n->remaining == 0)
        return Pull(node, 200, out, scratch);
    int need = stretchframesneeded(n, frames);
    if (n->packetPlayer >= 0) {
        if (need > FramesOutstanding(n->packetPlayer))
            return 0;
    }
    if (need > 0) {
        int got = Pull(node, need, out, scratch);
        if (got <= 0)
            return got;
    }
    int got = stretch(n, frames, scratch, out);
    if (got < frames)
        MemClear(out + got, (frames - got) * 4);
    return frames;
}

// blob: tag 0x98's data - byte 1 half the window, bytes 2..5 the input frames (big-endian), the shifts from 6
// FUNC_AT(0x001443f0)
int SFILTER_timestretchinit(SND::SFilterStretch *node, const uint8_t *blob, int voice) {
    SND_UNTESTED("SFILTER_timestretchinit");
    node->node.process = TimeStretchAt;
    node->node.restore = NULL;
    if (voice >= 0)
        node->packetPlayer = VoiceToPacketHandle(GetMasterVoice(voice));
    else
        node->packetPlayer = -1;
    node->data = blob + 6;
    node->ratio = 1.0f;
    node->error = 0.0f;
    node->window = blob[1] * 2;
    node->remaining = SNDI_getb(blob + 2, 4);
    node->pending = 0;
    node->available = 0;
    node->readPosition = 0;
    return 0;
}

// ratio: 4.12 fixed point, clamped to 0.5..2
// FUNC_AT(0x00144300)
void SFILTER_timestretchsetratio(SND::SFilterStretch *node, int ratio) {
    SND_UNTESTED("SFILTER_timestretchsetratio");
    double f = double(ratio) * kRatioScale;
    node->ratio = float(f);
    if (f < 0.5) {
        node->ratio = 0.5f;
        return;
    }
    if (f > 2.0)
        node->ratio = 2.0f;
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
        const float *from = OutputBuffer(node) + node->readPosition;
        int count = node->available;
        if (!(remaining > count))
            count = remaining;
        CopyForward(o, from, uint32_t(count));
        node->readPosition += count;
        o += count;
        node->available -= count;
        total += count;
        remaining -= count;
        if (remaining <= 0)
            break;
    }
    if (shift > 0) {
        CopyForward(node->held, input, uint32_t(node->window));
        node->pending = node->window;
        input += node->window;
    }
    if (remaining > 0) {
        int count = node->remaining;
        if (!(remaining > count))
            count = remaining;
        CopyForward(o, input, uint32_t(count));
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
    float error = node->error;
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
