#pragma fp_contract(off)

#include "SkyWater.h"

#include <bit>
#include <string.h>

#include "Draw.h"
#include "Fog.h"
#include "LensFlare.h"
#include "Lights.h"                       // RLightManager, fgLightManager
#include "Materials.h"
#include "Renderer.h"                     // fgRenderer
#include "RenderHigh.h"
#include "TextureContext.h"
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../camera/Camera.h"             // RCamera, RViewCamera
#include "../camera/PlayerCamera.h"
#include "../data/Carp.h"                 // CARP::Instance
#include "../data/DebugVariables.h"
#include "../eagl/GeoPrimState.h"
#include "../eagl/RenderContext.h"
#include "../engine/CoreContainers.h"     // CollisionInstanceMap
#include "../engine/CoreFoundation.h"     // ThrowLengthError
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../world/CollisionInstance.h"   // ColStl, WCollisionInstance
#include "../world/Render.h"              // fgRender, kInstanceSky

// ---------------------------------------------------------------------------------------------------------------
// RSky::Draw (0x000a6690), RWater, RWindow and the code between them (0x000a8460-0x000a8f10), ported from the
// listing.
// ---------------------------------------------------------------------------------------------------------------

class RTextureContext;

// ---- globals

// The sun's offset from the eye, worked out by the first RSky::Draw
#define SunOffset (*(Coord3 *)0x00201800)
#define SunPlaced BOOL8_AT(0x00201810)

#define WaterLayer U32_AT(0x0020184c)                       // "Water layer"

// The window batch: its vertex arrays, its count, and the texture
#define WindowTexture (*(EAGL::TAR **)0x00201850)
#define WindowUVs ((Coord4 *)0x00201860)
#define WindowPositions ((Coord4 *)0x00202060)
#define WindowVertexCount I32_AT(0x00202860)
#define WindowColours ((uint32_t *)0x00202870)
#define WindowColoursCleared U32_AT(0x00202a70)            // the colours' function-local static guard

// The debris tables ScaleDebrisTables scales: two lists of vectors, each scaled into a copy
#define DebrisVectorsA ((const Coord4 *)0x001c59e0)
#define DebrisScaledA ((Coord4 *)0x001c6a10)
#define DebrisScaleA FLOAT_AT(0x001c5b84)
#define DebrisVectorsB ((const Coord4 *)0x001c5d80)
#define DebrisScaledB ((Coord4 *)0x001c5f60)
#define DebrisCountB I32_AT(0x001c8e44)

static const USingletonVtable *const kWaterVtable = (const USingletonVtable *)0x001930b8;

constexpr float kTurnsPerDegree = 1.0f / 360.0f;
static_assert(std::bit_cast<uint32_t>(kTurnsPerDegree) == 0x3b360b61, "1/360 as the original's constant");
constexpr float kSunDistance = 1500.0f;
constexpr uint32_t kWindowTexture = 0x77646e77;     // 'wndw'
constexpr uint32_t kCrackColour = 0x80ffffff;       // white, half transparent
constexpr int kWindowBatch = 0x78;                  // the batch is drawn before a pane would take it past this
constexpr int kDebrisCountA = 26;
constexpr float kDebrisScaleB = 2.5f;
constexpr int kWindowVertices = 0x80;               // the batch's arrays

// GeoPrimState's depth comparison, in OpenGL's numbering
constexpr uint32_t kDepthLessEqual = 0x203;

// A crack's texture coordinates by its kind (WindowHit::kind): the texture's eight quarter-height cells, the left
// column for kinds 0-3, the right for 4-7, down from the top
static const Coord4 kCrackUVs[8][4] = {
    { { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.5f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.25f, 1.0f, 1.0f }, { 0.5f, 0.25f, 1.0f, 1.0f } },
    { { 0.0f, 0.25f, 1.0f, 1.0f }, { 0.5f, 0.25f, 1.0f, 1.0f }, { 0.0f, 0.5f, 1.0f, 1.0f }, { 0.5f, 0.5f, 1.0f, 1.0f } },
    { { 0.0f, 0.5f, 1.0f, 1.0f }, { 0.5f, 0.5f, 1.0f, 1.0f }, { 0.0f, 0.75f, 1.0f, 1.0f }, { 0.5f, 0.75f, 1.0f, 1.0f } },
    { { 0.0f, 0.75f, 1.0f, 1.0f }, { 0.5f, 0.75f, 1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f, 1.0f }, { 0.5f, 1.0f, 1.0f, 1.0f } },
    { { 0.5f, 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 1.0f, 1.0f }, { 0.5f, 0.25f, 1.0f, 1.0f }, { 1.0f, 0.25f, 1.0f, 1.0f } },
    { { 0.5f, 0.25f, 1.0f, 1.0f }, { 1.0f, 0.25f, 1.0f, 1.0f }, { 0.5f, 0.5f, 1.0f, 1.0f }, { 1.0f, 0.5f, 1.0f, 1.0f } },
    { { 0.5f, 0.5f, 1.0f, 1.0f }, { 1.0f, 0.5f, 1.0f, 1.0f }, { 0.5f, 0.75f, 1.0f, 1.0f }, { 1.0f, 0.75f, 1.0f, 1.0f } },
    { { 0.5f, 0.75f, 1.0f, 1.0f }, { 1.0f, 0.75f, 1.0f, 1.0f }, { 0.5f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
};

// ---- RSky

// FUNC_AT(0x000a6690)
void RSky::Draw(CARP::Instance *instances, bool skipSun) {
    Fog->DisableFog();
    fgLightManager->DisablePositionalLighting(true);
    if (!skipSun)
        fgRenderer->renderContext->SetZWritesEnable(0);
    const Coord3 *eye = reinterpret_cast<const Coord3 *>(fgRenderer->currentView->camera->matrix.mtx[3]);
    for (CARP::Instance *sky = instances; sky->flags & kInstanceSky; sky++) {
        sky->position[0] = eye->x;
        sky->position[1] = eye->y;
        sky->position[2] = eye->z;
        QuickDrawInstance(sky, fgRender->procAnims);
    }
    fgLightManager->DisablePositionalLighting(false);
    if (!skipSun) {
        fgRenderer->renderContext->SetZWritesEnable(1);
        if (!SunPlaced) {
            RLightAngles angles = fgLightManager->angles[0];
            float turns = (float)(angles.y[0] * kTurnsPerDegree);
            SunOffset.x = (float)(SineTurns(turns) * kSunDistance);
            SunOffset.y = fgLightManager->sunHeight;
            SunOffset.z = (float)(CosineTurns(turns) * kSunDistance);
            SunPlaced = true;
        }
        Coord4 sun = { SunOffset.x + eye->x, SunOffset.y + eye->y, SunOffset.z + eye->z, 1.0f };
        TheLensFlareManager->AddFlare(&sun, 0);
    }
    Fog->EnableFog();
}

// ---- RWater

// FUNC_AT(0x000a8460)
RWater* RWater::Construct() {
    singleton.vtable = kWaterVtable;
    return this;
}

// FUNC_AT(0x000a8470)
void RWater::Kill() {
    RWater *water = TheWater;
    if (water != NULL) {
        typedef RWater *(RWater::*DeletingDestructor)(unsigned flags);
        (water->*XbeVirtual<DeletingDestructor>(water, 0))(1);
    }
}

// FUNC_AT(0x000a8490)
void RWater::LoadAttributes() {
    const float scroll[kWaterLayers][2] = { { 0.01f, 0.01f }, { -0.01f, 0.01f }, { -0.01f, -0.01f }, { 0.01f, -0.01f } };
    for (int layer = 0; layer < kWaterLayers; layer++) {
        scales[layer].x = 0.01f;
        scales[layer].y = 0.01f;
        scales[layer].z = 0.0f;
        scales[layer].w = 0.0f;
        scrolls[layer].x = scroll[layer][0];
        scrolls[layer].y = scroll[layer][1];
        scrolls[layer].z = 0.0f;
        scrolls[layer].w = 0.0f;
    }
    sunDirection = 157.0f;
    dbindex("Water layer", &WaterLayer, 0, kWaterLayers - 1, NULL);
    dbattrib_float("Water scale X", &scales[0].x, 0.0f, 0.05f, sizeof(Coord4), 1.0f, NULL);
    dbattrib_float("Water scale Y", &scales[0].y, 0.0f, 0.05f, sizeof(Coord4), 1.0f, NULL);
    dbattrib_float("Water scroll U", &scrolls[0].x, 0.0f, 0.05f, sizeof(Coord4), 1.0f, NULL);
    dbattrib_float("Water scroll V", &scrolls[0].y, 0.0f, 0.05f, sizeof(Coord4), 1.0f, NULL);
    dbendindex();
    dbattrib_float("Water sun dir", &sunDirection, 0.0f, 1024.0f, 0, 1.0f, NULL);
    BuildRotate(&sunRotation, sunDirection, 0.0f, 1.0f, 0.0f);
}

// ---- RWindow

// FUNC_AT(0x000a8780)
void RWindow::Init() {
    WindowTexture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kWindowTexture, 0);
}

// FUNC_AT(0x000a87a0)
WindowPaneNode* WindowPaneMap::Min(WindowPaneNode *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

// FUNC_AT(0x000a87c0)
void InstanceList::Deallocate(InstanceListEntry *first, uint32_t count) {
    if (first != NULL)
        UMemory::FastFree(first, count * sizeof(InstanceListEntry));
}

// AUTOLTCG
__declspec(naked) void FUN_000a87e0() {
    __asm {
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push eax
        call QueueBrokenWindowQuad
        add esp, 0x1c
        ret
    }
}

void QueueBrokenWindowQuad(const Coord4 *a, const Coord4 *b, const Coord4 *c, const Coord4 *d, uint32_t colour,
                           const Coord4 *uvs, bool flush) {
    if ((WindowColoursCleared & 1) == 0) {
        WindowColoursCleared |= 1;
        memset(WindowColours, 0, kWindowVertices * sizeof(uint32_t));
    }
    int count = WindowVertexCount;
    if (flush || count > kWindowBatch) {
        if (count > 0) {
            TexturedGeoPrim *request = TexturedRequests[TexturedRequestIndex];
            request->positions.SetData(WindowPositions);
            request->colours.SetData(WindowColours);
            request->texCoords.SetData(WindowUVs);
            request->texture.SetData(WindowTexture);
            SimpleTexturedMaterial->Draw(kTriangleList, count, NULL);
            count = 0;
            WindowVertexCount = 0;
        }
        if (flush)
            return;
    }

    // (a, b, c) and (b, c, d), the texture's corners in the same order
    const Coord4 *vertices[6] = { a, b, c, b, c, d };
    const int corners[6] = { 0, 1, 2, 1, 2, 3 };
    for (int i = 0; i < 6; i++) {
        WindowPositions[count] = *vertices[i];
        WindowUVs[count] = uvs[corners[i]];
        WindowColours[count] = colour;
        count++;
    }
    WindowVertexCount = count;
}

// FUNC_AT(0x000a8a80)
void WindowPaneIterator::Increment() {
    if (node->isNil)
        return;
    WindowPaneNode *right = node->right;
    if (!right->isNil) {
        node = WindowPaneMap::Min(right);
        return;
    }
    WindowPaneNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    node = parent;
}

// FUNC_AT(0x000a8b50)
void InstanceList::Tidy() {
    ColStl::Tidy(this);
}

// FUNC_AT(0x000a8b90)
void InstanceList::Xlen() {
    SKYWATER_UNTESTED("the 8-byte vectors' _Xlen");
    ThrowLengthError("vector<T> too long");
}

// FUNC_AT(0x000a8c10)
void RWindow::DrawBrokenWindows(float radius) {
    SimpleMaterial->SetDepthTestMethod(kDepthLessEqual);
    SimpleTexturedMaterial->SetDepthTestMethod(kDepthLessEqual);
    Draw::SetNormalBlendMode(SimpleMaterial);
    Draw::SetNormalBlendMode(SimpleTexturedMaterial);

    const Coord3 *eye = reinterpret_cast<const Coord3 *>(fgRenderHigh->views[0].view->camera->matrix.mtx[3]);
    InstanceList instances = {};
    fgCollisionMgr->GetInstanceList(&instances, eye, radius, false, false);
    for (InstanceListEntry *entry = instances.first; entry != instances.last; entry++) {
        WCollisionInstance *instance = entry->instance;
        // The instance's frame and its inverse are made and not used
        alignas(16) MATRIX4 frame, inverse;
        instance->MakeMatrix(&frame, true);
        inverse = frame;
        OrthoInverse(&inverse);

        WindowMap *windows = &fgCollisionMgr->windows;
        CollisionInstanceNode *found;
        uint32_t key = uint32_t(uintptr_t(instance));
        reinterpret_cast<CollisionInstanceMap *>(windows)->Find(&found, &key);
        WindowMapNode *hits = reinterpret_cast<WindowMapNode *>(found);
        if (hits == windows->head)
            continue;
        WindowPaneMap *panes = &hits->value.panes;
        WindowPaneIterator pane;
        for (pane.node = panes->head->left; pane.node != panes->head; pane.Increment()) {
            // The pane's other two corners: each corner's x and z with the other's height
            const WindowHit *hit = &pane.node->value.hit;
            Coord4 lower = { hit->corner0.x, hit->corner1.y, hit->corner0.z, hit->corner0.w };
            Coord4 upper = { hit->corner1.x, hit->corner0.y, hit->corner1.z, hit->corner1.w };
            QueueBrokenWindowQuad(&hit->corner0, &lower, &upper, &hit->corner1, kCrackColour, kCrackUVs[hit->kind],
                                  false);
        }
    }
    QueueBrokenWindowQuad(NULL, NULL, NULL, NULL, 0, NULL, true);
    if (instances.first != NULL)
        UMemory::FastFree(instances.first, uint32_t(instances.end - instances.first) * sizeof(InstanceListEntry));
}

// FUNC_AT(0x000a8e80)
void ScaleDebrisTables() {
    for (int i = 0; i < kDebrisCountA; i++) {
        DebrisScaledA[i].x = DebrisScaleA * DebrisVectorsA[i].x;
        DebrisScaledA[i].y = DebrisScaleA * DebrisVectorsA[i].y;
        DebrisScaledA[i].z = DebrisScaleA * DebrisVectorsA[i].z;
    }
    for (int i = 0; i < DebrisCountB; i++) {
        DebrisScaledB[i].x = DebrisVectorsB[i].x * kDebrisScaleB;
        DebrisScaledB[i].y = DebrisVectorsB[i].y * kDebrisScaleB;
        DebrisScaledB[i].z = DebrisVectorsB[i].z * kDebrisScaleB;
    }
}
