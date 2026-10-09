#ifndef DRIVING_RENDER_PARTICULATE_H_
#define DRIVING_RENDER_PARTICULATE_H_

// ---------------------------------------------------------------------------------------------------------------
// RParticulate: 200 points drawn round the camera while the "Enable Particulate" tuning value is on, a singleton
// (fgParticulate). Each frame they drift by a noise, those further than `range` from a point `range` ahead of the
// camera are put back at random within `range` of it, and each fades with its distance from the camera and is
// shaded by its angle to the player's car. See Particulate.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Materials.h"                  // USimpleMaterial
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../engine/USingleton.h"

constexpr int kParticulateCount = 200;

// (field names ours)
class RParticulate {
public:
    const USingletonVtable *vtable;     // +0x000 a USingleton's (0x00192e54)
    uint8_t unknown04[0xc];
    Coord4 positions[kParticulateCount];    // +0x010 w 1
    uint32_t colours[kParticulateCount];    // +0xc90 ARGB
    float range;                        // +0xfb0
    float alpha;                        // +0xfb4 the alpha at the camera, before the clamp to 255
    uint8_t unknownFB8[8];
    Coord4 colour;                      // +0xfc0 red, green, blue (0-1)
    float driftAmplitude;               // +0xfd0 0: no drift
    float driftFrequency;               // +0xfd4
    float driftSpeed;                   // +0xfd8 the noise's phase a simulation step
    float shadeAngle;                   // +0xfdc 1 - the cosine below which points are shaded; 0: none
    float shadeStrength;                // +0xfe0
    uint8_t unknownFE4[0xc];
    Coord3 drift;                       // +0xff0 this frame's
    bool enabled;                       // +0xffc "Enable Particulate"
    uint8_t unknownFFD[3];
    uint32_t fullBatches;               // +0x1000 draws of 512 points before the last
    uint32_t lastBatchSize;             // +0x1004
    uint8_t unknown1008[8];
    USimpleMaterial material;           // +0x1010

    RParticulate* Construct();                                                                  // 0x000a3f30
    void Destruct();                                                                            // 0x000a3240
    // Vtable slot 0: the destructor; bit 0 of `flags` frees the object too.
    RParticulate* Delete(unsigned flags);                                                       // 0x000a3fa0
    // Vtable slot 2: deletes fgParticulate (without clearing it).
    void Kill();                                                                                // 0x000a3290
    // The settings and the points, scattered round the origin.
    void _init();                                                                               // 0x000a32b0
    void LoadAttributes();                                                                      // 0x000a3210
    void Update();                                                                              // 0x000a3fd0
    void Draw();                                                                                // 0x000a3e60
};
static_assert(sizeof(RParticulate) == 0x1070, "RParticulate is 0x1070 bytes");
// The material is not standard-layout, so neither is RParticulate: its offsets are checked under MSVC only.
#ifdef _MSC_VER
static_assert(offsetof(RParticulate, positions) == 0x10 && offsetof(RParticulate, colours) == 0xc90 &&
              offsetof(RParticulate, range) == 0xfb0 && offsetof(RParticulate, colour) == 0xfc0 &&
              offsetof(RParticulate, driftAmplitude) == 0xfd0 && offsetof(RParticulate, drift) == 0xff0 &&
              offsetof(RParticulate, enabled) == 0xffc && offsetof(RParticulate, fullBatches) == 0x1000 &&
              offsetof(RParticulate, material) == 0x1010, "RParticulate layout");
#endif

#define fgParticulate (*(RParticulate **)0x00201754)

#endif // DRIVING_RENDER_PARTICULATE_H_
