#include "XboxStartup.h"
#include "../actionhelpers.h"
#include "../game/weapon_stats.h"   // WeaponDataTableInit
#include "../ui/HUD.h"              // the HUD panes' static initialisers
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The game's startup, as run by the loader.
//
// The XBE's entry point creates a thread whose start routine is mainXapiStartup, and that function is the whole
// of process startup: it patches the kernel, initialises XAPI, sets up the thread's TLS, runs the C runtime's
// initialisers and the C++ constructors, and then calls main. This replaces it, because two of those steps
// cannot work here and a third is not wanted:
//
//  - the TLS setup reaches the current thread through the Xbox KPCR at FS:[0x20]/[0x28] and stores the block
//    where FS:[0x4] indexes it. A Win32 thread has a TEB at FS, not a KPCR: FS:[0x4] is the stack base, and
//    writing a TLS array pointer over it would break exception dispatch, which validates stack frames against
//    it. This is the one genuine incompatibility in the whole startup, and it is why the sequence is rebuilt
//    here rather than patched;
//
//  - FUN_000eddfc patches the running kernel image. There is no kernel image to patch - the loader resolves
//    the XBE's imports to its own implementations - so it is simply not called;
//
//  - XapiBootToDash at the end reboots the console. main never returns, so it is unreachable anyway.
//
// What is kept is what still means something: process initialisation cut down to the process heap and the
// XAPI initialiser table (see Xbox_XapiInitProcess below), then _rtinit and _cinit, then main.
//
// Note the calling convention. The original is reached as a thread start routine - __stdcall, one ignored
// parameter - and the loader's PsCreateSystemThreadEx hands it straight to Win32 CreateThread, which wants
// exactly the same shape. So this is an ordinary LPTHREAD_START_ROUTINE and the stack works out on its own.
// ---------------------------------------------------------------------------------------------------------------

// The C runtime's own initialisers, which the original _rtinit and _cinit found in the XBE's initialiser tables
// (0x00163100-0x00163160). The C runtime stays the game's until the code calling it is ours, so these still run.
#define CrtInitFloatingPoint ((void (__cdecl *)(void))0x000f3178u)   // _rtinit's second entry
#define CrtSetupLibs         ((void (__cdecl *)(void))0x000ee285u)   // _cinit's library hook
#define CrtInitStdio         ((void (__cdecl *)(void))0x000f0e37u)   // ___initstdio
#define CrtInitAtExit        ((void (__cdecl *)(void))0x000f4f52u)

// The game's own static initialisers (C++ constructors of globals) that are not ours yet
// AUTOGEN
undefined FUN_000f5460(void);
// AUTOGEN
undefined FUN_000f5470(void);
// AUTOGEN
undefined FUN_000f5480(void);
// AUTOGEN
void __stdcall FUN_000f5490(void);
// AUTOGEN
undefined FUN_000f54c0(void);
// AUTOGEN
undefined FUN_000f54f0(void);
// AUTOGEN
undefined FUN_000f5520(void);

// The XBE's own heap manager - RtlCreateHeap in all but name, and the allocator every malloc in the game
// eventually reaches. It is left in place and simply given memory to work with: its heap blocks are the ones
// the game's own free() understands, so replacing it would mean replacing the whole allocator with it.
#define XapiCreateHeap  ((void *(__stdcall *)(unsigned flags, void *base, unsigned reserve, \
                                              unsigned commit, void *lock, void *parameters))0x000ec4a1u)

// Where XapiInitProcess leaves the process heap handle, and the table of initialisers it runs afterwards.
#define ProcessHeapHandle (*(void **)0x0030093cu)
#define InitTableBegin    ((void (**)(void))0x00163178u)
#define InitTableEnd      ((void (**)(void))0x0016317cu)

// The game's main, which AUTOINJECT has already replaced with the one in src/action/main.cpp. Called by
// address so that this file does not have to declare a function named main, and so it goes through the same
// patched entry point as everything else.
#define GameMain        ((void (__stdcall *)(int, char **))0x000e8e90u)

// ---------------------------------------------------------------------------------------------------------------
// XAPI's last-error pair.
//
// Both read the thread's TLS block as [FS:[0x4] + _tls_index * 4] and keep the error at offset 4 of it. On a
// Win32 thread FS:[0x4] is the stack base, so that indexes off into nothing and the store faults - which is
// precisely what happened at 0x000e9a71 before these existed. Win32 already keeps a per-thread last error of
// its own, so these simply become it, which has the side benefit that errors set by our own file layer and
// errors set by the game are finally the same value.
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
// The original does six things. Two of them still mean something here and are kept:
//
//  - it creates the process heap, with the reserve and commit sizes out of the XBE header, and leaves the
//    handle at 0x0030093c where the game's allocator looks for it. Nothing works without this;
//  - it runs the table of initialisers at 0x163178, which is how parts of XAPI register themselves.
//
// The other four are dropped. Mounting D:, the title's save directories and the utility drive is handled by
// our own file layer (src/common/xboxPath.cpp), which maps drive letters to host paths directly, so
// doing it again through the kernel would only ask for symbolic links nothing reads. The auto-power-down
// timer, the allowed-media check and the parental-control check are console concerns with no meaning on a PC -
// and each of them ends in XapiBootToDash, which reboots.
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
// The multithreaded CRT keeps a _ptiddata block per thread - errno, the strtok cursor, the locale, and so on -
// and finds it through the Xbox TLS block, as *(*(FS:[0x4] + _tls_index * 4) + 8). That is the same FS:[0x4]
// problem as the last-error pair, and it is where startup faulted next, at 0x000f4090 writing to address 8.
//
// These three keep everything about the original except where the pointer is kept, which becomes a Win32 TLS
// slot. In particular the block itself is still allocated by the game's own calloc, because the game's own
// free is what will eventually release it - a block from this DLL's heap handed to the XBE's heap manager
// would be a much less obvious crash than the one being fixed.
//
// The original also guards on FS:[0x24], the Xbox's current IRQL, bug-checking above APC level. A Win32
// thread is never at a raised IRQL, so there is nothing to check.
// ---------------------------------------------------------------------------------------------------------------

#define CrtCalloc            ((void *(__cdecl *)(size_t count, size_t size))0x000f3a4fu)
#define CrtInitLocks         ((int (__cdecl *)(void))0x000f35b5u)
#define CrtLockInitFailed    ((void (__cdecl *)(void))0x000f35feu)
#define CrtFatalError        ((void (__cdecl *)(int code))0x000f28d4u)
// XBE_GLOBAL(0x001d6538, 0x90)
#define CrtDefaultLocaleInfo ((void *)0x001d6538u)

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
// through XRegisterThreadNotifyRoutine, which runs from XapiThreadStartup, which this loader does not use. So
// it is currently unreachable, and the honest thing is to drop the block rather than to port thirty lines of
// teardown that nothing exercises. The cost if that ever changes is a leak of about 132 bytes per thread that
// exits, which is the right way round for a mistake to go.
//
void __cdecl Xbox_freeptd(void *ptd) {
    (void)ptd;
    if (g_perThreadSlot != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_perThreadSlot, NULL);
}

// Returns zero exactly as the original does - it is called from the _rtinit table, which ignores the result.
//
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
// The allocation-notification hook, patched out.
//
// Five places in XAPI - including the hot paths of both RtlAllocateHeap and RtlFreeHeap - end with the same
// twelve bytes:
//
//     64 a1 20 00 00 00    mov eax, fs:[0x20]            ; KPCR.Prcb
//     8b 80 50 02 00 00    mov eax, [eax + 0x250]        ; the debug notification table, if there is one
//     85 c0                test eax, eax
//     74 xx                jz   past the call
//
// On a retail console that pointer is null and the call is skipped; it is there for the debug kernel's
// allocation tracking. Here it faults, because FS:[0x20] on a Win32 thread is the process id rather than a
// pointer to anything, so the dereference lands on a small unmapped address - which is exactly the fault seen
// at 0x000ed040, reading 0x0000bfc4, process id 0xbd74 plus 0x250.
//
// Replacing the two loads with "xor eax, eax" and padding makes the existing test fail and the existing jump
// skip the call, which is the retail behaviour. That is preferred over reimplementing the functions: two of
// them are the heap allocator and the heap free path, hundreds of lines each, and the notification is the
// only thing about them that does not already work.
// ---------------------------------------------------------------------------------------------------------------

static const unsigned NOTIFICATION_HOOK_SITES[] = {
    0x000eb0d1,   // FUN_000eb0b4, the launch/reboot path
    0x000ed03a,   // RtlAllocateHeap
    0x000ed0b1,   // RtlFreeHeap
    0x000edf7b,   // XapiThreadStartup - unused now, but patched so it cannot surprise us later
    0x00105f6f,   // FUN_00105ac0
};

static const unsigned char NOTIFICATION_HOOK_BYTES[12] = {
    0x64, 0xa1, 0x20, 0x00, 0x00, 0x00,   // mov eax, fs:[0x20]
    0x8b, 0x80, 0x50, 0x02, 0x00, 0x00,   // mov eax, [eax + 0x250]
};

// ---------------------------------------------------------------------------------------------------------------
// WBINVD, patched out.
//
// An Xbox title runs in ring 0, so the game flushes the CPU's caches itself before the GPU reads memory it has
// just written - the nv2a reads main RAM directly and is not coherent with the cache. WBINVD is privileged on
// a normal Windows process, so executing one raises a privileged-instruction exception (0xc0000096), which is
// what happened at 0x000e8f80 the moment the attract movie started.
//
// There is nothing to flush. Nothing reads the game's memory behind its back any more: the D3D9 backend copies
// whatever the game writes into real Direct3D resources when it is told the contents changed. So each of these
// becomes two nops, and where the whole function is nothing but the instruction and a return, that leaves a
// function that correctly does nothing.
// ---------------------------------------------------------------------------------------------------------------

static const unsigned WBINVD_SITES[] = {
    0x000e8f80,   // __WBINVD, the standalone helper the game calls around GPU handoffs
    0x000e91c0,   // FS_FatalErrorHandler
    0x00105e72,   // FUN_00105ac0, deep in the dormant D3D8 push-buffer code
};

static const unsigned char WBINVD_BYTES[2] = { 0x0f, 0x09 };

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's and the C++ constructors' initialisers, in place of _rtinit (0x000eddd3) and _cinit
// (0x000edd7b). Those walk the XBE's initialiser tables; these call the same entries, in the same order, by
// name - except the four DSOUND ones at the end of the constructor table (0x00114b94, 0x00114b9f, 0x0011a230,
// 0x0011a23b), which only point DSOUND's internal globals at its own data. DSOUND is never entered now.
// ---------------------------------------------------------------------------------------------------------------

static void Xbox_rtinit(void) {
    Xbox_mtinit();          // the table's first entry, 0x000f405e, already ours
    CrtInitFloatingPoint();
}

static void Xbox_cinit(void) {
    CrtSetupLibs();
    // The C initialisers
    CrtInitStdio();
    CrtInitAtExit();
    // The C++ constructors
    FUN_000f5460();
    FUN_000f5470();
    FUN_000f5480();
    FUN_000f5490();
    HUD_InitObjectiveStatusPaneWidth();
    FUN_000f54c0();
    HUD_InitInfoStatusPaneWidth();
    FUN_000f54f0();
    HUD_InitPickupStatusPaneWidth();
    FUN_000f5520();
    WeaponDataTableInit();
}

// ---------------------------------------------------------------------------------------------------------------
// The XBE's entry point (0x000eb2ac), which the loader calls. Like the original it starts the game on a thread of
// its own, with the XBE header's default stack size, and returns; the original rebooted to the dashboard if the
// thread could not be made.
// ---------------------------------------------------------------------------------------------------------------

#define XbeSizeOfHeaders     (*(uint32_t *)0x00010108u)
#define XbeCertificate       (*(uint32_t **)0x00010118u)   // its first word is the certificate's size
#define XbeDefaultStackSize  (*(uint32_t *)0x00010130u)
#define XbeTls               ((const uint32_t *)0x00162400u)   // the TLS directory
#define XapiThreadTlsSize    (*(uint32_t *)0x00300934u)

static void __cdecl Xbox_Entry(void) {
    // The certificate's size, clamped to what the headers hold, and the per-thread TLS block's size: its
    // template (end - start), the zero fill, rounded to 16, and a word for the pointer; the TLS index is minus
    // that in words. Both as the original, though nothing of ours reads them.
    uint32_t certificateLimit = XbeSizeOfHeaders - (uint32_t)(uintptr_t)XbeCertificate + 0x10000;
    if (certificateLimit < *XbeCertificate)
        *XbeCertificate = certificateLimit;
    int32_t tlsSize = (int32_t)((XbeTls[4] - XbeTls[0] + XbeTls[1] + 0xf) & ~0xfu) + 4;
    XapiThreadTlsSize = (uint32_t)tlsSize;
    **(int32_t **)(XbeTls + 2) = tlsSize / -4;

    HANDLE thread = CreateThread(NULL, XbeDefaultStackSize, mainXapiStartup, NULL, 0, NULL);
    if (thread == NULL) {
        printf("[startup] the game's thread could not be created (error %lu)\n", GetLastError());
        fflush(stdout);
        ExitProcess(1);
    }
    CloseHandle(thread);
}

// ---------------------------------------------------------------------------------------------------------------
// Installing all of it
//
// Patched by hand rather than through AUTOINJECT or FUNC_AT. That once kept these replacements out of a
// CXBX-hosted run, where CXBX's emulated kernel supplied the KPCR and the kernel image they avoid; it also
// lets the instruction-level patches below check the bytes they overwrite.
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
    WriteJump(0x000eb2ac, (void *)Xbox_Entry);               // the entry point
    WriteJump(0x000eb238, (void *)mainXapiStartup);          // the whole of process startup
    WriteJump(0x000ed8ac, (void *)Xbox_XapiInitProcess);     // heap and the XAPI initialiser table
    WriteJump(0x000e9a24, (void *)Xbox_GetLastError);
    WriteJump(0x000e9a4c, (void *)Xbox_SetLastError);
    WriteJump(0x000f3ec7, (void *)Xbox_getptd);              // the CRT's per-thread data
    WriteJump(0x000f3f49, (void *)Xbox_freeptd);
    WriteJump(0x000f405e, (void *)Xbox_mtinit);

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

DWORD WINAPI mainXapiStartup(LPVOID unused) {
    (void)unused;

    printf("[startup] thread running, initialising the process\n");
    fflush(stdout);

    Xbox_XapiInitProcess();

    printf("[startup] running the C runtime and C++ constructors\n");
    fflush(stdout);

    Xbox_rtinit();
    Xbox_cinit();

    printf("[startup] calling main\n");
    fflush(stdout);

    GameMain(0, NULL);

    // main loops forever, so this is only reached if it returns unexpectedly. The original would reboot here.
    printf("[startup] main returned - it is not supposed to\n");
    fflush(stdout);
    return 0;
}
