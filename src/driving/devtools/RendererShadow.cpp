#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "RendererShadow.h"
#include "FpControl.h"

#include "../camera/Camera.h"
#include "../camera/DirectorQueue.h"
#include "../eagl/Model.h"
#include "../eagl/RenderContext.h"
#include "../physics/PhysicsObject.h"
#include "../render/Draw.h"
#include "../render/Fog.h"
#include "../render/Lights.h"
#include "../render/Materials.h"
#include "../render/Reflection.h"
#include "../render/Renderer.h"
#include "../render/RGlareManager.hpp"
#include "../render/RSceneObj.hpp"
#include "../world/World.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_RENDERERSHADOW=1, from the first simulation tick on the loaded track: the renderer core against the
// originals (0x00075c40-0x00076810, 0x0007c9e0-0x0007e430 and 0x0011bb60-0x0011c5c0 swapped back in for each
// original run), on identical inputs, compared byte for byte. The calls that would draw or change render state -
// the materials' draws, EAGL's model draws and fog and mask setters, the frame bracket, the lighting, reflection,
// vehicle and glare calls of the other render packages, the draw groups' kept draws - go to recording fakes for the
// length of each run (five-byte jumps over the originals' entries and ours), and the logs of both runs are compared.
// The state the functions write (the batches, the rings and their models, the shadow-map and shared-data statics,
// the draw globals, the setup table, the fog's settings) is put back before each run and compared after. A ring
// request left pointing at a draw's own stack arrays is compared as "on the stack": the two sides' frames differ,
// and the arrays' contents are compared by the draw fakes when the draw is made.
//
//   - ColourConvertXBoxToPS2 on edge and random colours.
//   - GetTextureVal on the track's shadow maps at random, edge and NaN coordinates; GetShadowBrightness at random
//     points round the instances, and without shadow maps.
//   - GetArticleViewDistance and GetInstanceViewDistance from random cameras, LOD multipliers, radii and packed sizes.
//   - RFog on copies of the live fog: FadeScale, UpdateScale, SetFogParams, Enable/DisableFog, FogColour with random
//     states and game ticks around the fade's end.
//   - Draw's boxes, sprites and quads with random rectangles, depths and colours; the two batches through random
//     sequences that change texture, pass NULL and overflow; the blend modes on a copy of a material.
//   - The three materials' Draw on copies, at random ring positions, with and without a transform.
//   - The draw group's vector: built by pushes and inserts of random counts at random places (every _Insert_n
//     branch), then flushed (the kept draws drawn for real, on the track's instances) and destroyed by each side's
//     own functions. Our FlushDrawLists has DrawGroupDrawInstance inlined, so a jump over its entry would not catch
//     the port's draws.
//   - QuickDrawInstance over every instance, DrawInstance, the two list draws and DrawGroupDrawInstance with random
//     transforms, distances, passes and flags (copies with bits 0 and 3 flipped).
//   - RRenderSharedData on fake scene objects and physics objects; RRenderer's EndView, Flush, the alpha writes and
//     ConfigureRes on a copy of the renderer.
//
// The constructors, Init/Shutdown, the shadow-map loading, the debug font, the screen capture and the materials'
// rings' Init/Shutdown load or allocate for the game; lockstep runs test them.
//
// One mutation this catches: GetTextureVal keeping v rounded to a float after subtracting the half pixel (the
// original keeps it unrounded) changes the samples at most non-integer v.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x00075c40, 0x00076810, original);
    XbeOriginal_RestoreRange(0x0007c9e0, 0x0007e430, original);
    XbeOriginal_RestoreRange(0x0011bb60, 0x0011c5c0, original);
}

// ---- the originals

typedef uint32_t (*ColourFn)(uint32_t);
typedef double (*TextureValFn)(float, float, const ShadowMapImage *);
typedef double (*ShadowBrightnessFn)(Coord3, int);
typedef double (*ArticleDistanceFn)(const CARP::Instance *, float);
typedef double (*InstanceDistanceFn)(const CARP::Instance *);
typedef void (__fastcall *FogFadeFn)(RFog *, int, float, float);
typedef void (__fastcall *FogFn)(RFog *, int);
typedef uint32_t (__fastcall *FogColourFn)(RFog *, int);
typedef void (*SpriteFn)(float, float, float, float, uint32_t, const SpriteTexCoords *, EAGL::TAR *);
typedef void (*WholeSpriteFn)(float, float, float, float, uint32_t, EAGL::TAR *);
typedef void (*BoxFn)(float, float, float, float, uint32_t);
typedef void (*QuadCFn)(const Vec4 *, const uint32_t *);
typedef void (*QuadFn)(const Vec4 *, uint32_t);
typedef void (*BlendFn)(EAGL::GeoPrimState *);
typedef void (*BatchedQuadFn)(const Vec4 *, const Vec4 *, const Vec4 *, const Vec4 *, uint32_t, const Vec4 *,
                              EAGL::TAR *);
typedef void (*MatrixFn)(MATRIX4 *);
typedef void (*FloatFn)(float);
typedef void (*VoidFn)(void);
typedef PackedColour *(__fastcall *ColourCtorFn)(PackedColour *, int);
typedef void (__fastcall *MaterialDrawFn)(USimpleMaterial *, int, int, int, MATRIX4 *);
typedef RDrawGroup *(__fastcall *GroupCtorFn)(RDrawGroup *, int);
typedef void (__fastcall *GroupFn)(RDrawGroup *, int);
typedef void (__fastcall *GroupAddFn)(RDrawGroup *, int, const ReverseDrawEntry *);
typedef void (__fastcall *InsertNFn)(ReverseDrawList *, int, ReverseDrawEntry *, uint32_t, const ReverseDrawEntry *);
typedef void (*GroupDrawFn)(const MATRIX4 *, CARP::Instance *, uint32_t, RSceneObj *, int);
typedef void (*DrawInstanceFn)(CARP::Instance *, const MATRIX4 *, ProcAnimState *, float, RSceneObj *, int);
typedef void (*QuickDrawFn)(CARP::Instance *, ProcAnimState *);
typedef void (*ListFn)(CARP::Instance *, const MATRIX4 *, ProcAnimState *, uint32_t, float, RSceneObj *);
typedef void (*IndexedListFn)(CARP::Instance *, const uint16_t *, const MATRIX4 *, ProcAnimState *, uint32_t, float,
                              RSceneObj *);
typedef bool (*BoolFn)(bool);
typedef void (*SendFn)(CARP::Instance *, RSceneObj *);
typedef RViewCamera *(__fastcall *EndViewFn)(RRenderer *, int);
typedef void (__fastcall *FlushFn)(RRenderer *, int, bool);
typedef bool (__fastcall *AlphaFn)(RRenderer *, int);
typedef void (__fastcall *RendererFn)(RRenderer *, int);

#define Orig_ColourConvert ((ColourFn)0x00075c40)
#define Orig_GetTextureVal ((TextureValFn)0x0007e040)
#define Orig_GetShadowBrightness ((ShadowBrightnessFn)0x0007e200)
#define Orig_GetArticleViewDistance ((ArticleDistanceFn)0x0007dc00)
#define Orig_GetInstanceViewDistance ((InstanceDistanceFn)0x0007dc40)
#define Orig_FadeScale ((FogFadeFn)0x0007d7b0)
#define Orig_UpdateScale ((FogFn)0x0007d8c0)
#define Orig_SetFogParams ((FogFn)0x0007d840)
#define Orig_EnableFog ((FogFn)0x0007d820)
#define Orig_DisableFog ((FogFn)0x0007d7a0)
#define Orig_FogColour ((FogColourFn)0x0007d790)
#define Orig_DrawSprite ((SpriteFn)0x00075f60)
#define Orig_DrawWholeSprite ((WholeSpriteFn)0x00076250)
#define Orig_DrawBox ((BoxFn)0x000760e0)
#define Orig_DrawQuadC ((QuadCFn)0x000761c0)
#define Orig_DrawQuad ((QuadFn)0x000762d0)
#define Orig_SetNormalBlendMode ((BlendFn)0x00076200)
#define Orig_SetAdditiveBlendMode ((BlendFn)0x00076220)
#define Orig_DrawBatchedTexturedQuad ((BatchedQuadFn)0x00076330)
#define Orig_DrawBatchedSprite ((SpriteFn)0x000765e0)
#define Orig_SetModelMatrix ((MatrixFn)0x00075f30)
#define Orig_SetZDepth ((FloatFn)0x00075f40)
#define Orig_SetZDepthNear ((VoidFn)0x00075f50)
#define Orig_ColourConstruct ((ColourCtorFn)0x00076240)
#define Orig_SimpleDraw ((MaterialDrawFn)0x0011bbf0)
#define Orig_TexturedDraw ((MaterialDrawFn)0x0011bd50)
#define Orig_VolatileDraw ((MaterialDrawFn)0x0011c290)
#define Orig_GroupConstruct ((GroupCtorFn)0x0007cf40)
#define Orig_GroupDestruct ((GroupFn)0x0007cae0)
#define Orig_GroupFlush ((GroupFn)0x0007c9e0)
#define Orig_GroupAdd ((GroupAddFn)0x0007cfa0)
#define Orig_InsertN ((InsertNFn)0x0007cb00)
#define Orig_DrawGroupDrawInstance ((GroupDrawFn)0x0007ddd0)
#define Orig_DrawInstance ((DrawInstanceFn)0x0007de30)
#define Orig_QuickDrawInstance ((QuickDrawFn)0x0007df10)
#define Orig_DrawInstanceList ((ListFn)0x0007df90)
#define Orig_DrawIndexedInstanceList ((IndexedListFn)0x0007dfe0)
#define Orig_SharedInit ((VoidFn)0x0007e190)
#define Orig_SetVehiclesAllowed ((BoolFn)0x0007e1a0)
#define Orig_MarkAsDirty ((VoidFn)0x0007e1b0)
#define Orig_SendPerViewPort ((VoidFn)0x0007e1c0)
#define Orig_SendPerWorldInstance ((VoidFn)0x0007e1f0)
#define Orig_SendPerObjectInstance ((SendFn)0x0007e2b0)
#define Orig_SendPerCarInstance ((SendFn)0x0007e310)
#define Orig_EndView ((EndViewFn)0x0007d030)
#define Orig_Flush ((FlushFn)0x0007d070)
#define Orig_EnableAlphaWrites ((AlphaFn)0x0007d0b0)
#define Orig_DisableAlphaWrites ((AlphaFn)0x0007d0e0)
#define Orig_ConfigureRes ((RendererFn)0x0007cfb0)

// ---- the game's state

#define GameTick U32_AT(0x001f2a4c)
#define ShadowImages ((ShadowMapImage **)0x001ec018)
#define ShadowCount I32_AT(0x001ec010)
#define RenderTypeSetups ((RenderTypeSetup *)0x001c3ff4)
#define DrawZ FLOAT_AT(0x001c3e28)
#define DrawModelMatrix (*(MATRIX4 **)0x001e92d8)
#define CurrentObject (*(RSceneObj **)0x001ec224)

struct Region {
    uint32_t at;
    uint32_t bytes;
};
const Region kRegions[] = {
    {0x001e9420, 0x001eb880 - 0x001e9420},  // the two batches
    {0x001e92d0, 0xc},                      // the simple materials, the model matrix
    {0x001c3e28, 4},                        // the depth
    {0x00243708, 0x002438b8 - 0x00243708},  // the rings
    {0x001ec000, 0x260},                    // capture count ... the shadow maps, the shared data, the default info
    {0x001c4044, 4},                        // the object brightness
    {0x001c3ff0, 0x18},                     // the glare switch and the setup table
};
const int kRegionCount = sizeof(kRegions) / sizeof(kRegions[0]);
uint32_t RegionBytes() {
    uint32_t total = 0;
    for (int i = 0; i < kRegionCount; i++)
        total += kRegions[i].bytes;
    return total;
}

// The rings' requests and models, followed through their pointers
const uint32_t kRingBytes = 16 * (0x2c + 0x58) + 32 * (0x3c + 0x58);

struct Snapshot {
    uint8_t regions[0x2460 + 0xc + 4 + 0x1b0 + 0x260 + 4 + 0x18];
    uint8_t rings[kRingBytes];
    uint8_t fogParams[sizeof(FogParams)];
};

template <class Request>
void RingCopy(uint8_t *&cursor, Request **requests, EAGL::DynamicModel **models, bool take) {
    for (int i = 0; i < kMaterialRequests; i++) {
        if (requests[i] != NULL) {
            if (take)
                memcpy(cursor, requests[i], sizeof(Request));
            else
                memcpy(requests[i], cursor, sizeof(Request));
        }
        cursor += sizeof(Request);
        if (models[i] != NULL) {
            if (take)
                memcpy(cursor, models[i], sizeof(EAGL::DynamicModel));
            else
                memcpy(models[i], cursor, sizeof(EAGL::DynamicModel));
        }
        cursor += sizeof(EAGL::DynamicModel);
    }
}

void Copy(Snapshot *s, bool take) {
    uint8_t *cursor = s->regions;
    for (int i = 0; i < kRegionCount; i++) {
        uint8_t *game = (uint8_t *)(uintptr_t)kRegions[i].at;
        if (take)
            memcpy(cursor, game, kRegions[i].bytes);
        else
            memcpy(game, cursor, kRegions[i].bytes);
        cursor += kRegions[i].bytes;
    }
    cursor = s->rings;
    RingCopy(cursor, SimpleRequests, SimpleModels, take);
    RingCopy(cursor, TexturedRequests, TexturedModels, take);
    RingCopy(cursor, VolatileRequests, VolatileModels, take);
    if (Fog != NULL && Fog->params != NULL) {
        if (take)
            memcpy(s->fogParams, Fog->params, sizeof(FogParams));
        else
            memcpy(Fog->params, s->fogParams, sizeof(FogParams));
    }
}

Snapshot g_base, g_pre, g_afterOriginal, g_afterPort;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

// Each test is a section: its first few differences are printed, then its counts
const char *g_section = "";
int g_sectionDiffer = 0, g_sectionFaults = 0;
const int kSectionDetails = 4;

void SectionStart(const char *name) {
    g_section = name;
    g_sectionDiffer = g_differ;
    g_sectionFaults = g_faults;
    g_details = 0;
}

void SectionEnd() {
    int differ = g_differ - g_sectionDiffer, faults = g_faults - g_sectionFaults;
    if (differ != 0 || faults != 0)
        printf("[renderer]   %s: %d differ, %d faulted\n", g_section, differ, faults);
}

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < kSectionDetails)
        printf("[renderer]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
    Differ(what, index, detail);
}

void CheckU32(const char *what, int index, uint32_t a, uint32_t b) {
    g_checks++;
    if (a == b)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "original %08x, port %08x", a, b);
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

// ---- random inputs

uint32_t g_seed = 0x2468ace1;
uint32_t Random() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
float RandomFloat(float low, float high) {
    return low + (high - low) * float(Random() & 0xffffff) / float(0x1000000);
}
int RandomInt(int count) {
    return count > 0 ? int(Random() % uint32_t(count)) : 0;
}
float Nan() {
    uint32_t bits = 0x7fc00000;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

// ---- the recording fakes

struct LogEntry {
    uint32_t tag;
    uint32_t words[4];
    uint32_t hash;
};
const int kMaxLog = 8192;
LogEntry g_log[2][kMaxLog];
int g_logCount[2];
int g_side;

uint32_t Hash(const void *data, size_t bytes) {
    uint32_t h = 0x811c9dc5;
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < bytes; i++)
        h = (h ^ p[i]) * 0x01000193;
    return h;
}

void Log(uint32_t tag, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, const void *data = NULL,
         size_t bytes = 0) {
    int &n = g_logCount[g_side];
    if (n >= kMaxLog)
        return;
    LogEntry &e = g_log[g_side][n++];
    e.tag = tag;
    e.words[0] = a;
    e.words[1] = b;
    e.words[2] = c;
    e.words[3] = d;
    e.hash = data != NULL ? Hash(data, bytes) : 0;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint32_t Address(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

// The materials' draws: the request's arrays as they are when the draw is made
void LogTexturedRequest(uint32_t tag, const TexturedGeoPrim *request, int count) {
    uint32_t hash = 0;
    if (count > 0 && count <= 256) {
        hash = Hash(request->positions.data, count * sizeof(Vec4)) ^
               Hash(request->colours.data, count * 4) * 3 ^
               Hash(request->texCoords.data, count * sizeof(Vec4)) * 7;
    }
    Log(tag, Address(request->texture.data), request->state.count, request->texture.count, 0, &hash, 4);
}

void __fastcall FakeSimpleDraw(USimpleMaterial *self, int, int primitive, int count, MATRIX4 *transform) {
    const SimpleGeoPrim *request = SimpleRequests[SimpleRequestIndex];
    uint32_t hash = 0;
    if (count > 0 && count <= 256)
        hash = Hash(request->positions.data, count * sizeof(Vec4)) ^ Hash(request->colours.data, count * 4) * 3;
    Log(0x100, Address(self), primitive, count, Address(transform), &hash, 4);
}

void __fastcall FakeTexturedDraw(USimpleMaterial *self, int, int primitive, int count, MATRIX4 *transform) {
    Log(0x101, Address(self), primitive, count, Address(transform));
    LogTexturedRequest(0x102, TexturedRequests[TexturedRequestIndex], count);
}

void __fastcall FakeDynamicModelDraw(EAGL::DynamicModel *self, int) {
    Log(0x200, Address(self));
}

void __fastcall FakeDynamicModelSetMatrix(EAGL::DynamicModel *self, int, const float *m) {
    Log(0x201, Address(self), 0, 0, 0, m, 64);
}

void __fastcall FakeModelDraw(EAGL::Model *self, int, const float *m) {
    Log(0x202, Address(self), 0, 0, 0, m, 64);
}

bool __fastcall FakeFogSetter(EAGL::RenderContextExtension *self, int, uint32_t value) {
    Log(0x300, Address(self), value);
    return true;
}
bool __fastcall FakeFogSetterDensity(EAGL::RenderContextExtension *self, int, uint32_t value) {
    Log(0x301, Address(self), value);
    return true;
}
bool __fastcall FakeFogSetterStart(EAGL::RenderContextExtension *self, int, uint32_t value) {
    Log(0x302, Address(self), value);
    return true;
}
bool __fastcall FakeFogSetterEnd(EAGL::RenderContextExtension *self, int, uint32_t value) {
    Log(0x303, Address(self), value);
    return true;
}
bool __fastcall FakeFogSetterColour(EAGL::RenderContextExtension *self, int, uint32_t value) {
    Log(0x304, Address(self), value);
    return true;
}
bool __fastcall FakeFogSetterMode(EAGL::RenderContextExtension *self, int, uint32_t value) {
    Log(0x305, Address(self), value);
    return true;
}
bool __fastcall FakeRenderMask(EAGL::RenderContextExtension *self, int, uint32_t value) {
    Log(0x306, Address(self), value);
    return true;
}
void __fastcall FakeBeginFrame(EAGL::RenderContext *self, int) {
    Log(0x307, Address(self));
}
void *__fastcall FakeEndFrame(EAGL::RenderContext *self, int) {
    Log(0x308, Address(self));
    return NULL;
}

// A draw group's kept draw, at the vector's push_back: our DrawInstance has AddToTranslucentList inlined
void __fastcall FakePushBack(ReverseDrawList *self, int, const ReverseDrawEntry *draw) {
    Log(0x400, Address(self), Address(draw->instance), Address(draw->sceneObj), draw->distance, draw, 0x40);
}

void FakeSetup(CARP::Instance *instance, RSceneObj *sceneObj) {
    Log(0x402, Address(instance), Address(sceneObj));
}

void __fastcall FakeAddModelGlare(void *self, int, ArticleEffect *node, const MATRIX4 *transform, float distance) {
    Log(0x403, Address(self), Address(node), Bits(distance), 0, transform, 64);
}

void __fastcall FakeSpecularLight(RLightManager *self, int, const Coord4 *eye) {
    Log(0x500, Address(self), 0, 0, 0, eye, 12);
}
void __fastcall FakeLightingModel(RLightManager *self, int, int model) {
    Log(0x501, Address(self), model);
}
void __fastcall FakeSurfaceProperties(RLightManager *self, int, float ambient, float diffuse, float unused) {
    Log(0x502, Address(self), Bits(ambient), Bits(diffuse), Bits(unused));
}
double __fastcall FakeVerticalFalloff(RLightManager *self, int, const Coord3 *position) {
    Log(0x503, Address(self), 0, 0, 0, position, 12);
    return 0.8125;
}
void __fastcall FakeReflectivity(void *self, int, RSceneObj *sceneObj) {
    Log(0x504, Address(self), Address(sceneObj));
}
void __fastcall FakeSpecularStrength(void *self, int, int renderType, float strength) {
    Log(0x505, Address(self), renderType, Bits(strength));
}
void __fastcall FakeSetPerObjectBuffers(RSceneObj *self, int) {
    Log(0x506, Address(self));
}
void FakeClearSharedBuffers() {
    Log(0x507);
}

// Fake physics objects' positions, by object
const int kFakePhysics = 4;
uint8_t g_fakePhysics[kFakePhysics][sizeof(PhysicsObject)];
Coord3 g_fakePositions[kFakePhysics];
Coord3 g_spare;
Coord3 *__fastcall FakeGetPosition(PhysicsObject *self, int) {
    Log(0x508, Address(self));
    for (int i = 0; i < kFakePhysics; i++) {
        if ((uint8_t *)self == g_fakePhysics[i])
            return &g_fakePositions[i];
    }
    return &g_spare;
}

// ---- five-byte jumps over the originals the fakes stand in for (and over ours)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[64];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
    if (g_hookCount >= 64)
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old)) {
        h.on = false;
        return;
    }
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HookBoth(uint32_t at, const void *ours, const void *to) {
    HookInstall(at, to);
    if ((uint32_t)(uintptr_t)ours != at)
        HookInstall((uint32_t)(uintptr_t)ours, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

// A function's code address (a member function's too)
template <class F>
const void *CodeOf(F f) {
    const void *p;
    memcpy(&p, &f, sizeof(p));
    return p;
}

#define PTR(f) ((const void *)(f))

// Which of the fakes a test wants: the materials' draws are run for real by the materials' test, the draw group's
// vector's push for real by the vector's test
bool g_fakeMaterials = true;
bool g_fakePushBack = true;

void InstallFakes() {
    if (g_fakeMaterials) {
        HookBoth(0x0011bbf0, CodeOf(&USimpleMaterial::Draw), PTR(FakeSimpleDraw));
        HookBoth(0x0011bd50, CodeOf(&USimpleTexturedMaterial::Draw), PTR(FakeTexturedDraw));
    }
    HookBoth(0x000e99e0, CodeOf(&EAGL::DynamicModel::Draw), PTR(FakeDynamicModelDraw));
    HookBoth(0x000e9a80, CodeOf(&EAGL::DynamicModel::SetModelMatrix), PTR(FakeDynamicModelSetMatrix));
    HookBoth(0x000ea210, CodeOf(&EAGL::Model::Draw), PTR(FakeModelDraw));
    HookBoth(0x000e7950, CodeOf(&EAGL::RenderContextExtension::SetFogEnable), PTR(FakeFogSetter));
    HookBoth(0x000e7a60, CodeOf(&EAGL::RenderContextExtension::SetFogDensity), PTR(FakeFogSetterDensity));
    HookBoth(0x000e79e0, CodeOf(&EAGL::RenderContextExtension::SetFogStart), PTR(FakeFogSetterStart));
    HookBoth(0x000e7a20, CodeOf(&EAGL::RenderContextExtension::SetFogEnd), PTR(FakeFogSetterEnd));
    HookBoth(0x000e7aa0, CodeOf(&EAGL::RenderContextExtension::SetFogColour), PTR(FakeFogSetterColour));
    HookBoth(0x000e79a0, CodeOf(&EAGL::RenderContextExtension::SetFogTableMode), PTR(FakeFogSetterMode));
    HookBoth(0x000e7900, CodeOf(&EAGL::RenderContextExtension::SetRenderMask), PTR(FakeRenderMask));
    HookBoth(0x000e6610, CodeOf(&EAGL::RenderContext::BeginFrame), PTR(FakeBeginFrame));
    HookBoth(0x000e6640, CodeOf(&EAGL::RenderContext::EndFrame), PTR(FakeEndFrame));
    if (g_fakePushBack)
        HookBoth(0x0007ceb0, CodeOf(&ReverseDrawList::PushBack), PTR(FakePushBack));
    HookBoth(0x0006f330, CodeOf(&PhysicsObject::GetPosition), PTR(FakeGetPosition));
    HookBoth(0x000a9aa0, CodeOf(&RGlareManager::AddModelGlare), PTR(FakeAddModelGlare));
    HookBoth(0x0007e950, CodeOf(&RLightManager::AddSpecularLight), PTR(FakeSpecularLight));
    HookBoth(0x0007f470, CodeOf(&RLightManager::SetLightingModel), PTR(FakeLightingModel));
    HookBoth(0x0007eb30, CodeOf(&RLightManager::SetSurfaceProperties), PTR(FakeSurfaceProperties));
    HookBoth(0x0007e430, CodeOf(&RLightManager::VerticalFalloffMultiplier), PTR(FakeVerticalFalloff));
    HookBoth(0x00099610, CodeOf(&RReflection::SetReflectivity), PTR(FakeReflectivity));
    HookBoth(0x00098360, CodeOf(&RReflection::SetReflectiveSpecularStrength), PTR(FakeSpecularStrength));
    // Not ours yet: called by address by both sides
    HookInstall(0x00095ff0, PTR(FakeSetPerObjectBuffers));
    HookInstall(0x00095970, PTR(FakeClearSharedBuffers));
}

// ---- running both sides

typedef void (*CaseFn)(void *context, bool original);

// The ring requests' words that point into this thread's stack - vertex arrays a draw left behind - all read alike
const uint32_t kOnStack = 0x57ac0000;

void MarkStackPointers(void *request, size_t bytes) {
    if (request == NULL)
        return;
    const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    uint32_t low = Address(tib->StackLimit), high = Address(tib->StackBase);
    uint32_t *words = static_cast<uint32_t *>(request);
    for (size_t i = 0; i < bytes / 4; i++) {
        if (words[i] >= low && words[i] < high)
            words[i] = kOnStack;
    }
}

void MarkStackPointers() {
    for (int i = 0; i < kMaterialRequests; i++) {
        MarkStackPointers(SimpleRequests[i], sizeof(SimpleGeoPrim));
        MarkStackPointers(TexturedRequests[i], sizeof(TexturedGeoPrim));
        MarkStackPointers(VolatileRequests[i], sizeof(TexturedGeoPrim));
    }
}

// One side: the original inside the window, the fakes in place; a fault is counted, not fatal
bool Guarded(CaseFn run, void *context, bool original) {
    g_side = original ? 0 : 1;
    g_logCount[g_side] = 0;
    if (original)
        OriginalWindow(true);
    InstallFakes();
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(context, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(context, original);
#endif
    HooksRemove();
    if (original)
        OriginalWindow(false);
    return ok;
}

void CheckLogs(const char *what, int index) {
    CheckU32(what, index, g_logCount[0], g_logCount[1]);
    int n = g_logCount[0] < g_logCount[1] ? g_logCount[0] : g_logCount[1];
    for (int i = 0; i < n; i++)
        CheckBytes(what, index, &g_log[0][i], &g_log[1][i], sizeof(LogEntry));
}

// Both sides from the state as it is now; their state after compared, then put back. `reset` (may be NULL) puts
// the case's own inputs back before each side.
void SideBySide(const char *what, int index, CaseFn run, void *context, CaseFn reset = NULL) {
    g_cases++;
    Copy(&g_pre, true);
    if (reset != NULL)
        reset(context, true);
    Guarded(run, context, true);
    MarkStackPointers();
    Copy(&g_afterOriginal, true);
    Copy(&g_pre, false);
    if (reset != NULL)
        reset(context, false);
    Guarded(run, context, false);
    MarkStackPointers();
    Copy(&g_afterPort, true);
    Copy(&g_pre, false);
    CheckBytes(what, index, g_afterOriginal.regions, g_afterPort.regions, sizeof(g_afterPort.regions));
    CheckBytes(what, index, g_afterOriginal.rings, g_afterPort.rings, sizeof(g_afterPort.rings));
    CheckBytes(what, index, g_afterOriginal.fogParams, g_afterPort.fogParams, sizeof(g_afterPort.fogParams));
    CheckLogs(what, index);
}

// ---- pure functions

struct ValueCase {
    uint32_t colour;
    float u, v;
    const ShadowMapImage *image;
    Coord3 position;
    int instance;
    const CARP::Instance *instancePointer;
    float radius;
    uint8_t result[2][8];
};

void ColourCase(void *context, bool original) {
    ValueCase *c = static_cast<ValueCase *>(context);
    uint32_t r = original ? Orig_ColourConvert(c->colour) : ColourConvertXBoxToPS2(c->colour);
    memcpy(c->result[original ? 0 : 1], &r, 4);
}

void TextureValCase(void *context, bool original) {
    ValueCase *c = static_cast<ValueCase *>(context);
    double r = original ? Orig_GetTextureVal(c->u, c->v, c->image) : GetTextureVal(c->u, c->v, c->image);
    memcpy(c->result[original ? 0 : 1], &r, 8);
}

void ShadowBrightnessCase(void *context, bool original) {
    ValueCase *c = static_cast<ValueCase *>(context);
    double r = original ? Orig_GetShadowBrightness(c->position, c->instance)
                        : RRenderSharedData::GetShadowBrightness(c->position, c->instance);
    memcpy(c->result[original ? 0 : 1], &r, 8);
}

void ArticleDistanceCase(void *context, bool original) {
    ValueCase *c = static_cast<ValueCase *>(context);
    double r = original ? Orig_GetArticleViewDistance(c->instancePointer, c->radius)
                        : GetArticleViewDistance(c->instancePointer, c->radius);
    memcpy(c->result[original ? 0 : 1], &r, 8);
}

void InstanceDistanceCase(void *context, bool original) {
    ValueCase *c = static_cast<ValueCase *>(context);
    double r = original ? Orig_GetInstanceViewDistance(c->instancePointer) : GetInstanceViewDistance(c->instancePointer);
    memcpy(c->result[original ? 0 : 1], &r, 8);
}

void RunValue(const char *what, int index, CaseFn run, ValueCase *c, size_t bytes) {
    memset(c->result, 0, sizeof(c->result));
    SideBySide(what, index, run, c);
    CheckBytes(what, index, c->result[0], c->result[1], bytes);
}

void TestColours() {
    ValueCase c;
    memset(&c, 0, sizeof(c));
    const uint32_t edges[] = {0, 0xffffffff, 0xff000000, 0x00ffffff, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x80808080,
                              0x7f7f7f7f, 0x01010101};
    for (int i = 0; i < 10; i++) {
        c.colour = edges[i];
        RunValue("ColourConvertXBoxToPS2", i, ColourCase, &c, 4);
    }
    for (int i = 0; i < 1500; i++) {
        c.colour = Random();
        RunValue("ColourConvertXBoxToPS2", 10 + i, ColourCase, &c, 4);
    }
}

void TestShadowMaps() {
    ValueCase c;
    memset(&c, 0, sizeof(c));
    int images = ShadowImages[0] != NULL ? ShadowCount : 0;
    if (images > 64)
        images = 64;
    int index = 0;
    for (int i = 0; i < images; i++) {
        const ShadowMapImage *image = ShadowImages[i];
        if (image == NULL)
            continue;
        c.image = image;
        for (int k = 0; k < 120; k++) {
            float w = image->width, h = image->height;
            c.u = RandomFloat(-3.0f, w + 3.0f);
            c.v = RandomFloat(-3.0f, h + 3.0f);
            if (k % 10 == 0)
                c.u = float(RandomInt(int(w) + 2));
            if (k % 10 == 1)
                c.v = float(RandomInt(int(h) + 2)) + 0.5f;
            if (k == 2)
                c.u = Nan();
            if (k == 3)
                c.v = Nan();
            RunValue("GetTextureVal", index++, TextureValCase, &c, 8);
        }
    }
    if (fgWorld != NULL && fgWorld->instances != NULL && fgWorld->instanceCount != 0) {
        for (int i = 0; i < 400; i++) {
            c.instance = RandomInt(fgWorld->instanceCount);
            const float *at = fgWorld->instances[c.instance].position;
            c.position.x = at[0] + RandomFloat(-40.0f, 40.0f);
            c.position.y = at[1] + RandomFloat(-5.0f, 5.0f);
            c.position.z = at[2] + RandomFloat(-40.0f, 40.0f);
            RunValue("GetShadowBrightness", i, ShadowBrightnessCase, &c, 8);
        }
        ShadowMapImage *first = ShadowImages[0];
        ShadowImages[0] = NULL;
        for (int i = 0; i < 4; i++) {
            c.instance = RandomInt(fgWorld->instanceCount);
            RunValue("GetShadowBrightness (none)", i, ShadowBrightnessCase, &c, 8);
        }
        ShadowImages[0] = first;
    }
    printf("[renderer] %d shadow map(s) sampled\n", images);
}

// A camera and view of our own for the view distances
struct FakeView {
    alignas(16) uint8_t camera[sizeof(RCamera)];
    uint8_t view[sizeof(RViewCamera)];
};
FakeView g_fakeView;
RViewCamera *g_savedView;

void FakeViewOn(float x, float y, float z, float lod) {
    memset(&g_fakeView, 0, sizeof(g_fakeView));
    RCamera *camera = reinterpret_cast<RCamera *>(g_fakeView.camera);
    RViewCamera *view = reinterpret_cast<RViewCamera *>(g_fakeView.view);
    for (int i = 0; i < 4; i++)
        camera->matrix.mtx[i][i] = 1.0f;
    camera->matrix.mtx[2][0] = RandomFloat(-1.0f, 1.0f);
    camera->matrix.mtx[2][2] = RandomFloat(-1.0f, 1.0f);
    camera->matrix.mtx[3][0] = x;
    camera->matrix.mtx[3][1] = y;
    camera->matrix.mtx[3][2] = z;
    view->camera = camera;
    view->lodMultiplier = lod;
    g_savedView = fgRenderer->currentView;
    fgRenderer->currentView = view;
}

void FakeViewOff() {
    fgRenderer->currentView = g_savedView;
}

void TestViewDistances() {
    if (fgWorld == NULL || fgWorld->instances == NULL || fgWorld->instanceCount == 0)
        return;
    ValueCase c;
    memset(&c, 0, sizeof(c));
    CARP::Instance copy;
    int count = fgWorld->instanceCount;
    for (int i = 0; i < 600; i++) {
        const CARP::Instance *instance = &fgWorld->instances[RandomInt(count)];
        float lod = i % 7 == 0 ? 1.0f : RandomFloat(0.1f, 4.0f);
        FakeViewOn(instance->position[0] + RandomFloat(-500.0f, 500.0f), instance->position[1] + RandomFloat(-50.0f, 50.0f),
                   instance->position[2] + RandomFloat(-500.0f, 500.0f), lod);
        copy = *instance;
        if (i % 3 == 1)
            copy.packedDimensions = Random();
        c.instancePointer = i % 2 ? &copy : instance;
        c.radius = i % 11 == 0 ? Nan() : RandomFloat(-10.0f, 400.0f);
        RunValue("GetArticleViewDistance", i, ArticleDistanceCase, &c, 8);
        RunValue("GetInstanceViewDistance", i, InstanceDistanceCase, &c, 8);
        FakeViewOff();
    }
}

// ---- RFog

struct FogCase {
    RFog fog;
    FogParams params;
    RFog inFog;
    FogParams inParams;
    uint32_t tick;
    int op;
    float a, b;
    uint32_t colour[2];
    RFog fogOut[2];
    FogParams paramsOut[2];
};

void FogReset(void *context, bool) {
    FogCase *c = static_cast<FogCase *>(context);
    c->fog = c->inFog;
    c->params = c->inParams;
    c->fog.params = &c->params;
    GameTick = c->tick;
}

void FogRun(void *context, bool original) {
    FogCase *c = static_cast<FogCase *>(context);
    RFog *fog = &c->fog;
    switch (c->op) {
    case 0:
        if (original)
            Orig_FadeScale(fog, 0, c->a, c->b);
        else
            fog->FadeScale(c->a, c->b);
        break;
    case 1:
        if (original)
            Orig_UpdateScale(fog, 0);
        else
            fog->UpdateScale();
        break;
    case 2:
        if (original)
            Orig_SetFogParams(fog, 0);
        else
            fog->SetFogParams();
        break;
    case 3:
        if (original)
            Orig_EnableFog(fog, 0);
        else
            fog->EnableFog();
        break;
    case 4:
        if (original)
            Orig_DisableFog(fog, 0);
        else
            fog->DisableFog();
        break;
    default:
        c->colour[original ? 0 : 1] = original ? Orig_FogColour(fog, 0) : fog->FogColour();
        break;
    }
    c->fogOut[original ? 0 : 1] = c->fog;
    c->fogOut[original ? 0 : 1].params = NULL;
    c->paramsOut[original ? 0 : 1] = c->params;
}

void TestFog() {
    if (Fog == NULL || Fog->params == NULL)
        return;
    uint32_t savedTick = GameTick;
    FogCase *c = static_cast<FogCase *>(calloc(1, sizeof(FogCase)));
    for (int i = 0; i < 500; i++) {
        c->inFog = *Fog;
        c->inParams = *Fog->params;
        c->inParams.scale = RandomFloat(0.0f, 2.0f);
        c->inParams.baseEnd = RandomFloat(10.0f, 6000.0f);
        c->inParams.start = RandomFloat(0.0f, 500.0f);
        c->inParams.density = RandomFloat(0.0f, 0.01f);
        c->inParams.colour = Random();
        c->inParams.mode = RandomInt(4);
        c->tick = i % 9 == 0 ? 0x7ffffff0u + RandomInt(32) : savedTick + RandomInt(200);
        c->inFog.fading = (uint8_t)RandomInt(2);
        c->inFog.fadeEndTick = c->tick + RandomInt(120) - 60;
        c->inFog.fadeRate = RandomFloat(-0.05f, 0.05f);
        c->inFog.fadeTarget = RandomFloat(0.0f, 2.0f);
        c->op = i % 6;
        c->a = RandomFloat(0.0f, 2.0f);
        c->b = i % 13 == 0 ? -RandomFloat(0.0f, 2.0f) : RandomFloat(0.0f, 5.0f);
        c->colour[0] = c->colour[1] = 0;
        SideBySide("RFog", i, FogRun, c, FogReset);
        CheckBytes("RFog object", i, &c->fogOut[0], &c->fogOut[1], sizeof(RFog));
        CheckBytes("RFog settings", i, &c->paramsOut[0], &c->paramsOut[1], sizeof(FogParams));
        CheckU32("RFog::FogColour", i, c->colour[0], c->colour[1]);
    }
    GameTick = savedTick;
    free(c);
}

// ---- Draw

struct DrawCase {
    int op;
    float x, y, w, h, z;
    uint32_t colour;
    SpriteTexCoords uv;
    Vec4 points[4];
    Vec4 texCoords[4];
    uint32_t colours[4];
    EAGL::TAR *texture;
    MATRIX4 *matrix;
    uint8_t material[2][sizeof(USimpleMaterial)];
    uint8_t materialIn[sizeof(USimpleMaterial)];
    int steps;
    PackedColour packed[2];
};

void DrawReset(void *context, bool original) {
    DrawCase *c = static_cast<DrawCase *>(context);
    memcpy(c->material[original ? 0 : 1], c->materialIn, sizeof(c->materialIn));
    DrawZ = c->z;
}

EAGL::TAR *const kTextures[3] = {(EAGL::TAR *)0x00a00000, (EAGL::TAR *)0x00b00000, NULL};

void DrawRun(void *context, bool original) {
    DrawCase *c = static_cast<DrawCase *>(context);
    EAGL::GeoPrimState *material = reinterpret_cast<EAGL::GeoPrimState *>(c->material[original ? 0 : 1]);
    switch (c->op) {
    case 0:
        original ? Orig_DrawBox(c->x, c->y, c->w, c->h, c->colour) : Draw::DrawBox(c->x, c->y, c->w, c->h, c->colour);
        break;
    case 1:
        original ? Orig_DrawSprite(c->x, c->y, c->w, c->h, c->colour, &c->uv, c->texture)
                 : Draw::DrawSprite(c->x, c->y, c->w, c->h, c->colour, &c->uv, c->texture);
        break;
    case 2:
        original ? Orig_DrawWholeSprite(c->x, c->y, c->w, c->h, c->colour, c->texture)
                 : Draw::DrawSprite(c->x, c->y, c->w, c->h, c->colour, c->texture);
        break;
    case 3:
        original ? Orig_DrawQuadC(c->points, c->colours) : Draw::DrawQuadC(c->points, c->colours);
        break;
    case 4:
        original ? Orig_DrawQuad(c->points, c->colour) : Draw::DrawQuad(c->points, c->colour);
        break;
    case 5:
        original ? Orig_SetNormalBlendMode(material) : Draw::SetNormalBlendMode(material);
        break;
    case 6:
        original ? Orig_SetAdditiveBlendMode(material) : Draw::SetAdditiveBlendMode(material);
        break;
    case 7:
        original ? Orig_SetModelMatrix(c->matrix) : Draw::SetModelMatrix(c->matrix);
        original ? Orig_SetZDepth(c->x) : Draw::SetZDepth(c->x);
        if (c->steps & 1)
            original ? Orig_SetZDepthNear() : Draw::SetZDepthNear();
        original ? (void)Orig_ColourConstruct(&c->packed[0], 0) : (void)c->packed[1].Construct();
        break;
    default:
        // a sequence of batched draws; the inputs come from a generator seeded alike for both sides
        uint32_t seed = g_seed;
        for (int i = 0; i < c->steps; i++) {
            EAGL::TAR *texture = kTextures[Random() % 3];
            if (Random() % 4 != 0)
                texture = kTextures[i / 40 % 2];
            for (int k = 0; k < 4; k++)
                c->points[k] = {RandomFloat(0, 640), RandomFloat(0, 480), RandomFloat(0, 1), 1.0f};
            uint32_t colour = Random();
            if (c->op == 8)
                original ? Orig_DrawBatchedTexturedQuad(&c->points[0], &c->points[1], &c->points[2], &c->points[3],
                                                        colour, c->texCoords, texture)
                         : Draw::DrawBatchedTexturedQuad(&c->points[0], &c->points[1], &c->points[2], &c->points[3],
                                                         colour, c->texCoords, texture);
            else
                original ? Orig_DrawBatchedSprite(c->points[0].x, c->points[0].y, c->points[1].x, c->points[1].y,
                                                  colour, &c->uv, texture)
                         : Draw::DrawBatchedSprite(c->points[0].x, c->points[0].y, c->points[1].x, c->points[1].y,
                                                   colour, &c->uv, texture);
        }
        g_seed = seed;
        break;
    }
}

void TestDraw() {
    if (SimpleMaterial == NULL || SimpleTexturedMaterial == NULL)
        return;
    DrawCase *c = static_cast<DrawCase *>(calloc(1, sizeof(DrawCase) + 16));
    alignas(16) MATRIX4 matrix = {};
    for (int i = 0; i < 700; i++) {
        c->op = i % 10;
        c->x = RandomFloat(-100, 700);
        c->y = RandomFloat(-100, 500);
        c->w = RandomFloat(-50, 400);
        c->h = RandomFloat(-50, 400);
        c->z = i % 5 == 0 ? 1.0f : RandomFloat(0, 1);
        c->colour = Random();
        c->uv.topLeft = {RandomFloat(0, 1), RandomFloat(0, 1), RandomFloat(0, 1), RandomFloat(0, 1)};
        c->uv.bottomRight = {RandomFloat(0, 1), RandomFloat(0, 1), RandomFloat(0, 1), RandomFloat(0, 1)};
        for (int k = 0; k < 4; k++) {
            c->points[k] = {RandomFloat(0, 640), RandomFloat(0, 480), RandomFloat(0, 1), 1.0f};
            c->texCoords[k] = {RandomFloat(0, 1), RandomFloat(0, 1), RandomFloat(0, 1), RandomFloat(0, 1)};
            c->colours[k] = Random();
        }
        c->texture = kTextures[RandomInt(2)];
        c->matrix = i % 3 ? &matrix : NULL;
        memcpy(c->materialIn, SimpleTexturedMaterial, sizeof(c->materialIn));
        c->steps = 1 + RandomInt(90);
        c->packed[0].value = c->packed[1].value = Random();
        SideBySide("Draw", i, DrawRun, c, DrawReset);
        if (c->op == 5 || c->op == 6)
            CheckBytes("Draw blend mode", i, c->material[0], c->material[1], sizeof(c->materialIn));
        if (c->op == 7)
            CheckU32("PackedColour::Construct", i, c->packed[0].value, c->packed[1].value);
    }
    free(c);
}

// ---- the materials' draws

struct MaterialCase {
    int which;
    int primitive;
    int count;
    MATRIX4 *transform;
    uint32_t index;
    uint8_t material[sizeof(USimpleMaterial)];
    uint8_t materialIn[sizeof(USimpleMaterial)];
    uint8_t materialOut[2][sizeof(USimpleMaterial)];
};

void MaterialReset(void *context, bool) {
    MaterialCase *c = static_cast<MaterialCase *>(context);
    memcpy(c->material, c->materialIn, sizeof(c->material));
    SimpleRequestIndex = c->index;
    TexturedRequestIndex = c->index;
    VolatileRequestIndex = c->index;
}

void MaterialRun(void *context, bool original) {
    MaterialCase *c = static_cast<MaterialCase *>(context);
    USimpleMaterial *material = reinterpret_cast<USimpleMaterial *>(c->material);
    switch (c->which) {
    case 0:
        original ? Orig_SimpleDraw(material, 0, c->primitive, c->count, c->transform)
                 : material->Draw(c->primitive, c->count, c->transform);
        break;
    case 1:
        original ? Orig_TexturedDraw(material, 0, c->primitive, c->count, c->transform)
                 : static_cast<USimpleTexturedMaterial *>(material)->Draw(c->primitive, c->count, c->transform);
        break;
    default:
        original ? Orig_VolatileDraw(material, 0, c->primitive, c->count, c->transform)
                 : static_cast<UVolatileMaterial *>(material)->Draw(c->primitive, c->count, c->transform);
        break;
    }
    memcpy(c->materialOut[original ? 0 : 1], c->material, sizeof(c->material));
}

void TestMaterials() {
    if (SimpleRequests[0] == NULL || TexturedRequests[0] == NULL || VolatileRequests[0] == NULL ||
        SimpleMaterial == NULL)
        return;
    g_fakeMaterials = false;
    MaterialCase *c = static_cast<MaterialCase *>(calloc(1, sizeof(MaterialCase)));
    alignas(16) MATRIX4 matrix;
    for (int i = 0; i < 300; i++) {
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 4; k++)
                matrix.mtx[r][k] = RandomFloat(-2, 2);
        c->which = i % 3;
        const DrawPrimitive primitives[3] = {kTriangleList, kTriangleStrip, kQuadList};
        c->primitive = primitives[RandomInt(3)];
        c->count = 1 + RandomInt(200);
        c->transform = i % 2 ? &matrix : NULL;
        c->index = RandomInt(16);
        memcpy(c->materialIn, i % 2 ? (void *)SimpleMaterial : (void *)SimpleTexturedMaterial, sizeof(c->materialIn));
        SideBySide("material Draw", i, MaterialRun, c, MaterialReset);
        CheckBytes("material Draw object", i, c->materialOut[0], c->materialOut[1], sizeof(c->material));
    }
    g_fakeMaterials = true;
    free(c);
}

// ---- the draw group's vector

// DrawInstance and the draws under it read the article unchecked (as the original does): only instances with one
bool HasArticle(int index) {
    return fgWorld->instances[index].articleDesc.value != 0;
}

int RandomArticleInstance(int count) {
    for (int tries = 0; tries < 64; tries++) {
        int index = RandomInt(count);
        if (HasArticle(index))
            return index;
    }
    return -1;
}

struct GroupCase {
    uint32_t seed;
    int pushes;
    int inserts;
    // each side's vector as it was before the flush: count, capacity, the draws
    uint32_t count[2], capacity[2];
    ReverseDrawEntry *draws[2];
    uint32_t emptied[2];
};

int g_articleInstance;

// A kept draw of one of the track's instances with an article (the flush draws it); the scene object only goes to
// the faked render-type setups
ReverseDrawEntry RandomDraw() {
    ReverseDrawEntry d;
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++)
            d.transform.mtx[r][k] = RandomFloat(-10, 10);
    int index = RandomArticleInstance(fgWorld->instanceCount);
    d.instance = &fgWorld->instances[index >= 0 ? index : g_articleInstance];
    d.sceneObj = (RSceneObj *)(uintptr_t)(Random() & 0xfff0);
    d.distance = Random() % 4 == 0 ? Random() : uint32_t(RandomFloat(0, 3000) * 65536.0f);
    d.unknown4C = Random();
    return d;
}

void GroupRun(void *context, bool original) {
    GroupCase *c = static_cast<GroupCase *>(context);
    int side = original ? 0 : 1;
    uint32_t seed = g_seed;
    g_seed = c->seed;
    RDrawGroup group;
    original ? (void)Orig_GroupConstruct(&group, 0) : (void)group.Construct();
    for (int i = 0; i < c->pushes + c->inserts; i++) {
        ReverseDrawEntry d = RandomDraw();
        if (i < c->pushes || group.translucent->first == NULL) {
            original ? Orig_GroupAdd(&group, 0, &d) : group.AddToTranslucentList(&d);
            continue;
        }
        uint32_t size = uint32_t(group.translucent->last - group.translucent->first);
        ReverseDrawEntry *where = group.translucent->first + RandomInt(size + 1);
        uint32_t count = RandomInt(Random() % 4 == 0 ? 40 : 5);
        original ? Orig_InsertN(group.translucent, 0, where, count, &d) : group.translucent->InsertN(where, count, &d);
    }
    ReverseDrawList *list = group.translucent;
    c->count[side] = list->first == NULL ? 0 : uint32_t(list->last - list->first);
    c->capacity[side] = list->first == NULL ? 0 : uint32_t(list->end - list->first);
    c->draws[side] = static_cast<ReverseDrawEntry *>(malloc((c->count[side] + 1) * sizeof(ReverseDrawEntry)));
    if (c->count[side] != 0)
        memcpy(c->draws[side], list->first, c->count[side] * sizeof(ReverseDrawEntry));
    original ? Orig_GroupFlush(&group, 0) : group.FlushDrawLists();
    c->emptied[side] = Address(list->first) | Address(list->last) | Address(list->end);
    original ? Orig_GroupDestruct(&group, 0) : group.Destruct();
    g_seed = seed;
}

void TestDrawGroups() {
    if (fgWorld == NULL || fgWorld->instances == NULL || fgWorld->instanceCount == 0 || Fog == NULL)
        return;
    g_articleInstance = RandomArticleInstance(fgWorld->instanceCount);
    if (g_articleInstance < 0)
        return;
    RenderTypeSetup savedSetups[5];
    memcpy(savedSetups, RenderTypeSetups, sizeof(savedSetups));
    for (int i = 0; i < 5; i++)
        RenderTypeSetups[i] = FakeSetup;
    g_fakePushBack = false;
    GroupCase c;
    for (int i = 0; i < 120; i++) {
        memset(&c, 0, sizeof(c));
        c.seed = Random() | 1;
        c.pushes = RandomInt(i % 4 == 0 ? 200 : 20);
        c.inserts = RandomInt(30);
        SideBySide("RDrawGroup", i, GroupRun, &c);
        CheckU32("RDrawGroup count", i, c.count[0], c.count[1]);
        CheckU32("RDrawGroup capacity", i, c.capacity[0], c.capacity[1]);
        if (c.count[0] == c.count[1] && c.draws[0] != NULL && c.draws[1] != NULL)
            CheckBytes("RDrawGroup draws", i, c.draws[0], c.draws[1], c.count[0] * sizeof(ReverseDrawEntry));
        CheckU32("RDrawGroup flushed", i, c.emptied[0], c.emptied[1]);
        free(c.draws[0]);
        free(c.draws[1]);
    }
    g_fakePushBack = true;
    memcpy(RenderTypeSetups, savedSetups, sizeof(savedSetups));
}

// ---- the instance draws

struct InstanceCase {
    int op;
    CARP::Instance *instance;
    const MATRIX4 *transform;
    ProcAnimState *procAnims;
    float distance;
    uint32_t fixedDistance;
    RSceneObj *sceneObj;
    int pass;
    uint32_t count;
    uint16_t indices[8];
};

void InstanceRun(void *context, bool original) {
    InstanceCase *c = static_cast<InstanceCase *>(context);
    switch (c->op) {
    case 0:
        original ? Orig_QuickDrawInstance(c->instance, c->procAnims) : QuickDrawInstance(c->instance, c->procAnims);
        break;
    case 1:
        original ? Orig_DrawInstance(c->instance, c->transform, c->procAnims, c->distance, c->sceneObj, c->pass)
                 : DrawInstance(c->instance, c->transform, c->procAnims, c->distance, c->sceneObj, c->pass);
        break;
    case 2:
        original ? Orig_DrawInstanceList(c->instance, c->transform, c->procAnims, c->count, c->distance, c->sceneObj)
                 : DrawInstanceList(c->instance, c->transform, c->procAnims, c->count, c->distance, c->sceneObj);
        break;
    case 3:
        original ? Orig_DrawIndexedInstanceList(fgWorld->instances, c->indices, c->transform, c->procAnims, c->count,
                                                c->distance, c->sceneObj)
                 : DrawIndexedInstanceList(fgWorld->instances, c->indices, c->transform, c->procAnims, c->count,
                                           c->distance, c->sceneObj);
        break;
    default:
        original ? Orig_DrawGroupDrawInstance(c->transform, c->instance, c->fixedDistance, c->sceneObj, 0)
                 : DrawGroupDrawInstance(c->transform, c->instance, c->fixedDistance, c->sceneObj, 0);
        break;
    }
}

void TestInstances() {
    if (fgWorld == NULL || fgWorld->instances == NULL || fgWorld->instanceCount == 0 || Fog == NULL)
        return;
    RenderTypeSetup savedSetups[5];
    memcpy(savedSetups, RenderTypeSetups, sizeof(savedSetups));
    for (int i = 0; i < 5; i++)
        RenderTypeSetups[i] = FakeSetup;
    int count = fgWorld->instanceCount;
    InstanceCase c;
    memset(&c, 0, sizeof(c));
    c.procAnims = fgWorld->procAnims;
    // every instance where it stands
    for (int i = 0; i < count; i++) {
        const CARP::Instance *at = &fgWorld->instances[i];
        FakeViewOn(at->position[0] + RandomFloat(-300, 300), at->position[1] + 10.0f, at->position[2] + RandomFloat(-300, 300),
                   RandomFloat(0.5f, 3.0f));
        c.op = 0;
        c.instance = &fgWorld->instances[i];
        SideBySide("QuickDrawInstance", i, InstanceRun, &c);
        FakeViewOff();
    }
    // the others, at random
    alignas(16) MATRIX4 transform;
    CARP::Instance copies[1];
    for (int i = 0; i < 900; i++) {
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 4; k++)
                transform.mtx[r][k] = r == k ? 1.0f : (r == 3 ? RandomFloat(-50, 50) : RandomFloat(-0.3f, 0.3f));
        transform.mtx[3][3] = 1.0f;
        c.op = 1 + i % 4;
        int first = RandomArticleInstance(count);
        if (first < 0)
            break;
        c.instance = &fgWorld->instances[first];
        if (i % 5 == 0) {
            copies[0] = *c.instance;
            copies[0].flags ^= (uint8_t)(i % 10 == 0 ? 0x08 : 0x01);
            if (c.op == 1 || c.op == 4)
                c.instance = &copies[0];
        }
        c.transform = &transform;
        c.distance = i % 17 == 0 ? Nan() : RandomFloat(0, 3000);
        c.fixedDistance = i % 9 == 0 ? Random() : uint32_t(RandomFloat(0, 3000) * 65536.0f);
        c.sceneObj = (RSceneObj *)(uintptr_t)(i % 3 ? 0 : 0x00c0ffe0);
        c.pass = RandomInt(5);
        c.count = 1 + RandomInt(6);
        if (c.op == 2 && first + int(c.count) > count)
            c.count = count - first;
        for (uint32_t k = 1; c.op == 2 && k < c.count; k++) {
            if (!HasArticle(first + k))
                c.count = k;
        }
        for (int k = 0; k < 8; k++) {
            int index = RandomArticleInstance(count);
            c.indices[k] = (uint16_t)(index < 0 ? first : index);
        }
        SideBySide("instance draws", i, InstanceRun, &c);
    }
    memcpy(RenderTypeSetups, savedSetups, sizeof(savedSetups));
}

// ---- RRenderSharedData

const int kFakeSceneObjs = 6;
struct SharedCase {
    int op;
    uint8_t objects[kFakeSceneObjs][sizeof(RSceneObj)];
    uint8_t objectsIn[kFakeSceneObjs][sizeof(RSceneObj)];
    uint8_t physics[kFakePhysics][sizeof(PhysicsObject)];
    uint8_t collision[kFakePhysics][sizeof(WCollisionInstance)];
    int target;
    bool allowed;
    bool answer[2];
    uint8_t objectsOut[2][kFakeSceneObjs][sizeof(RSceneObj)];
    uint8_t physicsOut[2][kFakePhysics][sizeof(PhysicsObject)];
};

void SharedReset(void *context, bool) {
    SharedCase *c = static_cast<SharedCase *>(context);
    memcpy(c->objects, c->objectsIn, sizeof(c->objects));
    memcpy(g_fakePhysics, c->physics, sizeof(g_fakePhysics));
}

RSceneObj *FakeObject(SharedCase *c, int index) {
    return index < 0 ? NULL : reinterpret_cast<RSceneObj *>(c->objects[index]);
}

void SharedRun(void *context, bool original) {
    SharedCase *c = static_cast<SharedCase *>(context);
    RSceneObj *object = FakeObject(c, c->target);
    switch (c->op) {
    case 0:
        original ? Orig_SharedInit() : RRenderSharedData::Init();
        c->answer[original ? 0 : 1] = original ? Orig_SetVehiclesAllowed(c->allowed)
                                               : RRenderSharedData::SetVehiclesAllowed(c->allowed);
        break;
    case 1:
        original ? Orig_MarkAsDirty() : RRenderSharedData::MarkAsDirty();
        original ? Orig_SendPerWorldInstance() : RRenderSharedData::SendPerWorldInstance();
        break;
    case 2:
        original ? Orig_SendPerViewPort() : RRenderSharedData::SendPerViewPort();
        break;
    case 3:
        original ? Orig_SendPerObjectInstance(NULL, object) : RRenderSharedData::SendPerObjectInstance(NULL, object);
        original ? Orig_SendPerObjectInstance(NULL, object) : RRenderSharedData::SendPerObjectInstance(NULL, object);
        break;
    default:
        original ? Orig_SendPerCarInstance(NULL, object) : RRenderSharedData::SendPerCarInstance(NULL, object);
        original ? Orig_SendPerCarInstance(NULL, object) : RRenderSharedData::SendPerCarInstance(NULL, object);
        break;
    }
    memcpy(c->objectsOut[original ? 0 : 1], c->objects, sizeof(c->objects));
    memcpy(c->physicsOut[original ? 0 : 1], g_fakePhysics, sizeof(g_fakePhysics));
}

void TestSharedData() {
    if (fgLightManager == NULL || fgWorld == NULL || fgWorld->instanceCount == 0)
        return;
    SharedCase *c = static_cast<SharedCase *>(calloc(1, sizeof(SharedCase)));
    RSceneObj *savedObject = CurrentObject;
    for (int i = 0; i < 400; i++) {
        memset(c->objectsIn, 0, sizeof(c->objectsIn));
        memset(c->physics, 0, sizeof(c->physics));
        for (int p = 0; p < kFakePhysics; p++) {
            WCollisionInstance *collision = reinterpret_cast<WCollisionInstance *>(c->collision[p]);
            collision->renderIndex = (uint16_t)RandomInt(fgWorld->instanceCount);
            PhysicsObject *physics = reinterpret_cast<PhysicsObject *>(c->physics[p]);
            physics->worldPos.valid = (uint8_t)(Random() % 3 != 0);
            physics->worldPos.instance = Random() % 4 != 0 ? collision : NULL;
            const float *at = fgWorld->instances[collision->renderIndex].position;
            g_fakePositions[p] = {at[0] + RandomFloat(-20, 20), at[1], at[2] + RandomFloat(-20, 20)};
        }
        for (int k = 0; k < kFakeSceneObjs; k++) {
            RSceneObj *object = reinterpret_cast<RSceneObj *>(c->objectsIn[k]);
            object->brightness = (uint8_t)Random();
            object->flags = (uint8_t)Random();
            object->renderTypeIndex = (uint8_t)RandomInt(5);
            object->physics = Random() % 3 ? reinterpret_cast<PhysicsObject *>(g_fakePhysics[RandomInt(kFakePhysics)])
                                           : NULL;
        }
        c->op = i % 5;
        c->target = RandomInt(kFakeSceneObjs + 1) - 1;
        if (c->op == 4 && c->target < 0)
            c->target = 0;
        c->allowed = (Random() & 1) != 0;
        CurrentObject = i % 4 == 0 ? FakeObject(c, c->target) : NULL;
        if (c->op == 2)
            FakeViewOn(RandomFloat(-100, 100), 5.0f, RandomFloat(-100, 100), 1.0f);
        SideBySide("RRenderSharedData", i, SharedRun, c, SharedReset);
        if (c->op == 2)
            FakeViewOff();
        CheckBytes("RRenderSharedData objects", i, c->objectsOut[0], c->objectsOut[1], sizeof(c->objects));
        CheckBytes("RRenderSharedData physics", i, c->physicsOut[0], c->physicsOut[1], sizeof(c->physics));
        if (c->op == 0)
            CheckU32("SetVehiclesAllowed", i, c->answer[0], c->answer[1]);
    }
    CurrentObject = savedObject;
    free(c);
}

// ---- RRenderer, on a copy

struct RendererCase {
    int op;
    bool force;
    RRenderer copy;
    RRenderer copyIn;
    RRenderer after[2];
    uint32_t answer[2];
};

void RendererReset(void *context, bool) {
    RendererCase *c = static_cast<RendererCase *>(context);
    c->copy = c->copyIn;
}

void RendererRun(void *context, bool original) {
    RendererCase *c = static_cast<RendererCase *>(context);
    RRenderer *r = &c->copy;
    uint32_t answer = 0;
    switch (c->op) {
    case 0:
        answer = Address(original ? Orig_EndView(r, 0) : r->EndView());
        break;
    case 1:
        original ? Orig_Flush(r, 0, c->force) : r->Flush(c->force);
        break;
    case 2:
        answer = original ? Orig_EnableAlphaWrites(r, 0) : r->EnableAlphaWrites();
        break;
    case 3:
        answer = original ? Orig_DisableAlphaWrites(r, 0) : r->DisableAlphaWrites();
        break;
    default:
        original ? Orig_ConfigureRes(r, 0) : r->ConfigureRes();
        break;
    }
    c->answer[original ? 0 : 1] = answer & 0xff;
    c->after[original ? 0 : 1] = *r;
}

void TestRenderer() {
    RendererCase *c = static_cast<RendererCase *>(calloc(1, sizeof(RendererCase)));
    for (int i = 0; i < 100; i++) {
        c->copyIn = *fgRenderer;
        c->copyIn.framePending = (uint8_t)RandomInt(2);
        c->copyIn.alphaWrites = (uint8_t)RandomInt(2);
        c->copyIn.currentView = (RViewCamera *)(uintptr_t)(Random() & 0xfffff0);
        c->copyIn.screenWidth = Random();
        c->copyIn.videoMode = RandomInt(4);
        c->copyIn.presentFlag100 = (uint8_t)RandomInt(2);
        c->force = (Random() & 1) != 0;
        c->op = i % 5;
        SideBySide("RRenderer", i, RendererRun, c, RendererReset);
        CheckBytes("RRenderer copy", i, &c->after[0], &c->after[1], sizeof(RRenderer));
        CheckU32("RRenderer answer", i, c->answer[0], c->answer[1]);
    }
    free(c);
}

}  // namespace

void RendererShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_RENDERERSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    if (fgRenderer == NULL) {
        printf("[renderer] no renderer - skipped\n");
        fflush(stdout);
        return;
    }
    static_assert(sizeof(Snapshot::regions) == 0x2460 + 0xc + 4 + 0x1b0 + 0x260 + 4 + 0x18, "the regions' bytes");
    if (RegionBytes() != sizeof(g_base.regions)) {
        printf("[renderer] region table mismatch - skipped\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    Copy(&g_base, true);
    const struct {
        const char *name;
        void (*run)();
    } tests[] = {
        {"colour", TestColours},          {"shadow maps", TestShadowMaps}, {"view distances", TestViewDistances},
        {"fog", TestFog},                 {"draws", TestDraw},             {"draw groups", TestDrawGroups},
        {"instance draws", TestInstances}, {"shared data", TestSharedData}, {"renderer", TestRenderer},
        {"materials", TestMaterials},
    };
    for (const auto &test : tests) {
        SectionStart(test.name);
        test.run();
        SectionEnd();
    }
    Copy(&g_base, false);
    ResetFpu();
    printf("[renderer] colour, shadow maps, view distances, fog, draws, materials, draw groups, instance draws, shared "
           "data, renderer vs originals: %d cases, %d checks, %d differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[renderer]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
