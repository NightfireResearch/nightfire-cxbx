#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameEffectsShadowD.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <vector>

#include "../camera/Camera.h"
#include "../camera/PlayerCamera.h"
#include "../eagl/Model.h"
#include "../eagl/RenderContext.h"
#include "../eagl/RenderMethod.h"
#include "../eagl/Tar.h"
#include "../engine/MissionManager.h"
#include "../engine/UMemory.hpp"
#include "../game/WeaponCameras.h"
#include "../game/WeaponEffects.h"
#include "../render/Decals.h"
#include "../render/Draw.h"
#include "../render/Materials.h"
#include "../render/OffscreenBuffer.h"
#include "../render/RenderHigh.h"
#include "../render/Renderer.h"
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.effects' package D, run once from the first simulation tick when NIGHTFIRE_GAMEEFFECTSSHADOWD
// is set. Each case runs the ORIGINAL (every entry of the package swapped back together) and then the PORT from the
// same state, and compares what they leave - the globals both use at the game's addresses, the game's copies of the
// globals we own (the original's side) against ours, the objects - and a log of every draw, decal and debug variable
// call they make:
//
//   - RBulletStreak::Add, both overloads, into batches at random fill levels (some full), random positions and
//     directions, perturbed kinds' widths, lengths and colours; Draw over random counts; Init (the tables written
//     over with garbage first, the debug variable calls logged);
//   - RMissileStreak::Update (both) and Draw on synthetic streaks (no missile flies at the first tick): random counts,
//     ticks and types, a synthetic camera view (random right axis), perturbed widths and jitter, the 16-bit random
//     generator from random seeds; Init and Kill (the new GeoPrims compared by value, then freed);
//   - RMuzzleFlash::Draw and DrawPOV with random transforms, positions, directions, types and sizes, paused or not;
//     Init (the POV tables written over first; the model's turn compared);
//   - RScorchWorld::ScorchWorld and ScorchGround at random points round the first player's camera on the live
//     world (walls, floors, random normals, sizes about the 0.33 and 6 limits, every type), from fresh and searched
//     WWorldPos images;
//   - RMissileCam: Draw on a copy of the live one (on, off, the mission manager's +0x4f0, every texture step);
//     Construct, Delete(0) and Delete(1) on blocks of its own, Kill;
//   - RSniperZoom: SetupVerts, Draw, GrabBackBuffer and Reset on a copy of the live one (perturbed radii, greys,
//     alpha, aim point); Construct and Delete; Kill;
//   - FUN_000a68f0 and FUN_000a6cd0 through their register entries (the port's naked adaptors at the same address).
//
// Fakes (5-byte jumps over our implementations, which both sides reach): UVolatileMaterial::Draw,
// USimpleTexturedMaterial::Draw and DynamicModel::Draw (log the request's or GeoPrim's vertices by value),
// RenderContext::SetZWritesEnable, RDecalManager::AddDecal (the vectors' x, y, z: the original's centre and axis w
// are uninitialised stack), ROffscreenBuffer's Construct, Destruct and MakeTexCopy, and dbindex, dbattrib_argb,
// dbattrib_s8, dbattrib_float, dbendindex. Our globals are lent to the game's copies before each original run.
//
// A mutation the test sees: RBulletStreak::Init's UV switch with cases 1 and 2 swapped (the batches' texture
// coordinates differ); RMissileStreak::Update's half-width product rounded to float (the pairs' x, y, z differ in
// their low bits).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x00099e20, 0x00099f10, 0x0009a1d0, 0x0009a3b0, 0x000a0a50, 0x000a0b30, 0x000a0b50, 0x000a0df0, 0x000a0e70,
    0x000a0e90, 0x000a0ea0, 0x000a1070, 0x000a1330, 0x000a1460, 0x000a14b0, 0x000a1870, 0x000a19d0, 0x000a1b50,
    0x000a50c0, 0x000a5530, 0x000a6810, 0x000a6870, 0x000a6890, 0x000a68a0, 0x000a68f0, 0x000a69a0, 0x000a6a50,
    0x000a6a80, 0x000a6bc0, 0x000a6cd0,
};

#define Orig_BulletDraw ((void (*)(void))0x00099e20)
#define Orig_BulletInit ((void (*)(void))0x00099f10)
#define Orig_BulletAdd4 ((void (*)(const Coord4 *, const Coord4 *, int))0x0009a1d0)
#define Orig_BulletAdd3 ((void (*)(const Coord3 *, const Coord3 *, int))0x0009a3b0)
#define Orig_CamConstruct ((RMissileCam *(__fastcall *)(void *, int))0x000a0a50)
#define Orig_CamKill ((void (*)(void))0x000a0b30)
#define Orig_CamDraw ((void (__fastcall *)(void *, int))0x000a0b50)
#define Orig_CamDelete ((void *(__fastcall *)(void *, int, unsigned))0x000a0e70)
#define Orig_StreakTick ((void (__fastcall *)(void *, int))0x000a0e90)
#define Orig_StreakUpdate ((void (__fastcall *)(void *, int, const Coord4 *))0x000a0ea0)
#define Orig_StreakInit ((void (*)(void))0x000a1070)
#define Orig_StreakDraw ((void (__fastcall *)(void *, int))0x000a1330)
#define Orig_StreakKill ((void (*)(void))0x000a1460)
#define Orig_FlashDraw ((void (*)(const MATRIX4 *, const Coord4 *, const Coord4 *, int, float))0x000a1870)
#define Orig_FlashDrawPOV ((void (*)(const MATRIX4 *, const Coord4 *, int, float))0x000a19d0)
#define Orig_FlashInit ((void (*)(void))0x000a1b50)
#define Orig_ScorchWorld ((void (*)(WWorldPos *, const Coord4 *, const Coord4 *, float, int))0x000a50c0)
#define Orig_ScorchGround ((void (*)(WWorldPos *, const Coord4 *, float, int))0x000a5530)
#define Orig_ZoomKill ((void (*)(void))0x000a6870)
#define Orig_ZoomReset ((void (__fastcall *)(void *, int))0x000a6890)
#define Orig_ZoomGrab ((void (__fastcall *)(void *, int))0x000a68a0)
#define Orig_ZoomConstruct ((RSniperZoom *(__fastcall *)(void *, int))0x000a69a0)
#define Orig_ZoomDelete ((void *(__fastcall *)(void *, int, unsigned))0x000a6a50)
#define Orig_ZoomSetupVerts ((void (__fastcall *)(void *, int))0x000a6a80)
#define Orig_ZoomDraw ((void (__fastcall *)(void *, int))0x000a6bc0)
const uint32_t kRingPoint = 0x000a68f0;
const uint32_t kParticleRect = 0x000a6cd0;

// The callees faked (their entries jump to ours)
const uint32_t kVolatileDraw = 0x0011c290;
const uint32_t kTexturedDraw = 0x0011bd50;
const uint32_t kModelDraw = 0x000e99e0;
const uint32_t kZWrites = 0x000e73f0;
const uint32_t kAddDecal = 0x0009b070;
const uint32_t kOffscreenConstruct = 0x0007f610;
const uint32_t kOffscreenDestruct = 0x0007f5a0;
const uint32_t kMakeTexCopy = 0x0007f870;
const uint32_t kDbIndex = 0x00038190;
const uint32_t kDbArgb = 0x0003d530;
const uint32_t kDbS8 = 0x00039ff0;
const uint32_t kDbFloat = 0x0003a1f0;
const uint32_t kDbEnd = 0x00037ad0;

// ---- the game's state

#define ShadowRandomSeed U32_AT(0x001c45c4)
#define ShadowRandomMultiplier U32_AT(0x001c45c8)
#define ShadowSimState I32_AT(0x00234e24)
#define ShadowSimStepCount I32_AT(0x00234e34)
#define ShadowVehicleParticleTexture3 U32_AT(0x001c525c)

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameeffectsshadowd]   %s #%ld: %s\n", what, index, detail);
}

void CompareBytes(const char *what, long index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[112];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Report(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x6e5d0a17;

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

bool Chance(int percent) {
    return Range(0, 99) < percent;
}

Coord4 RandomUnit(float w) {
    for (;;) {
        Coord4 v = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), w };
        float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length > 0.1f) {
            v.x /= length;
            v.y /= length;
            v.z /= length;
            return v;
        }
    }
}

// ---- the regions a case may change: the game's addresses both sides use, and our globals with their game copies

struct Region {
    uint8_t *game;
    uint8_t *ours;          // NULL: both sides use the game's address
    uint32_t size;
    bool compare;
    bool geoPrim;           // a TexturedGeoPrim: its parameters' data pointers compared as game addresses
    const char *name;
};

std::vector<Region> g_regions;

void AddShared(uint32_t game, uint32_t size, const char *name, bool compare = true, bool geoPrim = false) {
    if (game != 0)
        g_regions.push_back({ reinterpret_cast<uint8_t *>(uintptr_t(game)), NULL, size, compare, geoPrim, name });
}

void AddOwned(void *ours, uint32_t game, uint32_t size, const char *name) {
    g_regions.push_back({ reinterpret_cast<uint8_t *>(uintptr_t(game)), static_cast<uint8_t *>(ours), size, true,
                          false, name });
}

// An address in one of our globals as the same byte of the game's copy
uint32_t Canon(const void *pointer) {
    const uint8_t *p = static_cast<const uint8_t *>(pointer);
    for (const Region &r : g_regions)
        if (r.ours != NULL && p >= r.ours && p < r.ours + r.size)
            return uint32_t(uintptr_t(r.game + (p - r.ours)));
    return uint32_t(uintptr_t(pointer));
}

struct Snapshot {
    std::vector<uint8_t> bytes;
};

void Capture(Snapshot *s) {
    s->bytes.clear();
    for (const Region &r : g_regions) {
        s->bytes.insert(s->bytes.end(), r.game, r.game + r.size);
        if (r.ours != NULL)
            s->bytes.insert(s->bytes.end(), r.ours, r.ours + r.size);
    }
}

void Restore(const Snapshot &s) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        memcpy(r.game, &s.bytes[at], r.size);
        at += r.size;
        if (r.ours != NULL) {
            memcpy(r.ours, &s.bytes[at], r.size);
            at += r.size;
        }
    }
}

// Our globals into the game's copies, so an original starts where the port does
void Lend() {
    for (const Region &r : g_regions)
        if (r.ours != NULL)
            memcpy(r.game, r.ours, r.size);
}

void CanonGeoPrim(uint8_t *bytes) {
    for (uint32_t offset = 0x08; offset < sizeof(TexturedGeoPrim); offset += 8) {
        uint32_t word;
        memcpy(&word, bytes + offset, 4);
        word = Canon(reinterpret_cast<const void *>(uintptr_t(word)));
        memcpy(bytes + offset, &word, 4);
    }
}

// The original's result (the game's copies) against the port's (ours)
void CompareResults(const char *what, long index, const Snapshot &original, const Snapshot &port) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        const uint8_t *a = &original.bytes[at];
        const uint8_t *b = r.ours != NULL ? &port.bytes[at + r.size] : &port.bytes[at];
        at += r.ours != NULL ? 2 * r.size : r.size;
        if (!r.compare)
            continue;
        char label[96];
        snprintf(label, sizeof(label), "%s: %s", what, r.name);
        if (r.geoPrim && r.size == sizeof(TexturedGeoPrim)) {
            uint8_t x[sizeof(TexturedGeoPrim)], y[sizeof(TexturedGeoPrim)];
            memcpy(x, a, sizeof(x));
            memcpy(y, b, sizeof(y));
            CanonGeoPrim(x);
            CanonGeoPrim(y);
            CompareBytes(label, index, x, y, sizeof(x));
        } else {
            CompareBytes(label, index, a, b, r.size);
        }
    }
}

// ---- the call logs

std::vector<uint32_t> g_logs[2];

void Log(uint32_t word) {
    g_logs[g_original ? 0 : 1].push_back(word);
}

void LogFloat(float value) {
    uint32_t word;
    memcpy(&word, &value, 4);
    Log(word);
}

void LogBytes(const void *bytes, uint32_t size) {
    if (bytes == NULL) {
        Log(0xdeadbeef);
        return;
    }
    const uint8_t *p = static_cast<const uint8_t *>(bytes);
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word = 0;
        memcpy(&word, p + i, size - i < 4 ? size - i : 4);
        Log(word);
    }
}

void LogXyz(const Coord4 *v) {
    if (v == NULL) {
        Log(0xdeadbeef);
        return;
    }
    LogFloat(v->x);
    LogFloat(v->y);
    LogFloat(v->z);
}

void LogString(const char *text) {
    if (text == NULL) {
        Log(0);
        return;
    }
    uint32_t hash = 2166136261u;
    for (const char *c = text; *c != '\0'; c++)
        hash = (hash ^ uint8_t(*c)) * 16777619u;
    Log(hash);
}

void CompareLogs(const char *what, long index) {
    g_checks++;
    const std::vector<uint32_t> &a = g_logs[0], &b = g_logs[1];
    if (a == b)
        return;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        at++;
    char detail[128];
    snprintf(detail, sizeof(detail), "call log word %u of %u/%u: original %08x, port %08x", unsigned(at),
             unsigned(a.size()), unsigned(b.size()), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
    Report(what, index, detail);
}

// ---- the fakes

enum : uint32_t {
    kTagVolatile = 0x56445257,
    kTagTextured = 0x54445257,
    kTagModel = 0x4d445257,
    kTagZWrites = 0x5a575254,
    kTagDecal = 0x4443414c,
    kTagTexCopy = 0x54584350,
    kTagOffConstruct = 0x4f464643,
    kTagOffDestruct = 0x4f464644,
    kTagDb = 0x44424147,
};

const uint32_t kMaxLoggedVertices = 256;

void LogRequest(const TexturedGeoPrim *request, uint32_t count, uint32_t uvBytes) {
    if (request == NULL) {
        Log(0);
        return;
    }
    if (count > kMaxLoggedVertices)
        count = kMaxLoggedVertices;
    Log(uint32_t(uintptr_t(request->texture.data)));
    LogBytes(request->positions.data, count * sizeof(Coord4));
    LogBytes(request->texCoords.data, count * uvBytes);
    LogBytes(request->colours.data, count * 4);
}

void __fastcall FakeVolatileDraw(UVolatileMaterial *material, int, int primitive, int count, MATRIX4 *transform) {
    Log(kTagVolatile);
    Log(uint32_t(uintptr_t(material)));
    Log(uint32_t(primitive));
    Log(uint32_t(count));
    LogBytes(transform, transform != NULL ? sizeof(MATRIX4) : 0);
    LogRequest(VolatileRequests[VolatileRequestIndex], uint32_t(count), sizeof(EffectUV));
}

void __fastcall FakeTexturedDraw(USimpleTexturedMaterial *material, int, int primitive, int count,
                                 MATRIX4 *transform) {
    Log(kTagTextured);
    Log(Canon(material));
    Log(uint32_t(primitive));
    Log(uint32_t(count));
    LogBytes(transform, transform != NULL ? sizeof(MATRIX4) : 0);
    LogRequest(TexturedRequests[TexturedRequestIndex], uint32_t(count), sizeof(Coord4));
}

void LogGeoPrim(const TexturedGeoPrim *geoPrim) {
    if (geoPrim == NULL) {
        Log(0);
        return;
    }
    Log(geoPrim->method != NULL);
    Log(geoPrim->state.count);
    Log(Canon(geoPrim->state.data));
    LogBytes(geoPrim->state.data, geoPrim->state.data != NULL ? sizeof(EAGL::GeoPrimState) : 0);
    Log(geoPrim->texture.count);
    Log(uint32_t(uintptr_t(geoPrim->texture.data)));
    Log(geoPrim->vertexCount.count);
    uint32_t vertices = 0;
    if (geoPrim->vertexCount.data != NULL)
        memcpy(&vertices, geoPrim->vertexCount.data, 4);
    Log(vertices);
    if (vertices > kMaxLoggedVertices)
        vertices = kMaxLoggedVertices;
    Log(geoPrim->modelViewProjection.count);
    Log(uint32_t(uintptr_t(geoPrim->modelViewProjection.data)));
    Log(geoPrim->positions.count);
    LogBytes(geoPrim->positions.data, vertices * sizeof(Coord4));
    Log(geoPrim->colours.count);
    LogBytes(geoPrim->colours.data, vertices * 4);
    Log(geoPrim->texCoords.count);
    LogBytes(geoPrim->texCoords.data, vertices * sizeof(Coord4));
}

void __fastcall FakeModelDraw(EAGL::DynamicModel *model, int) {
    Log(kTagModel);
    Log(uint32_t(model->count));
    for (int i = 0; i < model->count && i < 8; i++)
        LogGeoPrim(reinterpret_cast<const TexturedGeoPrim *>(model->geoPrims[i]));
}

bool __fastcall FakeZWrites(EAGL::RenderContext *context, int, uint8_t enable) {
    Log(kTagZWrites);
    Log(uint32_t(uintptr_t(context)));
    Log(enable);
    return true;
}

void __fastcall FakeAddDecal(RDecalManager *manager, int, const Coord4 *centre, const Coord4 *axis,
                             const Coord4 *normal, int type, float width, float height) {
    Log(kTagDecal);
    Log(uint32_t(uintptr_t(manager)));
    LogXyz(centre);
    LogXyz(axis);
    LogXyz(normal);
    Log(uint32_t(type));
    LogFloat(width);
    LogFloat(height);
}

void __fastcall FakeMakeTexCopy(ROffscreenBuffer *buffer, int, EAGL::TAR *source, int copies, float spread) {
    Log(kTagTexCopy);
    Log(uint32_t(uintptr_t(buffer)));
    Log(uint32_t(uintptr_t(source)));
    Log(uint32_t(copies));
    LogFloat(spread);
}

ROffscreenBuffer *__fastcall FakeOffscreenConstruct(ROffscreenBuffer *buffer, int, int width, int height, int depth,
                                                    bool withDepth, bool asTexture) {
    Log(kTagOffConstruct);
    Log(uint32_t(width));
    Log(uint32_t(height));
    Log(uint32_t(depth));
    Log(withDepth);
    Log(asTexture);
    memset(buffer, 0, sizeof(ROffscreenBuffer));
    buffer->width = width;
    buffer->height = height;
    buffer->bitDepth = depth;
    return buffer;
}

void __fastcall FakeOffscreenDestruct(ROffscreenBuffer *buffer, int) {
    Log(kTagOffDestruct);
    Log(uint32_t(buffer->width));
}

void FakeDbIndex(const char *name, uint32_t *data, uint32_t minimum, uint32_t maximum, const char *const *names) {
    Log(kTagDb);
    LogString(name);
    Log(Canon(data));
    Log(minimum);
    Log(maximum);
    for (uint32_t i = minimum; names != NULL && i <= maximum && i < 16; i++)
        LogString(names[i]);
}

void FakeDbArgb(const char *name, uint32_t *data, uint8_t *unused, uint32_t stride) {
    Log(kTagDb + 1);
    LogString(name);
    Log(Canon(data));
    Log(Canon(unused));
    Log(stride);
}

void FakeDbS8(const char *name, int *data, int minimum, int maximum, uint32_t stride, float step,
              const char *const *names) {
    Log(kTagDb + 2);
    LogString(name);
    Log(Canon(data));
    Log(uint32_t(minimum));
    Log(uint32_t(maximum));
    Log(stride);
    LogFloat(step);
    Log(uint32_t(uintptr_t(names)));
}

void FakeDbFloat(const char *name, float *data, float minimum, float maximum, uint32_t stride, float step,
                 const char *const *names) {
    Log(kTagDb + 3);
    LogString(name);
    Log(Canon(data));
    LogFloat(minimum);
    LogFloat(maximum);
    Log(stride);
    LogFloat(step);
    Log(uint32_t(uintptr_t(names)));
}

void FakeDbEnd() {
    Log(kTagDb + 4);
}

// ---- 5-byte jumps over our implementations

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

// Where a patched entry's jump goes now (our implementation)
uint32_t JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return 0;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return at + 5 + uint32_t(relative);
}

bool HookInstall(Hook *hook, uint32_t entry, const void *to) {
    hook->on = false;
    uint32_t at = JumpTarget(entry);
    if (at == 0)
        return false;
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    for (int i = 0; i < 5; i++)
        if (code[i] == 0xc3 || code[i] == 0xc2 || code[i] == 0xcc)    // too short to take a jump
            return false;
    hook->at = at;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(hook->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t relative = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &relative, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    hook->on = true;
    return true;
}

void HookRemove(Hook *hook) {
    if (!hook->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(hook->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(hook->at), hook->saved, 5);
    VirtualProtect((void *)uintptr_t(hook->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(hook->at), 5);
    hook->on = false;
}

enum HookIndex {
    kHookVolatile, kHookTextured, kHookModel, kHookZWrites, kHookDecal, kHookOffConstruct, kHookOffDestruct,
    kHookTexCopy, kHookDbIndex, kHookDbArgb, kHookDbS8, kHookDbFloat, kHookDbEnd, kHookCount
};
Hook g_hooks[kHookCount];

bool Hooked(std::initializer_list<int> which) {
    for (int i : which)
        if (!g_hooks[i].on)
            return false;
    return true;
}

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    if (original)
        Window(true);
#ifdef _MSC_VER
    __try {
        run();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
    }
#else
    run();
#endif
    if (original)
        Window(false);
    ResetFpu();
}

// The inputs are in place (ours): both sides from them, the results compared, everything put back
void RunCase(const char *what, long index, SideFn run, const Snapshot &before) {
    Lend();
    Snapshot inputs, original, port;
    Capture(&inputs);
    g_logs[0].clear();
    g_logs[1].clear();
    RunSide(run, true);
    Capture(&original);
    Restore(inputs);
    RunSide(run, false);
    Capture(&port);
    g_cases++;
    CompareResults(what, index, original, port);
    CompareLogs(what, index);
    Restore(before);
}

// ---- the synthetic objects

alignas(16) uint8_t g_streakBlock[0x40 + sizeof(RMissileStreak)];    // room before: a first update at an odd tick
RMissileStreak *const g_streak = reinterpret_cast<RMissileStreak *>(g_streakBlock + 0x40);
alignas(16) uint8_t g_camBlock[sizeof(RMissileCam)];
alignas(16) uint8_t g_zoomBlock[sizeof(RSniperZoom)];
RMissileCam *const g_cam = reinterpret_cast<RMissileCam *>(g_camBlock);
RSniperZoom *const g_zoom = reinterpret_cast<RSniperZoom *>(g_zoomBlock);
alignas(16) uint8_t g_viewBlock[sizeof(RViewCamera)];
alignas(16) uint8_t g_cameraBlock[sizeof(RCamera)];
alignas(16) uint8_t g_placeBlock[sizeof(WWorldPos)];
WWorldPos *const g_place = reinterpret_cast<WWorldPos *>(g_placeBlock);

// One case's inputs
struct Inputs {
    Coord4 position, direction, normal;
    Coord3 position3, direction3;
    alignas(16) MATRIX4 transform;
    int kind, type;
    float size;
    bool three, tickOnly;
    int zoomOp;
    uint32_t texture;
    float radius, turns;
    int flags;
} g_in;
alignas(16) Coord4 g_out[2];

// ---- RBulletStreak

void SideBulletAdd() {
    if (g_in.three) {
        if (g_original)
            Orig_BulletAdd3(&g_in.position3, &g_in.direction3, g_in.kind);
        else
            RBulletStreak::Add(&g_in.position3, &g_in.direction3, g_in.kind);
    } else {
        if (g_original)
            Orig_BulletAdd4(&g_in.position, &g_in.direction, g_in.kind);
        else
            RBulletStreak::Add(&g_in.position, &g_in.direction, g_in.kind);
    }
}

void SideBulletDraw() {
    if (g_original)
        Orig_BulletDraw();
    else
        RBulletStreak::Draw();
}

void SideBulletInit() {
    if (g_original)
        Orig_BulletInit();
    else
        RBulletStreak::Init();
}

void TestBulletStreaks(const Snapshot &before) {
    if (fgRenderHigh == NULL || fgRenderHigh->views[0].camera == NULL)
        return;
    const Coord4 *eye = MatrixRow(&fgRenderHigh->views[0].camera->matrix, 3);
    for (long i = 0; i < 300; i++) {
        for (int b = 0; b < kBulletStreakTextures; b++)
            BulletStreakCounts[b] = Chance(20) ? Range(118, 127) : Range(0, 120);
        g_in.kind = Range(0, kBulletStreakKinds - 1);
        BulletStreakKind &kind = BulletStreakKinds[g_in.kind];
        if (Chance(50)) {
            kind.width = Uniform(0.0f, 0.4f);
            kind.length = Uniform(0.0f, 12.0f);
            kind.colour = Next() ^ Next() << 8;
            kind.texture = Range(0, kBulletStreakTextures - 1);
        }
        g_in.position = { eye->x + Uniform(-50.0f, 50.0f), eye->y + Uniform(-10.0f, 10.0f),
                          eye->z + Uniform(-50.0f, 50.0f), Chance(50) ? 1.0f : Uniform(-2.0f, 2.0f) };
        g_in.direction = RandomUnit(Chance(50) ? 1.0f : Uniform(-2.0f, 2.0f));
        g_in.three = Chance(40);
        g_in.position3 = { g_in.position.x, g_in.position.y, g_in.position.z };
        g_in.direction3 = { g_in.direction.x, g_in.direction.y, g_in.direction.z };
        RunCase("RBulletStreak::Add", i, SideBulletAdd, before);
    }
    if (Hooked({ kHookVolatile, kHookZWrites }) && VolatileRequests[VolatileRequestIndex] != NULL) {
        for (long i = 0; i < 30; i++) {
            for (int b = 0; b < kBulletStreakTextures; b++)
                BulletStreakCounts[b] = Chance(30) ? 0 : Range(1, kBulletStreakVertices);
            RunCase("RBulletStreak::Draw", i, SideBulletDraw, before);
        }
    }
    if (Hooked({ kHookDbIndex, kHookDbArgb, kHookDbS8, kHookDbFloat, kHookDbEnd })) {
        for (long i = 0; i < 2; i++) {
            for (int b = 0; b < kBulletStreakTextures; b++) {
                BulletStreakCounts[b] = Range(0, 100);
                for (int v = 0; v < kBulletStreakVertices; v++) {
                    BulletStreakUVs[b][v] = { Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f) };
                    BulletStreakPositions[b][v].w = Uniform(-5.0f, 5.0f);
                    BulletStreakColours[b][v] = Next();
                }
            }
            RunCase("RBulletStreak::Init", i, SideBulletInit, before);
        }
    }
}

// ---- RMissileStreak

void SideStreakUpdate() {
    if (g_in.tickOnly) {
        if (g_original)
            Orig_StreakTick(g_streak, 0);
        else
            g_streak->Update();
    } else {
        if (g_original)
            Orig_StreakUpdate(g_streak, 0, &g_in.position);
        else
            g_streak->Update(&g_in.position);
    }
}

void SideStreakDraw() {
    if (g_original)
        Orig_StreakDraw(g_streak, 0);
    else
        g_streak->Draw();
}

// Init then Kill on each side: the new GeoPrims logged by value, the live ones put back
void SideStreakInitKill() {
    TexturedGeoPrim **geoPrims =
        g_original ? reinterpret_cast<TexturedGeoPrim **>(0x00201250) : MissileStreakGeoPrims;
    TexturedGeoPrim *live[kMissileStreakTypes];
    memcpy(live, geoPrims, sizeof(live));
    if (g_original)
        Orig_StreakInit();
    else
        RMissileStreak::Init();
    for (int i = 0; i < kMissileStreakTypes; i++) {
        TexturedGeoPrim *geoPrim = geoPrims[i];
        Log(geoPrim != NULL);
        if (geoPrim == NULL)
            continue;
        Log(geoPrim->method != NULL);
        for (int p = 0; p < 7; p++) {
            const EAGL::GeoPrimParam &param = (&geoPrim->state)[p];
            Log(param.count);
            Log(p == 3 || p == 4 ? 0 : Canon(param.data));     // the matrix and positions: whatever Construct left
        }
    }
    if (g_original)
        Orig_StreakKill();
    else
        RMissileStreak::Kill();
    memcpy(geoPrims, live, sizeof(live));
}

void RandomiseStreak() {
    memset(g_streakBlock, 0, 0x40);
    g_streak->count = Chance(10) ? RMissileStreak::kMaxPoints - Range(0, 2) * 2 : Range(0, 47) * 2;
    if (Chance(5))
        g_streak->count = Range(0, RMissileStreak::kMaxPoints);
    g_streak->tick = Chance(10) ? Range(0, 3) : Range(0, 500);
    g_streak->type = Range(0, kMissileStreakTypes - 1);
    g_streak->unknown0C = Next();
    for (int p = 0; p < RMissileStreak::kMaxPoints; p++)
        g_streak->points[p] = { Uniform(-100.0f, 100.0f), Uniform(-10.0f, 10.0f), Uniform(-100.0f, 100.0f), 1.0f };
}

void TestMissileStreaks(const Snapshot &before) {
    if (fgRenderer == NULL)
        return;
    // A camera view for Update's right axis
    RViewCamera *view = reinterpret_cast<RViewCamera *>(g_viewBlock);
    RCamera *camera = reinterpret_cast<RCamera *>(g_cameraBlock);
    for (long i = 0; i < 400; i++) {
        RandomiseStreak();
        memset(g_viewBlock, 0, sizeof(g_viewBlock));
        memset(g_cameraBlock, 0, sizeof(g_cameraBlock));
        view->camera = camera;
        Coord4 right = RandomUnit(0.0f);
        memcpy(camera->matrix.mtx[0], &right, sizeof(right));
        fgRenderer->currentView = view;
        if (Chance(30)) {
            for (int t = 0; t < kMissileStreakTypes; t++) {
                MissileStreakHalfWidths[t] = Uniform(0.0f, 1.0f);
                MissileStreakJitter[t] = Uniform(0.0f, 0.5f);
            }
        }
        ShadowRandomSeed = Next() & 0xffff;
        g_in.position = { Uniform(-200.0f, 200.0f), Uniform(-20.0f, 20.0f), Uniform(-200.0f, 200.0f), 1.0f };
        g_in.tickOnly = Chance(10);
        RunCase("RMissileStreak::Update", i, SideStreakUpdate, before);
    }
    bool geoPrims = MissileStreakGeoPrims[0] != NULL && MissileStreakGeoPrims[1] != NULL &&
                    MissileStreakGeoPrims[2] != NULL;
    if (geoPrims && Hooked({ kHookModel, kHookZWrites })) {
        for (long i = 0; i < 150; i++) {
            RandomiseStreak();
            RunCase("RMissileStreak::Draw", i, SideStreakDraw, before);
        }
    }
    for (long i = 0; i < 1; i++) {
        for (int v = 0; v < kMissileStreakDrawn + 2; v++)
            MissileStreakUVs[v] = { Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f), 0.0f, 0.0f };
        for (int v = 0; v < kMissileStreakDrawn; v++)
            MissileStreakColours[v] = Next();
        RunCase("RMissileStreak::Init/Kill", i, SideStreakInitKill, before);
    }
}

// ---- RMuzzleFlash

void SideFlashDraw() {
    if (g_in.three) {
        if (g_original)
            Orig_FlashDrawPOV(&g_in.transform, &g_in.position, g_in.type, g_in.size);
        else
            RMuzzleFlash::DrawPOV(&g_in.transform, &g_in.position, g_in.type, g_in.size);
    } else {
        if (g_original)
            Orig_FlashDraw(&g_in.transform, &g_in.position, &g_in.direction, g_in.type, g_in.size);
        else
            RMuzzleFlash::Draw(&g_in.transform, &g_in.position, &g_in.direction, g_in.type, g_in.size);
    }
}

void SideFlashInit() {
    if (g_original)
        Orig_FlashInit();
    else
        RMuzzleFlash::Init();
}

void TestMuzzleFlashes(const Snapshot &before) {
    if (Hooked({ kHookVolatile, kHookZWrites }) && VolatileRequests[VolatileRequestIndex] != NULL &&
        fgRenderer != NULL) {
        for (long i = 0; i < 300; i++) {
            float *m = &g_in.transform.mtx[0][0];
            for (int k = 0; k < 16; k++)
                m[k] = Uniform(-2.0f, 2.0f);
            m[12] = Uniform(-100.0f, 100.0f);
            m[13] = Uniform(-10.0f, 10.0f);
            m[14] = Uniform(-100.0f, 100.0f);
            m[3] = m[7] = m[11] = 0.0f;
            m[15] = 1.0f;
            g_in.position = { Uniform(-3.0f, 3.0f), Uniform(-3.0f, 3.0f), Uniform(-3.0f, 3.0f), Uniform(0.0f, 2.0f) };
            g_in.direction = RandomUnit(Uniform(-1.0f, 1.0f));
            g_in.three = Chance(50);
            g_in.type = g_in.three ? Range(0, kMuzzleFlashPOVTypes - 1) : Range(0, kMuzzleFlashTypes - 1);
            g_in.size = Chance(5) ? 0.0f : Uniform(0.05f, 6.0f);
            ShadowSimState = Chance(25) ? 3 : Range(0, 4);
            ShadowRandomSeed = Next() & 0xffff;
            RunCase(g_in.three ? "RMuzzleFlash::DrawPOV" : "RMuzzleFlash::Draw", i, SideFlashDraw, before);
        }
    }
    for (long i = 0; i < 2; i++) {
        for (int k = 0; k < 4; k++)
            MuzzleFlashPOVPositions[k] = { Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f), 7.0f, 7.0f };
        for (int k = 0; k < 4; k++)
            for (int v = 0; v < 4; v++)
                MuzzleFlashPOVUVs[k][v] = { Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f) };
        for (int k = 0; k < kMuzzleFlashVertices; k++)
            MuzzleFlashColours[k] = Next();
        RunCase("RMuzzleFlash::Init", i, SideFlashInit, before);
    }
}

// ---- RScorchWorld

void SideScorch() {
    if (g_in.three) {
        if (g_original)
            Orig_ScorchGround(g_place, &g_in.position, g_in.size, g_in.type);
        else
            RScorchWorld::ScorchGround(g_place, &g_in.position, g_in.size, g_in.type);
    } else {
        if (g_original)
            Orig_ScorchWorld(g_place, &g_in.position, &g_in.normal, g_in.size, g_in.type);
        else
            RScorchWorld::ScorchWorld(g_place, &g_in.position, &g_in.normal, g_in.size, g_in.type);
    }
}

void TestScorches(const Snapshot &before) {
    if (!Hooked({ kHookDecal }) || fgRenderHigh == NULL || fgRenderHigh->views[0].camera == NULL)
        return;
    Coord4 base = *MatrixRow(&fgRenderHigh->views[0].camera->matrix, 3);
    for (long i = 0; i < 400; i++) {
        Coord3 probe = { base.x + Uniform(-30.0f, 30.0f), base.y, base.z + Uniform(-30.0f, 30.0f) };
        g_place->Construct();
        g_place->FindClosestFace(&probe, false);
        float ground = g_place->valid ? float(g_place->HeightAtPoint(&probe, false)) : base.y - Uniform(0.0f, 5.0f);
        g_in.position = { probe.x, ground + (Chance(30) ? 0.0f : Uniform(-0.5f, 2.5f)), probe.z,
                          Chance(50) ? 1.0f : 0.0f };
        if (Chance(40))
            g_place->Construct();        // a fresh one, or one that has searched near
        int shape = Range(0, 2);
        if (shape == 0)
            g_in.normal = { Uniform(-0.1f, 0.1f), 1.0f, Uniform(-0.1f, 0.1f), 0.0f };
        else
            g_in.normal = RandomUnit(0.0f);
        if (shape == 1)
            g_in.normal.y = Uniform(-0.2f, 0.2f);        // a wall
        float length = sqrtf(g_in.normal.x * g_in.normal.x + g_in.normal.y * g_in.normal.y +
                             g_in.normal.z * g_in.normal.z);
        g_in.normal.x /= length;
        g_in.normal.y /= length;
        g_in.normal.z /= length;
        g_in.size = Chance(15) ? 0.33f : Chance(15) ? Uniform(0.05f, 0.33f) : Uniform(0.3f, 14.0f);
        g_in.type = Range(0, 7);
        g_in.three = Chance(45);
        RunCase(g_in.three ? "RScorchWorld::ScorchGround" : "RScorchWorld::ScorchWorld", i, SideScorch, before);
    }
}

// ---- RMissileCam

void SideCamDraw() {
    if (g_original)
        Orig_CamDraw(g_cam, 0);
    else
        g_cam->Draw();
}

// Construct, the object and its GeoPrim logged by value, then Delete (flags in g_in.flags)
void SideCamLife() {
    void *block = UMemory::FastAlloc(sizeof(RMissileCam), "RMissileCam");
    if (block == NULL)
        return;
    memset(block, 0xcd, sizeof(RMissileCam));
    RMissileCam *cam = g_original ? Orig_CamConstruct(block, 0) : static_cast<RMissileCam *>(block)->Construct();
    Log(cam == block);
    Log(uint32_t(uintptr_t(cam->vtable)));
    for (int i = 0; i < 3; i++) {
        Log(uint32_t(uintptr_t(cam->textures[i])));
        LogBytes(cam->textures[i], cam->textures[i] != NULL ? 0x18 : 0);
    }
    Log(cam->enabled);
    Log(uint32_t(cam->alpha));
    LogGeoPrim(cam->geoPrim);
    if (g_original)
        Orig_CamDelete(cam, 0, unsigned(g_in.flags));
    else
        cam->Delete(unsigned(g_in.flags));
    if ((g_in.flags & 1) == 0) {
        Log(uint32_t(uintptr_t(cam->vtable)));
        Log(uint32_t(uintptr_t(cam->textures[0])) | uint32_t(uintptr_t(cam->textures[2])));
        UMemory::FastFree(block, sizeof(RMissileCam));
    }
}

void SideCamKill() {
    void *block = UMemory::FastAlloc(sizeof(RMissileCam), "RMissileCam");
    if (block == NULL)
        return;
    MissileCam = static_cast<RMissileCam *>(block)->Construct();
    if (g_original)
        Orig_CamKill();
    else
        RMissileCam::Kill();
}

void TestMissileCam(const Snapshot &before) {
    if (MissileCam != NULL && Hooked({ kHookModel })) {
        for (long i = 0; i < 60; i++) {
            memcpy(g_cam, MissileCam, sizeof(RMissileCam));
            g_cam->enabled = Chance(85) ? 1 : 0;
            g_cam->alpha = Chance(20) ? int32_t(Next()) : Range(0, 255);
            ShadowSimStepCount = Chance(10) ? -Range(0, 100) : Range(0, 100000);
            glbMissionManager->unknown4f0 = Chance(10) ? 1 : 0;
            MissileCamStateMade = 1;
            RunCase("RMissileCam::Draw", i, SideCamDraw, before);
        }
    }
    for (long i = 0; i < 2; i++) {
        g_in.flags = int(i);
        RunCase("RMissileCam::Construct/Delete", i, SideCamLife, before);
    }
    RunCase("RMissileCam::Kill", 0, SideCamKill, before);
}

// ---- RSniperZoom

void SideZoom() {
    switch (g_in.zoomOp) {
    case 0:
        g_original ? Orig_ZoomSetupVerts(g_zoom, 0) : g_zoom->SetupVerts();
        break;
    case 1:
        g_original ? Orig_ZoomDraw(g_zoom, 0) : g_zoom->Draw();
        break;
    case 2:
        g_original ? Orig_ZoomGrab(g_zoom, 0) : g_zoom->GrabBackBuffer();
        break;
    default:
        g_original ? Orig_ZoomReset(g_zoom, 0) : g_zoom->Reset();
        break;
    }
}

void SideZoomLife() {
    void *block = UMemory::FastAlloc(sizeof(RSniperZoom), "RSniperZoom");
    if (block == NULL)
        return;
    memset(block, 0xcd, sizeof(RSniperZoom));
    RSniperZoom *zoom = g_original ? Orig_ZoomConstruct(block, 0) : static_cast<RSniperZoom *>(block)->Construct();
    Log(zoom == block);
    LogBytes(zoom, sizeof(RSniperZoom));
    if (g_original)
        Orig_ZoomDelete(zoom, 0, unsigned(g_in.flags));
    else
        zoom->Delete(unsigned(g_in.flags));
    if ((g_in.flags & 1) == 0) {
        Log(uint32_t(uintptr_t(zoom->vtable)));
        UMemory::FastFree(block, sizeof(RSniperZoom));
    }
}

void SideZoomKill() {
    void *block = UMemory::FastAlloc(sizeof(RSniperZoom), "RSniperZoom");
    if (block == NULL)
        return;
    SniperZoom = static_cast<RSniperZoom *>(block)->Construct();
    if (g_original)
        Orig_ZoomKill();
    else
        RSniperZoom::Kill();
}

__declspec(naked) void CallRingPoint(uint32_t function, Coord4 *position, Coord4 *uv, float radius, float turns) {
    __asm {
        push esi
        push edi
        mov esi, [esp + 0x10]
        mov edi, [esp + 0x14]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        call dword ptr [esp + 0x14]
        add esp, 8
        pop edi
        pop esi
        ret
    }
}

__declspec(naked) void CallParticleRect(uint32_t function, uint32_t texture, Coord4 *rect) {
    __asm {
        mov eax, [esp + 8]
        mov ecx, [esp + 0xc]
        call dword ptr [esp + 4]
        ret
    }
}

void SideRingPoint() {
    g_out[0] = { 7.0f, 7.0f, 7.0f, 7.0f };
    g_out[1] = { 7.0f, 7.0f, 7.0f, 7.0f };
    CallRingPoint(kRingPoint, &g_out[0], &g_out[1], g_in.radius, g_in.turns);
}

void SideParticleRect() {
    g_out[0] = { 7.0f, 7.0f, 7.0f, 7.0f };
    CallParticleRect(kParticleRect, g_in.texture, &g_out[0]);
}

void TestSniperZoom(const Snapshot &before) {
    RPlayerCamera *camera = fgRenderHigh != NULL ? fgRenderHigh->views[0].camera : NULL;
    if (SniperZoom != NULL && camera != NULL && fgRenderer != NULL) {
        for (long i = 0; i < 120; i++) {
            memcpy(g_zoom, SniperZoom, sizeof(RSniperZoom));
            g_zoom->active = Chance(80) ? 1 : 0;
            g_zoom->alpha = Chance(10) ? int32_t(Next()) : Range(0, 255);
            camera->unknown1D0 = Uniform(-100.0f, 100.0f);
            camera->unknown1D4 = Uniform(-100.0f, 100.0f);
            if (Chance(30)) {
                SniperInnerRadius = Uniform(0.0f, 200.0f);
                SniperMiddleRadius = Uniform(0.0f, 300.0f);
                SniperOuterRadius = Uniform(0.0f, 800.0f);
                SniperRingShade = Chance(10) ? int32_t(Next()) : Range(0, 255);
                SniperOuterShade = Range(0, 255);
                SniperCopies = Range(0, 8);
                SniperCopySpread = Uniform(0.0f, 2.0f);
            }
            glbMissionManager->unknown4f0 = Chance(15) ? 1 : 0;
            g_in.zoomOp = Range(0, 3);
            if (g_in.zoomOp == 1 && !Hooked({ kHookTextured }))
                g_in.zoomOp = 0;
            if (g_in.zoomOp == 2 && !Hooked({ kHookTexCopy }))
                g_in.zoomOp = 3;
            RunCase("RSniperZoom", i, SideZoom, before);
        }
        for (long i = 0; i < 300; i++) {
            camera->unknown1D0 = Uniform(-100.0f, 100.0f);
            camera->unknown1D4 = Uniform(-100.0f, 100.0f);
            g_in.radius = Chance(5) ? 0.0f : Uniform(-50.0f, 700.0f);
            g_in.turns = Chance(10) ? float(Range(0, 40)) * 0.05f : Uniform(-3.0f, 3.0f);
            RunCase("FUN_000a68f0", i, SideRingPoint, before);
        }
    }
    if (Hooked({ kHookOffConstruct, kHookOffDestruct })) {
        for (long i = 0; i < 2; i++) {
            g_in.flags = int(i);
            RunCase("RSniperZoom::Construct/Delete", i, SideZoomLife, before);
        }
        RunCase("RSniperZoom::Kill", 0, SideZoomKill, before);
    }
}

void TestParticleRect(const Snapshot &before) {
    const uint32_t named[] = {
        U32_AT(0x001c50fc), U32_AT(0x001c51ac), ShadowVehicleParticleTexture3, U32_AT(0x001c53bc),
        U32_AT(0x001c546c), U32_AT(0x001c530c),
    };
    for (long i = 0; i < 60; i++) {
        g_in.texture = i < 6 ? named[i] : Chance(30) ? named[Range(0, 5)] : Next() ^ Next() << 8;
        RunCase("FUN_000a6cd0", i, SideParticleRect, before);
    }
}

// ---- the regions

void AddGeoPrimRegion(const TexturedGeoPrim *geoPrim, const char *name, bool compare) {
    if (geoPrim != NULL)
        AddShared(uint32_t(uintptr_t(geoPrim)), sizeof(TexturedGeoPrim), name, compare, true);
}

void BuildRegions() {
    g_regions.clear();
    // Ours, with the game's copies
    AddOwned(BulletStreakCounts, 0x001f5ed0, 0x10, "BulletStreakCounts");
    AddOwned(BulletStreakTextures, 0x001f5ee0, 0x10, "BulletStreakTextures");
    AddOwned(BulletStreakUVs, 0x001f2ed0, 0x1000, "BulletStreakUVs");
    AddOwned(BulletStreakPositions, 0x001f3ed0, 0x2000, "BulletStreakPositions");
    AddOwned(&BulletStreakKindIndex, 0x001f6894, 4, "BulletStreakKindIndex");
    AddOwned(&BulletStreakColourFlag, 0x001f6890, 1, "BulletStreakColourFlag");
    AddOwned(MissileStreakGeoPrims, 0x00201250, 0xc, "MissileStreakGeoPrims");
    AddOwned(MissileStreakVertexCounts, 0x00201260, 0x30, "MissileStreakVertexCounts");
    AddOwned(MissileStreakUVs, 0x00200fb0, 0x2a0, "MissileStreakUVs");
    AddOwned(MissileStreakHalfWidths, 0x001c4b00, 0xc, "MissileStreakHalfWidths");
    AddOwned(MissileStreakJitter, 0x001c4b0c, 0xc, "MissileStreakJitter");
    AddOwned(MuzzleFlashTextures, 0x00201450, 0x14, "MuzzleFlashTextures");
    AddOwned(MuzzleFlashPositions, 0x001c4b30, 0x1a0, "MuzzleFlashPositions");
    AddOwned(MuzzleFlashPOVPositions, 0x00201410, 0x40, "MuzzleFlashPOVPositions");
    AddOwned(MuzzleFlashPOVUVs, 0x00201390, 0x80, "MuzzleFlashPOVUVs");
    AddOwned(MuzzleFlashPOVTypes, 0x001c4dc8, 0xc0, "MuzzleFlashPOVTypes");
    AddOwned(&MissileCamStateMade, 0x00200fac, 4, "MissileCamStateMade");
    AddOwned(MissileCamVertexCount, 0x001c4ab0, 0x10, "MissileCamVertexCount");
    AddOwned(MissileCamPositions, 0x001c4ac0, 0x40, "MissileCamPositions");
    AddOwned(&SniperCopySpread, 0x001c50a8, 4, "SniperCopySpread");
    AddOwned(&SniperCopies, 0x001c50b0, 4, "SniperCopies");
    AddOwned(&SniperOuterRadius, 0x001c50b4, 4, "SniperOuterRadius");
    AddOwned(&SniperRingShade, 0x001c50b8, 4, "SniperRingShade");
    AddOwned(&SniperMiddleRadius, 0x001c50bc, 4, "SniperMiddleRadius");
    AddOwned(&SniperInnerRadius, 0x001c50c0, 4, "SniperInnerRadius");
    AddOwned(&SniperOuterShade, 0x00201830, 4, "SniperOuterShade");
    AddOwned(BulletStreakColours, 0x001f6090, 0x800, "BulletStreakColours");
    AddOwned(BulletStreakMaterials, 0x001f5f10, 0x180, "BulletStreakMaterials");
    AddOwned(&MissileStreakState, 0x002012a0, 0x4c, "MissileStreakState");
    AddOwned(MissileStreakColours, 0x002012f0, 0xa0, "MissileStreakColours");
    AddOwned(MuzzleFlashColours, 0x00201480, 0x68, "MuzzleFlashColours");
    AddOwned(MuzzleFlashMaterials, 0x002014f0, 0x1e0, "MuzzleFlashMaterials");
    AddOwned(&MuzzleFlashPOVMaterial, 0x002016d0, 0x60, "MuzzleFlashPOVMaterial");
    AddOwned(&MissileCamState, 0x00200f60, 0x4c, "MissileCamState");
    // Both sides at the game's addresses
    AddShared(0x001f2e20, 0xa8, "BulletStreakKinds");
    AddShared(0x001c45c4, 8, "the random generator");
    AddShared(0x00234e24, 4, "SimState", false);
    AddShared(0x00234e34, 4, "SimStepCount", false);
    AddShared(0x00200f54, 4, "MissileCam", false);
    AddShared(0x00201818, 4, "SniperZoom", false);
    AddShared(uint32_t(uintptr_t(g_streakBlock)), sizeof(g_streakBlock), "the streak");
    AddShared(uint32_t(uintptr_t(g_camBlock)), sizeof(g_camBlock), "the missile camera");
    AddShared(uint32_t(uintptr_t(g_zoomBlock)), sizeof(g_zoomBlock), "the sniper zoom");
    AddShared(uint32_t(uintptr_t(g_placeBlock)), sizeof(g_placeBlock), "the world position");
    AddShared(uint32_t(uintptr_t(g_out)), sizeof(g_out), "the outputs");
    if (SimpleTexturedMaterial != NULL)
        AddShared(uint32_t(uintptr_t(SimpleTexturedMaterial)), sizeof(USimpleTexturedMaterial),
                  "SimpleTexturedMaterial");
    if (glbMissionManager != NULL)
        AddShared(uint32_t(uintptr_t(&glbMissionManager->unknown4f0)), 4, "the mission manager's +0x4f0", false);
    if (fgRenderer != NULL)
        AddShared(uint32_t(uintptr_t(&fgRenderer->currentView)), 4, "the renderer's view", false);
    if (fgRenderHigh != NULL && fgRenderHigh->views[0].camera != NULL)
        AddShared(uint32_t(uintptr_t(&fgRenderHigh->views[0].camera->unknown1D0)), 8, "the aim point", false);
    // The request slots the draws fill (pointers to either side's globals: put back, not compared)
    if (VolatileRequests[VolatileRequestIndex] != NULL)
        AddShared(uint32_t(uintptr_t(VolatileRequests[VolatileRequestIndex])), sizeof(TexturedGeoPrim),
                  "the volatile request", false);
    if (TexturedRequests[TexturedRequestIndex] != NULL)
        AddShared(uint32_t(uintptr_t(TexturedRequests[TexturedRequestIndex])), sizeof(TexturedGeoPrim),
                  "the textured request", false);
    for (int i = 0; i < kMissileStreakTypes; i++)
        AddGeoPrimRegion(MissileStreakGeoPrims[i], "a missile streak GeoPrim", true);
    if (MissileCam != NULL)     // its colours and texture coordinates are on each side's stack: logged by value
        AddGeoPrimRegion(MissileCam->geoPrim, "the missile camera's GeoPrim", false);
    // The textures Init sets the addressing of
    for (int i = 0; i < kBulletStreakTextures; i++)
        if (BulletStreakTextures[i] != NULL)
            AddShared(uint32_t(uintptr_t(BulletStreakTextures[i])), 0x18, "a bullet streak texture");
    for (int i = 0; i < kMuzzleFlashTypes; i++)
        if (MuzzleFlashTextures[i] != NULL)
            AddShared(uint32_t(uintptr_t(MuzzleFlashTextures[i])), 0x18, "a muzzle flash texture");
    if (MissileCam != NULL)
        for (int i = 0; i < 3; i++)
            if (MissileCam->textures[i] != NULL)
                AddShared(uint32_t(uintptr_t(MissileCam->textures[i])), 0x18, "a missile camera texture");
}

void InstallHooks() {
    HookInstall(&g_hooks[kHookVolatile], kVolatileDraw, (const void *)&FakeVolatileDraw);
    HookInstall(&g_hooks[kHookTextured], kTexturedDraw, (const void *)&FakeTexturedDraw);
    HookInstall(&g_hooks[kHookModel], kModelDraw, (const void *)&FakeModelDraw);
    HookInstall(&g_hooks[kHookZWrites], kZWrites, (const void *)&FakeZWrites);
    HookInstall(&g_hooks[kHookDecal], kAddDecal, (const void *)&FakeAddDecal);
    HookInstall(&g_hooks[kHookOffConstruct], kOffscreenConstruct, (const void *)&FakeOffscreenConstruct);
    HookInstall(&g_hooks[kHookOffDestruct], kOffscreenDestruct, (const void *)&FakeOffscreenDestruct);
    HookInstall(&g_hooks[kHookTexCopy], kMakeTexCopy, (const void *)&FakeMakeTexCopy);
    HookInstall(&g_hooks[kHookDbIndex], kDbIndex, (const void *)&FakeDbIndex);
    HookInstall(&g_hooks[kHookDbArgb], kDbArgb, (const void *)&FakeDbArgb);
    HookInstall(&g_hooks[kHookDbS8], kDbS8, (const void *)&FakeDbS8);
    HookInstall(&g_hooks[kHookDbFloat], kDbFloat, (const void *)&FakeDbFloat);
    HookInstall(&g_hooks[kHookDbEnd], kDbEnd, (const void *)&FakeDbEnd);
    int missing = 0;
    for (const Hook &hook : g_hooks)
        missing += hook.on ? 0 : 1;
    if (missing != 0)
        printf("[gameeffectsshadowd] %d of the %d fakes could not be installed - the cases needing them skipped\n",
               missing, int(kHookCount));
}

}  // namespace

void GameEffectsShadowD_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEEFFECTSSHADOWD", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    InstallHooks();
    BuildRegions();
    Snapshot before;
    Capture(&before);

    TestBulletStreaks(before);
    TestMissileStreaks(before);
    TestMuzzleFlashes(before);
    TestScorches(before);
    TestMissileCam(before);
    TestSniperZoom(before);
    TestParticleRect(before);

    Restore(before);
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
    ResetFpu();
    printf("[gameeffectsshadowd] bullet streaks, missile streaks, muzzle flashes, scorch marks, missile camera, sniper "
           "zoom vs originals: %ld cases, %ld checks, %ld differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameeffectsshadowd]   %ld calls faulted\n", g_faults);
    fflush(stdout);
}
