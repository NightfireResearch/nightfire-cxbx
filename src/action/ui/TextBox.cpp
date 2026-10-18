#include "TextBox.h"

#include "Menu.h"
#include "../gfx/Font.h"

#include <math.h>
#include <string.h>

#pragma fp_contract(off)

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// The menu framework's, called by address
#define Component_GetHandle ((MENU_SKINPIECE *(__cdecl *)(void *component))0x00071db0)
#define Component_SetupInstance ((void (__cdecl *)(void *component, uint state, int x, int y, int width, int height))0x00071e00)
#define Component_Free ((void (__cdecl *)(void *component))0x000722e0)
#define Label_Init ((void (__cdecl *)(void *label, NEW_CONTROL *create, M_CONTROL *parent))0x0008fdd0)
#define Label_Update ((void (__cdecl *)(M_MANAGER *manager, M_PAGE *page, void *label, bool cursorOver))0x0008fff0)
#define Label_SendMessage ((int (__cdecl *)(void *label, int message, int arg1, int arg2))0x00090590)
#define Menu_ResizeControl ((void (__cdecl *)(void *control, ushort x, ushort y, ushort right, ushort height))0x00072c30)
#define Menu_CursorOverMe ((bool (__cdecl *)(M_MANAGER *manager, M_CONTROL *control))0x000723c0)
#define Menu_InputAction ((ushort (__cdecl *)(ushort player, int action, int flags))0x00072e20)
#define Script_ExitLoop ((bool (__cdecl *)(M_CONTROL *control))0x00096d50)
#define Script_Play_UISTUFF ((bool (__cdecl *)(M_CONTROL *control, int script, char flag))0x00096ba0)
#define Script_Select ((void *(__cdecl *)(M_CONTROL *control, int script))0x00096b60)
#define Script_DelAllScripts ((void (__cdecl *)(M_CONTROL *control))0x00097560)
#define Script_PrepareScripting ((void (__cdecl *)(M_CONTROL *control))0x00096960)
#define Menu_Free ((void (__cdecl *)(void **data, int flags))0x00072d60)

// The label's string record (its M_LABEL's +0xe8): the text and its length
typedef struct LABEL_TEXT {
    char unknown00[0x18];
    char *text;             // 0x18
    char unknown1c[4];
    ushort length;          // 0x20
} LABEL_TEXT;
#define TextOf(box) (*(LABEL_TEXT **)&(box)->label[0xe8])

// Frames a direction has been held (TextBox_Update): it repeats every 5th after 15
#define TextBoxHeldFrames U32_AT(0x0025fc1c)
// The format of the text and of the grid's measure: font 2
#define TextBoxFormat ((char *)0x0015eb20)
#define TextBoxAlphabet ((char *)0x0015eb24)

// The menu inputs a text box reads
#define MENU_UP     0x1a
#define MENU_DOWN   0x1b
#define MENU_LEFT   0x1c
#define MENU_RIGHT  0x1d
#define MENU_SELECT 0x1f

#define GRID_WIDTH  0xf8
#define GRID_HEIGHT 0x7e

// The next key of the grid
M_TEXTBOX* _Text_SetupChar(M_TEXTBOX *box, char c, int x, int y, int width, int height, uchar blocked) {
    box->selected++;
    TEXTBOX_KEY *key = &box->keys[box->selected];
    key->c = c;
    key->x = x;
    key->y = y;
    key->width = width;
    key->height = height;
    key->blocked = blocked;
    return box;
}

// EAX the box (kept), DL the character
// AUTOLTCG
void __declspec(naked) Text_SetupChar(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push edx
        push eax
        call _Text_SetupChar
        add esp, 28
        pop edx
        pop ecx
        ret
    }
}

// Leaves editing: the box closes to the text's line, and its page is told
static void TextBox_EndEntry(M_TEXTBOX *box) {
    box->control.drawState &= ~TEXTBOX_EDITING;
    box->control.drawState &= ~0x80u;
    box->control.drawState |= 0x40;
    box->control.height = (short)(Ftol(box->lineHeight) + 5);
    __Menu_SendMessage(box->control.parent, MessageType_TextEntryDone, (int)box, 0);
}

// AUTOINJECT
int Text_SendMessage(M_TEXTBOX *box, int message, int arg1, int arg2) {
    switch (message) {
        case MessageType_ScriptExitLoop:
            return (char)Script_ExitLoop(&box->control);
        case MessageType_ScriptPlay:
            return (char)Script_Play_UISTUFF(&box->control, arg1, (char)arg2);
        case MessageType_SetScript:
            if (arg1 < 0 || arg1 > 6)
                return 0;
            box->defaultScripts[arg1] = Script_Select(&box->control, arg2);
            return 1;
        case MessageType_SetState:
            box->control.state = (uchar)arg1;
            return 1;
        case MessageType_GetControl:
            return (int)box;
        case MessageType_GetSelectedItemText:
            return (int)TextOf(box)->text;
        case MessageType_GetId:
            return box->control.id;
        case MessageType_Destroy:
            Label_SendMessage(box->label, MessageType_Destroy, 0, 0);
            Component_Free(box);
            Component_Free(box->grid);
            Component_Free(box->frame);
            Component_Free(box->cursor);
            Script_DelAllScripts(&box->control);
            Menu_Free((void **)&box, 3);
            return 0;
        case MessageType_TextEndEntry:
            TextBox_EndEntry(box);
            return 1;
        default:
            return Label_SendMessage(box->label, message, arg1, arg2);
    }
}

// A move of the cursor by a key: at once on the first frame of a press, then, held, every 5th frame after the 15th
static void TextBox_Step(M_TEXTBOX *box, int *at, int step, bool room) {
    if (!(box->control.drawState & TEXTBOX_HELD)) {
        if (room)
            *at += step;
        box->control.drawState |= TEXTBOX_HELD;
        TextBoxHeldFrames = 0;
    } else if (TextBoxHeldFrames > 15 && room) {
        TextBoxHeldFrames++;
        if (TextBoxHeldFrames % 5 == 0)
            *at += step;
    } else {
        TextBoxHeldFrames++;
    }
}

// The box's frame: hidden with its page; focused, A starts editing (the label's own update), and while editing the
// d-pad moves the cursor over the grid and A types the key under it ('-' deletes, '+' ends); then the label and the
// skin's pieces are laid out
// AUTOINJECT
void TextBox_Update(M_MANAGER *manager, M_PAGE *page, M_TEXTBOX *box) {
    if ((box->control.parent->state & 5) || (box->control.state & CONTROL_STATE_HIDDEN)) {
        box->control.state |= 4;
        Component_Free(box);
        Component_Free(box->frame);
        Component_Free(box->grid);
        Component_Free(box->cursor);
        return;
    }
    box->control.state &= ~4;
    short right = box->control.width - Component_GetHandle(box)->unknown0a + box->control.x;
    Menu_ResizeControl(box->label, Component_GetHandle(box)->unknown06 + box->control.x, box->control.y, right,
                       box->control.height);

    if (page->focused->hashcode != box->control.hashcode) {
        if (box->control.drawState & TEXTBOX_EDITING) {
            TextBox_EndEntry(box);
            box->control.drawState |= 0x10;
        }
    } else {
        box->control.drawState |= 0x20;
        if (!(box->control.drawState & TEXTBOX_EDITING)) {
            box->control.drawState &= ~0x40u;
        } else {
            box->control.drawState = (box->control.drawState & ~0x40u) | 0x80;
            box->control.height = (short)((ushort)Component_GetHandle(box->frame)->unknown0c +
                                          Ftol(box->lineHeight) + 0x84);
            for (uchar i = 0; i < TEXTBOX_KEYS; i++) {
                TEXTBOX_KEY *key = &box->keys[i];
                if (box->cursorX >= key->x && box->cursorY >= key->y && box->cursorX <= key->x + key->width &&
                    box->cursorY <= key->y + key->height) {
                    box->selected = i;
                    break;
                }
            }

            ushort player = manager->whichPlayer;
            TEXTBOX_KEY *key = &box->keys[box->selected];
            if (Menu_InputAction(player, MENU_RIGHT, 1) && !(box->keys[box->selected].blocked & 8)) {
                TextBox_Step(box, &box->cursorX, key->width, box->cursorX < GRID_WIDTH - key->width);
            } else if (Menu_InputAction(player, MENU_LEFT, 1) && !(box->keys[box->selected].blocked & 4)) {
                TextBox_Step(box, &box->cursorX, -key->width, box->cursorX > key->width);
            } else if (Menu_InputAction(player, MENU_DOWN, 1) && !(box->keys[box->selected].blocked & 2)) {
                TextBox_Step(box, &box->cursorY, key->height, box->cursorY < GRID_HEIGHT - key->height);
            } else if (Menu_InputAction(player, MENU_UP, 1) && !(box->keys[box->selected].blocked & 1)) {
                TextBox_Step(box, &box->cursorY, -key->height, box->cursorY > key->height);
            } else {
                box->control.drawState &= ~TEXTBOX_HELD;
            }

            if (Menu_InputAction(player, MENU_SELECT, 4)) {
                char c = box->keys[box->selected].c;
                LABEL_TEXT *text = TextOf(box);
                if (c == '-') {
                    if (text->length != 0) {
                        text->length--;
                        TextOf(box)->text[TextOf(box)->length] = '\0';
                        __Menu_SendMessage(box->control.parent, MessageType_TextChanged, (int)box, 0);
                    }
                } else if (c == '+') {
                    TextBox_EndEntry(box);
                } else {
                    text->text[text->length] = c;
                    TextOf(box)->length++;
                    if (TextOf(box)->length >= TEXTBOX_MAX_TEXT)
                        TextOf(box)->length--;
                    else
                        __Menu_SendMessage(box->control.parent, MessageType_TextChanged, (int)box, 0);
                    TextOf(box)->text[TextOf(box)->length] = '\0';
                }
            }
        }
        GPOINT extent;
        Font_GetTextExtent(TextOf(box)->text, TextBoxFormat, &extent, NULL, -1);
    }

    Label_Update(manager, page, box->label, Menu_CursorOverMe(manager, &box->control));
    Component_SetupInstance(box, box->control.drawState, -1, -1, -1, -1);
    int lineHeight = Ftol(box->lineHeight);
    Component_SetupInstance(box->frame, box->control.drawState, Component_GetHandle(box->grid)->unknown06 + box->control.x,
                            lineHeight + box->control.y + 5, 0x100, 0x80);
    int height = 0x80 - Component_GetHandle(box->grid)->unknown0c;
    int width = 0xfa - Component_GetHandle(box->grid)->unknown0a;
    int top = Component_GetHandle(box->grid)->unknown08 + Ftol(box->lineHeight) + box->control.y + 4;
    Component_SetupInstance(box->grid, box->control.drawState, Component_GetHandle(box->grid)->unknown06 + box->control.x,
                            top, width, height);
    TEXTBOX_KEY *key = &box->keys[box->selected];
    int keyTop = key->y + Ftol(box->lineHeight) + box->control.y + 7;
    Component_SetupInstance(box->cursor, box->control.drawState,
                            Component_GetHandle(box->grid)->unknown06 + box->keys[box->selected].x + box->control.x,
                            keyTop, key->width, key->height);
}

// A text box from the menu data: the label (in font 2, one line high), the skin's pieces under it, and its 65 keys
// FUNC_AT(0x00099550)
void Text_Init(M_TEXTBOX *box, NEW_CONTROL *create, M_CONTROL *parent) {
    box->control.type = ControlType_Text;
    box->control.state = 0;
    box->control.skinPiece = ((uint)create->skin << 16) | 10;
    box->control.extraControlHash = (HASHCODE)0;
    box->control.parent = parent;
    box->control.hashcode = create->hashcode;
    box->control.unknown08 = 0;
    box->control.id = create->id;
    box->control.depth = create->depth;
    GPOINT extent;
    Font_GetTextExtent(TextBoxAlphabet, TextBoxFormat, &extent, NULL, -1);
    box->selected = 0xffff;
    box->lineHeight = extent.y + 2.0f;
    box->control.x = create->x;
    box->control.y = create->y;
    box->control.width = Component_GetHandle(box)->unknown06 + 0xfa;
    box->control.height = (short)(Ftol(box->lineHeight) + 5);

    // The label within the skin's insets, a depth nearer
    NEW_CONTROL label = *create;
    label.x += Component_GetHandle(box)->unknown06;
    MENU_SKINPIECE *piece = Component_GetHandle(box);
    label.width = (short)(Component_GetHandle(box)->unknown06 - piece->unknown0a + 0xfa);
    int lineHeight = Ftol(box->lineHeight);
    label.font = TextBoxFormat;
    label.unknown10 = 0;
    label.depth--;
    label.height = (short)(lineHeight + 5);
    Label_Init(box->label, &label, &box->control);
    memset(box->text, 0, sizeof(box->text));
    TextOf(box)->text = box->text;

    M_CONTROL *frame = (M_CONTROL *)box->frame;
    frame->depth = create->depth - 3;
    frame->skinPiece = ((uint)create->skin << 16) | 0xb;
    Component_SetupInstance(box->frame, 0, -1, -1, -1, -1);
    M_CONTROL *cursor = (M_CONTROL *)box->cursor;
    cursor->depth = create->depth - 2;
    cursor->skinPiece = ((uint)create->skin << 16) | 0xd;
    Component_SetupInstance(box->cursor, 0, -1, -1, -1, -1);
    M_CONTROL *grid = (M_CONTROL *)box->grid;
    grid->depth = create->depth - 1;
    grid->skinPiece = ((uint)create->skin << 16) | 0xc;
    Component_SetupInstance(box->grid, 0, -1, -1, -1, -1);

    // Ten keys to a row: the capitals, the small letters, the digits, then space, delete and done. The keys on the
    // grid's edges block the cursor's way out.
    static const struct {
        char c;
        uchar x, y, width, blocked;
    } kKeys[TEXTBOX_KEYS] = {
        {'A', 0, 0, 25, 0},       {'B', 25, 0, 25, 0},     {'C', 50, 0, 25, 0},     {'D', 75, 0, 25, 0},
        {'E', 100, 0, 25, 0},     {'F', 125, 0, 25, 0},    {'G', 150, 0, 25, 0},    {'H', 175, 0, 25, 0},
        {'I', 200, 0, 25, 0},     {'J', 225, 0, 25, 0},    {'K', 0, 16, 25, 2},     {'L', 25, 16, 25, 2},
        {'M', 50, 16, 25, 0},     {'N', 75, 16, 25, 0},    {'O', 100, 16, 25, 0},   {'P', 125, 16, 25, 0},
        {'Q', 150, 16, 25, 0},    {'R', 175, 16, 25, 0},   {'S', 200, 16, 25, 2},   {'T', 225, 16, 25, 2},
        {'U', 50, 32, 25, 4},     {'V', 75, 32, 25, 0},    {'W', 100, 32, 25, 0},   {'X', 125, 32, 25, 0},
        {'Y', 150, 32, 25, 0},    {'Z', 175, 32, 25, 8},   {'a', 0, 48, 25, 1},     {'b', 25, 48, 25, 1},
        {'c', 50, 48, 25, 0},     {'d', 75, 48, 25, 0},    {'e', 100, 48, 25, 0},   {'f', 125, 48, 25, 0},
        {'g', 150, 48, 25, 0},    {'h', 175, 48, 25, 0},   {'i', 200, 48, 25, 1},   {'j', 225, 48, 25, 1},
        {'k', 0, 64, 25, 2},      {'l', 25, 64, 25, 2},    {'m', 50, 64, 25, 0},    {'n', 75, 64, 25, 0},
        {'o', 100, 64, 25, 0},    {'p', 125, 64, 25, 0},   {'q', 150, 64, 25, 0},   {'r', 175, 64, 25, 0},
        {'s', 200, 64, 25, 2},    {'t', 225, 64, 25, 2},   {'u', 50, 80, 25, 4},    {'v', 75, 80, 25, 0},
        {'w', 100, 80, 25, 0},    {'x', 125, 80, 25, 0},   {'y', 150, 80, 25, 0},   {'z', 175, 80, 25, 8},
        {'0', 0, 96, 25, 3},      {'1', 25, 96, 25, 1},    {'2', 50, 96, 25, 0},    {'3', 75, 96, 25, 0},
        {'4', 100, 96, 25, 2},    {'5', 125, 96, 25, 0},   {'6', 150, 96, 25, 0},   {'7', 175, 96, 25, 0},
        {'8', 200, 96, 25, 1},    {'9', 225, 96, 25, 3},   {' ', 25, 112, 75, 12},  {'-', 125, 112, 50, 4},
        {'+', 175, 112, 50, 8},
    };
    for (int i = 0; i < TEXTBOX_KEYS; i++)
        _Text_SetupChar(box, kKeys[i].c, kKeys[i].x, kKeys[i].y, kKeys[i].width, 16, kKeys[i].blocked);
    box->control.drawState = 0;
    Script_PrepareScripting(&box->control);
}
