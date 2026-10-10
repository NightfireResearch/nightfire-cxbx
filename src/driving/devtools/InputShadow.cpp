#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "InputShadow.h"
#include "FpControl.h"

#include "../engine/ActionQueue.hpp"
#include "../engine/Feedback.h"
#include "../engine/GameLoop.h"
#include "../engine/IOModule.hpp"
#include "../engine/InputConfig.h"
#include "../engine/InputDevice.hpp"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../data/SymbolTable.h"
#include "../physics/RigidBodyResolve.h"
#include "../platform/Pad.hpp"
#include "../platform/RealMemory.h"
#include "../platform/RealSystem.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_INPUTSHADOW=1, from the first simulation tick: the input layer's ports against the originals (their
// entry bytes swapped back in for each original call, common/xbeOriginal.h), on identical inputs, compared byte for
// byte. The state a call writes is snapshotted and put back between the original's run and the port's.
//
//   - InputConfigManager: ParseMasterConfigFile on the disc's Master.def and on rewritten copies (keyword case,
//     counts, section order, spacing); ParseDefFileForFrontEnd on every configuration's .def and mutated copies
//     (labels, slot names, no FRONTEND line); InitAndPreload and Shutdown on scratch managers, with the launch
//     page's choices varied (the active pad's mappings and the page compared too); SetConfig over every type and
//     configuration of the live manager, SetInverted, the getters, and GetLocaleID over every label of the live
//     configurations and every special id on every track name it tests.
//   - InputToAction and InputTable: the constructor on the live pad device, LoadParseDefFile on FrontEnd.def, every
//     configuration's .def and mutated copies (actions, controls, methods, thresholds, duplicated lines that make
//     the tables grow), Reset, the destructor; the tables' own constructor, destructor and growth.
//   - The text helpers: AdvanceFilePtr, getActionID (every action name and unknown ones), BuildFileName, BuildPath.
//   - IFeedback: Update on perturbed records, ticks and switches with a recording stand-in for XInputSetState (the
//     feedback thread suspended meanwhile); the constructor and destructor short of starting or stopping the thread;
//     Pause, ApplyJolt, SetRumble, SetVibrate and AddCollisionInfo on random and edge values.
//   - ActionQueueManager's vector: GetActionQueueManager, push_back, _Insert_n at every position (all three of its
//     branches), _Tidy and the pointer fill.
//
// Not covered here: IFeedback_Timer_Callback and the thread function (they post and wait on the real signal), the
// thread's start and stop, _Xlen (it throws). Those run in game.
//
// One mutation this catches: LoadParseDefFile giving '=' (UPDATE_CENTRED) the 0.5 default threshold as well as '>'
// and '<' changes the entries of every mutated copy that has an '=' without a threshold; Update's fading jolt
// indexing the dither by the tick rather than half of it changes which ticks switch the motors.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[2][2] = {
    { 0x0004f190, 0x00050b80 }, { 0x00051e90, 0x00052010 },
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

typedef IFeedback *(__fastcall *FeedbackConstructFn)(IFeedback *, int, int);
typedef void (__fastcall *FeedbackFn)(IFeedback *, int);
typedef void (__fastcall *FeedbackFloatFn)(IFeedback *, int, float);
typedef void (__fastcall *FeedbackFloat2Fn)(IFeedback *, int, float, float);
typedef void (__fastcall *FeedbackImpactFn)(IFeedback *, int, const CollisionImpact *);
typedef void (*VoidFn)(void);
typedef ActionQueueManager *(*GetManagerFn)(void);
typedef void (__fastcall *PushBackFn)(ActionQueueManager *, int, ActionQueue *const *);
typedef void (__fastcall *InsertNFn)(ActionQueueManager *, int, ActionQueue **, uint32_t, ActionQueue *const *);
typedef void (__fastcall *ManagerFn)(ActionQueueManager *, int);
typedef void (*FillFn)(ActionQueue **, ActionQueue **, ActionQueue *const *);
typedef void (*AdvanceFn)(char **);
typedef int (*ActionIdFn)(char *);
typedef char *(__fastcall *BuildFileNameFn)(char *, int, const char *, const char *, const char *);
typedef char *(__fastcall *BuildPathFn)(char *, int, const char *, const char *, const char *, const char *);
typedef InputTable *(__fastcall *TableConstructFn)(InputTable *, int);
typedef void (__fastcall *TableFn)(InputTable *, int);
typedef InputToAction *(__fastcall *MappingConstructFn)(InputToAction *, int, InputDevice *);
typedef void (__fastcall *MappingFn)(InputToAction *, int);
typedef void (__fastcall *MappingLoadFn)(InputToAction *, int, char *);
typedef InputConfigManager *(*GetConfigManagerFn)(void);
typedef void (__fastcall *ConfigFn)(InputConfigManager *, int);
typedef void (__fastcall *ConfigTextFn)(InputConfigManager *, int, char *);
typedef void (__fastcall *ConfigFrontEndFn)(InputConfigManager *, int, InputConfig *, char *);
typedef void (__fastcall *ConfigBoolFn)(InputConfigManager *, int, bool);
typedef uint32_t (__fastcall *ConfigIsFn)(InputConfigManager *, int);
typedef int (__fastcall *ConfigIntFn)(InputConfigManager *, int, int);
typedef int (__fastcall *ConfigLocaleFn)(InputConfigManager *, int, int, int, int);
typedef void (__fastcall *ConfigSetFn)(InputConfigManager *, int, int, int);

#define Orig_FeedbackConstruct ((FeedbackConstructFn)0x0004fe30)
#define Orig_FeedbackDestruct ((FeedbackFn)0x0004fb10)
#define Orig_ApplyJolt ((FeedbackFloatFn)0x0004f8c0)
#define Orig_SetRumble ((FeedbackFloat2Fn)0x0004f960)
#define Orig_SetVibrate ((FeedbackFloat2Fn)0x0004f9b0)
#define Orig_AddCollisionInfo ((FeedbackImpactFn)0x0004fee0)
#define Orig_FeedbackPause ((VoidFn)0x0004fa00)
#define Orig_FeedbackUpdate ((VoidFn)0x0004fbc0)
#define Orig_GetActionQueueManager ((GetManagerFn)0x0004f7d0)
#define Orig_PushBack ((PushBackFn)0x0004f830)
#define Orig_InsertN ((InsertNFn)0x0004f510)
#define Orig_Tidy ((ManagerFn)0x0004f400)
#define Orig_Fill ((FillFn)0x0004f3e0)
#define Orig_AdvanceFilePtr ((AdvanceFn)0x00050970)
#define Orig_getActionID ((ActionIdFn)0x0004f190)
#define Orig_BuildFileName ((BuildFileNameFn)0x00051e90)
#define Orig_BuildPath ((BuildPathFn)0x00051f30)
#define Orig_TableConstruct ((TableConstructFn)0x000507a0)
#define Orig_TableDestruct ((TableFn)0x000507c0)
#define Orig_TableGrow ((TableFn)0x000507e0)
#define Orig_MappingConstruct ((MappingConstructFn)0x00050860)
#define Orig_MappingDestruct ((MappingFn)0x000509c0)
#define Orig_MappingReset ((MappingFn)0x00050990)
#define Orig_MappingLoad ((MappingLoadFn)0x00050a20)
#define Orig_ConfigGet ((GetConfigManagerFn)0x00050270)
#define Orig_InitAndPreload ((ConfigFn)0x000504d0)
#define Orig_ParseMaster ((ConfigTextFn)0x000502d0)
#define Orig_ParseFrontEnd ((ConfigFrontEndFn)0x0004ff50)
#define Orig_Shutdown ((ConfigFn)0x00050080)
#define Orig_SetInverted ((ConfigBoolFn)0x00050140)
#define Orig_IsInverted ((ConfigIsFn)0x00050160)
#define Orig_GetNumConfigs ((ConfigIntFn)0x00050170)
#define Orig_GetCurrentConfig ((ConfigIntFn)0x00050180)
#define Orig_GetLocaleID ((ConfigLocaleFn)0x000501a0)
#define Orig_SetConfig ((ConfigSetFn)0x00050720)

// ---- the game's state the tests touch

#define ShLaunch (*(LaunchPage *)0x00243b90)
#define ShSuperEasy U8_AT(0x001e476c)
#define ShFeedbackAllowed U8_AT(0x001b6f50)
#define ShFeedbackThread (*(RealThread *)0x001e2410)
#define ShFeedbackUsers I32_AT(0x001e4430)
#define ShPadFeedback ((uint8_t *)0x001e4438)
#define ShFeedbackPorts ((FeedbackPort *)0x001e4488)
#define ShFeedbackTick U32_AT(0x001e4558)
#define ShPads ((PadData *)0x00241c50)
#define ShActionNames ((const StringToNumberEntry *)0x001b6ba0)
#define ShSlotNames ((StringToNumberEntry *)0x001b6f58)

const uint32_t kXInputSetState = 0x00184b59;
const size_t kPadFeedbackBytes = 0x46;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;
uint32_t g_random = 0x1234567u;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

float RandomFloat(float low, float high) {
    return low + (high - low) * float(Random() & 0xffffff) / float(0x1000000);
}

// Random floats with the edges among them
float EdgeFloat() {
    static const uint32_t kEdges[] = {
        0x00000000, 0x80000000, 0x3e800000, 0x3e800001, 0x3e7fffff, 0x3f800000, 0xbf800000, 0x7f800000,
        0xff800000, 0x7fc00000, 0xffc00000, 0x7f7fffff, 0x00000001, 0x4f000000, 0x3f000000, 0x3dcccccd,
    };
    uint32_t pick = Random() % 24;
    if (pick < 16) {
        float f;
        memcpy(&f, &kEdges[pick], 4);
        return f;
    }
    return RandomFloat(-2.0f, 3.0f);
}

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[input]   %s #%d: %s\n", what, index, detail);
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

void CheckInt(const char *what, int index, long long a, long long b) {
    g_checks++;
    if (a == b)
        return;
    char detail[96];
    snprintf(detail, sizeof(detail), "original %lld, port %lld", a, b);
    Differ(what, index, detail);
}

// Two loaded files: FileLoad gives the file's bytes in a block of exactly their size, with no terminating zero
void CheckLoaded(const char *what, int index, char *a, char *b) {
    g_checks++;
    if (a == nullptr || b == nullptr) {
        if (a != b)
            Differ(what, index, "one file missing");
        return;
    }
    size_t sizeA = MEM_size(a), sizeB = MEM_size(b);
    if (sizeA != sizeB) {
        char detail[96];
        snprintf(detail, sizeof(detail), "file sizes: original %u, port %u", unsigned(sizeA), unsigned(sizeB));
        Differ(what, index, detail);
    } else if (memcmp(a, b, sizeA) != 0) {
        Differ(what, index, "file contents differ");
    }
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context);

bool Guarded(CaseFn run, void *context) {
#ifdef _MSC_VER
    __try {
        run(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        return false;
    }
#else
    run(context);
    return true;
#endif
}

// Runs `run` guarded; with `original`, inside the window where the originals' entry bytes are back
template <class F>
bool Run(bool original, F &&run) {
    typedef typename std::remove_reference<F>::type Body;
    CaseFn thunk = [](void *context) { (*static_cast<Body *>(context))(); };
    if (original) {
        OriginalWindow window;
        return Guarded(thunk, &run);
    }
    return Guarded(thunk, &run);
}

// ---- a jump written over an entry point while a test runs

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;

    void Install(uint32_t address, const void *to) {
        at = address;
        DWORD old;
        on = VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old) != 0;
        if (!on)
            return;
        memcpy(saved, (void *)(uintptr_t)at, 5);
        uint8_t jump[5] = {0xe9};
        int32_t relative = int32_t((uintptr_t)to - (at + 5));
        memcpy(jump + 1, &relative, 4);
        memcpy((void *)(uintptr_t)at, jump, 5);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    }
    void Remove() {
        if (!on)
            return;
        memcpy((void *)(uintptr_t)at, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
        on = false;
    }
};

// =============================================================================================================
// IFeedback
// =============================================================================================================

struct MotorCall {
    void *handle;
    uint32_t event;
    uint16_t left, right;
};

std::vector<MotorCall> g_motorCalls;

uint32_t __stdcall FakeXInputSetState(void *handle, uint8_t *feedback) {
    MotorCall call;
    call.handle = handle;
    memcpy(&call.event, feedback + 4, 4);
    memcpy(&call.left, feedback + 0x42, 2);
    memcpy(&call.right, feedback + 0x44, 2);
    g_motorCalls.push_back(call);
    return 0;
}

struct FeedbackState {
    FeedbackPort ports[4];
    uint32_t tick;
    uint8_t padFeedback[kPadFeedbackBytes];
    uint8_t allowed;
    int32_t vibration;
    uint8_t superEasy;
    int32_t users;
    void *handles[4];
};

void SaveFeedback(FeedbackState *s) {
    memcpy(s->ports, ShFeedbackPorts, sizeof(s->ports));
    s->tick = ShFeedbackTick;
    memcpy(s->padFeedback, ShPadFeedback, kPadFeedbackBytes);
    s->allowed = ShFeedbackAllowed;
    s->vibration = ShLaunch.vibration;
    s->superEasy = ShSuperEasy;
    s->users = ShFeedbackUsers;
    for (int i = 0; i < 4; i++)
        s->handles[i] = ShPads[i].handle;
}

void LoadFeedback(const FeedbackState *s) {
    memcpy(ShFeedbackPorts, s->ports, sizeof(s->ports));
    ShFeedbackTick = s->tick;
    memcpy(ShPadFeedback, s->padFeedback, kPadFeedbackBytes);
    ShFeedbackAllowed = s->allowed;
    ShLaunch.vibration = s->vibration;
    ShSuperEasy = s->superEasy;
    ShFeedbackUsers = s->users;
    for (int i = 0; i < 4; i++)
        ShPads[i].handle = s->handles[i];
}

void CompareFeedback(const char *what, int index, const FeedbackState *a, const FeedbackState *b) {
    CheckBytes(what, index, a->ports, b->ports, sizeof(a->ports));
    CheckInt(what, index, a->tick, b->tick);
    CheckBytes(what, index, a->padFeedback, b->padFeedback, kPadFeedbackBytes);
    CheckInt(what, index, a->users, b->users);
}

void RandomPort(FeedbackPort *p, int number) {
    p->users = int(Random() % 5) - 1;
    p->port = number;
    p->pulseLevel = int(Random() % 300) - 20;
    p->steadyLevel = int(Random() % 300) - 20;
    p->joltLevel = int(Random() % 300);
    p->joltTicks = int(Random() % 30);
    p->joltFadeTicks = int(Random() % 16);
    p->rumbleLevel = int(Random() % 300) - 20;
    p->rumbleTicks = int(Random() % 24) - 2;
    p->vibrateLevel = int(Random() % 300) - 20;
    p->vibrateTicks = int(Random() % 24) - 2;
    p->pulseInput = Random() % 8 == 0 ? EdgeFloat() : RandomFloat(-0.5f, 3.0f);
    p->steadyInput = Random() % 8 == 0 ? EdgeFloat() : RandomFloat(-0.5f, 3.0f);
}

void RandomFeedbackState() {
    for (int i = 0; i < 4; i++)
        RandomPort(&ShFeedbackPorts[i], i);
    ShFeedbackTick = Random();
    ShFeedbackAllowed = Random() % 4 != 0;
    ShLaunch.vibration = Random() % 5 != 0 ? 1 : 0;
    ShSuperEasy = Random() % 6 == 0;
    for (int i = 0; i < 4; i++)
        ShPads[i].handle = Random() % 3 == 0 ? nullptr : (void *)(uintptr_t)(0x1000 + i);
}

void TestFeedbackUpdate() {
    Hook hook;
    hook.Install(kXInputSetState, (const void *)&FakeXInputSetState);
    if (!hook.on) {
        printf("[input] could not hook XInputSetState: Update not tested\n");
        return;
    }
    for (int c = 0; c < 4000; c++) {
        g_cases++;
        RandomFeedbackState();
        FeedbackState start, original, port;
        SaveFeedback(&start);
        g_motorCalls.clear();
        Run(true, [] { Orig_FeedbackUpdate(); });
        std::vector<MotorCall> originalCalls = g_motorCalls;
        SaveFeedback(&original);
        LoadFeedback(&start);
        g_motorCalls.clear();
        Run(false, [] { IFeedback::Update(); });
        SaveFeedback(&port);
        CompareFeedback("IFeedback::Update", c, &original, &port);
        CheckInt("IFeedback::Update calls", c, int(originalCalls.size()), int(g_motorCalls.size()));
        if (originalCalls.size() == g_motorCalls.size() && !originalCalls.empty())
            CheckBytes("IFeedback::Update call", c, originalCalls.data(), g_motorCalls.data(),
                       originalCalls.size() * sizeof(MotorCall));
    }
    hook.Remove();
}

// The constructor and destructor with enough users that neither starts nor stops the thread
void TestFeedbackLifetime() {
    for (int c = 0; c < 2000; c++) {
        g_cases++;
        RandomFeedbackState();
        int port = int(Random() % 4);
        ShFeedbackPorts[port].users = int(Random() % 4);
        ShFeedbackUsers = 2 + int(Random() % 3);
        bool construct = Random() % 2 == 0;
        FeedbackState start, original, ported;
        SaveFeedback(&start);
        IFeedback a = {(FeedbackPort *)(uintptr_t)0xdead0000}, b = a;
        IFeedback *returnedA = nullptr, *returnedB = nullptr;
        if (construct) {
            Run(true, [&] { returnedA = Orig_FeedbackConstruct(&a, 0, port); });
        } else {
            a.record = &ShFeedbackPorts[port];
            Run(true, [&] { Orig_FeedbackDestruct(&a, 0); });
        }
        SaveFeedback(&original);
        LoadFeedback(&start);
        if (construct) {
            Run(false, [&] { returnedB = b.Construct(port); });
        } else {
            b.record = &ShFeedbackPorts[port];
            Run(false, [&] { b.Destruct(); });
        }
        SaveFeedback(&ported);
        const char *what = construct ? "IFeedback::IFeedback" : "IFeedback::~IFeedback";
        CompareFeedback(what, c, &original, &ported);
        CheckInt(what, c, (long long)(uintptr_t)a.record, (long long)(uintptr_t)b.record);
        CheckInt(what, c, returnedA == &a, returnedB == &b);
    }
    for (int c = 0; c < 200; c++) {
        g_cases++;
        RandomFeedbackState();
        FeedbackState start, original, ported;
        SaveFeedback(&start);
        Run(true, [] { Orig_FeedbackPause(); });
        SaveFeedback(&original);
        LoadFeedback(&start);
        Run(false, [] { IFeedback::Pause(); });
        SaveFeedback(&ported);
        CompareFeedback("IFeedback::Pause", c, &original, &ported);
    }
}

// The setters, on a record of their own
void TestFeedbackSetters() {
    for (int c = 0; c < 20000; c++) {
        g_cases++;
        FeedbackPort start;
        RandomPort(&start, int(Random() % 4));
        FeedbackPort recordA = start, recordB = start;
        IFeedback a = {&recordA}, b = {&recordB};
        float x = EdgeFloat(), y = EdgeFloat();
        int which = c % 4;
        const char *what = nullptr;
        if (which == 0) {
            what = "IFeedback::ApplyJolt";
            Run(true, [&] { Orig_ApplyJolt(&a, 0, x); });
            Run(false, [&] { b.ApplyJolt(x); });
        } else if (which == 1) {
            what = "IFeedback::SetRumble";
            Run(true, [&] { Orig_SetRumble(&a, 0, x, y); });
            Run(false, [&] { b.SetRumble(x, y); });
        } else if (which == 2) {
            what = "IFeedback::SetVibrate";
            Run(true, [&] { Orig_SetVibrate(&a, 0, x, y); });
            Run(false, [&] { b.SetVibrate(x, y); });
        } else {
            what = "IFeedback::AddCollisionInfo";
            CollisionImpact impact;
            memset(&impact, 0, sizeof(impact));
            impact.strength = x;
            impact.unknown4a = Random() % 3 == 0 ? 14 : uint16_t(Random() % 32);
            impact.tag = Random() % 3 == 0 ? 14 : uint16_t(Random() % 32);
            impact.kindB = Random() % 2 == 0 ? 4 : uint8_t(Random() % 8);
            Run(true, [&] { Orig_AddCollisionInfo(&a, 0, &impact); });
            Run(false, [&] { b.AddCollisionInfo(&impact); });
        }
        CheckBytes(what, c, &recordA, &recordB, sizeof(FeedbackPort));
    }
}

void TestFeedback() {
    FeedbackState real;
    // Update runs on the feedback thread, on the same records: it waits while these run.
    HANDLE thread = ShFeedbackUsers > 0 ? ShFeedbackThread.handle : nullptr;
    bool suspended = thread != nullptr && SuspendThread(thread) != (DWORD)-1;
    SaveFeedback(&real);
    TestFeedbackUpdate();
    TestFeedbackLifetime();
    TestFeedbackSetters();
    LoadFeedback(&real);
    if (suspended)
        ResumeThread(thread);
}

// =============================================================================================================
// ActionQueueManager's vector
// =============================================================================================================

ActionQueue *RandomQueue() {
    return (ActionQueue *)(uintptr_t)(Random() & 0xfffffffc);
}

void CompareVectors(const char *what, int index, const ActionQueueManager *a, const ActionQueueManager *b) {
    long long sizeA = a->queuesFirst ? a->queuesLast - a->queuesFirst : -1;
    long long sizeB = b->queuesFirst ? b->queuesLast - b->queuesFirst : -1;
    CheckInt(what, index, sizeA, sizeB);
    CheckInt(what, index, a->queuesFirst ? a->queuesEnd - a->queuesFirst : -1,
             b->queuesFirst ? b->queuesEnd - b->queuesFirst : -1);
    if (sizeA == sizeB && sizeA > 0)
        CheckBytes(what, index, a->queuesFirst, b->queuesFirst, size_t(sizeA) * sizeof(ActionQueue *));
}

void TestQueueVector() {
    ActionQueueManager *returnedA = nullptr, *returnedB = nullptr;
    Run(true, [&] { returnedA = Orig_GetActionQueueManager(); });
    Run(false, [&] { returnedB = ActionQueueManager::GetActionQueueManager(); });
    g_cases++;
    CheckInt("GetActionQueueManager", 0, (long long)(uintptr_t)returnedA, (long long)(uintptr_t)returnedB);

    // push_back from empty
    for (int c = 0; c < 20; c++) {
        g_cases++;
        ActionQueueManager a = {}, b = {};
        int pushes = int(Random() % 120);
        for (int k = 0; k < pushes; k++) {
            ActionQueue *queue = RandomQueue();
            Run(true, [&] { Orig_PushBack(&a, 0, &queue); });
            Run(false, [&] { b.PushBack(&queue); });
            CompareVectors("ActionQueueManager::PushBack", c * 1000 + k, &a, &b);
        }
        Run(true, [&] { Orig_Tidy(&a, 0); });
        Run(false, [&] { b.Tidy(); });
        CompareVectors("ActionQueueManager::Tidy", c, &a, &b);
    }

    // _Insert_n anywhere, any count, the value inside the vector or not
    for (int c = 0; c < 600; c++) {
        g_cases++;
        ActionQueueManager a = {}, b = {};
        int size = int(Random() % 24);
        for (int k = 0; k < size; k++) {
            ActionQueue *queue = RandomQueue();
            a.PushBack(&queue);
            b.PushBack(&queue);
        }
        uint32_t where = size == 0 ? 0 : Random() % uint32_t(size + 1);
        uint32_t count = Random() % 7;
        ActionQueue *outside = RandomQueue();
        bool inside = size > 0 && Random() % 3 == 0;
        uint32_t from = inside ? Random() % uint32_t(size) : 0;
        Run(true, [&] {
            Orig_InsertN(&a, 0, a.queuesFirst + where, count, inside ? &a.queuesFirst[from] : &outside);
        });
        Run(false, [&] { b.InsertN(b.queuesFirst + where, count, inside ? &b.queuesFirst[from] : &outside); });
        CompareVectors("ActionQueueManager::InsertN", c, &a, &b);
        a.Tidy();
        b.Tidy();
    }

    for (int c = 0; c < 500; c++) {
        g_cases++;
        ActionQueue *arrayA[24], *arrayB[24];
        for (int k = 0; k < 24; k++)
            arrayA[k] = arrayB[k] = RandomQueue();
        int first = int(Random() % 24), last = first + int(Random() % uint32_t(25 - first));
        ActionQueue *value = RandomQueue();
        Run(true, [&] { Orig_Fill(arrayA + first, arrayA + last, &value); });
        Run(false, [&] { FillQueuePointers(arrayB + first, arrayB + last, &value); });
        CheckBytes("FillQueuePointers", c, arrayA, arrayB, sizeof(arrayA));
    }
}

// =============================================================================================================
// The text helpers
// =============================================================================================================

std::string RandomWord(int maxLength) {
    static const char kLetters[] = "abcXYZ019_./\\ \t";
    std::string s;
    int length = int(Random() % uint32_t(maxLength + 1));
    for (int i = 0; i < length; i++)
        s += kLetters[Random() % (sizeof(kLetters) - 1)];
    return s;
}

void TestTextHelpers() {
    for (int c = 0; c < 2000; c++) {
        g_cases++;
        std::string text = RandomWord(12);
        if (Random() % 2)
            text.insert(Random() % (text.size() + 1), "\n");
        if (Random() % 4 == 0)
            text.insert(Random() % (text.size() + 1), "\r\n");
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back(0);
        char *a = buffer.data(), *b = buffer.data();
        Run(true, [&] { Orig_AdvanceFilePtr(&a); });
        Run(false, [&] { AdvanceFilePtr(&b); });
        CheckInt("AdvanceFilePtr", c, a ? a - buffer.data() : -1, b ? b - buffer.data() : -1);
    }

    std::vector<std::string> names;
    for (const StringToNumberEntry *e = ShActionNames; e->string != nullptr; e++) {
        names.push_back(e->string);
        std::string lower = e->string;
        for (char &ch : lower)
            ch = char(tolower((unsigned char)ch));
        names.push_back(lower);
        names.push_back(std::string(e->string).substr(0, strlen(e->string) / 2));
    }
    names.push_back("");
    names.push_back("BOGUS_ACTION");
    for (size_t c = 0; c < names.size(); c++) {
        g_cases++;
        std::vector<char> name(names[c].begin(), names[c].end());
        name.push_back(0);
        int a = 0, b = 0;
        Run(true, [&] { a = Orig_getActionID(name.data()); });
        Run(false, [&] { b = getActionID(name.data()); });
        CheckInt("getActionID", int(c), a, b);
    }

    for (int c = 0; c < 2000; c++) {
        g_cases++;
        std::string w[4];
        for (std::string &s : w)
            s = Random() % 4 == 0 ? std::string() : RandomWord(14);
        char bufferA[128], bufferB[128];
        memset(bufferA, 0xcc, sizeof(bufferA));
        memset(bufferB, 0xcc, sizeof(bufferB));
        char *ra = nullptr, *rb = nullptr;
        if (c % 2 == 0) {
            Run(true, [&] { ra = Orig_BuildFileName(bufferA, 0, w[0].c_str(), w[1].c_str(), w[2].c_str()); });
            Run(false, [&] { rb = BuildFileName(bufferB, 0, w[0].c_str(), w[1].c_str(), w[2].c_str()); });
            CheckBytes("BuildFileName", c, bufferA, bufferB, sizeof(bufferA));
        } else {
            Run(true, [&] {
                ra = Orig_BuildPath(bufferA, 0, w[0].c_str(), w[1].c_str(), w[2].c_str(), w[3].c_str());
            });
            Run(false, [&] { rb = BuildPath(bufferB, 0, w[0].c_str(), w[1].c_str(), w[2].c_str(), w[3].c_str()); });
            CheckBytes("BuildPath", c, bufferA, bufferB, sizeof(bufferA));
        }
        CheckInt("BuildFileName/BuildPath answer", c, ra == bufferA, rb == bufferB);
    }
}

// =============================================================================================================
// InputTable and InputToAction
// =============================================================================================================

// A table by value: its count, capacity, whether it is still in place, its entries
void CompareTables(const char *what, int index, const InputTable *a, const InputTable *b) {
    CheckInt(what, index, a->count, b->count);
    CheckInt(what, index, a->capacity, b->capacity);
    CheckInt(what, index, a->entries == a->inlineEntries, b->entries == b->inlineEntries);
    if (a->count == b->count && a->count > 0)
        CheckBytes(what, index, a->entries, b->entries, size_t(a->count) * sizeof(InputTableEntry));
}

void CompareMappings(const char *what, int index, const InputToAction *a, const InputToAction *b) {
    CheckInt(what, index, a->numTables, b->numTables);
    if (a->numTables != b->numTables)
        return;
    for (int i = 0; i < a->numTables; i++)
        CompareTables(what, index, &a->tables[i], &b->tables[i]);
}

struct MappingSnapshot {
    std::vector<int32_t> shape;
    std::vector<InputTableEntry> entries;
};

void Snapshot(const InputToAction *mapping, MappingSnapshot *s) {
    s->shape.clear();
    s->entries.clear();
    for (int i = 0; i < mapping->numTables; i++) {
        const InputTable &t = mapping->tables[i];
        s->shape.push_back(t.count);
        s->shape.push_back(t.capacity);
        s->entries.insert(s->entries.end(), t.entries, t.entries + t.count);
    }
}

void CompareSnapshots(const char *what, int index, const MappingSnapshot *a, const MappingSnapshot *b) {
    CheckInt(what, index, int(a->shape.size()), int(b->shape.size()));
    CheckInt(what, index, int(a->entries.size()), int(b->entries.size()));
    if (a->shape.size() == b->shape.size() && !a->shape.empty())
        CheckBytes(what, index, a->shape.data(), b->shape.data(), a->shape.size() * sizeof(int32_t));
    if (a->entries.size() == b->entries.size() && !a->entries.empty())
        CheckBytes(what, index, a->entries.data(), b->entries.data(), a->entries.size() * sizeof(InputTableEntry));
}

void TestTables() {
    for (int c = 0; c < 300; c++) {
        g_cases++;
        InputTable a, b;
        memset(&a, 0xcd, sizeof(a));
        memset(&b, 0xcd, sizeof(b));
        InputTable *ra = nullptr, *rb = nullptr;
        Run(true, [&] { ra = Orig_TableConstruct(&a, 0); });
        Run(false, [&] { rb = b.Construct(); });
        CheckInt("InputTable::InputTable", c, ra == &a, rb == &b);
        CompareTables("InputTable::InputTable", c, &a, &b);
        int grows = int(Random() % 6);
        for (int g = 0; g < grows; g++) {
            int count = int(Random() % uint32_t(a.capacity + 1));
            a.count = b.count = count;
            for (int i = 0; i < count; i++) {
                InputTableEntry e = {int32_t(Random() % 10), RandomFloat(-1.0f, 1.0f), int32_t(Random() % 0x75)};
                a.entries[i] = e;
                b.entries[i] = e;
            }
            Run(true, [&] { Orig_TableGrow(&a, 0); });
            Run(false, [&] { b.GrowArrayForOneElement(); });
            CompareTables("InputTable::GrowArrayForOneElement", c * 10 + g, &a, &b);
        }
        Run(true, [&] { Orig_TableDestruct(&a, 0); });
        Run(false, [&] { b.Destruct(); });
    }
}

// ---- .def texts, rewritten

std::vector<std::string> SplitLines(const char *text) {
    std::vector<std::string> lines;
    const char *p = text;
    while (*p != 0) {
        const char *end = strstr(p, "\r\n");
        if (end == nullptr) {
            lines.push_back(p);
            break;
        }
        lines.push_back(std::string(p, end));
        p = end + 2;
    }
    return lines;
}

std::vector<std::string> Tokens(const std::string &line) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            i++;
        size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            i++;
        if (i > start)
            tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

std::string First(const std::string &line) {
    std::vector<std::string> t = Tokens(line);
    return t.empty() ? std::string() : t[0];
}

bool IsKeyword(const std::string &token, const char *keyword) {
    return _stricmp(token.c_str(), keyword) == 0;
}

std::string Pick(const std::vector<std::string> &from) {
    return from[Random() % from.size()];
}

std::string CaseChanged(const std::string &s) {
    std::string out = s;
    for (char &ch : out)
        ch = Random() % 2 ? char(toupper((unsigned char)ch)) : char(tolower((unsigned char)ch));
    return out;
}

// A copy with its mappings rewritten (actions, controls, methods, thresholds, duplicated and dropped lines) and
// its labels changed. Every control stays one of the device's: an unknown one would index table -1. With
// `dropFrontEnd`, one copy in ten loses its FRONTEND line - only for ParseDefFileForFrontEnd: LoadParseDefFile
// stops at that line, and without it would take each label line for a mapping of an unknown control.
std::string MutateDefinition(const char *text, const std::vector<std::string> &actions,
                             const std::vector<std::string> &controls, const std::vector<std::string> &slots,
                             bool dropFrontEnd) {
    static const char *const kMethods[] = {"", "U", "+", "-", ">", "<", "=", "I", "N", "R", "M", "Q"};
    static const char *const kThresholds[] = {"", "0.25", "0.75", "-1", "1e3", "abc", "0.5"};
    static const char *const kLabels[] = {"1424", "2713", "2712", "2711", "2719", "2723", "2463", "-5", "x", "0"};
    std::vector<std::string> lines = SplitLines(text);
    std::string out;
    if (lines.empty())
        return out;
    out += lines[0] + "\r\n";
    enum { kMappings, kLabelsPart, kDone } part = kMappings;
    for (size_t i = 1; i < lines.size() && part != kDone; i++) {
        std::vector<std::string> t = Tokens(lines[i]);
        if (t.empty())
            continue;   // an empty line would give strtok nothing to answer
        if (IsKeyword(t[0], "END")) {
            out += (Random() % 4 == 0 ? CaseChanged(t[0]) : t[0]) + "\r\n";
            part = kDone;
        } else if (IsKeyword(t[0], "FRONTEND")) {
            if (!dropFrontEnd || Random() % 10 != 0)
                out += (Random() % 4 == 0 ? CaseChanged(t[0]) : t[0]) + "\t; labels\r\n";
            part = kLabelsPart;
        } else if (part == kLabelsPart) {
            std::string slot = t[0], label = t.size() > 1 ? t[1] : "0";
            if (Random() % 10 == 0)
                slot = Pick(slots);
            if (Random() % 3 == 0)
                label = kLabels[Random() % (sizeof(kLabels) / sizeof(kLabels[0]))];
            out += slot + "\t\t" + label + "\r\n";
        } else if (t.size() >= 2) {
            std::string action = t[0], control = t[1];
            std::string method = t.size() > 2 ? t[2] : "", threshold = t.size() > 3 ? t[3] : "";
            if (Random() % 10 == 0)
                action = Random() % 4 == 0 ? std::string("BOGUS_ACTION") : Pick(actions);
            if (Random() % 10 == 0)
                control = Pick(controls);
            if (Random() % 4 == 0)
                method = kMethods[Random() % (sizeof(kMethods) / sizeof(kMethods[0]))];
            if (Random() % 5 == 0)
                threshold = kThresholds[Random() % (sizeof(kThresholds) / sizeof(kThresholds[0]))];
            if (method.empty() && !threshold.empty())
                method = "U";
            std::string line = action + "\t\t" + control;
            if (!method.empty())
                line += "\t" + method;
            if (!threshold.empty())
                line += "\t" + threshold;
            if (Random() % 6 == 0)
                line += "\t";
            int copies = Random() % 10 == 0 ? 1 + int(Random() % 5) : 1;
            if (Random() % 20 == 0)
                copies = 0;
            for (int k = 0; k < copies; k++)
                out += line + "\r\n";
        }
    }
    if (part != kDone)
        out += "END\r\n";
    out += "\r\n";
    return out;
}

std::vector<char> Writable(const std::string &s) {
    std::vector<char> v(s.begin(), s.end());
    v.push_back(0);
    return v;
}

InputDevice *ActiveDevice() {
    IOModule *io = IOModule::GetIOModule();
    for (int i = 0; i < io->numControllers && i < 16; i++)
        if (io->padDevices[i] != nullptr)
            return io->padDevices[i];
    return nullptr;
}

std::vector<std::string> NamesOf(const StringToNumberEntry *table) {
    std::vector<std::string> names;
    for (const StringToNumberEntry *e = table; e->string != nullptr; e++)
        names.push_back(e->string);
    return names;
}

// Every .def text the live manager holds: FrontEnd.def, then each configuration's
std::vector<const char *> LiveDefinitions() {
    std::vector<const char *> texts;
    InputConfigManager *m = InputConfigManager::Get();
    if (m->frontEndDefinition != nullptr)
        texts.push_back(m->frontEndDefinition);
    for (int type = 0; type < 2; type++)
        for (int i = 0; i < m->numConfigs[type]; i++)
            if (m->configs[type] != nullptr && m->configs[type][i].definition != nullptr)
                texts.push_back(m->configs[type][i].definition);
    return texts;
}

void TestMappings() {
    InputDevice *device = ActiveDevice();
    if (device == nullptr) {
        printf("[input] no pad device: InputToAction not tested\n");
        return;
    }
    std::vector<std::string> actions = NamesOf(ShActionNames), slots = NamesOf(ShSlotNames), controls;
    std::vector<const char *> texts = LiveDefinitions();

    for (int round = 0; round < 3; round++) {
        g_cases++;
        InputToAction a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        Run(true, [&] { Orig_MappingConstruct(&a, 0, device); });
        Run(false, [&] { b.Construct(device); });
        CompareMappings("InputToAction::InputToAction", round, &a, &b);
        CheckInt("InputToAction::InputToAction", round, a.numTables, b.numTables);
        if (a.numTables == b.numTables && a.scalarNames != nullptr && b.scalarNames != nullptr)
            CheckBytes("InputToAction names", round, a.scalarNames, b.scalarNames,
                       size_t(a.numTables + 1) * sizeof(StringToNumberEntry));
        CheckInt("InputToAction lookup", round, a.scalarLookup != nullptr, b.scalarLookup != nullptr);
        if (a.scalarLookup != nullptr && b.scalarLookup != nullptr) {
            CheckInt("InputToAction lookup", round, a.scalarLookup->count, b.scalarLookup->count);
            CheckInt("InputToAction lookup", round, a.scalarLookup->table == a.scalarNames,
                     b.scalarLookup->table == b.scalarNames);
            CheckInt("InputToAction lookup", round, a.scalarLookup->sortedByString == nullptr,
                     b.scalarLookup->sortedByString == nullptr);
        }
        if (controls.empty() && b.scalarNames != nullptr)
            controls = NamesOf(b.scalarNames);

        int index = 0;
        for (const char *text : texts) {
            for (int copy = 0; copy < 25; copy++, index++) {
                g_cases++;
                std::vector<char> buffer =
                    Writable(copy == 0 ? std::string(text) : MutateDefinition(text, actions, controls, slots, false));
                if (Random() % 3 == 0) {
                    Run(true, [&] { Orig_MappingReset(&a, 0); });
                    Run(false, [&] { b.Reset(); });
                    CompareMappings("InputToAction::Reset", index, &a, &b);
                }
                Run(true, [&] { Orig_MappingLoad(&a, 0, buffer.data()); });
                Run(false, [&] { b.LoadParseDefFile(buffer.data()); });
                CompareMappings("InputToAction::LoadParseDefFile", round * 10000 + index, &a, &b);
            }
        }
        Run(true, [&] { Orig_MappingDestruct(&a, 0); });
        Run(false, [&] { b.Destruct(); });
    }
}

// =============================================================================================================
// InputConfigManager
// =============================================================================================================

struct LaunchChoices {
    int32_t driving, pov, inverted;
};

LaunchChoices SaveChoices() {
    return {ShLaunch.drivingConfig, ShLaunch.povConfig, ShLaunch.invertedControls};
}

void LoadChoices(const LaunchChoices &c) {
    ShLaunch.drivingConfig = c.driving;
    ShLaunch.povConfig = c.pov;
    ShLaunch.invertedControls = c.inverted;
}

void CompareChoices(const char *what, int index, const LaunchChoices &a, const LaunchChoices &b) {
    CheckInt(what, index, a.driving, b.driving);
    CheckInt(what, index, a.pov, b.pov);
    CheckInt(what, index, a.inverted, b.inverted);
}

// The managers' own fields, then the configurations by value (their texts by content)
void CompareManagers(const char *what, int index, const InputConfigManager *a, const InputConfigManager *b,
                     bool texts) {
    CheckInt(what, index, a->initialised, b->initialised);
    CheckInt(what, index, a->currentType, b->currentType);
    CheckBytes(what, index, a->currentConfig, b->currentConfig, sizeof(a->currentConfig));
    CheckBytes(what, index, a->numConfigs, b->numConfigs, sizeof(a->numConfigs));
    CheckInt(what, index, a->inverted, b->inverted);
    CheckInt(what, index, a->frontEndSlots != nullptr, b->frontEndSlots != nullptr);
    CheckInt(what, index, (long long)(uintptr_t)a->unknown2C, (long long)(uintptr_t)b->unknown2C);
    if (texts)
        CheckLoaded(what, index, a->frontEndDefinition, b->frontEndDefinition);
    else
        CheckInt(what, index, (long long)(uintptr_t)a->frontEndDefinition, (long long)(uintptr_t)b->frontEndDefinition);
    for (int type = 0; type < 2; type++) {
        CheckInt(what, index, a->configs[type] != nullptr, b->configs[type] != nullptr);
        if (a->configs[type] == nullptr || b->configs[type] == nullptr || a->numConfigs[type] != b->numConfigs[type])
            continue;
        for (int i = 0; i < a->numConfigs[type]; i++) {
            const InputConfig &x = a->configs[type][i], &y = b->configs[type][i];
            CheckBytes(what, index, x.name, y.name, sizeof(x.name));
            CheckBytes(what, index, x.localeIDs, y.localeIDs, sizeof(x.localeIDs));
            if (texts)
                CheckLoaded(what, index, x.definition, y.definition);
            else
                CheckInt(what, index, (long long)(uintptr_t)x.definition, (long long)(uintptr_t)y.definition);
        }
    }
}

void FreeConfigs(InputConfigManager *m) {
    for (int type = 0; type < 2; type++) {
        OperatorDelete(m->configs[type]);
        m->configs[type] = nullptr;
    }
}

// Master.def rewritten: keyword case, larger counts, the sections swapped, other spacing
std::string MutateMaster(const char *text) {
    struct Section {
        std::string keyword;
        int count;
        std::vector<std::string> names;
    };
    std::vector<Section> sections;
    std::string end = "End";
    for (const std::string &line : SplitLines(text)) {
        std::vector<std::string> t = Tokens(line);
        if (t.empty())
            continue;
        if (IsKeyword(t[0], "END")) {
            end = t[0];
            break;
        }
        if (IsKeyword(t[0], "Driving") || IsKeyword(t[0], "POV"))
            sections.push_back({t[0], t.size() > 1 ? atoi(t[1].c_str()) : 0, {}});
        else if (!sections.empty())
            sections.back().names.push_back(t[0]);
    }
    if (sections.size() == 2 && Random() % 2)
        std::swap(sections[0], sections[1]);
    std::string out;
    for (Section &s : sections) {
        std::string keyword = Random() % 3 == 0 ? CaseChanged(s.keyword) : s.keyword;
        int count = int(s.names.size()) > s.count ? int(s.names.size()) : s.count;
        count += int(Random() % 3);
        out += keyword + (Random() % 2 ? "\t\t" : " ") + std::to_string(count) + "\r\n";
        for (std::string &name : s.names)
            out += (Random() % 4 == 0 ? CaseChanged(name) : name) + (Random() % 5 == 0 ? "\t" : "") + "\r\n";
    }
    out += (Random() % 3 == 0 ? CaseChanged(end) : end) + "\r\n";
    return out;
}

void TestMasterFile() {
    char *master = static_cast<char *>(UFileLoader::FileLoad("data/Control/Master.def", 0));
    if (master == nullptr) {
        printf("[input] could not load Master.def\n");
        return;
    }
    for (int c = 0; c < 200; c++) {
        g_cases++;
        std::vector<char> text = Writable(c == 0 ? std::string(master) : MutateMaster(master));
        InputConfigManager a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        Run(true, [&] { Orig_ParseMaster(&a, 0, text.data()); });
        Run(false, [&] { b.ParseMasterConfigFile(text.data()); });
        CompareManagers("InputConfigManager::ParseMasterConfigFile", c, &a, &b, false);
        FreeConfigs(&a);
        FreeConfigs(&b);
    }
    MEM_free(master);
}

void TestFrontEndLabels() {
    std::vector<std::string> actions = NamesOf(ShActionNames), slots = NamesOf(ShSlotNames), controls;
    InputDevice *device = ActiveDevice();
    if (device != nullptr)
        for (int i = 0; i < device->GetNumDeviceScalar(); i++)
            controls.push_back(device->scalars[i].name);
    if (controls.empty())
        controls.push_back("AButtonA");
    StringToNumber lookup;
    lookup.Construct(ShSlotNames);
    std::vector<const char *> texts = LiveDefinitions();
    int index = 0;
    for (const char *text : texts) {
        for (int copy = 0; copy < 40; copy++, index++) {
            g_cases++;
            std::vector<char> buffer =
                Writable(copy == 0 ? std::string(text) : MutateDefinition(text, actions, controls, slots, true));
            InputConfigManager a, b;
            memset(&a, 0, sizeof(a));
            a.frontEndSlots = &lookup;
            b = a;
            InputConfig configA, configB;
            memset(&configA, 0x5a, sizeof(configA));
            configB = configA;
            Run(true, [&] { Orig_ParseFrontEnd(&a, 0, &configA, buffer.data()); });
            Run(false, [&] { b.ParseDefFileForFrontEnd(&configB, buffer.data()); });
            CheckBytes("InputConfigManager::ParseDefFileForFrontEnd", index, &configA, &configB, sizeof(InputConfig));
        }
    }
    lookup.Destruct();
}

void TestPreload() {
    InputToAction *live = IOModule::GetIOModule()->inputActionMappings[ShLaunch.controllerPort];
    if (live == nullptr) {
        printf("[input] no mapping for the active pad: InitAndPreload not tested\n");
        return;
    }
    LaunchChoices saved = SaveChoices();
    for (int round = 0; round < 4; round++) {
        g_cases++;
        LaunchChoices start = saved;
        if (round > 0)
            start = {int32_t(Random() % 4), int32_t(Random() % 11), int32_t(Random() % 3)};
        InputConfigManager a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        MappingSnapshot mappingA, mappingB;
        LoadChoices(start);
        Run(true, [&] { Orig_InitAndPreload(&a, 0); });
        LaunchChoices afterA = SaveChoices();
        Snapshot(live, &mappingA);
        LoadChoices(start);
        Run(false, [&] { b.InitAndPreload(); });
        LaunchChoices afterB = SaveChoices();
        Snapshot(live, &mappingB);
        CompareManagers("InputConfigManager::InitAndPreload", round, &a, &b, true);
        CompareChoices("InputConfigManager::InitAndPreload page", round, afterA, afterB);
        CompareSnapshots("InputConfigManager::InitAndPreload mapping", round, &mappingA, &mappingB);
        Run(true, [&] { Orig_Shutdown(&a, 0); });
        Run(false, [&] { b.Shutdown(); });
        CompareManagers("InputConfigManager::Shutdown", round, &a, &b, false);
    }
    LoadChoices(saved);
    InputConfigManager *m = InputConfigManager::Get();
    m->SetConfig(m->currentType, -1);
}

void TestLiveManager() {
    InputConfigManager *m = InputConfigManager::Get();
    InputConfigManager *returned = nullptr;
    Run(true, [&] { returned = Orig_ConfigGet(); });
    g_cases++;
    CheckInt("InputConfigManager::Get", 0, (long long)(uintptr_t)returned, (long long)(uintptr_t)m);
    if (!m->initialised) {
        printf("[input] the configurations are not loaded: the live manager not tested\n");
        return;
    }
    InputToAction *live = IOModule::GetIOModule()->inputActionMappings[ShLaunch.controllerPort];
    InputConfigManager saved = *m;
    LaunchChoices savedChoices = SaveChoices();

    // the getters
    for (int type = 0; type < 3; type++) {
        g_cases++;
        int a = 0, b = 0;
        if (type < 2) {
            Run(true, [&] { a = Orig_GetNumConfigs(m, 0, type); });
            Run(false, [&] { b = m->GetNumConfigs(type); });
            CheckInt("InputConfigManager::GetNumConfigs", type, a, b);
        }
        Run(true, [&] { a = Orig_GetCurrentConfig(m, 0, type); });
        Run(false, [&] { b = m->GetCurrentConfig(type); });
        CheckInt("InputConfigManager::GetCurrentConfig", type, a, b);
    }
    for (int value = 0; value < 2; value++) {
        g_cases++;
        uint32_t a = 0;
        bool b = false;
        m->inverted = value != 0;
        Run(true, [&] { a = Orig_IsInverted(m, 0); });
        Run(false, [&] { b = m->IsInverted(); });
        CheckInt("InputConfigManager::IsInverted", value, a & 0xff, b);

        LaunchChoices afterA, afterB;
        InputConfigManager copyA = saved, copyB = saved;
        Run(true, [&] { Orig_SetInverted(&copyA, 0, value != 0); });
        afterA = SaveChoices();
        LoadChoices(savedChoices);
        Run(false, [&] { copyB.SetInverted(value != 0); });
        afterB = SaveChoices();
        LoadChoices(savedChoices);
        CheckInt("InputConfigManager::SetInverted", value, copyA.inverted, copyB.inverted);
        CompareChoices("InputConfigManager::SetInverted page", value, afterA, afterB);
    }
    *m = saved;

    // the labels, on each track name the function tests
    static const char *const kTracks[] = {"paris_mis01", "snow1a_mis3", "uw_mis11", "snow2a_mis4", "junglea_mis13a"};
    char savedMission[16];
    memcpy(savedMission, ShLaunch.missionName, sizeof(savedMission));
    for (int track = -1; track < 5; track++) {
        if (track >= 0) {
            memset(ShLaunch.missionName, 0, sizeof(ShLaunch.missionName));
            strcpy(ShLaunch.missionName, kTracks[track]);
        }
        for (int type = 0; type < 2; type++) {
            for (int config = 0; config < m->numConfigs[type]; config++) {
                for (int slot = 0; slot < 14; slot++) {
                    g_cases++;
                    int a = 0, b = 0;
                    Run(true, [&] { a = Orig_GetLocaleID(m, 0, type, config, slot); });
                    Run(false, [&] { b = m->GetLocaleID(type, config, slot); });
                    CheckInt("InputConfigManager::GetLocaleID", (track + 1) * 1000 + type * 100 + config * 14 + slot, a,
                             b);
                }
            }
        }
        // every special id, at every configuration index up to 9
        static const int kIds[] = {0x590, 0xa94, 0xa98, 0xa99, 0xa9a, 0xa9f, 0xaa3, 0xaa1, 0x6ad, -1, 0};
        InputConfig scratch[10];
        memset(scratch, 0, sizeof(scratch));
        InputConfigManager s;
        memset(&s, 0, sizeof(s));
        s.configs[0] = scratch;
        s.configs[1] = scratch;
        for (int id = 0; id < int(sizeof(kIds) / sizeof(kIds[0])); id++) {
            for (int config = 0; config < 10; config++) {
                scratch[config].localeIDs[3] = kIds[id];
                for (int type = 0; type < 2; type++) {
                    g_cases++;
                    int a = 0, b = 0;
                    Run(true, [&] { a = Orig_GetLocaleID(&s, 0, type, config, 3); });
                    Run(false, [&] { b = s.GetLocaleID(type, config, 3); });
                    int index = (track + 1) * 1000 + id * 20 + config * 2 + type;
                    CheckInt("InputConfigManager::GetLocaleID special", index, a, b);
                }
            }
        }
    }
    memcpy(ShLaunch.missionName, savedMission, sizeof(savedMission));

    // SetConfig over every type and configuration, and -1
    if (live != nullptr) {
        int index = 0;
        for (int type = 0; type < 2; type++) {
            for (int config = -1; config < saved.numConfigs[type]; config++, index++) {
                g_cases++;
                MappingSnapshot mappingA, mappingB;
                *m = saved;
                LoadChoices(savedChoices);
                Run(true, [&] { Orig_SetConfig(m, 0, type, config); });
                InputConfigManager afterA = *m;
                LaunchChoices choicesA = SaveChoices();
                Snapshot(live, &mappingA);
                *m = saved;
                LoadChoices(savedChoices);
                Run(false, [&] { m->SetConfig(type, config); });
                InputConfigManager afterB = *m;
                LaunchChoices choicesB = SaveChoices();
                Snapshot(live, &mappingB);
                CheckBytes("InputConfigManager::SetConfig", index, &afterA, &afterB, sizeof(InputConfigManager));
                CompareChoices("InputConfigManager::SetConfig page", index, choicesA, choicesB);
                CompareSnapshots("InputConfigManager::SetConfig mapping", index, &mappingA, &mappingB);
            }
        }
        *m = saved;
        LoadChoices(savedChoices);
        m->SetConfig(m->currentType, -1);
    }
    *m = saved;
    LoadChoices(savedChoices);
}

}  // namespace

void InputShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_INPUTSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    TestTextHelpers();
    TestQueueVector();
    TestTables();
    TestMappings();
    TestMasterFile();
    TestFrontEndLabels();
    TestPreload();
    TestLiveManager();
    TestFeedback();

    printf("[input] input layer shadow: %d cases, %d checks, %d differ%s\n", g_cases, g_checks, g_differ,
           g_faults ? " (faults)" : "");
    if (g_faults)
        printf("[input]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
