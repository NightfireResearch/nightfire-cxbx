#include "CRuntime.h"
#include "KernelImports.h"
#include "CRuntimeInternal.h"

#include "Win32Types.h"
#include <stdint.h>
#include <string.h>

#include "Host.h"
#include "XboxStartup.h"
#include "XboxXapi.h"

// ---------------------------------------------------------------------------------------------------------------
// The driving engine's own part of the C runtime: what the shared files (CRuntimeCore.cpp, CRuntimeX87.cpp, the
// printf and float files) stand on - XAPI's process heap, the kernel's critical sections, the per-thread block and
// the class table - and the exit list, rand, localeconv and qsort, which only this engine's code uses.
// ---------------------------------------------------------------------------------------------------------------

// The exit list atexit appends to, made by _onexitinit (32 entries). Only exit() would run it, and nothing calls
// exit(): _exit, the one way out, leaves it alone.
namespace Crt {
// XBE_GLOBAL(0x0024b5b4, 4)
AtExitFn *OnExitBegin;
// XBE_GLOBAL(0x0024b5b0, 4)
AtExitFn *OnExitEnd;
} // namespace Crt

namespace Crt {
// SIGSEGV for an access violation, SIGILL for an illegal or privileged instruction, SIGFPE for the x87's faults.
// XBE_GLOBAL(0x001d92b0, 0x78)
ExceptionAction ExceptionActions[10] = {
    { 0xc0000005, 11, NULL }, { 0xc000001d, 4, NULL }, { 0xc0000096, 4, NULL }, { 0xc000008d, 8, NULL },
    { 0xc000008e, 8, NULL }, { 0xc000008f, 8, NULL }, { 0xc0000090, 8, NULL }, { 0xc0000091, 8, NULL },
    { 0xc0000092, 8, NULL }, { 0xc0000093, 8, NULL },
};
} // namespace Crt

// The C locale's conventions: "." and empty strings, and CHAR_MAX for each count.
// XBE_GLOBAL(0x001d8840, 2)
static char LconvDecimalPoint[] = ".";
// XBE_GLOBAL(0x002448ec, 1)
static char LconvEmpty[1];
// XBE_GLOBAL(0x001d8844, 0x30)
static Crt::Lconv CLocaleConventions = {
    LconvDecimalPoint, LconvEmpty, LconvEmpty, LconvEmpty, LconvEmpty, LconvEmpty, LconvEmpty, LconvEmpty,
    LconvEmpty, LconvEmpty, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
};
// __lconv
// XBE_GLOBAL(0x001d8874, 4)
static Crt::Lconv *LocaleConventions = &CLocaleConventions;

namespace Crt {
// _pctype, one entry into the C locale's class table so that EOF indexes the first. A class test indexes it with
// whatever int it is given - a negative signed char reads the 127 words before the table, which in the original
// are its exception records - so the table is kept as one block with them.
constexpr CtypeBlock MakeCtypeBlock() {
    CtypeBlock block = {
        {
        0x6f65, 0x6266, 0x7469, 0x7320, 0x7465, 0x0000, 0x0000, 0x6f69, 0x5f73, 0x6162, 0x6573, 0x3a3a, 0x6166,
        0x6c69, 0x6962, 0x2074, 0x6573, 0x0074, 0x0000, 0x6f69, 0x5f73, 0x6162, 0x6573, 0x3a3a, 0x6162, 0x6264,
        0x7469, 0x7320, 0x7465, 0x0000, 0x0000, 0x19e7, 0x0013,
        },
        {
        0x0000, 0x0000, 0x0004, 0x0000,
        },
        {
        0x1c01, 0x0013,
        },
        {
        0x0000, 0x0000, 0x0058, 0x0000,
        },
        {
        0xffff, 0xffff, 0x0000, 0x0000, 0x21e8, 0x0013, 0x7363, 0xe06d, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0003, 0x0000, 0x0520, 0x1993, 0x0000, 0x0000, 0x0000, 0x0000, 0x26ea, 0x0013, 0x26a1, 0x0013,
        0x6e55, 0x6e6b, 0x776f, 0x206e, 0x7865, 0x6563, 0x7470, 0x6f69, 0x006e, 0x0000, 0x2706, 0x0013, 0x26a1,
        0x0013, 0x274b, 0x0013, 0x0000, 0x0000, 0xffff, 0xffff, 0x0000, 0x0000, 0x2a75, 0x0013, 0x0000, 0x0000,
        0xffff, 0xffff, 0x0000, 0x0000, 0x3229, 0x0013, 0x0000, 0x0000, 0xffff, 0xffff, 0x0000, 0x0000, 0x32b8,
        0x0013, 0x0000, 0x0000, 0xffff, 0xffff, 0x32ff, 0x0013, 0x3323, 0x0013, 0x0000, 0x0000, 0xffff, 0xffff,
        0x0000, 0x0000, 0x3376, 0x0013, 0x0000, 0x0000,
        },
        0,
        {},
    };
    for (int c = 0; c < 256; c++)
        block.classes[c] = CLocaleClass(c);
    return block;
}

// XBE_GLOBAL(0x001a31ea, 0x300) readonly
extern const CtypeBlock CLocaleCtype;
const CtypeBlock CLocaleCtype = MakeCtypeBlock();
// XBE_GLOBAL(0x001d8a00, 4)
const unsigned short *CtypeTable = CLocaleCtype.classes;
// XBE_GLOBAL(0x001d8a0c, 1)
extern const char DecimalPoint = '.';                     // __decimal_point
} // namespace Crt

namespace Crt {

PerThreadData* PerThread() {                                                // _getptd
    return (PerThreadData *)Xbox_getptd();
}

// The process heap (XboxStartup.h's ProcessHeapHandle). XAPI's heap calls are the host's (XboxXapi.cpp, Host.h),
// so the runtime's malloc reaches HeapAlloc with the same arguments as through them.
void* ProcessHeapAlloc(uint32_t flags, size_t size) {
    return HostHeapAlloc(ProcessHeapHandle, flags, size);
}

void ProcessHeapFree(void *block) {
    HostHeapFree(ProcessHeapHandle, 0, block);
}

void* ProcessHeapReAlloc(void *block, size_t size) {
    return HostHeapReAlloc(ProcessHeapHandle, 0, block, size);
}

size_t ProcessHeapSize(void *block) {
    return HostHeapSize(ProcessHeapHandle, 0, block);
}

void InitialiseSection(CriticalSection *section) {
    KernelRtlInitializeCriticalSection(section);
}

void EnterSection(CriticalSection *section) {
    KernelRtlEnterCriticalSection(section);
}

void LeaveSection(CriticalSection *section) {
    KernelRtlLeaveCriticalSection(section);
}

void BugCheck(uint32_t code) {
    KernelKeBugCheck(code);
}

void DebugOutput(const char *text) {
    Xbox_OutputDebugStringA(text);
}

} // namespace Crt

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

// ---- per-thread state

// FUNC_AT(0x00133ee0)
int Crt::rand() {
    PerThreadData *thread = PerThread();
    thread->randomSeed = thread->randomSeed * 214013 + 2531011;
    return (thread->randomSeed >> 16) & 0x7fff;
}

// ---- characters

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
