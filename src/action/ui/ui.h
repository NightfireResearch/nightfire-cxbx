#ifndef UI_H
#define UI_H

#include "../actionhelpers.h"

typedef enum {
    MessageType_SetText = 0x18,
    MessageType_SetColour = 0x1a,
    MessageType_GetValue = 0x40, // Get the value of the current item (eg the index of the selected item)
    MessageType_GoPage = 0x44, // Go to a new page
    MessageType_Scroll = 0x49, // Scroll (vertical?) event - fired by both d-pad and left analog stick
    MessageType_Select = 0x4b, // Selecting an item - fired by A or Start button
    MessageType_Enter = 0x54, // Entering / Loading the menu page?
} MessageType;

typedef enum {
    ControlType_Scroll = 0x0a
} ControlType;

#pragma pack(push, 1)

// TODO: Unfinished
typedef struct M_MANAGER {
    char pad[0x1bc];
    uint field158_0x1bc;
} M_MANAGER;

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

#endif // UI_H