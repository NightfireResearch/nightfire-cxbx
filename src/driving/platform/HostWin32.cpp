#include "Host.h"

#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

// The host layer on Windows: each function is the Win32 call the code made before the layer existed, with the
// same arguments. Host.h's types are Win32's own (HANDLE and HostHandle are both void *, DWORD is unsigned long),
// so nothing is converted on the way through.

#ifndef _WIN64
static_assert(sizeof(CRITICAL_SECTION) <= sizeof(HostLock), "a critical section fits a HostLock");
#endif
static_assert(sizeof(HostSystemTime) == sizeof(SYSTEMTIME), "HostSystemTime is SYSTEMTIME");
static_assert(sizeof(HostTimeZone) == sizeof(TIME_ZONE_INFORMATION), "HostTimeZone is TIME_ZONE_INFORMATION");

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

// ---- errors

unsigned long HostGetLastError(void) {
    return GetLastError();
}

void HostSetLastError(unsigned long error) {
    SetLastError(error);
}

// ---- threads

HostHandle HostCreateThread(size_t stackSize, HostThreadStart start, void *parameter, unsigned long flags,
                            unsigned long *threadId) {
    return CreateThread(NULL, stackSize, start, parameter, flags, threadId);
}

unsigned long HostResumeThread(HostHandle thread) {
    return ResumeThread(thread);
}

void HostExitThread(unsigned long exitCode) {
    ExitThread(exitCode);
}

int HostSetThreadPriority(HostHandle thread, int priority) {
    return SetThreadPriority(thread, priority);
}

int HostGetExitCodeThread(HostHandle thread, unsigned long *exitCode) {
    return GetExitCodeThread(thread, exitCode);
}

unsigned long HostGetCurrentThreadId(void) {
    return GetCurrentThreadId();
}

// ---- thread-local storage

unsigned long HostTlsAlloc(void) {
    return TlsAlloc();
}

void *HostTlsGetValue(unsigned long slot) {
    return TlsGetValue(slot);
}

int HostTlsSetValue(unsigned long slot, void *value) {
    return TlsSetValue(slot, value);
}

// ---- locks

#ifndef _WIN64
void HostInitLock(HostLock *lock) {
    InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(lock->storage));
}

void HostEnterLock(HostLock *lock) {
    EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(lock->storage));
}

void HostLeaveLock(HostLock *lock) {
    LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(lock->storage));
}
#else
// A 64-bit critical section is 40 bytes, more than the lock's place in the game's records: the lock holds a pointer
// to one of its own on the system heap, which is never freed (nothing deletes a lock).
static CRITICAL_SECTION *SectionOf(HostLock *lock) {
    CRITICAL_SECTION *section;
    memcpy(&section, lock->storage, sizeof(section));
    return section;
}

void HostInitLock(HostLock *lock) {
    CRITICAL_SECTION *section = (CRITICAL_SECTION *)HeapAlloc(GetProcessHeap(), 0, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(section);
    memcpy(lock->storage, &section, sizeof(section));
}

void HostEnterLock(HostLock *lock) {
    EnterCriticalSection(SectionOf(lock));
}

void HostLeaveLock(HostLock *lock) {
    LeaveCriticalSection(SectionOf(lock));
}
#endif

long HostInterlockedIncrement(volatile long *value) {
    return InterlockedIncrement(value);
}

long HostInterlockedDecrement(volatile long *value) {
    return InterlockedDecrement(value);
}

// ---- events and waits

HostHandle HostCreateEvent(int manualReset, int initialState) {
    return CreateEventA(NULL, manualReset, initialState, NULL);
}

int HostSetEvent(HostHandle event) {
    return SetEvent(event);
}

int HostResetEvent(HostHandle event) {
    return ResetEvent(event);
}

unsigned long HostWaitForObject(HostHandle handle, unsigned long milliseconds) {
    return WaitForSingleObject(handle, milliseconds);
}

unsigned long HostWaitForObjectEx(HostHandle handle, unsigned long milliseconds, int alertable) {
    return WaitForSingleObjectEx(handle, milliseconds, alertable);
}

int HostCloseHandle(HostHandle handle) {
    return CloseHandle(handle);
}

// ---- sleeping

void HostSleep(unsigned long milliseconds) {
    Sleep(milliseconds);
}

unsigned long HostSleepEx(unsigned long milliseconds, int alertable) {
    return SleepEx(milliseconds, alertable);
}

// ---- heaps (a 64-bit host's are GameWindow.cpp's)

#if UINTPTR_MAX == 0xffffffffu
HostHandle HostHeapCreate(unsigned long flags, size_t initialSize, size_t maximumSize) {
    return HeapCreate(flags, initialSize, maximumSize);
}

void *HostHeapAlloc(HostHandle heap, unsigned long flags, size_t size) {
    return HeapAlloc(heap, flags, size);
}

int HostHeapFree(HostHandle heap, unsigned long flags, void *block) {
    return HeapFree(heap, flags, block);
}

void *HostHeapReAlloc(HostHandle heap, unsigned long flags, void *block, size_t size) {
    return HeapReAlloc(heap, flags, block, size);
}

size_t HostHeapSize(HostHandle heap, unsigned long flags, const void *block) {
    return HeapSize(heap, flags, block);
}
#endif

// ---- pages

#if UINTPTR_MAX == 0xffffffffu
void *HostAllocatePages(size_t size, uintptr_t lowest, uintptr_t limit) {
    const uintptr_t granularity = 0x10000;
    uintptr_t address = (lowest + granularity - 1) & ~(granularity - 1);
    if (address < granularity)
        address = granularity;
    while (address < limit && size <= limit - address) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((void *)address, &region, sizeof(region)) != sizeof(region))
            break;
        uintptr_t end = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (region.State == MEM_FREE && end - address >= size) {
            void *block = VirtualAlloc((void *)address, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (block != NULL)
                return block;
        }
        address = (end + granularity - 1) & ~(granularity - 1);
    }
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}
#else
// The lowest free range of `size` bytes at or above `lowest` that ends below `limit`, allocated; NULL if there is none.
static void *AllocateLowest(size_t size, uintptr_t lowest, uintptr_t limit, DWORD type, DWORD protect) {
    const uintptr_t granularity = 0x10000;
    uintptr_t address = (lowest + granularity - 1) & ~(granularity - 1);
    if (address < granularity)
        address = granularity;
    while (address < limit && size <= limit - address) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((void *)address, &region, sizeof(region)) != sizeof(region))
            break;
        uintptr_t end = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (region.State == MEM_FREE && end - address >= size) {
            void *block = VirtualAlloc((void *)address, size, type, protect);
            if (block != NULL)
                return block;
        }
        address = (end + granularity - 1) & ~(granularity - 1);
    }
    return NULL;
}

static const uintptr_t kWindowEnd = uintptr_t(1) << 32;

// Where asked if there is room there, else anywhere in the game window.
void *HostAllocatePages(size_t size, uintptr_t lowest, uintptr_t limit) {
    void *block = AllocateLowest(size, lowest, limit, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (block == NULL)
        block = AllocateLowest(size, 0, kWindowEnd, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    return block;
}

void *HostReserveLow(size_t size, uintptr_t lowest) {
    return AllocateLowest(size, lowest, kWindowEnd, MEM_RESERVE, PAGE_NOACCESS);
}

int HostCommit(void *address, size_t size) {
    return VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}
#endif

void HostFreePages(void *block) {
    if (block != NULL)
        VirtualFree(block, 0, MEM_RELEASE);
}

// ---- files

HostHandle HostCreateFile(const char *path, unsigned long access, unsigned long share, unsigned long disposition,
                          unsigned long attributes) {
    return CreateFileA(path, access, share, NULL, disposition, attributes, NULL);
}

int HostDeleteFile(const char *path) {
    return DeleteFileA(path);
}

int HostReadFile(HostHandle file, void *buffer, unsigned long bytes, unsigned long *read) {
    return ReadFile(file, buffer, bytes, read, NULL);
}

int HostWriteFile(HostHandle file, const void *buffer, unsigned long bytes, unsigned long *written) {
    return WriteFile(file, buffer, bytes, written, NULL);
}

unsigned long HostSetFilePointer(HostHandle file, long distance, long *distanceHigh, unsigned long method) {
    return SetFilePointer(file, distance, distanceHigh, method);
}

unsigned long HostGetFileSize(HostHandle file, unsigned long *sizeHigh) {
    return GetFileSize(file, sizeHigh);
}

int HostGetFileSizeEx(HostHandle file, int64_t *size) {
    LARGE_INTEGER result;
    if (!GetFileSizeEx(file, &result))
        return FALSE;
    *size = result.QuadPart;
    return TRUE;
}

static void CopyFoundName(const WIN32_FIND_DATAA *found, char *name) {
    memcpy(name, found->cFileName, sizeof(found->cFileName));
}

HostHandle HostFindFirstFile(const char *pattern, char *name) {
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search != INVALID_HANDLE_VALUE)
        CopyFoundName(&found, name);
    return search;
}

int HostFindNextFile(HostHandle search, char *name) {
    WIN32_FIND_DATAA found;
    if (!FindNextFileA(search, &found))
        return FALSE;
    CopyFoundName(&found, name);
    return TRUE;
}

int HostFindClose(HostHandle search) {
    return FindClose(search);
}

unsigned long HostGetProfileString(const char *section, const char *key, const char *defaultValue, char *value,
                                   unsigned long size, const char *file) {
    return GetPrivateProfileStringA(section, key, defaultValue, value, size, file);
}

// ---- time

int HostQueryPerformanceCounter(int64_t *counter) {
    return QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(counter));
}

int HostQueryPerformanceFrequency(int64_t *frequency) {
    return QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(frequency));
}

unsigned long HostTimeMs(void) {
    return timeGetTime();
}

int HostRaiseTimerResolution(void) {
    return timeBeginPeriod(1) == TIMERR_NOERROR;
}

unsigned HostSetTimerEvent(unsigned delayMs, unsigned resolutionMs, HostTimerCallback callback, HostUlongPtr user,
                           unsigned flags) {
    return timeSetEvent(delayMs, resolutionMs, callback, user, flags);
}

void HostGetLocalTime(HostSystemTime *time) {
    GetLocalTime(reinterpret_cast<SYSTEMTIME *>(time));
}

unsigned long HostGetTimeZoneInformation(HostTimeZone *zone) {
    return GetTimeZoneInformation(reinterpret_cast<TIME_ZONE_INFORMATION *>(zone));
}

// ---- the process

static char *Narrow(const wchar_t *wide) {
    int length = WideCharToMultiByte(CP_ACP, 0, wide, -1, NULL, 0, NULL, NULL);
    char *text = (char *)malloc(length > 0 ? length : 1);
    if (length <= 0 || WideCharToMultiByte(CP_ACP, 0, wide, -1, text, length, NULL, NULL) <= 0)
        text[0] = '\0';
    return text;
}

// The process's own command line, narrowed once; the strings are kept for the life of the process.
char **HostCommandLine(int *count) {
    static char **arguments = NULL;
    static int argumentCount = 0;
    if (arguments == NULL) {
        int wideCount = 0;
        wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        if (wide == NULL) {
            *count = 0;
            return NULL;
        }
        arguments = (char **)malloc(sizeof(char *) * (wideCount + 1));
        for (int i = 0; i < wideCount; i++)
            arguments[i] = Narrow(wide[i]);
        arguments[wideCount] = NULL;
        argumentCount = wideCount;
        LocalFree(wide);
    }
    *count = argumentCount;
    return arguments;
}

// Windows keeps the command line itself.
void HostSetCommandLine(int count, char **arguments) {
    (void)count;
    (void)arguments;
}

unsigned long HostGetEnvironment(const char *name, char *value, unsigned long size) {
    return GetEnvironmentVariableA(name, value, size);
}

void HostExitProcess(unsigned exitCode) {
    ExitProcess(exitCode);
}

void HostDebugBreak(void) {
    DebugBreak();
}

void HostRaiseException(unsigned long code, unsigned long flags, unsigned long count, const HostUlongPtr *arguments) {
    RaiseException(code, flags, count, arguments);
}

int HostIsBadReadPtr(const void *pointer, size_t bytes) {
    return IsBadReadPtr(pointer, bytes);
}

int HostIsBadWritePtr(void *pointer, size_t bytes) {
    return IsBadWritePtr(pointer, bytes);
}
