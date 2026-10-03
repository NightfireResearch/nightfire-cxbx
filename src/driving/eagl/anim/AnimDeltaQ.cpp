#include "AnimDeltaQ.h"
#include "AnimMisc.h"
#include "AnimUntested.h"
#include "Skeleton.h"
#include "../../../helpers.h"

#include <bit>
#include <math.h>
#include <string.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// FnDeltaSingleQ, FnDeltaQFast and FnDeltaQ (docs/driving/eagl.md 3.4): EAGLAnim's quaternion keyframe-block
// channels. No shipped anim builds them, so these are provisional ports from the listing, each warning once
// (EAGL_UNTESTED) the first time it runs. All three keep their keys in bins of 2^power keys: each bin a "physical"
// quantised quaternion per bone, then a row of quantised deltas per key after the first. A key is reached from the
// cached one by adding rows on (FnDeltaQ and FnDeltaQFast also take them off going back, unless the global at
// 0x001ceb4c disables that), or by decoding its bin again. Between keys:
//  - FnDeltaSingleQ: one axis angle per bone (x, y or z) between a fixed pre- and post-rotation, normalised lerp;
//  - FnDeltaQFast: a plain component lerp towards the next key's quaternions, which it caches in a second buffer;
//  - FnDeltaQ: w recovered from xyz (and a sign bit), normalised lerp.
// FnDeltaQ and FnDeltaQFast also write constant bones from the end of the data. The output quaternion is at +0x10
// of each bone's 0x30-byte SQT record; a mask is a 256-bit set of bones. x87 arithmetic in double in the
// original's order, with a float store for every store (a single operation on floats written in float: the same
// bits).
// ---------------------------------------------------------------------------------------------------------------

namespace {

typedef void (*EaglFreeHook)(void *data, uint32_t size);
#define EaglFree (*(EaglFreeHook *)0x001caf6c)
#define VtDeltaSingleQ ((const void *)0x001a1360)
#define VtDeltaQFast ((const void *)0x001a13e0)
#define VtDeltaQ ((const void *)0x001a1450)
#define ReverseDeltaSumEnabled U8_AT(0x001ceb4c)            // 0: going back decodes the bin again
#define SingleQAngleRange FLOAT_AT(0x00241ae0)              // set at run time (2 pi, presumably)

// The dequantisation scales, the original's .rdata floats (1 and 0 are exact)
constexpr float kOneOver255 = 1.0f / 255;                   // 0x001a1348 (doubled), 0x001a1440
constexpr float kOneOver65535 = 1.0f / 65535;               // 0x001a1350, 0x001a1448 (doubled)
constexpr float kOneOver15 = 1.0f / 15;                     // 0x001a1354
constexpr float kMinusPi = -3.14159274f;                    // 0x001a1358
constexpr float kOneOver63 = 1.0f / 63;                     // 0x001a13d4
constexpr float kTwoOver65535 = 2.0f / 65535;               // 0x001a13d8
constexpr float kTwoOver4095 = 2.0f / 4095;                 // 0x001a13dc
constexpr float kOneOver127 = 1.0f / 127;                   // 0x001a143c
constexpr float kOneOver32767 = 1.0f / 32767;               // 0x001a1444 (doubled)
static_assert(std::bit_cast<uint32_t>(kOneOver255) == 0x3b808081, "the original's 1/255");
static_assert(std::bit_cast<uint32_t>(kOneOver65535) == 0x37800080, "the original's 1/65535");
static_assert(std::bit_cast<uint32_t>(kOneOver15) == 0x3d888889, "the original's 1/15");
static_assert(std::bit_cast<uint32_t>(kMinusPi) == 0xc0490fdb, "the original's -pi");
static_assert(std::bit_cast<uint32_t>(kOneOver63) == 0x3c820821, "the original's 1/63");
static_assert(std::bit_cast<uint32_t>(kTwoOver65535) == 0x38000080, "the original's 2/65535");
static_assert(std::bit_cast<uint32_t>(kTwoOver4095) == 0x3a000801, "the original's 2/4095");
static_assert(std::bit_cast<uint32_t>(kOneOver127) == 0x3c010204, "the original's 1/127");
static_assert(std::bit_cast<uint32_t>(kOneOver32767) == 0x38000100, "the original's 1/32767");

enum { kSlotEvalSQTMasked = 18 };    // FnDeltaSingleQ's and FnDeltaQ's extra virtual

inline int Truncate(float f) {   // CVTTSS2SI
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

inline bool InMask(const void *mask, int bone) {
    return (static_cast<const BoneMask *>(mask)->bits[bone >> 5] & (1u << (bone & 31))) != 0;
}

// A bone's quaternion in the SQT records.
inline float* SqtQuat(float *sqt, int bone) {
    return sqt + bone * 12 + 4;
}

inline void Copy4(float *to, const float *from) {
    memcpy(to, from, sizeof(float[4]));
}

// The key below the time: from the cached key, back or forward through the key times (the three formats share
// keys and times).
template <typename Data>
int FindKey(int frame, const Data *d, int cached) {
    const uint16_t *times = d->times;
    int keys = d->keys;
    if (times == NULL)
        return frame < 0 ? 0 : (frame >= keys ? keys - 1 : frame);
    if (frame < times[0])
        return 0;
    int i = cached >= 1 ? cached - 1 : 0;
    if (times[i] > frame) {
        while (i > 0 && times[i] > frame)
            i--;
    } else {
        int last = keys - 2;
        while (i < last && times[i + 1] <= frame)
            i++;
    }
    return i + 1;
}

// The fraction of the way from key k to k + 1; false when the time is exactly on key k (FCOMP, TEST AH,0x44,
// JNP: ordered equal), so that a NaN time interpolates.
template <typename Data>
bool LerpFactor(float time, int frame, const Data *d, int k, float *s) {
    const uint16_t *times = d->times;
    if (times == NULL) {
        double f = frame;
        if (time == f)
            return false;
        *s = float(time - f);
    } else if (k == 0) {
        if (time == 0.0f)
            return false;
        *s = time / times[0];
    } else {
        double p = times[k - 1];
        if (time == p)
            return false;
        *s = float((time - p) / (times[k] - p));
    }
    return true;
}

// The last key time plus one, or the key count (all three GetLengths).
template <typename Data>
bool DeltaGetLength(const Data *d, float *length) {
    const uint16_t *times = d->times;
    int keys = d->keys;
    int n = times == NULL ? keys : times[keys - 2] + 1;
    *length = float(n);
    return true;
}

DeltaSingleQData *SingleQData(uint8_t *anim) {
    return reinterpret_cast<DeltaSingleQData *>(anim);
}

DeltaQFastHeader *QFastData(uint8_t *anim) {
    return reinterpret_cast<DeltaQFastHeader *>(anim);
}

DeltaQHeader *QData(uint8_t *anim) {
    return reinterpret_cast<DeltaQHeader *>(anim);
}

}  // namespace

// ---- quaternion helpers

// a * b * c for a and c rotations about y (a pre and post multiplication) - the product with two float
// temporaries, as the original.
// FUNC_AT(0x00100ac0)
void QuatMultXxYxZ(const float *a, const float *b, const float *c, float *out) {
    EAGL_UNTESTED("QuatMultXxYxZ");
    double A = double(a[0]) * b[3];
    double B = double(b[1]) * a[3];
    float C = -(b[1] * a[0]);
    float D = a[3] * b[3];
    out[0] = float(A * c[3] - B * c[2]);
    out[1] = float(A * c[2] + B * c[3]);
    out[2] = float(double(D) * c[2] + double(C) * c[3]);
    out[3] = float(double(D) * c[3] - double(C) * c[2]);
}

// a * b for a rotation a about x (x and w only). No callers: FnDeltaSingleQ::EvalSQTMasked has it inline.
// FUNC_AT(0x00100b40)
void QuatMultXxQ(const float *a, const float *b, float *out) {
    EAGL_UNTESTED("QuatMultXxQ");
    out[0] = float(double(a[0]) * b[3] + double(a[3]) * b[0]);
    out[1] = float(double(a[3]) * b[1] + double(b[2]) * a[0]);
    out[2] = float(double(a[3]) * b[2] - double(b[1]) * a[0]);
    out[3] = float(double(a[3]) * b[3] - double(a[0]) * b[0]);
}

// a * b for a rotation b about z (z and w only). No callers: inline in FnDeltaSingleQ::EvalSQTMasked.
// FUNC_AT(0x00100b90)
void QuatMultQxZ(const float *a, const float *b, float *out) {
    EAGL_UNTESTED("QuatMultQxZ");
    out[0] = float(double(a[0]) * b[3] - double(b[2]) * a[1]);
    out[1] = float(double(a[1]) * b[3] + double(b[2]) * a[0]);
    out[2] = float(double(b[2]) * a[3] + double(a[2]) * b[3]);
    out[3] = float(double(a[3]) * b[3] - double(b[2]) * a[2]);
}

// (b - a) * t + a, normalised. z and w are stored to float first; w's square is the unrounded w times the
// rounded one, and x is scaled by the unrounded 1/length, the others by it rounded.
// FUNC_AT(0x00101e40)
void AnimQuatNLerp(float t, const float *a, const float *b, float *out) {
    EAGL_UNTESTED("AnimQuatNLerp");
    double x = (double(b[0]) - a[0]) * t + a[0];
    double y = (double(b[1]) - a[1]) * t + a[1];
    float z = float((double(b[2]) - a[2]) * t + a[2]);
    double w = (double(b[3]) - a[3]) * t + a[3];
    float wf = float(w);
    double ss = w * wf + double(z) * z;
    ss = ss + y * y;
    ss = ss + x * x;
    double r = 1.0 / sqrt(ss);
    float rf = float(r);
    out[0] = float(r * x);
    out[1] = float(rf * y);
    out[2] = rf * z;
    out[3] = rf * wf;
}

// w from xyz: sqrt(1 - |xyz|^2), negated for a positive sign, or xyz normalised and w 0 when |xyz| > 1.
// FUNC_AT(0x00104030)
void DeltaQRecoverW(int sign, float *q) {
    EAGL_UNTESTED("DeltaQRecoverW");
    double x = q[0], y = q[1], z = q[2];
    double ss = x * x + y * y;
    ss = ss + z * z;
    if (ss > 1.0) {            // FCOM, TEST AH,0x41, JNE: below, equal and unordered take w
        double r = 1.0 / sqrt(ss);
        q[3] = 0.0f;
        q[0] = float(r * q[0]);
        q[1] = float(r * q[1]);
        q[2] = float(r * q[2]);
    } else {
        double w = sqrt(1.0 - ss);
        q[3] = float(w);
        if (sign > 0)
            q[3] = float(-w);
    }
}

// FUNC_AT(0x00101f30)
int AnimTruncateSingleQ(float value) {
    EAGL_UNTESTED("AnimTruncateSingleQ");
    return Truncate(value);
}

// FUNC_AT(0x001033a0)
int AnimTruncateQFast(float value) {
    EAGL_UNTESTED("AnimTruncateQFast");
    return Truncate(value);
}

// FUNC_AT(0x001042a0)
int AnimTruncateQ(float value) {
    EAGL_UNTESTED("AnimTruncateQ");
    return Truncate(value);
}

// sqrt(x * x + y * y) of the 2-vector in EAX, answered in ST0 (other registers kept).
static double __cdecl Vec2Length(const float *v) {
    EAGL_UNTESTED("AnimVec2LengthEax");
    double y = v[1], x = v[0];
    return sqrt(x * x + y * y);
}

// FUNC_AT(0x001042b0)
__declspec(naked) void AnimVec2LengthEax() {
    __asm {
        push ecx
        push edx
        push eax
        call Vec2Length
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------
// FnDeltaSingleQ. Data: DeltaSingleQData, then the bins: per bone a DeltaSingleQPhysical (the axis component and
// w, 8 bits each), then a row of DeltaSingleQDelta (4 bits each) per key. A bone's rotation is pre * q * post,
// where q turns about the record's axis and pre/post are Euler rotations from its two constant angles.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00101aa0)
void DeltaSingleQMinRange::UnQuantize(DeltaSingleQMinRangef *out) {
    EAGL_UNTESTED("DeltaSingleQMinRange::UnQuantize");
    constexpr double k2 = double(kOneOver65535) + kOneOver65535;
    out->min[0] = float(min[0] * k2 - 1.0);
    out->min[1] = float(min[1] * k2 - 1.0);
    out->range[0] = float(range[0] * k2);
    out->range[1] = float(range[1] * k2);
    out->index = uint8_t(index);
    out->const0 = float(const0 * double(SingleQAngleRange) * kOneOver65535 + kMinusPi);
    out->const1 = float(const1 * double(SingleQAngleRange) * kOneOver65535 + kMinusPi);
}

// The axis component (x for index 0, y for 1, else z) and w; the other two components 0.
// FUNC_AT(0x00101b50)
void DeltaSingleQPhysical::UnQuantize(int index, float *q) {
    EAGL_UNTESTED("DeltaSingleQPhysical::UnQuantize");
    constexpr double k2 = double(kOneOver255) + kOneOver255;
    q[2] = 0.0f;
    q[1] = 0.0f;
    q[0] = 0.0f;
    float value = float(v * k2 - 1.0);
    if (index == 0)
        q[0] = value;
    else if (index == 1)
        q[1] = value;
    else
        q[2] = value;
    q[3] = float(w * k2 - 1.0);
}

// FUNC_AT(0x00101be0)
void DeltaSingleQDelta::UnQuantize(const DeltaSingleQMinRangef *range, float *q) {
    EAGL_UNTESTED("DeltaSingleQDelta::UnQuantize");
    uint8_t b = vw;
    q[2] = 0.0f;
    q[1] = 0.0f;
    q[0] = 0.0f;
    float value = float((b >> 4) * double(range->range[0]) * kOneOver15 + range->min[0]);
    if (range->index == 0)
        q[0] = value;
    else if (range->index == 1)
        q[1] = value;
    else
        q[2] = value;
    q[3] = float((b & 15) * double(range->range[1]) * kOneOver15 + range->min[1]);
}

// The bone's pre * q * post. The original has these products inline (x and z everywhere, y in its unmasked
// no-lerp loop); checked term by term against the listing: every component is one sum or difference of two
// float-by-float products (exact in double), with the same minuend and subtrahend as the functions here, and the
// inline y product keeps QuatMultXxYxZ's two unrounded products and two float temporaries - so bit-identical.
static void SingleQOut(FnDeltaSingleQ *self, int i, const float *q, float *out) {
    int index = self->minRanges[i].index;
    if (index == 0)
        QuatMultXxQ(q, self->postMultQs + i * 4, out);
    else if (index == 1)
        QuatMultXxYxZ(self->preMultQs + i * 4, q, self->postMultQs + i * 4, out);
    else
        QuatMultQxZ(self->preMultQs + i * 4, q, out);
}

// FUNC_AT(0x00100980)
FnDeltaSingleQ* FnDeltaSingleQ::Construct() {
    EAGL_UNTESTED("FnDeltaSingleQ::Construct");
    FnAnimMemoryMap::Construct();
    minRanges = NULL;
    bins = NULL;
    prevQBlock = NULL;
    prevQs = NULL;
    preMultQs = NULL;
    postMultQs = NULL;
    vtable = VtDeltaSingleQ;
    binSize = -1;
    prevKey = -1;
    type = kDeltaSingleQ;
    return this;
}

// FUNC_AT(0x001009c0)
void FnDeltaSingleQ::Destruct() {
    EAGL_UNTESTED("FnDeltaSingleQ::Destruct");
    vtable = VtDeltaSingleQ;
    if (prevQBlock != NULL) {
        AnimPool_FreeBlock(prevQBlock);
        AnimPool_FreeBlock(preMultQs);
        AnimPool_FreeBlock(postMultQs);
    }
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x00100a20)
void FnDeltaSingleQ::SetAnimMemoryMap(uint8_t *data) {
    EAGL_UNTESTED("FnDeltaSingleQ::SetAnimMemoryMap");
    anim = data;
}

// FUNC_AT(0x00100a30)
bool FnDeltaSingleQ::GetLength(float *length) {
    EAGL_UNTESTED("FnDeltaSingleQ::GetLength");
    return DeltaGetLength(SingleQData(anim), length);
}

// FUNC_AT(0x00100a80)
void FnDeltaSingleQ::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnDeltaSingleQ::Eval");
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQTMasked, time, (void *)NULL, out);
}

// FUNC_AT(0x00100aa0)
bool FnDeltaSingleQ::EvalSQT(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnDeltaSingleQ::EvalSQT");
    return AnimVCall<bool>(this, kSlotEvalSQTMasked, time, mask, sqt);
}

// The buffers, and each bone's pre/post rotations from its constant angles: index 0 (x) none before and
// (0, c0, c1) after, index 1 (y) (c0, 0, 0) before and (0, 0, c1) after, else (z) (c0, c1, 0) before and none
// after.
// FUNC_AT(0x00101c90)
void FnDeltaSingleQ::InitBuffersAsRequired() {
    EAGL_UNTESTED("FnDeltaSingleQ::InitBuffersAsRequired");
    if (prevQs != NULL)
        return;
    DeltaSingleQData *d = SingleQData(anim);
    bins = reinterpret_cast<uint8_t *>(d->ranges + d->bones);
    binSize = (((1 << d->binLengthPower) + 1) * d->bones + 1) & ~1;
    prevQs = prevQBlock = static_cast<float *>(AnimPool_NewBlock(d->bones * sizeof(float[4])));
    minRanges = d->ranges;
    preMultQs = static_cast<float *>(AnimPool_NewBlock(d->bones * sizeof(float[4])));
    postMultQs = static_cast<float *>(AnimPool_NewBlock(d->bones * sizeof(float[4])));
    for (int i = 0; i < d->bones; i++) {
        DeltaSingleQMinRangef r;
        minRanges[i].UnQuantize(&r);
        float *pre = preMultQs + i * 4, *post = postMultQs + i * 4;
        float e[3];
        if (r.index == 0) {
            pre[0] = 0.0f;
            pre[1] = 0.0f;
            pre[2] = 0.0f;
            pre[3] = 1.0f;
            e[0] = 0.0f;
            e[1] = r.const0;
            e[2] = r.const1;
            EAGLAnim_EulerToQuat(e, post);
        } else if (r.index == 1) {
            e[0] = r.const0;
            e[1] = 0.0f;
            e[2] = 0.0f;
            EAGLAnim_EulerToQuat(e, pre);
            e[0] = 0.0f;
            e[1] = 0.0f;
            e[2] = r.const1;
            EAGLAnim_EulerToQuat(e, post);
        } else {
            e[0] = r.const0;
            e[1] = r.const1;
            e[2] = 0.0f;
            EAGLAnim_EulerToQuat(e, pre);
            post[0] = 0.0f;
            post[1] = 0.0f;
            post[2] = 0.0f;
            post[3] = 1.0f;
        }
    }
}

// The original is the masked and unmasked loops written out separately for each case (and the x/y/z products
// inline); one loop with a mask test does the same. Going back always decodes the bin again.
// FUNC_AT(0x00100be0)
bool FnDeltaSingleQ::EvalSQTMasked(float time, void *mask, float *sqt) {
    EAGL_UNTESTED("FnDeltaSingleQ::EvalSQTMasked");
    InitBuffersAsRequired();
    DeltaSingleQData *d = SingleQData(anim);
    const uint8_t *boneIdx = d->boneIdx;
    auto skip = [&](int i) { return mask != NULL && !InMask(mask, boneIdx[i]); };
    int frame = Truncate(time);
    int k = FindKey(frame, d, prevKey);
    int shift = d->binLengthPower;
    int modMask = 0x7fffffff >> (31 - shift);
    int bin = k >> shift, offset = k & modMask;
    uint8_t *bp = bins + binSize * bin;
    DeltaSingleQPhysical *physical = reinterpret_cast<DeltaSingleQPhysical *>(bp);
    int cached = prevKey;
    int start;
    if (cached != -1 && bin == cached >> shift && k >= cached) {
        start = cached & modMask;
    } else {
        for (int i = 0; i < d->bones; i++) {
            if (skip(i))
                continue;
            int index = minRanges[i].index;
            physical[i].UnQuantize(index, prevQs + i * 4);
        }
        start = 0;
    }
    if (start < offset) {
        DeltaSingleQDelta *row = reinterpret_cast<DeltaSingleQDelta *>(bp + (start + 2) * d->bones);
        for (int f = offset - start; f != 0; f--) {
            for (int i = 0; i < d->bones; i++, row++) {
                if (skip(i))
                    continue;
                DeltaSingleQMinRangef r;
                float dq[4];
                minRanges[i].UnQuantize(&r);
                row->UnQuantize(&r, dq);
                float *v = prevQs + i * 4;
                for (int c = 0; c < 4; c++)
                    v[c] = dq[c] + v[c];
            }
        }
    }
    prevKey = k;

    float s;
    if (LerpFactor(time, frame, d, k, &s) && k < d->keys - 1) {
        int nextBin = (k + 1) >> shift;
        uint8_t *np = bins + binSize * nextBin;
        if (nextBin == bin) {
            DeltaSingleQDelta *row = reinterpret_cast<DeltaSingleQDelta *>(np + (offset + 2) * d->bones);
            for (int i = 0; i < d->bones; i++) {
                if (skip(i))
                    continue;
                DeltaSingleQMinRangef r;
                float dq[4], next[4], q[4];
                minRanges[i].UnQuantize(&r);
                row[i].UnQuantize(&r, dq);
                const float *v = prevQs + i * 4;
                for (int c = 0; c < 4; c++)
                    next[c] = dq[c] + v[c];
                AnimQuatNLerp(s, v, next, q);
                SingleQOut(this, i, q, SqtQuat(sqt, boneIdx[i]));
            }
        } else {
            DeltaSingleQPhysical *nextPhysical = reinterpret_cast<DeltaSingleQPhysical *>(np);
            for (int i = 0; i < d->bones; i++) {
                if (skip(i))
                    continue;
                float next[4], q[4];
                int index = minRanges[i].index;
                nextPhysical[i].UnQuantize(index, next);
                AnimQuatNLerp(s, prevQs + i * 4, next, q);
                SingleQOut(this, i, q, SqtQuat(sqt, boneIdx[i]));
            }
        }
    } else {
        for (int i = 0; i < d->bones; i++)
            if (!skip(i))
                SingleQOut(this, i, prevQs + i * 4, SqtQuat(sqt, boneIdx[i]));
    }
    return true;
}

// FUNC_AT(0x00101f00)
FnDeltaSingleQ* FnDeltaSingleQ::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnDeltaSingleQ::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// FnDeltaQFast. Data: DeltaQFastHeader. The ranges are unquantised once when the anim is set. The next key's
// quaternions are kept in a second buffer that becomes the first when time reaches it.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00102e30)
void DeltaQFastMinRange::UnQuantize(DeltaQFastMinRangef *out) {
    EAGL_UNTESTED("DeltaQFastMinRange::UnQuantize");
    for (int c = 0; c < 4; c++)
        out->min[c] = float(min[c] * double(kTwoOver65535) - 1.0);
    for (int c = 0; c < 4; c++)
        out->range[c] = range[c] * kTwoOver65535;
}

// FUNC_AT(0x00102f00)
void DeltaQFastPhysical::UnQuantize(float *q) {
    EAGL_UNTESTED("DeltaQFastPhysical::UnQuantize");
    for (int c = 0; c < 3; c++)
        q[c] = float((xyz[c] >> 4) * double(kTwoOver4095) - 1.0);
    int w = ((xyz[0] & 15) << 8) + ((xyz[1] & 15) << 4) + (xyz[2] & 15);
    q[3] = float(w * double(kTwoOver4095) - 1.0);
}

// FUNC_AT(0x00102fb0)
void DeltaQFastDelta::UnQuantize(const DeltaQFastMinRangef *range, float *q) {
    EAGL_UNTESTED("DeltaQFastDelta::UnQuantize");
    for (int c = 0; c < 3; c++)
        q[c] = float((xyz[c] >> 2) * double(range->range[c]) * kOneOver63 + range->min[c]);
    int w = ((((xyz[0] & 3) << 2) + (xyz[1] & 3)) << 2) + (xyz[2] & 3);
    q[3] = float(w * double(range->range[3]) * kOneOver63 + range->min[3]);
}

// Past the bins: the constant bone indexes (both headers' layouts).
template <typename Header>
static uint8_t* ConstBoneIdxs(Header *d) {
    int shift = d->binLengthPower;
    uint32_t keys = d->keys;
    int n = d->bones;
    uint32_t unit = 1u << shift;
    uint32_t bins = keys / unit, rem = keys % unit;
    int binSize = ((unit + 1) * n * 3 + 1) & ~1;
    uint8_t *p = reinterpret_cast<uint8_t *>(d->ranges + n) + binSize * bins;
    if (int(rem) > 0)
        p += (rem + 1) * n * 3;
    return p;
}

// FUNC_AT(0x00103060)
DeltaQFastPhysical* DeltaQFastHeader::GetConstPhysical() {
    EAGL_UNTESTED("DeltaQFastHeader::GetConstPhysical");
    uintptr_t p = reinterpret_cast<uintptr_t>(ConstBoneIdxs(this));
    return reinterpret_cast<DeltaQFastPhysical *>((p + constBones + 1) & ~uintptr_t(1));
}

// FUNC_AT(0x00103230)
void DeltaQFastHeader::GetArrays(DeltaQFastMinRange **minRanges, uint8_t **bins, uint8_t **constBoneIdxs,
                                 DeltaQFastPhysical **constPhysical) {
    EAGL_UNTESTED("DeltaQFastHeader::GetArrays");
    *minRanges = ranges;
    *bins = reinterpret_cast<uint8_t *>(ranges + bones);
    *constBoneIdxs = ConstBoneIdxs(this);
    *constPhysical = GetConstPhysical();
}

// FUNC_AT(0x00101f40)
FnDeltaQFast* FnDeltaQFast::Construct() {
    EAGL_UNTESTED("FnDeltaQFast::Construct");
    FnAnimMemoryMap::Construct();
    minRangesf = NULL;
    bins = NULL;
    prevQBlock = NULL;
    prevQs = NULL;
    nextQBlock = NULL;
    nextQs = NULL;
    constBoneIdxs = NULL;
    constPhysical = NULL;
    boneMask = NULL;
    vtable = VtDeltaQFast;
    binSize = -1;
    prevKey = -1;
    nextKey = -1;
    type = kDeltaQFast;
    return this;
}

// FUNC_AT(0x00101f90)
void FnDeltaQFast::Destruct() {
    EAGL_UNTESTED("FnDeltaQFast::Destruct");
    vtable = VtDeltaQFast;
    if (prevQBlock != NULL) {
        AnimPool_FreeBlock(prevQBlock);
        AnimPool_FreeBlock(minRangesf);
    }
    if (nextQBlock != NULL)
        AnimPool_FreeBlock(nextQBlock);
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x00101ff0)
bool FnDeltaQFast::GetLength(float *length) {
    EAGL_UNTESTED("FnDeltaQFast::GetLength");
    return DeltaGetLength(QFastData(anim), length);
}

// FUNC_AT(0x00102040)
void FnDeltaQFast::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnDeltaQFast::Eval");
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQT, time, out, (void *)NULL);
}

// The next key's quaternions: from its bin's physical values, or the current ones plus a row of deltas.
// FUNC_AT(0x00102060)
void FnDeltaQFast::UpdateNextQs(DeltaQFastHeader *data, int ceilKey, int floorBin, int floorDelta) {
    EAGL_UNTESTED("FnDeltaQFast::UpdateNextQs");
    if (ceilKey == nextKey)
        return;
    int nextBin = ceilKey >> data->binLengthPower;
    uint8_t *np = bins + binSize * nextBin;
    if (nextBin != floorBin) {
        DeltaQFastPhysical *physical = reinterpret_cast<DeltaQFastPhysical *>(np);
        for (int i = 0; i < data->bones; i++)
            physical[i].UnQuantize(nextQs + i * 4);
    } else {
        DeltaQFastDelta *row = reinterpret_cast<DeltaQFastDelta *>(np + (floorDelta + 2) * data->bones * 3);
        for (int i = 0; i < data->bones; i++, row++) {
            float dq[4];
            row->UnQuantize(&minRangesf[i], dq);
            for (int c = 0; c < 4; c++)
                nextQs[i * 4 + c] = dq[c] + prevQs[i * 4 + c];
        }
    }
    nextKey = ceilKey;
}

// FUNC_AT(0x00102370)
void FnDeltaQFast::UpdateNextQsMask(DeltaQFastHeader *data, int ceilKey, int floorBin, int floorDelta, void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::UpdateNextQsMask");
    if (ceilKey == nextKey)
        return;
    const uint8_t *boneIdx = data->boneIdx;
    int nextBin = ceilKey >> data->binLengthPower;
    uint8_t *np = bins + binSize * nextBin;
    if (nextBin != floorBin) {
        DeltaQFastPhysical *physical = reinterpret_cast<DeltaQFastPhysical *>(np);
        for (int i = 0; i < data->bones; i++)
            if (InMask(mask, boneIdx[i]))
                physical[i].UnQuantize(nextQs + i * 4);
    } else {
        DeltaQFastDelta *row = reinterpret_cast<DeltaQFastDelta *>(np + (floorDelta + 2) * data->bones * 3);
        for (int i = 0; i < data->bones; i++, row++) {
            if (!InMask(mask, boneIdx[i]))
                continue;
            float dq[4];
            row->UnQuantize(&minRangesf[i], dq);
            for (int c = 0; c < 4; c++)
                nextQs[i * 4 + c] = dq[c] + prevQs[i * 4 + c];
        }
    }
    nextKey = ceilKey;
}

// Rows prevDelta + 2 .. floorDelta + 1 of the bin added on.
// FUNC_AT(0x001030c0)
void FnDeltaQFast::AddDelta(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs) {
    EAGL_UNTESTED("FnDeltaQFast::AddDelta");
    DeltaQFastDelta *row = reinterpret_cast<DeltaQFastDelta *>(bin + (prevDelta + 2) * data->bones * 3);
    if (prevDelta >= floorDelta)
        return;
    for (int f = floorDelta - prevDelta; f != 0; f--)
        for (int i = 0; i < data->bones; i++, row++) {
            float dq[4];
            row->UnQuantize(&minRangesf[i], dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = dq[c] + qs[i * 4 + c];
        }
}

// Rows prevDelta + 1 down to floorDelta + 2 taken off, the bones last to first.
// FUNC_AT(0x00103170)
void FnDeltaQFast::SubDelta(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs) {
    EAGL_UNTESTED("FnDeltaQFast::SubDelta");
    DeltaQFastDelta *row = reinterpret_cast<DeltaQFastDelta *>(bin + (prevDelta + 2) * data->bones * 3) - 1;
    if (prevDelta - 1 < floorDelta)
        return;
    for (int f = prevDelta - floorDelta; f != 0; f--)
        for (int i = data->bones - 1; i >= 0; i--, row--) {
            float dq[4];
            row->UnQuantize(&minRangesf[i], dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = qs[i * 4 + c] - dq[c];
        }
}

// FUNC_AT(0x001021a0)
void FnDeltaQFast::AddDeltaMask(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs,
                                void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::AddDeltaMask");
    const uint8_t *boneIdx = data->boneIdx;
    DeltaQFastDelta *row = reinterpret_cast<DeltaQFastDelta *>(bin + (prevDelta + 2) * data->bones * 3);
    if (prevDelta >= floorDelta)
        return;
    for (int f = floorDelta - prevDelta; f != 0; f--)
        for (int i = 0; i < data->bones; i++, row++) {
            if (!InMask(mask, boneIdx[i]))
                continue;
            float dq[4];
            row->UnQuantize(&minRangesf[i], dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = dq[c] + qs[i * 4 + c];
        }
}

// FUNC_AT(0x00102280)
void FnDeltaQFast::SubDeltaMask(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs,
                                void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::SubDeltaMask");
    const uint8_t *boneIdx = data->boneIdx;
    DeltaQFastDelta *row = reinterpret_cast<DeltaQFastDelta *>(bin + (prevDelta + 2) * data->bones * 3) - 1;
    if (prevDelta - 1 < floorDelta)
        return;
    for (int f = prevDelta - floorDelta; f != 0; f--)
        for (int i = data->bones - 1; i >= 0; i--, row--) {
            if (!InMask(mask, boneIdx[i]))
                continue;
            float dq[4];
            row->UnQuantize(&minRangesf[i], dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = qs[i * 4 + c] - dq[c];
        }
}

// The arrays, and the buffers (made again, without freeing the old ones, each time an anim is set).
// FUNC_AT(0x001032e0)
void FnDeltaQFast::InitBuffers() {
    EAGL_UNTESTED("FnDeltaQFast::InitBuffers");
    DeltaQFastHeader *d = QFastData(anim);
    DeltaQFastMinRange *minRanges;
    d->GetArrays(&minRanges, &bins, &constBoneIdxs, &constPhysical);
    binSize = (((1 << d->binLengthPower) + 1) * d->bones * 3 + 1) & ~1;
    if (d->bones == 0)
        return;
    prevQs = prevQBlock = static_cast<float *>(AnimPool_NewBlock(d->bones * sizeof(float[4])));
    nextQs = nextQBlock = static_cast<float *>(AnimPool_NewBlock(d->bones * sizeof(float[4])));
    minRangesf = static_cast<DeltaQFastMinRangef *>(AnimPool_NewBlock(d->bones * sizeof(DeltaQFastMinRangef)));
    for (int i = 0; i < d->bones; i++)
        minRanges[i].UnQuantize(&minRangesf[i]);
}

// FUNC_AT(0x00102500)
void FnDeltaQFast::SetAnimMemoryMap(uint8_t *data) {
    EAGL_UNTESTED("FnDeltaQFast::SetAnimMemoryMap");
    anim = data;
    InitBuffers();
}

// EvalSQT without and EvalSQTMask with a mask differ in three ways besides the mask tests: only the unmasked one
// decodes the bin again at its first key (offset 0), and each calls its own delta and next-key helpers.
static bool QFastEval(FnDeltaQFast *self, float time, float *sqt, void *mask) {
    DeltaQFastHeader *d = QFastData(self->anim);
    const uint8_t *boneIdx = d->boneIdx;
    auto skip = [&](int i) { return mask != NULL && !InMask(mask, boneIdx[i]); };
    int n = d->bones;
    if (n != 0) {
        int frame = Truncate(time);
        int k = FindKey(frame, d, self->prevKey);
        int shift = d->binLengthPower;
        int modMask = 0x7fffffff >> (31 - shift);
        int bin = k >> shift, offset = k & modMask;
        int prevBin = self->prevKey >> shift;
        if (self->nextKey == k) {
            float *t = self->prevQs;
            self->prevQs = self->nextQs;
            self->nextQs = t;
            self->nextKey = self->prevKey;
        } else {
            if (self->prevKey == k + 1) {
                for (int i = 0; i < d->bones; i++)
                    Copy4(self->nextQs + i * 4, self->prevQs + i * 4);
                self->nextKey = self->prevKey;
            }
            uint8_t *bp = self->bins + self->binSize * bin;
            bool restart = k < self->prevKey && ReverseDeltaSumEnabled == 0;
            int start;
            if (self->prevKey != -1 && bin == prevBin && (mask != NULL || offset != 0) && !restart) {
                start = self->prevKey & modMask;
            } else {
                DeltaQFastPhysical *physical = reinterpret_cast<DeltaQFastPhysical *>(bp);
                for (int i = 0; i < d->bones; i++)
                    if (!skip(i))
                        physical[i].UnQuantize(self->prevQs + i * 4);
                start = 0;
            }
            if (start < offset) {
                if (mask != NULL)
                    self->AddDeltaMask(bp, d, start, offset, self->prevQs, mask);
                else
                    self->AddDelta(bp, d, start, offset, self->prevQs);
            } else if (start > offset) {
                if (mask != NULL)
                    self->SubDeltaMask(bp, d, start, offset, self->prevQs, mask);
                else
                    self->SubDelta(bp, d, start, offset, self->prevQs);
            }
        }
        self->prevKey = k;

        float s;
        if (LerpFactor(time, frame, d, k, &s) && k < d->keys - 1) {
            if (mask != NULL)
                self->UpdateNextQsMask(d, k + 1, bin, offset, mask);
            else
                self->UpdateNextQs(d, k + 1, bin, offset);
            for (int i = 0; i < d->bones; i++) {
                if (skip(i))
                    continue;
                float *out = SqtQuat(sqt, boneIdx[i]);
                const float *a = self->prevQs + i * 4, *b = self->nextQs + i * 4;
                for (int c = 0; c < 4; c++)
                    out[c] = float((double(b[c]) - a[c]) * s + a[c]);
            }
        } else {
            for (int i = 0; i < d->bones; i++)
                if (!skip(i))
                    Copy4(SqtQuat(sqt, boneIdx[i]), self->prevQs + i * 4);
        }
    }
    for (int j = 0; j < d->constBones; j++) {
        int bone = self->constBoneIdxs[j];
        if (mask == NULL || InMask(mask, bone))
            self->constPhysical[j].UnQuantize(SqtQuat(sqt, bone));
    }
    return true;
}

// A new mask starts again from the bin.
// FUNC_AT(0x00102510)
bool FnDeltaQFast::EvalSQTMask(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::EvalSQTMask");
    if (mask != boneMask) {
        prevKey = -1;
        nextKey = -1;
        boneMask = mask;
    }
    return QFastEval(this, time, sqt, mask);
}

// FUNC_AT(0x001029f0)
bool FnDeltaQFast::EvalSQT(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::EvalSQT");
    if (mask != NULL)
        return EvalSQTMask(time, sqt, mask);
    if (boneMask != NULL) {
        prevKey = -1;
        nextKey = -1;
        boneMask = NULL;
    }
    return QFastEval(this, time, sqt, NULL);
}

// FUNC_AT(0x001032b0)
FnDeltaQFast* FnDeltaQFast::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnDeltaQFast::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// FnDeltaQ. Data: DeltaQHeader, as FnDeltaQFast's with DeltaQMinRange records (min xyz, range xyz) kept
// quantised, DeltaQPhysical values and DeltaQDelta rows. w is recovered from xyz after the deltas are summed. With
// a mask the cache is dropped after every call.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00103fa0)
void DeltaQMinRange::UnQuantize(DeltaQMinRangef *out) {
    EAGL_UNTESTED("DeltaQMinRange::UnQuantize");
    constexpr double k = double(kOneOver65535) + kOneOver65535;
    for (int c = 0; c < 3; c++)
        out->min[c] = float(min[c] * k - 1.0);
    for (int c = 0; c < 3; c++)
        out->range[c] = float(range[c] * k);
}

// FUNC_AT(0x001040a0)
void DeltaQPhysical::UnQuantize(float *q) {
    EAGL_UNTESTED("DeltaQPhysical::UnQuantize");
    constexpr double k = double(kOneOver65535) + kOneOver65535;
    constexpr double kx = double(kOneOver32767) + kOneOver32767;
    q[0] = float((xyz[0] >> 1) * kx - 1.0);
    q[1] = float(xyz[1] * k - 1.0);
    q[2] = float(xyz[2] * k - 1.0);
    DeltaQRecoverW(xyz[0] & 1, q);
}

// The next key's physical value, as DeltaQPhysical::UnQuantize but with its own order of the squares (x, z, y).
static void QPhysicalInline(const DeltaQPhysical *p, float *q) {
    constexpr double k = double(kOneOver65535) + kOneOver65535;
    constexpr double kx = double(kOneOver32767) + kOneOver32767;
    q[0] = float((p->xyz[0] >> 1) * kx - 1.0);
    q[1] = float(p->xyz[1] * k - 1.0);
    q[2] = float(p->xyz[2] * k - 1.0);
    double ss = double(q[0]) * q[0] + double(q[2]) * q[2];
    ss = ss + double(q[1]) * q[1];
    if (ss > 1.0) {
        double r = 1.0 / sqrt(ss);
        q[3] = 0.0f;
        q[0] = float(q[0] * r);
        q[1] = float(q[1] * r);
        q[2] = float(q[2] * r);
    } else {
        q[3] = float(sqrt(1.0 - ss));
        if (p->xyz[0] & 1)
            q[3] = -q[3];
    }
}

// A delta's x and y (unrounded) and z (rounded to float).
static void QDelta(const DeltaQDelta *row, const DeltaQMinRangef *range, double *dx, double *dy, float *dz) {
    *dx = (row->xyz[0] >> 1) * double(range->range[0]) * kOneOver127 + range->min[0];
    *dy = row->xyz[1] * double(range->range[1]) * kOneOver255 + range->min[1];
    *dz = float(row->xyz[2] * double(range->range[2]) * kOneOver255 + range->min[2]);
}

// FUNC_AT(0x00104120)
DeltaQPhysical* DeltaQHeader::GetConstPhysical() {
    EAGL_UNTESTED("DeltaQHeader::GetConstPhysical");
    uintptr_t p = reinterpret_cast<uintptr_t>(ConstBoneIdxs(this));
    return reinterpret_cast<DeltaQPhysical *>((p + constBones + 1) & ~uintptr_t(1));
}

// FUNC_AT(0x00104180)
void DeltaQHeader::GetArrays(DeltaQMinRange **minRanges, uint8_t **bins, uint8_t **constBoneIdxs,
                             DeltaQPhysical **constPhysical) {
    EAGL_UNTESTED("DeltaQHeader::GetArrays");
    *minRanges = ranges;
    *bins = reinterpret_cast<uint8_t *>(ranges + bones);
    *constBoneIdxs = ConstBoneIdxs(this);
    *constPhysical = GetConstPhysical();
}

// FUNC_AT(0x001033b0)
FnDeltaQ* FnDeltaQ::Construct() {
    EAGL_UNTESTED("FnDeltaQ::Construct");
    FnAnimMemoryMap::Construct();
    minRanges = NULL;
    bins = NULL;
    prevQBlock = NULL;
    prevQs = NULL;
    constBoneIdxs = NULL;
    constPhysical = NULL;
    vtable = VtDeltaQ;
    binSize = -1;
    prevKey = -1;
    type = kDeltaQ;
    return this;
}

// FUNC_AT(0x001033f0)
void FnDeltaQ::Destruct() {
    EAGL_UNTESTED("FnDeltaQ::Destruct");
    vtable = VtDeltaQ;
    if (prevQBlock != NULL)
        AnimPool_FreeBlock(prevQBlock);
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x00103420)
void FnDeltaQ::SetAnimMemoryMap(uint8_t *data) {
    EAGL_UNTESTED("FnDeltaQ::SetAnimMemoryMap");
    anim = data;
}

// FUNC_AT(0x00103430)
bool FnDeltaQ::GetLength(float *length) {
    EAGL_UNTESTED("FnDeltaQ::GetLength");
    return DeltaGetLength(QData(anim), length);
}

// FUNC_AT(0x00103480)
void FnDeltaQ::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnDeltaQ::Eval");
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQTMasked, time, (void *)NULL, out);
}

// FUNC_AT(0x001034a0)
bool FnDeltaQ::EvalSQT(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnDeltaQ::EvalSQT");
    return AnimVCall<bool>(this, kSlotEvalSQTMasked, time, mask, sqt);
}

// FUNC_AT(0x00104230)
void FnDeltaQ::InitBuffersAsRequired() {
    EAGL_UNTESTED("FnDeltaQ::InitBuffersAsRequired");
    if (bins != NULL)
        return;
    DeltaQHeader *d = QData(anim);
    DeltaQMinRange *ranges;
    d->GetArrays(&ranges, &bins, &constBoneIdxs, &constPhysical);
    binSize = (((1 << d->binLengthPower) + 1) * d->bones * 3 + 1) & ~1;
    if (d->bones == 0)
        return;
    prevQs = prevQBlock = static_cast<float *>(AnimPool_NewBlock(d->bones * sizeof(float[4])));
    minRanges = ranges;
}

// The original writes the masked and unmasked cases as one loop with a mask test, as here. Its constant bones
// test the mask with the animated bones' indexes (boneIdx[j], not constBoneIdxs[j]) - kept.
// FUNC_AT(0x001034c0)
bool FnDeltaQ::EvalSQTMasked(float time, void *mask, float *sqt) {
    EAGL_UNTESTED("FnDeltaQ::EvalSQTMasked");
    InitBuffersAsRequired();
    DeltaQHeader *d = QData(anim);
    const uint8_t *boneIdx = d->boneIdx;
    auto skip = [&](int i) { return mask != NULL && !InMask(mask, boneIdx[i]); };
    if (d->bones != 0) {
        int frame = Truncate(time);
        int k = FindKey(frame, d, prevKey);
        int shift = d->binLengthPower;
        int modMask = 0x7fffffff >> (31 - shift);
        int bin = k >> shift, offset = k & modMask;
        int prevBin = prevKey >> shift;
        uint8_t *bp = bins + binSize * bin;
        bool restart = k < prevKey && ReverseDeltaSumEnabled == 0;
        int start;
        if (prevKey != -1 && bin == prevBin && offset != 0 && !restart) {
            start = prevKey & modMask;
        } else {
            DeltaQPhysical *physical = reinterpret_cast<DeltaQPhysical *>(bp);
            for (int i = 0; i < d->bones; i++)
                if (!skip(i))
                    physical[i].UnQuantize(prevQs + i * 4);
            start = 0;
        }
        if (start != offset) {
            if (start < offset) {
                const DeltaQDelta *row = reinterpret_cast<const DeltaQDelta *>(bp + (start + 2) * d->bones * 3);
                for (int f = offset - start; f != 0; f--)
                    for (int i = 0; i < d->bones; i++, row++) {
                        if (skip(i))
                            continue;
                        DeltaQMinRangef range;
                        double dx, dy;
                        float dz;
                        minRanges[i].UnQuantize(&range);
                        QDelta(row, &range, &dx, &dy, &dz);
                        float *v = prevQs + i * 4;
                        v[0] = float(dx + v[0]);
                        v[1] = float(dy + v[1]);
                        v[2] = dz + v[2];
                    }
            } else {
                const DeltaQDelta *row = reinterpret_cast<const DeltaQDelta *>(bp + (start + 2) * d->bones * 3) - 1;
                if (start - 1 >= offset)
                    for (int f = start - offset; f != 0; f--)
                        for (int i = d->bones - 1; i >= 0; i--, row--) {
                            if (skip(i))
                                continue;
                            DeltaQMinRangef range;
                            double dx, dy;
                            float dz;
                            minRanges[i].UnQuantize(&range);
                            QDelta(row, &range, &dx, &dy, &dz);
                            float *v = prevQs + i * 4;
                            v[0] = float(v[0] - dx);
                            v[1] = float(v[1] - dy);
                            v[2] = v[2] - dz;
                        }
            }
            // w from the summed xyz, its sign from the key's row (inline DeltaQRecoverW in the original)
            const DeltaQDelta *row = reinterpret_cast<const DeltaQDelta *>(bp + (offset + 1) * d->bones * 3);
            for (int i = 0; i < d->bones; i++, row++)
                if (!skip(i))
                    DeltaQRecoverW(row->xyz[0] & 1, prevQs + i * 4);
        }
        prevKey = k;

        float s;
        if (LerpFactor(time, frame, d, k, &s) && k < d->keys - 1) {
            int nextBin = (k + 1) >> shift;
            uint8_t *np = bins + binSize * nextBin;
            if (nextBin == bin) {
                const DeltaQDelta *row = reinterpret_cast<const DeltaQDelta *>(np + (offset + 2) * d->bones * 3);
                for (int i = 0; i < d->bones; i++, row++) {
                    if (skip(i))
                        continue;
                    DeltaQMinRangef range;
                    float next[4];
                    double dx, dy;
                    float dz;
                    minRanges[i].UnQuantize(&range);
                    QDelta(row, &range, &dx, &dy, &dz);
                    const float *v = prevQs + i * 4;
                    next[0] = float(dx + v[0]);
                    next[1] = float(dy + v[1]);
                    double z = double(dz) + v[2];
                    next[2] = float(z);
                    double ss = z * next[2] + double(next[1]) * next[1];
                    ss = ss + double(next[0]) * next[0];
                    if (ss > 1.0) {
                        double r = 1.0 / sqrt(ss);
                        next[3] = 0.0f;
                        next[0] = float(next[0] * r);
                        next[1] = float(next[1] * r);
                        next[2] = float(r * next[2]);
                    } else {
                        next[3] = float(sqrt(1.0 - ss));
                        if (row->xyz[0] & 1)
                            next[3] = -next[3];
                    }
                    AnimQuatNLerp(s, v, next, SqtQuat(sqt, boneIdx[i]));
                }
            } else {
                DeltaQPhysical *physical = reinterpret_cast<DeltaQPhysical *>(np);
                for (int i = 0; i < d->bones; i++) {
                    if (skip(i))
                        continue;
                    float next[4];
                    QPhysicalInline(&physical[i], next);
                    AnimQuatNLerp(s, prevQs + i * 4, next, SqtQuat(sqt, boneIdx[i]));
                }
            }
        } else {
            for (int i = 0; i < d->bones; i++)
                if (!skip(i))
                    Copy4(SqtQuat(sqt, boneIdx[i]), prevQs + i * 4);
        }
    }
    for (int j = 0; j < d->constBones; j++)
        if (!skip(j))
            constPhysical[j].UnQuantize(SqtQuat(sqt, constBoneIdxs[j]));
    if (mask != NULL)
        prevKey = -1;
    return true;
}

// FUNC_AT(0x00104200)
FnDeltaQ* FnDeltaQ::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnDeltaQ::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}
