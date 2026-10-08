#pragma fp_contract(off)

#include "Poser.h"

#include "AnimationDatabase.h"          // ActAnimGroup
#include "IK.h"                         // ActIKSolverArray, ActGlobalPoseOverrideArray
#include "Manager.h"                    // ActManager
#include "../eagl/anim/FnAnim.h"
#include "../eagl/anim/Skeleton.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// ActPoser and the cross-fade record's destructor. See Poser.h.
//
// Times are frames. The x87 arithmetic is written in double in the original's order, with a float at every store;
// a time compared with 0 after a store is compared unrounded, as the original does.
// ---------------------------------------------------------------------------------------------------------------


// ---- globals


// DoMainPose's scratch for moving a root bone: a scale, quaternion and translation (each w 1), and a pointer to
// each that the pose is copied in and out through.
struct RootPoseScratch {
    float scale[4];                   // +0x00
    float rotation[4];                // +0x10
    float translation[4];             // +0x20
    float *translationRef;            // +0x30 -> translation
    uint32_t unknown34[3];
    float *rotationRef;               // +0x40 -> rotation
    uint32_t unknown44[3];
    float *scaleRef;                  // +0x50 -> scale
};
static_assert(offsetof(RootPoseScratch, scaleRef) == 0x50, "the scratch's pointers are at +0x30..+0x50");
#define RootScratch (*(RootPoseScratch *)0x001b4a90)

// A bone of a pose
struct BonePose {
    float scale[3];
    float lengthScale;
    float rotation[4];
    float translation[3];
    float one;
};
static_assert(sizeof(BonePose) == 48, "a pose is 12 floats a bone");

constexpr float kFramesPerSecond = 20.0f;
constexpr float kSecondsPerFrame = 0.05f;
constexpr unsigned kAnimGroupSize = 0x20;      // ActAnimGroup, from the pools
constexpr float kEventTimeBeforeZero = -0.0001f;
static_assert(std::bit_cast<uint32_t>(kFramesPerSecond) == 0x41a00000 &&
              std::bit_cast<uint32_t>(kSecondsPerFrame) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(kEventTimeBeforeZero) == 0xb8d1b717, "the original's constants");

// ActAnimGroup's two channels, each NULL without its data (inline in the original)
static FnAnim *PoseChannel(const ActAnimGroup *group) {
    return group->animData != NULL ? group->anim : NULL;
}

static FnAnim *EventChannel(const ActAnimGroup *group) {
    return group->secondData != NULL ? group->second : NULL;
}

// The channels' virtuals: Eval (a pose from previous to time) and EvalEvent
static void EvalPose(FnAnim *channel, float previous, float time, float *pose) {
    AnimVCall<void>(channel, kSlotEval, previous, time, pose);
}

static void EvalEvents(FnAnim *channel, float previous, float time, ActEvents *events) {
    AnimVCall<bool>(channel, kSlotEvalEvent, previous, time, events, static_cast<void *>(NULL));
}

static Transform *AsTransforms(MATRIX4 *matrices) {
    return reinterpret_cast<Transform *>(matrices);
}

// The x axis of a matrix (row `row`'s xyz) negated: a mirrored poser's
static void NegateRow(MATRIX4 *matrix, int row) {
    matrix->mtx[row][0] = -matrix->mtx[row][0];
    matrix->mtx[row][1] = -matrix->mtx[row][1];
    matrix->mtx[row][2] = -matrix->mtx[row][2];
}

// A root bone's pose moved by `by`: its scale, rotation and translation made a matrix, multiplied by `by`, and the
// rotation and translation taken back (written twice in the original's DoMainPose).
static void MoveRootPose(BonePose *root, const MATRIX4 *by, bool suppressTranslation, bool mirrored) {
    RootPoseScratch &scratch = RootScratch;
    scratch.scaleRef[0] = root->scale[0];
    scratch.scaleRef[1] = root->scale[1];
    scratch.scaleRef[2] = root->scale[2];
    scratch.rotationRef[0] = root->rotation[0];
    scratch.rotationRef[1] = root->rotation[1];
    scratch.rotationRef[2] = root->rotation[2];
    scratch.rotationRef[3] = root->rotation[3];
    scratch.translationRef[0] = root->translation[0];
    scratch.translationRef[1] = root->translation[1];
    scratch.translationRef[2] = root->translation[2];
    alignas(16) MATRIX4 matrix;
    VU0_SQTquattom4(scratch.scale, scratch.rotation, scratch.translation, &matrix);
    if (suppressTranslation) {
        matrix.mtx[3][0] = 0.0f;
        matrix.mtx[3][2] = 0.0f;
    }
    VU0_MATRIX4_mult(&matrix, &matrix, by);
    if (mirrored)
        NegateRow(&matrix, 1);
    VU0_m4toquat(scratch.rotation, &matrix);
    scratch.translation[0] = matrix.mtx[3][0];
    scratch.translation[1] = matrix.mtx[3][1];
    scratch.translation[2] = matrix.mtx[3][2];
    scratch.translation[3] = 1.0f;
    root->rotation[0] = scratch.rotationRef[0];
    root->rotation[1] = scratch.rotationRef[1];
    root->rotation[2] = scratch.rotationRef[2];
    root->rotation[3] = scratch.rotationRef[3];
    root->translation[0] = scratch.translationRef[0];
    root->translation[1] = scratch.translationRef[1];
    root->translation[2] = scratch.translationRef[2];
}

// ---- construction

// FUNC_AT(0x00018550)
ActPoser* ActPoser::Construct(ActSkeleton *skeleton, ActEvents *events, bool mirrored, bool hasMatrixCallback) {
    this->hasMatrixCallback = hasMatrixCallback;
    this->mirrored = mirrored;
    this->skeleton = skeleton;
    this->events = events;
    framesPerSecond = kFramesPerSecond;
    secondsPerFrame = kSecondsPerFrame;
    timeScale = 1.0f;
    crossFade = NULL;
    mode = kPoseModeNone;
    frameTime = TheActManager->frameTime;
    scale = static_cast<Coord4 *>(OperatorNew(sizeof(Coord4)));
    offset = static_cast<Coord4 *>(OperatorNew(sizeof(Coord4)));
    scale->w = 1.0f;
    scale->z = 1.0f;
    scale->y = 1.0f;
    scale->x = 1.0f;
    offset->w = 0.0f;
    offset->z = 0.0f;
    offset->y = 0.0f;
    offset->x = 0.0f;

    ActPoseMatrices *poseMatrices = static_cast<ActPoseMatrices *>(OperatorNew(sizeof(ActPoseMatrices)));
    if (poseMatrices != NULL) {
        poseMatrices->count = skeleton->GetNumBones();
        poseMatrices->buffer = static_cast<MATRIX4 *>(OperatorNewArray(poseMatrices->count * sizeof(MATRIX4)));
        poseMatrices->current = poseMatrices->buffer;
    }
    matrices = poseMatrices;

    ActIKSolverArray *solvers = static_cast<ActIKSolverArray *>(OperatorNew(sizeof(ActIKSolverArray)));
    if (solvers != NULL)
        solvers = solvers->Construct(skeleton->rootBone, skeleton->skeleton, AsTransforms(matrices->current),
                                     AsTransforms(skeleton->matrices));
    ikSolvers = solvers;

    ActGlobalPoseOverrideArray *overrides =
        static_cast<ActGlobalPoseOverrideArray *>(OperatorNew(sizeof(ActGlobalPoseOverrideArray)));
    if (overrides != NULL)
        overrides = overrides->Construct(AsTransforms(matrices->current));
    poseOverrides = overrides;

    suppressTranslation = false;
    return this;
}

// FUNC_AT(0x00019580)
void ActPoser::Destruct() {
    if (poseOverrides != NULL) {
        poseOverrides->Destruct();
        OperatorDelete(poseOverrides);
    }
    if (ikSolvers != NULL) {
        ikSolvers->Destruct();
        OperatorDelete(ikSolvers);
    }
    if (matrices != NULL) {
        matrices->Destruct();
        OperatorDelete(matrices);
    }
    OperatorDelete(scale);
    OperatorDelete(offset);
    if (crossFade != NULL) {
        crossFade->Destruct();
        UMemory::FastFree(crossFade, sizeof(ActCrossFadeBlendData));
    }
    crossFade = NULL;
}

// ---- the cross-fade

// FUNC_AT(0x00018410)
ActPoseMatrices* ActPoseMatrices::ConstructCopy(const ActPoseMatrices *other) {
    count = other->count;
    matrices[0] = other->matrices[0];
    matrices[1] = other->matrices[0];
    matrices[2] = other->matrices[2];
    matrices[3] = other->matrices[3];
    buffer = static_cast<MATRIX4 *>(OperatorNewArray(count * sizeof(MATRIX4)));
    current = buffer;
    return this;
}

// FUNC_AT(0x00018400)
void ActPoseMatrices::Destruct() {
    OperatorDelete(buffer);
}

// FUNC_AT(0x00018490)
ActCrossFadeBlendData* ActCrossFadeBlendData::Construct(float length, float frame, float endFrame,
                                                        const ActAnimGroup *animation,
                                                        const ActPoseMatrices *matrices, bool unknown) {
    this->matrices.ConstructCopy(matrices);
    this->length = length + 1.0f;
    this->frame = frame;
    this->endFrame = endFrame;
    unknown120 = unknown;
    step = 1.0f / length;
    ActAnimGroup *memory = static_cast<ActAnimGroup *>(UMemory::FastAlloc(sizeof(ActAnimGroup), "ActAnimGroup"));
    this->animation = memory != NULL ? memory->Construct(animation) : NULL;
    return this;
}

// FUNC_AT(0x00018ac0)
void ActCrossFadeBlendData::Destruct() {
    if (animation != NULL) {
        animation->Destruct();
        UMemory::FastFree(animation, kAnimGroupSize);
    }
    matrices.Destruct();
}

// ---- bones and the placement

// FUNC_AT(0x000186e0)
int ActPoser::GetBoneIndex(const char *name) {
    return skeleton->GetBoneIndex(name);
}

// FUNC_AT(0x000186f0)
void ActPoser::GetRootBonePosOri(MATRIX4 *out) {
    *out = matrices->current[skeleton->rootBone];
}

// FUNC_AT(0x00018720)
void ActPoser::GetWeaponBonePosOri(int which, MATRIX4 *out) {
    *out = matrices->current[skeleton->weaponBones[which]];
}

// FUNC_AT(0x00018750)
void ActPoser::ChangeAnimationOrigin(const Coord3 *origin) {
    MATRIX4 &placement = matrices->matrices[kPoseTbOu];
    placement.mtx[3][0] = origin->x;
    placement.mtx[3][1] = origin->y;
    placement.mtx[3][2] = origin->z;
}

// FUNC_AT(0x00018780)
void ActPoser::GetAnimationOrigin(Coord3 *out) {
    const MATRIX4 &placement = matrices->matrices[kPoseTbOu];
    out->x = placement.mtx[3][0];
    out->y = placement.mtx[3][1];
    out->z = placement.mtx[3][2];
}

// FUNC_AT(0x000187b0)
void ActPoser::GetInitialTbOu(MATRIX4 *out) {
    MatrixCopy(&matrices->matrices[kPoseTbOu], out);
}

void ActPoser::UpdatePoseToWorld() {
    MATRIX4 *poseMatrices = matrices->matrices;
    VU0_MATRIX4_mult(&poseMatrices[kPoseToWorld], &poseMatrices[kPoseRootOffset], &poseMatrices[kPoseRootTurn]);
    VU0_MATRIX4_mult(&poseMatrices[kPoseToWorld], &poseMatrices[kPoseToWorld], &poseMatrices[kPoseTbOu]);
}

// FUNC_AT(0x00018b30)
void ActPoser::SetAnimationOrigin(const Coord3 *origin) {
    MATRIX4 &placement = matrices->matrices[kPoseTbOu];
    placement.mtx[3][0] = origin->x;
    placement.mtx[3][1] = origin->y;
    placement.mtx[3][2] = origin->z;
    UpdatePoseToWorld();
}

// FUNC_AT(0x00018b80)
void ActPoser::SetInitialTbOu(const MATRIX4 *placement) {
    MatrixCopy(placement, &matrices->matrices[kPoseTbOu]);
    UpdatePoseToWorld();
}

// FUNC_AT(0x00018bd0)
void ActPoser::Rotate(float turns) {
    float c = cos_fractionalangle(turns);
    float s = sin_fractionalangle(turns);
    alignas(16) MATRIX4 turn = {{
        {c, 0.0f, -s, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {s, 0.0f, c, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    MATRIX4 *placement = &matrices->matrices[kPoseTbOu];
    if (mirrored)
        NegateRow(placement, 0);
    VU0_MATRIX4_mult(placement, &turn, placement);
    if (mirrored)
        NegateRow(placement, 0);
    UpdatePoseToWorld();
}

// FUNC_AT(0x00018cf0)
void ActPoser::RotateX(float turns) {
    float c = cos_fractionalangle(turns);
    float s = sin_fractionalangle(turns);
    alignas(16) MATRIX4 turn = {{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, c, s, 0.0f},
        {0.0f, -s, c, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    MATRIX4 *placement = &matrices->matrices[kPoseTbOu];
    if (mirrored)
        NegateRow(placement, 0);
    VU0_MATRIX4_mult(placement, &turn, placement);
    if (mirrored)
        NegateRow(placement, 0);
    UpdatePoseToWorld();
}

// FUNC_AT(0x00018e10)
void ActPoser::CalcSnapAndCorrectionMatrices(const MATRIX4 *placement) {
    MATRIX4 *poseMatrices = matrices->matrices;
    MATRIX4 &tbOu = poseMatrices[kPoseTbOu];
    tbOu = *placement;
    if (!hasMatrixCallback) {
        // Upright: z flattened onto the ground, y straight up, x across them
        tbOu.mtx[2][1] = 0.0f;
        VU0_v4unitxyz(tbOu.mtx[2], tbOu.mtx[2]);
        tbOu.mtx[1][0] = 0.0f;
        tbOu.mtx[1][1] = 1.0f;
        tbOu.mtx[1][2] = 0.0f;
        VU0_v4unitcrossprodxyz(tbOu.mtx[1], tbOu.mtx[2], tbOu.mtx[0]);
        tbOu.mtx[3][1] = 0.0f;
    }
    if (mirrored)
        NegateRow(&tbOu, 0);

    EvalPose(PoseChannel(animation), previousTime, time, skeleton->stillPose);
    MATRIX4 *globals = matrices->current;
    skeleton->skeleton->PoseSQTToGlobal(skeleton->stillPose, AsTransforms(globals), NULL);
    const MATRIX4 &root = globals[skeleton->rootBone];

    MATRIX4 &turn = poseMatrices[kPoseRootTurn];
    MATRIX4 &offset = poseMatrices[kPoseRootOffset];
    if (!hasMatrixCallback) {
        float sine = root.mtx[2][0];
        float cosine = VU0_sqrt((float)(1.0 - (double)sine * sine));
        turn.mtx[2][2] = cosine;
        turn.mtx[0][0] = cosine;
        turn.mtx[2][0] = sine * -1.0f;
        turn.mtx[3][2] = 0.0f;
        turn.mtx[3][1] = 0.0f;
        turn.mtx[3][0] = 0.0f;
        turn.mtx[2][3] = 0.0f;
        turn.mtx[2][1] = 0.0f;
        turn.mtx[1][3] = 0.0f;
        turn.mtx[1][2] = 0.0f;
        turn.mtx[1][0] = 0.0f;
        turn.mtx[0][3] = 0.0f;
        turn.mtx[0][1] = 0.0f;
        turn.mtx[0][2] = sine;
        turn.mtx[3][3] = 1.0f;
        turn.mtx[1][1] = 1.0f;

        offset.mtx[3][3] = 1.0f;
        offset.mtx[2][2] = 1.0f;
        offset.mtx[1][1] = 1.0f;
        offset.mtx[0][0] = 1.0f;
        offset.mtx[2][3] = 0.0f;
        offset.mtx[2][1] = 0.0f;
        offset.mtx[2][0] = 0.0f;
        offset.mtx[1][3] = 0.0f;
        offset.mtx[1][2] = 0.0f;
        offset.mtx[1][0] = 0.0f;
        offset.mtx[0][3] = 0.0f;
        offset.mtx[0][2] = 0.0f;
        offset.mtx[0][1] = 0.0f;
        offset.mtx[3][1] = 0.0f;
        offset.mtx[3][0] = -root.mtx[3][0];
        offset.mtx[3][2] = -root.mtx[3][2];
    } else {
        VU0_MATRIX4Init(&turn);
        VU0_MATRIX4Init(&offset);
    }
    UpdatePoseToWorld();
}

// ---- animations and time

// FUNC_AT(0x00018fe0)
void ActPoser::SetNormalAnimation(ActAnimGroup *animation, float seconds, const MATRIX4 *placement) {
    this->animation = animation;
    mode = kPoseModeNormal;
    length = animation->length;
    endTime = animation->length;
    double frame = (double)seconds * framesPerSecond;
    time = (float)frame;
    nextTime = (float)frame;
    double previous = frame - FrameStep();
    previousTime = (float)previous;
    if (previous < 0.0)
        previousTime = 0.0f;
    CalcSnapAndCorrectionMatrices(placement);
}

// FUNC_AT(0x000187d0)
void ActPoser::SetupCrossFadeBlend(float seconds, bool unknown) {
    void *block = UMemory::FastAlloc(sizeof(ActCrossFadeBlendData), "ActCrossFadeBlendData");
    crossFade = block != NULL ? (static_cast<ActCrossFadeBlendData *>(block))->Construct(seconds * framesPerSecond, time, length, animation, matrices, unknown)
                              : NULL;
}

// FUNC_AT(0x00019040)
void ActPoser::SetCrossFadeBlendAnimation(ActAnimGroup *animation, const MATRIX4 *placement) {
    this->animation = animation;
    mode = kPoseModeNormal;
    length = animation->length;
    endTime = animation->length;
    double frame = framesPerSecond * 0.0;
    time = (float)frame;
    nextTime = (float)frame;
    double previous = frame - FrameStep();
    previousTime = (float)previous;
    if (previous < 0.0)
        previousTime = 0.0f;
    mode = kPoseModeCrossFade;
    endTime = crossFade->length;
    CalcSnapAndCorrectionMatrices(placement);
}

// FUNC_AT(0x00018850)
void ActPoser::CalcCrossFadeTimes(float *fadePrevious, float *fadeTime, float *previous, float *time, float *weight) {
    const ActCrossFadeBlendData *fade = crossFade;
    if (fade->unknown120)
        *fadeTime = fade->frame;
    else
        *fadeTime = fade->frame + this->time;
    double fadeLast = (double)fade->endFrame - 2.0;
    if (fadeLast < *fadeTime)
        *fadeTime = (float)fadeLast;
    double before = *fadeTime - FrameStep();
    *fadePrevious = (float)before;
    if (before < 0.0)
        *fadePrevious = 0.0f;

    if (fade->unknown120)
        *time = 0.0f;
    else
        *time = this->time;
    double last = (double)length - 2.0;
    if (last < *time)
        *time = (float)Ftol(last);
    before = *time - FrameStep();
    *previous = (float)before;
    if (before < 0.0)
        *previous = 0.0f;

    *weight = fade->step * this->time;
}

// FUNC_AT(0x00019610)
void ActPoser::FinishCrossFadeBlend() {
    ActCrossFadeBlendData *fade = crossFade;
    double frame;
    if (fade->unknown120) {
        frame = 0.0;
    } else {
        double fadeLast = (double)endTime - 1.0;
        if (fadeLast >= (double)length - 1.0)
            frame = (double)length - 2.0;
        else
            frame = fadeLast;
        if (frame < 0.0)
            frame = 0.0;
    }
    mode = kPoseModeNormal;
    length = animation->length;
    endTime = animation->length;
    frame = frame * secondsPerFrame * framesPerSecond;
    time = (float)frame;
    nextTime = (float)frame;
    double previous = frame - FrameStep();
    previousTime = (float)previous;
    if (previous < 0.0)
        previousTime = 0.0f;
    if (fade != NULL) {
        fade->Destruct();
        UMemory::FastFree(fade, sizeof(ActCrossFadeBlendData));
    }
    crossFade = NULL;
}

// FUNC_AT(0x00019710)
void ActPoser::AdvanceTime() {
    if (mode == kPoseModeManual)
        return;
    double next = FrameStep() + time;
    if ((double)endTime - 1.0 > next)
        nextTime = (float)next;
    else if (mode == kPoseModeCrossFade)
        FinishCrossFadeBlend();
}

// ---- posing

// FUNC_AT(0x00018960)
void ActPoser::DoEventPose() {
    float time = this->time;
    float previous = previousTime;
    if (previous == time && time == 0.0f)
        previous = kEventTimeBeforeZero;

    if (mode == kPoseModeNormal) {
        if (animation->secondData != NULL)
            EvalEvents(EventChannel(animation), previous, time, events);
    } else if (mode == kPoseModeCrossFade) {
        float fadePrevious, fadeTime, animationPrevious, animationTime, weight;
        CalcCrossFadeTimes(&fadePrevious, &fadeTime, &animationPrevious, &animationTime, &weight);
        if (crossFade->animation->secondData != NULL)
            EvalEvents(EventChannel(crossFade->animation), fadePrevious, fadeTime, events);
        if (animation->secondData != NULL)
            EvalEvents(EventChannel(animation), animationPrevious, animationTime, events);
    } else if (mode == kPoseModeManual) {
        if (animation->secondData != NULL)
            EvalEvents(EventChannel(animation), manualEventPrevious, manualEventTime, events);
    }
}

// FUNC_AT(0x000190b0)
void ActPoser::DoMainPose() {
    skeleton->GetStillPose();
    float frame = nextTime;
    time = frame;
    double previous = frame - FrameStep();
    previousTime = (float)previous;
    if (previous < 0.0)
        previousTime = 0.0f;

    if (mode == kPoseModeNormal) {
        if (animation->animData == NULL)
            return;
        EvalPose(PoseChannel(animation), previousTime, frame, skeleton->stillPose);
        skeleton->skeleton->PoseSQTToLocal(skeleton->stillPose, AsTransforms(skeleton->matrices), NULL);
    } else if (mode == kPoseModeCrossFade) {
        ActSkeleton *actSkeleton = skeleton;
        float *pose = actSkeleton->stillPose;
        int rootBone = actSkeleton->rootBone;
        float *fadePose = actSkeleton->pose;
        float fadePrevious, fadeTime, animationPrevious, animationTime, weight;
        CalcCrossFadeTimes(&fadePrevious, &fadeTime, &animationPrevious, &animationTime, &weight);

        actSkeleton->GetStillPose(fadePose);
        EvalPose(PoseChannel(crossFade->animation), fadePrevious, fadeTime, fadePose);
        MoveRootPose(&reinterpret_cast<BonePose *>(fadePose)[rootBone], &crossFade->matrices.matrices[kPoseToWorld],
                     suppressTranslation, mirrored);

        EvalPose(PoseChannel(animation), animationPrevious, animationTime, pose);
        MoveRootPose(&reinterpret_cast<BonePose *>(pose)[rootBone], &matrices->matrices[kPoseToWorld],
                     suppressTranslation, mirrored);

        skeleton->BlendBones(weight, fadePose, pose, pose, true);
        skeleton->skeleton->PoseSQTToLocal(pose, AsTransforms(skeleton->matrices), NULL);
        if (mirrored)
            NegateRow(&skeleton->matrices[skeleton->rootBone], 1);
        return;
    } else if (mode == kPoseModeManual) {
        float *pose = skeleton->stillPose;
        float *otherPose = skeleton->pose;
        skeleton->GetStillPose(otherPose);
        EvalPose(PoseChannel(animation), manualFromPrevious, manualFromTime, pose);
        EvalPose(PoseChannel(animation), manualToPrevious, manualToTime, otherPose);
        skeleton->BlendBones(manualWeight, pose, otherPose, pose, true);
        skeleton->skeleton->PoseSQTToLocal(pose, AsTransforms(skeleton->matrices), NULL);
    } else {
        return;
    }

    MATRIX4 *root = &skeleton->matrices[skeleton->rootBone];
    if (suppressTranslation) {
        root->mtx[3][0] = 0.0f;
        root->mtx[3][2] = 0.0f;
    }
    VU0_MATRIX4_mult(root, root, &matrices->matrices[kPoseToWorld]);
}

// FUNC_AT(0x000196d0)
void ActPoser::DoIK() {
    int count = ikSolvers->count;
    for (int i = 0; i < count; i++) {
        skeleton->skeleton->PoseLocalToGlobal(AsTransforms(skeleton->matrices), AsTransforms(matrices->current), NULL);
        ikSolvers->Solve(i);
    }
}

// FUNC_AT(0x00019750)
void ActPoser::DoSkeletonPose() {
    DoMainPose();
    DoIK();
    if (poseOverrides->count > 0) {
        skeleton->skeleton->PoseLocalToGlobal(AsTransforms(skeleton->matrices), AsTransforms(matrices->current), NULL);
        poseOverrides->DoGlobalOverrides(mirrored, AsTransforms(skeleton->matrices));
    }
    skeleton->skeleton->PoseLocalToGlobal(AsTransforms(skeleton->matrices), AsTransforms(matrices->current), NULL);
}

// FUNC_AT(0x000197c0)
void ActPoser::DoInitialPoses() {
    float next = nextTime;
    nextTime = 0.0f;
    DoSkeletonPose();
    nextTime = next;
}

// FUNC_AT(0x00018940)
void ActPoser::Skin() {
    skeleton->skeleton->PoseGlobalToSkin(AsTransforms(matrices->current), AsTransforms(AnimationBuffer), NULL);
}
