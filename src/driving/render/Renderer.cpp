#pragma fp_contract(off)

#include "Renderer.h"

#include <stdio.h>
#include <bit>

#include "DebugView.h"
#include "Draw.h"                         // SimpleDraw
#include "Fog.h"
#include "Lights.h"                       // RLightManager, fgLightManager
#include "Materials.h"                    // InitAllSimpleMaterials
#include "Reflection.h"
#include "RGlareManager.hpp"
#include "RSceneObj.hpp"
#include "TextureContext.h"
#include "TimeData.h"
#include "../../helpers.h"
#include "../anim/Character.h"            // RSceneObjBrightness
#include "../anim/ProcAnim.h"             // GetInstanceMatrix
#include "../camera/Camera.h"             // RViewCamera, RCamera
#include "../camera/DirectorQueue.h"      // ReverseDrawList, the vector's other members
#include "../eagl/EaglFont.h"             // FNTXFont
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GameCallbacks.h"        // EAGL_allocator, EAGL_deallocator
#include "../eagl/Model.h"                // EAGL::Model
#include "../eagl/Realgraph.h"            // FONT_*, SHAPE_*
#include "../eagl/RenderContext.h"
#include "../eagl/View.h"                 // EAGL::Device, EAGL::ViewPort
#include "../engine/CoreFoundation.h"     // NullFunction, GetFoundationVideoMode, ThrowLengthError
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../gfx/D3D8.h"
#include "../physics/PhysicsObject.h"
#include "../platform/FileSys.h"          // FILE_save
#include "../platform/RealMath.h"
#include "../platform/RealMemory.h"       // MEM_free
#include "../platform/RealPrint.h"        // MEM_copy, MEM_fill, REAL_exit
#include "../platform/RealSystem.h"       // CPU_detect
#include "../platform/X87.h"              // Ftol
#include "../platform/XboxXapi.h"         // Xbox_XGetVideoFlags
#include "../world/Render.h"              // kInstanceNoFarCull
#include "../world/World.h"               // fgWorld, ArticleEffect, kWorldInstanceSceneObj

// ---------------------------------------------------------------------------------------------------------------
// RDrawGroup, RRenderer, the instance draws, the shadow maps and RRenderSharedData (0x0007c9e0..0x0007e430 but RFog,
// Fog.cpp), ported from the listing. The x87 code keeps the original's order and roundings.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

#define ViewWidth I32_AT(0x001f2d7c)                  // the screen's size in pixels
#define ViewHeight I32_AT(0x001f2d80)
#define ScreenWidthCopy I32_AT(0x00241c0c)            // copies the constructor makes (names ours)
#define ScreenHeightCopy I32_AT(0x00241c10)
#define WideScreenOption I32_AT(0x002441a4)           // either makes ConfigureRes choose 16:9 (names ours)
#define WideScreenSwitch U8_AT(0x001e4762)
#define VBlankSyncOff U8_AT(0x001e4764)               // with kLaunchNoSync, the frames are not synced (names ours)
#define LaunchFlags U32_AT(0x00244530)
#define LoaderReady U8_AT(0x00244044)                 // the first byte of "LOADER READY"; with the next, no capture file
#define Unknown2445b0 U32_AT(0x002445b0)
#define CaptureCount I32_AT(0x001ec000)
#define DebugFontDriver ((void *)0x001cd114)          // EAGL's FONT driver table
#define ModelGlaresEnabled U8_AT(0x001c3ff0)          // 1: DrawGroupDrawEffects queues the articles' glares
#define RenderTypeSetups ((RenderTypeSetup *)0x001c3ff4)  // by WorldArticle::RenderType, five
#define InstanceOffset (*(const Coord4 *)0x001d4c00)  // (0, 0, 0, 1), a static initialiser's
#define ObjectBrightness FLOAT_AT(0x001c4044)         // SendPerCarInstance's, 1 to start with
#define VehiclesAllowed (*(bool *)0x001ec220)
#define CurrentObject (*(RSceneObj **)0x001ec224)     // the scene object the lighting is set up for

// ---- the game's code not ported yet

#define RVehicle_SetPerObjectBuffers ((void (__fastcall *)(RSceneObj *, int))0x00095ff0)
#define RVehicle_ClearSharedBuffers ((void (*)(void))0x00095970)
#define CRT_printf ((int (*)(const char *format, ...))0x00132192)
#define CRT_sprintf ((int (*)(char *buffer, const char *format, ...))0x00132767)

namespace {

// The track's shadow maps (0x0023ec010..0x001ec220 as one record; the name is ours)
struct ShadowMapData {
    int32_t count;                      // +0x000 RInstanceRender_SetShadowInformation's; the default's 1
    uint32_t unknown004;
    ShadowMapImage *images[64];         // +0x008 by ShadowMapInfo::image; images[0] NULL: no shadow maps
    void *defaultTexture;               // +0x108 RInstanceRender_SetDefaultShadowInformation's
    uint32_t unknown10C[63];
    RTextureContext *context;      // +0x208 the track's _S.xsh
    const ShadowMapInfo *info;          // +0x20c by the instances' shadow record index
};
static_assert(sizeof(ShadowMapData) == 0x210, "ShadowMapData layout");

#define ShadowMaps (*(ShadowMapData *)0x001ec010)
#define ShadowImageName ((char *)0x001ec244)          // "%04d", what SHAPE_locatez is handed
#define DefaultShadowInfo (*(ShadowMapInfo *)0x001ec250)

// A TGA file's header (18 bytes)
#pragma pack(push, 1)
struct TgaHeader {
    uint8_t idLength;
    uint8_t colourMapType;
    uint8_t imageType;                  // 2: uncompressed true colour
    uint8_t colourMap[5];
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t bitsPerPixel;
    uint8_t descriptor;                 // 0x20: the first row is the top one
};
#pragma pack(pop)
static_assert(sizeof(TgaHeader) == 18, "a TGA header is 18 bytes");

constexpr uint32_t kVideoFlagsWidescreen = 0x01;            // XC_VIDEO_FLAGS_WIDESCREEN
constexpr uint32_t kLaunchNoSync = 0x20;
constexpr uint32_t kMultiSampleSuperSampleHorizontal = 0x1121;   // D3DMULTISAMPLE_2_SAMPLES_SUPERSAMPLE_HORIZONTAL_LINEAR
constexpr uint32_t kRenderMaskRgba = 0x01010101;
constexpr uint32_t kRenderMaskRgb = 0x00010101;
constexpr uint32_t kLockReadOnly = 0x40;                    // D3DLOCK_READONLY

constexpr int kListPass = 2;                                // the pass the instance lists draw
constexpr uint8_t kSceneObjVehicle = 0x10;                  // RSceneObj::flags: a vehicle

constexpr float kDimensionSteps[2] = {0.25f, 16.0f};        // CARP::Instance's packed size, by bit 30
constexpr float kFixedOne = 65536.0f;
constexpr float kFixedToFloat = 1.0f / 65536.0f;
constexpr float kSampleScale = 1.0f / 14.0f;
constexpr float kShadowlessBrightness = 0.7f;
constexpr float kDefaultObjectBrightness = 0.4f;
constexpr float kSpecularBase = 0.7f;
constexpr float kSpecularScale = 0.3f;
constexpr float kBrightnessScale = 1.0f / 255.0f;
static_assert(std::bit_cast<uint32_t>(kSampleScale) == 0x3d924925, "1/14");
static_assert(std::bit_cast<uint32_t>(kShadowlessBrightness) == 0x3f333333, "0.7");
static_assert(std::bit_cast<uint32_t>(kDefaultObjectBrightness) == 0x3ecccccd, "0.4");
static_assert(std::bit_cast<uint32_t>(kSpecularScale) == 0x3e99999a, "0.3");
static_assert(std::bit_cast<uint32_t>(kBrightnessScale) == 0x3b808081, "1/255");

constexpr uint32_t kDefaultShadowTexture = 0x6c666564;      // "defl" in memory order

inline void RenderUntested(const char *what) {
    printf("[render] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define RENDER_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            RenderUntested(what); \
        } \
    } while (0)

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// RDrawGroup and its vector
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0007c9e0)
void RDrawGroup::FlushDrawLists() {
    for (ReverseDrawEntry *draw = translucent->first; draw != translucent->last; draw++)
        DrawGroupDrawInstance(&draw->transform, draw->instance, draw->distance, draw->sceneObj, 0);
    translucent->Tidy();
}

// FUNC_AT(0x0007cae0)
void RDrawGroup::Destruct() {
    ReverseDrawList *list = translucent;
    if (list != NULL) {
        list->Tidy();
        UMemory::FastFree(list, sizeof(ReverseDrawList));
    }
}

// FUNC_AT(0x0007cf40)
RDrawGroup* RDrawGroup::Construct() {
    ReverseDrawList *list =
        static_cast<ReverseDrawList *>(UMemory::FastAlloc(sizeof(ReverseDrawList), "RReverseDrawList"));
    if (list != NULL) {
        list->first = NULL;
        list->last = NULL;
        list->end = NULL;
    }
    translucent = list;
    return this;
}

// FUNC_AT(0x0007cfa0)
void RDrawGroup::AddToTranslucentList(const ReverseDrawEntry *draw) {
    translucent->PushBack(draw);
}

// ---------------------------------------------------------------------------------------------------------------
// RRenderer
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0007cfb0)
void RRenderer::ConfigureRes() {
    bool wide = WideScreenOption != 0 || WideScreenSwitch != 0;
    if (Xbox_XGetVideoFlags() & kVideoFlagsWidescreen)
        wide = true;
    screenWidth = 640;
    GetFoundationVideoMode();
    screenHeight = 480;
    videoMode = kVideoNormal;
    widescreen = wide;
    unknown50 = 1.0f;
    unknown54 = 1.0f;
    presentFlag100 = 0;
    if (wide)
        videoMode = kVideoWide;
}

// FUNC_AT(0x0007d020)
void RRenderer::FlushDrawLists() {
    currentDrawGroup->FlushDrawLists();
}

// FUNC_AT(0x0007d030)
RViewCamera* RRenderer::EndView() {
    RViewCamera *view = currentView;
    currentView = NULL;
    return view;
}

// FUNC_AT(0x0007d060)
void RRenderer::StartFrame() {
    renderContext->BeginFrame();
}

// FUNC_AT(0x0007d070)
void RRenderer::Flush(bool force) {
    if (framePending || force) {
        for (int i = 0; i < 2; i++) {
            renderContext->BeginFrame();
            renderContext->EndFrame();
        }
        framePending = 0;
    }
}

// FUNC_AT(0x0007d0b0)
bool RRenderer::EnableAlphaWrites() {
    if (alphaWrites)
        return true;
    alphaWrites = 1;
    fgRenderer->renderContext->Extension()->SetRenderMask(kRenderMaskRgba);
    return false;
}

// FUNC_AT(0x0007d0e0)
bool RRenderer::DisableAlphaWrites() {
    if (!alphaWrites)
        return false;
    alphaWrites = 0;
    fgRenderer->renderContext->Extension()->SetRenderMask(kRenderMaskRgb);
    return true;
}

// The back buffer is twice the screen's width (the horizontal supersampling): each pair of pixels is averaged into
// one of the file's.
// FUNC_AT(0x0007d110)
void RRenderer::DoScreenCapture() {
    char name[0x40];
    CRT_sprintf(name, "capture.%03d.tga", CaptureCount++);
    float width, height;
    renderContext->GetSize(&width, &height);
    int pixels = Ftol((double)width * height);
    uint8_t *samples = static_cast<uint8_t *>(UMemory::Alloc(pixels * 8, 0, "snapshot"));
    MEM_fill(samples, 0, pixels * 8);
    D3DLockedRect locked;
    D3DSurface_LockRect(D3DDevice_GetBackBuffer2(0), &locked, NULL, kLockReadOnly);
    MEM_copy(samples, locked.bits, pixels * 8);

    uint8_t *file = static_cast<uint8_t *>(UMemory::Alloc((Ftol(width) * Ftol(height) + 6) * 3, 0, "tga file"));
    MEM_fill(file, 0, sizeof(TgaHeader));
    TgaHeader *header = reinterpret_cast<TgaHeader *>(file);
    header->imageType = 2;
    header->width = Ftol(width);
    header->height = Ftol(height);
    header->descriptor = 0x20;
    header->bitsPerPixel = 24;
    const uint8_t *source = samples;
    uint8_t *out = file + sizeof(TgaHeader);
    for (int i = 0; i < pixels; i++, source += 8, out += 3) {
        out[0] = (source[0] + source[4]) >> 1;
        out[1] = (source[1] + source[5]) >> 1;
        out[2] = (source[2] + source[6]) >> 1;
    }
    int size = (Ftol(width) * Ftol(height) + 6) * 3;
    if (!LoaderReady || Unknown2445b0 == 0)
        FILE_save(name, file, size);
    UMemory::Free(file);
    UMemory::Free(samples);
}

// FUNC_AT(0x0007d2a0)
RRenderer* RRenderer::Construct(void *unused1, int unused2, bool unknown1C_) {
    (void)unused1;
    (void)unused2;
    currentView = NULL;
    drawGroups[0].Construct();
    drawGroups[1].Construct();
    currentDrawGroup = &drawGroups[0];
    cpuFeatures = CPU_detect();
    unknown18 = 0;
    unknown1C = unknown1C_;
    screenCapturePending = 0;
    unknown20 = 0;
    frameCount = 0;
    screenWidth = 0;
    screenHeight = 0;
    videoMode = kVideoNormal;
    widescreen = 0;
    presentFlag100 = 0;
    unknown50 = 1.0f;
    unknown54 = 1.0f;
    fieldOfViewScale = 1.0f;
    ps2Mipmap = -128;
    device = NULL;
    renderContext = NULL;
    debugFont = NULL;
    framePending = 0;
    fgRenderer = this;
    ConfigureRes();
    RTimeData::Init();

    EAGL::Device::SetNewOverride(reinterpret_cast<void *>(&EAGL_allocator));
    EAGL::Device::SetDeleteOverride(reinterpret_cast<void *>(&EAGL_deallocator));
    void *memory = EaglMalloc(sizeof(EAGL::Device), "EAGL::Device new");
    device = memory != NULL ? static_cast<EAGL::Device *>(memory)->Construct() : NULL;
    device->Init();
    renderContext = device->NewRenderContext();
    renderContext->SetSize((float)screenWidth, (float)screenHeight);
    renderContext->SetFrontBufferDepth(16);
    renderContext->SetBackBufferDepth(32);
    renderContext->SetZBufferDepth(32);
    if (videoMode != kVideoMode3) {
        renderContext->Extension()->SetMultiSampleType(kMultiSampleSuperSampleHorizontal);
        if (presentFlag100)
            renderContext->Extension()->SetPresentFlag100(1);
    } else if (screenHeight <= 480) {
        renderContext->Extension()->SetMultiSampleType(kMultiSampleSuperSampleHorizontal);
    }
    renderContext->SetupFrameBuffers();

    FeatureManager::Init(screenWidth, screenHeight, 32, 0);
    FeatureManager::SetUserSpecifiedRenderFeatures();
    ScreenWidthCopy = ViewWidth;
    ScreenHeightCopy = ViewHeight;
    FeatureManager::SetTexelsAreOffset(false, false);
    renderContext->SetSyncToVBL(!VBlankSyncOff && !(LaunchFlags & kLaunchNoSync));
    GetFoundationVideoMode();
    InitAllSimpleMaterials();

    // The full-screen viewport: (0, 0) the top left, a pixel a unit, z from -0.5 to -10 mapped to 0..1
    viewPort = renderContext->NewViewPort();
    float width = (float)ViewWidth;
    float height = (float)ViewHeight;
    viewPort->SetShape(0.0f, 0.0f, width, height, 0.01f, 1.0f);
    viewPort->SetOrthographicScreenSpace(1.0f, 0.5f, 10.0f);
    alignas(16) MATRIX4 view;
    alignas(16) MATRIX4 scale;
    BuildTranslate(&view, -1.0f, 1.0f, -0.1f);
    BuildScale(&scale, 2.0f / width, -2.0f / height, -1.0f);
    VU0_MATRIX4_mult(&view, &scale, &view);
    viewPort->SetViewMatrix(view.mtx[0]);
    return this;
}

// FUNC_AT(0x0007d560)
void RRenderer::LoadDebugFont() {
    FONT_installdriver(DebugFontDriver);
    FONT_init();
    debugFontData = UFileLoader::FileLoad("data/render/debug.xfn", 0);
    if (debugFontData == NULL)
        REAL_exit();
    debugFont = (FNTXFont *)FONT_create(static_cast<const uint8_t *>(debugFontData));
    debugFont->colour = 0xffffffff;
}

// FUNC_AT(0x0007d5b0)
void RRenderer::EndFrame() {
    if (screenCapturePending) {
        fgRenderer->DoScreenCapture();
        screenCapturePending = 0;
    }
    renderContext->EndFrame();
    frameCount++;
    framePending = 1;
}

// FUNC_AT(0x0007d5e0)
RRenderer* RRenderer::Init(void *unused1, int unused2, bool unknown1C) {
    if (fgRenderer == NULL) {
        void *memory = UMemory::FastAlloc(sizeof(RRenderer), "RRenderer");
        fgRenderer = memory != NULL ? static_cast<RRenderer *>(memory)->Construct(unused1, unused2, unknown1C) : NULL;
    }
    return fgRenderer;
}

// FUNC_AT(0x0007d670)
void RRenderer::Destruct() {
    ShutdownAllSimpleMaterials();
    FONT_destroy((const uint8_t *)debugFont);
    MEM_free(debugFontData);
    renderContext->DeleteViewPort(viewPort);
    device->DeleteRenderContext(renderContext);
    if (device != NULL) {
        device->Destruct();
        EaglFree(device, sizeof(EAGL::Device));
    }
    drawGroups[1].Destruct();
    drawGroups[0].Destruct();
}

// FUNC_AT(0x0007d710)
void Render_InitLibRender() {
    uint8_t unused[0x70];
    RRenderer::Init(unused, 0, false);
    SimpleDraw::Init();
    RRandom::StartUp(0x2e81a11);
}

// FUNC_AT(0x0007d740)
void RRenderer::Shutdown() {
    NullFunction();
    fgRenderer->renderContext->SetSyncToVBL(0);
    RRenderer *renderer = fgRenderer;
    if (renderer != NULL) {
        renderer->Destruct();
        UMemory::FastFree(renderer, sizeof(RRenderer));
    }
    fgRenderer = NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// The instance draws
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0007daf0)
void RInstanceRender_SetShadowInformation(const ShadowMapInfo *info) {
    ShadowMaps.info = info;
    char path[0x50];
    CRT_sprintf(path, "data\\track\\%s_S.xsh", fgWorld->trackName);
    CRT_printf("Loading shadow textures from [%s]\n", path);
    RTextureContext *context = TheTextureContextManager->NewContext(path, 2);
    ShadowMaps.context = context;
    uint8_t *shapes = context->shapes;
    int count = 0;
    if (shapes != NULL) {
        for (;;) {
            CRT_sprintf(ShadowImageName, "%04d", count);
            uint8_t *image = SHAPE_locatez(shapes, ShadowImageName);
            if (image == NULL)
                break;
            ShadowMaps.images[count] = reinterpret_cast<ShadowMapImage *>(image);
            count++;
        }
    }
    ShadowMaps.count = count;
    CRT_printf("Loaded %d shadow maps\n", count);
}

// FUNC_AT(0x0007dbc0)
void RInstanceRender_SetDefaultShadowInformation() {
    ShadowMaps.info = &DefaultShadowInfo;
    ShadowMaps.defaultTexture =
        RTextureContextManager::GetContext(0)->FindOrCreateTexture(kDefaultShadowTexture, 0);
    ShadowMaps.images[0] = NULL;
    ShadowMaps.count = 1;
}

// FUNC_AT(0x0007dc00)
double GetArticleViewDistance(const CARP::Instance *instance, float radius) {
    const RViewCamera *view = fgRenderer->currentView;
    double distance = (double)vec3distance(MatrixRow(&view->camera->matrix, 3), instance->position) - radius;
    if (distance < 0.0)
        distance = 0.0;
    return distance * view->lodMultiplier;
}

// FUNC_AT(0x0007dc40)
double GetInstanceViewDistance(const CARP::Instance *instance) {
    uint32_t packed = instance->packedDimensions;
    float radius = float(packed & 0x3ff) * kDimensionSteps[(packed >> 30) & 1];
    return GetArticleViewDistance(instance, radius);
}

// Glares (effect type 2) are queued for the glare manager; for a GFX effect with something triggered the original
// works out the transform's rotation and translation and hands them to an empty function.
void DrawArticleEffects(const WorldArticle *article, const MATRIX4 *transform, float distance) {
    for (ArticleEffect *effect = article->effects; effect->type != ArticleEffect::kTypeEnd; effect++) {
        if (!(effect->flags & ArticleEffect::kFlag01))
            continue;
        switch (effect->type) {
        case ArticleEffect::kTypeGlare:
            if (ModelGlaresEnabled)
                TheGlareManager->AddModelGlare(reinterpret_cast<Glare *>(effect), transform, distance * distance);
            break;
        case ArticleEffect::kTypeGfx:
            if (effect->gfx.triggered != NULL) {
                alignas(16) Coord4 rotation;
                alignas(16) Coord4 translation;
                ExtractQuatTrans(transform, &rotation, &translation);
                NullFunction();
            }
            break;
        default:
            CRT_printf("Unhandled effect type: %d\n", effect->type);
            break;
        }
    }
}

// AUTOLTCG
__declspec(naked) void DrawGroupDrawEffects() {
    __asm {
        push dword ptr [esp + 4]
        push edi
        push eax
        call DrawArticleEffects
        add esp, 12
        ret
    }
}

// Beyond the last model's distance nothing is drawn; kInstanceNoFarCull draws the nearest model at any distance.
void DrawArticleModel(CARP::Instance *instance, const WorldArticle *article, const MATRIX4 *transform,
                      float distance, RSceneObj *sceneObj) {
    uint32_t lod = 0;
    if (!(instance->flags & kInstanceNoFarCull)) {
        while (lod < article->lodCount && distance > article->lods[lod].distance)
            lod++;
    }
    if (lod >= article->lodCount || article->lods[lod].model == NULL)
        return;
    RenderTypeSetups[article->renderType](instance, sceneObj);
    bool noFog = (article->renderFlags & WorldArticle::kNoFog) != 0;
    if (noFog)
        Fog->DisableFog();
    article->lods[lod].model->Draw(transform->mtx[0]);
    if (noFog)
        Fog->EnableFog();
}

// AUTOLTCG
__declspec(naked) void DrawGroupDrawModel() {
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push edi
        push edx
        call DrawArticleModel
        add esp, 20
        ret
    }
}

// FUNC_AT(0x0007ddd0)
void DrawGroupDrawInstance(const MATRIX4 *transform, CARP::Instance *instance, uint32_t distance,
                           RSceneObj *sceneObj, int unused) {
    (void)unused;
    if (instance->flags & kWorldInstanceSceneObj)
        return;
    const WorldArticle *article = ArticleOf(instance);
    float viewDistance = (float)((double)distance * kFixedToFloat);
    DrawArticleModel(instance, article, transform, viewDistance, sceneObj);
    if (article->effects != NULL)
        DrawArticleEffects(article, transform, viewDistance);
}

// The kept draw's fourth word is left as the original's stack had it; ours is zero.
// FUNC_AT(0x0007de30)
void DrawInstance(CARP::Instance *instance, const MATRIX4 *transform, ProcAnimState *procAnims, float distance,
                  RSceneObj *sceneObj, int pass) {
    if (instance->flags & kWorldInstanceSceneObj)
        return;
    const WorldArticle *article = ArticleOf(instance);
    alignas(16) MATRIX4 placed;
    if (!GetInstanceMatrix(instance, MatrixRow(transform, 3), &placed, procAnims))
        return;
    VU0_MATRIX4_mult(&placed, &placed, transform);
    if (article->drawPass != pass) {
        ReverseDrawEntry draw;
        draw.transform = placed;
        draw.instance = instance;
        draw.sceneObj = sceneObj;
        draw.distance = Ftol((double)distance * kFixedOne);
        draw.unknown4C = 0;
        fgRenderer->currentDrawGroup->AddToTranslucentList(&draw);
        return;
    }
    DrawArticleModel(instance, article, &placed, distance, sceneObj);
    if (article->effects != NULL)
        DrawArticleEffects(article, &placed, distance);
}

// FUNC_AT(0x0007df10)
void QuickDrawInstance(CARP::Instance *instance, ProcAnimState *procAnims) {
    if (instance->flags & kWorldInstanceSceneObj)
        return;
    const WorldArticle *article = ArticleOf(instance);
    if (article == NULL)
        return;
    alignas(16) MATRIX4 placed;
    if (!GetInstanceMatrix(instance, &InstanceOffset, &placed, procAnims))
        return;
    float distance = (float)GetInstanceViewDistance(instance);
    DrawArticleModel(instance, article, &placed, distance, NULL);
    if (article->effects != NULL)
        DrawArticleEffects(article, &placed, distance);
}

// FUNC_AT(0x0007df90)
void DrawInstanceList(CARP::Instance *instances, const MATRIX4 *transform, ProcAnimState *procAnims, uint32_t count,
                      float distance, RSceneObj *sceneObj) {
    for (uint32_t i = 0; i < count; i++)
        DrawInstance(&instances[i], transform, procAnims, distance, sceneObj, kListPass);
}

// FUNC_AT(0x0007dfe0)
void DrawIndexedInstanceList(CARP::Instance *instances, const uint16_t *indices, const MATRIX4 *transform,
                             ProcAnimState *procAnims, uint32_t count, float distance, RSceneObj *sceneObj) {
    for (uint32_t i = 0; i < count; i++)
        DrawInstance(&instances[indices[i]], transform, procAnims, distance, sceneObj, kListPass);
}

// ---------------------------------------------------------------------------------------------------------------
// The shadow maps
// ---------------------------------------------------------------------------------------------------------------

// (u, v) less half a pixel, clamped to the image less two pixels; u is rounded to a float at each step, v kept
// unrounded.
// FUNC_AT(0x0007e040)
double GetTextureVal(float u, float v, const ShadowMapImage *image) {
    int rowBytes = SHAPE_rowbytes(reinterpret_cast<const uint8_t *>(image));
    float x = u - 0.5f;
    double y = v - 0.5;
    if (x < 0.0f)
        x = 0.0f;
    if (y < 0.0)
        y = 0.0;
    int right = image->width - 2;
    if (x > (double)right)
        x = (float)right;
    float bottom = (float)(image->height - 2);
    if (y > bottom)
        y = bottom;

    int column = Ftol(x);
    int row = Ftol(y);
    float fx = (float)(x - (double)column);
    double fy = y - row;
    const uint8_t *top = image->pixels + row * rowBytes;
    const uint8_t *below = top + rowBytes;
    double upper = top[column] * (1.0 - fx) + top[column + 1] * (double)fx;
    double lower = below[column + 1] * (double)fx + below[column] * (1.0 - fx);
    return ((upper * (1.0 - fy) + lower * fy) - 1.0) * kSampleScale;
}

// ---------------------------------------------------------------------------------------------------------------
// RRenderSharedData
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0007e190)
void RRenderSharedData::Init() {
    VehiclesAllowed = false;
}

// FUNC_AT(0x0007e1a0)
bool RRenderSharedData::SetVehiclesAllowed(bool allowed) {
    bool old = VehiclesAllowed;
    VehiclesAllowed = allowed;
    return old;
}

// FUNC_AT(0x0007e1b0)
void RRenderSharedData::MarkAsDirty() {
    CurrentObject = NULL;
}

// FUNC_AT(0x0007e1c0)
void RRenderSharedData::SendPerViewPort() {
    fgLightManager->AddSpecularLight(MatrixRow(&fgRenderer->currentView->camera->matrix, 2));
    CurrentObject = NULL;
}

// FUNC_AT(0x0007e1f0)
void RRenderSharedData::SendPerWorldInstance() {
    fgLightManager->SetLightingModel(kLightingModel1);
}

// FUNC_AT(0x0007e200)
double RRenderSharedData::GetShadowBrightness(Coord3 position, int instanceIndex) {
    if (ShadowMaps.images[0] == NULL)
        return fgLightManager->lightMapLightingBias * (double)kShadowlessBrightness;
    const ShadowMapInfo *info = &ShadowMaps.info[fgWorld->instances[instanceIndex].unknown2c >> 16];
    const ShadowMapImage *image = ShadowMaps.images[info->image];
    float u = (float)((double)position.x * info->scale + info->offsetU);
    double v = (double)position.z * info->scale + info->offsetV;
    u = (float)(image->width * (double)u);
    float pixelV = (float)(image->height * v);
    return GetTextureVal(u, pixelV, image) * fgLightManager->lightMapLightingBias;
}

// FUNC_AT(0x0007e2b0)
void RRenderSharedData::SendPerObjectInstance(CARP::Instance *instance, RSceneObj *sceneObj) {
    (void)instance;
    fgLightManager->SetLightingModel(kLightingModel0);
    if (sceneObj == CurrentObject)
        return;
    CurrentObject = sceneObj;
    float brightness = sceneObj != NULL ? sceneObj->brightness * kBrightnessScale : kDefaultObjectBrightness;
    fgLightManager->SetSurfaceProperties(brightness, 1.0f, 0.0f);
}

// The scene object's brightness follows the shadow maps under its physics object, when that stands on a face.
// FUNC_AT(0x0007e310)
void RRenderSharedData::SendPerCarInstance(CARP::Instance *instance, RSceneObj *sceneObj) {
    (void)instance;
    if (CurrentObject == sceneObj)
        return;
    CurrentObject = sceneObj;
    TheReflection->SetReflectivity(sceneObj);
    fgLightManager->SetLightingModel(kLightingModel0);
    int renderType = sceneObj->renderTypeIndex;
    if (sceneObj->flags & kSceneObjVehicle)
        RVehicle_SetPerObjectBuffers(sceneObj, 0);
    else
        RVehicle_ClearSharedBuffers();
    PhysicsObject *physics = sceneObj->physics;
    if (physics != NULL && physics->worldPos.valid && physics->worldPos.instance != NULL) {
        int instanceIndex = physics->worldPos.instance->renderIndex;
        float level = (float)GetShadowBrightness(*physics->GetPosition(), instanceIndex);
        sceneObj->SetBrightness(level);
    }
    ObjectBrightness = sceneObj->brightness * kBrightnessScale;
    if (physics != NULL)
        ObjectBrightness = (float)(fgLightManager->VerticalFalloffMultiplier(physics->GetPosition()) *
                                   ObjectBrightness);
    fgLightManager->SetSurfaceProperties(ObjectBrightness, ObjectBrightness, 0.0f);
    TheReflection->SetReflectiveSpecularStrength(renderType,
                                                 (float)(ObjectBrightness * (double)kSpecularScale + kSpecularBase));
}
