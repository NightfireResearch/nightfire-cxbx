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

extern uint8_t DropType;
extern _VECTOR WindVector;
extern _VECTOR PrevPos;
// WindVector and the steps, this frame's (UpdateDrops)
extern _VECTOR WindVector2;
extern _VECTOR CubeCentre;
extern _VECTOR RandTable2[32];
extern uchar DropState[MAX_DROPS];
// 32 steps a frame at 60 Hz, a drop takes the one of its index
extern _VECTOR RandTable1[32];
// Every drop starts at once about the view on the next update
extern uint8_t InitBodge;
extern int16_t CurrentMax;
extern obj_tag *RainBoxRoot;
extern DROP_tag DropVector[MAX_DROPS];
// Drops AddDrop has tried this frame; 999 when nothing is to be drawn
extern int16_t Added;

obj_tag * RainBox_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void InitRainBox(void);
ushort RainBox_Clip(void);
void InitDrops(uchar type);
void AddDrop(short index, int first, uchar *state, DROP_tag *drops);
void UpdateDrops(void);
void DrawDrops(void);

#endif // RAINBOX_H_
