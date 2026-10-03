#include "Realgraph.h"

#include "../platform/FileSys.h"
#include "../platform/RealPrint.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's realgraph library, the portable part of the driving engine's graphics: FONT draws and measures text (the
// glyphs are drawn by a driver EAGL installs, EAGLFont), SHAPE reads and builds SHPX image containers (`.xsh`),
// LOCALE looks strings up in LOCH tables (`.loc`). docs/driving/eagl.md 2.12 and 4.13 describe the formats; each
// function here is the original at the same address, ported from it. devtools/RealgraphShadow.cpp compares them
// with the originals on every font, image and string table in the archives.
//
// The text loops position glyphs on the x87; they are ported in double in the original's order, rounding to float
// where the original stores one - including the pen position, which FONT_getrectx keeps unrounded between a
// glyph's kerning and its advance (Ghidra's decompilation shows it rounded; the listing does not).
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---- FONT

namespace {

struct FontHeader {                  // 0x80, an FNTX file (.xfn)
    uint8_t magic[4];                // +0x00 "FNTX"
    int32_t size;                    // +0x04
    uint16_t version;                // +0x08 0x0135
    uint16_t glyphCount;             // +0x0a
    uint32_t flags;                  // +0x0c kWideGlyphs
    uint8_t centre[2];               // +0x10
    uint8_t ascent;                  // +0x12
    uint8_t descent;                 // +0x13
    int32_t glyphOffset;             // +0x14 the glyph table, sorted by code
    int32_t kernOffset;              // +0x18 the kern table
    int32_t shapeOffset;             // +0x1c
    uint8_t unknown20[0x18];
    float scaleX;                    // +0x38
    float scaleY;                    // +0x3c
    uint8_t unknown40[0x40];
};
static_assert(sizeof(FontHeader) == 0x80, "a realgraph Font header is 0x80 bytes");

// 16-byte glyphs with 16-bit advances and an indexed kern table; otherwise glyphs are 12 bytes and kern pairs are
// searched in one table
const uint32_t kWideGlyphs = 0x40000;

struct FontGlyph {                   // 0xc, or 0x10 with kWideGlyphs
    uint16_t code;                   // +0x00
    uint8_t width;                   // +0x02
    uint8_t height;                  // +0x03
    uint8_t unknown04[4];
    int8_t advance;                  // +0x08
    int8_t xOffset;                  // +0x09
    int8_t yOffset;                  // +0x0a
    uint8_t kernCount;               // +0x0b
    uint16_t firstKern;              // +0x0c wide form: the glyph's first entry in the kern table
    int16_t wideAdvance;             // +0x0e wide form: the advance
};
static_assert(sizeof(FontGlyph) == 0x10, "a wide glyph is 0x10 bytes");

struct FontKern {                    // 4
    uint16_t previous;               // +0x00 the character drawn before
    int8_t amount;                   // +0x02
    uint8_t code;                    // +0x03 the glyph it applies to (the narrow form's single table)
};

struct FontKernTable {
    int32_t count;                   // +0x00
    FontKern entries[1];             // +0x04
};

// FONTcurrentdriver's table
struct FontDriverTable {
    FontDrawFn draw;
    FontHookFn start;
    FontHookFn end;
    FontHookFn create;
    FontHookFn destroy;
};

// What the batch hooks are given: 128 of these at most
struct FontBatchEntry {
    const uint8_t *glyph;
    float x;
    float y;
};
static_assert(sizeof(FontBatchEntry) == 12, "a batch entry is three words");

typedef void (*FontBatchFn)(const uint8_t *font, FontBatchEntry *entries, int count);
typedef void (*FontBatchExFn)(const uint8_t *font, FontBatchEntry *entries, int count, int argument, int first);

}  // namespace

#define FontDriver (*(FontDriverTable **)0x001cec98)            // FONTcurrentdriver
#define DefaultFont (*(const uint8_t **)0x00241be0)
#define FontBatchDraw (*(FontBatchFn *)0x00241be8)
#define FontBatchDrawEx (*(FontBatchExFn *)0x00241bf0)
#define BuiltInFont ((const uint8_t *)0x001ceca0)               // the font linked into the executable
#define FontRestoreOriginal ((ExitCallback)0x00107b70)          // FONT_restore's own address, as the exit list holds it

static inline const FontHeader *AsFont(const uint8_t *font) {
    return reinterpret_cast<const FontHeader *>(font);
}

static inline const FontGlyph *AsGlyph(const uint8_t *glyph) {
    return reinterpret_cast<const FontGlyph *>(glyph);
}

// Binary search over count entries of entrySize bytes for the 16-bit code at their start.
// AUTOINJECT
const uint8_t* FONT_bsearch(int code, const uint8_t *table, int count, int entrySize) {
    while (count != 0) {
        const uint8_t *mid = table + (count >> 1) * entrySize;
        int c = code - AsGlyph(mid)->code;
        if (c == 0)
            return mid;
        if (c > 0) {
            table = mid + entrySize;
            count--;
        }
        count >>= 1;
    }
    return NULL;
}

// The kerning between the glyph and the character drawn before it (0 if none).
// AUTOINJECT
int FONT_getkern(const uint8_t *fontData, const uint8_t *glyphData, int previous) {
    const FontHeader *font = AsFont(fontData);
    const FontGlyph *glyph = AsGlyph(glyphData);
    int n = glyph->kernCount;
    if (n == 0)
        return 0;
    const FontKernTable *table = reinterpret_cast<const FontKernTable *>(fontData + font->kernOffset);
    const FontKern *entry = NULL;
    if (font->flags & kWideGlyphs) {
        const FontKern *kerns = &table->entries[glyph->firstKern];
        for (int i = 0; i < n; i++) {
            if (kerns[i].previous == previous) {
                entry = &kerns[i];
                break;
            }
        }
    } else {
        for (int i = 0; i < table->count; i++) {
            const FontKern *e = &table->entries[i];
            if (e->previous == previous && e->code == glyph->code) {
                entry = e;
                break;
            }
        }
    }
    return entry != NULL ? entry->amount : 0;
}

static inline int GlyphSize(const FontHeader *font) {
    return (font->flags & kWideGlyphs) ? 0x10 : 0xc;
}

// A glyph by code: straight at code - 0x20 if it is there, else by binary search. A code below 0x20 indexes
// before the table, as the original does.
static const uint8_t *FindGlyph(const uint8_t *fontData, int code) {
    const FontHeader *font = AsFont(fontData);
    const uint8_t *table = fontData + font->glyphOffset;
    int count = font->glyphCount, size = GlyphSize(font);
    if (code - 0x20 < count) {
        const uint8_t *g = table + (code - 0x20) * size;
        if (AsGlyph(g)->code == code)
            return g;
    }
    return FONT_bsearch(code, table, count, size);
}

// The other case of a Latin-1 letter, or 0 if it has none.
static int OtherCase(int c) {
    if (c >= 'A' && c <= 'Z') return c + 0x20;
    if (c >= 'a' && c <= 'z') return c - 0x20;
    if (c >= 0xc0 && c <= 0xd6) return c + 0x20;
    if (c >= 0xd8 && c <= 0xde) return c + 0x20;
    if (c >= 0xe0 && c <= 0xf6) return c - 0x20;
    if (c >= 0xf8 && c <= 0xfe) return c - 0x20;
    return 0;
}

// The glyph for a character that has none of its own: its other case, else the 0x7f glyph, else none.
static const uint8_t *FallbackGlyph(const uint8_t *fontData, int c) {
    int other = OtherCase(c);
    if (other != 0) {
        const uint8_t *g = FindGlyph(fontData, other);
        if (g != NULL)
            return g;
    }
    const FontHeader *font = AsFont(fontData);
    const uint8_t *table = fontData + font->glyphOffset;
    int count = font->glyphCount, size = GlyphSize(font);
    if (count > 0x5f) {
        const uint8_t *g = table + 0x5f * size;
        if (AsGlyph(g)->code == 0x7f)
            return g;
    }
    return FONT_bsearch(0x7f, table, count, size);
}

// __ftol2: truncation to 64 bits, of which the low 32 are used; what it cannot convert gives 0x80000000_00000000.
static inline int32_t Ftol(double v) {
    if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0))
        return 0;
    return (int32_t)(int64_t)v;
}

static inline int Advance(const FontHeader *font, const FontGlyph *glyph) {
    return (font->flags & kWideGlyphs) ? glyph->wideAdvance : glyph->advance;
}

static inline int LineHeight(const FontHeader *font, float scaleY) {
    return Ftol(double(font->ascent + font->descent) * scaleY);
}

// Draws text at (x, y): glyph by glyph through the driver, or in batches of 128 through the batch hooks when
// they are set (with batchArgument, the second hook). A newline returns to x and moves down a line.
// FUNC_AT(0x001073f0)
void FONT_drawtextx(const uint8_t *fontData, float x, float y, const uint8_t *text, int batchArgument) {
    const FontHeader *font = AsFont(fontData);
    float startX = x;
    int buffered = 0;
    int first = 1;
    uint8_t previous = 0;
    FontBatchEntry batch[128];
    if (FontDriver->start != NULL)
        FontDriver->start(fontData);
    float scaleY = font->scaleY, scaleX = font->scaleX;
    for (; *text != 0; text++) {
        const uint8_t *glyph = FindGlyph(fontData, *text);
        if (glyph == NULL) {
            if (*text == '\n') {
                y = float(double(LineHeight(font, scaleY)) + y);
                x = startX;
                previous = 0;
                continue;
            }
            glyph = FallbackGlyph(fontData, *text);
            if (glyph == NULL)
                continue;
        }
        x = float(double(FONT_getkern(fontData, glyph, previous)) * scaleX + x);
        if (FontBatchDraw != NULL) {
            batch[buffered].glyph = glyph;
            batch[buffered].x = x;
            batch[buffered].y = y;
            if (++buffered == 0x80) {
                if (batchArgument == 0) {
                    FontBatchDraw(fontData, batch, 0x80);
                } else {
                    FontBatchDrawEx(fontData, batch, 0x80, batchArgument, first);
                    first = 0;
                }
                buffered = 0;
            }
        } else {
            FontDriver->draw(fontData, glyph, x, y);
        }
        previous = *text;
        x = float(double(Advance(font, AsGlyph(glyph))) * scaleX + x);
    }
    if (buffered != 0) {
        if (batchArgument == 0)
            FontBatchDraw(fontData, batch, buffered);
        else
            FontBatchDrawEx(fontData, batch, buffered, batchArgument, first);
    }
    if (FontDriver->end != NULL)
        FontDriver->end(fontData);
}

// AUTOINJECT
void FONT_drawtexta(const uint8_t *font, float x, float y, const uint8_t *text) {
    FONT_drawtextx(font, x, y, text, 0);
}

// printf, then FONT_drawtexta.
// AUTOINJECT
void FONT_drawtextfa(const uint8_t *font, float x, float y, const char *format, ...) {
    char buffer[0x2000];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    FONT_drawtexta(font, x, y, (const uint8_t *)buffer);
}

// The rectangle text covers, from (0, 0): each output optional; an empty text gives 0s.
// FUNC_AT(0x00107770)
void FONT_getrectx(const uint8_t *fontData, const uint8_t *text, float *outX, float *outY, float *outWidth,
                   float *outHeight) {
    const FontHeader *font = AsFont(fontData);
    const float big = 10000000.0f;
    float minX = big, minY = big, maxX = -big, maxY = -big;
    float scaleX = font->scaleX, scaleY = font->scaleY;
    float penX = 0.0f, penY = 0.0f;
    uint8_t previous = 0;
    for (; *text != 0; text++) {
        const uint8_t *glyphData = FindGlyph(fontData, *text);
        if (glyphData == NULL) {
            if (*text == '\n') {
                penY = float(double(LineHeight(font, scaleY)) + penY);
                penX = 0.0f;
                previous = 0;
                continue;
            }
            glyphData = FallbackGlyph(fontData, *text);
            if (glyphData == NULL)
                continue;
        }
        const FontGlyph *glyph = AsGlyph(glyphData);
        double pen = double(FONT_getkern(fontData, glyphData, previous)) * scaleX + penX;   // never stored
        double left = double(glyph->xOffset) * scaleX + pen;
        float top = float(double(glyph->yOffset) * scaleY + penY);
        if (left < minX)
            minX = float(left);
        if (top < minY)
            minY = top;
        double right = double(glyph->width) * scaleX + left;
        float bottom = float(double(glyph->height) * scaleY + top);
        if (right > maxX)
            maxX = float(right);
        if (bottom > maxY)
            maxY = bottom;
        previous = *text;
        penX = float(double(Advance(font, glyph)) * scaleX + pen);
    }
    if (outX != NULL)
        *outX = (maxX > minX) ? minX : 0.0f;
    if (outY != NULL)
        *outY = (maxY > minY) ? minY : 0.0f;
    if (outWidth != NULL)
        *outWidth = (maxX > minX) ? maxX - minX : 0.0f;
    if (outHeight != NULL)
        *outHeight = (maxY > minY) ? maxY - minY : 0.0f;
}

// The jump callers use (0x00107b30).
// FUNC_AT(0x00107b30)
void FONT_getrectx_thunk(const uint8_t *font, const uint8_t *text, float *x, float *y, float *width, float *height) {
    FONT_getrectx(font, text, x, y, width, height);
}

// AUTOINJECT
const uint8_t* FONT_create(const uint8_t *font) {
    if (FontDriver->create != NULL)
        FontDriver->create(font);
    return font;
}

// AUTOINJECT
void FONT_destroy(const uint8_t *font) {
    if (FontDriver->destroy != NULL)
        FontDriver->destroy(font);
}

// AUTOINJECT
void FONT_restore() {
    if (DefaultFont != NULL) {
        FONT_destroy(DefaultFont);
        DefaultFont = NULL;
        REAL_removeexit(FontRestoreOriginal);
    }
}

// The default font, destroyed at exit.
// AUTOINJECT
void FONT_init() {
    if (DefaultFont == NULL) {
        DefaultFont = FONT_create(BuiltInFont);
        REAL_addexit(FontRestoreOriginal);
    }
}

// AUTOINJECT
void FONT_installdriver(void *driver) {
    FontDriver = static_cast<FontDriverTable *>(driver);
}

// ---- LOCALE

namespace {

struct LocaleFile {                  // a LOCH file (.loc)
    uint8_t magic[4];                // +0x00 "LOCH"
    int32_t headerSize;              // +0x04 where the id index starts
    uint32_t flags;                  // +0x08 kIndexedIds
    uint16_t languageCount;          // +0x0c
    uint16_t language;               // +0x0e the current one
    uint32_t tableOffsets[1];        // +0x10 each language's string table
};

const uint32_t kIndexedIds = 1;      // ids go through the sorted index after the header

struct LocaleIdEntry {
    uint16_t id;
    uint16_t index;                  // the string's number in the tables
};

struct LocaleIdIndex {               // at headerSize
    uint32_t unknown00;
    uint32_t unknown04;
    uint32_t count;                  // +0x08
    uint32_t unknown0c;
    LocaleIdEntry entries[1];        // +0x10, sorted by id
};

struct LocaleTable {                 // a "LOCI" string table
    uint32_t unknown00;
    uint32_t unknown04;
    uint32_t unknown08;
    uint32_t count;                  // +0x0c
    uint32_t offsets[1];             // +0x10 each string's, from the table
};

}  // namespace

// MSVC's bsearch, which the original calls (0x0013414b): which of several equal keys it finds depends on its
// halving, so it is reproduced as the C runtime has it.
static const uint8_t *CrtBsearch(const void *key, const uint8_t *base, size_t count, size_t width,
                                 int (*compare)(const void *, const void *)) {
    const uint8_t *lo = base, *hi = base + (count - 1) * width;
    while (lo <= hi) {
        size_t half = count / 2;
        if (half != 0) {
            const uint8_t *mid = lo + ((count & 1) ? half : half - 1) * width;
            int result = compare(key, mid);
            if (result == 0)
                return mid;
            if (result < 0) {
                hi = mid - width;
                count = (count & 1) ? half : half - 1;
            } else {
                lo = mid + width;
                count = half;
            }
        } else if (count != 0) {
            return compare(key, lo) ? NULL : lo;
        } else {
            break;
        }
    }
    return NULL;
}

static int CompareIds(const void *a, const void *b) {   // 0x00107be0
    return *static_cast<const uint16_t *>(a) - *static_cast<const uint16_t *>(b);
}

// The string for id in the current language, or NULL.
// AUTOINJECT
const char* LOCALE_getstr(const uint8_t *localeData, int id) {
    const LocaleFile *locale = reinterpret_cast<const LocaleFile *>(localeData);
    if (locale->flags & kIndexedIds) {
        const LocaleIdIndex *index = reinterpret_cast<const LocaleIdIndex *>(localeData + locale->headerSize);
        int32_t key = id;
        const uint8_t *found = CrtBsearch(&key, reinterpret_cast<const uint8_t *>(index->entries), index->count,
                                          sizeof(LocaleIdEntry), CompareIds);
        id = found != NULL ? reinterpret_cast<const LocaleIdEntry *>(found)->index : -1;
    }
    const uint8_t *tableData = localeData + locale->tableOffsets[locale->language];
    const LocaleTable *table = reinterpret_cast<const LocaleTable *>(tableData);
    if (id < 0 || (uint32_t)id >= table->count)
        return NULL;
    return (const char *)(tableData + table->offsets[id]);
}

// ---- SHAPE. An image header is followed by its pixels; attachments follow, each starting with the same link word
// ('p' long name, 'o' name data, 'i' info, palettes).

namespace {

struct ShapeImage {                  // 0x10
    int32_t link;                    // +0x00 the type in the low byte, the next attachment's offset in bits 8..31
    int16_t width;                   // +0x04
    int16_t height;                  // +0x06
    uint32_t unknown08;
    uint32_t bits;                   // +0x0c bits 28..31: mip levels
};
static_assert(sizeof(ShapeImage) == 0x10, "a SHAPE image header is 0x10 bytes");

struct ShapePalette {                // a palette attachment: the image header's layout, the colours after it
    int32_t link;                    // +0x00
    uint16_t entries;                // +0x04
    uint16_t rows;                   // +0x06 1
    uint32_t unknown08;
    uint32_t bits;                   // +0x0c kColoursElsewhere
    int32_t colourOffset;            // +0x10 with kColoursElsewhere: where the colours are, from the palette
};

const uint32_t kColoursElsewhere = 0x1000;

struct ShapeNameData {               // 'o'
    int32_t link;                    // +0x00
    int32_t bytes;                   // +0x04 the data's, from +0x08
};

struct ShapeInfo {                   // 'i'
    int32_t link;                    // +0x00
    uint16_t unknown04;
    uint16_t flags;                  // +0x06 kInfoHasData
    uint32_t unknown08;
    uint32_t unknown0c;
};

const uint16_t kInfoHasData = 0x10;  // data from +0x10

// Image types, by bits per pixel where SHAPE_type maps a depth to them
enum ShapeType : uint8_t {
    kType4Bit = 0x79, kType8Bit = 0x7b, kType15Bit = 0x7e, kType16Bit = 0x78, kType24Bit = 0x7f, kType32Bit = 0x7d,
    kType66 = 0x66, kType68 = 0x68, kType6a = 0x6a, kType6d = 0x6d,
};

}  // namespace

#define ShapeAlloc (*(void *(**)(const char *, int, int, int, int))0x001d1874)   // MEM_allocalign
#define ShapeFree (*(void (**)(void *))0x001d1878)                               // MEM_free

static inline const ShapeFile *AsShapes(const uint8_t *shapes) {
    return reinterpret_cast<const ShapeFile *>(shapes);
}

static inline int32_t Link(const uint8_t *block) {
    return reinterpret_cast<const ShapeImage *>(block)->link;
}

// AUTOINJECT
void SHAPE_name(const uint8_t *shapes, int index, uint32_t *name) {
    const ShapeFile *file = AsShapes(shapes);
    *name = index < file->count ? file->entries[index].name : 0;
}

// The first attachment of a type along the image's chain (the image itself included), or NULL.
static const uint8_t *FindAttachment(const uint8_t *image, uint8_t type) {
    while (image != NULL) {
        int32_t link = Link(image);
        if ((uint8_t)link == type)
            return image;
        if ((link >> 8) == 0)
            return NULL;
        image += link >> 8;
    }
    return NULL;
}

// The image's long name ('p' attachment), or NULL.
// AUTOINJECT
const char* SHAPE_longname(const uint8_t *image) {
    const uint8_t *a = FindAttachment(image, 'p');
    return a != NULL ? (const char *)(a + 4) : NULL;
}

// The 'i' attachment's flags, or 0.
// FUNC_AT(0x001082a0)
int SHAPE_infoflags(const uint8_t *image) {
    const uint8_t *a = FindAttachment(image, 'i');
    return a != NULL ? reinterpret_cast<const ShapeInfo *>(a)->flags : 0;
}

// The 'o' attachment's data, or NULL.
// FUNC_AT(0x001082d0)
uint8_t* SHAPE_namedata(const uint8_t *image) {
    const uint8_t *a = FindAttachment(image, 'o');
    return a != NULL ? const_cast<uint8_t *>(a) + sizeof(ShapeNameData) : NULL;
}

// The 'i' attachment's data when its flags say it has some, or NULL.
// FUNC_AT(0x001082f0)
uint8_t* SHAPE_infodata(const uint8_t *image) {
    const uint8_t *a = FindAttachment(image, 'i');
    if (a == NULL || !(reinterpret_cast<const ShapeInfo *>(a)->flags & kInfoHasData))
        return NULL;
    return const_cast<uint8_t *>(a) + sizeof(ShapeInfo);
}

// An image by name - its long name, else its four-character directory name - or NULL.
// AUTOINJECT
uint8_t* SHAPE_locatez(uint8_t *shapes, const char *name) {
    const ShapeFile *file = AsShapes(shapes);
    for (int i = 0; i < file->count; i++) {
        uint8_t *image = shapes + file->entries[i].offset;
        const char *n = SHAPE_longname(image);
        char shortName[5];
        if (n == NULL) {
            uint32_t four;
            SHAPE_name(shapes, i, &four);
            memcpy(shortName, &four, 4);
            shortName[4] = 0;
            n = shortName;
        }
        if (strcmp(n, name) == 0)
            return shapes + file->entries[i].offset;
    }
    return NULL;
}

static const uint8_t kDepth[128] = {   // bits per pixel by image type (the original's table at 0x001d17f0)
    0, 4, 8, 15, 24, 32, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8, 8, 16, 16, 15, 32, 0, 4, 8, 16, 0, 12, 0, 4, 0,
    15, 32, 24, 0, 24, 0, 0, 0, 0, 16, 32, 0, 32, 15, 32, 0, 16, 16, 15, 32, 0, 0, 0, 0, 32, 15, 0, 0, 0, 0, 0, 0,
    4, 8, 15, 24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8, 15, 16, 16, 24, 32, 0,
    4, 8, 8, 0, 8, 16, 24, 16, 16, 0, 32, 0, 12, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 4, 4, 8, 0, 32, 15, 24 };

// AUTOINJECT
int SHAPE_depth(const uint8_t *image) {
    return kDepth[Link(image) & 0x7f];
}

// Bytes per row: the width itself for 8-bit images, else width * bits per pixel (15 counts as 16), rounded up.
// AUTOINJECT
int SHAPE_rowbytes(const uint8_t *imageData) {
    const ShapeImage *image = reinterpret_cast<const ShapeImage *>(imageData);
    int width = image->width;
    if ((uint8_t)image->link == kType8Bit)
        return width;
    int depth = SHAPE_depth(imageData);
    if (depth == 15)
        depth = 16;
    return (depth * width + 7) >> 3;
}

// The image type for a format (bits per pixel, or one of EA's format codes); 0 if unknown.
// AUTOINJECT
int SHAPE_type(int format) {
    switch (format) {
    case 4: return kType4Bit;
    case 8: return kType8Bit;
    case 0xf: case 0x22b: case 0x613: return kType15Bit;
    case 0x10: case 0x235: return kType16Bit;
    case 0x18: case 0x378: return kType24Bit;
    case 0x20: case 0x22b8: return kType32Bit;
    case 0x1e4: return kType68;
    case 0x115c: return kType6d;
    case 0x1a0a: return kType66;
    case 0x200f12: return kType6a;
    }
    return 0;
}

// The palette type for a colour depth; 0 if unknown.
// AUTOINJECT
int SHAPE_cluttype(int format) {
    switch (format) {
    case 15: return 0x2d;
    case 16: return 0x29;
    case 18: return 0x22;
    case 24: return 0x24;
    case 32: return 0x2a;
    }
    return 0;
}

static inline int DivideBy8(int v) { return v / 8; }   // the original's CDQ/AND 7/SAR: toward zero

// The pixel bytes of an image and its mip levels; 0 for a bad size or format. The original takes the format in
// EAX and the height in EBX (0x00107e60).
static int PixelBytes(int format, int height, int width, int mipLevels) {
    if (format == 0)
        format = 0x20;
    uint8_t type = (uint8_t)SHAPE_type(format);
    int depth = kDepth[type & 0x7f];
    int bits = depth == 15 ? 0x10 : depth;
    if (width <= 0 || width > 0x10000 || height <= 0 || height > 0x10000 || width * height > 1000000000)
        return 0;
    if (SHAPE_type(depth) == 0)
        return 0;
    int total = 0;
    for (int i = 0; i <= mipLevels; i++) {
        int w = width >> i, h = height >> i;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        total += DivideBy8(w * bits + 7) * h;
        if (w == 1 && h == 1)
            break;
    }
    return total;
}

static inline int PaletteEntries(int format) { return format == 4 ? 0x10 : format == 8 ? 0x100 : 0; }

// The bytes SHAPE_createat needs for such an image.
// AUTOINJECT
int SHAPE_createsize(int width, int height, int format, int clutFormat, int mipLevels, int nameBytes, int infoBytes) {
    int f = format != 0 ? format : 0x20;
    uint8_t type = (uint8_t)SHAPE_type(f);
    int depth = kDepth[type & 0x7f];
    int size = PixelBytes(format, height, width, mipLevels) + 0x10;
    if (depth <= 8) {
        size = (size + 0x3f) & ~0x3f;
        int c = clutFormat != 0 ? clutFormat : 0x20;
        int palette = DivideBy8(PaletteEntries(depth) * c);
        size = palette + ((size + 0xf) & ~0xf) + 0x10;
    }
    if (nameBytes != 0)
        size = size + nameBytes + 8;
    return infoBytes != 0 ? size + infoBytes + 0x10 : size;
}

// The block's next-attachment offset, its type byte kept
static void SetLink(uint8_t *from, uint8_t *to) {
    int32_t &link = reinterpret_cast<ShapeImage *>(from)->link;
    link = (int32_t)((uint32_t)((to - from) << 8) ^ ((uint32_t)link & 0xff));
}

// Builds an image in place: header, a grey (or blank) palette for 4- and 8-bit formats, then the optional 'o' and
// 'i' attachments.
// AUTOINJECT
void SHAPE_createat(uint8_t *at, int width, int height, int format, int clutFormat, int mipLevels, int nameBytes,
                    int infoBytes) {
    if (format == 0)
        format = 0x20;
    int type = SHAPE_type(format);
    MEM_fill(at, 0, 0x10);
    ShapeImage *image = reinterpret_cast<ShapeImage *>(at);
    at[0] = (uint8_t)type;   // the link word's low byte
    image->width = (int16_t)width;
    image->height = (int16_t)height;
    image->bits = (image->bits & 0x0fffffff) | (uint32_t)mipLevels << 28;
    uint8_t *header = at;
    uint8_t *next = at + PixelBytes(format, height, width, mipLevels) + 0x10;
    if (format <= 8) {
        int entries = PaletteEntries(format);
        int c = clutFormat != 0 ? clutFormat : 0x20;
        uint8_t clutType = (uint8_t)SHAPE_cluttype(c);
        int bits = c == 0xf ? 0x10 : c;
        next = at + (((next - at) + 0x3f) & ~0x3f);
        SetLink(at, next);
        MEM_fill(next, 0, 0x10);
        ShapePalette *palette = reinterpret_cast<ShapePalette *>(next);
        next[0] = clutType;
        palette->entries = (uint16_t)entries;
        palette->rows = 1;
        uint8_t *colours = (palette->bits & kColoursElsewhere) ? next + palette->colourOffset : next + 0x10;
        if (c >= 0x20) {
            for (int i = 0; i < entries; i++) {
                colours[i * 4] = colours[i * 4 + 1] = colours[i * 4 + 2] = (uint8_t)i;
                colours[i * 4 + 3] = 0xff;
            }
        } else {
            MEM_fill(colours, 0xffffffff, DivideBy8(bits * entries));
        }
        header = next;
        next = next + DivideBy8(bits * entries) + 0x10;
    }
    if (nameBytes != 0) {
        SetLink(header, next);
        MEM_fill(next, 0, nameBytes + 8);
        next[0] = 'o';
        reinterpret_cast<ShapeNameData *>(next)->bytes = nameBytes;
        header = next;
        next = next + nameBytes + 8;
    }
    if (infoBytes != 0) {
        SetLink(header, next);
        MEM_fill(next, 0, infoBytes + 0x10);
        next[0] = 'i';
        reinterpret_cast<ShapeInfo *>(next)->flags = kInfoHasData;
    }
}

// Allocates (through SHAPE's allocator, named "SHP<w>x<h>x<format>") and builds an image.
// AUTOINJECT
uint8_t* SHAPE_create(int width, int height, int format, int clutFormat, int mipLevels, int allocFlags,
                      int nameBytes, int infoBytes) {
    int size = SHAPE_createsize(width, height, format, clutFormat, mipLevels, nameBytes, infoBytes);
    if (size == 0)
        return NULL;
    char label[16];
    snprintf(label, sizeof(label), "SHP%dx%dx%d", width, height, format);
    uint8_t *shape = (uint8_t *)ShapeAlloc(label, size, 0, 0x10, allocFlags);
    if (shape != NULL)
        SHAPE_createat(shape, width, height, format, clutFormat, mipLevels, nameBytes, infoBytes);
    return shape;
}

// The directory id's version: a letter then three digits ("G344" is 344); 0 otherwise.
// AUTOINJECT
int SHAPE_version(const uint8_t *shapes) {
    const int8_t *id = AsShapes(shapes)->id;
    int8_t l = id[0], a = id[1], b = id[2], c = id[3];
    if (l < 'A' || l > 'z')
        return 0;
    if (a < '0' || a > '9' || b < '0' || b > '9' || c < '0' || c > '9')
        return 0;
    return a * 100 + b * 10 + c - 0x14d0;   // 0x14d0 = '0' * 111
}

// Loads a SHPX file (".xsh" added when the name has no extension) and unpacks it if packed; NULL if it is not a
// SHPX file. The original takes the name in EAX (0x0014a370) and then reads SHAPE_version of a rejected file's
// NULL, which would fault; the version was never used, so it is not asked for here.
static uint8_t *LoadShapes(const char *name, int flags) {
    char path[0x80];
    snprintf(path, sizeof(path), "%s", name);
    const char *p = path[0] != 0 ? path + strlen(path) - 1 : path;
    bool extension = false;
    while (p > path) {
        if (*p == '.') {
            extension = true;
            break;
        }
        if (*p == ':' || *p == '/' || *p == '\\')
            break;
        p--;
    }
    if (!extension && *p != '.' && strlen(path) + 5 <= sizeof(path))
        strcat(path, ".xsh");
    uint8_t *data = (uint8_t *)FILE_loadpackz(path, flags);
    if (data == NULL)
        return NULL;
    if (memcmp(data, "SHPX", 4) != 0) {
        ShapeFree(data);
        return NULL;
    }
    return data;
}

// AUTOINJECT
uint8_t* SHAPE_loadfile(const char *name, int flags) {
    return LoadShapes(name, flags);
}

// AUTOINJECT
uint8_t* SHAPE_loadfilez(const char *name, int flags) {
    return LoadShapes(name, flags);
}
