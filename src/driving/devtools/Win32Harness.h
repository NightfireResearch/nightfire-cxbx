#ifndef DRIVING_DEVTOOLS_WIN32HARNESS_H_
#define DRIVING_DEVTOOLS_WIN32HARNESS_H_

// The Win32 calls the lockstep harness makes (Teleport.cpp, SndLockstep.cpp, SndCallTrace.cpp): windows.h's on
// Windows; elsewhere the same calls over the host layer, POSIX semaphores and the compiler's atomics.

#include "../platform/Win32Types.h"

#ifndef _WIN32

#include <errno.h>
#include <stdlib.h>
#include <sys/random.h>
#include <time.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>   // Darwin has no unnamed POSIX semaphores
#else
#include <semaphore.h>
#endif

#include "../platform/Host.h"

static inline DWORD GetEnvironmentVariableA(const char *name, char *value, DWORD size) {
    return HostGetEnvironment(name, value, size);
}

static inline DWORD GetPrivateProfileStringA(const char *section, const char *key, const char *defaultValue,
                                             char *value, DWORD size, const char *file) {
    return HostGetProfileString(section, key, defaultValue, value, size, file);
}

static inline UINT GetPrivateProfileIntA(const char *section, const char *key, INT defaultValue, const char *file) {
    char value[32];
    if (HostGetProfileString(section, key, "", value, sizeof(value), file) == 0)
        return (UINT)defaultValue;
    return (UINT)strtol(value, NULL, 10);
}

static inline DWORD GetTickCount(void) {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (DWORD)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static inline void GetLocalTime(SYSTEMTIME *time) {
    HostGetLocalTime(reinterpret_cast<HostSystemTime *>(time));
}

static inline void Sleep(DWORD milliseconds) {
    HostSleep(milliseconds);
}

static inline BOOL SwitchToThread(void) {
    HostSleep(0);
    return TRUE;
}

static inline DWORD GetCurrentThreadId(void) {
    return HostGetCurrentThreadId();
}

static inline LONG InterlockedIncrement(volatile LONG *value) {
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedExchange(volatile LONG *target, LONG value) {
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedCompareExchange(volatile LONG *target, LONG exchange, LONG comparand) {
    __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

typedef HostLock CRITICAL_SECTION;

static inline void InitializeCriticalSection(CRITICAL_SECTION *section) {
    HostInitLock(section);
}

static inline void EnterCriticalSection(CRITICAL_SECTION *section) {
    HostEnterLock(section);
}

static inline void LeaveCriticalSection(CRITICAL_SECTION *section) {
    HostLeaveLock(section);
}

// Semaphores are the only objects the harness waits on
#ifdef __APPLE__
static inline HANDLE CreateSemaphoreA(SECURITY_ATTRIBUTES *, LONG initialCount, LONG, const char *) {
    return dispatch_semaphore_create(initialCount);
}

static inline BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG *) {
    while (count-- > 0)
        dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(semaphore));
    return TRUE;
}

static inline DWORD WaitForSingleObject(HANDLE semaphore, DWORD milliseconds) {
    dispatch_time_t until = dispatch_time(DISPATCH_TIME_NOW, int64_t(milliseconds) * 1000000);
    return dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(semaphore), until) == 0 ? WAIT_OBJECT_0
                                                                                            : WAIT_TIMEOUT;
}

static inline int rand_s(unsigned *value) {
    return getentropy(value, sizeof(*value)) == 0 ? 0 : EINVAL;
}
#else
static inline HANDLE CreateSemaphoreA(SECURITY_ATTRIBUTES *, LONG initialCount, LONG, const char *) {
    sem_t *semaphore = new sem_t;
    sem_init(semaphore, 0, (unsigned)initialCount);
    return semaphore;
}

static inline BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG *) {
    while (count-- > 0)
        sem_post(static_cast<sem_t *>(semaphore));
    return TRUE;
}

static inline DWORD WaitForSingleObject(HANDLE semaphore, DWORD milliseconds) {
    timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += milliseconds / 1000;
    until.tv_nsec += (long)(milliseconds % 1000) * 1000000;
    if (until.tv_nsec >= 1000000000) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000;
    }
    while (sem_timedwait(static_cast<sem_t *>(semaphore), &until) != 0) {
        if (errno == ETIMEDOUT)
            return WAIT_TIMEOUT;
    }
    return WAIT_OBJECT_0;
}

static inline int rand_s(unsigned *value) {
    return getrandom(value, sizeof(*value), 0) == sizeof(*value) ? 0 : EINVAL;
}
#endif

#endif // !_WIN32

#endif // DRIVING_DEVTOOLS_WIN32HARNESS_H_
