#ifndef DRIVING_AUDIO_VOICE_H_
#define DRIVING_AUDIO_VOICE_H_

// ---------------------------------------------------------------------------------------------------------------
// AVoice: a sound's voice on the SND layer - the mix it belongs to and three views, each a bank patch with the
// volume, pitch, azimuth, start delay and effects level it plays at. AVoice::Play sets a view's parameters; a view
// played with a volume goes into the list of active views, and AVoice::PlayVoices, once per audio frame, starts,
// updates or retires each active view's SND voice. AVoice::BuildMap counts the active views by patch name for the
// audio debug display (DAudio::InfoDraw). See Voice.cpp.
//
// The list (std::list<AVoice::View *>) and the debug map (std::map<const char *, AVoiceMapInfo>) are the game's
// compiled STL: the list is world/Targeting.h's PointerList, the map an RbTree with 0x20-byte nodes.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../engine/RbTree.h"
#include "../world/Targeting.h"         // PointerList

class AMix;
struct AVoiceMap;

class AVoice {
public:
    // One of the voice's views (0x1c bytes).
    class View {
    public:
        int32_t patch;          // +0x00 -1: none
        int32_t bank;           // +0x04 the SND bank handle (ABank::handle); -1: none
        uint8_t loop;           // +0x08 (name ours) set: a voice that is over is started again; clear: the view
                                //       is marked finished
        uint8_t unknown09[3];
        int32_t handle;         // +0x0c the SND voice; -1: none
        uint16_t pitch;         // +0x10 SNDpitchmult's multiplier (Play's pitch x 4096)
        uint16_t azimuth;       // +0x12 SND3dpos's azimuth (Play's azimuth x 65536)
        int16_t delay;          // +0x14 timer ticks before the voice starts; Play sets it only while negative
        int8_t volume;          // +0x16 0..127
        int8_t listedVolume;    // +0x17 (name ours) the volume it went into the active list with: non-zero while
                                //       it is listed
        int8_t fxLevel;         // +0x18 SNDfxlevel's level, 0..127
        uint8_t finished;       // +0x19 (name ours)
        uint8_t unknown1a[2];

        // No patch, no voice, pitch 1.0, delay -1, loop set.
        View* Construct();                                                          // 0x00123820
        // Out of the active list, and the voice stopped.
        void Destruct();                                                            // 0x00123c50
        // Silent and out of the active list; the voice stopped but its handle kept.
        void Stop();                                                                // 0x00123be0
        // Appended to the active list.
        void Push();                                                                // 0x00124490
        // Plays at `volume` (0..1), `pitch` (a multiplier, its size up to 4), `azimuth`, after `delay` seconds
        // (when no delay is pending) and with `fxLevel` (0..1); a volume of zero or less stops it.
        void Play(float volume, float pitch, float azimuth, float delay, float fxLevel);   // 0x001244d0
    };

    AMix *mix;                  // +0x00
    View views[3];              // +0x04

    AVoice* Construct(AMix *mix, int bank, int patch);                              // 0x00123ca0
    // Every view on the bank's patch; a view that changes patch has its voice stopped. A patch of -1 only marks
    // the views finished.
    void Set(int bank, int patch);                                                  // 0x00123870
    // Plays the view at `volume` scaled by the mix's volume, counting it in the mix.
    void Play(int view, float volume, float pitch, float azimuth, float delay, float fxLevel);   // 0x00124690

    // Starts, updates or retires the SND voices of every active view (each audio frame).
    static void PlayVoices();                                                       // 0x00123a30
    // The debug map, rebuilt from the active views.
    static AVoiceMap* BuildMap();                                                   // 0x001243c0
};
static_assert(sizeof(AVoice::View) == 0x1c, "a voice's view is 28 bytes");
static_assert(offsetof(AVoice::View, handle) == 0x0c, "View::handle");
static_assert(offsetof(AVoice::View, delay) == 0x14, "View::delay");
static_assert(offsetof(AVoice::View, finished) == 0x19, "View::finished");
static_assert(sizeof(AVoice) == 0x58, "AVoice is 88 bytes");

// ---- the active views: std::list<AVoice::View *> (0x00243aa8)

struct AVoiceViewList : PointerList {
    // _Incsize: this list's compiled copy of PointerList's.
    void IncreaseSize(uint32_t count);                                              // 0x00123ec0
};
static_assert(sizeof(AVoiceViewList) == 12, "a list is 12 bytes");

// ---- the debug map: std::map<const char *, AVoiceMapInfo> (0x00243a9c), keyed by the patch name's address

// (names ours)
struct AVoiceMapInfo {
    int32_t count;              // the active views playing the patch
    int32_t volume;             // the sum of their volumes
    uint8_t fx;                 // one of them has an effects level
    uint8_t unknown09[3];
};

struct AVoiceMapValue {
    const char *name;           // the key (ABank::GetPatchName's answer, compared as a pointer)
    AVoiceMapInfo info;
};

struct AVoiceMapNode : RbTreeNode<AVoiceMapNode, AVoiceMapValue> {};
static_assert(sizeof(AVoiceMapNode) == 0x20, "a voice map node is 32 bytes");

struct AVoiceMapInsert {
    AVoiceMapNode *node;
    bool inserted;
};

// The map's compiled code. Lrotate and the iterator's -- serve every map with 0x20-byte nodes (the attribute
// system's, the collision manager's window map); the other rotation, ++ and the head's allocation are theirs.
struct AVoiceMap : RbTree<AVoiceMapNode> {
    struct Iterator {
        AVoiceMapNode *node;

        void Dec();                                                                 // 0x00123930
    };

    AVoiceMap* Construct();                                                         // 0x00124710
    void Destruct();                                                                // 0x00124750

    void Lrotate(AVoiceMapNode *node);                                              // 0x001238d0
    void EraseSubtree(AVoiceMapNode *node);                                         // 0x00123990
    AVoiceMapNode* Buynode(AVoiceMapNode *left, AVoiceMapNode *parent, AVoiceMapNode *right,
                           const AVoiceMapValue *value, uint8_t color);             // 0x001239d0
    AVoiceMapNode** InsertAt(AVoiceMapNode **result, bool addLeft, AVoiceMapNode *where,
                             const AVoiceMapValue *value);                          // 0x00123ce0
    AVoiceMapNode** EraseAt(AVoiceMapNode **result, AVoiceMapNode *where);          // 0x00123f70
    AVoiceMapInsert* InsertUnique(AVoiceMapInsert *result, const AVoiceMapValue *value);   // 0x00124240
    AVoiceMapNode** EraseRange(AVoiceMapNode **result, AVoiceMapNode *first, AVoiceMapNode *last);   // 0x00124300
};
static_assert(sizeof(AVoiceMap) == 12, "a map is 12 bytes");

// ---- the warning beside a provisional port: code no shipped data reaches (the containers' length and iterator
// errors), said once, the first time it runs

inline void AudioVoiceUntested(const char *what) {
    printf("[audio] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check it against "
           "the original.\n", what);
    fflush(stdout);
}

#define AUDIO_VOICE_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            AudioVoiceUntested(what); \
        } \
    } while (0)

#endif // DRIVING_AUDIO_VOICE_H_
