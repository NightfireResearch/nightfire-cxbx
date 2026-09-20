#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>

#include "kernel.h"
#include "file.h"
#include "kernel_ordinals.inc"

// ---------------------------------------------------------------------------------------------------------------
// The xboxkrnl imports, as far as the loader provides them.
//
// The XBE reaches the kernel through a thunk table of ordinals, which the loader rewrites into function
// pointers. The XBE imports 96; the ones below are implemented and every other ordinal resolves to a
// generated stub that reports which import was called, and by whom, and stops.
//
// They were added one at a time, driven by what the stub reported, rather than written on spec - which is
// cheap because the static inventory (tools/kernel_imports.py) is right that game code reaches very few of
// them directly, and because a stub that stops on the first call turns "the screen went away" into a line
// naming the function and the address that wanted it.
//
// Stopping rather than returning is on purpose. These are __stdcall functions with parameter counts this table
// does not know, so a stub cannot clean up the caller's stack; returning would corrupt it and the crash would
// land somewhere unrelated to the cause.
// ---------------------------------------------------------------------------------------------------------------

// A small block of generated trampolines, one per thunk slot, so that a single common handler can still say
// which ordinal it was reached through. Each is:
//
//     68 <ordinal>    push ordinal
//     E9 <rel32>      jmp KernelStubEntry
//
// The game reaches these through "call dword ptr [thunk]", so on entry the stack is the caller's return
// address, and the push puts the ordinal above it. Because the trampoline jumps rather than calls, there is
// no second return address: KernelStubEntry sees [esp] = ordinal and [esp+4] = the caller's return address.
// That is why it is naked and reads both by hand rather than being an ordinary function - taking an argument
// the normal way would read the return address and report it as the ordinal.
//
#pragma pack(push, 1)
struct KernelTrampoline {
    uint8_t  pushOpcode;
    uint32_t ordinal;
    uint8_t  jmpOpcode;
    int32_t  relativeTarget;
};
#pragma pack(pop)

static KernelTrampoline *g_trampolines = NULL;
static unsigned g_trampolineCount = 0;
#define KERNEL_MAX_ORDINAL 512

static void __cdecl KernelStubReport(unsigned ordinal, uint32_t calledFrom) {
    printf("\n[loader] ---------------------------------------------------------------\n");
    printf("[loader] unimplemented kernel import: %s (ordinal %u)\n", KernelOrdinalName(ordinal), ordinal);
    printf("[loader] called from 0x%08x\n", calledFrom);
    printf("[loader] The game got this far and then needed a kernel function the loader\n"
           "[loader] does not provide. Implement it in src/loader/kernel.cpp and run again.\n");
    printf("[loader] ---------------------------------------------------------------\n\n");
    fflush(stdout);

    // Deliberately not returning - see the note above about stack cleanup. A breakpoint gives a debugger
    // somewhere useful to stop; without one attached this exits rather than running on into nonsense.
    if (IsDebuggerPresent())
        DebugBreak();
    ExitProcess(1);
}

// Entered by a jmp from a trampoline, so [esp] is the ordinal it pushed and [esp+4] the caller's return
// address. Nothing is preserved because this never returns.
static void __declspec(naked) KernelStubEntry(void) {
    __asm {
        push [esp + 4]      // the caller's return address - which game function wanted this
        push [esp + 4]      // the ordinal the trampoline pushed (now one slot further down)
        call KernelStubReport
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The imports that are implemented.
//
// These are added one at a time, driven by what the stub table above reports, rather than written on spec.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_STATUS_SUCCESS       ((LONG)0x00000000)
#define XBOX_STATUS_UNSUCCESSFUL  ((LONG)0xC0000001)

typedef void (__stdcall *XboxStartRoutine)(void *context);

// Ordinal 255. Ten parameters - counted from the call site at 0x000ee08e, not from a prototype.
//
// SystemRoutine is deliberately ignored. On the Xbox it is XapiThreadStartup, which builds the thread's TLS
// block by walking the KPCR through FS:[0x28] to the current KTHREAD and taking the TLS pointer from +0x28 -
// a layout a Win32 thread simply does not have, since FS there is an ordinary TEB. Rather than fabricate a
// KPCR per thread, the startup it would perform is replaced wholesale (see mainXapiStartup in
// src/action/engine/XboxStartup.cpp), so the thread can start directly at StartRoutine.
//
// ThreadExtensionSize and TlsDataSize go the same way for the same reason. KernelStackSize is real, though -
// the game passes its XBE header's stack commit, and honouring it costs nothing.
static LONG __stdcall Xbox_PsCreateSystemThreadEx(HANDLE *threadHandle,
                                                  ULONG threadExtensionSize,
                                                  ULONG kernelStackSize,
                                                  ULONG tlsDataSize,
                                                  HANDLE *threadId,
                                                  XboxStartRoutine startRoutine,
                                                  void *startContext,
                                                  BOOLEAN createSuspended,
                                                  BOOLEAN debuggerThread,
                                                  void *systemRoutine) {
    (void)threadExtensionSize;
    (void)tlsDataSize;
    (void)debuggerThread;
    (void)systemRoutine;

    DWORD id = 0;
    HANDLE thread = CreateThread(NULL, kernelStackSize,
                                 (LPTHREAD_START_ROUTINE)startRoutine, startContext,
                                 createSuspended ? CREATE_SUSPENDED : 0, &id);
    if (thread == NULL) {
        printf("[loader] PsCreateSystemThreadEx: CreateThread failed (error %lu)\n", GetLastError());
        return XBOX_STATUS_UNSUCCESSFUL;
    }

    printf("[loader] PsCreateSystemThreadEx: thread %lu started at 0x%08x\n",
           id, (unsigned)(uintptr_t)startRoutine);
    fflush(stdout);

    if (threadHandle != NULL)
        *threadHandle = thread;
    else
        CloseHandle(thread);
    if (threadId != NULL)
        *threadId = (HANDLE)(uintptr_t)id;
    return XBOX_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------
// Virtual memory.
//
// The XBE brings its own heap manager - the game's malloc and free are its, and its blocks are the only ones
// its free understands - so the heap is left alone and simply given pages to work with. That makes these four
// the whole of the memory interface, and each is a thin translation of an NT-shaped call onto the Win32 one.
//
// The shape differences are worth naming, because they are where a silent bug would live: the sizes are
// in-out parameters that the caller reads back after the call, the base address is an in-out parameter too,
// and success is a zero NTSTATUS rather than a non-null pointer.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_STATUS_NO_MEMORY            ((LONG)0xC0000017)
#define XBOX_STATUS_INVALID_PARAMETER    ((LONG)0xC000000D)

// ---------------------------------------------------------------------------------------------------------------
// Every page the game gets is executable, because on the console every page is.
//
// An Xbox title runs in ring 0 with no data execution prevention, and this one relies on it: EAGL compiles
// each model's render method into a stream of its own and calls into the result, so the game's heap holds
// code. Under Windows those pages are PAGE_READWRITE and the first model drawn faults trying to execute one -
// which is what happened, with the loader reporting "tried to access 0x0423ed00, protection 0x4" from inside
// EAGL::Model::Draw.
//
// The loader is also linked /NXCOMPAT:NO, which turns DEP off for the process as a whole; this is the other
// half of the same decision, and it is the half that does not depend on the system's DEP policy.
// ---------------------------------------------------------------------------------------------------------------

static DWORD ExecutableProtection(DWORD protect) {
    switch (protect & 0xFF) {
        case PAGE_NOACCESS:
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return protect;                                    // already executable, or deliberately nothing
        case PAGE_READONLY:
            return (protect & ~0xFFu) | PAGE_EXECUTE_READ;
        default:
            return (protect & ~0xFFu) | PAGE_EXECUTE_READWRITE;
    }
}

// Ordinal 184. Note there is no process handle: an Xbox title is the only process there is.
static LONG __stdcall Xbox_NtAllocateVirtualMemory(void **baseAddress, ULONG zeroBits, ULONG *allocationSize,
                                                   DWORD allocationType, DWORD protect) {
    (void)zeroBits;
    if (baseAddress == NULL || allocationSize == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;

    void *result = VirtualAlloc(*baseAddress, *allocationSize, allocationType,
                                ExecutableProtection(protect));
    if (result == NULL)
        return XBOX_STATUS_NO_MEMORY;

    // The caller reads both of these back. Rounding matters: it asked for a size, and what it got was rounded
    // up to a page, and its heap will hand out every byte it is told it has.
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery(result, &mbi, sizeof(mbi)) == sizeof(mbi))
        *allocationSize = (ULONG)mbi.RegionSize;
    else
        *allocationSize = (*allocationSize + 0xfff) & ~0xfffu;
    *baseAddress = result;
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 199. MEM_RELEASE requires a size of zero and the exact base that was allocated, which is what the
// caller passes; MEM_DECOMMIT takes a range.
static LONG __stdcall Xbox_NtFreeVirtualMemory(void **baseAddress, ULONG *freeSize, ULONG freeType) {
    if (baseAddress == NULL || freeSize == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;

    SIZE_T size = (freeType & MEM_RELEASE) ? 0 : *freeSize;
    if (!VirtualFree(*baseAddress, size, freeType))
        return XBOX_STATUS_INVALID_PARAMETER;
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 204. It was registered as 205 until the ordinal table was regenerated from Cxbx-Reloaded's kernel
// thunk array rather than its headers: the header annotates this function EXPORTNUM(205), which is
// NtPulseEvent's number, and a game calling NtPulseEvent would have arrived here instead.
static LONG __stdcall Xbox_NtProtectVirtualMemory(void **baseAddress, ULONG *regionSize, ULONG newProtect,
                                                  ULONG *oldProtect) {
    if (baseAddress == NULL || regionSize == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;

    DWORD previous = 0;
    if (!VirtualProtect(*baseAddress, *regionSize, ExecutableProtection(newProtect), &previous))
        return XBOX_STATUS_INVALID_PARAMETER;
    if (oldProtect != NULL)
        *oldProtect = previous;
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 217. The Xbox version takes no size, because MEMORY_BASIC_INFORMATION is a fixed shape.
static LONG __stdcall Xbox_NtQueryVirtualMemory(void *baseAddress, MEMORY_BASIC_INFORMATION *buffer) {
    if (buffer == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;
    if (VirtualQuery(baseAddress, buffer, sizeof(*buffer)) != sizeof(*buffer))
        return XBOX_STATUS_INVALID_PARAMETER;
    return XBOX_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------
// Pool memory.
//
// On the console this is the kernel's non-paged pool. Here it is an ordinary private heap - private rather
// than the process default heap so that a mismatched free is caught by the heap that owns the block instead
// of quietly damaging something else's.
//
// The Xbox's pool aligns allocations of a page or more to a page boundary, which this does not reproduce.
// Nothing here needs it: the only caller so far is DirectSound's own memory manager, and its buffers are read
// as plain memory by the XAudio2 backend rather than being handed to hardware that cares.
// ---------------------------------------------------------------------------------------------------------------

static HANDLE g_poolHeap = NULL;

static void *__stdcall Xbox_ExAllocatePoolWithTag(ULONG numberOfBytes, ULONG tag) {
    (void)tag;   // a debugging aid on the console; nothing reads it back

    if (g_poolHeap == NULL) {
        g_poolHeap = HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
        if (g_poolHeap == NULL) {
            printf("[loader] ExAllocatePoolWithTag: no pool heap (error %lu)\n", GetLastError());
            return NULL;
        }
    }
    return HeapAlloc(g_poolHeap, 0, numberOfBytes);
}

static void *__stdcall Xbox_ExAllocatePool(ULONG numberOfBytes) {
    return Xbox_ExAllocatePoolWithTag(numberOfBytes, 0);
}

static void __stdcall Xbox_ExFreePool(void *p) {
    if (p != NULL && g_poolHeap != NULL)
        HeapFree(g_poolHeap, 0, p);
}

// Ordinal 23. DirectSound's memory manager asks this immediately after allocating, so that it can use
// whatever the pool rounded the request up to rather than only what it asked for. HeapSize answers exactly
// that question, so the rounding stays truthful instead of the block being trusted for more than it has.
static ULONG __stdcall Xbox_ExQueryPoolBlockSize(void *poolBlock) {
    if (poolBlock == NULL || g_poolHeap == NULL)
        return 0;
    SIZE_T size = HeapSize(g_poolHeap, 0, poolBlock);
    return (size == (SIZE_T)-1) ? 0 : (ULONG)size;
}

// ---------------------------------------------------------------------------------------------------------------
// Contiguous physical memory.
//
// On the console these hand back physically contiguous pages, because the GPU reads them directly - the push
// buffer and texture data live here. Nothing reads them directly any more: the D3D9 backend copies whatever
// the game puts in them into real Direct3D resources, so what the game actually needs is plain memory of the
// right size at a stable address, and the physical-address constraints are noise.
//
// The alignment argument is honoured, though, because the game does rely on it - the nv2a wanted its push
// buffer aligned and the game still asks. VirtualAlloc's 64 KB granularity covers every alignment the game
// requests, so a plain allocation satisfies it, and the check below says so rather than assuming it.
// ---------------------------------------------------------------------------------------------------------------

// Ordinal 166.
static void *__stdcall Xbox_MmAllocateContiguousMemoryEx(ULONG numberOfBytes,
                                                         ULONG lowestAcceptableAddress,
                                                         ULONG highestAcceptableAddress,
                                                         ULONG alignment,
                                                         ULONG protectionType) {
    (void)lowestAcceptableAddress;
    (void)highestAcceptableAddress;
    (void)protectionType;

    void *memory = VirtualAlloc(NULL, numberOfBytes, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (memory == NULL) {
        printf("[loader] MmAllocateContiguousMemoryEx: %lu bytes refused (error %lu)\n",
               numberOfBytes, GetLastError());
        return NULL;
    }

    if (alignment > 1 && ((uintptr_t)memory & (alignment - 1)) != 0) {
        // Not expected - VirtualAlloc returns 64 KB-aligned addresses - but silently mis-aligned memory would
        // show up much later as corrupt geometry rather than as a failed allocation.
        printf("[loader] MmAllocateContiguousMemoryEx: %p is not aligned to 0x%lx\n", memory, alignment);
    }
    return memory;
}

// Ordinal 171.
static void __stdcall Xbox_MmFreeContiguousMemory(void *baseAddress) {
    if (baseAddress != NULL)
        VirtualFree(baseAddress, 0, MEM_RELEASE);
}

// Ordinal 178. Persistence means surviving a launch into another title, through the console's soft reboot.
// Nothing survives here, and nothing asks it to.
static void __stdcall Xbox_MmPersistContiguousMemory(void *baseAddress, ULONG numberOfBytes, BOOLEAN persist) {
    (void)baseAddress;
    (void)numberOfBytes;
    (void)persist;
}

// Ordinal 180.
static ULONG __stdcall Xbox_MmQueryAllocationSize(void *baseAddress) {
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery(baseAddress, &mbi, sizeof(mbi)) != sizeof(mbi))
        return 0;
    return (ULONG)mbi.RegionSize;
}

// ---------------------------------------------------------------------------------------------------------------
// The EEPROM.
//
// A console keeps the settings made in the dashboard - language, video standard, widescreen, audio mode,
// parental controls - in a hundred-odd bytes of non-volatile memory, and the kernel hands them out one value
// at a time. The driving engine reads two of them before it can decide on a resolution: XC_VIDEO for the
// video flags (0x0010e02b, the video-flags getter, masks the high word) and XC_FACTORY_AV_REGION for the
// video standard (0x0010e002 takes bits 8..15), both from ConfigureRes.
//
// There is no EEPROM here, so the answers are constants: PAL-I, English, 4:3, stereo, no parental control.
// PAL matches what the engine has already chosen by the time it asks - the tick it set up is 20 ms, 50 Hz -
// and it is what the action engine defaults to as well.
//
// The action engine puts these behind a settings.ini instead (src/action/engine/XboxSettings.cpp), replacing
// the game's own getters rather than the kernel call under them. When the driving engine grows the same file,
// this is where it should be read from: one implementation serving every caller beats a getter each.
//
// An index that is not in the table is refused rather than answered with a zero, and says so once, because a
// plausible-looking zero would be indistinguishable from a real setting and the next thing to ask for one is
// how we find out that it matters.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_STATUS_OBJECT_NAME_NOT_FOUND ((LONG)0xC0000034)
#define XBOX_REG_BINARY                   3
#define XBOX_REG_DWORD                    4

#define XC_LANGUAGE            0x07
#define XC_VIDEO               0x08
#define XC_AUDIO               0x09
#define XC_P_CONTROL_GAMES     0x0a
#define XC_MISC                0x11
#define XC_DVD_REGION          0x12
#define XC_FACTORY_AV_REGION   0x103
#define XC_FACTORY_GAME_REGION 0x104
#define XC_MAX_OS              0xff    // the whole operating-system settings block, rather than one value

#define AV_STANDARD_PAL_I      0x00000300   // bits 8..15 are the standard; 1 is NTSC-M, 3 is PAL-I

// Ordinal 24. ValueLength is what the caller has room for; resultLength is optional and the callers here pass
// null for it.
static LONG __stdcall Xbox_ExQueryNonVolatileSetting(ULONG valueIndex, ULONG *type, void *value,
                                                     ULONG valueLength, ULONG *resultLength) {
    ULONG setting = 0;
    switch (valueIndex) {
        case XC_LANGUAGE:            setting = 1; break;                  // English
        case XC_VIDEO:               setting = 0; break;                  // 4:3, no HDTV mode, no letterbox
        case XC_AUDIO:               setting = 0; break;                  // stereo, no Dolby encoding
        case XC_P_CONTROL_GAMES:     setting = 0; break;                  // nothing restricted
        case XC_MISC:                setting = 0; break;
        case XC_DVD_REGION:          setting = 2; break;                  // Europe, to match PAL below
        case XC_FACTORY_AV_REGION:   setting = AV_STANDARD_PAL_I; break;
        case XC_FACTORY_GAME_REGION: setting = 2; break;                  // 1 is NA, 2 Japan, 4 rest of world
        case XC_MAX_OS:
            // Not one setting but the whole OS block, which is what the time zone code asks for: it reads
            // the lot and picks the fields it wants. Zeroed means UTC with no daylight saving, which is the
            // truthful answer when there is no EEPROM to have configured.
            if (value == NULL)
                return XBOX_STATUS_INVALID_PARAMETER;
            memset(value, 0, valueLength);
            if (type != NULL)
                *type = XBOX_REG_BINARY;
            if (resultLength != NULL)
                *resultLength = valueLength;
            return XBOX_STATUS_SUCCESS;
        default:
            printf("[loader] ExQueryNonVolatileSetting: no answer for setting 0x%lx\n", valueIndex);
            fflush(stdout);
            return XBOX_STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (value == NULL || valueLength < sizeof(ULONG))
        return XBOX_STATUS_INVALID_PARAMETER;

    memcpy(value, &setting, sizeof(setting));
    if (type != NULL)
        *type = XBOX_REG_DWORD;
    if (resultLength != NULL)
        *resultLength = sizeof(setting);
    return XBOX_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------
// Handles.
//
// The Xbox has one handle table for everything, so NtClose closes threads, events, timers and files alike -
// which is why it is reached before anything else does anything interesting: the XBE's entry point creates the
// startup thread and immediately closes the handle it got back. Every handle the loader hands out is a Win32
// handle, so this is CloseHandle.
//
// The action engine never needed this in the loader because its injected file layer replaces the XAPI wrapper
// that calls it (DoNtClose in src/action/engine/XboxFile.cpp), which also has to tell find handles from file
// handles. Nothing on the driving side replaces that wrapper yet, and when a file layer does arrive it will
// want the same distinction - so the general case lives here and the file layer can still take the wrapper.
// ---------------------------------------------------------------------------------------------------------------

// Ordinal 187.
static LONG __stdcall Xbox_NtClose(HANDLE handle) {
    if (handle == NULL || handle == INVALID_HANDLE_VALUE)
        return XBOX_STATUS_INVALID_PARAMETER;
    return CloseHandle(handle) ? XBOX_STATUS_SUCCESS : XBOX_STATUS_INVALID_PARAMETER;
}

// ---------------------------------------------------------------------------------------------------------------
// Threads, as far as the handle-based part goes.
//
// The rest of XAPI's thread layer converts handles into kernel objects and works on those - see the note in
// src/driving/platform/XboxStartup.cpp about SetThreadPriority, which is replaced wholesale for that reason.
// These two take handles and do exactly what Win32 does, so they stay here.
// ---------------------------------------------------------------------------------------------------------------

// Ordinal 224. The previous suspend count is what ResumeThread returns, and -1 is its failure.
static LONG __stdcall Xbox_NtResumeThread(HANDLE thread, ULONG *previousSuspendCount) {
    DWORD previous = ResumeThread(thread);
    if (previous == (DWORD)-1)
        return XBOX_STATUS_UNSUCCESSFUL;
    if (previousSuspendCount != NULL)
        *previousSuspendCount = previous;
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 231.
static LONG __stdcall Xbox_NtSuspendThread(HANDLE thread, ULONG *previousSuspendCount) {
    DWORD previous = SuspendThread(thread);
    if (previous == (DWORD)-1)
        return XBOX_STATUS_UNSUCCESSFUL;
    if (previousSuspendCount != NULL)
        *previousSuspendCount = previous;
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 238.
static LONG __stdcall Xbox_NtYieldExecution(void) {
    SwitchToThread();
    return XBOX_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------
// Events and waiting.
//
// The Xbox's synchronisation is NT's, so these are nearly one-for-one with Win32: a notification event is a
// manual-reset event and a synchronisation event is an auto-reset one, which is the whole of the difference
// the game cares about. The driving engine reaches them through XAPI's CreateEvent/WaitForSingleObject as
// soon as the file system starts a thread - docs/driving-engine-plan.md section 6.3 lists them.
//
// Two shape differences are worth naming because a silent mistranslation would look like a hang:
//
//  - an NT timeout is a pointer, not a value. Null means "wait forever"; a negative value is a relative time
//    in 100-nanosecond units; a positive one is an absolute time since 1601, which nothing here uses and
//    which is refused rather than silently treated as relative;
//  - waiting returns a status, not a Win32 wait result. Zero is the object being signalled (or, for a
//    multiple wait, the index of the one that was), and a timeout is STATUS_TIMEOUT rather than a failure.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_STATUS_TIMEOUT       ((LONG)0x00000102)
#define XBOX_STATUS_USER_APC      ((LONG)0x000000C0)
#define XBOX_WAIT_ANY             1     // WaitType: any one of the objects, as against WaitAll = 0

// Milliseconds for Win32, out of an NT timeout pointer. Returns false if the timeout is one this cannot
// express - an absolute time - so the caller can refuse rather than wait for the wrong length.
static bool TimeoutToMilliseconds(const LARGE_INTEGER *timeout, DWORD *milliseconds) {
    if (timeout == NULL) {
        *milliseconds = INFINITE;
        return true;
    }
    if (timeout->QuadPart > 0) {
        printf("[loader] absolute NT timeouts are not implemented (0x%08x%08x)\n",
               (unsigned)timeout->HighPart, (unsigned)timeout->LowPart);
        return false;
    }
    // Negative, in 100ns units. Round up, so that a sub-millisecond wait is a wait rather than a spin.
    ULONGLONG hundredNanoseconds = (ULONGLONG)(-timeout->QuadPart);
    *milliseconds = (DWORD)((hundredNanoseconds + 9999) / 10000);
    return true;
}

// Win32's wait result as the status the caller expects. WAIT_OBJECT_0 is zero, so for a single object the
// success case needs no translation at all; for several, the index is the return value.
static LONG WaitResultToStatus(DWORD result, ULONG objectCount) {
    if (result == WAIT_TIMEOUT)
        return XBOX_STATUS_TIMEOUT;
    if (result == WAIT_IO_COMPLETION)
        return XBOX_STATUS_USER_APC;
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + objectCount)
        return (LONG)(result - WAIT_OBJECT_0);
    if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + objectCount)
        return (LONG)(result - WAIT_ABANDONED_0);   // an abandoned mutex; the wait still succeeded
    return XBOX_STATUS_UNSUCCESSFUL;
}

// Ordinal 189. The object attributes name the event on the console, for the object namespace; nothing here
// looks an event up by name, so an unnamed Win32 event is the whole of it.
static LONG __stdcall Xbox_NtCreateEvent(HANDLE *eventHandle, void *objectAttributes,
                                         ULONG eventType, BOOLEAN initialState) {
    (void)objectAttributes;
    if (eventHandle == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;

    // NotificationEvent (0) stays signalled until it is cleared; SynchronizationEvent (1) releases one waiter
    // and resets itself. That is manual-reset and auto-reset respectively.
    BOOL manualReset = (eventType == 0) ? TRUE : FALSE;
    HANDLE handle = CreateEventA(NULL, manualReset, initialState ? TRUE : FALSE, NULL);
    if (handle == NULL)
        return XBOX_STATUS_UNSUCCESSFUL;

    *eventHandle = handle;
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 225. PreviousState is the signalled state before the call, which Win32 does not report; nothing has
// asked for it yet, and answering with a guess would be worse than saying so if something ever does.
static LONG __stdcall Xbox_NtSetEvent(HANDLE eventHandle, LONG *previousState) {
    if (previousState != NULL)
        printf("[loader] NtSetEvent: the previous state was asked for and is not tracked\n");
    return SetEvent(eventHandle) ? XBOX_STATUS_SUCCESS : XBOX_STATUS_UNSUCCESSFUL;
}

// Ordinal 186.
static LONG __stdcall Xbox_NtClearEvent(HANDLE eventHandle) {
    return ResetEvent(eventHandle) ? XBOX_STATUS_SUCCESS : XBOX_STATUS_UNSUCCESSFUL;
}

// Ordinal 205. Releases everything waiting and leaves the event clear, which is what PulseEvent does.
static LONG __stdcall Xbox_NtPulseEvent(HANDLE eventHandle, LONG *previousState) {
    if (previousState != NULL)
        printf("[loader] NtPulseEvent: the previous state was asked for and is not tracked\n");
    return PulseEvent(eventHandle) ? XBOX_STATUS_SUCCESS : XBOX_STATUS_UNSUCCESSFUL;
}

// Ordinal 233.
static LONG __stdcall Xbox_NtWaitForSingleObject(HANDLE handle, BOOLEAN alertable, LARGE_INTEGER *timeout) {
    DWORD milliseconds = INFINITE;
    if (!TimeoutToMilliseconds(timeout, &milliseconds))
        return XBOX_STATUS_INVALID_PARAMETER;
    return WaitResultToStatus(WaitForSingleObjectEx(handle, milliseconds, alertable ? TRUE : FALSE), 1);
}

// Ordinal 234. The extra argument is the processor mode the wait is performed in, which has no meaning here.
static LONG __stdcall Xbox_NtWaitForSingleObjectEx(HANDLE handle, char waitMode, BOOLEAN alertable,
                                                   LARGE_INTEGER *timeout) {
    (void)waitMode;
    return Xbox_NtWaitForSingleObject(handle, alertable, timeout);
}

// Ordinal 235.
static LONG __stdcall Xbox_NtWaitForMultipleObjectsEx(ULONG count, HANDLE *handles, ULONG waitType,
                                                      char waitMode, BOOLEAN alertable,
                                                      LARGE_INTEGER *timeout) {
    (void)waitMode;
    DWORD milliseconds = INFINITE;
    if (!TimeoutToMilliseconds(timeout, &milliseconds))
        return XBOX_STATUS_INVALID_PARAMETER;
    if (handles == NULL || count == 0 || count > MAXIMUM_WAIT_OBJECTS)
        return XBOX_STATUS_INVALID_PARAMETER;

    BOOL waitAll = (waitType == XBOX_WAIT_ANY) ? FALSE : TRUE;
    DWORD result = WaitForMultipleObjectsEx(count, handles, waitAll, milliseconds,
                                            alertable ? TRUE : FALSE);
    return WaitResultToStatus(result, count);
}

// Ordinal 99. The game's sleep, once XAPI's Sleep is unwrapped: the same timeout shape as the waits above.
static LONG __stdcall Xbox_KeDelayExecutionThread(char waitMode, BOOLEAN alertable, LARGE_INTEGER *interval) {
    (void)waitMode;
    DWORD milliseconds = INFINITE;
    if (!TimeoutToMilliseconds(interval, &milliseconds))
        return XBOX_STATUS_INVALID_PARAMETER;
    if (milliseconds == INFINITE) {
        // A sleep with no end is a hang, and the console's own would be one too; say so rather than joining it.
        printf("[loader] KeDelayExecutionThread with no interval - not sleeping forever\n");
        return XBOX_STATUS_INVALID_PARAMETER;
    }
    if (alertable) {
        DWORD result = SleepEx(milliseconds, TRUE);
        return (result == WAIT_IO_COMPLETION) ? XBOX_STATUS_USER_APC : XBOX_STATUS_SUCCESS;
    }
    Sleep(milliseconds);
    return XBOX_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------
// Time, as the kernel tells it.
//
// Two clocks, and they are not the same one. System time is the wall clock, in 100-nanosecond units since
// 1601 - the same epoch Win32's FILETIME uses, so it is the same number. Interrupt time is how long the
// machine has been up, in the same units, and it is the one the game uses for intervals because it does not
// jump when the clock is set.
// ---------------------------------------------------------------------------------------------------------------

#define HUNDRED_NS_PER_MS 10000ull

// Ordinal 128.
static void __stdcall Xbox_KeQuerySystemTime(LARGE_INTEGER *systemTime) {
    if (systemTime == NULL)
        return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    systemTime->LowPart = now.dwLowDateTime;
    systemTime->HighPart = (LONG)now.dwHighDateTime;
}

// Ordinal 125. Returned in EDX:EAX, which is what a 64-bit return value is on x86.
static ULONGLONG __stdcall Xbox_KeQueryInterruptTime(void) {
    return (ULONGLONG)GetTickCount64() * HUNDRED_NS_PER_MS;
}

// Ordinal 151. A busy wait on the console, where it is used for microsecond-scale hardware delays. Nothing
// here drives hardware, so the only thing that matters is not spinning a whole timeslice away.
static void __stdcall Xbox_KeStallExecutionProcessor(ULONG microseconds) {
    if (microseconds >= 1000)
        Sleep(microseconds / 1000);
    else
        SwitchToThread();
}

// The broken-down time both conversions work on. Same shape as NT's TIME_FIELDS, and the same as Win32's
// SYSTEMTIME with the fields in a different order - which is the whole of the work below.
struct XboxTimeFields {
    SHORT Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday;
};

// Ordinal 305. Splits a 100-nanosecond count since 1601 into fields; Win32 has the same calendar, so this is
// its FileTimeToSystemTime with the fields moved.
static void __stdcall Xbox_RtlTimeToTimeFields(const LARGE_INTEGER *time, XboxTimeFields *fields) {
    if (time == NULL || fields == NULL)
        return;

    FILETIME fileTime;
    fileTime.dwLowDateTime = time->LowPart;
    fileTime.dwHighDateTime = (DWORD)time->HighPart;

    SYSTEMTIME systemTime;
    memset(&systemTime, 0, sizeof(systemTime));
    if (!FileTimeToSystemTime(&fileTime, &systemTime)) {
        memset(fields, 0, sizeof(*fields));
        return;
    }

    fields->Year = (SHORT)systemTime.wYear;
    fields->Month = (SHORT)systemTime.wMonth;
    fields->Day = (SHORT)systemTime.wDay;
    fields->Hour = (SHORT)systemTime.wHour;
    fields->Minute = (SHORT)systemTime.wMinute;
    fields->Second = (SHORT)systemTime.wSecond;
    fields->Milliseconds = (SHORT)systemTime.wMilliseconds;
    fields->Weekday = (SHORT)systemTime.wDayOfWeek;
}

// Ordinal 304, the other direction. False for a date that is not a real one, as the original does.
static BOOLEAN __stdcall Xbox_RtlTimeFieldsToTime(const XboxTimeFields *fields, LARGE_INTEGER *time) {
    if (fields == NULL || time == NULL)
        return FALSE;

    SYSTEMTIME systemTime;
    memset(&systemTime, 0, sizeof(systemTime));
    systemTime.wYear = (WORD)fields->Year;
    systemTime.wMonth = (WORD)fields->Month;
    systemTime.wDay = (WORD)fields->Day;
    systemTime.wHour = (WORD)fields->Hour;
    systemTime.wMinute = (WORD)fields->Minute;
    systemTime.wSecond = (WORD)fields->Second;
    systemTime.wMilliseconds = (WORD)fields->Milliseconds;

    FILETIME fileTime;
    if (!SystemTimeToFileTime(&systemTime, &fileTime))
        return FALSE;
    time->LowPart = fileTime.dwLowDateTime;
    time->HighPart = (LONG)fileTime.dwHighDateTime;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------------------------
// Interrupt levels, which a Win32 process does not have.
//
// IRQL is the console's interrupt priority: raising it stops the scheduler and the interrupt handlers from
// running, which is how the game's driver code makes itself atomic. There is no equivalent here and nothing
// to protect against - the game's own threads use critical sections for that - so these keep the shape and
// do nothing. The value returned is the "old IRQL" the caller will hand back to KfLowerIrql.
// ---------------------------------------------------------------------------------------------------------------

// Ordinal 129.
static UCHAR __stdcall Xbox_KeRaiseIrqlToDpcLevel(void) {
    return 0;
}

// Ordinal 160. __fastcall on the console: the new IRQL arrives in CL.
static UCHAR __fastcall Xbox_KfRaiseIrql(UCHAR newIrql) {
    (void)newIrql;
    return 0;
}

// Ordinal 161.
static void __fastcall Xbox_KfLowerIrql(UCHAR newIrql) {
    (void)newIrql;
}

// Ordinal 153. On the console this runs a routine with the device's interrupt blocked. Here it is just the
// routine, called the way it would have been.
static BOOLEAN __stdcall Xbox_KeSynchronizeExecution(void *interrupt, BOOLEAN (__stdcall *routine)(void *),
                                                     void *context) {
    (void)interrupt;
    return (routine != NULL) ? routine(context) : FALSE;
}

// Ordinals 142 and 139. The kernel saves the FPU state around code that uses it from an interrupt handler.
// Nothing here runs in an interrupt, and Win32 saves the FPU state per thread anyway.
static LONG __stdcall Xbox_KeSaveFloatingPointState(void *state) {
    (void)state;
    return XBOX_STATUS_SUCCESS;
}

static LONG __stdcall Xbox_KeRestoreFloatingPointState(void *state) {
    (void)state;
    return XBOX_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------
// Objects and threads.
//
// The console's kernel hands out handles to objects and lets a caller convert one into the object itself.
// Every handle here is a Win32 handle and there is no object behind it, so a reference is the handle: the
// callers that do this (XAPI's thread functions) either pass it straight back to another kernel call or read
// fields the replacements in src/driving/platform/XboxStartup.cpp no longer go through.
// ---------------------------------------------------------------------------------------------------------------

// Ordinal 246.
static LONG __stdcall Xbox_ObReferenceObjectByHandle(HANDLE handle, void *objectType, void **object) {
    (void)objectType;
    if (object == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;
    *object = handle;
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 250. __fastcall on the console, with the object in ECX.
static void __fastcall Xbox_ObfDereferenceObject(void *object) {
    (void)object;
}

// Ordinal 258. The thread is a Win32 thread, so this is its exit.
static void __stdcall Xbox_PsTerminateSystemThread(LONG exitStatus) {
    ExitThread((DWORD)exitStatus);
}

// Ordinal 95. The console's panic: it stops, shows a message and waits to be switched off. Saying which code
// it was and stopping is the honest equivalent, and much more useful than continuing into whatever made it
// panic in the first place.
static void __stdcall Xbox_KeBugCheck(ULONG bugCheckCode) {
    printf("\n[loader] the game called KeBugCheck(0x%lx) - it has decided it cannot continue\n", bugCheckCode);
    fflush(stdout);
    if (IsDebuggerPresent())
        DebugBreak();
    ExitProcess(1);
}

// Ordinals 49 and 360. Rebooting, powering off, or going back to the dashboard. The process ending is as
// close as this gets.
static void __stdcall Xbox_HalReturnToFirmware(ULONG routine) {
    printf("[loader] the game asked the firmware to take over (mode %lu) - exiting\n", routine);
    fflush(stdout);
    ExitProcess(0);
}

static void __stdcall Xbox_HalInitiateShutdown(void) {
    printf("[loader] the game asked for a shutdown - exiting\n");
    fflush(stdout);
    ExitProcess(0);
}

// ---------------------------------------------------------------------------------------------------------------
// Critical sections.
//
// The game embeds these in its own structures - the heap has one - so they are built in place rather than
// allocated beside. That works because the Xbox's RTL_CRITICAL_SECTION is 28 bytes (a 16-byte dispatcher
// header, then LockCount, RecursionCount and OwningThread) against Win32's 24, so there is room to spare. The
// check below is what makes that safe to rely on rather than something to remember.
//
// The "AndRegion" variants differ only in also disabling kernel APCs around the section, which has no meaning
// in a Win32 process, so they are the same functions.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_CRITICAL_SECTION_SIZE 28
static_assert(sizeof(CRITICAL_SECTION) <= XBOX_CRITICAL_SECTION_SIZE,
              "a Win32 CRITICAL_SECTION no longer fits where the game reserved space for an Xbox one");

// ---------------------------------------------------------------------------------------------------------------
// Sections that were never initialised at run time, because on the Xbox they did not have to be.
//
// An Xbox RTL_CRITICAL_SECTION can be built by the compiler: it is a plain structure whose unlocked state is a
// self-linked dispatcher header and LockCount -1, so a static one is simply laid out that way in .data and
// RtlEnterCriticalSection works on it without any initialiser ever running. XAPI's multimedia timer has one
// (0x001d2f48 in the driving build), and it is the first thing the driving engine touched that the action
// engine never did.
//
// Those bytes mean something quite different to Win32: a CRITICAL_SECTION starts with DebugInfo, so the Xbox
// header's first word becomes a debug pointer, the list pointers become RecursionCount and OwningThread, and
// the -1 that means "free" becomes LockSemaphore. EnterCriticalSection then waits on a handle that is not one
// and never returns - which is exactly how the driving engine's boot hung, inside timeSetEvent.
//
// So every section is initialised on first use unless we initialised it ourselves earlier. Remembering which
// ones we have seen is what makes that safe: initialising a section that is currently held would lose the
// hold, and there is no field to test for "initialised" that a static Xbox section does not already fill in
// with something plausible.
// ---------------------------------------------------------------------------------------------------------------

#define KNOWN_SECTION_MAX 1024

static CRITICAL_SECTION g_knownSectionsLock;
static void *g_knownSections[KNOWN_SECTION_MAX];
static unsigned g_knownSectionCount = 0;

// True if this is the first time the loader has seen this section, in which case the caller initialises it.
// The linear scan is fine: the count is in the tens, and the alternative is a hash table for a list that is
// walked only on the first touch of each section.
static bool IsNewSection(void *section) {
    bool isNew = true;

    EnterCriticalSection(&g_knownSectionsLock);
    for (unsigned i = 0; i < g_knownSectionCount; i++) {
        if (g_knownSections[i] == section) {
            isNew = false;
            break;
        }
    }
    if (isNew) {
        if (g_knownSectionCount < KNOWN_SECTION_MAX) {
            g_knownSections[g_knownSectionCount++] = section;
        } else {
            // Not expected, and worth saying rather than silently re-initialising a live section from here on.
            printf("[loader] more than %u critical sections - the loader has stopped tracking them\n",
                   (unsigned)KNOWN_SECTION_MAX);
            isNew = false;
        }
    }
    LeaveCriticalSection(&g_knownSectionsLock);
    return isNew;
}

static void EnsureSectionInitialised(CRITICAL_SECTION *section) {
    if (IsNewSection(section))
        InitializeCriticalSection(section);
}

static void __stdcall Xbox_RtlInitializeCriticalSection(CRITICAL_SECTION *section) {
    // Always, even for an address we have seen before. The game allocates some of its sections, and when a
    // block is freed and handed out again the new owner zeroes it and initialises it afresh; skipping that
    // because the address was familiar leaves a zeroed CRITICAL_SECTION that the first contended wait faults
    // on, inside ntdll, a long way from the cause. Being remembered is still what stops the *entry* path
    // initialising one that is already held.
    //
    // Nothing deletes these - the XBE does not even import RtlDeleteCriticalSection - so re-initialising
    // leaks whatever Win32 attached to the previous one, which is nothing until a wait actually contends.
    IsNewSection(section);
    InitializeCriticalSection(section);
}

static void __stdcall Xbox_RtlEnterCriticalSection(CRITICAL_SECTION *section) {
    EnsureSectionInitialised(section);
    EnterCriticalSection(section);
}

static void __stdcall Xbox_RtlLeaveCriticalSection(CRITICAL_SECTION *section) {
    // No initialising here: nothing can leave a section it did not enter, so by this point it is known. Doing
    // it anyway would turn a stray leave into a fresh section rather than the noisy failure it should be.
    LeaveCriticalSection(section);
}

// Returns a BOOLEAN on the Xbox, but callers test the whole of EAX, so this returns a full-width value rather
// than letting only AL be meaningful.
static DWORD __stdcall Xbox_RtlTryEnterCriticalSection(CRITICAL_SECTION *section) {
    EnsureSectionInitialised(section);
    return TryEnterCriticalSection(section) ? 1u : 0u;
}

static const struct { unsigned ordinal; void *implementation; } g_implemented[] = {
    { 14,  (void *)Xbox_ExAllocatePool },
    { 15,  (void *)Xbox_ExAllocatePoolWithTag },
    { 17,  (void *)Xbox_ExFreePool },
    { 23,  (void *)Xbox_ExQueryPoolBlockSize },
    { 24,  (void *)Xbox_ExQueryNonVolatileSetting },
    { 166, (void *)Xbox_MmAllocateContiguousMemoryEx },
    { 171, (void *)Xbox_MmFreeContiguousMemory },
    { 178, (void *)Xbox_MmPersistContiguousMemory },
    { 180, (void *)Xbox_MmQueryAllocationSize },
    { 184, (void *)Xbox_NtAllocateVirtualMemory },
    { 49,  (void *)Xbox_HalReturnToFirmware },
    { 95,  (void *)Xbox_KeBugCheck },
    { 99,  (void *)Xbox_KeDelayExecutionThread },
    { 125, (void *)Xbox_KeQueryInterruptTime },
    { 128, (void *)Xbox_KeQuerySystemTime },
    { 129, (void *)Xbox_KeRaiseIrqlToDpcLevel },
    { 139, (void *)Xbox_KeRestoreFloatingPointState },
    { 142, (void *)Xbox_KeSaveFloatingPointState },
    { 151, (void *)Xbox_KeStallExecutionProcessor },
    { 153, (void *)Xbox_KeSynchronizeExecution },
    { 160, (void *)Xbox_KfRaiseIrql },
    { 161, (void *)Xbox_KfLowerIrql },
    { 186, (void *)Xbox_NtClearEvent },
    { 187, (void *)Xbox_NtClose },
    { 189, (void *)Xbox_NtCreateEvent },
    { 205, (void *)Xbox_NtPulseEvent },
    { 224, (void *)Xbox_NtResumeThread },
    { 225, (void *)Xbox_NtSetEvent },
    { 231, (void *)Xbox_NtSuspendThread },
    { 233, (void *)Xbox_NtWaitForSingleObject },
    { 234, (void *)Xbox_NtWaitForSingleObjectEx },
    { 235, (void *)Xbox_NtWaitForMultipleObjectsEx },
    { 238, (void *)Xbox_NtYieldExecution },
    { 199, (void *)Xbox_NtFreeVirtualMemory },
    { 204, (void *)Xbox_NtProtectVirtualMemory },
    { 217, (void *)Xbox_NtQueryVirtualMemory },
    { 246, (void *)Xbox_ObReferenceObjectByHandle },
    { 250, (void *)Xbox_ObfDereferenceObject },
    { 255, (void *)Xbox_PsCreateSystemThreadEx },
    { 258, (void *)Xbox_PsTerminateSystemThread },
    { 360, (void *)Xbox_HalInitiateShutdown },
    { 277, (void *)Xbox_RtlEnterCriticalSection },
    { 278, (void *)Xbox_RtlEnterCriticalSection },   // ...AndRegion: APC disabling has no meaning here
    { 291, (void *)Xbox_RtlInitializeCriticalSection },
    { 304, (void *)Xbox_RtlTimeFieldsToTime },
    { 305, (void *)Xbox_RtlTimeToTimeFields },
    { 294, (void *)Xbox_RtlLeaveCriticalSection },
    { 295, (void *)Xbox_RtlLeaveCriticalSection },   // ...AndRegion, as above
    { 306, (void *)Xbox_RtlTryEnterCriticalSection },
};

// ---------------------------------------------------------------------------------------------------------------
// KeTickCount - ordinal 156, and the one kernel export here that is data rather than a function.
//
// The console's kernel increments it once per clock interrupt, which on the Xbox is once a millisecond, and
// a game reads it straight out of kernel memory: the XBE's import thunk holds the address of the variable,
// not of a routine. That makes it invisible to the reporting stubs - nothing is ever called, so nothing is
// ever reported - and before this was here the thunk pointed at a stub trampoline, so the game's
// getTickCount() returned the first four bytes of a push instruction, the same number every time.
//
// That has consequences a frozen clock would not obviously have. EA's sound driver thread paces itself by
//
//     sleep(nextDeadline - getTickCount()); nextDeadline += 10;
//
// so with the clock stopped the deadline runs away from it and each sleep is ten milliseconds longer than
// the last. The 100 Hz sound server slows to a few hertz within a minute; the movie player, whose streaming
// is paced by how much audio the mixer has consumed, slows with it.
//
// One thread advances it. Sleep(1) with the multimedia timer period raised is a millisecond to within the
// scheduler's accuracy, and being a millisecond or two out matters to nothing that reads this: it is a
// coarse clock on the console too.
// ---------------------------------------------------------------------------------------------------------------

static volatile ULONG g_keTickCount;

static DWORD WINAPI KeTickCountThread(LPVOID) {
    timeBeginPeriod(1);
    DWORD start = timeGetTime();
    for (;;) {
        g_keTickCount = timeGetTime() - start;
        Sleep(1);
    }
}

bool Kernel_Init(void) {
    InitializeCriticalSection(&g_knownSectionsLock);
    CloseHandle(CreateThread(NULL, 0, KeTickCountThread, NULL, 0, NULL));

    g_trampolines = (KernelTrampoline *)VirtualAlloc(NULL, KERNEL_MAX_ORDINAL * sizeof(KernelTrampoline),
                                                     MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (g_trampolines == NULL) {
        printf("[loader] could not allocate the kernel stub trampolines (error %lu)\n", GetLastError());
        return false;
    }
    g_trampolineCount = KERNEL_MAX_ORDINAL;

    for (unsigned i = 0; i < g_trampolineCount; i++) {
        KernelTrampoline *t = &g_trampolines[i];
        t->pushOpcode = 0x68;                 // push imm32
        t->ordinal = i;
        t->jmpOpcode = 0xE9;                  // jmp rel32, measured from the end of this instruction
        t->relativeTarget = (int32_t)((uint8_t *)KernelStubEntry - ((uint8_t *)&t->relativeTarget + 4));
    }

    FlushInstructionCache(GetCurrentProcess(), g_trampolines,
                          g_trampolineCount * sizeof(KernelTrampoline));
    return true;
}

void *Kernel_Resolve(unsigned ordinal) {
    // The data export: the thunk takes the address of the counter itself, not of anything to call.
    if (ordinal == 156)
        return (void *)&g_keTickCount;

    for (size_t i = 0; i < sizeof(g_implemented) / sizeof(g_implemented[0]); i++) {
        if (g_implemented[i].ordinal == ordinal)
            return g_implemented[i].implementation;
    }

    // The file system lives in its own file; it is the one part of this that is more than a translation.
    unsigned fileExportCount = 0;
    const KernelFileExport *fileExports = Kernel_FileExports(&fileExportCount);
    for (unsigned i = 0; i < fileExportCount; i++) {
        if (fileExports[i].ordinal == ordinal)
            return fileExports[i].implementation;
    }

    // Everything else gets the stub, which reports itself when the game reaches it.
    if (ordinal >= g_trampolineCount) {
        printf("[loader] kernel ordinal %u is beyond the stub table\n", ordinal);
        return NULL;
    }
    return &g_trampolines[ordinal];
}

const char *Kernel_OrdinalName(unsigned ordinal) {
    return KernelOrdinalName(ordinal);
}
