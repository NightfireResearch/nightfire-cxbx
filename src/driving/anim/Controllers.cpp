#include "Controllers.h"

#include <stddef.h>
#include <stdint.h>

#include "AnimationDatabase.h"         // ActAnimGroup
#include "Poser.h"
#include "../engine/UMemory.hpp"

// ---------------------------------------------------------------------------------------------------------------
// The animation controllers (0x00011020-0x000112b0, 0x000125c0-0x00012620), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the C runtime's printf (the original's formatting and output)
#define CRT_printf ((int (*)(const char *, ...))0x00132192)

// The vtables
#define AnimationControllerVtable ((void **)0x00189e04)
#define StandardAnimationControllerVtable ((void **)0x00189e0c)
#define CrossFadeAnimationControllerVtable ((void **)0x00189e14)

// ---- AnimationController

// FUNC_AT(0x00012610)
void AnimationController::Destruct() {
    vtable = AnimationControllerVtable;
}

// FUNC_AT(0x000125c0)
AnimationController* AnimationController::Delete(unsigned flags) {
    vtable = AnimationControllerVtable;
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AnimationController));
    return this;
}

// FUNC_AT(0x00011020)
float AnimationController::GetCurrentFrame() {
    return poser->time;
}

// FUNC_AT(0x00011030)
float AnimationController::GetTotalFrames() {
    return poser->endTime;
}

// FUNC_AT(0x00011040)
double AnimationController::GetCurrentTimeSeconds() {
    return (double)poser->secondsPerFrame * poser->time;
}

// FUNC_AT(0x00011050)
double AnimationController::GetTotalTime() {
    return (double)poser->endTime * poser->secondsPerFrame;
}

// FUNC_AT(0x00011060)
double AnimationController::GetRemainingTime() {
    return (double)poser->endTime * poser->secondsPerFrame - (double)poser->secondsPerFrame * poser->time;
}

// ---- StandardAnimationController

// FUNC_AT(0x00011080)
StandardAnimationController* StandardAnimationController::Construct(ActAnimGroup *animGroup, ActPoser *poser,
                                                                    int bank, int index, MATRIX4 *frame) {
    this->animGroup = animGroup;
    this->poser = poser;
    this->frame = frame;
    vtable = StandardAnimationControllerVtable;
    animGroup->ChangeAnimation(bank, index);
    poser->SetNormalAnimation(animGroup, 0.0f, frame);
    return this;
}

// FUNC_AT(0x000125f0)
StandardAnimationController* StandardAnimationController::Delete(unsigned flags) {
    AnimationController::Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(StandardAnimationController));
    return this;
}

// FUNC_AT(0x00011100)
void StandardAnimationController::Update() {
    poser->AdvanceTime();
}

// ---- CrossFadeAnimationController

// FUNC_AT(0x00011110)
CrossFadeAnimationController* CrossFadeAnimationController::Construct(ActAnimGroup *animGroup, ActPoser *poser,
                                                                      int bank, int index, float blendTime,
                                                                      bool blendFlag, MATRIX4 *frame) {
    this->animGroup = animGroup;
    this->poser = poser;
    this->frame = frame;
    queued = 0;
    vtable = CrossFadeAnimationControllerVtable;
    poser->SetupCrossFadeBlend(blendTime, blendFlag);
    animGroup->ChangeAnimation(bank, index);
    poser->SetCrossFadeBlendAnimation(animGroup, frame);
    return this;
}

// FUNC_AT(0x00012620)
CrossFadeAnimationController* CrossFadeAnimationController::Delete(unsigned flags) {
    AnimationController::Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(CrossFadeAnimationController));
    return this;
}

// FUNC_AT(0x000111a0)
void CrossFadeAnimationController::Update() {
    poser->AdvanceTime();
    if (queued == 1 && poser->mode == kPoseModeNormal) {
        CRT_printf("blending is done...auto blending new anim\n");
        poser->SetupCrossFadeBlend(queuedBlendTime, false);
        animGroup->ChangeAnimation(queuedBank, queuedIndex);
        poser->SetCrossFadeBlendAnimation(animGroup, frame);
        queued = 0;
    }
    poser->DoSkeletonPose();
}

// FUNC_AT(0x00011210)
void CrossFadeAnimationController::StartNewCrossFade(int bank, int index, float blendTime) {
    int mode = poser->mode;
    if (mode == kPoseModeNormal) {
        CRT_printf("blend is already done, setting up new blend right now\n");
        poser->SetupCrossFadeBlend(blendTime, false);
        animGroup->ChangeAnimation(bank, index);
        poser->SetCrossFadeBlendAnimation(animGroup, frame);
        queued = 0;
    } else if (mode == kPoseModeCrossFade) {
        CRT_printf("blending is still happening,...queueing it up\n");
        queuedBlendTime = blendTime;
        queuedBank = bank;
        queuedIndex = index;
        queued = 1;
    }
    poser->DoSkeletonPose();
}
