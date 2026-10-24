#ifndef SPRITE_H
#define SPRITE_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

// sprite::flags and SpriteInfo::flags (Sprite_Create2 copies them). View_DrawSprites reads them, the blends go on
// to psiDrawSprites.
enum SpriteFlags {
    SPRITE_SHADOW = 0x20,           // a black copy where it was, the sprite a 64th of its size (1 to 5 pixels) on
    SPRITE_FLIP_X = 0x40,
    SPRITE_FLIP_Y = 0x80,
    SPRITE_BLEND_1 = 0x100,         // d3dSetupRenderStatesAndFog(1)
    SPRITE_BLEND_2 = 0x200,         // d3dSetupRenderStatesAndFog(2), when SPRITE_BLEND_1 is clear
    SPRITE_TILED = 0x400,           // mirrored tiles, as many as the texture goes into the texels used
    SPRITE_CENTRED = 0x800,
    SPRITE_CLIP_TO_VIEW = 0x1000,   // HUD_CreateDefault cuts the sprite to the view, HUD_CalcWidthHeight leaves it out
    SPRITE_TEXT_RING = 0x2000,      // a text's four-copy shadow (Font_DrawText's FONT_SHADOW_RING)
    SPRITE_TEXT_OUTLINE = 0x4000,
};

// Values of sprite::depth with special meaning to the list builder
#define SPRITE_HIDDEN 0xff          // never drawn
#define SPRITE_FOREGROUND_MIN 50    // sorted to the front; see Sprite_BuildList

#pragma pack(push, 1)

typedef struct {
    float x;
    float y;
} GPOINT;

typedef struct sprite {
    LLNODE_tag node;
    unsigned int createdOnFrame;
    unsigned int textureIndex;
    unsigned int colourTint;
    unsigned int shadowColour;  // 0x14 - a text's shadow (Font_DrawText's)
    char *text;
    char *format;
    short length; // at 0x20
    short flags;                // 0x22 - SpriteFlags
    short positionX;
    short positionY;
    short backupOnscreenWidth;
    short backupOnscreenHeight;
    short onscreenWidth;
    short onscreenHeight;
    short spritesheetX;
    short spritesheetY;
    short spritesheetWidth;
    short spritesheetHeight;
    uchar depth; // the draw order, highest first: SPRITE_FOREGROUND_MIN and up in front, SPRITE_HIDDEN not drawn
    char linkedViewer;
    char unknown7[2];
    GPOINT scale;
} sprite;

LAYOUT_32(sizeof(sprite) == 0x44, "Wrong size for sprite");
LAYOUT_32(offsetof(sprite, shadowColour) == 0x14, "Wrong offset for shadowColour");
LAYOUT_32(offsetof(sprite, linkedViewer) == 0x39, "Wrong offset for linkedViewer");

typedef struct SpriteInfo {
    uint colour;
    uint shadowColour;
    ushort depth;
    short flags;        // SpriteFlags
    short posX;
    short posY;
    short onscreenWidth;
    short onscreenHeight;
    ushort spritesheetX;
    ushort spritesheetY;
    short spritesheetWidth;
    short spritesheetHeight;
    Action_TranslatedText textRef;
    char* format;
    HASHCODE textureHashcode;
    char linkedViewer;
    char pad[3]; // Unclear if this is a single u32, padding, or not needed?
} SpriteInfo;

LAYOUT_32(sizeof(SpriteInfo) == 0x2c, "Size of SpriteInfo is incorrect");
static_assert(offsetof(SpriteInfo, depth) == 0x8, "Offset of depth is incorrect");
LAYOUT_32(offsetof(SpriteInfo, textureHashcode) == 0x24, "Offset of textureHashcode is incorrect");

#pragma pack(pop)

void Sprite_SetText(sprite *spr,char *text);
void Sprite_SetFmt(sprite *spr, char *fmt);
sprite* Sprite_Create(void);
sprite* Sprite_Create2(SpriteInfo *sprInfo);
void Sprite_Link2Viewer(sprite *spr,ushort playerNum);
sprite* Sprite_CreateAndLink2Viewer(char viewer);
void Sprite_Delete(sprite* sprite);
void Sprite_SetTextNFmt(sprite *spr, char *text, char *fmt);
void Sprite_SetParams(sprite *spr, uchar enabled, short x, short y, HASHCODE texture, uint colour);
void Sprite_InitLists(void);
sprite** Sprite_BuildList(ushort viewer, undefined4 *startOut, uint *lastForegroundOut, uint *countOut);

#endif // SPRITE_H