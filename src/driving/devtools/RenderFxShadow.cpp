#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "RenderFxShadow.h"
#include "FpControl.h"

#include "../eagl/EaglGlobals.h"          // D3DDevicePointer
#include "../eagl/RenderContext.h"
#include "../eagl/View.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../render/Lightning.h"
#include "../render/PostProcessing.h"
#include "../render/RGlareManager.hpp"
#include "../render/RSceneObj.hpp"
#include "../render/Renderer.h"
#include "../render/ShadowMap.h"
#include "../render/SkyWater.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_RENDERFXSHADOW=1, from the first simulation tick: the R_F package's pure and stateful-but-contained code
// against the originals (the package's address ranges swapped back for each original run, common/xbeOriginal.h).
// Each case runs both sides from one state - the game's 16-bit random generator, the C runtime's rand (jumped to a
// fake with a state of our own), Util_GaussRandom's kept number, the simulation's step and tick - on separate
// working copies, and the results are compared byte for byte (heap structures by value, their pointers made
// indices into the blocks found on the walk).
//
//   - RLightning on private images (TheLightning pointed at each in turn), with random tuning: AddBolt between
//     world points and points on the player's car, AddRoundedBolt round the car with made-up locators (some
//     aroundObject), the whole segment tree after each; Update over random tick steps (bolts dying, midpoints
//     respawning); the strip built by every bolt's Draw for each pass; IsBoltAlive over the handles; Reset (the
//     random tables made again, the bolts and strip freed). The constructor and destructor on scratch images.
//     RenderBolts with UVolatileMaterial::Draw pointed at a recorder, when that function is ours to redirect.
//   - RGlareManager::CreateUVsFromTexIDs on copies of the live manager with random texture ids.
//   - The broken windows' batch (QueueBrokenWindowQuad through FUN_000a87e0's EAX entry) with random fill levels,
//     and the pane map's Min and Increment walking random trees; ScaleDebrisTables on the real and perturbed
//     tables.
//   - RShadowMap::LookupVariable, ProjectedShadowsEnabled, and SetCamera on the live shadow map for the player's
//     car along random directions (the texture matrix and the shadow viewport's bytes compared, then put back).
//   - The scorch marks' ground normal on random WWorldPos images; GetSubsampledBackBuffer on the live object.
//
// The draws (RSky::Draw, RShadowMap::Draw/Begin/ClearShadowMap/DrawSimpleShadow, RPostProcessing's, the windows'
// flush) and the constructors that make EAGL objects are tested in game by the lockstep runs.
//
// One mutation this catches: BuildNewSegment placing midpoint i at i / segments rather than (i + 1) / segments
// changes every midpoint's base in the first flattened tree; JitterStripPoints stepping the colour table by 5
// rather than 7 changes the strip's colours.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[5][2] = {
    { 0x0009e8e0, 0x000a0a50 }, { 0x000a4550, 0x000a50c0 }, { 0x000a5820, 0x000a6810 },
    { 0x000a8460, 0x000a8f10 }, { 0x000a9870, 0x000a9ee0 },
};

void OriginalWindow(bool original) {
    for (const uint32_t *range : kRanges)
        XbeOriginal_RestoreRange(range[0], range[1], original);
}

// ---- the originals

typedef void (__fastcall *ThisFn)(void *, int);
typedef int (__fastcall *AddBoltFn)(RLightning *, int, const Coord3 *, RSceneObj *, const ArticleEffect *,
                                    const Coord3 *, RSceneObj *, const ArticleEffect *, float, int);
typedef int (__fastcall *AddRoundedBoltFn)(RLightning *, int, RSceneObj *, const ArticleEffect *,
                                           const ArticleEffect *, float, float, float, float, bool, int, int, float,
                                           int);
typedef void (__fastcall *BoltDrawFn)(RLightningBolt *, int, RLightning *);
typedef bool (__fastcall *AliveFn)(RLightning *, int, int);
typedef RLightning *(__fastcall *LightningConstructFn)(RLightning *, int);
typedef WindowPaneNode *(*PaneMinFn)(WindowPaneNode *);
typedef void (__fastcall *PaneIncrementFn)(WindowPaneNode **, int);
typedef void *(*LookupFn)(const char *, bool *);
typedef bool (*EnabledFn)(void);
typedef void (__fastcall *SetCameraFn)(RShadowMap *, int, RSceneObj *, const Coord4 *);
typedef void (*ScaleFn)(void);
typedef void (__fastcall *NormalFn)(WWorldPos *, int, Coord4 *, int);
typedef EAGL::TAR *(__fastcall *SubsampledFn)(RPostProcessing *, int);

#define Orig_CalcRandomTables ((ThisFn)0x0009ed50)
#define Orig_AddBolt ((AddBoltFn)0x000a0400)
#define Orig_AddRoundedBolt ((AddRoundedBoltFn)0x000a0500)
#define Orig_Update ((ThisFn)0x0009fb90)
#define Orig_BoltDraw ((BoltDrawFn)0x0009f6b0)
#define Orig_RenderBolts ((ThisFn)0x0009f020)
#define Orig_IsBoltAlive ((AliveFn)0x0009f840)
#define Orig_Reset ((ThisFn)0x0009fe30)
#define Orig_LightningConstruct ((LightningConstructFn)0x000a02e0)
#define Orig_LightningDestruct ((ThisFn)0x0009fec0)
#define Orig_CreateUVs ((ThisFn)0x000a9870)
#define Orig_PaneMin ((PaneMinFn)0x000a87a0)
#define Orig_PaneIncrement ((PaneIncrementFn)0x000a8a80)
#define Orig_LookupVariable ((LookupFn)0x000a59c0)
#define Orig_ProjectedShadowsEnabled ((EnabledFn)0x000a5950)
#define Orig_SetCamera ((SetCameraFn)0x000a5c90)
#define Orig_ScaleDebrisTables ((ScaleFn)0x000a8e80)
#define Orig_GetNormal ((NormalFn)0x000a5080)
#define Orig_GetSubsampledBackBuffer ((SubsampledFn)0x000a4560)

const uint32_t kVolatileDraw = 0x0011c290;          // UVolatileMaterial::Draw
const uint32_t kTexturedDraw = 0x0011bd50;          // USimpleTexturedMaterial::Draw
const uint32_t kRand = 0x00133ee0;                  // the C runtime's rand

// ---- the game's state

#define FxRandomSeed U32_AT(0x001c45c4)
#define FxRandomMultiplier U32_AT(0x001c45c8)
#define FxSimTimeStep FLOAT_AT(0x00234e30)
#define FxSimStepCount I32_AT(0x00234e34)
#define FxSimStepsPerSecond I32_AT(0x00234e2c)
#define FxGaussSpare ((uint8_t *)0x00239a60)       // the kept number and its flag, 8 bytes
#define FxPlayerPhysics (*(PhysicsObject **)PTR_AT(0x00234e40))
#define FxGlareManager (*(RGlareManager **)0x00208cb4)
#define FxWindowState ((uint8_t *)0x00201850)      // the windows' texture, arrays, count and guard
const uint32_t kWindowStateBytes = 0x00202a74 - 0x00201850;
#define FxWindowCount I32_AT(0x00202860)
#define FxDebrisState ((uint8_t *)0x001c59e0)      // the debris vectors and their scaled copies
const uint32_t kDebrisStateBytes = 0x001c6bb0 - 0x001c59e0;
#define FxDebrisScaleA FLOAT_AT(0x001c5b84)
#define FxDebrisCountB I32_AT(0x001c8e44)
#define FxShadowZWrites (*(uint8_t *)0x001cb94c)   // RenderContextExtension's shadow of the Z-write state

// The draw-request rings (the renderer core's): the slot fields the drawing code fills
struct FxRequest {
    uint8_t unknown00[0x10];
    const void *texture;                // +0x10
    uint8_t unknown14[0x14];
    const void *positions;              // +0x28
    uint32_t unknown2C;
    const uint32_t *colours;            // +0x30
    uint32_t unknown34;
    const void *uvs;                    // +0x38
};
#define FxVolatileRequestIndex I32_AT(0x00243830)
#define FxVolatileRequests ((FxRequest **)0x00243878)
#define FxTexturedRequestIndex I32_AT(0x00243750)
#define FxTexturedRequests ((FxRequest **)0x00243710)

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[renderfx]   %s #%d: %s\n", what, index, detail);
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

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
    if (original)
        OriginalWindow(true);
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
    if (original)
        OriginalWindow(false);
    return ok;
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x0f1a5e17;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

int Range(int lo, int hi) {
    return lo + int(Next() % uint32_t(hi - lo + 1));
}

// ---- the C runtime's rand jumped to a fake with a state we set (the real one is per-thread CRT data)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_randHook;
Hook g_volatileEntryHook, g_texturedEntryHook;   // our Draws' entries: our callers call them directly
uint32_t g_rand;

int FakeRand() {
    g_rand = g_rand * 0x343fd + 0x269ec3;
    return int((g_rand >> 16) & 0x7fff);
}

bool HookInstall(Hook *h, uint32_t at, const void *to) {
    h->at = at;
    h->on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(h->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h->on = true;
    return true;
}

void HookRemove(Hook *h) {
    if (!h->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(h->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(h->at), h->saved, 5);
    VirtualProtect((void *)uintptr_t(h->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(h->at), 5);
    h->on = false;
}

// Where a patched entry's jump goes now
const void *JumpTarget(unsigned at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(at);
    if (code[0] != 0xe9)
        return NULL;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return reinterpret_cast<const void *>(at + 5 + relative);
}

// ---- the random state every lightning case starts from

struct RandomState {
    uint32_t seed;
    uint32_t multiplier;
    uint32_t rand;
    uint8_t gauss[8];
    int32_t step;
    float timeStep;
    int32_t perSecond;
};

void SetRandomState(const RandomState &s) {
    FxRandomSeed = s.seed;
    FxRandomMultiplier = s.multiplier;
    g_rand = s.rand;
    memcpy(FxGaussSpare, s.gauss, sizeof(s.gauss));
    FxSimStepCount = s.step;
    FxSimTimeStep = s.timeStep;
    FxSimStepsPerSecond = s.perSecond;
}

void GetRandomState(RandomState *s) {
    s->seed = FxRandomSeed;
    s->multiplier = FxRandomMultiplier;
    s->rand = g_rand;
    memcpy(s->gauss, FxGaussSpare, sizeof(s->gauss));
    s->step = FxSimStepCount;
    s->timeStep = FxSimTimeStep;
    s->perSecond = FxSimStepsPerSecond;
}

// ---- a growable log, and the lightning flattened into it by value

struct Log {
    uint8_t *data;
    size_t size, capacity;
};

void Append(Log *log, const void *bytes, size_t count) {
    if (log->size + count > log->capacity) {
        size_t grown = log->capacity * 2 + count + 4096;
        log->data = static_cast<uint8_t *>(realloc(log->data, grown));
        log->capacity = grown;
    }
    memcpy(log->data + log->size, bytes, count);
    log->size += count;
}

void AppendU32(Log *log, uint32_t value) {
    Append(log, &value, 4);
}

struct Block {
    const uint8_t *base;
    uint32_t bytes;
};

struct Blocks {
    Block list[4096];
    int count;
    bool overflow;
};

Blocks g_blocks;

void AddBlock(const void *base, uint32_t bytes) {
    if (g_blocks.count == 4096) {
        g_blocks.overflow = true;
        return;
    }
    g_blocks.list[g_blocks.count].base = static_cast<const uint8_t *>(base);
    g_blocks.list[g_blocks.count].bytes = bytes;
    g_blocks.count++;
}

// A pointer as the block it falls in and the offset in it
void AppendPointer(Log *log, const void *pointer) {
    const uint8_t *p = static_cast<const uint8_t *>(pointer);
    for (int i = 0; i < g_blocks.count; i++) {
        const Block &b = g_blocks.list[i];
        if (p >= b.base && (p < b.base + b.bytes || (b.bytes == 0 && p == b.base))) {
            AppendU32(log, uint32_t(i));
            AppendU32(log, uint32_t(p - b.base));
            return;
        }
    }
    AppendU32(log, 0xffffffff);
    AppendU32(log, 0);
}

void FlattenSegment(Log *log, const RLightningSegment *segment, int depth) {
    AppendPointer(log, segment->start);
    AppendPointer(log, segment->end);
    AppendU32(log, segment->midpoints.count);
    AppendU32(log, segment->parts.count);
    if (depth > 8)
        return;
    AddBlock(segment->midpoints.data, segment->midpoints.count * sizeof(RLightningMidPoint));
    if (segment->midpoints.count < 64)
        Append(log, segment->midpoints.data, segment->midpoints.count * sizeof(RLightningMidPoint));
    if (segment->parts.data == NULL || segment->parts.count > 64)
        return;
    AddBlock(segment->parts.data, segment->parts.count * sizeof(RLightningSegment));
    for (uint32_t i = 0; i < segment->parts.count; i++)
        FlattenSegment(log, &segment->parts.data[i], depth + 1);
}

void FlattenLightning(Log *log, const RLightning *lightning) {
    g_blocks.count = 0;
    RLightning image;
    memcpy(&image, lightning, sizeof(image));
    uint32_t size = lightning->bolts.Size(), capacity = lightning->bolts.Capacity();
    image.bolts.first = image.bolts.last = image.bolts.end = NULL;
    image.strip = NULL;
    Append(log, &image, sizeof(image));
    AppendU32(log, size);
    AppendU32(log, capacity);
    AppendU32(log, lightning->strip != NULL);
    for (uint32_t i = 0; i < size && i < 32; i++) {
        const RLightningBolt *bolt = lightning->bolts.first[i];
        AddBlock(bolt, sizeof(RLightningBolt));
        RLightningBolt fields;
        memcpy(&fields, bolt, sizeof(fields));
        fields.points.data = NULL;
        fields.segments.data = NULL;
        Append(log, &fields, sizeof(fields));
        AddBlock(bolt->points.data, bolt->points.count * sizeof(RLightningControlPoint));
        if (bolt->points.count < 64)
            Append(log, bolt->points.data, bolt->points.count * sizeof(RLightningControlPoint));
        if (bolt->segments.data == NULL || bolt->segments.count > 64)
            continue;
        AddBlock(bolt->segments.data, bolt->segments.count * sizeof(RLightningSegment));
        for (uint32_t s = 0; s < bolt->segments.count; s++)
            FlattenSegment(log, &bolt->segments.data[s], 0);
    }
}

void FlattenStrip(Log *log, const RLightning *lightning) {
    AppendU32(log, lightning->stripCount);
    if (lightning->strip == NULL)
        return;
    uint32_t count = lightning->stripCount < uint32_t(kLightningStripVertices) ? lightning->stripCount
                                                                               : uint32_t(kLightningStripVertices);
    Append(log, lightning->strip->positions, count * sizeof(Coord4));
    Append(log, lightning->strip->colours, count * sizeof(uint32_t));
    Append(log, lightning->strip->uvs, count * sizeof(float) * 2);
}

void FlattenRandom(Log *log) {
    RandomState s;
    GetRandomState(&s);
    Append(log, &s, sizeof(s));
}

// ---- the material draws pointed at recorders

Log *g_drawLog;

void RecordDraw(const FxRequest *request, int primitive, uint32_t count, uint32_t vertexBytes, uint32_t uvBytes) {
    if (g_drawLog == NULL)
        return;
    AppendU32(g_drawLog, uint32_t(primitive));
    AppendU32(g_drawLog, count);
    AppendU32(g_drawLog, uint32_t(uintptr_t(request->texture)));
    if (count > 0x800)
        return;
    if (request->positions != NULL)
        Append(g_drawLog, request->positions, count * vertexBytes);
    if (request->colours != NULL)
        Append(g_drawLog, request->colours, count * 4);
    if (request->uvs != NULL)
        Append(g_drawLog, request->uvs, count * uvBytes);
}

void __fastcall RecordVolatileDraw(void *material, int, int primitive, uint32_t count, const MATRIX4 *transform) {
    (void)material;
    (void)transform;
    RecordDraw(FxVolatileRequests[FxVolatileRequestIndex], primitive, count, sizeof(Coord4), 8);
}

void __fastcall RecordTexturedDraw(void *material, int, int primitive, uint32_t count, const MATRIX4 *transform) {
    (void)material;
    (void)transform;
    RecordDraw(FxTexturedRequests[FxTexturedRequestIndex], primitive, count, sizeof(Coord4), sizeof(Coord4));
}

bool g_canDrawVolatile = false, g_canDrawTextured = false;

// ---- the lightning

RLightning g_template;
RSceneObj *g_car = NULL;
uint8_t g_locators[4][0x40];        // made-up effect locators: a position and type 0 (a plain translation)

struct BoltInput {
    bool rounded;
    Coord3 from, to;
    RSceneObj *fromObj, *toObj;
    const ArticleEffect *fromLocator, *toLocator;
    float bulge;
    int life;
    float driftMin, driftMax, driftVariance, spread;
    bool aroundObject;
    int pointCount, levels;
};

struct LightningRun {
    RLightning *image;
    RandomState start;
    int boltCount;
    BoltInput bolts[3];
    int stepCount;
    int steps[6];
    bool render;
    Log log;
};

void DrawStrip(RLightning *lightning, bool original) {
    // RLightning::Draw without its RenderBolts
    if (original)
        Orig_Update(lightning, 0);
    else
        lightning->Update();
    int tick = FxSimStepCount;
    lightning->randomNumIndex = (tick * 11) & 63;
    lightning->randomCoordIndex = (tick * 5) & 63;
    lightning->randomColourIndex = (tick * 7) & 63;
    lightning->stripCount = 0;
    lightning->joinNext = false;
    lightning->textureCycle = 0;
    lightning->alphaScale = 1.0f;
    for (RLightningBolt **bolt = lightning->bolts.first; bolt != lightning->bolts.last; bolt++) {
        for (int pass = 0; pass < lightning->passes; pass++) {
            if (original)
                Orig_BoltDraw(*bolt, 0, lightning);
            else
                (*bolt)->Draw(lightning);
        }
    }
}

void RunLightning(void *context, bool original) {
    LightningRun *run = static_cast<LightningRun *>(context);
    RLightning *lightning = run->image;
    TheLightning = lightning;
    SetRandomState(run->start);
    Log *log = &run->log;

    for (int i = 0; i < run->boltCount; i++) {
        const BoltInput &b = run->bolts[i];
        int handle;
        if (b.rounded)
            handle = original ? Orig_AddRoundedBolt(lightning, 0, b.fromObj, b.fromLocator, b.toLocator, b.driftMin,
                                                    b.driftMax, b.driftVariance, b.spread, b.aroundObject,
                                                    b.pointCount, b.levels, b.bulge, b.life)
                              : lightning->AddRoundedBolt(b.fromObj, b.fromLocator, b.toLocator, b.driftMin,
                                                          b.driftMax, b.driftVariance, b.spread, b.aroundObject,
                                                          b.pointCount, b.levels, b.bulge, b.life);
        else
            handle = original ? Orig_AddBolt(lightning, 0, &b.from, b.fromObj, b.fromLocator, &b.to, b.toObj,
                                             b.toLocator, b.bulge, b.life)
                              : lightning->AddBolt(&b.from, b.fromObj, b.fromLocator, &b.to, b.toObj, b.toLocator,
                                                   b.bulge, b.life);
        AppendU32(log, uint32_t(handle));
    }
    AppendU32(log, 0xb0b0b0b0);
    FlattenLightning(log, lightning);
    FlattenRandom(log);

    for (int s = 0; s < run->stepCount; s++) {
        FxSimStepCount += run->steps[s];
        AppendU32(log, 0x57e90000 + s);
        DrawStrip(lightning, original);
        FlattenLightning(log, lightning);
        FlattenStrip(log, lightning);
        FlattenRandom(log);
        for (int handle = 0; handle <= lightning->nextBoltHandle; handle++)
            AppendU32(log, original ? Orig_IsBoltAlive(lightning, 0, handle) : lightning->IsBoltAlive(handle));
        if (run->render) {
            g_drawLog = log;
            if (original)
                Orig_RenderBolts(lightning, 0);
            else
                lightning->RenderBolts();
            g_drawLog = NULL;
            FlattenLightning(log, lightning);
        }
    }

    AppendU32(log, 0x2e5e7000);
    if (original)
        Orig_Reset(lightning, 0);
    else
        lightning->Reset();
    FlattenLightning(log, lightning);
    FlattenRandom(log);
}

RandomState RandomStart() {
    RandomState s;
    s.seed = Next() & 0xffff;
    s.multiplier = (Next() << 1) | 1;
    s.rand = Next() ^ (Next() << 8);
    memset(s.gauss, 0, sizeof(s.gauss));
    if (Next() % 3 == 0) {
        float kept = Uniform(-2.0f, 2.0f);
        memcpy(s.gauss, &kept, 4);
        s.gauss[4] = 1;
    }
    s.step = Range(100, 100000);
    s.timeStep = Next() % 2 ? 1.0f / 30.0f : Uniform(0.01f, 0.05f);
    s.perSecond = Next() % 2 ? 30 : Range(1, 60);
    return s;
}

void RandomTuning(RLightning *l) {
    l->width = Uniform(0.02f, 0.5f);
    l->squiggle = Uniform(0.0f, 0.3f);
    l->spread = Uniform(0.0f, 0.3f);
    l->driftRate = Uniform(0.0f, 1.0f);
    l->driftVariance = Uniform(0.0f, 1.0f);
    l->lifeVariance = Uniform(0.0f, 1.0f);
    l->alphaInitial = Uniform(0.0f, 1.0f);
    l->alphaDecay = Uniform(0.0f, 1.0f);
    l->alphaNoise = Uniform(0.0f, 0.5f);
    l->segments = Range(2, 5);
    l->levels = Range(1, 4);
    l->passes = Range(1, 2);
    l->nextBoltHandle = Range(0, 10);
    l->lastSimTick = 0;
    l->randomCoordIndex = Range(0, 63);
    l->randomColourIndex = Range(0, 63);
    l->randomNumIndex = Range(0, 63);
}

Coord3 RandomPoint(float range) {
    Coord3 p = { Uniform(-range, range), Uniform(-range, range), Uniform(-range, range) };
    return p;
}

void TestLightning() {
    // The template: the live lightning's material and textures, everything else ours
    memset(&g_template, 0, sizeof(g_template));
    if (TheLightning != NULL)
        memcpy(&g_template, TheLightning, sizeof(g_template));
    g_template.bolts.first = g_template.bolts.last = g_template.bolts.end = NULL;
    g_template.strip = NULL;
    g_template.stripCount = 0;
    g_template.joinNext = false;

    RLightning *saved = TheLightning;
    static LightningRun runs[2];
    for (int c = 0; c < 120; c++) {
        RandomTuning(&g_template);
        LightningRun input;
        memset(&input, 0, sizeof(input));
        input.start = RandomStart();
        input.boltCount = Range(1, 3);
        bool rounded = g_car != NULL && c % 4 == 3;
        if (rounded) {
            g_template.segments = Range(2, 3);
            g_template.passes = 1;
        }
        for (int i = 0; i < input.boltCount; i++) {
            BoltInput &b = input.bolts[i];
            b.rounded = rounded;
            b.from = RandomPoint(50.0f);
            b.to = RandomPoint(50.0f);
            b.fromObj = g_car != NULL && Next() % 3 == 0 ? g_car : NULL;
            b.toObj = g_car != NULL && Next() % 3 == 0 ? g_car : NULL;
            b.bulge = Uniform(0.0f, 2.0f);
            b.life = Range(1, 20);
            if (rounded) {
                b.fromObj = g_car;
                b.fromLocator = reinterpret_cast<const ArticleEffect *>(g_locators[Range(0, 3)]);
                b.toLocator = reinterpret_cast<const ArticleEffect *>(g_locators[Range(0, 3)]);
                b.driftMin = Uniform(-1.0f, 1.0f);
                b.driftMax = Uniform(-1.0f, 2.0f);
                b.driftVariance = Uniform(0.0f, 1.0f);
                b.spread = Uniform(0.0f, 0.5f);
                b.aroundObject = Next() % 2 == 0;
                b.pointCount = Range(2, 4);
                b.levels = Range(0, 2);
            }
        }
        input.stepCount = Range(1, 6);
        for (int s = 0; s < input.stepCount; s++)
            input.steps[s] = Next() % 5 == 0 ? Range(-2, 0) : Range(1, 5);
        input.render = g_canDrawVolatile && Next() % 2 == 0;

        for (int side = 0; side < 2; side++) {
            runs[side] = input;
            runs[side].image = static_cast<RLightning *>(malloc(sizeof(RLightning)));
            memcpy(runs[side].image, &g_template, sizeof(RLightning));
        }
        g_cases++;
        bool ok = Guarded(RunLightning, &runs[0], true);
        ok = Guarded(RunLightning, &runs[1], false) && ok;
        if (ok) {
            CheckU32("lightning log size", c, uint32_t(runs[0].log.size), uint32_t(runs[1].log.size));
            if (runs[0].log.size == runs[1].log.size)
                CheckBytes("lightning", c, runs[0].log.data, runs[1].log.data, runs[0].log.size);
        }
        for (int side = 0; side < 2; side++) {
            free(runs[side].image);
            free(runs[side].log.data);
            runs[side].log.data = NULL;
        }
    }
    TheLightning = saved;
    if (g_blocks.overflow)
        printf("[renderfx]   the block list overflowed: some lightning pointers compared as unknown\n");
}

// The constructor and destructor on scratch images (the material made and destroyed, the textures looked up)
struct ConstructRun {
    RLightning image;
    RLightning after;
};

void RunConstruct(void *context, bool original) {
    ConstructRun *run = static_cast<ConstructRun *>(context);
    if (original)
        Orig_LightningConstruct(&run->image, 0);
    else
        run->image.Construct();
    memcpy(&run->after, &run->image, sizeof(RLightning));
    if (original)
        Orig_LightningDestruct(&run->image, 0);
    else
        run->image.Destruct();
}

void TestLightningConstruct() {
    static ConstructRun runs[2];
    RLightning *saved = TheLightning;
    for (int c = 0; c < 4; c++) {
        RandomState start = RandomStart();
        RandomState end[2];
        for (int side = 0; side < 2; side++) {
            memcpy(&runs[side].image, &g_template, sizeof(RLightning));
            TheLightning = &runs[side].image;
            SetRandomState(start);
            Guarded(RunConstruct, &runs[side], side == 0);
            GetRandomState(&end[side]);
        }
        g_cases++;
        CheckBytes("lightning constructed", c, &runs[0].after, &runs[1].after, sizeof(RLightning));
        CheckBytes("lightning destructed", c, &runs[0].image, &runs[1].image, sizeof(RLightning));
        CheckBytes("lightning construct random state", c, &end[0], &end[1], sizeof(RandomState));
    }
    TheLightning = saved;
}

// ---- the glare types' texture coordinates

void RunCreateUVs(void *context, bool original) {
    RGlareManager *glare = static_cast<RGlareManager *>(context);
    if (original)
        Orig_CreateUVs(glare, 0);
    else
        glare->CreateUVsFromTexIDs();
}

void TestGlare() {
    static uint8_t copies[2][sizeof(RGlareManager)];
    for (int c = 0; c < 12; c++) {
        if (FxGlareManager != NULL)
            memcpy(copies[0], FxGlareManager, sizeof(RGlareManager));
        else
            memset(copies[0], 0, sizeof(RGlareManager));
        RGlareManager *glare = reinterpret_cast<RGlareManager *>(copies[0]);
        if (c > 0) {
            for (int i = 0; i < 80; i++) {
                glare->types[i].haloTexture = Next() % 8 == 0 ? Next() : Next() % 20;
                glare->types[i].spikeTexture = Next() % 8 == 0 ? Next() : Next() % 20;
            }
        }
        memcpy(copies[1], copies[0], sizeof(RGlareManager));
        g_cases++;
        Guarded(RunCreateUVs, copies[0], true);
        Guarded(RunCreateUVs, copies[1], false);
        CheckBytes("glare UVs", c, copies[0], copies[1], sizeof(RGlareManager));
    }
}

// ---- the windows

// FUN_000a87e0 (0x000a87e0) called through its EAX entry: ours outside the window, the original inside it
__declspec(naked) void CallQuadEntry(const Coord4 *, const Coord4 *, const Coord4 *, const Coord4 *, uint32_t,
                                     const Coord4 *, int) {
    __asm {
        mov eax, dword ptr [esp + 4]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        mov ecx, 0x000a87e0
        call ecx
        add esp, 0x18
        ret
    }
}

struct QuadRun {
    Coord4 vertices[4];
    Coord4 uvs[4];
    uint32_t colour;
    int flush;
    int quads;
};

void RunQuads(void *context, bool original) {
    (void)original;
    QuadRun *run = static_cast<QuadRun *>(context);
    for (int i = 0; i < run->quads; i++)
        CallQuadEntry(&run->vertices[0], &run->vertices[1], &run->vertices[2], &run->vertices[3], run->colour,
                      run->uvs, 0);
    if (run->flush)
        CallQuadEntry(NULL, NULL, NULL, NULL, 0, NULL, 1);
}

void TestWindowQuads() {
    static uint8_t saved[kWindowStateBytes], after[2][kWindowStateBytes];
    memcpy(saved, FxWindowState, kWindowStateBytes);
    Log logs[2] = {};
    for (int c = 0; c < 60; c++) {
        QuadRun run;
        for (int i = 0; i < 4; i++) {
            Coord3 p = RandomPoint(100.0f);
            run.vertices[i] = { p.x, p.y, p.z, Uniform(0.0f, 1.0f) };
            run.uvs[i] = { Uniform(0.0f, 1.0f), Uniform(0.0f, 1.0f), 1.0f, 1.0f };
        }
        run.colour = Next() ^ (Next() << 16);
        // Without the textured draw's recorder the batch is kept from drawing: no quad finds it past 0x78
        int fill = Range(0, 0x78);
        run.quads = g_canDrawTextured ? Range(0, 30) : Range(0, (0x78 - fill) / 6 + 1);
        run.flush = g_canDrawTextured ? int(Next() % 2) : 0;
        uint32_t guard = Next() % 2;
        for (int side = 0; side < 2; side++) {
            memcpy(FxWindowState, saved, kWindowStateBytes);
            FxWindowCount = fill;
            U32_AT(0x00202a70) = guard;
            logs[side].size = 0;
            g_drawLog = &logs[side];
            Guarded(RunQuads, &run, side == 0);
            g_drawLog = NULL;
            memcpy(after[side], FxWindowState, kWindowStateBytes);
        }
        g_cases++;
        CheckBytes("window batch", c, after[0], after[1], kWindowStateBytes);
        CheckU32("window draws", c, uint32_t(logs[0].size), uint32_t(logs[1].size));
        if (logs[0].size == logs[1].size && logs[0].size != 0)
            CheckBytes("window draw log", c, logs[0].data, logs[1].data, logs[0].size);
    }
    memcpy(FxWindowState, saved, kWindowStateBytes);
    free(logs[0].data);
    free(logs[1].data);
}

// A random tree of pane-map nodes: an unbalanced binary search tree on random keys, the head its nil
struct PaneTree {
    WindowPaneNode head;
    WindowPaneNode nodes[48];
    int count;
    WindowPaneNode *order[2][64];
    int lengths[2];
};

void BuildPaneTree(PaneTree *tree) {
    memset(tree, 0, sizeof(PaneTree));
    WindowPaneNode *head = &tree->head;
    head->isNil = 1;
    head->parent = head->left = head->right = head;
    tree->count = Range(0, 48);
    for (int i = 0; i < tree->count; i++) {
        WindowPaneNode *node = &tree->nodes[i];
        node->value.pane = reinterpret_cast<const WindowPane *>(uintptr_t(Next()));
        node->left = node->right = head;
        node->color = uint8_t(Next() & 1);
        if (head->parent == head) {
            head->parent = node;
            node->parent = head;
            continue;
        }
        WindowPaneNode *at = head->parent;
        for (;;) {
            WindowPaneNode **link = node->value.pane < at->value.pane ? &at->left : &at->right;
            if (*link == head) {
                *link = node;
                node->parent = at;
                break;
            }
            at = *link;
        }
    }
    if (head->parent != head) {
        WindowPaneNode *n = head->parent;
        while (n->left != head)
            n = n->left;
        head->left = n;
        n = head->parent;
        while (n->right != head)
            n = n->right;
        head->right = n;
    }
}

void RunPaneWalk(void *context, bool original) {
    PaneTree *tree = static_cast<PaneTree *>(context);
    int side = original ? 0 : 1;
    int length = 0;
    WindowPaneNode *root = tree->head.parent;
    if (root != &tree->head) {
        WindowPaneIterator step;
        step.node = original ? Orig_PaneMin(root) : WindowPaneMap::Min(root);
        while (length < 63) {
            tree->order[side][length++] = step.node;
            if (step.node->isNil)
                break;
            if (original)
                Orig_PaneIncrement(&step.node, 0);
            else
                step.Increment();
        }
    }
    // The head steps nowhere
    WindowPaneIterator atHead;
    atHead.node = &tree->head;
    if (original)
        Orig_PaneIncrement(&atHead.node, 0);
    else
        atHead.Increment();
    tree->order[side][length++] = atHead.node;
    tree->lengths[side] = length;
}

void TestPaneWalk() {
    static PaneTree tree;
    for (int c = 0; c < 80; c++) {
        BuildPaneTree(&tree);
        g_cases++;
        Guarded(RunPaneWalk, &tree, true);
        Guarded(RunPaneWalk, &tree, false);
        CheckU32("pane walk length", c, uint32_t(tree.lengths[0]), uint32_t(tree.lengths[1]));
        if (tree.lengths[0] == tree.lengths[1])
            CheckBytes("pane walk", c, tree.order[0], tree.order[1], tree.lengths[0] * sizeof(WindowPaneNode *));
    }
}

void RunDebris(void *context, bool original) {
    (void)context;
    if (original)
        Orig_ScaleDebrisTables();
    else
        ScaleDebrisTables();
}

void TestDebris() {
    static uint8_t saved[kDebrisStateBytes], start[kDebrisStateBytes], after[2][kDebrisStateBytes];
    uint8_t savedOther[8];
    memcpy(saved, FxDebrisState, kDebrisStateBytes);
    memcpy(savedOther, &FxDebrisCountB, 4);
    for (int c = 0; c < 12; c++) {
        memcpy(FxDebrisState, saved, kDebrisStateBytes);
        if (c > 0) {
            float *vectors = reinterpret_cast<float *>(FxDebrisState);
            for (uint32_t i = 0; i < kDebrisStateBytes / 4; i++)
                if (Next() % 3 == 0)
                    vectors[i] = Uniform(-10.0f, 10.0f);
            FxDebrisScaleA = Uniform(-2.0f, 2.0f);
            FxDebrisCountB = Range(0, 30);
        }
        memcpy(start, FxDebrisState, kDebrisStateBytes);
        for (int side = 0; side < 2; side++) {
            memcpy(FxDebrisState, start, kDebrisStateBytes);
            Guarded(RunDebris, NULL, side == 0);
            memcpy(after[side], FxDebrisState, kDebrisStateBytes);
        }
        g_cases++;
        CheckBytes("debris tables", c, after[0], after[1], kDebrisStateBytes);
    }
    memcpy(FxDebrisState, saved, kDebrisStateBytes);
    memcpy(&FxDebrisCountB, savedOther, 4);
}

// ---- the shadow map

struct LookupRun {
    const char *name;
    void *result;
    bool found;
    bool enabled;
};

void RunLookup(void *context, bool original) {
    LookupRun *run = static_cast<LookupRun *>(context);
    run->result = original ? Orig_LookupVariable(run->name, &run->found) : RShadowMap::LookupVariable(run->name, &run->found);
    run->enabled = original ? Orig_ProjectedShadowsEnabled() : RShadowMap::ProjectedShadowsEnabled();
}

void TestShadowLookup() {
    const char *names[] = {
        "GAME::ShadowFatnessConstants", "GAME::ShadowPlaneConstants", "GAME::ZeroOneTwoThree", "GAME::ShadowTexture",
        "GAME::ShadowMatrix", "GAME::ShadowMatri", "GAME::ShadowMatrixX", "GAME::", "", "game::ShadowTexture",
        "GAME::ShadowTextures", "EAGL::ViewPort::gpModelViewProjectionMatrix",
    };
    for (int c = 0; c < int(sizeof(names) / sizeof(names[0])); c++) {
        if (TheShadowMap == NULL && (c == 3 || c == 4))
            continue;
        LookupRun runs[2];
        bool initial = Next() % 2 == 0;
        for (int side = 0; side < 2; side++) {
            runs[side].name = names[c];
            runs[side].found = initial;
            Guarded(RunLookup, &runs[side], side == 0);
        }
        g_cases++;
        CheckU32("shadow lookup", c, uint32_t(uintptr_t(runs[0].result)), uint32_t(uintptr_t(runs[1].result)));
        CheckU32("shadow lookup found", c, runs[0].found, runs[1].found);
        CheckU32("projected shadows", c, runs[0].enabled, runs[1].enabled);
    }
}

struct CameraRun {
    Coord4 direction;
};

void RunSetCamera(void *context, bool original) {
    CameraRun *run = static_cast<CameraRun *>(context);
    if (original)
        Orig_SetCamera(TheShadowMap, 0, g_car, &run->direction);
    else
        TheShadowMap->SetCamera(g_car, &run->direction);
}

void TestShadowCamera() {
    RShadowMap *map = TheShadowMap;
    if (map == NULL || map->data == NULL || map->viewPort == NULL || g_car == NULL || g_car->physics == NULL) {
        printf("[renderfx] no shadow map or car - SetCamera skipped\n");
        return;
    }
    if (map->viewPort->active != 0) {
        printf("[renderfx] the shadow viewport is active - SetCamera skipped\n");
        return;
    }
    static uint8_t savedMatrix[sizeof(MATRIX4)], savedView[sizeof(EAGL::ViewPort)];
    static uint8_t matrices[2][sizeof(MATRIX4)], views[2][sizeof(EAGL::ViewPort)];
    memcpy(savedMatrix, &map->data->matrix, sizeof(MATRIX4));
    memcpy(savedView, map->viewPort, sizeof(EAGL::ViewPort));
    for (int c = 0; c < 40; c++) {
        CameraRun run;
        Coord3 d = RandomPoint(1.0f);
        if (c == 0)
            d = { 0.3f, -1.0f, 0.2f };
        float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (length < 0.01f)
            continue;
        run.direction = { d.x / length, d.y / length, d.z / length, Uniform(-1.0f, 1.0f) };
        for (int side = 0; side < 2; side++) {
            memcpy(&map->data->matrix, savedMatrix, sizeof(MATRIX4));
            memcpy(map->viewPort, savedView, sizeof(EAGL::ViewPort));
            Guarded(RunSetCamera, &run, side == 0);
            memcpy(matrices[side], &map->data->matrix, sizeof(MATRIX4));
            memcpy(views[side], map->viewPort, sizeof(EAGL::ViewPort));
        }
        g_cases++;
        CheckBytes("shadow matrix", c, matrices[0], matrices[1], sizeof(MATRIX4));
        CheckBytes("shadow view", c, views[0], views[1], sizeof(EAGL::ViewPort));
    }
    memcpy(&map->data->matrix, savedMatrix, sizeof(MATRIX4));
    memcpy(map->viewPort, savedView, sizeof(EAGL::ViewPort));
}

// ---- the scorch marks' ground normal, the subsampled back buffer

struct NormalRun {
    uint8_t position[sizeof(WWorldPos)];
    Coord4 normal;
};

void RunNormal(void *context, bool original) {
    NormalRun *run = static_cast<NormalRun *>(context);
    WWorldPos *position = reinterpret_cast<WWorldPos *>(run->position);
    if (original)
        Orig_GetNormal(position, 0, &run->normal, 0);
    else
        position->GetNormal(&run->normal, 0);
}

void TestNormal() {
    for (int c = 0; c < 60; c++) {
        NormalRun runs[2];
        float *words = reinterpret_cast<float *>(runs[0].position);
        for (uint32_t i = 0; i < sizeof(WWorldPos) / 4; i++)
            words[i] = Uniform(-20.0f, 20.0f);
        reinterpret_cast<WWorldPos *>(runs[0].position)->valid = uint8_t(Next() % 2);
        runs[0].normal = { Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1) };
        runs[1] = runs[0];
        g_cases++;
        Guarded(RunNormal, &runs[0], true);
        Guarded(RunNormal, &runs[1], false);
        CheckBytes("ground normal", c, &runs[0].normal, &runs[1].normal, sizeof(Coord4));
    }
}

struct SubsampledRun {
    RPostProcessing *post;
    EAGL::TAR *result;
};

void RunSubsampled(void *context, bool original) {
    SubsampledRun *run = static_cast<SubsampledRun *>(context);
    run->result = original ? Orig_GetSubsampledBackBuffer(run->post, 0) : run->post->GetSubsampledBackBuffer();
}

void TestSubsampled() {
    if (ThePostProcessing == NULL)
        return;
    SubsampledRun runs[2] = { { ThePostProcessing, NULL }, { ThePostProcessing, NULL } };
    g_cases++;
    Guarded(RunSubsampled, &runs[0], true);
    Guarded(RunSubsampled, &runs[1], false);
    CheckU32("subsampled back buffer", 0, uint32_t(uintptr_t(runs[0].result)), uint32_t(uintptr_t(runs[1].result)));
}

}  // namespace

void RenderFxShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_RENDERFXSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    // The game state the cases change, kept to put back
    RandomState saved;
    uint32_t savedRand = 0;
    GetRandomState(&saved);
    void *savedDevice = D3DDevicePointer;
    EAGL::RenderContext *context = fgRenderer != NULL ? fgRenderer->renderContext : NULL;
    uint8_t zWrites = context != NULL ? context->zWritesEnable : 1;
    uint8_t shadowZWrites = FxShadowZWrites;

    if (FxPlayerPhysics != NULL)
        g_car = FxPlayerPhysics->renderObject;
    for (int i = 0; i < 4; i++) {
        memset(g_locators[i], 0, sizeof(g_locators[i]));
        float *position = reinterpret_cast<float *>(g_locators[i]);
        position[0] = Uniform(-2.0f, 2.0f);
        position[1] = Uniform(-1.0f, 1.0f);
        position[2] = Uniform(-3.0f, 3.0f);
    }

    // The material draws pointed at recorders, when they are patched (ours) to redirect; the D3D device hidden so
    // the Z-write state is not sent while they run
    const void *volatileDraw = JumpTarget(kVolatileDraw);
    const void *texturedDraw = JumpTarget(kTexturedDraw);
    g_canDrawVolatile = volatileDraw != NULL && context != NULL && FxVolatileRequests[FxVolatileRequestIndex] != NULL &&
                        XbeOriginal_Redirect(kVolatileDraw, (const void *)&RecordVolatileDraw);
    g_canDrawTextured = texturedDraw != NULL && FxTexturedRequests[FxTexturedRequestIndex] != NULL &&
                        XbeOriginal_Redirect(kTexturedDraw, (const void *)&RecordTexturedDraw);
    if (g_canDrawVolatile)
        HookInstall(&g_volatileEntryHook, uint32_t(uintptr_t(volatileDraw)), (const void *)&RecordVolatileDraw);
    if (g_canDrawTextured)
        HookInstall(&g_texturedEntryHook, uint32_t(uintptr_t(texturedDraw)), (const void *)&RecordTexturedDraw);
    if (!g_canDrawVolatile || !g_canDrawTextured)
        printf("[renderfx] a material's Draw is not ours yet - %s%s skipped\n",
               g_canDrawVolatile ? "" : "RenderBolts ", g_canDrawTextured ? "" : "the window batch's draws ");
    D3DDevicePointer = NULL;
    static uint8_t savedRequests[2][sizeof(FxRequest)];
    FxRequest *volatileRequest = FxVolatileRequests[FxVolatileRequestIndex];
    FxRequest *texturedRequest = FxTexturedRequests[FxTexturedRequestIndex];
    if (volatileRequest != NULL)
        memcpy(savedRequests[0], volatileRequest, sizeof(FxRequest));
    if (texturedRequest != NULL)
        memcpy(savedRequests[1], texturedRequest, sizeof(FxRequest));

    if (HookInstall(&g_randHook, kRand, (const void *)FakeRand)) {
        savedRand = g_rand;
        TestLightning();
        TestLightningConstruct();
        HookRemove(&g_randHook);
        g_rand = savedRand;
    } else {
        printf("[renderfx] could not jump the C runtime's rand to the fake - the lightning skipped\n");
    }
    TestGlare();
    TestWindowQuads();
    TestPaneWalk();
    TestDebris();
    TestShadowLookup();
    TestShadowCamera();
    TestNormal();
    TestSubsampled();

    D3DDevicePointer = savedDevice;
    if (volatileRequest != NULL)
        memcpy(volatileRequest, savedRequests[0], sizeof(FxRequest));
    if (texturedRequest != NULL)
        memcpy(texturedRequest, savedRequests[1], sizeof(FxRequest));
    HookRemove(&g_volatileEntryHook);
    HookRemove(&g_texturedEntryHook);
    if (g_canDrawVolatile)
        XbeOriginal_Redirect(kVolatileDraw, volatileDraw);
    if (g_canDrawTextured)
        XbeOriginal_Redirect(kTexturedDraw, texturedDraw);
    if (context != NULL)
        context->zWritesEnable = zWrites;
    FxShadowZWrites = shadowZWrites;
    SetRandomState(saved);
    ResetFpu();
    printf("[renderfx] lightning, glare UVs, windows, debris, shadow map, ground normal vs originals: %d cases, %d checks, "
           "%d differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[renderfx]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
