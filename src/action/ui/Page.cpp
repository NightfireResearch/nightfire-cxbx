// The menus' pages: the controls they hold, their frame, and the help line a page may show (PS2: menu_manager.cpp)

#include "Page.h"
#include "MenuCommon.h"

#include "../engine/Text.h"

// A help line: the text the help memo shows while a control has the focus (with a value of its, or any)
#pragma pack(push, 1)
typedef struct HELP_TEXT {
    HASHCODE control;           // 0x00
    int value;                  // 0x04 MessageType_GetSelectedItemValue's, or HELP_ANY_VALUE
    Action_TranslatedText text; // 0x08
} HELP_TEXT;
#pragma pack(pop)
static_assert(sizeof(HELP_TEXT) == 0xc, "HELP_TEXT is 0xc bytes");

#define HELP_TEXTS 130
// XBE_GLOBAL(0x0017d640, 0x618)
HELP_TEXT help_texts[HELP_TEXTS] = {
    {.control = (HASHCODE)0x10000002, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000000},
    {.control = (HASHCODE)0x10000003, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000001},
    {.control = (HASHCODE)0x10000004, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000002},
    {.control = (HASHCODE)0x10000008, .text = (Action_TranslatedText)0x100002f},
    {.control = (HASHCODE)0x100000f6, .text = (Action_TranslatedText)0x100002f},
    {.control = (HASHCODE)0x10000081, .value = 1, .text = (Action_TranslatedText)0x1000021},
    {.control = (HASHCODE)0x10000081, .text = (Action_TranslatedText)0x1000022},
    {.control = (HASHCODE)0x10000084, .text = (Action_TranslatedText)0x1000023},
    {.control = (HASHCODE)0x10000084, .value = 1, .text = (Action_TranslatedText)0x1000024},
    {.control = (HASHCODE)0x10000084, .value = 2, .text = (Action_TranslatedText)0x1000025},
    {.control = (HASHCODE)0x10000084, .value = 3, .text = (Action_TranslatedText)0x1000026},
    {.control = (HASHCODE)0x10000084, .value = 4, .text = (Action_TranslatedText)0x1000027},
    {.control = (HASHCODE)0x10000084, .value = 5, .text = (Action_TranslatedText)0x1000028},
    {.control = (HASHCODE)0x10000084, .value = 6, .text = (Action_TranslatedText)0x1000200},
    {.control = (HASHCODE)0x10000084, .value = 7, .text = (Action_TranslatedText)0x1000201},
    {.control = (HASHCODE)0x10000084, .value = 8, .text = (Action_TranslatedText)0x1000202},
    {.control = (HASHCODE)0x10000084, .value = 9, .text = (Action_TranslatedText)0x1000203},
    {.control = (HASHCODE)0x10000084, .value = 10, .text = (Action_TranslatedText)0x1000327},
    {.control = (HASHCODE)0x10000080, .value = 1, .text = (Action_TranslatedText)0x100002a},
    {.control = (HASHCODE)0x10000080, .text = (Action_TranslatedText)0x1000029},
    {.control = (HASHCODE)0x1000008c, .value = 1, .text = (Action_TranslatedText)0x100002c},
    {.control = (HASHCODE)0x1000008c, .text = (Action_TranslatedText)0x100002b},
    {.control = (HASHCODE)0x1000008d, .value = 1, .text = (Action_TranslatedText)0x100002d},
    {.control = (HASHCODE)0x1000008d, .text = (Action_TranslatedText)0x100002e},
    {.control = (HASHCODE)0x100000a2, .text = (Action_TranslatedText)0x100002f},
    {.control = (HASHCODE)0x100000a2, .value = 1, .text = (Action_TranslatedText)0x1000030},
    {.control = (HASHCODE)0x100000a2, .value = 2, .text = (Action_TranslatedText)0x1000031},
    {.control = (HASHCODE)0x1000009f, .value = 1, .text = (Action_TranslatedText)0x1000033},
    {.control = (HASHCODE)0x1000009f, .text = (Action_TranslatedText)0x1000032},
    {.control = (HASHCODE)0x1000019b, .text = (Action_TranslatedText)0x1000034},
    {.control = (HASHCODE)0x1000019b, .value = 1, .text = (Action_TranslatedText)0x1000035},
    {.control = (HASHCODE)0x1000019b, .value = 16, .text = (Action_TranslatedText)0x1000295},
    {.control = (HASHCODE)0x10000227, .value = 1, .text = (Action_TranslatedText)0x1000037},
    {.control = (HASHCODE)0x10000227, .text = (Action_TranslatedText)0x1000036},
    {.control = (HASHCODE)0x10000228, .text = (Action_TranslatedText)0x10001dd},
    {.control = (HASHCODE)0x10000228, .value = 1, .text = (Action_TranslatedText)0x1000325},
    {.control = (HASHCODE)0x10000228, .value = 2, .text = (Action_TranslatedText)0x1000326},
    {.control = (HASHCODE)0x10000228, .value = 3, .text = (Action_TranslatedText)0x10001dc},
    {.control = (HASHCODE)0x10000008, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x100030d},
    {.control = (HASHCODE)0x100000f6, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x100030e},
    {.control = (HASHCODE)0x10000116, .value = 1, .text = (Action_TranslatedText)0x1000079},
    {.control = (HASHCODE)0x10000116, .text = (Action_TranslatedText)0x100007a},
    {.control = (HASHCODE)0x10000115, .value = 1, .text = (Action_TranslatedText)0x100007b},
    {.control = (HASHCODE)0x10000115, .text = (Action_TranslatedText)0x100007c},
    {.control = (HASHCODE)0x1000018c, .text = (Action_TranslatedText)0x100007d},
    {.control = (HASHCODE)0x1000018c, .value = 1, .text = (Action_TranslatedText)0x100007e},
    {.control = (HASHCODE)0x1000018c, .value = 2, .text = (Action_TranslatedText)0x100007f},
    {.control = (HASHCODE)0x10000195, .text = (Action_TranslatedText)0x10002da},
    {.control = (HASHCODE)0x10000195, .value = 4, .text = (Action_TranslatedText)0x1000083},
    {.control = (HASHCODE)0x10000195, .value = 1, .text = (Action_TranslatedText)0x1000080},
    {.control = (HASHCODE)0x10000195, .value = 2, .text = (Action_TranslatedText)0x1000081},
    {.control = (HASHCODE)0x10000195, .value = 3, .text = (Action_TranslatedText)0x1000082},
    {.control = (HASHCODE)0x10000195, .value = 5, .text = (Action_TranslatedText)0x1000084},
    {.control = (HASHCODE)0x10000195, .value = 6, .text = (Action_TranslatedText)0x1000085},
    {.control = (HASHCODE)0x10000195, .value = 7, .text = (Action_TranslatedText)0x1000087},
    {.control = (HASHCODE)0x10000195, .value = 8, .text = (Action_TranslatedText)0x1000086},
    {.control = (HASHCODE)0x10000185, .value = 2, .text = (Action_TranslatedText)0x1000088},
    {.control = (HASHCODE)0x10000185, .value = 3, .text = (Action_TranslatedText)0x1000089},
    {.control = (HASHCODE)0x10000185, .value = 4, .text = (Action_TranslatedText)0x100008a},
    {.control = (HASHCODE)0x10000186, .value = 8, .text = (Action_TranslatedText)0x100008b},
    {.control = (HASHCODE)0x10000186, .value = 5, .text = (Action_TranslatedText)0x100008c},
    {.control = (HASHCODE)0x10000186, .value = 3, .text = (Action_TranslatedText)0x100008d},
    {.control = (HASHCODE)0x10000186, .value = 1, .text = (Action_TranslatedText)0x100008e},
    {.control = (HASHCODE)0x10000187, .value = 50, .text = (Action_TranslatedText)0x100008f},
    {.control = (HASHCODE)0x10000187, .value = 75, .text = (Action_TranslatedText)0x1000090},
    {.control = (HASHCODE)0x10000187, .value = 100, .text = (Action_TranslatedText)0x1000091},
    {.control = (HASHCODE)0x10000187, .value = 125, .text = (Action_TranslatedText)0x1000092},
    {.control = (HASHCODE)0x10000187, .value = 150, .text = (Action_TranslatedText)0x1000093},
    {.control = (HASHCODE)0x10000187, .value = 175, .text = (Action_TranslatedText)0x1000094},
    {.control = (HASHCODE)0x10000187, .value = 200, .text = (Action_TranslatedText)0x1000095},
    {.control = (HASHCODE)0x10000187, .value = 250, .text = (Action_TranslatedText)0x1000328},
    {.control = (HASHCODE)0x10000187, .value = 300, .text = (Action_TranslatedText)0x1000329},
    {.control = (HASHCODE)0x10000188, .value = 50, .text = (Action_TranslatedText)0x1000096},
    {.control = (HASHCODE)0x10000188, .value = 75, .text = (Action_TranslatedText)0x1000097},
    {.control = (HASHCODE)0x10000188, .value = 100, .text = (Action_TranslatedText)0x1000098},
    {.control = (HASHCODE)0x10000188, .value = 125, .text = (Action_TranslatedText)0x1000099},
    {.control = (HASHCODE)0x10000188, .value = 150, .text = (Action_TranslatedText)0x100009a},
    {.control = (HASHCODE)0x10000188, .value = 175, .text = (Action_TranslatedText)0x100009b},
    {.control = (HASHCODE)0x10000188, .value = 200, .text = (Action_TranslatedText)0x100009c},
    {.control = (HASHCODE)0x1000018a, .value = 50, .text = (Action_TranslatedText)0x100009d},
    {.control = (HASHCODE)0x1000018a, .value = 75, .text = (Action_TranslatedText)0x100009e},
    {.control = (HASHCODE)0x1000018a, .value = 100, .text = (Action_TranslatedText)0x100009f},
    {.control = (HASHCODE)0x1000018a, .value = 125, .text = (Action_TranslatedText)0x10000a0},
    {.control = (HASHCODE)0x1000018a, .value = 150, .text = (Action_TranslatedText)0x10000a1},
    {.control = (HASHCODE)0x1000018a, .value = 175, .text = (Action_TranslatedText)0x10000a2},
    {.control = (HASHCODE)0x1000018a, .value = 200, .text = (Action_TranslatedText)0x10000a3},
    {.control = (HASHCODE)0x1000011a, .value = 1, .text = (Action_TranslatedText)0x100030f},
    {.control = (HASHCODE)0x1000011a, .text = (Action_TranslatedText)16778000},
    {.control = (HASHCODE)0x1000011b, .value = 1, .text = (Action_TranslatedText)0x1000057},
    {.control = (HASHCODE)0x1000011b, .text = (Action_TranslatedText)0x1000058},
    {.control = (HASHCODE)0x10000124, .value = 1, .text = (Action_TranslatedText)0x1000059},
    {.control = (HASHCODE)0x10000124, .text = (Action_TranslatedText)0x100005a},
    {.control = (HASHCODE)0x10000125, .text = (Action_TranslatedText)0x100005b},
    {.control = (HASHCODE)0x10000125, .value = 1, .text = (Action_TranslatedText)0x100005c},
    {.control = (HASHCODE)0x10000126, .text = (Action_TranslatedText)0x100005d},
    {.control = (HASHCODE)0x10000126, .value = 1, .text = (Action_TranslatedText)0x100005e},
    {.control = (HASHCODE)0x100001cb, .value = 1, .text = (Action_TranslatedText)0x1000061},
    {.control = (HASHCODE)0x100001cb, .text = (Action_TranslatedText)0x1000062},
    {.control = (HASHCODE)0x10000226, .value = 2, .text = (Action_TranslatedText)0x1000302},
    {.control = (HASHCODE)0x10000226, .text = (Action_TranslatedText)0x1000303},
    {.control = (HASHCODE)0x1000023b, .text = (Action_TranslatedText)0x1000304},
    {.control = (HASHCODE)0x1000023b, .value = 1, .text = (Action_TranslatedText)0x1000305},
    {.control = (HASHCODE)0x10000128, .value = 1, .text = (Action_TranslatedText)0x100005f},
    {.control = (HASHCODE)0x10000128, .text = (Action_TranslatedText)0x1000060},
    {.control = (HASHCODE)0x1000011c, .value = -75, .text = (Action_TranslatedText)0x100006a},
    {.control = (HASHCODE)0x1000011c, .value = -50, .text = (Action_TranslatedText)0x1000069},
    {.control = (HASHCODE)0x1000011c, .value = -25, .text = (Action_TranslatedText)0x1000068},
    {.control = (HASHCODE)0x1000011c, .text = (Action_TranslatedText)0x1000063},
    {.control = (HASHCODE)0x1000011c, .value = 25, .text = (Action_TranslatedText)0x1000064},
    {.control = (HASHCODE)0x1000011c, .value = 50, .text = (Action_TranslatedText)0x1000065},
    {.control = (HASHCODE)0x1000011c, .value = 75, .text = (Action_TranslatedText)0x1000066},
    {.control = (HASHCODE)0x1000011c, .value = 100, .text = (Action_TranslatedText)0x1000067},
    {.control = (HASHCODE)0x10000129, .value = 1, .text = (Action_TranslatedText)0x100006b},
    {.control = (HASHCODE)0x10000129, .text = (Action_TranslatedText)0x100006c},
    {.control = (HASHCODE)0x10000135, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000306},
    {.control = (HASHCODE)0x10000134, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000307},
    {.control = (HASHCODE)0x10000136, .value = 1, .text = (Action_TranslatedText)0x1000071},
    {.control = (HASHCODE)0x10000136, .text = (Action_TranslatedText)0x1000072},
    {.control = (HASHCODE)0x10000198, .text = (Action_TranslatedText)0x100006d},
    {.control = (HASHCODE)0x10000198, .value = 1, .text = (Action_TranslatedText)0x100006e},
    {.control = (HASHCODE)0x10000198, .value = 2, .text = (Action_TranslatedText)0x100030b},
    {.control = (HASHCODE)0x10000198, .value = 4, .text = (Action_TranslatedText)0x100030b},
    {.control = (HASHCODE)0x10000223, .value = 1, .text = (Action_TranslatedText)0x1000309},
    {.control = (HASHCODE)0x10000223, .text = (Action_TranslatedText)0x100030a},
    {.control = (HASHCODE)0x10000137, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x100030c},
    {.control = (HASHCODE)0x1000019a, .text = (Action_TranslatedText)0x100006f},
    {.control = (HASHCODE)0x1000019a, .value = 1, .text = (Action_TranslatedText)0x1000070},
    {.control = (HASHCODE)0x100001a3, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000073},
    {.control = (HASHCODE)0x10000138, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000074},
    {.control = (HASHCODE)0x10000222, .value = -0xf0f0f10, .text = (Action_TranslatedText)0x1000308},
};
#define HELP_ANY_VALUE ((int)0xf0f0f0f0)

// The memo of the page shown that the help lines go to
// XBE_GLOBAL(0x0025fc18, 0x4)
M_CONTROL *help_control;
#define HELP_MEMO ((HASHCODE)0x10000225)

// AUTOINJECT
void Page_Init(M_PAGE *page, HASHCODE hashcode, M_MANAGER *mgr) {
    page->control.hashcode = hashcode;
    page->control.type = ControlType_Page;
    page->control.depth = 0;
    page->control.id = 0;
    page->control.parent = &mgr->control;
    page->control.x = 0;
    page->control.y = 0;
    page->control.width = SCREEN_WIDTH;
    page->control.height = SCREEN_HEIGHT;
    page->control.state = CONTROL_STATE_SHOWN;
    page->focused = NULL;
    page->navigationMode = 1;
    page->backgroundMovie = (HASHCODE)0;
    page->framesShown = 0;
    page->inputDelay = 0;
    for (int i = 0; i < 4; i++)
        page->playerFocus[i] = NULL;
    LinkedList_Init(&page->controls, 0);
    Script_PrepareScripting(&page->control);
}

// AUTOINJECT
intptr_t Page_SendMessage(M_PAGE *page, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_CreateObject: {
            M_CONTROL *control = (M_CONTROL *)arg1;
            LinkedList_Add(&page->controls, &control->prev, control);
            return 1;
        }
        // A control's selection goes to the page's handler first; then, as for the rest, the control plays its default
        // script, and (unless arg2 says it has been) the control's handler hears of it
        case MessageType_Select:
        case MessageType_AltSelect1:
        case MessageType_AltSelect2:
        case MessageType_BackOnControl:
            if (arg2 == 0)
                __Menu_SendMessage(page->control.parent, message, (intptr_t)page, arg1);
            // fall through
        case MessageType_Scroll:
        case MessageType_GainFocus:
        case MessageType_LoseFocus:
        case MessageType_ControlCreated:
            Script_PlayDefault((M_CONTROL *)arg1, message);
            // fall through
        case MessageType_TextChanged:
        case MessageType_MemoBack:
        case MessageType_TextEndEntry:
        case MessageType_ValueSet:
        case MessageType_SelectRepeat:
            if (arg2 == 0)
                return __Menu_SendMessage(page->control.parent, message, arg1, 0);
            return 0;
        case MessageType_Destroy:
            if (page->controls.count != 0) {
                M_CONTROL *control = (M_CONTROL *)LinkedList_First(&page->controls);
                while (control != NULL) {
                    M_CONTROL *next = control->next;
                    __Menu_SendMessage(control, MessageType_Destroy, 0, 0);
                    control = next;
                }
            }
            Menu_Free((void **)&page, 4);
            return 1;
        case MessageType_IsCursorOverFocused:
            return (char)Menu_CursorOverMe((M_MANAGER *)page->control.parent, page->focused);
        case MessageType_SetInputDelay:
            page->inputDelay = arg1;
            return 0;
    }
    return 0;
}

// A frame of the page: the focus follows the cursor, then each control from the last runs its script and (unless
// hidden) its update - as the player whose focus it has, if any, and without input until the page's input delay has
// passed. The cursor goes to the focused control, the page's extra control is updated as if focused, and the page's
// handler gets its frame.
// AUTOINJECT
void Page_Update(M_MANAGER *mgr, M_PAGE *page, int takingInput) {
    if (page == NULL || mgr->cursorSprite == NULL)
        return;
    mgr->takingInput = takingInput;
    page->framesShown++;
    M_CONTROL *under = Menu_GetControl(mgr);
    M_CONTROL *was = page->focused;
    if (under != was) {
        page->focused = under;
        under->framesFocused = 0;
        if (mgr->takingInput) {
            Menu_AlphaReset();
            __Menu_SendMessage(page->control.parent, MessageType_LoseFocus, (intptr_t)was, 0);
            __Menu_SendMessage(page->control.parent, MessageType_GainFocus, (intptr_t)under, 0);
            if (Menu_GetPage(was) == Menu_GetPage(under))
                Menu_PlaySound(MENU_SFX_FOCUS);
        }
    }
    if (page->framesShown == 1) {
        if (mgr->overlayMode)
            help_control = NULL;
        else
            Page_FindHelpControl(page);
    }
    Page_SetHelpText(page);

    for (M_CONTROL *c = (M_CONTROL *)LinkedList_Last(&page->controls); c != NULL && mgr->exists; c = c->prev) {
        if (c->hashcode == page->focused->hashcode)
            c->framesFocused = page->focused->framesFocused;
        Script_RunFrame(mgr, c);
        if (c->state & CONTROL_STATE_HIDDEN)
            continue;
        M_CONTROL *focused = page->focused;
        bool input = mgr->takingInput;
        short player = mgr->whichPlayer;
        for (uchar i = 0; i < 4; i++) {
            if (page->playerFocus[i] == c && page == mgr->currentPage) {
                mgr->whichPlayer = i;
                mgr->cursorSprite->positionX = c->x + 1;
                mgr->cursorSprite->positionY = c->y + 1;
                mgr->cursorSprite->backupOnscreenHeight = 1;
                mgr->cursorSprite->backupOnscreenWidth = 1;
                page->focused = c;
                break;
            }
        }
        c->framesFocused++;
        if ((uint)page->framesShown < (uint)page->inputDelay)
            mgr->takingInput = false;
        Menu_Update(mgr, page, c);
        mgr->whichPlayer = player;
        page->focused = focused;
        mgr->takingInput = input;
    }

    if (page->focused != NULL && mgr->cursorSprite != NULL && mgr->currentPage == page) {
        sprite *cursor = mgr->cursorSprite;
        cursor->positionX = (page->focused->width >> 1) + mgr->currentPage->control.x + page->focused->x;
        cursor->positionY = (page->focused->height >> 1) + page->focused->y + mgr->currentPage->control.y;
        cursor->onscreenWidth = page->focused->width;
        cursor->onscreenHeight = page->focused->height;
        cursor->depth = (uchar)page->focused->depth + 1;
    }
    if (page->control.extraControlHash != 0) {
        M_CONTROL *focused = page->focused;
        page->focused = (M_CONTROL *)__Menu_Send(mgr->index, page->control.extraControlHash, MessageType_GetControl, 0, 0);
        if (page->focused != NULL)
            Menu_Update(mgr, page, page->focused);
        page->focused = focused;
    }
    if (mgr->overlayMode && mgr->takingInput) {
        if (mgr->underPage != NULL)
            __Menu_SendMessage(mgr->underPage->control.parent, MessageType_PageUpdate, (intptr_t)page, 0);
    } else if (mgr->currentPage == page && mgr->takingInput) {
        __Menu_SendMessage(page->control.parent, MessageType_PageUpdate, (intptr_t)page, 0);
    }
}

// The page's help memo, if it has one
// AUTOINJECT
void Page_FindHelpControl(M_PAGE *page) {
    if (page == NULL) {
        help_control = NULL;
        return;
    }
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
        if (c->type == ControlType_Memo && c->hashcode == HELP_MEMO) {
            help_control = c;
            return;
        }
    }
    help_control = NULL;
}

// The help memo shows the focused control's help line, or nothing
// AUTOINJECT
void Page_SetHelpText(M_PAGE *page) {
    if (help_control == NULL)
        return;
    M_CONTROL *focused = page->focused;
    if (focused == NULL)
        return;
    int value = __Menu_SendMessage(focused, MessageType_GetSelectedItemValue, 0, 0);
    for (int i = 0; i < HELP_TEXTS; i++) {
        if (help_texts[i].control == focused->hashcode &&
            (help_texts[i].value == value || help_texts[i].value == HELP_ANY_VALUE)) {
            __Menu_SendMessage(help_control, MessageType_SetText, (intptr_t)Txt_BindLabel(help_texts[i].text, 0), 0);
            return;
        }
    }
    __Menu_SendMessage(help_control, MessageType_SetText, 0, 0);
}
