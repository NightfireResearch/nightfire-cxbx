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

struct DeltaSingleQMinRange {        // 0xe, a range record in the data
    uint16_t const0, const1;         // +0x00 the pre/post angles
    uint16_t min[2];                 // +0x04 the axis component's and w's
    uint16_t range[2];               // +0x08
    uint16_t index;                  // +0x0c the axis (UnQuantize keeps its low byte)

    void UnQuantize(DeltaSingleQMinRangef *out);                 // 0x00101aa0 (name from MW)
};
static_assert(sizeof(DeltaSingleQMinRange) == 0xe, "DeltaSingleQMinRange is 0xe bytes");

struct DeltaSingleQPhysical {        // a bone's quaternion at the start of a bin
    uint8_t v;                       // +0x00 the axis component
    uint8_t w;                       // +0x01

    void UnQuantize(int index, float *q);                        // 0x00101b50 (name from MW)
};
static_assert(sizeof(DeltaSingleQPhysical) == 2, "DeltaSingleQPhysical is 2 bytes");

struct DeltaSingleQDelta {           // a bone's delta from one key to the next
    uint8_t vw;                      // v in the high nibble, w in the low

    void UnQuantize(const DeltaSingleQMinRangef *range, float *q);   // 0x00101be0 (name from MW)
};

// The data: a range record per bone, then the bins: per bone a physical value, then a row of deltas per key after
// the first.
struct DeltaSingleQData : AnimData {
    uint16_t keys;                   // +0x04
    uint8_t bones;                   // +0x06
    uint8_t binLengthPower;          // +0x07 keys a bin: 1 << binLengthPower
    uint16_t *times;                 // +0x08 [keys - 1] (key k at times[k - 1]), or NULL: a key a frame
    uint8_t *boneIdx;                // +0x0c [bones] each bone's index in the pose
    DeltaSingleQMinRange ranges[1];  // +0x10 [bones], then the bins
};
static_assert(offsetof(DeltaSingleQData, ranges) == 0x10, "the single-axis ranges are at +0x10");

struct FnDeltaSingleQ : FnAnimMemoryMap {      // 0x30
    DeltaSingleQMinRange *minRanges; // +0x10 the data's range records
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

struct DeltaQFastMinRangef {         // 0x20, a range record unquantised
    float min[4];                    // +0x00
    float range[4];                  // +0x10
};
static_assert(sizeof(DeltaQFastMinRangef) == 0x20, "DeltaQFastMinRangef is 0x20 bytes");

struct DeltaQFastMinRange {          // 0x10, a range record in the data
    uint16_t min[4];                 // +0x00
    uint16_t range[4];               // +0x08

    void UnQuantize(DeltaQFastMinRangef *out);                   // 0x00102e30
};
static_assert(sizeof(DeltaQFastMinRange) == 0x10, "DeltaQFastMinRange is 0x10 bytes");

struct DeltaQFastPhysical {          // x, y, z in the high 12 bits each, w from the three low nibbles
    uint16_t xyz[3];

    void UnQuantize(float *q);                                   // 0x00102f00
};
static_assert(sizeof(DeltaQFastPhysical) == 6, "DeltaQFastPhysical is 6 bytes");

struct DeltaQFastDelta {             // x, y, z in the high 6 bits each, w from the three low bit pairs
    uint8_t xyz[3];

    void UnQuantize(const DeltaQFastMinRangef *range, float *q);   // 0x00102fb0 (name from MW)
};
static_assert(sizeof(DeltaQFastDelta) == 3, "DeltaQFastDelta is 3 bytes");

// The data: a range record per bone from +0x12, the bins (per bone a physical quaternion, then a row of deltas per
// key after the first), the constant bones' indexes and, on a 2-byte boundary, their physical quaternions.
struct DeltaQFastHeader : AnimData {
    uint16_t keys;                   // +0x04
    uint8_t bones;                   // +0x06 animated bones
    uint8_t constBones;              // +0x07
    uint16_t *times;                 // +0x08 [keys - 1] (key k at times[k - 1]), or NULL: a key a frame
    uint8_t *boneIdx;                // +0x0c [bones] each bone's index in the pose
    uint8_t binLengthPower;          // +0x10 keys a bin: 1 << binLengthPower
    uint8_t unknown11;
    DeltaQFastMinRange ranges[1];    // +0x12 [bones]

    DeltaQFastPhysical* GetConstPhysical();                      // 0x00103060
    void GetArrays(DeltaQFastMinRange **minRanges, uint8_t **bins, uint8_t **constBoneIdxs,
                   DeltaQFastPhysical **constPhysical);          // 0x00103230 (name from MW)
};
static_assert(offsetof(DeltaQFastHeader, ranges) == 0x12, "the DeltaQFast ranges are at +0x12");

struct FnDeltaQFast : FnAnimMemoryMap {        // 0x40
    DeltaQFastMinRangef *minRangesf; // +0x10 [bones] from the pool
    uint8_t *bins;                   // +0x14
    int32_t binSize;                 // +0x18
    int32_t prevKey;                 // +0x1c
    float *prevQBlock;               // +0x20
    float *prevQs;                   // +0x24
    int32_t nextKey;                 // +0x28
    float *nextQBlock;               // +0x2c
    float *nextQs;                   // +0x30
    uint8_t *constBoneIdxs;          // +0x34
    DeltaQFastPhysical *constPhysical;   // +0x38
    void *boneMask;                  // +0x3c the last mask

    FnDeltaQFast* Construct();                                   // 0x00101f40
    void Destruct();                                             // 0x00101f90
    bool GetLength(float *length);                               // 0x00101ff0
    void Eval(float previous, float time, float *out);           // 0x00102040
    void UpdateNextQs(DeltaQFastHeader *data, int ceilKey, int floorBin, int floorDelta);   // 0x00102060
    void AddDeltaMask(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs, void *mask);
                                                                 // 0x001021a0 (name from MW)
    void SubDeltaMask(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs, void *mask);
                                                                 // 0x00102280 (name from MW)
    void UpdateNextQsMask(DeltaQFastHeader *data, int ceilKey, int floorBin, int floorDelta, void *mask);
                                                                 // 0x00102370
    void SetAnimMemoryMap(uint8_t *data);                        // 0x00102500
    bool EvalSQTMask(float time, float *sqt, void *mask);        // 0x00102510
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x001029f0
    void AddDelta(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs);
                                                                 // 0x001030c0 (name from MW)
    void SubDelta(uint8_t *bin, DeltaQFastHeader *data, int prevDelta, int floorDelta, float *qs);
                                                                 // 0x00103170 (name from MW)
    FnDeltaQFast* ScalarDelete(unsigned flags);                  // 0x001032b0
    void InitBuffers();                                          // 0x001032e0 (name from MW)
};
static_assert(sizeof(FnDeltaQFast) == 0x40, "FnDeltaQFast is 0x40 bytes");

// ---- FnDeltaQ (type 17): 15-bit keys, 7/8-bit deltas, w recovered from xyz, normalised lerp between keys

struct DeltaQMinRangef {             // 0x18, a range record unquantised
    float min[3];                    // +0x00
    float range[3];                  // +0x0c
};
static_assert(sizeof(DeltaQMinRangef) == 0x18, "DeltaQMinRangef is 0x18 bytes");

struct DeltaQMinRange {              // 0xc, a range record in the data
    uint16_t min[3];                 // +0x00
    uint16_t range[3];               // +0x06

    void UnQuantize(DeltaQMinRangef *out);                       // 0x00103fa0 (name from MW)
};
static_assert(sizeof(DeltaQMinRange) == 0xc, "DeltaQMinRange is 0xc bytes");

struct DeltaQPhysical {              // x in the high 15 bits over w's sign bit, y, z
    uint16_t xyz[3];

    void UnQuantize(float *q);                                   // 0x001040a0 (name from MW)
};
static_assert(sizeof(DeltaQPhysical) == 6, "DeltaQPhysical is 6 bytes");

struct DeltaQDelta {                 // x in the high 7 bits over w's sign bit, y, z
    uint8_t xyz[3];
};
static_assert(sizeof(DeltaQDelta) == 3, "DeltaQDelta is 3 bytes");

struct DeltaQHeader : AnimData {     // as DeltaQFastHeader, with DeltaQ's range records
    uint16_t keys;                   // +0x04
    uint8_t bones;                   // +0x06 animated bones
    uint8_t constBones;              // +0x07
    uint16_t *times;                 // +0x08 [keys - 1] (key k at times[k - 1]), or NULL: a key a frame
    uint8_t *boneIdx;                // +0x0c [bones] each bone's index in the pose
    uint8_t binLengthPower;          // +0x10 keys a bin: 1 << binLengthPower
    uint8_t unknown11;
    DeltaQMinRange ranges[1];        // +0x12 [bones]

    DeltaQPhysical* GetConstPhysical();                          // 0x00104120 (name from MW)
    void GetArrays(DeltaQMinRange **minRanges, uint8_t **bins, uint8_t **constBoneIdxs,
                   DeltaQPhysical **constPhysical);              // 0x00104180 (name from MW)
};
static_assert(offsetof(DeltaQHeader, ranges) == 0x12, "the DeltaQ ranges are at +0x12");

struct FnDeltaQ : FnAnimMemoryMap {  // 0x30
    DeltaQMinRange *minRanges;       // +0x10 the data's range records
    uint8_t *bins;                   // +0x14 0 until the buffers are made
    int32_t binSize;                 // +0x18
    int32_t prevKey;                 // +0x1c
    float *prevQBlock;               // +0x20
    float *prevQs;                   // +0x24
    uint8_t *constBoneIdxs;          // +0x28
    DeltaQPhysical *constPhysical;   // +0x2c

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
