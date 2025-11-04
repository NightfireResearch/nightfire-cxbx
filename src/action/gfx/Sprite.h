#ifndef SPRITE_H
#define SPRITE_H

#include "../actionhelpers.h"


#pragma pack(push, 1)

typedef struct {
    float x;
    float y;
} GPOINT;

typedef struct sprite {
    LLNODE_tag node;
    unsigned int createdOnFrame;
    unsigned int unknown2;
    unsigned int colourTint;
    unsigned int unknown3;
    char *text;
    char *maybeClippedString;
    short length; // at 0x20
    short unknown4;
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
    uchar maybeEnabled; // 0x27 or 0x31 results in it being visible, 0xff results in it being invisible? Unclear.
    char linkedViewer;
    char unknown7[2];
    GPOINT scale;
} sprite;

static_assert(sizeof(sprite) == 0x44, "Wrong size for sprite");

typedef struct SpriteInfo {
    char pad[0x8];
    char maybeEnabled;
    char pad2[0x2c - 0x8 - 1];
} SpriteInfo;

static_assert(sizeof(SpriteInfo) == 0x2c, "Size of SpriteInfo is incorrect");
static_assert(offsetof(SpriteInfo, maybeEnabled) == 0x8, "Offset of maybeEnabled is incorrect");

#pragma pack(pop)


void Sprite_SetText(sprite *param_1,char *param_2);
sprite* Sprite_Create(void);
sprite* Sprite_Create2(SpriteInfo *param_1);
void Sprite_Link2Viewer(sprite *spr,ushort playerNum);
sprite* Sprite_CreateAndLink2Viewer(char viewer);
void Sprite_Delete(sprite* sprite);

#endif // SPRITE_H