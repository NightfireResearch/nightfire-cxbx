#ifndef SPRITE_H
#define SPRITE_H

struct sprite;

#pragma pack(push, 1)

typedef struct sprite {
    char unknown[8];
    unsigned int createdOnFrame;
    unsigned int unknown2[2];
    char unknown3[4];
    char *text;
    char *maybeClippedString;
    short length;
    short unknown4[5];
    short someWidth;
    short someHeight;
    short unknown5[4];
    char unknown6;
    char linkedViewer;
    char unknown7[2];
    float unknown8[2];
} sprite;

static_assert(sizeof(sprite) == 0x44, "Wrong size for sprite");


#pragma pack(pop)


void Sprite_SetText(sprite *param_1,char *param_2);

#endif // SPRITE_H