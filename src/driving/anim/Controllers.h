#ifndef DRIVING_ANIM_CONTROLLERS_H_
#define DRIVING_ANIM_CONTROLLERS_H_

// ---------------------------------------------------------------------------------------------------------------
// The animation controllers an ActActor drives its poser through (0x00011020-0x000112b0, 0x000125c0-0x00012620):
//   AnimationController (0x30, vtable 0x00189e04): the base - the poser's frame and time getters. Its Update is
//     pure; the class is only ever a base.
//   StandardAnimationController (0x30, vtable 0x00189e0c): one animation at a time, played straight.
//   CrossFadeAnimationController (0x40, vtable 0x00189e14): cross-fades into each new animation; a new one asked
//     for while a blend is running waits until the blend is done.
// Vtable slots: 0 Update, 1 the scalar deleting destructor. See Controllers.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../world/CollisionTypes.h"    // MATRIX4

class ActAnimGroup;
class ActPoser;

class AnimationController {
public:
    void **vtable;                      // +0x00
    uint8_t unknown04[0xc];
    ActAnimGroup *animGroup;            // +0x10 the animation played
    ActPoser *poser;                    // +0x14
    uint8_t unknown18[8];
    MATRIX4 *frame;                     // +0x20 the actor's frame the poser animates in (ActActorMatrices'
                                        //       animationFrame)
    uint8_t unknown24[0xc];

    void Destruct();                                                                            // 0x00012610
    AnimationController* Delete(unsigned flags);    // vtable slot 1                            // 0x000125c0

    float GetCurrentFrame();                                                                    // 0x00011020
    float GetTotalFrames();                                                                     // 0x00011030
    // In seconds, answered unrounded. GetCurrentTime in Ghidra: windows.h makes that name a macro.
    double GetCurrentTimeSeconds();                                                             // 0x00011040
    double GetTotalTime();                                                                      // 0x00011050
    double GetRemainingTime();                                                                  // 0x00011060

    // ---- calls through the vtable
    void UpdateVirtual() {
        typedef void (AnimationController::*Method)();
        (this->*XbeVirtual<Method>(this, 0))();
    }
    void DeleteVirtual(unsigned flags) {
        typedef AnimationController* (AnimationController::*Method)(unsigned);
        (this->*XbeVirtual<Method>(this, 1))(flags);
    }
};
static_assert(sizeof(AnimationController) == 0x30, "an AnimationController is 0x30 bytes");
static_assert(offsetof(AnimationController, animGroup) == 0x10 && offsetof(AnimationController, poser) == 0x14 &&
              offsetof(AnimationController, frame) == 0x20, "AnimationController layout");

class StandardAnimationController : public AnimationController {
public:
    // Plays animation `index` of `bank` in `frame`
    StandardAnimationController* Construct(ActAnimGroup *animGroup, ActPoser *poser, int bank, int index,
                                           MATRIX4 *frame);                                     // 0x00011080
    StandardAnimationController* Delete(unsigned flags);    // vtable slot 1                    // 0x000125f0

    void Update();                      // vtable slot 0                                        // 0x00011100
};
static_assert(sizeof(StandardAnimationController) == 0x30, "a StandardAnimationController is 0x30 bytes");

class CrossFadeAnimationController : public AnimationController {
public:
    // A blend asked for while one was running, started when it is done
    int32_t queued;                     // +0x30 1: queuedBlendTime, queuedBank, queuedIndex are waiting
    float queuedBlendTime;              // +0x34
    int32_t queuedBank;                 // +0x38
    int32_t queuedIndex;                // +0x3c

    // Cross-fades into animation `index` of `bank` over `blendTime` (blendTime and blendFlag are
    // ActPoser::SetupCrossFadeBlend's arguments)
    CrossFadeAnimationController* Construct(ActAnimGroup *animGroup, ActPoser *poser, int bank, int index,
                                            float blendTime, bool blendFlag, MATRIX4 *frame);   // 0x00011110
    CrossFadeAnimationController* Delete(unsigned flags);   // vtable slot 1                    // 0x00012620

    void Update();                      // vtable slot 0                                        // 0x000111a0
    // A blend into animation `index` of `bank` now if the last one is done, after it if one is running
    void StartNewCrossFade(int bank, int index, float blendTime);                          // 0x00011210
};
static_assert(sizeof(CrossFadeAnimationController) == 0x40, "a CrossFadeAnimationController is 0x40 bytes");

#endif // DRIVING_ANIM_CONTROLLERS_H_
