#pragma fp_contract(off)

#include "OffscreenBuffer.h"

#include <stdio.h>

#include "../eagl/GeoPrimState.h"
#include "../eagl/RenderContext.h"
#include "../eagl/Tar.h"
#include "../eagl/View.h"
#include "../gfx/D3D8.h"
#include "../platform/RealMath.h"
#include "../world/CollisionTypes.h"    // MATRIX4, Coord4
#include "Draw.h"
#include "Materials.h"
#include "Renderer.h"

// ---------------------------------------------------------------------------------------------------------------
// ROffscreenBuffer (0x0007f5a0..0x0007f9f0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OffscreenUntested(const char *what) {
    printf("[offscreen] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define OFFSCREEN_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            OffscreenUntested(what); \
        } \
    } while (0)

// D3DDevice_CreateTexture2's arguments for the colour texture
constexpr uint32_t kD3DUsageRenderTarget = 1;
constexpr uint32_t kD3DFormatA8R8G8B8 = 6;
constexpr uint32_t kD3DResourceTexture = 3;
// EAGL_TARRenderTarget's mode
constexpr int32_t kRenderTargetMode = 1;

constexpr uint32_t kClearAll = 7;               // the view's colour, depth and stencil
constexpr uint32_t kDepthTestAlways = 0x207;    // GL_ALWAYS
constexpr uint32_t kAlphaBlendOff = 0;          // the parser's ABM_OFF
constexpr uint32_t kAlphaBlendBlend = 1;        // ABM_BLEND
constexpr uint32_t kOpaque = 0;
constexpr uint32_t kTransparent = 1;
constexpr uint32_t kWhite = 0xffffffff;

// MakeTexCopy's offsets for its extra copies, in order
const struct {
    float x, y;
} kCopyOffsets[4] = { { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f } };

// Begins the buffer's frame and view, and clears it with alpha writes on
void BeginAndClear(ROffscreenBuffer *buffer) {
    buffer->context->BeginFrame();
    buffer->viewPort->BeginView();
    fgRenderer->EnableAlphaWrites();
    buffer->viewPort->ClearViewPort(kClearAll);
}

}  // namespace

// FUNC_AT(0x0007f5a0)
void ROffscreenBuffer::Destruct() {
    context->DeleteViewPort(viewPort);
}

// FUNC_AT(0x0007f5b0)
void ROffscreenBuffer::Begin() {
    BeginAndClear(this);
    fgRenderer->DisableAlphaWrites();
}

// FUNC_AT(0x0007f5f0)
void ROffscreenBuffer::End() {
    viewPort->EndView();
    context->EndFrame();
}

// FUNC_AT(0x0007f610)
ROffscreenBuffer* ROffscreenBuffer::Construct(int width, int height, int bitDepth, bool withDepth, bool asTexture) {
    this->width = width;
    this->height = height;
    this->bitDepth = bitDepth;
    if (asTexture) {
        texture = D3DDevice_CreateTexture2(width, height, 1, 1, kD3DUsageRenderTarget, kD3DFormatA8R8G8B8,
                                           kD3DResourceTexture);
        colourTarget = EAGL_TARFromSurface(texture);
    } else {
        colourTarget = EAGL_TARRenderTarget(width, height, bitDepth, kRenderTargetMode);
        texture = NULL;
    }
    if (withDepth)
        depthTarget = EAGL_TARDepthSurface(width, height, bitDepth);
    else
        depthTarget = NULL;

    context = fgRenderer->device->Extension()->NewTextureRenderContext();
    context->SetupFrameBuffers(colourTarget, depthTarget);
    viewPort = context->NewViewPort();
    viewPort->SetShape(0.0f, 0.0f, float(width), float(height), 0.01f, 1.0f);
    viewPort->SetOrthographic(0.0f, 10.0f);
    viewPort->SetBackgroundColour(0xff000000);
    alignas(16) MATRIX4 view;
    BuildScale(&view, 1.0f, 1.0f, -1.0f);
    viewPort->SetViewMatrix(view.mtx[0]);
    return this;
}

// FUNC_AT(0x0007f740)
void ROffscreenBuffer::MakeCopy(EAGL::TAR *source, int scale) {
    BeginAndClear(this);
    fgRenderer->DisableAlphaWrites();
    EAGL::GeoPrimState *state = SimpleTexturedMaterial;
    state->SetDepthTestMethod(kDepthTestAlways);
    state->SetAlphaBlendMode(kAlphaBlendBlend);
    state->SetTransparencyMethod(kOpaque);
    state->SetAlphaTestEnable(false);
    fgRenderer->EnableAlphaWrites();
    Draw::SetZDepthNear();

    float screenWidth, screenHeight;
    fgRenderer->renderContext->GetSize(&screenWidth, &screenHeight);
    const SpriteTexCoords uvs = {
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { float(scale * double(screenWidth)), screenHeight, 1.0f, 1.0f },
    };
    Draw::DrawSprite(0.0f, 0.0f, float(this->width), float(this->height), kWhite, &uvs, source);
    viewPort->EndView();
    context->EndFrame();
}

// FUNC_AT(0x0007f870)
void ROffscreenBuffer::MakeTexCopy(EAGL::TAR *source, int copies, float spread) {
    BeginAndClear(this);
    fgRenderer->DisableAlphaWrites();
    fgRenderer->DisableAlphaWrites();
    EAGL::GeoPrimState *state = SimpleTexturedMaterial;
    state->SetDepthTestMethod(kDepthTestAlways);
    state->SetAlphaTestEnable(false);
    state->SetTransparencyMethod(kOpaque);
    state->SetAlphaBlendMode(kAlphaBlendOff);
    Draw::DrawSprite(0.0f, 0.0f, float(width), float(height), kWhite, source);

    state->SetTransparencyMethod(kTransparent);
    state->SetAlphaBlendMode(kAlphaBlendBlend);
    for (int i = 0; i < copies; i++) {
        uint32_t alpha = 255 / (i + 2);
        float x = 0.0f, y = 0.0f;
        if (i < 4) {
            x = spread * kCopyOffsets[i].x;
            y = spread * kCopyOffsets[i].y;
        } else {
            // The original reads past its four offsets into its own stack frame
            OFFSCREEN_UNTESTED("ROffscreenBuffer::MakeTexCopy past four copies");
        }
        Draw::DrawSprite(x, y, float(width), float(height), alpha << 24 | 0xffffff, source);
    }
    viewPort->EndView();
    context->EndFrame();
}
