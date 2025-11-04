#include "Sprite.h"

// for strlen
#include <string.h>

// for NULL
#include <stddef.h>

#define SpriteList (*(DLISTINFO_tag*)0x0029aa98)


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

// AUTOINJECT
sprite* Sprite_Create(void) {

    sprite *spr = (sprite *)DList_MoveFromFree2InUse(&SpriteList);

    if (spr == NULL)
        return NULL;

    spr->text = NULL;
    spr->length = 0;
    spr->maybeClippedString = (char *)0xfcfcfcfc; // TODO: Some special signifier value?
    spr->positionX = 0;
    spr->positionY = 0;
    spr->spritesheetX = 0;
    spr->spritesheetY = 0;
    spr->spritesheetWidth = 0;
    spr->spritesheetHeight = 0;
    spr->createdOnFrame = GameState.NumFramesUnpaused;
    spr->scale.x = 1.0;
    spr->scale.y = 1.0;

    return spr;
}

// AUTOGEN
sprite* Sprite_Create2(SpriteInfo *param_1);

// AUTOINJECT
void Sprite_Link2Viewer(sprite *spr, ushort playerNum) {
    
    if(spr == NULL)
        return;

    spr->linkedViewer = playerNum;
}

// AUTOINJECT
sprite* Sprite_CreateAndLink2Viewer(char viewer) {

    sprite *spr = Sprite_Create();
    
    Sprite_Link2Viewer(spr, viewer);
    
    return spr;
}

// AUTOINJECT
void Sprite_Delete(sprite* sprite) {

    if(sprite == NULL)
        return;

    Txt_UnlockString(sprite->text);
    DList_RemoveFromInUse2Free(&SpriteList, (LLNODE_tag*)sprite);
}