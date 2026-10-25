#include "CRuntimeShadowA.h"

#include "../platform/CRuntime.h"
#include "../platform/CRuntimeInternal.h"
#include "../platform/RefPack.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"
#include "../platform/XboxStartup.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_CRUNTIMESHADOWA=1: src/driving/platform/CRuntime*.cpp against the game's C runtime, each original run
// with its entry swapped back in (common/xbeOriginal.h) on the same inputs, its outputs compared byte for byte.
//
// CRuntimeShadowA_RunHeap (injection time): an allocation script - malloc of every awkward size, calloc (with
// overflowing products), realloc, free, _msize - on a fresh heap lent as the process heap, once through the
// originals and once through ours; each result compared by its offset in the heap, its HeapSize and (calloc) its
// zeroing. The two heaps start alike, so equal requests give equal offsets.
//
// CRuntimeShadowA_Run (first tick):
//  - formats: every format string our code passes (with flags as the C++ streams build them), and random ones over
//    every conversion, flag, width, precision and size, with edge values (0, -0, denormals, the largest doubles,
//    halfway ties, INT_MIN, NaN and infinities, null and long strings, precisions over the 163 that take a heap
//    buffer); sprintf, vsprintf, and _vsnprintf at short counts. Return value, the whole buffer, %n, errno.
//  - parsing: every token with a digit in every text file of the disc's driving archives, and random numeric
//    strings (long mantissas, huge and tiny exponents, 2^53 + odd ties, hex and octal), through atof, strtod, atol,
//    strtol, strtoul, _strtoi64, _strtoui64 (bases 0, 10, 16) and sscanf (the game's formats and generic ones);
//    each text line through the game's line formats. The result's bits, the end pointer, errno, the count.
//  - qsort: random arrays with many ties, every comparison (which two elements) recorded and compared.
//  - rand: sequences on two threads at once, each from its own seed.
//  - atexit: on lent exit lists, full and not, so the growth path runs.
//  - isdigit, isalnum, tolower over -128..256; localeconv, _errno; _CIacos, _CIasin, _CIpow on the x87 (the 80-bit
//    result), _clearfp after raised exceptions.
//
// One summary line: "[crtshadowa] ...: N cases, M checks, D differ", first differences detailed
// (NIGHTFIRE_CRUNTIMESHADOWA=2: a line per part as well).
// ---------------------------------------------------------------------------------------------------------------

namespace {

const char kTag[] = "[crtshadowa]";

int g_cases, g_checks, g_differ, g_faults, g_details;

uint32_t g_seed = 0x2468ace1;
uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
uint32_t Below(uint32_t n) {
    return n ? Next() % n : 0;
}

void Differ(const char *what, const char *format, ...) {
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

// A call with a fault caught and counted.
typedef void (*GuardedFn)(void *);
bool Guard(GuardedFn fn, void *context) {
#ifdef _MSC_VER
    __try {
        fn(context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    fn(context);
#endif
    return true;
}

// fn on context, with the original at `swap` in place for the call when original (0: nothing to swap).
bool Side(bool original, uint32_t swap, GuardedFn fn, void *context) {
    if (original && swap != 0)
        XbeOriginal_Restore(swap, true);
    bool ok = Guard(fn, context);
    if (original && swap != 0)
        XbeOriginal_Restore(swap, false);
    return ok;
}

std::string Printable(const char *text, size_t max = 60) {
    std::string out;
    for (size_t i = 0; text[i] != '\0' && i < max; i++) {
        unsigned char c = text[i];
        if (c >= 32 && c < 127) {
            out += char(c);
        } else {
            char hex[8];
            snprintf(hex, sizeof(hex), "\\x%02x", c);
            out += hex;
        }
    }
    return out;
}

double Bits64(uint64_t bits) {
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

uint64_t Of(double d) {
    uint64_t bits;
    memcpy(&bits, &d, 8);
    return bits;
}

// ---------------------------------------------------------------------------------------------------------------
// The heap (injection time)

#define GameProcessHeapHandle (*(HANDLE *)0x0024b218u)   // the game's copy of ours (XboxStartup.h), the originals'

typedef void *(__cdecl *MallocFn)(size_t);
typedef void (__cdecl *FreeFn)(void *);
typedef void *(__cdecl *CallocFn)(size_t, size_t);
typedef void *(__cdecl *ReallocFn)(void *, size_t);
typedef size_t (__cdecl *MsizeFn)(void *);

struct HeapOp {
    int kind;             // 0 malloc, 1 calloc, 2 realloc, 3 free, 4 _msize
    uint32_t a, b;        // sizes, or the slot to work on
    int slot;
};

struct HeapResult {
    intptr_t offset;      // from the heap's handle; 0 for null
    size_t size;          // HeapSize, or _msize's answer
    int zeroed;
};

struct HeapRun {
    bool original;
    const std::vector<HeapOp> *ops;
    std::vector<HeapResult> results;
    void *slots[64];
};

void RunHeapScript(void *context) {
    HeapRun *run = (HeapRun *)context;
    MallocFn mallocFn = run->original ? (MallocFn)0x001340e3 : Crt::malloc;
    FreeFn freeFn = run->original ? (FreeFn)0x001331dc : Crt::free;
    CallocFn callocFn = run->original ? (CallocFn)0x0013439d : Crt::calloc;
    ReallocFn reallocFn = run->original ? (ReallocFn)0x00134815 : Crt::realloc;
    MsizeFn msizeFn = run->original ? (MsizeFn)0x001369cc : Crt::_msize;
    HANDLE heap = ProcessHeapHandle;
    for (const HeapOp &op : *run->ops) {
        HeapResult r = { 0, 0, -1 };
        void *block = NULL;
        switch (op.kind) {
        case 0:
            block = mallocFn(op.a);
            break;
        case 1:
            block = callocFn(op.a, op.b);
            if (block != NULL) {
                size_t size = HeapSize(heap, 0, block);
                r.zeroed = 1;
                for (size_t i = 0; i < size; i++) {
                    if (((uint8_t *)block)[i] != 0)
                        r.zeroed = 0;
                }
            }
            break;
        case 2:
            block = reallocFn(run->slots[op.slot], op.a);
            if (block == NULL && op.a != 0)
                block = run->slots[op.slot];     // a failed realloc leaves the block
            break;
        case 3:
            freeFn(run->slots[op.slot]);
            run->slots[op.slot] = NULL;
            break;
        case 4:
            if (run->slots[op.slot] != NULL)
                r.size = msizeFn(run->slots[op.slot]);
            break;
        }
        if (op.kind <= 2) {
            if (block != NULL) {
                r.offset = (intptr_t)block - (intptr_t)heap;
                r.size = HeapSize(heap, 0, block);
                memset(block, 0xa5, r.size);     // dirty, so a later calloc that reuses it must zero it
            }
            run->slots[op.slot] = block;
        }
        run->results.push_back(r);
    }
}

std::vector<HeapOp> HeapScript() {
    std::vector<HeapOp> ops;
    static const uint32_t sizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 100, 0x84, 0x1c, 4095, 4096, 4097, 0x10000,
                                      0x12345, 0xffffffe0, 0xffffffe1, 0xfffffff0, 0xffffffff };
    int slot = 0;
    for (uint32_t size : sizes)
        ops.push_back({ 0, size, 0, slot++ % 64 });
    static const uint32_t counts[][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 3, 5 }, { 7, 13 }, { 1, 0x84 },
                                          { 0x10000, 0x10001 }, { 0x10000, 0x10000 }, { 2, 0x7ffffff8 },
                                          { 0xffffffe0, 1 }, { 1, 0xffffffe1 }, { 100, 100 } };
    for (auto &c : counts)
        ops.push_back({ 1, c[0], c[1], slot++ % 64 });
    for (int i = 0; i < 400; i++) {
        uint32_t r = Below(100);
        int s = int(Below(64));
        uint32_t size = Below(4) == 0 ? Below(40) : Below(3) == 0 ? Below(5000) : Below(300);
        if (r < 35)
            ops.push_back({ 0, size, 0, s });
        else if (r < 55)
            ops.push_back({ 1, Below(20), Below(60), s });
        else if (r < 70)
            ops.push_back({ 2, Below(10) == 0 ? 0 : size, 0, s });
        else if (r < 90)
            ops.push_back({ 3, 0, 0, s });
        else
            ops.push_back({ 4, 0, 0, s });
    }
    for (int s = 0; s < 64; s++)
        ops.push_back({ 3, 0, 0, s });
    return ops;
}

// ---------------------------------------------------------------------------------------------------------------
// Formats

typedef int (__cdecl *SprintfFn)(char *, const char *, ...);
typedef int (__cdecl *VsprintfFn)(char *, const char *, va_list);
typedef int (__cdecl *VsnprintfFn)(char *, size_t, const char *, va_list);

const int kFormatBuffer = 4096;
const int kWordCount = 16;

int g_countSlots[4];

struct FormatCall {
    int kind;             // 0 sprintf, 1 vsprintf, 2 _vsnprintf
    bool original;
    char *buffer;
    size_t count;
    const char *format;
    const uint32_t *words;
    int result;
};

void CallFormat(void *context) {
    FormatCall *c = (FormatCall *)context;
    const uint32_t *w = c->words;
    va_list list = reinterpret_cast<va_list>(const_cast<uint32_t *>(w));
    if (c->kind == 0) {
        SprintfFn fn = c->original ? (SprintfFn)0x00132767 : Crt::sprintf;
        c->result = fn(c->buffer, c->format, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10],
                       w[11], w[12], w[13], w[14], w[15]);
    } else if (c->kind == 1) {
        VsprintfFn fn = c->original ? (VsprintfFn)0x00133aee : Crt::vsprintf;
        c->result = fn(c->buffer, c->format, list);
    } else {
        VsnprintfFn fn = c->original ? (VsnprintfFn)0x001340f5 : Crt::_vsnprintf;
        c->result = fn(c->buffer, c->count, c->format, list);
    }
}

void CompareFormat(int kind, const char *format, const uint32_t *words, size_t count) {
    static char buffers[2][kFormatBuffer];
    static const uint32_t kAddress[3] = { 0x00132767, 0x00133aee, 0x001340f5 };
    int results[2], errors[2], slots[2][4];
    bool ok[2];
    for (int side = 0; side < 2; side++) {
        memset(buffers[side], 0xcd, kFormatBuffer);
        memset(g_countSlots, 0x5a, sizeof(g_countSlots));
        *Crt::_errno() = 0;
        FormatCall call = { kind, side == 0, buffers[side], count, format, words, 0 };
        ok[side] = Side(side == 0, kAddress[kind], CallFormat, &call);
        results[side] = call.result;
        errors[side] = *Crt::_errno();
        memcpy(slots[side], g_countSlots, sizeof(g_countSlots));
    }
    g_cases++;
    static const char *const kNames[3] = { "sprintf", "vsprintf", "_vsnprintf" };
    Check(ok[0] == ok[1], kNames[kind], "\"%s\": fault original %d port %d", Printable(format).c_str(), !ok[0], !ok[1]);
    if (!ok[0] || !ok[1])
        return;
    Check(results[0] == results[1], kNames[kind], "\"%s\" (count %u): returns %d, port %d", Printable(format).c_str(),
          unsigned(count), results[0], results[1]);
    Check(memcmp(buffers[0], buffers[1], kFormatBuffer) == 0, kNames[kind], "\"%s\": \"%s\", port \"%s\"",
          Printable(format).c_str(), Printable(buffers[0], 80).c_str(), Printable(buffers[1], 80).c_str());
    Check(errors[0] == errors[1], kNames[kind], "\"%s\": errno %d, port %d", Printable(format).c_str(), errors[0],
          errors[1]);
    Check(memcmp(slots[0], slots[1], sizeof(slots[0])) == 0, kNames[kind], "\"%s\": %%n %d, port %d",
          Printable(format).c_str(), slots[0][0], slots[1][0]);
}

// Doubles worth printing: zeros, denormals, the extremes, ties at every few places, powers of ten, NaNs.
double InterestingDouble() {
    static const uint64_t specials[] = {
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000001ull, 0x8000000000000001ull,
        0x000fffffffffffffull, 0x0010000000000000ull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
        0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000000ull, 0xfff8000000000000ull,
        0x7ff0000000000001ull, 0x7ff4000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull,
        0x3fe0000000000000ull, 0x4024000000000000ull, 0x3fb999999999999aull, 0x3fd5555555555555ull,
        0x4341c37937e08000ull, 0x44b52d02c7e14af6ull, 0x7e37e43c8800759cull, 0x0031fa182c40c60dull,
    };
    static const double ties[] = { 0.5, 1.5, 2.5, 0.125, 0.375, 1.0625, 2.675, 1.005, 0.045, 1e21, 1e22, 1e23,
                                   123456789.5, 999999.5, 9.5, 99.5, 0.95, 0.0005, 5e-5, 1234.5678, 3.14159265358979 };
    uint32_t r = Below(100);
    if (r < 20)
        return Bits64(specials[Below(sizeof(specials) / sizeof(specials[0]))]);
    if (r < 40) {
        double v = ties[Below(sizeof(ties) / sizeof(ties[0]))];
        return Below(2) ? -v : v;
    }
    if (r < 60) {                         // any finite double
        uint64_t bits = uint64_t(Next()) << 32 | Next();
        if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull)
            bits ^= 0x4000000000000000ull;
        return Bits64(bits);
    }
    if (r < 80) {                         // a decimal of a few digits at some scale
        double v = double(int32_t(Next()) % 100000) * pow(10.0, double(int(Below(30)) - 15));
        return v;
    }
    return (double(int32_t(Next())) / 65536.0);
}

uint32_t InterestingInt() {
    static const uint32_t specials[] = { 0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 0x8000, 0x7fff, 0xffff, 0x10000,
                                         10, 9, 99, 100, 255, 256, 0x12345678, 0xdeadbeef };
    if (Below(3) == 0)
        return specials[Below(sizeof(specials) / sizeof(specials[0]))];
    if (Below(2))
        return Next();
    return uint32_t(int32_t(Below(2000)) - 1000);
}

const char *g_strings[6];
std::string g_longString;
const uint16_t kWide1[] = { 'w', 'i', 'd', 'e', 0 };
const uint16_t kWide2[] = { 'A', 0x100, 'B', 0 };
const uint16_t kWide3[] = { 0 };
struct CountedString {
    short length;
    short maximumLength;
    const void *buffer;
};
CountedString g_counted[3];

void PushDouble(std::vector<uint32_t> *words, double d) {
    uint64_t bits = Of(d);
    words->push_back(uint32_t(bits));
    words->push_back(uint32_t(bits >> 32));
}

// One random conversion appended to format, its arguments to words.
void RandomSpec(std::string *format, std::vector<uint32_t> *words) {
    static const char conversions[] = "diouxXcCeEfgGsSpnZ%";
    char type = conversions[Below(sizeof(conversions) - 1)];
    *format += '%';
    if (type == '%') {
        *format += '%';
        return;
    }
    static const char flagChars[] = "-+ #0";
    for (int i = 0; i < 5; i++) {
        if (Below(4) == 0)
            *format += flagChars[i];
    }
    uint32_t w = Below(10);
    if (w < 3) {
        *format += std::to_string(Below(30));
    } else if (w == 3) {
        *format += '*';
        words->push_back(uint32_t(int32_t(Below(41)) - 20));
    }
    uint32_t p = Below(12);
    if (p < 2) {
        *format += '.';
    } else if (p < 6) {
        *format += '.' + std::to_string(Below(25));
    } else if (p == 6) {
        *format += '.' + std::to_string(100 + Below(600));
    } else if (p == 7) {
        *format += ".*";
        words->push_back(Below(4) == 0 ? uint32_t(int32_t(Below(10)) - 5) : Below(700));
    }
    switch (type) {
    case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': {
        static const char *const sizes[] = { "", "", "", "h", "l", "I64", "I32", "L", "w" };
        const char *size = sizes[Below(9)];
        *format += size;
        *format += type;
        if (strcmp(size, "I64") == 0) {
            words->push_back(InterestingInt());
            words->push_back(InterestingInt());
        } else {
            words->push_back(InterestingInt());
        }
        break;
    }
    case 'c': case 'C': {
        static const char *const sizes[] = { "", "", "h", "l", "w" };
        *format += sizes[Below(5)];
        *format += type;
        words->push_back(Below(4) == 0 ? 0x100 + Below(0x200) : Below(256));
        break;
    }
    case 'e': case 'E': case 'f': case 'g': case 'G': {
        static const char *const sizes[] = { "", "", "", "l", "L", "h" };
        *format += sizes[Below(6)];
        *format += type;
        PushDouble(words, InterestingDouble());
        break;
    }
    case 's': case 'S': {
        static const char *const sizes[] = { "", "", "h", "l", "w" };
        const char *size = sizes[Below(5)];
        *format += size;
        *format += type;
        bool wide = (type == 'S' && size[0] != 'h') || size[0] == 'l' || size[0] == 'w';
        if (wide) {
            static const uint16_t *const wides[] = { kWide1, kWide2, kWide3, NULL };
            words->push_back(uint32_t(uintptr_t(wides[Below(4)])));
        } else {
            words->push_back(uint32_t(uintptr_t(g_strings[Below(6)])));
        }
        break;
    }
    case 'p':
        *format += type;
        words->push_back(InterestingInt());
        break;
    case 'n':
        *format += Below(2) ? "h" : "";
        *format += type;
        words->push_back(uint32_t(uintptr_t(&g_countSlots[Below(4)])));
        break;
    case 'Z':
        *format += Below(2) ? "w" : "";
        *format += type;
        words->push_back(Below(5) == 0 ? 0 : uint32_t(uintptr_t(&g_counted[Below(3)])));
        break;
    }
}

void RunFormats() {
    g_longString.assign(300, 'L');
    for (int i = 0; i < 300; i += 7)
        g_longString[i] = char('a' + i % 26);
    const char *strings[6] = { "", "x", "Bond", "a longer string, with spaces", g_longString.c_str(), NULL };
    memcpy(g_strings, strings, sizeof(strings));
    g_counted[0] = { 5, 8, "counted string" };
    g_counted[1] = { 8, 8, kWide1 };
    g_counted[2] = { 0, 0, NULL };

    // the game's own format strings, with their kinds of arguments
    struct Fixed {
        const char *format;
        int ints;             // words of int arguments
        int strings;
        int doubles;
    };
    static const Fixed fixed[] = {
        { "%s%s.%s", 0, 3, 0 }, { "%s", 0, 1, 0 }, { "%s%d", 1, 1, 0 }, { "%s:Arm%d", 1, 1, 0 },
        { "%s%s\\%s.tun", 0, 3, 0 }, { "%s%s\\default.tun", 0, 2, 0 }, { "%s{%s}", 0, 2, 0 },
        { "capture.%03d.tga", 1, 0, 0 }, { "data\\track\\%s_S.xsh", 0, 1, 0 }, { "%04d", 1, 0, 0 },
        { "%s.asf", 0, 1, 0 }, { "Validating stream [%s] (%s): ", 0, 2, 0 },
        { "RCameraIniLoader::LoadFile: Unknown camera type loaded: %s - check naming ", 0, 1, 0 },
        { "%p", 1, 0, 0 },
    };
    for (int n = 0; n < 40; n++) {
        for (const Fixed &f : fixed) {
            std::vector<uint32_t> words;
            for (int i = 0; i < f.strings; i++)
                words.push_back(uint32_t(uintptr_t(g_strings[Below(5)])));
            for (int i = 0; i < f.ints; i++)
                words.push_back(InterestingInt());
            words.resize(kWordCount, 0);
            CompareFormat(0, f.format, words.data(), 0);
        }
        // RoadNav's debug line: three ints, four floats as doubles, an int, four more
        std::vector<uint32_t> words;
        for (int i = 0; i < 3; i++)
            words.push_back(InterestingInt());
        for (int i = 0; i < 4; i++)
            PushDouble(&words, double(float(InterestingDouble())));
        words.push_back(InterestingInt());
        words.resize(kWordCount, 0);
        CompareFormat(0, "%d %d %d <%.4f %.4f> <%.4f %.4f> %d", words.data(), 0);
    }
    // the streams' formats: "%[+][#]l{d,u,o,x,X}", "%[+][#]I64{...}", "%[+][#].*[L]{f,e,g}"
    static const char *const prefixes[] = { "%", "%+", "%#", "%+#" };
    static const char *const ints[] = { "ld", "lu", "lo", "lx", "lX", "I64d", "I64u", "I64o", "I64x", "I64X" };
    static const char *const floats[] = { ".*f", ".*e", ".*g", ".*Lf", ".*Le", ".*Lg" };
    for (int n = 0; n < 60; n++) {
        for (const char *prefix : prefixes) {
            for (const char *spec : ints) {
                std::string format = std::string(prefix) + spec;
                uint32_t words[kWordCount] = { InterestingInt(), InterestingInt() };
                CompareFormat(0, format.c_str(), words, 0);
            }
            for (const char *spec : floats) {
                std::string format = std::string(prefix) + spec;
                std::vector<uint32_t> words;
                words.push_back(Below(5) == 0 ? Below(40) : Below(18));
                PushDouble(&words, InterestingDouble());
                words.resize(kWordCount, 0);
                CompareFormat(0, format.c_str(), words.data(), 0);
            }
        }
    }
    // random formats through all three entries
    for (int n = 0; n < 30000; n++) {
        std::string format;
        std::vector<uint32_t> words;
        static const char *const texts[] = { "", " ", "x=", "[", "] ", "text with spaces ", "\xe9", "I" };
        int specs = 1 + int(Below(3));
        for (int i = 0; i < specs; i++) {
            format += texts[Below(8)];
            RandomSpec(&format, &words);
            if (words.size() > kWordCount)
                break;
        }
        if (words.size() > kWordCount)
            continue;
        format += texts[Below(8)];
        words.resize(kWordCount, 0);
        int kind = int(Below(3));
        size_t count = kind == 2 ? Below(4) == 0 ? Below(300) : Below(40) : 0;
        CompareFormat(kind, format.c_str(), words.data(), count);
    }
    // a null buffer with _vsnprintf only counts
    for (int n = 0; n < 200; n++) {
        std::string format;
        std::vector<uint32_t> words;
        RandomSpec(&format, &words);
        if (words.size() > kWordCount)
            continue;
        words.resize(kWordCount, 0);
        static char *none = NULL;
        int results[2];
        for (int side = 0; side < 2; side++) {
            FormatCall call = { 2, side == 0, none, size_t(Below(100)), format.c_str(), words.data(), 0 };
            Side(side == 0, 0x001340f5, CallFormat, &call);
            results[side] = call.result;
        }
        g_cases++;
        Check(results[0] == results[1], "_vsnprintf", "null buffer \"%s\": %d, port %d", Printable(format.c_str()).c_str(),
              results[0], results[1]);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Parsing

typedef double (__cdecl *AtofFn)(const char *);
typedef double (__cdecl *StrtodFn)(const char *, char **);
typedef long (__cdecl *AtolFn)(const char *);
typedef long (__cdecl *StrtolFn)(const char *, char **, int);
typedef unsigned long (__cdecl *StrtoulFn)(const char *, char **, int);
typedef int64_t (__cdecl *Strtoi64Fn)(const char *, char **, int);
typedef uint64_t (__cdecl *Strtoui64Fn)(const char *, char **, int);
typedef int (__cdecl *SscanfFn)(const char *, const char *, ...);

struct NumberCall {
    int kind;             // 0 atof, 1 strtod, 2 atol, 3 strtol, 4 strtoul, 5 _strtoi64, 6 _strtoui64
    bool original;
    const char *text;
    int base;
    char *end;
    uint64_t value;
};

void CallNumber(void *context) {
    NumberCall *c = (NumberCall *)context;
    c->end = NULL;
    switch (c->kind) {
    case 0:
        c->value = Of((c->original ? (AtofFn)0x00133e84 : Crt::atof)(c->text));
        break;
    case 1:
        c->value = Of((c->original ? (StrtodFn)0x00135196 : Crt::strtod)(c->text, &c->end));
        break;
    case 2:
        c->value = uint32_t((c->original ? (AtolFn)0x00133d51 : Crt::atol)(c->text));
        break;
    case 3:
        c->value = uint32_t((c->original ? (StrtolFn)0x00133636 : Crt::strtol)(c->text, &c->end, c->base));
        break;
    case 4:
        c->value = (c->original ? (StrtoulFn)0x0013364d : Crt::strtoul)(c->text, &c->end, c->base);
        break;
    case 5:
        c->value = uint64_t((c->original ? (Strtoi64Fn)0x0013396f : Crt::_strtoi64)(c->text, &c->end, c->base));
        break;
    case 6:
        c->value = (c->original ? (Strtoui64Fn)0x00133986 : Crt::_strtoui64)(c->text, &c->end, c->base);
        break;
    }
}

void CompareNumber(int kind, const char *text, int base) {
    static const uint32_t kAddress[7] = { 0x00133e84, 0, 0x00133d51, 0x00133636, 0x0013364d, 0x0013396f, 0x00133986 };
    static const char *const kNames[7] = { "atof", "strtod", "atol", "strtol", "strtoul", "_strtoi64", "_strtoui64" };
    NumberCall calls[2];
    int errors[2];
    bool ok[2];
    for (int side = 0; side < 2; side++) {
        calls[side] = { kind, side == 0, text, base, NULL, 0 };
        *Crt::_errno() = 0;
        ok[side] = Side(side == 0, kAddress[kind], CallNumber, &calls[side]);
        errors[side] = *Crt::_errno();
    }
    g_cases++;
    Check(ok[0] == ok[1], kNames[kind], "\"%s\": fault original %d port %d", Printable(text).c_str(), !ok[0], !ok[1]);
    if (!ok[0] || !ok[1])
        return;
    Check(calls[0].value == calls[1].value, kNames[kind], "\"%s\" base %d: %016llx, port %016llx",
          Printable(text).c_str(), base, (unsigned long long)calls[0].value, (unsigned long long)calls[1].value);
    Check(calls[0].end == calls[1].end, kNames[kind], "\"%s\" base %d: end +%d, port +%d", Printable(text).c_str(),
          base, int(calls[0].end ? calls[0].end - text : -1), int(calls[1].end ? calls[1].end - text : -1));
    Check(errors[0] == errors[1], kNames[kind], "\"%s\" base %d: errno %d, port %d", Printable(text).c_str(), base,
          errors[0], errors[1]);
}

const int kScanSlot = 640;

struct ScanCall {
    bool original;
    const char *text;
    const char *format;
    uint8_t *slots;       // four of kScanSlot bytes
    int result;
};

void CallScan(void *context) {
    ScanCall *c = (ScanCall *)context;
    SscanfFn fn = c->original ? (SscanfFn)0x00133234 : Crt::sscanf;
    c->result = fn(c->text, c->format, c->slots, c->slots + kScanSlot, c->slots + 2 * kScanSlot,
                   c->slots + 3 * kScanSlot);
}

void CompareScan(const char *text, const char *format) {
    static uint8_t slots[2][4 * kScanSlot];
    ScanCall calls[2];
    int errors[2];
    bool ok[2];
    for (int side = 0; side < 2; side++) {
        memset(slots[side], 0xcd, sizeof(slots[side]));
        calls[side] = { side == 0, text, format, slots[side], 0 };
        *Crt::_errno() = 0;
        ok[side] = Side(side == 0, 0x00133234, CallScan, &calls[side]);
        errors[side] = *Crt::_errno();
    }
    g_cases++;
    Check(ok[0] == ok[1], "sscanf", "\"%s\" \"%s\": fault original %d port %d", Printable(text).c_str(), format, !ok[0],
          !ok[1]);
    if (!ok[0] || !ok[1])
        return;
    Check(calls[0].result == calls[1].result, "sscanf", "\"%s\" \"%s\": %d, port %d", Printable(text).c_str(), format,
          calls[0].result, calls[1].result);
    if (memcmp(slots[0], slots[1], sizeof(slots[0])) != 0) {
        int at = 0;
        while (slots[0][at] == slots[1][at])
            at++;
        Differ("sscanf", "\"%s\" \"%s\": output byte %d %02x, port %02x", Printable(text).c_str(), format, at,
               slots[0][at], slots[1][at]);
    }
    g_checks++;
    Check(errors[0] == errors[1], "sscanf", "\"%s\" \"%s\": errno %d, port %d", Printable(text).c_str(), format,
          errors[0], errors[1]);
}

// Every text file in the driving archives on the host (a file is text when it is nearly all printable).
void ReadTexts(std::vector<std::string> *texts) {
    static const char *const archives[] = { "mis01", "mis11", "mis13a", "mis13b", "mis13c", "mis3", "mis4", "race",
                                            "misc" };
    for (const char *archive : archives) {
        char xboxPath[64], hostPath[MAX_PATH];
        snprintf(xboxPath, sizeof(xboxPath), "D:\\driving\\%s.viv", archive);
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
                    if (size == 0 || size > (2u << 20))
                        continue;
                    std::string data(size, '\0');
                    long back = ftell(file);
                    fseek(file, long(offset), SEEK_SET);
                    bool read = fread(&data[0], 1, size, file) == size;
                    fseek(file, back, SEEK_SET);
                    if (!read)
                        continue;
                    if (size >= 5 && uint8_t(data[1]) == 0xfb) {
                        const uint8_t *packed = (const uint8_t *)data.data();
                        std::string unpacked(unpacksizez(packed), '\0');
                        if (unpacked.empty() || unpacked.size() > (8u << 20))
                            continue;
                        UNPACK_unpack(packed, (uint8_t *)&unpacked[0]);
                        data = unpacked;
                    }
                    size_t printable = 0;
                    for (unsigned char c : data) {
                        if ((c >= 32 && c < 127) || c == '\t' || c == '\r' || c == '\n')
                            printable++;
                    }
                    if (printable * 100 >= data.size() * 98)
                        texts->push_back(data);
                }
            }
        }
        fclose(file);
    }
}

// A random string that is, or nearly is, a number.
std::string RandomNumberText() {
    std::string s;
    static const char *const fixed[] = {
        "9007199254740993", "9007199254740995", "9007199254740997", "18014398509481985", "0.1", "0.3", "1e23",
        "8.98846567431158e307", "1.7976931348623157e308", "1.7976931348623159e308", "2.2250738585072011e-308",
        "2.2250738585072014e-308", "4.9406564584124654e-324", "2.4703282292062327e-324", "1e-400", "1e400",
        "123456789012345678901234567890", "0.000000000000000000000000000001", "0x1A", "0X", "0x", "-0x7fffffff",
        "4294967295", "4294967296", "-2147483648", "-2147483649", "2147483648", "18446744073709551615",
        "18446744073709551616", "-9223372036854775808", "9223372036854775808", "017", "089", "  +12abc", "-",
        "+", ".", "-.", "e5", ".e5", "1e", "1e+", "1e-", "1d5", "1D-5", "1.5E+3", "  \t\n\r 42", "0.0e0",
        "1.000000000000000000000000000001", "3.14159265358979323846264338327950288", "-0", "-0.0", "00000.0001",
        "1,5", "1.2.3", "12e5000", "12e-5000", "1e5201", "1e-5201", "99999999999999999999e-20",
        "0.99999999999999999999", "2.5e-324", "7.4e-324", "1.5", "2.5", "0.5", "16777217", "33554435",
        "0.30000000000000004", "1e308", "1e309", "-1e309",
    };
    if (Below(4) == 0)
        return fixed[Below(sizeof(fixed) / sizeof(fixed[0]))];
    if (Below(5) == 0)
        s += " \t\n"[Below(3)];
    if (Below(3) == 0)
        s += "+-"[Below(2)];
    if (Below(8) == 0) {
        s += Below(2) ? "0x" : "0X";
        int n = int(Below(18));
        for (int i = 0; i < n; i++)
            s += "0123456789abcdefABCDEFgG"[Below(24)];
    } else {
        int n = Below(6) == 0 ? int(Below(40)) : int(Below(10));
        for (int i = 0; i < n; i++)
            s += char('0' + Below(10));
        if (Below(2)) {
            s += '.';
            n = Below(6) == 0 ? int(Below(40)) : int(Below(10));
            for (int i = 0; i < n; i++)
                s += char('0' + Below(10));
        }
        if (Below(3) == 0) {
            s += "eEdD"[Below(4)];
            if (Below(2))
                s += "+-"[Below(2)];
            n = int(Below(5));
            for (int i = 0; i < n; i++)
                s += char('0' + Below(10));
        }
    }
    if (Below(5) == 0)
        s += " ,x.e+-;f"[Below(9)];
    return s;
}

void CompareAllNumbers(const char *text) {
    CompareNumber(0, text, 0);
    CompareNumber(1, text, 0);
    CompareNumber(2, text, 0);
    static const int bases[] = { 0, 10, 16 };
    for (int kind = 3; kind <= 6; kind++) {
        for (int base : bases)
            CompareNumber(kind, text, base);
    }
    static const char *const formats[] = { "%f", "%lf", "%g", "%e", "%d", "%i", "%x", "%o", "%u", "%hd", "%I64d",
                                           "%I64x", "%3d", "%5f", "%c%n", "%s", "%*d%n", "%[0-9.]", "%[^,]", "0x%08x",
                                           "%Lf", "%hf", "%d%n%f" };
    for (const char *format : formats)
        CompareScan(text, format);
}

void RunParsing() {
    std::vector<std::string> texts;
    ReadTexts(&texts);
    std::set<std::string> tokens;
    std::vector<std::string> lines;
    for (const std::string &text : texts) {
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.size() > 300)
                line.resize(300);
            if (!line.empty() && lines.size() < 60000)
                lines.push_back(line);
            start = end + 1;
        }
        std::string token;
        for (size_t i = 0; i <= text.size(); i++) {
            char c = i < text.size() ? text[i] : ' ';
            if (strchr(" \t\r\n,=;\"(){}<>[]:|", c) != NULL || c == '\0') {
                if (!token.empty() && token.size() <= 64 && token.find_first_of("0123456789") != std::string::npos)
                    tokens.insert(token);
                token.clear();
            } else {
                token += c;
            }
        }
    }
    printf("%s %u text files, %u numeric tokens, %u lines\n", kTag, unsigned(texts.size()), unsigned(tokens.size()),
           unsigned(lines.size()));
    fflush(stdout);
    for (const std::string &token : tokens)
        CompareAllNumbers(token.c_str());
    for (int i = 0; i < 6000; i++)
        CompareAllNumbers(RandomNumberText().c_str());

    static const char *const lineFormats[] = { "%[^=]=%f, %d", "#define%31s%d", "%[^:]: %s", "%f,%f,%f,%f",
                                               "%d,%d,%d,%d", "0x%08x", "%f", " %d ,%d", "%s %s %s %s" };
    for (const std::string &line : lines) {
        for (const char *format : lineFormats)
            CompareScan(line.c_str(), format);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// qsort

typedef void (__cdecl *QsortFn)(void *, size_t, size_t, Crt::CompareFn);

const char *g_sortBase;
std::vector<uint32_t> g_sortLog;

int __cdecl CompareKeys(const void *a, const void *b) {
    g_sortLog.push_back(uint32_t((const char *)a - g_sortBase));
    g_sortLog.push_back(uint32_t((const char *)b - g_sortBase));
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

struct SortCall {
    bool original;
    char *base;
    size_t count, width;
};

void CallSort(void *context) {
    SortCall *c = (SortCall *)context;
    g_sortBase = c->base;
    (c->original ? (QsortFn)0x00132db0 : Crt::qsort)(c->base, c->count, c->width, CompareKeys);
}

void RunSort() {
    for (int n = 0; n < 3000; n++) {
        size_t count = Below(10) == 0 ? Below(2000) : Below(60);
        size_t width = 4 * (1 + Below(4));
        int range = 1 + int(Below(4) == 0 ? count * 2 : count / 3 + 1);
        std::vector<char> data(count * width);
        for (size_t i = 0; i < count; i++) {
            int key = int(Below(uint32_t(range)));
            memcpy(&data[i * width], &key, 4);
            for (size_t k = 4; k < width; k += 4)
                memcpy(&data[i * width + k], &i, 4);
        }
        std::vector<char> sorted[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; side++) {
            sorted[side] = data;
            g_sortLog.clear();
            SortCall call = { side == 0, sorted[side].data(), count, width };
            Side(side == 0, 0x00132db0, CallSort, &call);
            logs[side] = g_sortLog;
        }
        g_cases++;
        Check(sorted[0] == sorted[1], "qsort", "%u elements of %u bytes: the order", unsigned(count), unsigned(width));
        Check(logs[0] == logs[1], "qsort", "%u elements of %u bytes: %u comparisons, port %u", unsigned(count),
              unsigned(width), unsigned(logs[0].size() / 2), unsigned(logs[1].size() / 2));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// rand, on two threads at once

typedef int (__cdecl *RandFn)();

struct RandThread {
    RandFn fn;
    int values[1000];
};

DWORD WINAPI RandThreadMain(LPVOID parameter) {
    RandThread *t = (RandThread *)parameter;
    for (int i = 0; i < 1000; i++) {
        t->values[i] = t->fn();
        if ((i & 63) == 0)
            Sleep(0);
    }
    return 0;
}

void RunRand() {
    static RandThread threads[2][2];
    for (int side = 0; side < 2; side++) {
        if (side == 0)
            XbeOriginal_Restore(0x00133ee0, true);
        HANDLE handles[2];
        for (int t = 0; t < 2; t++) {
            threads[side][t].fn = side == 0 ? (RandFn)0x00133ee0 : Crt::rand;
            handles[t] = CreateThread(NULL, 0x10000, RandThreadMain, &threads[side][t], 0, NULL);
        }
        WaitForMultipleObjects(2, handles, TRUE, INFINITE);
        for (int t = 0; t < 2; t++)
            CloseHandle(handles[t]);
        if (side == 0)
            XbeOriginal_Restore(0x00133ee0, false);
    }
    for (int t = 0; t < 2; t++) {
        g_cases++;
        Check(memcmp(threads[0][t].values, threads[1][t].values, sizeof(threads[0][t].values)) == 0, "rand",
              "thread %d: %d %d %d..., port %d %d %d...", t, threads[0][t].values[0], threads[0][t].values[1],
              threads[0][t].values[2], threads[1][t].values[0], threads[1][t].values[1], threads[1][t].values[2]);
    }
    // and on this thread, from wherever its seed is
    uint32_t seed = Crt::PerThread()->randomSeed;
    int values[2][500];
    for (int side = 0; side < 2; side++) {
        Crt::PerThread()->randomSeed = seed;
        if (side == 0)
            XbeOriginal_Restore(0x00133ee0, true);
        for (int i = 0; i < 500; i++)
            values[side][i] = (side == 0 ? (RandFn)0x00133ee0 : Crt::rand)();
        if (side == 0)
            XbeOriginal_Restore(0x00133ee0, false);
    }
    Crt::PerThread()->randomSeed = seed;
    g_cases++;
    Check(memcmp(values[0], values[1], sizeof(values[0])) == 0, "rand", "the main thread's sequence");
}

// ---------------------------------------------------------------------------------------------------------------
// atexit, on lent lists

// The exit list's two words: ours (CRuntimeInternal.h) for the port, the game's copy, which nothing lends
// (tools/owned_globals.py's SKIP), for the original
Crt::AtExitFn *&OnExitBegin(int side) {
    return side == 0 ? *(Crt::AtExitFn **)0x0024b5b4u : Crt::OnExitBegin;
}
Crt::AtExitFn *&OnExitEnd(int side) {
    return side == 0 ? *(Crt::AtExitFn **)0x0024b5b0u : Crt::OnExitEnd;
}

void __cdecl NeverCalled() {
}

typedef int (__cdecl *AtexitFn)(Crt::AtExitFn);

void RunAtexit() {
    Crt::AtExitFn *savedBegin[2] = { OnExitBegin(0), OnExitBegin(1) }, *savedEnd[2] = { OnExitEnd(0), OnExitEnd(1) };
    static const uint32_t sizes[] = { 4, 8, 16, 0x80, 0x84, 0x800, 0x1000, 0x2000 };
    for (uint32_t size : sizes) {
        for (int fill = 0; fill < 3; fill++) {
            uint32_t entries = fill == 0 ? 0 : fill == 1 ? size / 8 : size / 4;
            int results[2];
            size_t lengths[2], sizesAfter[2];
            std::vector<uintptr_t> lists[2];
            for (int side = 0; side < 2; side++) {
                Crt::AtExitFn *list = (Crt::AtExitFn *)Crt::calloc(1, size);
                for (uint32_t i = 0; i < entries; i++)
                    list[i] = (Crt::AtExitFn)(uintptr_t)(0x1000 + i);
                OnExitBegin(side) = list;
                OnExitEnd(side) = list + entries;
                if (side == 0)
                    XbeOriginal_Restore(0x00132a7b, true);
                results[side] = (side == 0 ? (AtexitFn)0x00132a7b : Crt::atexit)(NeverCalled);
                if (side == 0)
                    XbeOriginal_Restore(0x00132a7b, false);
                lengths[side] = size_t(OnExitEnd(side) - OnExitBegin(side));
                sizesAfter[side] = Crt::_msize(OnExitBegin(side));
                lists[side].assign((uintptr_t *)OnExitBegin(side), (uintptr_t *)OnExitEnd(side));
                Crt::free(OnExitBegin(side));
            }
            g_cases++;
            Check(results[0] == results[1], "atexit", "list of %u bytes, %u entries: %d, port %d", size, entries,
                  results[0], results[1]);
            Check(lengths[0] == lengths[1] && sizesAfter[0] == sizesAfter[1] && lists[0] == lists[1], "atexit",
                  "list of %u bytes, %u entries: %u entries in %u bytes, port %u in %u", size, entries,
                  unsigned(lengths[0]), unsigned(sizesAfter[0]), unsigned(lengths[1]), unsigned(sizesAfter[1]));
        }
    }
    for (int side = 0; side < 2; side++) {
        OnExitBegin(side) = savedBegin[side];
        OnExitEnd(side) = savedEnd[side];
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Characters

typedef int (__cdecl *CharFn)(int);

void RunCharacters() {
    static const uint32_t kAddress[3] = { 0x0013399d, 0x001339ef, 0x001327f9 };
    static const char *const kNames[3] = { "isdigit", "isalnum", "tolower" };
    CharFn ours[3] = { Crt::isdigit, Crt::isalnum, Crt::tolower };
    for (int f = 0; f < 3; f++) {
        int results[2][385];
        for (int side = 0; side < 2; side++) {
            if (side == 0)
                XbeOriginal_Restore(kAddress[f], true);
            CharFn fn = side == 0 ? (CharFn)kAddress[f] : ours[f];
            for (int c = -128; c <= 256; c++)
                results[side][c + 128] = fn(c);
            if (side == 0)
                XbeOriginal_Restore(kAddress[f], false);
        }
        for (int c = -128; c <= 256; c++) {
            g_cases++;
            Check(results[0][c + 128] == results[1][c + 128], kNames[f], "%d: %d, port %d", c, results[0][c + 128],
                  results[1][c + 128]);
        }
    }
    typedef Crt::Lconv *(__cdecl *LocaleconvFn)();
    typedef int *(__cdecl *ErrnoFn)();
    Crt::Lconv *conventions[2];
    int *errors[2];
    XbeOriginal_Restore(0x001333d6, true);
    conventions[0] = ((LocaleconvFn)0x001333d6)();
    XbeOriginal_Restore(0x001333d6, false);
    XbeOriginal_Restore(0x00133664, true);
    errors[0] = ((ErrnoFn)0x00133664)();
    XbeOriginal_Restore(0x00133664, false);
    conventions[1] = Crt::localeconv();
    errors[1] = Crt::_errno();
    g_cases += 2;
    // The C locale's table: the game's copy of it for the original, ours for the port - compared by what it holds
    const Crt::Lconv &o = *conventions[0], &p = *conventions[1];
    const char *const strings[2][10] = {
        { o.decimalPoint, o.thousandsSep, o.grouping, o.intCurrencySymbol, o.currencySymbol, o.monDecimalPoint,
          o.monThousandsSep, o.monGrouping, o.positiveSign, o.negativeSign },
        { p.decimalPoint, p.thousandsSep, p.grouping, p.intCurrencySymbol, p.currencySymbol, p.monDecimalPoint,
          p.monThousandsSep, p.monGrouping, p.positiveSign, p.negativeSign } };
    bool same = memcmp(&o.intFracDigits, &p.intFracDigits, 8) == 0;
    for (int i = 0; i < 10; i++)
        same = same && strcmp(strings[0][i], strings[1][i]) == 0;
    Check(same, "localeconv", "the tables at %p and, port, %p hold different conventions", (void *)conventions[0],
          (void *)conventions[1]);
    Check(errors[0] == errors[1], "_errno", "%p, port %p", (void *)errors[0], (void *)errors[1]);
}

// ---------------------------------------------------------------------------------------------------------------
// The x87

// fn with x in ST0 (one operand) or base in ST1 and exponent in ST0 (two), the 80-bit answer stored.
__declspec(naked) void CallX87Unary(uint32_t fn, const double *x, uint8_t *out) {
    __asm {
        mov eax, dword ptr [esp + 8]
        fld qword ptr [eax]
        call dword ptr [esp + 4]
        mov eax, dword ptr [esp + 12]
        fstp tbyte ptr [eax]
        ret
    }
}

__declspec(naked) void CallX87Binary(uint32_t fn, const double *x, const double *y, uint8_t *out) {
    __asm {
        mov eax, dword ptr [esp + 8]
        fld qword ptr [eax]
        mov eax, dword ptr [esp + 12]
        fld qword ptr [eax]
        call dword ptr [esp + 4]
        mov eax, dword ptr [esp + 16]
        fstp tbyte ptr [eax]
        ret
    }
}

__declspec(naked) void RaiseX87(int which) {
    __asm {
        cmp dword ptr [esp + 4], 1
        je divide
        cmp dword ptr [esp + 4], 2
        je invalid
        ret
    divide:
        fld1
        fldz
        fdivp st(1), st
        fstp st(0)
        ret
    invalid:
        fld1
        fchs
        fsqrt
        fstp st(0)
        ret
    }
}

__declspec(naked) unsigned X87Status() {
    __asm {
        push eax
        fnstsw word ptr [esp]
        pop eax
        and eax, 0xffff
        ret
    }
}

struct X87Call {
    uint32_t fn;
    double x, y;
    bool binary;
    uint8_t out[10];
};

void CallX87(void *context) {
    X87Call *c = (X87Call *)context;
    if (c->binary)
        CallX87Binary(c->fn, &c->x, &c->y, c->out);
    else
        CallX87Unary(c->fn, &c->x, c->out);
}

double X87Operand(bool unit) {
    static const uint64_t specials[] = { 0x0000000000000000ull, 0x8000000000000000ull, 0x3ff0000000000000ull,
                                         0xbff0000000000000ull, 0x3ff0000000000001ull, 0x3fefffffffffffffull,
                                         0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000000ull,
                                         0x7ff4000000000000ull, 0x0000000000000001ull, 0x4000000000000000ull,
                                         0x3fe0000000000000ull, 0xc000000000000000ull, 0x7fefffffffffffffull };
    uint32_t r = Below(10);
    if (r == 0)
        return Bits64(specials[Below(sizeof(specials) / sizeof(specials[0]))]);
    if (unit)
        return (double(int32_t(Next())) / 2147483648.0) * (r < 8 ? 1.0 : 1.25);
    if (r < 4)
        return double(int32_t(Below(41)) - 20);
    if (r < 7)
        return double(Next()) / 4294967296.0 * 4.0;
    return (double(int32_t(Next())) / 65536.0);
}

void RunX87() {
#if NIGHTFIRE_X87   // off, the runtime's _CI functions are C taking doubles: nothing here to compare
    static const uint32_t kAddress[3] = { 0x001328d0, 0x00133f94, 0x00132a94 };
    static const char *const kNames[3] = { "_CIacos", "_CIasin", "_CIpow" };
    void (*ours[3])() = { Crt::CIacos, Crt::CIasin, Crt::CIpow };
    for (int f = 0; f < 3; f++) {
        for (int n = 0; n < 20000; n++) {
            X87Call calls[2];
            int errors[2];
            double x = X87Operand(f < 2), y = X87Operand(false);
            for (int side = 0; side < 2; side++) {
                calls[side].fn = side == 0 ? kAddress[f] : (uint32_t)(uintptr_t)ours[f];
                calls[side].x = x;
                calls[side].y = y;
                calls[side].binary = f == 2;
                memset(calls[side].out, 0, 10);
                *Crt::_errno() = 0;
                Side(side == 0, kAddress[f], CallX87, &calls[side]);
                errors[side] = *Crt::_errno();
            }
            bool same = memcmp(calls[0].out, calls[1].out, 10) == 0 && errors[0] == errors[1];
            g_cases++;
            Check(same, kNames[f], "%.17g, %.17g: %02x%02x %02x%02x%02x%02x%02x%02x%02x%02x errno %d, port "
                  "%02x%02x %02x%02x%02x%02x%02x%02x%02x%02x errno %d", x, y,
                  calls[0].out[9], calls[0].out[8], calls[0].out[7], calls[0].out[6], calls[0].out[5], calls[0].out[4],
                  calls[0].out[3], calls[0].out[2], calls[0].out[1], calls[0].out[0], errors[0],
                  calls[1].out[9], calls[1].out[8], calls[1].out[7], calls[1].out[6], calls[1].out[5], calls[1].out[4],
                  calls[1].out[3], calls[1].out[2], calls[1].out[1], calls[1].out[0], errors[1]);
        }
    }

    typedef unsigned (__cdecl *ClearFn)();
    for (int which = 0; which < 3; which++) {
        unsigned results[2], after[2];
        for (int side = 0; side < 2; side++) {
            Crt::_clearfp();
            RaiseX87(which);
            if (side == 0)
                XbeOriginal_Restore(0x00133c9e, true);
            results[side] = (side == 0 ? (ClearFn)0x00133c9e : Crt::_clearfp)();
            if (side == 0)
                XbeOriginal_Restore(0x00133c9e, false);
            after[side] = X87Status() & 0x3f;
        }
        g_cases++;
        Check(results[0] == results[1] && after[0] == after[1], "_clearfp", "exception %d: %08x (then %02x), port %08x "
              "(then %02x)", which, results[0], after[0], results[1], after[1]);
    }
#endif
}

bool Enabled() {
    const char *value = getenv("NIGHTFIRE_CRUNTIMESHADOWA");
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

void CRuntimeShadowA_RunHeap(void) {
    if (!Enabled())
        return;
    g_cases = g_checks = g_differ = g_faults = g_details = 0;
    std::vector<HeapOp> script = HeapScript();
    HANDLE savedHeap = ProcessHeapHandle, savedGameHeap = GameProcessHeapHandle;
    HeapRun runs[2];
    for (int side = 0; side < 2; side++) {
        HANDLE lent = HeapCreate(HEAP_NO_SERIALIZE, 0x10000, 0x1000000);   // one fixed segment: offsets repeat
        ProcessHeapHandle = lent;
        GameProcessHeapHandle = lent;
        runs[side].original = side == 0;
        runs[side].ops = &script;
        memset(runs[side].slots, 0, sizeof(runs[side].slots));
        static const uint32_t kSwapped[3] = { 0x001340e3, 0x001331dc, 0x0013439d };
        if (side == 0) {
            for (uint32_t address : kSwapped)
                XbeOriginal_Restore(address, true);
        }
        Guard(RunHeapScript, &runs[side]);
        if (side == 0) {
            for (uint32_t address : kSwapped)
                XbeOriginal_Restore(address, false);
        }
        HeapDestroy(lent);
    }
    ProcessHeapHandle = savedHeap;
    GameProcessHeapHandle = savedGameHeap;
    static const char *const kNames[5] = { "malloc", "calloc", "realloc", "free", "_msize" };
    size_t count = runs[0].results.size() < runs[1].results.size() ? runs[0].results.size() : runs[1].results.size();
    Check(runs[0].results.size() == runs[1].results.size(), "heap", "%u results, port %u",
          unsigned(runs[0].results.size()), unsigned(runs[1].results.size()));
    for (size_t i = 0; i < count; i++) {
        const HeapResult &a = runs[0].results[i], &b = runs[1].results[i];
        const HeapOp &op = script[i];
        g_cases++;
        Check(a.offset == b.offset && a.size == b.size && a.zeroed == b.zeroed, kNames[op.kind],
              "step %u (%u, %u): +%x size %u zeroed %d, port +%x size %u zeroed %d", unsigned(i), op.a, op.b,
              unsigned(a.offset), unsigned(a.size), a.zeroed, unsigned(b.offset), unsigned(b.size), b.zeroed);
    }
    Summary("heap: malloc, free, calloc, realloc, _msize on a lent heap");
}

void CRuntimeShadowA_Run(void) {
    if (!Enabled())
        return;
    g_cases = g_checks = g_differ = g_faults = g_details = 0;
    // NIGHTFIRE_CRUNTIMESHADOWA=2 reports each part as it finishes
    bool verbose = getenv("NIGHTFIRE_CRUNTIMESHADOWA")[0] == '2';
    // The original printf and scanf convert floats through the _cfltcvt pointers, which the startup no longer sets
    // (our runtime calls the conversions directly): without them the originals stop with R6002. _cfltcvt_init writes
    // only those pointers, which nothing of ours reads.
    ((void (__cdecl *)(void))0x00131cf0)();
    static const struct {
        const char *name;
        void (*run)();
    } parts[] = {
        { "characters", RunCharacters }, { "x87", RunX87 }, { "rand", RunRand }, { "atexit", RunAtexit },
        { "qsort", RunSort }, { "formats", RunFormats }, { "parsing", RunParsing },
    };
    for (const auto &part : parts) {
        part.run();
        if (verbose)
            Summary(part.name);
    }
    Summary("printf, scanf, atof, strtod, atol, strtol, qsort, rand, atexit, ctype, x87");
}
