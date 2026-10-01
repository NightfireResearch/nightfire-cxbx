// The cheat menu's checkboxes.
#include "ui.h"
#include "../sound/SFX.h"

extern uint32_t SoundInfo; // defined in game.cpp: the music volume the cheat menu toggles between 0 and 100 (low word)
extern uint32_t switch_ForceDrawAll; // defined in view.cpp: draw every object, not just the visible cels'

// Music on/off: the checkbox shows whether the volume is non-zero, and selecting it sets 100 or 0.
// AUTOINJECT
bool C_CHCHMUSIC_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select) {
        ushort volume = __Menu_SendMessage(control, MessageType_GetValue, 0, 0) ? 100 : 0;
        SoundInfo = (SoundInfo & 0xffff0000) | volume;   // the original writes only the low word
        SFXMusicSetVolume(volume);
    } else if (message == MessageType_ControlCreated) {
        __Menu_SendMessage(control, MessageType_SetValue, (SoundInfo & 0xffff) != 0, 0);
    }
    return true;
}

// Draw everything: the checkbox is the flag.
// AUTOINJECT
bool C_CHCHDRAWALL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select) {
        switch_ForceDrawAll = __Menu_SendMessage(control, MessageType_GetValue, 0, 0);
    } else if (message == MessageType_ControlCreated) {
        __Menu_SendMessage(control, MessageType_SetValue, switch_ForceDrawAll, 0);
    }
    return true;
}
