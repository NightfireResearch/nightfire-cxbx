#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// GHud's menus and boxed messages: the pause menu, the mission result, the button and objective messages. See
// HudMenus.h.

#pragma fp_contract(off)

#include "HudMenus.h"

#include "FrontEndSystem.h"
#include "Gallery.h"
#include "Girl.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "WeaponCameras.h"                // SniperZoom
#include "../audio/Mix.h"
#include "../audio/Sound.h"               // AMenuSound
#include "../audio/Stream.h"
#include "../data/AttributeSet.h"
#include "../engine/GameLoop.h"           // LaunchPage
#include "../engine/InputConfig.h"
#include "../engine/UMemory.hpp"          // OperatorNewArray, OperatorDelete
#include "../platform/X87.h"
#include "../render/Renderer.h"           // fgRenderer
#include "../world/World.h"               // fgWorld

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace {

constexpr float kButtonScale = 1.4f;
static_assert(std::bit_cast<uint32_t>(kButtonScale) == 0x3fb33333, "1.4");
constexpr float kButtonY = 100.0f;
constexpr float kButtonFade = 0.5f;
constexpr float kWideTextScale = 1.33333337f;       // text widths on a wide screen
static_assert(std::bit_cast<uint32_t>(kWideTextScale) == 0x3faaaaab, "4/3");
constexpr float kResultScale = 1.3f;
static_assert(std::bit_cast<uint32_t>(kResultScale) == 0x3fa66666, "1.3");
constexpr float kResultFadeIn = 0.5f;
constexpr float kResultHold = 3.0f;
constexpr float kResultFade = 1.5f;
constexpr float kResultPauseAt = 0.2f;              // a failed mission's menu opens below this much of the fade
static_assert(std::bit_cast<uint32_t>(kResultPauseAt) == 0x3e4ccccd, "0.2");
constexpr float kObjectiveScale = 1.1f;
static_assert(std::bit_cast<uint32_t>(kObjectiveScale) == 0x3f8ccccd, "1.1");
constexpr float kObjectivePulse = 0.5f;
constexpr int32_t kObjectivePulses = 2;
constexpr float kObjectiveWaitTime = 2.0f;          // before a waiting message replaces the one shown
constexpr float kObjectiveDoneTime = 1.0f;          // a completed or failed objective's message, once titled
constexpr float kObjectiveMsgTime = 5.0f;
constexpr float kObjectiveBoxPad = 10.0f;
constexpr float kObjectiveBoxGap = 5.0f;
constexpr float kTitleScale = 1.2f;
static_assert(std::bit_cast<uint32_t>(kTitleScale) == 0x3f99999a, "1.2");
constexpr float kSmallScale = 0.9f;
static_assert(std::bit_cast<uint32_t>(kSmallScale) == 0x3f666666, "0.9");
constexpr float kInvertScale = 1.1f;
constexpr float kArrowOffset = 30.0f;
constexpr float kLineHeight = 25.0f;
constexpr float kScoreLine = 18.0f;
constexpr float kControlsLine = 21.0f;
constexpr float kOneThird = 0.333333343f;
static_assert(std::bit_cast<uint32_t>(kOneThird) == 0x3eaaaaab, "a third");
constexpr uint32_t kWhite = 0xfefefefe;

// The objective message's titles (locale symbols), by its kind
constexpr int32_t kTextNewObjective = 0x1869e;
constexpr int32_t kTextObjectiveComplete = 0x1869f;
constexpr int32_t kTextObjectiveKind3 = 0x18598;

// The pause menu's texts (locale symbols)
constexpr int32_t kTextSuperEasyTitle = 0x134;
constexpr int32_t kTextSuperEasyQuit = 0x2201;
constexpr int32_t kTextRestart = 0x2268;
constexpr int32_t kTextSuperEasyRestart = 0x2277;
constexpr int32_t kTextInverted = 0x14d;
constexpr int32_t kTextNotInverted = 0x1774;
constexpr int32_t kTextScoreTitle = 0x220e;
constexpr int32_t kTextScoreColumn1 = 0x220f;
constexpr int32_t kTextScoreColumn2 = 0x2210;
constexpr int32_t kTextScoreRows = 0x6b5;           // a row's label, from row -1
constexpr int kControlsHeaderSlot = 13;             // InputConfigManager::GetLocaleID's slots
constexpr int kControlsDetailsSlot = 12;

const char kMenuSounds[] = "Menu Sounds";

void PlayMenuSound(const char *patch) {
    AMenuSound::Trigger(8, patch, kMenuSounds, 2);
}

// A colour with its alpha scaled
uint32_t ScaleAlpha(uint32_t colour, float scale) {
    return (colour & 0xffffff) | uint32_t(RoundToInt(float(colour >> 24) * scale)) << 24;
}

// White, nearly opaque by `fraction`
uint32_t WhiteAlpha(float fraction) {
    return uint32_t(RoundToInt(fraction * 254.0f)) << 24 | 0xfefefe;
}

void DeleteLines(char **lines, int count) {
    for (int i = 0; i < count; i++)
        OperatorDelete(lines[i]);
    OperatorDelete(lines);
}

// The pause menu's button hints, as its pages ask for them
struct PauseHints {
    bool objective;                     // the objectives' hint page
    bool select;
    bool invert;
    bool back;
    bool cont;
    bool changePage;
    bool scroll;
};

// The unselected and selected colours of the pause menu's items
struct PauseColours {
    uint32_t cont;
    uint32_t restart;
    uint32_t quit;
    uint32_t yes;
    uint32_t no;
};

// ---- the result box

// A piece of the box between two keys: the quad interpolated, drawn at the first key's place
void DrawBoxBetween(GGallery *gallery, const GalleryKey *from, const GalleryKey *to, float t) {
    GalleryQuad quad;
    gallery->QUAD_Interpolate(&from->quad, &to->quad, &quad, t);
    gallery->QUAD_SetState(from);
    gallery->QUAD_Draw(&quad, 1.0f);
}

// A piece of the box at the key interpolated between two, in the first key's quad
void DrawBoxAtKey(GGallery *gallery, const GalleryKey *from, const GalleryKey *to, float t) {
    GalleryKey key;
    gallery->Get_InterpolatedKey(&key, from, to, t);
    gallery->QUAD_SetState(&key);
    gallery->QUAD_Draw(from, 1.0f);
}

void DrawBox(GHud *hud) {
    GGallery *gallery = hud->gallery;
    gallery->QUAD_SetState(hud->statusBackKeys[1]);
    gallery->QUAD_Draw(hud->statusBackKeys[1], 1.0f);
    gallery->QUAD_SetState(hud->statusBackTopKeys[1]);
    gallery->QUAD_Draw(hud->statusBackTopKeys[1], 1.0f);
    gallery->QUAD_SetState(hud->statusBackBotKeys[1]);
    gallery->QUAD_Draw(hud->statusBackBotKeys[1], 1.0f);
}

void DrawResultText(GHud *hud, uint32_t shadowColour, uint32_t colour) {
    const char *text = GSystem::LOCALE_Symbol(hud->missionResultText);
    float x = (float)hud->GetCenter(hud->statusPuppet, kResultScale, text);
    hud->DrawFontAndShadow(hud->statusPuppet, x, hud->statusPuppet->position.y, shadowColour, colour, kResultScale, text);
}

// ---- the pause menu's pages

// The arrow left of an item
void DrawArrowAt(GHud *hud, double x, float y) {
    hud->DrawBlinkingArrow((float)(x - kArrowOffset), y + 1.0f);
}

// An item's own text centred
void DrawCentred(GHud *hud, GalleryKey *puppet, float y, uint32_t colour) {
    hud->DrawFont(puppet, (float)hud->GetCenter(puppet, 1.0f, NULL), y, colour, 1.0f, NULL);
}

// The page's title, the super easy mode's or the failed mission's, at the mission title's height
void DrawPageTitle(GHud *hud, float missionY, int symbol) {
    const char *title = GSystem::LOCALE_Symbol(symbol);
    float y = (float)((double)missionY - 4.0 + 4.0);
    float x = (float)hud->GetCenter(hud->scratchTxtPuppet, kTitleScale, GSystem::LOCALE_Symbol(symbol));
    hud->DrawFont(hud->scratchTxtPuppet, x, y, HudTextColour, kTitleScale, title);
}

// Continue, restart and quit, or the question restart and quit ask
void DrawMissionPage(GHud *hud, PauseHints &hints, PauseColours &colours, float missionY) {
    char quit[64];
    hints.select = true;
    hints.scroll = true;
    const char *quitText;
    if (hud->superEasy) {
        DrawPageTitle(hud, missionY, kTextSuperEasyTitle);
        quitText = GSystem::LOCALE_Symbol(kTextSuperEasyQuit);
    } else {
        quitText = GSystem::LOCALE_Symbol(hud->quitMisPuppet->text.text);
    }
    sprintf(quit, "%s", quitText);

    double arrowX = hud->centerX;
    float arrowY = hud->centerY;
    if (hud->pauseConfirm != 0) {
        float yesY = hud->yesPuppet->position.y;
        float noY = hud->noPuppet->position.y;
        if (!hud->superEasy || hud->missionResultText != kTextMissionFailed)
            hints.cont = true;
        if (hud->pauseSelection == 1 || hud->pauseSelection == 2) {
            GalleryKey *question = hud->pauseSelection == 1 ? hud->restartConfirmPuppet : hud->quitConfirmPuppet;
            float x = (float)hud->GetCenter(question, kTitleScale, NULL);
            float y = question->position.y;
            hud->gallery->SPRITE_SetState(question);
            hud->DrawFont(question, x, y, HudTextColour, kTitleScale, NULL);
        }
        if (hud->pauseConfirm == 1) {
            colours.yes = HudTextColour;
            hints.back = true;
            arrowX = hud->GetCenter(hud->yesPuppet, 1.0f, NULL);
            arrowY = yesY;
        } else if (hud->pauseConfirm == 2) {
            hints.back = true;
            colours.no = HudTextColour;
            arrowX = hud->GetCenter(hud->noPuppet, 1.0f, NULL);
            arrowY = noY;
        }
        DrawArrowAt(hud, arrowX, arrowY);
        hud->gallery->SPRITE_SetState(hud->yesPuppet);
        DrawCentred(hud, hud->yesPuppet, yesY, colours.yes);
        hud->gallery->SPRITE_SetState(hud->noPuppet);
        DrawCentred(hud, hud->noPuppet, noY, colours.no);
        return;
    }

    float continueY = hud->continueMisPuppet->position.y;
    float restartY = hud->restartMisPuppet->position.y;
    float quitY = hud->quitMisPuppet->position.y;
    hints.changePage = true;
    switch (hud->pauseSelection) {
    case 0:
        colours.cont = HudTextColour;
        arrowX = hud->GetCenter(hud->continueMisPuppet, 1.0f, NULL);
        arrowY = continueY;
        break;
    case 1:
        colours.restart = HudTextColour;
        arrowX = hud->GetCenter(hud->restartMisPuppet, 1.0f, NULL);
        arrowY = restartY;
        break;
    case 2:
        colours.quit = HudTextColour;
        arrowX = hud->GetCenter(hud->quitMisPuppet, 1.0f, quit);
        arrowY = quitY;
        break;
    }
    DrawArrowAt(hud, arrowX, arrowY);
    if (!hud->superEasy || hud->missionResultText != kTextMissionFailed) {
        hints.cont = true;
        DrawCentred(hud, hud->continueMisPuppet, continueY, colours.cont);
    }
    DrawCentred(hud, hud->restartMisPuppet, restartY, colours.restart);
    hud->DrawFont(hud->quitMisPuppet, (float)hud->GetCenter(hud->quitMisPuppet, 1.0f, quit), quitY, colours.quit,
                  1.0f, quit);
}

// The objectives, each wrapped under its tick or bullet, and the arrow at the one chosen. If that one is gone, the
// line counts are not freed.
void DrawObjectiveList(GHud *hud, PauseHints &hints, float objectivesY, float y) {
    GGallery *gallery = hud->gallery;
    unsigned count = glbMissionManager->objectives.size;
    hints.scroll = true;
    hints.objective = true;
    hints.changePage = true;
    int *lineCounts = NULL;
    if (count != 0)
        lineCounts = static_cast<int *>(OperatorNewArray(count * sizeof(int)));
    for (unsigned i = 0; i < count; i++) {
        MissionObjective *objective = glbMissionManager->GetObjectiveByIndex(i);
        if (objective == NULL)
            continue;
        GalleryKey *icon;
        float iconX;
        float iconY;
        uint32_t colour;
        if (objective->state == kObjectivePassed) {
            icon = hud->objCheckPuppet;
            iconY = y + 4.0f;
            iconX = 100.0f;
            colour = HudColour4d8;
        } else {
            iconY = y + 9.0f;
            icon = hud->objBulletPuppet;
            if (objective->state == kObjectiveCurrent) {
                iconX = 100.0f;
                colour = HudTextColour;
            } else {
                iconX = 103.0f;
                colour = HudColour4d8;
            }
        }
        gallery->SPRITE_SetState(icon);
        gallery->colour = colour;
        gallery->SPRITE_Draw2D(Ftol(iconX), Ftol(iconY), icon, NULL);
        char **lines;
        int lineCount = WordWrap(GSystem::LOCALE_Symbol(objective->id), 60, &lines);
        lineCounts[i] = lineCount;
        hud->DrawFont(hud->scratchTxtPuppet, 120.0f, y, colour, kSmallScale, lines, lineCount);
        DeleteLines(lines, lineCount);
        y = (float)((double)lineCount * kLineHeight + y);
    }
    if (hud->pauseObjective != 0) {
        if (glbMissionManager->GetObjectiveByIndex(hud->pauseObjective - 1) == NULL)
            return;
        int lines = 0;
        for (unsigned i = 0; i < (unsigned)(hud->pauseObjective - 1); i++)
            lines += lineCounts[i];
        hud->DrawBlinkingArrow(70.0f, (float)((double)lines * kLineHeight + objectivesY + 40.0f));
    }
    OperatorDelete(lineCounts);
}

// The chosen objective's hint in a box, and how to leave it: the text around its button marker, the button between
void DrawObjectiveHint(GHud *hud) {
    GGallery *gallery = hud->gallery;
    gallery->QUAD_Draw2D(70.0f, 135.0f, 505.0f, 175.0f, HudColour54c);
    hud->DrawFont(hud->hintTitlePuppet, (float)hud->GetCenter(hud->hintTitlePuppet, 1.0f, NULL), hud->hintTitlePuppet->position.y,
                  HudColour550, 1.0f, NULL);
    MissionObjective *objective = glbMissionManager->GetObjectiveByIndex(hud->pauseObjective - 1);
    if (objective != NULL) {
        char **lines;
        int lineCount = WordWrap(GSystem::LOCALE_Symbol(objective->hint), 50, &lines);
        hud->DrawFontCentered(hud->hintTextPuppet, 70.0f, 135.0f, 575.0f, 310.0f, 1.0f, HudColour550, lineCount,
                              lines);
        DeleteLines(lines, lineCount);
    }

    char before[32] = {};
    char after[32] = {};
    const char *quitText = GSystem::LOCALE_Symbol(hud->hintQuitPuppet->text.text);
    float x = (float)hud->GetCenter(hud->hintQuitPuppet, 1.0f, quitText);
    GalleryKey *button = NULL;
    // (jump table 0x000dee6c, decoded from its bytes)
    switch (hud->GetButton(GSystem::LOCALE_Symbol(hud->hintQuitPuppet->text.text), before, after)) {
    case 0:
        button = hud->xboxRTPuppet;
        break;
    case 1:
        button = hud->invertButXBOXPuppet;
        break;
    case 2:
        button = hud->triggerPuppet;
        break;
    case 3:
        button = hud->selectButXBOXPuppet;
        break;
    }
    float y = 280.0f;
    hud->DrawFont(hud->buttonTxtPuppet, x, y, HudColour550, 1.0f, before);
    float width = (float)gallery->FONT_Width(before);
    if (fgRenderer->widescreen)
        width = width * kWideTextScale;
    float buttonX = (float)((double)width + x + gallery->FONT_Width("s"));
    gallery->SPRITE_SetState(button);
    gallery->colour = kWhite;
    gallery->SPRITE_Draw2D(Ftol((double)buttonX + 5.0), Ftol((double)y + 3.0), button, NULL);
    float right = buttonX + button->sprite.width;
    float afterX = (float)(gallery->FONT_Width("s") + right);
    hud->DrawFont(hud->buttonTxtPuppet, afterX, y, HudColour550, 1.0f, after);
}

void DrawObjectivesPage(GHud *hud, PauseHints &hints, float &objectivesY) {
    if (hud->superEasy)
        return;
    objectivesY = objectivesY - 4.0f;
    hints.cont = true;
    float y = objectivesY + 39.0f;
    if (hud->pauseObjective == 0 && glbMissionManager->objectives.size != 0)
        hud->pauseObjective = 1;
    if (hud->pauseObjectivesBrowse == 0)
        DrawObjectiveList(hud, hints, objectivesY, y);
    else if (hud->pauseObjectivesBrowse == 1)
        DrawObjectiveHint(hud);
}

// A column of the controls' labels beside the pad's pictures
void DrawControlsColumn(GHud *hud, const GalleryKey *first, int firstSlot) {
    float x = first->position.x + 23.0f;
    float y = first->position.y - 5.0f;
    for (int slot = firstSlot; slot < firstSlot + 6; slot++) {
        int symbol = InputConfigManager::Get()->GetLocaleID(hud->autoDrive, hud->pauseConfig, slot);
        hud->DrawFont(hud->scratchTxtPuppet, x, y, HudTextColour, kSmallScale, GSystem::LOCALE_Symbol(symbol));
        y = y + kControlsLine;
    }
}

void DrawSprite(GGallery *gallery, GalleryKey *puppet) {
    gallery->SPRITE_SetState(puppet);
    gallery->SPRITE_Draw2D(puppet);
}

// The control configuration chosen: its name and description, the inversion in auto-drive, the pad and its labels
void DrawControlsPage(GHud *hud, PauseHints &hints) {
    if (hud->superEasy)
        return;
    GGallery *gallery = hud->gallery;
    hints.cont = true;
    hints.changePage = true;
    DrawSprite(gallery, hud->controlsGelLPuppet);
    DrawSprite(gallery, hud->controlsGelMPuppet);
    DrawSprite(gallery, hud->controlsGelRPuppet);
    if ((unsigned)InputConfigManager::Get()->GetNumConfigs(hud->autoDrive) > 1) {
        hints.scroll = true;
        gallery->SPRITE_SetState(hud->controlsArrowLPuppet);
        gallery->BITMAP_Draw2D(hud->controlsArrowLPuppet);
        gallery->SPRITE_SetState(hud->controlsArrowRPuppet);
        gallery->BITMAP_Draw2D(hud->controlsArrowRPuppet);
    }
    int symbol = InputConfigManager::Get()->GetLocaleID(hud->autoDrive, hud->pauseConfig, kControlsHeaderSlot);
    GalleryKey *header = hud->controlsHeaderPuppet;
    hud->DrawFont(header, header->position.x, header->position.y, HudTextColour, 1.0f, GSystem::LOCALE_Symbol(symbol));
    symbol = InputConfigManager::Get()->GetLocaleID(hud->autoDrive, hud->pauseConfig, kControlsDetailsSlot);
    GalleryKey *details = hud->controlsDetailsPuppet;
    hud->DrawFont(details, details->position.x, details->position.y, HudTextColour, kSmallScale, GSystem::LOCALE_Symbol(symbol));
    if (hud->autoDrive) {
        hints.invert = true;
        char inverted[256];
        uint32_t colour;
        if (InputConfigManager::Get()->IsInverted()) {
            sprintf(inverted, "%s", GSystem::LOCALE_Symbol(kTextInverted));
            colour = HudTextColour;
        } else {
            sprintf(inverted, "%s", GSystem::LOCALE_Symbol(kTextNotInverted));
            colour = HudColour4d8;
        }
        GalleryKey *invertedPuppet = hud->controlsInvertedPuppet;
        hud->DrawFont(invertedPuppet, invertedPuppet->position.x - 15.0f, invertedPuppet->position.y, colour, kInvertScale, inverted);
        const char *axis = GSystem::LOCALE_Symbol(hud->controlsYAxisPuppet->text.text);
        float invertedX = hud->controlsInvertedPuppet->position.x;
        double axisWidth = gallery->FONT_Width(axis);
        hud->DrawFont(hud->controlsYAxisPuppet, (float)((double)invertedX - axisWidth - 50.0),
                      hud->controlsYAxisPuppet->position.y, HudTextColour, kInvertScale, NULL);
    }
    DrawSprite(gallery, hud->controlsXBOXAPuppet);
    DrawSprite(gallery, hud->controlsXBOXYPuppet);
    DrawSprite(gallery, hud->controlsXBOXXPuppet);
    DrawSprite(gallery, hud->controlsXBOXBPuppet);
    DrawSprite(gallery, hud->controlsXBOXDHorPuppet);
    DrawSprite(gallery, hud->controlsXBOXDVertPuppet);
    DrawSprite(gallery, hud->controlsXBOXLAPuppet);
    DrawSprite(gallery, hud->controlsLeftTriggerPuppet);
    DrawSprite(gallery, hud->controlsXBOXBlackPuppet);
    DrawSprite(gallery, hud->controlsXBOXRAPuppet);
    DrawSprite(gallery, hud->controlsRightTriggerPuppet);
    DrawSprite(gallery, hud->controlsXBOXWhitePuppet);
    DrawControlsColumn(hud, hud->controlsXBOXAPuppet, 0);
    DrawControlsColumn(hud, hud->controlsXBOXLAPuppet, 6);
}

// Two numbers right-aligned in the score columns
void DrawScoreValues(GHud *hud, const char *left, const char *right, float y) {
    GGallery *gallery = hud->gallery;
    hud->DrawFont(hud->scratchTxtPuppet, (float)(445.0 - gallery->FONT_Width(left)), y, HudTextColour, 1.0f, left);
    hud->DrawFont(hud->scratchTxtPuppet, (float)(560.0 - gallery->FONT_Width(right)), y, HudTextColour, 1.0f, right);
}

// The scores so far: a copy of the launch page tallied (with this part's time added while the mission runs), its
// rows of labels and two columns. Rows 2 to 4 are not shown, the last only from the launch page's +0xa3c of 3.
void DrawScorePage(GHud *hud, PauseHints &hints) {
    if (hud->superEasy)
        return;
    GGallery *gallery = hud->gallery;
    LaunchPage page = Launch;
    if (fgSimulation.simState != kSimFinished) {
        if (fgWorld->attributes.LookupInt("ACCUMULATE_SCORES", NULL) == 0)
            glbMissionManager->ClearScoreBuffer(&page);
        glbMissionManager->TallyScores(&page, false);
        if (glbMissionManager->missionState != kMissionWon && glbMissionManager->missionEndTime == 0)
            page.summary[7 * 3] += fgSimulation.stepCount / fgSimulation.stepsPerSecond;
    }
    hints.cont = true;
    hints.changePage = true;

    float x = 85.0f;
    hud->DrawFont(hud->scratchTxtPuppet, 85.0f, 130.0f, HudTextColour, 1.0f, GSystem::LOCALE_Symbol(kTextScoreTitle));
    const char *column = GSystem::LOCALE_Symbol(kTextScoreColumn1);
    hud->DrawFont(hud->scratchTxtPuppet, (float)(445.0 - gallery->FONT_Width(column)), 130.0f, HudTextColour, 1.0f,
                  column);
    column = GSystem::LOCALE_Symbol(kTextScoreColumn2);
    hud->DrawFont(hud->scratchTxtPuppet, (float)(560.0 - gallery->FONT_Width(column)), 130.0f, HudTextColour, 1.0f,
                  column);
    float y = 160.0f;
    for (int row = -1; row < 9; row++) {
        if (row == 1)
            x = 125.0f;
        if (row == 5)
            x = 85.0f;
        if ((row < 2 || row > 4) && (row != 8 || Launch.unknowna3c >= 3)) {
            hud->DrawFont(hud->scratchTxtPuppet, x, y, HudTextColour, 1.0f, GSystem::LOCALE_Symbol(kTextScoreRows + row));
            y = y + kScoreLine;
        }
    }

    y = 160.0f;
    for (int row = 0; row < 9; row++) {
        char left[28];
        char right[28];
        if (row >= 2 && row <= 4)
            continue;
        if (row == 8 && Launch.unknowna3c < 3)
            continue;
        if (row == 1)
            y = y + kScoreLine;
        const int32_t *values = &page.summary[row * 3];
        if (row == 7) {
            // times in seconds, as minutes and seconds (divided unsigned)
            sprintf(left, "%d:%02d", int(uint32_t(values[0]) / 60), int(uint32_t(values[0]) % 60));
            sprintf(right, "%d:%02d", int(uint32_t(values[1]) / 60), int(uint32_t(values[1]) % 60));
        } else if (row == 6) {
            sprintf(left, "%d", Ftol(glbMissionManager->GetPlayerHealth(true) * 100.0f));
            sprintf(right, "%d", values[1]);
        } else {
            sprintf(left, "%d", values[0]);
            sprintf(right, "%d", values[1]);
        }
        DrawScoreValues(hud, left, right, y);
        y = y + kScoreLine;
    }
}

// The failed mission's menu: restart (or the super easy mode's) and quit
void DrawFailedPage(GHud *hud, PauseHints &hints, PauseColours &colours, float missionY) {
    char quit[64];
    DrawPageTitle(hud, missionY, kTextMissionFailed);
    float restartY = hud->restartMisPuppet->position.y;
    float quitY = hud->quitMisPuppet->position.y;
    hints.select = true;
    hints.scroll = true;
    if (hud->superEasy)
        sprintf(quit, "%s", GSystem::LOCALE_Symbol(kTextSuperEasyQuit));
    else
        sprintf(quit, "%s", GSystem::LOCALE_Symbol(hud->quitMisPuppet->text.text));
    const char *restart = GSystem::LOCALE_Symbol(hud->superEasy ? kTextSuperEasyRestart : kTextRestart);
    double arrowX = hud->centerX;
    float arrowY = hud->centerY;
    if (hud->pauseSelection == 1) {
        colours.restart = HudTextColour;
        arrowX = hud->GetCenter(hud->restartMisPuppet, 1.0f, restart);
        arrowY = restartY;
    } else if (hud->pauseSelection == 2) {
        colours.quit = HudTextColour;
        arrowX = hud->GetCenter(hud->quitMisPuppet, 1.0f, quit);
        arrowY = quitY;
    }
    DrawArrowAt(hud, arrowX, arrowY);
    hud->DrawFont(hud->restartMisPuppet, (float)hud->GetCenter(hud->restartMisPuppet, 1.0f, restart), restartY,
                  colours.restart, 1.0f, restart);
    hud->DrawFont(hud->quitMisPuppet, (float)hud->GetCenter(hud->quitMisPuppet, 1.0f, quit), quitY, colours.quit,
                  1.0f, quit);
}

// The four pages' titles spread over 485 from x 80, the current one lit
void DrawPageTitles(GHud *hud, float wideScale) {
    GGallery *gallery = hud->gallery;
    const char *titles[4] = {
        GSystem::LOCALE_Symbol(hud->missionPuppet->text.text),
        GSystem::LOCALE_Symbol(hud->objectivesPuppet->text.text),
        GSystem::LOCALE_Symbol(hud->controlsPuppet->text.text),
        GSystem::LOCALE_Symbol(hud->scorePuppet->text.text),
    };
    float total = 0.0f;
    gallery->FONT_SetState(hud->missionPuppet);
    gallery->FONT_SetScale(1.0f, 1.0f);
    float x[4];
    x[0] = 80.0f;
    double width = 0.0;
    for (int i = 0; i < 4; i++) {
        if (i > 0)
            x[i] = (float)(width + x[i - 1]);
        width = gallery->FONT_Width(titles[i]) * wideScale;
        total = (float)(total + width);
    }
    double gap = (485.0 - total) * kOneThird;
    for (int i = 1; i < 4; i++)
        x[i] = (float)((double)i * gap + x[i]);
    for (int i = 0; i < 4; i++) {
        uint32_t colour = i == hud->pausePage ? HudTextColour : HudColour4e0;
        hud->DrawFont(hud->missionPuppet, x[i], hud->missionPuppet->position.y + 4.0f, colour, 1.0f, titles[i]);
    }
}

// The hints along the bottom, centred: each button, then its text
void DrawHints(GHud *hud, const PauseHints &hints, float wideScale) {
    GGallery *gallery = hud->gallery;
    GalleryKey *buttons[7];
    const char *texts[7];
    float buttonWidths[7];
    float textWidths[7];
    int count = 0;
    float total = 0.0f;
    if (hints.objective) {
        buttons[count] = hud->selectButXBOXPuppet;
        texts[count++] = GSystem::LOCALE_Symbol(hud->hintPuppet->text.text);
    }
    if (hints.select) {
        buttons[count] = hud->selectButXBOXPuppet;
        texts[count++] = GSystem::LOCALE_Symbol(hud->selectPuppet->text.text);
    }
    if (hints.invert) {
        buttons[count] = hud->invertButXBOXPuppet;
        texts[count++] = GSystem::LOCALE_Symbol(hud->invertPuppet->text.text);
    }
    if (hints.back) {
        buttons[count] = hud->backButXBOXPuppet;
        texts[count++] = GSystem::LOCALE_Symbol(hud->backPuppet->text.text);
    }
    if (hints.cont) {
        buttons[count] = hud->continueButXBOXPuppet;
        texts[count++] = GSystem::LOCALE_Symbol(hud->continuePuppet->text.text);
    }
    if (hints.changePage) {
        buttons[count] = hud->changepageButXBOXPuppet;
        texts[count++] = GSystem::LOCALE_Symbol(hud->changepagePuppet->text.text);
    }
    if (hints.scroll) {
        buttons[count] = hud->scrollButXBOXPuppet;
        texts[count++] = GSystem::LOCALE_Symbol(hud->scrollPuppet->text.text);
    }
    for (int i = 0; i < count; i++) {
        buttonWidths[i] = buttons[i]->sprite.width;
        double width = gallery->FONT_Width(texts[i]) * wideScale;
        textWidths[i] = (float)width;
        total = (float)(width + total + buttonWidths[i] + 5.0);
    }
    float y = hud->hintPuppet->position.y;
    float x = (float)((hud->screenWidth - ((double)(count - 1) * 20.0 + total)) * 0.5);
    for (int i = 0; i < count; i++) {
        GalleryKey *button = buttons[i];
        gallery->SPRITE_SetState(button);
        gallery->SPRITE_Draw2D(Ftol(x), Ftol(button->position.y), button, NULL);
        float textX = (float)((double)x + buttonWidths[i] + 5.0);
        hud->DrawFont(hud->scratchTxtPuppet, textX, y, HudLetterBoxColour, 1.0f, texts[i]);
        x = (float)((double)textX + textWidths[i] + 20.0);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// The pause menu

// The page tabs, the girl, the page (jump table 0x000dee54, decoded from its bytes: mission, objectives, controls,
// score, nothing, failed), the pages' titles (not in the super easy mode or after a failure) and the button hints.
// FUNC_AT(0x000dd340)
void GHud::DrawPauseMenu() {
    PauseColours colours;
    colours.cont = colours.restart = colours.quit = colours.yes = colours.no = HudColour4d8;
    float missionY = missionPuppet->position.y;
    float objectivesY = objectivesPuppet->position.y;
    PauseHints hints = {};
    for (int i = 0; i < 3; i++) {
        gallery->SPRITE_SetState(pausePagePuppets[i]);
        gallery->SPRITE_Draw2D(pausePagePuppets[i]);
    }
    GGirl::DoGirl();
    switch (pausePage) {
    case kPauseMission:
        DrawMissionPage(this, hints, colours, missionY);
        break;
    case kPauseObjectives:
        DrawObjectivesPage(this, hints, objectivesY);
        break;
    case kPauseControls:
        DrawControlsPage(this, hints);
        break;
    case kPauseScore:
        DrawScorePage(this, hints);
        break;
    case kPauseFailed:
        DrawFailedPage(this, hints, colours, missionY);
        break;
    }
    float wideScale = fgRenderer->widescreen ? kWideTextScale : 1.0f;
    if (!superEasy && missionResultText != kTextMissionFailed)
        DrawPageTitles(this, wideScale);
    DrawHints(this, hints, wideScale);
}

// ---------------------------------------------------------------------------------------------------------------
// The mission result

// Shows at once (the argument is not read); the objective message goes, the aim too.
// FUNC_AT(0x000dee80)
void GHud::SetMissionPassed(bool) {
    nextObjectiveMsgId = -1;
    nextObjectiveMsgKind = -1;
    objectiveMsgId = 0;
    objectivePulses = 0;
    objectivePulseTime = 0.0f;
    objectiveMsgTime = 0.0f;
    missionResultText = kTextMissionPassed;
    missionResultStep = 0;
    aimOn = false;
    SniperZoom->active = 0;
    missionResultState = kResultFading;
    missionResultDelay = 0.0f;
}

// FUNC_AT(0x000deee0)
void GHud::SetMissionFailed(bool immediate) {
    nextObjectiveMsgId = -1;
    nextObjectiveMsgKind = -1;
    objectiveMsgId = 0;
    objectivePulses = 0;
    objectivePulseTime = 0.0f;
    objectiveMsgTime = 0.0f;
    missionResultText = kTextMissionFailed;
    missionResultStep = 0;
    unknown47C = 0;
    pausePage = kPauseFailed;
    aimOn = false;
    SniperZoom->active = 0;
    if (immediate) {
        missionResultState = kResultFading;
        missionResultDelay = 0.0f;
    } else if (bloodCurtainTime > 0.0f) {
        missionResultDelay = bloodCurtainTime;
        missionResultState = kResultFading;
    } else {
        missionResultState = kResultOpening;
    }
}

// The result's box opening over its keys a frame at a time, its text fading in, held until the speech is done,
// then faded; a failed mission's text shows only after the blood curtain, and its menu opens near the fade's end.
// (Jump table 0x000dfc14, decoded from its bytes: states 1 to 5 in order.)
// FUNC_AT(0x000df2e0)
void GHud::DrawMissionResult() {
    if (paused)
        return;
    uint32_t textColour = HudTextColour;
    uint32_t shadowColour = HudShadowColour;
    switch (missionResultState) {
    case kResultOpening: {
        int frames = int32_t(statusBackKeys[0]->time);    // the key's frame
        float t = (float)((double)missionResultStep / frames);
        DrawBoxBetween(gallery, statusBackPuppet, statusBackKeys[0], t);
        DrawBoxBetween(gallery, statusBackTopPuppet, statusBackTopKeys[0], t);
        DrawBoxBetween(gallery, statusBackBotPuppet, statusBackBotKeys[0], t);
        if (missionResultStep < frames) {
            missionResultStep++;
            if (missionResultStep >= frames) {
                missionResultStep = 0;
                missionResultState = kResultWidening;
            }
        }
        break;
    }
    case kResultWidening: {
        int frames = int32_t(statusBackKeys[1]->time) - int32_t(statusBackKeys[0]->time);
        float t = (float)((double)missionResultStep / frames);
        DrawBoxBetween(gallery, statusBackKeys[0], statusBackKeys[1], t);
        DrawBoxAtKey(gallery, statusBackTopKeys[0], statusBackTopKeys[1], t);
        DrawBoxAtKey(gallery, statusBackBotKeys[0], statusBackBotKeys[1], t);
        if (missionResultStep < frames) {
            missionResultStep++;
            if (missionResultStep >= frames) {
                missionResultStep = 0;
                missionResultState = kResultFadingIn;
                missionResultFadeIn = kResultFadeIn;
            }
        }
        break;
    }
    case kResultFadingIn: {
        DrawBox(this);
        float alpha = 1.0f;
        if (missionResultFadeIn > 0.0f)
            alpha = (float)(1.0 - (double)missionResultFadeIn / kResultFadeIn);
        DrawResultText(this, ScaleAlpha(shadowColour, alpha), ScaleAlpha(textColour, alpha));
        if (missionResultFadeIn > 0.0f) {
            missionResultFadeIn -= elapsed;
            if (missionResultFadeIn <= 0.0f) {
                missionResultFadeIn = 0.0f;
                missionResultState = kResultHolding;
                missionResultHold = kResultHold;
            }
        }
        break;
    }
    case kResultHolding: {
        DrawBox(this);
        DrawResultText(this, HudShadowColour, HudTextColour);
        missionResultHold -= elapsed;
        AStream *speech = AStream::Get("Speech");
        if (missionResultHold <= 0.0f && (speech->IsOver() || speech->mix != AMix::Get("Speech"))) {
            missionResultHold = 0.0f;
            missionResultState = kResultFading;
            missionResultDelay = kResultFade;
        }
        break;
    }
    case kResultFading: {
        if (missionResultText != kTextMissionFailed)
            break;
        double fade = (double)missionResultDelay / kResultFade;
        float alpha = (float)fade;
        if (fade < kResultPauseAt && !paused) {
            PauseOn();
            break;
        }
        if (missionResultDelay > 0.0f) {
            missionResultDelay -= elapsed;
            if (missionResultDelay <= 0.0f)
                missionResultDelay = 0.0f;
            if (bloodCurtainTime <= 0.0f) {
                DrawBox(this);
                DrawResultText(this, ScaleAlpha(shadowColour, alpha), ScaleAlpha(textColour, alpha));
            }
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The messages

// The button message: its text (a string, or a locale symbol when buttonMsgUnknown is one) centred, a button's
// picture in place of its marker; pulsing. Without a marker the button text puppet's own text is drawn.
// (Jump table 0x000df2c4, decoded from its bytes.)
// FUNC_AT(0x000def90)
void GHud::DrawButtonMessage() {
    if (!(buttonMsgTime > 0.0f))
        return;
    char before[64] = {};
    char after[64] = {};
    float fade = buttonMsgFade / kButtonFade;
    uint32_t textColour = ScaleAlpha(HudTextColour, fade);
    uint32_t shadowColour = ScaleAlpha(HudShadowColour, fade);
    const char *text;
    if (buttonMsgUnknown > 0)
        text = GSystem::LOCALE_Symbol(buttonMsgUnknown);
    else
        text = reinterpret_cast<const char *>(uintptr_t(buttonMsg));
    if (text == NULL) {
        buttonMsg = 0;
        buttonMsgTime = 0.0f;
        buttonMsgFade = 0.0f;
        return;
    }
    float x = (float)GetCenter(buttonTxtPuppet, kButtonScale, text);
    GalleryKey *button = NULL;
    switch (GetButton(text, before, after)) {
    case 0:
        button = xboxRTPuppet;
        break;
    case 1:
        button = xboxXPuppet;
        break;
    case 2:
        button = triggerPuppet;
        break;
    case 3:
        button = xboxAPuppet;
        break;
    }
    if (button == NULL) {
        DrawFontAndShadow(buttonTxtPuppet, x, kButtonY, shadowColour, textColour, kButtonScale, NULL);
    } else {
        uint32_t buttonColour = WhiteAlpha(fade);
        DrawFontAndShadow(buttonTxtPuppet, x, kButtonY, shadowColour, textColour, kButtonScale, before);
        float width = (float)gallery->FONT_Width(before);
        if (fgRenderer->widescreen)
            width = width * kWideTextScale;
        float buttonX = (float)((double)width + x + gallery->FONT_Width("s"));
        gallery->SPRITE_SetState(button);
        gallery->colour = buttonColour;
        gallery->SPRITE_Draw2D(Ftol((double)buttonX + 5.0), Ftol(kButtonY - (double)button->sprite.height * 0.2f), button,
                              NULL);
        float right = buttonX + button->sprite.width;
        float afterX = (float)(gallery->FONT_Width("s") + right);
        DrawFontAndShadow(buttonTxtPuppet, afterX, kButtonY, shadowColour, textColour, kButtonScale, after);
    }
    buttonMsgFade -= elapsed;
    if (buttonMsgFade <= 0.0f)
        buttonMsgFade = kButtonFade;
    buttonMsgTime -= elapsed;
    if (buttonMsgTime <= 0.0f)
        buttonMsgTime = 0.0f;
}

// The objective message: a waiting one takes over once the shown one has gone; its title by kind fades in twice
// (with a sound on the second), then the objective's text under it (not for kinds 2 and 3, which go after a
// second) in a box sized to the wider of the two.
// FUNC_AT(0x000dfc30)
void GHud::DrawObjectiveMessage() {
    if (paused)
        return;
    if (nextObjectiveMsgId != -1 && objectiveMsgKind == -1) {
        objectiveMsgTime -= elapsed;
        if (objectiveMsgTime <= 0.0f) {
            objectiveMsgId = nextObjectiveMsgId;
            objectiveMsgKind = nextObjectiveMsgKind;
            objectivePulses = 0;
            objectivePulseTime = 0.0f;
            objectiveMsgTime = kObjectiveMsgTime;
            PlayMenuSound("SFX_misstatus");
            nextObjectiveMsgId = -1;
            nextObjectiveMsgKind = -1;
        }
        return;
    }
    if (!(objectiveMsgTime > 0.0f)) {
        if (nextObjectiveMsgId != -1) {
            objectiveMsgKind = -1;
            objectiveMsgTime = kObjectiveWaitTime;
        }
        return;
    }

    int titleSymbol;
    switch (objectiveMsgKind) {
    case 2:
        titleSymbol = kTextObjectiveComplete;
        break;
    case 3:
        titleSymbol = kTextObjectiveKind3;
        break;
    default:
        titleSymbol = kTextNewObjective;
        break;
    }
    const char *title = GSystem::LOCALE_Symbol(titleSymbol);
    const char *body = GSystem::LOCALE_Symbol(objectiveMsgId);
    gallery->FONT_SetState(objObjTxtPuppet);
    gallery->FONT_SetScale(kObjectiveScale, kObjectiveScale);
    float titleWidth = (float)gallery->FONT_Width(title);
    float titleHeight = (float)gallery->FONT_Height(title);
    float bodyWidth = (float)gallery->FONT_Width(body);
    double bodyHeight = gallery->FONT_Height(body);
    float widest = bodyWidth < titleWidth ? titleWidth : bodyWidth;
    float boxWidth = 2 * kObjectiveBoxPad + widest;
    float boxTop = objObjTxtPuppet->position.y - kObjectiveBoxGap;
    float boxHeight = (float)(((double)kObjectiveBoxGap + objBodyPuppet->position.y) + bodyHeight + titleHeight - boxTop);
    gallery->QUAD_Draw2D((float)(centerX - boxWidth * 0.5), boxTop, boxWidth, boxHeight, HudMessageBoxColour);

    float alpha = 1.0f;
    if (objectivePulses < kObjectivePulses) {
        if (objectivePulseTime < kObjectivePulse) {
            alpha = objectivePulseTime / kObjectivePulse;
            objectivePulseTime = elapsed + objectivePulseTime;
        } else {
            objectivePulseTime = 0.0f;
            objectivePulses++;
        }
    }
    float x = (float)GetCenter(objObjTxtPuppet, kObjectiveScale, title);
    float y = objObjTxtPuppet->position.y;
    DrawFontAndShadow(objObjTxtPuppet, x, y, ScaleAlpha(HudShadowColour, alpha), ScaleAlpha(HudObjectiveColour, alpha),
                      kObjectiveScale, title);
    if (objectivePulses == kObjectivePulses) {
        PlayMenuSound(objectiveMsgKind == 2 ? "SFX_objcomp" : "SFX_objective");
        objectivePulses++;
        if (objectiveMsgKind == 2 || objectiveMsgKind == 3)
            objectiveMsgTime = kObjectiveDoneTime;
    }
    if (objectivePulses >= kObjectivePulses) {
        if (objectiveMsgKind != 2 && objectiveMsgKind != 3) {
            float bodyX = (float)GetCenter(objBodyPuppet, kObjectiveScale, body);
            DrawFontAndShadow(objBodyPuppet, bodyX, objBodyPuppet->position.y + kObjectiveBoxGap, HudShadowColour, HudTextColour,
                              kObjectiveScale, body);
        }
        objectiveMsgTime -= elapsed;
    }
    if (objectiveMsgTime <= 0.0f) {
        objectiveMsgTime = 0.0f;
        objectiveMsgFlag = false;
    }
}
