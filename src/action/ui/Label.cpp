// The menus' labels (PS2: menu_controls.cpp)

#include "Label.h"
#include "MenuCommon.h"
#include "Process.h"

#include "../engine/Text.h"
#include "../gfx/Font.h"
#include "../util/hashtable.h"

#include <stdio.h>
#include <string.h>

#pragma fp_contract(off)

// What MessageType_SetFont adds to a font: a scale of 100
#define FONT_SCALE_100 "\xfc\x64\xfb\x64"

// The frames a label fades over to its normal colour
#define LABEL_FADE_FRAMES 15

// A label from the menu data: a text in its own format, or an icon. One the size of the screen or larger fills it.
// AUTOINJECT
void Label_Init(M_LABEL *label, NEW_CONTROL *create, M_CONTROL *parent) {
    if (create->width >= SCREEN_WIDTH && create->height >= SCREEN_HEIGHT) {
        create->width = SCREEN_WIDTH;
        create->height = SCREEN_HEIGHT;
        create->x = 0;
        create->y = 0;
    }
    label->control.type = ControlType_Label;
    label->control.state = CONTROL_STATE_SHOWN;
    label->control.parent = parent;
    label->control.extraControlHash = (HASHCODE)0;
    label->control.skin = 0;
    label->control.hashcode = create->hashcode;
    label->control.id = create->id;
    label->control.sprites = NULL;
    label->control.depth = create->depth;
    label->highlightColour = 0xff;
    label->normalColour = 0xffffff7f;
    label->mode = LABEL_OWN_COLOURS | LABEL_SHOWS_TEXT;
    label->faded = 0;
    label->textFlag2000 = 1;
    label->text = create->text;
    label->textLabel = Action_TranslatedText_NULLVALUE;
    label->iconFlags = 0;
    label->pulse = 0;
    label->pulseColour = 0x808080ff;
    label->alwaysHighlighted = 0;
    if (create->text == NULL)
        label->text = MenuEmptyString;
    memcpy(label->format, LabelFormat, 9);

    sprite *model = &label->model;
    model->text = NULL;
    model->onscreenWidth = create->width;
    model->maybeEnabled = (uchar)label->control.depth;
    model->onscreenHeight = create->height;
    model->colourTint = 0x7f7f7fc0;
    model->unknown1 = 0x80;
    model->linkedViewer = 7;
    model->maybeFlags = 0;
    model->unknownDataMaybeTexPtr = 0;
    model->scale.x = 1.0f;
    model->scale.y = 1.0f;
    model->maybeClippedString = LabelFormat;
    label->drawn = NULL;
    Sprite_SetText(model, create->text != NULL ? create->text : MenuEmptyString);
    Menu_ClipString(model, create->width, model->maybeClippedString);
    // By the label's width, which is only set below
    if (Font_GetAlignment(model->maybeClippedString) == ALIGN_CENTRE)
        model->positionX += Menu_ClipString(model, label->control.width, model->maybeClippedString) >> 1;
    if (Font_GetAlignment(model->maybeClippedString) == ALIGN_RIGHT)
        model->positionX += Menu_ClipString(model, label->control.width, model->maybeClippedString);
    label->control.x = create->x;
    label->control.y = create->y;
    label->control.width = create->width;
    label->control.height = create->height;
    Script_PrepareScripting(&label->control);
}

// Brought up to date at once while its page has a focused control
static void Label_Refresh(M_LABEL *label) {
    M_PAGE *page = Menu_GetPage(&label->control);
    if (page->focused != NULL)
        Label_Update((M_MANAGER *)page->control.parent, page, label, false);
}

// AUTOINJECT
int Label_SendMessage(M_LABEL *label, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&label->control);
        case MessageType_ScriptPlay:
            return Script_Play(&label->control, arg1, (char)arg2);
        case MessageType_SetText:
            label->mode |= LABEL_SHOWS_TEXT;
            if (arg1 != 0) {
                label->text = (char *)arg1;
                label->textLabel = Action_TranslatedText_NULLVALUE;
            } else if (arg2 != 0) {
                label->text = (char *)Txt_BindLabel((Action_TranslatedText)arg2, 0);
                label->textLabel = (Action_TranslatedText)arg2;
            } else {
                label->text = MenuSpaceString;
                label->textLabel = Action_TranslatedText_NULLVALUE;
            }
            Label_Refresh(label);
            return 1;
        case MessageType_SetColour:
            // A tint alone, or the one colour it always has
            label->model.colourTint = arg1;
            if (arg2 != 0) {
                label->mode |= LABEL_OWN_COLOURS;
                return 1;
            }
            label->mode &= ~LABEL_OWN_COLOURS;
            label->model.unknown1 = (arg1 & 0xff) < 0x80 ? arg1 & 0xff : 0x80;
            label->highlightColour = arg1;
            label->normalColour = arg1;
            return 1;
        case MessageType_SetHighlightColour:
            label->mode |= LABEL_OWN_COLOURS;
            label->highlightColour = arg1;
            label->model.colourTint = arg1;
            return 1;
        case MessageType_SetNormalColour:
            label->mode |= LABEL_OWN_COLOURS;
            label->normalColour = arg1;
            label->model.colourTint = arg1;
            return 1;
        case MessageType_SetIconSpriteFlags:
            label->iconFlags = arg1;
            return 1;
        case MessageType_SetScript:
            return Control_SetScript(&label->control, arg1, arg2);
        case MessageType_SetFont:
            sprintf(label->format, "%s%s", (char *)arg1, FONT_SCALE_100);
            label->model.maybeClippedString = label->format;
            return 1;
        case MessageType_SetIcon:
            if (arg1 == 0)
                return 1;
            label->mode &= ~LABEL_SHOWS_TEXT;
            label->icon = (HASHCODE)arg1;
            Label_Refresh(label);
            return 1;
        case MessageType_SetAlpha:
            label->control.drawState |= 4;
            label->model.unknown1 = arg1;
            return 1;
        case MessageType_SetPulseColour:
            label->pulseColour = arg1;
            return 1;
        case MessageType_SetState:
            label->control.state = (uchar)arg1;
            if (label->control.state & CONTROL_STATE_HIDDEN) {
                label->model.maybeEnabled = MENU_SPRITE_HIDDEN;
                if (label->drawn != NULL)
                    label->drawn->maybeEnabled = MENU_SPRITE_HIDDEN;
            }
            Label_Refresh(label);
            return 1;
        case MessageType_SetPulse:
            label->pulse = (uchar)arg1;
            return 0;
        case MessageType_SetIconRect:
            label->model.spritesheetX = (uint)arg1 >> 16;
            label->model.spritesheetY = (short)arg1;
            label->model.spritesheetWidth = (uint)arg2 >> 16;
            label->model.spritesheetHeight = (short)arg2;
            return 1;
        case MessageType_SetTextFlag2000:
            label->textFlag2000 = (uchar)arg1;
            return 1;
        case MessageType_SetAlwaysHighlighted:
            label->alwaysHighlighted = (uchar)arg1;
            return 0;
        case MessageType_GetText:
            return (int)label->text;
        case MessageType_GetColour:
            return label->model.colourTint;
        case MessageType_GetHighlightColour:
            return label->highlightColour;
        case MessageType_GetNormalColour:
            return label->normalColour;
        case MessageType_GetControl:
            return (int)label;
        case MessageType_GetId:
            return label->control.id;
        case MessageType_GetIconSize:
            return label->model.spritesheetWidth << 16 | (ushort)label->model.spritesheetHeight;
        case MessageType_GetIconPos:
            return label->model.spritesheetX << 16 | (ushort)label->model.spritesheetY;
        case MessageType_Destroy:
            Menu_DeleteSprite(label->drawn);
            Script_DelAllScripts(&label->control);
            if (label->control.parent->type == ControlType_Page)
                Menu_Free((void **)&label, 3);
            return 0;
    }
    return 0;
}

// Hidden (or its owner), its sprite goes. Else the model is coloured (with its own colours: the highlight while the
// cursor is over it or the focus has its hashcode, else a fade to the normal colour), placed (a text by its format's
// alignment and centred on its height, an icon over the whole label), its text bound and clipped, and copied into the
// sprite; select and the alternative selects on it go to its owner.
// AUTOINJECT
void Label_Update(M_MANAGER *mgr, M_PAGE *page, M_LABEL *label, bool cursorOver) {
    if ((label->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (label->control.state & CONTROL_STATE_HIDDEN)) {
        label->control.state |= CONTROL_STATE_UNDRAWN;
        if (label->drawn == NULL)
            return;
        label->drawn->maybeEnabled = MENU_SPRITE_HIDDEN;
        Menu_DeleteSprite(label->drawn);
        label->drawn = NULL;
        return;
    }
    label->control.state &= ~CONTROL_STATE_UNDRAWN;

    char pressed = 0;
    if (Menu_CursorOverMe(mgr, &label->control)) {
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_SELECT, 4))
            pressed = 1;
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_1, 4))
            pressed = 2;
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_2, 4))
            pressed = 3;
    }

    sprite *model = &label->model;
    model->maybeEnabled = (uchar)(label->control.depth - page->control.depth);
    uint alpha = model->colourTint & 0xff;
    if (alpha > 0x80)
        alpha = 0x80;
    model->unknown1 = (model->unknown1 & 0xffffff00) | alpha;
    if (label->mode & LABEL_OWN_COLOURS) {
        if (Menu_CursorOverMe(mgr, &label->control) || cursorOver ||
            (label->control.hashcode == page->focused->hashcode &&
             (label->control.id == page->focused->id || label->alwaysHighlighted))) {
            Menu_GetNormalTextFmt(model);
            label->faded = 0;
            if (label->pulse && label->control.state == CONTROL_STATE_SHOWN)
                model->colourTint = PulsedColour(label->pulseColour, Menu_AlphaGet());
            else
                model->colourTint = label->highlightColour;
        } else if (label->faded == 0) {
            if (label->control.framesFocused > 5) {
                M_MESSAGE fade = {};
                fade.type = MESSAGE_PROCESS;
                fade.arg1 = label->normalColour;
                fade.arg2 = 1;
                Process_Create(&label->control, &fade, LABEL_FADE_FRAMES, 1);
            } else {
                model->colourTint = label->normalColour;
            }
            Menu_GetNormalTextFmt(model);
            label->faded = 1;
        }
    }

    GPOINT extent;
    extent.x = 1.0f;
    extent.y = 1.0f;
    Font_GetTextExtent(MenuAlphabet, model->maybeClippedString, &extent, NULL, -1);
    float lineHeight = extent.y;
    if (!(label->mode & LABEL_SHOWS_TEXT)) {
        short width = model->spritesheetWidth;
        short height = model->spritesheetHeight;
        hashtable_set_sprite(model, label->icon);
        model->spritesheetHeight = height;
        model->maybeFlags = 0;
        model->spritesheetWidth = width;
        model->text = NULL;
        model->maybeClippedString = NULL;
        model->positionX = page->control.x + label->control.x;
        model->positionY = page->control.y + label->control.y;
        model->onscreenWidth = label->control.width;
        model->onscreenHeight = label->control.height;
        model->maybeFlags = (short)label->iconFlags;
    } else {
        if (Font_GetAlignment(model->maybeClippedString) == ALIGN_CENTRE)
            model->positionX = (label->control.width >> 1) + page->control.x + label->control.x;
        else if (Font_GetAlignment(model->maybeClippedString) == ALIGN_RIGHT)
            model->positionX = page->control.x + label->control.width + label->control.x;
        else
            model->positionX = page->control.x + label->control.x;
        // A text of more than one line is placed by its first line's top
        uchar oneLine = 1;
        if (strlen(label->text) != 0 && strstr(label->text, "\n") != NULL)
            oneLine = 0;
        model->unknownDataMaybeTexPtr = 0;
        short y = Ftol((label->control.height >> 1) + label->control.y + oneLine * (double)lineHeight * 0.5 +
                       page->control.y - 1.0);
        model->positionY = y;
        model->backupOnscreenHeight = y;
        model->backupOnscreenWidth = model->positionX;
        if (label->textLabel != Action_TranslatedText_NULLVALUE) {
            label->text = (char *)Txt_BindLabel(label->textLabel, 0);
            if (strcmp(label->text, "") == 0)
                label->text = MenuSpaceString;
        }
        if (strlen(label->text) != 0)
            Sprite_SetText(model, label->text);
        if (label->textFlag2000)
            model->maybeFlags |= 0x2000;
        else
            model->maybeFlags &= ~0x2000;
        Menu_ClipString(model, label->control.width, model->maybeClippedString);
    }

    if (mgr->takingInput && !mgr->inputLocked) {
        if (pressed == 1) {
            Menu_PlaySound(MENU_SFX_SELECT);
            __Menu_SendMessage(label->control.parent, MessageType_Select, (int)label, 0);
        } else if (pressed == 2) {
            Menu_PlaySound(MENU_SFX_ALTSELECT_1);
            __Menu_SendMessage(label->control.parent, MessageType_AltSelect1, (int)label, 0);
        } else if (pressed == 3) {
            Menu_PlaySound(MENU_SFX_ALTSELECT_2);
            __Menu_SendMessage(label->control.parent, MessageType_AltSelect2, (int)label, 0);
        }
    }

    if (label->drawn == NULL)
        label->drawn = Menu_CreateSprite(NULL);
    if (label->drawn == NULL)
        return;
    LLNODE_tag node = label->drawn->node;
    *label->drawn = *model;
    label->drawn->node = node;
}
