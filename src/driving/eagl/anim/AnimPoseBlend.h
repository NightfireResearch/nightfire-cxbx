#ifndef DRIVING_EAGL_ANIM_ANIMPOSEBLEND_H_
#define DRIVING_EAGL_ANIM_ANIMPOSEBLEND_H_

// FnPoseBlender and FnEventBlender's evaluation (the methods are declared in FnAnim.h), the pose blending helpers
// the pose, turn and run blenders share, and the phase data's match-time search. Never-built types: provisional.
// See AnimPoseBlend.cpp.

#include "FnAnim.h"

struct BoneMask;
class Transform;

// Pose blends (cdecl, invented names): 'count' bones of 12 floats (Skeleton.h), out = a..b at t. The quaternion is
// slerped (EAGL_VU0_fastqslerp), the translation and (SQT) the scale xyz lerped; the length scale and the 12th float
// are never written. A mask, where taken, skips the bones not in it (NULL: every bone).
void AnimBlendPoseQ(int count, float t, const float *a, const float *b, float *out,
                    const BoneMask *mask);                       // 0x000fbfd0
void AnimBlendPoseQT(int count, float t, const float *a, const float *b, float *out,
                     const BoneMask *mask);                      // 0x000fc080 (FnTurnBlender/FnRunBlender::EvalSQT)
void AnimBlendPoseSQT(int count, float t, const float *a, const float *b, float *out);   // 0x000fc1c0
// Two masks: a bone in both is blended, in one only is copied from that pose, in neither is left alone.
void AnimBlendPoseQMasks(int count, float t, const float *a, const BoneMask *maskA, const float *b,
                         const BoneMask *maskB, float *out);     // 0x000fc280
void AnimBlendPoseQTMasks(int count, float t, const float *a, const BoneMask *maskA, const float *b,
                          const BoneMask *maskB, float *out);    // 0x000fc360

// FnPoseBlender::XZProjectAlign's helpers (cdecl, invented names): of the matrix's rows 0..2, the one whose XZ
// projection is longest (its index to *axis), or the given one, as {x, 0, z, 1}.
void AnimXZProjectLongestAxis(const Transform *m, float *out, int *axis);   // 0x000fbe40
void AnimXZProjectAxis(const Transform *m, float *out, int axis);          // 0x000fbf60

static_assert(sizeof(FnPoseBlender) == 0x80, "FnPoseBlender is 0x80 bytes");
static_assert(sizeof(FnEventBlender) == 0x2c, "FnEventBlender is 0x2c bytes");

int AnimTruncateBlend(float value);                              // 0x000fd0b0 (invented name; CVTTSS2SI)

// FnPhaseChan's anim data (thiscall on the data; invented names): u16 type, u16 checksum, u16 frames, u16, u8 flags
// (0x08/0x10/0x20/0x40: a sample every 1/2/4/8 frames, else every frame), u8 first, then u8 phase samples (degrees as
// sample * 360/255 - 180) from index max(first, 2).
struct PhaseMatchData {
    uint16_t type;                   // +0x00
    uint16_t checksum;               // +0x02
    uint16_t frames;                 // +0x04
    uint16_t unknown6;               // +0x06
    uint8_t flags;                   // +0x08
    uint8_t first;                   // +0x09
    uint8_t samples[2];              // +0x0a

    // phase = {target degrees, direction, frame limit (<= 0 none)}: the frame where the phase reaches the target
    // moving in the direction's sense, else the sample nearest it.
    bool FindMatchTime(const float *phase, float *time);         // 0x000fd0c0
    int SampleCount();                                           // 0x000fd6d0
};

#endif // DRIVING_EAGL_ANIM_ANIMPOSEBLEND_H_
