#ifndef SHOOTER_H_
#define SHOOTER_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// The placement's keys (level_tag)
typedef struct ShooterPlacement {
    PlacementHeader header;     // 0x00
    uint channel;               // 0x2c
    uint weapon;                // 0x30
} ShooterPlacement;

static_assert(offsetof(ShooterPlacement, weapon) == 0x30, "Bad offset of ShooterPlacement.weapon");

#pragma pack(pop)

obj_tag * Shooter_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Shooter_Update(obj_tag *obj);

#endif // SHOOTER_H_
