#pragma fp_contract(off)

#include "Girl.h"

#include <stddef.h>
#include <stdint.h>

#include <bit>

#include "Gallery.h"                      // GGallery::GET_SomeThing
#include "Hud.h"
#include "Simulation.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GeoPrimState.h"
#include "../eagl/Realgraph.h"            // SHAPE_create, SHAPE_rowbytes
#include "../eagl/RenderMethod.h"         // GeoPrimParam
#include "../eagl/Tar.h"
#include "../engine/UMemory.hpp"          // OperatorNew, OperatorDelete
#include "../platform/RealPrint.h"        // MEM_copy, MEM_fill
#include "../platform/RealSystem.h"       // TIMER_gettick
#include "../platform/X87.h"              // Ftol
#include "../render/Materials.h"          // USimpleTexturedMaterial, TexturedRequests
#include "../../common/xbeOverload.h"     // XbeVirtual

// ---------------------------------------------------------------------------------------------------------------
// GGirl and GGirlieMaterial (0x000d6f20-0x000d7bd0), ported from the listing.
//
// DoGirl ends with WBINVD in the original, flushing the CPU's cache for the GPU's sake: nothing to do on a PC.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

#define GirlPalette ((const int8_t (*)[128][3])0x00193e70)  // a 48 KB table in the XBE's read-only data
#define GirlieMaterialVtable ((const void *)0x0019fe78)

// XBE_GLOBAL(0x0023f398, 0x8)
GGirlieMaterial *GirlMaterials[kGirlTextures];
// XBE_GLOBAL(0x0023f3a0, 0x4)
int32_t GirlCurrent;                    // the texture decoded into next
// XBE_GLOBAL(0x0023f3a4, 0x4)
uint8_t *GirlStream;                    // the gallery's 'fi' data
// XBE_GLOBAL(0x0023f3ac, 0x4)
int32_t GirlActive;
// Written by InitGirl, read by nothing
// XBE_GLOBAL(0x0023f3b0, 0x4)
int32_t GirlUnknownB0;
// XBE_GLOBAL(0x0023f3b4, 0x4)
int32_t GirlUnknownB4;
// XBE_GLOBAL(0x0023f3b8, 0x4)
int32_t GirlUnknownB8;
// XBE_GLOBAL(0x0023f3bc, 0x4)
int32_t GirlFps;
// The palette's two scrolling offsets, in texels: the first moves each frame, the second never does. Each has
// two speeds that UpdateTexture writes but nothing reads.
// XBE_GLOBAL(0x0023f3c0, 0x4)
float PaletteX;
// XBE_GLOBAL(0x0023f3c4, 0x4)
float PaletteY;
// XBE_GLOBAL(0x0023f3c8, 0x4)
float PaletteSpeedX;
// XBE_GLOBAL(0x001ca3f0, 0x4)
float PaletteSpeedY = 1.0f;
// XBE_GLOBAL(0x0023f3cc, 0x4)
float PaletteX2;
// XBE_GLOBAL(0x0023f3d0, 0x4)
float PaletteY2;
// XBE_GLOBAL(0x0023f3d4, 0x4)
float PaletteSpeedX2;
// XBE_GLOBAL(0x001ca3f4, 0x4)
float PaletteSpeedY2 = -1.0f;
// The quad's size on the screen (a static initialiser's 2 x 128)
// XBE_GLOBAL(0x0023f3dc, 0x4)
int32_t GirlWidth = 256;
// XBE_GLOBAL(0x0023f3e0, 0x4)
int32_t GirlHeight = 256;
// Never written: the frames are drawn mirrored
// XBE_GLOBAL(0x0023f3e4, 0x4)
int32_t GirlUnmirrored;
// The stream being played
// XBE_GLOBAL(0x0023f3e8, 0x4)
int32_t GirlFrameStart;                 // the current frame's record
// XBE_GLOBAL(0x0023f3ec, 0x4)
int32_t GirlCursor;                     // the next byte
// XBE_GLOBAL(0x0023f3f0, 0x4)
float GirlTime;                         // seconds played, counted up to 2
// XBE_GLOBAL(0x0023f3f4, 0x4)
int32_t GirlTick;                       // TIMER_gettick when the frame due was last worked out
// XBE_GLOBAL(0x0023f3f8, 0x4)
uint8_t *GirlData;                      // GirlStream while playing
// XBE_GLOBAL(0x0023f3fc, 0x4)
int32_t GirlFrames;
// XBE_GLOBAL(0x0023f400, 0x4)
int32_t GirlFrame;

// ---- constants

constexpr int kGirlSize = 128;              // the textures' width and height
constexpr int kMaxGirlFrames = 1024;
constexpr int kMaxFrameSize = 127;          // a frame's width, height, left and top
constexpr float kTimeLimit = 2.0f;          // GirlTime stops counting here
constexpr double kTicksWrap = 4294967296.0;
constexpr float kPaletteSpeedX = 0.31f;
constexpr float kPaletteSpeedY = 0.57f;
constexpr float kPaletteSpeedX2 = -0.71f;
constexpr float kPaletteSpeedY2 = -0.19f;
constexpr float kPaletteSize = 128.0f;
constexpr float kPaletteScale = 1.0f / 256.0f;
constexpr float kShadeLevels = 255.0f;
constexpr int kAlphaStep = 27;              // a shade's alpha: 0 to 7 of these
static_assert(std::bit_cast<uint32_t>(kPaletteSpeedX) == 0x3e9eb852 &&
              std::bit_cast<uint32_t>(kPaletteSpeedY) == 0x3f11eb85 &&
              std::bit_cast<uint32_t>(kPaletteSpeedX2) == 0xbf35c28f &&
              std::bit_cast<uint32_t>(kPaletteSpeedY2) == 0xbe428f5c, "the figure's constants");

constexpr uint32_t kDepthAlways = 0x207;    // GL_ALWAYS
constexpr int kStrip = 6;                   // kTriangleStrip

// A SHAPE image's pixels
static uint8_t *ShapePixels(uint8_t *shape) {
    const EAGL::ShapeImage *image = reinterpret_cast<const EAGL::ShapeImage *>(shape);
    return (image->flags & EAGL::kShapeDataAtOffset) ? shape + image->dataOffset : shape + 0x10;
}

static void ClearShape(uint8_t *shape) {
    uint8_t *pixels = ShapePixels(shape);
    MEM_fill(pixels, 0, SHAPE_rowbytes(shape) * reinterpret_cast<const EAGL::ShapeImage *>(shape)->height);
}

// The stream's next shade: a byte with the top bit set is a run (its low six bits long) of shade 0 or 7 by bit 6;
// with bit 6 alone, a run of (bits 3-5) of shade (bits 0-2); with neither, one pixel of shade (bits 0-2) followed
// by one of shade (bits 3-5).
static uint8_t NextShade(const uint8_t *stream, int *cursor, int *run, uint8_t *shade, uint8_t *pending) {
    if (*run <= 0) {
        uint8_t code = stream[(*cursor)++];
        if (code & 0x80) {
            *run = code & 0x3f;
            *shade = (code & 0x40) ? 7 : 0;
        } else if (code & 0x40) {
            *run = (code >> 3) & 7;
            *shade = code & 7;
        } else {
            *pending = code + 0x40;
            *run = 1;
            *shade = code & 7;
        }
    } else {
        if (*pending != 0) {
            *shade = (*pending >> 3) & 7;
            *pending = 0;
        }
        (*run)--;
    }
    return *shade;
}

// A palette offset moved on by `speed`, kept within 0..128
static void ScrollPalette(float *offset, float speed) {
    double moved = (double)*offset + speed;
    *offset = (float)moved;
    if (moved >= kPaletteSize)
        *offset = *offset - kPaletteSize;
    if (*offset <= 0.0f)
        *offset = *offset + kPaletteSize;
}

static int PaletteIndex(double position) {
    int index = Ftol(position);
    if (index >= kGirlSize)
        index -= int(uint32_t(index) >> 7) << 7;
    return index;
}

// ---- GGirlieMaterial

// FUNC_AT(0x000d7770)
GGirlieMaterial* GGirlieMaterial::Construct() {
    vtable = GirlieMaterialVtable;
    for (uint32_t &colour : colours)
        colour = 0;
    for (Coord4 &position : positions)
        position = { 0.0f, 0.0f, 1.0f, 1.0f };
    for (uint32_t &colour : colours)
        colour = 0xffffffff;
    texCoords[0] = { 0.0f, 0.0f, 1.0f, 1.0f };
    texCoords[1] = { (float)kGirlSize, 0.0f, 1.0f, 1.0f };
    texCoords[2] = { 0.0f, (float)kGirlSize, 1.0f, 1.0f };
    texCoords[3] = { (float)kGirlSize, (float)kGirlSize, 1.0f, 1.0f };
    shape = SHAPE_create(kGirlSize, kGirlSize, 0x20, 0, 0, 0, 0, 0x40);
    EAGL::TAR *memory = static_cast<EAGL::TAR *>(EaglMalloc(sizeof(EAGL::TAR), "EAGL::TAR new"));
    tar = memory != NULL ? memory->Construct(shape) : NULL;
    tar->address0 = 3;
    tar->addressU = 3;
    tar->addressV = 3;
    tar->addressW = 3;
    tar->filter = 1;
    tar->mipFilter = 0;
    USimpleTexturedMaterial *made = static_cast<USimpleTexturedMaterial *>(OperatorNew(sizeof(USimpleTexturedMaterial)));
    material = made != NULL ? made->Construct() : NULL;
    USimpleTexturedMaterial::Init();
    material->SetAlphaTestEnable(false);
    material->SetAlphaTestMethod(kDepthAlways);
    material->SetDepthTestMethod(kDepthAlways);
    TexturedGeoPrim *request = TexturedRequests[TexturedRequestIndex];
    request->positions.SetData(positions);
    request->colours.SetData(colours);
    request->texCoords.SetData(texCoords);
    request->texture.SetData(tar);
    return this;
}

// FUNC_AT(0x000d7b40)
void GGirlieMaterial::Destruct() {
    vtable = GirlieMaterialVtable;
    if (tar != NULL) {
        tar->Destruct();
        EaglFree(tar, sizeof(EAGL::TAR));
        tar = NULL;
    }
    if (material != NULL) {
        material->Destruct();
        OperatorDelete(material);
        material = NULL;
    }
    if (shape != NULL) {
        UMemory::Free(shape);
        shape = NULL;
    }
}

// FUNC_AT(0x000d7bb0)
GGirlieMaterial* GGirlieMaterial::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

GGirlieMaterial* GGirlieMaterial::DeleteVirtual(unsigned flags) {
    typedef GGirlieMaterial *(GGirlieMaterial::*Method)(unsigned);
    return (this->*XbeVirtual<Method>(this, 0))(flags);
}

// ---- GGirl

// FUNC_AT(0x000d7990)
void GGirl::InitGirl() {
    if (GirlActive != 0)
        return;
    GirlStream = static_cast<uint8_t *>(GHud::TheApp()->gallery->GET_SomeThing(0));
    GirlFps = Ftol(30.0f);
    GirlUnknownB0 = 1;
    GirlUnknownB4 = 0;
    GirlUnknownB8 = 0;
    GirlActive = 1;
    for (GGirlieMaterial *&material : GirlMaterials) {
        GGirlieMaterial *memory = static_cast<GGirlieMaterial *>(OperatorNew(sizeof(GGirlieMaterial)));
        material = memory != NULL ? memory->Construct() : NULL;
    }
}

// The materials are left pointing at what was deleted.
// FUNC_AT(0x000d6f20)
void GGirl::KillGirl() {
    if (GirlActive == 0)
        return;
    for (GGirlieMaterial *material : GirlMaterials) {
        if (material != NULL)
            material->DeleteVirtual(1);
    }
    GirlStream = NULL;
    GirlActive = 0;
}

// FUNC_AT(0x000d7a50)
void GGirl::DoGirl() {
    if (GirlActive == 0) {
        InitGirl();
        for (GGirlieMaterial *material : GirlMaterials)
            ClearShape(material->shape);
    } else {
        ClearShape(GirlMaterials[GirlCurrent]->shape);
    }
    int x;
    int y;
    if (UpdateTexture(&x, &y)) {
        DrawTexture(x, y);
        GirlCurrent++;
        if (GirlCurrent >= kGirlTextures)
            GirlCurrent = 0;
    }
}

// The time since GirlTick added to GirlTime, scaled by the simulation's step
static void CountGirlTime(int tick) {
    int elapsed = tick - GirlTick;
    double ticks = elapsed;
    if (elapsed < 0)
        ticks += kTicksWrap;
    GirlTick = tick;
    GirlTime = (float)(ticks * fgSimulation.timeStep + GirlTime);
}

// FUNC_AT(0x000d6f60)
bool GGirl::UpdateTexture(int *x, int *y) {
    uint8_t pending = 0;
    uint8_t shade = 0;
    int run = 0;
    int tick = TIMER_gettick();
    if (GirlActive == 0)
        return false;

    const uint8_t *stream;
    int cursor;
    if (GirlFrame >= GirlFrames || GirlData == NULL) {
        GirlTick = tick;
        int frames = (GirlStream[0] << 8) + GirlStream[1];
        GirlFrame = 0;
        GirlTime = 0.0f;
        GirlData = GirlStream;
        GirlFrames = frames;
        GirlCursor = 2;
        GirlFrameStart = 2;
        stream = GirlStream;
        cursor = 2;
        if (frames > kMaxGirlFrames) {
            GirlData = NULL;
            return false;
        }
        CountGirlTime(tick);
    } else {
        stream = GirlData;
        cursor = GirlCursor;
        if (GirlTime < kTimeLimit)
            CountGirlTime(tick);
    }

    // The frames due since GirlTick: none, the next one, or more, the ones before the last skipped
    int elapsed = tick - GirlTick;
    double ticks = elapsed;
    if (elapsed < 0)
        ticks += kTicksWrap;
    uint32_t steps = Ftol(ticks * GirlFps * fgSimulation.timeStep + 0.5);
    if (steps > uint32_t(GirlFrames / 2)) {
        GirlTick = tick;
        cursor = GirlFrameStart;
    } else if (steps == 0) {
        cursor = GirlFrameStart;
    } else if (steps == 1) {
        GirlTick = tick;
        GirlFrameStart = cursor;
        GirlFrame++;
    } else {
        for (uint32_t skipped = steps - 1; skipped != 0; skipped--) {
            int width = stream[cursor];
            int height = stream[cursor + 1];
            cursor += 4;
            for (int row = 0; row < height; row++) {
                for (int column = 0; column < width; column++)
                    NextShade(stream, &cursor, &run, &shade, &pending);
            }
        }
        GirlTick = tick;
        GirlFrame += steps;
        GirlFrameStart = cursor;
    }

    int width = stream[cursor];
    int height = stream[cursor + 1];
    int left = stream[cursor + 2];
    int top = stream[cursor + 3];
    cursor += 4;
    GirlCursor = cursor;
    if (width > kMaxFrameSize || height > kMaxFrameSize || left > kMaxFrameSize || top > kMaxFrameSize) {
        GirlData = NULL;
        return false;
    }

    PaletteSpeedX = kPaletteSpeedX;
    PaletteSpeedY = kPaletteSpeedY;
    PaletteSpeedX2 = kPaletteSpeedX2;
    PaletteSpeedY2 = kPaletteSpeedY2;
    ScrollPalette(&PaletteX, kPaletteSpeedX);
    ScrollPalette(&PaletteY, kPaletteSpeedY);

    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            uint32_t colour = 0;
            if (NextShade(stream, &cursor, &run, &shade, &pending) != 0) {
                double atY = (double)row + top;
                float atYRounded = (float)atY;
                int u = PaletteIndex(atY + PaletteY);
                double atX = (double)column + left;
                int v = PaletteIndex(PaletteX + atX);
                int u2 = PaletteIndex((double)atYRounded + PaletteY2);
                int v2 = PaletteIndex(atX + PaletteX2);
                const int8_t *first = GirlPalette[u][v];
                const int8_t *second = GirlPalette[u2][v2];
                uint8_t channels[3];
                for (int i = 0; i < 3; i++)
                    channels[i] = uint8_t(Ftol(((double)second[i] * kPaletteScale + (double)first[i] * kPaletteScale) *
                                               kShadeLevels));
                colour = uint32_t(uint8_t(shade * kAlphaStep)) << 24 | uint32_t(channels[0]) << 16 |
                         uint32_t(channels[1]) << 8 | channels[2];
            }
            GirlCursor = cursor;
            uint8_t *shape = GirlMaterials[GirlCurrent]->shape;
            uint8_t *pixels = ShapePixels(shape);
            uint8_t *line = pixels + SHAPE_rowbytes(shape) * row;
            uint8_t *destination = GirlUnmirrored == 1 ? line + column * 4 : line + (kGirlSize - 1 - column) * 4;
            MEM_copy(destination, &colour, 4);
        }
    }

    *x = GirlUnmirrored == 1 ? left * 2 + 16 : (184 - left) * 2;
    *y = top * 2 + 120;
    return true;
}

// FUNC_AT(0x000d7690)
void GGirl::DrawTexture(int x, int y) {
    GGirlieMaterial *material = GirlMaterials[GirlCurrent];
    material->positions[0].x = (float)x;
    material->positions[0].y = (float)y;
    material->positions[1].x = (float)(GirlWidth + x);
    material->positions[1].y = (float)y;
    material->positions[2].x = (float)x;
    material->positions[2].y = (float)(GirlHeight + y);
    material->positions[3].x = (float)(GirlWidth + x);
    material->positions[3].y = (float)(GirlHeight + y);
    TexturedGeoPrim *request = TexturedRequests[TexturedRequestIndex];
    request->positions.SetData(material->positions);
    request->colours.SetData(material->colours);
    request->texCoords.SetData(material->texCoords);
    request->texture.SetData(material->tar);
    material->material->Draw(kStrip, 4, NULL);
}

