#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "CarpShadow.h"

#include "../data/Carp.h"
#include "../data/Dafi.h"
#include "../data/SymbolTable.h"
#include "../data/UData.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../platform/FileSys.h"
#include "../platform/RefPack.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_CARPSHADOW=1, once after start-up: the data layer's ports (src/driving/data/) against the originals.
//
// Every test runs twice, calling everything at the original addresses: first with the originals swapped back in
// (XbeOriginal_RestoreRange over the package's ranges), then with our jumps in place. Each side writes a log of
// what it saw - results, pointers as offsets into the buffers they point into, output bytes as hex - and the two
// logs must match line for line. Buffers the code writes into are the same buffers, refilled, for both sides, so
// pointers into them compare as they are.
//
//   - DAFI: every .atr and .ini file in every archive on the disc, as shipped and perturbed (line ends, spacing,
//     comments, truncation): every section and key indexed, every lookup by name (and a case-flipped and a
//     missing one), the text copy's bytes after parsing. Sections whose parse would never end (a line without
//     '=', which the original loops on) are left unselected.
//   - StringToNumber: the game's two tables and random ones above and below the 30-entry line: the sorted copies
//     and every conversion, plus names not in the table.
//   - Symbol tables: scripted AddNamespace / RemoveNamespace / NameLookup runs on fresh tables, the tree's shape
//     and colours logged after every step; UCarpNamespace's map through InsertUnique and EraseRange (random
//     ranges and the whole tree); ParseNameAndTag and UDataGroupDecodeTag on generated names.
//   - CARP: every .crp file: deserialised, given a private symbol table ("CHAR", "DATA", and a "CARP" namespace
//     with the file's own groups), looked up by many names, resolved (ResolveSymbolicReferences: both passes,
//     every resolver), the whole image compared. Then on the resolved file: TagReference on its groups, every AI
//     spline's path evaluated (EvaluateMatrix, Linear, Spline, FindKey, ScaleToUnitTime) at key times, between
//     them, outside them and at NaN with random hints, GetApplyTransform; every BaseDesc's damage zones at
//     random and boundary points; SetDimensions on copies of every Instance with random dimensions.
//   - Synthetic paths with every channel kind (linear, quaternion, scalar, stepped, Bezier, Euler Bezier, equal
//     key times), RotateSplineAboutBase and ComputeLinear on random matrices and vectors.
//
// Not covered here (stateful, tested in game): RCARPFile and its loader, RegisterSymbols, the callbacks,
// GlobalSymbolTable, UFileLoader and its lists, and the singleton vector.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_checks, g_diffs, g_faults;
std::string *g_log;

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

void LogBytes(const char *what, const void *p, size_t n) {
    std::string s = what;
    s += ' ';
    char h[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(h, sizeof(h), "%02x", static_cast<const uint8_t *>(p)[i]);
        s += h;
    }
    Logf("%s", s.c_str());
}

// ---- the two sides

const unsigned kRanges[][2] = {
    { 0x0007aef0, 0x0007c160 }, { 0x0008baa0, 0x0008bab0 }, { 0x001177b0, 0x00117940 },
    { 0x00117df0, 0x0011bb50 },
};

struct Originals {
    bool on;
    explicit Originals(bool original) : on(original) {
        if (on)
            for (auto &r : kRanges)
                XbeOriginal_RestoreRange(r[0], r[1], true);
    }
    ~Originals() {
        if (on)
            for (auto &r : kRanges)
                XbeOriginal_RestoreRange(r[0], r[1], false);
    }
};

bool Safe(void (*thunk)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        thunk(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    thunk(context);
    return true;
#endif
}

template <class F> void Thunk(void *f) {
    (*static_cast<F *>(f))();
}

int g_side;   // 0 the originals, 1 the ports

void CompareLogs(const char *what, const std::string &o, const std::string &p, bool okO, bool okP) {
    g_cases++;
    size_t lines = 0;
    for (char c : o)
        lines += c == '\n';
    g_checks += int(lines) + 1;
    if (okO != okP) {
        if (g_diffs++ < 10)
            printf("[carpshadow] %s: %s faulted, %s did not\n", what, okO ? "port" : "original", okO ? "original" : "port");
        return;
    }
    if (o == p)
        return;
    size_t i = 0, start = 0;
    while (i < o.size() && i < p.size() && o[i] == p[i]) {
        if (o[i] == '\n')
            start = i + 1;
        i++;
    }
    if (g_diffs++ < 10) {
        size_t eo = o.find('\n', start), ep = p.find('\n', start);
        printf("[carpshadow] %s differs:\n    original: %.200s\n    port:     %.200s\n", what,
               o.substr(start, eo == std::string::npos ? std::string::npos : eo - start).c_str(),
               p.substr(start, ep == std::string::npos ? std::string::npos : ep - start).c_str());
    }
}

uint32_t g_seed = 0x2545f491;

// Both sides start from the same random state, so a side may draw numbers too.
template <class F> void Both(const char *what, F f) {
    std::string logs[2];
    bool ok[2];
    uint32_t seed = g_seed;
    for (int side = 0; side < 2; side++) {
        g_seed = seed;
        g_side = side;
        g_log = &logs[side];
        Originals originals(side == 0);
        ok[side] = Safe(Thunk<F>, &f);
    }
    g_log = NULL;
    CompareLogs(what, logs[0], logs[1], ok[0], ok[1]);
}

// ---- randomness

uint32_t Random() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

uint32_t Random(uint32_t n) {
    return n == 0 ? 0 : Random() % n;
}

float RandomFloat(float lo, float hi) {
    return lo + (hi - lo) * float(Random() & 0xffffff) / float(0x1000000);
}

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

uint32_t BitsOf(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint64_t BitsOf(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

// ---- the originals' addresses (called for both sides; the ranges decide which code runs)

typedef DAFI *(*DafiOpenFn)(const char *, int);
typedef void (*DafiVoidFn)(DAFI *);
typedef void (*DafiIndexFn)(DAFI *, int);
typedef int (*DafiCountFn)(DAFI *);
typedef int (*DafiNameIndexFn)(DAFI *, const char *);
typedef char *(*DafiIndexNameFn)(DAFI *, int);
typedef char *(*DafiNameValueFn)(DAFI *, const char *);
#define Dafi_open ((DafiOpenFn)0x0011a240)
#define Dafi_close ((DafiVoidFn)0x0011a370)
#define Dafi_setsectionbyindex ((DafiIndexFn)0x0011a390)
#define Dafi_getsectioncount ((DafiCountFn)0x0011a5b0)
#define Dafi_getsectionindex ((DafiNameIndexFn)0x0011a5c0)
#define Dafi_getsectionbyindex ((DafiIndexNameFn)0x0011a630)
#define Dafi_getkeycount ((DafiCountFn)0x0011a650)
#define Dafi_getkeyindex ((DafiNameIndexFn)0x0011a660)
#define Dafi_getkeybyindex ((DafiIndexNameFn)0x0011a6b0)
#define Dafi_getvaluebyindex ((DafiIndexNameFn)0x0011a6c0)
#define Dafi_setsection ((DafiNameIndexFn)0x0011a6d0)
#define Dafi_getvalue ((DafiNameValueFn)0x0011a700)

typedef StringToNumber *(__fastcall *StnConstructFn)(StringToNumber *, int, StringToNumberEntry *);
typedef void (__fastcall *StnDestructFn)(StringToNumber *, int);
typedef int (__fastcall *StnLookupFn)(StringToNumber *, int, const char *);
#define Stn_Construct ((StnConstructFn)0x00119f50)
#define Stn_Destruct ((StnDestructFn)0x0011a020)
#define Stn_BinarySearch ((StnLookupFn)0x0011a040)
#define Stn_Convert ((StnLookupFn)0x0011a110)

typedef uint32_t (*DecodeTagFn)(const char *);
typedef bool (*ParseNameFn)(const char *, char *, uint32_t *, int *);
#define Data_DecodeTag ((DecodeTagFn)0x001177b0)
#define Carp_ParseNameAndTag ((ParseNameFn)0x0011a750)

typedef USymbolTable *(__fastcall *TableConstructFn)(USymbolTable *, int);
typedef USymbolTable *(__fastcall *TableDeleteFn)(USymbolTable *, int, unsigned);
typedef void (__fastcall *TableAddFn)(USymbolTable *, int, const char *, SymbolNamespace *);
typedef SymbolNamespace *(__fastcall *TableRemoveFn)(USymbolTable *, int, const char *);
typedef void *(__fastcall *TableLookupFn)(USymbolTable *, int, const char *, int *);
#define Table_Construct ((TableConstructFn)0x0011b580)
#define Table_Delete ((TableDeleteFn)0x0011b650)
#define Table_AddNamespace ((TableAddFn)0x0011b510)
#define Table_RemoveNamespace ((TableRemoveFn)0x0011b110)
#define Table_NameLookup ((TableLookupFn)0x0011a950)

typedef UCharNamespace *(__fastcall *CharConstructFn)(UCharNamespace *, int);
typedef void (__fastcall *CharDestructFn)(UCharNamespace *, int);
#define Char_Construct ((CharConstructFn)0x0011a800)
#define Char_Destruct ((CharDestructFn)0x0011a810)

typedef UCarpNamespace *(__fastcall *CarpConstructFn)(UCarpNamespace *, int);
typedef void (__fastcall *CarpDestructFn)(UCarpNamespace *, int);
typedef void (__fastcall *CarpAddFn)(UCarpNamespace *, int, UGroup *);
typedef TreeInsertResult *(__fastcall *CarpInsertFn)(CarpGroupMap *, int, TreeInsertResult *, const TreePair *);
typedef TreeNode **(__fastcall *EraseRangeFn)(Tree *, int, TreeNode **, TreeNode *, TreeNode *);
#define Carp_Construct ((CarpConstructFn)0x0011b690)
#define Carp_Destruct ((CarpDestructFn)0x0011b730)
#define Carp_AddCarpFile ((CarpAddFn)0x0011b390)
#define CarpMap_InsertUnique ((CarpInsertFn)0x0011b180)
#define CarpMap_EraseRange ((EraseRangeFn)0x0011b2d0)
#define NamespaceMap_EraseRange ((EraseRangeFn)0x0011ae70)

typedef uint32_t (*ResolveFn)(UGroup *, USymbolTable *);
typedef CARP::TagReference *(__fastcall *TagRefFn)(CARP::TagReference *, int, UGroup *, UGroup *);
#define Carp_ResolveSymbolicReferences ((ResolveFn)0x00119d80)
#define Carp_TagReference ((TagRefFn)0x00117e50)

typedef void (__fastcall *EvalMatrixFn)(CARP::PathInfo *, int, float, uint32_t *, uint32_t *, float *, float *);
typedef uint32_t (__fastcall *EvalFn)(CARP::PathInfo *, int, float, uint32_t, CARP::PathChannel *, float *);
typedef uint32_t (__fastcall *FindKeyFn)(CARP::PathInfo *, int, float, uint32_t, CARP::PathChannel *);
typedef double (__fastcall *ScaleFn)(CARP::PathInfo *, int, float, uint32_t, uint32_t, CARP::PathChannel *);
typedef void (*ComputeLinearFn)(const float *, const float *, float *, float, bool);
typedef void (__fastcall *ApplyFn)(CARP::AISpline *, int, float *);
typedef void (*RotateFn)(const float *, const float *, const float *, float *);
typedef int (__fastcall *ZoneFn)(CARP::BaseDesc *, int, const float *);
typedef int (__fastcall *ZoneBitsFn)(CARP::BaseDesc *, int, int);
typedef void (__fastcall *DimensionsFn)(CARP::Instance *, int, bool, float, float, float);
#define Path_EvaluateMatrix ((EvalMatrixFn)0x001192d0)
#define Path_EvaluateLinear ((EvalFn)0x001190e0)
#define Path_EvaluateSpline ((EvalFn)0x00119190)
#define Path_FindKey ((FindKeyFn)0x00119010)
#define Path_ScaleToUnitTime ((ScaleFn)0x00118e30)
#define Path_ComputeLinear ((ComputeLinearFn)0x00118dd0)
#define Spline_GetApplyTransform ((ApplyFn)0x001193c0)
#define Carp_RotateSplineAboutBase ((RotateFn)0x00117ef0)
#define Base_CalcDamageZone ((ZoneFn)0x00118030)
#define Base_GetZoneBits ((ZoneBitsFn)0x00118170)
#define Instance_SetDimensions ((DimensionsFn)0x00118cc0)

#define CarpVerbose (*(uint8_t *)0x00243561)

// ---- the disc

struct DiscFile {
    std::string name;
    std::vector<uint8_t> data;
};

bool HasExt(const std::string &name, const char *ext) {
    size_t n = strlen(ext);
    return name.size() > n && _stricmp(name.c_str() + name.size() - n, ext) == 0;
}

// Every file with one of the extensions in every archive, once per distinct content.
void ReadDisc(const char *const *exts, int extCount, std::vector<DiscFile> *out) {
    char folder[MAX_PATH], pattern[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving", folder, sizeof(folder)))
        return;
    snprintf(pattern, sizeof(pattern), "%s\\*.viv", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        uint8_t header[16];
        DWORD got = 0;
        ReadFile(file, header, 16, &got, NULL);
        int dirSize = got == 16 ? BIG_dirsize(header) : 0;
        std::vector<uint8_t> dir(dirSize > 16 ? dirSize : 16);
        SetFilePointer(file, 0, NULL, FILE_BEGIN);
        ReadFile(file, dir.data(), DWORD(dirSize), &got, NULL);
        for (int i = 0;; i++) {
            int offset, size;
            const char *name = BIG_find(dir.data(), NULL, i, &offset, &size);
            if (name == NULL)
                break;
            std::string n = name;
            bool wanted = false;
            for (int e = 0; e < extCount; e++)
                wanted |= HasExt(n, exts[e]);
            if (!wanted)
                continue;
            std::vector<uint8_t> raw(size + 16);
            SetFilePointer(file, offset, NULL, FILE_BEGIN);
            ReadFile(file, raw.data(), DWORD(size), &got, NULL);
            DiscFile f;
            f.name = n;
            unsigned unpacked = unpacksizez(raw.data());
            if (unpacked != 0) {
                f.data.assign(unpacked + 64, 0);
                UNPACK_unpack(raw.data(), f.data.data());
                f.data.resize(unpacked);
            } else {
                f.data.assign(raw.begin(), raw.begin() + size);
            }
            bool seen = false;
            for (const DiscFile &o : *out)
                seen |= o.name == f.name && o.data == f.data;
            if (!seen)
                out->push_back(f);
        }
        CloseHandle(file);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

// =============================================================================================================
// DAFI
// =============================================================================================================

// Whether selecting the section is unsafe, for the original and the port alike:
//   - a key line with no '=' makes the parse start the same line over, forever;
//   - text ending inside a key (a truncated file) is taken as a key ending at the terminator, and the parse
//     steps past it: it reads and writes text[size + 1], one byte beyond the block DAFI_open allocated. On the
//     game's heap that byte is slack; here it can be a heap header.
// Decided by the port's own parse, on a padded scratch copy of the text.
bool DafiWouldLoop(const DAFI *dafi, int section) {
    const char *body = dafi->sectionBodies[section];
    if (body == NULL)
        return false;
    size_t size = size_t(dafi->textEnd - dafi->text);
    std::vector<char> copy(size + 16, 0);
    memcpy(copy.data(), dafi->text, size + 1);
    char *s = copy.data() + (body - dafi->text);
    char *end = copy.data() + size;
    int keys = 0;
    while (*s != '[' && keys < kDafiMaxKeys && s < end) {
        char *key = s;
        if (*s != '=') {
            for (;;) {
                if (*s == '\n' || *s == '\r' || *s == 0 || s >= end)
                    break;
                char next = s[1];
                s++;
                if (next == '=')
                    break;
            }
        }
        if (*s != '=' && *s != 0)
            return true;
        *s = 0;
        DafiTrim(key);
        s++;
        if (s > end)
            return true;   // the key ran to the end of the text
        if (*s == 0)
            while (s < end) {
                char next = s[1];
                s++;
                if (next != 0)
                    break;
            }
        char *value = s;
        if ((*s == '\n' || *s == '\r' || *s == 0) && s < end) {
            *s = ' ';
            s++;
        }
        if (*s != '\n')
            for (;;) {
                char c = *s;
                if (c == '\r' || c == ';' || c == 0 || s >= end)
                    break;
                char next = s[1];
                s++;
                if (next == '\n')
                    break;
            }
        *s = 0;
        DafiTrim(value);
        s = DafiNextLine(s, end);
        keys++;
        if (s == NULL)
            break;
    }
    return false;
}

std::string Flipped(const char *s) {
    std::string out = s;
    for (char &c : out)
        c = char(isupper((unsigned char)c) ? tolower((unsigned char)c) : toupper((unsigned char)c));
    return out;
}

void DafiCheck(const char *name, const std::vector<char> &text) {
    // which sections are safe to select: decided once, from the port's open, outside the two sides
    std::vector<bool> loops;
    {
        DAFI *probe = DAFI_open(text.data(), int(text.size()));
        if (probe == NULL)
            return;
        for (int i = 0; i < probe->sectionCount; i++)
            loops.push_back(DafiWouldLoop(probe, i));
        DAFI_close(probe);
    }
    Both(name, [&]() {
        DAFI *d = Dafi_open(text.data(), int(text.size()));
        if (d == NULL) {
            Logf("open null");
            return;
        }
        auto off = [&](const char *p) -> long { return p == NULL ? -1 : long(p - d->text); };
        int count = Dafi_getsectioncount(d);
        Logf("sections %d current %d end %ld", count, d->currentSection, off(d->textEnd));
        for (int i = 0; i < count; i++) {
            char *section = Dafi_getsectionbyindex(d, i);
            Logf("[%d] %s @%ld body @%ld", i, section != NULL ? section : "(null)", off(section),
                 off(d->sectionBodies[i]));
        }
        for (int i = 0; i < count && i < int(loops.size()); i++) {
            if (loops[i]) {
                Logf("[%d] skipped: unsafe for both (a line without '=', or a key cut off by the end)", i);
                continue;
            }
            Dafi_setsectionbyindex(d, i);
            int keys = Dafi_getkeycount(d);
            Logf("[%d] keys %d current %s", i, keys, Dafi_getsectionbyindex(d, -1) ? "named" : "unnamed");
            for (int k = 0; k < keys; k++) {
                char *key = Dafi_getkeybyindex(d, k);
                char *value = Dafi_getvaluebyindex(d, k);
                Logf("  %s = %s @%ld @%ld", key ? key : "(null)", value ? value : "(null)", off(key), off(value));
                if (key != NULL) {
                    char *found = Dafi_getvalue(d, key);
                    std::string flipped = Flipped(key);
                    Logf("  index %d %d value @%ld", Dafi_getkeyindex(d, key), Dafi_getkeyindex(d, flipped.c_str()),
                         off(found));
                }
            }
            Logf("  missing %d %ld", Dafi_getkeyindex(d, "zz_not_a_key"), off(Dafi_getvalue(d, "zz_not_a_key")));
        }
        for (int i = 0; i < count; i++) {
            char *section = d->sectionNames[i];
            if (section == NULL || loops[i])
                continue;
            std::string copy = section, flipped = Flipped(section);
            int index = Dafi_getsectionindex(d, flipped.c_str());
            Logf("by name %d %d", Dafi_getsectionindex(d, copy.c_str()), index);
            if (index >= 0 && index < int(loops.size()) && !loops[index])   // the first of that name may loop
                Logf("set %d keys %d", Dafi_setsection(d, flipped.c_str()), Dafi_getkeycount(d));
        }
        Logf("missing section %d current %d", Dafi_setsection(d, "zz_not_a_section"), Dafi_getsectionindex(d, NULL));
        LogBytes("text", d->text, size_t(d->textEnd - d->text) + 1);
        Dafi_close(d);
    });
}

void DafiPerturb(const std::vector<char> &in, int kind, std::vector<char> *out) {
    out->clear();
    switch (kind) {
    case 1:   // line ends: CR LF to LF
        for (size_t i = 0; i < in.size(); i++)
            if (!(in[i] == '\r' && i + 1 < in.size() && in[i + 1] == '\n'))
                out->push_back(in[i]);
        break;
    case 2:   // spacing around '=' and at line ends, tabs
        for (char c : in) {
            if (c == '=') {
                out->push_back(Random(2) ? '\t' : ' ');
                out->push_back('=');
                out->push_back(' ');
            } else if (c == '\n') {
                out->push_back(' ');
                out->push_back('\n');
            } else {
                out->push_back(c);
            }
        }
        break;
    case 3:   // comments after values, an empty value or two
        for (size_t i = 0; i < in.size(); i++) {
            if (in[i] == '\n' && i > 0 && in[i - 1] != '\n' && in[i - 1] != ']' && Random(3) == 0) {
                const char *comment = " ; note";
                out->insert(out->end(), comment, comment + strlen(comment));
            }
            out->push_back(in[i]);
        }
        break;
    case 4:   // truncated anywhere
        out->assign(in.begin(), in.begin() + (in.empty() ? 0 : Random(uint32_t(in.size()))));
        break;
    default:
        *out = in;
        break;
    }
}

void RunDafi() {
    static const char *const kExts[] = { ".atr", ".ini" };
    std::vector<DiscFile> files;
    ReadDisc(kExts, 2, &files);
    printf("[carpshadow] DAFI: %d files\n", int(files.size()));
    fflush(stdout);
    for (const DiscFile &f : files) {
        std::vector<char> text(f.data.begin(), f.data.end()), variant;
        for (int kind = 0; kind < 5; kind++) {
            DafiPerturb(text, kind, &variant);
            char what[300];
            snprintf(what, sizeof(what), "DAFI %s (%d)", f.name.c_str(), kind);
            DafiCheck(what, variant);
        }
    }
}

// =============================================================================================================
// StringToNumber, tags and names
// =============================================================================================================

void StnCheck(const char *what, StringToNumberEntry *table, const std::vector<std::string> &probes) {
    Both(what, [&]() {
        StringToNumber s;
        memset(&s, 0xcd, sizeof(s));
        Stn_Construct(&s, 0, table);
        Logf("count %d table %d sorted %d", s.count, s.table == table, s.sortedByString != NULL);
        if (s.sortedByString != NULL)
            for (int i = 0; i < s.count; i++)
                Logf("%d %d %s | %d %s", i, s.sortedByString[i].number, s.sortedByString[i].string,
                     s.sortedByNumber[i].number, s.sortedByNumber[i].string);
        for (int i = 0; i < s.count; i++)
            Logf("%s -> %d %d", table[i].string, Stn_Convert(&s, 0, table[i].string),
                 s.sortedByString != NULL ? Stn_BinarySearch(&s, 0, table[i].string) : -2);
        for (const std::string &p : probes)
            Logf("%s -> %d %d", p.c_str(), Stn_Convert(&s, 0, p.c_str()),
                 s.sortedByString != NULL ? Stn_BinarySearch(&s, 0, p.c_str()) : -2);
        Stn_Destruct(&s, 0);
    });
}

void RunStringToNumber() {
    const uint32_t kTables[] = { 0x001b6ba0, 0x001b6fd0 };
    for (uint32_t address : kTables) {
        StringToNumberEntry *table = reinterpret_cast<StringToNumberEntry *>(uintptr_t(address));
        std::vector<std::string> probes = { "", "zzz", "A", "a" };
        for (int i = 0; table[i].string != NULL; i++) {
            probes.push_back(Flipped(table[i].string));
            probes.push_back(std::string(table[i].string) + "x");
            probes.push_back(std::string(table[i].string).substr(0, strlen(table[i].string) / 2));
        }
        char what[64];
        snprintf(what, sizeof(what), "StringToNumber %08x", address);
        StnCheck(what, table, probes);
    }
    for (int round = 0; round < 40; round++) {
        int n = round % 2 ? 31 + int(Random(60)) : 1 + int(Random(30));
        std::vector<std::string> names;
        for (int i = 0; i < n; i++) {
            std::string s;
            int length = 1 + int(Random(8));
            for (int c = 0; c < length; c++)
                s += char('a' + Random(Random(4) ? 4 : 26));   // short alphabets: duplicates and prefixes
            names.push_back(s);
        }
        std::vector<StringToNumberEntry> table(n + 1);
        for (int i = 0; i < n; i++) {
            table[i].number = int(Random(round % 3 ? 50 : 1000)) - 10;   // duplicate numbers too
            table[i].string = const_cast<char *>(names[i].c_str());
        }
        table[n].number = 0;
        table[n].string = NULL;
        std::vector<std::string> probes = { "", "b", "zz", "aaaaaaaaa" };
        for (int i = 0; i < 20; i++)
            probes.push_back(names[Random(n)] + (Random(2) ? "a" : ""));
        char what[64];
        snprintf(what, sizeof(what), "StringToNumber random %d (%d)", round, n);
        StnCheck(what, table.data(), probes);
    }
}

std::string RandomTagName() {
    static const char *const kParts[] = { "{Base}", "{abcd}", "{as  }", "{Name}", "{sn  0003}", "{as  00ff}",
                                          "{as  1234}", "{xy}", "{abcdef}", "{abcd12}", "{abcd}::12", "{abcd}::",
                                          "}::7", "::", "{", "{{{{{}" };
    static const char *const kGroups[] = { "", "<<Root>>", "<<Shared>>", "Thing", "a::b", "CARP" };
    std::string s = kGroups[Random(6)];
    int parts = 1 + int(Random(3));
    for (int i = 0; i < parts; i++) {
        if (Random(2))
            s += "::";
        s += kParts[Random(16)];
    }
    return s;
}

// The original's DecodeTag reads an uninitialised local when sscanf matches nothing after the four characters;
// such names are left out (the port uses -1 there, see UData.cpp).
bool DecodeTagIsDefined(const char *text) {
    size_t n = strlen(text);
    if (n <= 5 || text[0] != '{' || !(text[5] == '}' || (n > 9 && text[9] == '}')))
        return true;
    if (text[5] == '}')
        return true;
    return isxdigit((unsigned char)text[5]) != 0;
}

void RunNames() {
    std::vector<std::string> names;
    for (int i = 0; i < 400; i++)
        names.push_back(RandomTagName());
    Both("UDataGroupDecodeTag / ParseNameAndTag", [&]() {
        for (const std::string &n : names) {
            const char *s = n.c_str();
            const char *tagText = strstr(s, "::") != NULL ? strstr(s, "::") + 2 : s;
            uint32_t decoded = DecodeTagIsDefined(s) ? Data_DecodeTag(s) : 0;
            char group[160];
            memset(group, 0xcd, sizeof(group));
            uint32_t tag = 0xcdcdcdcd;
            int offset = -12345;
            bool ok = (*s == '{' ? DecodeTagIsDefined(s) : DecodeTagIsDefined(tagText))
                          ? Carp_ParseNameAndTag(s, group, &tag, &offset) : false;
            group[sizeof(group) - 1] = 0;
            Logf("%s: %08x | %d %s %08x %d", s, decoded, ok, ok ? group : "-", tag, offset);
        }
    });
}

// =============================================================================================================
// The symbol table's trees
// =============================================================================================================

void Shape(TreeNode *node, std::string *out) {
    if (node->isNil) {
        *out += '.';
        return;
    }
    *out += '(';
    *out += node->name;
    *out += node->color ? 'b' : 'r';
    Shape(node->left, out);
    Shape(node->right, out);
    *out += ')';
}

void LogTree(const char *what, Tree *tree) {
    std::string s;
    Shape(tree->head->parent, &s);
    Logf("%s size %u left %s right %s %s", what, tree->size, tree->head->left->isNil ? "-" : tree->head->left->name,
         tree->head->right->isNil ? "-" : tree->head->right->name, s.c_str());
}

const char *const kNamespaceNames[] = { "CARP", "carp", "Carp", "CHAR", "DATA", "EAGL", "TEX0", "TEX1", "TEX9",
                                        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "zz",
                                        "ab", "AB", "abc", "x", "y", "z" };
const int kNamespaceNameCount = sizeof(kNamespaceNames) / sizeof(kNamespaceNames[0]);

UCharNamespace g_charNamespace;
UCarpNamespace g_carpNamespace;

void RunTables() {
    for (int script = 0; script < 30; script++) {
        uint32_t seed = Random();
        char what[64];
        snprintf(what, sizeof(what), "USymbolTable script %d", script);
        Both(what, [&]() {
            uint32_t saved = g_seed;
            g_seed = seed;
            USymbolTable *table = static_cast<USymbolTable *>(UMemory::FastAlloc(sizeof(USymbolTable), "shadow"));
            Table_Construct(table, 0);
            Char_Construct(&g_charNamespace, 0);
            std::vector<const char *> added;
            int steps = 20 + int(Random(80));
            for (int step = 0; step < steps; step++) {
                uint32_t op = Random(10);
                if (op < 6 || added.empty()) {
                    const char *name = kNamespaceNames[Random(kNamespaceNameCount)];
                    Table_AddNamespace(table, 0, name, &g_charNamespace);
                    added.push_back(name);
                    LogTree("add", table->namespaces);
                } else if (op < 8) {
                    size_t i = Random(uint32_t(added.size()));
                    SymbolNamespace *removed = Table_RemoveNamespace(table, 0, added[i]);
                    added.erase(added.begin() + i);
                    Logf("removed %d", removed == &g_charNamespace);
                    LogTree("remove", table->namespaces);
                } else {
                    const char *name = kNamespaceNames[Random(kNamespaceNameCount)];
                    char query[64];
                    snprintf(query, sizeof(query), "%s::%s", name, Random(2) ? "value" : "");
                    int size = -7;
                    void *found = Table_NameLookup(table, 0, Random(8) ? query : name, &size);
                    Logf("lookup %s: %s %d", query, found == NULL ? "null" : (const char *)found, size);
                }
            }
            if (Random(2)) {   // erase a range, or everything
                TreeNode *result;
                Tree *map = table->namespaces;
                if (Random(2)) {
                    NamespaceMap_EraseRange(map, 0, &result, map->head->left, map->head);
                } else {
                    TreeNode *first = map->head->left;
                    for (uint32_t k = Random(4); k > 0 && !first->isNil; k--)
                        first = TreeNext(first);
                    TreeNode *last = first;
                    for (uint32_t k = Random(6); k > 0 && !last->isNil; k--)
                        last = TreeNext(last);
                    NamespaceMap_EraseRange(map, 0, &result, first, last);
                    Logf("range end %s", result->isNil ? "-" : result->name);
                }
                LogTree("erase range", map);
            }
            Table_Delete(table, 0, 1);
            Char_Destruct(&g_charNamespace, 0);
            g_seed = saved;
        });
    }

    for (int script = 0; script < 30; script++) {
        uint32_t seed = Random();
        char what[64];
        snprintf(what, sizeof(what), "UCarpNamespace map script %d", script);
        Both(what, [&]() {
            uint32_t saved = g_seed;
            g_seed = seed;
            Carp_Construct(&g_carpNamespace, 0);
            CarpGroupMap *map = g_carpNamespace.groups;
            int steps = 10 + int(Random(60));
            for (int step = 0; step < steps; step++) {
                TreePair pair;
                pair.name = kNamespaceNames[Random(kNamespaceNameCount)];
                pair.group = reinterpret_cast<UGroup *>(uintptr_t(0x1000 + step));
                TreeInsertResult result;
                CarpMap_InsertUnique(map, 0, &result, &pair);
                Logf("insert %s: %d %s %08x", pair.name, result.inserted, result.where->name,
                     uint32_t(uintptr_t(result.where->group)));
                LogTree("map", map);
                if (Random(6) == 0) {
                    TreeNode *first = map->head->left;
                    for (uint32_t k = Random(3); k > 0 && !first->isNil; k--)
                        first = TreeNext(first);
                    TreeNode *last = first;
                    for (uint32_t k = Random(4); k > 0 && !last->isNil; k--)
                        last = TreeNext(last);
                    TreeNode *end;
                    CarpMap_EraseRange(map, 0, &end, first, last);
                    LogTree("erased", map);
                }
            }
            Carp_Destruct(&g_carpNamespace, 0);
            g_seed = saved;
        });
    }
}

// =============================================================================================================
// CARP files
// =============================================================================================================

uint8_t *g_buffer;
size_t g_bufferSize;

std::string Where(const void *p) {
    char s[32];
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (b >= g_buffer && b < g_buffer + g_bufferSize)
        snprintf(s, sizeof(s), "+%x", unsigned(b - g_buffer));
    else
        snprintf(s, sizeof(s), "%08x", unsigned(uintptr_t(p)));
    return s;
}

bool InBuffer(const void *p, size_t bytes) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return b >= g_buffer && b + bytes <= g_buffer + g_bufferSize;
}

// Every group, depth first.
void Groups(UGroup *group, std::vector<UGroup *> *out, int depth) {
    if (depth > 32 || out->size() > 100000)
        return;
    out->push_back(group);
    UGroup *children = group->GetArray();
    for (uint32_t i = 0; i < group->GroupCount(); i++)
        Groups(&children[i], out, depth + 1);
}

const uint32_t kTagSpline = 0x41495370;   // 'AISp'
const uint32_t kTagBase = 0x42617365;     // 'Base'
const uint32_t kTagBaseDesc = 0x70442020; // 'pD  '
const uint32_t kTagInstance = 0x696e2020; // 'ni  '
const uint32_t kTagCamInstance = 0x63692020;   // 'ci  '

// A stepped channel's key is floor(time): outside its keys the evaluation reads past them, original and port alike.
bool InRange(CARP::PathInfo *path, int channel, float time) {
    if (channel < 0)
        return true;
    CARP::PathChannel *ch = &path->channels[channel];
    return !(ch->flags & CARP::kPathStepped) || (time >= 0.0f && time < float(ch->keyCount));
}

void PathCheck(const char *what, CARP::PathInfo *path, int channelCount) {
    // the times to try: every key time of every channel, between them, outside, NaN
    std::vector<float> times = { -1.0f, 0.0f, 1e9f, Bits(0x7fc00000), -0.0f, 0.5f };
    for (int c = 0; c < channelCount; c++) {
        CARP::PathChannel *ch = &path->channels[c];
        float *t = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(path) + ch->timesOffset);
        for (uint32_t k = 0; k < ch->keyCount && k < 64; k++) {
            times.push_back(t[k]);
            if (k + 1 < ch->keyCount) {
                times.push_back((t[k] + t[k + 1]) * 0.5f);
                times.push_back(RandomFloat(t[k], t[k + 1]));
            }
        }
        if (ch->keyCount > 0)
            times.push_back(t[ch->keyCount - 1] + 1.0f);
    }
    std::vector<uint32_t> hints;
    for (size_t i = 0; i < times.size() * 3; i++)
        hints.push_back(Random(3) == 0 ? 0xffffffff : Random(8));
    Both(what, [&]() {
        size_t h = 0;
        for (float time : times) {
            for (int rep = 0; rep < 3; rep++) {
                if (!InRange(path, path->positionChannel, time) || !InRange(path, path->rotationChannel, time))
                    break;
                float matrix[16];
                for (int i = 0; i < 16; i++)
                    matrix[i] = float(i) * 0.25f - 1.0f;
                uint32_t positionKey = hints[h], rotationKey = hints[h + 1 < hints.size() ? h + 1 : 0];
                h = (h + 2) % hints.size();
                float weight = -3.0f;
                Path_EvaluateMatrix(path, 0, time, &positionKey, &rotationKey, matrix, &weight);
                Logf("t %08x keys %u %u weight %08x", BitsOf(time), positionKey, rotationKey, BitsOf(weight));
                LogBytes(" m", matrix, sizeof(matrix));
            }
            for (int c = 0; c < channelCount; c++) {
                CARP::PathChannel *ch = &path->channels[c];
                if (!InRange(path, c, time))
                    continue;
                uint32_t hint = hints[h];
                h = (h + 1) % hints.size();
                float out[4] = { 9, 9, 9, 9 };
                uint32_t key = Path_EvaluateLinear(path, 0, time, hint, ch, out);
                Logf(" c%d linear %u %08x %08x %08x %08x find %u", c, key, BitsOf(out[0]), BitsOf(out[1]),
                     BitsOf(out[2]), BitsOf(out[3]), Path_FindKey(path, 0, time, hint, ch));
                if (ch->flags & CARP::kPathSpline) {
                    key = Path_EvaluateSpline(path, 0, time, hint, ch, out);
                    Logf(" c%d spline %u %08x %08x %08x %08x", c, key, BitsOf(out[0]), BitsOf(out[1]),
                         BitsOf(out[2]), BitsOf(out[3]));
                }
                if (ch->keyCount > 0) {
                    uint32_t a = Random(ch->keyCount), b = Random(ch->keyCount);
                    Logf(" c%d scale %u %u %016llx", c, a, b,
                         (unsigned long long)BitsOf(Path_ScaleToUnitTime(path, 0, time, a, b, ch)));
                }
            }
        }
    });
}

// A path's channels, if they look like one (the file's own paths are checked before they are evaluated).
int PathChannels(CARP::PathInfo *path) {
    if (!InBuffer(path, sizeof(CARP::PathInfo)))
        return 0;
    int highest = path->positionChannel > path->rotationChannel ? path->positionChannel : path->rotationChannel;
    if (highest < 0 || highest > 15 || path->positionChannel < -1 || path->rotationChannel < -1)
        return 0;
    for (int c = 0; c <= highest; c++) {
        CARP::PathChannel *ch = &path->channels[c];
        if (!InBuffer(ch, sizeof(*ch)) || ch->keyCount == 0 || ch->keyCount > 4096)
            return 0;
        uint8_t *base = reinterpret_cast<uint8_t *>(path);
        if (!InBuffer(base + ch->timesOffset, ch->keyCount * 4) || !InBuffer(base + ch->keysOffset, ch->keyCount * 48))
            return 0;
    }
    return highest + 1;
}

void ZoneCheck(const char *what, CARP::BaseDesc *desc) {
    std::vector<float> points;
    for (int i = 0; i < 60; i++) {
        float p[4];
        for (int a = 0; a < 3; a++) {
            float lo = desc->damageBoxMin[a], hi = desc->damageBoxMax[a];
            float mid = float((double(hi) + lo) * 0.5);
            float choices[] = { lo, hi, mid, RandomFloat(lo - 2.0f, hi + 2.0f), RandomFloat(lo, hi), -lo, -hi, -mid,
                                Bits(0x7fc00000) };
            p[a] = choices[Random(i < 50 ? 8 : 9)];
        }
        p[3] = 1.0f;
        points.insert(points.end(), p, p + 4);
    }
    Both(what, [&]() {
        for (size_t i = 0; i < points.size(); i += 4) {
            int zone = Base_CalcDamageZone(desc, 0, &points[i]);
            Logf("zone %d bits %d", zone, Base_GetZoneBits(desc, 0, zone));
        }
    });
}

void DimensionsCheck(const char *what, const CARP::Instance *instance) {
    struct Args {
        uint32_t packed;
        bool separate;
        float x, y, z;
    };
    std::vector<Args> args;
    float specials[] = { 250.0f, Bits(0x437a0001), 0.0f, -0.0f, -1.0f, 0.24f, 0.26f, 255.75f, 255.76f, 4095.0f,
                         16368.0f, 16369.0f, 1e9f, Bits(0x7fc00000), Bits(0x7f800000) };
    for (int i = 0; i < 40; i++) {
        Args a;
        a.packed = Random();
        a.separate = Random(2) != 0;
        float *v[3] = { &a.x, &a.y, &a.z };
        for (float *f : v)
            *f = Random(3) == 0 ? specials[Random(15)] : RandomFloat(0.0f, Random(2) ? 255.0f : 4000.0f);
        args.push_back(a);
    }
    Both(what, [&]() {
        for (const Args &a : args) {
            CARP::Instance copy = *instance;
            copy.packedDimensions = a.packed;
            Instance_SetDimensions(&copy, 0, a.separate, a.x, a.y, a.z);
            Logf("%08x", copy.packedDimensions);
        }
    });
}

void TagReferenceCheck(const char *what, const std::vector<UGroup *> &groups) {
    struct Job {
        UGroup *shared, *local;
        uint32_t value;
    };
    std::vector<Job> jobs;
    for (int i = 0; i < 300 && !groups.empty(); i++) {
        Job j;
        j.shared = groups[Random(uint32_t(groups.size()))];
        j.local = groups[Random(uint32_t(groups.size()))];
        UGroup *from = Random(2) ? j.shared : j.local;
        uint32_t pick = Random(5);
        if (pick == 0)
            j.value = 0;
        else if (pick == 1)
            j.value = 0xffffffff;
        else if (pick == 2 || from->count == 0)
            j.value = Random();
        else
            j.value = from->GetArray()[from->GroupCount() + Random(from->count)].tag;
        jobs.push_back(j);
    }
    Both(what, [&]() {
        for (const Job &j : jobs) {
            CARP::TagReference reference;
            reference.value = j.value;
            Carp_TagReference(&reference, 0, j.shared, j.local);
            Logf("%08x -> %s", j.value, Where(reinterpret_cast<void *>(uintptr_t(reference.value))).c_str());
        }
    });
}

void CarpFileCheck(const DiscFile &file) {
    if (file.data.size() < 16 || file.data.size() + 64 > g_bufferSize)
        return;
    std::vector<uint8_t> images[2];
    char what[300];
    snprintf(what, sizeof(what), "CARP %s", file.name.c_str());
    Both(what, [&]() {
        memset(g_buffer, 0, file.data.size() + 64);
        memcpy(g_buffer, file.data.data(), file.data.size());
        UGroup *root = UGroup::Deserialize(g_buffer, true);
        USymbolTable *table = static_cast<USymbolTable *>(UMemory::FastAlloc(sizeof(USymbolTable), "shadow"));
        Table_Construct(table, 0);
        Char_Construct(&g_charNamespace, 0);
        Carp_Construct(&g_carpNamespace, 0);
        Table_AddNamespace(table, 0, "CHAR", &g_charNamespace);
        Table_AddNamespace(table, 0, "DATA", &g_charNamespace);
        Carp_AddCarpFile(&g_carpNamespace, 0, root);
        Table_AddNamespace(table, 0, "CARP", &g_carpNamespace);
        LogTree("groups", g_carpNamespace.groups);

        // lookups: every group by name, its first records by tag, offsets, and some that fail
        std::vector<std::string> queries = { "CHAR::hello", "DATA::", "EAGL::thing", "nothing", "CARP::",
                                             "CARP::no such group", "CARP::<<Root>>", "CARP::<<Shared>>::{Base}",
                                             "carp::<<map>>", "CARP::{Base}" };
        Tree *map = g_carpNamespace.groups;
        for (TreeNode *node = map->head->left; !node->isNil && queries.size() < 400; node = TreeNext(node)) {
            UGroup *group = node->group;
            queries.push_back(std::string("CARP::") + node->name);
            UData *records = group->GetArray() + group->GroupCount();
            for (uint32_t r = 0; r < group->count && r < 4; r++) {
                uint32_t tag = records[r].tag;
                char q[200];
                snprintf(q, sizeof(q), "CARP::%.100s::{%c%c%c%c}", node->name, char(tag >> 24), char(tag >> 16),
                         char(tag >> 8), char(tag));
                queries.push_back(q);
                snprintf(q, sizeof(q), "CARP::%.100s::{%c%c  %04x}}::%u", node->name, char(tag >> 24),
                         char(tag >> 16), unsigned(tag & 0xffff), unsigned(Random(64)));
                queries.push_back(q);
            }
        }
        for (const std::string &q : queries) {
            bool printable = true;
            for (char c : q)
                printable &= c >= 0x20 && c < 0x7f;
            const char *tag = strchr(q.c_str(), '{');
            if (!printable || (tag != NULL && !DecodeTagIsDefined(tag)))
                continue;
            int size = -7;
            void *found = Table_NameLookup(table, 0, q.c_str(), &size);
            // "CHAR::"/"DATA::" answer a pointer into the query itself (a std::string of this side's): log it as
            // an offset into the query, the rest as an offset into the image
            const char *text = static_cast<const char *>(found);
            if (text != NULL && text >= q.c_str() && text <= q.c_str() + q.size())
                Logf("lookup %s: query+%d \"%s\" %d", q.c_str(), int(text - q.c_str()), text, size);
            else
                Logf("lookup %s: %s %d", q.c_str(), Where(found).c_str(), size);
        }

        uint8_t verbose = CarpVerbose;
        CarpVerbose = 0;   // the failure report would print every EAGL name: none is in this table
        uint32_t sect = Carp_ResolveSymbolicReferences(root, table);
        CarpVerbose = verbose;
        Logf("resolved: sect %08x", sect);

        Table_Delete(table, 0, 1);
        Carp_Destruct(&g_carpNamespace, 0);
        Char_Destruct(&g_charNamespace, 0);
        images[g_side].assign(g_buffer, g_buffer + file.data.size() + 64);
    });
    g_checks++;
    if (images[0] != images[1]) {
        size_t i = 0;
        while (i < images[0].size() && i < images[1].size() && images[0][i] == images[1][i])
            i++;
        if (g_diffs++ < 10)
            printf("[carpshadow] %s: resolved images differ at +%x\n", what, unsigned(i));
        return;
    }

    // the resolved file (the port's, equal to the original's): the record types' own code
    UGroup *root = reinterpret_cast<UGroup *>(g_buffer);
    std::vector<UGroup *> groups;
    Groups(root, &groups, 0);
    snprintf(what, sizeof(what), "TagReference %s", file.name.c_str());
    TagReferenceCheck(what, groups);
    int paths = 0, zones = 0, instances = 0;
    for (UGroup *group : groups) {
        UData *records = group->GetArray() + group->GroupCount();
        for (uint32_t r = 0; r < group->count; r++) {
            UData *record = &records[r];
            uint32_t tag = record->MatchTag();
            uint8_t *data = record->Data();
            if (tag == kTagSpline && paths < 40) {
                CARP::AISpline *splines = reinterpret_cast<CARP::AISpline *>(data);
                for (uint32_t s = 0; s < record->count && paths < 40 && InBuffer(&splines[s], sizeof(splines[s])); s++) {
                    CARP::PathInfo *path = reinterpret_cast<CARP::PathInfo *>(uintptr_t(splines[s].path.value));
                    int channels = PathChannels(path);
                    if (channels > 0) {
                        snprintf(what, sizeof(what), "path %s #%d", file.name.c_str(), paths);
                        PathCheck(what, path, channels);
                    }
                    if (channels > 0 || path == NULL) {
                        snprintf(what, sizeof(what), "GetApplyTransform %s #%d", file.name.c_str(), paths);
                        CARP::AISpline *spline = &splines[s];
                        Both(what, [&]() {
                            float out[16];
                            memset(out, 0x55, sizeof(out));
                            Spline_GetApplyTransform(spline, 0, out);
                            LogBytes("m", out, sizeof(out));
                        });
                    }
                    paths++;
                }
            } else if ((tag == kTagBase || tag == kTagBaseDesc) && zones < 30 && InBuffer(data, sizeof(CARP::BaseDesc))) {
                snprintf(what, sizeof(what), "CalcDamageZone %s #%d", file.name.c_str(), zones++);
                ZoneCheck(what, reinterpret_cast<CARP::BaseDesc *>(data));
            } else if ((tag == kTagInstance || tag == kTagCamInstance) && instances < 10 && record->count > 0 &&
                       InBuffer(data, sizeof(CARP::Instance))) {
                snprintf(what, sizeof(what), "SetDimensions %s #%d", file.name.c_str(), instances++);
                DimensionsCheck(what, reinterpret_cast<CARP::Instance *>(data));
            }
        }
    }
}

void RunCarp() {
    static const char *const kExts[] = { ".crp" };
    std::vector<DiscFile> files;
    ReadDisc(kExts, 1, &files);
    size_t largest = 0;
    for (const DiscFile &f : files)
        largest = f.data.size() > largest ? f.data.size() : largest;
    g_bufferSize = (largest + 0x10000) & ~size_t(0xffff);
    g_buffer = static_cast<uint8_t *>(VirtualAlloc(NULL, g_bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (g_buffer == NULL)
        return;
    printf("[carpshadow] CARP: %d files\n", int(files.size()));
    fflush(stdout);
    for (const DiscFile &f : files)
        CarpFileCheck(f);
    VirtualFree(g_buffer, 0, MEM_RELEASE);
    g_buffer = NULL;
}

// =============================================================================================================
// Synthetic paths and the matrix helpers
// =============================================================================================================

void RunSynthetic() {
    // one PathInfo with a channel of every kind, rebuilt with new random keys each round
    const uint32_t kFlags[] = { 0, CARP::kPathRotation, CARP::kPathScalar, CARP::kPathStepped, CARP::kPathSpline,
                                CARP::kPathSpline | CARP::kPathRotation, 0, CARP::kPathSpline };
    const int kChannels = 8;
    std::vector<uint8_t> storage(0x10000);
    g_buffer = storage.data();
    g_bufferSize = storage.size();
    for (int round = 0; round < 12; round++) {
        memset(storage.data(), 0, storage.size());
        CARP::PathInfo *path = reinterpret_cast<CARP::PathInfo *>(storage.data());
        uint32_t at = 0x40 + kChannels * sizeof(CARP::PathChannel);
        for (int c = 0; c < kChannels; c++) {
            CARP::PathChannel *ch = &path->channels[c];
            ch->flags = kFlags[c];
            ch->keyCount = 1 + Random(c == 6 ? 3 : 12);
            ch->timesOffset = at;
            float *times = reinterpret_cast<float *>(storage.data() + at);
            float t = RandomFloat(-2.0f, 2.0f);
            for (uint32_t k = 0; k < ch->keyCount; k++) {
                times[k] = t;
                t += c == 6 && Random(2) ? 0.0f : RandomFloat(0.01f, 3.0f);   // channel 6: equal key times
            }
            at += ch->keyCount * 4;
            at = (at + 15) & ~15u;
            ch->keysOffset = at;
            float *keys = reinterpret_cast<float *>(storage.data() + at);
            for (uint32_t k = 0; k < ch->keyCount * 12; k++)
                keys[k] = RandomFloat(-1.0f, 1.0f);
            at += ch->keyCount * 48;
        }
        for (int pair = 0; pair < 4; pair++) {
            path->positionChannel = pair == 3 ? -1 : int(Random(kChannels));
            path->rotationChannel = pair == 2 ? -1 : int(Random(kChannels));
            char what[64];
            snprintf(what, sizeof(what), "synthetic path %d/%d", round, pair);
            PathCheck(what, path, kChannels);
        }
    }
    g_buffer = NULL;
    g_bufferSize = 0;

    std::vector<float> values(64 * 52);
    for (float &v : values)
        v = Random(20) == 0 ? Bits(Random()) : RandomFloat(-10.0f, 10.0f);
    Both("RotateSplineAboutBase / ComputeLinear", [&]() {
        for (size_t i = 0; i + 52 <= values.size(); i += 52) {
            float out[16];
            memset(out, 0x55, sizeof(out));
            Carp_RotateSplineAboutBase(&values[i], &values[i + 16], &values[i + 32], out);
            LogBytes("rotate", out, sizeof(out));
            float lerp[4];
            Path_ComputeLinear(&values[i], &values[i + 4], lerp, values[i + 48], false);
            LogBytes("lerp", lerp, sizeof(lerp));
            Path_ComputeLinear(&values[i], &values[i + 4], lerp, values[i + 48] * 0.1f, true);
            LogBytes("slerp", lerp, sizeof(lerp));
        }
    });
}

}   // namespace

void CarpShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_CARPSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    RunStringToNumber();
    RunNames();
    RunTables();
    RunSynthetic();
    RunDafi();
    RunCarp();
    printf("[carpshadow] data layer: %d cases, %d checks, %d differ%s\n", g_cases, g_checks, g_diffs,
           g_faults != 0 ? " (faults counted)" : "");
    fflush(stdout);
}
