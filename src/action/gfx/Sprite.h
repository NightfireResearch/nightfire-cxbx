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
    unsigned int unknown1;
    char *text;
    char *maybeClippedString;
    short length; // at 0x20
    short maybeFlags;
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
static_assert(offsetof(sprite, unknown1) == 0x14, "Wrong offset for unknown1");
static_assert(offsetof(sprite, linkedViewer) == 0x39, "Wrong offset for linkedViewer");

typedef struct SpriteInfo {
    uint colour;
    uint unknown1;
    uchar maybeEnabled;
    undefined1 unknown2;
    short maybeFlags;
    short posX;
    short posY;
    short onscreenWidth;
    short onscreenHeight;
    ushort spritesheetX;
    ushort spritesheetY;
    short maybeSpritesheetWidth;
    short maybeSpritesheetHeight;
    Action_TranslatedText textRef;
    char* maybeClippedText;
    HASHCODE textureHashcode;
    char linkedViewer;
    char pad[3];
} SpriteInfo;

static_assert(sizeof(SpriteInfo) == 0x2c, "Size of SpriteInfo is incorrect");
static_assert(offsetof(SpriteInfo, maybeEnabled) == 0x8, "Offset of maybeEnabled is incorrect");
static_assert(offsetof(SpriteInfo, textureHashcode) == 0x24, "Offset of textureHashcode is incorrect");

#pragma pack(pop)


void Sprite_SetText(sprite *param_1,char *param_2);
sprite* Sprite_Create(void);
sprite* Sprite_Create2(SpriteInfo *param_1);
void Sprite_Link2Viewer(sprite *spr,ushort playerNum);
sprite* Sprite_CreateAndLink2Viewer(char viewer);
void Sprite_Delete(sprite* sprite);
void Sprite_SetTextNFmt(sprite *param_1,char *param_2,char *param_3);

#endif // SPRITE_H