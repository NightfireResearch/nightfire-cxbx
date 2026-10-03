#include "GeoPrimState.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL::GeoPrimState (docs/driving/eagl.md 2.5, 4.5): the setters and getters, both constructors, and Apply - which
// sends what changed to the device. Apply keeps a cache of what it last sent (0x001cd18c..0x001cd1d4, invalidated
// by RenderContext::EndFrame), sends each change through D3D8's render-state entry points, and writes D3D8's own
// render-state table (0x001756xx) as the inlined D3D8 code did - the backend reads that table at draw time (5.2).
// The order of the calls and writes is the original's.
//
// Each setter answers true, the two the Xbox build does not keep (texture coordinate type, chroma colour) false.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// D3DDevice_SetRenderState_Simple takes the push-buffer method in ECX and the value in EDX: __fastcall's registers.
inline void SetRenderStateSimple(uint32_t method, uint32_t value) {
    ((void (__fastcall *)(uint32_t, uint32_t))0x001673e0)(method, value);
}

inline void SetTexture(uint32_t stage, void *texture) {
    ((void (__stdcall *)(uint32_t, void *))0x00166830)(stage, texture);   // D3DDevice_SetTexture
}

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}

inline float &F32(uint32_t address) {
    return *(float *)(uintptr_t)address;
}

// Writes the value into D3D8's render-state table, where the inlined SetRenderState left it.
inline void D3DState(uint32_t address, uint32_t value) {
    U32(address) = value;
}

inline uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// What Apply last sent
const uint32_t kCacheShading = 0x001cd18c, kCacheCullEnable = 0x001cd190, kCacheCullDirection = 0x001cd194,
               kCacheDepthTest = 0x001cd19c, kCacheBlendMode = 0x001cd1a0, kCacheAlphaTest = 0x001cd1a4,
               kCacheAlphaCompare = 0x001cd1a8, kCacheAlphaMethod = 0x001cd1ac, kCacheTextureEnable = 0x001cd1b0,
               kCacheTransparency = 0x001cd1b8, kCacheFillMode = 0x001cd1bc, kCacheBlendOperation = 0x001cd1c0,
               kCacheBlendSource = 0x001cd1c4, kCacheBlendDestination = 0x001cd1c8, kCacheBlendColour = 0x001cd1cc,
               kCacheZWrites = 0x001cd1d0, kCacheZSlope = 0x00240150, kCacheZOffset = 0x00240154;
// The texture bound per stage (opcode 15 and TAR::Use keep it)
const uint32_t kStageTexture = 0x0023ff80;
// RenderContextExtension's shadow of the Z write state (0x001cb938.. block)
const uint32_t kShadowZWrites = 0x001cb94c;

}  // namespace

// FUNC_AT(0x000ef050)
bool EAGL::GeoPrimState::Apply() {
    if (textureEnable == 0) {
        for (uint32_t stage = 0; stage < 4; stage++)
            if (U32(kStageTexture + stage * 4) != 0)
                SetTexture(stage, NULL);
        U32(kStageTexture + 12) = 0;
        U32(kStageTexture + 8) = 0;
        U32(kStageTexture + 4) = 0;
        U32(kStageTexture) = 0;
    }
    U8(kCacheTextureEnable) = textureEnable;
    if (U32(kCacheFillMode) != fillMode) {
        U32(kCacheFillMode) = fillMode;
        ((void (__stdcall *)(uint32_t))0x00167ad0)(fillMode);   // D3DDevice_SetRenderState_FillMode
    }
    if (U32(kCacheBlendOperation) != blendOperation) {
        U32(kCacheBlendOperation) = blendOperation;
        uint32_t v = blendOperation;
        SetRenderStateSimple(0x40350, v);
        D3DState(0x00175750, v);
    }
    if (!(F32(kCacheZSlope) == zSlopeScale)) {   // unordered (NaN) counts as changed
        U32(kCacheZSlope) = Bits(zSlopeScale);
        uint32_t v = Bits(zSlopeScale);
        SetRenderStateSimple(0x40384, v);
        D3DState(0x0017575c, v);
    }
    if (!(F32(kCacheZOffset) == zOffset)) {
        U32(kCacheZOffset) = Bits(zOffset);
        uint32_t v = Bits(zOffset);
        SetRenderStateSimple(0x40388, v);
        D3DState(0x00175760, v);
        uint32_t on = zOffset == 0.0f ? 0 : 1;   // the three polygon offset enables
        SetRenderStateSimple(0x40330, on);
        D3DState(0x00175764, on);
        SetRenderStateSimple(0x40334, on);
        D3DState(0x00175768, on);
        SetRenderStateSimple(0x40338, on);
        D3DState(0x0017576c, on);
    }
    if ((uint32_t)U8(kShadowZWrites) != zWritesEnable) {
        if (zWritesEnable == 0xffffffffu) {
            // The render context's own setting, sent again (RenderContext::GetZWritesEnable / SetZWritesEnable)
            void *context = *(void **)0x0023fb64u;
            uint8_t enable;
            ((bool (__fastcall *)(void *, int, uint8_t *))0x000e7430)(context, 0, &enable);
            context = *(void **)0x0023fb64u;
            ((bool (__fastcall *)(void *, int, uint8_t))0x000e73f0)(context, 0, enable);
        } else {
            U32(kCacheZWrites) = zWritesEnable;
            U8(kShadowZWrites) = (uint8_t)(zWritesEnable & 1);
            uint32_t v = zWritesEnable;
            SetRenderStateSimple(0x4035c, v);
            D3DState(0x00175728, v);
        }
    }
    if (U32(kCacheBlendColour) != blendColour) {
        U32(kCacheBlendColour) = blendColour;
        uint32_t v = blendColour;
        SetRenderStateSimple(0x4034c, v);
        D3DState(0x00175754, v);
    }
    if (U32(kCacheCullEnable) != (uint32_t)cullEnable || U32(kCacheCullDirection) != cullDirection) {
        U32(kCacheCullEnable) = cullEnable;
        U32(kCacheCullDirection) = cullDirection;
        ((void (__stdcall *)(uint32_t))0x001677b0)(cullEnable != 0 ? cullDirection : 0);   // SetRenderState_CullMode
    }
    if (U32(kCacheShading) != shading) {
        U32(kCacheShading) = shading;
        if (shading <= 2) {
            uint32_t mode = shading == 0 ? 0x1d00 : 0x1d01;   // D3DSHADE_FLAT, GOURAUD
            SetRenderStateSimple(0x4037c, mode);
            D3DState(0x00175730, mode);
            D3DState(0x001757c4, shading == 2 ? 1 : 0);    // specular
            U32(0x00175424) |= 0x3000;                     // D3D8's dirty flags
        }
    }
    if (U32(kCacheDepthTest) != depthTestMethod) {
        U32(kCacheDepthTest) = depthTestMethod;
        uint32_t v = depthTestMethod;
        SetRenderStateSimple(0x40354, v);
        D3DState(0x0017570c, v);
    }
    if (U32(kCacheBlendMode) != alphaBlendMode || U32(kCacheBlendSource) != blendSource ||
        U32(kCacheBlendDestination) != blendDestination) {
        U32(kCacheBlendMode) = alphaBlendMode;
        U32(kCacheBlendSource) = blendSource;
        U32(kCacheBlendDestination) = blendDestination;
        uint32_t v = blendSource;
        SetRenderStateSimple(0x40344, v);
        D3DState(0x00175720, v);
        v = blendDestination;
        SetRenderStateSimple(0x40348, v);
        D3DState(0x00175724, v);
    }
    if (U32(kCacheAlphaTest) != (uint32_t)alphaTestEnable) {
        U32(kCacheAlphaTest) = alphaTestEnable;
        uint32_t v = alphaTestEnable;
        SetRenderStateSimple(0x40300, v);
        D3DState(0x00175718, v);
    }
    if (U32(kCacheAlphaCompare) != alphaCompareValue) {
        U32(kCacheAlphaCompare) = alphaCompareValue;
        uint32_t v = alphaCompareValue;
        SetRenderStateSimple(0x40340, v);
        D3DState(0x0017571c, v);
    }
    if (U32(kCacheAlphaMethod) != alphaTestMethod) {
        U32(kCacheAlphaMethod) = alphaTestMethod;
        uint32_t v = alphaTestMethod;
        SetRenderStateSimple(0x4033c, v);
        D3DState(0x00175710, v);
    }
    if (U32(kCacheTransparency) != transparencyMethod) {
        U32(kCacheTransparency) = transparencyMethod;
        if (transparencyMethod <= 1) {
            uint32_t v = transparencyMethod;
            SetRenderStateSimple(0x40304, v);
            D3DState(0x00175714, v);
        }
    }
    return true;
}

// ---- construction

// FUNC_AT(0x000eeea0)
EAGL::GeoPrimStateExtension* EAGL::GeoPrimStateExtension::Construct() {
    blendColour = 0;
    primitiveType = 5;
    shading = 1;
    cullEnable = 0;
    cullDirection = 0x901;
    depthTestMethod = 0x203;
    alphaBlendMode = 1;
    alphaTestEnable = 1;
    alphaCompareValue = 0x10;
    alphaTestMethod = 0x204;
    textureEnable = 1;
    blendSource = 0x302;
    blendDestination = 0x303;
    transparencyMethod = 1;
    fillMode = 0x1b02;
    blendOperation = 0x8006;
    memset(&zSlopeScale, 0, 4);
    memset(&zOffset, 0, 4);
    blendColour = 0;
    zWritesEnable = 0xffffffffu;
    return this;
}

// FUNC_AT(0x000eef10)
void EAGL::GeoPrimStateExtension::DumpState() {
}

// FUNC_AT(0x000ef480)
EAGL::GeoPrimState* EAGL::GeoPrimState::Construct() {
    ((GeoPrimStateExtension *)this)->Construct();
    return this;
}

// FUNC_AT(0x000ef490)
void EAGL::GeoPrimState::Destruct() {
}

// FUNC_AT(0x000ef4a0)
EAGL::GeoPrimState* EAGL::GeoPrimState::ConstructCopy(const GeoPrimState *other) {
    ((GeoPrimStateExtension *)this)->Construct();
    memcpy(this, other, sizeof(GeoPrimState));
    return this;
}

// ---- setters and getters

// FUNC_AT(0x000eec70)
bool EAGL::GeoPrimState::SetPrimitiveType(uint32_t type) {
    primitiveType = type;
    return true;
}

// FUNC_AT(0x000eec80)
bool EAGL::GeoPrimState::GetPrimitiveType(uint32_t *type) const {
    *type = primitiveType;
    return true;
}

// FUNC_AT(0x000eec90)
bool EAGL::GeoPrimState::SetShading(uint32_t value) {
    shading = value;
    return true;
}

// FUNC_AT(0x000eeca0)
bool EAGL::GeoPrimState::GetShading(uint32_t *value) const {
    *value = shading;
    return true;
}

// FUNC_AT(0x000eecb0)
bool EAGL::GeoPrimState::SetCullEnable(bool enable) {
    cullEnable = enable;
    return true;
}

// FUNC_AT(0x000eecc0)
bool EAGL::GeoPrimState::GetCullEnable(bool *enable) const {
    *(uint8_t *)enable = cullEnable;
    return true;
}

// FUNC_AT(0x000eecd0)
bool EAGL::GeoPrimState::SetDepthTestMethod(uint32_t method) {
    depthTestMethod = method;
    return true;
}

// FUNC_AT(0x000eece0)
bool EAGL::GeoPrimState::GetDepthTestMethod(uint32_t *method) const {
    *method = depthTestMethod;
    return true;
}

// 0 replace, 1 alpha blend, 2 additive, 3 reverse subtract, 4 alpha over black, 5 multiply by destination colour.
// Out of range: the mode is kept and nothing else changes - and the answer is still true.
// FUNC_AT(0x000eecf0)
bool EAGL::GeoPrimState::SetAlphaBlendMode(uint32_t mode) {
    alphaBlendMode = mode;
    switch (mode) {
    case 0: blendSource = 1; blendDestination = 0; blendOperation = 0x8006; break;
    case 1: blendSource = 0x302; blendDestination = 0x303; blendOperation = 0x8006; break;
    case 2: blendSource = 0x302; blendDestination = 1; blendOperation = 0x8006; break;
    case 3: blendSource = 1; blendDestination = 1; blendOperation = 0x800b; break;
    case 4: blendSource = 0x302; blendDestination = 0; blendOperation = 0x8006; break;
    case 5: blendSource = 0x306; blendDestination = 0; blendOperation = 0x8006; break;
    }
    return true;
}

// FUNC_AT(0x000eedb0)
bool EAGL::GeoPrimState::GetAlphaBlendMode(uint32_t *mode) const {
    *mode = alphaBlendMode;
    return true;
}

// FUNC_AT(0x000eedc0)
bool EAGL::GeoPrimState::SetAlphaTestEnable(bool enable) {
    alphaTestEnable = enable;
    return true;
}

// FUNC_AT(0x000eedd0)
bool EAGL::GeoPrimState::GetAlphaTestEnable(bool *enable) const {
    *(uint8_t *)enable = alphaTestEnable;
    return true;
}

// FUNC_AT(0x000eede0)
bool EAGL::GeoPrimState::SetAlphaCompareValue(uint32_t value) {
    alphaCompareValue = value;
    return true;
}

// FUNC_AT(0x000eedf0)
bool EAGL::GeoPrimState::GetAlphaCompareValue(uint32_t *value) const {
    *value = alphaCompareValue;
    return true;
}

// FUNC_AT(0x000eee00)
bool EAGL::GeoPrimState::SetAlphaTestMethod(uint32_t method) {
    alphaTestMethod = method;
    return true;
}

// FUNC_AT(0x000eee10)
bool EAGL::GeoPrimState::GetAlphaTestMethod(uint32_t *method) const {
    *method = alphaTestMethod;
    return true;
}

// FUNC_AT(0x000eee20)
bool EAGL::GeoPrimState::SetTextureEnable(bool enable) {
    textureEnable = enable;
    return true;
}

// FUNC_AT(0x000eee30)
bool EAGL::GeoPrimState::GetTextureEnable(bool *enable) const {
    *(uint8_t *)enable = textureEnable;
    return true;
}

// FUNC_AT(0x000eee40)
bool EAGL::GeoPrimState::SetTextureCoordType(uint32_t) {
    return false;
}

// FUNC_AT(0x000eee50)
bool EAGL::GeoPrimState::GetTextureCoordType(uint32_t *) const {
    return false;
}

// FUNC_AT(0x000eee60)
bool EAGL::GeoPrimState::SetTransparencyMethod(uint32_t method) {
    transparencyMethod = method;
    return true;
}

// FUNC_AT(0x000eee70)
bool EAGL::GeoPrimState::GetTransparencyMethod(uint32_t *method) const {
    *method = transparencyMethod;
    return true;
}

// FUNC_AT(0x000eee80)
bool EAGL::GeoPrimState::SetChromaColour(uint32_t) {
    return false;
}

// FUNC_AT(0x000eee90)
bool EAGL::GeoPrimState::GetChromaColour(uint32_t *) const {
    return false;
}

// ---- the Xbox extension

// FUNC_AT(0x000eef20)
bool EAGL::GeoPrimStateExtension::SetCullDirection(uint32_t direction) {
    cullDirection = direction;
    return true;
}

// FUNC_AT(0x000eef30)
bool EAGL::GeoPrimStateExtension::GetCullDirection(uint32_t *direction) const {
    *direction = cullDirection;
    return true;
}

// FUNC_AT(0x000eef40)
bool EAGL::GeoPrimStateExtension::SetFillMode(uint32_t mode) {
    fillMode = mode;
    return true;
}

// FUNC_AT(0x000eef50)
bool EAGL::GeoPrimStateExtension::GetFillMode(uint32_t *mode) const {
    *mode = fillMode;
    return true;
}

// FUNC_AT(0x000eef60)
bool EAGL::GeoPrimStateExtension::SetBlendOperation(uint32_t operation) {
    blendOperation = operation;
    return true;
}

// FUNC_AT(0x000eef70)
bool EAGL::GeoPrimStateExtension::GetBlendOperation(uint32_t *operation) const {
    *operation = blendOperation;
    return true;
}

// FUNC_AT(0x000eef80)
bool EAGL::GeoPrimStateExtension::SetAlphaBlend(uint32_t source, uint32_t destination, uint32_t operation) {
    blendSource = source;
    blendOperation = operation;
    blendDestination = destination;
    return true;
}

// FUNC_AT(0x000eefa0)
bool EAGL::GeoPrimStateExtension::GetAlphaBlend(uint32_t *source, uint32_t *destination, uint32_t *operation) const {
    *source = blendSource;
    *destination = blendDestination;
    *operation = blendOperation;
    return true;
}

// FUNC_AT(0x000eefc0)
bool EAGL::GeoPrimStateExtension::SetZOffset(float offset) {
    memcpy(&zOffset, &offset, 4);
    return true;
}

// FUNC_AT(0x000eefd0)
bool EAGL::GeoPrimStateExtension::GetZOffset(float *offset) const {
    memcpy(offset, &zOffset, 4);
    return true;
}

// FUNC_AT(0x000eefe0)
bool EAGL::GeoPrimStateExtension::SetZSlopeScale(float scale) {
    memcpy(&zSlopeScale, &scale, 4);
    return true;
}

// FUNC_AT(0x000eeff0)
bool EAGL::GeoPrimStateExtension::GetZSlopeScale(float *scale) const {
    memcpy(scale, &zSlopeScale, 4);
    return true;
}

// FUNC_AT(0x000ef000)
bool EAGL::GeoPrimStateExtension::SetBlendColour(uint32_t colour) {
    blendColour = colour;
    return true;
}

// FUNC_AT(0x000ef010)
bool EAGL::GeoPrimStateExtension::GetBlendColour(uint32_t *colour) const {
    *colour = blendColour;
    return true;
}

// FUNC_AT(0x000ef020)
bool EAGL::GeoPrimStateExtension::SetZWritesEnable(bool enable) {
    zWritesEnable = (uint8_t)enable;
    return true;
}

// FUNC_AT(0x000ef030)
bool EAGL::GeoPrimStateExtension::GetZWritesEnable(bool *enable) const {
    *(uint8_t *)enable = zWritesEnable != 0;
    return true;
}

