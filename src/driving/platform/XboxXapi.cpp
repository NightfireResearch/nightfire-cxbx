#include "XboxXapi.h"
#include "Host.h"
#include "KernelImports.h"

#include "../../common/xboxPath.h"
#include "../devtools/Teleport.h"

#include "Win32Types.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// XAPI, the Xbox's cut-down Win32, replaced by the host's own (Host.h; on Windows, Win32 itself).
//
// The driving engine reaches XAPI from EA's platform layer - THREAD and SIGNAL (threads and events), FILESYS and
// FILE (files), MEM and UMemory (contiguous memory), the sound driver's threads - and from the C runtime, whose
// malloc, free and realloc sit on XAPI's heap. Each of those XAPI functions is a thin wrapper over a kernel call
// the loader already implements with the host's Win32 (src/loader/kernel.cpp, file.cpp), and every handle the
// loader hands out is a Win32 handle. So each can be replaced by its Win32 namesake, through the host layer whose
// Windows body is exactly those calls: the arguments are the same, a handle made by one side can be used or closed
// by the other, and the game reads errors through GetLastError, which is the host's already (Xbox_GetLastError in
// XboxStartup.cpp).
//
// What each was is from its code: the kernel calls it makes and its callers, since Ghidra has names for only some
// of them. The addresses are the functions' entry points in Driving.xbe; each is patched to jump here.
//
// THE HEAP. XAPI's heap (RtlCreateHeap and its siblings, 0x00110847..0x001124b8) is NT's, and so is Win32's, with
// the same flag bits; the game's malloc, calloc, free, realloc and _msize reached it only through the five entry
// points below, and ours (CRuntime.cpp) call the host's directly with the same arguments. Patched together before the
// heap is made (Inject_XboxXapi runs before the startup thread), every block is allocated and freed by the same
// Win32 heap. The process heap handle (0x0024b218 in the game) is
// XboxStartup.cpp's ProcessHeapHandle, made by HeapCreate there.
//
// THE KERNEL. XPhysicalAlloc and the two kernel thunks the game calls (MmFreeContiguousMemory and
// ExQueryNonVolatileSetting) go to the loader's kernel exports through the XBE's import table, as the
// originals did: contiguous memory is allocated and freed by the loader, which keeps the two paired.
// ---------------------------------------------------------------------------------------------------------------


// ---- the heap

// XapiCreateHeap (0x001118dd): RtlCreateHeap(flags, base, reserve, commit, lock, parameters). Flag 2 is
// HEAP_GROWABLE; Win32 makes a heap growable by giving it no maximum. The base, lock and parameters are the
// console's; Win32 chooses its own.
static void *__stdcall Xbox_XapiCreateHeap(ULONG flags, void *base, ULONG reserve, ULONG commit, void *lock,
                                           void *parameters) {
    (void)base; (void)lock; (void)parameters;
    return HostHeapCreate(0, commit, (flags & 2) ? 0 : reserve);
}

// RtlAllocateHeap (0x00111d01), RtlFreeHeap (0x001124b8), RtlReAllocateHeap (0x001126ac), RtlSizeHeap
// (0x0011135f): HEAP_NO_SERIALIZE (1), HEAP_GENERATE_EXCEPTIONS (4), HEAP_ZERO_MEMORY (8) and
// HEAP_REALLOC_IN_PLACE_ONLY (0x10) are the same bits on both. Freeing null succeeds, as NT's does.
static void *__stdcall Xbox_RtlAllocateHeap(HANDLE heap, ULONG flags, SIZE_T size) {
    return HostHeapAlloc(heap, flags, size);
}

static BOOLEAN __stdcall Xbox_RtlFreeHeap(HANDLE heap, ULONG flags, void *block) {
    if (block == NULL)
        return TRUE;
    return HostHeapFree(heap, flags, block) ? TRUE : FALSE;
}

static void *__stdcall Xbox_RtlReAllocateHeap(HANDLE heap, ULONG flags, void *block, SIZE_T size) {
    return HostHeapReAlloc(heap, flags, block, size);
}

static SIZE_T __stdcall Xbox_RtlSizeHeap(HANDLE heap, ULONG flags, void *block) {
    return HostHeapSize(heap, flags, block);
}

// ---- contiguous memory and the kernel thunks

// XPhysicalAlloc (0x0010e7e9): MmAllocateContiguousMemoryEx(size, lowest, highest, alignment, protect). As the
// original: with no particular address asked for (-1) the block may go anywhere; with one, exactly that range,
// unaligned. ERROR_NOT_ENOUGH_MEMORY when it fails.
void *__stdcall Xbox_XPhysicalAlloc(ULONG size, ULONG address, ULONG alignment, ULONG protect) {
    ULONG lowest = 0, highest = 0xffffffffu;
    if (address != 0xffffffffu) {
        alignment = 0;
        lowest = address;
        highest = size + address - 1;
    }
    void *block = KernelMmAllocateContiguousMemoryEx(size, lowest, highest, alignment, protect);
    if (block == NULL)
        HostSetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return block;
}

// The thunks at 0x0010e82c and 0x001143e0, each a JMP through its import slot.
void __stdcall Xbox_MmFreeContiguousMemory(void *base) {
    KernelMmFreeContiguousMemory(base);
}

static LONG __stdcall Xbox_ExQueryNonVolatileSetting(ULONG index, ULONG *type, void *value, ULONG length,
                                                     ULONG *resultLength) {
    return KernelExQueryNonVolatileSetting(index, type, value, length, resultLength);
}

// HalBootSMCVideoMode (kernel ordinal 356), a data export, is 1 when the console booted into an HD-capable mode.

// XGetAVPack (0x0010e002): the cable plugged in (setting 0x103, its second byte; 3 is HDTV component), 0 if the
// setting cannot be read.
static DWORD Xbox_XGetAVPack(void) {
    ULONG type, value;
    if (Xbox_ExQueryNonVolatileSetting(0x103, &type, &value, 4, NULL) < 0)
        return 0;
    return value >> 8 & 0xff;
}

// XGetVideoFlags (0x0010e02b): the dashboard's video settings (setting 8, bits 16..22 masked to 0x5f), without
// the HD modes (480p, 720p, 1080i: 2, 4, 8) unless the console booted HD-capable on a component cable.
DWORD Xbox_XGetVideoFlags(void) {
    ULONG type, value;
    DWORD flags = Xbox_ExQueryNonVolatileSetting(8, &type, &value, 4, NULL) < 0 ? 0 : value >> 16 & 0x5f;
    if (*KernelHalBootSMCVideoMode() != 1 || Xbox_XGetAVPack() != 3)
        flags &= 0xfffffff1;
    return flags;
}

// ---- files

// CreateFileA (0x0010f76c). The path is an Xbox one ("d:\driving\..."), resolved onto the host as the loader
// resolves NtCreateFile's (src/loader/file.cpp); a share mode of zero opens shared, as there.
HANDLE __stdcall Xbox_CreateFileA(const char *path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *security,
                                         DWORD disposition, DWORD flags, HANDLE templateFile) {
    (void)security; (void)templateFile;
    char hostPath[MAX_PATH];
    if (path == NULL || !Xbox_ResolvePath(path, hostPath, sizeof(hostPath))) {
        HostSetLastError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    if (share == 0)
        share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    return HostCreateFile(hostPath, access, share, disposition, flags);
}

// DeleteFileA (0x0010fd88).
BOOL __stdcall Xbox_DeleteFileA(const char *path) {
    char hostPath[MAX_PATH];
    if (path == NULL || !Xbox_ResolvePath(path, hostPath, sizeof(hostPath))) {
        HostSetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    return HostDeleteFile(hostPath);
}

// ReadFile (0x0010f2c3), WriteFile (0x0010f3b0), SetFilePointer (0x0010f50a), GetFileSizeEx (0x0010f6f3),
// GetFileSize (0x0010f731), CloseHandle (0x0010e9b9). Nothing reads or writes asynchronously: no caller passes an
// OVERLAPPED.
BOOL __stdcall Xbox_ReadFile(HANDLE file, void *buffer, DWORD bytes, DWORD *read, OVERLAPPED *overlapped) {
    (void)overlapped;
    return HostReadFile(file, buffer, bytes, read);
}

BOOL __stdcall Xbox_WriteFile(HANDLE file, const void *buffer, DWORD bytes, DWORD *written, OVERLAPPED *overlapped) {
    (void)overlapped;
    return HostWriteFile(file, buffer, bytes, written);
}

DWORD __stdcall Xbox_SetFilePointer(HANDLE file, LONG distance, LONG *distanceHigh, DWORD method) {
    return HostSetFilePointer(file, distance, distanceHigh, method);
}

static BOOL __stdcall Xbox_GetFileSizeEx(HANDLE file, LARGE_INTEGER *size) {
    return HostGetFileSizeEx(file, &size->QuadPart);
}

DWORD __stdcall Xbox_GetFileSize(HANDLE file, DWORD *sizeHigh) {
    return HostGetFileSize(file, sizeHigh);
}

BOOL __stdcall Xbox_CloseHandle(HANDLE handle) {
    return HostCloseHandle(handle);
}

// ---- events, waits and sleeping

// CreateEventA (0x0010e864), SetEvent (0x0010e8c5), ResetEvent (0x0010e8e5), WaitForSingleObjectEx (0x0010e903),
// WaitForSingleObject (0x0010e999), SleepEx (0x0010e947, "SleepMs" in Ghidra), Sleep (0x0010e9ab,
// "SleepMilliseconds"). Event names are ignored, as nothing here opens one by name.
HANDLE __stdcall Xbox_CreateEventA(SECURITY_ATTRIBUTES *security, BOOL manualReset, BOOL initialState,
                                   const char *name) {
    (void)security; (void)name;
    return HostCreateEvent(manualReset, initialState);
}

BOOL __stdcall Xbox_SetEvent(HANDLE event) {
    return HostSetEvent(event);
}

BOOL __stdcall Xbox_ResetEvent(HANDLE event) {
    return HostResetEvent(event);
}

static DWORD __stdcall Xbox_WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable) {
    return HostWaitForObjectEx(handle, milliseconds, alertable);
}

DWORD __stdcall Xbox_WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
    return HostWaitForObject(handle, milliseconds);
}

DWORD __stdcall Xbox_SleepEx(DWORD milliseconds, BOOL alertable) {
    return HostSleepEx(milliseconds, alertable);
}

void __stdcall Xbox_Sleep(DWORD milliseconds) {
    HostSleep(milliseconds);
}

// ---- threads

// CreateThread (0x0010ec6a). The original starts the thread in XapiThreadStartup (0x0010ebd2), which builds its
// TLS block through the Xbox KPCR and calls the thread-notify routines; the loader's PsCreateSystemThreadEx
// already starts the thread at its own routine instead (src/loader/kernel.cpp), the XBE's TLS template is empty
// (0x001a1ddc: no data, twelve bytes of zero fill), and nothing registers a notify routine. So this is Win32's
// CreateThread with the same arguments, which is what a thread got before. ExitThread (0x0010eadd) likewise.
HANDLE __stdcall Xbox_CreateThread(SECURITY_ATTRIBUTES *security, SIZE_T stackSize,
                                   LPTHREAD_START_ROUTINE start, void *parameter, DWORD flags,
                                   DWORD *threadId) {
    (void)security;
    return HostCreateThread(stackSize, start, parameter, flags & CREATE_SUSPENDED, threadId);
}

void __stdcall Xbox_ExitThread(DWORD exitCode) {
    HostExitThread(exitCode);
}

// ResumeThread (0x0010ea61, "THREAD_resume" in Ghidra, though it is XAPI's: NtResumeThread, then the previous
// suspend count, or -1 with the error set). EA's THREAD_create starts its threads suspended and resumes them here.
DWORD __stdcall Xbox_ResumeThread(HANDLE thread) {
    return HostResumeThread(thread);
}

// ---- time

// GetTimeZoneInformation (0x0010e4d2) and GetLocalTime (0x0010e684, which goes through the time-zone helper at
// 0x0010e62e). The original reads the zone from the console's settings; the host's own is the user's.
static DWORD __stdcall Xbox_GetTimeZoneInformation(TIME_ZONE_INFORMATION *zone) {
    return HostGetTimeZoneInformation(reinterpret_cast<HostTimeZone *>(zone));   // the same layout
}

// Its one caller is the dashboard clock (RVehicle::UpdatePosition, 0x000964b0).
void __stdcall Xbox_GetLocalTime(SYSTEMTIME *time) {
    if (Teleport_LockstepLocalTime(time))   // NIGHTFIRE_LOCKSTEP: the same time in every run
        return;
    HostGetLocalTime(reinterpret_cast<HostSystemTime *>(time));   // the same layout
}

// ---- the rest

// IsBadReadPtr (0x0014bf05) and IsBadWritePtr (0x0014bf6a): the original touches every page of the range under
// a structured exception handler. Win32's does the same.
BOOL __stdcall Xbox_IsBadReadPtr(const void *pointer, UINT_PTR bytes) {
    return HostIsBadReadPtr(pointer, bytes);
}

static BOOL __stdcall Xbox_IsBadWritePtr(void *pointer, UINT_PTR bytes) {
    return HostIsBadWritePtr(pointer, bytes);
}

// OutputDebugStringA (0x0010e832): the original hands the string to the kernel debugger (INT 2D), which a
// run under the loader does not have; the console is where this project's logging goes.
void __stdcall Xbox_OutputDebugStringA(const char *text) {
    if (text != NULL) {
        fputs(text, stdout);
        fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------------------------

#ifndef NIGHTFIRE_STANDALONE   // the injected build: the original's entry points patched to these
static void WriteJump(unsigned address, const void *target) {
    unsigned char *site = (unsigned char *)address;
    DWORD previous = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[xapi] could not make 0x%08x writable (error %lu)\n", address, GetLastError());
        return;
    }
    site[0] = 0xE9;                                             // jmp rel32
    *(int *)(site + 1) = (int)((const unsigned char *)target - (site + 5));
    VirtualProtect(site, 5, previous, &previous);
}
#endif   // NIGHTFIRE_STANDALONE

// The files here are resolved by this module's copy of xboxPath.cpp, so it takes settings.ini's DiscPath as the
// loader takes it for its own copy (ApplyDiscPathSetting in src/loader/loadermain.cpp): a ';' or '#' starts a
// comment, trailing whitespace goes, and empty leaves the default ("../disc" beside the executables).
void Xbox_ApplyDiscPathSetting(void) {
    char discPath[MAX_PATH];
    HostGetProfileString("Settings", "DiscPath", "", discPath, sizeof(discPath), ".\\settings.ini");
    discPath[strcspn(discPath, ";#")] = '\0';
    for (size_t length = strlen(discPath); length > 0 && (discPath[length - 1] == ' ' || discPath[length - 1] == '\t');)
        discPath[--length] = '\0';
    Xbox_SetDiscRoot(discPath);
}

#ifndef NIGHTFIRE_STANDALONE   // the injected build: the original's entry points patched to these
void Inject_XboxXapi(void) {
    Xbox_ApplyDiscPathSetting();
    WriteJump(0x001118dd, (const void *)Xbox_XapiCreateHeap);
    WriteJump(0x00111d01, (const void *)Xbox_RtlAllocateHeap);
    WriteJump(0x001124b8, (const void *)Xbox_RtlFreeHeap);
    WriteJump(0x001126ac, (const void *)Xbox_RtlReAllocateHeap);
    WriteJump(0x0011135f, (const void *)Xbox_RtlSizeHeap);
    WriteJump(0x0010e7e9, (const void *)Xbox_XPhysicalAlloc);
    WriteJump(0x0010e82c, (const void *)Xbox_MmFreeContiguousMemory);
    WriteJump(0x001143e0, (const void *)Xbox_ExQueryNonVolatileSetting);
    WriteJump(0x0010e002, (const void *)Xbox_XGetAVPack);
    WriteJump(0x0010e02b, (const void *)Xbox_XGetVideoFlags);
    WriteJump(0x0010f76c, (const void *)Xbox_CreateFileA);
    WriteJump(0x0010fd88, (const void *)Xbox_DeleteFileA);
    WriteJump(0x0010f2c3, (const void *)Xbox_ReadFile);
    WriteJump(0x0010f3b0, (const void *)Xbox_WriteFile);
    WriteJump(0x0010f50a, (const void *)Xbox_SetFilePointer);
    WriteJump(0x0010f6f3, (const void *)Xbox_GetFileSizeEx);
    WriteJump(0x0010f731, (const void *)Xbox_GetFileSize);
    WriteJump(0x0010e9b9, (const void *)Xbox_CloseHandle);
    WriteJump(0x0010e864, (const void *)Xbox_CreateEventA);
    WriteJump(0x0010e8c5, (const void *)Xbox_SetEvent);
    WriteJump(0x0010e8e5, (const void *)Xbox_ResetEvent);
    WriteJump(0x0010e903, (const void *)Xbox_WaitForSingleObjectEx);
    WriteJump(0x0010e999, (const void *)Xbox_WaitForSingleObject);
    WriteJump(0x0010e947, (const void *)Xbox_SleepEx);
    WriteJump(0x0010e9ab, (const void *)Xbox_Sleep);
    WriteJump(0x0010ec6a, (const void *)Xbox_CreateThread);
    WriteJump(0x0010eadd, (const void *)Xbox_ExitThread);
    WriteJump(0x0010ea61, (const void *)Xbox_ResumeThread);
    WriteJump(0x0010e4d2, (const void *)Xbox_GetTimeZoneInformation);
    WriteJump(0x0010e684, (const void *)Xbox_GetLocalTime);
    WriteJump(0x0014bf05, (const void *)Xbox_IsBadReadPtr);
    WriteJump(0x0014bf6a, (const void *)Xbox_IsBadWritePtr);
    WriteJump(0x0010e832, (const void *)Xbox_OutputDebugStringA);
    printf("[xapi] %d XAPI functions replaced by the host's\n", 31);
}
#endif   // NIGHTFIRE_STANDALONE
