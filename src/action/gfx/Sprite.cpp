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
void Sprite_SetTextNFmt(sprite *param_1,char *param_2,char *param_3);

// AUTOINJECT
sprite* Sprite_Create2(SpriteInfo *sprInfo) {

    sprite* spr = Sprite_Create();
    if (spr == NULL) 
        return NULL;
    
    if (sprInfo == NULL)
        return spr;

    if (sprInfo->textRef == Action_TranslatedText_NULLVALUE) {

        // Image-based sprite

        if (sprInfo->textureHashcode != 0) {
            hashtable_set_sprite(spr, sprInfo->textureHashcode);
        }

        spr->positionX = sprInfo->posX;
        spr->positionY = sprInfo->posY;
        spr->onscreenWidth = sprInfo->onscreenWidth != 0 ? sprInfo->onscreenWidth : spr->backupOnscreenWidth;
        spr->onscreenHeight = sprInfo->onscreenHeight != 0 ? sprInfo->onscreenHeight : spr->backupOnscreenHeight;
        spr->spritesheetX = sprInfo->spritesheetX;
        spr->spritesheetY = sprInfo->spritesheetY;
        spr->spritesheetWidth = sprInfo->maybeSpritesheetWidth != 0 ? sprInfo->maybeSpritesheetWidth : (spr->backupOnscreenWidth - 1);
        spr->spritesheetHeight = sprInfo->maybeSpritesheetHeight != 0 ? sprInfo->maybeSpritesheetHeight : (spr->backupOnscreenHeight - 1);

    } else { 
        
        // Text-based sprite
        
        spr->maybeEnabled = sprInfo->maybeEnabled;
        spr->positionX = sprInfo->posX;
        spr->positionY = sprInfo->posY;
        spr->backupOnscreenWidth = sprInfo->posX;
        spr->backupOnscreenHeight = sprInfo->posY;
        spr->colourTint = sprInfo->colour;
        spr->maybeFlags = 0;
        
        Sprite_SetTextNFmt(spr, (char*)Txt_BindLabel(sprInfo->textRef, 0), sprInfo->maybeClippedText);
        Txt_LockString(spr->text);

    }
    
    // Common to both image and text-based sprites
    spr->colourTint = sprInfo->colour;
    spr->unknown1 = sprInfo->unknown1;
    spr->maybeEnabled = sprInfo->maybeEnabled;
    spr->maybeFlags = sprInfo->maybeFlags;

    if (sprInfo->linkedViewer > -1) {
        Sprite_Link2Viewer(spr, sprInfo->linkedViewer);
    }

    return spr;
}

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