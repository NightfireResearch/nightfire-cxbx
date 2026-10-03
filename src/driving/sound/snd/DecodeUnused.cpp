#include "DecodeUnused.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// The data-dead decoders (docs/driving/sound.md 3.8, 4.9), provisional: nothing on the disc reaches them, so each
// warns once when it first runs (SND_UNTESTED). Every function is the original at the same address, ported from
// the listing: x87 arithmetic in double in the original's order with a float rounding at every float store
// (exact under the original's 53-bit precision control), integers and the bit reader as the original keeps them.
//
// MicroTalk (sample representation 4: SFILTER_unpackmtf, unpackmtpf): a frame of 432 samples is 12 reflection
// coefficients (4 from a 6-bit table, 8 from a 5-bit one), then four subframes of 108: a long-term predictor
// (lag, gain) over the excitation history plus a fixed codebook of 54 samples at every other position
// (readsamples: a two-state Huffman code with zero runs and escapes, or in the header's other mode -2/0/+2), the
// remaining positions either zero or interpolated by a 6-tap half-band filter. The 432 samples then go through
// the 12-pole synthesis filter in four steps (12, 12, 12, 396 samples) as the coefficients move a quarter of the
// way to the new ones per step. FUN_00146e00 turns the coefficients into the direct-form filter, FUN_00146f00
// (9440 bytes, unrolled four times) runs it: its two bodies below were generated from the listing by a script,
// one statement per output in the listing's order (the chain of products summed left to right, then the output
// sample added, stored to the filter ring and to the output).
//
// PCM16: decode16x87 converts int16 to float from the end backwards, eight at a time once the count is a
// multiple of 8 - a count of 0 converts the eight samples before the buffers, as the original does.
// ---------------------------------------------------------------------------------------------------------------

static_assert(offsetof(SND::MutState, threshold) == 0x00c, "MutState layout");
static_assert(offsetof(SND::MutState, gainTable) == 0x010, "MutState layout");
static_assert(offsetof(SND::MutState, coefs) == 0x110, "MutState layout");
static_assert(offsetof(SND::MutState, synth) == 0x140, "MutState layout");
static_assert(offsetof(SND::MutState, signal) == 0x170, "MutState layout");

namespace {

struct MutCode {                       // 12 bytes
    int32_t next;                      // +0 the state for the next code
    int32_t length;                    // +4 bits the code takes
    uint32_t value;                    // +8 a float's bits (codes 4 and up)
};

#define MutMasks       ((const uint32_t *)0x001da530u)   // (1 << n) - 1
#define MutCoefs6      ((const float *)0x001da558u)      // the first four coefficients' 64 levels
#define MutCoefs5      ((const float *)0x001da598u)      // the other eight's 32 levels
#define MutHuffStart   ((const uint8_t *)0x001da658u)    // [state][next 8 bits] -> code
#define MutHuffCodes   ((const MutCode *)0x001da858u)
#define kTwoTo32       (*(const float *)0x0018a4a8u)     // 4294967296: an unsigned FILD's correction
#define kEight         (*(const float *)0x0018b5dcu)
#define kGainStep      (*(const float *)0x001918ccu)     // 0.001
#define kGainBase      (*(const float *)0x001a759cu)     // 1.04
#define kQuarter       (*(const float *)0x0018a4c0u)
#define kLtpGainStep   (*(const float *)0x0018b928u)     // 1/15
#define kHalfBand0     (*(const float *)0x001a7598u)     // the taps at +-5
#define kHalfBand1     (*(const float *)0x001a7594u)     // +-3
#define kHalfBand2     (*(const float *)0x001a7590u)     // +-1
#define kHalf          (*(const float *)0x00189eb0u)

// The original's inlined reader: take n bits, then top up one byte if fewer than 8 are left.
inline uint32_t MutBits(SND::MutState *s, int n) {
    uint32_t value = s->bits & MutMasks[n];
    uint32_t bits = s->bits >> n;
    int32_t count = s->count - n;
    s->bits = bits;
    s->count = count;
    if (count < 8) {
        s->bits = ((uint32_t)*s->ptr << (count & 31)) | bits;
        s->ptr++;
        s->count = count + 8;
    }
    return value;
}

// FILD of a register, then the unsigned correction (the original tests the sign just before the FILD).
inline double UnsignedToDouble(uint32_t v) {
    double d = (double)(int32_t)v;
    if ((int32_t)v < 0)
        d = d + (double)kTwoTo32;
    return d;
}

inline void StoreBits(float *to, uint32_t bits) {
    memcpy(to, &bits, 4);
}

}   // namespace

// The frame header: a bit skipped, the readsamples threshold (4 bits), the gain table's g0 = (n + 1) x 8 (4 bits)
// and step = n x 0.001 + 1.04 (6 bits); clears the coefficients, the filter and the history. Returns 0 (EAX after
// the original's REP STOSD), which SFILTER_unpackmtfinit passes on as its own result.
// FUNC_AT(0x001493e0)
int initmut(const uint8_t *src, SND::MutState *s) {
    SND_UNTESTED("initmut");
    uint32_t edx = src[0];
    s->bits = edx;
    s->ptr = src + 1;
    edx >>= 1;
    s->bits = edx;
    s->count = 7;
    uint32_t esi = ((uint32_t)src[1] << 7) | edx;
    s->bits = esi;
    const uint8_t *p = src + 2;
    s->ptr = p;
    s->count = 15;
    uint32_t ecx = esi >> 4;
    esi &= MutMasks[4];
    s->bits = ecx;
    s->threshold = 32 - (int32_t)esi;
    s->count = 11;
    esi = ecx;
    ecx >>= 4;
    s->count = 7;
    s->bits = ecx;
    uint32_t ebx = ((uint32_t)*p << 7) | ecx;
    esi &= MutMasks[4];
    p++;
    esi++;
    s->bits = ebx;
    s->ptr = p;
    double g = UnsignedToDouble(esi);
    s->count = 15;
    g = g * (double)kEight;
    ecx = s->bits;
    edx = ecx & MutMasks[6];
    ecx >>= 6;
    s->gainTable[0] = (float)g;
    double step = UnsignedToDouble(edx);
    s->bits = ecx;
    s->count = 9;
    step = step * (double)kGainStep;
    step = step + (double)kGainBase;
    for (int i = 0; i < 63; i++)
        s->gainTable[i + 1] = (float)(step * (double)s->gainTable[i]);
    for (int i = 0; i < 12; i++) {
        StoreBits(&s->coefs[i], 0);
        StoreBits(&s->synth[i], 0);
    }
    memset(s->signal, 0, 324 * 4);
    return 0;
}

// 54 fixed-codebook samples into out[0], out[2] ... out[106]. Mode 0: two bits per sample, 01 = -2, 11 = +2,
// otherwise one bit, 0. Else a two-state code: a float for codes 4 and up, a run of 7 + (6 bits) zeros for codes 2
// and 3 (clipped at the end), and for codes 0 and 1 an escape: 7 + the number of 1 bits before a 0, then a sign.
// FUNC_AT(0x00146bb0)
void readsamples(SND::MutState *s, int mode, float *out) {
    SND_UNTESTED("readsamples");
    if (mode == 0) {
        for (int i = 0; i < 0x6c; i += 2) {
            uint32_t low = s->bits & 3;
            uint32_t bits;
            int32_t count;
            if (low == 1) {
                StoreBits(&out[i], 0xc0000000u);
                bits = s->bits >> 2;
                count = s->count - 2;
            } else if (low == 3) {
                StoreBits(&out[i], 0x40000000u);
                bits = s->bits >> 2;
                count = s->count - 2;
            } else {
                StoreBits(&out[i], 0);
                bits = s->bits >> 1;
                count = s->count - 1;
            }
            s->bits = bits;
            s->count = count;
            if (count < 8) {
                s->bits = ((uint32_t)*s->ptr << (count & 31)) | bits;
                s->ptr++;
                s->count = count + 8;
            }
        }
        return;
    }
    int32_t state = 0;
    int i = 0;
    while (i < 0x6c) {
        uint32_t code = MutHuffStart[(s->bits & 0xff) + ((uint32_t)state << 8)];
        state = MutHuffCodes[code].next;
        int32_t length = MutHuffCodes[code].length;
        uint32_t bits = s->bits >> (length & 31);
        int32_t count = s->count - length;
        s->bits = bits;
        s->count = count;
        if (count < 8) {
            s->bits = ((uint32_t)*s->ptr << (count & 31)) | bits;
            s->ptr++;
            s->count = count + 8;
        }
        if ((int32_t)code > 3) {
            StoreBits(&out[i], MutHuffCodes[code].value);
            i += 2;
        } else if ((int32_t)code > 1) {
            int32_t run = (int32_t)MutBits(s, 6) + 7;
            if (i + run * 2 > 0x6c)
                run = (0x6c - i) >> 1;
            if (run <= 0)
                continue;
            do {
                StoreBits(&out[i], 0);
                i += 2;
            } while (--run != 0);
        } else {
            int32_t magnitude = 7;
            while (MutBits(s, 1) == 1)
                magnitude++;
            if (MutBits(s, 1) == 1)
                out[i] = (float)(double)magnitude;
            else
                out[i] = (float)(double)-magnitude;
            i += 2;
        }
    }
}

// The reflection coefficients to the direct-form predictor b[0..11]. The original keeps the lattice memory in
// L[-1..10] (L[-1] starting at 1, L[0..10] at the coefficients themselves) and the impulse response in R.
void MutLpcCoefficients(float *b, const float *k) {
    SND_UNTESTED("FUN_00146e00");
    float L[12];                       // L[0] is the original's L[-1], L[1 + c] its L[c]
    float R[12];
    for (int i = 9; i >= -1; i--)
        L[i + 2] = k[i + 1];
    L[0] = 1.0f;
    for (int n = 0; n < 12; n++) {
        double acc = -((double)L[11] * (double)k[11]);
        for (int c = 10; c >= 0; c--) {
            acc = acc - (double)L[c] * (double)k[c];
            L[c + 1] = (float)(acc * (double)k[c] + (double)L[c]);
        }
        L[0] = (float)acc;
        R[n] = (float)acc;
        int j = 0;
        if (n >= 4) {
            int e = 3;
            do {
                acc = acc - (double)R[n - 1 - j] * (double)b[j];
                acc = acc - (double)b[j + 1] * (double)R[n - 2 - j];
                acc = acc - (double)R[n - 3 - j] * (double)b[j + 2];
                acc = acc - (double)R[n - 4 - j] * (double)b[j + 3];
                e += 4;
                j += 4;
            } while (e < n);
        }
        for (; j < n; j++)
            acc = acc - (double)b[j] * (double)R[n - 1 - j];
        b[n] = (float)acc;
    }
}

// 0x00146e00: EBX = out, [esp+4] = the coefficients; keeps EBX, ESI, EDI and EBP, as the original does.
// AUTOLTCG
__declspec(naked) void FUN_00146e00() {
    __asm {
        push dword ptr [esp + 4]
        push ebx
        call MutLpcCoefficients
        add esp, 8
        ret
    }
}

// groups x 12 samples of the frame from output index `first`, through the 12-pole filter in place; the filter's
// memory is a ring of 12 in `synth`, each output overwriting the oldest.
void MutSynthesise(int first, SND::MutState *s, int groups) {
    SND_UNTESTED("FUN_00146f00");
    float b[12];
    MutLpcCoefficients(b, s->coefs);
    float *sf = (float *)s;
    float *out = (float *)((uint8_t *)s + first * 4 + 0x680);
    double d;
    int done = 0;
    if (groups >= 4) {
        int next = 3;
        do {
            d = (double)b[8] * sf[0x160 / 4] + (double)b[4] * sf[0x150 / 4] + (double)b[0] * sf[0x140 / 4]
                + (double)b[9] * sf[0x164 / 4] + (double)b[5] * sf[0x154 / 4] + (double)b[1] * sf[0x144 / 4]
                + (double)b[11] * sf[0x16c / 4] + (double)b[10] * sf[0x168 / 4] + (double)b[6] * sf[0x158 / 4]
                + (double)b[2] * sf[0x148 / 4] + (double)b[7] * sf[0x15c / 4] + (double)b[3] * sf[0x14c / 4] + out[0];
            sf[0x16c / 4] = (float)d;
            out[0] = (float)d;
            d = (double)b[9] * sf[0x160 / 4] + (double)b[5] * sf[0x150 / 4] + (double)b[1] * sf[0x140 / 4]
                + (double)b[10] * sf[0x164 / 4] + (double)b[6] * sf[0x154 / 4] + (double)b[2] * sf[0x144 / 4]
                + (double)b[11] * sf[0x168 / 4] + (double)b[7] * sf[0x158 / 4] + (double)b[3] * sf[0x148 / 4]
                + (double)b[0] * sf[0x16c / 4] + (double)b[8] * sf[0x15c / 4] + (double)b[4] * sf[0x14c / 4] + out[1];
            sf[0x168 / 4] = (float)d;
            out[1] = (float)d;
            d = (double)b[10] * sf[0x160 / 4] + (double)b[6] * sf[0x150 / 4] + (double)b[2] * sf[0x140 / 4]
                + (double)b[11] * sf[0x164 / 4] + (double)b[7] * sf[0x154 / 4] + (double)b[3] * sf[0x144 / 4]
                + (double)b[8] * sf[0x158 / 4] + (double)b[4] * sf[0x148 / 4] + (double)b[1] * sf[0x16c / 4]
                + (double)b[0] * sf[0x168 / 4] + (double)b[9] * sf[0x15c / 4] + (double)b[5] * sf[0x14c / 4] + out[2];
            sf[0x164 / 4] = (float)d;
            out[2] = (float)d;
            d = (double)b[11] * sf[0x160 / 4] + (double)b[7] * sf[0x150 / 4] + (double)b[3] * sf[0x140 / 4]
                + (double)b[8] * sf[0x154 / 4] + (double)b[4] * sf[0x144 / 4] + (double)b[0] * sf[0x164 / 4]
                + (double)b[9] * sf[0x158 / 4] + (double)b[5] * sf[0x148 / 4] + (double)b[2] * sf[0x16c / 4]
                + (double)b[1] * sf[0x168 / 4] + (double)b[10] * sf[0x15c / 4] + (double)b[6] * sf[0x14c / 4]
                + out[3];
            sf[0x160 / 4] = (float)d;
            out[3] = (float)d;
            d = (double)b[8] * sf[0x150 / 4] + (double)b[4] * sf[0x140 / 4] + (double)b[0] * sf[0x160 / 4]
                + (double)b[9] * sf[0x154 / 4] + (double)b[5] * sf[0x144 / 4] + (double)b[1] * sf[0x164 / 4]
                + (double)b[10] * sf[0x158 / 4] + (double)b[6] * sf[0x148 / 4] + (double)b[3] * sf[0x16c / 4]
                + (double)b[2] * sf[0x168 / 4] + (double)b[11] * sf[0x15c / 4] + (double)b[7] * sf[0x14c / 4]
                + out[4];
            sf[0x15c / 4] = (float)d;
            out[4] = (float)d;
            d = (double)b[9] * sf[0x150 / 4] + (double)b[5] * sf[0x140 / 4] + (double)b[1] * sf[0x160 / 4]
                + (double)b[10] * sf[0x154 / 4] + (double)b[6] * sf[0x144 / 4] + (double)b[2] * sf[0x164 / 4]
                + (double)b[11] * sf[0x158 / 4] + (double)b[7] * sf[0x148 / 4] + (double)b[4] * sf[0x16c / 4]
                + (double)b[3] * sf[0x168 / 4] + (double)b[8] * sf[0x14c / 4] + (double)b[0] * sf[0x15c / 4] + out[5];
            sf[0x158 / 4] = (float)d;
            out[5] = (float)d;
            d = (double)b[10] * sf[0x150 / 4] + (double)b[6] * sf[0x140 / 4] + (double)b[2] * sf[0x160 / 4]
                + (double)b[11] * sf[0x154 / 4] + (double)b[7] * sf[0x144 / 4] + (double)b[3] * sf[0x164 / 4]
                + (double)b[8] * sf[0x148 / 4] + (double)b[5] * sf[0x16c / 4] + (double)b[4] * sf[0x168 / 4]
                + (double)b[0] * sf[0x158 / 4] + (double)b[9] * sf[0x14c / 4] + (double)b[1] * sf[0x15c / 4] + out[6];
            sf[0x154 / 4] = (float)d;
            out[6] = (float)d;
            d = (double)b[11] * sf[0x150 / 4] + (double)b[7] * sf[0x140 / 4] + (double)b[3] * sf[0x160 / 4]
                + (double)b[8] * sf[0x144 / 4] + (double)b[4] * sf[0x164 / 4] + (double)b[0] * sf[0x154 / 4]
                + (double)b[9] * sf[0x148 / 4] + (double)b[6] * sf[0x16c / 4] + (double)b[5] * sf[0x168 / 4]
                + (double)b[1] * sf[0x158 / 4] + (double)b[10] * sf[0x14c / 4] + (double)b[2] * sf[0x15c / 4]
                + out[7];
            sf[0x150 / 4] = (float)d;
            out[7] = (float)d;
            d = (double)b[8] * sf[0x140 / 4] + (double)b[4] * sf[0x160 / 4] + (double)b[0] * sf[0x150 / 4]
                + (double)b[9] * sf[0x144 / 4] + (double)b[5] * sf[0x164 / 4] + (double)b[1] * sf[0x154 / 4]
                + (double)b[10] * sf[0x148 / 4] + (double)b[7] * sf[0x16c / 4] + (double)b[6] * sf[0x168 / 4]
                + (double)b[2] * sf[0x158 / 4] + (double)b[11] * sf[0x14c / 4] + (double)b[3] * sf[0x15c / 4]
                + out[8];
            sf[0x14c / 4] = (float)d;
            out[8] = (float)d;
            d = (double)b[9] * sf[0x140 / 4] + (double)b[5] * sf[0x160 / 4] + (double)b[1] * sf[0x150 / 4]
                + (double)b[10] * sf[0x144 / 4] + (double)b[6] * sf[0x164 / 4] + (double)b[2] * sf[0x154 / 4]
                + (double)b[11] * sf[0x148 / 4] + (double)b[8] * sf[0x16c / 4] + (double)b[7] * sf[0x168 / 4]
                + (double)b[3] * sf[0x158 / 4] + (double)b[4] * sf[0x15c / 4] + (double)b[0] * sf[0x14c / 4] + out[9];
            sf[0x148 / 4] = (float)d;
            out[9] = (float)d;
            d = (double)b[10] * sf[0x140 / 4] + (double)b[6] * sf[0x160 / 4] + (double)b[2] * sf[0x150 / 4]
                + (double)b[11] * sf[0x144 / 4] + (double)b[7] * sf[0x164 / 4] + (double)b[3] * sf[0x154 / 4]
                + (double)b[9] * sf[0x16c / 4] + (double)b[8] * sf[0x168 / 4] + (double)b[4] * sf[0x158 / 4]
                + (double)b[0] * sf[0x148 / 4] + (double)b[5] * sf[0x15c / 4] + (double)b[1] * sf[0x14c / 4]
                + out[10];
            sf[0x144 / 4] = (float)d;
            out[10] = (float)d;
            d = (double)b[11] * sf[0x140 / 4] + (double)b[7] * sf[0x160 / 4] + (double)b[3] * sf[0x150 / 4]
                + (double)b[8] * sf[0x164 / 4] + (double)b[4] * sf[0x154 / 4] + (double)b[0] * sf[0x144 / 4]
                + (double)b[10] * sf[0x16c / 4] + (double)b[9] * sf[0x168 / 4] + (double)b[5] * sf[0x158 / 4]
                + (double)b[1] * sf[0x148 / 4] + (double)b[6] * sf[0x15c / 4] + (double)b[2] * sf[0x14c / 4]
                + out[11];
            sf[0x140 / 4] = (float)d;
            out[11] = (float)d;
            d = (double)b[8] * sf[0x160 / 4] + (double)b[4] * sf[0x150 / 4] + (double)b[0] * sf[0x140 / 4]
                + (double)b[9] * sf[0x164 / 4] + (double)b[5] * sf[0x154 / 4] + (double)b[1] * sf[0x144 / 4]
                + (double)b[11] * sf[0x16c / 4] + (double)b[10] * sf[0x168 / 4] + (double)b[6] * sf[0x158 / 4]
                + (double)b[2] * sf[0x148 / 4] + (double)b[7] * sf[0x15c / 4] + (double)b[3] * sf[0x14c / 4]
                + out[12];
            sf[0x16c / 4] = (float)d;
            out[12] = (float)d;
            d = (double)b[9] * sf[0x160 / 4] + (double)b[5] * sf[0x150 / 4] + (double)b[1] * sf[0x140 / 4]
                + (double)b[10] * sf[0x164 / 4] + (double)b[6] * sf[0x154 / 4] + (double)b[2] * sf[0x144 / 4]
                + (double)b[11] * sf[0x168 / 4] + (double)b[7] * sf[0x158 / 4] + (double)b[3] * sf[0x148 / 4]
                + (double)b[0] * sf[0x16c / 4] + (double)b[8] * sf[0x15c / 4] + (double)b[4] * sf[0x14c / 4]
                + out[13];
            sf[0x168 / 4] = (float)d;
            out[13] = (float)d;
            d = (double)b[10] * sf[0x160 / 4] + (double)b[6] * sf[0x150 / 4] + (double)b[2] * sf[0x140 / 4]
                + (double)b[11] * sf[0x164 / 4] + (double)b[7] * sf[0x154 / 4] + (double)b[3] * sf[0x144 / 4]
                + (double)b[8] * sf[0x158 / 4] + (double)b[4] * sf[0x148 / 4] + (double)b[1] * sf[0x16c / 4]
                + (double)b[0] * sf[0x168 / 4] + (double)b[9] * sf[0x15c / 4] + (double)b[5] * sf[0x14c / 4]
                + out[14];
            sf[0x164 / 4] = (float)d;
            out[14] = (float)d;
            d = (double)b[11] * sf[0x160 / 4] + (double)b[7] * sf[0x150 / 4] + (double)b[3] * sf[0x140 / 4]
                + (double)b[8] * sf[0x154 / 4] + (double)b[4] * sf[0x144 / 4] + (double)b[0] * sf[0x164 / 4]
                + (double)b[9] * sf[0x158 / 4] + (double)b[5] * sf[0x148 / 4] + (double)b[2] * sf[0x16c / 4]
                + (double)b[1] * sf[0x168 / 4] + (double)b[10] * sf[0x15c / 4] + (double)b[6] * sf[0x14c / 4]
                + out[15];
            sf[0x160 / 4] = (float)d;
            out[15] = (float)d;
            d = (double)b[8] * sf[0x150 / 4] + (double)b[4] * sf[0x140 / 4] + (double)b[0] * sf[0x160 / 4]
                + (double)b[9] * sf[0x154 / 4] + (double)b[5] * sf[0x144 / 4] + (double)b[1] * sf[0x164 / 4]
                + (double)b[10] * sf[0x158 / 4] + (double)b[6] * sf[0x148 / 4] + (double)b[3] * sf[0x16c / 4]
                + (double)b[2] * sf[0x168 / 4] + (double)b[11] * sf[0x15c / 4] + (double)b[7] * sf[0x14c / 4]
                + out[16];
            sf[0x15c / 4] = (float)d;
            out[16] = (float)d;
            d = (double)b[9] * sf[0x150 / 4] + (double)b[5] * sf[0x140 / 4] + (double)b[1] * sf[0x160 / 4]
                + (double)b[10] * sf[0x154 / 4] + (double)b[6] * sf[0x144 / 4] + (double)b[2] * sf[0x164 / 4]
                + (double)b[11] * sf[0x158 / 4] + (double)b[7] * sf[0x148 / 4] + (double)b[4] * sf[0x16c / 4]
                + (double)b[3] * sf[0x168 / 4] + (double)b[8] * sf[0x14c / 4] + (double)b[0] * sf[0x15c / 4]
                + out[17];
            sf[0x158 / 4] = (float)d;
            out[17] = (float)d;
            d = (double)b[10] * sf[0x150 / 4] + (double)b[6] * sf[0x140 / 4] + (double)b[2] * sf[0x160 / 4]
                + (double)b[11] * sf[0x154 / 4] + (double)b[7] * sf[0x144 / 4] + (double)b[3] * sf[0x164 / 4]
                + (double)b[8] * sf[0x148 / 4] + (double)b[5] * sf[0x16c / 4] + (double)b[4] * sf[0x168 / 4]
                + (double)b[0] * sf[0x158 / 4] + (double)b[9] * sf[0x14c / 4] + (double)b[1] * sf[0x15c / 4]
                + out[18];
            sf[0x154 / 4] = (float)d;
            out[18] = (float)d;
            d = (double)b[11] * sf[0x150 / 4] + (double)b[7] * sf[0x140 / 4] + (double)b[3] * sf[0x160 / 4]
                + (double)b[8] * sf[0x144 / 4] + (double)b[4] * sf[0x164 / 4] + (double)b[0] * sf[0x154 / 4]
                + (double)b[9] * sf[0x148 / 4] + (double)b[6] * sf[0x16c / 4] + (double)b[5] * sf[0x168 / 4]
                + (double)b[1] * sf[0x158 / 4] + (double)b[10] * sf[0x14c / 4] + (double)b[2] * sf[0x15c / 4]
                + out[19];
            sf[0x150 / 4] = (float)d;
            out[19] = (float)d;
            d = (double)b[8] * sf[0x140 / 4] + (double)b[4] * sf[0x160 / 4] + (double)b[0] * sf[0x150 / 4]
                + (double)b[9] * sf[0x144 / 4] + (double)b[5] * sf[0x164 / 4] + (double)b[1] * sf[0x154 / 4]
                + (double)b[10] * sf[0x148 / 4] + (double)b[7] * sf[0x16c / 4] + (double)b[6] * sf[0x168 / 4]
                + (double)b[2] * sf[0x158 / 4] + (double)b[11] * sf[0x14c / 4] + (double)b[3] * sf[0x15c / 4]
                + out[20];
            sf[0x14c / 4] = (float)d;
            out[20] = (float)d;
            d = (double)b[9] * sf[0x140 / 4] + (double)b[5] * sf[0x160 / 4] + (double)b[1] * sf[0x150 / 4]
                + (double)b[10] * sf[0x144 / 4] + (double)b[6] * sf[0x164 / 4] + (double)b[2] * sf[0x154 / 4]
                + (double)b[11] * sf[0x148 / 4] + (double)b[8] * sf[0x16c / 4] + (double)b[7] * sf[0x168 / 4]
                + (double)b[3] * sf[0x158 / 4] + (double)b[4] * sf[0x15c / 4] + (double)b[0] * sf[0x14c / 4]
                + out[21];
            sf[0x148 / 4] = (float)d;
            out[21] = (float)d;
            d = (double)b[10] * sf[0x140 / 4] + (double)b[6] * sf[0x160 / 4] + (double)b[2] * sf[0x150 / 4]
                + (double)b[11] * sf[0x144 / 4] + (double)b[7] * sf[0x164 / 4] + (double)b[3] * sf[0x154 / 4]
                + (double)b[9] * sf[0x16c / 4] + (double)b[8] * sf[0x168 / 4] + (double)b[4] * sf[0x158 / 4]
                + (double)b[0] * sf[0x148 / 4] + (double)b[5] * sf[0x15c / 4] + (double)b[1] * sf[0x14c / 4]
                + out[22];
            sf[0x144 / 4] = (float)d;
            out[22] = (float)d;
            d = (double)b[11] * sf[0x140 / 4] + (double)b[7] * sf[0x160 / 4] + (double)b[3] * sf[0x150 / 4]
                + (double)b[8] * sf[0x164 / 4] + (double)b[4] * sf[0x154 / 4] + (double)b[0] * sf[0x144 / 4]
                + (double)b[10] * sf[0x16c / 4] + (double)b[9] * sf[0x168 / 4] + (double)b[5] * sf[0x158 / 4]
                + (double)b[1] * sf[0x148 / 4] + (double)b[6] * sf[0x15c / 4] + (double)b[2] * sf[0x14c / 4]
                + out[23];
            sf[0x140 / 4] = (float)d;
            out[23] = (float)d;
            d = (double)b[8] * sf[0x160 / 4] + (double)b[4] * sf[0x150 / 4] + (double)b[0] * sf[0x140 / 4]
                + (double)b[9] * sf[0x164 / 4] + (double)b[5] * sf[0x154 / 4] + (double)b[1] * sf[0x144 / 4]
                + (double)b[11] * sf[0x16c / 4] + (double)b[10] * sf[0x168 / 4] + (double)b[6] * sf[0x158 / 4]
                + (double)b[2] * sf[0x148 / 4] + (double)b[7] * sf[0x15c / 4] + (double)b[3] * sf[0x14c / 4]
                + out[24];
            sf[0x16c / 4] = (float)d;
            out[24] = (float)d;
            d = (double)b[9] * sf[0x160 / 4] + (double)b[5] * sf[0x150 / 4] + (double)b[1] * sf[0x140 / 4]
                + (double)b[10] * sf[0x164 / 4] + (double)b[6] * sf[0x154 / 4] + (double)b[2] * sf[0x144 / 4]
                + (double)b[11] * sf[0x168 / 4] + (double)b[7] * sf[0x158 / 4] + (double)b[3] * sf[0x148 / 4]
                + (double)b[0] * sf[0x16c / 4] + (double)b[8] * sf[0x15c / 4] + (double)b[4] * sf[0x14c / 4]
                + out[25];
            sf[0x168 / 4] = (float)d;
            out[25] = (float)d;
            d = (double)b[10] * sf[0x160 / 4] + (double)b[6] * sf[0x150 / 4] + (double)b[2] * sf[0x140 / 4]
                + (double)b[11] * sf[0x164 / 4] + (double)b[7] * sf[0x154 / 4] + (double)b[3] * sf[0x144 / 4]
                + (double)b[8] * sf[0x158 / 4] + (double)b[4] * sf[0x148 / 4] + (double)b[1] * sf[0x16c / 4]
                + (double)b[0] * sf[0x168 / 4] + (double)b[9] * sf[0x15c / 4] + (double)b[5] * sf[0x14c / 4]
                + out[26];
            sf[0x164 / 4] = (float)d;
            out[26] = (float)d;
            d = (double)b[11] * sf[0x160 / 4] + (double)b[7] * sf[0x150 / 4] + (double)b[3] * sf[0x140 / 4]
                + (double)b[8] * sf[0x154 / 4] + (double)b[4] * sf[0x144 / 4] + (double)b[0] * sf[0x164 / 4]
                + (double)b[9] * sf[0x158 / 4] + (double)b[5] * sf[0x148 / 4] + (double)b[2] * sf[0x16c / 4]
                + (double)b[1] * sf[0x168 / 4] + (double)b[10] * sf[0x15c / 4] + (double)b[6] * sf[0x14c / 4]
                + out[27];
            sf[0x160 / 4] = (float)d;
            out[27] = (float)d;
            d = (double)b[8] * sf[0x150 / 4] + (double)b[4] * sf[0x140 / 4] + (double)b[0] * sf[0x160 / 4]
                + (double)b[9] * sf[0x154 / 4] + (double)b[5] * sf[0x144 / 4] + (double)b[1] * sf[0x164 / 4]
                + (double)b[10] * sf[0x158 / 4] + (double)b[6] * sf[0x148 / 4] + (double)b[3] * sf[0x16c / 4]
                + (double)b[2] * sf[0x168 / 4] + (double)b[11] * sf[0x15c / 4] + (double)b[7] * sf[0x14c / 4]
                + out[28];
            sf[0x15c / 4] = (float)d;
            out[28] = (float)d;
            d = (double)b[9] * sf[0x150 / 4] + (double)b[5] * sf[0x140 / 4] + (double)b[1] * sf[0x160 / 4]
                + (double)b[10] * sf[0x154 / 4] + (double)b[6] * sf[0x144 / 4] + (double)b[2] * sf[0x164 / 4]
                + (double)b[11] * sf[0x158 / 4] + (double)b[7] * sf[0x148 / 4] + (double)b[4] * sf[0x16c / 4]
                + (double)b[3] * sf[0x168 / 4] + (double)b[8] * sf[0x14c / 4] + (double)b[0] * sf[0x15c / 4]
                + out[29];
            sf[0x158 / 4] = (float)d;
            out[29] = (float)d;
            d = (double)b[10] * sf[0x150 / 4] + (double)b[6] * sf[0x140 / 4] + (double)b[2] * sf[0x160 / 4]
                + (double)b[11] * sf[0x154 / 4] + (double)b[7] * sf[0x144 / 4] + (double)b[3] * sf[0x164 / 4]
                + (double)b[8] * sf[0x148 / 4] + (double)b[5] * sf[0x16c / 4] + (double)b[4] * sf[0x168 / 4]
                + (double)b[0] * sf[0x158 / 4] + (double)b[9] * sf[0x14c / 4] + (double)b[1] * sf[0x15c / 4]
                + out[30];
            sf[0x154 / 4] = (float)d;
            out[30] = (float)d;
            d = (double)b[11] * sf[0x150 / 4] + (double)b[7] * sf[0x140 / 4] + (double)b[3] * sf[0x160 / 4]
                + (double)b[8] * sf[0x144 / 4] + (double)b[4] * sf[0x164 / 4] + (double)b[0] * sf[0x154 / 4]
                + (double)b[9] * sf[0x148 / 4] + (double)b[6] * sf[0x16c / 4] + (double)b[5] * sf[0x168 / 4]
                + (double)b[1] * sf[0x158 / 4] + (double)b[10] * sf[0x14c / 4] + (double)b[2] * sf[0x15c / 4]
                + out[31];
            sf[0x150 / 4] = (float)d;
            out[31] = (float)d;
            d = (double)b[8] * sf[0x140 / 4] + (double)b[4] * sf[0x160 / 4] + (double)b[0] * sf[0x150 / 4]
                + (double)b[9] * sf[0x144 / 4] + (double)b[5] * sf[0x164 / 4] + (double)b[1] * sf[0x154 / 4]
                + (double)b[10] * sf[0x148 / 4] + (double)b[7] * sf[0x16c / 4] + (double)b[6] * sf[0x168 / 4]
                + (double)b[2] * sf[0x158 / 4] + (double)b[11] * sf[0x14c / 4] + (double)b[3] * sf[0x15c / 4]
                + out[32];
            sf[0x14c / 4] = (float)d;
            out[32] = (float)d;
            d = (double)b[9] * sf[0x140 / 4] + (double)b[5] * sf[0x160 / 4] + (double)b[1] * sf[0x150 / 4]
                + (double)b[10] * sf[0x144 / 4] + (double)b[6] * sf[0x164 / 4] + (double)b[2] * sf[0x154 / 4]
                + (double)b[11] * sf[0x148 / 4] + (double)b[8] * sf[0x16c / 4] + (double)b[7] * sf[0x168 / 4]
                + (double)b[3] * sf[0x158 / 4] + (double)b[4] * sf[0x15c / 4] + (double)b[0] * sf[0x14c / 4]
                + out[33];
            sf[0x148 / 4] = (float)d;
            out[33] = (float)d;
            d = (double)b[10] * sf[0x140 / 4] + (double)b[6] * sf[0x160 / 4] + (double)b[2] * sf[0x150 / 4]
                + (double)b[11] * sf[0x144 / 4] + (double)b[7] * sf[0x164 / 4] + (double)b[3] * sf[0x154 / 4]
                + (double)b[9] * sf[0x16c / 4] + (double)b[8] * sf[0x168 / 4] + (double)b[4] * sf[0x158 / 4]
                + (double)b[0] * sf[0x148 / 4] + (double)b[5] * sf[0x15c / 4] + (double)b[1] * sf[0x14c / 4]
                + out[34];
            sf[0x144 / 4] = (float)d;
            out[34] = (float)d;
            d = (double)b[11] * sf[0x140 / 4] + (double)b[7] * sf[0x160 / 4] + (double)b[3] * sf[0x150 / 4]
                + (double)b[8] * sf[0x164 / 4] + (double)b[4] * sf[0x154 / 4] + (double)b[0] * sf[0x144 / 4]
                + (double)b[10] * sf[0x16c / 4] + (double)b[9] * sf[0x168 / 4] + (double)b[5] * sf[0x158 / 4]
                + (double)b[1] * sf[0x148 / 4] + (double)b[6] * sf[0x15c / 4] + (double)b[2] * sf[0x14c / 4]
                + out[35];
            sf[0x140 / 4] = (float)d;
            out[35] = (float)d;
            d = (double)b[8] * sf[0x160 / 4] + (double)b[4] * sf[0x150 / 4] + (double)b[0] * sf[0x140 / 4]
                + (double)b[9] * sf[0x164 / 4] + (double)b[5] * sf[0x154 / 4] + (double)b[1] * sf[0x144 / 4]
                + (double)b[11] * sf[0x16c / 4] + (double)b[10] * sf[0x168 / 4] + (double)b[6] * sf[0x158 / 4]
                + (double)b[2] * sf[0x148 / 4] + (double)b[7] * sf[0x15c / 4] + (double)b[3] * sf[0x14c / 4]
                + out[36];
            sf[0x16c / 4] = (float)d;
            out[36] = (float)d;
            d = (double)b[9] * sf[0x160 / 4] + (double)b[5] * sf[0x150 / 4] + (double)b[1] * sf[0x140 / 4]
                + (double)b[10] * sf[0x164 / 4] + (double)b[6] * sf[0x154 / 4] + (double)b[2] * sf[0x144 / 4]
                + (double)b[11] * sf[0x168 / 4] + (double)b[7] * sf[0x158 / 4] + (double)b[3] * sf[0x148 / 4]
                + (double)b[0] * sf[0x16c / 4] + (double)b[8] * sf[0x15c / 4] + (double)b[4] * sf[0x14c / 4]
                + out[37];
            sf[0x168 / 4] = (float)d;
            out[37] = (float)d;
            d = (double)b[10] * sf[0x160 / 4] + (double)b[6] * sf[0x150 / 4] + (double)b[2] * sf[0x140 / 4]
                + (double)b[11] * sf[0x164 / 4] + (double)b[7] * sf[0x154 / 4] + (double)b[3] * sf[0x144 / 4]
                + (double)b[8] * sf[0x158 / 4] + (double)b[4] * sf[0x148 / 4] + (double)b[1] * sf[0x16c / 4]
                + (double)b[0] * sf[0x168 / 4] + (double)b[9] * sf[0x15c / 4] + (double)b[5] * sf[0x14c / 4]
                + out[38];
            sf[0x164 / 4] = (float)d;
            out[38] = (float)d;
            d = (double)b[11] * sf[0x160 / 4] + (double)b[7] * sf[0x150 / 4] + (double)b[3] * sf[0x140 / 4]
                + (double)b[8] * sf[0x154 / 4] + (double)b[4] * sf[0x144 / 4] + (double)b[0] * sf[0x164 / 4]
                + (double)b[9] * sf[0x158 / 4] + (double)b[5] * sf[0x148 / 4] + (double)b[2] * sf[0x16c / 4]
                + (double)b[1] * sf[0x168 / 4] + (double)b[10] * sf[0x15c / 4] + (double)b[6] * sf[0x14c / 4]
                + out[39];
            sf[0x160 / 4] = (float)d;
            out[39] = (float)d;
            d = (double)b[8] * sf[0x150 / 4] + (double)b[4] * sf[0x140 / 4] + (double)b[0] * sf[0x160 / 4]
                + (double)b[9] * sf[0x154 / 4] + (double)b[5] * sf[0x144 / 4] + (double)b[1] * sf[0x164 / 4]
                + (double)b[10] * sf[0x158 / 4] + (double)b[6] * sf[0x148 / 4] + (double)b[3] * sf[0x16c / 4]
                + (double)b[2] * sf[0x168 / 4] + (double)b[11] * sf[0x15c / 4] + (double)b[7] * sf[0x14c / 4]
                + out[40];
            sf[0x15c / 4] = (float)d;
            out[40] = (float)d;
            d = (double)b[9] * sf[0x150 / 4] + (double)b[5] * sf[0x140 / 4] + (double)b[1] * sf[0x160 / 4]
                + (double)b[10] * sf[0x154 / 4] + (double)b[6] * sf[0x144 / 4] + (double)b[2] * sf[0x164 / 4]
                + (double)b[11] * sf[0x158 / 4] + (double)b[7] * sf[0x148 / 4] + (double)b[4] * sf[0x16c / 4]
                + (double)b[3] * sf[0x168 / 4] + (double)b[8] * sf[0x14c / 4] + (double)b[0] * sf[0x15c / 4]
                + out[41];
            sf[0x158 / 4] = (float)d;
            out[41] = (float)d;
            d = (double)b[10] * sf[0x150 / 4] + (double)b[6] * sf[0x140 / 4] + (double)b[2] * sf[0x160 / 4]
                + (double)b[11] * sf[0x154 / 4] + (double)b[7] * sf[0x144 / 4] + (double)b[3] * sf[0x164 / 4]
                + (double)b[8] * sf[0x148 / 4] + (double)b[5] * sf[0x16c / 4] + (double)b[4] * sf[0x168 / 4]
                + (double)b[0] * sf[0x158 / 4] + (double)b[9] * sf[0x14c / 4] + (double)b[1] * sf[0x15c / 4]
                + out[42];
            sf[0x154 / 4] = (float)d;
            out[42] = (float)d;
            d = (double)b[11] * sf[0x150 / 4] + (double)b[7] * sf[0x140 / 4] + (double)b[3] * sf[0x160 / 4]
                + (double)b[8] * sf[0x144 / 4] + (double)b[4] * sf[0x164 / 4] + (double)b[0] * sf[0x154 / 4]
                + (double)b[9] * sf[0x148 / 4] + (double)b[6] * sf[0x16c / 4] + (double)b[5] * sf[0x168 / 4]
                + (double)b[1] * sf[0x158 / 4] + (double)b[10] * sf[0x14c / 4] + (double)b[2] * sf[0x15c / 4]
                + out[43];
            sf[0x150 / 4] = (float)d;
            out[43] = (float)d;
            d = (double)b[8] * sf[0x140 / 4] + (double)b[4] * sf[0x160 / 4] + (double)b[0] * sf[0x150 / 4]
                + (double)b[9] * sf[0x144 / 4] + (double)b[5] * sf[0x164 / 4] + (double)b[1] * sf[0x154 / 4]
                + (double)b[10] * sf[0x148 / 4] + (double)b[7] * sf[0x16c / 4] + (double)b[6] * sf[0x168 / 4]
                + (double)b[2] * sf[0x158 / 4] + (double)b[11] * sf[0x14c / 4] + (double)b[3] * sf[0x15c / 4]
                + out[44];
            sf[0x14c / 4] = (float)d;
            out[44] = (float)d;
            d = (double)b[9] * sf[0x140 / 4] + (double)b[5] * sf[0x160 / 4] + (double)b[1] * sf[0x150 / 4]
                + (double)b[10] * sf[0x144 / 4] + (double)b[6] * sf[0x164 / 4] + (double)b[2] * sf[0x154 / 4]
                + (double)b[11] * sf[0x148 / 4] + (double)b[8] * sf[0x16c / 4] + (double)b[7] * sf[0x168 / 4]
                + (double)b[3] * sf[0x158 / 4] + (double)b[4] * sf[0x15c / 4] + (double)b[0] * sf[0x14c / 4]
                + out[45];
            sf[0x148 / 4] = (float)d;
            out[45] = (float)d;
            d = (double)b[10] * sf[0x140 / 4] + (double)b[6] * sf[0x160 / 4] + (double)b[2] * sf[0x150 / 4]
                + (double)b[11] * sf[0x144 / 4] + (double)b[7] * sf[0x164 / 4] + (double)b[3] * sf[0x154 / 4]
                + (double)b[9] * sf[0x16c / 4] + (double)b[8] * sf[0x168 / 4] + (double)b[4] * sf[0x158 / 4]
                + (double)b[0] * sf[0x148 / 4] + (double)b[5] * sf[0x15c / 4] + (double)b[1] * sf[0x14c / 4]
                + out[46];
            sf[0x144 / 4] = (float)d;
            out[46] = (float)d;
            d = (double)b[11] * sf[0x140 / 4] + (double)b[7] * sf[0x160 / 4] + (double)b[3] * sf[0x150 / 4]
                + (double)b[8] * sf[0x164 / 4] + (double)b[4] * sf[0x154 / 4] + (double)b[0] * sf[0x144 / 4]
                + (double)b[10] * sf[0x16c / 4] + (double)b[9] * sf[0x168 / 4] + (double)b[5] * sf[0x158 / 4]
                + (double)b[1] * sf[0x148 / 4] + (double)b[6] * sf[0x15c / 4] + (double)b[2] * sf[0x14c / 4]
                + out[47];
            sf[0x140 / 4] = (float)d;
            out[47] = (float)d;
            out += 48;
            next += 4;
            done += 4;
        } while (next < groups);
    }
    if (done < groups) {
        int left = groups - done;
        do {
            d = (double)b[8] * sf[0x160 / 4] + (double)b[4] * sf[0x150 / 4] + (double)b[0] * sf[0x140 / 4]
                + (double)b[9] * sf[0x164 / 4] + (double)b[5] * sf[0x154 / 4] + (double)b[1] * sf[0x144 / 4]
                + (double)b[11] * sf[0x16c / 4] + (double)b[10] * sf[0x168 / 4] + (double)b[6] * sf[0x158 / 4]
                + (double)b[2] * sf[0x148 / 4] + (double)b[7] * sf[0x15c / 4] + (double)b[3] * sf[0x14c / 4] + out[0];
            sf[0x16c / 4] = (float)d;
            out[0] = (float)d;
            d = (double)b[9] * sf[0x160 / 4] + (double)b[5] * sf[0x150 / 4] + (double)b[1] * sf[0x140 / 4]
                + (double)b[10] * sf[0x164 / 4] + (double)b[6] * sf[0x154 / 4] + (double)b[2] * sf[0x144 / 4]
                + (double)b[11] * sf[0x168 / 4] + (double)b[7] * sf[0x158 / 4] + (double)b[3] * sf[0x148 / 4]
                + (double)b[0] * sf[0x16c / 4] + (double)b[8] * sf[0x15c / 4] + (double)b[4] * sf[0x14c / 4] + out[1];
            sf[0x168 / 4] = (float)d;
            out[1] = (float)d;
            d = (double)b[10] * sf[0x160 / 4] + (double)b[6] * sf[0x150 / 4] + (double)b[2] * sf[0x140 / 4]
                + (double)b[11] * sf[0x164 / 4] + (double)b[7] * sf[0x154 / 4] + (double)b[3] * sf[0x144 / 4]
                + (double)b[8] * sf[0x158 / 4] + (double)b[4] * sf[0x148 / 4] + (double)b[1] * sf[0x16c / 4]
                + (double)b[0] * sf[0x168 / 4] + (double)b[9] * sf[0x15c / 4] + (double)b[5] * sf[0x14c / 4] + out[2];
            sf[0x164 / 4] = (float)d;
            out[2] = (float)d;
            d = (double)b[11] * sf[0x160 / 4] + (double)b[7] * sf[0x150 / 4] + (double)b[3] * sf[0x140 / 4]
                + (double)b[8] * sf[0x154 / 4] + (double)b[4] * sf[0x144 / 4] + (double)b[0] * sf[0x164 / 4]
                + (double)b[9] * sf[0x158 / 4] + (double)b[5] * sf[0x148 / 4] + (double)b[2] * sf[0x16c / 4]
                + (double)b[1] * sf[0x168 / 4] + (double)b[10] * sf[0x15c / 4] + (double)b[6] * sf[0x14c / 4]
                + out[3];
            sf[0x160 / 4] = (float)d;
            out[3] = (float)d;
            d = (double)b[8] * sf[0x150 / 4] + (double)b[4] * sf[0x140 / 4] + (double)b[0] * sf[0x160 / 4]
                + (double)b[9] * sf[0x154 / 4] + (double)b[5] * sf[0x144 / 4] + (double)b[1] * sf[0x164 / 4]
                + (double)b[10] * sf[0x158 / 4] + (double)b[6] * sf[0x148 / 4] + (double)b[3] * sf[0x16c / 4]
                + (double)b[2] * sf[0x168 / 4] + (double)b[11] * sf[0x15c / 4] + (double)b[7] * sf[0x14c / 4]
                + out[4];
            sf[0x15c / 4] = (float)d;
            out[4] = (float)d;
            d = (double)b[9] * sf[0x150 / 4] + (double)b[5] * sf[0x140 / 4] + (double)b[1] * sf[0x160 / 4]
                + (double)b[10] * sf[0x154 / 4] + (double)b[6] * sf[0x144 / 4] + (double)b[2] * sf[0x164 / 4]
                + (double)b[11] * sf[0x158 / 4] + (double)b[7] * sf[0x148 / 4] + (double)b[4] * sf[0x16c / 4]
                + (double)b[3] * sf[0x168 / 4] + (double)b[8] * sf[0x14c / 4] + (double)b[0] * sf[0x15c / 4] + out[5];
            sf[0x158 / 4] = (float)d;
            out[5] = (float)d;
            d = (double)b[10] * sf[0x150 / 4] + (double)b[6] * sf[0x140 / 4] + (double)b[2] * sf[0x160 / 4]
                + (double)b[11] * sf[0x154 / 4] + (double)b[7] * sf[0x144 / 4] + (double)b[3] * sf[0x164 / 4]
                + (double)b[8] * sf[0x148 / 4] + (double)b[5] * sf[0x16c / 4] + (double)b[4] * sf[0x168 / 4]
                + (double)b[0] * sf[0x158 / 4] + (double)b[9] * sf[0x14c / 4] + (double)b[1] * sf[0x15c / 4] + out[6];
            sf[0x154 / 4] = (float)d;
            out[6] = (float)d;
            d = (double)b[11] * sf[0x150 / 4] + (double)b[7] * sf[0x140 / 4] + (double)b[3] * sf[0x160 / 4]
                + (double)b[8] * sf[0x144 / 4] + (double)b[4] * sf[0x164 / 4] + (double)b[0] * sf[0x154 / 4]
                + (double)b[9] * sf[0x148 / 4] + (double)b[6] * sf[0x16c / 4] + (double)b[5] * sf[0x168 / 4]
                + (double)b[1] * sf[0x158 / 4] + (double)b[10] * sf[0x14c / 4] + (double)b[2] * sf[0x15c / 4]
                + out[7];
            sf[0x150 / 4] = (float)d;
            out[7] = (float)d;
            d = (double)b[8] * sf[0x140 / 4] + (double)b[4] * sf[0x160 / 4] + (double)b[0] * sf[0x150 / 4]
                + (double)b[9] * sf[0x144 / 4] + (double)b[5] * sf[0x164 / 4] + (double)b[1] * sf[0x154 / 4]
                + (double)b[10] * sf[0x148 / 4] + (double)b[7] * sf[0x16c / 4] + (double)b[6] * sf[0x168 / 4]
                + (double)b[2] * sf[0x158 / 4] + (double)b[11] * sf[0x14c / 4] + (double)b[3] * sf[0x15c / 4]
                + out[8];
            sf[0x14c / 4] = (float)d;
            out[8] = (float)d;
            d = (double)b[9] * sf[0x140 / 4] + (double)b[5] * sf[0x160 / 4] + (double)b[1] * sf[0x150 / 4]
                + (double)b[10] * sf[0x144 / 4] + (double)b[6] * sf[0x164 / 4] + (double)b[2] * sf[0x154 / 4]
                + (double)b[11] * sf[0x148 / 4] + (double)b[8] * sf[0x16c / 4] + (double)b[7] * sf[0x168 / 4]
                + (double)b[3] * sf[0x158 / 4] + (double)b[4] * sf[0x15c / 4] + (double)b[0] * sf[0x14c / 4] + out[9];
            sf[0x148 / 4] = (float)d;
            out[9] = (float)d;
            d = (double)b[10] * sf[0x140 / 4] + (double)b[6] * sf[0x160 / 4] + (double)b[2] * sf[0x150 / 4]
                + (double)b[11] * sf[0x144 / 4] + (double)b[7] * sf[0x164 / 4] + (double)b[3] * sf[0x154 / 4]
                + (double)b[9] * sf[0x16c / 4] + (double)b[8] * sf[0x168 / 4] + (double)b[4] * sf[0x158 / 4]
                + (double)b[0] * sf[0x148 / 4] + (double)b[5] * sf[0x15c / 4] + (double)b[1] * sf[0x14c / 4]
                + out[10];
            sf[0x144 / 4] = (float)d;
            out[10] = (float)d;
            d = (double)b[11] * sf[0x140 / 4] + (double)b[7] * sf[0x160 / 4] + (double)b[3] * sf[0x150 / 4]
                + (double)b[8] * sf[0x164 / 4] + (double)b[4] * sf[0x154 / 4] + (double)b[0] * sf[0x144 / 4]
                + (double)b[10] * sf[0x16c / 4] + (double)b[9] * sf[0x168 / 4] + (double)b[5] * sf[0x158 / 4]
                + (double)b[1] * sf[0x148 / 4] + (double)b[6] * sf[0x15c / 4] + (double)b[2] * sf[0x14c / 4]
                + out[11];
            sf[0x140 / 4] = (float)d;
            out[11] = (float)d;
            out += 12;
        } while (--left != 0);
    }
}

// 0x00146f00: EAX = first output index, ESI = the state, [esp+4] = groups of 12; keeps EBX, ESI, EDI and EBP.
// AUTOLTCG
__declspec(naked) void FUN_00146f00() {
    __asm {
        push dword ptr [esp + 4]
        push esi
        push eax
        call MutSynthesise
        add esp, 12
        ret
    }
}

// One frame: the coefficients, four subframes of excitation into signal[324..755], the history moved down, then
// the synthesis filter over the frame in four steps.
// FUNC_AT(0x00149500)
void decodemut(SND::MutState *s) {
    SND_UNTESTED("decodemut");
    float K[12];                       // the coefficients' per-step increments (the original's [esp+0x20])
    float w[118];                      // [esp+0x58..0x22f]: 5 zeros, 108 samples from w[5], 5 zeros
    float gainA, gainB;
    uint32_t a = MutBits(s, 6);
    int flag = (int32_t)a < s->threshold;
    K[0] = (float)(((double)MutCoefs6[a] - (double)s->coefs[0]) * (double)kQuarter);
    for (int i = 1; i < 4; i++) {
        a = MutBits(s, 6);
        K[i] = (float)(((double)MutCoefs6[a] - (double)s->coefs[i]) * (double)kQuarter);
    }
    for (int i = 4; i < 12; i++) {
        a = MutBits(s, 5);
        K[i] = (float)(((double)MutCoefs5[a] - (double)s->coefs[i]) * (double)kQuarter);
    }
    float *to = (float *)((uint8_t *)s + 0x684);
    for (int j = 0xd8; j < 0x288; j += 0x6c) {
        uint32_t back = MutBits(s, 8);
        int32_t lag = j - (int32_t)back;
        uint32_t ltpGain = MutBits(s, 4);
        gainB = (float)(UnsignedToDouble(ltpGain) * (double)kLtpGainStep);
        uint32_t gain = MutBits(s, 6);
        gainA = s->gainTable[gain];
        uint32_t phase = MutBits(s, 1);
        uint32_t sparse = MutBits(s, 1);
        readsamples(s, flag, &w[5 + phase]);
        if (sparse != 0) {
            for (int k = 0; k < 54; k++)
                StoreBits(&w[6 - phase + k * 2], 0);
        } else {
            for (int k = 0; k < 5; k++) {
                StoreBits(&w[113 + k], 0);
                StoreBits(&w[k], 0);
            }
            float *x = &w[6 - phase];
            for (int k = 0; k < 54; k++) {
                double v = ((double)x[2 * k - 5] + (double)x[2 * k + 5]) * (double)kHalfBand0;
                v = v - ((double)x[2 * k - 3] + (double)x[2 * k + 3]) * (double)kHalfBand1;
                v = v + ((double)x[2 * k - 1] + (double)x[2 * k + 1]) * (double)kHalfBand2;
                x[2 * k] = (float)v;
            }
            gainA = (float)((double)gainA * (double)kHalf);
        }
        float *history = s->signal + lag;
        for (int i = 0; i < 0x6c; i++)
            to[i - 1] = (float)((double)gainA * (double)w[5 + i] + (double)gainB * (double)history[i]);
        to += 0x6c;
    }
    for (int i = 0; i < 324; i++)
        memcpy(&s->signal[i], &s->signal[432 + i], 4);
    static const int kFirst[4] = { 0, 12, 24, 36 };
    static const int kGroups[4] = { 1, 1, 1, 33 };
    for (int step = 0; step < 4; step++) {
        for (int i = 0; i < 12; i++)
            s->coefs[i] = (float)((double)K[i] + (double)s->coefs[i]);
        MutSynthesise(kFirst[step], s, kGroups[step]);
    }
}

// int16 -> float from the end: one at a time until the count is a multiple of 8 (stopping at 0), then blocks of
// eight, loaded first and stored in the original's order, while the count stays above 0.
// FUNC_AT(0x0014a1e0)
void decode16x87(uint32_t count, const int16_t *in, float *out) {
    SND_UNTESTED("decode16x87");
    int32_t n = (int32_t)count;
    while ((n & 7) != 0) {
        out[n - 1] = (float)in[n - 1];
        n -= 1;
        if (n == 0)
            return;
    }
    do {
        int16_t v0 = in[n - 8], v1 = in[n - 7], v2 = in[n - 6], v3 = in[n - 5];
        int16_t v4 = in[n - 4], v5 = in[n - 3], v6 = in[n - 2], v7 = in[n - 1];
        out[n - 8] = (float)v0;
        out[n - 2] = (float)v6;
        out[n - 3] = (float)v5;
        out[n - 4] = (float)v4;
        out[n - 5] = (float)v3;
        out[n - 6] = (float)v2;
        out[n - 7] = (float)v1;
        out[n - 1] = (float)v7;
        n -= 8;
    } while (n > 0);
}
