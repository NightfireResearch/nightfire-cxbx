#include "drivinghelpers.h"

#include "Scheduler.hpp"
#include "EventManager.hpp"
#include "devtools/Teleport.h"
#include "engine/CoreFoundation.h"
#include "engine/UMemory.hpp"
#include "physics/RigidBodyBasics.h"   // PointerVectorDeallocate
#include "platform/RealSystem.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------------------------------------------
// The scheduler, its schedules and their task lists, the clock, and the event number tables (docs/driving-engine-
// plan.md, section 2). Every allocation goes through the game's allocator, in the original's sizes and order: the
// schedules and the scheduler from operator new, the task lists and their nodes from the fixed-size pools.
// ---------------------------------------------------------------------------------------------------------------

// The game's clock: ticks at the video refresh rate (50 Hz PAL), from the timer callback - see
// src/driving/platform/XboxTimer.cpp and docs/driving-engine-plan.md, section 2.
#define Clock U32_AT(0x001e5204)
#define RealClock_Div2Ticks U32_AT(0x001e5208)     // every second tick
#define fgScheduler (*(Scheduler **)0x001e520c)

// The event tables, the game's: event number to task callback, and to CARP resolver.
#define fgMakeEventCallbacks ((TaskCallback *)0x0018bf38)
#define fgResolveEventCallbacks ((void **)0x001b6660)

// The schedules' vtables, the game's (each: deleting destructor, Process).
#define Schedule_vtable ((vtable_Schedule *)0x0018ea0c)
#define Schedule_SimRate_vtable ((vtable_Schedule *)0x0018ea14)
#define Schedule_HalfSimRate_vtable ((vtable_Schedule *)0x0018ea1c)
#define Schedule_QuarterSimRate_vtable ((vtable_Schedule *)0x0018ea24)
#define Schedule_OncePerGameLoop_vtable ((vtable_Schedule *)0x0018ea2c)

// The STL algorithms the vector's _Insert_n calls, the game's copies for Schedule * (the ones popping their
// arguments are members that do not use `this`).
#define Uninitialized_Copy ((Schedule **(*)(Schedule **, Schedule **, Schedule **))0x000bfeb0)
#define Uninitialized_Fill_N ((void (*)(Schedule **, unsigned, Schedule *const *))0x000b2ec0)
#define Vector_Ucopy ((Schedule **(__stdcall *)(Schedule **, Schedule **, Schedule **))0x000c1230)
#define Vector_Ufill ((Schedule **(__stdcall *)(Schedule **, unsigned, Schedule *const *))0x000c1290)
#define Copy_Backward ((Schedule ***(*)(Schedule ***, Schedule **, Schedule **, Schedule **))0x000b2e40)
#define Fill ((void (*)(Schedule **, Schedule **, Schedule *const *))0x0004f3e0)
#define Vector_Destruct ((void (__fastcall *)(Vector_Schedule *, int))0x0004f400)

// Task numbers start here, and AddTask passes over a bucket this full.
static const uint32_t kFirstTaskNumber = 1000;
static const uint32_t kFullBucket = 999999;
static const int kMaxBuckets = 8;

// ---- the clock

// FUNC_AT(0x0005b9f0)
void RealClock_InterruptHandler() {
    Clock = Clock + 1;
    if ((Clock & 1) == 0)
        RealClock_Div2Ticks = RealClock_Div2Ticks + 1;
}

// FUNC_AT(0x0005ba10)
void RealClock_Init() {
    RealClock_Div2Ticks = 0;
    Clock = 0;
    TIMER_addtask(RealClock_InterruptHandler);
}

// FUNC_AT(0x0005ba30)
void RealClock_CleanUp() {
    TIMER_removetask(RealClock_InterruptHandler);
}

// ---- the event tables

// FUNC_AT(0x0005a5e0)
TaskCallback RegisterEvent::LookupEvent(int event) {
    return fgMakeEventCallbacks[event];
}

// FUNC_AT(0x0005a5f0)
void* RegisterEvent::ResolveEvent(int event) {
    return fgResolveEventCallbacks[event];
}

// ---- a task list: the game's std::list<TaskRecord> members

// FUNC_AT(0x0005bbe0)
TaskNode** TaskRecord_LListEntry::Erase(TaskNode **result, TaskNode *first, TaskNode *last) {
    while (first != last) {
        TaskNode *node = first;
        first = first->next;
        if (node != head) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            UMemory::FastFree(node, sizeof(TaskNode));
            size--;
        }
    }
    *result = first;
    return result;
}

// FUNC_AT(0x0005bc30)
TaskNode* TaskRecord_LListEntry::BuyNode(TaskNode *next, TaskNode *prev, const TaskRecord *record) {
    TaskNode *node = (TaskNode *)UMemory::FastAlloc(sizeof(TaskNode), "STL");
    if (node != NULL) {
        node->next = next;
        node->prev = prev;
        node->record = *record;
    }
    return node;
}

// FUNC_AT(0x0005bda0)
TaskNode* TaskRecord_LListEntry::BuyHead() {
    TaskNode *node = (TaskNode *)UMemory::FastAlloc(sizeof(TaskNode), "STL");
    // The original tests the address of `prev` rather than the block, so a null block would have it write to
    // address 4; the pools never answer null.
    if (node != NULL) {
        node->next = node;
        node->prev = node;
    }
    return node;
}

// FUNC_AT(0x0005bdc0)
void TaskRecord_LListEntry::Destruct() {
    TaskNode *erased;
    Erase(&erased, head != NULL ? head->next : NULL, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(TaskNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x0005bfe0)
void TaskRecord_LListEntry::IncSize(uint32_t count) {
    if (0x0ccccccc - size < count) {
        CORE_UNTESTED("std::list<TaskRecord>::_Incsize past max_size");
        ThrowLengthError("list<T> too long");
    }
    size += count;
}

// A new, empty task list, from the pools - as the constructor and RemoveAllTasks make them.
static TaskList *NewTaskList() {
    TaskList *list = (TaskList *)UMemory::FastAlloc(sizeof(TaskList), "TaskList");
    if (list == NULL)
        return NULL;
    list->head = TaskList::BuyHead();
    list->size = 0;
    return list;
}

// ---- Schedule

// FUNC_AT(0x0005be00)
Schedule* Schedule::Construct(int buckets) {
    next = (uint32_t *)(intptr_t)buckets;
    vtable = Schedule_vtable;
    numTasks = kFirstTaskNumber;
    for (int i = 0; i < kMaxBuckets; i++)
        listOfTasks[i] = NULL;
    for (int i = 0; i < BucketCount(); i++)
        listOfTasks[i] = NewTaskList();
    return this;
}

// FUNC_AT(0x0005bf80)
void Schedule::Destruct() {
    vtable = Schedule_vtable;
    for (int i = 0; i < BucketCount(); i++) {
        TaskList *list = listOfTasks[i];
        if (list != NULL) {
            list->Destruct();
            UMemory::FastFree(list, sizeof(TaskList));
        }
    }
}

// FUNC_AT(0x0005c130)
void Schedule::DestructDerived() {
    Destruct();
}

// FUNC_AT(0x0005bfc0)
Schedule* Schedule::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x0005c110)
Schedule* Schedule::DeleteDerived(unsigned flags) {
    DestructDerived();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// The emptiest bucket gets the task (the first of equals). Should every bucket hold 999999 tasks or more, the
// original reads the slot before listOfTasks - never the case.
// FUNC_AT(0x0005c490)
void Schedule::AddTask(int eventType, uint32_t staticData, unsigned short spacing, bool unused, int delayTicks,
                       int repeats) {
    (void)unused;
    int emptiest = -1;
    uint32_t fewest = kFullBucket;
    for (int i = 0; i < BucketCount(); i++) {
        if (listOfTasks[i]->size < fewest) {
            fewest = listOfTasks[i]->size;
            emptiest = i;
        }
    }
    TaskList *list = listOfTasks[emptiest];
    TaskNode *head = list->head;

    TaskRecord record = {};     // the two padding fields were stack garbage in the original
    record.eventType = eventType;
    record.staticData = staticData;
    record.spacing = (uint8_t)spacing;
    record.delayTicks = (uint16_t)delayTicks;
    record.numTimesToFire = (uint16_t)repeats;
    record.taskNum = numTasks;

    // push_back: a node before the head
    TaskNode *node = list->BuyNode(head, head->prev, &record);
    list->IncSize(1);
    head->prev = node;
    node->prev->next = node;
    numTasks++;
}

// Every bucket of the eight is searched, in use or not: a schedule with fewer buckets than the task's would read
// through a null list, as the original does. Only the task's own bucket is reached in practice.
// FUNC_AT(0x0005bc70)
bool Schedule::RemoveTask(uint32_t taskNum) {
    for (int i = 0; i < kMaxBuckets; i++) {
        TaskNode *node = listOfTasks[i]->head != NULL ? listOfTasks[i]->head->next : NULL;
        for (; node != listOfTasks[i]->head; node = node->next) {
            if (node->record.taskNum == taskNum) {
                TaskList *list = listOfTasks[i];
                if (node != list->head) {
                    node->prev->next = node->next;
                    node->next->prev = node->prev;
                    UMemory::FastFree(node, sizeof(TaskNode));
                    list->size--;
                }
                return true;
            }
        }
    }
    return false;
}

// FUNC_AT(0x0005bec0)
void Schedule::RemoveAllTasks() {
    for (int i = 0; i < kMaxBuckets; i++) {
        TaskList *list = listOfTasks[i];
        if (list != NULL) {
            TaskNode *erased;
            list->Erase(&erased, list->head != NULL ? list->head->next : NULL, list->head);
            if (list->head != NULL)
                UMemory::FastFree(list->head, sizeof(TaskNode));
            list->head = NULL;
            list->size = 0;
            UMemory::FastFree(list, sizeof(TaskList));
        }
        listOfTasks[i] = NewTaskList();
    }
}

// A task's callback may add tasks to the bucket being run, so the list's head is read again at every step. A
// task whose last run this was is removed after it ran, the next node taken first.
// FUNC_AT(0x0005bce0)
void Schedule::RunTasks(int bucket, unsigned short priority) {
    TaskList *list = listOfTasks[bucket];
    TaskNode *node = list->head != NULL ? list->head->next : NULL;
    while (node != listOfTasks[bucket]->head) {
        if (node->record.spacing == priority) {
            if (node->record.delayTicks != 0) {
                node->record.delayTicks--;
            } else {
                RegisterEvent::LookupEvent(node->record.eventType)(node->record.staticData);
                if (node->record.numTimesToFire != 0 && --node->record.numTimesToFire == 0) {
                    TaskNode *next = node->next;
                    RemoveTask(node->record.taskNum);
                    node = next;
                    continue;
                }
            }
        }
        node = node->next;
    }
}

// FUNC_AT(0x0005bd70)
void Schedule::ProcessFirstBucket(int tick, unsigned short priority) {
    (void)tick;
    RunTasks(0, priority);
}

// FUNC_AT(0x0005bd80)
void Schedule::ProcessHalfRate(int tick, unsigned short priority) {
    RunTasks(tick & 1, priority);
}

// FUNC_AT(0x0005bd90)
void Schedule::ProcessQuarterRate(int tick, unsigned short priority) {
    RunTasks(tick & 3, priority);
}

// ---- the list of simulation schedules: the game's std::vector<Schedule *> members

// FUNC_AT(0x0005c540)
void Vector_Schedule::Insert(Schedule *const *schedule) {
    unsigned size = Size();
    if (first != NULL && size < unsigned(end - first)) {
        Schedule **at = last;
        Uninitialized_Fill_N(at, 1, schedule);
        last = at + 1;
        return;
    }
    InsertN(last, 1, schedule);
}

// Only push_back calls it, and only when the storage is full, so the in-place branches are never reached; the
// growing one always inserts at the end.
// FUNC_AT(0x0005c1a0)
void Vector_Schedule::InsertN(Schedule **where, unsigned count, Schedule *const *value) {
    Schedule *copy = *value;
    unsigned capacity = Capacity();
    if (count == 0)
        return;
    if (0x3fffffff - Size() < count) {
        CORE_UNTESTED("std::vector<Schedule *>::_Insert_n past max_size");
        Xlen();
        return;
    }
    if (capacity < Size() + count) {
        capacity = 0x3fffffff - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (capacity < Size() + count)
            capacity = Size() + count;
        Schedule **block = (Schedule **)UMemory::FastAlloc(capacity * sizeof(Schedule *), "STL");
        Schedule **gap = Uninitialized_Copy(first, where, block);
        Uninitialized_Fill_N(gap, count, &copy);
        Uninitialized_Copy(where, last, gap + count);
        unsigned size = Size() + count;
        if (first != NULL)
            PointerVectorDeallocate(first, unsigned(end - first));
        end = block + capacity;
        last = block + size;
        first = block;
        return;
    }
    CORE_UNTESTED("std::vector<Schedule *>::_Insert_n in place");
    if (unsigned(last - where) < count) {
        Vector_Ucopy(where, last, where + count);
        Vector_Ufill(last, count - unsigned(last - where), &copy);
        last += count;
        Fill(where, last - count, &copy);
    } else {
        Schedule **oldLast = last;
        last = Vector_Ucopy(oldLast - count, oldLast, oldLast);
        Schedule **moved;
        Copy_Backward(&moved, where, oldLast - count, oldLast);
        Fill(where, where + count, &copy);
    }
}

// FUNC_AT(0x0005c090)
void Vector_Schedule::Xlen() {
    ThrowLengthError("vector<T> too long");
}

// ---- Scheduler

// FUNC_AT(0x0005c5b0)
Scheduler* Scheduler::Construct() {
    s_oncePerGameLoop = NULL;
    s_SimRate = NULL;
    s_halfSimRate = NULL;
    s_quarterSimRate = NULL;
    lastTickCount = (int32_t)Clock;
    oneTickPerRun = 0;
    cinematicModeSkipping = 0;
    timeScale = 1.0f;

    Vector_Schedule *list = (Vector_Schedule *)OperatorNew(sizeof(Vector_Schedule));
    if (list != NULL) {
        list->first = NULL;
        list->last = NULL;
        list->end = NULL;
    }
    listOfSchedules = list;

    Schedule *schedule = (Schedule *)OperatorNew(sizeof(Schedule));
    if (schedule != NULL) {
        schedule->Construct(1);
        schedule->vtable = Schedule_OncePerGameLoop_vtable;
    }
    s_oncePerGameLoop = schedule;

    schedule = (Schedule *)OperatorNew(sizeof(Schedule));
    if (schedule != NULL) {
        schedule->Construct(1);
        schedule->vtable = Schedule_SimRate_vtable;
    }
    s_SimRate = schedule;
    listOfSchedules->Insert(&schedule);

    schedule = (Schedule *)OperatorNew(sizeof(Schedule));
    if (schedule != NULL) {
        schedule->Construct(2);
        schedule->vtable = Schedule_HalfSimRate_vtable;
    }
    s_halfSimRate = schedule;
    listOfSchedules->Insert(&schedule);

    schedule = (Schedule *)OperatorNew(sizeof(Schedule));
    if (schedule != NULL) {
        schedule->Construct(4);
        schedule->vtable = Schedule_QuarterSimRate_vtable;
    }
    s_quarterSimRate = schedule;
    listOfSchedules->Insert(&schedule);
    return this;
}

// FUNC_AT(0x0005c140)
void Scheduler::Destruct() {
    if (s_oncePerGameLoop != NULL)
        s_oncePerGameLoop->DeletingDestructor(1);
    for (Schedule **at = listOfSchedules->first; at != listOfSchedules->last; at++)
        if (*at != NULL)
            (*at)->DeletingDestructor(1);
    Vector_Schedule *list = listOfSchedules;
    if (list != NULL) {
        Vector_Destruct(list, 0);
        OperatorDelete(list);
    }
}

// FUNC_AT(0x0005c740)
void Scheduler::Init() {
    if (fgScheduler != NULL)
        return;
    Scheduler *scheduler = (Scheduler *)OperatorNew(sizeof(Scheduler));
    fgScheduler = scheduler != NULL ? scheduler->Construct() : NULL;
}

// FUNC_AT(0x0005c460)
void Scheduler::Shutdown() {
    Scheduler *scheduler = fgScheduler;
    if (scheduler != NULL) {
        scheduler->Destruct();
        OperatorDelete(scheduler);
    }
    fgScheduler = NULL;
}

// FUNC_AT(0x0005ba40)
void Scheduler::Reset() {
    fgScheduler->timeScale = 1.0f;
    fgScheduler->cinematicModeSkipping = 0;
    fgScheduler->oneTickPerRun = 0;
    fgScheduler->lastTickCount = (int32_t)Clock;
}

// FUNC_AT(0x0005ba70)
void Scheduler::ResetTime() {
    lastTickCount = (int32_t)Clock;
    maybeElapsedTime = 0;
}

// ---- Scheduler::Run

// The fraction of a simulation tick carried from one call to the next - the one deliberate departure from the
// original, below. Always in [0, 1).
static double g_pendingTicks = 0.0;

// Runs one simulation tick: every simulation schedule at every priority, then the events they raised; on the last
// tick of the call, the per-frame schedule (rendering among it) and its events too.
static void RunTick(Scheduler *scheduler, int tick, bool last, bool *teleportDone) {
    // The original iterates listOfSchedules, which the Scheduler's constructor fills with exactly these three and
    // nothing else adds to. Each schedule's own Process (slot 1 of its vtable) picks which of its buckets the tick
    // runs - every tick, tick & 1, tick & 3 - so this does not need to know which is which.
    Schedule *const simulation[] = { scheduler->s_SimRate, scheduler->s_halfSimRate, scheduler->s_quarterSimRate };
    for (int priority = 0; priority < 8; priority++)
        for (Schedule *schedule : simulation)
            schedule->Process(tick, (unsigned short)priority);

    // Debug teleport (F8/F9, or Teleport= in settings.ini): once a call, before the events, because this is where
    // the game's own EResetPlayerCarPos event runs.
    if (!*teleportDone) {
        Teleport_Tick();
        *teleportDone = true;
    }
    EventManager::RunEvents();

    if (last) {
        for (int priority = 0; priority < 8; priority++)
            scheduler->s_oncePerGameLoop->Process(tick, (unsigned short)priority);
        EventManager::RunEvents();
    }
}

// Scheduler::Run (0x0005ba80), called once per pass of the game loop. As the original:
//
//   - The simulation advances by the ticks the clock has moved since the last call, scaled by timeScale (1 normally;
//     2 with the speed cheat; whatever an ESetSimRate event sets, up to 4, for slow or fast motion), and runs them
//     one at a time. With oneTickPerRun set it advances at most one tick a call, clock or not.
//   - A step of 12 ticks or more - a stall, such as a load - is not simulated at all: the time is dropped, so the
//     game does not lurch forward to catch up.
//   - With cinematicModeSkipping set (the mission manager sets it while a cinematic is being skipped), it runs
//     ticks as fast as it can until the flag clears, with the per-frame schedule only every fourth tick.
//   - The per-frame schedule, which renders, runs only on a call that simulates at least one tick.
//
// The departure: the original truncates elapsed * timeScale to whole ticks and then sets lastTickCount to the
// clock, so the fraction is lost every time. The clock usually moves one tick per call, which makes that loss the
// whole of the effect - a timeScale of 0.75 or 0.9 ran one tick every two (half speed), 0.3 one in four, and 1.5
// no faster than 1. Here the fraction is carried to the next call instead, so a slow or fast motion runs at the
// rate it asks for. Slow motion still lowers the frame rate, as in the original, because rendering only happens on
// a call that simulates a tick; smoothing that would need interpolation between ticks.
//
// AUTOINJECT
void Scheduler::Run(int unused) {
    (void)unused;
    const int now = (int)Clock;
    bool teleportDone = false;

    if (!this->cinematicModeSkipping) {
        int elapsed = now - this->lastTickCount;
        if (this->oneTickPerRun && elapsed != 0)
            elapsed = 1;
        if (elapsed <= 0) {
            // Nothing to simulate until the clock moves (every 20 ms at 50 Hz). The console spun here - the game
            // loop has no sleep - which on a PC holds a core at 100% for nothing; a millisecond's yield changes
            // nothing about the game (winmm's 1 ms timer period makes it a millisecond).
            Sleep(1);
            return;
        }

        g_pendingTicks += elapsed * (double)this->timeScale;
        int ticks = (int)std::floor(g_pendingTicks);
        g_pendingTicks -= ticks;

        if (ticks >= 12) {
            g_pendingTicks = 0.0;   // the stall guard drops the time, fraction and all
        } else if (ticks == 0) {
            Sleep(1);   // slow motion, and not yet a whole tick: as above
        } else {
            for (int k = 1; k <= ticks; k++)
                RunTick(this, this->lastTickCount + k, k == ticks, &teleportDone);
        }
        this->lastTickCount = now;
        return;
    }

    // Skipping a cinematic: simulate flat out until the mission manager clears the flag, drawing every fourth tick.
    int tick = this->lastTickCount;
    int untilDraw = 4;
    do {
        tick++;
        bool draw = --untilDraw < 1;
        if (draw)
            untilDraw = 4;
        RunTick(this, tick, draw, &teleportDone);
    } while (this->cinematicModeSkipping);
    g_pendingTicks = 0.0;
    this->lastTickCount = now;
}
