#pragma fp_contract(off)

#include "PostProcessing.h"

#include <bit>
#include <string.h>

#include "Draw.h"
#include "Materials.h"
#include "OffscreenBuffer.h"
#include "Renderer.h"                     // fgRenderer
#include "TextureContext.h"
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../data/DebugVariables.h"
#include "../eagl/D3D8State.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/Loader.h"               // DynamicLoader::GetRegisteredVar
#include "../eagl/RenderContext.h"
#include "../eagl/View.h"
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/UMemory.hpp"
#include "../gfx/D3D8.h"

// ---------------------------------------------------------------------------------------------------------------
// RPostProcessing and its private data (0x000a4550-0x000a50c0), and the scorch marks' ground normal after them,
// ported from the listing. The glow's draw writes D3D8's point-sprite states directly, as the original's inlined
// D3D8 code does (eagl/D3D8State.h); the original's WBINVDs before handing the GPU new vertex data have nothing to
// do on a PC.
// ---------------------------------------------------------------------------------------------------------------

class RTextureContext;

static const USingletonVtable *const kPostProcessingVtable = (const USingletonVtable *)0x00192ec4;
static const USingletonVtable *const kSingletonVtable = (const USingletonVtable *)0x0018beb0;

#define BondPostGlare ((EAGL::RenderMethod *)0x00242e84)    // "HB_RM = BondPostGlare", made by a static initialiser
#define OffOnNames ((const char *const *)0x001b6638)        // "Off", "On"

// Statics the code reads and nothing writes (0x001c4f50, 0x001c5068, 0x001c506c, 0x001c5070)
constexpr int32_t kGlowIntensity = 24;              // in 255ths
constexpr int32_t kGlowPointSize = 17;
constexpr uint32_t kGlowGrey = 0xa5;                // the glow sprite's colour, each of red, green and blue
constexpr float kGlowScale = 0.041f;

constexpr float kByteToUnit = 1.0f / 255.0f;
static_assert(std::bit_cast<uint32_t>(kByteToUnit) == 0x3b808081, "1/255 as the original's constant");
static_assert(std::bit_cast<uint32_t>(kGlowScale) == 0x3d27ef9e, "the glow's scale as the original's static");

constexpr uint32_t kTexture = 0x53726163;           // 'carS'
constexpr uint32_t kMultiSampleNone = 0x11;
constexpr uint32_t kLockReadOnly = 0x80;            // D3DLOCK_READONLY
constexpr uint32_t kClearAll = 7;                   // colour, depth and stencil
constexpr uint32_t kMaskAll = 0x01010101;           // EAGL's render mask: alpha, red, green, blue
constexpr uint32_t kMaskColour = 0x00010101;        // red, green, blue
constexpr uint32_t kDarken = 0xa0000000;            // black, alpha 0xa0
constexpr int kBufferDepth = 0x20;

// GeoPrimState's depth comparisons, in OpenGL's numbering
constexpr uint32_t kDepthGreaterEqual = 0x206;
constexpr uint32_t kDepthAlways = 0x207;

// D3D8's dirty flags for the point-sprite states (eagl.md 5.1)
constexpr uint32_t kDirtyPointParams = 0x100;
constexpr uint32_t kDirtyPointSprites = 0x900;
constexpr float kPointScaleA = 10.0f;

// ---- the private data

// FUNC_AT(0x000a4580)
GlowGeoPrim* GlowGeoPrim::Construct() {
    EAGL::RenderMethod *made = static_cast<EAGL::RenderMethod *>(EaglMalloc(sizeof(EAGL::RenderMethod),
                                                                            "EAGL::Rendermethod new"));
    method = made != NULL ? made->ConstructChild(BondPostGlare) : NULL;
    for (int i = kGlowParamState; i <= kGlowParamCount; i++) {
        params[i].count = 0;
        params[i].data = NULL;
    }
    bool found;
    void *matrix = DynamicLoader::GetRegisteredVar("EAGL::ViewPort::gpModelViewProjectionMatrix", &found);
    for (int i = kGlowParamConstants; i <= kGlowParamColours; i++) {
        params[i].count = 0;
        params[i].data = NULL;
    }
    params[kGlowParamMatrix].data = static_cast<uint8_t *>(matrix);
    params[kGlowParamMatrix].count = 1;
    return this;
}

// FUNC_AT(0x000a4630)
RPostProcessingData* RPostProcessingData::Construct() {
    state.Construct();
    model.Construct();
    prim.Construct();
    return this;
}

// FUNC_AT(0x000a4690)
void RPostProcessingData::Destruct() {
    EAGL::RenderMethod *method = prim.method;
    if (method != NULL) {
        method->Destruct();
        EaglFree(method, sizeof(EAGL::RenderMethod));
    }
    model.Destruct();
    state.Destruct();
}

// FUNC_AT(0x000a4710)
void RPostProcessingData::DrawGlowPoints(uint32_t count, GlowPoint *glowPoints, void *colours) {
    ROffscreenBuffer *glow = buffers[2];
    glow->context->BeginFrame();
    glow->viewPort->BeginView();
    if (clearPending) {
        fgRenderer->renderContext->extension->SetRenderMask(kMaskAll);
        buffers[2]->viewPort->ClearViewPort(kClearAll);
        clearPending = false;
    }
    SimpleMaterial->SetTransparencyMethod(1);
    SimpleMaterial->SetAlphaBlendMode(1);
    SimpleMaterial->SetAlphaTestEnable(false);
    fgRenderer->renderContext->extension->SetRenderMask(kMaskColour);
    Draw::DrawBox(0.0f, 0.0f, (float)kGlowSize, (float)kGlowSize, kDarken);

    if (count != 0) {
        D3DDirtyFlags |= kDirtyPointParams;
        D3DRenderState[kRsPointSize] = std::bit_cast<uint32_t>((float)kGlowPointSize);
        D3DRenderState[kRsPointScaleA] = std::bit_cast<uint32_t>(kPointScaleA);
        prim.params[kGlowParamColours].data = static_cast<uint8_t *>(colours);
        prim.params[kGlowParamPoints].data = reinterpret_cast<uint8_t *>(glowPoints);
        prim.params[kGlowParamPoints].count = count;
        prim.params[kGlowParamColours].count = count;
        pointCount = count;
        D3DDirtyFlags |= kDirtyPointSprites;
        D3DRenderState[kRsPointSpriteEnable] = 1;
        model.Draw();
        D3DDirtyFlags |= kDirtyPointSprites;
        D3DRenderState[kRsPointSpriteEnable] = 0;
    }

    buffers[2]->viewPort->EndView();
    buffers[2]->context->EndFrame();
}

// FUNC_AT(0x000a48b0)
void RPostProcessingData::Draw() {
    SimpleTexturedMaterial->SetDepthTestMethod(kDepthAlways);
    SimpleTexturedMaterial->SetAlphaBlendMode(2);
    SimpleTexturedMaterial->SetTransparencyMethod(1);
    SimpleTexturedMaterial->SetAlphaTestEnable(false);
    uint32_t colour = 0xff000000 | kGlowGrey << 16 | kGlowGrey << 8 | kGlowGrey;
    fgRenderer->renderContext->SetZWritesEnable(0);
    Draw::DrawSprite(0.0f, 0.0f, (float)fgRenderer->screenWidth, (float)fgRenderer->screenHeight, colour,
                     buffers[2]->colourTarget);
    SimpleTexturedMaterial->SetDepthTestMethod(kDepthGreaterEqual);
    fgRenderer->renderContext->SetZWritesEnable(1);
    NullFunction();     // called on each buffer in turn: an empty method
    NullFunction();
    NullFunction();
}

// FUNC_AT(0x000a4ee0)
void RPostProcessingData::UpdateGlow() {
    if (needsLock) {
        needsLock = false;
        D3DLockedRect locked;
        D3DTexture_LockRect(buffers[0]->texture, 0, &locked, NULL, kLockReadOnly);
        texels = locked.bits;
    }
    double intensity = (double)kGlowIntensity * kByteToUnit;
    glowConstants[1] = kGlowScale;
    glowConstants[0] = (float)intensity;
    glowConstants[2] = 1.0f;
    glowConstants[3] = (float)(1.0 / intensity);
    DrawGlowPoints(kGlowPoints, points, texels);
}

// ---- RPostProcessing

// FUNC_AT(0x000a4550)
void RPostProcessing::Reset() {
    data->clearPending = true;
}

// FUNC_AT(0x000a4560)
EAGL::TAR* RPostProcessing::GetSubsampledBackBuffer() {
    if (data == NULL || data->buffers[1] == NULL)
        return NULL;
    return data->buffers[1]->colourTarget;
}

// FUNC_AT(0x000a49a0)
RPostProcessing* RPostProcessing::Construct() {
    singleton.vtable = kPostProcessingVtable;
    RPostProcessingData *made = static_cast<RPostProcessingData *>(OperatorNew(sizeof(RPostProcessingData)));
    data = made != NULL ? made->Construct() : NULL;
    data->clearPending = true;
    data->doGlow = false;
    data->texture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kTexture, 0);
    data->frontBuffer = fgRenderer->renderContext->extension->GetFrontBuffer();
    data->backBuffer = fgRenderer->renderContext->extension->GetBackBuffer();

    data->state.SetPrimitiveType(1);
    data->state.SetTransparencyMethod(1);
    data->state.SetAlphaBlendMode(2);
    data->state.SetDepthTestMethod(kDepthAlways);
    data->state.SetAlphaTestEnable(false);
    data->pointCount = 0;
    data->unknown68 = 0;
    data->unknown6C = 1;
    data->unknown70 = 1;
    EAGL::GeoPrimParam *params = data->prim.params;
    params[kGlowParamTexture].data = reinterpret_cast<uint8_t *>(data->texture);
    params[kGlowParamTexture].count = 1;
    params[kGlowParamState].data = reinterpret_cast<uint8_t *>(&data->state);
    params[kGlowParamState].count = 1;
    params[kGlowParamCount].data = reinterpret_cast<uint8_t *>(&data->pointCount);
    params[kGlowParamCount].count = 1;
    params[kGlowParamConstants].data = reinterpret_cast<uint8_t *>(data->glowConstants);
    params[kGlowParamConstants].count = 1;
    data->model.AddGeoPrim(reinterpret_cast<EAGL::GeoPrim *>(&data->prim));
    data->needsLock = true;

    // Each texel's place on the glow buffer: the swizzled texture's texel at each linear place, unswizzled
    uint32_t *swizzled = static_cast<uint32_t *>(OperatorNewArray(kGlowPoints * sizeof(uint32_t)));
    uint32_t *linear = static_cast<uint32_t *>(OperatorNewArray(kGlowPoints * sizeof(uint32_t)));
    for (uint32_t i = 0; i < kGlowPoints; i++)
        swizzled[i] = i;
    XGUnswizzleRect(swizzled, kGlowSize, kGlowSize, 0, linear, 0, NULL, sizeof(uint32_t));
    for (uint32_t y = 0; y < kGlowSize; y++) {
        for (uint32_t x = 0; x < kGlowSize; x++) {
            float *point = data->points[linear[y * kGlowSize + x]];
            point[0] = (float)x;
            point[1] = (float)y;
            point[2] = 1.0f;
        }
    }
    OperatorDelete(swizzled);
    OperatorDelete(linear);
    dbattrib_bool("Do glow", &data->doGlow, 0, 1, 0, -1.0f, OffOnNames);

    for (int i = 0; i < 3; i++) {
        ROffscreenBuffer *buffer = static_cast<ROffscreenBuffer *>(OperatorNew(sizeof(ROffscreenBuffer)));
        data->buffers[i] = buffer != NULL ? buffer->Construct(kGlowSize, kGlowSize, kBufferDepth, false, i == 1)
                                          : NULL;
    }
    return this;
}

// FUNC_AT(0x000a4eb0)
void RPostProcessing::Kill() {
    RPostProcessing *postProcessing = ThePostProcessing;
    if (postProcessing != NULL) {
        typedef RPostProcessing *(RPostProcessing::*DeletingDestructor)(unsigned flags);
        (postProcessing->*XbeVirtual<DeletingDestructor>(postProcessing, 0))(1);
    }
}

// FUNC_AT(0x000a4ed0)
void RPostProcessing::Draw() {
    data->Draw();
}

// FUNC_AT(0x000a4f70)
void RPostProcessing::Destruct() {
    singleton.vtable = kPostProcessingVtable;
    for (int i = 0; i < 3; i++) {
        ROffscreenBuffer *buffer = data->buffers[i];
        if (buffer != NULL) {
            buffer->Destruct();
            OperatorDelete(buffer);
        }
    }
    if (data != NULL) {
        data->Destruct();
        OperatorDelete(data);
    }
    singleton.vtable = kSingletonVtable;
}

// FUNC_AT(0x000a5020)
void RPostProcessing::GrabBackBuffer() {
    uint32_t multiSample;
    fgRenderer->renderContext->extension->GetMultiSampleType(&multiSample);
    data->buffers[1]->MakeCopy(data->backBuffer, multiSample != kMultiSampleNone ? 2 : 1);
    data->UpdateGlow();
}

// FUNC_AT(0x000a5060)
RPostProcessing* RPostProcessing::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RPostProcessing));
    return this;
}

