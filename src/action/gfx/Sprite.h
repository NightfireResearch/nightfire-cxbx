#ifndef SPRITE_H
#define SPRITE_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"


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
    unsigned int unknown1;
    char *text;
    char *format;
    short length; // at 0x20
    short flags;
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
LAYOUT_32(offsetof(sprite, unknown1) == 0x14, "Wrong offset for unknown1");
LAYOUT_32(offsetof(sprite, linkedViewer) == 0x39, "Wrong offset for linkedViewer");

typedef struct SpriteInfo {
    uint colour;
    uint unknown1;
    ushort depth;
    short flags;
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


void Sprite_SetText(sprite *param_1,char *param_2);
void Sprite_SetFmt(sprite *spr, char *fmt);
sprite* Sprite_Create(void);
sprite* Sprite_Create2(SpriteInfo *param_1);
void Sprite_Link2Viewer(sprite *spr,ushort playerNum);
sprite* Sprite_CreateAndLink2Viewer(char viewer);
void Sprite_Delete(sprite* sprite);
void Sprite_SetTextNFmt(sprite *spr, char *text, char *fmt);
void Sprite_SetParams(sprite *spr, uchar enabled, short x, short y, HASHCODE texture, uint colour);
void Sprite_InitLists(void);
sprite** Sprite_BuildList(ushort viewer, undefined4 *startOut, uint *lastForegroundOut, uint *countOut);

#endif // SPRITE_H