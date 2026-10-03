#ifndef DRIVING_EAGL_EAGLFONT_H_
#define DRIVING_EAGL_EAGLFONT_H_

// EAGLFont, the FONT driver EAGL installs (docs/driving/eagl.md 4.8): realgraph's FONT_* (Realgraph.cpp) calls
// these through the driver table at 0x001cd114 (draw, start, end = D3DDevice_End, create, destroy, 0x1c, draw
// array) and draws each glyph as an immediate-mode quad. See EaglFont.cpp.

#include <stdint.h>

#include "GeoPrimState.h"

namespace EAGL {
struct TAR;
}

// A realgraph FNTX font, the fields the driver (and the profiler) use. Realgraph.cpp reads +0x0a..+0x18 and the
// scales; the driver keeps its TAR, shape and texture scale at the end.
struct FNTXFont {
    uint8_t unknown00[0xa];          // +0x00
    uint16_t glyphCount;             // +0x0a
    uint32_t flags;                  // +0x0c 0x40000: 16-byte glyphs with 16-bit advances and an indexed kern table
    uint8_t unknown10[2];            // +0x10
    uint8_t ascent;                  // +0x12
    uint8_t descent;                 // +0x13
    int32_t glyphTableOffset;        // +0x14
    int32_t kernTableOffset;         // +0x18
    int32_t shapeOffset;             // +0x1c the font's SHAPE image, from the font
    uint32_t colour;                 // +0x20 every vertex's
    uint32_t unknown24;              // +0x24
    uint32_t z;                      // +0x28 every vertex's z and w (float bits, passed on as they are)
    uint32_t w;                      // +0x2c
    float texOffsetU;                // +0x30
    float texOffsetV;                // +0x34
    float scaleX;                    // +0x38 position scale
    float scaleY;                    // +0x3c
    uint8_t unknown40[8];            // +0x40
    int32_t filterMode;              // +0x48 1: the TAR's filter 2, else 1
    int32_t depthMode;               // +0x4c 0: no Z writes and ALWAYS, 1: Z writes and LEQUAL, else left
    uint8_t unknown50[0x20];         // +0x50
    EAGL::TAR *tar;                  // +0x70 over the shape (FONTEAGL_createfont)
    uint8_t *shape;                  // +0x74
    float texScaleV;                 // +0x78 1 / the shape's height when it is linear, else 1
    float texScaleU;                 // +0x7c 1 / the shape's width
};
static_assert(sizeof(FNTXFont) == 0x80, "the font fields the driver uses end at 0x80");

// A glyph, the fields the driver reads.
struct FNTXGlyph {
    uint16_t code;                   // +0x00
    uint8_t width;                   // +0x02
    uint8_t height;                  // +0x03
    uint16_t texX;                   // +0x04 position in the texture
    uint16_t texY;                   // +0x06
    int8_t advance;                  // +0x08
    int8_t xOffset;                  // +0x09
    int8_t yOffset;                  // +0x0a
    uint8_t kernCount;               // +0x0b
};
static_assert(sizeof(FNTXGlyph) == 0xc, "the glyph fields the driver reads end at 0xc");

// FONTEAGL_drawarray's batch: a glyph at a position
struct FNTXBatchEntry {              // 0xc
    const FNTXGlyph *glyph;          // +0x00
    float x;                         // +0x04
    float y;                         // +0x08
};
static_assert(sizeof(FNTXBatchEntry) == 0xc, "a batch record is 12 bytes");

// The driver's entries, all cdecl. The draw functions return Device::Get() (their tail call).
void* FONTEAGL_drawarray(const FNTXFont *font, const FNTXBatchEntry *batch, int count);   // 0x000ee190 (invented name)
void* FONTEAGL_draw(const FNTXFont *font, const FNTXGlyph *glyph, float x, float y);      // 0x000ee490
void FONTEAGL_createfont(FNTXFont *font);                                                 // 0x000ee760 (invented name)
void FONTEAGL_startdraw(const FNTXFont *font);                                            // 0x000ee920
void FONTEAGL_destroyfont(FNTXFont *font);                                                // 0x000eea20 (invented name)

// 0x000eea50, unreferenced: D3DDevice_SetRenderState inlined as a routine of its own, the state in ESI and the
// value in EDI (one of the three "render-state dump blocks").
void EAGLFont_SetRenderStateEsiEdi();

namespace EAGL {

// 0x000eec50, unreferenced: a GeoPrimState's copy assignment (the 0x4c bytes as 19 words).
struct GeoPrimStateCopy : GeoPrimState {
    GeoPrimStateCopy* Assign(const GeoPrimState *other);                     // 0x000eec50
};
static_assert(sizeof(GeoPrimStateCopy) == 0x4c, "the copy is the GeoPrimState itself");

}  // namespace EAGL

#endif // DRIVING_EAGL_EAGLFONT_H_
