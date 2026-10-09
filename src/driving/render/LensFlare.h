#ifndef DRIVING_RENDER_LENSFLARE_H_
#define DRIVING_RENDER_LENSFLARE_H_

// ---------------------------------------------------------------------------------------------------------------
// RLensFlareManager (8 bytes, one instance at 0x00200f44, a USingleton): the flares RSky::Draw adds where the sky's
// light is on screen. TestFlares draws each as a sprite inside a visibility test; with the 'sunf' texture (no
// 'moon' found) DrawFlares reads a test result back, turns the count of visible pixels into a glare and draws the
// glares. The flares of the last three frames are kept. See LensFlare.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord4

namespace EAGL {
struct TAR;
}
class RViewCamera;

struct LensFlare {                      // 0x120
    float x;                            // +0x00 on the screen, in pixels
    float y;                            // +0x04
    float depth;                        // +0x08 the projected z; TestFlares sets its sprite depth here
    uint32_t unknown0C;
    uint32_t unknown10;                 // +0x10 AddFlare's second argument
    uint8_t unknown14[0x10c];
};
static_assert(sizeof(LensFlare) == 0x120, "a flare is 0x120 bytes");

// The manager's data (0x2200 bytes, operator new)
struct LensFlareData {
    static constexpr int kFrames = 3;
    static constexpr uint32_t kMaxFlares = 10;
    static constexpr int kTests = 16;

    LensFlare flares[kFrames][kMaxFlares];  // +0x0000
    uint32_t counts[kFrames];           // +0x21c0
    int32_t frames[kFrames];            // +0x21cc this frame's flares, the last frame's, the one before's
    EAGL::TAR *texture;                 // +0x21d8 'moon', or 'sunf'
    uint8_t sun;                        // +0x21dc no 'moon': the flares become glares
    uint8_t enabled;                    // +0x21dd
    uint8_t unknown21DE[2];
    int32_t testIndex;                  // +0x21e0 the visibility test last ended
    uint8_t testPending[kTests];        // +0x21e4 a test ended whose result is not read yet
    uint8_t unknown21F4[0xc];
};
static_assert(sizeof(LensFlareData) == 0x2200, "the lens flare data is 0x2200 bytes");
static_assert(offsetof(LensFlareData, counts) == 0x21c0 && offsetof(LensFlareData, texture) == 0x21d8 &&
              offsetof(LensFlareData, testIndex) == 0x21e0 && offsetof(LensFlareData, testPending) == 0x21e4,
              "LensFlareData layout");

class RLensFlareManager {
public:
    const void *vtable;                 // +0x00 0x00192bf4
    LensFlareData *data;                // +0x04

    RLensFlareManager* Construct();                                                     // 0x0009e460
    void Destruct();                                                                    // 0x0009e1b0
    RLensFlareManager* Delete(unsigned flags);  // the scalar deleting destructor, vtable slot 0  // 0x0009e520
    static void Kill();                     // vtable slot 2                             // 0x0009e1d0
    // No flares, the frames in order, no tests pending; vtable slot 1
    void Reset();                                                                       // 0x0009e150
    void Enable(bool enable);                                                           // 0x0009e1f0
    // The frames moved on, the new frame's flares emptied
    void EndFrame();                                                                    // 0x0009e200
    // A flare at a world position, if it is in front and on (or, for the moon, near) the screen (FUN_0009e250;
    // name ours)
    void AddFlare(const Coord4 *position, uint32_t unknown);                            // 0x0009e250
    void DrawFlares();                                                                  // 0x0009e540
    void TestFlares(RViewCamera *view);                                                 // 0x0009e720
};
static_assert(sizeof(RLensFlareManager) == 8, "RLensFlareManager is 8 bytes");

#define TheLensFlareManager (*(RLensFlareManager **)0x00200f44)

#endif // DRIVING_RENDER_LENSFLARE_H_
