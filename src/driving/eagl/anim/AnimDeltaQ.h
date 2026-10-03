#ifndef DRIVING_EAGL_ANIM_ANIMDELTAQ_H_
#define DRIVING_EAGL_ANIM_ANIMDELTAQ_H_

// FnDeltaSingleQ, FnDeltaQFast and FnDeltaQ, the quaternion keyframe-block channels no shipped anim uses
// (docs/driving/eagl.md 3.4): provisional, untested ports. See AnimDeltaQ.cpp. Field and helper names follow NFS
// Most Wanted's EAGL4Anim headers (docs/driving/eagl-prior-art.md), whose layouts match these objects.

#include "FnAnim.h"

// ---- FnDeltaSingleQ (type 19): one rotation axis per bone, between a fixed pre- and post-rotation

struct DeltaSingleQMinRangef {       // 0x1c, a range record unquantised
    float const0, const1;            // +0x00 the pre/post angles (radians)
    float min[2];                    // +0x08
    float range[2];                  // +0x10
    uint8_t index;                   // +0x18 the axis: 0 x, 1 y (pre and post), else z
};
static_assert(sizeof(DeltaSingleQMinRangef) == 0x1c, "DeltaSingleQMinRangef is 0x1c bytes");

struct DeltaSingleQMinRange {        // 0xe: u16 const0, const1, min[2], range[2], index
    void UnQuantize(DeltaSingleQMinRangef *out);                 // 0x00101aa0 (name from MW)
};

struct DeltaSingleQPhysical {        // 2 bytes: u8 v (the axis component), u8 w
    void UnQuantize(int index, float *q);                        // 0x00101b50 (name from MW)
};

struct DeltaSingleQDelta {           // 1 byte: v in the high nibble, w in the low
    void UnQuantize(const DeltaSingleQMinRangef *range, float *q);   // 0x00101be0 (name from MW)
};

struct FnDeltaSingleQ : FnAnimMemoryMap {      // 0x30
    uint8_t *minRanges;              // +0x10 the data's range records (0xe bytes each)
    uint8_t *bins;                   // +0x14
    int32_t binSize;                 // +0x18
    int32_t prevKey;                 // +0x1c the key prevQs holds, -1 none
    float *prevQBlock;               // +0x20
    float *prevQs;                   // +0x24 a quaternion per bone
    float *preMultQs;                // +0x28
    float *postMultQs;               // +0x2c

    FnDeltaSingleQ* Construct();                                 // 0x00100980
    void Destruct();                                             // 0x001009c0
    void SetAnimMemoryMap(uint8_t *data);                        // 0x00100a20
    bool GetLength(float *length);                               // 0x00100a30
    void Eval(float previous, float time, float *out);           // 0x00100a80
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x00100aa0
    bool EvalSQTMasked(float time, void *mask, float *sqt);      // 0x00100be0 (slot 18)
    void InitBuffersAsRequired();                                // 0x00101c90 (name from MW)
    FnDeltaSingleQ* ScalarDelete(unsigned flags);                // 0x00101f00
};
static_assert(sizeof(FnDeltaSingleQ) == 0x30, "FnDeltaSingleQ is 0x30 bytes");

// ---- FnDeltaQFast (type 18): 12-bit keys, 6-bit deltas, linear interpolation between keys

struct DeltaQFastMinRange {          // 0x10: u16 min[4], range[4]
    void UnQuantize(float *out);                                 // 0x00102e30 (8 floats: min, range)
};

struct DeltaQFastPhysical {          // 6 bytes: x, y, z in 12 bits each, w from the three low nibbles
    void UnQuantize(float *q);                                   // 0x00102f00
};

struct DeltaQFastDelta {             // 3 bytes: x, y, z in 6 bits each, w from the three low bit pairs
    void UnQuantize(const float *range, float *q);               // 0x00102fb0 (name from MW)
};

struct DeltaQFastHeader {            // the data: u16 type, checksum, keys; u8 bones, consts; u16 *times;
                                     // u8 *boneIdx; u8 binLengthPower; ranges at +0x12
    uint8_t* GetConstPhysical();                                 // 0x00103060
    void GetArrays(uint8_t **minRanges, uint8_t **bins, uint8_t **constBoneIdxs, uint8_t **constPhysical);
                                                                 // 0x00103230 (name from MW)
};

struct FnDeltaQFast : FnAnimMemoryMap {        // 0x40
    float *minRangesf;               // +0x10 8 floats a bone, from the pool
    uint8_t *bins;                   // +0x14
    int32_t binSize;                 // +0x18
    int32_t prevKey;                 // +0x1c
    float *prevQBlock;               // +0x20
    float *prevQs;                   // +0x24
    int32_t nextKey;                 // +0x28
    float *nextQBlock;               // +0x2c
    float *nextQs;                   // +0x30
    uint8_t *constBoneIdxs;          // +0x34
    uint8_t *constPhysical;          // +0x38
    void *boneMask;                  // +0x3c the last mask

    FnDeltaQFast* Construct();                                   // 0x00101f40
    void Destruct();                                             // 0x00101f90
    bool GetLength(float *length);                               // 0x00101ff0
    void Eval(float previous, float time, float *out);           // 0x00102040
    void UpdateNextQs(uint8_t *data, int ceilKey, int floorBin, int floorDelta);   // 0x00102060
    void AddDeltaMask(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs, void *mask);
                                                                 // 0x001021a0 (name from MW)
    void SubDeltaMask(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs, void *mask);
                                                                 // 0x00102280 (name from MW)
    void UpdateNextQsMask(uint8_t *data, int ceilKey, int floorBin, int floorDelta, void *mask);   // 0x00102370
    void SetAnimMemoryMap(uint8_t *data);                        // 0x00102500
    bool EvalSQTMask(float time, float *sqt, void *mask);        // 0x00102510
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x001029f0
    void AddDelta(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs);   // 0x001030c0 (MW)
    void SubDelta(uint8_t *bin, uint8_t *data, int prevDelta, int floorDelta, float *qs);   // 0x00103170 (MW)
    FnDeltaQFast* ScalarDelete(unsigned flags);                  // 0x001032b0
    void InitBuffers();                                          // 0x001032e0 (name from MW)
};
static_assert(sizeof(FnDeltaQFast) == 0x40, "FnDeltaQFast is 0x40 bytes");

// ---- FnDeltaQ (type 17): 15-bit keys, 7/8-bit deltas, w recovered from xyz, normalised lerp between keys

struct DeltaQMinRange {              // 0xc: u16 min[3], range[3]
    void UnQuantize(float *out);                                 // 0x00103fa0 (name from MW; 6 floats)
};

struct DeltaQPhysical {              // 6 bytes: x in 15 bits over w's sign bit, y, z
    void UnQuantize(float *q);                                   // 0x001040a0 (name from MW)
};

struct DeltaQHeader {                // as DeltaQFastHeader, with 0xc-byte ranges
    uint8_t* GetConstPhysical();                                 // 0x00104120 (name from MW)
    void GetArrays(uint8_t **minRanges, uint8_t **bins, uint8_t **constBoneIdxs, uint8_t **constPhysical);
                                                                 // 0x00104180 (name from MW)
};

struct FnDeltaQ : FnAnimMemoryMap {  // 0x30
    uint8_t *minRanges;              // +0x10 the data's range records
    uint8_t *bins;                   // +0x14 0 until the buffers are made
    int32_t binSize;                 // +0x18
    int32_t prevKey;                 // +0x1c
    float *prevQBlock;               // +0x20
    float *prevQs;                   // +0x24
    uint8_t *constBoneIdxs;          // +0x28
    uint8_t *constPhysical;          // +0x2c

    FnDeltaQ* Construct();                                       // 0x001033b0
    void Destruct();                                             // 0x001033f0
    void SetAnimMemoryMap(uint8_t *data);                        // 0x00103420
    bool GetLength(float *length);                               // 0x00103430
    void Eval(float previous, float time, float *out);           // 0x00103480
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x001034a0
    bool EvalSQTMasked(float time, void *mask, float *sqt);      // 0x001034c0 (slot 18)
    FnDeltaQ* ScalarDelete(unsigned flags);                      // 0x00104200
    void InitBuffersAsRequired();                                // 0x00104230 (name from MW)
};
static_assert(sizeof(FnDeltaQ) == 0x30, "FnDeltaQ is 0x30 bytes");

// ---- free helpers (cdecl)

void QuatMultXxYxZ(const float *a, const float *b, const float *c, float *out);   // 0x00100ac0
void QuatMultXxQ(const float *a, const float *b, float *out);    // 0x00100b40 (PS2 neighbour's name)
void QuatMultQxZ(const float *a, const float *b, float *out);    // 0x00100b90 (PS2 neighbour's name)
void AnimQuatNLerp(float t, const float *a, const float *b, float *out);   // 0x00101e40 (invented name)
void DeltaQRecoverW(int sign, float *q);                         // 0x00104030 (name from MW)
int AnimTruncateSingleQ(float value);                            // 0x00101f30 (invented name, no callers)
int AnimTruncateQFast(float value);                              // 0x001033a0 (invented name, no callers)
int AnimTruncateQ(float value);                                  // 0x001042a0 (invented name, no callers)
void AnimVec2LengthEax();                                        // 0x001042b0 (invented name, no callers;
                                                                 // the vector in EAX, the length in ST0)

#endif // DRIVING_EAGL_ANIM_ANIMDELTAQ_H_
