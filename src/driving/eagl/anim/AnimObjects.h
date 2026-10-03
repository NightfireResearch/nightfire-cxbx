#ifndef DRIVING_EAGL_ANIM_ANIMOBJECTS_H_
#define DRIVING_EAGL_ANIM_ANIMOBJECTS_H_

// EAGLAnim's object layer: the pool, the factory, the objects (FnAnim.h) and AnimationBank. See AnimObjects.cpp.

#include "FnAnim.h"

struct AttributeBlock;

struct CompoundData : AnimData {     // a compound channel's data (type 15)
    AttributeBlock *attributes;      // +0x04
    uint16_t count;                  // +0x08 sub-anims
    uint16_t frames;                 // +0x0a
    uint8_t *subs[1];                // +0x0c [count] their data
};
static_assert(offsetof(CompoundData, subs) == 0xc, "a compound's sub-anims are at +0xc");

struct AnimBank {                    // an AnimationBank symbol
    uint32_t unknown00;
    int32_t count;                   // +0x04
    uint32_t unknown08;
    uint8_t **anims;                 // +0x0c the anim data
    const char **names;              // +0x10 sorted, for FindAnim

    int FindAnim(const char *name);                              // 0x000f71b0
};
static_assert(offsetof(AnimBank, names) == 0x10, "an AnimationBank's names are at +0x10");

void AnimBank_Constructor(AnimBank *bank, void *loader);         // 0x000f7170, the loader's AnimationBank callback
void AnimBank_Destructor(AnimBank *bank);                        // 0x000f71a0
FnAnim* AnimBank_NewCompound(uint8_t *data);                     // 0x000f7240 (invented)
void CompoundChannel_InitAnimMemoryMap(uint8_t *data);           // 0x000faf60

void EAGLAnim_InitInternal(uint32_t poolSize);                   // 0x000fa9c0 (Initializer::InitInternal)
void EAGLAnim_Nothing0();                                        // 0x000faa00
void EAGLAnim_Nothing1();                                        // 0x000faa10
void EAGLAnim_Nothing2();                                        // 0x000faa20
uint32_t EAGLAnim_GetMemoryUsage();                              // 0x000faa30
void EAGLAnim_ResetPool();                                       // 0x000faa40
void EAGLAnim_ShutDown();                                        // 0x000faa50

#endif // DRIVING_EAGL_ANIM_ANIMOBJECTS_H_
