#include "Realgraph.h"

#include "../platform/FileSys.h"
#include "../platform/RealPrint.h"

#include <stdarg.h>
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

typedef float F;

template <typename T> static inline T At(const void *p, int offset) {
    T v;
    memcpy(&v, (const uint8_t *)p + offset, sizeof(T));
    return v;
}
template <typename T> static inline void Put(void *p, int offset, T v) { memcpy((uint8_t *)p + offset, &v, sizeof(T)); }

// ---- FONT. A font: +0xa glyph count (u16), +0xc flags (0x40000: 16-byte glyphs with 16-bit advances and an
// indexed kern table), +0x12/+0x13 ascent and descent, +0x14 glyph table offset, +0x18 kern table offset,
// +0x38/+0x3c the x and y scale. A glyph: +0 code (u16), +2/+3 width and height, +8 advance (s8; +0xe s16 in the
// wide form), +9/+0xa x and y offset (s8), +0xb kern count, +0xc first kern (u16, wide form).

#define FontDriver      (*(void ***)0x001cec98u)     // FONTcurrentdriver: draw, start, end, create, destroy
#define DefaultFont     (*(const uint8_t **)0x00241be0u)
#define FontBatchDraw   (*(void (**)(const uint8_t *, float *, int))0x00241be8u)
#define FontBatchDrawEx (*(void (**)(const uint8_t *, float *, int, int, int))0x00241bf0u)

static const uint32_t kFontRestore = 0x00107b70;    // FONT_restore's own address, as the exit list holds it

// Binary search over count entries of entrySize bytes for the 16-bit code at their start.
// AUTOINJECT
const uint8_t* FONT_bsearch(int code, const uint8_t *table, int count, int entrySize) {
    while (count != 0) {
        const uint8_t *mid = table + (count >> 1) * entrySize;
        int c = code - At<uint16_t>(mid, 0);
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
int FONT_getkern(const uint8_t *font, const uint8_t *glyph, int previous) {
    int n = glyph[0xb];
    if (n == 0)
        return 0;
    const uint8_t *entry = NULL;
    if (At<uint32_t>(font, 0xc) & 0x40000) {
        const uint8_t *table = font + At<int32_t>(font, 0x18) + 4 + At<uint16_t>(glyph, 0xc) * 4;
        for (int i = 0; i < n; i++) {
            if (At<uint16_t>(table, i * 4) == previous) {
                entry = table + i * 4;
                break;
            }
        }
    } else {
        const uint8_t *table = font + At<int32_t>(font, 0x18);
        int total = At<int32_t>(table, 0);
        for (int i = 0; i < total; i++) {
            const uint8_t *e = table + 4 + i * 4;
            if (At<uint16_t>(e, 0) == previous && (uint16_t)e[3] == At<uint16_t>(glyph, 0)) {
                entry = e;
                break;
            }
        }
    }
    return entry != NULL ? (int8_t)entry[2] : 0;
}

static inline int GlyphSize(const uint8_t *font) { return (At<uint32_t>(font, 0xc) & 0x40000) ? 0x10 : 0xc; }

// A glyph by code: straight at code - 0x20 if it is there, else by binary search. A code below 0x20 indexes
// before the table, as the original does.
static const uint8_t *FindGlyph(const uint8_t *font, int code) {
    const uint8_t *table = font + At<int32_t>(font, 0x14);
    int count = At<uint16_t>(font, 0xa), size = GlyphSize(font);
    if (code - 0x20 < count) {
        const uint8_t *g = table + (code - 0x20) * size;
        if (At<uint16_t>(g, 0) == code)
            return g;
    }
    return FONT_bsearch(code, table, count, size);
}

// The other case of a Latin-1 letter, or 0 if it has none.
static int OtherCase(int c) {
    if (c >= 0x41 && c <= 0x5a) return c + 0x20;
    if (c >= 0x61 && c <= 0x7a) return c - 0x20;
    if (c >= 0xc0 && c <= 0xd6) return c + 0x20;
    if (c >= 0xd8 && c <= 0xde) return c + 0x20;
    if (c >= 0xe0 && c <= 0xf6) return c - 0x20;
    if (c >= 0xf8 && c <= 0xfe) return c - 0x20;
    return 0;
}

// The glyph for a character that has none of its own: its other case, else the 0x7f glyph, else none.
static const uint8_t *FallbackGlyph(const uint8_t *font, int c) {
    int other = OtherCase(c);
    if (other != 0) {
        const uint8_t *g = FindGlyph(font, other);
        if (g != NULL)
            return g;
    }
    const uint8_t *table = font + At<int32_t>(font, 0x14);
    int count = At<uint16_t>(font, 0xa), size = GlyphSize(font);
    if (count > 0x5f) {
        const uint8_t *g = table + 0x5f * size;
        if (At<uint16_t>(g, 0) == 0x7f)
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

static inline int Advance(const uint8_t *font, const uint8_t *glyph) {
    return (At<uint32_t>(font, 0xc) & 0x40000) ? At<int16_t>(glyph, 0xe) : (int8_t)glyph[8];
}

static inline int LineHeight(const uint8_t *font, float scaleY) {
    return Ftol((double)(int)(font[0x12] + font[0x13]) * scaleY);
}

// Draws text at (x, y): glyph by glyph through the driver, or in batches of 128 through the batch hooks when
// they are set (with batchArgument, the second hook). A newline returns to x and moves down a line.
// FUNC_AT(0x001073f0)
void FONT_drawtextx(const uint8_t *font, float x, float y, const uint8_t *text, int batchArgument) {
    float startX = x;
    int buffered = 0;
    int first = 1;
    uint8_t previous = 0;
    float batch[128 * 3];
    if (FontDriver[1] != NULL)
        ((FontHookFn)FontDriver[1])(font);
    float scaleY = At<float>(font, 0x3c), scaleX = At<float>(font, 0x38);
    for (; *text != 0; text++) {
        const uint8_t *glyph = FindGlyph(font, *text);
        if (glyph == NULL) {
            if (*text == '\n') {
                y = (F)((double)LineHeight(font, scaleY) + y);
                x = startX;
                previous = 0;
                continue;
            }
            glyph = FallbackGlyph(font, *text);
            if (glyph == NULL)
                continue;
        }
        x = (F)((double)FONT_getkern(font, glyph, previous) * scaleX + x);
        if (FontBatchDraw != NULL) {
            uint32_t g = (uint32_t)(uintptr_t)glyph;
            memcpy(&batch[buffered * 3], &g, 4);
            batch[buffered * 3 + 1] = x;
            batch[buffered * 3 + 2] = y;
            if (++buffered == 0x80) {
                if (batchArgument == 0) {
                    FontBatchDraw(font, batch, 0x80);
                } else {
                    FontBatchDrawEx(font, batch, 0x80, batchArgument, first);
                    first = 0;
                }
                buffered = 0;
            }
        } else {
            ((FontDrawFn)FontDriver[0])(font, glyph, x, y);
        }
        previous = *text;
        x = (F)((double)Advance(font, glyph) * scaleX + x);
    }
    if (buffered != 0) {
        if (batchArgument == 0)
            FontBatchDraw(font, batch, buffered);
        else
            FontBatchDrawEx(font, batch, buffered, batchArgument, first);
    }
    if (FontDriver[2] != NULL)
        ((FontHookFn)FontDriver[2])(font);
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
void FONT_getrectx(const uint8_t *font, const uint8_t *text, float *outX, float *outY, float *outWidth,
                   float *outHeight) {
    const float big = 10000000.0f;   // 0x001a1570
    float minX = big, minY = big, maxX = -big, maxY = -big;
    float scaleX = At<float>(font, 0x38), scaleY = At<float>(font, 0x3c);
    float penX = 0.0f, penY = 0.0f;
    uint8_t previous = 0;
    for (; *text != 0; text++) {
        const uint8_t *glyph = FindGlyph(font, *text);
        if (glyph == NULL) {
            if (*text == '\n') {
                penY = (F)((double)LineHeight(font, scaleY) + penY);
                penX = 0.0f;
                previous = 0;
                continue;
            }
            glyph = FallbackGlyph(font, *text);
            if (glyph == NULL)
                continue;
        }
        double pen = (double)FONT_getkern(font, glyph, previous) * scaleX + penX;   // never stored
        double left = (double)(int8_t)glyph[9] * scaleX + pen;
        float top = (F)((double)(int8_t)glyph[0xa] * scaleY + penY);
        if (left < minX)
            minX = (F)left;
        if (top < minY)
            minY = top;
        double right = (double)glyph[2] * scaleX + left;
        float bottom = (F)((double)glyph[3] * scaleY + top);
        if (right > maxX)
            maxX = (F)right;
        if (bottom > maxY)
            maxY = bottom;
        previous = *text;
        penX = (F)((double)Advance(font, glyph) * scaleX + pen);
    }
    if (outX != NULL)
        *outX = (maxX > minX) ? minX : 0.0f;
    if (outY != NULL)
        *outY = (maxY > minY) ? minY : 0.0f;
    if (outWidth != NULL)
        *outWidth = (maxX > minX) ? (F)((double)maxX - minX) : 0.0f;
    if (outHeight != NULL)
        *outHeight = (maxY > minY) ? (F)((double)maxY - minY) : 0.0f;
}

// The jump callers use (0x00107b30).
// FUNC_AT(0x00107b30)
void FONT_getrectx_thunk(const uint8_t *font, const uint8_t *text, float *x, float *y, float *width, float *height) {
    FONT_getrectx(font, text, x, y, width, height);
}

// AUTOINJECT
const uint8_t* FONT_create(const uint8_t *font) {
    if (FontDriver[3] != NULL)
        ((FontHookFn)FontDriver[3])(font);
    return font;
}

// AUTOINJECT
void FONT_destroy(const uint8_t *font) {
    if (FontDriver[4] != NULL)
        ((FontHookFn)FontDriver[4])(font);
}

// AUTOINJECT
void FONT_restore() {
    if (DefaultFont != NULL) {
        FONT_destroy(DefaultFont);
        DefaultFont = NULL;
        REAL_removeexit((ExitCallback)kFontRestore);
    }
}

// The default font (0x001ceca0), destroyed at exit.
// AUTOINJECT
void FONT_init() {
    if (DefaultFont == NULL) {
        DefaultFont = FONT_create((const uint8_t *)0x001ceca0u);
        REAL_addexit((ExitCallback)kFontRestore);
    }
}

// AUTOINJECT
void FONT_installdriver(void *driver) {
    FontDriver = (void **)driver;
}

// ---- LOCALE. A LOCH file: +4 header size, +8 flags (1: ids go through a sorted {id, index} table after the
// header), +0xe current language, +0x10 the languages' table offsets; a table: +0xc count, +0x10 string offsets.

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
    return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

// The string for id in the current language, or NULL.
// AUTOINJECT
const char* LOCALE_getstr(const uint8_t *locale, int id) {
    if (At<uint32_t>(locale, 8) & 1) {
        int header = At<int32_t>(locale, 4);
        int32_t key = id;
        const uint8_t *found = CrtBsearch(&key, locale + header + 0x10, (size_t)At<uint32_t>(locale, header + 8), 4,
                                          CompareIds);
        id = found != NULL ? At<uint16_t>(found, 2) : -1;
    }
    const uint8_t *table = locale + At<uint32_t>(locale, 0x10 + At<uint16_t>(locale, 0xe) * 4);
    if (id < 0 || (uint32_t)id >= At<uint32_t>(table, 0xc))
        return NULL;
    return (const char *)(table + At<uint32_t>(table, 0x10 + id * 4));
}

// ---- SHAPE. A SHPX file: "SHPX", size, image count (+8), directory id (+0xc), then {name, offset} pairs from
// +0x10. An image header: type byte, the next attachment's offset in bits 8..31, +4 width, +6 height (s16), +0xc
// mip levels in bits 28..31; attachments follow ('p' long name, 'o', 'i', palettes).

#define ShapeAlloc (*(void *(**)(const char *, int, int, int, int))0x001d1874u)   // MEM_allocalign
#define ShapeFree  (*(void (**)(void *))0x001d1878u)                             // MEM_free

// AUTOINJECT
void SHAPE_name(const uint8_t *shapes, int index, uint32_t *name) {
    *name = index < At<int32_t>(shapes, 8) ? At<uint32_t>(shapes, 0x10 + index * 8) : 0;
}

// The image's long name ('p' attachment), or NULL.
// AUTOINJECT
const char* SHAPE_longname(const uint8_t *image) {
    while (image != NULL) {
        int32_t d = At<int32_t>(image, 0);
        if ((uint8_t)d == 0x70)
            return (const char *)(image + 4);
        if ((d >> 8) == 0)
            return NULL;
        image += d >> 8;
    }
    return NULL;
}

static const uint8_t *FindAttachment(const uint8_t *image, uint8_t type) {
    while (image != NULL) {
        int32_t d = At<int32_t>(image, 0);
        if ((uint8_t)d == type)
            return image;
        if ((d >> 8) == 0)
            return NULL;
        image += d >> 8;
    }
    return NULL;
}

// The 'i' attachment's flags (+6), or 0.
// FUNC_AT(0x001082a0)
int SHAPE_infoflags(const uint8_t *image) {
    const uint8_t *a = FindAttachment(image, 0x69);
    return a != NULL ? At<uint16_t>(a, 6) : 0;
}

// The 'o' attachment's data, or NULL.
// FUNC_AT(0x001082d0)
uint8_t* SHAPE_namedata(const uint8_t *image) {
    const uint8_t *a = FindAttachment(image, 0x6f);
    return a != NULL ? (uint8_t *)a + 8 : NULL;
}

// The 'i' attachment's data when its flag 0x10 says it has some, or NULL.
// FUNC_AT(0x001082f0)
uint8_t* SHAPE_infodata(const uint8_t *image) {
    const uint8_t *a = FindAttachment(image, 0x69);
    return (a != NULL && (a[6] & 0x10)) ? (uint8_t *)a + 0x10 : NULL;
}

// An image by name - its long name, else its four-character directory name - or NULL.
// AUTOINJECT
uint8_t* SHAPE_locatez(uint8_t *shapes, const char *name) {
    int count = At<int32_t>(shapes, 8);
    for (int i = 0; i < count; i++) {
        uint8_t *image = shapes + At<int32_t>(shapes, 0x14 + i * 8);
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
            return shapes + At<int32_t>(shapes, 0x14 + i * 8);
    }
    return NULL;
}

static const uint8_t kDepth[128] = {   // 0x001d17f0, bits per pixel by image type
    0, 4, 8, 15, 24, 32, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8, 8, 16, 16, 15, 32, 0, 4, 8, 16, 0, 12, 0, 4, 0,
    15, 32, 24, 0, 24, 0, 0, 0, 0, 16, 32, 0, 32, 15, 32, 0, 16, 16, 15, 32, 0, 0, 0, 0, 32, 15, 0, 0, 0, 0, 0, 0,
    4, 8, 15, 24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8, 15, 16, 16, 24, 32, 0,
    4, 8, 8, 0, 8, 16, 24, 16, 16, 0, 32, 0, 12, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 4, 4, 8, 0, 32, 15, 24 };

// AUTOINJECT
int SHAPE_depth(const uint8_t *image) {
    return kDepth[At<uint32_t>(image, 0) & 0x7f];
}

// Bytes per row: the width itself for type 0x7b, else width * bits per pixel (15 counts as 16), rounded up.
// AUTOINJECT
int SHAPE_rowbytes(const uint8_t *image) {
    int width = At<int16_t>(image, 4);
    if (image[0] == 0x7b)
        return width;
    int depth = SHAPE_depth(image);
    if (depth == 15)
        depth = 16;
    return (depth * width + 7) >> 3;
}

// The image type for a format (bits per pixel, or one of EA's format codes); 0 if unknown.
// AUTOINJECT
int SHAPE_type(int format) {
    switch (format) {
    case 4: return 0x79;
    case 8: return 0x7b;
    case 0xf: case 0x22b: case 0x613: return 0x7e;
    case 0x10: case 0x235: return 0x78;
    case 0x18: case 0x378: return 0x7f;
    case 0x20: case 0x22b8: return 0x7d;
    case 0x1e4: return 0x68;
    case 0x115c: return 0x6d;
    case 0x1a0a: return 0x66;
    case 0x200f12: return 0x6a;
    }
    return 0;
}

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

static void Link(uint8_t *from, uint8_t *to) {   // the header's next-attachment offset
    uint32_t d = At<uint32_t>(from, 0);
    Put<uint32_t>(from, 0, (uint32_t)((to - from) << 8) ^ (d & 0xff));
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
    at[0] = (uint8_t)type;
    Put<uint16_t>(at, 4, (uint16_t)width);
    Put<uint16_t>(at, 6, (uint16_t)height);
    Put<uint32_t>(at, 0xc, (At<uint32_t>(at, 0xc) & 0x0fffffff) | (uint32_t)mipLevels << 28);
    uint8_t *header = at;
    uint8_t *next = at + PixelBytes(format, height, width, mipLevels) + 0x10;
    if (format <= 8) {
        int entries = PaletteEntries(format);
        int c = clutFormat != 0 ? clutFormat : 0x20;
        uint8_t clutType = (uint8_t)SHAPE_cluttype(c);
        int bits = c == 0xf ? 0x10 : c;
        next = at + (((next - at) + 0x3f) & ~0x3f);
        Link(at, next);
        MEM_fill(next, 0, 0x10);
        next[0] = clutType;
        Put<uint16_t>(next, 4, (uint16_t)entries);
        Put<uint16_t>(next, 6, 1);
        uint8_t *palette = (At<uint32_t>(next, 0xc) & 0x1000) ? next + At<int32_t>(next, 0x10) : next + 0x10;
        if (c >= 0x20) {
            for (int i = 0; i < entries; i++) {
                palette[i * 4] = palette[i * 4 + 1] = palette[i * 4 + 2] = (uint8_t)i;
                palette[i * 4 + 3] = 0xff;
            }
        } else {
            MEM_fill(palette, 0xffffffff, DivideBy8(bits * entries));
        }
        header = next;
        next = next + DivideBy8(bits * entries) + 0x10;
    }
    if (nameBytes != 0) {
        Link(header, next);
        MEM_fill(next, 0, nameBytes + 8);
        next[0] = 0x6f;
        Put<int32_t>(next, 4, nameBytes);
        header = next;
        next = next + nameBytes + 8;
    }
    if (infoBytes != 0) {
        Link(header, next);
        MEM_fill(next, 0, infoBytes + 0x10);
        next[0] = 0x69;
        Put<uint16_t>(next, 6, 0x10);
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
    int8_t l = (int8_t)shapes[0xc], a = (int8_t)shapes[0xd], b = (int8_t)shapes[0xe], c = (int8_t)shapes[0xf];
    if (l < 0x41 || l > 0x7a)
        return 0;
    if (a < 0x30 || a > 0x39 || b < 0x30 || b > 0x39 || c < 0x30 || c > 0x39)
        return 0;
    return a * 100 + b * 10 + c - 0x14d0;
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
    if (((uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3]) != 0x53485058) {
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
