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

#include <windows.h>
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
#define OstreamVbtable ((const int *)0x001a322cu)
#define OStrStreamIosVtable ((const void *const *)Vtable_001a3234)   // basic_ios inside an ostrstream
#define OStrStreamVbtable ((const int *)0x001a3238u)
#define LocimpVtable ((const void *const *)Vtable_001a31c0)
#define ExceptionVtable ((const void *)Vtable_001a326c)
#define BadCastVtable ((const void *)Vtable_001a3288)
#define RuntimeErrorVtable ((const void *)Vtable_001a31cc)
#define FailureVtable ((const void *)Vtable_001a31d4)                // ios_base::failure
#define OutOfRangeVtable ((const void *)Vtable_0018a29c)
#define LengthErrorVtable ((const void *)Vtable_00189eec)
#define FailureThrowInfo ((const Crt::ThrowInfo *)0x001b3c08u)
#define OutOfRangeThrowInfo ((const Crt::ThrowInfo *)0x001a8bd8u)
#define LengthErrorThrowInfo ((const Crt::ThrowInfo *)0x001a89bcu)

// ---- the library's globals

// [4], one per _Lockit kind. Left at the game's address: the shadow test's originals take the same kernel locks.
#define LibraryLocks ((CriticalSection *)0x002447b8u)

namespace GameStd {
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
    Crt::CxxThrowException(&failure, FailureThrowInfo);
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

// FUNC_AT(0x001326ae)
BadCast* GameStd::BadCast::Construct(const char *message) {
    ConstructMessage(message);
    vtable = BadCastVtable;
    return this;
}

RuntimeError* GameStd::RuntimeError::Construct(const String *text) {
    Exception::Construct();
    vtable = RuntimeErrorVtable;
    message.ConstructCopy(text);
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
    Crt::CxxThrowException(&error, OutOfRangeThrowInfo);
}

// FUNC_AT(0x00130bc0)
void GameStd::String::Xlen() {
    String message;
    message.ConstructText("string too long");
    LogicError error;
    error.Construct(&message);
    error.vtable = LengthErrorVtable;
    Crt::CxxThrowException(&error, LengthErrorThrowInfo);
}

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
