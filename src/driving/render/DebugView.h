#ifndef DRIVING_RENDER_DEBUGVIEW_H_
#define DRIVING_RENDER_DEBUGVIEW_H_

// ---------------------------------------------------------------------------------------------------------------
// The renderer's debug views and its random start-up (0x0008b2b0..0x0008b5b0):
//   RRenderDebugViewPerspective (0x50 bytes, vtable 0x001919a0): a view that takes another view's camera and
//     extents before each render.
//   RRenderDebugViewScreenSpace (0x4c bytes, vtable 0x001919b4): a screen-space view; with the debug flag set it
//     draws a translucent grey quad 512 by 450, and each render it works out a fraction from the player car's
//     speed and draws the audio debug display.
//   RRandom::StartUp: the game's random generator seeded.
// RRenderHigh's constructor makes the two views. See DebugView.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../camera/Camera.h"           // RViewCamera, RCamera

class RRenderDebugViewPerspective : public RViewCamera {
public:
    RViewCamera *source;                // +0x4c

    RRenderDebugViewPerspective* Construct(RViewCamera *source);                                // 0x0008b300
    RRenderDebugViewPerspective* Delete(unsigned flags);    // the scalar deleting destructor, slot 0   // 0x0008b4d0
    // Vtable slot 2: the source's camera copied into this view's, and its extents taken
    void PreRender();                                                                           // 0x0008b4f0
};
static_assert(sizeof(RRenderDebugViewPerspective) == 0x50, "RRenderDebugViewPerspective is 0x50 bytes");

class RRenderDebugViewScreenSpace : public RViewCamera {
public:
    RRenderDebugViewScreenSpace* Construct();                                                   // 0x0008b3a0
    RRenderDebugViewScreenSpace* Delete(unsigned flags);    // the scalar deleting destructor, slot 0   // 0x0008b510
    // Vtable slot 1: with the debug flag set, a translucent grey quad from (0, 0) to (512, 450)
    void Debug();                                                                               // 0x0008b3c0
    // Vtable slot 4
    void DoRender();                                                                            // 0x0008b530
};
static_assert(sizeof(RRenderDebugViewScreenSpace) == 0x4c, "RRenderDebugViewScreenSpace is 0x4c bytes");

class RRandom {
public:
    // The generator's state from `seed`: a fixed start stepped seed % 500 times
    static void StartUp(unsigned seed);                                                         // 0x0008b2b0
};

#endif // DRIVING_RENDER_DEBUGVIEW_H_
