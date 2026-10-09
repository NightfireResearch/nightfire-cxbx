#ifndef DRIVING_RENDER_POSTPROCESSING_H_
#define DRIVING_RENDER_POSTPROCESSING_H_

// ---------------------------------------------------------------------------------------------------------------
// RPostProcessing (one instance, 8 bytes, and its private data): the glow. Each frame the back buffer is copied,
// shrunk, into a 128 x 128 buffer; a second buffer is darkened by a translucent box and the copy's texels drawn
// into it as 16,384 point sprites, one per texel, by the "BondPostGlare" render method; the result is laid over the
// screen as a sprite. The other effects (the gain, the sniper zoom) take the shrunk copy. See PostProcessing.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"         // Coord4
#include "../eagl/GeoPrimState.h"
#include "../eagl/Model.h"                // DynamicModel
#include "../eagl/RenderMethod.h"         // RenderMethod, GeoPrimParam
#include "../engine/USingleton.h"
#include "../world/WorldPos.h"

class ROffscreenBuffer;
namespace EAGL {
struct TAR;
}

constexpr int kGlowSize = 0x80;                     // the glow buffers' width and height
constexpr int kGlowPoints = kGlowSize * kGlowSize;
typedef float GlowPoint[3];             // x, y, z

// The glow's GeoPrim (0x3c bytes): the render method and its seven parameters - the state, the texture, the
// vertex count, the view's matrix, the constants, the points and their colours (the texels)
struct GlowGeoPrim {
    EAGL::RenderMethod *method;         // +0x00
    EAGL::GeoPrimParam params[7];       // +0x04

    // The method made from "BondPostGlare", the view's matrix found; the rest empty (FUN_000a4580, the name is
    // ours)
    GlowGeoPrim* Construct();                                                                   // 0x000a4580
};
static_assert(sizeof(GlowGeoPrim) == 0x3c, "the glow's GeoPrim is 0x3c bytes");

// The glow's parameters as the render method reads them (GlowGeoPrim::params)
enum GlowParam {
    kGlowParamState = 0,
    kGlowParamTexture = 1,
    kGlowParamCount = 2,
    kGlowParamMatrix = 3,
    kGlowParamConstants = 4,
    kGlowParamPoints = 5,
    kGlowParamColours = 6,
};

// RPostProcessing's private data (0x30128 bytes, the name is ours)
struct RPostProcessingData {
    ROffscreenBuffer *buffers[3];       // +0x00 the copy to lock, the shrunk copy, the glow
    EAGL::TAR *backBuffer;              // +0x0c
    EAGL::TAR *frontBuffer;             // +0x10
    EAGL::TAR *texture;                 // +0x14 'carS'
    EAGL::GeoPrimState state;           // +0x18
    int32_t pointCount;                 // +0x64
    int32_t unknown68;                  // +0x68 0
    int32_t unknown6C;                  // +0x6c 1
    int32_t unknown70;                  // +0x70 1
    float glowConstants[4];             // +0x74 the intensity, a scale, 1, the intensity's inverse
    EAGL::DynamicModel model;           // +0x84
    bool clearPending;                  // +0xdc the glow buffer is cleared before its next draw
    uint8_t unknownDD[3];
    GlowGeoPrim prim;                   // +0xe0
    bool needsLock;                     // +0x11c the copy's texture is locked on the first grab
    uint8_t unknown11D[3];
    void *texels;                       // +0x120 its bits, locked
    GlowPoint points[kGlowPoints];      // +0x124 each texel's place on the glow buffer, by its swizzled index
    bool doGlow;                        // +0x30124 "Do glow"
    uint8_t unknown30125[3];

    RPostProcessingData* Construct();   // FUN_000a4630                                          // 0x000a4630
    void Destruct();                    // FUN_000a4690                                          // 0x000a4690
    // The glow buffer darkened, then the points drawn into it as sprites (FUN_000a4710, the name is ours)
    void DrawGlowPoints(uint32_t count, GlowPoint *glowPoints, void *colours);                    // 0x000a4710
    // The glow laid over the screen (FUN_000a48b0, the name is ours)
    void Draw();                                                                                // 0x000a48b0
    // The copy's texture locked once, the constants set, the glow drawn (FUN_000a4ee0, the name is ours)
    void UpdateGlow();                                                                          // 0x000a4ee0
};
static_assert(sizeof(RPostProcessingData) == 0x30128, "the post-processing data is 0x30128 bytes");
static_assert(offsetof(RPostProcessingData, state) == 0x18 && offsetof(RPostProcessingData, model) == 0x84 &&
              offsetof(RPostProcessingData, prim) == 0xe0 && offsetof(RPostProcessingData, points) == 0x124 &&
              offsetof(RPostProcessingData, doGlow) == 0x30124, "RPostProcessingData layout");

class RPostProcessing {
public:
    USingleton singleton;               // +0x00 the base, its vtable pointer: RPostProcessing's (0x00192ec4)
    RPostProcessingData *data;          // +0x04

    RPostProcessing* Construct();                                                               // 0x000a49a0
    void Destruct();                                                                            // 0x000a4f70
    RPostProcessing* Delete(unsigned flags);    // the scalar deleting destructor, vtable slot 0 // 0x000a5060
    static void Kill();                         // vtable slot 2                                 // 0x000a4eb0
    void Reset();                               // vtable slot 1                                 // 0x000a4550
    void Draw();                                                                                // 0x000a4ed0
    void GrabBackBuffer();                                                                      // 0x000a5020
    EAGL::TAR* GetSubsampledBackBuffer();                                                       // 0x000a4560
};
static_assert(sizeof(RPostProcessing) == 8, "RPostProcessing is 8 bytes");

#define ThePostProcessing (*(RPostProcessing **)0x0020175c)     // RPostProcessing::Init's instance

#endif // DRIVING_RENDER_POSTPROCESSING_H_
