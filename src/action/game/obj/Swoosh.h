#ifndef SWOOSH_H_
#define SWOOSH_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A trail's extra data (OBJECTTYPE_SWOOSH, 0x20 bytes): a model stretched from one point to the other, turned to the
// camera, that narrows and fades out
typedef struct SWOOSH_tag {
    _VECTOR from;               // 0x00
    _VECTOR to;                 // 0x0c
    short timer;                // 0x18 - frames left
    short total;                // 0x1a
    uchar drift;                // 0x1c - rises and drifts a millimetre a frame
    uchar _pad1d[3];
} SWOOSH_tag;

static_assert(sizeof(SWOOSH_tag) == 0x20, "SWOOSH_tag is 0x20 bytes");

#pragma pack(pop)

obj_tag* Swoosh_Create(_VECTOR *from, _VECTOR *to, short frames, HASHCODE entity, uchar drift);
void Swoosh_Update(obj_tag *obj);

#endif // SWOOSH_H_
