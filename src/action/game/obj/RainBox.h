#ifndef RAINBOX_H_
#define RAINBOX_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A rain box's extra data: its model's box where it was placed, cut to the 30-unit cube about CubeCentre each frame
// (RainBox_Clip). Drops start only inside an active box. The boxes are linked through obj_tag::maybeParent.
typedef struct RAINBOX_tag {
    float min[3];               // 0x00
    float max[3];               // 0x0c
    uchar active;               // 0x18 - something of it is left in x and z
    uchar _pad19[3];
} RAINBOX_tag;

static_assert(sizeof(RAINBOX_tag) == 0x1c, "RAINBOX_tag is 0x1c bytes");

// A drop: where it is, and the height it ends at
typedef struct DROP_tag {
    _VECTOR pos;
    float end;
} DROP_tag;

static_assert(sizeof(DROP_tag) == 0x10, "DROP_tag is 0x10 bytes");

#pragma pack(pop)

// DropType: what falls (ENV_tag::dropType)
enum {
    DROP_PARTICLES = 0,         // a particle list, texture 0x03000045
    DROP_MODELS = 1,            // a model at each drop, moved by the wind alone
    DROP_RISING = 2,            // a particle list, texture 0x03000087, going up
    DROP_NONE = 0xff,
};

#define MAX_DROPS 0x1000

#define DropType U8_AT(0x002637e0)
#define WindVector (*(_VECTOR *)0x002637e4)
#define PrevPos (*(_VECTOR *)0x002637f0)
// WindVector and the steps, this frame's (UpdateDrops)
#define WindVector2 (*(_VECTOR *)0x002637fc)
#define CubeCentre (*(_VECTOR *)0x00263808)
#define RandTable2 (*(_VECTOR (*)[32])0x00263818)
#define DropState (*(uchar (*)[MAX_DROPS])0x00263998)
// 32 steps a frame at 60 Hz, a drop takes the one of its index
#define RandTable1 (*(_VECTOR (*)[32])0x00264998)
// Every drop starts at once about the view on the next update
#define InitBodge U8_AT(0x00264b18)
#define CurrentMax I16_AT(0x00264b1c)
#define RainBoxRoot (*(obj_tag **)0x00264b20)
#define DropVector (*(DROP_tag (*)[MAX_DROPS])0x00264b28)
// Drops AddDrop has tried this frame; 999 when nothing is to be drawn
#define Added I16_AT(0x00274b28)

obj_tag * RainBox_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void InitRainBox(void);
ushort RainBox_Clip(void);
void InitDrops(uchar type);
void AddDrop(short index, int first, uchar *state, DROP_tag *drops);
void UpdateDrops(void);
void DrawDrops(void);

#endif // RAINBOX_H_
