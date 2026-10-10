#ifndef UI_H
#define UI_H

#include "../actionhelpers.h"

#include "../util/Stack.h"

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
    MessageType_ListSetSelectionColour = 0x73, // List (+0x548). Args: a1 = colour (guess). Returns: 0.
    MessageType_GetCurrentPage = 0x74,       // Manager. Returns: hashcode of the current page (or of the page under an overlay).
    MessageType_SetLineSpacing = 0x75,       // List, Memo. Args: a1 = row height in % of the font height (sets visible rows). Returns: 0.
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
} ControlType;

// MessageType_SetState's argument (M_CONTROL.state)
typedef enum {
    CONTROL_STATE_SHOWN = 0,
    CONTROL_STATE_HIDDEN = 1,
    CONTROL_STATE_INERT = 2,    // shown, but the cursor skips it
} ControlState;

// Radios (a row of options picked with left and right, e.g. the multiplayer settings)
#define RADIO_ADD_ITEM(manager, radio, label, value) __Menu_Send(manager, radio, MessageType_AddItem, (int)Txt_BindLabel(label, 0), value)
#define RADIO_CLEAR(manager, radio) __Menu_Send(manager, radio, MessageType_ClearItems, 0, 0);
#define RADIO_SELECT_ITEM(manager, radio, value) __Menu_Send(manager, radio, MessageType_SelectItemByValue, value, 0);
#define RADIO_GET_VALUE(manager, radio) __Menu_Send(manager, radio, MessageType_GetSelectedItemValue, 0, 0)
// An item's enabled flag as the game reads and writes it: its first byte.
#define ITEM_ENABLED(item) (*(uchar *)&(item).enabled)
#define RADIO_GET_VALUE_OF(radio) __Menu_SendMessage(radio, MessageType_GetSelectedItemValue, 0, 0)

// Scrolls (the iris wheels among them)
#define SCROLL_GET_VALUE(control) __Menu_SendMessage(control, MessageType_GetValue, 0, 0)

// Label
#define LABEL_SET_TEXT(manager, label, text) __Menu_Send(manager, label, MessageType_SetText, (int)text, 0)
#define LABEL_SET_COLOUR(manager, label, colour) __Menu_Send(manager, label, MessageType_SetColour, colour, 0)

// Controls
#define CONTROL_GET(manager, control) (M_CONTROL*)__Menu_Send(manager, control, MessageType_GetControl, 0, 0)

#pragma pack(push, 1)

// The part every control, page and the manager share (docs/ui/messages.md). Each type extends it: ordinary
// controls continue with their script slots (M_WIDGET), the page and the manager with their own fields.
typedef struct M_CONTROL {
    M_CONTROL *next;            // 0x00 in the page's control list
    M_CONTROL *prev;            // 0x04
    char pad_08[4];
    M_CONTROL *parent;          // 0x0c the page for a control, the manager for a page; messages bubble up to it
    HASHCODE extraControlHash;  // 0x10 page only: a control Page_Update also updates
    char pad_14[4];
    HASHCODE hashcode;          // 0x18 the C_/P_ hashcode Handler_HandleMessage dispatches on
    uint drawState;             // 0x1c 0x10 normal, 0x20 focused, 0x40 pressed/checked, 0x80 under the cursor
    int id;                     // 0x20 SetId/GetId: a key's character code, a wheel's item; __Menu_SendEx picks by it
    uint framesFocused;         // 0x24
    char scripts[16];           // 0x28 LINKEDLIST of the control's scripts
    void *selectedScript;       // 0x38
    void *scriptPlaying;        // 0x3c
    char pad_40[0x10];
    int lastScriptResult;       // 0x50
    char pad_54[4];
    uint scriptExecutionFlags;  // 0x58
    char processes[16];         // 0x5c LINKEDLIST of running processes (colour fades)
    char pad_6c[4];
    short x;                    // 0x70 relative to the page
    short y;                    // 0x72
    short width;                // 0x74
    short height;               // 0x76
    ushort depth;               // 0x78 draw and hit order
    uchar state;                // 0x7a SetState: bit 0 hidden, bit 1 not selectable
    uchar type;                 // 0x7b ControlType
} M_CONTROL;
static_assert(sizeof(M_CONTROL) == 0x7c, "M_CONTROL is 0x7c bytes");
static_assert(offsetof(M_CONTROL, hashcode) == 0x18, "M_CONTROL hashcode offset incorrect");
static_assert(offsetof(M_CONTROL, x) == 0x70, "M_CONTROL x offset incorrect");
static_assert(offsetof(M_CONTROL, type) == 0x7b, "M_CONTROL type offset incorrect");

// An ordinary control (every type but the page and the manager): the shared part, then the default scripts.
typedef struct M_WIDGET {
    M_CONTROL control;
    void *defaultScripts[7];    // 0x7c SetScript slots: 0 created/entered, 1 focus gained, 2 selected, 3 scrolled,
                                //      4 focus lost, 5 B pressed
} M_WIDGET;
static_assert(sizeof(M_WIDGET) == 0x98, "M_WIDGET is 0x98 bytes");

typedef struct M_SCROLL {
    M_WIDGET widget;
    char pad_98[0x144 - 0x98];
    uchar wrap;                 // 0x144 the value wraps around from max to min (MessageType_ScrollNoWrap clears it)
    uchar proportionalBar;      // 0x145
    char pad_146[2];
} M_SCROLL;
static_assert(sizeof(M_SCROLL) == 0x148, "M_SCROLL is 0x148 bytes");

typedef struct M_PAGE {
    M_CONTROL control;          // type ControlType_Page
    char pad_7c[0x94 - 0x7c];
    char controls[16];          // 0x94 LINKEDLIST of the page's controls
    void *pageLink[2];          // 0xa4 in the manager's page list
    M_CONTROL *focused;         // 0xac the control under the cursor
    M_CONTROL *playerFocus[4];  // 0xb0 SetPlayerFocus; while one is set the d-pad does not move the cursor
    int navigationMode;         // 0xc0 1 four-way, 2 up/down, 3 left/right
    HASHCODE backgroundMovie;   // 0xc4 0 stops the movie
    int framesShown;            // 0xc8
    int inputDelay;             // 0xcc SetInputDelay: no input until framesShown reaches it
} M_PAGE;
static_assert(sizeof(M_PAGE) == 0xd0, "M_PAGE is 0xd0 bytes");

// A menu instance (the front end, the pause menu...): manager[managerNum].
typedef struct M_MANAGER {
    M_CONTROL control;          // type ControlType_Manager
    char pad_7c[0x94 - 0x7c];
    sprite *cursorSprite;       // 0x94 its position picks the focused control
    char pages[16];             // 0x98 LINKEDLIST of all pages
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
    bool exists;                // 0x1d1
    bool takingInput;           // 0x1d2
    bool overlayMode;           // 0x1d3 GoPage flag 2; Back then adds flag 8
    bool inputLocked;           // 0x1d4 LockInput
    bool didPauseAudio;         // 0x1d5
    char pad_1d6[2];
} M_MANAGER;
static_assert(sizeof(M_MANAGER) == 0x1d8, "M_MANAGER is 0x1d8 bytes");
static_assert(offsetof(M_MANAGER, stack) == 0xa8, "M_MANAGER stack offset incorrect");
static_assert(offsetof(M_MANAGER, currentPage) == 0x1bc, "M_MANAGER currentPage offset incorrect");

// A message kept for later: a process (Process_Create; type 0xe1 is a colour fade) or a delayed message.
typedef struct M_MESSAGE {
    char pad_00[0xc];
    int type;                   // 0x0c
    int arg1;                   // 0x10
    int arg2;                   // 0x14
} M_MESSAGE;
static_assert(sizeof(M_MESSAGE) == 0x18, "M_MESSAGE is 0x18 bytes");

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

bool Handler_HandleMessage(uchar param_1, M_CONTROL *param_2, uint param_3, int param_4, int param_5);

// In general, a handler seems to have either C_ or P_ prefix (PAGE and CONTROL?)
// They all take (uchar, M_CONTROL*, uint, uint, int, int) as parameters
// First indicates the manager number (ie the index into manager array)
// Second is the pointer to the M_CONTROL struct
// Third is the hashcode representing some resource (eg a menu item, page)
// Fourth: ??
// Fifth: ??

bool C_SBDOSSIER_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint param_4, int param_5, int param_6);
bool P_DOSSIER_Handler(uchar param_1, M_CONTROL* param_2, uint param_3, uint param_4, int param_5, int param_6);
bool P_MPMAP_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint event, int param_5, int param_6);
bool C_SBMPMAP_Handler(uchar param_1,M_CONTROL *param_2,uint param_3,uint param_4,int param_5,int param_6);
bool P_MPSCENARIO_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6);
bool P_MPPLAYERMODS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6);
bool P_MPENVIROMODS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6);
bool P_DSGADGETS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint messageType, int param_5, int param_6);
bool C_SBDSGTSCROLL_Handler(uchar param_1, M_CONTROL *param_2, uint control, uint eventType, int param_5, int param_6);
bool P_DSWEAPONS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint param_4, int param_5, int param_6);
bool C_SBDSWPSCROLL_Handler(uchar param_1,M_CONTROL *param_2,uint param_3,uint param_4,int param_5,int param_6);
bool P_DSREWARDS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint eventType, int param_5, int param_6);
bool P_DSRECORDS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint eventType, int param_5, int param_6);

// ui_nightfire, ui_mp, ui_codenames
bool P_NFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_SBNFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_NFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_SBNFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_MPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_MPBOTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_SBBOTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_CNMENU_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_SBCNOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_MPBOTCHOOSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_SBMPBTCHOOSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_MPBOTSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_SBMPSCEN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_SBMPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_RBMPSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_MPDEBRIEFING_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_MPCONFIRM_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool P_ATTRACT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_CHCHMUSIC_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_CHCHDRAWALL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_CHCHWS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);

// ui_credits
typedef struct CreditsEntry {
    const char *txt_left;
    const char *txt_right;  // unused (and the right-hand control hidden) on a centred line
    uchar modifiers_left;   // text style: 0 names, 1 companies and section headings, 2 role titles
    uchar modifiers_right;
    uchar centred;          // one line across the middle, the left text only
    uchar pad;
} CreditsEntry;
static_assert(sizeof(CreditsEntry) == 0xc, "CreditsEntry is 12 bytes");
void __stdcall Menu_InitCredits(void);
CreditsEntry* Menu_SetupCredits(uint *numLines_out);
bool P_CREDITS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);

// ui_secrets
uint64_t Menu_UpgradeCheat(uint64_t bonus, uint objId, byte count);
uint64_t Menu_SpecialCodenameCheck(byte *code);
bool P_CNNAME_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);
bool C_KEYBOARD_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2);

// ui_score
void SeparateNumber(uint score, char* scoreText);

#endif // UI_H
