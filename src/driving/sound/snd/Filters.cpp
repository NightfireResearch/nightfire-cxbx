#include "Filters.h"

#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>
#include <emmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// EA's SFILTER graph, the parts the disc's data reaches (docs/driving/sound.md 3.6, 4.8, 9.3 step 4).
//
// A MIX voice is a pull chain of nodes, each SFilterNode-headed: its process function pulls its upstream node
// (+0x08) and filters what comes back. The node list at MIX +0x40 is sorted by priority (SFILTER_addtofilterlist);
// the reverb network connects nodes by slot (SFILTER_connect). Process, restore and kernel pointers are stored as
// the originals' addresses (which jump here), so a node's bytes are the original's.
//
// Each function is the original at the same address, ported from the listing: x87 arithmetic in double in the
// original's order with a float rounding at every float store and the x87 stack's values kept unrounded, the
// resampler kernel FUN_001462b0 (hand-written SSE) instruction for instruction in intrinsics, writing the same
// scratch globals (0x001da4e0..0x001da52c). The x87 results equal the original's at 53-bit precision control
// (docs/driving/sound.md 8.8). devtools/SndFilterShadow.cpp compares every function here with the original.
//
// Calls outside module H go to the originals' addresses: the EA-XA decoder (module I), SNDPKTPLAYI (module D),
// SNDDRV/SNDMEMI, the reverb's cosine FUN_00146640 (module G) and memclr (ours).
// ---------------------------------------------------------------------------------------------------------------

using namespace SND;

namespace {

inline float F32(uint32_t address) {
    return *(const float *)(uintptr_t)address;
}

// .rdata constants
const uint32_t kOne = 0x00189de8u;           // 1.0f
const uint32_t kZero = 0x00189decu;          // 0.0f
const uint32_t kMinusOne = 0x0018a134u;      // -1.0f
const uint32_t kOver256 = 0x00190274u;       // 1/256
const uint32_t kMagic = 0x001a7560u;         // 12582912.0f, 1.5 x 2^23
const uint32_t kClampHigh = 0x0018b8ecu;     // 0.8f
const uint32_t kPi = 0x001a756cu;            // pi
const uint32_t kHalf = 0x00189eb0u;          // 0.5f
const uint32_t kQuarterPi = 0x0018c998u;     // pi/4
const uint32_t kHamming46 = 0x001a758cu;     // 0.46f
const uint32_t kHamming54 = 0x001a7588u;     // 0.54f
const uint32_t kTwoPi = 0x0019320cu;         // 2 pi
const uint32_t kSin3 = 0x001a1fb0u, kSin5 = 0x001a75b0u, kSin7 = 0x001a75acu, kSin9 = 0x001a75a8u,
               kSin11 = 0x001a75a4u, kSin13 = 0x001a75a0u;   // 1/3! .. 1/13!
const uint32_t kRsfScale = 0x001da4d0u;      // 4 x 2^-31

// The resampler kernel's scratch in .data
const uint32_t kRsfLo = 0x001da4e0u;         // 8 floats: src[i]
const uint32_t kRsfHi = 0x001da500u;         // 4 floats: src[i + 1]
const uint32_t kRsfFrac = 0x001da510u;       // 8 ints: the 31-bit fractions

#define MixList (*(uint8_t **)0x00245be4u)

const uint32_t kLpfRC = 0x001436c0u, kHpfFIR8 = 0x00145560u, kRsf = 0x00144710u, kRsfKernel = 0x001462b0u,
               kSrc = 0x001456b0u, kFt24 = 0x00144590u, kUnpackXapf = 0x00145ab0u, kXapfRestore = 0x00145bf0u;

inline void CopyBits(void *to, const void *from) {
    memcpy(to, from, 4);
}

inline void ClearBits(void *to) {
    memset(to, 0, 4);
}

// FST of a value FLD loaded: the x87 quiets a signalling NaN on the way
inline float Fst(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    if ((u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0)
        u |= 0x00400000u;
    memcpy(&f, &u, 4);
    return f;
}

inline int Pull(SFilterNode *node, int frames, float *a, float *b) {
    SFilterNode *up = node->input;
    return ((SFilterProcess)(uintptr_t)up->process)(up, frames, a, b, node->requester);
}

inline void Memclr(void *data, int bytes) {
    ((void (*)(void *, int))0x0013f600u)(data, bytes);
}

inline void *SndMemAlloc(uint32_t size) {
    return ((void *(*)(uint32_t))0x0013f780u)(size);   // SNDMEMI_alloc
}

inline double Cos(float x) {
    return ((double (*)(float))0x00146640u)(x);        // the reverb's cosine (module G)
}

// The EA-XA decoder (module I), thiscall
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

inline int GetMasterVoice(int voice) {
    return ((int (*)(int))0x00142420u)(voice);         // SNDDRV_getmastervoice
}

inline int VoiceToPacketHandle(int voice) {
    return ((int (*)(int))0x001457e0u)(voice);         // SNDPKTPLAYI_voicetopackethandle
}

inline int GetSampleChan(int voice) {
    return ((int (*)(int))0x00142460u)(voice);         // SNDDRV_getsamplechan
}

inline void FreeFrames(int player, int channel, int frames) {
    ((void (*)(int, int, int))0x0013ef80u)(player, channel, frames);   // SNDPKTPLAYI_freeframes
}

inline void *GetPacket(int player, int channel, int *frames, int *other) {
    return ((void *(*)(int, int, int *, int *))0x0013ee00u)(player, channel, frames, other);   // SNDPKTPLAYI_get
}

// 32-bit pointer arithmetic, as the original's
inline float *Advance(float *p, int32_t elements) {
    return (float *)(uintptr_t)((uint32_t)(uintptr_t)p + (uint32_t)elements * 4u);
}

inline const float *Advance(const float *p, int32_t elements) {
    return (const float *)(uintptr_t)((uint32_t)(uintptr_t)p + (uint32_t)elements * 4u);
}

// h[7] = h[6] .. h[1] = h[0], integer copies
inline void ShiftHistory(float *h) {
    for (int i = 7; i > 0; i--)
        CopyBits(&h[i], &h[i - 1]);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The node list and the connections
// ---------------------------------------------------------------------------------------------------------------

// Makes a node from a description (SNDPLATFORM_filteradd): its memory from the sound heap, its init, then into
// the voice's list. The answer is whatever addtofilterlist leaves in EAX (nobody reads it).
// FUNC_AT(0x00144a30)
SND::SFilterNode* SFILTER_add(int voice, uint32_t arg, const SND::SFilterDesc *desc) {
    uint8_t *mix = MixList + (uint32_t)voice * 0x60u;
    SFilterNode *node = (SFilterNode *)SndMemAlloc(desc->size);
    ((void (*)(SFilterNode *, uint32_t, uint32_t))(uintptr_t)desc->init)(node, desc->param, arg);
    node->priority = desc->priority;
    node->process = desc->process;
    node->restore = desc->restore;
    return SFILTER_addtofilterlist((SFilterNode **)(mix + 0x40), node);
}

// Inserts before the first node of the same or higher priority. Answers the node now after it (EAX).
// FUNC_AT(0x00144460)
SND::SFilterNode* SFILTER_addtofilterlist(SND::SFilterNode **head, SND::SFilterNode *node) {
    SFilterNode *cur = *head;
    SFilterNode *prev = NULL;
    if (cur != NULL) {
        uint16_t priority = node->priority;
        while (cur->priority < priority) {
            prev = cur;
            cur = cur->input;
            if (cur == NULL)
                break;
        }
    }
    node->input = cur;
    if (cur != NULL)
        cur->output = node;
    if (prev == NULL) {
        *head = node;
        return cur;
    }
    prev->input = node;
    node->output = prev;
    return cur;
}

// Unlinks a node. Removing the head does not clear the new head's back link; an empty list with another node
// faults, as the original. Answers the new head or the node before the one removed (EAX).
// FUNC_AT(0x001444a0)
SND::SFilterNode* SFILTER_remove(SND::SFilterNode **head, SND::SFilterNode *node) {
    SFilterNode *p = *head;
    if (node == p) {
        p = p->input;
        *head = p;
        return p;
    }
    if (p->input != NULL) {
        for (;;) {
            SFilterNode *c = p->input;
            if (c == node)
                break;
            p = c;
            if (p->input == NULL)
                break;
        }
    }
    SFilterNode *c = p->input;
    if (c != NULL && c == node) {
        SFilterNode *next = c->input;
        if (next != NULL)
            next->output = p;
        p->input = p->input->input;
    }
    return p;
}

// Output slot 'fromSlot' (1, 2) of one node to input slot 'toSlot' (1, 2) of another, if that input is free;
// the requester byte remembers the output slot. 0, or -1.
// FUNC_AT(0x001444f0)
int SFILTER_connect(SND::SFilterNode *from, SND::SFilterNode *to, int fromSlot, int toSlot) {
    if (fromSlot == 1) {
        if (toSlot == 1) {
            if (to->input != NULL)
                return -1;
            from->output = to;
            to->input = from;
            to->requester = 1;
            return 0;
        }
        if (toSlot == 2) {
            if (to->input2 != NULL)
                return -1;
            from->output = to;
            to->input2 = from;
            to->requester = 1;
            return 0;
        }
        return -1;
    }
    if (fromSlot == 2) {
        if (toSlot == 1) {
            if (to->input != NULL)
                return -1;
            from->output2 = to;
            to->input = from;
            to->requester = 2;
            return 0;
        }
        if (toSlot == 2) {
            if (to->input2 != NULL)
                return -1;
            from->output2 = to;
            to->input2 = from;
            to->requester = 2;
            return 0;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------
// One-pole low pass
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x001436c0)
int SFILTER_lpfRC(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    if (node->input != NULL) {
        int got = Pull(node, frames, out, scratch);
        if (got <= 0)
            return got;
    }
    FUN_00145720(&((SFilterLPFRC *)node)->y, frames, scratch, out);
    return frames;
}

// FUNC_AT(0x00143710)
int SFILTER_createLPFRC(SND::SFilterLPFRC *node) {
    node->node.input = NULL;
    node->node.input2 = NULL;
    node->node.output = NULL;
    node->node.output2 = NULL;
    node->node.flags = 0;
    node->node.restore = 0;
    node->node.process = kLpfRC;
    ClearBits(&node->y);
    return 0;
}

// params: {cutoff, sample rate, gain x 256}: b = 2 cutoff / rate, a = 1 - b, both scaled by the gain
// FUNC_AT(0x00143740)
void SFILTER_modifyLPFRC(SND::SFilterLPFRC *node, const int *params) {
    double x = (double)params[0];
    x = x + x;
    x = x / (double)params[1];
    node->b = (float)x;
    node->a = (float)((double)F32(kOne) - x);
    node->b = (float)((double)params[2] * (double)node->b * (double)F32(kOver256));
    node->a = (float)((double)params[2] * (double)node->a * (double)F32(kOver256));
}

// y = y a + b x per sample; y stays on the x87 stack between samples, stored once at the end. Runs at least once
// (the original's loop tests at the bottom): with no frames it filters in[0] into out[0].
// FUNC_AT(0x00145720)
void FUN_00145720(float *state, int frames, const float *in, float *out) {
    uint32_t span = (uint32_t)frames << 2;
    uint32_t inEnd = (uint32_t)(uintptr_t)in + span, outEnd = (uint32_t)(uintptr_t)out + span;
    int32_t i = -(int32_t)span;
    double y = state[0];
    for (;;) {
        y = y * (double)state[1] + (double)state[2] * (double)*(const float *)(uintptr_t)(inEnd + (uint32_t)i);
        *(float *)(uintptr_t)(outEnd + (uint32_t)i) = (float)y;
        int64_t next = (int64_t)i + 4;      // ADD ECX, 4; JL: SF != OF, the true sum's sign
        i = (int32_t)next;
        if (!(next < 0))
            break;
    }
    state[0] = (float)y;
}

// ---------------------------------------------------------------------------------------------------------------
// 8-tap FIR high pass and its design
// ---------------------------------------------------------------------------------------------------------------

// Filters what the upstream node answered (not 'frames'), and answers that.
// FUNC_AT(0x00145560)
int SFILTER_hpfFIR8(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    int count = frames;
    if (node->input != NULL) {
        count = Pull(node, frames, out, scratch);
        if (count <= 0)
            return count;
    }
    FUN_001466e0((FirState *)((SFilterFIR8 *)node)->history, count, scratch, out);
    return count;
}

// FUNC_AT(0x001454d0)
int SFILTER_createHPFFIR8(SND::SFilterFIR8 *node) {
    node->node.input = NULL;
    node->node.input2 = NULL;
    node->node.output = NULL;
    node->node.output2 = NULL;
    node->node.flags = 0;
    node->node.restore = 0;
    node->node.process = kHpfFIR8;
    FUN_001466c0((FirState *)node->history);
    return 0;
}

// params: {cutoff << 7, sample rate << 8}
// FUNC_AT(0x001455b0)
void SFILTER_modifyHPFFIR8(SND::SFilterFIR8 *node, const int *params) {
    int a = params[0] >> 7;
    int b = params[1] >> 8;
    node->cutoff = (float)((double)a / (double)b);
    FUN_00146930((FirState *)node->history, 3);
}

// FUNC_AT(0x001466c0)
void FUN_001466c0(SND::FirState *state) {
    for (int i = 0; i < 8; i++)
        ClearBits(&state->history[i]);
}

// The symmetric FIR: y = (x + h7) c0 + (h0 + h6) c1 + (h1 + h5) c2 + (h2 + h4) c3 + h3 c4, then the history
// shifts. The original runs four samples a turn; the first of each four and the tail sum the terms in one order,
// the other three in another, which matters in floating point, so both orders are kept.
// FUNC_AT(0x001466e0)
void FUN_001466e0(SND::FirState *state, int frames, const float *in, float *out) {
    float *h = state->history;
    const float *c = state->coef;
    int done = 0;
    if (frames >= 4) {
        int last = 3;
        const float *x = in;
        float *o = out;
        do {
            // the first of four: (((A + B) + C) + D) + E
            double a = ((double)x[0] + (double)h[7]) * (double)c[0];
            double b = ((double)h[5] + (double)h[1]) * (double)c[2];
            double cc = ((double)h[4] + (double)h[2]) * (double)c[3];
            double d = ((double)h[0] + (double)h[6]) * (double)c[1];
            double e = (double)h[3] * (double)c[4];
            o[0] = (float)((((a + b) + cc) + d) + e);
            ShiftHistory(h);
            // the other three: (((D + A) + B) + C) + E, h0 stored from the x87 stack
            for (int k = 1; k < 4; k++) {
                h[0] = Fst(x[k - 1]);
                d = ((double)h[0] + (double)h[6]) * (double)c[1];
                a = ((double)x[k] + (double)h[7]) * (double)c[0];
                b = ((double)h[5] + (double)h[1]) * (double)c[2];
                cc = ((double)h[4] + (double)h[2]) * (double)c[3];
                e = (double)h[3] * (double)c[4];
                o[k] = (float)((((d + a) + b) + cc) + e);
                ShiftHistory(h);
            }
            CopyBits(&h[0], &x[3]);
            last += 4;
            done += 4;
            x += 4;
            o += 4;
        } while (last < frames);
    }
    for (; done < frames; done++) {
        double a = ((double)h[7] + (double)in[done]) * (double)c[0];
        double b = ((double)h[5] + (double)h[1]) * (double)c[2];
        double cc = ((double)h[4] + (double)h[2]) * (double)c[3];
        double d = ((double)h[0] + (double)h[6]) * (double)c[1];
        double e = (double)h[3] * (double)c[4];
        out[done] = (float)((((a + b) + cc) + d) + e);
        ShiftHistory(h);
        CopyBits(&h[0], &in[done]);
    }
}

// The design: a windowed sinc. Mode 2 low pass at cutoff2, 3 high pass at cutoff (spectral inversion), 4 band
// pass between them; taps by FUN_0014a2e0 (sine) over i pi, a Hamming window through FUN_00146640 (cosine), then
// normalised to unity gain at DC (mode 3: at Nyquist). Other modes only normalise.
// FUNC_AT(0x00146930)
void FUN_00146930(SND::FirState *state, int mode) {
    float *coef = state->coef;
    if (mode == 3 || mode == 4) {
        if ((double)state->cutoff > (double)F32(kClampHigh)) {
            uint32_t u = 0x3f4ccccdu;   // 0.8f
            CopyBits(&state->cutoff, &u);
        }
    }
    switch (mode) {
    case 4: {
        coef[4] = (float)((double)state->cutoff2 - (double)state->cutoff);
        for (int i = 1; i <= 4; i++) {
            float l = (float)((double)i * (double)F32(kPi));
            float arg = (float)(((double)state->cutoff2 - (double)state->cutoff) * (double)l * (double)F32(kHalf));
            double s = FUN_0014a2e0(arg);
            float m = (float)(s / (double)l);
            float arg2 = (float)(((double)state->cutoff2 + (double)state->cutoff) * (double)l * (double)F32(kHalf));
            double r = Cos(arg2) * (double)m;
            coef[4 - i] = (float)(r + r);
        }
        break;
    }
    case 3:
    case 2: {
        const float *edge = mode == 3 ? &state->cutoff : &state->cutoff2;
        CopyBits(&coef[4], edge);
        for (int i = 1; i <= 4; i++) {
            double il = (double)i * (double)F32(kPi);
            float l = (float)il;
            float arg = (float)(il * (double)*edge);
            double s = FUN_0014a2e0(arg);
            coef[4 - i] = (float)(s / (double)l);
        }
        break;
    }
    default:
        break;
    }
    float sum;
    ClearBits(&sum);
    for (int k = 0; k <= 4; k++) {
        if (mode == 2 || mode == 4) {
            double w = Cos((float)((double)k * (double)F32(kQuarterPi)));
            w = w * (double)F32(kHamming46);
            w = (double)F32(kHamming54) - w;
            w = w * (double)coef[k];
            coef[k] = (float)w;
            sum = (float)(w + (double)sum);
        } else if (mode == 3) {
            double w = Cos((float)((double)k * (double)F32(kQuarterPi)));
            w = w * (double)F32(kHamming46);
            w = (double)F32(kHamming54) - w;
            w = w * (double)coef[k];
            w = -w;
            float fw = (float)w;
            coef[k] = (float)w;
            if (k % 2 != 0)
                sum = (float)((double)sum - (double)fw);
            else
                sum = (float)((double)fw + (double)sum);
        }
    }
    if (mode == 3) {
        coef[4] = (float)((double)coef[4] + (double)F32(kOne));
        sum = (float)((double)sum + (double)F32(kOne));
    }
    double t = (double)sum + (double)sum;
    t = t - (double)coef[4];
    if (t < (double)F32(kZero))
        t = t * (double)F32(kMinusOne);
    double g = (double)F32(kOne) / t;
    coef[0] = (float)(g * (double)coef[0]);
    coef[1] = (float)(g * (double)coef[1]);
    coef[2] = (float)(g * (double)coef[2]);
    coef[3] = (float)(g * (double)coef[3]);
    coef[4] = (float)(g * (double)coef[4]);
}

// Sine by its Taylor series to x^13 after taking 2 pi off while above 2 pi (PS2: beside cheapsqrt). Answered in
// ST0 unrounded; x^11 goes through a float.
// FUNC_AT(0x0014a2e0)
double FUN_0014a2e0(float x) {
    double k = (double)F32(kTwoPi);
    double v = (double)x;
    if (v > k) {
        do {
            v = v - k;
        } while (v > k);
    }
    double x2 = v * v;
    double x3 = x2 * v;
    double x5 = x3 * x2;
    double x7 = x5 * x2;
    double x9 = x7 * x2;
    float x11 = (float)(x9 * x2);
    double r = v - x3 * (double)F32(kSin3);
    r = r + x5 * (double)F32(kSin5);
    r = r - x7 * (double)F32(kSin7);
    r = r + x9 * (double)F32(kSin9);
    r = r - (double)x11 * (double)F32(kSin11);
    r = r + (double)x11 * x2 * (double)F32(kSin13);
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// The resampler
// ---------------------------------------------------------------------------------------------------------------

// At 0x10000 a pass-through that still keeps four samples of history (and the first call asks four more frames
// than it answers, into 'out'); otherwise it pulls (pitch x frames + phase) >> 16 frames after the history and
// interpolates linearly through the kernel.
// FUNC_AT(0x00144710)
int SFILTER_rsf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    SFilterRSF *n = (SFilterRSF *)node;
    if (n->pitch == 0x10000u) {
        int ask;
        int got = frames;
        float *o = out;
        if (n->primed == 0) {
            ask = frames - n->offset + 4;
        } else {
            for (int i = 0; i < 4 - n->offset; i++) {
                CopyBits(o, &n->history[n->offset + i]);
                o++;
            }
            ask = frames;
        }
        if (node->input != NULL)
            got = Pull(node, ask, scratch, o);
        if (got <= 0) {
            if (n->primed == 0)
                n->primed = 1;
            for (int i = 0; i < 4; i++)
                ClearBits(&n->history[i]);
            return got;
        }
        const float *h;
        if (n->primed == 0) {
            h = Advance(o, frames - n->offset);
            n->primed = 1;
        } else {
            h = Advance(o, frames - 4);
        }
        for (int i = 0; i < 4; i++)
            CopyBits(&n->history[i], &h[i]);
        return frames;
    }

    uint32_t need = (n->pitch * (uint32_t)frames + n->phase) >> 16;
    float *src = scratch;
    if (n->primed == 0) {
        need += (uint32_t)(4 - n->offset);
        for (int i = 0; i < n->offset; i++) {
            ClearBits(src);
            src++;
        }
    } else {
        for (int i = 0; i < 4; i++)
            CopyBits(&src[i], &n->history[i]);
        src += 4;
    }
    if ((int)need > 0 && node->input != NULL) {
        int got = Pull(node, (int)need, out, src);
        if (got <= 0) {
            if (n->primed == 0)
                n->primed = 1;
            for (int i = 0; i < 4; i++)
                ClearBits(&n->history[i]);
            return got;
        }
    }
    if (n->primed != 0)
        src = Advance(src, n->offset - 4);
    else
        n->primed = 1;
    uint32_t fraction = n->phase << 16;
    int index = 0;
    ((RsfKernel)(uintptr_t)n->kernel)(frames, src, out, &index, &fraction, (int)(n->pitch >> 16), n->pitch << 16);
    n->phase = ((uint32_t)index << 16) | (fraction >> 16);
    const float *h = Advance((const float *)src, (int32_t)(n->phase >> 16) - n->offset);
    for (int i = 0; i < 4; i++)
        CopyBits(&n->history[i], &h[i]);
    *(uint16_t *)((uint8_t *)n + 0x22) = 0;
    return frames;
}

// 'noKernel' non-zero leaves the kernel pointer alone (nobody passes it)
// FUNC_AT(0x00144920)
void SFILTER_rsfinit(SND::SFilterRSF *node, uint32_t param, int noKernel) {
    (void)param;
    node->node.process = kRsf;
    node->node.restore = 0;
    node->phase = 0;
    node->primed = 0;
    node->offset = 0;
    if (noKernel == 0)
        node->kernel = kRsfKernel;
    for (int i = 0; i < 4; i++)
        ClearBits(&node->history[i]);
}

// FUNC_AT(0x00144700)
void SFILTER_rsfsetpitch(SND::SFilterRSF *node, uint32_t pitch) {
    node->pitch = pitch;
}

// The kernel, hand-written SSE: out = lo + (hi - lo) x frac, frac a 31-bit fraction (the position's low bit is
// dropped each step) times 2^-31; eight samples a turn through the scratch globals (aligned stores: 'dst' must be
// 16-byte aligned when count >= 8), then one at a time while (count & 7) != 0. Leaves the position in *index and
// *fraction.
// AUTOLTCG
void FUN_001462b0(int count, const float *src, float *dst, int *index, uint32_t *fraction, int stepInt,
                  uint32_t stepFraction) {
    uint32_t frac = *fraction;
    int32_t at = *index;
    int c = count;
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *lo = (uint32_t *)(uintptr_t)kRsfLo;
    uint32_t *hi = (uint32_t *)(uintptr_t)kRsfHi;
    uint32_t *fr = (uint32_t *)(uintptr_t)kRsfFrac;
    __m128 scale = _mm_load_ps((const float *)(uintptr_t)kRsfScale);
    if (c >= 8) {
        c -= 8;
        do {
            __m128 x1 = _mm_setzero_ps();
            for (int j = 0; j < 8; j++) {
                if (j == 4)
                    x1 = _mm_load_ps((const float *)hi);
                frac >>= 1;
                hi[j & 3] = s[at + 1];
                fr[j] = frac;
                lo[j] = s[at];
                frac <<= 1;
                uint64_t sum = (uint64_t)frac + stepFraction;
                frac = (uint32_t)sum;
                at = (int32_t)((uint32_t)at + (uint32_t)stepInt + (uint32_t)(sum >> 32));
            }
            __m128 x3 = _mm_load_ps((const float *)hi);
            __m128 x6 = _mm_cvtepi32_ps(_mm_load_si128((const __m128i *)fr));
            __m128 x5 = _mm_cvtepi32_ps(_mm_load_si128((const __m128i *)(fr + 4)));
            x6 = _mm_mul_ps(x6, scale);
            x5 = _mm_mul_ps(x5, scale);
            x1 = _mm_sub_ps(x1, _mm_load_ps((const float *)lo));
            x3 = _mm_sub_ps(x3, _mm_load_ps((const float *)(lo + 4)));
            x1 = _mm_mul_ps(x1, x6);
            x3 = _mm_mul_ps(x3, x5);
            x1 = _mm_add_ps(x1, _mm_load_ps((const float *)lo));
            x3 = _mm_add_ps(x3, _mm_load_ps((const float *)(lo + 4)));
            _mm_store_ps(dst, x1);
            _mm_store_ps(dst + 4, x3);
            dst += 8;
            c -= 8;
        } while (c >= 0);
        c += 8;
    }
    while ((c & 7) != 0) {
        frac >>= 1;
        __m128 h = _mm_load_ss((const float *)&s[at + 1]);
        __m128 f = _mm_cvtsi32_ss(_mm_setzero_ps(), (int)frac);
        __m128 l = _mm_load_ss((const float *)&s[at]);
        h = _mm_sub_ss(h, l);
        f = _mm_mul_ss(f, scale);
        frac <<= 1;
        h = _mm_mul_ss(h, f);
        uint64_t sum = (uint64_t)frac + stepFraction;
        frac = (uint32_t)sum;
        l = _mm_add_ss(l, h);
        at = (int32_t)((uint32_t)at + (uint32_t)stepInt + (uint32_t)(sum >> 32));
        _mm_store_ss(dst, l);
        dst++;
        c -= 1;
    }
    *fraction = frac;
    *index = at;
}

// ---------------------------------------------------------------------------------------------------------------
// SOURCE: frames straight from memory
// ---------------------------------------------------------------------------------------------------------------

// The process initSOURCE installs; Ghidra never made it a function (sound.md 7.3).
// FUNC_AT(0x001456b0)
int SFILTER_src(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    (void)scratch;
    (void)requester;
    SFilterSource *n = (SFilterSource *)node;
    const uint32_t *from = (const uint32_t *)n->data;
    uint32_t *to = (uint32_t *)out;
    for (uint32_t i = 0; i < (uint32_t)frames; i++)
        to[i] = from[i];
    n->data = Advance(n->data, frames);
    return frames;
}

// FUNC_AT(0x001456e0)
void SFILTER_initSOURCE(SND::SFilterSource *node, const float *data) {
    node->node.process = kSrc;
    node->node.restore = 0;
    node->data = data;
}

// FUNC_AT(0x00145700)
int SFILTER_createSOURCE(SND::SFilterSource *node) {
    node->node.input = NULL;
    node->node.input2 = NULL;
    node->node.output = NULL;
    node->node.output2 = NULL;
    node->node.flags = 0;
    node->data = NULL;
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The output stage (PS2: SFILTER_ft16)
// ---------------------------------------------------------------------------------------------------------------

// Each float + 1.5 x 2^23 (rounding to nearest), stored back over the input; the low 20 bits of the sum are the
// sample, clipped to -32768..32767.
// FUNC_AT(0x00144590)
int SFILTER_ft24_32(SND::SFilterNode *node, int frames, float *scratch, int16_t *out, int requester) {
    (void)requester;
    if (node->input != NULL) {
        int got = Pull(node, frames, (float *)out, scratch);
        if (got <= 0)
            return got;
    }
    uint32_t p = (uint32_t)(uintptr_t)scratch;
    uint32_t end = p + (uint32_t)frames * 4u;
    int16_t *o = out;
    while (p < end) {
        float *f = (float *)(uintptr_t)p;
        *f = (float)((double)*f + (double)F32(kMagic));
        uint32_t v;
        memcpy(&v, f, 4);
        v &= 0xfffffu;
        if (v > 0x7fffu && v < 0xf8000u)
            v = v < 0x80000u ? 0x7fffu : 0xffff8000u;
        *o++ = (int16_t)(uint16_t)v;
        p += 4;
    }
    return frames;
}

// FUNC_AT(0x00144610)
void SFILTER_ft16init(SND::SFilterNode *node) {
    node->process = kFt24;
    node->restore = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// EA-XA from stream packets: every stream on the disc (sound.md 3.5)
// ---------------------------------------------------------------------------------------------------------------

// Decodes what is left of the current packet, then takes the next one from the packet player (its first two
// shorts are the predictor history); frames decoded are reported back on the next call. At the end of the data
// the rest is cleared if anything was decoded since the last report.
// FUNC_AT(0x00145ab0)
int SFILTER_unpackxapf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    (void)scratch;
    (void)requester;
    SFilterXAPF *n = (SFilterXAPF *)node;
    int total = 0;
    if (n->pending != 0) {
        FreeFrames(n->packetPlayer, n->channel, n->pending);
        n->pending = 0;
    }
    int remaining = frames;
    if (remaining > 0) {
        do {
            int got = DecoderDecode(n->decoder, &out, remaining);
            n->position += got;
            out = Advance(out, got);
            n->pending += got;
            total += got;
            if (got >= remaining) {
                remaining -= got;
            } else {
                remaining -= got;
                int other;
                const int16_t *packet = (const int16_t *)GetPacket(n->packetPlayer, n->channel, &n->packetFrames,
                                                                   &other);
                n->packet = packet;
                if (packet == NULL) {
                    if (remaining > 0 && n->pending != 0) {
                        Memclr(out, remaining * 4);
                        return total + remaining;
                    }
                    return total;
                }
                float state[2];
                state[0] = (float)packet[0];
                state[1] = (float)packet[1];
                DecoderSetState(n->decoder, state);
                n->packet = packet + 2;
                int packetFrames = n->packetFrames;
                n->position = 0;
                DecoderFeed(n->decoder, n->packet, packetFrames * 4, packetFrames);
            }
        } while (remaining > 0);
    }
    return total;
}

// FUNC_AT(0x00145bf0)
void SFILTER_unpackxapfrestore(SND::SFilterXAPF *node) {
    if (node->decoder != NULL)
        ((void (*)(void *))0x00149e60u)(node->decoder);   // SND::CEAXABLKDecf's operator delete
}

// The original's exception frame around the decoder's operator new is dropped: CODA_New answers NULL, it does
// not throw (sound.md 7.6).
// FUNC_AT(0x00145c10)
void SFILTER_unpackxapfinit(SND::SFilterXAPF *node, const SND::UnpackInfo *info) {
    node->node.process = kUnpackXapf;
    node->node.restore = kXapfRestore;
    node->packetPlayer = VoiceToPacketHandle(GetMasterVoice(info->voice));
    node->channel = (uint8_t)GetSampleChan(info->voice);
    node->packet = NULL;
    node->packetFrames = info->frames;
    node->position = 0;
    node->pending = 0;
    void *memory = ((void *(*)(uint32_t))0x00149e50u)(0xa8);   // SND::CEAXABLKDecf::operator new
    if (memory != NULL)
        node->decoder = ((CEAXABLKDecf *(__fastcall *)(void *, int))0x00149e70u)(memory, 0);
    else
        node->decoder = NULL;
}
