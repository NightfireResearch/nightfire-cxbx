#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ActorShadow.h"

#include "../anim/Actor.h"
#include "../anim/AnimationDatabase.h"
#include "../anim/Character.h"
#include "../anim/Poser.h"
#include "../data/StdStreams.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"   // XbeAddress

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_ACTORSHADOW=1, once on the first simulation tick: anim/Actor.cpp and anim/Controllers.cpp against the
// originals.
//
// Two passes run the same tests, calling everything at the original addresses: the first with the ported
// functions' originals swapped back in (0x00011020-0x00013c00 and 0x0008c8d0), the second with our jumps. Each
// pass writes a log (results as bits, whole records as bytes, lists and strings by content); the logs must match
// line for line. Records live in static buffers, so their addresses (and the pointers between them) are the same
// in both passes.
//
//   - VU0_quatstoangvel on random quaternion pairs, each component the largest in turn, ties, zeros and times;
//   - the controllers' five getters on random poser times, and the three destructors' vtable writes;
//   - actors of our own (poser, pose matrices, character and matrices all ours, a callback of ours on some):
//     InitializeMatrices, CalculateMatrices as the root and weapon bones move (ground lookups near the live
//     actors), the three getters, IsAnimationDone over poser modes and times, SetTimeScale, the origin functions,
//     SetSuppressAnimationTranslation, CurrentPositionRotateY, RotateActor, RotateActorX, TurnShadowsOff, and
//     DropWeapon at times across its thresholds - SpawnWeapon's call to ActCharacter::SpawnWeapon recorded by a
//     temporary jump to a fake, so nothing is thrown into the world;
//   - copies of the live actors (and their matrices): CalculateMatrices with their own callbacks, the world
//     position, IsAnimationDone on their posers, SpawnWeapon (recorded);
//   - the culling walk (PrepareActorsForCulling, GetNextActorCullInfo, SetActorCull) over a database of our
//     actors, from a restart and from a cursor, and on an empty list; DrawAll and SetupFOVConversions over culled
//     actors; UpdateAll on an empty list;
//   - std::list's _Buynode and _Incsize, KillActorByHandle on actorless nodes and the database's destructor;
//   - std::string: construction, assign (text, part of another, part of itself), erase, _Grow, _Eos, _Tidy, c_str,
//     the destructor, through random scripts;
//   - the animation database's private data's constructor (it loads ALookup.bin; freed after).
// Draw, DrawWeapons, SetupFOVConversion, Update, Fire, the IK and pose-override forwarders, SetNewAnimation and the
// controllers' constructors, Update and StartNewCrossFade drive the renderer, the poser and the animation banks:
// tested in game.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_faults;
bool g_counting;
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
    std::string s = what;
    s += ' ';
    char h[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(h, sizeof(h), "%02x", static_cast<const uint8_t *>(p)[i]);
        s += h;
    }
    Logf("%s", s.c_str());
}

void Case() {
    if (g_counting)
        g_cases++;
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

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f; }
    int Range(int lo, int hi) { return lo + int(Next() % uint32_t(hi - lo + 1)); }
};

// ---- what the tests read of the game

#define ShadowActorDatabase (*(ActActorDatabase **)0x001dd9a0)
#define ShadowCullRestart (*(uint8_t *)0x001dd9a4)
#define ShadowCullCursor (*(PointerListNode **)0x001dd9bc)

// ---- the functions, at their original addresses

typedef void (*QuatsToAngVelFn)(Coord3 *, const Coord4 *, const Coord4 *, float);
typedef float (__fastcall *ControllerFloatFn)(AnimationController *, int);
typedef double (__fastcall *ControllerDoubleFn)(AnimationController *, int);
typedef AnimationController *(__fastcall *ControllerDeleteFn)(AnimationController *, int, unsigned);
typedef void (__fastcall *ControllerVoidFn)(AnimationController *, int);
typedef void (__fastcall *ActorMatrixFn)(ActActor *, int, const MATRIX4 *);
typedef void (__fastcall *ActorBoolArgFn)(ActActor *, int, bool);
typedef void (__fastcall *ActorOutMatrixFn)(ActActor *, int, MATRIX4 *);
typedef void (__fastcall *ActorOutCoord4Fn)(ActActor *, int, Coord4 *);
typedef void (__fastcall *GetWeaponPositionFn)(ActActor *, int, MATRIX4 *, bool, int);
typedef bool (__fastcall *ActorBoolFn)(ActActor *, int);
typedef void (__fastcall *ActorFloatFn)(ActActor *, int, float);
typedef void (__fastcall *ActorCoord3Fn)(ActActor *, int, Coord3 *);
typedef void (__fastcall *ActorVoidFn)(ActActor *, int);
typedef void (*VoidFn)();
typedef int (*CullNextFn)(Coord4 *, float *, bool *, float *);
typedef void (*CullSetFn)(int, bool, float);
typedef PointerListNode *(__fastcall *BuyHeadFn)(ActActorDatabase *, int);
typedef PointerListNode *(__fastcall *BuyNodeFn)(ActActorDatabase *, int, PointerListNode *, PointerListNode *,
                                              ActActor *const *);
typedef void (__fastcall *IncreaseSizeFn)(ActActorDatabase *, int, uint32_t);
typedef void (*KillFn)(PointerListNode *);
typedef void (__fastcall *DatabaseVoidFn)(ActActorDatabase *, int);
typedef void (__fastcall *DrawAllFn)(ActActorDatabase *, int, RViewCamera *, bool, bool);
typedef void (__fastcall *SetupFOVConversionsFn)(ActActorDatabase *, int, RViewCamera *);
typedef GameStd::String *(__fastcall *StringTextFn)(GameStd::String *, int, const char *);
typedef GameStd::String *(__fastcall *StringCopyFn)(GameStd::String *, int, const GameStd::String *);
typedef GameStd::String *(__fastcall *StringAssignTextFn)(GameStd::String *, int, const char *, uint32_t);
typedef GameStd::String *(__fastcall *StringAssignSubFn)(GameStd::String *, int, const GameStd::String *, uint32_t,
                                                         uint32_t);
typedef GameStd::String *(__fastcall *StringEraseFn)(GameStd::String *, int, uint32_t, uint32_t);
typedef bool (__fastcall *StringGrowFn)(GameStd::String *, int, uint32_t, bool);
typedef void (__fastcall *StringSizeFn)(GameStd::String *, int, uint32_t);
typedef void (__fastcall *StringTidyFn)(GameStd::String *, int, bool);
typedef void (__fastcall *StringVoidFn)(GameStd::String *, int);
typedef const char *(__fastcall *StringCStrFn)(GameStd::String *, int);
typedef void (__stdcall *DeallocateFn)(void *, uint32_t);
typedef void *(__fastcall *PrivateDataFn)(void *, int);

const QuatsToAngVelFn QuatsToAngVel = (QuatsToAngVelFn)0x00011930;
const ControllerFloatFn GetCurrentFrame = (ControllerFloatFn)0x00011020;
const ControllerFloatFn GetTotalFrames = (ControllerFloatFn)0x00011030;
const ControllerDoubleFn GetCurrentTimeSeconds = (ControllerDoubleFn)0x00011040;
const ControllerDoubleFn GetTotalTime = (ControllerDoubleFn)0x00011050;
const ControllerDoubleFn GetRemainingTime = (ControllerDoubleFn)0x00011060;
const ControllerDeleteFn kControllerDeletes[3] = {(ControllerDeleteFn)0x000125c0, (ControllerDeleteFn)0x000125f0,
                                                  (ControllerDeleteFn)0x00012620};
const ControllerVoidFn ControllerDestruct = (ControllerVoidFn)0x00012610;
const ActorMatrixFn InitializeMatrices = (ActorMatrixFn)0x000127a0;
const ActorBoolArgFn CalculateMatrices = (ActorBoolArgFn)0x000112e0;
const ActorBoolArgFn SetSuppressAnimationTranslation = (ActorBoolArgFn)0x000124f0;
const ActorOutMatrixFn GetActorLocalPosOri = (ActorOutMatrixFn)0x00011650;
const ActorOutCoord4Fn GetActorWorldPosition = (ActorOutCoord4Fn)0x00011670;
const GetWeaponPositionFn GetWeaponPosition = (GetWeaponPositionFn)0x000116a0;
const ActorBoolFn IsAnimationDone = (ActorBoolFn)0x00012140;
const ActorFloatFn SetTimeScale = (ActorFloatFn)0x00012180;
const ActorFloatFn CurrentPositionRotateY = (ActorFloatFn)0x00012280;
const ActorFloatFn RotateActor = (ActorFloatFn)0x00012350;
const ActorFloatFn RotateActorX = (ActorFloatFn)0x00012420;
const ActorFloatFn DropWeapon = (ActorFloatFn)0x00012c30;
const ActorCoord3Fn ChangeAnimationOrigin = (ActorCoord3Fn)0x00012190;
const ActorCoord3Fn GetAnimationOrigin = (ActorCoord3Fn)0x000121f0;
const ActorCoord3Fn SetAnimationOrigin = (ActorCoord3Fn)0x00012220;
const ActorVoidFn TurnShadowsOff = (ActorVoidFn)0x00012570;
const ActorVoidFn SpawnWeapon = (ActorVoidFn)0x00011ff0;
const VoidFn PrepareActorsForCulling = (VoidFn)0x00012580;
const CullNextFn GetNextActorCullInfo = (CullNextFn)0x00012f40;
const CullSetFn SetActorCull = (CullSetFn)0x0008c8d0;
const BuyHeadFn ListBuyHead = (BuyHeadFn)0x000b8490;
const BuyNodeFn ListBuyNode = (BuyNodeFn)0x000130e0;
const IncreaseSizeFn ListIncreaseSize = (IncreaseSizeFn)0x00013880;
const KillFn KillActorByHandle = (KillFn)0x00013270;
const DatabaseVoidFn DatabaseDestruct = (DatabaseVoidFn)0x00013540;
const DatabaseVoidFn UpdateAll = (DatabaseVoidFn)0x00013060;
const DrawAllFn DrawAll = (DrawAllFn)0x00012fd0;
const SetupFOVConversionsFn SetupFOVConversions = (SetupFOVConversionsFn)0x000130a0;
const StringTextFn StringConstruct = (StringTextFn)0x00013840;
const StringCopyFn StringConstructCopy = (StringCopyFn)0x000136d0;
const StringAssignTextFn StringAssignText = (StringAssignTextFn)0x00013630;
const StringAssignSubFn StringAssignSub = (StringAssignSubFn)0x00013580;
const StringEraseFn StringErase = (StringEraseFn)0x00013480;
const StringGrowFn StringGrow = (StringGrowFn)0x00013300;
const StringSizeFn StringEos = (StringSizeFn)0x00012c80;
const StringTidyFn StringTidy = (StringTidyFn)0x00013110;
const StringVoidFn StringDestruct = (StringVoidFn)0x000132c0;
const StringCStrFn StringCStr = (StringCStrFn)0x00012590;
const DeallocateFn AllocatorDeallocate = (DeallocateFn)0x000125a0;
const PrivateDataFn PrivateDataConstruct = (PrivateDataFn)0x00013ba0;

// ---- a temporary jump over ActCharacter::SpawnWeapon (the original's entry and our port's), to a recording fake

struct TempJump {
    uint8_t *at;
    uint8_t saved[5];

    void Install(uint32_t address, const void *to) {
        at = (uint8_t *)(uintptr_t)address;
        DWORD old;
        VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy(saved, at, 5);
        int32_t rel = int32_t((uintptr_t)to - ((uintptr_t)at + 5));
        at[0] = 0xe9;
        memcpy(at + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
    void Remove() {
        memcpy(at, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
};

void __fastcall FakeSpawnWeapon(ActCharacter *character, int, int weapon, Coord3 *position, Coord3 *direction,
                                Coord3 *velocity, Coord3 *spin) {
    Logf("  spawn %d: p %08x %08x %08x d %08x %08x %08x v %08x %08x %08x s %08x %08x %08x", weapon,
         Bits(position->x), Bits(position->y), Bits(position->z), Bits(direction->x), Bits(direction->y),
         Bits(direction->z), Bits(velocity->x), Bits(velocity->y), Bits(velocity->z), Bits(spin->x), Bits(spin->y),
         Bits(spin->z));
}

// ---- records of our own, at the same addresses in both passes

enum { kFakeActors = 12, kLiveCopies = 8 };

struct alignas(16) MatricesSlot {
    ActActorMatrices m;
};
struct alignas(16) PoseSlot {
    uint8_t bytes[0x110];
};

MatricesSlot g_matrices[kFakeActors];
PoseSlot g_poses[kFakeActors];
ActPoser g_posers[kFakeActors];
ActCharacter g_characters[kFakeActors];
ActActor g_actors[kFakeActors];
MatricesSlot g_liveMatrices[kLiveCopies];
ActActor g_liveActors[kLiveCopies];
alignas(16) uint8_t g_controller[0x40];
alignas(16) uint8_t g_privateData[0x448];
PointerListNode g_cullNodes[kFakeActors + 1];
ActActorDatabase g_cullDatabase;

Coord3 g_anchor;            // a live actor's position, else the origin: where our actors stand

void ShadowCallback(int id, MATRIX4 *transform, Coord4 *velocity) {
    float a = float(id) * 0.37f;
    float c = cosf(a), s = sinf(a);
    memset(transform, 0, sizeof(*transform));
    transform->mtx[0][0] = c;
    transform->mtx[0][2] = -s;
    transform->mtx[1][1] = 1.0f;
    transform->mtx[2][0] = s;
    transform->mtx[2][2] = c;
    transform->mtx[3][0] = float(id) * 0.5f;
    transform->mtx[3][1] = 0.25f;
    transform->mtx[3][2] = -float(id) * 0.75f;
    transform->mtx[3][3] = 1.0f;
    velocity->x = float(id) * 0.1f;
    velocity->y = -float(id) * 0.2f;
    velocity->z = 0.3f;
    velocity->w = 0.0f;
}

void RandomMatrix(MATRIX4 *m, Rng *rng, const Coord3 *at, float spread) {
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++)
            m->mtx[r][c] = rng->Uniform(-1.0f, 1.0f);
        m->mtx[r][3] = 0.0f;
    }
    m->mtx[3][0] = at->x + rng->Uniform(-spread, spread);
    m->mtx[3][1] = at->y + rng->Uniform(-spread, spread);
    m->mtx[3][2] = at->z + rng->Uniform(-spread, spread);
    m->mtx[3][3] = 1.0f;
}

void RandomPoser(ActPoser *poser, Rng *rng) {
    poser->mode = rng->Range(0, 3);
    poser->framesPerSecond = 20.0f;
    poser->frameTime = rng->Range(0, 3) == 0 ? rng->Uniform(0.0f, 0.2f) : 0.05f;
    poser->timeScale = rng->Uniform(0.0f, 2.0f);
    poser->endTime = rng->Uniform(1.0f, 60.0f);
    poser->time = poser->endTime - 1.0f - rng->Uniform(-1.5f, 1.5f);
}

void BuildFakeActor(int i, Rng *rng) {
    ActActorMatrices *m = &g_matrices[i].m;
    memset(m, 0, sizeof(*m));
    float *words = (float *)m;
    for (size_t w = sizeof(WWorldPos) / 4; w < offsetof(ActActorMatrices, animationFrame) / 4; w++)
        words[w] = rng->Uniform(-3.0f, 3.0f);
    RandomMatrix(&m->root, rng, &g_anchor, 6.0f);
    RandomMatrix(&m->weaponBone[0], rng, &g_anchor, 6.0f);
    RandomMatrix(&m->weaponBone[1], rng, &g_anchor, 6.0f);
    m->groundQueryPoint = g_anchor;
    m->groundHeight = rng->Uniform(-2.0f, 2.0f);

    float *pose = (float *)g_poses[i].bytes;
    for (size_t w = 0; w < sizeof(g_poses[i].bytes) / 4; w++)
        pose[w] = rng->Uniform(-2.0f, 2.0f);
    ActPoser *poser = &g_posers[i];
    memset(poser, 0, sizeof(*poser));
    poser->mirrored = rng->Range(0, 1) != 0;
    poser->matrices = (ActPoseMatrices *)g_poses[i].bytes;
    RandomPoser(poser, rng);

    ActCharacter *character = &g_characters[i];
    memset(character, 0, sizeof(*character));
    character->hasShadow = true;

    ActActor *actor = &g_actors[i];
    memset(actor, 0, sizeof(*actor));
    actor->character = character;
    actor->poser = poser;
    actor->matrices = m;
    if (rng->Range(0, 2) == 0) {
        actor->callback = ShadowCallback;
        actor->hasCallback = true;
    }
    actor->callbackId = i * 7 + 1;
    actor->hasWeapon[0] = rng->Range(0, 3) != 0;
    actor->hasWeapon[1] = rng->Range(0, 2) == 0;
    actor->weaponVisible[0] = actor->hasWeapon[0];
    actor->weaponVisible[1] = actor->hasWeapon[1];
    actor->visible = true;
    actor->followGround = rng->Range(0, 3) != 0;
    actor->scale = rng->Uniform(0.5f, 1.5f);
    actor->baseScale = rng->Uniform(0.5f, 1.5f);
    actor->culled = true;
    actor->drawFlags = 3;
    actor->fovScale = 1.0f;
}

void LogActor(const char *what, int i) {
    Logf("%s %d", what, i);
    LogBytes("  actor", &g_actors[i], sizeof(ActActor));
    LogBytes("  matrices", &g_matrices[i].m, sizeof(ActActorMatrices));
    LogBytes("  poser", &g_posers[i], sizeof(ActPoser));
    LogBytes("  pose", g_poses[i].bytes, sizeof(g_poses[i].bytes));
    Logf("  shadow %d", int(g_characters[i].hasShadow));
}

// ---- the tests

void TestQuats(Rng *rng) {
    for (int i = 0; i < 4000; i++) {
        Case();
        Coord4 from, to;
        float *f = &from.x, *t = &to.x;
        for (int c = 0; c < 4; c++) {
            f[c] = rng->Uniform(-1.0f, 1.0f);
            t[c] = rng->Uniform(-1.0f, 1.0f);
        }
        int largest = i % 4;
        f[largest] *= 3.0f;
        if (i % 37 == 0)
            f[(largest + 1) % 4] = -f[largest];      // a tie
        if (i % 53 == 0)
            t[largest] = 0.0f;                       // a zero step
        if (i % 61 == 0)
            memset(&from, 0, sizeof(from));
        float time = i % 3 == 0 ? 0.05f : rng->Uniform(-1.0f, 1.0f);
        Coord3 out = {};
        QuatsToAngVel(&out, &from, &to, time);
        Logf("quat %d: %08x %08x %08x", i, Bits(out.x), Bits(out.y), Bits(out.z));
    }
}

void TestControllers(Rng *rng) {
    ActPoser poser;
    memset(&poser, 0, sizeof(poser));
    AnimationController *controller = (AnimationController *)g_controller;
    for (int i = 0; i < 500; i++) {
        Case();
        memset(g_controller, 0, sizeof(g_controller));
        controller->poser = &poser;
        poser.time = rng->Uniform(-5.0f, 100.0f);
        poser.endTime = rng->Uniform(-5.0f, 100.0f);
        poser.secondsPerFrame = i % 5 == 0 ? 0.05f : rng->Uniform(0.0f, 0.2f);
        float frame = GetCurrentFrame(controller, 0);
        float total = GetTotalFrames(controller, 0);
        double time = GetCurrentTimeSeconds(controller, 0);
        double totalTime = GetTotalTime(controller, 0);
        double remaining = GetRemainingTime(controller, 0);
        Logf("getters %d: %08x %08x %016llx %016llx %016llx", i, Bits(frame), Bits(total), Bits(time),
             Bits(totalTime), Bits(remaining));
    }
    for (int i = 0; i < 3; i++) {
        Case();
        memset(g_controller, 0xcd, sizeof(g_controller));
        AnimationController *result = kControllerDeletes[i](controller, 0, 0);
        Logf("delete %d: same %d", i, int(result == controller));
        LogBytes("  controller", g_controller, sizeof(g_controller));
    }
    memset(g_controller, 0xcd, sizeof(g_controller));
    ControllerDestruct(controller, 0);
    LogBytes("destruct", g_controller, sizeof(g_controller));
}

void TestFakeActors(Rng *rng) {
    for (int i = 0; i < kFakeActors; i++) {
        BuildFakeActor(i, rng);
        ActActor *actor = &g_actors[i];
        ActActorMatrices *m = &g_matrices[i].m;

        Case();
        alignas(16) MATRIX4 transform;
        RandomMatrix(&transform, rng, &g_anchor, 4.0f);
        InitializeMatrices(actor, 0, &transform);
        LogActor("initialised", i);

        for (int step = 0; step < 6; step++) {
            Case();
            float move = step % 2 == 0 ? 0.1f : 1.0f;
            m->root.mtx[3][0] += rng->Uniform(-move, move);
            m->root.mtx[3][2] += rng->Uniform(-move, move);
            RandomMatrix(&m->weaponBone[0], rng, &g_anchor, 6.0f);
            RandomMatrix(&m->weaponBone[1], rng, &g_anchor, 6.0f);
            CalculateMatrices(actor, 0, false);
            LogActor("calculated", i);
        }

        Case();
        alignas(16) MATRIX4 out;
        alignas(16) Coord4 position;
        GetActorLocalPosOri(actor, 0, &out);
        LogBytes("local", &out, sizeof(out));
        GetActorWorldPosition(actor, 0, &position);
        LogBytes("world", &position, sizeof(position));
        for (int weapon = 0; weapon < 2; weapon++) {
            GetWeaponPosition(actor, 0, &out, weapon == 0, weapon);
            LogBytes("weapon", &out, sizeof(out));
        }

        for (int k = 0; k < 24; k++) {
            Case();
            RandomPoser(&g_posers[i], rng);
            if (k % 6 == 0)
                g_posers[i].time = g_posers[i].endTime - 1.0f;
            Logf("done %d", int(IsAnimationDone(actor, 0)));
        }

        Case();
        SetTimeScale(actor, 0, rng->Uniform(0.0f, 3.0f));
        Coord3 origin = {rng->Uniform(-5.0f, 5.0f), rng->Uniform(-5.0f, 5.0f), rng->Uniform(-5.0f, 5.0f)};
        ChangeAnimationOrigin(actor, 0, &origin);
        LogBytes("changed", &origin, sizeof(origin));
        LogActor("change origin", i);
        GetAnimationOrigin(actor, 0, &origin);
        LogBytes("got", &origin, sizeof(origin));
        origin.x += 1.0f;
        SetAnimationOrigin(actor, 0, &origin);
        LogBytes("set", &origin, sizeof(origin));
        LogActor("set origin", i);
        SetSuppressAnimationTranslation(actor, 0, rng->Range(0, 1) != 0);
        LogActor("suppress", i);

        for (int k = 0; k < 4; k++) {
            Case();
            float turns = rng->Uniform(-1.5f, 1.5f);
            CurrentPositionRotateY(actor, 0, turns);
            RotateActor(actor, 0, turns * 0.5f);
            RotateActorX(actor, 0, -turns);
            LogActor("rotated", i);
        }

        Case();
        TurnShadowsOff(actor, 0);
        Logf("shadow off %d", int(g_characters[i].hasShadow));

        const float times[] = {0.0f, 0.5f, 0.949f, 0.95f, 1.0f, 1.5f, 1.949f, 1.95f, 2.5f};
        for (size_t k = 0; k < sizeof(times) / sizeof(times[0]); k++) {
            Case();
            actor->weaponVisible[0] = k % 2 == 0;
            CalculateMatrices(actor, 0, false);
            Logf("drop %u", unsigned(k));
            DropWeapon(actor, 0, times[k]);
            Logf("  visible %d", int(actor->weaponVisible[0]));
        }
    }
}

void TestLiveActors(Rng *rng) {
    ActActorDatabase *database = ShadowActorDatabase;
    if (database == NULL || database->head == NULL) {
        Logf("no actor database");
        return;
    }
    int n = 0;
    for (PointerListNode *node = database->head->next; node != database->head && n < kLiveCopies;
         node = node->next) {
        ActActor *live = static_cast<ActActor *>(node->value);
        if (live == NULL || live->matrices == NULL)
            continue;
        Case();
        g_liveActors[n] = *live;
        g_liveMatrices[n].m = *live->matrices;
        ActActor *copy = &g_liveActors[n];
        copy->matrices = &g_liveMatrices[n].m;
        Logf("live %d: callback %d weapons %d %d", n, int(copy->callback != NULL), int(copy->hasWeapon[0]),
             int(copy->hasWeapon[1]));
        CalculateMatrices(copy, 0, false);
        LogBytes("  actor", copy, sizeof(ActActor));
        LogBytes("  matrices", copy->matrices, sizeof(ActActorMatrices));
        alignas(16) Coord4 position;
        GetActorWorldPosition(copy, 0, &position);
        LogBytes("  world", &position, sizeof(position));
        Logf("  done %d", int(IsAnimationDone(copy, 0)));
        if (copy->hasWeapon[0])
            SpawnWeapon(copy, 0);
        n++;
    }
    Logf("live copies %d", n);
}

void TestCulling(Rng *rng) {
    ActActorDatabase *savedDatabase = ShadowActorDatabase;
    uint8_t savedRestart = ShadowCullRestart;
    PointerListNode *savedCursor = ShadowCullCursor;

    PointerListNode *head = &g_cullNodes[kFakeActors];
    PointerListNode *prev = head;
    for (int i = 0; i < kFakeActors; i++) {
        g_cullNodes[i].value = &g_actors[i];
        g_cullNodes[i].prev = prev;
        prev->next = &g_cullNodes[i];
        prev = &g_cullNodes[i];
        g_actors[i].culled = rng->Range(0, 1) != 0;
    }
    prev->next = head;
    head->prev = prev;
    head->value = NULL;
    memset(&g_cullDatabase, 0, sizeof(g_cullDatabase));
    g_cullDatabase.head = head;
    g_cullDatabase.size = kFakeActors;
    ShadowActorDatabase = &g_cullDatabase;

    for (int round = 0; round < 3; round++) {
        if (round == 1) {
            ShadowCullRestart = 0;
            ShadowCullCursor = &g_cullNodes[2];
        } else {
            PrepareActorsForCulling();
        }
        for (int k = 0; k < kFakeActors + 3; k++) {
            Case();
            Coord4 sphere = {};
            float height = 0.0f, farScale = 0.0f;
            bool checkFar = false;
            int item = GetNextActorCullInfo(&sphere, &height, &checkFar, &farScale);
            int index = -2;
            for (int i = 0; i < kFakeActors; i++) {
                if (item == (int)(intptr_t)&g_actors[i])
                    index = i;
            }
            if (item == -1)
                index = -1;
            Logf("cull %d %d: %d %08x %08x %08x %08x %08x %d %08x restart %d", round, k, index, Bits(sphere.x),
                 Bits(sphere.y), Bits(sphere.z), Bits(sphere.w), Bits(height), int(checkFar), Bits(farScale),
                 int(ShadowCullRestart));
            if (index >= 0) {
                SetActorCull(item, rng->Range(0, 1) != 0, rng->Uniform(0.0f, 100.0f));
                Logf("  culled %d", int(g_actors[index].culled));
            }
        }
    }

    for (int i = 0; i < kFakeActors; i++)
        g_actors[i].culled = true;
    DrawAll(&g_cullDatabase, 0, NULL, true, true);
    SetupFOVConversions(&g_cullDatabase, 0, NULL);
    Logf("culled walks done");

    g_cullDatabase.head = NULL;
    ShadowCullRestart = 1;
    Logf("empty: %d", GetNextActorCullInfo(NULL, NULL, NULL, NULL));

    head->next = head;
    head->prev = head;
    g_cullDatabase.head = head;
    UpdateAll(&g_cullDatabase, 0);
    Logf("update walk done");

    ShadowActorDatabase = savedDatabase;
    ShadowCullRestart = savedRestart;
    ShadowCullCursor = savedCursor;
}

void LogList(const char *what, const ActActorDatabase *database) {
    std::string s;
    for (PointerListNode *node = database->head->next; node != database->head; node = node->next) {
        char text[16];
        int index = -1;
        for (int i = 0; i < kFakeActors; i++) {
            if (node->value == &g_actors[i])
                index = i;
        }
        snprintf(text, sizeof(text), " %d", index);
        s += text;
    }
    Logf("%s: size %u:%s", what, database->size, s.c_str());
}

void TestList(Rng *rng) {
    ActActorDatabase *savedDatabase = ShadowActorDatabase;
    for (int round = 0; round < 20; round++) {
        Case();
        ActActorDatabase database;
        memset(&database, 0, sizeof(database));
        database.head = ListBuyHead(&database, 0);
        database.size = 0;
        int count = rng->Range(0, 8);
        std::vector<PointerListNode *> nodes;
        for (int i = 0; i < count; i++) {
            ActActor *value = rng->Range(0, 2) == 0 ? NULL : &g_actors[rng->Range(0, kFakeActors - 1)];
            bool front = rng->Range(0, 1) != 0;
            PointerListNode *next = front ? database.head->next : database.head;
            PointerListNode *node = ListBuyNode(&database, 0, next, next->prev, &value);
            ListIncreaseSize(&database, 0, 1);
            next->prev = node;
            node->prev->next = node;
            nodes.push_back(node);
        }
        LogList("built", &database);
        ShadowActorDatabase = &database;
        for (size_t i = 0; i < nodes.size(); i++) {
            if (nodes[i]->value == NULL && rng->Range(0, 1) == 0) {
                KillActorByHandle(nodes[i]);
                LogList("killed", &database);
            }
        }
        ShadowActorDatabase = savedDatabase;
        DatabaseDestruct(&database, 0);
        Logf("destroyed: %d %u", int(database.head == NULL), database.size);
    }
}

const char kCharset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _.,";

void RandomText(char *text, int length, Rng *rng) {
    for (int i = 0; i < length; i++)
        text[i] = kCharset[rng->Range(0, int(sizeof(kCharset)) - 2)];
    text[length] = 0;
}

const char *StringData(const GameStd::String *s) {
    return s->capacity < 16 ? s->text.buffer : s->text.pointer;
}

void LogString(const char *what, int i, GameStd::String *s) {
    Logf("%s %d: size %u capacity %u buffer %d \"%.*s\"", what, i, s->size, s->capacity, int(s->capacity < 16),
         int(s->size), StringData(s));
}

void TestStrings(Rng *rng) {
    enum { kStrings = 4 };
    GameStd::String strings[kStrings];
    char text[256];
    for (int i = 0; i < kStrings; i++) {
        RandomText(text, rng->Range(0, 40), rng);
        StringConstruct(&strings[i], 0, text);
        LogString("construct", i, &strings[i]);
    }
    for (int step = 0; step < 3000; step++) {
        Case();
        int i = rng->Range(0, kStrings - 1);
        int j = rng->Range(0, kStrings - 1);
        GameStd::String *s = &strings[i];
        GameStd::String *r = &strings[j];
        switch (rng->Range(0, 9)) {
        case 0: {
            int length = rng->Range(0, 60);
            RandomText(text, length, rng);
            StringAssignText(s, 0, text, uint32_t(length));
            break;
        }
        case 1: {
            uint32_t offset = r->size == 0 ? 0 : uint32_t(rng->Range(0, int(r->size)));
            uint32_t count = rng->Range(0, 3) == 0 ? 0xffffffffu : uint32_t(rng->Range(0, 40));
            StringAssignSub(s, 0, r, offset, count);
            break;
        }
        case 2: {
            if (s->size > 0) {
                uint32_t offset = uint32_t(rng->Range(0, int(s->size) - 1));
                StringAssignText(s, 0, StringData(s) + offset, uint32_t(rng->Range(0, int(s->size) + 4)));
            }
            break;
        }
        case 3: {
            uint32_t offset = uint32_t(rng->Range(0, int(s->size)));
            uint32_t count = rng->Range(0, 3) == 0 ? 0xffffffffu : uint32_t(rng->Range(0, 20));
            StringErase(s, 0, offset, count);
            break;
        }
        case 4: {
            uint32_t size = uint32_t(rng->Range(0, 120));
            bool grown = StringGrow(s, 0, size, rng->Range(0, 1) != 0);
            Logf("grow %u: %d", size, int(grown));
            break;
        }
        case 5:
            StringEos(s, 0, uint32_t(rng->Range(0, int(s->size))));
            break;
        case 6:
            StringTidy(s, 0, true);
            break;
        case 7: {
            GameStd::String copy;
            StringConstructCopy(&copy, 0, r);
            LogString("copy", j, &copy);
            Logf("cstr %d", int(StringCStr(&copy, 0) == StringData(&copy)));
            StringDestruct(&copy, 0);
            Logf("destroyed %u %u", copy.size, copy.capacity);
            break;
        }
        case 8: {
            uint32_t offset = s->size == 0 ? 0 : uint32_t(rng->Range(0, int(s->size)));
            StringAssignSub(s, 0, s, offset, uint32_t(rng->Range(0, 30)));
            break;
        }
        default: {
            uint32_t bytes = uint32_t(rng->Range(1, 64));
            void *block = rng->Range(0, 1) == 0 ? NULL : UMemory::FastAlloc(bytes, "ActorShadow");
            AllocatorDeallocate(block, bytes);
            Logf("deallocated %d", int(block != NULL));
            break;
        }
        }
        LogString("string", i, s);
    }
    for (int i = 0; i < kStrings; i++)
        StringDestruct(&strings[i], 0);
}

void TestPrivateData(Rng *rng) {
    // The constructor hands FileLoadz's answer to MEM_size unchecked: run it only if the file loads now
    void *probe = UFileLoader::FileLoadz("data\\actors\\anims\\ALookup.bin", 0);
    if (probe == NULL) {
        Logf("private data: ALookup.bin not loadable - skipped");
        return;
    }
    UMemory::Free(probe);
    Case();
    memset(g_privateData, 0xcd, sizeof(g_privateData));
    void *result = PrivateDataConstruct(g_privateData, 0);
    PrivateData *data = (PrivateData *)g_privateData;
    Logf("private data: same %d", int(result == g_privateData));
    LogBytes("  banks", data->banks, sizeof(data->banks));
    Logf("  counts %d %d size %u", data->bankCount, data->animCount, data->lookupSize);
    if (data->lookup != NULL) {
        uint32_t hash = 2166136261u;
        const uint8_t *bytes = (const uint8_t *)data->lookup;
        for (uint32_t i = 0; i < data->lookupSize; i++)
            hash = (hash ^ bytes[i]) * 16777619u;
        Logf("  lookup %08x", hash);
        UMemory::Free(data->lookup);
    }
}

bool Safe(void (*test)(Rng *), Rng *rng) {
#ifdef _MSC_VER
    __try {
        test(rng);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    test(rng);
    return true;
#endif
}

void RunPass(std::string *log, bool original) {
    g_log = log;
    if (original) {
        XbeOriginal_RestoreRange(0x00011020, 0x00013c00, true);
        XbeOriginal_Restore(0x0008c8d0, true);
    }
    // The original actor code calls ActCharacter::SpawnWeapon at its address, ours calls our port: both entries
    TempJump spawn, spawnPort;
    spawn.Install(0x000147f0, (const void *)FakeSpawnWeapon);
    spawnPort.Install(uint32_t(XbeAddress(&ActCharacter::SpawnWeapon)), (const void *)FakeSpawnWeapon);
    void (*const tests[])(Rng *) = {TestQuats, TestControllers, TestFakeActors, TestLiveActors, TestCulling,
                                    TestList, TestStrings, TestPrivateData};
    uint32_t seed = 0x4c7a0e1u;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        Rng rng = {seed + uint32_t(i) * 7919u};
        if (!Safe(tests[i], &rng))
            Logf("fault in test %u", unsigned(i));
    }
    spawnPort.Remove();
    spawn.Remove();
    if (original) {
        XbeOriginal_RestoreRange(0x00011020, 0x00013c00, false);
        XbeOriginal_Restore(0x0008c8d0, false);
    }
    g_log = NULL;
}

void SplitLines(const std::string &text, std::vector<std::string> *lines) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        lines->push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

}  // namespace

void ActorShadow_Run(void) {
    const char *setting = getenv("NIGHTFIRE_ACTORSHADOW");
    if (setting == NULL || atoi(setting) == 0)
        return;

    g_anchor.x = g_anchor.y = g_anchor.z = 0.0f;
    ActActorDatabase *database = ShadowActorDatabase;
    if (database != NULL && database->head != NULL && database->head->next != database->head) {
        const ActActor *first = static_cast<const ActActor *>(database->head->next->value);
        if (first != NULL && first->matrices != NULL) {
            g_anchor.x = first->matrices->world.mtx[3][0];
            g_anchor.y = first->matrices->world.mtx[3][1];
            g_anchor.z = first->matrices->world.mtx[3][2];
        }
    }

    std::string original, ours;
    g_counting = true;
    RunPass(&original, true);
    g_counting = false;
    RunPass(&ours, false);

    std::vector<std::string> a, b;
    SplitLines(original, &a);
    SplitLines(ours, &b);
    size_t lines = a.size() > b.size() ? a.size() : b.size();
    int differ = 0;
    for (size_t i = 0; i < lines; i++) {
        const std::string *x = i < a.size() ? &a[i] : NULL;
        const std::string *y = i < b.size() ? &b[i] : NULL;
        if (x != NULL && y != NULL && *x == *y)
            continue;
        if (differ < 10)
            printf("[actor]   line %u: original \"%.200s\" ours \"%.200s\"\n", unsigned(i),
                   x != NULL ? x->c_str() : "(none)", y != NULL ? y->c_str() : "(none)");
        differ++;
    }
    printf("[actor] quatstoangvel, controllers, actors, live copies, culling, list, strings, private data: %d cases, "
           "%u checks, %d differ (%d faults)\n", g_cases, unsigned(lines), differ, g_faults);
    fflush(stdout);
}
