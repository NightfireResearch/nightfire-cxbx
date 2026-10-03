#include "GeoPrimState.h"
#include "D3D8State.h"
#include "RenderContext.h"
#include "../../helpers.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL::GeoPrimState (docs/driving/eagl.md 2.5, 4.5): the setters and getters, both constructors, and Apply - which
// sends what changed to the device. Apply keeps a cache of what it last sent (invalidated by
// RenderContext::EndFrame), sends each change through D3D8's render-state entry points, and writes D3D8's own
// render-state table as the inlined D3D8 code did - the backend reads that table at draw time (5.2). The order of
// the calls and writes is the original's.
//
// Each setter answers true, the two the Xbox build does not keep (texture coordinate type, chroma colour) false.
// ---------------------------------------------------------------------------------------------------------------

#define CurrentRenderContext (*(EAGL::RenderContext **)0x0023fb64)

namespace {

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

}  // namespace

// FUNC_AT(0x000ef050)
bool EAGL::GeoPrimState::Apply() {
    if (!textureEnable) {
        for (uint32_t stage = 0; stage < 4; stage++)
            if (StageTexture[stage] != NULL)
                D3DDevice_SetTexture(stage, NULL);
        StageTexture[3] = NULL;
        StageTexture[2] = NULL;
        StageTexture[1] = NULL;
        StageTexture[0] = NULL;
    }
    ApplyCache.textureEnable = textureEnable;
    if (ApplyCache.fillMode != fillMode) {
        ApplyCache.fillMode = fillMode;
        D3DDevice_SetRenderState_FillMode(fillMode);
    }
    if (ApplyCache.blendOperation != blendOperation) {
        ApplyCache.blendOperation = blendOperation;
        SendState(0x40350, kRsBlendOp, blendOperation);
    }
    if (!(ApplyCacheZSlopeScale == zSlopeScale)) {   // unordered (NaN) counts as changed
        ApplyCacheZSlopeScale = zSlopeScale;
        SendState(0x40384, kRsPolygonOffsetZSlopeScale, Bits(zSlopeScale));
    }
    if (!(ApplyCacheZOffset == zOffset)) {
        ApplyCacheZOffset = zOffset;
        SendState(0x40388, kRsPolygonOffsetZOffset, Bits(zOffset));
        uint32_t on = zOffset == 0.0f ? 0 : 1;   // the three polygon offset enables
        SendState(0x40330, kRsPointOffsetEnable, on);
        SendState(0x40334, kRsWireFrameOffsetEnable, on);
        SendState(0x40338, kRsSolidOffsetEnable, on);
    }
    if (Shadows.zWritesEnable != zWritesEnable) {
        if (zWritesEnable == 0xffffffff) {
            // The render context's own setting, sent again
            uint8_t enable;
            CurrentRenderContext->GetZWritesEnable(&enable);
            CurrentRenderContext->SetZWritesEnable(enable);
        } else {
            ApplyCache.zWritesEnable = zWritesEnable;
            Shadows.zWritesEnable = zWritesEnable & 1;
            SendState(0x4035c, kRsZWriteEnable, zWritesEnable);
        }
    }
    if (ApplyCache.blendColour != blendColour) {
        ApplyCache.blendColour = blendColour;
        SendState(0x4034c, kRsBlendColor, blendColour);
    }
    if (ApplyCache.cullEnable != cullEnable || ApplyCache.cullDirection != cullDirection) {
        ApplyCache.cullEnable = cullEnable;
        ApplyCache.cullDirection = cullDirection;
        D3DDevice_SetRenderState_CullMode(cullEnable ? cullDirection : 0);
    }
    if (ApplyCache.shading != shading) {
        ApplyCache.shading = shading;
        if (shading <= 2) {
            uint32_t mode = shading == 0 ? 0x1d00 : 0x1d01;   // D3DSHADE_FLAT, GOURAUD
            SendState(0x4037c, kRsShadeMode, mode);
            D3DRenderState[kRsSpecularEnable] = shading == 2 ? 1 : 0;
            D3DDirtyFlags |= 0x3000;
        }
    }
    if (ApplyCache.depthTestMethod != depthTestMethod) {
        ApplyCache.depthTestMethod = depthTestMethod;
        SendState(0x40354, kRsZFunc, depthTestMethod);
    }
    if (ApplyCache.alphaBlendMode != alphaBlendMode || ApplyCache.blendSource != blendSource ||
        ApplyCache.blendDestination != blendDestination) {
        ApplyCache.alphaBlendMode = alphaBlendMode;
        ApplyCache.blendSource = blendSource;
        ApplyCache.blendDestination = blendDestination;
        SendState(0x40344, kRsSrcBlend, blendSource);
        SendState(0x40348, kRsDestBlend, blendDestination);
    }
    if (ApplyCache.alphaTestEnable != alphaTestEnable) {
        ApplyCache.alphaTestEnable = alphaTestEnable;
        SendState(0x40300, kRsAlphaTestEnable, alphaTestEnable);
    }
    if (ApplyCache.alphaCompareValue != alphaCompareValue) {
        ApplyCache.alphaCompareValue = alphaCompareValue;
        SendState(0x40340, kRsAlphaRef, alphaCompareValue);
    }
    if (ApplyCache.alphaTestMethod != alphaTestMethod) {
        ApplyCache.alphaTestMethod = alphaTestMethod;
        SendState(0x4033c, kRsAlphaFunc, alphaTestMethod);
    }
    if (ApplyCache.transparencyMethod != transparencyMethod) {
        ApplyCache.transparencyMethod = transparencyMethod;
        if (transparencyMethod <= 1)
            SendState(0x40304, kRsAlphaBlendEnable, transparencyMethod);
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
    zSlopeScale = 0.0f;
    zOffset = 0.0f;
    blendColour = 0;
    zWritesEnable = 0xffffffff;
    return this;
}

// FUNC_AT(0x000eef10)
void EAGL::GeoPrimStateExtension::DumpState() {
}

// FUNC_AT(0x000ef480)
EAGL::GeoPrimState* EAGL::GeoPrimState::Construct() {
    static_cast<GeoPrimStateExtension *>(this)->Construct();
    return this;
}

// FUNC_AT(0x000ef490)
void EAGL::GeoPrimState::Destruct() {
}

// FUNC_AT(0x000ef4a0)
EAGL::GeoPrimState* EAGL::GeoPrimState::ConstructCopy(const GeoPrimState *other) {
    static_cast<GeoPrimStateExtension *>(this)->Construct();
    *this = *other;
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
    *reinterpret_cast<uint8_t *>(enable) = cullEnable;   // the byte as stored, not normalised to 0/1
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
    enum { kZero = 0, kOne = 1, kSrcAlpha = 0x302, kOneMinusSrcAlpha = 0x303, kDstColor = 0x306 };   // GL numbering
    enum { kAdd = 0x8006, kReverseSubtract = 0x800b };
    alphaBlendMode = mode;
    switch (mode) {
    case 0: blendSource = kOne; blendDestination = kZero; blendOperation = kAdd; break;
    case 1: blendSource = kSrcAlpha; blendDestination = kOneMinusSrcAlpha; blendOperation = kAdd; break;
    case 2: blendSource = kSrcAlpha; blendDestination = kOne; blendOperation = kAdd; break;
    case 3: blendSource = kOne; blendDestination = kOne; blendOperation = kReverseSubtract; break;
    case 4: blendSource = kSrcAlpha; blendDestination = kZero; blendOperation = kAdd; break;
    case 5: blendSource = kDstColor; blendDestination = kZero; blendOperation = kAdd; break;
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
    *reinterpret_cast<uint8_t *>(enable) = alphaTestEnable;   // the byte as stored, not normalised to 0/1
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
    *reinterpret_cast<uint8_t *>(enable) = textureEnable;   // the byte as stored, not normalised to 0/1
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
    zOffset = offset;
    return true;
}

// FUNC_AT(0x000eefd0)
bool EAGL::GeoPrimStateExtension::GetZOffset(float *offset) const {
    *offset = zOffset;
    return true;
}

// FUNC_AT(0x000eefe0)
bool EAGL::GeoPrimStateExtension::SetZSlopeScale(float scale) {
    zSlopeScale = scale;
    return true;
}

// FUNC_AT(0x000eeff0)
bool EAGL::GeoPrimStateExtension::GetZSlopeScale(float *scale) const {
    *scale = zSlopeScale;
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
    zWritesEnable = enable;
    return true;
}

// FUNC_AT(0x000ef030)
bool EAGL::GeoPrimStateExtension::GetZWritesEnable(bool *enable) const {
    *enable = zWritesEnable != 0;
    return true;
}
