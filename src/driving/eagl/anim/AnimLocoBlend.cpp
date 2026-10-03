#include "AnimLocoBlend.h"
#include "AnimPoseBlend.h"
#include "AnimChannels.h"
#include "AnimDecode.h"
#include "AnimUntested.h"
#include "Skeleton.h"

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
// FSQRT is sqrt() on the double. The original's debug printfs are kept.
// ---------------------------------------------------------------------------------------------------------------

#define EaglFree      (*(void (**)(void *data, uint32_t size))0x001caf6cu)
#define FreeByType    ((void **)0x002414b0u)
#define FreeBySize    ((void **)0x00241520u)
#define TurnAlignCount (*(int32_t *)0x00241b30u)   // FnTurnBlender::AlignCycleBeginEnd's printf counter
#define VtFnAnim      ((const void *)0x001a0c6cu)
#define VtTurnBlender ((const void *)0x001a14b4u)
#define VtRunBlender  ((const void *)0x001a1504u)

#define K_One      (*(const float *)0x00189de8u)   // 1.0f
#define K_Zero     (*(const float *)0x00189decu)   // 0.0f
#define K_Half     (*(const float *)0x00189eb0u)   // 0.5f
#define K_Two      (*(const float *)0x00189e00u)   // 2.0f
#define K_MinusOne (*(const float *)0x0018a134u)   // -1.0f

// The quaternion product out = a * b (Ghidra's FUN_00016820, outside EAGLAnim, not ported).
#define QuatMultiply ((void (*)(const float *a, const float *b, float *out))0x00016820u)

static inline int Truncate(float f) {   // CVTTSS2SI
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

static inline void FreeSized(void *block) {
    uint32_t sizeClass = ((uint32_t *)block)[-1];
    *(void **)block = FreeBySize[sizeClass];
    FreeBySize[sizeClass] = block;
}

// AnimPool_DeleteFnAnim inlined: the destructor run, the object back on its type's free list.
static inline void ReleaseAnim(FnAnim *anim) {
    AnimVCall<void *>(anim, kSlotDelete, 0u);
    uint16_t type = (uint16_t)anim->type;
    *(void **)anim = FreeByType[type];
    FreeByType[type] = anim;
}

static inline float* ScratchPose(int index) {
    return (float *)ScratchBuffer_GetScratchBuffer(index)->buffer;
}

// 0x000fc080, AnimBlendPoseQT (AnimPoseBlend.cpp): out = blend of two poses by weight
static inline void PoseBlend(int count, float weight, float *a, float *b, float *out, void *mask) {
    AnimBlendPoseQT(count, weight, a, b, out, (const BoneMask *)mask);
}

static inline double PhaseLastFrame(const PhaseChanData *p) {   // fild (numFrames - 1)
    return (double)(int)(p->numFrames - 1);
}

// ---- the helpers both classes have a copy of (each copy is its own FUNC_AT below)

// The time wrapped into [startTime, endTime].
static double WrapCycleTime(float t, float startTime, float endTime) {
    float length = (float)((double)endTime - (double)startTime);
    if (t < startTime) {
        double d = (double)startTime - (double)t;
        float df = (float)d;
        int n = Truncate((float)(d / (double)length));
        return (double)endTime - ((double)df - (double)n * (double)length);
    }
    if (t >= endTime) {
        double d = (double)t - (double)endTime;
        float df = (float)d;
        int n = Truncate((float)(d / (double)length));
        return ((double)df - (double)n * (double)length) + (double)startTime;
    }
    return (double)t;
}

// How many cycles of [startTime, endTime] the time is outside it (0 inside).
static int CycleIndex(float t, float startTime, float endTime) {
    double length = (double)endTime - (double)startTime;
    if (t < startTime)
        return Truncate((float)(((double)startTime - (double)t) / length));
    if (t >= endTime)
        return Truncate((float)(((double)t - (double)endTime) / length)) + 1;
    return 0;
}

// The turn about y taking the 2D direction v1 to v2: q = {0, -+sin(a/2), 0, cos(a/2)}.
static void AlignQuat(const float *v1, const float *v2, float *q) {
    float b0 = v2[0], b1 = v2[1];
    double a1 = v1[1], a0 = v1[0];
    q[0] = 0.0f;
    q[2] = 0.0f;
    double dot = (double)v1[1] * (double)v2[1] + (double)v1[0] * (double)v2[0];
    double la = sqrt(a0 * a0 + a1 * a1);
    double lb = sqrt((double)b0 * (double)b0 + (double)b1 * (double)b1);
    double c = (dot / (la * lb) + (double)K_One) * (double)K_Half;
    float s = (float)sqrt((double)K_One - c);
    q[1] = s;
    q[3] = (float)sqrt(c);
    if ((double)v1[0] * (double)v2[1] - (double)v1[1] * (double)v2[0] > 0.0)
        q[1] = (float)((double)s * (double)K_MinusOne);
}

// The root bone's quaternion (pose floats 4..7) turned by alignQ.
static void AlignPoseRoot(const float *alignQ, float *sqt) {
    float q[4];
    QuatMultiply(sqt + 4, alignQ, q);
    memcpy(sqt + 4, q, 16);
}

static void AlignVel2D(const float *alignQ, float *vel) {
    float v[4] = { vel[0], 0.0f, vel[1], 1.0f };
    float out[4];
    AnimQuatRotateVector(alignQ, v, out);
    vel[0] = out[0];
    vel[1] = out[2];
}

// The two anims' velocities blended by weight, rescaled to the blend of their speeds.
static bool BlendVel2D(FnAnim *a0, FnAnim *a1, float weight, float t0, float t1, float *vel) {
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
    double u = (double)K_One - w;
    vel[0] = (float)((double)v1[0] * w + (double)v0[0] * u);
    double y = (double)v1[1] * w + (double)v0[1] * u;
    vel[1] = (float)y;
    double x = vel[0];
    double length = sqrt(x * x + y * y);
    float lengthF = (float)length;
    if (length == 0.0)
        return true;
    double s0 = sqrt((double)v0[1] * (double)v0[1] + (double)v0[0] * (double)v0[0]) * u;
    double s1 = sqrt((double)v1[1] * (double)v1[1] + (double)v1[0] * (double)v1[0]);
    double scale = (s0 + s1 * w) / (double)lengthF;
    vel[0] = (float)(scale * (double)vel[0]);
    vel[1] = (float)(scale * (double)vel[1]);
    return true;
}

// ---- free functions

// FUNC_AT(0x00104eb0)
void AnimQuatRotateVector(const float *q, const float *v, float *out) {
    EAGL_UNTESTED("AnimQuatRotateVector");
    double x2 = (double)q[0] + (double)q[0];
    double y2 = (double)q[1] + (double)q[1];
    float z2 = (float)((double)q[2] + (double)q[2]);
    float wx = (float)(x2 * (double)q[3]);
    float wy = (float)(y2 * (double)q[3]);
    float wz = (float)((double)z2 * (double)q[3]);
    float xx = (float)(x2 * (double)q[0]);
    float xy = (float)(y2 * (double)q[0]);
    float xz = (float)((double)z2 * (double)q[0]);
    double yy = y2 * (double)q[1];
    double yz = (double)z2 * (double)q[1];
    double zz = (double)z2 * (double)q[2];
    double one = K_One;
    out[0] = (float)(((one - (zz + yy)) * (double)v[0] + ((double)xz + (double)wy) * (double)v[2]) +
                     ((double)xy - (double)wz) * (double)v[1]);
    out[1] = (float)(((one - (zz + (double)xx)) * (double)v[1] + ((double)xy + (double)wz) * (double)v[0]) +
                     (yz - (double)wx) * (double)v[2]);
    out[3] = 1.0f;
    out[2] = (float)(((one - (yy + (double)xx)) * (double)v[2] + ((double)xz - (double)wy) * (double)v[0]) +
                     (yz + (double)wx) * (double)v[1]);
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
        FreeSized(anims);
    FnAnim::Destruct();
}

// FUNC_AT(0x00104fa0)
FnTurnBlender* FnTurnBlender::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnTurnBlender::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, 0x5c);
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
    weight = (float)((double)w - (double)i);
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
        cycles[0] = (float)((double)K_One / (double)a->GetFrequency());
        offsets[0] = a->offset;
        FnRunBlender *b = fnAnims[1];
        cycles[1] = (float)((double)K_One / (double)b->GetFrequency());
        offsets[1] = b->offset;
    }
    double old = freq;
    freq = (float)(((double)K_One - (double)weight) / (double)cycles[0] + (double)weight / (double)cycles[1]);
    offset = (float)(((double)prevTime + (double)offset) / (double)freq * old - (double)prevTime);
}

// FUNC_AT(0x00104500)
void FnTurnBlender::SetAnims(Skeleton *s, int count, FnRunBlender **blenders) {
    EAGL_UNTESTED("FnTurnBlender::SetAnims");
    skeleton = s;
    ScratchBuffer_GetScratchBuffer(1)->AllocateBuffer((uint32_t)(s->count * 48));
    numAnims = count;
    anims = (FnRunBlender **)AnimPool_NewBlock((uint32_t)(count * 4));
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
    printf("Facing: %g %g\n", (double)out[0], (double)out[2]);
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
    cycles[0] = (float)((double)K_One / (double)a->GetFrequency());
    offsets[0] = a->offset;
    FnRunBlender *b = fnAnims[1];
    b->SetWeight(s);
    double cycle1 = (double)K_One / (double)b->GetFrequency();   // kept unrounded on the x87 stack
    cycles[1] = (float)cycle1;
    offsets[1] = b->offset;
    double old = freq;
    double f = ((double)K_One - (double)weight) / (double)cycles[0] + (double)weight / cycle1;
    freq = (float)f;
    offset = (float)(((double)offset + (double)prevTime) / f * old - (double)prevTime);
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
    printf("Facing: %g %g\n", (double)out[0], (double)out[2]);
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
        q[1] = (float)((double)q[1] * (double)K_MinusOne);
    QuatMultiply(alignQ, q, product);
    memcpy(alignQ, product, 16);
    int n = TurnAlignCount++;
    cycleIdx = cIdx;
    printf("turn align[%d] Q: %g %g %g %g\n\n", n, (double)alignQ[0], (double)alignQ[1], (double)alignQ[2],
           (double)alignQ[3]);
}

// The mask is not used (both sub-evaluations and the still pose get none).
// FUNC_AT(0x00104bf0)
bool FnTurnBlender::EvalSQT(float time, float *sqt, void *mask) {
    EAGL_UNTESTED("FnTurnBlender::EvalSQT");
    (void)mask;
    prevTime = time;
    if (fnAnims[0] == NULL)
        SetWeight(0.0f);
    float t = (float)((double)time + (double)offset);
    int cIdx = ComputeCycleIdx(t, 0.0f, (float)((double)K_Two / (double)freq));
    float t0 = (float)((double)cycles[0] * (double)freq * (double)t);
    float t1 = (float)((double)t * (double)freq * (double)cycles[1]);
    float c0 = (float)(CycleTime(t0, 0.0f, (float)((double)cycles[0] + (double)cycles[0])) - (double)offsets[0]);
    float c1 = (float)(CycleTime(t1, 0.0f, (float)((double)cycles[1] + (double)cycles[1])) - (double)offsets[1]);
    skeleton->GetStillPose(sqt, NULL);
    if (!AnimVCall<bool>(fnAnims[0], kSlotEvalSQT, c0, sqt, (void *)NULL))
        return false;
    if (!(weight == 0.0f)) {
        float *pose = ScratchPose(1);
        skeleton->GetStillPose(pose, NULL);
        if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, c1, pose, (void *)NULL))
            return false;
        PoseBlend(skeleton->count, weight, sqt, pose, sqt, NULL);
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
    float t = (float)((double)time + (double)offset);
    int cIdx = ComputeCycleIdx(t, 0.0f, (float)((double)K_Two / (double)freq));
    printf("currTime: %g  offset: %g   cycle: %g\n", (double)t, (double)offset, (double)K_One / (double)freq);
    printf("offset0: %g  offset1: %g\n", (double)offsets[0], (double)offsets[1]);
    printf("cycle0: %g  cycle1: %g\n", (double)cycles[0], (double)cycles[1]);
    float t0 = (float)((double)freq * (double)cycles[0] * (double)t);
    double t1Wide = (double)freq * (double)cycles[1] * (double)t;   // printed unrounded
    float t1 = (float)t1Wide;
    printf("before offset t0: %g  t1: %g\n", (double)t0, t1Wide);
    float c0 = (float)(CycleTime(t0, 0.0f, (float)((double)cycles[0] + (double)cycles[0])) - (double)offsets[0]);
    float c1 = (float)(CycleTime(t1, 0.0f, (float)((double)cycles[1] + (double)cycles[1])) - (double)offsets[1]);
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
        ReleaseAnim(fnAnims[0]);
    if (fnAnims[1] != NULL)
        ReleaseAnim(fnAnims[1]);
    if (fnVelAnims[0] != NULL)
        ReleaseAnim(fnVelAnims[0]);
    if (fnVelAnims[1] != NULL)
        ReleaseAnim(fnVelAnims[1]);
    if (numAnims != 0)
        ScratchBuffer_GetScratchBuffer(0)->FreeBuffer();
    if (anims != NULL)
        FreeSized(anims);
    if (phases != NULL)
        FreeSized(phases);
    if (vels != NULL)
        FreeSized(vels);
    if (speeds != NULL)
        FreeSized(speeds);
    FnAnim::Destruct();
}

// FUNC_AT(0x00106300)
FnRunBlender* FnRunBlender::ScalarDelete(unsigned flags) {
    EAGL_UNTESTED("FnRunBlender::ScalarDelete");
    Destruct();
    if (flags & 1)
        EaglFree(this, 0x80);
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
    weight = (float)((double)w - (double)i);
    if (i != idx) {
        if (i == idx + 1) {
            ReleaseAnim(fnAnims[0]);
            fnAnims[0] = fnAnims[1];
            fnAnims[1] = AnimPool_NewFnAnim(anims[i + 1]);
        } else if (i == idx - 1) {
            ReleaseAnim(fnAnims[1]);
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
                ReleaseAnim(fnVelAnims[0]);
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
        if (p0->flag & 1)
            alignFrame[0] = (float)(int)p0->startTime;
        else
            alignFrame[0] = (float)(int)(p0->cycles[0] + p0->startTime);
        const PhaseChanData *p1 = phases[i + 1];
        alignFrame[1] = (float)(int)p1->startTime;
        if (!(p1->flag & 1))
            alignFrame[1] = (float)((double)(int)p1->cycles[0] + (double)alignFrame[1]);
        idx = i;
        p0 = phases[idx];
        cycles[0] = (float)((double)(int)(p0->cycles[1] + p0->cycles[0]) * (double)K_Half);
        p1 = phases[idx + 1];
        cycles[1] = (float)((double)(int)(p1->cycles[1] + p1->cycles[0]) * (double)K_Half);
    }
    double old = freq;
    freq = (float)(((double)K_One - (double)weight) / (double)cycles[0] + (double)weight / (double)cycles[1]);
    offset = (float)(((double)prevTime + (double)offset) / (double)freq * old - (double)prevTime);
}

// The phase channels are kept from objects built and released at once (as built); the speeds are the velocity
// anims' speeds at time 0.
// FUNC_AT(0x00105500)
void FnRunBlender::SetAnims(Skeleton *s, int count, uint8_t **animData, uint8_t **phaseData, uint8_t **velData) {
    EAGL_UNTESTED("FnRunBlender::SetAnims");
    skeleton = s;
    ScratchBuffer_GetScratchBuffer(0)->AllocateBuffer((uint32_t)(s->count * 48));
    uint32_t bytes = (uint32_t)(count * 4);
    numAnims = count;
    anims = (uint8_t **)AnimPool_NewBlock(bytes);
    phases = (PhaseChanData **)AnimPool_NewBlock(bytes);
    for (int i = 0; i < numAnims; i++) {
        anims[i] = animData[i];
        FnAnim *phase = AnimPool_NewFnAnim(phaseData[i]);
        phases[i] = AnimVCall<PhaseChanData *>(phase, kSlotGetPhaseChan);
        ReleaseAnim(phase);
    }
    if (velData == NULL)
        return;
    vels = (uint8_t **)AnimPool_NewBlock(bytes);
    memcpy(vels, velData, bytes);
    speeds = (float *)AnimPool_NewBlock(bytes);
    for (int i = 0; i < numAnims; i++) {
        FnAnim *v = AnimPool_NewFnAnim(velData[i]);
        float vel[2];                // as built: read whether or not EvalVel2D wrote it
        AnimVCall<bool>(v, kSlotEvalVel2D, 0.0f, vel);
        speeds[i] = (float)sqrt((double)vel[1] * (double)vel[1] + (double)vel[0] * (double)vel[0]);
        ReleaseAnim(v);
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
    double half = ((double)cycles[1] - (double)cycles[0]) * (double)weight + (double)cycles[0];
    int limit = Truncate((float)(half + half));
    float angle = input->angle;
    float dAngle = input->dAngle;
    if (input->searchLength > 0.0f && (double)limit > (double)input->searchLength)
        limit = Truncate(input->searchLength) + 1;
    AnimVCall<bool>(this, kSlotEvalPhase, 0.0f, &phase);
    double d = (double)angle - (double)phase;
    float previous = phase;
    float best = (float)d;
    if (d < 0.0)
        best = (float)((double)best * (double)K_MinusOne);
    int bestIdx = 0;
    for (int i = 1; i < limit; i++) {
        AnimVCall<bool>(this, kSlotEvalPhase, (float)i, &phase);
        if (previous <= angle && angle <= phase) {
            double step = (double)phase - (double)previous;
            if ((double)dAngle * step >= 0.0) {
                if (step == 0.0)
                    *time = (float)((double)(i - 1) + (double)K_Half);
                else
                    *time = (float)(((double)angle - (double)previous) / step + (double)(i - 1));
                return true;
            }
        }
        double e = (double)angle - (double)phase;
        if (e < 0.0)
            e = e * (double)K_MinusOne;
        if (e < (double)best) {
            best = (float)e;
            bestIdx = i;
        }
        previous = phase;
    }
    *time = (float)bestIdx;
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
    memcpy(q0, pose + 4, 16);
    if (!(weight == 0.0f)) {
        skeleton->GetStillPose(pose, NULL);
        if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, t1, pose, (void *)NULL))
            return false;
        float q1[4];
        memcpy(q1, pose + 4, 16);
        EAGL_VU0_fastqslerp(weight, q0, q1, q);
    } else {
        memcpy(q, pose + 4, 16);
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
        SetWeight((float)(numAnims - 1));
        return;
    }
    int j = 0;
    for (int k = 0; k < numAnims; k++)
        if (s <= speeds[k]) {
            j = k;
            break;
        }
    SetWeight((float)(((double)s - (double)speeds[j - 1]) / ((double)speeds[j] - (double)speeds[j - 1]) +
                      (double)(j - 1)));
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
    memcpy(q0, pose + 4, 16);
    if (weight == 0.0f) {
        memcpy(q, q0, 16);
        return;
    }
    skeleton->GetStillPose(pose, NULL);
    if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, t1, pose, (void *)NULL))
        return;
    float q1[4];
    memcpy(q1, pose + 4, 16);
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
    ComputeRootQ((float)PhaseLastFrame(phases[idx]), (float)PhaseLastFrame(phases[idx + 1]), q);
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
    BlendFacing((float)PhaseLastFrame(phases[idx]), (float)PhaseLastFrame(phases[idx + 1]), end);
    ComputeAlignQ(begin, end, q);
    if (cycleIdx - 1 == cIdx)
        q[1] = (float)((double)q[1] * (double)K_MinusOne);
    QuatMultiply(alignQ, q, product);
    memcpy(alignQ, product, 16);
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
    double t = (double)time + (double)offset;   // kept unrounded on the x87 stack
    float t0 = (float)((double)cycles[0] * (double)freq * t + (double)alignFrame[0]);
    float t1 = (float)((double)cycles[1] * (double)freq * t + (double)alignFrame[1]);
    int cIdx = ComputeCycleIdx(t0, 0.0f, (float)PhaseLastFrame(phases[idx]));
    float c0 = (float)CycleTime(t0, 0.0f, (float)PhaseLastFrame(phases[idx]));
    float c1 = (float)CycleTime(t1, 0.0f, (float)PhaseLastFrame(phases[idx + 1]));
    skeleton->GetStillPose(sqt, NULL);
    if (!AnimVCall<bool>(fnAnims[0], kSlotEvalSQT, c0, sqt, (void *)NULL))
        return false;
    if (!(weight == 0.0f)) {
        float *pose = ScratchPose(0);
        skeleton->GetStillPose(pose, NULL);
        if (!AnimVCall<bool>(fnAnims[1], kSlotEvalSQT, c1, pose, (void *)NULL))
            return false;
        PoseBlend(skeleton->count, weight, sqt, pose, sqt, NULL);
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
    double t = (double)time + (double)offset;
    float t0 = (float)((double)cycles[0] * (double)freq * t + (double)alignFrame[0]);
    float t1 = (float)((double)cycles[1] * (double)freq * t + (double)alignFrame[1]);
    int cIdx = ComputeCycleIdx(t0, 0.0f, (float)PhaseLastFrame(phases[idx]));
    float c0 = (float)CycleTime(t0, 0.0f, (float)PhaseLastFrame(phases[idx]));
    float c1 = (float)CycleTime(t1, 0.0f, (float)PhaseLastFrame(phases[idx + 1]));
    if (!BlendVel(c0, c1, vel))
        return false;
    AlignCycleBeginEnd(cIdx);
    AlignVel(vel);
    return true;
}
