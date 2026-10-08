#include "IKShadow.h"

#include "../anim/Events.h"
#include "../anim/IK.h"
#include "../engine/UMemory.hpp"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_IKSHADOW=1, from the first simulation tick (the manager's event resolver exists then): every function
// of anim/IK.cpp, ActEvents' constructor and the Transform methods and helpers compiled beside them
// (eagl/Transform.cpp) against the original, on the same inputs, compared byte for byte.
//
// The originals run with their whole range swapped back (0x000157e0..0x00016ec0, common/xbeOriginal.h), so
// an original composite reaches the original helpers; what lies outside (the VU0 maths, ActSkeleton's blends,
// EAGLAnim's still pose) is the same code for both. Each case builds its state twice from the same parameters,
// once for the port and once for the original, and compares everything the call may write: output buffers
// prefilled with a pattern (so "untouched" is checked too), the matrices, the objects. Allocations compare by
// content. Inputs: random rigid matrices and unit quaternions, random vectors, targets in and out of the chain's
// reach, weights below 0, between 0 and 1, exactly 1 and above, poles or none, and a fake EAGLAnim skeleton for
// ActIK::Init and CreateIKs. ActEvents is built twice over the live resolver (0x001dd9d0).
//
// A word that differs only as two NaNs is counted apart (NaN payloads differ between the x87 and SSE).
// ---------------------------------------------------------------------------------------------------------------

#define LiveEventResolver (*(ActEventResolver **)0x001dd9d0)

namespace {

constexpr unsigned kRangeStart = 0x000157e0;
constexpr unsigned kRangeEnd = 0x00016ec0;
constexpr int kBones = 8;
constexpr int kCases = 400;
constexpr uint32_t kPattern = 0x4640e400;      // 12345.0f

struct OriginalRange {
    OriginalRange() { XbeOriginal_RestoreRange(kRangeStart, kRangeEnd, true); }
    ~OriginalRange() { XbeOriginal_RestoreRange(kRangeStart, kRangeEnd, false); }
};

// ---- the originals

typedef void (__fastcall *TransformVectorFn)(const Transform *, int, const float *, float *);
typedef void (__fastcall *OrthoInverseFn)(const Transform *, int, Transform *);
typedef void (__fastcall *BuildQTFn)(Transform *, int, float, float, float, float, float, float, float);
typedef void (__fastcall *BuildRotationFn)(Transform *, int, float, float, float, float);
typedef double (*AngleFn)(const float *, const float *);
typedef void (*PerpendicularFn)(const float *, float *);
typedef void (*AxisAngleFn)(const float *, float, float *);
typedef void (*QuatFn)(const float *, const float *, float *);
typedef double (__fastcall *JointAngleFn)(ActIK *, int, float, float, const float *);
typedef void (__fastcall *IKInitFn)(ActIK *, int, Skeleton *, int, const Coord4 *);
typedef void (__fastcall *IKSolveFn)(ActIK *, int, const Transform *, const float *, Transform *, Transform *,
                                     const Transform *, const float *);
typedef void (__fastcall *SolverBlendFn)(ActIKSolver *, int, Transform *, const Transform *, float);
typedef void (__fastcall *OverrideBlendFn)(ActGlobalPoseOverrideArray *, int, Transform *, const Transform *, float);
typedef ActIKSolverArray *(__fastcall *SolverArrayConstructFn)(ActIKSolverArray *, int, int, Skeleton *, Transform *,
                                                                Transform *);
typedef void (__fastcall *SetInfoFn)(ActIKSolverArray *, int, int, const ActIKSolveInfo *);
typedef void (__fastcall *CreateIKsFn)(ActIKSolverArray *, int, int, const int *, const Coord4 *, const bool *);
typedef void (__fastcall *SolverArrayFn)(ActIKSolverArray *, int);
typedef void (__fastcall *SolverArraySolveFn)(ActIKSolverArray *, int, int);
typedef ActGlobalPoseOverrideArray *(__fastcall *OverrideConstructFn)(ActGlobalPoseOverrideArray *, int, Transform *);
typedef void (__fastcall *OverrideCreateFn)(ActGlobalPoseOverrideArray *, int, int, const int *, bool);
typedef void (__fastcall *SetOverrideFn)(ActGlobalPoseOverrideArray *, int, int, const Transform *, float);
typedef void (__fastcall *SetOverridesFn)(ActGlobalPoseOverrideArray *, int, int, const Transform *, const float *);
typedef void (__fastcall *DoOverridesFn)(ActGlobalPoseOverrideArray *, int, bool, Transform *);
typedef void (__fastcall *OverrideDestructFn)(ActGlobalPoseOverrideArray *, int);
typedef ActEvents *(__fastcall *EventsConstructFn)(ActEvents *, int, ActEventResolver *);

#define OrigTransformVector ((TransformVectorFn)0x000161a0)
#define OrigOrthoInverse ((OrthoInverseFn)0x00016250)
#define OrigBuildQT ((BuildQTFn)0x000162e0)
#define OrigBuildRotation ((BuildRotationFn)0x000163e0)
#define OrigAngleBetween ((AngleFn)0x00016530)
#define OrigPerpendicular ((PerpendicularFn)0x00016590)
#define OrigAxisAngle ((AxisAngleFn)0x00016640)
#define OrigQuatFromTo ((QuatFn)0x000166a0)
#define OrigQuatProduct ((QuatFn)0x00016820)
#define OrigJointAngle ((JointAngleFn)0x000168b0)
#define OrigIKInit ((IKInitFn)0x00016980)
#define OrigIKSolve ((IKSolveFn)0x00016a30)
#define OrigSolverBlend ((SolverBlendFn)0x00015900)
#define OrigOverrideBlend ((OverrideBlendFn)0x00015ac0)
#define OrigSolverArrayConstruct ((SolverArrayConstructFn)0x000159b0)
#define OrigSetInfo ((SetInfoFn)0x000159e0)
#define OrigCreateIKs ((CreateIKsFn)0x00016c30)
#define OrigDeleteIKs ((SolverArrayFn)0x00016d20)
#define OrigSolverArrayDestruct ((SolverArrayFn)0x00016e90)
#define OrigSolverArraySolve ((SolverArraySolveFn)0x00016ea0)
#define OrigOverrideConstruct ((OverrideConstructFn)0x00015a10)
#define OrigOverrideCreate ((OverrideCreateFn)0x00015a30)
#define OrigSetOverride ((SetOverrideFn)0x00015b40)
#define OrigSetOverrides ((SetOverridesFn)0x00015d00)
#define OrigDoOverrides ((DoOverridesFn)0x00015d40)
#define OrigOverrideDestruct ((OverrideDestructFn)0x00016940)
#define OrigEventsConstruct ((EventsConstructFn)0x000157e0)

// ---- bookkeeping

long g_cases, g_checks, g_differ, g_nan, g_faults;
int g_reported;

bool IsNaN(uint32_t w) {
    return (w & 0x7f800000) == 0x7f800000 && (w & 0x007fffff) != 0;
}

// One comparison of a region: port against original, a word at a time.
void Check(const char *what, const void *port, const void *original, size_t bytes) {
    g_checks++;
    const uint8_t *p = static_cast<const uint8_t *>(port);
    const uint8_t *o = static_cast<const uint8_t *>(original);
    bool nanOnly = true;
    size_t first = bytes;
    for (size_t i = 0; i + 4 <= bytes; i += 4) {
        uint32_t a, b;
        memcpy(&a, p + i, 4);
        memcpy(&b, o + i, 4);
        if (a == b)
            continue;
        if (first == bytes)
            first = i;
        if (!(IsNaN(a) && IsNaN(b)))
            nanOnly = false;
    }
    if (bytes % 4 != 0 && memcmp(p + bytes / 4 * 4, o + bytes / 4 * 4, bytes % 4) != 0) {
        nanOnly = false;
        if (first == bytes)
            first = bytes / 4 * 4;
    }
    if (first == bytes)
        return;
    if (nanOnly) {
        g_nan++;
        return;
    }
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        uint32_t a = 0, b = 0;
        memcpy(&a, p + first, bytes - first < 4 ? bytes - first : 4);
        memcpy(&b, o + first, bytes - first < 4 ? bytes - first : 4);
        printf("[ikshadow] DIFF %s (case %ld) +0x%x: port %08x original %08x\n", what, g_cases, (unsigned)first, a,
               b);
    }
}

void CheckDouble(const char *what, double port, double original) {
    Check(what, &port, &original, sizeof(double));
}

// A call guarded so a fault is counted, not fatal
bool Guard(void (*fn)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        fn(context);
    } __except (1) {
        g_faults++;
        if (g_reported < 10) {
            g_reported++;
            printf("[ikshadow] FAULT in case %ld\n", g_cases);
        }
        return false;
    }
#else
    fn(context);
#endif
    return true;
}

// ---- inputs

uint32_t g_seed = 0x1c0ffee5;
uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

float Uniform(float range) {
    return float(int32_t(Next())) / 2147483648.0f * range;
}

void Fill(void *p, size_t bytes) {
    uint32_t *w = static_cast<uint32_t *>(p);
    for (size_t i = 0; i < bytes / 4; i++)
        w[i] = kPattern;
}

void UnitQuat(float *q) {
    double x = Uniform(1), y = Uniform(1), z = Uniform(1), w = Uniform(1);
    double n = sqrt(x * x + y * y + z * z + w * w);
    if (n < 1e-3)
        x = 0, y = 0, z = 0, w = 1, n = 1;
    q[0] = float(x / n);
    q[1] = float(y / n);
    q[2] = float(z / n);
    q[3] = float(w / n);
}

// A rotation and translation (row vectors, translation in row 3)
void RigidMatrix(Transform *t, float reach) {
    float q[4];
    UnitQuat(q);
    double x = q[0], y = q[1], z = q[2], w = q[3];
    float *m = t->m;
    m[0] = float(1 - 2 * (y * y + z * z));
    m[1] = float(2 * (x * y + z * w));
    m[2] = float(2 * (x * z - y * w));
    m[3] = 0;
    m[4] = float(2 * (x * y - z * w));
    m[5] = float(1 - 2 * (x * x + z * z));
    m[6] = float(2 * (y * z + x * w));
    m[7] = 0;
    m[8] = float(2 * (x * z + y * w));
    m[9] = float(2 * (y * z - x * w));
    m[10] = float(1 - 2 * (x * x + y * y));
    m[11] = 0;
    m[12] = Uniform(reach);
    m[13] = Uniform(reach);
    m[14] = Uniform(reach);
    m[15] = 1;
}

void RandomVector(float *v, int n, float range) {
    for (int i = 0; i < n; i++)
        v[i] = Uniform(range);
    // now and then a degenerate one
    if (Next() % 40 == 0)
        for (int i = 0; i < n; i++)
            v[i] = 0.0f;
}

// A weight from every range the code tells apart
float RandomWeight() {
    static const float kWeights[] = { -0.25f, 0.0f, 0.25f, 0.5f, 0.999f, 1.0f, 1.5f };
    if (Next() % 3 == 0)
        return Uniform(1.2f) + 0.4f;
    return kWeights[Next() % (sizeof(kWeights) / sizeof(kWeights[0]))];
}

// A fake EAGLAnim skeleton: bones with random still poses
alignas(16) uint8_t g_skeletonBytes[offsetof(Skeleton, bones) + kBones * sizeof(SkeletonBone)];
Skeleton *FakeSkeleton() {
    Skeleton *s = reinterpret_cast<Skeleton *>(g_skeletonBytes);
    memset(g_skeletonBytes, 0, sizeof(g_skeletonBytes));
    s->count = kBones;
    s->lengthScales = NULL;
    for (int i = 0; i < kBones; i++) {
        SkeletonBone *b = &s->bones[i];
        b->scale[0] = b->scale[1] = b->scale[2] = 1.0f;
        b->parent = i - 1;
        UnitQuat(b->rotation);
        RandomVector(b->translation, 3, 1.0f);
        b->mirror = i;
        RigidMatrix(&b->inverseBind, 1.0f);
    }
    return s;
}

// ---- the helpers

struct HelperState {
    alignas(16) Transform matrix;
    alignas(16) Transform out;
    float in[4];
    float in2[4];
    float vec[4];
    float vec2[4];
    double result;
    float f[8];
};

struct HelperCase {
    int which;
    bool original;
    HelperState *s;
};

void RunHelper(void *context) {
    HelperCase *c = static_cast<HelperCase *>(context);
    HelperState *s = c->s;
    Transform *m = &s->matrix;
    ActIK ik = {};
    switch (c->which) {
    case 0:
        if (c->original) OrigTransformVector(m, 0, s->in, s->vec);
        else m->TransformVector(s->in, s->vec);
        break;
    case 1:
        if (c->original) OrigTransformVector(m, 0, s->in, s->in);
        else m->TransformVector(s->in, s->in);
        break;
    case 2:
        if (c->original) OrigOrthoInverse(m, 0, &s->out);
        else m->GetOrthoInverse(&s->out);
        break;
    case 3:
        if (c->original) OrigBuildQT(m, 0, s->f[0], s->f[1], s->f[2], s->f[3], s->f[4], s->f[5], s->f[6]);
        else m->BuildQT(s->f[0], s->f[1], s->f[2], s->f[3], s->f[4], s->f[5], s->f[6]);
        break;
    case 4:
        if (c->original) OrigBuildRotation(m, 0, s->f[0], s->f[1], s->f[2], s->f[3]);
        else m->BuildRotation(s->f[0], s->f[1], s->f[2], s->f[3]);
        break;
    case 5:
        s->result = c->original ? OrigAngleBetween(s->in, s->in2) : AngleBetweenVectors(s->in, s->in2);
        break;
    case 6:
        if (c->original) OrigPerpendicular(s->in, s->vec);
        else PerpendicularVector(s->in, s->vec);
        break;
    case 7:
        if (c->original) OrigAxisAngle(s->in, s->f[0], s->vec);
        else QuatFromAxisAngle(s->in, s->f[0], s->vec);
        break;
    case 8:
        if (c->original) OrigQuatFromTo(s->in, s->in2, s->vec);
        else QuatFromTo(s->in, s->in2, s->vec);
        break;
    case 9:
        if (c->original) OrigQuatProduct(s->in, s->in2, s->vec);
        else QuatProduct(s->in, s->in2, s->vec);
        break;
    case 10:
        s->result = c->original ? OrigJointAngle(&ik, 0, s->f[0], s->f[1], s->in)
                                : ik.JointAngle(s->f[0], s->f[1], s->in);
        break;
    }
}

const char *const kHelperNames[] = { "TransformVector", "TransformVector in place", "GetOrthoInverse", "BuildQT",
                                     "BuildRotation", "AngleBetweenVectors", "PerpendicularVector",
                                     "QuatFromAxisAngle", "QuatFromTo", "QuatProduct", "JointAngle" };

void TestHelpers() {
    for (int which = 0; which < 11; which++) {
        for (int n = 0; n < kCases; n++) {
            HelperState input;
            Fill(&input, sizeof(input));
            RigidMatrix(&input.matrix, 3.0f);
            RandomVector(input.in, 4, 2.0f);
            RandomVector(input.in2, 4, 2.0f);
            for (int i = 0; i < 8; i++)
                input.f[i] = Uniform(2.0f);
            switch (which) {
            case 3:                                     // a unit quaternion and a translation
                UnitQuat(input.f);
                break;
            case 4:                                     // degrees (0 now and then) and an axis
                input.f[0] = n % 10 == 0 ? 0.0f : Uniform(400.0f);
                break;
            case 7:
                input.f[0] = n % 10 == 0 ? 3.14159274f : Uniform(7.0f);
                break;
            case 8:                                     // parallel, opposite, or anything
                if (n % 5 == 1)
                    for (int i = 0; i < 3; i++)
                        input.in2[i] = input.in[i] * 1.5f;
                else if (n % 5 == 2)
                    for (int i = 0; i < 3; i++)
                        input.in2[i] = -input.in[i] * 0.7f;
                break;
            case 9:
                UnitQuat(input.in);
                UnitQuat(input.in2);
                break;
            case 10:                                    // lengths, and a target near and beyond their reach
                input.f[0] = fabsf(input.f[0]) + 0.1f;
                input.f[1] = fabsf(input.f[1]) + 0.1f;
                if (n % 4 == 0) {
                    double reach = (n % 8 == 0) ? input.f[0] + input.f[1] : fabs(input.f[0] - input.f[1]);
                    input.in[0] = float(reach);
                    input.in[1] = 0.0f;
                    input.in[2] = 0.0f;
                }
                break;
            }
            HelperState port, original;
            memcpy(&port, &input, sizeof(input));
            memcpy(&original, &input, sizeof(input));
            g_cases++;
            HelperCase pc = { which, false, &port };
            HelperCase oc = { which, true, &original };
            Guard(RunHelper, &pc);
            {
                OriginalRange scope;
                Guard(RunHelper, &oc);
            }
            Check(kHelperNames[which], &port, &original, sizeof(HelperState));
        }
    }
}

// ---- ActIK::Init and ActIK::Solve, ActIKSolver::BlendMatrices

struct ChainState {
    alignas(16) Transform global;
    alignas(16) Transform upper;
    alignas(16) Transform middle;
    alignas(16) Transform lower;
    float target[4];
    float pole[4];
    bool usePole;
    ActIK ik;
    float weight;
};

struct ChainCase {
    int which;
    bool original;
    ChainState *s;
    Skeleton *skeleton;
    int bone;
    Coord4 axis;
};

void RunChain(void *context) {
    ChainCase *c = static_cast<ChainCase *>(context);
    ChainState *s = c->s;
    ActIKSolver solver = {};
    ActGlobalPoseOverrideArray overrides = {};
    const float *pole = s->usePole ? s->pole : NULL;
    switch (c->which) {
    case 0:
        if (c->original) OrigIKInit(&s->ik, 0, c->skeleton, c->bone, &c->axis);
        else s->ik.Init(c->skeleton, c->bone, &c->axis);
        break;
    case 1:
        if (c->original) OrigIKSolve(&s->ik, 0, &s->global, s->target, &s->upper, &s->middle, &s->lower, pole);
        else s->ik.Solve(&s->global, s->target, &s->upper, &s->middle, &s->lower, pole);
        break;
    case 2:
        if (c->original) OrigSolverBlend(&solver, 0, &s->upper, &s->middle, s->weight);
        else solver.BlendMatrices(&s->upper, &s->middle, s->weight);
        break;
    case 3:
        if (c->original) OrigOverrideBlend(&overrides, 0, &s->upper, &s->middle, s->weight);
        else overrides.BlendMatrices(&s->upper, &s->middle, s->weight);
        break;
    }
}

const char *const kChainNames[] = { "ActIK::Init", "ActIK::Solve", "ActIKSolver::BlendMatrices",
                                    "ActGlobalPoseOverrideArray::BlendMatrices" };

void TestChain() {
    Skeleton *skeleton = FakeSkeleton();
    for (int which = 0; which < 4; which++) {
        for (int n = 0; n < kCases; n++) {
            ChainState input;
            memset(&input, 0, sizeof(input));
            RigidMatrix(&input.global, 3.0f);
            RigidMatrix(&input.upper, 1.0f);
            RigidMatrix(&input.middle, 1.0f);
            RigidMatrix(&input.lower, 1.0f);
            RandomVector(input.target, 3, 3.0f);
            input.target[3] = 1.0f;
            RandomVector(input.pole, 3, 1.0f);
            input.usePole = n % 3 != 0;
            input.weight = RandomWeight();
            input.ik.skeleton = skeleton;
            input.ik.bones[0] = 0;
            input.ik.bones[1] = 1;
            input.ik.bones[2] = 2;
            input.ik.upperLength = fabsf(Uniform(1.5f)) + 0.05f;
            input.ik.lowerLength = fabsf(Uniform(1.5f)) + 0.05f;
            float axis[4];
            UnitQuat(axis);
            input.ik.axis = { axis[0], axis[1], axis[2], 0.0f };
            ChainState port, original;
            memcpy(&port, &input, sizeof(input));
            memcpy(&original, &input, sizeof(input));
            ChainCase pc = { which, false, &port, skeleton, int(Next() % (kBones - 2)), { axis[0], axis[1], axis[2],
                                                                                          axis[3] } };
            ChainCase oc = pc;
            oc.original = true;
            oc.s = &original;
            g_cases++;
            Guard(RunChain, &pc);
            {
                OriginalRange scope;
                Guard(RunChain, &oc);
            }
            Check(kChainNames[which], &port, &original, sizeof(ChainState));
        }
    }
}

// ---- ActIKSolverArray: Solve, SetInfo, the constructor

struct SolverState {
    alignas(16) Transform globals[kBones];
    alignas(16) Transform locals[kBones];
    ActIKSolver solver;
    ActIK ik;
    ActIKSolver *solvers[1];
    ActIKSolverArray array;
    ActIKSolveInfo info;
};

struct SolverParams {
    uint32_t seed;
    int bone;
    int referenceBone;
    bool transformTarget;
    bool active;
};

void BuildSolver(SolverState *s, const SolverParams *p) {
    uint32_t saved = g_seed;
    g_seed = p->seed;
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < kBones; i++) {
        RigidMatrix(&s->globals[i], 3.0f);
        RigidMatrix(&s->locals[i], 1.0f);
    }
    s->ik.skeleton = NULL;
    s->ik.bones[0] = p->bone;
    s->ik.bones[1] = p->bone + 1;
    s->ik.bones[2] = p->bone + 2;
    s->ik.upperLength = fabsf(Uniform(1.5f)) + 0.05f;
    s->ik.lowerLength = fabsf(Uniform(1.5f)) + 0.05f;
    float axis[4];
    UnitQuat(axis);
    s->ik.axis = { axis[0], axis[1], axis[2], 0.0f };
    s->solver.ik = &s->ik;
    s->solver.referenceBone = p->referenceBone;
    s->solver.bone = p->bone;
    s->solver.transformTarget = p->transformTarget;
    s->solver.active = p->active;
    RandomVector(&s->info.target.x, 3, 3.0f);
    s->info.target.w = 1.0f;
    RandomVector(&s->info.pole.x, 3, 1.0f);
    s->info.weight = RandomWeight();
    s->info.usePole = Next() % 3 != 0;
    s->solver.info = s->info;
    Fill(&s->info, sizeof(s->info));
    s->solvers[0] = &s->solver;
    s->array.count = 1;
    s->array.solvers = s->solvers;
    s->array.referenceBone = p->referenceBone;
    s->array.globals = s->globals;
    s->array.locals = s->locals;
    g_seed = saved;
}

struct SolverCase {
    int which;
    bool original;
    SolverState *s;
    const SolverParams *p;
};

void RunSolver(void *context) {
    SolverCase *c = static_cast<SolverCase *>(context);
    SolverState *s = c->s;
    switch (c->which) {
    case 0:
        if (c->original) OrigSolverArraySolve(&s->array, 0, 0);
        else s->array.Solve(0);
        break;
    case 1: {
        ActIKSolveInfo info = s->solver.info;
        info.weight = 0.75f;
        s->solver.active = false;
        if (c->original) OrigSetInfo(&s->array, 0, 0, &info);
        else s->array.SetInfo(0, &info);
        break;
    }
    case 2:
        if (c->original) OrigSolverArrayConstruct(&s->array, 0, c->p->referenceBone, NULL, s->globals, s->locals);
        else s->array.Construct(c->p->referenceBone, NULL, s->globals, s->locals);
        break;
    }
}

const char *const kSolverNames[] = { "ActIKSolverArray::Solve", "ActIKSolverArray::SetInfo",
                                     "ActIKSolverArray::Construct" };

void TestSolvers() {
    for (int which = 0; which < 3; which++) {
        for (int n = 0; n < kCases; n++) {
            SolverParams p;
            p.seed = Next() | 1;
            p.bone = int(Next() % (kBones - 2));
            p.referenceBone = int(Next() % kBones);
            p.transformTarget = n % 2 != 0;
            p.active = n % 7 != 0;
            static SolverState port, original;
            BuildSolver(&port, &p);
            BuildSolver(&original, &p);
            SolverCase pc = { which, false, &port, &p };
            SolverCase oc = { which, true, &original, &p };
            g_cases++;
            Guard(RunSolver, &pc);
            {
                OriginalRange scope;
                Guard(RunSolver, &oc);
            }
            Check(kSolverNames[which], port.globals, original.globals, sizeof(port.globals));
            Check(kSolverNames[which], port.locals, original.locals, sizeof(port.locals));
            Check(kSolverNames[which], &port.ik, &original.ik, sizeof(ActIK));
            Check(kSolverNames[which], &port.solver.referenceBone, &original.solver.referenceBone,
                  sizeof(ActIKSolver) - offsetof(ActIKSolver, referenceBone));
            Check(kSolverNames[which], &port.array.count, &original.array.count, sizeof(int32_t));
            Check(kSolverNames[which], &port.array.referenceBone, &original.array.referenceBone,
                  sizeof(int32_t));
            Check(kSolverNames[which], &port.array.skeleton, &original.array.skeleton, sizeof(void *));
            bool pointers = (port.array.globals == port.globals) == (original.array.globals == original.globals) &&
                            (port.array.locals == port.locals) == (original.array.locals == original.locals) &&
                            (port.array.solvers == NULL) == (original.array.solvers == NULL);
            const bool same = true;
            Check(kSolverNames[which], &pointers, &same, 1);
        }
    }
}

// ---- CreateIKs, DeleteIKs and the destructor (allocations compared by content)

struct CreateCase {
    bool original;
    bool destruct;
    ActIKSolverArray *array;
    int count;
    const int *bones;
    const Coord4 *axes;
    const bool *flags;
};

void RunCreate(void *context) {
    CreateCase *c = static_cast<CreateCase *>(context);
    if (c->original) OrigCreateIKs(c->array, 0, c->count, c->bones, c->axes, c->flags);
    else c->array->CreateIKs(c->count, c->bones, c->axes, c->flags);
}

void RunDelete(void *context) {
    CreateCase *c = static_cast<CreateCase *>(context);
    if (c->destruct) {
        if (c->original) OrigSolverArrayDestruct(c->array, 0);
        else c->array->Destruct();
    } else {
        if (c->original) OrigDeleteIKs(c->array, 0);
        else c->array->DeleteIKs();
    }
}

void TestCreateIKs() {
    Skeleton *skeleton = FakeSkeleton();
    for (int n = 0; n < 50; n++) {
        int count = int(Next() % 4);
        int bones[4];
        Coord4 axes[4];
        bool flags[4];
        for (int i = 0; i < 4; i++) {
            bones[i] = int(Next() % (kBones - 2));
            UnitQuat(&axes[i].x);
            flags[i] = Next() % 2 != 0;
        }
        ActIKSolverArray port = {}, original = {};
        port.referenceBone = original.referenceBone = int(Next() % kBones);
        port.skeleton = original.skeleton = skeleton;
        CreateCase pc = { false, n % 2 != 0, &port, count, bones, axes, flags };
        CreateCase oc = pc;
        oc.original = true;
        oc.array = &original;
        g_cases++;
        Guard(RunCreate, &pc);
        {
            OriginalRange scope;
            Guard(RunCreate, &oc);
        }
        Check("CreateIKs count", &port.count, &original.count, sizeof(int32_t));
        for (int i = 0; i < count && i < port.count && i < original.count; i++) {
            ActIKSolver *a = port.solvers[i], *b = original.solvers[i];
            Check("CreateIKs solver", &a->referenceBone, &b->referenceBone,
                  sizeof(ActIKSolver) - offsetof(ActIKSolver, referenceBone));
            Check("CreateIKs solver skeleton", &a->skeleton, &b->skeleton, sizeof(void *));
            Check("CreateIKs ik", a->ik, b->ik, sizeof(ActIK));
        }
        Guard(RunDelete, &pc);
        {
            OriginalRange scope;
            Guard(RunDelete, &oc);
        }
        Check("DeleteIKs", &port, &original, sizeof(ActIKSolverArray));
    }
}

// ---- ActGlobalPoseOverrideArray

constexpr int kOverrides = 4;

struct OverrideState {
    alignas(16) Transform globals[kBones];
    alignas(16) Transform locals[kBones];
    alignas(16) Transform matrices[kOverrides];
    alignas(16) Transform given[kOverrides];
    int32_t bones[kOverrides];
    float weights[kOverrides];
    float givenWeights[kOverrides];
    bool set[kOverrides];
    ActGlobalPoseOverrideArray array;
};

struct OverrideParams {
    uint32_t seed;
    bool local;
    bool mirrored;
    int index;
};

void BuildOverrides(OverrideState *s, const OverrideParams *p) {
    uint32_t saved = g_seed;
    g_seed = p->seed;
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < kBones; i++) {
        RigidMatrix(&s->globals[i], 3.0f);
        RigidMatrix(&s->locals[i], 1.0f);
    }
    for (int i = 0; i < kOverrides; i++) {
        RigidMatrix(&s->matrices[i], 1.0f);
        RigidMatrix(&s->given[i], 1.0f);
        s->bones[i] = int(Next() % kBones);
        s->weights[i] = RandomWeight();
        s->givenWeights[i] = RandomWeight();
        s->set[i] = Next() % 4 != 0;
    }
    s->array.count = kOverrides;
    s->array.bones = s->bones;
    s->array.matrices = s->matrices;
    s->array.weights = s->weights;
    s->array.globals = s->globals;
    s->array.local = p->local;
    s->array.set = s->set;
    g_seed = saved;
}

struct OverrideCase {
    int which;
    bool original;
    OverrideState *s;
    const OverrideParams *p;
};

void RunOverride(void *context) {
    OverrideCase *c = static_cast<OverrideCase *>(context);
    OverrideState *s = c->s;
    ActGlobalPoseOverrideArray *a = &s->array;
    switch (c->which) {
    case 0:
        if (c->original) OrigSetOverride(a, 0, c->p->index, &s->given[0], s->givenWeights[0]);
        else a->SetGlobalPoseOverride(c->p->index, &s->given[0], s->givenWeights[0]);
        break;
    case 1:
        if (c->original) OrigSetOverrides(a, 0, a->count, s->given, s->givenWeights);
        else a->SetGlobalPoseOverrides(a->count, s->given, s->givenWeights);
        break;
    case 2:
        if (c->original) OrigDoOverrides(a, 0, c->p->mirrored, s->locals);
        else a->DoGlobalOverrides(c->p->mirrored, s->locals);
        break;
    case 3:
        if (c->original) OrigOverrideConstruct(a, 0, s->globals);
        else a->Construct(s->globals);
        break;
    }
}

const char *const kOverrideNames[] = { "SetGlobalPoseOverride", "SetGlobalPoseOverrides", "DoGlobalOverrides",
                                       "ActGlobalPoseOverrideArray::Construct" };

void TestOverrides() {
    for (int which = 0; which < 4; which++) {
        for (int n = 0; n < kCases; n++) {
            OverrideParams p;
            p.seed = Next() | 1;
            p.local = n % 2 != 0;
            p.mirrored = (n / 2) % 2 != 0;
            p.index = n % kOverrides;
            static OverrideState port, original;
            BuildOverrides(&port, &p);
            BuildOverrides(&original, &p);
            OverrideCase pc = { which, false, &port, &p };
            OverrideCase oc = { which, true, &original, &p };
            g_cases++;
            Guard(RunOverride, &pc);
            {
                OriginalRange scope;
                Guard(RunOverride, &oc);
            }
            Check(kOverrideNames[which], &port, &original, offsetof(OverrideState, array));
            Check(kOverrideNames[which], &port.array.count, &original.array.count, sizeof(int32_t));
            Check(kOverrideNames[which], &port.array.local, &original.array.local, 1);
        }
    }

    // Create and destroy, by content
    for (int n = 0; n < 50; n++) {
        int count = int(Next() % 5);
        int bones[4];
        for (int i = 0; i < 4; i++)
            bones[i] = int(Next() % kBones);
        bool local = n % 2 != 0;
        ActGlobalPoseOverrideArray port = {}, original = {};
        g_cases++;
        port.CreateGlobalPoseOverrides(count, bones, local);
        {
            OriginalRange scope;
            OrigOverrideCreate(&original, 0, count, bones, local);
        }
        Check("CreateGlobalPoseOverrides count", &port.count, &original.count, sizeof(int32_t));
        Check("CreateGlobalPoseOverrides local", &port.local, &original.local, 1);
        if (port.count == original.count && count > 0) {
            Check("CreateGlobalPoseOverrides bones", port.bones, original.bones, count * sizeof(int32_t));
            Check("CreateGlobalPoseOverrides set", port.set, original.set, count);
        }
        port.Destruct();
        {
            OriginalRange scope;
            OrigOverrideDestruct(&original, 0);
        }
        Check("ActGlobalPoseOverrideArray::Destruct", &port.count, &original.count, sizeof(int32_t));
    }
}

// ---- ActEvents over the live resolver

// Which of the object's four handlers an entry is (-1: none of them)
int HandlerIndex(const ActEvents *e, const EventHandler *h) {
    if (h == e->defaultHandler) return 0;
    if (h == e->dropWeapon) return 1;
    if (h == e->physicsOff) return 2;
    if (h == e->fire) return 3;
    return -1;
}

void TestEvents() {
    ActEventResolver *resolver = LiveEventResolver;
    if (resolver == NULL || resolver->target == NULL) {
        printf("[ikshadow] no event resolver: ActEvents not tested\n");
        return;
    }
    for (int n = 0; n < 2; n++) {
        ActEvents port, original;
        memset(&port, 0xcd, sizeof(port));
        memset(&original, 0xcd, sizeof(original));
        g_cases++;
        port.Construct(resolver);
        {
            OriginalRange scope;
            OrigEventsConstruct(&original, 0, resolver);
        }
        int a[ActEvents::kMaxEvents], b[ActEvents::kMaxEvents];
        for (int i = 0; i < ActEvents::kMaxEvents; i++) {
            bool filled = i < resolver->target->count;
            a[i] = filled ? HandlerIndex(&port, port.handlers[i]) : -2;
            b[i] = filled ? HandlerIndex(&original, original.handlers[i]) : -2;
        }
        Check("ActEvents handlers", a, b, sizeof(a));
        Check("ActEvents untouched entries", &port.handlers[resolver->target->count],
              &original.handlers[resolver->target->count],
              (ActEvents::kMaxEvents - resolver->target->count) * sizeof(void *));
        EventHandler *ph[4] = { port.defaultHandler, port.dropWeapon, port.physicsOff, port.fire };
        EventHandler *oh[4] = { original.defaultHandler, original.dropWeapon, original.physicsOff, original.fire };
        for (int i = 0; i < 4; i++) {
            Check("ActEvents handler object", ph[i], oh[i], sizeof(EventHandler));
            OperatorDelete(ph[i]);
            OperatorDelete(oh[i]);
        }
        Check("ActEvents currentActor", &port.currentActor, &original.currentActor, sizeof(void *));
    }
}

} // namespace

void IKShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_IKSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    TestHelpers();
    TestChain();
    TestSolvers();
    TestCreateIKs();
    TestOverrides();
    TestEvents();
    printf("[ikshadow] IK and events: %ld cases, %ld checks, %ld differ (%ld NaN-only, %ld faults)\n", g_cases,
           g_checks, g_differ, g_nan, g_faults);
    fflush(stdout);
}
