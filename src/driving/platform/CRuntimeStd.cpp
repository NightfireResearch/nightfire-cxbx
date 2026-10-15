// The C++ half of the game's runtime. See CRuntimeStd.h.

#pragma fp_contract(off)

#include "CRuntimeStd.h"
#include "../Vtables.h"   // our copies of the game's vtables
#include "KernelImports.h"

#include "CRuntime.h"
#include "CRuntimeInternal.h"
#include "Host.h"
#include "../data/StdStreams.h"
#include "../engine/UMemory.hpp"
#include "../../common/xbeOverload.h"
#include "../anim/Model.h"   // OutOfRangeError

#include "Win32Types.h"
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace GameStd;

// ---- the game's vtables and throw information

#define IosBaseVtable ((const void *const *)Vtable_0018bc70)
#define BasicIosVtable ((const void *const *)Vtable_0018bcc8)
#define IstreamIosVtable ((const void *const *)Vtable_0018bccc)      // basic_ios inside a basic_istream
#define StrStreambufVtable ((const void *const *)Vtable_0018bdc4)
#define IStrStreamIosVtable ((const void *const *)Vtable_0018bdf8)   // basic_ios inside an istrstream
#define OstreamIosVtable ((const void *const *)Vtable_001a3228)      // basic_ios inside a basic_ostream
#define OstreamVbtable ((const int *)Crt::CLocaleCtype.ostreamVbtable)
#define OStrStreamIosVtable ((const void *const *)Vtable_001a3234)   // basic_ios inside an ostrstream
#define OStrStreamVbtable ((const int *)Crt::CLocaleCtype.ostrstreamVbtable)
#define LocimpVtable ((const void *const *)Vtable_001a31c0)
#define ExceptionVtable ((const void *)Vtable_001a326c)
#define BadCastVtable ((const void *)Vtable_001a3288)
#define RuntimeErrorVtable ((const void *)Vtable_001a31cc)
#define FailureVtable ((const void *)Vtable_001a31d4)                // ios_base::failure
#define OutOfRangeVtable ((const void *)Vtable_0018a29c)
#define LengthErrorVtable ((const void *)Vtable_00189eec)
#define FacetVtable ((const void *const *)Vtable_0018bc6c)
extern void *TypeInfoVtable[1];

// ---- the library's globals

namespace GameStd {
// The library's static _Init_locks objects and its _Init_atexit, which the static initialisers construct (names ours)
// XBE_GLOBAL(0x00244828, 1)
InitLocks InitLocksFirst;
// XBE_GLOBAL(0x002448d4, 1)
InitLocks InitLocksSecond;
// XBE_GLOBAL(0x002448d5, 1)
InitAtExit TheInitAtExit;
// One per _Lockit kind
// XBE_GLOBAL(0x002447b8, 0x70)
CriticalSection LibraryLocks[4];
// XBE_GLOBAL(0x001d8720, 4)
volatile long InitLocksCount = -1;         // _Init_locks::_Init_cnt
// XBE_GLOBAL(0x0024482c, 4)
FacetNode *FacetList;                      // _Fac_head
// XBE_GLOBAL(0x00244830, 4)
Locimp *GlobalLocale;                      // locale::_Locimp::_Global
// XBE_GLOBAL(0x00244834, 4)
Locimp *ClassicImpl;                       // locale::_Locimp::_Clocptr
// XBE_GLOBAL(0x0024483c, 4)
Locale ClassicLocale;                      // the locale classic() returns
// XBE_GLOBAL(0x00244840, 1)
bool GlobalTidyRegistered;
// XBE_GLOBAL(0x00244854, 0x34)
StandardStreamTable StandardStreams;       // ios_base::_Stdstr and _Stdopens
// XBE_GLOBAL(0x001d8770, 4)
uint32_t AtExitCount = 10;                 // _Atcount: the free slots
// XBE_GLOBAL(0x002448ac, 0x28)
AtExitFn AtExitFunctions[10];              // _Atfuns, filled from the end
} // namespace GameStd

// ---- the C runtime's locale state (__lc_handle, __lc_codepage): the C locale's, which nothing changes

// XBE_GLOBAL(0x00244ac0, 0x18)
static const uint32_t LocaleHandles[6] = {};
// XBE_GLOBAL(0x00244ad8, 4)
static const uint32_t LocaleCodePage = 0;

// ---- the kernel's critical sections, through the import table


namespace {

typedef Facet* (Facet::*FacetDeleteMethod)(unsigned flags);

enum {
    kLockLocale = 0,           // _LOCK_LOCALE
    kLockStream = 2,           // _LOCK_STREAM
    kLibraryLocks = 4,
    kAtExitSlots = 10,
    kStandardStreams = 8,      // _Nstdstr
};

enum {
    kAllCategories = 0x3f,     // locale::all
    kCollate = 1,              // LC_COLLATE
    kCtypeCategory = 2,        // LC_CTYPE
    kLcAll = 0,                // LC_ALL
};

enum { kEraseEvent = 0 };      // ios_base::erase_event
enum { kStateMask = kEofBit | kFailBit | kBadBit };
enum { kAppend = 0x8 };        // ios_base::app
enum { kIn = 0x1, kOut = 0x2 };                     // ios_base::in, ios_base::out
enum { kSeekBeg = 0, kSeekCur = 1, kSeekEnd = 2 };  // ios_base::beg, cur, end
enum { kTypeInfoLock = 14 };   // _TYPEINFO_LOCK

// strstreambuf::_Strstate
enum {
    kAllocated = 0x1,
    kConstant = 0x2,
    kDynamic = 0x4,
    kFrozen = 0x8,
};
const int kMinAllocSize = 32;  // _MINSIZE

// _Stod's scaling: well above the doubles' normal range, and back.
const int kScaleBits = 960;
const double kScale = 0x1p960;
const double kSmallest = 0x1p-960;

// The C++ exception as the compiler raises it.
const DWORD kCxxExceptionCode = 0xe06d7363;
const ULONG_PTR kCxxMagic = 0x19930520;

// _Atexit: a function for _Init_atexit's destructor to run at exit.
void Atexit(AtExitFn function) {
    if (AtExitCount == 0)
        Crt::_amsg_exit(10);   // abort() in the original; never reached, only two functions are registered
    AtExitFunctions[--AtExitCount] = function;
}

// The Xbox runtime's setlocale (0x001347c2) knows only the C locale and changes nothing.
const char* SetLocale(int, const char *name) {
    if (name == NULL || name[0] == '\0' || (name[0] == 'C' && name[1] == '\0'))
        return "C";
    return NULL;
}

void DeleteFacet(Facet *facet) {
    (facet->*XbeVirtual<FacetDeleteMethod>(facet, 0))(1);
}

// _Fac_tidy: releases the registered facets at exit.
void FacetTidy() {
    Lockit lock;
    lock.Construct(kLockLocale);
    while (FacetList != NULL) {
        FacetNode *node = FacetList;
        FacetList = node->next;
        node->Destruct();
        OperatorDelete(node);
    }
    lock.Destruct();
}

// _Deletegloballocale, under the locale lock (tidy_global): at exit.
void TidyGlobal() {
    Lockit lock;
    lock.Construct(kLockLocale);
    if (GlobalLocale != NULL) {
        Facet *dead = GlobalLocale->Decref();
        if (dead != NULL)
            DeleteFacet(dead);
    }
    lock.Destruct();
}

// _Setgloballocale
void SetGlobalLocale(Locimp *impl) {
    if (!GlobalTidyRegistered) {
        GlobalTidyRegistered = true;
        Atexit(&TidyGlobal);
    }
    GlobalLocale = impl;
}

// locale::_Init: the global locale, made the first time as the classic "C" one.
Locimp *InitLocale() {
    Locimp *impl = GlobalLocale;
    if (impl == NULL) {
        Lockit lock;
        lock.Construct(kLockLocale);
        impl = GlobalLocale;
        if (impl == NULL) {
            Locimp *made = (Locimp *)OperatorNew(sizeof(Locimp));
            impl = made != NULL ? made->Construct(false) : NULL;
            SetGlobalLocale(impl);
            impl->categories = kAllCategories;
            impl->name.Assign("C");
            ClassicImpl = impl;
            ClassicImpl->Incref();
            ClassicLocale.impl = ClassicImpl;
        }
        lock.Destruct();
    }
    return impl;
}

// _Stod's value is *value * 2^-*scale: scaled up while below the doubles' normal range, back once well inside it.
void KeepNormal(double *value, int *scale) {
    if (*value != 0 && fabs(*value) < kSmallest) {
        *value *= kScale;
        *scale += kScaleBits;
    } else if (*scale > 0 && fabs(*value) >= 1.0) {
        *value = ldexp(*value, -*scale);
        *scale = 0;
    }
}

// ios_base::failure with its message, thrown.
void ThrowFailure(const char *what) {
    String message;
    message.ConstructText(what);
    RuntimeError failure;
    failure.Construct(&message);
    failure.vtable = FailureVtable;
    Crt::CxxThrowException(&failure, &Crt::FailureThrowInfo);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// The library's locks

// FUNC_AT(0x00130c99)
InitLocks* GameStd::InitLocks::Construct() {
    if (HostInterlockedIncrement(&InitLocksCount) == 0) {
        for (int i = 0; i < kLibraryLocks; i++)
            KernelRtlInitializeCriticalSection(&LibraryLocks[i]);
    }
    return this;
}

// The last destruction would destroy the locks, which on the Xbox (_Mtxdst) leaves nothing to do.
// FUNC_AT(0x00130cc7)
void GameStd::InitLocks::Destruct() {
    HostInterlockedDecrement(&InitLocksCount);
}

// FUNC_AT(0x00130cef)
Lockit* GameStd::Lockit::Construct(int lockKind) {
    kind = lockKind & 3;
    KernelRtlEnterCriticalSection(&LibraryLocks[kind]);
    return this;
}

// FUNC_AT(0x00130d10)
void GameStd::Lockit::Destruct() {
    KernelRtlLeaveCriticalSection(&LibraryLocks[kind]);
}

// The allocation is not checked.
// FUNC_AT(0x001313fe)
Mutex* GameStd::Mutex::Construct() {
    section = (CriticalSection *)OperatorNew(sizeof(CriticalSection));
    KernelRtlInitializeCriticalSection(section);
    return this;
}

// FUNC_AT(0x00131416)
void GameStd::Mutex::Destruct() {
    OperatorDelete(section);
}

// FUNC_AT(0x0013142b)
void GameStd::Mutex::Lock() {
    KernelRtlEnterCriticalSection(section);
}

// FUNC_AT(0x00131434)
void GameStd::Mutex::Unlock() {
    KernelRtlLeaveCriticalSection(section);
}

// FUNC_AT(0x00131cd0)
void GameStd::InitAtExit::Destruct() {
    while (AtExitCount < kAtExitSlots) {
        AtExitFn function = AtExitFunctions[AtExitCount++];
        function();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// locale

Locimp* GameStd::Locimp::Construct(bool isTransparent) {
    refs = 1;
    facets = NULL;
    facetCount = 0;
    categories = 0;
    vtable = LocimpVtable;
    transparent = isTransparent;
    name.ConstructText("*");
    return this;
}

// FUNC_AT(0x00130e4c)
void GameStd::Locimp::Destruct() {
    vtable = LocimpVtable;
    Lockit lock;
    lock.Construct(kLockLocale);
    for (uint32_t i = facetCount; i > 0;) {
        --i;
        if (facets[i] != NULL) {
            Facet *dead = facets[i]->Decref();
            if (dead != NULL)
                DeleteFacet(dead);
        }
    }
    Crt::free(facets);
    lock.Destruct();
    name.Tidy(true);
    vtable = FacetVtable;
}

// FUNC_AT(0x00130f23)
Locimp* GameStd::Locimp::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

void GameStd::FacetNode::Destruct() {
    Facet *dead = facet->Decref();
    if (dead != NULL)
        DeleteFacet(dead);
}

// FUNC_AT(0x001310bf)
Locale* GameStd::Locale::Construct() {
    impl = InitLocale();
    GlobalLocale->Incref();
    return this;
}

// FUNC_AT(0x00130d9b)
const Facet* GameStd::Locale::GetFacet(uint32_t id) const {
    const Locimp *own = (const Locimp *)impl;
    const Facet *facet = id < own->facetCount ? own->facets[id] : NULL;
    if (facet != NULL || !own->transparent)
        return facet;
    const Locimp *global = GlobalLocale;
    return id < global->facetCount ? global->facets[id] : NULL;
}

// FUNC_AT(0x00130e13)
void GameStd::Facet::Register() {
    if (FacetList == NULL)
        Atexit(&FacetTidy);
    FacetNode *node = (FacetNode *)OperatorNew(sizeof(FacetNode));
    if (node != NULL) {
        node->next = FacetList;
        node->facet = this;
    }
    FacetList = node;
}

// FUNC_AT(0x00130f8f)
Locinfo* GameStd::Locinfo::Construct(const char *localeName) {
    lock.Construct(kLockLocale);
    days.Tidy(false);
    months.Tidy(false);
    oldName.Tidy(false);
    newName.Tidy(false);
    oldName.Assign(SetLocale(kLcAll, NULL));
    const char *now = localeName != NULL ? SetLocale(kLcAll, localeName) : NULL;
    newName.Assign(now != NULL ? now : "*");
    return this;
}

// FUNC_AT(0x00130ed2)
void GameStd::Locinfo::Destruct() {
    if (oldName.size > 0)
        SetLocale(kLcAll, oldName.Data());
    newName.Tidy(true);
    oldName.Tidy(true);
    months.Tidy(true);
    days.Tidy(true);
    lock.Destruct();
}

// The handle is the collation category's, as the runtime filled it.
// FUNC_AT(0x00130c3c)
CtypeVec GameStd::Getctype() {
    CtypeVec ctype;
    ctype.handle = LocaleHandles[kCollate];
    ctype.page = LocaleCodePage;
    short *table = (short *)Crt::malloc(256 * sizeof(short));
    const short *classes = (const short *)Crt::CtypeTable;
    if (table != NULL) {
        memcpy(table, classes, 256 * sizeof(short));
        ctype.table = table;
        ctype.deleteTable = 1;
    } else {
        ctype.table = classes;
        ctype.deleteTable = 0;
    }
    return ctype;
}

// FUNC_AT(0x00131c31)
CvtVec GameStd::Getcvt() {
    CvtVec cvt;
    cvt.handle = LocaleHandles[kCtypeCategory];
    cvt.page = LocaleCodePage;
    return cvt;
}

// The Xbox runtime's versions ignore the table they are given and test the C locale's classes (the multibyte test
// beside each is never taken, see CRuntimeInternal.h).
// FUNC_AT(0x00130c00)
int GameStd::Tolower(int c, const CtypeVec *) {
    return (Crt::Ctype(c) & Crt::kUpper) ? Crt::tolower(c) : c;
}

// (toupper, inlined: its own test is the same one.)
// FUNC_AT(0x0013143d)
int GameStd::Toupper(int c, const CtypeVec *) {
    return (Crt::Ctype(c) & Crt::kLower) ? c - ('a' - 'A') : c;
}

// ---------------------------------------------------------------------------------------------------------------
// ios_base

// FUNC_AT(0x00131279)
void GameStd::IosBase::Clear(int newState, bool reraise) {
    state = newState & kStateMask;
    int raised = state & exceptions;
    if (raised == 0)
        return;
    if (reraise)
        Crt::CxxThrowException(NULL, NULL);
    else if (raised & kBadBit)
        ThrowFailure("ios_base::badbit set");
    else if (raised & kFailBit)
        ThrowFailure("ios_base::failbit set");
    else
        ThrowFailure("ios_base::eofbit set");
}

// FUNC_AT(0x001313b8)
void GameStd::IosBase::Init() {
    Locale *made = (Locale *)OperatorNew(sizeof(Locale));
    locale = made != NULL ? made->Construct() : NULL;
    exceptions = kGoodBit;
    flags = kSkipWs | kDec;
    precision = 6;
    width = 0;
    words = NULL;
    callbacks = NULL;
    Clear(kGoodBit, false);
}

// A ninth standard stream is written to the slots past the tables.
// FUNC_AT(0x001310fa)
void GameStd::IosBase::AddStandard() {
    Lockit lock;
    lock.Construct(kLockStream);
    for (++standardStream; standardStream < kStandardStreams; ++standardStream) {
        IosBase *open = StandardStreams.streams[standardStream];
        if (open == NULL || open == this)
            break;
    }
    StandardStreams.streams[standardStream] = this;
    ++StandardStreams.opens[standardStream];
    lock.Destruct();
}

// FUNC_AT(0x00131191)
void GameStd::IosBase::Destruct() {
    vtable = IosBaseVtable;
    if (standardStream > 0 && --StandardStreams.opens[standardStream] > 0)
        return;   // a standard stream still open elsewhere
    Tidy();
    Locale *owned = locale;
    if (owned != NULL) {
        owned->Destruct();
        OperatorDelete(owned);
    }
}

void GameStd::IosBase::Tidy() {
    CallFunctions(kEraseEvent);
    for (Word *word = words; word != NULL;) {
        Word *next = word->next;
        OperatorDelete(word);
        word = next;
    }
    words = NULL;
    for (Callback *callback = callbacks; callback != NULL;) {
        Callback *next = callback->next;
        OperatorDelete(callback);
        callback = next;
    }
    callbacks = NULL;
}

void GameStd::IosBase::CallFunctions(int event) {
    for (Callback *callback = callbacks; callback != NULL; callback = callback->next)
        callback->function(event, *this, callback->index);
}

// ---------------------------------------------------------------------------------------------------------------
// strstreambuf, istrstream, ostrstream

// FUNC_AT(0x00131901)
void GameStd::StrStreambuf::Init(int count, char *getBuffer, char *putBuffer, int strMode) {
    InitPointers();
    pendSave = NULL;
    seekHigh = NULL;
    allocFn = NULL;
    freeFn = NULL;
    allocSize = kMinAllocSize;
    mode = strMode;
    if (getBuffer == NULL) {
        mode |= kDynamic;
        if (count > kMinAllocSize)
            allocSize = count;
        return;
    }
    int size = count < 0 ? INT_MAX : count == 0 ? int(strlen(getBuffer)) : count;
    char *end = getBuffer + size;
    seekHigh = end;
    if (putBuffer == NULL) {
        *getFirst = getBuffer;   // setg(getBuffer, getBuffer, end)
        *getNext = getBuffer;
        *getCount = size;
    } else {
        if (putBuffer < getBuffer)
            putBuffer = getBuffer;
        else if (end < putBuffer)
            putBuffer = end;
        *putFirst = putBuffer;   // setp(putBuffer, end)
        *putNext = putBuffer;
        *putCount = int(end - putBuffer);
        *getFirst = getBuffer;   // setg(getBuffer, getBuffer, putBuffer)
        *getNext = getBuffer;
        *getCount = int(putBuffer - getBuffer);
    }
}

StrStreambuf* GameStd::StrStreambuf::ConstructRange(char *getBuffer, int count, char *putBuffer) {
    Streambuf::Construct();
    vtable = StrStreambufVtable;
    Init(count, getBuffer, putBuffer, 0);
    return this;
}

void GameStd::StrStreambuf::Tidy() {
    if ((mode & (kAllocated | kFrozen)) == kAllocated) {
        if (freeFn != NULL)
            freeFn(*getFirst);
        else
            OperatorDelete(*getFirst);
    }
    seekHigh = NULL;
    mode &= ~(kAllocated | kFrozen);
}

// FUNC_AT(0x00131a44)
void GameStd::StrStreambuf::Destruct() {
    vtable = StrStreambufVtable;
    Tidy();
    Streambuf::Destruct();
}

// A dynamic buffer grows by half its size (at least the allocation size asked for, then 32), halved while that
// would overflow, and keeps its pointers' places in the new block.
// FUNC_AT(0x001314ae)
int GameStd::StrStreambuf::Overflow(int ch) {
    if (ch == kEof)
        return 0;
    if (*putNext != NULL && *putNext < *putNext + *putCount) {
        --*putCount;
        *(*putNext)++ = char(ch);
        return uint8_t(ch);
    }
    if (!(mode & kDynamic) || (mode & (kConstant | kFrozen)))
        return kEof;
    int oldSize = *getNext == NULL ? 0 : int(*putNext + *putCount - *getFirst);
    int increment = oldSize / 2;
    if (increment < allocSize)
        increment = allocSize;
    allocSize = kMinAllocSize;
    while (increment > 0 && INT_MAX - increment < oldSize)
        increment /= 2;
    if (increment <= 0)
        return kEof;
    int newSize = oldSize + increment;
    char *buffer = allocFn != NULL ? (char *)allocFn(newSize) : (char *)OperatorNewArray(newSize);
    if (buffer == NULL)
        return kEof;
    if (oldSize > 0)
        memcpy(buffer, *getFirst, oldSize);
    if (mode & kAllocated) {
        if (freeFn != NULL)
            freeFn(*getFirst);
        else
            OperatorDelete(*getFirst);
    }
    mode |= kAllocated;
    if (oldSize == 0) {
        seekHigh = buffer;
        *putFirst = buffer;   // setp(buffer, buffer + newSize)
        *putNext = buffer;
        *putCount = newSize;
        *getFirst = buffer;   // setg(buffer, buffer, buffer)
        *getNext = buffer;
        *getCount = 0;
    } else {
        char *oldBuffer = *getFirst;
        seekHigh = buffer + (seekHigh - oldBuffer);
        char *next = buffer + (*putNext - oldBuffer);
        *putFirst = buffer + (*putFirst - oldBuffer);   // setp(pbase, pptr, buffer + newSize)
        *putNext = next;
        *putCount = int(buffer + newSize - next);
        char *getAt = buffer + (*getNext - oldBuffer);
        *getFirst = buffer;                              // setg(buffer, gptr, pptr + 1)
        *getNext = getAt;
        *getCount = int(*putNext - getAt) + 1;
    }
    --*putCount;
    *(*putNext)++ = char(ch);
    return uint8_t(ch);
}

// FUNC_AT(0x0013166a)
int GameStd::StrStreambuf::Pbackfail(int ch) {
    char *next = *getNext;
    if (next == NULL || next <= *getFirst || (ch != kEof && char(ch) != next[-1] && (mode & kConstant)))
        return kEof;
    ++*getCount;
    --*getNext;
    if (ch == kEof)
        return 0;
    **getNext = char(ch);
    return uint8_t(ch);
}

// The get area reaches as far as anything has been written.
// FUNC_AT(0x001316b3)
int GameStd::StrStreambuf::Underflow() {
    char *next = *getNext;
    if (next == NULL)
        return kEof;
    if (next < next + *getCount)
        return uint8_t(*next);
    char *written = *putNext;
    if (written == NULL || (written <= next && seekHigh <= next))
        return kEof;
    if (seekHigh < written)
        seekHigh = written;
    *getCount = int(seekHigh - *getNext);
    return uint8_t(**getNext);
}

// FUNC_AT(0x00131702)
StreamPos* GameStd::StrStreambuf::SeekOff(StreamPos *result, int offset, int way, int which) {
    if (*putNext != NULL && seekHigh < *putNext)
        seekHigh = *putNext;
    if ((which & kIn) && *getNext != NULL) {
        if (way == kSeekEnd)
            offset += int(seekHigh - *getFirst);
        else if (way == kSeekCur && !(which & kOut))
            offset += int(*getNext - *getFirst);
        else if (way != kSeekBeg || offset == -1)
            offset = -1;
        if (offset < 0 || seekHigh - *getFirst < offset) {
            offset = -1;
        } else {
            int move = int(*getFirst - *getNext) + offset;
            *getCount -= move;   // gbump
            *getNext += move;
            if ((which & kOut) && *putNext != NULL) {
                char *next = *putNext;   // setp(gptr, epptr)
                int count = *putCount;
                *putNext = *getNext;
                *putCount = int(next + count - *putNext);
            }
        }
    } else if ((which & kOut) && *putNext != NULL) {
        char *next = *putNext;
        if (way == kSeekEnd)
            offset += int(seekHigh - *getFirst);
        else if (way == kSeekCur)
            offset += int(next - *getFirst);
        else if (way != kSeekBeg || offset == -1)
            offset = -1;
        if (offset < 0 || seekHigh - *getFirst < offset) {
            offset = -1;
        } else {
            int move = int(*getFirst - next) + offset;
            *putCount -= move;   // pbump
            *putNext += move;
        }
    } else {
        offset = -1;
    }
    return result->Construct(offset);
}

// FUNC_AT(0x0013183b)
StreamPos* GameStd::StrStreambuf::SeekPos(StreamPos *result, StreamPos position, int which) {
    int offset = position.offset + int(position.position[0]);
    if (*putNext != NULL && seekHigh < *putNext)
        seekHigh = *putNext;
    if (offset != -1) {
        if ((which & kIn) && *getNext != NULL) {
            if (offset >= 0 && offset <= seekHigh - *getFirst) {
                int move = int(*getFirst - *getNext) + offset;
                *getCount -= move;
                *getNext += move;
                if ((which & kOut) && *putNext != NULL) {
                    char *next = *putNext;
                    int count = *putCount;
                    *putNext = *getNext;
                    *putCount = int(next + count - *putNext);
                }
                return result->Construct(offset);
            }
        } else if ((which & kOut) && *putNext != NULL && offset >= 0 && offset <= seekHigh - *getFirst) {
            int move = int(*getFirst - *putNext) + offset;
            *putCount -= move;
            *putNext += move;
            return result->Construct(offset);
        }
        offset = -1;
    }
    return result->Construct(offset);
}

// Entered at the virtual base; ~basic_istream inlined after the buffer's destructor.
// FUNC_AT(0x00131ae1)
void GameStd::BasicIos::DestructIStrStream() {
    IStrStream *stream = (IStrStream *)((char *)this - offsetof(IStrStream, ios));
    stream->Ios()->vtable = IStrStreamIosVtable;
    stream->buffer.Destruct();
    stream->Ios()->vtable = IstreamIosVtable;
}

// Entered at the virtual base; ~basic_ostream inlined after the buffer's destructor.
// FUNC_AT(0x00131bbb)
void GameStd::BasicIos::DestructOStrStream() {
    OStrStream *stream = (OStrStream *)((char *)this - offsetof(OStrStream, ios));
    stream->Ios()->vtable = OStrStreamIosVtable;
    stream->buffer.Destruct();
    stream->Ios()->vtable = OstreamIosVtable;
}

// Entered at the virtual base, as each of these.
// FUNC_AT(0x00131a06)
void GameStd::BasicIos::DestructOstream() {
    Ostream *stream = (Ostream *)((char *)this - 4);   // the basic_ios is at +4
    stream->Ios()->vtable = OstreamIosVtable;
    Destruct();
}

// FUNC_AT(0x001319e7)
void* GameStd::BasicIos::DeleteOstream(unsigned flags) {
    Ostream *stream = (Ostream *)((char *)this - 4);
    DestructOstream();
    if (flags & 1)
        OperatorDelete(stream);
    return stream;
}

// FUNC_AT(0x00131c01)
void* GameStd::BasicIos::DeleteOStrStream(unsigned flags) {
    OStrStream *stream = (OStrStream *)((char *)this - offsetof(OStrStream, ios));
    DestructOStrStream();
    Destruct();
    if (flags & 1)
        OperatorDelete(stream);
    return stream;
}

// basic_ostream(buffer, isStandard). With its virtual base (a plain basic_ostream, which the game never makes) the
// basic_ios follows the vbptr.
Ostream* GameStd::Ostream::Construct(Streambuf *buffer, bool isStandard, int constructVirtualBase) {
    if (constructVirtualBase) {
        vbtable = OstreamVbtable;
        Ios()->standardStream = 0;
        Ios()->vtable = BasicIosVtable;
    }
    Ios()->vtable = OstreamIosVtable;
    Ios()->Init(buffer, isStandard);
    return this;
}

// ostrstream(text, count, mode): writing starts at the end of the text for ios_base::app.
// FUNC_AT(0x00131b27)
OStrStream* GameStd::OStrStream::Construct(char *text, int count, int mode, int constructVirtualBase) {
    if (constructVirtualBase) {
        vbtable = OStrStreamVbtable;
        ios.standardStream = 0;
        ios.vtable = BasicIosVtable;
    }
    Ostream::Construct(&buffer, false, 0);
    Ios()->vtable = OStrStreamIosVtable;
    buffer.ConstructRange(text, count, text != NULL && (mode & kAppend) ? text + strlen(text) : text);
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// _Stod

// The x87 keeps the value through the loops: at the 53-bit precision the game runs at, each product rounds as a
// double's would, but with the x87's exponent, so a product outside the doubles' normal range is only rounded (to a
// denormal) once, by the caller's store. Here a power of two keeps it in range, and comes off at the end.
// FUNC_AT(0x00131c45)
double GameStd::Stod(const char *text, char **end, long powerOfTen) {
    double value = Crt::strtod(text, end);
    int scale = 0;
    for (; powerOfTen > 0; --powerOfTen) {
        KeepNormal(&value, &scale);
        value *= 10.0;
    }
    for (; powerOfTen < 0; ++powerOfTen) {
        KeepNormal(&value, &scale);
        value *= 0.1;
    }
    return ldexp(value, -scale);
}

// FUNC_AT(0x00131c79)
double GameStd::Stof(const char *text, char **end, long powerOfTen) {
    return Stod(text, end, powerOfTen);
}

// ---------------------------------------------------------------------------------------------------------------
// Exceptions

// FUNC_AT(0x001325e6)
Exception* GameStd::Exception::Construct() {
    what = NULL;
    doFree = 0;
    vtable = ExceptionVtable;
    return this;
}

// exception(const char *const &): a copy of the message, from the C runtime's heap. A failed copy leaves no
// message but still one to free.
Exception* GameStd::Exception::ConstructMessage(const char *message) {
    vtable = ExceptionVtable;
    char *copy = (char *)Crt::malloc(strlen(message) + 1);
    what = copy;
    if (copy != NULL)
        strcpy(copy, message);
    doFree = 1;
    return this;
}

// FUNC_AT(0x0013263c)
Exception* GameStd::Exception::ConstructCopy(const Exception *other) {
    vtable = ExceptionVtable;
    doFree = other->doFree;
    if (doFree) {
        char *copy = (char *)Crt::malloc(strlen(other->what) + 1);
        what = copy;
        if (copy != NULL)
            strcpy(copy, other->what);
    } else {
        what = other->what;
    }
    return this;
}

// FUNC_AT(0x0013268b)
void GameStd::Exception::Destruct() {
    vtable = ExceptionVtable;
    if (doFree)
        Crt::free((void *)what);
}

// FUNC_AT(0x001326ea)
Exception* GameStd::Exception::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x001326a1)
const char* GameStd::Exception::What() {
    return what != NULL ? what : "Unknown exception";
}

// FUNC_AT(0x001326ae)
BadCast* GameStd::BadCast::Construct(const char *message) {
    ConstructMessage(message);
    vtable = BadCastVtable;
    return this;
}

// FUNC_AT(0x001326c7)
BadCast* GameStd::BadCast::ConstructCopy(const BadCast *other) {
    Exception::ConstructCopy(other);
    vtable = BadCastVtable;
    return this;
}

// FUNC_AT(0x001326df)
void GameStd::BadCast::Destruct() {
    vtable = BadCastVtable;
    Exception::Destruct();
}

// FUNC_AT(0x00132706)
BadCast* GameStd::BadCast::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

RuntimeError* GameStd::RuntimeError::Construct(const String *text) {
    Exception::Construct();
    vtable = RuntimeErrorVtable;
    message.ConstructCopy(text);
    return this;
}

// FUNC_AT(0x0013133f)
RuntimeError* GameStd::RuntimeError::ConstructCopy(const RuntimeError *other) {
    Exception::ConstructCopy(other);
    vtable = RuntimeErrorVtable;
    message.ConstructCopy(&other->message);
    return this;
}

// FUNC_AT(0x001311d1)
void GameStd::RuntimeError::Destruct() {
    vtable = RuntimeErrorVtable;
    message.Tidy(true);
    Exception::Destruct();
}

// FUNC_AT(0x001311fa)
RuntimeError* GameStd::RuntimeError::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x001311ec)
const char* GameStd::RuntimeError::What() {
    return message.CStr();
}

// FUNC_AT(0x00131382)
Failure* GameStd::Failure::ConstructCopy(const Failure *other) {
    RuntimeError::ConstructCopy(other);
    vtable = FailureVtable;
    return this;
}

// FUNC_AT(0x0013126e)
void GameStd::Failure::Destruct() {
    vtable = FailureVtable;
    RuntimeError::Destruct();
}

// FUNC_AT(0x00131252)
Failure* GameStd::Failure::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// throw *this
// FUNC_AT(0x0013139a)
void GameStd::Failure::Raise() {
    Failure copy;
    copy.ConstructCopy(this);
    Crt::CxxThrowException(&copy, &Crt::FailureThrowInfo);
}

// FUNC_AT(0x00132722)
void GameStd::TypeInfo::Destruct() {
    vtable = TypeInfoVtable;
    Crt::_lock(kTypeInfoLock);
    if (undecoratedName != NULL)
        Crt::free(undecoratedName);
    Crt::_unlock(kTypeInfoLock);
}

// FUNC_AT(0x0013274b)
TypeInfo* GameStd::TypeInfo::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00131c2c)
bool GameStd::UncaughtException() {
    return Crt::PerThread()->processingThrow != 0;
}

// FUNC_AT(0x00130b80)
void GameStd::String::Xran() {
    String message;
    message.ConstructText("invalid string position");
    LogicError error;
    error.Construct(&message);
    error.vtable = OutOfRangeVtable;
    Crt::CxxThrowException(&error, &Crt::OutOfRangeThrowInfo);
}

// FUNC_AT(0x00130bc0)
void GameStd::String::Xlen() {
    String message;
    message.ConstructText("string too long");
    LogicError error;
    error.Construct(&message);
    error.vtable = LengthErrorVtable;
    Crt::CxxThrowException(&error, &Crt::LengthErrorThrowInfo);
}

// ---------------------------------------------------------------------------------------------------------------
// The throw information of the types thrown: what the frame handlers match a catch against and how they copy and
// destroy the object (the compiler's records, typed).

// type_info's vtable
// XBE_GLOBAL(0x001a3290, 4) readonly
void *TypeInfoVtable[1] = {
    (void *)XbeAddress(&TypeInfo::Delete),
};

// XBE_GLOBAL(0x001b4950, 0x18)
static TypeDescriptor<16> ExceptionType = { { TypeInfoVtable, NULL }, ".?AVexception@@" };
// XBE_GLOBAL(0x001b4968, 0x1e)
static TypeDescriptor<22> LogicErrorType = { { TypeInfoVtable, NULL }, ".?AVlogic_error@std@@" };
// XBE_GLOBAL(0x001b4988, 0x1f)
static TypeDescriptor<23> LengthErrorType = { { TypeInfoVtable, NULL }, ".?AVlength_error@std@@" };
// XBE_GLOBAL(0x001b4a70, 0x1f)
static TypeDescriptor<23> OutOfRangeType = { { TypeInfoVtable, NULL }, ".?AVout_of_range@std@@" };
// XBE_GLOBAL(0x001b6640, 0x17)
static TypeDescriptor<15> BadCastType = { { TypeInfoVtable, NULL }, ".?AVbad_cast@@" };
// XBE_GLOBAL(0x001d8728, 0x20)
static TypeDescriptor<24> RuntimeErrorType = { { TypeInfoVtable, NULL }, ".?AVruntime_error@std@@" };
// XBE_GLOBAL(0x001d8748, 0x23)
static TypeDescriptor<27> FailureType = { { TypeInfoVtable, NULL }, ".?AVfailure@ios_base@std@@" };

// XBE_GLOBAL(0x001a8958, 0x1c) readonly
static const Crt::CatchableType ExceptionCatch = { 0, &ExceptionType, 0, -1, 0, 0xc, (void *)XbeAddress(&Exception::ConstructCopy) };
// XBE_GLOBAL(0x001a8974, 0x1c) readonly
static const Crt::CatchableType LogicErrorCatch = { 0, &LogicErrorType, 0, -1, 0, 0x28, (void *)XbeAddress(&LogicError::ConstructCopy) };
// XBE_GLOBAL(0x001a8990, 0x1c) readonly
static const Crt::CatchableType LengthErrorCatch = { 0, &LengthErrorType, 0, -1, 0, 0x28, (void *)XbeAddress(&LengthError::ConstructCopy) };
// XBE_GLOBAL(0x001a8bac, 0x1c) readonly
static const Crt::CatchableType OutOfRangeCatch = { 0, &OutOfRangeType, 0, -1, 0, 0x28, (void *)XbeAddress(&OutOfRangeError::ConstructCopy) };
// XBE_GLOBAL(0x001a9504, 0x1c) readonly
static const Crt::CatchableType BadCastCatch = { 0, &BadCastType, 0, -1, 0, 0xc, (void *)XbeAddress(&BadCast::ConstructCopy) };
// XBE_GLOBAL(0x001b3bc0, 0x1c) readonly
static const Crt::CatchableType RuntimeErrorCatch = { 0, &RuntimeErrorType, 0, -1, 0, 0x28, (void *)XbeAddress(&RuntimeError::ConstructCopy) };
// XBE_GLOBAL(0x001b3bdc, 0x1c) readonly
static const Crt::CatchableType FailureCatch = { 0, &FailureType, 0, -1, 0, 0x28, (void *)XbeAddress(&Failure::ConstructCopy) };

// XBE_GLOBAL(0x001a89ac, 0x10) readonly
static const Crt::CatchableTypeArray<3> LengthErrorCatches = { 3, { &LengthErrorCatch, &LogicErrorCatch, &ExceptionCatch } };
// XBE_GLOBAL(0x001a8bc8, 0x10) readonly
static const Crt::CatchableTypeArray<3> OutOfRangeCatches = { 3, { &OutOfRangeCatch, &LogicErrorCatch, &ExceptionCatch } };
// XBE_GLOBAL(0x001a9520, 0xc) readonly
static const Crt::CatchableTypeArray<2> BadCastCatches = { 2, { &BadCastCatch, &ExceptionCatch } };
// XBE_GLOBAL(0x001b3bf8, 0x10) readonly
static const Crt::CatchableTypeArray<3> FailureCatches = { 3, { &FailureCatch, &RuntimeErrorCatch, &ExceptionCatch } };

namespace Crt {
// XBE_GLOBAL(0x001a89bc, 0x10) readonly
extern const ThrowInfo LengthErrorThrowInfo = { 0, (void *)XbeAddress(&LengthError::Destruct), NULL, &LengthErrorCatches };
// XBE_GLOBAL(0x001a8bd8, 0x10) readonly
extern const ThrowInfo OutOfRangeThrowInfo = { 0, (void *)XbeAddress(&OutOfRangeError::Destruct), NULL, &OutOfRangeCatches };
// XBE_GLOBAL(0x001a952c, 0x10) readonly
extern const ThrowInfo BadCastThrowInfo = { 0, (void *)XbeAddress(&BadCast::Destruct), NULL, &BadCastCatches };
// XBE_GLOBAL(0x001b3c08, 0x10) readonly
extern const ThrowInfo FailureThrowInfo = { 0, (void *)XbeAddress(&Failure::Destruct), NULL, &FailureCatches };
} // namespace Crt

// XAPI's RaiseException, which the original calls, is Win32's: the same record, raised by the kernel's
// RtlRaiseException.
// FUNC_AT(0x001325ad)
void __stdcall Crt::CxxThrowException(void *object, const ThrowInfo *info) {
    ULONG_PTR parameters[3] = { kCxxMagic, ULONG_PTR(object), ULONG_PTR(info) };
    HostRaiseException(kCxxExceptionCode, EXCEPTION_NONCONTINUABLE, 3, parameters);
}

// The original destroys the elements made so far if a constructor throws; none here does.
// FUNC_AT(0x0013326e)
void __stdcall Crt::EhVectorConstructor(void *array, size_t size, int count, ElementFn constructor, ElementFn) {
    char *element = (char *)array;
    for (int i = 0; i < count; i++) {
        constructor(element);
        element += size;
    }
}

// FUNC_AT(0x0013332e)
void __stdcall Crt::EhVectorDestructor(void *array, size_t size, int count, ElementFn destructor) {
    char *element = (char *)array + size * count;
    while (--count >= 0) {
        element -= size;
        destructor(element);
    }
}
