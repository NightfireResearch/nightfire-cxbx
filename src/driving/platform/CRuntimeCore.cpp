#include "CRuntime.h"
#include "CRuntimeInternal.h"

#include "Win32Types.h"
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's heap, locks, fatal stop, errno and character tests: what both engines' runtimes share (the same
// XDK library), over what each engine gives it (CRuntimeInternal.h: XAPI's process heap, the kernel's critical
// sections and bug check, the debugger's output, the per-thread block and the class table). The tags are the
// driving engine's; the action engine patches its own entry points to these (src/action/engine/CRuntime.cpp).
// ---------------------------------------------------------------------------------------------------------------

// The runtime's locks: 36 slots of a critical section and whether it is one of the preallocated ones, which
// _mtinitlocks points at static sections; the others are made on first use. The runtime's stdio and low-level I/O
// take them too. The sections are the Xbox's 28-byte ones, worked by the kernel's calls (EnterSection and the rest).
namespace Crt {

// The 14 locks _mtinitlocks points at the static sections are marked 1.
// XBE_GLOBAL(0x001d8de0, 0x120)
LockSlot LockTable[kLockCount] = {
    { NULL, 1 }, { NULL, 1 }, { NULL, 0 }, { NULL, 1 }, { NULL, 1 }, { NULL, 0 }, { NULL, 1 }, { NULL, 1 },
    { NULL, 1 }, { NULL, 0 }, { NULL, 1 }, { NULL, 0 }, { NULL, 1 }, { NULL, 1 }, { NULL, 1 }, { NULL, 0 },
    { NULL, 1 }, { NULL, 1 }, { NULL, 1 },
};
// XBE_GLOBAL(0x00244920, 0x188)
CriticalSection StaticLocks[kPreallocatedLocks];
} // namespace Crt


// _aexit_rtn: what _amsg_exit ends in. Nothing changes it.
typedef void (__cdecl *ExitFn)(int);
// XBE_GLOBAL(0x001d8b50, 4)
static ExitFn ExitRoutine = Crt::_exit;


namespace Crt {

namespace {

// Run-time error codes (_RT_*) for _amsg_exit.
enum {
    kErrorLock = 0x11,       // R6017
    kBanner = 252,           // the line break before the message
    kErrorRuntime = 255      // "runtime error "
};

// _HEAP_MAXREQ: anything larger is refused outright.
const size_t kMaxRequest = 0xffffffe0;

} // namespace

// The new-handler mode and handler (_newmode, _pnhHeap). Nothing in the game sets either, so a failed allocation
// returns null at once.
// XBE_GLOBAL(0x00244abc, 4)
static int NewMode;
// XBE_GLOBAL(0x00244ab8, 4)
static int (*NewHandler)(size_t);

// _adbgmsg: a hook the fatal-error banner calls if set; nothing sets it.
// XBE_GLOBAL(0x00244ae0, 4)
static void (*DebugMessageHook)(void);

// doexit's flags: that termination has run, and whether it returns to its caller (_exitflag).
// XBE_GLOBAL(0x002448f8, 4)
static int TerminationDone;
// XBE_GLOBAL(0x002448f4, 1)
static char ExitReturns;

namespace {

// Asks the new handler for memory: whether to try again.
int CallNewHandler(size_t size) {                                           // _callnewh
    int (*handler)(size_t) = NewHandler;
    return handler != NULL && handler(size) != 0;
}

// __active_heap (the driving engine's 0x0024b5a0, the action engine's 0x00300bb0) is never set, so it is 0 - neither the system heap (1) nor the small-block heap (3)
// that the runtime's tests look for. So malloc, whose test is "not the system heap", rounds every request up to
// 16 bytes, calloc and realloc, whose tests are "the small-block heap", do not, and no heap lock is ever taken.
void* HeapAllocate(size_t size) {                                           // _heap_alloc
    if (size == 0)
        size = 1;
    size = (size + 15) & ~size_t(15);
    return ProcessHeapAlloc(0, size);
}

void* NewHandlerMalloc(size_t size, int callNewHandler) {                   // _nh_malloc
    if (size > kMaxRequest)
        return NULL;
    for (;;) {
        void *block = HeapAllocate(size);
        if (block != NULL || callNewHandler == 0)
            return block;
        if (!CallNewHandler(size))
            return NULL;
    }
}

} // namespace

// The runtime's InitializeCriticalSectionAndSpinCount: the kernel's initialise, which cannot fail here (the
// original turns an out-of-memory exception from it into a failure), and no spin count.
static int InitialiseLock(CriticalSection *section) {
    InitialiseSection(section);
    return 1;
}

// Makes a lock that is not preallocated, under the lock-table lock: whether it could.
static int InitialiseLockNumber(int lock) {                                 // _mtinitlocknum
    LockSlot *slot = &LockTable[lock];
    if (slot->section != NULL)
        return 1;
    CriticalSection *section = (CriticalSection *)malloc(sizeof(CriticalSection));
    if (section == NULL) {
        *_errno() = kNoMemory;
        return 0;
    }
    _lock(kLockTableLock);
    if (slot->section == NULL) {
        if (!InitialiseLock(section)) {
            free(section);
            _unlock(kLockTableLock);
            *_errno() = kNoMemory;
            return 0;
        }
        slot->section = section;
    } else {
        free(section);                   // another thread made it first
    }
    _unlock(kLockTableLock);
    return 1;
}

// The runtime's run-time error messages (_NMSG_WRITE's table), written to the debugger.
static void WriteMessage(int code) {                                        // _NMSG_WRITE
    static const struct {
        int code;
        const char *text;
    } kMessages[] = {
        { 2, "R6002\n- floating point not loaded\n" },
        { 8, "R6008\n- not enough space for arguments\n" },
        { 9, "R6009\n- not enough space for environment\n" },
        { 10, "\nThis application has requested the Runtime to terminate it in an unusual way.\n"
              "Please contact the application's support team for more information.\n" },
        { 16, "R6016\n- not enough space for thread data\n" },
        { 17, "R6017\n- unexpected multithread lock error\n" },
        { 18, "R6018\n- unexpected heap error\n" },
        { 19, "R6019\n- unable to open console device\n" },
        { 24, "R6024\n- not enough space for _onexit/atexit table\n" },
        { 25, "R6025\n- pure virtual function call\n" },
        { 26, "R6026\n- not enough space for stdio initialization\n" },
        { 27, "R6027\n- not enough space for lowio initialization\n" },
        { 28, "R6028\n- unable to initialize heap\n" },
        { 120, "DOMAIN error\n" },
        { 121, "SING error\n" },
        { 122, "TLOSS error\n" },
        { 252, "\n" },
        { 255, "runtime error " },
    };
    // (The original compares one entry past the table when the code is not in it; every code passed is.)
    for (size_t i = 0; i < sizeof(kMessages) / sizeof(kMessages[0]); i++) {
        if (kMessages[i].code == code) {
            DebugOutput(kMessages[i].text);
            return;
        }
    }
}

} // namespace Crt

// ---- the heap

// FUNC_AT(0x001340e3)
void* Crt::malloc(size_t size) {
    return NewHandlerMalloc(size, NewMode);
}

// FUNC_AT(0x001331dc)
void Crt::free(void *block) {
    if (block != NULL)
        ProcessHeapFree(block);
}

// FUNC_AT(0x0013439d)
void* Crt::calloc(size_t count, size_t size) {
    size_t total = count * size;
    if (total == 0)
        total = 1;
    for (;;) {
        void *block = NULL;
        if (total <= kMaxRequest) {
            block = ProcessHeapAlloc(kHeapZeroMemory, total);
            if (block != NULL)
                return block;
        }
        if (NewMode == 0)
            return block;
        if (!CallNewHandler(total))
            return NULL;
    }
}

void* Crt::realloc(void *block, size_t size) {
    if (block == NULL)
        return malloc(size);
    if (size == 0) {
        free(block);
        return NULL;
    }
    for (;;) {
        void *moved = NULL;
        if (size <= kMaxRequest)
            moved = ProcessHeapReAlloc(block, size);
        if (moved != NULL || NewMode == 0)
            return moved;
        if (!CallNewHandler(size))
            return NULL;
    }
}

size_t Crt::_msize(void *block) {
    return ProcessHeapSize(block);
}

// ---- the locks

// Points the preallocated locks at the static sections and initialises them: 0 if one could not be.
// FUNC_AT(0x001363d8)
int Crt::_mtinitlocks() {
    CriticalSection *next = StaticLocks;
    for (int i = 0; i < kLockCount; i++) {
        if (LockTable[i].preallocated == 1) {
            LockTable[i].section = next++;
            if (!InitialiseLock(LockTable[i].section)) {
                LockTable[i].section = NULL;
                return 0;
            }
        }
    }
    return 1;
}

// Frees the locks made on first use. (No delete: the Xbox's critical sections have nothing to release.)
// FUNC_AT(0x00136421)
void Crt::_mtdeletelocks() {
    for (int i = 0; i < kLockCount; i++) {
        if (LockTable[i].section != NULL && LockTable[i].preallocated != 1) {
            free(LockTable[i].section);
            LockTable[i].section = NULL;
        }
    }
}

void Crt::_lock(int lock) {
    if (LockTable[lock].section == NULL && !InitialiseLockNumber(lock))
        _amsg_exit(kErrorLock);
    EnterSection(LockTable[lock].section);
}

void Crt::_unlock(int lock) {
    LeaveSection(LockTable[lock].section);
}

// ---- the fatal stop

// A pure virtual method's slot: calling one is the runtime's error 25 (R6025).
// FUNC_AT(0x00131d35)
void Crt::_purecall() {
    _amsg_exit(25);
}

// The banner, the message, and the runtime's exit, which does not return.
// FUNC_AT(0x0013566d)
void Crt::_amsg_exit(int code) {
    WriteMessage(kBanner);               // _FF_MSGBANNER
    if (DebugMessageHook != NULL)
        DebugMessageHook();
    WriteMessage(kErrorRuntime);
    WriteMessage(code);
    ExitRoutine(255);
}

// _exit: doexit(code, quick, not returning). The quick exit skips the exit list and the pre-terminators (stdio's
// flush); of the terminators, the driving engine's only one (0x001363cc) puts back the unhandled-exception filter
// that an initialiser (0x001363ba) took over, and our startup runs neither; the action engine has none. Then the
// kernel's bug check, which does not return.
// FUNC_AT(0x0013438b)
void Crt::_exit(int) {
    _lock(kExitLock);
    TerminationDone = 1;
    ExitReturns = 0;
    BugCheck(0);
}

// ---- per-thread state

// FUNC_AT(0x00133664)
int* Crt::_errno() {
    return &PerThread()->errorNumber;
}

// ---- characters

// FUNC_AT(0x0013399d)
int Crt::isdigit(int c) {
    return Ctype(c) & kDigit;
}

// FUNC_AT(0x001339ef)
int Crt::isalnum(int c) {
    return Ctype(c) & (kAlpha | kDigit);
}

// FUNC_AT(0x001327f9)
int Crt::tolower(int c) {
    return (Ctype(c) & kUpper) ? c + ('a' - 'A') : c;
}

int Crt::toupper(int c) {
    return (Ctype(c) & kLower) ? c - ('a' - 'A') : c;
}

