#pragma fp_contract(off)

#include "Decals.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GeoPrimState.h"
#include "../eagl/Loader.h"               // DynamicLoader::GetRegisteredVar
#include "../eagl/Model.h"                // DynamicModel
#include "../eagl/RenderContext.h"
#include "../engine/InputConfig.h"        // BuildFileName
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/RealMemory.h"       // MEM_free_copy
#include "../platform/RealPrint.h"        // MEM_copy, MEM_fill
#include "Draw.h"
#include "Renderer.h"
#include "TextureContext.h"

// ---------------------------------------------------------------------------------------------------------------
// RDecalManager and its neighbours (0x0009ac40-0x0009b680), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define CRT_printf ((int (*)(const char *format, ...))0x00132192)

// ---- globals
#define TexturedParentMethod ((EAGL::RenderMethod *)0x00242de8)     // the render methods the GeoPrims inherit
#define EmpParentMethod ((EAGL::RenderMethod *)0x00242e1c)
// The quads: eight GeoPrims of 21, six vertices each (two triangles), and per quad its alpha and delay
#define DecalGeoPrims ((TexturedGeoPrim **)0x001f68b0)
#define DecalAlpha ((uint8_t *)0x001f68d0)
#define DecalDelay ((uint8_t *)0x001f6978)
#define DecalPositions ((Coord4 *)0x001f6a20)
#define DecalUVs ((Coord4 *)0x001fa920)
#define DecalColours ((uint32_t *)0x001fe830)
#define DecalState (*(EAGL::GeoPrimState *)0x001ff7f0)
#define DecalVertexCount ((uint32_t *)0x001c48a0)      // the GeoPrims' vertex count record: 126, 0, 0, 0

namespace {

constexpr uintptr_t kRDecalManagerVtable = 0x00192838;
constexpr uintptr_t kUSingletonVtable = 0x0018beb0;
constexpr int kGeoPrims = 8;
constexpr int kQuadsPerGeoPrim = 21;
constexpr int kVerticesPerQuad = 6;
constexpr int kQuads = kGeoPrims * kQuadsPerGeoPrim;
constexpr int kVerticesPerGeoPrim = kQuadsPerGeoPrim * kVerticesPerQuad;
constexpr int kVertices = kQuads * kVerticesPerQuad;
constexpr uint32_t kTextureDecl = 0x6c636564;   // 'decl'
constexpr uint32_t kPrimitiveTriangles = 5;
constexpr int kCylinderSteps = 17;
constexpr float kCylinderTurnStep = 1.0f / 16.0f;

void *RegisteredVar(const char *name) {
    bool found;
    return DynamicLoader::GetRegisteredVar(name, &found);
}

void DeleteMethod(EAGL::RenderMethod *method) {
    method->Destruct();
    EaglFree(method, sizeof(EAGL::RenderMethod));
}

// What Reset and the constructor share: the quads cleared, the GeoPrims' arrays and render state
void ClearQuads() {
    memset(DecalAlpha, 0, kQuads);
    memset(DecalDelay, 0, kQuads);
    for (int i = 0; i < kVertices; i++)
        DecalColours[i] = 0xffffffff;
    const Coord4 zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < kVertices; i++) {
        DecalPositions[i] = zero;
        DecalUVs[i] = zero;
    }
}

void PointGeoPrim(TexturedGeoPrim *geoPrim, int index) {
    int first = index * kVerticesPerGeoPrim;
    geoPrim->colours.Set(kVerticesPerGeoPrim, &DecalColours[first]);
    geoPrim->positions.Set(kVerticesPerGeoPrim, &DecalPositions[first]);
    geoPrim->texCoords.Set(kVerticesPerGeoPrim, &DecalUVs[first]);
}

} // namespace

// FUNC_AT(0x0009ac40)
TexturedGeoPrim* TexturedGeoPrim::Construct() {
    method = NewChildRenderMethod(TexturedParentMethod);
    state.Set(0, NULL);
    texture.Set(0, NULL);
    vertexCount.Set(0, NULL);
    void *matrix = RegisteredVar("EAGL::ViewPort::gpModelViewProjectionMatrix");
    positions.Set(0, NULL);
    colours.Set(0, NULL);
    texCoords.Set(0, NULL);
    modelViewProjection.Set(1, matrix);
    return this;
}

// FUNC_AT(0x0009b550)
EmpGeoPrim* EmpGeoPrim::Construct() {
    method = NewChildRenderMethod(EmpParentMethod);
    for (int i = 0; i < 4; i++)
        params[i].Set(0, NULL);
    params[4].Set(1, RegisteredVar("EAGL::ViewPort::gpModelMatrix"));
    params[5].Set(1, RegisteredVar("EAGL::ViewPort::gpModelViewMatrix"));
    params[6].Set(1, RegisteredVar("EAGL::ViewPort::gpProjectionMatrix"));
    params[7].Set(1, RegisteredVar("GAME::FishEyeParams"));
    params[8].Set(8, RegisteredVar("GAME::PositionalLights"));
    params[9].Set(0, NULL);
    params[10].Set(0, NULL);
    void *simStep = RegisteredVar("GAME::SimStep");
    for (int i = 12; i < 15; i++)
        params[i].Set(0, NULL);
    params[11].Set(1, simStep);
    return this;
}

// FUNC_AT(0x0009acf0)
RDecalManager* RDecalManager::Construct() {
    vtable = reinterpret_cast<const void *>(kRDecalManagerVtable);
    nextDecal = 0;
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = static_cast<TexturedGeoPrim *>(EaglMalloc(sizeof(TexturedGeoPrim), "EAGL::GeoPrim new"));
        DecalGeoPrims[i] = geoPrim != NULL ? geoPrim->Construct() : NULL;
    }
    EAGL::TAR *texture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kTextureDecl, 0);
    DecalState.SetPrimitiveType(kPrimitiveTriangles);
    DecalState.SetTextureEnable(true);
    DecalState.SetShading(1);
    Draw::SetNormalBlendMode(&DecalState);
    ClearQuads();
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = DecalGeoPrims[i];
        PointGeoPrim(geoPrim, i);
        geoPrim->texture.Set(1, texture);
        geoPrim->state.Set(1, &DecalState);
        geoPrim->vertexCount.Set(1, DecalVertexCount);
    }

    char path[64];
    DecalType *table = types;
    void *file = UFileLoader::FileLoadz(BuildFileName(path, 0, "data\\render\\", "decal.dat", ""), 0x100);
    if (file == NULL) {
        CRT_printf("Unable to load lighting file for config [Decal.dat]\n");
        MEM_fill(table, 0, sizeof(types));
    } else {
        MEM_copy(table, file, sizeof(types));
        MEM_free_copy(file);
    }
    return this;
}

// The GeoPrims are freed but the table keeps their addresses, as the original has it.
// FUNC_AT(0x0009b3c0)
void RDecalManager::Destruct() {
    vtable = reinterpret_cast<const void *>(kRDecalManagerVtable);
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = DecalGeoPrims[i];
        if (geoPrim != NULL) {
            if (geoPrim->method != NULL)
                DeleteMethod(geoPrim->method);
            EaglFree(geoPrim, sizeof(TexturedGeoPrim));
        }
    }
    vtable = reinterpret_cast<const void *>(kUSingletonVtable);
}

// FUNC_AT(0x0009b450)
RDecalManager* RDecalManager::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// The instance is not cleared, as the original has it.
// FUNC_AT(0x0009af50)
void RDecalManager::Kill() {
    RDecalManager *manager = TheDecalManager;
    if (manager != NULL)
        (manager->*XbeVirtual<decltype(&RDecalManager::Delete)>(manager, 0))(1);
}

// FUNC_AT(0x0009af70)
void RDecalManager::Reset() {
    ClearQuads();
    for (int i = 0; i < kGeoPrims; i++) {
        TexturedGeoPrim *geoPrim = DecalGeoPrims[i];
        PointGeoPrim(geoPrim, i);
        geoPrim->state.Set(1, &DecalState);
    }
}

// FUNC_AT(0x0009b070)
void RDecalManager::AddDecal(const Coord4 *centre, const Coord4 *axis, const Coord4 *normal, int type, float width,
                             float height) {
    const DecalType *decal = &types[type];
    int slot = nextDecal;
    Coord4 *position = &DecalPositions[slot * kVerticesPerQuad];
    Coord4 *uv = &DecalUVs[slot * kVerticesPerQuad];

    uint32_t delay = decal->delay;
    if (delay == 0)
        delay = 1;
    DecalDelay[slot] = uint8_t(delay);
    DecalAlpha[slot] = decal->alpha;
    DecalAlpha[slot] <<= 2;
    for (int i = 0; i < kVerticesPerQuad; i++)
        DecalColours[slot * kVerticesPerQuad + i] = 0x00ffffff;

    // The quad's sides: across = normal x axis, times width; along = (normal x axis) x axis, times height
    Coord4 along, across, corner;
    VU0_v4copy(normal, &along);
    VU0_v4crossprod1(&along, axis, &across);
    VU0_v4crossprod1(&across, axis, &along);
    VU0_v4scale4(&across, width, &across);
    VU0_v4scale4(&along, height, &along);
    along.w = 0.0f;
    across.w = 0.0f;
    VU0_v4scaleadd4(&across, -0.5f, centre, &corner);
    VU0_v4scaleadd4(&along, -0.5f, &corner, &corner);
    corner.w = 1.0f;

    // Two triangles: 0 1 2 and 1 2 3 (as 3 4 5)
    VU0_v4copy(&corner, &position[0]);
    VU0_v4add4(&corner, &across, &position[1]);
    VU0_v4add4(&corner, &along, &position[2]);
    VU0_v4copy(&position[1], &position[3]);
    VU0_v4copy(&position[2], &position[4]);
    VU0_v4add4(&position[1], &along, &position[5]);
    VU0_v4copy(&decal->uvs[0], &uv[0]);
    VU0_v4copy(&decal->uvs[1], &uv[1]);
    VU0_v4copy(&decal->uvs[2], &uv[2]);
    VU0_v4copy(&decal->uvs[1], &uv[3]);
    VU0_v4copy(&decal->uvs[2], &uv[4]);
    VU0_v4copy(&decal->uvs[3], &uv[5]);

    if (++nextDecal >= kQuads)
        nextDecal = 0;
}

// FUNC_AT(0x0009b2d0)
void RDecalManager::DrawDecals() {
    for (int i = 0; i < kQuads; i++) {
        if (DecalDelay[i] != 0 && --DecalDelay[i] == 0) {
            uint32_t colour = DecalColours[i * kVerticesPerQuad] + (uint32_t(DecalAlpha[i]) << 24);
            for (int j = 0; j < kVerticesPerQuad; j++)
                DecalColours[i * kVerticesPerQuad + j] = colour;
        }
    }
    fgRenderer->renderContext->SetZWritesEnable(0);
    EAGL::DynamicModel model;
    model.Construct();
    for (int i = 0; i < kGeoPrims; i++)
        model.AddGeoPrim(DecalGeoPrims[i]->AsGeoPrim());
    model.Draw();
    fgRenderer->renderContext->SetZWritesEnable(1);
    model.Destruct();
}

// FUNC_AT(0x0009b470)
void CylinderSection(Coord3 *positions, CylinderUV *uvs, float radiusB, float radiusA, float zA, float zB) {
    float turns = 0.0f;
    int vertex = 0;
    for (int step = 0; step < kCylinderSteps; step++) {
        for (int side = 0; side < 2; side++) {
            float radius = side != 0 ? radiusB : radiusA;
            Coord3 *position = &positions[vertex];
            position->x = float(CosineTurns(turns) * radius);
            position->y = float(SineTurns(turns) * radius + 0.5f);
            position->z = side != 0 ? zB : zA;
            uvs[vertex][0] = float((position->x + 1.0) * 0.5f);
            uvs[vertex][1] = float((position->z + 1.0) * 0.5f);
            vertex++;
        }
        turns += kCylinderTurnStep;
    }
}
