#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "GameAudioShadowB.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include "../../common/xbeOriginal.h"
#include "../../helpers.h"
#include "../audio/Bank.h"
#include "../audio/Mix.h"
#include "../audio/Sound.h"
#include "../audio/SoundManager.h"
#include "../audio/Voice.h"
#include "../data/StdStreams.h"
#include "../game/MissionManager.h"     // MissionCinematicMode
#include "../game/VehicleSound.h"
#include "../game/VehicleSoundKinds.h"
#include "../game/WeaponSound.h"

#include <windows.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_GAMEAUDIOSHADOWB=1: the vehicle kinds' sounds (game/VehicleSoundKinds.cpp) against the originals
// (their entry bytes swapped back in for each original call, common/xbeOriginal.h), on identical inputs, compared.
//
//   - Play of every kind, on copies of each live sound of the kind (the fgSoundList entries with its vtable) and on
//     sounds made up from nothing: inputs perturbed at random (surfaces, compressions, throttle, rpm, speeds,
//     velocity, camera vectors, buttons, the kind's own state, the frames since the last audio update, the
//     cinematic flag, the play parameters - volume, pitch, azimuth, facing, distance about the 1 and 2 thresholds,
//     the listener's view). The calls they make beyond the kinds are recording fakes: AVehicle::Play, AVoice's
//     Play, Set and constructor, the views' destructor, IsFinished and PlayOnce, AMix::GetVolume (a made-up volume
//     per mix), the one-shot sounds' constructors and destructor, the pools (an arena reset per side), the wind's
//     Play and Stop, ATurret::Play, AMenuSound::Trigger, NormalizedRandomNumber, and the engine's and siren's
//     virtual slots (objects of our own). The live pointers the copies hold are only passed to fakes.
//   - IsTracked, GetName, SetCreakLevel, PlayLanding, AddSiren.
//   - Every constructor (on a zeroed block), destructor and deleting destructor, with package A's and C's calls
//     (AVehicle's constructor, destructor and Activate*, AEngine::Get and RemoveAll, the wind's and turret's
//     constructors and destructors, AGun::SetFirePatch), AMix::Get, operator new/delete, ABasic's constructor and
//     the views' constructor faked too.
// Compared: the object's bytes, the fake wind's and gun's, the call records argument by argument, the counters.
// Everything written is put back: the frames since the last audio update, the cinematic flag, the counters.
//
// One mutation this catches: AUltraLite::PlayMotor easing the pitch by 0.9/0.1 rather than 0.95/0.05 changes
// motorPitch and the pitch of every voice it plays; ATrafficVehicle::Play stepping the gain by 1/8 rather than 16/127
// changes engineGain whenever the gain moves.
// ---------------------------------------------------------------------------------------------------------------

extern int32_t TrafficCountA;           // game/VehicleSoundKinds.cpp
extern int32_t TrafficCountB;

namespace {

const uint32_t kRanges[2][2] = {
    { 0x001284b0, 0x00129250 },     // APlayerVehicle, APlayerHeli
    { 0x00129b00, 0x0012d6f0 },     // AUltraLite to ATrafficVehicle
};

struct OriginalWindow {
    OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], true);
    }
    ~OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], false);
    }
};

#define OrigCinematic I32_AT(0x00243a64)       // the game's copy of MissionCinematicMode
#define OrigTrafficCountA I32_AT(0x00243b5c)
#define OrigTrafficCountB I32_AT(0x00243b60)

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[gameaudiob]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    size_t at = OwnedGlobals_FirstDifference(a, b, bytes);   // a vtable or callback: ours, or the game's
    if (at == bytes)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    char detail[96];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
#ifdef _MSC_VER
    __try {
        run(context, original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        return false;
    }
#else
    run(context, original);
    return true;
#endif
}

// ---- random inputs

uint32_t g_seed = 0x5eedb0b0;

uint32_t RandomWord() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

int RandomInt(int n) { return int(RandomWord() % uint32_t(n)); }

float RandomFloat(float lo, float hi) { return lo + (hi - lo) * float(RandomWord() & 0xffff) / 65535.0f; }

uint32_t Bits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}

uint32_t TextHash(const char *text) {
    if (text == NULL)
        return 0;
    uint32_t hash = 2166136261u;
    for (; *text != 0; text++)
        hash = (hash ^ uint8_t(*text)) * 16777619u;
    return hash;
}

// ---- the calls: recorded, side by side

struct Call {
    uint32_t function, w[7];
};

std::vector<Call> g_calls[2];
int g_side = 0;

void Record(uint32_t function, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0,
            uint32_t f = 0, uint32_t g = 0) {
    Call call = { function, { a, b, c, d, e, f, g } };
    g_calls[g_side].push_back(call);
}

// A pointer as recorded: the stack's (the landing sounds the snowmobiles make there) differ between the sides
uint32_t Word(const void *p) {
    char here;
    uintptr_t v = uintptr_t(p), sp = uintptr_t(&here);
    if (v + 0x10000 > sp && v < sp + 0x100000)
        return 0x5ac00000u;
    return uint32_t(v);
}

void CheckCalls(const char *what, int index) {
    g_checks++;
    if (g_calls[0].size() != g_calls[1].size()) {
        char detail[64];
        snprintf(detail, sizeof(detail), "original made %u calls, port %u", unsigned(g_calls[0].size()),
                 unsigned(g_calls[1].size()));
        Differ(what, index, detail);
        return;
    }
    for (size_t i = 0; i < g_calls[0].size(); i++) {
        if (memcmp(&g_calls[0][i], &g_calls[1][i], sizeof(Call)) != 0) {
            const Call &a = g_calls[0][i], &b = g_calls[1][i];
            char detail[160];
            snprintf(detail, sizeof(detail), "call %u: original %x(%x, %x, %x, %x), port %x(%x, %x, %x, %x)",
                     unsigned(i), a.function, a.w[1], a.w[2], a.w[3], a.w[4], b.function, b.w[1], b.w[2], b.w[3],
                     b.w[4]);
            Differ(what, index, detail);
            return;
        }
    }
}

// ---- the pools: an arena, reset for each side so both get the same blocks

alignas(16) uint8_t g_arena[0x10000];
uint32_t g_arenaUsed = 0;

void *ArenaAlloc(uint32_t size) {
    size = (size + 15) & ~15u;
    if (g_arenaUsed + size > sizeof(g_arena))
        return NULL;
    void *block = g_arena + g_arenaUsed;
    memset(block, 0xcd, size);
    g_arenaUsed += size;
    return block;
}

// ---- objects of our own

AMix g_fakeMixes[8];
AVoice g_fakeVoices[24];
alignas(4) uint8_t g_fakeWind[sizeof(AVehicleWind)];
alignas(4) uint8_t g_fakeGun[sizeof(AGun)];
AListener g_listener;

struct FakeObject {
    const void *vtable;
    uint8_t body[0x100];
};
const void *g_engineVtable[2];
const void *g_sirenVtable[5];
FakeObject g_fakeEngine, g_fakeSiren;

AMix *FakeMix(const char *name) { return &g_fakeMixes[TextHash(name) % 8]; }

// The case's made-up state the fakes answer from
double g_volumeBase = 0.5;
uint32_t g_finishedBits = 0, g_finishedCalls = 0, g_normalCalls = 0, g_normalSeed = 0;

// ---- the fakes: the play calls

void __fastcall FakeVehiclePlay(AVehicle *self, int, ASoundPlayParams *params) {
    Record(0x0011f9b0, Word(self), Word(params));
}
void __fastcall FakeVoicePlay(AVoice *voice, int, int view, float volume, float pitch, float azimuth, float delay,
                              float fxLevel) {
    Record(0x00124690, Word(voice), uint32_t(view), Bits(volume), Bits(pitch), Bits(azimuth), Bits(delay),
           Bits(fxLevel));
}
void __fastcall FakeVoiceSet(AVoice *voice, int, int bank, int patch) {
    Record(0x00123870, Word(voice), uint32_t(bank), uint32_t(patch));
}
AVoice *__fastcall FakeVoiceConstruct(AVoice *voice, int, AMix *mix, int bank, int patch) {
    Record(0x00123ca0, Word(voice), Word(mix), uint32_t(bank), uint32_t(patch));
    memset(voice, 0, sizeof(AVoice));
    voice->mix = mix;
    voice->views[0].bank = bank;
    voice->views[0].patch = patch;
    return voice;
}
void __fastcall FakeViewDestruct(AVoice::View *view, int) { Record(0x00123c50, Word(view)); }
AVoice::View *__fastcall FakeViewConstruct(AVoice::View *view, int) {
    Record(0x00123820, Word(view));
    memset(view, 0, sizeof(AVoice::View));
    return view;
}
bool __fastcall FakeIsFinished(AVoice *voice, int) {
    Record(0x000d36c0, Word(voice));
    uint32_t n = g_finishedCalls++;
    return ((g_finishedBits >> (n & 31)) & 1) != 0;
}
void __fastcall FakePlayOnce(AVoice *voice, int) { Record(0x0003e6b0, Word(voice)); }
double __fastcall FakeGetVolume(AMix *mix, int) {
    Record(0x0011ca10, Word(mix));
    uint32_t h = uint32_t(uintptr_t(mix)) * 2654435761u;
    return g_volumeBase + double(h >> 24) / 512.0;
}
void *FakeFastAlloc(unsigned int size, const char *name) {
    Record(0x00114750, size, TextHash(name));
    return ArenaAlloc(size);
}
void FakeFastFree(void *block, unsigned int size) { Record(0x001147d0, Word(block), size); }
void *FakeSoundNew(unsigned int size, const char *name) {
    Record(0x0011c910, size, TextHash(name));
    return ArenaAlloc(size);
}
void FakeSoundDelete(void *block, unsigned int size) { Record(0x0011c830, Word(block), size); }
void *__fastcall FakeOneShot(void *self, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity,
                             float volume, const char *mix) {
    Record(0x000476f0, Word(self), uint32_t(bank), uint32_t(patch), Word(position), Word(velocity), Bits(volume),
           TextHash(mix));
    return self;
}
void *__fastcall FakeOneShotAt(void *self, int, int bank, int patch, const Coord3 *position, float volume,
                               const char *mix, bool loop) {
    Record(0x000477d0, Word(self), uint32_t(bank), uint32_t(patch), Word(position), Bits(volume), TextHash(mix),
           uint32_t(loop));
    return self;
}
void __fastcall FakeOneShotDestruct(void *self, int) { Record(0x000478a0, Word(self)); }
void __fastcall FakeWindPlay(AVehicleWind *wind, int, ASoundPlayParams *params, float frames) {
    Record(0x0012e8f0, Word(wind), Word(params), Bits(frames));
}
void __fastcall FakeWindStop(AVehicleWind *wind, int, ASoundPlayParams *params) {
    Record(0x0012ea30, Word(wind), Word(params));
}
// The flags are compared at their width: only the low byte is the argument
void __fastcall FakeTurretPlay(ATurret *turret, int, uint32_t sideways, uint32_t upDown, uint32_t servo,
                               ASoundPlayParams *params) {
    Record(0x0012e2b0, Word(turret), sideways & 0xff, upDown & 0xff, servo & 0xff, Word(params));
}
void FakeTrigger(int bank, int patch, const char *mix, int view) {
    Record(0x0011df50, uint32_t(bank), uint32_t(patch), TextHash(mix), uint32_t(view));
}
double FakeNormal() {
    uint32_t n = g_normalCalls++;
    Record(0x00120db0, n);
    return double(int((g_normalSeed + n * 7919u) % 2001u) - 1000) / 400.0;
}
void __fastcall FakeEnginePlay(void *engine, int, ASoundPlayParams *params) {
    Record(0x100, Word(engine), Word(params));
}
void *__fastcall FakeSirenDelete(void *siren, int, unsigned int flags) {
    Record(0x101, Word(siren), flags);
    return siren;
}
void __fastcall FakeSirenPlay(void *siren, int, ASoundPlayParams *params) { Record(0x102, Word(siren), Word(params)); }
void __fastcall FakeSirenStop(void *siren, int, ASoundPlayParams *params) { Record(0x103, Word(siren), Word(params)); }

// ---- the fakes: the constructors' and destructors' calls

const uint32_t kAVehicleVtable = 0x001a2680;

AVehicle *__fastcall FakeVehicleConstruct(AVehicle *self, int, const char *mixName, int skidVoices) {
    Record(0x0011e2e0, Word(self), TextHash(mixName), uint32_t(skidVoices));
    self->vtable = kAVehicleVtable;
    self->mix = FakeMix(mixName);
    self->asphaltMix = FakeMix("Asphalt");
    self->gun = NULL;
    return self;
}
void __fastcall FakeVehicleDestruct(AVehicle *self, int) { Record(0x00120a10, Word(self)); }
void __fastcall FakeActivateGun(AVehicle *self, int, const char *mixName) {
    Record(0x0011e9a0, Word(self), TextHash(mixName));
    self->gun = reinterpret_cast<AGun *>(g_fakeGun);
}
void __fastcall FakeActivateZoom(AVehicle *self, int) { Record(0x0011ea80, Word(self)); }
void __fastcall FakeActivateWind(AVehicle *self, int) { Record(0x0011eaf0, Word(self)); }
AEngine *FakeEngineGet(const char *name, AVehicle *vehicle, float rate) {
    Record(0x0012f940, TextHash(name), Word(vehicle), Bits(rate));
    return reinterpret_cast<AEngine *>(&g_fakeEngine);
}
void FakeRemoveAll() { Record(0x0012f9e0); }
AVehicleWind *__fastcall FakeWindConstruct(void *block, int, AVehicle *vehicle) {
    Record(0x0012e830, Word(block), Word(vehicle));
    return static_cast<AVehicleWind *>(block);
}
void __fastcall FakeWindDestruct(AVehicleWind *wind, int) { Record(0x0012ea60, Word(wind)); }
ATurret *__fastcall FakeTurretConstruct(ATurret *turret, int) {
    Record(0x0012e220, Word(turret));
    return turret;
}
void __fastcall FakeTurretDestruct(ATurret *turret, int) { Record(0x0012e250, Word(turret)); }
AMix *FakeMixGet(const char *name) {
    Record(0x0011d720, TextHash(name));
    return FakeMix(name);
}
// The string is passed by value and destroyed by the callee, as the original does
void __fastcall FakeSetFirePatch(AGun *gun, int, GameStd::String patch) {
    Record(0x001296b0, Word(gun), TextHash(patch.Data()), patch.size);
    patch.Destruct();
}
void *FakeOperatorNew(unsigned int size) {
    Record(0x001146a0, size);
    return ArenaAlloc(size);
}
void FakeOperatorDelete(void *block) { Record(0x001146e0, Word(block)); }
ABasic *__fastcall FakeBasicConstruct(ABasic *self, int, const char *name, const char *mixName) {
    Record(0x0012fa40, Word(self), TextHash(name), TextHash(mixName));
    return self;
}

// ---- hooks

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[128];
int g_hookCount = 0;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount >= int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    h.on = VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old) != 0;
    if (!h.on)
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
}

// The original's entry, and the port its jump leads to (ported callers call the port directly)
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
    }
    g_hookCount = 0;
}

void InstallPlayFakes() {
    HookInstall(0x0011f9b0, (const void *)&FakeVehiclePlay);
    HookInstall(0x00124690, (const void *)&FakeVoicePlay);
    HookInstall(0x00123870, (const void *)&FakeVoiceSet);
    HookInstall(0x00123ca0, (const void *)&FakeVoiceConstruct);
    HookInstall(0x00123c50, (const void *)&FakeViewDestruct);
    HookInstall(0x000d36c0, (const void *)&FakeIsFinished);
    HookInstall(0x0003e6b0, (const void *)&FakePlayOnce);
    HookInstall(0x0011ca10, (const void *)&FakeGetVolume);
    HookInstall(0x00114750, (const void *)&FakeFastAlloc);
    HookInstall(0x001147d0, (const void *)&FakeFastFree);
    HookInstall(0x0011c910, (const void *)&FakeSoundNew);
    HookInstall(0x0011c830, (const void *)&FakeSoundDelete);
    HookInstall(0x000476f0, (const void *)&FakeOneShot);
    HookInstall(0x000477d0, (const void *)&FakeOneShotAt);
    HookInstall(0x000478a0, (const void *)&FakeOneShotDestruct);
    HookInstall(0x0012e8f0, (const void *)&FakeWindPlay);
    HookInstall(0x0012ea30, (const void *)&FakeWindStop);
    HookInstall(0x0012e2b0, (const void *)&FakeTurretPlay);
    HookInstall(0x0011df50, (const void *)&FakeTrigger);
    HookInstall(0x00120db0, (const void *)&FakeNormal);
}

void InstallLifeFakes() {
    HookInstall(0x0011e2e0, (const void *)&FakeVehicleConstruct);
    HookInstall(0x00120a10, (const void *)&FakeVehicleDestruct);
    HookInstall(0x0011e9a0, (const void *)&FakeActivateGun);
    HookInstall(0x0011ea80, (const void *)&FakeActivateZoom);
    HookInstall(0x0011eaf0, (const void *)&FakeActivateWind);
    HookInstall(0x0012f940, (const void *)&FakeEngineGet);
    HookInstall(0x0012f9e0, (const void *)&FakeRemoveAll);
    HookInstall(0x0012e830, (const void *)&FakeWindConstruct);
    HookInstall(0x0012ea60, (const void *)&FakeWindDestruct);
    HookInstall(0x0012e220, (const void *)&FakeTurretConstruct);
    HookInstall(0x0012e250, (const void *)&FakeTurretDestruct);
    HookInstall(0x0011d720, (const void *)&FakeMixGet);
    HookInstall(0x001296b0, (const void *)&FakeSetFirePatch);
    HookInstall(0x001146a0, (const void *)&FakeOperatorNew);
    HookInstall(0x001146e0, (const void *)&FakeOperatorDelete);
    HookInstall(0x0012fa40, (const void *)&FakeBasicConstruct);
    HookInstall(0x00123820, (const void *)&FakeViewConstruct);
}

// ---- running both sides on the same block

const uint32_t kMaxSize = 0x310;
alignas(16) uint8_t g_work[kMaxSize];
alignas(16) uint8_t g_before[kMaxSize];
alignas(16) uint8_t g_afterOriginal[kMaxSize];
uint8_t g_windBefore[sizeof(g_fakeWind)], g_windOriginal[sizeof(g_fakeWind)];
uint8_t g_gunBefore[sizeof(g_fakeGun)], g_gunOriginal[sizeof(g_fakeGun)];

void ResetSide(int side) {
    g_side = side;
    g_arenaUsed = 0;
    g_finishedCalls = 0;
    g_normalCalls = 0;
}

// The original (inside the window), then the port, each from the same bytes of the object, wind and gun
template <class F>
void Both(const char *what, int index, uint32_t size, F &&run) {
    typedef typename std::remove_reference<F>::type Run;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Run *>(context))(original); };
    memcpy(g_before, g_work, size);
    memcpy(g_windBefore, g_fakeWind, sizeof(g_fakeWind));
    memcpy(g_gunBefore, g_fakeGun, sizeof(g_fakeGun));
    int32_t countA = OrigTrafficCountA, countB = OrigTrafficCountB;
    int32_t portA = TrafficCountA, portB = TrafficCountB;
    g_calls[0].clear();
    g_calls[1].clear();
    bool ok;
    {
        OriginalWindow window;
        ResetSide(0);
        ok = Guarded(thunk, &run, true);
    }
    int32_t changeA = OrigTrafficCountA - countA, changeB = OrigTrafficCountB - countB;
    OrigTrafficCountA = countA;
    OrigTrafficCountB = countB;
    memcpy(g_afterOriginal, g_work, size);
    memcpy(g_windOriginal, g_fakeWind, sizeof(g_fakeWind));
    memcpy(g_gunOriginal, g_fakeGun, sizeof(g_fakeGun));
    memcpy(g_work, g_before, size);
    memcpy(g_fakeWind, g_windBefore, sizeof(g_fakeWind));
    memcpy(g_fakeGun, g_gunBefore, sizeof(g_fakeGun));
    ResetSide(1);
    ok = Guarded(thunk, &run, false) && ok;
    ResetSide(0);
    g_cases++;
    if (!ok) {
        Differ(what, index, "faulted");
        return;
    }
    CheckBytes(what, index, g_afterOriginal, g_work, size);
    CheckBytes(what, index, g_windOriginal, g_fakeWind, sizeof(g_fakeWind));
    CheckBytes(what, index, g_gunOriginal, g_fakeGun, sizeof(g_fakeGun));
    CheckCalls(what, index);
    int32_t counts[2][2] = { { changeA, changeB }, { TrafficCountA - portA, TrafficCountB - portB } };
    CheckBytes(what, index, counts[0], counts[1], sizeof(counts[0]));
    TrafficCountA = portA;
    TrafficCountB = portB;
}

// ---- the kinds

typedef void (__fastcall *PlayFn)(void *, int, ASoundPlayParams *);
typedef void *(__fastcall *Construct0Fn)(void *, int);
typedef void *(__fastcall *Construct1Fn)(void *, int, const char *);
typedef void *(__fastcall *Construct2Fn)(void *, int, const char *, const char *);
typedef void (__fastcall *DestructFn)(void *, int);
typedef void *(__fastcall *DeleteFn)(void *, int, unsigned int);

const int kSurfacePool[] = { 0, 1, 1, 2, 3, 4, 5, 8, 8, 9, 9, 10, 11, 15 };

int RandomSurface() { return kSurfacePool[RandomInt(int(sizeof(kSurfacePool) / sizeof(kSurfacePool[0])))]; }

AVoice *FakeVoice() { return &g_fakeVoices[RandomInt(24)]; }

Coord3 RandomVector(float range) {
    Coord3 v = { RandomFloat(-range, range), RandomFloat(-range, range), RandomFloat(-range, range) };
    return v;
}

// AVehicle's inputs as the vehicles and the sound manager leave them
void RandomBase(AVehicle *v) {
    for (int wheel = 0; wheel < 4; wheel++) {
        v->surfaces[wheel] = RandomSurface();
        v->previousSurfaces[wheel] = RandomInt(3) == 0 ? 0 : RandomSurface();
        v->suspensionCompression[wheel] = RandomInt(3) == 0 ? 0.0f : RandomFloat(0.0f, 0.35f);
    }
    v->throttle = RandomInt(4) == 0 ? 0.0f : RandomFloat(-0.3f, 1.0f);
    v->rpm = RandomFloat(0.0f, 9000.0f);
    v->rpmWobble = RandomFloat(-150.0f, 150.0f);
    v->speed = RandomInt(8) == 0 ? float(RandomInt(4) * 5) : RandomFloat(0.0f, 60.0f);
    v->carSpeed = RandomInt(4) == 0 ? 0.0f : RandomFloat(-30.0f, 30.0f);
    v->velocity = RandomVector(15.0f);
    if (RandomInt(6) == 0)
        v->velocity.y = float(RandomInt(5) * 5 - 10);
    v->cameraAim = RandomVector(1.0f);
    v->forward = RandomVector(1.0f);
    v->right = RandomVector(1.0f);
    v->pitch = RandomFloat(0.5f, 1.5f);
    v->volume = RandomFloat(0.0f, 1.0f);
    v->fading = RandomInt(2) != 0;
    v->fadeSteps = RandomInt(12);
    v->fadeStep = RandomInt(12);
    v->fade = RandomFloat(0.0f, 1.0f);
    v->gasButton = uint8_t(RandomInt(2));
    v->brakeButton = uint8_t(RandomInt(2));
    v->xyTrigger = uint8_t(RandomInt(3) == 0);
    v->xyReleaseTrigger = uint8_t(RandomInt(3) == 0);
    v->steering = RandomInt(3) == 0 ? 0.0f : RandomFloat(-1.0f, 1.0f);
    v->steeringVertical = RandomInt(3) == 0 ? 0.0f : RandomFloat(-1.0f, 1.0f);
    v->engineOn = uint8_t(RandomInt(4) != 0);
    v->servo = uint8_t(RandomInt(2));
    v->firing = uint8_t(RandomInt(3) == 0);
    v->turningSideways = uint8_t(RandomInt(2));
    v->turningUpDown = uint8_t(RandomInt(2));
    v->horn = RandomInt(2) ? NULL : reinterpret_cast<AHorn *>(&g_fakeSiren);
}

void RandomPlayerVehicle(uint8_t *object, bool live) {
    APlayerVehicle *s = reinterpret_cast<APlayerVehicle *>(object);
    s->engine = reinterpret_cast<AEngine *>(&g_fakeEngine);
    if (!live)
        s->roadVoice = RandomInt(4) == 0 ? NULL : FakeVoice();
    for (int wheel = 0; wheel < 4; wheel++)
        if (RandomInt(2))
            s->roadSurfaces[wheel] = RandomSurface();
}

void RandomPlayerHeli(uint8_t *object, bool live) {
    APlayerHeli *s = reinterpret_cast<APlayerHeli *>(object);
    if (!live)
        for (int i = 0; i < kCloseVoices; i++) {
            s->changeVoices[i] = FakeVoice();
            s->rotorVoices[i] = FakeVoice();
        }
    s->moving = RandomFloat(0.0f, 1.0f);
}

void RandomUltraLite(uint8_t *object, bool live) {
    AUltraLite *s = reinterpret_cast<AUltraLite *>(object);
    if (!live)
        for (int at = 0; at < kCloseVoices; at++)
            for (int kind = 0; kind < kUltraLiteVoices; kind++)
                s->voices[at][kind] = FakeVoice();
    s->motorPitch = RandomFloat(0.5f, 1.9f);
    s->levelGain = RandomFloat(0.0f, 1.0f);
    s->diveGain = RandomFloat(0.0f, 1.0f);
    s->climbGain = RandomFloat(0.0f, 1.0f);
}

void RandomSnowMobile(uint8_t *object, bool live) {
    ASnowMobile *s = reinterpret_cast<ASnowMobile *>(object);
    s->snowWind = reinterpret_cast<AVehicleWind *>(g_fakeWind);
    if (!live) {
        s->roadVoice = FakeVoice();
        s->snowVoice = FakeVoice();
        s->roadCarveVoice = FakeVoice();
        s->carveVoice = FakeVoice();
        s->idleVoice = FakeVoice();
        s->cruiseVoice = FakeVoice();
        s->carvingMix = &g_fakeMixes[1];
        s->motorMix = &g_fakeMixes[2];
    }
    s->revVoice = RandomInt(2) ? NULL : FakeVoice();
    s->airborneWheels = int16_t(RandomInt(5));
    s->jumped = RandomInt(2) != 0;
    s->rearLifted = RandomInt(2) != 0;
    s->revving = RandomInt(2) != 0;
    s->motorPitch = RandomFloat(0.0f, 1.5f);
    s->restCompression[0] = 0.119527f;
    s->restCompression[1] = 0.119531f;
    s->restCompression[2] = 0.075912f;
    s->restCompression[3] = 0.075912f;
    s->frontCompression = RandomFloat(0.0f, 0.4f);
    s->rearCompression = RandomFloat(0.0f, 0.4f);
    s->cruiseGain = RandomFloat(0.0f, 1.0f);
    s->cruiseLevel = RandomFloat(0.0f, 1.0f);
    s->smoothedRpm = RandomFloat(0.0f, 6000.0f);
}

void RandomSubmersible(uint8_t *object, bool live) {
    ASubmersible *s = reinterpret_cast<ASubmersible *>(object);
    if (!live) {
        s->jetVoice = FakeVoice();
        s->rudderVoice = FakeVoice();
        s->fanVoice = FakeVoice();
    }
    s->servoOnCount = int16_t(RandomInt(4) - 1);
    s->servoOffCount = int16_t(RandomInt(4) - 1);
    s->cavitationCount = int16_t(RandomInt(4) - 1);
    s->fansOn = RandomInt(2) != 0;
    s->rudderOn = RandomInt(2) != 0;
    s->jetOn = RandomInt(2) != 0;
    s->jetLevel = RandomFloat(0.0f, 0.3f);
    s->rudderLevel = RandomFloat(0.0f, 0.3f);
    s->levelStep = 0.06f;
    s->creakLevel = int16_t(RandomInt(5));
    s->creakPeriod = 10 + RandomInt(200);
    s->creakDelay = RandomInt(200);
    s->creakTime = RandomInt(3) == 0 ? s->creakDelay : RandomInt(200);
}

void RandomPlayerTank(uint8_t *object, bool live) {
    APlayerTank *s = reinterpret_cast<APlayerTank *>(object);
    s->engine = reinterpret_cast<AEngine *>(&g_fakeEngine);
    if (!live) {
        s->treadMix = &g_fakeMixes[3];
        s->treadVoice = FakeVoice();
    }
}

void RandomHelicopter(uint8_t *object, bool live) {
    AHelicopter *s = reinterpret_cast<AHelicopter *>(object);
    s->rotorLevel = RandomInt(3) == 0 ? 0.8f : RandomFloat(0.0f, 1.0f);
    s->landingTime = RandomInt(3) == 0 ? 0.0f : RandomFloat(-1.0f, 0.4f);
    s->pitchOffset = RandomFloat(0.0f, 0.2f);
}

void RandomTrafficVehicle(uint8_t *object, bool live) {
    ATrafficVehicle *s = reinterpret_cast<ATrafficVehicle *>(object);
    if (!live) {
        s->engineVoice = FakeVoice();
        s->exhaustVoice = FakeVoice();
        s->roadVoice = FakeVoice();
    }
    s->siren = RandomInt(3) == 0 ? reinterpret_cast<ABasic *>(&g_fakeSiren) : NULL;
    s->engineRpm = RandomFloat(0.0f, 10000.0f);
    s->engineGain = RandomFloat(0.0f, 1.0f);
    s->pitchOffset = RandomFloat(0.0f, 0.2f);
    s->sirenOn = RandomInt(2) != 0;
    s->airborneWheels = int16_t(RandomInt(5));
    s->jumped = RandomInt(2) != 0;
    s->rearLifted = RandomInt(2) != 0;
    if (RandomInt(2)) {
        s->restCompression[0] = 0.119527f;
        s->restCompression[1] = 0.119531f;
        s->restCompression[2] = 0.075912f;
        s->restCompression[3] = 0.075912f;
    }
    s->frontCompression = RandomFloat(0.0f, 0.4f);
    s->vehicleType = RandomInt(2) ? kVehicleSoundSnowMobile : kVehicleSoundPlayerCar;
}

// A live sound's pointers to objects the tests must not reach for real, pointed at ours
void Detach(uint32_t vtable, uint8_t *object) {
    if (vtable == 0x001a2abc)
        reinterpret_cast<APlayerVehicle *>(object)->engine = reinterpret_cast<AEngine *>(&g_fakeEngine);
    else if (vtable == 0x001a2e28)
        reinterpret_cast<APlayerTank *>(object)->engine = reinterpret_cast<AEngine *>(&g_fakeEngine);
    else if (vtable == 0x001a2c40)
        reinterpret_cast<ASnowMobile *>(object)->snowWind = reinterpret_cast<AVehicleWind *>(g_fakeWind);
    else if (vtable == 0x001a2ed8) {
        ATrafficVehicle *s = reinterpret_cast<ATrafficVehicle *>(object);
        if (s->siren != NULL)
            s->siren = reinterpret_cast<ABasic *>(&g_fakeSiren);
    }
}

struct Kind {
    const char *name;
    uint32_t vtable, size;
    uint32_t play, construct, destruct, del;
    int arguments;
    void (*randomize)(uint8_t *object, bool live);
    void (*portPlay)(void *object, ASoundPlayParams *params);
    void *(*portConstruct)(void *object, const char *first, const char *second);
    void (*portDestruct)(void *object);
    void *(*portDelete)(void *object, unsigned int flags);
};

#define KIND(Class, vtable, play, construct, destruct, del, arguments, randomize, constructCall)                   \
    { #Class, vtable, sizeof(Class), play, construct, destruct, del, arguments, randomize,                        \
      [](void *o, ASoundPlayParams *p) { static_cast<Class *>(o)->Play(p); },                                     \
      [](void *o, const char *first, const char *second) -> void * {                                              \
          (void)first; (void)second; return static_cast<Class *>(o)->constructCall; },                            \
      [](void *o) { static_cast<Class *>(o)->Destruct(); },                                                       \
      [](void *o, unsigned int flags) -> void * { return static_cast<Class *>(o)->Delete(flags); } }

const Kind kKinds[] = {
    KIND(APlayerVehicle, 0x001a2abc, 0x001286a0, 0x001284b0, 0x001285f0, 0x00128670, 1, RandomPlayerVehicle,
         Construct(first)),
    KIND(APlayerHeli, 0x001a2b14, 0x00128d10, 0x00128a40, 0x001290e0, 0x00129220, 0, RandomPlayerHeli, Construct()),
    KIND(AUltraLite, 0x001a2bc4, 0x00129b00, 0x0012a440, 0x00129b20, 0x0012a410, 0, RandomUltraLite, Construct()),
    KIND(ASnowMobile, 0x001a2c40, 0x0012b540, 0x0012b5f0, 0x0012a890, 0x0012b510, 1, RandomSnowMobile,
         Construct(first)),
    KIND(ASubmersible, 0x001a2d44, 0x0012c510, 0x0012b960, 0x0012be30, 0x0012c4e0, 1, RandomSubmersible,
         Construct(first)),
    KIND(APlayerTank, 0x001a2e28, 0x0012c640, 0x0012c550, 0x0012c730, 0x0012c7c0, 1, RandomPlayerTank,
         Construct(first)),
    KIND(AHelicopter, 0x001a2e58, 0x0012c950, 0x0012c7f0, 0x0012cac0, 0x0012cb60, 1, RandomHelicopter,
         Construct(first)),
    KIND(ATrafficVehicle, 0x001a2ed8, 0x0012d430, 0x0012cb90, 0x0012d300, 0x0012d400, 2, RandomTrafficVehicle,
         Construct(first, second)),
};
const int kKindCount = int(sizeof(kKinds) / sizeof(kKinds[0]));

// ---- the play tests

ASoundPlayParams g_params;

void RandomCase() {
    g_volumeBase = RandomFloat(0.0f, 0.6f);
    g_finishedBits = RandomWord() ^ (RandomWord() << 12);
    g_normalSeed = RandomWord();
    FramesSinceAudioUpdate = 1 + RandomInt(4);
    OrigCinematic = MissionCinematicMode = RandomInt(5) == 0;
    static const float kDistances[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f };
    float distance = RandomInt(3) == 0 ? kDistances[RandomInt(5)] : RandomFloat(0.0f, 3.0f);
    g_params.volume = RandomFloat(0.0f, 1.0f);
    g_params.pitch = RandomFloat(0.5f, 2.0f);
    g_params.azimuth = RandomFloat(-0.5f, 1.0f);
    g_params.facing = RandomFloat(-1.0f, 1.0f);
    g_params.unknown10 = RandomFloat(0.0f, 1.0f);
    g_params.distance = distance;
    g_listener.view = RandomInt(3);
    g_params.listener = &g_listener;
}

void PlayCases(const Kind &kind, const uint8_t *live, int rounds) {
    alignas(16) uint8_t base[kMaxSize];
    if (live != NULL) {
        memcpy(base, live, kind.size);
        Detach(kind.vtable, base);
    } else {
        memset(base, 0, kind.size);
        reinterpret_cast<ABaseSound *>(base)->vtable = kind.vtable;
        reinterpret_cast<ABaseSound *>(base)->mix = &g_fakeMixes[0];
        reinterpret_cast<AVehicle *>(base)->asphaltMix = &g_fakeMixes[4];
    }
    for (int round = 0; round < rounds; round++) {
        memcpy(g_work, base, kind.size);
        // a live sound's first round is as it is
        if (live == NULL || round > 0) {
            RandomBase(reinterpret_cast<AVehicle *>(g_work));
            kind.randomize(g_work, live != NULL);
        }
        RandomCase();
        char what[64];
        snprintf(what, sizeof(what), "%s::Play%s", kind.name, live != NULL ? " (live)" : "");
        Both(what, round, kind.size, [&](bool original) {
            if (original)
                ((PlayFn)kind.play)(g_work, 0, &g_params);
            else
                kind.portPlay(g_work, &g_params);
        });
    }
}

// The small methods
void SmallCases() {
    for (int round = 0; round < 200; round++) {
        memset(g_work, 0, sizeof(ATrafficVehicle));
        ATrafficVehicle *traffic = reinterpret_cast<ATrafficVehicle *>(g_work);
        traffic->vtable = 0x001a2ed8;
        traffic->vehicleType = RandomInt(8);
        strcpy(traffic->engineName, "750");
        uint32_t answers[2][2];
        Both("ATrafficVehicle::IsTracked/GetName", round, sizeof(ATrafficVehicle), [&](bool original) {
            if (original) {
                answers[0][0] = ((uint8_t (__fastcall *)(void *, int))0x0012ce10)(g_work, 0);
                answers[0][1] = Word(((char *(__fastcall *)(void *, int))0x0012ce20)(g_work, 0));
            } else {
                answers[1][0] = traffic->IsTracked();
                answers[1][1] = Word(traffic->GetName());
            }
        });
        CheckBytes("ATrafficVehicle::IsTracked/GetName", round, answers[0], answers[1], sizeof(answers[0]));

        traffic->siren = RandomInt(2) ? NULL : reinterpret_cast<ABasic *>(&g_fakeSiren);
        Both("ATrafficVehicle::AddSiren", round, sizeof(ATrafficVehicle), [&](bool original) {
            if (original)
                ((void (__fastcall *)(void *, int, const char *))0x0012ce30)(g_work, 0, "SFX_copsiren");
            else
                traffic->AddSiren("SFX_copsiren");
        });

        memset(g_work, 0, sizeof(ASubmersible));
        int16_t level = int16_t(RandomInt(9) - 2);
        int32_t period = int32_t(RandomWord());
        Both("ASubmersible::SetCreakLevel", round, sizeof(ASubmersible), [&](bool original) {
            if (original)
                ((void (__fastcall *)(void *, int, int16_t, int32_t))0x0012bb60)(g_work, 0, level, period);
            else
                reinterpret_cast<ASubmersible *>(g_work)->SetCreakLevel(level, period);
        });

        memset(g_work, 0, sizeof(AHelicopter));
        reinterpret_cast<AHelicopter *>(g_work)->landingTime = RandomInt(2) ? 0.0f : RandomFloat(-1.0f, 1.0f);
        Coord3 place = RandomVector(10.0f);
        float volume = RandomFloat(0.0f, 1.0f);
        Both("AHelicopter::PlayLanding", round, sizeof(AHelicopter), [&](bool original) {
            if (original)
                ((void (__fastcall *)(void *, int, float, const Coord3 *, const Coord3 *))0x0012c930)(g_work, 0, volume,
                                                                                                      &place, &place);
            else
                reinterpret_cast<AHelicopter *>(g_work)->PlayLanding(volume, &place, &place);
        });
    }
}

// The constructors on a zeroed block, then the destructors and deleting destructors on what the port made
void LifeCases() {
    static const char *const kEngines[] = { "suv", "BMW", "Vanquish", "tank", "supsled", "oppsled", "750", "Nsub" };
    for (int k = 0; k < kKindCount; k++) {
        const Kind &kind = kKinds[k];
        for (int round = 0; round < 8; round++) {
            const char *first = kind.arguments == 0 ? NULL : kEngines[(round + k) % 8];
            const char *second = round & 1 ? "Opponents" : "Traffic Cars";
            memset(g_work, 0, kind.size);
            memset(g_fakeGun, 0, sizeof(g_fakeGun));
            uint32_t answers[2];
            char what[64];
            snprintf(what, sizeof(what), "%s::Construct", kind.name);
            Both(what, round, kind.size, [&](bool original) {
                void *made;
                if (!original)
                    made = kind.portConstruct(g_work, first, second);
                else if (kind.arguments == 0)
                    made = ((Construct0Fn)kind.construct)(g_work, 0);
                else if (kind.arguments == 1)
                    made = ((Construct1Fn)kind.construct)(g_work, 0, first);
                else
                    made = ((Construct2Fn)kind.construct)(g_work, 0, first, second);
                answers[original ? 0 : 1] = Word(made);
            });
            CheckBytes(what, round, &answers[0], &answers[1], sizeof(answers[0]));

            // the made sound: its arena blocks stay in the arena's first bytes, which both sides reuse alike
            snprintf(what, sizeof(what), "%s::Destruct", kind.name);
            alignas(16) uint8_t made[kMaxSize];
            memcpy(made, g_work, kind.size);
            Both(what, round, kind.size, [&](bool original) {
                if (original)
                    ((DestructFn)kind.destruct)(g_work, 0);
                else
                    kind.portDestruct(g_work);
            });
            unsigned int flags = round & 1;
            memcpy(g_work, made, kind.size);
            snprintf(what, sizeof(what), "%s::Delete", kind.name);
            Both(what, round, kind.size, [&](bool original) {
                void *answer = original ? ((DeleteFn)kind.del)(g_work, 0, flags) : kind.portDelete(g_work, flags);
                answers[original ? 0 : 1] = Word(answer);
            });
            CheckBytes(what, round, &answers[0], &answers[1], sizeof(answers[0]));
        }
    }
}

// The live sounds of a kind (fgSoundList's entries with its vtable)
int LiveSounds(uint32_t vtable, const uint8_t **out, int most) {
    int count = 0;
    ASoundList *list = fgSoundList;
    if (list == NULL || list->head == NULL)
        return 0;
    for (PointerListNode *node = list->head->next; node != list->head && count < most; node = node->next) {
        const ABaseSound *sound = static_cast<const ABaseSound *>(node->value);
        if (sound != NULL && sound->vtable == vtable)
            out[count++] = reinterpret_cast<const uint8_t *>(sound);
    }
    return count;
}

}  // namespace

void GameAudioShadowB_Run(void) {
    const char *setting = getenv("NIGHTFIRE_GAMEAUDIOSHADOWB");
    if (setting == NULL || atoi(setting) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    int frames = FramesSinceAudioUpdate, cinematic = OrigCinematic, portCinematic = MissionCinematicMode;

    g_engineVtable[0] = (const void *)&FakeEnginePlay;
    g_engineVtable[1] = (const void *)&FakeSirenDelete;
    g_fakeEngine.vtable = g_engineVtable;
    g_sirenVtable[0] = (const void *)&FakeSirenDelete;
    g_sirenVtable[3] = (const void *)&FakeSirenPlay;
    g_sirenVtable[4] = (const void *)&FakeSirenStop;
    g_fakeSiren.vtable = g_sirenVtable;
    for (int i = 0; i < 8; i++)
        g_fakeMixes[i].volume = 1.0f;

    InstallPlayFakes();
    int live = 0;
    for (int k = 0; k < kKindCount; k++) {
        const uint8_t *sounds[6];
        int count = LiveSounds(kKinds[k].vtable, sounds, 6);
        live += count;
        for (int i = 0; i < count; i++)
            PlayCases(kKinds[k], sounds[i], 300);
        PlayCases(kKinds[k], NULL, 1500);
    }
    InstallLifeFakes();
    SmallCases();
    LifeCases();
    HooksRemove();

    FramesSinceAudioUpdate = frames;
    OrigCinematic = cinematic;
    MissionCinematicMode = portCinematic;
    printf("[gameaudiob] the vehicle kinds' sounds (%d live): %d cases, %d checks, %d differ (%d faults)\n", live,
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
