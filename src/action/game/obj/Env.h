#ifndef ENV_H_
#define ENV_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A cel's wind (cel_tag::env), 0x44 bytes (Env_Create). Its strength swells to windMax and dies back to a quarter
// of it, and its gust runs from -1 to 1 over and over (Env_Update).
typedef struct ENV_tag {
    _VECTOR windDir;            // 0x00
    _VECTOR windSide;           // 0x0c - windDir crossed with the placement's direction
    _VECTOR pos;                // 0x18 - the placement's
    float windStrength;         // 0x24
    float windMax;              // 0x28 - no wind for 0
    float windGust;             // 0x2c - its size counts
    float unknown30;            // 0x30
    float unknown34;            // 0x34
    float windSpread;           // 0x38 - how much the wind grows along its way from the cel's centre
    uchar unknown3c[3];         // 0x3c
    uchar dropType;             // 0x3f - InitDrops' kind for the level (GlobalEnv's)
    uchar unknown40;            // 0x40 - with no rain only
    uchar windRising;           // 0x41 - windStrength is swelling
    uchar unknown42;            // 0x42
    uchar pad43;
} ENV_tag;

static_assert(sizeof(ENV_tag) == 0x44, "ENV_tag is 0x44 bytes");
static_assert(offsetof(ENV_tag, windSpread) == 0x38, "Bad offset of ENV_tag.windSpread");
static_assert(offsetof(ENV_tag, windRising) == 0x41, "Bad offset of ENV_tag.windRising");

// The last ENV_tag Env_Create made, whose dropType is the level's rain
extern ENV_tag *GlobalEnv;

// A wind's placement keys (level_tag)
typedef struct EnvPlacement {
    char _unknown00[0x2c];
    uint dropType;              // 0x2c - ENV_tag's
    uint unknown30;             // 0x30
    uint unknown34;             // 0x34
    uint wind;                  // 0x38 - tenths
    uint unknown3c;             // 0x3c
    uint unknown40;             // 0x40
    uint unknown44;             // 0x44
    uint unknown48;             // 0x48
    uint unknown4c;             // 0x4c
    uint sun;                   // 0x50 - the sun is placed
    uint sunYaw;                // 0x54 - degrees
    uint sunPitch;              // 0x58 - degrees, upwards
} EnvPlacement;

static_assert(offsetof(EnvPlacement, sunPitch) == 0x58, "Bad offset of EnvPlacement.sunPitch");

// An animated tree's placement keys (level_tag)
typedef struct TreePlacement {
    char _unknown00[0x2c];
    HASHCODE skin;              // 0x2c
    HASHCODE script;            // 0x30
} TreePlacement;

static_assert(offsetof(TreePlacement, script) == 0x30, "Bad offset of TreePlacement.script");

#pragma pack(pop)

void Env_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);
obj_tag* Env_Tree_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Env_GetWindVel(cel_tag *cel, _VECTOR *pos, _VECTOR *velOut);
void Env_WaterRipple(HITDATA_tag *hit);
void Env_Breath(_VECTOR *mouth, _VECTOR *dir, cel_tag *cel, int created);
void Env_Tree_Update(obj_tag *obj);
void Env_Update(void);
void Env_Reset(void);


#endif // ENV_H_
