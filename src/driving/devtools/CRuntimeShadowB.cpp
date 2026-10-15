#include "CRuntimeShadowB.h"

#include "../platform/CRuntime.h"
#include "../platform/CRuntimeInternal.h"
#include "../platform/CRuntimeStd.h"
#include "../data/StdStreams.h"
#include "../engine/UMemory.hpp"
#include "OwnedGlobals.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_CRUNTIMESHADOWB=1: src/driving/platform/CRuntimeStd.cpp against the game's runtime. Each case runs twice
// on the same lent objects and runtime state - once through the original (its entry swapped back in), once through
// ours - and compares a description of everything the call wrote: the object's fields (pointers into the lent
// objects as offsets, heap blocks by their contents), the library's globals (the global locale, the facet list, the
// exit table, the standard-stream tables, the lock count), the kernel critical-section calls made on this thread
// (which lock; not the allocator's), and what was called back (facets deleted, ios callbacks, array elements, exit
// functions). Throws are caught (MSVC builds) and compared by the exception record, the thrown object's vtable and
// message; the originals' throws reach the loader's RtlRaiseException.
//
// Parts: locks (_Lockit, _Mutex, _Init_locks, _Init_atexit), locale (_Getfacet, locale(), the first locale::_Init,
// _Register and the exit functions both register), _Locinfo, ctype (_Getctype, _Getcvt, _Tolower, _Toupper),
// ios_base (_Init, clear with and without throws, _Addstd, ~ios_base), streams (strstreambuf::_Init and its
// destructor, ostrstream's constructor and destructor, istrstream's destructor), _Stod/_Stof (random numerals with
// powers of ten), exceptions (std::exception, bad_cast, _CxxThrowException, _Xran, _Xlen, uncaught_exception, the
// eh vector iterators), and the virtual functions nothing in game reaches (strstreambuf's overflow, pbackfail,
// underflow, seekoff and seekpos; the exceptions' deleting destructors, copies, what() and failure's _Doraise;
// type_info's and _Locimp's deleting destructors; basic_ostream's and ostrstream's, entered at the basic_ios).
//
// One summary line: "[crtshadowb] ...: N cases, M checks, D differ", first differences detailed
// (NIGHTFIRE_CRUNTIMESHADOWB=2: a line per part as well).
// ---------------------------------------------------------------------------------------------------------------

using namespace GameStd;

extern void *TypeInfoVtable[1];   // CRuntimeStd.cpp: type_info's vtable

namespace {

// ---- the originals

#define OrigInitLocksConstruct ((InitLocks *(__fastcall *)(InitLocks *, int))0x00130c99)
#define OrigInitLocksDestruct ((void (__fastcall *)(InitLocks *, int))0x00130cc7)
#define OrigLockitConstruct ((Lockit *(__fastcall *)(Lockit *, int, int))0x00130cef)
#define OrigLockitDestruct ((void (__fastcall *)(Lockit *, int))0x00130d10)
#define OrigLocaleGetFacet ((const Facet *(__fastcall *)(const Locale *, int, uint32_t))0x00130d9b)
#define OrigFacetRegister ((void (__fastcall *)(Facet *, int))0x00130e13)
#define OrigLocinfoDestruct ((void (__fastcall *)(Locinfo *, int))0x00130ed2)
#define OrigLocinfoConstruct ((Locinfo *(__fastcall *)(Locinfo *, int, const char *))0x00130f8f)
#define OrigLocaleConstruct ((Locale *(__fastcall *)(Locale *, int))0x001310bf)
#define OrigIosAddStandard ((void (__fastcall *)(IosBase *, int))0x001310fa)
#define OrigIosDestruct ((void (__fastcall *)(IosBase *, int))0x00131191)
#define OrigIosClear ((void (__fastcall *)(IosBase *, int, int, bool))0x00131279)
#define OrigIosInit ((void (__fastcall *)(IosBase *, int))0x001313b8)
#define OrigMutexConstruct ((Mutex *(__fastcall *)(Mutex *, int))0x001313fe)
#define OrigMutexDestruct ((void (__fastcall *)(Mutex *, int))0x00131416)
#define OrigMutexLock ((void (__fastcall *)(Mutex *, int))0x0013142b)
#define OrigMutexUnlock ((void (__fastcall *)(Mutex *, int))0x00131434)
#define OrigStrInit ((void (__fastcall *)(StrStreambuf *, int, int, char *, char *, int))0x00131901)
#define OrigStrDestruct ((void (__fastcall *)(StrStreambuf *, int))0x00131a44)
#define OrigIStrDestruct ((void (__fastcall *)(BasicIos *, int))0x00131ae1)
#define OrigOStrConstruct ((OStrStream *(__fastcall *)(OStrStream *, int, char *, int, int, int))0x00131b27)
#define OrigOStrDestruct ((void (__fastcall *)(BasicIos *, int))0x00131bbb)
#define OrigUncaught ((bool (__cdecl *)())0x00131c2c)
#define OrigGetcvt ((uint64_t (__cdecl *)())0x00131c31)
#define OrigGetctype ((CtypeVec *(__cdecl *)(CtypeVec *))0x00130c3c)
#define OrigTolower ((int (__cdecl *)(int, const CtypeVec *))0x00130c00)
#define OrigToupper ((int (__cdecl *)(int, const CtypeVec *))0x0013143d)
#define OrigStod ((double (__cdecl *)(const char *, char **, long))0x00131c45)
#define OrigStof ((double (__cdecl *)(const char *, char **, long))0x00131c79)
#define OrigAtExitRun ((void (__fastcall *)(InitAtExit *, int))0x00131cd0)
#define OrigThrow ((void (__stdcall *)(void *, const Crt::ThrowInfo *))0x001325ad)
#define OrigExceptionConstruct ((Exception *(__fastcall *)(Exception *, int))0x001325e6)
#define OrigExceptionCopy ((Exception *(__fastcall *)(Exception *, int, const Exception *))0x0013263c)
#define OrigExceptionDestruct ((void (__fastcall *)(Exception *, int))0x0013268b)
#define OrigBadCast ((BadCast *(__fastcall *)(BadCast *, int, const char *))0x001326ae)
#define OrigVectorConstructor ((void (__stdcall *)(void *, size_t, int, Crt::ElementFn, Crt::ElementFn))0x0013326e)
#define OrigVectorDestructor ((void (__stdcall *)(void *, size_t, int, Crt::ElementFn))0x0013332e)
#define OrigXran ((void (__cdecl *)())0x00130b80)
#define OrigXlen ((void (__cdecl *)())0x00130bc0)
#define OrigStrOverflow ((int (__fastcall *)(StrStreambuf *, int, int))0x001314ae)
#define OrigStrPbackfail ((int (__fastcall *)(StrStreambuf *, int, int))0x0013166a)
#define OrigStrUnderflow ((int (__fastcall *)(StrStreambuf *, int))0x001316b3)
#define OrigStrSeekOff ((StreamPos *(__fastcall *)(StrStreambuf *, int, StreamPos *, int, int, int))0x00131702)
#define OrigStrSeekPos ((StreamPos *(__fastcall *)(StrStreambuf *, int, StreamPos *, StreamPos, int))0x0013183b)
#define OrigLocimpDelete ((void *(__fastcall *)(Locimp *, int, unsigned))0x00130f23)
#define OrigExceptionDelete ((void *(__fastcall *)(void *, int, unsigned))0x001326ea)
#define OrigExceptionWhat ((const char *(__fastcall *)(void *, int))0x001326a1)
#define OrigBadCastDelete ((void *(__fastcall *)(void *, int, unsigned))0x00132706)
#define OrigBadCastCopy ((void *(__fastcall *)(void *, int, const void *))0x001326c7)
#define OrigBadCastDestruct ((void (__fastcall *)(void *, int))0x001326df)
#define OrigRuntimeErrorDelete ((void *(__fastcall *)(void *, int, unsigned))0x001311fa)
#define OrigRuntimeErrorWhat ((const char *(__fastcall *)(void *, int))0x001311ec)
#define OrigRuntimeErrorCopy ((void *(__fastcall *)(void *, int, const void *))0x0013133f)
#define OrigRuntimeErrorDestruct ((void (__fastcall *)(void *, int))0x001311d1)
#define OrigFailureDelete ((void *(__fastcall *)(void *, int, unsigned))0x00131252)
#define OrigFailureCopy ((void *(__fastcall *)(void *, int, const void *))0x00131382)
#define OrigFailureDestruct ((void (__fastcall *)(void *, int))0x0013126e)
#define OrigFailureRaise ((void (__fastcall *)(void *, int))0x0013139a)
#define OrigTypeInfoDelete ((void *(__fastcall *)(TypeInfo *, int, unsigned))0x0013274b)
#define OrigDeleteOstream ((void *(__fastcall *)(BasicIos *, int, unsigned))0x001319e7)
#define OrigDeleteOStrStream ((void *(__fastcall *)(BasicIos *, int, unsigned))0x00131c01)

// Set-up and tidy-up by address: whatever runs there (ours in game), the same on both sides.
#define SetupStreambufConstruct ((Streambuf *(__fastcall *)(Streambuf *, int))0x000387f0)
#define SetupIStrStreamConstruct ((IStrStream *(__fastcall *)(IStrStream *, int, const char *, int))0x00038b70)
#define SetupBasicIosDestruct ((void (__fastcall *)(BasicIos *, int))0x00037ae0)

// ---- the runtime's state: the library's globals are ours (CRuntimeStd.h) and the originals use the game's copies,
//      which nothing lends (tools/owned_globals.py's SKIP): each name below is the copy of the side running

bool g_originalSide;

template <class T> T &Side(T &ours, uint32_t game) {
    return g_originalSide ? *reinterpret_cast<T *>(uintptr_t(game)) : ours;
}

#define LibraryLocks ((CriticalSection *)OwnedGlobals_Ours(0x002447b8u))    // ours, both sides (shared)
#define InitLocksCount Side(GameStd::InitLocksCount, 0x001d8720u)
#define FacetList Side(GameStd::FacetList, 0x0024482cu)
#define GlobalLocale Side(GameStd::GlobalLocale, 0x00244830u)
#define ClassicImpl Side(GameStd::ClassicImpl, 0x00244834u)
#define ClassicLocale Side(GameStd::ClassicLocale, 0x0024483cu)
#define GlobalTidyRegistered Side(GameStd::GlobalTidyRegistered, 0x00244840u)
#define StandardTable Side(GameStd::StandardStreams, 0x00244854u)
#define AtExitCount Side(GameStd::AtExitCount, 0x001d8770u)
#define AtExitFunctions (Side(GameStd::AtExitFunctions, 0x002448acu))

#define KernelInitializeSlot (*(CsFn *)0x00189cf0u)
#define KernelEnterSlot (*(CsFn *)0x00189c40u)
#define KernelLeaveSlot (*(CsFn *)0x00189c3cu)

#define IosBaseVtable ((const void *const *)0x0018bc70u)

typedef void (*AtExitFn)(void);
typedef void (__stdcall *CsFn)(CriticalSection *);

const char kTag[] = "[crtshadowb]";

int g_cases, g_checks, g_differ, g_faults, g_details;
bool g_trace;   // NIGHTFIRE_CRUNTIMESHADOWB=3: each case named before it runs

uint32_t g_seed = 0x13572468;
uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
uint32_t Below(uint32_t n) {
    return n ? Next() % n : 0;
}

void Check(bool same, const char *what, const char *format, ...) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_details++ >= 16)
        return;
    printf("%s %s differs: ", kTag, what);
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    printf("\n");
}

std::string Format(const char *format, ...) {
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    return text;
}

// ---- pointers described by what they point into

struct Region {
    const void *base;
    size_t size;
    const char *name;
};
std::vector<Region> g_regions;

void Lend(const void *base, size_t size, const char *name) {
    g_regions.push_back({ base, size, name });
}

std::string Where(const void *p) {
    if (p == NULL)
        return "0";
    for (const Region &region : g_regions) {
        uintptr_t at = uintptr_t(p), base = uintptr_t(region.base);
        if (at >= base && at <= base + region.size)
            return Format("%s+%x", region.name, unsigned(at - base));
    }
    // the image: vtables, globals, code; our copies of its vtables and our replacements as the game's
    const uint32_t game = OwnedGlobals_AsGame(uint32_t(uintptr_t(p)));
    if (game >= 0x00010000 && game < 0x00260000)
        return Format("@%08x", unsigned(game));
    return "heap";
}

std::string Text(const String *s) {
    const char *data = s->capacity < 16 ? s->text.buffer : s->text.pointer;
    std::string out = Format("[%u/%u]", s->size, s->capacity);
    if (data != NULL && s->size < 256)
        out.append(data, s->size);
    return out;
}

// ---- the kernel's critical sections, recorded on this thread: the library's four locks and sections set up during
//      the side (a _Mutex's). Others are the allocator's (its locked memory classes), whose use follows the heap's
//      state rather than the code under test.

struct CsCall {
    char kind;   // I(nitialise), E(nter), L(eave)
    int lock;    // the library lock, -1 another
};
std::vector<CsCall> g_cs;
std::vector<CriticalSection *> g_madeSections;
DWORD g_thread;
CsFn g_kernelInitialize, g_kernelEnter, g_kernelLeave;

int LockOf(CriticalSection *section) {
    for (int i = 0; i < 4; i++) {
        if (section == &LibraryLocks[i])
            return i;
    }
    return -1;
}

bool Recorded(CriticalSection *section) {
    if (GetCurrentThreadId() != g_thread)
        return false;
    if (LockOf(section) >= 0)
        return true;
    for (CriticalSection *made : g_madeSections) {
        if (made == section)
            return true;
    }
    return false;
}

void __stdcall RecordInitialize(CriticalSection *section) {
    if (GetCurrentThreadId() == g_thread) {
        g_cs.push_back({ 'I', LockOf(section) });
        if (LockOf(section) >= 0)
            return;   // the live locks are not set up again
        g_madeSections.push_back(section);
    }
    g_kernelInitialize(section);
}

void __stdcall RecordEnter(CriticalSection *section) {
    if (Recorded(section))
        g_cs.push_back({ 'E', LockOf(section) });
    g_kernelEnter(section);
}

void __stdcall RecordLeave(CriticalSection *section) {
    if (Recorded(section))
        g_cs.push_back({ 'L', LockOf(section) });
    g_kernelLeave(section);
}

std::string CsLog() {
    std::string out = " cs:";
    for (const CsCall &call : g_cs)
        out += Format("%c%d", call.kind, call.lock);
    return out;
}

// ---- callbacks the originals and ours make, recorded

std::vector<std::string> g_calls;

struct FakeFacet {
    const void *const *vtable;
    uint32_t refs;

    FakeFacet* Delete(unsigned flags) {
        g_calls.push_back(Format("delete %s %u", Where(this).c_str(), flags));
        return this;
    }
};
const void *g_fakeFacetVtable[1];

void InitFake(FakeFacet *facet, uint32_t refs) {
    g_fakeFacetVtable[0] = (const void *)XbeAddress(&FakeFacet::Delete);
    facet->vtable = g_fakeFacetVtable;
    facet->refs = refs;
}

struct Element {
    void Construct() { g_calls.push_back("c " + Where(this)); }
    void Destruct() { g_calls.push_back("d " + Where(this)); }
};

void RecordIosCallback(int event, IosBase &ios, int index) {
    g_calls.push_back(Format("callback %d %s %d", event, Where(&ios).c_str(), index));
}

void RecordFree(void *block) {
    g_calls.push_back("free " + Where(block));
}

template <int N> void RecordExit() {
    g_calls.push_back(Format("exit %d", N));
}
const AtExitFn kExitRecorders[10] = {
    RecordExit<0>, RecordExit<1>, RecordExit<2>, RecordExit<3>, RecordExit<4>,
    RecordExit<5>, RecordExit<6>, RecordExit<7>, RecordExit<8>, RecordExit<9>,
};

std::string Calls() {
    std::string out = " calls:";
    for (const std::string &call : g_calls)
        out += "{" + call + "}";
    return out;
}

// ---- the runtime's globals, kept and put back around every side of every case

struct RuntimeState {
    Locimp *global, *classic;
    Facet *classicLocale;
    bool registered;
    FacetNode *facets;
    uint32_t atExitCount;
    AtExitFn atExit[10];
    long initLocks;
    StandardStreamTable standard;
    int processingThrow;
};

void Save(RuntimeState *state) {
    state->global = GlobalLocale;
    state->classic = ClassicImpl;
    state->classicLocale = ClassicLocale.impl;
    state->registered = GlobalTidyRegistered;
    state->facets = FacetList;
    state->atExitCount = AtExitCount;
    memcpy(state->atExit, AtExitFunctions, sizeof(state->atExit));
    state->initLocks = InitLocksCount;
    state->standard = StandardTable;
    state->processingThrow = Crt::PerThread()->processingThrow;
}

void Restore(const RuntimeState *state) {
    GlobalLocale = state->global;
    ClassicImpl = state->classic;
    ClassicLocale.impl = state->classicLocale;
    GlobalTidyRegistered = state->registered;
    FacetList = state->facets;
    AtExitCount = state->atExitCount;
    memcpy(AtExitFunctions, state->atExit, sizeof(state->atExit));
    InitLocksCount = state->initLocks;
    StandardTable = state->standard;
    Crt::PerThread()->processingThrow = state->processingThrow;
}

std::string Globals() {
    std::string out = Format(" global %s classic %s/%s registered %d facets %s exits %u",
                             Where(GlobalLocale).c_str(), Where(ClassicImpl).c_str(),
                             Where(ClassicLocale.impl).c_str(), GlobalTidyRegistered, Where(FacetList).c_str(),
                             AtExitCount);
    for (uint32_t i = AtExitCount; i < 10; i++)
        out += AtExitFunctions[i] != NULL ? " fn" : " null";
    out += Format(" initlocks %ld std", long(InitLocksCount));
    for (int i = 0; i < 9; i++)
        out += " " + Where(StandardTable.streams[i]);
    for (int i = 0; i < 9; i++)
        out += Format(" %d", StandardTable.opens[i]);
    return out;
}

// ---- running a case on both sides

typedef std::string (*CaseFn)(bool original, void *context);

struct Run {
    CaseFn fn;
    void *context;
    bool original;
    std::string out;
};

#ifdef _MSC_VER
// A C++ exception's record and object, read while the thrower's frame is still there.
struct Throw {
    bool caught;
    DWORD code;
    DWORD flags;
    DWORD parameterCount;
    ULONG_PTR magic;
    const void *object;
    const void *info;
    const void *vtable;
    std::string message;
};
Throw g_throw;

int CatchFilter(EXCEPTION_POINTERS *pointers) {
    const EXCEPTION_RECORD *record = pointers->ExceptionRecord;
    if (record->ExceptionCode != 0xe06d7363)
        return EXCEPTION_CONTINUE_SEARCH;
    g_throw.caught = true;
    g_throw.code = record->ExceptionCode;
    g_throw.flags = record->ExceptionFlags & EXCEPTION_NONCONTINUABLE;
    g_throw.parameterCount = record->NumberParameters;
    g_throw.magic = record->ExceptionInformation[0];
    g_throw.object = (const void *)record->ExceptionInformation[1];
    g_throw.info = (const void *)record->ExceptionInformation[2];
    g_throw.vtable = NULL;
    g_throw.message.clear();
    if (g_throw.object != NULL) {
        g_throw.vtable = *(const void *const *)g_throw.object;
        const uint32_t vtable = OwnedGlobals_AsGame(uint32_t(uintptr_t(g_throw.vtable)));
        // runtime_error, ios_base::failure, out_of_range, length_error: the message string after the exception
        if (vtable == 0x001a31d4 || vtable == 0x001a31cc || vtable == 0x0018a29c || vtable == 0x00189eec)
            g_throw.message = Text((const String *)((const char *)g_throw.object + 0xc));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void CallCase(Run *run) {
    run->out = run->fn(run->original, run->context);
}

void DescribeThrow(Run *run) {
    run->out = Format("threw %08lx %lx %lu %08lx %s %s %s %s", g_throw.code, g_throw.flags, g_throw.parameterCount,
                      (unsigned long)g_throw.magic, Where(g_throw.object).c_str(), Where(g_throw.info).c_str(),
                      Where(g_throw.vtable).c_str(), g_throw.message.c_str());
}

void DescribeFault(Run *run) {
    g_faults++;
    run->out = "fault";
}

void Execute(Run *run) {
    g_throw.caught = false;
    __try {
        __try {
            CallCase(run);
        } __except (CatchFilter(GetExceptionInformation())) {
            DescribeThrow(run);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DescribeFault(run);
    }
}
const bool kCanCatch = true;
#else
void Execute(Run *run) {
    run->out = run->fn(run->original, run->context);
}
const bool kCanCatch = false;
#endif

// Both sides from the same runtime state - ours, the live one, put in the game's copies for the original's side -
// each described with the globals, the lock calls and the callbacks.
void Compare(const char *what, CaseFn fn, void *context) {
    g_cases++;
    if (g_trace)
        printf("%s case %d: %s\n", kTag, g_cases, what);
    RuntimeState saved, savedGame;
    g_originalSide = true;
    Save(&savedGame);
    g_originalSide = false;
    Save(&saved);
    Run runs[2] = { { fn, context, true, "" }, { fn, context, false, "" } };
    for (Run &run : runs) {
        g_originalSide = run.original;
        Restore(&saved);
        g_cs.clear();
        g_madeSections.clear();
        g_calls.clear();
        Execute(&run);
        run.out += Globals() + CsLog() + Calls();
        Restore(&saved);
    }
    g_originalSide = true;
    Restore(&savedGame);
    g_originalSide = false;
    Check(runs[0].out == runs[1].out, what, "\n  original %s\n  port     %s", runs[0].out.c_str(),
          runs[1].out.c_str());
}

// ---------------------------------------------------------------------------------------------------------------
// Locks

struct LockCase {
    int kind;
    long count;
};

std::string LockitCase(bool original, void *context) {
    const LockCase *c = (const LockCase *)context;
    Lockit lock;
    lock.kind = 0x55555555;
    Lockit *returned;
    if (original) {
        XbeOriginalScope a(0x00130cef), b(0x00130d10);
        returned = OrigLockitConstruct(&lock, 0, c->kind);
        std::string out = Format("kind %d ret %d", lock.kind, returned == &lock);
        OrigLockitDestruct(&lock, 0);
        return out;
    }
    returned = lock.Construct(c->kind);
    std::string out = Format("kind %d ret %d", lock.kind, returned == &lock);
    lock.Destruct();
    return out;
}

std::string MutexCase(bool original, void *) {
    Mutex mutex;
    mutex.section = (CriticalSection *)uintptr_t(0x55555555);
    std::string out;
    if (original) {
        XbeOriginalScope a(0x001313fe), b(0x00131416), c(0x0013142b), d(0x00131434);
        Mutex *returned = OrigMutexConstruct(&mutex, 0);
        out = Format("ret %d section %s", returned == &mutex, mutex.section != NULL ? "set" : "null");
        OrigMutexLock(&mutex, 0);
        OrigMutexUnlock(&mutex, 0);
        OrigMutexDestruct(&mutex, 0);
    } else {
        Mutex *returned = mutex.Construct();
        out = Format("ret %d section %s", returned == &mutex, mutex.section != NULL ? "set" : "null");
        mutex.Lock();
        mutex.Unlock();
        mutex.Destruct();
    }
    return out;
}

std::string InitLocksCase(bool original, void *context) {
    const LockCase *c = (const LockCase *)context;
    InitLocks object;
    InitLocksCount = c->count;
    std::string out;
    if (original) {
        XbeOriginalScope a(0x00130c99), b(0x00130cc7);
        InitLocks *returned = OrigInitLocksConstruct(&object, 0);
        out = Format("ret %d count %ld", returned == &object, long(InitLocksCount));
        OrigInitLocksDestruct(&object, 0);
    } else {
        InitLocks *returned = object.Construct();
        out = Format("ret %d count %ld", returned == &object, long(InitLocksCount));
        object.Destruct();
    }
    return out + Format(" then %ld", long(InitLocksCount));
}

std::string AtExitCase(bool original, void *context) {
    const LockCase *c = (const LockCase *)context;
    for (int i = 0; i < 10; i++)
        AtExitFunctions[i] = kExitRecorders[i];
    AtExitCount = uint32_t(c->count);
    InitAtExit object;
    if (original) {
        XbeOriginalScope a(0x00131cd0);
        OrigAtExitRun(&object, 0);
    } else {
        object.Destruct();
    }
    return Format("count %u", AtExitCount);
}

void RunLocks() {
    for (int kind = -3; kind <= 9; kind++) {
        LockCase c = { kind, 0 };
        Compare("_Lockit", LockitCase, &c);
    }
    Compare("_Mutex", MutexCase, NULL);
    static const long kCounts[] = { -1, 0, 1, 3, -2, 0x7fffffff };
    for (long count : kCounts) {
        LockCase c = { 0, count };
        Compare("_Init_locks", InitLocksCase, &c);
    }
    static const long kExits[] = { 10, 9, 8, 5, 1, 0, 11, 0x7fffffff };
    for (long count : kExits) {
        LockCase c = { 0, count };
        Compare("_Init_atexit", AtExitCase, &c);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Locale

struct FacetCase {
    Locimp own, global;
    Facet *ownFacets[8], *globalFacets[8];
    uint32_t id;
};

std::string GetFacetCase(bool original, void *context) {
    FacetCase *c = (FacetCase *)context;
    GlobalLocale = &c->global;
    Locale locale = { &c->own };
    const Facet *facet;
    if (original) {
        XbeOriginalScope a(0x00130d9b);
        facet = OrigLocaleGetFacet(&locale, 0, c->id);
    } else {
        facet = locale.GetFacet(c->id);
    }
    return Format("facet %08x", unsigned(uintptr_t(facet)));
}

void InitLocimp(Locimp *impl, Facet **facets, uint32_t count, bool transparent, uint32_t refs) {
    memset(impl, 0, sizeof(*impl));
    impl->vtable = (const void *const *)uintptr_t(0x001a31c0);
    impl->refs = refs;
    impl->facets = facets;
    impl->facetCount = count;
    impl->transparent = transparent;
    impl->name.capacity = 15;
}

struct LocaleCase {
    Locimp global;
    uint32_t refs;
    bool none;   // no global locale yet: the first locale::_Init
};

std::string DescribeLocimp(const Locimp *impl) {
    if (impl == NULL)
        return "null";
    return Format("vt %s refs %u facets %s count %u cat %x transparent %d name %s", Where(impl->vtable).c_str(),
                  impl->refs, Where(impl->facets).c_str(), impl->facetCount, impl->categories, impl->transparent,
                  Text(&impl->name).c_str());
}

std::string LocaleConstructCase(bool original, void *context) {
    LocaleCase *c = (LocaleCase *)context;
    if (c->none) {
        GlobalLocale = NULL;
        ClassicImpl = NULL;
        ClassicLocale.impl = NULL;
        GlobalTidyRegistered = false;
        AtExitCount = 10;
        memset(AtExitFunctions, 0, sizeof(AtExitFn) * 10);
    } else {
        InitLocimp(&c->global, NULL, 0, false, c->refs);
        GlobalLocale = &c->global;
    }
    Locale locale = { NULL };
    Locale *returned;
    if (original) {
        XbeOriginalScope a(0x001310bf);
        returned = OrigLocaleConstruct(&locale, 0);
    } else {
        returned = locale.Construct();
    }
    std::string out = Format("ret %d impl %s", returned == &locale, locale.impl == GlobalLocale ? "global" :
                             Where(locale.impl).c_str());
    out += " " + DescribeLocimp(GlobalLocale);
    if (c->none) {
        out += Format(" classic==global %d %d", ClassicImpl == GlobalLocale, ClassicLocale.impl == GlobalLocale);
        // the exit function it registered: one reference dropped, then (from one) the locale deleted
        Locimp *made = GlobalLocale;
        if (AtExitCount == 9 && AtExitFunctions[9] != NULL) {
            AtExitFunctions[9]();
            out += Format(" tidy: refs %u global %s;", made->refs, made == GlobalLocale ? "kept" : "changed");
            made->refs = 1;
            AtExitFunctions[9]();
            out += Format(" tidy: global %s", made == GlobalLocale ? "kept" : "changed");
        }
    }
    return out;
}

struct RegisterCase {
    FakeFacet facet, second, third;
    FacetNode lentNode;   // a node already on the list
    bool listed;
    uint32_t count;
    uint32_t refs, secondRefs;
};

std::string RegisterCaseFn(bool original, void *context) {
    RegisterCase *c = (RegisterCase *)context;
    InitFake(&c->facet, c->refs);
    InitFake(&c->second, c->secondRefs);
    FacetList = NULL;
    if (c->listed) {
        c->lentNode.next = NULL;
        c->lentNode.facet = (Facet *)&c->second;
        // the list's own nodes are heap blocks the tidy-up deletes
        FacetNode *node = (FacetNode *)OperatorNew(sizeof(FacetNode));
        *node = c->lentNode;
        FacetList = node;
    }
    AtExitCount = c->count;
    for (int i = 0; i < 10; i++)
        AtExitFunctions[i] = kExitRecorders[i];
    if (original) {
        XbeOriginalScope a(0x00130e13);
        OrigFacetRegister((Facet *)&c->facet, 0);
    } else {
        ((Facet *)&c->facet)->Register();
    }
    FacetNode *head = FacetList;
    std::string out = Format("head %s", head != NULL ? "node" : "null");
    if (head != NULL)
        out += Format(" next %s facet %s", head->next == NULL ? "null" : "listed", Where(head->facet).c_str());
    out += Format(" exits %u", AtExitCount);
    // the list released as at exit, by the function registered with the first facet (or the original's)
    if (c->count > 0 && !c->listed && AtExitCount == c->count - 1) {
        AtExitFn tidy = AtExitFunctions[AtExitCount];
        AtExitFunctions[AtExitCount] = kExitRecorders[AtExitCount];
        tidy();
        out += Format(" tidied: head %s refs %u", FacetList == NULL ? "null" : "left", c->facet.refs);
    }
    return out;
}

void RunLocale() {
    static Facet *const kFacets[8] = {
        (Facet *)uintptr_t(0x11110000), NULL, (Facet *)uintptr_t(0x11110002), NULL,
        (Facet *)uintptr_t(0x11110004), (Facet *)uintptr_t(0x11110005), NULL, (Facet *)uintptr_t(0x11110007),
    };
    static Facet *const kGlobalFacets[8] = {
        (Facet *)uintptr_t(0x22220000), (Facet *)uintptr_t(0x22220001), NULL, (Facet *)uintptr_t(0x22220003),
        NULL, (Facet *)uintptr_t(0x22220005), (Facet *)uintptr_t(0x22220006), NULL,
    };
    FacetCase *facetCase = new FacetCase;
    memcpy(facetCase->ownFacets, kFacets, sizeof(kFacets));
    memcpy(facetCase->globalFacets, kGlobalFacets, sizeof(kGlobalFacets));
    for (uint32_t ownCount = 0; ownCount <= 8; ownCount += 2) {
        for (uint32_t globalCount = 0; globalCount <= 8; globalCount += 3) {
            for (int transparent = 0; transparent < 2; transparent++) {
                for (uint32_t id = 0; id < 10; id++) {
                    InitLocimp(&facetCase->own, facetCase->ownFacets, ownCount, transparent != 0, 1);
                    InitLocimp(&facetCase->global, facetCase->globalFacets, globalCount, false, 1);
                    facetCase->id = id;
                    Compare("locale::_Getfacet", GetFacetCase, facetCase);
                }
            }
        }
    }
    delete facetCase;

    LocaleCase *localeCase = new LocaleCase;
    Lend(&localeCase->global, sizeof(Locimp), "lentglobal");
    static const uint32_t kRefs[] = { 0, 1, 5, 0xfffffffe, 0xffffffff };
    for (uint32_t refs : kRefs) {
        localeCase->refs = refs;
        localeCase->none = false;
        Compare("locale::locale", LocaleConstructCase, localeCase);
    }
    localeCase->none = true;
    Compare("locale::locale (the first)", LocaleConstructCase, localeCase);
    g_regions.pop_back();
    delete localeCase;

    RegisterCase *registerCase = new RegisterCase;
    Lend(&registerCase->facet, sizeof(FakeFacet), "facet");
    Lend(&registerCase->second, sizeof(FakeFacet), "second");
    static const uint32_t kFacetRefs[] = { 0, 1, 2, 0xffffffff };
    for (int listed = 0; listed < 2; listed++) {
        for (uint32_t count : { 10u, 3u, 1u }) {
            for (uint32_t refs : kFacetRefs) {
                registerCase->listed = listed != 0;
                registerCase->count = count;
                registerCase->refs = refs;
                registerCase->secondRefs = 1;
                Compare("locale::facet::_Register", RegisterCaseFn, registerCase);
            }
        }
    }
    g_regions.pop_back();
    g_regions.pop_back();
    delete registerCase;
}

// ---------------------------------------------------------------------------------------------------------------
// _Locinfo

std::string DescribeLocinfo(const Locinfo *info) {
    return Format("kind %d days %s months %s old %s new %s", info->lock.kind, Text(&info->days).c_str(),
                  Text(&info->months).c_str(), Text(&info->oldName).c_str(), Text(&info->newName).c_str());
}

std::string LocinfoCase(bool original, void *context) {
    const char *name = (const char *)context;
    Locinfo info;
    memset(&info, 0xcd, sizeof(info));
    std::string out;
    if (original) {
        XbeOriginalScope a(0x00130f8f), b(0x00130ed2);
        Locinfo *returned = OrigLocinfoConstruct(&info, 0, name);
        out = Format("ret %d ", returned == &info) + DescribeLocinfo(&info);
        OrigLocinfoDestruct(&info, 0);
    } else {
        Locinfo *returned = info.Construct(name);
        out = Format("ret %d ", returned == &info) + DescribeLocinfo(&info);
        info.Destruct();
    }
    return out + "; after " + DescribeLocinfo(&info);
}

void RunLocinfo() {
    static const char *const kNames[] = {
        NULL, "", "C", "*", "c", "Cx", "C ", "English", "french_france.1252", "a name longer than the small buffer",
    };
    for (const char *name : kNames)
        Compare("_Locinfo", LocinfoCase, (void *)name);
}

// ---------------------------------------------------------------------------------------------------------------
// ctype

std::string GetctypeCase(bool original, void *) {
    CtypeVec ctype;
    if (original) {
        XbeOriginalScope a(0x00130c3c);
        CtypeVec result;
        ctype = *OrigGetctype(&result);
    } else {
        ctype = Getctype();
    }
    const short *classes = (const short *)Crt::CtypeTable;
    std::string out = Format("handle %x page %x delete %d copy %d same %d", ctype.handle, ctype.page,
                             ctype.deleteTable, ctype.table != classes,
                             ctype.table != NULL && memcmp(ctype.table, classes, 512) == 0);
    if (ctype.deleteTable > 0)
        Crt::free((void *)ctype.table);
    return out;
}

std::string GetcvtCase(bool original, void *) {
    CvtVec cvt;
    if (original) {
        XbeOriginalScope a(0x00131c31);
        uint64_t both = OrigGetcvt();
        cvt.handle = uint32_t(both);
        cvt.page = uint32_t(both >> 32);
    } else {
        cvt = Getcvt();
    }
    return Format("handle %x page %x", cvt.handle, cvt.page);
}

std::string CaseCase(bool original, void *) {
    CtypeVec ctype = { 0, 0, NULL, 0 };
    std::string out;
    for (int c = -128; c < 384; c++) {
        int lower, upper;
        if (original) {
            XbeOriginalScope a(0x00130c00), b(0x0013143d);
            lower = OrigTolower(c, &ctype);
            upper = OrigToupper(c, (c & 1) ? &ctype : NULL);
        } else {
            lower = Tolower(c, &ctype);
            upper = Toupper(c, (c & 1) ? &ctype : NULL);
        }
        out += Format("%d,%d ", lower, upper);
    }
    return out;
}

void RunCtype() {
    Compare("_Getctype", GetctypeCase, NULL);
    Compare("_Getcvt", GetcvtCase, NULL);
    Compare("_Tolower, _Toupper", CaseCase, NULL);
}

// ---------------------------------------------------------------------------------------------------------------
// ios_base

struct IosCase {
    BasicIos ios;
    Locimp global;
    FakeFacet localeImpl;
    uint32_t fill;
    int state, exceptions;
    bool reraise;
    uint32_t standard;          // _Stdstr
    int8_t opens;               // _Stdopens[_Stdstr]
    int prefilled;              // _Stdstr's first slots taken by other streams
    int selfAt;                 // and this one already in the table there (-1 not)
    int words, callbacks;       // list lengths
    bool hasLocale;
    uint32_t localeRefs;
};

std::string DescribeIos(const IosBase *ios) {
    return Format("vt %s state %x except %x flags %x prec %d width %d words %s calls %s locale %s std %u",
                  Where(ios->vtable).c_str(), ios->state, ios->exceptions, ios->flags, ios->precision, ios->width,
                  Where(ios->words).c_str(), Where(ios->callbacks).c_str(),
                  ios->locale == NULL ? "null" : Where(ios->locale->impl).c_str(), ios->standardStream);
}

std::string IosInitCase(bool original, void *context) {
    IosCase *c = (IosCase *)context;
    memset(&c->ios, int(c->fill), sizeof(c->ios));
    InitLocimp(&c->global, NULL, 0, false, c->localeRefs);
    GlobalLocale = &c->global;
    if (original) {
        XbeOriginalScope a(0x001313b8), b(0x001310bf);   // and its locale(), which reads the game's global
        OrigIosInit(&c->ios, 0);
    } else {
        c->ios.IosBase::Init();
    }
    std::string out = DescribeIos(&c->ios) + Format(" global refs %u", c->global.refs);
    if (c->ios.locale != NULL)
        OperatorDelete(c->ios.locale);
    return out;
}

std::string IosClearCase(bool original, void *context) {
    IosCase *c = (IosCase *)context;
    memset(&c->ios, 0, sizeof(c->ios));
    c->ios.state = 0x55;
    c->ios.exceptions = c->exceptions;
    if (original) {
        XbeOriginalScope a(0x00131279);
        OrigIosClear(&c->ios, 0, c->state, c->reraise);
    } else {
        c->ios.Clear(c->state, c->reraise);
    }
    return Format("state %x", c->ios.state);
}

std::string IosAddStandardCase(bool original, void *context) {
    IosCase *c = (IosCase *)context;
    StandardTable = StandardStreamTable();
    for (int i = 0; i < c->prefilled && i < 8; i++) {
        StandardTable.streams[i] = (IosBase *)uintptr_t(0x33330000 + i);
        StandardTable.opens[i] = int8_t(i + 1);
    }
    if (c->selfAt >= 0)
        StandardTable.streams[c->selfAt] = &c->ios;
    memset(&c->ios, 0, sizeof(c->ios));
    c->ios.standardStream = c->standard;
    if (original) {
        XbeOriginalScope a(0x001310fa);
        OrigIosAddStandard(&c->ios, 0);
    } else {
        c->ios.AddStandard();
    }
    return Format("std %u", c->ios.standardStream);
}

std::string IosDestructCase(bool original, void *context) {
    IosCase *c = (IosCase *)context;
    memset(&c->ios, 0, sizeof(c->ios));
    c->ios.vtable = (const void *const *)uintptr_t(0x0018bcc8);
    c->ios.standardStream = c->standard;
    if (c->standard < 9)
        StandardTable.opens[c->standard] = c->opens;
    IosBase::Word **wordAt = &c->ios.words;
    for (int i = 0; i < c->words; i++) {
        IosBase::Word *word = (IosBase::Word *)OperatorNew(sizeof(IosBase::Word));
        word->index = i;
        *wordAt = word;
        wordAt = &word->next;
    }
    *wordAt = NULL;
    IosBase::Callback **callbackAt = &c->ios.callbacks;
    for (int i = 0; i < c->callbacks; i++) {
        IosBase::Callback *callback = (IosBase::Callback *)OperatorNew(sizeof(IosBase::Callback));
        callback->index = 100 + i;
        callback->function = RecordIosCallback;
        *callbackAt = callback;
        callbackAt = &callback->next;
    }
    *callbackAt = NULL;
    InitFake(&c->localeImpl, c->localeRefs);
    if (c->hasLocale) {
        c->ios.locale = (Locale *)OperatorNew(sizeof(Locale));
        c->ios.locale->impl = (Facet *)&c->localeImpl;
    }
    if (original) {
        XbeOriginalScope a(0x00131191);
        OrigIosDestruct(&c->ios, 0);
    } else {
        c->ios.IosBase::Destruct();
    }
    std::string out = Format("vt %s words %s calls %s locale refs %u", Where(c->ios.vtable).c_str(),
                             c->ios.words == NULL ? "null" : "kept", c->ios.callbacks == NULL ? "null" : "kept",
                             c->localeImpl.refs);
    return out;
}

void RunIos() {
    IosCase *c = new IosCase;
    memset(c, 0, sizeof(*c));
    Lend(&c->ios, sizeof(c->ios), "ios");
    Lend(&c->global, sizeof(c->global), "lentglobal");
    Lend(&c->localeImpl, sizeof(c->localeImpl), "localeimpl");
    static const uint32_t kFills[] = { 0, 0xcccccccc, 0xffffffff };
    static const uint32_t kRefs[] = { 0, 1, 7, 0xfffffffe, 0xffffffff };
    for (uint32_t fill : kFills) {
        for (uint32_t refs : kRefs) {
            c->fill = fill;
            c->localeRefs = refs;
            Compare("ios_base::_Init", IosInitCase, c);
        }
    }
    for (int state = 0; state < 16; state++) {
        for (int exceptions = 0; exceptions < 8; exceptions++) {
            for (int reraise = 0; reraise < 2; reraise++) {
                if (!kCanCatch && (state & 7 & exceptions) != 0)
                    continue;
                c->state = state | (state & 8 ? 0x100 : 0);
                c->exceptions = exceptions;
                c->reraise = reraise != 0;
                Compare("ios_base::clear", IosClearCase, c);
            }
        }
    }
    for (int prefilled = 0; prefilled <= 8; prefilled += 2) {
        for (int selfAt = -1; selfAt < 8; selfAt += 3) {
            for (uint32_t standard = 0; standard <= 9; standard += 3) {
                c->prefilled = prefilled;
                c->selfAt = selfAt;
                c->standard = standard;
                Compare("ios_base::_Addstd", IosAddStandardCase, c);
            }
        }
    }
    static const int8_t kOpens[] = { 1, 2, 0, -1, 127, -128 };
    for (uint32_t standard = 0; standard <= 3; standard += 3) {
        for (int8_t opens : kOpens) {
            for (int lists = 0; lists < 3; lists++) {
                for (int hasLocale = 0; hasLocale < 2; hasLocale++) {
                    c->standard = standard;
                    c->opens = opens;
                    c->words = lists;
                    c->callbacks = lists == 2 ? 3 : lists;
                    c->hasLocale = hasLocale != 0;
                    c->localeRefs = lists == 1 ? 1 : 4;
                    Compare("ios_base::~ios_base", IosDestructCase, c);
                }
            }
        }
    }
    g_regions.resize(g_regions.size() - 3);
    delete c;
}

// ---------------------------------------------------------------------------------------------------------------
// Streams

struct StreamCase {
    char text[64];
    char empty[4];
    union {
        StrStreambuf buffer;
        uint8_t bufferBytes[sizeof(StrStreambuf)];
    };
    union {
        OStrStream ostream;
        uint8_t ostreamBytes[sizeof(OStrStream)];
    };
    union {
        IStrStream istream;
        uint8_t istreamBytes[sizeof(IStrStream)];
    };
    int count;
    int getAt, putAt;           // offsets into text (-1: null, -2: the empty string)
    int mode;
    bool freeFn;
    int constructVirtualBase;
};

char *TextAt(StreamCase *c, int at) {
    return at == -1 ? NULL : at == -2 ? c->empty : c->text + at;
}

std::string DescribeStreambuf(const StrStreambuf *b) {
    return Format("vt %s gf %s pf %s gn %s pn %s gc %d pc %d -> %s %s %s %s alsize %d pend %s seek %s mode %x "
                  "alloc %s free %s",
                  Where(b->vtable).c_str(), Where(b->getFirst).c_str(), Where(b->putFirst).c_str(),
                  Where(b->getNext).c_str(), Where(b->putNext).c_str(), b->getCountStore, b->putCountStore,
                  Where(b->getFirstStore).c_str(), Where(b->putFirstStore).c_str(),
                  Where(b->getNextStore).c_str(), Where(b->putNextStore).c_str(), b->allocSize,
                  Where(b->pendSave).c_str(), Where(b->seekHigh).c_str(), b->mode,
                  Where((const void *)b->allocFn).c_str(), b->freeFn == NULL ? "null" : "set");
}

std::string StrInitCase(bool original, void *context) {
    StreamCase *c = (StreamCase *)context;
    memset(c->bufferBytes, 0x5a, sizeof(c->bufferBytes));
    char *get = TextAt(c, c->getAt), *put = TextAt(c, c->putAt);
    if (original) {
        XbeOriginalScope a(0x00131901);
        OrigStrInit(&c->buffer, 0, c->count, get, put, c->mode);
    } else {
        c->buffer.Init(c->count, get, put, c->mode);
    }
    return DescribeStreambuf(&c->buffer);
}

std::string StrDestructCase(bool original, void *context) {
    StreamCase *c = (StreamCase *)context;
    memset(c->bufferBytes, 0, sizeof(c->bufferBytes));
    SetupStreambufConstruct(&c->buffer, 0);
    c->buffer.vtable = (const void *const *)uintptr_t(0x0018bdc4);
    c->buffer.mode = c->mode;
    c->buffer.seekHigh = c->text + 10;
    c->buffer.freeFn = c->freeFn ? RecordFree : NULL;
    // what _Tidy may free: the text, through the recorded free function, or else a block of its own (left over
    // where it is kept)
    c->buffer.getFirstStore = c->freeFn ? c->text : (char *)OperatorNew(32);
    if (original) {
        XbeOriginalScope a(0x00131a44);
        OrigStrDestruct(&c->buffer, 0);
    } else {
        c->buffer.Destruct();
    }
    return Format("vt %s seek %s mode %x", Where(c->buffer.vtable).c_str(), Where(c->buffer.seekHigh).c_str(),
                  c->buffer.mode);
}

// After the destructor the buffer's locale is freed: only whether the pointer is left.
std::string DescribeOStrStream(OStrStream *s, bool destroyed) {
    BasicIos *ios = s->Ios();
    return Format("vbt %s ios at %s: ", Where(s->vbtable).c_str(), Where(ios).c_str()) + DescribeIos(ios) +
           Format(" buf %s tie %s fill %d; ", Where(ios->streambuf).c_str(), Where(ios->tie).c_str(), ios->fill) +
           DescribeStreambuf(&s->buffer) +
           Format(" mutex %s locale %s", s->buffer.mutex.section != NULL ? "set" : "null",
                  s->buffer.locale == NULL ? "null" : destroyed ? "left" :
                  Where(s->buffer.locale->impl).c_str());
}

std::string OStrConstructCase(bool original, void *context) {
    StreamCase *c = (StreamCase *)context;
    memset(c->ostreamBytes, 0, sizeof(c->ostreamBytes));
    if (!c->constructVirtualBase) {   // as a derived class would have left it
        c->ostream.vbtable = (const int *)uintptr_t(0x001a3238);
        c->ostream.ios.vtable = (const void *const *)uintptr_t(0x0018bcc8);
    }
    char *text = TextAt(c, c->getAt);
    OStrStream *returned;
    if (original) {
        XbeOriginalScope a(0x00131b27);
        returned = OrigOStrConstruct(&c->ostream, 0, text, c->count, c->mode, c->constructVirtualBase);
    } else {
        returned = c->ostream.Construct(text, c->count, c->mode, c->constructVirtualBase);
    }
    std::string out = Format("ret %d ", returned == &c->ostream) + DescribeOStrStream(&c->ostream, false);
    // and destroyed again: the ostrstream part, then basic_ios
    if (original) {
        XbeOriginalScope a(0x00131bbb);
        OrigOStrDestruct(&c->ostream.ios, 0);
    } else {
        c->ostream.ios.DestructOStrStream();
    }
    out += "; destroyed " + DescribeOStrStream(&c->ostream, true);
    SetupBasicIosDestruct(&c->ostream.ios, 0);
    return out;
}

std::string IStrDestructCase(bool original, void *context) {
    StreamCase *c = (StreamCase *)context;
    memset(c->istreamBytes, 0, sizeof(c->istreamBytes));
    SetupIStrStreamConstruct(&c->istream, 0, TextAt(c, c->getAt), 1);
    if (original) {
        XbeOriginalScope a(0x00131ae1);
        OrigIStrDestruct(&c->istream.ios, 0);
    } else {
        c->istream.ios.DestructIStrStream();
    }
    BasicIos *ios = c->istream.Ios();
    std::string out = Format("vbt %s ios vt %s ", Where(c->istream.vbtable).c_str(), Where(ios->vtable).c_str()) +
                      DescribeStreambuf(&c->istream.buffer);
    SetupBasicIosDestruct(&c->istream.ios, 0);
    return out;
}

void RunStreams() {
    StreamCase *c = new StreamCase;
    memset(c, 0, sizeof(*c));
    strcpy(c->text, "0.25 17 -3 1e5 tuning text");
    Lend(c->text, sizeof(c->text), "text");
    Lend(c->empty, sizeof(c->empty), "empty");
    Lend(c->bufferBytes, sizeof(c->bufferBytes), "buf");
    Lend(c->ostreamBytes, sizeof(c->ostreamBytes), "os");
    Lend(c->istreamBytes, sizeof(c->istreamBytes), "is");
    static const int kCounts[] = { -1, 0, 1, 5, 26, 31, 32, 33, 100, -100 };
    static const int kGets[] = { -1, -2, 0, 4 };
    static const int kPuts[] = { -1, 0, 2, 4, 10, 26, 40 };
    static const int kModes[] = { 0, 1, 2, 4, 8, 9, 15 };
    for (int count : kCounts) {
        for (int get : kGets) {
            for (int put : kPuts) {
                for (int mode : kModes) {
                    if (mode != 0 && put != -1 && put != 10)
                        continue;
                    c->count = count;
                    c->getAt = get;
                    c->putAt = put;
                    c->mode = mode;
                    Compare("strstreambuf::_Init", StrInitCase, c);
                }
            }
        }
    }
    for (int mode : kModes) {
        for (int freeFn = 0; freeFn < 2; freeFn++) {
            c->mode = mode;
            c->freeFn = freeFn != 0;
            Compare("strstreambuf::~strstreambuf", StrDestructCase, c);
        }
    }
    static const int kOCounts[] = { 0x20, 0, 5, -1 };
    for (int count : kOCounts) {
        for (int get : { 0, 4, -2, -1 }) {
            for (int mode : { 2, 10, 8, 0 }) {
                for (int vbase = 0; vbase < 2; vbase++) {
                    c->count = count;
                    c->getAt = get;
                    c->mode = mode;
                    c->constructVirtualBase = vbase;
                    Compare("ostrstream", OStrConstructCase, c);
                }
            }
        }
    }
    for (int get : { 0, 4, -2 }) {
        c->getAt = get;
        Compare("istrstream::~istrstream", IStrDestructCase, c);
    }
    g_regions.resize(g_regions.size() - 5);
    delete c;
}

// ---------------------------------------------------------------------------------------------------------------
// _Stod

struct StodCase {
    char text[96];
    long power;
    bool asFloat;
};

std::string StodCaseFn(bool original, void *context) {
    StodCase *c = (StodCase *)context;
    char *end = NULL;
    *Crt::_errno() = 0;
    double value;
    if (original) {
        XbeOriginalScope a(0x00131c45), b(0x00131c79);
        value = c->asFloat ? OrigStof(c->text, &end, c->power) : OrigStod(c->text, &end, c->power);
    } else {
        value = c->asFloat ? Stof(c->text, &end, c->power) : Stod(c->text, &end, c->power);
    }
    uint64_t bits;
    memcpy(&bits, &value, 8);
    return Format("%016llx end %d errno %d", (unsigned long long)bits, int(end - c->text), *Crt::_errno());
}

void RandomNumeral(char *out) {
    static const char *const kFixed[] = {
        "0", "1", "-0", "0.1", "1e308", "1e-308", "4.9e-324", "2.2250738585072014e-308", "1.7976931348623157e308",
        "123456789012345678901234567890123456789", "0.000000000000000000000000000000000000001", ".5", "-.5e+3",
        "1e", "e5", "+", "nan", "inf", "  42", "9007199254740993", "0x10",
    };
    if (Below(4) == 0) {
        strcpy(out, kFixed[Below(sizeof(kFixed) / sizeof(kFixed[0]))]);
        return;
    }
    char *at = out;
    if (Below(3) == 0)
        *at++ = Below(2) ? '-' : '+';
    int digits = 1 + int(Below(40));
    int point = int(Below(digits + 2)) - 1;
    for (int i = 0; i < digits; i++) {
        if (i == point)
            *at++ = '.';
        *at++ = char('0' + Below(10));
    }
    if (Below(2)) {
        *at++ = 'e';
        if (Below(2))
            *at++ = '-';
        at += sprintf(at, "%u", Below(Below(4) == 0 ? 400 : 40));
    }
    *at = '\0';
}

void RunStod() {
    StodCase c;
    static const long kPowers[] = { 0, 0, 1, -1, 3, -3, 17, -17, 36, -36, 300, -300, 400, -400 };
    for (int i = 0; i < 3000; i++) {
        RandomNumeral(c.text);
        c.power = i < 2000 ? kPowers[Below(sizeof(kPowers) / sizeof(kPowers[0]))] : long(Below(81)) - 40;
        c.asFloat = Below(3) == 0;
        Compare(c.asFloat ? "_Stof" : "_Stod", StodCaseFn, &c);
    }
    // small decimals as num_get passes them: "0.0005" becomes "0.05" and a power of -2
    for (int i = 0; i < 1000; i++) {
        int zeros = 2 + int(Below(12));
        sprintf(c.text, "0.0%u", 1 + Below(Below(2) ? 1000 : 10000000));
        c.power = -long(zeros - 1);
        c.asFloat = Below(2) == 0;
        Compare(c.asFloat ? "_Stof" : "_Stod", StodCaseFn, &c);
    }
    // near and below the doubles' smallest, where the x87's exponent shows
    for (int i = 0; i < 2000; i++) {
        sprintf(c.text, "%u.%ue-%u", Below(100000), Below(1000000), 280 + Below(50));
        c.power = Below(2) ? -long(Below(60)) : long(Below(40));
        c.asFloat = Below(4) == 0;
        Compare(c.asFloat ? "_Stof" : "_Stod", StodCaseFn, &c);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Exceptions

struct ExceptionCase {
    Exception object, other;
    const char *message;
    int doFree;
    void *throwObject;
    const void *throwInfo;
    int which;
    size_t size;
    int count;
    char elements[64];
    int processingThrow;
};

std::string DescribeException(const Exception *e) {
    std::string out = Format("vt %s free %d what ", Where(e->vtable).c_str(), e->doFree);
    if (e->what == NULL)
        return out + "null";
    return out + Where(e->what) + "=" + e->what;
}

std::string ExceptionConstructCase(bool original, void *context) {
    ExceptionCase *c = (ExceptionCase *)context;
    memset(&c->object, 0x77, sizeof(c->object));
    Exception *returned;
    if (original) {
        XbeOriginalScope a(0x001325e6);
        returned = OrigExceptionConstruct(&c->object, 0);
    } else {
        returned = c->object.Construct();
    }
    return Format("ret %d ", returned == &c->object) + DescribeException(&c->object);
}

std::string BadCastCase(bool original, void *context) {
    ExceptionCase *c = (ExceptionCase *)context;
    memset(&c->object, 0x77, sizeof(c->object));
    BadCast *object = (BadCast *)&c->object;
    BadCast *returned;
    std::string out;
    if (original) {
        XbeOriginalScope a(0x001326ae), b(0x0013268b);
        returned = OrigBadCast(object, 0, c->message);
        out = Format("ret %d ", returned == object) + DescribeException(object);
        OrigExceptionDestruct(object, 0);
    } else {
        returned = object->Construct(c->message);
        out = Format("ret %d ", returned == object) + DescribeException(object);
        object->Destruct();
    }
    return out + Format("; vt %s", Where(object->vtable).c_str());
}

std::string ExceptionCopyCase(bool original, void *context) {
    ExceptionCase *c = (ExceptionCase *)context;
    memset(&c->object, 0x77, sizeof(c->object));
    c->other.vtable = (const void *)uintptr_t(0x001a3288);
    c->other.doFree = c->doFree;
    c->other.what = c->message;
    if (c->doFree && c->message != NULL) {   // a copy of its own, which a copy sharing it would free
        char *own = (char *)Crt::malloc(strlen(c->message) + 1);
        strcpy(own, c->message);
        c->other.what = own;
    }
    Exception *returned;
    std::string out;
    if (original) {
        XbeOriginalScope a(0x0013263c), b(0x0013268b);
        returned = OrigExceptionCopy(&c->object, 0, &c->other);
        out = Format("ret %d shared %d ", returned == &c->object, c->object.what == c->other.what) +
              DescribeException(&c->object);
        OrigExceptionDestruct(&c->object, 0);
    } else {
        returned = c->object.ConstructCopy(&c->other);
        out = Format("ret %d shared %d ", returned == &c->object, c->object.what == c->other.what) +
              DescribeException(&c->object);
        c->object.Destruct();
    }
    if (c->other.what != c->message && out.find("shared 1") == std::string::npos)
        Crt::free((void *)c->other.what);
    return out + Format("; vt %s", Where(c->object.vtable).c_str());
}

std::string ThrowCase(bool original, void *context) {
    ExceptionCase *c = (ExceptionCase *)context;
    if (c->throwObject != NULL) {   // a bad_cast, again: the other side's unwinding destroyed it
        BadCast *thrown = (BadCast *)c->throwObject;
        thrown->vtable = (const void *)uintptr_t(0x001a3288);
        thrown->what = "thrown";
        thrown->doFree = 0;
    }
    if (original) {
        XbeOriginalScope a(0x001325ad);
        OrigThrow(c->throwObject, (const Crt::ThrowInfo *)c->throwInfo);
    } else {
        Crt::CxxThrowException(c->throwObject, (const Crt::ThrowInfo *)c->throwInfo);
    }
    return "returned";
}

std::string StringThrowCase(bool original, void *context) {
    ExceptionCase *c = (ExceptionCase *)context;
    if (original) {
        XbeOriginalScope a(0x00130b80), b(0x00130bc0);
        if (c->which == 0)
            OrigXran();
        else
            OrigXlen();
    } else {
        if (c->which == 0)
            String::Xran();
        else
            String::Xlen();
    }
    return "returned";
}

std::string UncaughtCase(bool original, void *context) {
    ExceptionCase *c = (ExceptionCase *)context;
    Crt::PerThread()->processingThrow = c->processingThrow;
    bool result;
    if (original) {
        XbeOriginalScope a(0x00131c2c);
        result = OrigUncaught();
    } else {
        result = UncaughtException();
    }
    return Format("%d", result);
}

std::string VectorCase(bool original, void *context) {
    ExceptionCase *c = (ExceptionCase *)context;
    Crt::ElementFn construct = (Crt::ElementFn)XbeAddress(&Element::Construct);
    Crt::ElementFn destruct = (Crt::ElementFn)XbeAddress(&Element::Destruct);
    if (original) {
        XbeOriginalScope a(0x0013326e), b(0x0013332e);
        OrigVectorConstructor(c->elements, c->size, c->count, construct, destruct);
        OrigVectorDestructor(c->elements, c->size, c->count, destruct);
    } else {
        Crt::EhVectorConstructor(c->elements, c->size, c->count, construct, destruct);
        Crt::EhVectorDestructor(c->elements, c->size, c->count, destruct);
    }
    return "";
}

void RunExceptions() {
    ExceptionCase *c = new ExceptionCase;
    memset(c, 0, sizeof(*c));
    Lend(&c->object, sizeof(c->object), "object");
    Lend(&c->other, sizeof(c->other), "other");
    Lend(c->elements, sizeof(c->elements), "elements");
    static char longMessage[300];
    memset(longMessage, 'm', sizeof(longMessage) - 1);
    static const char *const kMessages[] = { "", "bad cast", "x", longMessage };
    Compare("exception::exception", ExceptionConstructCase, c);
    for (const char *message : kMessages) {
        c->message = message;
        Lend(message, strlen(message) + 1, "message");
        Compare("bad_cast::bad_cast", BadCastCase, c);
        for (int doFree = 0; doFree < 3; doFree++) {
            c->doFree = doFree;
            Compare("exception's copy", ExceptionCopyCase, c);
        }
        g_regions.pop_back();
    }
    c->message = NULL;
    c->doFree = 0;
    Compare("exception's copy (no message)", ExceptionCopyCase, c);
    if (kCanCatch) {
        // a rethrow, and a bad_cast (whose destructor the catching side's unwinding runs)
        c->throwObject = NULL;
        c->throwInfo = NULL;
        Compare("_CxxThrowException", ThrowCase, c);
        static BadCast thrown;
        Lend(&thrown, sizeof(thrown), "thrown");
        c->throwObject = &thrown;
        c->throwInfo = (const void *)uintptr_t(0x001a952c);
        Compare("_CxxThrowException", ThrowCase, c);
        g_regions.pop_back();
        for (int which = 0; which < 2; which++) {
            c->which = which;
            Compare(which == 0 ? "_Xran" : "_Xlen", StringThrowCase, c);
        }
    }
    for (int processing : { 0, 1, 2, -1 }) {
        c->processingThrow = processing;
        Compare("uncaught_exception", UncaughtCase, c);
    }
    static const size_t kSizes[] = { 1, 4, 12, 0 };
    static const int kElementCounts[] = { 0, 1, 2, 5, -1, -3 };
    for (size_t size : kSizes) {
        for (int count : kElementCounts) {
            if (size * (count > 0 ? count : 0) > sizeof(c->elements))
                continue;
            c->size = size;
            c->count = count;
            Compare("eh vector iterators", VectorCase, c);
        }
    }
    g_regions.resize(g_regions.size() - 3);
    delete c;
}

// ---------------------------------------------------------------------------------------------------------------
// strstreambuf's virtual functions: overflow, pbackfail, underflow, seekoff, seekpos, on a buffer laid out over a lent
// text, its allocations from a lent arena (or the game's operator new[], the new block lent once made)

struct BufferLayout {
    int getFirst, getNext, getEnd;   // offsets into the text, -1 null
    int putFirst, putNext, putEnd;
    int seekHigh;
};

struct BufferCase {
    char text[48];
    char arena[256];
    int arenaUsed;
    union {
        StrStreambuf buffer;
        uint8_t bufferBytes[sizeof(StrStreambuf)];
    };
    BufferLayout layout;
    int mode, allocSize;
    int alloc;                       // 0 operator new[], 1 the recording allocator, 2 it and the recording free
    int op;                          // 0 overflow, 1 pbackfail, 2 underflow, 3 seekoff, 4 seekpos
    int ch;                          // -3: the character before the get pointer
    int offset, way, which;
    StreamPos position;
};

BufferCase *g_bufferCase;

void *RecordAlloc(size_t size) {
    BufferCase *c = g_bufferCase;
    g_calls.push_back(Format("alloc %u", unsigned(size)));
    if (c->arenaUsed + size > sizeof(c->arena))
        return NULL;
    void *block = c->arena + c->arenaUsed;
    c->arenaUsed += int((size + 15) & ~size_t(15));
    return block;
}

char *BufferAt(BufferCase *c, int at) {
    return at < 0 ? NULL : c->text + at;
}

void LayBuffer(BufferCase *c) {
    for (int i = 0; i < int(sizeof(c->text)); i++)
        c->text[i] = char('a' + i % 26);
    memset(c->arena, 0, sizeof(c->arena));
    c->arenaUsed = 0;
    memset(c->bufferBytes, 0, sizeof(c->bufferBytes));
    StrStreambuf *b = &c->buffer;
    const BufferLayout &l = c->layout;
    b->vtable = (const void *const *)uintptr_t(0x0018bdc4);
    b->getFirst = &b->getFirstStore;
    b->putFirst = &b->putFirstStore;
    b->getNext = &b->getNextStore;
    b->putNext = &b->putNextStore;
    b->getCount = &b->getCountStore;
    b->putCount = &b->putCountStore;
    b->getFirstStore = BufferAt(c, l.getFirst);
    b->getNextStore = BufferAt(c, l.getNext);
    b->getCountStore = l.getNext < 0 ? 0 : l.getEnd - l.getNext;
    b->putFirstStore = BufferAt(c, l.putFirst);
    b->putNextStore = BufferAt(c, l.putNext);
    b->putCountStore = l.putNext < 0 ? 0 : l.putEnd - l.putNext;
    b->seekHigh = BufferAt(c, l.seekHigh);
    b->mode = c->mode;
    b->allocSize = c->allocSize;
    b->allocFn = c->alloc != 0 ? RecordAlloc : NULL;
    b->freeFn = c->alloc == 2 ? RecordFree : NULL;
}

std::string Bytes(const char *p, int n) {
    std::string out;
    for (int i = 0; i < n; i++)
        out += Format("%02x", uint8_t(p[i]));
    return out;
}

std::string BufferOpCase(bool original, void *context) {
    BufferCase *c = (BufferCase *)context;
    g_bufferCase = c;
    LayBuffer(c);
    StrStreambuf *b = &c->buffer;
    int ch = c->ch;
    if (ch == -3)
        ch = *b->getNext != NULL && *b->getNext > *b->getFirst ? uint8_t((*b->getNext)[-1]) : 'q';
    StreamPos result;
    memset(&result, 0x5a, sizeof(result));
    int returned = 0;
    StreamPos *returnedPos = NULL;
    if (original) {
        XbeOriginalScope a(0x001314ae), p(0x0013166a), u(0x001316b3), o(0x00131702), s(0x0013183b);
        switch (c->op) {
        case 0: returned = OrigStrOverflow(b, 0, ch); break;
        case 1: returned = OrigStrPbackfail(b, 0, ch); break;
        case 2: returned = OrigStrUnderflow(b, 0); break;
        case 3: returnedPos = OrigStrSeekOff(b, 0, &result, c->offset, c->way, c->which); break;
        case 4: returnedPos = OrigStrSeekPos(b, 0, &result, c->position, c->which); break;
        }
    } else {
        switch (c->op) {
        case 0: returned = b->Overflow(ch); break;
        case 1: returned = b->Pbackfail(ch); break;
        case 2: returned = b->Underflow(); break;
        case 3: returnedPos = b->SeekOff(&result, c->offset, c->way, c->which); break;
        case 4: returnedPos = b->SeekPos(&result, c->position, c->which); break;
        }
    }
    // a block from operator new[]: lent while described, then freed
    char *made = NULL;
    int madeSize = 0;
    if (c->alloc == 0 && (b->mode & 1) && !(c->mode & 1) && *b->getFirst != NULL) {
        made = *b->getFirst;
        madeSize = int(*b->putNext + *b->putCount - made);
        Lend(made, size_t(madeSize), "new");
    }
    std::string out = Format("ret %d pos ", returned);
    if (c->op >= 3)
        out += Format("%s %d %08x%08x %d ", returnedPos == &result ? "result" : Where(returnedPos).c_str(),
                      result.offset, result.position[1], result.position[0], result.state);
    out += DescribeStreambuf(b) + " text " + Bytes(c->text, sizeof(c->text));
    if (c->arenaUsed > 0)
        out += " arena " + Bytes(c->arena, c->arenaUsed);
    if (made != NULL) {
        out += Format(" new %d ", madeSize) + Bytes(made, madeSize);
        g_regions.pop_back();
        OperatorDelete(made);
    }
    return out;
}

void RunBufferVirtuals() {
    BufferCase *c = new BufferCase;
    memset(c, 0, sizeof(*c));
    Lend(c->text, sizeof(c->text), "text");
    Lend(c->arena, sizeof(c->arena), "arena");
    Lend(c->bufferBytes, sizeof(c->bufferBytes), "buf");
    static const BufferLayout kLayouts[] = {
        { -1, -1, -1, -1, -1, -1, -1 },
        { 0, 4, 20, -1, -1, -1, 20 },
        { 0, 0, 10, -1, -1, -1, 10 },
        { -1, -1, -1, 0, 5, 30, 0 },
        { -1, -1, -1, 0, 5, 30, -1 },
        { 0, 0, 0, 0, 10, 32, 5 },
        { 0, 6, 10, 0, 10, 32, 12 },
        { 0, 20, 20, 0, 25, 40, 22 },
        { 0, 2, 40, 0, 40, 40, 40 },
        { 8, 9, 12, 8, 14, 14, 30 },
    };
    static const int kModes[] = { 0, 1, 2, 4, 5, 6, 12, 8 };
    static const int kAllocSizes[] = { 0, 4, 32, 100, 400 };
    static const int kOverflowChars[] = { kEof, 'A', 0xff, 0, -2 };
    static const int kPutBackChars[] = { kEof, -3, 'x', 0x80, -2, 0x141 };   // -3: the character before the get pointer
    for (const BufferLayout &layout : kLayouts) {
        c->layout = layout;
        for (int mode : kModes) {
            c->mode = mode;
            // a dynamic buffer with a get area and no put area (none is made so) sizes its growth from a null pointer
            bool growable = !(mode & 4) || layout.getNext < 0 || layout.putNext >= 0;
            // overflow: the allocation only matters for a dynamic buffer
            for (int ch : kOverflowChars) {
                if (!growable)
                    break;
                c->op = 0;
                c->ch = ch;
                for (int alloc = 0; alloc < 3; alloc++) {
                    if (alloc > 0 && !(mode & 4))
                        break;
                    if ((mode & 5) == 5 && alloc != 2)
                        continue;   // a dynamic buffer's own block freed: only through the recorded free
                    for (int allocSize : kAllocSizes) {
                        if (allocSize != 0 && !(mode & 4))
                            break;
                        c->alloc = alloc;
                        c->allocSize = allocSize;
                        Compare("strstreambuf::overflow", BufferOpCase, c);
                    }
                }
            }
            c->alloc = 2;
            c->allocSize = 0;
            for (int ch : kPutBackChars) {
                c->op = 1;
                c->ch = ch;
                Compare("strstreambuf::pbackfail", BufferOpCase, c);
            }
            c->op = 2;
            Compare("strstreambuf::underflow", BufferOpCase, c);
            if (mode != 0 && mode != 6)
                continue;
            for (int offset : { -1, 0, 3, 10, 40, 100, -5 }) {
                for (int way = 0; way < 4; way++) {
                    for (int which = 0; which < 4; which++) {
                        c->op = 3;
                        c->offset = offset;
                        c->way = way;
                        c->which = which;
                        Compare("strstreambuf::seekoff", BufferOpCase, c);
                    }
                }
            }
            static const int kPositions[][2] = { { -1, 0 }, { 0, 0 }, { 3, 0 }, { 0, 5 }, { 10, 0 }, { 2, -3 },
                                                 { 41, 0 }, { -2, 1 }, { 0, -1 } };
            for (const auto &position : kPositions) {
                for (int which = 0; which < 4; which++) {
                    c->op = 4;
                    memset(&c->position, 0, sizeof(c->position));
                    c->position.offset = position[0];
                    c->position.position[0] = uint32_t(position[1]);
                    c->position.position[1] = position[1] < 0 ? 0xffffffffu : 0;
                    c->which = which;
                    Compare("strstreambuf::seekpos", BufferOpCase, c);
                }
            }
        }
    }
    g_regions.resize(g_regions.size() - 3);
    delete c;
}

// ---------------------------------------------------------------------------------------------------------------
// The other virtual functions with no caller in game, and what they call: the exceptions' deleting destructors,
// copies and what(), ios_base::failure's _Doraise, type_info's destructor, _Locimp's deleting destructor, and the
// deleting destructors of a basic_ostream and an ostrstream entered at their basic_ios

struct VirtualCase {
    union {
        RuntimeError error;       // the largest: a runtime_error or failure, or an exception or bad_cast in front
        uint8_t errorBytes[sizeof(RuntimeError)];
    };
    union {
        RuntimeError other;
        uint8_t otherBytes[sizeof(RuntimeError)];
    };
    Locimp impl;
    Facet *facets[6];
    FakeFacet fakes[6];
    TypeInfo type;
    struct {
        Ostream stream;
        BasicIos ios;
    } ostream;
    union {
        OStrStream ostrstream;
        uint8_t ostrstreamBytes[sizeof(OStrStream)];
    };
    Streambuf buffer;
    char text[40];
    const char *message;
    int doFree;
    bool onHeap;                  // deleted with flags 1: a heap object, described only by what it returned
    unsigned flags;
    int kind;
    int facetCount;
    uint32_t refs[6];
};

// An exception's what(), by text only where the pointer is the library's own string
std::string WhatText(const char *what) {
    if (what == NULL)
        return "null";
    std::string where = Where(what);
    return (where[0] == '@' || where == "heap" ? std::string("const") : where) + "=" + what;
}

// The message a destructor may free: a C runtime block of its own when doFree is set
const char *OwnMessage(const char *message, int doFree) {
    if (!doFree || message == NULL)
        return message;
    char *own = (char *)Crt::malloc(strlen(message) + 1);
    strcpy(own, message);
    return own;
}

std::string ExceptionDeleteCase(bool original, void *context) {
    VirtualCase *c = (VirtualCase *)context;
    // kind: 0 exception, 1 bad_cast, 2 runtime_error, 3 failure
    static const uint32_t kVtables[] = { 0x001a326c, 0x001a3288, 0x001a31cc, 0x001a31d4 };
    Exception *object = c->onHeap ? (Exception *)OperatorNew(sizeof(RuntimeError)) : &c->error;
    memset(object, 0, sizeof(RuntimeError));
    object->vtable = (const void *)uintptr_t(kVtables[c->kind]);
    object->what = OwnMessage(c->message, c->doFree);
    object->doFree = c->doFree;
    if (c->kind >= 2)
        ((RuntimeError *)object)->message.ConstructText(c->text);
    void *returned;
    if (original) {
        XbeOriginalScope a(0x001326ea), b(0x00132706), r(0x001311fa), f(0x00131252), d(0x0013268b), e(0x001326df),
            g(0x001311d1), h(0x0013126e);
        switch (c->kind) {
        case 0: returned = OrigExceptionDelete(object, 0, c->flags); break;
        case 1: returned = OrigBadCastDelete(object, 0, c->flags); break;
        case 2: returned = OrigRuntimeErrorDelete(object, 0, c->flags); break;
        default: returned = OrigFailureDelete(object, 0, c->flags); break;
        }
    } else {
        switch (c->kind) {
        case 0: returned = object->Delete(c->flags); break;
        case 1: returned = ((BadCast *)object)->Delete(c->flags); break;
        case 2: returned = ((RuntimeError *)object)->Delete(c->flags); break;
        default: returned = ((Failure *)object)->Delete(c->flags); break;
        }
    }
    std::string out = Format("ret %d", returned == object);
    if (!(c->flags & 1)) {
        out += Format(" vt %s free %d", Where(object->vtable).c_str(), object->doFree);
        if (c->kind >= 2)   // left as _Tidy leaves it
            out += Format(" message size %u capacity %u", ((RuntimeError *)object)->message.size,
                          ((RuntimeError *)object)->message.capacity);
        if (c->onHeap)
            OperatorDelete(object);
    }
    return out;
}

std::string ExceptionWhatCase(bool original, void *context) {
    VirtualCase *c = (VirtualCase *)context;
    memset(c->errorBytes, 0, sizeof(c->errorBytes));
    c->error.vtable = (const void *)uintptr_t(c->kind == 0 ? 0x001a326c : 0x001a31cc);
    c->error.what = c->message;
    if (c->kind != 0)
        c->error.message.ConstructText(c->text);
    const char *what;
    if (original) {
        XbeOriginalScope a(0x001326a1), b(0x001311ec);
        what = c->kind == 0 ? OrigExceptionWhat(&c->error, 0) : OrigRuntimeErrorWhat(&c->error, 0);
    } else {
        what = c->kind == 0 ? c->error.Exception::What() : c->error.What();
    }
    std::string out = WhatText(what);
    if (c->kind != 0)
        c->error.message.Tidy(true);
    return out;
}

std::string DescribeError(RuntimeError *e, int kind) {
    std::string out = Format("vt %s free %d what %s", Where(e->vtable).c_str(), e->doFree,
                             e->what == NULL ? "null" : (Where(e->what) + "=" + e->what).c_str());
    if (kind >= 2)
        out += " message " + Text(&e->message);
    return out;
}

// Copy constructors, then destructors: bad_cast, runtime_error, failure
std::string ErrorCopyCase(bool original, void *context) {
    VirtualCase *c = (VirtualCase *)context;
    static const uint32_t kVtables[] = { 0, 0x001a3288, 0x001a31cc, 0x001a31d4 };
    memset(c->errorBytes, 0x77, sizeof(c->errorBytes));
    memset(c->otherBytes, 0, sizeof(c->otherBytes));
    c->other.vtable = (const void *)uintptr_t(kVtables[c->kind]);
    c->other.what = OwnMessage(c->message, c->doFree);
    c->other.doFree = c->doFree;
    if (c->kind >= 2)
        c->other.message.ConstructText(c->text);
    void *returned;
    std::string out;
    if (original) {
        XbeOriginalScope a(0x001326c7), b(0x0013133f), f(0x00131382), d(0x001326df), e(0x001311d1), g(0x0013126e),
            x(0x0013263c), y(0x0013268b);
        switch (c->kind) {
        case 1: returned = OrigBadCastCopy(&c->error, 0, &c->other); break;
        case 2: returned = OrigRuntimeErrorCopy(&c->error, 0, &c->other); break;
        default: returned = OrigFailureCopy(&c->error, 0, &c->other); break;
        }
        out = Format("ret %d shared %d ", returned == &c->error, c->error.what == c->other.what) +
              DescribeError(&c->error, c->kind);
        switch (c->kind) {
        case 1: OrigBadCastDestruct(&c->error, 0); break;
        case 2: OrigRuntimeErrorDestruct(&c->error, 0); break;
        default: OrigFailureDestruct(&c->error, 0); break;
        }
    } else {
        switch (c->kind) {
        case 1: returned = ((BadCast *)&c->error)->ConstructCopy((BadCast *)&c->other); break;
        case 2: returned = c->error.ConstructCopy(&c->other); break;
        default: returned = ((Failure *)&c->error)->ConstructCopy((Failure *)&c->other); break;
        }
        out = Format("ret %d shared %d ", returned == &c->error, c->error.what == c->other.what) +
              DescribeError(&c->error, c->kind);
        switch (c->kind) {
        case 1: ((BadCast *)&c->error)->Destruct(); break;
        case 2: c->error.Destruct(); break;
        default: ((Failure *)&c->error)->Destruct(); break;
        }
    }
    out += Format("; vt %s", Where(c->error.vtable).c_str());
    if (c->kind >= 2)
        out += Format(" message size %u capacity %u", c->error.message.size, c->error.message.capacity);
    if (c->other.what != c->message && out.find("shared 1") == std::string::npos)
        Crt::free((void *)c->other.what);
    if (c->kind >= 2)
        c->other.message.Tidy(true);
    return out;
}

// _Doraise: throws a copy of the failure (MSVC builds catch it)
std::string FailureRaiseCase(bool original, void *context) {
    VirtualCase *c = (VirtualCase *)context;
    memset(c->otherBytes, 0, sizeof(c->otherBytes));
    c->other.vtable = (const void *)uintptr_t(0x001a31d4);
    c->other.what = c->message;
    c->other.doFree = 0;
    c->other.message.ConstructText(c->text);
    if (original) {
        XbeOriginalScope a(0x0013139a), b(0x00131382), d(0x0013126e);
        OrigFailureRaise(&c->other, 0);
    } else {
        ((Failure *)&c->other)->Raise();
    }
    return "returned";
}

std::string TypeInfoCase(bool original, void *context) {
    VirtualCase *c = (VirtualCase *)context;
    TypeInfo *type = c->onHeap ? (TypeInfo *)OperatorNew(sizeof(TypeInfo)) : &c->type;
    type->vtable = (const void *const *)uintptr_t(0x001a3290);
    type->undecoratedName = NULL;
    if (c->message != NULL) {
        type->undecoratedName = (char *)Crt::malloc(strlen(c->message) + 1);
        strcpy(type->undecoratedName, c->message);
    }
    void *returned;
    if (original) {
        XbeOriginalScope a(0x0013274b), b(0x00132722);
        returned = OrigTypeInfoDelete(type, 0, c->flags);
    } else {
        returned = type->Delete(c->flags);
    }
    std::string out = Format("ret %d", returned == type);
    if (!(c->flags & 1)) {
        // our copy of the vtable is not a lent global: named by its game address here
        out += Format(" vt %s name %s", type->vtable == TypeInfoVtable ? "@001a3290" : Where(type->vtable).c_str(),
                      type->undecoratedName == NULL ? "null" : "left");
        if (c->onHeap)
            OperatorDelete(type);
    }
    return out;
}

std::string LocimpDeleteCase(bool original, void *context) {
    VirtualCase *c = (VirtualCase *)context;
    Locimp *impl = c->onHeap ? (Locimp *)OperatorNew(sizeof(Locimp)) : &c->impl;
    Facet **facets = NULL;
    if (c->facetCount > 0) {
        facets = (Facet **)Crt::malloc(c->facetCount * sizeof(Facet *));
        for (int i = 0; i < c->facetCount; i++) {
            InitFake(&c->fakes[i], c->refs[i]);
            facets[i] = i == 2 ? NULL : (Facet *)&c->fakes[i];
        }
    }
    InitLocimp(impl, facets, uint32_t(c->facetCount), false, 1);
    impl->name.ConstructText(c->text);
    void *returned;
    if (original) {
        XbeOriginalScope a(0x00130f23), b(0x00130e4c);
        returned = OrigLocimpDelete(impl, 0, c->flags);
    } else {
        returned = impl->Delete(c->flags);
    }
    std::string out = Format("ret %d refs", returned == impl);
    for (int i = 0; i < c->facetCount; i++)
        out += Format(" %u", c->fakes[i].refs);
    if (!(c->flags & 1)) {
        out += Format(" vt %s name size %u capacity %u", Where(impl->vtable).c_str(), impl->name.size,
                      impl->name.capacity);
        if (c->onHeap)
            OperatorDelete(impl);
    }
    return out;
}

// A basic_ostream (the basic_ios after its vbptr, as the game's vbtable has it) or an ostrstream, made by our
// constructors and deleted through the basic_ios's vtable slot
std::string IosDeleteCase(bool original, void *context) {
    VirtualCase *c = (VirtualCase *)context;
    void *object;
    BasicIos *ios;
    if (c->kind == 0) {
        Ostream *stream = c->onHeap ? (Ostream *)OperatorNew(sizeof(c->ostream)) : &c->ostream.stream;
        memset(stream, 0, sizeof(c->ostream));
        memset(&c->buffer, 0, sizeof(c->buffer));
        SetupStreambufConstruct(&c->buffer, 0);
        stream->Construct(&c->buffer, false, 1);
        object = stream;
        ios = stream->Ios();
    } else {
        OStrStream *stream = c->onHeap ? (OStrStream *)OperatorNew(sizeof(OStrStream)) : &c->ostrstream;
        memset(stream, 0, sizeof(OStrStream));
        stream->Construct(c->text, int(sizeof(c->text)), 2, 1);
        object = stream;
        ios = &stream->ios;
    }
    void *returned;
    if (original) {
        XbeOriginalScope a(0x001319e7), b(0x00131a06), d(0x00131c01), e(0x00131bbb);
        returned = c->kind == 0 ? OrigDeleteOstream(ios, 0, c->flags) : OrigDeleteOStrStream(ios, 0, c->flags);
    } else {
        returned = c->kind == 0 ? ios->DeleteOstream(c->flags) : ios->DeleteOStrStream(c->flags);
    }
    std::string out = Format("ret %d", returned == object);
    if (!(c->flags & 1)) {
        out += Format(" ios vt %s words %s calls %s", Where(ios->vtable).c_str(), Where(ios->words).c_str(),
                      Where(ios->callbacks).c_str());
        if (c->kind == 1)
            out += " " + DescribeStreambuf(&c->ostrstream.buffer);
        if (c->onHeap)
            OperatorDelete(object);
    }
    if (c->kind == 0)
        c->buffer.Destruct();
    return out;
}

void RunVirtuals() {
    VirtualCase *c = new VirtualCase;
    memset(c, 0, sizeof(*c));
    Lend(c->errorBytes, sizeof(c->errorBytes), "error");
    Lend(c->otherBytes, sizeof(c->otherBytes), "other");
    Lend(&c->impl, sizeof(c->impl), "impl");
    Lend(c->fakes, sizeof(c->fakes), "fakes");
    Lend(&c->type, sizeof(c->type), "type");
    Lend(&c->ostream, sizeof(c->ostream), "ostream");
    Lend(c->ostrstreamBytes, sizeof(c->ostrstreamBytes), "ostrstream");
    Lend(&c->buffer, sizeof(c->buffer), "buffer");
    Lend(c->text, sizeof(c->text), "text");
    static char longMessage[300];
    memset(longMessage, 'm', sizeof(longMessage) - 1);
    static const char *const kMessages[] = { NULL, "", "bad cast", longMessage };
    static const char *const kTexts[] = { "", "short", "a text of more than fifteen characters" };
    for (const char *text : kTexts) {
        strcpy(c->text, text);
        for (const char *message : kMessages) {
            c->message = message;
            if (message != NULL)
                Lend(message, strlen(message) + 1, "message");
            for (int kind = 0; kind < 4; kind++) {
                c->kind = kind;
                for (int doFree = 0; doFree < 2; doFree++) {
                    c->doFree = doFree;
                    for (unsigned flags : { 0u, 1u, 2u, 3u }) {
                        c->flags = flags;
                        c->onHeap = (flags & 1) != 0;
                        Compare("exceptions' deleting destructors", ExceptionDeleteCase, c);
                    }
                    if (kind > 0 && (message != NULL || !doFree))   // a copy strlens a message it must free
                        Compare("exceptions' copies and destructors", ErrorCopyCase, c);
                }
                if (kind < 2)
                    Compare("what()", ExceptionWhatCase, c);
            }
            if (kCanCatch)
                Compare("ios_base::failure::_Doraise", FailureRaiseCase, c);
            for (unsigned flags : { 0u, 1u }) {
                c->flags = flags;
                c->onHeap = flags != 0;
                Compare("type_info's deleting destructor", TypeInfoCase, c);
            }
            if (message != NULL)
                g_regions.pop_back();
        }
        static const uint32_t kRefs[][6] = { { 1, 1, 1, 1, 1, 1 }, { 2, 0, 1, 5, 1, 3 }, { 0, 0, 0, 0, 0, 0 } };
        for (int count : { 0, 1, 3, 6 }) {
            for (const auto &refs : kRefs) {
                for (unsigned flags : { 0u, 1u }) {
                    c->facetCount = count;
                    memcpy(c->refs, refs, sizeof(c->refs));
                    c->flags = flags;
                    c->onHeap = flags != 0;
                    Compare("_Locimp's deleting destructor", LocimpDeleteCase, c);
                }
            }
        }
        for (int kind = 0; kind < 2; kind++) {
            for (unsigned flags : { 0u, 1u }) {
                c->kind = kind;
                c->flags = flags;
                c->onHeap = flags != 0;
                Compare(kind == 0 ? "basic_ostream's deleting destructor" : "ostrstream's deleting destructor",
                        IosDeleteCase, c);
            }
        }
    }
    g_regions.resize(g_regions.size() - 9);
    delete c;
}

// ---------------------------------------------------------------------------------------------------------------

bool Enabled() {
    const char *value = getenv("NIGHTFIRE_CRUNTIMESHADOWB");
    return value != NULL && value[0] != '\0' && value[0] != '0';
}

void Summary(const char *part) {
    printf("%s %s: %d cases, %d checks, %d differ", kTag, part, g_cases, g_checks, g_differ);
    if (g_faults)
        printf(" (%d faults)", g_faults);
    printf("\n");
    fflush(stdout);
}

} // namespace

void CRuntimeShadowB_Run(void) {
    if (!Enabled())
        return;
    g_cases = g_checks = g_differ = g_faults = g_details = 0;
    bool verbose = getenv("NIGHTFIRE_CRUNTIMESHADOWB")[0] >= '2';
    g_trace = getenv("NIGHTFIRE_CRUNTIMESHADOWB")[0] == '3';
    g_thread = GetCurrentThreadId();
    g_kernelInitialize = KernelInitializeSlot;
    g_kernelEnter = KernelEnterSlot;
    g_kernelLeave = KernelLeaveSlot;
    KernelInitializeSlot = RecordInitialize;
    KernelEnterSlot = RecordEnter;
    KernelLeaveSlot = RecordLeave;
    static const struct {
        const char *name;
        void (*run)();
    } parts[] = {
        { "locks", RunLocks }, { "locale", RunLocale }, { "_Locinfo", RunLocinfo }, { "ctype", RunCtype },
        { "ios_base", RunIos }, { "streams", RunStreams }, { "_Stod", RunStod }, { "exceptions", RunExceptions },
        { "strstreambuf virtuals", RunBufferVirtuals }, { "other virtuals", RunVirtuals },
    };
    for (const auto &part : parts) {
        part.run();
        if (verbose)
            Summary(part.name);
    }
    KernelInitializeSlot = g_kernelInitialize;
    KernelEnterSlot = g_kernelEnter;
    KernelLeaveSlot = g_kernelLeave;
    Summary("locks, locale, _Locinfo, ctype, ios_base, strstreams, _Stod, exceptions, throws, array iterators, "
            "virtuals");
}
