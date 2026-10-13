#ifndef DRIVING_AUDIO_SOUND_H_
#define DRIVING_AUDIO_SOUND_H_

// ---------------------------------------------------------------------------------------------------------------
// The audio framework's sounds (engine.audio): ABaseSound, the base of every sound the game plays, and the kinds
// whose code is here - AOneShotSound and ALimitedSound (their destructors), ABasic (a bank patch by name),
// AMenuSoundPriv (a sound AMenuSound::Trigger makes and forgets: it deletes itself when its voice is done) - and
// AWorldSound, the world's (WSound's base, world/SoundGroup.h). ASound is ZoomObj.h's, AStream Stream.h's.
//
// Every kind but ALimitedSound (an AOneShotSound) derives from ABaseSound directly: its constructors call
// ABaseSound's, and its destructor writes its own vtable and calls ABaseSound's - none passes through another
// kind's. ASound, AOneShotSound, AMenuSoundPriv and AWorldSound each have an AVoice at +0xc0; the GetName in slot 2
// of ASound's, AOneShotSound's, ALimitedSound's and AWorldSound's vtables is one copy of the same code (0x00127bd0,
// ASound::GetName).
//
// Every sound comes from ABaseSound::operator new, which also appends it to the sound manager's list (fgSoundList,
// SoundManager.h); operator delete takes it out again. Once per audio frame ASoundManager::BuildPaths works out
// how each listener hears each sound and calls the sound's virtual Play. A sound is heard by a set of views (the
// bits of `views`), one per listener. See Sound.cpp.
//
// The vtables stay the game's; the virtual methods are called through them (CallDelete, CallIsTransient,
// CallPlay below).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "Mix.h"                        // AMix, AListener
#include "Voice.h"                      // AVoice
#include "../data/CoordConvert.h"       // Coord3
#include "../world/Targeting.h"         // PointerList
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"

// The listeners a sound can be heard by
enum SoundView {
    kSoundViewCount = 3,
    kSoundViewShared = 2,       // the view AMenuSound::Trigger uses for every sound while it is active
    kSoundViewsActive = 4,      // ABaseSound's constructor: the views in fgActiveViews, not one view
};

class ABaseSound;

// What a sound's virtual Play is given (0x1c bytes; the name is ours). ASoundManager::BuildPaths fills it for a
// listener (Compute); Stop and Pause hand over a silent one.
struct ASoundPlayParams {
    float volume;               // +0x00 scales the sound's and its mix's
    float pitch;                // +0x04 a multiplier
    float azimuth;              // +0x08
    float facing;               // +0x0c the sound's forward vector along the line from the listener (name ours)
    float unknown10;            // +0x10 zero within 25 of the listener, a quarter of a hundredth per unit beyond
    float distance;             // +0x14 from the listener, once out of the band (SoundPlayParams::Compute)
    AListener *listener;        // +0x18 the listener it is heard by

    // Volume, pitch (Doppler), azimuth and the rest for `sound` as `listener` hears it (Ghidra: FUN_0012eab0;
    // game/VehicleSoundEngine.cpp)
    ASoundPlayParams* Compute(ABaseSound *sound);                               // 0x0012eab0
};
static_assert(sizeof(ASoundPlayParams) == 0x1c, "a sound's play parameters are 0x1c bytes");

// The base sound (0xc0 bytes; vtable 0x0018a510). Field names from Ghidra's structure where it has them.
class ABaseSound {
public:
    uint32_t vtable;            // +0x00
    uint8_t unknown04[0xc];
    Coord3 position;            // +0x10
    uint32_t unknown1c;
    Coord3 velocity;            // +0x20
    uint32_t unknown2c;
    Coord3 forward;             // +0x30 (0, 0, 1) from the constructor, as are right and up
    uint32_t unknown3c;
    Coord3 right;               // +0x40
    uint32_t unknown4c;
    Coord3 up;                  // +0x50
    uint32_t unknown5c;
    Coord3 cameraAim;           // +0x60
    float minDistance;          // +0x6c
    float maxDistance;          // +0x70
    float maxDistanceSq;        // +0x74 zero: heard at any distance
    float falloff;              // +0x78
    float volume;               // +0x7c
    float pitch;                // +0x80
    float lastVolumes[kSoundViewCount];     // +0x84 the volume each view last played it at (name ours)
    bool paused;                // +0x90 ASoundManager::Pause sets it, Resume clears it; BuildPaths skips it
    uint8_t unknown91[3];
    AMix *mix;                  // +0x94
    uint32_t views;             // +0x98 a bit per view (SoundView) that hears the sound
    char name[0x10];            // +0x9c operator new's label less its first character
    float fade;                 // +0xac
    int32_t fadeStep;           // +0xb0
    int32_t fadeSteps;          // +0xb4
    bool fading;                // +0xb8
    uint8_t unknownB9[7];

    // A sound in the mix `mixName` (made if new), heard by `view` (or kSoundViewsActive).
    ABaseSound* Construct(const char *mixName, int view);                       // 0x0001bfa0
    // Gives up the mix.
    void Destruct();                                                            // 0x0011c6f0
    ABaseSound* Delete(unsigned int flags);                                     // 0x0001c090

    char* GetName();                                                            // 0x0011c710
    // The volume times the mix's (name ours; Ghidra: FUN_0011dab0). Unrounded, as the x87 leaves it.
    double GetMixedVolume();                                                    // 0x0011dab0
    // A fade in from zero over `steps` calls to GetFade.
    void StartFade(int steps);                                                  // 0x0011c720
    // The fade's next step (1 once it is over, or without one).
    float GetFade();                                                            // 0x0011c750
    // Out of its mix and into `mixName`'s, made if new (compiled among GFX's code; the name ours)
    void SetMix(const char *mixName);                                           // 0x000d3690

    // The pool block for a sound of `size` bytes, labelled `name` and named by it less its
    // first character, appended to fgSoundList.
    static void* OperatorNew(unsigned int size, const char *name);              // 0x0011c910
    // Out of fgSoundList, and back to the pools.
    static void OperatorDelete(void *block, unsigned int size);                 // 0x0011c830

    // The virtual methods, through the sound's vtable. Slot 0: the scalar deleting destructor. Slot 1: whether
    // ASoundManager::Restart deletes the sound (false for the base, true for the one-shot kinds; the name is
    // ours). Slot 2: GetName. Slot 3: Play (pure in the base).
    ABaseSound* CallDelete(unsigned int flags) {
        return (this->*XbeVirtual<DeleteMethod>(this, 0))(flags);
    }
    bool CallIsTransient() {
        return (this->*XbeVirtual<IsTransientMethod>(this, 1))();
    }
    void CallPlay(ASoundPlayParams *params) {
        (this->*XbeVirtual<PlayMethod>(this, 3))(params);
    }

private:
    typedef ABaseSound* (ABaseSound::*DeleteMethod)(unsigned int flags);
    typedef bool (ABaseSound::*IsTransientMethod)();
    typedef void (ABaseSound::*PlayMethod)(ASoundPlayParams *params);
};
static_assert(offsetof(ABaseSound, minDistance) == 0x6c, "ABaseSound::minDistance");
static_assert(offsetof(ABaseSound, lastVolumes) == 0x84, "ABaseSound::lastVolumes");
static_assert(offsetof(ABaseSound, mix) == 0x94, "ABaseSound::mix");
static_assert(offsetof(ABaseSound, name) == 0x9c, "ABaseSound::name");
static_assert(offsetof(ABaseSound, fading) == 0xb8, "ABaseSound::fading");
static_assert(sizeof(ABaseSound) == 0xc0, "ABaseSound is 192 bytes");

// The base's slot 1, and the same answer in many unrelated vtables (the linker kept one copy).
bool Generic_FuncReturnsFalse();                                                // 0x0001c080

// The ALimitedSounds in existence: their constructor counts up, their destructor down (name ours).
extern int32_t LimitedSoundCount;

// ---- the sound manager's list of every sound: std::list<ABaseSound *>, the game's list of pointers (its remove,
// PointerList::Remove, is shared by every such list)

struct ASoundList : PointerList {
    // _Incsize: throws length_error("list<T> too long") past 0x3fffffff nodes.
    void IncreaseSize(uint32_t count);                                          // 0x0011c860
};
static_assert(sizeof(ASoundList) == 12, "a list is 12 bytes");

// ---- the kinds

// A sound with a voice of its own that deletes itself (0x120 bytes; vtable 0x0018c840). Its GetName is ASound's
// code.
class AOneShotSound : public ABaseSound {
public:
    AVoice voice;               // +0xc0
    float fxLevel;              // +0x118 the effects level it plays at (name ours)
    float timeLeft;             // +0x11c seconds to live; negative: no limit (name ours)

    // The bank's patch in the mix, at `volume`, from `position` with `velocity`; every view plays once (Ghidra:
    // FUN_000476f0)
    AOneShotSound* Construct(int bank, int patch, const Coord3 *position, const Coord3 *velocity, float volume,
                             const char *mixName);                              // 0x000476f0
    // The same without a velocity; every view plays once unless `loop` (Ghidra: FUN_000477d0)
    AOneShotSound* ConstructAt(int bank, int patch, const Coord3 *position, float volume, const char *mixName,
                               bool loop);                                      // 0x000477d0
    void Destruct();                                                            // 0x000478a0
    AOneShotSound* Delete(unsigned int flags);  // Ghidra: FUN_00047900          0x00047900

    // As ASound's Play, with the effects level; then, while the time is not negative, the sound deletes itself
    // when its voice is done, or counts its time down and deletes itself when that runs out (vtable slot 3; in
    // ZoomObj.cpp). (Ghidra: FUN_00127cb0)
    void Play(ASoundPlayParams *params);                                        // 0x00127cb0
};
static_assert(offsetof(AOneShotSound, fxLevel) == 0x118, "AOneShotSound::fxLevel");
static_assert(sizeof(AOneShotSound) == 0x120, "AOneShotSound is 0x120 bytes");

// A one-shot sound counted while it exists (vtable 0x0018c8c8)
class ALimitedSound : public AOneShotSound {
public:
    void Destruct();                                                            // 0x0004de50
    ALimitedSound* Delete(unsigned int flags);                                  // 0x0004de20

    // The volume becomes one over the number of limited sounds, then AOneShotSound's Play (in ZoomObj.cpp).
    void Play(ASoundPlayParams *params);                                        // 0x00127da0
};
static_assert(sizeof(ALimitedSound) == 0x120, "ALimitedSound is 0x120 bytes");

// The world's sound (0x150 bytes; vtable 0x00193ae8; WSound's base): its voice played in bursts of repeats with
// random waits between, and a pass voice heard while it comes close. Construct and Delete are in
// world/SoundGroup.cpp, the destructor and Play in game/WeaponSound.cpp. (Field names ours, from what Play does.)
class AWorldSound : public ABaseSound {
public:
    AVoice voice;               // +0xc0
    float minDelay;             // +0x118 the random wait between bursts: from minDelay to maxDelay (from zero to
    float maxDelay;             // +0x11c maxDelay the first time); zero from the constructor, as are the next three
    float minRepeats;           // +0x120 the random number of plays after a wait
    float maxRepeats;           // +0x124
    int32_t mode;               // +0x128 1: neither the listener's pitch nor its azimuth
    uint8_t inUse;              // +0x12c WSoundGroup::Add sets it, Start clears it, End deletes the sound if it is
                                //        still clear
    uint8_t unknown12d[3];
    float nextTime;             // +0x130 the audio clock's time the wait ends; zero: never waited
    float repeats;              // +0x134 plays left before the next wait
    uint8_t waiting;            // +0x138 one: the voice is done; wait, or play it again
    uint8_t unknown139[3];
    AVoice *passVoice;          // +0x13c NULL from the constructor; WSound::SetVoice's, played while something
                                //        passes close
    int32_t passFrames;         // +0x140 180 when the listener is within 6 of where the sound will be over the
                                //        next five velocity steps; counted down
    uint8_t unknown144[0xc];

    // A sound named `name` playing the bank's sound `index`.
    AWorldSound* Construct(int bank, int index, const char *name);              // 0x000cc930
    void Destruct();                                                            // 0x0012e090
    AWorldSound* Delete(unsigned flags);                                        // 0x000cca00
    // vtable slot 3
    void Play(ASoundPlayParams *params);                                        // 0x0012dda0
};
static_assert(sizeof(AWorldSound) == 0x150, "AWorldSound is 336 bytes");

// The sound AMenuSound::Trigger makes (0x120 bytes; vtable 0x001a250c): heard at any distance, deleted by its
// own Play once its voice is done.
class AMenuSoundPriv : public ABaseSound {
public:
    AVoice voice;               // +0xc0
    uint8_t unknown118[8];

    AMenuSoundPriv* Construct(int bank, int patch, const char *mixName, int view);   // 0x0011de40
    void Destruct();                                                            // 0x0011e090
    AMenuSoundPriv* Delete(unsigned int flags);                                 // 0x0011e060

    void Play(ASoundPlayParams *params);                                        // 0x0011dee0
};
static_assert(sizeof(AMenuSoundPriv) == 0x120, "AMenuSoundPriv is 0x120 bytes");

class AMenuSound {
public:
    // A sound of the bank's patch in the mix `mixName` for `view` (the shared view while it is active), left to
    // play once and delete itself.
    static void Trigger(int bank, int patch, const char *mixName, int view);    // 0x0011df50
    // The same by the bank's name and the patch's name; nothing if the bank has no such patch.
    static void Trigger(const char *bankName, const char *patchName, const char *mixName, int view);   // 0x0011dfd0
    // The same by the bank's number (its slot in the sound manager's banks).
    static void Trigger(int bankNumber, const char *patchName, const char *mixName, int view);         // 0x0011e010
};

// A patch of the first bank by name (0xf0 bytes; vtable 0x001a3060), heard by the active views. The voice is made
// at the first Play and freed by Stop.
class ABasic : public ABaseSound {
public:
    AVoice *voice;              // +0xc0
    char patchName[0x2c];       // +0xc4

    ABasic* Construct(const char *name, const char *mixName);                   // 0x0012fa40
    void Destruct();                                                            // 0x0012fb80
    ABasic* Delete(unsigned int flags);                                         // 0x0012fc00

    void Play(ASoundPlayParams *params);                                        // 0x0012fa90
    // Frees the voice (vtable slot 4; the parameters are not read).
    void Stop(ASoundPlayParams *params);                                        // 0x0012fc30
};
static_assert(sizeof(ABasic) == 0xf0, "ABasic is 0xf0 bytes");

// ---- the warning beside a provisional port: code no shipped data reaches, said once, the first time it runs

inline void AudioSoundUntested(const char *what) {
    printf("[audio] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check it against "
           "the original.\n", what);
    fflush(stdout);
}

#define AUDIO_SOUND_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            AudioSoundUntested(what); \
        } \
    } while (0)

#endif // DRIVING_AUDIO_SOUND_H_
