#pragma fp_contract(off)

#include "DebugView.h"

#include <bit>

#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../eagl/GeoPrimState.h"
#include "../eagl/RenderContext.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"       // RigidVehicle
#include "Draw.h"
#include "Materials.h"
#include "Renderer.h"
#include "RSceneObj.hpp"           // RandomSeed, RandomMultiplier

// ---------------------------------------------------------------------------------------------------------------
// RRandom::StartUp and the debug views (0x0008b2b0..0x0008b5b0), ported from the listing. The camera assignment
// between them is RCamera::Assign, in camera/Camera.cpp.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

#define PlayerVehicles (*(RigidVehicle ***)0x00234e40)              // the player's first
#define DebugViewEnabled BOOL8_AT(0x001ec484)
#define DebugSpeedFraction FLOAT_AT(0x001c45d4)                     // written only here (the name is ours)

// ---- the game's code not ported yet

#define DAudio_Draw ((void (*)())0x00037620)

namespace {

void **const kPerspectiveVtable = reinterpret_cast<void **>(0x001919a0);
void **const kScreenSpaceVtable = reinterpret_cast<void **>(0x001919b4);

// SetRenderMask's masks, as RRenderer::EnableAlphaWrites and DisableAlphaWrites set them
constexpr uint32_t kRenderMaskWithAlpha = 0x01010101;
constexpr uint32_t kRenderMaskNoAlpha = 0x00010101;

// SetAlphaBlend's factors and operation, in OpenGL's numbering
constexpr uint32_t kBlendSrcAlpha = 0x302;
constexpr uint32_t kBlendOneMinusSrcAlpha = 0x303;
constexpr uint32_t kBlendDstAlpha = 0x304;
constexpr uint32_t kBlendOneMinusDstAlpha = 0x305;
constexpr uint32_t kBlendAdd = 0x8006;

constexpr uint32_t kDebugGrey = 0x70808080;

// The player car's speed above 20, over 75
constexpr float kSpeedFloor = 20.0f;
constexpr float kSpeedScale = 1.0f / 75.0f;
static_assert(std::bit_cast<uint32_t>(kSpeedScale) == 0x3c5a740e, "1/75");

constexpr uint32_t kRandomStart = 0x3ade68b1;
constexpr uint32_t kRandomStep = 0xffffcd15;
constexpr uint32_t kRandomMultiplier = 123456789;

// PBondCar::GetCarSpeed (vtable slot 20), its result unrounded
typedef double (RigidVehicle::*GetCarSpeedMethod)();

}  // namespace

// FUNC_AT(0x0008b2b0)
void RRandom::StartUp(unsigned seed) {
    RandomMultiplier = kRandomMultiplier;
    uint32_t state = kRandomStart;
    for (unsigned steps = seed % 500; steps > 0; steps--)
        state = state * kRandomStep & 0xffff;
    RandomSeed = state;
}

// ---------------------------------------------------------------------------------------------------------------
// RRenderDebugViewPerspective

// FUNC_AT(0x0008b300)
RRenderDebugViewPerspective* RRenderDebugViewPerspective::Construct(RViewCamera *source) {
    RViewCamera::Construct(NULL);
    this->source = source;
    vtable = kPerspectiveVtable;
    transformMode = kWorldTransform;
    return this;
}

// FUNC_AT(0x0008b4d0)
RRenderDebugViewPerspective* RRenderDebugViewPerspective::Delete(unsigned flags) {
    DestructThunk();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RRenderDebugViewPerspective));
    return this;
}

// FUNC_AT(0x0008b4f0)
void RRenderDebugViewPerspective::PreRender() {
    camera->Assign(source->camera);
    SetExtents(source);
}

// ---------------------------------------------------------------------------------------------------------------
// RRenderDebugViewScreenSpace

// FUNC_AT(0x0008b3a0)
RRenderDebugViewScreenSpace* RRenderDebugViewScreenSpace::Construct() {
    RViewCamera::Construct(NULL);
    vtable = kScreenSpaceVtable;
    transformMode = kDeviceTransform;
    return this;
}

// FUNC_AT(0x0008b3c0)
void RRenderDebugViewScreenSpace::Debug() {
    if (!DebugViewEnabled)
        return;
    const Coord4 corners[4] = {
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 512.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 450.0f, 1.0f, 1.0f },
        { 512.0f, 450.0f, 1.0f, 1.0f },
    };
    EAGL::GeoPrimStateExtension *state = SimpleMaterial->Extension();
    fgRenderer->renderContext->Extension()->SetRenderMask(kRenderMaskNoAlpha);
    state->SetAlphaBlend(kBlendDstAlpha, kBlendOneMinusDstAlpha, kBlendAdd);
    Draw::DrawQuad(corners, kDebugGrey);
    fgRenderer->renderContext->Extension()->SetRenderMask(kRenderMaskWithAlpha);
    state->SetAlphaBlend(kBlendSrcAlpha, kBlendOneMinusSrcAlpha, kBlendAdd);
}

// FUNC_AT(0x0008b510)
RRenderDebugViewScreenSpace* RRenderDebugViewScreenSpace::Delete(unsigned flags) {
    DestructThunk();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RRenderDebugViewScreenSpace));
    return this;
}

// FUNC_AT(0x0008b530)
void RRenderDebugViewScreenSpace::DoRender() {
    RigidVehicle *car = PlayerVehicles[0];
    double speed = (car->*XbeVirtual<GetCarSpeedMethod>(car, 20))();
    float fraction = float((speed - kSpeedFloor) * kSpeedScale);
    if (1.0f < fraction)
        fraction = 1.0f;
    if (fraction < 0.0f)
        fraction = 0.0f;
    DebugSpeedFraction = fraction;
    DAudio_Draw();
}
