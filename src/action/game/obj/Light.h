#ifndef LIGHT_H_
#define LIGHT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// light_tag structure - 61 bytes of actual data, padded to 64 for 4-byte alignment
struct light_tag {
    LLNODE_tag node;                       // 0x00-0x07 - linked list node (prev/next)
    undefined4 field8_0x8;                 // 0x08-0x0b
    uchar padding0c[8];                    // 0x0c-0x13
    short field17_0x14;                    // 0x14
    undefined2 field18_0x16;               // 0x16
    undefined2 field19_0x18;               // 0x18
    ushort field20_0x1a;                   // 0x1a
    ushort field21_0x1c;                   // 0x1c
    undefined2 field22_0x1e;               // 0x1e
    short field23_0x20;                    // 0x20
    uchar padding22[2];                    // 0x22-0x23
    obj_tag* maybeParentObject;            // 0x24
    undefined4 psiLight;                   // 0x28
    uchar padding2c[4];                    // 0x2c-0x2f
    float maybeBrightness;                 // 0x30
    float field33_0x34;                    // 0x34
    uchar clr_r;                           // 0x38
    uchar clr_g;                           // 0x39
    uchar clr_b;                           // 0x3a
    uchar field_0x3b;                      // 0x3b
    uchar field_0x3c;                      // 0x3c
    uchar padding3d[3];                    // 0x3d-0x3f - pad to 4-byte alignment (64 bytes total)
};
#pragma pack(pop)

static_assert(sizeof(light_tag) == 64, "light_tag must be 64 bytes");

void Light_Init(void);
void Light_AllocateEntries(void);
light_tag * Light_Create(_VECTOR *pos,undefined1 clr_r,undefined1 clr_g,undefined1 clr_b,float maybeBrightness,undefined2 param_6,short param_7,undefined1 param_8,float param_9,undefined2 param_10,ushort param_11,undefined2 param_12,int param_13);
bool Light_Delete(light_tag* light);

#endif // LIGHT_H_