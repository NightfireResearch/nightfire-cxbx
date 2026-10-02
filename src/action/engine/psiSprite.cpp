#include "psiSprite.h"
#include "psiGraphics.h"         // Tex
#include "Direct3D/d3dSeam.h"
#include "../game.h"             // GameState

#include <string.h>

// The frame of an animated texture showing now
static int CurrentFrameSlot(const TextureInfo *info) {
    uint32_t frame = ((uint32_t)GameState.NumFramesUnpaused / (uint32_t)info->animSpeed) % (uint32_t)info->numFrames;
    return (&info->baseIdx)[frame];
}

// Draws a list of screen-space sprites through the immediate-mode buffer, changing texture and blend only when
// they change from one sprite to the next. The count is a short (0x000de3d0).
// AUTOINJECT
void psiDrawSprites(SPRITE_DRAW* spriteList, int numItems) {
    short count = (short)numItems;
    maybeResetRenderState(1);
    int blend = 0;
    for (int i = 0; i < count; i++) {
        SPRITE_DRAW *sprite = &spriteList[i];
        int spriteBlend = (sprite->flags & SPRITE_BLEND_1) ? 1 : (sprite->flags & SPRITE_BLEND_2) ? 2 : 0;
        int texture = (int)sprite->textureIndex;
        if (i == 0 || spriteBlend != blend || texture != (int)spriteList[i - 1].textureIndex) {
            if (i > 0)
                maybeImmediateModeFlush();
            TextureInfo *info = Tex[texture];
            if (info->registerParam6 != 0)
                d3dSetDeferredTextureState(0, 0);
            d3dSetTextureStage0(CurrentFrameSlot(info));
            if (info->registerParam6 == 0) {
                const int *wrap = (const int *)((const char *)info + 0x2c);   // the texture's two address modes
                d3dSetDeferredTextureState(wrap[0] == 0, wrap[1] == 0);
            }
            d3dSetupRenderStatesAndFog(spriteBlend);
            blend = spriteBlend;
        }
        uint32_t colour = (uint32_t)sprite->clrA << 24 | (uint32_t)sprite->clrR << 16 | (uint32_t)sprite->clrG << 8 |
                          sprite->clrB;
        float colourBits;
        memcpy(&colourBits, &colour, sizeof(colourBits));
        maybeImmediateModePushItem(sprite->x, sprite->y, sprite->width, sprite->height, (float)sprite->u0,
                                   (float)sprite->v0, (float)sprite->u1, (float)sprite->v1, colourBits);
    }
    maybeImmediateModeFlush();
    d3dSetDeferredTextureState(1, 1);
}
