#pragma fp_contract(off)

#include "Colorize.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../anim/Character.h"            // MakeCoord4
#include "../anim/Manager.h"              // ActManager::SetIRMode
#include "../eagl/GeoPrimState.h"
#include "../eagl/RenderContext.h"
#include "../engine/CoreFoundation.h"     // GetFoundationVideoMode
#include "../engine/MissionManager.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "Draw.h"
#include "Lights.h"
#include "Materials.h"
#include "Renderer.h"

// ---------------------------------------------------------------------------------------------------------------
// RColorize (0x0009a420-0x0009ac40), ported from the listing. SetEnabled's jump table at 0x0009a6d0 holds the four
// cases in order (0x0009a520, 0x0009a547, 0x0009a5e9, 0x0009a656), no byte table in front.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// The missile camera as the colouring uses it (RMissileCam; only these fields)
struct ColorizeMissileCamView {
    uint8_t unknown00[0x10];
    uint8_t unknown10;                  // +0x10 set in the infrared mode and mode 2
    uint8_t unknown11[3];
    int32_t unknown14;                  // +0x14 0x20 infrared, 0x40 mode 2
};

} // namespace

// ---- globals
#define MissileCam (*(ColorizeMissileCamView **)0x00200f54)
#define SimStepCount U32_AT(0x00234e34)
#define InfraredStartBrightness FLOAT_AT(0x001c487c)    // 0.4: the previous brightness the infrared mode starts from
#define BrightnessLightScale FLOAT_AT(0x001c4880)       // 100
#define BrightnessEaseSteps I32_AT(0x001c4884)          // 240: the steps the light takes to follow the brightness
#define ColorizeUnknown1c4888 FLOAT_AT(0x001c4888)      // set to 1 when the video mode is not 1; not read
#define ColorizeUnknown1c488c FLOAT_AT(0x001c488c)      // set to 0.625 then
#define VideoModeChecked BOOL8_AT(0x001f68a0)           // a function-local static's guard

namespace {

constexpr uintptr_t kRColorizeVtable = 0x001927c8;
constexpr int32_t kMotionBlurDrawn = 2;
constexpr int kVideoMode1 = 1;

// GeoPrimState depth tests, in OpenGL's numbering
constexpr uint32_t kDepthGreaterEqual = 0x206;
constexpr uint32_t kDepthAlways = 0x207;

constexpr float kInfraredLight = 0.06f;
static_assert(std::bit_cast<uint32_t>(kInfraredLight) == 0x3d75c28f, "the infrared light's level");

EAGL::RenderContextExtension *Extension(EAGL::RenderContext *context) {
    return reinterpret_cast<EAGL::RenderContextExtension *>(context);
}

// Each channel of an ARGB colour times `scale`, truncated to a byte
uint32_t ScaleColour(uint32_t colour, double scale) {
    uint32_t scaled = uint8_t(Ftol(double(colour >> 24) * scale));
    scaled = scaled << 8 | uint8_t(Ftol(double(colour >> 16 & 0xff) * scale));
    scaled = scaled << 8 | uint8_t(Ftol(double(colour >> 8 & 0xff) * scale));
    return scaled << 8 | uint8_t(Ftol(double(colour & 0xff) * scale));
}

void SetLight(RColorize *colorize, float level) {
    Coord4 colour;
    MakeCoord4(&colour, level, level, level, level);
    colorize->colourA = colour;
    colorize->colourB = colour;
}

} // namespace

// FUNC_AT(0x0009a420)
RColorize* RColorize::Construct() {
    vtable = reinterpret_cast<const void *>(kRColorizeVtable);
    mode = kModeOff;
    brightnessChangeTime = 0;
    areaBrightness = 0.0f;
    prevAreaBrightness = 0.0f;
    motionBlurAmount = 0.0f;
    frontBuffer = Extension(fgRenderer->renderContext)->GetFrontBuffer();
    return this;
}

// The instance is not cleared, as the original has it.
// FUNC_AT(0x0009a480)
void RColorize::Kill() {
    typedef RColorize *(RColorize::*DeletingDestructor)(unsigned flags);
    RColorize *colorize = Colorize;
    if (colorize != NULL)
        (colorize->*XbeVirtual<DeletingDestructor>(colorize, 0))(1);
}

// FUNC_AT(0x0009a4a0)
void RColorize::SetAreaBrightness(float brightness) {
    prevAreaBrightness = areaBrightness;
    areaBrightness = float(double(brightness) * brightness * 10.0f);
    brightnessChangeTime = SimStepCount;
}

// FUNC_AT(0x0009a4d0)
void RColorize::EnableMotionBlur(float amount) {
    motionBlurState = 0;
    motionBlurAmount = amount;
}

// FUNC_AT(0x0009a4f0)
void RColorize::DisableMotionBlur() {
    motionBlurState = 1;
    motionBlurAmount = 0.0f;
}

// FUNC_AT(0x0009a500)
void RColorize::SetEnabled(int newMode) {
    mode = newMode;
    switch (newMode) {
    case kModeOff:
        ActManager::SetIRMode(false);
        MissileCam->unknown10 = 0;
        fgLightManager->forcePositionalLighting = false;
        break;
    case kModeInfrared:
        ActManager::SetIRMode(true);
        MissileCam->unknown10 = 1;
        MissileCam->unknown14 = 0x20;
        fgLightManager->forcePositionalLighting = true;
        tintColour = 0xff001010;
        tintColour3 = 0xffff0050;
        SetLight(this, kInfraredLight);
        tintColour2 = 0xff001010;
        prevAreaBrightness = InfraredStartBrightness;
        areaBrightness = 0.0f;
        brightnessChangeTime = SimStepCount;
        break;
    case kMode2:
        MissileCam->unknown10 = 1;
        MissileCam->unknown14 = 0x40;
        tintColour = 0xff282840;
        tintColour2 = 0xff282840;
        tintColour3 = 0xff008080;
        SetLight(this, 0.0f);
        break;
    case kMode3:
        tintColour = 0xff000000;
        tintColour3 = 0xff000000;
        SetLight(this, kInfraredLight);
        tintColour2 = 0xff000000;
        prevAreaBrightness = InfraredStartBrightness;
        areaBrightness = 10.0f;
        brightnessChangeTime = SimStepCount;
        break;
    }
}

// FUNC_AT(0x0009a6e0)
void RColorize::Draw() {
    if (motionBlurState != kMotionBlurDrawn)
        motionBlurState = kMotionBlurDrawn;

    // The last frame over this one
    if (motionBlurAmount > 0.0f) {
        SimpleTexturedMaterial->SetDepthTestMethod(kDepthAlways);
        SimpleTexturedMaterial->SetAlphaBlendMode(1);
        SimpleTexturedMaterial->SetTransparencyMethod(1);
        float width, height;
        fgRenderer->renderContext->GetSize(&width, &height);
        const SpriteTexCoords uvRect = { { 0.0f, 0.0f, 0.0f, 0.0f }, { width, height, 0.0f, 0.0f } };
        uint32_t alpha = uint8_t(Ftol(double(motionBlurAmount) * 255.0f));
        Draw::DrawSprite(0.0f, 0.0f, float(fgRenderer->screenWidth), float(fgRenderer->screenHeight),
                         alpha << 24 | 0xffffff, &uvRect, frontBuffer);
        SimpleTexturedMaterial->SetDepthTestMethod(kDepthGreaterEqual);
    }

    if (mode == kModeOff || glbMissionManager->unknown4f0 != 0)
        return;

    if (mode == kMode3) {
        float level = blastStrength + 1.0f;
        Coord4 colour;
        MakeCoord4(&colour, float(double(blastStrength) * 0.5f + 1.0f), level, level, level);
        colourA = colour;
        colourB = colour;
        int red = Ftol(double(blastStrength) * 64.0f);
        int green = Ftol(double(blastStrength) * 32.0f);
        int blue = green;
        if (red > 0xff)
            red = 0xff;
        if (green > 0xff)
            green = 0xff;
        if (blue > 0xff)
            blue = 0xff;
        uint32_t tint = red;
        tint = tint << 8 | green;
        tintColour = tint << 8 | blue;
    }

    if (!VideoModeChecked) {
        VideoModeChecked = 1;
        if (GetFoundationVideoMode() != kVideoMode1) {
            ColorizeUnknown1c488c = 0.625f;
            ColorizeUnknown1c4888 = 1.0f;
        }
    }

    // The light at the eye: in the infrared mode, eased from the previous area brightness to the new one
    const Coord4 position = { 0.0f, 0.0f, 0.0f, 0.0f };
    uint32_t changeTime = brightnessChangeTime;
    if (changeTime != 0 && mode == kModeInfrared) {
        uint32_t now = SimStepCount;
        if (int32_t(now - changeTime) > BrightnessEaseSteps) {
            brightnessChangeTime = 0;
            VU0_v4scale4(&colourA, float(double(BrightnessLightScale) * areaBrightness + 1.0f), &colourB);
            tintColour2 = ScaleColour(tintColour, double(areaBrightness) + 1.0f);
        } else if (now != changeTime) {
            double t = 1.0 - 1.0 / (double(int32_t(now)) - double(int32_t(changeTime)));
            float brightness = float((1.0 - t) * prevAreaBrightness + t * areaBrightness);
            Coord4 light;
            VU0_v4scale4(&colourA, float(double(brightness) * BrightnessLightScale + 1.0f), &light);
            fgLightManager->AddPositionalLight(&position, &light);
            tintColour2 = ScaleColour(tintColour, double(brightness) + 1.0f);
        }
    } else {
        fgLightManager->AddPositionalLight(&position, &colourB);
    }

    // The tints: two full-screen boxes
    SimpleTexturedMaterial->SetDepthTestMethod(kDepthAlways);
    SimpleTexturedMaterial->SetTransparencyMethod(1);
    SimpleMaterial->SetDepthTestMethod(kDepthAlways);
    SimpleMaterial->SetTransparencyMethod(1);
    SimpleMaterial->SetAlphaBlendMode(5);
    if (tintColour2 != 0) {
        SimpleMaterial->SetAlphaBlendMode(2);
        Draw::DrawBox(0.0f, 0.0f, float(fgRenderer->screenWidth), float(fgRenderer->screenHeight), tintColour2);
    }
    if (tintColour3 != 0) {
        SimpleMaterial->SetAlphaBlendMode(5);
        Draw::DrawBox(0.0f, 0.0f, float(fgRenderer->screenWidth), float(fgRenderer->screenHeight), tintColour3);
    }
    SimpleMaterial->SetAlphaBlendMode(1);
    SimpleTexturedMaterial->SetAlphaBlendMode(1);
}

// FUNC_AT(0x0009ac00)
void RColorize::Reset() {
    mode = kModeOff;
    ActManager::SetIRMode(false);
    MissileCam->unknown10 = 0;
    fgLightManager->forcePositionalLighting = false;
    prevAreaBrightness = areaBrightness;
    areaBrightness = 0.0f;
    brightnessChangeTime = SimStepCount;
}
