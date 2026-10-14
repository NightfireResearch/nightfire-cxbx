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
    uint32_t infoHeader;             // +0xb40 'i', the 'p' attachment next
    uint32_t infoB44;                // +0xb44
    uint8_t infoB48[8];              // +0xb48
    char infoText[0x40];             // +0xb50 an "EAGL" block, room for the TAR's shared data
    uint32_t nameHeader;             // +0xb90 'p', the last
    char name[12];                   // +0xb94
};
static_assert(sizeof(BuiltInShapeFile) == 0xba0, "EAGL's built-in SHPX is 0xba0 bytes");
static_assert(offsetof(BuiltInShapeFile, level0) == 0x80 && offsetof(BuiltInShapeFile, infoHeader) == 0xb40 &&
              offsetof(BuiltInShapeFile, nameHeader) == 0xb90, "BuiltInShapeFile layout");

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
