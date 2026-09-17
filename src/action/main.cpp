#include "actionhelpers.h"
#include "engine/psiInput.h" // xboxInitInputDevices is reimplemented there now, talking to real XInput directly

#include "engine/Direct3D/d3dSeam.h" // xboxInitGraphics is reimplemented there now
// AUTOGEN
void xboxInitTextures(void);
// AUTOGEN
void xboxInitSound(void);
// AUTOGEN
void* GetPTPData(void);
// AUTOGEN
void Graphics_Init_LowLevel(void);

// AUTOINJECT
void main(int argc, char **argv) {

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