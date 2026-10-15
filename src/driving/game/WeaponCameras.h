#ifndef DRIVING_GAME_WEAPONCAMERAS_H_
#define DRIVING_GAME_WEAPONCAMERAS_H_

// ---------------------------------------------------------------------------------------------------------------
// The weapon cameras' screen effects (game tier), two of the renderer's USingleton managers. See WeaponCameras.cpp.
//   RMissileCam (0x1c bytes, one, MissileCam): a full-screen quad of one of three noise textures, a different one
//     each simulation step, over the missile camera's view; RColorize's modes turn it on and set its alpha.
//   RSniperZoom (0xc70 bytes, one, SniperZoom): the sniper scope - the screen copied small into an offscreen
//     buffer and drawn back through two rings of 42 vertices round the first player's aim point; GHud's
//     crosshairs turn it on and set its alpha.
// And a neighbour the vehicle particles use: the colour that goes with a particle system's texture.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"         // Coord4
#include "../render/Materials.h"          // TexturedGeoPrim, USimpleTexturedMaterial
#include "../render/OffscreenBuffer.h"
#include "../../common/xbeClass.h"        // LAYOUT_32

namespace EAGL {
struct TAR;
}

// ---- RMissileCam

class RMissileCam {
public:
    const void *vtable;                 // +0x00 0x00192d38
    EAGL::TAR *textures[3];             // +0x04 'fuz1' 'fuz2' 'fuz3'
    uint8_t enabled;                    // +0x10 RColorize's infrared mode and mode 2 set it
    uint8_t unknown11[3];
    int32_t alpha;                      // +0x14 0x20 infrared, 0x40 mode 2
    TexturedGeoPrim *geoPrim;           // +0x18

    RMissileCam* Construct();                                                                   // 0x000a0a50
    void Destruct();                    // the GeoPrim freed                                    // 0x000a0df0
    RMissileCam* Delete(unsigned flags);    // the scalar deleting destructor, vtable slot 0   // 0x000a0e70
    static void Kill();                     // vtable slot 2 (slot 1, Reset, is an empty function)  // 0x000a0b30
    // The quad over the whole screen, unless the mission manager's cinematicMode is set
    void Draw();                                                                                // 0x000a0b50
};
LAYOUT_32(sizeof(RMissileCam) == 0x1c, "RMissileCam is 0x1c bytes");
LAYOUT_32(offsetof(RMissileCam, enabled) == 0x10 && offsetof(RMissileCam, alpha) == 0x14 &&
          offsetof(RMissileCam, geoPrim) == 0x18, "RMissileCam layout");

extern RMissileCam *MissileCam;   // RMissileCam::Init's instance
extern EAGL::GeoPrimState MissileCamState;          // Draw's static; DestroyStatic_00200f60 at exit
extern uint32_t MissileCamStateMade;                // its guard
extern uint32_t MissileCamVertexCount[4];           // the GeoPrim's vertex count record
extern Coord4 MissileCamPositions[4];               // the screen's corners, set at every draw

// ---- RSniperZoom

constexpr int kSniperRingVertices = 42;

class RSniperZoom {
public:
    const void *vtable;                 // +0x000 0x00193008
    uint8_t active;                     // +0x004 GHud's crosshairs set it; GHud::AimOff clears it
    uint8_t unknown005[3];
    int32_t alpha;                      // +0x008 GHud's crosshairs set it; 0xff at construction
    ROffscreenBuffer backBuffer;        // +0x00c 64 x 64
    uint32_t unknown02C;
    Coord4 innerPositions[kSniperRingVertices];     // +0x030 the inner ring: a strip between two circles
    Coord4 outerPositions[kSniperRingVertices];     // +0x2d0
    Coord4 innerUVs[kSniperRingVertices];           // +0x570 the positions as fractions of the screen
    Coord4 outerUVs[kSniperRingVertices];           // +0x810
    uint32_t innerColours[kSniperRingVertices];     // +0xab0
    uint32_t unknownB58[2];
    uint32_t outerColours[kSniperRingVertices];     // +0xb60
    uint32_t unknownC08[2];
    USimpleTexturedMaterial material;   // +0xc10

    RSniperZoom* Construct();                                                                   // 0x000a69a0
    void Destruct();                                                                            // 0x000a6810
    RSniperZoom* Delete(unsigned flags);    // the scalar deleting destructor, vtable slot 0   // 0x000a6a50
    static void Kill();                     // vtable slot 2                                     // 0x000a6870
    void Reset();                           // vtable slot 1: off, alpha 0                       // 0x000a6890
    // The post-processing's subsampled back buffer copied into the offscreen buffer, when active (the mission
    // manager's +0x4f0 turns it off)
    void GrabBackBuffer();                                                                      // 0x000a68a0
    // The rings' positions, texture coordinates and colours
    void SetupVerts();                                                                          // 0x000a6a80
    // The rings, the outer first, through the offscreen buffer's texture, when active
    void Draw();                                                                                // 0x000a6bc0
};
LAYOUT_32(sizeof(RSniperZoom) == 0xc70, "RSniperZoom is 0xc70 bytes");
LAYOUT_32(offsetof(RSniperZoom, backBuffer) == 0x0c && offsetof(RSniperZoom, innerPositions) == 0x30 &&
          offsetof(RSniperZoom, outerPositions) == 0x2d0 && offsetof(RSniperZoom, innerUVs) == 0x570 &&
          offsetof(RSniperZoom, outerUVs) == 0x810 && offsetof(RSniperZoom, innerColours) == 0xab0 &&
          offsetof(RSniperZoom, outerColours) == 0xb60 && offsetof(RSniperZoom, material) == 0xc10,
          "RSniperZoom layout");

extern RSniperZoom *SniperZoom;   // RSniperZoom::Init's instance
extern float SniperCopySpread;          // MakeTexCopy's
extern int32_t SniperCopies;
extern float SniperOuterRadius;         // the rings' radii, in pixels: inner to middle, middle to outer
extern float SniperMiddleRadius;
extern float SniperInnerRadius;
extern int32_t SniperRingShade;         // the grey of the inner ring's outer edge and the outer ring's inner edge
extern int32_t SniperOuterShade;        // the grey of the outer ring's outer edge

// A point of a ring: `radius` from the first player's aim point (its camera's +0x1d0, +0x1d4, from the screen's
// centre) at `turns`, z and w 1, and the same over the screen's size (the name is ours). The core of
// FUN_000a68f0, which takes the outputs in ESI and EDI.
void SniperRingPoint(Coord4 *position, Coord4 *uv, float radius, float turns);
void FUN_000a68f0();                    // naked: ESI position, EDI uv, then radius and turns on the stack

// ---- the vehicle particles' sprite rectangles

// A particle system type's sprite in its texture (RParticleSystemData's u, v, uvSize and the word after), by
// comparison of `texture` with the texture names of five of the vehicle particles' types: a corner and a size for
// each (0, 0 and 0x100 for any other); u and v get the corner plus a half, the size less one, all over 256; the
// fourth word 0.
// (The name is ours.) The core of FUN_000a6cd0, which takes the texture in EAX and the rectangle in ECX.
void ParticleTextureRect(uint32_t texture, float *rect);
void FUN_000a6cd0();                    // naked: EAX texture, ECX rectangle

#endif // DRIVING_GAME_WEAPONCAMERAS_H_
