// The menus' memos: a text wrapped onto lines, scrolled by a bar at the right (PS2: menu_controls.cpp)

#include "Memo.h"
#include "MenuCommon.h"
#include "Component.h"
#include "Scroll.h"

#include "../gfx/Font.h"
#include "../gfx/Sprite.h"

#include <string.h>

#pragma fp_contract(off)

#define MEMO_BAR_WIDTH 16

// A memo from the menu data: its skin's component 3, the plain format, centred both ways, and a bar at the right (of
// 0 to 10 at first) without its own skin
// AUTOINJECT
void Memo_Init(M_MEMO *memo, NEW_CONTROL *create, M_CONTROL *parent) {
    GPOINT extent;
    Font_GetTextExtent(MenuAlphabet, PlainFormat, &extent, NULL, -1);
    memo->control.type = ControlType_Memo;
    memo->control.state = CONTROL_STATE_SHOWN;
    memo->control.skin = (uint)create->skin << 16 | 3;
    memo->control.parent = parent;
    memo->topLine = 0;
    memo->control.hashcode = create->hashcode;
    memo->control.id = create->id;
    // The visible lines are the height over the line's height as computed, not as stored
    double lineHeight = extent.y * (double)1.5f;
    memo->lineHeight = (float)lineHeight;
    memo->control.sprites = NULL;
    memo->highlightColour = 0xff;
    memo->normalColour = 0x404040ff;
    memo->alpha = 0x80;
    memo->visibleLines = Ftol(create->height / lineHeight);
    memo->numLines = 0;
    memo->control.depth = create->depth;
    memo->text = MenuEmptyString;
    memo->centred = 1;
    memo->centredVertically = 1;
    memo->format = PlainFormat;
    memo->linesShown = 0;
    memset(memo->lines, 0, sizeof(memo->lines));
    memo->control.x = create->x;
    memo->control.y = create->y;
    memo->control.width = create->width;
    memo->control.height = create->height;

    NEW_CONTROL bar = *create;
    bar.x += bar.width - MEMO_BAR_WIDTH;
    bar.height = create->height;
    bar.min = 0;
    bar.hasBar = 0;
    bar.keepState = 0;
    bar.width = MEMO_BAR_WIDTH;
    bar.max = 10;
    bar.depth = create->depth - 1;
    Scroll_Init(&memo->scroll, &bar, &memo->control);
    Script_PrepareScripting(&memo->control);
}

// Brought up to date at once, without input, while its page has a focused control
static void Memo_Refresh(M_MEMO *memo) {
    M_PAGE *page = Menu_GetPage(&memo->control);
    M_MANAGER *mgr = (M_MANAGER *)page->control.parent;
    if (page->focused == NULL)
        return;
    bool input = mgr->takingInput;
    mgr->takingInput = false;
    Memo_Update(mgr, page, memo);
    mgr->takingInput = input;
}

// AUTOINJECT
intptr_t Memo_SendMessage(M_MEMO *memo, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_Select:
            return __Menu_SendMessage(memo->control.parent, MessageType_Select, (intptr_t)memo, 0);
        case MessageType_Destroy:
            Component_Free(&memo->control);
            Scroll_SendMessage(&memo->scroll, MessageType_Destroy, 0, arg2);
            for (ushort i = 0; i < (ushort)memo->numLines; i++) {
                if (memo->lines[i] == NULL)
                    break;
                Sprite_Delete(memo->lines[i]);
            }
            Script_DelAllScripts(&memo->control);
            if (memo->control.parent->type == ControlType_Page)
                Menu_Free((void **)&memo, 3);
            return 0;
        case MessageType_GetText:
            return (intptr_t)memo->text;
        case MessageType_GetHighlightColour:
            return memo->highlightColour;
        case MessageType_GetNormalColour:
            return memo->normalColour;
        case MessageType_MemoGetNumLines:
            return memo->linesShown;
        case MessageType_GetControl:
            return (intptr_t)memo;
        case MessageType_ScriptPlay:
            return Script_Play(&memo->control, arg1, (char)arg2);
        case MessageType_MemoSetOption280:
            memo->centred = (uchar)arg1;
            return 1;
        case MessageType_SetText:
            memo->text = (char *)arg1;
            memo->scroll.value = 0;
            Memo_Refresh(memo);
            return 1;
        case MessageType_SetHighlightColour:
            memo->highlightColour = arg1;
            return 1;
        case MessageType_SetNormalColour:
            memo->normalColour = arg1;
            return 1;
        case MessageType_SetScript:
            return Control_SetScript(&memo->control, arg1, arg2);
        case MessageType_SetFont:
            memo->format = (char *)arg1;
            return 1;
        case MessageType_SetLineSpacing: {
            GPOINT extent;
            Font_GetTextExtent(MenuAlphabet, memo->format, &extent, NULL, -1);
            memo->lineHeight = (float)(arg1 * (double)0.01f * extent.y);
            // The line's height over the memo's, the other way round from Memo_Update's
            memo->visibleLines = Ftol(memo->lineHeight / (double)memo->control.height);
            return 0;
        }
        case MessageType_SetAlpha:
            memo->alpha = arg1;
            return 1;
        case MessageType_SetState:
            memo->control.state = (uchar)arg1;
            Scroll_SendMessage(&memo->scroll, MessageType_SetState, arg1, arg2);
            if (memo->control.state & CONTROL_STATE_HIDDEN) {
                Component_SetupInstance(&memo->control, 0, -1, -1, -1, -1);
                for (int i = 0; i < MEMO_LINES; i++) {
                    if (memo->lines[i] == NULL)
                        break;
                    memo->lines[i]->maybeEnabled = MENU_SPRITE_HIDDEN;
                }
            }
            Memo_Refresh(memo);
            return 1;
        case MessageType_MemoSetOption282:
            memo->centredVertically = (uchar)arg1;
            return 0;
    }
    return 0;
}

// Hidden (or its owner), its lines' sprites go. Else the bar shows while more than one line is visible, the text is
// wrapped from the scroll's line on into sprites (each line centred if asked, the lines centred on the memo's height if
// asked), the scroll's range set to the lines not shown, and select, the alternative selects and back go to its owner.
// The lines are not bounded by the sprites' table: one past it, the hiding reads the visible lines' count as a sprite.
// AUTOINJECT
void Memo_Update(M_MANAGER *mgr, M_PAGE *page, M_MEMO *memo) {
    char pressed = 0;
    if ((memo->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (memo->control.state & CONTROL_STATE_HIDDEN)) {
        memo->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&memo->control);
        Scroll_Update(mgr, page, &memo->scroll);
        for (ushort i = 0; i < (ushort)memo->numLines; i++) {
            if (memo->lines[i] == NULL)
                return;
            Sprite_Delete(memo->lines[i]);
            memo->lines[i] = NULL;
        }
        return;
    }
    memo->control.state &= ~CONTROL_STATE_UNDRAWN;
    Menu_ResizeControl(&memo->scroll.control, memo->control.width + memo->control.x - 0x12,
                       (memo->control.height >> 1) + memo->control.y - 8, 0x24, 0x10);
    memo->visibleLines = Ftol(memo->control.height / (double)memo->lineHeight);
    if (memo->topLine + memo->visibleLines > memo->numLines && memo->numLines > memo->visibleLines)
        memo->topLine = memo->numLines - memo->visibleLines;
    if (page->focused != NULL && page->focused->hashcode == memo->control.hashcode)
        memo->control.drawState = 0x20;
    else
        memo->control.drawState = 0x10;
    if (memo->visibleLines > 1 && (memo->scroll.control.state & CONTROL_STATE_HIDDEN)) {
        Scroll_SendMessage(&memo->scroll, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        Component_SetupInstance(&memo->control, memo->control.drawState, -1, -1,
                                memo->control.width - MEMO_BAR_WIDTH, memo->control.height);
    } else if (memo->visibleLines == 1 && !(memo->scroll.control.state & CONTROL_STATE_HIDDEN)) {
        Scroll_SendMessage(&memo->scroll, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        Component_SetupInstance(&memo->control, memo->control.drawState, -1, -1, memo->control.width,
                                memo->control.height);
    }

    memo->linesShown = 0;
    if (memo->text != NULL) {
        for (int i = 0; i <= MEMO_LINES; i++) {
            if (memo->lines[i] == NULL)
                break;
            memo->lines[i]->maybeEnabled = MENU_SPRITE_HIDDEN;
        }
        memo->topLine = Scroll_SendMessage(&memo->scroll, MessageType_GetValue, 0, 0);
        char *rest = memo->text;
        memo->numLines = 0;
        do {
            int length = strlen(rest);
            int width = memo->control.width - Component_GetHandle(&memo->control)->right -
                        Component_GetHandle(&memo->control)->left - MEMO_BAR_WIDTH;
            char *line = Font_WordWrapString(&rest, length, memo->format, width);
            if (line == NULL)
                break;
            GPOINT extent;
            Font_GetTextExtent(line, memo->format, &extent, NULL, -1);
            int indent = memo->centred == 1 ? Ftol((memo->control.width - (double)extent.x) * (double)0.5f) : 0;
            if (memo->numLines < memo->visibleLines + memo->topLine)
                memo->visibleLines++;
            if (memo->numLines >= memo->topLine) {
                if (memo->lines[memo->linesShown] == NULL)
                    memo->lines[memo->linesShown] = Sprite_Create();
                uint colour = page->focused->hashcode == memo->control.hashcode ? memo->highlightColour
                                                                                 : memo->normalColour;
                short y = Ftol(Component_GetHandle(&memo->control)->top +
                               (memo->numLines - memo->topLine) * (double)memo->lineHeight + Ftol(memo->lineHeight) +
                               memo->control.y) +
                          page->control.y;
                short x = Component_GetHandle(&memo->control)->left + page->control.x + memo->control.x + indent + 2;
                Menu_InitializeSprite(memo->lines[memo->linesShown], (uchar)(memo->control.depth - 5), x, y,
                                      memo->control.width - MEMO_BAR_WIDTH, 11, line, (HASHCODE)0, colour, memo->alpha,
                                      0, 0, 0, 0, 0x2000);
                memo->lines[memo->linesShown]->maybeClippedString = memo->format;
                memo->linesShown++;
            }
            memo->numLines++;
        } while (rest != NULL);
    }

    if (memo->centredVertically) {
        short offset = Ftol((memo->control.height - memo->linesShown * (double)memo->lineHeight) * (double)0.5f);
        for (uchar i = 0; i < memo->linesShown; i++)
            memo->lines[i]->positionY += offset;
    }

    if (Menu_CursorOverMe(mgr, &memo->control)) {
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_SELECT, 4))
            pressed = 1;
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_1, 4))
            pressed = 2;
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_2, 4))
            pressed = 3;
    }

    int hidden = memo->numLines - memo->visibleLines;
    Scroll_SendMessage(&memo->scroll, MessageType_SetRange, 0, hidden > 0 ? hidden : 0);
    if (memo->numLines - memo->visibleLines <= 0 && memo->scroll.control.state == CONTROL_STATE_SHOWN)
        Scroll_SendMessage(&memo->scroll, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    else if (memo->scroll.control.state & CONTROL_STATE_HIDDEN)
        Scroll_SendMessage(&memo->scroll, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
    Component_SetupInstance(&memo->control, memo->control.drawState, -1, -1, memo->control.width,
                            memo->control.height);
    if (!(memo->scroll.control.state & CONTROL_STATE_HIDDEN))
        Scroll_Update(mgr, page, &memo->scroll);

    // Back from the first pad
    if (page->focused->hashcode == memo->control.hashcode && Menu_InputAction(0, ACTION_MENU_BACK, 4))
        __Menu_SendMessage(memo->control.parent, MessageType_MemoBack, (intptr_t)memo, 0);
    if (!mgr->takingInput || mgr->inputLocked)
        return;
    if (pressed == 1) {
        Menu_PlaySound(MENU_SFX_SELECT);
        __Menu_SendMessage(memo->control.parent, MessageType_Select, (intptr_t)memo, 0);
    } else if (pressed == 2) {
        Menu_PlaySound(MENU_SFX_ALTSELECT_1);
        __Menu_SendMessage(memo->control.parent, MessageType_AltSelect1, (intptr_t)memo, 0);
    } else if (pressed == 3) {
        Menu_PlaySound(MENU_SFX_ALTSELECT_2);
        __Menu_SendMessage(memo->control.parent, MessageType_AltSelect2, (intptr_t)memo, 0);
    }
}
