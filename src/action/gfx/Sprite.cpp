#include "Sprite.h"
#include "Font.h"
#include "../util/Sort.h"
#include "../util/hashtable.h"
#include "../math/X87Ops.h"

#include <math.h>

// for strlen
#include <string.h>

// for NULL
#include <stddef.h>

#define MAX_SPRITES 0x200 // entries in SpriteList's pool, and so the most Sprite_BuildList can ever collect

// The pool every sprite comes from. Sprite_InitLists, Sprite_Create and Sprite_Delete are its only users, all ours.
// XBE_GLOBAL(0x0029aa98, 0x18)
static DLISTINFO_tag SpriteList;

// Sprite_BuildList's output, handed back to Game_Draw / LoadScreen_Draw, which pass it straight to
// View_DrawSprites. Nothing else names it (PS2: SprActiveList).
// XBE_GLOBAL(0x0029a298, 0x800)
static sprite* SprActiveList[MAX_SPRITES];

// The sprite flag bits Sprite_BuildList clears on every sprite it collects (the low nibble of flags)
#define SPRITE_FLAGS_KEEP_MASK 0xfff0

// AUTOINJECT
void Sprite_InitLists(void) {
    DList_Init(&SpriteList, sizeof(sprite), MAX_SPRITES);
}

// Collects the drawable sprites linked to one viewer into SprActiveList, sorts them, and returns the list.
// A sprite is drawable when it is not hidden, belongs to this viewer, has a non-zero low byte in its colour
// (the original tests colourTint & 0xff, whatever that byte means - probably alpha), and has something to draw:
// text or a texture.
//
// *startOut is always 0. *lastForegroundOut is left at the index of the LAST sprite, from the front of the sorted
// list, whose depth is at least 50 - an index, not a count, and 0 both when only the first sprite
// qualifies and when none does; the original has this ambiguity and so does this. The callers differ:
// LoadScreen_Draw passes *startOut to View_DrawSprites as the first index to draw, but Game_Draw passes
// *lastForegroundOut (it tests *startOut only in its viewer 4-9 loop, where it is always 0), so in the game's
// views the sprites in front of that index are not drawn.
//
// AUTOINJECT
sprite** Sprite_BuildList(ushort viewer, undefined4 *startOut, uint *lastForegroundOut, uint *countOut) {

    uint count = 0;

    for(sprite *spr = (sprite *)SpriteList.activeList.head; spr != NULL; spr = (sprite *)spr->node.next) {

        if(spr->depth == SPRITE_HIDDEN)
            continue;

        // The original widens the byte to 16 bits and compares with the whole ushort, so a viewer above 255
        // never matches
        if((ushort)(uchar)spr->linkedViewer != viewer)
            continue;

        if((spr->colourTint & 0xff) == 0)
            continue;

        if(spr->text == NULL && spr->textureIndex == 0)
            continue;

        spr->flags &= SPRITE_FLAGS_KEEP_MASK;
        SprActiveList[count] = spr;
        count++;
    }

    *startOut = 0;
    *lastForegroundOut = 0;
    *countOut = count;

    if(count != 0) {
        QuickSort(SprActiveList, count, sizeof(sprite*), Compare_Sprites, NULL);

        // Signed compare in the original (JL): depth is read as a signed char here, so 0x80-0xff count
        // as below 50 - moot for 0xff, which never got into the list
        for(uint i = 0; i < count; i++) {
            if((signed char)SprActiveList[i]->depth < SPRITE_FOREGROUND_MIN)
                break;
            *lastForegroundOut = i;
        }
    }

    return SprActiveList;
}

// A text sprite's format, and its size measured in it, kept in spritesheetWidth and spritesheetHeight
// AUTOINJECT
void Sprite_SetFmt(sprite *spr, char *fmt) {
    if (spr->text == NULL) {
        spr->format = fmt;
        return;
    }
    GPOINT extent;
    spr->format = fmt;
    Font_GetTextExtent(spr->text, fmt, &extent, &spr->scale, -1);
    spr->spritesheetWidth = (short)Ftol(extent.x);
    spr->spritesheetHeight = (short)Ftol(extent.y);
}

// AUTOINJECT
void Sprite_SetText(sprite *spr, char *text) {

    if (spr == NULL || text == NULL)
        return;

    spr->text = text;
    spr->length = strlen(text);

    Sprite_SetFmt(spr, spr->format);

}

// AUTOINJECT
sprite* Sprite_Create(void) {

    sprite *spr = (sprite *)DList_MoveFromFree2InUse(&SpriteList);

    if (spr == NULL)
        return NULL;

    spr->text = NULL;
    spr->length = 0;
    spr->format = (char *)0xfcfcfcfc; // TODO: Some special signifier value?
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

// A sprite's text and its format, measured when it has both
// AUTOINJECT
void Sprite_SetTextNFmt(sprite *spr, char *text, char *fmt) {
    spr->text = text;
    spr->format = fmt;
    if (text == NULL || fmt == NULL)
        return;
    spr->length = (short)strlen(text);
    spr->format = fmt;
    spr->text = text;
    Sprite_SetFmt(spr, fmt);
}

// A sprite's shown state, position (its base position too), colour and, unless 0, texture
// AUTOINJECT
void Sprite_SetParams(sprite *spr, uchar enabled, short x, short y, HASHCODE texture, uint colour) {
    if (spr == NULL)
        return;
    spr->depth = enabled;
    spr->positionX = x;
    spr->backupOnscreenWidth = x;
    spr->positionY = y;
    spr->backupOnscreenHeight = y;
    spr->colourTint = colour;
    spr->flags = 0;
    if (texture != 0)
        hashtable_set_sprite(spr, texture);
}

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
        } else {
            NF_WARN("Sprite With no texture set\n"); // GC check (0x8006637c)
        }

        spr->positionX = sprInfo->posX;
        spr->positionY = sprInfo->posY;
        spr->onscreenWidth = sprInfo->onscreenWidth != 0 ? sprInfo->onscreenWidth : spr->backupOnscreenWidth;
        spr->onscreenHeight = sprInfo->onscreenHeight != 0 ? sprInfo->onscreenHeight : spr->backupOnscreenHeight;
        spr->spritesheetX = sprInfo->spritesheetX;
        spr->spritesheetY = sprInfo->spritesheetY;
        // With no box given, the original spans one texel less than the texture (DEC at 0x000ceed1, 0x000ceee8), so
        // the whole-texture sprite loses its last column and row; the fix spans it all, as hashtable_set_sprite does
        // (docs/original-bug-fixes.md).
#if FIX_ORIGINAL_GAME_BUGS
        spr->spritesheetWidth = sprInfo->spritesheetWidth != 0 ? sprInfo->spritesheetWidth : spr->backupOnscreenWidth;
        spr->spritesheetHeight = sprInfo->spritesheetHeight != 0 ? sprInfo->spritesheetHeight : spr->backupOnscreenHeight;
#else
        spr->spritesheetWidth = sprInfo->spritesheetWidth != 0 ? sprInfo->spritesheetWidth : (spr->backupOnscreenWidth - 1);
        spr->spritesheetHeight = sprInfo->spritesheetHeight != 0 ? sprInfo->spritesheetHeight : (spr->backupOnscreenHeight - 1);
#endif

    } else { 
        
        // Text-based sprite
        
        spr->depth = sprInfo->depth;
        spr->positionX = sprInfo->posX;
        spr->positionY = sprInfo->posY;
        spr->backupOnscreenWidth = sprInfo->posX;
        spr->backupOnscreenHeight = sprInfo->posY;
        spr->colourTint = sprInfo->colour;
        spr->flags = 0;
        
        Sprite_SetTextNFmt(spr, (char*)Txt_BindLabel(sprInfo->textRef, 0), sprInfo->format);
        Txt_LockString(spr->text);

    }
    
    // Common to both image and text-based sprites
    spr->colourTint = sprInfo->colour;
    spr->shadowColour = sprInfo->shadowColour;
    spr->depth = sprInfo->depth;
    spr->flags = sprInfo->flags;

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