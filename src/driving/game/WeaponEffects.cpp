#pragma fp_contract(off)

#include "WeaponEffects.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bit>

#include "Simulation.h"
#include "../../helpers.h"
#include "../camera/Camera.h"             // RCamera, RViewCamera, AsCoord3
#include "../camera/PlayerCamera.h"       // RPlayerCamera
#include "../data/DebugVariables.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GeoPrimState.h"
#include "../eagl/Model.h"                // DynamicModel
#include "../eagl/RenderContext.h"
#include "../eagl/RenderMethod.h"
#include "../eagl/Tar.h"
#include "../physics/PhysicsMath.h"       // Abs
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/Decals.h"             // TheDecalManager
#include "../render/Draw.h"
#include "../render/RSceneObj.hpp"        // RandomShort
#include "../render/RenderHigh.h"         // fgRenderHigh
#include "../render/Renderer.h"           // fgRenderer
#include "../render/TextureContext.h"
#include "../world/CollisionManager.h"    // fgCollisionMgr
#include "../world/WorldPos.h"
#include "../data/XbeData.h"   // XBE_INITIAL

// ---------------------------------------------------------------------------------------------------------------
// RBulletStreak (0x00099e20-0x0009a420), RMissileStreak and RMuzzleFlash (0x000a0e90-0x000a1c00) and RScorchWorld
// (0x000a50c0-0x000a5820), ported from the listing.
//
// Random numbers: RMissileStreak::Update draws three, RMuzzleFlash::DrawPOV two (unpaused), from the game's 16-bit
// generator (RandomShort, which the original inlines).
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

// ---- globals

// The bullet streaks' batches
// XBE_GLOBAL(0x001f5ed0, 0x10)
int32_t BulletStreakCounts[kBulletStreakTextures];
// XBE_GLOBAL(0x001f5ee0, 0x10)
EAGL::TAR *BulletStreakTextures[kBulletStreakTextures];
// XBE_GLOBAL(0x001f2ed0, 0x1000)
EffectUV BulletStreakUVs[kBulletStreakTextures][kBulletStreakVertices];
// XBE_GLOBAL(0x001f3ed0, 0x2000)
alignas(16) Coord4 BulletStreakPositions[kBulletStreakTextures][kBulletStreakVertices];
// XBE_GLOBAL(0x001f6090, 0x800)
uint32_t BulletStreakColours[kBulletStreakTextures][kBulletStreakVertices];
// XBE_GLOBAL(0x001f5f10, 0x180)
UVolatileMaterial BulletStreakMaterials[kBulletStreakTextures];
// XBE_GLOBAL(0x001f6894, 0x4)
uint32_t BulletStreakKindIndex;
// XBE_GLOBAL(0x001f6890, 0x1)
uint8_t BulletStreakColourFlag;
// XBE_GLOBAL(0x001c485c, 0x1c)
const char *const BulletStreakKindNames[kBulletStreakKinds] = {
    "Henchmen", "Vanquish", "SuperSnowmobile", "SUV", "Captain", "Ultralight", "Sentinel",
};

// The missile streaks' GeoPrims
// XBE_GLOBAL(0x002012a0, 0x4c)
EAGL::GeoPrimState MissileStreakState;
// XBE_GLOBAL(0x002012f0, 0xa0)
uint32_t MissileStreakColours[kMissileStreakDrawn];
// XBE_GLOBAL(0x00201250, 0xc)
TexturedGeoPrim *MissileStreakGeoPrims[kMissileStreakTypes];
// XBE_GLOBAL(0x00201260, 0x30)
uint32_t MissileStreakVertexCounts[kMissileStreakTypes][4];
// XBE_GLOBAL(0x00200fb0, 0x2a0)
alignas(16) Coord4 MissileStreakUVs[kMissileStreakDrawn + 2];
// XBE_GLOBAL(0x001c4b00, 0xc)
float MissileStreakHalfWidths[kMissileStreakTypes] = { 0.3575f, 0.1f, 0.1f };
// XBE_GLOBAL(0x001c4b0c, 0xc)
float MissileStreakJitter[kMissileStreakTypes] = { 0.2f, 0.0f, 0.0f };

// The muzzle flashes' model, textures and POV quad
// XBE_GLOBAL(0x00201480, 0x68)
uint32_t MuzzleFlashColours[kMuzzleFlashVertices];
// XBE_GLOBAL(0x002014f0, 0x1e0)
UVolatileMaterial MuzzleFlashMaterials[kMuzzleFlashTypes];
// XBE_GLOBAL(0x002016d0, 0x60)
UVolatileMaterial MuzzleFlashPOVMaterial;
// XBE_GLOBAL(0x00201450, 0x14)
EAGL::TAR *MuzzleFlashTextures[kMuzzleFlashTypes];
// XBE_GLOBAL(0x001c4b30, 0x1a0)
alignas(16) Coord4 MuzzleFlashPositions[kMuzzleFlashVertices];
XBE_INITIAL(MuzzleFlashPositions);
// XBE_GLOBAL(0x001c4cd0, 0xd0)
EffectUV MuzzleFlashUVs[kMuzzleFlashVertices];
XBE_INITIAL(MuzzleFlashUVs);
// XBE_GLOBAL(0x00201410, 0x40)
alignas(16) Coord4 MuzzleFlashPOVPositions[4];
// XBE_GLOBAL(0x00201390, 0x80)
EffectUV MuzzleFlashPOVUVs[4][4];
// XBE_GLOBAL(0x001c4dc8, 0xc0)
MuzzleFlashPOVType MuzzleFlashPOVTypes[kMuzzleFlashPOVTypes];
XBE_INITIAL(MuzzleFlashPOVTypes);

namespace {

// A texture's name is the word its four characters spell in memory
const uint32_t kBulletStreakTextureNames[kBulletStreakTextures] = {
    0x63727477,     // 'wtrc'
    0x63727479,     // 'ytrc'
    0x6372746f,     // 'otrc'
    0x63727462,     // 'btrc'
};
// The original keeps these byte-reversed and swaps them as it reads them
const uint32_t kMissileStreakTextureNames[kMissileStreakTypes] = {
    0x69617274, 0x69617274, 0x69617274,     // 'trai'
};
const uint32_t kMuzzleFlashTextureNames[kMuzzleFlashTypes] = {
    0x64336c66, 0x64326c66, 0x64336c66, 0x64326c66, 0x64336c66,     // 'fl3d' 'fl2d' ...
};

// TAR addressing and filtering (D3D's numbering)
constexpr uint32_t kTextureWrap = 1;
constexpr uint32_t kTextureClamp = 3;
// GeoPrimState's shading: Gouraud
constexpr uint32_t kShadingGouraud = 1;

const char kGeoPrimNew[] = "EAGL::GeoPrim new";

constexpr int kBulletStreakFull = kBulletStreakVertices - 6;
constexpr uint32_t kBulletStreakGrey = 0x80808080;

constexpr double kRandomScale = 1.0 / 65536.0;      // 16 random bits to [0, 1)
constexpr int kMissileStreakFadeIn = 13;            // the strip's first vertices fade in
constexpr float kOneTwelfth = 1.0f / 12.0f;
constexpr float kMissileStreakFadeAlpha = 30.0f;
constexpr int kMissileStreakAlpha = 30;

constexpr uint32_t kMuzzleFlashColour = 0xd0ffffff;

constexpr float kScorchMaxSize = 6.0f;
constexpr double kScorchFlatNormalY = 0.85f;        // a double in the original, 0.85f's value
constexpr float kScorchSmallSize = 0.33f;
constexpr float kScorchLiftSlope = 0.05f;
constexpr float kScorchLiftBase = 0.035f;
constexpr float kScorchGroundNormalY = 0.98f;
constexpr float kScorchLevelNormalY = 0.95f;
constexpr float kScorchLevelHeight = 0.15f;
constexpr float kScorchGroundLift = 0.025f;
constexpr int kScorchUnprobed = 4;                  // ScorchGround's type that skips the probes round the point
// The faces' tags: the type and the sub-type bit that take no marks, and the type ScorchGround rejects once its
// probes are done
constexpr uint8_t kFaceType6 = 6;
constexpr uint8_t kFaceType9 = 9;
constexpr uint8_t kFaceSubType02 = 0x02;

static_assert(sizeof(EffectUV) == 8, "texture coordinates are two floats");
static_assert(std::bit_cast<uint64_t>(kScorchFlatNormalY) == 0x3feb333340000000 &&
              std::bit_cast<uint32_t>(kOneTwelfth) == 0x3daaaaab &&
              std::bit_cast<uint32_t>(kScorchSmallSize) == 0x3ea8f5c3 &&
              std::bit_cast<uint32_t>(kScorchLiftBase) == 0x3d0f5c29, "the original's constants");

TexturedGeoPrim *NewTexturedGeoPrim() {
    void *memory = EaglMalloc(sizeof(TexturedGeoPrim), kGeoPrimNew);
    return memory != NULL ? static_cast<TexturedGeoPrim *>(memory)->Construct() : NULL;
}

void DeleteTexturedGeoPrim(TexturedGeoPrim *geoPrim) {
    if (geoPrim == NULL)
        return;
    EAGL::RenderMethod *method = geoPrim->method;
    if (method != NULL) {
        method->Destruct();
        EaglFree(method, sizeof(EAGL::RenderMethod));
    }
    EaglFree(geoPrim, sizeof(TexturedGeoPrim));
}

void SetAddressing(EAGL::TAR *texture, uint32_t mode) {
    texture->address0 = mode;
    texture->addressU = mode;
    texture->addressV = mode;
    texture->addressW = mode;
}

// The face's tag, as the last search left it
const FaceTag &FaceTagOf(const WWorldPos *place) {
    return place->face.corner[2].tag;
}

bool TakesNoScorch(const FaceTag &tag) {
    return (tag.subType & kFaceSubType02) != 0 || tag.type == kFaceType6;
}

// The instance's article flags its geometry as taking none (its byte at +0x0c)
bool InstanceTakesNoScorch(const WWorldPos *place) {
    return place->instance != NULL && place->instance->article->unknown0a[2] != 0;
}

// A hit of the world that is not a face of type 6
bool HitScorchable(const WorldCollisionInfo &info) {
    return info.hitType != kHitNone && info.faceType != kFaceType6;
}

// ScorchGround's probe: the face under `probe`, within 0.15 of the height at the point
bool ProbeIsLevel(WWorldPos *place, const Coord3 *probe, float baseHeight) {
    place->FindClosestFace(probe, true);
    if (!place->valid)
        return false;
    return Abs(float(baseHeight - place->HeightAtPoint(probe, false))) < kScorchLevelHeight;
}

}  // namespace

// ---- RBulletStreak

// FUNC_AT(0x00099e20)
void RBulletStreak::Draw() {
    fgRenderer->renderContext->SetZWritesEnable(0);
    for (int i = 0; i < kBulletStreakTextures; i++) {
        if (BulletStreakCounts[i] > 0) {
            TexturedGeoPrim *request = VolatileRequests[VolatileRequestIndex];
            request->positions.SetData(BulletStreakPositions[i]);
            request->texCoords.SetData(BulletStreakUVs[i]);
            request->texture.SetData(BulletStreakTextures[i]);
            request->colours.SetData(BulletStreakColours[i]);
            BulletStreakMaterials[i].Draw(kTriangleList, BulletStreakCounts[i], NULL);
        }
        BulletStreakCounts[i] = 0;
    }
    fgRenderer->renderContext->SetZWritesEnable(1);
}

// FUNC_AT(0x00099f10)
void RBulletStreak::Init() {
    for (int i = 0; i < kBulletStreakTextures; i++) {
        EAGL::TAR *texture =
            RTextureContextManager::GetContext(0)->FindOrCreateTexture(kBulletStreakTextureNames[i], 0);
        BulletStreakTextures[i] = texture;
        SetAddressing(texture, kTextureClamp);
        for (int v = 0; v < kBulletStreakVertices; v++)
            BulletStreakColours[i][v] = kBulletStreakGrey;
        // Two triangles a streak: (0, 1) (0, 0) (1, 0.1), then (1, 0.1) (1, 1) (0, 1)
        for (int v = 0; v < kBulletStreakVertices; v++) {
            EffectUV &uv = BulletStreakUVs[i][v];
            switch (v % 6) {
            case 0:
            case 5:
                uv.u = 0.0f;
                uv.v = 1.0f;
                break;
            case 1:
                uv.u = 0.0f;
                uv.v = 0.0f;
                break;
            case 2:
            case 3:
                uv.u = 1.0f;
                uv.v = 0.1f;
                break;
            case 4:
                uv.u = 1.0f;
                uv.v = 1.0f;
                break;
            }
            BulletStreakPositions[i][v].w = 1.0f;
        }
        UVolatileMaterial &material = BulletStreakMaterials[i];
        material.SetShading(kShadingGouraud);
        material.SetTextureEnable(true);
        material.SetAlphaBlendMode(kBlendAdditive);
        BulletStreakCounts[i] = 0;
    }

    BulletStreakKind *kinds = BulletStreakKinds;
    const uint32_t stride = sizeof(BulletStreakKind);
    dbindex("BulletStreakType", &BulletStreakKindIndex, 0, kBulletStreakKinds - 1, BulletStreakKindNames);
    dbattrib_argb("BulletStreakColour", &kinds[0].colour, &BulletStreakColourFlag, stride);
    dbattrib_s8("BulletStreakTexture", &kinds[0].texture, 0, kBulletStreakTextures - 1, stride, -1.0f, NULL);
    dbattrib_float("BulletStreakWidth", &kinds[0].width, 0.0f, 0.2f, stride, 1.0f, NULL);
    dbattrib_float("BulletStreakLength", &kinds[0].length, 0.0f, 10.0f, stride, 1.0f, NULL);
    dbattrib_float("BulletStreakSpeed", &kinds[0].speed, 10.0f, 300.0f, stride, 1.0f, NULL);
    dbattrib_s8("BulletStreakCount", &kinds[0].count, 1, 9, stride, -1.0f, NULL);
    dbendindex();
}

// FUNC_AT(0x0009a1d0)
void RBulletStreak::Add(const Coord4 *position, const Coord4 *direction, int kind) {
    const BulletStreakKind &streak = BulletStreakKinds[kind];
    int batch = streak.texture;
    int n = BulletStreakCounts[batch];
    if (n >= kBulletStreakFull)
        return;

    // Across the streak, square to the line to the first player's camera
    const Coord4 *eye = MatrixRow(&fgRenderHigh->views[0].camera->matrix, 3);
    Coord4 toEye, across, along;
    VU0_v4sub(eye, position, &toEye);
    VU0_v4unitxyz(&toEye, &toEye);
    VU0_v4unitcrossprodxyz(&toEye, direction, &across);
    VU0_v4scale(&across, streak.width, &across);
    VU0_v4scale(direction, streak.length, &along);

    Coord4 point = *position;
    uint32_t colour = streak.colour;
    Coord4 *vertices = BulletStreakPositions[batch];
    uint32_t *colours = BulletStreakColours[batch];
    colours[n] = colour;
    VU0_v3add(&point, &across, &vertices[n]);
    colours[++n] = colour;
    VU0_v4sub(&point, &across, &vertices[n]);
    VU0_v4sub(&point, &along, &point);
    colours[++n] = colour;
    VU0_v4sub(&point, &across, &vertices[n]);
    colours[++n] = colour;
    VU0_v4sub(&point, &across, &vertices[n]);
    colours[++n] = colour;
    VU0_v3add(&point, &across, &vertices[n]);
    VU0_v3add(&point, &along, &point);
    colours[++n] = colour;
    VU0_v3add(&point, &across, &vertices[n]);
    BulletStreakCounts[batch] += 6;
}

// FUNC_AT(0x0009a3b0)
void RBulletStreak::Add(const Coord3 *position, const Coord3 *direction, int kind) {
    Coord4 position4 = { position->x, position->y, position->z, 1.0f };
    Coord4 direction4 = { direction->x, direction->y, direction->z, 1.0f };
    Add(&position4, &direction4, kind);
}

// ---- RMissileStreak

// FUNC_AT(0x000a0e90)
void RMissileStreak::Update() {
    tick++;
}

// FUNC_AT(0x000a0ea0)
void RMissileStreak::Update(const Coord4 *position) {
    if (count < kMaxPoints) {
        if (tick % 2 == 0)
            count += 2;
        const float *right = fgRenderer->currentView->camera->matrix.mtx[0];
        double jitter = MissileStreakJitter[type];
        double halfJitter = 0.5 * jitter;
        Coord4 &first = points[count - 2];
        first = *position;
        first.x = float(int32_t(RandomShort()) * jitter * kRandomScale - halfJitter + first.x);
        first.y = float(int32_t(RandomShort()) * jitter * kRandomScale - halfJitter + first.y);
        double z = int32_t(RandomShort()) * jitter * kRandomScale - halfJitter + first.z;
        first.z = float(z);

        // The pair: either side along the camera's right axis (the first's z from the unrounded sum)
        Coord4 &second = points[count - 1];
        second = first;
        first.x = float(double(MissileStreakHalfWidths[type]) * right[0] + first.x);
        first.y = float(double(MissileStreakHalfWidths[type]) * right[1] + first.y);
        first.z = float(double(MissileStreakHalfWidths[type]) * right[2] + z);
        second.x = float(second.x - double(MissileStreakHalfWidths[type]) * right[0]);
        second.y = float(second.y - double(MissileStreakHalfWidths[type]) * right[1]);
        second.z = float(second.z - double(MissileStreakHalfWidths[type]) * right[2]);
    }
    tick++;
}

// FUNC_AT(0x000a1070)
void RMissileStreak::Init() {
    for (int i = 0; i < kMissileStreakTypes; i++)
        MissileStreakGeoPrims[i] = NewTexturedGeoPrim();
    EAGL::TAR *textures[kMissileStreakTypes];
    for (int i = 0; i < kMissileStreakTypes; i++)
        textures[i] = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kMissileStreakTextureNames[i], 0);

    // Alpha (doubled into the top byte): rising as the square over the first 13 vertices, then 30
    for (int i = 0; i < kMissileStreakDrawn; i++) {
        int alpha = kMissileStreakAlpha;
        if (i < kMissileStreakFadeIn) {
            double x = double(i) * kOneTwelfth;
            alpha = Truncate(float(x * x * kMissileStreakFadeAlpha));
        }
        MissileStreakColours[i] = uint32_t(alpha) << 25 | 0xffffff;
    }
    // The strip's texture coordinates: u 1, 0 by the vertex's side, v 0, 0, 1, 1 along
    for (int i = 0; i < kMissileStreakDrawn + 2; i++) {
        const Coord4 left = { 0.0f, 0.0f, 1.0f, 1.0f };
        const Coord4 right = { 1.0f, 0.0f, 1.0f, 1.0f };
        MissileStreakUVs[i] = (i & 1) != 0 ? left : right;
        if ((i & 2) != 0)
            MissileStreakUVs[i].y = 1.0f;
    }

    for (int i = 0; i < kMissileStreakTypes; i++) {
        TexturedGeoPrim *geoPrim = MissileStreakGeoPrims[i];
        geoPrim->positions.count = kMissileStreakDrawn;
        geoPrim->state.Set(1, &MissileStreakState);
        geoPrim->texCoords.Set(kMissileStreakDrawn, MissileStreakUVs);
        geoPrim->colours.Set(kMissileStreakDrawn, MissileStreakColours);
        geoPrim->texture.Set(1, textures[i]);
        geoPrim->vertexCount.Set(1, MissileStreakVertexCounts[i]);
    }
    MissileStreakState.SetPrimitiveType(kTriangleStrip);
    MissileStreakState.SetAlphaCompareValue(4);
    MissileStreakState.SetShading(kShadingGouraud);
    MissileStreakState.SetTextureEnable(true);
    ::Draw::SetNormalBlendMode(&MissileStreakState);
}

// FUNC_AT(0x000a1330)
void RMissileStreak::Draw() {
    if (count <= 3)
        return;
    int newest = (tick + 1) / 2 * 2;
    int end = newest < count ? newest : count;
    int start = newest - kMissileStreakDrawn > 0 ? newest - kMissileStreakDrawn : 0;
    int vertices = end - start;
    if (vertices <= 3)
        return;

    fgRenderer->renderContext->SetZWritesEnable(0);
    TexturedGeoPrim *geoPrim = MissileStreakGeoPrims[type];
    geoPrim->positions.SetData(&points[start]);
    geoPrim->texCoords.SetData((start & 2) != 0 ? &MissileStreakUVs[2] : &MissileStreakUVs[0]);
    MissileStreakVertexCounts[type][0] = vertices;
    EAGL::DynamicModel model;
    model.Construct();
    model.AddGeoPrim(geoPrim->AsGeoPrim());
    model.Draw();
    fgRenderer->renderContext->SetZWritesEnable(1);
    model.Destruct();
}

// The GeoPrims are freed, not cleared, as the original has it.
// FUNC_AT(0x000a1460)
void RMissileStreak::Kill() {
    for (int i = 0; i < kMissileStreakTypes; i++)
        DeleteTexturedGeoPrim(MissileStreakGeoPrims[i]);
}

// ---- RMuzzleFlash

// FUNC_AT(0x000a14b0)
void RMuzzleFlash::InitPOVLayer() {
    static const Coord4 kPositions[4] = {
        { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, -1.0f, 0.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f },
    };
    static const EffectUV kTurns[4][4] = {
        { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f } },
        { { 1.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 0.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } },
    };
    memcpy(MuzzleFlashPOVPositions, kPositions, sizeof(kPositions));
    memcpy(MuzzleFlashPOVUVs, kTurns, sizeof(kTurns));

    MuzzleFlashPOVMaterial.SetShading(kShadingGouraud);
    MuzzleFlashPOVMaterial.SetTextureEnable(true);
    MuzzleFlashPOVMaterial.SetAlphaBlendMode(kBlendAdditive);
    for (int type = 0; type < kMuzzleFlashPOVTypes; type++) {
        MuzzleFlashPOVType &pov = MuzzleFlashPOVTypes[type];
        for (int i = 0; i < 3; i++)
            pov.textures[i] = RTextureContextManager::GetContext(0)->FindOrCreateTexture(pov.names[i], 0);
    }
}

// FUNC_AT(0x000a1870)
void RMuzzleFlash::Draw(const MATRIX4 *transform, const Coord4 *position, const Coord4 *direction, int type,
                        float size) {
    alignas(16) MATRIX4 matrix;
    VU0_MATRIX4Init(&matrix);
    const Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
    VU0_v4copy(&up, MatrixRow(&matrix, 1));
    VU0_v4copy(direction, MatrixRow(&matrix, 2));
    MatrixRow(&matrix, 2)->w = 0.0f;
    VU0_v4unitcrossprodxyz(direction, &up, MatrixRow(&matrix, 0));
    MatrixRow(&matrix, 0)->w = 0.0f;
    VU0_v4copy(position, MatrixRow(&matrix, 3));
    VU0_MATRIX4_mult(&matrix, &matrix, transform);
    size *= 0.25f;
    alignas(16) MATRIX4 scale;
    BuildScaleXYZ(&scale, size, size, size);
    VU0_MATRIX4_mult(&matrix, &scale, &matrix);

    fgRenderer->renderContext->SetZWritesEnable(0);
    TexturedGeoPrim *request = VolatileRequests[VolatileRequestIndex];
    request->positions.SetData(MuzzleFlashPositions);
    request->texCoords.SetData(MuzzleFlashUVs);
    request->colours.SetData(MuzzleFlashColours);
    request->texture.SetData(MuzzleFlashTextures[type]);
    MuzzleFlashMaterials[type].Draw(kTriangleStrip, kMuzzleFlashVertices, &matrix);
    fgRenderer->renderContext->SetZWritesEnable(1);
}

// FUNC_AT(0x000a19d0)
void RMuzzleFlash::DrawPOV(const MATRIX4 *transform, const Coord4 *position, int type, float size) {
    alignas(16) MATRIX4 matrix;
    alignas(16) MATRIX4 scale;
    BuildTranslate(&matrix, position->x, position->y, position->z);
    VU0_MATRIX4_mult(&matrix, &matrix, transform);
    BuildScaleUniform(&scale, size);
    VU0_MATRIX4_mult(&matrix, &scale, &matrix);

    fgRenderer->renderContext->SetZWritesEnable(0);
    TexturedGeoPrim *request = VolatileRequests[VolatileRequestIndex];
    request->positions.SetData(MuzzleFlashPOVPositions);
    request->colours.SetData(MuzzleFlashColours);
    EAGL::TAR *texture;
    if (fgSimulation.simState != kSimPaused) {
        request->texCoords.SetData(MuzzleFlashPOVUVs[RandomShort() % 4]);
        texture = MuzzleFlashPOVTypes[type].textures[RandomShort() % 3];
    } else {
        request->texCoords.SetData(MuzzleFlashPOVUVs[0]);
        texture = MuzzleFlashPOVTypes[type].textures[0];
    }
    request->texture.SetData(texture);
    MuzzleFlashPOVMaterial.Draw(kTriangleStrip, 4, &matrix);
    fgRenderer->renderContext->SetZWritesEnable(1);
}

// FUNC_AT(0x000a1b50)
void RMuzzleFlash::Init() {
    InitPOVLayer();
    for (int i = 0; i < kMuzzleFlashTypes; i++) {
        EAGL::TAR *texture =
            RTextureContextManager::GetContext(0)->FindOrCreateTexture(kMuzzleFlashTextureNames[i], 0);
        MuzzleFlashTextures[i] = texture;
        SetAddressing(texture, kTextureWrap);
        UVolatileMaterial &material = MuzzleFlashMaterials[i];
        material.SetShading(kShadingGouraud);
        material.SetTextureEnable(true);
        material.SetAlphaBlendMode(kBlendAdditive);
    }
    // The model turned a quarter round y, (x, z) to (z, -x) - again at every Init
    for (int i = 0; i < kMuzzleFlashVertices; i += 2) {
        Coord4 &a = MuzzleFlashPositions[i];
        Coord4 &b = MuzzleFlashPositions[i + 1];
        float x = a.x;
        a.x = a.z;
        a.z = -x;
        MuzzleFlashColours[i] = kMuzzleFlashColour;
        MuzzleFlashColours[i + 1] = kMuzzleFlashColour;
        x = b.x;
        b.x = b.z;
        b.z = -x;
    }
}

// ---- RScorchWorld

// FUNC_AT(0x000a50c0)
void RScorchWorld::ScorchWorld(WWorldPos *place, const Coord4 *point, const Coord4 *normal, float size, int type) {
    place->FindClosestFace(AsCoord3(point), true);
    if (TakesNoScorch(FaceTagOf(place)))
        return;

    // The mark's other axis: across the normal from straight up, or from z on a level face
    Coord4 reference = { 0.0f, 1.0f, 0.0f, 1.0f };
    if (size > kScorchMaxSize)
        size = kScorchMaxSize;
    bool level = Abs(normal->y) > kScorchFlatNormalY;
    if (level) {
        reference.y = 0.0f;
        reference.z = -1.0f;
    }
    Coord4 across = {};     // its w is the original's uninitialised stack
    VU0_v4crossprodxyz(normal, &reference, &across);
    WorldCollisionInfo info;
    info.Construct();

    // A large mark must find the surface at both ends of the axis and along the reference, through the point;
    // halved until it does, twice at most
    bool hitAcross = true, hitBack = true, hitAlong = true;
    if (!(size <= kScorchSmallSize)) {
        for (int attempt = 0; attempt < 2; attempt++) {
            Coord4 segment[2] = { *point, *point };
            VU0_v4scaleadd(normal, 0.25f, &segment[0], &segment[0]);
            VU0_v4scaleadd(normal, -0.25f, &segment[1], &segment[1]);
            VU0_v4scaleadd(&across, size, &segment[0], &segment[0]);
            VU0_v4scaleadd(&across, size, &segment[1], &segment[1]);
            hitAcross = false;
            fgCollisionMgr->CheckHitWorld(segment, &info);
            if (HitScorchable(info))
                hitAcross = true;

            float back = -2.0f * size;
            VU0_v4scaleadd(&across, back, &segment[0], &segment[0]);
            VU0_v4scaleadd(&across, back, &segment[1], &segment[1]);
            hitBack = false;
            fgCollisionMgr->CheckHitWorld(segment, &info);
            if (HitScorchable(info))
                hitBack = true;

            VU0_v4scaleadd(&reference, size, &segment[0], &segment[0]);
            VU0_v4scaleadd(&reference, size, &segment[1], &segment[1]);
            hitAlong = false;
            fgCollisionMgr->CheckHitWorld(segment, &info);
            if (HitScorchable(info))
                hitAlong = true;

            if (hitBack && hitAcross && hitAlong)
                break;
            size *= 0.5f;
        }
    }

    // Lifted off the face, more the steeper it is
    float lift = float(fabs((1.0 - normal->y) * kScorchLiftSlope) + kScorchLiftBase);
    Coord4 centre = {};     // its w is the original's uninitialised stack (AddDecal copies it into the corners)
    VU0_v4scaleadd(normal, lift, point, &centre);
    if (!hitBack || !hitAcross || !hitAlong)
        return;
    place->FindClosestFace(AsCoord3(point), true);
    if (InstanceTakesNoScorch(place))
        return;
    if ((info.faceSubType & kFaceSubType02) == 0)
        TheDecalManager->AddDecal(&centre, normal, &across, type, size + size, size + size);
    if (level)
        return;

    // A wall's mark runs down to the ground at its foot, when that is near and level
    place->FindClosestFace(AsCoord3(&centre), true);
    if (!place->valid || (FaceTagOf(place).subType & kFaceSubType02) != 0)
        return;
    double drop = centre.y - place->HeightAtPoint(AsCoord3(&centre), false);
    float height = float(drop);
    if (!(drop < size) || !(height > 0.0f))
        return;
    Coord4 down;
    VU0_v4scale(normal, height * 4.0f, &down);
    VU0_v4sub(&centre, &down, &centre);
    centre.y = float(place->HeightAtPoint(AsCoord3(&centre), false) + lift);
    Coord4 groundNormal;
    place->GetNormal(&groundNormal, 0);
    if (!(Abs(groundNormal.y) >= kScorchGroundNormalY))
        return;
    TheDecalManager->AddDecal(&centre, &groundNormal, normal, type, size + size, size * 8.0f);
}

// FUNC_AT(0x000a5530)
void RScorchWorld::ScorchGround(WWorldPos *place, const Coord4 *point, float size, int type) {
    place->FindClosestFace(AsCoord3(point), true);
    if (TakesNoScorch(FaceTagOf(place)) || InstanceTakesNoScorch(place))
        return;
    double half = size * 0.5;
    float radius = kScorchMaxSize < half ? kScorchMaxSize : float(half);
    float above = float(point->y - place->HeightAtPoint(AsCoord3(point), false));
    Coord4 groundNormal;
    place->GetNormal(&groundNormal, 0);
    if (groundNormal.y < kScorchLevelNormalY)
        return;
    if (!place->valid)
        return;

    // The ground a radius away on each side within 0.15 of the height at the point
    if (type != kScorchUnprobed) {
        Coord3 probe = { point->x, point->y, point->z };
        float baseHeight = float(place->HeightAtPoint(&probe, false));
        probe.x += radius;
        if (!ProbeIsLevel(place, &probe, baseHeight))
            return;
        float diameter = radius + radius;
        probe.x -= diameter;
        if (!ProbeIsLevel(place, &probe, baseHeight))
            return;
        probe.z += radius;
        if (!ProbeIsLevel(place, &probe, baseHeight))
            return;
        probe.z -= diameter;
        if (!ProbeIsLevel(place, &probe, baseHeight))
            return;
        if (FaceTagOf(place).type == kFaceType9)
            return;
    }

    double margin = radius - above * 0.5;
    if (!(margin > 0.0))
        return;
    Coord4 centre = *point;
    centre.y = float(double(centre.y) - above + kScorchGroundLift);
    const Coord4 axis = { 0.0f, 0.0f, 1.0f, 0.0f };
    float width = float(margin + margin);
    TheDecalManager->AddDecal(&centre, &groundNormal, &axis, type, width, width);
}
