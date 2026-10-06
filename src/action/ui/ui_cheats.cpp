// The cheat menu's checkboxes.
#include "ui.h"
#include "../sound/SFX.h"
#include "../engine/Camera.h"
#include "../engine/viewFov.h"

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

// The widescreen switches: the menu's, and the copy the cameras read (Camera_CalcViewAngles).
#define switch_CONFIG_WIDESCREEN U32_AT(0x001df9e8)
#define IsWidescreen U32_AT(0x001f6610)

// Widescreen: the checkbox is the switch, and every viewer's projection is worked out again for the new shape -
// the players' at their field of view, the rest at the game's own.
// AUTOINJECT
bool C_CHCHWS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select) {
        switch_CONFIG_WIDESCREEN = __Menu_SendMessage(control, MessageType_GetValue, 0, 0);
        IsWidescreen = switch_CONFIG_WIDESCREEN;
        for (ushort viewer = 0; viewer < 9; viewer++)
            Camera_CalcViewAngles(viewer, ViewFov_ForViewer(viewer));
    } else if (message == MessageType_ControlCreated) {
        __Menu_SendMessage(control, MessageType_SetValue, switch_CONFIG_WIDESCREEN, 0);
    }
    return true;
}
