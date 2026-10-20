#ifndef UI_CONTROLS_H
#define UI_CONTROLS_H

// The layouts of the menus' control types (docs/ui/messages.md)

#include "ui.h"
#include "../gfx/Sprite.h"

#pragma pack(push, 1)

// A label: a text, or an icon (MessageType_SetIcon), drawn by a sprite Label_Update makes from a model
typedef struct M_LABEL {
    M_CONTROL control;          // type ControlType_Label
    M_SCRIPT *defaultScripts[6]; // 0x7c
    char format[0x10];          // 0x94 MessageType_SetFont's: the font and a scale
    sprite model;               // 0xa4 what Label_Update copies into the sprite, its links aside
    sprite *drawn;              // 0xe8 the sprite drawn
    uint iconFlags;             // 0xec the model's flags for an icon (MessageType_SetIconSpriteFlags)
    Action_TranslatedText textLabel; // 0xf0 the text's label, bound again every frame; -1 for a string
    char *text;                 // 0xf4
    HASHCODE icon;              // 0xf8
    uint highlightColour;       // 0xfc
    uint normalColour;          // 0x100
    uint pulseColour;           // 0x104
    uint mode;                  // 0x108 LABEL_*
    short faded;                // 0x10c it has faded to the normal colour since it was last highlighted
    uchar textFlag2000;         // 0x10e the model's flag 0x2000
    uchar pulse;                // 0x10f
    uchar alwaysHighlighted;    // 0x110 highlighted whenever its page's focused control has its hashcode
    char pad_111[3];
} M_LABEL;
static_assert(sizeof(M_LABEL) == 0x114, "M_LABEL is 0x114 bytes");
static_assert(offsetof(M_LABEL, drawn) == 0xe8, "M_LABEL sprite offset incorrect");

#define LABEL_OWN_COLOURS   1   // the highlight and normal colours are its own, else the model's tint
#define LABEL_SHOWS_TEXT          2   // a text, else an icon

typedef struct M_BUTTON {
    M_CONTROL control;          // type ControlType_Button
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_LABEL label;              // 0x94
    short unknown1a8;           // 0x1a8 NEW_CONTROL::value
    uchar unknown1aa;           // 0x1aa
    char pad_1ab;
} M_BUTTON;
static_assert(sizeof(M_BUTTON) == 0x1ac, "M_BUTTON is 0x1ac bytes");

typedef struct M_CHECKBOX {
    M_CONTROL control;          // type ControlType_Checkbox
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_LABEL label;              // 0x94 beside the box
    uchar checked;              // 0x1a8
    char pad_1a9[3];
} M_CHECKBOX;
static_assert(sizeof(M_CHECKBOX) == 0x1ac, "M_CHECKBOX is 0x1ac bytes");

// A value between min and max: a bar with a thumb, moved with the d-pad while the scroll has the focus
typedef struct M_SCROLL {
    M_CONTROL control;          // type ControlType_Scroll
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_BASECONTROL thumb;        // 0x94 its skin's component 8
    int min;                    // 0x128
    int max;                    // 0x12c
    int value;                  // 0x130
    int range;                  // 0x134 max - min + 1
    uint heldFrames;            // 0x138 a direction has been held this long
    ushort flags;               // 0x13c SCROLL_*
    short startInset;           // 0x13e from the bar's start to the thumb's track: the skin's inset and the arrow
    short endInset;             // 0x140
    ushort pressed;             // 0x142 the direction held: 0x40 down, 0x80 up (also its draw state)
    uchar wrap;                 // 0x144 the value wraps around from max to min (MessageType_ScrollNoWrap clears it)
    uchar proportionalBar;      // 0x145 the thumb fills the track up to the value
    char pad_146[2];
} M_SCROLL;
static_assert(sizeof(M_SCROLL) == 0x148, "M_SCROLL is 0x148 bytes");

#define SCROLL_NO_BAR   1       // not drawn (MessageType_ScrollHideBar); the value still moves
#define SCROLL_VERTICAL 2       // taller than wide: up and down move it

#define LIST_CELL_TEXT 0x80
#define LIST_COLUMNS 5

// A cell of a list row
typedef struct LIST_CELL {
    char text[LIST_CELL_TEXT];  // 0x00
    HASHCODE icon;              // 0x80
    ushort u;                   // 0x84
    ushort v;
    ushort uw;
    ushort vh;
} LIST_CELL;
static_assert(sizeof(LIST_CELL) == 0x8c, "LIST_CELL is 0x8c bytes");

// A row of a list, or an item of a radio (its first cell only)
typedef struct LIST_ROW {
    LIST_ROW *next;             // 0x00
    LIST_ROW *prev;             // 0x04
    LIST_CELL cells[LIST_COLUMNS]; // 0x08
    int value;                  // 0x2c4 MessageType_GetSelectedItemValue
    uchar continued;            // 0x2c8 the rest of the row before's text, wrapped
    char pad_2c9[3];
    sprite *stripe;             // 0x2cc its background (MessageType_ListSetSelectionColour)
} LIST_ROW;
static_assert(sizeof(LIST_ROW) == 0x2d0, "LIST_ROW is 0x2d0 bytes");

#define LIST_VISIBLE_ROWS 32    // the rows with sprites

typedef struct M_LIST {
    M_CONTROL control;          // type ControlType_List
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_BASECONTROL highlight;    // 0x94 the selected row's bar, its skin's component 4
    M_SCROLL scroll;            // 0x128 at its right; its value is the selected row
    LINKEDLIST rows;            // 0x270 LIST_ROW
    sprite *cells[LIST_VISIBLE_ROWS][LIST_COLUMNS]; // 0x280 the visible rows'
    int visibleRows;            // 0x500
    int numColumns;             // 0x504
    int columnWidth[LIST_COLUMNS]; // 0x508 in hundredths of the list's width
    int columnAlign[LIST_COLUMNS]; // 0x51c as a format's: 2 right, 3 centred
    float rowHeight;            // 0x530
    int topRow;                 // 0x534
    uint highlightColour;       // 0x538
    uint normalColour;          // 0x53c
    uint pulseColour;           // 0x540 the selected row's
    uint alpha;                 // 0x544
    uint stripeColour;          // 0x548 every other row's background; none when 0
    char *format;               // 0x54c
    char selectedFormat[0x10];  // 0x550
    LIST_ROW *selected;         // 0x560
    ushort highlightMode;       // 0x564 LIST_HIGHLIGHT_*
    char pad_566;
    uchar textFlag2000;         // 0x567 the cells' sprite flag 0x2000
    uchar pulse;                // 0x568
    uchar wrap;                 // 0x569 a text too wide carries on in a row of its own
    uchar continuedRows;        // 0x56a the rows added by wrapping
    uchar selectedIndex;        // 0x56b
} M_LIST;
static_assert(sizeof(M_LIST) == 0x56c, "M_LIST is 0x56c bytes");
static_assert(offsetof(M_LIST, rows) == 0x270, "M_LIST rows offset incorrect");
static_assert(offsetof(M_LIST, selected) == 0x560, "M_LIST selected offset incorrect");

#define LIST_HIGHLIGHT_BAR    1 // the bar and the pulse colour
#define LIST_HIGHLIGHT_COLOUR 2 // the pulse colour only

// A row of options picked with left and right
typedef struct M_RADIO {
    M_CONTROL control;          // type ControlType_Radio
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_LABEL label;              // 0x94 the selected item's text
    M_SCROLL scroll;            // 0x1a8 its value is the selected item's index
    LINKEDLIST items;           // 0x2f0 LIST_ROW
    char pad_300[4];
    LIST_ROW *selected;         // 0x304
} M_RADIO;
static_assert(sizeof(M_RADIO) == 0x308, "M_RADIO is 0x308 bytes");

// A list that opens under a label (Combo_*): its size (MessageType_CreateObject) and its parts' places
// (Process_RunFrame)
typedef struct M_COMBO {
    M_CONTROL control;          // type ControlType_Combo
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_LIST list;                // 0x94 open under the label while drawState has COMBO_OPEN
    M_LABEL label;              // 0x600 given the text the list answers GetSelectedItemText with: none
    float labelHeight;          // 0x714 a line of the plain format and 2
    int visibleRows;            // 0x718 MessageType_GetVisibleRows
} M_COMBO;
static_assert(sizeof(M_COMBO) == 0x71c, "M_COMBO is 0x71c bytes");

#define COMBO_OPEN 0x80000000 // drawState: the list is open


// A value picked with left and right and shown as a number (Spin_*)
typedef struct M_SPIN {
    M_CONTROL control;          // type ControlType_Spin
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_SCROLL scroll;            // 0x94 a thin one at its right; the value shown is max + min - its value
    M_LABEL label;              // 0x1dc the value
    char text[0x20];            // 0x2f0 the label's text
    float labelHeight;          // 0x310 a line of the plain format and 2
    char pad_314[4];
} M_SPIN;
static_assert(sizeof(M_SPIN) == 0x318, "M_SPIN is 0x318 bytes");

#define MEMO_LINES 32           // the lines with sprites

// A text wrapped onto lines, scrolled by a bar at its right
typedef struct M_MEMO {
    M_CONTROL control;          // type ControlType_Memo
    M_SCRIPT *defaultScripts[6]; // 0x7c
    M_SCROLL scroll;            // 0x94 its value is the first line shown
    char *text;                 // 0x1dc
    char *format;               // 0x1e0
    sprite *lines[MEMO_LINES];  // 0x1e4
    int visibleLines;           // 0x264
    int numLines;               // 0x268 the text's
    float lineHeight;           // 0x26c
    int topLine;                // 0x270
    uint highlightColour;       // 0x274
    uint normalColour;          // 0x278
    uint alpha;                 // 0x27c
    uchar centred;              // 0x280 1: each line centred (MessageType_MemoSetOption280)
    uchar linesShown;           // 0x281 MessageType_MemoGetNumLines
    uchar centredVertically;    // 0x282 MessageType_MemoSetOption282
    char pad_283;
} M_MEMO;
static_assert(sizeof(M_MEMO) == 0x284, "M_MEMO is 0x284 bytes");
static_assert(offsetof(M_MEMO, visibleLines) == 0x264, "M_MEMO visibleLines offset incorrect");

#pragma pack(pop)

#endif // UI_CONTROLS_H
