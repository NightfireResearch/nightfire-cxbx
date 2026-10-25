#pragma fp_contract(off)

#include "Gallery.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bit>

#include "EffectsGallery.h"               // TheEffectsGallery
#include "FrontEndSystem.h"               // GSystem
#include "../eagl/EaglFont.h"             // FNTXFont
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GeoPrimState.h"
#include "../eagl/Realgraph.h"            // FONT_, SHAPE_name, ShapeFile
#include "../eagl/RenderMethod.h"         // GeoPrimParam
#include "../eagl/Tar.h"
#include "../engine/StaticInit.h"         // DestroyStatic_0023f330
#include "../engine/UFileLoader.h"
#include "../engine/UGroup.h"
#include "../platform/RealMath.h"         // VU0_
#include "../platform/RealMemory.h"       // MEM_free
#include "../platform/X87.h"              // Ftol
#include "../render/Draw.h"
#include "../render/Materials.h"          // USimpleMaterial, SimpleRequests
#include "../render/Renderer.h"           // fgRenderer
#include "../../helpers.h"
#include "../platform/CRuntime.h"

// ---------------------------------------------------------------------------------------------------------------
// GGallery (0x000d5800-0x000d6f20, with GET_Canvas and CULL_SetCanvas in GFX's code at 0x000d3850 and 0x000d3470),
// ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

// LINE_Draw2DStrip's material, a function-local static whose destructor (StaticInit.cpp's DestroyStatic_0023f330)
// is registered on first use; bit 0 of the guard says it was made
// XBE_GLOBAL(0x0023f330, 0x60)
alignas(16) USimpleMaterial LineMaterial;
// XBE_GLOBAL(0x0023f390, 0x4)
uint32_t LineMaterialMade;

// ---- constants

// A texture rectangle is pulled in at each edge by half a texel of a 256-texel texture
constexpr float kTexelInset = 1.0f / 512.0f;
// The draws' frame is 480 lines high
constexpr float kPerLine = 1.0f / 480.0f;
// On a 4:3 screen text is drawn at 0.9 of its size, moved in by 5% of the screen and down by one line in 48
constexpr float kSafeScale = 0.9f;
constexpr float kSafeMargin = -0.050000012f;
constexpr float kLineOf48 = 1.0f / 48.0f;
// On a 16:9 screen in the wide video mode, x is pulled a quarter of the way towards the frame's centre
constexpr float kFrameCentre = 320.0f;
constexpr float kPerHalfFrame = 1.0f / 320.0f;
constexpr float kWideSqueeze = 80.0f;
constexpr float kWideFontScale = 0.75f;
constexpr float kThousandths = 0.001f;
static_assert(std::bit_cast<uint32_t>(kTexelInset) == 0x3b000000 && std::bit_cast<uint32_t>(kPerLine) == 0x3b088889 &&
              std::bit_cast<uint32_t>(kSafeScale) == 0x3f666666 && std::bit_cast<uint32_t>(kSafeMargin) == 0xbd4cccd0 &&
              std::bit_cast<uint32_t>(kLineOf48) == 0x3caaaaab &&
              std::bit_cast<uint32_t>(kPerHalfFrame) == 0x3b4ccccd && std::bit_cast<uint32_t>(kThousandths) == 0x3a83126f,
              "the gallery's constants");

const float kUnitScale[3] = { 1.0f, 1.0f, 1.0f };
const Coord4 kUnitScale4 = { 1.0f, 1.0f, 1.0f, 1.0f };

constexpr uint32_t kDepthAlways = 0x207;    // GL_ALWAYS
constexpr uint32_t kTarClamp = 3;           // D3DTADDRESS_CLAMP
constexpr int kLineStrip = 4;               // the simple materials' primitive numbers

// ---- construction

// FUNC_AT(0x000d5d20)
GGallery* GGallery::Construct(const char *fileName) {
    position = { 0.0f, 0.0f, 0.0f, 1.0f };
    orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    scale = { 1.0f, 1.0f, 1.0f, 1.0f };
    unknownA0 = 0;
    shapeCount = 0;
    colour = 0x7f7f7f7f;
    texture = NULL;
    unknownA4 = 0;
    font = NULL;
    fontFile = NULL;
    keepRenderState = false;

    char path[128];
    GSystem::FILE_FullPathname(fileName, path);
    void *data = UFileLoader::FileLoad(path, 0);
    if (data == NULL) {
        char fallback[128];
        GSystem::FILE_FullPathname("gall1.gal", fallback);
        data = UFileLoader::FileLoad(fallback, 0);
    }
    gallery = UGroup::Deserialize(data, true);
    file = data;

    for (int i = 0; i < kGalleryShapes; i++) {
        UData *textures = gallery->DataLocateTag(GalleryIndexedTag(kGalleryTagTextures, i));
        if (textures == gallery->DataEnd())
            break;
        ShapeFile *textureFile = reinterpret_cast<ShapeFile *>(textures->Data());
        int main = MGMT_MainShapeIndex(textureFile);
        uint8_t *image = reinterpret_cast<uint8_t *>(textureFile) + textureFile->entries[main].offset;
        EAGL::TAR *memory = static_cast<EAGL::TAR *>(EaglMalloc(sizeof(EAGL::TAR), "EAGL::TAR new"));
        shapes[shapeCount] = memory != NULL ? memory->Construct(image) : NULL;
        EAGL::TAR *tar = shapes[shapeCount];
        tar->address0 = kTarClamp;
        tar->addressU = kTarClamp;
        tar->addressV = kTarClamp;
        tar->addressW = kTarClamp;
        tar->filter = 2;
        tar->mipFilter = 0;
        shapeCount++;
    }
    return this;
}

// FUNC_AT(0x000d6e30)
void GGallery::Destruct() {
    if (file != NULL) {
        MEM_free(file);
        file = NULL;
    }
    for (int i = 0; i < shapeCount; i++) {
        EAGL::TAR *tar = shapes[i];
        if (tar != NULL) {
            tar->Destruct();
            EaglFree(tar, sizeof(EAGL::TAR));
        }
    }
    if (font != NULL)
        FONT_destroy(reinterpret_cast<const uint8_t *>(font));
    if (fontFile != NULL)
        MEM_free_copy(fontFile);
}

// FUNC_AT(0x000d5800)
UGroup* GGallery::GET_Gallery() {
    return gallery;
}

// FUNC_AT(0x000d57a0)
int GGallery::MGMT_MainShapeIndex(const ShapeFile *textureFile) {
    for (int i = textureFile->count - 1; i >= 0; i--) {
        uint32_t name;
        SHAPE_name(reinterpret_cast<const uint8_t *>(textureFile), i, &name);
        if (memcmp(&name, "MAIN", 4) == 0)
            return i;
    }
    return 0;
}

// FUNC_AT(0x000d59c0)
void* GGallery::GET_SomeThing(int index) {
    return gallery->DataLocateTag(GalleryIndexedTag(kGalleryTagStream, index))->Data();
}

// ---- finding canvases, puppets and keys

// FUNC_AT(0x000d3850)
UGroup* GGallery::GET_Canvas(UGroup *group, const char *name) {
    if (group == NULL)
        return NULL;
    for (UGroup *canvas = group->GetArray(); canvas != group->GetArray() + group->GroupCount(); canvas++) {
        const char *canvasName = reinterpret_cast<const char *>(canvas->DataLocateTag(kGalleryTagName)->Data());
        if (_stricmp(canvasName, name) == 0)
            return canvas;
    }
    return NULL;
}

// FUNC_AT(0x000d5810)
GalleryKey* GGallery::GET_Puppet(UGroup *canvas, const char *name, int kind, int unknown) {
    (void)kind;
    (void)unknown;
    UGroup *puppet = canvas->GetArray();
    for (; puppet != canvas->GetArray() + canvas->GroupCount(); puppet++) {
        const char *puppetName = reinterpret_cast<const char *>(puppet->DataLocateTag(kGalleryTagName)->Data());
        if (strcmp(puppetName, name) == 0)
            break;
    }
    if (puppet == canvas->GetArray() + canvas->GroupCount())
        return NULL;
    return reinterpret_cast<GalleryKey *>(puppet->DataLocateTag(kGalleryTagKeys)->Data());
}

// FUNC_AT(0x000d58f0)
GalleryKey* GGallery::GET_Key(GalleryKey *puppet, int index) {
    return reinterpret_cast<GalleryKey *>(reinterpret_cast<uint8_t *>(puppet) + puppet->KeyStride() * index);
}

// 0x34 bytes are written: the header's last bytes and four of the data come from the original's uninitialised
// stack. Here they are 0.
// FUNC_AT(0x000d5910)
void GGallery::Get_InterpolatedKey(GalleryKey *out, const GalleryKey *a, const GalleryKey *b, float t) {
    GalleryKey key = {};
    key.position.x = (float)(((double)b->position.x - a->position.x) * t + a->position.x);
    key.position.y = (float)(((double)b->position.y - a->position.y) * t + a->position.y);
    key.position.z = (float)(((double)b->position.z - a->position.z) * t + a->position.z);
    key.time = Ftol((double)(b->time - a->time) * t + a->time);
    key.orientation = a->orientation;
    key.unknown20 = a->unknown20;
    key.extraSize = a->extraSize;
    memcpy(out, &key, 0x34);
}

// FUNC_AT(0x000d5b70)
void GGallery::QUAD_Interpolate(const GalleryQuad *a, const GalleryQuad *b, GalleryQuad *out, float t) {
    *out = *a;
    for (int i = 0; i < 4; i++)
        out->colours[i] = GSystem::COLOR_Interpolate(a->colours[i], b->colours[i], t);
    for (int i = 0; i < 4; i++) {
        out->corners[i][0] = (float)(((double)b->corners[i][0] - a->corners[i][0]) * t + a->corners[i][0]);
        out->corners[i][1] = (float)(((double)b->corners[i][1] - a->corners[i][1]) * t + a->corners[i][1]);
    }
}

// FUNC_AT(0x000d5c60)
void GGallery::TRANSFORM_Vertices(Coord4 *vertices, int count, const Coord4 *where, const Coord4 *rotation,
                                  const float *scaling) {
    alignas(16) Coord4 conjugate;
    conjugate.x = -rotation->x;
    conjugate.y = -rotation->y;
    conjugate.z = -rotation->z;
    conjugate.w = rotation->w;
    alignas(16) MATRIX4 transform;
    VU0_quattom4(&transform, &conjugate);
    transform.mtx[3][0] = where->x;
    transform.mtx[3][1] = where->y;
    transform.mtx[3][2] = where->z;
    VU0_v4scale4(transform.mtx[0], scaling[0], transform.mtx[0]);
    VU0_v4scale4(transform.mtx[1], scaling[1], transform.mtx[1]);
    VU0_v4scale4(transform.mtx[2], scaling[2], transform.mtx[2]);
    for (int i = 0; i < count; i++)
        VU0_MATRIX4_vect4mult(&vertices[i], &transform, &vertices[i]);
}

// ---- state

// FUNC_AT(0x000d5a00)
void GGallery::SPRITE_SetState(const GalleryKey *key) {
    position.x = key->position.x;
    position.y = key->position.y;
    position.z = key->position.z;
    orientation = key->orientation;
    scale.z = 1.0f;
    scale.x = (float)((double)key->sprite.scaleX * kThousandths);
    scale.y = (float)((double)key->sprite.scaleY * kThousandths);
    colour = key->sprite.colour;
    texture = shapes[key->sprite.shape];
}

// FUNC_AT(0x000d5b30)
void GGallery::QUAD_SetState(const GalleryKey *key) {
    position.x = key->position.x;
    position.y = key->position.y;
    position.z = key->position.z;
    orientation = key->orientation;
    colour = 0;
}

// FUNC_AT(0x000d6eb0)
void GGallery::FONT_SetState(const GalleryKey *key) {
    position.x = key->position.x;
    position.y = key->position.y;
    position.z = key->position.z;
    orientation = key->orientation;
    colour = key->text.colour;
    texture = shapes[key->text.shape];
    font->scaleX = (float)((double)key->text.scaleX * kThousandths);
    font->scaleY = (float)((double)key->text.scaleY * kThousandths);
}

// FUNC_AT(0x000d61e0)
void GGallery::FONT_SetScale(float x, float y) {
    font->scaleX = x;
    font->scaleY = y;
}

// FUNC_AT(0x000d5a80)
void GGallery::FONT_Load(const char *fileName) {
    if (font != NULL)
        FONT_destroy(reinterpret_cast<const uint8_t *>(font));
    if (fontFile != NULL)
        MEM_free_copy(fontFile);
    fontFile = UFileLoader::FileLoad(fileName, 0);
    font = (FNTXFont *)FONT_create(static_cast<const uint8_t *>(fontFile));
}

// FUNC_AT(0x000d5ad0)
float GGallery::FONT_Width(const char *text) {
    float width = 0.0f;
    FONT_getrectx(reinterpret_cast<const uint8_t *>(font), reinterpret_cast<const uint8_t *>(text), NULL, NULL,
                  &width, NULL);
    return width;
}

// FUNC_AT(0x000d5b00)
float GGallery::FONT_Height(const char *text) {
    float height = 0.0f;
    FONT_getrectx(reinterpret_cast<const uint8_t *>(font), reinterpret_cast<const uint8_t *>(text), NULL, NULL,
                  NULL, &height);
    return height;
}

// ---- drawing

// The simple materials' state for a screen draw: no alpha test, the depth test always passing
void GGallery::DefaultRenderState() {
    SimpleTexturedMaterial->SetAlphaTestEnable(false);
    SimpleTexturedMaterial->SetDepthTestMethod(kDepthAlways);
    SimpleMaterial->SetDepthTestMethod(kDepthAlways);
}

// Text at (x, y) of the 640 x 480 frame on the real screen. `text` goes to FONT as its format.
void GGallery::DrawFontText(float x, float y, uint32_t textColour, const char *text) {
    float screenHeight = (float)fgRenderer->screenHeight;
    float screenY = (float)((double)y * screenHeight * kPerLine);
    double scaleX = font->scaleX;
    double scaleY = (double)font->scaleY * screenHeight * kPerLine;
    if (!fgRenderer->widescreen) {
        scaleX *= kSafeScale;
        scaleY *= kSafeScale;
        x = (float)((double)x * kSafeScale - (double)fgRenderer->screenWidth * kSafeMargin);
        screenY = (float)((double)screenY * kSafeScale - (double)screenHeight * kSafeMargin +
                          (double)screenHeight * kLineOf48);
    } else {
        scaleX *= kWideFontScale;
        if (fgRenderer->videoMode == RRenderer::kVideoWide) {
            if (x < kFrameCentre)
                x = (float)(((double)kFrameCentre - x) * kPerHalfFrame * kWideSqueeze + x);
            else
                x = (float)(x - ((double)x - kFrameCentre) * kPerHalfFrame * kWideSqueeze);
        } else {
            scaleY *= kWideFontScale;
        }
    }
    font->scaleX = (float)scaleX;
    font->scaleY = (float)scaleY;
    font->colour = textColour;
    FONT_drawtextfa(reinterpret_cast<const uint8_t *>(font), x, screenY, text);
}

// FUNC_AT(0x000d6200)
void GGallery::FONT_Draw(const GalleryText *key, const char *text) {
    if (text == NULL)
        text = GSystem::LOCALE_Symbol(key->text);
    if (!keepRenderState)
        DefaultRenderState();
    DrawFontText(position.x, position.y, colour, text);
}

// FUNC_AT(0x000d63a0)
void GGallery::FONT_Draw(const char *text, float x, float y, uint32_t textColour) {
    if (!keepRenderState)
        DefaultRenderState();
    DrawFontText(x, y, textColour, text);
}

// FUNC_AT(0x000d6520)
void GGallery::FONT_Draw(const char *text, float x, float y) {
    if (!keepRenderState)
        DefaultRenderState();
    DrawFontText(x, y, colour, text);
}

// FUNC_AT(0x000d5f50)
void GGallery::BITMAP_Draw2D(const GalleryKey *key) {
    const GallerySprite *sprite = &key->sprite;
    Coord4 where = { position.x, position.y, position.z, 1.0f };
    float halfWidth = (float)((double)scale.x * sprite->width * 0.5);
    float halfHeight = (float)((double)scale.y * sprite->height * 0.5);
    alignas(16) Coord4 corners[4] = {
        { -halfWidth, -halfHeight, 1.0f, 1.0f },
        { halfWidth, -halfHeight, 1.0f, 1.0f },
        { -halfWidth, halfHeight, 1.0f, 1.0f },
        { halfWidth, halfHeight, 1.0f, 1.0f },
    };
    float left = kTexelInset + sprite->texture[0];
    float top = kTexelInset + sprite->texture[2];
    float right = sprite->texture[1] - kTexelInset;
    float bottom = sprite->texture[3] - kTexelInset;
    Coord4 texCoords[4] = {
        { left, top, 1.0f, 1.0f },
        { right, top, 1.0f, 1.0f },
        { left, bottom, 1.0f, 1.0f },
        { right, bottom, 1.0f, 1.0f },
    };
    for (Coord4 &corner : corners)
        GSystem::MATH_TransformPoints(&corner, &where, &orientation, &kUnitScale4);
    if (!keepRenderState) {
        Draw::SetNormalBlendMode(SimpleTexturedMaterial);
        DefaultRenderState();
    }
    Draw::DrawBatchedTexturedQuad(&corners[0], &corners[1], &corners[2], &corners[3], colour, texCoords,
                                  shapes[sprite->shape]);
}

// FUNC_AT(0x000d66a0)
void GGallery::LINE_Draw2DStrip(const Coord4 *points, const uint32_t *colours, int count) {
    if ((LineMaterialMade & 1) == 0) {
        LineMaterialMade |= 1;
        LineMaterial.Construct();
        Crt::atexit(DestroyStatic_0023f330);
    }
    LineMaterial.SetDepthTestMethod(kDepthAlways);
    SimpleRequests[SimpleRequestIndex]->colours.SetData(colours);
    SimpleRequests[SimpleRequestIndex]->positions.SetData(points);
    LineMaterial.Draw(kLineStrip, count, NULL);
}

// FUNC_AT(0x000d6750)
void GGallery::QUAD_Draw(const GalleryQuad *quad, float alpha) {
    uint32_t colours[4];
    if (colour != 0) {
        colours[0] = colour;
        colours[1] = colour;
        colours[2] = colour;
        colours[3] = colour;
    } else {
        colours[0] = quad->colours[0];
        colours[1] = quad->colours[1];
        colours[2] = quad->colours[2];
        colours[3] = quad->colours[3];
    }
    for (uint32_t &corner : colours)
        GSystem::COLOR_ReduceAlpha(&corner, alpha);
    alignas(16) Coord4 corners[4];
    for (int i = 0; i < 4; i++)
        corners[i] = { quad->corners[i][0], quad->corners[i][1], 1.0f, 1.0f };
    TRANSFORM_Vertices(corners, 4, &position, &orientation, kUnitScale);
    if (!keepRenderState)
        DefaultRenderState();
    Draw::DrawQuadC(corners, colours);
}

// FUNC_AT(0x000d68b0)
void GGallery::QUAD_Draw(const GalleryKey *key, float alpha) {
    QUAD_Draw(&key->quad, alpha);
}

// FUNC_AT(0x000d68c0)
void GGallery::QUAD_Draw2D(float x, float y, float width, float height, uint32_t quadColour) {
    float right = x + width;
    float bottom = y + height;
    alignas(16) Coord4 corners[4] = {
        { x, y, 1.0f, 1.0f },
        { right, y, 1.0f, 1.0f },
        { x, bottom, 1.0f, 1.0f },
        { right, bottom, 1.0f, 1.0f },
    };
    if (!keepRenderState)
        DefaultRenderState();
    Draw::DrawQuad(corners, quadColour);
}

// FUNC_AT(0x000d69a0)
void GGallery::SPRITE_Draw2D(int x, int y, const GalleryKey *key, const GallerySprite *sprite) {
    if (sprite == NULL)
        sprite = &key->sprite;
    else
        colour = sprite->colour;
    int stepX = 0;
    int stepY = 0;
    SpriteTexCoords texCoords;
    texCoords.topLeft = { kTexelInset + sprite->texture[0], kTexelInset + sprite->texture[2], 1.0f, 0.0f };
    texCoords.bottomRight = { sprite->texture[1] - kTexelInset, sprite->texture[3] - kTexelInset, 1.0f, 0.0f };
    float width = scale.x * sprite->width;
    float height = scale.y * sprite->height;
    if (sprite->repeatLeft > 0)
        stepX = Ftol(-width);
    if (sprite->repeatRight > 0)
        stepX = Ftol(width);
    if (sprite->repeatUp > 0)
        stepY = Ftol(-height);
    if (sprite->repeatDown > 0)
        stepY = Ftol(height);
    int count = sprite->repeatDown + sprite->repeatUp + sprite->repeatRight + sprite->repeatLeft;
    if (count == 0)
        count = 1;

    Draw::SetNormalBlendMode(SimpleTexturedMaterial);
    if (!keepRenderState)
        DefaultRenderState();
    for (; count > 0; count--) {
        float left = kTexelInset + sprite->texture[0];
        float top = kTexelInset + sprite->texture[2];
        float right = sprite->texture[1] - kTexelInset;
        float bottom = sprite->texture[3] - kTexelInset;
        bool drawn = false;
        if ((sprite->flags & kSpriteMirrorX) && (sprite->flags & kSpriteMirrorY)) {
            texCoords.topLeft.x = left;
            texCoords.topLeft.y = top;
            texCoords.bottomRight.x = right;
            texCoords.bottomRight.y = bottom;
            float at = (float)x;
            float atY = (float)y;
            Draw::DrawSprite(at, atY, width, height, colour, &texCoords, shapes[sprite->shape]);
            texCoords.topLeft.x = right;
            texCoords.topLeft.y = bottom;
            texCoords.bottomRight.x = left;
            texCoords.bottomRight.y = top;
            Draw::DrawSprite(at - width, atY - height, width, height, colour, &texCoords, shapes[sprite->shape]);
            drawn = true;
        }
        if (sprite->flags & kSpriteMirrorX) {
            texCoords.topLeft.x = right;
            texCoords.topLeft.y = top;
            texCoords.bottomRight.x = left;
            texCoords.bottomRight.y = bottom;
            Draw::DrawSprite((float)((double)x - width), (float)y, width, height, colour, &texCoords,
                             shapes[sprite->shape]);
            drawn = true;
        }
        if (sprite->flags & kSpriteMirrorY) {
            texCoords.topLeft.x = left;
            texCoords.topLeft.y = bottom;
            texCoords.bottomRight.x = right;
            texCoords.bottomRight.y = top;
            Draw::DrawSprite((float)x, (float)((double)y - height), width, height, colour, &texCoords,
                             shapes[sprite->shape]);
        } else if (!drawn) {
            texCoords.topLeft.x = left;
            texCoords.topLeft.y = top;
            texCoords.bottomRight.x = right;
            texCoords.bottomRight.y = bottom;
            Draw::DrawSprite((float)x, (float)y, width, height, colour, &texCoords, shapes[sprite->shape]);
        }
        x += stepX;
        y += stepY;
    }
}

// FUNC_AT(0x000d6d70)
void GGallery::SPRITE_Draw2D(const GalleryKey *key) {
    int y = Ftol(key->position.y);
    int x = Ftol(key->position.x);
    SPRITE_Draw2D(x, y, key, NULL);
}

// FUNC_AT(0x000d6da0)
void GGallery::SPRITE_Flush() {
    Draw::SetNormalBlendMode(SimpleTexturedMaterial);
    if (!keepRenderState)
        DefaultRenderState();
    Draw::DrawBatchedSprite(0.0f, 0.0f, 0.0f, 0.0f, 0, NULL, NULL);
    Draw::DrawBatchedTexturedQuad(NULL, NULL, NULL, NULL, 0, NULL, NULL);
}

// FUNC_AT(0x000d3470)
void GGallery::CULL_SetCanvas(intptr_t item, bool culled, float distance) {
    TheEffectsGallery->CULL_SetCanvas(int(item), culled, distance);
}
