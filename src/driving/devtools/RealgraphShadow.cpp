#include "RealgraphShadow.h"

#include "../eagl/Realgraph.h"
#include "../platform/FileSys.h"
#include "../platform/RefPack.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_RGSHADOW=1, at injection time: eagl/Realgraph.cpp against the originals on every font (.xfn), image
// container (.xsh) and string table (.loc) in D:\driving\*.viv (read straight from the archives and unpacked).
//
// FONT: every font draws and measures the same strings - every character, random bytes, newlines and every
// string of every string table - through a recording driver (and again through recording batch hooks), and the
// recorded calls (glyph, x and y bit for bit), FONT_getrectx's outputs, every kerning pair and glyph search are
// compared. SHAPE: every image of every container is located, named and measured; SHAPE_createsize/createat
// build the same bytes for a sweep of sizes and formats. LOCALE: every id (and some that are not) in every table.
// Originals run with their entry swapped back in (common/xbeOriginal.h).
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types: another test's of the same name must not merge with them

static int g_checks, g_failures;

static void Report(const char *what, const char *detail) {
    if (g_failures++ < 30)
        printf("[rgshadow] %s: %s\n", what, detail);
}

struct Scope {   // the originals in play for one comparison
    const unsigned *addresses;
    int count;
    Scope(const unsigned *a, int n) : addresses(a), count(n) {
        for (int i = 0; i < n; i++)
            XbeOriginal_Restore(a[i], true);
    }
    ~Scope() {
        for (int i = 0; i < count; i++)
            XbeOriginal_Restore(addresses[i], false);
    }
};

static const unsigned kFont[] = { 0x001073f0, 0x00107770, 0x00107b10, 0x00107b30, 0x00107c70, 0x00107cb0,
                                  0x00107b40, 0x00107b60 };
static const unsigned kShape[] = { 0x00107d40, 0x00107df0, 0x00107e30, 0x00107f50, 0x00108000, 0x00108280,
                                   0x001082a0, 0x001082d0, 0x001082f0, 0x00108320, 0x00108340, 0x001083f0,
                                   0x0014a490 };
static const unsigned kLocale[] = { 0x00107c00 };

// ---- reading the archives

// Each file sits 4 KB into its buffer, as it sits inside a larger allocation in the game: a character below 0x20
// makes FONT look 0x20 glyphs before its table (the original does, and so does the port).
static const size_t kPad = 0x1000;

struct Entry {
    std::string archive, name;
    std::vector<uint8_t> data;
    uint8_t *base() { return data.data() + kPad; }
    const uint8_t *base() const { return data.data() + kPad; }
};

static void ReadArchive(const char *hostPath, const char *archive, std::vector<Entry> *out) {
    HANDLE file = CreateFileA(hostPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    uint8_t header[16];
    DWORD got = 0;
    ReadFile(file, header, 16, &got, NULL);
    int dirSize = got == 16 ? BIG_dirsize(header) : 0;
    std::vector<uint8_t> dir(dirSize > 16 ? dirSize : 16);
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ReadFile(file, dir.data(), (DWORD)dirSize, &got, NULL);
    for (int i = 0;; i++) {
        int offset, size;
        const char *name = BIG_find(dir.data(), NULL, i, &offset, &size);
        if (name == NULL)
            break;
        size_t n = strlen(name);
        if (n < 4)
            continue;
        const char *ext = name + n - 4;
        if (_stricmp(ext, ".xfn") != 0 && _stricmp(ext, ".xsh") != 0 && _stricmp(ext, ".loc") != 0)
            continue;
        std::vector<uint8_t> raw(size + 16);
        SetFilePointer(file, offset, NULL, FILE_BEGIN);
        ReadFile(file, raw.data(), (DWORD)size, &got, NULL);
        Entry e;
        e.archive = archive;
        e.name = name;
        unsigned unpacked = unpacksizez(raw.data());
        if (unpacked != 0) {
            e.data.assign(kPad + unpacked + 64, 0);
            UNPACK_unpack(raw.data(), e.base());
        } else {
            e.data.assign(kPad, 0);
            e.data.insert(e.data.end(), raw.begin(), raw.end());
        }
        out->push_back(e);
    }
    CloseHandle(file);
}

// ---- FONT: a recording driver

static std::vector<uint32_t> *g_log;
static const uint8_t *g_logFont;

static void Record(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    g_log->push_back(a);
    g_log->push_back(b);
    g_log->push_back(c);
    g_log->push_back(d);
}
static uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
static void DrawRecord(const uint8_t *font, const uint8_t *glyph, float x, float y) {
    Record(1, (uint32_t)(glyph - font), Bits(x), Bits(y));
}
static void StartRecord(const uint8_t *font) { Record(2, (uint32_t)(font - g_logFont), 0, 0); }
static void EndRecord(const uint8_t *font) { Record(3, (uint32_t)(font - g_logFont), 0, 0); }
static void CreateRecord(const uint8_t *font) { Record(4, (uint32_t)(font - g_logFont), 0, 0); }
static void DestroyRecord(const uint8_t *font) { Record(5, (uint32_t)(font - g_logFont), 0, 0); }
static void BatchRecord(const uint8_t *font, float *batch, int count) {
    Record(6, (uint32_t)count, 0, 0);
    for (int i = 0; i < count; i++) {
        uint32_t g;
        memcpy(&g, &batch[i * 3], 4);
        Record(7, g - (uint32_t)(uintptr_t)font, Bits(batch[i * 3 + 1]), Bits(batch[i * 3 + 2]));
    }
}
static void BatchExRecord(const uint8_t *font, float *batch, int count, int argument, int first) {
    Record(8, (uint32_t)count, (uint32_t)argument, (uint32_t)(first & 0xff));
    BatchRecord(font, batch, count);
}

static void *g_driver[5] = { (void *)DrawRecord, (void *)StartRecord, (void *)EndRecord, (void *)CreateRecord,
                             (void *)DestroyRecord };

#define FontDriverSlot  (*(void ***)0x001cec98u)
#define FontBatch       (*(void **)0x00241be8u)
#define FontBatchEx     (*(void **)0x00241bf0u)

typedef void (*DrawFn)(const uint8_t *, float, float, const uint8_t *, int);
typedef void (*RectFn)(const uint8_t *, const uint8_t *, float *, float *, float *, float *);
typedef const uint8_t *(*SearchFn)(int, const uint8_t *, int, int);
typedef int (*KernFn)(const uint8_t *, const uint8_t *, int);

static void CompareText(const Entry &font, const uint8_t *text, float x, float y, int batchMode) {
    const uint8_t *f = font.base();
    std::vector<uint32_t> logO, logP;
    float rO[4], rP[4];
    for (int side = 0; side < 2; side++) {
        g_log = side == 0 ? &logO : &logP;
        g_logFont = f;
        FontBatch = batchMode ? (void *)BatchRecord : NULL;
        FontBatchEx = batchMode ? (void *)BatchExRecord : NULL;
        float *r = side == 0 ? rO : rP;
        memset(r, 0x5a, 16);
        int argument = batchMode == 2 ? 7 : 0;
        if (side == 0) {
            Scope s(kFont, 8);
            ((DrawFn)0x001073f0)(f, x, y, text, argument);
            ((RectFn)0x00107770)(f, text, r, r + 1, r + 2, r + 3);
        } else {
            FONT_drawtextx(f, x, y, text, argument);
            FONT_getrectx(f, text, r, r + 1, r + 2, r + 3);
        }
    }
    g_checks++;
    if (logO != logP || memcmp(rO, rP, 16) != 0) {
        char detail[256];
        size_t i = 0;
        while (i < logO.size() && i < logP.size() && logO[i] == logP[i])
            i++;
        snprintf(detail, sizeof(detail), "\"%.40s\" mode %d: calls differ at %u of %u/%u; rect %08x %08x %08x %08x / "
                 "%08x %08x %08x %08x", (const char *)text, batchMode, (unsigned)i, (unsigned)logO.size(),
                 (unsigned)logP.size(), Bits(rO[0]), Bits(rO[1]), Bits(rO[2]), Bits(rO[3]), Bits(rP[0]), Bits(rP[1]),
                 Bits(rP[2]), Bits(rP[3]));
        Report(font.name.c_str(), detail);
    }
}

static void CheckFont(const Entry &font, const std::vector<std::string> &strings) {
    const uint8_t *f = font.base();
    // glyph searches and kerning, for every code and every pair of codes
    int count = *(const uint16_t *)(f + 0xa);
    int size = (*(const uint32_t *)(f + 0xc) & 0x40000) ? 0x10 : 0xc;
    const uint8_t *table = f + *(const int32_t *)(f + 0x14);
    for (int code = 0; code < 0x110; code++) {
        const uint8_t *o, *p;
        {
            Scope s(kFont, 8);
            o = ((SearchFn)0x00107c70)(code, table, count, size);
        }
        p = FONT_bsearch(code, table, count, size);
        g_checks++;
        if (o != p)
            Report(font.name.c_str(), "FONT_bsearch differs");
    }
    for (int g = 0; g < count; g++) {
        for (int previous = 0; previous < 0x100; previous++) {
            int o, p;
            {
                Scope s(kFont, 8);
                o = ((KernFn)0x00107cb0)(f, table + g * size, previous);
            }
            p = FONT_getkern(f, table + g * size, previous);
            g_checks++;
            if (o != p)
                Report(font.name.c_str(), "FONT_getkern differs");
        }
    }
    // text
    uint8_t all[256];
    for (int i = 1; i < 256; i++)
        all[i - 1] = (uint8_t)i;
    all[255] = 0;
    uint8_t random[200];
    for (int mode = 0; mode < 3; mode++) {
        CompareText(font, all, 0.0f, 0.0f, mode);
        CompareText(font, (const uint8_t *)"Bond, James Bond\nNIGHTFIRE 007 - Q\n\n x", 12.5f, 33.25f, mode);
        for (int r = 0; r < 20; r++) {
            for (int i = 0; i < 199; i++)
                random[i] = (uint8_t)(rand() % 255 + 1);
            random[199] = 0;
            CompareText(font, random, (float)(rand() % 640) + 0.3f, (float)(rand() % 480) - 0.7f, mode);
        }
    }
    for (const std::string &s : strings)
        CompareText(font, (const uint8_t *)s.c_str(), 100.0f, 200.0f, 0);
    // create and destroy through the driver
    std::vector<uint32_t> logO, logP;
    for (int side = 0; side < 2; side++) {
        g_log = side == 0 ? &logO : &logP;
        g_logFont = f;
        const uint8_t *r;
        if (side == 0) {
            Scope s(kFont, 8);
            r = ((const uint8_t *(*)(const uint8_t *))0x00107b40)(f);
            ((void (*)(const uint8_t *))0x00107b60)(f);
        } else {
            r = FONT_create(f);
            FONT_destroy(f);
        }
        Record(9, (uint32_t)(r - f), 0, 0);
    }
    g_checks++;
    if (logO != logP)
        Report(font.name.c_str(), "FONT_create/destroy differ");
}

// ---- LOCALE

static void CheckLocale(Entry &locale, std::vector<std::string> *strings) {
    uint8_t *l = locale.base();
    for (int id = -3; id < 3000; id++) {
        const char *o, *p;
        {
            Scope s(kLocale, 1);
            o = ((const char *(*)(const uint8_t *, int))0x00107c00)(l, id);
        }
        p = LOCALE_getstr(l, id);
        g_checks++;
        if (o != p) {
            char detail[96];
            snprintf(detail, sizeof(detail), "id %d: +%d / +%d", id, o ? (int)(o - (char *)l) : -1,
                     p ? (int)(p - (char *)l) : -1);
            Report(locale.name.c_str(), detail);
        } else if (p != NULL && strings->size() < 4000) {
            strings->push_back(std::string(p).substr(0, 300));
        }
    }
}

// ---- SHAPE

static void CheckShapes(Entry &shapes) {
    uint8_t *s = shapes.base();
    int vO, vP;
    {
        Scope sc(kShape, 13);
        vO = ((int (*)(const uint8_t *))0x0014a490)(s);
    }
    vP = SHAPE_version(s);
    g_checks++;
    if (vO != vP)
        Report(shapes.name.c_str(), "SHAPE_version differs");
    int count = *(int32_t *)(s + 8);
    for (int i = -1; i <= count; i++) {
        uint32_t nO = 0x12345678, nP = 0x12345678;
        {
            Scope sc(kShape, 13);
            ((void (*)(const uint8_t *, int, uint32_t *))0x00107e30)(s, i, &nO);
        }
        SHAPE_name(s, i, &nP);
        g_checks++;
        if (nO != nP)
            Report(shapes.name.c_str(), "SHAPE_name differs");
        if (i < 0 || i >= count)
            continue;
        uint8_t *image = s + *(int32_t *)(s + 0x14 + i * 8);
        int r[2][6];
        const void *q[2][4];
        for (int side = 0; side < 2; side++) {
            char shortName[5];
            memcpy(shortName, &nP, 4);
            shortName[4] = 0;
            if (side == 0) {
                Scope sc(kShape, 13);
                q[0][0] = ((const char *(*)(const uint8_t *))0x00108280)(image);
                q[0][1] = ((uint8_t *(*)(const uint8_t *))0x001082d0)(image);
                q[0][2] = ((uint8_t *(*)(const uint8_t *))0x001082f0)(image);
                q[0][3] = ((uint8_t *(*)(uint8_t *, const char *))0x00107d40)(s, q[0][0] ? (const char *)q[0][0] : shortName);
                r[0][0] = ((int (*)(const uint8_t *))0x00107df0)(image);
                r[0][1] = ((int (*)(const uint8_t *))0x00108320)(image);
                r[0][2] = ((int (*)(const uint8_t *))0x001082a0)(image);
                r[0][3] = (int)(intptr_t)((uint8_t *(*)(uint8_t *, const char *))0x00107d40)(s, "no such image");
                r[0][4] = 0;
                r[0][5] = 0;
            } else {
                q[1][0] = SHAPE_longname(image);
                q[1][1] = SHAPE_namedata(image);
                q[1][2] = SHAPE_infodata(image);
                q[1][3] = SHAPE_locatez(s, q[1][0] ? (const char *)q[1][0] : shortName);
                r[1][0] = SHAPE_rowbytes(image);
                r[1][1] = SHAPE_depth(image);
                r[1][2] = SHAPE_infoflags(image);
                r[1][3] = (int)(intptr_t)SHAPE_locatez(s, "no such image");
                r[1][4] = 0;
                r[1][5] = 0;
            }
        }
        g_checks++;
        if (memcmp(r[0], r[1], sizeof(r[0])) != 0 || memcmp(q[0], q[1], sizeof(q[0])) != 0)
            Report(shapes.name.c_str(), "an image's queries differ");
    }
}

typedef int (*CreateSizeFn)(int, int, int, int, int, int, int);
typedef void (*CreateAtFn)(uint8_t *, int, int, int, int, int, int, int);

static void CheckCreate() {
    static const int formats[] = { 0, 4, 8, 0xf, 0x10, 0x18, 0x20, 0x1e4, 0x22b, 0x235, 0x378, 0x613, 0x115c,
                                   0x1a0a, 0x22b8, 0x200f12, 3, 0x12 };
    static const int cluts[] = { 0, 15, 16, 17, 18, 24, 32, 7 };
    std::vector<uint8_t> a(4 << 20), b(4 << 20);
    for (int code = -2; code < 0x1000; code++) {
        int o[2], p[2];
        {
            Scope sc(kShape, 13);
            o[0] = ((int (*)(int))0x00108340)(code);
            o[1] = ((int (*)(int))0x001083f0)(code);
        }
        p[0] = SHAPE_type(code);
        p[1] = SHAPE_cluttype(code);
        g_checks++;
        if (memcmp(o, p, sizeof(o)) != 0)
            Report("SHAPE_type/cluttype", "differ");
    }
    for (int run = 0; run < 3000; run++) {
        int w = (run % 7 == 0) ? -(rand() % 4) : rand() % 700, h = (run % 11 == 0) ? 0x10001 : rand() % 520;
        int format = formats[rand() % (sizeof(formats) / sizeof(formats[0]))];
        int clut = cluts[rand() % (sizeof(cluts) / sizeof(cluts[0]))];
        int mips = rand() % 12 - 1, nameBytes = (rand() % 3) ? 0 : rand() % 40, infoBytes = (rand() % 3) ? 0 : rand() % 40;
        int so, sp;
        {
            Scope sc(kShape, 13);
            so = ((CreateSizeFn)0x00107f50)(w, h, format, clut, mips, nameBytes, infoBytes);
        }
        sp = SHAPE_createsize(w, h, format, clut, mips, nameBytes, infoBytes);
        g_checks++;
        if (so != sp) {
            char detail[128];
            snprintf(detail, sizeof(detail), "createsize(%d,%d,%x,%d,%d,%d,%d): %d / %d", w, h, format, clut, mips,
                     nameBytes, infoBytes, so, sp);
            Report("SHAPE_createsize", detail);
            continue;
        }
        if (sp <= 0 || sp > (int)a.size() - 64)
            continue;
        memset(a.data(), 0xa5, sp + 64);
        memset(b.data(), 0xa5, sp + 64);
        {
            Scope sc(kShape, 13);
            ((CreateAtFn)0x00108000)(a.data(), w, h, format, clut, mips, nameBytes, infoBytes);
        }
        SHAPE_createat(b.data(), w, h, format, clut, mips, nameBytes, infoBytes);
        g_checks++;
        if (memcmp(a.data(), b.data(), sp + 64) != 0) {
            char detail[128];
            snprintf(detail, sizeof(detail), "createat(%d,%d,%x,%d,%d,%d,%d) builds different bytes", w, h, format,
                     clut, mips, nameBytes, infoBytes);
            Report("SHAPE_createat", detail);
        }
    }
}

}   // namespace

void RealgraphShadow_Run(void) {
    if (getenv("NIGHTFIRE_RGSHADOW") == NULL)
        return;
    char folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving", folder, sizeof(folder)))
        return;
    std::vector<Entry> entries;
    snprintf(pattern, sizeof(pattern), "%s\\*.viv", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
            ReadArchive(path, found.cFileName, &entries);
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    void **savedDriver = FontDriverSlot;
    void *savedBatch = FontBatch, *savedBatchEx = FontBatchEx;
    FontDriverSlot = g_driver;
    std::vector<std::string> strings;
    int fonts = 0, shapes = 0, locales = 0;
    for (Entry &e : entries)
        if (_stricmp(e.name.c_str() + e.name.size() - 4, ".loc") == 0) {
            CheckLocale(e, &strings);
            locales++;
        }
    for (Entry &e : entries) {
        const char *ext = e.name.c_str() + e.name.size() - 4;
        if (_stricmp(ext, ".xfn") == 0) {
            CheckFont(e, strings);
            fonts++;
        } else if (_stricmp(ext, ".xsh") == 0) {
            CheckShapes(e);
            shapes++;
        }
    }
    CheckCreate();
    FontDriverSlot = savedDriver;
    FontBatch = savedBatch;
    FontBatchEx = savedBatchEx;
    printf("[rgshadow] %d fonts, %d image containers, %d string tables (%u strings), %d checks: %s\n", fonts, shapes,
           locales, (unsigned)strings.size(), g_checks, g_failures == 0 ? "the same as the original" : "FAILED");
    fflush(stdout);
}
