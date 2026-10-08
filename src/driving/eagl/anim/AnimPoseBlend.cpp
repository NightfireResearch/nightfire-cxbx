#include "AnimPoseBlend.h"
#include "AnimChannels.h"
#include "AnimMisc.h"
#include "AnimUntested.h"
#include "Skeleton.h"
#include "../EaglOriginals.h"
#include "../Transform.h"
#include "../../platform/X87.h"

#include <bit>
#include <string.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// FnPoseBlender (type 6) and FnEventBlender (type 4), docs/driving/eagl.md 3.4: no shipped anim builds either, so
// every function here is a provisional port from the listing with EAGL_UNTESTED.
//
// The pose blender plays animA up to 'start', animB from 'end', and between them evaluates both into its own pose
// buffers (optionally seeded with the skeleton's still pose) and slerps the quaternions bone by bone at
// (time - start) / duration, lerping the root translation. animB can be aligned to animA: Set measures, at the
// blend's start, the rotation about Y between one axis of each anim's alignment bone projected on XZ and the XZ
// offset between them (XZProjectAlign), and from then on that bone of animB's pose is post-multiplied by it. With
// an alignment bone, the translation lerped is at float bone + 8 of the pose (the original's byte offset bone * 4
// + 0x20, not bone * 0x30 + 0x20 - the same as the root's for bone 0, and kept). The event blender forwards Eval
// to one anim or both by the same windows.
//
// x87 in double in the original's order with a float store per store; the comparisons keep the original's
// unordered (NaN) outcomes.
// ---------------------------------------------------------------------------------------------------------------

namespace {


// The original's pi (.rdata 0x001a11d0; its 180, 0.5, -1 and 0 are exact; kPhaseScale: AnimMisc.h)
constexpr float kPi = 3.14159274f;
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "the original's pi");

inline double Lerp(float a, float b, float t) {
    return (double(b) - a) * t + a;
}

// out = (b - a) * t + a on a 3-vector (0x00019870, not ours).
inline void Lerp3(float t, const float *a, const float *b, float *out) {
    out[0] = float(Lerp(a[0], b[0], t));
    out[1] = float(Lerp(a[1], b[1], t));
    out[2] = float(Lerp(a[2], b[2], t));
}

// One bone of a pose (12 floats) through the alignment: built as a matrix, post-multiplied, its quaternion and
// translation written back (by copies, as the original's dword moves).
void AlignBone(float *bone, const float *align) {
    alignas(16) Transform t;
    t.BuildSQT(bone[0], bone[1], bone[2], bone[4], bone[5], bone[6], bone[7], bone[8], bone[9], bone[10]);
    t.PostMult(align);
    float quaternion[4], translation[3];
    t.ExtractQuatTrans(quaternion, translation);
    memcpy(bone + 4, quaternion, sizeof(quaternion));
    memcpy(bone + 8, translation, sizeof(translation));
}

inline void SlerpBone(float t, const float *a, const float *b, float *out, int bone) {
    EAGL_VU0_fastqslerp(t, a + bone * 12 + 4, b + bone * 12 + 4, out + bone * 12 + 4);
}

inline void LerpTranslation(float t, const float *a, const float *b, float *out, int bone) {
    Lerp3(t, a + bone * 12 + 8, b + bone * 12 + 8, out + bone * 12 + 8);
}

}  // namespace

// ---- the pose blends

// FUNC_AT(0x000fbfd0)
void AnimBlendPoseQ(int count, float t, const float *a, const float *b, float *out, const BoneMask *mask) {
    EAGL_UNTESTED("AnimBlendPoseQ");
    for (int i = 0; i < count; i++)
        if (mask == NULL || mask->Has(i))
            SlerpBone(t, a, b, out, i);
}

// FUNC_AT(0x000fc080)
void AnimBlendPoseQT(int count, float t, const float *a, const float *b, float *out, const BoneMask *mask) {
    EAGL_UNTESTED("AnimBlendPoseQT");
    for (int i = 0; i < count; i++) {
        if (mask != NULL && !mask->Has(i))
            continue;
        SlerpBone(t, a, b, out, i);
        LerpTranslation(t, a, b, out, i);
    }
}

// FUNC_AT(0x000fc1c0)
void AnimBlendPoseSQT(int count, float t, const float *a, const float *b, float *out) {
    EAGL_UNTESTED("AnimBlendPoseSQT");
    for (int i = 0; i < count; i++) {
        Lerp3(t, a + i * 12, b + i * 12, out + i * 12);
        SlerpBone(t, a, b, out, i);
        LerpTranslation(t, a, b, out, i);
    }
}

// FUNC_AT(0x000fc280)
void AnimBlendPoseQMasks(int count, float t, const float *a, const BoneMask *maskA, const float *b,
                         const BoneMask *maskB, float *out) {
    EAGL_UNTESTED("AnimBlendPoseQMasks");
    for (int i = 0; i < count; i++) {
        bool inA = maskA->Has(i), inB = maskB->Has(i);
        if (inA && inB)
            SlerpBone(t, a, b, out, i);
        else if (inA)
            memcpy(out + i * 12 + 4, a + i * 12 + 4, 16);
        else if (inB)
            memcpy(out + i * 12 + 4, b + i * 12 + 4, 16);
    }
}

// FUNC_AT(0x000fc360)
void AnimBlendPoseQTMasks(int count, float t, const float *a, const BoneMask *maskA, const float *b,
                          const BoneMask *maskB, float *out) {
    EAGL_UNTESTED("AnimBlendPoseQTMasks");
    for (int i = 0; i < count; i++) {
        bool inA = maskA->Has(i), inB = maskB->Has(i);
        if (inA && inB) {
            SlerpBone(t, a, b, out, i);
            LerpTranslation(t, a, b, out, i);
        } else if (inA) {
            memcpy(out + i * 12 + 4, a + i * 12 + 4, 28);
        } else if (inB) {
            memcpy(out + i * 12 + 4, b + i * 12 + 4, 28);
        }
    }
}

// ---- XZProjectAlign and its helpers

// The squared XZ lengths of rows 0..2 (m[0], m[2] / m[4], m[6] / m[8], m[10]); ties go to the lower row.
// FUNC_AT(0x000fbe40)
void AnimXZProjectLongestAxis(const Transform *transform, float *out, int *axis) {
    EAGL_UNTESTED("AnimXZProjectLongestAxis");
    const float *m = transform->m;
    float s0 = float(double(m[2]) * m[2] + double(m[0]) * m[0]);
    float s1 = float(double(m[6]) * m[6] + double(m[4]) * m[4]);
    float s2 = float(double(m[10]) * m[10] + double(m[8]) * m[8]);
    int row;
    if (s0 >= s1 && s0 >= s2) {   // test ah,1 / jne: on, less or unordered
        *axis = 0;
        row = 0;
    } else if (s1 >= s0 && s1 >= s2) {
        *axis = 1;
        row = 4;
    } else {
        *axis = 2;
        row = 8;
    }
    out[0] = m[row];
    out[1] = 0.0f;
    out[2] = m[row + 2];
    out[3] = 1.0f;
}

// Any other axis leaves out alone.
// FUNC_AT(0x000fbf60)
void AnimXZProjectAxis(const Transform *transform, float *out, int axis) {
    EAGL_UNTESTED("AnimXZProjectAxis");
    const float *m = transform->m;
    if (axis < 0 || axis > 2)
        return;
    out[0] = m[axis * 4];
    out[1] = 0.0f;
    out[2] = m[axis * 4 + 2];
    out[3] = 1.0f;
}

// out = a rotation about Y by the angle between a's longest XZ-projected axis and the same axis of b (in degrees,
// never negative: the angle is atan2 of the cross product's length), translated by a - b in X and Z.
// FUNC_AT(0x000fcde0)
void FnPoseBlender::XZProjectAlign(const Transform *a, const Transform *b, Transform *out) {
    EAGL_UNTESTED("FnPoseBlender::XZProjectAlign");
    float va[4], vb[4];
    int axis;
    AnimXZProjectLongestAxis(a, va, &axis);
    AnimXZProjectAxis(b, vb, axis);
    constexpr double kRadiansToDegrees = 180.0 / kPi;   // the original's 180 / pi, in double as its x87 FDIV
    double angle = AngleBetweenVectors(vb, va);
    float degrees = float(angle * kRadiansToDegrees);
    EAGL_BuildRotate(out, 0, degrees, 0.0f, 1.0f, 0.0f);
    out->m[12] = a->m[12] - b->m[12];
    out->m[13] = 0.0f;
    out->m[14] = a->m[14] - b->m[14];
}

// ---- FnPoseBlender

// The blend with alignment, the transform given (invented name).
// FUNC_AT(0x000fc4c0)
void FnPoseBlender::SetAligned(FnAnim *a, float offA, FnAnim *b, float offB, const Transform *transform,
                               int alignBone, float startTime, float length) {
    EAGL_UNTESTED("FnPoseBlender::SetAligned");
    bone = alignBone;
    memcpy(align, transform, sizeof(align));
    end = startTime + length;
    animA = a;
    animB = b;
    offsetA = offA;
    offsetB = offB;
    start = startTime;
    duration = length;
}

// The blend without alignment (invented name).
// FUNC_AT(0x000fc520)
void FnPoseBlender::SetUnaligned(FnAnim *a, float offA, FnAnim *b, float offB, float startTime, float length) {
    EAGL_UNTESTED("FnPoseBlender::SetUnaligned");
    animA = a;
    animB = b;
    end = startTime + length;
    offsetA = offA;
    offsetB = offB;
    bone = -1;
    start = startTime;
    duration = length;
}

// FUNC_AT(0x000fc560)
bool FnPoseBlender::EvalSQT(float time, float *sqt, void *maskData) {
    EAGL_UNTESTED("FnPoseBlender::EvalSQT");
    const BoneMask *mask = static_cast<const BoneMask *>(maskData);
    if (time <= start)   // test ah,0x41 / jp: past start or unordered goes on
        return AnimVCall<bool>(animA, kSlotEvalSQT, time - offsetA, sqt, maskData);
    if (time >= end) {   // test ah,1 / jne: before end or unordered blends
        if (!AnimVCall<bool>(animB, kSlotEvalSQT, time - offsetB, sqt, maskData))
            return false;
        int b = bone;
        if (b < 0)
            return true;
        if (mask != NULL && !mask->GetBone(b))
            return true;
        AlignBone(sqt + b * 12, align);
        return true;
    }

    float s = float((double(time) - start) / duration);
    if (stillA)
        skeleton->GetStillPose(poseA, mask);
    if (stillB)
        skeleton->GetStillPose(poseB, mask);
    if (!AnimVCall<bool>(animA, kSlotEvalSQT, time - offsetA, poseA, maskData))
        return false;
    if (!AnimVCall<bool>(animB, kSlotEvalSQT, time - offsetB, poseB, maskData))
        return false;

    if (mask == NULL) {
        if (bone >= 0) {
            for (int i = skeleton->count - 1; i >= 0; i--) {
                if (i == bone) {
                    AlignBone(poseB + bone * 12, align);
                    Lerp3(s, poseA + bone + 8, poseB + bone + 8, sqt + bone + 8);   // sic: float bone + 8
                }
                SlerpBone(s, poseA, poseB, sqt, i);
            }
            return true;
        }
        for (int i = skeleton->count - 1; i >= 0; i--)
            SlerpBone(s, poseA, poseB, sqt, i);
        LerpTranslation(s, poseA, poseB, sqt, 0);
        return true;
    }

    if (bone >= 0) {
        for (int i = skeleton->count - 1; i >= 0; i--) {
            if (!mask->Has(i))
                continue;
            if (i == bone) {
                AlignBone(poseB + bone * 12, align);
                Lerp3(s, poseA + bone + 8, poseB + bone + 8, sqt + bone + 8);       // sic
            }
            SlerpBone(s, poseA, poseB, sqt, i);
        }
        return true;
    }
    for (int i = skeleton->count - 1; i >= 0; i--)
        if (mask->Has(i))
            SlerpBone(s, poseA, poseB, sqt, i);
    if (mask->bits[0] & 1)
        LerpTranslation(s, poseA, poseB, sqt, 0);
    return true;
}

// FUNC_AT(0x000fca70)
void FnPoseBlender::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnPoseBlender::Eval");
    if (time <= start) {
        AnimVCall<void>(animA, kSlotEval, previous - offsetA, time - offsetA, out);
        return;
    }
    if (time >= end) {
        AnimVCall<void>(animB, kSlotEval, previous - offsetB, time - offsetB, out);
        if (bone >= 0)
            AlignBone(out + bone * 12, align);
        return;
    }

    float s = float((double(time) - start) / duration);
    if (stillA)
        skeleton->GetStillPose(poseA, NULL);
    if (stillB)
        skeleton->GetStillPose(poseB, NULL);
    AnimVCall<void>(animA, kSlotEval, previous - offsetA, time - offsetA, poseA);
    AnimVCall<void>(animB, kSlotEval, previous - offsetB, time - offsetB, poseB);
    if (bone >= 0) {
        for (int i = skeleton->count - 1; i >= 0; i--) {
            if (i == bone) {
                AlignBone(poseB + bone * 12, align);
                Lerp3(s, poseA + bone + 8, poseB + bone + 8, out + bone + 8);       // sic
            }
            SlerpBone(s, poseA, poseB, out, i);
        }
        return;
    }
    for (int i = skeleton->count - 1; i >= 0; i--)   // the original counts float indices 12 * count - 8 down to 4
        SlerpBone(s, poseA, poseB, out, i);
    LerpTranslation(s, poseA, poseB, out, 0);
}

// Starts a blend at startTime: both anims evaluated there (from 'previous') into the pose buffers, and the
// alignment of animB's bone onto animA's measured.
// FUNC_AT(0x000fce70)
void FnPoseBlender::Set(FnAnim *a, float offA, FnAnim *b, float offB, int alignBone, float previous, float startTime,
                        float length) {
    EAGL_UNTESTED("FnPoseBlender::Set");
    AnimVCall<void>(a, kSlotEval, previous - offA, startTime - offA, poseA);
    AnimVCall<void>(b, kSlotEval, previous - offB, startTime - offB, poseB);
    const float *pa = poseA + alignBone * 12;
    const float *pb = poseB + alignBone * 12;
    alignas(16) Transform ta;
    alignas(16) Transform tb;
    ta.BuildSQT(pa[0], pa[1], pa[2], pa[4], pa[5], pa[6], pa[7], pa[8], pa[9], pa[10]);
    tb.BuildSQT(pb[0], pb[1], pb[2], pb[4], pb[5], pb[6], pb[7], pb[8], pb[9], pb[10]);
    XZProjectAlign(&ta, &tb, reinterpret_cast<Transform *>(align));
    end = startTime + length;
    bone = alignBone;
    animB = b;
    offsetA = offA;
    animA = a;
    offsetB = offB;
    start = startTime;
    duration = length;
}

// ---- FnEventBlender

enum EventBlendMode {                // FnEventBlender::mode: between start and end, whose events (else both)
    kEventsFromA = 0,
    kEventsFromB = 1,
};

// FUNC_AT(0x000fcfa0)
void FnEventBlender::Set(FnAnim *a, FnAnim *b, float offA, float offB, float startTime, float length, int blendMode) {
    EAGL_UNTESTED("FnEventBlender::Set");
    animA = a;
    animB = b;
    end = startTime + length;
    offsetA = offA;
    offsetB = offB;
    start = startTime;
    duration = length;
    mode = blendMode;
}

// FUNC_AT(0x000fcfe0)
void FnEventBlender::Eval(float previous, float time, float *out) {
    EAGL_UNTESTED("FnEventBlender::Eval");
    bool useA, useB;
    if (time <= start) {                      // test ah,0x41 / jnp
        useA = true;
        useB = false;
    } else if (time >= end) {                 // test ah,1 / je
        useA = false;
        useB = true;
    } else if (mode == kEventsFromA) {
        useA = true;
        useB = false;
    } else if (mode == kEventsFromB) {
        useA = false;
        useB = true;
    } else {
        useA = true;
        useB = true;
    }
    if (useA)
        AnimVCall<void>(animA, kSlotEval, previous - offsetA, time - offsetA, out);
    if (useB)
        AnimVCall<void>(animB, kSlotEval, previous - offsetB, time - offsetB, out);
}

// ---- the phase data

// FUNC_AT(0x000fd0b0)
int AnimTruncateBlend(float value) {
    EAGL_UNTESTED("AnimTruncateBlend");
    return Truncate(value);
}

// FUNC_AT(0x000fd6d0)
int PhaseChanData::SampleCount() {
    EAGL_UNTESTED("PhaseChanData::SampleCount");
    return (numFrames - 1) / SampleStep() + 1;
}

// First pass: the first pair of samples with prev <= target <= next whose rise times phase[1] is not negative
// answers by linear interpolation (the middle of the pair if they are equal), in frames. Otherwise the sample
// nearest the target (first of equals). phase[2] > 0 limits the samples searched to that many frames.
// FUNC_AT(0x000fd0c0)
bool PhaseChanData::FindMatchTime(const float *phase, float *time) {
    EAGL_UNTESTED("PhaseChanData::FindMatchTime");
    int count = SampleCount();
    int step = SampleStep();
    float target = phase[0];
    float direction = phase[1];
    if (phase[2] > 0.0f &&                                    // test ah,0x41 / jne: skip unless greater
        double(step * count) > phase[2])
        count = Truncate(float(double(phase[2]) / step)) + 1;
    const uint8_t *s = Samples();

    float previousValue = float(s[0] * double(kPhaseScale) - 180.0);
    float rise = 0.0f;   // set before use
    for (int i = 1; i < count; i++) {
        double value = s[i] * double(kPhaseScale) - 180.0;
        // test ah,0x41 / jp: on only for prev <= target and target <= value (ordered)
        if (previousValue <= target && target <= value) {
            double d = value - previousValue;
            rise = float(d);
            if (d * direction >= 0.0) {                       // test ah,1 / je: greater or equal
                int k = i - 1;
                if (rise == 0.0f)                             // test ah,0x44 / jnp: equal only
                    *time = float((double(k) + 0.5) * step);
                else
                    *time = float(((double(target) - previousValue) / rise + k) * step);
                return true;
            }
        }
        previousValue = float(value);
    }

    int best = 0;
    double nearest = target - (s[0] * double(kPhaseScale) - 180.0);
    if (nearest < 0.0)                                        // test ah,5 / jp: negated only when less
        nearest = -nearest;
    for (int i = 1; i < count; i++) {
        double t = target - (s[i] * double(kPhaseScale) - 180.0);
        float stored = float(t);
        if (t < 0.0) {
            t = -t;
            stored = float(t);
        }
        if (t < nearest) {                                    // test ah,5 / jp: replaced only when less
            best = i;
            nearest = stored;                                 // the replacement is the float, reloaded
        }
    }
    *time = float(best * step);
    return true;
}
