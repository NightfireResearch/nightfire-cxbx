#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAudioShadowC.h"
#include "FpControl.h"

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <initializer_list>
#include <vector>

#include "../audio/Bank.h"
#include "../audio/Mix.h"
#include "../audio/Sound.h"
#include "../audio/SoundManager.h"
#include "../game/CharacterSound.h"
#include "../game/FingoDeathSound.h"
#include "../game/VehicleSound.h"
#include "../game/WeaponSound.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.audio's package C, run once from the first simulation tick when NIGHTFIRE_GAMEAUDIOSHADOWC
// is set. Each case runs the ORIGINAL (every entry of the package swapped back together, so an original reaches
// the other originals) and then the PORT on the same inputs, and compares the bytes they leave (the object, the
// blocks allocated, the globals) and a log of every call they make out of the package:
//
//   - ASceneObj, ASentry, ASmackable, ACharacter, AFingoDeath: constructors and deleting destructors;
//   - AGun: constructor, destructor, SetFirePatch (short and long patch names), Play over random timers, sustain,
//     interval, the trigger byte (0, 1 and 2), frames, shells, IsTracked and the owner's gunLoops;
//   - ATurret: Play over every combination of the three flags and their previous states, voices present or not,
//     finished or not;
//   - ASentry::Play; ASmackable's PlayLanding, PlayCollision (impacts about 0.75, exactly 0.75, NaN), IsStillActive,
//     Play (roll frames, volumes, velocities, frames since the audio update), Detatch, GetName;
//   - ATargeting::SetState over every pair of states, paused or not;
//   - AWorldSound's Play (waiting, repeats, delays, modes, the audio clock, a pass voice and velocities that bring
//     it near the listener), its destructor, the voice's Rewind;
//   - ACharacter::Say for every kind of line (the counters at every position and the bank answering at random),
//     GetName, Play; MakeCoord3; AFingoDeath's Play and SetRechargePercent;
//   - copies of the live ASentry, ASmackable, ACharacter and world sounds in the sound list, and of every live
//     AVehicle's AGun and ATurret, played with perturbed inputs.
//
// Fakes: the mixes (Add, Get, Remove, GetVolume), AVoice (constructor, Play, its destructors, FUN_000d36c0),
// AIndex::Lookup, ABank::IsPatchPresent and GetPatchName, both AMenuSound::Trigger, both AOneShotSound
// constructors, rrandom, UMemory::FastAlloc/FastFree and ABaseSound's operator new/delete (blocks from a private
// arena, the same addresses for both runs), and AVehicle's constructor, destructor, Play, ActivateGun and
// ActivateTurret. The bank slots are private banks for the test. Nothing reaches a live object but reads.
//
// A mutation the test sees: AGun::Play testing `firing != 0` instead of `firing == 1` (the trigger byte 2 cases),
// or ASmackable::PlayCollision's `<= 0.75f` made `< 0.75f` (the exact 0.75 impacts).
//
// One summary line: [gameaudioshadowc] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

#define Orig_SceneConstruct ((ASceneObj *(__fastcall *)(void *, int, const char *))0x0012e190)
#define Orig_SceneDelete ((void *(__fastcall *)(void *, int, unsigned))0x0012e1f0)
#define Orig_ScenePlayCollision ((void (__fastcall *)(void *, int, float, const Coord3 *, const Coord3 *, int))0x00037a10)
#define Orig_GunConstruct ((AGun *(__fastcall *)(void *, int, ASceneObj *, const char *))0x00129550)
#define Orig_GunDestruct ((void (__fastcall *)(void *, int))0x00129470)
#define Orig_GunSetFirePatch ((void (__fastcall *)(void *, int, GameStd::String))0x001296b0)
#define Orig_GunPlay ((void (__fastcall *)(void *, int, uint8_t, ASoundPlayParams *, float))0x00129250)
#define Orig_TurretConstruct ((ATurret *(__fastcall *)(void *, int))0x0012e220)
#define Orig_TurretDestruct ((void (__fastcall *)(void *, int))0x0012e250)
#define Orig_TurretPlay ((void (__fastcall *)(void *, int, bool, bool, bool, ASoundPlayParams *))0x0012e2b0)
#define Orig_SentryConstruct ((ASentry *(__fastcall *)(void *, int))0x0012d7f0)
#define Orig_SentryDelete ((void *(__fastcall *)(void *, int, unsigned))0x0012d7c0)
#define Orig_SentryPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012d6f0)
#define Orig_SmackConstruct ((ASmackable *(__fastcall *)(void *, int, int, int, int, int, const char *))0x0012d8b0)
#define Orig_SmackLanding ((void (__fastcall *)(void *, int, float, const Coord3 *, const Coord3 *))0x0012d9a0)
#define Orig_SmackCollision ((void (__fastcall *)(void *, int, float, const Coord3 *, const Coord3 *, int))0x0012d9c0)
#define Orig_SmackIsStillActive ((bool (__fastcall *)(void *, int))0x0012da80)
#define Orig_SmackPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012da90)
#define Orig_SmackDetatch ((void (__fastcall *)(void *, int))0x0012dc50)
#define Orig_SmackGetName ((const char *(__fastcall *)(void *, int))0x0012dc80)
#define Orig_SmackDelete ((void *(__fastcall *)(void *, int, unsigned))0x0012dd50)
#define Orig_TargetConstruct ((ATargeting *(__fastcall *)(void *, int))0x0012e0f0)
#define Orig_TargetSetState ((void (__fastcall *)(void *, int, int))0x0012e120)
#define Orig_Rewind ((void (__fastcall *)(void *, int))0x0012dd80)
#define Orig_WorldDestruct ((void (__fastcall *)(void *, int))0x0012e090)
#define Orig_WorldPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012dda0)
#define Orig_CharConstruct ((ACharacter *(__fastcall *)(void *, int, const char *))0x0001c0c0)
#define Orig_CharDelete ((void *(__fastcall *)(void *, int, unsigned))0x0001c110)
#define Orig_CharSay ((void (__fastcall *)(void *, int, const char *))0x0011dad0)
#define Orig_CharGetName ((const char *(__fastcall *)(void *, int))0x0011dd00)
#define Orig_CharPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0011ddb0)
#define Orig_MakeCoord3 ((Coord3 *(*)(Coord3 *, float, float, float))0x0001c140)
#define Orig_FingoConstruct ((AFingoDeath *(__fastcall *)(void *, int))0x00129a30)
#define Orig_FingoDelete ((void *(__fastcall *)(void *, int, unsigned))0x00129a00)
#define Orig_FingoPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x00129830)
#define Orig_FingoSetRecharge ((void (__fastcall *)(void *, int, float))0x00129820)

// The package's entries, swapped back together for an original's run
const unsigned kEntries[] = {
    0x0001c0c0, 0x0001c110, 0x0001c140, 0x00037a10, 0x0011dad0, 0x0011dd00, 0x0011dd30, 0x0011ddb0, 0x00129250,
    0x00129470, 0x00129550, 0x001296b0, 0x001297a0, 0x00129820, 0x00129830, 0x00129a00, 0x00129a30, 0x0012d6f0,
    0x0012d750, 0x0012d7c0, 0x0012d7f0, 0x0012d8b0, 0x0012d9a0, 0x0012d9c0, 0x0012da80, 0x0012da90, 0x0012dc50,
    0x0012dc80, 0x0012dcb0, 0x0012dd50, 0x0012dd80, 0x0012dda0, 0x0012e090, 0x0012e0f0, 0x0012e120, 0x0012e190,
    0x0012e1e0, 0x0012e1f0, 0x0012e220, 0x0012e250, 0x0012e2b0,
};

// The globals the code reads and writes at the game's addresses
#define ShadowFrames I32_AT(0x00243a68)
#define ShadowClock FLOAT_AT(0x00243a6c)
#define ShadowPaused U8_AT(0x002439d0)
// The original's copies of the globals the port owns
#define OrigNextHardCollision I32_AT(0x00243b68)
#define OrigFPedWrapped U8_AT(0x00243960)
#define OrigNextFPed I32_AT(0x00243964)
#define OrigMPedWrapped U8_AT(0x00243968)
#define OrigNextMPed I32_AT(0x0024396c)
#define OrigNextGrunt I32_AT(0x00243970)
#define OrigNextScream I32_AT(0x00243974)
#define OrigNextDeath I32_AT(0x00243978)
#define OrigNextYell I32_AT(0x0024397c)
#define OrigNextFall I32_AT(0x00243980)

const uint32_t kAVehicleVtables[] = {
    0x001a2680, 0x001a2abc, 0x001a2b14, 0x001a2b60, 0x001a2bc4, 0x001a2c40, 0x001a2d44, 0x001a2e28, 0x001a2e58,
    0x001a2ed8,
};

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
bool g_dumped;      // the first differing case's words printed
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[gameaudioshadowc] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x2545f491;
uint32_t NextRandom() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}
int RandomInt(int lo, int hi) {
    return lo + int((NextRandom() >> 8) % uint32_t(hi - lo + 1));
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(NextRandom() >> 8) * (1.0f / 16777216.0f);
}
bool Chance(int percent) {
    return RandomInt(0, 99) < percent;
}
uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}
uint32_t Hash(const void *data, size_t size) {
    uint32_t h = 2166136261u;
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
uint32_t HashString(const char *s) {
    return s == NULL ? 0 : Hash(s, strlen(s));
}
uint32_t Word(const void *p) {
    return uint32_t(uintptr_t(p));
}

template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    call();
#endif
    return true;
}

void Originals(bool on) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, on);
}

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[64];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

// ---- the private objects

alignas(16) uint8_t g_object[0x400];        // the object under test
alignas(16) uint8_t g_owner[0x200];         // a gun's owner (an ASceneObj with a fake vtable)
alignas(16) uint8_t g_gun[sizeof(AGun)];    // AFingoDeath's gun (the fake ActivateGun's)
alignas(16) uint8_t g_arena[0x2000];        // FastAlloc's and operator new's blocks
size_t g_arenaUsed;
alignas(16) AListener g_listener;
ABank g_banks[kBankSlotCount];
ABank *g_savedBanks[kBankSlotCount];

struct FakeMix {
    AMix mix;
    char name[32];
};
FakeMix g_mixes[16];
int g_mixCount;

AMix *MixByName(const char *name) {
    for (int i = 0; i < g_mixCount; i++) {
        if (strcmp(g_mixes[i].name, name) == 0)
            return &g_mixes[i].mix;
    }
    if (g_mixCount == int(sizeof(g_mixes) / sizeof(g_mixes[0])))
        return &g_mixes[0].mix;
    FakeMix &m = g_mixes[g_mixCount];
    strncpy(m.name, name, sizeof(m.name) - 1);
    m.mix.volume = 0.45f + 0.07f * float(g_mixCount);
    g_mixCount++;
    return &m.mix;
}

// Fake answers, set by each case
bool g_finished;            // FUN_000d36c0's
int g_finishedPattern;      // or, when non-zero, its bits in turn
int g_finishedCalls;
bool g_tracked;             // the owner's IsTracked
int g_presentCalls;
int g_presentSalt;
double g_randoms[8];
int g_randomCalls;

void ArenaReset() {
    memset(g_arena, 0, sizeof(g_arena));
    g_arenaUsed = 0;
}
void *ArenaAlloc(unsigned size) {
    size_t need = (size + 15) & ~size_t(15);
    if (g_arenaUsed + need > sizeof(g_arena))
        g_arenaUsed = 0;
    void *block = g_arena + g_arenaUsed;
    g_arenaUsed += need;
    memset(block, 0xcd, size);
    return block;
}

// ---- the call log and the fakes

enum : uint32_t {
    kMixAdd = 1, kMixGet, kMixRemove, kMixVolume, kVoiceNew, kVoicePlay, kViewDestruct, kVoiceDestruct,
    kVoiceFinished, kLookup, kPresent, kPatchName, kTriggerIds, kTriggerName, kOneShot, kOneShotAt, kRandom,
    kAlloc, kFree, kSoundNew, kSoundDelete, kVehicleConstruct, kVehicleDestruct, kVehiclePlay, kActivateGun,
    kActivateTurret, kIsTracked,
};

std::vector<uint32_t> g_log;
void Log(uint32_t type, std::initializer_list<uint32_t> words) {
    g_log.push_back(type);
    g_log.push_back(uint32_t(words.size()));
    g_log.insert(g_log.end(), words.begin(), words.end());
}

AMix *FakeMixAdd(const char *name) {
    Log(kMixAdd, { HashString(name) });
    return MixByName(name);
}
AMix *FakeMixGet(const char *name) {
    Log(kMixGet, { HashString(name) });
    return MixByName(name);
}
void __fastcall FakeMixRemove(AMix *mix, int) {
    Log(kMixRemove, { Word(mix) });
}
double __fastcall FakeMixGetVolume(AMix *mix, int) {
    Log(kMixVolume, { Word(mix) });
    return double(mix->volume) * (1.0 / 3.0);      // unrounded, as the x87 would leave it
}
AVoice *__fastcall FakeVoiceConstruct(AVoice *voice, int, AMix *mix, int bank, int patch) {
    Log(kVoiceNew, { Word(voice), Word(mix), uint32_t(bank), uint32_t(patch) });
    voice->mix = mix;
    for (int i = 0; i < 3; i++) {
        memset(&voice->views[i], 0x40 + i, sizeof(AVoice::View));
        voice->views[i].bank = bank;
        voice->views[i].patch = patch;
    }
    return voice;
}
void __fastcall FakeVoicePlay(AVoice *voice, int, int view, float volume, float pitch, float azimuth, float delay,
                              float fxLevel) {
    Log(kVoicePlay, { Word(voice), uint32_t(view), Bits(volume), Bits(pitch), Bits(azimuth), Bits(delay),
                      Bits(fxLevel) });
}
void __fastcall FakeViewDestruct(AVoice::View *view, int) {
    Log(kViewDestruct, { Word(view) });
}
void __fastcall FakeVoiceDestruct(AVoice *voice, int) {
    Log(kVoiceDestruct, { Word(voice) });
}
bool __fastcall FakeVoiceIsFinished(AVoice *voice, int) {
    Log(kVoiceFinished, { Word(voice) });
    if (g_finishedPattern != 0)
        return ((g_finishedPattern >> (g_finishedCalls++ & 15)) & 1) != 0;
    return g_finished;
}
int __fastcall FakeLookup(AIndex *index, int, const char *name) {
    Log(kLookup, { Word(index), HashString(name) });
    return name == NULL ? -1 : int(HashString(name) % 61) - 5;
}
bool __fastcall FakeIsPatchPresent(ABank *bank, int, int patch) {
    Log(kPresent, { Word(bank), uint32_t(patch) });
    return ++g_presentCalls > 24 || (uint32_t(patch * 7 + g_presentSalt) % 3) == 0;
}
const char *FakeGetPatchName(int handle, int patch) {
    Log(kPatchName, { uint32_t(handle), uint32_t(patch) });
    return "SFX_FakePatch";
}
void FakeTriggerIds(int bank, int patch, const char *mixName, int view) {
    Log(kTriggerIds, { uint32_t(bank), uint32_t(patch), HashString(mixName), uint32_t(view) });
}
void FakeTriggerName(int bankNumber, const char *patchName, const char *mixName, int view) {
    Log(kTriggerName, { uint32_t(bankNumber), HashString(patchName), HashString(mixName), uint32_t(view) });
}
void *__fastcall FakeOneShot(void *memory, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity,
                             float volume, const char *mixName) {
    Log(kOneShot, { Word(memory), uint32_t(bank), uint32_t(patch), Word(position), Hash(position, 12),
                    Word(velocity), Hash(velocity, 12), Bits(volume), HashString(mixName) });
    memset(memory, 0x3c, sizeof(AOneShotSound));
    return memory;
}
void *__fastcall FakeOneShotAt(void *memory, int, int bank, int patch, const Coord3 *position, float volume,
                               const char *mixName, bool loop) {
    Log(kOneShotAt, { Word(memory), uint32_t(bank), uint32_t(patch), Word(position), Hash(position, 12),
                      Bits(volume), HashString(mixName), uint32_t(loop) });
    memset(memory, 0x3d, sizeof(AOneShotSound));
    return memory;
}
double FakeRrandom() {
    Log(kRandom, {});
    return g_randoms[g_randomCalls++ & 7];
}
void *FakeFastAlloc(unsigned size, const char *name) {
    Log(kAlloc, { size, HashString(name) });
    return ArenaAlloc(size);
}
void FakeFastFree(void *block, unsigned size) {
    Log(kFree, { Word(block), size });
}
void *FakeSoundNew(unsigned size, const char *name) {
    Log(kSoundNew, { size, HashString(name) });
    return ArenaAlloc(size);
}
void FakeSoundDelete(void *block, unsigned size) {
    Log(kSoundDelete, { Word(block), size });
}
AVehicle *__fastcall FakeVehicleConstruct(AVehicle *vehicle, int, const char *mixName, int skidVoices) {
    Log(kVehicleConstruct, { Word(vehicle), HashString(mixName), uint32_t(skidVoices) });
    memset(vehicle, 0x11, sizeof(AVehicle));
    vehicle->vtable = 0x001a2680;
    vehicle->mix = MixByName(mixName);
    vehicle->gun = NULL;
    vehicle->turret = NULL;
    vehicle->gunLoops = 1;
    return vehicle;
}
void __fastcall FakeVehicleDestruct(AVehicle *vehicle, int) {
    Log(kVehicleDestruct, { Word(vehicle) });
}
void __fastcall FakeVehiclePlay(AVehicle *vehicle, int, ASoundPlayParams *params) {
    Log(kVehiclePlay, { Word(vehicle), Word(params) });
}
void __fastcall FakeActivateGun(AVehicle *vehicle, int, const char *mixName) {
    Log(kActivateGun, { Word(vehicle), HashString(mixName) });
    vehicle->gun = reinterpret_cast<AGun *>(g_gun);
}
void __fastcall FakeActivateTurret(AVehicle *vehicle, int) {
    Log(kActivateTurret, { Word(vehicle) });
}

// The owner's vtable: slot 7 (IsTracked) answers g_tracked
bool __fastcall FakeIsTracked(void *owner, int) {
    Log(kIsTracked, { Word(owner) });
    return g_tracked;
}
const void *g_ownerVtable[9] = {
    (const void *)0x0001c080, (const void *)0x0001c080, (const void *)0x0011c710, (const void *)0x00017550,
    (const void *)0x00015770, (const void *)0x00037a10, (const void *)0x0001c080, (const void *)&FakeIsTracked,
    (const void *)0x0001c080,
};

// ---- the globals

struct Globals {
    int32_t frames;
    float clock;
    uint8_t paused;
    int32_t hard, origHard;
    int32_t next[7], origNext[7];
    bool wrapped[2];
    uint8_t origWrapped[2];
};

void SaveGlobals(Globals *g) {
    g->frames = ShadowFrames;
    g->clock = ShadowClock;
    g->paused = ShadowPaused;
    g->hard = NextHardCollision;
    g->origHard = OrigNextHardCollision;
    int32_t next[7] = {NextFPedLine, NextMPedLine, NextGruntLine, NextScreamLine, NextDeathLine, NextYellLine,
                       NextFallLine};
    int32_t orig[7] = {OrigNextFPed, OrigNextMPed, OrigNextGrunt, OrigNextScream, OrigNextDeath, OrigNextYell,
                       OrigNextFall};
    memcpy(g->next, next, sizeof(next));
    memcpy(g->origNext, orig, sizeof(orig));
    g->wrapped[0] = MPedLinesWrapped;
    g->wrapped[1] = FPedLinesWrapped;
    g->origWrapped[0] = OrigMPedWrapped;
    g->origWrapped[1] = OrigFPedWrapped;
}

void LoadGlobals(const Globals &g) {
    ShadowFrames = g.frames;
    ShadowClock = g.clock;
    ShadowPaused = g.paused;
    NextHardCollision = g.hard;
    OrigNextHardCollision = g.origHard;
    NextFPedLine = g.next[0];
    NextMPedLine = g.next[1];
    NextGruntLine = g.next[2];
    NextScreamLine = g.next[3];
    NextDeathLine = g.next[4];
    NextYellLine = g.next[5];
    NextFallLine = g.next[6];
    OrigNextFPed = g.origNext[0];
    OrigNextMPed = g.origNext[1];
    OrigNextGrunt = g.origNext[2];
    OrigNextScream = g.origNext[3];
    OrigNextDeath = g.origNext[4];
    OrigNextYell = g.origNext[5];
    OrigNextFall = g.origNext[6];
    MPedLinesWrapped = g.wrapped[0];
    FPedLinesWrapped = g.wrapped[1];
    OrigMPedWrapped = g.origWrapped[0];
    OrigFPedWrapped = g.origWrapped[1];
}

// The owned globals as the side running sees them: both sides start from the same values
void SetOwned(int32_t hard, const int32_t next[7], bool mWrapped) {
    NextHardCollision = OrigNextHardCollision = hard;
    NextFPedLine = OrigNextFPed = next[0];
    NextMPedLine = OrigNextMPed = next[1];
    NextGruntLine = OrigNextGrunt = next[2];
    NextScreamLine = OrigNextScream = next[3];
    NextDeathLine = OrigNextDeath = next[4];
    NextYellLine = OrigNextYell = next[5];
    NextFallLine = OrigNextFall = next[6];
    MPedLinesWrapped = mWrapped;
    OrigMPedWrapped = mWrapped;
    FPedLinesWrapped = false;
    OrigFPedWrapped = 0;
}

void OwnedWords(std::vector<uint32_t> &out) {
    if (g_original) {
        out.insert(out.end(), {uint32_t(OrigNextHardCollision), uint32_t(OrigNextFPed), uint32_t(OrigNextMPed),
                               uint32_t(OrigNextGrunt), uint32_t(OrigNextScream), uint32_t(OrigNextDeath),
                               uint32_t(OrigNextYell), uint32_t(OrigNextFall), uint32_t(OrigMPedWrapped != 0),
                               uint32_t(OrigFPedWrapped != 0)});
    } else {
        out.insert(out.end(), {uint32_t(NextHardCollision), uint32_t(NextFPedLine), uint32_t(NextMPedLine),
                               uint32_t(NextGruntLine), uint32_t(NextScreamLine), uint32_t(NextDeathLine),
                               uint32_t(NextYellLine), uint32_t(NextFallLine), uint32_t(MPedLinesWrapped),
                               uint32_t(FPedLinesWrapped)});
    }
}

// ---- one case: the original's run, then the port's, compared

void Append(std::vector<uint32_t> &out, const void *data, size_t size) {
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i += 4) {
        uint32_t w = 0;
        memcpy(&w, b + i, size - i < 4 ? size - i : 4);
        out.push_back(w);
    }
}

void DumpWords(const char *what, const std::vector<uint32_t> &words, size_t from = 0) {
    printf("[gameaudioshadowc]   %s (%u words):\n", what, unsigned(words.size()));
    for (size_t i = from; i < words.size() && i < from + 192; i += 8) {
        printf("[gameaudioshadowc]     %3u:", unsigned(i));
        for (size_t j = i; j < i + 8 && j < words.size(); j++)
            printf(" %08x", words[j]);
        printf("\n");
    }
    fflush(stdout);
}

// Every case's common inputs: the arena emptied, the fakes' answers
struct Inputs {
    bool finished;
    int finishedPattern;
    bool tracked;
    int presentSalt;
    double randoms[8];
    int32_t frames;
    float clock;
    uint8_t paused;
    int32_t hard;
    int32_t next[7];
    bool mWrapped;
};

Inputs RandomInputs() {
    Inputs in;
    in.finished = Chance(50);
    in.finishedPattern = Chance(30) ? RandomInt(1, 0xffff) : 0;
    in.tracked = Chance(30);
    in.presentSalt = RandomInt(0, 2);
    for (double &r : in.randoms)
        r = Chance(10) ? 0.1 : double(NextRandom()) / 4294967296.0;
    in.frames = Chance(10) ? RandomInt(-2, 0) : RandomInt(1, 12);
    in.clock = Chance(20) ? 0.0f : Uniform(0.0f, 400.0f);
    in.paused = Chance(15);
    in.hard = RandomInt(0, 1);
    const int sizes[7] = {5, 10, 8, 4, 11, 3, 3};
    for (int i = 0; i < 7; i++)
        in.next[i] = RandomInt(0, sizes[i] - 1);
    in.mWrapped = Chance(20);
    return in;
}

void ApplyInputs(const Inputs &in) {
    ArenaReset();
    g_mixCount = 0;
    memset(g_mixes, 0, sizeof(g_mixes));
    g_finished = in.finished;
    g_finishedPattern = in.finishedPattern;
    g_finishedCalls = 0;
    g_tracked = in.tracked;
    g_presentCalls = 0;
    g_presentSalt = in.presentSalt;
    memcpy(g_randoms, in.randoms, sizeof(g_randoms));
    g_randomCalls = 0;
    ShadowFrames = in.frames;
    ShadowClock = in.clock;
    ShadowPaused = in.paused;
    SetOwned(in.hard, in.next, in.mWrapped);
}

// setup() puts the object in place (after the inputs); run(original) makes the call; capture(words) adds what
// the case should compare beyond the object, the arena and the owned globals.
template <class Setup, class Run, class Capture>
void Case(const char *what, const Inputs &in, size_t objectSize, Setup setup, Run run, Capture capture) {
    std::vector<uint32_t> words[2], logs[2];
    for (int phase = 0; phase < 2; phase++) {
        bool original = phase == 0;
        g_original = original;
        ApplyInputs(in);
        setup();
        g_log.clear();
        bool ok;
        if (original) {
            Originals(true);
            ok = Guarded([&] { run(true); });
            Originals(false);
        } else {
            ok = Guarded([&] { run(false); });
        }
        words[phase].push_back(ok);
        Append(words[phase], g_object, objectSize);
        words[phase].push_back(uint32_t(g_arenaUsed));
        Append(words[phase], g_arena, g_arenaUsed);
        OwnedWords(words[phase]);
        capture(words[phase]);
        logs[phase] = g_log;
    }
    char label[96];
    snprintf(label, sizeof(label), "%s (state)", what);
    Check(words[0] == words[1], label, g_cases);
    snprintf(label, sizeof(label), "%s (calls)", what);
    Check(logs[0] == logs[1], label, g_cases);
    if (!g_dumped && (words[0] != words[1] || logs[0] != logs[1])) {
        g_dumped = true;
        size_t first = 0;
        while (first < words[0].size() && first < words[1].size() && words[0][first] == words[1][first])
            first++;
        first &= ~size_t(7);
        DumpWords("state, original", words[0], first);
        DumpWords("state, port    ", words[1], first);
        DumpWords("calls, original", logs[0]);
        DumpWords("calls, port    ", logs[1]);
    }
    g_cases++;
}

void NoCapture(std::vector<uint32_t> &) {
}

// ---- inputs for the objects

ASoundPlayParams g_params;

void RandomParams() {
    g_params.volume = Chance(10) ? 0.0f : Uniform(0.0f, 1.2f);
    g_params.pitch = Chance(10) ? 0.0f : Uniform(-0.3f, 2.5f);
    g_params.azimuth = Uniform(-1.0f, 1.0f);
    g_params.facing = 0.0f;
    g_params.unknown10 = 0.0f;
    g_params.distance = 0.0f;
    g_params.listener = &g_listener;
    g_listener.view = RandomInt(0, 2);
    float at[4] = {Uniform(-20.0f, 20.0f), Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f), 1.0f};
    memcpy(g_listener.unknown60, at, sizeof(at));
}

// A voice in the arena, as FastAlloc and the fake constructor would leave it (made in setup: the same address
// for both runs)
AVoice *ArenaVoice(int patch) {
    AVoice *voice = static_cast<AVoice *>(ArenaAlloc(sizeof(AVoice)));
    voice->mix = MixByName("Voice Mix");
    for (int i = 0; i < 3; i++) {
        memset(&voice->views[i], 0x50 + i, sizeof(AVoice::View));
        voice->views[i].bank = 9;
        voice->views[i].patch = patch;
    }
    return voice;
}

void RandomBase(ABaseSound *sound, uint32_t vtable) {
    uint8_t *b = reinterpret_cast<uint8_t *>(sound);
    for (size_t i = 0; i < sizeof(ABaseSound); i++)
        b[i] = uint8_t(NextRandom() >> 24);
    sound->vtable = vtable;
    sound->position = {Uniform(-20.0f, 20.0f), Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f)};
    sound->velocity = {Uniform(-4.0f, 4.0f), Uniform(-1.0f, 1.0f), Uniform(-4.0f, 4.0f)};
    sound->volume = Uniform(0.0f, 1.2f);
    sound->pitch = Uniform(0.5f, 1.5f);
    sound->mix = MixByName("Object Mix");
}

void SetString(GameStd::String *s, const char *text) {
    memset(s, 0, sizeof(*s));
    s->capacity = 15;
    s->ConstructText(text);
}

// An AGun's random state; strings from the arena when long (the setup's allocations)
void RandomGun(AGun *gun, ASceneObj *owner) {
    memset(gun, 0x77, sizeof(AGun));
    gun->owner = owner;
    gun->bank = fgBanks[4];
    gun->mix = MixByName("Gun Mix");
    gun->voice = Chance(85) ? ArenaVoice(RandomInt(0, 40)) : NULL;
    gun->fireTimer = Chance(10) ? 6.0f : Uniform(-1.0f, 9.0f);
    gun->sustain = Chance(20) ? 0.0f : Uniform(-2.0f, 11.0f);
    gun->fireInterval = Chance(70) ? 6.0f : Uniform(0.5f, 8.0f);
    SetString(&gun->firePatch, Chance(50) ? "SFX_HeliMachGun" : "SFX_mntmachfire_long_name");
    SetString(&gun->shellPatch, "SFX_ShellCasing");
    SetString(&gun->mixName, Chance(50) ? "World Sounds" : "A Longer Mix Name For The Gun");
    gun->shells = Chance(50);
}

void RandomOwner() {
    ASceneObj *owner = reinterpret_cast<ASceneObj *>(g_owner);
    RandomBase(owner, uint32_t(uintptr_t(g_ownerVtable)));
    owner->gunLoops = uint8_t(Chance(50));
}

// ---- the tests

void TestSceneObj() {
    for (int c = 0; c < 40; c++) {
        Inputs in = RandomInputs();
        unsigned flags = c & 1;
        Case("ASceneObj construct and delete", in, sizeof(ASceneObj),
             [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 const char *mixName = c & 2 ? "World Sounds" : "Other";
                 if (original) {
                     Orig_SceneConstruct(g_object, 0, mixName);
                     Orig_ScenePlayCollision(g_object, 0, 1.0f, NULL, NULL, 3);
                     Orig_SceneDelete(g_object, 0, flags);
                 } else {
                     ASceneObj *s = reinterpret_cast<ASceneObj *>(g_object);
                     s->Construct(mixName);
                     s->PlayCollision(1.0f, NULL, NULL, 3);
                     s->Delete(flags);
                 }
             },
             NoCapture);
    }
}

void TestGun() {
    AGun *gun = reinterpret_cast<AGun *>(g_object);
    ASceneObj *owner = reinterpret_cast<ASceneObj *>(g_owner);
    for (int c = 0; c < 30; c++) {
        Inputs in = RandomInputs();
        const char *patch = c & 1 ? "SFX_SALFire" : "SFX_fingergunfire_and_more";
        Case("AGun construct, fire patch, destruct", in, sizeof(AGun),
             [&] {
                 memset(g_object, 0xee, sizeof(g_object));
                 RandomOwner();
             },
             [&](bool original) {
                 GameStd::String s;
                 SetString(&s, patch);
                 if (original) {
                     Orig_GunConstruct(gun, 0, owner, c & 2 ? "World Sounds" : "A Longer Mix Name For The Gun");
                     Orig_GunSetFirePatch(gun, 0, s);
                     if (c & 4)
                         Orig_GunDestruct(gun, 0);
                 } else {
                     gun->Construct(owner, c & 2 ? "World Sounds" : "A Longer Mix Name For The Gun");
                     gun->SetFirePatch(s);
                     if (c & 4)
                         gun->Destruct();
                 }
             },
             NoCapture);
    }
    for (int c = 0; c < 1500; c++) {
        Inputs in = RandomInputs();
        uint8_t firing = uint8_t(RandomInt(0, 2));
        float frames = Chance(10) ? 0.0f : Uniform(0.0f, 8.0f);
        uint32_t seed = NextRandom();
        Case("AGun::Play", in, sizeof(AGun),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomOwner();
                 RandomGun(gun, owner);
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original)
                     Orig_GunPlay(gun, 0, firing, &g_params, frames);
                 else
                     gun->Play(firing, &g_params, frames);
             },
             NoCapture);
    }
}

void TestTurret() {
    ATurret *turret = reinterpret_cast<ATurret *>(g_object);
    for (int c = 0; c < 20; c++) {
        Inputs in = RandomInputs();
        Case("ATurret construct, destruct", in, sizeof(ATurret),
             [&] {
                 memset(g_object, 0xee, sizeof(g_object));
             },
             [&](bool original) {
                 if (original) {
                     Orig_TurretConstruct(turret, 0);
                     turret->servoVoice = c & 1 ? ArenaVoice(1) : NULL;
                     turret->upDownVoice = c & 2 ? ArenaVoice(2) : NULL;
                     turret->sidewaysVoice = c & 4 ? ArenaVoice(3) : NULL;
                     Orig_TurretDestruct(turret, 0);
                 } else {
                     turret->Construct();
                     turret->servoVoice = c & 1 ? ArenaVoice(1) : NULL;
                     turret->upDownVoice = c & 2 ? ArenaVoice(2) : NULL;
                     turret->sidewaysVoice = c & 4 ? ArenaVoice(3) : NULL;
                     turret->Destruct();
                 }
             },
             NoCapture);
    }
    for (int c = 0; c < 64 * 6; c++) {
        Inputs in = RandomInputs();
        int bits = c & 63;
        uint32_t seed = NextRandom();
        Case("ATurret::Play", in, sizeof(ATurret),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 memset(turret, 0x66, sizeof(ATurret));
                 turret->mix = MixByName("Weapons Init");
                 turret->servo = (bits & 8) != 0;
                 turret->turningSideways = (bits & 16) != 0;
                 turret->turningUpDown = (bits & 32) != 0;
                 turret->servoVoice = Chance(60) ? ArenaVoice(4) : NULL;
                 turret->sidewaysVoice = Chance(60) ? ArenaVoice(5) : NULL;
                 turret->upDownVoice = Chance(60) ? ArenaVoice(6) : NULL;
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original)
                     Orig_TurretPlay(turret, 0, (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, &g_params);
                 else
                     turret->Play((bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, &g_params);
             },
             NoCapture);
    }
}

void TestSentry() {
    ASentry *sentry = reinterpret_cast<ASentry *>(g_object);
    for (int c = 0; c < 20; c++) {
        Inputs in = RandomInputs();
        unsigned flags = c & 1;
        Case("ASentry construct and delete", in, sizeof(ASentry),
             [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 if (original) {
                     Orig_SentryConstruct(sentry, 0);
                     if (c & 2)
                         Orig_SentryDelete(sentry, 0, flags);
                 } else {
                     sentry->Construct();
                     if (c & 2)
                         sentry->Delete(flags);
                 }
             },
             NoCapture);
    }
    for (int c = 0; c < 400; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        Case("ASentry::Play", in, sizeof(ASentry),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomBase(sentry, 0x001a2f08);
                 memset(reinterpret_cast<uint8_t *>(sentry) + 0xc0, 0, 0x10);
                 sentry->servo = uint8_t(RandomInt(0, 2));
                 sentry->firing = uint8_t(RandomInt(0, 2));
                 sentry->turningSideways = uint8_t(RandomInt(0, 2));
                 sentry->turningUpDown = uint8_t(RandomInt(0, 2));
                 sentry->gunLoops = uint8_t(Chance(50));
                 sentry->turret.mix = MixByName("Weapons Init");
                 sentry->turret.servo = Chance(50);
                 sentry->turret.turningSideways = Chance(50);
                 sentry->turret.turningUpDown = Chance(50);
                 sentry->turret.unknown13 = 0;
                 sentry->turret.servoVoice = Chance(50) ? ArenaVoice(7) : NULL;
                 sentry->turret.sidewaysVoice = Chance(50) ? ArenaVoice(8) : NULL;
                 sentry->turret.upDownVoice = Chance(50) ? ArenaVoice(9) : NULL;
                 RandomGun(&sentry->gun, sentry);
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original)
                     Orig_SentryPlay(sentry, 0, &g_params);
                 else
                     sentry->Play(&g_params);
             },
             NoCapture);
    }
}

void RandomSmackable(ASmackable *s) {
    RandomBase(s, 0x001a2f38);
    memset(reinterpret_cast<uint8_t *>(s) + 0xc0, 0, 0x30);
    s->gunLoops = 1;
    s->collisionVoice = Chance(50) ? ArenaVoice(10) : NULL;
    s->rollVoice = Chance(80) ? ArenaVoice(11) : NULL;
    for (int i = 0; i < 3; i++)
        s->collisionIds[i] = Chance(20) ? -1 : RandomInt(0, 0x7ff);
    s->rollFrames = Chance(20) ? RandomInt(-3, 3) : RandomInt(0, 14);
    s->rollVolume = Chance(10) ? 0.0f : Uniform(-0.2f, 1.3f);
    s->detached = Chance(30);
    float scale = Chance(30) ? 0.1f : 1.0f;
    s->velocity = {Uniform(-12.0f, 12.0f) * scale, Uniform(-2.0f, 2.0f), Uniform(-12.0f, 12.0f) * scale};
}

void TestSmackable() {
    ASmackable *s = reinterpret_cast<ASmackable *>(g_object);
    for (int c = 0; c < 40; c++) {
        Inputs in = RandomInputs();
        int roll = c & 1 ? -1 : RandomInt(0, 0x3fff);
        int ids[3] = {RandomInt(-1, 0x3fff), RandomInt(-1, 0x3fff), RandomInt(-1, 0x3fff)};
        unsigned flags = (c >> 1) & 1;
        Case("ASmackable construct and delete", in, sizeof(ASmackable),
             [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 if (original) {
                     Orig_SmackConstruct(s, 0, ids[0], ids[1], ids[2], roll, "Smackables");
                     if (c & 4)
                         Orig_SmackDelete(s, 0, flags);
                 } else {
                     s->Construct(ids[0], ids[1], ids[2], roll, "Smackables");
                     if (c & 4)
                         s->Delete(flags);
                 }
             },
             NoCapture);
    }
    for (int c = 0; c < 1500; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        int op = c % 6;
        float impact;
        switch (RandomInt(0, 4)) {
        case 0: impact = 0.75f; break;
        case 1: impact = std::bit_cast<float>(0x7fc00000u); break;
        default: impact = Uniform(0.0f, 1.5f); break;
        }
        uint32_t answer = 0;
        Case("ASmackable methods", in, sizeof(ASmackable),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomSmackable(s);
                 RandomParams();
                 g_seed = keep;
                 answer = 0;
             },
             [&](bool original) {
                 Coord3 at = {1.0f, 2.0f, 3.0f};
                 switch (op) {
                 case 0:
                     if (original)
                         Orig_SmackLanding(s, 0, impact, &at, &at);
                     else
                         s->PlayLanding(impact, &at, &at);
                     break;
                 case 1:
                     if (original)
                         Orig_SmackCollision(s, 0, impact, &at, &at, 2);
                     else
                         s->PlayCollision(impact, &at, &at, 2);
                     break;
                 case 2:
                     answer = original ? Orig_SmackIsStillActive(s, 0) : s->IsStillActive();
                     answer |= HashString(original ? Orig_SmackGetName(s, 0) : s->GetName()) << 1;
                     break;
                 case 3:
                 case 4:
                     if (original)
                         Orig_SmackPlay(s, 0, &g_params);
                     else
                         s->Play(&g_params);
                     break;
                 default:
                     if (original)
                         Orig_SmackDetatch(s, 0);
                     else
                         s->Detatch();
                     break;
                 }
             },
             [&](std::vector<uint32_t> &w) { w.push_back(answer); });
    }
}

void TestTargeting() {
    ATargeting *t = reinterpret_cast<ATargeting *>(g_object);
    for (int c = 0; c < 10; c++) {
        Inputs in = RandomInputs();
        Case("ATargeting construct", in, sizeof(ATargeting), [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 if (original)
                     Orig_TargetConstruct(t, 0);
                 else
                     t->Construct();
             },
             NoCapture);
    }
    for (int c = 0; c < 5 * 5 * 2; c++) {
        Inputs in = RandomInputs();
        in.paused = uint8_t(c / 25);
        int from = c % 5 - 1, to = (c / 5) % 5 - 1;
        Case("ATargeting::SetState", in, sizeof(ATargeting),
             [&] {
                 memset(t, 0, sizeof(ATargeting));
                 t->state = TargetLockState(from);
                 t->mix = MixByName("Tracking System");
             },
             [&](bool original) {
                 if (original)
                     Orig_TargetSetState(t, 0, to);
                 else
                     t->SetState(TargetLockState(to));
             },
             NoCapture);
    }
}

void RandomWorldSound(AWorldSound *w) {
    RandomBase(w, 0x00193ae8);
    for (int i = 0; i < 3; i++) {
        memset(&w->voice.views[i], 0x30 + i, sizeof(AVoice::View));
    }
    w->voice.mix = w->mix;
    w->minDelay = Chance(20) ? 0.0f : Uniform(0.0f, 5.0f);
    w->maxDelay = Chance(20) ? 0.0f : Uniform(0.0f, 9.0f);
    w->minRepeats = Uniform(0.0f, 2.0f);
    w->maxRepeats = Uniform(0.0f, 4.0f);
    w->mode = RandomInt(0, 2);
    w->inUse = 1;
    w->nextTime = Chance(30) ? 0.0f : Uniform(0.0f, 400.0f);
    w->repeats = Chance(20) ? 0.5f : Uniform(-1.0f, 3.0f);
    w->waiting = uint8_t(Chance(60));
    w->passVoice = Chance(60) ? ArenaVoice(12) : NULL;
    w->passFrames = Chance(30) ? 0 : RandomInt(-2, 200);
    if (Chance(50)) {   // heading for the listener
        float at[4];
        memcpy(at, g_listener.unknown60, sizeof(at));
        w->velocity = {(at[0] - w->position.x) / 4.0f, (at[1] - w->position.y) / 4.0f,
                       (at[2] - w->position.z) / 4.0f};
    }
}

void TestWorldSound() {
    AWorldSound *w = reinterpret_cast<AWorldSound *>(g_object);
    for (int c = 0; c < 1200; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        int op = c % 8;
        Case("AWorldSound", in, sizeof(AWorldSound),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomParams();
                 RandomWorldSound(w);
                 g_seed = keep;
             },
             [&](bool original) {
                 if (op == 0) {
                     if (original)
                         Orig_WorldDestruct(w, 0);
                     else
                         w->Destruct();
                 } else if (op == 1) {
                     if (original)
                         Orig_Rewind(&w->voice, 0);
                     else
                         w->voice.Rewind();
                 } else {
                     if (original)
                         Orig_WorldPlay(w, 0, &g_params);
                     else
                         w->Play(&g_params);
                 }
             },
             NoCapture);
    }
}

void TestCharacter() {
    ACharacter *ch = reinterpret_cast<ACharacter *>(g_object);
    for (int c = 0; c < 20; c++) {
        Inputs in = RandomInputs();
        unsigned flags = c & 1;
        Case("ACharacter construct and delete", in, sizeof(ACharacter),
             [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 if (original) {
                     Orig_CharConstruct(ch, 0, "Characters");
                     if (c & 2)
                         ch->voice = ArenaVoice(13);
                     Orig_CharDelete(ch, 0, flags);
                 } else {
                     ch->Construct("Characters");
                     if (c & 2)
                         ch->voice = ArenaVoice(13);
                     ch->Delete(flags);
                 }
             },
             NoCapture);
    }
    static const char *const kWhat[] = {"HF", "HY", "HD", "HS", "HX", "H", "MPed", "M", "FPed", "", "x"};
    for (int c = 0; c < 1200; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        int op = c % 4;
        const char *what = kWhat[RandomInt(0, int(sizeof(kWhat) / sizeof(kWhat[0])) - 1)];
        uint32_t answer = 0;
        Case("ACharacter methods", in, sizeof(ACharacter),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomBase(ch, 0x0018a520);
                 ch->voice = Chance(op == 0 ? 20 : 60) ? ArenaVoice(14) : NULL;
                 memset(ch->unknownC4, 0, sizeof(ch->unknownC4));
                 RandomParams();
                 g_seed = keep;
                 answer = 0;
             },
             [&](bool original) {
                 if (op <= 1) {
                     if (original)
                         Orig_CharSay(ch, 0, what);
                     else
                         ch->Say(what);
                 } else if (op == 2) {
                     answer = HashString(original ? Orig_CharGetName(ch, 0) : ch->GetName());
                 } else {
                     if (original)
                         Orig_CharPlay(ch, 0, &g_params);
                     else
                         ch->Play(&g_params);
                 }
             },
             [&](std::vector<uint32_t> &w) { w.push_back(answer); });
    }
    for (int c = 0; c < 50; c++) {
        Inputs in = RandomInputs();
        float x = Uniform(-100.0f, 100.0f), y = Uniform(-100.0f, 100.0f), z = Uniform(-100.0f, 100.0f);
        uint32_t answer = 0;
        Case("MakeCoord3", in, sizeof(Coord3), [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 Coord3 *out = reinterpret_cast<Coord3 *>(g_object);
                 Coord3 *r = original ? Orig_MakeCoord3(out, x, y, z) : MakeCoord3(out, x, y, z);
                 answer = uint32_t(reinterpret_cast<uint8_t *>(r) - g_object);
             },
             [&](std::vector<uint32_t> &w) { w.push_back(answer); });
    }
}

void PrepareFingoGun() {
    AGun *gun = reinterpret_cast<AGun *>(g_gun);
    memset(gun, 0x55, sizeof(AGun));
    gun->owner = NULL;
    gun->bank = fgBanks[4];
    gun->mix = MixByName("POV Weapons");
    gun->voice = ArenaVoice(15);
    SetString(&gun->firePatch, "SFX_HeliMachGun");
    SetString(&gun->shellPatch, "SFX_ShellCasing");
    SetString(&gun->mixName, "POV Weapons");
    gun->shells = false;
}

void TestFingoDeath() {
    AFingoDeath *f = reinterpret_cast<AFingoDeath *>(g_object);
    for (int c = 0; c < 20; c++) {
        Inputs in = RandomInputs();
        unsigned flags = c & 1;
        Case("AFingoDeath construct and delete", in, sizeof(AFingoDeath),
             [&] {
                 memset(g_object, 0xee, sizeof(g_object));
                 PrepareFingoGun();
             },
             [&](bool original) {
                 if (original) {
                     Orig_FingoConstruct(f, 0);
                     if (c & 2)
                         f->reloadVoice = ArenaVoice(16);
                     Orig_FingoDelete(f, 0, flags);
                 } else {
                     f->Construct();
                     if (c & 2)
                         f->reloadVoice = ArenaVoice(16);
                     f->Delete(flags);
                 }
             },
             [&](std::vector<uint32_t> &w) { Append(w, g_gun, sizeof(g_gun)); });
    }
    for (int c = 0; c < 400; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        float recharge = Chance(20) ? 1.0f : Uniform(0.0f, 1.2f);
        Case("AFingoDeath::Play", in, sizeof(AFingoDeath),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomBase(f, 0x001a2b60);
                 memset(reinterpret_cast<uint8_t *>(f) + 0xc0, 0x22, sizeof(AFingoDeath) - 0xc0);
                 f->reloadVoice = Chance(50) ? ArenaVoice(17) : NULL;
                 f->recharging = Chance(50);
                 memset(f->unknown1f9, 0, sizeof(f->unknown1f9));
                 f->rechargePercent = Uniform(0.0f, 1.2f);
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original) {
                     Orig_FingoSetRecharge(f, 0, recharge);
                     Orig_FingoPlay(f, 0, &g_params);
                 } else {
                     f->SetRechargePercent(recharge);
                     f->Play(&g_params);
                 }
             },
             NoCapture);
    }
}

// ---- copies of the live sounds

bool IsVehicle(uint32_t vtable) {
    for (uint32_t v : kAVehicleVtables) {
        if (v == vtable)
            return true;
    }
    return false;
}

void TestLive() {
    ASoundList *list = fgSoundList;
    if (list == NULL || list->head == NULL)
        return;
    std::vector<ABaseSound *> sounds;
    int guard = 0;
    for (PointerListNode *node = list->Begin(); node != list->head && guard < 512; node = node->next, guard++)
        sounds.push_back(static_cast<ABaseSound *>(node->value));
    int counts[6] = {};
    for (ABaseSound *sound : sounds) {
        uint32_t vtable = sound->vtable;
        int kind = vtable == 0x001a2f08 ? 0 : vtable == 0x001a2f38 ? 1 : vtable == 0x0018a520 ? 2
                 : vtable == 0x00193ae8 || vtable == 0x00193ac4 ? 3 : IsVehicle(vtable) ? 4 : -1;
        if (kind < 0 || counts[kind] >= 6)
            continue;
        counts[kind]++;
        AVehicle *vehicle = static_cast<AVehicle *>(sound);
        if (kind == 4 && vehicle->gun == NULL && vehicle->turret == NULL) {
            counts[kind]--;
            continue;
        }
        static const size_t kSizes[] = {sizeof(ASentry), sizeof(ASmackable), sizeof(ACharacter),
                                        sizeof(AWorldSound), sizeof(AGun) + sizeof(ATurret)};
        uint8_t start[sizeof(ASentry) + 0x10];
        if (kind < 4)
            memcpy(start, sound, kSizes[kind]);
        for (int c = 0; c < 40; c++) {
            Inputs in = RandomInputs();
            uint32_t seed = NextRandom();
            uint8_t firing = uint8_t(RandomInt(0, 2));
            bool flags[3] = {Chance(50), Chance(50), Chance(50)};
            float frames = Uniform(0.0f, 6.0f);
            Case("live copies", in, kSizes[kind],
                 [&] {
                     uint32_t keep = g_seed;
                     g_seed = seed;
                     RandomParams();
                     if (kind < 4) {
                         memcpy(g_object, start, kSizes[kind]);
                     } else {
                         if (vehicle->gun != NULL)
                             memcpy(g_object, vehicle->gun, sizeof(AGun));
                         else
                             memset(g_object, 0, sizeof(AGun));
                         if (vehicle->turret != NULL)
                             memcpy(g_object + sizeof(AGun), vehicle->turret, sizeof(ATurret));
                         else
                             memset(g_object + sizeof(AGun), 0, sizeof(ATurret));
                     }
                     if (kind == 0) {
                         ASentry *s = reinterpret_cast<ASentry *>(g_object);
                         s->gun.owner = s;
                         s->firing = firing;
                         s->servo = flags[0];
                         s->turningSideways = flags[1];
                         s->turningUpDown = flags[2];
                     } else if (kind == 1) {
                         ASmackable *s = reinterpret_cast<ASmackable *>(g_object);
                         s->rollFrames += RandomInt(-2, 6);
                         s->rollVolume += Uniform(-0.3f, 0.3f);
                     } else if (kind == 3) {
                         AWorldSound *w = reinterpret_cast<AWorldSound *>(g_object);
                         w->waiting = uint8_t(Chance(50));
                         w->repeats += Uniform(-1.0f, 1.0f);
                     }
                     g_seed = keep;
                 },
                 [&](bool original) {
                     switch (kind) {
                     case 0:
                         if (original)
                             Orig_SentryPlay(g_object, 0, &g_params);
                         else
                             reinterpret_cast<ASentry *>(g_object)->Play(&g_params);
                         break;
                     case 1:
                         if (original)
                             Orig_SmackPlay(g_object, 0, &g_params);
                         else
                             reinterpret_cast<ASmackable *>(g_object)->Play(&g_params);
                         break;
                     case 2:
                         if (original)
                             Orig_CharPlay(g_object, 0, &g_params);
                         else
                             reinterpret_cast<ACharacter *>(g_object)->Play(&g_params);
                         break;
                     case 3:
                         if (original)
                             Orig_WorldPlay(g_object, 0, &g_params);
                         else
                             reinterpret_cast<AWorldSound *>(g_object)->Play(&g_params);
                         break;
                     default: {
                         AGun *gun = reinterpret_cast<AGun *>(g_object);
                         ATurret *turret = reinterpret_cast<ATurret *>(g_object + sizeof(AGun));
                         if (vehicle->gun != NULL) {
                             if (original)
                                 Orig_GunPlay(gun, 0, firing, &g_params, frames);
                             else
                                 gun->Play(firing, &g_params, frames);
                         }
                         if (vehicle->turret != NULL) {
                             if (original)
                                 Orig_TurretPlay(turret, 0, flags[0], flags[1], flags[2], &g_params);
                             else
                                 turret->Play(flags[0], flags[1], flags[2], &g_params);
                         }
                         break;
                     }
                     }
                 },
                 NoCapture);
        }
    }
    printf("[gameaudioshadowc] live: %d sentries, %d smackables, %d characters, %d world sounds, %d vehicles with "
           "a gun or turret\n", counts[0], counts[1], counts[2], counts[3], counts[4]);
    fflush(stdout);
}

}  // namespace

void GameAudioShadowC_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEAUDIOSHADOWC");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    FpControlGet(&g_x87, &g_sse);

    Globals saved;
    SaveGlobals(&saved);
    memcpy(g_savedBanks, fgBanks, sizeof(g_savedBanks));
    for (int i = 0; i < kBankSlotCount; i++) {
        memset(&g_banks[i], 0, sizeof(ABank));
        g_banks[i].slot = i;
        g_banks[i].handle = 0x20 + i;
        fgBanks[i] = &g_banks[i];
    }

    HookInstall(0x0011d6a0, (const void *)&FakeMixAdd);
    HookInstall(0x0011d720, (const void *)&FakeMixGet);
    HookInstall(0x0011d750, (const void *)&FakeMixRemove);
    HookInstall(0x0011ca10, (const void *)&FakeMixGetVolume);
    HookInstall(0x00123ca0, (const void *)&FakeVoiceConstruct);
    HookInstall(0x00124690, (const void *)&FakeVoicePlay);
    HookInstall(0x00123c50, (const void *)&FakeViewDestruct);
    HookInstall(0x0003f400, (const void *)&FakeVoiceDestruct);
    HookInstall(0x000d36c0, (const void *)&FakeVoiceIsFinished);
    HookInstall(0x00126d40, (const void *)&FakeLookup);
    HookInstall(0x00125ec0, (const void *)&FakeIsPatchPresent);
    HookInstall(0x00126a30, (const void *)&FakeGetPatchName);
    HookInstall(0x0011df50, (const void *)&FakeTriggerIds);
    HookInstall(0x0011e010, (const void *)&FakeTriggerName);
    HookInstall(0x000476f0, (const void *)&FakeOneShot);
    HookInstall(0x000477d0, (const void *)&FakeOneShotAt);
    HookInstall(0x00114a60, (const void *)&FakeRrandom);
    HookInstall(0x00114750, (const void *)&FakeFastAlloc);
    HookInstall(0x001147d0, (const void *)&FakeFastFree);
    HookInstall(0x0011c910, (const void *)&FakeSoundNew);
    HookInstall(0x0011c830, (const void *)&FakeSoundDelete);
    HookInstall(0x0011e2e0, (const void *)&FakeVehicleConstruct);
    HookInstall(0x00120a10, (const void *)&FakeVehicleDestruct);
    HookInstall(0x0011f9b0, (const void *)&FakeVehiclePlay);
    HookInstall(0x0011e9a0, (const void *)&FakeActivateGun);
    HookInstall(0x0011ea10, (const void *)&FakeActivateTurret);

    TestSceneObj();
    TestGun();
    TestTurret();
    TestSentry();
    TestSmackable();
    TestTargeting();
    TestWorldSound();
    TestCharacter();
    TestFingoDeath();

    // the live objects read the real banks' handles
    memcpy(fgBanks, g_savedBanks, sizeof(g_savedBanks));
    TestLive();

    HooksRemove();
    memcpy(fgBanks, g_savedBanks, sizeof(g_savedBanks));
    LoadGlobals(saved);

    printf("[gameaudioshadowc] weapons, world and speech: %ld cases, %ld checks, %ld differ (%ld calls faulted)\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
