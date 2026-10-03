#include "EaglFont.h"

#include "RenderContext.h"

#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGLFont, the FONT driver (docs/driving/eagl.md 4.8): every code entry point in 0x000ee190..0x000eec70 except
// 0x000eea10, the XDK's inline D3DDevice_End compiled into this unit, which the seam replaces (d3d8Entries.inc).
//
// A glyph is drawn as one immediate-mode quad (the caller's D3DDevice_Begin(8) is in FONTEAGL_startdraw, the End
// is the driver table's third entry): per corner D3DDevice_SetVertexDataColor(3, colour),
// SetVertexData2f(9, u, v) and SetVertexData4f(-1, x, y, z, w), the position completing the vertex. The
// arithmetic is the original's x87, in double in its order with its float roundings (the four corners reuse
// rounded and unrounded intermediates exactly as the listing does). FONTEAGL_startdraw copies the font's drawing
// parameters into globals the draw functions read:
//   0x002400b8/bc  the render context's screen-space offset    0x002400c8/cc  texture offset (font +0x30, +0x34)
//   0x002400d0/d4  texture scale u, v (font +0x7c, +0x78)       0x002400d8/dc  position scale (font +0x38, +0x3c)
//   0x002400e0/e4  z and w of every vertex (font +0x28, +0x2c)  0x001cd110     the colour (font +0x20)
// and sets up the shared GeoPrimState at 0x002400f8 (FONTEAGL_createfont gives it its fixed settings).
//
// FONTEAGL_createfont's SEH frame (handler 0x00154efd, for the TAR and shader constructors) is left out.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

namespace {

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

inline float &F32(uint32_t address) {
    return *(float *)(uintptr_t)address;
}

inline double D(uint32_t address) {   // a float in memory, as FLD/FADD/FMUL read it
    return (double)F32(address);
}

template <typename T> inline T At(const void *p, int offset) {
    T v;
    memcpy(&v, (const uint8_t *)p + offset, sizeof(T));
    return v;
}

template <typename T> inline void Put(void *p, int offset, T v) {
    memcpy((uint8_t *)p + offset, &v, sizeof(T));
}

// Constants in .rdata
const uint32_t kHalf = 0x00189eb0, kOne = 0x00189de8, kZero = 0x00189dec;

// The drawing parameters (see above)
const uint32_t kOffsetX = 0x002400b8, kOffsetY = 0x002400bc, kVertexShader = 0x002400c0,
               kPixelShader = 0x002400c4, kTexOffsetU = 0x002400c8, kTexOffsetV = 0x002400cc,
               kTexScaleU = 0x002400d0, kTexScaleV = 0x002400d4, kScaleX = 0x002400d8, kScaleY = 0x002400dc,
               kZ = 0x002400e0, kW = 0x002400e4, kColour = 0x001cd110;

EAGL::GeoPrimStateExtension *const gFontState = (EAGL::GeoPrimStateExtension *)0x002400f8u;

inline void *DeviceGet() {
    return ((void *(*)())0x000e8a40)();   // EAGL::Device::Get
}

inline void *EaglMalloc(uint32_t size, const char *name) {
    return (*(void *(**)(uint32_t, const char *))0x001caf68u)(size, name);
}

inline void EaglFree(void *p, uint32_t size) {
    (*(void (**)(void *, uint32_t))0x001caf6cu)(p, size);
}

inline void VertexColour() {
    ((void (__stdcall *)(uint32_t, uint32_t))0x0016b9d0)(3, U32(kColour));   // D3DDevice_SetVertexDataColor
}

inline void VertexTexCoord(float u, float v) {
    ((void (__stdcall *)(uint32_t, float, float))0x0016b930)(9, u, v);       // D3DDevice_SetVertexData2f
}

// z and w go as the globals' bits
inline void VertexPosition(float x, float y) {
    uint32_t w = U32(kW), z = U32(kZ);
    ((void (__stdcall *)(uint32_t, float, float, uint32_t, uint32_t))0x0016b970)(0xffffffffu, x, y, z, w);
}

// One glyph's quad at (x, y), as both FONTEAGL_draw and FONTEAGL_drawarray compute it. A glyph: +2/+3 width and
// height (u8), +4/+6 position in the texture (u16), +9/+0xa x and y offset (s8).
void DrawGlyph(const uint8_t *glyph, float x, float y) {
    float tu = (float)((double)(int32_t)At<uint16_t>(glyph, 4) - D(kHalf));
    float tv = (float)((double)(int32_t)At<uint16_t>(glyph, 6) - D(kHalf));
    float xo = (float)(((double)(int32_t)(int8_t)glyph[9] + D(kOffsetX)) + D(kHalf));
    float yo = (float)(((double)(int32_t)(int8_t)glyph[0xa] + D(kOffsetY)) + D(kHalf));
    double width1 = (double)(int32_t)glyph[2] + D(kOne);
    float w1 = (float)width1;
    float h1 = (float)((double)(int32_t)glyph[3] + D(kOne));
    // FCOMP of the unrounded width + 1 against 0.0f, TEST AH,0x44, JNP: no quad only when it is equal (ordered)
    if (width1 == D(kZero))
        return;

    // top left
    VertexColour();
    {
        float v = (float)((D(kTexOffsetV) + (double)tv) * D(kTexScaleV));
        float u = (float)((D(kTexOffsetU) + (double)tu) * D(kTexScaleU));
        VertexTexCoord(u, v);
    }
    {
        double py = D(kScaleY) * (double)yo + (double)y;
        float fy = (float)py;
        float fx = (float)(D(kScaleX) * (double)xo + (double)x);
        VertexPosition(fx, fy);
    }

    // top right
    VertexColour();
    float tu2 = (float)((double)w1 + (double)tu);
    {
        float v = (float)((D(kTexOffsetV) + (double)tv) * D(kTexScaleV));
        float u = (float)(((double)tu2 + D(kTexOffsetU)) * D(kTexScaleU));
        VertexTexCoord(u, v);
    }
    float x2 = (float)((double)w1 + (double)xo);
    {
        float fy = (float)(D(kScaleY) * (double)yo + (double)y);
        float fx = (float)((double)x2 * D(kScaleX) + (double)x);
        VertexPosition(fx, fy);
    }

    // bottom right
    VertexColour();
    double tv2Unrounded = (double)h1 + (double)tv;
    float tv2 = (float)tv2Unrounded;
    {
        float v = (float)((tv2Unrounded + D(kTexOffsetV)) * D(kTexScaleV));   // the unrounded sum (FST, then FADD)
        float u = (float)(((double)tu2 + D(kTexOffsetU)) * D(kTexScaleU));
        VertexTexCoord(u, v);
    }
    float y2 = (float)((double)h1 + (double)yo);
    {
        float fy = (float)((double)y2 * D(kScaleY) + (double)y);
        float fx = (float)((double)x2 * D(kScaleX) + (double)x);
        VertexPosition(fx, fy);
    }

    // bottom left
    VertexColour();
    {
        float v = (float)(((double)tv2 + D(kTexOffsetV)) * D(kTexScaleV));
        float u = (float)((D(kTexOffsetU) + (double)tu) * D(kTexScaleU));
        VertexTexCoord(u, v);
    }
    {
        float fy = (float)((double)y2 * D(kScaleY) + (double)y);
        float fx = (float)(D(kScaleX) * (double)xo + (double)x);
        VertexPosition(fx, fy);
    }
}

}  // namespace

// The batch hook: count records of {glyph, x, y} (12 bytes). The font is not read.
// FUNC_AT(0x000ee190)
void* FONTEAGL_drawarray(const uint8_t *, const uint8_t *batch, int count) {
    DeviceGet();
    if (count > 0) {
        for (int i = count; i != 0; i--) {
            const uint8_t *glyph = At<const uint8_t *>(batch, 0);
            DrawGlyph(glyph, At<float>(batch, 4), At<float>(batch, 8));
            batch += 0xc;
        }
    }
    return DeviceGet();
}

// FUNC_AT(0x000ee490)
void* FONTEAGL_draw(const uint8_t *, const uint8_t *glyph, float x, float y) {
    DeviceGet();
    DrawGlyph(glyph, x, y);
    return DeviceGet();
}

// A font's TAR over its shape (font +0x1c, an offset from the font), the texture scale (1 / the shape's size when
// its flag 0x2000 is set, else 1), the shared GeoPrimState's fixed settings, and the font shaders (made once).
// FUNC_AT(0x000ee760)
void FONTEAGL_createfont(uint8_t *font) {
    uint8_t *shape = font + At<int32_t>(font, 0x1c);
    Put<uint8_t *>(font, 0x74, shape);
    void *tar = EaglMalloc(0x4c, (const char *)0x0018a348u);   // "EAGL::TAR new"
    if (tar != NULL)
        tar = ((void *(__fastcall *)(void *, int, void *))0x000eca20)(tar, 0, shape);   // EAGL::TAR::TAR
    else
        tar = NULL;
    Put<void *>(font, 0x70, tar);
    Put<uint32_t>(font, 0x30, 0);
    Put<uint32_t>(font, 0x34, 0);
    if ((At<uint32_t>(shape, 0xc) & 0x2000) == 0) {
        Put<uint32_t>(font, 0x7c, 0x3f800000u);   // 1.0f
        Put<uint32_t>(font, 0x78, 0x3f800000u);
    } else {
        Put<float>(font, 0x7c, (float)(D(kOne) / (double)(int32_t)At<int16_t>(shape, 4)));
        Put<float>(font, 0x78, (float)(D(kOne) / (double)(int32_t)At<int16_t>(shape, 6)));
    }
    gFontState->SetPrimitiveType(5);
    gFontState->SetAlphaBlendMode(1);
    gFontState->SetAlphaTestMethod(0x204);
    gFontState->SetCullEnable(false);
    gFontState->SetDepthTestMethod(0x203);
    gFontState->SetShading(1);                  // 0x000eec90 (Ghidra: SetTransparencyMethod)
    gFontState->SetTextureEnable(true);
    gFontState->SetTextureCoordType(0xffffffffu);
    gFontState->SetTransparencyMethod(1);
    if (U32(kVertexShader) == 0) {
        void *shader = EaglMalloc(4, (const char *)0x001cd144u);   // "EAGL::VertexShader new"
        if (shader != NULL)
            shader = ((void *(__fastcall *)(void *, int, const void *, const void *))0x000f6fa0)(
                shader, 0, (const void *)0x001ccfd8u, (const void *)0x001ccfecu);
        else
            shader = NULL;
        U32(kVertexShader) = (uint32_t)(uintptr_t)shader;
    }
    if (U32(kPixelShader) == 0) {
        void *shader = EaglMalloc(4, (const char *)0x001cd15cu);   // "EAGL::PixelShader new"
        if (shader != NULL)
            shader = ((void *(__fastcall *)(void *, int, const void *))0x000f6ff0)(shader, 0,
                                                                                  (const void *)0x001cd020u);
        else
            shader = NULL;
        U32(kPixelShader) = (uint32_t)(uintptr_t)shader;
    }
}

// The drawing parameters into the globals, the font shaders, the TAR's filter (2 when font +0x48 is 1, else 1),
// depth (font +0x4c: 0 no writes and ALWAYS, 1 writes and LEQUAL, else left), apply, bind, and Begin(quads).
// FUNC_AT(0x000ee920)
void FONTEAGL_startdraw(const uint8_t *font) {
    void *device = DeviceGet();
    void *context = ((void *(__fastcall *)(void *, int))0x000e89e0)(device, 0);   // Device::GetCurrentRenderContext
    ((EAGL::RenderContextExtension *)context)->GetScreenSpaceOffset((float *)(uintptr_t)kOffsetX,
                                                                    (float *)(uintptr_t)kOffsetY);
    U32(kTexOffsetU) = At<uint32_t>(font, 0x30);
    U32(kTexOffsetV) = At<uint32_t>(font, 0x34);
    U32(kTexScaleU) = At<uint32_t>(font, 0x7c);
    U32(kTexScaleV) = At<uint32_t>(font, 0x78);
    U32(kColour) = At<uint32_t>(font, 0x20);
    U32(kScaleX) = At<uint32_t>(font, 0x38);
    U32(kScaleY) = At<uint32_t>(font, 0x3c);
    U32(kZ) = At<uint32_t>(font, 0x28);
    U32(kW) = At<uint32_t>(font, 0x2c);
    ((void (*)(uint32_t))0x000f4350)(U32(kVertexShader));   // the SetVertexShader wrapper
    ((void (*)(uint32_t))0x000f4380)(U32(kPixelShader));    // the SetPixelShader wrapper
    uint8_t *tar = At<uint8_t *>(font, 0x70);
    Put<uint32_t>(tar, 0x14, (At<int32_t>(font, 0x48) == 1 ? 1u : 0u) + 1);
    int32_t depthMode = At<int32_t>(font, 0x4c);
    if (depthMode == 0) {
        gFontState->SetZWritesEnable(false);
        gFontState->SetDepthTestMethod(0x207);
    } else if (depthMode == 1) {
        gFontState->SetZWritesEnable(true);
        gFontState->SetDepthTestMethod(0x203);
    }
    gFontState->Apply();
    ((void (__fastcall *)(void *, int))0x000eb3f0)(At<void *>(font, 0x70), 0);   // bind the TAR
    ((void (__stdcall *)(uint32_t))0x0016ba20)(8);   // D3DDevice_Begin(D3DPT_QUADLIST)
}

// FUNC_AT(0x000eea20)
void FONTEAGL_destroyfont(uint8_t *font) {
    void *tar = At<void *>(font, 0x70);
    if (tar != NULL) {
        ((void (__fastcall *)(void *, int))0x000ecab0)(tar, 0);   // EAGL::TAR::~TAR
        EaglFree(tar, 0x4c);
    }
    Put<void *>(font, 0x70, NULL);
}

// ---- 0x000eea50: D3DDevice_SetRenderState(state = ESI, value = EDI) inlined. The simple states (below 0x5c) go
// through SetRenderState_Simple with their push-buffer method from D3D8's table at 0x0018e888 and are written into
// the render-state table; the deferred ones (below 0x88) only set their dirty bits (table 0x0018e668) and are
// written; the complex ones each have their entry point; anything else is ignored.

static void EAGLFont_SetRenderState(int32_t state, uint32_t value) {
    if (state < 0x5c) {
        ((void (__fastcall *)(uint32_t, uint32_t))0x001673e0)(*(const uint32_t *)(uintptr_t)(0x0018e888u + state * 4),
                                                             value);
        U32(0x00175628u + state * 4) = value;
        return;
    }
    if (state < 0x88) {
        U32(0x00175424) = U32(0x00175424) | *(const uint32_t *)(uintptr_t)(0x0018e668u + state * 4);
        U32(0x00175628u + state * 4) = value;
        return;
    }
    uint32_t entry;
    switch (state) {
    case 0x88: entry = 0x001673b0; break;   // PSTextureModes
    case 0x89: entry = 0x00167bf0; break;   // VertexBlend
    case 0x8a: entry = 0x00167760; break;   // FogColor
    case 0x8b: entry = 0x00167ad0; break;   // FillMode
    case 0x8c: entry = 0x00167b20; break;   // BackFillMode
    case 0x8d: entry = 0x00167b80; break;   // TwoSidedLighting
    case 0x8e: entry = 0x00167860; break;   // NormalizeNormals
    case 0x8f: entry = 0x001687f0; break;   // ZEnable
    case 0x90: entry = 0x00168880; break;   // StencilEnable
    case 0x91: entry = 0x00168910; break;   // StencilFail
    case 0x93: entry = 0x001677b0; break;   // CullMode (tested before FrontFace, as the original)
    case 0x92: entry = 0x00167820; break;   // FrontFace
    case 0x94: entry = 0x001678a0; break;   // TextureFactor
    case 0x95: entry = 0x001679f0; break;   // ZBias
    case 0x96: entry = 0x00167a70; break;   // LogicOp
    case 0x97: entry = 0x001676e0; break;   // EdgeAntiAlias
    case 0x98: entry = 0x00168b70; break;   // MultiSampleAntiAlias
    case 0x99: entry = 0x00168bf0; break;   // MultiSampleMask
    case 0x9a: entry = 0x00168af0; break;   // MultiSampleMode
    case 0x9b: entry = 0x00168b30; break;   // MultiSampleRenderTargetMode
    case 0x9c: entry = 0x00167720; break;   // ShadowFunc
    case 0x9d: entry = 0x00167900; break;   // LineWidth
    case 0x9e: entry = 0x00168c40; break;   // SampleAlpha
    case 0x9f: entry = 0x00167970; break;   // Dxt1NoiseEnable
    case 0xa0: entry = 0x00168980; break;   // YuvEnable
    case 0xa1: entry = 0x001689b0; break;   // OcclusionCullEnable
    case 0xa2: entry = 0x00168a20; break;   // StencilCullEnable
    case 0xa3: entry = 0x00168a90; break;   // RopZCmpAlwaysRead
    case 0xa4: entry = 0x00168ab0; break;   // RopZRead
    case 0xa5: entry = 0x00168ad0; break;   // DoNotCullUncompressed
    default: return;
    }
    ((void (__stdcall *)(uint32_t))(uintptr_t)entry)(value);
}

// FUNC_AT(0x000eea50)
__declspec(naked) void EAGLFont_SetRenderStateEsiEdi() {
    __asm {
        push edi
        push esi
        call EAGLFont_SetRenderState
        add esp, 8
        ret
    }
}

// FUNC_AT(0x000eec50)
EAGL::GeoPrimStateCopy* EAGL::GeoPrimStateCopy::Assign(const GeoPrimState *other) {
    memcpy(this, other, 0x4c);
    return this;
}
