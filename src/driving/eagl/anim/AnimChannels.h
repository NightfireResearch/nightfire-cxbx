#ifndef DRIVING_EAGL_ANIM_ANIMCHANNELS_H_
#define DRIVING_EAGL_ANIM_ANIMCHANNELS_H_

// The channel types the disc's anims are made of (with DeltaF1/F3): raw event, DeltaQuat, KeyQuat. See
// AnimChannels.cpp.

#include "FnAnim.h"

// Raw event data: u16 type, u16 checksum, u32 count, then 16-byte events {u32 id, float time, ...}.
struct RawEventData {
    void Eval(float previous, float time, int32_t *index, float *lastTime, void **handlers, void *data);  // 0x000fb170
};

int AnimTruncate(float value);                                   // 0x000fb350
void EAGL_VU0_fastqslerp(float t, const float *q0, const float *q1, float *out);   // 0x000fbd10

#endif // DRIVING_EAGL_ANIM_ANIMCHANNELS_H_
