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

// AUTOGEN
sprite* Sprite_Create2(SpriteInfo *param_1);

// AUTOINJECT
void Sprite_Link2Viewer(sprite *spr,ushort playerNum) {
    if(spr == NULL)
        return;
    spr->linkedViewer = playerNum;
}

#define SpriteList (*(DList*)0x0029aa98)

// AUTOINJECT
void Sprite_Delete(sprite* sprite) {

    if(sprite == NULL)
        return;

    Txt_UnlockString(sprite->text);
    DList_MoveFromInUse2Free(&SpriteList, sprite);
}