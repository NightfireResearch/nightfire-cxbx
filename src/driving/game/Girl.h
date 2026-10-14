#ifndef DRIVING_GAME_GIRL_H_
#define DRIVING_GAME_GIRL_H_

// ---------------------------------------------------------------------------------------------------------------
// GGirl (statics only): the animated figure GHud::DrawPauseMenu draws behind every page of the pause menu
// (DoGirl, each frame of the menu; PauseOff kills it). Its frames are a run-length stream in the HUD gallery ('fi'
// data 0): each frame a small rectangle of eight shades, decoded into one of two 128 x 128 textures in turn
// (GGirlieMaterial) and coloured from a scrolling 128 x 128 palette. It plays at 30 frames a second of the real
// clock. See Girl.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"         // Coord4
#include "../../helpers.h"

class USimpleTexturedMaterial;
namespace EAGL {
struct TAR;
}

// A texture the figure is decoded into, and the quad that draws it (0xd0 bytes; vtable 0x0019fe78, one slot: Delete)
class GGirlieMaterial {
public:
    const void *vtable;                 // +0x00
    uint32_t unknown04[3];
    uint8_t *shape;                     // +0x10 SHAPE_create's 128 x 128 image, 32 bits a pixel
    uint32_t unknown14[3];
    EAGL::TAR *tar;                     // +0x20
    uint32_t unknown24[3];
    USimpleTexturedMaterial *material;  // +0x30
    uint32_t unknown34[3];
    Coord4 positions[4];                // +0x40 the quad, a strip
    uint32_t colours[4];                // +0x80
    Coord4 texCoords[4];                // +0x90 in texels

    GGirlieMaterial* Construct();                                                               // 0x000d7770
    void Destruct();                                                                            // 0x000d7b40
    // The scalar deleting destructor, vtable slot 0
    GGirlieMaterial* Delete(unsigned flags);                                                    // 0x000d7bb0

    GGirlieMaterial* DeleteVirtual(unsigned flags);
};
static_assert(sizeof(GGirlieMaterial) == 0xd0, "GGirlieMaterial is 0xd0 bytes");
static_assert(offsetof(GGirlieMaterial, shape) == 0x10 && offsetof(GGirlieMaterial, tar) == 0x20 &&
              offsetof(GGirlieMaterial, material) == 0x30 && offsetof(GGirlieMaterial, positions) == 0x40 &&
              offsetof(GGirlieMaterial, colours) == 0x80 && offsetof(GGirlieMaterial, texCoords) == 0x90,
              "GGirlieMaterial layout");

class GGirl {
public:
    // The stream from the HUD's gallery and the two textures, the first time
    static void InitGirl();                                                                     // 0x000d7990
    static void KillGirl();                                                                     // 0x000d6f20
    // A frame: the next texture cleared, the frame due decoded into it and drawn
    static void DoGirl();                                                                       // 0x000d7a50
    // The frame due by the clock decoded into the current texture; its place on the screen. False when the
    // stream is missing, too long or a frame too large (the stream is dropped and starts again next time).
    static bool UpdateTexture(int *x, int *y);                                                  // 0x000d6f60
    // The current texture at (x, y), 256 x 256
    static void DrawTexture(int x, int y);                                                      // 0x000d7690
};

// The figure's state (Girl.cpp)
constexpr int kGirlTextures = 2;
extern GGirlieMaterial *GirlMaterials[kGirlTextures];
extern int32_t GirlCurrent, GirlActive, GirlUnknownB0, GirlUnknownB4, GirlUnknownB8, GirlFps, GirlUnmirrored;
extern uint8_t *GirlStream, *GirlData;
extern float PaletteX, PaletteY, PaletteSpeedX, PaletteSpeedY, PaletteX2, PaletteY2, PaletteSpeedX2, PaletteSpeedY2;
extern int32_t GirlFrameStart, GirlCursor, GirlTick, GirlFrames, GirlFrame;
extern float GirlTime;
extern const int8_t GirlPalette[128][128][3];   // GirlPalette.cpp

// The quad's size on the screen (the static initialisers compute them: 2 x 128)
extern int32_t GirlWidth;
extern int32_t GirlHeight;

#endif // DRIVING_GAME_GIRL_H_
