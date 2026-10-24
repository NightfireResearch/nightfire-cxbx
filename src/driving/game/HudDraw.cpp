#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// GHud's gauges and screen effects. See HudDraw.h.

#pragma fp_contract(off)

#include "HudDraw.h"

#include "FrontEndSystem.h"
#include "Gallery.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "Subtitles.h"
#include "WeaponManager.h"
#include "../audio/Sound.h"               // AMenuSound
#include "../camera/CameraIniLoader.h"    // fgCameraModeIndices
#include "../camera/PlayerCamera.h"       // RPlayerCamera
#include "../engine/GameLoop.h"           // LaunchPage
#include "../platform/RealMath.h"
#include "../platform/RealSystem.h"       // TIMER_gettick
#include "../platform/X87.h"
#include "../render/Draw.h"
#include "../render/Materials.h"
#include "../render/RenderHigh.h"         // fgRenderHigh
#include "../render/Renderer.h"           // fgRenderer
#include "../world/Targeting.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ---- the game's state

// XBE_GLOBAL(0x00243a74, 0x2)
int16_t ScreenFade;
// The black fade's level while it runs (written, not read; name ours)
// XBE_GLOBAL(0x00243a70, 0x4)
float ScreenFadeLevel;
// DrawHealth's next grunt, 0..2 (name ours)
// XBE_GLOBAL(0x0023f564, 0x4)
int32_t HudGruntIndex;
// DrawFlyByWire's frame count, 0..30: the beep at 0, the dot below 15 (name ours)
// XBE_GLOBAL(0x0023f568, 0x4)
int32_t HudFlyByWireFrame;

namespace {

constexpr float kHealthFadeTime = 0.5f;
constexpr float kHealthBlinkTime = 0.5f;
constexpr float kHealthShowTime = 3.0f;
constexpr int kIrisSegments = 7;
constexpr int kArmSegments = 8;
constexpr float kOneSeventh = 1.0f / 7.0f;
static_assert(std::bit_cast<uint32_t>(kOneSeventh) == 0x3e124925, "the lock's step");
constexpr float kOneThirtieth = 0.0333333351f;
static_assert(std::bit_cast<uint32_t>(kOneThirtieth) == 0x3d088889, "a thirtieth");
constexpr float kNearlyLocked = 0.949999988f;
static_assert(std::bit_cast<uint32_t>(kNearlyLocked) == 0x3f733333, "0.95");
constexpr float kArrowScale = 1.2f;
static_assert(std::bit_cast<uint32_t>(kArrowScale) == 0x3f99999a, "1.2");
constexpr float kTextScale = 1.4f;
static_assert(std::bit_cast<uint32_t>(kTextScale) == 0x3fb33333, "1.4");
constexpr float kWideShift = 78.0f;                 // a wide screen's extra, each side
constexpr float kWideExtra = 156.0f;
constexpr float kBloodWideShift = 110.0f;
constexpr float kBloodWideExtra = 220.0f;
constexpr float kBloodTime = 8.0f;
constexpr float kBloodTexture = 128.0f;
constexpr float kStylinFadeTime = 0.5f;
constexpr float kStylinHoldTime = 3.0f;
constexpr float kStylinY = 95.0f;
constexpr float kTimerY = 35.0f;
constexpr int32_t kWeaponText64B = 0x64b;           // the weapon whose ammunition is not shown while it has any

// The pause errors' texts (locale symbols): the title, the controller to plug in (by port), the instruction
constexpr int32_t kTextControllerTitle = 0x1390;
constexpr int32_t kTextControllerPorts[4] = {0x1391, 0x1393, 0x1394, 0x1395};
constexpr int32_t kTextControllerInstruction = 0x1392;

// SimpleTexturedMaterial's alpha blend for the blood curtain's first layer
constexpr uint32_t kBlendBlood = 5;

const char *const kGrunts[3] = {"SFX_Bond_body_hit", "SFX_Bond_body_hit2", "SFX_Bond_body_hit3"};

const MATRIX4 kIdentity = {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
                            {0.0f, 0.0f, 0.0f, 1.0f}}};

// The blood's two strips of the texture: its top, and the rest
const SpriteTexCoords kBloodTop = {{0.0f, 0.05f, 1.0f, 0.0f}, {1.0f, 0.15f, 1.0f, 0.0f}};
const SpriteTexCoords kBloodRest = {{0.0f, 0.05f, 1.0f, 0.0f}, {1.0f, 0.95f, 1.0f, 0.0f}};

// A colour with its alpha scaled
uint32_t ScaleAlpha(uint32_t colour, float scale) {
    return (colour & 0xffffff) | uint32_t(RoundToInt(float(colour >> 24) * scale)) << 24;
}

// White, nearly opaque by `fraction`
uint32_t WhiteAlpha(float fraction) {
    return uint32_t(RoundToInt(fraction * 254.0f)) << 24 | 0xfefefe;
}

WeaponRecord& SelectedWeapon() {
    return WeaponManager->weapons[WeaponManager->secondary];
}

bool InMissileCamera() {
    return fgRenderHigh->views[0].camera->cameraMode == fgCameraModeIndices.missile;
}

} // namespace

// The iris's seven segments for the health, the armour's eight for the shield: lit, the last one in part, the
// rest dim; shown for a while after a change, then faded, and blinking when low. In auto-drive a drop of a 32nd of
// the health grunts.
// FUNC_AT(0x000d81d0)
void GHud::DrawHealth() {
    if (superEasy || missionMsgLong || GSubtitles::TheApp()->IsSubtitleDisplaying())
        return;
    float health = glbMissionManager->playerHealth / glbMissionManager->playerMaxHealth;
    float alpha = 1.0f;
    float shield = glbMissionManager->playerShield / glbMissionManager->playerMaxShield;
    if (!unknown431 && healthShowTime <= 0.0f) {
        if (healthFadeTime > 0.0f)
            alpha = healthFadeTime / kHealthFadeTime;
        else
            alpha = 0.0f;
    }
    if (healthBlinkTime > 0.0f)
        alpha = healthBlinkTime / kHealthBlinkTime;

    double irisValue = (double)health * kIrisSegments;
    int irisLit = Ftol(irisValue);
    double armValue = (double)shield * kArmSegments;
    int armLit = Ftol(armValue);
    float irisPart = (float)(irisValue - irisLit);
    float armPart = (float)(armValue - armLit);
    bool draw = true;
    if (health <= 0.0f) {
        alpha = 1.0f;
        irisLit = 0;
        armLit = 0;
    } else if (!(alpha > 0.0f)) {
        draw = false;
    }

    if (draw) {
        uint32_t lit, dim;
        if (health > 0.5f) {
            lit = HudHealthColour;
            dim = HudHealthDimColour;
        } else if (health > 0.25f) {
            lit = HudHealthLowColour;
            dim = HudHealthLowDimColour;
        } else {
            lit = HudHealthCriticalColour;
            dim = HudHealthCriticalDimColour;
        }
        lit = ScaleAlpha(lit, alpha);
        dim = ScaleAlpha(dim, alpha);
        uint32_t irisPartColour = ScaleAlpha(lit, irisPart);
        uint32_t armPartColour = ScaleAlpha(WhiteAlpha(armPart), alpha);
        uint32_t armColour = WhiteAlpha(alpha);

        for (int i = 0; i < kIrisSegments; i++) {
            gallery->SPRITE_SetState(irisPuppets[i]);
            gallery->colour = dim;
            gallery->SPRITE_Draw2D(irisPuppets[i]);
        }
        for (int i = 0; i < irisLit; i++) {
            gallery->SPRITE_SetState(irisPuppets[i]);
            gallery->colour = lit;
            gallery->SPRITE_Draw2D(irisPuppets[i]);
        }
        if (health < 1.0f) {
            gallery->SPRITE_SetState(irisPuppets[irisLit]);
            gallery->colour = irisPartColour;
            gallery->SPRITE_Draw2D(irisPuppets[irisLit]);
        }
        for (int i = 0; i < armLit; i++) {
            gallery->SPRITE_SetState(armPuppets[i]);
            gallery->colour = armColour;
            gallery->SPRITE_Draw2D(armPuppets[i]);
        }
        if (shield < 1.0f) {
            gallery->SPRITE_SetState(armPuppets[armLit]);
            gallery->colour = armPartColour;
            gallery->SPRITE_Draw2D(armPuppets[armLit]);
        }
        gallery->SPRITE_SetState(healthBorderPuppet);
        gallery->colour = lit;
        gallery->SPRITE_Draw2D(healthBorderPuppet);
    }

    if (autoDrive && Ftol((double)health * 32.0) < Ftol((double)shownHealth * 32.0)) {
        const char *grunt = kGrunts[HudGruntIndex];
        HudGruntIndex++;
        AMenuSound::Trigger(7, grunt, "Player Grunts", 2);
        if (HudGruntIndex == 3)
            HudGruntIndex = 0;
    }

    if (healthShowTime > 0.0f) {
        healthShowTime -= elapsed;
        if (healthShowTime <= 0.0f) {
            healthShowTime = 0.0f;
            healthFadeTime = kHealthFadeTime;
        }
    }
    if (healthFadeTime > 0.0f) {
        healthFadeTime -= elapsed;
        if (healthFadeTime <= 0.0f)
            healthFadeTime = 0.0f;
    }
    if (health <= 0.25f) {
        healthBlinkTime -= elapsed;
        if (healthBlinkTime <= 0.0f)
            healthBlinkTime = kHealthBlinkTime;
    } else {
        healthBlinkTime = 0.0f;
    }
    if (!(health == shownHealth) || !(shield == shownShield)) {
        shownHealth = health;
        shownShield = shield;
        healthShowTime = kHealthShowTime;
    }
}

// The hole over the whole screen, fading with the flash's count.
// FUNC_AT(0x000d8820)
void GHud::DrawScreenHits() {
    if (letterBoxMode != kLetterBoxOff)
        return;
    int flash = damageFlash;
    int length = damageFlashLength;
    if (flash <= 0 || length <= 0) {
        damageFlashLength = 0;
        return;
    }
    float fraction = (float)((double)flash / length);
    float width = screenWidth;
    float left = 0.0f;
    float height = screenHeight;
    float holeWidth = holePuppet->sprite.width;
    float holeHeight = holePuppet->sprite.height;
    if (fgRenderer->widescreen) {
        width += kWideExtra;
        left = -kWideShift;
    }
    gallery->SPRITE_SetState(holePuppet);
    gallery->scale.z = 1.0f;
    gallery->scale.x = width / holeWidth;
    gallery->scale.y = height / holeHeight;
    gallery->colour = ScaleAlpha(HudHoleColour, fraction);
    gallery->SPRITE_Draw2D(Ftol(left), 0, holePuppet, NULL);
    damageFlash--;
}

// The black fade over the screen: black for the renderer's first three frames whatever the fade.
// FUNC_AT(0x000d8970)
void GHud::DrawScreenFades() {
    if (blackFadeAnimating) {
        if (paused) {
            blackFadePct = 0.0f;
            blackFadeAnimating = false;
            ScreenFadeLevel = 0.0f;
            ScreenFade = 0;
        } else {
            blackFadePct = (float)((double)(fgSimulation.stepCount - blackFadeStartStep) * blackFadeRate + blackFadeStart);
            if (blackFadePct < 0.0f) {
                blackFadePct = 0.0f;
                blackFadeAnimating = false;
                ScreenFadeLevel = 0.0f;
                ScreenFade = 0;
            } else if (blackFadePct > 1.0f) {
                blackFadePct = 1.0f;
                blackFadeAnimating = false;
                ScreenFadeLevel = 0.0f;
                ScreenFade = 0;
            } else {
                ScreenFadeLevel = blackFadePct;
            }
        }
    }
    if (!(blackFadePct > 0.0f) && (int32_t)fgRenderer->frameCount >= 3)
        return;
    uint32_t alpha = Truncate(blackFadePct * 255.0f) & 0xff;
    if ((int32_t)fgRenderer->frameCount < 3)
        alpha = 0xff;
    if (fgRenderer->widescreen)
        Draw::DrawBox(-kWideShift, 0.0f, screenWidth + kWideExtra, screenHeight, alpha << 24);
    else
        Draw::DrawBox(0.0f, 0.0f, screenWidth, screenHeight, alpha << 24);
}

// The guided torpedo's overlay: the four corners, the dot blinking, the text; a beep every 31 frames.
// FUNC_AT(0x000d8ae0)
void GHud::DrawFlyByWire() {
    if (!paused && HudFlyByWireFrame == 0)
        AMenuSound::Trigger(0, "SFX_Gtorpbeep", "Weapons Init", 2);
    for (int i = 0; i < 4; i++) {
        gallery->SPRITE_SetState(fbwPuppets[i]);
        gallery->BITMAP_Draw2D(fbwPuppets[i]);
    }
    if (HudFlyByWireFrame < 15) {
        gallery->SPRITE_SetState(guidedDotPuppet);
        gallery->SPRITE_Draw2D(guidedDotPuppet);
    }
    gallery->FONT_SetState(guidedTxtPuppet);
    gallery->FONT_SetScale(0.75f, 0.75f);
    gallery->FONT_Draw(&guidedTxtPuppet->text, NULL);
    int next = HudFlyByWireFrame + 1;
    HudFlyByWireFrame = next > 30 ? 0 : next;
}

// The lock's crosshairs closing in on the selected target over seven frames, turning as they come; their colour by
// the weapon, which is answered.
// FUNC_AT(0x000d8c00)
uint32_t GHud::DrawZoomingCrossHairs(GalleryKey *puppet) {
    if (!zoomCrossHairsOn)
        zoomCrossHairsOn = true;
    MATRIX4 rotate = kIdentity;
    MATRIX4 translate = kIdentity;
    MATRIX4 transform = kIdentity;
    WTargetable *selected = TargetPicker.GetSelectedTarget();
    float x = selected->screenPos.x;
    float y = selected->screenPos.y;
    if (!fgRenderer->widescreen)
        x = (float)(((double)x - targetOffsetX) / targetScaleX);
    y = (float)(((double)y - targetOffsetY) / targetScaleY);
    lockX = x;
    lockY = y;
    float zoom = (float)((double)(uint32_t)lockFrames * kOneSeventh);
    if ((uint32_t)lockFrames < 7) {
        lockFrames++;
        lockFramesShown = lockFrames;
    }
    double width = puppet->sprite.width;
    float scale = (float)(1.0 - (1.0 - crossTorpRoamPuppet->sprite.width / (width + width)) * zoom);

    uint32_t colour, drawn;
    switch (SelectedWeapon().id) {
    case kWeaponGun:
        colour = HudCrossColour4f8;
        drawn = zoom > kNearlyLocked ? HudCrossColour4fc : colour;
        break;
    case kWeaponMissile:
    case kWeaponUltralightRocket:
    case kWeaponFodRocket:
        if (zoom > kNearlyLocked) {
            colour = HudCrossColour4f8;
            drawn = HudCrossColour4fc;
        } else {
            colour = HudCrossColour;
            drawn = HudCrossColour4f8;
        }
        break;
    case kWeaponTorpedo:
        if (zoom > kNearlyLocked) {
            colour = HudCrossColour4ec;
            drawn = HudCrossColour4f0;
        } else {
            colour = HudCrossColour;
            drawn = HudCrossColour4ec;
        }
        break;
    default:
        colour = 0xfefefefe;
        drawn = colour;
        break;
    }

    BuildRotate(&rotate, (float)(90.0 * zoom - 90.0), 0.0f, 0.0f, 1.0f);
    BuildTranslate(&translate, x, y, 0.0f);
    VU0_MATRIX4_mult(&transform, &rotate, &translate);
    Draw::SetModelMatrix(&transform);
    gallery->SPRITE_SetState(puppet);
    gallery->colour = drawn;
    gallery->scale.x = scale;
    gallery->scale.y = scale;
    gallery->scale.z = 1.0f;
    gallery->SPRITE_Draw2D(0, 0, puppet, NULL);
    Draw::SetModelMatrix(NULL);
    return colour;
}

// The lock frame fading where the zooming crosshairs left it, at the size they reached, as the count runs down.
// FUNC_AT(0x000d8ea0)
void GHud::DrawFadingCrossHairs(GalleryKey *puppet) {
    MATRIX4 transform = kIdentity;
    double width = puppet->sprite.width;
    float scale = (float)(1.0 - (1.0 - crossTorpRoamPuppet->sprite.width / (width + width)) *
                                    ((double)(uint32_t)lockFramesShown * kOneSeventh));
    float fade = (float)((double)(uint32_t)lockFrames * kOneSeventh);
    if ((uint32_t)lockFrames == 0)
        return;
    lockFrames--;
    TargetPicker.GetSelectedTarget();   // (not used)
    uint32_t colour;
    if (SelectedWeapon().id != kWeaponTorpedo)
        colour = ScaleAlpha(HudCrossColour4fc, fade);
    else
        colour = ScaleAlpha(HudCrossColour4f0, fade);
    BuildTranslate(&transform, lockX, lockY, 0.0f);
    Draw::SetModelMatrix(&transform);
    gallery->SPRITE_SetState(puppet);
    gallery->colour = colour;
    gallery->scale.x = scale;
    gallery->scale.y = scale;
    gallery->scale.z = 1.0f;
    gallery->SPRITE_Draw2D(0, 0, targetLockPuppet, NULL);
    Draw::SetModelMatrix(NULL);
}

// The text centred on the puppet's x, at its y, in the gallery's colour (each symbol fetched twice)
static void DrawPauseErrorText(GHud *hud, GalleryKey *puppet, int symbol) {
    GGallery *gallery = hud->gallery;
    gallery->FONT_SetState(puppet);
    gallery->FONT_SetScale(1.0f, 1.0f);
    float x = (float)(puppet->position.x - gallery->FONT_Width(GSystem::LOCALE_Symbol(symbol)) * 0.5);
    gallery->FONT_Draw(GSystem::LOCALE_Symbol(symbol), x, puppet->position.y, gallery->colour);
}

// The controller is unplugged: the box, the title, which controller, what to do.
// FUNC_AT(0x000d93d0)
void GHud::DrawPauseErrors() {
    gallery->QUAD_SetState(pauseErrQuadPuppets[0]);
    gallery->QUAD_Draw(pauseErrQuadPuppets[0], 1.0f);
    DrawPauseErrorText(this, pauseErrTextPuppets[0], kTextControllerTitle);
    // (jump table 0x000d9560, decoded from its bytes)
    int port = Launch.controllerPort;
    int text = (uint32_t)port <= 3 ? kTextControllerPorts[port] : kTextControllerPorts[0];
    DrawPauseErrorText(this, pauseErrTextPuppets[1], text);
    DrawPauseErrorText(this, pauseErrTextPuppets[2], kTextControllerInstruction);
}

// The pause menu's arrow at x, y, fading out and in a step at a time, by the real clock.
// FUNC_AT(0x000d9570)
void GHud::DrawBlinkingArrow(float x, float y) {
    double now = TIMER_gettick();
    if (arrowState == 0) {
        arrowState = 1;
        arrowTick = (float)TIMER_gettick();
        arrowTime = 0.0f;
    } else {
        arrowTime = (float)((now - arrowTick) * fgSimulation.stepsPerSecond + arrowTime);
        arrowTick = (float)now;
        if (Ftol(arrowTime) >= 1) {
            arrowTime = 0.0f;
            if (arrowState == 1)
                arrowAlpha = arrowAlpha - HudArrowFadeStep;
            else if (arrowState == 2)
                arrowAlpha = HudArrowFadeStep + arrowAlpha;
            if (arrowAlpha >= 1.0f) {
                arrowState = 1;
                arrowAlpha = 1.0f;
            } else if (arrowAlpha <= 0.0f) {
                arrowState = 2;
                arrowAlpha = 0.0f;
            }
        }
    }
    uint32_t colour = WhiteAlpha(arrowAlpha);
    gallery->SPRITE_SetState(arrowPuppet);
    gallery->colour = colour;
    gallery->scale.x = kArrowScale;
    gallery->scale.y = kArrowScale;
    gallery->scale.z = 1.0f;
    gallery->SPRITE_Draw2D(Ftol(x), Ftol(y), arrowPuppet, NULL);
}

// The gadget's puppet pulsing, then growing in from the centre of the screen to its place.
// FUNC_AT(0x000d9c50)
void GHud::DrawGadget() {
    if (!(gadgetTime > 0.0f))
        return;
    GalleryKey *puppet = gadget >= 0 && gadget < 6 ? gadgetPuppets[gadget] : gadgetPuppets[3];
    uint32_t colour = (HudGadgetColour & 0xffffff) |
        uint32_t(RoundToInt((float)((double)(HudGadgetColour >> 24) * ((double)gadgetPulse / 0.5)))) << 24;
    double grow = (double)gadgetGrow / 0.5;
    double away = (3.0 - (grow + grow)) * (grow * grow);     // smoothstep
    double home = 1.0 - away;
    float x = (float)((centerX - puppet->sprite.width * 0.5) * away + home * puppet->position.x);
    float y = (float)(((double)centerY - puppet->sprite.height) * away + home * puppet->position.y);
    gallery->SPRITE_SetState(puppet);
    gallery->colour = colour;
    gallery->SPRITE_Draw2D(Ftol(x), Ftol(y), puppet, NULL);

    gadgetTime -= elapsed;
    if (gadgetTime <= 0.0f)
        gadgetTime = 0.0f;
    if (gadgetPulses <= 0)
        gadgetGrow -= elapsed;
    if (gadgetGrow <= 0.0f)
        gadgetGrow = 0.0f;
    if (gadgetPulses > 0)
        gadgetPulse -= elapsed;
    if (gadgetPulse <= 0.0f) {
        gadgetPulses--;
        gadgetPulse = 0.5f;
    }
}

// The bars at the top and bottom of the screen, opening or closing a step a frame.
// FUNC_AT(0x000d9e30)
void GHud::DrawLetterBox() {
    if (letterBoxMode == kLetterBoxOff)
        return;
    if (letterBoxMode == kLetterBoxOpening) {
        letterBoxSize++;
        if (letterBoxSize >= kLetterBoxSteps) {
            letterBoxSize = kLetterBoxSteps;
            letterBoxMode = kLetterBoxOn;
        }
    }
    if (letterBoxMode == kLetterBoxClosing) {
        letterBoxSize--;
        if (letterBoxSize <= 0) {
            letterBoxSize = 0;
            letterBoxMode = kLetterBoxOff;
        }
    }
    int height = Ftol((double)letterBoxSize * 65.0 * kOneThirtieth);
    if (fgRenderer->videoMode == RRenderer::kVideoLetterbox)
        height = Ftol((double)height * 1.5);
    SimpleMaterial->SetDepthTestMethod(0x207);  // always
    float bar = (float)height;
    Draw::DrawBox(0.0f, 0.0f, screenWidth, bar, HudLetterBoxColour);
    Draw::DrawBox(0.0f, screenHeight - bar, screenWidth, bar, HudLetterBoxColour);
}

// "Stylin": the puppet zooms in from nothing, wobbling (the zoom takes 100 frames' worth of frame rate), holds for
// three seconds and fades. The fade counts down in steps, not seconds.
// FUNC_AT(0x000da540)
void GHud::DrawStylin() {
    if (stylinState == 0)
        return;
    float zoomTime = 100.0f / frameRate;
    float x = centerX;
    float y = kStylinY;
    GalleryKey *puppet = stylinPuppet;
    if (stylinState == 1) {
        if (stylinTime > 0.0f && stylinTime > zoomTime) {
            double left = (double)stylinTime - zoomTime;
            double inverse = 1.0 / zoomTime;
            float scale = (float)((zoomTime - left) * inverse);
            double turns = inverse * left;
            turns += turns;
            if (turns > 1.0) {
                do
                    turns -= 1.0;
                while (turns > 1.0);
            }
            float wobble = (float)CosineTurnsTimes((float)turns, scale);
            stylinScale = scale;
            gallery->SPRITE_SetState(puppet);
            gallery->scale.y = wobble;
            gallery->scale.x = scale;
            gallery->scale.z = 1.0f;
            gallery->colour = HudStylinColour;
            int top = Ftol(y - (double)wobble * puppet->sprite.height * 0.5);
            int left2 = Ftol(x - (double)scale * puppet->sprite.width * 0.5);
            gallery->SPRITE_Draw2D(left2, top, puppet, NULL);
            stylinTime -= elapsed;
        } else {
            stylinState = 2;
            stylinTime = kStylinHoldTime;
        }
    }
    if (stylinState == 2) {
        double alpha = 1.0;
        if (stylinTime <= 0.0f)
            alpha = stylinFade > 0.0f ? (double)stylinFade / kStylinFadeTime : 0.0;
        uint32_t colour = uint32_t(RoundToInt((float)((double)(HudStylinColour >> 24) * alpha))) << 24 |
                          (HudStylinColour & 0xffffff);
        gallery->SPRITE_SetState(puppet);
        gallery->colour = colour;
        gallery->scale.y = stylinScale;
        gallery->scale.z = 1.0f;
        gallery->scale.x = stylinScale;
        int top = Ftol(y - (double)puppet->sprite.height * stylinScale * 0.5);
        int left = Ftol(x - (double)puppet->sprite.width * stylinScale * 0.5);
        gallery->SPRITE_Draw2D(left, top, puppet, NULL);
        if (stylinFade > 0.0f) {
            stylinFade = (float)((double)stylinFade - (double)(uint32_t)steps);
            if (stylinFade <= 0.0f)
                stylinFade = 0.0f;
        }
        if (stylinTime > 0.0f) {
            stylinTime -= elapsed;
            if (stylinTime <= 0.0f) {
                stylinTime = 0.0f;
                stylinFade = kStylinFadeTime;
                stylinState = 0;
            }
        }
    }
}

// The blood running down the screen after the player loses, eight seconds long, in two blends.
// FUNC_AT(0x000dc590)
void GHud::DrawBloodCurtain() {
    if (InMissileCamera() || missionResultState == 0 || bloodCurtainTime < 0.0f)
        return;
    double inverse = 1.0 / kBloodTime;
    float width = screenWidth;
    float left = 0.0f;
    double eighth = inverse * screenHeight;
    float textureScale = (float)(inverse * kBloodTexture);
    double gone = (double)kBloodTime - bloodCurtainTime;
    float goneFloat = (float)gone;
    float drop = (float)(gone * eighth);
    float rest = goneFloat * textureScale;
    if (fgRenderer->widescreen || fgRenderer->videoMode == RRenderer::kVideoLetterbox) {
        width += kBloodWideExtra;
        left = -kBloodWideShift;
    }
    SimpleTexturedMaterial->SetAlphaBlendMode(kBlendBlood);
    Draw::DrawSprite(left, 0.0f, width, drop, HudBloodColour, &kBloodTop, bloodTar);
    Draw::DrawSprite(left, drop, width, rest, HudBloodColour, &kBloodRest, bloodTar);
    SimpleTexturedMaterial->SetAlphaBlendMode(kBlendAdditive);
    Draw::DrawSprite(left, 0.0f, width, drop, HudBloodColour2, &kBloodTop, bloodTar);
    Draw::DrawSprite(left, drop, width, rest, HudBloodColour2, &kBloodRest, bloodTar);
    SimpleTexturedMaterial->SetAlphaBlendMode(kBlendNormal);
    bloodCurtainTime -= elapsed;
    if (bloodCurtainTime <= 0.0f)
        bloodCurtainTime = 0.0f;
}

// The mission timer at the top centre in whole seconds, in the warning colour and beeping for its last ten when it
// counts down.
// FUNC_AT(0x000dc7d0)
void GHud::DrawTimer() {
    float x = (float)(screenWidth * 0.5);
    if (timer < 0)
        return;
    int seconds = glbMissionManager->timers[timer].GetValue() / fgSimulation.stepsPerSecond;
    bool reverse = glbMissionManager->timers[timer].IsReverse();
    if (seconds <= 0)
        return;
    gallery->FONT_SetState(scratchTxtPuppet);
    gallery->FONT_SetScale(kTextScale, kTextScale);
    float width = (float)gallery->FONT_Width("00:00");
    double height = gallery->FONT_Height("00:00");
    x = (float)(x - width * 0.5);
    gallery->QUAD_Draw2D(x, kTimerY - 2.0f, width, (float)(height + height - 2.0), HudTimerBoxColour);
    int minutes = seconds / 60;
    int second = seconds % 60;
    char text[16];
    sprintf(text, "%2d:%02d", minutes, second);
    if (reverse && minutes == 0 && second < 10) {
        DrawFontAndShadow(scratchTxtPuppet, x, kTimerY, HudShadowColour, HudTimerWarningColour, kTextScale, text);
        if (timerLastSecond - second == 1) {
            AMenuSound::Trigger(0, "SFX_timerbeep", "POV Weapons", 0);
            timerLastSecond = second;
        }
    } else {
        DrawFontAndShadow(scratchTxtPuppet, x, kTimerY, HudShadowColour, HudTimerColour, kTextScale, text);
    }
}

// The selected weapon's ammunition and name, right-aligned on their puppets.
// FUNC_AT(0x000dc990)
void GHud::DrawWeapon() {
    if (superEasy || missionMsgLong || GSubtitles::TheApp()->IsSubtitleDisplaying())
        return;
    WeaponRecord &weapon = SelectedWeapon();
    if (weapon.textId != kWeaponText64B || weapon.ammo == 0) {
        char ammo[60];
        sprintf(ammo, "%d", weapon.ammo < 0 ? 0 : weapon.ammo);
        gallery->FONT_SetScale(1.0f, 1.0f);
        float x = (float)(ammoPuppet->position.x - gallery->FONT_Width(ammo));
        DrawFontAndShadow(ammoPuppet, x, ammoPuppet->position.y, HudShadowColour, HudTextColour, 1.0f, ammo);
    }
    char name[60];
    sprintf(name, "%s", GSystem::LOCALE_Symbol(weapon.textId));
    gallery->FONT_SetState(weaponPuppet);
    float x = (float)(weaponPuppet->position.x - gallery->FONT_Width(name));
    DrawFontAndShadow(weaponPuppet, x, weaponPuppet->position.y, HudShadowColour, HudTextColour, 1.0f, name);
}
