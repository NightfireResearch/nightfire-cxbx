#ifndef PSISPRITE_H_
#define PSISPRITE_H_

#include "../actionhelpers.h"

typedef struct SPRITE_DRAW {
    char unknown[0x30];
} SPRITE_DRAW;

void psiDrawSprites(SPRITE_DRAW* spriteList, int numItems);

#endif // PSISPRITE_H_