#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "GameEffectsShadowA.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include "../anim/AnimEngine.h"             // Handle
#include "../camera/PlayerCamera.h"
#include "../eagl/RenderContext.h"
#include "../engine/UMemory.hpp"
#include "../game/BondCar.h"
#include "../game/Debris.h"
#include "../game/EmpEffect.h"               // REmpBolts
#include "../game/Simulation.h"
#include "../game/TyreTrack.h"
#include "../game/Vehicle.h"
#include "../game/VehicleRender.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../render/Materials.h"
#include "../render/RenderHigh.h"
#include "../render/Renderer.h"
#include "../render/RSceneObj.hpp"
#include "../world/World.h"                 // ArticleEffect
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_GAMEEFFECTSSHADOWA=1, from the first simulation tick: the GE_A package (RVehicle, RTyreTrack, RDebris,
// RDebrisType, FUN_00095f90) against the originals, the package's three address ranges swapped back for each
// original run. Every case runs both sides on one working copy (the same address, so pointers into it compare as
// they are) from one state - the copy, the live car it belongs to (its PBondCar, CarPhysics and animation handle),
// the renderer's random sequence, the game tick, SimStep and the simulation's step, the shared buffers
// (GAME::SwitchData, EAGL::DamageZones), the tyre tracks' strip buffers, colours, materials and material index and
// the debris colours (the game's copies for the original, ours for the port), and the volatile material's current
// request (its pointers into our globals compared as the game's) - and compares all of it afterwards, with a log of
// the calls the draws make.
//
// The callees that draw or reach other packages are recording fakes, each jumped to at its entry (and at its
// port's, for a ported one our code calls directly): RMuzzleFlash::Draw and DrawPOV, RVehicleParticle::UpdateCar,
// TriggerTireSmoke and TriggerSparks, REmp::DrawShootingEffect, REmpBolts' constructor, RRenderSharedData::
// SendPerViewPort, RSceneObj::RenderEffects, EAGL::Model::Draw, RSkeletalObj::UpdatePosition, XAPI's GetLocalTime
// (a time the case picks), UVolatileMaterial::Draw (its request's vertices, colours and texture coordinates copied
// into the log), the render context's SetZWritesEnable, and the viewport's EndView, SetViewMatrix and BeginView.
//
//   - FUN_00095f90 (through its register adaptor and its core) and GetNeedlePos on random inputs; GetRenderOffset
//     and ResolveObjectData on every live RVehicle.
//   - On copies of every live RVehicle (cars and helicopters), with the car's controls, wheels, suspension, class,
//     submarine flag and damage zones perturbed: ComputeDamage (also without physics), ComputeWheels (the bones a
//     private copy), UpdatePosition (the needles: the player's car), SetPerObjectBuffers and ClearSharedBuffers
//     over perturbed ticks, TriggerIlluminate, SetEMPVictimEffect, TriggerTurboBoostSmoke, TriggerEmpBolts (the
//     bolts' constructor faked, the blocks freed), TriggerFX's own types (sparks, colour dissolve, muzzle flash,
//     switches), StartMuzzleFlash (the large type and the turret's), StartColourDissolve over variations,
//     RenderShadowGeometry, RenderFX and RenderDeferredEffects with the flashes, the EMP state and the particles
//     perturbed.
//   - Tyre tracks: copies of the live cars' tracks (and made-up ones), Add with perturbed points, slip, spin,
//     surfaces (3, 8, 9 and others), ring states and snow, on a copy of the wheel's WWorldPos; Break; Draw over
//     whole rings (the batches, the breaks, the snow textures); the constructor on scratch memory; Init.
//   - Debris on private copies of the 23 kinds: Init, Add, Draw over made-up pieces and SimSteps (pieces dropped),
//     RDebris::Add, Reset, Draw and DrawShellCasings (queued casings on a live car's effects).
// Tested in game by the lockstep runs: the constructor, destructor and PostLoad, InitSharedBuffers (the tuning),
// Render and RenderShadow (every car's draw), RDebris's constructor.
//
// Mutations this catches: RTyreTrack::Draw's break test reading the next point's joined flag (the logged strip
// differs wherever a break falls inside a ring); ComputeWheels dropping the hub by the compression rather than the
// smaller of it and SPRING_COMPRESSION_LIMIT_DRAW (the bones differ whenever the perturbed compression is over it);
// Add's spacing masks swapped (the ring's count and head differ).
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[3][2] = {
    { 0x000953e0, 0x00096820 }, { 0x000a8f10, 0x000a9870 }, { 0x000abfc0, 0x000ac870 },
};

void OriginalWindow(bool original) {
    for (const uint32_t *range : kRanges)
        XbeOriginal_RestoreRange(range[0], range[1], original);
}

// ---- the originals

typedef RVehicle *(__fastcall *VehicleFn)(RVehicle *, int);
typedef void (__fastcall *VoidFn)(void *, int);
typedef float (__fastcall *FloatFn)(RVehicle *, int);
typedef double (__fastcall *DoubleFn)(RVehicle *, int);
typedef double (__fastcall *NeedleFn)(RVehicle *, int, int, float);
typedef void *(__fastcall *ResolveFn)(RVehicle *, int, const char *, bool *);
typedef void (__fastcall *BoolFn)(void *, int, bool);
typedef void (__fastcall *IntFn)(void *, int, int);
typedef void (__fastcall *IlluminateFn)(RVehicle *, int, int, int32_t, int);
typedef void (__fastcall *MuzzleFn)(RVehicle *, int, const Coord3 *, const Coord3 *, int, int);
typedef void (__fastcall *DissolveFn)(RVehicle *, int, uint32_t, uint32_t);
typedef void (__fastcall *TriggerFXFn)(RVehicle *, int, int, uint32_t, uint32_t, const Coord3 *, const Coord3 *, float);
typedef void (__fastcall *RenderFXFn)(RVehicle *, int, const MATRIX4 *);
typedef void (*StaticFn)(void);
typedef RTyreTrack *(__fastcall *TrackConstructFn)(void *, int, float, bool, bool);
typedef void (__fastcall *TrackAddFn)(RTyreTrack *, int, const Coord4 *, float, float, int, WWorldPos *);
typedef void (__fastcall *DebrisInitFn)(RDebrisType *, int, uint32_t);
typedef void (__fastcall *DebrisTypeAddFn)(RDebrisType *, int, const Coord4 *, const Coord4 *, uint32_t, uint32_t, float,
                                           uint32_t);
typedef void (__fastcall *DebrisAddFn)(RDebris *, int, int, const Coord4 *, const Coord4 *, uint32_t, float, uint32_t);

#define Orig_ResolveObjectData ((ResolveFn)0x00095560)
#define Orig_ClearSharedBuffers ((StaticFn)0x00095970)
#define Orig_TriggerIlluminate ((IlluminateFn)0x000959b0)
#define Orig_SetEMPVictimEffect ((BoolFn)0x000959d0)
#define Orig_TriggerTurboBoostSmoke ((IntFn)0x000959f0)
#define Orig_TriggerEmpBolts ((VoidFn)0x00095a10)
#define Orig_ComputeDamage ((VoidFn)0x00095a80)
#define Orig_RenderFX ((RenderFXFn)0x00095b60)
#define Orig_GetNeedlePos ((NeedleFn)0x00095c60)
#define Orig_GetRenderOffset ((DoubleFn)0x00095cc0)
#define Orig_StartMuzzleFlash ((MuzzleFn)0x00095ce0)
#define Orig_StartColourDissolve ((DissolveFn)0x00095e20)
#define Orig_TriggerFX ((TriggerFXFn)0x00095ed0)
#define Orig_SetPerObjectBuffers ((VoidFn)0x00095ff0)
#define Orig_ComputeWheels ((FloatFn)0x00096060)
#define Orig_RenderShadowGeometry ((VoidFn)0x00096410)
#define Orig_UpdatePosition ((BoolFn)0x000964b0)
#define Orig_RenderDeferredEffects ((VoidFn)0x00096770)
#define Orig_DebrisReset ((VoidFn)0x000a8f10)
#define Orig_DebrisTypeInit ((DebrisInitFn)0x000a8f30)
#define Orig_DebrisTypeAdd ((DebrisTypeAddFn)0x000a8fa0)
#define Orig_DebrisAdd ((DebrisAddFn)0x000a9260)
#define Orig_DebrisTypeDraw ((VoidFn)0x000a92a0)
#define Orig_DebrisDraw ((VoidFn)0x000a9660)
#define Orig_DrawShellCasings ((VoidFn)0x000a9680)
#define Orig_TrackConstruct ((TrackConstructFn)0x000abfc0)
#define Orig_TrackInit ((StaticFn)0x000ac050)
#define Orig_TrackBreak ((IntFn)0x000ac170)
#define Orig_TrackDraw ((VoidFn)0x000ac250)
#define Orig_TrackAdd ((TrackAddFn)0x000ac4c0)

// ---- the game's state

#define GxRandomSeed U32_AT(OwnedGlobals_Ours(0x001c45c4))
#define GxRandomMultiplier U32_AT(OwnedGlobals_Ours(0x001c45c8))
#define GxGameTick U32_AT(OwnedGlobals_Ours(0x001f2a4c))
#define GxSimStep FLOAT_AT(OwnedGlobals_Ours(0x001c465c))
#define GxSimStepCount U32_AT(OwnedGlobals_Ours(0x00234e34))
#define GxPlayerVehicles (*(PhysicsObject ***)OwnedGlobals_Ours(0x00234e40))
#define GxSharedBuffers ((uint8_t *)OwnedGlobals_Ours(0x001f2a50))              // GAME::SwitchData, EAGL::DamageZones
const size_t kSharedBufferBytes = 0x200;
#define GxDamageLevelEffects FLOAT_AT(OwnedGlobals_Ours(0x001c3dd8))            // fgDamageLevels.effects
const size_t kDebrisColourBytes = 0x200;
const size_t kTrackColourBytes = 0x800;
const size_t kTrackMaterialBytes = 4 * 0x60;
#define GxVolatileRequestIndex U32_AT(OwnedGlobals_Ours(0x00243830))
#define GxVolatileRequests ((TexturedGeoPrim **)OwnedGlobals_Ours(0x00243878))
// The game's copies of the globals the port owns
#define OrigDebrisColours ((uint8_t *)0x00202a90)
#define OrigTrackColours ((uint8_t *)0x00233620)
#define OrigTrackMaterials ((uint8_t *)0x00233e20)
#define OrigClearEmpVictim I32_AT(0x001f2c6c)
#define OrigTrackTexCoords ((uint8_t *)0x00230610)
#define OrigTrackVertices ((uint8_t *)0x00231610)
#define OrigTrackTextures ((EAGL::TAR **)0x00233610)
#define OrigTrackMaterialIndex U32_AT(0x00233fa0)
#define GxDebris (*(RDebris **)OwnedGlobals_Ours(0x00202a80))

const int kOwners = 0x40;
const size_t kHandleLimit = 0x10000;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[geffectsA]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    size_t at = OwnedGlobals_FirstDifference(a, b, bytes);   // a vtable or callback: ours, or the game's
    if (at == bytes)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    char detail[96];
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
    Differ(what, index, detail);
}

void CheckU32(const char *what, int index, uint32_t a, uint32_t b) {
    g_checks++;
    if (a == b)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "original %08x, port %08x", a, b);
    Differ(what, index, detail);
}

void CheckDouble(const char *what, int index, double a, double b) {
    g_checks++;
    if (memcmp(&a, &b, sizeof(a)) == 0)
        return;
    char detail[96];
    snprintf(detail, sizeof(detail), "original %.17g, port %.17g", a, b);
    Differ(what, index, detail);
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(context, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(context, original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x6ea0a11u;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

int Range(int lo, int hi) {
    return lo + int(Next() % uint32_t(hi - lo + 1));
}

bool Chance(int in) {
    return Next() % uint32_t(in) == 0;
}

// ---- jumps to the fakes

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

bool HookInstall(Hook *h, uint32_t at, const void *to) {
    h->at = at;
    h->on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(h->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h->on = true;
    return true;
}

void HookRemove(Hook *h) {
    if (!h->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(h->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(h->at), h->saved, 5);
    VirtualProtect((void *)uintptr_t(h->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(h->at), 5);
    h->on = false;
}

const void *JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return NULL;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return reinterpret_cast<const void *>(uintptr_t(at + 5 + relative));
}

// A callee faked for both sides: the jump at its address, and, where that address already jumps to a port, the
// port's own entry too (our code calls it directly)
struct Fake {
    uint32_t address;
    const void *fake;
    Hook entry, port;
};

void FakeInstall(Fake *f) {
    const void *port = JumpTarget(f->address);
    HookInstall(&f->entry, f->address, f->fake);
    f->port.on = false;
    if (port != NULL)
        HookInstall(&f->port, uint32_t(uintptr_t(port)), f->fake);
}

void FakeRemove(Fake *f) {
    HookRemove(&f->port);
    HookRemove(&f->entry);
}

// ---- the call log

struct Record {
    uint32_t kind;
    std::vector<uint32_t> words;
    std::vector<uint8_t> blob;
};
typedef std::vector<Record> CallLog;
CallLog *g_log = NULL;

void Log(uint32_t kind, std::initializer_list<uint32_t> words, const void *blob = NULL, size_t size = 0) {
    if (g_log == NULL)
        return;
    Record r;
    r.kind = kind;
    r.words.assign(words.begin(), words.end());
    if (blob != NULL && size != 0)
        r.blob.assign(static_cast<const uint8_t *>(blob), static_cast<const uint8_t *>(blob) + size);
    g_log->push_back(r);
}

uint32_t Word(const void *p) {
    return uint32_t(uintptr_t(p));
}

// A pointer into one of the globals the port owns as the same byte of the game's copy (what the original's
// pointer would be); any other pointer as it is
uint32_t ToGame(const void *p) {
    struct Owned {
        const void *ours;
        uint32_t game;
        size_t size;
    };
    const Owned owned[] = {
        { TrackTexCoords, 0x00230610, sizeof(TrackTexCoords) }, { TrackVertices, 0x00231610, sizeof(TrackVertices) },
        { TrackColours, 0x00233620, kTrackColourBytes }, { TrackMaterials, 0x00233e20, kTrackMaterialBytes },
        { DebrisColours, 0x00202a90, kDebrisColourBytes },
    };
    uint32_t at = Word(p);
    for (const Owned &o : owned) {
        uint32_t lo = Word(o.ours);
        if (at >= lo && at < lo + o.size)
            return o.game + (at - lo);
    }
    return at;
}

void CheckLogs(const char *what, int index, const CallLog &a, const CallLog &b) {
    g_checks++;
    char detail[96];
    if (a.size() != b.size()) {
        snprintf(detail, sizeof(detail), "%u calls, port %u", unsigned(a.size()), unsigned(b.size()));
        Differ(what, index, detail);
        return;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].kind != b[i].kind || a[i].words != b[i].words || a[i].blob != b[i].blob) {
            snprintf(detail, sizeof(detail), "call %u (kind %u, port's kind %u) differs", unsigned(i), a[i].kind,
                     b[i].kind);
            Differ(what, index, detail);
            return;
        }
    }
}

enum CallKind : uint32_t {
    kCallMuzzleDraw = 1,
    kCallMuzzleDrawPOV,
    kCallUpdateCar,
    kCallShootingEffect,
    kCallSendPerViewPort,
    kCallRenderEffects,
    kCallTireSmoke,
    kCallSparks,
    kCallEmpBolts,
    kCallModelDraw,
    kCallSkeletalUpdate,
    kCallLocalTime,
    kCallVolatileDraw,
    kCallZWrites,
    kCallEndView,
    kCallBeginView,
    kCallViewMatrix,
};

// ---- the fakes

alignas(16) MATRIX4 g_transform;

// A transform handed on: the case's own, or one the caller built on its stack (compared by value)
uint32_t TransformWord(const MATRIX4 *transform) {
    return transform == &g_transform ? 1 : 0;
}

void FakeMuzzleDraw(const MATRIX4 *transform, const Coord4 *position, const Coord4 *direction, int type, float size) {
    Log(kCallMuzzleDraw, { TransformWord(transform), Word(position), Word(direction), uint32_t(type), Bits(size) },
        transform, sizeof(*transform));
}

void FakeMuzzleDrawPOV(const MATRIX4 *transform, const Coord4 *position, int type, float size) {
    Log(kCallMuzzleDrawPOV, { TransformWord(transform), Word(position), uint32_t(type), Bits(size) }, transform,
        sizeof(*transform));
}

void __fastcall FakeUpdateCar(void *self, int, bool active) {
    Log(kCallUpdateCar, { Word(self), uint32_t(active) });
}

void FakeShootingEffect(const MATRIX4 *transform, float intensity) {
    Log(kCallShootingEffect, { TransformWord(transform), Bits(intensity) }, transform, sizeof(*transform));
}

void FakeSendPerViewPort() {
    Log(kCallSendPerViewPort, {});
}

void __fastcall FakeRenderEffects(void *self, int, bool glaresOnly) {
    Log(kCallRenderEffects, { Word(self), uint32_t(glaresOnly) });
}

void __fastcall FakeTireSmoke(void *self, int, int ticks) {
    Log(kCallTireSmoke, { Word(self), uint32_t(ticks) });
}

void __fastcall FakeSparks(void *self, int, const Coord3 *position, Coord3 velocity, float intensity, uint32_t system) {
    Log(kCallSparks, { Word(self), Word(position), Bits(velocity.x), Bits(velocity.y), Bits(velocity.z),
                       Bits(intensity), system });
}

void *__fastcall FakeEmpBolts(void *self, int, RVehicle *vehicle) {
    Log(kCallEmpBolts, { Word(vehicle) });
    return self;
}

void __fastcall FakeModelDraw(void *self, int, const float *matrix) {
    Log(kCallModelDraw, { Word(self), Word(matrix) });
}

void __fastcall FakeSkeletalUpdate(void *self, int, bool update) {
    Log(kCallSkeletalUpdate, { Word(self), uint32_t(update) });
}

uint16_t g_localTime[8];

void __stdcall FakeLocalTime(uint16_t *time) {
    memcpy(time, g_localTime, sizeof(g_localTime));
    Log(kCallLocalTime, {});
}

void __fastcall FakeVolatileDraw(void *self, int, int primitive, int count, MATRIX4 *transform) {
    TexturedGeoPrim *request = GxVolatileRequests[GxVolatileRequestIndex];
    std::vector<uint8_t> data;
    if (transform != NULL)
        data.insert(data.end(), reinterpret_cast<uint8_t *>(transform), reinterpret_cast<uint8_t *>(transform + 1));
    if (request != NULL && count > 0 && count <= 0x200) {
        const uint8_t *positions = request->positions.data, *colours = request->colours.data;
        const uint8_t *uvs = request->texCoords.data;
        if (positions != NULL)
            data.insert(data.end(), positions, positions + count * 16);
        if (colours != NULL)
            data.insert(data.end(), colours, colours + count * 4);
        if (uvs != NULL)
            data.insert(data.end(), uvs, uvs + count * 8);
    }
    const uint8_t *state = static_cast<const uint8_t *>(self);
    data.insert(data.end(), state, state + sizeof(EAGL::GeoPrimState));
    Log(kCallVolatileDraw, { ToGame(self), uint32_t(primitive), uint32_t(count), transform != NULL,
                             request != NULL ? Word(request->texture.data) : 0 },
        data.data(), data.size());
}

bool __fastcall FakeZWrites(void *self, int, uint8_t enable) {
    Log(kCallZWrites, { Word(self), enable });
    return true;
}

void __fastcall FakeEndView(void *self, int) {
    Log(kCallEndView, { Word(self) });
}

void __fastcall FakeBeginView(void *self, int) {
    Log(kCallBeginView, { Word(self) });
}

void __fastcall FakeViewMatrix(void *self, int, const float *matrix) {
    Log(kCallViewMatrix, { Word(self) }, matrix, 16 * sizeof(float));
}

Fake g_fakes[] = {
    { 0x000a1870, (const void *)&FakeMuzzleDraw },
    { 0x000a19d0, (const void *)&FakeMuzzleDrawPOV },
    { 0x000a7ac0, (const void *)&FakeUpdateCar },
    { 0x0009bb30, (const void *)&FakeShootingEffect },
    { 0x0007e1c0, (const void *)&FakeSendPerViewPort },
    { 0x0008f740, (const void *)&FakeRenderEffects },
    { 0x000a6f10, (const void *)&FakeTireSmoke },
    { 0x000a6f70, (const void *)&FakeSparks },
    { 0x0009dc90, (const void *)&FakeEmpBolts },
    { 0x000ea210, (const void *)&FakeModelDraw },
    { 0x00091360, (const void *)&FakeSkeletalUpdate },
    { 0x0010e684, (const void *)&FakeLocalTime },
    { 0x0011c290, (const void *)&FakeVolatileDraw },
    { 0x000e73f0, (const void *)&FakeZWrites },
    { 0x000e49a0, (const void *)&FakeEndView },
    { 0x000e4be0, (const void *)&FakeBeginView },
    { 0x000e4ef0, (const void *)&FakeViewMatrix },
};

// ---- the state every case starts from, and compares

struct Shared {
    uint32_t seed, multiplier;
    uint32_t gameTick;
    float simStep;
    uint32_t simStepCount;
    float damageLevelEffects;
    int32_t clearEmpVictim;         // the side's own copy: the game's for the original, ours for the port
    uint32_t trackMaterialIndex;    // ditto
    uint8_t sharedBuffers[kSharedBufferBytes];
    uint8_t debrisColours[kDebrisColourBytes];      // ditto
    uint8_t trackColours[kTrackColourBytes];        // ditto
    uint8_t trackMaterials[kTrackMaterialBytes];    // ditto
    uint8_t trackVertices[sizeof(TrackVertices)];   // ditto
    uint8_t request[sizeof(TexturedGeoPrim)];
};

void GetShared(Shared *s, bool original) {
    s->seed = GxRandomSeed;
    s->multiplier = GxRandomMultiplier;
    s->gameTick = GxGameTick;
    s->simStep = GxSimStep;
    s->simStepCount = GxSimStepCount;
    s->damageLevelEffects = GxDamageLevelEffects;
    s->clearEmpVictim = original ? OrigClearEmpVictim : ClearEmpVictim;
    s->trackMaterialIndex = original ? OrigTrackMaterialIndex : TrackMaterialIndex;
    memcpy(s->sharedBuffers, GxSharedBuffers, kSharedBufferBytes);
    memcpy(s->debrisColours, original ? OrigDebrisColours : (uint8_t *)DebrisColours, kDebrisColourBytes);
    memcpy(s->trackColours, original ? OrigTrackColours : (uint8_t *)TrackColours, kTrackColourBytes);
    memcpy(s->trackMaterials, original ? OrigTrackMaterials : (uint8_t *)TrackMaterials, kTrackMaterialBytes);
    memcpy(s->trackVertices, original ? OrigTrackVertices : (uint8_t *)TrackVertices, sizeof(TrackVertices));
    TexturedGeoPrim *request = GxVolatileRequests[GxVolatileRequestIndex];
    if (request != NULL)
        memcpy(s->request, request, sizeof(s->request));
}

// A request's data pointers into the port's owned globals as the game's
void RequestToGame(uint8_t *request) {
    for (size_t offset = offsetof(TexturedGeoPrim, state.data); offset < sizeof(TexturedGeoPrim); offset += 8) {
        uint32_t word;
        memcpy(&word, request + offset, 4);
        word = ToGame(reinterpret_cast<const void *>(uintptr_t(word)));
        memcpy(request + offset, &word, 4);
    }
}

// The state for a side; the port's owned globals lent to the game's copies for the original
void PutShared(const Shared *s, bool original) {
    GxRandomSeed = s->seed;
    GxRandomMultiplier = s->multiplier;
    GxGameTick = s->gameTick;
    GxSimStep = s->simStep;
    GxSimStepCount = s->simStepCount;
    GxDamageLevelEffects = s->damageLevelEffects;
    OrigClearEmpVictim = s->clearEmpVictim;
    ClearEmpVictim = s->clearEmpVictim;
    OrigTrackMaterialIndex = s->trackMaterialIndex;
    TrackMaterialIndex = s->trackMaterialIndex;
    memcpy(GxSharedBuffers, s->sharedBuffers, kSharedBufferBytes);
    memcpy(OrigDebrisColours, s->debrisColours, kDebrisColourBytes);
    memcpy(DebrisColours, s->debrisColours, kDebrisColourBytes);
    memcpy(OrigTrackColours, s->trackColours, kTrackColourBytes);
    memcpy(TrackColours, s->trackColours, kTrackColourBytes);
    memcpy(OrigTrackMaterials, s->trackMaterials, kTrackMaterialBytes);
    memcpy(TrackMaterials, s->trackMaterials, kTrackMaterialBytes);
    memcpy(OrigTrackVertices, s->trackVertices, sizeof(TrackVertices));
    memcpy(TrackVertices, s->trackVertices, sizeof(TrackVertices));
    memcpy(OrigTrackTexCoords, TrackTexCoords, sizeof(TrackTexCoords));
    memcpy(OrigTrackTextures, TrackTextures, sizeof(TrackTextures));
    TexturedGeoPrim *request = GxVolatileRequests[GxVolatileRequestIndex];
    if (request != NULL)
        memcpy(request, s->request, sizeof(s->request));
    (void)original;
}

void CheckShared(const char *what, int index, const Shared &a, const Shared &b) {
    CheckU32(what, index, a.seed, b.seed);
    CheckU32(what, index, a.gameTick, b.gameTick);
    CheckU32(what, index, a.simStepCount, b.simStepCount);
    CheckU32(what, index, a.clearEmpVictim, b.clearEmpVictim);
    CheckU32(what, index, a.trackMaterialIndex, b.trackMaterialIndex);
    CheckBytes(what, index, a.sharedBuffers, b.sharedBuffers, kSharedBufferBytes);
    CheckBytes(what, index, a.debrisColours, b.debrisColours, kDebrisColourBytes);
    CheckBytes(what, index, a.trackColours, b.trackColours, kTrackColourBytes);
    CheckBytes(what, index, a.trackMaterials, b.trackMaterials, kTrackMaterialBytes);
    CheckBytes(what, index, a.trackVertices, b.trackVertices, sizeof(TrackVertices));
    uint8_t requestA[sizeof(TexturedGeoPrim)], requestB[sizeof(TexturedGeoPrim)];
    memcpy(requestA, a.request, sizeof(requestA));
    memcpy(requestB, b.request, sizeof(requestB));
    RequestToGame(requestA);
    RequestToGame(requestB);
    CheckBytes(what, index, requestA, requestB, sizeof(requestA));
}

Shared g_live;          // as the shadow found it
Shared g_input;
Shared g_out[2];

// ---- the live vehicles and what a case may write of them

struct LiveVehicle {
    PhysicsObject *object;
    RVehicle *render;
    bool isBondCar;
};
std::vector<LiveVehicle> g_vehicles;

struct CarState {
    uint8_t car[sizeof(PBondCar)];
    uint8_t carPhysics[sizeof(CarPhysics)];
    uint8_t handle[kHandleLimit];
};

const LiveVehicle *g_vehicle = NULL;
size_t g_carBytes = 0, g_handleBytes = 0;
CarState g_carLive, g_carOut[2];

void BindVehicle(const LiveVehicle *v) {
    g_vehicle = v;
    g_carBytes = v->isBondCar ? sizeof(PBondCar) : sizeof(PhysicsObject);
    Handle *handle = v->render->animHandle;
    g_handleBytes = 0;
    if (handle != NULL && sizeof(Handle) + handle->dataSize <= kHandleLimit)
        g_handleBytes = sizeof(Handle) + handle->dataSize;
}

CarPhysics *CarPhysicsOf(const LiveVehicle *v) {
    return v->isBondCar ? static_cast<PBondCar *>(static_cast<PVehicle *>(v->object))->physics : NULL;
}

void GetCar(CarState *s) {
    memcpy(s->car, g_vehicle->object, g_carBytes);
    if (CarPhysicsOf(g_vehicle) != NULL)
        memcpy(s->carPhysics, CarPhysicsOf(g_vehicle), sizeof(CarPhysics));
    if (g_handleBytes != 0)
        memcpy(s->handle, g_vehicle->render->animHandle, g_handleBytes);
}

void PutCar(const CarState *s) {
    memcpy(g_vehicle->object, s->car, g_carBytes);
    if (CarPhysicsOf(g_vehicle) != NULL)
        memcpy(CarPhysicsOf(g_vehicle), s->carPhysics, sizeof(CarPhysics));
    if (g_handleBytes != 0)
        memcpy(g_vehicle->render->animHandle, s->handle, g_handleBytes);
}

void CheckCar(const char *what, int index, const CarState &a, const CarState &b) {
    CheckBytes(what, index, a.car, b.car, g_carBytes);
    if (CarPhysicsOf(g_vehicle) != NULL)
        CheckBytes(what, index, a.carPhysics, b.carPhysics, sizeof(CarPhysics));
    if (g_handleBytes != 0)
        CheckBytes(what, index, a.handle, b.handle, g_handleBytes);
}

// The working copy both sides run on, and its private bones
alignas(16) uint8_t g_workBytes[sizeof(RVehicle)];
RVehicle *const g_work = reinterpret_cast<RVehicle *>(g_workBytes);
const uint32_t kMaxBones = 64;
alignas(16) MATRIX4 g_bones[kMaxBones];

struct VehicleInput {
    uint8_t work[sizeof(RVehicle)];
    MATRIX4 bones[kMaxBones];
    CarState car;
};

struct VehicleOutput {
    uint8_t work[sizeof(RVehicle)];
    MATRIX4 bones[kMaxBones];
    CallLog log;
    double result;
};

VehicleInput g_vehicleInput;
VehicleOutput g_vehicleOut[2];

// A copy of the vehicle's render object to perturb, with private bones
void CopyVehicle(VehicleInput *in) {
    memcpy(in->work, g_vehicle->render, sizeof(RVehicle));
    RVehicle *copy = reinterpret_cast<RVehicle *>(in->work);
    uint32_t bones = copy->numBones < kMaxBones ? copy->numBones : kMaxBones;
    memset(in->bones, 0, sizeof(in->bones));
    if (copy->bones != NULL)
        memcpy(in->bones, copy->bones, bones * sizeof(MATRIX4));
    copy->numBones = bones;
    copy->bones = g_bones;
    for (int w = 0; w < RVehicle::kWheels; w++) {
        if (copy->hubBone[w] < 0 || uint32_t(copy->hubBone[w]) > bones)
            copy->hubBone[w] = 0;
        if (copy->wheelBone[w] < 0 || uint32_t(copy->wheelBone[w]) > bones)
            copy->wheelBone[w] = 0;
    }
    GetCar(&in->car);
}

typedef void (*VehicleCaseFn)(bool original);
VehicleCaseFn g_vehicleCase = NULL;
// TriggerEmpBolts' new block: freed, and the pointer made the same on both sides
bool g_releaseBolts = false;

void ReleaseBolts(RVehicle *out) {
    const RVehicle *in = reinterpret_cast<const RVehicle *>(g_vehicleInput.work);
    if (out->empBolts != NULL && out->empBolts != in->empBolts) {
        OperatorDelete(out->empBolts);
        out->empBolts = reinterpret_cast<REmpBolts *>(1);
    }
}

void RunVehicleCase(void *, bool original) {
    g_vehicleCase(original);
}

// One case over the working copy: both sides from the same state, everything compared
void VehicleCase(const char *what, int index, VehicleCaseFn run) {
    g_cases++;
    g_vehicleCase = run;
    for (int side = 0; side < 2; side++) {
        bool original = side == 0;
        PutShared(&g_input, original);
        PutCar(&g_vehicleInput.car);
        memcpy(g_workBytes, g_vehicleInput.work, sizeof(RVehicle));
        memcpy(g_bones, g_vehicleInput.bones, sizeof(g_bones));
        VehicleOutput &out = g_vehicleOut[side];
        out.log.clear();
        out.result = 0.0;
        g_log = &out.log;
        Guarded(RunVehicleCase, NULL, original);
        g_log = NULL;
        if (g_releaseBolts)
            ReleaseBolts(g_work);
        memcpy(out.work, g_workBytes, sizeof(RVehicle));
        memcpy(out.bones, g_bones, sizeof(g_bones));
        GetShared(&g_out[side], original);
        GetCar(&g_carOut[side]);
    }
    CheckBytes(what, index, g_vehicleOut[0].work, g_vehicleOut[1].work, sizeof(RVehicle));
    CheckBytes(what, index, g_vehicleOut[0].bones, g_vehicleOut[1].bones, sizeof(g_bones));
    CheckDouble(what, index, g_vehicleOut[0].result, g_vehicleOut[1].result);
    CheckLogs(what, index, g_vehicleOut[0].log, g_vehicleOut[1].log);
    CheckShared(what, index, g_out[0], g_out[1]);
    CheckCar(what, index, g_carOut[0], g_carOut[1]);
    PutCar(&g_carLive);
}

double &Result(bool original) {
    return g_vehicleOut[original ? 0 : 1].result;
}

// ---- inputs shared by several cases

int g_argInt[4];
float g_argFloat[2];
alignas(16) Coord3 g_point, g_direction;
const char *g_name = NULL;

void RandomTransform() {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            g_transform.mtx[r][c] = Uniform(-2.0f, 2.0f);
    g_transform.mtx[3][3] = 1.0f;
}

// The car's inputs to the wheels, needles and damage
void PerturbCar(RVehicle *copy) {
    if (g_vehicle->isBondCar) {
        PBondCar *car = reinterpret_cast<PBondCar *>(g_vehicleInput.car.car);
        car->carSteer = Chance(6) ? Uniform(-0.3f, 0.3f) : Uniform(-0.08f, 0.08f);
        car->wheelSpinAngle[0] = Uniform(-1.0f, 2.0f);
        car->wheelSpinAngle[1] = Uniform(-1.0f, 2.0f);
        for (int w = 0; w < kCarWheels; w++)
            car->suspensionCompression[w] = Uniform(-0.2f, 0.6f);
        car->carSpeed = Uniform(-5.0f, 90.0f);
        car->reversing = Chance(3);
        car->control.steeringVertical = Uniform(-1.0f, 1.0f);
        car->control.brake = Uniform(0.0f, 1.0f);
        if (Chance(3))
            car->carClass = Range(0, 4);
        for (int z = 0; z < kDamageZoneCount; z++) {
            car->damageZones[z].unknown00 = Chance(2) ? 0.0f : Uniform(0.0f, 1.0f);
            car->damageZones[z].unknown04 = Chance(2) ? 0.0f : Uniform(0.0f, 2.0f);
        }
        CarPhysics *physics = reinterpret_cast<CarPhysics *>(g_vehicleInput.car.carPhysics);
        if (Chance(3))
            physics->isSub = !physics->isSub;
        if (Chance(3))
            physics->isSnowmobile = !physics->isSnowmobile;
        physics->springCompressionLimitDraw = Uniform(0.0f, 0.4f);
        physics->springRestLength = Uniform(0.2f, 0.6f);
        physics->wheelRadius = Uniform(0.2f, 0.5f);
    }
    copy->steer = Uniform(-0.08f, 0.08f);
    copy->pitch = Uniform(-0.3f, 0.3f);
    copy->rudder = Uniform(-0.5f, 0.5f);
    for (int w = 0; w < RVehicle::kWheels; w++) {
        copy->wheelHeight[w] = Uniform(-0.5f, 0.5f);
        if (Chance(4))
            copy->hubBone[w] = Range(0, int(copy->numBones));
        if (Chance(4))
            copy->wheelBone[w] = Range(0, int(copy->numBones));
    }
}

// ---- the pure ones

__declspec(naked) float CallFade(uint32_t entry, uint32_t now, int32_t start) {
    __asm {
        mov eax, [esp + 8]
        mov ecx, [esp + 12]
        push ebx
        mov ebx, [esp + 8]
        call ebx
        pop ebx
        ret
    }
}

void TestFade() {
    for (int i = 0; i < 400; i++) {
        uint32_t now = Next();
        int32_t start;
        switch (Range(0, 4)) {
        case 0: start = 0; break;
        case 1: start = -int32_t(Next() & 0xff); break;
        case 2: start = int32_t(now) + Range(-1, 3); break;
        default: start = int32_t(now) - Range(0, 40); break;
        }
        if (start == 0 && Chance(2))
            start = 1;
        g_cases++;
        OriginalWindow(true);
        float original = CallFade(0x00095f90, now, start);
        OriginalWindow(false);
        float adaptor = CallFade(uint32_t(uintptr_t(&FUN_00095f90)), now, start);
        float core = SwitchEffectFade(now, start);
        CheckU32("FUN_00095f90 adaptor", i, Bits(original), Bits(adaptor));
        CheckU32("SwitchEffectFade", i, Bits(original), Bits(core));
    }
}

void TestNeedles() {
    if (g_vehicles.empty())
        return;
    RVehicle *any = g_vehicles[0].render;
    for (int i = 0; i < 300; i++) {
        int needle = Range(0, 6);
        float value = Chance(10) ? NAN : Uniform(-100.0f, 8000.0f);
        if (Chance(3))
            value = Uniform(-2.0f, 70.0f);
        g_cases++;
        OriginalWindow(true);
        double original = Orig_GetNeedlePos(any, 0, needle, value);
        OriginalWindow(false);
        CheckDouble("GetNeedlePos", i, original, any->GetNeedlePos(needle, value));
    }
}

void TestOffsetsAndNames() {
    static const char *const names[] = { "EAGL::DamageZones", "EAGL::DamageZone", "EAGL::DamageZones2",
                                         "GAME::SwitchData", "", "EAGL::damageZones" };
    int index = 0;
    for (const LiveVehicle &v : g_vehicles) {
        g_cases++;
        OriginalWindow(true);
        double original = Orig_GetRenderOffset(v.render, 0);
        OriginalWindow(false);
        CheckDouble("GetRenderOffset", index, original, v.render->GetRenderOffset());
        for (const char *name : names) {
            g_cases++;
            bool found[2] = { true, true };
            OriginalWindow(true);
            void *a = Orig_ResolveObjectData(v.render, 0, name, &found[0]);
            OriginalWindow(false);
            void *b = v.render->ResolveObjectData(name, &found[1]);
            CheckU32("ResolveObjectData", index, Word(a), Word(b));
            CheckU32("ResolveObjectData found", index, found[0], found[1]);
        }
        index++;
    }
}

// ---- the vehicle cases

void CaseComputeDamage(bool original) {
    if (original)
        Orig_ComputeDamage(g_work, 0);
    else
        g_work->ComputeDamage();
}

void CaseComputeWheels(bool original) {
    if (original)
        Result(true) = Orig_ComputeWheels(g_work, 0);
    else
        Result(false) = g_work->ComputeWheels();
}

void CaseUpdatePosition(bool original) {
    if (original)
        Orig_UpdatePosition(g_work, 0, g_argInt[0] != 0);
    else
        g_work->UpdatePosition(g_argInt[0] != 0);
}

void CaseBuffers(bool original) {
    if (g_argInt[0] == 0) {
        if (original)
            Orig_SetPerObjectBuffers(g_work, 0);
        else
            g_work->SetPerObjectBuffers();
    } else {
        if (original)
            Orig_ClearSharedBuffers();
        else
            RVehicle::ClearSharedBuffers();
    }
}

std::vector<void *> g_boltBlocks;

void CaseEffect(bool original) {
    switch (g_argInt[0]) {
    case 0:
        if (original)
            Orig_TriggerIlluminate(g_work, 0, g_argInt[1], g_argInt[2], g_argInt[3]);
        else
            g_work->TriggerIlluminate(g_argInt[1], g_argInt[2], g_argInt[3]);
        break;
    case 1:
        if (original)
            Orig_SetEMPVictimEffect(g_work, 0, g_argInt[1] != 0);
        else
            g_work->SetEMPVictimEffect(g_argInt[1] != 0);
        break;
    case 2:
        if (original)
            Orig_TriggerTurboBoostSmoke(g_work, 0, g_argInt[1]);
        else
            g_work->TriggerTurboBoostSmoke(g_argInt[1]);
        break;
    case 3:
        if (original)
            Orig_TriggerEmpBolts(g_work, 0);
        else
            g_work->TriggerEmpBolts();
        break;
    case 4:
        if (original)
            Orig_TriggerFX(g_work, 0, g_argInt[1], uint32_t(g_argInt[2]), uint32_t(g_argInt[3]), &g_point, &g_direction,
                           g_argFloat[0]);
        else
            g_work->TriggerFX(g_argInt[1], uint32_t(g_argInt[2]), uint32_t(g_argInt[3]), &g_point, &g_direction,
                              g_argFloat[0]);
        break;
    case 5:
        if (original)
            Orig_StartMuzzleFlash(g_work, 0, &g_point, &g_direction, g_argInt[1], g_argInt[2]);
        else
            g_work->StartMuzzleFlash(&g_point, &g_direction, g_argInt[1], g_argInt[2]);
        break;
    case 6:
        if (original)
            Orig_StartColourDissolve(g_work, 0, uint32_t(g_argInt[1]), uint32_t(g_argInt[2]));
        else
            g_work->StartColourDissolve(uint32_t(g_argInt[1]), uint32_t(g_argInt[2]));
        break;
    case 7:
        if (original)
            Orig_RenderShadowGeometry(g_work, 0);
        else
            g_work->RenderShadowGeometry();
        break;
    }
}

void CaseRenderFX(bool original) {
    if (g_argInt[0] == 0) {
        if (original)
            Orig_RenderFX(g_work, 0, &g_transform);
        else
            g_work->RenderFX(&g_transform);
    } else {
        if (original)
            Orig_RenderDeferredEffects(g_work, 0);
        else
            g_work->RenderDeferredEffects();
    }
}

uint8_t g_dummyModel[0xd8];
uint8_t g_dummyBolts[sizeof(REmpBolts)];

void TestVehicles() {
    bool camera = fgRenderHigh != NULL && fgRenderHigh->views[0].camera != NULL;
    int index = 0;
    for (const LiveVehicle &v : g_vehicles) {
        BindVehicle(&v);
        GetCar(&g_carLive);
        RVehicle *copy = reinterpret_cast<RVehicle *>(g_vehicleInput.work);

        // ComputeDamage, also without physics
        for (int i = 0; i < 6; i++) {
            g_input = g_live;
            CopyVehicle(&g_vehicleInput);
            PerturbCar(copy);
            if (i == 5)
                copy->physics = NULL;
            if (Chance(2))
                g_input.damageLevelEffects = Uniform(0.0f, 1.0f);
            VehicleCase("ComputeDamage", index, CaseComputeDamage);
        }

        // the wheels and needles (the player's camera is read for the player's class)
        for (int i = 0; camera && i < 24; i++) {
            g_input = g_live;
            CopyVehicle(&g_vehicleInput);
            PerturbCar(copy);
            VehicleCase("ComputeWheels", index, CaseComputeWheels);
            g_argInt[0] = Range(0, 1);
            g_localTime[4] = uint16_t(Range(0, 23));
            g_localTime[5] = uint16_t(Range(0, 59));
            g_localTime[6] = uint16_t(Range(0, 59));
            VehicleCase("UpdatePosition", index, CaseUpdatePosition);
        }

        // the shared buffers
        for (int i = 0; i < 12; i++) {
            g_input = g_live;
            CopyVehicle(&g_vehicleInput);
            g_input.gameTick = Next();
            for (int s = 0; s < RVehicle::kSwitchEffects; s++)
                copy->switchIntensity[s] = Uniform(0.0f, 1.0f);
            for (int s = RVehicle::kSwitchLeftButton; s <= RVehicle::kSwitchRightButton; s++) {
                switch (Range(0, 3)) {
                case 0: copy->switchStartTick[s] = -10000; break;
                case 1: copy->switchStartTick[s] = 0; break;
                case 2: copy->switchStartTick[s] = int32_t(g_input.gameTick) + Range(-2, 2); break;
                default: copy->switchStartTick[s] = int32_t(g_input.gameTick) - Range(0, 35); break;
                }
            }
            for (int z = 0; z < RVehicle::kDamageZones; z++)
                copy->damageZones[z].y = Uniform(0.0f, 1.0f);
            g_argInt[0] = i % 4 == 3;
            VehicleCase(g_argInt[0] ? "ClearSharedBuffers" : "SetPerObjectBuffers", index, CaseBuffers);
        }

        // the effects
        for (int i = 0; i < 64; i++) {
            g_input = g_live;
            CopyVehicle(&g_vehicleInput);
            if (Chance(2))
                PerturbCar(copy);
            g_input.gameTick = Next() & 0xffffff;
            g_input.seed = Next() & 0xffff;
            g_point = { Uniform(-3.0f, 3.0f), Uniform(-1.0f, 2.0f), Uniform(-3.0f, 3.0f) };
            g_direction = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
            g_argFloat[0] = Uniform(0.0f, 1.0f);
            for (int m = 0; m < RVehicle::kMuzzles; m++)
                copy->muzzleEndTick[m] = g_input.gameTick + Range(-6, 6);
            copy->variation = uint8_t(Range(0, 3));
            if (Chance(3))
                copy->particles = NULL;
            g_argInt[0] = i % 8;
            const char *what = "effect";
            switch (g_argInt[0]) {
            case 0:
                what = "TriggerIlluminate";
                g_argInt[1] = Range(0, RVehicle::kSwitchEffects - 1);
                g_argInt[2] = int32_t(Next());
                g_argInt[3] = int32_t(Next());
                break;
            case 1:
                what = "SetEMPVictimEffect";
                g_argInt[1] = Range(0, 1);
                g_input.simStepCount = Next();
                break;
            case 2:
                what = "TriggerTurboBoostSmoke";
                g_argInt[1] = Range(0, 400);
                break;
            case 3:
                what = "TriggerEmpBolts";
                copy->empBolts = Chance(3) ? reinterpret_cast<REmpBolts *>(g_dummyBolts) : NULL;
                break;
            case 4: {
                what = "TriggerFX";
                static const int types[] = { RVehicle::kFXSparks, RVehicle::kFXColourDissolve,
                                             RVehicle::kFXMuzzleFlash, RVehicle::kFXSwitchOn, RVehicle::kFXSwitchOff };
                g_argInt[1] = types[Range(0, 4)];
                g_argInt[2] = g_argInt[1] == RVehicle::kFXMuzzleFlash ? Range(0, 1) : Range(0, 15);
                g_argInt[3] = Chance(2) ? 45 : Range(0, 50);
                if (g_argInt[1] == RVehicle::kFXSparks && copy->particles == NULL)
                    copy->particles = g_vehicle->render->particles;
                if (g_argInt[1] == RVehicle::kFXSparks && copy->particles == NULL)
                    g_argInt[1] = RVehicle::kFXSwitchOn;
                break;
            }
            case 5:
                what = "StartMuzzleFlash";
                g_argInt[1] = Range(0, 1);
                g_argInt[2] = Chance(2) ? 45 : Range(0, 50);
                break;
            case 6:
                what = "StartColourDissolve";
                g_argInt[1] = int32_t(Next());
                g_argInt[2] = int32_t(Next());
                break;
            default:
                what = "RenderShadowGeometry";
                copy->shadowModel = Chance(2) ? reinterpret_cast<EAGL::Model *>(g_dummyModel) : NULL;
                break;
            }
            g_releaseBolts = g_argInt[0] == 3;
            VehicleCase(what, index, CaseEffect);
            g_releaseBolts = false;
        }

        // the effects drawn
        for (int i = 0; i < 24; i++) {
            g_input = g_live;
            CopyVehicle(&g_vehicleInput);
            RandomTransform();
            g_input.gameTick = Next() & 0xffffff;
            g_input.clearEmpVictim = Chance(4) ? 1 : 0;
            static const int types[] = { RVehicle::kMuzzlePOV, RVehicle::kMuzzleTurret,
                                         RVehicle::kMuzzleSnowmobileTurret, 7 };
            for (int m = 0; m < RVehicle::kMuzzles; m++) {
                copy->muzzleEndTick[m] = g_input.gameTick + Range(-3, 6);
                copy->muzzleType[m] = types[Range(0, 3)];
                copy->muzzleSize[m] = Uniform(0.1f, 4.0f);
            }
            copy->empVictim = uint8_t(Range(0, 1));
            copy->empShootingIntensity = Chance(3) ? 0.0f : Uniform(-0.5f, 1.0f);
            if (Chance(3))
                copy->particles = NULL;
            g_argInt[0] = camera && i % 3 == 2;
            VehicleCase(g_argInt[0] ? "RenderDeferredEffects" : "RenderFX", index, CaseRenderFX);
        }
        PutCar(&g_carLive);
        index++;
    }
}

// ---- tyre tracks

alignas(16) uint8_t g_trackBytes[sizeof(RTyreTrack)];
RTyreTrack *const g_track = reinterpret_cast<RTyreTrack *>(g_trackBytes);
uint8_t g_trackInput[sizeof(RTyreTrack)];
alignas(16) WWorldPos g_ground;
uint8_t g_groundInput[sizeof(WWorldPos)];
alignas(16) Coord4 g_trackPoint;

struct TrackOutput {
    uint8_t track[sizeof(RTyreTrack)];
    uint8_t ground[sizeof(WWorldPos)];
    uint8_t texCoords[sizeof(TrackTexCoords)];
    EAGL::TAR *textures[3];
    CallLog log;
};
TrackOutput g_trackOut[2];

void CaseTrack(void *, bool original) {
    switch (g_argInt[0]) {
    case 0:
        if (original)
            Orig_TrackAdd(g_track, 0, &g_trackPoint, g_argFloat[0], g_argFloat[1], g_argInt[1], &g_ground);
        else
            g_track->Add(&g_trackPoint, g_argFloat[0], g_argFloat[1], g_argInt[1], &g_ground);
        break;
    case 1:
        if (original)
            Orig_TrackBreak(g_track, 0, g_argInt[1]);
        else
            g_track->Break(g_argInt[1]);
        break;
    case 2:
        if (original)
            Orig_TrackDraw(g_track, 0);
        else
            g_track->Draw();
        break;
    case 3:
        if (original)
            Orig_TrackConstruct(g_track, 0, g_argFloat[0], g_argInt[1] != 0, g_argInt[2] != 0);
        else
            g_track->Construct(g_argFloat[0], g_argInt[1] != 0, g_argInt[2] != 0);
        break;
    case 4:
        if (original)
            Orig_TrackInit();
        else
            RTyreTrack::Init();
        break;
    }
}

void TrackCase(const char *what, int index) {
    g_cases++;
    // Init writes the port's texture coordinates and textures: put them back after
    static uint8_t savedTexCoords[sizeof(TrackTexCoords)];
    EAGL::TAR *savedTextures[3];
    memcpy(savedTexCoords, TrackTexCoords, sizeof(TrackTexCoords));
    memcpy(savedTextures, TrackTextures, sizeof(TrackTextures));
    for (int side = 0; side < 2; side++) {
        bool original = side == 0;
        memcpy(TrackTexCoords, savedTexCoords, sizeof(TrackTexCoords));
        memcpy(TrackTextures, savedTextures, sizeof(TrackTextures));
        PutShared(&g_input, original);
        memcpy(g_trackBytes, g_trackInput, sizeof(RTyreTrack));
        memcpy(&g_ground, g_groundInput, sizeof(WWorldPos));
        TrackOutput &out = g_trackOut[side];
        out.log.clear();
        g_log = &out.log;
        Guarded(CaseTrack, NULL, original);
        g_log = NULL;
        memcpy(out.track, g_trackBytes, sizeof(RTyreTrack));
        memcpy(out.ground, &g_ground, sizeof(WWorldPos));
        memcpy(out.texCoords, original ? OrigTrackTexCoords : (uint8_t *)TrackTexCoords, sizeof(TrackTexCoords));
        memcpy(out.textures, original ? OrigTrackTextures : TrackTextures, sizeof(out.textures));
        GetShared(&g_out[side], original);
    }
    memcpy(TrackTexCoords, savedTexCoords, sizeof(TrackTexCoords));
    memcpy(TrackTextures, savedTextures, sizeof(TrackTextures));
    CheckBytes(what, index, g_trackOut[0].track, g_trackOut[1].track, sizeof(RTyreTrack));
    CheckBytes(what, index, g_trackOut[0].ground, g_trackOut[1].ground, sizeof(WWorldPos));
    CheckBytes(what, index, g_trackOut[0].texCoords, g_trackOut[1].texCoords, sizeof(TrackTexCoords));
    CheckBytes(what, index, g_trackOut[0].textures, g_trackOut[1].textures, sizeof(g_trackOut[0].textures));
    CheckLogs(what, index, g_trackOut[0].log, g_trackOut[1].log);
    CheckShared(what, index, g_out[0], g_out[1]);
}

// A made-up ring around a point
void MakeTrack(RTyreTrack *track, const Coord4 &centre) {
    memset(track, 0, sizeof(RTyreTrack));
    track->capacity = Chance(2) ? RTyreTrack::kLongTrack : RTyreTrack::kShortTrack;
    track->width = Uniform(0.2f, 0.8f);
    track->snow = Chance(3);
    for (int i = 0; i < RTyreTrack::kMaxPoints; i++) {
        RTyreTrack::Point &p = track->points[i];
        p.left = { centre.x + Uniform(-4.0f, 4.0f), centre.y + Uniform(-0.5f, 0.5f), centre.z + Uniform(-4.0f, 4.0f), 1.0f };
        p.right = { p.left.x + Uniform(-0.5f, 0.5f), p.left.y, p.left.z + Uniform(-0.5f, 0.5f) };
        p.joined = Chance(4) ? 0 : 1;
        track->colours[i] = Next() | (Next() << 24);
    }
}

void PerturbRing(RTyreTrack *track) {
    int capacity = track->capacity;
    track->count = Chance(4) ? capacity : Range(0, capacity);
    track->head = track->count == capacity ? Range(0, capacity - 1) : 0;
    track->current = Range(0, capacity - 1);
    track->drawn = Chance(3) ? track->count : Range(0, capacity);
    track->advanced = uint8_t(Range(0, 1));
    track->segments = uint8_t(Chance(3) ? 0 : Range(0, 255));
    if (Chance(3))
        track->snow = !track->snow;
    if (Chance(3))
        track->width = Uniform(0.2f, 0.8f);
    for (int i = 0; i < RTyreTrack::kMaxPoints; i++)
        if (Chance(5))
            track->points[i].joined = !track->points[i].joined;
}

void TestTracks() {
    int index = 0;
    std::vector<std::pair<RTyreTrack *, PBondCar *> > tracks;
    for (const LiveVehicle &v : g_vehicles) {
        if (!v.isBondCar)
            continue;
        PBondCar *car = static_cast<PBondCar *>(static_cast<PVehicle *>(v.object));
        for (int w = 0; w < kCarWheels; w++)
            tracks.push_back(std::make_pair(car->tyreTracks[w], car));
    }
    for (auto &entry : tracks) {
        PBondCar *car = entry.second;
        int wheel = int(&entry - &tracks[0]) % kCarWheels;
        for (int i = 0; i < 40; i++) {
            g_input = g_live;
            if (entry.first != NULL && !Chance(4))
                memcpy(g_trackInput, entry.first, sizeof(RTyreTrack));
            else
                MakeTrack(reinterpret_cast<RTyreTrack *>(g_trackInput), car->wheelPos[wheel]);
            RTyreTrack *track = reinterpret_cast<RTyreTrack *>(g_trackInput);
            if (track->capacity != RTyreTrack::kShortTrack && track->capacity != RTyreTrack::kLongTrack)
                continue;
            if (i != 0)
                PerturbRing(track);
            memcpy(g_groundInput, &car->wheels[wheel], sizeof(WWorldPos));
            g_trackPoint = car->wheelPos[wheel];
            g_trackPoint.x += Uniform(-1.5f, 1.5f);
            g_trackPoint.y += Uniform(-0.3f, 0.3f);
            g_trackPoint.z += Uniform(-1.5f, 1.5f);
            g_trackPoint.w = 1.0f;
            static const float slips[] = { 0.0f, 0.3f, 1.0f, 1.4f };
            g_argFloat[0] = Chance(2) ? slips[Range(0, 3)] : Uniform(0.0f, 2.0f);
            g_argFloat[1] = Chance(3) ? 1.0f : Uniform(0.0f, 1.6f);
            static const int surfaces[] = { 0, 1, 3, 8, 9, 11 };
            g_argInt[1] = surfaces[Range(0, 5)];
            for (size_t c = 0; c < kTrackColourBytes / 4; c++)
                reinterpret_cast<uint32_t *>(g_input.trackColours)[c] = Next();
            g_input.trackMaterialIndex = uint32_t(Range(0, 3));
            g_argInt[0] = 0;
            TrackCase("RTyreTrack::Add", index);
            g_argInt[0] = 1;
            TrackCase("RTyreTrack::Break", index);
            g_argInt[0] = 2;
            TrackCase("RTyreTrack::Draw", index);
        }
        index++;
    }
    // a long ring of points all joined (the batches fill and carry over) and the constructor on scratch memory
    for (int i = 0; i < 8; i++) {
        g_input = g_live;
        Coord4 centre = { Uniform(-50.0f, 50.0f), 0.0f, Uniform(-50.0f, 50.0f), 1.0f };
        MakeTrack(reinterpret_cast<RTyreTrack *>(g_trackInput), centre);
        PerturbRing(reinterpret_cast<RTyreTrack *>(g_trackInput));
        g_argInt[0] = 2;
        TrackCase("RTyreTrack::Draw (made up)", i);
        for (size_t b = 0; b < sizeof(g_trackInput); b++)
            g_trackInput[b] = uint8_t(Next());
        g_argFloat[0] = Uniform(0.1f, 9.0f);
        g_argInt[1] = Range(0, 1);
        g_argInt[2] = Range(0, 1);
        g_argInt[0] = 3;
        TrackCase("RTyreTrack::Construct", i);
    }
    g_input = g_live;
    g_argInt[0] = 4;
    TrackCase("RTyreTrack::Init", 0);
}

// ---- debris

RDebrisType *g_types = NULL;            // private copies of the kinds
RDebrisType *g_typeInput = NULL;
RDebrisType *g_typeOut[2] = { NULL, NULL };
alignas(16) uint8_t g_debrisBytes[sizeof(RDebris)];
RDebris *const g_debris = reinterpret_cast<RDebris *>(g_debrisBytes);
uint8_t g_debrisInput[sizeof(RDebris)];
uint8_t g_debrisOut[2][sizeof(RDebris)];
CallLog g_debrisLog[2];
alignas(16) Coord4 g_debrisPosition, g_debrisVelocity;
uint32_t g_debrisTick = 0;
int g_typeCount = 0;                    // the kinds a case copies and compares

void CaseDebris(void *, bool original) {
    RDebrisType *type = &g_types[g_argInt[1]];
    switch (g_argInt[0]) {
    case 0:
        if (original)
            Orig_DebrisTypeInit(type, 0, uint32_t(g_argInt[1]));
        else
            type->Init(uint32_t(g_argInt[1]));
        break;
    case 1:
        if (original)
            Orig_DebrisTypeAdd(type, 0, &g_debrisPosition, &g_debrisVelocity, uint32_t(g_argInt[2]), g_debrisTick,
                               g_argFloat[0], uint32_t(g_argInt[3]));
        else
            type->Add(&g_debrisPosition, &g_debrisVelocity, uint32_t(g_argInt[2]), g_debrisTick, g_argFloat[0],
                      uint32_t(g_argInt[3]));
        break;
    case 2:
        if (original)
            Orig_DebrisTypeDraw(type, 0);
        else
            type->Draw();
        break;
    case 3:
        if (original)
            Orig_DebrisAdd(g_debris, 0, g_argInt[1], &g_debrisPosition, &g_debrisVelocity, uint32_t(g_argInt[2]),
                           g_argFloat[0], uint32_t(g_argInt[3]));
        else
            g_debris->Add(g_argInt[1], &g_debrisPosition, &g_debrisVelocity, uint32_t(g_argInt[2]), g_argFloat[0],
                          uint32_t(g_argInt[3]));
        break;
    case 4:
        if (original)
            Orig_DebrisReset(g_debris, 0);
        else
            g_debris->Reset();
        break;
    case 5:
        if (original)
            Orig_DebrisDraw(g_debris, 0);
        else
            g_debris->Draw();
        break;
    case 6:
        if (original)
            Orig_DrawShellCasings(g_debris, 0);
        else
            g_debris->DrawShellCasings();
        break;
    }
}

void DebrisCase(const char *what, int index) {
    g_cases++;
    for (int side = 0; side < 2; side++) {
        bool original = side == 0;
        PutShared(&g_input, original);
        memcpy(g_types, g_typeInput, g_typeCount * sizeof(RDebrisType));
        memcpy(g_debrisBytes, g_debrisInput, sizeof(RDebris));
        g_debrisLog[side].clear();
        g_log = &g_debrisLog[side];
        Guarded(CaseDebris, NULL, original);
        g_log = NULL;
        memcpy(g_typeOut[side], g_types, g_typeCount * sizeof(RDebrisType));
        memcpy(g_debrisOut[side], g_debrisBytes, sizeof(RDebris));
        GetShared(&g_out[side], original);
    }
    for (int t = 0; t < g_typeCount; t++)
        CheckBytes(what, index * 100 + t, &g_typeOut[0][t], &g_typeOut[1][t], sizeof(RDebrisType));
    CheckBytes(what, index, g_debrisOut[0], g_debrisOut[1], sizeof(RDebris));
    CheckLogs(what, index, g_debrisLog[0], g_debrisLog[1]);
    CheckShared(what, index, g_out[0], g_out[1]);
}

// Made-up pieces in a kind, thrown up to `span` ticks before SimStep
void FillPieces(RDebrisType *type, float simStep, float span) {
    type->count = uint32_t(Range(0, int(RDebrisType::kMaxPieces)));
    type->lifetime = uint32_t(Range(1, 120));
    type->gravity = Uniform(0.0f, 0.01f);
    for (uint32_t p = 0; p < RDebrisType::kMaxPieces; p++) {
        type->position[p] = { Uniform(-20.0f, 20.0f), Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f), 1.0f };
        type->velocity[p] = { Uniform(-0.1f, 0.1f), Uniform(-0.1f, 0.2f), Uniform(-0.1f, 0.1f),
                              floorf(simStep - Uniform(0.0f, span)) };
        type->spin[p] = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(0.0f, 0.5f) };
        type->colour[p] = Next() | (Next() << 24);
    }
}

void TestDebris() {
    RDebris *live = GxDebris;
    if (live == NULL)
        return;
    g_typeCount = RDebris::kKinds;
    size_t bytes = RDebris::kKinds * sizeof(RDebrisType);
    g_types = static_cast<RDebrisType *>(malloc(bytes));
    g_typeInput = static_cast<RDebrisType *>(malloc(bytes));
    g_typeOut[0] = static_cast<RDebrisType *>(malloc(bytes));
    g_typeOut[1] = static_cast<RDebrisType *>(malloc(bytes));
    if (g_types == NULL || g_typeInput == NULL || g_typeOut[0] == NULL || g_typeOut[1] == NULL)
        return;
    for (int t = 0; t < RDebris::kKinds; t++) {
        if (live->types[t] == NULL)
            return;
        memcpy(&g_typeInput[t], live->types[t], sizeof(RDebrisType));
    }
    // the private RDebris over the copies
    memcpy(g_debrisInput, live, sizeof(RDebris));
    RDebris *debrisInput = reinterpret_cast<RDebris *>(g_debrisInput);
    for (int t = 0; t < RDebris::kKinds; t++)
        debrisInput->types[t] = &g_types[t];
    debrisInput->shellCount = 0;

    for (int i = 0; i < 160; i++) {
        g_input = g_live;
        g_input.seed = Next() & 0xffff;
        g_input.gameTick = Next() & 0xfffff;
        g_input.simStep = float(Range(100, 100000));
        int kind = Range(0, RDebris::kKinds - 1);
        RDebrisType *type = &g_typeInput[kind];
        FillPieces(type, g_input.simStep, 150.0f);
        g_debrisPosition = { Uniform(-20.0f, 20.0f), Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f), 1.0f };
        g_debrisVelocity = { Uniform(-0.1f, 0.1f), Uniform(-0.1f, 0.2f), Uniform(-0.1f, 0.1f), Uniform(-1.0f, 1.0f) };
        g_argInt[1] = kind;
        g_argInt[2] = int32_t(Next() | (Next() << 24));
        g_argInt[3] = Chance(4) ? 0 : Range(1, 200);
        g_argFloat[0] = Uniform(0.0f, 0.01f);
        g_debrisTick = uint32_t(g_input.simStep) - uint32_t(Range(0, 100));
        switch (i % 7) {
        case 0:
            g_argInt[0] = 0;
            DebrisCase("RDebrisType::Init", i);
            break;
        case 1:
            if (Chance(4))
                type->count = RDebrisType::kMaxPieces;
            g_argInt[0] = 1;
            DebrisCase("RDebrisType::Add", i);
            break;
        case 2:
            g_argInt[0] = 2;
            DebrisCase("RDebrisType::Draw", i);
            break;
        case 3:
            g_argInt[0] = 3;
            DebrisCase("RDebris::Add", i);
            break;
        case 4:
            g_argInt[0] = 4;
            DebrisCase("RDebris::Reset", i);
            break;
        case 5:
            for (int t = 0; t < RDebris::kKinds; t++)
                if (Chance(3))
                    FillPieces(&g_typeInput[t], g_input.simStep, 150.0f);
            g_argInt[0] = 5;
            DebrisCase("RDebris::Draw", i);
            break;
        default: {
            // queued casings on a live vehicle's effects, then kShellCasings drawn
            debrisInput->shellCount = 0;
            for (const LiveVehicle &v : g_vehicles) {
                Handle *handle = v.render->animHandle;
                if (handle == NULL || handle->effectCount == 0 || debrisInput->shellCount >= RDebris::kMaxShells)
                    continue;
                uint32_t s = debrisInput->shellCount++;
                debrisInput->shellObject[s] = v.render;
                debrisInput->shellEffect[s] = &handle->effects[Range(0, handle->effectCount - 1)];
                debrisInput->shellTick[s] = uint32_t(g_input.simStep) - uint32_t(Range(0, 60));
            }
            if (Chance(3))
                g_typeInput[RDebris::kShellCasings].count = 0;
            g_argInt[0] = 6;
            DebrisCase("RDebris::DrawShellCasings", i);
            debrisInput->shellCount = 0;
            break;
        }
        }
        memcpy(&g_typeInput[kind], live->types[kind], sizeof(RDebrisType));
    }
    free(g_types);
    free(g_typeInput);
    free(g_typeOut[0]);
    free(g_typeOut[1]);
    g_types = g_typeInput = g_typeOut[0] = g_typeOut[1] = NULL;
}

}  // namespace

void GameEffectsShadowA_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEEFFECTSSHADOWA", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    g_vehicles.clear();
    for (int i = 0; i < kOwners; i++) {
        PhysicsObject *object = fgSimulation.rigidBodyOwners[i];
        if (object == NULL || object->renderObject == NULL ||
            uintptr_t(object->renderObject->vtable) != uintptr_t(kRVehicleVtable))
            continue;
        LiveVehicle v;
        v.object = object;
        v.render = static_cast<RVehicle *>(object->renderObject);
        v.isBondCar = object->vtable == kPBondCarVtable;
        g_vehicles.push_back(v);
    }

    GetShared(&g_live, false);
    uint32_t gameClearEmpVictim = OrigClearEmpVictim;
    uint32_t gameTrackMaterialIndex = OrigTrackMaterialIndex;
    static uint8_t gameTrackVertices[sizeof(TrackVertices)];
    static uint8_t gameTrackTexCoords[sizeof(TrackTexCoords)];
    static EAGL::TAR *gameTrackTextures[3];
    memcpy(gameTrackVertices, OrigTrackVertices, sizeof(gameTrackVertices));
    memcpy(gameTrackTexCoords, OrigTrackTexCoords, sizeof(gameTrackTexCoords));
    memcpy(gameTrackTextures, OrigTrackTextures, sizeof(gameTrackTextures));

    for (Fake &f : g_fakes)
        FakeInstall(&f);

    TestFade();
    TestNeedles();
    TestOffsetsAndNames();
    TestVehicles();
    TestTracks();
    TestDebris();

    for (int i = int(sizeof(g_fakes) / sizeof(g_fakes[0])) - 1; i >= 0; i--)
        FakeRemove(&g_fakes[i]);
    PutShared(&g_live, false);
    OrigClearEmpVictim = gameClearEmpVictim;
    OrigTrackMaterialIndex = gameTrackMaterialIndex;
    memcpy(OrigTrackVertices, gameTrackVertices, sizeof(gameTrackVertices));
    memcpy(OrigTrackTexCoords, gameTrackTexCoords, sizeof(gameTrackTexCoords));
    memcpy(OrigTrackTextures, gameTrackTextures, sizeof(gameTrackTextures));
    ResetFpu();
    printf("[geffectsA] RVehicle (%u vehicles), tyre tracks, debris vs originals: %d cases, %d checks, %d differ%s\n",
           unsigned(g_vehicles.size()), g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[geffectsA]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
