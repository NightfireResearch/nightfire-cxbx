#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#include "kernel.h"
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

// Ordinal 184. Note there is no process handle: an Xbox title is the only process there is.
static LONG __stdcall Xbox_NtAllocateVirtualMemory(void **baseAddress, ULONG zeroBits, ULONG *allocationSize,
                                                   DWORD allocationType, DWORD protect) {
    (void)zeroBits;
    if (baseAddress == NULL || allocationSize == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;

    void *result = VirtualAlloc(*baseAddress, *allocationSize, allocationType, protect);
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

// Ordinal 205.
static LONG __stdcall Xbox_NtProtectVirtualMemory(void **baseAddress, ULONG *regionSize, ULONG newProtect,
                                                  ULONG *oldProtect) {
    if (baseAddress == NULL || regionSize == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;

    DWORD previous = 0;
    if (!VirtualProtect(*baseAddress, *regionSize, newProtect, &previous))
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
        g_poolHeap = HeapCreate(0, 0, 0);
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

    void *memory = VirtualAlloc(NULL, numberOfBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
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

static void __stdcall Xbox_RtlInitializeCriticalSection(CRITICAL_SECTION *section) {
    InitializeCriticalSection(section);
}

static void __stdcall Xbox_RtlEnterCriticalSection(CRITICAL_SECTION *section) {
    EnterCriticalSection(section);
}

static void __stdcall Xbox_RtlLeaveCriticalSection(CRITICAL_SECTION *section) {
    LeaveCriticalSection(section);
}

// Returns a BOOLEAN on the Xbox, but callers test the whole of EAX, so this returns a full-width value rather
// than letting only AL be meaningful.
static DWORD __stdcall Xbox_RtlTryEnterCriticalSection(CRITICAL_SECTION *section) {
    return TryEnterCriticalSection(section) ? 1u : 0u;
}

static const struct { unsigned ordinal; void *implementation; } g_implemented[] = {
    { 14,  (void *)Xbox_ExAllocatePool },
    { 15,  (void *)Xbox_ExAllocatePoolWithTag },
    { 17,  (void *)Xbox_ExFreePool },
    { 23,  (void *)Xbox_ExQueryPoolBlockSize },
    { 166, (void *)Xbox_MmAllocateContiguousMemoryEx },
    { 171, (void *)Xbox_MmFreeContiguousMemory },
    { 178, (void *)Xbox_MmPersistContiguousMemory },
    { 180, (void *)Xbox_MmQueryAllocationSize },
    { 184, (void *)Xbox_NtAllocateVirtualMemory },
    { 199, (void *)Xbox_NtFreeVirtualMemory },
    { 205, (void *)Xbox_NtProtectVirtualMemory },
    { 217, (void *)Xbox_NtQueryVirtualMemory },
    { 255, (void *)Xbox_PsCreateSystemThreadEx },
    { 277, (void *)Xbox_RtlEnterCriticalSection },
    { 278, (void *)Xbox_RtlEnterCriticalSection },   // ...AndRegion: APC disabling has no meaning here
    { 291, (void *)Xbox_RtlInitializeCriticalSection },
    { 294, (void *)Xbox_RtlLeaveCriticalSection },
    { 295, (void *)Xbox_RtlLeaveCriticalSection },   // ...AndRegion, as above
    { 306, (void *)Xbox_RtlTryEnterCriticalSection },
};

bool Kernel_Init(void) {
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
    for (size_t i = 0; i < sizeof(g_implemented) / sizeof(g_implemented[0]); i++) {
        if (g_implemented[i].ordinal == ordinal)
            return g_implemented[i].implementation;
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
