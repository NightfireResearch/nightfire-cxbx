#include "XboxStartup.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../main.h"
#include "XboxTimer.h"
#include "LaunchOptions.h"

// ---------------------------------------------------------------------------------------------------------------
// The driving engine's process startup, as run by the standalone loader - step 2 of docs/driving-engine-plan.md
// section 7.
//
// The XBE's entry point creates a thread whose start routine is mainXapiStartup, and that function is the whole
// of process startup: it patches the kernel, initialises XAPI, sets up the thread's TLS, runs the C runtime's
// initialisers and the C++ constructors, and then calls main. The same three things as in the action engine
// make that impossible to run as it stands:
//
//  - the TLS setup reaches the current thread through the Xbox KPCR at FS:[0x20]/[0x28] and stores the block
//    where FS:[0x4] indexes it. A Win32 thread has a TEB at FS, so FS:[0x4] is the stack base, and writing a
//    TLS array pointer over it breaks exception dispatch, which validates every frame against it;
//  - FUN_001106e4 patches the running kernel image. There is no kernel image to patch - the loader resolves
//    the XBE's imports to its own implementations - so it is simply not called;
//  - XapiBootToDash at the end reboots the console, and main never returns anyway.
//
// The two XBEs carry the same XAPI, as far as this code goes: decompiling mainXapiStartup and XapiInitProcess
// in both gives identical output, only at different addresses. So this file is the action engine's
// src/action/engine/XboxStartup.cpp transposed, and that file is where the full reasoning lives. The addresses
// were read out of the driving build rather than assumed - the correspondence is:
//
//     action                                     driving
//     0x000eb238 mainXapiStartup                 0x0010e703
//     0x000ed8ac XapiInitProcess                 0x001104aa
//     0x000ec4a1 XapiCreateHeap                  0x001118dd
//     0x0030093c the process heap handle         0x0024b218
//     0x00163178 the XAPI initialiser table      0x001b4938
//     0x000eddd3 _rtinit, 0x000edd7b _cinit      0x001106bb, 0x00110663
//     0x000e9a24 GetLastError, 0x000e9a4c Set    0x0010f8f7, 0x0010f91f
//     0x000f3ec7 getptd, f3f49 free, f405e mt    0x001356f2, 0x00135774, 0x00135889
// ---------------------------------------------------------------------------------------------------------------

// XAPI internals, called by address: tools/functions_driving.json does not carry them.
#define Xapi_rtinit     ((void (__cdecl *)(void))0x001106bbu)
#define Xapi_cinit      ((void (__cdecl *)(void))0x00110663u)

// The XBE's own heap manager - RtlCreateHeap in all but name, and the allocator every malloc in the game
// eventually reaches. It is left in place and simply given memory to work with: its blocks are the ones the
// game's own free() understands.
#define XapiCreateHeap  ((void *(__stdcall *)(unsigned flags, void *base, unsigned reserve, \
                                              unsigned commit, void *lock, void *parameters))0x001118ddu)

// Its allocator, RtlAllocateHeap(heap, flags, size): what XAPI's LocalAlloc (0x0010fe33) calls.
#define XapiAllocateHeap ((void *(__stdcall *)(void *heap, unsigned flags, unsigned size))0x00111d01u)

// Where XapiInitProcess leaves the process heap handle, and the table of initialisers it runs afterwards.
#define ProcessHeapHandle (*(void **)0x0024b218u)
#define InitTableBegin    ((void (**)(void))0x001b4938u)
#define InitTableEnd      ((void (**)(void))0x001b493cu)

// ---------------------------------------------------------------------------------------------------------------
// XAPI's last-error pair.
//
// Both read the thread's TLS block as [FS:[0x4] + _tls_index * 4] and keep the error at offset 4 of it, which
// on a Win32 thread indexes off the stack base into nothing. Win32 keeps a per-thread last error of its own,
// so these become it - with the side benefit that errors set by our own code and errors set by the game are
// finally the same value.
// ---------------------------------------------------------------------------------------------------------------

DWORD __stdcall Xbox_GetLastError(void) {
    return GetLastError();
}

void __stdcall Xbox_SetLastError(DWORD error) {
    SetLastError(error);
}

// ---------------------------------------------------------------------------------------------------------------
// Process initialisation, in place of XapiInitProcess.
//
// Two of the original's jobs still mean something and are kept: creating the process heap with the reserve and
// commit sizes out of the XBE header and leaving the handle where the game's allocator looks for it, and
// running the initialiser table through which parts of XAPI register themselves.
//
// The rest are console concerns that each end in XapiBootToDash: mounting D: and the title's save directories,
// the utility drive, the allowed-media check and the parental-control check. The auto-power-down timer goes
// with them - it is the first thing the original does (FUN_0011313b), and its KeInitializeDpc is where the
// standalone boot stopped before this file existed.
// ---------------------------------------------------------------------------------------------------------------

void __stdcall Xbox_XapiInitProcess(void) {
    // RTL_HEAP_PARAMETERS, all defaults. Only the leading length field is set, exactly as the original does.
    unsigned parameters[12];
    memset(parameters, 0, sizeof(parameters));
    parameters[0] = sizeof(parameters);

    ProcessHeapHandle = XapiCreateHeap(2, NULL, 0x100000, 0x1000, NULL, parameters);
    if (ProcessHeapHandle == NULL) {
        printf("[startup] the process heap could not be created - nothing can allocate\n");
        fflush(stdout);
        return;
    }
    printf("[startup] process heap at %p\n", ProcessHeapHandle);

    for (void (**initialiser)(void) = InitTableBegin; initialiser < InitTableEnd; initialiser++) {
        if (*initialiser != NULL)
            (*initialiser)();
    }
    fflush(stdout);
}

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's per-thread data.
//
// The multithreaded CRT keeps a _ptiddata block per thread - errno, the strtok cursor, the locale - and finds
// it through the Xbox TLS block, as *(*(FS:[0x4] + _tls_index * 4) + 8). Same FS:[0x4] problem as above, so
// the pointer moves to a Win32 TLS slot and everything else about the block is kept: it is still allocated by
// the game's own calloc, because the game's own free is what would eventually release it.
//
// The original also bug-checks above APC level on FS:[0x24], the Xbox's IRQL. A Win32 thread is never at a
// raised IRQL, so there is nothing to check.
// ---------------------------------------------------------------------------------------------------------------

#define CrtCalloc            ((void *(__cdecl *)(size_t count, size_t size))0x0013439du)
#define CrtInitLocks         ((int (__cdecl *)(void))0x001363d8u)          // __mtinitlocks
#define CrtLockInitFailed    ((void (__cdecl *)(void))0x00136421u)
#define CrtFatalError        ((void (__cdecl *)(int code))0x0013566du)
#define CrtDefaultLocaleInfo ((void *)0x001d92b0u)

// Offsets into _ptiddata, as the original writes them: the owning thread, the thread handle, an
// initialised-flag, and the pointer to the default multibyte/locale information.
#define PTD_THREAD_ID     0
#define PTD_THREAD_HANDLE 1
#define PTD_INITIALISED   5
#define PTD_LOCALE_INFO   0x15

static DWORD g_perThreadSlot = TLS_OUT_OF_INDEXES;

void *__cdecl Xbox_getptd(void) {
    // TlsGetValue clears the last error on success, and this is reached from __dosmaperr, whose whole job is
    // to turn the last error into errno. Losing it there would turn a specific failure into a silent one.
    DWORD lastError = GetLastError();

    void *ptd = (g_perThreadSlot != TLS_OUT_OF_INDEXES) ? TlsGetValue(g_perThreadSlot) : NULL;
    if (ptd == NULL) {
        ptd = CrtCalloc(1, 0x84);
        if (ptd == NULL) {
            SetLastError(lastError);
            CrtFatalError(0x10);   // _RT_THREAD: no way to continue without per-thread state
            return NULL;
        }

        uintptr_t *fields = (uintptr_t *)ptd;
        fields[PTD_THREAD_ID] = GetCurrentThreadId();
        fields[PTD_THREAD_HANDLE] = (uintptr_t)0xffffffffu;
        fields[PTD_INITIALISED] = 1;
        fields[PTD_LOCALE_INFO] = (uintptr_t)CrtDefaultLocaleInfo;

        if (g_perThreadSlot != TLS_OUT_OF_INDEXES)
            TlsSetValue(g_perThreadSlot, ptd);
    }

    SetLastError(lastError);
    return ptd;
}

// The original unpicks the block - locale buffers, the strtok state, the floating-point context - and frees
// each piece with the game's free. It is reached from one place only: the thread-exit routine XAPI registers
// through XRegisterThreadNotifyRoutine, which runs from XapiThreadStartup, which this startup does not use.
// So it is unreachable, and dropping the block is honest where porting thirty lines of teardown that nothing
// exercises is not. The cost if that ever changes is a leak of about 132 bytes per thread that exits.
void __cdecl Xbox_freeptd(void *ptd) {
    (void)ptd;
    if (g_perThreadSlot != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_perThreadSlot, NULL);
}

// Returns zero exactly as the original does - it is called from the _rtinit table, which ignores the result.
int __cdecl Xbox_mtinit(void) {
    g_perThreadSlot = TlsAlloc();
    if (g_perThreadSlot == TLS_OUT_OF_INDEXES) {
        printf("[startup] no Win32 TLS slot for the C runtime's per-thread data\n");
        fflush(stdout);
        return 0;
    }

    if (CrtInitLocks() == 0) {
        CrtLockInitFailed();
        return 0;
    }

    Xbox_getptd();   // the main thread's block, so it exists before anything asks for errno
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The thread id.
//
// XAPI's GetCurrentThreadId (0x0010ecd8) is three instructions: it reaches the current KTHREAD through the
// KPCR at FS:[0x28] and takes the id from +0x12c. On a Win32 thread FS:[0x28] is ActiveRpcHandle, which is
// zero, so the second instruction reads address 0x12c and faults - which is exactly where the boot stopped
// once the renderer was up, in THREAD_init. Win32 has the same function, and the game only ever compares the
// value against itself (THREAD_iscurrent), so any id that is unique per thread will do.
//
// This is one of the plan's eight FS:[0x28] sites; the other seven are inside the CRT's per-thread data
// functions above, which are replaced whole.
// ---------------------------------------------------------------------------------------------------------------

static DWORD __stdcall Xbox_GetCurrentThreadId(void) {
    return GetCurrentThreadId();
}

// XAPI's SetThreadPriority (0x0010ea0f) turns the handle into a KTHREAD with ObReferenceObjectByHandle and
// calls KeSetBasePriorityThread on it - three kernel imports in aid of something Win32 does in one call, and
// all three would need fabricated kernel objects to mean anything. The priority values are the same numbers
// on both systems (the Xbox's own function only special-cases the two extremes, which Win32 handles), so the
// whole thing is the Win32 function.
static BOOL __stdcall Xbox_SetThreadPriority(HANDLE thread, int priority) {
    return SetThreadPriority(thread, priority);
}

// XAPI's GetExitCodeThread (0x0010eaef) does the same: ObReferenceObjectByHandle, then the ETHREAD's
// terminated flag at +4 and its ExitStatus at +0x120. With the handle standing in for the object, that is a
// read of address 0x650 - which is how the first part of a mission ended, in IFeedback's destructor waiting
// for the force-feedback thread to finish (0x0010a8f0 polls it for STILL_ACTIVE, 0x103). The Win32 function
// returns the same values: STILL_ACTIVE while the thread runs, its exit code after.
static BOOL __stdcall Xbox_GetExitCodeThread(HANDLE thread, DWORD *exitCode) {
    return GetExitCodeThread(thread, exitCode);
}

// ---------------------------------------------------------------------------------------------------------------
// XInitDevices, replaced by nothing at all.
//
// ApplicationMemoryHeapConfig (0x00059920) calls XAPILIB::XInitDevices(0, 0) on its way to setting the video
// mode, and it runs long before main: the first allocation out of a global constructor reaches it through
// FUN_00114840. XInitDevices walks the XBE's device-type table and starts XAPI's USB stack - it allocates a
// pool block per device type, initialises a DPC and a kernel timer for the polling loop, and from there it
// talks to the console's USB host controller. That is the first thing on this boot path with no meaning on a
// PC, and it is where the standalone boot stopped once startup worked (KeInitializeTimerEx, from the device
// table's initialiser at 0x00183df6).
//
// There is nothing to emulate, because the game will not be reading devices through XAPI. The action engine's
// input layer talks to Win32's XInput directly (src/action/engine/psiInput.cpp), having replaced the game's
// own device init for exactly this reason, and the driving engine's input seam will go the same way. Until
// it does, no controller is present as far as the game is concerned.
//
// Signature from the call site: two arguments, __stdcall, no return value.
// ---------------------------------------------------------------------------------------------------------------

static void __stdcall Xbox_XInitDevices(unsigned typeCount, void *preallocTypes) {
    (void)typeCount;
    (void)preallocTypes;
    printf("[startup] XInitDevices: no Xbox USB stack here, so nothing to initialise\n");
    fflush(stdout);
}

// ---------------------------------------------------------------------------------------------------------------
// The allocation-notification hook, patched out.
//
// Seven places in XAPI - including the hot paths of the heap allocator and the heap free path - end with the
// same twelve bytes:
//
//     64 a1 20 00 00 00    mov eax, fs:[0x20]            ; KPCR.Prcb
//     8b 80 50 02 00 00    mov eax, [eax + 0x250]        ; the debug notification table, if there is one
//     85 c0                test eax, eax
//     74 xx                jz   past the call
//
// On a retail console that pointer is null and the call is skipped; it is there for the debug kernel's
// allocation tracking. Here it faults, because FS:[0x20] on a Win32 thread is the process id rather than a
// pointer to anything. Replacing the two loads with "xor eax, eax" and padding makes the existing test fail
// and the existing jump skip the call, which is the retail behaviour - much cheaper than reimplementing the
// heap allocator to remove one branch from it.
//
// These are every FS:[0x20] site in the binary bar the one in mainXapiStartup, which this file replaces
// outright; together they account for all 8 that the plan's survey counted.
// ---------------------------------------------------------------------------------------------------------------

static const unsigned NOTIFICATION_HOOK_SITES[] = {
    0x0010eb96,   // the launch/reboot path
    0x0010f15c,   // FUN_0010f13f
    0x00112476,   // FUN_00111d01, the heap allocator
    0x001124ed,   // FUN_001124b8
    0x00112d84,   // FUN_001126ac, the heap free path
    0x00112db9,   // FUN_001126ac again
    0x0016dcaf,   // FUN_0016d800
};

static const unsigned char NOTIFICATION_HOOK_BYTES[12] = {
    0x64, 0xa1, 0x20, 0x00, 0x00, 0x00,   // mov eax, fs:[0x20]
    0x8b, 0x80, 0x50, 0x02, 0x00, 0x00,   // mov eax, [eax + 0x250]
};

// ---------------------------------------------------------------------------------------------------------------
// WBINVD, patched out.
//
// An Xbox title runs in ring 0, so the game flushes the CPU's caches itself before the GPU reads memory it has
// just written - the nv2a reads main RAM directly and is not coherent with the cache. WBINVD is privileged in
// a normal Windows process, so executing one raises 0xc0000096.
//
// There is nothing to flush: no GPU reads this process's memory behind its back. So each becomes two nops.
//
// Ghidra finds eleven WBINVDs in the image and places nine of them inside a function. Of the other two,
// 0x0014d452 is real - it is in a region Ghidra has not analysed as code, and the movie player reaches it,
// which is how it announced itself as a privileged-instruction fault at exactly that address - and
// 0x0024bf11 is not: it is the bytes "cc 0f 09 00" inside an ascending table of dwords. Both halves of that
// were worth checking rather than assuming, and the byte check below is the guard either way: an address
// that does not hold a WBINVD is reported rather than written to.
// ---------------------------------------------------------------------------------------------------------------

static const unsigned WBINVD_SITES[] = {
    0x000a4861,   // FUN_000a4710
    0x000a4915,   // FUN_000a48b0
    0x000d7b2d,   // DoGirl
    0x000f62b9,   // FUN_000f62b0
    0x000f65c9,   // FUN_000f65c0
    0x000f6609,   // FUN_000f6600
    0x00117682,   // FileLoad
    0x00117762,   // FUN_001176f0
    0x0016dbb2,   // FUN_0016d800
    0x0014d452,   // no function around it, but the movie player runs it
};

static const unsigned char WBINVD_BYTES[2] = { 0x0f, 0x09 };

// ---------------------------------------------------------------------------------------------------------------
// Installing all of it.
//
// Deliberately not AUTOINJECT. That patches unconditionally, and every replacement here would be wrong under
// CXBX: there the XBE's startup runs against CXBX's emulated kernel, which provides the KPCR these functions
// are avoiding, does its own drive mounting, and expects the kernel-patching routine to have run. Patching by
// hand keeps a CXBX-hosted run byte for byte as it was.
// ---------------------------------------------------------------------------------------------------------------

static void WriteJump(unsigned address, void *target) {
    unsigned char *site = (unsigned char *)address;
    DWORD previous = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[startup] could not make 0x%08x writable (error %lu)\n", address, GetLastError());
        return;
    }
    site[0] = 0xE9;                                             // jmp rel32
    *(int *)(site + 1) = (int)((unsigned char *)target - (site + 5));
}

void Inject_XboxStartup(void) {
    if (!Xbox_RunningStandalone())
        return;

    WriteJump(0x0010e703, (void *)mainXapiStartup);          // the whole of process startup
    WriteJump(0x001104aa, (void *)Xbox_XapiInitProcess);     // heap and the XAPI initialiser table
    WriteJump(0x0010f8f7, (void *)Xbox_GetLastError);
    WriteJump(0x0010f91f, (void *)Xbox_SetLastError);
    WriteJump(0x001356f2, (void *)Xbox_getptd);              // the CRT's per-thread data
    WriteJump(0x00135774, (void *)Xbox_freeptd);
    WriteJump(0x00135889, (void *)Xbox_mtinit);
    WriteJump(0x00183dfd, (void *)Xbox_XInitDevices);   // XAPI's USB stack, which has nothing to talk to
    WriteJump(0x0010eed3, (void *)Xbox_timeSetEvent);   // the tick source, see XboxTimer.cpp
    WriteJump(0x0010ecd8, (void *)Xbox_GetCurrentThreadId);   // FS:[0x28] is not a KTHREAD here
    WriteJump(0x0010ea0f, (void *)Xbox_SetThreadPriority);    // no kernel thread objects to reference
    WriteJump(0x0010eaef, (void *)Xbox_GetExitCodeThread);    // same
    Inject_XboxCycleCounter();                                // RDTSC at the console's rate, see XboxTimer.cpp

    for (size_t i = 0; i < sizeof(WBINVD_SITES) / sizeof(WBINVD_SITES[0]); i++) {
        unsigned char *site = (unsigned char *)WBINVD_SITES[i];
        if (memcmp(site, WBINVD_BYTES, sizeof(WBINVD_BYTES)) != 0) {
            printf("[startup] 0x%08x is not a wbinvd - not patching it\n", WBINVD_SITES[i]);
            continue;
        }
        memset(site, 0x90, sizeof(WBINVD_BYTES));
    }

    for (size_t i = 0; i < sizeof(NOTIFICATION_HOOK_SITES) / sizeof(NOTIFICATION_HOOK_SITES[0]); i++) {
        unsigned char *site = (unsigned char *)NOTIFICATION_HOOK_SITES[i];

        // Checked rather than assumed: a wrong address here would write over live instructions, and the crash
        // would be somewhere else entirely.
        if (memcmp(site, NOTIFICATION_HOOK_BYTES, sizeof(NOTIFICATION_HOOK_BYTES)) != 0) {
            printf("[startup] 0x%08x is not the notification hook - not patching it\n",
                   NOTIFICATION_HOOK_SITES[i]);
            continue;
        }
        site[0] = 0x31;                                   // xor eax, eax
        site[1] = 0xc0;
        memset(site + 2, 0x90, sizeof(NOTIFICATION_HOOK_BYTES) - 2);   // nop
    }
}

// The game's main frees argv when it has parsed it (0x0005a353, the C runtime's free, into the process heap) -
// on the console XAPI built it there from the launch data's command line. So it is built there here too: one
// block holding the pointers and then the strings, which is what that one free releases. NULL when there is
// nothing to pass, which main takes as "no arguments" and does not free.
static char **BuildArgvInProcessHeap(int argc, char **source) {
    if (argc <= 0 || source == NULL)
        return NULL;
    unsigned size = (unsigned)(argc + 1) * sizeof(char *);
    for (int i = 0; i < argc; i++)
        size += (unsigned)strlen(source[i]) + 1;

    char **argv = (char **)XapiAllocateHeap(ProcessHeapHandle, 0, size);
    if (argv == NULL) {
        printf("[startup] could not allocate the game's argv - starting it without arguments\n");
        return NULL;
    }
    char *text = (char *)(argv + argc + 1);
    for (int i = 0; i < argc; i++) {
        size_t length = strlen(source[i]) + 1;
        memcpy(text, source[i], length);
        argv[i] = text;
        text += length;
    }
    argv[argc] = NULL;
    return argv;
}

DWORD WINAPI mainXapiStartup(LPVOID unused) {
    (void)unused;

    printf("[startup] thread running, initialising the process\n");
    fflush(stdout);

    Xbox_XapiInitProcess();

    printf("[startup] running the C runtime and C++ constructors\n");
    fflush(stdout);

    Xapi_rtinit();
    Xapi_cinit();

    printf("[startup] calling main\n");
    fflush(stdout);

    // Through preMain rather than straight to the game's main at 0x0005a1b0, so that a standalone run logs its
    // arguments exactly as a CXBX-hosted one does - there the same call site is patched to reach it, see
    // src/inject_driving.cpp. The game's main takes argc and argv, and does not return; they are the flags on
    // driving.exe's command line that are the game's own (-pal, -T<track> and so on, see LaunchOptions.cpp).
    int argc = LaunchOptions_GameArgc();
    char **argv = BuildArgvInProcessHeap(argc, LaunchOptions_GameArgv());
    preMain(argv != NULL ? argc : 0, argv);

    printf("[startup] main returned - it is not supposed to\n");
    fflush(stdout);
    return 0;
}
