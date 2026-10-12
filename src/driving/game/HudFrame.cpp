#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// GHud's frame: Render, Draw, UpdateTargets. See HudFrame.h.

#pragma fp_contract(off)

#include "HudFrame.h"
#include "HudMenus.h"                 // HudResultState

#include "DebugAudio.h"                   // DAudio
#include "Gallery.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "Subtitles.h"
#include "Vehicle.h"                      // PVehicle
#include "WeaponCameras.h"                // SniperZoom
#include "../audio/Mix.h"
#include "../audio/Sound.h"               // AMenuSound
#include "../audio/Stream.h"
#include "../camera/Camera.h"             // RViewCamera
#include "../camera/CameraIniLoader.h"    // fgCameraModeIndices
#include "../camera/PlayerCamera.h"       // RPlayerCamera
#include "../eagl/RenderContext.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/GameLoop.h"           // LaunchPage
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsNamespace.h" // PhysicsData
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"         // vec3distance
#include "../render/Draw.h"
#include "../render/RenderHigh.h"         // fgRenderHigh
#include "../render/Renderer.h"           // fgRenderer
#include "../render/RSceneObj.hpp"
#include "../world/World.h"               // fgWorld

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// ---- the game's state

// XBE_GLOBAL(0x001b7055, 0x1)
bool gHudOn = true;

namespace {

constexpr float kWeaponShowTime = 3.0f;
constexpr float kWeaponFadeTime = 0.5f;
constexpr int32_t kOutroSteps = 600;                // steps into a failed mission before the outro may end it
constexpr float kTargetRange = 120.0f;              // the instances the HUD points at: nearer than this...
constexpr float kTargetMinRange = 10.0f;            // ... and further than this
constexpr float kObjectiveMinRange = 2.0f;
constexpr int kMissionEndRestart = 2;               // the launch page's unknown978 for a relaunch

const char kMenuSounds[] = "Menu Sounds";

void PlayMenuSound(const char *patch) {
    AMenuSound::Trigger(8, patch, kMenuSounds, 2);
}

} // namespace

// FUNC_AT(0x000e00d0)
GHud* GHud::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x000e00f0)
void GHud::SetControllerUnplugged(bool unplugged) {
    if (controllerUnplugged == unplugged)
        return;
    if (unplugged) {
        pauseError = 1;
        if (!paused)
            PauseOn();
    } else {
        pauseError = 0;
    }
    controllerUnplugged = unplugged;
}

// The frame's time; the mission's end once the result has faded and the music and speech are done; the HUD's
// actions (the weapon selection shows the weapon, start pauses or skips the cinematic, the rest go to the pause
// menu and the cheats while paused); then the HUD's view.
// FUNC_AT(0x000e0140)
int GHud::Render() {
    steps = fgSimulation.stepCount - lastStep;
    lastStep = fgSimulation.stepCount;
    elapsed = (float)((double)(uint32_t)steps * fgSimulation.timeStep);
    if (Launch.unknown5d8 > 0 && (uint32_t)lastStep > kOutroSteps)
        exitRequested = true;

    if (missionResultState == kResultFading && missionResultDelay <= 0.0f) {
        AStream *music = AStream::Get("Music");
        AStream *speech = AStream::Get("Speech");
        music->Stop();
        if (music->IsNotFound() || music->IsOver() || music->IsPaused() || music->IsLoop()) {
            if (speech->IsNotFound() || speech->IsOver() || speech->mix != AMix::Get("Speech")) {
                Launch.lives = Launch.lives > 0 ? Launch.lives - 1 : 0;
                exitRequested = true;
            }
        }
    }

    while (!fHudActionQueue->IsEmpty()) {
        ActionRef ref;
        fHudActionQueue->GetAction(&ref);
        ActionData *action = ref.data;
        if (action != NULL && action->action >= kActionToggleSecondary && action->action <= kActionToggleSecondaryDown) {
            if (missionResultState == kResultNone && !paused) {
                weaponShowTime = kWeaponShowTime;
                unknown2E0 = 0.0f;
                unknown2E4 = 0.0f;
                weaponFadeTime = kWeaponFadeTime;
            }
        } else if (action != NULL && action->action == kFrontEndStart) {
            if (missionResultText != kTextMissionFailed) {
                if (!paused) {
                    if (glbMissionManager->cinematicMode != 0) {
                        glbMissionManager->SkipCinematic();
                    } else {
                        Launch.unknown5d8 = 0;
                        PauseOn();
                    }
                } else {
                    PlayMenuSound("SFX_Exit");
                    PauseOff();
                }
            }
            // The failed mission's menu answers start as accept
            if (paused && pausePage == kPauseFailed) {
                if (pauseSelection == 1) {
                    glbMissionManager->RestoreLives();
                    restartRequested = true;
                } else if (pauseSelection == 2) {
                    PlayMenuSound("SFX_Exit");
                    if (Launch.unknown978 != 1 && Launch.unknown978 != 4)
                        Launch.unknown978 = kMissionEndRestart;
                    exitRequested = true;
                }
            }
        } else if (paused) {
            PauseMenuMsgHandler(action != NULL ? action->action : 0);
            CheatMsgHandler(action != NULL ? action->action : 0);
        } else if (action != NULL && action->action == kFrontEndSkipMovie) {
            glbMissionManager->SkipCinematic();
        }
        fHudActionQueue->PopAction();
    }
    fHudView->Render();
    return 0;
}

// Without the HUD on, only the aiming crosshairs. Otherwise the guided torpedo's overlay in the missile camera,
// the mission result, or the gauges (all but the objective message and stylin hidden by the letterbox); the
// subtitles unless paused; the pause menu or the controller error while paused; the debug audio targets.
// FUNC_AT(0x000e1970)
void GHud::Draw() {
    fgRenderer->renderContext->SetZWritesEnable(1);
    Draw::SetZDepthNear();
    if (!gHudOn) {
        if (aimOn) {
            DrawCrossHairs();
            gallery->SPRITE_Flush();
        }
        return;
    }
    if (fgRenderHigh->views[0].camera->cameraMode == fgCameraModeIndices.missile) {
        DrawFlyByWire();
        gallery->SPRITE_Flush();
    } else if (missionResultState != kResultNone) {
        DrawMissionResult();
        gallery->SPRITE_Flush();
    } else {
        DrawObjectiveMessage();
        DrawStylin();
        if (letterBoxMode == kLetterBoxOff) {
            DrawWeapon();
            DrawCrossHairs();
            DrawHealth();
            DrawMessage();
            DrawButtonMessage();
            DrawGadget();
            DrawTimer();
            gallery->SPRITE_Flush();
        }
    }
    if (!paused)
        GSubtitles::TheApp()->DrawSubtitles();
    gallery->SPRITE_Flush();
    if (paused) {
        if (pauseError == 1)
            DrawPauseErrors();
        else
            DrawPauseMenu();
        gallery->SPRITE_Flush();
    }
    if (gDAudio)
        DAudio::DrawTargets();
}

// The nearest tracked instance on screen between 10 and 120 from the player (out of auto-drive), measured to the
// top of its box, and the current objective, raised to its arrow's height, when it is visible, further than 2 and
// not level with the player.
// FUNC_AT(0x000e1a90)
void GHud::UpdateTargets() {
    float nearest = kTargetRange;
    instanceTargetShown = false;
    objectiveTargetShown = false;
    const Coord3 *player = &fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->position;
    bool onScreen;
    ScreenPos screen;

    if (!glbMissionManager->autoDrive) {
        SimVector<TrackedInstance> *tracked = fgSimulation.GetTrackedInstanceList();
        for (TrackedInstance **it = tracked->first; it != tracked->last; it++) {
            TrackedInstance *entry = *it;
            if (entry->unknown00 != 0 || !entry->visible)
                continue;
            RSceneObj *object = fgWorld->GetSceneObjFromInstance(entry->instance);
            Coord4 dimensions;
            object->GetBoundingDimensions(&dimensions);
            Coord3 top;
            object->GetPosition(&top);
            top.y = top.y + dimensions.y;
            float distance = vec3distance(player, &top);
            if (distance < nearest && distance > kTargetMinRange) {
                GetScreenCoords(&top, &onScreen, &screen);
                if (onScreen) {
                    instanceTarget = screen;
                    instanceTargetShown = true;
                    const PhysicsData *data =
                        reinterpret_cast<const PhysicsData *>(uintptr_t(object->Desc()->physicsAttributes.value));
                    instanceTargetDescription = data != NULL ? data->description : 0.0f;
                    nearest = distance;
                }
            }
        }
    }

    const Coord4 *objective = glbMissionManager->GetCurrObjective(0);
    if (objective != NULL && glbMissionManager->IsObjectiveVisibleToUser(0)) {
        Coord3 arrow = {objective->x, objective->y, objective->z};
        arrow.y = (float)(glbMissionManager->GetObjectiveArrowHeight(0) + arrow.y);
        if (vec3distance(player, &arrow) > kObjectiveMinRange && fabs((double)player->y - arrow.y) != 0.0) {
            GetScreenCoords(&arrow, &onScreen, &screen);
            if (onScreen) {
                objectiveTarget = screen;
                objectiveTargetShown = true;
                objectiveTargetComment = glbMissionManager->GetObjectiveComment(0);
            }
        }
    }
    if (gDAudio)
        DAudio::UpdateTargets();
}
