#ifndef ENV_H_
#define ENV_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A cel's wind (cel_tag::env). Only what Env_GetWindVel reads is named.
typedef struct ENV_tag {
    _VECTOR windDir;            // 0x00
    char _unknown0c[0x24 - 0x0c];
    float windStrength;         // 0x24
    float windOn;               // 0x28 - no wind for 0
    float windGust;             // 0x2c - its size counts
    char _unknown30[0x38 - 0x30];
    float windSpread;           // 0x38 - how much the wind grows along its way from the cel's centre
} ENV_tag;

static_assert(offsetof(ENV_tag, windSpread) == 0x38, "Bad offset of ENV_tag.windSpread");

#pragma pack(pop)

void Env_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);
obj_tag* Env_Tree_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Env_GetWindVel(cel_tag *cel, _VECTOR *pos, _VECTOR *velOut);
void Env_WaterRipple(HITDATA_tag *hit);


#endif // ENV_H_