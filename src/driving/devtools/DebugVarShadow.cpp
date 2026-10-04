#include "DebugVarShadow.h"

#include "../data/CoordConvert.h"
#include "../data/DebugVariables.h"
#include "../data/IniFiles.h"
#include "../data/StdStreams.h"
#include "../data/Tuning.h"
#include "../platform/FileSys.h"
#include "../platform/RealMemory.h"
#include "../platform/RefPack.h"
#include "../engine/UFileLoader.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_DBVARSHADOW=1: data/DebugVariables.cpp, Tuning.cpp, IniFiles.cpp, StdStreams.cpp and CoordConvert.cpp
// against the originals.
//
// The whole suite runs twice on the same inputs and the same static objects (so pointers into them agree): once
// with every original swapped back in (common/xbeOriginal.h) and once with our jumps, every call made through the
// original's address either way, so the second run goes through the patches as the game does and calls between
// the functions follow each side's own code. Each case logs what it produced - values, stream states, output
// buffers filled with a pattern first, the objects built (never heap pointers, which differ run to run) - and the
// two logs are compared line by line.
//
// - The parsers dbattrib_ uses (istrstream >> char, int, unsigned, float, bool) on fixed and random texts.
// - Every num_put do_put (long, unsigned long, __int64, unsigned __int64, double, long double, void*, bool) over
//   flag, precision, width and fill combinations, into ostrstreams big and too narrowValue; every num_get do_get, and
//   _Getifld, _Getffld and _Getloctxt, over texts and base and boolalpha flags.
// - The streams, iterators, ctype<char>, numpunct<char> and std::string members directly.
// - DebugVariable<unsigned int>: Increase/Decrease over ranges, steps, scales and values, SetToMin/Max, Debounce,
//   AsString with and without names, SetFromString, GetValuePtr/GetDeindexedPtr through an index.
// - dbattrib_ of every type, plain and through dbindex, on every key of every tuning file on the disc (read from
//   the .viv archives on the host, so no mount is needed) and on a synthetic file.
// - With a mission archive mounted: DTuningDBMgr::LoadDatabase/CloseCurrent for every database the game opens and
//   every level, DTuningFile directly, and IniFiles on data/render/camera.ini (on the host copy otherwise).
// - ParseData, ParseData_Colour, EncryptDecrypt, Float_COORD3toCOORD4/4toCOORD3, the singleton.
//
// Catches, for instance, an off-by-one in _Getffld's 36-significant-digit limit (the long digit strings), a wrong
// case for hex digits in _Ifmt (uppercase hex puts), or a missed wrap in Increase (the max-1 values).
// ---------------------------------------------------------------------------------------------------------------

using namespace GameStd;

namespace {   // this file's own types: another test's of the same name must not merge with them

#define ArchiveName ((char *)0x002431d8)          // UFileLoader's mission archive name (kept after closing)
#define ArchiveOpen (*(uint8_t *)0x002434da)
#define ArchiveLogRequests (*(uint8_t *)0x002434e0)
#define ShadowCurrentIndexer (*(DebugUIntVariable **)0x001e22c4)
#define ShadowBuiltinNew ((void *(__cdecl *)(size_t))0x001146a0)
#define ShadowBuiltinDelete ((void (__cdecl *)(void *))0x001146e0)
#define ShadowOStrStream_Construct ((OStrStream *(__fastcall *)(OStrStream *, int, char *, int, int, int))0x00131b27)
#define ShadowOStrStream_Destruct ((void (__fastcall *)(BasicIos *, int))0x00131bbb)
#define ShadowIosBase_GetLoc ((Locale *(__fastcall *)(IosBase *, int, Locale *))0x00037980)
#define ShadowString_Tidy ((void (__fastcall *)(String *, int, bool))0x00013110)
#define ShadowDAFI_open ((DAFI *(__cdecl *)(const char *, int))0x0011a240)

static std::string *g_log;
static uint32_t g_seed;
static int g_cases;

static void Logf(const char *format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

static void LogBytes(const char *label, const void *data, size_t count) {
    std::string line = label;
    line += ' ';
    char hex[4];
    for (size_t i = 0; i < count; i++) {
        snprintf(hex, sizeof(hex), "%02x", ((const uint8_t *)data)[i]);
        line += hex;
    }
    g_log->append(line);
    g_log->push_back('\n');
}

static uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

typedef void (*ThunkFn)(void *);

static bool Guarded(ThunkFn fn, void *context) {
#ifdef _MSC_VER
    __try {
        fn(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    fn(context);
    return true;
#endif
}

template <class F> static void ThunkOf(void *context) {
    (*(F *)context)();
}

// One case: the call, logged by the lambda; a fault is logged instead.
// Each case starts with a "#kind" line, which the compare counts differences under.
template <class F> static void Run(const char *what, int index, F f) {
    g_cases++;
    Logf("#%s", what);
    if (!Guarded(&ThunkOf<F>, &f))
        Logf("%s %d: FAULT", what, index);
}

template <class F> static F At(unsigned address) {
    return XbeOriginal<F>(address);
}

static int SwapOriginals(bool original) {
    return XbeOriginal_RestoreRange(0x00037ad0, 0x0003da90, original) +
           XbeOriginal_RestoreRange(0x00059630, 0x00059640, original) +
           XbeOriginal_RestoreRange(0x0005c9f0, 0x0005ca80, original) +
           XbeOriginal_RestoreRange(0x000e4030, 0x000e4290, original) +
           XbeOriginal_RestoreRange(0x0011c5c0, 0x0011c5d0, original);
}

// ---- inputs

static const char *const kTexts[] = {
    "", "0", "1", "-1", "+1", "  42", "\t\n7", "42  ", "0x1F", "0X1f", "017", "08", "4294967295", "4294967296",
    "-4294967295", "2147483647", "2147483648", "-2147483648", "-2147483649", "99999999999999999999", "3.14159",
    "-0.0", "1e10", "1E-5", "1e", "1e+", "1e-", ".5", "5.", ".", "-.", "1.5e-45", "1e-50", "3.4028235e38",
    "3.5e38", "1e400", "-1e400", "nan", "inf", "abc", "12abc", "1,234", "1 234", "true", "false", "t", "f",
    "00000000000000000000000000000000000001", "0.000000000000000000000000000000000000000001234",
    "123456789012345678901234567890123456789012345", "1.23456789012345678901234567890123456789e5",
    "0.00000000000000000000000000000000000000000000000000000000000000001", "1e123456789", "1e-123456789", "-", "+",
    "x", "0x", "-0x10", "\xff", "\x80" "5", " ", "2 3", "0.1", "0.2", "0.3", "1.1754943e-38", "1.4e-45",
    "16777217", "-16777217", "0.1e1", "1e0005", "1e000000005", "123e-2", "00.0001", "-00", "+0x", "0xffffffff",
    "0x100000000", "037777777777", "040000000000", "1.0,2.0,3.0,4.0", "255,128,64,32", "0.5,0.25" };
static const int kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);

static std::vector<std::string> MakeTexts() {
    std::vector<std::string> texts(kTexts, kTexts + kTextCount);
    static const char alphabet[] = "0123456789000111+-.eExX ab,\t";
    for (int i = 0; i < 300; i++) {
        std::string text;
        int length = Next() % 14;
        for (int j = 0; j < length; j++)
            text.push_back(alphabet[Next() % (sizeof(alphabet) - 1)]);
        texts.push_back(text);
    }
    return texts;
}

static double RandomDouble() {
    static const uint64_t specials[] = {
        0x0000000000000000ull, 0x8000000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull,
        0x3fb999999999999aull, 0x3fd5555555555555ull, 0x7ff0000000000000ull, 0xfff0000000000000ull,
        0x7ff8000000000000ull, 0x0000000000000001ull, 0x000fffffffffffffull, 0x7fefffffffffffffull,
        0x4732426172c74d82ull, 0x4733426172c74d82ull, 0x38aa95a5b7f87a0full, 0x38a995a5b7f87a0full,
        0x54b249ad2594c37dull, 0x2b2bff2ee48e0530ull, 0x40fe240c9fbe76c9ull, 0xc0fe240c9fbe76c9ull };
    uint32_t r = Next() % 100;
    if (r < 30)
        return *(const double *)&specials[Next() % (sizeof(specials) / sizeof(specials[0]))];
    if (r < 60) {
        uint64_t bits = (uint64_t(Next()) << 32) | Next();
        double d;
        memcpy(&d, &bits, 8);
        return d;
    }
    double m = double(Next()) / 4294967296.0 * 10.0;
    int e = int(Next() % 90) - 45;
    double v = m * pow(10.0, e);
    return (Next() & 1) ? -v : v;
}

// ---- the streams the facet tests read and write

static IStrStream g_in;
static char g_inText[512];
static OStrStream g_out;
static char g_outText[512];
static NumGet g_numGet;
static NumPut g_numPut;

static void OpenIn(const char *text) {
    strncpy(g_inText, text, sizeof(g_inText) - 1);
    (g_in.*At<IStrStream *(IStrStream::*)(const char *, int)>(0x00038b70))(g_inText, 1);
}

static int g_closes;

// The `vbase destructor', or (every other time) the deleting destructor without the delete.
static void CloseIn() {
    if (g_closes++ & 1)
        (g_in.ios.*At<void *(BasicIos::*)(unsigned)>(0x00038c40))(0);
    else
        (g_in.*At<void (IStrStream::*)()>(0x000380c0))();
}

static void OpenOut(int size) {
    memset(g_outText, 0xaa, sizeof(g_outText));
    ShadowOStrStream_Construct(&g_out, 0, g_outText, size, 2, 1);
}

static void CloseOut() {
    if (g_closes++ & 1) {
        ShadowOStrStream_Destruct(&g_out.ios, 0);
        (g_out.ios.*At<BasicIos *(BasicIos::*)(unsigned)>(0x00037b30))(0);
    } else {
        (g_out.*At<void (OStrStream::*)()>(0x000397f0))();
    }
}

// An object with the heap pointers it holds blanked (they differ run to run).
// (A streambuf's mutex and locale, a basic_ios's locale.)
static void LogStreamObject(const char *label, const void *object, size_t size, size_t heap1, size_t heap2,
                            size_t heap3) {
    uint8_t copy[0x100];
    memcpy(copy, object, size);
    memset(copy + heap1, 0, 4);
    memset(copy + heap2, 0, 4);
    memset(copy + heap3, 0, 4);
    LogBytes(label, copy, size);
}

// ---------------------------------------------------------------------------------------------------------------
// The parsers

static void ParserCases(const std::vector<std::string> &texts) {
    for (int i = 0; i <= (int)texts.size(); i++) {
        const char *text = i < (int)texts.size() ? texts[i].c_str() : NULL;
        Run("StringToChar", i, [&] {
            char value = char(0xa5);
            ((void (*)(const char *, char *))0x00038f10)(text, &value);
            Logf("StringToChar %d: %02x", i, uint8_t(value));
        });
        Run("StringToInt", i, [&] {
            int value = int(0xa5a5a5a5);
            ((void (*)(const char *, int *))0x00039db0)(text, &value);
            Logf("StringToInt %d: %08x", i, value);
        });
        Run("StringToUInt", i, [&] {
            unsigned value = 0xa5a5a5a5;
            ((void (*)(const char *, unsigned *))0x00039e40)(text, &value);
            Logf("StringToUInt %d: %08x", i, value);
        });
        Run("StringToFloat", i, [&] {
            uint32_t value = 0xa5a5a5a5;
            ((void (*)(const char *, uint32_t *))0x00039ed0)(text, &value);
            Logf("StringToFloat %d: %08x", i, value);
        });
        Run("StringToBool", i, [&] {
            uint8_t value = 0xa5;
            ((void (*)(const char *, uint8_t *))0x00039f60)(text, &value);
            Logf("StringToBool %d: %02x", i, value);
        });
        if (text == NULL)
            continue;
        Run("ExtractChar/Ipfx", i, [&] {   // the stream itself after an extraction
            OpenIn(text);
            char value = 0;
            ((Istream *(*)(Istream *, char *))0x00038ce0)(&g_in, &value);
            ((Istream *(*)(Istream *, char *))0x00038ce0)(&g_in, &value);
            Logf("ExtractChar %d: %02x state %x consumed %d", i, uint8_t(value), g_in.ios.state,
                 int(*g_in.buffer.getNext - g_inText));
            LogStreamObject("istrstream", &g_in, sizeof(g_in), 8 + 4, 8 + 0x38, 0x5c + 0x20);
            CloseIn();
        });
    }
}

// ---------------------------------------------------------------------------------------------------------------
// num_put

static const int kPutFlags[] = {
    0, kDec, kOct, kHex, kHex | kShowBase, kHex | kUppercase | kShowBase, kOct | kShowBase, kShowPos,
    kShowPos | kHex, kLeft, kRight, kInternal, kInternal | kShowPos, kInternal | kHex | kShowBase, kFixed,
    kScientific, kFixed | kScientific, kFixed | kShowPos, kScientific | kUppercase, kShowPoint,
    kShowPoint | kFixed, kBoolAlpha, kBoolAlpha | kLeft, kBoolAlpha | kInternal, kFixed | kInternal | kShowPos,
    kScientific | kLeft | kShowPoint };
static const int kPutFlagCount = sizeof(kPutFlags) / sizeof(kPutFlags[0]);
static const int kPrecisions[] = { -1, 0, 1, 2, 6, 9, 10, 17, 20, 36, 40, 60 };
static const int kWidths[] = { 0, 1, 5, 12, 30 };

template <class T>
static void PutValue(unsigned address, const char *type, int index, T value, int flags, int precision, int width,
                     char fill, int size) {
    typedef OutIter *(NumPut::*Method)(OutIter *, OutIter, IosBase *, char, T);
    Run(type, index, [&] {
        OpenOut(size);
        g_out.ios.flags = flags;
        g_out.ios.precision = precision;
        g_out.ios.width = width;
        OutIter dest = { false, {}, &g_out.buffer };
        OutIter result = { true, {}, NULL };
        (g_numPut.*At<Method>(address))(&result, dest, &g_out.ios, fill, value);
        int written = int(*g_out.buffer.putNext - g_outText);
        Logf("%s %d: failed %d buffer %d width %d state %x written %d", type, index, result.failed,
             result.streambuf == &g_out.buffer, g_out.ios.width, g_out.ios.state, written);
        int shown = size + 4 < 512 ? size + 4 : 512;
        LogBytes("out", g_outText, shown);
        CloseOut();
    });
}

static void PutCases() {
    int index = 0;
    for (int i = 0; i < 1500; i++) {
        int flags = kPutFlags[Next() % kPutFlagCount];
        int precision = kPrecisions[Next() % (sizeof(kPrecisions) / sizeof(kPrecisions[0]))];
        int width = kWidths[Next() % (sizeof(kWidths) / sizeof(kWidths[0]))];
        char fill = (Next() & 1) ? ' ' : '*';
        int size = (Next() % 8) == 0 ? int(1 + Next() % 6) : 160;   // sometimes too small: overflow
        uint32_t r = Next();
        int64_t big = (int64_t(Next()) << 32) | Next();
        static const uint32_t ints[] = { 0, 1, 9, 10, 0xffffffff, 0x80000000, 0x7fffffff, 1000, 123456789 };
        uint32_t narrowValue = (r % 3 == 0) ? ints[Next() % (sizeof(ints) / sizeof(ints[0]))] : Next() >> (Next() % 32);
        switch (i % 8) {
        case 0: PutValue<long>(0x0003a440, "DoPutLong", index++, long(narrowValue), flags, precision, width, fill, size); break;
        case 1: PutValue<unsigned long>(0x0003ad10, "DoPutULong", index++, narrowValue, flags, precision, width, fill, size); break;
        case 2: PutValue<int64_t>(0x0003ad70, "DoPutInt64", index++, (r & 1) ? big : int64_t(int32_t(narrowValue)), flags, precision, width, fill, size); break;
        case 3: PutValue<uint64_t>(0x0003add0, "DoPutUInt64", index++, uint64_t(big), flags, precision, width, fill, size); break;
        case 4: PutValue<double>(0x0003ae30, "DoPutDouble", index++, RandomDouble(), flags, precision, width, fill, size); break;
        case 5: PutValue<double>(0x0003b420, "DoPutLongDouble", index++, RandomDouble(), flags, precision, width, fill, size); break;
        case 6: PutValue<const void *>(0x0003b5e0, "DoPutPointer", index++, (const void *)(uintptr_t)narrowValue, flags, precision, width, fill, size); break;
        case 7: PutValue<bool>(0x0003cb00, "DoPutBool", index++, (narrowValue & 1) != 0, flags, precision, width, fill, size); break;
        }
    }
    // _Ifmt and _Ffmt alone, every flag combination
    for (int flags = 0; flags < 0x8000; flags += 0x10 + (Next() & 0x0f)) {
        Run("IntFormat", flags, [&] {
            char format[16];
            memset(format, 0xaa, sizeof(format));
            static const char *const specs[] = { "ld", "lu", "Ld", "Lu" };
            for (int s = 0; s < 4; s++) {
                ((char *(*)(char *, const char *, int))0x0003a4a0)(format, specs[s], flags);
                LogBytes("IntFormat", format, sizeof(format));
            }
            memset(format, 0xaa, sizeof(format));
            ((char *(*)(char *, char, int))0x0003afb0)(format, (flags & 0x100) ? 'L' : '\0', flags);
            LogBytes("FloatFormat", format, sizeof(format));
        });
    }
}

// ---------------------------------------------------------------------------------------------------------------
// num_get

template <class T> static void GetValue(unsigned address, const char *type, int index, const char *text, int flags) {
    typedef InIter *(NumGet::*Method)(InIter *, InIter, InIter, IosBase *, int *, T *);
    Run(type, index, [&] {
        OpenIn(text);
        g_in.ios.flags = flags;
        InIter first = { &g_in.buffer, false, 0 };
        InIter last = { NULL, true, 0 };
        int state = 0x100;
        T value;
        memset(&value, 0xa5, sizeof(value));
        InIter result;
        memset(&result, 0, sizeof(result));
        (g_numGet.*At<Method>(address))(&result, first, last, &g_in.ios, &state, &value);
        Logf("%s %d: state %x end %d got %d value %02x consumed %d", type, index, state, result.streambuf == NULL,
             result.got, uint8_t(result.value), int(*g_in.buffer.getNext - g_inText));
        LogBytes("value", &value, sizeof(value));
        CloseIn();
    });
}

static void GetCases(const std::vector<std::string> &texts) {
    static const int flagSet[] = { kDec, 0, kOct, kHex, kBoolAlpha, kHex | kBoolAlpha, kOct | kHex };
    int index = 0;
    for (int t = 0; t < (int)texts.size(); t++) {
        const char *text = texts[t].c_str();
        int flags = flagSet[t % (sizeof(flagSet) / sizeof(flagSet[0]))];
        GetValue<bool>(0x0003cdd0, "DoGetBool", index, text, flags);
        GetValue<unsigned short>(0x0003b630, "DoGetUShort", index, text, flags);
        GetValue<unsigned>(0x0003bd50, "DoGetUInt", index, text, flags);
        GetValue<long>(0x0003be90, "DoGetLong", index, text, flags);
        GetValue<unsigned long>(0x0003bfb0, "DoGetULong", index, text, flags);
        GetValue<int64_t>(0x0003c0d0, "DoGetInt64", index, text, flags);
        GetValue<uint64_t>(0x0003c1f0, "DoGetUInt64", index, text, flags);
        GetValue<float>(0x0003c310, "DoGetFloat", index, text, flags);
        GetValue<double>(0x0003c790, "DoGetDouble", index, text, flags);
        GetValue<double>(0x0003c8b0, "DoGetLongDouble", index, text, flags);
        GetValue<void *>(0x0003c9d0, "DoGetPointer", index, text, flags);
        index++;
    }
    static const char *const alpha[] = { "true", "false", "tru", "fals", "truex", "falsey", "t", "f", "", "1", "0",
                                         "TRUE", " true", "falsetrue" };
    for (int i = 0; i < int(sizeof(alpha) / sizeof(alpha[0])); i++) {
        GetValue<bool>(0x0003cdd0, "DoGetBool alpha", i, alpha[i], kBoolAlpha);
        Run("GetLocText", i, [&] {
            OpenIn(alpha[i]);
            InIter first = { &g_in.buffer, false, 0 };
            InIter last = { NULL, true, 0 };
            static const char fields[] = "\0false\0true\0fa";
            int answer = ((int (*)(InIter *, InIter *, uint32_t, const char *))0x0003d180)(&first, &last, 2, fields);
            int answer2 = ((int (*)(InIter *, InIter *, uint32_t, const char *))0x0003d180)(&first, &last, 2, "|ab|abc|b");
            Logf("GetLocText %d: %d %d consumed %d", i, answer, answer2, int(*g_in.buffer.getNext - g_inText));
            CloseIn();
        });
    }
    for (int t = 0; t < (int)texts.size(); t++) {   // the field gatherers on their own
        Run("GetIntField/GetFloatField", t, [&] {
            char digits[80];
            Locale locale;
            for (int base = 0; base < 4; base++) {
                OpenIn(texts[t].c_str());
                ShadowIosBase_GetLoc(&g_in.ios, 0, &locale);
                InIter first = { &g_in.buffer, false, 0 };
                InIter last = { NULL, true, 0 };
                memset(digits, 0xaa, sizeof(digits));
                static const int bases[] = { 0, kOct, kHex, kDec };
                int result = ((int (*)(char *, InIter *, InIter *, int, const Locale *))0x0003b810)(
                    digits, &first, &last, bases[base], &locale);
                Logf("GetIntField %d/%d: %d consumed %d", t, base, result, int(*g_in.buffer.getNext - g_inText));
                LogBytes("digits", digits, sizeof(digits));
                CloseIn();
            }
            OpenIn(texts[t].c_str());
            ShadowIosBase_GetLoc(&g_in.ios, 0, &locale);
            InIter first = { &g_in.buffer, false, 0 };
            InIter last = { NULL, true, 0 };
            memset(digits, 0xaa, sizeof(digits));
            int exponent = ((int (*)(char *, InIter *, InIter *, const Locale *))0x0003c430)(digits, &first, &last,
                                                                                            &locale);
            Logf("GetFloatField %d: %d consumed %d", t, exponent, int(*g_in.buffer.getNext - g_inText));
            LogBytes("digits", digits, sizeof(digits));
            CloseIn();
        });
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Streams, ctype, numpunct and string members directly

static void LogString(const char *label, String *s) {
    Logf("%s: size %u capacity %u", label, s->size, s->capacity);
    LogBytes("text", s->Data(), s->size + 1);
}

static void LibraryCases() {
    Run("Streambuf", 0, [&] {
        static const int counts[] = { 0, 1, 3, 100 };
        for (int c = 0; c < 4; c++) {
            OpenIn("hello, world 12345");
            char out[128];
            memset(out, 0xaa, sizeof(out));
            int got = (g_in.buffer.*At<int (Streambuf::*)(char *, int)>(0x000382e0))(out, counts[c]);
            int next = (g_in.buffer.*At<int (Streambuf::*)()>(0x00038860))();
            int bump = (g_in.buffer.*At<int (Streambuf::*)()>(0x000382b0))();
            Logf("Xsgetn %d: %d snextc %d uflow %d", c, got, next, bump);
            LogBytes("got", out, 24);
            CloseIn();
            OpenOut(c == 3 ? 160 : 8);
            int put = (g_out.buffer.*At<int (Streambuf::*)(const char *, int)>(0x00038380))("abcdefghijklmnop",
                                                                                         c == 3 ? 16 : counts[c] * 3);
            int sputc = (g_out.buffer.*At<int (Streambuf::*)(char)>(0x00039900))('Z');
            Logf("Xsputn %d: %d sputc %d", c, put, sputc);
            LogBytes("out", g_outText, 20);
            CloseOut();
        }
        StreamPos pos, in;
        memset(&pos, 0xaa, sizeof(pos));
        memset(&in, 0x55, sizeof(in));
        (g_in.buffer.*At<StreamPos *(Streambuf::*)(StreamPos *, int, int, int)>(0x00038430))(&pos, 5, 1, 1);
        LogBytes("SeekOff", &pos, sizeof(pos));
        memset(&pos, 0xaa, sizeof(pos));
        (g_in.buffer.*At<StreamPos *(Streambuf::*)(StreamPos *, StreamPos, int)>(0x00038460))(&pos, in, 1);
        LogBytes("SeekPos", &pos, sizeof(pos));
        Logf("Overflow %d Underflow %d SetBuf %d",
             (g_in.buffer.*At<int (Streambuf::*)(int)>(0x00038290))('a'),
             (g_in.buffer.*At<int (Streambuf::*)()>(0x000382a0))(),
             (g_in.buffer.*At<Streambuf *(Streambuf::*)(char *, int)>(0x00038280))(NULL, 0) == &g_in.buffer);
        OpenOut(4);
        Ostream *os = &g_out;
        (os->*At<Ostream *(Ostream::*)(char)>(0x00039810))('a');
        (os->*At<Ostream *(Ostream::*)(char)>(0x00039810))('b');
        (os->*At<Ostream *(Ostream::*)(unsigned)>(0x00039a90))(123456u);
        (os->*At<Ostream *(Ostream::*)()>(0x00038790))();
        Logf("ostream state %x", g_out.ios.state);
        LogBytes("out", g_outText, 8);
        CloseOut();
    });

    Run("Streambuf objects", 0, [&] {   // constructed and destroyed on their own
        static Streambuf plain;
        static StrStreambuf text;
        memset(&plain, 0xaa, sizeof(plain));
        memset(&text, 0xaa, sizeof(text));
        (plain.*At<Streambuf *(Streambuf::*)()>(0x000387f0))();
        LogStreamObject("streambuf", &plain, sizeof(plain), 4, 0x38, 0x38);
        (plain.*At<Streambuf *(Streambuf::*)(unsigned)>(0x00038520))(0);
        LogStreamObject("destroyed", &plain, sizeof(plain), 4, 0x38, 0x38);
        (text.*At<StrStreambuf *(StrStreambuf::*)(const char *, int)>(0x00038af0))("some text", 4);
        LogStreamObject("strstreambuf", &text, sizeof(text), 4, 0x38, 0x38);
        (text.*At<StrStreambuf *(StrStreambuf::*)(unsigned)>(0x00038b50))(0);
        Logf("strstreambuf destroyed: vtable %08x", uint32_t(uintptr_t(text.vtable)));
    });

    Run("Ctype", 0, [&] {
        static Ctype ctype;
        memset(&ctype, 0, sizeof(ctype));
        (ctype.*At<Ctype *(Ctype::*)(const short *, bool, uint32_t)>(0x00037df0))(NULL, false, 7);
        Logf("ctype refs %u page %u delete %d", ctype.refs, ctype.ctype.page, ctype.ctype.deleteTable);
        LogBytes("table", ctype.ctype.table, 512);
        char all[256], copy[256];
        for (int c = 0; c < 256; c++) {
            all[c] = char(c);
            copy[c] = (ctype.*At<char (Ctype::*)(char)>(0x00037ed0))(char(c));
        }
        LogBytes("tolower", copy, 256);
        for (int c = 0; c < 256; c++)
            copy[c] = (ctype.*At<char (Ctype::*)(char)>(0x00037f20))(char(c));
        LogBytes("toupper", copy, 256);
        memcpy(copy, all, 256);
        (ctype.*At<const char *(Ctype::*)(char *, const char *)>(0x00037ef0))(copy, copy + 256);
        LogBytes("tolower range", copy, 256);
        memcpy(copy, all, 256);
        (ctype.*At<const char *(Ctype::*)(char *, const char *)>(0x00037f40))(copy, copy + 256);
        LogBytes("toupper range", copy, 256);
        memset(copy, 0, 256);
        (ctype.*At<const char *(Ctype::*)(const char *, const char *, char *)>(0x00037f80))(all, all + 100, copy);
        (ctype.*At<const char *(Ctype::*)(const char *, const char *, char, char *)>(0x00037fc0))(all + 100, all + 256, '?', copy + 100);
        LogBytes("widen/narrow range", copy, 256);
        Logf("widen %02x narrow %02x", uint8_t((ctype.*At<char (Ctype::*)(char)>(0x00037f70))('\x93')),
             uint8_t((ctype.*At<char (Ctype::*)(char, char)>(0x00037fb0))('\x94', '?')));
        (ctype.*At<void (Ctype::*)()>(0x00038080))();
        Logf("ctype after %08x", uint32_t(uintptr_t(ctype.vtable)));
    });

    Run("Facets", 0, [&] {
        static const unsigned getCats[] = { 0x00037ff0, 0x00038a80, 0x00039d10, 0x0003aa90 };
        static const unsigned deletes[] = { 0x00038060, 0x00038770, 0x00038770, 0x0003ab60 };
        for (int f = 0; f < 4; f++) {
            const Facet *made = NULL;
            uint32_t category = ((uint32_t (*)(const Facet **))getCats[f])(&made);
            uint32_t again = ((uint32_t (*)(const Facet **))getCats[f])(&made);   // already made: no-op
            uint32_t none = ((uint32_t (*)(const Facet **))getCats[f])(NULL);
            Logf("GetCat %d: %u %u %u vtable %08x refs %u", f, category, again, none,
                 uint32_t(uintptr_t(made->vtable)), made->refs);
            if (f == 3) {
                Numpunct *punct = (Numpunct *)made;
                Logf("numpunct point %02x sep %02x", uint8_t((punct->*At<char (Numpunct::*)()>(0x0003ab40))()),
                     uint8_t((punct->*At<char (Numpunct::*)()>(0x0003ab50))()));
                static const unsigned names[] = { 0x0003d3b0, 0x0003d3d0, 0x0003d420, 0x0003a960, 0x0003cd90, 0x0003cdb0 };
                for (int n = 0; n < 6; n++) {
                    String s;
                    memset(&s, 0xaa, sizeof(s));
                    (punct->*At<String *(Numpunct::*)(String *)>(names[n]))(&s);
                    LogString("numpunct string", &s);
                    ShadowString_Tidy(&s, 0, true);
                }
                CvtVec cvt = { 0, 0 };
                char *copy = ((char *(*)(const char *, char *, const CvtVec *))0x0003acd0)("locale text", NULL, &cvt);
                Logf("MakeLocString %s", copy);
                ShadowBuiltinDelete(copy);
            }
            Facet *dying = const_cast<Facet *>(made);
            (dying->*At<void *(Facet::*)(unsigned)>(deletes[f]))(1);
        }
        static NumGet numGet;
        (numGet.*At<NumGet *(NumGet::*)(uint32_t)>(0x00038700))(3);
        LogBytes("NumGet", &numGet, sizeof(numGet));
        OpenIn("x");
        Locale locale;
        ShadowIosBase_GetLoc(&g_in.ios, 0, &locale);
        Logf("use_facet %d %d %d %d", ((const Ctype *(*)(const Locale *))0x000385f0)(&locale) != NULL,
             ((const NumGet *(*)(const Locale *))0x00038e00)(&locale) != NULL,
             ((const NumPut *(*)(const Locale *))0x00039c00)(&locale) != NULL,
             ((const Numpunct *(*)(const Locale *))0x0003a980)(&locale) != NULL);
        CloseIn();
    });

    Run("Iterators", 0, [&] {
        OpenIn("ab");
        InIter a = { &g_in.buffer, false, 0 }, end = { NULL, true, 0 };
        for (int i = 0; i < 4; i++) {
            bool equal = (a.*At<bool (InIter::*)(const InIter *)>(0x0003b770))(&end);
            char peek = (a.*At<char (InIter::*)()>(0x0003b7c0))();
            Logf("iterator %d: equal %d peek %02x got %d", i, equal, uint8_t(peek), a.got);
            (a.*At<void (InIter::*)()>(0x0003bbd0))();
        }
        CloseIn();
        OpenOut(3);
        OutIter out = { false, {}, &g_out.buffer };
        for (int i = 0; i < 5; i++)
            (out.*At<OutIter *(OutIter::*)(char)>(0x0003a900))(char('p' + i));
        OutIter none = { false, {}, NULL };
        (none.*At<OutIter *(OutIter::*)(char)>(0x0003a900))('q');
        Logf("ostreambuf_iterator failed %d %d", out.failed, none.failed);
        LogBytes("out", g_outText, 6);
        CloseOut();
    });

    Run("String", 0, [&] {
        static const uint32_t counts[] = { 0, 1, 15, 16, 40 };
        for (int i = 0; i < 5; i++) {
            String s, t;
            memset(&s, 0xaa, sizeof(s));
            (s.*At<String *(String::*)(uint32_t, char)>(0x0003bcb0))(counts[i], 'a');
            LogString("ConstructFill", &s);
            (s.*At<String *(String::*)(uint32_t, char)>(0x0003bc20))(counts[(i + 2) % 5], 'b');
            LogString("AppendFill", &s);
            (s.*At<String *(String::*)(uint32_t, char)>(0x0003bce0))(counts[(i + 1) % 5], 'c');
            LogString("AssignFill", &s);
            memset(&t, 0xaa, sizeof(t));
            (t.*At<String *(String::*)(uint32_t, char)>(0x0003bcb0))(counts[(i + 3) % 5], 'd');
            // An offset past the end throws out_of_range (_Xran), which nothing in this process can catch: keep
            // the offset inside the string.
            uint32_t offset = uint32_t(i % 3) <= t.size ? uint32_t(i % 3) : t.size;
            (s.*At<String *(String::*)(const String *, uint32_t, uint32_t)>(0x0003d090))(&t, offset, (i & 1) ? 0xffffffff : 2);
            LogString("AppendSub", &s);
            ShadowString_Tidy(&s, 0, true);
            ShadowString_Tidy(&t, 0, true);
        }
    });
}

// ---------------------------------------------------------------------------------------------------------------
// DebugVariable<unsigned int>

static DebugUIntVariable g_var, g_inner;
static uint32_t g_values[8];
static uint32_t g_innerValue;
static const char *const kValueNames[] = { "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight" };

static void SetUpVariable(uint32_t minimum, uint32_t maximum, float step, uint32_t value, bool indexed, bool named) {
    memset(&g_var, 0, sizeof(g_var));
    memset(&g_inner, 0, sizeof(g_inner));
    g_var.vtable = (const void *const *)0x0018bd00;
    g_var.name = "variable";
    g_var.data = g_values;
    g_var.maximum = maximum;
    g_var.minimum = minimum;
    g_var.step = step;
    g_var.names = named ? kValueNames : NULL;
    for (int i = 0; i < 8; i++)
        g_values[i] = 0x11111111u * i;
    if (indexed) {
        g_inner.vtable = (const void *const *)0x0018bd00;
        g_inner.data = &g_innerValue;
        g_innerValue = 2;
        g_var.indexer = &g_inner;
        g_var.stride = 4;
        g_values[2] = value;
    } else {
        g_values[0] = value;
    }
}

static void VariableCases() {
    static const uint32_t ranges[][2] = { { 0, 3 }, { 0, 0 }, { 5, 2 }, { 0, 0xffffffff }, { 10, 1000 }, { 7, 8 } };
    static const float steps[] = { -1.0f, 0.0f, 0.1f, 1.0f, -0.25f, 1e9f, NAN, 1e-12f };
    static const float scales[] = { 1.0f, 0.5f, 0.0f, -1.0f, 2.0f, 1e-9f, NAN, INFINITY };
    int index = 0;
    for (int r = 0; r < 6; r++)
        for (int s = 0; s < 8; s++)
            for (int c = 0; c < 8; c++) {
                uint32_t minimum = ranges[r][0], maximum = ranges[r][1];
                uint32_t values[] = { minimum, maximum, maximum - 1, minimum + 1, (minimum + maximum) / 2, 0xffffffff };
                for (int v = 0; v < 6; v++) {
                    for (int method = 0; method < 2; method++) {
                        int i = index++;
                        Run("Increase/Decrease", i, [&] {
                            SetUpVariable(minimum, maximum, steps[s], values[v], (i & 1) != 0, false);
                            (g_var.*At<void (DebugUIntVariable::*)(float)>(method ? 0x00037cb0 : 0x00037b70))(scales[c]);
                            Logf("Increase/Decrease %d:", i);
                            LogBytes("values", g_values, sizeof(g_values));
                        });
                    }
                }
            }
    for (int i = 0; i < 400; i++) {
        uint32_t value = (i < 20) ? uint32_t(i) : (i % 5 == 0) ? 0xffffffff - (Next() % 3) : Next() >> (Next() % 32);
        bool indexed = (i & 2) != 0;
        bool named = (i % 3 == 0) && value < 9;
        Run("variable", i, [&] {
            SetUpVariable(uint32_t(i % 4), 3 + i, (i & 4) ? -1.0f : 0.5f, value, indexed, named);
            char text[128];
            memset(text, 0xaa, sizeof(text));
            char *result = (g_var.*At<char *(DebugUIntVariable::*)(char *)>(0x000396d0))(text);
            Logf("AsString %d: %d", i, result == text);
            LogBytes("text", text, 40);
            int debounce = (g_var.*At<int (DebugUIntVariable::*)()>(0x00037b50))();
            uint32_t *pointer = (g_var.*At<uint32_t *(DebugData::*)()>(0x00037b00))();
            void *deindexed = (g_var.*At<void *(DebugData::*)(void *)>(0x00038160))(g_values + 1);
            Logf("Debounce %d pointer %d deindexed %d", debounce, int(pointer - g_values),
                 int((uint32_t *)deindexed - g_values));
            (g_var.*At<void (DebugUIntVariable::*)()>((i & 1) ? 0x00038490 : 0x000384c0))();
            LogBytes("SetToMin/Max", g_values, sizeof(g_values));
            char number[16];
            snprintf(number, sizeof(number), (i & 8) ? "%u" : " %d", value);
            (g_var.*At<void (DebugUIntVariable::*)(const char *)>(0x0003a3f0))(number);
            LogBytes("SetFromString", g_values, sizeof(g_values));
        });
    }
    Run("DebugData", 0, [&] {
        char text[8];
        memset(text, 0xaa, sizeof(text));
        DebugData *data = &g_var;
        (data->*At<char *(DebugData::*)(char *)>(0x000380e0))(text);
        LogBytes("DebugData::AsString", text, sizeof(text));
    });
}

// ---------------------------------------------------------------------------------------------------------------
// The tuning files

struct TuningText {
    std::string name;
    std::string text;
};

// Every .tun and .ini in the driving archives on the host.
static void ReadArchives(std::vector<TuningText> *tunings, std::vector<TuningText> *inis) {
    static const char *const archives[] = { "mis01", "mis11", "mis13a", "mis13b", "mis13c", "mis3", "mis4", "race",
                                            "misc" };
    std::set<std::string> seen;
    for (int a = 0; a < int(sizeof(archives) / sizeof(archives[0])); a++) {
        char xboxPath[64], hostPath[MAX_PATH];
        snprintf(xboxPath, sizeof(xboxPath), "D:\\driving\\%s.viv", archives[a]);
        if (!Xbox_ResolvePath(xboxPath, hostPath, sizeof(hostPath)))
            continue;
        FILE *file = fopen(hostPath, "rb");
        if (file == NULL)
            continue;
        uint8_t header[16];
        if (fread(header, 1, 16, file) == 16 && memcmp(header, "BIGF", 4) == 0) {
            uint32_t count = uint32_t(header[8]) << 24 | header[9] << 16 | header[10] << 8 | header[11];
            uint32_t headerSize = uint32_t(header[12]) << 24 | header[13] << 16 | header[14] << 8 | header[15];
            std::vector<uint8_t> directory(headerSize > 16 ? headerSize - 16 : 0);
            if (fread(directory.data(), 1, directory.size(), file) == directory.size()) {
                size_t at = 0;
                for (uint32_t i = 0; i < count && at + 8 < directory.size(); i++) {
                    const uint8_t *e = &directory[at];
                    uint32_t offset = uint32_t(e[0]) << 24 | e[1] << 16 | e[2] << 8 | e[3];
                    uint32_t size = uint32_t(e[4]) << 24 | e[5] << 16 | e[6] << 8 | e[7];
                    std::string name((const char *)e + 8);
                    at += 8 + name.size() + 1;
                    bool isTuning = name.size() > 4 && name.compare(name.size() - 4, 4, ".tun") == 0;
                    bool isIni = name.size() > 4 && name.compare(name.size() - 4, 4, ".ini") == 0;
                    if ((!isTuning && !isIni) || size > (1u << 20) || !seen.insert(name).second)
                        continue;
                    TuningText entry;
                    entry.name = name;
                    entry.text.resize(size);
                    long back = ftell(file);
                    fseek(file, offset, SEEK_SET);
                    if (fread(&entry.text[0], 1, size, file) == size) {
                        // Most entries are packed ("xx FB" RefPack), as FileLoadz unpacks them in the game.
                        if (size >= 5 && uint8_t(entry.text[1]) == 0xfb) {
                            const uint8_t *packed = (const uint8_t *)entry.text.data();
                            std::string unpacked(unpacksizez(packed), '\0');
                            if (!unpacked.empty())
                                UNPACK_unpack(packed, (uint8_t *)&unpacked[0]);
                            entry.text = unpacked;
                        }
                        (isTuning ? tunings : inis)->push_back(entry);
                    }
                    fseek(file, back, SEEK_SET);
                }
            }
        }
        fclose(file);
    }
}

static DTuningDBMgr g_fakeManager;
static DTuningFile::Reader g_fakeReader;

typedef void (*AttribChar)(const char *, char *, int, int, uint32_t, float, const char *const *);
typedef void (*AttribInt)(const char *, int *, int, int, uint32_t, float, const char *const *);
typedef void (*AttribUInt)(const char *, unsigned *, unsigned, unsigned, uint32_t, float, const char *const *);
typedef void (*AttribFloat)(const char *, float *, float, float, uint32_t, float, const char *const *);
typedef void (*AttribBool)(const char *, bool *, int, int, uint32_t, float, const char *const *);
typedef void (*AttribVector)(const char *, float *, uint8_t *, uint32_t);
typedef void (*AttribColour)(const char *, uint32_t *, uint8_t *, uint32_t);

// Every dbattrib_ for one name, into arrays (stride apart) filled with a pattern first; with an index running,
// indices 0..indexMax, named by `names` or numbered.
//
// A colour value with fewer than four numbers leaves the original's other channels as whatever its stack held
// (the port has 0 there, Tuning.cpp), so those colours are blanked before the compare. Every colour in the
// shipped tuning files has all four.
static void ReadAllTypes(const char *name, bool indexed, uint32_t indexMax, const char *const *names) {
    struct Outputs {
        char chars[16];
        int ints[16];
        unsigned uints[16];
        float floats[16];
        bool bools[16];
        float vectors[16][4];
        uint32_t colours[16];
    } out;
    memset(&out, 0xa5, sizeof(out));
    ((AttribChar)0x000395d0)(name, out.chars, 0, 1, 1, 1.0f, NULL);
    ((AttribInt)0x00039ff0)(name, out.ints, 0, 1, 4, 1.0f, NULL);
    ((AttribUInt)0x0003a0f0)(name, out.uints, 0, 1, 4, 1.0f, NULL);
    ((AttribFloat)0x0003a1f0)(name, out.floats, 0.0f, 1.0f, 4, 1.0f, NULL);
    ((AttribBool)0x0003a2f0)(name, out.bools, 0, 1, 1, -1.0f, NULL);
    ((AttribVector)0x0003d470)(name, &out.vectors[0][0], NULL, 16);
    ((AttribColour)0x0003d530)(name, out.colours, NULL, 4);
    for (uint32_t i = 0; i <= (indexed ? indexMax : 0); i++) {
        const char *item;
        if (indexed) {
            char number[16];
            snprintf(number, sizeof(number), "%u", i);
            item = (g_fakeReader.*At<char *(DTuningFile::Reader::*)(const char *, const char *)>(0x0003d870))(
                name, names != NULL ? names[i] : number);
        } else {
            item = (g_fakeReader.*At<char *(DTuningFile::Reader::*)(const char *)>(0x0003d7f0))(name);
        }
        int a, r, g, b;
        if (item != NULL && sscanf(item, "%d,%d,%d,%d", &a, &r, &g, &b) < 4)
            out.colours[i] = 0;
    }
    LogBytes("dbattrib", &out, sizeof(out));
}

static void TuningTextCases(const std::vector<TuningText> &tunings) {
    DTuningDBMgr *savedManager = TuningDBMgr;
    DebugUIntVariable *savedIndex = ShadowCurrentIndexer;
    std::vector<TuningText> all = tunings;
    TuningText synthetic;
    synthetic.name = "synthetic";
    synthetic.text = "fogSTART 1234.5\nMoonsize 17\nEnablelightmaps 1\nflag t\nLight1{0} 0.1,0.2,0.3,1.0\n"
                     "Light1{1} 1,2,3,4\nLight1{Two} 5,6\nColour{0} 255,128,64,32\nColour{1} 1,2,3\n"
                     "Size{Zero} 3.5\nSize{One} -2\nSize{2} 0x10\nempty \nweird =  42 \n#comment 3\n"
                     "Deep{Zero} 1\nDeep{One} 2\nDeep{Two} 3\nDeep{Three} 4\nbool{0} true\nbool{1} 0\n";
    all.push_back(synthetic);
    for (int f = 0; f < (int)all.size(); f++) {
        const std::string &text = all[f].text;
        std::vector<std::string> keys;
        std::map<std::string, std::vector<std::string> > indexed;
        for (size_t at = 0; at < text.size();) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            size_t keyEnd = line.find_first_of(" \t=\r");
            std::string key = line.substr(0, keyEnd);
            if (key.empty())
                continue;
            size_t brace = key.find('{');
            size_t close = key.find('}');
            if (brace != std::string::npos && close != std::string::npos && close > brace)
                indexed[key.substr(0, brace)].push_back(key.substr(brace + 1, close - brace - 1));
            keys.push_back(key);
        }
        // (No empty name: OptionParser finds "" at every character, and each match not at a line's start
        // leaks the key copy it allocates - the original does too - which runs the game's heap dry on a long file.)
        keys.push_back("not a key");
        g_fakeReader.text = text.c_str();
        g_fakeManager.reader = &g_fakeReader;
        for (int k = 0; k < (int)keys.size(); k++) {
            Run("dbattrib", k, [&] {
                TuningDBMgr = &g_fakeManager;
                ShadowCurrentIndexer = NULL;
                Logf("file %d key %d:", f, k);
                ReadAllTypes(keys[k].c_str(), false, 0, NULL);
                char *item = (g_fakeReader.*At<char *(DTuningFile::Reader::*)(const char *)>(0x0003d7f0))(keys[k].c_str());
                Logf("FindItem %s", item != NULL ? item : "(null)");
            });
        }
        int b = 0;
        for (std::map<std::string, std::vector<std::string> >::iterator it = indexed.begin(); it != indexed.end();
             ++it, ++b) {
            for (int named = 0; named < 2; named++) {
                Run("dbindex", b, [&] {
                    TuningDBMgr = &g_fakeManager;
                    std::vector<const char *> names;
                    for (size_t n = 0; n < it->second.size(); n++)
                        names.push_back(it->second[n].c_str());
                    static uint32_t indexValue;
                    indexValue = 0x5a5a5a5a;
                    uint32_t last = uint32_t(names.size()) - 1;
                    if (last > 15)
                        last = 15;   // the output arrays hold 16
                    uint32_t indexMax = named ? last : (last > 4 ? 4 : last);
                    ((void (*)(const char *, uint32_t *, uint32_t, uint32_t, const char *const *))0x00038190)(
                        "index", &indexValue, 0, indexMax, named ? names.data() : NULL);
                    DebugUIntVariable *index = ShadowCurrentIndexer;
                    Logf("file %d index %d/%d: index value %08x", f, b, named, indexValue);
                    ReadAllTypes(it->first.c_str(), true, indexMax, named ? names.data() : NULL);
                    LogBytes("indexer", (const uint8_t *)index + 8, 0x20);   // not the name or names pointers
                    Logf("names %d", index->names != NULL);
                    char *item = (g_fakeReader.*At<char *(DTuningFile::Reader::*)(const char *, const char *)>(
                        0x0003d870))(it->first.c_str(), names[0]);
                    Logf("FindIndexedItem %s value %08x", item != NULL ? item : "(null)", indexValue);
                    ((void (*)())0x00037ad0)();
                    Logf("after dbendindex %d", ShadowCurrentIndexer == NULL);
                    ShadowBuiltinDelete(index);
                });
            }
        }
    }
    TuningDBMgr = savedManager;
    ShadowCurrentIndexer = savedIndex;
}

// ParseData, ParseData_Colour, EncryptDecrypt, the coordinates.
static void SmallCases(const std::vector<std::string> &texts) {
    for (int t = 0; t <= (int)texts.size(); t++) {
        const char *text = t < (int)texts.size() ? texts[t].c_str() : NULL;
        Run("ParseData", t, [&] {
            float values[5];
            memset(values, 0xa5, sizeof(values));
            ((void (*)(const char *, float *))0x0003d910)(text, values);
            uint32_t colour = 0xa5a5a5a5;
            ((void (*)(const char *, uint32_t *))0x0003d940)(text, &colour);
            // A colour with fewer than four numbers: the original's missing channels are its stack's leftovers
            // (the port has 0, and the text pointer for a missing alpha), so the colour is blanked on both sides.
            int a, r, g, b;
            if (text != NULL && sscanf(text, "%d,%d,%d,%d", &a, &r, &g, &b) < 4)
                colour = 0;
            Logf("ParseData %d: colour %08x", t, colour);
            LogBytes("values", values, sizeof(values));
        });
    }
    for (int i = 0; i < 40; i++) {
        Run("EncryptDecrypt", i, [&] {
            uint8_t in[300], out[300];
            for (int j = 0; j < 300; j++) {
                in[j] = uint8_t(Next());
                out[j] = 0xaa;
            }
            static const char *const keys[] = { "b8D;V`fj", "k", "longer key with spaces", "\x80\xff" };
            int count = int(Next() % 300);
            ((void (*)(const uint8_t *, int, uint8_t *, const char *))0x0011c5c0)(in, count, out, keys[i % 4]);
            Logf("EncryptDecrypt %d: %d", i, count);
            LogBytes("out", out, sizeof(out));
        });
    }
    for (int i = 0; i < 200; i++) {
        Run("Float_COORD", i, [&] {
            uint32_t source[4] = { Next(), Next(), Next(), Next() };
            if (i % 7 == 0)
                source[1] = 0x7f800001;   // a signalling NaN
            uint32_t four[5], three[4];
            memset(four, 0xa5, sizeof(four));
            memset(three, 0xa5, sizeof(three));
            Coord4 *r4 = ((Coord4 *(*)(Coord4 *, const Coord3 *))0x0005c9f0)((Coord4 *)four, (const Coord3 *)source);
            Coord3 *r3 = ((Coord3 *(*)(Coord3 *, const Coord4 *))0x0005ca40)((Coord3 *)three, (const Coord4 *)source);
            Logf("Float_COORD %d: %d %d", i, (void *)r4 == four, (void *)r3 == three);
            LogBytes("four", four, sizeof(four));
            LogBytes("three", three, sizeof(three));
        });
    }
}

// ---- through the game's file system (a mission archive mounted)

static const char *const kDatabases[] = { "Physics:Physical", "Physics:Friction", "Physics:Rigid", "Render:Fog",
                                          "Render:Lighting", "Render:FX", "Render", "Render:SwitchEffects",
                                          "Render:CarRender", "Render:Glare", "No:Such" };
static const char *const kLevels[] = { "paris_mis01", "uw_mis11", "junglea_mis13a", "jungleb_mis13b",
                                       "junglec_mis13c", "snow1a_mis3", "snow2a_mis4", "snow2a_race", "paris_map",
                                       "nosuchlevel" };

static int g_filesOpened;

static void LogText(const char *label, const char *text) {
    if (text == NULL) {
        Logf("%s (null)", label);
        return;
    }
    size_t length = strlen(text);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++)
        hash = (hash ^ uint8_t(text[i])) * 16777619u;
    Logf("%s length %u hash %08x", label, unsigned(length), hash);
}

// A file loaded with FileLoadz, by the original and the port alike (the same call, path and flags): the loader
// does not terminate it, so strlen runs on into whatever heap bytes follow, which differ allocation to
// allocation. Only the file's own bytes are hashed, as many as UFileLoader::FileSize says it has.
static void LogLoadedFile(const char *label, const char *path, const char *text) {
    if (text == NULL) {
        Logf("%s (null)", label);
        return;
    }
    int size = ((int (__cdecl *)(const char *))0x001170a0)(path);   // UFileLoader::FileSize
    uint32_t hash = 2166136261u;
    for (int i = 0; i < size; i++)
        hash = (hash ^ uint8_t(text[i])) * 16777619u;
    Logf("%s size %d hash %08x", label, size, hash);
}

static void MountedCases(const std::vector<TuningText> &inis) {
    g_filesOpened = 0;
    static DTuningDBMgr manager;
    for (int d = 0; d < int(sizeof(kDatabases) / sizeof(kDatabases[0])); d++)
        for (int l = 0; l < int(sizeof(kLevels) / sizeof(kLevels[0])); l++) {
            int i = d * 100 + l;
            Run("LoadDatabase", i, [&] {
                memset(&manager, 0, sizeof(manager));
                manager.vtable = (const void *const *)0x0018bebc;
                (manager.*At<void (DTuningDBMgr::*)(const char *, const char *, int, bool)>(0x0003d640))(
                    kDatabases[d], kLevels[l], l & 1, false);
                Logf("LoadDatabase %d: name %d file %d reader %d", i, manager.name == kDatabases[d],
                     manager.file != NULL, manager.reader != NULL);
                if (manager.file != NULL) {
                    DTuningFile copy = *manager.file;
                    copy.reader = NULL;
                    LogBytes("file", &copy, sizeof(copy));
                }
                if (manager.reader != NULL) {
                    LogLoadedFile("text", manager.file->path, manager.reader->text);
                    if (manager.reader->text != NULL)
                        g_filesOpened++;
                }
                (manager.*At<void (DTuningDBMgr::*)()>(0x0003d6c0))();
            });
        }
    for (int i = 0; i < 16; i++) {
        Run("DTuningFile", i, [&] {
            static DTuningFile file;
            memset(&file, 0xaa, sizeof(file));
            const char *level = (i & 3) == 3 ? NULL : kLevels[i % 10];
            (file.*At<DTuningFile *(DTuningFile::*)(const char *, const char *, int, bool)>(0x0003d6e0))(
                kDatabases[i % 11], level, (i >> 2) & 1, ((i >> 3) & 1) != 0);
            LogBytes("Construct", &file, sizeof(file));
            DTuningFile::Reader *reader = (file.*At<DTuningFile::Reader *(DTuningFile::*)()>(0x0003d9c0))();
            Logf("OpenForRead %d: mode %d", reader != NULL, file.mode);
            if (reader != NULL)
                LogLoadedFile("text", file.path, reader->text);
            (file.*At<void (DTuningFile::*)()>(0x0003da50))();
        });
    }
    Run("Singleton", 0, [&] {
        DTuningDBMgr *saved = TuningDBMgr;
        ((void (*)())0x00059630)();
        DTuningDBMgr *made = TuningDBMgr;
        Logf("InitSingleton vtable %08x", made != NULL ? uint32_t(uintptr_t(made->vtable)) : 0);
        (made->*At<void (DTuningDBMgr::*)()>(0x0003d620))();   // deletes it
        Logf("DestroySingleton leaves the pointer %d", TuningDBMgr == made);
        TuningDBMgr = saved;
    });

    // IniFiles: from the file system when it is mounted, from the host copy always.
    for (int i = 0; i < (int)inis.size() + 1; i++) {
        Run("IniFiles", i, [&] {
            static IniFiles ini;
            memset(&ini, 0, sizeof(ini));
            std::string text;
            if (i == (int)inis.size()) {
                (ini.*At<IniFiles *(IniFiles::*)(const char *, bool)>(0x000e4030))("data/render/camera.ini", false);
                Logf("IniFiles mounted: text %d dafi %d", ini.text != NULL, ini.dafi != NULL);
                if (ini.text == NULL) {
                    (ini.*At<IniFiles *(IniFiles::*)(unsigned)>(0x000e4240))(0);
                    return;
                }
                text = std::string(ini.text, MEM_size(ini.text));
            } else {
                text = inis[i].text;
                ini.vtable = (const void *const *)0x001a0998;
                ini.text = (char *)MEM_alloc("dbvarshadow ini", int(text.size()) + 1, *(unsigned *)0x00242cdc);
                memcpy(ini.text, text.data(), text.size());
                ini.text[text.size()] = '\0';
                ini.dafi = ShadowDAFI_open(ini.text, int(text.size()));
                Logf("IniFiles %d: %s", i, inis[i].name.c_str());
            }
            std::string section;
            for (size_t at = 0; at < text.size();) {
                size_t end = text.find('\n', at);
                if (end == std::string::npos)
                    end = text.size();
                std::string line = text.substr(at, end - at);
                at = end + 1;
                if (!line.empty() && line[0] == '[') {
                    section = line.substr(1, line.find(']') == std::string::npos ? std::string::npos : line.find(']') - 1);
                    bool found = (ini.*At<bool (IniFiles::*)(const char *)>(0x000e4220))(section.c_str());
                    Logf("FindSection %d", found);
                    continue;
                }
                std::string key = line.substr(0, line.find_first_of(" =\t\r"));
                if (key.empty())
                    continue;
                for (int pass = 0; pass < 2; pass++) {
                    const char *s = pass ? "nosection" : section.c_str();
                    double f = (ini.*At<double (IniFiles::*)(const char *, const char *, float)>(0x000e40f0))(s, key.c_str(), -7.5f);
                    int n = (ini.*At<int (IniFiles::*)(const char *, const char *, int)>(0x000e4150))(s, key.c_str(), -9);
                    const char *str = (ini.*At<const char *(IniFiles::*)(const char *, const char *, const char *)>(0x000e41c0))(s, key.c_str(), "fallback");
                    uint64_t bits;
                    memcpy(&bits, &f, 8);
                    Logf("Read %d: %08x%08x %d", pass, uint32_t(bits >> 32), uint32_t(bits), n);
                    LogText("ReadString", str);
                }
            }
            (ini.*At<IniFiles *(IniFiles::*)(unsigned)>(0x000e4240))(0);
            Logf("Delete: text %d dafi %d", ini.text == NULL, ini.dafi == NULL);
        });
    }
}

// ---- the suite

static void Suite(const std::vector<TuningText> &tunings, const std::vector<TuningText> &inis) {
    g_seed = 0x2468ace1;
    g_numGet.vtable = (const void *const *)0x0018bd94;
    g_numGet.refs = 1;
    g_numPut.vtable = (const void *const *)0x0018be04;
    g_numPut.refs = 1;
    std::vector<std::string> texts = MakeTexts();
    ParserCases(texts);
    PutCases();
    GetCases(texts);
    LibraryCases();
    VariableCases();
    TuningTextCases(tunings);
    SmallCases(texts);
    // The mission archive the tuning files and camera.ini are in is closed just before the game's main loop
    // (UFileLoader::StopUsingBigFile): opened again under its name for this part, and closed after.
    bool reopened = false;
    uint8_t logRequests = ArchiveLogRequests;
    if (ArchiveOpen == 0 && ArchiveName[0] != 0) {
        char name[256];
        strcpy(name, ArchiveName);
        reopened = UFileLoader::StartUsingBigFile("driving", name, false);
    }
    MountedCases(inis);
    if (reopened)
        UFileLoader::StopUsingBigFile();
    ArchiveLogRequests = logRequests;
}

} // namespace

void DebugVarShadow_Run(void) {
    const char *on = getenv("NIGHTFIRE_DBVARSHADOW");
    if (on == NULL || atoi(on) == 0)
        return;

    std::vector<TuningText> tunings, inis;
    ReadArchives(&tunings, &inis);

    std::string originalLog, portLog;
    g_log = &originalLog;
    g_cases = 0;
    int swapped = SwapOriginals(true);
    Suite(tunings, inis);
    SwapOriginals(false);
    int cases = g_cases;
    int filesOriginal = g_filesOpened;

    g_log = &portLog;
    g_cases = 0;
    Suite(tunings, inis);

    int checks = 0, differ = 0;
    std::map<std::string, int> differByKind;
    std::string kind = "(start)";
    size_t a = 0, b = 0;
    while (a < originalLog.size() || b < portLog.size()) {
        size_t ae = originalLog.find('\n', a), be = portLog.find('\n', b);
        if (ae == std::string::npos)
            ae = originalLog.size();
        if (be == std::string::npos)
            be = portLog.size();
        std::string lineA = originalLog.substr(a, ae - a), lineB = portLog.substr(b, be - b);
        if (!lineA.empty() && lineA[0] == '#') {
            kind = lineA.substr(1);
            differByKind[kind] += 0;
        }
        checks++;
        if (lineA != lineB)
            differByKind[kind]++;
        if (lineA != lineB && differ++ < 10) {
            size_t at = 0;
            while (at < lineA.size() && at < lineB.size() && lineA[at] == lineB[at])
                at++;
            size_t from = at > 40 ? at - 40 : 0;
            printf("[dbvarshadow] differ at character %u of: %.60s\n  original: ...%.80s\n  port:     ...%.80s\n",
                   unsigned(at), lineA.c_str(), lineA.c_str() + (from < lineA.size() ? from : lineA.size()),
                   lineB.c_str() + (from < lineB.size() ? from : lineB.size()));
        }
        a = ae + 1;
        b = be + 1;
    }
    printf("[dbvarshadow] %d originals swapped; %d tuning files and %d ini files from the archives, %d opened "
           "through the file system: %d cases, %d checks, %d differ\n", swapped, (int)tunings.size(),
           (int)inis.size(), filesOriginal, cases, checks, differ);
    std::string kinds;
    for (std::map<std::string, int>::iterator it = differByKind.begin(); it != differByKind.end(); ++it) {
        char part[96];
        snprintf(part, sizeof(part), "%s%s %d", kinds.empty() ? "" : ", ", it->first.c_str(), it->second);
        kinds += part;
    }
    printf("[dbvarshadow] differ by kind: %s\n", kinds.c_str());
    fflush(stdout);
}
