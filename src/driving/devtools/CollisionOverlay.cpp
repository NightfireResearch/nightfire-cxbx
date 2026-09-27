#include "CollisionOverlay.h"

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../render/RenderState.hpp"

// ---------------------------------------------------------------------------------------------------------------
// The level's collision geometry, drawn through the game's own renderer - so with its camera, projection and
// depth buffer - as one volatile-material draw a frame, the way the glares are drawn.
//
// What is drawn, all read from memory the track load left in place (docs/driving-collision.md):
//   - the collision manager (*0x00239a70) holds the instance array (+0x8) and count (+0xc);
//   - an instance (64 bytes) is a world->local matrix - rows 0 and 2 of a rotation at +0x00 and +0x20, row 1
//     derived when flags (+0x18) & 3, else (0, 1, 0), and a translation at +0x30 - a bounding radius (+0x3c) and
//     a pointer to its geometry (+0x1c, null if switched off);
//   - the geometry is in the instance's local space: a strip count (+0x0), strip headers from +0x20 (16 bytes:
//     centre, radius, and the strip's offset from +0x20 at +0xe), triangle strips of 16-byte vertices (the 4th
//     word of vertex 0 is the vertex count; of vertex i+2, triangle i's surface byte and face flags), and
//     vertical barrier walls (count at +0x4, offset from +0x20 at +0x2; 32 bytes: x, y min, z, then x, y max, z
//     at +0x10).
// Local to world is the orthonormal inverse: world_i = row_i . (local - t).
// ---------------------------------------------------------------------------------------------------------------

#define CollisionManager (*(uint8_t **)0x00239a70)
#define RRendererInstance (*(uint8_t **)0x001ebff4)
// The material needs a texture - a null one faults in the draw - so the glare's "flar" texture stands in,
// sampled at its bright centre and tinted by the vertex colours.
#define GlareTexture (*(uint32_t *)0x00208cb0)

enum Mode { MODE_OFF, MODE_SOLID, MODE_WIRE, MODE_COUNT };
static const char *kModeNames[MODE_COUNT] = { "off", "solid", "wire" };

// Xbox Direct3D primitive types, which is what UVolatileMaterial::Draw takes.
static const int kLineList = 2;
static const int kTriangleList = 5;

static const int kMaxVertices = 90000;
static float g_positions[kMaxVertices][4];
static uint32_t g_colours[kMaxVertices];
static float g_uvs[kMaxVertices][2];

// Our own material, made by the game's constructor the first time it is needed.
alignas(16) static uint8_t g_materialStorage[0x60];
static UVolatileMaterial *g_material;

// The surface colours (ARGB, alpha applied per mode), by the WSurface byte: the car's friction table's order.
static const uint32_t kSurfaceColour[16] = {
    0xff808080,   //  0 NODRIVE
    0xff505a64,   //  1 PAVED
    0xffa08c64,   //  2 GRAVEL
    0xff40a040,   //  3 GRASS
    0xff8c7864,   //  4 COBBLE
    0xff8c5a32,   //  5 DIRT
    0xff3264c8,   //  6 WATER
    0xffa0703c,   //  7 WOOD
    0xffb4e6ff,   //  8 ICE
    0xfff0f0f0,   //  9 SNOW
    0xff646e78,   // 10 PAVED_ROUGH
    0xffff00ff,   // 11 (unnamed)
    0xff6e5a50,   // 12 RAILROAD
    0xff9aa6b2,   // 13 METAL
    0xffffff00,   // 14 (unnamed)
    0xff78a078,   // 15 (unnamed: off-road terrain in the snow levels)
};
static const uint32_t kBarrierColour = 0xffff4040;
static const uint32_t kIgnoredColour = 0xff404040;   // faces the collision manager's queries skip (flags & 0xf0)

struct V3 { float x, y, z; };

struct Instance {
    V3 row[3];
    V3 t;
    const uint8_t *geometry;
    float distance;
};

static Mode g_mode = MODE_OFF;
static float g_radius = 150.0f;
static int g_vertexCount;
static bool g_full;

static void ReadSettings() {
    static bool done;
    if (done)
        return;
    done = true;
    char text[32];
    GetPrivateProfileStringA("Settings", "CollisionOverlay", "off", text, sizeof(text), ".\\settings.ini");
    for (int m = 0; m < MODE_COUNT; m++)
        if (_stricmp(text, kModeNames[m]) == 0)
            g_mode = (Mode)m;
    g_radius = (float)GetPrivateProfileIntA("Settings", "CollisionOverlayRadius", 150, ".\\settings.ini");
    if (g_mode != MODE_OFF)
        printf("[collision] overlay %s, within %.0f m of the camera (F7 cycles off / solid / wire)\n",
               kModeNames[g_mode], g_radius);
}

static void PollKey() {
    static bool wasDown;
    bool down = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (down && !wasDown) {
        g_mode = (Mode)((g_mode + 1) % MODE_COUNT);
        printf("[collision] overlay %s\n", kModeNames[g_mode]);
    }
    wasDown = down;
}

static float Dot(const V3 &a, const V3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static V3 ToWorld(const Instance &inst, const float *local) {
    V3 d = { local[0] - inst.t.x, local[1] - inst.t.y, local[2] - inst.t.z };
    return { Dot(inst.row[0], d), Dot(inst.row[1], d), Dot(inst.row[2], d) };
}

static void Emit(const V3 &p, uint32_t colour) {
    int i = g_vertexCount++;
    g_positions[i][0] = p.x;
    g_positions[i][1] = p.y;
    g_positions[i][2] = p.z;
    g_positions[i][3] = 1.0f;
    g_colours[i] = colour;
    g_uvs[i][0] = 0.5f;
    g_uvs[i][1] = 0.5f;
}

// One triangle, as three vertices (solid) or its three edges (wire).
static void EmitTriangle(const V3 &a, const V3 &b, const V3 &c, uint32_t colour) {
    int needed = g_mode == MODE_WIRE ? 6 : 3;
    if (g_vertexCount + needed > kMaxVertices) {
        g_full = true;
        return;
    }
    if (g_mode == MODE_WIRE) {
        Emit(a, colour); Emit(b, colour);
        Emit(b, colour); Emit(c, colour);
        Emit(c, colour); Emit(a, colour);
    } else {
        Emit(a, colour); Emit(b, colour); Emit(c, colour);
    }
}

static uint32_t WithAlpha(uint32_t colour) {
    uint32_t alpha = g_mode == MODE_SOLID ? 0x60 : 0xff;
    return (colour & 0x00ffffff) | (alpha << 24);
}

static void EmitInstance(const Instance &inst) {
    const uint8_t *geometry = inst.geometry;
    int strips = *(const uint16_t *)geometry;
    const uint8_t *base = geometry + 0x20;
    for (int s = 0; s < strips && !g_full; s++) {
        const uint8_t *header = base + s * 16;
        const uint8_t *strip = base + *(const uint16_t *)(header + 0xe);
        uint32_t count = *(const uint32_t *)(strip + 0xc);
        if (count < 3 || count > 4096)
            continue;
        V3 prev2 = ToWorld(inst, (const float *)strip);
        V3 prev1 = ToWorld(inst, (const float *)(strip + 16));
        for (uint32_t i = 2; i < count; i++) {
            const uint8_t *vertex = strip + i * 16;
            V3 p = ToWorld(inst, (const float *)vertex);
            uint8_t surface = vertex[0xc];
            uint8_t flags = vertex[0xd];
            uint32_t colour = (flags & 0xf0) ? kIgnoredColour : kSurfaceColour[surface & 15];
            EmitTriangle(prev2, prev1, p, WithAlpha(colour));
            prev2 = prev1;
            prev1 = p;
        }
    }

    int barriers = *(const uint16_t *)(geometry + 4);
    const uint8_t *barrier = base + *(const uint16_t *)(geometry + 2);
    for (int b = 0; b < barriers && !g_full; b++, barrier += 32) {
        const float *p0 = (const float *)barrier;
        const float *p1 = (const float *)(barrier + 0x10);
        const float corners[4][3] = { { p0[0], p0[1], p0[2] }, { p1[0], p0[1], p1[2] },
                                      { p1[0], p1[1], p1[2] }, { p0[0], p1[1], p0[2] } };
        V3 w[4];
        for (int k = 0; k < 4; k++)
            w[k] = ToWorld(inst, corners[k]);
        uint32_t colour = WithAlpha(kBarrierColour);
        EmitTriangle(w[0], w[1], w[2], colour);
        EmitTriangle(w[0], w[2], w[3], colour);
    }
}

static int ByDistance(const void *a, const void *b) {
    float da = ((const Instance *)a)->distance, db = ((const Instance *)b)->distance;
    return da < db ? -1 : da > db ? 1 : 0;
}

void CollisionOverlay_Draw() {
    ReadSettings();
    PollKey();
    uint8_t *manager = CollisionManager;
    if (g_mode == MODE_OFF || manager == nullptr || RRendererInstance == nullptr || GlareTexture == 0)
        return;

    // The camera's position: RRenderer::fgRenderer->view (+4)->camera (+4), +0x40.
    const uint8_t *view = *(uint8_t **)(RRendererInstance + 4);
    const uint8_t *camera = view ? *(uint8_t **)(view + 4) : nullptr;
    if (camera == nullptr)
        return;
    const float *eye = (const float *)(camera + 0x40);

    // The instances within reach, nearest first, so a full buffer drops the far ones.
    const uint8_t *records = *(uint8_t **)(manager + 0x8);
    int count = *(int *)(manager + 0xc);
    static Instance *near_;
    static int nearCapacity;
    if (count > nearCapacity) {
        free(near_);
        near_ = (Instance *)malloc(sizeof(Instance) * count);
        nearCapacity = near_ ? count : 0;
        if (!near_)
            return;
    }
    int nearCount = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t *r = records + i * 64;
        Instance inst;
        inst.geometry = *(const uint8_t *const *)(r + 0x1c);
        if (inst.geometry == nullptr)
            continue;
        memcpy(&inst.row[0], r + 0x00, 12);
        memcpy(&inst.row[2], r + 0x20, 12);
        memcpy(&inst.t, r + 0x30, 12);
        if (r[0x18] & 3) {   // row 1 = row 2 x row 0
            const V3 &a = inst.row[2], &b = inst.row[0];
            inst.row[1] = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        } else {
            inst.row[1] = { 0.0f, 1.0f, 0.0f };
        }
        // The centre is -(row_i . t); reach is the XZ radius, which is enough for picking.
        V3 centre = { -Dot(inst.row[0], inst.t), -Dot(inst.row[1], inst.t), -Dot(inst.row[2], inst.t) };
        float dx = centre.x - eye[0], dy = centre.y - eye[1], dz = centre.z - eye[2];
        float distance = sqrtf(dx * dx + dy * dy + dz * dz) - *(const float *)(r + 0x3c);
        if (distance > g_radius)
            continue;
        inst.distance = distance;
        near_[nearCount++] = inst;
    }
    qsort(near_, nearCount, sizeof(Instance), ByDistance);

    g_vertexCount = 0;
    g_full = false;
    for (int i = 0; i < nearCount && !g_full; i++)
        EmitInstance(near_[i]);
    if (g_vertexCount == 0)
        return;

    static bool reportedFull;
    if (g_full && !reportedFull) {
        reportedFull = true;
        printf("[collision] overlay: more than %d vertices within %.0f m - the farthest instances are left out\n",
               kMaxVertices, g_radius);
    }

    if (g_material == nullptr)
        g_material = ((UVolatileMaterial *)g_materialStorage)->Construct();

    uint8_t *request = VolatileRequests[VolatileRequestIndex];
    *(uint32_t **)(request + 0x30) = g_colours;
    *(float (**)[4])(request + 0x28) = g_positions;
    *(float (**)[2])(request + 0x38) = g_uvs;
    *(uint32_t *)(request + 0x10) = GlareTexture;
    g_material->Draw(g_mode == MODE_WIRE ? kLineList : kTriangleList, g_vertexCount, nullptr);
}
