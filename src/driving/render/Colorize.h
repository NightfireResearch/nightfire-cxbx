#ifndef DRIVING_RENDER_COLORIZE_H_
#define DRIVING_RENDER_COLORIZE_H_

// ---------------------------------------------------------------------------------------------------------------
// RColorize (0x60 bytes, one instance at 0x001f6898, a USingleton): the full-screen colouring drawn over the world
// view - the motion blur (the last frame's front buffer drawn over the new one), the modes SetEnabled picks
// (infrared and two others: tints, a light at the eye, full-screen boxes) and the area brightness that eases the
// infrared light. See Colorize.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord4

namespace EAGL {
struct TAR;
}

class RColorize {
public:
    // SetEnabled's modes
    enum Mode : int32_t {
        kModeOff = 0,
        kModeInfrared = 1,              // ActManager::SetIRMode(true)
        kMode2 = 2,
        kMode3 = 3,                     // scaled by blastStrength in Draw
    };

    const void *vtable;                 // +0x00 0x001927c8
    uint8_t unknown04[0xc];
    Coord4 colourA;                     // +0x10
    Coord4 colourB;                     // +0x20 the light at the eye
    int32_t mode;                       // +0x30 Mode
    int32_t motionBlurState;            // +0x34 0 after EnableMotionBlur, 1 after DisableMotionBlur, 2 once drawn
    uint32_t brightnessChangeTime;      // +0x38 the step count when the area brightness changed; 0: settled
    uint32_t tintColour;                // +0x3c
    uint32_t tintColour2;               // +0x40 a full-screen box, blend mode 2
    uint32_t tintColour3;               // +0x44 a full-screen box, blend mode 5
    float areaBrightness;               // +0x48
    float prevAreaBrightness;           // +0x4c
    float motionBlurAmount;             // +0x50 the last frame's alpha, 0..1
    float blastStrength;                // +0x54
    EAGL::TAR *frontBuffer;             // +0x58
    uint32_t unknown5C;

    RColorize* Construct();                                                             // 0x0009a420
    static void Kill();                     // vtable slot 2                             // 0x0009a480
    void Reset();                           // vtable slot 1                             // 0x0009ac00
    // brightness squared, times ten
    void SetAreaBrightness(float brightness);                                           // 0x0009a4a0
    void EnableMotionBlur(float amount);                                                // 0x0009a4d0
    void DisableMotionBlur();                                                           // 0x0009a4f0
    void SetEnabled(int mode);                                                          // 0x0009a500
    void Draw();                                                                        // 0x0009a6e0
};
static_assert(sizeof(RColorize) == 0x60, "RColorize is 0x60 bytes");
static_assert(offsetof(RColorize, colourA) == 0x10 && offsetof(RColorize, mode) == 0x30 &&
              offsetof(RColorize, tintColour) == 0x3c && offsetof(RColorize, areaBrightness) == 0x48 &&
              offsetof(RColorize, frontBuffer) == 0x58, "RColorize layout");

#define Colorize (*(RColorize **)0x001f6898)

#endif // DRIVING_RENDER_COLORIZE_H_
