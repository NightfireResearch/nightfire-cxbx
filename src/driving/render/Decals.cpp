#pragma fp_contract(off)

#include "Decals.h"
#include "../Vtables.h"   // our copies of the game's vtables

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GeoPrimState.h"
#include "../eagl/Loader.h"               // DynamicLoader::GetRegisteredVar
#include "../eagl/Model.h"                // DynamicModel
#include "../eagl/RenderContext.h"
#include "../engine/InputConfig.h"        // BuildFileName
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/RealMemory.h"       // MEM_free_copy
#include "../platform/RealPrint.h"        // MEM_copy, MEM_fill
#include "Draw.h"
#include "Renderer.h"
#include "TextureContext.h"
#include "../logging.h"

// XBE_GLOBAL(0x001fe820, 0x4)
RDecalManager *TheDecalManager;

// ---------------------------------------------------------------------------------------------------------------
// RDecalManager and its neighbours (0x0009ac40-0x0009b680), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals
// The render methods' compiled data (their .rm): the shaders, the packets the interpreter runs, the parameters'
// names
// XBE_GLOBAL(0x001d3620, 0x88)
const std::array<uint32_t, 34> TexturedParentVSFunction = [] {
    using namespace Vsh;
    return Function<34>({
        Dp4(oPos.x(), v(0), c(-96)),
        Dp4(oPos.y(), v(0), c(-95)),
        Dp4(oPos.z(), v(0), c(-94)),
        Dp4(oPos.w(), v(0), c(-93)),
        Mov(oT3, v(2)),
        Mov(oD0, v(1)),
        Mul(oPos.xyz(), r(12), c(-38)) + Rcc(r1.x(), r(12).w()),
        Mad(oPos.xyz(), r(12), r(1).x(), c(-37)),
    });
}();

// XBE_GLOBAL(0x001d36a8, 0xf0)
const D3DPixelShaderDef TexturedParentPSDefinition = {
    .alphaInputs = { PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                       PS_REGISTER_T3 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                       PS_REGISTER_ZERO | PS_CHANNEL_ALPHA, PS_REGISTER_ZERO | PS_CHANNEL_ALPHA) },
    .finalCombinerInputsABCD = PS_FINALCOMBINERINPUTS_ABCD(PS_REGISTER_ZERO, PS_REGISTER_ZERO,
                                                           PS_REGISTER_R0 | PS_CHANNEL_RGB, PS_REGISTER_ZERO),
    .finalCombinerInputsEFG = PS_FINALCOMBINERINPUTS_EFG(PS_REGISTER_ZERO, PS_REGISTER_ZERO,
                                                         PS_REGISTER_R0 | PS_CHANNEL_ALPHA,
                                                         PS_FINALCOMBINERSETTING_CLAMP_SUM),
    .alphaOutputs = { PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                         PS_COMBINEROUTPUT_IDENTITY) },
    .rgbInputs = { PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_RGB | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                     PS_REGISTER_T3 | PS_CHANNEL_RGB | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                     PS_REGISTER_ZERO, PS_REGISTER_ZERO) },
    .rgbOutputs = { PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                       PS_COMBINEROUTPUT_IDENTITY) },
    .combinerCount = PS_COMBINERCOUNT(1, PS_COMBINERCOUNT_MUX_MSB | PS_COMBINERCOUNT_UNIQUE_C0 |
                                             PS_COMBINERCOUNT_UNIQUE_C1),
    .textureModes = PS_TEXTUREMODES(PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE,
                                    PS_TEXTUREMODES_PROJECT2D),
    .c0Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .c1Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .finalCombinerConstants = PS_FINALCOMBINERCONSTANTS(15, 15, PS_GLOBALFLAGS_TEXMODE_ADJUST),
};

// XBE_GLOBAL(0x001d3798, 4)
const void *TexturedParentVSFunctions[1] = { TexturedParentVSFunction.data() };
// XBE_GLOBAL(0x001d379c, 4)
const uint8_t *TexturedParentPSDefinitions[1] = {
    reinterpret_cast<const uint8_t *>(&TexturedParentPSDefinition),
};
// XBE_GLOBAL(0x001d37a0, 0x94)
uint32_t TexturedParentPackets[37] = {
    EAGL::PacketHeader(EAGL::kOpDefaultShaders, 2), 0,
    EAGL::PacketHeader(EAGL::kOpGeoPrimState, 2), 0,   // State
    EAGL::PacketHeader(EAGL::kOpTAR, 3), 0, 3,   // Texture
    EAGL::PacketHeader(EAGL::kOpStream, 2), 0,   // Parameters
    EAGL::PacketHeader(EAGL::kOpVSMatrix20, 3), 0, uint32_t(-96),   // ModelViewProjectionMatrix
    // Coordinates
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 0, 0x10, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // Colours
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 1, 4, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // STQ
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 2, 0x10, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    EAGL::PacketHeader(EAGL::kOpDrawVertices, 3), 0, 3,
    0,
};
// XBE_GLOBAL(0x001d3834, 0x1c)
const uint32_t TexturedParentVSDeclaration[7] = {
    D3DVSD_STREAM(0),
    D3DVSD_REG(0, D3DVSDT_FLOAT4),
    D3DVSD_STREAM(1),
    D3DVSD_REG(1, D3DVSDT_D3DCOLOR),
    D3DVSD_STREAM(2),
    D3DVSD_REG(2, D3DVSDT_FLOAT4),
    D3DVSD_END(),
};
// XBE_GLOBAL(0x001d38a0, 0x20)
const char *TexturedParentParamNames[8] = {
    "State", "Texture", "Parameters", "ModelViewProjectionMatrix", "Coordinates", "Colours", "STQ", NULL,
};

// XBE_GLOBAL(0x001d38d8, 0x328)
const std::array<uint32_t, 202> EmpParentVSFunction = [] {
    using namespace Vsh;
    return Function<202>({
        Mov(r2.y(), c(-75)),
        Mov(r3, v(0)),
        Dp4(r4.x(), r(3), c(-96)),
        Dp4(r5.y(), r(3), c(-95)),
        Dp4(r6.z(), r(3), c(-94)),
        Dp4(r7.x(), r(3), c(-92)),
        Dp4(r7.y(), r(3), c(-91)),
        Dp4(r8.z(), r(3), c(-90)),
        Dp4(r8.w(), r(3), c(-89)),
        Mul(r9.xy(), r(7).xy(), c(-84).x()),
        Mov(r9.z(), r(8)),
        Dp3(r10.x(), r(9), r(9)),
        Add(r0.xy(), r(2).y(), -c(-84).y()),
        Rsq(r1.y(), r(10).x()),
        Mul(r11.xyz(), r(9), r(1).y()),
        Add(r9, c(-82), -r(5).y()),
        Add(r2.z(), r(2).y(), -r(11).z()),
        Mul(r3.xy(), r(7), r(0)) + Rcp(r1.z(), r(2).z()),
        Mov(r7, c(-75).x()),
        Mov(r2.zw(), c(-75).y()),
        Mul(r6.xy(), r(11).xy(), r(1).z()),
        Mul(r10.xy(), r(6).xy(), c(-84).y()),
        Add(r8.xy(), r(3).xy(), r(10).xy()),
        Add(r10, c(-81), -r(6).z()),
        Dp4(oPos.x(), r(8), c(-88)),
        Dp4(oPos.y(), r(8), c(-87)),
        Dp4(oPos.z(), r(8), c(-86)),
        Dp4(oPos.w(), r(8), c(-85)),
        Add(r11, c(-83), -r(4).x()),
        Mul(oPos.xyz(), r(12), c(-38)) + Rcc(r1.x(), r(12).w()),
        Mul(r0, r(11), r(11)) + Mov(oFog.x(), r(12).w()),
        Mad(r11, r(9), r(9), r(0)),
        Mad(r0, r(10), r(10), r(11)),
        Mad(oPos.xyz(), r(12), r(1).x(), c(-37)),
        Mul(r3, r(0), -c(-80)),
        Mul(r4, r(3), c(-75).z()),
        Mad(r5, r(4), c(-75).w(), c(-75).y()),
        Max(r6, r(5), c(-75).x()),
        Mad(r8.xyz(), c(-79), r(6).x(), r(7)),
        Mad(r9.xyz(), c(-78), r(6).y(), r(8)),
        Mad(r10.xyz(), c(-77), r(6).z(), r(9)),
        Mad(r7.xyz(), c(-76), r(6).w(), r(10)),
        Mov(r10.xy(), c(-73).x()),
        Add(oD0, r(7), v(2)),
        Mul(r11.xy(), r(10).xy(), c(-74)),
        Exp(r1.y(), r(11).x()),
        Exp(r0.y(), r(11).y()),
        Mov(r0.x(), r(1).y()),
        Add(r2.xy(), r(0).xy(), v(1).xy()),
        Mov(oT0, r(2)),
    });
}();

// XBE_GLOBAL(0x001d3c00, 0xf0)
const D3DPixelShaderDef EmpParentPSDefinition = {
    .alphaInputs = { 0,
                     PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                       PS_REGISTER_T0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                       PS_REGISTER_ZERO | PS_CHANNEL_ALPHA, PS_REGISTER_ZERO | PS_CHANNEL_ALPHA) },
    .finalCombinerInputsABCD = PS_FINALCOMBINERINPUTS_ABCD(PS_REGISTER_FOG | PS_CHANNEL_ALPHA,
                                                           PS_REGISTER_R0 | PS_CHANNEL_RGB,
                                                           PS_REGISTER_FOG | PS_CHANNEL_RGB, PS_REGISTER_ZERO),
    .finalCombinerInputsEFG = PS_FINALCOMBINERINPUTS_EFG(PS_REGISTER_ZERO, PS_REGISTER_ZERO,
                                                         PS_REGISTER_R0 | PS_CHANNEL_ALPHA,
                                                         PS_FINALCOMBINERSETTING_CLAMP_SUM),
    .alphaOutputs = { 0,
                      PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                         PS_COMBINEROUTPUT_IDENTITY) },
    .rgbInputs = { PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_RGB | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                     PS_REGISTER_T0 | PS_CHANNEL_RGB | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                     PS_REGISTER_ZERO, PS_REGISTER_ZERO) },
    .rgbOutputs = { PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                       PS_COMBINEROUTPUT_SHIFTLEFT_1) },
    .combinerCount = PS_COMBINERCOUNT(2, PS_COMBINERCOUNT_MUX_MSB | PS_COMBINERCOUNT_UNIQUE_C0 |
                                             PS_COMBINERCOUNT_UNIQUE_C1),
    .textureModes = PS_TEXTUREMODES(PS_TEXTUREMODES_PROJECT2D, PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE,
                                    PS_TEXTUREMODES_NONE),
    .c0Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .c1Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .finalCombinerConstants = PS_FINALCOMBINERCONSTANTS(15, 15, PS_GLOBALFLAGS_TEXMODE_ADJUST),
};

// XBE_GLOBAL(0x001d3cf0, 4)
const void *EmpParentVSFunctions[1] = { EmpParentVSFunction.data() };
// XBE_GLOBAL(0x001d3cf4, 4)
const uint8_t *EmpParentPSDefinitions[1] = {
    reinterpret_cast<const uint8_t *>(&EmpParentPSDefinition),
};
// XBE_GLOBAL(0x001d3cf8, 0x104)
uint32_t EmpParentPackets[65] = {
    EAGL::PacketHeader(EAGL::kOpDefaultShaders, 2), 0,
    EAGL::PacketHeader(EAGL::kOpGeoPrimState, 2), 0,   // State
    EAGL::PacketHeader(EAGL::kOpTAR, 3), 0, 0,   // Texture
    EAGL::PacketHeader(EAGL::kOpStream, 2), 0,   // Parameters
    EAGL::PacketHeader(EAGL::kOpIndexData, 2), 0,   // Indices
    EAGL::PacketHeader(EAGL::kOpVSMatrix19, 3), 0, uint32_t(-96),   // ModelMatrix
    EAGL::PacketHeader(EAGL::kOpVSMatrix19, 3), 0, uint32_t(-92),   // MV
    EAGL::PacketHeader(EAGL::kOpVSMatrix19, 3), 0, uint32_t(-88),   // Proj
    EAGL::PacketHeader(EAGL::kOpVSConstants5, 4), 0, uint32_t(-84), 4,   // FishEyeParams
    EAGL::PacketHeader(EAGL::kOpVSConstants5, 4), 0, uint32_t(-83), 0x20,   // PositionalLights
    EAGL::PacketHeader(EAGL::kOpVSConstants5, 4), 0, uint32_t(-75), 4,   // ScalarConsts
    EAGL::PacketHeader(EAGL::kOpVSConstants5, 4), 0, uint32_t(-74), 4,   // ScrollUV
    EAGL::PacketHeader(EAGL::kOpVSConstants5, 4), 0, uint32_t(-73), 4,   // SimStep
    // Coordinates
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 0, 0xc, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // UV
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 1, 8, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // Colours
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 2, 4, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    EAGL::PacketHeader(EAGL::kOpDrawIndexedVertices, 3), 0, 3,
    0,
};
// XBE_GLOBAL(0x001d3dfc, 0x1c)
const uint32_t EmpParentVSDeclaration[7] = {
    D3DVSD_STREAM(0),
    D3DVSD_REG(0, D3DVSDT_FLOAT3),
    D3DVSD_STREAM(1),
    D3DVSD_REG(1, D3DVSDT_FLOAT2),
    D3DVSD_STREAM(2),
    D3DVSD_REG(2, D3DVSDT_D3DCOLOR),
    D3DVSD_END(),
};
// XBE_GLOBAL(0x001d3eb8, 0x40)
const char *EmpParentParamNames[16] = {
    "State", "Texture", "Parameters", "Indices", "ModelMatrix", "MV", "Proj", "FishEyeParams", "PositionalLights",
    "ScalarConsts", "ScrollUV", "SimStep", "Coordinates", "UV", "Colours", NULL,
};

// XBE_GLOBAL(0x00242de8, 0x34)
EAGL::RenderMethod TexturedParentMethod;   // the render methods the GeoPrims inherit
// XBE_GLOBAL(0x00242e1c, 0x34)
EAGL::RenderMethod EmpParentMethod;
// XBE_GLOBAL(0x001ff7f0, 0x4c)
alignas(16) EAGL::GeoPrimState DecalState;

namespace {

const uintptr_t kRDecalManagerVtable = reinterpret_cast<uintptr_t>(Vtable_RDecalManager);
const uintptr_t kUSingletonVtable = reinterpret_cast<uintptr_t>(Vtable_0018beb0);
constexpr int kGeoPrims = 8;
constexpr int kQuadsPerGeoPrim = 21;
constexpr int kVerticesPerQuad = 6;
constexpr int kQuads = kGeoPrims * kQuadsPerGeoPrim;
constexpr int kVerticesPerGeoPrim = kQuadsPerGeoPrim * kVerticesPerQuad;
constexpr int kVertices = kQuads * kVerticesPerQuad;

}  // namespace

// The quads: eight GeoPrims of 21, six vertices each (two triangles), and per quad its alpha and delay
// XBE_GLOBAL(0x001f68b0, 0x20)
TexturedGeoPrim *DecalGeoPrims[kGeoPrims];
// XBE_GLOBAL(0x001f68d0, 0xa8)
uint8_t DecalAlpha[kQuads];
// XBE_GLOBAL(0x001f6978, 0xa8)
uint8_t DecalDelay[kQuads];
// XBE_GLOBAL(0x001f6a20, 0x3f00)
alignas(16) Coord4 DecalPositions[kVertices];
// XBE_GLOBAL(0x001fa920, 0x3f00)
alignas(16) Coord4 DecalUVs[kVertices];
// XBE_GLOBAL(0x001fe830, 0xfc0)
uint32_t DecalColours[kVertices];
// XBE_GLOBAL(0x001c48a0, 0x10)
uint32_t DecalVertexCount[4] = { kVerticesPerGeoPrim, 0, 0, 0 };   // the GeoPrims' vertex count record

namespace {

constexpr uint32_t kTextureDecl = 0x6c636564;   // 'decl'
constexpr uint32_t kPrimitiveTriangles = 5;
constexpr int kCylinderSteps = 17;
constexpr float kCylinderTurnStep = 1.0f / 16.0f;

void *RegisteredVar(const char *name) {
    bool found;
    return DynamicLoader::GetRegisteredVar(name, &found);
}

void DeleteMethod(EAGL::RenderMethod *method) {
    method->Destruct();
    EaglFree(method, sizeof(EAGL::RenderMethod));
}

// What Reset and the constructor share: the quads cleared, the GeoPrims' arrays and render state
void ClearQuads() {
    memset(DecalAlpha, 0, kQuads);
    memset(DecalDelay, 0, kQuads);
    for (int i = 0; i < kVertices; i++)
        DecalColours[i] = 0xffffffff;
    const Coord4 zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < kVertices; i++) {
        DecalPositions[i] = zero;
        DecalUVs[i] = zero;
    }
}

void PointGeoPrim(TexturedGeoPrim *geoPrim, int index) {
    int first = index * kVerticesPerGeoPrim;
    geoPrim->colours.Set(kVerticesPerGeoPrim, &DecalColours[first]);
    geoPrim->positions.Set(kVerticesPerGeoPrim, &DecalPositions[first]);
    geoPrim->texCoords.Set(kVerticesPerGeoPrim, &DecalUVs[first]);
}

} // namespace

// FUNC_AT(0x0009ac40)
TexturedGeoPrim* TexturedGeoPrim::Construct() {
    method = NewChildRenderMethod(&TexturedParentMethod);
    state.Set(0, NULL);
    texture.Set(0, NULL);
    vertexCount.Set(0, NULL);
    void *matrix = RegisteredVar("EAGL::ViewPort::gpModelViewProjectionMatrix");
    positions.Set(0, NULL);
    colours.Set(0, NULL);
    texCoords.Set(0, NULL);
    modelViewProjection.Set(1, matrix);
    return this;
}

// FUNC_AT(0x0009b550)
EmpGeoPrim* EmpGeoPrim::Construct() {
    method = NewChildRenderMethod(&EmpParentMethod);
    for (int i = kEmpParamState; i <= kEmpParamIndices; i++)
        params[i].Set(0, NULL);
    params[kEmpParamModelMatrix].Set(1, RegisteredVar("EAGL::ViewPort::gpModelMatrix"));
    params[kEmpParamModelViewMatrix].Set(1, RegisteredVar("EAGL::ViewPort::gpModelViewMatrix"));
    params[kEmpParamProjectionMatrix].Set(1, RegisteredVar("EAGL::ViewPort::gpProjectionMatrix"));
    params[kEmpParamFishEye].Set(1, RegisteredVar("GAME::FishEyeParams"));
    params[kEmpParamLights].Set(8, RegisteredVar("GAME::PositionalLights"));
    params[kEmpParamConstants].Set(0, NULL);
    params[kEmpParam10].Set(0, NULL);
    void *simStep = RegisteredVar("GAME::SimStep");
    for (int i = kEmpParamPositions; i <= kEmpParamColours; i++)
        params[i].Set(0, NULL);
    params[kEmpParamSimStep].Set(1, simStep);
    return this;
}

// FUNC_AT(0x0009acf0)
RDecalManager* RDecalManager::Construct() {
    vtable = reinterpret_cast<const void *>(kRDecalManagerVtable);
    nextDecal = 0;
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = static_cast<TexturedGeoPrim *>(EaglMalloc(sizeof(TexturedGeoPrim), "EAGL::GeoPrim new"));
        DecalGeoPrims[i] = geoPrim != NULL ? geoPrim->Construct() : NULL;
    }
    EAGL::TAR *texture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kTextureDecl, 0);
    DecalState.SetPrimitiveType(kPrimitiveTriangles);
    DecalState.SetTextureEnable(true);
    DecalState.SetShading(1);
    Draw::SetNormalBlendMode(&DecalState);
    ClearQuads();
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = DecalGeoPrims[i];
        PointGeoPrim(geoPrim, i);
        geoPrim->texture.Set(1, texture);
        geoPrim->state.Set(1, &DecalState);
        geoPrim->vertexCount.Set(1, DecalVertexCount);
    }

    char path[64];
    DecalType *table = types;
    void *file = UFileLoader::FileLoadz(BuildFileName(path, 0, "data\\render\\", "decal.dat", ""), 0x100);
    if (file == NULL) {
        dbg_wprintf("Unable to load lighting file for config [Decal.dat]\n");
        MEM_fill(table, 0, sizeof(types));
    } else {
        MEM_copy(table, file, sizeof(types));
        MEM_free_copy(file);
    }
    return this;
}

// The GeoPrims are freed but the table keeps their addresses, as the original has it.
// FUNC_AT(0x0009b3c0)
void RDecalManager::Destruct() {
    vtable = reinterpret_cast<const void *>(kRDecalManagerVtable);
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = DecalGeoPrims[i];
        if (geoPrim != NULL) {
            if (geoPrim->method != NULL)
                DeleteMethod(geoPrim->method);
            EaglFree(geoPrim, sizeof(TexturedGeoPrim));
        }
    }
    vtable = reinterpret_cast<const void *>(kUSingletonVtable);
}

// FUNC_AT(0x0009b450)
RDecalManager* RDecalManager::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// The instance is not cleared, as the original has it.
// FUNC_AT(0x0009af50)
void RDecalManager::Kill() {
    RDecalManager *manager = TheDecalManager;
    if (manager != NULL)
        (manager->*XbeVirtual<decltype(&RDecalManager::Delete)>(manager, 0))(1);
}

// FUNC_AT(0x0009af70)
void RDecalManager::Reset() {
    ClearQuads();
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = DecalGeoPrims[i];
        PointGeoPrim(geoPrim, i);
        geoPrim->state.Set(1, &DecalState);
    }
}

// FUNC_AT(0x0009b070)
void RDecalManager::AddDecal(const Coord4 *centre, const Coord4 *axis, const Coord4 *normal, int type, float width,
                             float height) {
    const DecalType *decal = &types[type];
    int slot = nextDecal;
    Coord4 *position = &DecalPositions[slot * kVerticesPerQuad];
    Coord4 *uv = &DecalUVs[slot * kVerticesPerQuad];

    uint32_t delay = decal->delay;
    if (delay == 0)
        delay = 1;
    DecalDelay[slot] = uint8_t(delay);
    DecalAlpha[slot] = decal->alpha;
    DecalAlpha[slot] <<= 2;
    for (int i = 0; i < kVerticesPerQuad; i++)
        DecalColours[slot * kVerticesPerQuad + i] = 0x00ffffff;

    // The quad's sides: across = normal x axis, times width; along = (normal x axis) x axis, times height
    Coord4 along, across, corner;
    VU0_v4copy(normal, &along);
    VU0_v4crossprod1(&along, axis, &across);
    VU0_v4crossprod1(&across, axis, &along);
    VU0_v4scale4(&across, width, &across);
    VU0_v4scale4(&along, height, &along);
    along.w = 0.0f;
    across.w = 0.0f;
    VU0_v4scaleadd4(&across, -0.5f, centre, &corner);
    VU0_v4scaleadd4(&along, -0.5f, &corner, &corner);
    corner.w = 1.0f;

    // Two triangles: 0 1 2 and 1 2 3 (as 3 4 5)
    VU0_v4copy(&corner, &position[0]);
    VU0_v4add4(&corner, &across, &position[1]);
    VU0_v4add4(&corner, &along, &position[2]);
    VU0_v4copy(&position[1], &position[3]);
    VU0_v4copy(&position[2], &position[4]);
    VU0_v4add4(&position[1], &along, &position[5]);
    VU0_v4copy(&decal->uvs[0], &uv[0]);
    VU0_v4copy(&decal->uvs[1], &uv[1]);
    VU0_v4copy(&decal->uvs[2], &uv[2]);
    VU0_v4copy(&decal->uvs[1], &uv[3]);
    VU0_v4copy(&decal->uvs[2], &uv[4]);
    VU0_v4copy(&decal->uvs[3], &uv[5]);

    if (++nextDecal >= kQuads)
        nextDecal = 0;
}

// FUNC_AT(0x0009b2d0)
void RDecalManager::DrawDecals() {
    for (int i = 0; i < kQuads; i++) {
        if (DecalDelay[i] != 0 && --DecalDelay[i] == 0) {
            uint32_t colour = DecalColours[i * kVerticesPerQuad] + (uint32_t(DecalAlpha[i]) << 24);
            for (int j = 0; j < kVerticesPerQuad; j++)
                DecalColours[i * kVerticesPerQuad + j] = colour;
        }
    }
    fgRenderer->renderContext->SetZWritesEnable(0);
    EAGL::DynamicModel model;
    model.Construct();
    for (int i = 0; i < kGeoPrims; i++)
        model.AddGeoPrim(DecalGeoPrims[i]->AsGeoPrim());
    model.Draw();
    fgRenderer->renderContext->SetZWritesEnable(1);
    model.Destruct();
}

// FUNC_AT(0x0009b470)
void CylinderSection(Coord3 *positions, CylinderUV *uvs, float radiusB, float radiusA, float zA, float zB) {
    float turns = 0.0f;
    int vertex = 0;
    for (int step = 0; step < kCylinderSteps; step++) {
        for (int side = 0; side < 2; side++) {
            float radius = side != 0 ? radiusB : radiusA;
            Coord3 *position = &positions[vertex];
            position->x = float(CosineTurns(turns) * radius);
            position->y = float(SineTurns(turns) * radius + 0.5f);
            position->z = side != 0 ? zB : zA;
            uvs[vertex][0] = float((position->x + 1.0) * 0.5f);
            uvs[vertex][1] = float((position->z + 1.0) * 0.5f);
            vertex++;
        }
        turns += kCylinderTurnStep;
    }
}
