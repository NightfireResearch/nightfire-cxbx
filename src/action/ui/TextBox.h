#ifndef TEXTBOX_H
#define TEXTBOX_H

#include "ui.h"

// The menus' text box (ControlType_Text): a label showing what has been typed, and an on-screen grid of characters
// the cursor moves over and picks from. PS2: text.cpp's second Text_Init.

#pragma pack(push, 1)
// What MenuManager_Create hands a control's initialiser, from the menu data
typedef struct NEW_CONTROL {
    HASHCODE hashcode;
    char *font;             // 0x04 a format
    int unknown08;
    int id;                 // 0x0c
    int unknown10;
    ushort skin;            // 0x14 its skin's index
    ushort depth;           // 0x16
    short x;                // 0x18
    short y;
    short width;
    short height;
    int unknown20;
} NEW_CONTROL;
static_assert(sizeof(NEW_CONTROL) == 0x24, "NEW_CONTROL is 0x24 bytes");

// One key of the grid, in pixels from the grid's corner
typedef struct TEXTBOX_KEY {
    int x;
    int y;
    int width;
    int height;
    char c;                 // 0x10 '-' deletes, '+' ends the entry
    uchar blocked;          // 0x11 the sides the cursor cannot leave by: 1 up, 2 down, 4 left, 8 right
    char pad[2];
} TEXTBOX_KEY;
static_assert(sizeof(TEXTBOX_KEY) == 0x14, "TEXTBOX_KEY is 0x14 bytes");

#define TEXTBOX_KEYS 65
#define TEXTBOX_MAX_TEXT 0x13   // the typed text, its terminator with it

typedef struct M_TEXTBOX {
    M_CONTROL control;
    M_SCRIPT *defaultScripts[6]; // 0x7c SetScript's slots (a seventh would be the label's first word)
    char label[0x114];          // 0x94 an M_LABEL, which shows the typed text
    M_BASECONTROL frame;        // 0x1a8 skin components: the grid's frame,
    M_BASECONTROL cursor;       // 0x23c the cursor's box
    M_BASECONTROL grid;         // 0x2d0 and the grid
    char unknown364[0x478 - 0x364];
    TEXTBOX_KEY keys[TEXTBOX_KEYS]; // 0x478
    char text[TEXTBOX_MAX_TEXT];    // 0x98c the label's string
    char pad99f;
    int cursorX;                // 0x9a0 in the grid
    int cursorY;
    float lineHeight;           // 0x9a8 the font's, and 2
    ushort selected;            // 0x9ac the key under the cursor (Text_SetupChar: the last key set up)
    char pad9ae[2];
} M_TEXTBOX;
static_assert(sizeof(M_TEXTBOX) == 0x9b0, "M_TEXTBOX is 0x9b0 bytes");
#pragma pack(pop)

// M_CONTROL::drawState bits of the text box
#define TEXTBOX_EDITING     0x80000000u
#define TEXTBOX_HELD        0x40000000u // a direction has been held since the last frame

M_TEXTBOX* _Text_SetupChar(M_TEXTBOX *box, char c, int x, int y, int width, int height, uchar blocked);
void Text_SetupChar(void);
int Text_SendMessage(M_TEXTBOX *box, int message, int arg1, int arg2);
void TextBox_Update(M_MANAGER *mgr, M_PAGE *page, M_TEXTBOX *box);
void Text_Init(M_TEXTBOX *box, NEW_CONTROL *create, M_CONTROL *parent);

#endif // TEXTBOX_H
