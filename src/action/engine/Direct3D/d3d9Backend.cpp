#include "d3d9Backend.h"
#include "../../../common/renderWindow.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See d3d9Backend.h for the overview.
//
// Checkpoint 1: device creation on CXBX's render window, clear, present, viewport and gamma.
// Checkpoint 2 (this file now): Xbox texture headers built here instead of by XGRAPHC, host textures created
//   lazily at bind time with unswizzling and format conversion (YUY2 movie frames included), the 11 NV2A
//   render-state methods plus cull/fog/border colour, texture-stage state read from D3D8's own deferred state
//   arrays at draw time, and the immediate-mode quad path (menus, HUD, FMV, screen blur).
// Checkpoint 3: the game's NV2A vertex programs translated to HLSL at first use (see "Vertex shader
//   translation" below), Xbox vertex declarations turned into D3D9 ones, vertex/index buffers uploaded from the
//   game's memory on first bind, the indexed-strip mesh path, shard triangle lists and the point-sprite
//   overlay. Draws issued while an off-screen render target is selected are skipped for now.
// Still missing: render targets (shadow blur, aux pass), the backbuffer readback in psiBlurScreen.

#include "../XboxSettings.h"
#include "../XboxFile.h"      // XboxFile_ReportStreamingIfDue, reported alongside the frame timing

int g_gfxBackend = GFX_BACKEND_CXBX;

// Backend messages go to the console and to d3d9_backend.log in the working directory (easier to hand over).
static void D3D9Log(const char *fmt, ...) {
    static FILE *logFile = NULL;
    static bool opened = false;
    if (!opened) { opened = true; logFile = fopen("d3d9_backend.log", "w"); }
    va_list ap;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (logFile != NULL) { va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fflush(logFile); }
}

static IDirect3D9 *g_d3d = NULL;
static IDirect3DDevice9 *g_device = NULL;
static HWND g_window = NULL;
static D3DPRESENT_PARAMETERS g_presentParams;
static bool g_inScene = false;
static uint32_t g_frameCount = 0;
static int g_targetFrameRate = 0;     // Xbox Swap blocks for the vertical blank; we pace Present to the same rate
static double g_nextFrameDeadline = 0;
static LARGE_INTEGER g_qpcFrequency;

static double NowSeconds(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)g_qpcFrequency.QuadPart;
}

static uint64_t g_statDraws = 0;
static uint64_t g_statDrawsIndexed = 0;     // indexed geometry out of a vertex buffer
static uint64_t g_statDrawsDirect = 0;      // non-indexed, still from a buffer
static uint64_t g_statDrawsImmediate = 0;   // DrawPrimitiveUP - HUD and effects, the batchable kind
static uint64_t g_statVertices = 0;
static uint64_t g_statTextureUploads = 0;
static uint64_t g_statTextureLookups = 0;
static uint64_t g_statTextureScanSteps = 0;
static uint64_t g_statBackBufferCaptures = 0;   // psiBlurScreen grabbing the frame
static uint64_t g_statRenderTargetCreates = 0;  // each one is a D3DPOOL_DEFAULT allocation
static uint64_t g_statBufferCreates = 0;        // vertex and index buffers created, not reused

static int HostTextureCount(void);   // defined with the texture table further down

// Where the frame time actually goes, printed every few seconds when PerfLog is on in settings.ini.
//
// Two numbers separate the two explanations for a low frame rate that people reach for. If the pacer is
// spending most of its time waiting, the game is comfortably inside its frame budget and the rate is simply
// the rate it was asked for. If it is waiting for none of it, the frame took longer than the period and the
// game is genuinely behind - and then the interesting question is what it spent the time on, which is what
// the streaming-read counters next door are for.
static void ReportFrameTiming(double arrivedAtPacer, double leftPacer) {
    if (!Settings_GetPerfLog())
        return;

    static double windowStart = 0.0;
    static double busySeconds = 0.0;
    static double pacedSeconds = 0.0;
    static double previousLeft = 0.0;
    static int frames = 0;

    if (windowStart == 0.0) {
        windowStart = arrivedAtPacer;
        previousLeft = leftPacer;
        return;
    }

    busySeconds += arrivedAtPacer - previousLeft;   // drawing and everything else the game did
    pacedSeconds += leftPacer - arrivedAtPacer;     // deliberately waiting to hold the frame rate
    previousLeft = leftPacer;
    frames++;

    double elapsed = leftPacer - windowStart;
    if (elapsed < 5.0)
        return;

    double fps = frames / elapsed;
    double busyMs = (busySeconds / frames) * 1000.0;
    double pacedMs = (pacedSeconds / frames) * 1000.0;
    printf("[perf] %.1f fps (asked for %d), %.1f ms working + %.1f ms waiting per frame\n",
           fps, g_targetFrameRate, busyMs, pacedMs);
    // Cost per draw is the number worth comparing between machines: it is nearly constant for a given
    // graphics stack, so a scene being slow because it draws more is easy to tell from a stack that is slow
    // per call. Native D3D9 is around 2 microseconds; WineD3D translating to OpenGL has been measured at
    // about 150, which makes a 2000-draw scene hopeless and an 85-draw one fine.
    double usPerDraw = (g_statDraws > 0) ? (busySeconds * 1e6) / (double)g_statDraws : 0.0;
    printf("[perf]   %.1f us per draw call\n", usPerDraw);
    // The mix says whether fewer draw calls is a realistic answer. Immediate-mode draws are the HUD and
    // effects and are the batchable kind; a scene made mostly of small indexed draws is the game's own
    // geometry submission and much harder to merge. Vertices per draw is the giveaway: a few hundred tiny
    // draws is a batching problem, a few hundred large ones is not.
    printf("[perf]   draw mix: %llu indexed, %llu direct, %llu immediate, %llu vertices each on average\n",
           (unsigned long long)(g_statDrawsIndexed / frames),
           (unsigned long long)(g_statDrawsDirect / frames),
           (unsigned long long)(g_statDrawsImmediate / frames),
           (unsigned long long)(g_statDraws > 0 ? g_statVertices / g_statDraws : 0));
    printf("[perf]   per frame: %llu draws, %llu texture uploads, %llu texture lookups costing %llu"
           " comparisons (%d registered)\n",
           (unsigned long long)(g_statDraws / frames),
           (unsigned long long)(g_statTextureUploads / frames),
           (unsigned long long)(g_statTextureLookups / frames),
           (unsigned long long)(g_statTextureScanSteps / frames),
           HostTextureCount());
    // Allocations are separated out because they are the expensive kind of work: creating a
    // D3DPOOL_DEFAULT render target or a vertex buffer costs far more than issuing a draw, and doing either
    // every frame is the usual reason a scene is slow in a way that scales with nothing obvious.
    printf("[perf]   per frame: %llu backbuffer captures, %llu render targets created,"
           " %llu buffers created\n",
           (unsigned long long)(g_statBackBufferCaptures / frames),
           (unsigned long long)(g_statRenderTargetCreates / frames),
           (unsigned long long)(g_statBufferCreates / frames));
    if (pacedMs < 0.5) {
        printf("[perf]   never idle, so the frame rate is what the machine can manage, not the pacing.\n");
    }
    g_statDraws = g_statTextureUploads = g_statTextureLookups = g_statTextureScanSteps = 0;
    g_statBackBufferCaptures = g_statRenderTargetCreates = g_statBufferCreates = 0;
    g_statDrawsIndexed = g_statDrawsDirect = g_statDrawsImmediate = g_statVertices = 0;
    fflush(stdout);

    windowStart = leftPacer;
    busySeconds = pacedSeconds = 0.0;
    frames = 0;

    XboxFile_ReportStreamingIfDue();
}

// On the Xbox, D3DDevice_Swap waits for the next vertical blank, which is what held the game to its 50/60 Hz
// frame rate (and what CXBX's HLE emulated). Windowed D3D9 Present on a fast monitor returns almost
// immediately, so hold each frame to the period ourselves: sleep in 1 ms steps, then spin the last stretch.
static void PaceFrame(void) {
    if (g_targetFrameRate <= 0)
        return;
    double period = 1.0 / (double)g_targetFrameRate;
    double now = NowSeconds();
    double arrived = now;
    if (g_nextFrameDeadline == 0 || now > g_nextFrameDeadline + 0.25) // first frame, or we fell far behind (a load)
        g_nextFrameDeadline = now;
    while (now < g_nextFrameDeadline) {
        if (g_nextFrameDeadline - now > 0.002)
            Sleep(1);
        else
            YieldProcessor();
        now = NowSeconds();
    }
    g_nextFrameDeadline += period;
    ReportFrameTiming(arrived, now);
}

// ---------------------------------------------------------------------------------------------------------------
// Missing entry-point accounting
// ---------------------------------------------------------------------------------------------------------------

struct MissingEntry { const char *name; uint32_t count; };
static MissingEntry g_missing[128];
static int g_missingCount = 0;

void D3D9_BackendMissing(const char *entryPoint) {
    for (int i = 0; i < g_missingCount; i++) {
        if (g_missing[i].name == entryPoint || strcmp(g_missing[i].name, entryPoint) == 0) {
            g_missing[i].count++;
            return;
        }
    }
    if (g_missingCount < (int)(sizeof(g_missing) / sizeof(g_missing[0]))) {
        g_missing[g_missingCount].name = entryPoint;
        g_missing[g_missingCount].count = 1;
        g_missingCount++;
        D3D9Log("[d3d9] not implemented yet: %s\n", entryPoint);
    }
}

static void PrintMissingSummary(void) {
    if (g_missingCount == 0)
        return;
    D3D9Log("[d3d9] frame %u - entry points still unimplemented (calls so far):", g_frameCount);
    for (int i = 0; i < g_missingCount; i++)
        D3D9Log(" %s=%u", g_missing[i].name, g_missing[i].count);
    D3D9Log("\n");
}

// ---------------------------------------------------------------------------------------------------------------
// Xbox D3D8 structures and state arrays we read directly. The seam still writes D3D8's own deferred state
// arrays (via its D3D8_* macros), so the backend reads them back at draw time rather than needing a second
// copy of the state: D3D__TextureState[4][32] at 0x001117D0 (index = X_D3DTSS_*) and D3D__RenderState at
// 0x001119D0 (index = X_D3DRS_*).
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_TEXTURE_STATE(stage, index) (*(const uint32_t*)(0x001117D0u + (stage) * 0x80u + (index) * 4u))
#define XBOX_RENDER_STATE(index)         (*(const uint32_t*)(0x001119D0u + (index) * 4u))
enum { XTSS_TEXCOORDINDEX = 28, XTSS_ADDRESSU = 0, XTSS_ADDRESSV = 1, XTSS_MAGFILTER = 3, XTSS_MINFILTER = 4, XTSS_MIPFILTER = 5, XTSS_MIPMAPLODBIAS = 6,
       XTSS_COLOROP = 12, XTSS_COLORARG0 = 13, XTSS_COLORARG1 = 14, XTSS_COLORARG2 = 15,
       XTSS_ALPHAOP = 16, XTSS_ALPHAARG0 = 17, XTSS_ALPHAARG1 = 18, XTSS_ALPHAARG2 = 19, XTSS_BORDERCOLOR = 29 };
enum { XRS_FOGENABLE = 92, XRS_FOGTABLEMODE = 93, XRS_FOGSTART = 94, XRS_FOGEND = 95, XRS_FOGDENSITY = 96 };

// Xbox pixel container header (the first 20 bytes of Gfx's 36-byte texture slots and of surface objects).
struct XboxPixelContainer { uint32_t Common, Data, Lock, Format, Size; };
struct XboxSurface { uint32_t Common, Data, Lock, Format, Size, Parent; };
#define XBOX_SURFACE_COMMON 0x01050001u // D3DCOMMON_TYPE_SURFACE, refcount 1
#define XBOX_TEXTURE_COMMON 0x00040001u // D3DCOMMON_TYPE_TEXTURE, refcount 1
// Stand-ins for the backbuffer / render target / depth surface objects the seam asks for (see the surface code
// at the end of the file). There is no CPU-visible backbuffer here, so their Data words are 0.
// The dummy backbuffer's Data word is a sentinel physical address: psiBlurScreen registers a texture over
// "the backbuffer's memory" (Data | 0x80000000), which the backend recognises and answers with a GPU copy.
#define BACKBUFFER_DATA_SENTINEL 0x0BB00000u
static XboxSurface g_dummyBackBuffer   = { XBOX_SURFACE_COMMON | 0x7FFE, BACKBUFFER_DATA_SENTINEL, 0, 0, 0, 0 };
static XboxSurface g_dummyRenderTarget = { XBOX_SURFACE_COMMON | 0x7FFE, 0, 0, 0, 0, 0 };
static XboxSurface g_dummyDepthStencil = { XBOX_SURFACE_COMMON | 0x7FFE, 0, 0, 0, 0, 0 };

// Xbox X_D3DFMT_* values this game produces (see RegisterTexture's format switch) plus a few neighbours.
enum {
    XFMT_L8 = 0x00, XFMT_AL8 = 0x01, XFMT_A1R5G5B5 = 0x02, XFMT_X1R5G5B5 = 0x03, XFMT_A4R4G4B4 = 0x04, XFMT_R5G6B5 = 0x05,
    XFMT_A8R8G8B8 = 0x06, XFMT_X8R8G8B8 = 0x07, XFMT_P8 = 0x0b, XFMT_DXT1 = 0x0c, XFMT_DXT3 = 0x0e, XFMT_DXT5 = 0x0f,
    XFMT_LIN_A1R5G5B5 = 0x10, XFMT_LIN_R5G6B5 = 0x11, XFMT_LIN_A8R8G8B8 = 0x12, XFMT_LIN_L8 = 0x13,
    XFMT_A8 = 0x19, XFMT_A8L8 = 0x1a, XFMT_LIN_AL8 = 0x1b, XFMT_LIN_X1R5G5B5 = 0x1c, XFMT_LIN_A4R4G4B4 = 0x1d,
    XFMT_LIN_X8R8G8B8 = 0x1e, XFMT_LIN_A8 = 0x1f, XFMT_LIN_A8L8 = 0x20, XFMT_YUY2 = 0x24, XFMT_UYVY = 0x25,
};

static bool XboxFormatIsLinear(uint32_t f) {
    return (f >= 0x10 && f <= 0x20) || f == XFMT_YUY2 || f == XFMT_UYVY || (f >= 0x2e && f <= 0x31) || (f >= 0x35 && f <= 0x37) || f == 0x3d;
}
static bool XboxFormatIsDxt(uint32_t f) { return f == XFMT_DXT1 || f == XFMT_DXT3 || f == XFMT_DXT5; }
static uint32_t XboxFormatBitsPerPixel(uint32_t f) {
    switch (f) {
        case XFMT_L8: case XFMT_AL8: case XFMT_P8: case XFMT_LIN_L8: case XFMT_A8: case XFMT_LIN_AL8: case XFMT_LIN_A8: return 8;
        case XFMT_DXT1: return 4;
        case XFMT_DXT3: case XFMT_DXT5: return 8;
        case XFMT_A8R8G8B8: case XFMT_X8R8G8B8: case XFMT_LIN_A8R8G8B8: case XFMT_LIN_X8R8G8B8: return 32;
        default: return 16;
    }
}
static uint32_t Log2Floor(uint32_t v) { uint32_t n = 0; while (v > 1) { v >>= 1; n++; } return n; }

// Same header XGSetTextureHeader builds: Common/Data/Lock, the Format word (dma channel 1, 2D, format,
// mip count, log2 sizes for swizzled/compressed) and, for linear formats, the Size word (width-1, height-1,
// pitch/64-1). The seam's RegisterTexture forces Data to the real pointer afterwards, so Data is just
// initialised here and D3D9_ResourceRegister sets it.
void D3D9_XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage, int format,
                             uint32_t pool, void *pTexture, uint32_t data, uint32_t pitch) {
    (void)usage; (void)pool;
    XboxPixelContainer *h = (XboxPixelContainer*)pTexture;
    uint32_t f = (uint32_t)format & 0xFF;
    if (levels == 0) levels = 1;
    h->Common = XBOX_TEXTURE_COMMON;
    h->Data = data;
    h->Lock = 0;
    if (XboxFormatIsLinear(f)) {
        if (pitch == 0)
            pitch = ((width * XboxFormatBitsPerPixel(f) / 8) + 63) & ~63u;
        h->Format = 0x00000001u | (2u << 4) | (f << 8) | (1u << 16);
        h->Size = (width - 1) | ((height - 1) << 12) | ((pitch / 64 - 1) << 24);
    } else {
        h->Format = 0x00000001u | (2u << 4) | (f << 8) | ((levels & 0xF) << 16) | (Log2Floor(width) << 20) | (Log2Floor(height) << 24);
        h->Size = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Host texture cache, keyed by the Xbox header address (Gfx's slot). Created/re-uploaded lazily at bind time.
// ---------------------------------------------------------------------------------------------------------------

struct HostTexture {
    const void *header;      // Xbox header address (the cache key)
    uint32_t data, format, size; // the header words the host texture was built from - any change means rebuild
    IDirect3DTexture9 *texture;
    bool dirty;              // CPU wrote into the pixel data since the last upload
    bool renderTarget;       // lives in D3DPOOL_DEFAULT with D3DUSAGE_RENDERTARGET; never uploaded from CPU memory
    IDirect3DSurface9 *rtSurface; // level 0 of a render-target texture
};
static HostTexture g_textures[2200];
static int g_textureCount = 0;

// Counted so that PerfLog can say how much of a frame goes on finding textures rather than drawing them.
// This is a linear scan over every texture the level has registered, run on every bind, so its cost is
// (draws x texture stages x textures in the level) - invisible on a fast native machine, and capable of
// dominating on a slower or translated one. The counters are what tell those two apart.

static int HostTextureCount(void) { return g_textureCount; }

static HostTexture *FindHostTexture(const void *header) {
    g_statTextureLookups++;
    for (int i = 0; i < g_textureCount; i++) {
        g_statTextureScanSteps++;
        if (g_textures[i].header == header)
            return &g_textures[i];
    }
    return NULL;
}

static HostTexture *FindOrAddHostTexture(const void *header) {
    HostTexture *t = FindHostTexture(header);
    if (t != NULL)
        return t;
    if (g_textureCount >= (int)(sizeof(g_textures) / sizeof(g_textures[0])))
        return NULL;
    t = &g_textures[g_textureCount++];
    memset(t, 0, sizeof(*t));
    t->header = header;
    t->dirty = true;
    return t;
}

static void MarkTextureDirty(const void *header) {
    HostTexture *t = FindHostTexture(header);
    if (t != NULL && !t->renderTarget)
        t->dirty = true;
}

static void ReleaseHostTexture(HostTexture *t) {
    if (t->rtSurface != NULL) { t->rtSurface->Release(); t->rtSurface = NULL; }
    if (t->texture != NULL) { t->texture->Release(); t->texture = NULL; }
    t->renderTarget = false;
    t->dirty = true;
}

static void InvalidateHostBuffers(const void *obj);
static void CaptureBackBufferInto(void *header);
void D3D9_ResourceRegister(void *pResource, uint32_t data) {
    XboxPixelContainer *h = (XboxPixelContainer*)pResource;
    h->Data = data;
    HostTexture *t = FindHostTexture(pResource);
    if (t != NULL && t->renderTarget)
        ReleaseHostTexture(t); // the slot is being reused for something else (or re-captured)
    MarkTextureDirty(pResource);   // a slot being (re)registered with new data
    InvalidateHostBuffers(pResource);
    if ((data | 0x80000000u) == (BACKBUFFER_DATA_SENTINEL | 0x80000000u))
        CaptureBackBufferInto(pResource); // psiBlurScreen grabbing the backbuffer, see the sentinel's comment
}

void D3D9_NotifyTextureModified(void *pTextureOrSurface) {
    const uint32_t *obj = (const uint32_t*)pTextureOrSurface;
    if (obj == NULL)
        return;
    if ((obj[0] & 0x70000u) == 0x50000u) { // a surface object - modify its parent texture
        const XboxSurface *s = (const XboxSurface*)obj;
        if (s->Parent != 0)
            MarkTextureDirty((const void*)(uintptr_t)s->Parent);
        return;
    }
    MarkTextureDirty(pTextureOrSurface);
}

// The CPU-visible address of a resource's Data word. The seam registers every texture, vertex and index buffer
// with a real host pointer (RegisterTexture forces the texture Data word back to the unmasked pointer), so the
// word is used as-is here; only surface objects carry the Xbox-style stripped address, and those are read via
// the seam's own d3dLockSurface, not here.
static const uint8_t *XboxDataPointer(uint32_t dataWord) {
    return (const uint8_t*)(uintptr_t)dataWord;
}

// Morton-order unswizzle of one level, bytesPerPixel wide texels, into a tightly packed linear buffer.
static void Unswizzle(uint8_t *dst, const uint8_t *src, uint32_t width, uint32_t height, uint32_t bytesPerPixel) {
    // Bit masks for x and y (the same construction as d3dSeam.cpp's d3dComputeSwizzleMasks).
    uint32_t maskX = 0, maskY = 0, bit = 1, size = 1;
    for (;;) {
        bool any = false;
        if (size < width)  { maskX |= bit; bit <<= 1; any = true; }
        if (size < height) { maskY |= bit; bit <<= 1; any = true; }
        size <<= 1;
        if (!any) break;
    }
    uint32_t yOff = 0;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t xOff = 0;
        uint8_t *row = dst + (size_t)y * width * bytesPerPixel;
        for (uint32_t x = 0; x < width; x++) {
            memcpy(row + (size_t)x * bytesPerPixel, src + (size_t)(yOff | xOff) * bytesPerPixel, bytesPerPixel);
            xOff = (xOff - maskX) & maskX;
        }
        yOff = (yOff - maskY) & maskY;
    }
}

static inline uint8_t ClampByte(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

// YUY2 (Y0 U Y1 V, BT.601) to A8R8G8B8, one row.
static void ConvertYuy2Row(uint32_t *dst, const uint8_t *src, uint32_t width) {
    for (uint32_t x = 0; x < width; x += 2) {
        int y0 = src[0], u = src[1] - 128, y1 = src[2], v = src[3] - 128;
        src += 4;
        for (int k = 0; k < 2; k++) {
            int c = ((k == 0 ? y0 : y1) - 16) * 298;
            int r = (c + 409 * v + 128) >> 8;
            int g = (c - 100 * u - 208 * v + 128) >> 8;
            int b = (c + 516 * u + 128) >> 8;
            *dst++ = 0xFF000000u | ((uint32_t)ClampByte(r) << 16) | ((uint32_t)ClampByte(g) << 8) | ClampByte(b);
        }
    }
}

// Host format for an Xbox format, plus whether pixels are copied as-is after (un)swizzling.
static D3DFORMAT HostFormatFor(uint32_t xboxFormat, bool *convertYuy2) {
    *convertYuy2 = false;
    switch (xboxFormat) {
        case XFMT_A8R8G8B8: case XFMT_LIN_A8R8G8B8: return D3DFMT_A8R8G8B8;
        case XFMT_X8R8G8B8: case XFMT_LIN_X8R8G8B8: return D3DFMT_X8R8G8B8;
        case XFMT_A4R4G4B4: case XFMT_LIN_A4R4G4B4: return D3DFMT_A4R4G4B4;
        case XFMT_A1R5G5B5: case XFMT_LIN_A1R5G5B5: return D3DFMT_A1R5G5B5;
        case XFMT_X1R5G5B5: case XFMT_LIN_X1R5G5B5: return D3DFMT_X1R5G5B5;
        case XFMT_R5G6B5: case XFMT_LIN_R5G6B5: return D3DFMT_R5G6B5;
        case XFMT_L8: case XFMT_LIN_L8: return D3DFMT_L8;
        case XFMT_A8: case XFMT_LIN_A8: return D3DFMT_A8;
        case XFMT_A8L8: case XFMT_LIN_A8L8: return D3DFMT_A8L8;
        case XFMT_DXT1: return D3DFMT_DXT1;
        case XFMT_DXT3: return D3DFMT_DXT3;
        case XFMT_DXT5: return D3DFMT_DXT5;
        case XFMT_YUY2: *convertYuy2 = true; return D3DFMT_A8R8G8B8;
        default: return D3DFMT_UNKNOWN;
    }
}

// (Re)creates and/or uploads the host texture for an Xbox header. Returns NULL for formats not handled yet.
static IDirect3DTexture9 *GetHostTexture(const void *headerPtr) {
    const XboxPixelContainer *h = (const XboxPixelContainer*)headerPtr;
    HostTexture *t = FindOrAddHostTexture(headerPtr);
    if (t == NULL || g_device == NULL)
        return NULL;
    if (t->renderTarget)
        return t->texture; // drawn by the GPU, nothing to upload

    bool rebuild = (t->texture == NULL) || t->format != h->Format || t->size != h->Size || t->data != h->Data;
    if (!rebuild && !t->dirty)
        return t->texture;

    uint32_t xboxFormat = (h->Format >> 8) & 0xFF;
    bool linear = (h->Size != 0);
    bool dxt = XboxFormatIsDxt(xboxFormat);
    uint32_t levels = linear ? 1 : ((h->Format >> 16) & 0xF);
    if (levels == 0) levels = 1;
    uint32_t width, height, pitch;
    if (linear) {
        width = (h->Size & 0xFFF) + 1;
        height = ((h->Size >> 12) & 0xFFF) + 1;
        pitch = ((h->Size >> 24) + 1) * 64;
    } else {
        width = 1u << ((h->Format >> 20) & 0xF);
        height = 1u << ((h->Format >> 24) & 0xF);
        pitch = 0;
    }
    bool convertYuy2;
    D3DFORMAT hostFormat = HostFormatFor(xboxFormat, &convertYuy2);
    if (hostFormat == D3DFMT_UNKNOWN) {
        static uint32_t warned[64]; static int warnedCount = 0;
        bool seen = false;
        for (int i = 0; i < warnedCount; i++) if (warned[i] == xboxFormat) seen = true;
        if (!seen && warnedCount < 64) { warned[warnedCount++] = xboxFormat; D3D9Log("[d3d9] texture format 0x%02x not handled yet (%ux%u)\n", xboxFormat, width, height); }
        return NULL;
    }

    if (rebuild) {
        if (t->texture != NULL) { t->texture->Release(); t->texture = NULL; }
        HRESULT hr = g_device->CreateTexture(width, height, levels, 0, hostFormat, D3DPOOL_MANAGED, &t->texture, NULL);
        if (FAILED(hr)) {
            D3D9Log("[d3d9] CreateTexture %ux%u levels %u format %u failed: 0x%08lx\n", width, height, levels, (unsigned)hostFormat, hr);
            t->texture = NULL;
            return NULL;
        }
        t->format = h->Format; t->size = h->Size; t->data = h->Data;
    }

    g_statTextureUploads++;

    // Upload every level. Xbox mip levels are stored back to back (swizzled or DXT); linear textures have one.
    const uint8_t *src = XboxDataPointer(h->Data);
    uint32_t bpp = XboxFormatBitsPerPixel(xboxFormat);
    static uint8_t *scratch = NULL; static size_t scratchSize = 0;
    for (uint32_t level = 0; level < levels; level++) {
        uint32_t lw = width >> level, lh = height >> level;
        if (lw == 0) lw = 1;
        if (lh == 0) lh = 1;
        D3DLOCKED_RECT lr;
        if (FAILED(t->texture->LockRect(level, &lr, NULL, 0)))
            break;
        if (dxt) {
            uint32_t bw = (lw < 4 ? 4 : lw) / 4, bh = (lh < 4 ? 4 : lh) / 4;
            uint32_t blockBytes = (xboxFormat == XFMT_DXT1) ? 8 : 16;
            uint32_t rowBytes = bw * blockBytes;
            for (uint32_t r = 0; r < bh; r++)
                memcpy((uint8_t*)lr.pBits + (size_t)r * lr.Pitch, src + (size_t)r * rowBytes, rowBytes);
            src += (size_t)rowBytes * bh;
        } else if (convertYuy2) {
            for (uint32_t r = 0; r < lh; r++)
                ConvertYuy2Row((uint32_t*)((uint8_t*)lr.pBits + (size_t)r * lr.Pitch), src + (size_t)r * pitch, lw);
        } else if (linear) {
            uint32_t rowBytes = lw * bpp / 8;
            for (uint32_t r = 0; r < lh; r++)
                memcpy((uint8_t*)lr.pBits + (size_t)r * lr.Pitch, src + (size_t)r * pitch, rowBytes);
        } else {
            uint32_t bytesPerPixel = bpp / 8;
            size_t levelBytes = (size_t)lw * lh * bytesPerPixel;
            if (scratchSize < levelBytes) { free(scratch); scratch = (uint8_t*)malloc(levelBytes); scratchSize = levelBytes; }
            Unswizzle(scratch, src, lw, lh, bytesPerPixel);
            uint32_t rowBytes = lw * bytesPerPixel;
            for (uint32_t r = 0; r < lh; r++)
                memcpy((uint8_t*)lr.pBits + (size_t)r * lr.Pitch, scratch + (size_t)r * rowBytes, rowBytes);
            src += levelBytes;
        }
        t->texture->UnlockRect(level);
    }
    t->dirty = false;
    return t->texture;
}

// ---------------------------------------------------------------------------------------------------------------
// Window and device
// ---------------------------------------------------------------------------------------------------------------

// The launcher passes its own top-level window to CXBX as "/hwnd <decimal>" on the command line, and CXBX
// creates its "CxbxRender" child inside it. Prefer the child (it's the area CXBX itself would draw to and it
// tracks the launcher's resizing); fall back to the parent, then to a standalone CxbxRender window.
//
// Under the standalone loader there is no launcher and no CXBX, so the loader creates the window itself and
// pumps its messages - see CreateRenderWindow in src/loader/loadermain.cpp. It is looked for last, so that
// nothing changes for a CXBX-hosted run.
static HWND FindRenderWindow(void) {
    HWND parent = NULL;
    const char *cmd = GetCommandLineA();
    const char *p = (cmd != NULL) ? strstr(cmd, "/hwnd") : NULL;
    if (p != NULL)
        parent = (HWND)(uintptr_t)strtoul(p + 5, NULL, 10);

    HWND child = (parent != NULL) ? FindWindowExA(parent, NULL, "CxbxRender", NULL) : NULL;
    if (child != NULL)
        return child;
    if (parent != NULL)
        return parent;

    HWND cxbx = FindWindowA("CxbxRender", NULL);
    if (cxbx != NULL)
        return cxbx;
    return FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
}

static void InitPinnedConstants(void);
static void ReleaseIndexRing(void);
static IDirect3DSurface9 *g_backBufferSurface = NULL, *g_mainDepthSurface = NULL; // the device's own, held across the frame
static uint32_t g_targetWidth = 640, g_targetHeight = 480; // size of the current render target (backbuffer or texture)
static void ReleaseDefaultPoolResources(void);
static bool g_reversedDepth = false;   // 32-bit float depth buffer with reversed Z (see D3D9_CreateDevice)
static bool g_hasStencil = true;
static void BeginSceneIfNeeded(void) {
    if (g_device != NULL && !g_inScene) {
        g_device->BeginScene();
        g_inScene = true;
    }
}

uint32_t D3D9_CreateDevice(uint32_t adapter, uint32_t deviceType, void *hFocusWindow, uint32_t behaviorFlags,
                           void *pPresentationParameters, void **ppDevice) {
    (void)adapter; (void)deviceType; (void)hFocusWindow; (void)behaviorFlags;
    const uint32_t *xboxParams = (const uint32_t*)pPresentationParameters; // Xbox D3DPRESENT_PARAMETERS: [0] width, [1] height, ..., [11] refresh rate
    uint32_t width = xboxParams[0], height = xboxParams[1];

    g_window = FindRenderWindow();
    if (g_window == NULL) {
        D3D9Log("[d3d9] no render window found (no /hwnd on the command line and no CxbxRender window).\n");
        return 0x8876086Cu; // D3DERR_INVALIDCALL
    }

    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (g_d3d == NULL) {
        D3D9Log("[d3d9] Direct3DCreate9 failed.\n");
        return 0x8876086Cu;
    }

    D3DADAPTER_IDENTIFIER9 ident;
    if (SUCCEEDED(g_d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident)))
        D3D9Log("[d3d9] adapter: %s\n", ident.Description);

    memset(&g_presentParams, 0, sizeof(g_presentParams));
    g_presentParams.Windowed = TRUE;
    g_presentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_presentParams.hDeviceWindow = g_window;
    g_presentParams.BackBufferWidth = width;
    g_presentParams.BackBufferHeight = height;
    g_presentParams.BackBufferFormat = D3DFMT_X8R8G8B8;
    g_presentParams.BackBufferCount = 1;
    g_presentParams.EnableAutoDepthStencil = TRUE;
    // Depth format. The game W-buffers with a near:far ratio around 1:300000, which a 24-bit fixed z-buffer
    // would z-fight badly, so prefer a 32-bit float depth buffer and store reversed Z (1 at near, 0 at far),
    // which keeps precision at all distances. No stencil: the game never sets a stencil state.
    g_presentParams.AutoDepthStencilFormat = D3DFMT_D24S8;
    g_reversedDepth = false;
    g_hasStencil = true;
    if (SUCCEEDED(g_d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D32F_LOCKABLE)) &&
        SUCCEEDED(g_d3d->CheckDepthStencilMatch(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_D32F_LOCKABLE))) {
        g_presentParams.AutoDepthStencilFormat = D3DFMT_D32F_LOCKABLE;
        g_reversedDepth = true;
        g_hasStencil = false;
    }
    g_presentParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; // paced by PaceFrame, not the host monitor

    DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
    HRESULT hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window, flags, &g_presentParams, &g_device);
    if (FAILED(hr)) {
        flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
        hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window, flags, &g_presentParams, &g_device);
    }
    if (FAILED(hr)) {
        D3D9Log("[d3d9] CreateDevice failed: 0x%08lx\n", hr);
        return (uint32_t)hr;
    }

    QueryPerformanceFrequency(&g_qpcFrequency);
    timeBeginPeriod(1);
    int fpsOverride = Settings_GetFPSOverride(); // the same override mainloop applies to the game's own tick rate
    g_targetFrameRate = (fpsOverride > 0) ? fpsOverride : (int)xboxParams[11];
    D3D9Log("[d3d9] device created on window %p (%ux%u backbuffer, %s depth, paced to %d Hz).\n",
           (void*)g_window, width, height, g_reversedDepth ? "32-bit float reversed" : "24-bit fixed", g_targetFrameRate);
    InitPinnedConstants();
    g_device->GetRenderTarget(0, &g_backBufferSurface);
    g_device->GetDepthStencilSurface(&g_mainDepthSurface);
    g_targetWidth = width; g_targetHeight = height;
    D3DCAPS9 caps;
    if (SUCCEEDED(g_device->GetDeviceCaps(&caps))) {
        // D3D9 clamps point sprites to D3DRS_POINTSIZE_MAX, which defaults to 64 pixels - far smaller than the
        // game's overlay blobs. Raise it to whatever the hardware allows.
        g_device->SetRenderState(D3DRS_POINTSIZE_MAX, *(DWORD*)&caps.MaxPointSize);
        D3D9Log("[d3d9] max point size %.0f\n", caps.MaxPointSize);
    }
    BeginSceneIfNeeded();
    *ppDevice = g_device;
    return 0;
}

uint32_t D3D9_ArePushBuffersSupported(uint32_t unused) {
    (void)unused;
    return 1; // what the D3D8 library answers too - the game keys "device exists" off it
}

void D3D9_SetPushBufferSize(uint32_t pushBufferSize, uint32_t kickOffSize) {
    (void)pushBufferSize; (void)kickOffSize; // no push buffer here
}

// ---------------------------------------------------------------------------------------------------------------
// Debug dumps: every D3D9_DUMP_EVERY frames the backbuffer and any render-target texture that gets sampled are
// written as 24-bit BMP files (colour, and the alpha channel as grey) in the working directory, so the bring-up
// can be looked at without a capture tool. 0 disables.
// ---------------------------------------------------------------------------------------------------------------
#define D3D9_DUMP_EVERY 0 // set to e.g. 300 to dump every 300 frames
static uint32_t g_dumpFrame = 0;   // the frame currently being dumped (0 = none)
static int g_dumpRtCount = 0;

static void WriteBmp24(const char *path, uint32_t width, uint32_t height, const uint8_t *bgr, size_t rowBytes) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    uint32_t stride = (width * 3 + 3) & ~3u;
    uint32_t imageBytes = stride * height;
    uint8_t header[54] = { 'B', 'M' };
    uint32_t fileSize = 54 + imageBytes, dataOffset = 54, infoSize = 40, planesBpp = 1 | (24u << 16);
    int32_t w = (int32_t)width, h = -(int32_t)height; // negative height: top-down rows
    memcpy(header + 2, &fileSize, 4); memcpy(header + 10, &dataOffset, 4); memcpy(header + 14, &infoSize, 4);
    memcpy(header + 18, &w, 4); memcpy(header + 22, &h, 4); memcpy(header + 26, &planesBpp, 4); memcpy(header + 34, &imageBytes, 4);
    fwrite(header, 1, 54, f);
    static uint8_t pad[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < height; y++) {
        fwrite(bgr + (size_t)y * rowBytes, 1, width * 3, f);
        fwrite(pad, 1, stride - width * 3, f);
    }
    fclose(f);
}

static void DumpSurface(IDirect3DSurface9 *surface, const char *name) {
    D3DSURFACE_DESC desc;
    if (surface == NULL || FAILED(surface->GetDesc(&desc)))
        return;
    IDirect3DSurface9 *sys = NULL;
    if (FAILED(g_device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, NULL)))
        return;
    if (SUCCEEDED(g_device->GetRenderTargetData(surface, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
            size_t rowBytes = (size_t)desc.Width * 3;
            uint8_t *colour = (uint8_t*)malloc(rowBytes * desc.Height), *alpha = (uint8_t*)malloc(rowBytes * desc.Height);
            if (colour != NULL && alpha != NULL) {
                for (UINT y = 0; y < desc.Height; y++) {
                    const uint8_t *row = (const uint8_t*)lr.pBits + y * lr.Pitch;
                    for (UINT x = 0; x < desc.Width; x++) {
                        memcpy(colour + y * rowBytes + x * 3, row + x * 4, 3); // B, G, R as stored
                        memset(alpha + y * rowBytes + x * 3, row[x * 4 + 3], 3);
                    }
                }
                char path[128];
                snprintf(path, sizeof(path), "%s.bmp", name);
                WriteBmp24(path, desc.Width, desc.Height, colour, rowBytes);
                snprintf(path, sizeof(path), "%s_alpha.bmp", name);
                WriteBmp24(path, desc.Width, desc.Height, alpha, rowBytes);
            }
            free(colour); free(alpha);
            sys->UnlockRect();
        }
    }
    sys->Release();
}

// Xbox clear flags: bits 4..7 are the per-channel colour masks (0xF0 = all), bit 0 = Z, bit 1 = stencil.
void D3D9_Clear(uint32_t rectCount, void *pRects, uint32_t flags, uint32_t colour, float z, uint32_t stencil) {
    (void)rectCount; (void)pRects;
    if (g_device == NULL)
        return;
    DWORD d3dFlags = 0;
    if (flags & 0xF0) d3dFlags |= D3DCLEAR_TARGET;
    if (flags & 0x01) d3dFlags |= D3DCLEAR_ZBUFFER;
    if ((flags & 0x02) && g_hasStencil) d3dFlags |= D3DCLEAR_STENCIL;
    if (g_reversedDepth) z = 1.0f - z;
    IDirect3DSurface9 *depth = NULL;
    if (FAILED(g_device->GetDepthStencilSurface(&depth)) || depth == NULL)
        d3dFlags &= ~(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL); // a Clear naming a depth buffer that isn't there fails outright
    if (depth != NULL)
        depth->Release();
    if (d3dFlags != 0) {
        HRESULT hr = g_device->Clear(0, NULL, d3dFlags, colour, z, stencil);
        static int failures = 0;
        if (FAILED(hr) && failures++ < 4)
            D3D9Log("[d3d9] Clear(flags 0x%lx) failed: 0x%08lx" "\n", d3dFlags, hr);
    }
}

void D3D9_Swap(uint32_t type) {
    (void)type;
    if (g_device == NULL)
        return;
    if (g_inScene) {
        g_device->EndScene();
        g_inScene = false;
    }
    if (g_dumpFrame != 0) {
        DumpSurface(g_backBufferSurface, "d3d9_dump_frame");
        g_dumpFrame = 0;
    }
    if (D3D9_DUMP_EVERY > 0 && (g_frameCount + 1) % D3D9_DUMP_EVERY == 0) {
        g_dumpFrame = g_frameCount + 1; // the next frame gets dumped
        g_dumpRtCount = 0;
    }
    HRESULT hr = g_device->Present(NULL, NULL, NULL, NULL);
    PaceFrame();
    if (hr == D3DERR_DEVICELOST) {
        if (g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            D3D9Log("[d3d9] device lost - resetting (device state is not restored yet at this checkpoint).\n");
            ReleaseDefaultPoolResources(); // D3DPOOL_DEFAULT: must not exist across Reset
            if (SUCCEEDED(g_device->Reset(&g_presentParams))) {
                g_device->GetRenderTarget(0, &g_backBufferSurface);
                g_device->GetDepthStencilSurface(&g_mainDepthSurface);
            }
        }
    }
    g_frameCount++;
    if (g_frameCount == 1 || g_frameCount % 600 == 0)
        PrintMissingSummary();
    BeginSceneIfNeeded();
}

void D3D9_SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float minZ, float maxZ) {
    if (g_device == NULL)
        return;
    D3DVIEWPORT9 vp = { x, y, width, height, minZ, maxZ };
    g_device->SetViewport(&vp);
}

// Xbox D3DGAMMARAMP is three 256-entry byte tables; D3D9's is three 16-bit tables. (D3D9 only honours gamma
// ramps in fullscreen mode, so this is a no-op visually while we run windowed - kept for completeness.)
void D3D9_SetGammaRamp(uint32_t flags, void *pRamp) {
    (void)flags;
    if (g_device == NULL || pRamp == NULL)
        return;
    const uint8_t *xbox = (const uint8_t*)pRamp;
    D3DGAMMARAMP ramp;
    for (int i = 0; i < 256; i++) {
        ramp.red[i]   = (WORD)(xbox[i] * 257);
        ramp.green[i] = (WORD)(xbox[256 + i] * 257);
        ramp.blue[i]  = (WORD)(xbox[512 + i] * 257);
    }
    g_device->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &ramp);
}

// ---------------------------------------------------------------------------------------------------------------
// Render state. The "simple" render states arrive as raw NV097 methods with GL-style enum values; the rest
// through their own entry points.
// ---------------------------------------------------------------------------------------------------------------

static D3DCMPFUNC GlCompareToD3D(uint32_t gl) {           // NV097 0x200..0x207 = NEVER..ALWAYS
    if (gl >= 0x200 && gl <= 0x207) return (D3DCMPFUNC)(gl - 0x200 + 1);
    return D3DCMP_ALWAYS;
}
// With reversed Z, "nearer" is a larger depth value, so the ordering comparisons flip.
static D3DCMPFUNC ReverseCompareIfNeeded(D3DCMPFUNC f) {
    if (!g_reversedDepth) return f;
    switch (f) {
        case D3DCMP_LESS: return D3DCMP_GREATER;
        case D3DCMP_LESSEQUAL: return D3DCMP_GREATEREQUAL;
        case D3DCMP_GREATER: return D3DCMP_LESS;
        case D3DCMP_GREATEREQUAL: return D3DCMP_LESSEQUAL;
        default: return f;
    }
}
static D3DBLEND GlBlendToD3D(uint32_t gl) {
    switch (gl) {
        case 0: return D3DBLEND_ZERO;
        case 1: return D3DBLEND_ONE;
        case 0x300: return D3DBLEND_SRCCOLOR;
        case 0x301: return D3DBLEND_INVSRCCOLOR;
        case 0x302: return D3DBLEND_SRCALPHA;
        case 0x303: return D3DBLEND_INVSRCALPHA;
        case 0x304: return D3DBLEND_DESTALPHA;
        case 0x305: return D3DBLEND_INVDESTALPHA;
        case 0x306: return D3DBLEND_DESTCOLOR;
        case 0x307: return D3DBLEND_INVDESTCOLOR;
        case 0x308: return D3DBLEND_SRCALPHASAT;
        default: return D3DBLEND_ONE;
    }
}
static D3DBLENDOP GlBlendOpToD3D(uint32_t gl) {
    switch (gl) {
        case 0x8006: return D3DBLENDOP_ADD;
        case 0x8007: return D3DBLENDOP_MIN;
        case 0x8008: return D3DBLENDOP_MAX;
        case 0x800a: return D3DBLENDOP_SUBTRACT;
        case 0x800b: return D3DBLENDOP_REVSUBTRACT;
        default: return D3DBLENDOP_ADD;
    }
}

void D3D9_SetRenderStateSimple(uint32_t nv2aMethod, uint32_t value) {
    if (g_device == NULL)
        return;
    switch (nv2aMethod & 0xFFFF) {
        case 0x300: g_device->SetRenderState(D3DRS_ALPHATESTENABLE, value != 0); break;
        case 0x304: g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, value != 0); break;
        case 0x310: g_device->SetRenderState(D3DRS_DITHERENABLE, value != 0); break;
        case 0x33c: g_device->SetRenderState(D3DRS_ALPHAFUNC, GlCompareToD3D(value)); break;
        case 0x340: g_device->SetRenderState(D3DRS_ALPHAREF, value & 0xFF); break;
        case 0x344: g_device->SetRenderState(D3DRS_SRCBLEND, GlBlendToD3D(value)); break;
        case 0x348: g_device->SetRenderState(D3DRS_DESTBLEND, GlBlendToD3D(value)); break;
        case 0x350: g_device->SetRenderState(D3DRS_BLENDOP, GlBlendOpToD3D(value)); break;
        case 0x354: g_device->SetRenderState(D3DRS_ZFUNC, ReverseCompareIfNeeded(GlCompareToD3D(value))); break;
        case 0x358: { // NV097_SET_COLOR_MASK: one byte per channel, A R G B from the top
            DWORD mask = 0;
            if (value & 0x00FF0000) mask |= D3DCOLORWRITEENABLE_RED;
            if (value & 0x0000FF00) mask |= D3DCOLORWRITEENABLE_GREEN;
            if (value & 0x000000FF) mask |= D3DCOLORWRITEENABLE_BLUE;
            if (value & 0xFF000000) mask |= D3DCOLORWRITEENABLE_ALPHA;
            g_device->SetRenderState(D3DRS_COLORWRITEENABLE, mask);
            break;
        }
        case 0x35c: g_device->SetRenderState(D3DRS_ZWRITEENABLE, value != 0); break;
        default: {
            static uint32_t warned[16]; static int warnedCount = 0;
            bool seen = false;
            for (int i = 0; i < warnedCount; i++) if (warned[i] == nv2aMethod) seen = true;
            if (!seen && warnedCount < 16) { warned[warnedCount++] = nv2aMethod; D3D9Log("[d3d9] unhandled NV2A render state method 0x%x = 0x%x\n", nv2aMethod, value); }
            break;
        }
    }
}

void D3D9_SetCullMode(int xboxCullMode) {
    if (g_device == NULL) return;
    DWORD mode = (xboxCullMode == 0x901) ? D3DCULL_CCW : (xboxCullMode == 0x900) ? D3DCULL_CW : D3DCULL_NONE;
    g_device->SetRenderState(D3DRS_CULLMODE, mode);
}
void D3D9_SetFogColor(uint32_t colour) {
    if (g_device != NULL) g_device->SetRenderState(D3DRS_FOGCOLOR, colour);
}
void D3D9_SetZBias(int zBias) {
    if (g_device == NULL) return;
    float bias = (float)zBias * (g_reversedDepth ? 0.00001f : -0.00001f); // Xbox ZBias (0..16 towards the viewer) -> D3D9 depth bias
    g_device->SetRenderState(D3DRS_DEPTHBIAS, *(DWORD*)&bias);
}
void D3D9_SetZEnable(uint32_t value) {
    if (g_device != NULL) g_device->SetRenderState(D3DRS_ZENABLE, value != 0 ? D3DZB_TRUE : D3DZB_FALSE);
}
void D3D9_SetNormalizeNormals(uint32_t value) { (void)value; } // fixed-function lighting is never used
void D3D9_SetVertexBlend(uint32_t value) { (void)value; }      // ditto
void D3D9_SetShaderConstantMode(uint32_t value) { (void)value; } // constants are addressed absolutely here
void D3D9_SetYuvEnable(uint32_t enable) { (void)enable; }      // movie frames are converted to RGB on upload
// The game runs with W-buffering (SetRenderState_ZEnable(2)) and scales its projection so that clip w is the
// 24-bit depth value; SetDepthClipPlanes then gives the near/far clip in those units (float bit patterns).
// The translated shaders write depth from w accordingly - see the translation notes.
static float g_depthClipNear = 0.0f, g_depthClipFar = 16777215.0f;
void D3D9_SetDepthClipPlanes(uint32_t p1, uint32_t p2, uint32_t p3) {
    (void)p3;
    memcpy(&g_depthClipNear, &p1, 4);
    memcpy(&g_depthClipFar, &p2, 4);
}
// Texture stages. The game only ever binds Xbox stages 0, 1 and 3, and the NV2A's register combiners didn't
// care about gaps; D3D9's fixed-function pixel pipeline stops at the first disabled stage. So at draw time the
// Xbox stages that have a texture bound (plus stage 0 always) are packed into host stages 0, 1, 2 in order,
// and each host stage gets its Xbox stage's texture, sampler and combiner state from D3D8's deferred arrays.
// Texture coordinate indices refer to the shader's oT outputs, which are all emitted, so they survive the
// packing unchanged.
static const void *g_boundTexture[4]; // Xbox header bound to each Xbox stage (NULL = none)
static uint32_t g_borderColour[4];

void D3D9_SetTextureBorderColor(uint32_t stage, uint32_t colour) {
    if (stage < 4) g_borderColour[stage] = colour;
}

void D3D9_SetTexture(uint32_t stage, void *pTexture) {
    if (stage < 4)
        g_boundTexture[stage] = pTexture; // applied at draw time, see ApplyTextureStageState
}

static DWORD XboxAddressToD3D(uint32_t v) { return (v >= 1 && v <= 4) ? v : (v == 5 ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP); }
static DWORD XboxFilterToD3D(uint32_t v) { return (v <= 3) ? v : D3DTEXF_LINEAR; }

// The NV2A addresses linear (non-swizzled) textures - movie frames, render targets, the LIN_* formats - in
// texels rather than 0..1, so their coordinates need scaling by 1/size on the way to D3D9. Indexed by
// texture-coordinate set (the stage's TEXCOORDINDEX), recomputed per draw; (1,1) for swizzled/compressed.
static float g_texCoordScale[4][2] = { { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 } };

// Binds textures (re-uploading any the CPU wrote to since - movie frames, the intro effect - the seam only
// calls SetTexture when the bound slot changes) and applies the stage state for the current draw.
static void ApplyTextureStageState(bool shaderDraw) {
    static const uint32_t xboxStages[3] = { 0, 1, 3 };
    uint32_t host = 0;
    for (int t = 0; t < 4; t++) g_texCoordScale[t][0] = g_texCoordScale[t][1] = 1.0f;
    for (int i = 0; i < 3; i++) {
        uint32_t s = xboxStages[i];
        if (s != 0 && g_boundTexture[s] == NULL)
            continue;
        uint32_t h = host++;
        g_device->SetTexture(h, g_boundTexture[s] != NULL ? GetHostTexture(g_boundTexture[s]) : NULL);
        if (g_dumpFrame != 0 && g_boundTexture[s] != NULL && g_dumpRtCount < 8) {
            HostTexture *t = FindHostTexture(g_boundTexture[s]);
            if (t != NULL && t->renderTarget && t->rtSurface != NULL) {
                char name[64];
                snprintf(name, sizeof(name), "d3d9_dump_rt_sampled_%d", g_dumpRtCount++);
                DumpSurface(t->rtSurface, name);
            }
        }
        if (g_boundTexture[s] != NULL) {
            const XboxPixelContainer *header = (const XboxPixelContainer*)g_boundTexture[s];
            if (header->Size != 0) { // linear: texel coordinates on the NV2A
                uint32_t tci = XBOX_TEXTURE_STATE(s, XTSS_TEXCOORDINDEX) & 0xFFFF;
                if (tci >= 4) tci = s;
                g_texCoordScale[tci][0] = 1.0f / (float)((header->Size & 0xFFF) + 1);
                g_texCoordScale[tci][1] = 1.0f / (float)(((header->Size >> 12) & 0xFFF) + 1);
            }
        }
        g_device->SetSamplerState(h, D3DSAMP_ADDRESSU, XboxAddressToD3D(XBOX_TEXTURE_STATE(s, XTSS_ADDRESSU)));
        g_device->SetSamplerState(h, D3DSAMP_ADDRESSV, XboxAddressToD3D(XBOX_TEXTURE_STATE(s, XTSS_ADDRESSV)));
        g_device->SetSamplerState(h, D3DSAMP_MAGFILTER, XboxFilterToD3D(XBOX_TEXTURE_STATE(s, XTSS_MAGFILTER)));
        g_device->SetSamplerState(h, D3DSAMP_MINFILTER, XboxFilterToD3D(XBOX_TEXTURE_STATE(s, XTSS_MINFILTER)));
        g_device->SetSamplerState(h, D3DSAMP_MIPFILTER, XboxFilterToD3D(XBOX_TEXTURE_STATE(s, XTSS_MIPFILTER)));
        g_device->SetSamplerState(h, D3DSAMP_MIPMAPLODBIAS, XBOX_TEXTURE_STATE(s, XTSS_MIPMAPLODBIAS));
        g_device->SetSamplerState(h, D3DSAMP_BORDERCOLOR, g_borderColour[s]);
        uint32_t colorOp = XBOX_TEXTURE_STATE(s, XTSS_COLOROP), alphaOp = XBOX_TEXTURE_STATE(s, XTSS_ALPHAOP);
        g_device->SetTextureStageState(h, D3DTSS_COLOROP, colorOp == 0 ? D3DTOP_DISABLE : colorOp);
        g_device->SetTextureStageState(h, D3DTSS_COLORARG0, XBOX_TEXTURE_STATE(s, XTSS_COLORARG0));
        g_device->SetTextureStageState(h, D3DTSS_COLORARG1, XBOX_TEXTURE_STATE(s, XTSS_COLORARG1));
        g_device->SetTextureStageState(h, D3DTSS_COLORARG2, XBOX_TEXTURE_STATE(s, XTSS_COLORARG2));
        g_device->SetTextureStageState(h, D3DTSS_ALPHAOP, alphaOp == 0 ? D3DTOP_DISABLE : alphaOp);
        g_device->SetTextureStageState(h, D3DTSS_ALPHAARG0, XBOX_TEXTURE_STATE(s, XTSS_ALPHAARG0));
        g_device->SetTextureStageState(h, D3DTSS_ALPHAARG1, XBOX_TEXTURE_STATE(s, XTSS_ALPHAARG1));
        g_device->SetTextureStageState(h, D3DTSS_ALPHAARG2, XBOX_TEXTURE_STATE(s, XTSS_ALPHAARG2));
        uint32_t texCoordIndex = XBOX_TEXTURE_STATE(s, XTSS_TEXCOORDINDEX) & 0xFFFF;
        g_device->SetTextureStageState(h, D3DTSS_TEXCOORDINDEX, texCoordIndex < 4 ? texCoordIndex : s);
        // Every NV2A 2D texture stage divides by q; the shaders leave q = 1 unless projecting (the character
        // shadow). Pre-transformed quads carry 2D coordinates with no q.
        g_device->SetTextureStageState(h, D3DTSS_TEXTURETRANSFORMFLAGS, shaderDraw ? (D3DTTFF_COUNT4 | D3DTTFF_PROJECTED) : D3DTTFF_DISABLE);
    }
    for (; host < 4; host++) {
        g_device->SetTexture(host, NULL);
        g_device->SetTextureStageState(host, D3DTSS_COLOROP, D3DTOP_DISABLE);
        g_device->SetTextureStageState(host, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }
    g_device->SetRenderState(D3DRS_FOGENABLE, XBOX_RENDER_STATE(XRS_FOGENABLE) != 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Vertex shader translation
//
// The game's vertex shaders are NV2A vertex programs (the XDK's D3DVS binary: an 8-byte header {0x78, 0x20,
// instruction count, 0} then 16 bytes per instruction, three meaningful words and one unused). Each is
// decoded here with the field layout documented by Erik Abair's nv2a-vsh (Unlicense) and re-emitted as
// HLSL, compiled with D3DCompile to vs_2_0 on first use. vs_2_0 rather than 3_0 because D3D9 refuses to
// pair a shader-model-3 vertex shader with the fixed-function pixel pipeline, and the game's texture
// combining still goes through texture-stage states.
//
// Two things differ from the hardware. First, an NV2A program ends by doing its own perspective divide and
// viewport transform (MUL oPos.xyz, R12, c[58] + RCC R1.x, R12.w; MAD oPos.xyz, R12, R1.x, c[59]), with c58/
// c59 written by the Xbox D3D runtime from the viewport. D3D9 wants clip-space positions, so c58 is pinned to
// (1,1,1,1) and c59 to zero - making the program's own epilogue produce plain NDC - and the translated shader
// multiplies xy back up by w on output. Depth is taken from w rather than the program's z: the game W-buffers
// (ZEnable(2)) with a projection scaled so w is the 24-bit depth, and clips w against SetDepthClipPlanes, so
// the emitted z is (w - near) / (far - near), which reproduces both the clipping and the depth order. Second,
// Xbox's NORMPACKED3 vertex type (11:11:10 signed normalised) has no D3D9 declaration type; it is declared as
// UBYTE4 and unpacked with float arithmetic in the prologue. Third, oFog is a fog coordinate on the NV2A and a
// factor on D3D9, so the render-state fog table function is applied in the shader epilogue.
// ---------------------------------------------------------------------------------------------------------------

enum { VSH_MAC_NOP, VSH_MAC_MOV, VSH_MAC_MUL, VSH_MAC_ADD, VSH_MAC_MAD, VSH_MAC_DP3, VSH_MAC_DPH, VSH_MAC_DP4,
       VSH_MAC_DST, VSH_MAC_MIN, VSH_MAC_MAX, VSH_MAC_SLT, VSH_MAC_SGE, VSH_MAC_ARL };
enum { VSH_ILU_NOP, VSH_ILU_MOV, VSH_ILU_RCP, VSH_ILU_RCC, VSH_ILU_RSQ, VSH_ILU_EXP, VSH_ILU_LOG, VSH_ILU_LIT };
enum { VSH_PARAM_R = 1, VSH_PARAM_V = 2, VSH_PARAM_C = 3 };

struct VshSource { int mux, tempReg, swz[4]; bool negate; };
struct VshDecoded {
    int mac, ilu, input, constIndex;
    bool a0x, final, outIsOutput;
    int outMux, outAddress, outOMask, outIluMask, outTempReg, outMacMask;
    VshSource a, b, c;
};

static void DecodeVshInstruction(const uint32_t *w, VshDecoded *d) {
    uint32_t b = w[0], c = w[1], e = w[2];
    d->a.swz[3] = b & 3; d->a.swz[2] = (b >> 2) & 3; d->a.swz[1] = (b >> 4) & 3; d->a.swz[0] = (b >> 6) & 3;
    d->a.negate = (b >> 8) & 1;
    d->input = (b >> 9) & 0xF;
    d->constIndex = (b >> 13) & 0xFF;
    d->mac = (b >> 21) & 0xF;
    d->ilu = (b >> 25) & 7;
    d->c.tempReg = (c & 3) << 2;
    d->c.swz[3] = (c >> 2) & 3; d->c.swz[2] = (c >> 4) & 3; d->c.swz[1] = (c >> 6) & 3; d->c.swz[0] = (c >> 8) & 3;
    d->c.negate = (c >> 10) & 1;
    d->b.mux = (c >> 11) & 3;
    d->b.tempReg = (c >> 13) & 0xF;
    d->b.swz[3] = (c >> 17) & 3; d->b.swz[2] = (c >> 19) & 3; d->b.swz[1] = (c >> 21) & 3; d->b.swz[0] = (c >> 23) & 3;
    d->b.negate = (c >> 25) & 1;
    d->a.mux = (c >> 26) & 3;
    d->a.tempReg = (c >> 28) & 0xF;
    d->final = e & 1;
    d->a0x = (e >> 1) & 1;
    d->outMux = (e >> 2) & 1;
    d->outAddress = (e >> 3) & 0xFF;
    d->outIsOutput = (e >> 11) & 1;
    d->outOMask = (e >> 12) & 0xF;
    d->outIluMask = (e >> 16) & 0xF;
    d->outTempReg = (e >> 20) & 0xF;
    d->outMacMask = (e >> 24) & 0xF;
    d->c.mux = (e >> 28) & 3;
    d->c.tempReg |= (e >> 30) & 3;
}

struct HlslWriter {
    char *buf; size_t size, len; bool overflow;
    void printf(const char *fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int n = vsnprintf(buf + len, size > len ? size - len : 0, fmt, ap);
        va_end(ap);
        if (n < 0 || (size_t)n >= size - len) { overflow = true; len = size - 1; }
        else len += (size_t)n;
    }
};

static const char *VshOutputName(int address) {
    switch (address) {
        case 0: return "oPos"; case 3: return "oD0"; case 4: return "oD1"; case 5: return "oFog"; case 6: return "oPts";
        case 7: return "oB0"; case 8: return "oB1"; case 9: return "oT0"; case 10: return "oT1"; case 11: return "oT2"; case 12: return "oT3";
        default: return NULL;
    }
}

// Writes "R3.xyzz" / "-(c[a0 + 96].xyzw)" / "v3.zzzz" for one source operand.
static void EmitVshSource(HlslWriter *w, const VshDecoded *d, const VshSource *s, uint32_t *inputsUsed) {
    static const char swzName[4] = { 'x', 'y', 'z', 'w' };
    if (s->negate) w->printf("-(");
    switch (s->mux) {
        case VSH_PARAM_R: if (s->tempReg == 12) w->printf("oPos"); else w->printf("R%d", s->tempReg); break;
        case VSH_PARAM_V: w->printf("v%d", d->input); *inputsUsed |= 1u << d->input; break;
        case VSH_PARAM_C: if (d->a0x) w->printf("c[a0 + %d]", d->constIndex); else w->printf("c[%d]", d->constIndex); break;
        default: w->printf("float4(0,0,0,0)"); break;
    }
    w->printf(".%c%c%c%c", swzName[s->swz[0]], swzName[s->swz[1]], swzName[s->swz[2]], swzName[s->swz[3]]);
    if (s->negate) w->printf(")");
}

static void VshMaskString(int vshMask, char out[5]) { // NV2A mask bits: 8 = x, 4 = y, 2 = z, 1 = w
    int n = 0;
    if (vshMask & 8) out[n++] = 'x';
    if (vshMask & 4) out[n++] = 'y';
    if (vshMask & 2) out[n++] = 'z';
    if (vshMask & 1) out[n++] = 'w';
    out[n] = 0;
}

// Translates one XDK vertex shader function blob to HLSL. Returns false if something isn't handled.
// inputsUsed/outputsWritten come back as register bitmasks (v0..v15 / the oX output addresses).
static bool TranslateVshToHlsl(const uint8_t *function, const uint32_t *normPackedInputs, char *out, size_t outSize,
                               uint32_t *inputsUsed, uint32_t *outputsWritten, int *instructionCountOut) {
    if (function[0] != 0x78 || function[1] != 0x20)
        return false;
    int count = function[2];
    *instructionCountOut = count;
    const uint32_t *words = (const uint32_t*)(function + 8);

    // Body first (so we know which inputs/outputs the header needs), into the second half of the buffer.
    char *body = out + outSize / 2;
    HlslWriter w = { body, outSize / 2, 0, false };
    *inputsUsed = 0; *outputsWritten = 1;
    for (int i = 0; i < count; i++) {
        VshDecoded d;
        DecodeVshInstruction(words + i * 4, &d);
        if (d.mac == VSH_MAC_NOP && d.ilu == VSH_ILU_NOP)
            continue;
        w.printf("    { // %d\n", i);
        w.printf("        float4 A = "); EmitVshSource(&w, &d, &d.a, inputsUsed); w.printf(";\n");
        w.printf("        float4 B = "); EmitVshSource(&w, &d, &d.b, inputsUsed); w.printf(";\n");
        w.printf("        float4 C = "); EmitVshSource(&w, &d, &d.c, inputsUsed); w.printf(";\n");
        // Both units read their operands before either writes (they execute in parallel on the hardware).
        if (d.mac != VSH_MAC_NOP) {
            w.printf("        float4 M = ");
            switch (d.mac) {
                case VSH_MAC_MOV: w.printf("A"); break;
                case VSH_MAC_MUL: w.printf("A * B"); break;
                case VSH_MAC_ADD: w.printf("A + C"); break;
                case VSH_MAC_MAD: w.printf("A * B + C"); break;
                case VSH_MAC_DP3: w.printf("dot(A.xyz, B.xyz).xxxx"); break;
                case VSH_MAC_DPH: w.printf("(dot(A.xyz, B.xyz) + B.w).xxxx"); break;
                case VSH_MAC_DP4: w.printf("dot(A, B).xxxx"); break;
                case VSH_MAC_DST: w.printf("float4(1.0, A.y * B.y, A.z, B.w)"); break;
                case VSH_MAC_MIN: w.printf("min(A, B)"); break;
                case VSH_MAC_MAX: w.printf("max(A, B)"); break;
                case VSH_MAC_SLT: w.printf("(float4)(A < B)"); break;
                case VSH_MAC_SGE: w.printf("(float4)(A >= B)"); break;
                case VSH_MAC_ARL: w.printf("A"); break;
                default: return false;
            }
            w.printf(";\n");
        }
        if (d.ilu != VSH_ILU_NOP) {
            w.printf("        float4 I = ");
            switch (d.ilu) {
                case VSH_ILU_MOV: w.printf("C"); break;
                case VSH_ILU_RCP: w.printf("(1.0 / C.x).xxxx"); break;
                case VSH_ILU_RCC: w.printf("((1.0 / C.x) >= 0 ? clamp(1.0 / C.x, 5.42101e-20, 1.884467e19) : clamp(1.0 / C.x, -1.884467e19, -5.42101e-20)).xxxx"); break;
                case VSH_ILU_RSQ: w.printf("(1.0 / sqrt(abs(C.x))).xxxx"); break;
                case VSH_ILU_EXP: w.printf("float4(exp2(floor(C.x)), frac(C.x), exp2(C.x), 1.0)"); break;
                case VSH_ILU_LOG: w.printf("float4(floor(log2(abs(C.x))), abs(C.x) / exp2(floor(log2(abs(C.x)))), log2(abs(C.x)), 1.0)"); break;
                case VSH_ILU_LIT: w.printf("lit(C.x, C.y, C.w)"); break;
                default: return false;
            }
            w.printf(";\n");
        }
        char mask[5];
        if (d.mac == VSH_MAC_ARL) {
            w.printf("        a0 = (int)floor(M.x);\n");
        } else if (d.mac != VSH_MAC_NOP) {
            if (d.outMacMask) { VshMaskString(d.outMacMask, mask); w.printf("        R%d.%s = M.%s;\n", d.outTempReg, mask, mask); }
            if (d.outOMask && d.outMux == 0) {
                const char *name = d.outIsOutput ? VshOutputName(d.outAddress) : NULL;
                if (name == NULL) return false;
                VshMaskString(d.outOMask, mask); w.printf("        %s.%s = M.%s;\n", name, mask, mask);
                *outputsWritten |= 1u << d.outAddress;
            }
        }
        if (d.ilu != VSH_ILU_NOP) {
            if (d.outIluMask) {
                int reg = (d.mac != VSH_MAC_NOP) ? 1 : d.outTempReg; // paired ILU results land in R1
                VshMaskString(d.outIluMask, mask); w.printf("        R%d.%s = I.%s;\n", reg, mask, mask);
            }
            if (d.outOMask && d.outMux == 1) {
                const char *name = d.outIsOutput ? VshOutputName(d.outAddress) : NULL;
                if (name == NULL) return false;
                VshMaskString(d.outOMask, mask); w.printf("        %s.%s = I.%s;\n", name, mask, mask);
                *outputsWritten |= 1u << d.outAddress;
            }
        }
        w.printf("    }\n");
        if (d.final) break;
    }
    if (w.overflow) return false;

    HlslWriter h = { out, outSize / 2, 0, false };
    h.printf("float4 c[192] : register(c0);\n");
    h.printf("float4 hostAdjust : register(c200); // .xy = D3D9's half-pixel offset in NDC\n");
    h.printf("float4 depthClip : register(c201);  // z_clip = .x * (w - .y): the W-buffer depth in screen-affine form, see PrepareShaderDraw\n");
    h.printf("float4 fogParams : register(c202);  // .x = start, .y = end, .z = density, .w = table mode (0 none, 1 exp, 2 exp2, 3 linear)\n");
    h.printf("float4 texScale01 : register(c203); // 1/size for linear textures on coordinate sets 0 (.xy) and 1 (.zw), else 1\n");
    h.printf("float4 texScale23 : register(c204); // same for sets 2 and 3\n");
    h.printf("struct VS_IN {\n");
    for (int r = 0; r < 16; r++)
        if (*inputsUsed & (1u << r)) h.printf("    float4 v%d : %s%d;\n", r, r == 0 ? "POSITION" : "TEXCOORD", r == 0 ? 0 : r);
    h.printf("};\nstruct VS_OUT {\n    float4 oPos : POSITION;\n    float4 oD0 : COLOR0;\n    float4 oD1 : COLOR1;\n");
    if (*outputsWritten & (1u << 5)) h.printf("    float oFog : FOG;\n");
    if (*outputsWritten & (1u << 6)) h.printf("    float oPts : PSIZE;\n");
    h.printf("    float4 oT0 : TEXCOORD0;\n    float4 oT1 : TEXCOORD1;\n    float4 oT2 : TEXCOORD2;\n    float4 oT3 : TEXCOORD3;\n};\n");
    h.printf("VS_OUT main(VS_IN vin) {\n");
    for (int r = 0; r < 16; r++) {
        if (!(*inputsUsed & (1u << r))) continue;
        if (*normPackedInputs & (1u << r)) {
            // 11:11:10 signed normalised, arriving as four raw bytes (UBYTE4): rebuild the fields with exact float maths.
            h.printf("    float4 v%d;\n    {\n        float4 b = vin.v%d;\n        float lo = b.x + 256.0 * b.y;\n", r, r);
            h.printf("        float x = fmod(lo, 2048.0);\n        float y = floor(lo / 2048.0) + 32.0 * fmod(b.z, 64.0);\n        float z = floor(b.z / 64.0) + 4.0 * b.w;\n");
            h.printf("        x = (x >= 1024.0) ? x - 2048.0 : x;\n        y = (y >= 1024.0) ? y - 2048.0 : y;\n        z = (z >= 512.0) ? z - 1024.0 : z;\n");
            h.printf("        v%d = float4(x / 1023.0, y / 1023.0, z / 511.0, 1.0);\n    }\n", r);
        } else {
            h.printf("    float4 v%d = vin.v%d;\n", r, r);
        }
    }
    h.printf("    float4 R0 = 0, R1 = 0, R2 = 0, R3 = 0, R4 = 0, R5 = 0, R6 = 0, R7 = 0, R8 = 0, R9 = 0, R10 = 0, R11 = 0;\n");
    // Output registers start as (0,0,0,1) on the hardware; the w matters because texture stages divide by q.
    h.printf("    float4 oPos = 0, oD0 = 0, oD1 = 0, oFog = 0, oPts = 0, oB0 = 0, oB1 = 0;\n");
    h.printf("    float4 oT0 = float4(0,0,0,1), oT1 = float4(0,0,0,1), oT2 = float4(0,0,0,1), oT3 = float4(0,0,0,1);\n");
    h.printf("    int a0 = 0;\n");
    if (h.overflow || h.len + w.len + 512 > outSize / 2) return false;
    memmove(out + h.len, body, w.len);
    h.len += w.len;
    h.printf("    VS_OUT o;\n");
    // Position: xy back to clip space; z from w (the W-buffer depth) in the a + b/w form the rasteriser
    // interpolates exactly, mapped so the depth-clip planes land on D3D9's 0..1 z range (reversed when the
    // float depth buffer is in use) - which both clips where the NV2A clipped and keeps the depth ordering.
    h.printf("    o.oPos = float4((oPos.xy + hostAdjust.xy) * oPos.w, depthClip.x * (oPos.w - depthClip.y), oPos.w);\n");
    h.printf("    o.oD0 = oD0;\n    o.oD1 = oD1;\n");
    // Fog: on the Xbox the shader's oFog is the fog coordinate (the game emits its normalised near..far
    // distance) and the fog table mode turns it into the factor; D3D9 takes the factor straight from the
    // shader, so apply the table function here. Written as arithmetic selects, since vs_2_0 can't branch.
    if (*outputsWritten & (1u << 5)) {
        h.printf("    {\n        float d = oFog.x;\n");
        h.printf("        float fLinear = (fogParams.y - d) / max(fogParams.y - fogParams.x, 1e-6);\n");
        h.printf("        float fExp = exp(-d * fogParams.z);\n");
        h.printf("        float fExp2 = exp(-(d * d * fogParams.z * fogParams.z));\n");
        h.printf("        float f = (fogParams.w == 1.0) ? fExp : (fogParams.w == 2.0) ? fExp2 : (fogParams.w == 3.0) ? fLinear : d;\n");
        h.printf("        o.oFog = saturate(f);\n    }\n");
    }
    if (*outputsWritten & (1u << 6)) h.printf("    o.oPts = oPts.x;\n");
    h.printf("    o.oT0 = oT0 * float4(texScale01.xy, 1, 1);\n    o.oT1 = oT1 * float4(texScale01.zw, 1, 1);\n");
    h.printf("    o.oT2 = oT2 * float4(texScale23.xy, 1, 1);\n    o.oT3 = oT3 * float4(texScale23.zw, 1, 1);\n    return o;\n}\n");
    return !h.overflow;
}

// Xbox D3DVSD declaration tokens: 0x2000000s selects stream s, 0x40tt00rr declares register rr of type tt at the
// running offset, 0xffffffff ends. Type codes are the Xbox D3DVSDT_* values.
static bool XboxVertexTypeInfo(uint32_t type, uint32_t *bytes, D3DDECLTYPE *hostType, bool *normPacked) {
    *normPacked = false;
    switch (type) {
        case 0x12: *bytes = 4;  *hostType = D3DDECLTYPE_FLOAT1; return true;
        case 0x22: *bytes = 8;  *hostType = D3DDECLTYPE_FLOAT2; return true;
        case 0x32: *bytes = 12; *hostType = D3DDECLTYPE_FLOAT3; return true;
        case 0x42: *bytes = 16; *hostType = D3DDECLTYPE_FLOAT4; return true;
        case 0x40: *bytes = 4;  *hostType = D3DDECLTYPE_D3DCOLOR; return true;
        case 0x16: *bytes = 4;  *hostType = D3DDECLTYPE_UBYTE4; *normPacked = true; return true; // NORMPACKED3
        case 0x15: *bytes = 4;  *hostType = D3DDECLTYPE_SHORT2; return true;
        case 0x35: *bytes = 6;  *hostType = D3DDECLTYPE_SHORT4; return true; // SHORT3: read as SHORT4, .w unused (see the +8 buffer slack)
        case 0x45: *bytes = 8;  *hostType = D3DDECLTYPE_SHORT4; return true;
        case 0x11: *bytes = 2;  *hostType = D3DDECLTYPE_SHORT2N; return true; // NORMSHORT1 - approximated
        case 0x21: *bytes = 4;  *hostType = D3DDECLTYPE_SHORT2N; return true;
        case 0x41: *bytes = 8;  *hostType = D3DDECLTYPE_SHORT4N; return true;
        case 0x14: *bytes = 4;  *hostType = D3DDECLTYPE_UBYTE4; return true;  // PBYTE4 (raw bytes)
        default: return false;
    }
}

struct TranslatedVertexShader {
    const void *declaration, *function;
    IDirect3DVertexShader9 *shader;
    IDirect3DVertexDeclaration9 *decl;
    uint32_t streamsUsed;   // bit s = declaration reads stream s
    uint32_t inputsUsed, outputsWritten;
    bool attempted, failed;
};
static TranslatedVertexShader g_vertexShaders[160];
static int g_vertexShaderCount = 0;
static int g_currentVertexShader = -1;
static float g_vertexConstants[192][4];
static bool g_constantsDirty = true;
#define VS_HANDLE_TAG 0x56530000u

static bool BuildVertexShader(TranslatedVertexShader *vs, int index) {
    vs->attempted = true;
    vs->failed = true;
    if (g_device == NULL)
        return false;

    // Declaration first: it also tells us which inputs are NORMPACKED3.
    D3DVERTEXELEMENT9 elements[24];
    int elementCount = 0;
    uint32_t normPacked = 0;
    uint32_t stream = 0, offset = 0;
    vs->streamsUsed = 0;
    for (const uint32_t *tok = (const uint32_t*)vs->declaration; *tok != 0xFFFFFFFFu; tok++) {
        if ((*tok & 0xF0000000u) == 0x20000000u) { stream = *tok & 0xF; offset = 0; continue; }
        if ((*tok & 0xF0000000u) != 0x40000000u) continue;
        uint32_t type = (*tok >> 16) & 0xFF, reg = *tok & 0xF;
        uint32_t bytes; D3DDECLTYPE hostType; bool np;
        if (!XboxVertexTypeInfo(type, &bytes, &hostType, &np) || elementCount >= 23) {
            D3D9Log("[d3d9] shader %d: vertex type 0x%02x not handled\n", index, type);
            return false;
        }
        D3DVERTEXELEMENT9 e = { (WORD)stream, (WORD)offset, (BYTE)hostType, D3DDECLMETHOD_DEFAULT,
                                (BYTE)(reg == 0 ? D3DDECLUSAGE_POSITION : D3DDECLUSAGE_TEXCOORD), (BYTE)(reg == 0 ? 0 : reg) };
        elements[elementCount++] = e;
        if (np) normPacked |= 1u << reg;
        vs->streamsUsed |= 1u << stream;
        offset += bytes;
    }
    D3DVERTEXELEMENT9 end = D3DDECL_END();
    elements[elementCount++] = end;

    static char hlsl[65536];
    int instructionCount = 0;
    if (!TranslateVshToHlsl((const uint8_t*)vs->function, &normPacked, hlsl, sizeof(hlsl), &vs->inputsUsed, &vs->outputsWritten, &instructionCount)) {
        D3D9Log("[d3d9] shader %d: translation failed\n", index);
        return false;
    }
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), NULL, NULL, NULL, "main", "vs_2_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        if (errors != NULL) { errors->Release(); errors = NULL; }
        hr = D3DCompile(hlsl, strlen(hlsl), NULL, NULL, NULL, "main", "vs_2_a", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    }
    if (FAILED(hr)) {
        D3D9Log("[d3d9] shader %d: HLSL compile failed (0x%08lx):\n%s\n", index, hr, errors ? (const char*)errors->GetBufferPointer() : "(no message)");
        static bool dumped = false;
        if (!dumped) { dumped = true; FILE *f = fopen("d3d9_failed_shader.hlsl", "w"); if (f) { fputs(hlsl, f); fclose(f); } }
        if (errors) errors->Release();
        return false;
    }
    if (errors) errors->Release();
    hr = g_device->CreateVertexShader((const DWORD*)code->GetBufferPointer(), &vs->shader);
    code->Release();
    if (FAILED(hr)) { D3D9Log("[d3d9] shader %d: CreateVertexShader failed 0x%08lx\n", index, hr); return false; }
    if (FAILED(g_device->CreateVertexDeclaration(elements, &vs->decl))) { D3D9Log("[d3d9] shader %d: CreateVertexDeclaration failed\n", index); return false; }
    D3D9Log("[d3d9] shader %d translated: %d NV2A instructions, inputs 0x%04x, outputs 0x%04x, streams 0x%03x\n",
           index, instructionCount, vs->inputsUsed, vs->outputsWritten, vs->streamsUsed);
    vs->failed = false;
    return true;
}

uint32_t D3D9_CreateVertexShader(const void *pDeclaration, const void *pFunction, void **pHandle, uint32_t usage) {
    (void)usage;
    if (g_vertexShaderCount >= (int)(sizeof(g_vertexShaders) / sizeof(g_vertexShaders[0])))
        return 0x8876086Cu;
    TranslatedVertexShader *vs = &g_vertexShaders[g_vertexShaderCount];
    memset(vs, 0, sizeof(*vs));
    vs->declaration = pDeclaration;
    vs->function = pFunction;
    *pHandle = (void*)(uintptr_t)(VS_HANDLE_TAG | (uint32_t)g_vertexShaderCount); // 'VS' tag + index
    g_vertexShaderCount++;
    return 0;
}

void D3D9_SetVertexShader(void *handle) {
    uint32_t h = (uint32_t)(uintptr_t)handle;
    g_currentVertexShader = ((h & 0xFFFF0000u) == VS_HANDLE_TAG) ? (int)(h & 0xFFFF) : -1;
}

static void StoreConstants(uint32_t index, const float *values, uint32_t count4) {
    for (uint32_t i = 0; i < count4; i++) {
        uint32_t k = index + i;
        if (k < 192 && k != 58 && k != 59) // 58/59 are the pinned viewport constants, see the translation notes
            memcpy(g_vertexConstants[k], values + i * 4, 16);
    }
    g_constantsDirty = true;
}
void D3D9_SetVertexShaderConstant1(uint32_t constantIndex, float *pConstants) { StoreConstants(constantIndex, pConstants, 1); }
void D3D9_SetVertexShaderConstant4(uint32_t constantIndex, void *pMatrix) { StoreConstants(constantIndex, (const float*)pMatrix, 4); }
void D3D9_SetVertexShaderConstantNotInline(uint32_t constantIndex, void *pData, uint32_t countDwords) { StoreConstants(constantIndex, (const float*)pData, countDwords / 4); }

static void InitPinnedConstants(void) {
    static const float scale[4] = { 1, 1, 1, 1 }, offset[4] = { 0, 0, 0, 0 };
    memcpy(g_vertexConstants[58], scale, 16);
    memcpy(g_vertexConstants[59], offset, 16);
    g_constantsDirty = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Textures and geometry
// ---------------------------------------------------------------------------------------------------------------

// Vertex and index buffers live in the game's memory (Gfx's slot tables, see d3dSeam.cpp). Each Xbox buffer
// object gets a host copy on first bind; D3DResource_Register on the same slot (a level reload) drops it.
// The overlay quad table (0x2CAFE8, 20-byte slots) has no byte size, only a vertex count at +0x10 with the
// point-sprite stride of 0x24; the vertex-buffer table (0x2DECEC, 36-byte slots) has its byte size at +0x14.
#define OVERLAY_TABLE_BASE 0x002CAFE8u
#define OVERLAY_TABLE_END  (OVERLAY_TABLE_BASE + 256u * 20u)

struct HostVertexBuffer { const void *obj; const void *data; uint32_t size; IDirect3DVertexBuffer9 *vb; };
struct HostIndexBuffer  { const void *obj; const void *data; uint32_t size; IDirect3DIndexBuffer9 *ib; };
static HostVertexBuffer g_vertexBuffers[2300];
static int g_vertexBufferCount = 0;
static HostIndexBuffer g_indexBuffers[2100];
static int g_indexBufferCount = 0;

static void InvalidateHostBuffers(const void *obj) {
    for (int i = 0; i < g_vertexBufferCount; i++) {
        if (g_vertexBuffers[i].obj == obj) {
            if (g_vertexBuffers[i].vb) g_vertexBuffers[i].vb->Release();
            g_vertexBuffers[i] = g_vertexBuffers[--g_vertexBufferCount];
            return;
        }
    }
    for (int i = 0; i < g_indexBufferCount; i++) {
        if (g_indexBuffers[i].obj == obj) {
            if (g_indexBuffers[i].ib) g_indexBuffers[i].ib->Release();
            g_indexBuffers[i] = g_indexBuffers[--g_indexBufferCount];
            return;
        }
    }
}

static IDirect3DVertexBuffer9 *GetHostVertexBuffer(const void *obj, uint32_t *sizeOut) {
    const uint32_t *slot = (const uint32_t*)obj;
    const void *data = (const void*)(uintptr_t)slot[1];
    uint32_t size;
    if ((uintptr_t)obj >= OVERLAY_TABLE_BASE && (uintptr_t)obj < OVERLAY_TABLE_END)
        size = slot[4] * 0x24;
    else
        size = slot[5];
    if (data == NULL || size == 0)
        return NULL;
    for (int i = 0; i < g_vertexBufferCount; i++) {
        HostVertexBuffer *h = &g_vertexBuffers[i];
        if (h->obj == obj) {
            if (h->data == data && h->size == size) { *sizeOut = size; return h->vb; }
            if (h->vb) h->vb->Release();
            g_vertexBuffers[i] = g_vertexBuffers[--g_vertexBufferCount];
            break;
        }
    }
    if (g_vertexBufferCount >= (int)(sizeof(g_vertexBuffers) / sizeof(g_vertexBuffers[0])))
        return NULL;
    IDirect3DVertexBuffer9 *vb = NULL;
    // +8 bytes of slack: SHORT3 morph streams are declared as SHORT4, so the last vertex reads 2 bytes past the end.
    g_statBufferCreates++;
    if (FAILED(g_device->CreateVertexBuffer(size + 8, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb, NULL)))
        return NULL;
    void *dst = NULL;
    if (SUCCEEDED(vb->Lock(0, 0, &dst, 0))) {
        memcpy(dst, data, size);
        memset((uint8_t*)dst + size, 0, 8);
        vb->Unlock();
    }
    HostVertexBuffer *h = &g_vertexBuffers[g_vertexBufferCount++];
    h->obj = obj; h->data = data; h->size = size; h->vb = vb;
    *sizeOut = size;
    return vb;
}

static IDirect3DIndexBuffer9 *GetHostIndexBuffer(const void *obj, const void **dataOut, uint32_t *sizeOut) {
    const uint32_t *slot = (const uint32_t*)obj;
    const void *data = (const void*)(uintptr_t)slot[1];
    uint32_t size = slot[5]; // D3DIndexBufferSlotRaw::byteSize
    if (data == NULL || size == 0)
        return NULL;
    for (int i = 0; i < g_indexBufferCount; i++) {
        HostIndexBuffer *h = &g_indexBuffers[i];
        if (h->obj == obj) {
            if (h->data == data && h->size == size) { *dataOut = data; *sizeOut = size; return h->ib; }
            if (h->ib) h->ib->Release();
            g_indexBuffers[i] = g_indexBuffers[--g_indexBufferCount];
            break;
        }
    }
    if (g_indexBufferCount >= (int)(sizeof(g_indexBuffers) / sizeof(g_indexBuffers[0])))
        return NULL;
    IDirect3DIndexBuffer9 *ib = NULL;
    g_statBufferCreates++;
    if (FAILED(g_device->CreateIndexBuffer(size, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL)))
        return NULL;
    void *dst = NULL;
    if (SUCCEEDED(ib->Lock(0, 0, &dst, 0))) {
        memcpy(dst, data, size);
        ib->Unlock();
    }
    HostIndexBuffer *h = &g_indexBuffers[g_indexBufferCount++];
    h->obj = obj; h->data = data; h->size = size; h->ib = ib;
    *dataOut = data; *sizeOut = size;
    return ib;
}

static const void *g_streams[16];
static int g_streamStrides[16];
static const void *g_indexBuffer = NULL;
void D3D9_SetStreamSource(int streamNumber, void *vertexBuffer, int stride) {
    if (streamNumber >= 0 && streamNumber < 16) { g_streams[streamNumber] = vertexBuffer; g_streamStrides[streamNumber] = stride; }
}
void D3D9_SetIndices(void *pIndexBuffer, uint32_t baseVertexIndex) { (void)baseVertexIndex; g_indexBuffer = pIndexBuffer; }

// ---------------------------------------------------------------------------------------------------------------
// Render targets. The game renders character shadows into a 256x256 texture (the aux pass), box-blurs them via
// a 128x128 texture with the immediate quad path, and projects the result onto the level; psiBlurScreen copies
// the backbuffer into a texture. Render-target textures live in D3DPOOL_DEFAULT with their own depth surface.
// ---------------------------------------------------------------------------------------------------------------

struct DepthSurfaceForSize { uint32_t width, height; IDirect3DSurface9 *surface; };
static DepthSurfaceForSize g_depthSurfaces[8];
static int g_depthSurfaceCount = 0;

static IDirect3DSurface9 *GetDepthSurfaceFor(uint32_t width, uint32_t height) {
    for (int i = 0; i < g_depthSurfaceCount; i++)
        if (g_depthSurfaces[i].width == width && g_depthSurfaces[i].height == height)
            return g_depthSurfaces[i].surface;
    if (g_depthSurfaceCount >= (int)(sizeof(g_depthSurfaces) / sizeof(g_depthSurfaces[0])))
        return NULL;
    IDirect3DSurface9 *surface = NULL;
    // Discard must be FALSE: D3D9 refuses discardable depth surfaces in lockable formats (D3DFMT_D32F_LOCKABLE).
    HRESULT hr = g_device->CreateDepthStencilSurface(width, height, g_presentParams.AutoDepthStencilFormat, D3DMULTISAMPLE_NONE, 0, FALSE, &surface, NULL);
    if (FAILED(hr)) {
        D3D9Log("[d3d9] depth surface %ux%u creation failed: 0x%08lx" "\n", width, height, hr);
        return NULL;
    }
    DepthSurfaceForSize *d = &g_depthSurfaces[g_depthSurfaceCount++];
    d->width = width; d->height = height; d->surface = surface;
    return surface;
}

// Turns (or keeps) the host texture for an Xbox header as a render-target texture of the header's size.
static HostTexture *EnsureRenderTargetTexture(const void *headerPtr) {
    const XboxPixelContainer *h = (const XboxPixelContainer*)headerPtr;
    HostTexture *t = FindOrAddHostTexture(headerPtr);
    if (t == NULL || g_device == NULL)
        return NULL;
    if (t->renderTarget && t->texture != NULL && t->format == h->Format && t->size == h->Size)
        return t;
    ReleaseHostTexture(t);
    uint32_t width, height;
    if (h->Size != 0) { width = (h->Size & 0xFFF) + 1; height = ((h->Size >> 12) & 0xFFF) + 1; }
    else { width = 1u << ((h->Format >> 20) & 0xF); height = 1u << ((h->Format >> 24) & 0xF); }
    uint32_t xboxFormat = (h->Format >> 8) & 0xFF;
    D3DFORMAT format = (xboxFormat == XFMT_X8R8G8B8 || xboxFormat == XFMT_LIN_X8R8G8B8) ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8;
    g_statRenderTargetCreates++;
    if (FAILED(g_device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &t->texture, NULL))) {
        D3D9Log("[d3d9] render-target texture %ux%u creation failed\n", width, height);
        t->texture = NULL;
        return NULL;
    }
    t->texture->GetSurfaceLevel(0, &t->rtSurface);
    t->renderTarget = true;
    t->dirty = false;
    t->format = h->Format; t->size = h->Size; t->data = h->Data;
    return t;
}

static void CaptureBackBufferInto(void *header) {
    g_statBackBufferCaptures++;
    HostTexture *t = EnsureRenderTargetTexture(header);
    if (t == NULL || g_backBufferSurface == NULL)
        return;
    g_device->StretchRect(g_backBufferSurface, NULL, t->rtSurface, NULL, D3DTEXF_NONE);
}

static void ReleaseDefaultPoolResources(void) {
    ReleaseIndexRing();
    for (int i = 0; i < g_textureCount; i++)
        if (g_textures[i].renderTarget)
            ReleaseHostTexture(&g_textures[i]);
    for (int i = 0; i < g_depthSurfaceCount; i++)
        g_depthSurfaces[i].surface->Release();
    g_depthSurfaceCount = 0;
    if (g_backBufferSurface) { g_backBufferSurface->Release(); g_backBufferSurface = NULL; }
    if (g_mainDepthSurface) { g_mainDepthSurface->Release(); g_mainDepthSurface = NULL; }
}

static IDirect3DSurface9 *g_currentRtSurface = NULL; // the render-target texture surface currently selected, if any
static int g_dumpPassCount = 0;
void D3D9_SetRenderTarget(void *pRenderTarget, void *pDepthStencil) {
    (void)pDepthStencil;
    if (g_device == NULL)
        return;
    if (g_dumpFrame != 0 && g_currentRtSurface != NULL && g_dumpPassCount < 8) {
        char name[64];
        snprintf(name, sizeof(name), "d3d9_dump_rt_pass_%d", g_dumpPassCount++);
        DumpSurface(g_currentRtSurface, name);
    }
    if (g_dumpFrame == 0) g_dumpPassCount = 0;
    g_currentRtSurface = NULL;
    for (DWORD i = 0; i < 4; i++)
        g_device->SetTexture(i, NULL); // a texture must not be sampled while it is the target; re-bound at the next draw
    bool toBackBuffer = (pRenderTarget == NULL || pRenderTarget == &g_dummyBackBuffer || pRenderTarget == &g_dummyRenderTarget);
    if (!toBackBuffer) {
        const XboxSurface *surface = (const XboxSurface*)pRenderTarget;
        HostTexture *t = (surface->Parent != 0) ? EnsureRenderTargetTexture((const void*)(uintptr_t)surface->Parent) : NULL;
        if (t != NULL) {
            D3DSURFACE_DESC desc;
            t->rtSurface->GetDesc(&desc);
            g_device->SetRenderTarget(0, t->rtSurface);
            g_device->SetDepthStencilSurface(GetDepthSurfaceFor(desc.Width, desc.Height));
            g_currentRtSurface = t->rtSurface;
            g_targetWidth = desc.Width; g_targetHeight = desc.Height;
            return;
        }
        D3D9_BackendMissing("D3DDevice_SetRenderTarget(surface with no texture behind it)");
    }
    g_device->SetRenderTarget(0, g_backBufferSurface);
    g_device->SetDepthStencilSurface(g_mainDepthSurface);
    g_targetWidth = g_presentParams.BackBufferWidth; g_targetHeight = g_presentParams.BackBufferHeight;
}

// Everything a programmable draw needs: the translated shader and declaration, the constants, the bound
// streams' host buffers, textures and stage state. Returns false (after counting why) if the draw can't happen.
static bool PrepareShaderDraw(bool bindStreams) {
    if (g_currentVertexShader < 0 || g_currentVertexShader >= g_vertexShaderCount) {
        D3D9_BackendMissing("draw with no vertex shader selected");
        return false;
    }
    TranslatedVertexShader *vs = &g_vertexShaders[g_currentVertexShader];
    if (!vs->attempted)
        BuildVertexShader(vs, g_currentVertexShader);
    if (vs->failed)
        return false;
    BeginSceneIfNeeded();
    g_device->SetVertexDeclaration(vs->decl);
    g_device->SetVertexShader(vs->shader);
    if (g_constantsDirty) {
        g_device->SetVertexShaderConstantF(0, &g_vertexConstants[0][0], 192);
        g_constantsDirty = false;
    }
    ApplyTextureStageState(true); // before the host constants: it computes the linear-texture coordinate scales
    float hostConstants[5][4] = {
        { -1.0f / (float)g_targetWidth, 1.0f / (float)g_targetHeight, 0, 0 },
        { 0, 0, 0, 0 }, // depth mapping, filled in below
        { 0, 0, 0, 0 },
        { g_texCoordScale[0][0], g_texCoordScale[0][1], g_texCoordScale[1][0], g_texCoordScale[1][1] },
        { g_texCoordScale[2][0], g_texCoordScale[2][1], g_texCoordScale[3][0], g_texCoordScale[3][1] },
    };
    {
        // Depth from w, in the screen-affine a + b/w form a z-buffer interpolates exactly: z_ndc = scale * (w - ref) / w.
        // Reversed: ref = far, scale = -near / (far - near) (1 at near, 0 at far); otherwise ref = near, scale = far / (far - near).
        float range = (g_depthClipFar - g_depthClipNear) != 0.0f ? (g_depthClipFar - g_depthClipNear) : 1.0f;
        if (g_reversedDepth) { hostConstants[1][0] = -g_depthClipNear / range; hostConstants[1][1] = g_depthClipFar; }
        else                 { hostConstants[1][0] = g_depthClipFar / range;   hostConstants[1][1] = g_depthClipNear; }
    }
    memcpy(&hostConstants[2][0], (const void*)(0x001119D0u + XRS_FOGSTART * 4), 4);   // the deferred fog render states hold raw floats
    memcpy(&hostConstants[2][1], (const void*)(0x001119D0u + XRS_FOGEND * 4), 4);
    memcpy(&hostConstants[2][2], (const void*)(0x001119D0u + XRS_FOGDENSITY * 4), 4);
    hostConstants[2][3] = (float)XBOX_RENDER_STATE(XRS_FOGTABLEMODE);
    g_device->SetVertexShaderConstantF(200, &hostConstants[0][0], 5);
    if (bindStreams) {
        for (int s = 0; s < 16; s++) {
            if (!(vs->streamsUsed & (1u << s)))
                continue;
            uint32_t size;
            IDirect3DVertexBuffer9 *vb = (g_streams[s] != NULL) ? GetHostVertexBuffer(g_streams[s], &size) : NULL;
            if (vb == NULL && s != 0) {
                // A morph-capable shader selected with no morph streams bound (or a stream that couldn't be
                // uploaded): the NV2A would read whatever was bound last, with zero weights. Feed zeros.
                static IDirect3DVertexBuffer9 *zeroStream = NULL;
                if (zeroStream == NULL && SUCCEEDED(g_device->CreateVertexBuffer(65536, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &zeroStream, NULL))) {
                    void *p = NULL;
                    if (SUCCEEDED(zeroStream->Lock(0, 0, &p, 0))) { memset(p, 0, 65536); zeroStream->Unlock(); }
                }
                vb = zeroStream;
                if (g_streams[s] == NULL) D3D9_BackendMissing("draw with a declared morph stream unbound (zeros fed)");
                else D3D9_BackendMissing("draw with a morph stream that couldn't be uploaded (zeros fed)");
            }
            if (vb == NULL) {
                D3D9_BackendMissing(g_streams[s] == NULL ? "draw with stream 0 unbound" : "draw with a vertex buffer that couldn't be uploaded");
                return false;
            }
            g_device->SetStreamSource(s, vb, 0, g_streamStrides[s] > 0 ? g_streamStrides[s] : 6);
        }
    }
    g_device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);   // the shader's oFog is the fog factor
    g_device->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
    return true;
}

static bool XboxPrimitiveToD3D(uint32_t xboxType, uint32_t vertexCount, D3DPRIMITIVETYPE *type, UINT *primCount) {
    switch (xboxType) {
        case 1: *type = D3DPT_POINTLIST;     *primCount = vertexCount; break;
        case 2: *type = D3DPT_LINELIST;      *primCount = vertexCount / 2; break;
        case 4: *type = D3DPT_LINESTRIP;     *primCount = vertexCount > 1 ? vertexCount - 1 : 0; break;
        case 5: *type = D3DPT_TRIANGLELIST;  *primCount = vertexCount / 3; break;
        case 6: *type = D3DPT_TRIANGLESTRIP; *primCount = vertexCount > 2 ? vertexCount - 2 : 0; break;
        case 7: *type = D3DPT_TRIANGLEFAN;   *primCount = vertexCount > 2 ? vertexCount - 2 : 0; break;
        default: return false;
    }
    return *primCount > 0;
}

// The mesh path: the seam's d3dDrawIndexedVertices issues one triangle strip per primitive, pointing straight
// into the bound index buffer's data (Xbox DrawIndexedVertices takes an index pointer, not an offset).
// Xbox DrawIndexedVertices takes the indices by pointer, so the bound index buffer means nothing to the game
// (the seam only re-binds it when its own cache changes, and the pointer can be into any buffer). The indices
// are therefore copied per draw into a dynamic ring buffer, which also yields the exact vertex range.
static IDirect3DIndexBuffer9 *g_indexRing = NULL;
static uint32_t g_indexRingPos = 0;
#define INDEX_RING_COUNT (1u << 20) // 16-bit indices, 2 MB

static void ReleaseIndexRing(void) {
    if (g_indexRing != NULL) { g_indexRing->Release(); g_indexRing = NULL; }
    g_indexRingPos = 0;
}

void D3D9_DrawIndexedVertices(uint32_t primitiveType, uint32_t vertexCount, const void *pIndexData) {
    g_statDraws++; g_statDrawsIndexed++; g_statVertices += vertexCount;
    if (g_device == NULL)
        return;
    D3DPRIMITIVETYPE type; UINT primCount;
    if (!XboxPrimitiveToD3D(primitiveType, vertexCount, &type, &primCount) || vertexCount > INDEX_RING_COUNT)
        return;
    if (g_indexRing == NULL && FAILED(g_device->CreateIndexBuffer(INDEX_RING_COUNT * 2, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_indexRing, NULL))) {
        D3D9_BackendMissing("DrawIndexedVertices: index ring buffer creation failed");
        return;
    }
    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (g_indexRingPos + vertexCount > INDEX_RING_COUNT) { g_indexRingPos = 0; lockFlags = D3DLOCK_DISCARD; }
    void *dst = NULL;
    if (FAILED(g_indexRing->Lock(g_indexRingPos * 2, vertexCount * 2, &dst, lockFlags)))
        return;
    const uint16_t *srcIndices = (const uint16_t*)pIndexData;
    uint16_t minIndex = 0xFFFF, maxIndex = 0;
    for (uint32_t i = 0; i < vertexCount; i++) {
        uint16_t v = srcIndices[i];
        ((uint16_t*)dst)[i] = v;
        if (v < minIndex) minIndex = v;
        if (v > maxIndex) maxIndex = v;
    }
    g_indexRing->Unlock();
    uint32_t start = g_indexRingPos;
    g_indexRingPos += vertexCount;
    if (!PrepareShaderDraw(true))
        return;
    g_device->SetIndices(g_indexRing);
    g_device->DrawIndexedPrimitive(type, 0, minIndex, maxIndex - minIndex + 1, start, primCount);
}

// Non-indexed draws from a bound stream: the point-sprite overlay (reticle etc.) is the only user.
void D3D9_DrawVertices(uint32_t primitiveType, uint32_t startVertex, uint32_t vertexCount) {
    g_statDraws++; g_statDrawsDirect++; g_statVertices += vertexCount;
    if (g_device == NULL)
        return;
    D3DPRIMITIVETYPE type; UINT primCount;
    if (!XboxPrimitiveToD3D(primitiveType, vertexCount, &type, &primCount))
        return;
    if (!PrepareShaderDraw(true))
        return;
    bool points = (type == D3DPT_POINTLIST);
    if (points) {
        g_device->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
        g_device->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
    }
    g_device->DrawPrimitive(type, startVertex, primCount);
    if (points)
        g_device->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
}

// Multiplies a packed D3DCOLOR by a float4 colour constant (what the immediate-mode shader's MUL oD0 does).
static uint32_t ModulateColour(uint32_t packed, const float *k) {
    uint32_t out = 0;
    for (int c = 0; c < 4; c++) {
        int shift = c * 8; // B, G, R, A
        float f = ((packed >> shift) & 0xFF) * k[(c == 0) ? 2 : (c == 1) ? 1 : (c == 2) ? 0 : 3];
        int v = (int)(f + 0.5f);
        out |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << shift;
    }
    return out;
}

// The immediate-mode quad path: maybeImmediateModeFlush hands us up to 64 quads of {x, y, colour, u, v, pad}
// in screen pixels, and its shader (5 instructions) passes position through unchanged, multiplies the colour
// by constant 103 and forwards the UVs. Drawn here as pre-transformed vertices, quads split into two
// triangles, fixed-function texture stages from the deferred state arrays.
struct RhwVertex { float x, y, z, rhw; uint32_t colour; float u, v; };
static RhwVertex g_rhwVertices[64 * 4];
static uint16_t g_quadIndices[64 * 6];
static bool g_quadIndicesBuilt = false;

static void DrawImmediateQuads(uint32_t vertexCount, const uint8_t *data, uint32_t stride) {
    if (!g_quadIndicesBuilt) {
        for (int q = 0; q < 64; q++) {
            uint16_t b = (uint16_t)(q * 4);
            uint16_t *i = g_quadIndices + q * 6;
            i[0] = b; i[1] = b + 1; i[2] = b + 2; i[3] = b; i[4] = b + 2; i[5] = b + 3;
        }
        g_quadIndicesBuilt = true;
    }
    if (vertexCount > 64 * 4) vertexCount = 64 * 4;
    BeginSceneIfNeeded();
    ApplyTextureStageState(false); // first: it also works out the linear-texture coordinate scale used below
    const float *k = g_vertexConstants[103];
    for (uint32_t v = 0; v < vertexCount; v++) {
        const float *src = (const float*)(data + (size_t)v * stride);
        RhwVertex *d = &g_rhwVertices[v];
        d->x = src[0] - 0.5f; // D3D9 samples pixel centres at +0.5; the game already biases by -1/32
        d->y = src[1] - 0.5f;
        d->z = g_reversedDepth ? 0.0f : 1.0f; // the shader writes constant 102's z (the far-plane sentinel) for z and w
        d->rhw = 1.0f;
        d->colour = ModulateColour(*(const uint32_t*)&src[2], k);
        d->u = src[3] * g_texCoordScale[0][0];
        d->v = src[4] * g_texCoordScale[0][1];
    }
    g_device->SetVertexShader(NULL);
    g_device->SetPixelShader(NULL);
    g_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    g_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, vertexCount, (vertexCount / 4) * 2, g_quadIndices, D3DFMT_INDEX16, g_rhwVertices, sizeof(RhwVertex));
}

void D3D9_DrawVerticesUP(uint32_t primitiveType, uint32_t vertexCount, void *pVertexData, uint32_t stride) {
    g_statDraws++; g_statDrawsImmediate++; g_statVertices += vertexCount;
    if (g_device == NULL)
        return;
    if (primitiveType == 8 && stride == 0x18) { // X_D3DPT_QUADLIST from maybeImmediateModeFlush
        DrawImmediateQuads(vertexCount, (const uint8_t*)pVertexData, stride);
        return;
    }
    // Shards (glass shatter): a triangle list in the mesh vertex layout, drawn with the current mesh shader.
    D3DPRIMITIVETYPE type; UINT primCount;
    if (!XboxPrimitiveToD3D(primitiveType, vertexCount, &type, &primCount))
        return;
    if (!PrepareShaderDraw(false))
        return;
    g_device->DrawPrimitiveUP(type, primCount, pVertexData, stride);
}


// ---------------------------------------------------------------------------------------------------------------
// Surfaces. The seam and the video decoder expect Xbox-layout surface objects (Common, Data, Lock, Format,
// Size, Parent), so hand those out. There is no CPU-visible backbuffer here, so the dummy backbuffer's Data
// word is 0: psiBlurScreen (which reads the live backbuffer through the physical alias) gets a harmless read
// of Xbox physical page 0 under CXBX's memory map rather than real pixels - that path becomes a GPU copy in a
// later checkpoint.
// ---------------------------------------------------------------------------------------------------------------

static XboxSurface *g_allocatedSurfaces[64];
static int g_allocatedSurfaceCount = 0;

uint32_t *D3D9_GetBackBuffer2(int32_t backBufferIndex) {
    (void)backBufferIndex;
    return (uint32_t*)&g_dummyBackBuffer;
}

// Same construction as D3D8's own GetSurfaceLevel2 (FUN_0010ba80): a fresh surface object aliasing the
// texture's level-0 data, with the top nibble of Data stripped, and the caller owns one reference.
void *D3D9_GetSurfaceLevel2(void *pTexture, uint32_t level) {
    (void)level; // only level 0 is ever asked for
    const uint32_t *header = (const uint32_t*)pTexture;
    XboxSurface *s = (XboxSurface*)malloc(sizeof(XboxSurface));
    if (s == NULL)
        return NULL;
    s->Common = XBOX_SURFACE_COMMON;
    s->Data = header[1] & 0x0FFFFFFFu;
    s->Lock = 0;
    s->Format = header[3];
    s->Size = header[4];
    s->Parent = (uint32_t)(uintptr_t)pTexture;
    if (g_allocatedSurfaceCount < (int)(sizeof(g_allocatedSurfaces) / sizeof(g_allocatedSurfaces[0])))
        g_allocatedSurfaces[g_allocatedSurfaceCount++] = s;
    return s;
}

void *D3D9_GetRenderTarget2(void) {
    return &g_dummyRenderTarget;
}

void *D3D9_GetDepthStencilSurface2(void) {
    return &g_dummyDepthStencil;
}

// Xbox refcount semantics on the Common word's low 16 bits; surface objects this backend allocated are freed
// on their last release, everything else (texture headers in Gfx's table, the static dummies) is just counted.
uint32_t D3D9_ResourceRelease(void *pResource) {
    if (pResource == NULL)
        return 0;
    uint32_t *common = (uint32_t*)pResource;
    uint32_t refCount = *common & 0xFFFFu;
    if (refCount > 1) {
        *common = *common - 1;
        return refCount - 1;
    }
    for (int i = 0; i < g_allocatedSurfaceCount; i++) {
        if (g_allocatedSurfaces[i] == pResource) {
            free(pResource);
            g_allocatedSurfaces[i] = g_allocatedSurfaces[--g_allocatedSurfaceCount];
            return 0;
        }
    }
    return 0;
}

void D3D9_BlockUntilNotBusy(void *pResource) {
    (void)pResource; // no GPU-side ownership of CPU memory here
}
