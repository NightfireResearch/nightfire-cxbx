#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// The subtitles: GSubtitles and GSubtitleSplitter. See Subtitles.h.

#pragma fp_contract(off)

#include "Subtitles.h"
#include "../Vtables.h"   // our copies of the game's vtables

#include "FrontEndSystem.h"               // GSystem::LOCALE_Symbol
#include "MissionManager.h"
#include "Simulation.h"
#include "../eagl/EaglFont.h"             // FNTXFont
#include "../eagl/GeoPrimState.h"
#include "../eagl/Realgraph.h"            // FONT_
#include "../engine/CoreFoundation.h"     // GetFoundationVideoMode
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"          // OperatorNewArray, OperatorDelete
#include "../platform/RealMemory.h"       // MEM_free
#include "../platform/RealSystem.h"       // TIMER_gettick
#include "../render/Draw.h"
#include "../render/Materials.h"
#include "../render/Renderer.h"           // fgRenderer
#include "../../helpers.h"
#include "../platform/CRuntime.h"

#include <bit>
#include <string.h>

// XBE_GLOBAL(0x0023f828, 0x4)
GSubtitles *TheSubtitles;

namespace {

void **const kSubtitlesVtable = Vtable_GSubtitles;
constexpr uint32_t kDepthAlways = 0x207;
constexpr uint32_t kSubtitleColour = 0xfefefefe;
constexpr float kSubtitleScale = 1.0f;
constexpr float kBottomMargin = 65.0f;
constexpr float kPAL50FrameTime = 1.0f / 25.0f;
constexpr float kNTSCFrameTime = 1.0f / 30.0f;
static_assert(std::bit_cast<uint32_t>(kPAL50FrameTime) == 0x3d23d70a, "1 / 25");
static_assert(std::bit_cast<uint32_t>(kNTSCFrameTime) == 0x3d088889, "1 / 30");

constexpr int kVideoPAL50 = 2;          // GetFoundationVideoMode's

// The seconds since `startTick`: simulation steps, or real clock ticks, as the x87 leaves them
double SecondsSince(int32_t startTick, bool simClock) {
    if (simClock)
        return double(fgSimulation.stepCount - startTick) * fgSimulation.timeStep;
    float ticks = float(TIMER_gettick() - startTick);
    return ticks / double(TIMER_getfrequency());
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// GSubtitleSplitter

// FUNC_AT(0x000e31a0)
unsigned GSubtitleSplitter::WordWrap(const char *text, unsigned width, char **lines) {
    unsigned line = 0;
    int length = strlen(text);
    memset(lines, 0, kMaxLines * sizeof(char *));
    while (strlen(text) > width && line < kMaxLines - 1) {
        int end = width - 1;
        while (end > -1 && text[end] != ' ')
            end--;
        if (end == -1) {
            lines[line] = static_cast<char *>(OperatorNewArray(width + 1));
            lines[line][width] = '\0';
            memcpy(lines[line], text, width);
            times[line] = float(double(width) * duration / length);
            text += width;
        } else {
            lines[line] = static_cast<char *>(OperatorNewArray(end + 1));
            lines[line][end] = '\0';
            memcpy(lines[line], text, end);
            times[line] = float(double(end) * duration / length);
            text += end;
        }
        while (*text == ' ')
            text++;
        line++;
    }
    unsigned rest = strlen(text);
    if (rest > width)
        rest = width;
    lines[line] = static_cast<char *>(OperatorNewArray(rest + 1));
    lines[line][rest] = '\0';
    memcpy(lines[line], text, rest);
    times[line] = float(double(rest) * duration / length);
    return line + 1;
}

// FUNC_AT(0x000e3390)
void GSubtitleSplitter::Init(const char *text, float duration, bool simClock) {
    this->duration = duration;
    current = 0;
    count = WordWrap(text, kLineLength, lines);
    this->simClock = simClock;
    startTick = simClock ? fgSimulation.stepCount : TIMER_gettick();
}

// FUNC_AT(0x000e33e0)
void GSubtitleSplitter::Reset() {
    for (unsigned i = 0; i < count; i++) {
        if (lines[i] != NULL)
            OperatorDelete(lines[i]);
    }
    startTick = 0;
    duration = 0.0f;
    count = 0;
}

// FUNC_AT(0x000e3430)
const char* GSubtitleSplitter::GetString() {
    if (duration == 0.0f)
        return NULL;
    if (SecondsSince(startTick, simClock) >= times[current]) {
        if (current < kMaxLines - 1)
            current++;
        if (simClock) {
            startTick = fgSimulation.stepCount;
            return lines[current];
        }
        startTick = TIMER_gettick();
    }
    return lines[current];
}

// ---------------------------------------------------------------------------------------------------------------
// GSubtitles

// FUNC_AT(0x000e3180)
GSubtitles* GSubtitles::TheApp() {
    return TheSubtitles;
}

// FUNC_AT(0x000e34e0)
GSubtitles* GSubtitles::Construct() {
    vtable = kSubtitlesVtable;
    splitter.count = 0;
    splitter.duration = 0.0f;
    splitter.startTick = 0;
    font = NULL;
    fontFile = NULL;
    symbol = 0;
    startTick = 0;
    duration = 0.0f;
    screenWidth = float(fgRenderer->screenWidth);
    cues = NULL;
    cueCount = 0;
    nextCue = 0;
    screenHeight = float(fgRenderer->screenHeight);
    fontFile = UFileLoader::FileLoad("data\\loading\\loading.xfn", 0);
    font = (FNTXFont *)FONT_create(static_cast<const uint8_t *>(fontFile));
    TheSubtitles = this;
    return this;
}

// FUNC_AT(0x000e3730)
void GSubtitles::Destruct() {
    vtable = kSubtitlesVtable;
    if (font != NULL)
        FONT_destroy(reinterpret_cast<const uint8_t *>(font));
    if (fontFile != NULL)
        MEM_free_copy(fontFile);
    if (cues != NULL)
        OperatorDelete(cues);
    cues = NULL;
    nextCue = 0;
    splitter.Reset();
    TheSubtitles = NULL;
}

// FUNC_AT(0x000e3ad0)
GSubtitles* GSubtitles::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x000e3190)
bool GSubtitles::IsSubtitleDisplaying() {
    return symbol != 0;
}

// FUNC_AT(0x000e3840)
void GSubtitles::LoadSubtitles(const char *file) {
    if (file == NULL)
        return;
    char *text = static_cast<char *>(UFileLoader::FileLoad(file, 0x100));
    if (text == NULL)
        return;
    if (cues != NULL)
        OperatorDelete(cues);
    cues = NULL;
    nextCue = 0;
    splitter.Reset();

    const char *countLine = strtok(text, "\r\n");
    long count = Crt::atol(countLine);
    char *next = text + strlen(countLine) + 2;      // past the line and its CR LF
    nextCue = 0;
    cueCount = count + 1;
    cues = static_cast<SubtitleCue *>(OperatorNewArray((count + 1) * sizeof(SubtitleCue)));
    cues[0].frame = 0;
    cues[0].symbol = 0;
    symbol = 0;
    startTick = 0;
    duration = 0.0f;
    for (unsigned i = 1; i < cueCount; i++) {
        char *line = strtok(next, "\r\n");
        next += strlen(line) + 2;
        long frame = Crt::atol(strtok(line, "=") + 6);
        long cueSymbol = Crt::atol(strtok(NULL, "="));
        if (GetFoundationVideoMode() == kVideoPAL50)
            frame = frame * 5 / 6;
        cues[i].frame = frame;
        cues[i].symbol = cueSymbol;
    }
    MEM_free(text);
}

// FUNC_AT(0x000e3630)
void GSubtitles::ClearSubtitles() {
    startTick = 0;
    duration = 0.0f;
    symbol = 0;
    splitter.Reset();
    if (cues != NULL)
        OperatorDelete(cues);
    cues = NULL;
    nextCue = 0;
    splitter.Reset();
}

// FUNC_AT(0x000e3560)
void GSubtitles::SetupFMVSubtitles(unsigned frame) {
    if (cueCount == 0 || nextCue >= cueCount)
        return;
    simClock = false;
    if (nextCue < cueCount - 1 && frame >= cues[nextCue].frame) {
        splitter.Reset();
        symbol = cues[nextCue].symbol;
        float seconds = float(cues[nextCue + 1].frame - cues[nextCue].frame);
        if (GetFoundationVideoMode() == kVideoPAL50)
            seconds = seconds * kPAL50FrameTime;
        else
            seconds = seconds * kNTSCFrameTime;
        splitter.Init(GSystem::LOCALE_Symbol(symbol), seconds, simClock);
        duration = seconds;
        nextCue++;
    }
    startTick = TIMER_gettick();
}

// FUNC_AT(0x000e3790)
void GSubtitles::DisplaySubtitle(int symbol, float duration) {
    if (glbMissionManager->GoingToSkipCinematic()) {
        ClearSubtitles();
        return;
    }
    this->duration = duration;
    this->symbol = symbol;
    startTick = fgSimulation.stepCount;
    splitter.Reset();
    simClock = true;
    splitter.Init(GSystem::LOCALE_Symbol(symbol), duration, true);
}

// FUNC_AT(0x000e39b0)
void GSubtitles::DrawSubtitles() {
    if (symbol == 0 || duration <= 0.0f)
        return;
    if (SecondsSince(startTick, simClock) >= duration) {
        splitter.Reset();
        startTick = 0;
        duration = 0.0f;
        symbol = 0;
        return;
    }
    const char *line = splitter.GetString();
    if (line == NULL)
        return;
    float width, height;
    GetWidthHeight(line, kSubtitleScale, &width, &height);
    DrawFont(line, float((double(screenWidth) - width) * 0.5), float(double(screenHeight) - kBottomMargin - 2.0),
             kSubtitleScale, kSubtitleColour);
}

// FUNC_AT(0x000e3ab0)
void GSubtitles::DrawFrame(int frame) {
    if (TheSubtitles == NULL)
        return;
    TheSubtitles->SetupFMVSubtitles(frame);
    TheSubtitles->DrawSubtitles();
}

// FUNC_AT(0x000e3670)
void GSubtitles::DrawFont(const char *text, float x, float y, float scale, uint32_t colour) {
    SimpleTexturedMaterial->SetAlphaTestEnable(false);
    SimpleTexturedMaterial->SetDepthTestMethod(kDepthAlways);
    SimpleMaterial->SetDepthTestMethod(kDepthAlways);
    Draw::SetZDepth(0.0f);
    font->scaleX = scale;
    font->scaleY = scale;
    font->colour = colour;
    FONT_drawtextfa(reinterpret_cast<const uint8_t *>(font), x, y, text);
}

// FUNC_AT(0x000e36f0)
void GSubtitles::GetWidthHeight(const char *text, float scale, float *width, float *height) {
    font->scaleX = scale;
    font->scaleY = scale;
    FONT_getrectx(reinterpret_cast<const uint8_t *>(font), reinterpret_cast<const uint8_t *>(text), NULL, NULL,
                  width, height);
}
