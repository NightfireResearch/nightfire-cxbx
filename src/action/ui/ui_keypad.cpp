#include "ui.h"

#include "Menu.h"
#include "../game.h"

#include <stdio.h>
#include <string.h>

// The keypad page: each key adds its digit to the code being entered (GlobalVars' decoder digits), and the digits
// so far are shown.

#define GlobalVars (*((GlobalVars_t*)0x001f6568))

// XBE_GLOBAL(0x0025dc48, 0x8)
char keypad_text[8];

// AUTOINJECT
bool C_KEYPAD_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select) {
        // a full code starts again
        if (GlobalVars.DecoderDigitAt >= GlobalVars.DecoderNumDigits)
            GlobalVars.DecoderDigitAt = 0;
        GlobalVars.DecoderDisplay[GlobalVars.DecoderDigitAt] = control->id + '0';
        uint length = GlobalVars.DecoderDigitAt + 1;
        memcpy(keypad_text, GlobalVars.DecoderDisplay, length);
        keypad_text[length] = 0;
        LABEL_SET_TEXT(managerNum, SUB_C_KEYPAD_ENTRY_TEXT, keypad_text);
        GlobalVars.DecoderDigitAt++;
    } else if (message == MessageType_ControlCreated) {
        sprintf(keypad_text, "****");
        LABEL_SET_TEXT(managerNum, SUB_C_KEYPAD_ENTRY_TEXT, keypad_text);
        __Menu_Send(managerNum, SUB_C_KEYPAD_ENTRY_TEXT, MessageType_SetTextFlag2000, 0, 0);
    }
    return true;
}
