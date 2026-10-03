#include "Decode.h"
#include "DecodeUnused.h"   // SND_UNTESTED

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// EA-XA, the codec of every stream on the disc (docs/driving/sound.md 3.7, 3.8): 15-byte blocks of 28 samples
// per channel, byte 0 the predictor (high nibble) and shift (low nibble), then 28 nibbles high first;
// s = (s1 x k1 + table[shift][nibble]) + s2 x k2 in single precision. decodexac is the original's hand-written SSE
// scalar loop, here as the same intrinsics in the same order (the tables are read where the original reads them,
// including past the four predictors for a predictor nibble above 3, which no stream has). The object's methods
// are the original's at the same addresses, member for member.
// ---------------------------------------------------------------------------------------------------------------

static_assert(offsetof(SND::CEAXABLKDecf, buffered) == 0x04, "CEAXABLKDecf layout");
static_assert(offsetof(SND::CEAXABLKDecf, framesLeft) == 0x08, "CEAXABLKDecf layout");
static_assert(offsetof(SND::CEAXABLKDecf, bytes) == 0x0c, "CEAXABLKDecf layout");
static_assert(offsetof(SND::CEAXABLKDecf, bufferRead) == 0x10, "CEAXABLKDecf layout");
static_assert(offsetof(SND::CEAXABLKDecf, buffer) == 0x24, "CEAXABLKDecf layout");
static_assert(offsetof(SND::CEAXABLKDecf, xac) == 0x94, "CEAXABLKDecf layout");

namespace {

#define XaPredictor1  ((const float *)0x001da9b8u)   // 0, 0.9375, 1.796875, 1.53125
#define XaPredictor2  ((const float *)0x001da9c8u)   // 0, 0, -0.8125, -0.859375
#define XaNibbles     ((const float *)0x001da9d8u)   // [shift][nibble]: signed nibble << (12 - shift)
#define CodaNew       (*(void *(**)(size_t size))0x002475f4u)    // SNDMEMI_alloc's thunk, set by MIX_create
#define CodaDelete    (*(void (**)(void *block))0x002475f8u)     // SNDMEMI_free's thunk

const int kBlockFrames = 28;
const int kBlockBytes = 15;

// The copies are dword moves in the original: bits kept as they are.
inline void CopyFrame(float *to, const float *from) {
    memcpy(to, from, 4);
}

}   // namespace

// The block loop: xmm2/xmm4 the newest sample, xmm3 the one before; each byte gives two samples, written back
// from the end of the block (the original indexes from -14).
// FUNC_AT(0x00149d80)
void SND::decodexac(DecodeXacParams *params) {
    const uint8_t *src = params->src;
    float *dst = params->dst;
    __m128 x2 = _mm_load_ss(&params->s1);
    __m128 x3 = _mm_load_ss(&params->s2);
    __m128 x4 = x2;
    while (params->frames > 0) {
        params->frames -= kBlockFrames;
        uint32_t predictor = src[0] >> 4;
        const float *nibbles = XaNibbles + (src[0] & 0xf) * 16;
        src += 1;
        src += 14;
        dst += kBlockFrames;
        for (int i = -14; i < 0; i++) {
            x4 = _mm_mul_ss(x4, _mm_load_ss(&XaPredictor1[predictor]));
            uint32_t high = src[i] >> 4;
            x3 = _mm_mul_ss(x3, _mm_load_ss(&XaPredictor2[predictor]));
            x4 = _mm_add_ss(x4, _mm_load_ss(&nibbles[high]));
            x2 = _mm_mul_ss(x2, _mm_load_ss(&XaPredictor2[predictor]));
            x4 = _mm_add_ss(x4, x3);
            _mm_store_ss(&dst[i * 2], x4);
            x3 = x4;
            uint32_t low = src[i] & 0xf;
            x4 = _mm_mul_ss(x4, _mm_load_ss(&XaPredictor1[predictor]));
            x4 = _mm_add_ss(x4, _mm_load_ss(&nibbles[low]));
            x4 = _mm_add_ss(x4, x2);
            x2 = x4;
            _mm_store_ss(&dst[i * 2 + 1], x4);
        }
    }
    _mm_store_ss(&params->s1, x2);
    _mm_store_ss(&params->s2, x3);
    params->dst = dst;
}

// FUNC_AT(0x00149e50)
void* SND::CEAXABLKDecf::operator new(size_t size) {
    return CodaNew(size);
}

// FUNC_AT(0x00149e60)
void SND::CEAXABLKDecf::operator delete(void *block) {
    CodaDelete(block);
}

// FUNC_AT(0x00149e70)
SND::CEAXABLKDecf* SND::CEAXABLKDecf::Construct() {
    bytes = 0;
    framesLeft = 0;
    buffered = 0;
    xac.s1 = 0.0f;
    xac.s2 = 0.0f;
    return this;
}

// -1 (nothing taken) without data or while frames of the last packet are left.
// FUNC_AT(0x00149e90)
int SND::CEAXABLKDecf::Feed(const void *data, int byteCount, int frames) {
    if (data == NULL || framesLeft != 0)
        return -1;
    framesLeft = frames;
    xac.src = (const uint8_t *)data;
    bytes = byteCount;
    return 0;
}

// Frames written to *out (min(frames, framesLeft)); *out itself is not moved. A request that ends inside a block
// decodes the whole block into `buffer` and hands out the rest of it on the next call.
// FUNC_AT(0x00149ec0)
int SND::CEAXABLKDecf::Decode(float **out, int frames) {
    int produced = 0;
    xac.dst = *out;
    if (framesLeft == 0)
        return 0;
    int wanted = frames < framesLeft ? frames : framesLeft;
    if (buffered != 0) {
        int take = buffered;
        if (take > wanted)
            take = wanted;
        for (int i = 0; i < take; i++) {
            CopyFrame(xac.dst, bufferRead);
            bufferRead++;
            xac.dst++;
        }
        framesLeft -= take;
        buffered -= take;
        wanted -= take;
        produced = take;
    }
    int whole = wanted / kBlockFrames * kBlockFrames;
    int rest = wanted - whole;
    xac.frames = whole;
    if (whole > 0) {
        decodexac(&xac);
        framesLeft -= whole;
        produced += whole;
        xac.src += whole / kBlockFrames * kBlockBytes;
    }
    if (rest > 0) {
        float *to = xac.dst;
        xac.dst = buffer;
        xac.frames = rest;
        decodexac(&xac);
        framesLeft -= rest;
        xac.src += kBlockBytes;
        bufferRead = xac.dst + xac.frames;           // xac.frames is rest - 28 now
        buffered = -xac.frames;
        xac.dst = xac.dst - kBlockFrames;            // back to buffer
        for (int i = 0; i < rest; i++)
            CopyFrame(&to[i], &xac.dst[i]);
        produced += rest;
    }
    if (framesLeft <= 0)
        buffered = 0;
    return produced;
}

// The history as two raw floats, s1 and s2 (SFILTER_unpackxalf only, for its loop point): an 8-byte value
// returned through the caller's buffer, whose address comes back in EAX - the caller reads it from there.
// FUNC_AT(0x0014a190)
void* SND::CEAXABLKDecf::GetState(void *state) {
    SND_UNTESTED("SND::CEAXABLKDecf::GetState");
    uint32_t s1, s2;
    memcpy(&s1, &xac.s1, 4);
    memcpy(&s2, &xac.s2, 4);
    ((uint32_t *)state)[0] = s1;
    ((uint32_t *)state)[1] = s2;
    return state;
}

// FUNC_AT(0x0014a1c0)
void SND::CEAXABLKDecf::SetState(const void *state) {
    memcpy(&xac.s1, (const uint32_t *)state, 4);
    memcpy(&xac.s2, (const uint32_t *)state + 1, 4);
}
