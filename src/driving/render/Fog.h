#ifndef DRIVING_RENDER_FOG_H_
#define DRIVING_RENDER_FOG_H_

// ---------------------------------------------------------------------------------------------------------------
// The fog (Ghidra: RFog, a USingleton; one instance, Fog): its settings from the "Render:Fog" tuning database, handed
// to the render context, and a scale on the fog's end that fades to a target over a number of game ticks. See
// Fog.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../engine/USingleton.h"

// The fog's settings (0x1c bytes, new'd by the constructor; the name is ours)
struct FogParams {
    float start;                        // +0x00 "fog START"
    float end;                          // +0x04 baseEnd * scale, the end the render context is given
    float density;                      // +0x08 "fog Density"
    uint32_t mode;                      // +0x0c "fog mode": the render context's fog table mode
    uint32_t colour;                    // +0x10 "Fog Colour", 0xAARRGGBB
    float baseEnd;                      // +0x14 "fog END"
    float scale;                        // +0x18
};
static_assert(sizeof(FogParams) == 0x1c, "the fog's settings are 0x1c bytes");

// The fog (0x1c bytes, vtable 0x00191514: the scalar deleting destructor, the singleton's empty reset, Kill)
class RFog {
public:
    USingleton singleton;               // +0x00 the base, its vtable pointer
    FogParams *params;                  // +0x04
    uint8_t fading;                     // +0x08 the scale is on its way to fadeTarget
    uint8_t unknown09[3];
    uint32_t fadeEndTick;               // +0x0c the game tick the fade ends at
    float fadeRate;                     // +0x10 the scale's change per tick
    uint32_t unknown14;
    float fadeTarget;                   // +0x18

    RFog* Construct();                                                                          // 0x0007d930
    RFog* Delete(unsigned flags);       // the scalar deleting destructor, vtable slot 0          // 0x0007dac0
    // Vtable slot 2, the singleton's kill: deletes the instance
    void Kill();                                                                                // 0x0007daa0

    uint32_t FogColour();                                                                       // 0x0007d790
    // On and off on the renderer's render context; EnableFog sets the end first
    void DisableFog();                                                                          // 0x0007d7a0
    void EnableFog();                                                                           // 0x0007d820
    void SetFogParams();                                                                        // 0x0007d840
    // Starts a fade of the scale to `scale` over `seconds`, if that ends after this tick (the name is ours)
    void FadeScale(float scale, float seconds);                                                 // 0x0007d7b0
    // A step of the fade, and the settings handed over again
    void UpdateScale();                                                                         // 0x0007d8c0
};
static_assert(sizeof(RFog) == 0x1c, "RFog is 0x1c bytes");
static_assert(offsetof(RFog, params) == 4 && offsetof(RFog, fadeEndTick) == 0xc && offsetof(RFog, fadeTarget) == 0x18,
              "RFog layout");

#define Fog (*(RFog **)0x001ec004)

#endif // DRIVING_RENDER_FOG_H_
