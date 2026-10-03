#include "DecodeUnused.h"
#include "SndUntested.h"

#include <bit>
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
    float value;                       // +8 the sample (codes 4 and up)
};
static_assert(sizeof(MutCode) == 12, "a MicroTalk code is 12 bytes");

// The tables, in the original's .rdata
#define MutCoefs6 ((const float *)0x001da558)              // the first four coefficients' 64 levels
#define MutCoefs5 ((const float *)0x001da598)              // the other eight's 32 levels
#define MutHuffStart ((const uint8_t (*)[256])0x001da658)  // [state][next 8 bits] -> code
#define MutHuffCodes ((const MutCode *)0x001da858)

// The constants (the original's .rdata values)
constexpr float kEight = 8.0f;
constexpr float kGainStep = 0.001f;
constexpr float kGainBase = 1.04f;
constexpr float kQuarter = 0.25f;
constexpr float kLtpGainStep = 1.0f / 15;
constexpr float kHalfBand0 = 0x1.27728ap-6f;          // the half-band filter's taps at +-5
constexpr float kHalfBand1 = 0x1.d55df6p-4f;          // +-3
constexpr float kHalfBand2 = 0x1.31dc92p-1f;          // +-1
constexpr float kHalf = 0.5f;
static_assert(std::bit_cast<uint32_t>(kGainStep) == 0x3a83126f, "the original's 0.001");
static_assert(std::bit_cast<uint32_t>(kGainBase) == 0x3f851eb8, "the original's 1.04");
static_assert(std::bit_cast<uint32_t>(kLtpGainStep) == 0x3d888889, "the original's 1/15");
static_assert(std::bit_cast<uint32_t>(kHalfBand0) == 0x3c93b945, "the original's tap at +-5");
static_assert(std::bit_cast<uint32_t>(kHalfBand1) == 0x3deaaefb, "the original's tap at +-3");
static_assert(std::bit_cast<uint32_t>(kHalfBand2) == 0x3f18ee49, "the original's tap at +-1");

// The original's inlined reader: drop n bits, then top up one byte if fewer than 8 are left.
inline void MutSkip(SND::MutState *s, int n) {
    uint32_t bits = s->bits >> (n & 31);
    int32_t count = s->count - n;
    s->bits = bits;
    s->count = count;
    if (count < 8) {
        s->bits = (uint32_t(*s->ptr) << (count & 31)) | bits;
        s->ptr++;
        s->count = count + 8;
    }
}

// Take n bits (n <= 8: the original masks with a table of (1 << n) - 1).
inline uint32_t MutBits(SND::MutState *s, int n) {
    uint32_t value = s->bits & ((1u << n) - 1);
    MutSkip(s, n);
    return value;
}

}   // namespace

// The frame header: a bit skipped, the readsamples threshold (4 bits), the gain table's g0 = (n + 1) x 8 (4 bits)
// and step = n x 0.001 + 1.04 (6 bits); clears the coefficients, the filter and the history. Returns 0 (EAX after
// the original's REP STOSD), which SFILTER_unpackmtfinit passes on as its own result.
// FUNC_AT(0x001493e0)
int initmut(const uint8_t *src, SND::MutState *s) {
    SND_UNTESTED("initmut");
    s->bits = src[0];
    s->ptr = src + 1;
    s->count = 8;
    MutBits(s, 1);
    s->threshold = 32 - int32_t(MutBits(s, 4));
    // The original converts the two fields with FILD and its unsigned correction (+ 2^32 for a negative value):
    // exact, so a plain conversion gives the same doubles.
    uint32_t g0 = MutBits(s, 4) + 1;
    uint32_t stepField = MutBits(s, 6);
    s->gainTable[0] = float(double(g0) * kEight);
    double step = double(stepField) * kGainStep + kGainBase;
    for (int i = 0; i < 63; i++)
        s->gainTable[i + 1] = float(step * s->gainTable[i]);
    for (int i = 0; i < 12; i++) {
        s->coefs[i] = 0.0f;
        s->synth[i] = 0.0f;
    }
    memset(s->signal, 0, 324 * sizeof(float));
    return 0;
}

// 54 fixed-codebook samples into out[0], out[2] ... out[106]. Mode 0: two bits per sample, 01 = -2, 11 = +2,
// otherwise one bit, 0. Else a two-state code: a float for codes 4 and up, a run of 7 + (6 bits) zeros for codes 2
// and 3 (clipped at the end), and for codes 0 and 1 an escape: 7 + the number of 1 bits before a 0, then a sign.
// FUNC_AT(0x00146bb0)
void readsamples(SND::MutState *s, int mode, float *out) {
    SND_UNTESTED("readsamples");
    if (mode == 0) {
        for (int i = 0; i < 108; i += 2) {
            uint32_t low = s->bits & 3;
            if (low == 1) {
                out[i] = -2.0f;
                MutSkip(s, 2);
            } else if (low == 3) {
                out[i] = 2.0f;
                MutSkip(s, 2);
            } else {
                out[i] = 0.0f;
                MutSkip(s, 1);
            }
        }
        return;
    }
    int32_t state = 0;
    int i = 0;
    while (i < 108) {
        int code = MutHuffStart[state][s->bits & 0xff];
        state = MutHuffCodes[code].next;
        MutSkip(s, MutHuffCodes[code].length);
        if (code > 3) {
            out[i] = MutHuffCodes[code].value;
            i += 2;
        } else if (code > 1) {
            int32_t run = int32_t(MutBits(s, 6)) + 7;
            if (i + run * 2 > 108)
                run = (108 - i) >> 1;
            if (run <= 0)
                continue;
            do {
                out[i] = 0.0f;
                i += 2;
            } while (--run != 0);
        } else {
            int32_t magnitude = 7;
            while (MutBits(s, 1) == 1)
                magnitude++;
            if (MutBits(s, 1) == 1)
                out[i] = float(magnitude);
            else
                out[i] = float(-magnitude);
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
        double acc = -(double(L[11]) * k[11]);
        for (int c = 10; c >= 0; c--) {
            acc = acc - double(L[c]) * k[c];
            L[c + 1] = float(acc * k[c] + L[c]);
        }
        L[0] = float(acc);
        R[n] = float(acc);
        int j = 0;
        if (n >= 4) {
            int e = 3;
            do {
                acc = acc - double(R[n - 1 - j]) * b[j];
                acc = acc - double(b[j + 1]) * R[n - 2 - j];
                acc = acc - double(R[n - 3 - j]) * b[j + 2];
                acc = acc - double(R[n - 4 - j]) * b[j + 3];
                e += 4;
                j += 4;
            } while (e < n);
        }
        for (; j < n; j++)
            acc = acc - double(b[j]) * R[n - 1 - j];
        b[n] = float(acc);
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
    float *ring = s->synth;
    float *out = &s->signal[324 + first];   // the frame's outputs
    double d;
    int done = 0;
    if (groups >= 4) {
        int next = 3;
        do {
            d = double(b[8]) * ring[8] + double(b[4]) * ring[4] + double(b[0]) * ring[0]
                + double(b[9]) * ring[9] + double(b[5]) * ring[5] + double(b[1]) * ring[1]
                + double(b[11]) * ring[11] + double(b[10]) * ring[10] + double(b[6]) * ring[6]
                + double(b[2]) * ring[2] + double(b[7]) * ring[7] + double(b[3]) * ring[3] + out[0];
            ring[11] = float(d);
            out[0] = float(d);
            d = double(b[9]) * ring[8] + double(b[5]) * ring[4] + double(b[1]) * ring[0]
                + double(b[10]) * ring[9] + double(b[6]) * ring[5] + double(b[2]) * ring[1]
                + double(b[11]) * ring[10] + double(b[7]) * ring[6] + double(b[3]) * ring[2]
                + double(b[0]) * ring[11] + double(b[8]) * ring[7] + double(b[4]) * ring[3] + out[1];
            ring[10] = float(d);
            out[1] = float(d);
            d = double(b[10]) * ring[8] + double(b[6]) * ring[4] + double(b[2]) * ring[0]
                + double(b[11]) * ring[9] + double(b[7]) * ring[5] + double(b[3]) * ring[1]
                + double(b[8]) * ring[6] + double(b[4]) * ring[2] + double(b[1]) * ring[11]
                + double(b[0]) * ring[10] + double(b[9]) * ring[7] + double(b[5]) * ring[3] + out[2];
            ring[9] = float(d);
            out[2] = float(d);
            d = double(b[11]) * ring[8] + double(b[7]) * ring[4] + double(b[3]) * ring[0]
                + double(b[8]) * ring[5] + double(b[4]) * ring[1] + double(b[0]) * ring[9]
                + double(b[9]) * ring[6] + double(b[5]) * ring[2] + double(b[2]) * ring[11]
                + double(b[1]) * ring[10] + double(b[10]) * ring[7] + double(b[6]) * ring[3]
                + out[3];
            ring[8] = float(d);
            out[3] = float(d);
            d = double(b[8]) * ring[4] + double(b[4]) * ring[0] + double(b[0]) * ring[8]
                + double(b[9]) * ring[5] + double(b[5]) * ring[1] + double(b[1]) * ring[9]
                + double(b[10]) * ring[6] + double(b[6]) * ring[2] + double(b[3]) * ring[11]
                + double(b[2]) * ring[10] + double(b[11]) * ring[7] + double(b[7]) * ring[3]
                + out[4];
            ring[7] = float(d);
            out[4] = float(d);
            d = double(b[9]) * ring[4] + double(b[5]) * ring[0] + double(b[1]) * ring[8]
                + double(b[10]) * ring[5] + double(b[6]) * ring[1] + double(b[2]) * ring[9]
                + double(b[11]) * ring[6] + double(b[7]) * ring[2] + double(b[4]) * ring[11]
                + double(b[3]) * ring[10] + double(b[8]) * ring[3] + double(b[0]) * ring[7] + out[5];
            ring[6] = float(d);
            out[5] = float(d);
            d = double(b[10]) * ring[4] + double(b[6]) * ring[0] + double(b[2]) * ring[8]
                + double(b[11]) * ring[5] + double(b[7]) * ring[1] + double(b[3]) * ring[9]
                + double(b[8]) * ring[2] + double(b[5]) * ring[11] + double(b[4]) * ring[10]
                + double(b[0]) * ring[6] + double(b[9]) * ring[3] + double(b[1]) * ring[7] + out[6];
            ring[5] = float(d);
            out[6] = float(d);
            d = double(b[11]) * ring[4] + double(b[7]) * ring[0] + double(b[3]) * ring[8]
                + double(b[8]) * ring[1] + double(b[4]) * ring[9] + double(b[0]) * ring[5]
                + double(b[9]) * ring[2] + double(b[6]) * ring[11] + double(b[5]) * ring[10]
                + double(b[1]) * ring[6] + double(b[10]) * ring[3] + double(b[2]) * ring[7]
                + out[7];
            ring[4] = float(d);
            out[7] = float(d);
            d = double(b[8]) * ring[0] + double(b[4]) * ring[8] + double(b[0]) * ring[4]
                + double(b[9]) * ring[1] + double(b[5]) * ring[9] + double(b[1]) * ring[5]
                + double(b[10]) * ring[2] + double(b[7]) * ring[11] + double(b[6]) * ring[10]
                + double(b[2]) * ring[6] + double(b[11]) * ring[3] + double(b[3]) * ring[7]
                + out[8];
            ring[3] = float(d);
            out[8] = float(d);
            d = double(b[9]) * ring[0] + double(b[5]) * ring[8] + double(b[1]) * ring[4]
                + double(b[10]) * ring[1] + double(b[6]) * ring[9] + double(b[2]) * ring[5]
                + double(b[11]) * ring[2] + double(b[8]) * ring[11] + double(b[7]) * ring[10]
                + double(b[3]) * ring[6] + double(b[4]) * ring[7] + double(b[0]) * ring[3] + out[9];
            ring[2] = float(d);
            out[9] = float(d);
            d = double(b[10]) * ring[0] + double(b[6]) * ring[8] + double(b[2]) * ring[4]
                + double(b[11]) * ring[1] + double(b[7]) * ring[9] + double(b[3]) * ring[5]
                + double(b[9]) * ring[11] + double(b[8]) * ring[10] + double(b[4]) * ring[6]
                + double(b[0]) * ring[2] + double(b[5]) * ring[7] + double(b[1]) * ring[3]
                + out[10];
            ring[1] = float(d);
            out[10] = float(d);
            d = double(b[11]) * ring[0] + double(b[7]) * ring[8] + double(b[3]) * ring[4]
                + double(b[8]) * ring[9] + double(b[4]) * ring[5] + double(b[0]) * ring[1]
                + double(b[10]) * ring[11] + double(b[9]) * ring[10] + double(b[5]) * ring[6]
                + double(b[1]) * ring[2] + double(b[6]) * ring[7] + double(b[2]) * ring[3]
                + out[11];
            ring[0] = float(d);
            out[11] = float(d);
            d = double(b[8]) * ring[8] + double(b[4]) * ring[4] + double(b[0]) * ring[0]
                + double(b[9]) * ring[9] + double(b[5]) * ring[5] + double(b[1]) * ring[1]
                + double(b[11]) * ring[11] + double(b[10]) * ring[10] + double(b[6]) * ring[6]
                + double(b[2]) * ring[2] + double(b[7]) * ring[7] + double(b[3]) * ring[3]
                + out[12];
            ring[11] = float(d);
            out[12] = float(d);
            d = double(b[9]) * ring[8] + double(b[5]) * ring[4] + double(b[1]) * ring[0]
                + double(b[10]) * ring[9] + double(b[6]) * ring[5] + double(b[2]) * ring[1]
                + double(b[11]) * ring[10] + double(b[7]) * ring[6] + double(b[3]) * ring[2]
                + double(b[0]) * ring[11] + double(b[8]) * ring[7] + double(b[4]) * ring[3]
                + out[13];
            ring[10] = float(d);
            out[13] = float(d);
            d = double(b[10]) * ring[8] + double(b[6]) * ring[4] + double(b[2]) * ring[0]
                + double(b[11]) * ring[9] + double(b[7]) * ring[5] + double(b[3]) * ring[1]
                + double(b[8]) * ring[6] + double(b[4]) * ring[2] + double(b[1]) * ring[11]
                + double(b[0]) * ring[10] + double(b[9]) * ring[7] + double(b[5]) * ring[3]
                + out[14];
            ring[9] = float(d);
            out[14] = float(d);
            d = double(b[11]) * ring[8] + double(b[7]) * ring[4] + double(b[3]) * ring[0]
                + double(b[8]) * ring[5] + double(b[4]) * ring[1] + double(b[0]) * ring[9]
                + double(b[9]) * ring[6] + double(b[5]) * ring[2] + double(b[2]) * ring[11]
                + double(b[1]) * ring[10] + double(b[10]) * ring[7] + double(b[6]) * ring[3]
                + out[15];
            ring[8] = float(d);
            out[15] = float(d);
            d = double(b[8]) * ring[4] + double(b[4]) * ring[0] + double(b[0]) * ring[8]
                + double(b[9]) * ring[5] + double(b[5]) * ring[1] + double(b[1]) * ring[9]
                + double(b[10]) * ring[6] + double(b[6]) * ring[2] + double(b[3]) * ring[11]
                + double(b[2]) * ring[10] + double(b[11]) * ring[7] + double(b[7]) * ring[3]
                + out[16];
            ring[7] = float(d);
            out[16] = float(d);
            d = double(b[9]) * ring[4] + double(b[5]) * ring[0] + double(b[1]) * ring[8]
                + double(b[10]) * ring[5] + double(b[6]) * ring[1] + double(b[2]) * ring[9]
                + double(b[11]) * ring[6] + double(b[7]) * ring[2] + double(b[4]) * ring[11]
                + double(b[3]) * ring[10] + double(b[8]) * ring[3] + double(b[0]) * ring[7]
                + out[17];
            ring[6] = float(d);
            out[17] = float(d);
            d = double(b[10]) * ring[4] + double(b[6]) * ring[0] + double(b[2]) * ring[8]
                + double(b[11]) * ring[5] + double(b[7]) * ring[1] + double(b[3]) * ring[9]
                + double(b[8]) * ring[2] + double(b[5]) * ring[11] + double(b[4]) * ring[10]
                + double(b[0]) * ring[6] + double(b[9]) * ring[3] + double(b[1]) * ring[7]
                + out[18];
            ring[5] = float(d);
            out[18] = float(d);
            d = double(b[11]) * ring[4] + double(b[7]) * ring[0] + double(b[3]) * ring[8]
                + double(b[8]) * ring[1] + double(b[4]) * ring[9] + double(b[0]) * ring[5]
                + double(b[9]) * ring[2] + double(b[6]) * ring[11] + double(b[5]) * ring[10]
                + double(b[1]) * ring[6] + double(b[10]) * ring[3] + double(b[2]) * ring[7]
                + out[19];
            ring[4] = float(d);
            out[19] = float(d);
            d = double(b[8]) * ring[0] + double(b[4]) * ring[8] + double(b[0]) * ring[4]
                + double(b[9]) * ring[1] + double(b[5]) * ring[9] + double(b[1]) * ring[5]
                + double(b[10]) * ring[2] + double(b[7]) * ring[11] + double(b[6]) * ring[10]
                + double(b[2]) * ring[6] + double(b[11]) * ring[3] + double(b[3]) * ring[7]
                + out[20];
            ring[3] = float(d);
            out[20] = float(d);
            d = double(b[9]) * ring[0] + double(b[5]) * ring[8] + double(b[1]) * ring[4]
                + double(b[10]) * ring[1] + double(b[6]) * ring[9] + double(b[2]) * ring[5]
                + double(b[11]) * ring[2] + double(b[8]) * ring[11] + double(b[7]) * ring[10]
                + double(b[3]) * ring[6] + double(b[4]) * ring[7] + double(b[0]) * ring[3]
                + out[21];
            ring[2] = float(d);
            out[21] = float(d);
            d = double(b[10]) * ring[0] + double(b[6]) * ring[8] + double(b[2]) * ring[4]
                + double(b[11]) * ring[1] + double(b[7]) * ring[9] + double(b[3]) * ring[5]
                + double(b[9]) * ring[11] + double(b[8]) * ring[10] + double(b[4]) * ring[6]
                + double(b[0]) * ring[2] + double(b[5]) * ring[7] + double(b[1]) * ring[3]
                + out[22];
            ring[1] = float(d);
            out[22] = float(d);
            d = double(b[11]) * ring[0] + double(b[7]) * ring[8] + double(b[3]) * ring[4]
                + double(b[8]) * ring[9] + double(b[4]) * ring[5] + double(b[0]) * ring[1]
                + double(b[10]) * ring[11] + double(b[9]) * ring[10] + double(b[5]) * ring[6]
                + double(b[1]) * ring[2] + double(b[6]) * ring[7] + double(b[2]) * ring[3]
                + out[23];
            ring[0] = float(d);
            out[23] = float(d);
            d = double(b[8]) * ring[8] + double(b[4]) * ring[4] + double(b[0]) * ring[0]
                + double(b[9]) * ring[9] + double(b[5]) * ring[5] + double(b[1]) * ring[1]
                + double(b[11]) * ring[11] + double(b[10]) * ring[10] + double(b[6]) * ring[6]
                + double(b[2]) * ring[2] + double(b[7]) * ring[7] + double(b[3]) * ring[3]
                + out[24];
            ring[11] = float(d);
            out[24] = float(d);
            d = double(b[9]) * ring[8] + double(b[5]) * ring[4] + double(b[1]) * ring[0]
                + double(b[10]) * ring[9] + double(b[6]) * ring[5] + double(b[2]) * ring[1]
                + double(b[11]) * ring[10] + double(b[7]) * ring[6] + double(b[3]) * ring[2]
                + double(b[0]) * ring[11] + double(b[8]) * ring[7] + double(b[4]) * ring[3]
                + out[25];
            ring[10] = float(d);
            out[25] = float(d);
            d = double(b[10]) * ring[8] + double(b[6]) * ring[4] + double(b[2]) * ring[0]
                + double(b[11]) * ring[9] + double(b[7]) * ring[5] + double(b[3]) * ring[1]
                + double(b[8]) * ring[6] + double(b[4]) * ring[2] + double(b[1]) * ring[11]
                + double(b[0]) * ring[10] + double(b[9]) * ring[7] + double(b[5]) * ring[3]
                + out[26];
            ring[9] = float(d);
            out[26] = float(d);
            d = double(b[11]) * ring[8] + double(b[7]) * ring[4] + double(b[3]) * ring[0]
                + double(b[8]) * ring[5] + double(b[4]) * ring[1] + double(b[0]) * ring[9]
                + double(b[9]) * ring[6] + double(b[5]) * ring[2] + double(b[2]) * ring[11]
                + double(b[1]) * ring[10] + double(b[10]) * ring[7] + double(b[6]) * ring[3]
                + out[27];
            ring[8] = float(d);
            out[27] = float(d);
            d = double(b[8]) * ring[4] + double(b[4]) * ring[0] + double(b[0]) * ring[8]
                + double(b[9]) * ring[5] + double(b[5]) * ring[1] + double(b[1]) * ring[9]
                + double(b[10]) * ring[6] + double(b[6]) * ring[2] + double(b[3]) * ring[11]
                + double(b[2]) * ring[10] + double(b[11]) * ring[7] + double(b[7]) * ring[3]
                + out[28];
            ring[7] = float(d);
            out[28] = float(d);
            d = double(b[9]) * ring[4] + double(b[5]) * ring[0] + double(b[1]) * ring[8]
                + double(b[10]) * ring[5] + double(b[6]) * ring[1] + double(b[2]) * ring[9]
                + double(b[11]) * ring[6] + double(b[7]) * ring[2] + double(b[4]) * ring[11]
                + double(b[3]) * ring[10] + double(b[8]) * ring[3] + double(b[0]) * ring[7]
                + out[29];
            ring[6] = float(d);
            out[29] = float(d);
            d = double(b[10]) * ring[4] + double(b[6]) * ring[0] + double(b[2]) * ring[8]
                + double(b[11]) * ring[5] + double(b[7]) * ring[1] + double(b[3]) * ring[9]
                + double(b[8]) * ring[2] + double(b[5]) * ring[11] + double(b[4]) * ring[10]
                + double(b[0]) * ring[6] + double(b[9]) * ring[3] + double(b[1]) * ring[7]
                + out[30];
            ring[5] = float(d);
            out[30] = float(d);
            d = double(b[11]) * ring[4] + double(b[7]) * ring[0] + double(b[3]) * ring[8]
                + double(b[8]) * ring[1] + double(b[4]) * ring[9] + double(b[0]) * ring[5]
                + double(b[9]) * ring[2] + double(b[6]) * ring[11] + double(b[5]) * ring[10]
                + double(b[1]) * ring[6] + double(b[10]) * ring[3] + double(b[2]) * ring[7]
                + out[31];
            ring[4] = float(d);
            out[31] = float(d);
            d = double(b[8]) * ring[0] + double(b[4]) * ring[8] + double(b[0]) * ring[4]
                + double(b[9]) * ring[1] + double(b[5]) * ring[9] + double(b[1]) * ring[5]
                + double(b[10]) * ring[2] + double(b[7]) * ring[11] + double(b[6]) * ring[10]
                + double(b[2]) * ring[6] + double(b[11]) * ring[3] + double(b[3]) * ring[7]
                + out[32];
            ring[3] = float(d);
            out[32] = float(d);
            d = double(b[9]) * ring[0] + double(b[5]) * ring[8] + double(b[1]) * ring[4]
                + double(b[10]) * ring[1] + double(b[6]) * ring[9] + double(b[2]) * ring[5]
                + double(b[11]) * ring[2] + double(b[8]) * ring[11] + double(b[7]) * ring[10]
                + double(b[3]) * ring[6] + double(b[4]) * ring[7] + double(b[0]) * ring[3]
                + out[33];
            ring[2] = float(d);
            out[33] = float(d);
            d = double(b[10]) * ring[0] + double(b[6]) * ring[8] + double(b[2]) * ring[4]
                + double(b[11]) * ring[1] + double(b[7]) * ring[9] + double(b[3]) * ring[5]
                + double(b[9]) * ring[11] + double(b[8]) * ring[10] + double(b[4]) * ring[6]
                + double(b[0]) * ring[2] + double(b[5]) * ring[7] + double(b[1]) * ring[3]
                + out[34];
            ring[1] = float(d);
            out[34] = float(d);
            d = double(b[11]) * ring[0] + double(b[7]) * ring[8] + double(b[3]) * ring[4]
                + double(b[8]) * ring[9] + double(b[4]) * ring[5] + double(b[0]) * ring[1]
                + double(b[10]) * ring[11] + double(b[9]) * ring[10] + double(b[5]) * ring[6]
                + double(b[1]) * ring[2] + double(b[6]) * ring[7] + double(b[2]) * ring[3]
                + out[35];
            ring[0] = float(d);
            out[35] = float(d);
            d = double(b[8]) * ring[8] + double(b[4]) * ring[4] + double(b[0]) * ring[0]
                + double(b[9]) * ring[9] + double(b[5]) * ring[5] + double(b[1]) * ring[1]
                + double(b[11]) * ring[11] + double(b[10]) * ring[10] + double(b[6]) * ring[6]
                + double(b[2]) * ring[2] + double(b[7]) * ring[7] + double(b[3]) * ring[3]
                + out[36];
            ring[11] = float(d);
            out[36] = float(d);
            d = double(b[9]) * ring[8] + double(b[5]) * ring[4] + double(b[1]) * ring[0]
                + double(b[10]) * ring[9] + double(b[6]) * ring[5] + double(b[2]) * ring[1]
                + double(b[11]) * ring[10] + double(b[7]) * ring[6] + double(b[3]) * ring[2]
                + double(b[0]) * ring[11] + double(b[8]) * ring[7] + double(b[4]) * ring[3]
                + out[37];
            ring[10] = float(d);
            out[37] = float(d);
            d = double(b[10]) * ring[8] + double(b[6]) * ring[4] + double(b[2]) * ring[0]
                + double(b[11]) * ring[9] + double(b[7]) * ring[5] + double(b[3]) * ring[1]
                + double(b[8]) * ring[6] + double(b[4]) * ring[2] + double(b[1]) * ring[11]
                + double(b[0]) * ring[10] + double(b[9]) * ring[7] + double(b[5]) * ring[3]
                + out[38];
            ring[9] = float(d);
            out[38] = float(d);
            d = double(b[11]) * ring[8] + double(b[7]) * ring[4] + double(b[3]) * ring[0]
                + double(b[8]) * ring[5] + double(b[4]) * ring[1] + double(b[0]) * ring[9]
                + double(b[9]) * ring[6] + double(b[5]) * ring[2] + double(b[2]) * ring[11]
                + double(b[1]) * ring[10] + double(b[10]) * ring[7] + double(b[6]) * ring[3]
                + out[39];
            ring[8] = float(d);
            out[39] = float(d);
            d = double(b[8]) * ring[4] + double(b[4]) * ring[0] + double(b[0]) * ring[8]
                + double(b[9]) * ring[5] + double(b[5]) * ring[1] + double(b[1]) * ring[9]
                + double(b[10]) * ring[6] + double(b[6]) * ring[2] + double(b[3]) * ring[11]
                + double(b[2]) * ring[10] + double(b[11]) * ring[7] + double(b[7]) * ring[3]
                + out[40];
            ring[7] = float(d);
            out[40] = float(d);
            d = double(b[9]) * ring[4] + double(b[5]) * ring[0] + double(b[1]) * ring[8]
                + double(b[10]) * ring[5] + double(b[6]) * ring[1] + double(b[2]) * ring[9]
                + double(b[11]) * ring[6] + double(b[7]) * ring[2] + double(b[4]) * ring[11]
                + double(b[3]) * ring[10] + double(b[8]) * ring[3] + double(b[0]) * ring[7]
                + out[41];
            ring[6] = float(d);
            out[41] = float(d);
            d = double(b[10]) * ring[4] + double(b[6]) * ring[0] + double(b[2]) * ring[8]
                + double(b[11]) * ring[5] + double(b[7]) * ring[1] + double(b[3]) * ring[9]
                + double(b[8]) * ring[2] + double(b[5]) * ring[11] + double(b[4]) * ring[10]
                + double(b[0]) * ring[6] + double(b[9]) * ring[3] + double(b[1]) * ring[7]
                + out[42];
            ring[5] = float(d);
            out[42] = float(d);
            d = double(b[11]) * ring[4] + double(b[7]) * ring[0] + double(b[3]) * ring[8]
                + double(b[8]) * ring[1] + double(b[4]) * ring[9] + double(b[0]) * ring[5]
                + double(b[9]) * ring[2] + double(b[6]) * ring[11] + double(b[5]) * ring[10]
                + double(b[1]) * ring[6] + double(b[10]) * ring[3] + double(b[2]) * ring[7]
                + out[43];
            ring[4] = float(d);
            out[43] = float(d);
            d = double(b[8]) * ring[0] + double(b[4]) * ring[8] + double(b[0]) * ring[4]
                + double(b[9]) * ring[1] + double(b[5]) * ring[9] + double(b[1]) * ring[5]
                + double(b[10]) * ring[2] + double(b[7]) * ring[11] + double(b[6]) * ring[10]
                + double(b[2]) * ring[6] + double(b[11]) * ring[3] + double(b[3]) * ring[7]
                + out[44];
            ring[3] = float(d);
            out[44] = float(d);
            d = double(b[9]) * ring[0] + double(b[5]) * ring[8] + double(b[1]) * ring[4]
                + double(b[10]) * ring[1] + double(b[6]) * ring[9] + double(b[2]) * ring[5]
                + double(b[11]) * ring[2] + double(b[8]) * ring[11] + double(b[7]) * ring[10]
                + double(b[3]) * ring[6] + double(b[4]) * ring[7] + double(b[0]) * ring[3]
                + out[45];
            ring[2] = float(d);
            out[45] = float(d);
            d = double(b[10]) * ring[0] + double(b[6]) * ring[8] + double(b[2]) * ring[4]
                + double(b[11]) * ring[1] + double(b[7]) * ring[9] + double(b[3]) * ring[5]
                + double(b[9]) * ring[11] + double(b[8]) * ring[10] + double(b[4]) * ring[6]
                + double(b[0]) * ring[2] + double(b[5]) * ring[7] + double(b[1]) * ring[3]
                + out[46];
            ring[1] = float(d);
            out[46] = float(d);
            d = double(b[11]) * ring[0] + double(b[7]) * ring[8] + double(b[3]) * ring[4]
                + double(b[8]) * ring[9] + double(b[4]) * ring[5] + double(b[0]) * ring[1]
                + double(b[10]) * ring[11] + double(b[9]) * ring[10] + double(b[5]) * ring[6]
                + double(b[1]) * ring[2] + double(b[6]) * ring[7] + double(b[2]) * ring[3]
                + out[47];
            ring[0] = float(d);
            out[47] = float(d);
            out += 48;
            next += 4;
            done += 4;
        } while (next < groups);
    }
    if (done < groups) {
        int left = groups - done;
        do {
            d = double(b[8]) * ring[8] + double(b[4]) * ring[4] + double(b[0]) * ring[0]
                + double(b[9]) * ring[9] + double(b[5]) * ring[5] + double(b[1]) * ring[1]
                + double(b[11]) * ring[11] + double(b[10]) * ring[10] + double(b[6]) * ring[6]
                + double(b[2]) * ring[2] + double(b[7]) * ring[7] + double(b[3]) * ring[3] + out[0];
            ring[11] = float(d);
            out[0] = float(d);
            d = double(b[9]) * ring[8] + double(b[5]) * ring[4] + double(b[1]) * ring[0]
                + double(b[10]) * ring[9] + double(b[6]) * ring[5] + double(b[2]) * ring[1]
                + double(b[11]) * ring[10] + double(b[7]) * ring[6] + double(b[3]) * ring[2]
                + double(b[0]) * ring[11] + double(b[8]) * ring[7] + double(b[4]) * ring[3] + out[1];
            ring[10] = float(d);
            out[1] = float(d);
            d = double(b[10]) * ring[8] + double(b[6]) * ring[4] + double(b[2]) * ring[0]
                + double(b[11]) * ring[9] + double(b[7]) * ring[5] + double(b[3]) * ring[1]
                + double(b[8]) * ring[6] + double(b[4]) * ring[2] + double(b[1]) * ring[11]
                + double(b[0]) * ring[10] + double(b[9]) * ring[7] + double(b[5]) * ring[3] + out[2];
            ring[9] = float(d);
            out[2] = float(d);
            d = double(b[11]) * ring[8] + double(b[7]) * ring[4] + double(b[3]) * ring[0]
                + double(b[8]) * ring[5] + double(b[4]) * ring[1] + double(b[0]) * ring[9]
                + double(b[9]) * ring[6] + double(b[5]) * ring[2] + double(b[2]) * ring[11]
                + double(b[1]) * ring[10] + double(b[10]) * ring[7] + double(b[6]) * ring[3]
                + out[3];
            ring[8] = float(d);
            out[3] = float(d);
            d = double(b[8]) * ring[4] + double(b[4]) * ring[0] + double(b[0]) * ring[8]
                + double(b[9]) * ring[5] + double(b[5]) * ring[1] + double(b[1]) * ring[9]
                + double(b[10]) * ring[6] + double(b[6]) * ring[2] + double(b[3]) * ring[11]
                + double(b[2]) * ring[10] + double(b[11]) * ring[7] + double(b[7]) * ring[3]
                + out[4];
            ring[7] = float(d);
            out[4] = float(d);
            d = double(b[9]) * ring[4] + double(b[5]) * ring[0] + double(b[1]) * ring[8]
                + double(b[10]) * ring[5] + double(b[6]) * ring[1] + double(b[2]) * ring[9]
                + double(b[11]) * ring[6] + double(b[7]) * ring[2] + double(b[4]) * ring[11]
                + double(b[3]) * ring[10] + double(b[8]) * ring[3] + double(b[0]) * ring[7] + out[5];
            ring[6] = float(d);
            out[5] = float(d);
            d = double(b[10]) * ring[4] + double(b[6]) * ring[0] + double(b[2]) * ring[8]
                + double(b[11]) * ring[5] + double(b[7]) * ring[1] + double(b[3]) * ring[9]
                + double(b[8]) * ring[2] + double(b[5]) * ring[11] + double(b[4]) * ring[10]
                + double(b[0]) * ring[6] + double(b[9]) * ring[3] + double(b[1]) * ring[7] + out[6];
            ring[5] = float(d);
            out[6] = float(d);
            d = double(b[11]) * ring[4] + double(b[7]) * ring[0] + double(b[3]) * ring[8]
                + double(b[8]) * ring[1] + double(b[4]) * ring[9] + double(b[0]) * ring[5]
                + double(b[9]) * ring[2] + double(b[6]) * ring[11] + double(b[5]) * ring[10]
                + double(b[1]) * ring[6] + double(b[10]) * ring[3] + double(b[2]) * ring[7]
                + out[7];
            ring[4] = float(d);
            out[7] = float(d);
            d = double(b[8]) * ring[0] + double(b[4]) * ring[8] + double(b[0]) * ring[4]
                + double(b[9]) * ring[1] + double(b[5]) * ring[9] + double(b[1]) * ring[5]
                + double(b[10]) * ring[2] + double(b[7]) * ring[11] + double(b[6]) * ring[10]
                + double(b[2]) * ring[6] + double(b[11]) * ring[3] + double(b[3]) * ring[7]
                + out[8];
            ring[3] = float(d);
            out[8] = float(d);
            d = double(b[9]) * ring[0] + double(b[5]) * ring[8] + double(b[1]) * ring[4]
                + double(b[10]) * ring[1] + double(b[6]) * ring[9] + double(b[2]) * ring[5]
                + double(b[11]) * ring[2] + double(b[8]) * ring[11] + double(b[7]) * ring[10]
                + double(b[3]) * ring[6] + double(b[4]) * ring[7] + double(b[0]) * ring[3] + out[9];
            ring[2] = float(d);
            out[9] = float(d);
            d = double(b[10]) * ring[0] + double(b[6]) * ring[8] + double(b[2]) * ring[4]
                + double(b[11]) * ring[1] + double(b[7]) * ring[9] + double(b[3]) * ring[5]
                + double(b[9]) * ring[11] + double(b[8]) * ring[10] + double(b[4]) * ring[6]
                + double(b[0]) * ring[2] + double(b[5]) * ring[7] + double(b[1]) * ring[3]
                + out[10];
            ring[1] = float(d);
            out[10] = float(d);
            d = double(b[11]) * ring[0] + double(b[7]) * ring[8] + double(b[3]) * ring[4]
                + double(b[8]) * ring[9] + double(b[4]) * ring[5] + double(b[0]) * ring[1]
                + double(b[10]) * ring[11] + double(b[9]) * ring[10] + double(b[5]) * ring[6]
                + double(b[1]) * ring[2] + double(b[6]) * ring[7] + double(b[2]) * ring[3]
                + out[11];
            ring[0] = float(d);
            out[11] = float(d);
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
    int flag = int32_t(a) < s->threshold;
    K[0] = float((double(MutCoefs6[a]) - s->coefs[0]) * kQuarter);
    for (int i = 1; i < 4; i++) {
        a = MutBits(s, 6);
        K[i] = float((double(MutCoefs6[a]) - s->coefs[i]) * kQuarter);
    }
    for (int i = 4; i < 12; i++) {
        a = MutBits(s, 5);
        K[i] = float((double(MutCoefs5[a]) - s->coefs[i]) * kQuarter);
    }
    float *to = &s->signal[324];       // the frame's 432 outputs, a subframe of 108 at a time
    for (int j = 216; j < 648; j += 108) {
        uint32_t back = MutBits(s, 8);
        int32_t lag = j - int32_t(back);
        uint32_t ltpGain = MutBits(s, 4);
        gainB = float(ltpGain) * kLtpGainStep;   // 0..15, exact as a float: one rounded product either way
        uint32_t gain = MutBits(s, 6);
        gainA = s->gainTable[gain];
        uint32_t phase = MutBits(s, 1);
        uint32_t sparse = MutBits(s, 1);
        readsamples(s, flag, &w[5 + phase]);
        if (sparse != 0) {
            for (int k = 0; k < 54; k++)
                w[6 - phase + k * 2] = 0.0f;
        } else {
            for (int k = 0; k < 5; k++) {
                w[113 + k] = 0.0f;
                w[k] = 0.0f;
            }
            float *x = &w[6 - phase];
            for (int k = 0; k < 54; k++) {
                double v = (double(x[2 * k - 5]) + x[2 * k + 5]) * kHalfBand0;
                v = v - (double(x[2 * k - 3]) + x[2 * k + 3]) * kHalfBand1;
                v = v + (double(x[2 * k - 1]) + x[2 * k + 1]) * kHalfBand2;
                x[2 * k] = float(v);
            }
            gainA = gainA * kHalf;
        }
        const float *history = s->signal + lag;
        for (int i = 0; i < 108; i++)
            to[i] = float(double(gainA) * w[5 + i] + double(gainB) * history[i]);
        to += 108;
    }
    for (int i = 0; i < 324; i++)
        s->signal[i] = s->signal[432 + i];
    static const int kFirst[4] = { 0, 12, 24, 36 };
    static const int kGroups[4] = { 1, 1, 1, 33 };
    for (int step = 0; step < 4; step++) {
        for (int i = 0; i < 12; i++)
            s->coefs[i] = K[i] + s->coefs[i];
        MutSynthesise(kFirst[step], s, kGroups[step]);
    }
}

// int16 -> float from the end: one at a time until the count is a multiple of 8 (stopping at 0), then blocks of
// eight, loaded first and stored in the original's order, while the count stays above 0.
// FUNC_AT(0x0014a1e0)
void decode16x87(uint32_t count, const int16_t *in, float *out) {
    SND_UNTESTED("decode16x87");
    int32_t n = int32_t(count);
    while ((n & 7) != 0) {
        out[n - 1] = in[n - 1];
        n -= 1;
        if (n == 0)
            return;
    }
    do {
        int16_t v0 = in[n - 8], v1 = in[n - 7], v2 = in[n - 6], v3 = in[n - 5];
        int16_t v4 = in[n - 4], v5 = in[n - 3], v6 = in[n - 2], v7 = in[n - 1];
        out[n - 8] = v0;
        out[n - 2] = v6;
        out[n - 3] = v5;
        out[n - 4] = v4;
        out[n - 5] = v3;
        out[n - 6] = v2;
        out[n - 7] = v1;
        out[n - 1] = v7;
        n -= 8;
    } while (n > 0);
}
