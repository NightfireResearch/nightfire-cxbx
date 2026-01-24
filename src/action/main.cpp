#include "actionhelpers.h"

// AUTOGEN
void xboxInitInputDevices(void);
// AUTOGEN
void xboxInitGraphics(void);
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