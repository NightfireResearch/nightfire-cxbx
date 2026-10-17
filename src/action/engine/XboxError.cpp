#include "XboxError.h"
#include "Direct3D/d3dSeam.h"
#include "XboxSystem.h"   // Language_Get, allocateAligned0x1000

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

// ---------------------------------------------------------------------------------------------------------------
// The error log: 40 lines of up to 55 characters, scrolled up when full, drawn by ShowFatalErrorScreen in the
// debug font (set up by xboxInitTextures). Its one writer is FS_AllocateAndLoadBlocking, for a missing file.
// ---------------------------------------------------------------------------------------------------------------

#define ERROR_LOG_LINES 40
#define ERROR_LOG_WIDTH 0x38   // 55 characters and the terminator

// XBE_GLOBAL(0x002afa58, 0x4)
static int ErrorLogCount;
// XBE_GLOBAL(0x002afa5c, 0x8c0)
static char ErrorLog[ERROR_LOG_LINES][ERROR_LOG_WIDTH];

// The debug font: a 256x16 one-bit image of '!' to 'Z' in 6x6 cells, 42 to a row, and per character its glyph
// (lower case maps to upper; 0xff for none) and the glyph's texel position
// XBE_GLOBAL(0x002b0320, 0x1)
static bool FontInitialised;
// XBE_GLOBAL(0x002b0324, 0x4)
static int FontTexture;
// XBE_GLOBAL(0x002b0328, 0x100)
static uint8_t FontCharToGlyph[256];
// XBE_GLOBAL(0x002b0428, 0x400)
static int FontGlyphU[256];
// XBE_GLOBAL(0x002b0828, 0x400)
static int FontGlyphV[256];

#define FontImage ((const uint16_t *)0x0019a9c0)   // game data: nonzero for a set pixel

// Makes the debug font's texture and glyph tables, once (0x000e1e70)
// AUTOINJECT
void xboxInitTextures(void) {
    if (FontInitialised)
        return;
    FontInitialised = true;
    uint16_t *pixels = (uint16_t *)allocateAligned0x1000(0x2000);
    for (int i = 0; i < 0x1000; i++)
        pixels[i] = FontImage[i] != 0 ? 0xffff : 0x8000;   // ARGB1555: white, or opaque black
    FontTexture = RegisterTexture(256, 16, 0, 1, pixels, 1);
    d3dMarkTexturePermanent(FontTexture);
    memset(FontCharToGlyph, 0xff, sizeof(FontCharToGlyph));
    for (int c = 0x21; c < 0x7b; c++) {
        uint8_t glyph = (c > '`' && c < '{') ? (uint8_t)(c - 0x20) : (uint8_t)c;
        FontCharToGlyph[c] = glyph;
        FontGlyphU[c] = ((glyph - 0x21) % 42) * 6;
        FontGlyphV[c] = ((glyph - 0x21) / 42) * 6;
    }
}

// maybeImmediateModePushItem takes the colour's bits in a float argument
static float ColourBits(uint32_t colour) {
    float bits;
    memcpy(&bits, &colour, sizeof(bits));
    return bits;
}

// AUTOINJECT
void logError(const char *format, ...) {
    char text[0xc00];
    va_list args;
    va_start(args, format);
    vsprintf(text, format, args);
    va_end(args);
    printf("[error] %s\n", text);

    if (ErrorLogCount > ERROR_LOG_LINES - 1) {
        memmove(ErrorLog[0], ErrorLog[1], (ERROR_LOG_LINES - 1) * ERROR_LOG_WIDTH);
        memset(ErrorLog[ERROR_LOG_LINES - 1], ' ', ERROR_LOG_WIDTH);
        ErrorLog[ERROR_LOG_LINES - 1][ERROR_LOG_WIDTH - 1] = 0;
        ErrorLogCount = ERROR_LOG_LINES - 1;
    }
    if (ErrorLogCount >= 0 && ErrorLogCount < ERROR_LOG_LINES) {
        int len = (int)strlen(text);
        if (len > ERROR_LOG_WIDTH - 1)
            len = ERROR_LOG_WIDTH - 1;
        if (len > 0) {
            memcpy(ErrorLog[ErrorLogCount], text, len);
            ErrorLog[ErrorLogCount][ERROR_LOG_WIDTH - 1] = 0;
        }
    }
    ErrorLogCount++;
}

// AUTOINJECT
void ShowFatalErrorScreen(bool doSwap) {
    if (doSwap) {
        d3dBeginFrame();
        d3dClear(0, true, true);
    }
    maybeResetRenderState(1);
    d3dSetDeferredTextureState(0, 0);
    d3dSetTextureStage0(FontTexture);
    int y = 0x1a;
    for (int line = 0; line < ERROR_LOG_LINES; line++, y += 10) {
        int x = 0x20;
        for (const uint8_t *c = (const uint8_t *)ErrorLog[line]; *c != 0; c++, x += 10) {
            uint8_t glyph = FontCharToGlyph[*c];
            if (glyph != 0xff)
                maybeImmediateModePushItem((float)x, (float)y, 8.0f, 8.0f, (float)FontGlyphU[glyph],
                                           (float)FontGlyphV[glyph], 5.0f, 5.0f, ColourBits(0xffffffffu));
        }
    }
    maybeImmediateModeFlush();
    d3dSetTextureStage0(0);
    if (doSwap)
        d3dSwap();
}

// ---------------------------------------------------------------------------------------------------------------
// The disc error screen. A disc read that fails (or a disc file that will not open) ends here: one line of a
// 512x512 picture of the message in each language, pulsing red, until the console is switched off. The picture
// is game data at 0x001b52e8.
// ---------------------------------------------------------------------------------------------------------------

#define DiscErrorImage ((const void *)0x001b52e8)
#define DISC_ERROR_IMAGE_BYTES 0x20000

// XBE_GLOBAL(0x00300378, 0x4)
static float DiscErrorPulse;

// AUTOINJECT
void FS_FatalErrorHandler(void) {
    printf("[error] a disc file failed to open or read: showing the disc error screen\n");
    fflush(stdout);
    int language = Language_Get();
    if (language == 8)          // American: the English line
        language = 1;
    void *image = allocateAligned0x1000(DISC_ERROR_IMAGE_BYTES);
    memcpy(image, DiscErrorImage, DISC_ERROR_IMAGE_BYTES);
    int texture = RegisterTexture(0x200, 0x200, 3, 1, image, 0);
    d3dMarkTexturePermanent(texture);
    float row = (float)(language << 6);
    for (;;) {
        DiscErrorPulse = DiscErrorPulse + 0.042f;
        int level = (int)(fabs(sin((double)DiscErrorPulse) * 88.0) + 40.0f);
        if (level < 0)
            level = 0;
        else if (level > 0x80)
            level = 0x80;
        d3dBeginFrame();
        d3dClear(0, true, true);
        maybeResetRenderState(1);
        d3dSetTextureStage0(texture);
        uint32_t colour = (((uint32_t)level | 0xffff8000u) << 8) | (uint32_t)level;
        maybeImmediateModePushItem(64.0f, 188.0f, 512.0f, 64.0f, 0.0f, row, 512.0f, 64.0f, ColourBits(colour));
        maybeImmediateModeFlush();
        d3dSwap();
    }
}

void XboxError_Halt(const char *what, const char *detail) {
    printf("[error] fatal: %s: %s\n", what, detail ? detail : "");
    fflush(stdout);
    char text[512];
    snprintf(text, sizeof(text), "The game stopped: %s\n\n%s", what, detail ? detail : "");
    MessageBoxA(NULL, text, "Nightfire", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}
