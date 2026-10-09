#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "AnimEngineShadow.h"
#include "FpControl.h"

#include "../anim/AnimEngine.h"
#include "../anim/ProcAnim.h"
#include "../engine/UMemory.hpp"
#include "../render/RSceneObj.hpp"
#include "../world/Trigger.h"
#include "../world/World.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_ANIMENGINESHADOW=1, from the first simulation tick on the loaded track: the animation engine's ports
// (anim/AnimEngine.cpp, ProcAnim.cpp) against the originals (their entry bytes swapped back in for each original
// call, common/xbeOriginal.h), on identical inputs, compared.
//
//   - std::sort's helpers over random byte arrays (few or many distinct values, every length to 300, the heap path
//     forced by a small ideal), both lower_bounds, and the register-argument FUN_000776d0 and DeactivateSystem
//     through their adaptors.
//   - GetInstanceMatrix over every instance of the track as QuickDrawInstance calls it, then on perturbed copies of
//     real and synthetic instances: every proc-anim type, random parameters, states (some with a scene object, some
//     naming another type), offsets, transform and parameter lists, tick and SimStep values, sizes, the mirrored
//     flag; the matrix and the answer compared. SetProcAnimXFormList and SetProcAnimParamList's globals compared.
//   - The live handles of the track's scene objects: every query for every system id and instance.
//   - Handles made by the original's Create and by ours, from synthetic scenes (articles with random animation
//     lists, looping or not, frames with empty event lists or events whose callback is NULL, kept and dropped
//     effects, mirrored instances, states, an owner with a damage description and a recording SetEventDynamicData)
//     and from the track's animated instances; then the same random sequence on each pair: stimuli (one system,
//     all, by zones, every queueing mode), Update over advancing ticks, SetFrame, SetFrameRate, NumFrames, every
//     query, SetEffectBits, InitAllSystemStates, Stop, DeactivateSystem, and the instances' matrices. Each side has
//     its own active list, event data and copy of the scene's articles, swapped in for its call; after every step
//     the handles (their pointers made relative), the lists, the event data, the articles and the answers compared.
// The track's active list, event data, proc-anim lists, SimStep and tick length are put back at the end. Not
// covered: event callbacks that run (the track's animations with events are not stepped), which the lockstep runs
// test, as they do RSceneObj's use of the handles.
//
// One mutation this catches: EvaluateInstance negating row 3's y instead of x for a mirrored instance changes
// every mirrored animated instance's matrix; System::Update wrapping a loop at `>` instead of `>=` shows on the
// looping systems stepped one tick at a time.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[2][2] = {
    { 0x00076810, 0x00078430 }, { 0x0008a4f0, 0x0008b2b0 },
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

// ---- the originals

typedef Handle *(*CreateFn)(uint32_t, uint32_t, CARP::Instance *, ProcAnimState *, RSceneObj *);
typedef void (*DeleteFn)(Handle *, uint32_t);
typedef void (__fastcall *SetBitsFn)(Handle *, int, uint32_t, uint32_t);
typedef ArticleEffect *(__fastcall *FindEffectFn)(Handle *, int, uint32_t);
typedef void (__fastcall *HandleArgFn)(Handle *, int, uint32_t);
typedef bool (__fastcall *HandleBoolFn)(Handle *, int);
typedef uint32_t (__fastcall *HandleWordFn)(Handle *, int, uint32_t);
typedef bool (__fastcall *HandleBoolArgFn)(Handle *, int, uint32_t);
typedef int32_t (__fastcall *HandleIndexFn)(Handle *, int, uint32_t);
typedef CARP::Instance *(__fastcall *HandleInstanceFn)(Handle *, int, uint32_t);
typedef void (__fastcall *HandleFn)(Handle *, int);
typedef void (__fastcall *SetFrameFn)(Handle *, int, uint32_t, uint32_t, uint32_t);
typedef uint32_t (__fastcall *NumFramesFn)(Handle *, int, uint32_t, uint32_t);
typedef void (__fastcall *Stimuli4Fn)(Handle *, int, uint32_t, uint32_t, uint32_t, int);
typedef void (__fastcall *Stimuli3Fn)(Handle *, int, uint32_t, uint32_t, int);
typedef void (*UpdateFn)(uint32_t);
typedef bool (*MatrixFn)(CARP::Instance *, const Coord4 *, MATRIX4 *, ProcAnimState *);
typedef void (*XFormListFn)(MATRIX4 *, uint32_t);
typedef void (*ParamListFn)(float *, uint32_t);
typedef void (*SortFn)(uint8_t *, uint8_t *, int);
typedef void (*RangeFn)(uint8_t *, uint8_t *);
typedef void (*TagRangeFn)(uint8_t *, uint8_t *, int *, uint8_t *);
typedef void (*RotateFn)(uint8_t *, uint8_t *, uint8_t *, int *, uint8_t *);
typedef void (*HeapFn)(uint8_t *, int, int, uint8_t);
typedef void (*MedianFn)(uint8_t *, uint8_t *, uint8_t *);
typedef SystemIdSort::Range *(*PartitionFn)(SystemIdSort::Range *, uint8_t *, uint8_t *);
typedef uint8_t *(*LowerBoundFn)(uint8_t *, uint8_t *, const uint8_t *, int *);
typedef CARP::AnimInfo *(*InfoLowerBoundFn)(CARP::AnimInfo *, CARP::AnimInfo *, const CARP::AnimInfo *, int *);

#define Orig_Create ((CreateFn)0x00078210)
#define Orig_SetEffectBits ((SetBitsFn)0x00076830)
#define Orig_FindEffectByID ((FindEffectFn)0x00076850)
#define Orig_InitAllSystemStates ((HandleArgFn)0x00076890)
#define Orig_AnySystemPlaying ((HandleBoolFn)0x000768c0)
#define Orig_GetInstanceSystemID ((HandleWordFn)0x00076900)
#define Orig_IsSystemPlaying ((HandleBoolArgFn)0x000774a0)
#define Orig_GetSystemState ((HandleWordFn)0x00077500)
#define Orig_GetFirstSystemInstanceIndex ((HandleIndexFn)0x00077560)
#define Orig_GetBestSystemInstanceIndex ((HandleIndexFn)0x000775c0)
#define Orig_GetFirstSystemInstance ((HandleInstanceFn)0x000776a0)
#define Orig_Stop ((HandleFn)0x00077be0)
#define Orig_StopThunk ((HandleFn)0x00078200)
#define Orig_SetFrame ((SetFrameFn)0x00077c50)
#define Orig_SetFrameRate ((SetFrameFn)0x00077cb0)
#define Orig_NumFrames ((NumFramesFn)0x00077d10)
#define Orig_ProcessStimuli4 ((Stimuli4Fn)0x00077d70)
#define Orig_ProcessStimuli3 ((Stimuli3Fn)0x00077e00)
#define Orig_ProcessStimuliZones ((Stimuli4Fn)0x00077e60)
#define Orig_Update ((UpdateFn)0x00078190)
#define Orig_GetInstanceMatrix ((MatrixFn)0x0008a4f0)
#define Orig_SetProcAnimXFormList ((XFormListFn)0x0008a520)
#define Orig_SetProcAnimParamList ((ParamListFn)0x0008a550)
#define Orig_PushHeap ((HeapFn)0x00076930)
#define Orig_Rotate ((RotateFn)0x00076980)
#define Orig_AdjustHeap ((HeapFn)0x00076bc0)
#define Orig_LowerBound ((LowerBoundFn)0x00076c20)
#define Orig_AnimInfoLowerBound ((InfoLowerBoundFn)0x00076c60)
#define Orig_Median ((MedianFn)0x00076cd0)
#define Orig_MakeHeap ((TagRangeFn)0x00076e00)
#define Orig_UnguardedPartition ((PartitionFn)0x00076e40)
#define Orig_InsertionSort ((RangeFn)0x00076f70)
#define Orig_SortHeap ((RangeFn)0x00077b20)
#define Orig_Sort ((SortFn)0x000780d0)
const uint32_t kDeactivateSystem = 0x00077b60;
const uint32_t kFindAnimInfo = 0x000776d0;

// Calls a register-argument function at `address` (the original inside the window, our adaptor outside it)
__declspec(naked) uint32_t CallDeactivateSystem(uint32_t address, uint32_t index, uint32_t frame, uint32_t next) {
    __asm {
        push edi
        mov edi, dword ptr [esp + 0xc]
        mov ecx, dword ptr [esp + 0x10]
        push dword ptr [esp + 0x14]
        call dword ptr [esp + 0xc]
        add esp, 4
        pop edi
        ret
    }
}

__declspec(naked) CARP::AnimInfo *CallFindAnimInfo(uint32_t address, const WorldArticle *article, uint32_t count,
                                                   uint32_t state, uint32_t stimulus) {
    __asm {
        mov eax, dword ptr [esp + 0xc]
        mov edx, dword ptr [esp + 8]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        call dword ptr [esp + 0xc]
        add esp, 8
        ret
    }
}

// ---- the game's state the tests write and restore

#define ShadowActiveList ((uint8_t *)0x001eb878)      // the active systems, their count, Update's last frame
const size_t kActiveListBytes = 0x408;
#define ShadowActiveCount U32_AT(0x001ebc78)
#define ShadowProcAnimLists ((uint8_t *)0x001c41b0)   // the parameter and transform lists and their counts
const size_t kProcAnimListBytes = 0x10;
#define ShadowSimStep FLOAT_AT(0x001c465c)
#define ShadowTickSeconds FLOAT_AT(0x001f2a48)
#define ShadowRenderer PTR_AT(0x001ebff4)
#define ShadowEventCallbacks ((void **)0x0018bf38)    // RegisterEvent's
#define ShadowDrawOffset ((const Coord4 *)0x001d4c00) // QuickDrawInstance's offset

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[animengine]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
    Differ(what, index, detail);
}

void CheckVector(const char *what, int index, const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    if (a.size() != b.size()) {
        g_checks++;
        char detail[64];
        snprintf(detail, sizeof(detail), "size: original %u, port %u", unsigned(a.size()), unsigned(b.size()));
        Differ(what, index, detail);
        return;
    }
    if (!a.empty())
        CheckBytes(what, index, a.data(), b.data(), a.size());
}

void CheckWord(const char *what, int index, uint32_t a, uint32_t b) {
    g_checks++;
    if (a == b)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "original %08x, port %08x", a, b);
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

// The original (inside the window), then the port, each between `before` and `after` (its side's state swapped
// in and out); false if either faulted
template <class F>
bool Both(F &&run, void (*before)(int side) = NULL, void (*after)(int side) = NULL) {
    typedef typename std::remove_reference<F>::type Run;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Run *>(context))(original); };
    bool ok;
    if (before != NULL)
        before(0);
    {
        OriginalWindow window;
        ok = Guarded(thunk, &run, true);
    }
    if (after != NULL)
        after(0);
    if (before != NULL)
        before(1);
    ok = Guarded(thunk, &run, false) && ok;
    if (after != NULL)
        after(1);
    return ok;
}

// ---- inputs

uint32_t g_random = 0x5eed1e55;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }

void RandomMatrix(MATRIX4 *m) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            m->mtx[r][c] = Uniform(-2.0f, 2.0f);
}

void RandomQuaternion(Coord4 *q) {
    Coord4 v = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
    if (length < 0.01f)
        v = { 0.0f, 0.0f, 0.0f, 1.0f }, length = 1.0f;
    *q = { v.x / length, v.y / length, v.z / length, v.w / length };
}

// ---------------------------------------------------------------------------------------------------------------
// std::sort's helpers

void TestSort() {
    for (int i = 0; i < 4000; i++) {
        int n = RandomInt(4) == 0 ? RandomInt(8) : RandomInt(301);
        int spread = RandomInt(3) == 0 ? 1 + RandomInt(5) : 256;
        uint8_t data[2][320];
        for (int k = 0; k < 320; k++)
            data[0][k] = uint8_t(RandomInt(spread));
        memcpy(data[1], data[0], sizeof(data[0]));
        uint32_t result[2][2] = {};
        int op = RandomInt(10);
        if (op == 9) {
            std::sort(data[0], data[0] + n);
            memcpy(data[1], data[0], sizeof(data[0]));
        }
        int ideal = RandomInt(3) == 0 ? RandomInt(4) : n;
        int hole = RandomInt(n), top = RandomInt(hole + 1), bottom = hole + 1 + RandomInt(n - hole);
        int mid = RandomInt(n + 1);
        uint8_t value = uint8_t(RandomInt(spread));
        bool ok = Both([&](bool original) {
            int k = original ? 0 : 1;
            uint8_t *first = data[k], *last = data[k] + n;
            switch (op) {
            case 0:
                original ? Orig_Sort(first, last, ideal) : SystemIdSort::Sort(first, last, ideal);
                break;
            case 1:
                original ? Orig_InsertionSort(first, last) : SystemIdSort::InsertionSort(first, last);
                break;
            case 2:
                original ? Orig_MakeHeap(first, last, NULL, NULL) : SystemIdSort::MakeHeap(first, last, NULL, NULL);
                break;
            case 3:
                original ? Orig_SortHeap(first, last) : SystemIdSort::SortHeap(first, last);
                break;
            case 4:
                if (n > 0)
                    original ? Orig_PushHeap(first, hole, top, value) : SystemIdSort::PushHeap(first, hole, top, value);
                break;
            case 5:
                if (n > 0)
                    original ? Orig_AdjustHeap(first, hole, bottom, value)
                             : SystemIdSort::AdjustHeap(first, hole, bottom, value);
                break;
            case 6:
                if (n >= 3)
                    original ? Orig_Median(first, first + n / 2, last - 1)
                             : SystemIdSort::Median(first, first + n / 2, last - 1);
                break;
            case 7:
                if (n >= 2) {
                    SystemIdSort::Range range = {};
                    SystemIdSort::Range *answer = original ? Orig_UnguardedPartition(&range, first, last)
                                                           : SystemIdSort::UnguardedPartition(&range, first, last);
                    result[k][0] = uint32_t(range.first - first) | (answer == &range ? 0 : 0x80000000u);
                    result[k][1] = uint32_t(range.second - first);
                }
                break;
            case 8:
                original ? Orig_Rotate(first, first + mid, last, NULL, NULL)
                         : SystemIdSort::Rotate(first, first + mid, last, NULL, NULL);
                break;
            case 9:
                result[k][0] = uint32_t((original ? Orig_LowerBound(first, last, &value, NULL)
                                                  : SystemIdSort::LowerBound(first, last, &value, NULL)) - first);
                break;
            }
        });
        if (!ok)
            continue;
        CheckBytes("sort helper bytes", op, data[0], data[1], sizeof(data[0]));
        CheckWord("sort helper answer", op, result[0][0], result[1][0]);
        CheckWord("sort helper answer 2", op, result[0][1], result[1][1]);
        g_cases++;
    }

    // lower_bound over animations
    for (int i = 0; i < 1000; i++) {
        CARP::AnimInfo infos[40] = {};
        int n = RandomInt(41);
        for (int k = 0; k < n; k++) {
            infos[k].stimulus = uint8_t(RandomInt(6));
            infos[k].state = uint8_t(RandomInt(6));
        }
        std::sort(infos, infos + n, [](const CARP::AnimInfo &a, const CARP::AnimInfo &b) {
            return a.stimulus != b.stimulus ? a.stimulus < b.stimulus : a.state < b.state;
        });
        CARP::AnimInfo key = {};
        key.stimulus = uint8_t(RandomInt(7));
        key.state = uint8_t(RandomInt(7));
        uint32_t found[2] = {};
        if (!Both([&](bool original) {
                found[original ? 0 : 1] = uint32_t((original ? Orig_AnimInfoLowerBound(infos, infos + n, &key, NULL)
                                                             : AnimInfoLowerBound(infos, infos + n, &key, NULL)) - infos);
            }))
            continue;
        CheckWord("AnimInfoLowerBound", i, found[0], found[1]);
        g_cases++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The synthetic scene: articles with animations, frames and effects; instances of them; their states; an owner

const int kArticles = 6;
const int kInfos = 24;         // the most animations an article has, its terminator among them
const int kKeys = 24;
const int kEffects = 6;
const int kStates = 48;
const int kSystemIdChoices = 6;
const uint8_t kSystemIds[kSystemIdChoices] = { 0, 3, 7, 9, 0x80, 0xff };

struct Scene {
    WorldArticle articles[kArticles];
    CARP::AnimInfo infos[kArticles][kInfos];
    ArticleEffect effects[kArticles][kEffects + 1];
};

struct EventBlock {
    TriggerEvents header;
    TriggerEvent events[2];
};

Scene g_scene;
CARP::AnimKey g_keys[kArticles][kInfos][kKeys];
alignas(16) EventBlock g_emptyEvents;
alignas(16) EventBlock g_nullEvents;    // two events whose callbacks are NULL, if RegisterEvent has such a number
bool g_haveNullEvents = false;
alignas(16) uint8_t g_states[kStates][sizeof(ProcAnimState)];
CARP::BaseDesc g_damageDesc;

// An owner: RSceneObj's vtable (slot 11 SetEventDynamicData, recorded) and damage description
int g_sceneObjCalls = 0;
void __fastcall FakeSetEventDynamicData(void *, int) { g_sceneObjCalls++; }
void __fastcall FakeOtherSlot(void *, int) { g_sceneObjCalls += 1000; }
void *g_fakeVtable[19];
struct FakeSceneObj {
    void **vtable;
    uint8_t unknown04[0xc];
    CARP::BaseDesc *baseDesc;            // +0x10
    uint8_t unknown14[0x2c];
};
static_assert(sizeof(FakeSceneObj) == 0x40, "a scene object is 64 bytes");
FakeSceneObj g_owner;

void FindNullEvent() {
    for (int type = 0; type < 0x40; type++) {
        if (ShadowEventCallbacks[type] == NULL) {
            g_nullEvents.header.count = 2;
            for (TriggerEvent &event : g_nullEvents.events)
                event = { type, 0, 0, 0 };
            g_haveNullEvents = true;
            return;
        }
    }
}

void MakeScene() {
    memset(&g_scene, 0, sizeof(g_scene));
    for (int a = 0; a < kArticles; a++) {
        WorldArticle &article = g_scene.articles[a];
        // the animations: distinct (stimulus, state) pairs, sorted
        int count = 1 + RandomInt(kInfos - 1);
        int made = 0;
        for (int stimulus = 0; stimulus < 6 && made < count; stimulus++)
            for (int state = 0; state < 5 && made < count; state++)
                if (RandomInt(2) == 0) {
                    CARP::AnimInfo &info = g_scene.infos[a][made++];
                    info.stimulus = uint8_t(stimulus);
                    info.state = uint8_t(state);
                }
        for (int k = 0; k < made; k++) {
            CARP::AnimInfo &info = g_scene.infos[a][k];
            info.keys = g_keys[a][k];   // (an animation without frames faults Update in both)
            info.systemId = kSystemIds[RandomInt(kSystemIdChoices)];
            info.mirroredSystemId = RandomInt(2) ? info.systemId : kSystemIds[RandomInt(kSystemIdChoices)];
            info.flags = RandomInt(3) == 0 ? CARP::AnimInfo::kLoops : 0;
            info.frameCount = uint16_t((info.flags & CARP::AnimInfo::kLoops) ? 2 + RandomInt(kKeys - 1)
                                                                              : 1 + RandomInt(kKeys));
            info.unknown0B = uint8_t(Random());
            info.nextState = uint8_t(RandomInt(5));
            static const uint8_t kRates[] = { 15, 30, 60, 1, 24, 7 };
            info.frameRate = kRates[RandomInt(6)];
        }
        article.animInfos = made > 0 && RandomInt(8) != 0 ? g_scene.infos[a] : NULL;
        int effects = RandomInt(kEffects + 1);
        static const uint16_t kFlags[] = { 0x0002, 0x0004, 0x0008, 0x0010, 0x0080, 0x0200, 0x1000, 0x0001, 0x0400 };
        for (int e = 0; e < effects; e++) {
            ArticleEffect &effect = g_scene.effects[a][e];
            uint8_t *head = reinterpret_cast<uint8_t *>(&effect.position);    // +0x00 to +0x10
            for (size_t i = 0; i < offsetof(ArticleEffect, flags); i++)
                head[i] = uint8_t(Random());
            effect.flags = 0;
            for (int f = RandomInt(4); f > 0; f--)
                effect.flags |= kFlags[RandomInt(9)];
            effect.bits = uint16_t(Random());
            static const uint8_t kTypes[] = { 1, 2, 6, 8 };
            effect.type = kTypes[RandomInt(4)];
            effect.instance = uint8_t(RandomInt(40));
            effect.id = uint8_t(RandomInt(8));
            uint8_t *tail = reinterpret_cast<uint8_t *>(&effect.gfx);   // +0x18 on
            for (size_t i = 0; i < sizeof(ArticleEffect) - offsetof(ArticleEffect, gfx); i++)
                tail[i] = uint8_t(Random());
        }
        article.effects = RandomInt(5) == 0 ? NULL : g_scene.effects[a];
    }
    for (int a = 0; a < kArticles; a++)
        for (int k = 0; k < kInfos; k++)
            for (int f = 0; f < kKeys; f++) {
                CARP::AnimKey &key = g_keys[a][k][f];
                RandomQuaternion(&key.rotation);
                key.position = { Uniform(-50.0f, 50.0f), Uniform(-50.0f, 50.0f), Uniform(-50.0f, 50.0f) };
                int events = RandomInt(8);
                key.events = events == 0 ? &g_emptyEvents.header
                                         : events == 1 && g_haveNullEvents ? &g_nullEvents.header : NULL;
            }
    for (auto &state : g_states)
        for (uint8_t &byte : state)
            byte = uint8_t(Random());
    g_damageDesc = {};
    for (int c = 0; c < 3; c++) {
        g_damageDesc.damageBoxMin[c] = Uniform(-12.0f, -1.0f);
        g_damageDesc.damageBoxMax[c] = Uniform(1.0f, 12.0f);
    }
    for (void *&slot : g_fakeVtable)
        slot = reinterpret_cast<void *>(&FakeOtherSlot);
    g_fakeVtable[11] = reinterpret_cast<void *>(&FakeSetEventDynamicData);
    g_owner = {};
    g_owner.vtable = g_fakeVtable;
    g_owner.baseDesc = &g_damageDesc;
}

void MakeInstance(CARP::Instance *instance) {
    memset(instance, 0, sizeof(*instance));
    MATRIX4 m;
    RandomMatrix(&m);
    memcpy(instance, &m, sizeof(m));
    instance->flags = uint8_t((RandomInt(2) ? kWorldInstanceProcAnim : 0) | (RandomInt(3) == 0 ? kAnimInstanceMirrored : 0) |
                              (RandomInt(4) == 0 ? kWorldInstanceSceneObj : 0));
    instance->procAnimType = RandomInt(3) == 0 ? 0xff : uint8_t(RandomInt(kInfos));   // past the list: no frames
    instance->procAnimIndex = uint16_t(RandomInt(kStates));
    instance->articleDesc.value = RandomInt(10) == 0 ? 0 : uint32_t(uintptr_t(&g_scene.articles[RandomInt(kArticles)]));
    for (float &p : instance->position)
        p = Uniform(-14.0f, 14.0f);
    instance->packedDimensions = Random() & 0xc00fffff;
}

// ---------------------------------------------------------------------------------------------------------------
// The proc-anim functions

alignas(16) MATRIX4 g_xforms[8];
float g_params[16];

void RandomLists() {
    for (MATRIX4 &m : g_xforms)
        RandomMatrix(&m);
    for (float &p : g_params)
        p = RandomInt(4) == 0 ? float(RandomInt(5)) : Uniform(-400.0f, 400.0f);
    if (RandomInt(4) == 0)
        SetProcAnimXFormList(NULL, 0);
    else
        SetProcAnimXFormList(g_xforms, 1 + RandomInt(8));
    if (RandomInt(4) == 0)
        SetProcAnimParamList(NULL, 0);
    else
        SetProcAnimParamList(g_params, 1 + RandomInt(16));
}

void TestLists() {
    for (int i = 0; i < 200; i++) {
        bool none = RandomInt(3) == 0;
        uint32_t count = Random() % 20;
        uint8_t lists[2][kProcAnimListBytes];
        bool xform = RandomInt(2) == 0;
        Both([&](bool original) {
            if (xform)
                (original ? Orig_SetProcAnimXFormList : SetProcAnimXFormList)(none ? NULL : g_xforms, count);
            else
                (original ? Orig_SetProcAnimParamList : SetProcAnimParamList)(none ? NULL : g_params, count);
            memcpy(lists[original ? 0 : 1], ShadowProcAnimLists, kProcAnimListBytes);
        });
        CheckBytes("SetProcAnim*List", i, lists[0], lists[1], kProcAnimListBytes);
        g_cases++;
    }
}

// One GetInstanceMatrix call on both
void MatrixCase(const char *what, int index, CARP::Instance *instance, const Coord4 *offset, ProcAnimState *states) {
    alignas(16) MATRIX4 out[2];
    RandomMatrix(&out[0]);
    out[1] = out[0];
    bool answer[2] = {};
    if (!Both([&](bool original) {
            int k = original ? 0 : 1;
            answer[k] = original ? Orig_GetInstanceMatrix(instance, offset, &out[k], states)
                                 : GetInstanceMatrix(instance, offset, &out[k], states);
        }))
        return;
    CheckBytes(what, index, &out[0], &out[1], sizeof(MATRIX4));
    CheckWord(what, index, answer[0], answer[1]);
    g_cases++;
}

// An animation type the instance's article has (0..99 index its list; past a real list's end is not data)
uint8_t SafeType(const CARP::Instance &instance, uint8_t type) {
    if (type >= 100)
        return type;
    const WorldArticle *article = ArticleOf(&instance);
    if (article == NULL || article->animInfos == NULL)
        return type;
    if (article >= g_scene.articles && article < g_scene.articles + kArticles)
        return uint8_t(type % kInfos);   // the scene's lists are zero past their ends
    int count = 0;
    while (article->animInfos[count].frameRate != 0)
        count++;
    return count == 0 ? 0xff : uint8_t(type % count);
}

void TestProcAnim(const std::vector<CARP::Instance> &real) {
    // every instance of the track as it is drawn
    WWorld *world = fgWorld;
    if (ShadowRenderer != NULL && world != NULL)
        for (uint32_t i = 0; i < world->instanceCount; i++)
            MatrixCase("GetInstanceMatrix (track)", int(i), &world->instances[i], ShadowDrawOffset, world->procAnims);

    static const uint8_t kPorted[] = { 0, 5, 63, 99, 100, 101, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247,
                                       249, 250, 251, 252, 253, 254, 255 };
    float savedSimStep = ShadowSimStep, savedTickSeconds = ShadowTickSeconds;
    for (int i = 0; i < 30000; i++) {
        CARP::Instance instance;
        if (!real.empty() && RandomInt(2) == 0) {
            instance = real[RandomInt(int(real.size()))];
            if (RandomInt(3) == 0)
                instance.flags ^= kWorldInstanceProcAnim;
            if (RandomInt(4) == 0)
                instance.flags ^= kAnimInstanceMirrored;
            if (RandomInt(4) == 0)
                instance.packedDimensions = Random();
        } else {
            MakeInstance(&instance);
        }
        switch (RandomInt(3)) {
        case 0: break;
        case 1: instance.procAnimType = kPorted[RandomInt(sizeof(kPorted))]; break;
        default: instance.procAnimType = uint8_t(RandomInt(256)); break;
        }
        instance.procAnimType = SafeType(instance, instance.procAnimType);
        if (instance.procAnimType == 237 || instance.procAnimType == 238)
            if (ShadowRenderer == NULL)
                continue;
        int parameterRange = RandomInt(3) == 0 ? 0x10000 : 32;
        instance.procAnimIndex = uint16_t(RandomInt(kStates));
        for (auto &bytes : g_states) {
            ProcAnimState *state = reinterpret_cast<ProcAnimState *>(bytes);
            state->parameter = uint16_t(RandomInt(parameterRange));
            do
                state->procAnimType = uint8_t(RandomInt(4) == 0 ? RandomInt(256) : kPorted[RandomInt(sizeof(kPorted))]);
            while (state->procAnimType == 250);
            state->procAnimType = SafeType(instance, state->procAnimType);
            state->sceneObj = RandomInt(3) == 0 ? reinterpret_cast<RSceneObj *>(&g_owner) : NULL;
        }
        alignas(16) Coord4 offset = { Uniform(-30.0f, 30.0f), Uniform(-30.0f, 30.0f), Uniform(-30.0f, 30.0f), 1.0f };
        ShadowSimStep = RandomInt(3) == 0 ? Uniform(0.0f, 100000.0f) : float(Random() % 2000000);
        ShadowTickSeconds = RandomInt(2) ? 1.0f / 60.0f : 1.0f / 50.0f;
        RandomLists();
        MatrixCase("GetInstanceMatrix", i, &instance, &offset, reinterpret_cast<ProcAnimState *>(g_states));
    }
    ShadowSimStep = savedSimStep;
    ShadowTickSeconds = savedTickSeconds;
}

// ---------------------------------------------------------------------------------------------------------------
// Handles

// A handle's bytes with its own addresses made offsets from it
std::vector<uint8_t> Normalise(Handle *handle) {
    std::vector<uint8_t> bytes;
    if (handle == NULL || handle->dataSize > 0x100000)
        return bytes;
    size_t total = sizeof(Handle) + handle->dataSize;
    uintptr_t base = uintptr_t(handle);
    bytes.assign(reinterpret_cast<uint8_t *>(handle), reinterpret_cast<uint8_t *>(handle) + total);
    Handle *copy = reinterpret_cast<Handle *>(bytes.data());
    copy->states = reinterpret_cast<ProcAnimState *>(uintptr_t(handle->states) - base);
    copy->systems = reinterpret_cast<RAnimEngine::System *>(uintptr_t(handle->systems) - base);
    copy->systemIds = reinterpret_cast<uint8_t *>(uintptr_t(handle->systemIds) - base);
    copy->effects = reinterpret_cast<ArticleEffect *>(uintptr_t(handle->effects) - base);
    copy->effectBits = reinterpret_cast<uint16_t *>(uintptr_t(handle->effectBits) - base);
    size_t systems = uintptr_t(handle->systems) - base;
    if (systems + handle->systemCount * sizeof(RAnimEngine::System) <= total)
        for (int i = 0; i < handle->systemCount; i++) {
            RAnimEngine::System *system = reinterpret_cast<RAnimEngine::System *>(bytes.data() + systems) + i;
            system->handle = reinterpret_cast<Handle *>(uintptr_t(system->handle) - base);
        }
    return bytes;
}

uint32_t Relative(const void *pointer, const Handle *handle) {
    uintptr_t at = uintptr_t(pointer), base = uintptr_t(handle);
    if (handle != NULL && at >= base && at < base + sizeof(Handle) + handle->dataSize)
        return uint32_t(at - base) | 0x40000000u;
    return uint32_t(at);
}

// Each side's state: the active list, the event data, the articles, the owner's calls
struct Side {
    uint8_t active[kActiveListBytes];
    EventDynamicData eventData;
    Scene scene;
    int sceneObjCalls;
};
Side g_sides[2];
Handle *g_handles[2];

void SwapIn(int side) {
    memcpy(ShadowActiveList, g_sides[side].active, kActiveListBytes);
    gEventDynamicData = g_sides[side].eventData;
    g_scene = g_sides[side].scene;
    g_sceneObjCalls = g_sides[side].sceneObjCalls;
}

void SwapOut(int side) {
    memcpy(g_sides[side].active, ShadowActiveList, kActiveListBytes);
    g_sides[side].eventData = gEventDynamicData;
    g_sides[side].scene = g_scene;
    g_sides[side].sceneObjCalls = g_sceneObjCalls;
}

void CompareSides(const char *what, int index) {
    CheckVector(what, index, Normalise(g_handles[0]), Normalise(g_handles[1]));
    uint32_t active[2][kActiveListBytes / 4];
    EventDynamicData data[2];
    for (int k = 0; k < 2; k++) {
        memcpy(active[k], g_sides[k].active, kActiveListBytes);
        for (int i = 0; i < 0x100; i++)
            active[k][i] = Relative(reinterpret_cast<void *>(uintptr_t(active[k][i])), g_handles[k]);
        data[k] = g_sides[k].eventData;
        data[k].instance = reinterpret_cast<CARP::Instance *>(uintptr_t(Relative(data[k].instance, g_handles[k])));
    }
    CheckBytes("active list", index, active[0], active[1], kActiveListBytes);
    CheckBytes("event data", index, &data[0], &data[1], sizeof(EventDynamicData));
    CheckBytes("articles", index, &g_sides[0].scene, &g_sides[1].scene, sizeof(Scene));
    CheckWord("owner calls", index, uint32_t(g_sides[0].sceneObjCalls), uint32_t(g_sides[1].sceneObjCalls));
}

// A system's last frame is left unset until it starts: cleared in both, so the comparison sees only what the code
// defines (it is written before it is read)
void ClearUnstartedFrames() {
    for (Handle *handle : g_handles)
        if (handle != NULL && handle->dataSize <= 0x100000)
            for (int i = 0; i < handle->systemCount; i++)
                if (handle->systems[i].playing == 0)
                    handle->systems[i].lastFrame = 0;
}

// Random steps on the pair; `events` false: no Update (the frames' events would run the game's)
void Steps(int pairIndex, bool synthetic, bool events, uint32_t tick) {
    for (int step = 0; step < 160; step++) {
        Handle *reference = g_handles[0];
        uint32_t id = reference->systemCount > 0 && RandomInt(4) != 0
                          ? reference->systemIds[RandomInt(reference->systemCount)] : uint32_t(RandomInt(256));
        uint8_t stimulus = uint8_t(RandomInt(6));
        int mode = RandomInt(4);
        uint32_t answer[2] = {};
        int op = RandomInt(22);
        if (op == 0 && !events)
            op = 1;
        if (op == 6 && !synthetic)
            op = 7;
        if (op == 0 || RandomInt(3) == 0)
            tick += RandomInt(4) == 0 ? 0 : RandomInt(12);
        uint32_t time = Random() % 3000;
        uint16_t zones = uint16_t(Random());
        uint32_t value = Random();
        uint32_t index = uint32_t(RandomInt(reference->instanceCount + 1));
        uint32_t activeIndex = 0, activeCount = 0;
        memcpy(&activeCount, g_sides[0].active + 0x400, 4);
        if (activeCount > 0)
            activeIndex = uint32_t(RandomInt(int(activeCount)));
        if (op == 18 && activeCount == 0)
            op = 2;
        uint8_t rate = uint8_t(1 + RandomInt(60));
        bool stop = RandomInt(8) == 0, thunk = RandomInt(2) == 0;
        uint32_t instanceIndex = index % reference->instanceCount;
        uint32_t infoLimit = 0;   // FUN_000776d0's count, at most one past the article's list
        if (const WorldArticle *article = ArticleOf(&reference->Instances()[instanceIndex]))
            if (article->animInfos != NULL) {
                while (article->animInfos[infoLimit].frameRate != 0)
                    infoLimit++;
                infoLimit = value % (infoLimit + 2);
            }
        alignas(16) Coord4 offset = { Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f), 1.0f };
        alignas(16) MATRIX4 out[2];
        RandomMatrix(&out[0]);
        out[1] = out[0];
        bool ok = Both(
            [&](bool original) {
                int k = original ? 0 : 1;
                Handle *h = g_handles[k];
                switch (op) {
                case 0: original ? Orig_Update(tick) : RAnimEngine::Update(tick); break;
                case 1:
                case 2:
                    original ? Orig_ProcessStimuli4(h, 0, id, stimulus, tick, mode)
                             : h->ProcessStimuli(id, stimulus, tick, mode);
                    break;
                case 3:
                    original ? Orig_ProcessStimuli3(h, 0, stimulus, tick, mode) : h->ProcessStimuli(stimulus, tick, mode);
                    break;
                case 4:
                    original ? Orig_ProcessStimuliZones(h, 0, stimulus, zones, tick, mode)
                             : h->ProcessStimuliZones(stimulus, zones, tick, mode);
                    break;
                case 5:
                    original ? Orig_SetFrame(h, 0, id, stimulus, time) : h->SetFrame(id, stimulus, time);
                    break;
                case 6:
                    original ? Orig_SetFrameRate(h, 0, id, stimulus, rate) : h->SetFrameRate(id, stimulus, rate);
                    break;
                case 7: answer[k] = original ? Orig_NumFrames(h, 0, id, stimulus) : h->NumFrames(id, stimulus); break;
                case 8: answer[k] = original ? Orig_IsSystemPlaying(h, 0, id) : h->IsSystemPlaying(id); break;
                case 9: answer[k] = original ? Orig_GetSystemState(h, 0, id) : h->GetSystemState(id); break;
                case 10:
                    answer[k] = uint32_t(original ? Orig_GetFirstSystemInstanceIndex(h, 0, id)
                                                  : h->GetFirstSystemInstanceIndex(id));
                    break;
                case 11:
                    answer[k] = uint32_t(original ? Orig_GetBestSystemInstanceIndex(h, 0, id)
                                                  : h->GetBestSystemInstanceIndex(id));
                    break;
                case 12:
                    answer[k] = Relative(original ? Orig_GetFirstSystemInstance(h, 0, id) : h->GetFirstSystemInstance(id), h);
                    break;
                case 13:
                    if (index < h->instanceCount)
                        answer[k] = original ? Orig_GetInstanceSystemID(h, 0, index) : h->GetInstanceSystemID(index);
                    break;
                case 14: answer[k] = original ? Orig_AnySystemPlaying(h, 0) : h->AnySystemPlaying(); break;
                case 15:
                    answer[k] = Relative(original ? Orig_FindEffectByID(h, 0, value % 10) : h->FindEffectByID(value % 10), h);
                    break;
                case 16:
                    if (h->effectCount > 0)
                        original ? Orig_SetEffectBits(h, 0, value % h->effectCount, value >> 16)
                                 : h->SetEffectBits(value % h->effectCount, uint16_t(value >> 16));
                    break;
                case 17:
                    original ? Orig_InitAllSystemStates(h, 0, value % 5) : h->InitAllSystemStates(uint8_t(value % 5));
                    break;
                case 18:
                    answer[k] = CallDeactivateSystem(kDeactivateSystem, activeIndex, uint32_t(tick), value & 1);
                    break;
                case 19:
                    if (stop) {
                        if (original)
                            (thunk ? Orig_StopThunk : Orig_Stop)(h, 0);
                        else if (thunk)
                            h->StopThunk();
                        else
                            h->Stop();
                    }
                    break;
                case 20: {
                    WorldArticle *article = ArticleOf(&h->Instances()[instanceIndex]);
                    if (article != NULL && article->animInfos != NULL)
                        answer[k] = uint32_t(uintptr_t(CallFindAnimInfo(kFindAnimInfo, article, infoLimit, value % 5,
                                                                        stimulus)));
                    break;
                }
                default: {
                    CARP::Instance *instance = &h->Instances()[instanceIndex];
                    answer[k] = original ? Orig_GetInstanceMatrix(instance, &offset, &out[k], h->states)
                                         : GetInstanceMatrix(instance, &offset, &out[k], h->states);
                    break;
                }
                }
            },
            SwapIn, SwapOut);
        if (!ok) {
            printf("[animengine]   pair %d step %d (op %d) faulted - pair abandoned\n", pairIndex, step, op);
            return;
        }
        CompareSides("handle step", pairIndex * 1000 + op);
        CheckWord("handle step answer", pairIndex * 1000 + op, answer[0], answer[1]);
        CheckBytes("handle step matrix", pairIndex * 1000 + op, &out[0], &out[1], sizeof(MATRIX4));
        g_cases++;
    }
}

// Creates the pair (the original's handle and ours) and steps it, then takes it off the lists and frees it
void HandlePair(int pairIndex, uint32_t count, CARP::Instance *instances, ProcAnimState *states, RSceneObj *owner,
                bool synthetic, bool events) {
    uint32_t tick = Random() % 100000;
    for (Side &side : g_sides) {
        memset(side.active, 0, kActiveListBytes);
        memset(&side.eventData, 0, sizeof(EventDynamicData));
        side.scene = g_scene;
        side.sceneObjCalls = 0;
    }
    g_handles[0] = g_handles[1] = NULL;
    bool ok = Both(
        [&](bool original) {
            g_handles[original ? 0 : 1] = original ? Orig_Create(count, tick, instances, states, owner)
                                                   : Handle::Create(count, tick, instances, states, owner);
        },
        SwapIn, SwapOut);
    if (!ok || g_handles[0] == NULL || g_handles[1] == NULL) {
        printf("[animengine]   pair %d: Create faulted\n", pairIndex);
        return;
    }
    ClearUnstartedFrames();
    CompareSides("Create", pairIndex);
    g_cases++;
    if (Normalise(g_handles[0]) == Normalise(g_handles[1]))
        Steps(pairIndex, synthetic, events, tick);

    for (int k = 0; k < 2; k++) {
        SwapIn(k);
        g_handles[k]->Stop();
        Handle::OperatorDelete(g_handles[k], sizeof(Handle));
        SwapOut(k);
    }
}

void TestHandles(const std::vector<CARP::Instance *> &animated, ProcAnimState *trackStates) {
    // synthetic scenes
    for (int pair = 0; pair < 400; pair++) {
        if (pair % 20 == 0)
            MakeScene();
        CARP::Instance instances[24];
        uint32_t count = 1 + RandomInt(24);
        for (uint32_t i = 0; i < count; i++)
            MakeInstance(&instances[i]);
        ProcAnimState *states = RandomInt(4) == 0 ? NULL : reinterpret_cast<ProcAnimState *>(g_states);
        RSceneObj *owner = RandomInt(2) ? reinterpret_cast<RSceneObj *>(&g_owner) : NULL;
        HandlePair(pair, count, instances, states, owner, true, true);
    }

    // the track's animated instances, a few at a time
    int pair = 1000;
    for (size_t i = 0; i < animated.size() && pair < 1300; i += 1 + RandomInt(3), pair++) {
        uint32_t count = uint32_t(std::min<size_t>(1 + RandomInt(3), animated.size() - i));
        std::vector<CARP::Instance> instances;
        bool events = true;
        for (uint32_t k = 0; k < count; k++) {
            instances.push_back(*animated[i + k]);
            const WorldArticle *article = ArticleOf(animated[i + k]);
            for (const CARP::AnimInfo *info = article->animInfos; info->frameRate != 0; info++)
                if (info->keys != NULL)
                    for (uint32_t f = 0; f < info->frameCount; f++)
                        if (info->keys[f].events != NULL)
                            events = false;
        }
        HandlePair(pair, count, instances.data(), trackStates, NULL, false, events);
    }
}

// The live handles of the track's scene objects, read only
void TestLiveHandles() {
    WWorld *world = fgWorld;
    if (world == NULL || world->procAnims == NULL)
        return;
    std::vector<Handle *> handles;
    for (uint32_t i = 0; i < world->instanceCount; i++) {
        const CARP::Instance &instance = world->instances[i];
        if (!(instance.flags & kWorldInstanceProcAnim))
            continue;
        RSceneObj *object = world->procAnims[instance.procAnimIndex].sceneObj;
        if (object != NULL && object->animHandle != NULL &&
            std::find(handles.begin(), handles.end(), object->animHandle) == handles.end())
            handles.push_back(object->animHandle);
    }
    for (size_t n = 0; n < handles.size(); n++) {
        Handle *h = handles[n];
        for (uint32_t id = 0; id < 0x100; id++) {
            uint32_t answer[2][6] = {};
            if (!Both([&](bool original) {
                    uint32_t *a = answer[original ? 0 : 1];
                    a[0] = original ? Orig_IsSystemPlaying(h, 0, id) : h->IsSystemPlaying(id);
                    a[1] = original ? Orig_GetSystemState(h, 0, id) : h->GetSystemState(id);
                    a[2] = uint32_t(original ? Orig_GetFirstSystemInstanceIndex(h, 0, id) : h->GetFirstSystemInstanceIndex(id));
                    a[3] = uint32_t(original ? Orig_GetBestSystemInstanceIndex(h, 0, id) : h->GetBestSystemInstanceIndex(id));
                    a[4] = uint32_t(uintptr_t(original ? Orig_GetFirstSystemInstance(h, 0, id) : h->GetFirstSystemInstance(id)));
                    a[5] = uint32_t(uintptr_t(original ? Orig_FindEffectByID(h, 0, id) : h->FindEffectByID(id)));
                }))
                continue;
            CheckBytes("live handle queries", int(n), answer[0], answer[1], sizeof(answer[0]));
            g_cases++;
        }
        for (uint32_t i = 0; i < h->instanceCount; i++) {
            uint32_t answer[2] = {};
            Both([&](bool original) {
                answer[original ? 0 : 1] = original ? Orig_GetInstanceSystemID(h, 0, i) : h->GetInstanceSystemID(i);
            });
            CheckWord("live GetInstanceSystemID", int(n), answer[0], answer[1]);
            alignas(16) Coord4 offset = { 0.0f, 0.0f, 0.0f, 1.0f };
            MatrixCase("live handle matrix", int(n), &h->Instances()[i], &offset, h->states);
            g_cases++;
        }
    }
    uint32_t any[2] = {};
    for (size_t n = 0; n < handles.size(); n++) {
        Both([&](bool original) {
            any[original ? 0 : 1] = original ? Orig_AnySystemPlaying(handles[n], 0) : handles[n]->AnySystemPlaying();
        });
        CheckWord("live AnySystemPlaying", int(n), any[0], any[1]);
    }
}

}  // namespace

void AnimEngineShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_ANIMENGINESHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    WWorld *world = fgWorld;
    if (world == NULL || world->instances == NULL) {
        printf("[animengine] the track is not loaded - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);

    // what the tests write, put back at the end
    std::vector<uint8_t> savedActive(ShadowActiveList, ShadowActiveList + kActiveListBytes);
    std::vector<uint8_t> savedLists(ShadowProcAnimLists, ShadowProcAnimLists + kProcAnimListBytes);
    EventDynamicData savedData = gEventDynamicData;
    float savedSimStep = ShadowSimStep, savedTickSeconds = ShadowTickSeconds;

    std::vector<CARP::Instance> real(world->instances, world->instances + world->instanceCount);
    std::vector<CARP::Instance *> animated;
    for (uint32_t i = 0; i < world->instanceCount; i++) {
        const WorldArticle *article = ArticleOf(&world->instances[i]);
        if (article != NULL && article->animInfos != NULL && article->animInfos->frameRate != 0)
            animated.push_back(&world->instances[i]);
    }

    TestLiveHandles();
    FindNullEvent();
    MakeScene();
    TestSort();
    TestLists();
    TestProcAnim(real);
    memcpy(ShadowActiveList, savedActive.data(), kActiveListBytes);
    TestHandles(animated, world->procAnims);

    memcpy(ShadowActiveList, savedActive.data(), kActiveListBytes);
    memcpy(ShadowProcAnimLists, savedLists.data(), kProcAnimListBytes);
    gEventDynamicData = savedData;
    ShadowSimStep = savedSimStep;
    ShadowTickSeconds = savedTickSeconds;
    ResetFpu();

    printf("[animengine] RAnimEngine, handles, proc-anim functions vs originals: %d cases, %d checks, %d differ%s "
           "(%u animated track instances)\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "", unsigned(animated.size()));
    if (g_faults != 0)
        printf("[animengine]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
