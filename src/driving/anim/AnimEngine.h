#ifndef DRIVING_ANIM_ANIMENGINE_H_
#define DRIVING_ANIM_ANIMENGINE_H_

// ---------------------------------------------------------------------------------------------------------------
// RAnimEngine, the render side's animation of CARP instances: keyframed instances ('Cams' camera animations, animated
// scenery) driven as systems of a handle by stimuli.
//
// A Handle (RAnimEngine::Handle, made by Handle::Create from a list of instances) holds copies of the instances, of
// their proc-anim states, of their articles' effects, and one System per animation system id the articles name.
// An article's animations (CARP::AnimInfo) form a state machine: the animation for a (stimulus, state) pair plays
// on every instance of its system, and leaves the system in the animation's next state. A system that is playing
// is on the engine's active list; RAnimEngine::Update steps every active system once per new frame, firing the
// events of each animation frame it passes and dropping the systems that have ended.
//
// Times: a tick (the game's) is turned into 60ths of a second (FrameOf in AnimEngine.cpp); an instance's time in an
// animation is in 60ths, an animation's frames are (frameRate) per second, and the systems count four 60ths a frame.
// See AnimEngine.cpp; the proc-anim functions that place every instance (GetInstanceMatrix) are in ProcAnim.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/Carp.h"                // CARP::Instance, CARP::AnimInfo
#include "../data/CoordConvert.h"        // Coord3, Coord4
#include "../../helpers.h"               // FLOAT_AT

class RSceneObj;
struct ArticleEffect;                    // world/World.h
struct Handle;
struct ProcAnimState;                    // world/World.h
struct WorldArticle;                     // world/World.h

// ---- the animation data

// An instance's flags (CARP::Instance::flags) as the engine reads them; world/World.h's WorldInstanceFlags has the
// others (0x01 drawn by its scene object, 0x10 has a proc-anim state). An instance's article (WorldArticle) holds its
// animations and effects.
enum AnimInstanceFlags : uint8_t {
    kAnimInstanceMirrored = 0x80,        // drawn mirrored in x; its article's mirrored system and effects
};

// ---- the engine

// A tick's length: 1 / the video mode's rate
#define TickSeconds FLOAT_AT(0x001f2a48)

namespace RAnimEngine {

typedef ::Handle Handle;

// An animation system of a handle (64 bytes): the handle's instances whose articles name its id, and its state
struct System {
    static constexpr int kMaxInstances = 0x22;

    Handle *handle;                      // +0x00
    uint32_t startFrame;                 // +0x04 in 60ths, when the animation started
    uint32_t lastFrame;                  // +0x08 of the last Update
    uint16_t zones;                      // +0x0c the damage zones of its instances (Handle::ProcessStimuliZones)
    uint16_t length;                     // +0x0e the animation's length in 60ths: four per frame of the longest
    int8_t queue[4];                     // +0x10 stimuli waiting for the animation to end; -1 none
    uint8_t queuedState;                 // +0x14 the state the queue leads to
    uint8_t id;                          // +0x15
    uint8_t state;                       // +0x16
    uint8_t nextState;                   // +0x17 the playing animation's
    uint8_t looping;                     // +0x18
    uint8_t playing;                     // +0x19 the instances still playing; 0 when it is not
    uint8_t instanceCount;               // +0x1a
    uint8_t animInfoCount;               // +0x1b the fewest animations of its instances' articles
    uint8_t unknown1C;
    uint8_t started;                     // +0x1d Update has run since it started
    uint8_t instances[kMaxInstances];    // +0x1e the handle's instances, by index

    // The handle's constructor builds its systems in place (inlined there)
    void Construct(Handle *owner, uint8_t systemId);

    // Starts the animation for (stimulus, state) on every instance that has it, and puts the system on the active
    // list - or, while it plays (or the list is full), queues the stimulus: `mode` 0 drops it, 2 queues up to
    // four, any other one
    void ProcessStimuli(uint8_t stimulus, uint32_t frame, int mode);                            // 0x00077720
    // Sets the time of the animation for (stimulus, state) on each instance, at most its length
    void SetFrame(uint8_t stimulus, uint32_t time);                                             // 0x000778f0
    void SetFrameRate(uint8_t stimulus, uint8_t frameRate);                                     // 0x000779c0
    // The longest of the animations for (stimulus, state), in 60ths
    uint32_t NumFrames(uint8_t stimulus);                                                       // 0x00077a50
    // Steps the system, the active list's `index`th, to `frame`: the instances' times, the events of the frames
    // passed, the end. The index of the system the caller's loop goes on after.
    uint32_t Update(uint32_t index, uint32_t frame);                                            // 0x00077ed0
};
static_assert(sizeof(System) == 0x40, "a system is 64 bytes");
static_assert(offsetof(System, zones) == 0x0c && offsetof(System, queue) == 0x10 &&
              offsetof(System, state) == 0x16 && offsetof(System, playing) == 0x19 &&
              offsetof(System, instances) == 0x1e, "System layout");

// Steps every active system, once per frame (`tick` the game's)
void Update(uint32_t tick);                                                                     // 0x00078190
// The instance's matrix: its animation's at its time (between two frames), or its own when it has none
void EvaluateInstance(CARP::Instance *instance, MATRIX4 *out);                                  // 0x00076a60

// DeactivateSystem (register arguments: EDI the active list's index, ECX the frame, the flag on the stack; EAX the
// index - 1): the adaptor over DeactivateActiveSystem
void DeactivateSystem();                                                                        // 0x00077b60

} // namespace RAnimEngine

// Takes the active list's `index`th system off the list, leaving it in its next state; with `next`, starts its first
// queued stimulus. The index before it.
uint32_t DeactivateActiveSystem(uint32_t index, uint32_t frame, bool next);

// ---- RAnimEngine::Handle (a header of 0x40 bytes, then its data, one allocation)

struct Handle {
    // The stimulus Create sends
    enum Stimulus : uint8_t {
        kStimulusStart = 1,              // what Create sends every system
    };

    uint32_t dataSize;                   // +0x00 the bytes after the header
    RSceneObj *sceneObj;                 // +0x04 its owner, NULL for a camera's
    ProcAnimState *states;               // +0x08 copies of the instances' proc-anim states (32 bytes each)
    RAnimEngine::System *systems;        // +0x0c
    uint8_t *systemIds;                  // +0x10 the systems' ids, ascending
    ArticleEffect *effects;                 // +0x14 copies of the instances' articles' effects
    uint16_t *effectBits;                // +0x18 one per effect (SetEffectBits); the effects' ids follow
    uint8_t instanceCount;               // +0x1c
    uint8_t stateCount;                  // +0x1d
    uint8_t systemCount;                 // +0x1e
    uint8_t effectCount;                 // +0x1f
    uint64_t unknown20;                  // +0x20 0
    uint64_t effectMask28;               // +0x28 the effects with ArticleEffect::kFlag08, by index
    uint64_t effectMask30;               // +0x30 ... of type kType6
    uint64_t effectMask38;               // +0x38 ... with any of kMask38Flags
    // +0x40: CARP::Instance instances[instanceCount], effects, states, systems, effectBits, effect ids, system ids

    CARP::Instance *Instances() { return reinterpret_cast<CARP::Instance *>(this + 1); }
    uint8_t *EffectIds() { return reinterpret_cast<uint8_t *>(effectBits + effectCount); }

    // A handle for `count` instances (from `states` their proc-anim states, if any), at `tick`; every system sent
    // kStimulusStart. `sceneObj` is the owner (its damage zones mark the systems'), or NULL.
    static Handle* Create(uint32_t count, uint32_t tick, CARP::Instance *instances, ProcAnimState *states,
                          RSceneObj *sceneObj);                                                 // 0x00078210
    // The constructor (Ghidra: FUN_00076fe0): copies the instances, their states and their articles' effects
    // (those kept), and builds the systems of `systemIds`
    Handle* Construct(CARP::Instance *instances, ProcAnimState *states, uint32_t size, uint32_t count,
                      uint8_t stateCountIn, uint8_t effectCountIn, uint8_t systemCountIn,
                      const uint8_t *systemIdsIn, RSceneObj *owner);                            // 0x00076fe0
    // operator delete: frees the header and its data (`size`, the header's)
    static void OperatorDelete(Handle *handle, uint32_t size);                                  // 0x00076810

    void SetEffectBits(uint32_t index, uint16_t bits);                                          // 0x00076830
    ArticleEffect* FindEffectByID(uint32_t id);    // NULL if none                                  // 0x00076850
    void InitAllSystemStates(uint8_t state);                                                    // 0x00076890
    bool AnySystemPlaying();                                                                    // 0x000768c0
    // The system id of the instance's article (its animations' first), 0 if none; zero-extended, callers keep EAX
    uint32_t GetInstanceSystemID(uint32_t index);                                               // 0x00076900
    bool IsSystemPlaying(uint32_t id);                                                          // 0x000774a0
    uint32_t GetSystemState(uint32_t id);       // 0 for no such system                         // 0x00077500
    // The system's first instance; -1 for no such system
    int32_t GetFirstSystemInstanceIndex(uint32_t id);                                           // 0x00077560
    // ... its largest in x not drawn by a scene object (the first if none)
    int32_t GetBestSystemInstanceIndex(uint32_t id);                                            // 0x000775c0
    CARP::Instance* GetFirstSystemInstance(uint32_t id);                                        // 0x000776a0
    // Takes every system of the handle off the active list
    void Stop();                                                                                // 0x00077be0
    void StopThunk();                   // a second entry to Stop (a jump to it)                 // 0x00078200
    void SetFrame(uint32_t id, uint8_t stimulus, uint32_t time);                                // 0x00077c50
    void SetFrameRate(uint32_t id, uint8_t stimulus, uint8_t frameRate);                        // 0x00077cb0
    uint32_t NumFrames(uint32_t id, uint8_t stimulus);  // 0 for no such system                 // 0x00077d10
    // Sends a stimulus to one system, every system, or the systems in any of `zones` (System::ProcessStimuli)
    void ProcessStimuli(uint32_t id, uint8_t stimulus, uint32_t tick, int mode);                // 0x00077d70
    void ProcessStimuli(uint8_t stimulus, uint32_t tick, int mode);                             // 0x00077e00
    void ProcessStimuliZones(uint8_t stimulus, uint16_t zones, uint32_t tick, int mode);        // 0x00077e60

private:
    // The system of the id, or NULL (inlined in the game; PS2: Handle::FindSystem)
    RAnimEngine::System *FindSystem(uint32_t id);
};
static_assert(sizeof(Handle) == 0x40, "a handle's header is 64 bytes");
static_assert(offsetof(Handle, systems) == 0x0c && offsetof(Handle, effectBits) == 0x18 &&
              offsetof(Handle, instanceCount) == 0x1c && offsetof(Handle, effectMask28) == 0x28 &&
              offsetof(Handle, effectMask38) == 0x38, "Handle layout");

// ---- the standard library's sort and lower_bound, as compiled for the handle's system ids (Dinkumware's;
// Handle::Create sorts them)

namespace SystemIdSort {

struct Range {
    uint8_t *first;
    uint8_t *second;
};

void PushHeap(uint8_t *first, int hole, int top, uint8_t value);                                // 0x00076930
// The two pointers are the distance and value type tags, unused
void Rotate(uint8_t *first, uint8_t *mid, uint8_t *last, int *, uint8_t *);                     // 0x00076980
void AdjustHeap(uint8_t *first, int hole, int bottom, uint8_t value);                           // 0x00076bc0
uint8_t* LowerBound(uint8_t *first, uint8_t *last, const uint8_t *value, int *);                // 0x00076c20
void Median(uint8_t *first, uint8_t *mid, uint8_t *last);                                       // 0x00076cd0
void MakeHeap(uint8_t *first, uint8_t *last, int *, uint8_t *);                                 // 0x00076e00
Range* UnguardedPartition(Range *result, uint8_t *first, uint8_t *last);                        // 0x00076e40
void InsertionSort(uint8_t *first, uint8_t *last);                                              // 0x00076f70
void SortHeap(uint8_t *first, uint8_t *last);                                                   // 0x00077b20
void Sort(uint8_t *first, uint8_t *last, int ideal);                                            // 0x000780d0

} // namespace SystemIdSort

// std::lower_bound over an article's animations, by stimulus then state
CARP::AnimInfo* AnimInfoLowerBound(CARP::AnimInfo *first, CARP::AnimInfo *last, const CARP::AnimInfo *value,
                                   int *);                                                      // 0x00076c60
// FUN_000776d0 (register arguments: EAX the count of animations, EDX the article): the adaptor over FindAnimInfo
void FUN_000776d0();                                                                            // 0x000776d0
// The article's animation for (stimulus, state) among its first `count`, or NULL
CARP::AnimInfo* FindAnimInfo(const WorldArticle *article, uint32_t count, uint8_t state, uint8_t stimulus);

#endif // DRIVING_ANIM_ANIMENGINE_H_
