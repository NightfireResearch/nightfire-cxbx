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
// passes them straight through. The POSIX body translates. DWORDs and LONGs are HostDword and HostLong: 32 bits,
// as Win32's, on every host.

#include <stddef.h>
#include <stdint.h>

#if !defined(_WIN32) && !defined(__stdcall)
#define __stdcall __attribute__((stdcall))
#endif

typedef void *HostHandle;

// DWORD and LONG: unsigned long and long on Windows and on 32-bit hosts; fixed-width on LP64 ones, where long is 64 bits
#if defined(_WIN32) || UINTPTR_MAX == 0xffffffffu
typedef unsigned long HostDword;
typedef long HostLong;
#else
typedef uint32_t HostDword;
typedef int32_t HostLong;
#endif

// ULONG_PTR and DWORD_PTR: unsigned long on 32-bit hosts and on LP64 ones, 64 bits on 64-bit Windows
#ifdef _WIN64
typedef unsigned long long HostUlongPtr;
#else
typedef unsigned long HostUlongPtr;
#endif

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

typedef HostDword (__stdcall *HostThreadStart)(void *parameter);
typedef void (__stdcall *HostTimerCallback)(unsigned id, unsigned message, HostUlongPtr user,
                                            HostUlongPtr reserved1, HostUlongPtr reserved2);

// ---- errors: the calling thread's last error, a Win32 error number
HostDword HostGetLastError(void);
void HostSetLastError(HostDword error);

// ---- threads. Priorities are Win32's THREAD_PRIORITY_* numbers.
HostHandle HostCreateThread(size_t stackSize, HostThreadStart start, void *parameter, HostDword flags,
                            HostDword *threadId);
HostDword HostResumeThread(HostHandle thread);
void HostExitThread(HostDword exitCode);
int HostSetThreadPriority(HostHandle thread, int priority);
int HostGetExitCodeThread(HostHandle thread, HostDword *exitCode);   // HOST_STILL_ACTIVE while it runs
HostDword HostGetCurrentThreadId(void);

// ---- thread-local storage
HostDword HostTlsAlloc(void);
void *HostTlsGetValue(HostDword slot);
int HostTlsSetValue(HostDword slot, void *value);

// ---- locks: a recursive critical section, kept in place in its owner's memory
struct HostLock {
    alignas(4) unsigned char storage[24];
};
void HostInitLock(HostLock *lock);
void HostEnterLock(HostLock *lock);
void HostLeaveLock(HostLock *lock);

HostLong HostInterlockedIncrement(volatile HostLong *value);
HostLong HostInterlockedDecrement(volatile HostLong *value);

// ---- events and waits. HostWaitForObject waits on an event or a thread.
HostHandle HostCreateEvent(int manualReset, int initialState);
int HostSetEvent(HostHandle event);
int HostResetEvent(HostHandle event);
HostDword HostWaitForObject(HostHandle handle, HostDword milliseconds);
HostDword HostWaitForObjectEx(HostHandle handle, HostDword milliseconds, int alertable);
int HostCloseHandle(HostHandle handle);   // a thread, an event or a file

// ---- sleeping
void HostSleep(HostDword milliseconds);
HostDword HostSleepEx(HostDword milliseconds, int alertable);

// ---- heaps. A maximum of zero makes a growable heap.
HostHandle HostHeapCreate(HostDword flags, size_t initialSize, size_t maximumSize);
void *HostHeapAlloc(HostHandle heap, HostDword flags, size_t size);
int HostHeapFree(HostHandle heap, HostDword flags, void *block);
void *HostHeapReAlloc(HostHandle heap, HostDword flags, void *block, size_t size);
size_t HostHeapSize(HostHandle heap, HostDword flags, const void *block);

// ---- pages: memory of its own, aligned to at least a page, zeroed, readable, writable and executable; placed at the
// lowest free address at or above `lowest` that ends below `limit`, or anywhere if there is none (on a 64-bit host,
// `lowest` and `limit` are offsets in the game window, and anywhere is anywhere in it)
void *HostAllocatePages(size_t size, uintptr_t lowest, uintptr_t limit);
void HostFreePages(void *block);

// ---- the game window (a 64-bit host; GameWindow.cpp): the 4 GB of address space from kGameWindowBase, which every
// address the game keeps in its files' data must lie in (Ptr32.h). HostReserveLow reserves `size` bytes, inaccessible,
// at the lowest free address at or above the offset `lowest` that ends in the window (NULL if there is none);
// HostCommit makes part of it readable and writable. The heap functions above allocate there, the pages and the
// stacks of the threads HostCreateThread starts stay in the window.
#if UINTPTR_MAX > 0xffffffffu
void *HostReserveLow(size_t size, uintptr_t lowest);
int HostCommit(void *address, size_t size);
#endif

// ---- files, by host path. Access, share, disposition and attributes are CreateFile's.
HostHandle HostCreateFile(const char *path, HostDword access, HostDword share, HostDword disposition,
                          HostDword attributes);
int HostDeleteFile(const char *path);
int HostReadFile(HostHandle file, void *buffer, HostDword bytes, HostDword *read);
int HostWriteFile(HostHandle file, const void *buffer, HostDword bytes, HostDword *written);
HostDword HostSetFilePointer(HostHandle file, HostLong distance, HostLong *distanceHigh, HostDword method);
HostDword HostGetFileSize(HostHandle file, HostDword *sizeHigh);
int HostGetFileSizeEx(HostHandle file, int64_t *size);
int HostFlushFile(HostHandle file);       // FlushFileBuffers
int HostSetEndOfFile(HostHandle file);    // SetEndOfFile: the file ends at its pointer

// GetFileAttributes: FILE_ATTRIBUTE_DIRECTORY (0x10) or, for a file, Win32's own (FILE_ATTRIBUTE_NORMAL, 0x80, on
// POSIX); HOST_INVALID_FILE_ATTRIBUTES if there is nothing there
#define HOST_INVALID_FILE_ATTRIBUTES 0xffffffffu
HostDword HostGetFileAttributes(const char *path);
int HostCreateDirectory(const char *path);
int HostRemoveDirectory(const char *path);        // an empty one
int HostSetCurrentDirectory(const char *path);
// GetFullPathName: the path made absolute against the current directory (its length, 0 if it does not fit)
HostDword HostGetFullPathName(const char *path, HostDword size, char *out);

// GetDiskFreeSpace and GetDiskFreeSpaceEx: the volume a path is on, or (NULL) the current directory's
int HostGetDiskFreeSpace(const char *path, HostDword *sectorsPerCluster, HostDword *bytesPerSector,
                         HostDword *freeClusters, HostDword *totalClusters);
int HostGetDiskFreeSpaceEx(const char *path, uint64_t *availableBytes, uint64_t *totalBytes, uint64_t *freeBytes);

// A directory search: the pattern's last component may hold * and ?, matched without regard to case. Each call
// gives the next entry's name (at most HOST_MAX_PATH bytes with its terminator), sorted the same way on every host:
// by their bytes with a-z as A-Z (NTFS's order for ASCII names), then as they are. NTFS lists names so, but FAT,
// network drives and POSIX directories list them as they lie, and the game keeps the first save it finds
// (Menu_EnumSaves).
HostHandle HostFindFirstFile(const char *pattern, char *name);
int HostFindNextFile(HostHandle search, char *name);
int HostFindClose(HostHandle search);

// GetPrivateProfileString: a key of an .ini file's section, or the default
HostDword HostGetProfileString(const char *section, const char *key, const char *defaultValue, char *value,
                               HostDword size, const char *file);

// ---- time
int HostQueryPerformanceCounter(int64_t *counter);
int HostQueryPerformanceFrequency(int64_t *frequency);
HostDword HostTimeMs(void);   // timeGetTime: milliseconds, wrapping

// The multimedia timer: a callback on a thread of the host's every delay ms (periodic, flags 1) or once.
// HostRaiseTimerResolution asks for 1 ms scheduling for the whole process.
int HostRaiseTimerResolution(void);
unsigned HostSetTimerEvent(unsigned delayMs, unsigned resolutionMs, HostTimerCallback callback, HostUlongPtr user,
                           unsigned flags);

struct HostSystemTime {          // SYSTEMTIME
    uint16_t year, month, dayOfWeek, day, hour, minute, second, milliseconds;
};
struct HostTimeZone {            // TIME_ZONE_INFORMATION
    HostLong bias;
    uint16_t standardName[32];
    HostSystemTime standardDate;
    HostLong standardBias;
    uint16_t daylightName[32];
    HostSystemTime daylightDate;
    HostLong daylightBias;
};
void HostGetLocalTime(HostSystemTime *time);
HostDword HostGetTimeZoneInformation(HostTimeZone *zone);

// ---- the process
// The command line as narrow strings, argv[0] first; the POSIX body is told it by main (HostSetCommandLine).
char **HostCommandLine(int *count);
void HostSetCommandLine(int count, char **arguments);
// An environment variable's value, its length (0 if unset)
HostDword HostGetEnvironment(const char *name, char *value, HostDword size);
void HostExitProcess(unsigned exitCode);
void HostDebugBreak(void);
// RaiseException: the C++ runtime's throw (CRuntimeStd.cpp)
void HostRaiseException(HostDword code, HostDword flags, HostDword count, const HostUlongPtr *arguments);
// IsBadReadPtr / IsBadWritePtr
int HostIsBadReadPtr(const void *pointer, size_t bytes);
int HostIsBadWritePtr(void *pointer, size_t bytes);

#endif // DRIVING_PLATFORM_HOST_H_
