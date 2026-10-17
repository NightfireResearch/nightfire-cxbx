#ifndef DRIVING_GAME_DEBUGAUDIO_H_
#define DRIVING_GAME_DEBUGAUDIO_H_

// ---------------------------------------------------------------------------------------------------------------
// DAudio: the debug HUD's audio pages (0x00036c90..0x00037750). The screen-space debug view draws them every frame
// (DAudio::Draw):
//   - InfoDraw, with gDAudio set: the music and speech streams (latency, file, volume), the effect mode and its
//     mix's volume, and every patch the voices are playing (AVoice::BuildMap: count, average volume, name);
//   - MixerDraw, with its own flag set: the mixes as rows, a swatch per group, and a selection the editing mode
//     scrolls through.
// GHud::UpdateTargets and GHud::Draw call UpdateTargets and DrawTargets with gDAudio set: the sounds on screen in
// range of the first view's camera, kept in a list and labelled with their distance and name.
//
// gDAudio is set from the launch page (GameLoop.cpp); nothing sets the mixer's flag or its editing flag. Only Draw
// runs in a shipped run: the pages are provisional ports, each with a one-time untested warning.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../world/Targeting.h"         // ScreenPos
#include "../../common/xbeClass.h"      // LAYOUT_32

// The warning beside a provisional port: code no shipped run reaches, ported from the listing without a test. It
// says so once, the first time it runs.
inline void DebugAudioUntested(const char *what) {
    printf("[game] WARNING: %s ran - a provisional port that no shipped run reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define DEBUGAUDIO_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            DebugAudioUntested(what); \
        } \
    } while (0)

// A sound on screen (0x2c bytes; names ours)
struct DAudioTarget {
    ScreenPos screen;           // +0x00
    float distance;             // +0x08 from the listener
    char name[0x20];            // +0x0c the sound's GetName
};
static_assert(sizeof(DAudioTarget) == 0x2c, "a target is 0x2c bytes");

struct DAudioTargetNode {
    DAudioTargetNode *next;
    DAudioTargetNode *prev;
    DAudioTarget value;
};
LAYOUT_32(sizeof(DAudioTargetNode) == 0x34, "a target node is 0x34 bytes");

// std::list<DAudioTarget>. The linker folded its _Buynode, its head's _Buynode and erase with
// those of WGrid's list of dynamic elements (world/Grid.h), whose nodes are the same size.
struct DAudioTargetList {
    uint8_t allocator;          // +0x00
    uint8_t unknown01[3];
    DAudioTargetNode *head;     // +0x04
    uint32_t size;              // +0x08

    DAudioTargetNode *Begin() const { return head != NULL ? head->next : NULL; }

    DAudioTargetList* Construct();                                                          // 0x00037640
    void Destruct();                                                                        // 0x00037660
    // erase(first, last): unlinks and frees each node (never the head), answers `last`.
    DAudioTargetNode** Erase(DAudioTargetNode **result, DAudioTargetNode *first, DAudioTargetNode *last);   // 0x00037160
    // _Incsize: throws length_error("list<T> too long") past 0x5d1745d nodes.
    void IncreaseSize(uint32_t count);                                                      // 0x000376a0
};
LAYOUT_32(sizeof(DAudioTargetList) == 12, "a list is 12 bytes");

// The sounds on screen, UpdateTargets' list
extern DAudioTargetList DAudioTargets;

// The mixer page's state (names ours)
extern int32_t DAudioMixerSelected;     // the selected row
extern int32_t DAudioMixerScrollTop;    // the first row the editing mode shows
extern bool DAudioMixerEditing;
extern bool DAudioMixerVisible;         // Draw draws the mixer page

class DAudio {
public:
    // The sounds on screen labelled "(distance)name".
    static void DrawTargets();                                                              // 0x00036c90
    static void MixerDraw();                                                                // 0x00036d10
    static void InfoDraw();                                                                 // 0x000371b0
    // Each page whose flag is set.
    static void Draw();                                                                     // 0x00037620
    // The list rebuilt: every sound with a range, heard by a view, on screen and in range of the listener.
    static void UpdateTargets();                                                            // 0x00037750
};

#endif // DRIVING_GAME_DEBUGAUDIO_H_
