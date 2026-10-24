#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// GHud's crosshairs. See HudCrossHairs.h.

#pragma fp_contract(off)

#include "HudCrossHairs.h"
#include "../../common/math/Libm.h"

#include "AIHands.h"                      // AICharacterHands
#include "Gallery.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "Vehicle.h"                      // PVehicle
#include "VehicleSound.h"                 // AVehicle
#include "WeaponCameras.h"                // SniperZoom
#include "WeaponManager.h"
#include "../audio/ZoomObj.h"
#include "../camera/PlayerCamera.h"       // RPlayerCamera
#include "../data/AttributeSet.h"
#include "../engine/GameLoop.h"           // LaunchPage
#include "../platform/RealMath.h"         // BuildTranslate, BuildRotate, VU0_MATRIX4_mult
#include "../platform/X87.h"
#include "../render/Draw.h"
#include "../render/RenderHigh.h"         // fgRenderHigh
#include "../render/Renderer.h"           // fgRenderer
#include "../world/Targeting.h"
#include "../../common/xbeOverload.h"     // XbeVirtual

#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---- the game's state

// XBE_GLOBAL(0x0023f56c, 0x4)
int32_t HudAutoAimFrame;

namespace {

constexpr float kNearlyZoomed = 0.99f;              // the breath's zone (InTheZone) from here
static_assert(std::bit_cast<uint32_t>(kNearlyZoomed) == 0x3f7d70a4, "0.99");
constexpr float kZoomChange = 0.001f;
static_assert(std::bit_cast<uint32_t>(kZoomChange) == 0x3a83126f, "0.001");
constexpr float kDegreesPerRadian = 57.2957840f;
static_assert(std::bit_cast<uint32_t>(kDegreesPerRadian) == 0x42652ee2, "180/pi");
constexpr float kWideTextScale = 1.33333337f;
static_assert(std::bit_cast<uint32_t>(kWideTextScale) == 0x3faaaaab, "4/3");
constexpr float kRechargeHeight = 150.0f;
constexpr float kScopeArm = 152.0f;                 // the sniper scope's lines start this far from the aim
constexpr float kScopeLine = 1.25f;
constexpr float kWideShift = 78.0f;
constexpr float kScopeDim = 0.3f;
static_assert(std::bit_cast<uint32_t>(kScopeDim) == 0x3e99999a, "0.3");
constexpr float kScopeBright = 1.5f;
constexpr float kTickSpacing = 0.1f;
static_assert(std::bit_cast<uint32_t>(kTickSpacing) == 0x3dcccccd, "0.1");
constexpr float kTickRadius = -130.0f;
constexpr float kTickTurn = 8.0f;
constexpr float kZoneWidth = 118.0f;                // the torpedo's target zone round the screen's centre
constexpr float kZoneHeight = 10.0f;
constexpr uint32_t kWhite = 0xfefefefe;

const MATRIX4 kIdentity = {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
                            {0.0f, 0.0f, 0.0f, 1.0f}}};

#if NIGHTFIRE_X87
// FPATAN: atan2(y, x), as the x87 leaves it
__declspec(naked) double X87Atan2(double, double) {
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        fpatan
        ret
    }
}
#else
double X87Atan2(double y, double x) {
    return Libm::Atan2(y, x);
}
#endif

// A colour with its alpha scaled
uint32_t ScaleAlpha(uint32_t colour, float scale) {
    return (colour & 0xffffff) | uint32_t(RoundToInt(float(colour >> 24) * scale)) << 24;
}

// The player's car's engine sound (its GetAudio, through its vtable)
AVehicle *PlayerCarAudio() {
    typedef AVehicle *(PVehicle::*Method)();
    PVehicle *car = fgSimulation.cars.first[0];
    return (car->*XbeVirtual<Method>(car, PVehicle::kGetAudio))();
}

// The remote rocket's recharge for its sound (AFingoDeath's vtable slot 9)
void SetPlayerRecharge(float percent) {
    typedef void (AVehicle::*Method)(float percent);
    AVehicle *audio = PlayerCarAudio();
    (audio->*XbeVirtual<Method>(audio, 9))(percent);
}

void SetScale(GGallery *gallery, float x, float y) {
    gallery->scale.x = x;
    gallery->scale.y = y;
    gallery->scale.z = 1.0f;
}

// A puppet centred on the model matrix's origin
void DrawCentredAt(GHud *hud, GalleryKey *puppet, MATRIX4 *transform, uint32_t colour) {
    GGallery *gallery = hud->gallery;
    float width = puppet->sprite.width;
    float height = puppet->sprite.height;
    Draw::SetModelMatrix(transform);
    gallery->SPRITE_SetState(puppet);
    gallery->colour = colour;
    SetScale(gallery, 1.0f, 1.0f);
    gallery->SPRITE_Draw2D(Ftol(width * -0.5), Ftol(height * -0.5), puppet, NULL);
    Draw::SetModelMatrix(NULL);
}

// A puppet centred on the crosshairs' target
void DrawAtTarget(GHud *hud, GalleryKey *puppet, uint32_t colour) {
    GGallery *gallery = hud->gallery;
    float width = puppet->sprite.width;
    float height = puppet->sprite.height;
    gallery->SPRITE_SetState(puppet);
    gallery->colour = colour;
    SetScale(gallery, 1.0f, 1.0f);
    gallery->SPRITE_Draw2D(Ftol(hud->target.x - width * 0.5), Ftol(hud->target.y - height * 0.5), puppet, NULL);
}

// The aim's crosshairs: zoomed while aiming
void DrawAim(GHud *hud, uint32_t normColour, uint32_t zoomColour) {
    if (hud->aimOn)
        DrawAtTarget(hud, hud->crossZoomPuppet, zoomColour);
    else
        DrawAtTarget(hud, hud->crossNormPuppet, normColour);
}

// A target's place in the HUD's frame (x stays on a wide screen)
void TargetPosition(const GHud *hud, const WTargetable *target, float *x, float *y) {
    *x = target->screenPos.x;
    if (!fgRenderer->widescreen)
        *x = (float)(((double)*x - hud->targetOffsetX) / hud->targetScaleX);
    *y = (float)(((double)target->screenPos.y - hud->targetOffsetY) / hud->targetScaleY);
}

// A target's frame; the selected one locked zooms in, its colour the zoom's
void DrawTargetFrame(GHud *hud, WTargetable *target, WTargetable *selected, float x, float y, MATRIX4 *transform) {
    uint32_t colour;
    if (target == selected && hud->targetLockState != kLockNone)
        colour = hud->DrawZoomingCrossHairs(hud->targetLockPuppet);
    else
        colour = HudCrossColour;
    BuildTranslate(transform, x, y, 0.0f);
    DrawCentredAt(hud, hud->crossTorpRoamPuppet, transform, colour);
}

// The sniper scope: four lines from the aim to the screen's edges, the circle, ten ticks turning round it dimly and
// as many brightly as the zoom's tenths (the last in part), and the scope's centre at twice its size; the sniper
// zoom faded in with the hands' aim. In the super easy mode the scope stays at the screen's centre with the
// auto-aim text, its dot blinking.
void DrawSniperScope(GHud *hud, MATRIX4 *rotate, MATRIX4 *translate, MATRIX4 *transform) {
    GGallery *gallery = hud->gallery;
    if (hud->superEasy) {
        if (HudAutoAimFrame < 15) {
            gallery->SPRITE_SetState(hud->autoAimDotPuppet);
            gallery->SPRITE_Draw2D(hud->autoAimDotPuppet);
        }
        gallery->FONT_SetState(hud->autoAimTextPuppet);
        gallery->FONT_Draw(&hud->autoAimTextPuppet->text, NULL);
        int next = HudAutoAimFrame + 1;
        HudAutoAimFrame = next > 30 ? 0 : next;
    }
    uint32_t baseColour = HudCrossColour504;
    float x, y;
    if (hud->superEasy) {
        x = (float)(((double)hud->centerX - hud->targetOffsetX) / hud->targetScaleX);
        y = (float)(((double)hud->centerY - hud->targetOffsetY) / hud->targetScaleY);
    } else {
        x = hud->target.x;
        y = hud->target.y;
    }
    float aim = AICharacterHands::GetAimFraction();
    uint32_t colour = ScaleAlpha(baseColour, aim);
    int zoomAlpha = RoundToInt(aim * 255.0f);
    SniperZoom->active = 1;
    SniperZoom->alpha = zoomAlpha;
    float levelHeight = hud->zoomLevelPuppet->sprite.height;
    float levelWidth = hud->zoomLevelPuppet->sprite.width;

    gallery->QUAD_Draw2D(x, -hud->targetOffsetY, kScopeLine, (float)((double)y - kScopeArm + hud->targetOffsetY), colour);
    double leftWidth = (double)x - kScopeArm;
    float left = 0.0f;
    if (fgRenderer->widescreen) {
        leftWidth += kWideShift;
        left = -kWideShift;
    }
    gallery->QUAD_Draw2D(left - hud->targetOffsetX, y, (float)(leftWidth + hud->targetOffsetX), kScopeLine, colour);
    float below = y + kScopeArm;
    gallery->QUAD_Draw2D(x, below, kScopeLine, below + hud->targetOffsetY, colour);
    float right = x + kScopeArm;
    double rightWidth = (double)hud->screenWidth - right;
    if (fgRenderer->widescreen)
        rightWidth += kWideShift;
    gallery->QUAD_Draw2D(right, y, (float)(rightWidth + hud->targetOffsetX), kScopeLine, colour);

    gallery->SPRITE_SetState(hud->zoomCirclePuppet);
    gallery->colour = colour;
    SetScale(gallery, 1.0f, 1.0f);
    gallery->SPRITE_Draw2D(Ftol(x), Ftol(y), hud->zoomCirclePuppet, NULL);

    BuildTranslate(translate, x, y, 0.0f);
    BuildRotate(rotate, -45.0f, 0.0f, 0.0f, 1.0f);
    VU0_MATRIX4_mult(transform, rotate, translate);
    Draw::SetModelMatrix(transform);
    MATRIX4 ticks = *transform;
    float alpha = (float)(colour >> 24);
    uint32_t rgb = colour & 0xffffff;
    float dim = alpha * kScopeDim;
    int tickY = Ftol(-(double)levelHeight);
    // The ticks' spacing is multiplied by zero: every tick is at 130 and turns 8 degrees
    for (int i = 0; i < 10; i++) {
        gallery->SPRITE_SetState(hud->zoomLevelPuppet);
        gallery->colour = uint32_t(RoundToInt(dim)) << 24 | rgb;
        double offset = (double)i - 5.0;
        if (offset < 0.0)
            offset = -offset;
        float spacing = (float)((1.0 - offset * kTickSpacing) * 0.0 + 1.0);
        gallery->SPRITE_Draw2D(Ftol((double)spacing * kTickRadius - levelWidth), tickY, hud->zoomLevelPuppet, NULL);
        BuildRotate(rotate, (float)(kTickTurn / spacing), 0.0f, 0.0f, 1.0f);
        VU0_MATRIX4_mult(&ticks, rotate, &ticks);
        Draw::SetModelMatrix(&ticks);
    }
    Draw::SetModelMatrix(transform);
    float bright = alpha * kScopeBright;
    for (int i = 0; i < Ftol((double)hud->zoomPercent * 10.0); i++) {
        gallery->SPRITE_SetState(hud->zoomLevelPuppet);
        gallery->colour = uint32_t(RoundToInt(bright)) << 24 | rgb;
        double offset = (double)i - 5.0;
        if (offset < 0.0)
            offset = -offset;
        float spacing = (float)((1.0 - offset * kTickSpacing) * 0.0 + 1.0);
        gallery->SPRITE_Draw2D(Ftol((double)spacing * kTickRadius - levelWidth), tickY, hud->zoomLevelPuppet, NULL);
        BuildRotate(rotate, (float)(kTickTurn / spacing), 0.0f, 0.0f, 1.0f);
        VU0_MATRIX4_mult(transform, rotate, transform);
        Draw::SetModelMatrix(transform);
    }
    double tenths = (double)hud->zoomPercent * 10.0;
    float part = (float)((tenths - Ftol(tenths)) * alpha * kScopeBright);
    int partAlpha = RoundToInt(part);
    gallery->SPRITE_SetState(hud->zoomLevelPuppet);
    gallery->colour = uint32_t(partAlpha) << 24 | rgb;
    double offset = (double)hud->zoomPercent * 10.0 - 5.0;
    if (offset < 0.0)
        offset = -offset;
    gallery->SPRITE_Draw2D(Ftol(((1.0 - offset * kTickSpacing) * 0.0 + 1.0) * kTickRadius - levelWidth), tickY,
                          hud->zoomLevelPuppet, NULL);
    Draw::SetModelMatrix(NULL);

    GalleryKey *centre = hud->crossTorpNormPuppet;
    float width = centre->sprite.width + centre->sprite.width;
    float height = centre->sprite.height + centre->sprite.height;
    gallery->SPRITE_SetState(centre);
    gallery->colour = colour;
    SetScale(gallery, 2.0f, 2.0f);
    gallery->SPRITE_Draw2D(Ftol((double)x - width * 0.5), Ftol((double)y - height * 0.5), hud->crossTorpNormPuppet, NULL);
}

// The torpedo's target zone: its corner drawn four times, mirrored, round the screen's centre
void DrawTargetZone(GHud *hud) {
    GGallery *gallery = hud->gallery;
    GalleryKey *zone = hud->targetZonePuppet;
    uint32_t colour = HudCrossColour4e8;
    float halfWidth = (float)(((double)kZoneWidth + zone->sprite.width) * hud->targetScaleX);
    float halfHeight = (float)(((double)kZoneHeight + zone->sprite.height) * hud->targetScaleY);
    float x = (float)(((double)hud->centerX - hud->targetOffsetX) / hud->targetScaleX);
    float y = (float)(((double)hud->centerY - hud->targetOffsetY) / hud->targetScaleY);
    gallery->SPRITE_SetState(zone);
    gallery->colour = colour;
    SetScale(gallery, 1.0f, 1.0f);
    int top = Ftol((double)y - halfHeight);
    int left = Ftol((double)x - halfWidth);
    gallery->SPRITE_Draw2D(left, top, hud->targetZonePuppet, NULL);
    gallery->SPRITE_SetState(hud->targetZonePuppet);
    gallery->colour = colour;
    SetScale(gallery, -1.0f, 1.0f);
    int right = Ftol((double)x + halfWidth);
    gallery->SPRITE_Draw2D(right, top, hud->targetZonePuppet, NULL);
    gallery->SPRITE_SetState(hud->targetZonePuppet);
    gallery->colour = colour;
    SetScale(gallery, -1.0f, -1.0f);
    int bottom = Ftol((double)y + halfHeight);
    gallery->SPRITE_Draw2D(right, bottom, hud->targetZonePuppet, NULL);
    gallery->SPRITE_SetState(hud->targetZonePuppet);
    gallery->colour = colour;
    SetScale(gallery, 1.0f, -1.0f);
    gallery->SPRITE_Draw2D(left, bottom, hud->targetZonePuppet, NULL);
}

} // namespace

// By the selected weapon: the missile's frames round its targets (two, or four with the double shot, on
// snow2a_mis4; none in the super easy mode on paris_mis01), the torpedoes' and the ultralight rocket's with arrows
// at the screen's edge to those off it, the remote rocket's recharge bar. Then the aim: the gun's lock on
// junglea_mis13a, the torpedo's target zone, or in auto-drive the weapon's crosshairs (the sniper's scope), the
// breath held near full zoom in the super easy mode. Unless locked, the lock's frame fades out.
// (Jump table 0x000e1950, decoded from its bytes: weapons 24 and 25 together, 26, 27 as the default, 28 and 30
// together, 29.)
// FUNC_AT(0x000e0440)
void GHud::DrawCrossHairs() {
    MATRIX4 rotate = kIdentity;
    MATRIX4 translate = kIdentity;
    MATRIX4 transform = kIdentity;
    bool paris = strcmp(Launch.missionName, "paris_mis01") == 0;
    bool jungle = strcmp(Launch.missionName, "junglea_mis13a") == 0;
    bool snow = strcmp(Launch.missionName, "snow2a_mis4") == 0;
    WeaponRecord &weapon = WeaponManager->weapons[WeaponManager->secondary];

    switch (weapon.id) {
    case kWeaponMissile:
        if (!(paris && superEasy)) {
            int count = TargetPicker.sortedCount;
            if (snow) {
                int limit = WeaponManager->doubleShot ? 4 : 2;
                if (limit < count)
                    count = limit;
            }
            WTargetable *selected = TargetPicker.GetSelectedTarget();
            for (int i = 0; i < count; i++) {
                WTargetable *target = TargetPicker.sorted[i];
                if (target->ownerType == kTargetPhysicsObject && target->physics != NULL &&
                    target->physics->attributes.LookupBool("ONLY_TARGET_WITH_EMP", NULL))
                    continue;
                target = TargetPicker.sorted[i];
                float x, y;
                TargetPosition(this, target, &x, &y);
                if (target->onScreen)
                    DrawTargetFrame(this, target, selected, x, y, &transform);
            }
        }
        break;
    case kWeaponTorpedo:
    case kWeaponUltralightRocket: {
        int count = TargetPicker.sortedCount;
        WTargetable *selected = TargetPicker.GetSelectedTarget();
        for (int i = 0; i < count; i++) {
            WTargetable *target = TargetPicker.sorted[i];
            float x, y;
            TargetPosition(this, target, &x, &y);
            if (target->onScreen) {
                DrawTargetFrame(this, target, selected, x, y, &transform);
            } else {
                // An arrow at the target's place on the edge, turned away from the centre
                double dx = (double)x - centerX;
                float angle = (float)(X87Atan2((double)y - centerY, dx) * kDegreesPerRadian + 90.0f);
                if (fgRenderer->widescreen)
                    x = (float)(dx * kWideTextScale + centerX);
                BuildTranslate(&translate, x, y, 0.0f);
                BuildRotate(&rotate, angle, 0.0f, 0.0f, 1.0f);
                VU0_MATRIX4_mult(&transform, &rotate, &translate);
                DrawCentredAt(this, crossTorpOffscreenPuppet, &transform, HudCrossColour);
            }
        }
        break;
    }
    }

    if (weapon.id == kWeaponFodRocket) {
        double charge = (double)(fgSimulation.stepCount - weapon.fireSteps[0]) / weapon.refireSteps;
        float recharge = charge < 1.0 ? (float)charge : 1.0f;
        SetPlayerRecharge(recharge);
        GalleryKey *bar = rechargeBarPuppet;
        float barHeight = bar->sprite.height;
        int segments = Ftol((double)kRechargeHeight / barHeight * recharge);
        gallery->QUAD_Draw2D(bar->position.x, (float)((double)bar->position.y - kRechargeHeight + barHeight), bar->sprite.width,
                            kRechargeHeight, HudCrossColour560);
        gallery->SPRITE_SetState(rechargeBarPuppet);
        gallery->colour = kWhite;
        for (unsigned i = 0; i < (unsigned)segments; i++) {
            bar = rechargeBarPuppet;
            gallery->SPRITE_Draw2D(Ftol(bar->position.x), Ftol((double)bar->position.y - (double)i * barHeight), bar, NULL);
        }
    }

    // The launch page's +0x99c: no crosshairs but the aim's (and the gun's lock on junglea_mis13a)
    if (unknown432 && !aimOn && !jungle)
        return;
    RPlayerCamera *camera = fgRenderHigh->views[0].camera;
    if (!autoDrive) {
        if (weapon.id == kWeaponGun) {
            if (jungle && targetLockState != kLockNone) {
                DrawZoomingCrossHairs(targetLockPuppet);
                TargetPicker.GetSelectedTarget();   // (not used)
            }
        } else if (weapon.id == kWeaponTorpedo) {
            DrawTargetZone(this);
            if (targetLockState == kLockNone) {
                BuildTranslate(&translate, target.x, target.y, 0.0f);
                DrawCentredAt(this, crossTorpNormPuppet, &translate, HudCrossColour4e8);
            }
        }
    } else {
        double change = zoomPercent - camera->GetZoomPercent();
        if (change < 0.0)
            change = -change;
        if (change > kZoomChange) {
            zoomPercent = (float)camera->GetZoomPercent();
            PlayerCarAudio()->aiming = 1;
        } else {
            PlayerCarAudio()->aiming = 0;
        }
        if (superEasy)
            PlayerCarAudio()->zoom->InTheZone(zoomPercent > kNearlyZoomed);
        switch (weapon.id) {
        case kWeaponSnowmobileGun:
        case kWeaponSnowmobileRocket:
            DrawAim(this, HudCrossColour500, HudCrossColour500);
            break;
        case kWeaponSniperRifle:
            if (aimOn)
                DrawSniperScope(this, &rotate, &translate, &transform);
            else if (!superEasy)
                DrawAtTarget(this, crossNormPuppet, HudCrossColour508);
            break;
        case kWeaponUltralightRocket:
        case kWeaponFodRocket:
            if (targetLockState != kLockNone)
                return;
            DrawAim(this, HudCrossColour4f4, HudCrossColour4f4);
            break;
        case kWeaponFodGun:
            DrawAim(this, HudCrossColour50c, HudCrossColour510);
            SetPlayerRecharge(1.0f);
            break;
        default:
            DrawAim(this, HudCrossColour50c, HudCrossColour510);
            break;
        }
    }
    if (targetLockState != kLockNone)
        return;
    DrawFadingCrossHairs(targetLockPuppet);
    zoomCrossHairsOn = false;
}
