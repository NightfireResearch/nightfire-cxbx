// The C++ library's string streams and numeric facets, as compiled into the debug-variable code. See StdStreams.h.
//
// Each function is the Dinkumware (MSVC 7) template body the listing shows, with what the compiler inlined
// (sentry constructors, sgetc/sbumpc, ~locale, std::string's destructor ...) written out where it stands, so that
// the runtime calls - locks, facet lookups, frees - come in the original's order.

#pragma fp_contract(off)

#include "StdStreams.h"

#include "DebugVarUntested.h"
#include "../engine/UMemory.hpp"
#include "../../helpers.h"
#include "../../common/xbeOverload.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace GameStd;

// ---- the game's vtables (the first word of each object) and the library's globals

#define FacetVtable ((const void *const *)0x0018bc6c)
#define BasicIosVtable ((const void *const *)0x0018bcc8)
#define IstreamIosVtable ((const void *const *)0x0018bccc)       // basic_ios inside a basic_istream
#define CtypeVtable ((const void *const *)0x0018bcd8)
#define StreambufVtable ((const void *const *)0x0018bd54)
#define NumGetVtable ((const void *const *)0x0018bd94)
#define StrStreambufVtable ((const void *const *)0x0018bdc4)
#define IStrStreamIosVtable ((const void *const *)0x0018bdf8)    // basic_ios inside an istrstream
#define IStrStreamVbtable ((const int *)0x0018bdfc)
#define NumPutVtable ((const void *const *)0x0018be04)
#define NumpunctVtable ((const void *const *)0x0018be30)
#define BadCastThrowInfo ((const void *)0x001a952c)

#define FacetIdCount U32_AT(0x00244838)       // locale::id::_Id_cnt
#define CtypeId U32_AT(0x00244888)            // ctype<char>::id (the runtime's)
#define NumGetId U32_AT(0x001e22e0)           // the ids of the facets instantiated here
#define NumPutId U32_AT(0x001e22e4)
#define NumpunctId U32_AT(0x001e22dc)
#define CtypeCache (*(const Facet **)0x001e22cc)      // _Facetptr<>::_Psave: the facet use_facet made itself
#define NumGetCache (*(const Facet **)0x001e22d0)
#define NumPutCache (*(const Facet **)0x001e22d4)
#define NumpunctCache (*(const Facet **)0x001e22d8)
#define ZeroStreamPos ((const uint32_t *)0x00244848)  // the fpos_t a failed seek reports
#define InitialMbState I32_AT(0x001e22c8)             // _Stinit, the conversion state it reports

// ---- the C++ runtime (the non-template library) and std::string's members compiled elsewhere

#define Lockit_Construct ((Lockit *(__fastcall *)(Lockit *, int, int))0x00130cef)
#define Lockit_Destruct ((void (__fastcall *)(Lockit *, int))0x00130d10)
#define Locale_GetFacet ((const Facet *(__fastcall *)(const Locale *, int, uint32_t))0x00130d9b)
#define Locale_Construct ((Locale *(__fastcall *)(Locale *, int))0x001310bf)
#define Facet_Register ((void (__fastcall *)(Facet *, int))0x00130e13)
#define Facet_Destruct ((void (__fastcall *)(Facet *, int))0x00037930)
#define Locinfo_Construct ((Locinfo *(__fastcall *)(Locinfo *, int, const char *))0x00130f8f)
#define Locinfo_Destruct ((void (__fastcall *)(Locinfo *, int))0x00130ed2)
#define Getctype ((CtypeVec *(__cdecl *)(CtypeVec *))0x00130c3c)
#define Getcvt ((uint64_t (__cdecl *)())0x00131c31)
#define Tolower ((int (__cdecl *)(int, const CtypeVec *))0x00130c00)
#define Toupper ((int (__cdecl *)(int, const CtypeVec *))0x0013143d)
#define IosBase_Clear ((void (__fastcall *)(IosBase *, int, int, bool))0x00131279)
#define IosBase_Init ((void (__fastcall *)(IosBase *, int))0x001313b8)
#define IosBase_AddStandard ((void (__fastcall *)(IosBase *, int))0x001310fa)
#define IosBase_Destruct ((void (__fastcall *)(IosBase *, int))0x00131191)
#define IosBase_GetLoc ((Locale *(__fastcall *)(IosBase *, int, Locale *))0x00037980)
#define Mutex_Construct ((void (__fastcall *)(uint32_t *, int))0x001313fe)
#define Mutex_Destruct ((void (__fastcall *)(uint32_t *, int))0x00131416)
#define Mutex_Lock ((void (__fastcall *)(uint32_t *, int))0x0013142b)
#define Mutex_Unlock ((void (__fastcall *)(uint32_t *, int))0x00131434)
#define StrStreambuf_Init ((void (__fastcall *)(StrStreambuf *, int, int, char *, char *, int))0x00131901)
#define StrStreambuf_Destruct ((void (__fastcall *)(StrStreambuf *, int))0x00131a44)
#define IStrStream_Destruct ((void (__fastcall *)(BasicIos *, int))0x00131ae1)   // entered at the virtual base
#define OStrStream_Destruct ((void (__fastcall *)(BasicIos *, int))0x00131bbb)
#define UncaughtException ((bool (__cdecl *)())0x00131c2c)
#define BadCast_Construct ((void *(__fastcall *)(void *, int, const char *))0x001326ae)
#define CxxThrowException ((void (__stdcall *)(void *, const void *))0x001325ad)

#define String_Eos ((void (__fastcall *)(String *, int, uint32_t))0x00012c80)
#define String_Copy ((void (__fastcall *)(String *, int, uint32_t, uint32_t))0x00013150)
#define String_Grow ((bool (__fastcall *)(String *, int, uint32_t, bool))0x00013300)
#define String_AssignText ((String *(__fastcall *)(String *, int, const char *, uint32_t))0x00013630)
#define String_ConstructCopy ((String *(__fastcall *)(String *, int, const String *))0x000136d0)
#define String_ConstructText ((String *(__fastcall *)(String *, int, const char *))0x00013840)
#define String_Tidy ((void (__fastcall *)(String *, int, bool))0x00013110)
#define String_Xran ((void (__fastcall *)(String *, int))0x00130b80)
#define String_Xlen ((void (__fastcall *)(String *, int))0x00130bc0)

#define CrtFree ((void (__cdecl *)(void *))0x001331dc)
#define Sprintf ((int (__cdecl *)(char *, const char *, ...))0x00132767)
#define Localeconv ((Lconv *(__cdecl *)())0x001333d6)
#define Isdigit ((int (__cdecl *)(int))0x0013399d)
#define Errno ((int *(__cdecl *)())0x00133664)
#define Stol ((long (__cdecl *)(const char *, char **, int))0x00133636)
#define Stoul ((unsigned long (__cdecl *)(const char *, char **, int))0x0013364d)
#define Stoll ((int64_t (__cdecl *)(const char *, char **, int))0x0013396f)
#define Stoull ((uint64_t (__cdecl *)(const char *, char **, int))0x00133986)
#define Stod ((double (__cdecl *)(const char *, char **, long))0x00131c45)
#define Stof ((double (__cdecl *)(const char *, char **, long))0x00131c79)   // float and long double both

namespace {

// The start of the C runtime's struct lconv.
struct Lconv {
    char *decimalPoint;
    char *thousandsSep;
    char *grouping;
};

static const char kCharMax = 0x7f;   // CHAR_MAX: "no further grouping"
static const uint32_t kNpos = 0xffffffff;

// A call through the object's (the game's) vtable.
template <class R, class C, class... A> inline R CallVirtual(C *object, int slot, A... args) {
    typedef R (C::*Method)(A...);
    return (object->*XbeVirtual<Method>(object, slot))(args...);
}

// The virtual slots called here.
enum : int {
    kSlotDelete = 0,
    kStreambufOverflow = 1, kStreambufUnderflow = 4, kStreambufUflow = 5, kStreambufSync = 11,
    kNumpunctDecimalPoint = 1, kNumpunctThousandsSep = 2, kNumpunctGrouping = 3, kNumpunctFalseName = 4,
    kNumpunctTrueName = 5,
    kNumGetFloat = 4, kNumGetLong = 8, kNumGetUInt = 9, kNumGetBool = 11,
    kNumPutULong = 6, kNumPutLong = 7,
};

// ~locale, inlined everywhere: drop a reference to the implementation, and delete it on the last.
void ReleaseLocale(Facet *impl) {
    if (impl == NULL)
        return;
    Lockit lock;
    Lockit_Construct(&lock, 0, 0);
    if (0 < impl->refs && impl->refs < 0xffffffff)
        --impl->refs;
    Facet *dead = impl->refs != 0 ? NULL : impl;
    Lockit_Destruct(&lock, 0);
    if (dead != NULL)
        CallVirtual<void *>(dead, kSlotDelete, 1u);
}

// basic_ios::clear(state | add), as setstate inlines it: a stream without a buffer is bad as well.
void SetState(BasicIos *ios, int add) {
    IosBase_Clear(ios, 0, ios->state | add | (ios->streambuf != NULL ? 0 : kBadBit), false);
}

// The streambuf's inline sgetc/sbumpc tests: is there a character in the get (put) area?
inline bool HasGet(Streambuf *buffer) {
    char *next = *buffer->getNext;
    return next != NULL && uintptr_t(next) < uintptr_t(next) + uint32_t(*buffer->getCount);
}

inline bool HasPut(Streambuf *buffer) {
    char *next = *buffer->putNext;
    return next != NULL && uintptr_t(next) < uintptr_t(next) + uint32_t(*buffer->putCount);
}

inline int Sgetc(Streambuf *buffer) {
    return HasGet(buffer) ? uint8_t(**buffer->getNext) : CallVirtual<int>(buffer, kStreambufUnderflow);
}

inline int Sbumpc(Streambuf *buffer) {
    if (HasGet(buffer)) {
        --*buffer->getCount;
        return uint8_t(*(*buffer->getNext)++);
    }
    return CallVirtual<int>(buffer, kStreambufUflow);
}

// ostreambuf_iterator's operator= inlined into _Iput: sputc by way of the out-of-line Sputc.
inline void PutInline(OutIter *dest, char ch) {
    if (dest->streambuf == NULL || dest->streambuf->Sputc(ch) == kEof)
        dest->failed = true;
}

// ~basic_string as it is inlined: free the long buffer (through the game's allocator), no reset.
inline void FreeString(String *s) {
    if (s->capacity >= 16 && s->text.pointer != NULL)
        UMemory::FastFree(s->text.pointer, s->capacity + 1);
}

// The same, followed by _Tidy's reset, for a temporary destroyed in the middle of a function.
inline void TidyString(String *s) {
    FreeString(s);
    s->capacity = 15;
    String_Eos(s, 0, 0);
}

// use_facet<F>(locale): the locale's facet, or (made once, kept in the cache) a default one.
const Facet *UseFacet(const Locale *locale, uint32_t &id, const Facet *&cache, uint32_t (*getCat)(const Facet **)) {
    Lockit lock;
    Lockit_Construct(&lock, 0, 0);
    if (id == 0) {
        Lockit idLock;
        Lockit_Construct(&idLock, 0, 0);
        if (id == 0)
            id = ++FacetIdCount;
        Lockit_Destruct(&idLock, 0);
    }
    const Facet *facet = Locale_GetFacet(locale, 0, id);
    if (facet == NULL) {
        facet = cache;
        if (facet == NULL) {
            if (getCat(&cache) == 0xffffffff) {
                uint32_t badCast[4];
                BadCast_Construct(badCast, 0, "bad cast");
                CxxThrowException(badCast, BadCastThrowInfo);
            }
            Facet *made = const_cast<Facet *>(cache);
            facet = made;
            Lockit refLock;
            Lockit_Construct(&refLock, 0, 0);
            if (made->refs < 0xffffffff)
                made->refs++;
            Lockit_Destruct(&refLock, 0);
            Facet_Register(made, 0);
        }
    }
    Lockit_Destruct(&lock, 0);
    return facet;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// basic_ios, basic_istream's destructors

// FUNC_AT(0x00037ae0)
void GameStd::BasicIos::Destruct() {
    vtable = BasicIosVtable;
    IosBase_Destruct(this, 0);
}

// FUNC_AT(0x00037af0)
void GameStd::BasicIos::DestructIstream() {
    DEBUGVAR_UNTESTED("basic_istream<char>::~basic_istream");   // only exception unwinding calls it
    Istream *stream = (Istream *)((char *)this - 8);
    stream->Ios()->vtable = IstreamIosVtable;
}

// FUNC_AT(0x00037b30)
BasicIos* GameStd::BasicIos::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x000384f0)
void* GameStd::BasicIos::DeleteIstream(unsigned flags) {
    DEBUGVAR_UNTESTED("basic_istream<char> deleting destructor");   // no plain istream is ever made
    Istream *stream = (Istream *)((char *)this - 8);
    stream->Ios()->vtable = IstreamIosVtable;
    Destruct();
    if (flags & 1)
        OperatorDelete(stream);
    return stream;
}

// FUNC_AT(0x00038c40)
void* GameStd::BasicIos::DeleteIStrStream(unsigned flags) {
    IStrStream *stream = (IStrStream *)((char *)this - 0x5c);   // the basic_ios is at +0x5c
    IStrStream_Destruct(this, 0);
    Destruct();
    if (flags & 1)
        OperatorDelete(stream);
    return stream;
}

// FUNC_AT(0x00038540)
void GameStd::BasicIos::Init(Streambuf *buffer, bool isStandard) {
    streambuf = buffer;
    tie = NULL;
    fill = ' ';
    IosBase_Init(this, 0);
    if (streambuf == NULL)
        IosBase_Clear(this, 0, state | kBadBit, false);
    if (isStandard)
        IosBase_AddStandard(this, 0);
}

// FUNC_AT(0x000380c0)
void GameStd::IStrStream::DestructAll() {
    IStrStream_Destruct(&ios, 0);
    ios.Destruct();
}

// FUNC_AT(0x000397f0)
void GameStd::OStrStream::DestructAll() {
    OStrStream_Destruct(&ios, 0);
    ios.Destruct();
}

// ---------------------------------------------------------------------------------------------------------------
// ctype<char>

// FUNC_AT(0x00037df0)
Ctype* GameStd::Ctype::Construct(const short *table, bool deleteTable, uint32_t refCount) {
    refs = refCount;
    vtable = CtypeVtable;
    Locinfo info;
    Locinfo_Construct(&info, 0, "C");
    CtypeVec fromRuntime;
    ctype = *Getctype(&fromRuntime);
    Locinfo_Destruct(&info, 0);
    if (table != NULL) {
        if (ctype.deleteTable > 0)
            CrtFree((void *)ctype.table);
        else if (ctype.deleteTable < 0)
            OperatorDelete((void *)ctype.table);
        ctype.table = table;
        ctype.deleteTable = deleteTable ? -1 : 0;
    }
    return this;
}

// FUNC_AT(0x00037ed0)
char GameStd::Ctype::DoToLower(char ch) {
    return Tolower(uint8_t(ch), &ctype);
}

// FUNC_AT(0x00037ef0)
const char* GameStd::Ctype::DoToLowerRange(char *first, const char *last) {
    for (; first != last; ++first)
        *first = Tolower(uint8_t(*first), &ctype);
    return first;
}

// FUNC_AT(0x00037f20)
char GameStd::Ctype::DoToUpper(char ch) {
    return Toupper(uint8_t(ch), &ctype);
}

// FUNC_AT(0x00037f40)
const char* GameStd::Ctype::DoToUpperRange(char *first, const char *last) {
    for (; first != last; ++first)
        *first = Toupper(uint8_t(*first), &ctype);
    return first;
}

// FUNC_AT(0x00037f70)
char GameStd::Ctype::DoWiden(char ch) {
    return ch;
}

// FUNC_AT(0x00037f80)
const char* GameStd::Ctype::DoWidenRange(const char *first, const char *last, char *dest) {
    memcpy(dest, first, last - first);
    return last;
}

// FUNC_AT(0x00037fb0)
char GameStd::Ctype::DoNarrow(char ch, char) {
    return ch;
}

// FUNC_AT(0x00037fc0)
const char* GameStd::Ctype::DoNarrowRange(const char *first, const char *last, char, char *dest) {
    memcpy(dest, first, last - first);
    return last;
}

// FUNC_AT(0x00037ff0)
uint32_t GameStd::Ctype::GetCat(const Facet **cache) {
    if (cache != NULL && *cache == NULL) {
        Ctype *made = (Ctype *)OperatorNew(sizeof(Ctype));
        *cache = made != NULL ? made->Construct(NULL, false, 0) : NULL;
    }
    return 2;   // _X_CTYPE
}

// FUNC_AT(0x00038060)
Ctype* GameStd::Ctype::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00038080)
void GameStd::Ctype::Destruct() {
    vtable = CtypeVtable;
    if (ctype.deleteTable > 0)
        CrtFree((void *)ctype.table);
    else if (ctype.deleteTable < 0)
        OperatorDelete((void *)ctype.table);
    vtable = FacetVtable;
}

// FUNC_AT(0x000385f0)
const Ctype* GameStd::UseCtype(const Locale *locale) {
    return (const Ctype *)UseFacet(locale, CtypeId, CtypeCache, &Ctype::GetCat);
}

// ---------------------------------------------------------------------------------------------------------------
// basic_streambuf<char>

// FUNC_AT(0x000387f0)
Streambuf* GameStd::Streambuf::Construct() {
    vtable = StreambufVtable;
    Mutex_Construct(&mutex, 0);
    Locale *made = (Locale *)OperatorNew(sizeof(Locale));
    locale = made != NULL ? Locale_Construct(made, 0) : NULL;
    InitPointers();
    return this;
}

// FUNC_AT(0x00038210)
void GameStd::Streambuf::Destruct() {
    Locale *owned = locale;
    vtable = StreambufVtable;
    if (owned != NULL) {
        ReleaseLocale(owned->impl);
        OperatorDelete(owned);
    }
    Mutex_Destruct(&mutex, 0);
}

// FUNC_AT(0x00038520)
Streambuf* GameStd::Streambuf::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00038590)
void GameStd::Streambuf::InitPointers() {
    getNext = &getNextStore;
    putNext = &putNextStore;
    getFirst = &getFirstStore;
    getCount = &getCountStore;
    putFirst = &putFirstStore;
    putCount = &putCountStore;
    *putFirst = NULL;
    *putNext = NULL;
    *putCount = 0;
    *getFirst = NULL;
    *getNext = NULL;
    *getCount = 0;
}

// FUNC_AT(0x00038280)
Streambuf* GameStd::Streambuf::SetBuf(char *, int) {
    return this;
}

// FUNC_AT(0x00038290)
int GameStd::Streambuf::Overflow(int) {
    return kEof;
}

// FUNC_AT(0x000382a0)
int GameStd::Streambuf::Underflow() {
    return kEof;
}

// FUNC_AT(0x000382b0)
int GameStd::Streambuf::Uflow() {
    if (CallVirtual<int>(this, kStreambufUnderflow) == kEof)
        return kEof;
    --*getCount;
    return uint8_t(*(*getNext)++);
}

// FUNC_AT(0x000382e0)
int GameStd::Streambuf::Xsgetn(char *out, int count) {
    int copied = 0;
    while (count > 0) {
        char *next = *getNext;
        int available;
        if (next != NULL && (available = *getCount) > 0) {
            if (count < available)
                available = count;
            memcpy(out, next, available);
            copied += available;
            *getCount -= available;
            *getNext += available;
            count -= available;
            out += available;
        } else {
            int ch = CallVirtual<int>(this, kStreambufUflow);
            if (ch == kEof)
                break;
            *out++ = char(ch);
            copied++;
            count--;
        }
    }
    return copied;
}

// FUNC_AT(0x00038380)
int GameStd::Streambuf::Xsputn(const char *text, int count) {
    int copied = 0;
    while (count > 0) {
        char *next = *putNext;
        int available;
        if (next != NULL && (available = *putCount) > 0) {
            if (count < available)
                available = count;
            memcpy(next, text, available);
            copied += available;
            *putCount -= available;
            *putNext += available;
            count -= available;
            text += available;
        } else {
            if (CallVirtual<int>(this, kStreambufOverflow, int(uint8_t(*text))) == kEof)
                break;
            text++;
            copied++;
            count--;
        }
    }
    return copied;
}

// FUNC_AT(0x00038430)
StreamPos* GameStd::Streambuf::SeekOff(StreamPos *result, int, int, int) {
    result->offset = -1;   // _BADOFF
    result->position[0] = ZeroStreamPos[0];
    result->position[1] = ZeroStreamPos[1];
    result->state = InitialMbState;
    return result;
}

// FUNC_AT(0x00038460)
StreamPos* GameStd::Streambuf::SeekPos(StreamPos *result, StreamPos, int) {
    result->offset = -1;
    result->position[0] = ZeroStreamPos[0];
    result->position[1] = ZeroStreamPos[1];
    result->state = InitialMbState;
    return result;
}

// FUNC_AT(0x00038860)
int GameStd::Streambuf::Snextc() {
    int ch;
    if (HasGet(this)) {
        --*getCount;
        ch = uint8_t(*(*getNext)++);
    } else {
        ch = CallVirtual<int>(this, kStreambufUflow);
    }
    if (ch == kEof)
        return kEof;
    if (HasGet(this))
        return uint8_t(**getNext);
    return CallVirtual<int>(this, kStreambufUnderflow);
}

// FUNC_AT(0x00039900)
int GameStd::Streambuf::Sputc(char ch) {
    if (HasPut(this)) {
        *Pninc() = ch;
        return uint8_t(ch);
    }
    return CallVirtual<int>(this, kStreambufOverflow, int(uint8_t(ch)));
}

// FUNC_AT(0x00039940)
char* GameStd::Streambuf::Pninc() {
    --*putCount;
    return (*putNext)++;
}

// FUNC_AT(0x00038af0)
StrStreambuf* GameStd::StrStreambuf::Construct(const char *text, int count) {
    Streambuf::Construct();
    vtable = StrStreambufVtable;
    StrStreambuf_Init(this, 0, count, const_cast<char *>(text), NULL, 2);   // _Constant
    return this;
}

// FUNC_AT(0x00038b50)
StrStreambuf* GameStd::StrStreambuf::Delete(unsigned flags) {
    StrStreambuf_Destruct(this, 0);
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// basic_istream<char>, istrstream

// FUNC_AT(0x000388c0)
bool GameStd::Istream::Ipfx(bool noSkip) {
    BasicIos *ios = Ios();
    if (ios->state == kGoodBit) {
        if (ios->tie != NULL)
            ios->tie->Flush();
        if (!noSkip && (Ios()->flags & kSkipWs)) {
            Locale locale;
            const Ctype *ctype = UseCtype(IosBase_GetLoc(Ios(), 0, &locale));
            ReleaseLocale(locale.impl);
            int ch = Sgetc(Ios()->streambuf);
            for (;;) {
                if (ch == kEof) {
                    SetState(Ios(), kEofBit);
                    break;
                }
                if (!(ctype->ctype.table[uint8_t(ch)] & 0x48))   // is(space | blank)
                    break;
                ch = Ios()->streambuf->Snextc();
            }
        }
        if (Ios()->state == kGoodBit)
            return true;
    }
    SetState(Ios(), kFailBit);
    return false;
}

// FUNC_AT(0x00038b70)
IStrStream* GameStd::IStrStream::Construct(const char *text, int constructVirtualBase) {
    if (constructVirtualBase) {
        vbtable = IStrStreamVbtable;
        ios.standardStream = 0;
        ios.vtable = BasicIosVtable;
    }
    Ios()->vtable = IstreamIosVtable;
    count = 0;
    BasicIos *base = Ios();
    base->streambuf = &buffer;
    base->tie = NULL;
    base->fill = ' ';
    IosBase_Init(base, 0);
    if (base->streambuf == NULL)
        IosBase_Clear(base, 0, base->state | kBadBit, false);
    Ios()->vtable = IStrStreamIosVtable;
    buffer.Construct(text, 0);
    return this;
}

// FUNC_AT(0x00038c70)
IstreamSentry* GameStd::IstreamSentry::Construct(Istream *istream, bool noSkip) {
    stream = istream;
    Streambuf *buffer = istream->Ios()->streambuf;
    if (buffer != NULL)
        Mutex_Lock(&buffer->mutex, 0);
    ok = stream->Ipfx(noSkip);
    return this;
}

namespace {

// ~sentry, inlined: unlock the buffer.
void DestructSentry(Istream *stream) {
    Streambuf *buffer = stream->Ios()->streambuf;
    if (buffer != NULL)
        Mutex_Unlock(&buffer->mutex, 0);
}

// The iterators an extractor hands its facet: the stream's buffer, and the end of stream.
inline InIter BeginOf(Streambuf *buffer) {
    InIter it = { buffer, buffer == NULL, 0 };
    return it;
}

inline InIter EndOfStream() {
    InIter it = { NULL, true, 0 };
    return it;
}

} // namespace

// FUNC_AT(0x00038ce0)
Istream* GameStd::ExtractChar(Istream *stream, char *value) {
    int state = kGoodBit;
    IstreamSentry sentry;
    sentry.Construct(stream, false);
    if (sentry.ok) {
        int ch = Sbumpc(stream->Ios()->streambuf);
        if (ch == kEof)
            state = kEofBit | kFailBit;
        else
            *value = char(ch);
    }
    if (state != kGoodBit)
        SetState(stream->Ios(), state);
    DestructSentry(sentry.stream);
    return stream;
}

// FUNC_AT(0x00038fa0)
Istream* GameStd::Istream::ExtractBool(bool *value) {
    int state = kGoodBit;
    IstreamSentry sentry;
    sentry.Construct(this, false);
    if (sentry.ok) {
        Locale locale;
        const NumGet *numGet = UseNumGet(IosBase_GetLoc(Ios(), 0, &locale));
        ReleaseLocale(locale.impl);
        BasicIos *ios = Ios();
        InIter result;
        CallVirtual<InIter *>(const_cast<NumGet *>(numGet), kNumGetBool, &result, BeginOf(ios->streambuf),
                              EndOfStream(), (IosBase *)ios, &state, value);
    }
    if (state != kGoodBit)
        SetState(Ios(), state);
    DestructSentry(sentry.stream);
    return this;
}

// FUNC_AT(0x00039120)
Istream* GameStd::Istream::ExtractInt(int *value) {
    int state = kGoodBit;
    IstreamSentry sentry;
    sentry.Construct(this, false);
    if (sentry.ok) {
        long read = 0;
        Locale locale;
        const NumGet *numGet = UseNumGet(IosBase_GetLoc(Ios(), 0, &locale));
        ReleaseLocale(locale.impl);
        BasicIos *ios = Ios();
        InIter result;
        CallVirtual<InIter *>(const_cast<NumGet *>(numGet), kNumGetLong, &result, BeginOf(ios->streambuf),
                              EndOfStream(), (IosBase *)ios, &state, &read);
        // operator>>(int &) range-checks the long against INT_MIN..INT_MAX, which on a 32-bit long never fails.
        if (!(state & kFailBit))
            *value = read;
        else
            state |= kFailBit;
    }
    if (state != kGoodBit)
        SetState(Ios(), state);
    DestructSentry(sentry.stream);
    return this;
}

// FUNC_AT(0x000392d0)
Istream* GameStd::Istream::ExtractUInt(unsigned *value) {
    int state = kGoodBit;
    IstreamSentry sentry;
    sentry.Construct(this, false);
    if (sentry.ok) {
        Locale locale;
        const NumGet *numGet = UseNumGet(IosBase_GetLoc(Ios(), 0, &locale));
        ReleaseLocale(locale.impl);
        BasicIos *ios = Ios();
        InIter result;
        CallVirtual<InIter *>(const_cast<NumGet *>(numGet), kNumGetUInt, &result, BeginOf(ios->streambuf),
                              EndOfStream(), (IosBase *)ios, &state, value);
    }
    if (state != kGoodBit)
        SetState(Ios(), state);
    DestructSentry(sentry.stream);
    return this;
}

// FUNC_AT(0x00039450)
Istream* GameStd::Istream::ExtractFloat(float *value) {
    int state = kGoodBit;
    IstreamSentry sentry;
    sentry.Construct(this, false);
    if (sentry.ok) {
        Locale locale;
        const NumGet *numGet = UseNumGet(IosBase_GetLoc(Ios(), 0, &locale));
        ReleaseLocale(locale.impl);
        BasicIos *ios = Ios();
        InIter result;
        CallVirtual<InIter *>(const_cast<NumGet *>(numGet), kNumGetFloat, &result, BeginOf(ios->streambuf),
                              EndOfStream(), (IosBase *)ios, &state, value);
    }
    if (state != kGoodBit)
        SetState(Ios(), state);
    DestructSentry(sentry.stream);
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// basic_ostream<char>

// FUNC_AT(0x00038790)
Ostream* GameStd::Ostream::Flush() {
    int state = kGoodBit;
    if (!(Ios()->state & (kBadBit | kFailBit)) && CallVirtual<int>(Ios()->streambuf, kStreambufSync) == kEof)
        state = kBadBit;
    if (state != kGoodBit)
        SetState(Ios(), state);
    return this;
}

// FUNC_AT(0x000399f0)
OstreamSentry* GameStd::OstreamSentry::ConstructBase(Ostream *ostream) {
    stream = ostream;
    Streambuf *buffer = ostream->Ios()->streambuf;
    if (buffer != NULL)
        Mutex_Lock(&buffer->mutex, 0);
    return this;
}

// FUNC_AT(0x000399d0)
void GameStd::OstreamSentry::DestructBase() {
    Streambuf *buffer = stream->Ios()->streambuf;
    if (buffer != NULL)
        Mutex_Unlock(&buffer->mutex, 0);
}

// FUNC_AT(0x00039950)
OstreamSentry* GameStd::OstreamSentry::Construct(Ostream *ostream) {
    ConstructBase(ostream);
    BasicIos *ios = ostream->Ios();
    if (ios->state == kGoodBit && ios->tie != NULL)
        ios->tie->Flush();
    ok = ostream->Ios()->state == kGoodBit;
    return this;
}

// FUNC_AT(0x00039a20)
void GameStd::OstreamSentry::Destruct() {
    if (!UncaughtException() && (stream->Ios()->flags & kUnitBuf))
        stream->Flush();   // _Osfx
    DestructBase();
}

// FUNC_AT(0x00039810)
Ostream* GameStd::Ostream::Put(char ch) {
    int state = kGoodBit;
    OstreamSentry sentry;
    sentry.Construct(this);
    if (!sentry.ok)
        state = kBadBit;
    else if (Ios()->streambuf->Sputc(ch) == kEof)
        state = kBadBit;
    if (state != kGoodBit)
        SetState(Ios(), state);
    sentry.Destruct();
    return this;
}

// FUNC_AT(0x00039a90)
Ostream* GameStd::Ostream::InsertUInt(unsigned value) {
    int state = kGoodBit;
    OstreamSentry sentry;
    sentry.Construct(this);
    if (sentry.ok) {
        Locale locale;
        const NumPut *numPut = UseNumPut(IosBase_GetLoc(Ios(), 0, &locale));
        ReleaseLocale(locale.impl);
        BasicIos *ios = Ios();
        OutIter dest = { false, {}, ios->streambuf };
        OutIter result;
        CallVirtual<OutIter *>(const_cast<NumPut *>(numPut), kNumPutULong, &result, dest, (IosBase *)ios, ios->fill,
                               (unsigned long)value);
        if (result.failed)
            state = kBadBit;
    }
    if (state != kGoodBit)
        SetState(Ios(), state);
    sentry.Destruct();
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// The iterators

// FUNC_AT(0x0003b770)
bool GameStd::IstreambufIterator::Equal(const IstreambufIterator *right) {
    if (!got)
        Peek();
    if (!right->got)
        const_cast<IstreambufIterator *>(right)->Peek();
    return (streambuf == NULL) == (right->streambuf == NULL);
}

// FUNC_AT(0x0003b7c0)
char GameStd::IstreambufIterator::Peek() {
    if (streambuf != NULL) {
        int ch = Sgetc(streambuf);
        if (ch != kEof) {
            value = char(ch);
            got = true;
            return char(ch);
        }
    }
    char last = value;
    streambuf = NULL;
    got = true;
    return last;
}

// FUNC_AT(0x0003bbd0)
void GameStd::IstreambufIterator::Inc() {
    if (streambuf != NULL && Sbumpc(streambuf) != kEof) {
        got = false;
        return;
    }
    streambuf = NULL;
    got = true;
}

// FUNC_AT(0x0003a900)
OutIter* GameStd::OstreambufIterator::Assign(char ch) {
    Streambuf *buffer = streambuf;
    if (buffer != NULL) {
        int put;
        if (HasPut(buffer)) {
            --*buffer->putCount;
            char *at = (*buffer->putNext)++;
            *at = ch;
            put = uint8_t(ch);
        } else {
            put = CallVirtual<int>(buffer, kStreambufOverflow, int(uint8_t(ch)));
        }
        if (put != kEof)
            return this;
    }
    failed = true;
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// The facets' construction and lookup

// FUNC_AT(0x00038700)
NumGet* GameStd::NumGet::Construct(uint32_t refCount) {
    refs = refCount;
    vtable = NumGetVtable;
    Locinfo info;
    Locinfo_Construct(&info, 0, "C");
    Locinfo_Destruct(&info, 0);   // _Init(info) is empty
    return this;
}

// FUNC_AT(0x00038770)
NumGet* GameStd::NumGet::Delete(unsigned flags) {
    Facet_Destruct(this, 0);
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00038a80)
uint32_t GameStd::NumGet::GetCat(const Facet **cache) {
    if (cache != NULL && *cache == NULL) {
        NumGet *made = (NumGet *)OperatorNew(sizeof(NumGet));
        *cache = made != NULL ? made->Construct(0) : NULL;
    }
    return 4;   // _X_NUMERIC
}

// FUNC_AT(0x00038e00)
const NumGet* GameStd::UseNumGet(const Locale *locale) {
    return (const NumGet *)UseFacet(locale, NumGetId, NumGetCache, &NumGet::GetCat);
}

// FUNC_AT(0x00039c00)
const NumPut* GameStd::UseNumPut(const Locale *locale) {
    return (const NumPut *)UseFacet(locale, NumPutId, NumPutCache, &NumPut::GetCat);
}

// FUNC_AT(0x00039d10)
uint32_t GameStd::NumPut::GetCat(const Facet **cache) {
    if (cache != NULL && *cache == NULL) {
        NumPut *made = (NumPut *)OperatorNew(sizeof(NumPut));
        if (made != NULL) {
            made->refs = 0;
            made->vtable = NumPutVtable;
            Locinfo info;
            Locinfo_Construct(&info, 0, "C");
            Locinfo_Destruct(&info, 0);
        }
        *cache = made;
    }
    return 4;
}

// FUNC_AT(0x0003a980)
const Numpunct* GameStd::UseNumpunct(const Locale *locale) {
    return (const Numpunct *)UseFacet(locale, NumpunctId, NumpunctCache, &Numpunct::GetCat);
}

// FUNC_AT(0x0003aa90)
uint32_t GameStd::Numpunct::GetCat(const Facet **cache) {
    if (cache != NULL && *cache == NULL) {
        Numpunct *made = (Numpunct *)OperatorNew(sizeof(Numpunct));
        if (made != NULL) {
            made->refs = 0;
            made->vtable = NumpunctVtable;
            Locinfo info;
            made->Init(Locinfo_Construct(&info, 0, "C"));
            Locinfo_Destruct(&info, 0);
        }
        *cache = made;
    }
    return 4;
}

// ---------------------------------------------------------------------------------------------------------------
// numpunct<char>

// FUNC_AT(0x0003ab40)
char GameStd::Numpunct::DoDecimalPoint() {
    return decimalPoint;
}

// FUNC_AT(0x0003ab50)
char GameStd::Numpunct::DoThousandsSep() {
    return thousandsSep;
}

// FUNC_AT(0x0003ab60)
Numpunct* GameStd::Numpunct::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x0003ab80)
void GameStd::Numpunct::Destruct() {
    vtable = NumpunctVtable;
    Tidy();
    vtable = FacetVtable;
}

// FUNC_AT(0x0003abd0)
void GameStd::Numpunct::Tidy() {
    OperatorDelete((void *)grouping);   // delete, not delete[], as the library has it
    OperatorDelete((void *)falseName);
    OperatorDelete((void *)trueName);
}

namespace {

inline CvtVec GetCvt() {
    uint64_t both = Getcvt();
    CvtVec cvt = { uint32_t(both), uint32_t(both >> 32) };
    return cvt;
}

} // namespace

// FUNC_AT(0x0003ac00)
void GameStd::Numpunct::Init(const Locinfo *) {
    Lconv *conventions = Localeconv();
    grouping = NULL;
    falseName = NULL;
    trueName = NULL;
    CvtVec cvt = GetCvt();
    grouping = MakeLocString(conventions->grouping, NULL, &cvt);
    cvt = GetCvt();
    falseName = MakeLocString("false", NULL, &cvt);
    cvt = GetCvt();
    trueName = MakeLocString("true", NULL, &cvt);
    decimalPoint = *conventions->decimalPoint;
    thousandsSep = *conventions->thousandsSep;
}

// FUNC_AT(0x0003acd0)
char* GameStd::Numpunct::MakeLocString(const char *text, char *, const CvtVec *) {
    uint32_t length = uint32_t(strlen(text)) + 1;
    char *copy = (char *)OperatorNewArray(length);
    memcpy(copy, text, length);
    return copy;
}

// FUNC_AT(0x0003a960)
String* GameStd::Numpunct::Grouping(String *result) {
    CallVirtual<String *>(this, kNumpunctGrouping, result);
    return result;
}

// FUNC_AT(0x0003cd90)
String* GameStd::Numpunct::FalseName(String *result) {
    CallVirtual<String *>(this, kNumpunctFalseName, result);
    return result;
}

// FUNC_AT(0x0003cdb0)
String* GameStd::Numpunct::TrueName(String *result) {
    CallVirtual<String *>(this, kNumpunctTrueName, result);
    return result;
}

// FUNC_AT(0x0003d3b0)
String* GameStd::Numpunct::DoGrouping(String *result) {
    String_ConstructText(result, 0, grouping);
    return result;
}

// FUNC_AT(0x0003d3d0)
String* GameStd::Numpunct::DoFalseName(String *result) {
    const char *name = falseName;
    result->capacity = 15;
    result->size = 0;
    result->text.buffer[0] = '\0';
    String_AssignText(result, 0, name, uint32_t(strlen(name)));
    return result;
}

// FUNC_AT(0x0003d420)
String* GameStd::Numpunct::DoTrueName(String *result) {
    const char *name = trueName;
    result->capacity = 15;
    result->size = 0;
    result->text.buffer[0] = '\0';
    String_AssignText(result, 0, name, uint32_t(strlen(name)));
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// std::string members instantiated here

// FUNC_AT(0x0003bcb0)
String* GameStd::String::ConstructFill(uint32_t count, char ch) {
    size = 0;
    capacity = 15;
    text.buffer[0] = '\0';
    AssignFill(count, ch);
    return this;
}

// FUNC_AT(0x0003bce0)
String* GameStd::String::AssignFill(uint32_t count, char ch) {
    if (count == kNpos)
        String_Xlen(this, 0);
    if (String_Grow(this, 0, count, true)) {
        memset(Data(), ch, count);
        size = count;
        Data()[count] = '\0';
    }
    return this;
}

// FUNC_AT(0x0003bc20)
String* GameStd::String::AppendFill(uint32_t count, char ch) {
    if (kNpos - size <= count)
        String_Xlen(this, 0);
    if (count > 0) {
        uint32_t newSize = size + count;
        if (String_Grow(this, 0, newSize, false)) {
            memset(Data() + size, ch, count);
            size = newSize;
            Data()[newSize] = '\0';
        }
    }
    return this;
}

// FUNC_AT(0x0003d090)
String* GameStd::String::AppendSub(const String *right, uint32_t offset, uint32_t count) {
    if (right->size < offset)
        String_Xran(this, 0);
    uint32_t available = right->size - offset;
    if (available < count)
        count = available;
    if (kNpos - size <= count)
        String_Xlen(this, 0);
    if (count > 0) {
        // _Grow(newSize), inlined
        uint32_t newSize = size + count;
        if (newSize > kNpos - 1)
            String_Xlen(this, 0);
        if (capacity < newSize) {
            String_Copy(this, 0, newSize, size);
        } else if (newSize == 0) {
            size = 0;
            Data()[0] = '\0';
            return this;
        }
        if (newSize > 0) {
            const char *from = (right->capacity < 16 ? right->text.buffer : right->text.pointer) + offset;
            memcpy(Data() + size, from, count);
            size = newSize;
            Data()[newSize] = '\0';
        }
    }
    return this;
}

// FUNC_AT(0x0005b0a0)
String* GameStd::String::Assign(const char *string) {
    return String_AssignText(this, 0, string, uint32_t(strlen(string)));
}

// ---------------------------------------------------------------------------------------------------------------
// num_put<char, ostreambuf_iterator<char> >

// FUNC_AT(0x0003a4a0)
char* GameStd::NumPut::IntFormat(char *format, const char *spec, int flags) {
    char *at = format;
    *at++ = '%';
    if (flags & kShowPos)
        *at++ = '+';
    if (flags & kShowBase)
        *at++ = '#';
    if (spec[0] != 'L') {
        *at = spec[0];
    } else {   // __int64: "I64"
        *at++ = 'I';
        *at++ = '6';
        *at = '4';
    }
    ++at;
    int base = flags & kBaseField;
    if (base == kOct)
        *at = 'o';
    else if (base != kHex)
        *at = spec[1];
    else
        *at = (flags & kUppercase) ? 'X' : 'x';
    at[1] = '\0';
    return format;
}

// FUNC_AT(0x0003afb0)
char* GameStd::NumPut::FloatFormat(char *format, char spec, int flags) {
    char *at = format;
    *at++ = '%';
    if (flags & kShowPos)
        *at++ = '+';
    if (flags & kShowPoint)
        *at++ = '#';
    *at++ = '.';
    *at++ = '*';
    if (spec != '\0')
        *at++ = spec;
    int field = flags & kFloatField;
    *at = field == kFixed ? 'f' : field != kScientific ? 'g' : 'e';
    at[1] = '\0';
    return format;
}

// The integer put: separators added by the locale's grouping (marked ',' in the text, then written as the
// facet's separator, or dropped when it has none), then the fill as the adjustment asks.
//
// FUNC_AT(0x0003a530)
OutIter* GameStd::NumPut::IntPut(OutIter *result, OutIter dest, IosBase *ios, char fill, char *text, uint32_t count) {
    uint32_t prefix = text[0] == '+' || text[0] == '-' ? 1
                    : text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 2 : 0;
    Locale locale;
    const Numpunct *punct = UseNumpunct(IosBase_GetLoc(ios, 0, &locale));
    ReleaseLocale(locale.impl);
    String grouping;
    const_cast<Numpunct *>(punct)->Grouping(&grouping);
    char separator = CallVirtual<char>(const_cast<Numpunct *>(punct), kNumpunctThousandsSep);
    const char *group = grouping.Data();
    bool grouped = *group > 0;
    if (grouped) {
        grouped = false;
        uint32_t offset = count;
        while (*group != kCharMax && *group > 0 && uint32_t(*group) < offset - prefix) {
            offset -= *group;
            memmove(&text[offset + 1], &text[offset], count + 1 - offset);
            text[offset] = ',';
            ++count;
            if (group[1] > 0)
                ++group;
            grouped = true;
        }
    }

    uint32_t fillCount = ios->width > 0 && uint32_t(ios->width) > count ? ios->width - count : 0;
    int adjust = ios->flags & kAdjustField;
    OutIter out = dest;
    if (adjust == kLeft) {
    } else if (adjust == kInternal) {
        for (uint32_t i = 0; i < prefix; i++)
            PutInline(&out, text[i]);
        text += prefix;
        count -= prefix;
        for (; fillCount > 0; fillCount--)
            out.Assign(fill);
    } else {
        for (; fillCount > 0; fillCount--)
            PutInline(&out, fill);
    }

    if (!grouped) {
        for (; count > 0; count--)
            out.Assign(*text++);
    } else {
        for (;;) {
            uint32_t run = uint32_t(strcspn(text, ","));
            for (uint32_t i = 0; i < run; i++)
                out.Assign(text[i]);
            text += run;
            count -= run;
            if (count == 0)
                break;
            if (separator != '\0')
                out.Assign(separator);
            ++text;
            --count;
        }
    }
    ios->width = 0;
    for (; fillCount > 0; fillCount--)
        PutInline(&out, fill);
    *result = out;
    String_Tidy(&grouping, 0, true);
    return result;
}

// FUNC_AT(0x0003a440)
OutIter* GameStd::NumPut::DoPutLong(OutIter *result, OutIter dest, IosBase *ios, char fill, long value) {
    char format[8];
    char text[64];
    int count = Sprintf(text, IntFormat(format, "ld", ios->flags), value);
    return IntPut(result, dest, ios, fill, text, count);
}

// FUNC_AT(0x0003ad10)
OutIter* GameStd::NumPut::DoPutULong(OutIter *result, OutIter dest, IosBase *ios, char fill, unsigned long value) {
    char format[8];
    char text[64];
    int count = Sprintf(text, IntFormat(format, "lu", ios->flags), value);
    return IntPut(result, dest, ios, fill, text, count);
}

// FUNC_AT(0x0003ad70)
OutIter* GameStd::NumPut::DoPutInt64(OutIter *result, OutIter dest, IosBase *ios, char fill, int64_t value) {
    char format[8];
    char text[64];
    int count = Sprintf(text, IntFormat(format, "Ld", ios->flags), value);
    return IntPut(result, dest, ios, fill, text, count);
}

// FUNC_AT(0x0003add0)
OutIter* GameStd::NumPut::DoPutUInt64(OutIter *result, OutIter dest, IosBase *ios, char fill, uint64_t value) {
    char format[8];
    char text[64];
    int count = Sprintf(text, IntFormat(format, "Lu", ios->flags), value);
    return IntPut(result, dest, ios, fill, text, count);
}

namespace {

// The start of do_put(double) and do_put(long double): the precision, split into what sprintf is asked for
// (at most 36 significant digits) and trailing zeros; in fixed notation, a huge value is scaled down and a tiny
// one up by powers of 1e10, the zeros that stand for them counted and written by _Fput instead.
struct FloatLayout {
    int significant;
    uint32_t beforePoint, afterPoint, trailing;
};

FloatLayout LayOutFloat(IosBase *ios, int flags, double &value) {
    int precision = ios->precision;
    if (precision <= 0 && !(ios->flags & kFixed))
        precision = 6;
    FloatLayout layout;
    layout.significant = precision > 36 ? 36 : precision;
    layout.trailing = precision - layout.significant;
    layout.beforePoint = 0;
    layout.afterPoint = 0;
    if ((flags & kFloatField) == kFixed) {
        double v = value;
        bool negative = v < 0.0;
        if (negative) {
            v = -v;
            value = v;
        }
        if (1e35 <= v) {
            while (layout.beforePoint < 5000) {
                v *= 1e-10;   // the compiler's reciprocal of the library's / 1e10
                layout.beforePoint += 10;
                if (!(1e35 <= v))
                    break;
            }
            value = v;
        }
        if (0.0 < v && int(layout.trailing) >= 10) {
            while (v <= 1e-35 && layout.afterPoint < 5000) {
                v *= 1e10;
                layout.trailing -= 10;
                layout.afterPoint += 10;
                if (int(layout.trailing) < 10)
                    break;
            }
            value = v;
        }
        if (negative)
            value = -v;
    }
    return layout;
}

} // namespace

// FUNC_AT(0x0003ae30)
OutIter* GameStd::NumPut::DoPutDouble(OutIter *result, OutIter dest, IosBase *ios, char fill, double value) {
    int flags = ios->flags;
    FloatLayout layout = LayOutFloat(ios, flags, value);
    char format[8];
    char text[108];
    int count = Sprintf(text, FloatFormat(format, '\0', flags), layout.significant, value);
    return FloatPut(result, dest, ios, fill, text, layout.beforePoint, layout.afterPoint, layout.trailing, count);
}

// FUNC_AT(0x0003b420)
OutIter* GameStd::NumPut::DoPutLongDouble(OutIter *result, OutIter dest, IosBase *ios, char fill, double value) {
    int flags = ios->flags;
    FloatLayout layout = LayOutFloat(ios, flags, value);
    char format[8];
    char text[108];
    int count = Sprintf(text, FloatFormat(format, 'L', flags), layout.significant, value);   // _Ffmt inlined
    return FloatPut(result, dest, ios, fill, text, layout.beforePoint, layout.afterPoint, layout.trailing, count);
}

// FUNC_AT(0x0003b5e0)
OutIter* GameStd::NumPut::DoPutPointer(OutIter *result, OutIter dest, IosBase *ios, char fill, const void *value) {
    char text[64];
    int count = Sprintf(text, "%p", value);
    return IntPut(result, dest, ios, fill, text, count);
}

// The float put: the fill, the integer part with the zeros scaled out of it, the facet's decimal point, the
// zeros scaled out of the fraction, the fraction, the exponent, the trailing zeros, the fill.
//
// FUNC_AT(0x0003b010)
OutIter* GameStd::NumPut::FloatPut(OutIter *result, OutIter dest, IosBase *ios, char fill, const char *text,
                                   uint32_t beforePoint, uint32_t afterPoint, uint32_t trailing, uint32_t count) {
    uint32_t total = beforePoint + afterPoint + trailing + count;
    uint32_t fillCount = ios->width > 0 && uint32_t(ios->width) > total ? ios->width - total : 0;
    int adjust = ios->flags & kAdjustField;
    OutIter out = dest;
    if (adjust == kLeft) {
    } else if (adjust == kInternal) {
        if (count > 0 && (*text == '+' || *text == '-')) {
            out.Assign(*text);
            ++text;
            --count;
        }
        for (; fillCount > 0; fillCount--)
            out.Assign(fill);
    } else {
        for (; fillCount > 0; fillCount--)
            out.Assign(fill);
    }

    const char *point = (const char *)memchr(text, Localeconv()->decimalPoint[0], count);
    if (point != NULL) {
        Locale locale;
        const Numpunct *punct = UseNumpunct(IosBase_GetLoc(ios, 0, &locale));
        ReleaseLocale(locale.impl);
        uint32_t fraction = uint32_t(point - text) + 1;
        for (uint32_t i = 0; i < fraction - 1; i++)
            out.Assign(text[i]);
        for (uint32_t i = 0; i < beforePoint; i++)
            out.Assign('0');
        out.Assign(CallVirtual<char>(const_cast<Numpunct *>(punct), kNumpunctDecimalPoint));
        for (uint32_t i = 0; i < afterPoint; i++)
            out.Assign('0');
        text += fraction;
        count -= fraction;
    }

    point = (const char *)memchr(text, 'e', count);
    if (point != NULL) {
        uint32_t exponent = uint32_t(point - text) + 1;
        for (uint32_t i = 0; i < exponent - 1; i++)
            out.Assign(text[i]);
        for (uint32_t i = 0; i < trailing; i++)
            out.Assign('0');
        trailing = 0;
        out.Assign((ios->flags & kUppercase) ? 'E' : 'e');
        text += exponent;
        count -= exponent;
    }

    for (uint32_t i = 0; i < count; i++)
        out.Assign(text[i]);
    for (uint32_t i = 0; i < trailing; i++)
        out.Assign('0');
    ios->width = 0;
    for (; fillCount > 0; fillCount--)
        out.Assign(fill);
    *result = out;
    return result;
}

// FUNC_AT(0x0003cb00)
OutIter* GameStd::NumPut::DoPutBool(OutIter *result, OutIter dest, IosBase *ios, char fill, bool value) {
    if (!(ios->flags & kBoolAlpha))
        return CallVirtual<OutIter *>(this, kNumPutLong, result, dest, ios, fill, long(uint8_t(value)));

    Locale locale;
    const Numpunct *punct = UseNumpunct(IosBase_GetLoc(ios, 0, &locale));
    ReleaseLocale(locale.impl);
    String name, trueName, falseName;
    if (value) {
        const_cast<Numpunct *>(punct)->TrueName(&trueName);
        String_ConstructCopy(&name, 0, &trueName);
        TidyString(&trueName);
    } else {
        const_cast<Numpunct *>(punct)->FalseName(&falseName);
        String_ConstructCopy(&name, 0, &falseName);
        TidyString(&falseName);
    }

    uint32_t length = name.size;
    uint32_t fillCount = ios->width > 0 && uint32_t(ios->width) > length ? ios->width - length : 0;
    OutIter out = dest;
    if ((ios->flags & kAdjustField) != kLeft) {
        for (; fillCount > 0; fillCount--)
            out.Assign(fill);
    }
    const char *at = name.Data();
    for (uint32_t i = 0; i < length; i++)
        out.Assign(at[i]);
    ios->width = 0;
    for (; fillCount > 0; fillCount--)
        out.Assign(fill);
    *result = out;
    FreeString(&name);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// num_get<char, istreambuf_iterator<char> >

// The integer field: sign, base prefix and digits copied into digits (at most 31), thousands separators checked
// against the locale's grouping. Returns the base for strtoul; an empty digits string means failure.
//
// FUNC_AT(0x0003b810)
int GameStd::NumGet::GetIntField(char *digits, InIter *first, InIter *last, int baseField, const Locale *locale) {
    const Numpunct *punct = UseNumpunct(locale);
    String grouping;
    const_cast<Numpunct *>(punct)->Grouping(&grouping);
    char separator = CallVirtual<char>(const_cast<Numpunct *>(punct), kNumpunctThousandsSep);
    char *at = digits;
    if (!first->Equal(last)) {
        if (first->Get() == '+') {
            *at++ = '+';
            first->Inc();
        } else if (first->Get() == '-') {
            *at++ = '-';
            first->Inc();
        }
    }

    baseField &= kBaseField;
    int base = baseField == kOct ? 8 : baseField == kHex ? 16 : baseField != 0 ? 10 : 0;
    bool seenDigit = false;
    bool nonZero = false;
    if (!first->Equal(last) && first->Get() == '0') {   // a leading zero: octal, or 0x for hex
        seenDigit = true;
        first->Inc();
        if (!first->Equal(last) && (first->Get() == 'x' || first->Get() == 'X') && (base == 0 || base == 16)) {
            base = 16;
            seenDigit = false;
            first->Inc();
        } else if (base == 0) {
            base = 8;
        }
    }
    int digitCount = base == 0 || base == 10 ? 10 : base == 8 ? 8 : 16 + 6;

    String groups;   // the digit count of each group seen, starting with one for the leading zero
    groups.ConstructFill(1, char(seenDigit));
    uint32_t group = 0;
    char *const last31 = &digits[31];
    for (; !first->Equal(last); first->Inc()) {
        char ch = first->Get();
        *at = ch;
        if (memchr("0123456789abcdefABCDEF", ch, digitCount) != NULL) {
            if ((nonZero || *at != '0') && at < last31) {   // leading zeros are dropped
                ++at;
                nonZero = true;
            }
            seenDigit = true;
            if (groups.Data()[group] != kCharMax)
                ++groups.Data()[group];
        } else if (groups.Data()[group] == '\0' || separator == '\0' || first->Get() != separator) {
            break;
        } else {
            groups.AppendFill(1, '\0');
            ++group;
        }
    }

    if (group != 0) {
        if (groups.Data()[group] > 0)
            ++group;
        else
            seenDigit = false;   // a trailing separator
    }
    const char *size = grouping.Data();
    while (seenDigit && group > 0) {   // each group's size against the locale's
        if (*size == kCharMax)
            break;
        if (--group != 0 ? *size != groups.Data()[group] : *size < groups.Data()[group])
            seenDigit = false;
        else if (size[1] > 0)
            ++size;
    }
    if (seenDigit && !nonZero)
        *at++ = '0';   // all zeros: put one back
    else if (!seenDigit)
        at = digits;
    *at = '\0';
    FreeString(&groups);
    FreeString(&grouping);
    return base;
}

// The float field: sign, at most 36 significant digits with the decimal point as the C library writes it, and
// an exponent of at most 8 digits. Returns the power of ten the dropped digits stand for; an empty digits string
// means failure.
//
// FUNC_AT(0x0003c430)
int GameStd::NumGet::GetFloatField(char *digits, InIter *first, InIter *last, const Locale *locale) {
    const Numpunct *punct = UseNumpunct(locale);
    char *at = digits;
    if (!first->Equal(last)) {
        if (first->Get() == '+') {
            *at++ = '+';
            first->Inc();
        } else if (first->Get() == '-') {
            *at++ = '-';
            first->Inc();
        }
    }
    bool seenDigit = false;
    for (; !first->Equal(last) && first->Get() == '0'; first->Inc())
        seenDigit = true;
    if (seenDigit)
        *at++ = '0';

    int significant = 0;
    int exponent = 0;
    for (; !first->Equal(last); first->Inc()) {
        char ch = first->Get();
        *at = ch;
        if (!Isdigit(ch))
            break;
        if (significant < 36) {
            ++at;
            ++significant;
        } else {
            ++exponent;
        }
        seenDigit = true;
    }
    if (!first->Equal(last)) {
        char ch = first->Get();
        if (ch == CallVirtual<char>(const_cast<Numpunct *>(punct), kNumpunctDecimalPoint)) {
            *at++ = Localeconv()->decimalPoint[0];
            first->Inc();
        }
    }
    if (significant == 0) {   // zeros after the point
        for (; !first->Equal(last) && first->Get() == '0'; first->Inc()) {
            --exponent;
            seenDigit = true;
        }
        if (exponent < 0) {
            *at++ = '0';
            ++exponent;
        }
    }
    for (; !first->Equal(last); first->Inc()) {
        char ch = first->Get();
        *at = ch;
        if (!Isdigit(ch))
            break;
        if (significant < 36) {
            ++at;
            ++significant;
        }
        seenDigit = true;
    }

    if (seenDigit && !first->Equal(last) && (first->Get() == 'e' || first->Get() == 'E')) {
        *at++ = 'e';
        first->Inc();
        seenDigit = false;
        significant = 0;
        if (!first->Equal(last)) {
            if (first->Get() == '+') {
                *at++ = '+';
                first->Inc();
            } else if (first->Get() == '-') {
                *at++ = '-';
                first->Inc();
            }
        }
        for (; !first->Equal(last) && first->Get() == '0'; first->Inc())
            seenDigit = true;
        if (seenDigit)
            *at++ = '0';
        for (; !first->Equal(last); first->Inc()) {
            char ch = first->Get();
            *at = ch;
            if (!Isdigit(ch))
                break;
            if (significant < 8) {
                ++at;
                ++significant;
            }
            seenDigit = true;
        }
    }
    if (!seenDigit)
        at = digits;
    *at = '\0';
    return exponent;
}

// _Getloctxt: which of the fields (each starting with fields[0]) the input matches, -1 if none.
//
// FUNC_AT(0x0003d180)
int GameStd::NumGet::GetLocText(InIter *first, InIter *last, uint32_t fieldCount, const char *fields) {
    for (const char *at = fields; *at != '\0'; ++at)
        if (*at == fields[0])
            ++fieldCount;
    String columns;   // per field: the column it was decided at (capped at 127), or 0 while still a candidate
    columns.capacity = 15;
    columns.size = 0;
    columns.text.buffer[0] = '\0';
    if (fieldCount == kNpos)
        String_Xlen(&columns, 0);
    if (String_Grow(&columns, 0, fieldCount, true)) {
        memset(columns.Data(), 0, fieldCount);
        columns.size = fieldCount;
        columns.Data()[fieldCount] = '\0';
    }

    int answer = -2;
    for (uint32_t column = 1;; ) {
        bool prefix = false;
        uint32_t offset = 0;
        for (uint32_t field = 0; field < fieldCount; ++field) {
            for (; fields[offset] != '\0' && fields[offset] != fields[0]; ++offset) {
            }
            char *decided = &columns.Data()[field];
            if (*decided != '\0') {
                offset += *decided;
            } else if (fields[offset += column] == fields[0] || fields[offset] == '\0') {
                *decided = char(column < 127 ? column : 127);   // the whole field matched
                answer = int(field);
            } else if (first->Equal(last) || fields[offset] != first->Get()) {
                *decided = char(column < 127 ? column : 127);
            } else {
                prefix = true;
            }
        }
        if (!prefix || first->Equal(last))
            break;
        ++column;
        first->Inc();
        answer = -1;
    }
    FreeString(&columns);
    return answer;
}

// FUNC_AT(0x0003cdd0)
InIter* GameStd::NumGet::DoGetBool(InIter *result, InIter first, InIter last, IosBase *ios, int *state, bool *value) {
    int answer = -1;
    if (ios->flags & kBoolAlpha) {
        Locale locale;
        const Numpunct *punct = UseNumpunct(IosBase_GetLoc(ios, 0, &locale));
        ReleaseLocale(locale.impl);
        String names;   // "\0" "false" "\0" "true": the two fields, each led by a NUL
        names.capacity = 15;
        names.text.buffer[0] = '\0';
        names.size = 1;
        names.Data()[1] = '\0';
        String name;
        const_cast<Numpunct *>(punct)->FalseName(&name);
        names.AppendSub(&name, 0, kNpos);
        FreeString(&name);
        names.AppendFill(1, '\0');
        const_cast<Numpunct *>(punct)->TrueName(&name);
        names.AppendSub(&name, 0, kNpos);
        FreeString(&name);
        answer = GetLocText(&first, &last, 2, names.Data());
        FreeString(&names);
    } else {
        *Errno() = 0;
        Locale locale;
        IosBase_GetLoc(ios, 0, &locale);
        char digits[32];
        char *end;
        int base = GetIntField(digits, &first, &last, ios->flags, &locale);
        unsigned long read = Stoul(digits, &end, base);
        ReleaseLocale(locale.impl);
        if (end != digits && *Errno() == 0 && read <= 1)
            answer = int(read);
    }
    if (first.Equal(&last))
        *state |= kEofBit;
    if (answer < 0)
        *state |= kFailBit;
    else
        *value = answer != 0;
    *result = first;
    return result;
}

// FUNC_AT(0x0003b630)
InIter* GameStd::NumGet::DoGetUShort(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                     unsigned short *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[32];
    char *end;
    int base = GetIntField(digits, &first, &last, ios->flags, &locale);
    ReleaseLocale(locale.impl);
    char *start = digits[0] == '-' ? digits + 1 : digits;
    unsigned long read = Stoul(start, &end, base);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != start && *Errno() == 0 && read <= 0xffff)
        *value = (unsigned short)(digits[0] == '-' ? 0 - read : read);
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003bd50)
InIter* GameStd::NumGet::DoGetUInt(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                   unsigned *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[32];
    char *end;
    int base = GetIntField(digits, &first, &last, ios->flags, &locale);
    ReleaseLocale(locale.impl);
    char *start = digits[0] == '-' ? digits + 1 : digits;
    unsigned long read = Stoul(start, &end, base);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != start && *Errno() == 0)   // (and read <= UINT_MAX, always so)
        *value = digits[0] == '-' ? 0 - read : read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003be90)
InIter* GameStd::NumGet::DoGetLong(InIter *result, InIter first, InIter last, IosBase *ios, int *state, long *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[32];
    char *end;
    int base = GetIntField(digits, &first, &last, ios->flags, &locale);
    long read = Stol(digits, &end, base);
    ReleaseLocale(locale.impl);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003bfb0)
InIter* GameStd::NumGet::DoGetULong(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                    unsigned long *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[32];
    char *end;
    int base = GetIntField(digits, &first, &last, ios->flags, &locale);
    unsigned long read = Stoul(digits, &end, base);
    ReleaseLocale(locale.impl);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003c0d0)
InIter* GameStd::NumGet::DoGetInt64(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                    int64_t *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[32];
    char *end;
    int base = GetIntField(digits, &first, &last, ios->flags, &locale);
    int64_t read = Stoll(digits, &end, base);
    ReleaseLocale(locale.impl);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003c1f0)
InIter* GameStd::NumGet::DoGetUInt64(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                     uint64_t *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[32];
    char *end;
    int base = GetIntField(digits, &first, &last, ios->flags, &locale);
    uint64_t read = Stoull(digits, &end, base);
    ReleaseLocale(locale.impl);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003c310)
InIter* GameStd::NumGet::DoGetFloat(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                    float *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[60];
    char *end;
    int exponent = GetFloatField(digits, &first, &last, &locale);
    float read = float(Stof(digits, &end, exponent));
    ReleaseLocale(locale.impl);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003c790)
InIter* GameStd::NumGet::DoGetDouble(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                     double *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[60];
    char *end;
    int exponent = GetFloatField(digits, &first, &last, &locale);
    double read = Stod(digits, &end, exponent);
    ReleaseLocale(locale.impl);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003c8b0)
InIter* GameStd::NumGet::DoGetLongDouble(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                         double *value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[60];
    char *end;
    int exponent = GetFloatField(digits, &first, &last, &locale);
    double read = Stof(digits, &end, exponent);   // the same converter as float's (_Stold folded into it)
    ReleaseLocale(locale.impl);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}

// FUNC_AT(0x0003c9d0)
InIter* GameStd::NumGet::DoGetPointer(InIter *result, InIter first, InIter last, IosBase *ios, int *state,
                                      void **value) {
    *Errno() = 0;
    Locale locale;
    IosBase_GetLoc(ios, 0, &locale);
    char digits[32];
    char *end;
    int base = GetIntField(digits, &first, &last, kHex, &locale);
    ReleaseLocale(locale.impl);
    unsigned long read = Stoul(digits, &end, base);
    if (first.Equal(&last))
        *state |= kEofBit;
    if (end != digits && *Errno() == 0)
        *value = (void *)read;
    else
        *state |= kFailBit;
    *result = first;
    return result;
}
