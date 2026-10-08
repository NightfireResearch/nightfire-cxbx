#ifndef DRIVING_AUDIO_SOUNDMANAGER_H_
#define DRIVING_AUDIO_SOUNDMANAGER_H_

// ---------------------------------------------------------------------------------------------------------------
// ASoundManager: the game's sound manager (all static) - Init brings the audio framework up (the mixes and their
// masters, the effects, the sound library through ASystem, the music and speech streams, the banks banks.ini
// names), BuildPaths plays every sound for a listener once per audio frame, Stop, Pause and Resume, Restart and
// ClearMission between missions, Shutdown. ASystem starts the sound library over a heap of its own and sets its
// options. See SoundManager.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Bank.h"                       // ABank, fgBanks
#include "Mix.h"                        // AListener
#include "Sound.h"
#include "../data/CoordConvert.h"       // Coord3
#include "../world/Targeting.h"         // PointerList
#include "../../helpers.h"

class ASystem;

// The sound manager's state shared with the sounds, the mixes, the faders and the streams (names ours, but
// ASoundManager_fgIsPaused and ASystem_fgSystem)
#define fgSoundList (*(ASoundList **)0x00243a58)    // every sound ("ASoundList")
#define fgActiveViews U32_AT(0x00243940)            // the views kSoundViewsActive stands for
#define ASoundManager_fgIsPaused BOOL8_AT(0x002439d0)
#define ASoundManager_fgMissionOver BOOL8_AT(0x00243a77)    // set by SetMissionOver, cleared by Restart
#define FramesSinceAudioUpdate I32_AT(0x00243a68)   // the simulation's frames since the last audio update
#define MusicVolumeScale FLOAT_AT(0x001d8064)       // the options' volumes
#define EffectsVolumeScale FLOAT_AT(0x001d8068)
#define ASystem_fgSystem (*(ASystem **)0x00243b34)  // the one ASystem, or NULL

// The streams that failed to play: std::list<char *> ("FailedStreams")
struct StreamNameList : PointerList {
    // _Incsize: this list's compiled copy of PointerList's.
    void IncreaseSize(uint32_t count);                                          // 0x00121d90
};
static_assert(sizeof(StreamNameList) == 12, "a list is 12 bytes");

class ASoundManager {
public:
    // Brings the audio up: `directory` (if any) is where banks.ini is found, the mixes come from data/audio/
    // <section>.ini and the banks from banks.ini's [section] (or [default]), `streamFile` names the speech stream
    // and, less its last two characters, the music stream. Without `loadBanks` neither the sound library nor
    // the banks are started.
    static void Init(const char *directory, bool loadBanks, const char *streamFile, const char *section,
                     int outputMode);                                           // 0x00121470
    static void Shutdown();                                                     // 0x00121d00

    // Once per audio frame for each listener: every sound not paused played as the listener hears it (or
    // silenced, if the listener heard it before and no longer does).
    static void BuildPaths(AListener *listener);                                // 0x00120e60
    // Every sound a view still hears played silent, then the voices updated.
    static void Stop();                                                         // 0x00120f90
    static void Pause();                                                        // 0x00121070
    static void Resume();                                                       // 0x00121150
    // Deletes every sound at `position` exactly.
    static void StopSoundPos(const Coord3 *position);                           // 0x001211d0
    // The streams stopped, the sounds silenced, the engines removed and every bank removed.
    static void ClearMission();                                                 // 0x00121280
    // For the mission's restart: transient sounds deleted, the others unpaused, mixes and faders back.
    static void Restart();                                                      // 0x00121320
    static void SetMissionOver();                                               // 0x00120d80
    // Appends the stream's name to the failed streams.
    static void ReportFailure(const char *name);                                // 0x00121e40

    // A normally distributed random number (mean 0, deviation 1): the polar method, two at a time.
    static double NormalizedRandomNumber();                                     // 0x00120db0
};

// The sound library's owner (8 bytes, "ASystem" from the pools; one, fgSystem)
class ASystem {
public:
    void *heap;                 // +0x00 the sound library's memory ("Audio Heap")
    uint32_t unknown04;

    ASystem* Construct();                                                       // 0x00128350

    // Makes the one ASystem if there is none.
    static void Init();                                                         // 0x00128390
    // The sound library's two render modes; `outputMode` 0 or 1 sets the library's output mode to 1 or 2.
    static void SetOpts(int outputMode);                                        // 0x001283f0
    // The library restored, its heap freed, the ASystem deleted.
    static void Shutdown();                                                     // 0x00128470
};
static_assert(sizeof(ASystem) == 8, "ASystem is 8 bytes");

#endif // DRIVING_AUDIO_SOUNDMANAGER_H_
