#include "d3dSeam.h"

#include "../../common/gfx/d3d9Backend.h"
#include "../../common/standalone.h"
#include "../../common/xbeEntrySeam.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The driving engine's graphics seam - docs/driving-engine-plan.md section 6.1.
//
// WHERE THE SEAM IS, AND WHY IT IS NOT WHERE THE ACTION ENGINE'S IS.
//
// The action engine's seam sits above D3D8: Eurocom's own thin wrapper functions are reimplemented in C++, and
// each D3D8 call inside them goes either to the XBE's library (under CXBX) or to the native backend. That
// works there because those wrappers are a small, well-understood layer with good symbols.
//
// The driving engine's equivalent layer is EAGL, which is most of the binary, largely unnamed, and reaches
// D3D8 from everywhere - 108 entry points against the action engine's 41. Reimplementing enough of EAGL to
// cover them all before anything renders would be a very long time with nothing running. So this seam is at
// the library boundary instead: every D3D8 and XGRAPHC entry point is patched at its own address, and EAGL
// runs exactly as built. That is the same technique the plan prescribes for the DirectSound entry points the
// sound seam cannot cover from above (section 6.2), applied to graphics because the boundary is the same
// shape: the backend's API already mirrors D3D8 entry point for entry point.
//
// The table and the stubbing are src/common/xbeEntrySeam.cpp, shared with the sound seam, which stands in
// front of DirectSound the same way and for the same reasons. The table itself is d3d8Entries.inc, generated
// from the binary by tools/xbe_entry_points.py.
//
// CALLING CONVENTIONS. Everything here is __stdcall unless the table says the function pops nothing while
// plainly taking arguments, which means they arrive in registers. D3DDevice_SetRenderState_Simple is the one
// of those so far - it writes ECX and EDX straight into the push buffer - and it is handled by a naked
// adapter below. The backend's functions are ordinary __cdecl, so each replacement is a __stdcall wrapper
// rather than the backend function itself: jumping a __stdcall call site into a __cdecl function would leave
// the arguments on the stack.
// ---------------------------------------------------------------------------------------------------------------

static XbeEntry g_entries[] = {
#define XBE_ENTRY(name, address, stack)        { #name, address, stack, 0, 0, false },
#define XBE_ENTRY_UNKNOWN_STACK(name, address) { #name, address, XBE_ENTRY_STACK_UNKNOWN, 0, 0, false },
#include "d3d8Entries.inc"
#undef XBE_ENTRY
#undef XBE_ENTRY_UNKNOWN_STACK
};

static XbeEntrySeam g_seam = { g_entries, sizeof(g_entries) / sizeof(g_entries[0]), "d3dSeam" };

// ---------------------------------------------------------------------------------------------------------------
// The entry points that do have an implementation.
//
// Each is a __stdcall function with the original's signature, forwarding to the backend. Where the backend
// wants something in a different shape - a viewport as six numbers rather than a structure, a float where the
// stack holds a dword - the conversion happens here, so the backend stays free of Xbox-isms it does not need.
// ---------------------------------------------------------------------------------------------------------------

// The game's tick rate, as Timer_Init (0x0010ae50) left it: 50 on a PAL video mode, 60 on an NTSC one.
#define GameTimerFrequency (*(const uint32_t *)0x00242424u)

static uint32_t __stdcall Seam_Direct3D_CreateDevice(uint32_t adapter, uint32_t deviceType, void *focusWindow,
                                                     uint32_t behaviourFlags, void *presentationParameters,
                                                     void **returnedDevice) {
    // The backend paces Present to the refresh rate in the present parameters, standing in for the vertical
    // blank an Xbox Swap would have waited for. EAGL leaves that field zero unless the game asked for a
    // particular rate, which would mean no pacing at all - so where it is zero, the video mode's own rate is
    // what it means, and that is the rate Timer_Init already derived.
    uint32_t *parameters = (uint32_t *)presentationParameters;
    if (parameters != NULL && parameters[11] == 0 && GameTimerFrequency != 0) {
        parameters[11] = GameTimerFrequency;
        printf("[d3dSeam] no refresh rate in the present parameters; pacing to the game's %u Hz tick\n",
               GameTimerFrequency);
    }

    return D3D9_CreateDevice(adapter, deviceType, focusWindow, behaviourFlags,
                             parameters, returnedDevice);
}

static void __stdcall Seam_D3D_SetPushBufferSize(uint32_t pushBufferSize, uint32_t kickOffSize) {
    D3D9_SetPushBufferSize(pushBufferSize, kickOffSize);
}

static void __stdcall Seam_D3DDevice_Clear(uint32_t count, void *rects, uint32_t flags, uint32_t colour,
                                           uint32_t z, uint32_t stencil) {
    // Z arrives as the raw bits of a float, pushed like any other dword.
    float depth;
    memcpy(&depth, &z, sizeof(depth));
    D3D9_Clear(count, rects, flags, colour, depth, stencil);
}

static void __stdcall Seam_D3DDevice_Swap(uint32_t flags) {
    D3D9_Swap(flags);
}

static uint32_t *__stdcall Seam_D3DDevice_GetBackBuffer2(int32_t index) {
    return D3D9_GetBackBuffer2(index);
}

static void *__stdcall Seam_D3DDevice_GetDepthStencilSurface2(void) {
    return D3D9_GetDepthStencilSurface2();
}

static void *__stdcall Seam_D3DTexture_GetSurfaceLevel2(void *texture, uint32_t level) {
    return D3D9_GetSurfaceLevel2(texture, level);
}

static void __stdcall Seam_D3DDevice_SetShaderConstantMode(uint32_t mode) {
    D3D9_SetShaderConstantMode(mode);
}

static void __stdcall Seam_D3DDevice_SetRenderState_CullMode(int cullMode) {
    D3D9_SetCullMode(cullMode);
}

static void __stdcall Seam_D3DDevice_SetRenderState_ZEnable(uint32_t value) {
    D3D9_SetZEnable(value);
}

static void __stdcall Seam_D3DDevice_SetRenderState_FogColor(uint32_t colour) {
    D3D9_SetFogColor(colour);
}

static void __stdcall Seam_D3DDevice_SetTexture(uint32_t stage, void *texture) {
    D3D9_SetTexture(stage, texture);
}

static void __stdcall Seam_D3DResource_Register(void *resource, uint32_t data) {
    D3D9_ResourceRegister(resource, data);
}

static uint32_t __stdcall Seam_D3DResource_Release(void *resource) {
    return D3D9_ResourceRelease(resource);
}

static void __stdcall Seam_D3DResource_BlockUntilNotBusy(void *resource) {
    D3D9_BlockUntilNotBusy(resource);
}

static void __stdcall Seam_XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage,
                                              int format, uint32_t pool, void *texture, uint32_t data,
                                              uint32_t pitch) {
    D3D9_XGSetTextureHeader(width, height, levels, usage, format, pool, texture, data, pitch);
}

// The Xbox D3DVIEWPORT8, as the entry point receives it.
struct XboxViewport { uint32_t X, Y, Width, Height; float MinZ, MaxZ; };

static void __stdcall Seam_D3DDevice_SetViewport(const XboxViewport *viewport) {
    if (viewport == NULL)
        return;
    D3D9_SetViewport(viewport->X, viewport->Y, viewport->Width, viewport->Height,
                     viewport->MinZ, viewport->MaxZ);
}

// The Xbox D3DSURFACE_DESC, which is not the PC one: there is no Size field between Pool and
// MultiSampleType, so Width and Height sit at +0x14 and +0x18. Read off the original Get2DSurfaceDesc
// (0x0016e390), which writes exactly these seven words - and getting it wrong is not academic, since the
// first version of this file had the PC layout and EAGL built its backbuffer texture header out of the
// wrong two words.
struct XboxSurfaceDesc {
    uint32_t Format, Type, Usage, Pool, MultiSampleType, Width, Height;
};

#define XBOX_MULTISAMPLE_NONE 0x11u

static void FillSurfaceDesc(void *surface, XboxSurfaceDesc *desc) {
    if (desc == NULL)
        return;
    memset(desc, 0, sizeof(*desc));
    desc->Type = 1;                              // D3DRTYPE_SURFACE
    desc->MultiSampleType = XBOX_MULTISAMPLE_NONE;
    D3D9_GetSurfaceDesc(surface, &desc->Format, &desc->Width, &desc->Height);
}

static void __stdcall Seam_D3DSurface_GetDesc(void *surface, XboxSurfaceDesc *desc) {
    FillSurfaceDesc(surface, desc);
}

// The same thing one level down, and the one the rest of EAGL calls directly. The middle argument is the mip
// level, which only matters for textures; every caller here passes zero.
static void __stdcall Seam_Get2DSurfaceDesc(void *surface, uint32_t level, XboxSurfaceDesc *desc) {
    (void)level;
    FillSurfaceDesc(surface, desc);
}

// The gamma ramp the game reads at startup and puts back later. There is no Xbox ramp to read, so hand back
// the identity - which is what is in effect anyway, since the backend does not touch the host's gamma.
struct XboxGammaRamp { uint8_t red[256], green[256], blue[256]; };

static void __stdcall Seam_D3DDevice_GetGammaRamp(XboxGammaRamp *ramp) {
    if (ramp == NULL)
        return;
    for (int i = 0; i < 256; i++)
        ramp->red[i] = ramp->green[i] = ramp->blue[i] = (uint8_t)i;
}

// Memory tiling. The nv2a can mark regions of memory as tiled so that framebuffer access is faster; D3D9
// resources have no such notion, and the game only reads the tiles back in order to restore them, so
// reporting them as unset and accepting whatever is set is both truthful and inert.
struct XboxTile { uint32_t Flags, Size, Pitch, ZStartTag, ZOffset, ZEndTag; };

static void __stdcall Seam_D3DDevice_GetTile(uint32_t index, XboxTile *tile) {
    (void)index;
    if (tile != NULL)
        memset(tile, 0, sizeof(*tile));
}

static void __stdcall Seam_D3DDevice_SetTile(uint32_t index, const XboxTile *tile) {
    (void)index; (void)tile;
}

// The nv2a's screen-space offset is the sub-pixel origin of rasterisation. The backend applies the pixel
// centre offset D3D9 needs in its own vertex path, so taking this one as well would shift everything twice.
static void __stdcall Seam_D3DDevice_SetScreenSpaceOffset(float x, float y) {
    (void)x; (void)y;
}

// Stencil, and the shadow-buffer comparison that goes with it. The backend has no stencil support yet - it
// creates a depth-stencil buffer but never sets a stencil state - so these are accepted and dropped rather
// than reported on every frame. They are on the list for when shadows are looked at.
static void __stdcall Seam_D3DDevice_SetRenderState_StencilEnable(uint32_t value) {
    (void)value;
}

static void __stdcall Seam_D3DDevice_SetRenderState_StencilFail(uint32_t value) {
    (void)value;
}

static void __stdcall Seam_D3DDevice_SetRenderState_ShadowFunc(uint32_t value) {
    (void)value;
}

static uint32_t __stdcall Seam_D3DDevice_CreateVertexShader(const void *declaration, const void *function,
                                                            void **handle, uint32_t usage) {
    return D3D9_CreateVertexShader(declaration, function, handle, usage);
}

// ---------------------------------------------------------------------------------------------------------------
// Textures, surfaces and palettes.
//
// An Xbox texture is a twenty-byte header - Common, Data, Lock, Format, Size - and a block of memory the game
// fills in itself; there is no host texture until something binds it, at which point the backend builds one
// from those words. So creating a texture here is allocating the pixels and writing the header the backend
// reads, which is the same header XGSetTextureHeader writes and is why that function does the work.
//
// THE UNCACHED ALIAS. The original Lock functions return the pixel pointer with bit 31 set, which on the
// console is the same memory seen uncached, and the original Create masks the pointer to 28 bits because a
// console address fits in that. Neither applies here: our pointers are ordinary Win32 addresses with no alias
// and no 28-bit guarantee, so both are left alone. That is safe to do only because every site in the binary
// that sets bit 31 on an address is inside the D3D8 library this seam replaces - checked, rather than assumed:
// a search of the image finds fifteen, fourteen of them in D3D8 and the last one a flag on a physics slot
// index. EAGL never does it itself.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_TEXTURE_COMMON_WORD 0x01040001u   // D3DCOMMON_TYPE_TEXTURE, refcount 1
#define XBOX_PALETTE_COMMON_WORD 0x01030001u   // D3DCOMMON_TYPE_PALETTE, refcount 1

struct XboxPixelContainer {
    uint32_t Common;
    uint32_t Data;
    uint32_t Lock;
    uint32_t Format;
    uint32_t Size;
};

// Enough of the format table to size an allocation. The backend has the same knowledge for its uploads; this
// copy is deliberately the small half of it - how many bits a pixel takes - rather than a second opinion on
// anything the backend decides.
static uint32_t XboxFormatBits(uint32_t format) {
    switch (format & 0xFF) {
        case 0x00: case 0x01: case 0x0b: case 0x13: case 0x19: case 0x1b: case 0x1f:
            return 8;                       // L8, AL8, P8, and their linear forms
        case 0x0c:
            return 4;                       // DXT1
        case 0x0e: case 0x0f:
            return 8;                       // DXT3, DXT5
        case 0x06: case 0x07: case 0x12: case 0x1e:
            return 32;                      // A8R8G8B8, X8R8G8B8, and their linear forms
        default:
            return 16;
    }
}

static void *__stdcall Seam_D3DDevice_CreateTexture2(uint32_t width, uint32_t height, uint32_t depth,
                                                     uint32_t levels, uint32_t usage, uint32_t format,
                                                     uint32_t resourceType) {
    (void)depth; (void)resourceType;
    if (width == 0 || height == 0)
        return NULL;
    if (levels == 0)
        levels = 1;

    // The mip chain is a third again on top of the base level at most, and rounding the base up to a whole
    // number of 64-byte rows covers the pitch alignment a linear texture is given. Over-allocating by a few
    // kilobytes is the cheap side of this to be wrong on.
    uint32_t bits = XboxFormatBits(format);
    uint32_t pitch = (((width * bits) / 8) + 63) & ~63u;
    uint32_t bytes = pitch * height;
    bytes += bytes / 2;

    XboxPixelContainer *texture = (XboxPixelContainer *)calloc(1, sizeof(XboxPixelContainer));
    void *pixels = calloc(1, bytes);
    if (texture == NULL || pixels == NULL) {
        free(texture);
        free(pixels);
        printf("[d3dSeam] out of memory for a %ux%u texture\n", width, height);
        return NULL;
    }

    // The header the backend reads, built by the same function the game would have called itself.
    D3D9_XGSetTextureHeader(width, height, levels, usage, (int)format, 0, texture,
                            (uint32_t)(uintptr_t)pixels, 0);
    texture->Common = XBOX_TEXTURE_COMMON_WORD;
    texture->Data = (uint32_t)(uintptr_t)pixels;
    return texture;
}

// The locked rectangle, as both LockRect entry points fill it in: pitch first, then the pointer.
struct XboxLockedRect { uint32_t Pitch; void *pBits; };

// Somewhere to put a lock of the backbuffer or the depth surface. Those are the backend's stand-ins: they
// have no pixels, and their Data word is a marker rather than an address, so handing it back has the game
// reading or writing through it - which it duly did, as a fault inside memcpy at 0x0bb00000. One scratch
// buffer per stand-in, big enough for the frame, is somewhere harmless for a write to go. A read gets zeros,
// which is wrong for a screen-capture effect and right for everything else; capturing the backbuffer
// properly is the backend's business, and it does exactly that for the action engine with a GPU copy.
static void *ScratchForStandIn(const void *surface, uint32_t bytes) {
    static const void *surfaces[4];
    static void *buffers[4];
    static uint32_t sizes[4];
    static unsigned count = 0;

    for (unsigned i = 0; i < count; i++) {
        if (surfaces[i] == surface && sizes[i] >= bytes)
            return buffers[i];
    }
    if (count >= sizeof(surfaces) / sizeof(surfaces[0]) || bytes == 0)
        return NULL;

    void *buffer = calloc(1, bytes);
    if (buffer == NULL)
        return NULL;
    surfaces[count] = surface;
    buffers[count] = buffer;
    sizes[count] = bytes;
    count++;
    return buffer;
}

static void LockPixels(void *container, XboxLockedRect *lockedRect) {
    if (lockedRect == NULL)
        return;
    lockedRect->Pitch = 0;
    lockedRect->pBits = NULL;
    if (container == NULL)
        return;

    uint32_t format = 0, width = 0, height = 0;
    D3D9_GetSurfaceDesc(container, &format, &width, &height);

    const XboxPixelContainer *header = (const XboxPixelContainer *)container;
    lockedRect->Pitch = (((width * XboxFormatBits(format)) / 8) + 63) & ~63u;
    if (D3D9_IsStandInSurface(container)) {
        lockedRect->pBits = ScratchForStandIn(container, lockedRect->Pitch * height);
        return;
    }
    lockedRect->pBits = (void *)(uintptr_t)header->Data;

    // Whatever is about to be written lands in memory the backend has already uploaded from, so tell it the
    // copy it holds is stale. The re-upload happens at the next bind, which is after the write.
    D3D9_NotifyTextureModified(container);
}

static uint32_t __stdcall Seam_D3DTexture_LockRect(void *texture, uint32_t level, XboxLockedRect *lockedRect,
                                                   const void *rect, uint32_t flags) {
    (void)level; (void)rect; (void)flags;   // only level 0 and whole-surface locks are asked for
    LockPixels(texture, lockedRect);
    return 0;
}

static uint32_t __stdcall Seam_D3DSurface_LockRect(void *surface, XboxLockedRect *lockedRect,
                                                   const void *rect, uint32_t flags) {
    (void)rect; (void)flags;
    LockPixels(surface, lockedRect);
    return 0;
}

// A palette is 32, 64, 128 or 256 four-byte entries, chosen by the size enum, and the object keeps that enum
// in the top two bits of its Common word exactly as the original does - the game reads it back.
static void *__stdcall Seam_D3DDevice_CreatePalette2(uint32_t size) {
    static const uint32_t entriesForSize[4] = { 256, 128, 64, 32 };
    if (size > 3)
        return NULL;

    XboxPixelContainer *palette = (XboxPixelContainer *)calloc(1, sizeof(XboxPixelContainer));
    void *entries = calloc(entriesForSize[size], sizeof(uint32_t));
    if (palette == NULL || entries == NULL) {
        free(palette);
        free(entries);
        return NULL;
    }

    palette->Common = (size << 30) | XBOX_PALETTE_COMMON_WORD;
    palette->Data = (uint32_t)(uintptr_t)entries;
    return palette;
}

static void *__stdcall Seam_D3DPalette_Lock2(void *palette, uint32_t flags) {
    (void)flags;
    return (palette != NULL) ? (void *)(uintptr_t)((const XboxPixelContainer *)palette)->Data : NULL;
}

// Binding a palette, which the backend expands paletted textures through at upload.
static void __stdcall Seam_D3DDevice_SetPalette(uint32_t stage, void *palette) {
    const XboxPixelContainer *header = (const XboxPixelContainer *)palette;
    D3D9_SetPalette(stage, (header != NULL) ? (const void *)(uintptr_t)header->Data : NULL);
}

// ---------------------------------------------------------------------------------------------------------------
// Immediate mode.
//
// Four entry points that between them are most of the game's two-dimensional drawing: the font renderer, the
// HUD, the loading screen and the movie player. Each vertex arrives as a colour, a texture coordinate and
// then a position, and the position is what completes it - EAGLFont::FONTEAGL_draw (0x000ee490) is the
// clearest example, writing register 3, then 9, then 0xffffffff, four times for a character's quad.
//
// The backend does the collecting and the drawing; these only unpack the arguments. SetVertexData2f and
// SetVertexData4f differ only in how many floats follow the register number.
// ---------------------------------------------------------------------------------------------------------------

static void __stdcall Seam_D3DDevice_Begin(uint32_t primitiveType) {
    D3D9_ImmediateBegin(primitiveType);
}

static void __stdcall Seam_D3DDevice_End(void) {
    D3D9_ImmediateEnd();
}

static void __stdcall Seam_D3DDevice_SetVertexDataColor(uint32_t reg, uint32_t colour) {
    D3D9_ImmediateColour(reg, colour);
}

static void __stdcall Seam_D3DDevice_SetVertexData2f(uint32_t reg, float a, float b) {
    D3D9_ImmediateTexCoord(reg, a, b);
}

static void __stdcall Seam_D3DDevice_SetVertexData4f(uint32_t reg, float a, float b, float c, float d) {
    D3D9_ImmediateVertex(reg, a, b, c, d);
}

// ---------------------------------------------------------------------------------------------------------------
// Stand-alone surfaces, and the swizzle helpers that go with them.
//
// A stand-alone surface is a texture with no texture around it: a header and a block of pixels, used for
// render targets and for the depth surfaces EAGL builds per shadow pass. It is created the same way a texture
// is, and its Common word says surface rather than texture.
//
// Swizzling is the nv2a's texture layout: the address of a texel interleaves the bits of x and y, so that
// nearby texels in both directions are nearby in memory. The game converts between that and a plain raster
// itself for anything it reads back or builds on the CPU, which is what XGSwizzleRect and XGUnswizzleRect
// are for, and the two are the same loop with source and destination exchanged.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_STANDALONE_SURFACE_COMMON 0x81050001u   // D3DCOMMON_TYPE_SURFACE, refcount 1, not owned by a texture

static void *__stdcall Seam_D3D_CreateStandAloneSurface(uint32_t width, uint32_t height, uint32_t levels,
                                                        uint32_t format) {
    if (width == 0 || height == 0)
        return NULL;

    uint32_t bits = XboxFormatBits(format);
    uint32_t pitch = (((width * bits) / 8) + 63) & ~63u;
    uint32_t bytes = pitch * height;

    XboxPixelContainer *surface = (XboxPixelContainer *)calloc(1, sizeof(XboxPixelContainer) + 4);
    void *pixels = calloc(1, bytes);
    if (surface == NULL || pixels == NULL) {
        free(surface);
        free(pixels);
        printf("[d3dSeam] out of memory for a %ux%u surface\n", width, height);
        return NULL;
    }

    D3D9_XGSetTextureHeader(width, height, levels, 0, (int)format, 0, surface,
                            (uint32_t)(uintptr_t)pixels, 0);
    surface->Common = XBOX_STANDALONE_SURFACE_COMMON;
    surface->Data = (uint32_t)(uintptr_t)pixels;
    return surface;
}

// Is this format stored in the nv2a's interleaved layout? The linear formats and the compressed ones are not;
// everything else is. Same rule the backend applies when it uploads.
static uint32_t __stdcall Seam_XGIsSwizzledFormat(uint32_t format) {
    uint32_t f = format & 0xFF;
    bool linear = (f >= 0x10 && f <= 0x20) || f == 0x24 || f == 0x25 ||
                  (f >= 0x2e && f <= 0x31) || (f >= 0x35 && f <= 0x37) || f == 0x3d;
    bool compressed = (f == 0x0c || f == 0x0e || f == 0x0f);
    return (!linear && !compressed) ? 1u : 0u;
}

// The masks that spread x and y into the interleaved address, and the trick that walks them: subtracting a
// mask from an offset and masking again is the increment along that axis. The same construction the backend
// uses for its own uploads.
static void SwizzleMasks(uint32_t width, uint32_t height, uint32_t *maskXOut, uint32_t *maskYOut) {
    uint32_t maskX = 0, maskY = 0, bit = 1, size = 1;
    for (;;) {
        bool any = false;
        if (size < width)  { maskX |= bit; bit <<= 1; any = true; }
        if (size < height) { maskY |= bit; bit <<= 1; any = true; }
        size <<= 1;
        if (!any)
            break;
    }
    *maskXOut = maskX;
    *maskYOut = maskY;
}

// The rectangle both functions take; null means the whole surface.
struct XboxRect { int32_t left, top, right, bottom; };

static void SwizzleCopy(const void *source, uint32_t width, uint32_t height, void *destination,
                        uint32_t pitch, const XboxRect *rect, uint32_t bytesPerPixel, bool toSwizzled) {
    if (source == NULL || destination == NULL || bytesPerPixel == 0)
        return;

    uint32_t left = 0, top = 0, right = width, bottom = height;
    if (rect != NULL) {
        left = (uint32_t)rect->left;
        top = (uint32_t)rect->top;
        right = (uint32_t)rect->right;
        bottom = (uint32_t)rect->bottom;
    }
    if (right > width)   right = width;
    if (bottom > height) bottom = height;
    if (left >= right || top >= bottom)
        return;

    uint32_t maskX = 0, maskY = 0;
    SwizzleMasks(width, height, &maskX, &maskY);
    if (pitch == 0)
        pitch = width * bytesPerPixel;

    uint8_t *linearBase = (uint8_t *)(toSwizzled ? (void *)source : destination);
    uint8_t *swizzledBase = (uint8_t *)(toSwizzled ? destination : (void *)source);

    // Walk the interleaved offset for the first column of the rectangle, then along each row.
    uint32_t yOffset = 0;
    for (uint32_t y = 0; y < top; y++)
        yOffset = (yOffset - maskY) & maskY;

    for (uint32_t y = top; y < bottom; y++) {
        uint32_t xOffset = 0;
        for (uint32_t x = 0; x < left; x++)
            xOffset = (xOffset - maskX) & maskX;

        uint8_t *row = linearBase + (size_t)y * pitch;
        for (uint32_t x = left; x < right; x++) {
            uint8_t *linear = row + (size_t)x * bytesPerPixel;
            uint8_t *swizzled = swizzledBase + (size_t)(yOffset | xOffset) * bytesPerPixel;
            if (toSwizzled)
                memcpy(swizzled, linear, bytesPerPixel);
            else
                memcpy(linear, swizzled, bytesPerPixel);
            xOffset = (xOffset - maskX) & maskX;
        }
        yOffset = (yOffset - maskY) & maskY;
    }
}

static void __stdcall Seam_XGUnswizzleRect(const void *source, uint32_t width, uint32_t height, uint32_t depth,
                                           void *destination, uint32_t pitch, const XboxRect *rect,
                                           uint32_t bytesPerPixel) {
    (void)depth;   // volume textures; nothing here asks for one
    SwizzleCopy(source, width, height, destination, pitch, rect, bytesPerPixel, false);
}

static void __stdcall Seam_XGSwizzleRect(const void *source, uint32_t pitch, const XboxRect *rect,
                                         void *destination, uint32_t width, uint32_t height,
                                         const XboxRect *point, uint32_t bytesPerPixel) {
    (void)point;   // where in the destination to put it; only the whole-surface form is used
    SwizzleCopy(source, width, height, destination, pitch, rect, bytesPerPixel, true);
}

// ---------------------------------------------------------------------------------------------------------------
// Vertex buffers.
//
// An Xbox vertex buffer is a resource header and a block of memory the game writes into directly: Create
// hands back the header, Lock hands back the pointer, and the contents are whatever the game put there by the
// time it draws. There is no host buffer until the backend needs one, which is why this can allocate plain
// memory and leave the uploading to bind time.
//
// The header is laid out to suit both readers. The game reads the first three words - Common, Data, Lock -
// and the backend reads the data pointer from word 1 and the byte size from word 5, because that is where the
// action engine's own vertex-buffer slots keep them (GetHostVertexBuffer in common/gfx/d3d9Backend.cpp). Two
// spare words in between cost nothing and save the backend from having to know which engine made the buffer.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_VERTEXBUFFER_COMMON 0x00000001u   // D3DCOMMON_TYPE_VERTEXBUFFER, refcount 1

struct SeamVertexBuffer {
    uint32_t Common;
    uint32_t Data;
    uint32_t Lock;
    uint32_t reserved[2];
    uint32_t Size;      // word 5, where the backend looks
};

static void *__stdcall Seam_D3DDevice_CreateVertexBuffer2(uint32_t length) {
    if (length == 0)
        return NULL;

    SeamVertexBuffer *buffer = (SeamVertexBuffer *)calloc(1, sizeof(SeamVertexBuffer));
    void *data = calloc(1, length);
    if (buffer == NULL || data == NULL) {
        free(buffer);
        free(data);
        printf("[d3dSeam] out of memory for a %u-byte vertex buffer\n", length);
        return NULL;
    }

    buffer->Common = XBOX_VERTEXBUFFER_COMMON;
    buffer->Data = (uint32_t)(uintptr_t)data;
    buffer->Size = length;
    return buffer;
}

// Locking is just asking where the memory is: there is no GPU-side copy to wait for, because the backend only
// reads the block when something draws from it.
static void *__stdcall Seam_D3DVertexBuffer_Lock2(SeamVertexBuffer *buffer, uint32_t flags) {
    (void)flags;
    return (buffer != NULL) ? (void *)(uintptr_t)buffer->Data : NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// Geometry submission and the rest of the state the renderer sets per draw.
// ---------------------------------------------------------------------------------------------------------------

static void __stdcall Seam_D3DDevice_SetStreamSource(uint32_t streamNumber, void *vertexBuffer,
                                                     uint32_t stride) {
    D3D9_SetStreamSource((int)streamNumber, vertexBuffer, (int)stride);
}

static void __stdcall Seam_D3DDevice_DrawVertices(uint32_t primitiveType, uint32_t startVertex,
                                                  uint32_t vertexCount) {
    D3D9_DrawVertices(primitiveType, startVertex, vertexCount);
}

static void __stdcall Seam_D3DDevice_DrawIndexedVertices(uint32_t primitiveType, uint32_t vertexCount,
                                                         const void *indexData) {
    D3D9_DrawIndexedVertices(primitiveType, vertexCount, indexData);
}

static void __stdcall Seam_D3DDevice_SetVertexShader(void *handle) {
    D3D9_SetVertexShader(handle);
}

static void __stdcall Seam_D3DDevice_SetRenderTarget(void *renderTarget, void *depthStencil) {
    D3D9_SetRenderTarget(renderTarget, depthStencil);
}

static void __stdcall Seam_D3DDevice_SetRenderState_YuvEnable(uint32_t enable) {
    D3D9_SetYuvEnable(enable);
}

static void __stdcall Seam_D3DDevice_SetTextureState_BorderColor(uint32_t stage, uint32_t colour) {
    D3D9_SetTextureBorderColor(stage, colour);
}

// Reference counting on the Common word's low sixteen bits, matching what the backend's release does.
static uint32_t __stdcall Seam_D3DResource_AddRef(void *resource) {
    if (resource == NULL)
        return 0;
    uint32_t *common = (uint32_t *)resource;
    *common = *common + 1;
    return *common & 0xFFFFu;
}

// Fences are the game asking "has the GPU finished with this yet". Every draw here is submitted through D3D9,
// which keeps its own ordering, and nothing the game frees is read by the GPU afterwards - the backend copies
// into host resources - so a fence can be a number that is always already reached.
static uint32_t __stdcall Seam_D3DDevice_InsertFence(void) {
    static uint32_t fence = 0;
    return ++fence;
}

static void __stdcall Seam_D3DDevice_BlockOnFence(uint32_t fence) {
    (void)fence;
}

static uint32_t __stdcall Seam_D3DDevice_IsBusy(void) {
    return 0;
}

// The push buffer the nv2a would have read; there is none, so there is always room in it.
static void __stdcall Seam_D3DDevice_MakeSpace(void) {
}

// Wireframe and point fill modes are a debug feature the game does not use in anger, and D3D9 has them
// through a render state the backend does not expose yet. Accepting and ignoring it keeps solid fill.
static void __stdcall Seam_D3DDevice_SetRenderState_FillMode(uint32_t fillMode) {
    (void)fillMode;
}

// Bump-environment matrices for embossed bump mapping. The backend has no bump path yet, so this is dropped
// rather than reported on every material that sets one.
static void __stdcall Seam_D3DDevice_SetTextureState_BumpEnv(uint32_t stage, uint32_t type, uint32_t value) {
    (void)stage; (void)type; (void)value;
}

// The vertex shader constant family, all register-argument functions like SetRenderState_Simple: ECX is the
// first constant register to write and EDX points at the values. Constant1 writes one register (four floats),
// Constant4 writes four (a matrix), and NotInline writes a run whose length - in dwords - is its one stack
// argument. Confirmed by disassembly at 0x0016a790, 0x0016a7f0 and 0x0016a980, and by their callers: the
// table shows the first two popping nothing, which is what says the arguments cannot be on the stack.
static void __declspec(naked) Seam_D3DDevice_SetVertexShaderConstant1(void) {
    __asm {
        push edx
        push ecx
        call D3D9_SetVertexShaderConstant1
        add  esp, 8
        ret
    }
}

static void __declspec(naked) Seam_D3DDevice_SetVertexShaderConstant4(void) {
    __asm {
        push edx
        push ecx
        call D3D9_SetVertexShaderConstant4
        add  esp, 8
        ret
    }
}

// The same, plus a count: the register index is in ECX, the data in EDX, and the number of dwords on the
// stack. It is the one the EAGL render methods use for anything larger than a matrix, which is why getting
// its shape wrong showed up as a crash inside a model draw rather than anywhere near here.
static void __declspec(naked) Seam_D3DDevice_SetVertexShaderConstantNotInline(void) {
    __asm {
        push [esp + 4]                              // the dword count, the caller's only stack argument
        push edx                                    // where the values are
        push ecx                                    // the first constant register
        call D3D9_SetVertexShaderConstantNotInline
        add  esp, 12                                // __cdecl
        ret  4                                      // and the original's own argument
    }
}

// D3DDevice_SetRenderState_Simple takes the NV2A method in ECX and the value in EDX and writes the pair
// straight into the push buffer - it pops nothing, which is how the generated table spots it. Confirmed by
// disassembly at 0x001673e0, the same arrangement the action engine found at its own address.
static void __declspec(naked) Seam_D3DDevice_SetRenderState_Simple(void) {
    __asm {
        push edx                        // value
        push ecx                        // method
        call D3D9_SetRenderStateSimple
        add  esp, 8                     // __cdecl
        ret
    }
}

// Name, not address: the addresses live in the generated table, and a name that is not in it is a mistake
// worth hearing about rather than a patch that silently lands nowhere.
static const struct { const char *name; void *replacement; unsigned stackBytes; } g_replacements[] = {
    { "Direct3D_CreateDevice",                (void *)Seam_Direct3D_CreateDevice, 24 },
    { "D3D_SetPushBufferSize",                (void *)Seam_D3D_SetPushBufferSize, 8 },
    { "D3DDevice_Clear",                      (void *)Seam_D3DDevice_Clear, 24 },
    { "D3DDevice_Swap",                       (void *)Seam_D3DDevice_Swap, 4 },
    { "D3DDevice_GetBackBuffer2",             (void *)Seam_D3DDevice_GetBackBuffer2, 4 },
    { "D3DDevice_GetDepthStencilSurface2",    (void *)Seam_D3DDevice_GetDepthStencilSurface2, 0 },
    { "D3DTexture_GetSurfaceLevel2",          (void *)Seam_D3DTexture_GetSurfaceLevel2, 8 },
    { "D3DDevice_SetShaderConstantMode",      (void *)Seam_D3DDevice_SetShaderConstantMode, 4 },
    { "D3DDevice_SetRenderState_CullMode",    (void *)Seam_D3DDevice_SetRenderState_CullMode, 4 },
    { "D3DDevice_SetRenderState_ZEnable",     (void *)Seam_D3DDevice_SetRenderState_ZEnable, 4 },
    { "D3DDevice_SetRenderState_FogColor",    (void *)Seam_D3DDevice_SetRenderState_FogColor, 4 },
    { "D3DDevice_SetRenderState_Simple",      (void *)Seam_D3DDevice_SetRenderState_Simple, 0 },
    { "D3DDevice_SetTexture",                 (void *)Seam_D3DDevice_SetTexture, 8 },
    { "D3DDevice_SetViewport",                (void *)Seam_D3DDevice_SetViewport, 4 },
    { "D3DSurface_GetDesc",                   (void *)Seam_D3DSurface_GetDesc, 8 },
    { "Get2DSurfaceDesc",                     (void *)Seam_Get2DSurfaceDesc, 12 },
    { "D3DDevice_GetGammaRamp",               (void *)Seam_D3DDevice_GetGammaRamp, 4 },
    { "D3DDevice_GetTile",                    (void *)Seam_D3DDevice_GetTile, 8 },
    { "D3DDevice_SetTile",                    (void *)Seam_D3DDevice_SetTile, 8 },
    { "D3DDevice_SetScreenSpaceOffset",       (void *)Seam_D3DDevice_SetScreenSpaceOffset, 8 },
    { "D3DDevice_SetRenderState_StencilEnable", (void *)Seam_D3DDevice_SetRenderState_StencilEnable, 4 },
    { "D3DDevice_SetRenderState_StencilFail", (void *)Seam_D3DDevice_SetRenderState_StencilFail, 4 },
    { "D3DDevice_SetRenderState_ShadowFunc",  (void *)Seam_D3DDevice_SetRenderState_ShadowFunc, 4 },
    { "D3DDevice_CreateVertexShader",         (void *)Seam_D3DDevice_CreateVertexShader, 16 },
    { "D3DDevice_SetVertexShader",            (void *)Seam_D3DDevice_SetVertexShader, 4 },
    { "D3DDevice_SetVertexShaderConstant1",   (void *)Seam_D3DDevice_SetVertexShaderConstant1, 0 },
    { "D3DDevice_SetVertexShaderConstant4",   (void *)Seam_D3DDevice_SetVertexShaderConstant4, 0 },
    { "D3DDevice_SetVertexShaderConstantNotInline", (void *)Seam_D3DDevice_SetVertexShaderConstantNotInline, 4 },
    { "D3DDevice_CreateVertexBuffer2",        (void *)Seam_D3DDevice_CreateVertexBuffer2, 4 },
    { "D3DVertexBuffer_Lock2",                (void *)Seam_D3DVertexBuffer_Lock2, 8 },
    { "D3DDevice_SetStreamSource",            (void *)Seam_D3DDevice_SetStreamSource, 12 },
    { "D3DDevice_DrawVertices",               (void *)Seam_D3DDevice_DrawVertices, 12 },
    { "D3DDevice_DrawIndexedVertices",        (void *)Seam_D3DDevice_DrawIndexedVertices, 12 },
    { "D3DDevice_SetRenderTarget",            (void *)Seam_D3DDevice_SetRenderTarget, 8 },
    { "D3DDevice_SetRenderState_YuvEnable",   (void *)Seam_D3DDevice_SetRenderState_YuvEnable, 4 },
    { "D3DDevice_SetRenderState_FillMode",    (void *)Seam_D3DDevice_SetRenderState_FillMode, 4 },
    { "D3DDevice_SetTextureState_BorderColor", (void *)Seam_D3DDevice_SetTextureState_BorderColor, 8 },
    { "D3DDevice_SetTextureState_BumpEnv",    (void *)Seam_D3DDevice_SetTextureState_BumpEnv, 12 },
    { "D3DResource_AddRef",                   (void *)Seam_D3DResource_AddRef, 4 },
    { "D3DDevice_InsertFence",                (void *)Seam_D3DDevice_InsertFence, 0 },
    { "D3DDevice_BlockOnFence",               (void *)Seam_D3DDevice_BlockOnFence, 4 },
    { "D3DDevice_IsBusy",                     (void *)Seam_D3DDevice_IsBusy, 0 },
    { "D3DDevice_MakeSpace",                  (void *)Seam_D3DDevice_MakeSpace, 0 },
    { "D3DDevice_CreateTexture2",             (void *)Seam_D3DDevice_CreateTexture2, 28 },
    { "D3DTexture_LockRect",                  (void *)Seam_D3DTexture_LockRect, 20 },
    { "D3DSurface_LockRect",                  (void *)Seam_D3DSurface_LockRect, 16 },
    { "D3DDevice_CreatePalette2",             (void *)Seam_D3DDevice_CreatePalette2, 4 },
    { "D3DPalette_Lock2",                     (void *)Seam_D3DPalette_Lock2, 8 },
    { "D3DDevice_SetPalette",                 (void *)Seam_D3DDevice_SetPalette, 8 },
    { "D3D_CreateStandAloneSurface",          (void *)Seam_D3D_CreateStandAloneSurface, 16 },
    { "D3DDevice_Begin",                      (void *)Seam_D3DDevice_Begin, 4 },
    { "D3DDevice_End",                        (void *)Seam_D3DDevice_End, 0 },
    { "D3DDevice_SetVertexDataColor",         (void *)Seam_D3DDevice_SetVertexDataColor, 8 },
    { "D3DDevice_SetVertexData2f",            (void *)Seam_D3DDevice_SetVertexData2f, 12 },
    { "D3DDevice_SetVertexData4f",            (void *)Seam_D3DDevice_SetVertexData4f, 20 },
    { "XGIsSwizzledFormat",                   (void *)Seam_XGIsSwizzledFormat, 4 },
    { "XGUnswizzleRect",                      (void *)Seam_XGUnswizzleRect, 32 },
    { "XGSwizzleRect",                        (void *)Seam_XGSwizzleRect, 32 },
    { "D3DResource_Register",                 (void *)Seam_D3DResource_Register, 8 },
    { "D3DResource_Release",                  (void *)Seam_D3DResource_Release, 4 },
    { "D3DResource_BlockUntilNotBusy",        (void *)Seam_D3DResource_BlockUntilNotBusy, 4 },
    { "XGSetTextureHeader",                   (void *)Seam_XGSetTextureHeader, 36 },
};

// ---------------------------------------------------------------------------------------------------------------
// Installing it.
// ---------------------------------------------------------------------------------------------------------------

// Printed alongside the backend's frame timing, through GfxHost_ReportPeriodic - see gfx/backendHost.cpp.
// Only entry points that have actually been reached appear, which is the list that decides what to write
// next.
void D3dSeam_ReportMissing(void) {
    XbeSeam_ReportMissing(&g_seam);
}

void Inject_D3dSeam(void) {
    // Under CXBX the D3D8 library is already replaced by CXBX's HLE, which owns the device; a second backend
    // patched over the same entry points would be two renderers fighting over one window.
    if (!Xbox_RunningStandalone())
        return;

    // Where this build's D3D8 keeps the deferred state the backend reads back at draw time. Read out of
    // D3DDevice_SetRenderStateNotInline (0x00167410) and D3DDevice_SetTextureStageStateNotInline
    // (0x00167e40), which are the functions that write them.
    g_xboxTextureStateTable = 0x00175428u;
    g_xboxRenderStateTable = 0x00175628u;

    // Every D3D8 entry point is ours now, so the backend is the only implementation there is.
    g_gfxBackend = GFX_BACKEND_D3D9;

    for (size_t i = 0; i < sizeof(g_replacements) / sizeof(g_replacements[0]); i++)
        XbeSeam_Replace(&g_seam, g_replacements[i].name, g_replacements[i].replacement,
                        g_replacements[i].stackBytes);

    XbeSeam_Install(&g_seam);
}
