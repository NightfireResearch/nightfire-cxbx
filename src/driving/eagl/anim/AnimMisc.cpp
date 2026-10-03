#include "AnimMisc.h"
#include "AnimChannels.h"
#include "AnimDecode.h"
#include "AnimUntested.h"
#include "Skeleton.h"

#include <bit>
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
// address; x87 in double in the original's order with a float store per store (a single operation on floats written
// in float: the same bits), comparisons as the original's FCOMP flag tests decide them (unordered included),
// truncation by CVTTSS2SI.
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define ScratchQuat ((float *)0x00241a58)            // the lerps' first quaternion: ScratchQuat[4]
#define ScratchTrans ((float *)0x00241a68)           // the translation lerp's first vector: ScratchTrans[3]

// The original's .rdata constants (0x001a1228, 0x0018a138; 180, 1 and 0 are exact; kPhaseScale: AnimMisc.h)
constexpr float kPi = 3.14159274f;
constexpr float kOneOver180 = 1.0f / 180;
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "the original's pi");
static_assert(std::bit_cast<uint32_t>(kOneOver180) == 0x3bb60b61, "the original's 1/180");

inline int Truncate(float f) {   // CVTTSS2SI
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

inline bool InMask(const BoneMask *mask, int bone) {
    return (mask->bits[bone >> 5] & (1u << (bone & 31))) != 0;
}

DeltaChanData *DeltaData(uint8_t *anim) {
    return reinterpret_cast<DeltaChanData *>(anim);
}

KeyChanData *KeyData(uint8_t *anim) {
    return reinterpret_cast<KeyChanData *>(anim);
}

PhaseChanData *PhaseData(uint8_t *anim) {
    return reinterpret_cast<PhaseChanData *>(anim);
}

}  // namespace

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

// ---- DeltaLerp: the DeltaQuat layout (DeltaChanData), each value written on its own (DeltaQuat's in groups of 4)

// The frame below the time decoded; between frames, one more frame's deltas scaled by the fraction.
static void DeltaLerpEval(FnDeltaChan *c, float time, float *out) {
    int t0 = Truncate(time);
    DeltaChanData *d = DeltaData(c->anim);
    DeltaCompressedData *info = d->info;
    int frames = d->frames;
    int f = t0;
    if (t0 >= frames)
        f = frames - 1;
    else if (t0 < 0)
        f = 0;
    info->DecompressValues(0, info->count, c->frame, f, c->values, c->values);
    c->frame = f;
    float tf = float(t0);
    d = DeltaData(c->anim);   // read again after the call, as the original does
    info = d->info;
    int count = info->count;
    if (!(time == tf) && t0 + 1 < d->frames) {   // test ah,0x44 / jnp: unordered counts as between
        float s = time - tf;
        info->DecompressValuesIndexed(0, count, t0, t0 + 1, c->values, out, 1, d->index, s);
        return;
    }
    for (int i = 0; i < count; i++)
        out[d->index[i]] = c->values[i];
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

// ---- KeyLerp: the KeyQuat layout (KeyChanData), each value on its own and linear between keys

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
    DeltaCompressedData *info = KeyData(anim)->info;
    info->DecompressValues(0, info->count, key, k, values, values);
    KeyChanData *d = KeyData(anim);   // read again after the call
    key = k;
    int keys = d->keys;
    uint16_t *times = d->times;
    info = d->info;
    int count = info->count;
    int previousTime = k != 0 ? times[k - 1] : 0;
    float pf = float(previousTime);
    bool copy = double(previousTime) == time;                        // test ah,0x44 / jnp
    if (!copy && k == keys - 1 && double(times[keys - 2]) < time)    // test ah,5 / jnp
        copy = true;
    if (!copy && k == 0 && time < 0.0f)                              // test ah,5 / jnp
        copy = true;
    if (copy) {
        for (int i = 0; i < count; i++)
            sqt[d->index[i]] = values[i];
        return true;
    }
    int span = times[k] - previousTime;
    float s = float((double(time) - pf) / span);
    info->DecompressValuesIndexed(0, count, k, k + 1, values, sqt, 1, d->index, s);
    return true;
}

// ---- the phase channel: one angle, sampled every 'step' frames (PhaseChanData); looping data wraps the time into
// [0, samples - 1], and past the last sample the angle extrapolates from the last two.

// FUNC_AT(0x000fd4a0)
bool FnPhaseChan::GetLength(float *length) {
    EAGL_UNTESTED("FnPhaseChan::GetLength");
    *length = PhaseData(anim)->numFrames;
    return true;
}

// FUNC_AT(0x000fd4c0)
void FnPhaseChan::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnPhaseChan::Eval");
    (void)previous;
    PhaseChanData *d = PhaseData(anim);
    int last = d->numFrames - 1;
    if (d->flags & kPhaseLooping) {
        if (time < 0.0f) {   // test ah,5 / jp not taken: ordered less
            float lf = float(last);
            float q = time / lf;
            int k = Truncate(q) * last;
            time = float(lf - (double(time) - k));
        } else if (double(time) > last) {   // test ah,0x41 / jne not taken: ordered greater
            double difference = double(time) - last;
            float df = float(difference);
            float q = float(difference / last);    // the unrounded difference
            int k = Truncate(q) * last;
            time = float(double(df) - k);
        }
    }
    int i = Truncate(time);
    int div = step;
    int seg = i / div;
    double frac = (double(time) - div * seg) / div;
    const uint8_t *samples = d->Samples();
    double v0 = samples[seg] * double(kPhaseScale) - 180.0;
    out[0] = float(v0);
    if (seg < last / step + 1) {
        double v1 = samples[seg + 1] * double(kPhaseScale) - 180.0;
        out[0] = float(v1 * frac + (1.0 - frac) * v0);
    } else {
        double before = samples[seg - 1] * double(kPhaseScale) - 180.0;
        out[0] = float((frac + 1.0) * v0 - before * frac);
    }
}

// FUNC_AT(0x000fd670)
void FnPhaseChan::SetAnimMemoryMap(uint8_t *data) {
    EAGL_UNTESTED("FnPhaseChan::SetAnimMemoryMap");
    PhaseChanData *d = PhaseData(data);
    anim = data;
    index = 0;
    count = d->startTime;
    notFlag1 = uint8_t(~d->flags & kPhaseFlag01);
    step = uint8_t(d->SampleStep());   // the flags read again after that store, as the original
}

// ---- the raw state channel: u16 type, u16 checksum, u16, u16 frames at +6, u8 fields at +8, u8 frame size at +9,
// u16 field descriptors at +0xa, then (4-aligned) the frames: float time, then the fields packed. A descriptor:
// bits 0-7 the output byte offset, 11-12 the size - 1 (1, 2 or 4 bytes stored; 3 stores nothing), 13-15 the
// encoding (RawStateEncoding; others keep the last value).

namespace {

enum RawStateEncoding {
    kEncode1Bit = 0,                 // 1, 2 or 4 bits from a bit stream, MSB first
    kEncode2Bits = 1,
    kEncode4Bits = 2,
    kEncodeU8 = 3,                   // a whole byte, u16 or u32
    kEncodeU16 = 4,
    kEncodeU32 = 5,
};

static_assert(offsetof(RawStateChanData, descriptors) == 0xa, "the raw state descriptors are at +0xa");

RawStateChanData *StateData(uint8_t *anim) {
    return reinterpret_cast<RawStateChanData *>(anim);
}

// A frame's time, its first dword (the packed fields follow at +4).
float FrameTime(const uint8_t *frame) {
    return *reinterpret_cast<const float *>(frame);
}

}  // namespace

// FUNC_AT(0x000fd730)
void FnRawStateChan::Decode(uint8_t *data, uint8_t *out) {
    EAGL_UNTESTED("FnRawStateChan::Decode");
    RawStateChanData *d = StateData(anim);
    uint32_t value = 0;
    uint8_t bit = 0;
    for (int i = 0; i < d->fields; i++) {
        uint16_t descriptor = d->descriptors[i];
        uint8_t size = uint8_t(((descriptor >> 11) & 3) + 1);
        switch (descriptor >> 13) {
        case kEncode1Bit:
            bit = uint8_t(bit + 1);
            value = (data[0] >> ((8 - bit) & 31)) & 1;   // SHR by CL: the count masked to 5 bits
            break;
        case kEncode2Bits:
            bit = uint8_t(bit + 2);
            value = (data[0] >> ((8 - bit) & 31)) & 3;
            break;
        case kEncode4Bits:
            bit = uint8_t(bit + 4);
            value = (data[0] >> ((8 - bit) & 31)) & 0xf;
            break;
        case kEncodeU8:
            value = data[0];
            data += 1;
            break;
        case kEncodeU16: {
            uint16_t v;
            memcpy(&v, data, 2);
            value = v;
            data += 2;
            break;
        }
        case kEncodeU32:
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
        uint8_t *to = out + (descriptor & 0xff);
        if (size == 1) {
            *to = uint8_t(value);
        } else if (size == 2) {
            uint16_t v = uint16_t(value);
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
    RawStateChanData *d = StateData(anim);
    uint8_t *out = static_cast<uint8_t *>(state);
    uint8_t *frames = d->Frames();
    int stride = d->frameSize;
    int f = frame;
    if (time >= FrameTime(frames + f * stride)) {   // test ah,1 / jne not taken
        int count = d->frames;
        if (f < count) {
            do {
                uint8_t *p = frames + stride * f;
                if (time < FrameTime(p + stride)) {   // test ah,5 / jnp: ordered less
                    Decode(p + 4, out);
                    frame = f;
                    return true;
                }
                f++;
            } while (f < d->frames);
        }
        Decode(frames + (d->frames - 1) * stride + 4, out);
        frame = d->frames - 1;
        return true;
    }
    for (f = f - 1; f >= 0; f--) {
        uint8_t *p = frames + stride * f;
        float pointerAsFloat = std::bit_cast<float>(uint32_t(reinterpret_cast<uintptr_t>(p)));
        if (time >= pointerAsFloat) {   // test ah,1 / je
            Decode(p + 4, out);
            frame = f;
            return true;
        }
    }
    Decode(frames + 4, out);
    frame = 0;
    return true;
}

// The first frame after 'from' whose decoded state the test accepts (the test's vtable slot 0, thiscall on the
// state): its time.
// FUNC_AT(0x000fd9c0)
bool FnRawStateChan::FindTime(void *test, float from, float *time) {
    EAGL_UNTESTED("FnRawStateChan::FindTime");
    RawStateChanData *d = StateData(anim);
    for (int f = 0; f < d->frames; f++) {
        uint8_t *p = d->Frames() + d->frameSize * f;
        float t = FrameTime(p);
        if (!(t <= from)) {   // test ah,0x41 / jnp: greater or unordered
            alignas(4) uint8_t state[0x50];
            Decode(p + 4, state);
            if (AnimVCall<bool>(test, 0, static_cast<void *>(state))) {
                *time = t;
                return true;
            }
        }
    }
    return false;
}

// ---- the raw pose data (RawPoseChannel in AnimMisc.h)

namespace {

// The channel functions' original addresses, as InitAnimMemoryMap writes them into the signature tables (each
// jumps to ours below).
enum RawPoseFunctionAddress : uint32_t {
    kCopyQuatAddress = 0x000fdf20,
    kEulF3Address = 0x000fdec0,
    kCopyTransAddress = 0x000fdf60,
    kLerpQuatAddress = 0x000fdfd0,
    kLerpEulerAddress = 0x000fdf90,
    kLerpTransAddress = 0x000fe060,
};

typedef void (*FrameFn)(float **cursor, float *out);
typedef void (*LerpFn)(float t, float **cursor0, float **cursor1, float *out);

FrameFn FrameFunction(uint32_t address) {
    return reinterpret_cast<FrameFn>(uintptr_t(address));
}

LerpFn LerpFunction(uint32_t address) {
    return reinterpret_cast<LerpFn>(uintptr_t(address));
}

}  // namespace

// The signature tables' type numbers become the original addresses of the channel functions.
// FUNC_AT(0x000fda70)
void RawPoseChannel_InitAnimMemoryMap(uint8_t *data) {
    EAGL_UNTESTED("RawPoseChannel::InitAnimMemoryMap");
    RawPoseChannel *p = reinterpret_cast<RawPoseChannel *>(data);
    int count = p->count;
    uint32_t *e = p->signatures;
    for (int i = 0; i < count;) {
        int n = *e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            switch (*e) {
            case kSignatureQuat: *e = kCopyQuatAddress; break;
            case kSignatureEuler: *e = kEulF3Address; break;
            case kSignatureTrans: *e = kCopyTransAddress; break;
            default: printf("Bad signature channel type\n"); break;
            }
        }
    }
    e = p->Lerps();
    for (int i = 0; i < count;) {
        int n = *e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            switch (*e) {
            case kSignatureQuat: *e = kLerpQuatAddress; break;
            case kSignatureEuler: *e = kLerpEulerAddress; break;
            case kSignatureTrans: *e = kLerpTransAddress; break;
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
    uint32_t *e = signatures;
    for (int i = 0; i < n0;) {
        int n = *e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            if (*e == kEulF3Address)
                *e = kSignatureEuler;
            else if (*e == kCopyQuatAddress)
                *e = kSignatureQuat;
            else if (*e == kCopyTransAddress)
                *e = kSignatureTrans;
            else
                printf("Bad signature channel type\n");
        }
    }
    e = Lerps();
    for (int i = 0; i < n0;) {
        int n = *e++;
        i++;
        if (n <= 0)
            continue;
        i += n;
        for (; n > 0; n--, e++) {
            if (*e == kLerpEulerAddress)
                *e = kSignatureEuler;
            else if (*e == kLerpQuatAddress)
                *e = kSignatureQuat;
            else if (*e == kLerpTransAddress)
                *e = kSignatureTrans;
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
    uint32_t *e = signatures;
    uint32_t *end = e + count;
    float *cursor = Frame(frame);
    float *bone = out + 4;
    if (mask == NULL) {
        for (; e < end; bone += 12) {
            int n = *e++;
            for (; n > 0; n--)
                FrameFunction(*e++)(&cursor, bone);
        }
        return;
    }
    for (int b = 0; e < end; b++, bone += 12) {
        int n = *e++;
        if (InMask(static_cast<const BoneMask *>(mask), b)) {
            for (; n > 0; n--)
                FrameFunction(*e++)(&cursor, bone);
        } else {
            for (; n > 0; n--) {
                uint32_t fn = *e++;
                if (fn == kEulF3Address || fn == kCopyTransAddress)
                    cursor += 3;
                else if (fn == kCopyQuatAddress)
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
    double f = double(time) - i;
    float frac = float(f);
    if (f == 0.0 || !interpolate) {   // test ah,0x44 / jnp: the unrounded difference, ordered equal
        EvalFrame(i, out, mask);
        return;
    }
    Lerp(frac, i, i + 1, out, mask);
}

// Two frames through the second table; masked-out bones stepped over in both.
// FUNC_AT(0x000fe110)
void RawPoseChannel::Lerp(float t, int frame0, int frame1, float *out, void *mask) {
    EAGL_UNTESTED("RawPoseChannel::Lerp");
    float *cursor1 = Frame(frame1);
    float *cursor0 = Frame(frame0);
    uint32_t *e = Lerps();
    uint32_t *end = e + count;
    float *bone = out + 4;
    if (mask == NULL) {
        for (; e < end; bone += 12) {
            int n = *e++;
            for (; n > 0; n--)
                LerpFunction(*e++)(t, &cursor0, &cursor1, bone);
        }
        return;
    }
    for (int b = 0; e < end; b++, bone += 12) {
        int n = *e++;
        if (InMask(static_cast<const BoneMask *>(mask), b)) {
            for (; n > 0; n--)
                LerpFunction(*e++)(t, &cursor0, &cursor1, bone);
        } else {
            for (; n > 0; n--) {
                uint32_t fn = *e++;
                if (fn == kLerpEulerAddress || fn == kLerpTransAddress) {
                    cursor0 += 3;
                    cursor1 += 3;
                } else if (fn == kLerpQuatAddress) {
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
// 0.5 from 0x00189eb0 as a constant). The wrapper only adds the untested warning. Called by EulF3 and by
// FnDeltaSingleQ::InitBuffersAsRequired.
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
    constexpr double kDegreesToRadians = double(kPi) * kOneOver180;   // exact: a product of two floats
    float angles[3];
    angles[0] = float(kDegreesToRadians * c[0]);
    angles[1] = float(kDegreesToRadians * c[1]);
    angles[2] = float(kDegreesToRadians * c[2]);
    *cursor = c + 3;
    EAGLAnim_EulerToQuat(angles, out);
}

// FUNC_AT(0x000fdf20)
void RawPose_CopyQuat(float **cursor, float *out) {
    EAGL_UNTESTED("RawPose_CopyQuat");
    memcpy(out, *cursor, sizeof(float[4]));
    *cursor += 4;
}

// FUNC_AT(0x000fdf60)
void RawPose_CopyTrans(float **cursor, float *out) {
    EAGL_UNTESTED("RawPose_CopyTrans");
    memcpy(out + 4, *cursor, sizeof(float[3]));
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
    memcpy(ScratchQuat, *cursor0, sizeof(float[4]));
    *cursor0 += 4;
    memcpy(out, *cursor1, sizeof(float[4]));
    *cursor1 += 4;
    EAGL_VU0_fastqslerp(t, ScratchQuat, out, out);   // a tail jump in the original
}

// FUNC_AT(0x000fe060)
void RawPose_LerpTrans(float t, float **cursor0, float **cursor1, float *out) {
    EAGL_UNTESTED("RawPose_LerpTrans");
    float *first = ScratchTrans;
    memcpy(first, *cursor0, sizeof(float[3]));
    *cursor0 += 3;
    memcpy(out + 4, *cursor1, sizeof(float[3]));
    *cursor1 += 3;
    for (int j = 0; j < 3; j++)
        out[4 + j] = float((double(out[4 + j]) - first[j]) * t + first[j]);
}
