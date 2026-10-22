#ifndef UI_H
#define UI_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

#include "../util/Stack.h"
#include "LinkedList.h"

// Messages between the menu system's controls, pages, the manager and the handlers (docs/ui/messages.md).
// Every name is invented - the game has none. A handler gets (managerNum, control, hashcode, message, a1, a2);
// for most, a1 is the control the message is about. Messages a control does not handle bubble up to its page,
// then the manager, then Handler_HandleMessage; a handler returning false passes a message on to the page.
typedef enum {
    MessageType_TextEntryDone = 0x00,        // sent to the textbox's parent page, which drops it (not in Page_SendMessage's switch) - effectively dead. Args: a1 = textbox.
    MessageType_ListAddColumn = 0x0e,        // List. Args: a1 = width in % of the list, a2 = alignment (as 0x59). Returns: 1, or 0 if 5 columns exist.
    MessageType_ListAddRow = 0x0f,           // List. Args: a1 = row (5 cells of 0x8c: text or sprite), a2 = item value. Returns: 1.
    MessageType_AddItem = 0x10,              // List, Radio (Combo not). Args: a1 = char* text, a2 = item value (returned by 0x35). Returns: List: new item index; Radio: 1.
    MessageType_AddItemLabel = 0x11,         // List, Radio. Args: a1 = text label id (Txt_BindLabel), a2 = item value. Returns: as 0x10.
    MessageType_ScrollAddToMax = 0x12,       // Scroll. Args: a1 = delta added to max. Returns: 0.
    MessageType_ListRemoveItem = 0x13,       // List. Args: a1 = index (probable). Returns: 1 if removed.
    MessageType_ScriptExitLoop = 0x14,       // Button, Checkbox, Combo, Label, List, Radio, Scroll, Spin, Text, Window. Returns: Script_ExitLoop result.
    MessageType_ScrollHideBar = 0x15,        // Scroll. Args: - (sets +0x13c bit0: bar not drawn, input still works). Returns: 1.
    MessageType_ScriptPlay = 0x16,           // Button, Checkbox, Label, List, Memo, Radio, Scroll, Spin, Text, Window. Args: a1 = script hashcode, a2 = flag. Returns: Script_Play_UISTUFF result.
    MessageType_ClearItems = 0x17,           // List, Radio. Returns: List 1, Radio 0.
    MessageType_SetText = 0x18,              // Label; Button/Checkbox/Radio/Spin/Text pass it to their label; Memo. Args: a1 = char* (or 0); if a1 == 0, a2 = text label id; both 0 = blank. Retur...
    MessageType_ListSetCellText = 0x19,      // List. Args: a1 = row<<16 | column, a2 = char* (word-wraps into extra rows). Returns: 1.
    MessageType_SetColour = 0x1a,            // Label (+ owners). Args: a1 = colour; a2 = 0: fixed colour (auto highlight off, normal = highlight = a1); a2 != 0: tint only. Returns: 1.
    MessageType_SetHighlightColour = 0x1b,   // Label, List, Memo. Args: a1 = colour used when focused / cursor over. Returns: 1.
    MessageType_SetNormalColour = 0x1c,      // Label, List, Memo. Args: a1 = colour when not focused (faded to with process 0xe1). Returns: 1.
    MessageType_SelectIndex = 0x1d,          // List, Radio. Args: a1 = index. Returns: List: result of the 0x49 sent to the parent; Radio: same.
    MessageType_SelectItemByValue = 0x1e,    // List, Radio. Args: a1 = item value, a2 = 1: silent (no 0x49 to the parent). Returns: 1 / result of 0x49.
    MessageType_ListSetCurrentItemValue = 0x1f, // List. Args: a1 = value. Returns: 1.
    MessageType_SetId = 0x20,                // Button, Scroll: control +0x20; Radio: value of the last item. Args: a1 = id / value. Returns: 1.
    MessageType_SetScript = 0x21,            // all except Page, Manager, Combo. Args: a1 = slot 0..6, a2 = script hashcode (slot table at +0x7c). Returns: 1.
    MessageType_SetCursor = 0x22,            // Manager. Args: a1 = control hashcode, or a1 = 0 and a2 = M_CONTROL*. Returns: 1 if found.
    MessageType_SetFont = 0x23,              // Label (sprintf name+suffix), List (a1 = 1/2/3 font id), Memo (font name). Args: a1 = font name or id. Returns: 1.
    MessageType_SetIcon = 0x24,              // Label (+ owners). Args: a1 = sprite hashcode (switches the label to icon mode). Returns: 1.
    MessageType_ScrollSetMax = 0x25,         // Scroll, Spin. Args: a1 = max. Returns: 1.
    MessageType_ScrollSetMin = 0x26,         // Scroll, Spin. Args: a1 = min. Returns: 1.
    MessageType_SetRange = 0x27,             // Scroll, Spin. Args: a1 = min, a2 = max. Returns: 1.
    MessageType_ListSetColumnWidth = 0x28,   // List. Args: a1 = column, a2 = width % (max 100). Returns: 1.
    MessageType_SetPulseColour = 0x29,       // Label (+0x104), List (+0x540). Args: a1 = colour used when 0x70 pulsing is on. Returns: 1.
    MessageType_ListSetOption564 = 0x2a,     // List. Args: a1 = byte stored in +0x564 (unknown). Returns: 0.
    MessageType_SetState = 0x2b,             // Label, Button, Checkbox, Text, Scroll, Spin, Radio, List, Memo (control +0x7a). Args: a1 = 0 visible, 1 hidden, 2 shown but not selectable. Returns...
    MessageType_ListSetTopRow = 0x2c,        // List. Args: a1 = first visible row. Returns: 1.
    MessageType_SetIconRect = 0x2d,          // Label. Args: a1 = x<<16|y, a2 = w<<16|h in the sprite sheet. Returns: 1.
    MessageType_SetValue = 0x2e,             // Scroll (clamped, then 0x54 to parent), Spin (inverted), Checkbox (bool). Args: a1 = value. Returns: 1 / 0 out of range.
    MessageType_GetText = 0x2f,              // Label, Button, Memo. Returns: char*.
    MessageType_GetColour = 0x30,            // Label. Returns: current tint.
    MessageType_GetHighlightColour = 0x31,   // Label, List, Memo. Returns: colour.
    MessageType_GetNormalColour = 0x32,      // Label, List, Memo. Returns: colour.
    MessageType_GetNumItems = 0x33,          // List (count), Radio (count - 1), Combo (its list). Returns: count.
    MessageType_GetSelectedIndex = 0x34,     // List, Radio. Returns: index.
    MessageType_GetSelectedItemValue = 0x35, // List, Radio. Returns: value given to 0x10/0x11 (0 if nothing selected).
    MessageType_GetMax = 0x38,               // Scroll, Spin. Returns: max.
    MessageType_GetControl = 0x39,           // every control type except Page and Manager. Returns: M_CONTROL* (self).
    MessageType_GetMin = 0x3a,               // Scroll, Spin. Returns: min.
    MessageType_GetSelectedItemText = 0x3b,  // Radio (item text), Text (edit buffer). Returns: char*.
    MessageType_ListGetTopRow = 0x3c,        // List. Returns: row.
    MessageType_GetId = 0x3d,                // Button, Checkbox, Label, List, Radio, Scroll, Spin, Text. Returns: control +0x20.
    MessageType_GetIconSize = 0x3e,          // Label. Returns: w<<16|h.
    MessageType_GetIconPos = 0x3f,           // Label. Returns: x<<16|y.
    MessageType_GetValue = 0x40,             // Scroll (clamped), Spin, Checkbox, Radio (index). Returns: value.
    MessageType_GetVisibleRows = 0x41,       // List (+0x500); Combo returns +0x718. Returns: rows.
    MessageType_CreateObject = 0x42,         // Manager: allocate + init by type (NEW_CONTROL +0x21); Page: add child. Args: a1 = NEW_CONTROL* (to Manager) / M_CONTROL* (to Page). Returns: new co...
    MessageType_CreatePage = 0x43,           // Manager (Page_Init, list add, GoPage flags 0x19). Args: a1 = page hashcode. Returns: M_PAGE*.
    MessageType_GoPage = 0x44,               // Manager. Args: a1 = page hashcode, a2 = flags (see below). Returns: 1.
    MessageType_SetSidePage = 0x45,          // Manager (+0x1c4). Args: a1 = page hashcode or 0; sends 0x4d/0x4c to the old/new page. Returns: 1.
    MessageType_Destroy = 0x47,              // all. Returns: 0.
    MessageType_IsCursorOverFocused = 0x48,  // Page. Returns: bool.
    MessageType_Scroll = 0x49,               // List/Radio/Spin/Combo resync and forward; Page -> handlers (C_*); default script slot 3. Args: a1 = control; a2 = 0, or 1 when Radio re-announces a...
    MessageType_TextChanged = 0x4a,          // Page -> handlers. Args: a1 = textbox.
    MessageType_Select = 0x4b,               // Page -> P_ handler (a1 = page, a2 = control) then C_ handler (a1 = control); default script slot 2. Args: a1 = control. Returns: handler result.
    MessageType_PageEnter = 0x4c,            // handlers (P_*); page window script slot 0. Args: a1 = page, a2 = previous page hashcode.
    MessageType_PageLeave = 0x4d,            // handlers; page window script slot 1. Args: a1 = page, a2 = GoPage flags (or 0).
    MessageType_GainFocus = 0x4e,            // handlers; default script slot 1. Args: a1 = new control.
    MessageType_LoseFocus = 0x4f,            // handlers; default script slot 4. Args: a1 = old control.
    MessageType_PageUpdate = 0x50,           // handlers (iris animation, message boxes). Args: a1 = page.
    MessageType_ControlCreated = 0x51,       // handlers (init range/value); default script slot 0. Args: a1 = control.
    MessageType_MemoBack = 0x52,             // List/Radio/Spin treat it like 0x49; Page -> handlers. Args: a1 = control.
    MessageType_TextEndEntry = 0x53,         // Text (leave edit mode, sends 0x00); Page forwards. Returns: 1.
    MessageType_ValueSet = 0x54,             // List/Radio resync (Radio re-sends 0x49 a2=1); Page -> handlers (wheels redraw without animation). Args: a1 = control.
    MessageType_SetPageOption = 0x56,        // Manager. Args: a1 = option (only 1 = navigation mode, page +0xc0), a2 = value. Returns: 1.
    MessageType_SetAlpha = 0x58,             // Label (sprite alpha, +0x1c |= 4), List (+0x544), Memo (+0x27c). Args: a1 = alpha (0..0x80). Returns: 1.
    MessageType_ListSetColumnAlign = 0x59,   // List (+0x51c[col]). Args: a1 = column, a2 = alignment. Returns: 1.
    MessageType_SetPlayerFocus = 0x5a,       // Manager: page +0xb0[player] = control; 0x60 to the old, 0x5c to the new. Args: a1 = control hashcode, a2 = player 0..3. Returns: 1 if found.
    MessageType_PlayerFocusGained = 0x5c,    // handlers (C_RBMP*). Args: a1 = control.
    MessageType_AltSelect1 = 0x5d,           // Radio/List forward; Page -> P_ then C_ handler. Args: a1 = control.
    MessageType_AltSelect2 = 0x5e,           // as 0x5d. Args: a1 = control.
    MessageType_Back = 0x5f,                 // Manager: 0x6b veto query, then pop the page stack (GoPage flags 5|8), or 0x63 when empty. Args: a1 = 0 normal / 1 forced (no 0x6b), a2 = player. Re...
    MessageType_PlayerFocusLost = 0x60,      // handlers. Args: a1 = control.
    MessageType_SelectRepeat = 0x61,         // Page -> handlers (C_KEYBOARD). Args: a1 = control.
    MessageType_SetTextFlag2000 = 0x62,      // Label (+0x10e -> text sprite flag 0x2000), List (+0x567). Args: a1 = bool. Returns: 1.
    MessageType_BackAtRoot = 0x63,           // handlers (P_MPSCENARIO, P_NFMAP go to P_MAIN); page window script slot 2. Args: a1 = page, a2 = page hashcode.
    MessageType_MemoSetOption280 = 0x64,     // Memo (+0x280 byte). Args: a1 = byte. Returns: 1.
    MessageType_MemoGetNumLines = 0x65,      // Memo (+0x281). Returns: line count.
    MessageType_MemoSetOption282 = 0x66,     // Memo (+0x282 byte). Args: a1 = byte. Returns: 0.
    MessageType_LockInput = 0x68,            // Manager (+0x1d4; clears all actions). Args: a1 = 1 lock / 0 unlock. Returns: 1.
    MessageType_ScrollNoWrap = 0x69,         // Scroll (+0x144 wrap = 0, +0x145 proportional bar = 1). Returns: 1.
    MessageType_SaveFlowDone = 0x6a,         // handlers (P_CNMENU). Args: a1 = page, a2 = result.
    MessageType_QueryBack = 0x6b,            // handlers. Args: a1 = control/page, a2 = int*: write 0xfffffffe to keep the page.
    MessageType_SetIconSpriteFlags = 0x6c,   // Label (+0xec). Args: a1 = sprite flags. Returns: 1.
    MessageType_BackOnControl = 0x6d,        // List forwards; Page -> P_ then C_ handler (C_LB*OPTIONS). Args: a1 = control.
    MessageType_PageResume = 0x6e,           // handlers (P_MPBOTS...). Args: a1 = page, a2 = previous page hashcode.
    MessageType_SetInputDelay = 0x6f,        // Page (+0xcc). Args: a1 = frames after entering before input is taken. Returns: 0.
    MessageType_SetPulse = 0x70,             // Label (+0x10f), List (+0x568). Args: a1 = bool. Returns: 0.
    MessageType_ListSetSelectionColour = 0x73, // List (+0x548). Args: a1 = the colour of every other row's stripe, 0 none. Returns: 0.
    MessageType_GetCurrentPage = 0x74,       // Manager. Returns: hashcode of the current page (or of the page under an overlay).
    MessageType_SetLineSpacing = 0x75,       // List, Memo. Args: a1 = row height in % of the font height (visible rows = row height / control height). Returns: 0.
    MessageType_SetAlwaysHighlighted = 0x76, // Label (+0x110). Args: a1 = bool. Returns: 0.
} MessageType;

typedef enum {
    ControlType_Button = 1,     // Button_*; label at +0x94; handler prefix GO
    ControlType_Checkbox = 2,   // prefix CH
    ControlType_Combo = 3,      // label + list
    ControlType_Label = 5,      // text or an icon; also the iris picture
    ControlType_List = 6,       // prefix LB; a Scroll at +0x128
    ControlType_Manager = 7,    // M_MANAGER
    ControlType_Page = 8,       // M_PAGE
    ControlType_Radio = 9,      // a left/right option picker; prefix RB. Label +0x94, Scroll +0x1a8
    ControlType_Scroll = 10,    // a value between min and max; the iris wheels are Scrolls (prefix SB)
    ControlType_Spin = 11,      // label + scroll, the value inverted
    ControlType_Text = 12,      // the on-screen keyboard's character grid
    ControlType_Window = 13,    // a page's background and script holder
    ControlType_Memo = 200,     // multi-line text with a scroll at +0x94
    ControlType_72 = 0x72,      // no such control is created; Menu_GetPage accepts it
} ControlType;

// MessageType_SetState's argument (M_CONTROL.state)
typedef enum {
    CONTROL_STATE_SHOWN = 0,
    CONTROL_STATE_HIDDEN = 1,
    CONTROL_STATE_INERT = 2,    // shown, but the cursor skips it
    CONTROL_STATE_UNDRAWN = 4,  // its update found it or its owner hidden, and freed its sprites
} ControlState;

// Radios (a row of options picked with left and right, e.g. the multiplayer settings)
#define RADIO_ADD_ITEM(manager, radio, label, value) __Menu_Send(manager, radio, MessageType_AddItem, (intptr_t)Txt_BindLabel(label, 0), value)
#define RADIO_CLEAR(manager, radio) __Menu_Send(manager, radio, MessageType_ClearItems, 0, 0);
#define RADIO_SELECT_ITEM(manager, radio, value) __Menu_Send(manager, radio, MessageType_SelectItemByValue, value, 0);
#define RADIO_GET_VALUE(manager, radio) __Menu_Send(manager, radio, MessageType_GetSelectedItemValue, 0, 0)
// An item's enabled flag as the game reads and writes it: its first byte.
#define ITEM_ENABLED(item) (*(uchar *)&(item).enabled)
#define RADIO_GET_VALUE_OF(radio) __Menu_SendMessage(radio, MessageType_GetSelectedItemValue, 0, 0)

// Scrolls (the iris wheels among them)
#define SCROLL_GET_VALUE(control) __Menu_SendMessage(control, MessageType_GetValue, 0, 0)

// Label
#define LABEL_SET_TEXT(manager, label, text) __Menu_Send(manager, label, MessageType_SetText, (intptr_t)text, 0)
#define LABEL_SET_COLOUR(manager, label, colour) __Menu_Send(manager, label, MessageType_SetColour, colour, 0)

// Controls
#define CONTROL_GET(manager, control) (M_CONTROL*)__Menu_Send(manager, control, MessageType_GetControl, 0, 0)

#pragma pack(push, 1)

typedef struct M_SCRIPT M_SCRIPT;
typedef struct M_KEYFRAME M_KEYFRAME;

// The part every control, page and the manager share (docs/ui/messages.md). Each type extends it: ordinary
// controls continue with their script slots (M_WIDGET), the page and the manager with their own fields.
typedef struct M_CONTROL {
    M_CONTROL *next;            // 0x00 in the page's control list
    M_CONTROL *prev;            // 0x04
    sprite **sprites;           // 0x08 its skin's pieces (Component_SetupInstance), numSprites of them
    M_CONTROL *parent;          // 0x0c the page for a control, the manager for a page; messages bubble up to it
    HASHCODE extraControlHash;  // 0x10 page only: a control Page_Update also updates
    uint skin;                  // 0x14 its skin: the set's id << 16 | the skin's index in the set (Component_GetHandle)
    HASHCODE hashcode;          // 0x18 the C_/P_ hashcode Handler_HandleMessage dispatches on
    uint drawState;             // 0x1c 0x10 normal, 0x20 focused, 0x40 pressed/checked, 0x80 under the cursor
    int id;                     // 0x20 SetId/GetId: a key's character code, a wheel's item; __Menu_SendEx picks by it
    uint framesFocused;         // 0x24
    LINKEDLIST scripts;         // 0x28 M_SCRIPT
    M_SCRIPT *script;           // 0x38 the script selected (Script_Select, Script_Play)
    M_KEYFRAME *key;            // 0x3c the keyframe being played to; NULL when no script plays
    M_KEYFRAME *loopKey;        // 0x40 where a keyframe flagged KEYFRAME_LOOP_BACK goes back to
    int loopStep;               // 0x44 the step there
    char pad_48[4];
    int step;                   // 0x4c the keyframes passed
    intptr_t lastScriptResult;  // 0x50 the last keyframe message's result (an argument of -4 passes it on)
    int frame;                  // 0x54 the script's frame
    uint scriptExecutionFlags;  // 0x58 0x10 once a loop has gone back; Script_ExitLoop clears 8
    LINKEDLIST processes;       // 0x5c running processes (colour fades)
    int numSprites;             // 0x6c
    short x;                    // 0x70 relative to the page
    short y;                    // 0x72
    short width;                // 0x74
    short height;               // 0x76
    ushort depth;               // 0x78 draw and hit order
    uchar state;                // 0x7a SetState: bit 0 hidden, bit 1 not selectable
    uchar type;                 // 0x7b ControlType
} M_CONTROL;
LAYOUT_32(sizeof(M_CONTROL) == 0x7c, "M_CONTROL is 0x7c bytes");
LAYOUT_32(offsetof(M_CONTROL, hashcode) == 0x18, "M_CONTROL hashcode offset incorrect");
LAYOUT_32(offsetof(M_CONTROL, x) == 0x70, "M_CONTROL x offset incorrect");
LAYOUT_32(offsetof(M_CONTROL, type) == 0x7b, "M_CONTROL type offset incorrect");

// An ordinary control (every type but the page and the manager): the shared part, then the default scripts.
typedef struct M_WIDGET {
    M_CONTROL control;
    M_SCRIPT *defaultScripts[7]; // 0x7c SetScript slots: 0 created/entered, 1 focus gained, 2 selected, 3 scrolled,
                                //      4 focus lost, 5 B pressed (Script_PlayDefault)
} M_WIDGET;
LAYOUT_32(sizeof(M_WIDGET) == 0x98, "M_WIDGET is 0x98 bytes");

// The 0x94 bytes every control type starts with (M_WIDGET's seventh slot is not every type's): the text box's skin
// parts, Menu_GetFirstControl's stand-in
typedef struct M_BASECONTROL {
    M_CONTROL control;
    M_SCRIPT *defaultScripts[6]; // 0x7c
} M_BASECONTROL;
LAYOUT_32(sizeof(M_BASECONTROL) == 0x94, "M_BASECONTROL is 0x94 bytes");

// A page's background and script holder
typedef struct M_WINDOW {
    M_CONTROL control;
    M_SCRIPT *defaultScripts[6]; // 0x7c
    struct M_PAGE *page;        // 0x94
    char pad_98[4];
} M_WINDOW;
LAYOUT_32(sizeof(M_WINDOW) == 0x9c, "M_WINDOW is 0x9c bytes");

// What MenuManager_Create hands a control's initialiser from the menu data, and what a control hands the parts it is
// made of. The words at 0x04 and 0x08 are the type's own.
typedef struct NEW_CONTROL {
    HASHCODE hashcode;          // 0x00
    union {
        char *font;             // 0x04 a label's format
        int min;                // a scroll's range
        int value;              // a checkbox's state, a list's highlight mode, a button's unknown1a8
    };
    union {
        char *text;             // 0x08 a label's text
        int max;
    };
    int id;                     // 0x0c
    int keepState;              // 0x10 NEW_CONTROL_LAYOUT: Scroll_Init only lays the scroll out again
    ushort skin;                // 0x14 the skin set (M_CONTROL::skin's high word); the type picks the skin in it
    ushort depth;               // 0x16
    short x;                    // 0x18
    short y;
    short width;
    short height;
    uchar hasBar;               // 0x20 a scroll shows its bar (MenuManager_Create: a scroll of the menu data's own)
    uchar type;                 // 0x21 ControlType (MessageType_CreateObject)
    char pad_22[2];
} NEW_CONTROL;
LAYOUT_32(sizeof(NEW_CONTROL) == 0x24, "NEW_CONTROL is 0x24 bytes");

#define NEW_CONTROL_LAYOUT 0xff


typedef struct M_PAGE {
    M_CONTROL control;          // type ControlType_Page
    char pad_7c[0x94 - 0x7c];
    LINKEDLIST controls;        // 0x94 M_CONTROL
    void *pageLink[2];          // 0xa4 in the manager's page list
    M_CONTROL *focused;         // 0xac the control under the cursor
    M_CONTROL *playerFocus[4];  // 0xb0 SetPlayerFocus; while one is set the d-pad does not move the cursor
    int navigationMode;         // 0xc0 1 four-way, 2 up/down, 3 left/right
    HASHCODE backgroundMovie;   // 0xc4 0 stops the movie
    int framesShown;            // 0xc8
    int inputDelay;             // 0xcc SetInputDelay: no input until framesShown reaches it
} M_PAGE;
LAYOUT_32(sizeof(M_PAGE) == 0xd0, "M_PAGE is 0xd0 bytes");

// A menu instance (the front end, the pause menu...): manager[managerNum].
typedef struct M_MANAGER {
    M_CONTROL control;          // type ControlType_Manager
    char pad_7c[0x94 - 0x7c];
    sprite *cursorSprite;       // 0x94 its position picks the focused control
    LINKEDLIST pages;           // 0x98 M_PAGE, linked through pageLink
    STACKINFO stack;            // 0xa8 page history for Back: {M_PAGE*, focused control} entries
    int stackMem[64];           // 0xb0
    HASHCODE currentMovie;      // 0x1b0 the background movie playing
    uint status;                // 0x1b4 MenuManager_GetStatus; above 3, START selects too
    HASHCODE startPage;         // 0x1b8
    M_PAGE *currentPage;        // 0x1bc
    M_PAGE *underPage;          // 0x1c0 the page left showing under an overlay (GoPage flag 2)
    M_PAGE *sidePage;           // 0x1c4 SetSidePage
    uint closeAction;           // 0x1c8 GameActions_tag that deletes this manager
    short whichPlayer;          // 0x1cc -1 = any controller
    uchar startPlayer;          // 0x1ce
    uchar field_0x1cf;          // 0x1cf 7 once created
    uchar index;                // 0x1d0 the manager number, every handler's first argument
    uchar exists;               // 0x1d1
    uchar takingInput;          // 0x1d2
    uchar overlayMode;          // 0x1d3 GoPage flag 2; Back then adds flag 8
    uchar inputLocked;          // 0x1d4 LockInput
    uchar didPauseAudio;        // 0x1d5
    char pad_1d6[2];
} M_MANAGER;
LAYOUT_32(sizeof(M_MANAGER) == 0x1d8, "M_MANAGER is 0x1d8 bytes");
LAYOUT_32(offsetof(M_MANAGER, stack) == 0xa8, "M_MANAGER stack offset incorrect");
LAYOUT_32(offsetof(M_MANAGER, currentPage) == 0x1bc, "M_MANAGER currentPage offset incorrect");

// A message a keyframe sends as it passes (Script_RunFrame), or a process (Process_Create; type 0xe1 is a colour fade)
typedef struct M_MESSAGE {
    M_MESSAGE *next;            // 0x00
    M_MESSAGE *prev;            // 0x04
    uint target;                // 0x08 the hashcode of the controls it goes to, or MESSAGE_TO_*
    uint type;                  // 0x0c a MessageType; from MESSAGE_PROCESS on, a process
    int arg1;                   // 0x10 MESSAGE_LAST_RESULT: the control's lastScriptResult
    int arg2;                   // 0x14
} M_MESSAGE;
LAYOUT_32(sizeof(M_MESSAGE) == 0x18, "M_MESSAGE is 0x18 bytes");

#define MESSAGE_TO_MANAGER  0xfffffffdu
#define MESSAGE_TO_SELF     0xfffffffeu
#define MESSAGE_LAST_RESULT -4
#define MESSAGE_PROCESS     0xe1

// A control's process (Process_Create): a message that runs a number of frames and then sends the control its colour
typedef struct M_PROCESS {
    M_PROCESS *next;            // 0x00
    M_PROCESS *prev;            // 0x04
    uint frame;                 // 0x08 the frames it has run
    uint type;                  // 0x0c its message's: MESSAGE_PROCESS fades the colour each frame
    uint colour;                // 0x10 the colour it ends on
    int tint;                   // 0x14 above 0, the colour is sent as a tint (MessageType_SetColour's second argument)
    uint from;                  // 0x18 the control's colour when it began (MessageType_GetColour)
    uint unknown1c;             // 0x1c
    uint frames;                // 0x20 how long it runs
} M_PROCESS;
LAYOUT_32(sizeof(M_PROCESS) == 0x24, "M_PROCESS is 0x24 bytes");

// A keyframe of a control's script: where the control is at that frame, and the messages it sends then
typedef struct M_KEYFRAME {
    M_KEYFRAME *next;           // 0x00
    M_KEYFRAME *prev;           // 0x04
    LINKEDLIST messages;        // 0x08 M_MESSAGE
    uint frame;                 // 0x18
    uint flags;                 // 0x1c KEYFRAME_*
    short x;                    // 0x20
    short y;
    short width;
    short height;
} M_KEYFRAME;
LAYOUT_32(sizeof(M_KEYFRAME) == 0x28, "M_KEYFRAME is 0x28 bytes");

#define KEYFRAME_LOOP_START 2       // a later KEYFRAME_LOOP_BACK comes back here
#define KEYFRAME_LOOP_BACK  4
#define KEYFRAME_FIXED      0x20    // Script_AddKeyFrame does not change it

// A control's keyframe script (the menu data's 0xfffffff7 records)
typedef struct M_SCRIPT {
    M_SCRIPT *next;             // 0x00
    M_SCRIPT *prev;             // 0x04
    LINKEDLIST keys;            // 0x08 M_KEYFRAME, by frame
    int id;                     // 0x18
    uint flags;                 // 0x1c SCRIPT_*
} M_SCRIPT;
LAYOUT_32(sizeof(M_SCRIPT) == 0x20, "M_SCRIPT is 0x20 bytes");

#define SCRIPT_SPLINE   1       // interpolated by spline, else by line
#define SCRIPT_LOOPING  8       // a KEYFRAME_LOOP_START has been passed


// Common between PS2 and Xbox
typedef struct M_ITEM {
    HASHCODE iconHashcode;
    Action_TranslatedText title;
    Action_TranslatedText description;
    uint identifier; // Identifier or index
    uint enabled; // 4-byte bool? Upper 3 bits seem unused
    Action_TranslatedText descriptionWhenDisabled;
} M_ITEM;

#pragma pack(pop)

// The menus' item lists (docs/ui/items.md). Ours: every function that reads or writes them is reimplemented
// (MenuUnlocks.cpp and the handlers), and nothing original refers to the game's copies any more - which
// settings.ini MenuCheckLists compares with these at start.
extern M_ITEM sp_level[12];               // ui_nightfire.cpp, the game's at 0x17c580
extern M_ITEM difficulty[3];              // ui_nightfire.cpp, the game's at 0x17c760
extern M_ITEM mp_level[8];                // ui_mp.cpp, the game's at 0x17c6a0
extern M_ITEM mp_scenario[13];            // ui_mp.cpp, the game's at 0x17c7a8
extern M_ITEM mp_characters[29];          // ui_mp.cpp, the game's at 0x17c8e0
extern M_ITEM mp_characters_small[29];    // ui_mp.cpp, the game's at 0x17cb98
extern M_ITEM mp_options[5];              // ui_mp.cpp, the game's at 0x17ce50
extern M_ITEM mp_bots[17];                // ui_mp.cpp, the game's at 0x17cfd0
extern M_ITEM cn_options[7];              // ui_codenames.cpp, the game's at 0x17cec8
extern const M_ITEM ds_options[4];        // ui_dossier.cpp, the game's at 0x17cf70
extern M_ITEM ds_weapons[27];             // ui_dossier.cpp, the game's at 0x17d168
extern M_ITEM ds_gadgets[14];             // ui_dossier.cpp, the game's at 0x17d3f0

bool Handler_HandleMessage(uchar param_1, M_CONTROL *param_2, uint param_3, intptr_t param_4, intptr_t param_5);

// In general, a handler seems to have either C_ or P_ prefix (PAGE and CONTROL?)
// They all take (uchar, M_CONTROL*, uint, uint, int, int) as parameters
// First indicates the manager number (ie the index into manager array)
// Second is the pointer to the M_CONTROL struct
// Third is the hashcode representing some resource (eg a menu item, page)
// Fourth: ??
// Fifth: ??

bool C_SBDOSSIER_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint param_4, intptr_t param_5, intptr_t param_6);
bool P_DOSSIER_Handler(uchar param_1, M_CONTROL* param_2, uint param_3, uint param_4, intptr_t param_5, intptr_t param_6);
bool P_MPMAP_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint event, intptr_t param_5, intptr_t param_6);
bool C_SBMPMAP_Handler(uchar param_1,M_CONTROL *param_2,uint param_3,uint param_4,intptr_t param_5,intptr_t param_6);
bool P_MPSCENARIO_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, intptr_t param_5, intptr_t param_6);
bool P_MPPLAYERMODS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, intptr_t param_5, intptr_t param_6);
bool P_MPENVIROMODS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, intptr_t param_5, intptr_t param_6);
bool P_DSGADGETS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint messageType, intptr_t param_5, intptr_t param_6);
bool C_SBDSGTSCROLL_Handler(uchar param_1, M_CONTROL *param_2, uint control, uint eventType, intptr_t param_5, intptr_t param_6);
bool P_DSWEAPONS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint param_4, intptr_t param_5, intptr_t param_6);
bool C_SBDSWPSCROLL_Handler(uchar param_1,M_CONTROL *param_2,uint param_3,uint param_4,intptr_t param_5,intptr_t param_6);
bool P_DSREWARDS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint eventType, intptr_t param_5, intptr_t param_6);
bool P_DSRECORDS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint eventType, intptr_t param_5, intptr_t param_6);

// ui_nightfire, ui_mp, ui_codenames
bool P_NFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBNFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_NFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBNFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPBOTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBBOTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_CNMENU_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBCNOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPBOTCHOOSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBMPBTCHOOSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPBOTSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBMPSCEN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBMPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_RBMPSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPJOIN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_RBMPSTART_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_RBMPCNAME_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_RBMPFINISH_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPRULES_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPDEBRIEFING_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MPCONFIRM_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_MPDBG_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_ATTRACT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHMUSIC_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHDRAWALL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHWS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHFLY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHDRONES_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHBLIND_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHWEAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHHEALTH_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHHUD_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHDEBUG_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHCOORDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHALLOWFREEZE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHDUMMY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHZEROG_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHLOCKUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHBRIGHT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHUNLOCK_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_CHCHCONTROLS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_codenames: the Codenames page, and the save flow's error list
bool P_CNSELECT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBCNSELECT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_LBERROPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_keypad
bool C_KEYPAD_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_credits
typedef struct CreditsEntry {
    const char *txt_left;
    const char *txt_right;  // unused (and the right-hand control hidden) on a centred line
    uchar modifiers_left;   // text style: 0 names, 1 companies and section headings, 2 role titles
    uchar modifiers_right;
    uchar centred;          // one line across the middle, the left text only
    uchar pad;
} CreditsEntry;
LAYOUT_32(sizeof(CreditsEntry) == 0xc, "CreditsEntry is 12 bytes");
void __stdcall Menu_InitCredits(void);
CreditsEntry* Menu_SetupCredits(uint *numLines_out);
bool P_CREDITS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_secrets
uint64_t Menu_UpgradeCheat(uint64_t bonus, uint objId, byte count);
uint64_t Menu_SpecialCodenameCheck(byte *code);
bool P_CNNAME_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_KEYBOARD_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_frontend: the boot pages, the main menu and the movie pages
bool P_LANGUAGE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_LANGUAGE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_INTRO_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_START_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_PARISENUM_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_ESTHERO_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_MAIN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_GONIGHTFIRE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_GOMULTIPLAYER_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_GOCODENAMES_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_FMVTEST_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_FMVPLAYER_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_TRAILER_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_WINGAME_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_tweaks: the debug page's tools
bool C_LBPMMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_TWEAKS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_TWEAKS2_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_NIS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_NIS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_CHEATMEDAL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// Menu.cpp: the option box's answers
bool C_LBMSGOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_nightfire: the codename select before the campaign, the results after a mission
bool P_NFSELECT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_SBNFCN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_NFRESULTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_NFBONUS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_NFSTATS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_dossier: the records and rewards rows
bool C_RBDSRECORDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_RBDSREWARDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_options: the codename's options pages and the controller styles
bool P_CNCONTROLS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_RBCONTROL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_CNOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_CNMPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_CNAVOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_pause: the pause menu and the end of a mission
bool P_PAUSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool C_GCPAUSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);
bool P_ENDMISSION_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2);

// ui_score
void SeparateNumber(uint score, char* scoreText);

extern char BotString[4];   // ui_mp.cpp

#endif // UI_H
