#include "Filters.h"
#include "Decode.h"
#include "Reverb.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
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
// Calls outside module H go to the originals' addresses where the shadow test puts its recording fakes (SNDPKTPLAYI,
// SNDDRV, SNDMEMI_alloc) and for memclr; the EA-XA decoder (module I) and the reverb's cosine FUN_00146640
// (module G) are called directly.
// ---------------------------------------------------------------------------------------------------------------

using namespace SND;

namespace {

// The constants (the original's .rdata values)
constexpr float kOver256 = 1.0f / 256;
constexpr float kMagic = 12582912.0f;              // 1.5 x 2^23
constexpr float kClampHigh = 0.8f;
constexpr float kPi = 0x1.921fb6p+1f;
constexpr float kQuarterPi = 0x1.921fb6p-1f;
constexpr float kTwoPi = 0x1.921fb6p+2f;
constexpr float kHamming46 = 0.46f;
constexpr float kHamming54 = 0.54f;
constexpr float kHalf = 0.5f;
constexpr float kSin3 = 0x1.555556p-3f;            // 1/3! .. 1/13!, as the original rounded them
constexpr float kSin5 = 0x1.11110ap-7f;
constexpr float kSin7 = 0x1.a01a02p-13f;
constexpr float kSin9 = 0x1.71de3ap-19f;
constexpr float kSin11 = 0x1.ae6456p-26f;
constexpr float kSin13 = 0x1.612422p-33f;
constexpr float kRsfScale = 0x1p-31f;              // the kernel's fraction scale, four of them in the original
static_assert(std::bit_cast<uint32_t>(kClampHigh) == 0x3f4ccccd, "the original's 0.8");
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "the original's pi");
static_assert(std::bit_cast<uint32_t>(kQuarterPi) == 0x3f490fdb, "the original's pi/4");
static_assert(std::bit_cast<uint32_t>(kTwoPi) == 0x40c90fdb, "the original's 2 pi");
static_assert(std::bit_cast<uint32_t>(kHamming46) == 0x3eeb851f, "the original's 0.46");
static_assert(std::bit_cast<uint32_t>(kHamming54) == 0x3f0a3d71, "the original's 0.54");
static_assert(std::bit_cast<uint32_t>(kSin3) == 0x3e2aaaab, "the original's 1/3!");
static_assert(std::bit_cast<uint32_t>(kSin5) == 0x3c088885, "the original's 1/5!");
static_assert(std::bit_cast<uint32_t>(kSin7) == 0x39500d01, "the original's 1/7!");
static_assert(std::bit_cast<uint32_t>(kSin9) == 0x3638ef1d, "the original's 1/9!");
static_assert(std::bit_cast<uint32_t>(kSin11) == 0x32d7322b, "the original's 1/11!");
static_assert(std::bit_cast<uint32_t>(kSin13) == 0x2f309211, "the original's 1/13!");
static_assert(std::bit_cast<uint32_t>(kRsfScale) == 0x30000000, "the original's 2^-31");

// The resampler kernel's scratch in .data (0x001da4e0..0x001da52f), written as the original writes it
struct RsfKernelScratch {            // 0x50, every array 16-byte aligned
    float lo[8];                     // +0x00 src[i]
    float hi[4];                     // +0x20 src[i + 1]
    uint32_t fraction[8];            // +0x30 the 31-bit fractions
};
static_assert(sizeof(RsfKernelScratch) == 0x50, "the kernel's scratch is 0x50 bytes");
#define KernelScratch (*(RsfKernelScratch *)0x001da4e0)

#define MixVoices (*(SND::MixVoice **)0x00245be4)   // MixList: SndMix.voices

// The process, restore and kernel functions a node stores: the originals' addresses (which jump to the ports
// here), so a node's bytes are the original's
#define LpfRCAt ((SFilterProcess)0x001436c0)              // SFILTER_lpfRC
#define HpfFIR8At ((SFilterProcess)0x00145560)            // SFILTER_hpfFIR8
#define RsfAt ((SFilterProcess)0x00144710)                // SFILTER_rsf
#define RsfKernelAt ((RsfKernel)0x001462b0)               // FUN_001462b0
#define SrcAt ((SFilterProcess)0x001456b0)                // SFILTER_src
#define Ft24At ((SFilterProcess)0x00144590)               // SFILTER_ft24_32
#define UnpackXapfAt ((SFilterProcess)0x00145ab0)         // SFILTER_unpackxapf
#define XapfRestoreAt ((SFilterRestore)0x00145bf0)        // SFILTER_unpackxapfrestore

// ---- the originals called from here (other modules). Through the originals' addresses, which jump to the ports
// in game: the shadow test puts its recording fakes there (devtools/SndFilterShadow.cpp).
#define MemClear ((void (*)(void *, int))0x0013f600)                            // memclr
#define SndMemAlloc ((void *(*)(uint32_t))0x0013f780)                           // SNDMEMI_alloc
#define GetMasterVoice ((int (*)(int))0x00142420)                               // SNDDRV_getmastervoice
#define GetSampleChan ((int (*)(int))0x00142460)                                // SNDDRV_getsamplechan
#define VoiceToPacketHandle ((int (*)(int))0x001457e0)                          // SNDPKTPLAYI_voicetopackethandle
#define FreeFrames ((void (*)(int, int, int))0x0013ef80)                        // SNDPKTPLAYI_freeframes
#define GetPacket ((const int16_t *(*)(int player, int channel, int *frames, int *other))0x0013ee00)   // SNDPKTPLAYI_get

// FST of a value FLD loaded: the x87 quiets a signalling NaN on the way
float Fst(float f) {
    uint32_t u = std::bit_cast<uint32_t>(f);
    if ((u & 0x7f800000) == 0x7f800000 && (u & 0x007fffff) != 0)
        u |= 0x00400000;
    return std::bit_cast<float>(u);
}

int Pull(SFilterNode *node, int frames, float *a, float *b) {
    SFilterNode *up = node->input;
    return up->process(up, frames, a, b, node->requester);
}

// h[7] = h[6] .. h[1] = h[0] (integer moves in the original: a float copy keeps the bits)
void ShiftHistory(float *h) {
    for (int i = 7; i > 0; i--)
        h[i] = h[i - 1];
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The node list and the connections
// ---------------------------------------------------------------------------------------------------------------

// Makes a node from a description (SNDPLATFORM_filteradd): its memory from the sound heap, its init, then into
// the voice's list. The answer is whatever addtofilterlist leaves in EAX (nobody reads it).
// FUNC_AT(0x00144a30)
SND::SFilterNode* SFILTER_add(int voice, uint32_t arg, const SND::SFilterDesc *desc) {
    MixVoice *mix = &MixVoices[voice];
    SFilterNode *node = static_cast<SFilterNode *>(SndMemAlloc(desc->size));
    desc->init(node, desc->param, arg);
    node->priority = desc->priority;
    node->process = desc->process;
    node->restore = desc->restore;
    return SFILTER_addtofilterlist(&mix->chain, node);
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
    FUN_00145720(&reinterpret_cast<SFilterLPFRC *>(node)->y, frames, scratch, out);
    return frames;
}

// FUNC_AT(0x00143710)
int SFILTER_createLPFRC(SND::SFilterLPFRC *node) {
    node->node.input = NULL;
    node->node.input2 = NULL;
    node->node.output = NULL;
    node->node.output2 = NULL;
    node->node.flags = 0;
    node->node.restore = NULL;
    node->node.process = LpfRCAt;
    node->y = 0.0f;
    return 0;
}

// params: {cutoff, sample rate, gain x 256}: b = 2 cutoff / rate, a = 1 - b, both scaled by the gain
// FUNC_AT(0x00143740)
void SFILTER_modifyLPFRC(SND::SFilterLPFRC *node, const int *params) {
    double x = params[0];
    x = x + x;
    x = x / params[1];
    node->b = float(x);
    node->a = float(1.0 - x);
    node->b = float(double(params[2]) * node->b * kOver256);
    node->a = float(double(params[2]) * node->a * kOver256);
}

// y = y a + b x per sample; y stays on the x87 stack between samples, stored once at the end. Runs at least once
// (the original's loop tests at the bottom): with no frames it filters in[0] into out[0].
// FUNC_AT(0x00145720)
void FUN_00145720(float *state, int frames, const float *in, float *out) {
    double y = state[0];
    int i = 0;
    do {
        y = y * state[1] + double(state[2]) * in[i];
        out[i] = float(y);
    } while (++i < frames);
    state[0] = float(y);
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
    FUN_001466e0(&reinterpret_cast<SFilterFIR8 *>(node)->fir, count, scratch, out);
    return count;
}

// FUNC_AT(0x001454d0)
int SFILTER_createHPFFIR8(SND::SFilterFIR8 *node) {
    node->node.input = NULL;
    node->node.input2 = NULL;
    node->node.output = NULL;
    node->node.output2 = NULL;
    node->node.flags = 0;
    node->node.restore = NULL;
    node->node.process = HpfFIR8At;
    FUN_001466c0(&node->fir);
    return 0;
}

// params: {cutoff << 7, sample rate << 8}
// FUNC_AT(0x001455b0)
void SFILTER_modifyHPFFIR8(SND::SFilterFIR8 *node, const int *params) {
    int a = params[0] >> 7;
    int b = params[1] >> 8;
    node->fir.cutoff = float(double(a) / b);
    FUN_00146930(&node->fir, 3);
}

// FUNC_AT(0x001466c0)
void FUN_001466c0(SND::FirState *state) {
    for (int i = 0; i < 8; i++)
        state->history[i] = 0.0f;
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
            double a = (double(x[0]) + h[7]) * c[0];
            double b = (double(h[5]) + h[1]) * c[2];
            double cc = (double(h[4]) + h[2]) * c[3];
            double d = (double(h[0]) + h[6]) * c[1];
            double e = double(h[3]) * c[4];
            o[0] = float((((a + b) + cc) + d) + e);
            ShiftHistory(h);
            // the other three: (((D + A) + B) + C) + E, h0 stored from the x87 stack
            for (int k = 1; k < 4; k++) {
                h[0] = Fst(x[k - 1]);
                d = (double(h[0]) + h[6]) * c[1];
                a = (double(x[k]) + h[7]) * c[0];
                b = (double(h[5]) + h[1]) * c[2];
                cc = (double(h[4]) + h[2]) * c[3];
                e = double(h[3]) * c[4];
                o[k] = float((((d + a) + b) + cc) + e);
                ShiftHistory(h);
            }
            h[0] = x[3];
            last += 4;
            done += 4;
            x += 4;
            o += 4;
        } while (last < frames);
    }
    for (; done < frames; done++) {
        double a = (double(h[7]) + in[done]) * c[0];
        double b = (double(h[5]) + h[1]) * c[2];
        double cc = (double(h[4]) + h[2]) * c[3];
        double d = (double(h[0]) + h[6]) * c[1];
        double e = double(h[3]) * c[4];
        out[done] = float((((a + b) + cc) + d) + e);
        ShiftHistory(h);
        h[0] = in[done];
    }
}

// The design: a windowed sinc. Mode 2 low pass at cutoff2, 3 high pass at cutoff (spectral inversion), 4 band
// pass between them; taps by FUN_0014a2e0 (sine) over i pi, a Hamming window through FUN_00146640 (cosine), then
// normalised to unity gain at DC (mode 3: at Nyquist). Other modes only normalise.
// FUNC_AT(0x00146930)
void FUN_00146930(SND::FirState *state, int mode) {
    float *coef = state->coef;
    if (mode == 3 || mode == 4) {
        if (state->cutoff > kClampHigh)
            state->cutoff = kClampHigh;
    }
    switch (mode) {
    case 4: {
        coef[4] = state->cutoff2 - state->cutoff;
        for (int i = 1; i <= 4; i++) {
            float l = i * kPi;             // i is exact as a float: one rounded product
            float arg = float((double(state->cutoff2) - state->cutoff) * l * kHalf);
            double s = FUN_0014a2e0(arg);
            float m = float(s / l);
            float arg2 = float((double(state->cutoff2) + state->cutoff) * l * kHalf);
            double r = FUN_00146640(arg2) * m;
            coef[4 - i] = float(r + r);
        }
        break;
    }
    case 3:
    case 2: {
        const float *edge = mode == 3 ? &state->cutoff : &state->cutoff2;
        coef[4] = *edge;
        for (int i = 1; i <= 4; i++) {
            double il = double(i) * kPi;
            float l = float(il);
            float arg = float(il * *edge);
            double s = FUN_0014a2e0(arg);
            coef[4 - i] = float(s / l);
        }
        break;
    }
    default:
        break;
    }
    float sum = 0.0f;
    for (int k = 0; k <= 4; k++) {
        if (mode == 2 || mode == 4) {
            double w = FUN_00146640(k * kQuarterPi);
            w = w * kHamming46;
            w = kHamming54 - w;
            w = w * coef[k];
            coef[k] = float(w);
            sum = float(w + sum);
        } else if (mode == 3) {
            double w = FUN_00146640(k * kQuarterPi);
            w = w * kHamming46;
            w = kHamming54 - w;
            w = w * coef[k];
            w = -w;
            float fw = float(w);
            coef[k] = fw;
            if (k % 2 != 0)
                sum = sum - fw;
            else
                sum = fw + sum;
        }
    }
    if (mode == 3) {
        coef[4] = coef[4] + 1.0f;
        sum = sum + 1.0f;
    }
    double t = double(sum) + sum;
    t = t - coef[4];
    if (t < 0.0)
        t = t * -1.0;
    double g = 1.0 / t;
    coef[0] = float(g * coef[0]);
    coef[1] = float(g * coef[1]);
    coef[2] = float(g * coef[2]);
    coef[3] = float(g * coef[3]);
    coef[4] = float(g * coef[4]);
}

// Sine by its Taylor series to x^13 after taking 2 pi off while above 2 pi (PS2: beside cheapsqrt). Answered in
// ST0 unrounded; x^11 goes through a float.
// FUNC_AT(0x0014a2e0)
double FUN_0014a2e0(float x) {
    double k = kTwoPi;
    double v = x;
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
    float x11 = float(x9 * x2);
    double r = v - x3 * kSin3;
    r = r + x5 * kSin5;
    r = r - x7 * kSin7;
    r = r + x9 * kSin9;
    r = r - double(x11) * kSin11;
    r = r + double(x11) * x2 * kSin13;
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
    SFilterRSF *n = reinterpret_cast<SFilterRSF *>(node);
    if (n->pitch == 0x10000) {
        int ask;
        int got = frames;
        float *o = out;
        if (n->primed == 0) {
            ask = frames - n->offset + 4;
        } else {
            for (int i = 0; i < 4 - n->offset; i++)
                *o++ = n->history[n->offset + i];
            ask = frames;
        }
        if (node->input != NULL)
            got = Pull(node, ask, scratch, o);
        if (got <= 0) {
            if (n->primed == 0)
                n->primed = 1;
            for (int i = 0; i < 4; i++)
                n->history[i] = 0.0f;
            return got;
        }
        const float *h;
        if (n->primed == 0) {
            h = o + (frames - n->offset);
            n->primed = 1;
        } else {
            h = o + (frames - 4);
        }
        for (int i = 0; i < 4; i++)
            n->history[i] = h[i];
        return frames;
    }

    uint32_t need = (n->pitch * uint32_t(frames) + n->phase) >> 16;
    float *src = scratch;
    if (n->primed == 0) {
        need += uint32_t(4 - n->offset);
        for (int i = 0; i < n->offset; i++)
            *src++ = 0.0f;
    } else {
        for (int i = 0; i < 4; i++)
            src[i] = n->history[i];
        src += 4;
    }
    if (int(need) > 0 && node->input != NULL) {
        int got = Pull(node, int(need), out, src);
        if (got <= 0) {
            if (n->primed == 0)
                n->primed = 1;
            for (int i = 0; i < 4; i++)
                n->history[i] = 0.0f;
            return got;
        }
    }
    if (n->primed != 0)
        src += n->offset - 4;
    else
        n->primed = 1;
    uint32_t fraction = n->phase << 16;
    int index = 0;
    n->kernel(frames, src, out, &index, &fraction, int(n->pitch >> 16), n->pitch << 16);
    n->phase = (uint32_t(index) << 16) | (fraction >> 16);
    const float *h = src + (int32_t(n->phase >> 16) - n->offset);
    for (int i = 0; i < 4; i++)
        n->history[i] = h[i];
    n->phase &= 0xffff;   // the original clears the high half with a 16-bit store
    return frames;
}

// 'noKernel' non-zero leaves the kernel pointer alone (nobody passes it)
// FUNC_AT(0x00144920)
void SFILTER_rsfinit(SND::SFilterRSF *node, uint32_t param, int noKernel) {
    (void)param;
    node->node.process = RsfAt;
    node->node.restore = NULL;
    node->phase = 0;
    node->primed = 0;
    node->offset = 0;
    if (noKernel == 0)
        node->kernel = RsfKernelAt;
    for (int i = 0; i < 4; i++)
        node->history[i] = 0.0f;
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
    RsfKernelScratch &k = KernelScratch;
    const __m128 scale = _mm_set1_ps(kRsfScale);
    if (c >= 8) {
        c -= 8;
        do {
            __m128 x1 = _mm_setzero_ps();
            for (int j = 0; j < 8; j++) {
                if (j == 4)
                    x1 = _mm_load_ps(k.hi);
                frac >>= 1;
                k.hi[j & 3] = src[at + 1];
                k.fraction[j] = frac;
                k.lo[j] = src[at];
                frac <<= 1;
                uint64_t sum = uint64_t(frac) + stepFraction;
                frac = uint32_t(sum);
                at = int32_t(uint32_t(at) + uint32_t(stepInt) + uint32_t(sum >> 32));
            }
            __m128 x3 = _mm_load_ps(k.hi);
            __m128 x6 = _mm_cvtepi32_ps(_mm_load_si128(reinterpret_cast<const __m128i *>(&k.fraction[0])));
            __m128 x5 = _mm_cvtepi32_ps(_mm_load_si128(reinterpret_cast<const __m128i *>(&k.fraction[4])));
            x6 = _mm_mul_ps(x6, scale);
            x5 = _mm_mul_ps(x5, scale);
            x1 = _mm_sub_ps(x1, _mm_load_ps(&k.lo[0]));
            x3 = _mm_sub_ps(x3, _mm_load_ps(&k.lo[4]));
            x1 = _mm_mul_ps(x1, x6);
            x3 = _mm_mul_ps(x3, x5);
            x1 = _mm_add_ps(x1, _mm_load_ps(&k.lo[0]));
            x3 = _mm_add_ps(x3, _mm_load_ps(&k.lo[4]));
            _mm_store_ps(dst, x1);
            _mm_store_ps(dst + 4, x3);
            dst += 8;
            c -= 8;
        } while (c >= 0);
        c += 8;
    }
    while ((c & 7) != 0) {
        frac >>= 1;
        __m128 h = _mm_load_ss(&src[at + 1]);
        __m128 f = _mm_cvtsi32_ss(_mm_setzero_ps(), int(frac));
        __m128 l = _mm_load_ss(&src[at]);
        h = _mm_sub_ss(h, l);
        f = _mm_mul_ss(f, scale);
        frac <<= 1;
        h = _mm_mul_ss(h, f);
        uint64_t sum = uint64_t(frac) + stepFraction;
        frac = uint32_t(sum);
        l = _mm_add_ss(l, h);
        at = int32_t(uint32_t(at) + uint32_t(stepInt) + uint32_t(sum >> 32));
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
    SFilterSource *n = reinterpret_cast<SFilterSource *>(node);
    for (uint32_t i = 0; i < uint32_t(frames); i++)   // dword moves in the original: a float copy keeps the bits
        out[i] = n->data[i];
    n->data += frames;
    return frames;
}

// FUNC_AT(0x001456e0)
void SFILTER_initSOURCE(SND::SFilterSource *node, const float *data) {
    node->node.process = SrcAt;
    node->node.restore = NULL;
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
        int got = Pull(node, frames, reinterpret_cast<float *>(out), scratch);
        if (got <= 0)
            return got;
    }
    int16_t *o = out;
    for (float *f = scratch, *end = scratch + frames; f < end; f++) {
        *f = *f + kMagic;
        uint32_t v = std::bit_cast<uint32_t>(*f) & 0xfffff;
        if (v > 0x7fff && v < 0xf8000)
            v = v < 0x80000 ? 0x7fff : 0xffff8000;
        *o++ = int16_t(v);
    }
    return frames;
}

// FUNC_AT(0x00144610)
void SFILTER_ft16init(SND::SFilterNode *node) {
    node->process = Ft24At;
    node->restore = NULL;
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
    SFilterXAPF *n = reinterpret_cast<SFilterXAPF *>(node);
    int total = 0;
    if (n->pending != 0) {
        FreeFrames(n->packetPlayer, n->channel, n->pending);
        n->pending = 0;
    }
    int remaining = frames;
    if (remaining > 0) {
        do {
            int got = n->decoder->Decode(&out, remaining);
            n->position += got;
            out += got;
            n->pending += got;
            total += got;
            if (got >= remaining) {
                remaining -= got;
            } else {
                remaining -= got;
                int other;
                const int16_t *packet = GetPacket(n->packetPlayer, n->channel, &n->packetFrames, &other);
                n->packet = packet;
                if (packet == NULL) {
                    if (remaining > 0 && n->pending != 0) {
                        MemClear(out, remaining * 4);
                        return total + remaining;
                    }
                    return total;
                }
                float state[2];
                state[0] = packet[0];
                state[1] = packet[1];
                n->decoder->SetState(state);
                n->packet = packet + 2;
                int packetFrames = n->packetFrames;
                n->position = 0;
                n->decoder->Feed(n->packet, packetFrames * 4, packetFrames);
            }
        } while (remaining > 0);
    }
    return total;
}

// FUNC_AT(0x00145bf0)
void SFILTER_unpackxapfrestore(SND::SFilterXAPF *node) {
    if (node->decoder != NULL)
        SND::CEAXABLKDecf::operator delete(node->decoder);
}

// The original's exception frame around the decoder's operator new is dropped: CODA_New answers NULL, it does
// not throw (sound.md 7.6).
// FUNC_AT(0x00145c10)
void SFILTER_unpackxapfinit(SND::SFilterXAPF *node, const SND::UnpackInfo *info) {
    node->node.process = UnpackXapfAt;
    node->node.restore = XapfRestoreAt;
    node->packetPlayer = VoiceToPacketHandle(GetMasterVoice(info->voice));
    node->channel = uint8_t(GetSampleChan(info->voice));
    node->packet = NULL;
    node->packetFrames = info->frames;
    node->position = 0;
    node->pending = 0;
    void *memory = SND::CEAXABLKDecf::operator new(sizeof(SND::CEAXABLKDecf));
    if (memory != NULL)
        node->decoder = static_cast<SND::CEAXABLKDecf *>(memory)->Construct();
    else
        node->decoder = NULL;
}
