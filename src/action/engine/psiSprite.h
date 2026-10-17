#ifndef PSISPRITE_H_
#define PSISPRITE_H_

#include "../actionhelpers.h"

// One queued screen-space sprite: a textured rectangle in screen pixels. __Font_DrawText (0x00069c50) fills the
// first two from a texture record; psiDrawSprites (0x000de3d0) draws a list of them.
#define SPRITE_BLEND_1 0x100       // flags: d3dSetupRenderStatesAndFog(1)
#define SPRITE_BLEND_2 0x200       // flags: d3dSetupRenderStatesAndFog(2), when SPRITE_BLEND_1 is clear
typedef struct SPRITE_DRAW {
    float invWidth;            // 0x00
    float invHeight;           // 0x04
    char unknown08[4];         // 0x08
    uint32_t textureIndex;     // 0x0c: into Tex[]
    float x;                   // 0x10
    float y;                   // 0x14
    float width;               // 0x18
    float height;              // 0x1c
    short u0;                  // 0x20 texels
    short v0;                  // 0x22
    short u1;                  // 0x24
    short v1;                  // 0x26
    uint8_t clrR;              // 0x28
    uint8_t clrG;              // 0x29
    uint8_t clrB;              // 0x2a
    uint8_t clrA;              // 0x2b
    uint16_t flags;            // 0x2c SPRITE_BLEND_*
    uint16_t _pad2e;
} SPRITE_DRAW;
static_assert(sizeof(SPRITE_DRAW) == 0x30, "SPRITE_DRAW is 0x30 bytes");

void psiDrawSprites(SPRITE_DRAW* spriteList, int numItems);

#endif // PSISPRITE_H_
