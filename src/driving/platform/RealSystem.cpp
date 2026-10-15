#include "RealSystem.h"
#include "KernelImports.h"

#include "XboxStartup.h"
#include "XboxTimer.h"
#include "XboxXapi.h"
#include "../devtools/SndLockstep.h"

#include "Win32Types.h"
#ifdef _WIN32
#include <mmsystem.h>
#endif
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's portable system library ("REAL"), the part the driving engine's game and engine code calls for time,
// threads, events, periodic tasks and locks: TIMER, THREAD, SIGNAL, SYNCTASK, MUTEX, CPU. Each function here is
// the original at the same address, ported from it (Ghidra's Driving.xbe; the PS2 build's symbols name them).
// The XAPI calls the originals made are ours (platform/XboxXapi.cpp, XboxStartup.cpp, over the host layer); the
// critical sections go through the kernel imports, as the originals do, because the loader's
// RtlEnterCriticalSection also initialises a section that was only ever zero-filled (src/loader/kernel.cpp).
// ---------------------------------------------------------------------------------------------------------------

// ---- TIMER: the periodic tick (the video refresh rate) and the task list it runs

static const int kTimerTasks = 8;

// XBE_GLOBAL(0x002422d0, 4)
static int TimerActive;
// XBE_GLOBAL(0x002422d4, 4)
static unsigned SysTimerHandle;
// XBE_GLOBAL(0x00242404, 0x20)
static TimerTask TimerTaskList[kTimerTasks];
// XBE_GLOBAL(0x00242424, 4)
int TimerFrequency;
// XBE_GLOBAL(0x00242428, 4)
volatile int SomeTicks;
// XBE_GLOBAL(0x0024242c, 4)
static volatile int LibTicks;

// AUTOINJECT
int TIMER_gettick() {
    // NIGHTFIRE_SNDLOCKSTEP: the audio framework times voices and streams by it, so it counts simulation ticks
    if (SndLockstep_Enabled())
        return SndLockstep_TimerTick(SomeTicks);
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


// AUTOINJECT
int getTickCount() {
    return *KernelKeTickCount();   // the kernel's tick count, an imported variable
}

// ---- SYNCTASK: tasks run from the game loop (SYNCTASK_run), each every so many ticks

static const int kSyncTasks = 16;

// XBE_GLOBAL(0x002421c8, 0x100)
static SyncTask SyncTasks[kSyncTasks];
// XBE_GLOBAL(0x002422c8, 4)
static int SyncTaskSkip;      // free slots for SYNCTASK_add to pass over before taking one
// XBE_GLOBAL(0x002422cc, 4)
static int LastSystemTask;

// LibTicks as the sync tasks read it. NIGHTFIRE_SNDLOCKSTEP: from the first simulation tick it counts those ticks
// instead, as TIMER_gettick does, and every task falls due at that tick's first pass, so that each runs on the same
// ticks in every run (SndLockstep_SyncTick). `pass` is SYNCTASK_run's first read.
static int SyncTaskTick(bool pass) {
    if (!SndLockstep_Enabled())
        return LibTicks;
    bool restart = false;
    int tick = SndLockstep_SyncTick(LibTicks, pass, &restart);
    if (restart) {
        for (int i = 0; i < kSyncTasks; i++)
            SyncTasks[i].nextTick = tick;
    }
    return tick;
}

// interval: -1 means every tick (0), 0 means every other (1); first run `delay` ticks from now. A task already
// in the list has its slot rewritten (the last such slot, as the original's scan leaves it); otherwise the first
// free slot after SyncTaskSkip of them. With neither, nothing happens.
// AUTOINJECT
void SYNCTASK_add(SyncTaskFn task, int interval, int delay) {
    int now = SyncTaskTick(false);
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
    LastSystemTask = SyncTaskTick(true);
    for (int i = 0; i < kSyncTasks; i++) {
        SyncTask *task = &SyncTasks[i];
        if (task->function != NULL && task->nextTick <= SyncTaskTick(false) && task->running == 0) {
            int late = SyncTaskTick(false) - task->nextTick;
            task->running = 1;
            result |= task->function(argument, late);
            task->nextTick = SyncTaskTick(false) + task->interval;
            task->running = 0;
        }
    }
    return result;
}

// ---- THREAD

// XBE_GLOBAL(0x002421c0, 4)
static HANDLE MainThreadHandle;
// XBE_GLOBAL(0x002421c4, 4)
static DWORD MainThreadId;

// AUTOINJECT
void THREAD_init() {
    MainThreadHandle = (HANDLE)(intptr_t)-2;   // the current thread's pseudo-handle, as the original stores it
    MainThreadId = Xbox_GetCurrentThreadId();
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
    Xbox_SetThreadPriority(handle, value);
    return 1;
}

static int StartThread(RealThread *thread, ThreadStart *start, unsigned stackSize, int priority) {
    thread->handle = Xbox_CreateThread(NULL, stackSize, ThreadEntry, start, CREATE_SUSPENDED, &thread->id);
    if (thread->handle == NULL)
        return 0;
    THREAD_setpriority(thread, priority);
    Xbox_ResumeThread(thread->handle);
    while (start->started == 0)
        Xbox_SleepEx(1, TRUE);
    return 1;
}

// AUTOINJECT
int THREAD_create(RealThread *thread, ThreadFn function, void *unused, unsigned stackSize, int priority) {
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
        Xbox_CloseHandle((HANDLE)(intptr_t)-2);
        return;
    }
    if (thread->handle != NULL) {
        Xbox_CloseHandle(thread->handle);
        thread->handle = NULL;
    }
}

// AUTOINJECT
void THREAD_yield(unsigned milliseconds) {
    Xbox_SleepEx(milliseconds, TRUE);
}

// Zero asks about the main thread, -1 always answers yes.
// AUTOINJECT
bool THREAD_iscurrent(RealThread *thread) {
    if (thread == NULL)
        return Xbox_GetCurrentThreadId() == MainThreadId;
    if (thread == (RealThread *)(intptr_t)-1)
        return true;
    return thread->id == Xbox_GetCurrentThreadId();
}

// AUTOINJECT
bool THREAD_testexit(RealThread *thread) {
    DWORD code = 0;
    if (!Xbox_GetExitCodeThread(thread->handle, &code))
        return false;
    return code != STILL_ACTIVE;
}

// AUTOINJECT
void THREAD_exit() {
    Xbox_ExitThread(0);
}

// ---- SIGNAL: an auto-reset event

// AUTOINJECT
int SIGNAL_create(RealSignal *signal) {
    HANDLE event = Xbox_CreateEventA(NULL, FALSE, FALSE, NULL);
    if (event == NULL)
        return 0;
    signal->event = event;
    return 1;
}

// AUTOINJECT
void SIGNAL_destroy(RealSignal *signal) {
    Xbox_CloseHandle(signal->event);
}

// Ghidra's iFILE_maybeExecCommand: FILESYS raises its worker's signal with it, IFeedback its own.
// FUNC_AT(0x0010a700)
BOOL SIGNAL_post(RealSignal *signal) {
    return Xbox_SetEvent(signal->event);
}

// FUNC_AT(0x0010a710)
DWORD SIGNAL_wait(RealSignal *signal) {
    return Xbox_WaitForSingleObject(signal->event, INFINITE);
}

// ---- MUTEX: a critical section at +4, through the kernel's imports


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

// The console's answer, from the XBE's data: "Pentium3" and its clock speed.
// XBE_GLOBAL(0x001d1b90, 4)
static const unsigned CpuSpeed = 733000000;

struct CpuInfoRecord {
    unsigned speed;
    unsigned unknown4;
    char name[16];
};
static_assert(sizeof(CpuInfoRecord) == 0x18, "CpuInfoRecord is 0x18 bytes");

// XBE_GLOBAL(0x002422e0, 0x18)
static CpuInfoRecord CpuInfo;

// AUTOINJECT
unsigned CPU_detect() {
    CpuInfo.speed = CpuSpeed;
    CpuInfo.unknown4 = 0;
    memcpy(CpuInfo.name, "Pentium3", 9);
    return CpuSpeed;
}
