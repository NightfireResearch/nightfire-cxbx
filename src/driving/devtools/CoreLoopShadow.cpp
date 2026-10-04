#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "CoreLoopShadow.h"
#include "FpControl.h"

#include "../Scheduler.hpp"
#include "../engine/GameLoop.h"
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../data/StdStreams.h"
#include "../platform/RealMath.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_CORELOOPSHADOW=1, at injection time on the loader's thread before the game runs: CORE_B's
// deterministic code against the originals (swapped back in for the calls, common/xbeOriginal.h), on the same
// inputs, compared byte for byte.
//
//   - Schedules: random scripts on synthetic schedules of 1, 2, 4 and 8 buckets - construct, add tasks, run
//     buckets directly and through the three Process overrides, remove tasks, remove them all, delete - with the
//     event table's first ten entries pointed at recording callbacks (some of which add or remove tasks while
//     their bucket runs). The game's allocator entry points (pool alloc/free, operator new/delete/new[]), and
//     our UMemory functions behind them that the ports call directly, are five-byte jumps to a bump arena for
//     the duration, reset to the same state before each run, so the two runs'
//     objects sit at the same addresses: the arena's bytes, the allocation log (sizes, names, order, what was
//     freed) and the callbacks' log must match exactly. Scheduler::Init / Reset / ResetTime / Shutdown likewise,
//     which takes in the scheduler's constructor and destructor and the vector's push_back and _Insert_n.
//   - SimRandom: construct, reset and generate from random states and seeds.
//   - Noise: noise_init and Noise::Init (the C runtime's rand jumped to a resettable fake, the platform generator
//     reseeded the same for both runs), then Noise1 on random, special and huge inputs over the real tables and
//     over random raw tables - doubles compared bit for bit (both-NaN counted as equal: payloads are the x87's).
//   - MissionNumToString on every mission number, difficulty and hand-over flag, the whole launch page compared.
//   - OptionParser and GetFullString on generated tuning-file texts (keys mid-line, at line starts after \n and
//     \r\n, values with every separator and trailing ']' / ')'), the key copies' allocations included - the
//     original leaks one when a key matches mid-line.
//   - std::string's assign (GameStd::String::Assign) on strings of 0-40 characters; the bit set's AndNotFrom.
//
// Everything the tests touch is saved first and put back: the noise tables and their flag, the generator states,
// the launch page, the scheduler pointer, the event table entries and the patched code bytes.
//
// One mutation this catches: Schedule::RunTasks comparing `spacing` with `priority` as '<=' runs extra tasks,
// which shows in the callbacks' log; Noise1 computing the smoothing weight in float changes its low bits.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals (called inside a window with the package's entry bytes put back)

const uint32_t kPackageLo = 0x000596a0, kPackageHi = 0x0005cd60;

typedef Schedule *(__fastcall *ScheduleConstructFn)(Schedule *, int, int);
typedef void (__fastcall *AddTaskFn)(Schedule *, int, int, uint32_t, unsigned, unsigned, int, int);
typedef bool (__fastcall *RemoveTaskFn)(Schedule *, int, uint32_t);
typedef void (__fastcall *ScheduleVoidFn)(Schedule *, int);
typedef void (__fastcall *RunTasksFn)(Schedule *, int, int, unsigned);
typedef Schedule *(__fastcall *ScheduleDeleteFn)(Schedule *, int, unsigned);
typedef void (*StaticFn)(void);

struct ScheduleOps {
    ScheduleConstructFn construct;
    AddTaskFn addTask;
    RemoveTaskFn removeTask;
    ScheduleVoidFn removeAll;
    RunTasksFn runTasks;
    RunTasksFn process[3];
    ScheduleDeleteFn remove[2];
    StaticFn schedulerInit, schedulerShutdown, schedulerReset;
};

const ScheduleOps kOriginalOps = {
    (ScheduleConstructFn)0x0005be00, (AddTaskFn)0x0005c490, (RemoveTaskFn)0x0005bc70, (ScheduleVoidFn)0x0005bec0,
    (RunTasksFn)0x0005bce0, { (RunTasksFn)0x0005bd70, (RunTasksFn)0x0005bd80, (RunTasksFn)0x0005bd90 },
    { (ScheduleDeleteFn)0x0005bfc0, (ScheduleDeleteFn)0x0005c110 },
    (StaticFn)0x0005c740, (StaticFn)0x0005c460, (StaticFn)0x0005ba40,
};

Schedule *__fastcall PortConstruct(Schedule *s, int, int buckets) { return s->Construct(buckets); }
void __fastcall PortAddTask(Schedule *s, int, int event, uint32_t data, unsigned spacing, unsigned unused, int delay,
                            int repeats) {
    s->AddTask(event, data, (unsigned short)spacing, unused != 0, delay, repeats);
}
bool __fastcall PortRemoveTask(Schedule *s, int, uint32_t task) { return s->RemoveTask(task); }
void __fastcall PortRemoveAll(Schedule *s, int) { s->RemoveAllTasks(); }
void __fastcall PortRunTasks(Schedule *s, int, int bucket, unsigned priority) {
    s->RunTasks(bucket, (unsigned short)priority);
}
void __fastcall PortProcess0(Schedule *s, int, int tick, unsigned p) { s->ProcessFirstBucket(tick, (unsigned short)p); }
void __fastcall PortProcess1(Schedule *s, int, int tick, unsigned p) { s->ProcessHalfRate(tick, (unsigned short)p); }
void __fastcall PortProcess2(Schedule *s, int, int tick, unsigned p) { s->ProcessQuarterRate(tick, (unsigned short)p); }
Schedule *__fastcall PortDelete(Schedule *s, int, unsigned flags) { return s->Delete(flags); }
Schedule *__fastcall PortDeleteDerived(Schedule *s, int, unsigned flags) { return s->DeleteDerived(flags); }

const ScheduleOps kPortOps = {
    PortConstruct, PortAddTask, PortRemoveTask, PortRemoveAll, PortRunTasks,
    { PortProcess0, PortProcess1, PortProcess2 }, { PortDelete, PortDeleteDerived },
    Scheduler::Init, Scheduler::Shutdown, Scheduler::Reset,
};

typedef SimRandom *(__fastcall *SimConstructFn)(SimRandom *, int);
typedef uint32_t (__fastcall *SimGenerateFn)(SimRandom *, int);
typedef void (__fastcall *SimResetFn)(SimRandom *, int);
typedef double (*Noise1Fn)(float);
typedef void (__fastcall *AndNotFn)(BitSet192 *, int, const BitSet192 *);
typedef OptionParser *(__fastcall *OptionConstructFn)(OptionParser *, int, const char *, const char *);
typedef bool (__fastcall *GetFullStringFn)(OptionParser *, int, char *);
typedef GameStd::String *(__fastcall *AssignFn)(GameStd::String *, int, const char *);

#define Orig_SimConstruct ((SimConstructFn)0x0005cc80)
#define Orig_SimGenerate ((SimGenerateFn)0x0005cc90)
#define Orig_SimReset ((SimResetFn)0x0005ccb0)
#define Orig_Noise1 ((Noise1Fn)0x0005ca80)
#define Orig_noise_init ((StaticFn)0x0005cb50)
#define Orig_NoiseInit ((StaticFn)0x0005cbb0)
#define Orig_AndNot ((AndNotFn)0x0005cd00)
#define Orig_MissionNumToString ((StaticFn)0x000596a0)
#define Orig_OptionConstruct ((OptionConstructFn)0x0005b870)
#define Orig_GetFullString ((GetFullStringFn)0x0005b9c0)
#define Orig_Assign ((AssignFn)0x0005b0a0)

// ---- the game's state the tests touch

#define fgScheduler (*(Scheduler **)0x001e520c)
#define EventTable ((TaskCallback *)0x0018bf38)
#define LaunchBytes ((uint8_t *)0x00243b90)
const uint32_t kNoiseLo = 0x001e5220, kNoiseHi = 0x001e7a5c;     // both tables, the permutation, the flag
#define NoiseGradient ((float *)0x001e5628)
#define NoisePermutation ((int16_t *)0x001e5a58)
#define NoiseTableMade I32_AT(0x001e7a58)
#define NoiseRandomWords ((uint32_t *)0x001c34f8)                // state, multiplier, count
#define RealRandomWords ((uint32_t *)0x001d187c)                 // the platform generator's six words

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0, g_notRestored = 0;

void Differ(const char *what, int index, uint32_t a, uint32_t b) {
    g_differ++;
    if (g_details++ < 10)
        printf("[coreloop]   %s #%d: original %08x, port %08x\n", what, index, a, b);
}

void Check(const char *what, int index, uint32_t a, uint32_t b) {
    g_checks++;
    if (a != b)
        Differ(what, index, a, b);
}

uint32_t g_random = 0x2468ace1;
uint32_t Random(uint32_t below) {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return below == 0 ? g_random : g_random % below;
}

// ---- the arena and the fakes standing in for the game's allocator and rand

const uint32_t kArenaBytes = 1 << 18;
alignas(16) uint8_t g_arena[kArenaBytes];
uint32_t g_used;
const int kLogWords = 1 << 16;
uint32_t g_log[kLogWords];
int g_logCount;

void Log(uint32_t a, uint32_t b) {
    if (g_logCount + 2 <= kLogWords) {
        g_log[g_logCount++] = a;
        g_log[g_logCount++] = b;
    }
}

uint32_t Offset(const void *p) {
    uintptr_t at = (uintptr_t)p;
    if (at >= (uintptr_t)g_arena && at < (uintptr_t)g_arena + kArenaBytes)
        return (uint32_t)(at - (uintptr_t)g_arena);
    return (uint32_t)at;
}

void *Bump(unsigned size) {
    unsigned rounded = (size + 15) & ~15u;
    if (g_used + rounded > kArenaBytes)
        return NULL;
    void *p = g_arena + g_used;
    g_used += rounded;
    return p;
}

// The task nodes handed out, so that Take can blank the two padding fields of their records: AddTask copies its
// record off the stack, and the original never writes those bytes - stack garbage there, zeros in the port.
const int kMaxNodes = 8192;
uint32_t g_nodes[kMaxNodes];
int g_nodeCount;

void *FakeFastAlloc(unsigned size, const char *name) {
    Log(1, size);
    Log(2, name != NULL ? (uint8_t)name[0] : 0);
    void *p = Bump(size);
    if (p != NULL && size == sizeof(TaskNode) && g_nodeCount < kMaxNodes)
        g_nodes[g_nodeCount++] = Offset(p);
    return p;
}
void FakeFastFree(void *p, unsigned size) {
    Log(3, Offset(p));
    Log(4, size);
}
void *FakeNew(unsigned size) {
    Log(5, size);
    return Bump(size);
}
void FakeDelete(void *p) {
    Log(6, Offset(p));
}
void *FakeVecNew(unsigned size) {
    Log(7, size);
    return Bump(size);
}

uint32_t g_rand;
int FakeRand() {
    g_rand = g_rand * 0x343fd + 0x269ec3;
    return (g_rand >> 16) & 0x7fff;
}

void ResetWorkspace() {
    memset(g_arena, 0xcd, kArenaBytes);
    g_used = 0;
    g_logCount = 0;
    g_nodeCount = 0;
}

struct Snapshot {
    uint32_t used;
    int logCount;
    uint8_t arena[kArenaBytes];
    uint32_t log[kLogWords];
};
Snapshot g_snap[2];

void Take(Snapshot *s) {
    for (int i = 0; i < g_nodeCount; i++) {
        TaskNode *node = (TaskNode *)(g_arena + g_nodes[i]);
        node->record.unknown09 = 0;
        node->record.unknown0e = 0;
    }
    s->used = g_used;
    s->logCount = g_logCount;
    memcpy(s->arena, g_arena, g_used);
    memcpy(s->log, g_log, g_logCount * sizeof(uint32_t));
}

// The two runs' arenas and logs; the first difference of each is reported.
void CompareSnapshots(const char *what, int index) {
    const Snapshot &a = g_snap[0], &b = g_snap[1];
    Check(what, index, a.used, b.used);
    Check(what, index, a.logCount, b.logCount);
    uint32_t used = a.used < b.used ? a.used : b.used;
    g_checks++;
    for (uint32_t i = 0; i < used; i++)
        if (a.arena[i] != b.arena[i]) {
            Differ(what, index, 0xa0000000 | i, (a.arena[i] << 8) | b.arena[i]);
            break;
        }
    int count = a.logCount < b.logCount ? a.logCount : b.logCount;
    g_checks++;
    for (int i = 0; i < count; i++)
        if (a.log[i] != b.log[i]) {
            Differ(what, index, 0xb0000000 | i, a.log[i] ^ b.log[i]);
            break;
        }
}

// ---- five-byte jumps over the game's entry points the fakes stand in for

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[16];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
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

// The same jump over our C++ function too, for the callers that are ours and call it directly rather than through
// the game's address (which jumps to it): the port's allocations must reach the fakes as the original's do.
bool HookBoth(uint32_t at, const void *ours, const void *to) {
    HookInstall(at, to);
    bool on = g_hooks[g_hookCount - 1].on;
    if ((uint32_t)(uintptr_t)ours != at)
        HookInstall((uint32_t)(uintptr_t)ours, to);
    return on;
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

// The originals' window: every patched entry in the package put back.
struct OriginalsWindow {
    OriginalsWindow() {
        g_notRestored += XbeOriginal_RestoreRange(kPackageLo, kPackageHi, true) == 0;
        XbeOriginal_RestoreRange(0x001306d0, 0x00130780, true);
    }
    ~OriginalsWindow() {
        XbeOriginal_RestoreRange(kPackageLo, kPackageHi, false);
        XbeOriginal_RestoreRange(0x001306d0, 0x00130780, false);
    }
};

// A call that faults is counted, not fatal.
bool Guarded(void (*body)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        body(context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    body(context);
#endif
    return true;
}

// ---- schedules

enum OpKind { kAdd, kRun, kProcess, kRemove, kRemoveAll };
struct Op {
    int kind;
    int a, b, c, d, e;
    uint32_t data;
};
const int kMaxOps = 160;
struct Script {
    int buckets;
    int deleter;
    int count;
    Op ops[kMaxOps];
};

const ScheduleOps *g_ops;
Schedule *g_schedule;
int g_buckets;

// What a recording task does: logs its event and data; bit 8 of the data adds a task, bit 9 removes task number
// data >> 16 (only on 8-bucket schedules, where RemoveTask's scan of all eight lists is safe).
void TaskRan(int event, uint32_t data) {
    Log(0x10 + event, data);
    if (g_schedule == NULL)
        return;
    if (data & 0x100)
        g_ops->addTask(g_schedule, 0, (data >> 4) & 7, data & 0xff, (data >> 12) & 7, 1, 0, 1);
    if ((data & 0x200) && g_buckets == 8)
        Log(0x30, g_ops->removeTask(g_schedule, 0, data >> 16));
}

template <int E> void FakeTask(uint32_t data) {
    TaskRan(E, data);
}
const TaskCallback kFakeTasks[10] = { FakeTask<0>, FakeTask<1>, FakeTask<2>, FakeTask<3>, FakeTask<4>,
                                      FakeTask<5>, FakeTask<6>, FakeTask<7>, FakeTask<8>, FakeTask<9> };

void MakeScript(Script *s) {
    static const int bucketChoices[4] = { 1, 2, 4, 8 };
    s->buckets = bucketChoices[Random(4)];
    s->deleter = Random(2);
    s->count = 20 + Random(kMaxOps - 20);
    for (int i = 0; i < s->count; i++) {
        Op &op = s->ops[i];
        memset(&op, 0, sizeof(op));
        uint32_t r = Random(100);
        if (r < 45) {
            op.kind = kAdd;
            op.a = Random(10);
            op.data = Random(0) & 0x0fff00ff;
            if (Random(6) == 0)
                op.data |= 0x100;
            if (Random(6) == 0)
                op.data = (op.data & 0xffff) | 0x200 | ((1000 + Random(i + 4)) << 16);
            op.b = Random(8);           // spacing
            op.c = Random(4) == 0 ? Random(4) : 0;     // delay
            op.d = Random(3) == 0 ? Random(4) : 0;     // repeats
            op.e = Random(2);           // the unused flag
        } else if (r < 70) {
            op.kind = kRun;
            op.a = Random(s->buckets);
            op.b = Random(8);
        } else if (r < 88) {
            op.kind = kProcess;
            int highest = s->buckets >= 4 ? 3 : s->buckets >= 2 ? 2 : 1;
            op.a = Random(highest);
            op.b = (int)Random(0);
            op.c = Random(8);
        } else if (r < 97) {
            if (s->buckets == 8) {
                op.kind = kRemove;
                op.a = 1000 + Random(i + 4);
            } else {
                op.kind = kRun;   // RemoveTask's scan of all eight lists is only safe with eight
                op.a = Random(s->buckets);
                op.b = Random(8);
            }
        } else {
            op.kind = kRemoveAll;
        }
    }
}

const Script *g_script;

void RunScript(void *) {
    const Script *s = g_script;
    g_buckets = s->buckets;
    Schedule *schedule = (Schedule *)FakeNew(sizeof(Schedule));
    Log(0x40, Offset(g_ops->construct(schedule, 0, s->buckets)));
    g_schedule = schedule;
    for (int i = 0; i < s->count; i++) {
        const Op &op = s->ops[i];
        switch (op.kind) {
        case kAdd: g_ops->addTask(schedule, 0, op.a, op.data, op.b, op.e, op.c, op.d); break;
        case kRun: g_ops->runTasks(schedule, 0, op.a, op.b); break;
        case kProcess: g_ops->process[op.a](schedule, 0, op.b, op.c); break;
        case kRemove: Log(0x41, g_ops->removeTask(schedule, 0, op.a)); break;
        case kRemoveAll: g_ops->removeAll(schedule, 0); break;
        }
    }
    Log(0x42, Offset(g_ops->remove[s->deleter](schedule, 0, 1)));
    g_schedule = NULL;
}

void RunSchedulerLife(void *) {
    g_ops->schedulerInit();
    Scheduler *scheduler = fgScheduler;
    Log(0x50, Offset(scheduler));
    if (scheduler != NULL) {
        scheduler->timeScale = 3.0f;
        scheduler->oneTickPerRun = 1;
        g_ops->schedulerReset();
        Log(0x51, *(uint32_t *)&scheduler->timeScale);
        Log(0x52, scheduler->oneTickPerRun | (scheduler->cinematicModeSkipping << 8));
        // a task on each schedule, so the destructor has lists to free
        g_ops->addTask(scheduler->s_oncePerGameLoop, 0, 1, 2, 3, 1, 0, 0);
        g_ops->addTask(scheduler->s_quarterSimRate, 0, 4, 5, 6, 1, 0, 0);
    }
    g_ops->schedulerShutdown();
    Log(0x53, Offset(fgScheduler));
}

// Runs `body` for the original (inside the window) and then the port, each from a fresh workspace.
void RunBoth(void (*body)(void *), const char *what, int index) {
    for (int side = 0; side < 2; side++) {
        ResetWorkspace();
        g_ops = side == 0 ? &kOriginalOps : &kPortOps;
        if (side == 0) {
            OriginalsWindow window;
            Guarded(body, NULL);
        } else {
            Guarded(body, NULL);
        }
        Take(&g_snap[side]);
    }
    CompareSnapshots(what, index);
}

void TestSchedules() {
    TaskCallback savedEvents[10];
    DWORD old;
    if (!VirtualProtect(EventTable, sizeof(savedEvents), PAGE_READWRITE, &old)) {
        printf("[coreloop] could not unprotect the event table - schedules not tested\n");
        return;
    }
    memcpy(savedEvents, EventTable, sizeof(savedEvents));
    memcpy(EventTable, kFakeTasks, sizeof(kFakeTasks));

    static Script script;
    for (int i = 0; i < 400; i++) {
        MakeScript(&script);
        g_script = &script;
        g_cases++;
        RunBoth(RunScript, "schedule script", i);
    }

    Scheduler *savedScheduler = fgScheduler;
    fgScheduler = NULL;
    g_cases++;
    RunBoth(RunSchedulerLife, "scheduler init/reset/shutdown", 0);
    fgScheduler = savedScheduler;

    memcpy(EventTable, savedEvents, sizeof(savedEvents));
    VirtualProtect(EventTable, sizeof(savedEvents), old, &old);
}

// ---- SimRandom and the bit set

struct SimContext {
    SimRandom r;
    uint32_t results[24];
    bool construct;
    int side;
};

void RunSim(void *p) {
    SimContext *c = (SimContext *)p;
    if (c->construct)
        c->results[0] = Offset(c->side == 0 ? Orig_SimConstruct(&c->r, 0) : c->r.Construct()) - Offset(&c->r);
    if (c->side == 0)
        Orig_SimReset(&c->r, 0);
    else
        c->r.Reset();
    for (int i = 1; i < 24; i++)
        c->results[i] = c->side == 0 ? Orig_SimGenerate(&c->r, 0) : c->r.Generate();
}

void TestSimRandom() {
    for (int i = 0; i < 3000; i++) {
        SimContext c[2];
        memset(c, 0, sizeof(c));
        c[0].r.state = Random(0);
        c[0].r.lastProduct = Random(0);
        c[0].r.seed = Random(4) == 0 ? (uint32_t)i : Random(0);
        c[0].r.counter = Random(0);
        c[0].construct = Random(3) == 0;
        c[1] = c[0];
        c[0].side = 0;
        c[1].side = 1;
        {
            OriginalsWindow window;
            Guarded(RunSim, &c[0]);
        }
        Guarded(RunSim, &c[1]);
        g_cases++;
        for (int k = 0; k < 24; k++)
            Check("SimRandom result", i, c[0].results[k], c[1].results[k]);
        Check("SimRandom state", i, c[0].r.state, c[1].r.state);
        Check("SimRandom product", i, c[0].r.lastProduct, c[1].r.lastProduct);
        Check("SimRandom seed", i, c[0].r.seed, c[1].r.seed);
        Check("SimRandom counter", i, c[0].r.counter, c[1].r.counter);
    }

    for (int i = 0; i < 1000; i++) {
        BitSet192 a[2], other;
        for (int k = 0; k < 6; k++) {
            a[0].words[k] = Random(0);
            other.words[k] = Random(0);
        }
        a[1] = a[0];
        {
            OriginalsWindow window;
            Orig_AndNot(&a[0], 0, &other);
        }
        a[1].AndNotFrom(&other);
        g_cases++;
        for (int k = 0; k < 6; k++)
            Check("BitSet192::AndNotFrom", i, a[0].words[k], a[1].words[k]);
    }
}

// ---- Noise

uint8_t g_noiseSaved[kNoiseHi - kNoiseLo];
uint8_t g_noiseAfter[2][kNoiseHi - kNoiseLo];
uint32_t g_noiseWordsAfter[2][3 + 6];

void RunNoiseInitOriginal(void *) { Orig_NoiseInit(); }
void RunNoiseInitPort(void *) { Noise::Init(); }
void RunNoiseTableOriginal(void *) { Orig_noise_init(); }
void RunNoiseTablePort(void *) { noise_init(); }

float NoiseInput() {
    static const uint32_t specials[] = { 0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0x4f000000,
                                         0xcf000000, 0x461c4000, 0xc61c4000, 0x3f800000, 0x7f7fffff, 0x00000001 };
    uint32_t r = Random(100);
    float f;
    if (r < 50) {
        f = ((float)(int32_t)Random(0) / 2147483648.0f) * (float)(1 << Random(16));
    } else if (r < 70) {
        f = -10000.0f + (float)Random(20000) / 7.0f;
    } else if (r < 80) {
        uint32_t u = specials[Random(sizeof(specials) / 4)];
        memcpy(&f, &u, 4);
    } else {
        uint32_t u = Random(0);
        memcpy(&f, &u, 4);
    }
    return f;
}

struct Noise1Context {
    float x;
    double result;
    int side;
};

void RunNoise1(void *p) {
    Noise1Context *c = (Noise1Context *)p;
    c->result = c->side == 0 ? Orig_Noise1(c->x) : Noise::Noise1(c->x);
}

void CompareNoise1(const char *what, int count) {
    for (int i = 0; i < count; i++) {
        Noise1Context c[2];
        c[0].x = c[1].x = NoiseInput();
        c[0].side = 0;
        c[1].side = 1;
        c[0].result = c[1].result = 0.0;
        {
            OriginalsWindow window;
            Guarded(RunNoise1, &c[0]);
        }
        Guarded(RunNoise1, &c[1]);
        g_cases++;
        uint64_t a, b;
        memcpy(&a, &c[0].result, 8);
        memcpy(&b, &c[1].result, 8);
        bool bothNaN = c[0].result != c[0].result && c[1].result != c[1].result;
        g_checks++;
        if (a != b && !bothNaN)
            Differ(what, i, (uint32_t)(a >> 32) ^ (uint32_t)a, (uint32_t)(b >> 32) ^ (uint32_t)b);
    }
}

void TestNoise() {
    memcpy(g_noiseSaved, (void *)(uintptr_t)kNoiseLo, sizeof(g_noiseSaved));
    uint32_t noiseWords[3], realWords[6];
    memcpy(noiseWords, NoiseRandomWords, sizeof(noiseWords));
    memcpy(realWords, RealRandomWords, sizeof(realWords));
    HookInstall(0x00133ee0, (const void *)FakeRand);
    if (!g_hooks[g_hookCount - 1].on) {
        printf("[coreloop] could not jump the C runtime's rand to the fake - noise not tested\n");
        HooksRemove();
        return;
    }

    // noise_init alone, then Noise::Init with and without it, from several generator states
    for (int i = 0; i < 12; i++) {
        uint32_t randSeed = Random(0), realSeed = Random(0), state = Random(0x10000);
        uint32_t multiplier = i < 4 ? noiseWords[1] : Random(0);
        bool table = i % 3 != 0;
        for (int side = 0; side < 2; side++) {
            memcpy((void *)(uintptr_t)kNoiseLo, g_noiseSaved, sizeof(g_noiseSaved));
            NoiseTableMade = table ? 0 : 1;
            NoiseRandomWords[0] = state;
            NoiseRandomWords[1] = multiplier;
            NoiseRandomWords[2] = 77;
            seedrandom(realSeed);
            g_rand = randSeed;
            if (side == 0) {
                OriginalsWindow window;
                Guarded(i % 2 ? RunNoiseTableOriginal : RunNoiseInitOriginal, NULL);
            } else {
                Guarded(i % 2 ? RunNoiseTablePort : RunNoiseInitPort, NULL);
            }
            memcpy(g_noiseAfter[side], (void *)(uintptr_t)kNoiseLo, sizeof(g_noiseSaved));
            memcpy(g_noiseWordsAfter[side], NoiseRandomWords, 3 * 4);
            memcpy(g_noiseWordsAfter[side] + 3, RealRandomWords, 6 * 4);
            g_noiseWordsAfter[side][2] ^= g_rand;   // how far the fake rand went, folded in
        }
        g_cases++;
        g_checks++;
        for (uint32_t k = 0; k < sizeof(g_noiseSaved); k++)
            if (g_noiseAfter[0][k] != g_noiseAfter[1][k]) {
                Differ(i % 2 ? "noise_init table" : "Noise::Init tables", i, 0xa0000000 | k,
                       (g_noiseAfter[0][k] << 8) | g_noiseAfter[1][k]);
                break;
            }
        for (int k = 0; k < 9; k++)
            Check("noise generator words", i, g_noiseWordsAfter[0][k], g_noiseWordsAfter[1][k]);
    }

    // Noise1 over the real tables (the game's own generator state, as made by the port above)
    memcpy((void *)(uintptr_t)kNoiseLo, g_noiseSaved, sizeof(g_noiseSaved));
    NoiseTableMade = 0;
    memcpy(NoiseRandomWords, noiseWords, sizeof(noiseWords));
    seedrandom(0);
    Guarded(RunNoiseInitPort, NULL);
    CompareNoise1("Noise1 (real tables)", 20000);

    // and over random raw tables
    for (int t = 0; t < 8; t++) {
        for (int k = 0; k < 267; k++) {
            uint32_t u = t < 4 ? Random(0) : (Random(0) & 0x807fffff) | 0x3f000000;
            memcpy(&NoiseGradient[k], &u, 4);
        }
        for (int k = 0; k < 0x1000; k++)
            NoisePermutation[k] = (int16_t)Random(0);
        CompareNoise1("Noise1 (random tables)", 2000);
    }

    HooksRemove();
    memcpy((void *)(uintptr_t)kNoiseLo, g_noiseSaved, sizeof(g_noiseSaved));
    memcpy(NoiseRandomWords, noiseWords, sizeof(noiseWords));
    memcpy(RealRandomWords, realWords, sizeof(realWords));
}

// ---- MissionNumToString

uint8_t g_pageSaved[0xa4c], g_pageCase[0xa4c], g_pageAfter[0xa4c];

void RunMissionOriginal(void *) { Orig_MissionNumToString(); }
void RunMissionPort(void *) { MissionNumToString(); }

void TestMissions() {
    static const int missions[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x105, 0x7fffffff };
    static const int difficulties[] = { -1, 0, 1, 2, 3, 4, 5 };
    LaunchPage *page = (LaunchPage *)LaunchBytes;
    memcpy(g_pageSaved, LaunchBytes, sizeof(g_pageSaved));
    int index = 0;
    for (int handOver = 0; handOver < 2; handOver++)
        for (int mission : missions)
            for (int difficulty : difficulties)
                for (int flag = 0; flag < 2; flag++, index++) {
                    for (uint32_t k = 0; k < sizeof(g_pageCase); k++)
                        g_pageCase[k] = (uint8_t)Random(0);
                    memcpy(LaunchBytes, g_pageCase, sizeof(g_pageCase));
                    page->handOver = handOver;
                    page->missionNum = mission;
                    page->difficulty = difficulty;
                    page->unknown970 = flag ? (int)Random(0) | 1 : 0;
                    memcpy(g_pageCase, LaunchBytes, sizeof(g_pageCase));
                    {
                        OriginalsWindow window;
                        Guarded(RunMissionOriginal, NULL);
                    }
                    memcpy(g_pageAfter, LaunchBytes, sizeof(g_pageAfter));
                    memcpy(LaunchBytes, g_pageCase, sizeof(g_pageCase));
                    Guarded(RunMissionPort, NULL);
                    g_cases++;
                    g_checks++;
                    for (uint32_t k = 0; k < sizeof(g_pageAfter); k++)
                        if (g_pageAfter[k] != LaunchBytes[k]) {
                            Differ("MissionNumToString page", index, 0xa0000000 | k,
                                   (g_pageAfter[k] << 8) | LaunchBytes[k]);
                            break;
                        }
                }
    memcpy(LaunchBytes, g_pageSaved, sizeof(g_pageSaved));
}

// ---- OptionParser and std::string

char g_text[1024];
const char *g_key;

void RunOptionParser(void *) {
    const char *text = g_text + 1;
    OptionParser *parser = (OptionParser *)FakeNew(sizeof(OptionParser));
    memset(parser, 0x5a, sizeof(OptionParser));
    char *out = (char *)FakeNew(512);
    memset(out, 0xcc, 512);
    bool original = g_ops == &kOriginalOps;
    Log(0x60, Offset(original ? Orig_OptionConstruct(parser, 0, text, g_key) : parser->Construct(text, g_key)));
    Log(0x61, parser->value != NULL ? (uint32_t)(parser->value - text) : 0xffffffff);
    Log(0x62, (uint32_t)parser->length);
    if (parser->length >= 0 && parser->length < 500)
        Log(0x63, original ? Orig_GetFullString(parser, 0, out) : parser->GetFullString(out));
}

void MakeText(char *text, int size) {
    static const char *const keys[] = { "speed", "speedmax", "max", "grip", "a" };
    static const char *const separators[] = { " ", "=", " = ", "#", "(", "[", "  [", ":", "x", "=#(" };
    static const char *const values[] = { "12", "1.5]", "(3)", "4])", "]", ")", "", "0.25 # c", "[7]", "x)]" };
    static const char *const ends[] = { "\n", "\r\n", "\n\n", " ", "" };
    int lines = 1 + Random(6), used = 0;
    text[0] = 0;
    for (int i = 0; i < lines && used < size - 64; i++) {
        if (Random(5) == 0)
            used += snprintf(text + used, size - used, "%s", keys[Random(5)]);   // mid-line next time
        used += snprintf(text + used, size - used, "%s%s%s%s", keys[Random(5)], separators[Random(10)],
                         values[Random(10)], ends[Random(5)]);
    }
}

GameStd::String *g_string;
const char *g_assign;

void RunAssign(void *) {
    GameStd::String *s = (GameStd::String *)FakeNew(sizeof(GameStd::String));
    memset(s, 0, sizeof(GameStd::String));
    s->capacity = 15;
    bool original = g_ops == &kOriginalOps;
    Log(0x70, Offset(original ? Orig_Assign(s, 0, g_assign) : s->Assign(g_assign)));
    Log(0x71, s->size);
    Log(0x72, s->capacity);
}

void TestOptionsAndStrings() {
    static const char *const keys[] = { "speed", "speedmax", "max", "grip", "a", "zz" };
    for (int i = 0; i < 600; i++) {
        g_text[0] = 'Z';   // a byte before the text, for the original's read of value[-1]
        if (i == 0)
            strcpy(g_text + 1, "");
        else if (i == 1)
            strcpy(g_text + 1, "speed = 10\r\nmax 3]\n");
        else
            MakeText(g_text + 1, sizeof(g_text) - 1);
        g_key = keys[Random(6)];
        g_cases++;
        RunBoth(RunOptionParser, "OptionParser", i);
    }
    static char text[48];
    for (int length = 0; length <= 40; length++) {
        for (int k = 0; k < length; k++)
            text[k] = (char)('a' + (k + length) % 26);
        text[length] = 0;
        g_assign = text;
        g_cases++;
        RunBoth(RunAssign, "String::Assign", length);
    }
}

}  // namespace

void CoreLoopShadow_Run(void) {
    char flag[16] = "";
    if (!GetEnvironmentVariableA("NIGHTFIRE_CORELOOPSHADOW", flag, sizeof(flag)) || flag[0] == '0')
        return;

    unsigned x87, sse;
    FpControlGet(&x87, &sse);
    FpControlSetX87(0x027f);   // 53-bit precision, round to nearest, all masked: the game's
    FpControlSetSse(0x1f80);

    TestSimRandom();
    TestMissions();
    TestNoise();

    HookBoth(0x00114750, (const void *)&UMemory::FastAlloc, (const void *)FakeFastAlloc);
    HookBoth(0x001147d0, (const void *)&UMemory::FastFree, (const void *)FakeFastFree);
    HookBoth(0x001146a0, (const void *)&OperatorNew, (const void *)FakeNew);
    HookBoth(0x001146e0, (const void *)&OperatorDelete, (const void *)FakeDelete);
    HookBoth(0x00114710, (const void *)&OperatorNewArray, (const void *)FakeVecNew);
    bool hooked = true;
    for (int i = 0; i < g_hookCount; i++)
        hooked = hooked && g_hooks[i].on;
    if (hooked) {
        TestSchedules();
        TestOptionsAndStrings();
    } else {
        printf("[coreloop] could not jump the allocator to the arena - schedules, options, strings not tested\n");
    }
    HooksRemove();

    FpControlSetX87(x87);
    FpControlSetSse(sse);

    printf("[coreloop] scheduler, SimRandom, Noise, MissionNumToString, OptionParser: %d cases, %d checks, "
           "%d differ (%d faulted, %d windows without originals)\n",
           g_cases, g_checks, g_differ, g_faults, g_notRestored);
    fflush(stdout);
}
