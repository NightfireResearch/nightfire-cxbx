#ifndef UI_H
#define UI_H

#include "../actionhelpers.h"

#include "../util/Stack.h"

typedef enum {
    MessageType_AddTextToScroll = 0x10,
    MessageType_ScriptExitLoop = 0x14, // Calls "Script_ExitLoop" - Unknown what it does
    MessageType_ScriptPlay = 0x16, // Calls "Script_Play" - Unknown what it does
    MessageType_Unknown_0x17,
    MessageType_SetText = 0x18,
    MessageType_SetColour = 0x1a,
    MessageType_SelectScrollItem = 0x1e,
    MessageType_SetValue = 0x2e,
    MessageType_GetScrollValue = 0x35,
    MessageType_GetValue = 0x40, // Get the value of the current item (eg the index of the selected item)
    MessageType_GoPage = 0x44, // Go to a new page
    MessageType_Destroy = 0x47, // Delete/destroy this item
    MessageType_Scroll = 0x49, // Scroll event - fired by both d-pad and left analog stick. Can be vertical (iris menu) or horizontal (eg selecting Max Points in MP)
    MessageType_Select = 0x4b, // Selecting an item - fired by A or Start button
    MessageType_MaybeEnterPage = 0x4c,
    MessageType_MaybeGainFocus = 0x4e,
    MessageType_MaybeLoseFocus = 0x4f,
    MessageType_Enter = 0x54, // Entering / Loading the menu page?
    MessageType_Unknown_0x5f = 0x5f,
} MessageType;

typedef enum {
    ControlType_Scroll = 0x0a
} ControlType;


#define SCROLL_ADD_ITEM(manager, scroll, label, value) __Menu_Send(manager, scroll, MessageType_AddTextToScroll, (int)Txt_BindLabel(label, 0), value)
#define SCROLL_INIT(manager, scroll) __Menu_Send(manager, scroll, MessageType_Unknown_0x17, 0, 0);
#define SCROLL_SELECT_ITEM(manager, scroll, value) __Menu_Send(manager, scroll, MessageType_SelectScrollItem, value, 0);
#define SCROLL_GET_VALUE(manager, scroll) __Menu_Send(manager, scroll, MessageType_GetScrollValue, 0, 0)

#pragma pack(push, 1)

// TODO: Unfinished
typedef struct M_MANAGER {
    char pad[0xa8];
    STACKINFO stack; // 0xa8
    int stackMem[64];
    char pad2[12]; // 0xb0 to 0x1bc
    uint field158_0x1bc;
} M_MANAGER;

static_assert(offsetof(M_MANAGER, stack) == 0xa8, "M_MANAGER stack offset incorrect");
static_assert(offsetof(M_MANAGER, field158_0x1bc) == 0x1bc, "M_MANAGER field158_0x1bc offset incorrect");

// TODO: Unfinished
typedef struct M_CONTROL {
    char pad[0x18];
    uint hashcode; // 0x18
    char pad2[0x7b-0x18-4];
    char type; // 0x7b
    char pad3[0x144-0x7b-1];
    char field_0x144; // 0x144 - purpose unknown
} M_CONTROL;

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

// ui_score
void SeparateNumber(uint score, char* scoreText);

#endif // UI_H
