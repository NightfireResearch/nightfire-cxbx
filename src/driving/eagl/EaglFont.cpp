#include "EaglFont.h"

#include "D3D8State.h"
#include "RenderContext.h"
#include "RenderMethod.h"
#include "Tar.h"
#include "View.h"

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
// parameters into the driver's statics (DrawParameters, and the colour) the draw functions read, and sets up the
// shared GeoPrimState at 0x002400f8 (FONTEAGL_createfont gives it its fixed settings).
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

// The driver's statics at 0x002400b8: what FONTEAGL_startdraw copies from the font, and the font shaders
struct DrawParameters {
    float offsetX;                   // +0x00 the render context's screen-space offset
    float offsetY;                   // +0x04
    EAGL::VertexShader *vertexShader;  // +0x08 made once, by FONTEAGL_createfont
    EAGL::PixelShader *pixelShader;  // +0x0c
    float texOffsetU;                // +0x10 font +0x30
    float texOffsetV;                // +0x14 font +0x34
    float texScaleU;                 // +0x18 font +0x7c
    float texScaleV;                 // +0x1c font +0x78
    float scaleX;                    // +0x20 font +0x38
    float scaleY;                    // +0x24 font +0x3c
    uint32_t z;                      // +0x28 font +0x28 (float bits)
    uint32_t w;                      // +0x2c font +0x2c
};
static_assert(sizeof(DrawParameters) == 0x30, "the driver's statics run to 0x002400e8");

#define Params (*(DrawParameters *)0x002400b8)
#define VertexColour U32_AT(0x001cd110)                     // font +0x20
#define FontState (*(EAGL::GeoPrimStateExtension *)0x002400f8)

// The font shaders' declaration and programs, in the XBE
#define FontShaderDeclaration ((const void *)0x001ccfd8)
#define FontVertexShaderFunction ((const void *)0x001ccfec)
#define FontPixelShaderDefinition ((const void *)0x001cd020)

#define EaglMalloc (*(void *(**)(uint32_t size, const char *name))0x001caf68)
#define EaglFree (*(void (**)(void *pointer, uint32_t size))0x001caf6c)
#define NameTARNew ((const char *)0x0018a348)               // "EAGL::TAR new"
#define NameVertexShaderNew ((const char *)0x001cd144)      // "EAGL::VertexShader new"
#define NamePixelShaderNew ((const char *)0x001cd15c)       // "EAGL::PixelShader new"

#define D3DDevice_SetVertexDataColor ((void (__stdcall *)(uint32_t reg, uint32_t colour))0x0016b9d0)
#define D3DDevice_SetVertexData2f ((void (__stdcall *)(uint32_t reg, float a, float b))0x0016b930)
#define D3DDevice_SetVertexData4f ((void (__stdcall *)(uint32_t reg, float x, float y, uint32_t z, uint32_t w))0x0016b970)
#define D3DDevice_Begin ((void (__stdcall *)(uint32_t primitiveType))0x0016ba20)

enum { kVertexDiffuse = 3, kVertexTexCoord0 = 9, kVertexPosition = 0xffffffff };   // D3DVSDE_*
enum { kDepthLessEqual = 0x203, kDepthAlways = 0x207, kAlphaGreaterEqual = 0x204 };   // OpenGL numbering
enum { kQuadList = 8 };

void Colour() {
    D3DDevice_SetVertexDataColor(kVertexDiffuse, VertexColour);
}

void TexCoord(float u, float v) {
    D3DDevice_SetVertexData2f(kVertexTexCoord0, u, v);
}

// z and w go as the statics' bits
void Position(float x, float y) {
    D3DDevice_SetVertexData4f(kVertexPosition, x, y, Params.z, Params.w);
}

// One glyph's quad at (x, y), as both FONTEAGL_draw and FONTEAGL_drawarray compute it.
void DrawGlyph(const FNTXGlyph *glyph, float x, float y) {
    float tu = glyph->texX - 0.5f;
    float tv = glyph->texY - 0.5f;
    float xo = float(double(glyph->xOffset) + Params.offsetX + 0.5);
    float yo = float(double(glyph->yOffset) + Params.offsetY + 0.5);
    double width1 = glyph->width + 1.0;
    float w1 = float(width1);
    float h1 = glyph->height + 1.0f;
    // FCOMP of the unrounded width + 1 against 0.0f, TEST AH,0x44, JNP: no quad only when it is equal (ordered)
    if (width1 == 0.0)
        return;

    // top left
    Colour();
    {
        float v = float((double(Params.texOffsetV) + tv) * Params.texScaleV);
        float u = float((double(Params.texOffsetU) + tu) * Params.texScaleU);
        TexCoord(u, v);
    }
    {
        float fy = float(double(Params.scaleY) * yo + y);
        float fx = float(double(Params.scaleX) * xo + x);
        Position(fx, fy);
    }

    // top right
    Colour();
    float tu2 = w1 + tu;
    {
        float v = float((double(Params.texOffsetV) + tv) * Params.texScaleV);
        float u = float((double(tu2) + Params.texOffsetU) * Params.texScaleU);
        TexCoord(u, v);
    }
    float x2 = w1 + xo;
    {
        float fy = float(double(Params.scaleY) * yo + y);
        float fx = float(double(x2) * Params.scaleX + x);
        Position(fx, fy);
    }

    // bottom right
    Colour();
    double tv2Unrounded = double(h1) + tv;
    float tv2 = float(tv2Unrounded);
    {
        float v = float((tv2Unrounded + Params.texOffsetV) * Params.texScaleV);   // the unrounded sum (FST, then FADD)
        float u = float((double(tu2) + Params.texOffsetU) * Params.texScaleU);
        TexCoord(u, v);
    }
    float y2 = h1 + yo;
    {
        float fy = float(double(y2) * Params.scaleY + y);
        float fx = float(double(x2) * Params.scaleX + x);
        Position(fx, fy);
    }

    // bottom left
    Colour();
    {
        float v = float((double(tv2) + Params.texOffsetV) * Params.texScaleV);
        float u = float((double(Params.texOffsetU) + tu) * Params.texScaleU);
        TexCoord(u, v);
    }
    {
        float fy = float(double(y2) * Params.scaleY + y);
        float fx = float(double(Params.scaleX) * xo + x);
        Position(fx, fy);
    }
}

}  // namespace

// The batch hook: count records of {glyph, x, y}. The font is not read.
// FUNC_AT(0x000ee190)
void* FONTEAGL_drawarray(const FNTXFont *, const FNTXBatchEntry *batch, int count) {
    EAGL::Device::Get();
    if (count > 0) {
        for (int i = count; i != 0; i--, batch++)
            DrawGlyph(batch->glyph, batch->x, batch->y);
    }
    return EAGL::Device::Get();
}

// FUNC_AT(0x000ee490)
void* FONTEAGL_draw(const FNTXFont *, const FNTXGlyph *glyph, float x, float y) {
    EAGL::Device::Get();
    DrawGlyph(glyph, x, y);
    return EAGL::Device::Get();
}

// A font's TAR over its shape, the texture scale (1 / the shape's size when its flag 0x2000 is set, else 1), the
// shared GeoPrimState's fixed settings, and the font shaders (made once).
// FUNC_AT(0x000ee760)
void FONTEAGL_createfont(FNTXFont *font) {
    uint8_t *shape = (uint8_t *)font + font->shapeOffset;
    font->shape = shape;
    EAGL::TAR *tar = (EAGL::TAR *)EaglMalloc(sizeof(EAGL::TAR), NameTARNew);
    tar = tar != NULL ? tar->Construct(shape) : NULL;
    font->tar = tar;
    font->texOffsetU = 0.0f;
    font->texOffsetV = 0.0f;
    const EAGL::ShapeImage *image = (const EAGL::ShapeImage *)shape;
    if ((image->flags & EAGL::kShapeLinear) == 0) {
        font->texScaleU = 1.0f;
        font->texScaleV = 1.0f;
    } else {
        font->texScaleU = 1.0f / image->width;
        font->texScaleV = 1.0f / image->height;
    }
    FontState.SetPrimitiveType(5);
    FontState.SetAlphaBlendMode(1);
    FontState.SetAlphaTestMethod(kAlphaGreaterEqual);
    FontState.SetCullEnable(false);
    FontState.SetDepthTestMethod(kDepthLessEqual);
    FontState.SetShading(1);                  // 0x000eec90 (Ghidra: SetTransparencyMethod)
    FontState.SetTextureEnable(true);
    FontState.SetTextureCoordType(0xffffffff);
    FontState.SetTransparencyMethod(1);
    if (Params.vertexShader == NULL) {
        EAGL::VertexShader *shader = (EAGL::VertexShader *)EaglMalloc(sizeof(EAGL::VertexShader), NameVertexShaderNew);
        shader = shader != NULL ? shader->Construct(FontShaderDeclaration, FontVertexShaderFunction) : NULL;
        Params.vertexShader = shader;
    }
    if (Params.pixelShader == NULL) {
        EAGL::PixelShader *shader = (EAGL::PixelShader *)EaglMalloc(sizeof(EAGL::PixelShader), NamePixelShaderNew);
        shader = shader != NULL ? shader->Construct(FontPixelShaderDefinition) : NULL;
        Params.pixelShader = shader;
    }
}

// The drawing parameters into the statics, the font shaders, the TAR's filter, depth, apply, bind, and
// Begin(quads).
// FUNC_AT(0x000ee920)
void FONTEAGL_startdraw(const FNTXFont *font) {
    EAGL::RenderContext *context = EAGL::Device::Get()->GetCurrentRenderContext();
    context->Extension()->GetScreenSpaceOffset(&Params.offsetX, &Params.offsetY);
    Params.texOffsetU = font->texOffsetU;
    Params.texOffsetV = font->texOffsetV;
    Params.texScaleU = font->texScaleU;
    Params.texScaleV = font->texScaleV;
    VertexColour = font->colour;
    Params.scaleX = font->scaleX;
    Params.scaleY = font->scaleY;
    Params.z = font->z;
    Params.w = font->w;
    EAGL_SetVertexShader(Params.vertexShader);
    EAGL_SetPixelShader(Params.pixelShader);
    font->tar->filter = (font->filterMode == 1 ? 1 : 0) + 1;
    if (font->depthMode == 0) {
        FontState.SetZWritesEnable(false);
        FontState.SetDepthTestMethod(kDepthAlways);
    } else if (font->depthMode == 1) {
        FontState.SetZWritesEnable(true);
        FontState.SetDepthTestMethod(kDepthLessEqual);
    }
    FontState.Apply();
    font->tar->Use();
    D3DDevice_Begin(kQuadList);
}

// FUNC_AT(0x000eea20)
void FONTEAGL_destroyfont(FNTXFont *font) {
    EAGL::TAR *tar = font->tar;
    if (tar != NULL) {
        tar->Destruct();
        EaglFree(tar, sizeof(EAGL::TAR));
    }
    font->tar = NULL;
}

// ---- 0x000eea50: D3DDevice_SetRenderState(state = ESI, value = EDI) inlined. The simple states (below 0x5c) go
// through SetRenderState_Simple with their push-buffer method from D3D8's table and are written into the
// render-state table; the deferred ones (below 0x88) only set their dirty bits (from D3D8's table) and are
// written; the complex ones each have their entry point; anything else is ignored.

#define D3DSimpleStateMethods ((const uint32_t *)0x0018e888)   // the push-buffer method per simple state
#define D3DDeferredStateDirty ((const uint32_t *)0x0018e668)   // the dirty bits per deferred state

typedef void (__stdcall *RenderStateEntry)(uint32_t value);

static void EAGLFont_SetRenderState(int32_t state, uint32_t value) {
    if (state < 0x5c) {
        D3DDevice_SetRenderState_Simple(D3DSimpleStateMethods[state], value);
        D3DRenderState[state] = value;
        return;
    }
    if (state < 0x88) {
        D3DDirtyFlags = D3DDirtyFlags | D3DDeferredStateDirty[state];
        D3DRenderState[state] = value;
        return;
    }
    uintptr_t entry;
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
    ((RenderStateEntry)entry)(value);
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
    *static_cast<GeoPrimState *>(this) = *other;
    return this;
}
