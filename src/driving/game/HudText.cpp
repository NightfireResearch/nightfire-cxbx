#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// GHud's fonts and messages. See HudText.h.

#pragma fp_contract(off)

#include "HudText.h"

#include "FrontEndSystem.h"
#include "Gallery.h"
#include "MissionManager.h"
#include "../engine/UMemory.hpp"          // OperatorNewArray
#include "../platform/X87.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

constexpr int kMaxLines = 5;
constexpr float kLineGap = 5.0f;
constexpr float kMessageScale = 1.2f;
static_assert(std::bit_cast<uint32_t>(kMessageScale) == 0x3f99999a, "1.2");
constexpr float kMessageY = 380.0f;
constexpr float kMessageBoxY = 376.0f;
constexpr float kMessageFadeTime = 0.5f;
constexpr float kTargetOffset = 19.0f;
constexpr float kTargetSpread = 0.0666666701f;
static_assert(std::bit_cast<uint32_t>(kTargetSpread) == 0x3d888889, "a fifteenth");

// The height of a line of the current font
double LineHeight(GGallery *gallery) {
    return gallery->FONT_Height("T") + kLineGap;
}

} // namespace

// FUNC_AT(0x000d9740)
int WordWrap(const char *text, unsigned width, char ***lines) {
    int line = 0;
    *lines = static_cast<char **>(OperatorNewArray(kMaxLines * sizeof(char *)));
    for (int i = 0; i < kMaxLines; i++)
        (*lines)[i] = NULL;
    while (strlen(text) > width && (unsigned)line < kMaxLines - 1) {
        int cut = width - 1;
        while (cut > -1 && text[cut] != ' ')
            cut--;
        int length = cut == -1 ? (int)width : cut;
        (*lines)[line] = static_cast<char *>(OperatorNewArray(length + 1));
        (*lines)[line][length] = '\0';
        memcpy((*lines)[line], text, length);
        text += length;
        while (*text == ' ')
            text++;
        line++;
    }
    size_t length = strlen(text);
    if (length > width)
        length = width;
    (*lines)[line] = static_cast<char *>(OperatorNewArray(length + 1));
    (*lines)[line][length] = '\0';
    memcpy((*lines)[line], text, length);
    return line + 1;
}

// FUNC_AT(0x000d9370)
double GHud::GetCenter(GalleryKey *puppet, float scale, const char *text) {
    if (text == NULL)
        text = GSystem::LOCALE_Symbol(puppet->text.text);
    gallery->FONT_SetState(puppet);
    gallery->FONT_SetScale(scale, scale);
    return centerX - gallery->FONT_Width(text) * 0.5;
}

// FUNC_AT(0x000d98d0)
void GHud::DrawFontAndShadow(GalleryKey *puppet, float x, float y, uint32_t shadowColour, uint32_t colour,
                             float scale, const char *text) {
    gallery->FONT_SetState(puppet);
    if (text == NULL)
        text = GSystem::LOCALE_Symbol(puppet->text.text);
    gallery->colour = shadowColour;
    gallery->FONT_SetScale(scale, scale);
    gallery->FONT_Draw(text, x + 1.0f, y + 1.0f);
    gallery->colour = colour;
    gallery->FONT_SetScale(scale, scale);
    gallery->FONT_Draw(text, x, y);
}

// FUNC_AT(0x000d9970)
void GHud::DrawFontCentered(GalleryKey *puppet, float left, float top, float right, float bottom, float scale,
                            uint32_t colour, unsigned count, char **lines) {
    gallery->FONT_SetState(puppet);
    gallery->FONT_SetScale(scale, scale);
    gallery->colour = colour;
    float lineHeight = (float)LineHeight(gallery);
    float y = (float)((((double)bottom - top) - (double)count * lineHeight) * 0.5 + top);
    if (count == 0)
        return;
    float boxWidth = right - left;
    for (unsigned i = 0; i < count; i++) {
        float x = (float)((boxWidth - gallery->FONT_Width(lines[i])) * 0.5 + left);
        gallery->FONT_Draw(lines[i], x, y);
        y = y + lineHeight;
    }
}

// FUNC_AT(0x000d9a50)
void GHud::DrawFont(GalleryKey *puppet, float x, float y, uint32_t colour, float scale, char **lines, int count) {
    gallery->FONT_SetState(puppet);
    gallery->FONT_SetScale(scale, scale);
    gallery->colour = colour;
    float lineHeight = (float)LineHeight(gallery);
    for (int i = 0; i < count; i++) {
        gallery->FONT_Draw(lines[i], x, y);
        y = lineHeight + y;
    }
}

// FUNC_AT(0x000d9ad0)
void GHud::DrawFont(GalleryKey *puppet, float x, float y, uint32_t colour, float scale, const char *text) {
    gallery->FONT_SetState(puppet);
    gallery->FONT_SetScale(scale, scale);
    if (text == NULL)
        text = GSystem::LOCALE_Symbol(puppet->text.text);
    gallery->colour = colour;
    gallery->FONT_Draw(text, x, y);
}

// The mission message centred low on the screen in a box (the box in its own colour, its alpha left alone; the
// text's shadow in the box's colour faded); it fades out after its time.
// FUNC_AT(0x000da2e0)
void GHud::DrawMessage() {
    float alpha = 1.0f;
    bool draw = true;
    if (!(missionMsgTime > 0.0f)) {
        if (!(missionMsgFade > 0.0f)) {
            missionMsgLong = false;
            draw = false;
        } else {
            alpha = missionMsgFade / kMessageFadeTime;
            if (!(alpha > 0.0f))
                draw = false;
        }
    }
    if (draw) {
        const char *text = GSystem::LOCALE_Symbol(missionMsg);
        float scale = kMessageScale;
        float x = (float)GetCenter(txtPuppet, scale, text);
        uint32_t textColour = (HudTextColour & 0xffffff) | uint32_t(RoundToInt(float(HudTextColour >> 24) * alpha)) << 24;
        uint32_t shadowColour = (HudMessageBoxColour & 0xffffff) |
                                uint32_t(RoundToInt(float(HudMessageBoxColour >> 24) * alpha)) << 24;
        gallery->FONT_SetState(txtPuppet);
        gallery->FONT_SetScale(scale, scale);
        float width = (float)gallery->FONT_Width(text);
        float height = (float)(gallery->FONT_Height(text) + 18.0);
        gallery->QUAD_Draw2D(x - 4.0f, kMessageBoxY, width + 12.0f, height, HudMessageBoxColour);
        DrawFontAndShadow(txtPuppet, x, kMessageY, shadowColour, textColour, scale, text);
    }
    if (missionMsgFade > 0.0f) {
        missionMsgFade -= elapsed;
        if (missionMsgFade <= 0.0f) {
            missionMsgFade = 0.0f;
            missionMsgLong = false;
        }
    }
    if (missionMsgTime > 0.0f) {
        missionMsgTime -= elapsed;
        if (missionMsgTime <= 0.0f) {
            missionMsgTime = 0.0f;
            missionMsgFade = kMessageFadeTime;
        }
    }
}

// The debug audio page's target: its texts, a two-line bracket over them and a blip. The offset's spread is
// multiplied by zero, which leaves 19 (or NaN).
// FUNC_AT(0x000da8e0)
void GHud::dbDrawTarget(const ScreenPos *position, const char *text, float, uint32_t colour, const char *text2) {
    HUD_UNTESTED("GHud::dbDrawTarget");
    float dx = (float)(((double)centerX - kTargetOffset) * kTargetSpread * 0.0 + kTargetOffset);
    float dy = (float)((((double)centerY - kTargetOffset) * kTargetSpread * 0.0 + kTargetOffset) * 0.5);
    gallery->FONT_Draw(text, (float)((double)dx + position->x), (float)((double)dy + position->y - 12.0), colour);
    float width = (float)gallery->FONT_Width(text);
    if (text2 != NULL)
        gallery->FONT_Draw(text2, (float)((double)dx + position->x),
                          (float)((double)dy + position->y + 4.0), colour);
    float right = (float)((double)width + position->x + dx);
    float top = (float)((double)dy + position->y);
    float below = top + 1.0f;
    uint32_t colours[5] = {colour, colour, colour, colour, colour};
    Coord4 points[4] = {
        {right, top, 0.0f, 1.0f},
        {position->x, top, 0.0f, 1.0f},
        {right, below, 0.0f, 1.0f},
        {position->x, below, 0.0f, 1.0f},
    };
    gallery->LINE_Draw2DStrip(&points[0], colours, 2);
    gallery->LINE_Draw2DStrip(&points[2], colours, 2);
    gallery->SPRITE_SetState(blipPuppet);
    gallery->colour = colour;
    gallery->SPRITE_Draw2D(Ftol(position->x), Ftol((double)dy + position->y - 1.0), blipPuppet, NULL);
}
