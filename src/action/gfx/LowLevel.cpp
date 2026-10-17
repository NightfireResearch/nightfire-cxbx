#include "LowLevel.h"
#include "../game.h"
#include "../math/math.h"
#include "../engine/Direct3D/d3dSeam.h"

#include <string.h>

// Both AUTOGEN-declared elsewhere (memory.cpp, game.cpp): plain declarations here, so no second stub body
void* allocateAligned0x1000(int numBytes);
int Language_Get(void);

// The widescreen switches (read by the display-settings menu, C_CHCHWS_Handler) and the copy the cameras use
#define switch_CONFIG_WIDESCREEN        U32_AT(0x001df9e8)
#define IsWidescreen                    U32_AT(0x001f6610)
// Set from Graphics_IsSomeRegionBasedThing and never read
// XBE_GLOBAL(0x001df9ec, 0x4)
static uint switch_CONFIG_SOMEGRAPHICSTHING;

// An identity matrix that maybe_psiDrawShadow and psiDrawSkinObjectMatrix (both still the game's) point at
#define LowLevelIdentityMatrix (*(_MATRIX *)0x002ade8c)

// The loading screen's state. Only the functions in this file touch it.
// XBE_GLOBAL(0x002adf50, 0x1)
static uchar LoadingScreenVisible;
// XBE_GLOBAL(0x002adf54, 0x4)
static int LoadingScreenNumFramesDrawn;
// XBE_GLOBAL(0x002adf58, 0x4)
static int LoadingDotTexture;           // the dot's texture slot, permanent

// The dot: a 15-pixel white disc in a 16x16 ARGB texture, transparent around it. The original builds it a dword at
// a time on the stack (which is why it never appears in the XBE as one block).
#define LOADING_DOT_SIZE 16
static const char *const LoadingDotArt[LOADING_DOT_SIZE] = {
    "     #####      ",
    "   #########    ",
    "  ###########   ",
    " #############  ",
    " #############  ",
    "############### ",
    "############### ",
    "############### ",
    "############### ",
    "############### ",
    " #############  ",
    " #############  ",
    "  ###########   ",
    "   #########    ",
    "     #####      ",
    "                ",
};

// The original also copies "FPS MADE BY EUROCOM" into a stack buffer here and never reads it - a leftover
// AUTOINJECT
void Graphics_Init_LowLevel(void) {
    memset(&LowLevelIdentityMatrix, 0, 0x3c);
    Mat_Identity(&LowLevelIdentityMatrix);

    if (Graphics_IsSomeGraphicsRegion())
        ConfigureGammaRamp(1.01f, 1.11f, -1.0f);
    else
        ConfigureGammaRamp(1.0f, 1.0f, 1.0f);

    switch_CONFIG_WIDESCREEN = Graphics_IsWidescreen();
    switch_CONFIG_SOMEGRAPHICSTHING = Graphics_IsSomeRegionBasedThing();
    IsWidescreen = (switch_CONFIG_WIDESCREEN != 0);
    Language_Get();     // result unused

    uint32_t *pixels = (uint32_t *)allocateAligned0x1000(LOADING_DOT_SIZE * LOADING_DOT_SIZE * 4);
    if (pixels == NULL)
        return;
    for (int y = 0; y < LOADING_DOT_SIZE; y++)
        for (int x = 0; x < LOADING_DOT_SIZE; x++)
            pixels[y * LOADING_DOT_SIZE + x] = LoadingDotArt[y][x] == '#' ? 0xffffffffu : 0;
    LoadingDotTexture = RegisterTexture(LOADING_DOT_SIZE, LOADING_DOT_SIZE, 2, 1, pixels, 1);
    d3dMarkTexturePermanent(LoadingDotTexture);
}

// Six dots in a row; every 12 frames one more lights up (gold over dim brown), up to all six, then back down
#define LOADING_DOT_COUNT   6
#define LOADING_DOT_X       140.0f
#define LOADING_DOT_SPACING 22.0f
#define LOADING_DOT_Y       329.0f
#define LOADING_DOT_LIT     0xff7b5416u
#define LOADING_DOT_UNLIT   0xff1f1505u

static float ColourBits(uint32_t colour) {
    float f;
    memcpy(&f, &colour, sizeof(f));
    return f;
}

// AUTOINJECT
void ShowLoadProgressScreen(void) {
    if (!LoadingScreenVisible)
        return;

    d3dBeginFrame();
    maybeResetRenderState(1);
    d3dSetDeferredTextureState(0, 0);
    d3dSetTextureStage0(LoadingDotTexture);

    int lit = (LoadingScreenNumFramesDrawn / 12) % 12;
    if (lit > 6)
        lit = 12 - lit;
    for (int i = 0; i < LOADING_DOT_COUNT; i++) {
        float x = LOADING_DOT_X + i * LOADING_DOT_SPACING;
        maybeImmediateModePushItem(x, LOADING_DOT_Y, LOADING_DOT_SIZE, LOADING_DOT_SIZE, 0.0f, 0.0f,
                                   LOADING_DOT_SIZE, LOADING_DOT_SIZE, ColourBits(i < lit ? LOADING_DOT_LIT : LOADING_DOT_UNLIT));
    }
    maybeImmediateModeFlush();

    d3dSetTextureStage0(0);
    d3dSwap();
    LoadingScreenNumFramesDrawn++;
}

// AUTOINJECT
void LoadingScreenMakeVisible(void) {
    LoadingScreenVisible = 1;
    LoadingScreenNumFramesDrawn = 0;
}

// AUTOINJECT
void LoadingScreenMakeInvisible(void) {
    LoadingScreenVisible = 0;
}
