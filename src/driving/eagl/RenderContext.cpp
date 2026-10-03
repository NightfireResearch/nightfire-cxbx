#include "RenderContext.h"
#include "../platform/XboxXapi.h"
#include "Profiler.h"
#include "RenderMethod.h"
#include "Tar.h"
#include "View.h"
#include "D3D8State.h"
#include "EaglGlobals.h"
#include "EaglOriginals.h"
#include "../platform/RealPrint.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#include <bit>
#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL::RenderContext, RenderContextExtension and RenderContextPrivate (docs/driving/eagl.md 2.2, 3.2, 4.2): every
// code entry point in 0x000e6610..0x000e8900, including the getters that sit after their setters without a
// Ghidra function and the unreferenced debug routines (the screenshot, the back-buffer read, the gamma setters).
//
// This code runs every frame. Each function makes the original's D3D8 calls (to the seam, ../gfx/D3D8.h) in the
// original's order with the original's arguments, and writes D3D8's own
// render-state table and dirty flags (D3D8State.h) where the original's inlined D3D8 code did, in the same order
// relative to the calls (5.2, 8.1). EndFrame's re-send of ~40 states after Swap (8.2) is kept statement for
// statement, including the redundant dirty-flag writes.
//
// Every state write is gated on the device existing (D3DDevicePointer), as in the original. The extension's
// shadows (D3D8State.h) are what it last sent; GeoPrimState::Apply reads the Z-write one.
//
// The destructor's SEH frame (handler 0x00154c58) is left out: nothing in it can throw.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

using EAGL::D3DPixelContainer;
using EAGL::Device;
using EAGL::RenderContext;
using EAGL::RenderContextExtension;
using EAGL::RenderMethod;
using EAGL::SurfaceTexture;

// ---- globals

#define RenderTargetTexture PTR_AT(0x0023ff1c)
#define GammaRamp (*(D3DGammaRamp *)0x0023fc08)       // what GetGammaRamp read at start-up
#define SavedDither U8_AT(0x0023ff10)                 // EndFrame puts it in the specular-enable slot
#define SavedFog U8_AT(0x0023ff11)
#define CurrentPixelShader U32_AT(0x00240474)
#define CurrentVertexShader U32_AT(0x00240478)
#define D3DTextureNames ((const char (*)[12])0x001cb964)   // six copies of "D3DTexture", one per call site

namespace {

// D3DPRESENT_PARAMETERS' presentation interval for a swap interval: 1 -> 0, 2 -> 2, 3 -> 4, anything else 0.
uint32_t PresentationInterval(int swapInterval) {
    switch (swapInterval) {
    case 2: return 2;
    case 3: return 4;
    default: return 0;
    }
}

// The texture header laid over a surface, as SetupFrameBuffers and the three buffer getters build it: allocated
// under the given name (each site passes its own copy of "D3DTexture"), cleared, given the surface's size and
// format (the depth surface's format mapped to its texture equivalent), and pointed at the surface's data.
void BuildSurfaceTexture(SurfaceTexture **slot, D3DPixelContainer *surface, const char *name, bool depth) {
    SurfaceTexture *texture = static_cast<SurfaceTexture *>(EaglMalloc(sizeof(SurfaceTexture), name));
    *slot = texture;
    texture->common = 0;
    texture->data = 0;
    texture->lock = 0;
    texture->format = 0;
    texture->size = 0;
    D3DSurfaceDesc desc;
    D3DSurface_GetDesc(surface, &desc);
    uint32_t format = desc.format;
    if (depth) {
        switch (format) {   // the switch tables at 0x000e7378 and 0x000e853c
        case 0x2a: format = 6; desc.format = format; break;
        case 0x2c: format = 5; desc.format = format; break;
        case 0x2e: format = 0x12; desc.format = format; break;
        case 0x30: format = 0x11; desc.format = format; break;
        default: break;
        }
    }
    uint32_t pitch = XGBytesPerPixelFromFormat(format) * desc.width;
    XGSetTextureHeader(desc.width, desc.height, 1, 0, desc.format, 0, *slot, 0, pitch);
    (*slot)->data = surface->data;
}

// One gamma-ramp entry scaled: (int)(entry * scale + 0.5) by __ftol2, 0xff when it overflows a byte.
uint8_t ScaledGamma(uint8_t entry, float scale) {
    int32_t v = Ftol(double(entry) * scale + 0.5);
    if ((v & 0xff000000) != 0)
        v = 0xff;
    return uint8_t(v);
}

}  // namespace

// =================================================================================================================
// The frame
// =================================================================================================================

// FUNC_AT(0x000e6610)
void EAGL::RenderContext::BeginFrame() {
    Device *d = Device::Get();
    d->Private()->SetCurrentRenderContext(this);
    Device::Get();   // the original's call, its answer unused
    D3DDevice_SetRenderTarget(backBuffer, depthSurface);
}

// Presents the frame, then sends again the states Swap leaves behind (8.2). Returns Device::Get(), the tail call.
// FUNC_AT(0x000e6640)
void* EAGL::RenderContext::EndFrame() {
    ProfilerFrameMark();

    // Unbind whatever is still bound from the render-target texture
    for (uint32_t stage = 0; stage < 4; stage++) {
        if (StageTexture[stage] == RenderTargetTexture) {
            D3DDevice_SetTexture(stage, NULL);
            StageTexture[stage] = NULL;
        }
    }
    if (multiSampleType != 0x11) {   // polygon offset off for the swap
        SendState(0x40384, kRsPolygonOffsetZSlopeScale, 0);
        SendState(0x40388, kRsPolygonOffsetZOffset, 0);
    }
    Device::Get();
    D3DDevice_SetShaderConstantMode(0);
    D3DDevice_Swap(0);
    D3DDevice_SetShaderConstantMode(1);

    if (multiSampleType != 0x11) {
        uint32_t savedDither = SavedDither, savedFog = SavedFog;
        D3DDirtyFlags |= 0x3000;
        uint32_t dirty = D3DDirtyFlags;
        D3DRenderState[kRsSpecularEnable] = savedDither;   // slot 103, as the original (dither is slot 65)
        D3DRenderState[kRsFogEnable] = savedFog;
        dirty |= 0x2000;
        ApplyCache.fillMode = 0xffffffff;
        ApplyCache.cullEnable = 0xffffffff;
        ApplyCache.cullDirection = 0xffffffff;
        ApplyCache.alphaTestEnable = 0xffffffff;
        ApplyCache.transparencyMethod = 0xffffffff;
        D3DDirtyFlags = dirty;
        dirty |= 0x2000;
        D3DRenderState[kRsFogTableMode] = fogTableMode;
        D3DDirtyFlags = dirty;
        D3DRenderState[kRsFogStart] = fogStart;
        dirty |= 0x2000;
        D3DDirtyFlags = dirty;
        D3DRenderState[kRsFogEnd] = fogEnd;
        dirty |= 0x2000;
        D3DDirtyFlags = dirty;
        D3DRenderState[kRsFogDensity] = fogDensity;
        D3DDevice_SetRenderState_FogColor(fogColour);
        D3DDevice_SetRenderState_StencilEnable(Shadows.stencilEnable);
        Shadows.colourWriteMask = 0xffffffff;
        D3DDevice_SetRenderState_YuvEnable(YuvEnable);
        ApplyCache.shading = 0xffffffff;
        D3DDevice_SetPixelShader(CurrentPixelShader);
        D3DDevice_SetVertexShader(CurrentVertexShader);

        SendState(0x4147c, kRsStippleEnable, 0);
        SendState(0x40384, kRsPolygonOffsetZSlopeScale, std::bit_cast<uint32_t>(ApplyCacheZSlopeScale));
        SendState(0x40388, kRsPolygonOffsetZOffset, std::bit_cast<uint32_t>(ApplyCacheZOffset));
        SendState(0x41d78, kRsDepthClipControl, 1);
        // FCOMP against 0.0f, TEST AH,0x44, JNP: the enables go off only when the offset is equal (and ordered)
        uint32_t on = ApplyCacheZOffset == 0.0f ? 0 : 1;
        SendState(0x40330, kRsPointOffsetEnable, on);
        SendState(0x40334, kRsWireFrameOffsetEnable, on);
        SendState(0x40338, kRsSolidOffsetEnable, on);
        SendState(0x409f8, kRsSwathWidth, 4);
        SendState(0x4034c, kRsBlendColor, ApplyCache.blendColour);
        SendState(0x40344, kRsSrcBlend, ApplyCache.blendSource);
        SendState(0x40348, kRsDestBlend, ApplyCache.blendDestination);
        SendState(0x40354, kRsZFunc, ApplyCache.depthTestMethod);
        SendState(0x4033c, kRsAlphaFunc, ApplyCache.alphaTestMethod);
        SendState(0x40340, kRsAlphaRef, ApplyCache.alphaCompareValue);
        SendState(0x4035c, kRsZWriteEnable, Shadows.zWritesEnable);
        SendState(0x40350, kRsBlendOp, ApplyCache.blendOperation);
        SendState(0x40374, kRsStencilZFail, Shadows.stencilZFail);
        SendState(0x40378, kRsStencilPass, Shadows.stencilZPass);
        SendState(0x40364, kRsStencilFunc, Shadows.stencilFunc);
        SendState(0x40368, kRsStencilRef, Shadows.stencilRef);
    }

    // The texture headers over the buffers follow the buffers' data, which Swap moved
    if (frontAlias != NULL)
        frontAlias->data = frontBuffer->data;
    if (backAlias != NULL)
        backAlias->data = backBuffer->data;
    if (depthAlias != NULL)
        depthAlias->data = depthSurface->data;
    return Device::Get();
}

// =================================================================================================================
// Sizes and buffer depths
// =================================================================================================================

// FUNC_AT(0x000e6a60)
void EAGL::RenderContext::SetSize(float width_, float height_) {
    width = Ftol(width_);
    height = Ftol(height_);
}

// What SetupFrameBuffers last set up, not what SetSize asked for.
// FUNC_AT(0x000e6a80)
void EAGL::RenderContext::GetSize(float *width_, float *height_) {
    *width_ = float(currentWidth);
    *height_ = float(currentHeight);
}

// FUNC_AT(0x000e6aa0)
void EAGL::RenderContext::SetFrontBufferDepth(int) {
}

// FUNC_AT(0x000e6ab0)
int EAGL::RenderContext::GetFrontBufferDepth() {
    return currentFrontBufferDepth;
}

// 16 or 32 bits; anything else is ignored. Sets the front buffer's depth too.
// FUNC_AT(0x000e6ac0)
void EAGL::RenderContext::SetBackBufferDepth(int depth) {
    if (depth == 16) {
        backBufferFormat = 5;
        frontBufferDepth = 16;
        backBufferDepth = 16;
    } else if (depth == 32) {
        backBufferFormat = 6;
        frontBufferDepth = 32;
        backBufferDepth = 32;
    }
}

// FUNC_AT(0x000e6b00)
int EAGL::RenderContext::GetBackBufferDepth() {
    return currentBackBufferDepth;
}

// 0 (none), 16 or 32 bits; anything else is ignored.
// FUNC_AT(0x000e6b10)
void EAGL::RenderContext::SetZBufferDepth(int depth) {
    if (depth == 0) {
        zBufferDepth = 0;
    } else if (depth == 16) {
        zBufferDepth = 16;
        depthFormat = 0x2c;
    } else if (depth == 32) {
        zBufferDepth = 32;
        depthFormat = 0x2a;
    }
}

// FUNC_AT(0x000e6b50)
int EAGL::RenderContext::GetZBufferDepth() {
    return currentZBufferDepth;
}

// Off: immediate presentation. On: the swap interval's. Resets the device if it exists. The refresh rate is
// cleared either way.
// FUNC_AT(0x000e6b60)
void EAGL::RenderContext::SetSyncToVBL(uint8_t sync) {
    syncToVBL = sync;
    present.refreshRate = 0;
    if (sync == 0)
        present.presentationInterval = 0x80000000;   // D3DPRESENT_INTERVAL_IMMEDIATE
    else
        present.presentationInterval = PresentationInterval(swapInterval);
    if (D3DDevicePointer != NULL) {
        D3DDevice_PersistDisplay();
        D3DDevice_Reset(&present);
    }
}

// FUNC_AT(0x000e6bd0)
uint8_t EAGL::RenderContext::GetSyncToVBL() {
    return syncToVBL;
}

// Kept for SetupFrameBuffers; nothing is sent.
// FUNC_AT(0x000e6be0)
bool EAGL::RenderContext::SetZEnable(uint32_t enable) {
    zEnable = enable;
    return true;
}

// FUNC_AT(0x000e6bf0)
bool EAGL::RenderContext::GetZEnable(uint32_t *enable) {
    *enable = zEnable;
    return true;
}

// =================================================================================================================
// SetupFrameBuffers: creates the device (or resets it), sends the whole state block once, clears and swaps, and
// builds the texture headers and TARs over the three buffers. 1, or 0 when Direct3D_CreateDevice fails.
// =================================================================================================================

// FUNC_AT(0x000e6c00)
uint32_t EAGL::RenderContext::SetupFrameBuffers() {
    if (D3DDevicePointer == NULL)
        D3D_SetPushBufferSize(pushBufferSize, kickOffSize);

    memset(&present, 0, sizeof(present));
    int32_t w = width, h = height;
    present.backBufferWidth = w;
    present.backBufferHeight = h;
    present.backBufferFormat = backBufferFormat;
    present.backBufferCount = 2;
    if (w == 1280 && h == 720)
        present.flags = 0x50;   // 720p
    if (w == 1920) {
        if (h == 1080)
            present.flags = 0x30;   // 1080
        if (h == 540)
            present.flags = 0xb0;   // 1080i field
    }
    if (wideScreen != 0)
        present.flags |= 0x10;
    if (presentFlag100 != 0 && w == 640)
        present.flags |= 0x100;
    present.windowed = 0;
    present.refreshRate = pal60 != 0 ? 60 : 0;
    if (syncToVBL == 0) {
        present.refreshRate = 0;
        present.presentationInterval = 0x80000000;   // D3DPRESENT_INTERVAL_IMMEDIATE
    } else {
        present.presentationInterval = PresentationInterval(swapInterval);
    }
    if (zBufferDepth != 0) {
        present.enableAutoDepthStencil = 1;
        present.autoDepthStencilFormat = depthFormat;
    } else {
        present.enableAutoDepthStencil = 0;
    }
    present.swapEffect = 1;
    present.multiSampleType = multiSampleType;

    if (D3DDevicePointer == NULL) {
        if (Direct3D_CreateDevice(0, 1, NULL, 0x10, &present, &D3DDevicePointer) < 0)
            return 0;
    } else {
        D3DDevice_PersistDisplay();
        D3DDevice_Reset(&present);
    }

    D3DDevice_GetGammaRamp(&GammaRamp);
    D3DDevice_SetShaderConstantMode(1);
    D3DDevice_SetRenderState_CullMode(0x901);
    SendState(0x40304, kRsAlphaBlendEnable, 1);
    ApplyCache.transparencyMethod = 0xffffffff;
    ApplyCache.cullEnable = 0xffffffff;
    ApplyCache.cullDirection = 0xffffffff;
    D3DDevice_SetRenderState_ZEnable(zEnable);
    D3DDevice_SetRenderState_StencilEnable(stencilEnable);
    SendState(0x4036c, kRsStencilMask, stencilMask);
    SendState(0x40368, kRsStencilRef, stencilRef);
    SendState(0x40364, kRsStencilFunc, stencilFunc);
    D3DDevice_SetRenderState_StencilFail(stencilFail);
    SendState(0x40378, kRsStencilPass, stencilZPass);
    SendState(0x40374, kRsStencilZFail, stencilZFail);
    SendState(0x4035c, kRsZWriteEnable, zWritesEnable);
    SendState(0x40358, kRsColorWriteEnable, colourWriteMask);
    SendState(0x40310, kRsDitherEnable, ditherEnable);
    D3DDevice_SetScreenSpaceOffset(screenSpaceOffsetX, screenSpaceOffsetY);
    ApplyCache.zEnable = zEnable;
    Shadows.stencilEnable = stencilEnable;
    Shadows.stencilMask = stencilMask;
    Shadows.stencilRef = stencilRef;
    Shadows.stencilFunc = stencilFunc;
    Shadows.stencilFail = stencilFail;
    Shadows.stencilZPass = stencilZPass;
    Shadows.stencilZFail = stencilZFail;
    Shadows.zWritesEnable = zWritesEnable;
    Shadows.colourWriteMask = colourWriteMask;
    SavedDither = ditherEnable;
    SavedFog = fogEnable;
    D3DDevice_SetRenderState_ShadowFunc(shadowFunc);

    // fog, slots 92..96
    uint32_t dirty = D3DDirtyFlags | 0x2000;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsFogEnable] = fogEnable;
    dirty |= 0x2000;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsFogTableMode] = fogTableMode;
    dirty |= 0x2000;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsFogStart] = fogStart;
    dirty |= 0x2000;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsFogEnd] = fogEnd;
    dirty |= 0x2000;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsFogDensity] = fogDensity;
    D3DDevice_SetRenderState_FogColor(fogColour);

    // points, slots 116..123
    dirty = D3DDirtyFlags | 0x100;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsPointSize] = pointSize;
    dirty |= 0x100;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsPointSizeMin] = pointSizeMin;
    dirty |= 0x100;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsPointSizeMax] = pointSizeMax;
    dirty |= 0x100;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsPointScaleA] = pointScaleA;
    dirty |= 0x100;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsPointScaleB] = pointScaleB;
    dirty |= 0x100;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsPointScaleC] = pointScaleC;
    dirty |= 0x900;
    D3DDirtyFlags = dirty;
    D3DRenderState[kRsPointSpriteEnable] = pointSpriteEnable;
    D3DRenderState[kRsPointScaleEnable] = pointScaleEnable;
    dirty |= 0x100;
    D3DDirtyFlags = dirty;

    currentWidth = width;
    currentHeight = height;
    currentFrontBufferDepth = frontBufferDepth;
    currentBackBufferDepth = backBufferDepth;
    currentZBufferDepth = zBufferDepth;
    D3DDevice_Clear(0, NULL, 0xf3, 0xff000000, 1.0f, 0);   // target | Z | stencil, opaque black, Z 1, stencil 0
    D3DDevice_SetShaderConstantMode(0);
    D3DDevice_Swap(0);
    D3DDevice_SetShaderConstantMode(1);
    backBuffer = D3DDevice_GetBackBuffer2(0);
    depthSurface = D3DDevice_GetDepthStencilSurface2();
    frontBuffer = D3DDevice_GetBackBuffer2(-1);

    // (The original passes the constructor a second argument, 0, which it does not read.)
    for (RenderMethod *method = ConstructedMethods; method != NULL; method = method->next)
        EAGL_RenderMethodConstructor(method);
    for (RenderMethod *method = WaitingMethods; method != NULL; method = method->next)
        EAGL_CopyParentPackets(method);

    // Tiles 0 and 1 set again (tile 1 from a copy, as the original's by-value argument)
    D3DTile tile0, tile1;
    D3DDevice_GetTile(0, &tile0);
    D3DDevice_GetTile(1, &tile1);
    D3DDevice_SetTile(0, NULL);
    D3DDevice_SetTile(0, &tile0);
    D3DTile tileCopy = tile1;
    D3DDevice_SetTile(1, NULL);
    D3DDevice_SetTile(1, &tileCopy);

    if (frontAlias == NULL)
        BuildSurfaceTexture(&frontAlias, frontBuffer, D3DTextureNames[0], false);
    if (frontTar == NULL)
        frontTar = EAGL_TARFromSurface(frontAlias);
    if (backAlias == NULL)
        BuildSurfaceTexture(&backAlias, backBuffer, D3DTextureNames[1], false);
    if (backTar == NULL)
        backTar = EAGL_TARFromSurface(backAlias);
    if (depthAlias == NULL)
        BuildSurfaceTexture(&depthAlias, depthSurface, D3DTextureNames[2], true);
    if (depthTar == NULL)
        depthTar = EAGL_TARFromSurface(depthAlias);
    return 1;
}

// =================================================================================================================
// RenderContext's own setters
// =================================================================================================================

// Method 0x40310 (D3D8's slot 65), shadowed in SavedDither - which EndFrame writes into slot 103.
// FUNC_AT(0x000e73a0)
bool EAGL::RenderContext::SetDitherEnable(uint8_t enable) {
    ditherEnable = enable;
    if (SavedDither != enable) {
        SavedDither = enable;
        if (D3DDevicePointer != NULL)
            SendState(0x40310, kRsDitherEnable, ditherEnable);
    }
    return true;
}

// FUNC_AT(0x000e73e0)
bool EAGL::RenderContext::GetDitherEnable(uint8_t *enable) {
    *enable = ditherEnable;
    return true;
}

// FUNC_AT(0x000e73f0)
bool EAGL::RenderContext::SetZWritesEnable(uint8_t enable) {
    zWritesEnable = enable;
    if (Shadows.zWritesEnable != enable) {
        Shadows.zWritesEnable = enable;
        if (D3DDevicePointer != NULL)
            SendState(0x4035c, kRsZWriteEnable, enable);
    }
    return true;
}

// FUNC_AT(0x000e7430)
bool EAGL::RenderContext::GetZWritesEnable(uint8_t *enable) {
    *enable = zWritesEnable;
    return true;
}

// FUNC_AT(0x000e7440)
bool EAGL::RenderContext::SetField30(uint32_t value) {
    unknown030 = value;
    return true;
}

// FUNC_AT(0x000e7450)
bool EAGL::RenderContext::GetField30(uint32_t *value) {
    *value = unknown030;
    return true;
}

// 1..3 (else false). With the device and VBL sync, D3D8's presentation-interval slot directly - and a write of the
// dirty flags to themselves, which the original makes (an OR with nothing).
// FUNC_AT(0x000e7460)
bool EAGL::RenderContext::SetSwapInterval(int interval) {
    if (interval <= 0 || interval > 3)
        return false;
    swapInterval = interval;
    uint32_t value = PresentationInterval(interval);
    if (D3DDevicePointer != NULL && syncToVBL != 0) {
        D3DDirtyFlags = D3DDirtyFlags;
        D3DRenderState[kRsPresentationInterval] = value;
    }
    return true;
}

// FUNC_AT(0x000e74c0)
bool EAGL::RenderContext::GetSwapInterval(int *interval) {
    *interval = swapInterval;
    return true;
}

// =================================================================================================================
// RenderContextExtension
// =================================================================================================================

// The multisample type; 0x11 (none) also turns the multisample antialias flag off, anything else on.
// FUNC_AT(0x000e74e0)
bool EAGL::RenderContextExtension::SetMultiSampleType(uint32_t type) {
    context->multiSampleType = type;
    context->multiSampleAntiAlias = type == 0x11 ? 0 : 1;
    return true;
}

// FUNC_AT(0x000e7510)
bool EAGL::RenderContextExtension::GetMultiSampleType(uint32_t *type) {
    *type = context->multiSampleType;
    return true;
}

// ---- stencil. Each keeps the field, and when it differs from the shadow updates the shadow and, with the
// device, sends it.

// FUNC_AT(0x000e7520)
bool EAGL::RenderContextExtension::SetStencilZFail(uint32_t op) {
    context->stencilZFail = op;
    if (Shadows.stencilZFail != op) {
        Shadows.stencilZFail = op;
        if (D3DDevicePointer != NULL)
            SendState(0x40374, kRsStencilZFail, op);
    }
    return true;
}

// FUNC_AT(0x000e7560)
bool EAGL::RenderContextExtension::GetStencilZFail(uint32_t *op) {
    *op = context->stencilZFail;
    return true;
}

// FUNC_AT(0x000e7570)
bool EAGL::RenderContextExtension::SetStencilZPass(uint32_t op) {
    context->stencilZPass = op;
    if (Shadows.stencilZPass != op) {
        Shadows.stencilZPass = op;
        if (D3DDevicePointer != NULL)
            SendState(0x40378, kRsStencilPass, op);
    }
    return true;
}

// FUNC_AT(0x000e75b0)
bool EAGL::RenderContextExtension::GetStencilZPass(uint32_t *op) {
    *op = context->stencilZPass;
    return true;
}

// FUNC_AT(0x000e75c0)
bool EAGL::RenderContextExtension::SetStencilFail(uint32_t op) {
    context->stencilFail = op;
    if (Shadows.stencilFail != op) {
        Shadows.stencilFail = op;
        if (D3DDevicePointer != NULL)
            D3DDevice_SetRenderState_StencilFail(op);
    }
    return true;
}

// FUNC_AT(0x000e75f0)
bool EAGL::RenderContextExtension::GetStencilFail(uint32_t *op) {
    *op = context->stencilFail;
    return true;
}

// FUNC_AT(0x000e7600)
bool EAGL::RenderContextExtension::SetStencilFunc(uint32_t func) {
    context->stencilFunc = func;
    if (Shadows.stencilFunc != func) {
        Shadows.stencilFunc = func;
        if (D3DDevicePointer != NULL)
            SendState(0x40364, kRsStencilFunc, func);
    }
    return true;
}

// FUNC_AT(0x000e7640)
bool EAGL::RenderContextExtension::GetStencilFunc(uint32_t *func) {
    *func = context->stencilFunc;
    return true;
}

// FUNC_AT(0x000e7650)
bool EAGL::RenderContextExtension::SetStencilRef(uint32_t ref) {
    context->stencilRef = ref;
    if (Shadows.stencilRef != ref) {
        Shadows.stencilRef = ref;
        if (D3DDevicePointer != NULL)
            SendState(0x40368, kRsStencilRef, ref);
    }
    return true;
}

// FUNC_AT(0x000e7690)
bool EAGL::RenderContextExtension::GetStencilRef(uint32_t *ref) {
    *ref = context->stencilRef;
    return true;
}

// FUNC_AT(0x000e76a0)
bool EAGL::RenderContextExtension::SetStencilMask(uint32_t mask) {
    context->stencilMask = mask;
    if (Shadows.stencilMask != mask) {
        Shadows.stencilMask = mask;
        if (D3DDevicePointer != NULL)
            SendState(0x4036c, kRsStencilMask, mask);
    }
    return true;
}

// FUNC_AT(0x000e76e0)
bool EAGL::RenderContextExtension::GetStencilMask(uint32_t *mask) {
    *mask = context->stencilMask;
    return true;
}

// FUNC_AT(0x000e76f0)
bool EAGL::RenderContextExtension::SetStencilWriteMask(uint32_t mask) {
    context->stencilWriteMask = mask;
    if (Shadows.stencilWriteMask != mask) {
        Shadows.stencilWriteMask = mask;
        if (D3DDevicePointer != NULL)
            SendState(0x40360, kRsStencilWriteMask, mask);
    }
    return true;
}

// FUNC_AT(0x000e7730)
bool EAGL::RenderContextExtension::GetStencilWriteMask(uint32_t *mask) {
    *mask = context->stencilWriteMask;
    return true;
}

// ---- gamma (both unreferenced)

// The start-up ramp scaled per channel.
// FUNC_AT(0x000e7740)
bool EAGL::RenderContextExtension::SetGamma(float red, float green, float blue) {
    D3DGammaRamp ramp;
    for (int i = 0; i < 0x100; i++) {
        ramp.red[i] = ScaledGamma(GammaRamp.red[i], red);
        ramp.green[i] = ScaledGamma(GammaRamp.green[i], green);
        ramp.blue[i] = ScaledGamma(GammaRamp.blue[i], blue);
    }
    if (D3DDevicePointer != NULL)
        D3DDevice_SetGammaRamp(2, &ramp);
    return true;
}

// FUNC_AT(0x000e7820)
bool EAGL::RenderContextExtension::SetGammaRamp(const uint8_t *source) {
    D3DGammaRamp ramp;
    memcpy(&ramp, source, sizeof(ramp));
    if (D3DDevicePointer != NULL)
        D3DDevice_SetGammaRamp(2, &ramp);
    return true;
}

// Not compared with its shadow: the shadow is written and, with the device, the state sent every time.
// FUNC_AT(0x000e7890)
bool EAGL::RenderContextExtension::SetStencilEnable(uint8_t enable) {
    context->stencilEnable = enable;
    Shadows.stencilEnable = enable;
    if (D3DDevicePointer != NULL)
        D3DDevice_SetRenderState_StencilEnable(enable);
    return true;
}

// FUNC_AT(0x000e78c0)
bool EAGL::RenderContextExtension::GetStencilEnable(uint8_t *enable) {
    *enable = context->stencilEnable;
    return true;
}

// The front buffer written to a file.
// FUNC_AT(0x000e78d0)
bool EAGL::RenderContextExtension::Screenshot(const char *path) {
    D3DPixelContainer *surface = D3DDevice_GetBackBuffer2(-1);
    XGWriteSurfaceToFile(surface, path);
    D3DResource_Release(surface);
    return true;
}

// The colour write mask (method 0x40358).
// FUNC_AT(0x000e7900)
bool EAGL::RenderContextExtension::SetRenderMask(uint32_t mask) {
    context->colourWriteMask = mask;
    if (Shadows.colourWriteMask != mask) {
        Shadows.colourWriteMask = mask;
        if (D3DDevicePointer != NULL)
            SendState(0x40358, kRsColorWriteEnable, mask);
    }
    return true;
}

// FUNC_AT(0x000e7940)
bool EAGL::RenderContextExtension::GetRenderMask(uint32_t *mask) {
    *mask = context->colourWriteMask;
    return true;
}

// ---- fog: D3D8's deferred fog states written directly (slots 92..96, dirty flag 0x2000)

// FUNC_AT(0x000e7950)
bool EAGL::RenderContextExtension::SetFogEnable(uint8_t enable) {
    context->fogEnable = enable;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x2000;
        D3DRenderState[kRsFogEnable] = enable;
    }
    return true;
}

// FUNC_AT(0x000e7990)
bool EAGL::RenderContextExtension::GetFogEnable(uint8_t *enable) {
    *enable = context->fogEnable;
    return true;
}

// FUNC_AT(0x000e79a0)
bool EAGL::RenderContextExtension::SetFogTableMode(uint32_t mode) {
    context->fogTableMode = mode;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x2000;
        D3DRenderState[kRsFogTableMode] = mode;
    }
    return true;
}

// FUNC_AT(0x000e79d0)
bool EAGL::RenderContextExtension::GetFogTableMode(uint32_t *mode) {
    *mode = context->fogTableMode;
    return true;
}

// FUNC_AT(0x000e79e0)
bool EAGL::RenderContextExtension::SetFogStart(uint32_t start) {
    context->fogStart = start;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x2000;
        D3DRenderState[kRsFogStart] = start;
    }
    return true;
}

// FUNC_AT(0x000e7a10)
bool EAGL::RenderContextExtension::GetFogStart(uint32_t *start) {
    *start = context->fogStart;
    return true;
}

// FUNC_AT(0x000e7a20)
bool EAGL::RenderContextExtension::SetFogEnd(uint32_t end) {
    context->fogEnd = end;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x2000;
        D3DRenderState[kRsFogEnd] = end;
    }
    return true;
}

// FUNC_AT(0x000e7a50)
bool EAGL::RenderContextExtension::GetFogEnd(uint32_t *end) {
    *end = context->fogEnd;
    return true;
}

// FUNC_AT(0x000e7a60)
bool EAGL::RenderContextExtension::SetFogDensity(uint32_t density) {
    context->fogDensity = density;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x2000;
        D3DRenderState[kRsFogDensity] = density;
    }
    return true;
}

// FUNC_AT(0x000e7a90)
bool EAGL::RenderContextExtension::GetFogDensity(uint32_t *density) {
    *density = context->fogDensity;
    return true;
}

// FUNC_AT(0x000e7aa0)
bool EAGL::RenderContextExtension::SetFogColour(uint32_t colour) {
    context->fogColour = colour;
    if (D3DDevicePointer != NULL)
        D3DDevice_SetRenderState_FogColor(colour);
    return true;
}

// FUNC_AT(0x000e7ac0)
bool EAGL::RenderContextExtension::GetFogColour(uint32_t *colour) {
    *colour = context->fogColour;
    return true;
}

// ---- presentation flags: with the device, the flag is ORed into the present parameters (never cleared, as the
// original) and the device reset.

// FUNC_AT(0x000e7ad0)
bool EAGL::RenderContextExtension::SetWideScreen(uint8_t enable) {
    context->wideScreen = enable;
    if (D3DDevicePointer != NULL) {
        context->present.flags |= 0x10;
        D3DDevice_PersistDisplay();
        D3DDevice_Reset(&context->present);
    }
    return true;
}

// FUNC_AT(0x000e7b10)
bool EAGL::RenderContextExtension::GetWideScreen(uint8_t *enable) {
    *enable = context->wideScreen;
    return true;
}

// FUNC_AT(0x000e7b20)
bool EAGL::RenderContextExtension::SetPresentFlag100(uint8_t enable) {
    context->presentFlag100 = enable;
    if (D3DDevicePointer != NULL) {
        context->present.flags |= 0x100;
        D3DDevice_PersistDisplay();
        D3DDevice_Reset(&context->present);
    }
    return true;
}

// FUNC_AT(0x000e7b60)
bool EAGL::RenderContextExtension::GetPresentFlag100(uint8_t *enable) {
    *enable = context->presentFlag100;
    return true;
}

// FUNC_AT(0x000e7b70)
bool EAGL::RenderContextExtension::SetSoftDisplayFilter(uint8_t enable) {
    context->softDisplayFilter = enable;
    if (D3DDevicePointer != NULL)
        D3DDevice_SetSoftDisplayFilter(enable);
    return true;
}

// FUNC_AT(0x000e7ba0)
bool EAGL::RenderContextExtension::GetSoftDisplayFilter(uint8_t *enable) {
    *enable = context->softDisplayFilter;
    return true;
}

// Clamped to 0..5 in the field.
// FUNC_AT(0x000e7bc0)
bool EAGL::RenderContextExtension::SetFlickerFilter(int level) {
    context->flickerFilter = level;
    if (level > 5)
        context->flickerFilter = 5;
    if (level < 0)
        context->flickerFilter = 0;
    if (D3DDevicePointer != NULL)
        D3DDevice_SetFlickerFilter(context->flickerFilter);
    return true;
}

// FUNC_AT(0x000e7c10)
bool EAGL::RenderContextExtension::GetFlickerFilter(int *level) {
    *level = context->flickerFilter;
    return true;
}

// FUNC_AT(0x000e7c30)
bool EAGL::RenderContextExtension::SetShadowFunc(uint32_t func) {
    context->shadowFunc = func;
    if (D3DDevicePointer != NULL)
        D3DDevice_SetRenderState_ShadowFunc(func);
    return true;
}

// FUNC_AT(0x000e7c50)
bool EAGL::RenderContextExtension::GetShadowFunc(uint32_t *func) {
    *func = context->shadowFunc;
    return true;
}

// ---- visibility tests (the lens flares)

// FUNC_AT(0x000e7c60)
bool EAGL::RenderContextExtension::BeginVisibilityTest() {
    if (D3DDevicePointer == NULL)
        return false;
    D3DDevice_BeginVisibilityTest();
    return true;
}

// FUNC_AT(0x000e7c80)
bool EAGL::RenderContextExtension::EndVisibilityTest(uint32_t index) {
    if (D3DDevicePointer == NULL)
        return false;
    D3DDevice_EndVisibilityTest(index);
    return true;
}

// The result is stored whatever the call returns; a failure (D3DERR_TESTINCOMPLETE among them) then zeroes it and
// answers false. src/common/gfx/d3d9Backend.cpp relies on this.
// FUNC_AT(0x000e7ca0)
bool EAGL::RenderContextExtension::GetVisibilityTestResult(uint32_t index, uint32_t *result) {
    if (D3DDevicePointer == NULL) {
        *result = 0;
        return false;
    }
    uint32_t pixels = 0;
    int32_t hr = D3DDevice_GetVisibilityTestResult(index, &pixels, NULL);
    *result = pixels;
    if (hr < 0) {
        *result = 0;
        return false;
    }
    return true;
}

// The front buffer copied out, when it is 32 bits a pixel and unpadded (else false).
// FUNC_AT(0x000e7d00)
bool EAGL::RenderContextExtension::ReadBackBuffer(void *destination) {
    D3DPixelContainer *surface = D3DDevice_GetBackBuffer2(-1);
    D3DSurfaceDesc desc;
    D3DSurface_GetDesc(surface, &desc);
    if (desc.size != (desc.height * desc.width) << 2) {
        D3DResource_Release(surface);
        return false;
    }
    D3DLockedRect locked;
    D3DSurface_LockRect(surface, &locked, NULL, 0x40);
    MEM_copy(destination, locked.bits, desc.size);
    D3DResource_Release(surface);
    return true;
}

// The TARs' two overrides (Tar.cpp), with a setter and getter each; RRenderHigh::Render sets the LOD bias.
// FUNC_AT(0x000e7d70)
bool EAGL::RenderContextExtension::SetGlobal23ff0c(uint32_t value) {
    LodBiasOverride = std::bit_cast<float>(value);
    return true;
}

// FUNC_AT(0x000e7d80)
bool EAGL::RenderContextExtension::GetGlobal23ff0c(float *value) {
    *value = LodBiasOverride;
    return true;
}

// FUNC_AT(0x000e7da0)
bool EAGL::RenderContextExtension::SetGlobal1cb938(uint32_t value) {
    FilterOverride = value;
    return true;
}

// FUNC_AT(0x000e7db0)
bool EAGL::RenderContextExtension::GetGlobal1cb938(uint32_t *value) {
    *value = FilterOverride;
    return true;
}

// FUNC_AT(0x000e7dd0)
bool EAGL::RenderContextExtension::SetMultiSampleAntiAlias(uint8_t enable) {
    context->multiSampleAntiAlias = enable;
    if (D3DDevicePointer != NULL)
        D3DDevice_SetRenderState_MultiSampleAntiAlias(enable);
    return true;
}

// FUNC_AT(0x000e7e00)
bool EAGL::RenderContextExtension::GetMultiSampleAntiAlias(uint8_t *enable) {
    *enable = context->multiSampleAntiAlias;
    return true;
}

// Only before the device exists.
// FUNC_AT(0x000e7e10)
bool EAGL::RenderContextExtension::SetPushBufferSize(uint32_t size, uint32_t kickOffSize) {
    if (D3DDevicePointer != NULL)
        return false;
    context->pushBufferSize = size;
    context->kickOffSize = kickOffSize;
    return true;
}

// FUNC_AT(0x000e7e40)
bool EAGL::RenderContextExtension::GetPushBufferSize(uint32_t *size, uint32_t *kickOffSize) {
    *size = context->pushBufferSize;
    *kickOffSize = context->kickOffSize;
    return true;
}

// FUNC_AT(0x000e7e70)
bool EAGL::RenderContextExtension::SetScreenSpaceOffset(float x, float y) {
    context->screenSpaceOffsetX = x;
    context->screenSpaceOffsetY = y;
    if (D3DDevicePointer != NULL)
        D3DDevice_SetScreenSpaceOffset(x, y);
    return true;
}

// FUNC_AT(0x000e7eb0)
bool EAGL::RenderContextExtension::GetScreenSpaceOffset(float *x, float *y) {
    *x = context->screenSpaceOffsetX;
    *y = context->screenSpaceOffsetY;
    return true;
}

// ---- point sprites: D3D8's deferred point states written directly (slots 116..123, dirty flag 0x100)

// FUNC_AT(0x000e7ee0)
bool EAGL::RenderContextExtension::SetPointSize(uint32_t size) {
    context->pointSize = size;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x100;
        D3DRenderState[kRsPointSize] = size;
    }
    return true;
}

// FUNC_AT(0x000e7f20)
bool EAGL::RenderContextExtension::GetPointSize(uint32_t *size) {
    *size = context->pointSize;
    return true;
}

// FUNC_AT(0x000e7f40)
bool EAGL::RenderContextExtension::SetPointSizeMin(uint32_t size) {
    context->pointSizeMin = size;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x100;
        D3DRenderState[kRsPointSizeMin] = size;
    }
    return true;
}

// FUNC_AT(0x000e7f80)
bool EAGL::RenderContextExtension::GetPointSizeMin(uint32_t *size) {
    *size = context->pointSizeMin;
    return true;
}

// FUNC_AT(0x000e7fa0)
bool EAGL::RenderContextExtension::SetPointSizeMax(uint32_t size) {
    context->pointSizeMax = size;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x100;
        D3DRenderState[kRsPointSizeMax] = size;
    }
    return true;
}

// FUNC_AT(0x000e7fe0)
bool EAGL::RenderContextExtension::GetPointSizeMax(uint32_t *size) {
    *size = context->pointSizeMax;
    return true;
}

// FUNC_AT(0x000e8000)
bool EAGL::RenderContextExtension::SetPointScale(uint32_t a, uint32_t b, uint32_t c) {
    context->pointScaleA = a;
    context->pointScaleB = b;
    context->pointScaleC = c;
    if (D3DDevicePointer != NULL) {
        D3DRenderState[kRsPointScaleB] = b;
        uint32_t dirty = D3DDirtyFlags | 0x100;
        D3DRenderState[kRsPointScaleA] = a;
        D3DDirtyFlags = dirty;
        D3DRenderState[kRsPointScaleC] = c;
    }
    return true;
}

// FUNC_AT(0x000e8060)
bool EAGL::RenderContextExtension::GetPointScale(uint32_t *a, uint32_t *b, uint32_t *c) {
    *a = context->pointScaleA;
    *b = context->pointScaleB;
    *c = context->pointScaleC;
    return true;
}

// FUNC_AT(0x000e8090)
bool EAGL::RenderContextExtension::SetPointSpriteEnable(uint8_t enable) {
    context->pointSpriteEnable = enable;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x900;
        D3DRenderState[kRsPointSpriteEnable] = enable;
    }
    return true;
}

// FUNC_AT(0x000e80d0)
bool EAGL::RenderContextExtension::GetPointSpriteEnable(uint8_t *enable) {
    *enable = context->pointSpriteEnable;
    return true;
}

// FUNC_AT(0x000e80f0)
bool EAGL::RenderContextExtension::SetPointScaleEnable(uint8_t enable) {
    context->pointScaleEnable = enable;
    if (D3DDevicePointer != NULL) {
        D3DDirtyFlags |= 0x100;
        D3DRenderState[kRsPointScaleEnable] = enable;
    }
    return true;
}

// FUNC_AT(0x000e8130)
bool EAGL::RenderContextExtension::GetPointScaleEnable(uint8_t *enable) {
    *enable = context->pointScaleEnable;
    return true;
}

// ---- the rest

// Whether the dashboard allows PAL60 (XGetVideoFlags bit 0x40), kept for SetupFrameBuffers' refresh rate.
// FUNC_AT(0x000e8150)
uint8_t EAGL::RenderContextExtension::QueryPal60() {
    context->pal60 = (Xbox_XGetVideoFlags() & 0x40) != 0 ? 1 : 0;
    return context->pal60;
}

// FUNC_AT(0x000e8190)
bool EAGL::RenderContextExtension::IsDeviceCreated(uint32_t) {
    return D3DDevicePointer != NULL;
}

// The front buffer copied into a texture of its own (made the first time, with a TAR over it in the back
// buffer's TAR slot if that is still empty), returning that slot.
// FUNC_AT(0x000e81a0)
EAGL::TAR* EAGL::RenderContextExtension::CopyBackBuffer() {
    D3DPixelContainer *source = D3DDevice_GetBackBuffer2(0);
    D3DSurfaceDesc desc;
    D3DSurface_GetDesc(source, &desc);
    if (context->copyTexture == NULL) {
        context->copyTexture = D3DDevice_CreateTexture2(desc.width, desc.height, 1, 1, 0, desc.format, 3);
        if (context->backTar == NULL)
            context->backTar = EAGL_TARFromSurface(context->copyTexture);
    }
    D3DPixelContainer *destination = D3DTexture_GetSurfaceLevel2(context->copyTexture, 0);
    D3DRect rect = {0, 0, int32_t(desc.width), int32_t(desc.height)};
    D3DPoint point = {0, 0};
    D3DDevice_CopyRects(source, &rect, 1, destination, &point);
    D3DResource_Release(destination);
    D3DResource_Release(source);
    return context->backTar;
}

// The buffers as TARs, the texture headers and TARs built the first time (SetupFrameBuffers has normally built
// them already).
// FUNC_AT(0x000e8270)
EAGL::TAR* EAGL::RenderContextExtension::GetFrontBuffer() {
    if (context->frontAlias == NULL)
        BuildSurfaceTexture(&context->frontAlias, context->frontBuffer, D3DTextureNames[3], false);
    if (context->frontTar == NULL)
        context->frontTar = EAGL_TARFromSurface(context->frontAlias);
    return context->frontTar;
}

// FUNC_AT(0x000e8350)
EAGL::TAR* EAGL::RenderContextExtension::GetBackBuffer() {
    if (context->backAlias == NULL)
        BuildSurfaceTexture(&context->backAlias, context->backBuffer, D3DTextureNames[4], false);
    if (context->backTar == NULL)
        context->backTar = EAGL_TARFromSurface(context->backAlias);
    return context->backTar;
}

// FUNC_AT(0x000e8430)
EAGL::TAR* EAGL::RenderContextExtension::GetDepthBuffer() {
    if (context->depthAlias == NULL)
        BuildSurfaceTexture(&context->depthAlias, context->depthSurface, D3DTextureNames[5], true);
    if (context->depthTar == NULL)
        context->depthTar = EAGL_TARFromSurface(context->depthAlias);
    return context->depthTar;
}

// FUNC_AT(0x000e8560)
EAGL::RenderContextExtension* EAGL::RenderContextExtension::Construct(RenderContext *owner) {
    context = owner;
    owner->copyTexture = NULL;
    context->backTar = NULL;
    context->frontTar = NULL;
    context->depthTar = NULL;
    context->frontAlias = NULL;
    context->backAlias = NULL;
    context->depthAlias = NULL;
    return this;
}

// FUNC_AT(0x000e85b0)
void EAGL::RenderContextExtension::ReleaseCopyTexture() {
    if (context->copyTexture != NULL) {
        D3DResource_Release(context->copyTexture);
        context->copyTexture = NULL;
    }
}

// FUNC_AT(0x000e85e0)
EAGL::SurfaceTexture* EAGL::SurfaceTexture::Construct() {
    common = 0;
    data = 0;
    lock = 0;
    format = 0;
    size = 0;
    return this;
}

// =================================================================================================================
// Construction and destruction
// =================================================================================================================

// Every render method is destroyed with the context (EAGL has one), then the viewports, the three surfaces and the
// copy texture. The texture headers and TARs over the buffers are not freed (as the original).
// FUNC_AT(0x000e8600)
void EAGL::RenderContext::Destruct() {
    for (RenderMethod *method = ConstructedMethods; method != NULL; method = method->next)
        EAGL_RenderMethodDestructor(method);
    for (RenderMethod *method = WaitingMethods; method != NULL; method = method->next)
        EAGL_ReleaseDynamicBuffers(method);
    while (viewPorts != NULL)
        DeleteViewPort(viewPorts);
    if (depthSurface != NULL)
        D3DResource_Release(depthSurface);
    if (frontBuffer != NULL)
        D3DResource_Release(frontBuffer);
    if (backBuffer != NULL)
        D3DResource_Release(backBuffer);
    // ReleaseCopyTexture inlined, on the extension pointer taken as the object (which it is)
    RenderContext *owner = reinterpret_cast<RenderContext *>(extension);
    if (owner->copyTexture != NULL) {
        D3DResource_Release(owner->copyTexture);
        reinterpret_cast<RenderContext *>(extension)->copyTexture = NULL;
    }
}

// FUNC_AT(0x000e86f0)
EAGL::RenderContextPrivate* EAGL::RenderContextPrivate::Construct(RenderContext *owner_) {
    RenderContext *object = Object();
    owner = owner_;
    object->syncToVBL = 1;
    object->fogColour = 0;
    object->currentWidth = 0;
    object->currentHeight = 0;
    object->currentFrontBufferDepth = 0;
    object->currentBackBufferDepth = 0;
    object->currentZBufferDepth = 0;
    object->currentViewPort = NULL;
    object->viewPorts = NULL;
    object->next = NULL;
    object->unknown144 = 0;
    return this;
}

// The extension and private constructors inlined, then the defaults: 640x480, 32-bit buffers, no multisampling,
// sync to the VBL, Z on, stencil off (ALWAYS, ref 0, masks 0xffffffff/0xff, ops 0x1e00 KEEP), Z writes on, all
// colour channels, fog off (0..1), point states at D3D8's defaults with a maximum of 64.
// FUNC_AT(0x000e8740)
EAGL::RenderContext* EAGL::RenderContext::Construct(Device *device_) {
    extension = Extension();
    copyTexture = NULL;
    RenderContext *owner = reinterpret_cast<RenderContext *>(extension);
    owner->backTar = NULL;
    owner->frontTar = NULL;
    owner->depthTar = NULL;
    owner->frontAlias = NULL;
    owner->backAlias = NULL;
    owner->depthAlias = NULL;
    privateOwner = this;
    syncToVBL = 1;
    fogColour = 0;
    currentWidth = 0;
    currentHeight = 0;
    currentFrontBufferDepth = 0;
    currentBackBufferDepth = 0;
    currentZBufferDepth = 0;
    currentViewPort = NULL;
    viewPorts = NULL;
    next = NULL;
    unknown144 = 0;
    device = device_;
    width = 640;
    currentWidth = 640;
    height = 480;
    currentHeight = 480;
    frontBufferDepth = 32;
    currentFrontBufferDepth = 32;
    backBufferDepth = 32;
    currentBackBufferDepth = 32;
    zBufferDepth = 32;
    currentZBufferDepth = 32;
    stencilZFail = 0x1e00;
    stencilFail = 0x1e00;
    zEnable = 1;
    multiSampleType = 0x11;
    syncToVBL = 1;
    ditherEnable = 0;
    stencilZPass = 0;
    stencilFunc = 0x207;
    stencilRef = 0;
    stencilMask = 0xffffffff;
    stencilWriteMask = 0xff;
    stencilEnable = 0;
    zWritesEnable = 1;
    colourWriteMask = 0x01010101;
    multiSampleAntiAlias = 0;
    fogEnable = 0;
    fogTableMode = 0;
    fogStart = 0;
    fogEnd = 0x3f800000;         // 1.0f
    fogDensity = 0x3f800000;
    fogColour = 0;
    wideScreen = 0;
    presentFlag100 = 0;
    shadowFunc = 0x200;
    pushBufferSize = 0x80000;
    kickOffSize = 0x8000;
    screenSpaceOffsetX = 0.0f;
    screenSpaceOffsetY = 0.0f;
    pointSize = 0x3f800000;      // 1.0f
    pointSizeMin = 0x3f800000;
    pointSizeMax = 0x42800000;   // 64.0f
    pointScaleA = 0x3f800000;
    pointScaleB = 0;
    pointScaleC = 0;
    pointSpriteEnable = 0;
    pointScaleEnable = 0;
    softDisplayFilter = 0;
    pal60 = 0;
    swapInterval = 1;
    flickerFilter = 5;
    memset(&present, 0, sizeof(present));
    return this;
}
