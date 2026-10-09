#pragma fp_contract(off)

#include "LensFlare.h"

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../eagl/GeoPrimState.h"
#include "../eagl/RenderContext.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"         // TransformPoint
#include "../platform/X87.h"
#include "Draw.h"
#include "Lights.h"
#include "Materials.h"
#include "RGlareManager.hpp"
#include "Renderer.h"
#include "TextureContext.h"

// ---------------------------------------------------------------------------------------------------------------
// RLensFlareManager (0x0009e150-0x0009e8e0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals
#define ViewProjectionMatrixPointer (*(const MATRIX4 **)0x001caf5c)
#define FlareTestDepth FLOAT_AT(0x001c4a84)             // 9.9999: the test sprites' depth (name ours)

namespace {

constexpr uintptr_t kRLensFlareManagerVtable = 0x00192bf4;
constexpr uintptr_t kUSingletonVtable = 0x0018beb0;
constexpr uint32_t kTextureMoon = 0x6e6f6f6d;   // 'moon'
constexpr uint32_t kTextureSunf = 0x666e7573;   // 'sunf'
constexpr uint32_t kDepthLess = 0x201;
constexpr int kSunSpriteSize = 16;
constexpr uint32_t kSpriteColour = 0xff7f7f7f;
constexpr int32_t kFlareGlareType = 0x25;

EAGL::RenderContextExtension *Extension(EAGL::RenderContext *context) {
    return reinterpret_cast<EAGL::RenderContextExtension *>(context);
}

} // namespace

// FUNC_AT(0x0009e460)
RLensFlareManager* RLensFlareManager::Construct() {
    vtable = reinterpret_cast<const void *>(kRLensFlareManagerVtable);
    data = static_cast<LensFlareData *>(OperatorNew(sizeof(LensFlareData)));
    data->texture = TheTextureContextManager->FindOrCreateTexture(kTextureMoon, 4);
    if (data->texture == NULL) {
        data->sun = 1;
        data->texture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kTextureSunf, 0);
    } else {
        data->sun = 0;
    }
    data->testIndex = 0;
    Reset();
    return this;
}

// FUNC_AT(0x0009e1b0)
void RLensFlareManager::Destruct() {
    vtable = reinterpret_cast<const void *>(kRLensFlareManagerVtable);
    OperatorDelete(data);
    vtable = reinterpret_cast<const void *>(kUSingletonVtable);
}

// FUNC_AT(0x0009e520)
RLensFlareManager* RLensFlareManager::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// The instance is not cleared, as the original has it.
// FUNC_AT(0x0009e1d0)
void RLensFlareManager::Kill() {
    RLensFlareManager *manager = TheLensFlareManager;
    if (manager != NULL)
        (manager->*XbeVirtual<decltype(&RLensFlareManager::Delete)>(manager, 0))(1);
}

// FUNC_AT(0x0009e150)
void RLensFlareManager::Reset() {
    for (int i = 0; i < LensFlareData::kFrames; i++)
        data->frames[i] = i;
    for (int i = 0; i < LensFlareData::kFrames; i++)
        data->counts[i] = 0;
    for (int i = 0; i < LensFlareData::kTests; i++)
        data->testPending[i] = 0;
}

// FUNC_AT(0x0009e1f0)
void RLensFlareManager::Enable(bool enable) {
    data->enabled = enable;
}

// FUNC_AT(0x0009e200)
void RLensFlareManager::EndFrame() {
    int32_t oldest = data->frames[2];
    data->frames[2] = data->frames[1];
    data->frames[1] = data->frames[0];
    data->frames[0] = oldest;
    data->counts[data->frames[0]] = 0;
}

// FUNC_AT(0x0009e250)
void RLensFlareManager::AddFlare(const Coord4 *position, uint32_t unknown) {
    if (!data->enabled)
        return;
    int32_t frame = data->frames[0];
    if (data->counts[frame] >= LensFlareData::kMaxFlares)
        return;
    LensFlare *flare = &data->flares[frame][data->counts[frame]];
    fgRenderer->renderContext->GetCurrentViewPort();
    Coord4 projected;
    TransformPoint(ViewProjectionMatrixPointer, position, &projected);
    if (!(projected.z >= 0.01f))
        return;

    // Projected to the screen, y down. The product for y is rounded to a float first, as the original stores it.
    double inverse = 1.0 / projected.z;
    double screenX = 0.5 * inverse * projected.x + 0.5;
    float scaledY = float(inverse * -0.5 * projected.y);
    double screenY = scaledY + 0.5;
    float width = float(fgRenderer->screenWidth);
    float height = float(fgRenderer->screenHeight);
    float x = float(double(fgRenderer->screenWidth) * screenX);
    float y = float(double(fgRenderer->screenHeight) * screenY);
    if (data->sun) {
        // Inside the screen with four pixels to spare
        if (!(x > 4.0f) || !(width - 5.0 > x) || !(y > 4.0f) || !(height - 5.0 > y))
            return;
    } else {
        // Up to fifteen pixels off it
        if (!(x > -15.0f) || !(width + 15.0 > x) || !(y > -15.0f) || !(height + 15.0 > y))
            return;
    }
    flare->x = x;
    flare->y = y;
    flare->depth = projected.z;
    flare->unknown10 = unknown;
    data->counts[data->frames[0]]++;
}

// FUNC_AT(0x0009e720)
void RLensFlareManager::TestFlares(RViewCamera *) {
    if (!data->enabled)
        return;
    for (uint32_t i = 0; i < data->counts[data->frames[0]]; i++) {
        LensFlare *flare = &data->flares[data->frames[0]][i];
        float depth = FlareTestDepth;
        Draw::SetZDepth(depth);
        flare->depth = depth;
        int size = kSunSpriteSize;
        if (!data->sun)
            size = fgLightManager->moonSize;
        int x = Ftol(flare->x);
        int y = Ftol(flare->y);
        EAGL::GeoPrimState *state = SimpleTexturedMaterial;
        Draw::SetAdditiveBlendMode(state);     // the original pushes 0x7f too, which it does not read
        state->SetDepthTestMethod(kDepthLess);
        Extension(fgRenderer->renderContext)->BeginVisibilityTest();
        Draw::DrawSprite(float(x - size / 2), float(y - size / 2), float(size), float(size), kSpriteColour,
                         data->texture);
        Draw::SetNormalBlendMode(state);
        if (++data->testIndex >= LensFlareData::kTests)
            data->testIndex = 0;
        Extension(fgRenderer->renderContext)->EndVisibilityTest(data->testIndex);
        data->testPending[data->testIndex] = 1;
    }
}

// The original counts with the frame before last's flares but reads this frame's, and reads the last test's
// result for every flare (only the first can find it pending): kept.
// FUNC_AT(0x0009e540)
void RLensFlareManager::DrawFlares() {
    if (!data->enabled)
        return;
    if (!data->sun) {
        EndFrame();
        return;
    }
    for (uint32_t i = 0; i < data->counts[data->frames[2]]; i++) {
        const LensFlare *flare = &data->flares[data->frames[0]][i];
        uint32_t visible = 0;
        int test = data->testIndex - 1;
        if (test < 0)
            test = LensFlareData::kTests - 1;
        if (data->testPending[test]) {
            while (!Extension(fgRenderer->renderContext)->GetVisibilityTestResult(test, &visible)) {
            }
        }
        data->testPending[test] = 0;
        // Brighter for every visible pixel past 256
        float intensity = float((double(visible) - 256.0f) * (1.0f / 256.0f));
        if (intensity > 0.0f) {
            // Fields the original leaves as stack garbage are zero here
            Glare glare = {};
            glare.position.x = flare->x;
            glare.position.y = flare->y;
            glare.position.z = 0.5f;
            glare.w = 1.0f;
            glare.flags = GLARE_DIRECTIONAL;
            glare.type = kFlareGlareType;
            glare.rangeOrIntensity = float(double(intensity) * 0.7f + 0.3f);
            TheGlareManager->AddGlare(&glare, 0);
        }
    }
    TheGlareManager->DrawGlares(false);
    EndFrame();
}
