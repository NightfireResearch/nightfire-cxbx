#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// DAudio, the debug HUD's audio pages, and its list of targets. See DebugAudio.h.

#pragma fp_contract(off)

#include "DebugAudio.h"

#include "Hud.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "../audio/Mix.h"
#include "../audio/Sound.h"
#include "../audio/SoundManager.h"
#include "../audio/Stream.h"
#include "../audio/Voice.h"
#include "../camera/Camera.h"
#include "../eagl/EaglFont.h"
#include "../eagl/Realgraph.h"
#include "../engine/CoreFoundation.h"
#include "../engine/Feedback.h"
#include "../engine/GameLoop.h"           // gDAudio
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/Draw.h"
#include "../render/RenderHigh.h"
#include "../render/Renderer.h"
#include "../world/Grid.h"
#include "../../common/xbeOverload.h"
#include "../../helpers.h"
#include "../platform/CRuntime.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---- originals called by address


// ---- the game's state

extern MixList Mixes;   // Mix.cpp's list of the mixes Load read
extern int32_t StreamHold;   // Stream.cpp's StreamHoldState

// Constructed by the static initialisers (name ours)
// XBE_GLOBAL(0x001e2278, 0xc)
DAudioTargetList DAudioTargets;

// The mixer page (names ours)
// XBE_GLOBAL(0x001e2268, 0x4)
int32_t DAudioMixerSelected;
// XBE_GLOBAL(0x001e226c, 0x4)
int32_t DAudioMixerScrollTop;
// XBE_GLOBAL(0x001e2270, 0x1)
bool DAudioMixerEditing;
// XBE_GLOBAL(0x001e4760, 0x1)
bool DAudioMixerVisible;

namespace {


// Stream.cpp's StreamHoldState
enum : int32_t {
    kHoldNone = 0,
    kHoldWaiting = 1,
    kHoldReleased = 2,
};

constexpr uint32_t kMaxTargets = 0x5d1745d;         // allocator<node>::max_size()

constexpr uint32_t kTextGrey = 0xffaaaaaa;
constexpr uint32_t kTextBlack = 0xff000000;
constexpr uint32_t kBoxColour = 0xaf000000;         // translucent black
constexpr uint32_t kSelectedBoxColour = 0xaf7f7f7f;
constexpr uint32_t kTargetColour = 0xc0ffffff;

// A mix's group colour (unknown20, 0..6), without its alpha
constexpr uint32_t kGroupColours[7] = {
    0xff00ff, 0x7f00ff, 0x0000ff, 0x00ff00, 0xffff00, 0xff7f00, 0xff0000,
};

constexpr int kRowHeight = 14;
constexpr int kMixerRows = 25;                      // the rows the editing mode shows

typedef char* (ABaseSound::*GetNameMethod)();

// The map's iterator's operator++ (inlined)
AVoiceMapNode *NextNode(AVoiceMapNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    AVoiceMapNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// The targets' list

// FUNC_AT(0x00037640)
DAudioTargetList* DAudioTargetList::Construct() {
    head = (DAudioTargetNode *)reinterpret_cast<WGridMoverList *>(this)->BuyHead();
    size = 0;
    return this;
}

// FUNC_AT(0x00037660)
void DAudioTargetList::Destruct() {
    DAudioTargetNode *ignored;
    Erase(&ignored, Begin(), head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(DAudioTargetNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x00037160)
DAudioTargetNode** DAudioTargetList::Erase(DAudioTargetNode **result, DAudioTargetNode *first,
                                           DAudioTargetNode *last) {
    while (first != last) {
        DAudioTargetNode *node = first;
        first = first->next;
        if (node != head) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            UMemory::FastFree(node, sizeof(DAudioTargetNode));
            size--;
        }
    }
    *result = first;
    return result;
}

// FUNC_AT(0x000376a0)
void DAudioTargetList::IncreaseSize(uint32_t count) {
    DEBUGAUDIO_UNTESTED("DAudioTargetList::IncreaseSize");
    if (kMaxTargets - size < count)
        ThrowLengthError("list<T> too long");
    size += count;
}

// ---------------------------------------------------------------------------------------------------------------
// DAudio

// FUNC_AT(0x00036c90)
void DAudio::DrawTargets() {
    DEBUGAUDIO_UNTESTED("DAudio::DrawTargets");
    for (DAudioTargetNode *node = DAudioTargets.Begin(); node != DAudioTargets.head; node = node->next) {
        char label[64];
        Crt::sprintf(label, "(%.0f)%s", double(node->value.distance), node->value.name);
        GHud::TheApp()->dbDrawTarget(&node->value.screen, "", 5.0f, kTargetColour, label);
    }
}

// The editing mode pauses the game every frame it draws. A group past 6 picks its colour past the table, as the
// original reads past its own; only the selected row's group is clamped, after its colour is picked.
//
// FUNC_AT(0x00036d10)
void DAudio::MixerDraw() {
    DEBUGAUDIO_UNTESTED("DAudio::MixerDraw");
    const uint8_t *font = (const uint8_t *)fgRenderer->debugFont;
    if (DAudioMixerEditing && fgSimulation.simState != kSimPaused) {
        fgSimulation.PauseSimState();
        glbMissionManager->Stop();
        IFeedback::Pause();
        ASoundManager::Pause();
    }

    int y = 64;             // the next row with a level
    int quietY = 256;       // the next row without
    int index = 0;
    uint32_t boxColour = kBoxColour;
    for (PointerListNode *node = Mixes.Begin(); node != Mixes.head; node = node->next) {
        AMix *mix = (AMix *)node->value;
        int group = mix->unknown20;
        if (group <= -1)
            continue;
        if (!DAudioMixerEditing && !(mix->unknown28 > 0.0f)) {
            index++;
            continue;
        }
        if (y - 1 >= 413) {
            index++;
            continue;
        }
        if (DAudioMixerEditing) {
            if (index < DAudioMixerScrollTop) {
                index++;
                continue;
            }
            if (index == DAudioMixerSelected) {
                fgRenderer->debugFont->colour = kTextBlack;
                boxColour = kSelectedBoxColour;
                if (mix->unknown20 < 0)
                    mix->unknown20 = 0;
                else if (mix->unknown20 > 6)
                    mix->unknown20 = 6;
            }
        }

        uint32_t colour = kGroupColours[group];
        int alpha;
        if (DAudioMixerEditing) {
            alpha = 0xff;
        } else {
            alpha = RoundToInt(mix->unknown28 * 85.0f);
            if (alpha > 0xff)
                alpha = 0xff;
        }
        colour += uint32_t(alpha) << 24;

        int rowY;
        float swatchX = float(group * 24 + 200);
        if (DAudioMixerEditing) {
            rowY = y;
            Draw::DrawBox(30.0f, float(y - 1), 336.0f, 14.0f, boxColour);
            Draw::DrawBox(swatchX, float(rowY), 20.0f, 12.0f, colour);
            if (index == DAudioMixerSelected) {
                Draw::DrawBox(swatchX, float(rowY), 20.0f, 2.0f, kTextBlack);
                Draw::DrawBox(swatchX, float(rowY + 10), 20.0f, 2.0f, kTextBlack);
                Draw::DrawBox(swatchX, float(rowY + 2), 2.0f, 10.0f, kTextBlack);
                Draw::DrawBox(float(group * 24 + 218), float(rowY + 2), 2.0f, 10.0f, kTextBlack);
                fgRenderer->debugFont->colour = kTextGrey;
                boxColour = kBoxColour;
            }
            y += kRowHeight;
        } else {
            if (mix->unknown2c > 0.0f) {
                rowY = y;
                y += kRowHeight;
            } else {
                rowY = quietY;
                quietY += kRowHeight;
            }
            Draw::DrawBox(30.0f, float(rowY - 1), 336.0f, 14.0f, boxColour);
            Draw::DrawBox(swatchX, float(rowY), 20.0f, 12.0f, colour);
        }

        // Right-aligned at 178; the name is the format, as in the original
        float width = 0.0f;
        FONT_getrectx_thunk(font, (const uint8_t *)mix->name, NULL, NULL, &width, NULL);
        FONT_drawtextfa(font, float(178 - RoundToInt(width)), float(rowY), mix->name);
        index++;
    }

    if (DAudioMixerSelected < 0)
        DAudioMixerSelected = 0;
    else if (DAudioMixerSelected >= index)
        DAudioMixerSelected--;
    int fromTop = DAudioMixerSelected - DAudioMixerScrollTop;
    if (fromTop < 0)
        DAudioMixerScrollTop--;
    else if (fromTop >= kMixerRows)
        DAudioMixerScrollTop++;
}

// FUNC_AT(0x000371b0)
void DAudio::InfoDraw() {
    DEBUGAUDIO_UNTESTED("DAudio::InfoDraw");
    const uint8_t *font = (const uint8_t *)fgRenderer->debugFont;
    fgRenderer->debugFont->colour = kTextGrey;
    AStream *music = AStream::Get("Music");
    AStream *speech = AStream::Get("Speech");

    float latency = (float)music->GetLatency();
    Draw::DrawBox(380.0f, 32.0f, 48.0f, 18.0f, kBoxColour);
    FONT_drawtextfa(font, 380.0f, 34.0f, "%.3f", double(latency));
    const char *state = ASoundManager_fgMissionOver ? "fade" : "play";
    Draw::DrawBox(430.0f, 32.0f, 200.0f, 18.0f, kBoxColour);
    float volume = music->GetInternalVolume();
    const char *file = music->GetFile();
    FONT_drawtextfa(font, 430.0f, 34.0f, "%s (%.3f) %s", file, double(volume), state);

    latency = (float)speech->GetLatency();
    Draw::DrawBox(380.0f, 50.0f, 48.0f, 18.0f, kBoxColour);
    FONT_drawtextfa(font, 380.0f, 52.0f, "%.3f", double(latency));
    file = speech->IsOver() ? "" : speech->GetFile();
    char hold = 0;          // the original leaves a stack byte for any other state
    switch (StreamHold) {
    case kHoldNone:
        hold = '0';
        break;
    case kHoldWaiting:
        hold = 'H';
        break;
    case kHoldReleased:
        hold = 'P';
        break;
    }
    Draw::DrawBox(430.0f, 50.0f, 200.0f, 18.0f, kBoxColour);
    volume = speech->GetInternalVolume();
    FONT_drawtextfa(font, 430.0f, 52.0f, "%s (%.3f) %c", file, double(volume), hold);

    // The effect mode's mix is "X:" and its name
    char mixName[32];
    sprintf(mixName, "X:%s", AFX::GetCurrentModeName());
    float level;
    if (_stricmp(mixName, "x:off") == 0)
        level = 0.0f;
    else
        level = (float)AMix::Get(mixName)->GetVolume();
    Draw::DrawBox(430.0f, 68.0f, 200.0f, 18.0f, kBoxColour);
    FONT_drawtextfa(font, 430.0f, 70.0f, "Env: %s @ %.1f", AFX::GetCurrentModeName(), double(level));

    // Each patch playing: its voices, their average volume (in brackets with an effects level), its name - the
    // name is the format, as in the original
    AVoiceMap *map = AVoice::BuildMap();
    uint32_t height = map->size * 14 + 4;
    int total = 0;
    int y = 106;
    Draw::DrawBox(430.0f, 104.0f, 200.0f, float(height), kBoxColour);
    for (AVoiceMapNode *node = map->Begin(); node != map->head; node = NextNode(node)) {
        const AVoiceMapInfo &info = node->value.info;
        float rowY = float(y);
        total += info.count;
        FONT_drawtextfa(font, 430.0f, rowY, "%d", info.count);
        if (info.fx)
            FONT_drawtextfa(font, 450.0f, rowY, "(%d)", info.volume / info.count);
        else
            FONT_drawtextfa(font, 450.0f, rowY, "%d", info.volume / info.count);
        FONT_drawtextfa(font, 490.0f, rowY, node->value.name);
        y += kRowHeight;
    }
    Draw::DrawBox(430.0f, 86.0f, 200.0f, 18.0f, kBoxColour);
    FONT_drawtextfa(font, 430.0f, 88.0f, "%d", total);
    FONT_drawtextfa(font, 450.0f, 88.0f, "TOTAL");
}

// FUNC_AT(0x00037620)
void DAudio::Draw() {
    if (gDAudio)
        InfoDraw();
    if (DAudioMixerVisible)
        MixerDraw();
}

// FUNC_AT(0x00037750)
void DAudio::UpdateTargets() {
    DEBUGAUDIO_UNTESTED("DAudio::UpdateTargets");
    // The camera's unknown50 is what the listener takes as its velocity
    RCamera *camera = fgRenderHigh->views[0].view->camera;
    AListener listener;
    listener.Construct(&camera->matrix, (const Coord3 *)&camera->unknown50, 2);

    DAudioTargetNode *ignored;
    DAudioTargets.Erase(&ignored, DAudioTargets.Begin(), DAudioTargets.head);

    ASoundList *sounds = fgSoundList;
    for (PointerListNode *node = sounds->Begin(); node != sounds->head; node = node->next) {
        ABaseSound *sound = (ABaseSound *)node->value;
        if (!(sound->maxDistance > 0.0f))
            continue;
        bool heard = false;
        for (int view = 0; view < kSoundViewCount; view++) {
            if (sound->lastVolumes[view] > 0.0f) {
                heard = true;
                break;
            }
        }
        if (!heard)
            continue;
        bool onScreen;
        ScreenPos screen;
        GHud::TheApp()->GetScreenCoords(&sound->position, &onScreen, &screen);
        if (!onScreen)
            continue;
        float distance = vec3distance(&listener.position, &sound->position);
        if (!(distance < sound->maxDistance))
            continue;

        DAudioTarget target;
        const char *name = (sound->*XbeVirtual<GetNameMethod>(sound, 2))();
        target.screen = screen;
        target.distance = distance;
        strcpy(target.name, name);

        DAudioTargetNode *head = DAudioTargets.head;
        DAudioTargetNode *added = (DAudioTargetNode *)reinterpret_cast<WGridMoverList *>(&DAudioTargets)->BuyNode(
            (WGridMoverNode *)head, (WGridMoverNode *)head->prev, (const WGridMover *)&target);
        DAudioTargets.IncreaseSize(1);
        head->prev = added;
        added->prev->next = added;
    }
}
