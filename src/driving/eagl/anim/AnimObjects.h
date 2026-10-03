#ifndef DRIVING_EAGL_ANIM_ANIMOBJECTS_H_
#define DRIVING_EAGL_ANIM_ANIMOBJECTS_H_

// EAGLAnim's object layer: the pool, the factory, the objects (FnAnim.h) and AnimationBank. See AnimObjects.cpp.

#include "FnAnim.h"

struct AnimData {
    uint16_t* GetType(uint16_t *out);                            // 0x000141d0
};

struct AnimBank {                    // an AnimationBank symbol: count at +4, anims[] at +0xc, sorted names[] at +0x10
    int FindAnim(const char *name);                              // 0x000f71b0
};

void AnimBank_Constructor(uint8_t *bank, void *loader);          // 0x000f7170, the loader's AnimationBank callback
void AnimBank_Destructor(uint8_t *bank);                         // 0x000f71a0
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
