#ifndef SPRITE_H
#define SPRITE_H

struct sprite;

#pragma pack(push, 1)

typedef struct sprite {
    char unknown[8];
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
    short unknown44[2];
    short onscreenWidth;
    short onscreenHeight;
    short spritesheetX;
    short spritesheetY;
    short spritesheetWidth;
    short spritesheetHeight;
    char maybeEnabled; // 0x27 results in it being visible, 0xff results in it being invisible? Unclear.
    char linkedViewer;
    char unknown7[2];
    float unknown8[2];
} sprite;

static_assert(sizeof(sprite) == 0x44, "Wrong size for sprite");


#pragma pack(pop)


void Sprite_SetText(sprite *param_1,char *param_2);

#endif // SPRITE_H