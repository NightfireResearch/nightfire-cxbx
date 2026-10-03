#ifndef DRIVING_EAGL_ANIM_ANIMLOCOBLEND_H_
#define DRIVING_EAGL_ANIM_ANIMLOCOBLEND_H_

// FnRunBlender (type 8) and FnTurnBlender (type 9), EAGLAnim's locomotion blenders: never built by the disc's data
// (docs/driving/eagl.md 3.4), so provisional ports. See AnimLocoBlend.cpp. Names are the PS2 symbol sheet's; field
// names are NFS Most Wanted's EAGL4Anim headers (same lineage, layouts checked against the Xbox code).

#include "FnAnim.h"

struct Skeleton;

struct PhaseChanData;                // a phase channel's data (AnimMisc.h), as GetPhaseChan answers it

struct MatchPhaseInput {             // FindMatchTime's input (MW's MatchPhaseInput)
    float angle;                     // +0x00
    float dAngle;                    // +0x04 the direction the phase must be moving in
    float searchLength;              // +0x08 > 0: search no further than this
};

struct FnRunBlender : FnAnim {       // 0x80, type 8: blends neighbouring run cycles by a weight (or speed)
    uint8_t **anims;                 // +0x0c [numAnims] anim data, from the pool
    PhaseChanData **phases;          // +0x10 [numAnims], from the pool
    uint8_t **vels;                  // +0x14 [numAnims] velocity anim data, or NULL
    FnAnim *fnAnims[2];              // +0x18 the two anims blended, built from anims[idx], anims[idx + 1]
    FnAnim *fnVelAnims[2];           // +0x20 the same from vels
    float weight;                    // +0x28 0..1 between fnAnims[0] and [1]
    int32_t numAnims;                // +0x2c
    int32_t idx;                     // +0x30 -100 until SetWeight
    Skeleton *skeleton;              // +0x34
    float alignFrame[2];             // +0x38
    float cycles[2];                 // +0x40 cycle lengths
    float freq;                      // +0x48
    float prevTime;                  // +0x4c
    float offset;                    // +0x50
    int32_t cycleIdx;                // +0x54 the cycle alignQ was last aligned for, -100 / -1
    float alignQ[4];                 // +0x58
    uint8_t init;                    // +0x68
    float rootQ[4];                  // +0x6c (MW's; unused here)
    float *speeds;                   // +0x7c [numAnims] from the pool, when vels

    FnRunBlender* Construct();                                   // 0x00105000
    void Destruct();                                             // 0x00105050
    FnRunBlender* ScalarDelete(unsigned flags);                  // 0x00106300
    void Eval(float previous, float time, float *pose);          // 0x001051a0
    void SetWeight(float w);                                     // 0x001051c0
    void SetAnims(Skeleton *s, int count, uint8_t **animData, uint8_t **phaseData, uint8_t **velData);  // 0x00105500
    double CycleTime(float t, float startTime, float endTime);   // 0x00105670 (answers in ST0)
    int ComputeCycleIdx(float t, float startTime, float endTime);   // 0x00105710
    bool EvalPhase(float time, float *phase);                    // 0x00105780
    bool FindMatchTime(const MatchPhaseInput *input, float *time);  // 0x00105790
    void ComputeAlignQ(const float *v1, const float *v2, float *q);  // 0x00105960
    void AlignRootQ(float *sqt);                                 // 0x00105a20
    void AlignVel(float *vel);                                   // 0x00105a60
    bool BlendVel(float t0, float t1, float *vel);               // 0x00105ab0
    bool BlendFacing(float t0, float t1, float *facing);         // 0x00105bd0
    void SetSpeed(float s);                                      // 0x00105d00
    void GetMinMaxSpeed(float *min, float *max);                 // 0x00105da0
    float GetFrequency();                                        // 0x00105e00
    void ComputeRootQ(float t0, float t1, float *q);             // 0x00105e10
    void ComputeBeginRootQ(float *q);                            // 0x00105ef0
    void ComputeEndRootQ(float *q);                              // 0x00105f10
    void AlignCycleBeginEnd(int cIdx);                           // 0x00105f50
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x00106050
    bool EvalVel2D(float time, float *vel);                      // 0x001061f0
};
static_assert(sizeof(FnRunBlender) == 0x80, "FnRunBlender is 0x80 bytes (the factory's block, the delete size)");

struct FnTurnBlender : FnAnim {      // 0x5c, type 9: blends neighbouring run blenders by a turn weight
    FnRunBlender **anims;            // +0x0c [numAnims], from the pool
    FnRunBlender *fnAnims[2];        // +0x10 anims[idx], anims[idx + 1]
    float weight;                    // +0x18
    int32_t numAnims;                // +0x1c
    int32_t idx;                     // +0x20 -100 until SetWeight
    Skeleton *skeleton;              // +0x24
    float cycles[2];                 // +0x28 1 / each blender's frequency
    float offsets[2];                // +0x30 each blender's offset
    float freq;                      // +0x38
    float prevTime;                  // +0x3c
    float offset;                    // +0x40
    int32_t cycleIdx;                // +0x44
    float alignQ[4];                 // +0x48
    uint8_t init;                    // +0x58

    FnTurnBlender* Construct();                                  // 0x001042d0
    void Destruct();                                             // 0x00104310
    FnTurnBlender* ScalarDelete(unsigned flags);                 // 0x00104fa0
    void Eval(float previous, float time, float *pose);          // 0x00104390
    void SetWeight(float w);                                     // 0x001043b0
    void SetAnims(Skeleton *s, int count, FnRunBlender **blenders);  // 0x00104500
    bool EvalPhase(float time, float *phase);                    // 0x00104570
    bool BlendVel(float t0, float t1, float *vel);               // 0x00104580
    float GetFrequency();                                        // 0x001046a0
    int ComputeCycleIdx(float t, float startTime, float endTime);   // 0x001046b0
    void ComputeAlignQ(const float *v1, const float *v2, float *q);  // 0x00104720
    void AlignRootQ(float *sqt);                                 // 0x001047e0
    void AlignVel(float *vel);                                   // 0x00104820
    bool BlendBeginFacing(float *facing);                        // 0x00104870
    double CycleTime(float t, float startTime, float endTime);   // 0x00104920 (answers in ST0)
    void SetSpeed(float s);                                      // 0x001049c0
    bool BlendEndFacing(float *facing);                          // 0x00104a40
    void AlignCycleBeginEnd(int cIdx);                           // 0x00104af0
    bool EvalSQT(float time, float *sqt, void *mask);            // 0x00104bf0
    bool EvalVel2D(float time, float *vel);                      // 0x00104d60
};
static_assert(sizeof(FnTurnBlender) == 0x5c, "FnTurnBlender is 0x5c bytes (the factory's block, the delete size)");

// out = v rotated by the quaternion q (out[3] = 1); invented name, Ghidra's FUN_00104eb0.
void AnimQuatRotateVector(const float *q, const float *v, float *out);   // 0x00104eb0
int AnimTruncateLoco(float value);                               // 0x00104fd0 (invented name)
double AnimLength2D(const float *v);                             // 0x00104fe0 (invented name, answers in ST0)

#endif // DRIVING_EAGL_ANIM_ANIMLOCOBLEND_H_
