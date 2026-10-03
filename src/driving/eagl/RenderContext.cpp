#include "RenderContext.h"

#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL::RenderContext, RenderContextExtension and RenderContextPrivate (docs/driving/eagl.md 2.2, 3.2, 4.2): every
// code entry point in 0x000e6610..0x000e8900, including the getters that sit after their setters without a
// Ghidra function and the unreferenced debug routines (the screenshot, the back-buffer read, the gamma setters).
//
// This code runs every frame. Each function makes the original's D3D8 calls (through the seam, by the entry
// points' original addresses) in the original's order with the original's arguments, and writes D3D8's own
// render-state table (0x00175628 + 4 * slot) and dirty flags (0x00175424) where the original's inlined D3D8 code
// did, in the same order relative to the calls (5.2, 8.1). EndFrame's re-send of ~40 states after Swap (8.2) is
// kept statement for statement, including the redundant dirty-flag writes.
//
// Every state write is gated on the device existing (0x0023ff18), as in the original. The shadows at
// 0x001cb938..0x001cb960 are what the extension last sent; GeoPrimState::Apply reads the Z-write one.
//
// The destructor's SEH frame (handler 0x00154c58) is left out: nothing in it can throw.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

using EAGL::RenderContext;
using EAGL::RenderContextExtension;

namespace {

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}

inline float &F32(uint32_t address) {
    return *(float *)(uintptr_t)address;
}

inline uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// __ftol2: truncation to 64 bits, of which the low 32 are used; what it cannot convert gives 0x80000000_00000000.
inline int32_t Ftol(double v) {
    if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0))
        return 0;
    return (int32_t)(int64_t)v;
}

// ---- globals

const uint32_t kDeviceCreated = 0x0023ff18;   // D3D8's device pointer, written by Direct3D_CreateDevice
const uint32_t kDirty = 0x00175424;           // D3D8's dirty flags
const uint32_t kStageTexture = 0x0023ff80;    // the texture bound per stage (TAR::Use, opcode 15)
const uint32_t kRenderTargetTexture = 0x0023ff1c;
const uint32_t kGammaRamp = 0x0023fc08;       // the ramp GetGammaRamp read at start-up: red, green, blue x 256
const uint32_t kSavedDither = 0x0023ff10, kSavedFog = 0x0023ff11;
const uint32_t kYuvEnable = 0x0023ffe0;       // the TAR code's YUV-enable shadow
const uint32_t kPixelShader = 0x00240474, kVertexShader = 0x00240478;   // the shaders in use
const uint32_t kVertexBufferList = 0x0023ff20, kRenderMethodList = 0x0023ff24;
const uint32_t kGlobal23ff0c = 0x0023ff0c;

// RenderContextExtension's shadows of what it last sent
const uint32_t kShadow1cb938 = 0x001cb938, kShadowStencilEnable = 0x001cb93c, kShadowStencilFail = 0x001cb940,
               kShadowStencilZPass = 0x001cb944, kShadowStencilZFail = 0x001cb948, kShadowZWrites = 0x001cb94c,
               kShadowColourMask = 0x001cb950, kShadowStencilMask = 0x001cb954, kShadowStencilRef = 0x001cb958,
               kShadowStencilFunc = 0x001cb95c, kShadowStencilWriteMask = 0x001cb960;

// GeoPrimState::Apply's cache (GeoPrimState.cpp names them); EndFrame and SetupFrameBuffers invalidate some
const uint32_t kCacheShading = 0x001cd18c, kCacheCullEnable = 0x001cd190, kCacheCullDirection = 0x001cd194,
               kCache198 = 0x001cd198, kCacheDepthTest = 0x001cd19c, kCacheAlphaTest = 0x001cd1a4,
               kCacheAlphaCompare = 0x001cd1a8, kCacheAlphaMethod = 0x001cd1ac, kCacheTransparency = 0x001cd1b8,
               kCacheFillMode = 0x001cd1bc, kCacheBlendOperation = 0x001cd1c0, kCacheBlendSource = 0x001cd1c4,
               kCacheBlendDestination = 0x001cd1c8, kCacheBlendColour = 0x001cd1cc, kCacheZSlope = 0x00240150,
               kCacheZOffset = 0x00240154;

// D3D8's render-state table: the slot each write lands in, by address
inline void D3DState(uint32_t address, uint32_t value) {
    U32(address) = value;
}

// ---- callees

// D3DDevice_SetRenderState_Simple takes the push-buffer method in ECX and the value in EDX: __fastcall's registers.
inline void SetRenderStateSimple(uint32_t method, uint32_t value) {
    ((void (__fastcall *)(uint32_t, uint32_t))0x001673e0)(method, value);
}

inline void *DeviceGet() {
    return ((void *(*)())0x000e8a40)();   // EAGL::Device::Get
}

inline void *EaglMalloc(uint32_t size, const char *name) {
    return (*(void *(**)(uint32_t, const char *))0x001caf68u)(size, name);
}

inline void SetTexture(uint32_t stage, void *texture) {
    ((void (__stdcall *)(uint32_t, void *))0x00166830)(stage, texture);
}

inline void SetShaderConstantMode(uint32_t mode) {
    ((void (__stdcall *)(uint32_t))0x0016ab30)(mode);
}

inline void Swap(uint32_t flags) {
    ((void (__stdcall *)(uint32_t))0x00169fb0)(flags);
}

inline void SetRenderStateFogColor(uint32_t colour) {
    ((void (__stdcall *)(uint32_t))0x00167760)(colour);
}

inline void SetRenderStateStencilEnable(uint32_t enable) {
    ((void (__stdcall *)(uint32_t))0x00168880)(enable);
}

inline void SetRenderStateStencilFail(uint32_t op) {
    ((void (__stdcall *)(uint32_t))0x00168910)(op);
}

inline void SetRenderStateShadowFunc(uint32_t func) {
    ((void (__stdcall *)(uint32_t))0x00167720)(func);
}

inline void PersistDisplay() {
    ((void (__stdcall *)())0x00166eb0)();
}

inline void Reset(void *presentParameters) {
    ((void (__stdcall *)(void *))0x00166230)(presentParameters);
}

inline void *GetBackBuffer2(int32_t index) {
    return ((void *(__stdcall *)(int32_t))0x001662e0)(index);
}

inline void Release(void *resource) {
    ((void (__stdcall *)(void *))0x00169230)(resource);
}

inline void D3DSetGammaRamp(uint32_t flags, const void *ramp) {
    ((void (__stdcall *)(uint32_t, const void *))0x00165de0)(flags, ramp);
}

// D3DSURFACE_DESC as the Xbox's D3D8 has it
struct SurfaceDesc {
    uint32_t format;              // +0x00
    uint32_t type;                // +0x04
    uint32_t usage;               // +0x08
    uint32_t size;                // +0x0c
    uint32_t multiSampleType;     // +0x10
    uint32_t width;               // +0x14
    uint32_t height;              // +0x18
};
static_assert(sizeof(SurfaceDesc) == 0x1c, "D3DSURFACE_DESC is 0x1c bytes");

inline void SurfaceGetDesc(void *surface, SurfaceDesc *desc) {
    ((void (__stdcall *)(void *, SurfaceDesc *))0x00167220)(surface, desc);
}

inline uint32_t XGBytesPerPixelFromFormat(uint32_t format) {
    return ((uint32_t (__stdcall *)(uint32_t))0x00178fb8)(format);
}

inline void XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage, uint32_t format,
                               uint32_t pool, void *texture, uint32_t data, uint32_t pitch) {
    ((void (__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void *, uint32_t,
                         uint32_t))0x0017a8ac)(width, height, levels, usage, format, pool, texture, data, pitch);
}

inline void *MakeTar(void *texture) {
    return ((void *(*)(void *))0x000ec610)(texture);   // a TAR over a D3D texture (texture module)
}

// The texture header laid over a surface, as SetupFrameBuffers and the three buffer getters build it: allocated
// under the given name (each site passes its own copy of "D3DTexture"), cleared, given the surface's size and
// format (the depth surface's format mapped to its texture equivalent), and pointed at the surface's data.
void BuildSurfaceTexture(void **slot, void *surface, const char *name, bool depth) {
    uint32_t *texture = (uint32_t *)EaglMalloc(0x14, name);
    *slot = texture;
    texture[0] = 0;
    texture[1] = 0;
    texture[2] = 0;
    texture[3] = 0;
    texture[4] = 0;
    SurfaceDesc desc;
    SurfaceGetDesc(surface, &desc);
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
    ((uint32_t *)*slot)[1] = ((const uint32_t *)surface)[1];
}

}  // namespace

// =================================================================================================================
// The frame
// =================================================================================================================

// FUNC_AT(0x000e6610)
void EAGL::RenderContext::BeginFrame() {
    void *device = DeviceGet();
    // DevicePrivate::SetCurrentRenderContext, on the device's private part at +4
    ((void (__fastcall *)(void *, int, void *))0x000e8a00)((uint8_t *)device + 4, 0, this);
    DeviceGet();
    ((void (__stdcall *)(void *, void *))0x00165dc0)(backBuffer, depthSurface);   // D3DDevice_SetRenderTarget
}

// Presents the frame, then sends again the states Swap leaves behind (8.2). Returns Device::Get(), the tail call.
// FUNC_AT(0x000e6640)
void* EAGL::RenderContext::EndFrame() {
    ((void (*)())0x000f52d0)();   // the profiler's frame mark

    // Unbind whatever is still bound from the render-target texture
    if (U32(kStageTexture) == U32(kRenderTargetTexture)) {
        SetTexture(0, NULL);
        U32(kStageTexture) = 0;
    }
    if (U32(kStageTexture + 4) == U32(kRenderTargetTexture)) {
        SetTexture(1, NULL);
        U32(kStageTexture + 4) = 0;
    }
    if (U32(kStageTexture + 8) == U32(kRenderTargetTexture)) {
        SetTexture(2, NULL);
        U32(kStageTexture + 8) = 0;
    }
    if (U32(kStageTexture + 12) == U32(kRenderTargetTexture)) {
        SetTexture(3, NULL);
        U32(kStageTexture + 12) = 0;
    }
    if (multiSampleType != 0x11) {   // polygon offset off for the swap
        SetRenderStateSimple(0x40384, 0);
        D3DState(0x0017575c, 0);
        SetRenderStateSimple(0x40388, 0);
        D3DState(0x00175760, 0);
    }
    DeviceGet();
    SetShaderConstantMode(0);
    Swap(0);
    SetShaderConstantMode(1);

    if (multiSampleType != 0x11) {
        uint32_t savedDither = U8(kSavedDither), savedFog = U8(kSavedFog);
        U32(kDirty) = U32(kDirty) | 0x3000;
        uint32_t dirty = U32(kDirty);
        D3DState(0x001757c4, savedDither);   // slot 103
        D3DState(0x00175798, savedFog);      // slot 92, fog enable
        dirty |= 0x2000;
        U32(kCacheFillMode) = 0xffffffffu;
        U32(kCacheCullEnable) = 0xffffffffu;
        U32(kCacheCullDirection) = 0xffffffffu;
        U32(kCacheAlphaTest) = 0xffffffffu;
        U32(kCacheTransparency) = 0xffffffffu;
        U32(kDirty) = dirty;
        dirty |= 0x2000;
        D3DState(0x0017579c, fogTableMode);
        U32(kDirty) = dirty;
        D3DState(0x001757a0, fogStart);
        dirty |= 0x2000;
        U32(kDirty) = dirty;
        D3DState(0x001757a4, fogEnd);
        dirty |= 0x2000;
        U32(kDirty) = dirty;
        D3DState(0x001757a8, fogDensity);
        SetRenderStateFogColor(fogColour);
        SetRenderStateStencilEnable(U32(kShadowStencilEnable));
        U32(kShadowColourMask) = 0xffffffffu;
        ((void (__stdcall *)(uint32_t))0x00168980)(U8(kYuvEnable));   // SetRenderState_YuvEnable
        U32(kCacheShading) = 0xffffffffu;
        ((void (__stdcall *)(uint32_t))0x0016af60)(U32(kPixelShader));    // D3DDevice_SetPixelShader
        ((void (__stdcall *)(uint32_t))0x0016ad90)(U32(kVertexShader));   // D3DDevice_SetVertexShader

        SetRenderStateSimple(0x4147c, 0);
        D3DState(0x00175774, 0);
        uint32_t v = U32(kCacheZSlope);
        SetRenderStateSimple(0x40384, v);
        D3DState(0x0017575c, v);
        v = U32(kCacheZOffset);
        SetRenderStateSimple(0x40388, v);
        D3DState(0x00175760, v);
        SetRenderStateSimple(0x41d78, 1);
        D3DState(0x00175770, 1);
        // FCOMP against 0.0f, TEST AH,0x44, JNP: the enables go off only when the offset is equal (and ordered)
        if (F32(kCacheZOffset) == F32(0x00189dec)) {
            SetRenderStateSimple(0x40330, 0);
            D3DState(0x00175764, 0);
            SetRenderStateSimple(0x40334, 0);
            D3DState(0x00175768, 0);
            SetRenderStateSimple(0x40338, 0);
            D3DState(0x0017576c, 0);
        } else {
            SetRenderStateSimple(0x40330, 1);
            D3DState(0x00175764, 1);
            SetRenderStateSimple(0x40334, 1);
            D3DState(0x00175768, 1);
            SetRenderStateSimple(0x40338, 1);
            D3DState(0x0017576c, 1);
        }
        SetRenderStateSimple(0x409f8, 4);
        D3DState(0x00175758, 4);
        v = U32(kCacheBlendColour);
        SetRenderStateSimple(0x4034c, v);
        D3DState(0x00175754, v);
        v = U32(kCacheBlendSource);
        SetRenderStateSimple(0x40344, v);
        D3DState(0x00175720, v);
        v = U32(kCacheBlendDestination);
        SetRenderStateSimple(0x40348, v);
        D3DState(0x00175724, v);
        v = U32(kCacheDepthTest);
        SetRenderStateSimple(0x40354, v);
        D3DState(0x0017570c, v);
        v = U32(kCacheAlphaMethod);
        SetRenderStateSimple(0x4033c, v);
        D3DState(0x00175710, v);
        v = U32(kCacheAlphaCompare);
        SetRenderStateSimple(0x40340, v);
        D3DState(0x0017571c, v);
        v = U8(kShadowZWrites);
        SetRenderStateSimple(0x4035c, v);
        D3DState(0x00175728, v);
        v = U32(kCacheBlendOperation);
        SetRenderStateSimple(0x40350, v);
        D3DState(0x00175750, v);
        v = U32(kShadowStencilZFail);
        SetRenderStateSimple(0x40374, v);
        D3DState(0x00175738, v);
        v = U32(kShadowStencilZPass);
        SetRenderStateSimple(0x40378, v);
        D3DState(0x0017573c, v);
        v = U32(kShadowStencilFunc);
        SetRenderStateSimple(0x40364, v);
        D3DState(0x00175740, v);
        v = U32(kShadowStencilRef);
        SetRenderStateSimple(0x40368, v);
        D3DState(0x00175744, v);
    }

    // The texture headers over the buffers follow the buffers' data, which Swap moved
    if (frontAlias != NULL)
        ((uint32_t *)frontAlias)[1] = ((const uint32_t *)frontBuffer)[1];
    if (backAlias != NULL)
        ((uint32_t *)backAlias)[1] = ((const uint32_t *)backBuffer)[1];
    if (depthAlias != NULL)
        ((uint32_t *)depthAlias)[1] = ((const uint32_t *)depthSurface)[1];
    return DeviceGet();
}

// =================================================================================================================
// Sizes and buffer depths
// =================================================================================================================

// FUNC_AT(0x000e6a60)
void EAGL::RenderContext::SetSize(float width_, float height_) {
    width = Ftol((double)width_);
    height = Ftol((double)height_);
}

// What SetupFrameBuffers last set up, not what SetSize asked for.
// FUNC_AT(0x000e6a80)
void EAGL::RenderContext::GetSize(float *width_, float *height_) {
    *width_ = (float)currentWidth;
    *height_ = (float)currentHeight;
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
    if (depth == 0x10) {
        backBufferFormat = 5;
        frontBufferDepth = 0x10;
        backBufferDepth = 0x10;
    } else if (depth == 0x20) {
        backBufferFormat = 6;
        frontBufferDepth = 0x20;
        backBufferDepth = 0x20;
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
    } else if (depth == 0x10) {
        zBufferDepth = 0x10;
        depthFormat = 0x2c;
    } else if (depth == 0x20) {
        zBufferDepth = depth;
        depthFormat = 0x2a;
    }
}

// FUNC_AT(0x000e6b50)
int EAGL::RenderContext::GetZBufferDepth() {
    return currentZBufferDepth;
}

// Off: immediate presentation. On: the swap interval's (1 -> 0, 2 -> 2, 3 -> 4, else 0). Resets the device if
// it exists. The refresh rate is cleared either way.
// FUNC_AT(0x000e6b60)
void EAGL::RenderContext::SetSyncToVBL(uint8_t sync) {
    syncToVBL = sync;
    present.refreshRate = 0;
    if (sync == 0) {
        present.presentationInterval = 0x80000000u;
    } else {
        present.presentationInterval = 0;
        uint32_t interval = 0;
        switch (swapInterval) {
        case 1: interval = 0; break;
        case 2: interval = 2; break;
        case 3: interval = 4; break;
        default: break;
        }
        present.presentationInterval = interval;
    }
    if (U32(kDeviceCreated) != 0) {
        PersistDisplay();
        Reset(&present);
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
    if (U32(kDeviceCreated) == 0)
        ((void (__stdcall *)(uint32_t, uint32_t))0x00169460)(pushBufferSize, kickOffSize);   // D3D_SetPushBufferSize

    memset(&present, 0, sizeof(present));
    int32_t w = width, h = height;
    present.backBufferWidth = (uint32_t)w;
    present.backBufferHeight = (uint32_t)h;
    present.backBufferFormat = backBufferFormat;
    present.backBufferCount = 2;
    if (w == 0x500 && h == 0x2d0)
        present.flags = 0x50;   // 720p
    if (w == 0x780) {
        if (h == 0x438)
            present.flags = 0x30;   // 1080
        if (w == 0x780 && h == 0x21c)
            present.flags = 0xb0;   // 1080i field
    }
    if (wideScreen != 0)
        present.flags |= 0x10;
    if (presentFlag100 != 0 && w == 0x280)
        present.flags |= 0x100;
    present.windowed = 0;
    present.refreshRate = pal60 != 0 ? 0x3c : 0;
    if (syncToVBL == 0) {
        present.refreshRate = 0;
        present.presentationInterval = 0x80000000u;
    } else {
        uint32_t interval = 0;
        switch (swapInterval) {
        case 1: interval = 0; break;
        case 2: interval = 2; break;
        case 3: interval = 4; break;
        default: break;
        }
        present.presentationInterval = interval;
    }
    if (zBufferDepth != 0) {
        present.enableAutoDepthStencil = 1;
        present.autoDepthStencilFormat = depthFormat;
    } else {
        present.enableAutoDepthStencil = 0;
    }
    present.swapEffect = 1;
    present.multiSampleType = multiSampleType;

    if (U32(kDeviceCreated) == 0) {
        int32_t hr = ((int32_t (__stdcall *)(uint32_t, uint32_t, void *, uint32_t, void *, void *))0x00169480)(
            0, 1, NULL, 0x10, &present, (void *)(uintptr_t)kDeviceCreated);   // Direct3D_CreateDevice
        if (hr < 0)
            return 0;
    } else {
        PersistDisplay();
        Reset(&present);
    }

    ((void (__stdcall *)(void *))0x00165e70)((void *)(uintptr_t)kGammaRamp);   // D3DDevice_GetGammaRamp
    SetShaderConstantMode(1);
    ((void (__stdcall *)(uint32_t))0x001677b0)(0x901);   // SetRenderState_CullMode
    SetRenderStateSimple(0x40304, 1);
    D3DState(0x00175714, 1);
    U32(kCacheTransparency) = 0xffffffffu;
    U32(kCacheCullEnable) = 0xffffffffu;
    U32(kCacheCullDirection) = 0xffffffffu;
    ((void (__stdcall *)(uint32_t))0x001687f0)(zEnable);   // SetRenderState_ZEnable
    SetRenderStateStencilEnable(stencilEnable);
    uint32_t v = stencilMask;
    SetRenderStateSimple(0x4036c, v);
    D3DState(0x00175748, v);
    v = stencilRef;
    SetRenderStateSimple(0x40368, v);
    D3DState(0x00175744, v);
    v = stencilFunc;
    SetRenderStateSimple(0x40364, v);
    D3DState(0x00175740, v);
    SetRenderStateStencilFail(stencilFail);
    v = stencilZPass;
    SetRenderStateSimple(0x40378, v);
    D3DState(0x0017573c, v);
    v = stencilZFail;
    SetRenderStateSimple(0x40374, v);
    D3DState(0x00175738, v);
    v = zWritesEnable;
    SetRenderStateSimple(0x4035c, v);
    D3DState(0x00175728, v);
    v = colourWriteMask;
    SetRenderStateSimple(0x40358, v);
    D3DState(0x00175734, v);
    v = ditherEnable;
    SetRenderStateSimple(0x40310, v);
    D3DState(0x0017572c, v);
    // FLD/FSTP of the two floats: passed as their bits (only a signalling NaN would differ, and none is stored)
    ((void (__stdcall *)(uint32_t, uint32_t))0x00167030)(screenSpaceOffsetX, screenSpaceOffsetY);
    U32(kCache198) = zEnable;
    U32(kShadowStencilEnable) = stencilEnable;
    U32(kShadowStencilMask) = stencilMask;
    U32(kShadowStencilRef) = stencilRef;
    U32(kShadowStencilFunc) = stencilFunc;
    U32(kShadowStencilFail) = stencilFail;
    U32(kShadowStencilZPass) = stencilZPass;
    U32(kShadowStencilZFail) = stencilZFail;
    U8(kShadowZWrites) = zWritesEnable;
    U32(kShadowColourMask) = colourWriteMask;
    U8(kSavedDither) = ditherEnable;
    U8(kSavedFog) = fogEnable;
    SetRenderStateShadowFunc(shadowFunc);

    // fog, slots 92..96
    uint32_t dirty = U32(kDirty) | 0x2000;
    U32(kDirty) = dirty;
    D3DState(0x00175798, fogEnable);
    dirty |= 0x2000;
    U32(kDirty) = dirty;
    D3DState(0x0017579c, fogTableMode);
    dirty |= 0x2000;
    U32(kDirty) = dirty;
    D3DState(0x001757a0, fogStart);
    dirty |= 0x2000;
    U32(kDirty) = dirty;
    D3DState(0x001757a4, fogEnd);
    dirty |= 0x2000;
    U32(kDirty) = dirty;
    D3DState(0x001757a8, fogDensity);
    SetRenderStateFogColor(fogColour);

    // points, slots 116..123
    dirty = U32(kDirty) | 0x100;
    U32(kDirty) = dirty;
    D3DState(0x001757f8, pointSize);
    dirty |= 0x100;
    U32(kDirty) = dirty;
    D3DState(0x001757fc, pointSizeMin);
    dirty |= 0x100;
    U32(kDirty) = dirty;
    D3DState(0x00175814, pointSizeMax);
    dirty |= 0x100;
    U32(kDirty) = dirty;
    D3DState(0x00175808, pointScaleA);
    dirty |= 0x100;
    U32(kDirty) = dirty;
    D3DState(0x0017580c, pointScaleB);
    dirty |= 0x100;
    U32(kDirty) = dirty;
    D3DState(0x00175810, pointScaleC);
    dirty |= 0x900;
    U32(kDirty) = dirty;
    D3DState(0x00175800, pointSpriteEnable);
    D3DState(0x00175804, pointScaleEnable);
    dirty |= 0x100;
    U32(kDirty) = dirty;

    currentWidth = width;
    currentHeight = height;
    currentFrontBufferDepth = frontBufferDepth;
    currentBackBufferDepth = backBufferDepth;
    currentZBufferDepth = zBufferDepth;
    // D3DDevice_Clear(0, NULL, target | Z | stencil, opaque black, 1.0f, 0)
    ((void (__stdcall *)(uint32_t, void *, uint32_t, uint32_t, uint32_t, uint32_t))0x00168c90)(
        0, NULL, 0xf3, 0xff000000u, 0x3f800000u, 0);
    SetShaderConstantMode(0);
    Swap(0);
    SetShaderConstantMode(1);
    backBuffer = GetBackBuffer2(0);
    depthSurface = ((void *(__stdcall *)())0x001666b0)();   // D3DDevice_GetDepthStencilSurface2
    frontBuffer = GetBackBuffer2(-1);

    for (uint8_t *p = *(uint8_t **)(uintptr_t)kVertexBufferList; p != NULL; p = *(uint8_t **)(p + 0x1c))
        ((void (*)(void *, int))0x000f11c0)(p, 0);   // EAGLInternal::RenderMethodConstructor
    for (uint8_t *p = *(uint8_t **)(uintptr_t)kRenderMethodList; p != NULL; p = *(uint8_t **)(p + 0x1c))
        ((void (*)(void *))0x000f0e50)(p);

    // Tiles 0 and 1 set again (tile 1 from a copy, as the original's by-value argument)
    uint32_t tile0[6], tile1[6], tileCopy[6];
    ((void (__stdcall *)(uint32_t, void *))0x00166060)(0, tile0);   // D3DDevice_GetTile
    ((void (__stdcall *)(uint32_t, void *))0x00166060)(1, tile1);
    ((void (__stdcall *)(uint32_t, const void *))0x00166d00)(0, NULL);   // D3DDevice_SetTile
    ((void (__stdcall *)(uint32_t, const void *))0x00166d00)(0, tile0);
    memcpy(tileCopy, tile1, sizeof(tileCopy));
    ((void (__stdcall *)(uint32_t, const void *))0x00166d00)(1, NULL);
    ((void (__stdcall *)(uint32_t, const void *))0x00166d00)(1, tileCopy);

    if (frontAlias == NULL)
        BuildSurfaceTexture(&frontAlias, frontBuffer, (const char *)0x001cb964u, false);
    if (frontTar == NULL)
        frontTar = MakeTar(frontAlias);
    if (backAlias == NULL)
        BuildSurfaceTexture(&backAlias, backBuffer, (const char *)0x001cb970u, false);
    if (backTar == NULL)
        backTar = MakeTar(backAlias);
    if (depthAlias == NULL)
        BuildSurfaceTexture(&depthAlias, depthSurface, (const char *)0x001cb97cu, true);
    if (depthTar == NULL)
        depthTar = MakeTar(depthAlias);
    return 1;
}

// =================================================================================================================
// RenderContext's own setters
// =================================================================================================================

// Method 0x40310 (D3D8's slot 65), shadowed in 0x0023ff10 - which EndFrame writes into slot 103.
// FUNC_AT(0x000e73a0)
bool EAGL::RenderContext::SetDitherEnable(uint8_t enable) {
    ditherEnable = enable;
    if (U8(kSavedDither) != enable) {
        U8(kSavedDither) = enable;
        if (U32(kDeviceCreated) != 0) {
            uint32_t v = ditherEnable;
            SetRenderStateSimple(0x40310, v);
            D3DState(0x0017572c, v);
        }
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
    if (U8(kShadowZWrites) != enable) {
        uint32_t device = U32(kDeviceCreated);
        U8(kShadowZWrites) = enable;
        if (device != 0) {
            uint32_t v = enable;
            SetRenderStateSimple(0x4035c, v);
            D3DState(0x00175728, v);
        }
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
    field030 = value;
    return true;
}

// FUNC_AT(0x000e7450)
bool EAGL::RenderContext::GetField30(uint32_t *value) {
    *value = field030;
    return true;
}

// 1..3 (else false). With the device and VBL sync, D3D8's slot 127 directly - and a write of the dirty flags to
// themselves, which the original makes (an OR with nothing).
// FUNC_AT(0x000e7460)
bool EAGL::RenderContext::SetSwapInterval(int interval) {
    if (interval <= 0 || interval > 3)
        return false;
    swapInterval = interval;
    uint32_t value = 0;
    switch (interval) {
    case 1: value = 0; break;
    case 2: value = 2; break;
    case 3: value = 4; break;
    }
    if (U32(kDeviceCreated) != 0 && syncToVBL != 0) {
        U32(kDirty) = U32(kDirty);
        D3DState(0x00175824, value);
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
    if (type == 0x11) {
        context->multiSampleAntiAlias = 0;
        return true;
    }
    context->multiSampleAntiAlias = 1;
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
    uint32_t value = context->stencilZFail;
    if (U32(kShadowStencilZFail) != value) {
        U32(kShadowStencilZFail) = value;
        if (U32(kDeviceCreated) != 0) {
            SetRenderStateSimple(0x40374, op);
            D3DState(0x00175738, op);
        }
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
    uint32_t value = context->stencilZPass;
    if (U32(kShadowStencilZPass) != value) {
        U32(kShadowStencilZPass) = value;
        if (U32(kDeviceCreated) != 0) {
            SetRenderStateSimple(0x40378, op);
            D3DState(0x0017573c, op);
        }
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
    uint32_t value = context->stencilFail;
    if (U32(kShadowStencilFail) != value) {
        U32(kShadowStencilFail) = value;
        if (U32(kDeviceCreated) != 0)
            SetRenderStateStencilFail(op);
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
    uint32_t value = context->stencilFunc;
    if (U32(kShadowStencilFunc) != value) {
        U32(kShadowStencilFunc) = value;
        if (U32(kDeviceCreated) != 0) {
            SetRenderStateSimple(0x40364, func);
            D3DState(0x00175740, func);
        }
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
    uint32_t value = context->stencilRef;
    if (U32(kShadowStencilRef) != value) {
        U32(kShadowStencilRef) = value;
        if (U32(kDeviceCreated) != 0) {
            SetRenderStateSimple(0x40368, ref);
            D3DState(0x00175744, ref);
        }
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
    uint32_t value = context->stencilMask;
    if (U32(kShadowStencilMask) != value) {
        U32(kShadowStencilMask) = value;
        if (U32(kDeviceCreated) != 0) {
            SetRenderStateSimple(0x4036c, mask);
            D3DState(0x00175748, mask);
        }
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
    uint32_t value = context->stencilWriteMask;
    if (U32(kShadowStencilWriteMask) != value) {
        U32(kShadowStencilWriteMask) = value;
        if (U32(kDeviceCreated) != 0) {
            SetRenderStateSimple(0x40360, mask);
            D3DState(0x0017574c, mask);
        }
    }
    return true;
}

// FUNC_AT(0x000e7730)
bool EAGL::RenderContextExtension::GetStencilWriteMask(uint32_t *mask) {
    *mask = context->stencilWriteMask;
    return true;
}

// ---- gamma (both unreferenced)

// The start-up ramp scaled per channel: (int)(entry * scale + 0.5) by __ftol2, 0xff when it overflows a byte.
// FUNC_AT(0x000e7740)
bool EAGL::RenderContextExtension::SetGamma(float red, float green, float blue) {
    uint8_t ramp[0x300];
    for (int i = 0; i < 0x100; i++) {
        int32_t v = Ftol((double)(int32_t)U8(kGammaRamp + i) * (double)red + (double)F32(0x00189eb0));
        if ((v & 0xff000000) != 0)
            v = 0xff;
        ramp[i] = (uint8_t)v;
        v = Ftol((double)(int32_t)U8(kGammaRamp + 0x100 + i) * (double)green + (double)F32(0x00189eb0));
        if ((v & 0xff000000) != 0)
            v = 0xff;
        ramp[0x100 + i] = (uint8_t)v;
        v = Ftol((double)(int32_t)U8(kGammaRamp + 0x200 + i) * (double)blue + (double)F32(0x00189eb0));
        if ((v & 0xff000000) != 0)
            v = 0xff;
        ramp[0x200 + i] = (uint8_t)v;
    }
    if (U32(kDeviceCreated) != 0)
        D3DSetGammaRamp(2, ramp);
    return true;
}

// FUNC_AT(0x000e7820)
bool EAGL::RenderContextExtension::SetGammaRamp(const uint8_t *source) {
    uint8_t ramp[0x300];
    for (int i = 0; i < 0x100; i++) {
        ramp[i] = source[i];
        ramp[0x100 + i] = source[0x100 + i];
        ramp[0x200 + i] = source[0x200 + i];
    }
    if (U32(kDeviceCreated) != 0)
        D3DSetGammaRamp(2, ramp);
    return true;
}

// Not compared with its shadow: the shadow is written and, with the device, the state sent every time.
// FUNC_AT(0x000e7890)
bool EAGL::RenderContextExtension::SetStencilEnable(uint8_t enable) {
    context->stencilEnable = enable;
    uint32_t value = context->stencilEnable;
    uint32_t device = U32(kDeviceCreated);
    U32(kShadowStencilEnable) = value;
    if (device != 0)
        SetRenderStateStencilEnable(enable);
    return true;
}

// FUNC_AT(0x000e78c0)
bool EAGL::RenderContextExtension::GetStencilEnable(uint8_t *enable) {
    *enable = context->stencilEnable;
    return true;
}

// The front buffer written to a file (XGWriteSurfaceToFile).
// FUNC_AT(0x000e78d0)
bool EAGL::RenderContextExtension::Screenshot(const char *path) {
    void *surface = GetBackBuffer2(-1);
    ((void (__stdcall *)(void *, const char *))0x0017a8ee)(surface, path);
    Release(surface);
    return true;
}

// The colour write mask (method 0x40358).
// FUNC_AT(0x000e7900)
bool EAGL::RenderContextExtension::SetRenderMask(uint32_t mask) {
    context->colourWriteMask = mask;
    uint32_t value = context->colourWriteMask;
    if (U32(kShadowColourMask) != value) {
        U32(kShadowColourMask) = value;
        if (U32(kDeviceCreated) != 0) {
            SetRenderStateSimple(0x40358, mask);
            D3DState(0x00175734, mask);
        }
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x2000;
        D3DState(0x00175798, enable);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) |= 0x2000;
        D3DState(0x0017579c, mode);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x2000;
        D3DState(0x001757a0, start);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x2000;
        D3DState(0x001757a4, end);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x2000;
        D3DState(0x001757a8, density);
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
    if (U32(kDeviceCreated) != 0)
        SetRenderStateFogColor(colour);
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
    if (U32(kDeviceCreated) != 0) {
        context->present.flags |= 0x10;
        PersistDisplay();
        Reset(&context->present);
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
    if (U32(kDeviceCreated) != 0) {
        context->present.flags |= 0x100;
        PersistDisplay();
        Reset(&context->present);
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
    if (U32(kDeviceCreated) != 0)
        ((void (__stdcall *)(uint32_t))0x001660e0)(enable);   // D3DDevice_SetSoftDisplayFilter
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
    if (U32(kDeviceCreated) != 0)
        ((void (__stdcall *)(int))0x00166090)(context->flickerFilter);   // D3DDevice_SetFlickerFilter
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
    if (U32(kDeviceCreated) != 0)
        SetRenderStateShadowFunc(func);
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
    if (U32(kDeviceCreated) == 0)
        return false;
    ((void (__stdcall *)())0x00166b40)();   // D3DDevice_BeginVisibilityTest
    return true;
}

// FUNC_AT(0x000e7c80)
bool EAGL::RenderContextExtension::EndVisibilityTest(uint32_t index) {
    if (U32(kDeviceCreated) == 0)
        return false;
    ((void (__stdcall *)(uint32_t))0x00166be0)(index);   // D3DDevice_EndVisibilityTest
    return true;
}

// The result is stored whatever the call returns; a failure (D3DERR_TESTINCOMPLETE among them) then zeroes it and
// answers false. src/common/gfx/d3d9Backend.cpp relies on this.
// FUNC_AT(0x000e7ca0)
bool EAGL::RenderContextExtension::GetVisibilityTestResult(uint32_t index, uint32_t *result) {
    if (U32(kDeviceCreated) == 0) {
        *result = 0;
        return false;
    }
    uint32_t pixels = 0;
    int32_t hr = ((int32_t (__stdcall *)(uint32_t, uint32_t *, void *))0x00165fd0)(index, &pixels, NULL);
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
    void *surface = GetBackBuffer2(-1);
    SurfaceDesc desc;
    SurfaceGetDesc(surface, &desc);
    if (desc.size != (desc.height * desc.width) << 2) {
        Release(surface);
        return false;
    }
    struct {
        int32_t pitch;
        void *bits;
    } locked;
    ((void (__stdcall *)(void *, void *, void *, uint32_t))0x00167240)(surface, &locked, NULL, 0x40);   // LockRect
    ((void (*)(void *, const void *, uint32_t))0x0010a5b0)(destination, locked.bits, desc.size);    // MEM_copy
    Release(surface);
    return true;
}

// Two globals with a setter and getter each; the first is set by RRenderHigh::Render, read as a float.
// FUNC_AT(0x000e7d70)
bool EAGL::RenderContextExtension::SetGlobal23ff0c(uint32_t value) {
    U32(kGlobal23ff0c) = value;
    return true;
}

// FUNC_AT(0x000e7d80)
bool EAGL::RenderContextExtension::GetGlobal23ff0c(float *value) {
    *value = F32(kGlobal23ff0c);
    return true;
}

// FUNC_AT(0x000e7da0)
bool EAGL::RenderContextExtension::SetGlobal1cb938(uint32_t value) {
    U32(kShadow1cb938) = value;
    return true;
}

// FUNC_AT(0x000e7db0)
bool EAGL::RenderContextExtension::GetGlobal1cb938(uint32_t *value) {
    *value = U32(kShadow1cb938);
    return true;
}

// FUNC_AT(0x000e7dd0)
bool EAGL::RenderContextExtension::SetMultiSampleAntiAlias(uint8_t enable) {
    context->multiSampleAntiAlias = enable;
    if (U32(kDeviceCreated) != 0)
        ((void (__stdcall *)(uint32_t))0x00168b70)(enable);   // SetRenderState_MultiSampleAntiAlias
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
    if (U32(kDeviceCreated) != 0)
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
    context->screenSpaceOffsetX = Bits(x);
    context->screenSpaceOffsetY = Bits(y);
    if (U32(kDeviceCreated) != 0)
        ((void (__stdcall *)(uint32_t, uint32_t))0x00167030)(Bits(x), Bits(y));   // SetScreenSpaceOffset
    return true;
}

// FUNC_AT(0x000e7eb0)
bool EAGL::RenderContextExtension::GetScreenSpaceOffset(float *x, float *y) {
    memcpy(x, &context->screenSpaceOffsetX, 4);
    memcpy(y, &context->screenSpaceOffsetY, 4);
    return true;
}

// ---- point sprites: D3D8's deferred point states written directly (slots 116..123, dirty flag 0x100)

// FUNC_AT(0x000e7ee0)
bool EAGL::RenderContextExtension::SetPointSize(uint32_t size) {
    context->pointSize = size;
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x100;
        D3DState(0x001757f8, size);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x100;
        D3DState(0x001757fc, size);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x100;
        D3DState(0x00175814, size);
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
    if (U32(kDeviceCreated) != 0) {
        D3DState(0x0017580c, b);
        uint32_t dirty = U32(kDirty) | 0x100;
        D3DState(0x00175808, a);
        U32(kDirty) = dirty;
        D3DState(0x00175810, c);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x900;
        D3DState(0x00175800, enable);
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
    if (U32(kDeviceCreated) != 0) {
        U32(kDirty) = U32(kDirty) | 0x100;
        D3DState(0x00175804, enable);
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
    if ((((uint32_t (__stdcall *)())0x0010e02b)() & 0x40) != 0) {   // XGetVideoFlags
        context->pal60 = 1;
        return context->pal60;
    }
    context->pal60 = 0;
    return context->pal60;
}

// FUNC_AT(0x000e8190)
bool EAGL::RenderContextExtension::IsDeviceCreated(uint32_t) {
    return U32(kDeviceCreated) != 0;
}

// The front buffer copied into a texture of its own (made the first time, with a TAR over it in the back
// buffer's TAR slot if that is still empty), returning that slot.
// FUNC_AT(0x000e81a0)
void* EAGL::RenderContextExtension::CopyBackBuffer() {
    void *source = GetBackBuffer2(0);
    SurfaceDesc desc;
    SurfaceGetDesc(source, &desc);
    RenderContext *owner = context;
    if (owner->copyTexture == NULL) {
        owner->copyTexture = ((void *(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                   uint32_t))0x00167260)(desc.width, desc.height, 1, 1, 0,
                                                                         desc.format, 3);   // CreateTexture2
        if (context->backTar == NULL)
            context->backTar = MakeTar(context->copyTexture);
    }
    void *destination = ((void *(__stdcall *)(void *, uint32_t))0x00167330)(context->copyTexture, 0);   // GetSurfaceLevel2
    struct {
        int32_t left, top, right, bottom;
    } rect = {0, 0, (int32_t)desc.width, (int32_t)desc.height};
    struct {
        int32_t x, y;
    } point = {0, 0};
    ((void (__stdcall *)(void *, void *, uint32_t, void *, void *))0x001663e0)(source, &rect, 1, destination,
                                                                              &point);   // D3DDevice_CopyRects
    Release(destination);
    Release(source);
    return context->backTar;
}

// The buffers as TARs, the texture headers and TARs built the first time (SetupFrameBuffers has normally built
// them already).
// FUNC_AT(0x000e8270)
void* EAGL::RenderContextExtension::GetFrontBuffer() {
    if (context->frontAlias == NULL)
        BuildSurfaceTexture(&context->frontAlias, context->frontBuffer, (const char *)0x001cb988u, false);
    if (context->frontTar == NULL) {
        context->frontTar = MakeTar(context->frontAlias);
        return context->frontTar;
    }
    return context->frontTar;
}

// FUNC_AT(0x000e8350)
void* EAGL::RenderContextExtension::GetBackBuffer() {
    if (context->backAlias == NULL)
        BuildSurfaceTexture(&context->backAlias, context->backBuffer, (const char *)0x001cb994u, false);
    if (context->backTar == NULL) {
        context->backTar = MakeTar(context->backAlias);
        return context->backTar;
    }
    return context->backTar;
}

// FUNC_AT(0x000e8430)
void* EAGL::RenderContextExtension::GetDepthBuffer() {
    if (context->depthAlias == NULL)
        BuildSurfaceTexture(&context->depthAlias, context->depthSurface, (const char *)0x001cb9a0u, true);
    if (context->depthTar == NULL) {
        context->depthTar = MakeTar(context->depthAlias);
        return context->depthTar;
    }
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
        Release(context->copyTexture);
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

// Every render method and vertex buffer is destroyed with the context (EAGL has one), then the viewports, the
// three surfaces and the copy texture. The texture headers and TARs over the buffers are not freed (as the
// original).
// FUNC_AT(0x000e8600)
void EAGL::RenderContext::Destruct() {
    for (uint8_t *p = *(uint8_t **)(uintptr_t)kVertexBufferList; p != NULL; p = *(uint8_t **)(p + 0x1c))
        ((void (*)(void *))0x000f1390)(p);   // EAGLInternal::RenderMethodDestructor
    for (uint8_t *p = *(uint8_t **)(uintptr_t)kRenderMethodList; p != NULL; p = *(uint8_t **)(p + 0x1c))
        ((void (*)(void *))0x000f1210)(p);
    while (viewPorts != NULL)
        ((void (__fastcall *)(void *, int, void *))0x000ee0a0)(this, 0, viewPorts);   // DeleteViewPort
    if (depthSurface != NULL)
        Release(depthSurface);
    if (frontBuffer != NULL)
        Release(frontBuffer);
    if (backBuffer != NULL)
        Release(backBuffer);
    RenderContext *owner = (RenderContext *)extension;   // the extension's back pointer: this object
    if (owner->copyTexture != NULL) {
        Release(owner->copyTexture);
        ((RenderContext *)extension)->copyTexture = NULL;
    }
}

// FUNC_AT(0x000e86f0)
EAGL::RenderContextPrivate* EAGL::RenderContextPrivate::Construct(RenderContext *owner_) {
    uint8_t *self = (uint8_t *)this;
    owner = owner_;
    self[0x28] = 1;                                   // object +0x2c, sync to VBL
    *(uint32_t *)(self + 0x70) = 0;                   // object +0x74
    *(uint32_t *)(self + 0xf8) = 0;                   // object +0xfc..+0x10c
    *(uint32_t *)(self + 0xfc) = 0;
    *(uint32_t *)(self + 0x100) = 0;
    *(uint32_t *)(self + 0x104) = 0;
    *(uint32_t *)(self + 0x108) = 0;
    *(uint32_t *)(self + 0x134) = 0;                  // object +0x138..+0x144
    *(uint32_t *)(self + 0x138) = 0;
    *(uint32_t *)(self + 0x13c) = 0;
    *(uint32_t *)(self + 0x140) = 0;
    return this;
}

// The extension and private constructors inlined, then the defaults: 640x480, 32-bit buffers, no multisampling,
// sync to the VBL, Z on, stencil off (ALWAYS, ref 0, masks 0xffffffff/0xff, ops 0x1e00 KEEP), Z writes on, all
// colour channels, fog off (0..1), point states at D3D8's defaults with a maximum of 64.
// FUNC_AT(0x000e8740)
EAGL::RenderContext* EAGL::RenderContext::Construct(void *device_) {
    extension = (RenderContextExtension *)this;
    copyTexture = NULL;
    RenderContext *owner = (RenderContext *)extension;
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
    field144 = 0;
    device = device_;
    width = 0x280;
    currentWidth = 0x280;
    height = 0x1e0;
    currentHeight = 0x1e0;
    frontBufferDepth = 0x20;
    currentFrontBufferDepth = 0x20;
    backBufferDepth = 0x20;
    currentBackBufferDepth = 0x20;
    zBufferDepth = 0x20;
    currentZBufferDepth = 0x20;
    stencilZFail = 0x1e00;
    stencilFail = 0x1e00;
    zEnable = 1;
    multiSampleType = 0x11;
    syncToVBL = 1;
    ditherEnable = 0;
    stencilZPass = 0;
    stencilFunc = 0x207;
    stencilRef = 0;
    stencilMask = 0xffffffffu;
    stencilWriteMask = 0xff;
    stencilEnable = 0;
    zWritesEnable = 1;
    colourWriteMask = 0x1010101;
    multiSampleAntiAlias = 0;
    fogEnable = 0;
    fogTableMode = 0;
    fogStart = 0;
    fogEnd = 0x3f800000;       // 1.0f
    fogDensity = 0x3f800000;
    fogColour = 0;
    wideScreen = 0;
    presentFlag100 = 0;
    shadowFunc = 0x200;
    pushBufferSize = 0x80000;
    kickOffSize = 0x8000;
    screenSpaceOffsetX = 0;
    screenSpaceOffsetY = 0;
    pointSize = 0x3f800000;    // 1.0f
    pointSizeMin = 0x3f800000;
    pointSizeMax = 0x42800000; // 64.0f
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
