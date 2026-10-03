#ifndef DRIVING_DATA_STDSTREAMS_H_
#define DRIVING_DATA_STDSTREAMS_H_

// The C++ library's string streams and numeric facets as the debug-variable code instantiates them (0x00037ae0 ..
// 0x0003d470, interleaved with DebugVariables.cpp's functions). The game links Dinkumware's library (MSVC 7)
// statically, so every template the debug-variable translation unit uses was compiled into it at its own address:
// std::istrstream and std::ostrstream and the stream classes under them, std::ctype<char>, std::numpunct<char>,
// std::num_get and std::num_put over stream-buffer iterators, and a few std::string members. The tuning loader
// reads every value through them (istrstream(text) >> value) and an indexed variable prints its index through
// them (ostrstream << value), so their output - every digit, every float - is what the game's tuning data turns
// into.
//
// The non-template parts stay the C runtime's, called at their addresses (0x00130xxx..0x00134xxx: ios_base,
// locale, _Lockit, _Locinfo, strstreambuf's own members, _Stod/_Stoul, sprintf ...), as do std::string's members
// compiled elsewhere (0x00012c80..0x00013840) and the getloc/facet helpers just below this range (0x00037930,
// 0x00037980).
//
// The classes here are the game's objects, laid out as Dinkumware lays them out (sizes asserted); their vtables
// are the game's, in the first word. MSVC lists overloaded virtuals in reverse, so num_get's slots run void*, long
// double, double, float, unsigned __int64, __int64, unsigned long, long, unsigned int, unsigned short, bool after
// the destructor, and num_put's void*, long double, double, unsigned __int64, __int64, unsigned long, long, bool.
//
// Exceptions: the library's try/catch blocks (catch (...) { setstate(badbit, true); } around each facet call,
// which rethrows) and use_facet's throw of bad_cast are kept as far as the throw itself - the throw calls the
// runtime as the original does - but our code has no handlers: nothing the tuning data reaches throws (only the
// facets' own allocations could).

#include <stddef.h>
#include <stdint.h>

namespace GameStd {

// Dinkumware's state and format bits (ios_base::iostate / fmtflags), the ones these functions test.
enum IoState : int {
    kGoodBit = 0x0,
    kEofBit = 0x1,
    kFailBit = 0x2,
    kBadBit = 0x4,
};

enum FmtFlags : int {
    kSkipWs = 0x0001,
    kUnitBuf = 0x0002,
    kUppercase = 0x0004,
    kShowBase = 0x0008,
    kShowPoint = 0x0010,
    kShowPos = 0x0020,
    kLeft = 0x0040,
    kRight = 0x0080,
    kInternal = 0x0100,
    kDec = 0x0200,
    kOct = 0x0400,
    kHex = 0x0800,
    kScientific = 0x1000,
    kFixed = 0x2000,
    kBoolAlpha = 0x4000,
    kAdjustField = kLeft | kRight | kInternal,   // 0x01c0
    kBaseField = kDec | kOct | kHex,             // 0x0e00
    kFloatField = kScientific | kFixed,          // 0x3000
};

static const int kEof = -1;   // char_traits<char>::eof()

struct Facet;

// std::locale: a pointer to its implementation, itself a facet.
struct Locale {
    Facet *impl;
};
static_assert(sizeof(Locale) == 4, "Locale");

// std::locale::facet.
struct Facet {
    const void *const *vtable;   // +0x00
    uint32_t refs;               // +0x04
};
static_assert(sizeof(Facet) == 8, "Facet");

// std::_Lockit: the library's lock, taken for a scope (kind 0, the locale lock).
struct Lockit {
    int kind;
};

// std::_Locinfo: the C locale's names and tables, built from a locale name. Only its constructor, destructor and
// _Getcvt are used here; the runtime owns the inside.
struct Locinfo {
    uint8_t opaque[0x74];
};

// std::_Ctypevec, the C runtime's ctype table.
struct CtypeVec {
    uint32_t handle;      // +0x00
    uint32_t page;        // +0x04
    const short *table;   // +0x08
    int deleteTable;      // +0x0c >0: free() it, <0: delete it, 0: not ours
};
static_assert(sizeof(CtypeVec) == 16, "CtypeVec");

// std::_Cvtvec, the code page conversion info _Getcvt returns (in EDX:EAX).
struct CvtVec {
    uint32_t handle;
    uint32_t page;
};

// std::string (basic_string<char> with the game's allocator, which is UMemory): a 16-byte small buffer, used while
// the capacity is under 16.
struct String {
    uint8_t allocator;   // +0x00 empty allocator object
    uint8_t pad01[3];
    union {
        char buffer[16];
        char *pointer;
    } text;              // +0x04
    uint32_t size;       // +0x14
    uint32_t capacity;   // +0x18
    char *Data() { return capacity < 16 ? text.buffer : text.pointer; }

    String* ConstructFill(uint32_t count, char ch);                       // 0x0003bcb0 basic_string(count, ch)
    String* AssignFill(uint32_t count, char ch);                          // 0x0003bce0 assign(count, ch)
    String* AppendFill(uint32_t count, char ch);                          // 0x0003bc20 append(count, ch)
    String* AppendSub(const String *right, uint32_t offset, uint32_t count);   // 0x0003d090 append(right, offset, count)
};
static_assert(sizeof(String) == 0x1c, "String");

// std::fpos<_Mbstatet>, as streambuf's seek functions return it (through a hidden pointer).
struct StreamPos {
    int32_t offset;       // +0x00
    uint32_t pad04;
    uint32_t position[2]; // +0x08 fpos_t (an __int64, kept as two words so the struct stays 4-aligned)
    int32_t state;        // +0x10
    uint32_t pad14;
};
static_assert(sizeof(StreamPos) == 0x18, "StreamPos");

struct BasicIos;

// std::basic_streambuf<char>: the get and put areas, each reached through a pointer the derived buffer may
// redirect.
struct Streambuf {
    const void *const *vtable;   // +0x00
    uint32_t mutex;              // +0x04 std::_Mutex
    char *getFirstStore;         // +0x08
    char *putFirstStore;         // +0x0c
    char **getFirst;             // +0x10 -> getFirstStore
    char **putFirst;             // +0x14 -> putFirstStore
    char *getNextStore;          // +0x18
    char *putNextStore;          // +0x1c
    char **getNext;              // +0x20 -> getNextStore
    char **putNext;              // +0x24 -> putNextStore
    int getCountStore;           // +0x28
    int putCountStore;           // +0x2c
    int *getCount;               // +0x30 -> getCountStore
    int *putCount;               // +0x34 -> putCountStore
    Locale *locale;              // +0x38

    Streambuf* Construct();                                            // 0x000387f0
    void Destruct();                                                   // 0x00038210
    Streambuf* Delete(unsigned flags);                                 // 0x00038520
    void InitPointers();                                               // 0x00038590 _Init()
    Streambuf* SetBuf(char *buffer, int count);                        // 0x00038280 setbuf: does nothing
    int Overflow(int ch);                                              // 0x00038290 also pbackfail: eof
    int Underflow();                                                   // 0x000382a0
    int Uflow();                                                       // 0x000382b0
    int Xsgetn(char *out, int count);                                  // 0x000382e0
    int Xsputn(const char *text, int count);                           // 0x00038380
    StreamPos* SeekOff(StreamPos *result, int offset, int way, int mode);   // 0x00038430
    StreamPos* SeekPos(StreamPos *result, StreamPos position, int mode);    // 0x00038460
    int Snextc();                                                      // 0x00038860
    int Sputc(char ch);                                                // 0x00039900
    char* Pninc();                                                     // 0x00039940 _Pninc()
};
static_assert(sizeof(Streambuf) == 0x3c, "Streambuf");

// std::strstreambuf.
struct StrStreambuf : Streambuf {
    int mode;           // +0x3c _Strmode
    char *pendSave;     // +0x40
    char *seekHigh;     // +0x44
    int allocSize;      // +0x48
    void *allocFn;      // +0x4c
    void *freeFn;       // +0x50

    StrStreambuf* Construct(const char *text, int count);              // 0x00038af0 strstreambuf(const char *, n)
    StrStreambuf* Delete(unsigned flags);                              // 0x00038b50
};
static_assert(sizeof(StrStreambuf) == 0x54, "StrStreambuf");

struct Ostream;

// std::ios_base.
struct IosBase {
    const void *const *vtable;   // +0x00
    int state;                   // +0x04 _Mystate
    int exceptions;              // +0x08
    int flags;                   // +0x0c
    int precision;               // +0x10
    int width;                   // +0x14
    void *words;                 // +0x18 _Arr
    void *callbacks;             // +0x1c _Calls
    Locale *locale;              // +0x20
    uint32_t standardStream;     // +0x24 _Stdstr
};
static_assert(sizeof(IosBase) == 0x28, "IosBase");

// std::basic_ios<char>. The stream classes hold it as a virtual base, found through their vbtable. The destructors
// of classes with that virtual base are entered with this at the virtual base, so they are methods here.
struct BasicIos : IosBase {
    Streambuf *streambuf;   // +0x28
    Ostream *tie;           // +0x2c
    char fill;              // +0x30
    uint8_t pad31[3];

    void Destruct();                                                   // 0x00037ae0 ~basic_ios
    BasicIos* Delete(unsigned flags);                                  // 0x00037b30
    void Init(Streambuf *buffer, bool isStandard);                     // 0x00038540 init()
    void DestructIstream();                                            // 0x00037af0 ~basic_istream
    void* DeleteIstream(unsigned flags);                               // 0x000384f0
    void* DeleteIStrStream(unsigned flags);                            // 0x00038c40
};
static_assert(sizeof(BasicIos) == 0x34, "BasicIos");

// std::basic_istream<char>: vbptr, then the count of the last unformatted read; basic_ios is its virtual base.
struct Istream {
    const int *vbtable;   // +0x00 [1]: offset of the basic_ios
    int count;            // +0x04 _Chcount
    BasicIos *Ios() { return (BasicIos *)((char *)this + vbtable[1]); }

    bool Ipfx(bool noSkip);                                            // 0x000388c0 _Ipfx
    Istream* ExtractBool(bool *value);                                 // 0x00038fa0 operator>>(bool &)
    Istream* ExtractInt(int *value);                                   // 0x00039120 operator>>(int &)
    Istream* ExtractUInt(unsigned *value);                             // 0x000392d0 operator>>(unsigned int &)
    Istream* ExtractFloat(float *value);                               // 0x00039450 operator>>(float &)
};
static_assert(sizeof(Istream) == 8, "Istream");

// std::basic_ostream<char>: vbptr only.
struct Ostream {
    const int *vbtable;   // +0x00
    BasicIos *Ios() { return (BasicIos *)((char *)this + vbtable[1]); }

    Ostream* Flush();                                                  // 0x00038790 flush()
    Ostream* Put(char ch);                                             // 0x00039810 put()
    Ostream* InsertUInt(unsigned value);                               // 0x00039a90 operator<<(unsigned int)
};
static_assert(sizeof(Ostream) == 4, "Ostream");

// std::istrstream: the istream, its strstreambuf, then the basic_ios.
struct IStrStream : Istream {
    StrStreambuf buffer;   // +0x08
    BasicIos ios;          // +0x5c

    IStrStream* Construct(const char *text, int constructVirtualBase); // 0x00038b70 istrstream(const char *)
    void DestructAll();                                                // 0x000380c0 the `vbase destructor'
};
static_assert(sizeof(IStrStream) == 0x90, "IStrStream");

// std::ostrstream: the ostream, its strstreambuf, then the basic_ios.
struct OStrStream : Ostream {
    StrStreambuf buffer;   // +0x04
    BasicIos ios;          // +0x58

    void DestructAll();                                                // 0x000397f0 the `vbase destructor'
};
static_assert(sizeof(OStrStream) == 0x8c, "OStrStream");

// std::basic_istream<char>::sentry: the stream (its buffer locked while the sentry lives) and whether it is good.
struct IstreamSentry {
    Istream *stream;
    bool ok;
    uint8_t pad05[3];

    IstreamSentry* Construct(Istream *stream, bool noSkip);           // 0x00038c70
};

// std::basic_ostream<char>::sentry, over its _Sentry_base.
struct OstreamSentry {
    Ostream *stream;
    bool ok;
    uint8_t pad05[3];

    OstreamSentry* ConstructBase(Ostream *stream);                    // 0x000399f0 _Sentry_base(ostream &)
    void DestructBase();                                               // 0x000399d0 ~_Sentry_base
    OstreamSentry* Construct(Ostream *stream);                        // 0x00039950
    void Destruct();                                                   // 0x00039a20
};

// std::istreambuf_iterator<char>: the buffer (null at end of stream), and the character peeked, once peeked.
struct IstreambufIterator {
    Streambuf *streambuf;   // +0x00
    bool got;               // +0x04
    char value;             // +0x05
    uint8_t pad06[2];

    bool Equal(const IstreambufIterator *right);                       // 0x0003b770 equal()
    char Peek();                                                       // 0x0003b7c0 _Peek()
    void Inc();                                                        // 0x0003bbd0 _Inc()
    char Get() {   // operator*, inlined everywhere
        if (!got)
            Peek();
        return value;
    }
};
static_assert(sizeof(IstreambufIterator) == 8, "IstreambufIterator");

// std::ostreambuf_iterator<char>.
struct OstreambufIterator {
    bool failed;            // +0x00
    uint8_t pad01[3];
    Streambuf *streambuf;   // +0x04

    OstreambufIterator* Assign(char ch);                               // 0x0003a900 operator=(char)
};
static_assert(sizeof(OstreambufIterator) == 8, "OstreambufIterator");

// std::ctype<char>.
struct Ctype : Facet {
    CtypeVec ctype;   // +0x08

    Ctype* Construct(const short *table, bool deleteTable, uint32_t refs);   // 0x00037df0
    void Destruct();                                                   // 0x00038080
    Ctype* Delete(unsigned flags);                                     // 0x00038060
    char DoToLower(char ch);                                           // 0x00037ed0
    const char* DoToLowerRange(char *first, const char *last);         // 0x00037ef0
    char DoToUpper(char ch);                                           // 0x00037f20
    const char* DoToUpperRange(char *first, const char *last);         // 0x00037f40
    char DoWiden(char ch);                                             // 0x00037f70
    const char* DoWidenRange(const char *first, const char *last, char *dest);   // 0x00037f80
    char DoNarrow(char ch, char unused);                               // 0x00037fb0
    const char* DoNarrowRange(const char *first, const char *last, char unused, char *dest);   // 0x00037fc0
    static uint32_t GetCat(const Facet **cache);                       // 0x00037ff0 _Getcat
};
static_assert(sizeof(Ctype) == 0x18, "Ctype");

// std::numpunct<char>.
struct Numpunct : Facet {
    const char *grouping;    // +0x08
    char decimalPoint;       // +0x0c
    char thousandsSep;       // +0x0d
    uint8_t pad0e[2];
    const char *falseName;   // +0x10
    const char *trueName;    // +0x14

    Numpunct* Delete(unsigned flags);                                  // 0x0003ab60
    void Destruct();                                                   // 0x0003ab80
    void Tidy();                                                       // 0x0003abd0
    void Init(const Locinfo *info);                                    // 0x0003ac00 _Init
    char DoDecimalPoint();                                             // 0x0003ab40
    char DoThousandsSep();                                             // 0x0003ab50
    String* DoGrouping(String *result);                                // 0x0003d3b0
    String* DoFalseName(String *result);                               // 0x0003d3d0
    String* DoTrueName(String *result);                                // 0x0003d420
    String* Grouping(String *result);                                  // 0x0003a960 grouping()
    String* FalseName(String *result);                                 // 0x0003cd90 falsename()
    String* TrueName(String *result);                                  // 0x0003cdb0 truename()
    static uint32_t GetCat(const Facet **cache);                       // 0x0003aa90
    static char* MakeLocString(const char *text, char *unused, const CvtVec *cvt);   // 0x0003acd0 _Maklocstr
};
static_assert(sizeof(Numpunct) == 0x18, "Numpunct");

typedef IstreambufIterator InIter;
typedef OstreambufIterator OutIter;

// std::num_get<char, istreambuf_iterator<char> >.
struct NumGet : Facet {
    NumGet* Construct(uint32_t refs);                                  // 0x00038700
    NumGet* Delete(unsigned flags);                                    // 0x00038770 (num_put's too)
    static uint32_t GetCat(const Facet **cache);                       // 0x00038a80

    InIter* DoGetBool(InIter *result, InIter first, InIter last, IosBase *ios, int *state, bool *value);              // 0x0003cdd0
    InIter* DoGetUShort(InIter *result, InIter first, InIter last, IosBase *ios, int *state, unsigned short *value);  // 0x0003b630
    InIter* DoGetUInt(InIter *result, InIter first, InIter last, IosBase *ios, int *state, unsigned *value);          // 0x0003bd50
    InIter* DoGetLong(InIter *result, InIter first, InIter last, IosBase *ios, int *state, long *value);              // 0x0003be90
    InIter* DoGetULong(InIter *result, InIter first, InIter last, IosBase *ios, int *state, unsigned long *value);    // 0x0003bfb0
    InIter* DoGetInt64(InIter *result, InIter first, InIter last, IosBase *ios, int *state, int64_t *value);          // 0x0003c0d0
    InIter* DoGetUInt64(InIter *result, InIter first, InIter last, IosBase *ios, int *state, uint64_t *value);        // 0x0003c1f0
    InIter* DoGetFloat(InIter *result, InIter first, InIter last, IosBase *ios, int *state, float *value);            // 0x0003c310
    InIter* DoGetDouble(InIter *result, InIter first, InIter last, IosBase *ios, int *state, double *value);          // 0x0003c790
    InIter* DoGetLongDouble(InIter *result, InIter first, InIter last, IosBase *ios, int *state, double *value);      // 0x0003c8b0
    InIter* DoGetPointer(InIter *result, InIter first, InIter last, IosBase *ios, int *state, void **value);          // 0x0003c9d0

    static int GetIntField(char *digits, InIter *first, InIter *last, int baseField, const Locale *locale);   // 0x0003b810 _Getifld
    static int GetFloatField(char *digits, InIter *first, InIter *last, const Locale *locale);               // 0x0003c430 _Getffld
    static int GetLocText(InIter *first, InIter *last, uint32_t fieldCount, const char *fields);             // 0x0003d180 _Getloctxt
};
static_assert(sizeof(NumGet) == 8, "NumGet");

// std::num_put<char, ostreambuf_iterator<char> >.
struct NumPut : Facet {
    static uint32_t GetCat(const Facet **cache);                       // 0x00039d10

    OutIter* DoPutBool(OutIter *result, OutIter dest, IosBase *ios, char fill, bool value);                 // 0x0003cb00
    OutIter* DoPutLong(OutIter *result, OutIter dest, IosBase *ios, char fill, long value);                 // 0x0003a440
    OutIter* DoPutULong(OutIter *result, OutIter dest, IosBase *ios, char fill, unsigned long value);       // 0x0003ad10
    OutIter* DoPutInt64(OutIter *result, OutIter dest, IosBase *ios, char fill, int64_t value);             // 0x0003ad70
    OutIter* DoPutUInt64(OutIter *result, OutIter dest, IosBase *ios, char fill, uint64_t value);           // 0x0003add0
    OutIter* DoPutDouble(OutIter *result, OutIter dest, IosBase *ios, char fill, double value);             // 0x0003ae30
    OutIter* DoPutLongDouble(OutIter *result, OutIter dest, IosBase *ios, char fill, double value);         // 0x0003b420
    OutIter* DoPutPointer(OutIter *result, OutIter dest, IosBase *ios, char fill, const void *value);       // 0x0003b5e0

    static char* IntFormat(char *format, const char *spec, int flags);                                      // 0x0003a4a0 _Ifmt
    static char* FloatFormat(char *format, char spec, int flags);                                           // 0x0003afb0 _Ffmt
    static OutIter* IntPut(OutIter *result, OutIter dest, IosBase *ios, char fill, char *text, uint32_t count);   // 0x0003a530 _Iput
    static OutIter* FloatPut(OutIter *result, OutIter dest, IosBase *ios, char fill, const char *text,
                             uint32_t beforePoint, uint32_t afterPoint, uint32_t trailing, uint32_t count);  // 0x0003b010 _Fput
};
static_assert(sizeof(NumPut) == 8, "NumPut");

// use_facet<> for the four facets (by locale reference).
const Ctype* UseCtype(const Locale *locale);                           // 0x000385f0
const NumGet* UseNumGet(const Locale *locale);                         // 0x00038e00
const NumPut* UseNumPut(const Locale *locale);                         // 0x00039c00
const Numpunct* UseNumpunct(const Locale *locale);                     // 0x0003a980

// operator>>(istream &, char &).
Istream* ExtractChar(Istream *stream, char *value);                    // 0x00038ce0

} // namespace GameStd

#endif // DRIVING_DATA_STDSTREAMS_H_
