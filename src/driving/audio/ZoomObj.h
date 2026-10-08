#ifndef DRIVING_AUDIO_ZOOMOBJ_H_
#define DRIVING_AUDIO_ZOOMOBJ_H_

// ---------------------------------------------------------------------------------------------------------------
// The code the linker placed after AIndex's (0x00127a70..0x00128350):
//
//   - ASound: a sound with a voice of its own (a bank patch), played as each listener hears it. Its GetName is
//     the vtable slot of AOneShotSound, ALimitedSound and the world's sounds too (one copy of the same code);
//   - the Play of AOneShotSound and of ALimitedSound (Sound.h): the voice played with the sound's effects level,
//     and the sound deleted once its voice is done or its time runs out; a limited sound plays at one over the
//     number of limited sounds;
//   - AZoomObj: an AVehicle's breath voice, and InTheZone, which moves most mixes down (or back to their presets)
//     over ten audio frames.
//
// See ZoomObj.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Mix.h"                        // AMix
#include "Sound.h"                      // ABaseSound, ASoundPlayParams
#include "Voice.h"                      // AVoice

// ---- ASound (0x120 bytes; vtable 0x001a2a7c)

class ASound : public ABaseSound {
public:
    AVoice voice;               // +0xc0
    uint8_t unknown118[8];

    // A sound of the bank's patch in the mix `mixName`, heard by the active views, at full volume.
    ASound* Construct(int bank, int patch, const char *mixName);                    // 0x00127a70
    // A sound with no patch in the mix `mixName`, silent. (Ghidra: FUN_00127bf0)
    ASound* Construct(const char *mixName);                                         // 0x00127bf0
    void Destruct();                                    // Ghidra: FUN_00127b70      0x00127b70
    ASound* Delete(unsigned int flags);                 // Ghidra: FUN_00127c80      0x00127c80

    // The name of the first view's patch (vtable slot 2). (Ghidra: FUN_00127bd0)
    const char* GetName();                                                          // 0x00127bd0
    // Plays the voice's view for the listener at the sound's volume times its mix's (vtable slot 3).
    void Play(ASoundPlayParams *params);                                            // 0x00127b00
};
static_assert(offsetof(ASound, voice) == 0xc0, "ASound::voice");
static_assert(sizeof(ASound) == 0x120, "ASound is 0x120 bytes");

// ---- AZoomObj (0x10 bytes, from UMemory::FastAlloc "AZoomObj")

class AVehicle;

class AZoomObj {
public:
    AMix *mix;                  // +0x00 "Weapons Init"
    AVoice *voice;              // +0x04 bank slot 6's "SFX_breath"
    AVehicle *vehicle;          // +0x08 the vehicle that made it (AVehicle::ActivateZoom)
    bool inZone;                // +0x0c
    uint8_t unknown0d[3];

    AZoomObj* Construct(AVehicle *vehicle);                                         // 0x00127dc0
    void Destruct();                                                                // 0x00128330

    // The breath voice for the listener: at the mix's volume while in the zone, silent otherwise. Called from
    // AVehicle::Play (name ours; Ghidra: FUN_00127e50).
    void Play(ASoundPlayParams *params);                                            // 0x00127e50
    // Entering: every mix moves to its zone volume over ten audio frames; leaving: AMix::Reset over ten.
    void InTheZone(bool entering);                                                  // 0x00127ed0
};
static_assert(sizeof(AZoomObj) == 0x10, "AZoomObj is 16 bytes");

#endif // DRIVING_AUDIO_ZOOMOBJ_H_
