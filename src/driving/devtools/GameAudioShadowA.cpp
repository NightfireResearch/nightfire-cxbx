#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAudioShadowA.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
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
#include "../audio/ZoomObj.h"
#include "../game/VehicleSound.h"
#include "../game/WeaponSound.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.audio's package A, run once from the first simulation tick when NIGHTFIRE_GAMEAUDIOSHADOWA
// is set. Each case runs the ORIGINAL (every entry of the package swapped back together, so an original reaches
// the other originals) and then the PORT on the same inputs, and compares the bytes they leave (the object, the
// blocks allocated, PlayCollision's tick table and gate counter, the answers) and a log of every call they make out
// of the package:
//
//   - AHorn: constructor, Play over random counters, honks and random numbers;
//   - AVehicle: constructor (both skid sets), Reset, the four Activate* (made or already there), ChooseHorn (with
//     and without a horn), destructor and deleting destructor (every pointer present or not), CalculateSpeed (with
//     and without a horn, speeds about 5 and 14), GetAirborneWheels, CalculateRpm (tracked, with a horn across the
//     gears, plain), PlayLanding (every surface mix, forces about 0.5 and 0.9, 0 to 4 wheels down), PlayCollision
//     (strengths about every threshold, every surface, the submersible's and snowmobile's types, busy ticks,
//     squeals), Play (random surfaces, slips, scrapes, bumps over their distances, wind at each distance, horn,
//     gun, turret, zoom and alarm present or not);
//   - AVehicleWind: constructor, Play (levels and pitches about their bounds), Stop, destructor;
//   - the play parameters: ListenerView::Azimuth and ASoundPlayParams::Compute (no maximum distance, the level band
//     both ways, beyond the maximum, inside the minimum and half of it, at the listener);
//   - AEngine: constructor, deleting destructor, Play (wheels down or not, rpm falling fast), Get (found or made),
//     RemoveAll; ARaceEngine: constructor, Play (throttle about 0.2 and 0.5, rev time about 0 and 60, rpm about 2000,
//     the listener inside, between 1 and 2, beyond), destructor and deleting destructor; RefCounterPair::Construct;
//   - copies of every live AVehicle in the sound list (the player's car, the AI's cars and helicopters, whatever the
//     mission has), their horn and wind copied too, played and queried with perturbed inputs, and copies of the live
//     engines in URefCounter<AEngine>.
//
// Fakes: the mixes (Add, Get, GetVolume), AVoice (constructor, Play, its destructors), AIndex::Lookup, AOneShotSound's
// constructor, rrandom, REAL_random, TIMER_gettick, UMemory::FastAlloc/FastFree, operator new/delete and ABaseSound's
// (blocks from a private arena, the same addresses for both runs), ASceneObj's constructor and destructor, AGun's and
// ATurret's constructors, destructors and Play, AZoomObj's constructor, destructor and Play, and URefCounter<AEngine>'s
// Get, GetReference, AddReference and RemoveReference. The bank slots are private banks for the private cases.
// Nothing reaches a live object but reads.
//
// Not covered: EngineRefTree's seven entries (the tree code every URefCounter compiles: each is instruction for
// instruction URefCounter<AMix>'s copy but for the addresses it calls, checked by listing; the in-game runs reach
// them through AEngine::Get and RemoveAll).
//
// A mutation the test sees: AVehicle::Play's rear grass skid pitch without its 0.75 (every case with grass or rough
// paving under a rear wheel logs a different AVoice::Play), or PlayCollision's `strength < 0.05f` made `<=` (the
// strength exactly 0.05 cases with a busy first tick).
//
// One summary line: [gameaudioshadowa] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

#define Orig_HornConstruct ((AHorn *(__fastcall *)(void *, int, const char *, float, float))0x0011e0f0)
#define Orig_HornPlay ((void (__fastcall *)(void *, int, bool, ASoundPlayParams *))0x0011e150)
#define Orig_Reset ((void (__fastcall *)(void *, int))0x0011e230)
#define Orig_VehicleConstruct ((AVehicle *(__fastcall *)(void *, int, const char *, int))0x0011e2e0)
#define Orig_ActivateGun ((void (__fastcall *)(void *, int, const char *))0x0011e9a0)
#define Orig_ActivateTurret ((void (__fastcall *)(void *, int))0x0011ea10)
#define Orig_ActivateZoom ((void (__fastcall *)(void *, int))0x0011ea80)
#define Orig_ActivateWind ((void (__fastcall *)(void *, int))0x0011eaf0)
#define Orig_CalculateSpeed ((void (__fastcall *)(void *, int))0x0011eb50)
#define Orig_GetAirborneWheels ((int (__fastcall *)(void *, int))0x0011ebf0)
#define Orig_PairDestruct ((void (__fastcall *)(void *, int))0x0011ec10)
#define Orig_CalculateRpm ((double (__fastcall *)(void *, int))0x0011ec80)
#define Orig_PlayLanding ((void (__fastcall *)(void *, int, float, const Coord3 *, const Coord3 *))0x0011ed30)
#define Orig_PlayCollision ((void (__fastcall *)(void *, int, float, const Coord3 *, const Coord3 *, int))0x0011f1a0)
#define Orig_VehiclePlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0011f9b0)
#define Orig_ChooseHorn ((void (__fastcall *)(void *, int, const char *, float, float))0x00120960)
#define Orig_VehicleDestruct ((void (__fastcall *)(void *, int))0x00120a10)
#define Orig_VehicleDelete ((void *(__fastcall *)(void *, int, unsigned))0x00120d50)
#define Orig_WindConstruct ((AVehicleWind *(__fastcall *)(void *, int, AVehicle *))0x0012e830)
#define Orig_WindPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *, float))0x0012e8f0)
#define Orig_WindStop ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012ea30)
#define Orig_WindDestruct ((void (__fastcall *)(void *, int))0x0012ea60)
#define Orig_Azimuth ((float (__fastcall *)(void *, int, float))0x0012ea80)
#define Orig_Compute ((void *(__fastcall *)(void *, int, ABaseSound *))0x0012eab0)
#define Orig_EngineConstruct ((AEngine *(__fastcall *)(void *, int))0x0012edd0)
#define Orig_EnginePlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012ede0)
#define Orig_EngineDestruct ((void (__fastcall *)(void *, int))0x0012ef20)
#define Orig_EngineDelete ((void *(__fastcall *)(void *, int, unsigned))0x0012ef30)
#define Orig_PairConstruct ((void *(__fastcall *)(void *, int, const char *, const RefCounterEntry *))0x0012ef50)
#define Orig_EngineGet ((AEngine *(*)(const char *, AVehicle *, float))0x0012f940)
#define Orig_RemoveAll ((void (*)(void))0x0012f9e0)
#define Orig_RaceConstruct ((ARaceEngine *(__fastcall *)(void *, int))0x0012fc60)
#define Orig_RacePlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012ffb0)
#define Orig_RaceDestruct ((void (__fastcall *)(void *, int))0x00130590)
#define Orig_RaceDelete ((void *(__fastcall *)(void *, int, unsigned))0x001306b0)

// The package's entries, swapped back together for an original's run
const unsigned kEntries[] = {
    0x0011e0f0, 0x0011e150, 0x0011e230, 0x0011e2e0, 0x0011e9a0, 0x0011ea10, 0x0011ea80, 0x0011eaf0, 0x0011eb50,
    0x0011ebf0, 0x0011ec10, 0x0011ec80, 0x0011ed30, 0x0011f1a0, 0x0011f9b0, 0x00120960, 0x00120a10, 0x00120d50,
    0x0012e830, 0x0012e8f0, 0x0012ea30, 0x0012ea60, 0x0012ea80, 0x0012eab0, 0x0012edd0, 0x0012ede0, 0x0012ef20,
    0x0012ef30, 0x0012ef50, 0x0012ef80, 0x0012efd0, 0x0012f340, 0x0012f530, 0x0012f620, 0x0012f7e0, 0x0012f820,
    0x0012f940, 0x0012f9e0, 0x0012fc60, 0x0012ffb0, 0x00130590, 0x001306b0,
};

// The globals the code reads and writes at the game's addresses
#define ShadowFrames I32_AT(0x00243a68)
#define ShadowCollisionTicks ((int32_t *)0x00243988)
// The original's copy of the global the port owns
#define OrigMetalGateCount U32_AT(0x00243984)

const uint32_t kAVehicleVtable = 0x001a2680;
const uint32_t kAVehicleVtables[] = {
    0x001a2680, 0x001a2abc, 0x001a2b14, 0x001a2b60, 0x001a2bc4, 0x001a2c40, 0x001a2d44, 0x001a2e28, 0x001a2e58,
    0x001a2ed8,
};
const uint32_t kARaceEngineVtable = 0x001a310c;

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
bool g_dumped;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[gameaudioshadowa] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x6d2b79f5;
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
// Near one of the values, or exactly it
float Near(float value, float spread) {
    if (Chance(25))
        return value;
    return value + Uniform(-spread, spread);
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
Hook g_hooks[96];
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
alignas(16) uint8_t g_vehicle[0x200];       // an engine's or a wind's vehicle
alignas(16) uint8_t g_arena[0x3000];        // FastAlloc's and the operator news' blocks
size_t g_arenaUsed;
alignas(16) AListener g_listener;
alignas(16) uint8_t g_sound[0x200];         // ASoundPlayParams::Compute's sound
ABank g_banks[kBankSlotCount];
ABank *g_savedBanks[kBankSlotCount];
AVoice g_voices[16];                        // the vehicle's voices (only their addresses reach the fakes)
uint8_t g_gunDummy[sizeof(AGun)], g_turretDummy[sizeof(ATurret)], g_zoomDummy[sizeof(AZoomObj)];
uint8_t g_pairDummy[0xc0];

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
bool g_tracked;
double g_randoms[8];
int g_randomCalls;
uint32_t g_randomWords[8];
int g_randomWordCalls;
int32_t g_ticks[8];
int g_tickCalls;
AEngine *g_refAnswer;
int g_removePattern;
int g_removeCalls;

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

// URefCounter<AEngine> as the fakes have it: a head and up to four nodes in a chain from head->left
alignas(4) uint8_t g_refsBlock[sizeof(EngineRefCounter)];
RefCounterNode g_head, g_nodes[4];
int g_nodeCount;
alignas(16) uint8_t g_engines[4][sizeof(ARaceEngine)];

EngineRefCounter *FakeRefs() {
    return reinterpret_cast<EngineRefCounter *>(g_refsBlock);
}

// ---- the call log and the fakes

enum : uint32_t {
    kMixAdd = 1, kMixGet, kMixVolume, kVoiceNew, kVoicePlay, kViewDestruct, kVoiceDestruct, kLookup, kOneShot,
    kRandom, kRandomWord, kTick, kAlloc, kFree, kNew, kDelete, kSoundNew, kSoundDelete, kSceneConstruct,
    kSceneDestruct, kGunConstruct, kGunDestruct, kGunPlay, kTurretConstruct, kTurretDestruct, kTurretPlay,
    kZoomConstruct, kZoomDestruct, kZoomPlay, kRefGet, kRefFind, kRefAdd, kRefRemove, kIsTracked, kEngineDelete,
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
int __fastcall FakeLookup(AIndex *index, int, const char *name) {
    Log(kLookup, { Word(index), HashString(name) });
    return name == NULL ? -1 : int(HashString(name) % 61) - 5;
}
void *__fastcall FakeOneShot(void *memory, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity,
                             float volume, const char *mixName) {
    Log(kOneShot, { Word(memory), uint32_t(bank), uint32_t(patch), Word(position), Hash(position, 12),
                    Word(velocity), Hash(velocity, 12), Bits(volume), HashString(mixName) });
    memset(memory, 0x3c, sizeof(AOneShotSound));
    return memory;
}
double FakeRrandom() {
    Log(kRandom, {});
    return g_randoms[g_randomCalls++ & 7];
}
uint32_t FakeRandomWord() {
    Log(kRandomWord, {});
    return g_randomWords[g_randomWordCalls++ & 7];
}
int FakeTick() {
    Log(kTick, {});
    return g_ticks[g_tickCalls++ & 7];
}
void *FakeFastAlloc(unsigned size, const char *name) {
    Log(kAlloc, { size, HashString(name) });
    return ArenaAlloc(size);
}
void FakeFastFree(void *block, unsigned size) {
    Log(kFree, { Word(block), size });
}
void *FakeNew(unsigned size) {
    Log(kNew, { size });
    return ArenaAlloc(size);
}
void FakeDelete(void *block) {
    Log(kDelete, { Word(block) });
}
void *FakeSoundNew(unsigned size, const char *name) {
    Log(kSoundNew, { size, HashString(name) });
    return ArenaAlloc(size);
}
void FakeSoundDelete(void *block, unsigned size) {
    Log(kSoundDelete, { Word(block), size });
}
ASceneObj *__fastcall FakeSceneConstruct(ASceneObj *object, int, const char *mixName) {
    Log(kSceneConstruct, { Word(object), HashString(mixName) });
    object->vtable = 0x001a2f80;
    object->mix = MixByName(mixName);
    object->servo = 0;
    object->firing = 0;
    object->turningSideways = 0;
    object->turningUpDown = 0;
    object->aiming = 0;
    object->gunLoops = 1;
    return object;
}
void __fastcall FakeSceneDestruct(ASceneObj *object, int) {
    Log(kSceneDestruct, { Word(object) });
}
void *__fastcall FakeGunConstruct(void *gun, int, ASceneObj *owner, const char *mixName) {
    Log(kGunConstruct, { Word(gun), Word(owner), HashString(mixName) });
    memset(gun, 0x5a, sizeof(AGun));
    return gun;
}
void __fastcall FakeGunDestruct(void *gun, int) {
    Log(kGunDestruct, { Word(gun) });
}
void __fastcall FakeGunPlay(void *gun, int, uint8_t firing, ASoundPlayParams *params, float frames) {
    Log(kGunPlay, { Word(gun), firing, Word(params), Bits(frames) });
}
void *__fastcall FakeTurretConstruct(void *turret, int) {
    Log(kTurretConstruct, { Word(turret) });
    memset(turret, 0x5b, sizeof(ATurret));
    return turret;
}
void __fastcall FakeTurretDestruct(void *turret, int) {
    Log(kTurretDestruct, { Word(turret) });
}
void __fastcall FakeTurretPlay(void *turret, int, bool sideways, bool upDown, bool servo, ASoundPlayParams *params) {
    Log(kTurretPlay, { Word(turret), uint32_t(sideways), uint32_t(upDown), uint32_t(servo), Word(params) });
}
void *__fastcall FakeZoomConstruct(void *zoom, int, AVehicle *vehicle) {
    Log(kZoomConstruct, { Word(zoom), Word(vehicle) });
    memset(zoom, 0x5c, sizeof(AZoomObj));
    return zoom;
}
void __fastcall FakeZoomDestruct(void *zoom, int) {
    Log(kZoomDestruct, { Word(zoom) });
}
void __fastcall FakeZoomPlay(void *zoom, int, ASoundPlayParams *params) {
    Log(kZoomPlay, { Word(zoom), Word(params) });
}
EngineRefCounter *FakeRefGet() {
    Log(kRefGet, {});
    return FakeRefs();
}
void *__fastcall FakeRefFind(void *map, int, const char *name) {
    Log(kRefFind, { Word(map), HashString(name) });
    return g_refAnswer;
}
void __fastcall FakeRefAdd(void *map, int, const char *name, AEngine *engine) {
    Log(kRefAdd, { Word(map), HashString(name), Word(engine) });
}
bool __fastcall FakeRefRemove(void *map, int, AEngine *engine) {
    Log(kRefRemove, { Word(map), Word(engine) });
    // The first node goes; the answer by the pattern
    RefCounterNode *first = g_head.left;
    for (int i = 0; i < g_nodeCount; i++) {
        if (first == &g_nodes[i]) {
            g_head.left = i + 1 < g_nodeCount ? &g_nodes[i + 1] : &g_head;
            break;
        }
    }
    return ((g_removePattern >> (g_removeCalls++ & 7)) & 1) != 0;
}
bool __fastcall FakeIsTracked(void *vehicle, int) {
    Log(kIsTracked, { Word(vehicle) });
    return g_tracked;
}
void *__fastcall FakeEngineDelete(void *engine, int, unsigned flags) {
    Log(kEngineDelete, { Word(engine), flags });
    return engine;
}

// AVehicle's vtable with slot 7 (IsTracked) answering g_tracked
const void *g_vehicleVtable[9] = {
    (const void *)0x00120d50, (const void *)0x0001c080, (const void *)0x0011c710, (const void *)0x0011f9b0,
    (const void *)0x0011ed30, (const void *)0x0011f1a0, (const void *)0x0001c080, (const void *)&FakeIsTracked,
    (const void *)0x0001c080,
};
// An engine's, its deleting destructor recorded
const void *g_engineVtable[2] = { (const void *)0x0012ffb0, (const void *)&FakeEngineDelete };

// ---- the globals

struct Globals {
    int32_t frames;
    int32_t ticks[12];
    uint32_t gate, origGate;
};

void SaveGlobals(Globals *g) {
    g->frames = ShadowFrames;
    memcpy(g->ticks, ShadowCollisionTicks, sizeof(g->ticks));
    g->gate = MetalGateCount;
    g->origGate = OrigMetalGateCount;
}

void LoadGlobals(const Globals &g) {
    ShadowFrames = g.frames;
    memcpy(ShadowCollisionTicks, g.ticks, sizeof(g.ticks));
    MetalGateCount = g.gate;
    OrigMetalGateCount = g.origGate;
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
    printf("[gameaudioshadowa]   %s (%u words):\n", what, unsigned(words.size()));
    for (size_t i = from; i < words.size() && i < from + 192; i += 8) {
        printf("[gameaudioshadowa]     %3u:", unsigned(i));
        for (size_t j = i; j < i + 8 && j < words.size(); j++)
            printf(" %08x", words[j]);
        printf("\n");
    }
    fflush(stdout);
}

// Every case's common inputs: the arena emptied, the fakes' answers, the globals
struct Inputs {
    bool tracked;
    double randoms[8];
    uint32_t randomWords[8];
    int32_t ticks[8];
    int32_t collisionTicks[12];
    int32_t frames;
    uint32_t gate;
};

Inputs RandomInputs() {
    Inputs in;
    in.tracked = Chance(30);
    for (double &r : in.randoms)
        r = Chance(10) ? 0.1 : double(NextRandom()) / 4294967296.0;
    for (uint32_t &w : in.randomWords)
        w = NextRandom();
    int32_t now = RandomInt(0, 2000);
    for (int32_t &t : in.ticks)
        t = Chance(80) ? now : now + RandomInt(1, 3);
    for (int32_t &t : in.collisionTicks)
        t = Chance(60) ? now - RandomInt(0, 50) : now + RandomInt(1, 5);
    if (Chance(15))
        in.collisionTicks[0] = Chance(50) ? 0 : -RandomInt(1, 5);
    in.frames = Chance(5) ? RandomInt(-2, 0) : RandomInt(1, 12);
    in.gate = NextRandom() & 7;
    return in;
}

void ApplyInputs(const Inputs &in) {
    ArenaReset();
    g_mixCount = 0;
    memset(g_mixes, 0, sizeof(g_mixes));
    g_tracked = in.tracked;
    memcpy(g_randoms, in.randoms, sizeof(g_randoms));
    g_randomCalls = 0;
    memcpy(g_randomWords, in.randomWords, sizeof(g_randomWords));
    g_randomWordCalls = 0;
    memcpy(g_ticks, in.ticks, sizeof(g_ticks));
    g_tickCalls = 0;
    memcpy(ShadowCollisionTicks, in.collisionTicks, sizeof(in.collisionTicks));
    ShadowFrames = in.frames;
    MetalGateCount = in.gate;
    OrigMetalGateCount = in.gate;
}

void GlobalWords(std::vector<uint32_t> &out) {
    out.push_back(g_original ? OrigMetalGateCount : MetalGateCount);
    out.push_back(uint32_t(ShadowFrames));
    Append(out, ShadowCollisionTicks, 12 * sizeof(int32_t));
}

// setup() puts the object in place (after the inputs); run(original) makes the call; capture(words) adds what
// the case should compare beyond the object, the arena and the globals.
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
        GlobalWords(words[phase]);
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

void RandomListener() {
    float *m = &g_listener.matrix.mtx[0][0];
    for (int i = 0; i < 16; i++)
        m[i] = Uniform(-1.2f, 1.2f);
    m[15] = 1.0f;
    g_listener.position = {Uniform(-20.0f, 20.0f), Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f), 1.0f};
    g_listener.velocity = {Uniform(-30.0f, 30.0f), Uniform(-3.0f, 3.0f), Uniform(-30.0f, 30.0f)};
    g_listener.view = RandomInt(0, 3);
    g_listener.level = Chance(50) ? -100000.0f : Uniform(-6.0f, 6.0f);
    g_listener.bandWidth = Chance(10) ? 0.0f : Uniform(0.5f, 6.0f);
}

void RandomParams() {
    g_params.volume = Chance(10) ? 0.0f : Uniform(0.0f, 1.2f);
    g_params.pitch = Chance(10) ? 0.0f : Uniform(-0.3f, 2.5f);
    g_params.azimuth = Uniform(-1.0f, 1.0f);
    g_params.facing = Uniform(-1.0f, 1.0f);
    g_params.unknown10 = Uniform(0.0f, 1.0f);
    float distance = Chance(20) ? float(RandomInt(0, 3)) : Uniform(0.0f, 3.0f);
    g_params.distance = distance;
    g_params.listener = &g_listener;
    RandomListener();
}

void RandomBase(ABaseSound *sound, uint32_t vtable) {
    uint8_t *b = reinterpret_cast<uint8_t *>(sound);
    for (size_t i = 0; i < sizeof(ABaseSound); i++)
        b[i] = uint8_t(NextRandom() >> 24);
    sound->vtable = vtable;
    sound->position = {Uniform(-20.0f, 20.0f), Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f)};
    sound->velocity = {Uniform(-30.0f, 30.0f), Uniform(-2.0f, 2.0f), Uniform(-30.0f, 30.0f)};
    sound->forward = {Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f)};
    sound->volume = Uniform(0.0f, 1.2f);
    sound->pitch = Uniform(0.5f, 1.5f);
    sound->mix = MixByName("Object Mix");
}

int RandomSurface() {
    return Chance(25) ? 0 : RandomInt(1, 13);
}

void RandomHorn(AHorn *horn) {
    memset(horn, 0x66, sizeof(AHorn));
    horn->volume = Uniform(0.0f, 1.2f);
    horn->pitch = Uniform(0.5f, 1.5f);
    horn->mix = MixByName("Horns");
    horn->honks = Chance(40) ? 0 : RandomInt(0, 6);
    horn->honkTime = Chance(40) ? 0 : RandomInt(0, 70);
    horn->pause = Chance(40) ? 0 : RandomInt(0, 300);
}

void RandomWind(AVehicleWind *wind, AVehicle *vehicle) {
    wind->vehicle = vehicle;
    wind->mix = MixByName("Player Wind");
    wind->voice = &g_voices[15];
    wind->level = Chance(20) ? Near(1.0f, 0.01f) : Chance(20) ? Near(0.4f, 0.01f) : Uniform(0.3f, 1.1f);
    wind->levelRate = Uniform(-0.03f, 0.03f);
    wind->pitch = Chance(20) ? Near(2.8f, 0.01f) : Chance(20) ? Near(1.0f, 0.01f) : Uniform(0.9f, 2.9f);
    wind->pitchRate = Uniform(-0.02f, 0.04f);
    wind->volume = Uniform(0.5f, 1.0f);
    wind->azimuth = Uniform(0.0f, 1.0f);
}

// A vehicle's random state (`vtable`: the real one or ours with a fake IsTracked); horn and wind from the arena
void RandomVehicle(AVehicle *v, bool fakeVtable) {
    RandomBase(v, fakeVtable ? Word(g_vehicleVtable) : kAVehicleVtable);
    uint8_t *b = reinterpret_cast<uint8_t *>(v);
    for (size_t i = sizeof(ABaseSound); i < sizeof(AVehicle); i++)
        b[i] = uint8_t(NextRandom() >> 24);
    v->servo = uint8_t(Chance(50));
    v->firing = uint8_t(RandomInt(0, 2));
    v->turningSideways = uint8_t(Chance(50));
    v->turningUpDown = uint8_t(Chance(50));
    static const int kTypes[] = {0, 1, 2, 3, 4, 5, 6, 7};
    v->vehicleType = kTypes[RandomInt(0, 7)];
    bool flat = Chance(20);
    int common = RandomSurface();
    for (int w = 0; w < 4; w++) {
        v->surfaces[w] = flat ? common : RandomSurface();
        v->previousSurfaces[w] = Chance(30) ? 0 : RandomSurface();
        v->suspensionCompression[w] = Uniform(0.0f, 1.0f);
    }
    v->frontSlipTarget = Uniform(0.0f, 1.0f);
    v->rearSlipTarget = Uniform(0.0f, 1.0f);
    v->frontSlip = Uniform(0.0f, 1.0f);
    v->rearSlip = Uniform(0.0f, 1.0f);
    v->skidMultiple = Chance(50) ? 1.0f : Uniform(1.0f, 3.0f);
    v->throttle = Uniform(-0.2f, 1.2f);
    v->rpm = Uniform(0.0f, 8000.0f);
    v->rpmWobble = Chance(20) ? Near(150.0f, 2.0f) : Chance(20) ? Near(-150.0f, 2.0f) : Uniform(-150.0f, 150.0f);
    v->rpmWobbleDirection = Chance(50) ? 1.0f : -1.0f;
    for (float &l : v->rumbleLevels)
        l = Uniform(0.0f, 1.0f);
    v->bumpDistances[kBumpWood] = Chance(30) ? Near(150.0f, 20.0f) : Uniform(0.0f, 160.0f);
    v->bumpDistances[kBumpRail] = Chance(30) ? Near(250.0f, 20.0f) : Uniform(0.0f, 260.0f);
    int band = RandomInt(0, 3);
    v->speed = band == 0 ? Near(5.0f, 1.0f) : band == 1 ? Near(14.0f, 1.0f) : band == 2 ? Near(15.0f, 2.0f)
                                                                                       : Uniform(0.0f, 60.0f);
    v->carSpeed = Uniform(0.0f, 60.0f);
    for (float &l : v->scrapeLevels)
        l = Uniform(0.0f, 1.0f);
    v->landingForce = Chance(30) ? Near(0.5f, 0.05f) : Chance(20) ? Near(0.9f, 0.05f) : Uniform(0.0f, 1.5f);
    v->hornHonk = Chance(50);
    v->bumpToggles[0] = Chance(50);
    v->bumpToggles[1] = Chance(50);
    v->boost = uint8_t(Chance(30));
    v->engineOn = uint8_t(Chance(70));
    v->squeals = Chance(50);
    for (int k = 0; k < kVehicleBumpCount; k++) {
        v->bumpPatches[k][0] = RandomInt(-1, 40);
        v->bumpPatches[k][1] = RandomInt(-1, 40);
    }
    for (int32_t &t : v->scrapeTimers)
        t = Chance(30) ? 0 : RandomInt(-3, 12);
    v->gun = Chance(40) ? reinterpret_cast<AGun *>(g_gunDummy) : NULL;
    v->turret = Chance(30) ? reinterpret_cast<ATurret *>(g_turretDummy) : NULL;
    v->zoom = Chance(30) ? reinterpret_cast<AZoomObj *>(g_zoomDummy) : NULL;
    v->bank = &g_banks[1];
    for (int i = 0; i < kVehicleVoiceCount; i++)
        v->voices[i] = Chance(97) ? &g_voices[i] : NULL;
    v->unknown1d0 = Chance(20) ? reinterpret_cast<AVehicleVoicePair *>(g_pairDummy) : NULL;
    v->carAlarm = Chance(30) ? &g_voices[14] : NULL;
    if (Chance(60)) {
        v->wind = static_cast<AVehicleWind *>(ArenaAlloc(sizeof(AVehicleWind)));
        RandomWind(v->wind, v);
    } else {
        v->wind = NULL;
    }
    if (Chance(50)) {
        v->horn = static_cast<AHorn *>(ArenaAlloc(sizeof(AHorn)));
        RandomHorn(v->horn);
    } else {
        v->horn = NULL;
    }
    v->asphaltMix = MixByName("Asphalt");
    v->offRoadMix = MixByName("OffRoad");
    v->skidsMix = MixByName("Skids");
    v->scrapesMix = MixByName("Scrapes");
}

AVehicle *Vehicle() {
    return reinterpret_cast<AVehicle *>(g_object);
}

// ---- the tests

void TestHorn() {
    AHorn *horn = reinterpret_cast<AHorn *>(g_object);
    for (int c = 0; c < 20; c++) {
        Inputs in = RandomInputs();
        float volume = Uniform(0.0f, 1.2f), pitch = Uniform(0.5f, 2.0f);
        Case("AHorn construct", in, sizeof(AHorn), [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 const char *patch = c & 1 ? "SFX_horn1" : "SFX_TruckHornLong";
                 if (original)
                     Orig_HornConstruct(horn, 0, patch, volume, pitch);
                 else
                     horn->Construct(patch, volume, pitch);
             },
             NoCapture);
    }
    for (int c = 0; c < 600; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        bool honk = Chance(60);
        Case("AHorn::Play", in, sizeof(AHorn),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomHorn(horn);
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original)
                     Orig_HornPlay(horn, 0, honk, &g_params);
                 else
                     horn->Play(honk, &g_params);
             },
             NoCapture);
    }
}

void TestVehicleLife() {
    for (int c = 0; c < 40; c++) {
        Inputs in = RandomInputs();
        int skids = c & 1 ? kVehicleSkids : kVehicleSqueals;
        unsigned flags = (c >> 1) & 1;
        int activate = c >> 2;
        Case("AVehicle construct, activate, delete", in, sizeof(AVehicle),
             [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 const char *mixName = c & 2 ? "Player Car" : "Traffic Cars";
                 if (original) {
                     Orig_VehicleConstruct(g_object, 0, mixName, skids);
                     for (int k = 0; k < 2; k++) {
                         if (activate & 1)
                             Orig_ActivateGun(g_object, 0, "Machine Gun");
                         if (activate & 2)
                             Orig_ActivateTurret(g_object, 0);
                         if (activate & 4)
                             Orig_ActivateZoom(g_object, 0);
                         if (activate & 8)
                             Orig_ActivateWind(g_object, 0);
                     }
                     Orig_ChooseHorn(g_object, 0, "horn2", 0.8f, 1.1f);
                     Orig_ChooseHorn(g_object, 0, "TruckHorn", 1.0f, 0.9f);
                     Orig_VehicleDelete(g_object, 0, flags);
                 } else {
                     AVehicle *v = Vehicle();
                     v->Construct(mixName, skids);
                     for (int k = 0; k < 2; k++) {
                         if (activate & 1)
                             v->ActivateGun("Machine Gun");
                         if (activate & 2)
                             v->ActivateTurret();
                         if (activate & 4)
                             v->ActivateZoom();
                         if (activate & 8)
                             v->ActivateWind();
                     }
                     v->ChooseHorn("horn2", 0.8f, 1.1f);
                     v->ChooseHorn("TruckHorn", 1.0f, 0.9f);
                     v->Delete(flags);
                 }
             },
             NoCapture);
    }
    for (int c = 0; c < 200; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        int which = c % 3;
        Case("AVehicle reset, destruct", in, sizeof(AVehicle),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomVehicle(Vehicle(), false);
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original) {
                     if (which == 0)
                         Orig_Reset(g_object, 0);
                     else if (which == 1)
                         Orig_VehicleDestruct(g_object, 0);
                     else
                         Orig_PairDestruct(g_pairDummy, 0);
                 } else {
                     if (which == 0)
                         Vehicle()->Reset();
                     else if (which == 1)
                         Vehicle()->Destruct();
                     else
                         reinterpret_cast<AVehicleVoicePair *>(g_pairDummy)->Destruct();
                 }
             },
             NoCapture);
    }
}

void TestVehicleQueries() {
    for (int c = 0; c < 1500; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        int which = c % 3;
        uint32_t answer[2] = {};
        Case("AVehicle speed, wheels, rpm", in, sizeof(AVehicle),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 AVehicle *v = Vehicle();
                 RandomVehicle(v, true);
                 int band = RandomInt(0, 4);
                 float length = band == 0 ? Near(5.0f, 0.5f) : band == 1 ? Near(14.0f, 0.5f)
                              : band == 2 ? Near(15.0f * float(RandomInt(1, 10)), 0.5f) : Uniform(0.0f, 160.0f);
                 if (Chance(2))
                     length = NAN;
                 float x = Uniform(-1.0f, 1.0f), y = Uniform(-1.0f, 1.0f), z = Uniform(-1.0f, 1.0f);
                 float scale = length / sqrtf(x * x + y * y + z * z + 1e-6f);
                 v->velocity = {x * scale, y * scale, z * scale};
                 if (which == 2)
                     v->speed = band == 2 ? Near(15.0f * float(RandomInt(0, 10)), 0.01f) : Uniform(-1.0f, 160.0f);
                 g_seed = keep;
             },
             [&](bool original) {
                 if (which == 0) {
                     if (original)
                         Orig_CalculateSpeed(g_object, 0);
                     else
                         Vehicle()->CalculateSpeed();
                 } else if (which == 1) {
                     int n = original ? Orig_GetAirborneWheels(g_object, 0) : Vehicle()->GetAirborneWheels();
                     answer[0] = uint32_t(n);
                 } else {
                     double rpm = original ? Orig_CalculateRpm(g_object, 0) : Vehicle()->CalculateRpm();
                     uint64_t bits = std::bit_cast<uint64_t>(rpm);
                     answer[0] = uint32_t(bits);
                     answer[1] = uint32_t(bits >> 32);
                 }
             },
             [&](std::vector<uint32_t> &out) { out.insert(out.end(), {answer[0], answer[1]}); });
    }
}

void TestLandingAndCollision() {
    static const float kThresholds[] = {0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.45f, 0.5f, 0.55f, 0.6f};
    for (int c = 0; c < 1500; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        Coord3 position = {Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f)};
        Coord3 velocity = {Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f)};
        float volume = Uniform(0.0f, 1.0f);
        Case("AVehicle::PlayLanding", in, sizeof(AVehicle),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomVehicle(Vehicle(), false);
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original)
                     Orig_PlayLanding(g_object, 0, volume, &position, &velocity);
                 else
                     Vehicle()->PlayLanding(volume, &position, &velocity);
             },
             NoCapture);
    }
    for (int c = 0; c < 3000; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        Coord3 position = {Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f)};
        Coord3 velocity = {Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f)};
        float strength = Chance(70) ? Near(kThresholds[RandomInt(0, 11)], 0.02f) : Uniform(-0.1f, 1.0f);
        if (Chance(10))
            strength = Near(kThresholds[RandomInt(0, 11)] - 0.3f, 0.01f);
        int surface = Chance(30) ? (RandomInt(0, 2) == 0 ? 1 : RandomInt(0, 1) ? 5 : 13) : RandomInt(0, 15);
        Case("AVehicle::PlayCollision", in, sizeof(AVehicle),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 AVehicle *v = Vehicle();
                 RandomVehicle(v, false);
                 if (Chance(70)) {
                     for (int32_t &t : v->scrapeTimers)
                         t = RandomInt(-3, 3);
                 }
                 if (Chance(40))
                     v->vehicleType = RandomInt(0, 1) ? kVehicleSoundSubmersible : kVehicleSoundSnowMobile;
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original)
                     Orig_PlayCollision(g_object, 0, strength, &position, &velocity, surface);
                 else
                     Vehicle()->PlayCollision(strength, &position, &velocity, surface);
             },
             NoCapture);
    }
}

void TestVehiclePlay() {
    for (int c = 0; c < 3000; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        Case("AVehicle::Play", in, sizeof(AVehicle),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 RandomVehicle(Vehicle(), false);
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original)
                     Orig_VehiclePlay(g_object, 0, &g_params);
                 else
                     Vehicle()->Play(&g_params);
             },
             NoCapture);
    }
}

void TestWind() {
    AVehicleWind *wind = reinterpret_cast<AVehicleWind *>(g_object);
    AVehicle *vehicle = reinterpret_cast<AVehicle *>(g_vehicle);
    for (int c = 0; c < 20; c++) {
        Inputs in = RandomInputs();
        Case("AVehicleWind construct, destruct", in, sizeof(AVehicleWind),
             [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 if (original) {
                     Orig_WindConstruct(wind, 0, vehicle);
                     if (c & 1)
                         Orig_WindDestruct(wind, 0);
                 } else {
                     wind->Construct(vehicle);
                     if (c & 1)
                         wind->Destruct();
                 }
             },
             NoCapture);
    }
    for (int c = 0; c < 1000; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        float frames = Chance(10) ? 0.0f : Uniform(0.0f, 8.0f);
        bool stop = c % 5 == 0;
        Case("AVehicleWind play, stop", in, sizeof(AVehicleWind),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 memset(g_vehicle, 0, sizeof(g_vehicle));
                 vehicle->speed = Chance(30) ? Near(5.0f, 0.5f) : Chance(20) ? Near(10.0f, 0.5f) : Uniform(0.0f, 40.0f);
                 RandomWind(wind, vehicle);
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original) {
                     if (stop)
                         Orig_WindStop(wind, 0, &g_params);
                     else
                         Orig_WindPlay(wind, 0, &g_params, frames);
                 } else {
                     if (stop)
                         wind->Stop(&g_params);
                     else
                         wind->Play(&g_params, frames);
                 }
             },
             NoCapture);
    }
}

void TestPlayParams() {
    for (int c = 0; c < 40; c++) {
        Inputs in = RandomInputs();
        int32_t view = RandomInt(-2, 5);
        float azimuth = Uniform(-1.0f, 1.0f);
        uint32_t answer = 0;
        Case("ListenerView::Azimuth", in, 4, [&] { memcpy(g_object, &view, 4); },
             [&](bool original) {
                 float a = original ? Orig_Azimuth(g_object, 0, azimuth)
                                    : reinterpret_cast<ListenerView *>(g_object)->Azimuth(azimuth);
                 answer = Bits(a);
             },
             [&](std::vector<uint32_t> &out) { out.push_back(answer); });
    }
    for (int c = 0; c < 3000; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        Case("SoundPlayParams::Compute", in, sizeof(ASoundPlayParams),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 ABaseSound *sound = reinterpret_cast<ABaseSound *>(g_sound);
                 RandomBase(sound, 0x0018a510);
                 RandomListener();
                 sound->minDistance = Chance(10) ? 0.0f : Uniform(0.5f, 30.0f);
                 sound->maxDistance = Chance(10) ? 0.0f : sound->minDistance + Uniform(0.0f, 60.0f);
                 int where = RandomInt(0, 4);
                 if (where == 0) {
                     // at the listener
                     for (int i = 0; i < 3; i++)
                         for (int j = 0; j < 3; j++)
                             g_listener.matrix.mtx[i][j] = i == j ? 1.0f : 0.0f;
                     sound->position = {-g_listener.matrix.mtx[3][0], -g_listener.matrix.mtx[3][1],
                                        -g_listener.matrix.mtx[3][2]};
                 } else if (where == 1) {
                     // the level between them, each way
                     g_listener.level = (sound->position.y + g_listener.position.y) * 0.5f +
                                            Uniform(-1.0f, 1.0f);
                 }
                 ASoundPlayParams *p = reinterpret_cast<ASoundPlayParams *>(g_object);
                 memset(p, 0x77, sizeof(*p));
                 p->listener = &g_listener;
                 g_seed = keep;
             },
             [&](bool original) {
                 ABaseSound *sound = reinterpret_cast<ABaseSound *>(g_sound);
                 if (original)
                     Orig_Compute(g_object, 0, sound);
                 else
                     reinterpret_cast<ASoundPlayParams *>(g_object)->Compute(sound);
             },
             NoCapture);
    }
}

void RandomEngine(AEngine *engine, AVehicle *vehicle, bool race) {
    uint8_t *b = reinterpret_cast<uint8_t *>(engine);
    for (size_t i = 0; i < sizeof(ARaceEngine); i++)
        b[i] = uint8_t(NextRandom() >> 24);
    engine->vtable = race ? kARaceEngineVtable : 0x001a3058;
    engine->pitch = Uniform(0.5f, 2.0f);
    engine->loadVolume = Uniform(0.0f, 1.0f);
    engine->idleVolume = Uniform(0.0f, 1.0f);
    engine->revTime = Chance(30) ? 0.0f : Chance(30) ? Near(60.0f, 3.0f) : Uniform(0.0f, 70.0f);
    engine->engineRpm = Chance(30) ? Near(2000.0f, 50.0f) : Uniform(-100.0f, 9000.0f);
    engine->rpmScale = Uniform(0.0001f, 0.001f);
    engine->engineThrottle = Chance(30) ? Near(0.2f, 0.02f) : Chance(30) ? Near(0.5f, 0.02f) : Uniform(0.0f, 1.0f);
    engine->vehicle = vehicle;
    if (race) {
        ARaceEngine *r = static_cast<ARaceEngine *>(engine);
        for (int i = 0; i < kRaceEngineVoices; i++) {
            r->voices[i][0] = &g_voices[i];
            r->voices[i][1] = &g_voices[5 + i];
        }
        r->idleBlend = Uniform(0.0f, 1.0f);
        r->loadBlend = Uniform(0.0f, 1.0f);
    }
}

void TestEngines() {
    AVehicle *vehicle = reinterpret_cast<AVehicle *>(g_vehicle);
    for (int c = 0; c < 30; c++) {
        Inputs in = RandomInputs();
        unsigned flags = c & 1;
        int which = (c >> 1) % 3;
        Case("AEngine/ARaceEngine construct, destruct, delete", in, sizeof(ARaceEngine),
             [&] { memset(g_object, 0xee, sizeof(g_object)); },
             [&](bool original) {
                 if (original) {
                     if (which == 0) {
                         Orig_EngineConstruct(g_object, 0);
                         Orig_EngineDelete(g_object, 0, flags);
                     } else {
                         Orig_RaceConstruct(g_object, 0);
                         if (which == 1)
                             Orig_RaceDelete(g_object, 0, flags);
                         else
                             Orig_RaceDestruct(g_object, 0);
                     }
                 } else {
                     if (which == 0) {
                         reinterpret_cast<AEngine *>(g_object)->Construct();
                         reinterpret_cast<AEngine *>(g_object)->Delete(flags);
                     } else {
                         ARaceEngine *r = reinterpret_cast<ARaceEngine *>(g_object);
                         r->Construct();
                         if (which == 1)
                             r->Delete(flags);
                         else
                             r->Destruct();
                     }
                 }
             },
             NoCapture);
    }
    for (int c = 0; c < 3000; c++) {
        Inputs in = RandomInputs();
        uint32_t seed = NextRandom();
        bool race = c % 4 != 0;
        Case(race ? "ARaceEngine::Play" : "AEngine::Play", in, sizeof(ARaceEngine),
             [&] {
                 uint32_t keep = g_seed;
                 g_seed = seed;
                 ArenaReset();
                 RandomVehicle(vehicle, false);
                 RandomEngine(reinterpret_cast<AEngine *>(g_object), vehicle, race);
                 RandomParams();
                 g_seed = keep;
             },
             [&](bool original) {
                 if (original) {
                     if (race)
                         Orig_RacePlay(g_object, 0, &g_params);
                     else
                         Orig_EnginePlay(g_object, 0, &g_params);
                 } else {
                     if (race)
                         reinterpret_cast<ARaceEngine *>(g_object)->Play(&g_params);
                     else
                         reinterpret_cast<AEngine *>(g_object)->Play(&g_params);
                 }
             },
             [&](std::vector<uint32_t> &out) { Append(out, g_vehicle, sizeof(AVehicle)); });
    }
    for (int c = 0; c < 40; c++) {
        Inputs in = RandomInputs();
        bool found = c & 1;
        float rate = Uniform(0.5f, 2.0f);
        uint32_t answer = 0;
        Case("AEngine::Get", in, sizeof(ARaceEngine),
             [&] {
                 memset(g_object, 0x44, sizeof(g_object));
                 g_refAnswer = found ? reinterpret_cast<AEngine *>(g_object) : NULL;
             },
             [&](bool original) {
                 const char *name = c & 2 ? "Vanquish" : "Tank";
                 AEngine *e = original ? Orig_EngineGet(name, vehicle, rate) : AEngine::Get(name, vehicle, rate);
                 answer = Word(e);
             },
             [&](std::vector<uint32_t> &out) { out.push_back(answer); });
    }
    for (int c = 0; c < 40; c++) {
        Inputs in = RandomInputs();
        int count = RandomInt(0, 4);
        int pattern = RandomInt(0, 255);
        Case("AEngine::RemoveAll", in, 4,
             [&] {
                 memset(g_refsBlock, 0, sizeof(g_refsBlock));
                 FakeRefs()->head = &g_head;
                 memset(&g_head, 0, sizeof(g_head));
                 g_nodeCount = count;
                 for (int i = 0; i < count; i++) {
                     memset(&g_nodes[i], 0, sizeof(g_nodes[i]));
                     memset(g_engines[i], 0, sizeof(g_engines[i]));
                     uint32_t vtable = Word(g_engineVtable);
                     memcpy(g_engines[i], &vtable, sizeof(vtable));
                     g_nodes[i].value.entry.object = g_engines[i];
                 }
                 g_head.left = count != 0 ? &g_nodes[0] : &g_head;
                 g_removePattern = pattern;
                 g_removeCalls = 0;
             },
             [&](bool original) {
                 if (original)
                     Orig_RemoveAll();
                 else
                     AEngine::RemoveAll();
             },
             NoCapture);
    }
    for (int c = 0; c < 10; c++) {
        Inputs in = RandomInputs();
        char key[0x80];
        for (char &k : key)
            k = char(RandomInt(1, 127));
        RefCounterEntry entry = {RandomInt(0, 9), &g_engines[0]};
        uint32_t answer = 0;
        Case("RefCounterPair::Construct", in, sizeof(RefCounterValue), [&] { memset(g_object, 0xee, 0x100); },
             [&](bool original) {
                 void *r = original ? Orig_PairConstruct(g_object, 0, key, &entry)
                                    : reinterpret_cast<RefCounterPair *>(g_object)->Construct(key, &entry);
                 answer = Word(r);
             },
             [&](std::vector<uint32_t> &out) { out.push_back(answer); });
    }
}

bool IsVehicle(uint32_t vtable) {
    for (uint32_t v : kAVehicleVtables) {
        if (v == vtable)
            return true;
    }
    return false;
}

// The live vehicles' sounds (and their horns and winds), and the live engines, copied before the fakes go in
struct LiveVehicle {
    uint8_t bytes[sizeof(AVehicle)];
    AHorn horn;
    AVehicleWind wind;
    bool hasHorn, hasWind;
};
std::vector<LiveVehicle> g_live;
std::vector<std::vector<uint8_t>> g_liveEngines;

void CollectLive() {
    ASoundList *list = fgSoundList;
    if (list != NULL && list->head != NULL) {
        int guard = 0;
        for (PointerListNode *node = list->Begin(); node != list->head && guard < 512; node = node->next, guard++) {
            ABaseSound *sound = static_cast<ABaseSound *>(node->value);
            if (sound == NULL || !IsVehicle(sound->vtable) || g_live.size() >= 12)
                continue;
            AVehicle *vehicle = static_cast<AVehicle *>(sound);
            LiveVehicle live;
            memcpy(live.bytes, vehicle, sizeof(AVehicle));
            live.hasHorn = vehicle->horn != NULL;
            live.hasWind = vehicle->wind != NULL;
            if (live.hasHorn)
                live.horn = *vehicle->horn;
            if (live.hasWind)
                live.wind = *vehicle->wind;
            g_live.push_back(live);
        }
    }
    EngineRefCounter *refs = EngineRefCounter::Get();
    if (refs != NULL && refs->head != NULL) {
        int guard = 0;
        for (RefCounterNode *node = refs->head->left; node != refs->head && guard < 16; guard++) {
            AEngine *engine = static_cast<AEngine *>(node->value.entry.object);
            if (engine != NULL && engine->vtable == kARaceEngineVtable) {
                const uint8_t *b = reinterpret_cast<const uint8_t *>(engine);
                g_liveEngines.push_back(std::vector<uint8_t>(b, b + sizeof(ARaceEngine)));
            }
            RefCounterIterator it = {node};
            it.Increment();
            node = it.node;
        }
    }
}

// A live vehicle's copy in g_object, its horn and wind copied into the arena, perturbed
void PlaceLive(const LiveVehicle &live) {
    memcpy(g_object, live.bytes, sizeof(AVehicle));
    AVehicle *v = Vehicle();
    if (live.hasHorn) {
        v->horn = static_cast<AHorn *>(ArenaAlloc(sizeof(AHorn)));
        *v->horn = live.horn;
        v->horn->honkTime += uint32_t(RandomInt(0, 2));
    }
    if (live.hasWind) {
        v->wind = static_cast<AVehicleWind *>(ArenaAlloc(sizeof(AVehicleWind)));
        *v->wind = live.wind;
        v->wind->vehicle = v;
    }
    v->throttle += Uniform(-0.3f, 0.3f);
    v->rpm += Uniform(-500.0f, 500.0f);
    v->carSpeed += Uniform(-3.0f, 3.0f);
    v->velocity.x += Uniform(-4.0f, 4.0f);
    v->velocity.z += Uniform(-4.0f, 4.0f);
    v->frontSlipTarget = Uniform(0.0f, 1.0f);
    v->rearSlipTarget = Uniform(0.0f, 1.0f);
    v->landingForce = Uniform(0.0f, 1.2f);
    v->hornHonk = Chance(40);
    for (int w = 0; w < 4; w++) {
        if (Chance(30))
            v->surfaces[w] = RandomSurface();
        if (Chance(30))
            v->previousSurfaces[w] = RandomSurface();
    }
    for (int32_t &t : v->scrapeTimers) {
        if (Chance(30))
            t = RandomInt(0, 12);
    }
}

void TestLive() {
    for (const LiveVehicle &live : g_live) {
        for (int c = 0; c < 120; c++) {
            Inputs in = RandomInputs();
            uint32_t seed = NextRandom();
            int which = c % 5;
            Coord3 position = {Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f), Uniform(-9.0f, 9.0f)};
            float strength = Uniform(0.0f, 0.7f);
            int surface = RandomInt(0, 13);
            uint32_t answer[2] = {};
            Case("live vehicle copies", in, sizeof(AVehicle),
                 [&] {
                     uint32_t keep = g_seed;
                     g_seed = seed;
                     PlaceLive(live);
                     RandomParams();
                     g_seed = keep;
                 },
                 [&](bool original) {
                     AVehicle *v = Vehicle();
                     switch (which) {
                     case 0:
                     case 1:
                         if (original)
                             Orig_VehiclePlay(v, 0, &g_params);
                         else
                             v->Play(&g_params);
                         break;
                     case 2: {
                         double rpm = original ? Orig_CalculateRpm(v, 0) : v->CalculateRpm();
                         uint64_t bits = std::bit_cast<uint64_t>(rpm);
                         answer[0] = uint32_t(bits);
                         answer[1] = uint32_t(bits >> 32);
                         break;
                     }
                     case 3:
                         if (original)
                             Orig_PlayLanding(v, 0, 1.0f, &position, &v->velocity);
                         else
                             v->PlayLanding(1.0f, &position, &v->velocity);
                         break;
                     default:
                         if (original)
                             Orig_PlayCollision(v, 0, strength, &position, &v->velocity, surface);
                         else
                             v->PlayCollision(strength, &position, &v->velocity, surface);
                         break;
                     }
                 },
                 [&](std::vector<uint32_t> &out) { out.insert(out.end(), {answer[0], answer[1]}); });
        }
    }
    for (const std::vector<uint8_t> &engine : g_liveEngines) {
        for (int c = 0; c < 200; c++) {
            Inputs in = RandomInputs();
            uint32_t seed = NextRandom();
            Case("live engine copies", in, sizeof(ARaceEngine),
                 [&] {
                     uint32_t keep = g_seed;
                     g_seed = seed;
                     memcpy(g_object, engine.data(), sizeof(ARaceEngine));
                     AVehicle *v = reinterpret_cast<AVehicle *>(g_vehicle);
                     if (!g_live.empty())
                         memcpy(g_vehicle, g_live[RandomInt(0, int(g_live.size()) - 1)].bytes, sizeof(AVehicle));
                     else
                         RandomVehicle(v, false);
                     v->throttle = Uniform(0.0f, 1.0f);
                     v->rpm = Uniform(0.0f, 8000.0f);
                     ARaceEngine *e = reinterpret_cast<ARaceEngine *>(g_object);
                     e->vehicle = v;
                     e->engineThrottle += Uniform(-0.2f, 0.2f);
                     RandomParams();
                     g_seed = keep;
                 },
                 [&](bool original) {
                     if (original)
                         Orig_RacePlay(g_object, 0, &g_params);
                     else
                         reinterpret_cast<ARaceEngine *>(g_object)->Play(&g_params);
                 },
                 NoCapture);
        }
    }
}

}  // namespace

void GameAudioShadowA_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEAUDIOSHADOWA");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    FpControlGet(&g_x87, &g_sse);

    Globals saved;
    SaveGlobals(&saved);
    CollectLive();
    memcpy(g_savedBanks, fgBanks, sizeof(g_savedBanks));
    for (int i = 0; i < kBankSlotCount; i++) {
        memset(&g_banks[i], 0, sizeof(ABank));
        g_banks[i].slot = i;
        g_banks[i].handle = 0x20 + i;
        fgBanks[i] = &g_banks[i];
    }

    HookInstall(0x0011d6a0, (const void *)&FakeMixAdd);
    HookInstall(0x0011d720, (const void *)&FakeMixGet);
    HookInstall(0x0011ca10, (const void *)&FakeMixGetVolume);
    HookInstall(0x00123ca0, (const void *)&FakeVoiceConstruct);
    HookInstall(0x00124690, (const void *)&FakeVoicePlay);
    HookInstall(0x00123c50, (const void *)&FakeViewDestruct);
    HookInstall(0x0003f400, (const void *)&FakeVoiceDestruct);
    HookInstall(0x00126d40, (const void *)&FakeLookup);
    HookInstall(0x000476f0, (const void *)&FakeOneShot);
    HookInstall(0x00114a60, (const void *)&FakeRrandom);
    HookInstall(0x00108a50, (const void *)&FakeRandomWord);
    HookInstall(0x0010a6c0, (const void *)&FakeTick);
    HookInstall(0x00114750, (const void *)&FakeFastAlloc);
    HookInstall(0x001147d0, (const void *)&FakeFastFree);
    HookInstall(0x001146a0, (const void *)&FakeNew);
    HookInstall(0x001146e0, (const void *)&FakeDelete);
    HookInstall(0x0011c910, (const void *)&FakeSoundNew);
    HookInstall(0x0011c830, (const void *)&FakeSoundDelete);
    HookInstall(0x0012e190, (const void *)&FakeSceneConstruct);
    HookInstall(0x0012e1e0, (const void *)&FakeSceneDestruct);
    HookInstall(0x00129550, (const void *)&FakeGunConstruct);
    HookInstall(0x00129470, (const void *)&FakeGunDestruct);
    HookInstall(0x00129250, (const void *)&FakeGunPlay);
    HookInstall(0x0012e220, (const void *)&FakeTurretConstruct);
    HookInstall(0x0012e250, (const void *)&FakeTurretDestruct);
    HookInstall(0x0012e2b0, (const void *)&FakeTurretPlay);
    HookInstall(0x00127dc0, (const void *)&FakeZoomConstruct);
    HookInstall(0x00128330, (const void *)&FakeZoomDestruct);
    HookInstall(0x00127e50, (const void *)&FakeZoomPlay);
    HookInstall(0x0012f8b0, (const void *)&FakeRefGet);
    HookInstall(0x00125270, (const void *)&FakeRefFind);
    HookInstall(0x0012f700, (const void *)&FakeRefAdd);
    HookInstall(0x0012f5b0, (const void *)&FakeRefRemove);

    TestHorn();
    TestVehicleLife();
    TestVehicleQueries();
    TestLandingAndCollision();
    TestVehiclePlay();
    TestWind();
    TestPlayParams();
    TestEngines();

    // the live objects read the real banks
    memcpy(fgBanks, g_savedBanks, sizeof(g_savedBanks));
    TestLive();

    HooksRemove();
    memcpy(fgBanks, g_savedBanks, sizeof(g_savedBanks));
    LoadGlobals(saved);

    printf("[gameaudioshadowa] engine sound: %ld cases, %ld checks, %ld differ (%ld calls faulted; %u live "
           "vehicles, %u live engines)\n",
           g_cases, g_checks, g_differ, g_faults, unsigned(g_live.size()), unsigned(g_liveEngines.size()));
    fflush(stdout);
}
