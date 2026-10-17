#include "MenuScript.h"

// The menus' keyframe scripts on their controls (docs/ui/framework.md) - not the engine's level scripts.

// Stops the script a control is playing. The name is shared with the engine's Script_Stop (engine/Script.cpp).
// FUNC_AT(0x00096d70)
void Script_Stop(M_CONTROL *control) {
    if (control != NULL)
        control->scriptPlaying = NULL;
}
