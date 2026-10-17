#ifndef DRIVING_EAGL_EAGLFONT_H_
#define DRIVING_EAGL_EAGLFONT_H_

// EAGLFont, the FONT driver EAGL installs (docs/driving/eagl.md 4.8): realgraph's FONT_* (Realgraph.cpp) calls
// these through the driver table at 0x001cd114 (draw, start, end = D3DDevice_End, create, destroy, 0x1c, draw
// array) and draws each glyph as an immediate-mode quad. See EaglFont.cpp.

#include <stdint.h>

#include "GeoPrimState.h"
#include "Realgraph.h"
#include "../../common/xbeClass.h"   // LAYOUT_32

// FNTXFont and FNTXGlyph are realgraph's (Realgraph.h).

// FONTEAGL_drawarray's batch: a glyph at a position
struct FNTXBatchEntry {              // 0xc
    const FNTXGlyph *glyph;          // +0x00
    float x;                         // +0x04
    float y;                         // +0x08
};
LAYOUT_32(sizeof(FNTXBatchEntry) == 0xc, "a batch record is 12 bytes");

// The driver's entries, all cdecl. The draw functions return Device::Get() (their tail call).
void* FONTEAGL_drawarray(const FNTXFont *font, const FNTXBatchEntry *batch, int count);   // 0x000ee190 (invented name)
void* FONTEAGL_draw(const FNTXFont *font, const FNTXGlyph *glyph, float x, float y);      // 0x000ee490
void FONTEAGL_createfont(FNTXFont *font);                                                 // 0x000ee760 (invented name)
void FONTEAGL_startdraw(const FNTXFont *font);                                            // 0x000ee920
void FONTEAGL_destroyfont(FNTXFont *font);                                                // 0x000eea20 (invented name)

// The driver's table (0x001cd114), which FONT_installdriver is given
extern FontDriverTable EaglFontDriver;

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

extern EAGL::GeoPrimStateExtension FontState;   // the font's render state (EaglFont.cpp)

#endif // DRIVING_EAGL_EAGLFONT_H_
