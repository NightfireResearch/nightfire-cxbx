#include "Gain.h"
#include "../Vtables.h"   // our copies of the game's vtables

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
// The render method's compiled data (its .rm): the shaders, the packets the interpreter runs, the parameters' names
// XBE_GLOBAL(0x001d3f10, 0x88)
const std::array<uint32_t, 34> GainParentVSFunction = [] {
    using namespace Vsh;
    return Function<34>({
        Dp4(oPos.x(), v(0), c(-96)),
        Dp4(oPos.y(), v(0), c(-95)),
        Dp4(oPos.z(), v(0), c(-94)),
        Dp4(oPos.w(), v(0), c(-93)),
        Mov(oT0, v(1)),
        Mov(oD0, v(2)),
        Mul(oPos.xyz(), r(12), c(-38)) + Rcc(r1.x(), r(12).w()),
        Mad(oPos.xyz(), r(12), r(1).x(), c(-37)),
    });
}();

// XBE_GLOBAL(0x001d3f98, 0xf0)
const D3DPixelShaderDef GainParentPSDefinition = {
    .alphaInputs = { PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                       PS_REGISTER_ONE | PS_CHANNEL_ALPHA, PS_REGISTER_ZERO | PS_CHANNEL_ALPHA,
                                       PS_REGISTER_ZERO | PS_CHANNEL_ALPHA) },
    .alphaOutputs = { PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                         PS_COMBINEROUTPUT_IDENTITY) },
    .rgbInputs = { PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_RGB | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                     PS_REGISTER_ONE | PS_CHANNEL_RGB, PS_REGISTER_ZERO, PS_REGISTER_ZERO) },
    .rgbOutputs = { PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                       PS_COMBINEROUTPUT_IDENTITY) },
    .combinerCount = PS_COMBINERCOUNT(1, PS_COMBINERCOUNT_MUX_MSB | PS_COMBINERCOUNT_UNIQUE_C0 |
                                             PS_COMBINERCOUNT_UNIQUE_C1),
    .textureModes = PS_TEXTUREMODES(PS_TEXTUREMODES_PROJECT2D, PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE,
                                    PS_TEXTUREMODES_NONE),
    .c0Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .c1Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .finalCombinerConstants = PS_FINALCOMBINERCONSTANTS(15, 15, PS_GLOBALFLAGS_TEXMODE_ADJUST),
};

// XBE_GLOBAL(0x001d4088, 4)
const void *GainParentVSFunctions[1] = { GainParentVSFunction.data() };
// XBE_GLOBAL(0x001d408c, 4)
const uint8_t *GainParentPSDefinitions[1] = {
    reinterpret_cast<const uint8_t *>(&GainParentPSDefinition),
};
// XBE_GLOBAL(0x001d4090, 0xbc)
uint32_t GainParentPackets[47] = {
    EAGL::PacketHeader(EAGL::kOpDefaultShaders, 2), 0,
    EAGL::PacketHeader(EAGL::kOpGeoPrimState, 2), 0,   // State
    EAGL::PacketHeader(EAGL::kOpTAR, 3), 0, 0,   // Texture
    EAGL::PacketHeader(EAGL::kOpStream, 2), 0,   // Parameters
    EAGL::PacketHeader(EAGL::kOpIndexData, 2), 0,   // Indices
    EAGL::PacketHeader(EAGL::kOpVSMatrix20, 3), 0, uint32_t(-96),   // ModelViewMatrix
    EAGL::PacketHeader(EAGL::kOpPSConstants34, 4), 0, 0, 4,   // Gain
    EAGL::PacketHeader(EAGL::kOpPSConstants34, 4), 0, 1, 4,   // Offset
    // Coordinates
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 0, 0x10, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // UV
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 1, 8, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // Colours
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 2, 4, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    EAGL::PacketHeader(EAGL::kOpDrawIndexedVertices, 3), 0, 3,
    0,
};
// XBE_GLOBAL(0x001d414c, 0x1c)
const uint32_t GainParentVSDeclaration[7] = {
    D3DVSD_STREAM(0),
    D3DVSD_REG(0, D3DVSDT_FLOAT4),
    D3DVSD_STREAM(1),
    D3DVSD_REG(1, D3DVSDT_FLOAT2),
    D3DVSD_STREAM(2),
    D3DVSD_REG(2, D3DVSDT_D3DCOLOR),
    D3DVSD_END(),
};
// XBE_GLOBAL(0x001d41c4, 0x2c)
const char *GainParentParamNames[11] = {
    "State", "Texture", "Parameters", "Indices", "ModelViewMatrix", "Gain", "Offset", "Coordinates", "UV", "Colours",
    NULL,
};

// XBE_GLOBAL(0x00242e50, 0x34)
alignas(16) EAGL::RenderMethod GainParentMethod;   // the render method the GeoPrim inherits
// The screen quad: its four corners (0, 0), (w, 0), (0, h), (w, h), their texture coordinates, and their colours, a
// function-local static made white on the first draw (names ours)
// XBE_GLOBAL(0x001c4a40, 0x40)
alignas(16) Coord4 GainPositions[4] = {
    { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } };
// XBE_GLOBAL(0x001c4a20, 0x20)
static const float GainUVs[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };
// XBE_GLOBAL(0x00200f30, 0x10)
uint32_t GainColours[4];
// XBE_GLOBAL(0x00200f40, 4)
uint32_t GainColoursGuard;

// XBE_GLOBAL(0x00200f20, 4)
RGain *TheGain;

namespace {

void **const kRGainVtable = Vtable_RGain;
void **const kUSingletonVtable = Vtable_0018beb0;
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
    method = NewChildRenderMethod(&GainParentMethod);
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
    vtable = kRGainVtable;
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
    vtable = kRGainVtable;
    EAGL::RenderMethod *method = geoPrim.method;
    if (method != NULL) {
        method->Destruct();
        EaglFree(method, sizeof(EAGL::RenderMethod));
    }
    model.Destruct();
    state.Destruct();
    vtable = kUSingletonVtable;
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
