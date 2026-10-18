#ifndef APOCALYPSE_H_
#define APOCALYPSE_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// An earthquake's extra data: once its channel is on it runs its length, the rumble, the view's shaking and the
// flash fading with the time left
typedef struct APOCALYPSE_tag {
    float timeLeft;             // 0x00 - frames at 60 Hz
    float length;               // 0x04
    short channel;              // 0x08
    short rumble;               // 0x0a - at the start; 0 for none
    uchar shake;                // 0x0c - viewer 0's apocalypseEffect
    uchar flash;                // 0x0d - player 0's flash bang
    uchar running;              // 0x0e
    uchar _pad0f;
} APOCALYPSE_tag;

static_assert(sizeof(APOCALYPSE_tag) == 0x10, "APOCALYPSE_tag is 0x10 bytes");

// The placement's keys (level_tag)
typedef struct ApocalypsePlacement {
    char _unknown00[0x2c];
    uint channel;               // 0x2c
    uint seconds;               // 0x30
    uint rumble;                // 0x34
    uint shake;                 // 0x38
    uint flash;                 // 0x3c
} ApocalypsePlacement;

static_assert(offsetof(ApocalypsePlacement, flash) == 0x3c, "Bad offset of ApocalypsePlacement.flash");

#pragma pack(pop)

obj_tag * Apocalypse_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Apocalypse_Update(obj_tag *obj);

#endif // APOCALYPSE_H_
