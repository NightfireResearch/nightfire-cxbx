#ifndef DRIVING_EAGL_ANIM_ANIMDELTAF_H_
#define DRIVING_EAGL_ANIM_ANIMDELTAF_H_

// FnDeltaF1 and FnDeltaF3, the keyframe-block channels most of the disc's anims are made of. See AnimDeltaF.cpp.

#include "FnAnim.h"

// The F1 and F3 data: this header, a range record per value (F3: per 3-vector), the key blocks, the constants.
struct DeltaFData : AnimData {
    uint16_t *index;                 // +0x04 [count] each value's output index (F3: its x's)
    uint16_t *times;                 // +0x08 [keys - 1] key k's frame at times[k - 1]; NULL: a key a frame
    uint16_t keys;                   // +0x0c
    uint16_t count;                  // +0x0e values (F3: vectors)
    uint8_t shift;                   // +0x10 2^shift keys a block
    uint8_t constants;               // +0x11 values (F3: vectors) that never change, after the blocks
    uint16_t unknown12;
};
static_assert(sizeof(DeltaFData) == 0x14, "the DeltaF header is 0x14 bytes");

struct DeltaF1Range {                // 12, per value
    float min;                       // +0x00
    float scale;                     // +0x04
    uint16_t deltaOffset;            // +0x08
    uint16_t deltaScale;             // +0x0a
};
static_assert(sizeof(DeltaF1Range) == 12, "a DeltaF1 range is 12 bytes");

struct DeltaF3Range {                // 0x24, per 3-vector
    float min[3];                    // +0x00
    float scale[3];                  // +0x0c
    uint16_t deltaOffset[3];         // +0x18
    uint16_t deltaScale[3];          // +0x1e
};
static_assert(sizeof(DeltaF3Range) == 0x24, "a DeltaF3 range is 0x24 bytes");

// A value is base * scale + min at a block's first key, and adds delta * deltaScale + deltaOffset per key after it.
struct DeltaF1Dequant {              // per value
    float min;
    float scale;
    float deltaOffset;
    float deltaScale;
};

struct DeltaF3Dequant {              // per 3-vector
    float min[3];
    float scale[3];
    float deltaOffset[3];
    float deltaScale[3];
};

struct FnDeltaF : FnAnimMemoryMap {  // 0x30, FnDeltaF1 (type 21) and FnDeltaF3 (type 20)
    int32_t key;                     // +0x10 the key whose values 'values' holds, -1 none
    float *valuesBlock;              // +0x14
    float *values;                   // +0x18 (F3: 4 floats a vector)
    int32_t nextKey;                 // +0x1c the key whose values 'next' holds, -1 none
    float *nextBlock;                // +0x20
    float *next;                     // +0x24
    void *mask;                      // +0x28 the last mask (F1), or a reset request
    union {                          // +0x2c from the pool, made with the value buffers
        DeltaF1Dequant *dequant1;    // F1, per value
        DeltaF3Dequant *dequant3;    // F3, per vector
    };

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
static_assert(sizeof(FnDeltaF) == 0x30, "FnDeltaF is 0x30 bytes");

struct DeltaF3Header : DeltaFData {  // the F3 data (thiscall on it)
    uint8_t* ConstantsPointer();                                 // 0x00100800
};

int AnimTruncateF1(float value);                                 // 0x000ff270
int AnimTruncateF3(float value);                                 // 0x00100970

#endif // DRIVING_EAGL_ANIM_ANIMDELTAF_H_
