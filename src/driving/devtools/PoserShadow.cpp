#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "PoserShadow.h"

#include "../anim/AnimationDatabase.h"
#include "../anim/IK.h"
#include "../anim/Poser.h"
#include "../anim/Weapon.h"
#include "../eagl/anim/AnimDeltaF.h"
#include "../eagl/anim/AnimObjects.h"
#include "../eagl/anim/FnAnim.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../world/SoundMap.h"          // RefCounterMapBuyHead
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_POSERSHADOW=1, once on the first simulation tick (mission 1 has actors with weapons then).
//
// Two passes run the same tests, calling everything at the original addresses: the first with the ported
// functions' originals swapped back in (0x00018550-0x000197f0 and 0x0001ab20-0x0001bfa0, less core's two URefCounter entries),
// the second with our jumps. Each pass writes a log - results as bits, records as bytes, fresh allocations and
// trees by content - and the logs must match line for line. Live state a test writes is saved before it and put
// back after, so both passes start each test from the same state and the game goes on as before.
//
//   - each live actor's poser: DoSkeletonPose as it stands, at perturbed times with the root flags flipped, in the
//     manual mode with random times and weights, after CalcSnapAndCorrectionMatrices and SetNormalAnimation on
//     perturbed placements; DoInitialPoses; AdvanceTime; Rotate, RotateX, the origin and placement setters and
//     getters, the bone getters, Skin; a whole cross-fade (SetupCrossFadeBlend, SetCrossFadeBlendAnimation, then
//     DoSkeletonPose, CalcCrossFadeTimes and AdvanceTime until FinishCrossFadeBlend frees the copy). Logged: the
//     poser, its matrices, the skeleton's pose buffers and local matrices, the skin matrices, DoMainPose's root
//     scratch, the IK solvers and pose overrides;
//   - posers of the test's own over recording fake channels: DoEventPose in every mode (events are not fired into
//     the game), CalcCrossFadeTimes, FinishCrossFadeBlend and AdvanceTime over random times and lengths, the
//     clamps and the hold flag; Construct and Destruct over the live skeletons;
//   - weapons: FloatAbs; CurrentMuzzleFlashStrength, the three transforms (both, one or no matrices; w near and
//     far from 1), GetVelocity, SetTransformToWorldSpace and SetOwner on records of our own; StartMuzzleFlash and
//     SetEventDynamicData on the live weapons (the random generator and the event block saved and put back);
//   - URefCounter<WeaponInfo>'s tree code on a map of our own: random inserts, erases, RemoveReference, range
//     erases and the destructor, the tree logged after every step.
// Rendering (Render, ManualRender, RenderShellCasings), the weapons' construction and the database (they load
// files and link scene objects) are tested in game.
//
// The animation channels keep their last decoded keys, and a delta channel's values differ by an ulp between stepping
// to a key from the one before and decoding it afresh; so each live case starts with the poser's channels' keys
// forgotten (in both passes), and leaves them forgotten.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_faults;
bool g_counting;
std::string *g_log;

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[1024];
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
        if (s.size() > 900) {
            Logf("%s", s.c_str());
            s = "  ";
        }
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
    bool Flip() { return (Next() & 1) != 0; }
};

// ---- what the tests read of the game

struct ShadowCharacter {
    uint8_t unknown00[0x24];
    ActWeapon *weapons[2];              // +0x24
};

struct ShadowActor {
    ShadowCharacter *character;         // +0x00
    ActPoser *poser;                    // +0x04
};

struct ShadowActorNode {
    ShadowActorNode *next;
    ShadowActorNode *prev;
    ShadowActor *actor;
};

struct ShadowActorDatabase {
    uint32_t allocator;
    ShadowActorNode *actors;            // +0x04 the list's head
};

#define ShadowActors (*(ShadowActorDatabase **)0x001dd9a0)
#define ShadowSkinMatrices (*(MATRIX4 **)0x001dd9fc)
#define ShadowRootScratch ((void *)0x001b4a90)
#define ShadowRandomSeed (*(uint32_t *)0x001c45c4)
#define ShadowStepCount (*(int32_t *)0x00234e34)
#define ShadowPlayerPhysics (*(PhysicsObject ***)0x00234e40)
#define ShadowEventBlock ((void *)0x001e47e8)

constexpr unsigned kEventBlockSize = 0x68;
constexpr unsigned kRootScratchSize = 0x60;

// ---- the functions, at their original addresses

typedef void (__fastcall *PoserFn)(ActPoser *, int);
typedef ActPoser *(__fastcall *PoserConstructFn)(ActPoser *, int, ActSkeleton *, ActEvents *, bool, bool);
typedef void (__fastcall *PoserMatrixFn)(ActPoser *, int, MATRIX4 *);
typedef void (__fastcall *PoserConstMatrixFn)(ActPoser *, int, const MATRIX4 *);
typedef void (__fastcall *PoserBoneFn)(ActPoser *, int, int, MATRIX4 *);
typedef void (__fastcall *PoserVectorFn)(ActPoser *, int, float *);
typedef void (__fastcall *PoserTurnFn)(ActPoser *, int, float);
typedef void (__fastcall *PoserNormalFn)(ActPoser *, int, ActAnimGroup *, float, const MATRIX4 *);
typedef void (__fastcall *PoserCrossFadeFn)(ActPoser *, int, ActAnimGroup *, const MATRIX4 *);
typedef void (__fastcall *PoserSetupFn)(ActPoser *, int, float, bool);
typedef void (__fastcall *PoserTimesFn)(ActPoser *, int, float *, float *, float *, float *, float *);
typedef void (__fastcall *CrossFadeDestructFn)(ActCrossFadeBlendData *, int);
typedef float (*FloatAbsFn)(float);
typedef double (__fastcall *StrengthFn)(ActWeapon *, int);
typedef void (__fastcall *WeaponMatrixFn)(ActWeapon *, int, MATRIX4 *);
typedef void (__fastcall *WeaponPointFn)(ActWeapon *, int, Coord4 *);
typedef Coord3 *(__fastcall *WeaponVelocityFn)(ActWeapon *, int);
typedef void (__fastcall *WeaponSetTransformsFn)(ActWeapon *, int, const MATRIX4 *, const MATRIX4 *);
typedef void (__fastcall *WeaponOwnerFn)(ActWeapon *, int, PhysicsObject *);
typedef void (__fastcall *WeaponFlashFn)(ActWeapon *, int, const Coord3 *);
typedef void (__fastcall *WeaponFn)(ActWeapon *, int);
typedef RefCounterInsertResult *(__fastcall *TreeInsertFn)(URefCounterMap *, int, RefCounterInsertResult *,
                                                           const RefCounterValue *);
typedef RefCounterNode **(__fastcall *TreeEraseFn)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *);
typedef RefCounterNode **(__fastcall *TreeEraseRangeFn)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *,
                                                        RefCounterNode *);
typedef bool (__fastcall *TreeRemoveFn)(URefCounterMap *, int, void *);
typedef void (__fastcall *TreeDestructFn)(URefCounterMap *, int);

#define Poser_Construct ((PoserConstructFn)0x00018550)
#define Poser_GetRootBonePosOri ((PoserMatrixFn)0x000186f0)
#define Poser_GetWeaponBonePosOri ((PoserBoneFn)0x00018720)
#define Poser_ChangeAnimationOrigin ((PoserVectorFn)0x00018750)
#define Poser_GetAnimationOrigin ((PoserVectorFn)0x00018780)
#define Poser_GetInitialTbOu ((PoserMatrixFn)0x000187b0)
#define Poser_SetupCrossFadeBlend ((PoserSetupFn)0x000187d0)
#define Poser_CalcCrossFadeTimes ((PoserTimesFn)0x00018850)
#define Poser_Skin ((PoserFn)0x00018940)
#define Poser_DoEventPose ((PoserFn)0x00018960)
#define CrossFade_Destruct ((CrossFadeDestructFn)0x00018ac0)
#define Poser_SetAnimationOrigin ((PoserVectorFn)0x00018b30)
#define Poser_SetInitialTbOu ((PoserConstMatrixFn)0x00018b80)
#define Poser_Rotate ((PoserTurnFn)0x00018bd0)
#define Poser_RotateX ((PoserTurnFn)0x00018cf0)
#define Poser_CalcSnapAndCorrectionMatrices ((PoserConstMatrixFn)0x00018e10)
#define Poser_SetNormalAnimation ((PoserNormalFn)0x00018fe0)
#define Poser_SetCrossFadeBlendAnimation ((PoserCrossFadeFn)0x00019040)
#define Poser_Destruct ((PoserFn)0x00019580)
#define Poser_FinishCrossFadeBlend ((PoserFn)0x00019610)
#define Poser_AdvanceTime ((PoserFn)0x00019710)
#define Poser_DoSkeletonPose ((PoserFn)0x00019750)
#define Poser_DoInitialPoses ((PoserFn)0x000197c0)
#define Weapon_SetOwner ((WeaponOwnerFn)0x0001ad70)
#define Weapon_SetEventDynamicData ((WeaponFn)0x0001adc0)
#define Weapon_SetTransformToWorldSpace ((WeaponSetTransformsFn)0x0001ad00)
#define Weapon_CurrentMuzzleFlashStrength ((StrengthFn)0x0001ae30)
#define Weapon_TransformToWorldSpace ((WeaponMatrixFn)0x0001b050)
#define Weapon_TransformMatrixToWorldSpace ((WeaponMatrixFn)0x0001b090)
#define Weapon_GetVelocity ((WeaponVelocityFn)0x0001b130)
#define Weapon_FloatAbs ((FloatAbsFn)0x0001b160)
#define Weapon_TransformPointToWorldSpace ((WeaponPointFn)0x0001b1e0)
#define Weapon_StartMuzzleFlash ((WeaponFlashFn)0x0001b270)
#define WeaponTree_EraseAt ((TreeEraseFn)0x0001b4a0)
#define WeaponTree_EraseRange ((TreeEraseRangeFn)0x0001ba00)
#define WeaponTree_RemoveReference ((TreeRemoveFn)0x0001ba80)
#define WeaponTree_InsertUnique ((TreeInsertFn)0x0001baf0)
#define WeaponTree_Destruct ((TreeDestructFn)0x0001be60)

// ---- saving and restoring what a test writes

struct Saved {
    std::vector<std::pair<void *, std::string>> regions;

    void Add(void *p, size_t n) {
        if (p == NULL || n == 0)
            return;
        regions.push_back(std::make_pair(p, std::string(static_cast<const char *>(p), n)));
    }
    void Log(const char *what) const {
        for (size_t i = 0; i < regions.size(); i++) {
            char label[64];
            snprintf(label, sizeof(label), "%s %u", what, unsigned(i));
            LogBytes(label, regions[i].first, regions[i].second.size());
        }
    }
    void Restore() const {
        for (size_t i = regions.size(); i-- > 0;)
            memcpy(regions[i].first, regions[i].second.data(), regions[i].second.size());
    }
};

int BoneCount(const ActPoser *poser) {
    return poser->skeleton->skeleton->count;
}

// Everything a poser's tests may write
void AddPoserRegions(ActPoser *poser, Saved *saved) {
    int bones = BoneCount(poser);
    saved->Add(poser, sizeof(ActPoser));
    ActPoseMatrices *matrices = poser->matrices;
    saved->Add(matrices, sizeof(ActPoseMatrices));
    saved->Add(matrices->buffer, matrices->count * sizeof(MATRIX4));
    ActSkeleton *skeleton = poser->skeleton;
    saved->Add(skeleton->stillPose, bones * 12 * sizeof(float));
    saved->Add(skeleton->pose, bones * 12 * sizeof(float));
    saved->Add(skeleton->matrices, bones * sizeof(MATRIX4));
    saved->Add(ShadowSkinMatrices, bones * sizeof(MATRIX4));
    saved->Add(ShadowRootScratch, kRootScratchSize);
    if (poser->crossFade != NULL) {
        saved->Add(poser->crossFade, sizeof(ActCrossFadeBlendData));
        saved->Add(poser->crossFade->matrices.buffer, poser->crossFade->matrices.count * sizeof(MATRIX4));
    }
    ActIKSolverArray *solvers = poser->ikSolvers;
    if (solvers != NULL) {
        saved->Add(solvers, sizeof(ActIKSolverArray));
        saved->Add(solvers->solvers, solvers->count * sizeof(ActIKSolver *));
        for (int i = 0; i < solvers->count; i++) {
            if (solvers->solvers[i] == NULL)
                continue;
            saved->Add(solvers->solvers[i], sizeof(ActIKSolver));
            saved->Add(solvers->solvers[i]->ik, sizeof(ActIK));
        }
    }
    ActGlobalPoseOverrideArray *overrides = poser->poseOverrides;
    if (overrides != NULL) {
        saved->Add(overrides, sizeof(ActGlobalPoseOverrideArray));
        saved->Add(overrides->bones, overrides->count * sizeof(int32_t));
        saved->Add(overrides->matrices, overrides->count * sizeof(Transform));
        saved->Add(overrides->weights, overrides->count * sizeof(float));
        saved->Add(overrides->set, overrides->count * sizeof(bool));
    }
}

// The regions as they are now (the saved copy holds them as they were)
void LogRegions(const Saved &saved, const char *what) {
    for (size_t i = 0; i < saved.regions.size(); i++) {
        char label[64];
        snprintf(label, sizeof(label), "%s %u", what, unsigned(i));
        LogBytes(label, saved.regions[i].first, saved.regions[i].second.size());
    }
}

bool SafeCall(void (*test)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        test(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    test(context);
    return true;
#endif
}

void RandomMatrix(Rng *rng, MATRIX4 *m, float scale) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            m->mtx[r][c] = rng->Uniform(-scale, scale);
}

// ---- 1. the live posers

std::vector<ActPoser *> g_posers;
std::vector<ActWeapon *> g_weapons;

struct LivePoserCase {
    ActPoser *poser;
    Rng *rng;
    int kind;
};

// The cross-fade's state, by content (the copy is a fresh allocation)
void LogCrossFade(const ActCrossFadeBlendData *fade) {
    if (fade == NULL) {
        Logf("  no cross-fade");
        return;
    }
    LogBytes("  fade matrices", fade->matrices.matrices, sizeof(fade->matrices.matrices));
    LogBytes("  fade buffer", fade->matrices.buffer, fade->matrices.count * sizeof(MATRIX4));
    Logf("  fade %d %08x %08x %08x %08x %d current-is-buffer %d", fade->matrices.count, Bits(fade->length),
         Bits(fade->step), Bits(fade->frame), Bits(fade->endFrame), fade->unknown120,
         fade->matrices.current == fade->matrices.buffer);
    if (fade->animation != NULL)
        Logf("  fade animation %08x %08x %d %d", Bits(fade->animation->length), Bits(fade->animation->secondLength),
             fade->animation->index, fade->animation->bank);
}

void LogPoserWithoutFade(const ActPoser *poser) {
    ActPoser copy = *poser;
    copy.crossFade = NULL;
    LogBytes("  poser", &copy, sizeof(copy));
    LogCrossFade(poser->crossFade);
}

void RunLivePoserCase(void *context) {
    LivePoserCase *c = static_cast<LivePoserCase *>(context);
    ActPoser *poser = c->poser;
    Rng *rng = c->rng;
    alignas(16) MATRIX4 placement = poser->matrices->matrices[kPoseTbOu];
    switch (c->kind) {
    case 0:
        Poser_DoSkeletonPose(poser, 0);
        break;
    case 1:
        poser->nextTime = rng->Uniform(-2.0f, poser->length + 4.0f);
        poser->suppressTranslation = rng->Flip();
        poser->mirrored = rng->Flip();
        Poser_DoSkeletonPose(poser, 0);
        break;
    case 2:
        Poser_DoInitialPoses(poser, 0);
        break;
    case 3:
        for (int r = 0; r < 4; r++)
            for (int col = 0; col < 3; col++)
                placement.mtx[r][col] += rng->Uniform(-0.5f, 0.5f);
        poser->hasMatrixCallback = rng->Flip();
        poser->mirrored = rng->Flip();
        Poser_CalcSnapAndCorrectionMatrices(poser, 0, &placement);
        Poser_DoSkeletonPose(poser, 0);
        break;
    case 4:
        placement.mtx[3][0] += rng->Uniform(-10.0f, 10.0f);
        placement.mtx[3][2] += rng->Uniform(-10.0f, 10.0f);
        Poser_SetNormalAnimation(poser, 0, poser->animation, rng->Uniform(0.0f, poser->length / 20.0f + 1.0f),
                                 &placement);
        Poser_DoSkeletonPose(poser, 0);
        break;
    case 5:
        poser->mode = kPoseModeManual;
        poser->manualFromPrevious = rng->Uniform(0.0f, poser->length);
        poser->manualFromTime = poser->manualFromPrevious + rng->Uniform(0.0f, 2.0f);
        poser->manualToPrevious = rng->Uniform(0.0f, poser->length);
        poser->manualToTime = poser->manualToPrevious + rng->Uniform(0.0f, 2.0f);
        poser->manualWeight = rng->Uniform(-0.2f, 1.2f);
        poser->suppressTranslation = rng->Flip();
        Poser_DoSkeletonPose(poser, 0);
        break;
    case 6:
        if (poser->mode == kPoseModeCrossFade)
            poser->mode = kPoseModeNormal;   // FinishCrossFadeBlend would free the live copy
        if (rng->Flip())
            poser->nextTime = poser->time = rng->Uniform(poser->endTime - 3.0f, poser->endTime);
        poser->timeScale = rng->Uniform(0.0f, 3.0f);
        Poser_AdvanceTime(poser, 0);
        break;
    case 7: {
        poser->mirrored = rng->Flip();
        Poser_Rotate(poser, 0, rng->Uniform(-1.0f, 1.0f));
        Poser_RotateX(poser, 0, rng->Uniform(-1.0f, 1.0f));
        float origin[3] = {rng->Uniform(-50.0f, 50.0f), rng->Uniform(-5.0f, 5.0f), rng->Uniform(-50.0f, 50.0f)};
        if (rng->Flip())
            Poser_SetAnimationOrigin(poser, 0, origin);
        else
            Poser_ChangeAnimationOrigin(poser, 0, origin);
        float back[3];
        Poser_GetAnimationOrigin(poser, 0, back);
        LogBytes("  origin", back, sizeof(back));
        alignas(16) MATRIX4 out;
        Poser_GetInitialTbOu(poser, 0, &out);
        LogBytes("  tbou", &out, sizeof(out));
        if (rng->Flip()) {
            RandomMatrix(rng, &placement, 2.0f);
            Poser_SetInitialTbOu(poser, 0, &placement);
        }
        Poser_DoSkeletonPose(poser, 0);
        Poser_Skin(poser, 0);
        Poser_GetRootBonePosOri(poser, 0, &out);
        LogBytes("  root", &out, sizeof(out));
        for (int which = 0; which < 2; which++) {
            if (poser->skeleton->weaponBones[which] < 0)
                continue;
            Poser_GetWeaponBonePosOri(poser, 0, which, &out);
            LogBytes("  weapon bone", &out, sizeof(out));
        }
        break;
    }
    case 8: {
        poser->crossFade = NULL;
        poser->mode = kPoseModeNormal;
        Poser_SetupCrossFadeBlend(poser, 0, rng->Uniform(0.1f, 1.0f), rng->Flip());
        Poser_SetCrossFadeBlendAnimation(poser, 0, poser->animation, &placement);
        for (int step = 0; step < 40 && poser->crossFade != NULL; step++) {
            Poser_DoSkeletonPose(poser, 0);
            float times[5];
            Poser_CalcCrossFadeTimes(poser, 0, &times[0], &times[1], &times[2], &times[3], &times[4]);
            LogBytes("  times", times, sizeof(times));
            LogBytes("  locals", poser->skeleton->matrices, BoneCount(poser) * sizeof(MATRIX4));
            Poser_AdvanceTime(poser, 0);
            LogPoserWithoutFade(poser);
        }
        if (poser->crossFade != NULL) {
            CrossFade_Destruct(poser->crossFade, 0);
            UMemory::FastFree(poser->crossFade, sizeof(ActCrossFadeBlendData));
            poser->crossFade = NULL;
        }
        break;
    }
    }
}

// The channels' decoded keys forgotten, so each pass decodes from the same state: a delta channel's values depend
// (by an ulp) on whether it stepped to a key from the one before or decoded it afresh.
void ForgetDecodedKeys(FnAnim *channel) {
    if (channel == NULL || channel->type != kCompound)
        return;
    FnCompoundChannel *compound = static_cast<FnCompoundChannel *>(channel);
    if (compound->channels == NULL)
        return;
    const CompoundData *data = reinterpret_cast<const CompoundData *>(compound->anim);
    for (int i = 0; i < data->count; i++) {
        FnAnim *sub = compound->channels[i];
        switch (sub->type) {
        case kDeltaF1:
        case kDeltaF3:
            static_cast<FnDeltaF *>(sub)->key = -1;
            static_cast<FnDeltaF *>(sub)->nextKey = -1;
            break;
        case kDeltaLerp:
        case kDeltaQuat:
            static_cast<FnDeltaChan *>(sub)->frame = -1;
            break;
        case kKeyLerp:
        case kKeyQuat:
            static_cast<FnKeyDeltaChan *>(sub)->key = -1;
            break;
        }
    }
}

void ForgetPoserKeys(ActPoser *poser) {
    ForgetDecodedKeys(poser->animation->anim);
    if (poser->crossFade != NULL && poser->crossFade->animation != NULL)
        ForgetDecodedKeys(poser->crossFade->animation->anim);
}

void TestLivePosers(Rng *rng) {
    for (size_t i = 0; i < g_posers.size(); i++) {
        ActPoser *poser = g_posers[i];
        for (int kind = 0; kind < 9; kind++) {
            for (int repeat = 0; repeat < 3; repeat++) {
                Case();
                Saved saved;
                AddPoserRegions(poser, &saved);
                ForgetPoserKeys(poser);
                LivePoserCase c = {poser, rng, kind};
                Logf("poser %u kind %d", unsigned(i), kind);
                if (!SafeCall(RunLivePoserCase, &c))
                    Logf("  fault");
                if (kind == 8) {
                    // The poser's own bytes but its cross-fade pointer (the copy was a fresh allocation)
                    LogPoserWithoutFade(poser);
                    for (size_t r = 1; r < saved.regions.size(); r++) {
                        char label[32];
                        snprintf(label, sizeof(label), "  region %u", unsigned(r));
                        LogBytes(label, saved.regions[r].first, saved.regions[r].second.size());
                    }
                } else {
                    LogRegions(saved, "  region");
                }
                saved.Restore();
                ForgetPoserKeys(poser);
            }
        }
    }
}

// ---- 2. posers of our own over fake channels

int g_eventsToken;

struct FakeChannel {
    const void *const *vtable;
    uint32_t stat;
    uint32_t type;
    int id;
};

void __fastcall FakeEval(FakeChannel *self, int, float previous, float time, float *out) {
    Logf("  eval ch%d %08x %08x out %d", self->id, Bits(previous), Bits(time), out != NULL);
}

bool __fastcall FakeEvalEvent(FakeChannel *self, int, float previous, float time, void *handlers, void *data) {
    Logf("  event ch%d %08x %08x handlers %d data %d", self->id, Bits(previous), Bits(time),
         handlers == &g_eventsToken, data != NULL);
    return true;
}

void __fastcall FakeUnexpected(FakeChannel *self, int) {
    Logf("  unexpected call on ch%d", self->id);
}

const void *g_fakeVtable[18];

void InitFakeVtable() {
    for (int i = 0; i < 18; i++)
        g_fakeVtable[i] = reinterpret_cast<const void *>(&FakeUnexpected);
    g_fakeVtable[kSlotEval] = reinterpret_cast<const void *>(&FakeEval);
    g_fakeVtable[kSlotEvalEvent] = reinterpret_cast<const void *>(&FakeEvalEvent);
}

struct FakePoserCase {
    Rng *rng;
    int kind;
};

void FillFakePoser(Rng *rng, ActPoser *poser) {
    memset(poser, 0, sizeof(*poser));
    poser->framesPerSecond = 20.0f;
    poser->secondsPerFrame = 0.05f;
    poser->frameTime = rng->Flip() ? 1.0f / 60.0f : rng->Uniform(0.0f, 0.2f);
    poser->timeScale = rng->Flip() ? 1.0f : rng->Uniform(-0.5f, 3.0f);
    poser->length = rng->Uniform(-2.0f, 60.0f);
    poser->endTime = rng->Flip() ? poser->length : rng->Uniform(-2.0f, 60.0f);
    poser->time = rng->Uniform(-2.0f, 70.0f);
    poser->previousTime = rng->Uniform(-2.0f, 70.0f);
    poser->nextTime = rng->Uniform(-2.0f, 70.0f);
    int special = rng->Range(0, 5);
    if (special == 0)
        poser->time = poser->previousTime = 0.0f;
    else if (special == 1)
        poser->previousTime = poser->time;
    poser->manualEventPrevious = rng->Uniform(0.0f, 30.0f);
    poser->manualEventTime = rng->Uniform(0.0f, 30.0f);
    poser->events = reinterpret_cast<ActEvents *>(&g_eventsToken);
}

void RunFakePoserCase(void *context) {
    FakePoserCase *c = static_cast<FakePoserCase *>(context);
    Rng *rng = c->rng;
    FakeChannel channels[4] = {{g_fakeVtable, 0, 0, 0}, {g_fakeVtable, 0, 0, 1}, {g_fakeVtable, 0, 0, 2},
                               {g_fakeVtable, 0, 0, 3}};
    static uint8_t data;
    ActAnimGroup animation, fadeAnimation;
    memset(&animation, 0, sizeof(animation));
    memset(&fadeAnimation, 0, sizeof(fadeAnimation));
    animation.length = rng->Uniform(0.0f, 60.0f);
    animation.anim = reinterpret_cast<FnAnim *>(&channels[0]);
    animation.animData = &data;
    animation.second = reinterpret_cast<FnAnim *>(&channels[1]);
    animation.secondData = rng->Range(0, 3) != 0 ? &data : NULL;
    fadeAnimation.anim = reinterpret_cast<FnAnim *>(&channels[2]);
    fadeAnimation.animData = &data;
    fadeAnimation.second = reinterpret_cast<FnAnim *>(&channels[3]);
    fadeAnimation.secondData = rng->Range(0, 3) != 0 ? &data : NULL;

    ActPoser poser;
    FillFakePoser(rng, &poser);
    poser.animation = &animation;
    poser.mode = rng->Range(0, 4);

    // A cross-fade record: on the stack for the times, from the pools (as the poser frees it) for the finish
    ActCrossFadeBlendData stackFade;
    memset(&stackFade, 0, sizeof(stackFade));
    stackFade.frame = rng->Uniform(-2.0f, 40.0f);
    stackFade.endFrame = rng->Uniform(-2.0f, 40.0f);
    stackFade.step = rng->Uniform(0.0f, 1.0f);
    stackFade.length = rng->Uniform(0.0f, 30.0f);
    stackFade.unknown120 = rng->Range(0, 3) == 0;
    stackFade.animation = &fadeAnimation;

    switch (c->kind) {
    case 0:
        poser.crossFade = &stackFade;
        Poser_DoEventPose(&poser, 0);
        break;
    case 1: {
        poser.crossFade = &stackFade;
        float times[5] = {12345.0f, 12345.0f, 12345.0f, 12345.0f, 12345.0f};
        Poser_CalcCrossFadeTimes(&poser, 0, &times[0], &times[1], &times[2], &times[3], &times[4]);
        LogBytes("  times", times, sizeof(times));
        break;
    }
    case 2:
    case 3: {
        ActCrossFadeBlendData *fade =
            static_cast<ActCrossFadeBlendData *>(UMemory::FastAlloc(sizeof(ActCrossFadeBlendData), "test"));
        memcpy(fade, &stackFade, sizeof(stackFade));
        fade->animation = NULL;
        fade->matrices.count = 1;
        fade->matrices.buffer = static_cast<MATRIX4 *>(OperatorNewArray(sizeof(MATRIX4)));
        fade->matrices.current = fade->matrices.buffer;
        poser.crossFade = fade;
        if (c->kind == 2) {
            Poser_FinishCrossFadeBlend(&poser, 0);
        } else {
            poser.mode = rng->Range(0, 3) == 0 ? rng->Range(0, 4) : int(kPoseModeCrossFade);
            Poser_AdvanceTime(&poser, 0);
        }
        Logf("  fade freed %d", poser.crossFade == NULL);
        if (poser.crossFade != NULL) {
            OperatorDelete(fade->matrices.buffer);
            UMemory::FastFree(fade, sizeof(ActCrossFadeBlendData));
            poser.crossFade = NULL;
        }
        break;
    }
    }
    poser.crossFade = NULL;
    poser.animation = NULL;
    poser.events = NULL;
    LogBytes("  poser", &poser, sizeof(poser));
}

void TestFakePosers(Rng *rng) {
    for (int kind = 0; kind < 4; kind++) {
        for (int i = 0; i < 300; i++) {
            Case();
            Logf("fake poser kind %d", kind);
            FakePoserCase c = {rng, kind};
            if (!SafeCall(RunFakePoserCase, &c))
                Logf("  fault");
        }
    }
}

struct ConstructCase {
    ActSkeleton *skeleton;
    ActEvents *events;
    bool mirrored;
    bool hasMatrixCallback;
};

void RunConstructCase(void *context) {
    ConstructCase *c = static_cast<ConstructCase *>(context);
    ActPoser *poser = static_cast<ActPoser *>(UMemory::FastAlloc(sizeof(ActPoser), "ActPoser"));
    Poser_Construct(poser, 0, c->skeleton, c->events, c->mirrored, c->hasMatrixCallback);
    Logf("  flags %d %d skeleton %d events %d mode %d crossFade %d suppress %d", poser->hasMatrixCallback,
         poser->mirrored, poser->skeleton == c->skeleton, poser->events == c->events, poser->mode,
         poser->crossFade != NULL, poser->suppressTranslation);
    LogBytes("  times", &poser->time, 5 * sizeof(float));
    LogBytes("  rates", &poser->framesPerSecond, 4 * sizeof(float));
    LogBytes("  offset", poser->offset, sizeof(Coord4));
    LogBytes("  scale", poser->scale, sizeof(Coord4));
    ActPoseMatrices *matrices = poser->matrices;
    Logf("  matrices %d current-is-buffer %d", matrices->count, matrices->current == matrices->buffer);
    ActIKSolverArray *solvers = poser->ikSolvers;
    Logf("  solvers %d ref %d globals %d locals %d skeleton %d", solvers->count, solvers->referenceBone,
         solvers->globals == reinterpret_cast<Transform *>(matrices->current),
         solvers->locals == reinterpret_cast<Transform *>(c->skeleton->matrices),
         solvers->skeleton == c->skeleton->skeleton);
    ActGlobalPoseOverrideArray *overrides = poser->poseOverrides;
    Logf("  overrides %d globals %d", overrides->count,
         overrides->globals == reinterpret_cast<Transform *>(matrices->current));
    Poser_Destruct(poser, 0);
    Logf("  destructed crossFade %d", poser->crossFade != NULL);
    UMemory::FastFree(poser, sizeof(ActPoser));
}

void TestConstruct(Rng *rng) {
    std::vector<ActSkeleton *> skeletons;
    for (size_t i = 0; i < g_posers.size(); i++) {
        bool seen = false;
        for (size_t j = 0; j < skeletons.size(); j++)
            seen = seen || skeletons[j] == g_posers[i]->skeleton;
        if (!seen)
            skeletons.push_back(g_posers[i]->skeleton);
    }
    for (size_t i = 0; i < skeletons.size(); i++) {
        for (int flags = 0; flags < 4; flags++) {
            Case();
            Logf("construct skeleton %u flags %d", unsigned(i), flags);
            ConstructCase c = {skeletons[i], g_posers[0]->events, (flags & 1) != 0, (flags & 2) != 0};
            if (!SafeCall(RunConstructCase, &c))
                Logf("  fault");
        }
    }
    (void)rng;
}

// ---- 3. weapons

struct WeaponCase {
    Rng *rng;
    int kind;
};

float SpecialFloat(Rng *rng) {
    static const uint32_t specials[] = {0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000,
                                        0x7fa00000, 0x00000001, 0x80000001, 0x3f800000, 0xbf800000, 0x3f800001,
                                        0x3f7ffffe, 0x3f7fffff, 0xbf800001, 0xbf7ffffe};
    if (rng->Range(0, 3) == 0) {
        float f;
        memcpy(&f, &specials[rng->Range(0, int(sizeof(specials) / sizeof(specials[0])) - 1)], sizeof(f));
        return f;
    }
    return rng->Uniform(-100.0f, 100.0f);
}

void RunWeaponCase(void *context) {
    WeaponCase *c = static_cast<WeaponCase *>(context);
    Rng *rng = c->rng;
    alignas(16) uint8_t storage[sizeof(ActWeapon)];
    memset(storage, 0, sizeof(storage));
    ActWeapon *weapon = reinterpret_cast<ActWeapon *>(storage);
    alignas(16) MATRIX4 first, second;
    RandomMatrix(rng, &first, 2.0f);
    RandomMatrix(rng, &second, 2.0f);
    if (rng->Flip()) {
        first.mtx[3][3] = 1.0f;
        second.mtx[3][3] = 1.0f;
        first.mtx[0][3] = first.mtx[1][3] = first.mtx[2][3] = 0.0f;
        second.mtx[0][3] = second.mtx[1][3] = second.mtx[2][3] = 0.0f;
    }
    int which = rng->Range(0, 3);
    weapon->worldTransforms[0] = (which & 1) ? &first : NULL;
    weapon->worldTransforms[1] = (which & 2) ? &second : NULL;

    switch (c->kind) {
    case 0: {
        float x = SpecialFloat(rng);
        Logf("  abs %08x -> %08x", Bits(x), Bits(Weapon_FloatAbs(x)));
        break;
    }
    case 1:
        weapon->muzzleFlashEndTick = ShadowStepCount + rng->Range(-10, 10);
        Logf("  strength %016llx", Bits(Weapon_CurrentMuzzleFlashStrength(weapon, 0)));
        break;
    case 2: {
        alignas(16) MATRIX4 matrix;
        RandomMatrix(rng, &matrix, 3.0f);
        if (rng->Flip())
            matrix.mtx[3][3] = SpecialFloat(rng);
        alignas(16) MATRIX4 copy = matrix;
        Weapon_TransformToWorldSpace(weapon, 0, &matrix);
        LogBytes("  matrix", &matrix, sizeof(matrix));
        Weapon_TransformMatrixToWorldSpace(weapon, 0, &copy);
        LogBytes("  ortho", &copy, sizeof(copy));
        break;
    }
    case 3: {
        alignas(16) Coord4 point = {rng->Uniform(-50.0f, 50.0f), rng->Uniform(-50.0f, 50.0f),
                                    rng->Uniform(-50.0f, 50.0f), rng->Flip() ? 1.0f : SpecialFloat(rng)};
        Weapon_TransformPointToWorldSpace(weapon, 0, &point);
        LogBytes("  point", &point, sizeof(point));
        Logf("  velocity at %d", int(reinterpret_cast<uint8_t *>(Weapon_GetVelocity(weapon, 0)) - storage));
        break;
    }
    case 4: {
        weapon->worldTransforms[0] = NULL;
        weapon->worldTransforms[1] = NULL;
        Weapon_SetTransformToWorldSpace(weapon, 0, &first, &second);
        LogBytes("  first", weapon->worldTransforms[0], sizeof(MATRIX4));
        LogBytes("  second", weapon->worldTransforms[1], sizeof(MATRIX4));
        RandomMatrix(rng, &first, 1.0f);
        MATRIX4 *kept = weapon->worldTransforms[0];
        Weapon_SetTransformToWorldSpace(weapon, 0, &second, &first);
        Logf("  kept %d", weapon->worldTransforms[0] == kept);
        LogBytes("  first", weapon->worldTransforms[0], sizeof(MATRIX4));
        LogBytes("  second", weapon->worldTransforms[1], sizeof(MATRIX4));
        OperatorDelete(weapon->worldTransforms[0]);
        OperatorDelete(weapon->worldTransforms[1]);
        weapon->worldTransforms[0] = NULL;
        weapon->worldTransforms[1] = NULL;
        break;
    }
    case 5: {
        PhysicsObject *player = *ShadowPlayerPhysics;
        PhysicsObject *other = NULL;
        for (size_t i = 0; i < g_weapons.size() && other == NULL; i++)
            other = g_weapons[i]->owner;
        weapon->flags = rng->Next();
        PhysicsObject *owner = rng->Flip() || other == NULL ? player : other;
        Weapon_SetOwner(weapon, 0, owner);
        Logf("  owner %d flags %08x", owner == player, weapon->flags);
        break;
    }
    }
    weapon->worldTransforms[0] = NULL;
    weapon->worldTransforms[1] = NULL;
    weapon->owner = NULL;
    LogBytes("  weapon", storage, sizeof(storage));
}

struct LiveWeaponCase {
    ActWeapon *weapon;
    Rng *rng;
    int kind;
};

void RunLiveWeaponCase(void *context) {
    LiveWeaponCase *c = static_cast<LiveWeaponCase *>(context);
    ActWeapon *weapon = c->weapon;
    Rng *rng = c->rng;
    if (c->kind == 0) {
        for (int i = 0; i < ActWeapon::kFlashCount; i++)
            weapon->flashes[i].endTick = ShadowStepCount + rng->Range(-3, rng->Range(0, 3) == 0 ? 8 : 1);
        const Coord3 *player = (*ShadowPlayerPhysics)->GetPosition();
        Coord3 at = {player->x + rng->Uniform(-300.0f, 300.0f), player->y + rng->Uniform(-20.0f, 20.0f),
                     player->z + rng->Uniform(-300.0f, 300.0f)};
        Weapon_StartMuzzleFlash(weapon, 0, &at);
    } else {
        Weapon_SetEventDynamicData(weapon, 0);
    }
}

void TestWeapons(Rng *rng) {
    for (int kind = 0; kind < 6; kind++) {
        for (int i = 0; i < 400; i++) {
            Case();
            Logf("weapon kind %d", kind);
            WeaponCase c = {rng, kind};
            if (!SafeCall(RunWeaponCase, &c))
                Logf("  fault");
        }
    }
    for (size_t w = 0; w < g_weapons.size(); w++) {
        for (int kind = 0; kind < 2; kind++) {
            for (int i = 0; i < 20; i++) {
                Case();
                Logf("live weapon %u kind %d", unsigned(w), kind);
                Saved saved;
                saved.Add(g_weapons[w], sizeof(ActWeapon));
                saved.Add(&ShadowRandomSeed, sizeof(uint32_t));
                saved.Add(ShadowEventBlock, kEventBlockSize);
                LiveWeaponCase c = {g_weapons[w], rng, kind};
                if (!SafeCall(RunLiveWeaponCase, &c))
                    Logf("  fault");
                LogRegions(saved, "  region");
                saved.Restore();
            }
        }
    }
}

// ---- 4. URefCounter<WeaponInfo>'s tree

void LogTree(const URefCounterMap *map) {
    std::vector<const RefCounterNode *> order;
    for (const RefCounterNode *node = map->head->left; node != map->head;) {
        order.push_back(node);
        if (!node->right->isNil) {
            node = node->right;
            while (!node->left->isNil)
                node = node->left;
        } else {
            const RefCounterNode *parent = node->parent;
            while (!parent->isNil && node == parent->right) {
                node = parent;
                parent = parent->parent;
            }
            node = parent;
        }
    }
    std::string s;
    for (size_t i = 0; i < order.size(); i++) {
        const RefCounterNode *node = order[i];
        int parent = -1, left = -1, right = -1;
        for (size_t j = 0; j < order.size(); j++) {
            if (order[j] == node->parent)
                parent = int(j);
            if (order[j] == node->left)
                left = int(j);
            if (order[j] == node->right)
                right = int(j);
        }
        char text[200];
        snprintf(text, sizeof(text), " %s:%d:%u:%d:%d:%d:%d", node->value.name, node->value.entry.references,
                 unsigned(uintptr_t(node->value.entry.object)), node->color, parent, left, right);
        s += text;
    }
    int first = -1, last = -1, root = -1;
    for (size_t j = 0; j < order.size(); j++) {
        if (order[j] == map->head->left)
            first = int(j);
        if (order[j] == map->head->right)
            last = int(j);
        if (order[j] == map->head->parent)
            root = int(j);
    }
    Logf("  tree size %u root %d first %d last %d:%s", map->size, root, first, last, s.c_str());
}

void RunTreeCase(void *context) {
    Rng *rng = static_cast<Rng *>(context);
    URefCounterMap map;
    memset(&map, 0, sizeof(map));
    map.head = RefCounterMapBuyHead();
    map.head->isNil = 1;
    map.head->parent = map.head;
    map.head->left = map.head;
    map.head->right = map.head;
    int steps = rng->Range(5, 60);
    for (int step = 0; step < steps; step++) {
        int op = rng->Range(0, 9);
        if (op <= 5 || map.size == 0) {
            RefCounterValue value;
            memset(&value, 0, sizeof(value));
            snprintf(value.name, sizeof(value.name), "%c%c%d", 'a' + rng->Range(0, 3), 'A' + rng->Range(0, 3),
                     rng->Range(0, 20));
            value.entry.references = rng->Range(1, 3);
            value.entry.object = reinterpret_cast<void *>(uintptr_t(rng->Range(1, 6)));
            RefCounterInsertResult result = {NULL, false};
            WeaponTree_InsertUnique(&map, 0, &result, &value);
            Logf("  insert %s: %s %d", value.name, result.node->value.name, result.inserted);
        } else if (op <= 7) {
            int index = rng->Range(0, int(map.size) - 1);
            RefCounterNode *node = map.head->left;
            for (int i = 0; i < index; i++) {
                RefCounterNode *next = node;
                if (!next->right->isNil) {
                    next = next->right;
                    while (!next->left->isNil)
                        next = next->left;
                } else {
                    RefCounterNode *parent = next->parent;
                    while (!parent->isNil && next == parent->right) {
                        next = parent;
                        parent = parent->parent;
                    }
                    next = parent;
                }
                node = next;
            }
            RefCounterNode *after = NULL;
            WeaponTree_EraseAt(&map, 0, &after, node);
            Logf("  erase %d: next %s", index, after == map.head ? "end" : after->value.name);
        } else if (op == 8) {
            void *object = reinterpret_cast<void *>(uintptr_t(rng->Range(1, 7)));
            Logf("  remove %u: %d", unsigned(uintptr_t(object)), WeaponTree_RemoveReference(&map, 0, object));
        } else {
            RefCounterNode *after = NULL;
            RefCounterNode *first = rng->Flip() ? map.head->left : map.head->left->right->isNil
                                                                       ? map.head->left
                                                                       : map.head->left->right;
            WeaponTree_EraseRange(&map, 0, &after, first, map.head);
            Logf("  erase range: end %d", after == map.head);
        }
        LogTree(&map);
    }
    WeaponTree_Destruct(&map, 0);
    Logf("  destructed head %d size %u", map.head == NULL, map.size);
}

void TestTree(Rng *rng) {
    for (int i = 0; i < 60; i++) {
        Case();
        Logf("tree %d", i);
        if (!SafeCall(RunTreeCase, rng))
            Logf("  fault");
    }
}

// ---- the passes

const unsigned kRanges[][2] = {
    {0x00018550, 0x000197f0},
    {0x0001ab20, 0x0001bbd0},           // 0x0001bbd0 and 0x0001bef0 are core's (engine/URefCounter.cpp)
    {0x0001bcb0, 0x0001bef0},
    {0x0001bf80, 0x0001bfa0},
};

void SwapRanges(bool original) {
    for (size_t i = 0; i < sizeof(kRanges) / sizeof(kRanges[0]); i++)
        XbeOriginal_RestoreRange(kRanges[i][0], kRanges[i][1], original);
}

void RunPass(std::string *log, bool original) {
    g_log = log;
    if (original)
        SwapRanges(true);
    void (*const tests[])(Rng *) = {TestLivePosers, TestFakePosers, TestConstruct, TestWeapons, TestTree};
    uint32_t seed = 0x5e0f11u;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        Rng rng = {seed + uint32_t(i) * 7919u};
        tests[i](&rng);
    }
    if (original)
        SwapRanges(false);
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

void FindLiveActors() {
    g_posers.clear();
    g_weapons.clear();
    ShadowActorDatabase *database = ShadowActors;
    if (database == NULL || database->actors == NULL)
        return;
    for (ShadowActorNode *node = database->actors->next; node != database->actors; node = node->next) {
        ShadowActor *actor = node->actor;
        if (actor == NULL)
            continue;
        ActPoser *poser = actor->poser;
        if (poser != NULL && poser->animation != NULL && poser->animation->animData != NULL &&
            poser->mode >= kPoseModeNormal && poser->mode <= kPoseModeManual &&
            (poser->mode != kPoseModeCrossFade || poser->crossFade != NULL))
            g_posers.push_back(poser);
        if (actor->character != NULL) {
            for (int i = 0; i < 2; i++) {
                if (actor->character->weapons[i] != NULL)
                    g_weapons.push_back(actor->character->weapons[i]);
            }
        }
    }
}

}  // namespace

void PoserShadow_Run(void) {
    const char *setting = getenv("NIGHTFIRE_POSERSHADOW");
    if (setting == NULL || atoi(setting) == 0)
        return;
    InitFakeVtable();
    FindLiveActors();
    if (g_posers.empty()) {
        printf("[anim] poser shadow: no posed actors yet - skipped\n");
        fflush(stdout);
        return;
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
            printf("[anim]   line %u: original \"%.200s\" ours \"%.200s\"\n", unsigned(i),
                   x != NULL ? x->c_str() : "(none)", y != NULL ? y->c_str() : "(none)");
        differ++;
    }
    printf("[anim] posers (%u live), weapons (%u live), weapon tree: %d cases, %u checks, %d differ (%d faults)\n",
           unsigned(g_posers.size()), unsigned(g_weapons.size()), g_cases, unsigned(lines), differ, g_faults);
    fflush(stdout);
}
