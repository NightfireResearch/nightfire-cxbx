#include "Sprite.h"

// for strlen
#include <string.h>

// for NULL
#include <stddef.h>

// AUTOGEN
void Sprite_SetFmt(sprite*, char*);

// AUTOINJECT
void Sprite_SetText(sprite *spr, char *text) {

    if (spr == NULL || text == NULL)
        return;

    spr->text = text;
    spr->length = strlen(text);

    Sprite_SetFmt(spr, spr->maybeClippedString);

}