#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameEffectsShadowC.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <initializer_list>
#include <vector>

#include "../anim/AnimEngine.h"         // Handle
#include "../data/Tree.h"
#include "../eagl/EaglGlobals.h"
#include "../eagl/GeoPrimState.h"
#include "../eagl/Model.h"
#include "../eagl/RenderMethod.h"
#include "../engine/UMemory.hpp"
#include "../game/EmpEffect.h"
#include "../render/Decals.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"
#include "../world/World.h"             // ArticleEffect
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.effects' package C, run once from the first simulation tick when
// NIGHTFIRE_GAMEEFFECTSSHADOWC is set. Each case runs the ORIGINAL (every entry of the package swapped back
// together) and then the PORT on the same inputs, and compares what they leave and a log of every call they make
// out of the package:
//
//   - REmp::Init: the ring's and the sphere's arrays, the light record, the colours and states, and the two
//     GeoPrims' parameters (the original's addresses of the records the port owns translated); then Kill, its
//     EaglFree calls logged through the allocator's function pointer;
//   - REmp::DrawShootingEffect on private copies of the ring's GeoPrim, random transforms, progress 0, 1, inside,
//     outside and NaN; the DynamicModel calls and the depth writes faked;
//   - REmpBolts::LoadAttributes, the dbattrib calls faked (our variables' addresses translated to the original's);
//   - the anchor map (EmpAnchorMap) and the count map (EmpGroupCountMap): random inserts, lookups, erases at a node
//     and over ranges, minimum and maximum, the trees compared node by node after each step;
//   - REmpBolts on a synthetic car (locators named "emp..." in several groups, short and long names, other types
//     and names): construction (FindAnchors), then Draw, Update, TrySpawnBolt, GetRandomAnchor, CreateVehicleBolt
//     and CreateWorldBolt with the debug variables, the generator's seed, the steps since the last update and the
//     bolts perturbed; destruction.
//
// Fakes: RLightning's IsBoltAlive, AddBolt and AddRoundedBolt, RSceneObj's GetPosition and LocateFX, the collision
// manager's CheckHitWorld and CheckHitWindow, Shell::ApplyWorldEffects, RLightManager::AddPositionalLight,
// Util_GaussRandom and Util_PerturbVector, and for the ring the DynamicModel and SetZWritesEnable. The debug
// variables the port owns are copied to the original's addresses before each original run.
//
// Known difference, masked: CreateVehicleBolt's spark direction (the original subtracts a vector it never sets;
// the port zero) - ApplyWorldEffects' normal is not compared after a LocateFX in world space. The map's allocator
// byte (the original's copy of an unset byte) is not compared.
//
// A mutation the test sees: CreateWorldBolt taking the sign from RandomShort()'s bit 0 instead of the first
// product's bit 8 (AddBolt's bulge and the seed differ), or REmp::Init clearing only the band's own two last
// texture coordinates (the sphere's first position differs).
//
// One summary line: [gameeffectsshadowc] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

#define Orig_EmpInit ((void (*)(void))0x0009b680)
#define Orig_EmpDraw ((void (*)(const MATRIX4 *, float))0x0009bb30)
#define Orig_EmpKill ((void (*)(void))0x0009bce0)
#define Orig_LoadAttributes ((void (*)(void))0x0009bd50)
#define Orig_BoltsConstruct ((REmpBolts *(__fastcall *)(REmpBolts *, int, RSceneObj *))0x0009dc90)
#define Orig_BoltsDestruct ((void (__fastcall *)(REmpBolts *, int))0x0009d630)
#define Orig_BoltsDraw ((void (__fastcall *)(REmpBolts *, int))0x0009d7c0)
#define Orig_BoltsUpdate ((void (__fastcall *)(REmpBolts *, int))0x0009d6b0)
#define Orig_TrySpawnBolt ((void (__fastcall *)(REmpBolts *, int))0x0009d320)
#define Orig_GetRandomAnchor ((EmpAnchor *(__fastcall *)(REmpBolts *, int, uint32_t))0x0009d2d0)
#define Orig_CreateVehicleBolt ((void (__fastcall *)(REmpBolts *, int, EmpAnchor *, EmpAnchor *))0x0009c0e0)
#define Orig_CreateWorldBolt ((void (__fastcall *)(REmpBolts *, int, EmpAnchor *, const Coord3 *))0x0009c260)
#define Orig_ListClear ((void (__fastcall *)(EmpAnchorList *, int))0x0009c370)
#define Orig_GroupDestruct ((void (__fastcall *)(EmpAnchorGroup *, int))0x0009c400)
#define Orig_AnchorMax ((EmpAnchorNode *(*)(EmpAnchorNode *))0x0009c330)
#define Orig_AnchorMin ((EmpAnchorNode *(*)(EmpAnchorNode *))0x0009c350)
#define Orig_AnchorBuyHead ((EmpAnchorNode *(*)(void))0x0009c420)
#define Orig_AnchorInsertUnique ((EmpAnchorMap::InsertResult *(__fastcall *)(EmpAnchorMap *, int, EmpAnchorMap::InsertResult *, const EmpAnchorGroup *))0x0009cf40)
#define Orig_AnchorEraseAt ((EmpAnchorNode **(__fastcall *)(EmpAnchorMap *, int, EmpAnchorNode **, EmpAnchorNode *))0x0009c980)
#define Orig_AnchorEraseRange ((EmpAnchorNode **(__fastcall *)(EmpAnchorMap *, int, EmpAnchorNode **, EmpAnchorNode *, EmpAnchorNode *))0x0009d0c0)
#define Orig_AnchorLookup ((EmpAnchorList *(__fastcall *)(EmpAnchorMap *, int, const uint32_t *))0x0009d240)
#define Orig_AnchorDestruct ((void (__fastcall *)(EmpAnchorMap *, int))0x0009d5f0)
#define Orig_TreeMin ((TreeNode *(*)(TreeNode *))0x0009c310)
#define Orig_CountInsertUnique ((TreeInsertResult *(__fastcall *)(EmpGroupCountMap *, int, TreeInsertResult *, const TreePair *))0x0009d000)
#define Orig_CountEraseAt ((TreeNode **(__fastcall *)(EmpGroupCountMap *, int, TreeNode **, TreeNode *))0x0009cc70)
#define Orig_CountEraseRange ((TreeNode **(__fastcall *)(EmpGroupCountMap *, int, TreeNode **, TreeNode *, TreeNode *))0x0009d180)
#define Orig_CountDestroy ((void (__fastcall *)(EmpGroupCountMap *, int))0x0009d960)

const unsigned kEntries[] = {
    0x0009b680, 0x0009bb30, 0x0009bce0, 0x0009bd50, 0x0009c0e0, 0x0009c260, 0x0009c310, 0x0009c330, 0x0009c350,
    0x0009c370, 0x0009c3a0, 0x0009c400, 0x0009c420, 0x0009c460, 0x0009c4a0, 0x0009c4c0, 0x0009c570, 0x0009c5c0,
    0x0009c7a0, 0x0009c980, 0x0009cc70, 0x0009cf40, 0x0009d000, 0x0009d0c0, 0x0009d180, 0x0009d240, 0x0009d2d0,
    0x0009d320, 0x0009d5f0, 0x0009d630, 0x0009d6b0, 0x0009d7c0, 0x0009d960, 0x0009d9a0, 0x0009dc90,
};

// The original's copies of the globals the port owns, and the ones both share
#define OrigRingGeoPrim (*(EmpGeoPrim **)0x001ff840)
#define OrigSphereGeoPrim (*(EmpGeoPrim **)0x001ffb98)
constexpr uint32_t kOrigRingUVs = 0x001ff850;
constexpr uint32_t kOrigRegionLo = 0x001ff840;       // REmp's statics through the debug variables in .bss
constexpr uint32_t kOrigRegionHi = 0x00200f18;
#define OrigRingColours ((uint32_t *)0x00200d80)
#define ShadowSeed U32_AT(0x001c45c4)

// The debug variables: ours and the original's address
struct Tunable {
    void *ours;
    uint32_t original;
};
const Tunable kTunables[] = {
    { &kSnapToBoundingBox, 0x00200f00 }, { &kAllowVehicleBolts, 0x001c49a8 }, { &kAllowWorldBolts, 0x001c49ac },
    { &kTargetBoltsAlive, 0x001c49b0 }, { &kScaleLifetime, 0x001c49b4 }, { &kHitWorldPct, 0x001c49b8 },
    { &kHitWorldRange, 0x001c49bc }, { &kWorldPerturb, 0x001c49c0 }, { &kWorldFlashSize, 0x001c49c4 },
    { &kWorldArcFactor, 0x001c49c8 }, { &kVehicleArcFactor, 0x001c49cc }, { &kVehicleDriftMin, 0x001c49d0 },
    { &kVehicleDriftMax, 0x001c49d4 }, { &kVehicleDriftVariance, 0x001c49d8 }, { &kVehiclePerturb, 0x001c49dc },
    { &kVehicleSparkRate, 0x001c49e0 }, { &kVehicleJoltForce, 0x00200f04 }, { &kVehicleJoltTorque, 0x00200f08 },
    { &kVehicleSegments, 0x001c49e4 }, { &kVehicleLevels, 0x001c49e8 }, { &kCenterPullPct, 0x00200f0c },
    { &kGlowEnable, 0x001c49ec }, { &kGlowStrength, 0x001c49f0 }, { &kGlowAmplitude, 0x001c49f4 },
    { &kGlowFrequency, 0x001c49f8 }, { &kGlowNoiseStrength, 0x001c49fc }, { &kGlowNoiseRange, 0x001c4a00 },
    { &kGlowScatter, 0x001c4a04 }, { &kGlowColour, 0x001c4a08 },
};
constexpr int kTunableCount = int(sizeof(kTunables) / sizeof(kTunables[0]));

// The port's records the original has at its own addresses (for its GeoPrims' parameters and dbattrib's pointers)
struct Translation {
    const void *ours;
    uint32_t original;
    uint32_t size;
};
const Translation kTranslations[] = {
    { EmpRingUVs, 0x001ff850, 0x150 }, { EmpRingPositions, 0x001ff9a0, 0x1f8 }, { &EmpSphere, 0x001ffba0, 0x10e0 },
    { EmpLights, 0x00200e80, 0x80 }, { EmpParam10Vectors, 0x001c48c0, 0x40 }, { EmpRingVertexCount, 0x001c4910, 0x10 },
    { EmpSphereVertexCount, 0x001c4920, 0x10 }, { EmpIndices, 0x001c4930, 0x60 }, { &EmpConstants, 0x001c4990, 0x10 },
    { &GlowColourFlag, 0x00200f14, 1 }, { EmpSphereColours, 0x00200c90, 0x98 }, { &EmpSphereState, 0x00200d30, 0x4c },
    { EmpRingColours, 0x00200d80, 0xa8 }, { &EmpRingState, 0x00200e30, 0x4c },
};

uint32_t ToOriginal(const void *p) {
    uint32_t at = uint32_t(uintptr_t(p));
    for (const Translation &t : kTranslations) {
        uint32_t lo = uint32_t(uintptr_t(t.ours));
        if (at >= lo && at < lo + t.size)
            return t.original + (at - lo);
    }
    for (const Tunable &t : kTunables) {
        if (p == t.ours)
            return t.original;
    }
    return at;
}

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[gameeffectsshadowc] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x6d2b79f5;
uint32_t NextRandom() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}
int RandomInt(int lo, int hi) {
    return lo + int((NextRandom() >> 8) % uint32_t(hi - lo + 1));
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(NextRandom() >> 8) * (1.0f / 16777216.0f);
}
bool Chance(int percent) {
    return RandomInt(0, 99) < percent;
}
uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}
uint32_t Hash(const void *data, size_t size) {
    uint32_t h = 2166136261u;
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
uint32_t Word(const void *p) {
    return uint32_t(uintptr_t(p));
}

template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    call();
#endif
    return true;
}

void Originals(bool on) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, on);
}

// Runs the original (the package's entries swapped back) or the port
template <class F>
void Run(bool original, const F &call) {
    g_original = original;
    if (original)
        Originals(true);
    Guarded(call);
    if (original)
        Originals(false);
}

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[64];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
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

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
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

// ---- the call log and the fakes

enum : uint32_t {
    kBoltAlive = 1, kRoundedBolt, kBolt, kGetPosition, kLocateFX, kHitWorld, kHitWindow, kWorldEffects, kLight,
    kGauss, kPerturb, kModelConstruct, kModelMatrix, kModelAdd, kModelDraw, kModelDestruct, kZWrites, kAttrib,
    kEaglFree,
};

std::vector<uint32_t> g_log;
void Log(uint32_t type, std::initializer_list<uint32_t> words) {
    g_log.push_back(type);
    g_log.push_back(uint32_t(words.size()));
    g_log.insert(g_log.end(), words.begin(), words.end());
}
void LogFloats(uint32_t type, const float *f, int n) {
    g_log.push_back(type);
    g_log.push_back(uint32_t(n));
    for (int i = 0; i < n; i++)
        g_log.push_back(Bits(f[i]));
}

// The fakes' answers, set by each case and reset before each run
uint32_t g_alivePattern;
int g_aliveCalls;
uint32_t g_hitPattern;
int g_hitCalls;
int g_boltHandles;
int g_gaussCalls;
bool g_lastLocateWorld;
Coord3 g_position;
const ArticleEffect *g_effectBase;      // the synthetic car's effects, for LocateFX's matrices

void ResetFakes() {
    g_aliveCalls = 0;
    g_hitCalls = 0;
    g_boltHandles = 0;
    g_gaussCalls = 0;
    g_lastLocateWorld = false;
}

bool __fastcall FakeIsBoltAlive(void *, int, int handle) {
    Log(kBoltAlive, { uint32_t(handle) });
    return ((g_alivePattern >> (g_aliveCalls++ & 31)) & 1) != 0;
}
int __fastcall FakeAddRoundedBolt(void *, int, RSceneObj *obj, const ArticleEffect *from, const ArticleEffect *to,
                                  float driftMin, float driftMax, float driftVariance, float spread, bool around,
                                  int points, int levels, float bulge, int life) {
    Log(kRoundedBolt, { Word(obj), Word(from), Word(to), Bits(driftMin), Bits(driftMax), Bits(driftVariance),
                        Bits(spread), uint32_t(around), uint32_t(points), uint32_t(levels), Bits(bulge),
                        uint32_t(life) });
    return 0x100 + g_boltHandles++;
}
int __fastcall FakeAddBolt(void *, int, const Coord3 *from, RSceneObj *fromObj, const ArticleEffect *fromLocator,
                           const Coord3 *to, RSceneObj *toObj, const ArticleEffect *toLocator, float bulge, int life) {
    Log(kBolt, { Hash(from, sizeof(Coord3)), Word(fromObj), Word(fromLocator), Bits(to->x), Bits(to->y), Bits(to->z),
                 Word(toObj), Word(toLocator), Bits(bulge), uint32_t(life) });
    return 0x200 + g_boltHandles++;
}
void __fastcall FakeGetPosition(RSceneObj *self, int, Coord3 *position) {
    Log(kGetPosition, { Word(self) });
    *position = g_position;
}
void __fastcall FakeLocateFX(RSceneObj *self, int, const ArticleEffect *effect, MATRIX4 *transform, bool world) {
    Log(kLocateFX, { Word(self), Word(effect), uint32_t(world) });
    g_lastLocateWorld = world;
    float k = float(effect - g_effectBase) + (world ? 0.5f : 0.0f);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++)
            transform->mtx[r][c] = (r == c ? 1.0f : 0.0f) + 0.01f * float(r * 4 + c) * k;
    }
    transform->mtx[3][0] = 1.5f * k - 2.0f;
    transform->mtx[3][1] = 0.25f * k + 0.75f;
    transform->mtx[3][2] = -0.5f * k + 1.0f;
    transform->mtx[3][3] = 1.0f;
}
int __fastcall FakeCheckHitWorld(void *, int, const Coord4 *segment, WorldCollisionInfo *info) {
    LogFloats(kHitWorld, &segment[0].x, 8);
    int hit = (g_hitPattern >> (g_hitCalls++ & 31)) & 1;
    if (hit) {
        info->point = { segment[1].x * 0.75f, segment[1].y - 1.0f, segment[1].z + 0.5f, 1.0f };
        info->normal = { 0.0f, 1.0f, 0.0f, 0.0f };
        info->faceType = uint8_t(g_hitCalls * 0x11);
    }
    return hit;
}
bool __fastcall FakeCheckHitWindow(void *, int, WorldCollisionInfo *info, bool create, int kind) {
    Log(kHitWindow, { Bits(info->point.x), Bits(info->point.y), Bits(info->point.z), uint32_t(create),
                      uint32_t(kind) });
    return false;
}
void FakeApplyWorldEffects(const float *position, const Coord4 *normal, int arg3, int arg4, const uint8_t *faceType,
                           int arg6, PhysicsObject *physics) {
    // A spark's normal comes from the original's unset vector: not compared
    bool spark = g_lastLocateWorld;
    Log(kWorldEffects, { Bits(position[0]), Bits(position[1]), Bits(position[2]), spark ? 0 : Bits(normal->x),
                         spark ? 0 : Bits(normal->y), spark ? 0 : Bits(normal->z), uint32_t(arg3), uint32_t(arg4),
                         *faceType, uint32_t(arg6), Word(physics) });
}
void __fastcall FakeAddPositionalLight(void *, int, const Coord4 *position, const Coord4 *colour) {
    float f[8] = { position->x, position->y, position->z, position->w, colour->x, colour->y, colour->z, colour->w };
    LogFloats(kLight, f, 8);
}
double FakeGaussRandom() {
    Log(kGauss, {});
    static const double kValues[8] = { 0.3125, -1.0625, 2.21875, -0.15625, 0.0, 1.5, -2.75, 0.640625 };
    return kValues[g_gaussCalls++ & 7] + 1.0 / 3.0;      // unrounded, as the x87 would leave it
}
Coord3 *FakePerturbVector(Coord3 *result, const Coord4 *direction, float spread) {
    Log(kPerturb, { Bits(direction->x), Bits(direction->y), Bits(direction->z), Bits(spread) });
    result->x = direction->x + spread;
    result->y = direction->y * 0.875f;
    result->z = direction->z - spread * 0.5f;
    return result;
}

// The ring's draw
EmpGeoPrim *SideRing() {
    return g_original ? OrigRingGeoPrim : EmpRingGeoPrim;
}
const void *SideRingUVs() {
    return g_original ? (const void *)(uintptr_t)kOrigRingUVs : (const void *)EmpRingUVs;
}
EAGL::DynamicModel *__fastcall FakeModelConstruct(EAGL::DynamicModel *model, int) {
    Log(kModelConstruct, {});
    memset(model, 0, sizeof(EAGL::DynamicModel));
    return model;
}
void __fastcall FakeSetModelMatrix(EAGL::DynamicModel *, int, const float *matrix) {
    LogFloats(kModelMatrix, matrix, 16);
}
int __fastcall FakeAddGeoPrim(EAGL::DynamicModel *, int, EAGL::GeoPrim *geoPrim) {
    Log(kModelAdd, { uint32_t(geoPrim == reinterpret_cast<EAGL::GeoPrim *>(SideRing())) });
    return 0;
}
void __fastcall FakeModelDraw(EAGL::DynamicModel *, int) {
    EmpGeoPrim *ring = SideRing();
    uint32_t param10 = g_original ? Word(ring->params[10].data) : ToOriginal(ring->params[10].data);
    Log(kModelDraw, { param10, ring->params[10].count, Hash(g_original ? OrigRingColours : EmpRingColours, 42 * 4),
                      Hash(SideRingUVs(), 0x150) });
}
void __fastcall FakeModelDestruct(EAGL::DynamicModel *, int) {
    Log(kModelDestruct, {});
}
bool __fastcall FakeZWrites(void *, int, uint8_t enable) {
    Log(kZWrites, { enable });
    return true;
}

// LoadAttributes
void FakeAttribS8(const char *name, int *data, int minimum, int maximum, uint32_t stride, float step,
                  const char *const *names) {
    Log(kAttrib, { 1, Hash(name, strlen(name)), ToOriginal(data), uint32_t(minimum), uint32_t(maximum), stride,
                   Bits(step), Word(names) });
}
void FakeAttribU8(const char *name, unsigned *data, unsigned minimum, unsigned maximum, uint32_t stride, float step,
                  const char *const *names) {
    Log(kAttrib, { 2, Hash(name, strlen(name)), ToOriginal(data), minimum, maximum, stride, Bits(step), Word(names) });
}
void FakeAttribFloat(const char *name, float *data, float minimum, float maximum, uint32_t stride, float step,
                     const char *const *names) {
    Log(kAttrib, { 3, Hash(name, strlen(name)), ToOriginal(data), Bits(minimum), Bits(maximum), stride, Bits(step),
                   Word(names) });
}
void FakeAttribArgb(const char *name, uint32_t *data, uint8_t *flag, uint32_t stride) {
    Log(kAttrib, { 4, Hash(name, strlen(name)), ToOriginal(data), ToOriginal(flag), stride });
}

// Kill's frees, classified by what they free
EaglFreeFn g_realEaglFree;
const void *g_freeTargets[4];
void FakeEaglFree(void *pointer, uint32_t size) {
    uint32_t which = 0;
    for (int i = 0; i < 4; i++) {
        if (pointer == g_freeTargets[i])
            which = uint32_t(i + 1);
    }
    Log(kEaglFree, { which, size });
    g_realEaglFree(pointer, size);
}

void InstallBoltFakes() {
    HookInstall(0x0009f840, (const void *)&FakeIsBoltAlive);
    HookInstall(0x000a0500, (const void *)&FakeAddRoundedBolt);
    HookInstall(0x000a0400, (const void *)&FakeAddBolt);
    HookInstall(0x0008dc90, (const void *)&FakeGetPosition);
    HookInstall(0x0008eca0, (const void *)&FakeLocateFX);
    HookInstall(0x000c3b40, (const void *)&FakeCheckHitWorld);
    HookInstall(0x000c4d70, (const void *)&FakeCheckHitWindow);
    HookInstall(0x000749b0, (const void *)&FakeApplyWorldEffects);
    HookInstall(0x0007e440, (const void *)&FakeAddPositionalLight);
    HookInstall(0x000bd130, (const void *)&FakeGaussRandom);
    HookInstall(0x000bd250, (const void *)&FakePerturbVector);
}

// ---- the debug variables: ours saved, perturbed, and copied to the original's addresses

uint32_t g_savedOurs[kTunableCount], g_savedOriginal[kTunableCount];

void SaveTunables() {
    for (int i = 0; i < kTunableCount; i++) {
        memcpy(&g_savedOurs[i], kTunables[i].ours, 4);
        memcpy(&g_savedOriginal[i], (const void *)(uintptr_t)kTunables[i].original, 4);
    }
}
void RestoreTunables() {
    for (int i = 0; i < kTunableCount; i++) {
        memcpy(kTunables[i].ours, &g_savedOurs[i], 4);
        memcpy((void *)(uintptr_t)kTunables[i].original, &g_savedOriginal[i], 4);
    }
}
void SyncTunables() {
    for (int i = 0; i < kTunableCount; i++)
        memcpy((void *)(uintptr_t)kTunables[i].original, kTunables[i].ours, 4);
}
void PerturbTunables() {
    for (int i = 0; i < kTunableCount; i++)
        memcpy(kTunables[i].ours, &g_savedOurs[i], 4);
    kAllowVehicleBolts = Chance(80);
    kAllowWorldBolts = Chance(80);
    kSnapToBoundingBox = Chance(50) ? RandomInt(1, 3) : 0;
    kGlowEnable = Chance(80);
    kTargetBoltsAlive = unsigned(RandomInt(0, 32));
    kHitWorldPct = Chance(10) ? (Chance(50) ? 0.0f : 1.0f) : Uniform(0.0f, 1.0f);
    kVehicleSparkRate = Chance(10) ? 1.0f : Uniform(0.0f, 1.0f);
    kScaleLifetime = Uniform(0.0f, 1.5f);
    kWorldArcFactor = Uniform(-12.0f, 12.0f);
    kHitWorldRange = Uniform(0.0f, 30.0f);
    kWorldPerturb = Uniform(0.0f, 1.0f);
    kWorldFlashSize = Uniform(0.0f, 50.0f);
    kGlowStrength = Uniform(0.0f, 50.0f);
    kGlowAmplitude = Uniform(0.0f, 10.0f);
    kGlowFrequency = Uniform(0.1f, 10.0f);
    kGlowNoiseStrength = Uniform(0.0f, 1.0f);
    kGlowNoiseRange = Uniform(0.0f, 5.0f);
    kGlowScatter = Uniform(0.0f, 10.0f);
    kGlowColour = NextRandom();
    kVehicleSegments = RandomInt(2, 5);
    kVehicleLevels = RandomInt(1, 4);
}

// =============================================================================================================
// REmp
// =============================================================================================================

uint8_t g_region[kOrigRegionHi - kOrigRegionLo];

struct InitResult {
    std::vector<uint8_t> region;            // the original's statics, or the port's translated into the same shape
    EmpGeoPrim sphere, ring;
};

void CaptureRegion(std::vector<uint8_t> *out) {
    out->assign((const uint8_t *)(uintptr_t)kOrigRegionLo, (const uint8_t *)(uintptr_t)kOrigRegionHi);
}

// The port's owned statics copied over the shape of the original's region
void OverlayPort(std::vector<uint8_t> *region) {
    for (const Translation &t : kTranslations) {
        if (t.size > 1 && t.original >= kOrigRegionLo && t.original + t.size <= kOrigRegionHi)
            memcpy(region->data() + (t.original - kOrigRegionLo), t.ours, t.size);
    }
    uint32_t light[2] = { ToOriginal(EmpLightParams.lights), ToOriginal(EmpLightParams.simStep) };
    memcpy(region->data() + (0x00200c80 - kOrigRegionLo), light, 8);
}

void CompareGeoPrims(const EmpGeoPrim &original, const EmpGeoPrim &port, const char *what, long index) {
    Check((original.method != NULL) == (port.method != NULL), what, index);
    for (int i = 0; i < 15; i++) {
        Check(original.params[i].count == port.params[i].count, what, index);
        Check(Word(original.params[i].data) == ToOriginal(port.params[i].data), what, index);
    }
}

struct OwnedStatics {
    CylinderUV ringUVs[42];
    Coord3 ringPositions[42];
    EmpSphereData sphere;
    EmpLightRecord lightParams;
    Coord4 lights[8];
};
OwnedStatics g_owned;

void SaveOwned() {
    memcpy(g_owned.ringUVs, EmpRingUVs, sizeof(EmpRingUVs));
    memcpy(g_owned.ringPositions, EmpRingPositions, sizeof(EmpRingPositions));
    g_owned.sphere = EmpSphere;
    g_owned.lightParams = EmpLightParams;
    memcpy(g_owned.lights, EmpLights, sizeof(EmpLights));
}
void RestoreOwned() {
    memcpy(EmpRingUVs, g_owned.ringUVs, sizeof(EmpRingUVs));
    memcpy(EmpRingPositions, g_owned.ringPositions, sizeof(EmpRingPositions));
    EmpSphere = g_owned.sphere;
    EmpLightParams = g_owned.lightParams;
    memcpy(EmpLights, g_owned.lights, sizeof(EmpLights));
}

// The GeoPrims' colours and states: ours (the static initialisers construct them) saved, and lent to the game's
// copies so that both sides start from them
struct LentStatics {
    uint32_t sphereColours[38];
    EAGL::GeoPrimState sphereState;
    uint32_t ringColours[42];
    EAGL::GeoPrimState ringState;
};
LentStatics g_lent;

void SaveLent() {
    memcpy(g_lent.sphereColours, EmpSphereColours, sizeof(EmpSphereColours));
    g_lent.sphereState = EmpSphereState;
    memcpy(g_lent.ringColours, EmpRingColours, sizeof(EmpRingColours));
    g_lent.ringState = EmpRingState;
    memcpy((void *)(uintptr_t)0x00200c90, EmpSphereColours, sizeof(EmpSphereColours));
    memcpy((void *)(uintptr_t)0x00200d30, &EmpSphereState, sizeof(EmpSphereState));
    memcpy((void *)(uintptr_t)0x00200d80, EmpRingColours, sizeof(EmpRingColours));
    memcpy((void *)(uintptr_t)0x00200e30, &EmpRingState, sizeof(EmpRingState));
}
void RestoreLent() {
    memcpy(EmpSphereColours, g_lent.sphereColours, sizeof(EmpSphereColours));
    EmpSphereState = g_lent.sphereState;
    memcpy(EmpRingColours, g_lent.ringColours, sizeof(EmpRingColours));
    EmpRingState = g_lent.ringState;
}
// ... and the port's copied over the shape of the original's region
void OverlayLent(std::vector<uint8_t> *region) {
    memcpy(region->data() + (0x00200c90 - kOrigRegionLo), EmpSphereColours, sizeof(EmpSphereColours));
    memcpy(region->data() + (0x00200d30 - kOrigRegionLo), &EmpSphereState, sizeof(EmpSphereState));
    memcpy(region->data() + (0x00200d80 - kOrigRegionLo), EmpRingColours, sizeof(EmpRingColours));
    memcpy(region->data() + (0x00200e30 - kOrigRegionLo), &EmpRingState, sizeof(EmpRingState));
}

void TestEmpInit() {
    EmpGeoPrim *liveRing = EmpRingGeoPrim, *liveSphere = EmpSphereGeoPrim;
    SaveOwned();
    SaveLent();
    memcpy(g_region, (const void *)(uintptr_t)kOrigRegionLo, sizeof(g_region));
    for (int c = 0; c < 2; c++) {
        g_cases++;
        InitResult results[2];
        std::vector<uint32_t> frees[2];
        for (int side = 0; side < 2; side++) {
            bool original = side == 0;
            memcpy((void *)(uintptr_t)kOrigRegionLo, g_region, sizeof(g_region));
            RestoreLent();
            memset(EmpRingUVs, 0x5a, sizeof(EmpRingUVs));
            memset(EmpRingPositions, 0x5a, sizeof(EmpRingPositions));
            memset(&EmpSphere, 0x5a, sizeof(EmpSphere));
            memset(EmpLights, 0x5a, sizeof(EmpLights));
            memset((void *)(uintptr_t)kOrigRegionLo, 0x5a, 0x00200c80 - kOrigRegionLo);
            Run(original, [] {
                if (g_original)
                    Orig_EmpInit();
                else
                    REmp::Init();
            });
            CaptureRegion(&results[side].region);
            if (!original)
                OverlayPort(&results[side].region);
            EmpGeoPrim *sphere = original ? OrigSphereGeoPrim : EmpSphereGeoPrim;
            EmpGeoPrim *ring = original ? OrigRingGeoPrim : EmpRingGeoPrim;
            memset(&results[side].sphere, 0, sizeof(EmpGeoPrim));
            memset(&results[side].ring, 0, sizeof(EmpGeoPrim));
            if (sphere != NULL)
                results[side].sphere = *sphere;
            if (ring != NULL)
                results[side].ring = *ring;

            // Kill, its frees logged
            g_log.clear();
            g_freeTargets[0] = sphere;
            g_freeTargets[1] = ring;
            g_freeTargets[2] = sphere != NULL ? sphere->method : NULL;
            g_freeTargets[3] = ring != NULL ? ring->method : NULL;
            g_realEaglFree = EaglFree;
            EaglFree = &FakeEaglFree;
            Run(original, [] {
                if (g_original)
                    Orig_EmpKill();
                else
                    REmp::Kill();
            });
            EaglFree = g_realEaglFree;
            frees[side] = g_log;
        }
        // The GeoPrim pointers themselves differ (fresh allocations): mask them
        for (int side = 0; side < 2; side++) {
            memset(results[side].region.data() + (0x001ff840 - kOrigRegionLo), 0, 4);
            memset(results[side].region.data() + (0x001ffb98 - kOrigRegionLo), 0, 4);
        }
        Check(results[0].region == results[1].region, "REmp::Init statics", c);
        if (results[0].region != results[1].region && g_reported < 10) {
            for (size_t i = 0; i < results[0].region.size(); i += 4) {
                if (memcmp(&results[0].region[i], &results[1].region[i], 4) != 0) {
                    printf("[gameeffectsshadowc]   first differing word at 0x%08x\n", unsigned(kOrigRegionLo + i));
                    break;
                }
            }
        }
        CompareGeoPrims(results[0].sphere, results[1].sphere, "REmp::Init sphere GeoPrim", c);
        CompareGeoPrims(results[0].ring, results[1].ring, "REmp::Init ring GeoPrim", c);
        Check(frees[0] == frees[1], "REmp::Kill frees", c);
    }
    memcpy((void *)(uintptr_t)kOrigRegionLo, g_region, sizeof(g_region));
    RestoreOwned();
    RestoreLent();
    EmpRingGeoPrim = liveRing;
    EmpSphereGeoPrim = liveSphere;
}

void TestEmpDraw() {
    if (EmpRingGeoPrim == NULL)
        return;
    EmpGeoPrim *liveRing = EmpRingGeoPrim;
    SaveLent();
    memcpy(g_region, (const void *)(uintptr_t)kOrigRegionLo, sizeof(g_region));
    CylinderUV savedUVs[42];
    memcpy(savedUVs, EmpRingUVs, sizeof(savedUVs));
    EmpGeoPrim copies[2];

    HookInstall(0x000e9760, (const void *)&FakeModelConstruct);
    HookInstall(0x000e9a80, (const void *)&FakeSetModelMatrix);
    HookInstall(0x000e9890, (const void *)&FakeAddGeoPrim);
    HookInstall(0x000e99e0, (const void *)&FakeModelDraw);
    HookInstall(0x000ea720, (const void *)&FakeModelDestruct);
    HookInstall(0x000e73f0, (const void *)&FakeZWrites);

    for (int c = 0; c < 200; c++) {
        g_cases++;
        MATRIX4 transform;
        for (int r = 0; r < 4; r++) {
            for (int k = 0; k < 4; k++)
                transform.mtx[r][k] = Uniform(-3.0f, 3.0f);
        }
        float progress = Uniform(0.0f, 1.0f);
        switch (c % 8) {
        case 0: progress = 0.0f; break;
        case 1: progress = 1.0f; break;
        case 2: progress = Uniform(-1.0f, 0.0f); break;
        case 3: progress = Uniform(1.0f, 4.0f); break;
        case 4: progress = std::bit_cast<float>(0x7fc00000u); break;
        default: break;
        }
        float uvs[84];
        for (int i = 0; i < 84; i++)
            uvs[i] = Uniform(-1.0f, 2.0f);
        uint32_t colours[42];
        for (int i = 0; i < 42; i++)
            colours[i] = NextRandom();

        std::vector<uint32_t> logs[2];
        std::vector<uint8_t> after[2];
        uint32_t param10[2];
        for (int side = 0; side < 2; side++) {
            bool original = side == 0;
            memcpy((void *)(uintptr_t)kOrigRegionLo, g_region, sizeof(g_region));
            RestoreLent();
            copies[side] = *liveRing;
            if (original)
                OrigRingGeoPrim = &copies[side];
            else
                EmpRingGeoPrim = &copies[side];
            memcpy(original ? (void *)(uintptr_t)kOrigRingUVs : (void *)EmpRingUVs, uvs, sizeof(uvs));
            memcpy(original ? OrigRingColours : EmpRingColours, colours, sizeof(colours));
            g_log.clear();
            Run(original, [&] {
                if (g_original)
                    Orig_EmpDraw(&transform, progress);
                else
                    REmp::DrawShootingEffect(&transform, progress);
            });
            logs[side] = g_log;
            CaptureRegion(&after[side]);
            if (!original) {
                memcpy(after[side].data() + (kOrigRingUVs - kOrigRegionLo), EmpRingUVs, sizeof(uvs));
                OverlayLent(&after[side]);
            }
            memset(after[side].data() + (0x001ff840 - kOrigRegionLo), 0, 4);
            param10[side] = original ? Word(copies[side].params[10].data) : ToOriginal(copies[side].params[10].data);
            EmpRingGeoPrim = liveRing;
        }
        Check(logs[0] == logs[1], "REmp::DrawShootingEffect calls", c);
        Check(after[0] == after[1], "REmp::DrawShootingEffect statics", c);
        Check(param10[0] == param10[1], "REmp::DrawShootingEffect parameter", c);
    }
    HooksRemove();
    memcpy((void *)(uintptr_t)kOrigRegionLo, g_region, sizeof(g_region));
    memcpy(EmpRingUVs, savedUVs, sizeof(savedUVs));
    RestoreLent();
    EmpRingGeoPrim = liveRing;
}

void TestLoadAttributes() {
    HookInstall(0x00039ff0, (const void *)&FakeAttribS8);
    HookInstall(0x0003a0f0, (const void *)&FakeAttribU8);
    HookInstall(0x0003a1f0, (const void *)&FakeAttribFloat);
    HookInstall(0x0003d530, (const void *)&FakeAttribArgb);
    std::vector<uint32_t> logs[2];
    for (int side = 0; side < 2; side++) {
        g_log.clear();
        Run(side == 0, [] {
            if (g_original)
                Orig_LoadAttributes();
            else
                REmpBolts::LoadAttributes();
        });
        logs[side] = g_log;
    }
    HooksRemove();
    g_cases++;
    Check(logs[0] == logs[1] && logs[0].size() > 29 * 2, "REmpBolts::LoadAttributes calls", 0);
}

// =============================================================================================================
// The maps
// =============================================================================================================

// The tree under `node` as a word list: shape, colours, keys and values (anchor pointers through `anchorBase`)
void WalkAnchors(const EmpAnchorNode *node, const EmpAnchor *anchorBase, std::vector<uint32_t> *out, int depth) {
    if (depth > 64) {
        out->push_back(0xdeadbeef);
        return;
    }
    if (node->isNil) {
        out->push_back(0xffffffff);
        return;
    }
    out->push_back(node->value.group);
    out->push_back(node->color);
    out->push_back(uint32_t(node->value.list.count));
    for (int i = 0; i < node->value.list.count && i < 64; i++) {
        const EmpAnchor *a = node->value.list.anchors[i];
        out->push_back(anchorBase != NULL ? uint32_t(a - anchorBase) : Word(a));
    }
    out->push_back(node->left->parent == node || node->left->isNil);
    out->push_back(node->right->parent == node || node->right->isNil);
    WalkAnchors(node->left, anchorBase, out, depth + 1);
    WalkAnchors(node->right, anchorBase, out, depth + 1);
}

std::vector<uint32_t> AnchorTree(const EmpAnchorMap &map, const EmpAnchor *anchorBase) {
    std::vector<uint32_t> out;
    out.push_back(map.size);
    if (map.head == NULL) {
        out.push_back(0);
        return out;
    }
    out.push_back(map.head->isNil);
    out.push_back(map.head->left->isNil ? 0xffffffff : map.head->left->value.group);
    out.push_back(map.head->right->isNil ? 0xffffffff : map.head->right->value.group);
    WalkAnchors(map.head->parent, anchorBase, &out, 0);
    return out;
}

void WalkCounts(const TreeNode *node, std::vector<uint32_t> *out, int depth) {
    if (depth > 64) {
        out->push_back(0xdeadbeef);
        return;
    }
    if (node->isNil) {
        out->push_back(0xffffffff);
        return;
    }
    out->push_back(node->value.tag);
    out->push_back(uint32_t(node->value.index));
    out->push_back(node->color);
    out->push_back(node->left->parent == node || node->left->isNil);
    out->push_back(node->right->parent == node || node->right->isNil);
    WalkCounts(node->left, out, depth + 1);
    WalkCounts(node->right, out, depth + 1);
}

std::vector<uint32_t> CountTree(const EmpGroupCountMap &map) {
    std::vector<uint32_t> out;
    out.push_back(map.size);
    out.push_back(map.head->left->isNil ? 0xffffffff : map.head->left->value.tag);
    out.push_back(map.head->right->isNil ? 0xffffffff : map.head->right->value.tag);
    WalkCounts(map.head->parent, &out, 0);
    return out;
}

// The node at in-order position `index` (the head past the end)
template <class Node>
Node *NodeAt(Node *head, int index) {
    Node *node = head->left;
    for (int i = 0; i < index && node != head; i++) {
        if (!node->right->isNil) {
            node = node->right;
            while (!node->left->isNil)
                node = node->left;
        } else {
            Node *parent = node->parent;
            while (!parent->isNil && node == parent->right) {
                node = parent;
                parent = parent->parent;
            }
            node = parent;
        }
    }
    return node;
}

uint32_t AnchorKey(const EmpAnchorNode *node) {
    return node->isNil ? 0xffffffff : node->value.group;
}
uint32_t CountKey(const TreeNode *node) {
    return node->isNil ? 0xffffffff : node->value.tag;
}

void TestAnchorMap() {
    EmpAnchorMap maps[2];
    for (int side = 0; side < 2; side++) {
        EmpAnchorMap *map = &maps[side];
        Run(side == 0, [&] { map->head = g_original ? Orig_AnchorBuyHead() : EmpAnchorMap::BuyHead(); });
        map->allocator = 0;
        map->head->isNil = 1;
        map->head->parent = map->head->left = map->head->right = map->head;
        map->size = 0;
    }
    for (int c = 0; c < 600; c++) {
        g_cases++;
        int op = RandomInt(0, 9);
        uint32_t key = uint32_t(RandomInt(0, 40)) | (Chance(10) ? 0x80000000u : 0);
        int count = RandomInt(0, 3);
        int first = RandomInt(0, int(maps[0].size));
        int last = RandomInt(first, int(maps[0].size));
        bool fromBegin = Chance(20), toEnd = Chance(20);
        uint32_t answers[2] = { 0, 0 };
        for (int side = 0; side < 2; side++) {
            EmpAnchorMap *map = &maps[side];
            Run(side == 0, [&] {
                if (op <= 3) {
                    EmpAnchorGroup value;
                    value.group = key;
                    value.list.count = count;
                    value.list.anchors = static_cast<EmpAnchor **>(OperatorNewArray(count * sizeof(EmpAnchor *)));
                    for (int i = 0; i < count; i++)
                        value.list.anchors[i] = reinterpret_cast<EmpAnchor *>(uintptr_t(0x1000 + i * 0x20 + key));
                    EmpAnchorMap::InsertResult result;
                    if (g_original)
                        Orig_AnchorInsertUnique(map, 0, &result, &value);
                    else
                        map->InsertUnique(&result, &value);
                    answers[side] = AnchorKey(result.where) ^ (result.inserted ? 0x40000000u : 0);
                    if (g_original)
                        Orig_GroupDestruct(&value, 0);
                    else
                        value.Destruct();
                } else if (op <= 5) {
                    EmpAnchorList *list = g_original ? Orig_AnchorLookup(map, 0, &key) : map->Lookup(&key);
                    answers[side] = uint32_t(list->count);
                } else if (op <= 7 && map->size > 0) {
                    EmpAnchorNode *where = NodeAt(map->head, first % int(map->size));
                    EmpAnchorNode *next;
                    if (g_original)
                        Orig_AnchorEraseAt(map, 0, &next, where);
                    else
                        map->EraseAt(&next, where);
                    answers[side] = AnchorKey(next);
                } else if (op == 8) {
                    EmpAnchorNode *from = fromBegin ? map->head->left : NodeAt(map->head, first);
                    EmpAnchorNode *to = toEnd ? map->head : NodeAt(map->head, last);
                    EmpAnchorNode *next;
                    if (g_original)
                        Orig_AnchorEraseRange(map, 0, &next, from, to);
                    else
                        map->EraseRange(&next, from, to);
                    answers[side] = AnchorKey(next);
                } else if (!map->head->parent->isNil) {
                    EmpAnchorNode *root = map->head->parent;
                    EmpAnchorNode *lo = g_original ? Orig_AnchorMin(root) : EmpAnchorMap::Min(root);
                    EmpAnchorNode *hi = g_original ? Orig_AnchorMax(root) : EmpAnchorMap::Max(root);
                    answers[side] = AnchorKey(lo) * 1000 + AnchorKey(hi);
                }
            });
        }
        Check(answers[0] == answers[1], "the anchor map's answers", c);
        Check(AnchorTree(maps[0], NULL) == AnchorTree(maps[1], NULL), "the anchor map's tree", c);
        if (maps[0].size > 40 || maps[1].size > 40)
            break;
    }
    // The list helpers, alone
    for (int c = 0; c < 20; c++) {
        g_cases++;
        int count = RandomInt(0, 6);
        EmpAnchorList lists[2];
        for (int side = 0; side < 2; side++) {
            EmpAnchorList source = { static_cast<EmpAnchor **>(OperatorNewArray(count * sizeof(EmpAnchor *))), count };
            for (int i = 0; i < count; i++)
                source.anchors[i] = reinterpret_cast<EmpAnchor *>(uintptr_t(0x3000 + i));
            lists[side].anchors = NULL;
            lists[side].count = 0;
            lists[side].Assign(&source);
            source.Clear();
            Run(side == 0, [&] {
                if (g_original)
                    Orig_ListClear(&lists[side], 0);
                else
                    lists[side].Clear();
            });
        }
        Check(lists[0].anchors == NULL && lists[1].anchors == NULL && lists[0].count == lists[1].count,
              "EmpAnchorList::Clear", c);
    }
    for (int side = 0; side < 2; side++) {
        EmpAnchorMap *map = &maps[side];
        Run(side == 0, [&] {
            if (g_original)
                Orig_AnchorDestruct(map, 0);
            else
                map->Destruct();
        });
    }
    g_cases++;
    Check(maps[0].head == NULL && maps[1].head == NULL && maps[0].size == 0 && maps[1].size == 0,
          "EmpAnchorMap::Destruct", 0);
}

void TestCountMap() {
    EmpGroupCountMap maps[2];
    for (int side = 0; side < 2; side++) {
        maps[side].allocator = 0;
        maps[side].Init();
    }
    for (int c = 0; c < 600; c++) {
        g_cases++;
        int op = RandomInt(0, 7);
        TreePair value;
        value.tag = uint32_t(RandomInt(0, 40)) | (Chance(10) ? 0x80000000u : 0);
        value.index = RandomInt(-5, 5);
        int first = RandomInt(0, int(maps[0].size));
        int last = RandomInt(first, int(maps[0].size));
        bool whole = Chance(15);
        uint32_t answers[2] = { 0, 0 };
        for (int side = 0; side < 2; side++) {
            EmpGroupCountMap *map = &maps[side];
            Run(side == 0, [&] {
                if (op <= 3) {
                    TreeInsertResult result;
                    if (g_original)
                        Orig_CountInsertUnique(map, 0, &result, &value);
                    else
                        map->InsertUnique(&result, &value);
                    answers[side] = CountKey(result.where) ^ (result.inserted ? 0x40000000u : 0);
                } else if (op <= 5 && map->size > 0) {
                    TreeNode *where = NodeAt(map->head, first % int(map->size));
                    TreeNode *next;
                    if (g_original)
                        Orig_CountEraseAt(map, 0, &next, where);
                    else
                        map->EraseAt(&next, where);
                    answers[side] = CountKey(next);
                } else if (op == 6) {
                    TreeNode *from = whole ? map->head->left : NodeAt(map->head, first);
                    TreeNode *to = whole ? map->head : NodeAt(map->head, last);
                    TreeNode *next;
                    if (g_original)
                        Orig_CountEraseRange(map, 0, &next, from, to);
                    else
                        map->EraseRange(&next, from, to);
                    answers[side] = CountKey(next);
                } else if (!map->head->parent->isNil) {
                    TreeNode *root = map->head->parent;
                    answers[side] = CountKey(g_original ? Orig_TreeMin(root) : SharedTreeMin(root));
                }
            });
        }
        Check(answers[0] == answers[1], "the count map's answers", c);
        Check(CountTree(maps[0]) == CountTree(maps[1]), "the count map's tree", c);
        if (maps[0].size > 40)
            break;
    }
    for (int side = 0; side < 2; side++) {
        EmpGroupCountMap *map = &maps[side];
        Run(side == 0, [&] {
            if (g_original)
                Orig_CountDestroy(map, 0);
            else
                map->Destroy();
        });
    }
    g_cases++;
    Check(maps[0].head == NULL && maps[1].head == NULL, "EmpGroupCountMap::Destroy", 0);
}

// =============================================================================================================
// REmpBolts on a synthetic car
// =============================================================================================================

alignas(16) uint8_t g_vehicleBytes[sizeof(RSceneObj)];
alignas(16) uint8_t g_handleBytes[sizeof(Handle)];
ArticleEffect g_effects[24];
alignas(16) uint8_t g_objects[2][sizeof(REmpBolts)];

RSceneObj *Vehicle() {
    return reinterpret_cast<RSceneObj *>(g_vehicleBytes);
}
REmpBolts *Object(int side) {
    return reinterpret_cast<REmpBolts *>(g_objects[side]);
}

// The car's locators: `names` in turn, each a locator unless it starts with '!'
void BuildCar(const char *const *names, int count) {
    memset(g_vehicleBytes, 0, sizeof(g_vehicleBytes));
    memset(g_handleBytes, 0, sizeof(g_handleBytes));
    memset(g_effects, 0, sizeof(g_effects));
    Handle *handle = reinterpret_cast<Handle *>(g_handleBytes);
    handle->effects = g_effects;
    handle->effectCount = uint8_t(count);
    for (int i = 0; i < count; i++) {
        const char *name = names[i];
        bool locator = name[0] != '!';
        if (!locator)
            name++;
        g_effects[i].type = locator ? ArticleEffect::kTypeLocator : ArticleEffect::kTypeGlare;
        strncpy(g_effects[i].locator.name, name, sizeof(g_effects[i].locator.name) - 1);
    }
    RSceneObj *vehicle = Vehicle();
    vehicle->animHandle = handle;
    vehicle->physics = reinterpret_cast<PhysicsObject *>(uintptr_t(0x0badf00d));
    g_effectBase = g_effects;
}

// The object's words that must match, its anchors and its map (anchor pointers as indices)
std::vector<uint32_t> Snapshot(REmpBolts *bolts) {
    std::vector<uint32_t> out;
    const uint32_t *words = reinterpret_cast<const uint32_t *>(bolts);
    for (size_t i = 0; i < sizeof(REmpBolts) / 4; i++) {
        size_t offset = i * 4;
        if (offset == offsetof(REmpBolts, anchors) || offset == offsetof(REmpBolts, groups) ||
            offset == offsetof(REmpBolts, groups) + 4)
            continue;
        out.push_back(words[i]);
    }
    out.push_back(bolts->anchors == NULL);
    if (bolts->anchors != NULL) {
        for (int i = 0; i < bolts->numAnchors && i < 24; i++) {
            const EmpAnchor &a = bolts->anchors[i];
            out.push_back(Word(a.effect));
            out.push_back(a.group);
            out.push_back(a.linkCount);
            for (int k = 0; k < 5; k++)
                out.push_back(uint32_t(a.links[k]));
        }
    }
    std::vector<uint32_t> tree = AnchorTree(bolts->groups, bolts->anchors);
    out.insert(out.end(), tree.begin(), tree.end());
    return out;
}

int32_t SimStepCountNow() {
    return I32_AT(0x00234e34);
}

// One method case: the same state on both objects, then the original on the first and the port on the second
template <class F>
void BoltsCase(const char *what, long index, const F &call) {
    g_cases++;
    PerturbTunables();
    uint32_t seed = NextRandom() & 0xffff;
    uint32_t steps = Chance(10) ? 0 : uint32_t(RandomInt(1, 12));
    uint32_t numBolts = Chance(30) ? 0 : uint32_t(RandomInt(0, 32));
    uint32_t cursor = numBolts == 0 ? 0 : uint32_t(RandomInt(0, int(numBolts) - 1));
    EmpBolt bolts[32];
    for (int i = 0; i < 32; i++) {
        bolts[i].handle = int(NextRandom() & 0xfff);
        bolts[i].unknown04[0] = NextRandom();
        bolts[i].unknown04[1] = NextRandom();
        bolts[i].unknown04[2] = NextRandom();
    }
    g_alivePattern = NextRandom();
    g_hitPattern = NextRandom();
    g_position = { Uniform(-100.0f, 100.0f), Uniform(-10.0f, 10.0f), Uniform(-100.0f, 100.0f) };
    int pickA = RandomInt(0, 23), pickB = RandomInt(0, 23);
    uint32_t group = uint32_t(RandomInt(0x40, 0x46));
    Coord3 hit = { Uniform(-50.0f, 50.0f), Uniform(-5.0f, 5.0f), Uniform(-50.0f, 50.0f) };
    bool anchorsGroup = Chance(70);

    std::vector<uint32_t> logs[2], states[2];
    uint32_t seeds[2], gauss[2], answers[2] = { 0, 0 };
    for (int side = 0; side < 2; side++) {
        REmpBolts *object = Object(side);
        object->lastUpdateTime = SimStepCountNow() - steps;
        object->numBolts = numBolts;
        object->boltCursor = cursor;
        memcpy(object->bolts, bolts, sizeof(bolts));
        SyncTunables();
        ShadowSeed = seed;
        ResetFakes();
        g_log.clear();
        EmpAnchor *a = object->numAnchors > 0 ? &object->anchors[pickA % object->numAnchors] : NULL;
        EmpAnchor *b = object->numAnchors > 0 ? &object->anchors[pickB % object->numAnchors] : NULL;
        uint32_t key = a != NULL && anchorsGroup ? a->group : group;
        Run(side == 0, [&] { answers[side] = call(object, a, b, key, &hit); });
        logs[side] = g_log;
        seeds[side] = ShadowSeed;
        gauss[side] = uint32_t(g_gaussCalls);
        states[side] = Snapshot(object);
    }
    Check(logs[0] == logs[1], what, index);
    Check(seeds[0] == seeds[1] && gauss[0] == gauss[1], what, index);
    Check(answers[0] == answers[1], what, index);
    Check(states[0] == states[1], what, index);
}

void TestBolts() {
    static const char *const kCar1[] = {
        "empA1", "empA23", "!empA9", "empB", "emp", "empB123456", "empC", "xemp1", "empA", "em", "empB7", "empA45",
    };
    static const char *const kCar2[] = { "empZ1", "wheel", "!empZ2" };
    static const char *const kCar3[] = { "door", "wheel" };
    static const char *const kCar4[] = {
        "emp1", "emp2a", "emp3", "emp1b", "emp\xc1x", "emp\xc1", "emp2", "emp9", "empq", "empq12", "emp1",
        "emp\xff", "empA", "empB", "empC", "empD", "empE", "empF", "empG", "empH",
    };
    struct Car {
        const char *const *names;
        int count;
    } cars[] = {
        { kCar1, int(sizeof(kCar1) / sizeof(kCar1[0])) }, { kCar2, 3 }, { kCar3, 2 },
        { kCar4, int(sizeof(kCar4) / sizeof(kCar4[0])) },
    };

    InstallBoltFakes();
    for (int carIndex = 0; carIndex < 4; carIndex++) {
        BuildCar(cars[carIndex].names, cars[carIndex].count);
        g_cases++;
        std::vector<uint32_t> built[2];
        for (int side = 0; side < 2; side++) {
            memset(g_objects[side], 0xcc, sizeof(REmpBolts));
            REmpBolts *object = Object(side);
            Run(side == 0, [&] {
                if (g_original)
                    Orig_BoltsConstruct(object, 0, Vehicle());
                else
                    object->Construct(Vehicle());
            });
            // A single anchor is left unset by FindAnchors (the original's too): the same record on both sides
            if (object->numAnchors == 1) {
                EmpAnchor single = { &g_effects[0], 'Z', 1, { '1', 0, 0, 0, 0 } };
                object->anchors[0] = single;
            }
            built[side] = Snapshot(object);
        }
        Check(built[0] == built[1], "REmpBolts::Construct", carIndex);

        for (int c = 0; c < 150; c++) {
            int which = c % 6;
            switch (which) {
            case 0:
                BoltsCase("REmpBolts::Draw", c, [](REmpBolts *o, EmpAnchor *, EmpAnchor *, uint32_t, const Coord3 *) {
                    if (g_original)
                        Orig_BoltsDraw(o, 0);
                    else
                        o->Draw();
                    return 0u;
                });
                break;
            case 1:
                BoltsCase("REmpBolts::Update", c, [](REmpBolts *o, EmpAnchor *, EmpAnchor *, uint32_t, const Coord3 *) {
                    if (g_original)
                        Orig_BoltsUpdate(o, 0);
                    else
                        o->Update();
                    return 0u;
                });
                break;
            case 2:
                BoltsCase("REmpBolts::TrySpawnBolt", c,
                          [](REmpBolts *o, EmpAnchor *, EmpAnchor *, uint32_t, const Coord3 *) {
                              if (g_original)
                                  Orig_TrySpawnBolt(o, 0);
                              else
                                  o->TrySpawnBolt();
                              return 0u;
                          });
                break;
            case 3:
                BoltsCase("REmpBolts::GetRandomAnchor", c,
                          [](REmpBolts *o, EmpAnchor *, EmpAnchor *, uint32_t key, const Coord3 *) {
                              EmpAnchor *found = g_original ? Orig_GetRandomAnchor(o, 0, key) : o->GetRandomAnchor(key);
                              return found == NULL ? 0xffffffffu : uint32_t(found - o->anchors);
                          });
                break;
            case 4:
                if (Object(0)->numAnchors > 0) {
                    BoltsCase("REmpBolts::CreateVehicleBolt", c,
                              [](REmpBolts *o, EmpAnchor *a, EmpAnchor *b, uint32_t, const Coord3 *) {
                                  if (g_original)
                                      Orig_CreateVehicleBolt(o, 0, a, b);
                                  else
                                      o->CreateVehicleBolt(a, b);
                                  return 0u;
                              });
                }
                break;
            default:
                if (Object(0)->numAnchors > 0) {
                    BoltsCase("REmpBolts::CreateWorldBolt", c,
                              [](REmpBolts *o, EmpAnchor *a, EmpAnchor *, uint32_t, const Coord3 *hit) {
                                  if (g_original)
                                      Orig_CreateWorldBolt(o, 0, a, hit);
                                  else
                                      o->CreateWorldBolt(a, hit);
                                  return 0u;
                              });
                }
                break;
            }
        }

        g_cases++;
        std::vector<uint32_t> destroyed[2];
        for (int side = 0; side < 2; side++) {
            REmpBolts *object = Object(side);
            Run(side == 0, [&] {
                if (g_original)
                    Orig_BoltsDestruct(object, 0);
                else
                    object->Destruct();
            });
            destroyed[side].assign(reinterpret_cast<const uint32_t *>(object),
                                   reinterpret_cast<const uint32_t *>(object) + 9);
            destroyed[side][6] = 0;     // the allocator byte's word
        }
        Check(destroyed[0] == destroyed[1], "REmpBolts::Destruct", carIndex);
    }
    HooksRemove();
}

} // namespace

void GameEffectsShadowC_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEEFFECTSSHADOWC");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    FpControlGet(&g_x87, &g_sse);
    uint32_t savedSeed = ShadowSeed;
    SaveTunables();

    TestEmpInit();
    TestEmpDraw();
    TestLoadAttributes();
    TestAnchorMap();
    TestCountMap();
    TestBolts();

    RestoreTunables();
    ShadowSeed = savedSeed;
    printf("[gameeffectsshadowc] EMP effects and their maps: %ld cases, %ld checks, %ld differ (%ld calls faulted)\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
