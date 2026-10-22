#ifndef LIGHT_H_
#define LIGHT_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)
// light_tag structure - 61 bytes of actual data, padded to 64 for 4-byte alignment
struct light_tag {
    LLNODE_tag node;                       // 0x00-0x07 - linked list node (prev/next)
    _VECTOR position;                      // 0x08-0x13 - light position
    short lifetimeFrames;                  // 0x14 - frame countdown timer (0 = infinite)
    ushort switchChannel;                  // 0x16 - switch channel index
    ushort soundState;                     // 0x18 - non-zero: flicker (and its sound); upper bound of soundTimingCounter & 0xfff
    ushort soundTimingBase;                // 0x1a - sound timing randomization base
    ushort soundTimingCounter;             // 0x1c - sound timing counter
    ushort flickerMask;                   // 0x1e
    ushort soundID;                        // 0x20 - sound ID (0xffff = no sound)
    uchar padding22[2];                    // 0x22-0x23
    obj_tag* maybeParentObject;            // 0x24
    undefined4 psiLight;                   // 0x28
    float distanceShare;                   // 0x2c - its distance from the sphere over half its reach (Lights_CalcClosestLights)
    float brightness;                      // 0x30
    float field33_0x34;                    // 0x34
    uchar clr_r;                           // 0x38
    uchar clr_g;                           // 0x39
    uchar clr_b;                           // 0x3a
    uchar lightType;                       // 0x3b - light type/mode
    uchar enabled;                         // 0x3c - controlled by switch channel
    uchar padding3d[3];                    // 0x3d-0x3f - pad to 4-byte alignment (64 bytes total)
};
#pragma pack(pop)

LAYOUT_32(sizeof(light_tag) == 64, "light_tag must be 64 bytes");

#pragma pack(push, 1)
// One of the level's ambient lights in its map
typedef struct AMBIENT_RADIATOR {
    _VECTOR pos;
    float radiusSq;             // 0x0c
    float unknown10;            // 0x10
    float r;                    // 0x14 - 0 to 1
    float g;                    // 0x18
    float b;                    // 0x1c
} AMBIENT_RADIATOR;

typedef struct AMBIENT_RADIATORS {
    uint count;
    AMBIENT_RADIATOR lights[1]; // count of them
} AMBIENT_RADIATORS;
#pragma pack(pop)

static_assert(sizeof(AMBIENT_RADIATOR) == 0x20, "AMBIENT_RADIATOR is 0x20 bytes");

void Light_Init(void);
void Light_AllocateEntries(void);
light_tag * Light_Create(_VECTOR *pos,undefined1 clr_r,undefined1 clr_g,undefined1 clr_b,float brightness,ushort switchChannel,short lifetimeFrames,undefined1 lightType,float param_9,ushort soundState,ushort soundTimingBase,undefined2 param_12,int soundID);
bool Light_Delete(light_tag* light);
light_tag *Light_FindMapLight(_VECTOR *pos, float radius);
void Light_Update(void);
double ClrStep(float from, float to, float share);
void Lights_CalcAmbientLight(obj_tag *obj);
light_tag** Lights_CalcClosestLights(_VECTOR *sphere, uint mask, ushort *count);
void Light_SetAmbientRadiators(AMBIENT_RADIATORS *radiators);

#endif // LIGHT_H_