#ifndef DRIVING_EAGL_D3D8STATE_H_
#define DRIVING_EAGL_D3D8STATE_H_

// D3D8's own state tables as EAGL's inlined D3D8 code writes them, the D3D8 entry points more than one EAGL file
// calls, and EAGL's shadows of what it last sent (docs/driving/eagl.md 5.1, 5.2). The backend reads the tables back
// at draw time, so a port writes them where the original did. Used by GeoPrimState.cpp, RenderContext.cpp and
// View.cpp.

#include <stdint.h>

#include "../../helpers.h"

// ---- D3D8's deferred state

#define D3DDirtyFlags U32_AT(0x00175424)
#define D3DTextureState ((uint32_t (*)[32])0x00175428)    // D3DTextureState[stage][state]: 32 dwords a stage
#define D3DRenderState ((uint32_t *)0x00175628)           // D3DRenderState[slot]

// The render-state slots EAGL writes, as the Xbox's D3D8 numbers D3DRENDERSTATETYPE (57..83 the "simple" states,
// each paired with a push-buffer method; the rest deferred, flushed through the dirty flags).
enum D3DRenderStateIndex {
    kRsZFunc = 57, kRsAlphaFunc, kRsAlphaBlendEnable, kRsAlphaTestEnable, kRsAlphaRef, kRsSrcBlend, kRsDestBlend,
    kRsZWriteEnable, kRsDitherEnable, kRsShadeMode, kRsColorWriteEnable, kRsStencilZFail, kRsStencilPass,
    kRsStencilFunc, kRsStencilRef, kRsStencilMask, kRsStencilWriteMask, kRsBlendOp, kRsBlendColor, kRsSwathWidth,
    kRsPolygonOffsetZSlopeScale, kRsPolygonOffsetZOffset, kRsPointOffsetEnable, kRsWireFrameOffsetEnable,
    kRsSolidOffsetEnable, kRsDepthClipControl, kRsStippleEnable,
    kRsFogEnable = 92, kRsFogTableMode, kRsFogStart, kRsFogEnd, kRsFogDensity,
    kRsSpecularEnable = 103,
    kRsPointSize = 116, kRsPointSizeMin, kRsPointSpriteEnable, kRsPointScaleEnable, kRsPointScaleA, kRsPointScaleB,
    kRsPointScaleC, kRsPointSizeMax,
    kRsPresentationInterval = 127,
};

// ---- D3D8 entry points (the seam), by their original addresses

// D3DDevice_SetRenderState_Simple takes the push-buffer method in ECX and the value in EDX: __fastcall's registers.
#define D3DDevice_SetRenderState_Simple ((void (__fastcall *)(uint32_t, uint32_t))0x001673e0)
#define D3DDevice_SetRenderState_FillMode ((void (__stdcall *)(uint32_t))0x00167ad0)
#define D3DDevice_SetRenderState_CullMode ((void (__stdcall *)(uint32_t))0x001677b0)
#define D3DDevice_SetTexture ((void (__stdcall *)(uint32_t, void *))0x00166830)
#define D3DDevice_SetRenderTarget ((void (__stdcall *)(void *, void *))0x00165dc0)
#define D3DDevice_Clear ((void (__stdcall *)(uint32_t, const void *, uint32_t, uint32_t, float, uint32_t))0x00168c90)
#define D3DTexture_GetSurfaceLevel2 ((void *(__stdcall *)(void *, uint32_t))0x00167330)
#define D3DResource_Release ((uint32_t (__stdcall *)(void *))0x00169230)

// A simple render state through its push-buffer method, then into D3D8's table
static inline void SendState(uint32_t method, D3DRenderStateIndex index, uint32_t value) {
    D3DDevice_SetRenderState_Simple(method, value);
    D3DRenderState[index] = value;
}

// ---- EAGL's shadows of what it last sent

// What GeoPrimState::Apply last sent, at 0x001cd18c. RenderContext::EndFrame and SetupFrameBuffers invalidate parts
// of it (0xffffffff) and SetupFrameBuffers writes the Z enable.
struct GeoPrimApplyCache {
    uint32_t shading;                // +0x00
    uint32_t cullEnable;             // +0x04
    uint32_t cullDirection;          // +0x08
    uint32_t zEnable;                // +0x0c only SetupFrameBuffers writes it
    uint32_t depthTestMethod;        // +0x10
    uint32_t alphaBlendMode;         // +0x14
    uint32_t alphaTestEnable;        // +0x18
    uint32_t alphaCompareValue;      // +0x1c
    uint32_t alphaTestMethod;        // +0x20
    uint8_t textureEnable;           // +0x24
    uint8_t pad25[3];
    uint32_t unknown28;
    uint32_t transparencyMethod;     // +0x2c
    uint32_t fillMode;               // +0x30
    uint32_t blendOperation;         // +0x34
    uint32_t blendSource;            // +0x38
    uint32_t blendDestination;       // +0x3c
    uint32_t blendColour;            // +0x40
    uint32_t zWritesEnable;          // +0x44
};
static_assert(sizeof(GeoPrimApplyCache) == 0x48, "the GeoPrimState apply cache runs to 0x001cd1d4");

#define ApplyCache (*(GeoPrimApplyCache *)0x001cd18c)
#define ApplyCacheZSlopeScale FLOAT_AT(0x00240150)
#define ApplyCacheZOffset FLOAT_AT(0x00240154)

// What RenderContextExtension last sent, at 0x001cb93c; GeoPrimState::Apply reads the Z-write byte.
struct ExtensionShadows {
    uint32_t stencilEnable;          // +0x00
    uint32_t stencilFail;            // +0x04
    uint32_t stencilZPass;           // +0x08
    uint32_t stencilZFail;           // +0x0c
    uint8_t zWritesEnable;           // +0x10 only ever accessed as a byte
    uint8_t pad11[3];
    uint32_t colourWriteMask;        // +0x14
    uint32_t stencilMask;            // +0x18
    uint32_t stencilRef;             // +0x1c
    uint32_t stencilFunc;            // +0x20
    uint32_t stencilWriteMask;       // +0x24
};
static_assert(sizeof(ExtensionShadows) == 0x28, "the extension's shadows run to 0x001cb964");

#define Shadows (*(ExtensionShadows *)0x001cb93c)

#define StageTexture ((void **)0x0023ff80)                  // the texture bound per stage (opcode 15, TAR::Use)

#endif // DRIVING_EAGL_D3D8STATE_H_
