#ifndef DRIVING_ANIM_IK_H_
#define DRIVING_ANIM_IK_H_

// ---------------------------------------------------------------------------------------------------------------
// The actors' inverse kinematics and pose overrides (engine.anim): ActIK solves a two-bone chain (a bone, its
// child and grandchild) for a target; ActIKSolver holds one ActIK with its target and blend weight, and
// ActIKSolverArray an actor's solvers; ActGlobalPoseOverrideArray replaces chosen bones' orientations with given
// matrices, blended in by a weight. ActPoser owns one of each array: DoIK runs the solvers after the local
// matrices are made global, DoSkeletonPose the overrides. See IK.cpp.
//
// Matrices are EAGL's Transform (row vectors, the translation in row 3). "globals" and "locals" are an actor's
// per-bone matrix arrays (ActPoser's): the global matrices and the local (parent-relative) ones.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../eagl/Transform.h"
#include "../eagl/anim/Skeleton.h"
#include "../data/CoordConvert.h"   // Coord4

// The vector and quaternion helpers the solver uses (quaternions x, y, z, w; AngleBetweenVectors and QuatProduct,
// which EAGL uses too, are in eagl/Transform.h). Names ours.
void PerpendicularVector(const float *in, float *out);           // a vector at right angles to in 0x00016590
void QuatFromAxisAngle(const float *axis, float radians, float *out);                        // 0x00016640
void QuatFromTo(const float *from, const float *to, float *out);   // the shortest rotation     0x000166a0

// ---- ActIK

class ActIK {                       // 0x28
public:
    Skeleton *skeleton;             // +0x00
    int32_t bones[3];               // +0x04 the chain: a bone, its child, its grandchild
    float upperLength;              // +0x10 the still pose's length of bones[1] (from bones[0])
    float lowerLength;              // +0x14 ... and of bones[2] (from bones[1])
    Coord4 axis;                    // +0x18 the bend axis, in bones[1]'s space

    ActIK* Construct() {            // inlined in ActIKSolverArray::CreateIKs
        skeleton = NULL;
        return this;
    }
    void Init(Skeleton *skeleton, int bone, const Coord4 *axis);                                   // 0x00016980
    void Solve(const Transform *global, const float *target, Transform *upper, Transform *middle,
               const Transform *lower, const float *pole);                                         // 0x00016a30
    // The angle at bones[1] for the chain to reach `target` (in bones[0]'s space): the law of cosines, clamped.
    // Name ours (FUN_000168b0).                                                                     0x000168b0
    double JointAngle(float upper, float lower, const float *target);
};
static_assert(sizeof(ActIK) == 0x28, "an ActIK is 0x28 bytes");
static_assert(offsetof(ActIK, axis) == 0x18, "ActIK's axis is at +0x18");

// ---- ActIKSolver

// What ActActor::SetIKInfoArray hands each solver (name ours).
struct ActIKSolveInfo {             // 0x30
    Coord4 target;                  // +0x00 in the reference bone's space if the solver transforms targets
    Coord4 pole;                    // +0x10 the direction the middle joint should point, in global space
    float weight;                   // +0x20 1 or more: the solution alone; below 1 blended with the pose
    bool usePole;                   // +0x24
    uint8_t unknown25[0xb];
};
static_assert(sizeof(ActIKSolveInfo) == 0x30, "an IK solve info is 0x30 bytes");

class ActIKSolver {                 // 0x50
public:
    Skeleton *skeleton;             // +0x00
    ActIK *ik;                      // +0x04
    int32_t referenceBone;          // +0x08
    int32_t bone;                   // +0x0c the chain's first bone
    bool transformTarget;           // +0x10 the target is in referenceBone's space (moved to global each solve)
    bool active;                    // +0x11 set once it has been given its info
    uint8_t unknown12[0xe];
    ActIKSolveInfo info;            // +0x20

    ActIKSolver* Construct(Skeleton *skeleton, int referenceBone, int bone, bool transformTarget,
                           const Coord4 *axis);   // inlined in ActIKSolverArray::CreateIKs
    void BlendMatrices(Transform *matrix, const Transform *other, float weight);                 // 0x00015900
    void Solve(Transform *globals, Transform *locals);                                            // 0x00016d80
};
static_assert(sizeof(ActIKSolver) == 0x50, "an ActIKSolver is 0x50 bytes");
static_assert(offsetof(ActIKSolver, info) == 0x20, "ActIKSolver's info is at +0x20");

class ActIKSolverArray {            // 0x18
public:
    int32_t count;                  // +0x00
    ActIKSolver **solvers;          // +0x04 [count]
    int32_t referenceBone;          // +0x08 ActPoser passes its skeleton's root bone
    Transform *globals;             // +0x0c
    Transform *locals;              // +0x10
    Skeleton *skeleton;             // +0x14

    ActIKSolverArray* Construct(int referenceBone, Skeleton *skeleton, Transform *globals,
                                Transform *locals);                                               // 0x000159b0
    void SetInfo(int index, const ActIKSolveInfo *info);                                          // 0x000159e0
    void CreateIKs(int count, const int *bones, const Coord4 *axes, const bool *transformTargets);    // 0x00016c30
    // PS2: DeleteIKs (Ghidra calls it a destructor too; its only caller is the destructor below).    0x00016d20
    void DeleteIKs();
    void Destruct();                                                                              // 0x00016e90
    void Solve(int index);                                                                        // 0x00016ea0
};
static_assert(sizeof(ActIKSolverArray) == 0x18, "an ActIKSolverArray is 0x18 bytes");

// ---- ActGlobalPoseOverrideArray

class ActGlobalPoseOverrideArray {  // 0x1c
public:
    int32_t count;                  // +0x00
    int32_t *bones;                 // +0x04 [count]
    Transform *matrices;            // +0x08 [count] the overrides
    float *weights;                 // +0x0c [count]
    Transform *globals;             // +0x10 the actor's global matrices, by bone
    bool local;                     // +0x14 the overrides replace local matrices (else global orientations)
    bool *set;                      // +0x18 [count] an override has been given

    ActGlobalPoseOverrideArray* Construct(Transform *globals);                                    // 0x00015a10
    void CreateGlobalPoseOverrides(int count, const int *bones, bool local);                      // 0x00015a30
    void BlendMatrices(Transform *matrix, const Transform *other, float weight);                 // 0x00015ac0
    void SetGlobalPoseOverride(int index, const Transform *matrix, float weight);                // 0x00015b40
    // The count is not read: every override is given
    void SetGlobalPoseOverrides(int unusedCount, const Transform *matrices, const float *weights);   // 0x00015d00
    void DoGlobalOverrides(bool mirrored, Transform *locals);                                    // 0x00015d40
    void Destruct();                                                                              // 0x00016940
};
static_assert(sizeof(ActGlobalPoseOverrideArray) == 0x1c, "an ActGlobalPoseOverrideArray is 0x1c bytes");

#endif // DRIVING_ANIM_IK_H_
