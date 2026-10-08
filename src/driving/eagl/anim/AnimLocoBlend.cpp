#include "AnimLocoBlend.h"
#include "AnimPoseBlend.h"
#include "AnimChannels.h"
#include "AnimDecode.h"
#include "AnimMisc.h"
#include "AnimUntested.h"
#include "Skeleton.h"
#include "../EaglGlobals.h"
#include "../EaglOriginals.h"
#include "../Transform.h"                // QuatProduct
#include "../../platform/X87.h"
#include "../../../helpers.h"

#include <math.h>
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
// FnRunBlender and FnTurnBlender (docs/driving/eagl.md 3.4), EAGLAnim's locomotion blenders. A run blender holds
// a row of run cycles (anim data, a phase channel each, optionally a velocity anim each) and blends the two either
// side of a weight (or of a speed, through the cycles' speeds), with each cycle's time scaled so that both stay in
// step; between cycles it turns the root by the facing change over a cycle (alignQ). A turn blender does the same
// over a row of run blenders. No shipped data builds either, so every function is a provisional port from the
// listing (EAGL_UNTESTED). x87 in double in the original's order, a float store where the original stores one;
// FSQRT is sqrt() on the double (a single operation on floats written in float: the same bits). The original's
// debug printfs are kept.
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define TurnAlignCount I32_AT(0x00241b30)               // FnTurnBlender::AlignCycleBeginEnd's printf counter
#define VtTurnBlender ((const void *)0x001a14b4)
#define VtRunBlender ((const void *)0x001a1504)

// The quaternion product out = a * b (Ghidra's FUN_00016820, outside EAGLAnim, not ported).

inline float* ScratchPose(int index) {
    return static_cast<float *>(ScratchBuffer_GetScratchBuffer(index)->buffer);
}

// The phase channel's last frame (fild numFrames - 1).
inline float LastFrame(const PhaseChanData *p) {
    return float(p->numFrames - 1);
}

// ---- the helpers both classes have a copy of (each copy is its own FUNC_AT below)

// The time wrapped into [startTime, endTime].
double WrapCycleTime(float t, float startTime, float endTime) {
    float length = endTime - startTime;
    if (t < startTime) {
        double d = double(startTime) - t;
        float df = float(d);
        int n = Truncate(float(d / length));
        return endTime - (df - double(n) * length);
    }
    if (t >= endTime) {
        double d = double(t) - endTime;
        float df = float(d);
        int n = Truncate(float(d / length));
        return (df - double(n) * length) + startTime;
    }
    return t;
}

// How many cycles of [startTime, endTime] the time is outside it (0 inside).
int CycleIndex(float t, float startTime, float endTime) {
    double length = double(endTime) - startTime;
    if (t < startTime)
        return Truncate(float((double(startTime) - t) / length));
    if (t >= endTime)
        return Truncate(float((double(t) - endTime) / length)) + 1;
    return 0;
}

// The turn about y taking the 2D direction v1 to v2: q = {0, -+sin(a/2), 0, cos(a/2)}. The sign is turned by a
// multiply, as the original's FMUL by -1, which (unlike a negation) leaves a NaN's sign alone.
void AlignQuat(const float *v1, const float *v2, float *q) {
    float b0 = v2[0], b1 = v2[1];
    double a1 = v1[1], a0 = v1[0];
    q[0] = 0.0f;
    q[2] = 0.0f;
    double dot = double(v1[1]) * v2[1] + double(v1[0]) * v2[0];   // read again after the stores, as the original
    double la = sqrt(a0 * a0 + a1 * a1);
    double lb = sqrt(double(b0) * b0 + double(b1) * b1);
    double c = (dot / (la * lb) + 1.0) * 0.5;
    float s = float(sqrt(1.0 - c));
    q[1] = s;
    q[3] = float(sqrt(c));
    if (double(v1[0]) * v2[1] - double(v1[1]) * v2[0] > 0.0)
        q[1] = s * -1.0f;
}

// The root bone's quaternion (pose floats 4..7) turned by alignQ.
void AlignPoseRoot(const float *alignQ, float *sqt) {
    float q[4];
    QuatProduct(sqt + 4, alignQ, q);
    memcpy(sqt + 4, q, sizeof(q));
}

void AlignVel2D(const float *alignQ, float *vel) {
    float v[4] = { vel[0], 0.0f, vel[1], 1.0f };
    float out[4];
    AnimQuatRotateVector(alignQ, v, out);
    vel[0] = out[0];
    vel[1] = out[2];
}

// The two anims' velocities blended by weight, rescaled to the blend of their speeds.
bool BlendVel2D(FnAnim *a0, FnAnim *a1, float weight, float t0, float t1, float *vel) {
    float v0[2], v1[2];
    if (!AnimVCall<bool>(a0, kSlotEvalVel2D, t0, v0))
        return false;
    if (weight == 0.0f) {
        vel[0] = v0[0];
        vel[1] = v0[1];
        return true;
    }
    if (!AnimVCall<bool>(a1, kSlotEvalVel2D, t1, v1))
        return false;
    double w = weight;
    double u = 1.0 - w;
    vel[0] = float(v1[0] * w + v0[0] * u);
    double y = v1[1] * w + v0[1] * u;
    vel[1] = float(y);
    double x = vel[0];
    double length = sqrt(x * x + y * y);
    float lengthF = float(length);
    if (length == 0.0)
        return true;
    double s0 = sqrt(double(v0[1]) * v0[1] + double(v0[0]) * v0[0]) * u;
    double s1 = sqrt(double(v1[1]) * v1[1] + double(v1[0]) * v1[0]);
    double scale = (s0 + s1 * w) / lengthF;
    vel[0] = float(scale * vel[0]);
    vel[1] = float(scale * vel[1]);
    return true;
}

}  // namespace

// ---- free functions

// FUNC_AT(0x00104eb0)
void AnimQuatRotateVector(const float *q, const float *v, float *out) {
    EAGL_UNTESTED("AnimQuatRotateVector");
    double x2 = double(q[0]) + q[0];
    double y2 = double(q[1]) + q[1];
    float z2 = q[2] + q[2];
    float wx = float(x2 * q[3]);
    float wy = float(y2 * q[3]);
    float wz = z2 * q[3];
    float xx = float(x2 * q[0]);
    float xy = float(y2 * q[0]);
    float xz = z2 * q[0];
    double yy = y2 * q[1];
    double yz = double(z2) * q[1];
    double zz = double(z2) * q[2];
    out[0] = float(((1.0 - (zz + yy)) * v[0] + (double(xz) + wy) * v[2]) + (double(xy) - wz) * v[1]);
    out[1] = float(((1.0 - (zz + xx)) * v[1] + (double(xy) + wz) * v[0]) + (yz - wx) * v[2]);
    out[3] = 1.0f;
    out[2] = float(((1.0 - (yy + xx)) * v[2] + (double(xz) - wy) * v[0]) + (yz + wx) * v[1]);
}

// FUNC_AT(0x00104fd0)
int AnimTruncateLoco(float value) {
    EAGL_UNTESTED("AnimTruncateLoco");
    return Truncate(value);
}

// FUNC_AT(0x00104fe0)
double AnimLength2D(const float *v) {
    EAGL_UNTESTED("AnimLength2D");
    double x = v[0], y = v[1];
    return sqrt(x * x + y * y);
}

// ---- FnTurnBlender

// FUNC_AT(0x001042d0)
FnTurnBlender* FnTurnBlender::Construct() {
    EAGL_UNTESTED("FnTurnBlender::Construct");
    stat = 0;
    vtable = VtTurnBlender;
    anims = NULL;
    weight = 0.0f;
    numAnims = 0;
    idx = -100;
    freq = 1.0f;
    prevTime = 0.0f;
    offset = 0.0f;
    cycleIdx = -100;
    init = 0;
    fnAnims[0] = NULL;
    fnAnims[1] = NULL;
    type = kTurnBlender;
    return this;
}

// The original has an EH frame (state 0, its unwind funclet runs the base's destructor); nothing here can throw
// and the members are plain pointers, so none is needed.
// FUNC_AT(0x00104310)
void FnTurnBlender::Destruct() {
    EAGL_UNTESTED("FnTurnBlender::Destruct");
    vtable = VtTurnBlender;
    if (numAnims != 0)
        ScratchBuffer_GetScratchBuffer(1)->FreeBuffer();
    if (anims != NULL)
        AnimPool_FreeBlock(anims);
    FnAnim::Destruct();
}

// FUNC_AT(0x00104fa0)
FnTurnBlender* FnTurnBlender::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnTurnBlender::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x00104390)
void FnTurnBlender::Eval(float previous, float time, float *pose) {
    EAGL_UNTESTED("FnTurnBlender::Eval");
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQT, time, pose, (void *)NULL);
}

// FUNC_AT(0x001043b0)
void FnTurnBlender::SetWeight(float w) {
    EAGL_UNTESTED("FnTurnBlender::SetWeight");
    int i = Truncate(w);
    if (i < 0)
        i = 0;
    if (i >= numAnims - 1)
        i = numAnims - 2;
    weight = float(double(w) - i);
    if (i != idx) {
        if (i == idx + 1) {
            fnAnims[0] = fnAnims[1];
            fnAnims[1] = anims[i + 1];
        } else if (i == idx - 1) {
            fnAnims[1] = fnAnims[0];
            fnAnims[0] = anims[i];
        } else {
            fnAnims[0] = anims[i];
            fnAnims[1] = anims[i + 1];
        }
        idx = i;
        FnRunBlender *a = fnAnims[0];
        cycles[0] = 1.0f / a->GetFrequency();
        offsets[0] = a->offset;
        FnRunBlender *b = fnAnims[1];
        cycles[1] = 1.0f / b->GetFrequency();
        offsets[1] = b->offset;
    }
    double old = freq;
    freq = float((1.0 - weight) / cycles[0] + double(weight) / cycles[1]);
    offset = float((double(prevTime) + offset) / freq * old - prevTime);
}

// FUNC_AT(0x00104500)
void FnTurnBlender::SetAnims(Skeleton *s, int count, FnRunBlender **blenders) {
    EAGL_UNTESTED("FnTurnBlender::SetAnims");
    skeleton = s;
    ScratchBuffer_GetScratchBuffer(1)->AllocateBuffer(s->count * sizeof(float[12]));
    numAnims = count;
    anims = static_cast<FnRunBlender **>(AnimPool_NewBlock(count * sizeof(FnRunBlender *)));
    for (int i = 0; i < numAnims; i++)
        anims[i] = blenders[i];
}

// FUNC_AT(0x00104570)
bool FnTurnBlender::EvalPhase(float time, float *phase) {
    EAGL_UNTESTED("FnTurnBlender::EvalPhase");
    (void)time;
    (void)phase;
    return false;
}

// FUNC_AT(0x00104580)
bool FnTurnBlender::BlendVel(float t0, float t1, float *vel) {
    EAGL_UNTESTED("FnTurnBlender::BlendVel");
    return BlendVel2D(fnAnims[0], fnAnims[1], weight, t0, t1, vel);
}

// FUNC_AT(0x001046a0)
float FnTurnBlender::GetFrequency() {
    EAGL_UNTESTED("FnTurnBlender::GetFrequency");
    return freq;
}

// FUNC_AT(0x001046b0)
int FnTurnBlender::ComputeCycleIdx(float t, float startTime, float endTime) {
    EAGL_UNTESTED("FnTurnBlender::ComputeCycleIdx");
    return CycleIndex(t, startTime, endTime);
}

// FUNC_AT(0x00104720)
void FnTurnBlender::ComputeAlignQ(const float *v1, const float *v2, float *q) {
    EAGL_UNTESTED("FnTurnBlender::ComputeAlignQ");
    AlignQuat(v1, v2, q);
}

// FUNC_AT(0x001047e0)
void FnTurnBlender::AlignRootQ(float *sqt) {
    EAGL_UNTESTED("FnTurnBlender::AlignRootQ");
    AlignPoseRoot(alignQ, sqt);
}

// FUNC_AT(0x00104820)
void FnTurnBlender::AlignVel(float *vel) {
    EAGL_UNTESTED("FnTurnBlender::AlignVel");
    AlignVel2D(alignQ, vel);
}

// The facing (x and z of y turned by the blended root quaternion) at the start of the blenders' cycles.
// FUNC_AT(0x00104870)
bool FnTurnBlender::BlendBeginFacing(float *facing) {
    EAGL_UNTESTED("FnTurnBlender::BlendBeginFacing");
    float q0[4], q1[4], q[4], out[4];
    fnAnims[0]->ComputeBeginRootQ(q0);
    fnAnims[1]->ComputeBeginRootQ(q1);
    EAGL_VU0_fastqslerp(weight, q0, q1, q);
    float v[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    AnimQuatRotateVector(q, v, out);
    facing[0] = out[0];
    facing[1] = out[2];
    printf("Facing: %g %g\n", out[0], out[2]);
    return true;
}

// FUNC_AT(0x00104920)
double FnTurnBlender::CycleTime(float t, float startTime, float endTime) {
    EAGL_UNTESTED("FnTurnBlender::CycleTime");
    return WrapCycleTime(t, startTime, endTime);
}

// Both run blenders to the same weight (the PS2 sheet's SetSpeed).
// FUNC_AT(0x001049c0)
void FnTurnBlender::SetSpeed(float s) {
    EAGL_UNTESTED("FnTurnBlender::SetSpeed");
    FnRunBlender *a = fnAnims[0];
    a->SetWeight(s);
    cycles[0] = 1.0f / a->GetFrequency();
    offsets[0] = a->offset;
    FnRunBlender *b = fnAnims[1];
    b->SetWeight(s);
    double cycle1 = 1.0 / b->GetFrequency();   // kept unrounded on the x87 stack
    cycles[1] = float(cycle1);
    offsets[1] = b->offset;
    double old = freq;
    double f = (1.0 - weight) / cycles[0] + weight / cycle1;
    freq = float(f);
    offset = float((double(offset) + prevTime) / f * old - prevTime);
}

// FUNC_AT(0x00104a40)
bool FnTurnBlender::BlendEndFacing(float *facing) {
    EAGL_UNTESTED("FnTurnBlender::BlendEndFacing");
    float q0[4], q1[4], q[4], out[4];
    fnAnims[0]->ComputeEndRootQ(q0);
    fnAnims[1]->ComputeEndRootQ(q1);
    EAGL_VU0_fastqslerp(weight, q0, q1, q);
    float v[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    AnimQuatRotateVector(q, v, out);
    facing[0] = out[0];
    facing[1] = out[2];
    printf("Facing: %g %g\n", out[0], out[2]);
    return true;
}

// On a new cycle, alignQ turned on by the facing change from the cycle's start to its end (back, going back).
// FUNC_AT(0x00104af0)
void FnTurnBlender::AlignCycleBeginEnd(int cIdx) {
    EAGL_UNTESTED("FnTurnBlender::AlignCycleBeginEnd");
    if (!init) {
        cycleIdx = -1;
        alignQ[0] = 0.0f;
        alignQ[1] = 0.0f;
        alignQ[2] = 0.0f;
        alignQ[3] = 1.0f;
        init = 1;
        return;
    }
    if (cycleIdx == cIdx)
        return;
    float begin[2], end[2], q[4], product[4];
    BlendBeginFacing(begin);
    BlendEndFacing(end);
    ComputeAlignQ(begin, end, q);
    if (cycleIdx - 1 == cIdx)
        q[1] = q[1] * -1.0f;   // FMUL by -1 (a NaN keeps its sign)
    QuatProduct(alignQ, q, product);
    memcpy(alignQ, product, sizeof(product));
    int n = TurnAlignCount++;
    cycleIdx = cIdx;
    printf("turn align[%d] Q: %g %g %g %g\n\n", n, alignQ[0], alignQ[1], alignQ[2], alignQ[3]);
}

// The mask is not used (both sub-evaluations and the still pose get none).
// FUNC_AT(0x00104bf0)
bool FnTurnBlender::EvalSQT(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnTurnBlender::EvalSQT");
    (void)mask;
    prevTime = time;
    if (fnAnims[0] == NULL)
        SetWeight(0.0f);
    float t = time + offset;
    int cIdx = ComputeCycleIdx(t, 0.0f, 2.0f / freq);
    float t0 = float(double(cycles[0]) * freq * t);
    float t1 = float(double(t) * freq * cycles[1]);
    float c0 = float(CycleTime(t0, 0.0f, cycles[0] + cycles[0]) - offsets[0]);
    float c1 = float(CycleTime(t1, 0.0f, cycles[1] + cycles[1]) - offsets[1]);
    skeleton->GetStillPose(sqt, NULL);
    if (!AnimVCall<bool>(fnAnims[0], kSlotEvalSQT, c0, sqt, (void *)NULL))
        return false;
    if (!(weight == 0.0f)) {
        float *pose = ScratchPose(1);
        skeleton->GetStillPose(pose, NULL);
        if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, c1, pose, (void *)NULL))
            return false;
        AnimBlendPoseQT(skeleton->count, weight, sqt, pose, sqt, NULL);
    }
    AlignCycleBeginEnd(cIdx);
    AlignPoseRoot(alignQ, sqt);
    return true;
}

// FUNC_AT(0x00104d60)
bool FnTurnBlender::EvalVel2D(float time, float *vel) {
    EAGL_UNTESTED("FnTurnBlender::EvalVel2D");
    prevTime = time;
    if (fnAnims[0] == NULL)
        SetWeight(0.0f);
    float t = time + offset;
    int cIdx = ComputeCycleIdx(t, 0.0f, 2.0f / freq);
    printf("currTime: %g  offset: %g   cycle: %g\n", t, offset, 1.0 / freq);
    printf("offset0: %g  offset1: %g\n", offsets[0], offsets[1]);
    printf("cycle0: %g  cycle1: %g\n", cycles[0], cycles[1]);
    float t0 = float(double(freq) * cycles[0] * t);
    double t1Wide = double(freq) * cycles[1] * t;   // printed unrounded
    float t1 = float(t1Wide);
    printf("before offset t0: %g  t1: %g\n", t0, t1Wide);
    float c0 = float(CycleTime(t0, 0.0f, cycles[0] + cycles[0]) - offsets[0]);
    float c1 = float(CycleTime(t1, 0.0f, cycles[1] + cycles[1]) - offsets[1]);
    if (!BlendVel(c0, c1, vel))
        return false;
    AlignCycleBeginEnd(cIdx);
    AlignVel(vel);
    return true;
}

// ---- FnRunBlender

// FUNC_AT(0x00105000)
FnRunBlender* FnRunBlender::Construct() {
    EAGL_UNTESTED("FnRunBlender::Construct");
    stat = 0;
    vtable = VtRunBlender;
    anims = NULL;
    phases = NULL;
    vels = NULL;
    weight = 0.0f;
    numAnims = 0;
    idx = -100;
    freq = 1.0f;
    prevTime = 0.0f;
    offset = 0.0f;
    cycleIdx = -100;
    init = 0;
    speeds = NULL;
    fnAnims[0] = NULL;
    fnAnims[1] = NULL;
    fnVelAnims[0] = NULL;
    fnVelAnims[1] = NULL;
    type = kRunBlender;
    return this;
}

// EH frame as FnTurnBlender's destructor: not needed.
// FUNC_AT(0x00105050)
void FnRunBlender::Destruct() {
    EAGL_UNTESTED("FnRunBlender::Destruct");
    vtable = VtRunBlender;
    if (fnAnims[0] != NULL)
        AnimPool_ReleaseFnAnim(fnAnims[0]);
    if (fnAnims[1] != NULL)
        AnimPool_ReleaseFnAnim(fnAnims[1]);
    if (fnVelAnims[0] != NULL)
        AnimPool_ReleaseFnAnim(fnVelAnims[0]);
    if (fnVelAnims[1] != NULL)
        AnimPool_ReleaseFnAnim(fnVelAnims[1]);
    if (numAnims != 0)
        ScratchBuffer_GetScratchBuffer(0)->FreeBuffer();
    if (anims != NULL)
        AnimPool_FreeBlock(anims);
    if (phases != NULL)
        AnimPool_FreeBlock(phases);
    if (vels != NULL)
        AnimPool_FreeBlock(vels);
    if (speeds != NULL)
        AnimPool_FreeBlock(speeds);
    FnAnim::Destruct();
}

// FUNC_AT(0x00106300)
FnRunBlender* FnRunBlender::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnRunBlender::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// FUNC_AT(0x001051a0)
void FnRunBlender::Eval(float previous, float time, float *pose) {
    EAGL_UNTESTED("FnRunBlender::Eval");
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQT, time, pose, (void *)NULL);
}

// FUNC_AT(0x001051c0)
void FnRunBlender::SetWeight(float w) {
    EAGL_UNTESTED("FnRunBlender::SetWeight");
    int i = Truncate(w);
    if (i < 0)
        i = 0;
    if (i >= numAnims - 1)
        i = numAnims - 2;
    weight = float(double(w) - i);
    if (i != idx) {
        if (i == idx + 1) {
            AnimPool_ReleaseFnAnim(fnAnims[0]);
            fnAnims[0] = fnAnims[1];
            fnAnims[1] = AnimPool_NewFnAnim(anims[i + 1]);
        } else if (i == idx - 1) {
            AnimPool_ReleaseFnAnim(fnAnims[1]);
            fnAnims[1] = fnAnims[0];
            fnAnims[0] = AnimPool_NewFnAnim(anims[i]);
        } else {
            if (fnAnims[0] != NULL)
                AnimPool_DeleteFnAnim(fnAnims[0]);
            if (fnAnims[1] != NULL)
                AnimPool_DeleteFnAnim(fnAnims[1]);
            fnAnims[0] = AnimPool_NewFnAnim(anims[i]);
            fnAnims[1] = AnimPool_NewFnAnim(anims[i + 1]);
        }
        if (vels != NULL) {
            if (i == idx + 1) {
                AnimPool_ReleaseFnAnim(fnVelAnims[0]);
                fnVelAnims[0] = fnVelAnims[1];
                fnVelAnims[1] = AnimPool_NewFnAnim(vels[i + 1]);
            } else if (i == idx - 1) {
                AnimPool_DeleteFnAnim(fnVelAnims[1]);   // as built: no NULL test on this path
                fnVelAnims[1] = fnVelAnims[0];
                fnVelAnims[0] = AnimPool_NewFnAnim(vels[i]);
            } else {
                if (fnVelAnims[0] != NULL)
                    AnimPool_DeleteFnAnim(fnVelAnims[0]);
                if (fnVelAnims[1] != NULL)
                    AnimPool_DeleteFnAnim(fnVelAnims[1]);
                fnVelAnims[0] = AnimPool_NewFnAnim(vels[i]);
                fnVelAnims[1] = AnimPool_NewFnAnim(vels[i + 1]);
            }
        }
        const PhaseChanData *p0 = phases[i];
        if (p0->flags & kPhaseFlag01)
            alignFrame[0] = float(p0->startTime);
        else
            alignFrame[0] = float(p0->cycles[0] + p0->startTime);
        const PhaseChanData *p1 = phases[i + 1];
        alignFrame[1] = float(p1->startTime);
        if (!(p1->flags & kPhaseFlag01))
            alignFrame[1] = p1->cycles[0] + alignFrame[1];
        idx = i;
        p0 = phases[idx];
        cycles[0] = float((p0->cycles[1] + p0->cycles[0]) * 0.5);
        p1 = phases[idx + 1];
        cycles[1] = float((p1->cycles[1] + p1->cycles[0]) * 0.5);
    }
    double old = freq;
    freq = float((1.0 - weight) / cycles[0] + double(weight) / cycles[1]);
    offset = float((double(prevTime) + offset) / freq * old - prevTime);
}

// The phase channels are kept from objects built and released at once (as built); the speeds are the velocity
// anims' speeds at time 0.
// FUNC_AT(0x00105500)
void FnRunBlender::SetAnims(Skeleton *s, int count, uint8_t **animData, uint8_t **phaseData, uint8_t **velData) {
    EAGL_UNTESTED("FnRunBlender::SetAnims");
    skeleton = s;
    ScratchBuffer_GetScratchBuffer(0)->AllocateBuffer(s->count * sizeof(float[12]));
    uint32_t bytes = count * sizeof(void *);
    numAnims = count;
    anims = static_cast<uint8_t **>(AnimPool_NewBlock(bytes));
    phases = static_cast<PhaseChanData **>(AnimPool_NewBlock(bytes));
    for (int i = 0; i < numAnims; i++) {
        anims[i] = animData[i];
        FnAnim *phase = AnimPool_NewFnAnim(phaseData[i]);
        phases[i] = AnimVCall<PhaseChanData *>(phase, kSlotGetPhaseChan);
        AnimPool_ReleaseFnAnim(phase);
    }
    if (velData == NULL)
        return;
    vels = static_cast<uint8_t **>(AnimPool_NewBlock(bytes));
    memcpy(vels, velData, bytes);
    speeds = static_cast<float *>(AnimPool_NewBlock(bytes));
    for (int i = 0; i < numAnims; i++) {
        FnAnim *v = AnimPool_NewFnAnim(velData[i]);
        float vel[2];                // as built: read whether or not EvalVel2D wrote it
        AnimVCall<bool>(v, kSlotEvalVel2D, 0.0f, vel);
        speeds[i] = float(sqrt(double(vel[1]) * vel[1] + double(vel[0]) * vel[0]));
        AnimPool_ReleaseFnAnim(v);
    }
}

// FUNC_AT(0x00105670)
double FnRunBlender::CycleTime(float t, float startTime, float endTime) {
    EAGL_UNTESTED("FnRunBlender::CycleTime");
    return WrapCycleTime(t, startTime, endTime);
}

// FUNC_AT(0x00105710)
int FnRunBlender::ComputeCycleIdx(float t, float startTime, float endTime) {
    EAGL_UNTESTED("FnRunBlender::ComputeCycleIdx");
    return CycleIndex(t, startTime, endTime);
}

// FUNC_AT(0x00105780)
bool FnRunBlender::EvalPhase(float time, float *phase) {
    EAGL_UNTESTED("FnRunBlender::EvalPhase");
    (void)time;
    (void)phase;
    return false;
}

// The frame whose phase (slot 7, EvalPhase - false and nothing written for this class, so 0) crosses the input
// angle moving in its direction, interpolated; else the frame nearest the angle.
// FUNC_AT(0x00105790)
bool FnRunBlender::FindMatchTime(const MatchPhaseInput *input, float *time) {
    EAGL_UNTESTED("FnRunBlender::FindMatchTime");
    float phase = 0.0f;
    double half = (double(cycles[1]) - cycles[0]) * weight + cycles[0];
    int limit = Truncate(float(half + half));
    float angle = input->angle;
    float dAngle = input->dAngle;
    if (input->searchLength > 0.0f && double(limit) > input->searchLength)
        limit = Truncate(input->searchLength) + 1;
    AnimVCall<bool>(this, kSlotEvalPhase, 0.0f, &phase);
    double d = double(angle) - phase;
    float previous = phase;
    float best = float(d);
    if (d < 0.0)
        best = best * -1.0f;
    int bestIdx = 0;
    for (int i = 1; i < limit; i++) {
        AnimVCall<bool>(this, kSlotEvalPhase, float(i), &phase);
        if (previous <= angle && angle <= phase) {
            double step = double(phase) - previous;
            if (dAngle * step >= 0.0) {
                if (step == 0.0)
                    *time = float((i - 1) + 0.5);
                else
                    *time = float((double(angle) - previous) / step + (i - 1));
                return true;
            }
        }
        double e = double(angle) - phase;
        if (e < 0.0)
            e = e * -1.0;
        if (e < best) {
            best = float(e);
            bestIdx = i;
        }
        previous = phase;
    }
    *time = float(bestIdx);
    return true;
}

// FUNC_AT(0x00105960)
void FnRunBlender::ComputeAlignQ(const float *v1, const float *v2, float *q) {
    EAGL_UNTESTED("FnRunBlender::ComputeAlignQ");
    AlignQuat(v1, v2, q);
}

// FUNC_AT(0x00105a20)
void FnRunBlender::AlignRootQ(float *sqt) {
    EAGL_UNTESTED("FnRunBlender::AlignRootQ");
    AlignPoseRoot(alignQ, sqt);
}

// FUNC_AT(0x00105a60)
void FnRunBlender::AlignVel(float *vel) {
    EAGL_UNTESTED("FnRunBlender::AlignVel");
    AlignVel2D(alignQ, vel);
}

// FUNC_AT(0x00105ab0)
bool FnRunBlender::BlendVel(float t0, float t1, float *vel) {
    EAGL_UNTESTED("FnRunBlender::BlendVel");
    return BlendVel2D(fnVelAnims[0], fnVelAnims[1], weight, t0, t1, vel);
}

// The facing (x and z of y turned by the blended root quaternion) at times t0, t1 of the two anims.
// FUNC_AT(0x00105bd0)
bool FnRunBlender::BlendFacing(float t0, float t1, float *facing) {
    EAGL_UNTESTED("FnRunBlender::BlendFacing");
    float *pose = ScratchPose(0);
    if (!AnimVCall<bool>(fnAnims[0], kSlotEvalSQT, t0, pose, (void *)NULL))
        return false;
    float q0[4], q[4];
    memcpy(q0, pose + 4, sizeof(q0));
    if (!(weight == 0.0f)) {
        skeleton->GetStillPose(pose, NULL);
        if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, t1, pose, (void *)NULL))
            return false;
        float q1[4];
        memcpy(q1, pose + 4, sizeof(q1));
        EAGL_VU0_fastqslerp(weight, q0, q1, q);
    } else {
        memcpy(q, pose + 4, sizeof(float[4]));
    }
    float v[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    float out[4];
    AnimQuatRotateVector(q, v, out);
    facing[0] = out[0];
    facing[1] = out[2];
    return true;
}

// The weight from a speed, between the cycles' speeds.
// FUNC_AT(0x00105d00)
void FnRunBlender::SetSpeed(float s) {
    EAGL_UNTESTED("FnRunBlender::SetSpeed");
    if (s <= speeds[0]) {
        SetWeight(0.0f);
        return;
    }
    if (s >= speeds[numAnims - 1]) {
        SetWeight(float(numAnims - 1));
        return;
    }
    int j = 0;
    for (int k = 0; k < numAnims; k++)
        if (s <= speeds[k]) {
            j = k;
            break;
        }
    SetWeight(float((double(s) - speeds[j - 1]) / (double(speeds[j]) - speeds[j - 1]) + (j - 1)));
}

// FUNC_AT(0x00105da0)
void FnRunBlender::GetMinMaxSpeed(float *min, float *max) {
    EAGL_UNTESTED("FnRunBlender::GetMinMaxSpeed");
    *min = 9999999.0f;               // 0x4b18967f
    *max = -9999999.0f;
    for (int i = 0; i < numAnims; i++) {
        if (speeds[i] < *min)
            *min = speeds[i];
        if (speeds[i] > *max)
            *max = speeds[i];
    }
}

// FUNC_AT(0x00105e00)
float FnRunBlender::GetFrequency() {
    EAGL_UNTESTED("FnRunBlender::GetFrequency");
    return freq;
}

// The root bone's quaternion of the blend, anim 0 at t0 and anim 1 at t1 (q unwritten if an anim fails).
// FUNC_AT(0x00105e10)
void FnRunBlender::ComputeRootQ(float t0, float t1, float *q) {
    EAGL_UNTESTED("FnRunBlender::ComputeRootQ");
    float *pose = ScratchPose(0);
    if (!AnimVCall<bool>(fnAnims[0], kSlotEvalSQT, t0, pose, (void *)NULL))
        return;
    float q0[4];
    memcpy(q0, pose + 4, sizeof(q0));
    if (weight == 0.0f) {
        memcpy(q, q0, sizeof(q0));
        return;
    }
    skeleton->GetStillPose(pose, NULL);
    if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, t1, pose, (void *)NULL))
        return;
    float q1[4];
    memcpy(q1, pose + 4, sizeof(q1));
    EAGL_VU0_fastqslerp(weight, q0, q1, q);
}

// FUNC_AT(0x00105ef0)
void FnRunBlender::ComputeBeginRootQ(float *q) {
    EAGL_UNTESTED("FnRunBlender::ComputeBeginRootQ");
    ComputeRootQ(0.0f, 0.0f, q);
}

// FUNC_AT(0x00105f10)
void FnRunBlender::ComputeEndRootQ(float *q) {
    EAGL_UNTESTED("FnRunBlender::ComputeEndRootQ");
    ComputeRootQ(LastFrame(phases[idx]), LastFrame(phases[idx + 1]), q);
}

// FUNC_AT(0x00105f50)
void FnRunBlender::AlignCycleBeginEnd(int cIdx) {
    EAGL_UNTESTED("FnRunBlender::AlignCycleBeginEnd");
    if (!init) {
        cycleIdx = -1;
        alignQ[0] = 0.0f;
        alignQ[1] = 0.0f;
        alignQ[2] = 0.0f;
        alignQ[3] = 1.0f;
        init = 1;
        return;
    }
    if (cycleIdx == cIdx)
        return;
    float begin[2], end[2], q[4], product[4];
    BlendFacing(0.0f, 0.0f, begin);
    BlendFacing(LastFrame(phases[idx]), LastFrame(phases[idx + 1]), end);
    ComputeAlignQ(begin, end, q);
    if (cycleIdx - 1 == cIdx)
        q[1] = q[1] * -1.0f;   // FMUL by -1 (a NaN keeps its sign)
    QuatProduct(alignQ, q, product);
    memcpy(alignQ, product, sizeof(product));
    cycleIdx = cIdx;
}

// The mask is not used (both sub-evaluations and the still pose get none).
// FUNC_AT(0x00106050)
bool FnRunBlender::EvalSQT(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnRunBlender::EvalSQT");
    (void)mask;
    prevTime = time;
    if (fnAnims[0] == NULL)
        SetWeight(0.0f);
    double t = double(time) + offset;   // kept unrounded on the x87 stack
    float t0 = float(double(cycles[0]) * freq * t + alignFrame[0]);
    float t1 = float(double(cycles[1]) * freq * t + alignFrame[1]);
    int cIdx = ComputeCycleIdx(t0, 0.0f, LastFrame(phases[idx]));
    float c0 = float(CycleTime(t0, 0.0f, LastFrame(phases[idx])));
    float c1 = float(CycleTime(t1, 0.0f, LastFrame(phases[idx + 1])));
    skeleton->GetStillPose(sqt, NULL);
    if (!AnimVCall<bool>(fnAnims[0], kSlotEvalSQT, c0, sqt, (void *)NULL))
        return false;
    if (!(weight == 0.0f)) {
        float *pose = ScratchPose(0);
        skeleton->GetStillPose(pose, NULL);
        if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, c1, pose, (void *)NULL))
            return false;
        AnimBlendPoseQT(skeleton->count, weight, sqt, pose, sqt, NULL);
    }
    AlignCycleBeginEnd(cIdx);
    AlignPoseRoot(alignQ, sqt);
    return true;
}

// FUNC_AT(0x001061f0)
bool FnRunBlender::EvalVel2D(float time, float *vel) {
    EAGL_UNTESTED("FnRunBlender::EvalVel2D");
    prevTime = time;
    if (vels == NULL)
        return false;
    if (fnVelAnims[0] == NULL)
        SetWeight(0.0f);
    double t = double(time) + offset;
    float t0 = float(double(cycles[0]) * freq * t + alignFrame[0]);
    float t1 = float(double(cycles[1]) * freq * t + alignFrame[1]);
    int cIdx = ComputeCycleIdx(t0, 0.0f, LastFrame(phases[idx]));
    float c0 = float(CycleTime(t0, 0.0f, LastFrame(phases[idx])));
    float c1 = float(CycleTime(t1, 0.0f, LastFrame(phases[idx + 1])));
    if (!BlendVel(c0, c1, vel))
        return false;
    AlignCycleBeginEnd(cIdx);
    AlignVel(vel);
    return true;
}
