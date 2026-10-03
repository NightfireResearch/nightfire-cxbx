#ifndef DRIVING_EAGL_ANIM_FNANIM_H_
#define DRIVING_EAGL_ANIM_FNANIM_H_

// EAGLAnim's channel objects (docs/driving/eagl.md 2.11, 3.4): every FnAnim is a vtable, a stat word and a type id,
// built in EAGLAnim's pool by the factory from the type word at the start of its anim data. The objects keep the
// game's vtables (the game and the rest of EAGLAnim call through them); each method here is the original at the
// same address. Layouts are the Xbox's.

#include <stdint.h>
#include <stddef.h>

#ifndef _MSC_VER
#ifndef __fastcall
#define __fastcall __attribute__((fastcall))
#endif
#endif

// A virtual call through the object's own (game) vtable: thiscall, called as __fastcall with a dummy EDX.
template <typename R, typename... A>
static inline R AnimVCall(const void *object, int slot, A... args) {
    typedef R (__fastcall *Method)(const void *, int, A...);
    return ((Method)((void *const *)*(void *const *)object)[slot])(object, 0, args...);
}

enum FnAnimSlot {                    // the 15 FnAnim slots; FnAnimMemoryMap adds 15-17
    kSlotDelete = 0, kSlotTargetCheckSum, kSlotUseFPS, kSlotEval, kSlotGetLength, kSlotFindMatchTime, kSlotEvalSQT,
    kSlotEvalPhase, kSlotEvalVel2D, kSlotEvalEvent, kSlotEvalWeights, kSlotEvalState, kSlotFindTime,
    kSlotGetPhaseChan, kSlotGetAttributes, kSlotSetAnimMemoryMap, kSlotGetAnimMemoryMap, kSlotGetAnimMemoryMap2
};

enum FnAnimType {                    // the type word of the anim data = the factory's index
    kRawPose = 0, kRawEvent, kRawLinear, kCycle, kEventBlender, kGraft, kPoseBlender, kPoseMirror, kRunBlender,
    kTurnBlender, kDeltaLerp, kDeltaQuat, kKeyLerp, kKeyQuat, kPhase, kCompound, kRawState, kDeltaQ, kDeltaQFast,
    kDeltaSingleQ, kDeltaF3, kDeltaF1, kAnimTypeCount
};

struct FnAnim {                      // 0xc
    const void *vtable;
    uint32_t stat;
    uint32_t type;

    void Destruct();                                             // 0x000f72e0
    FnAnim* ScalarDelete(unsigned flags);                        // 0x000f7340
    uint16_t GetTargetCheckSum();                                // 0x000f72f0
    bool GetLength(float *length);                               // 0x000f7300
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x000f7310 (also FindTime)
    bool EvalEvent(float previous, float time, void **handlers, void *data);   // 0x000f7320
    void* GetAttributes();                                       // 0x000fb110
    bool NotImplemented(float a, void *b);                       // 0x000f70e0 (FindMatchTime, EvalPhase, ...)
};

struct FnAnimMemoryMap : FnAnim {    // 0x10
    uint8_t *anim;                   // +0x0c the anim data

    FnAnimMemoryMap* Construct();                                // 0x000faa80
    void Destruct();                                             // 0x000faaa0
    void SetAnimMemoryMap(uint8_t *data);                        // 0x000faab0
    uint8_t* GetAnimMemoryMap();                                 // 0x000faac0
    uint8_t* GetAnimMemoryMap2();                                // 0x000faad0
    uint16_t GetTargetCheckSum();                                // 0x000faae0
    FnAnimMemoryMap* ScalarDelete(unsigned flags);               // 0x000faaf0
    bool GetAttributeByte(uint16_t id, uint8_t *out);            // 0x000fab10
};

struct FnCompoundChannel : FnAnimMemoryMap {   // 0x18, type 15
    FnAnim **channels;               // +0x10, from the pool (NewBlock), made on first use
    uint8_t useFPS;                  // +0x14
    uint8_t fps;                     // +0x15, 0 until read from the attributes

    FnCompoundChannel* Construct();                              // 0x000f70b0
    void Destruct();                                             // 0x000fab60
    FnCompoundChannel* ScalarDelete(unsigned flags);             // 0x000f7140
    uint16_t GetTargetCheckSum();                                // 0x000f70f0
    bool GetLength(float *length);                               // 0x000f7100
    void SetAnimMemoryMap(uint8_t *data);                        // 0x000fac00
    void InitSubChannels();                                      // 0x000fb050
    void Eval(float previous, float time, float *out);           // 0x000fb0a0
    bool EvalEvent(float previous, float time, void **handlers, void *data);   // 0x000fac70
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x000facf0
    bool EvalWeights(float time, float *weights);                // 0x000fad60
    bool EvalVel2D(float time, float *velocity);                 // 0x000fadc0
    bool EvalState(float time, void *state);                     // 0x000fae20
    bool FindTime(void *test, float from, float *time);          // 0x000fae80
    bool EvalPhase(float time, void *phase);                     // 0x000faf00
    void* GetPhaseChan();                                        // 0x000faf90
    void UseFPS(bool use);                                       // 0x000fafd0
    uint8_t GetFPS();                                            // 0x000fb000
    void* GetAttributes();                                       // 0x000fb040
};

struct FnRawPoseChannel : FnAnimMemoryMap {    // 0x14, type 0
    uint8_t interpolate;             // +0x10

    FnRawPoseChannel* Construct();                               // 0x000f7360
    void Destruct();                                             // 0x000f73d0
    FnRawPoseChannel* ScalarDelete(unsigned flags);              // 0x000f73a0
    bool GetLength(float *length);                               // 0x000f7380
    void Eval(float previous, float time, float *out);           // 0x000fb120
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x000fb140
};

struct FnRawEventChannel : FnAnimMemoryMap {   // 0x18, type 1
    int32_t lastIndex;               // +0x10
    float lastTime;                  // +0x14

    FnRawEventChannel* Construct();                              // 0x000f73e0
    void Destruct();                                             // 0x000f74b0
    FnRawEventChannel* ScalarDelete(unsigned flags);             // 0x000f7480
    void SetAnimMemoryMap(uint8_t *data);                        // 0x000f7410
    bool EvalEvent(float previous, float time, void **handlers, void *data);   // 0x000f7430
    void Eval(float previous, float time, float *out);           // 0x000f7460
};

struct FnRawLinearChannel : FnAnimMemoryMap {  // 0x14, type 2
    uint8_t interpolate;             // +0x10

    FnRawLinearChannel* Construct();                             // 0x000f74c0
    void Destruct();                                             // 0x000f76a0
    FnRawLinearChannel* ScalarDelete(unsigned flags);            // 0x000f7670
    void Eval(float previous, float time, float *out);           // 0x000f74e0
    bool GetLength(float *length);                               // 0x000f7650
};

// The raw linear data (thiscall on the data): u16 type, u16 checksum, u16 channels, u16 frames, u16 index[channels]
// (padded to even), then frames x channels floats.
struct RawLinearData {
    void Eval(float time, float *out, bool interpolate);         // 0x000f7500
    void Copy(int frame, float *out);                            // 0x000f75a0
    void Lerp(float t, int frame0, int frame1, float *out);      // 0x000f75e0
};

struct FnKeyDeltaChan : FnAnimMemoryMap {      // 0x18: KeyLerp 12, KeyQuat 13
    int32_t key;                     // +0x10, -1 until decoded
    float *values;                   // +0x14, from the pool

    void Destruct();                                             // 0x000fb890
    void DestructThunk();                                        // 0x000f82c0
    FnKeyDeltaChan* ScalarDelete(unsigned flags);                // 0x000f76b0
    FnKeyDeltaChan* ScalarDeleteLerp(unsigned flags);            // 0x000f8290
    FnKeyDeltaChan* ConstructLerp();                             // 0x000f76e0
    FnKeyDeltaChan* ConstructQuat();                             // 0x000f7710
    void SetAnimMemoryMap(uint8_t *data);                        // 0x000fb8c0
    void EvalToPrevValues(int key);                              // 0x000fb930
    int FindLowerKey(float time);                                // 0x000fb960
    bool GetLength(float *length);                               // 0x000fbe10
    void EvalQuat(float previous, float time, float *out);       // 0x000fbb40 (FnKeyQuatChan::Eval)
    bool EvalSQTQuat(float time, float *sqt, void *mask);        // 0x000fbb60 (FnKeyQuatChan::EvalSQT)
    void EvalLerp(float previous, float time, float *out);       // 0x000fba00 (FnKeyLerpChan::Eval)
    bool EvalSQTLerp(float time, float *sqt, void *mask);        // 0x000fba20 (FnKeyLerpChan::EvalSQT)
};

struct FnDeltaChan : FnAnimMemoryMap {         // 0x18: DeltaLerp 10, DeltaQuat 11
    int32_t frame;                   // +0x10, -1 until decoded
    float *values;                   // +0x14, from the pool

    void Destruct();                                             // 0x000fb360
    void DestructThunk();                                        // 0x000f8280
    FnDeltaChan* ScalarDelete(unsigned flags);                   // 0x000f7740
    FnDeltaChan* ScalarDeleteQuat(unsigned flags);               // 0x000f8250
    FnDeltaChan* ConstructLerp();                                // 0x000f81f0
    FnDeltaChan* ConstructQuat();                                // 0x000f8220
    bool GetLength(float *length);                               // 0x000f7770
    void SetAnimMemoryMap(uint8_t *data);                        // 0x000fb390
    void DecodeFrame(int frame);                                 // 0x000fb400
    void EvalQuat(float previous, float time, float *out);       // 0x000fb670 (FnDeltaQuatChan::Eval)
    bool EvalSQTQuat(float time, float *sqt, void *mask);        // 0x000fb780 (FnDeltaQuatChan::EvalSQT)
    void EvalLerp(float previous, float time, float *out);       // 0x000fb440 (FnDeltaLerpChan::Eval)
    bool EvalSQTLerp(float time, float *sqt, void *mask);        // 0x000fb530 (FnDeltaLerpChan::EvalSQT)
    bool EvalWeightsLerp(float time, float *weights);            // 0x000fb630 (FnDeltaLerpChan::EvalWeights)
    bool EvalVel2DLerp(float time, float *velocity);             // 0x000fb650 (FnDeltaLerpChan::EvalVel2D)
};

struct FnGraft : FnAnim {            // 0x14, type 5: Eval runs every sub-anim
    FnAnim **anims;                  // +0x0c
    int32_t count;                   // +0x10

    FnGraft* ScalarDelete(unsigned flags);                       // 0x000f7790
    void Eval(float previous, float time, float *out);           // 0x000f77c0
};

struct FnCycle : FnAnim {            // 0x1c, type 3: time wrapped into [start, end] around a sub-anim
    float start;                     // +0x0c
    float end;                       // +0x10
    float period;                    // +0x14
    FnAnim *anim;                    // +0x18

    double Wrap(float time);                                     // 0x000f79b0 (answers in ST0)
    void Eval(float previous, float time, float *out);           // 0x000f7970
    bool EvalEvent(float previous, float time, void **handlers, void *data);   // 0x000f7a50
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x000f7a90
    bool EvalPhase(float time, void *phase);                     // 0x000f7ac0
};

struct FnPoseBlender : FnAnim {      // 0x80, type 6 (the blending: AnimPoseBlend.cpp)
    struct Skeleton *skeleton;       // +0x0c for the still poses
    float *poseA;                    // +0x10 pose buffers the two anims evaluate into while blending
    float *poseB;                    // +0x14
    FnAnim *animA;                   // +0x18 up to start
    FnAnim *animB;                   // +0x1c from end
    float offsetA;                   // +0x20 taken off the time for animA
    float offsetB;                   // +0x24
    int32_t bone;                    // +0x28 the bone animB is aligned on, -1 none
    uint32_t unknown2c;              // +0x2c
    float align[16];                 // +0x30 the Transform applied to animB's bone
    float start;                     // +0x70
    float duration;                  // +0x74
    float end;                       // +0x78 start + duration
    uint8_t stillA;                  // +0x7c fill poseA with the still pose first
    uint8_t stillB;                  // +0x7d
    uint8_t pad7e[2];

    FnPoseBlender* Construct();                                  // 0x000f7810
    FnPoseBlender* ScalarDelete(unsigned flags);                 // 0x000f7840
    void SetAligned(FnAnim *a, float offA, FnAnim *b, float offB, const class Transform *transform, int alignBone,
                    float startTime, float length);              // 0x000fc4c0
    void SetUnaligned(FnAnim *a, float offA, FnAnim *b, float offB, float startTime, float length);   // 0x000fc520
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x000fc560
    void Eval(float previous, float time, float *out);           // 0x000fca70
    static void XZProjectAlign(const class Transform *a, const class Transform *b, class Transform *out);  // 0x000fcde0
    void Set(FnAnim *a, float offA, FnAnim *b, float offB, int alignBone, float previous, float startTime,
             float length);                                      // 0x000fce70
};

struct FnPoseMirror : FnAnim {       // 0x1c, type 7
    FnAnim *anim;                    // +0x0c
    void *skeleton;                  // +0x10
    float *pose;                     // +0x14
    uint8_t flags;                   // +0x18
    uint8_t enabled;                 // +0x19

    FnPoseMirror* Construct();                                   // 0x000f7870
    FnPoseMirror* ScalarDelete(unsigned flags);                  // 0x000f7940 (also the cycle's)
    void Eval(float previous, float time, float *out);           // 0x000f78a0
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x000f78f0
};

struct FnEventBlender : FnAnim {     // 0x2c, type 4 (Set/Eval: AnimPoseBlend.cpp)
    FnAnim *animA;                   // +0x0c up to start
    FnAnim *animB;                   // +0x10 from end
    float offsetA;                   // +0x14
    float offsetB;                   // +0x18
    float start;                     // +0x1c
    float end;                       // +0x20 start + duration
    float duration;                  // +0x24
    int32_t mode;                    // +0x28 between start and end: 0 A, 1 B, else both

    FnEventBlender* ScalarDelete(unsigned flags);                // 0x000f7af0
    void Set(FnAnim *a, FnAnim *b, float offA, float offB, float startTime, float length, int blendMode);  // 0x000fcfa0
    void Eval(float previous, float time, float *out);           // 0x000fcfe0
};

struct FnPhaseChan : FnAnimMemoryMap {         // 0x18, type 14
    uint16_t index;                  // +0x10, 0 (constructor, SetAnimMemoryMap)
    uint16_t count;                  // +0x12, the data's u16 at +6
    uint8_t notFlag1;                // +0x14, !(data flags & 1)
    uint8_t step;                    // +0x15, frames a sample: 1, 2, 4 or 8 from the data flags

    FnPhaseChan* Construct();                                    // 0x000f7b20
    void* GetPhaseChan();                                        // 0x000f7b50
    void Destruct();                                             // 0x000f8300
    FnPhaseChan* ScalarDelete(unsigned flags);                   // 0x000f82d0
    bool GetLength(float *length);                               // 0x000fd4a0
    void Eval(float previous, float time, float *out);           // 0x000fd4c0
    void SetAnimMemoryMap(uint8_t *data);                        // 0x000fd670
};

struct FnRawStateChan : FnAnimMemoryMap {      // 0x14, type 16
    int32_t frame;                   // +0x10 the frame last decoded

    void Decode(uint8_t *data, uint8_t *out);                    // 0x000fd730
    bool EvalState(float time, void *state);                     // 0x000fd840
    bool FindTime(void *test, float from, float *time);          // 0x000fd9c0 (vtable slot 12)
    FnRawStateChan* Construct();                                 // 0x000f7b60
    bool GetLength(float *length);                               // 0x000f7b80
    void Eval(float previous, float time, float *out);           // 0x000f7ba0
    FnRawStateChan* ScalarDelete(unsigned flags);                // 0x000f7bc0
    void Destruct();                                             // 0x000f7bf0
};

// EAGLAnim's pool (MemoryPoolManager): blocks by anim type from free lists, and size-classed blocks.
void* AnimPool_NewBlockByIdx(uint16_t type);                     // 0x000f7c40
void* AnimPool_NewBlock(uint32_t size);                          // 0x000f7c90
void AnimPool_ResetPool();                                       // 0x000f7cd0
void AnimPool_Init(uint32_t size);                               // 0x000f7d00
void AnimPool_Cleanup();                                         // 0x000f7d50
void AnimPool_InitAnimMemoryMap(uint8_t *data);                  // 0x000f7d70
FnAnim* AnimPool_Construct(uint16_t type);                       // 0x000f7de0 (the factory)
FnAnim* AnimPool_NewFnAnim(uint8_t *data);                       // 0x00014210
void AnimPool_DeleteFnAnim(FnAnim *anim);                        // 0x000141e0

#endif // DRIVING_EAGL_ANIM_FNANIM_H_
