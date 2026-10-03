#ifndef DRIVING_EAGL_ANIM_ANIMDELTAF_H_
#define DRIVING_EAGL_ANIM_ANIMDELTAF_H_

// FnDeltaF1 and FnDeltaF3, the keyframe-block channels most of the disc's anims are made of. See AnimDeltaF.cpp.

#include "FnAnim.h"

struct FnDeltaF : FnAnimMemoryMap {  // 0x30, FnDeltaF1 (type 21) and FnDeltaF3 (type 20)
    int32_t key;                     // +0x10 the key whose values 'values' holds, -1 none
    float *valuesBlock;              // +0x14
    float *values;                   // +0x18
    int32_t nextKey;                 // +0x1c the key whose values 'next' holds, -1 none
    float *nextBlock;                // +0x20
    float *next;                     // +0x24
    void *mask;                      // +0x28 the last mask (F1), or a reset request
    float *dequant;                  // +0x2c per value {min, scale, delta offset, delta scale}

    FnDeltaF* ConstructF1();                                     // 0x000fe280
    void EvalF1(float previous, float time, float *out);         // 0x000fe2b0
    bool EvalSQTMaskF1(float time, float *sqt, void *mask);      // 0x000fe2d0
    bool EvalWeightsF1(float time, float *weights);              // 0x000feaa0
    bool EvalVel2DF1(float time, float *velocity);               // 0x000feac0
    void InitBuffersF1();                                        // 0x000feae0
    bool EvalSQTF1(float time, float *sqt, void *mask);          // 0x000febd0
    FnDeltaF* ScalarDeleteF1(unsigned flags);                    // 0x000ff1e0
    void DestructF1();                                           // 0x000ff210
    void SetAnimMemoryMap(uint8_t *data);                        // 0x00100880 (both)
    bool GetLength(float *length);                               // 0x001008a0 (both)
    FnDeltaF* ScalarDeleteF3(unsigned flags);                    // 0x001008e0
    void DestructF3();                                           // 0x00100910
    FnDeltaF* ConstructF3();                                     // 0x000ff280
    void EvalF3(float previous, float time, float *out);         // 0x000ff2b0
    bool EvalSQTMaskF3(float time, float *sqt, void *mask);      // 0x000ff2d0
    bool EvalWeightsF3(float time, float *weights);              // 0x000ffce0
    bool EvalVel2DF3(float time, float *velocity);               // 0x000ffd00
    void InitBuffersF3();                                        // 0x000ffd20
    bool EvalSQTF3(float time, float *sqt, void *mask);          // 0x000fff10
};

struct DeltaF3Header {               // the F3 data's header (as F1's, counting 3-vectors)
    uint8_t* ConstantsPointer();                                 // 0x00100800
};
static_assert(sizeof(FnDeltaF) == 0x30, "FnDeltaF is 0x30 bytes");

int AnimTruncateF1(float value);                                 // 0x000ff270
int AnimTruncateF3(float value);                                 // 0x00100970

#endif // DRIVING_EAGL_ANIM_ANIMDELTAF_H_
