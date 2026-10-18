#ifndef UI_MENUCOMMON_H
#define UI_MENUCOMMON_H

// What the control types' files share (Label.cpp, Button.cpp, Checkbox.cpp, Scroll.cpp, List.cpp, Radio.cpp, Memo.cpp,
// Page.cpp, Window.cpp, Process.cpp, Manager.cpp, MenuManager.cpp)

#include "Controls.h"
#include "Menu.h"
#include "MenuScript.h"

#include <math.h>
#include <stdint.h>

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static inline int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// A sprite's priority that hides it
#define MENU_SPRITE_HIDDEN 0xff

// The game's strings that controls keep pointers to
#define MenuEmptyString ((char *)0x0015d308)    // ""
#define MenuSpaceString ((char *)0x00160d8c)    // " "
#define MenuAlphabet ((char *)0x0015eb24)       // the letters a font's height is measured by
// Formats: a byte 0xff and a font, then options (0xfe an alignment, 0xfc and 0xfb a scale)
#define ListFormat1 ((char *)0x0015eb5c)        // a list's at first (MessageType_SetFont 1)
#define ListFormat2 ((char *)0x0016197c)        // MessageType_SetFont 2
#define ListFormat3 ((char *)0x00161984)        // MessageType_SetFont 3
#define PlainFormat ((char *)0x0015eb20)        // a memo's at first; a checkbox's text is measured in it
#define CentredFormat ((char *)0x0015eb18)      // a button's and a radio's label
#define LabelFormat ((char *)0x00161964)        // a label's at first, of 9 bytes

// Font_GetAlignment's, and a list column's
#define ALIGN_RIGHT  2
#define ALIGN_CENTRE 3

// Menu_PlaySound's sounds
#define MENU_SFX_SELECT         0   // with MENU_SOUND_SELECT
#define MENU_SFX_ALTSELECT_1    1   // with MENU_SOUND_ALT1
#define MENU_SFX_ALTSELECT_2    2   // with MENU_SOUND_ALT2
#define MENU_SFX_PREV           3   // a scroll's value down
#define MENU_SFX_NEXT           4   // up
#define MENU_SFX_PREV_REPEAT    5   // held
#define MENU_SFX_NEXT_REPEAT    6
#define MENU_SFX_FOCUS          7   // the focus moves
#define MENU_SFX_BACK           8   // with MENU_SOUND_BACK
#define MENU_SFX_BACK_ON_CONTROL 9  // none plays

// A colour's three high bytes raised by the highlight's pulse; a carry runs into the byte above
inline uint PulsedColour(uint colour, int alpha) {
    return ((colour & 0xff0000) + (alpha << 16)) | ((colour & 0xff000000) + (alpha << 24)) |
           ((colour & 0xff00) + (alpha << 8)) | (colour & 0xff);
}

// MessageType_SetScript: slots 0 to 6, the seventh being the word after a control's six default scripts
inline int Control_SetScript(M_CONTROL *control, int slot, int id) {
    if (slot < 0 || slot > 6)
        return 0;
    ((M_WIDGET *)control)->defaultScripts[slot] = Script_Select(control, id);
    return 1;
}

#endif // UI_MENUCOMMON_H
