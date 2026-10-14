#ifndef DRIVING_EAGL_REALGRAPH_H_
#define DRIVING_EAGL_REALGRAPH_H_

// EA's realgraph library as the driving engine has it: FONT (text drawn through a driver EAGL installs), SHAPE
// (the SHPX image container) and LOCALE (LOCH string tables). See Realgraph.cpp and docs/driving/eagl.md 4.13.

#include <stddef.h>
#include <stdint.h>

// A SHPX file (.xsh): the header and its directory, one entry per image (docs/driving/eagl.md 2.12)
struct ShapeDirectoryEntry {
    uint32_t name;                   // four characters, space padded
    int32_t offset;                  // the image header's, from the start of the file
};

struct ShapeFile {
    uint8_t magic[4];                // +0x00 "SHPX"
    int32_t size;                    // +0x04
    int32_t count;                   // +0x08 images
    int8_t id[4];                    // +0x0c directory id: a letter and three digits ("G344", SHAPE_version)
    ShapeDirectoryEntry entries[1];  // +0x10 count of them
};

// An image's or attachment's first word: its type, and the offset from it to the next attachment (0 at the last)
constexpr uint32_t ShapeHeaderWord(uint32_t type, uint32_t next) { return type | next << 8; }

// A DXT1 block: 4x4 texels, two RGB565 colours and two bits a texel
struct DxtBlock {
    uint16_t colour0;
    uint16_t colour1;
    uint32_t indices;
};
static_assert(sizeof(DxtBlock) == 8, "a DXT1 block is 8 bytes");

// The attachments EAGL's tools leave after an image: 'i', with room for the TAR's shared data (a TAR made from the
// image keeps its record there), and 'p', the image's long name
struct ShapeInfoAttachment {         // 0x50
    uint32_t header;                 // +0x00 'i'
    uint16_t unknown04;              // +0x04
    uint16_t unknown06;              // +0x06
    uint8_t unknown08[8];            // +0x08
    char text[0x40];                 // +0x10 an "EAGL" block
};
static_assert(sizeof(ShapeInfoAttachment) == 0x50, "an 'i' attachment is 0x50 bytes");

struct ShapeNameAttachment {         // 0x10
    uint32_t header;                 // +0x00 'p'
    char name[12];                   // +0x04
};

// EAGL's own SHPX, linked into the executable: one 64x64 DXT1 image "fail", with its mipmaps and an 'i' and a 'p'
// attachment, the image a TAR falls back on when its shape is missing
struct BuiltInShapeFile {            // 0xba0
    ShapeFile file;                  // +0x000
    char comment[0x58];              // +0x018
    uint32_t imageHeader;            // +0x070 type 0x60, the 'i' attachment next
    int16_t width;                   // +0x074
    int16_t height;                  // +0x076
    uint32_t unknown78;              // +0x078
    uint32_t flags;                  // +0x07c ShapeImage::flags: seven mip levels
    DxtBlock level0[16][16];         // +0x080 64x64
    DxtBlock level1[8][8];           // +0x880 32x32
    DxtBlock level2[4][4];           // +0xa80
    DxtBlock level3[2][2];           // +0xb00
    DxtBlock level4[1][1];           // +0xb20
    DxtBlock level5[1][1];           // +0xb28
    DxtBlock level6[1][1];           // +0xb30 1x1
    uint8_t padding[8];              // +0xb38
    ShapeInfoAttachment info;        // +0xb40 the 'p' attachment next
    ShapeNameAttachment longName;    // +0xb90 the last
};
static_assert(sizeof(BuiltInShapeFile) == 0xba0, "EAGL's built-in SHPX is 0xba0 bytes");
static_assert(offsetof(BuiltInShapeFile, level0) == 0x80 && offsetof(BuiltInShapeFile, info) == 0xb40 &&
              offsetof(BuiltInShapeFile, longName) == 0xb90, "BuiltInShapeFile layout");

namespace EAGL {
struct TAR;
}

// A realgraph FNTX font (.xfn): the header FONT reads (Realgraph.cpp) and in which the driver (EaglFont.cpp) keeps
// its TAR, shape and texture scale
struct FNTXFont {
    uint8_t magic[4];                // +0x00 "FNTX"
    int32_t size;                    // +0x04
    uint16_t version;                // +0x08
    uint16_t glyphCount;             // +0x0a
    uint32_t flags;                  // +0x0c 0x40000: 16-byte glyphs with 16-bit advances and an indexed kern table
    uint8_t centre[2];               // +0x10
    uint8_t ascent;                  // +0x12
    uint8_t descent;                 // +0x13
    int32_t glyphTableOffset;        // +0x14
    int32_t kernTableOffset;         // +0x18
    int32_t shapeOffset;             // +0x1c the font's SHAPE image, from the font
    uint32_t colour;                 // +0x20 every vertex's
    uint32_t unknown24;              // +0x24
    uint32_t z;                      // +0x28 every vertex's z and w (float bits, passed on as they are)
    uint32_t w;                      // +0x2c
    float texOffsetU;                // +0x30
    float texOffsetV;                // +0x34
    float scaleX;                    // +0x38 position scale
    float scaleY;                    // +0x3c
    float unknown40[2];              // +0x40
    int32_t filterMode;              // +0x48 1: the TAR's filter 2, else 1
    int32_t depthMode;               // +0x4c 0: no Z writes and ALWAYS, 1: Z writes and LEQUAL, else left
    uint8_t unknown50[0x20];         // +0x50
    EAGL::TAR *tar;                  // +0x70 over the shape (FONTEAGL_createfont)
    uint8_t *shape;                  // +0x74
    float texScaleV;                 // +0x78 1 / the shape's height when it is linear, else 1
    float texScaleU;                 // +0x7c 1 / the shape's width
};
static_assert(sizeof(FNTXFont) == 0x80, "an FNTX header is 0x80 bytes");

// A glyph of the 12-byte form (without the 0x40000 flag)
struct FNTXGlyph {
    uint16_t code;                   // +0x00
    uint8_t width;                   // +0x02
    uint8_t height;                  // +0x03
    uint16_t texX;                   // +0x04 position in the texture
    uint16_t texY;                   // +0x06
    int8_t advance;                  // +0x08
    int8_t xOffset;                  // +0x09
    int8_t yOffset;                  // +0x0a
    uint8_t kernCount;               // +0x0b
};
static_assert(sizeof(FNTXGlyph) == 0xc, "a glyph is 12 bytes");

// An FNTX file linked into the executable: the header, Glyphs glyphs of the 12-byte form, a comment, and the
// texture, an 8-bit SHAPE image (type 0x64) of Width x Height with an 'i' attachment
template <int Glyphs, int CommentBytes, int Width, int Height>
struct LinkedFontFile {
    FNTXFont header;
    FNTXGlyph glyphs[Glyphs];
    char comment[CommentBytes];
    uint32_t imageHeader;            // type 0x64, the 'i' attachment next
    int16_t width;
    int16_t height;
    uint32_t unknown08;
    uint32_t flags;
    uint8_t pixels[Height][Width];
    ShapeInfoAttachment info;
};

// realgraph's own font: 111 glyphs of 8x8 on a 128x73 texture
typedef LinkedFontFile<111, 0x3c, 128, 73> BuiltInFontFile;
static_assert(sizeof(BuiltInFontFile) == 0x2ad0, "the built-in font is 0x2ad0 bytes");
static_assert(offsetof(BuiltInFontFile, imageHeader) == 0x5f0 && offsetof(BuiltInFontFile, info) == 0x2a80,
              "BuiltInFontFile layout");
extern BuiltInFontFile BuiltInFont;  // FONT_init's

typedef void (*FontDrawFn)(const uint8_t *font, const uint8_t *glyph, float x, float y);
typedef void (*FontHookFn)(const uint8_t *font);

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

extern FontDriverTable *FontDriver;  // FONTcurrentdriver
extern FontBatchFn FontBatchDraw;    // the batch hooks: nothing in the game sets them
extern FontBatchExFn FontBatchDrawEx;

// ---- FONT
void FONT_drawtextfa(const uint8_t *font, float x, float y, const char *format, ...);
void FONT_drawtextx(const uint8_t *font, float x, float y, const uint8_t *text, int batchArgument);
void FONT_drawtexta(const uint8_t *font, float x, float y, const uint8_t *text);
void FONT_getrectx(const uint8_t *font, const uint8_t *text, float *x, float *y, float *width, float *height);
void FONT_getrectx_thunk(const uint8_t *font, const uint8_t *text, float *x, float *y, float *width, float *height);
const uint8_t *FONT_create(const uint8_t *font);
void FONT_destroy(const uint8_t *font);
void FONT_restore();
void FONT_init();
void FONT_installdriver(void *driver);
const uint8_t *FONT_bsearch(int code, const uint8_t *table, int count, int entrySize);
int FONT_getkern(const uint8_t *font, const uint8_t *glyph, int previous);

// ---- LOCALE
const char *LOCALE_getstr(const uint8_t *locale, int id);

// ---- SHAPE
uint8_t *SHAPE_locatez(uint8_t *shapes, const char *name);
int SHAPE_rowbytes(const uint8_t *image);
void SHAPE_name(const uint8_t *shapes, int index, uint32_t *name);
int SHAPE_createsize(int width, int height, int format, int clutFormat, int mipLevels, int nameBytes, int infoBytes);
void SHAPE_createat(uint8_t *at, int width, int height, int format, int clutFormat, int mipLevels, int nameBytes,
                    int infoBytes);
uint8_t *SHAPE_create(int width, int height, int format, int clutFormat, int mipLevels, int allocFlags,
                      int nameBytes, int infoBytes);
const char *SHAPE_longname(const uint8_t *image);
int SHAPE_infoflags(const uint8_t *image);
uint8_t *SHAPE_namedata(const uint8_t *image);
uint8_t *SHAPE_infodata(const uint8_t *image);
int SHAPE_depth(const uint8_t *image);
int SHAPE_type(int format);
int SHAPE_cluttype(int format);
uint8_t *SHAPE_loadfile(const char *name, int flags);
uint8_t *SHAPE_loadfilez(const char *name, int flags);
int SHAPE_version(const uint8_t *shapes);

#endif // DRIVING_EAGL_REALGRAPH_H_
