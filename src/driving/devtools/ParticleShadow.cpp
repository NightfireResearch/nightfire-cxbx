#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "ParticleShadow.h"
#include "FpControl.h"

#include "../eagl/D3D8State.h"
#include "../eagl/RenderContext.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../render/Materials.h"
#include "../render/ParticleCache.h"
#include "../render/Particles.h"
#include "../render/Particulate.h"
#include "../render/RenderHigh.h"
#include "../render/Renderer.h"
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
// NIGHTFIRE_PARTICLESHADOW=1, from the first simulation tick: the particle code (render/Particles.cpp,
// ParticleCache.cpp, Particulate.cpp) against the originals, the two address ranges 0x000a1c00-0x000a4550 and
// 0x000aaa60-0x000abfc0 swapped back for each original run (common/xbeOriginal.h). Every case runs both sides from
// one state: the watched memory - the game's random generator, the live cache, the live library's types, the
// particle vertex arrays and materials, the current material requests, the point render states, the case's own
// working copies - is saved, the original run, its result kept, the memory put back, ours run, and the two results
// compared byte for byte; the memory is put back again after. The live library's types are perturbed for some
// cases (flags, sizes, colours) and put back at the end.
//
//   - RParticle::ResetParticle on types copied from the library and perturbed (every flag, both spreads, a life
//     past 255), random emitters, axes, inherited velocities, motions and fractions.
//   - MungeData and MungeDataForPlatform on such types, in place and copied; FindSystemViaTag / ViaIndex.
//   - RParticleSystem's constructor (with and without InitData), SetSystem, Update (on and off times, the size
//     cut-off, every flag, moving emitters, 1-3 ticks: spawning into the live cache) and the deleting destructor,
//     on copies; RParticleCreator::Create.
//   - CreateParticles, steep and level directions, into the live cache.
//   - The cache: Spawn, JustUpdate (with UpdateBlock), RenderBlock (with RenderParticle) and UpdateAndRender on
//     copies filled with random particles of the live types; UVolatileMaterial::Draw is pointed at a recorder.
//   - The library on scratch copies: the RPartLibraryData constructor, AddSystem (both, past 256), Reload and the
//     destructors; the maps' insert and erase code on scratch maps, compared by shape.
//   - RParticulate::Update, _init and Draw on a copy of the live one (USimpleMaterial::Draw pointed at a recorder).
//
// The manager's constructor, destructor, Init, Shutdown, Reset, AddOrRefresh and UpdateSpawnAllSystems change the
// live systems: lockstep runs test them.
//
// One mutation this catches: ResetParticle drawing z's random spread before x's changes the particle's position
// in every case.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x000a1c00, 0x000a4550, original);
    XbeOriginal_RestoreRange(0x000aaa60, 0x000abfc0, original);
}

// ---- the originals

typedef RParticleSystemData RType;
typedef RParticleLibrary::RPartLibraryData RLibData;

#define Orig_ResetParticle ((void (__fastcall *)(RParticle *, int, const Coord3 *, const RType *, const RParticleEmitter *, const Coord4 *, const Coord3 *, const Coord4 *, float, float))0x000aadc0)
#define Orig_MungeData ((void (*)(RType *, RType *))0x000a1e60)
#define Orig_MungeDataForPlatform ((void (*)(RType *, RType *))0x000ab460)
#define Orig_FindSystemViaTag ((RType *(__fastcall *)(RParticleLibrary *, int, uint32_t))0x000a2100)
#define Orig_FindSystemViaIndex ((RType *(__fastcall *)(RParticleLibrary *, int, int))0x000a1e40)
#define Orig_SystemConstruct ((RParticleSystem *(__fastcall *)(RParticleSystem *, int, float, RParticleEmitter *, bool))0x000a2140)
#define Orig_SetSystem ((void (__fastcall *)(RParticleSystem *, int, uint32_t))0x000a2290)
#define Orig_SystemUpdate ((void (__fastcall *)(RParticleSystem *, int, int))0x000a1f00)
#define Orig_SystemDelete ((RParticleSystem *(__fastcall *)(RParticleSystem *, int, unsigned))0x000a1ed0)
#define Orig_Create ((RParticleSystem *(__fastcall *)(RParticleSystem::RParticleCreator *, int, float, RParticleEmitter *))0x000a2350)
#define Orig_CreateParticles ((void (__fastcall *)(RParticleSystemManager *, int, RType *, const Coord3 *, const Coord3 *, int))0x000a1c60)
#define Orig_Spawn ((RParticle *(__fastcall *)(RParticleParticleCache *, int))0x000aabd0)
#define Orig_JustUpdate ((void (__fastcall *)(RParticleParticleCache *, int))0x000aad60)
#define Orig_RenderBlock ((void (__fastcall *)(RParticleParticleCache *, int, int))0x000aba10)
#define Orig_UpdateAndRender ((void (__fastcall *)(RParticleParticleCache *, int))0x000abf20)
#define Orig_LibDataConstruct ((RLibData *(__fastcall *)(RLibData *, int))0x000a2e20)
#define Orig_LibDataDestruct ((void (__fastcall *)(RLibData *, int))0x000a2de0)
#define Orig_LibDataAddSystem ((int (__fastcall *)(RLibData *, int, RType *, uint32_t))0x000a2c80)
#define Orig_LibraryAddSystem ((int (__fastcall *)(RParticleLibrary *, int, RType *, uint32_t))0x000a2d10)
#define Orig_LibraryReload ((void (__fastcall *)(RParticleLibrary *, int))0x000a2d50)
#define Orig_InsertUnique ((TreeInsertResult *(__fastcall *)(ParticleMap *, int, TreeInsertResult *, const TreePair *))0x000a2a70)
#define Orig_EraseAt ((TreeNode **(__fastcall *)(ParticleMap *, int, TreeNode **, TreeNode *))0x000a2660)
#define Orig_EraseSystemRange ((TreeNode **(__fastcall *)(ParticleMap *, int, TreeNode **, TreeNode *, TreeNode *))0x000a25a0)
#define Orig_EraseTypeRange ((TreeNode **(__fastcall *)(ParticleMap *, int, TreeNode **, TreeNode *, TreeNode *))0x000a2b30)
#define Orig_ParticulateInit ((void (__fastcall *)(RParticulate *, int))0x000a32b0)
#define Orig_ParticulateUpdate ((void (__fastcall *)(RParticulate *, int))0x000a3fd0)
#define Orig_ParticulateDraw ((void (__fastcall *)(RParticulate *, int))0x000a3e60)

#define RandomSeed U32_AT(0x001c45c4)
#define ParticleFovScale FLOAT_AT(0x001c8ed4)
#define VertexArrays ((uint8_t *)0x00209d30)        // the vertex arrays, materials and drag, up to 0x00230604
const size_t kVertexBytes = 0x00230604 - 0x00209d30;
const unsigned kVolatileDraw = 0x0011c290;
const unsigned kSimpleDraw = 0x0011bbf0;

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[particles]   %s #%d: %s\n", what, index, detail);
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

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

// ---- the watched memory: saved before a case, the original's and ours kept, put back after

struct Region {
    const char *name;
    uint8_t *at;
    size_t size;
    uint8_t *before, *original, *port;
};

const int kMaxRegions = 16;
Region g_regions[kMaxRegions];
int g_regionCount = 0;
int g_fixedRegions = 0;

void Watch(const char *name, void *at, size_t size) {
    if (at == NULL || g_regionCount == kMaxRegions)
        return;
    Region *r = &g_regions[g_regionCount++];
    r->name = name;
    r->at = static_cast<uint8_t *>(at);
    r->size = size;
    r->before = static_cast<uint8_t *>(malloc(size));
    r->original = static_cast<uint8_t *>(malloc(size));
    r->port = static_cast<uint8_t *>(malloc(size));
}

// The case's own regions forgotten
void Unwatch() {
    while (g_regionCount > g_fixedRegions) {
        Region *r = &g_regions[--g_regionCount];
        free(r->before);
        free(r->original);
        free(r->port);
    }
}

void SaveAll(int which) {
    for (int i = 0; i < g_regionCount; i++) {
        Region *r = &g_regions[i];
        memcpy(which == 0 ? r->before : which == 1 ? r->original : r->port, r->at, r->size);
    }
}

void RestoreAll() {
    for (int i = 0; i < g_regionCount; i++)
        memcpy(g_regions[i].at, g_regions[i].before, g_regions[i].size);
}

typedef void (*CaseFn)(bool original);

// One side; a fault is counted, not fatal. The original runs inside the window.
bool Guarded(CaseFn run, bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

// Both sides from the state as it is now, every watched region compared, the state put back
void SideBySide(const char *what, int index, CaseFn run) {
    g_cases++;
    SaveAll(0);
    bool originalOk = Guarded(run, true);
    SaveAll(1);
    RestoreAll();
    bool portOk = Guarded(run, false);
    SaveAll(2);
    RestoreAll();
    if (!originalOk || !portOk) {
        if (g_details++ < 10)
            printf("[particles]   %s #%d: faulted (original %s, port %s)\n", what, index, originalOk ? "ran" : "faulted",
                   portOk ? "ran" : "faulted");
        return;
    }
    for (int i = 0; i < g_regionCount; i++) {
        char name[64];
        snprintf(name, sizeof(name), "%s: %s", what, g_regions[i].name);
        CheckBytes(name, index, g_regions[i].original, g_regions[i].port, g_regions[i].size);
    }
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x9a7c1e5u;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

bool Chance(int percent) {
    return int(Next() % 100) < percent;
}

Coord3 RandomPoint(float range) {
    Coord3 p = { Uniform(-range, range), Uniform(-range, range), Uniform(-range, range) };
    return p;
}

RParticleLibrary *Library() {
    return &fgParticleSystems->library;
}

int TypeCount() {
    return int(Library()->data->count);
}

const uint32_t kFlagBits[] = { kParticleFlag02, kParticleRotates, kParticleSpinFixed, kParticleStreak,
                               kParticleUsesAlphaByte, kParticleUsesSizeByte, kParticleFlag100, kParticleAdditive };

bool g_haveView = false;

// A live type, perturbed. `munged`: values as the munged types have them, else as loaded.
RType RandomType(bool munged) {
    RType t = Library()->data->systems[Next() % TypeCount()];
    for (uint32_t bit : kFlagBits)
        if (Chance(25))
            t.flags ^= bit;
    if (!g_haveView)
        t.flags &= ~kParticleSpinFixed;
    if (Chance(50)) {
        t.spreadX = Uniform(0, 2);
        t.spreadZ = Uniform(0, 2);
        t.speed = Uniform(-1, 1);
        t.speedSpread = Uniform(0, 1);
        t.sideSpeed = Uniform(0, 1);
        t.axisSpeed = Uniform(0, 1);
        t.gravity = Uniform(-40, 40);
        t.gravitySpread = Uniform(0, 20);
        t.spin = munged ? Uniform(-3, 3) : Uniform(-180, 180);
        t.size = Uniform(0, 3);
        t.sizeChange = Uniform(-3, 3);
    }
    t.alphaSpread = Chance(40) ? Uniform(0, 1) : 0.0f;
    t.unknownA0 = Chance(40) ? Uniform(0, 3) : 0.0f;
    t.unknown2C = Next() & 0xff;
    if (Chance(30))
        t.life = !munged && Chance(5) ? 256 + Next() % 50 : 1 + Next() % 255;  // past 255 prints the game's warning
    if (Chance(30)) {
        t.onTime = Next() % 40;
        t.offTime = Chance(30) ? 0 : Next() % 40;
    }
    if (Chance(30))
        t.count = int32_t(Next() % 200);
    t.unknown48 = Chance(50) ? Next() % 30 : 0;
    float colourScale = munged ? 1.0f : 300.0f;
    for (int i = 0; i < 3 && Chance(50); i++) {
        t.colours[i].x = Uniform(0, colourScale);
        t.colours[i].y = Uniform(0, colourScale);
        t.colours[i].z = Uniform(0, colourScale);
        t.colours[i].w = Uniform(0, colourScale);
    }
    return t;
}

// ---- recorders standing in for the materials' Draw

struct DrawRecord {
    const void *material;
    int primitive;
    int count;
    const void *transform;
    const void *positions;
    const void *colours;
    const void *uvs;
    const void *texture;
};

struct DrawLog {
    int count;
    DrawRecord draws[8];
};
DrawLog g_drawLog;

void __fastcall RecordVolatileDraw(UVolatileMaterial *material, int, int primitive, int count, MATRIX4 *transform) {
    if (g_drawLog.count == 8)
        return;
    const TexturedGeoPrim *request = VolatileRequests[VolatileRequestIndex];
    DrawRecord d = { material, primitive, count, transform, request->positions.data, request->colours.data,
                     request->texCoords.data, request->texture.data };
    g_drawLog.draws[g_drawLog.count++] = d;
}

void __fastcall RecordSimpleDraw(USimpleMaterial *material, int, int primitive, int count, MATRIX4 *transform) {
    if (g_drawLog.count == 8)
        return;
    const SimpleGeoPrim *request = SimpleRequests[SimpleRequestIndex];
    DrawRecord d = { material, primitive, count, transform, request->positions.data, request->colours.data, NULL,
                     NULL };
    g_drawLog.draws[g_drawLog.count++] = d;
}

// Our Draws' entries jumped to the recorders as well: our callers call them directly
struct EntryJump {
    uint8_t *at;
    uint8_t saved[5];
};
EntryJump g_volatileEntry, g_simpleEntry;

void JumpEntry(EntryJump *jump, const void *at, const void *to) {
    jump->at = static_cast<uint8_t *>(const_cast<void *>(at));
    DWORD old;
    VirtualProtect(jump->at, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(jump->saved, jump->at, 5);
    int32_t rel = int32_t(uintptr_t(to) - (uintptr_t(jump->at) + 5));
    jump->at[0] = 0xe9;
    memcpy(jump->at + 1, &rel, 4);
    VirtualProtect(jump->at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), jump->at, 5);
}

void RestoreEntry(EntryJump *jump) {
    if (jump->at == NULL)
        return;
    DWORD old;
    VirtualProtect(jump->at, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(jump->at, jump->saved, 5);
    VirtualProtect(jump->at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), jump->at, 5);
    jump->at = NULL;
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

// ---- ResetParticle, MungeData

struct ResetWork {
    RParticle particle;
    RType type;
    RParticleEmitter emitter;
    Coord3 start;
    Coord4 axis;
    Coord3 velocity;
    Coord4 motion;
    float fraction;
    float ticks;
    bool hasVelocity;
} g_reset;

void RunReset(bool original) {
    ResetWork *w = &g_reset;
    const Coord3 *velocity = w->hasVelocity ? &w->velocity : NULL;
    if (original)
        Orig_ResetParticle(&w->particle, 0, &w->start, &w->type, &w->emitter, &w->axis, velocity, &w->motion,
                           w->fraction, w->ticks);
    else
        w->particle.ResetParticle(&w->start, &w->type, &w->emitter, &w->axis, velocity, &w->motion, w->fraction,
                                  w->ticks);
}

void TestReset() {
    Watch("reset", &g_reset, sizeof(g_reset));
    for (int i = 0; i < 2000; i++) {
        memset(&g_reset, 0xcd, sizeof(g_reset));
        g_reset.type = RandomType(true);
        memset(&g_reset.emitter, 0, sizeof(g_reset.emitter));
        g_reset.emitter.direction = RandomPoint(1);
        g_reset.emitter.side = RandomPoint(1);
        g_reset.emitter.body = Chance(50) ? 0xff : Next() & 0xff;
        g_reset.start = RandomPoint(200);
        Coord3 axis = RandomPoint(1);
        g_reset.axis = { axis.x, axis.y, axis.z, 5.0f };
        g_reset.velocity = RandomPoint(30);
        g_reset.hasVelocity = Chance(50);
        Coord3 motion = RandomPoint(2);
        g_reset.motion = { motion.x, motion.y, motion.z, 0.0f };
        g_reset.fraction = Chance(20) ? 0.0f : Uniform(0, 1);
        g_reset.ticks = float(Next() % 4);
        RandomSeed = Next() & 0xffff;
        SideBySide("ResetParticle", i, RunReset);
    }
    Unwatch();
}

struct MungeWork {
    RType source;
    RType data;
    int which;
} g_munge;

void RunMunge(bool original) {
    MungeWork *w = &g_munge;
    RType *source = w->which & 1 ? &w->data : &w->source;
    if (w->which & 2)
        (original ? Orig_MungeData : RParticleLibrary::MungeData)(source, &w->data);
    else
        (original ? Orig_MungeDataForPlatform : RParticleLibrary::MungeDataForPlatform)(source, &w->data);
}

void TestMunge() {
    Watch("munge", &g_munge, sizeof(g_munge));
    for (int i = 0; i < 400; i++) {
        g_munge.source = RandomType(false);
        g_munge.data = RandomType(false);
        g_munge.which = i & 3;
        SideBySide("MungeData", i, RunMunge);
    }
    Unwatch();
}

struct FindWork {
    uint32_t tag;
    int index;
    RType *byTag;
    RType *byIndex;
} g_find;

void RunFind(bool original) {
    FindWork *w = &g_find;
    if (original) {
        w->byTag = Orig_FindSystemViaTag(Library(), 0, w->tag);
        w->byIndex = Orig_FindSystemViaIndex(Library(), 0, w->index);
    } else {
        w->byTag = Library()->FindSystemViaTag(w->tag);
        w->byIndex = Library()->FindSystemViaIndex(w->index);
    }
}

// A tag the library has, or now and then one it may not
uint32_t RandomTag() {
    ParticleMap *types = &Library()->data->types;
    if (Chance(20) || types->size == 0)
        return Next();
    int steps = int(Next() % types->size);
    TreeNode *node = types->Begin();
    while (steps-- > 0)
        node = TreeNext(node);
    return node->value.tag;
}

void TestFind() {
    Watch("find", &g_find, sizeof(g_find));
    for (int i = 0; i < 200; i++) {
        g_find.tag = RandomTag();
        g_find.index = int(Next() % TypeCount());
        SideBySide("FindSystem", i, RunFind);
    }
    Unwatch();
}

// ---- the systems

struct SystemWork {
    RParticleSystem system;
    RParticleEmitter emitter;
    uint8_t emitterTail[0x40];
    RType type;
    float unknown34;
    uint32_t tag;
    int ticks;
    int op;
    bool initData;
    RParticleSystem created;
} g_system;

enum SystemOp { kOpConstruct, kOpSetSystem, kOpUpdate, kOpDelete, kOpCreate, kSystemOps };

void RunSystem(bool original) {
    SystemWork *w = &g_system;
    switch (w->op) {
    case kOpConstruct:
        if (original)
            Orig_SystemConstruct(&w->system, 0, w->unknown34, &w->emitter, w->initData);
        else
            w->system.Construct(w->unknown34, &w->emitter, w->initData);
        break;
    case kOpSetSystem:
        if (original)
            Orig_SetSystem(&w->system, 0, w->tag);
        else
            w->system.SetSystem(w->tag);
        break;
    case kOpUpdate:
        if (original)
            Orig_SystemUpdate(&w->system, 0, w->ticks);
        else
            w->system.Update(w->ticks);
        break;
    case kOpDelete:
        if (original)
            Orig_SystemDelete(&w->system, 0, 0);
        else
            static_cast<RMovableParticleSystem *>(&w->system)->Delete(0);
        break;
    case kOpCreate: {
        RParticleSystem::RParticleCreator creator = { NULL };
        RParticleSystem *made = original ? Orig_Create(&creator, 0, w->unknown34, &w->emitter)
                                         : creator.Create(w->unknown34, &w->emitter);
        if (made != NULL) {
            w->created = *made;
            UMemory::FastFree(made, sizeof(RParticleSystem));
        }
        break;
    }
    }
}

void TestSystems() {
    Watch("system", &g_system, sizeof(g_system));
    for (int i = 0; i < 1500; i++) {
        SystemWork *w = &g_system;
        memset(w, 0, sizeof(*w));
        w->op = i % kSystemOps;
        w->emitter.position = RandomPoint(100);
        w->emitter.side = RandomPoint(1);
        w->emitter.direction = RandomPoint(1);
        w->emitter.systemTag = RandomTag();
        w->emitter.body = 0xff;
        w->unknown34 = Chance(30) ? 0.0f : Chance(50) ? Uniform(0, 5) : Uniform(0, 2000);
        w->initData = Chance(70);
        w->tag = Chance(30) ? w->emitter.systemTag : RandomTag();
        w->ticks = 1 + int(Next() % 3);
        if (w->op != kOpConstruct && w->op != kOpCreate) {
            // A system made by ours, then perturbed
            w->system.Construct(w->unknown34, &w->emitter, true);
            w->type = *w->system.data;
            if (Chance(50)) {
                w->type.onTime = Next() % 20;
                w->type.offTime = Chance(30) ? 0 : Next() % 20;
                if (Chance(30))
                    w->type.flags ^= kParticleFlag02;
            }
            w->system.data = &w->type;
            w->system.active = Chance(90);
            w->system.systemFlags = uint16_t(Next() & 3);
            w->system.time = Next() % 50;
            w->system.spawns = Uniform(0, 6);
            w->system.nextSpawn = Uniform(0, 6);
            w->system.rate = Uniform(0, 3);
            w->system.rateScale = Chance(50) ? 1.0f : Uniform(0, 2);
            if (Chance(30))
                w->system.velocityPerSecond = &w->emitter.side;
            Coord3 moved = RandomPoint(3);
            w->emitter.position.x += moved.x;
            w->emitter.position.y += moved.y;
            w->emitter.position.z += moved.z;
            if (Chance(10) && w->op != kOpSetSystem)     // SetSystem reads the emitter unchecked
                w->system.emitter = NULL;
        }
        RandomSeed = Next() & 0xffff;
        SideBySide("RParticleSystem", i, RunSystem);
    }
    Unwatch();
}

// ---- CreateParticles

struct CreateWork {
    RType *type;
    Coord3 position;
    Coord3 direction;
    int count;
} g_create;

void RunCreate(bool original) {
    CreateWork *w = &g_create;
    if (original)
        Orig_CreateParticles(fgParticleSystems, 0, w->type, &w->position, &w->direction, w->count);
    else
        fgParticleSystems->CreateParticles(w->type, &w->position, &w->direction, w->count);
}

void TestCreate() {
    Watch("create", &g_create, sizeof(g_create));
    for (int i = 0; i < 300; i++) {
        g_create.type = &Library()->data->systems[Next() % TypeCount()];
        g_create.position = RandomPoint(100);
        g_create.direction = RandomPoint(1);
        switch (i % 4) {
        case 0:     // steep, up or down
            g_create.direction.x = Uniform(-0.05f, 0.05f);
            g_create.direction.z = Uniform(-0.05f, 0.05f);
            break;
        case 1:
            g_create.direction.x = 0.0f;
            g_create.direction.z = 0.0f;
            g_create.direction.y = Chance(50) ? 1.0f : -1.0f;
            break;
        }
        g_create.count = int(Next() % 30);
        RandomSeed = Next() & 0xffff;
        SideBySide("CreateParticles", i, RunCreate);
    }
    Unwatch();
}

// ---- the cache

RParticleParticleCache g_cache;
struct CacheResult {
    int op;
    int block;
    int32_t spawned;        // Spawn's answer, as an offset into the cache; -1 for NULL
} g_cacheResult;

enum CacheOp { kOpSpawn, kOpJustUpdate, kOpRenderBlock, kOpUpdateAndRender, kCacheOps };

void RunCache(bool original) {
    CacheResult *r = &g_cacheResult;
    switch (r->op) {
    case kOpSpawn: {
        RParticle *p = original ? Orig_Spawn(&g_cache, 0) : g_cache.Spawn();
        r->spawned = p == NULL ? -1 : int32_t(reinterpret_cast<uint8_t *>(p) - reinterpret_cast<uint8_t *>(&g_cache));
        break;
    }
    case kOpJustUpdate:
        if (original)
            Orig_JustUpdate(&g_cache, 0);
        else
            g_cache.JustUpdate();
        break;
    case kOpRenderBlock:
        if (original)
            Orig_RenderBlock(&g_cache, 0, r->block);
        else
            g_cache.RenderBlock(r->block);
        break;
    case kOpUpdateAndRender:
        if (original)
            Orig_UpdateAndRender(&g_cache, 0);
        else
            g_cache.UpdateAndRender();
        break;
    }
}

RParticle RandomParticle(int bodySlot) {
    RParticle p;
    p.position = RandomPoint(100);
    p.life = uint8_t(1 + Next() % 60);
    p.system = uint8_t(Next() % TypeCount());
    p.flags = uint8_t(Library()->data->systems[p.system].flags);
    if (Chance(30))
        p.flags ^= uint8_t(1 << (Next() % 8));
    p.gravity = int8_t(Next());
    p.velocity = RandomPoint(2);
    p.alphaSize = uint8_t(Next());
    p.unknown1D = uint8_t(Next());
    p.body = 0xff;
    if ((Library()->data->systems[p.system].flags & kParticleStreak) && bodySlot >= 0 && Chance(20))
        p.body = uint8_t(bodySlot);
    p.spin = int8_t(Next());
    return p;
}

void TestCache(bool canDraw, bool haveCamera) {
    RLibData *types = Library()->data;
    int bodySlot = -1;
    PhysicsObject *player = PTR_AT(0x00234e40) != NULL ? *static_cast<PhysicsObject **>(PTR_AT(0x00234e40)) : NULL;
    if (player != NULL)
        bodySlot = player->rigidBodySlot;
    Watch("cache", &g_cache, sizeof(g_cache));
    Watch("cache result", &g_cacheResult, sizeof(g_cacheResult));
    for (int i = 0; i < 240; i++) {
        int op = i % kCacheOps;
        if ((!canDraw || !haveCamera) && (op == kOpRenderBlock || op == kOpUpdateAndRender))
            continue;
        // the live types' drawing flags and sizes perturbed (the base snapshot puts them back)
        for (uint32_t t = 0; t < types->count; t++) {
            if (!Chance(30))
                continue;
            RType *type = &types->systems[t];
            type->flags ^= kFlagBits[Next() % 8];
            if (!g_haveView)
                type->flags &= ~kParticleSpinFixed;
            type->size = Uniform(0, 4);
            type->sizeChange = Uniform(-3, 3);
        }
        memset(&g_cache, 0, sizeof(g_cache));
        g_cache.unknown00 = -1;
        g_cache.buffer = int32_t(Next() & 1);
        g_cache.count = op == kOpSpawn ? int32_t(Next() % (kParticleBufferSize + 1)) : int32_t(Next() % 1100) % 1025;
        g_cache.spawned = int32_t(Next() % (kParticleBufferSize - g_cache.count + 1));
        g_cache.block = -1;
        g_cache.lastTick = U32_AT(0x001f2a4c) - Next() % 6;
        for (int b = 0; b < 2; b++)
            for (int k = 0; k < kParticleBufferSize; k++)
                g_cache.particles[b][k] = RandomParticle(bodySlot);
        g_cacheResult.op = op;
        g_cacheResult.block = int(Next() % 3);
        g_cacheResult.spawned = 0;
        if (op == kOpRenderBlock)
            g_cache.block = g_cacheResult.block;
        if (op == kOpJustUpdate || op == kOpSpawn) {
            // Spawn and the update keep count + spawned within a buffer
            if (g_cache.count + g_cache.spawned > kParticleBufferSize)
                g_cache.spawned = kParticleBufferSize - g_cache.count;
        }
        if (op == kOpUpdateAndRender)
            g_cache.spawned = 0;    // the frames' updates start from an empty next buffer
        memset(&g_drawLog, 0, sizeof(g_drawLog));
        SideBySide("cache", i, RunCache);
    }
    Unwatch();
    Unwatch();
}

// ---- the library and its maps, on scratch copies

RLibData g_libraryA, g_libraryB;

// The map's nodes in order, each with its colour and its neighbours' keys, values as offsets from `base`
size_t MapShape(const ParticleMap *map, const void *base, uint32_t *out, size_t room) {
    size_t n = 0;
    out[n++] = map->size;
    for (TreeNode *node = map->Begin(); node != map->head && n + 6 <= room; node = TreeNext(node)) {
        out[n++] = node->value.tag;
        out[n++] = uint32_t(reinterpret_cast<uintptr_t>(node->value.group) - reinterpret_cast<uintptr_t>(base));
        out[n++] = node->color;
        out[n++] = node->left->isNil ? 0xffffffffu : node->left->value.tag;
        out[n++] = node->right->isNil ? 0xffffffffu : node->right->value.tag;
        out[n++] = node->parent == map->head ? 0xfffffffeu : node->parent->value.tag;
    }
    return n;
}

uint32_t g_shapeA[4096], g_shapeB[4096];

void CompareMaps(const char *what, int index, const ParticleMap *a, const void *baseA, const ParticleMap *b,
                 const void *baseB) {
    size_t na = MapShape(a, baseA, g_shapeA, 4096);
    size_t nb = MapShape(b, baseB, g_shapeB, 4096);
    if (na != nb) {
        g_checks++;
        Differ(what, index, "the maps' sizes differ");
        return;
    }
    CheckBytes(what, index, g_shapeA, g_shapeB, na * sizeof(uint32_t));
}

TreeNode *NodeAt(ParticleMap *map, uint32_t steps) {
    TreeNode *node = map->Begin();
    while (steps-- > 0 && node != map->head)
        node = TreeNext(node);
    return node;
}

void TestMaps() {
    for (int round = 0; round < 6; round++) {
        ParticleMap a, b;
        a.allocator = b.allocator = 0;
        a.Init();
        b.Init();
        int inserts = 1 + int(Next() % 300);
        uint32_t keyRange = round & 1 ? 64 : 0x7fffffff;
        for (int i = 0; i < inserts; i++) {
            uint32_t key = Next() % keyRange;
            TreePair value;
            value.tag = key;
            value.system = reinterpret_cast<RParticleSystem *>(uintptr_t(key * 7));
            TreeInsertResult ra, rb;
            OriginalWindow(true);
            Orig_InsertUnique(&a, 0, &ra, &value);
            OriginalWindow(false);
            b.InsertUnique(&rb, &value);
            g_checks++;
            if (ra.inserted != rb.inserted || ra.where->value.tag != rb.where->value.tag)
                Differ("map insert", i, "the answers differ");
        }
        g_cases++;
        CompareMaps("map after inserts", round, &a, NULL, &b, NULL);
        for (int i = 0; i < 40 && a.size > 0; i++) {
            uint32_t at = Next() % a.size;
            TreeNode *ra, *rb;
            OriginalWindow(true);
            Orig_EraseAt(&a, 0, &ra, NodeAt(&a, at));
            OriginalWindow(false);
            b.EraseAt(&rb, NodeAt(&b, at));
        }
        g_cases++;
        CompareMaps("map after erases", round, &a, NULL, &b, NULL);
        if (a.size > 2) {
            uint32_t first = Next() % (a.size / 2), last = first + Next() % (a.size - first);
            TreeNode *ra, *rb;
            OriginalWindow(true);
            (round & 2 ? Orig_EraseTypeRange : Orig_EraseSystemRange)(&a, 0, &ra, NodeAt(&a, first), NodeAt(&a, last));
            OriginalWindow(false);
            if (round & 2)
                b.EraseTypeRange(&rb, NodeAt(&b, first), NodeAt(&b, last));
            else
                b.EraseSystemRange(&rb, NodeAt(&b, first), NodeAt(&b, last));
            g_cases++;
            CompareMaps("map after a range erase", round, &a, NULL, &b, NULL);
        }
        // the whole map: the subtree erase
        TreeNode *ra, *rb;
        OriginalWindow(true);
        (round & 2 ? Orig_EraseTypeRange : Orig_EraseSystemRange)(&a, 0, &ra, a.Begin(), a.head);
        OriginalWindow(false);
        if (round & 2)
            b.EraseTypeRange(&rb, b.Begin(), b.head);
        else
            b.EraseSystemRange(&rb, b.Begin(), b.head);
        g_cases++;
        CompareMaps("map after clearing", round, &a, NULL, &b, NULL);
        a.Destroy();
        b.Destroy();
    }
}

void CompareLibraries(const char *what, int index) {
    CheckBytes(what, index, g_libraryA.systems, g_libraryB.systems, sizeof(g_libraryA.systems));
    CheckBytes(what, index, &g_libraryA.count, &g_libraryB.count, sizeof(g_libraryA.count));
    CompareMaps(what, index, &g_libraryA.types, &g_libraryA, &g_libraryB.types, &g_libraryB);
}

void TestLibrary() {
    memset(&g_libraryA, 0, sizeof(g_libraryA));
    memset(&g_libraryB, 0, sizeof(g_libraryB));
    OriginalWindow(true);
    Orig_LibDataConstruct(&g_libraryA, 0);
    OriginalWindow(false);
    g_libraryB.Construct();
    g_cases++;
    CompareLibraries("library construct", 0);

    RParticleLibrary libraryA = { &g_libraryA, 0 }, libraryB = { &g_libraryB, 0 };
    for (int i = 0; i < 270; i++) {
        RType source = RandomType(false);
        RType copy = source;
        uint32_t tag = Chance(10) && g_libraryA.count > 0 ? g_libraryA.types.Begin()->value.tag : Next();
        bool whole = Chance(50) && g_libraryA.count < 256;   // a full library's AddSystem munges systems[-1]
        OriginalWindow(true);
        int ia = whole ? Orig_LibraryAddSystem(&libraryA, 0, &source, tag) : Orig_LibDataAddSystem(&g_libraryA, 0, &source, tag);
        OriginalWindow(false);
        int ib = whole ? libraryB.AddSystem(&copy, tag) : g_libraryB.AddSystem(&copy, tag);
        g_cases++;
        g_checks++;
        if (ia != ib)
            Differ("library AddSystem's answer", i, "differs");
        CheckBytes("library AddSystem's source", i, &source, &copy, sizeof(source));
    }
    CompareLibraries("library after AddSystem", 0);

    OriginalWindow(true);
    Orig_LibraryReload(&libraryA, 0);
    OriginalWindow(false);
    libraryB.Reload();
    g_cases++;
    CompareLibraries("library after Reload", 0);

    OriginalWindow(true);
    Orig_LibDataDestruct(&g_libraryA, 0);
    OriginalWindow(false);
    g_libraryB.Destruct();
    g_cases++;
    g_checks++;
    if (g_libraryA.types.head != g_libraryB.types.head || g_libraryA.types.size != g_libraryB.types.size)
        Differ("library destruct", 0, "the maps differ");
}

// ---- the particulate

RParticulate g_particulate;
int g_particulateOp;

void RunParticulate(bool original) {
    switch (g_particulateOp) {
    case 0:
        if (original)
            Orig_ParticulateUpdate(&g_particulate, 0);
        else
            g_particulate.Update();
        break;
    case 1:
        if (original)
            Orig_ParticulateInit(&g_particulate, 0);
        else
            g_particulate._init();
        break;
    case 2:
        if (original)
            Orig_ParticulateDraw(&g_particulate, 0);
        else
            g_particulate.Draw();
        break;
    }
}

void TestParticulate(bool canDraw, bool haveCamera) {
    if (fgParticulate == NULL) {
        printf("[particles] no particulate - its cases skipped\n");
        return;
    }
    Watch("particulate", &g_particulate, sizeof(g_particulate));
    Watch("particulate op", &g_particulateOp, sizeof(g_particulateOp));
    const Coord4 eye = fgRenderer->cameraPosition;
    for (int i = 0; i < 90; i++) {
        g_particulateOp = i % 3;
        if ((g_particulateOp == 2 && !canDraw) || (g_particulateOp == 0 && !haveCamera))   // Update reads the view's camera
            continue;
        memcpy(&g_particulate, fgParticulate, sizeof(g_particulate));
        g_particulate.enabled = !Chance(10);
        if (Chance(50)) {
            g_particulate.range = Uniform(5, 100);
            g_particulate.alpha = Uniform(0, 400);
            g_particulate.driftAmplitude = Chance(30) ? 0.0f : Uniform(0, 0.5f);
            g_particulate.shadeAngle = Chance(30) ? 0.0f : Uniform(0, 1.5f);
            g_particulate.shadeStrength = Uniform(0, 2);
            g_particulate.colour = { Uniform(0, 1.2f), Uniform(0, 1.2f), Uniform(0, 1.2f), 0.0f };
        }
        g_particulate.fullBatches = 0;
        for (int k = 0; k < kParticulateCount; k++) {
            Coord3 p = RandomPoint(g_particulate.range * 2.5f);
            g_particulate.positions[k] = { eye.x + p.x, eye.y + p.y, eye.z + p.z, 1.0f };
        }
        RandomSeed = Next() & 0xffff;
        memset(&g_drawLog, 0, sizeof(g_drawLog));
        SideBySide("RParticulate", i, RunParticulate);
    }
    Unwatch();
    Unwatch();
}

}  // namespace

void ParticleShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_PARTICLESHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    if (fgParticleSystems == NULL || fgParticleSystems->library.data == NULL ||
        fgParticleSystems->library.data->count == 0 || fgRenderer == NULL) {
        printf("[particles] no particle manager, library or renderer - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    EAGL::RenderContext *context = fgRenderer->renderContext;
    g_haveView = context != NULL && context->GetCurrentViewPort() != NULL;
    uint8_t zWrites = context != NULL ? context->zWritesEnable : 1;

    // Between frames the renderer has no current view (EndView clears it); the drawing and the particulate read
    // its camera, so the first player's view stands in while the cases run.
    RViewCamera *savedView = fgRenderer->currentView;
    if (savedView == NULL && fgRenderHigh != NULL && fgRenderHigh->viewCount != 0)
        fgRenderer->currentView = fgRenderHigh->views[0].view;
    bool haveCamera = fgRenderer->currentView != NULL && fgRenderer->currentView->camera != NULL;
    if (!haveCamera)
        printf("[particles] no view camera - the cases that read it skipped\n");

    // The draws pointed at the recorders while the cases run
    const void *volatileDraw = JumpTarget(kVolatileDraw);
    const void *simpleDraw = JumpTarget(kSimpleDraw);
    bool canDraw = volatileDraw != NULL && simpleDraw != NULL && context != NULL &&
                   VolatileRequests[VolatileRequestIndex] != NULL && SimpleRequests[SimpleRequestIndex] != NULL &&
                   XbeOriginal_Redirect(kVolatileDraw, (const void *)&RecordVolatileDraw) &&
                   XbeOriginal_Redirect(kSimpleDraw, (const void *)&RecordSimpleDraw);
    if (canDraw) {
        JumpEntry(&g_volatileEntry, volatileDraw, (const void *)&RecordVolatileDraw);
        JumpEntry(&g_simpleEntry, simpleDraw, (const void *)&RecordSimpleDraw);
    }
    if (!canDraw)
        printf("[particles] the materials' Draw is not ours yet - the drawing cases skipped\n");

    // The memory every case watches
    Watch("random state", &RandomSeed, 4);
    Watch("fov scale", &ParticleFovScale, 4);
    Watch("vertex arrays", VertexArrays, kVertexBytes);
    Watch("live cache", &fgParticleSystems->cache, sizeof(RParticleParticleCache));
    Watch("live types", fgParticleSystems->library.data, sizeof(RLibData));
    Watch("draws", &g_drawLog, sizeof(g_drawLog));
    Watch("point states", &D3DRenderState[kRsPointSize], (kRsPointScaleA - kRsPointSize + 1) * 4);
    Watch("dirty flags", &D3DDirtyFlags, 4);
    if (canDraw) {
        Watch("volatile request", VolatileRequests[VolatileRequestIndex], sizeof(TexturedGeoPrim));
        Watch("simple request", SimpleRequests[SimpleRequestIndex], sizeof(SimpleGeoPrim));
    }
    g_fixedRegions = g_regionCount;
    SaveAll(0);
    uint8_t *base[kMaxRegions];
    for (int i = 0; i < g_regionCount; i++) {
        base[i] = static_cast<uint8_t *>(malloc(g_regions[i].size));
        memcpy(base[i], g_regions[i].at, g_regions[i].size);
    }

    TestReset();
    TestMunge();
    TestFind();
    TestSystems();
    TestCreate();
    TestCache(canDraw, haveCamera);
    TestMaps();
    TestLibrary();
    TestParticulate(canDraw, haveCamera);

    for (int i = 0; i < g_fixedRegions; i++) {
        memcpy(g_regions[i].at, base[i], g_regions[i].size);
        free(base[i]);
    }
    g_fixedRegions = 0;
    Unwatch();
    RestoreEntry(&g_volatileEntry);
    RestoreEntry(&g_simpleEntry);
    if (canDraw) {
        XbeOriginal_Redirect(kVolatileDraw, volatileDraw);
        XbeOriginal_Redirect(kSimpleDraw, simpleDraw);
    }
    if (context != NULL)
        context->SetZWritesEnable(zWrites);
    fgRenderer->currentView = savedView;
    ResetFpu();
    printf("[particles] particles, systems, cache, library, particulate vs originals: %d cases, %d checks, %d differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[particles]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
