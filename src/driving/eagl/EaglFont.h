#ifndef DRIVING_EAGL_EAGLFONT_H_
#define DRIVING_EAGL_EAGLFONT_H_

// EAGLFont, the FONT driver EAGL installs (docs/driving/eagl.md 4.8): realgraph's FONT_* (Realgraph.cpp) calls
// these through the driver table at 0x001cd114 (draw, start, end = D3DDevice_End, create, destroy, 0x1c, draw
// array) and draws each glyph as an immediate-mode quad. See EaglFont.cpp.

#include <stdint.h>

#include "GeoPrimState.h"

// The driver's entries, all cdecl. The draw functions return Device::Get() (their tail call).
void* FONTEAGL_drawarray(const uint8_t *font, const uint8_t *batch, int count);   // 0x000ee190 (invented name)
void* FONTEAGL_draw(const uint8_t *font, const uint8_t *glyph, float x, float y);  // 0x000ee490
void FONTEAGL_createfont(uint8_t *font);                                            // 0x000ee760 (invented name)
void FONTEAGL_startdraw(const uint8_t *font);                                       // 0x000ee920
void FONTEAGL_destroyfont(uint8_t *font);                                           // 0x000eea20 (invented name)

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
