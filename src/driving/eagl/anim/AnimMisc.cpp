#include "AnimMisc.h"
#include "AnimChannels.h"
#include "AnimDecode.h"
#include "AnimUntested.h"

#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// The small channel types no shipped data builds (docs/driving/eagl.md 3.4): DeltaLerp (10), KeyLerp (12), phase
// (14), raw state (16), and the raw pose data type 0's channel evaluates. Faithful ports from the listing, each
// with a one-time "untested" warning: nothing on the disc reaches them. Each function is the original at the same
// address; x87 in double in the original's order with a float store per store, comparisons as the original's FCOMP
// flag tests decide them (unordered included), truncation by CVTTSS2SI.
// ---------------------------------------------------------------------------------------------------------------

#define Zero         (*(const float *)0x00189decu)   // 0.0
#define One          (*(const float *)0x00189de8u)   // 1.0
#define PhaseScale   (*(const float *)0x001a120cu)   // 360/255
#define Phase180     (*(const float *)0x00189f08u)   // 180.0
#define PiF          (*(const float *)0x001a1228u)   // pi
#define OneOver180   (*(const float *)0x0018a138u)   // 1/180
#define ScratchQuat  ((float *)0x00241a58u)           // the lerps' first quaternion
#define ScratchTrans ((float *)0x00241a68u)           // the translation lerp's first vector

static inline int Truncate(float f) {
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

static inline void CopyBits(void *to, const void *from, size_t n) {
    memcpy(to, from, n);
}

// FUNC_AT(0x000fda60)
int AnimTruncateRawState(float value) {
    EAGL_UNTESTED("AnimTruncateRawState");
    return Truncate(value);
}

// FUNC_AT(0x000fe270)
int AnimTruncateRawPose(float value) {
    EAGL_UNTESTED("AnimTruncateRawPose");
    return Truncate(value);
}

// ---- DeltaLerp: the DeltaQuat layout (u16 type, u16 checksum, DeltaCompressedData* at +4, u16 frames at +8,
// u16 index[] at +0xa), each value written on its own (DeltaQuat's in groups of 4)

#define DeltaInfo(anim) (*(DeltaCompressedData **)((anim) + 4))
#define InfoCount(info) (*(uint16_t *)(info))

// The frame below the time decoded; between frames, one more frame's deltas scaled by the fraction.
static void DeltaLerpEval(FnDeltaChan *c, float time, float *out) {
    int t0 = Truncate(time);
    uint8_t *a = c->anim;
    DeltaCompressedData *info = DeltaInfo(a);
    int frames = *(uint16_t *)(a + 8);
    int f = t0;
    if (t0 >= frames)
        f = frames - 1;
    else if (t0 < 0)
        f = 0;
    info->DecompressValues(0, InfoCount(info), c->frame, f, c->values, c->values);
    c->frame = f;
    float tf = (float)t0;
    a = c->anim;
    info = DeltaInfo(a);
    int count = InfoCount(info);
    uint16_t *index = (uint16_t *)(a + 0xa);
    if (!((double)time == (double)tf) && t0 + 1 < (int)*(uint16_t *)(a + 8)) {   // test ah,0x44 / jnp
        float s = (float)((double)time - (double)tf);
        info->DecompressValuesIndexed(0, count, t0, t0 + 1, c->values, out, 1, index, s);
        return;
    }
    for (int i = 0; i < count; i++)
        CopyBits(out + index[i], c->values + i, 4);
}

// FUNC_AT(0x000fb440)
void FnDeltaChan::EvalLerp(float previous, float time, float *out) {
    EAGL_UNTESTED("FnDeltaLerpChan::Eval");
    (void)previous;
    DeltaLerpEval(this, time, out);
}

// FUNC_AT(0x000fb530)
bool FnDeltaChan::EvalSQTLerp(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnDeltaLerpChan::EvalSQT");
    (void)mask;
    DeltaLerpEval(this, time, sqt);
    return true;
}

// FUNC_AT(0x000fb630)
bool FnDeltaChan::EvalWeightsLerp(float time, float *weights) {
    EAGL_UNTESTED("FnDeltaLerpChan::EvalWeights");
    AnimVCall<void>(this, kSlotEval, time, time, weights);
    return true;
}

// FUNC_AT(0x000fb650)
bool FnDeltaChan::EvalVel2DLerp(float time, float *velocity) {
    EAGL_UNTESTED("FnDeltaLerpChan::EvalVel2D");
    AnimVCall<void>(this, kSlotEval, time, time, velocity);
    return true;
}

// ---- KeyLerp: the KeyQuat layout (DeltaCompressedData* at +4, u16 key times* at +8, u16 keys at +0xc, u16
// index[] at +0xe), each value on its own and linear between keys

#define KeyTimes(anim) (*(uint16_t **)((anim) + 8))
#define KeyCount(anim) (*(uint16_t *)((anim) + 0xc))

// FUNC_AT(0x000fba00)
void FnKeyDeltaChan::EvalLerp(float previous, float time, float *out) {
    EAGL_UNTESTED("FnKeyLerpChan::Eval");
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQT, time, out, (void *)NULL);
}

// The key below the time decoded; on a key, after the last key or before 0 a copy, otherwise the next key's
// deltas scaled by the fraction of the span.
// FUNC_AT(0x000fba20)
bool FnKeyDeltaChan::EvalSQTLerp(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnKeyLerpChan::EvalSQT");
    (void)mask;
    int k = FindLowerKey(time);
    DeltaCompressedData *info = DeltaInfo(anim);
    info->DecompressValues(0, InfoCount(info), key, k, values, values);
    uint8_t *a = anim;
    key = k;
    int keys = KeyCount(a);
    uint16_t *times = KeyTimes(a);
    info = DeltaInfo(a);
    int count = InfoCount(info);
    uint16_t *index = (uint16_t *)(a + 0xe);
    int previousTime = k != 0 ? times[k - 1] : 0;
    float pf = (float)previousTime;
    bool copy = (double)previousTime == (double)time;                       // test ah,0x44 / jnp
    if (!copy && k == keys - 1 && (double)(int)times[keys - 2] < (double)time)   // test ah,5 / jnp
        copy = true;
    if (!copy && k == 0 && (double)time < (double)Zero)                     // test ah,5 / jnp
        copy = true;
    if (copy) {
        for (int i = 0; i < count; i++)
            CopyBits(sqt + index[i], values + i, 4);
        return true;
    }
    int span = (int)times[k] - previousTime;
    float s = (float)(((double)time - (double)pf) / (double)span);
    info->DecompressValuesIndexed(0, count, k, k + 1, values, sqt, 1, index, s);
    return true;
}

// ---- the phase channel: one angle, sampled every 'step' frames (the phase data, AnimMisc.h); looping data wraps the time
// into [0, samples - 1], and past the last sample the angle extrapolates from the last two.

// FUNC_AT(0x000fd4a0)
bool FnPhaseChan::GetLength(float *length) {
    EAGL_UNTESTED("FnPhaseChan::GetLength");
    *length = (float)(int)*(uint16_t *)(anim + 4);
    return true;
}

// FUNC_AT(0x000fd4c0)
void FnPhaseChan::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnPhaseChan::Eval");
    (void)previous;
    uint8_t *a = anim;
    int last = (int)*(uint16_t *)(a + 4) - 1;
    if (a[8] & 2) {
        if ((double)time < (double)Zero) {   // test ah,5 / jp not taken: ordered less
            float lf = (float)last;
            float q = (float)((double)time / (double)lf);
            int k = Truncate(q) * last;
            time = (float)((double)lf - ((double)time - (double)k));
        } else if ((double)time > (double)last) {   // test ah,0x41 / jne not taken: ordered greater
            double d = (double)time - (double)last;
            float df = (float)d;
            float q = (float)(d / (double)last);    // the unrounded difference
            int k = Truncate(q) * last;
            time = (float)((double)df - (double)k);
        }
    }
    int i = Truncate(time);
    int div = step;
    int seg = i / div;
    double frac = ((double)time - (double)(div * seg)) / (double)div;
    int h = a[9] < 2 ? 2 : a[9];
    double v0 = (double)(int)a[0xa + h + seg] * (double)PhaseScale - (double)Phase180;
    out[0] = (float)v0;
    if (seg < last / (int)step + 1) {
        double v1 = (double)(int)a[0xb + h + seg] * (double)PhaseScale - (double)Phase180;
        out[0] = (float)(v1 * frac + ((double)One - frac) * v0);
    } else {
        double before = (double)(int)a[9 + h + seg] * (double)PhaseScale - (double)Phase180;
        out[0] = (float)((frac + (double)One) * v0 - before * frac);
    }
}

// FUNC_AT(0x000fd670)
void FnPhaseChan::SetAnimMemoryMap(uint8_t *data) {
    EAGL_UNTESTED("FnPhaseChan::SetAnimMemoryMap");
    anim = data;
    index = 0;
    count = *(uint16_t *)(data + 6);
    notFlag1 = (uint8_t)(~data[8] & 1);
    uint8_t flags = data[8];
    if (flags & 8)
        step = 1;
    else if (flags & 0x10)
        step = 2;
    else if (flags & 0x20)
        step = 4;
    else
        step = (flags & 0x40) ? 8 : 1;
}

// ---- the raw state channel: u16 type, u16 checksum, u16, u16 frames at +6, u8 fields at +8, u8 frame size at +9,
// u16 field descriptors at +0xa, then (4-aligned) the frames: float time, then the fields packed. A descriptor:
// bits 0-7 the output byte offset, 11-12 the size - 1 (1, 2 or 4 bytes stored; 3 stores nothing), 13-15 the
// encoding (0/1/2: 1/2/4 bits from a bit stream, MSB first; 3/4/5: a whole byte/u16/u32; others keep the last value).

static inline uint8_t *StateFrames(uint8_t *a) {
    int n = a[8];
    return (n & 1) ? a + n * 2 + 0xa : a + n * 2 + 0xc;
}

// FUNC_AT(0x000fd730)
void FnRawStateChan::Decode(uint8_t *data, uint8_t *out) {
    EAGL_UNTESTED("FnRawStateChan::Decode");
    uint8_t *a = anim;
    uint32_t value = 0;
    uint8_t bit = 0;
    const uint16_t *desc = (const uint16_t *)(a + 0xa);
    for (int i = 0; i < (int)a[8]; i++) {
        uint16_t d = desc[i];
        uint8_t size = (uint8_t)(((d >> 11) & 3) + 1);
        switch (d >> 13) {
        case 0:
            bit = (uint8_t)(bit + 1);
            value = ((uint32_t)data[0] >> ((8 - bit) & 31)) & 1;   // SHR by CL: the count masked to 5 bits
            break;
        case 1:
            bit = (uint8_t)(bit + 2);
            value = ((uint32_t)data[0] >> ((8 - bit) & 31)) & 3;
            break;
        case 2:
            bit = (uint8_t)(bit + 4);
            value = ((uint32_t)data[0] >> ((8 - bit) & 31)) & 0xf;
            break;
        case 3:
            value = data[0];
            data += 1;
            break;
        case 4: {
            uint16_t v;
            memcpy(&v, data, 2);
            value = v;
            data += 2;
            break;
        }
        case 5:
            memcpy(&value, data, 4);
            data += 4;
            break;
        default:
            break;
        }
        if (bit >= 8) {
            data++;
            bit = 0;
        }
        uint8_t *to = out + (d & 0xff);
        if (size == 1) {
            *to = (uint8_t)value;
        } else if (size == 2) {
            uint16_t v = (uint16_t)value;
            memcpy(to, &v, 2);
        } else if (size == 4) {
            memcpy(to, &value, 4);
        }
    }
}

// The frame whose time is the last at or before the time, searched from the last one decoded. Going back, the
// original compares the time against the frame POINTER's bits read as a float (it stores the address and FCOMPs
// that dword - a bug in the original, kept: addresses are tiny positive floats, so any time >= 0 stops at once).
// FUNC_AT(0x000fd840)
bool FnRawStateChan::EvalState(float time, void *state) {
    EAGL_UNTESTED("FnRawStateChan::EvalState");
    uint8_t *a = anim;
    uint8_t *frames = StateFrames(a);
    int stride = a[9];
    int f = frame;
    if ((double)time >= (double)*(float *)(frames + f * stride)) {   // test ah,1 / jne not taken
        int count = *(uint16_t *)(a + 6);
        if (f < count) {
            do {
                uint8_t *p = frames + stride * f;
                if ((double)time < (double)*(float *)(p + stride)) {   // test ah,5 / jnp: ordered less
                    Decode(p + 4, (uint8_t *)state);
                    frame = f;
                    return true;
                }
                f++;
            } while (f < (int)*(uint16_t *)(a + 6));
        }
        Decode(frames + ((int)*(uint16_t *)(a + 6) - 1) * stride + 4, (uint8_t *)state);
        frame = (int)*(uint16_t *)(a + 6) - 1;
        return true;
    }
    for (f = f - 1; f >= 0; f--) {
        uint8_t *p = frames + stride * f;
        uint32_t bits = (uint32_t)(uintptr_t)p;
        float pointerAsFloat;
        memcpy(&pointerAsFloat, &bits, 4);
        if ((double)time >= (double)pointerAsFloat) {   // test ah,1 / je
            Decode(p + 4, (uint8_t *)state);
            frame = f;
            return true;
        }
    }
    Decode(frames + 4, (uint8_t *)state);
    frame = 0;
    return true;
}

// The first frame after 'from' whose decoded state the test accepts (the test's vtable slot 0, thiscall on the
// state): its time.
// FUNC_AT(0x000fd9c0)
bool FnRawStateChan::FindTime(void *test, float from, float *time) {
    EAGL_UNTESTED("FnRawStateChan::FindTime");
    uint8_t *a = anim;
    for (int f = 0; f < (int)*(uint16_t *)(a + 6); f++) {
        uint8_t *p = StateFrames(a) + (int)a[9] * f;
        float t = *(float *)p;
        if (!((double)t < (double)from || (double)t == (double)from)) {   // test ah,0x41 / jnp: greater or unordered
            uint32_t state[0x50 / 4];
            Decode(p + 4, (uint8_t *)state);
            if (((bool (__fastcall *)(void *, int, void *))(*(void ***)test)[0])(test, 0, state)) {
                *time = t;
                return true;
            }
        }
    }
    return false;
}

// ---- the raw pose data (RawPoseChannel in AnimMisc.h)

#define FnCopyQuat   0x000fdf20u
#define FnEulF3      0x000fdec0u
#define FnCopyTrans  0x000fdf60u
#define FnLerpQuat   0x000fdfd0u
#define FnLerpEuler  0x000fdf90u
#define FnLerpTrans  0x000fe060u

typedef void (*FrameFn)(float **cursor, float *out);
typedef void (*LerpFn)(float t, float **cursor0, float **cursor1, float *out);

static inline uint32_t *Signatures(RawPoseChannel *p) {
    return (uint32_t *)((uint8_t *)p + 0x10);
}

static inline bool InMask(const uint32_t *mask, int bone) {
    return (mask[bone >> 5] & (1u << (bone & 31))) != 0;
}

// The signature tables' type numbers become the original addresses of the channel functions.
// FUNC_AT(0x000fda70)
void RawPoseChannel_InitAnimMemoryMap(uint8_t *data) {
    EAGL_UNTESTED("RawPoseChannel::InitAnimMemoryMap");
    RawPoseChannel *p = (RawPoseChannel *)data;
    int count = p->count;
    uint32_t *e = Signatures(p);
    for (int i = 0; i < count;) {
        int n = (int)*e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            switch (*e) {
            case 0: *e = FnCopyQuat; break;
            case 1: *e = FnEulF3; break;
            case 2: *e = FnCopyTrans; break;
            default: printf("Bad signature channel type\n"); break;
            }
        }
    }
    e = Signatures(p) + p->count;
    for (int i = 0; i < count;) {
        int n = (int)*e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            switch (*e) {
            case 0: *e = FnLerpQuat; break;
            case 1: *e = FnLerpEuler; break;
            case 2: *e = FnLerpTrans; break;
            default: printf("Bad signature channel type\n"); break;
            }
        }
    }
}

// InitAnimMemoryMap undone: the addresses back to type numbers.
// FUNC_AT(0x000fdb40)
void RawPoseChannel::RestoreSignatures() {
    EAGL_UNTESTED("RawPoseChannel::RestoreSignatures");
    int n0 = count;
    uint32_t *e = Signatures(this);
    for (int i = 0; i < n0;) {
        int n = (int)*e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            if (*e == FnEulF3)
                *e = 1;
            else if (*e == FnCopyQuat)
                *e = 0;
            else if (*e == FnCopyTrans)
                *e = 2;
            else
                printf("Bad signature channel type\n");
        }
    }
    e = Signatures(this) + count;
    for (int i = 0; i < n0;) {
        int n = (int)*e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            if (*e == FnLerpEuler)
                *e = 1;
            else if (*e == FnLerpQuat)
                *e = 0;
            else if (*e == FnLerpTrans)
                *e = 2;
            else
                printf("Bad signature channel type\n");
        }
    }
}

// One frame through the first table: each bone's functions read its values in turn. Bones outside the mask are
// stepped over (4 floats a quaternion, 3 an Euler or a translation).
// FUNC_AT(0x000fdc20)
void RawPoseChannel::EvalFrame(int frame, float *out, void *mask) {
    EAGL_UNTESTED("RawPoseChannel::EvalFrame");
    uint32_t *e = Signatures(this);
    uint32_t *end = e + count;
    float *cursor = (float *)((uint8_t *)this + 4 * (frameSize * frame + count * 2 + 4));
    float *bone = out + 4;
    if (mask == NULL) {
        for (; e < end; bone += 12) {
            int n = (int)*e++;
            for (; n > 0; n--)
                ((FrameFn)(uintptr_t)*e++)(&cursor, bone);
        }
        return;
    }
    for (int b = 0; e < end; b++, bone += 12) {
        int n = (int)*e++;
        if (InMask((const uint32_t *)mask, b)) {
            for (; n > 0; n--)
                ((FrameFn)(uintptr_t)*e++)(&cursor, bone);
        } else {
            for (; n > 0; n--) {
                uint32_t fn = *e++;
                if (fn == FnEulF3 || fn == FnCopyTrans)
                    cursor += 3;
                else if (fn == FnCopyQuat)
                    cursor += 4;
            }
        }
    }
}

// The frame below the time; between frames (when interpolating) the two through the second table.
// FUNC_AT(0x000fdd40)
void RawPoseChannel::Eval(float time, float *out, bool interpolate, void *mask) {
    EAGL_UNTESTED("RawPoseChannel::Eval");
    int i = Truncate(time);
    if (i < 0) {
        EvalFrame(0, out, mask);
        return;
    }
    int last = frames - 1;
    if (i >= last) {
        EvalFrame(last, out, mask);
        return;
    }
    double f = (double)time - (double)i;
    float frac = (float)f;
    if (f == (double)Zero || !interpolate) {   // test ah,0x44 / jnp: the unrounded difference, ordered equal
        EvalFrame(i, out, mask);
        return;
    }
    Lerp(frac, i, i + 1, out, mask);
}

// Two frames through the second table; masked-out bones stepped over in both.
// FUNC_AT(0x000fe110)
void RawPoseChannel::Lerp(float t, int frame0, int frame1, float *out, void *mask) {
    EAGL_UNTESTED("RawPoseChannel::Lerp");
    int base = count * 2 + 4;
    float *cursor1 = (float *)((uint8_t *)this + 4 * (frameSize * frame1 + base));
    float *cursor0 = (float *)((uint8_t *)this + 4 * (frameSize * frame0 + base));
    uint32_t *e = Signatures(this) + count;
    uint32_t *end = e + count;
    float *bone = out + 4;
    if (mask == NULL) {
        for (; e < end; bone += 12) {
            int n = (int)*e++;
            for (; n > 0; n--)
                ((LerpFn)(uintptr_t)*e++)(t, &cursor0, &cursor1, bone);
        }
        return;
    }
    for (int b = 0; e < end; b++, bone += 12) {
        int n = (int)*e++;
        if (InMask((const uint32_t *)mask, b)) {
            for (; n > 0; n--)
                ((LerpFn)(uintptr_t)*e++)(t, &cursor0, &cursor1, bone);
        } else {
            for (; n > 0; n--) {
                uint32_t fn = *e++;
                if (fn == FnLerpEuler || fn == FnLerpTrans) {
                    cursor0 += 3;
                    cursor1 += 3;
                } else if (fn == FnLerpQuat) {
                    cursor0 += 4;
                    cursor1 += 4;
                }
            }
        }
    }
}

// Euler angles (radians) to a quaternion from the half angles' sines and cosines. The original keeps two FSIN
// results (of the second and third half angles) on the x87 stack and multiplies them unrounded, at the 64-bit
// mantissa FSIN returns whatever the precision control (docs/driving/maths.md 3.1-3.2); C++ double cannot hold
// that, so, as Transform.cpp's EAGL_BuildRotate, the arithmetic is the original's instructions in assembly (the
// 0.5 from 0x00189eb0 as a constant). The wrapper only adds the untested warning. Called by EulF3 and, by
// address, by 0x00101c90.
static const float kHalf = 0.5f;   // 0x3f000000

__declspec(naked) static void __cdecl EulerToQuatX87(const float *angles, float *quat) {
    __asm {
        sub esp, 0x10
        mov eax, dword ptr [esp + 0x14]
        fld dword ptr [eax]
        fmul dword ptr [kHalf]
        fld dword ptr [eax + 4]
        fmul dword ptr [kHalf]
        fld dword ptr [eax + 8]
        mov eax, dword ptr [esp + 0x18]
        fmul dword ptr [kHalf]
        fstp dword ptr [esp]
        fld st(1)
        fcos
        fstp dword ptr [esp + 4]
        fld st(0)
        fcos
        fstp dword ptr [esp + 0x14]
        fld dword ptr [esp]
        fcos
        fstp dword ptr [esp + 8]
        fxch st(1)
        fsin
        fstp dword ptr [esp + 0xc]
        fsin
        fld dword ptr [esp]
        fsin
        fld dword ptr [esp + 8]
        fmul dword ptr [esp + 4]
        fstp dword ptr [esp]
        fld dword ptr [esp + 4]
        fmul st, st(1)
        fld dword ptr [esp + 0xc]
        fmul dword ptr [esp + 8]
        fxch st(2)
        fmul dword ptr [esp + 0xc]
        fstp dword ptr [esp + 0xc]
        fld dword ptr [esp + 0x14]
        fmul st, st(2)
        fld st(1)
        fmul st, st(4)
        fsubp st(1), st
        fstp dword ptr [eax]
        fld dword ptr [esp]
        fmul st, st(3)
        fld dword ptr [esp + 0xc]
        fmul dword ptr [esp + 0x14]
        faddp st(1), st
        fstp dword ptr [eax + 4]
        fmul dword ptr [esp + 0x14]
        fxch st(1)
        fmul st, st(2)
        fsubp st(1), st
        fstp dword ptr [eax + 8]
        fld dword ptr [esp + 0xc]
        fmul st, st(1)
        fld dword ptr [esp]
        fmul dword ptr [esp + 0x14]
        faddp st(1), st
        fstp dword ptr [eax + 0xc]
        fstp st(0)
        add esp, 0x10
        ret
    }
}

// FUNC_AT(0x000fde00)
void EAGLAnim_EulerToQuat(const float *angles, float *quat) {
    EAGL_UNTESTED("EAGLAnim_EulerToQuat");
    EulerToQuatX87(angles, quat);
}

// Three angles in degrees from the cursor (times pi * 1/180, the product unrounded) to a quaternion.
// FUNC_AT(0x000fdec0)
void EAGLAnim_EulF3(float **cursor, float *out) {
    EAGL_UNTESTED("EAGLAnim::EulF3");
    float *c = *cursor;
    double s = (double)PiF * (double)OneOver180;
    float angles[3];
    angles[0] = (float)(s * (double)c[0]);
    angles[1] = (float)(s * (double)c[1]);
    angles[2] = (float)(s * (double)c[2]);
    *cursor = c + 3;
    EAGLAnim_EulerToQuat(angles, out);
}

// FUNC_AT(0x000fdf20)
void RawPose_CopyQuat(float **cursor, float *out) {
    EAGL_UNTESTED("RawPose_CopyQuat");
    CopyBits(out, *cursor, 16);
    *cursor += 4;
}

// FUNC_AT(0x000fdf60)
void RawPose_CopyTrans(float **cursor, float *out) {
    EAGL_UNTESTED("RawPose_CopyTrans");
    CopyBits(out + 4, *cursor, 12);
    *cursor += 3;
}

// FUNC_AT(0x000fdf90)
void RawPose_LerpEuler(float t, float **cursor0, float **cursor1, float *out) {
    EAGL_UNTESTED("RawPose_LerpEuler");
    EAGLAnim_EulF3(cursor0, ScratchQuat);
    EAGLAnim_EulF3(cursor1, out);
    EAGL_VU0_fastqslerp(t, ScratchQuat, out, out);
}

// FUNC_AT(0x000fdfd0)
void RawPose_LerpQuat(float t, float **cursor0, float **cursor1, float *out) {
    EAGL_UNTESTED("RawPose_LerpQuat");
    CopyBits(ScratchQuat, *cursor0, 16);
    *cursor0 += 4;
    CopyBits(out, *cursor1, 16);
    *cursor1 += 4;
    EAGL_VU0_fastqslerp(t, ScratchQuat, out, out);   // a tail jump in the original
}

// FUNC_AT(0x000fe060)
void RawPose_LerpTrans(float t, float **cursor0, float **cursor1, float *out) {
    EAGL_UNTESTED("RawPose_LerpTrans");
    float *g = ScratchTrans;
    CopyBits(g, *cursor0, 12);
    *cursor0 += 3;
    CopyBits(out + 4, *cursor1, 12);
    *cursor1 += 3;
    for (int j = 0; j < 3; j++)
        out[4 + j] = (float)(((double)out[4 + j] - (double)g[j]) * (double)t + (double)g[j]);
}
