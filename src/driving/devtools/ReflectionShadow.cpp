#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "ReflectionShadow.h"
#include "FpControl.h"

#include "../render/Colorize.h"
#include "../render/Decals.h"
#include "../render/Gain.h"
#include "../render/LensFlare.h"
#include "../render/Lights.h"
#include "../render/Reflection.h"
#include "../render/RSceneObj.hpp"
#include "../anim/Character.h"            // LightBlock
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <bit>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_REFLECTIONSHADOW=1, from the first simulation tick: package R_E's ports against the originals
// (0x000980e0-0x00099e20, 0x0009a420-0x0009b680 and 0x0009de00-0x0009e8e0 swapped back in for each original run,
// common/xbeOriginal.h), on identical inputs, compared byte for byte. Before each side's run every byte the call can
// write is put back.
//
//   - The sphere map maths (ReflSphereMapCoords, ReflHemisphereCoords, ReflMapVertex) and CylinderSection on
//     random directions, heights past +-1, zero and NaN components.
//   - RReflection's queries on the live object: LightingProps for every kind and every car name, "Character",
//     "Dynamic Objects" (in other cases too) and an unknown name; GetReflectionData2; the six getters.
//   - RReflection's state on a copy of the live object and its private data (the lighting records shared, put
//     back): random sequences of PrivateSubmitSceneObj (equal and NaN distances among them) and
//     DeregisterSceneObj with stand-in objects (neither reads them), EnableReflectionMapWarpage with perturbed
//     warp globals, SetReflectiveSpecularStrength (signalling NaNs among the values), ResetSceneObjDistances on
//     empty slots, and on the live slots with SetReflectivity when the live reflection holds objects.
//   - RColorize on a copy of the live one: SetAreaBrightness, Enable/DisableMotionBlur, SetEnabled with every mode
//     and out-of-range ones, Reset; the missile camera's and light manager's fields, ActManager's infrared state
//     and light blocks compared too.
//   - RLensFlareManager on a copy of the live data: Reset, Enable, EndFrame, AddFlare at points round the eye
//     (both the sun's and the moon's bounds), every flare and count compared.
//   - RDecalManager on a copy of the live one: AddDecal of every type round the ring and Reset, the decal
//     arrays, GeoPrims and render state compared (the live ones put back afterwards).
//   - RGain::Reset and Draw at the identity on a copy; FeatureManager's three functions.
//
// Construction, destruction, InitPostSim and everything that draws (RenderView, UpdateMaps, DrawSphereMap,
// SceneObjRender, RColorize::Draw, DrawDecals, RGain::Draw off the identity, TestFlares, DrawFlares) are covered by
// the lockstep runs.
//
// One mutation this catches: PrivateSubmitSceneObj counting an object at an equal distance as nearer (>= for >)
// changes the ranks in the cases that submit a distance already held.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x000980e0, 0x00099e20, original);
    XbeOriginal_RestoreRange(0x0009a420, 0x0009b680, original);
    XbeOriginal_RestoreRange(0x0009de00, 0x0009e8e0, original);
}

// ---- the originals

typedef void (*SphereCoordsFn)(const Coord4 *, Coord4 *);
typedef void (*HemisphereCoordsFn)(const Coord4 *, Coord4 *, bool *);
typedef void (*MapVertexFn)(float, float, bool, Coord4 *, Coord4 *);
typedef void (*CylinderFn)(Coord3 *, float (*)[2], float, float, float, float);
typedef ReflMaterial *(__fastcall *LightingPropsFn)(RReflection *, int, int, const char *);
typedef float *(__fastcall *Data2Fn)(RReflection *, int, const char *);
typedef void *(__fastcall *GetterFn)(RReflection *, int);
typedef void (__fastcall *ReflBoolFn)(RReflection *, int, bool);
typedef void (__fastcall *SpecularFn)(RReflection *, int, int, float);
typedef void (__fastcall *SubmitFn)(RReflection *, int, float, RSceneObj *);
typedef void (__fastcall *ReflObjectFn)(RReflection *, int, RSceneObj *);
typedef void (__fastcall *ThisFn)(void *, int);
typedef void (__fastcall *ThisFloatFn)(void *, int, float);
typedef void (__fastcall *ThisIntFn)(void *, int, int);
typedef void (__fastcall *ThisBoolFn)(void *, int, bool);
typedef void (__fastcall *AddFlareFn)(RLensFlareManager *, int, const Coord4 *, uint32_t);
typedef void (__fastcall *AddDecalFn)(RDecalManager *, int, const Coord4 *, const Coord4 *, const Coord4 *, int, float,
                                      float);
typedef void (*StaticFn)();
typedef void (*TexelsFn)(bool, bool);
typedef void (*FeatureInitFn)(int, int, int, int);

#define Orig_ReflSphereMapCoords ((SphereCoordsFn)0x00098980)
#define Orig_ReflHemisphereCoords ((HemisphereCoordsFn)0x00098a00)
#define Orig_ReflMapVertex ((MapVertexFn)0x00098ab0)
#define Orig_CylinderSection ((CylinderFn)0x0009b470)
#define Orig_LightingProps ((LightingPropsFn)0x00098380)
#define Orig_GetReflectionData2 ((Data2Fn)0x00098410)
#define Orig_GetReflectionMapWarpageData ((GetterFn)0x00098210)
#define Orig_GetReflectionCarPos ((GetterFn)0x00098470)
#define Orig_TextureWeaponEnvMap ((GetterFn)0x00098480)
#define Orig_TextureSpecular ((GetterFn)0x00098490)
#define Orig_Texture ((GetterFn)0x000984a0)
#define Orig_GetReflectionMatrix ((GetterFn)0x000984b0)
#define Orig_EnableReflectionMapWarpage ((ReflBoolFn)0x00098220)
#define Orig_SetReflectiveSpecularStrength ((SpecularFn)0x00098360)
#define Orig_PrivateSubmitSceneObj ((SubmitFn)0x00098520)
#define Orig_DeregisterSceneObj ((ReflObjectFn)0x000986a0)
#define Orig_ResetSceneObjDistances ((ThisFn)0x000988a0)
#define Orig_SetReflectivity ((ReflObjectFn)0x00099610)
#define Orig_SetAreaBrightness ((ThisFloatFn)0x0009a4a0)
#define Orig_EnableMotionBlur ((ThisFloatFn)0x0009a4d0)
#define Orig_DisableMotionBlur ((ThisFn)0x0009a4f0)
#define Orig_SetEnabled ((ThisIntFn)0x0009a500)
#define Orig_ColorizeReset ((ThisFn)0x0009ac00)
#define Orig_FlareReset ((ThisFn)0x0009e150)
#define Orig_FlareEnable ((ThisBoolFn)0x0009e1f0)
#define Orig_FlareEndFrame ((ThisFn)0x0009e200)
#define Orig_AddFlare ((AddFlareFn)0x0009e250)
#define Orig_AddDecal ((AddDecalFn)0x0009b070)
#define Orig_DecalReset ((ThisFn)0x0009af70)
#define Orig_GainReset ((ThisFn)0x0009dea0)
#define Orig_GainDraw ((ThisFn)0x0009dec0)
#define Orig_SetUserSpecifiedRenderFeatures ((StaticFn)0x000980f0)
#define Orig_SetTexelsAreOffset ((TexelsFn)0x000980e0)
#define Orig_FeatureInit ((FeatureInitFn)0x000981c0)

#define PVehicle_GetNameCount ((uint32_t (*)())0x00071880)
#define PVehicle_GetCarNames ((const char *const *(*)())0x000718e0)

// ---- the game's state

#define ShadowReflection (*(RReflection **)0x001f2dfc)
#define ShadowColorize (*(RColorize **)0x001f6898)
#define ShadowDecals (*(RDecalManager **)0x001fe820)
#define ShadowGain (*(RGain **)0x00200f20)
#define ShadowLensFlares (*(RLensFlareManager **)0x00200f44)
#define ShadowMissileCam (*(uint8_t **)0x00200f54)
#define ShadowLightManager (*(uint8_t **)0x001ec260)
#define ShadowRendererEye ((const float *)((*(uint8_t **)0x001ebff4) + 0x30))
#define ShadowNormalLight (*(LightBlock **)0x001dd9d4)
#define ShadowIRLight (*(LightBlock **)0x001dd9d8)
#define ShadowIRState ((uint8_t *)0x001dd9dc)          // IRLightReady, IRModeOn
#define ShadowWarpage ((uint8_t *)0x001c482c)
#define ShadowWarpageX FLOAT_AT(0x001c482c)
#define ShadowWarpageY FLOAT_AT(0x001c4830)
#define ShadowFeatures ((uint8_t *)0x001f2d78)
#define ShadowFeaturesLocked BOOL8_AT(0x001f2df8)
#define ShadowCpuType ((uint8_t *)0x001c4810)
#define ShadowMouseBounds ((uint8_t *)0x002420bc)
#define ShadowDecalGeoPrims ((TexturedGeoPrim **)0x001f68b0)
#define ShadowDecalGlobals ((uint8_t *)0x001f68b0)
const size_t kDecalGlobalsBytes = 0x001ff7f0 + 0x4c - 0x001f68b0;

// ---- the regions a case may write, snapshotted as one block

struct Region {
    void *at;
    size_t bytes;
};

struct Regions {
    Region list[24];
    int count;
    size_t total;
};

Regions g_regions;
uint8_t *g_pre, *g_afterOriginal, *g_afterPort;
size_t g_capacity = 0;

void ClearRegions() {
    g_regions.count = 0;
    g_regions.total = 0;
}

void AddRegion(void *at, size_t bytes) {
    if (at == NULL || bytes == 0)
        return;
    g_regions.list[g_regions.count++] = { at, bytes };
    g_regions.total += bytes;
    if (g_regions.total > g_capacity) {
        g_capacity = g_regions.total + 0x1000;
        g_pre = static_cast<uint8_t *>(realloc(g_pre, g_capacity));
        g_afterOriginal = static_cast<uint8_t *>(realloc(g_afterOriginal, g_capacity));
        g_afterPort = static_cast<uint8_t *>(realloc(g_afterPort, g_capacity));
    }
}

void Take(uint8_t *out) {
    for (int i = 0; i < g_regions.count; i++) {
        memcpy(out, g_regions.list[i].at, g_regions.list[i].bytes);
        out += g_regions.list[i].bytes;
    }
}

void Put(const uint8_t *in) {
    for (int i = 0; i < g_regions.count; i++) {
        memcpy(g_regions.list[i].at, in, g_regions.list[i].bytes);
        in += g_regions.list[i].bytes;
    }
}

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[reflection]   %s #%d: %s\n", what, index, detail);
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

// The written state after both sides, region by region
void CheckRegions(const char *what, int index) {
    const uint8_t *a = g_afterOriginal, *b = g_afterPort;
    for (int i = 0; i < g_regions.count; i++) {
        size_t bytes = g_regions.list[i].bytes;
        if (memcmp(a, b, bytes) != 0) {
            char name[64];
            snprintf(name, sizeof(name), "%s (region %d)", what, i);
            CheckBytes(name, index, a, b, bytes);
        } else {
            g_checks++;
        }
        a += bytes;
        b += bytes;
    }
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

// Runs one side; a fault is counted, not fatal. The original runs inside the window.
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

// Both sides from the state as it is now; the regions compared, the port's state kept
bool SideBySide(const char *what, int index, CaseFn run, void *original, void *port) {
    g_cases++;
    Take(g_pre);
    bool ok = Guarded(run, original, true);
    Take(g_afterOriginal);
    Put(g_pre);
    ok = Guarded(run, port, false) && ok;
    Take(g_afterPort);
    if (ok)
        CheckRegions(what, index);
    return ok;
}

// ---- random inputs (a generator of our own: the game's is not touched)

uint32_t g_seed = 0x5eed980e;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

// Mostly ordinary values; now and then zero, a NaN or a signalling NaN
float Awkward(float lo, float hi) {
    switch (Next() % 40) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return std::bit_cast<float>(0x7fc00000u);
    case 3:
        return std::bit_cast<float>(0x7f800001u);
    default:
        return Uniform(lo, hi);
    }
}

Coord4 RandomVector(float range) {
    Coord4 v = { Uniform(-range, range), Uniform(-range, range), Uniform(-range, range), Uniform(-1.0f, 1.0f) };
    return v;
}

// ---- the sphere map maths

struct MathsCase {
    int op;
    Coord4 in;
    float a, b, c, d;
    bool flag;
    Coord4 out[2];
    bool front;
    Coord3 positions[34];
    float uvs[34][2];
};

MathsCase g_maths[2];

void RunMaths(void *context, bool original) {
    MathsCase *c = static_cast<MathsCase *>(context);
    switch (c->op) {
    case 0:
        if (original)
            Orig_ReflSphereMapCoords(&c->in, &c->out[0]);
        else
            ReflSphereMapCoords(&c->in, &c->out[0]);
        break;
    case 1:
        if (original)
            Orig_ReflHemisphereCoords(&c->in, &c->out[0], &c->front);
        else
            ReflHemisphereCoords(&c->in, &c->out[0], &c->front);
        break;
    case 2:
        if (original)
            Orig_ReflMapVertex(c->a, c->b, c->flag, &c->out[0], &c->out[1]);
        else
            ReflMapVertex(c->a, c->b, c->flag, &c->out[0], &c->out[1]);
        break;
    case 3:
        if (original)
            Orig_CylinderSection(c->positions, c->uvs, c->a, c->b, c->c, c->d);
        else
            CylinderSection(c->positions, c->uvs, c->a, c->b, c->c, c->d);
        break;
    }
}

void TestMaths() {
    ClearRegions();
    for (int i = 0; i < 4000; i++) {
        MathsCase &c = g_maths[0];
        memset(&c, 0, sizeof(c));
        c.op = i % 4;
        c.in = RandomVector(2.0f);
        if (Next() % 10 == 0)
            c.in.z = 0.0f;
        if (Next() % 50 == 0)
            c.in.x = std::bit_cast<float>(0x7fc00000u);
        c.a = Awkward(-1.5f, 1.5f);
        c.b = Awkward(-2.0f, 2.0f);
        c.c = Awkward(-3.0f, 3.0f);
        c.d = Awkward(-3.0f, 3.0f);
        c.flag = Next() % 2 == 0;
        g_maths[1] = c;
        g_cases++;
        if (!Guarded(RunMaths, &g_maths[0], true) || !Guarded(RunMaths, &g_maths[1], false))
            continue;
        if (c.op == 3) {
            CheckBytes("CylinderSection positions", i, g_maths[0].positions, g_maths[1].positions,
                       sizeof(c.positions));
            CheckBytes("CylinderSection uvs", i, g_maths[0].uvs, g_maths[1].uvs, sizeof(c.uvs));
        } else {
            static const char *const kNames[] = { "ReflSphereMapCoords", "ReflHemisphereCoords", "ReflMapVertex" };
            CheckBytes(kNames[c.op], i, g_maths[0].out, g_maths[1].out, sizeof(c.out));
            if (c.op == 1)
                CheckBytes("ReflHemisphereCoords front", i, &g_maths[0].front, &g_maths[1].front, 1);
        }
    }
}

// ---- RReflection's queries on the live object

struct QueryCase {
    int op;
    int kind;
    const char *name;
    void *result;
};

QueryCase g_query[2];

void RunQuery(void *context, bool original) {
    QueryCase *c = static_cast<QueryCase *>(context);
    RReflection *r = ShadowReflection;
    switch (c->op) {
    case 0:
        c->result = original ? Orig_LightingProps(r, 0, c->kind, c->name) : r->LightingProps(c->kind, c->name);
        break;
    case 1:
        c->result = original ? Orig_GetReflectionData2(r, 0, c->name) : r->GetReflectionData2(c->name);
        break;
    case 2:
        c->result = original ? Orig_GetReflectionMapWarpageData(r, 0) : r->GetReflectionMapWarpageData();
        break;
    case 3:
        c->result = original ? Orig_GetReflectionCarPos(r, 0) : r->GetReflectionCarPos();
        break;
    case 4:
        c->result = original ? Orig_TextureWeaponEnvMap(r, 0) : r->TextureWeaponEnvMap();
        break;
    case 5:
        c->result = original ? Orig_TextureSpecular(r, 0) : r->TextureSpecular();
        break;
    case 6:
        c->result = original ? Orig_Texture(r, 0) : r->Texture();
        break;
    case 7:
        c->result = original ? Orig_GetReflectionMatrix(r, 0) : r->GetReflectionMatrix();
        break;
    }
}

void Query(int op, int kind, const char *name, int index) {
    g_query[0].op = g_query[1].op = op;
    g_query[0].kind = g_query[1].kind = kind;
    g_query[0].name = g_query[1].name = name;
    g_query[0].result = g_query[1].result = NULL;
    g_cases++;
    if (!Guarded(RunQuery, &g_query[0], true) || !Guarded(RunQuery, &g_query[1], false))
        return;
    g_checks++;
    if (g_query[0].result != g_query[1].result) {
        char detail[96];
        snprintf(detail, sizeof(detail), "op %d kind %d \"%s\": original %p, port %p", op, kind, name ? name : "",
                 g_query[0].result, g_query[1].result);
        Differ("query", index, detail);
    }
}

void TestQueries() {
    static const char *const kOthers[] = { "Character", "character", "Dynamic Objects", "DYNAMIC OBJECTS",
                                           "Characters", "no such car" };
    int index = 0;
    uint32_t cars = PVehicle_GetNameCount();
    const char *const *names = PVehicle_GetCarNames();
    for (int kind = 0; kind < 6; kind++) {
        for (uint32_t car = 0; car < cars; car++)
            Query(0, kind, names[car], index++);
        for (const char *name : kOthers)
            Query(0, kind, name, index++);
    }
    for (uint32_t car = 0; car < cars; car++)
        Query(1, 0, names[car], index++);
    for (const char *name : kOthers)
        Query(1, 0, name, index++);
    for (int op = 2; op <= 7; op++)
        Query(op, 0, NULL, index++);
}

// ---- RReflection's state on a copy

RReflection *g_reflection;              // the copy
RReflection::ReflPrivateData *g_private;

RSceneObj *StandIn(int k) {
    return reinterpret_cast<RSceneObj *>(uintptr_t(0x00010000 + 0x40 * k));
}

struct ReflectionCase {
    int op;
    float distance;
    RSceneObj *object;
    bool enable;
    int index;
};

ReflectionCase g_refl;

void RunReflection(void *context, bool original) {
    ReflectionCase *c = static_cast<ReflectionCase *>(context);
    RReflection *r = g_reflection;
    switch (c->op) {
    case 0:
        if (original)
            Orig_PrivateSubmitSceneObj(r, 0, c->distance, c->object);
        else
            r->PrivateSubmitSceneObj(c->distance, c->object);
        break;
    case 1:
        if (original)
            Orig_DeregisterSceneObj(r, 0, c->object);
        else
            r->DeregisterSceneObj(c->object);
        break;
    case 2:
        if (original)
            Orig_EnableReflectionMapWarpage(r, 0, c->enable);
        else
            r->EnableReflectionMapWarpage(c->enable);
        break;
    case 3:
        if (original)
            Orig_SetReflectiveSpecularStrength(r, 0, c->index, c->distance);
        else
            r->SetReflectiveSpecularStrength(c->index, c->distance);
        break;
    case 4:
        if (original)
            Orig_ResetSceneObjDistances(r, 0);
        else
            r->ResetSceneObjDistances();
        break;
    case 5:
        if (original)
            Orig_SetReflectivity(r, 0, c->object);
        else
            r->SetReflectivity(c->object);
        break;
    }
}

void ReflectionStep(int index) {
    static const char *const kNames[] = { "PrivateSubmitSceneObj", "DeregisterSceneObj", "EnableReflectionMapWarpage",
                                          "SetReflectiveSpecularStrength", "ResetSceneObjDistances",
                                          "SetReflectivity" };
    SideBySide(kNames[g_refl.op], index, RunReflection, &g_refl, &g_refl);
}

void TestReflectionState() {
    RReflection *live = ShadowReflection;
    static uint8_t copy[sizeof(RReflection)];
    static uint8_t privateCopy[sizeof(RReflection::ReflPrivateData)];
    memcpy(copy, live, sizeof(copy));
    memcpy(privateCopy, live->privateData, sizeof(privateCopy));
    g_reflection = reinterpret_cast<RReflection *>(copy);
    g_private = reinterpret_cast<RReflection::ReflPrivateData *>(privateCopy);
    g_reflection->privateData = g_private;
    int records = g_private->lightingCount;

    ClearRegions();
    AddRegion(copy, sizeof(copy));
    AddRegion(privateCopy, sizeof(privateCopy));
    AddRegion(g_private->lighting, records * sizeof(ReflLighting));
    AddRegion(ShadowWarpage, 8);
    uint8_t *base = static_cast<uint8_t *>(malloc(g_regions.total));
    Take(base);

    // The live slots first, while they hold the live objects
    int index = 0;
    for (int i = 0; i < 4; i++) {
        if (g_private->objects[i] == NULL)
            continue;
        g_refl.op = 5;
        g_refl.object = g_private->objects[i];
        ReflectionStep(index++);
    }
    g_refl.op = 4;
    ReflectionStep(index++);

    // Then stand-ins, from empty slots
    for (int i = 0; i < 4; i++) {
        g_private->ranks[i] = int8_t(i);
        g_private->distances[i] = 100000000.0f;
        g_private->objects[i] = NULL;
    }
    float held[8] = {};
    for (int step = 0; step < 1500; step++) {
        uint32_t roll = Next() % 100;
        g_refl.object = StandIn(int(Next() % 8));
        if (roll < 45) {
            g_refl.op = 0;
            g_refl.distance = Next() % 8 == 0 ? held[Next() % 8] : Awkward(0.0f, 500.0f);
            held[step % 8] = g_refl.distance;
        } else if (roll < 75) {
            g_refl.op = 1;
            if (Next() % 6 == 0)
                g_refl.object = StandIn(100);
        } else if (roll < 85) {
            g_refl.op = 2;
            g_refl.enable = Next() % 2 == 0;
            ShadowWarpageX = Awkward(-2.0f, 2.0f);
            ShadowWarpageY = Awkward(-2.0f, 2.0f);
        } else if (roll < 95) {
            g_refl.op = 3;
            g_refl.index = int(Next() % uint32_t(records));
            g_refl.distance = Awkward(-4.0f, 4.0f);
        } else {
            // ResetSceneObjDistances would ask the stand-ins their distance: empty the slots first
            g_refl.op = 4;
            for (int i = 0; i < 4; i++)
                g_private->objects[i] = NULL;
        }
        ReflectionStep(index++);
    }

    Put(base);
    free(base);
}

// ---- RColorize on a copy

RColorize *g_colorize;

struct ColorizeCase {
    int op;
    float value;
    int mode;
};

ColorizeCase g_colour;

void RunColorize(void *context, bool original) {
    ColorizeCase *c = static_cast<ColorizeCase *>(context);
    RColorize *z = g_colorize;
    switch (c->op) {
    case 0:
        if (original)
            Orig_SetAreaBrightness(z, 0, c->value);
        else
            z->SetAreaBrightness(c->value);
        break;
    case 1:
        if (original)
            Orig_EnableMotionBlur(z, 0, c->value);
        else
            z->EnableMotionBlur(c->value);
        break;
    case 2:
        if (original)
            Orig_DisableMotionBlur(z, 0);
        else
            z->DisableMotionBlur();
        break;
    case 3:
        if (original)
            Orig_SetEnabled(z, 0, c->mode);
        else
            z->SetEnabled(c->mode);
        break;
    case 4:
        if (original)
            Orig_ColorizeReset(z, 0);
        else
            z->Reset();
        break;
    }
}

void TestColorize() {
    static uint8_t copy[sizeof(RColorize)];
    memcpy(copy, ShadowColorize, sizeof(copy));
    g_colorize = reinterpret_cast<RColorize *>(copy);

    ClearRegions();
    AddRegion(copy, sizeof(copy));
    AddRegion(ShadowMissileCam, 0x18);
    AddRegion(ShadowLightManager, 0x170);
    AddRegion(ShadowIRState, 2);
    AddRegion(ShadowNormalLight, sizeof(LightBlock));
    AddRegion(ShadowIRLight, sizeof(LightBlock));
    uint8_t *base = static_cast<uint8_t *>(malloc(g_regions.total));
    Take(base);

    static const char *const kNames[] = { "SetAreaBrightness", "EnableMotionBlur", "DisableMotionBlur", "SetEnabled",
                                          "RColorize::Reset" };
    for (int step = 0; step < 600; step++) {
        g_colour.op = int(Next() % 5);
        g_colour.value = Awkward(-3.0f, 3.0f);
        g_colour.mode = int(Next() % 7) - 1;
        if (Next() % 8 == 0)
            g_colorize->brightnessChangeTime = 0;
        SideBySide(kNames[g_colour.op], step, RunColorize, &g_colour, &g_colour);
    }

    Put(base);
    free(base);
}

// ---- RLensFlareManager on a copy

RLensFlareManager g_flares;

struct FlareCase {
    int op;
    Coord4 position;
    uint32_t value;
    bool enable;
};

FlareCase g_flare;

void RunFlares(void *context, bool original) {
    FlareCase *c = static_cast<FlareCase *>(context);
    RLensFlareManager *f = &g_flares;
    switch (c->op) {
    case 0:
        if (original)
            Orig_FlareReset(f, 0);
        else
            f->Reset();
        break;
    case 1:
        if (original)
            Orig_FlareEnable(f, 0, c->enable);
        else
            f->Enable(c->enable);
        break;
    case 2:
        if (original)
            Orig_FlareEndFrame(f, 0);
        else
            f->EndFrame();
        break;
    default:
        if (original)
            Orig_AddFlare(f, 0, &c->position, c->value);
        else
            f->AddFlare(&c->position, c->value);
        break;
    }
}

void TestFlares() {
    static LensFlareData copy;
    memcpy(&copy, ShadowLensFlares->data, sizeof(copy));
    g_flares.vtable = ShadowLensFlares->vtable;
    g_flares.data = &copy;
    copy.enabled = 1;

    ClearRegions();
    AddRegion(&copy, sizeof(copy));

    static const char *const kNames[] = { "RLensFlareManager::Reset", "Enable", "EndFrame", "AddFlare" };
    const float *eye = ShadowRendererEye;
    for (int step = 0; step < 1500; step++) {
        uint32_t roll = Next() % 100;
        g_flare.op = roll < 3 ? 0 : roll < 8 ? 1 : roll < 18 ? 2 : 3;
        g_flare.enable = Next() % 4 != 0;
        if (Next() % 50 == 0)
            copy.sun = copy.sun ? 0 : 1;
        float range = Next() % 2 == 0 ? 50.0f : 2000.0f;
        g_flare.position.x = eye[0] + Uniform(-range, range);
        g_flare.position.y = eye[1] + Uniform(-range, range);
        g_flare.position.z = eye[2] + Uniform(-range, range);
        g_flare.position.w = 1.0f;
        g_flare.value = Next();
        SideBySide(kNames[g_flare.op], step, RunFlares, &g_flare, &g_flare);
    }
}

// ---- RDecalManager on a copy

RDecalManager *g_decals;

struct DecalCase {
    int op;
    Coord4 centre, axis, normal;
    int type;
    float width, height;
};

DecalCase g_decal;

void RunDecals(void *context, bool original) {
    DecalCase *c = static_cast<DecalCase *>(context);
    if (c->op == 0) {
        if (original)
            Orig_AddDecal(g_decals, 0, &c->centre, &c->axis, &c->normal, c->type, c->width, c->height);
        else
            g_decals->AddDecal(&c->centre, &c->axis, &c->normal, c->type, c->width, c->height);
    } else {
        if (original)
            Orig_DecalReset(g_decals, 0);
        else
            g_decals->Reset();
    }
}

void TestDecals() {
    static uint8_t copy[sizeof(RDecalManager)];
    memcpy(copy, ShadowDecals, sizeof(copy));
    g_decals = reinterpret_cast<RDecalManager *>(copy);

    ClearRegions();
    AddRegion(copy, sizeof(copy));
    AddRegion(ShadowDecalGlobals, kDecalGlobalsBytes);
    for (int i = 0; i < 8; i++)
        AddRegion(ShadowDecalGeoPrims[i], sizeof(TexturedGeoPrim));
    uint8_t *base = static_cast<uint8_t *>(malloc(g_regions.total));
    Take(base);

    for (int i = 0; i < 8; i++) {
        g_decals->types[i].delay = Next() % 4 == 0 ? 0 : Next() % 300;
        g_decals->types[i].alpha = uint8_t(Next());
    }
    for (int step = 0; step < 500; step++) {
        g_decal.op = Next() % 40 == 0 ? 1 : 0;
        g_decal.centre = RandomVector(1000.0f);
        g_decal.axis = RandomVector(1.0f);
        g_decal.normal = RandomVector(1.0f);
        g_decal.type = int(Next() % 8);
        g_decal.width = Awkward(0.0f, 10.0f);
        g_decal.height = Awkward(0.0f, 10.0f);
        SideBySide(g_decal.op == 0 ? "AddDecal" : "RDecalManager::Reset", step, RunDecals, &g_decal, &g_decal);
    }

    Put(base);
    free(base);
}

// ---- RGain on a copy, and the feature switches

RGain *g_gain;
int g_gainOp;

void RunGain(void *, bool original) {
    if (g_gainOp == 0) {
        if (original)
            Orig_GainReset(g_gain, 0);
        else
            g_gain->Reset();
    } else {
        if (original)
            Orig_GainDraw(g_gain, 0);
        else
            g_gain->Draw();
    }
}

void TestGain() {
    static uint8_t copy[sizeof(RGain)];
    memcpy(copy, ShadowGain, sizeof(copy));
    g_gain = reinterpret_cast<RGain *>(copy);
    ClearRegions();
    AddRegion(copy, sizeof(copy));
    for (int step = 0; step < 20; step++) {
        for (int i = 0; i < 4; i++) {
            g_gain->gain[i] = Awkward(0.0f, 2.0f);
            g_gain->offset[i] = Awkward(-1.0f, 1.0f);
        }
        g_gainOp = 0;
        SideBySide("RGain::Reset", step, RunGain, NULL, NULL);
        if (step % 2 == 1)
            g_gain->offset[step % 4] = -0.0f;   // still the identity
        g_gainOp = 1;
        SideBySide("RGain::Draw at the identity", step, RunGain, NULL, NULL);
    }
}

struct FeatureCase {
    int op;
    int width, height, depth, stamp;
};

FeatureCase g_feature;

void RunFeatures(void *context, bool original) {
    FeatureCase *c = static_cast<FeatureCase *>(context);
    switch (c->op) {
    case 0:
        if (original)
            Orig_SetUserSpecifiedRenderFeatures();
        else
            FeatureManager::SetUserSpecifiedRenderFeatures();
        break;
    case 1:
        if (original)
            Orig_SetTexelsAreOffset(true, true);
        else
            FeatureManager::SetTexelsAreOffset(true, true);
        break;
    case 2:
        if (original)
            Orig_FeatureInit(c->width, c->height, c->depth, c->stamp);
        else
            FeatureManager::Init(c->width, c->height, c->depth, c->stamp);
        break;
    }
}

void TestFeatures() {
    ClearRegions();
    AddRegion(ShadowFeatures, sizeof(FeatureManagerData));
    AddRegion(ShadowCpuType, 4);
    AddRegion(ShadowMouseBounds, 0x24);
    uint8_t *base = static_cast<uint8_t *>(malloc(g_regions.total));
    Take(base);
    for (int step = 0; step < 30; step++) {
        // Scrambled first, so every write shows
        for (size_t i = 0; i < sizeof(FeatureManagerData); i++)
            ShadowFeatures[i] = uint8_t(Next());
        ShadowFeaturesLocked = step % 3 == 0 ? 1 : 0;
        g_feature.op = step % 3;
        g_feature.width = int(Next() % 1280);
        g_feature.height = int(Next() % 960);
        g_feature.depth = Next() % 2 == 0 ? 16 : 32;
        g_feature.stamp = int(Next());
        SideBySide("FeatureManager", step, RunFeatures, &g_feature, &g_feature);
    }
    Put(base);
    free(base);
}

}  // namespace

void ReflectionShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_REFLECTIONSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    TestMaths();
    TestFeatures();
    if (ShadowReflection != NULL && ShadowReflection->privateData != NULL) {
        TestQueries();
        TestReflectionState();
    } else {
        printf("[reflection] no RReflection - its cases skipped\n");
    }
    if (ShadowColorize != NULL && ShadowMissileCam != NULL && ShadowLightManager != NULL &&
        ShadowNormalLight != NULL && ShadowIRLight != NULL)
        TestColorize();
    else
        printf("[reflection] no RColorize, missile camera, light manager or IR light blocks - RColorize skipped\n");
    if (ShadowLensFlares != NULL && ShadowLensFlares->data != NULL)
        TestFlares();
    else
        printf("[reflection] no RLensFlareManager - its cases skipped\n");
    if (ShadowDecals != NULL && ShadowDecalGeoPrims[0] != NULL)
        TestDecals();
    else
        printf("[reflection] no RDecalManager - its cases skipped\n");
    if (ShadowGain != NULL)
        TestGain();

    ResetFpu();
    printf("[reflection] sphere map maths, reflection, colourise, decals, gain, lens flares, features vs originals: "
           "%d cases, %d checks, %d differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[reflection]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
