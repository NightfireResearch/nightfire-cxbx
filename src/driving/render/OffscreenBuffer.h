#ifndef DRIVING_RENDER_OFFSCREENBUFFER_H_
#define DRIVING_RENDER_OFFSCREENBUFFER_H_

// ---------------------------------------------------------------------------------------------------------------
// ROffscreenBuffer (0x20 bytes; 0x0007f5a0..0x0007f9f0): a texture to draw into - an EAGL texture render context
// over a colour target and an optional depth target, with one orthographic viewport. The reflections, the
// post-processing and the sniper zoom make them; MakeCopy and MakeTexCopy draw a texture into one as a screen-sized
// sprite (MakeTexCopy adding fainter copies offset round it). See OffscreenBuffer.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

struct D3DPixelContainer;
namespace EAGL {
struct TAR;
struct TextureRenderContext;
struct ViewPort;
}  // namespace EAGL

class ROffscreenBuffer {
public:
    D3DPixelContainer *texture;         // +0x00 the D3D texture made for it (asTexture), else NULL
    EAGL::TextureRenderContext *context;    // +0x04
    EAGL::ViewPort *viewPort;           // +0x08
    EAGL::TAR *colourTarget;            // +0x0c
    EAGL::TAR *depthTarget;             // +0x10 NULL without depth
    int32_t width;                      // +0x14
    int32_t height;                     // +0x18
    int32_t bitDepth;                   // +0x1c the targets' (unused by a D3D texture's colour target)

    // asTexture: the colour target is a D3D render-target texture (A8R8G8B8) rather than an EAGL render target
    ROffscreenBuffer* Construct(int width, int height, int bitDepth, bool withDepth, bool asTexture); // 0x0007f610
    void Destruct();                    // the viewport deleted                                 // 0x0007f5a0
    // Brackets drawing into it: the frame and view begun, everything cleared; then ended (the names are ours)
    void Begin();                                                                               // 0x0007f5b0
    void End();                                                                                 // 0x0007f5f0
    // `source` drawn over the whole buffer, its texture coordinates running to scale times the screen's width
    // and the screen's height
    void MakeCopy(EAGL::TAR *source, int scale);                                                // 0x0007f740
    // `source` drawn over the whole buffer, then `copies` more at an alpha of 255 / (n + 2), each offset by
    // `spread` along one of the four diagonals
    void MakeTexCopy(EAGL::TAR *source, int copies, float spread);                              // 0x0007f870
};
static_assert(sizeof(ROffscreenBuffer) == 0x20, "ROffscreenBuffer is 0x20 bytes");
static_assert(offsetof(ROffscreenBuffer, colourTarget) == 0x0c && offsetof(ROffscreenBuffer, bitDepth) == 0x1c,
              "ROffscreenBuffer layout");

#endif // DRIVING_RENDER_OFFSCREENBUFFER_H_
