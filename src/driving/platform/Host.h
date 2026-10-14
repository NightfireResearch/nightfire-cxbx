#ifndef DRIVING_PLATFORM_HOST_H_
#define DRIVING_PLATFORM_HOST_H_

// What the driving engine takes from the operating system it runs on: threads, thread-local storage, locks,
// events and waits, the heap, files, the clock, sleeping, and a few process-level odds and ends. Two bodies:
// HostWin32.cpp, the Win32 calls the code made before this layer existed, one for one; and HostPosix.cpp,
// pthreads, clock_gettime and POSIX files, for a Linux build. docs/driving/portable.md has the plan.
//
// Above this layer, XboxXapi.cpp and XboxStartup.cpp give the game XAPI's semantics, as before; the game's own
// ports (FILESYS, THREAD, SIGNAL...) call XAPI, as the originals did.
//
// The interface speaks Win32's language, which XAPI shares: handles are opaque pointers the game keeps in its
// records, failures set a Win32 error number the game reads through GetLastError, and the flags and results
// (GENERIC_READ, OPEN_EXISTING, WAIT_TIMEOUT, STILL_ACTIVE, HEAP_ZERO_MEMORY...) are Win32's numbers, so XAPI
// passes them straight through. The POSIX body translates. Out-parameters that are DWORDs on the XAPI side are
// `unsigned long`, which is DWORD on Windows and 32 bits on a 32-bit Linux.

#include <stddef.h>
#include <stdint.h>

#if !defined(_WIN32) && !defined(__stdcall)
#define __stdcall __attribute__((stdcall))
#endif

typedef void *HostHandle;

#define HOST_INVALID_HANDLE  ((HostHandle)(intptr_t)-1)   // INVALID_HANDLE_VALUE: a file that did not open
#define HOST_CURRENT_THREAD  ((HostHandle)(intptr_t)-2)   // GetCurrentThread()'s pseudo-handle
#define HOST_INFINITE        0xffffffffu
#define HOST_WAIT_OBJECT_0   0u
#define HOST_WAIT_TIMEOUT    0x102u
#define HOST_WAIT_FAILED     0xffffffffu
#define HOST_STILL_ACTIVE    0x103u
#define HOST_CREATE_SUSPENDED 4u
#define HOST_TLS_OUT_OF_INDEXES 0xffffffffu
#define HOST_HEAP_ZERO_MEMORY 8u
#define HOST_MAX_PATH        260

typedef unsigned long (__stdcall *HostThreadStart)(void *parameter);
typedef void (__stdcall *HostTimerCallback)(unsigned id, unsigned message, unsigned long user,
                                            unsigned long reserved1, unsigned long reserved2);

// ---- errors: the calling thread's last error, a Win32 error number
unsigned long HostGetLastError(void);
void HostSetLastError(unsigned long error);

// ---- threads. Priorities are Win32's THREAD_PRIORITY_* numbers.
HostHandle HostCreateThread(size_t stackSize, HostThreadStart start, void *parameter, unsigned long flags,
                            unsigned long *threadId);
unsigned long HostResumeThread(HostHandle thread);
void HostExitThread(unsigned long exitCode);
int HostSetThreadPriority(HostHandle thread, int priority);
int HostGetExitCodeThread(HostHandle thread, unsigned long *exitCode);   // HOST_STILL_ACTIVE while it runs
unsigned long HostGetCurrentThreadId(void);

// ---- thread-local storage
unsigned long HostTlsAlloc(void);
void *HostTlsGetValue(unsigned long slot);
int HostTlsSetValue(unsigned long slot, void *value);

// ---- locks: a recursive critical section, kept in place in its owner's memory
struct HostLock {
    alignas(4) unsigned char storage[24];
};
void HostInitLock(HostLock *lock);
void HostEnterLock(HostLock *lock);
void HostLeaveLock(HostLock *lock);

long HostInterlockedIncrement(volatile long *value);
long HostInterlockedDecrement(volatile long *value);

// ---- events and waits. HostWaitForObject waits on an event or a thread.
HostHandle HostCreateEvent(int manualReset, int initialState);
int HostSetEvent(HostHandle event);
int HostResetEvent(HostHandle event);
unsigned long HostWaitForObject(HostHandle handle, unsigned long milliseconds);
unsigned long HostWaitForObjectEx(HostHandle handle, unsigned long milliseconds, int alertable);
int HostCloseHandle(HostHandle handle);   // a thread, an event or a file

// ---- sleeping
void HostSleep(unsigned long milliseconds);
unsigned long HostSleepEx(unsigned long milliseconds, int alertable);

// ---- heaps. A maximum of zero makes a growable heap.
HostHandle HostHeapCreate(unsigned long flags, size_t initialSize, size_t maximumSize);
void *HostHeapAlloc(HostHandle heap, unsigned long flags, size_t size);
int HostHeapFree(HostHandle heap, unsigned long flags, void *block);
void *HostHeapReAlloc(HostHandle heap, unsigned long flags, void *block, size_t size);
size_t HostHeapSize(HostHandle heap, unsigned long flags, const void *block);

// ---- pages: memory of its own, aligned to at least a page, zeroed, readable, writable and executable; placed at the
// lowest free address at or above `lowest` that ends below `limit`, or anywhere if there is none
void *HostAllocatePages(size_t size, uintptr_t lowest, uintptr_t limit);
void HostFreePages(void *block);

// ---- files, by host path. Access, share, disposition and attributes are CreateFile's.
HostHandle HostCreateFile(const char *path, unsigned long access, unsigned long share, unsigned long disposition,
                          unsigned long attributes);
int HostDeleteFile(const char *path);
int HostReadFile(HostHandle file, void *buffer, unsigned long bytes, unsigned long *read);
int HostWriteFile(HostHandle file, const void *buffer, unsigned long bytes, unsigned long *written);
unsigned long HostSetFilePointer(HostHandle file, long distance, long *distanceHigh, unsigned long method);
unsigned long HostGetFileSize(HostHandle file, unsigned long *sizeHigh);
int HostGetFileSizeEx(HostHandle file, int64_t *size);

// A directory search: the pattern's last component may hold * and ?, matched without regard to case. Each call
// gives the next entry's name (at most HOST_MAX_PATH bytes with its terminator).
HostHandle HostFindFirstFile(const char *pattern, char *name);
int HostFindNextFile(HostHandle search, char *name);
int HostFindClose(HostHandle search);

// GetPrivateProfileString: a key of an .ini file's section, or the default
unsigned long HostGetProfileString(const char *section, const char *key, const char *defaultValue, char *value,
                                   unsigned long size, const char *file);

// ---- time
int HostQueryPerformanceCounter(int64_t *counter);
int HostQueryPerformanceFrequency(int64_t *frequency);
unsigned long HostTimeMs(void);   // timeGetTime: milliseconds, wrapping

// The multimedia timer: a callback on a thread of the host's every delay ms (periodic, flags 1) or once.
// HostRaiseTimerResolution asks for 1 ms scheduling for the whole process.
int HostRaiseTimerResolution(void);
unsigned HostSetTimerEvent(unsigned delayMs, unsigned resolutionMs, HostTimerCallback callback, unsigned long user,
                           unsigned flags);

struct HostSystemTime {          // SYSTEMTIME
    uint16_t year, month, dayOfWeek, day, hour, minute, second, milliseconds;
};
struct HostTimeZone {            // TIME_ZONE_INFORMATION
    long bias;
    uint16_t standardName[32];
    HostSystemTime standardDate;
    long standardBias;
    uint16_t daylightName[32];
    HostSystemTime daylightDate;
    long daylightBias;
};
void HostGetLocalTime(HostSystemTime *time);
unsigned long HostGetTimeZoneInformation(HostTimeZone *zone);

// ---- the process
// The command line as narrow strings, argv[0] first; the POSIX body is told it by main (HostSetCommandLine).
char **HostCommandLine(int *count);
void HostSetCommandLine(int count, char **arguments);
// An environment variable's value, its length (0 if unset)
unsigned long HostGetEnvironment(const char *name, char *value, unsigned long size);
void HostExitProcess(unsigned exitCode);
void HostDebugBreak(void);
// RaiseException: the C++ runtime's throw (CRuntimeStd.cpp)
void HostRaiseException(unsigned long code, unsigned long flags, unsigned long count, const unsigned long *arguments);
// IsBadReadPtr / IsBadWritePtr
int HostIsBadReadPtr(const void *pointer, size_t bytes);
int HostIsBadWritePtr(void *pointer, size_t bytes);

#endif // DRIVING_PLATFORM_HOST_H_
