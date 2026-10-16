#pragma once

// Shared pieces for the shadow tests of game objects (TriggerShadow.cpp, DoorShadow.cpp, SecurityShadow.cpp,
// ScoreShadow.cpp): a function under test runs twice, the original and ours, from the same state, and what each
// leaves in memory, calls and returns is compared.
//
// - Regions: the memory a test lets the function touch. Saved before the first run, put back before the second, and
//   compared byte for byte after both.
// - Hooks: the game functions the one under test calls that would need the game running (sounds, text, the heap,
//   rooms, the camera) are pointed at fakes that log their arguments and answer from settings the test makes. A hook
//   covers both the original's entry and the port it jumps to, so the original and ours reach the same fake.
// - The log: what the fakes were called with, in order, compared as text.

#include <stdint.h>
#include <string>
#include <vector>

#include "../actionhelpers.h"

class ShadowRegions {
public:
    void Clear() { regions.clear(); }
    void Add(void *at, size_t size) { regions.push_back({(uint8_t *)at, size}); }
    void Save();
    void Restore();
    void Capture(std::vector<uint8_t> &out) const;
    // The region and offset of the first byte two captures differ at; false if they do not
    bool Describe(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, int *region, size_t *offset) const;

private:
    struct Region {
        uint8_t *at;
        size_t size;
    };
    std::vector<Region> regions;
    std::vector<uint8_t> saved;
};

// Points 'at' (an original's entry) at fake, and the port behind it too when 'at' jumps to one.
void ShadowHook_Install(unsigned at, const void *fake);
void ShadowHook_RemoveAll(void);

void ShadowLog_Clear(void);
void ShadowLog(const char *format, ...);
const std::string &ShadowLog_Text(void);

// A pseudo-random sequence, the same on every run
uint32_t ShadowRand(uint32_t n);
float ShadowRandFloat(float lo, float hi);

// Runs theirs, then ours, from the same state of the regions and an empty log; true when both leave the regions and
// the log the same and return the same value. Reports the first few differences under tag. The regions are left as
// ours left them.
bool ShadowCompare(ShadowRegions &regions, const char *tag, const char *what, int trial, uint32_t (*theirs)(void),
                   uint32_t (*ours)(void));

// The standard fakes (ShadowFakes_Install), and what they answer
struct ShadowFakeSettings {
    bool createFails;           // control_create_object returns NULL
    obj_tag *firstObject;       // control_first_object
    ushort inputAction;         // Input_Action
    float damage;               // Collide_GetDamageNObjects
    uint32_t time;              // psiGetTimeIn100ths
    uint32_t missionStatus;     // Mission_Status
};
extern ShadowFakeSettings ShadowFakes;

// control_create_object hands out objects from a pool of this many bytes, zeroed, with extraObjectData after them
#define SHADOW_CREATE_POOL 0x1000
extern uint8_t ShadowCreatePool[SHADOW_CREATE_POOL];

// Hooks: control_create_object, build_LinkToRoom, Spline_Interp (a made-up matrix turning with the frame),
// KeyFrame_Interp (the same), Sound_Play3D, Sound_PlayExt, Text_AddMsg, Txt_BindLabel, GameFlow_PushState,
// Music_Event, GS_PauseGame, ResetMap_LevelToLoad, Player_RamSave, LList_Add, Control_BuildWorldSph,
// Controls_StraddleTest, Control_NextLOD, Explode_Create, Camera_SetUpdator, Player_Enable, Player_Disable,
// HUD_Enable, Input_SetAction, Input_Action, Collide_GetDamageNObjects, MPDrone_MaybeIsDyingOrDead (true when the
// drone's tweakA is odd), control_first_object, psiGetTimeIn100ths, Mission_Status.
void ShadowFakes_Install(void);
// The fakes' own state, for a test's regions
void ShadowFakes_AddRegions(ShadowRegions &regions);
