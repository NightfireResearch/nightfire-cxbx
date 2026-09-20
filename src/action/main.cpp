#include "actionhelpers.h"
#include "engine/psiInput.h" // xboxInitInputDevices is reimplemented there now, talking to real XInput directly

#include "engine/Direct3D/d3dSeam.h" // xboxInitGraphics is reimplemented there now
#include "sound/dsndSeam.h"            // xboxInitSound is reimplemented there now
// AUTOGEN
void xboxInitTextures(void);
// AUTOGEN
void* GetPTPData(void);
// AUTOGEN
void Graphics_Init_LowLevel(void);

// The game's own main - not the entry point of any executable we build. It cannot keep that name in C++,
// which requires main to return int: MSVC accepts "void main", clang rejects it outright. Because the name
// no longer matches the Ghidra export, this is injected by address rather than through AUTOINJECT, which
// would look "Game_Main" up in tools/functions_action.json and not find it.
// FUNC_AT(000e8e90)
void Game_Main(int argc, char **argv) {

    xboxInitInputDevices();
    xboxInitGraphics();
    FS_Init();
    xboxInitTextures();
    xboxInitSound();
    GetPTPData();
    Graphics_Init_LowLevel();

    while(1) {
        mainloop();
    }

}