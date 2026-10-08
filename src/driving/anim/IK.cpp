#pragma fp_contract(off)

#include "IK.h"

#include "Skeleton.h"                   // ActSkeleton::BlendQ, BlendQT

#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"

#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// ActIK, ActIKSolver, ActIKSolverArray, ActGlobalPoseOverrideArray and the vector and quaternion helpers compiled
// beside them (the Transform methods and the two helpers EAGL shares are in eagl/Transform.cpp). See IK.h.
//
// The x87 arithmetic is written in double in the original's order with a float at every store; FSIN, FCOS, FPATAN
// and the C runtime's arc cosine are the original instructions (small helpers below), their results unrounded.
// ---------------------------------------------------------------------------------------------------------------


namespace {

constexpr float kPi = 3.14159274f;
constexpr float kOneOver180 = 0.0055555557f;
constexpr float kQuatEpsilon = 9.99999975e-06f;
constexpr float kCosineLimit = 0.99f;
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb && std::bit_cast<uint32_t>(kOneOver180) == 0x3bb60b61 &&
              std::bit_cast<uint32_t>(kQuatEpsilon) == 0x3727c5ac &&
              std::bit_cast<uint32_t>(kCosineLimit) == 0x3f7d70a4, "the original's constants");

// FSIN, FCOS and FPATAN (atan2(y, x)) as the original uses them
__declspec(naked) double X87Sin(double) {
    __asm {
        fld qword ptr [esp + 4]
        fsin
        ret
    }
}

__declspec(naked) double X87Cos(double) {
    __asm {
        fld qword ptr [esp + 4]
        fcos
        ret
    }
}

__declspec(naked) double X87Atan2(double, double) {
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        fpatan
        ret
    }
}

// The C runtime's arc cosine (_CIacos at 0x001328d0, the game's own), which takes and answers on the x87 stack
__declspec(naked) double CrtAcos(float) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x001328d0
        call eax
        ret
    }
}

Coord4& Row(Transform *matrix, int row) {
    return *reinterpret_cast<Coord4 *>(&matrix->m[row * 4]);
}

void Negate(Coord4 *v) {
    v->x *= -1.0f;
    v->y *= -1.0f;
    v->z *= -1.0f;
    v->w *= -1.0f;
}

void NegateXYZ(Coord4 *v) {
    v->x = -v->x;
    v->y = -v->y;
    v->z = -v->z;
}

void Swap(Coord4 *a, Coord4 *b) {
    Coord4 t = *a;
    *a = *b;
    *b = t;
}

} // namespace

// ---- vectors and quaternions

// FUNC_AT(0x00016590)
void PerpendicularVector(const float *in, float *out) {
    float ax = fabsf(in[0]);
    float ay = fabsf(in[1]);
    float az = fabsf(in[2]);
    if (ax > ay && ax > az) {
        out[0] = in[2] + in[1];
        out[1] = -in[0];
        out[2] = -in[0];
    } else if (ay > ax && ay > az) {
        out[1] = in[2] + in[0];
        out[2] = -in[1];
        out[0] = -in[1];
    } else {
        out[2] = in[1] + in[0];
        out[0] = -in[2];
        out[1] = -in[2];
    }
}

// The sine takes the half angle unrounded, the cosine rounded to a float.
// FUNC_AT(0x00016640)
void QuatFromAxisAngle(const float *axis, float radians, float *out) {
    double half = radians * 0.5;
    double s = X87Sin(half);
    double scale = s / sqrt(double(axis[0]) * axis[0] + double(axis[1]) * axis[1] + double(axis[2]) * axis[2]);
    out[0] = float(scale * axis[0]);
    out[1] = float(scale * axis[1]);
    out[2] = float(scale * axis[2]);
    out[3] = float(X87Cos(float(half)));
}

// FUNC_AT(0x000166a0)
void QuatFromTo(const float *from, const float *to, float *out) {
    double fromScale = 1.0 / sqrt(double(from[0]) * from[0] + double(from[1]) * from[1] + double(from[2]) * from[2]);
    float u[3] = { float(fromScale * from[0]), float(fromScale * from[1]), float(fromScale * from[2]) };
    double toScale = 1.0 / sqrt(double(to[0]) * to[0] + double(to[1]) * to[1] + double(to[2]) * to[2]);
    double vz = toScale * to[2];
    float v[3] = { float(toScale * to[0]), float(toScale * to[1]), float(vz) };
    double dot = vz * u[2] + double(v[0]) * u[0] + double(v[1]) * u[1];
    if (dot > 1.0 - kQuatEpsilon) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = 1.0f;
    } else if (dot < kQuatEpsilon - 1.0) {
        float axis[3];
        PerpendicularVector(u, axis);
        QuatFromAxisAngle(axis, kPi, out);
    } else {
        float axis[3] = { float(double(v[2]) * u[1] - double(v[1]) * u[2]),
                          float(double(u[2]) * v[0] - double(v[2]) * u[0]),
                          float(double(v[1]) * u[0] - double(v[0]) * u[1]) };
        QuatFromAxisAngle(axis, float(AngleBetweenVectors(u, v)), out);
    }
}

// ---- ActIK

// FUNC_AT(0x00016980)
void ActIK::Init(Skeleton *skeleton, int bone, const Coord4 *axis) {
    this->axis = *axis;
    bones[0] = bone;
    bones[1] = bone + 1;
    this->skeleton = skeleton;
    bones[2] = bone + 2;
    float pose[12];
    skeleton->GetStillPoseBone(bones[1], pose);
    upperLength = float(sqrt(double(pose[10]) * pose[10] + double(pose[9]) * pose[9] + double(pose[8]) * pose[8]));
    skeleton->GetStillPoseBone(bones[2], pose);
    lowerLength = float(sqrt(double(pose[10]) * pose[10] + double(pose[9]) * pose[9] + double(pose[8]) * pose[8]));
}

// FUNC_AT(0x000168b0)
double ActIK::JointAngle(float upper, float lower, const float *target) {
    double reach = double(target[0]) * target[0] + double(target[1]) * target[1] + double(target[2]) * target[2];
    float cosine = float((double(upper) * upper + double(lower) * lower - reach) / (double(upper) * lower * -2.0));
    if (cosine < -kCosineLimit)
        return kPi;
    if (cosine > kCosineLimit)
        return 0.0;
    return CrtAcos(cosine);
}

// upper (the chain's first bone, local) is turned so the chain reaches target, with middle bent by the angle
// that makes the two bones' lengths span it; given a pole, the chain is then twisted about the line to the
// target so that the bend axis points away from the pole.
// FUNC_AT(0x00016a30)
void ActIK::Solve(const Transform *global, const float *target, Transform *upper, Transform *middle,
                  const Transform *lower, const float *pole) {
    constexpr double kDegreesPerRadian = 180.0 / double(kPi);
    Transform inverse;
    global->GetOrthoInverse(&inverse);
    float localTarget[3];
    inverse.TransformPoint(target, localTarget);
    float degrees = float(JointAngle(upperLength, lowerLength, localTarget) * kDegreesPerRadian);
    middle->BuildRotation(degrees, axis.x, axis.y, axis.z);

    float end[3] = { lower->m[12], lower->m[13], lower->m[14] };
    float reached[3];
    middle->TransformPoint(end, reached);
    float swing[4];
    QuatFromTo(reached, localTarget, swing);
    Transform rotation;
    rotation.BuildQT(swing[0], swing[1], swing[2], swing[3], 0.0f, 0.0f, 0.0f);
    if (pole != NULL) {
        float localPole[3];
        inverse.TransformVector(pole, localPole);
        float bend[3];
        rotation.TransformVector(&axis.x, bend);
        float twist[4];
        QuatFromAxisAngle(reached, -float(AngleBetweenVectors(bend, localPole)), twist);
        float q[4];
        QuatProduct(twist, swing, q);
        rotation.BuildQT(q[0], q[1], q[2], q[3], 0.0f, 0.0f, 0.0f);
    }
    upper->PrependMatrix(rotation.m);
}

// ---- ActIKSolver

// Inlined in ActIKSolverArray::CreateIKs
ActIKSolver* ActIKSolver::Construct(Skeleton *skeleton, int referenceBone, int bone, bool transformTarget,
                                    const Coord4 *axis) {
    this->skeleton = skeleton;
    this->referenceBone = referenceBone;
    this->bone = bone;
    this->transformTarget = transformTarget;
    active = false;
    ActIK *chain = static_cast<ActIK *>(OperatorNew(sizeof(ActIK)));
    ik = chain != NULL ? chain->Construct() : NULL;
    ik->Init(this->skeleton, this->bone, axis);
    return this;
}

// matrix's rotation and translation moved towards other's by weight (scale 1).
// FUNC_AT(0x00015900)
void ActIKSolver::BlendMatrices(Transform *matrix, const Transform *other, float weight) {
    // XBE_GLOBAL(0x001b49b0, 16)
    alignas(16) static const Coord4 scale = { 1.0f, 1.0f, 1.0f, 1.0f };
    // XBE_GLOBAL(0x001b49c0, 16)
    alignas(16) static Coord4 otherTranslation;
    // XBE_GLOBAL(0x001b49d0, 16)
    alignas(16) static Coord4 translation;
    // XBE_GLOBAL(0x001b49e0, 16)
    alignas(16) static Coord4 otherRotation;
    // XBE_GLOBAL(0x001b49f0, 16)
    alignas(16) static Coord4 rotation;
    VU0_m4toquat(&rotation, matrix);
    VU0_m4toquat(&otherRotation, other);
    translation = { matrix->m[12], matrix->m[13], matrix->m[14], 1.0f };
    otherTranslation = { other->m[12], other->m[13], other->m[14], 1.0f };
    ActSkeleton::BlendQT(&rotation, &translation, &otherRotation, &otherTranslation, weight);
    VU0_SQTquattom4(&scale, &rotation, &translation, matrix);
}

// FUNC_AT(0x00016d80)
void ActIKSolver::Solve(Transform *globals, Transform *locals) {
    if (!active)
        return;
    if (transformTarget) {
        Transform reference = globals[referenceBone];
        VU0_MATRIX4_vect4mult(&info.target, &reference, &info.target);
    }
    Transform upper = locals[bone];
    Transform middle = locals[bone + 1];
    ik->Solve(&globals[ik->bones[0]], &info.target.x, &locals[ik->bones[0]], &locals[ik->bones[1]],
              &locals[ik->bones[2]], info.usePole ? &info.pole.x : NULL);
    if (info.weight < 1.0f) {
        BlendMatrices(&locals[bone], &upper, 1.0f - info.weight);
        BlendMatrices(&locals[bone + 1], &middle, 1.0f - info.weight);
    }
}

// ---- ActIKSolverArray

// FUNC_AT(0x000159b0)
ActIKSolverArray* ActIKSolverArray::Construct(int referenceBone, Skeleton *skeleton, Transform *globals,
                                              Transform *locals) {
    this->referenceBone = referenceBone;
    this->globals = globals;
    count = 0;
    solvers = NULL;
    this->locals = locals;
    this->skeleton = skeleton;
    return this;
}

// FUNC_AT(0x000159e0)
void ActIKSolverArray::SetInfo(int index, const ActIKSolveInfo *info) {
    ActIKSolver *solver = solvers[index];
    solver->active = true;
    solver->info = *info;
}

// FUNC_AT(0x00016c30)
void ActIKSolverArray::CreateIKs(int count, const int *bones, const Coord4 *axes, const bool *transformTargets) {
    this->count = count;
    solvers = static_cast<ActIKSolver **>(OperatorNewArray(count * sizeof(ActIKSolver *)));
    for (int i = 0; i < this->count; i++) {
        ActIKSolver *solver = static_cast<ActIKSolver *>(OperatorNew(sizeof(ActIKSolver)));
        if (solver != NULL)
            solver->Construct(skeleton, referenceBone, bones[i], transformTargets[i], &axes[i]);
        solvers[i] = solver;
    }
}

// FUNC_AT(0x00016d20)
void ActIKSolverArray::DeleteIKs() {
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            ActIKSolver *solver = solvers[i];
            if (solver != NULL) {
                if (solver->ik != NULL)
                    OperatorDelete(solver->ik);
                OperatorDelete(solver);
            }
        }
        OperatorDelete(solvers);
    }
    solvers = NULL;
}

// FUNC_AT(0x00016e90)
void ActIKSolverArray::Destruct() {
    DeleteIKs();
}

// FUNC_AT(0x00016ea0)
void ActIKSolverArray::Solve(int index) {
    solvers[index]->Solve(globals, locals);
}

// ---- ActGlobalPoseOverrideArray

// FUNC_AT(0x00015a10)
ActGlobalPoseOverrideArray* ActGlobalPoseOverrideArray::Construct(Transform *globals) {
    count = 0;
    this->globals = globals;
    return this;
}

// FUNC_AT(0x00015a30)
void ActGlobalPoseOverrideArray::CreateGlobalPoseOverrides(int count, const int *bones, bool local) {
    this->count = count;
    this->bones = static_cast<int32_t *>(OperatorNewArray(this->count * sizeof(int32_t)));
    matrices = static_cast<Transform *>(OperatorNewArray(this->count * sizeof(Transform)));
    weights = static_cast<float *>(OperatorNewArray(this->count * sizeof(float)));
    for (int i = 0; i < this->count; i++)
        this->bones[i] = bones[i];
    this->local = local;
    set = static_cast<bool *>(OperatorNewArray(this->count));
    for (int i = 0; i < this->count; i++)
        set[i] = false;
}

// matrix's rotation turned towards other's by weight; the translation kept (scale 1).
// FUNC_AT(0x00015ac0)
void ActGlobalPoseOverrideArray::BlendMatrices(Transform *matrix, const Transform *other, float weight) {
    // XBE_GLOBAL(0x001b4a00, 16)
    alignas(16) static const Coord4 scale = { 1.0f, 1.0f, 1.0f, 1.0f };
    // XBE_GLOBAL(0x001b4a10, 16)
    alignas(16) static Coord4 translation;
    // XBE_GLOBAL(0x001b4a20, 16)
    alignas(16) static Coord4 otherRotation;
    // XBE_GLOBAL(0x001b4a30, 16)
    alignas(16) static Coord4 rotation;
    VU0_m4toquat(&rotation, matrix);
    VU0_m4toquat(&otherRotation, other);
    translation = { matrix->m[12], matrix->m[13], matrix->m[14], 1.0f };
    ActSkeleton::BlendQ(&rotation, &otherRotation, weight);
    VU0_SQTquattom4(&scale, &rotation, &translation, matrix);
}

// A global override's rows are rearranged into the skeleton's axes: rows 0 and 2 swapped, and for the first two
// overrides one row negated and rows 1 and 2 swapped as well.
// FUNC_AT(0x00015b40)
void ActGlobalPoseOverrideArray::SetGlobalPoseOverride(int index, const Transform *matrix, float weight) {
    Transform *override = &matrices[index];
    *override = *matrix;
    set[index] = true;
    if (!local) {
        Swap(&Row(override, 0), &Row(override, 2));
        if (index == 0) {
            Negate(&Row(override, 2));
            Swap(&Row(override, 1), &Row(override, 2));
        } else if (index == 1) {
            Negate(&Row(override, 0));
            Swap(&Row(override, 1), &Row(override, 2));
        }
    }
    weights[index] = weight;
}

// FUNC_AT(0x00015d00)
void ActGlobalPoseOverrideArray::SetGlobalPoseOverrides(int unusedCount, const Transform *matrices, const float *weights) {
    for (int i = 0; i < count; i++)
        SetGlobalPoseOverride(i, &matrices[i], weights[i]);
}

// Local overrides take the bone's translation and replace (or blend into) its local matrix. Global ones replace
// the orientation of the bone's global matrix (blended by the weight, with the first rows' x, y and z negated
// around the blend when mirrored) and the local matrix is made again from it, keeping its translation.
// FUNC_AT(0x00015d40)
void ActGlobalPoseOverrideArray::DoGlobalOverrides(bool mirrored, Transform *locals) {
    if (local) {
        for (int i = 0; i < count; i++) {
            if (!set[i])
                continue;
            VU0_v4copy(&locals[bones[i]].m[12], &matrices[i].m[12]);
            if (0.0f < weights[i] && weights[i] < 1.0f)
                BlendMatrices(&locals[bones[i]], &matrices[i], weights[i]);
            else if (weights[i] >= 1.0f)
                MatrixCopy(&matrices[i], &locals[bones[i]]);
        }
        return;
    }
    for (int i = 0; i < count; i++) {
        if (!set[i])
            continue;
        Transform relative = locals[bones[i]];
        Coord4 translation = Row(&relative, 3);
        OrthoInverse(&relative);
        VU0_MATRIX4_mult(&relative, &relative, &globals[bones[i]]);
        OrthoInverse(&relative);
        if (weights[i] < 1.0f) {
            if (mirrored) {
                NegateXYZ(&Row(&globals[bones[i]], 0));
                NegateXYZ(&Row(&matrices[i], 0));
            }
            BlendMatrices(&globals[bones[i]], &matrices[i], weights[i]);
            if (mirrored) {
                NegateXYZ(&Row(&globals[bones[i]], 0));
                NegateXYZ(&Row(&matrices[i], 0));
            }
        } else {
            Transform *global = &globals[bones[i]];
            global->m[0] = matrices[i].m[0];
            global->m[1] = matrices[i].m[1];
            global->m[2] = matrices[i].m[2];
            global->m[4] = matrices[i].m[4];
            global->m[5] = matrices[i].m[5];
            global->m[6] = matrices[i].m[6];
            global->m[8] = matrices[i].m[8];
            global->m[9] = matrices[i].m[9];
            global->m[10] = matrices[i].m[10];
        }
        VU0_MATRIX4_mult(&locals[bones[i]], &globals[bones[i]], &relative);
        Row(&locals[bones[i]], 3) = translation;
    }
}

// FUNC_AT(0x00016940)
void ActGlobalPoseOverrideArray::Destruct() {
    if (count > 0) {
        OperatorDelete(weights);
        OperatorDelete(bones);
        OperatorDelete(matrices);
        OperatorDelete(set);
    }
    count = 0;
}
