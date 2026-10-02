#ifndef DRIVING_PLATFORM_REALSYSTEM_H_
#define DRIVING_PLATFORM_REALSYSTEM_H_

// EA's portable system library ("REAL") - TIMER, THREAD, SIGNAL, SYNCTASK, MUTEX, CPU - as the driving engine has
// it. See RealSystem.cpp.

#include <windows.h>
#include <stdint.h>

typedef void (*TimerTask)(void);
typedef unsigned (*SyncTaskFn)(int argument, int ticksLate);
typedef void (*ThreadFn)(void);
typedef void (*ThreadParamFn)(void *parameter);

#pragma pack(push, 4)
struct SyncTask {            // 16 bytes, sixteen of them at 0x002421c8
    SyncTaskFn function;     // +0
    int interval;            // +4  ticks between runs
    int nextTick;            // +8  LibTicks when it is next due
    int running;             // +12 set while it runs
};
static_assert(sizeof(SyncTask) == 16, "SyncTask is 16 bytes");

struct RealThread {
    uint32_t unknown0;
    HANDLE handle;           // +4
    DWORD id;                // +8
};

struct RealSignal {
    uint32_t unknown0;
    HANDLE event;            // +4
};

struct RealMutex {
    uint32_t magic;          // +0  "Ftum" once destroyed
    uint8_t section[0x1c];   // +4  the Xbox's RTL_CRITICAL_SECTION (a Win32 one, in place, under the loader)
};
#pragma pack(pop)

int TIMER_gettick();
int TIMER_getfrequency();
void __stdcall TIMER_ontick(unsigned id, unsigned message, DWORD_PTR user, DWORD_PTR reserved1, DWORD_PTR reserved2);
int TIMER_init(int freqHz);
void TIMER_addtask(TimerTask task);
void TIMER_removetask(TimerTask task);
int getTickCount();

void SYNCTASK_add(SyncTaskFn task, int interval, int delay);
void SYNCTASK_del(SyncTaskFn task);
unsigned SYNCTASK_run(int argument);

void THREAD_init();
int THREAD_setpriority(RealThread *thread, int priority);
int THREAD_create(RealThread *thread, ThreadFn function, int unused, unsigned stackSize, int priority);
int THREAD_createparam(RealThread *thread, ThreadParamFn function, void *parameter, int unused,
                       unsigned stackSize, int priority);
void THREAD_destroy(RealThread *thread);
void THREAD_yield(unsigned milliseconds);
bool THREAD_iscurrent(RealThread *thread);
bool THREAD_testexit(RealThread *thread);
void THREAD_exit();

int SIGNAL_create(RealSignal *signal);
void SIGNAL_destroy(RealSignal *signal);

char MUTEX_create(RealMutex *mutex);
void REALMUTEX_destroy(RealMutex *mutex);
void MUTEX_lock(RealMutex *mutex);
void MUTEX_unlock(RealMutex *mutex);

unsigned CPU_detect();

#endif // DRIVING_PLATFORM_REALSYSTEM_H_
