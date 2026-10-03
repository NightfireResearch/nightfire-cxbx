#include "AnimDeltaQ.h"

#include "AnimUntested.h"

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
// original's order, with a float store for every store.
// ---------------------------------------------------------------------------------------------------------------

#define EaglFree     (*(void (**)(void *data, uint32_t size))0x001caf6cu)
#define FreeBySize   ((void **)0x00241520u)
#define VtDeltaSingleQ ((const void *)0x001a1360u)
#define VtDeltaQFast   ((const void *)0x001a13e0u)
#define VtDeltaQ       ((const void *)0x001a1450u)
#define ReverseDeltaSumEnabled (*(uint8_t *)0x001ceb4cu)   // 0: going back decodes the bin again
#define kOne         (*(const float *)0x00189de8u)          // 1.0
#define kZero        (*(const float *)0x00189decu)          // 0.0
#define SqPhysScale  (*(const float *)0x001a1348u)          // 1/255 (doubled)
#define SqRangeScale (*(const float *)0x001a1350u)          // 1/65535 (doubled for min and range)
#define SqDeltaScale (*(const float *)0x001a1354u)          // 1/15
#define SqConstBase  (*(const float *)0x001a1358u)          // -pi
#define SqAngleRange (*(const float *)0x00241ae0u)          // a global set at run time (2 pi, presumably)
#define QfDeltaScale (*(const float *)0x001a13d4u)          // 1/63
#define QfRangeScale (*(const float *)0x001a13d8u)          // 2/65535
#define QfPhysScale  (*(const float *)0x001a13dcu)          // 2/4095
#define QDeltaScaleX (*(const float *)0x001a143cu)          // 1/127
#define QDeltaScale  (*(const float *)0x001a1440u)          // 1/255
#define QPhysScaleX  (*(const float *)0x001a1444u)          // 1/32767 (doubled)
#define QRangeScale  (*(const float *)0x001a1448u)          // 1/65535 (doubled)

enum { kSlotEvalSQTMasked = 18 };    // FnDeltaSingleQ's and FnDeltaQ's extra virtual

static inline int Truncate(float f) {
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

static inline void FreeSized(void *block) {
    uint32_t sizeClass = ((uint32_t *)block)[-1];
    *(void **)block = FreeBySize[sizeClass];
    FreeBySize[sizeClass] = block;
}

static inline bool InMask(const void *mask, int bone) {
    return (((const uint32_t *)mask)[bone >> 5] & (1u << (bone & 31))) != 0;
}

// A bone's quaternion in the SQT records.
static inline float* SqtQuat(float *sqt, int bone) {
    return sqt + bone * 12 + 4;
}

static inline void Copy4(float *to, const float *from) {
    memcpy(to, from, 16);
}

// The three data formats share the header's first 0x10 bytes: u16 type, u16 checksum, u16 keys, u8 bones, u8,
// u16 *times (or none: a key a frame), u8 *boneIdx.
static inline int Keys(const uint8_t *d) {
    return *(const uint16_t *)(d + 4);
}
static inline const uint16_t* Times(const uint8_t *d) {
    return *(const uint16_t *const *)(d + 8);
}
static inline const uint8_t* BoneIdx(const uint8_t *d) {
    return *(const uint8_t *const *)(d + 0xc);
}

// The key below the time: from the cached key, back or forward through the key times.
static int FindKey(int frame, const uint8_t *d, int cached) {
    const uint16_t *times = Times(d);
    int keys = Keys(d);
    if (times == NULL)
        return frame < 0 ? 0 : (frame >= keys ? keys - 1 : frame);
    if (frame < (int)times[0])
        return 0;
    int i = cached >= 1 ? cached - 1 : 0;
    if ((int)times[i] > frame) {
        while (i > 0 && (int)times[i] > frame)
            i--;
    } else {
        int last = keys - 2;
        while (i < last && (int)times[i + 1] <= frame)
            i++;
    }
    return i + 1;
}

// The fraction of the way from key k to k + 1; false when the time is exactly on key k (FCOMP, TEST AH,0x44,
// JNP: ordered equal), so that a NaN time interpolates.
static bool LerpFactor(float time, int frame, const uint8_t *d, int k, float *s) {
    const uint16_t *times = Times(d);
    if (times == NULL) {
        double f = (double)frame;
        if ((double)time == f)
            return false;
        *s = (float)((double)time - f);
    } else if (k == 0) {
        if ((double)time == (double)kZero)
            return false;
        *s = (float)((double)time / (double)(int)times[0]);
    } else {
        double p = (double)(int)times[k - 1];
        if ((double)time == p)
            return false;
        *s = (float)(((double)time - p) / ((double)(int)times[k] - p));
    }
    return true;
}

// The last key time plus one, or the key count (all three GetLengths).
static bool DeltaGetLength(const uint8_t *d, float *length) {
    const uint16_t *times = Times(d);
    int keys = Keys(d);
    int n = times == NULL ? keys : (int)times[keys - 2] + 1;
    *length = (float)n;
    return true;
}

// ---- quaternion helpers

// a * b * c for a and c rotations about y (a pre and post multiplication) - the product with two float
// temporaries, as the original.
// FUNC_AT(0x00100ac0)
void QuatMultXxYxZ(const float *a, const float *b, const float *c, float *out) {
    EAGL_UNTESTED("QuatMultXxYxZ");
    double A = (double)a[0] * (double)b[3];
    double B = (double)b[1] * (double)a[3];
    float C = (float)-((double)b[1] * (double)a[0]);
    float D = (float)((double)a[3] * (double)b[3]);
    out[0] = (float)(A * (double)c[3] - B * (double)c[2]);
    out[1] = (float)(A * (double)c[2] + B * (double)c[3]);
    out[2] = (float)((double)D * (double)c[2] + (double)C * (double)c[3]);
    out[3] = (float)((double)D * (double)c[3] - (double)C * (double)c[2]);
}

// a * b for a rotation a about x (x and w only). No callers: FnDeltaSingleQ::EvalSQTMasked has it inline.
// FUNC_AT(0x00100b40)
void QuatMultXxQ(const float *a, const float *b, float *out) {
    EAGL_UNTESTED("QuatMultXxQ");
    out[0] = (float)((double)a[0] * (double)b[3] + (double)a[3] * (double)b[0]);
    out[1] = (float)((double)a[3] * (double)b[1] + (double)b[2] * (double)a[0]);
    out[2] = (float)((double)a[3] * (double)b[2] - (double)b[1] * (double)a[0]);
    out[3] = (float)((double)a[3] * (double)b[3] - (double)a[0] * (double)b[0]);
}

// a * b for a rotation b about z (z and w only). No callers: inline in FnDeltaSingleQ::EvalSQTMasked.
// FUNC_AT(0x00100b90)
void QuatMultQxZ(const float *a, const float *b, float *out) {
    EAGL_UNTESTED("QuatMultQxZ");
    out[0] = (float)((double)a[0] * (double)b[3] - (double)b[2] * (double)a[1]);
    out[1] = (float)((double)a[1] * (double)b[3] + (double)b[2] * (double)a[0]);
    out[2] = (float)((double)b[2] * (double)a[3] + (double)a[2] * (double)b[3]);
    out[3] = (float)((double)a[3] * (double)b[3] - (double)b[2] * (double)a[2]);
}

// (b - a) * t + a, normalised. z and w are stored to float first; w's square is the unrounded w times the
// rounded one, and x is scaled by the unrounded 1/length, the others by it rounded.
// FUNC_AT(0x00101e40)
void AnimQuatNLerp(float t, const float *a, const float *b, float *out) {
    EAGL_UNTESTED("AnimQuatNLerp");
    double x = ((double)b[0] - (double)a[0]) * (double)t + (double)a[0];
    double y = ((double)b[1] - (double)a[1]) * (double)t + (double)a[1];
    float z = (float)(((double)b[2] - (double)a[2]) * (double)t + (double)a[2]);
    double w = ((double)b[3] - (double)a[3]) * (double)t + (double)a[3];
    float wf = (float)w;
    double ss = w * (double)wf + (double)z * (double)z;
    ss = ss + y * y;
    ss = ss + x * x;
    double r = (double)kOne / sqrt(ss);
    float rf = (float)r;
    out[0] = (float)(r * x);
    out[1] = (float)((double)rf * y);
    out[2] = (float)((double)rf * (double)z);
    out[3] = (float)((double)rf * (double)wf);
}

// w from xyz: sqrt(1 - |xyz|^2), negated for a positive sign, or xyz normalised and w 0 when |xyz| > 1.
// FUNC_AT(0x00104030)
void DeltaQRecoverW(int sign, float *q) {
    EAGL_UNTESTED("DeltaQRecoverW");
    double x = q[0], y = q[1], z = q[2];
    double ss = x * x + y * y;
    ss = ss + z * z;
    if (ss > (double)kOne) {            // FCOM, TEST AH,0x41, JNE: below, equal and unordered take w
        double r = (double)kOne / sqrt(ss);
        q[3] = 0.0f;
        q[0] = (float)(r * (double)q[0]);
        q[1] = (float)(r * (double)q[1]);
        q[2] = (float)(r * (double)q[2]);
    } else {
        double w = sqrt((double)kOne - ss);
        q[3] = (float)w;
        if (sign > 0)
            q[3] = (float)-w;
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
// FnDeltaSingleQ. Data: the 0x10-byte header (byte 6 the bone count, byte 7 the bin length power), a 0xe-byte
// range record per bone, then the bins: per bone a 2-byte physical value (the axis component and w, 8 bits each),
// then a row of 1-byte deltas (4 bits each) per key. A bone's rotation is pre * q * post, where q turns about the
// record's axis and pre/post are Euler rotations from its two constant angles.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00101aa0)
void DeltaSingleQMinRange::UnQuantize(DeltaSingleQMinRangef *out) {
    EAGL_UNTESTED("DeltaSingleQMinRange::UnQuantize");
    const uint16_t *p = (const uint16_t *)this;
    double k = (double)SqRangeScale;
    double k2 = k + k;
    out->min[0] = (float)((double)(int)p[2] * k2 - (double)kOne);
    out->min[1] = (float)((double)(int)p[3] * k2 - (double)kOne);
    out->range[0] = (float)((double)(int)p[4] * k2);
    out->range[1] = (float)((double)(int)p[5] * k2);
    out->index = ((const uint8_t *)this)[0xc];
    out->const0 = (float)((double)(int)p[0] * (double)SqAngleRange * k + (double)SqConstBase);
    out->const1 = (float)((double)(int)p[1] * (double)SqAngleRange * k + (double)SqConstBase);
}

// The axis component (x for index 0, y for 1, else z) and w; the other two components 0.
// FUNC_AT(0x00101b50)
void DeltaSingleQPhysical::UnQuantize(int index, float *q) {
    EAGL_UNTESTED("DeltaSingleQPhysical::UnQuantize");
    const uint8_t *p = (const uint8_t *)this;
    double k2 = (double)SqPhysScale + (double)SqPhysScale;
    q[2] = 0.0f;
    q[1] = 0.0f;
    q[0] = 0.0f;
    float v = (float)((double)(int)p[0] * k2 - (double)kOne);
    if (index == 0)
        q[0] = v;
    else if (index == 1)
        q[1] = v;
    else
        q[2] = v;
    q[3] = (float)((double)(int)p[1] * k2 - (double)kOne);
}

// FUNC_AT(0x00101be0)
void DeltaSingleQDelta::UnQuantize(const DeltaSingleQMinRangef *range, float *q) {
    EAGL_UNTESTED("DeltaSingleQDelta::UnQuantize");
    uint8_t b = *(const uint8_t *)this;
    q[2] = 0.0f;
    q[1] = 0.0f;
    q[0] = 0.0f;
    float v = (float)((double)(int)(b >> 4) * (double)range->range[0] * (double)SqDeltaScale +
                      (double)range->min[0]);
    if (range->index == 0)
        q[0] = v;
    else if (range->index == 1)
        q[1] = v;
    else
        q[2] = v;
    q[3] = (float)((double)(int)(b & 15) * (double)range->range[1] * (double)SqDeltaScale + (double)range->min[1]);
}

// The bone's pre * q * post. The original has these products inline (x and z everywhere, y in its unmasked
// no-lerp loop); checked term by term against the listing: every component is one sum or difference of two
// float-by-float products (exact in double), with the same minuend and subtrahend as the functions here, and the
// inline y product keeps QuatMultXxYxZ's two unrounded products and two float temporaries - so bit-identical.
static void SingleQOut(FnDeltaSingleQ *self, int i, const float *q, float *out) {
    int index = *(const uint16_t *)(self->minRanges + i * 0xe + 0xc);
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
        FreeSized(prevQBlock);
        FreeSized(preMultQs);
        FreeSized(postMultQs);
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
    return DeltaGetLength(anim, length);
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
    typedef void (*EulerToQuat)(const float *angles, float *quat);
    const EulerToQuat eulerToQuat = (EulerToQuat)0x000fde00u;   // EAGLAnim_EulerToQuat
    if (prevQs != NULL)
        return;
    uint8_t *d = anim;
    bins = d + 0x10 + d[6] * 0xe;
    binSize = (((1 << d[7]) + 1) * d[6] + 1) & ~1;
    prevQs = prevQBlock = (float *)AnimPool_NewBlock((uint32_t)d[6] << 4);
    minRanges = d + 0x10;
    preMultQs = (float *)AnimPool_NewBlock((uint32_t)d[6] << 4);
    postMultQs = (float *)AnimPool_NewBlock((uint32_t)d[6] << 4);
    for (int i = 0; i < d[6]; i++) {
        DeltaSingleQMinRangef r;
        ((DeltaSingleQMinRange *)(minRanges + i * 0xe))->UnQuantize(&r);
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
            eulerToQuat(e, post);
        } else if (r.index == 1) {
            e[0] = r.const0;
            e[1] = 0.0f;
            e[2] = 0.0f;
            eulerToQuat(e, pre);
            e[0] = 0.0f;
            e[1] = 0.0f;
            e[2] = r.const1;
            eulerToQuat(e, post);
        } else {
            e[0] = r.const0;
            e[1] = r.const1;
            e[2] = 0.0f;
            eulerToQuat(e, pre);
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
    uint8_t *d = anim;
    const uint8_t *boneIdx = BoneIdx(d);
    int frame = Truncate(time);
    int k = FindKey(frame, d, prevKey);
    int shift = d[7];
    int modMask = (int)(0x7fffffffu >> (31 - shift));
    int bin = k >> shift, offset = k & modMask;
    uint8_t *bp = bins + binSize * bin;
    int cached = prevKey;
#define SKIP(i) (mask != NULL && !InMask(mask, boneIdx[i]))
    int start;
    if (cached != -1 && bin == cached >> shift && k >= cached) {
        start = cached & modMask;
    } else {
        for (int i = 0; i < d[6]; i++) {
            if (SKIP(i))
                continue;
            int index = *(const uint16_t *)(minRanges + i * 0xe + 0xc);
            ((DeltaSingleQPhysical *)(bp + i * 2))->UnQuantize(index, prevQs + i * 4);
        }
        start = 0;
    }
    if (start < offset) {
        const uint8_t *row = bp + (start + 2) * d[6];
        for (int f = offset - start; f != 0; f--) {
            for (int i = 0; i < d[6]; i++, row++) {
                if (SKIP(i))
                    continue;
                DeltaSingleQMinRangef r;
                float dq[4];
                ((DeltaSingleQMinRange *)(minRanges + i * 0xe))->UnQuantize(&r);
                ((DeltaSingleQDelta *)row)->UnQuantize(&r, dq);
                float *v = prevQs + i * 4;
                for (int c = 0; c < 4; c++)
                    v[c] = (float)((double)dq[c] + (double)v[c]);
            }
        }
    }
    prevKey = k;

    float s;
    if (LerpFactor(time, frame, d, k, &s) && k < Keys(d) - 1) {
        int nextBin = (k + 1) >> shift;
        uint8_t *np = bins + binSize * nextBin;
        if (nextBin == bin) {
            const uint8_t *row = np + (offset + 2) * d[6];
            for (int i = 0; i < d[6]; i++) {
                if (SKIP(i))
                    continue;
                DeltaSingleQMinRangef r;
                float dq[4], next[4], q[4];
                ((DeltaSingleQMinRange *)(minRanges + i * 0xe))->UnQuantize(&r);
                ((DeltaSingleQDelta *)(row + i))->UnQuantize(&r, dq);
                const float *v = prevQs + i * 4;
                for (int c = 0; c < 4; c++)
                    next[c] = (float)((double)dq[c] + (double)v[c]);
                AnimQuatNLerp(s, v, next, q);
                SingleQOut(this, i, q, SqtQuat(sqt, boneIdx[i]));
            }
        } else {
            for (int i = 0; i < d[6]; i++) {
                if (SKIP(i))
                    continue;
                float next[4], q[4];
                int index = *(const uint16_t *)(minRanges + i * 0xe + 0xc);
                ((DeltaSingleQPhysical *)(np + i * 2))->UnQuantize(index, next);
                AnimQuatNLerp(s, prevQs + i * 4, next, q);
                SingleQOut(this, i, q, SqtQuat(sqt, boneIdx[i]));
            }
        }
    } else {
        for (int i = 0; i < d[6]; i++)
            if (!SKIP(i))
                SingleQOut(this, i, prevQs + i * 4, SqtQuat(sqt, boneIdx[i]));
    }
#undef SKIP
    return true;
}

// FUNC_AT(0x00101f00)
FnDeltaSingleQ* FnDeltaSingleQ::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnDeltaSingleQ::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, 0x30);
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// FnDeltaQFast. Data: the header (byte 7 the constant bone count, byte 0x10 the bin length power), a 0x10-byte
// range record per bone from +0x12, the bins (per bone a 6-byte physical quaternion, then a row of 3-byte deltas
// per key), the constant bones' indexes and, on a 2-byte boundary, their physical quaternions. The ranges are
// unquantised once when the anim is set. The next key's quaternions are kept in a second buffer that becomes the
// first when time reaches it.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00102e30)
void DeltaQFastMinRange::UnQuantize(float *out) {
    EAGL_UNTESTED("DeltaQFastMinRange::UnQuantize");
    const uint16_t *p = (const uint16_t *)this;
    for (int c = 0; c < 4; c++)
        out[c] = (float)((double)(int)p[c] * (double)QfRangeScale - (double)kOne);
    for (int c = 4; c < 8; c++)
        out[c] = (float)((double)(int)p[c] * (double)QfRangeScale);
}

// FUNC_AT(0x00102f00)
void DeltaQFastPhysical::UnQuantize(float *q) {
    EAGL_UNTESTED("DeltaQFastPhysical::UnQuantize");
    const uint16_t *p = (const uint16_t *)this;
    for (int c = 0; c < 3; c++)
        q[c] = (float)((double)(int)(uint16_t)(p[c] >> 4) * (double)QfPhysScale - (double)kOne);
    int w = ((p[0] & 15) << 8) + ((p[1] & 15) << 4) + (p[2] & 15);
    q[3] = (float)((double)w * (double)QfPhysScale - (double)kOne);
}

// FUNC_AT(0x00102fb0)
void DeltaQFastDelta::UnQuantize(const float *range, float *q) {
    EAGL_UNTESTED("DeltaQFastDelta::UnQuantize");
    const uint8_t *b = (const uint8_t *)this;
    for (int c = 0; c < 3; c++)
        q[c] = (float)((double)(int)(b[c] >> 2) * (double)range[4 + c] * (double)QfDeltaScale + (double)range[c]);
    int w = (uint8_t)(((((b[0] & 3) << 2) + (b[1] & 3)) << 2) + (b[2] & 3));
    q[3] = (float)((double)w * (double)range[7] * (double)QfDeltaScale + (double)range[3]);
}

// Past the bins: the constant bone indexes.
static uint8_t* QFastConstBoneIdxs(uint8_t *d, int rangeSize) {
    int shift = d[0x10];
    uint32_t keys = Keys(d);
    int n = d[6];
    uint32_t unit = 1u << shift;
    uint32_t bins = keys / unit, rem = keys % unit;
    int binSize = (((int)unit + 1) * n * 3 + 1) & ~1;
    uint8_t *p = d + binSize * (int)bins + n * rangeSize + 0x12;
    if ((int)rem > 0)
        p += ((int)rem + 1) * n * 3;
    return p;
}

// FUNC_AT(0x00103060)
uint8_t* DeltaQFastHeader::GetConstPhysical() {
    EAGL_UNTESTED("DeltaQFastHeader::GetConstPhysical");
    uint8_t *d = (uint8_t *)this;
    uintptr_t p = (uintptr_t)QFastConstBoneIdxs(d, 0x10);
    return (uint8_t *)((p + d[7] + 1) & ~(uintptr_t)1);
}

// FUNC_AT(0x00103230)
void DeltaQFastHeader::GetArrays(uint8_t **minRanges, uint8_t **bins, uint8_t **constBoneIdxs,
                                 uint8_t **constPhysical) {
    EAGL_UNTESTED("DeltaQFastHeader::GetArrays");
    uint8_t *d = (uint8_t *)this;
    *minRanges = d + 0x12;
    *bins = d + 0x12 + d[6] * 0x10;
    *constBoneIdxs = QFastConstBoneIdxs(d, 0x10);
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
        FreeSized(prevQBlock);
        FreeSized(minRangesf);
    }
    if (nextQBlock != NULL)
        FreeSized(nextQBlock);
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x00101ff0)
bool FnDeltaQFast::GetLength(float *length) {
    EAGL_UNTESTED("FnDeltaQFast::GetLength");
    return DeltaGetLength(anim, length);
}

// FUNC_AT(0x00102040)
void FnDeltaQFast::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnDeltaQFast::Eval");
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQT, time, out, (void *)NULL);
}

// The next key's quaternions: from its bin's physical values, or the current ones plus a row of deltas.
// FUNC_AT(0x00102060)
void FnDeltaQFast::UpdateNextQs(uint8_t *data, int ceilKey, int floorBin, int floorDelta) {
    EAGL_UNTESTED("FnDeltaQFast::UpdateNextQs");
    if (ceilKey == nextKey)
        return;
    int nextBin = ceilKey >> data[0x10];
    uint8_t *np = bins + binSize * nextBin;
    if (nextBin != floorBin) {
        for (int i = 0; i < data[6]; i++)
            ((DeltaQFastPhysical *)(np + i * 6))->UnQuantize(nextQs + i * 4);
    } else {
        const uint8_t *row = np + (floorDelta + 2) * data[6] * 3;
        for (int i = 0; i < data[6]; i++, row += 3) {
            float dq[4];
            ((DeltaQFastDelta *)row)->UnQuantize(minRangesf + i * 8, dq);
            for (int c = 0; c < 4; c++)
                nextQs[i * 4 + c] = (float)((double)dq[c] + (double)prevQs[i * 4 + c]);
        }
    }
    nextKey = ceilKey;
}

// FUNC_AT(0x00102370)
void FnDeltaQFast::UpdateNextQsMask(uint8_t *data, int ceilKey, int floorBin, int floorDelta, void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::UpdateNextQsMask");
    if (ceilKey == nextKey)
        return;
    const uint8_t *boneIdx = BoneIdx(data);
    int nextBin = ceilKey >> data[0x10];
    uint8_t *np = bins + binSize * nextBin;
    if (nextBin != floorBin) {
        for (int i = 0; i < data[6]; i++)
            if (InMask(mask, boneIdx[i]))
                ((DeltaQFastPhysical *)(np + i * 6))->UnQuantize(nextQs + i * 4);
    } else {
        const uint8_t *row = np + (floorDelta + 2) * data[6] * 3;
        for (int i = 0; i < data[6]; i++, row += 3) {
            if (!InMask(mask, boneIdx[i]))
                continue;
            float dq[4];
            ((DeltaQFastDelta *)row)->UnQuantize(minRangesf + i * 8, dq);
            for (int c = 0; c < 4; c++)
                nextQs[i * 4 + c] = (float)((double)dq[c] + (double)prevQs[i * 4 + c]);
        }
    }
    nextKey = ceilKey;
}

// Rows prevDelta + 2 .. floorDelta + 1 of the bin added on.
// FUNC_AT(0x001030c0)
void FnDeltaQFast::AddDelta(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs) {
    EAGL_UNTESTED("FnDeltaQFast::AddDelta");
    const uint8_t *row = bin + (prevDelta + 2) * data[6] * 3;
    if (prevDelta >= floorDelta)
        return;
    for (int f = floorDelta - prevDelta; f != 0; f--)
        for (int i = 0; i < data[6]; i++, row += 3) {
            float dq[4];
            ((DeltaQFastDelta *)row)->UnQuantize(minRangesf + i * 8, dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = (float)((double)dq[c] + (double)qs[i * 4 + c]);
        }
}

// Rows prevDelta + 1 down to floorDelta + 2 taken off, the bones last to first.
// FUNC_AT(0x00103170)
void FnDeltaQFast::SubDelta(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs) {
    EAGL_UNTESTED("FnDeltaQFast::SubDelta");
    const uint8_t *row = bin + (prevDelta + 2) * data[6] * 3 - 3;
    if (prevDelta - 1 < floorDelta)
        return;
    for (int f = prevDelta - floorDelta; f != 0; f--)
        for (int i = data[6] - 1; i >= 0; i--, row -= 3) {
            float dq[4];
            ((DeltaQFastDelta *)row)->UnQuantize(minRangesf + i * 8, dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = (float)((double)qs[i * 4 + c] - (double)dq[c]);
        }
}

// FUNC_AT(0x001021a0)
void FnDeltaQFast::AddDeltaMask(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs,
                                void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::AddDeltaMask");
    const uint8_t *boneIdx = BoneIdx(data);
    const uint8_t *row = bin + (prevDelta + 2) * data[6] * 3;
    if (prevDelta >= floorDelta)
        return;
    for (int f = floorDelta - prevDelta; f != 0; f--)
        for (int i = 0; i < data[6]; i++, row += 3) {
            if (!InMask(mask, boneIdx[i]))
                continue;
            float dq[4];
            ((DeltaQFastDelta *)row)->UnQuantize(minRangesf + i * 8, dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = (float)((double)dq[c] + (double)qs[i * 4 + c]);
        }
}

// FUNC_AT(0x00102280)
void FnDeltaQFast::SubDeltaMask(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs,
                                void *mask) {
    EAGL_UNTESTED("FnDeltaQFast::SubDeltaMask");
    const uint8_t *boneIdx = BoneIdx(data);
    const uint8_t *row = bin + (prevDelta + 2) * data[6] * 3 - 3;
    if (prevDelta - 1 < floorDelta)
        return;
    for (int f = prevDelta - floorDelta; f != 0; f--)
        for (int i = data[6] - 1; i >= 0; i--, row -= 3) {
            if (!InMask(mask, boneIdx[i]))
                continue;
            float dq[4];
            ((DeltaQFastDelta *)row)->UnQuantize(minRangesf + i * 8, dq);
            for (int c = 0; c < 4; c++)
                qs[i * 4 + c] = (float)((double)qs[i * 4 + c] - (double)dq[c]);
        }
}

// The arrays, and the buffers (made again, without freeing the old ones, each time an anim is set).
// FUNC_AT(0x001032e0)
void FnDeltaQFast::InitBuffers() {
    EAGL_UNTESTED("FnDeltaQFast::InitBuffers");
    uint8_t *d = anim;
    uint8_t *minRanges;
    ((DeltaQFastHeader *)d)->GetArrays(&minRanges, &bins, &constBoneIdxs, &constPhysical);
    binSize = (((1 << d[0x10]) + 1) * d[6] * 3 + 1) & ~1;
    if (d[6] == 0)
        return;
    prevQs = prevQBlock = (float *)AnimPool_NewBlock((uint32_t)d[6] << 4);
    nextQs = nextQBlock = (float *)AnimPool_NewBlock((uint32_t)d[6] << 4);
    minRangesf = (float *)AnimPool_NewBlock((uint32_t)d[6] << 5);
    for (int i = 0; i < d[6]; i++)
        ((DeltaQFastMinRange *)(minRanges + i * 0x10))->UnQuantize(minRangesf + i * 8);
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
    uint8_t *d = self->anim;
    const uint8_t *boneIdx = BoneIdx(d);
    int n = d[6];
#define SKIP(i) (mask != NULL && !InMask(mask, boneIdx[i]))
    if (n != 0) {
        int frame = Truncate(time);
        int k = FindKey(frame, d, self->prevKey);
        int shift = d[0x10];
        int modMask = (int)(0x7fffffffu >> (31 - shift));
        int bin = k >> shift, offset = k & modMask;
        int prevBin = self->prevKey >> shift;
        if (self->nextKey == k) {
            float *t = self->prevQs;
            self->prevQs = self->nextQs;
            self->nextQs = t;
            self->nextKey = self->prevKey;
        } else {
            if (self->prevKey == k + 1) {
                for (int i = 0; i < d[6]; i++)
                    Copy4(self->nextQs + i * 4, self->prevQs + i * 4);
                self->nextKey = self->prevKey;
            }
            uint8_t *bp = self->bins + self->binSize * bin;
            bool restart = k < self->prevKey && ReverseDeltaSumEnabled == 0;
            int start;
            if (self->prevKey != -1 && bin == prevBin && (mask != NULL || offset != 0) && !restart) {
                start = self->prevKey & modMask;
            } else {
                for (int i = 0; i < d[6]; i++)
                    if (!SKIP(i))
                        ((DeltaQFastPhysical *)(bp + i * 6))->UnQuantize(self->prevQs + i * 4);
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
        if (LerpFactor(time, frame, d, k, &s) && k < Keys(d) - 1) {
            if (mask != NULL)
                self->UpdateNextQsMask(d, k + 1, bin, offset, mask);
            else
                self->UpdateNextQs(d, k + 1, bin, offset);
            for (int i = 0; i < d[6]; i++) {
                if (SKIP(i))
                    continue;
                float *out = SqtQuat(sqt, boneIdx[i]);
                const float *a = self->prevQs + i * 4, *b = self->nextQs + i * 4;
                for (int c = 0; c < 4; c++)
                    out[c] = (float)(((double)b[c] - (double)a[c]) * (double)s + (double)a[c]);
            }
        } else {
            for (int i = 0; i < d[6]; i++)
                if (!SKIP(i))
                    Copy4(SqtQuat(sqt, boneIdx[i]), self->prevQs + i * 4);
        }
    }
#undef SKIP
    for (int j = 0; j < d[7]; j++) {
        int bone = self->constBoneIdxs[j];
        if (mask == NULL || InMask(mask, bone))
            ((DeltaQFastPhysical *)(self->constPhysical + j * 6))->UnQuantize(SqtQuat(sqt, bone));
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
        EaglFree(this, 0x40);
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// FnDeltaQ. Data as FnDeltaQFast's with 0xc-byte range records (min xyz, range xyz) kept quantised, physical
// values of x (15 bits, w's sign in bit 0), y and z, and 3-byte deltas of x (7 bits, w's sign in bit 0), y and z.
// w is recovered from xyz after the deltas are summed. With a mask the cache is dropped after every call.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00103fa0)
void DeltaQMinRange::UnQuantize(float *out) {
    EAGL_UNTESTED("DeltaQMinRange::UnQuantize");
    const uint16_t *p = (const uint16_t *)this;
    double k = (double)QRangeScale + (double)QRangeScale;
    for (int c = 0; c < 3; c++)
        out[c] = (float)((double)(int)p[c] * k - (double)kOne);
    for (int c = 3; c < 6; c++)
        out[c] = (float)((double)(int)p[c] * k);
}

// FUNC_AT(0x001040a0)
void DeltaQPhysical::UnQuantize(float *q) {
    EAGL_UNTESTED("DeltaQPhysical::UnQuantize");
    const uint16_t *p = (const uint16_t *)this;
    double k = (double)QRangeScale + (double)QRangeScale;
    double kx = (double)QPhysScaleX + (double)QPhysScaleX;
    q[0] = (float)((double)(int)(uint16_t)(p[0] >> 1) * kx - (double)kOne);
    q[1] = (float)((double)(int)p[1] * k - (double)kOne);
    q[2] = (float)((double)(int)p[2] * k - (double)kOne);
    DeltaQRecoverW(*(const uint8_t *)this & 1, q);
}

// The next key's physical value, as DeltaQPhysical::UnQuantize but with its own order of the squares (x, z, y).
static void QPhysicalInline(const uint16_t *p, float *q) {
    double k = (double)QRangeScale + (double)QRangeScale;
    double kx = (double)QPhysScaleX + (double)QPhysScaleX;
    q[0] = (float)((double)(int)(uint16_t)(p[0] >> 1) * kx - (double)kOne);
    q[1] = (float)((double)(int)p[1] * k - (double)kOne);
    q[2] = (float)((double)(int)p[2] * k - (double)kOne);
    double ss = (double)q[0] * (double)q[0] + (double)q[2] * (double)q[2];
    ss = ss + (double)q[1] * (double)q[1];
    if (ss > (double)kOne) {
        double r = (double)kOne / sqrt(ss);
        q[3] = 0.0f;
        q[0] = (float)((double)q[0] * r);
        q[1] = (float)((double)q[1] * r);
        q[2] = (float)((double)q[2] * r);
    } else {
        q[3] = (float)sqrt((double)kOne - ss);
        if (*(const uint8_t *)p & 1)
            q[3] = -q[3];
    }
}

// A delta's x and y (unrounded) and z (rounded to float).
static void QDelta(const uint8_t *row, const float *range, double *dx, double *dy, float *dz) {
    *dx = (double)(int)(row[0] >> 1) * (double)range[3] * (double)QDeltaScaleX + (double)range[0];
    *dy = (double)(int)row[1] * (double)range[4] * (double)QDeltaScale + (double)range[1];
    *dz = (float)((double)(int)row[2] * (double)range[5] * (double)QDeltaScale + (double)range[2]);
}

// FUNC_AT(0x00104120)
uint8_t* DeltaQHeader::GetConstPhysical() {
    EAGL_UNTESTED("DeltaQHeader::GetConstPhysical");
    uint8_t *d = (uint8_t *)this;
    uintptr_t p = (uintptr_t)QFastConstBoneIdxs(d, 0xc);
    return (uint8_t *)((p + d[7] + 1) & ~(uintptr_t)1);
}

// FUNC_AT(0x00104180)
void DeltaQHeader::GetArrays(uint8_t **minRanges, uint8_t **bins, uint8_t **constBoneIdxs,
                             uint8_t **constPhysical) {
    EAGL_UNTESTED("DeltaQHeader::GetArrays");
    uint8_t *d = (uint8_t *)this;
    *minRanges = d + 0x12;
    *bins = d + 0x12 + d[6] * 0xc;
    *constBoneIdxs = QFastConstBoneIdxs(d, 0xc);
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
        FreeSized(prevQBlock);
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
    return DeltaGetLength(anim, length);
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
    uint8_t *d = anim;
    uint8_t *ranges;
    ((DeltaQHeader *)d)->GetArrays(&ranges, &bins, &constBoneIdxs, &constPhysical);
    binSize = (((1 << d[0x10]) + 1) * d[6] * 3 + 1) & ~1;
    if (d[6] == 0)
        return;
    prevQs = prevQBlock = (float *)AnimPool_NewBlock((uint32_t)d[6] << 4);
    minRanges = ranges;
}

// The original writes the masked and unmasked cases as one loop with a mask test, as here. Its constant bones
// test the mask with the animated bones' indexes (boneIdx[j], not constBoneIdxs[j]) - kept.
// FUNC_AT(0x001034c0)
bool FnDeltaQ::EvalSQTMasked(float time, void *mask, float *sqt) {
    EAGL_UNTESTED("FnDeltaQ::EvalSQTMasked");
    InitBuffersAsRequired();
    uint8_t *d = anim;
    const uint8_t *boneIdx = BoneIdx(d);
#define SKIP(i) (mask != NULL && !InMask(mask, boneIdx[i]))
    if (d[6] != 0) {
        int frame = Truncate(time);
        int k = FindKey(frame, d, prevKey);
        int shift = d[0x10];
        int modMask = (int)(0x7fffffffu >> (31 - shift));
        int bin = k >> shift, offset = k & modMask;
        int prevBin = prevKey >> shift;
        uint8_t *bp = bins + binSize * bin;
        bool restart = k < prevKey && ReverseDeltaSumEnabled == 0;
        int start;
        if (prevKey != -1 && bin == prevBin && offset != 0 && !restart) {
            start = prevKey & modMask;
        } else {
            for (int i = 0; i < d[6]; i++)
                if (!SKIP(i))
                    ((DeltaQPhysical *)(bp + i * 6))->UnQuantize(prevQs + i * 4);
            start = 0;
        }
        if (start != offset) {
            if (start < offset) {
                const uint8_t *row = bp + (start + 2) * d[6] * 3;
                for (int f = offset - start; f != 0; f--)
                    for (int i = 0; i < d[6]; i++, row += 3) {
                        if (SKIP(i))
                            continue;
                        float range[6];
                        double dx, dy;
                        float dz;
                        ((DeltaQMinRange *)(minRanges + i * 0xc))->UnQuantize(range);
                        QDelta(row, range, &dx, &dy, &dz);
                        float *v = prevQs + i * 4;
                        v[0] = (float)(dx + (double)v[0]);
                        v[1] = (float)(dy + (double)v[1]);
                        v[2] = (float)((double)dz + (double)v[2]);
                    }
            } else {
                const uint8_t *row = bp + (start + 2) * d[6] * 3 - 3;
                if (start - 1 >= offset)
                    for (int f = start - offset; f != 0; f--)
                        for (int i = d[6] - 1; i >= 0; i--, row -= 3) {
                            if (SKIP(i))
                                continue;
                            float range[6];
                            double dx, dy;
                            float dz;
                            ((DeltaQMinRange *)(minRanges + i * 0xc))->UnQuantize(range);
                            QDelta(row, range, &dx, &dy, &dz);
                            float *v = prevQs + i * 4;
                            v[0] = (float)((double)v[0] - dx);
                            v[1] = (float)((double)v[1] - dy);
                            v[2] = (float)((double)v[2] - (double)dz);
                        }
            }
            // w from the summed xyz, its sign from the key's row (inline DeltaQRecoverW in the original)
            const uint8_t *row = bp + (offset + 1) * d[6] * 3;
            for (int i = 0; i < d[6]; i++, row += 3)
                if (!SKIP(i))
                    DeltaQRecoverW(row[0] & 1, prevQs + i * 4);
        }
        prevKey = k;

        float s;
        if (LerpFactor(time, frame, d, k, &s) && k < Keys(d) - 1) {
            int nextBin = (k + 1) >> shift;
            uint8_t *np = bins + binSize * nextBin;
            if (nextBin == bin) {
                const uint8_t *row = np + (offset + 2) * d[6] * 3;
                for (int i = 0; i < d[6]; i++, row += 3) {
                    if (SKIP(i))
                        continue;
                    float range[6], next[4];
                    double dx, dy;
                    float dz;
                    ((DeltaQMinRange *)(minRanges + i * 0xc))->UnQuantize(range);
                    QDelta(row, range, &dx, &dy, &dz);
                    const float *v = prevQs + i * 4;
                    next[0] = (float)(dx + (double)v[0]);
                    next[1] = (float)(dy + (double)v[1]);
                    double z = (double)dz + (double)v[2];
                    next[2] = (float)z;
                    double ss = z * (double)next[2] + (double)next[1] * (double)next[1];
                    ss = ss + (double)next[0] * (double)next[0];
                    if (ss > (double)kOne) {
                        double r = (double)kOne / sqrt(ss);
                        next[3] = 0.0f;
                        next[0] = (float)((double)next[0] * r);
                        next[1] = (float)((double)next[1] * r);
                        next[2] = (float)(r * (double)next[2]);
                    } else {
                        next[3] = (float)sqrt((double)kOne - ss);
                        if (row[0] & 1)
                            next[3] = -next[3];
                    }
                    AnimQuatNLerp(s, v, next, SqtQuat(sqt, boneIdx[i]));
                }
            } else {
                for (int i = 0; i < d[6]; i++) {
                    if (SKIP(i))
                        continue;
                    float next[4];
                    QPhysicalInline((const uint16_t *)(np + i * 6), next);
                    AnimQuatNLerp(s, prevQs + i * 4, next, SqtQuat(sqt, boneIdx[i]));
                }
            }
        } else {
            for (int i = 0; i < d[6]; i++)
                if (!SKIP(i))
                    Copy4(SqtQuat(sqt, boneIdx[i]), prevQs + i * 4);
        }
    }
    for (int j = 0; j < d[7]; j++)
        if (!SKIP(j))
            ((DeltaQPhysical *)(constPhysical + j * 6))->UnQuantize(SqtQuat(sqt, constBoneIdxs[j]));
#undef SKIP
    if (mask != NULL)
        prevKey = -1;
    return true;
}

// FUNC_AT(0x00104200)
FnDeltaQ* FnDeltaQ::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnDeltaQ::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, 0x30);
    return this;
}
