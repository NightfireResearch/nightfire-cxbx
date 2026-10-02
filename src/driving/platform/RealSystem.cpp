#include "RealSystem.h"

#include "XboxTimer.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's portable system library ("REAL"), the part the driving engine's game and engine code calls for time,
// threads, events, periodic tasks and locks: TIMER, THREAD, SIGNAL, SYNCTASK, MUTEX, CPU. Each function here is
// the original at the same address, ported from it (Ghidra's Driving.xbe; the PS2 build's symbols name them).
// The XAPI calls the originals made are Win32's now (platform/XboxXapi.cpp), so these call Win32 directly; the
// critical sections go through the kernel imports, as the originals do, because the loader's
// RtlEnterCriticalSection also initialises a section that was only ever zero-filled (src/loader/kernel.cpp).
// ---------------------------------------------------------------------------------------------------------------

// ---- TIMER: the periodic tick (the video refresh rate) and the task list it runs

#define TimerActive      (*(int *)0x002422d0u)
#define SysTimerHandle   (*(unsigned *)0x002422d4u)
#define TimerTaskList    ((TimerTask *)0x00242404u)    // eight slots, to 0x00242424
#define TimerFrequency   (*(int *)0x00242424u)
#define SomeTicks        (*(volatile int *)0x00242428u)
#define LibTicks         (*(volatile int *)0x0024242cu)
static const int kTimerTasks = 8;

// AUTOINJECT
int TIMER_gettick() {
    return SomeTicks;
}

// AUTOINJECT
int TIMER_getfrequency() {
    return TimerFrequency;
}

// The callback timeSetEvent runs on its own thread: both tick counts up by one, then every task in the list.
// The original returns whatever its last task left in EDX:EAX; nothing reads it.
// AUTOINJECT
void __stdcall TIMER_ontick(unsigned id, unsigned message, DWORD_PTR user, DWORD_PTR reserved1, DWORD_PTR reserved2) {
    (void)id; (void)message; (void)user; (void)reserved1; (void)reserved2;
    SomeTicks = SomeTicks + 1;
    LibTicks = LibTicks + 1;
    for (int i = 0; i < kTimerTasks; i++) {
        if (TimerTaskList[i] != NULL)
            TimerTaskList[i]();
    }
}

// Starts the tick at freqHz (100 if zero), once: a second call answers the rate already running.
// AUTOINJECT
int TIMER_init(int freqHz) {
    if (TimerActive != 0)
        return TimerFrequency;
    if (freqHz == 0)
        freqHz = 100;
    SysTimerHandle = Xbox_timeSetEvent(1000 / freqHz, 0, (LPTIMECALLBACK)TIMER_ontick, 0, TIME_PERIODIC);
    TimerFrequency = freqHz;
    TimerActive = 1;
    return freqHz;
}

// A task already in the list is not added twice; with all eight slots taken, it is not added at all.
// AUTOINJECT
void TIMER_addtask(TimerTask task) {
    for (int i = 0; i < kTimerTasks; i++) {
        if (TimerTaskList[i] == task)
            return;
    }
    for (int i = 0; i < kTimerTasks; i++) {
        if (TimerTaskList[i] == NULL) {
            TimerTaskList[i] = task;
            return;
        }
    }
}

// AUTOINJECT
void TIMER_removetask(TimerTask task) {
    for (int i = 0; i < kTimerTasks; i++) {
        if (TimerTaskList[i] == task) {
            TimerTaskList[i] = NULL;
            return;
        }
    }
}

// The kernel's tick count, an imported variable (KeTickCount, through the import slot at 0x00189bfc).
#define KeTickCountImport (*(volatile int **)0x00189bfcu)

// AUTOINJECT
int getTickCount() {
    return *KeTickCountImport;
}

// ---- SYNCTASK: tasks run from the game loop (SYNCTASK_run), each every so many ticks

#define SyncTasks        ((SyncTask *)0x002421c8u)    // sixteen slots
#define SyncTaskSkip     (*(int *)0x002422c8u)        // free slots for SYNCTASK_add to pass over before taking one
#define LastSystemTask   (*(int *)0x002422ccu)
static const int kSyncTasks = 16;

// interval: -1 means every tick (0), 0 means every other (1); first run `delay` ticks from now. A task already
// in the list has its slot rewritten (the last such slot, as the original's scan leaves it); otherwise the first
// free slot after SyncTaskSkip of them. With neither, nothing happens.
// AUTOINJECT
void SYNCTASK_add(SyncTaskFn task, int interval, int delay) {
    int now = LibTicks;
    if (interval == -1)
        interval = 0;
    else if (interval == 0)
        interval = 1;

    int chosen = -1, skip = SyncTaskSkip;
    for (int i = 0; i < kSyncTasks; i++) {
        if (SyncTasks[i].function == task) {
            chosen = i;
        } else if (SyncTasks[i].function == NULL && chosen == -1) {
            if (skip == 0)
                chosen = i;
            else
                skip--;
        }
    }
    if (chosen != -1) {
        SyncTasks[chosen].interval = interval;
        SyncTasks[chosen].function = task;
        SyncTasks[chosen].nextTick = now + delay;
        SyncTasks[chosen].running = 0;
    }
}

// AUTOINJECT
void SYNCTASK_del(SyncTaskFn task) {
    for (int i = 0; i < kSyncTasks; i++) {
        if (SyncTasks[i].function == task) {
            SyncTasks[i].function = NULL;
            return;
        }
    }
}

// Runs every task that is due and not already running (a task may run the list itself), passing how many ticks
// late it is; returns the tasks' results ORed together.
// AUTOINJECT
unsigned SYNCTASK_run(int argument) {
    unsigned result = 0;
    LastSystemTask = LibTicks;
    for (int i = 0; i < kSyncTasks; i++) {
        SyncTask *task = &SyncTasks[i];
        if (task->function != NULL && task->nextTick <= LibTicks && task->running == 0) {
            int late = LibTicks - task->nextTick;
            task->running = 1;
            result |= task->function(argument, late);
            task->nextTick = LibTicks + task->interval;
            task->running = 0;
        }
    }
    return result;
}

// ---- THREAD

#define MainThreadHandle (*(HANDLE *)0x002421c0u)
#define MainThreadId     (*(DWORD *)0x002421c4u)

// AUTOINJECT
void THREAD_init() {
    MainThreadHandle = (HANDLE)(intptr_t)-2;   // the current thread's pseudo-handle, as the original stores it
    MainThreadId = GetCurrentThreadId();
}

// The block a new thread starts with (on its creator's stack): a started flag the creator waits on, then either
// a function of no arguments or one of one argument. The original sets the flag before reading the rest, while
// its creator may already be returning; this reads them first.
struct ThreadStart {
    volatile int started;
    void (*function)(void);
    void (*functionWithParameter)(void *);
    void *parameter;
};

static DWORD WINAPI ThreadEntry(void *block) {   // 0x0010a730
    ThreadStart start = *(ThreadStart *)block;
    ((ThreadStart *)block)->started = 1;
    if (start.function != NULL)
        start.function();
    else
        start.functionWithParameter(start.parameter);
    return 1;
}

// AUTOINJECT
int THREAD_setpriority(RealThread *thread, int priority) {
    HANDLE handle = MainThreadHandle;   // zero means the main thread
    if (thread != NULL) {
        if (thread == (RealThread *)(intptr_t)-1)
            handle = (HANDLE)(intptr_t)-2;
        else
            handle = thread->handle;
    }
    if (handle == NULL)
        return 0;
    int value;
    switch (priority) {
        case 1:  value = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case 2:  value = THREAD_PRIORITY_HIGHEST; break;
        case 3:  value = THREAD_PRIORITY_TIME_CRITICAL; break;
        case -3: value = THREAD_PRIORITY_IDLE; break;
        case -2: value = THREAD_PRIORITY_LOWEST; break;
        case -1: value = THREAD_PRIORITY_BELOW_NORMAL; break;
        default: value = THREAD_PRIORITY_NORMAL; break;
    }
    SetThreadPriority(handle, value);
    return 1;
}

static int StartThread(RealThread *thread, ThreadStart *start, unsigned stackSize, int priority) {
    thread->handle = CreateThread(NULL, stackSize, ThreadEntry, start, CREATE_SUSPENDED, &thread->id);
    if (thread->handle == NULL)
        return 0;
    THREAD_setpriority(thread, priority);
    ResumeThread(thread->handle);
    while (start->started == 0)
        SleepEx(1, TRUE);
    return 1;
}

// AUTOINJECT
int THREAD_create(RealThread *thread, ThreadFn function, int unused, unsigned stackSize, int priority) {
    (void)unused;
    ThreadStart start = { 0, function, NULL, NULL };
    return StartThread(thread, &start, stackSize, priority);
}

// AUTOINJECT
int THREAD_createparam(RealThread *thread, ThreadParamFn function, void *parameter, int unused,
                       unsigned stackSize, int priority) {
    (void)unused;
    ThreadStart start = { 0, NULL, function, parameter };
    return StartThread(thread, &start, stackSize, priority);
}

// -1 is the current thread: the original closes its pseudo-handle and then writes to address 3, so it is never
// called that way; the write is left out.
// AUTOINJECT
void THREAD_destroy(RealThread *thread) {
    if (thread == (RealThread *)(intptr_t)-1) {
        CloseHandle((HANDLE)(intptr_t)-2);
        return;
    }
    if (thread->handle != NULL) {
        CloseHandle(thread->handle);
        thread->handle = NULL;
    }
}

// AUTOINJECT
void THREAD_yield(unsigned milliseconds) {
    SleepEx(milliseconds, TRUE);
}

// Zero asks about the main thread, -1 always answers yes.
// AUTOINJECT
bool THREAD_iscurrent(RealThread *thread) {
    if (thread == NULL)
        return GetCurrentThreadId() == MainThreadId;
    if (thread == (RealThread *)(intptr_t)-1)
        return true;
    return thread->id == GetCurrentThreadId();
}

// AUTOINJECT
bool THREAD_testexit(RealThread *thread) {
    DWORD code = 0;
    if (!GetExitCodeThread(thread->handle, &code))
        return false;
    return code != STILL_ACTIVE;
}

// AUTOINJECT
void THREAD_exit() {
    ExitThread(0);
}

// ---- SIGNAL: an auto-reset event

// AUTOINJECT
int SIGNAL_create(RealSignal *signal) {
    HANDLE event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (event == NULL)
        return 0;
    signal->event = event;
    return 1;
}

// AUTOINJECT
void SIGNAL_destroy(RealSignal *signal) {
    CloseHandle(signal->event);
}

// ---- MUTEX: a critical section at +4, through the kernel's imports

#define KernelRtlInitializeCriticalSection (*(void (__stdcall **)(void *))0x00189cf0u)
#define KernelRtlEnterCriticalSection      (*(void (__stdcall **)(void *))0x00189c40u)
#define KernelRtlLeaveCriticalSection      (*(void (__stdcall **)(void *))0x00189c3cu)

// Returns 0 in AL, as the original does whatever happens.
// AUTOINJECT
char MUTEX_create(RealMutex *mutex) {
    KernelRtlInitializeCriticalSection(&mutex->section);
    return 0;
}

// Nothing is deleted: the original only marks the mutex ("Ftum").
// AUTOINJECT
void REALMUTEX_destroy(RealMutex *mutex) {
    mutex->magic = 0x6d757446u;
}

// AUTOINJECT
void MUTEX_lock(RealMutex *mutex) {
    KernelRtlEnterCriticalSection(&mutex->section);
}

// AUTOINJECT
void MUTEX_unlock(RealMutex *mutex) {
    KernelRtlLeaveCriticalSection(&mutex->section);
}

// ---- CPU

// The console's answer, from the XBE's data: "Pentium3" and its clock speed (0x001d1b90).
#define CpuSpeed  (*(const unsigned *)0x001d1b90u)
#define CpuInfo   ((uint8_t *)0x002422e0u)   // speed, 0, then the name

// AUTOINJECT
unsigned CPU_detect() {
    *(unsigned *)(CpuInfo + 0) = CpuSpeed;
    *(unsigned *)(CpuInfo + 4) = 0;
    memcpy(CpuInfo + 8, (const void *)0x001a1880u, 9);   // "Pentium3" and its terminator
    return CpuSpeed;
}
