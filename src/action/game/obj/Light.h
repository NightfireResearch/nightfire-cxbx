#ifndef LIGHT_H_
#define LIGHT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// light_tag structure - 61 bytes of actual data, padded to 64 for 4-byte alignment
struct light_tag {
    LLNODE_tag node;                       // 0x00-0x07 - linked list node (prev/next)
    _VECTOR position;                      // 0x08-0x13 - light position
    short lifetimeFrames;                  // 0x14 - frame countdown timer (0 = infinite)
    ushort switchChannel;                  // 0x16 - switch channel index
    ushort soundState;                     // 0x18 - sound state/counter
    ushort soundTimingBase;                // 0x1a - sound timing randomization base
    ushort soundTimingCounter;             // 0x1c - sound timing counter
    ushort flickerMask;                   // 0x1e
    short soundID;                         // 0x20 - sound ID (0xffff = no sound)
    uchar padding22[2];                    // 0x22-0x23
    obj_tag* maybeParentObject;            // 0x24
    undefined4 psiLight;                   // 0x28
    uchar padding2c[4];                    // 0x2c-0x2f
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

static_assert(sizeof(light_tag) == 64, "light_tag must be 64 bytes");

void Light_Init(void);
void Light_AllocateEntries(void);
light_tag * Light_Create(_VECTOR *pos,undefined1 clr_r,undefined1 clr_g,undefined1 clr_b,float brightness,ushort switchChannel,short lifetimeFrames,undefined1 lightType,float param_9,ushort soundState,ushort soundTimingBase,undefined2 param_12,int soundID);
bool Light_Delete(light_tag* light);
void Light_Update(void);

#endif // LIGHT_H_