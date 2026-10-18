#ifndef LIGHTNING_H_
#define LIGHTNING_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A lightning point's extra data. Of two with the same pair, the one placed second makes the bolt: eight beam
// segments from itself to the other, the first itself and the last the other
typedef struct LIGHTNING_tag {
    obj_tag *segments[8];       // 0x00 - NULL for the point that does not make the bolt
    _VECTOR start;              // 0x20
    _VECTOR end;                // 0x2c
    uint key0;                  // 0x38 - the placement's first key, not read
    short pair;                 // 0x3c
    short key2;                 // 0x3e - not read
} LIGHTNING_tag;

static_assert(sizeof(LIGHTNING_tag) == 0x40, "LIGHTNING_tag is 0x40 bytes");

// The placement's keys (level_tag)
typedef struct LightningPlacement {
    char _unknown00[0x2c];
    uint key0;                  // 0x2c
    uint pair;                  // 0x30
    uint key2;                  // 0x34
} LightningPlacement;

static_assert(offsetof(LightningPlacement, key2) == 0x34, "Bad offset of LightningPlacement.key2");

#pragma pack(pop)

obj_tag * Lightning_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Lightning_Update(obj_tag *obj);
void FractalThing(obj_tag **segments, short first, short last, _VECTOR *start, _VECTOR *end, char byRoot);

#endif // LIGHTNING_H_
