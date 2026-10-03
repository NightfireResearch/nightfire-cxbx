#ifndef DRIVING_EAGL_REALGRAPH_H_
#define DRIVING_EAGL_REALGRAPH_H_

// EA's realgraph library as the driving engine has it: FONT (text drawn through a driver EAGL installs), SHAPE
// (the SHPX image container) and LOCALE (LOCH string tables). See Realgraph.cpp and docs/driving/eagl.md 4.13.

#include <stdint.h>

typedef void (*FontDrawFn)(const uint8_t *font, const uint8_t *glyph, float x, float y);
typedef void (*FontHookFn)(const uint8_t *font);

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
