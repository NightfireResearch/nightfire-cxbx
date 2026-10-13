#include "CRuntime.h"
#include "KernelImports.h"
#include "CRuntimeInternal.h"

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "XboxStartup.h"
#include "XboxXapi.h"

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's heap, exit list, locks, per-thread state, character classes, qsort and x87 helpers.
// ---------------------------------------------------------------------------------------------------------------

// The process heap (XboxStartup.h's ProcessHeapHandle). XAPI's heap calls are Win32's (XboxXapi.cpp), so the
// runtime's malloc reaches HeapAlloc with the same arguments as through them.

// The exit list atexit appends to, made by _onexitinit (32 entries). Only exit() would run it, and nothing calls
// exit(): _exit, the one way out, leaves it alone.
namespace Crt {
// XBE_GLOBAL(0x0024b5b4, 4)
AtExitFn *OnExitBegin;
// XBE_GLOBAL(0x0024b5b0, 4)
AtExitFn *OnExitEnd;
} // namespace Crt

// The runtime's locks: 36 slots of a critical section and whether it is one of the preallocated ones, which
// _mtinitlocks points at static sections; the others are made on first use. Original code (stdio, low-level I/O)
// still takes them. The sections are the Xbox's 28-byte ones, worked by the kernel's calls through the import table.
namespace Crt {
struct CriticalSection {
    uint8_t bytes[0x1c];
};
struct LockSlot {
    CriticalSection *section;
    int preallocated;
};
} // namespace Crt
#define LockTable      ((LockSlot *)0x001d8de0u)
#define StaticLocks    ((CriticalSection *)0x00244920u)
static const int kLockCount = 36;


// _aexit_rtn: what _amsg_exit ends in, _exit. Nothing changes it.
#define ExitRoutine (*(void (__cdecl **)(int))0x001d8b50u)


// __lconv: the C locale's conventions, the game's table of them (0x001d8844).
// XBE_GLOBAL(0x001d8874, 4)
static Crt::Lconv *LocaleConventions = (Crt::Lconv *)0x001d8844u;

namespace Crt {
// _pctype, one entry into the C locale's class table (0x001a32e8) so that EOF indexes the first.
// XBE_GLOBAL(0x001d8a00, 4)
const unsigned short *CtypeTable = (const unsigned short *)0x001a32eau;
} // namespace Crt

namespace Crt {

namespace {

// Lock numbers.
enum {
    kExitLock = 8,
    kLockTableLock = 10
};

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

// __active_heap (0x0024b5a0) is never set, so it is 0 - neither the system heap (1) nor the small-block heap (3)
// that the runtime's tests look for. So malloc, whose test is "not the system heap", rounds every request up to
// 16 bytes, calloc and realloc, whose tests are "the small-block heap", do not, and no heap lock is ever taken.
void* HeapAllocate(size_t size) {                                           // _heap_alloc
    if (size == 0)
        size = 1;
    size = (size + 15) & ~size_t(15);
    return HeapAlloc(ProcessHeapHandle, 0, size);
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

PerThreadData* PerThread() {                                                // _getptd
    return (PerThreadData *)Xbox_getptd();
}

// The runtime's InitializeCriticalSectionAndSpinCount: the kernel's initialise, which cannot fail here (the
// original turns an out-of-memory exception from it into a failure), and no spin count.
static int InitialiseLock(CriticalSection *section) {
    KernelRtlInitializeCriticalSection(section);
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
            Xbox_OutputDebugStringA(kMessages[i].text);
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
        HeapFree(ProcessHeapHandle, 0, block);
}

// FUNC_AT(0x0013439d)
void* Crt::calloc(size_t count, size_t size) {
    size_t total = count * size;
    if (total == 0)
        total = 1;
    for (;;) {
        void *block = NULL;
        if (total <= kMaxRequest) {
            block = HeapAlloc(ProcessHeapHandle, HEAP_ZERO_MEMORY, total);
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
            moved = HeapReAlloc(ProcessHeapHandle, 0, block, size);
        if (moved != NULL || NewMode == 0)
            return moved;
        if (!CallNewHandler(size))
            return NULL;
    }
}

size_t Crt::_msize(void *block) {
    return HeapSize(ProcessHeapHandle, 0, block);
}

// ---- the exit list

namespace Crt {
namespace {

// Appends to the list, growing it when full by as much again (at most 512 entries more), or failing that by four
// entries. The test for full compares the block's size in bytes with the list's length plus one entry.
AtExitFn OnExitLocked(AtExitFn function) {                                  // _onexit_lk
    size_t size = _msize(OnExitBegin);
    if (size < size_t((char *)OnExitEnd - (char *)OnExitBegin) + sizeof(AtExitFn)) {
        size_t growth = size < 512 * sizeof(AtExitFn) ? size : 512 * sizeof(AtExitFn);
        AtExitFn *list = (AtExitFn *)realloc(OnExitBegin, size + growth);
        if (list == NULL) {
            list = (AtExitFn *)realloc(OnExitBegin, size + 4 * sizeof(AtExitFn));
            if (list == NULL)
                return NULL;
        }
        OnExitEnd = list + (OnExitEnd - OnExitBegin);
        OnExitBegin = list;
    }
    *OnExitEnd++ = function;
    return function;
}

AtExitFn OnExit(AtExitFn function) {                                        // _onexit
    _lock(kExitLock);
    AtExitFn result = OnExitLocked(function);
    _unlock(kExitLock);
    return result;
}

} // namespace
} // namespace Crt

// The exit list: room for 32 entries, none used. ENOMEM's _RT_ONEXIT (24) when there is no memory for it.
// FUNC_AT(0x00132a1b)
int Crt::_onexitinit() {
    OnExitBegin = (AtExitFn *)malloc(32 * sizeof(AtExitFn));
    if (OnExitBegin == NULL)
        return 24;
    *OnExitBegin = NULL;
    OnExitEnd = OnExitBegin;
    return 0;
}

// FUNC_AT(0x00132a7b)
int Crt::atexit(AtExitFn function) {
    return OnExit(function) != NULL ? 0 : -1;
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
    KernelRtlEnterCriticalSection(LockTable[lock].section);
}

void Crt::_unlock(int lock) {
    KernelRtlLeaveCriticalSection(LockTable[lock].section);
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
// flush); of the terminators, the only one (0x001363cc) puts back the unhandled-exception filter that an initialiser
// (0x001363ba) took over, and our startup runs neither. Then the kernel's bug check, which does not return.
// FUNC_AT(0x0013438b)
void Crt::_exit(int) {
    _lock(kExitLock);
    TerminationDone = 1;
    ExitReturns = 0;
    KernelKeBugCheck(0);
}

// ---- per-thread state

// FUNC_AT(0x00133ee0)
int Crt::rand() {
    PerThreadData *thread = PerThread();
    thread->randomSeed = thread->randomSeed * 214013 + 2531011;
    return (thread->randomSeed >> 16) & 0x7fff;
}

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

// FUNC_AT(0x001333d6)
Crt::Lconv* Crt::localeconv() {
    return LocaleConventions;
}

// ---- qsort

namespace Crt {
namespace {

// Exchanges two elements byte by byte.
void Swap(char *a, char *b, size_t width) {
    if (a != b) {
        while (width--) {
            char t = *a;
            *a++ = *b;
            *b++ = t;
        }
    }
}

// Selection sort for short runs: the largest to the end, repeatedly.
void ShortSort(char *lo, char *hi, size_t width, CompareFn compare) {   // shortsort
    while (hi > lo) {
        char *max = lo;
        for (char *p = lo + width; p <= hi; p += width) {
            if (compare(p, max) > 0)
                max = p;
        }
        Swap(max, hi, width);
        hi -= width;
    }
}

} // namespace
} // namespace Crt

// Quicksort on the median of the first, middle and last elements, partitions kept on a stack (the larger pushed,
// the smaller sorted next), runs of eight or fewer left to ShortSort. Not stable; the comparisons are the game's,
// in its order.
// FUNC_AT(0x00132db0)
void Crt::qsort(void *base, size_t count, size_t width, CompareFn compare) {
    const size_t kCutoff = 8;
    char *loStack[30], *hiStack[30];
    int stack = 0;

    if (count < 2 || width == 0)
        return;
    char *lo = (char *)base;
    char *hi = (char *)base + width * (count - 1);

    for (;;) {
        size_t size = (hi - lo) / width + 1;
        if (size <= kCutoff) {
            ShortSort(lo, hi, width, compare);
        } else {
            char *mid = lo + (size / 2) * width;
            if (compare(lo, mid) > 0)
                Swap(lo, mid, width);
            if (compare(lo, hi) > 0)
                Swap(lo, hi, width);
            if (compare(mid, hi) > 0)
                Swap(mid, hi, width);

            char *loGuy = lo;
            char *hiGuy = hi;
            for (;;) {
                if (mid > loGuy) {
                    do {
                        loGuy += width;
                    } while (loGuy < mid && compare(loGuy, mid) <= 0);
                }
                if (mid <= loGuy) {
                    do {
                        loGuy += width;
                    } while (loGuy <= hi && compare(loGuy, mid) <= 0);
                }
                do {
                    hiGuy -= width;
                } while (hiGuy > mid && compare(hiGuy, mid) > 0);
                if (hiGuy < loGuy)
                    break;
                Swap(loGuy, hiGuy, width);
                if (mid == hiGuy)
                    mid = loGuy;
            }

            // skip the elements equal to the partition element
            hiGuy += width;
            if (mid < hiGuy) {
                do {
                    hiGuy -= width;
                } while (hiGuy > mid && compare(hiGuy, mid) == 0);
            }
            if (mid >= hiGuy) {
                do {
                    hiGuy -= width;
                } while (hiGuy > lo && compare(hiGuy, mid) == 0);
            }

            if (hiGuy - lo >= hi - loGuy) {
                if (lo < hiGuy) {
                    loStack[stack] = lo;
                    hiStack[stack] = hiGuy;
                    stack++;
                }
                if (loGuy < hi) {
                    lo = loGuy;
                    continue;
                }
            } else {
                if (loGuy < hi) {
                    loStack[stack] = loGuy;
                    hiStack[stack] = hi;
                    stack++;
                }
                if (lo < hiGuy) {
                    hi = hiGuy;
                    continue;
                }
            }
        }

        if (--stack < 0)
            return;
        lo = loStack[stack];
        hi = hiStack[stack];
    }
}

// ---- the x87

namespace Crt {
namespace {

// What the _CI functions answer outside the instructions' own range: a domain error (errno EDOM), the argument
// itself if it is a NaN (quietened), the x87's indefinite NaN otherwise.
double DomainError(double x) {
    *_errno() = kDomainError;
    uint64_t bits;
    memcpy(&bits, &x, 8);
    if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (bits & 0x000fffffffffffffull) != 0)
        bits |= 0x0008000000000000ull;
    else
        bits = 0xfff8000000000000ull;
    double result;
    memcpy(&result, &bits, 8);
    return result;
}

// A NaN operand's NaN, as the x87 picks it from two (the larger significand, quietened).
__declspec(naked) double X87NaN(double, double) {
    __asm {
        fld qword ptr [esp + 4]
        fadd qword ptr [esp + 12]
        ret
    }
}

// pow where the x87 path does not apply: a zero, infinite or NaN operand, or a negative base with a fractional
// exponent. As the runtime: a NaN operand gives a NaN and +-1 to an infinite power the indefinite NaN, both domain
// errors (errno EDOM); a negative base with a fractional exponent the indefinite NaN likewise; a zero base a signed
// zero or an infinity (a pole: errno ERANGE). Infinite operands otherwise follow C's rules (pow), as the runtime's do.
double PowEdge(double x, double y) {
    if (x != x || y != y) {
        *_errno() = kDomainError;
        return X87NaN(x, y);
    }
    if (x == 0.0 && fabs(y) != INFINITY) {
        if (y == 0.0)
            return 1.0;
        bool odd = floor(y) == y && fmod(y, 2.0) != 0.0;
        bool negative = signbit(x) && odd;
        if (y < 0.0) {
            *_errno() = kRangeError;
            return negative ? -INFINITY : INFINITY;
        }
        return negative ? -0.0 : 0.0;
    }
    if (fabs(x) == 1.0 && fabs(y) == INFINITY)
        return DomainError(x);
    if (x < 0.0 && fabs(x) != INFINITY && fabs(y) != INFINITY && floor(y) != y)
        return DomainError(x);
    return pow(x, y);
}

// pow's answer past the largest double: the infinity of its sign, a range error (errno ERANGE).
double PowOverflow(double answer) {
    *_errno() = kRangeError;
    return answer;
}

// pow's answer below the normal doubles, as the runtime's masked-underflow handling (_handle_exc) makes it from the
// answer scaled up by 2^1536 and rounded to a double: the mantissa shifted down to a denormal, the bits shifted out
// dropped rather than rounded.
double PowUnderflow(double scaled) {
    if (scaled == 0.0)
        return scaled;
    uint64_t bits;
    memcpy(&bits, &scaled, 8);
    int exponent = int((bits >> 52) & 0x7ff) - 0x3fe - 1536;
    if (exponent < -1074)
        return scaled * 0.0;
    uint64_t mantissa = (bits & 0x000fffffffffffffull) | 0x0010000000000000ull;
    if (exponent < -1021)
        mantissa >>= -1021 - exponent;
    double result;
    memcpy(&result, &mantissa, 8);
    return scaled < 0.0 ? -result : result;
}

// The x87 status word's exception flags as the runtime's _SW_ bits (__abstract_sw).
unsigned AbstractStatus(unsigned status) {
    unsigned abstract = 0;
    if (status & 0x01)
        abstract = 0x10;                 // invalid
    if (status & 0x04)
        abstract |= 0x08;                // divide by zero
    if (status & 0x08)
        abstract |= 0x04;                // overflow
    if (status & 0x10)
        abstract |= 0x02;                // underflow
    if (status & 0x20)
        abstract |= 0x01;                // inexact
    if (status & 0x02)
        abstract |= 0x80000;             // denormal
    return abstract;
}

} // namespace
} // namespace Crt

// The arc cosine of ST0, answered in ST0 unrounded: atan2(sqrt((1 + x)(1 - x)), x) on the x87, as the runtime
// computes it (with the FPU at the game's 53-bit precision); exactly pi or 0 at -1 and 1.
// FUNC_AT(0x001328d0)
__declspec(naked) void Crt::CIacos() {
    __asm {
        sub esp, 8
        fst qword ptr [esp]
        mov eax, dword ptr [esp + 4]
        and eax, 0x7ff00000
        cmp eax, 0x3ff00000
        jae aboveInside
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        fxch st(1)
        fpatan
        add esp, 8
        ret
    aboveInside:
        ja domainError
        mov eax, dword ptr [esp + 4]
        and eax, 0x000fffff
        or eax, dword ptr [esp]
        jnz domainError
        fstp st(0)
        test byte ptr [esp + 7], 0x80
        jz acosOfOne
        fldpi
        add esp, 8
        ret
    acosOfOne:
        fldz
        add esp, 8
        ret
    domainError:
        fstp st(0)
        call DomainError
        add esp, 8
        ret
    }
}

// The arc sine likewise: atan2(x, sqrt((1 + x)(1 - x))); exactly +-pi/2 at +-1.
// FUNC_AT(0x00133f94)
__declspec(naked) void Crt::CIasin() {
    __asm {
        sub esp, 8
        fst qword ptr [esp]
        mov eax, dword ptr [esp + 4]
        and eax, 0x7ff00000
        cmp eax, 0x3ff00000
        jae aboveInside
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        fpatan
        add esp, 8
        ret
    aboveInside:
        ja domainError
        mov eax, dword ptr [esp + 4]
        and eax, 0x000fffff
        or eax, dword ptr [esp]
        jnz domainError
        fstp st(0)
        fld1
        fchs
        fldpi
        fscale
        fstp st(1)
        test byte ptr [esp + 7], 0x80
        jz asinPositive
        fchs
    asinPositive:
        add esp, 8
        ret
    domainError:
        fstp st(0)
        call DomainError
        add esp, 8
        ret
    }
}

// pow(ST1, ST0), answered in ST0 unrounded: 2 to the power y * log2|x| on the x87, as the runtime computes it,
// negated for a negative base with an odd exponent; an answer below the normal doubles is PowUnderflow's. A negative
// base needs a whole exponent; zero, infinite and NaN operands are PowEdge's.
// FUNC_AT(0x00132a94)
__declspec(naked) void Crt::CIpow() {
    __asm {
        sub esp, 24
        fstp qword ptr [esp + 8]         // the exponent
        fstp qword ptr [esp]             // the base
        mov eax, dword ptr [esp + 12]
        and eax, 0x7ff00000
        cmp eax, 0x7ff00000
        je edgeCase
        mov eax, dword ptr [esp + 4]
        and eax, 0x7ff00000
        cmp eax, 0x7ff00000
        je edgeCase
        mov eax, dword ptr [esp + 4]
        and eax, 0x7fffffff
        or eax, dword ptr [esp]
        jz edgeCase
        fld qword ptr [esp + 8]
        fld qword ptr [esp]
        xor ecx, ecx                     // 1: an odd exponent of a negative base
        test byte ptr [esp + 7], 0x80
        jz positiveBase
        fld st(1)                        // is the exponent whole, and odd?
        fld st(0)
        frndint
        fcomp st(1)
        fnstsw ax
        sahf
        jne notWhole
        fld1                             // halved
        fchs
        fxch st(1)
        fscale
        fstp st(1)
        fld st(0)
        frndint
        fcompp
        fnstsw ax
        sahf
        je evenExponent
        inc ecx
    evenExponent:
        fchs                             // the base made positive
    positiveBase:
        fyl2x
        fld st(0)                        // 2 to that: 2^(v - round v) scaled by 2^round v
        frndint
        fsubr st(1), st
        fxch st(1)
        fchs
        f2xm1
        fld1
        faddp st(1), st
        fscale
        fstp st(1)
        test ecx, ecx
        jz rangeCheck
        fchs
    rangeCheck:
        fst qword ptr [esp + 16]         // does it fit a double?
        mov eax, dword ptr [esp + 20]
        and eax, 0x7ff00000
        jz underflow
        cmp eax, 0x7ff00000
        je outOfRange
        add esp, 24
        ret
    underflow:
        sub esp, 8                       // scaled up by 2^1536, or a zero of its sign if still too small
        mov dword ptr [esp], 0
        mov dword ptr [esp + 4], 0x40980000
        fld qword ptr [esp]
        fxch st(1)
        fscale
        fstp st(1)
        mov dword ptr [esp + 4], 0x00100000
        fld st(0)
        fabs
        fcomp qword ptr [esp]
        fnstsw ax
        sahf
        jae scaled
        fldz
        fmulp st(1), st
    scaled:
        fstp qword ptr [esp]
        call PowUnderflow
        add esp, 32
        ret
    outOfRange:
        fstp st(0)
        fld qword ptr [esp + 16]
        fstp qword ptr [esp]
        call PowOverflow
        add esp, 24
        ret
    notWhole:
        fstp st(0)
        fstp st(0)
        fstp st(0)
    edgeCase:
        call PowEdge
        add esp, 24
        ret
    }
}

// _fpmath, which _cinit calls first: the conversion pointers printf and scanf would go through (_cfltcvt_init: left
// out, our printf and scanf call the conversions directly), then the x87 at 53-bit precision (_setdefaultprecision:
// _controlfp(_PC_53, _MCW_PC), which leaves the other control bits as they are) and its exceptions cleared.
// FUNC_AT(0x00131d28)
__declspec(naked) void Crt::_fpmath() {
    __asm {
        push eax
        fnstcw word ptr [esp]
        and word ptr [esp], 0xfcff
        or word ptr [esp], 0x0200
        fldcw word ptr [esp]
        fnclex
        pop eax
        ret
    }
}

// The x87 status as the runtime abstracts it, the exceptions cleared.
// FUNC_AT(0x00133c9e)
__declspec(naked) unsigned Crt::_clearfp() {
    __asm {
        push ecx
        fnstsw word ptr [esp]
        fnclex
        call AbstractStatus
        pop ecx
        ret
    }
}
