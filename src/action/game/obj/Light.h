#ifndef LIGHT_H_
#define LIGHT_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

// light_tag::lightType: which objects it lights (Lights_CalcClosestLights)
enum LightType {
    LIGHT_PLACED,       // the map's: for objects with EFFECT_PLACED_LIGHTS
    LIGHT_TRANSIENT,    // muzzle flashes, explosions, night vision: but objects with EFFECT_NO_TRANSIENT_LIGHTS
    LIGHT_CARRIED,      // carried by its parentObject (the searchlight): objects with EFFECT_PLACED_LIGHTS within 4
                        // across the floor
};

#pragma pack(push, 1)
// light_tag structure - 61 bytes of actual data, padded to 64 for 4-byte alignment
struct light_tag {
    LLNODE_tag node;                       // 0x00-0x07 - linked list node (prev/next)
    _VECTOR position;                      // 0x08-0x13 - light position
    short lifetimeFrames;                  // 0x14 - frame countdown timer (0 = infinite)
    ushort switchChannel;                  // 0x16 - switch channel index
    ushort flickerMax;                     // 0x18 - non-zero: it flickers; the limit of flickerCounter & 0xfff
    ushort flickerBase;                    // 0x1a - flickerCounter restarts at it plus a random share of it
    ushort flickerCounter;                 // 0x1c - counts down lit, then (0xf000 set) up flickering
    ushort flickerMask;                   // 0x1e
    ushort soundID;                        // 0x20 - Action_SFX, SFX_NONE for none
    uchar padding22[2];                    // 0x22-0x23
    obj_tag* parentObject;                 // 0x24
    undefined4 psiLight;                   // 0x28
    float distanceShare;                   // 0x2c - its distance from the sphere over half its reach (Lights_CalcClosestLights)
    float brightness;                      // 0x30
    float intensity;                       // 0x34 - psiLight_SetLights' scale for its colour; NaN when off
    uchar clr_r;                           // 0x38
    uchar clr_g;                           // 0x39
    uchar clr_b;                           // 0x3a
    uchar lightType;                       // 0x3b - LightType
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
    float invRadiusSq;          // 0x10 - 1 / radiusSq in every map; Light_SetAmbientRadiators does not read it
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
light_tag * Light_Create(_VECTOR *pos,undefined1 clr_r,undefined1 clr_g,undefined1 clr_b,float brightness,
                         ushort switchChannel,short lifetimeFrames,undefined1 lightType,
                         float intensity,ushort flickerMax,ushort flickerBase,undefined2 flickerMask,int soundID);
bool Light_Delete(light_tag* light);
light_tag *Light_FindMapLight(_VECTOR *pos, float radius);
void Light_Update(void);
double ClrStep(float from, float to, float share);
void Lights_CalcAmbientLight(obj_tag *obj);
light_tag** Lights_CalcClosestLights(_VECTOR *sphere, uint mask, ushort *count);
void Light_SetAmbientRadiators(AMBIENT_RADIATORS *radiators);

#endif // LIGHT_H_