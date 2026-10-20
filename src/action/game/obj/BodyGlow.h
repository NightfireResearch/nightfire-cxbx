#ifndef BODYGLOW_H_
#define BODYGLOW_H_

#include "../../actionhelpers.h"

#define BODYGLOW_BEAMS 15

#pragma pack(push, 1)
// A body glow's extra data: who it follows, a beam along each of 15 of their bones, and how long it lasts once
// they are dead
typedef struct BODYGLOW_tag {
    obj_tag *owner;                     // 0x00 - a drone or a player
    obj_tag *beams[BODYGLOW_BEAMS];     // 0x04
    short life;                         // 0x40 - 0x100 to start; a frame less each frame the owner is dead
    short pad42;
} BODYGLOW_tag;
#pragma pack(pop)

static_assert(sizeof(BODYGLOW_tag) == 0x44, "BODYGLOW_tag is 0x44 bytes");

obj_tag* BodyGlow_Create(obj_tag *owner);
void BodyGlow_Delete(obj_tag *glow);
void BodyGlow_Update(obj_tag *glow);

#endif // BODYGLOW_H_
