#include "Gain.h"

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../eagl/EaglGlobals.h"          // EaglFree
#include "../eagl/Loader.h"               // DynamicLoader
#include "../engine/UMemory.hpp"
#include "Materials.h"                    // NewChildRenderMethod
#include "PostProcessing.h"
#include "Renderer.h"

// ---------------------------------------------------------------------------------------------------------------
// RGain (0x0009de00-0x0009e150), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals
#define GainParentMethod ((EAGL::RenderMethod *)0x00242e50)       // the render method the GeoPrim inherits
// The screen quad: its four corners (0, 0), (w, 0), (0, h), (w, h), their texture coordinates, and their colours, a
// function-local static made white on the first draw (names ours)
#define GainPositions ((Coord4 *)0x001c4a40)
#define GainUVs ((float (*)[2])0x001c4a20)
#define GainColours ((uint32_t *)0x00200f30)
#define GainColoursGuard U32_AT(0x00200f40)

namespace {

constexpr uintptr_t kRGainVtable = 0x00192bdc;
constexpr uintptr_t kUSingletonVtable = 0x0018beb0;
constexpr uint32_t kPrimitiveTriangleStrip = 6;
constexpr uint32_t kDepthAlways = 0x207;
constexpr int kQuadVertices = 4;

enum GainParam {
    kParamState = 0,
    kParamTexture = 1,
    kParamVertexCount = 2,
    kParamGain = 5,
    kParamOffset = 6,
    kParamPositions = 7,
    kParamUVs = 8,
    kParamColours = 9,
};

} // namespace

// FUNC_AT(0x0009dd40)
GainGeoPrim* GainGeoPrim::Construct() {
    method = NewChildRenderMethod(GainParentMethod);
    for (int i = 0; i < 4; i++)
        params[i].Set(0, NULL);
    bool found;
    void *matrix = DynamicLoader::GetRegisteredVar("EAGL::ViewPort::gpModelViewProjectionMatrix", &found);
    for (int i = 5; i < 10; i++)
        params[i].Set(0, NULL);
    params[4].Set(1, matrix);
    return this;
}

// FUNC_AT(0x0009dfe0)
RGain* RGain::Construct() {
    vtable = reinterpret_cast<const void *>(kRGainVtable);
    state.Construct();
    model.Construct();
    geoPrim.Construct();
    for (int i = 0; i < 4; i++)
        gain[i] = 1.0f;
    for (int i = 0; i < 4; i++)
        offset[i] = 0.0f;
    for (int i = 0; i < 4; i++)
        vertexCount[i] = 0;
    state.SetPrimitiveType(kPrimitiveTriangleStrip);
    state.SetTransparencyMethod(1);
    state.SetAlphaBlendMode(1);
    state.SetDepthTestMethod(kDepthAlways);
    state.SetAlphaTestEnable(false);
    state.SetCullEnable(false);
    state.SetTextureEnable(true);
    geoPrim.params[kParamVertexCount].SetData(vertexCount);
    geoPrim.params[kParamGain].SetData(gain);
    geoPrim.params[kParamOffset].SetData(offset);
    geoPrim.params[kParamState].SetData(&state);
    geoPrim.params[kParamState].count = 1;
    geoPrim.params[kParamVertexCount].count = 1;
    geoPrim.params[kParamGain].count = 1;
    geoPrim.params[kParamOffset].count = 1;
    model.AddGeoPrim(reinterpret_cast<EAGL::GeoPrim *>(&geoPrim));
    return this;
}

// FUNC_AT(0x0009de00)
void RGain::Destruct() {
    vtable = reinterpret_cast<const void *>(kRGainVtable);
    EAGL::RenderMethod *method = geoPrim.method;
    if (method != NULL) {
        method->Destruct();
        EaglFree(method, sizeof(EAGL::RenderMethod));
    }
    model.Destruct();
    state.Destruct();
    vtable = reinterpret_cast<const void *>(kUSingletonVtable);
}

// FUNC_AT(0x0009e120)
RGain* RGain::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RGain));
    return this;
}

// The instance is not cleared, as the original has it.
// FUNC_AT(0x0009de80)
void RGain::Kill() {
    RGain *instance = TheGain;
    if (instance != NULL)
        (instance->*XbeVirtual<decltype(&RGain::Delete)>(instance, 0))(1);
}

// FUNC_AT(0x0009dea0)
void RGain::Reset() {
    for (int i = 0; i < 4; i++)
        gain[i] = 1.0f;
    for (int i = 0; i < 4; i++)
        offset[i] = 0.0f;
}

// FUNC_AT(0x0009dec0)
void RGain::Draw() {
    if (gain[0] == 1.0f && gain[1] == 1.0f && gain[2] == 1.0f && gain[3] == 1.0f && offset[0] == 0.0f &&
        offset[1] == 0.0f && offset[2] == 0.0f && offset[3] == 0.0f)
        return;

    GainPositions[1].x = float(fgRenderer->screenWidth);
    GainPositions[2].y = float(fgRenderer->screenHeight);
    GainPositions[3].x = float(fgRenderer->screenWidth);
    GainPositions[3].y = float(fgRenderer->screenHeight);
    if (!(GainColoursGuard & 1)) {
        GainColoursGuard |= 1;
        for (int i = 0; i < kQuadVertices; i++)
            GainColours[i] = 0xffffffff;
    }
    geoPrim.params[kParamTexture].SetData(ThePostProcessing->GetSubsampledBackBuffer());
    geoPrim.params[kParamTexture].count = 1;
    geoPrim.params[kParamUVs].SetData(GainUVs);
    geoPrim.params[kParamUVs].count = kQuadVertices;
    geoPrim.params[kParamPositions].SetData(GainPositions);
    geoPrim.params[kParamPositions].count = kQuadVertices;
    geoPrim.params[kParamColours].SetData(GainColours);
    geoPrim.params[kParamColours].count = kQuadVertices;
    vertexCount[0] = kQuadVertices;
    model.Draw();
}
