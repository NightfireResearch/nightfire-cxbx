#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "LightShadow.h"

#include "../camera/Camera.h"
#include "../data/Tuning.h"
#include "../render/DebugView.h"
#include "../render/Lights.h"
#include "../render/PathEngine.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../../common/xbeOriginal.h"
#include "../world/World.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_LIGHTSHADOW=1, once on the first simulation tick: render/Lights.cpp, render/PathEngine.cpp and the
// pure parts of render/DebugView.cpp against the originals.
//
// Two passes run the same tests, calling everything at the original addresses: the first with the package's
// originals swapped back in (0x0007e430-0x00080a60, 0x0008b2b0-0x0008b5b0), the second with our jumps. Each pass
// writes a log (results as bits, records as bytes, lists by content with pointers as indices); the logs must match
// line for line.
//
//   - the live light manager, its three buffers, DisablePositionalLighting's saved block, the colourise mode and
//     the game's tick snapshotted once and put back before every case: every RLightManager query and setter with
//     perturbed counts, colours, angles, models and colourise modes; FUN_0007e520 and FUN_0007eed0 through
//     register thunks; RHighLevelLightManager's adds (full and empty tables, NaN sizes and scales), the dynamic
//     light effect on random effects (every blink shape, dark and lit), Process and FinishLights over random
//     explosions, missiles and lights; the constructor and the deleting destructor on a buffer of our own (with
//     the tuning file), its tables put back after;
//   - each live path handle copied with its instance and proc-anim state (the scene object cleared, so nothing
//     live is posed): Init, then runs of Update with perturbed throttles, delays, times near both ends, next
//     paths, masters, the getters and setters, both constructors over filled buffers;
//   - the live list's iteration and lookups, then with the list set aside a list of our own: AddInstanceList over
//     copies of the live instances and states plus rejected ones, iteration, lookups, Update, Reset,
//     CreatePathHandle (with and without a list), DestroyPathHandle, Purge;
//   - the camera helpers after the path engine on random and edge values, RoadNavLaneCount over every segment,
//     the camera assignment, RRandom::StartUp (the generator's two words put back).
// Drawing (the offscreen buffers, the debug views' renders) and the views' construction are tested in game.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_checks, g_diffs, g_faults;
std::string *g_log;

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

void LogBytes(const char *what, const void *p, size_t n) {
    if (g_log == NULL)
        return;
    g_log->append(what);
    g_log->push_back(' ');
    char h[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(h, sizeof(h), "%02x", static_cast<const uint8_t *>(p)[i]);
        g_log->append(h);
    }
    g_log->push_back('\n');
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

unsigned long long Bits(double d) {
    unsigned long long u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

float FromBits(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

// ---- the two sides

const unsigned kRanges[][2] = {
    { 0x0007e430, 0x0007f5a0 },     // the lights
    { 0x0007f5a0, 0x00080a60 },     // the offscreen buffers, the path engine, the camera helpers
    { 0x0008b2b0, 0x0008b5b0 },     // RRandom, the debug views
};

struct Originals {
    bool on;
    explicit Originals(bool original) : on(original) {
        if (on)
            for (const auto &r : kRanges)
                XbeOriginal_RestoreRange(r[0], r[1], true);
    }
    ~Originals() {
        if (on)
            for (const auto &r : kRanges)
                XbeOriginal_RestoreRange(r[0], r[1], false);
    }
};

bool Safe(void (*thunk)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        thunk(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    thunk(context);
    return true;
#endif
}

template <class F>
void Guarded(const char *what, F f) {
    g_cases++;
    if (!Safe([](void *c) { (*static_cast<F *>(c))(); }, &f))
        Logf("%s FAULT", what);
}

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float Unit() { return float(Next() >> 8) * (1.0f / 16777216.0f); }
    float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }
    uint32_t Below(uint32_t n) { return n == 0 ? 0 : Next() % n; }
    bool Chance(uint32_t percent) { return Below(100) < percent; }
};

// ---- the functions under test, at their addresses (thiscall through __fastcall and a dummy EDX)

typedef double (__fastcall *VerticalFalloffFn)(RLightManager *, int, const Coord3 *);
typedef void (__fastcall *AddLightFn)(RLightManager *, int, const Coord4 *, const Coord4 *);
typedef void (__fastcall *DisableFn)(RLightManager *, int, bool);
typedef void (__fastcall *SpecularFn)(RLightManager *, int, const Coord4 *);
typedef void (__fastcall *ManagerFn)(RLightManager *, int);
typedef void (__fastcall *SurfaceFn)(RLightManager *, int, float, float, float);
typedef RLightManager *(__fastcall *ConstructFn)(RLightManager *, int);
typedef RLightManager *(__fastcall *DeleteFn)(RLightManager *, int, unsigned);
typedef void (__fastcall *ModelFn)(RLightManager *, int, int);
typedef void (__fastcall *ExplosionFn)(RHighLevelLightManager *, int, const Coord4 *, float);
typedef void (__fastcall *MissileFn)(RHighLevelLightManager *, int, const Coord4 *, int, float);
typedef void (__fastcall *HighLightFn)(RHighLevelLightManager *, int, const Coord4 *, const Coord3 *, int);
typedef void (__fastcall *EffectFn)(RHighLevelLightManager *, int, const ArticleEffect *, const MATRIX4 *);
typedef void (__fastcall *ProcessFn)(RHighLevelLightManager *, int);

#define VerticalFalloffAt ((VerticalFalloffFn)0x0007e430)
#define AddPositionalLightAt ((AddLightFn)0x0007e440)
#define HighAddExplosionAt ((ExplosionFn)0x0007e620)
#define HighAddMissileAt ((MissileFn)0x0007e6f0)
#define HighAddPositionalLightAt ((HighLightFn)0x0007e790)
#define DisablePositionalLightingAt ((DisableFn)0x0007e860)
#define AddSpecularLightAt ((SpecularFn)0x0007e950)
#define SetCurrentAmbientAndDiffuseAt ((ManagerFn)0x0007eaa0)
#define SetSurfacePropertiesAt ((SurfaceFn)0x0007eb30)
#define HighAddDynamicLightEffectAt ((EffectFn)0x0007ec40)
#define HighProcessAt ((ProcessFn)0x0007ed10)
#define RefreshLightBlocksAt ((ManagerFn)0x0007efb0)
#define ConstructAt ((ConstructFn)0x0007f010)
#define DeleteAt ((DeleteFn)0x0007f450)
#define SetLightingModelAt ((ModelFn)0x0007f470)
#define FinishLightsAt ((ManagerFn)0x0007f540)

typedef RPathHandle *(__fastcall *CopyHandleFn)(RPathHandle *, int, const RPathHandle *);
typedef RPathHandle *(__fastcall *MakeHandleFn)(RPathHandle *, int, CARP::Instance *, ProcAnimState *);
typedef void (__fastcall *HandleTimeFn)(RPathHandle *, int, float);
typedef void (__fastcall *HandlePathFn)(RPathHandle *, int, CARP::PathInfo *);
typedef double (__fastcall *HandleDurationFn)(RPathHandle *, int);
typedef void (__fastcall *HandleBoolFn)(RPathHandle *, int, bool);
typedef void (__fastcall *HandleUIntFn)(RPathHandle *, int, unsigned);
typedef Coord3 *(__fastcall *HandlePositionFn)(RPathHandle *, int);
typedef void (__fastcall *HandleMatrixFn)(RPathHandle *, int, MATRIX4 *);
typedef RPathHandle *(*FirstFn)();
typedef void (*EngineTimeFn)(float);
typedef RPathHandle *(*LookupFn)(const CARP::Instance *);
typedef void (*DestroyFn)(RPathHandle *);
typedef void (*PurgeFn)();
typedef void (*AddListFn)(CARP::Instance *, ProcAnimState *, uint32_t, float);
typedef RPathHandle *(*CreateFn)(CARP::Instance *, ProcAnimState *, float);

#define CopyHandleAt ((CopyHandleFn)0x0007f9f0)
#define MakeHandleAt ((MakeHandleFn)0x0007fb00)
#define HandleUpdateAt ((HandleTimeFn)0x0007fbc0)
#define SetNextPathAt ((HandlePathFn)0x0007fec0)
#define GetParametricDurationAt ((HandleDurationFn)0x0007fed0)
#define SetRunningAt ((HandleBoolFn)0x0007fee0)
#define SetThrottleAt ((HandleTimeFn)0x0007fef0)
#define SetDesiredThrottleAt ((HandleTimeFn)0x0007ff00)
#define SetAccelerationAt ((HandleTimeFn)0x0007ff10)
#define SetAccelDelayAt ((HandleUIntFn)0x0007ff20)
#define SetParametricTimeAt ((HandleTimeFn)0x0007ff30)
#define GetPositionAt ((HandlePositionFn)0x0007ff50)
#define GetOrientMatAt ((HandleMatrixFn)0x0007ff60)
#define GetFirstAt ((FirstFn)0x0007ffa0)
#define GetNextAt ((FirstFn)0x0007ffc0)
#define EngineUpdateAt ((EngineTimeFn)0x0007fff0)
#define GetPathHandleAt ((LookupFn)0x00080030)
#define HandleInitAt ((HandleTimeFn)0x00080130)
#define DestroyPathHandleAt ((DestroyFn)0x000802d0)
#define EngineResetAt ((EngineTimeFn)0x00080330)
#define PurgeAt ((PurgeFn)0x000803e0)
#define AddInstanceListAt ((AddListFn)0x00080520)
#define CreatePathHandleAt ((CreateFn)0x00080700)

typedef double (*TurnsFn)(float);
typedef double (*HeadingFn)(float, float);
typedef void (*RotateOffsetFn)(const Coord4 *, const Coord4 *, Coord4 *);
typedef void (*FrameFn)(MATRIX4 *);
typedef int (__fastcall *LaneCountFn)(WRoadNav *);
typedef RCamera *(__fastcall *AssignFn)(RCamera *, int, const RCamera *);
typedef void (*StartUpFn)(unsigned);

#define WrapTurnsAt ((TurnsFn)0x00080820)
#define SinTurnsAt ((TurnsFn)0x00080860)
#define TanTurnsAt ((TurnsFn)0x00080890)
#define HeadingTurnsAt ((HeadingFn)0x000808c0)
#define RotateOffsetAt ((RotateOffsetFn)0x000808f0)
#define TurnFrameRowsAt ((FrameFn)0x00080980)
#define FlipFrameRowsAt ((FrameFn)0x000809e0)
#define LaneCountAt ((LaneCountFn)0x00080a30)
#define AssignCameraAt ((AssignFn)0x0008b330)
#define StartUpAt ((StartUpFn)0x0008b2b0)

// FUN_0007e520 takes the effect in ESI; FUN_0007eed0 the light set in EAX and the angles in EBX
__declspec(naked) double CallBrightness(const ArticleEffect *) {
    __asm {
        push esi
        mov esi, dword ptr [esp + 8]
        mov eax, 0x0007e520
        call eax
        pop esi
        ret
    }
}

__declspec(naked) void CallDirections(LightBlock *, const RLightAngles *) {
    __asm {
        push ebx
        mov eax, dword ptr [esp + 8]
        mov ebx, dword ptr [esp + 12]
        mov ecx, 0x0007eed0
        call ecx
        pop ebx
        ret
    }
}

// ---- the game's state the tests read and write

#define GameTick (*(uint32_t *)0x001f2a4c)
#define SavedLights (*(RPositionalLights *)0x001ec268)
#define ColorizeMode (*(int32_t *)(*(uint8_t **)0x001f6898 + 0x30))
#define RandomSeed (*(uint32_t *)0x001c45c4)
#define RandomMultiplier (*(uint32_t *)0x001c45c8)
uint8_t *const kLightTables = reinterpret_cast<uint8_t *>(0x001c4080);     // explosion and missile types
const size_t kLightTablesSize = 0xf0;
uint8_t *const kLightIndices = reinterpret_cast<uint8_t *>(0x001ec2e8);    // the dbindex values and flag bytes
const size_t kLightIndicesSize = 0x14;

struct LightState {
    uint8_t manager[sizeof(RLightManager)];
    RPositionalLights pending;
    RPositionalLights positional;
    LightBlock block;
    RPositionalLights saved;
    int32_t colorizeMode;
    uint32_t tick;
};

LightState g_lights;

void TakeLights(LightState *s) {
    RLightManager *m = fgLightManager;
    memcpy(s->manager, m, sizeof(RLightManager));
    s->pending = *m->pendingLights;
    s->positional = *m->positionalLights;
    s->block = *m->lightBlock;
    s->saved = SavedLights;
    s->colorizeMode = ColorizeMode;
    s->tick = GameTick;
}

void PutLights(const LightState *s) {
    RLightManager *m = fgLightManager;
    memcpy(m, s->manager, sizeof(RLightManager));
    *m->pendingLights = s->pending;
    *m->positionalLights = s->positional;
    *m->lightBlock = s->block;
    SavedLights = s->saved;
    ColorizeMode = s->colorizeMode;
    GameTick = s->tick;
}

// The manager with its buffers; specularLight.w is masked (the original leaves stack garbage there)
void LogLights(const char *what) {
    RLightManager *m = fgLightManager;
    uint8_t copy[sizeof(RLightManager)];
    memcpy(copy, m, sizeof(copy));
    memset(copy + offsetof(RLightManager, specularLight) + 12, 0, 4);
    Logf("%s", what);
    LogBytes(" manager", copy, sizeof(copy));
    LogBytes(" pending", m->pendingLights, sizeof(RPositionalLights));
    LogBytes(" positional", m->positionalLights, sizeof(RPositionalLights));
    LogBytes(" block", m->lightBlock, sizeof(LightBlock));
    LogBytes(" saved", &SavedLights, sizeof(RPositionalLights));
}

Coord4 RandomVector(Rng &rng, float range, float w) {
    return Coord4{ rng.Range(-range, range), rng.Range(-range, range), rng.Range(-range, range), w };
}

void RandomMatrix(Rng &rng, MATRIX4 *m) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            m->mtx[r][c] = rng.Range(-4.0f, 4.0f);
}

void RandomLightInfo(Rng &rng, LightBlock *info) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            info->directions[r][c] = rng.Range(-1.0f, 1.0f);
    for (int c = 0; c < 4; c++)
        info->colours[c] = Coord4{ rng.Unit(), rng.Unit(), rng.Unit(), rng.Unit() };
}

float RandomAngle(Rng &rng) {
    switch (rng.Below(8)) {
    case 0: return 0.0f;
    case 1: return 90.0f;
    case 2: return -90.0f;
    case 3: return rng.Range(-720.0f, 720.0f);
    default: return rng.Range(-90.0f, 360.0f);
    }
}

// The high-level manager filled at random: lights, explosions round the tick, missiles
void RandomHighLevel(Rng &rng, RHighLevelLightManager *h) {
    h->lightCount = int32_t(rng.Below(5));
    for (int i = 0; i < 4; i++) {
        h->lights[i].position = RandomVector(rng, 300.0f, rng.Range(0.0f, 30.0f));
        h->lights[i].colour = Coord3{ rng.Unit(), rng.Unit(), rng.Unit() };
        h->lights[i].priority = int32_t(rng.Below(20)) - 5;
        h->explosionPositions[i] = RandomVector(rng, 300.0f, rng.Unit());
        h->explosionTypes[i] = int32_t(rng.Below(5));
        switch (rng.Below(4)) {
        case 0: h->explosionStartTicks[i] = 0; break;
        case 1: h->explosionStartTicks[i] = GameTick + rng.Below(5); break;
        default: h->explosionStartTicks[i] = GameTick - rng.Below(120); break;
        }
    }
    h->missileCount = int32_t(rng.Below(3));
    for (int i = 0; i < 2; i++) {
        h->missilePositions[i] = RandomVector(rng, 300.0f, rng.Unit());
        h->missileTypes[i] = int32_t(rng.Below(4));
        h->missileScales[i] = rng.Chance(10) ? FromBits(0x7fc00000) : rng.Range(-1.0f, 4.0f);
    }
}

void RandomEffect(Rng &rng, ArticleEffect *e) {
    memset(e, 0, sizeof(*e));
    Coord4 position = RandomVector(rng, 10.0f, 1.0f);
    e->position = { position.x, position.y, position.z };
    e->unknown0C = position.w;
    static const uint16_t kFlags[] = { 0, ArticleEffect::kBlinkShaped,
                                       ArticleEffect::kBlinkShaped | ArticleEffect::kBlinkPulse,
                                       ArticleEffect::kBlinkPulse, 0x1234 };
    e->flags = kFlags[rng.Below(5)];
    e->light.priority = int32_t(rng.Below(10));
    for (int i = 0; i < 4; i++)
        e->light.colour[i] = uint8_t(rng.Below(256));
    e->light.blinkStartTick = GameTick - rng.Below(4000);
    e->light.blinkRate = rng.Chance(20) ? 0.0f : rng.Range(0.0f, 0.2f);
    e->light.blinkDuty = rng.Range(0.0f, 1.2f);
    e->light.blinkBase = rng.Range(-0.5f, 1.0f);
    e->light.blinkAmplitude = rng.Range(-1.0f, 2.0f);
}

void TestLightQueries(Rng &rng) {
    RLightManager *m = fgLightManager;
    for (int i = 0; i < 4; i++) {
        PutLights(&g_lights);
        Coord3 p = { rng.Range(-500.0f, 500.0f), rng.Range(-50.0f, 50.0f), rng.Range(-500.0f, 500.0f) };
        Guarded("VerticalFalloffMultiplier", [&] {
            Logf("falloff %016llx", Bits(VerticalFalloffAt(m, 0, &p)));
        });
    }

    // AddPositionalLight over every count and colourise mode
    for (int i = 0; i < 60; i++) {
        PutLights(&g_lights);
        m->pendingLightCount = int32_t(rng.Below(6));
        ColorizeMode = int32_t(rng.Below(3));
        Coord4 position = RandomVector(rng, 400.0f, rng.Unit());
        Coord4 colour = { rng.Unit(), rng.Unit(), rng.Unit(), rng.Range(0.0f, 40.0f) };
        Guarded("AddPositionalLight", [&] { AddPositionalLightAt(m, 0, &position, &colour); });
        LogLights("AddPositionalLight");
    }

    for (int i = 0; i < 6; i++) {
        PutLights(&g_lights);
        for (int c = 0; c < 4; c++)
            m->positionalLights->colours[c] = RandomVector(rng, 100.0f, rng.Unit());
        bool disable = (i & 1) == 0;
        Guarded("DisablePositionalLighting", [&] { DisablePositionalLightingAt(m, 0, disable); });
        LogLights("DisablePositionalLighting");
    }

    for (int i = 0; i < 30; i++) {
        PutLights(&g_lights);
        RandomLightInfo(rng, m->lightBlock);
        Coord4 from = RandomVector(rng, i < 5 ? 1.0f : 800.0f, 1.0f);
        Guarded("AddSpecularLight", [&] { AddSpecularLightAt(m, 0, &from); });
        LogLights("AddSpecularLight");
    }

    for (int i = 0; i < 8; i++) {
        PutLights(&g_lights);
        RandomLightInfo(rng, m->lightBlock);
        m->positionalLightCount = int32_t(rng.Below(5));
        Guarded("SetCurrentAmbientAndDiffuse", [&] { SetCurrentAmbientAndDiffuseAt(m, 0); });
        LogLights("SetCurrentAmbientAndDiffuse");
    }

    for (int i = 0; i < 24; i++) {
        PutLights(&g_lights);
        RandomLightInfo(rng, &m->lightInfos[i % kEnvironmentCount]);
        m->currentLightInfo = &m->lightInfos[i % kEnvironmentCount];
        float ambient = rng.Range(0.0f, 2.0f), diffuse = rng.Range(0.0f, 2.0f), unused = rng.Unit();
        Guarded("SetSurfaceProperties", [&] { SetSurfacePropertiesAt(m, 0, ambient, diffuse, unused); });
        LogLights("SetSurfaceProperties");
    }

    for (int i = 0; i < 16; i++) {
        PutLights(&g_lights);
        if (i > 0) {
            for (int e = 0; e < kEnvironmentCount; e++)
                for (int l = 0; l < 3; l++) {
                    m->angles[e].x[l] = RandomAngle(rng);
                    m->angles[e].y[l] = RandomAngle(rng);
                }
        }
        Guarded("RefreshLightBlocks", [&] { RefreshLightBlocksAt(m, 0); });
        LogLights("RefreshLightBlocks");
    }

    for (int i = 0; i < 12; i++) {
        LightBlock info;
        RandomLightInfo(rng, &info);
        RLightAngles angles;
        for (int l = 0; l < 3; l++) {
            angles.x[l] = RandomAngle(rng);
            angles.y[l] = RandomAngle(rng);
        }
        Guarded("FUN_0007eed0", [&] { CallDirections(&info, &angles); });
        LogBytes("directions", &info, sizeof(info));
    }

    static const int kModels[] = { 0, 1, 2, 3, -1 };
    for (int i = 0; i < 20; i++) {
        PutLights(&g_lights);
        int model = kModels[rng.Below(5)];
        if (rng.Chance(40))
            m->lightingModel = model;
        if (rng.Chance(30))
            m->currentLightInfo = NULL;
        if (m->currentLightInfo == NULL && (model < 0 || model > 2))
            m->currentLightInfo = &m->lightInfos[3];
        RandomLightInfo(rng, &m->lightInfos[rng.Below(2)]);
        Guarded("SetLightingModel", [&] { SetLightingModelAt(m, 0, model); });
        LogLights("SetLightingModel");
    }
}

void TestHighLevel(Rng &rng) {
    RLightManager *m = fgLightManager;
    RHighLevelLightManager *h = &m->highLevel;

    static const float kSizes[] = { 50.0f, 100.0f, 100.5f, 200.0f, 300.0f, 400.0f, 401.0f, 800.0f, 801.0f, -1.0f, 0.0f };
    for (int i = 0; i < 40; i++) {
        PutLights(&g_lights);
        RandomHighLevel(rng, h);
        for (int s = 0; s < 4; s++) {
            if (rng.Chance(20))
                h->explosionStartTicks[s] = 0xffffffff;
        }
        GameTick = 1000 + rng.Below(100000);
        float size = rng.Chance(8) ? FromBits(0x7fc00000) : kSizes[rng.Below(11)];
        Coord4 position = RandomVector(rng, 100.0f, rng.Unit());
        Guarded("AddExplosion", [&] { HighAddExplosionAt(h, 0, &position, size); });
        LogLights("AddExplosion");
    }

    for (int i = 0; i < 30; i++) {
        PutLights(&g_lights);
        RandomHighLevel(rng, h);
        static const int kCounts[] = { 0, 1, 2, 2, 2, 5 };
        h->missileCount = kCounts[rng.Below(6)];
        Coord4 position = RandomVector(rng, 100.0f, rng.Unit());
        int type = int(rng.Below(4));
        float scale = rng.Range(-1.0f, 3.0f);
        Guarded("AddMissile", [&] { HighAddMissileAt(h, 0, &position, type, scale); });
        LogLights("AddMissile");
    }

    for (int i = 0; i < 30; i++) {
        PutLights(&g_lights);
        RandomHighLevel(rng, h);
        h->lightCount = int32_t(rng.Below(6));
        Coord4 position = RandomVector(rng, 100.0f, rng.Unit());
        Coord3 colour = { rng.Unit(), rng.Unit(), rng.Unit() };
        int priority = int(rng.Below(20)) - 5;
        Guarded("HighLevel::AddPositionalLight", [&] { HighAddPositionalLightAt(h, 0, &position, &colour, priority); });
        LogLights("HighLevel::AddPositionalLight");
    }

    for (int i = 0; i < 80; i++) {
        PutLights(&g_lights);
        RandomHighLevel(rng, h);
        GameTick = 100000 + rng.Below(100000);
        ArticleEffect effect;
        RandomEffect(rng, &effect);
        alignas(16) MATRIX4 transform;
        RandomMatrix(rng, &transform);
        Guarded("FUN_0007e520", [&] { Logf("brightness %016llx", Bits(CallBrightness(&effect))); });
        Guarded("AddDynamicLightEffect", [&] { HighAddDynamicLightEffectAt(h, 0, &effect, &transform); });
        LogLights("AddDynamicLightEffect");
    }

    for (int i = 0; i < 40; i++) {
        PutLights(&g_lights);
        GameTick = 1000 + rng.Below(100000);
        RandomHighLevel(rng, h);
        m->pendingLightCount = int32_t(rng.Below(5));
        ColorizeMode = int32_t(rng.Below(2));
        if (i & 1) {
            Guarded("Process", [&] { HighProcessAt(h, 0); });
            LogLights("Process");
        } else {
            Guarded("FinishLights", [&] { FinishLightsAt(m, 0); });
            LogLights("FinishLights");
        }
    }
    PutLights(&g_lights);
}

// The constructor and the deleting destructor on a buffer of ours. The heap pointers are logged by what they
// point at.
alignas(16) uint8_t g_managerBuffer[sizeof(RLightManager)];
std::vector<uint8_t> g_tables, g_indices;

void TestConstruction() {
    if (TuningDBMgr == NULL || TuningDBMgr->file != NULL) {
        Logf("construction skipped");
        return;
    }
    memset(g_managerBuffer, 0x5a, sizeof(g_managerBuffer));
    RLightManager *m = reinterpret_cast<RLightManager *>(g_managerBuffer);
    RLightManager *result = NULL;
    Guarded("RLightManager::RLightManager", [&] { result = ConstructAt(m, 0); });
    Logf("construct %d", result == m);
    if (result == m) {
        LogBytes(" pending", m->pendingLights, sizeof(RPositionalLights));
        LogBytes(" positional", m->positionalLights, sizeof(RPositionalLights));
        LogBytes(" block", m->lightBlock, sizeof(LightBlock));
    }
    uint8_t copy[sizeof(RLightManager)];
    memcpy(copy, m, sizeof(copy));
    memset(copy + offsetof(RLightManager, pendingLights), 0, 8);
    memset(copy + offsetof(RLightManager, lightBlock), 0, 4);
    LogBytes(" manager", copy, sizeof(copy));
    LogBytes(" tables", kLightTables, kLightTablesSize);
    LogBytes(" indices", kLightIndices, kLightIndicesSize);
    if (result == m) {
        Guarded("RLightManager::scalar_deleting_destructor", [&] { result = DeleteAt(m, 0, 0); });
        memcpy(copy, m, sizeof(copy));
        memset(copy + offsetof(RLightManager, pendingLights), 0, 8);
        memset(copy + offsetof(RLightManager, lightBlock), 0, 4);
        Logf("delete %d", result == m);
        LogBytes(" manager", copy, sizeof(copy));
    }
    memcpy(kLightTables, g_tables.data(), kLightTablesSize);
    memcpy(kLightIndices, g_indices.data(), kLightIndicesSize);
}

// ---- path handles: copies of the live ones

struct HandleCopy {
    CARP::Instance instance;
    ProcAnimState state;
    RPathHandle handle;
};

std::vector<const RPathHandle *> g_liveHandles;
std::vector<HandleCopy> g_copies;           // one per live handle, remade before each case

void MakeCopy(size_t i) {
    const RPathHandle *live = g_liveHandles[i];
    HandleCopy &c = g_copies[i];
    memcpy(&c.instance, live->instance, sizeof(c.instance));
    memcpy(&c.state, live->procAnim, sizeof(c.state));
    c.state.sceneObj = NULL;
    memcpy(&c.handle, live, sizeof(c.handle));
    c.handle.instance = &c.instance;
    c.handle.procAnim = &c.state;
}

void LogCopy(const char *what, size_t i) {
    Logf("%s %u", what, unsigned(i));
    LogBytes(" handle", &g_copies[i].handle, sizeof(RPathHandle));
    LogBytes(" instance", &g_copies[i].instance, sizeof(CARP::Instance));
}

float NearEnd(Rng &rng, const CARP::PathInfo *path) {
    float duration = path != NULL ? path->duration : 10.0f;
    switch (rng.Below(6)) {
    case 0: return 0.0f;
    case 1: return duration;
    case 2: return -rng.Range(0.0f, duration * 0.5f + 1.0f);
    case 3: return duration + rng.Range(0.0f, duration * 0.5f + 1.0f);
    default: return rng.Range(0.0f, duration);
    }
}

void TestHandles(Rng &rng) {
    for (size_t i = 0; i < g_copies.size(); i++) {
        RPathHandle *h = &g_copies[i].handle;
        const RPathHandle *live = g_liveHandles[i];

        MakeCopy(i);
        float time = rng.Range(0.0f, 20000.0f);
        Guarded("Init", [&] { HandleInitAt(h, 0, time); });
        LogCopy("Init", i);
        MakeCopy(i);
        g_copies[i].state.untransformed = uint8_t(!g_copies[i].state.untransformed);
        g_copies[i].state.flags ^= kPathAnimRunning;
        Guarded("Init", [&] { HandleInitAt(h, 0, time); });
        LogCopy("Init flipped", i);

        for (int run = 0; run < 6; run++) {
            MakeCopy(i);
            static const float kThrottles[] = { 0.0f, 1.0f, 0.5f, -0.5f, 2.0f };
            h->throttle = kThrottles[rng.Below(5)];
            h->desiredThrottle = rng.Chance(30) ? h->throttle : kThrottles[rng.Below(5)];
            h->acceleration = rng.Range(0.0f, 0.6f);
            h->accelDelay = int32_t(rng.Below(3));
            h->running = run == 0 ? live->running : rng.Chance(85);
            h->parametricTime = NearEnd(rng, h->currentPath);
            switch (rng.Below(4)) {
            case 0: h->nextPath = NULL; break;
            case 1: h->nextPath = live->currentPath; break;
            default: break;
            }
            if (rng.Chance(10))
                h->currentPath = NULL;
            if (rng.Chance(30))
                h->master = NULL;
            float now = float(double(h->lastUpdateTime) / (60.0 * (*(float *)0x001f2a48))) + rng.Range(-2.0f, 2.0f);
            for (int step = 0; step < 6; step++) {
                if (step != 2)
                    now += rng.Range(0.0f, 3.0f);
                Guarded("Update", [&] { HandleUpdateAt(h, 0, now); });
                LogCopy("Update", i);
            }
        }

        MakeCopy(i);
        Guarded("getters", [&] {
            if (h->currentPath != NULL)
                Logf("duration %016llx", Bits(GetParametricDurationAt(h, 0)));
            Coord3 *position = GetPositionAt(h, 0);
            Logf("position %d", int(reinterpret_cast<uint8_t *>(position) - reinterpret_cast<uint8_t *>(&g_copies[i].instance)));
            MATRIX4 m;
            memset(&m, 0x77, sizeof(m));
            GetOrientMatAt(h, 0, &m);
            LogBytes("orient", &m, sizeof(m));
        });
        Guarded("setters", [&] {
            SetNextPathAt(h, 0, live->currentPath);
            SetRunningAt(h, 0, true);
            SetThrottleAt(h, 0, 0.25f);
            SetDesiredThrottleAt(h, 0, 0.75f);
            SetAccelerationAt(h, 0, 0.125f);
            SetAccelDelayAt(h, 0, 7);
            SetParametricTimeAt(h, 0, 1.5f);
            h->master = h->master != NULL ? NULL : const_cast<RPathHandle *>(live);
            SetParametricTimeAt(h, 0, 2.5f);
        });
        LogCopy("setters", i);

        RPathHandle made;
        memset(&made, 0xa5, sizeof(made));
        Guarded("RPathHandle(instance, state)", [&] { MakeHandleAt(&made, 0, &g_copies[i].instance, &g_copies[i].state); });
        LogBytes("made", &made, sizeof(made));
        RPathHandle copied;
        memset(&copied, 0xa5, sizeof(copied));
        Guarded("RPathHandle(copy)", [&] { CopyHandleAt(&copied, 0, h); });
        LogBytes("copied", &copied, sizeof(copied));
    }
}

// ---- the engine: the live list read, then a list of our own

std::vector<CARP::Instance> g_instances, g_instancesPristine;
std::vector<ProcAnimState> g_states, g_statesPristine;

int IndexOf(const void *p, const void *base, size_t size, size_t count) {
    if (p == NULL)
        return -1;
    const uint8_t *b = static_cast<const uint8_t *>(base);
    const uint8_t *q = static_cast<const uint8_t *>(p);
    if (q < b || q >= b + size * count)
        return -2;
    return int((q - b) / size);
}

int NodeIndex(const RPathHandle *handle) {
    if (handle == NULL)
        return -1;
    PathList *list = fgPathHandles;
    if (list == NULL)
        return -3;
    int i = 0;
    for (PathListNode *node = list->Begin(); node != list->head; node = node->next, i++)
        if (&node->value == handle)
            return i;
    return -2;
}

void LogList(const char *what) {
    PathList *list = fgPathHandles;
    if (list == NULL) {
        Logf("%s: no list", what);
        return;
    }
    Logf("%s: size %u, head %d", what, list->size, list->head != NULL);
    int i = 0;
    for (PathListNode *node = list->Begin(); node != list->head && i < 4000; node = node->next, i++) {
        RPathHandle h;
        memcpy(&h, &node->value, sizeof(h));
        int instance = IndexOf(h.instance, g_instances.data(), sizeof(CARP::Instance), g_instances.size());
        int state = IndexOf(h.procAnim, g_states.data(), sizeof(ProcAnimState), g_states.size());
        int master = NodeIndex(h.master);
        h.instance = NULL;
        h.procAnim = NULL;
        h.master = NULL;
        Logf(" [%d] instance %d state %d master %d links %d", i, instance, state, master,
             node->next->prev == node && node->prev->next == node);
        LogBytes("  handle", &h, sizeof(h));
    }
}

void TestLiveList() {
    PathListNode *iterator = fgPathIterator;
    Guarded("live iteration", [&] {
        int n = 0;
        for (RPathHandle *h = GetFirstAt(); h != NULL && n < 4000; h = GetNextAt(), n++)
            Logf("live %d: %d", n, NodeIndex(h));
        Logf("live end %d", NodeIndex(GetNextAt() /* past the end */));
    });
    Guarded("live lookups", [&] {
        for (size_t i = 0; i < g_liveHandles.size(); i++)
            Logf("lookup %u: %d", unsigned(i), NodeIndex(GetPathHandleAt(g_liveHandles[i]->instance)));
        CARP::Instance none = {};
        Logf("lookup none: %d", NodeIndex(GetPathHandleAt(&none)));
    });
    fgPathIterator = iterator;
}

void TestOwnList(Rng &rng) {
    PathList *liveList = fgPathHandles;
    PathListNode *liveIterator = fgPathIterator;
    g_instances = g_instancesPristine;
    g_states = g_statesPristine;
    uint32_t count = uint32_t(g_instances.size());
    fgPathHandles = NULL;

    float time = rng.Range(0.0f, 20000.0f);
    if (count > 0) {
        Guarded("CreatePathHandle (no list)", [&] {
            RPathHandle *h = CreatePathHandleAt(&g_instances[0], &g_states[0], time);
            Logf("created %d", NodeIndex(h));
        });
        LogList("CreatePathHandle (no list)");
    }
    Guarded("AddInstanceList", [&] { AddInstanceListAt(g_instances.data(), g_states.data(), count, time); });
    LogList("AddInstanceList");
    LogBytes("instances", g_instances.data(), g_instances.size() * sizeof(CARP::Instance));

    Guarded("iteration", [&] {
        int n = 0;
        for (RPathHandle *h = GetFirstAt(); h != NULL && n < 4000; h = GetNextAt(), n++)
            Logf("iterate %d: %d", n, NodeIndex(h));
        Logf("iterate end %d", NodeIndex(GetNextAt()));
    });
    Guarded("lookups", [&] {
        for (uint32_t i = 0; i < count; i++)
            Logf("lookup %u: %d", i, NodeIndex(GetPathHandleAt(&g_instances[i])));
    });
    for (int i = 0; i < 4; i++) {
        time += rng.Range(0.0f, 3.0f);
        Guarded("RPathEngine::Update", [&] { EngineUpdateAt(time); });
        LogList("RPathEngine::Update");
    }
    time += 5.0f;
    Guarded("RPathEngine::Reset", [&] { EngineResetAt(time); });
    LogList("RPathEngine::Reset");
    for (uint32_t i = 0; i < count && i < 6; i++) {
        uint32_t k = rng.Below(count);
        Guarded("CreatePathHandle", [&] {
            RPathHandle *h = CreatePathHandleAt(&g_instances[k], &g_states[k], time);
            Logf("created %u: %d", k, NodeIndex(h));
        });
    }
    LogList("CreatePathHandle");
    LogBytes("instances", g_instances.data(), g_instances.size() * sizeof(CARP::Instance));

    Guarded("DestroyPathHandle", [&] {
        RPathHandle stray;
        DestroyPathHandleAt(&stray);
        for (int i = 0; i < 3 && fgPathHandles->size > 0; i++) {
            uint32_t k = rng.Below(fgPathHandles->size);
            PathListNode *node = fgPathHandles->Begin();
            while (k-- > 0)
                node = node->next;
            DestroyPathHandleAt(&node->value);
        }
    });
    LogList("DestroyPathHandle");
    Guarded("Purge", [&] { PurgeAt(); });
    Logf("purged %d", fgPathHandles == NULL);
    Guarded("Purge (no list)", [&] { PurgeAt(); });
    Guarded("DestroyPathHandle (no list)", [&] {
        RPathHandle stray;
        DestroyPathHandleAt(&stray);
    });

    fgPathHandles = liveList;
    fgPathIterator = liveIterator;
}

// ---- the helpers

void TestHelpers(Rng &rng) {
    static const uint32_t kEdges[] = { 0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x40000000, 0xc0000000,
                                       0x3f7fffff, 0xbf7fffff, 0x7fc00000, 0x7f800000, 0xff800000, 0x00000001,
                                       0x3effffff, 0x4b000000 };
    for (int i = 0; i < 80; i++) {
        float turns = i < 14 ? FromBits(kEdges[i]) : rng.Range(-3.0f, 3.0f);
        Guarded("turns", [&] {
            Logf("turns %08x: %016llx %016llx %016llx", Bits(turns), Bits(WrapTurnsAt(turns)), Bits(SinTurnsAt(turns)),
                 Bits(TanTurnsAt(turns)));
        });
    }
    for (int i = 0; i < 40; i++) {
        float x = i < 14 ? FromBits(kEdges[i]) : rng.Range(-5.0f, 5.0f);
        float z = i < 14 ? FromBits(kEdges[13 - i]) : rng.Range(-5.0f, 5.0f);
        Guarded("HeadingTurns", [&] { Logf("heading %016llx", Bits(HeadingTurnsAt(x, z))); });
    }
    for (int i = 0; i < 30; i++) {
        alignas(16) Coord4 offset = RandomVector(rng, 10.0f, rng.Unit());
        alignas(16) Coord4 heading = RandomVector(rng, 2.0f, rng.Unit());
        alignas(16) Coord4 out = { 9.0f, 9.0f, 9.0f, 9.0f };
        Guarded("RotateOffsetToHeading", [&] { RotateOffsetAt(&offset, &heading, &out); });
        LogBytes("rotated", &out, sizeof(out));
        Guarded("RotateOffsetToHeading in place", [&] { RotateOffsetAt(&offset, &heading, &offset); });
        LogBytes("rotated", &offset, sizeof(offset));
    }
    for (int i = 0; i < 10; i++) {
        alignas(16) MATRIX4 a, b;
        RandomMatrix(rng, &a);
        b = a;
        Guarded("TurnFrameRows", [&] { TurnFrameRowsAt(&a); });
        Guarded("FlipFrameRows", [&] { FlipFrameRowsAt(&b); });
        LogBytes("frames", &a, sizeof(a));
        LogBytes("frames", &b, sizeof(b));
    }

    WRoadNetworkData &net = fgRoadNetworkData;
    if (net.segments != NULL) {
        alignas(16) uint8_t nav[sizeof(WRoadNav)] = {};
        WRoadNav *n = reinterpret_cast<WRoadNav *>(nav);
        for (int s = 0; s < net.segmentCount; s++) {
            n->segment = int16_t(s);
            Guarded("RoadNavLaneCount", [&] { Logf("lanes %d: %d", s, LaneCountAt(n)); });
        }
    }

    for (int i = 0; i < 6; i++) {
        alignas(16) uint8_t a[sizeof(RCamera)], b[sizeof(RCamera)];
        for (size_t k = 0; k < sizeof(a); k++) {
            a[k] = uint8_t(rng.Next());
            b[k] = uint8_t(rng.Next());
        }
        RCamera *result = NULL;
        Guarded("AssignCamera", [&] {
            result = AssignCameraAt(reinterpret_cast<RCamera *>(a), 0, reinterpret_cast<RCamera *>(b));
        });
        Logf("assign %d", result == reinterpret_cast<RCamera *>(a));
        LogBytes("assigned", a, sizeof(a));
    }

    uint32_t seed = RandomSeed, multiplier = RandomMultiplier;
    static const unsigned kSeeds[] = { 0, 1, 499, 500, 501, 0xffffffff };
    for (int i = 0; i < 12; i++) {
        unsigned s = i < 6 ? kSeeds[i] : rng.Next();
        Guarded("RRandom::StartUp", [&] { StartUpAt(s); });
        Logf("random %u: %08x %08x", s, RandomSeed, RandomMultiplier);
    }
    RandomSeed = seed;
    RandomMultiplier = multiplier;
}

void RunPass(bool original, std::string *log) {
    g_log = log;
    Originals originals(original);
    Rng rng = { 0x6b43a9b5u };
    if (fgLightManager != NULL) {
        TestLightQueries(rng);
        TestHighLevel(rng);
        PutLights(&g_lights);
        TestConstruction();
    }
    if (fgPathHandles != NULL) {
        TestHandles(rng);
        TestLiveList();
    }
    TestOwnList(rng);
    TestHelpers(rng);
    g_log = NULL;
}

void SplitLines(const std::string &text, std::vector<std::string> *lines) {
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        lines->push_back(text.substr(at, end - at));
        at = end + 1;
    }
}

// The live handles, and the instance and state arrays the own-list tests start from: a copy of each handle's
// instance and state (the scene object cleared, a master pointing into the copies), then rejected ones
void GatherPaths() {
    g_liveHandles.clear();
    PathList *list = fgPathHandles;
    if (list != NULL && list->head != NULL) {
        for (PathListNode *node = list->Begin(); node != list->head && g_liveHandles.size() < 40; node = node->next)
            g_liveHandles.push_back(&node->value);
    }
    g_copies.assign(g_liveHandles.size(), HandleCopy());

    size_t n = g_liveHandles.size();
    // The working arrays get their storage now: the masters point into it, and copying the pristine arrays over
    // them later keeps it
    g_instances.reserve(n + 3);
    g_states.reserve(n + 3);
    g_instancesPristine.assign(n + 3, CARP::Instance());
    g_statesPristine.assign(n + 3, ProcAnimState());
    for (size_t i = 0; i < n; i++) {
        memcpy(&g_instancesPristine[i], g_liveHandles[i]->instance, sizeof(CARP::Instance));
        memcpy(&g_statesPristine[i], g_liveHandles[i]->procAnim, sizeof(ProcAnimState));
        g_instancesPristine[i].procAnimIndex = uint16_t(i);
        g_statesPristine[i].sceneObj = NULL;
    }
    for (size_t i = 0; i < n; i++) {
        ProcAnimState &s = g_statesPristine[i];
        if (s.master == NULL)
            continue;
        CARP::Instance *mapped = NULL;
        for (size_t j = 0; j < n; j++)
            if (g_liveHandles[j]->instance == s.master)
                mapped = g_instances.data() + j;
        s.master = mapped;
    }
    // Rejected: no proc-anim flag, another type, no path
    for (size_t k = 0; k < 3; k++) {
        CARP::Instance &instance = g_instancesPristine[n + k];
        ProcAnimState &state = g_statesPristine[n + k];
        if (n > 0) {
            instance = g_instancesPristine[0];
            state = g_statesPristine[0];
        }
        instance.procAnimIndex = uint16_t(n + k);
        instance.flags = k == 0 ? uint8_t(instance.flags & ~0x10) : uint8_t(instance.flags | 0x10);
        state.type = k == 1 ? 3 : kPathAnimType;
        if (k == 2)
            state.path = NULL;
        state.master = NULL;
    }
}

}  // namespace

void LightShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_LIGHTSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;

    GatherPaths();

    if (fgLightManager != NULL)
        TakeLights(&g_lights);
    g_tables.assign(kLightTables, kLightTables + kLightTablesSize);
    g_indices.assign(kLightIndices, kLightIndices + kLightIndicesSize);
    uint32_t tick = GameTick;

    std::string logOriginal, logPort;
    RunPass(true, &logOriginal);
    if (fgLightManager != NULL)
        PutLights(&g_lights);
    GameTick = tick;
    RunPass(false, &logPort);
    if (fgLightManager != NULL)
        PutLights(&g_lights);
    GameTick = tick;

    std::vector<std::string> a, b;
    SplitLines(logOriginal, &a);
    SplitLines(logPort, &b);
    size_t n = a.size() > b.size() ? a.size() : b.size();
    std::vector<std::pair<std::string, int>> byCase;   // differing lines by the case that logged them
    for (size_t i = 0; i < n; i++) {
        g_checks++;
        const std::string none = "(missing)";
        const std::string &x = i < a.size() ? a[i] : none;
        const std::string &y = i < b.size() ? b[i] : none;
        if (x != y) {
            std::string what = none;
            if (!a.empty()) {
                size_t h = i < a.size() ? i : a.size() - 1;
                while (h > 0 && !a[h].empty() && a[h][0] == ' ')
                    h--;
                what = a[h].substr(0, a[h].find(' '));
            }
            if (g_diffs < 10)
                printf("[lights] line %u differs (%s):\n  original %.300s\n  port     %.300s\n", unsigned(i),
                       what.c_str(), x.c_str(), y.c_str());
            g_diffs++;
            size_t k = 0;
            while (k < byCase.size() && byCase[k].first != what)
                k++;
            if (k == byCase.size())
                byCase.push_back(std::make_pair(what, 0));
            byCase[k].second++;
        }
    }
    for (const auto &c : byCase)
        printf("[lights]   %s: %d lines differ\n", c.first.c_str(), c.second);

    printf("[lights] light manager %s, %u path handles\n", fgLightManager != NULL ? "live" : "missing",
           unsigned(g_liveHandles.size()));
    printf("[lights] lights, paths and helpers vs originals: %d cases, %d checks, %d differ%s\n", g_cases / 2, g_checks,
           g_diffs, g_faults ? " (with faults)" : "");
    if (g_faults)
        printf("[lights] %d faults\n", g_faults);
    fflush(stdout);
}
