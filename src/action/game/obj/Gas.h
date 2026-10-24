#ifndef GAS_H_
#define GAS_H_

#include "../../actionhelpers.h"

// The kinds of smoke gases_smoke knows
enum GasKind {
    GAS_SLOWING = 0,            // rises, slowing down, to the cel's ceiling
    GAS_GROWING = 1,            // only grows
    GAS_STILL = 2,
    GAS_RISING = 3,             // rises faster, to the ceiling
    GAS_FALLING = 4,            // falls, slowing sideways
    GAS_GROWING5 = 5,
    GAS_RISING6 = 6,
    GAS_WIND = 7,               // blown by the cel's wind
    GAS_SINKING = 8,
    GAS_SOURCE = 9,             // hidden; gives off GAS_SINKING smoke every fourth frame once 20 frames old
    GAS_WIND_SOURCE = 10,       // hidden; gives off GAS_WIND smoke, rising
    GAS_FADING = 11,            // rises, fading out over its last 30 frames
};

#pragma pack(push, 1)

// A puff of smoke's extra data (OBJECTTYPE_GAS, 0x3c bytes; 0x54 for kinds 9 and 10, which give off smoke of their
// own): over its life it grows from scale0 to scale1 and changes from colour0 to colour1 (r, g, b, a from the top
// byte), turning slowly. gases_smoke moves it by its kind.
typedef struct GAS_tag {
    _VECTOR vel;                // 0x00 - a frame; for kinds 9 and 10, where they give off their smoke
    float colourStep[4];        // 0x0c - a frame
    float colour[4];            // 0x1c
    float spin;                 // 0x2c - about z, a frame
    float scaleStep;            // 0x30 - a frame
    float life;                 // 0x34 - frames
    uchar kind;                 // 0x38 - GasKind
    uchar _pad39[3];
    // Kinds 9 and 10 only: the smoke they give off
    uint colour0;               // 0x3c
    uint colour1;               // 0x40
    HASHCODE entity;            // 0x44
    float scale0;               // 0x48
    float scale1;               // 0x4c
    float life0;                // 0x50
} GAS_tag;

static_assert(sizeof(GAS_tag) == 0x54, "GAS_tag is 0x54 bytes");
static_assert(offsetof(GAS_tag, kind) == 0x38, "Bad offset of GAS_tag.kind");

#pragma pack(pop)

struct HEINFO_tag;

obj_tag* Gas_Init(cel_tag *cel, _VECTOR *pos, _VECTOR *rot, _VECTOR *vel, float scale0, float scale1, HASHCODE entity,
                  uint kind, uint colour0, uint colour1, ushort life);
void gases_smoke(obj_tag *obj);
void Debris_Create(HITDATA_tag *hit, struct HEINFO_tag *info, cel_tag *cel, ushort count);
void Debris_CreateEx(_VECTOR *pos, _VECTOR *dir, _VECTOR *side, float speed, uint life, uint count, cel_tag *cel,
                     HASHCODE *entities, uint numEntities);
void Debris_Update(obj_tag *obj);

#endif // GAS_H_
