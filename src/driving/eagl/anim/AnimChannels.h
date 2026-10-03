#ifndef DRIVING_EAGL_ANIM_ANIMCHANNELS_H_
#define DRIVING_EAGL_ANIM_ANIMCHANNELS_H_

// The channel types the disc's anims are made of (with DeltaF1/F3): raw event, DeltaQuat, KeyQuat. See
// AnimChannels.cpp.

#include "FnAnim.h"

struct DeltaCompressedData;

struct RawEvent {                    // 0x10, handed to the handler as it is
    uint32_t id;                     // +0x00 the handler table's index
    float time;                      // +0x04
    uint32_t data[2];                // +0x08
};
static_assert(sizeof(RawEvent) == 0x10, "a raw event is 0x10 bytes");

// Raw event data (thiscall on the data), the events sorted by time.
struct RawEventData : AnimData {
    int32_t count;                   // +0x04
    RawEvent events[1];              // +0x08 [count]

    void Eval(float previous, float time, int32_t *index, float *lastTime, void **handlers, void *data);  // 0x000fb170
};
static_assert(offsetof(RawEventData, events) == 8, "the raw events are at +8");

// DeltaLerp and DeltaQuat data: a value set per frame, delta compressed.
struct DeltaChanData : AnimData {
    DeltaCompressedData *info;       // +0x04
    uint16_t frames;                 // +0x08
    uint16_t index[1];               // +0x0a the output index of each group of four values (a quaternion)
};
static_assert(offsetof(DeltaChanData, index) == 0xa, "the DeltaQuat indexes are at +0xa");

// KeyLerp and KeyQuat data: a value set per key, delta compressed, at the key times.
struct KeyChanData : AnimData {
    DeltaCompressedData *info;       // +0x04
    uint16_t *times;                 // +0x08 [keys - 1]: key k is reached at times[k - 1]
    uint16_t keys;                   // +0x0c
    uint16_t index[1];               // +0x0e the output index of each group of four values
};
static_assert(offsetof(KeyChanData, index) == 0xe, "the KeyQuat indexes are at +0xe");

int AnimTruncate(float value);                                   // 0x000fb350
void EAGL_VU0_fastqslerp(float t, const float *q0, const float *q1, float *out);   // 0x000fbd10

#endif // DRIVING_EAGL_ANIM_ANIMCHANNELS_H_
