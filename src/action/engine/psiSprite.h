#ifndef PSISPRITE_H_
#define PSISPRITE_H_

#include "../actionhelpers.h"

// One queued screen-space sprite. Only the fields named so far: __Font_DrawText (0x00069c50) fills the first
// two from a texture record, and psiDrawSprites (0x000de3d0) binds the texture through textureIndex.
typedef struct SPRITE_DRAW {
    float invWidth;            // 0x00
    float invHeight;           // 0x04
    char unknown08[4];         // 0x08
    uint32_t textureIndex;     // 0x0c: into the game's texture records (TextureRecords in view.cpp)
    char unknown10[0x20];      // 0x10
} SPRITE_DRAW;

void psiDrawSprites(SPRITE_DRAW* spriteList, int numItems);

#endif // PSISPRITE_H_