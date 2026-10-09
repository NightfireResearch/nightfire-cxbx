#pragma fp_contract(off)

#include "Fog.h"

#include <bit>

#include "Renderer.h"                     // fgRenderer
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../data/DebugVariables.h"
#include "../data/Tuning.h"
#include "../eagl/RenderContext.h"
#include "../engine/UMemory.hpp"
#include "../platform/X87.h"              // Ftol

// ---------------------------------------------------------------------------------------------------------------
// RFog (0x0007d790..0x0007dad0), ported from the listing. The x87 code keeps the original's order and roundings.
// ---------------------------------------------------------------------------------------------------------------

#define GameTick U32_AT(0x001f2a4c)
#define TicksPerSecond FLOAT_AT(0x001f2a44)             // (name ours)
#define TuningLevel ((const char *)0x00244084)          // the name of the level the tuning databases are read for
#define FogColourFlag (*(uint8_t *)0x001ec008)          // the flag byte dbattrib_argb is handed (and does not use)

namespace {

const USingletonVtable *const kFogVtable = reinterpret_cast<const USingletonVtable *>(0x00191514);

constexpr uint32_t kDefaultColour = 0x80856838;
constexpr float kDefaultStart = 1.0f;
constexpr float kDefaultEnd = 2500.0f;
constexpr float kDefaultBaseEnd = 5000.0f;
constexpr uint32_t kDefaultMode = 3;
constexpr float kDefaultDensity = 0.000475f;
static_assert(std::bit_cast<uint32_t>(kDefaultDensity) == 0x39f9096c, "0.000475");

// The tuning menu's ranges (not used on the Xbox)
constexpr float kDistanceMin = 1.0f;
constexpr float kDistanceMax = 5000.0f;
constexpr float kDensityMin = 0.0001f;
constexpr float kDensityMax = 0.01f;
static_assert(std::bit_cast<uint32_t>(kDensityMin) == 0x38d1b717 && std::bit_cast<uint32_t>(kDensityMax) == 0x3c23d70a,
              "the density's range");
constexpr unsigned kModeMax = 3;

typedef RFog *(RFog::*DeleteMethod)(unsigned flags);

}  // namespace

// FUNC_AT(0x0007d790)
uint32_t RFog::FogColour() {
    return params->colour;
}

// FUNC_AT(0x0007d7a0)
void RFog::DisableFog() {
    fgRenderer->renderContext->Extension()->SetFogEnable(0);
}

// FUNC_AT(0x0007d7b0)
void RFog::FadeScale(float scale, float seconds) {
    fadeEndTick = Ftol((double)TicksPerSecond * seconds + (double)GameTick);
    if (fadeEndTick > GameTick) {
        fading = 1;
        fadeTarget = scale;
        fadeRate = (float)(((double)scale - params->scale) / (double)(fadeEndTick - GameTick));
    }
}

// FUNC_AT(0x0007d820)
void RFog::EnableFog() {
    params->end = params->scale * params->baseEnd;
    fgRenderer->renderContext->Extension()->SetFogEnable(1);
}

// FUNC_AT(0x0007d840)
void RFog::SetFogParams() {
    params->end = params->scale * params->baseEnd;
    fgRenderer->renderContext->Extension()->SetFogDensity(std::bit_cast<uint32_t>(params->density));
    fgRenderer->renderContext->Extension()->SetFogStart(std::bit_cast<uint32_t>(params->start));
    fgRenderer->renderContext->Extension()->SetFogEnd(std::bit_cast<uint32_t>(params->end));
    fgRenderer->renderContext->Extension()->SetFogColour(params->colour);
    fgRenderer->renderContext->Extension()->SetFogTableMode(params->mode);
}

// The ticks are compared signed here, unsigned in FadeScale.
// FUNC_AT(0x0007d8c0)
void RFog::UpdateScale() {
    if (!fading)
        return;
    if ((int32_t)GameTick < (int32_t)fadeEndTick) {
        int32_t remaining = fadeEndTick - GameTick;
        params->scale = (float)(fadeTarget - remaining * (double)fadeRate);
    } else {
        params->scale = fadeTarget;
        fading = 0;
    }
    params->end = params->scale * params->baseEnd;
    SetFogParams();
}

// FUNC_AT(0x0007d930)
RFog* RFog::Construct() {
    singleton.vtable = kFogVtable;
    params = static_cast<FogParams *>(OperatorNew(sizeof(FogParams)));
    params->colour = kDefaultColour;
    params->scale = 1.0f;
    fading = 0;
    fadeEndTick = 0;
    fadeRate = 0.0f;
    fadeTarget = 1.0f;
    params->start = kDefaultStart;
    params->end = kDefaultEnd;
    params->baseEnd = kDefaultBaseEnd;
    params->mode = kDefaultMode;
    params->density = kDefaultDensity;

    TuningDBMgr->LoadDatabase("Render:Fog", TuningLevel, 0, false);
    dbattrib_float("fog START", &params->start, kDistanceMin, kDistanceMax, 0, 1.0f, NULL);
    dbattrib_float("fog END", &params->baseEnd, kDistanceMin, kDistanceMax, 0, 1.0f, NULL);
    dbattrib_u8("fog mode", &params->mode, 0, kModeMax, 0, -1.0f, NULL);
    dbattrib_float("fog Density", &params->density, kDensityMin, kDensityMax, 0, 1.0f, NULL);
    dbattrib_argb("Fog Colour", &params->colour, &FogColourFlag, 0);
    TuningDBMgr->CloseCurrent();

    params->end = params->baseEnd;
    SetFogParams();
    return this;
}

// FUNC_AT(0x0007daa0)
void RFog::Kill() {
    RFog *fog = Fog;
    if (fog != NULL)
        (fog->*XbeVirtual<DeleteMethod>(fog, 0))(1);
}

// The settings are not freed.
// FUNC_AT(0x0007dac0)
RFog* RFog::Delete(unsigned flags) {
    singleton.Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}
