#pragma fp_contract(off)

#include "WeaponCameras.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "MissionManager.h"     // glbMissionManager
#include "Simulation.h"
#include "VehicleParticle.h"              // the vehicle particles' system types
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../camera/PlayerCamera.h"       // RPlayerCamera
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GeoPrimState.h"
#include "../eagl/Model.h"                // DynamicModel
#include "../eagl/RenderContext.h"
#include "../eagl/RenderMethod.h"
#include "../eagl/Tar.h"
#include "../engine/StaticInit.h"         // DestroyStatic_00200f60
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"         // sin_fractionalangle, cos_fractionalangle
#include "../render/Draw.h"               // SimpleTexturedMaterial
#include "../render/Particles.h"          // RParticleSystemData
#include "../render/PostProcessing.h"     // ThePostProcessing
#include "../render/RenderHigh.h"         // fgRenderHigh
#include "../render/Renderer.h"           // fgRenderer
#include "../render/TextureContext.h"

// ---------------------------------------------------------------------------------------------------------------
// RMissileCam (0x000a0a50-0x000a0e90), RSniperZoom with its ring helper (0x000a6810-0x000a6cd0) and the particle
// texture helper after it (0x000a6cd0-0x000a6db0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)
// The offscreen buffer's bracket round the copy and the draws: one empty function (Ghidra: dummyNullFunction)
#define DummyNullFunction ((void (__fastcall *)(ROffscreenBuffer *, int))0x000d3580)

// ---- globals

// XBE_GLOBAL(0x00200f60, 0x4c)
EAGL::GeoPrimState MissileCamState;
// XBE_GLOBAL(0x00200fac, 0x4)
uint32_t MissileCamStateMade;
// XBE_GLOBAL(0x001c4ab0, 0x10)
uint32_t MissileCamVertexCount[4] = { 4, 0, 0, 0 };
// XBE_GLOBAL(0x001c4ac0, 0x40)
alignas(16) Coord4 MissileCamPositions[4] = {
    { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f },
};

// XBE_GLOBAL(0x001c50a8, 0x4)
float SniperCopySpread = 0.5f;
// XBE_GLOBAL(0x001c50b0, 0x4)
int32_t SniperCopies = 4;
// XBE_GLOBAL(0x001c50b4, 0x4)
float SniperOuterRadius = 530.0f;
// XBE_GLOBAL(0x001c50b8, 0x4)
int32_t SniperRingShade = 185;
// XBE_GLOBAL(0x001c50bc, 0x4)
float SniperMiddleRadius = 150.0f;
// XBE_GLOBAL(0x001c50c0, 0x4)
float SniperInnerRadius = 65.0f;
// XBE_GLOBAL(0x00201830, 0x4)
int32_t SniperOuterShade;

namespace {

const void *const kMissileCamVtable = reinterpret_cast<const void *>(0x00192d38);
const void *const kSniperZoomVtable = reinterpret_cast<const void *>(0x00193008);
const void *const kUSingletonVtable = reinterpret_cast<const void *>(0x0018beb0);

// A texture's name is the word its four characters spell in memory; the original keeps these byte-reversed and
// swaps them as it reads them
const uint32_t kMissileCamTextureNames[3] = {
    0x317a7566,     // 'fuz1'
    0x327a7566,     // 'fuz2'
    0x337a7566,     // 'fuz3'
};

// TAR addressing and filtering, GeoPrimState's depth and alpha test methods (D3D's and OpenGL's numbering)
constexpr uint32_t kTextureWrap = 1;
constexpr uint32_t kFilterPoint = 1;
constexpr uint32_t kTestAlways = 0x207;
constexpr uint32_t kTransparencyOn = 1;

constexpr int kMissileCamVertices = 4;
constexpr uint32_t kMissileCamGrey = 0x808080;
constexpr double kNoiseRepeatsPerPixel = 1.0 / 128.0;   // the noise's v across the screen's height
constexpr float kNoiseRepeatsAcross = 4.0f;

constexpr int kBackBufferSize = 64;
constexpr int kBackBufferDepth = 32;
constexpr float kRingTurnStep = 0.05f;              // a pair of ring vertices a twentieth of a turn on
constexpr uint32_t kRingInnerEdge = 0xffffff;       // the inner ring's inner edge: white, alpha 0

constexpr float kHalf = 0.5f;
constexpr float kOneIn256 = 1.0f / 256.0f;

TexturedGeoPrim *NewTexturedGeoPrim() {
    void *memory = EaglMalloc(sizeof(TexturedGeoPrim), "EAGL::GeoPrim new");
    return memory != NULL ? static_cast<TexturedGeoPrim *>(memory)->Construct() : NULL;
}

// The alpha and a grey in each colour byte, shifted in as the original does
uint32_t ShadeColour(int32_t alpha, int32_t grey) {
    uint32_t colour = uint32_t(alpha);
    colour = colour << 8 | uint32_t(grey);
    colour = colour << 8 | uint32_t(grey);
    return colour << 8 | uint32_t(grey);
}

// sin or cos of `turns` times `scale` on the x87, as the original multiplies FSIN's and FCOS's unrounded results
__declspec(naked) double ScaledSine(float turns, float scale) {
    __asm {
        push dword ptr [esp + 4]
        call sin_fractionalangle
        add esp, 4
        fmul dword ptr [esp + 8]
        ret
    }
}

__declspec(naked) double ScaledCosine(float turns, float scale) {
    __asm {
        push dword ptr [esp + 4]
        call cos_fractionalangle
        add esp, 4
        fmul dword ptr [esp + 8]
        ret
    }
}

}  // namespace

// ---- RMissileCam

// FUNC_AT(0x000a0a50)
RMissileCam* RMissileCam::Construct() {
    vtable = kMissileCamVtable;
    enabled = 0;
    for (int i = 0; i < 3; i++) {
        EAGL::TAR *texture =
            RTextureContextManager::GetContext(0)->FindOrCreateTexture(kMissileCamTextureNames[i], 0);
        textures[i] = texture;
        texture->address0 = kTextureWrap;
        texture->addressU = kTextureWrap;
        texture->addressV = kTextureWrap;
        texture->addressW = kTextureWrap;
        texture->filter = kFilterPoint;
    }
    geoPrim = NewTexturedGeoPrim();
    return this;
}

// FUNC_AT(0x000a0b30)
void RMissileCam::Kill() {
    RMissileCam *camera = MissileCam;
    if (camera != NULL)
        (camera->*XbeVirtual<decltype(&RMissileCam::Delete)>(camera, 0))(1);
}

// FUNC_AT(0x000a0b50)
void RMissileCam::Draw() {
    if (glbMissionManager->cinematicMode != 0 || !enabled)
        return;
    EAGL::TAR *texture = textures[fgSimulation.stepCount % 3];
    uint32_t colour = uint32_t(alpha) << 24 | kMissileCamGrey;
    uint32_t colours[kMissileCamVertices] = { colour, colour, colour, colour };
    float bottom = float(fgRenderer->screenHeight * kNoiseRepeatsPerPixel);
    Coord4 uvs[kMissileCamVertices] = {
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { kNoiseRepeatsAcross, 0.0f, 1.0f, 1.0f },
        { 0.0f, bottom, 1.0f, 1.0f },
        { kNoiseRepeatsAcross, bottom, 1.0f, 1.0f },
    };

    // A function-local static in the original
    if ((MissileCamStateMade & 1) == 0) {
        MissileCamStateMade |= 1;
        MissileCamState.Construct();
        CRT_atexit(DestroyStatic_00200f60);
    }
    MissileCamPositions[1].x = float(fgRenderer->screenWidth);
    MissileCamPositions[2].y = float(fgRenderer->screenHeight);
    MissileCamPositions[3].x = float(fgRenderer->screenWidth);
    MissileCamPositions[3].y = float(fgRenderer->screenHeight);
    MissileCamState.SetPrimitiveType(kTriangleStrip);
    MissileCamState.SetTextureEnable(true);
    MissileCamState.SetTransparencyMethod(kTransparencyOn);
    MissileCamState.SetAlphaBlendMode(kBlendNormal);
    MissileCamState.SetAlphaTestEnable(false);
    MissileCamState.SetDepthTestMethod(kTestAlways);
    MissileCamState.SetAlphaTestMethod(kTestAlways);

    geoPrim->vertexCount.Set(1, MissileCamVertexCount);
    geoPrim->positions.Set(kMissileCamVertices, MissileCamPositions);
    geoPrim->texture.Set(1, texture);
    geoPrim->texCoords.Set(kMissileCamVertices, uvs);
    geoPrim->colours.Set(kMissileCamVertices, colours);
    geoPrim->state.Set(1, &MissileCamState);
    EAGL::DynamicModel model;
    model.Construct();
    model.AddGeoPrim(geoPrim->AsGeoPrim());
    model.Draw();
    model.Destruct();
}

// FUNC_AT(0x000a0df0)
void RMissileCam::Destruct() {
    vtable = kMissileCamVtable;
    textures[0] = NULL;
    textures[1] = NULL;
    textures[2] = NULL;
    if (geoPrim != NULL) {
        EAGL::RenderMethod *method = geoPrim->method;
        if (method != NULL) {
            method->Destruct();
            EaglFree(method, sizeof(EAGL::RenderMethod));
        }
        EaglFree(geoPrim, sizeof(TexturedGeoPrim));
    }
    vtable = kUSingletonVtable;
}

// FUNC_AT(0x000a0e70)
RMissileCam* RMissileCam::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RMissileCam));
    return this;
}

// ---- RSniperZoom

// FUNC_AT(0x000a6810)
void RSniperZoom::Destruct() {
    vtable = kSniperZoomVtable;
    material.Destruct();
    backBuffer.Destruct();
    vtable = kUSingletonVtable;
}

// FUNC_AT(0x000a6870)
void RSniperZoom::Kill() {
    RSniperZoom *zoom = SniperZoom;
    if (zoom != NULL)
        (zoom->*XbeVirtual<decltype(&RSniperZoom::Delete)>(zoom, 0))(1);
}

// FUNC_AT(0x000a6890)
void RSniperZoom::Reset() {
    alpha = 0;
    active = 0;
}

// FUNC_AT(0x000a68a0)
void RSniperZoom::GrabBackBuffer() {
    if (glbMissionManager->cinematicMode != 0)
        active = 0;
    if (!active)
        return;
    DummyNullFunction(&backBuffer, 0);
    backBuffer.MakeTexCopy(ThePostProcessing->GetSubsampledBackBuffer(), SniperCopies, SniperCopySpread);
}

void SniperRingPoint(Coord4 *position, Coord4 *uv, float radius, float turns) {
    const RPlayerCamera *camera = fgRenderHigh->views[0].camera;
    float aimX = camera->unknown1D0;
    float aimY = camera->unknown1D4;
    int32_t width = fgRenderer->screenWidth;
    position->x = float(ScaledSine(turns, radius) + width * 0.5 + aimX);
    int32_t height = fgRenderer->screenHeight;
    double cosine = ScaledCosine(turns, radius);
    position->z = 1.0f;
    position->w = 1.0f;
    position->y = float(cosine + height * 0.5 + aimY);
    uv->x = float(position->x / double(fgRenderer->screenWidth));
    uv->z = 1.0f;
    uv->w = 1.0f;
    uv->y = float(position->y / double(fgRenderer->screenHeight));
}

// AUTOLTCG
__declspec(naked) void FUN_000a68f0() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edi
        push esi
        call SniperRingPoint
        add esp, 0x10
        ret
    }
}

// FUNC_AT(0x000a69a0)
RSniperZoom* RSniperZoom::Construct() {
    vtable = kSniperZoomVtable;
    backBuffer.Construct(kBackBufferSize, kBackBufferSize, kBackBufferDepth, false, false);
    memset(innerColours, 0, sizeof(innerColours));
    memset(outerColours, 0, sizeof(outerColours));
    material.Construct();
    alpha = 0xff;
    active = 0;
    material.SetAlphaBlendMode(kBlendNormal);
    material.SetAlphaTestEnable(false);
    material.SetDepthTestMethod(kTestAlways);
    return this;
}

// FUNC_AT(0x000a6a50)
RSniperZoom* RSniperZoom::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RSniperZoom));
    return this;
}

// Each ring a strip: even vertices on its inner circle, odd on its outer, a pair every twentieth of a turn
// FUNC_AT(0x000a6a80)
void RSniperZoom::SetupVerts() {
    for (int i = 0; i < kSniperRingVertices; i++) {
        bool outerEdge = (i & 1) != 0;
        float turns = float(double(i / 2) * kRingTurnStep);
        SniperRingPoint(&innerPositions[i], &innerUVs[i], outerEdge ? SniperMiddleRadius : SniperInnerRadius, turns);
        uint32_t shade = ShadeColour(alpha, SniperRingShade);
        innerColours[i] = outerEdge ? shade : kRingInnerEdge;
        SniperRingPoint(&outerPositions[i], &outerUVs[i], outerEdge ? SniperOuterRadius : SniperMiddleRadius, turns);
        if (outerEdge)
            shade = ShadeColour(alpha, SniperOuterShade);
        outerColours[i] = shade;
    }
}

// FUNC_AT(0x000a6bc0)
void RSniperZoom::Draw() {
    if (!active)
        return;
    SimpleTexturedMaterial->SetAlphaBlendMode(kBlendNormal);
    SimpleTexturedMaterial->SetAlphaTestEnable(false);
    SimpleTexturedMaterial->SetDepthTestMethod(kTestAlways);
    SetupVerts();
    EAGL::TAR *texture = backBuffer.colourTarget;
    TexturedGeoPrim *request = TexturedRequests[TexturedRequestIndex];
    request->texture.SetData(texture);
    request->texCoords.SetData(outerUVs);
    request->colours.SetData(outerColours);
    request->positions.SetData(outerPositions);
    material.Draw(kTriangleStrip, kSniperRingVertices, NULL);
    request = TexturedRequests[TexturedRequestIndex];
    request->texture.SetData(texture);
    request->texCoords.SetData(innerUVs);
    request->colours.SetData(innerColours);
    request->positions.SetData(innerPositions);
    material.Draw(kTriangleStrip, kSniperRingVertices, NULL);
    DummyNullFunction(&backBuffer, 0);
}

// ---- the vehicle particles' sprite rectangles

// A particle type's sprite in its texture, in 256ths: the corner (u, v) and the size, by the texture's name
void ParticleTextureRect(uint32_t texture, float *rect) {
    int32_t u = 0, v = 0, size = 0x100;
    if (texture == ParticleTypeSMRT.texture) {
        u = 0x60;
        v = 0xa0;
        size = 0x20;
    } else if (texture == ParticleTypeRDDF.texture) {
        u = 0x80;
        v = 0x40;
        size = 0x40;
    } else if (texture == ParticleTypeRDGR.texture) {
        u = 0xa0;
        v = 0xe0;
        size = 0x20;
    } else if (texture == ParticleTypeSNOW.texture) {
        u = 0x80;
        v = 0x60;
        size = 0x40;
    } else if (texture == ParticleTypeWATR.texture) {
        u = 0x60;
        v = 0xa0;
        size = 0x20;
    }
    rect[3] = 0.0f;
    rect[0] = float((double(u) + kHalf) * kOneIn256);
    rect[1] = float((double(v) + kHalf) * kOneIn256);
    rect[2] = float((size - 1.0) * kOneIn256);
}

// AUTOLTCG
__declspec(naked) void FUN_000a6cd0() {
    __asm {
        push ecx
        push eax
        call ParticleTextureRect
        add esp, 8
        ret
    }
}
