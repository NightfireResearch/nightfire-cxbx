#include "d3dSeam.h"

#include "../../common/gfx/d3d9Backend.h"
#include "../../common/standalone.h"
#include "../../common/xbeEntrySeam.h"
#include "../../common/xbeProfiler.h"
#include "../../common/xboxPath.h"   // XGWriteSurfaceToFile

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
#include "d3d8EntriesUnnamed.inc"
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

// The game's tick rate, as TIMER_init (0x0010ae50) left it: 50 on a PAL video mode, 60 on an NTSC one.
#define GameTimerFrequency (*(const uint32_t *)0x00242424u)

// ---------------------------------------------------------------------------------------------------------------
// The state D3D8 would have started with.
//
// D3D8 keeps its deferred texture stage and render states in two tables inside the XBE, and the backend reads
// them back at draw time - it is the game's own state, so there is no second copy to keep in step. Both start
// as zeros in the image and are filled in by Direct3D_CreateDevice, which this seam replaces. Nothing put
// them back, so every stage read as COLOROP = 0, which the backend maps to DISABLE, and *everything drew
// untextured*: the level, the HUD, and the menu font, whose glyph quads came out as blocks of flat colour.
//
// So the defaults go in here, where the device is created, which is where they went before. They are D3D8's
// own: stage 0 modulates the texture with what came before, the other three are off, and every stage wraps
// and filters linearly.
//
// This matters more than it should because the game uses pixel shaders for most materials. With a shader
// bound the NV2A ignores the stage states entirely, so the values here are what the fixed-function fallback
// draws with until the combiner translator exists - and "modulate the texture by the vertex colour" is what
// most of those shaders amount to.
// ---------------------------------------------------------------------------------------------------------------

// Xbox X_D3DTSS_* indices into a stage's 32 words, and the X_D3DTOP_/X_D3DTA_ values the defaults use. Same
// numbering as PC D3D8, which is what lets the backend pass them through.
enum { TSS_ADDRESSU = 0, TSS_ADDRESSV = 1, TSS_ADDRESSW = 2, TSS_MAGFILTER = 3, TSS_MINFILTER = 4,
       TSS_MIPFILTER = 5, TSS_MIPMAPLODBIAS = 6, TSS_COLOROP = 12, TSS_COLORARG0 = 13, TSS_COLORARG1 = 14,
       TSS_COLORARG2 = 15, TSS_ALPHAOP = 16, TSS_ALPHAARG0 = 17, TSS_ALPHAARG1 = 18, TSS_ALPHAARG2 = 19,
       TSS_TEXCOORDINDEX = 28 };
enum { TOP_DISABLE = 1, TOP_SELECTARG1 = 2, TOP_MODULATE = 4 };
enum { TA_DIFFUSE = 0, TA_CURRENT = 1, TA_TEXTURE = 2 };
enum { TADDRESS_WRAP = 1, TEXF_POINT = 1, TEXF_LINEAR = 2 };

#define TEXTURE_STAGE_WORDS 32

static void InitialiseD3D8State(void) {
    if (g_xboxTextureStateTable == 0)
        return;

    for (uint32_t stage = 0; stage < 4; stage++) {
        uint32_t *state = (uint32_t *)(g_xboxTextureStateTable + stage * TEXTURE_STAGE_WORDS * 4);

        state[TSS_ADDRESSU] = TADDRESS_WRAP;
        state[TSS_ADDRESSV] = TADDRESS_WRAP;
        state[TSS_ADDRESSW] = TADDRESS_WRAP;
        state[TSS_MAGFILTER] = TEXF_LINEAR;
        state[TSS_MINFILTER] = TEXF_LINEAR;
        state[TSS_MIPFILTER] = TEXF_POINT;
        state[TSS_MIPMAPLODBIAS] = 0;
        state[TSS_TEXCOORDINDEX] = stage;

        // Stage 0 samples and modulates; the rest are off until something turns them on.
        state[TSS_COLOROP] = (stage == 0) ? TOP_MODULATE : TOP_DISABLE;
        state[TSS_COLORARG1] = TA_TEXTURE;
        state[TSS_COLORARG2] = TA_CURRENT;
        state[TSS_ALPHAOP] = (stage == 0) ? TOP_SELECTARG1 : TOP_DISABLE;
        state[TSS_ALPHAARG1] = TA_TEXTURE;
        state[TSS_ALPHAARG2] = TA_CURRENT;
    }
}

static uint32_t __stdcall Seam_Direct3D_CreateDevice(uint32_t adapter, uint32_t deviceType, void *focusWindow,
                                                     uint32_t behaviourFlags, void *presentationParameters,
                                                     void **returnedDevice) {
    // The backend paces Present to the refresh rate in the present parameters, standing in for the vertical
    // blank an Xbox Swap would have waited for. EAGL leaves that field zero unless the game asked for a
    // particular rate, which would mean no pacing at all - so where it is zero, the video mode's own rate is
    // what it means, and that is the rate TIMER_init already derived.
    uint32_t *parameters = (uint32_t *)presentationParameters;
    // Word 4 is MultiSampleType. The NV2A's visibility tests count samples, not pixels, so the lens flare's
    // arithmetic depends on it (RLensFlareManager::DrawFlares, plan section 3).
    if (parameters != NULL)
        printf("[d3dSeam] device %ux%u, format 0x%x, multisample type 0x%x, flags 0x%x\n",
               parameters[0], parameters[1], parameters[2], parameters[4], parameters[10]);
    if (parameters != NULL && parameters[11] == 0 && GameTimerFrequency != 0) {
        parameters[11] = GameTimerFrequency;
        printf("[d3dSeam] no refresh rate in the present parameters; pacing to the game's %u Hz tick\n",
               GameTimerFrequency);
    }

    uint32_t result = D3D9_CreateDevice(adapter, deviceType, focusWindow, behaviourFlags,
                                        parameters, returnedDevice);
    InitialiseD3D8State();
    return result;
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
    Profiler_Frame("the frame just presented");   // does nothing unless settings.ini says Profile=on
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

// ---------------------------------------------------------------------------------------------------------------
// The deferred render states EAGL sets through their own entry points. Each original stores the value in its
// slot of D3D8's render-state table and pushes the matching NV2A methods; the slots are read out of the
// originals (the one store each makes into the table at 0x00175628). A few also refresh a derived slot that
// only feeds the push buffer (139 for BackFillMode and TwoSidedLighting, 142 for FrontFace), and MultiSampleAntiAlias
// recomputes the multisample setup; none of that has a reader here. The backend reads none of these slots
// either - it reads only the fog states - so storing the value keeps the table as the game expects to find it
// and changes nothing on screen, which is what running the originals did.
// ---------------------------------------------------------------------------------------------------------------

static void StoreDeferredRenderState(unsigned slot, uint32_t value) {
    if (g_xboxRenderStateTable != 0)
        ((uint32_t *)g_xboxRenderStateTable)[slot] = value;
}

#define DEFERRED_RENDER_STATE(name, slot)                                       \
    static void __stdcall Seam_D3DDevice_SetRenderState_##name(uint32_t value) { \
        StoreDeferredRenderState(slot, value);                                  \
    }
DEFERRED_RENDER_STATE(PSTextureModes, 136)                  // 0x001673b0
DEFERRED_RENDER_STATE(VertexBlend, 137)                     // 0x00167bf0
DEFERRED_RENDER_STATE(BackFillMode, 140)                    // 0x00167b20
DEFERRED_RENDER_STATE(TwoSidedLighting, 141)                // 0x00167b80
DEFERRED_RENDER_STATE(NormalizeNormals, 142)                // 0x00167860
DEFERRED_RENDER_STATE(FrontFace, 146)                       // 0x00167820
DEFERRED_RENDER_STATE(ZBias, 149)                           // 0x001679f0
DEFERRED_RENDER_STATE(LogicOp, 150)                         // 0x00167a70
DEFERRED_RENDER_STATE(EdgeAntiAlias, 151)                   // 0x001676e0
DEFERRED_RENDER_STATE(MultiSampleAntiAlias, 152)            // 0x00168b70
DEFERRED_RENDER_STATE(MultiSampleMask, 153)                 // 0x00168bf0
DEFERRED_RENDER_STATE(MultiSampleMode, 154)                 // 0x00168af0
DEFERRED_RENDER_STATE(MultiSampleRenderTargetMode, 155)     // 0x00168b30
DEFERRED_RENDER_STATE(SampleAlpha, 158)                     // 0x00168c40
DEFERRED_RENDER_STATE(OcclusionCullEnable, 161)             // 0x001689b0
DEFERRED_RENDER_STATE(StencilCullEnable, 162)               // 0x00168a20
DEFERRED_RENDER_STATE(RopZCmpAlwaysRead, 163)               // 0x00168a90
DEFERRED_RENDER_STATE(RopZRead, 164)                        // 0x00168ab0
DEFERRED_RENDER_STATE(DoNotCullUncompressed, 165)           // 0x00168ad0
#undef DEFERRED_RENDER_STATE

// The index buffer for indexed draws. The original records it (with a reference) and the base vertex index in
// the device; this engine's draws are handed their index data directly (D3DDevice_DrawIndexedVertices), so the
// backend's note of it is all that is needed.
static void __stdcall Seam_D3DDevice_SetIndices(void *indexBuffer, uint32_t baseVertexIndex) {
    D3D9_SetIndices(indexBuffer, baseVertexIndex);
}

static void __stdcall Seam_D3DDevice_SetGammaRamp(uint32_t flags, void *ramp) {
    D3D9_SetGammaRamp(flags, ramp);
}

// Whether the GPU is still using a resource: never, here - the backend copies what it reads at the draw.
static uint32_t __stdcall Seam_D3DResource_IsBusy(void *resource) {
    (void)resource;
    return 0;
}

// An index buffer: as the original (0x0016b430) makes it, one block of 12 header bytes followed by the indices -
// Common = 0x01010001 (an index buffer, one reference), Data = the indices, Lock = 0. The original takes the
// block from the XAPI heap; Release never frees an index buffer (the backend frees only the surfaces it made),
// so neither did the original path.
static void *__stdcall Seam_D3DDevice_CreateIndexBuffer2(uint32_t length) {
    uint32_t *buffer = (uint32_t *)calloc(1, 12 + length);
    if (buffer == NULL) {
        printf("[d3dSeam] out of memory for a %u-byte index buffer\n", length);
        return NULL;
    }
    buffer[0] = 0x01010001u;
    buffer[1] = (uint32_t)(uintptr_t)(buffer + 3);
    buffer[2] = 0;
    return buffer;
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

    {   // Each distinct object the game locks, once: it is how a read of something the seam stands in for
        // gets noticed, since a stand-in has no pixels to give.
        static const void *seen[64]; static unsigned seenCount = 0;
        bool known = false;
        for (unsigned i = 0; i < seenCount; i++) if (seen[i] == container) known = true;
        if (!known && seenCount < 64) {
            seen[seenCount++] = container;
            const XboxPixelContainer *h = (const XboxPixelContainer *)container;
            printf("[d3dSeam] lock of %p (common %08x data %08x format %08x size %08x)%s\n", container, h->Common,
                   h->Data, h->Format, h->Size, D3D9_IsStandInSurface(container) ? " - a stand-in" : "");
        }
    }

    uint32_t format = 0, width = 0, height = 0;
    D3D9_GetSurfaceDesc(container, &format, &width, &height);

    const XboxPixelContainer *header = (const XboxPixelContainer *)container;
    lockedRect->Pitch = (((width * XboxFormatBits(format)) / 8) + 63) & ~63u;
    if (D3D9_IsStandInSurface(container)) {
        lockedRect->pBits = ScratchForStandIn(container, lockedRect->Pitch * height);
        // A lock of the backbuffer is the game about to read the scene - the pause menu copies it to blur
        // behind its panel - so the scene is fetched into the scratch first. Anything else stays zeros.
        if (lockedRect->pBits != NULL && container == D3D9_GetBackBuffer2(0))
            D3D9_ReadBackBuffer(lockedRect->pBits, lockedRect->Pitch, width, height);
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
// Pixel shaders.
//
// NV2A register combiner definitions, which the backend translates (common/gfx/nv2aPixelShader.h). The
// handle the game gets back is the backend's tag; the original's is a pointer to a copy of the definition,
// and the only reader of that pointer is SetPixelShader, which is replaced too. The constants arrive as
// float4s, which the original packs to bytes and writes into the stages whose mapping nibble names them -
// the backend does the same at draw time.
// ---------------------------------------------------------------------------------------------------------------

static uint32_t __stdcall Seam_D3DDevice_CreatePixelShader(const void *definition, uint32_t *handleOut) {
    return D3D9_CreatePixelShader(definition, handleOut);
}

static void __stdcall Seam_D3DDevice_SetPixelShader(uint32_t handle) {
    D3D9_SetPixelShader(handle);
}

static void __stdcall Seam_D3DDevice_SetPixelShaderConstant(uint32_t reg, const void *values, uint32_t count) {
    D3D9_SetPixelShaderConstant(reg, (const float *)values, count);
}

static void __stdcall Seam_D3DDevice_DeletePixelShader(uint32_t handle) {
    D3D9_DeletePixelShader(handle);
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
// Library functions EAGL reaches that Ghidra has no name for (d3d8EntriesUnnamed.inc).
// ---------------------------------------------------------------------------------------------------------------

// 0x00169450: MOV EAX, 1 / RET 4. EAGL::Device::Init calls it with 0 and keeps the answer at 0x0023ff14. Name
// invented: the original is a constant, so what it once asked is not recoverable from this build.
static uint32_t __stdcall Seam_D3D_ReturnsTrue(uint32_t unused) {
    (void)unused;
    return 1;
}

// XGBytesPerPixelFromFormat (0x00178fb8): the original's jump table, read out of the XBE (0x00178fe2 and the
// index bytes at 0x00178ff2). DXT3 and DXT5 count as one byte a pixel and DXT1 as none, as there.
static uint32_t __stdcall Seam_XGBytesPerPixelFromFormat(uint32_t format) {
    switch (format) {
        case 0x06: case 0x07: case 0x12: case 0x1e: case 0x24: case 0x25: case 0x2a: case 0x2b: case 0x2e:
        case 0x2f: case 0x33: case 0x3a: case 0x3b: case 0x3c: case 0x3f: case 0x40: case 0x41:
            return 4;
        case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11: case 0x16: case 0x17: case 0x1a:
        case 0x1c: case 0x1d: case 0x20: case 0x27: case 0x28: case 0x29: case 0x2c: case 0x2d: case 0x30:
        case 0x31: case 0x32: case 0x35: case 0x37: case 0x38: case 0x39: case 0x3d: case 0x3e:
            return 2;
        case 0x00: case 0x01: case 0x0b: case 0x0e: case 0x0f: case 0x13: case 0x19: case 0x1b: case 0x1f:
            return 1;
        default:
            return 0;
    }
}

// D3DXLoadSurfaceFromMemory (0x0015d3bd; ten arguments, D3DERR_INVALIDCALL for a missing surface, source or
// source rectangle). EAGL's one caller (FUN_000eb910) copies a rectangle of image data into each mip level of
// a texture: no palettes, D3DX_FILTER_NONE (1), no colour key. With no filter, D3DX copies pixel for pixel and
// clips to the smaller rectangle, so that is what this does - into the surface's swizzled layout when its
// format is swizzled, or block rows for a compressed one. A conversion between formats of different sizes, or
// a filter, is what the original's sixty-odd helpers did and nothing here has needed; it is reported once.
#define D3DERR_INVALIDCALL_ 0x8876086Cu

static uint32_t __stdcall Seam_D3DXLoadSurfaceFromMemory(void *destSurface, const void *destPalette,
                                                         const XboxRect *destRect, const void *source,
                                                         uint32_t sourceFormat, uint32_t sourcePitch,
                                                         const void *sourcePalette, const XboxRect *sourceRect,
                                                         uint32_t filter, uint32_t colorKey) {
    (void)destPalette; (void)sourcePalette; (void)colorKey;
    if (destSurface == NULL || source == NULL || sourceRect == NULL)
        return D3DERR_INVALIDCALL_;

    XboxSurfaceDesc desc;
    FillSurfaceDesc(destSurface, &desc);
    static bool first = true;
    if (first) {
        first = false;
        printf("[d3dSeam] D3DXLoadSurfaceFromMemory: first call, format 0x%x into a %ux%u surface of format 0x%x\n",
               sourceFormat, desc.Width, desc.Height, desc.Format);
    }
    uint32_t destBits = XboxFormatBits(desc.Format), sourceBits = XboxFormatBits(sourceFormat);
    if (destBits != sourceBits || (filter & 0xFF) > 1) {
        static bool said = false;
        if (!said) {
            said = true;
            printf("[d3dSeam] D3DXLoadSurfaceFromMemory: format 0x%x into 0x%x, filter 0x%x is not handled\n",
                   sourceFormat, desc.Format, filter);
        }
        return D3DERR_INVALIDCALL_;
    }

    XboxRect dest = { 0, 0, (int32_t)desc.Width, (int32_t)desc.Height };
    if (destRect != NULL)
        dest = *destRect;
    uint32_t width = (uint32_t)(sourceRect->right - sourceRect->left);
    uint32_t height = (uint32_t)(sourceRect->bottom - sourceRect->top);
    if ((uint32_t)(dest.right - dest.left) < width)  width = (uint32_t)(dest.right - dest.left);
    if ((uint32_t)(dest.bottom - dest.top) < height) height = (uint32_t)(dest.bottom - dest.top);

    XboxLockedRect locked;
    LockPixels(destSurface, &locked);
    if (locked.pBits == NULL)
        return D3DERR_INVALIDCALL_;
    const uint8_t *src = (const uint8_t *)source;
    uint8_t *dst = (uint8_t *)locked.pBits;
    uint32_t f = desc.Format & 0xFF;

    if (f == 0x0c || f == 0x0e || f == 0x0f) {
        // Compressed: 4x4 blocks, 8 bytes each for DXT1 and 16 for DXT3/5, a row of blocks at a time
        uint32_t blockBytes = (f == 0x0c) ? 8 : 16;
        uint32_t destPitch = ((desc.Width + 3) / 4) * blockBytes;
        for (uint32_t by = 0; by < (height + 3) / 4; by++)
            memcpy(dst + (size_t)(dest.top / 4 + by) * destPitch + (size_t)(dest.left / 4) * blockBytes,
                   src + (size_t)(sourceRect->top / 4 + by) * sourcePitch + (size_t)(sourceRect->left / 4) * blockBytes,
                   (size_t)((width + 3) / 4) * blockBytes);
        return 0;
    }

    uint32_t bpp = destBits / 8;
    if (!Seam_XGIsSwizzledFormat(desc.Format)) {
        for (uint32_t y = 0; y < height; y++)
            memcpy(dst + (size_t)(dest.top + y) * locked.Pitch + (size_t)dest.left * bpp,
                   src + (size_t)(sourceRect->top + y) * sourcePitch + (size_t)sourceRect->left * bpp,
                   (size_t)width * bpp);
        return 0;
    }

    uint32_t maskX = 0, maskY = 0;
    SwizzleMasks(desc.Width, desc.Height, &maskX, &maskY);
    uint32_t yOffset = 0;
    for (int32_t y = 0; y < dest.top; y++)
        yOffset = (yOffset - maskY) & maskY;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t xOffset = 0;
        for (int32_t x = 0; x < dest.left; x++)
            xOffset = (xOffset - maskX) & maskX;
        const uint8_t *row = src + (size_t)(sourceRect->top + y) * sourcePitch + (size_t)sourceRect->left * bpp;
        for (uint32_t x = 0; x < width; x++) {
            memcpy(dst + (size_t)(yOffset | xOffset) * bpp, row + (size_t)x * bpp, bpp);
            xOffset = (xOffset - maskX) & maskX;
        }
        yOffset = (yOffset - maskY) & maskY;
    }
    return 0;
}

// XGWriteSurfaceToFile (0x0017a8ee): a surface as a 24-bit BMP, for EAGL's screenshot. As the original: only
// the four linear formats it knows (R5G6B5 0x11, A8R8G8B8 0x12, X1R5G5B5 0x1c, X8R8G8B8 0x1e), rows bottom
// up, each pixel's bytes as it took them - and rows not padded to four bytes, nor the header's size counting
// any padding, so a width that is not a multiple of four makes a file other programs misread, as it did.
static uint32_t __stdcall Seam_XGWriteSurfaceToFile(void *surface, const char *xboxPath) {
    XboxSurfaceDesc desc;
    FillSurfaceDesc(surface, &desc);
    uint32_t f = desc.Format;
    if (f != 0x11 && f != 0x12 && f != 0x1c && f != 0x1e)
        return 0x80004005u;   // E_FAIL

    char hostPath[260];
    if (xboxPath == NULL || !Xbox_ResolvePath(xboxPath, hostPath, sizeof(hostPath)))
        return 0x80004005u;
    FILE *file = fopen(hostPath, "wb");
    if (file == NULL) {
        printf("[d3dSeam] Unable to open file %s\n", hostPath);
        return 0x80004005u;
    }

    uint32_t imageBytes = desc.Width * desc.Height * 3;
    uint8_t header[0x36] = { 'B', 'M' };
    uint32_t fileSize = imageBytes + 0x36;
    memcpy(header + 2, &fileSize, 4);
    header[10] = 0x36;
    header[14] = 0x28;                                   // BITMAPINFOHEADER
    memcpy(header + 18, &desc.Width, 4);
    memcpy(header + 22, &desc.Height, 4);
    header[26] = 1;                                      // planes
    header[28] = 24;                                     // bits a pixel
    memcpy(header + 34, &imageBytes, 4);
    fwrite(header, 1, sizeof(header), file);

    XboxLockedRect locked;
    LockPixels(surface, &locked);
    for (int32_t y = (int32_t)desc.Height - 1; y >= 0 && locked.pBits != NULL; y--) {
        const uint8_t *row = (const uint8_t *)locked.pBits + (size_t)y * locked.Pitch;
        for (uint32_t x = 0; x < desc.Width; x++) {
            uint8_t out[3];
            if (f == 0x11 || f == 0x1c) {
                uint16_t p = ((const uint16_t *)row)[x];
                out[0] = (uint8_t)(p << 3);
                out[1] = (f == 0x11) ? (uint8_t)((p >> 3) & 0xfc) : (uint8_t)((p >> 2) & 0xf8);
                out[2] = (f == 0x11) ? (uint8_t)((p >> 8) & 0xf8) : (uint8_t)((p >> 7) & 0xf8);
            } else {
                memcpy(out, row + (size_t)x * 4, 3);
            }
            fwrite(out, 1, 3, file);
        }
    }
    fclose(file);
    return 0;
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

// A vertex buffer header the game wraps around memory it already has, which the original writes as three
// words: Common = 1 (a vertex buffer, one reference), Data, Lock = 0. The one caller (FUN_000f6d50) passes
// its pointer minus 0x80000000 - on the console that turns the uncached alias of a block into the physical
// address the GPU wants, and on a Win32 pointer, which has no bit 31 to take away, it *sets* the bit instead.
// So the plan's "EAGL never touches bit 31" was one site short. The bit is masked off here, which is right
// either way: a real alias loses it and a plain pointer gets it back. Note that the buffer this makes is
// three words long - there is no word 5 for the backend to read a size from, and it does not need one, since
// every draw here knows its own extent.
static void __stdcall Seam_XGSetVertexBufferHeader(uint32_t length, uint32_t usage, uint32_t fvf, uint32_t pool,
                                                   uint32_t *buffer, uint32_t data) {
    (void)length; (void)usage; (void)fvf; (void)pool;
    if (buffer == NULL)
        return;
    buffer[0] = XBOX_VERTEXBUFFER_COMMON;
    buffer[1] = data & 0x7FFFFFFFu;
    buffer[2] = 0;
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

// Visibility tests, which gate the lens flares - the red lights on mines, projectiles and door nodes. The
// backend answers them with occlusion queries; see the notes there. GetVisibilityTestResult's result is a
// pointer to a UINT and the timestamp a pointer to a ULONGLONG, both optional.
static void __stdcall Seam_D3DDevice_BeginVisibilityTest(void) {
    D3D9_BeginVisibilityTest();
}

static void __stdcall Seam_D3DDevice_EndVisibilityTest(uint32_t index) {
    D3D9_EndVisibilityTest(index);
}

static uint32_t __stdcall Seam_D3DDevice_GetVisibilityTestResult(uint32_t index, uint32_t *result,
                                                                 uint64_t *timeStamp) {
    return D3D9_GetVisibilityTestResult(index, result, timeStamp);
}

// Wireframe and point fill modes are a debug feature the game does not use in anger, and D3D9 has them
// through a render state the backend does not expose yet. Accepting and ignoring it keeps solid fill.
static void __stdcall Seam_D3DDevice_SetRenderState_FillMode(uint32_t fillMode) {
    (void)fillMode;
}

// Bump-environment matrices and luminance, for the BUMPENVMAP texture modes. The original (0x00167d50)
// writes the value into the deferred texture state table at the type's index and pushes it; the backend
// reads the table when it binds a translated pixel shader, so writing it is the whole job here.
static void __stdcall Seam_D3DDevice_SetTextureState_BumpEnv(uint32_t stage, uint32_t type, uint32_t value) {
    if (g_xboxTextureStateTable == 0 || stage >= 4 || type >= TEXTURE_STAGE_WORDS)
        return;
    *(uint32_t *)(g_xboxTextureStateTable + stage * TEXTURE_STAGE_WORDS * 4 + type * 4) = value;
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
    { "XGSetVertexBufferHeader",              (void *)Seam_XGSetVertexBufferHeader, 24 },
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
    { "D3DDevice_BeginVisibilityTest",        (void *)Seam_D3DDevice_BeginVisibilityTest, 0 },
    { "D3DDevice_EndVisibilityTest",          (void *)Seam_D3DDevice_EndVisibilityTest, 4 },
    { "D3DDevice_GetVisibilityTestResult",    (void *)Seam_D3DDevice_GetVisibilityTestResult, 12 },
    { "D3DDevice_CreateTexture2",             (void *)Seam_D3DDevice_CreateTexture2, 28 },
    { "D3DTexture_LockRect",                  (void *)Seam_D3DTexture_LockRect, 20 },
    { "D3DSurface_LockRect",                  (void *)Seam_D3DSurface_LockRect, 16 },
    { "D3DDevice_CreatePalette2",             (void *)Seam_D3DDevice_CreatePalette2, 4 },
    { "D3DPalette_Lock2",                     (void *)Seam_D3DPalette_Lock2, 8 },
    { "D3DDevice_SetPalette",                 (void *)Seam_D3DDevice_SetPalette, 8 },
    { "D3D_CreateStandAloneSurface",          (void *)Seam_D3D_CreateStandAloneSurface, 16 },
    { "D3DDevice_Begin",                      (void *)Seam_D3DDevice_Begin, 4 },
    { "D3DDevice_CreatePixelShader",          (void *)Seam_D3DDevice_CreatePixelShader, 8 },
    { "D3DDevice_SetPixelShader",             (void *)Seam_D3DDevice_SetPixelShader, 4 },
    { "D3DDevice_SetPixelShaderConstant",     (void *)Seam_D3DDevice_SetPixelShaderConstant, 12 },
    { "D3DDevice_DeletePixelShader",          (void *)Seam_D3DDevice_DeletePixelShader, 4 },
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
    { "D3DDevice_SetRenderState_PSTextureModes", (void *)Seam_D3DDevice_SetRenderState_PSTextureModes, 4 },
    { "D3DDevice_SetRenderState_VertexBlend", (void *)Seam_D3DDevice_SetRenderState_VertexBlend, 4 },
    { "D3DDevice_SetRenderState_BackFillMode", (void *)Seam_D3DDevice_SetRenderState_BackFillMode, 4 },
    { "D3DDevice_SetRenderState_TwoSidedLighting", (void *)Seam_D3DDevice_SetRenderState_TwoSidedLighting, 4 },
    { "D3DDevice_SetRenderState_NormalizeNormals", (void *)Seam_D3DDevice_SetRenderState_NormalizeNormals, 4 },
    { "D3DDevice_SetRenderState_FrontFace",   (void *)Seam_D3DDevice_SetRenderState_FrontFace, 4 },
    { "D3DDevice_SetRenderState_ZBias",       (void *)Seam_D3DDevice_SetRenderState_ZBias, 4 },
    { "D3DDevice_SetRenderState_LogicOp",     (void *)Seam_D3DDevice_SetRenderState_LogicOp, 4 },
    { "D3DDevice_SetRenderState_EdgeAntiAlias", (void *)Seam_D3DDevice_SetRenderState_EdgeAntiAlias, 4 },
    { "D3DDevice_SetRenderState_MultiSampleAntiAlias", (void *)Seam_D3DDevice_SetRenderState_MultiSampleAntiAlias, 4 },
    { "D3DDevice_SetRenderState_MultiSampleMask", (void *)Seam_D3DDevice_SetRenderState_MultiSampleMask, 4 },
    { "D3DDevice_SetRenderState_MultiSampleMode", (void *)Seam_D3DDevice_SetRenderState_MultiSampleMode, 4 },
    { "D3DDevice_SetRenderState_MultiSampleRenderTargetMode", (void *)Seam_D3DDevice_SetRenderState_MultiSampleRenderTargetMode, 4 },
    { "D3DDevice_SetRenderState_SampleAlpha", (void *)Seam_D3DDevice_SetRenderState_SampleAlpha, 4 },
    { "D3DDevice_SetRenderState_OcclusionCullEnable", (void *)Seam_D3DDevice_SetRenderState_OcclusionCullEnable, 4 },
    { "D3DDevice_SetRenderState_StencilCullEnable", (void *)Seam_D3DDevice_SetRenderState_StencilCullEnable, 4 },
    { "D3DDevice_SetRenderState_RopZCmpAlwaysRead", (void *)Seam_D3DDevice_SetRenderState_RopZCmpAlwaysRead, 4 },
    { "D3DDevice_SetRenderState_RopZRead",    (void *)Seam_D3DDevice_SetRenderState_RopZRead, 4 },
    { "D3DDevice_SetRenderState_DoNotCullUncompressed", (void *)Seam_D3DDevice_SetRenderState_DoNotCullUncompressed, 4 },
    { "D3DDevice_SetIndices",                 (void *)Seam_D3DDevice_SetIndices, 8 },
    { "D3DDevice_SetGammaRamp",               (void *)Seam_D3DDevice_SetGammaRamp, 8 },
    { "D3DResource_IsBusy",                   (void *)Seam_D3DResource_IsBusy, 4 },
    { "D3DDevice_CreateIndexBuffer2",         (void *)Seam_D3DDevice_CreateIndexBuffer2, 4 },
    { "D3DXLoadSurfaceFromMemory",            (void *)Seam_D3DXLoadSurfaceFromMemory, 40 },
    { "D3D_ReturnsTrue",                      (void *)Seam_D3D_ReturnsTrue, 4 },
    { "XGBytesPerPixelFromFormat",            (void *)Seam_XGBytesPerPixelFromFormat, 4 },
    { "XGWriteSurfaceToFile",                 (void *)Seam_XGWriteSurfaceToFile, 8 },
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

    // EAGL rewrites its vertex buffers between draws - its dynamic buffer is three Xbox buffers behind one
    // object, refilled per draw - and nothing here is told. Every draw copies what it reads, at the draw.
    g_streamsVolatile = true;

    // Every D3D8 entry point is ours now, so the backend is the only implementation there is.
    g_gfxBackend = GFX_BACKEND_D3D9;

    for (size_t i = 0; i < sizeof(g_replacements) / sizeof(g_replacements[0]); i++)
        XbeSeam_Replace(&g_seam, g_replacements[i].name, g_replacements[i].replacement,
                        g_replacements[i].stackBytes);

    XbeSeam_Install(&g_seam);
}
