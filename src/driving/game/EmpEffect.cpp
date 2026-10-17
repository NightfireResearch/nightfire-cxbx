#pragma fp_contract(off)

#include "EmpEffect.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <bit>

#include "Shell.h"                        // Shell::ApplyWorldEffects
#include "Simulation.h"
#include "../audio/Bank.h"                // SharedTreeMax, SharedTreeIterator
#include "../data/AttributeContainers.h"  // AttributeMap: the rotations and steps of maps with 0x1c-byte nodes
#include "../data/DebugVariables.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../eagl/GeoPrimState.h"
#include "../eagl/Model.h"                // DynamicModel
#include "../eagl/RenderContext.h"
#include "../eagl/RenderMethod.h"
#include "../engine/PhysicsUtil.h"        // Util_GaussRandom, Util_PerturbVector
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"              // Ftol
#include "../render/Decals.h"             // EmpGeoPrim, CylinderSection
#include "../render/Lightning.h"          // TheLightning
#include "../render/Lights.h"             // fgLightManager
#include "../render/Reflection.h"         // TheReflection
#include "../render/Renderer.h"           // fgRenderer
#include "../render/RSceneObj.hpp"        // RSceneObj, RandomShort
#include "../render/TextureContext.h"
#include "../render/TimeData.h"           // SimStep
#include "../world/CollisionManager.h"    // fgCollisionMgr, WorldCollisionInfo
#include "../world/SoundMap.h"            // BuyMapHead
#include "../world/World.h"               // ArticleEffect
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// REmp, REmpBolts and the two maps between them (0x0009b680-0x0009dd40), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

// ---- the simulation's globals

namespace {

constexpr int kRingSteps = 21;                          // vertex pairs round the ring
constexpr int kRingVertices = kRingSteps * 2;
constexpr int kSphereBands = 6;
constexpr int kBandVertices = 36;
constexpr int kSphereBandVertices = 34;

} // namespace

// ---- REmp's globals

// XBE_GLOBAL(0x001ff840, 4)
EmpGeoPrim *EmpRingGeoPrim;
// XBE_GLOBAL(0x001ff850, 0x150)
CylinderUV EmpRingUVs[kRingVertices];
// XBE_GLOBAL(0x001ff9a0, 0x1f8)
Coord3 EmpRingPositions[kRingVertices];
// XBE_GLOBAL(0x001ffb98, 4)
EmpGeoPrim *EmpSphereGeoPrim;
// XBE_GLOBAL(0x001ffba0, 0x10e0)
EmpSphereData EmpSphere;
// XBE_GLOBAL(0x00200c80, 8)
EmpLightRecord EmpLightParams;
// XBE_GLOBAL(0x00200c90, 0x98)
uint32_t EmpSphereColours[38];
// XBE_GLOBAL(0x00200d30, 0x4c)
EAGL::GeoPrimState EmpSphereState;
// XBE_GLOBAL(0x00200d80, 0xa8)
uint32_t EmpRingColours[kRingVertices];
// XBE_GLOBAL(0x00200e30, 0x4c)
EAGL::GeoPrimState EmpRingState;
// XBE_GLOBAL(0x00200e80, 0x80)
alignas(16) Coord4 EmpLights[8];

// XBE_GLOBAL(0x001c48c0, 0x40)
Coord4 EmpParam10Vectors[4] = {
    { 0.005f, 0.02f, 0.0f, 0.0f },      // the sphere's
    { -0.004f, -0.03f, 0.0f, 0.0f },    // not used
    { 0.0078f, 0.0f, 0.0f, 0.0f },      // the ring's first draw
    { -0.0074f, 0.0f, 0.0f, 0.0f },     // and its second
};
// XBE_GLOBAL(0x001c4910, 0x10)
uint32_t EmpRingVertexCount[4] = { kRingVertices, 0, 0, 0 };
// XBE_GLOBAL(0x001c4920, 0x10)
uint32_t EmpSphereVertexCount[4] = { kSphereBandVertices, 0, 0, 0 };
// XBE_GLOBAL(0x001c4930, 0x60)
uint16_t EmpIndices[48] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
};
// XBE_GLOBAL(0x001c4990, 0x10)
Coord4 EmpConstants = { 0.0f, 1.0f, 2.0f, 1.0f / 127.0f };

static_assert(std::bit_cast<uint32_t>(0.005f) == 0x3ba3d70a && std::bit_cast<uint32_t>(0.02f) == 0x3ca3d70a &&
              std::bit_cast<uint32_t>(-0.004f) == 0xbb83126f && std::bit_cast<uint32_t>(-0.03f) == 0xbcf5c28f &&
              std::bit_cast<uint32_t>(0.0078f) == 0x3bff9724 && std::bit_cast<uint32_t>(-0.0074f) == 0xbbf27bb3 &&
              std::bit_cast<uint32_t>(1.0f / 127.0f) == 0x3c010204, "the original's constant records");

// ---- the debug variables
// XBE_GLOBAL(0x00200f00, 4)
int kSnapToBoundingBox;
// XBE_GLOBAL(0x001c49a8, 4)
int kAllowVehicleBolts = 1;
// XBE_GLOBAL(0x001c49ac, 4)
int kAllowWorldBolts = 1;
// XBE_GLOBAL(0x001c49b0, 4)
unsigned kTargetBoltsAlive = 24;
// XBE_GLOBAL(0x001c49b4, 4)
float kScaleLifetime = 0.4f;
// XBE_GLOBAL(0x001c49b8, 4)
float kHitWorldPct = 0.4f;
// XBE_GLOBAL(0x001c49bc, 4)
float kHitWorldRange = 13.0f;
// XBE_GLOBAL(0x001c49c0, 4)
float kWorldPerturb = 0.05f;
// XBE_GLOBAL(0x001c49c4, 4)
float kWorldFlashSize = 10.0f;
// XBE_GLOBAL(0x001c49c8, 4)
float kWorldArcFactor = 11.5f;
// XBE_GLOBAL(0x001c49cc, 4)
float kVehicleArcFactor = 23.0f;
// XBE_GLOBAL(0x001c49d0, 4)
float kVehicleDriftMin = 1.7f;
// XBE_GLOBAL(0x001c49d4, 4)
float kVehicleDriftMax = 3.3f;
// XBE_GLOBAL(0x001c49d8, 4)
float kVehicleDriftVariance = 0.34f;
// XBE_GLOBAL(0x001c49dc, 4)
float kVehiclePerturb = 0.04f;
// XBE_GLOBAL(0x001c49e0, 4)
float kVehicleSparkRate = 0.75f;
// XBE_GLOBAL(0x00200f04, 4)
float kVehicleJoltForce;
// XBE_GLOBAL(0x00200f08, 4)
float kVehicleJoltTorque;
// XBE_GLOBAL(0x001c49e4, 4)
int kVehicleSegments = 3;
// XBE_GLOBAL(0x001c49e8, 4)
int kVehicleLevels = 2;
// XBE_GLOBAL(0x00200f0c, 4)
float kCenterPullPct;
// XBE_GLOBAL(0x001c49ec, 4)
int kGlowEnable = 1;
// XBE_GLOBAL(0x001c49f0, 4)
float kGlowStrength = 2.5f;
// XBE_GLOBAL(0x001c49f4, 4)
float kGlowAmplitude = 0.6f;
// XBE_GLOBAL(0x001c49f8, 4)
float kGlowFrequency = 2.75f;
// XBE_GLOBAL(0x001c49fc, 4)
float kGlowNoiseStrength = 0.3f;
// XBE_GLOBAL(0x001c4a00, 4)
float kGlowNoiseRange = 1.0f;
// XBE_GLOBAL(0x001c4a04, 4)
float kGlowScatter = 0.8f;
// XBE_GLOBAL(0x001c4a08, 4)
uint32_t kGlowColour = 0xff3026a0;
// XBE_GLOBAL(0x00200f14, 1)
uint8_t GlowColourFlag;                 // the flag byte dbattrib_argb is handed (and does not use)

static_assert(std::bit_cast<uint32_t>(0.4f) == 0x3ecccccd && std::bit_cast<uint32_t>(0.05f) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(1.7f) == 0x3fd9999a && std::bit_cast<uint32_t>(3.3f) == 0x40533333 &&
              std::bit_cast<uint32_t>(0.34f) == 0x3eae147b && std::bit_cast<uint32_t>(0.04f) == 0x3d23d70a &&
              std::bit_cast<uint32_t>(0.6f) == 0x3f19999a && std::bit_cast<uint32_t>(0.3f) == 0x3e99999a &&
              std::bit_cast<uint32_t>(0.8f) == 0x3f4ccccd, "the debug variables' starting values");

namespace {

constexpr uint32_t kTextureEmpA = 0x61706d65;           // "empa"
constexpr uint32_t kTextureEmp4 = 0x34706d65;           // "emp4"
constexpr uint32_t kPrimitiveStrip = 6;
constexpr uint32_t kRingAlpha = 70;
constexpr float kRingScale = 6.0f;
constexpr float kQuarterTurn = 0.25f;
constexpr float kTwelfthTurn = 1.0f / 12.0f;
constexpr float kRingStep = 0.05f;
constexpr float kInnerRadius = 0.25f;
constexpr float kOneOver65536 = 1.0f / 65536.0f;
constexpr float kOneOver255 = 1.0f / 255.0f;
constexpr Coord4 kFlashColour = { 0.7f, 0.6f, 1.0f, 0.0f };
const Coord3 kZeroPoint = { 0.0f, 0.0f, 0.0f };
const Coord4 kZero = { 0.0f, 0.0f, 0.0f, 0.0f };
const Coord4 kOrigin = { 0.0f, 0.0f, 0.0f, 1.0f };
static_assert(std::bit_cast<uint32_t>(kTwelfthTurn) == 0x3daaaaab && std::bit_cast<uint32_t>(kRingStep) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(kOneOver255) == 0x3b808081 && std::bit_cast<uint32_t>(0.7f) == 0x3f333333,
              "the original's constants");

constexpr uint32_t kGeoPrimSize = sizeof(EmpGeoPrim);
constexpr uint32_t kRenderMethodSize = sizeof(EAGL::RenderMethod);

EmpGeoPrim *NewEmpGeoPrim() {
    EmpGeoPrim *geoPrim = static_cast<EmpGeoPrim *>(EaglMalloc(kGeoPrimSize, "EAGL::GeoPrim new"));
    return geoPrim != NULL ? geoPrim->Construct() : NULL;
}

void DeleteEmpGeoPrim(EmpGeoPrim *geoPrim) {
    if (geoPrim == NULL)
        return;
    EAGL::RenderMethod *method = geoPrim->method;
    if (method != NULL) {
        method->Destruct();
        EaglFree(method, kRenderMethodSize);
    }
    EaglFree(geoPrim, kGeoPrimSize);
}

EAGL::TAR *EmpTexture(uint32_t name) {
    return RTextureContextManager::GetContext(0)->FindOrCreateTexture(name, 0);
}

#if NIGHTFIRE_X87
// strength + amplitude * FSIN(radians), on the x87 as the original keeps it (FSIN's result is not rounded)
__declspec(naked) float GlowWave(double radians, float amplitude, float strength) {
    __asm {
        fld qword ptr [esp + 4]
        fsin
        fmul dword ptr [esp + 12]
        fadd dword ptr [esp + 16]
        ret
    }
}
#else
// strength + amplitude * sin(radians) in double
float GlowWave(double radians, float amplitude, float strength) {
    return float(sin(radians) * amplitude + strength);
}
#endif

// The rotations and iterator steps of the maps with 0x1c-byte nodes are AttributeMap's (the same compiled code)
AttributeMap *AsAttributeMap(EmpAnchorMap *map) {
    return reinterpret_cast<AttributeMap *>(map);
}
AttributeNode *AsAttributeNode(EmpAnchorNode *node) {
    return reinterpret_cast<AttributeNode *>(node);
}
void Lrotate(EmpAnchorMap *map, EmpAnchorNode *node) {
    AsAttributeMap(map)->Lrotate(AsAttributeNode(node));
}
void Rrotate(EmpAnchorMap *map, EmpAnchorNode *node) {
    AsAttributeMap(map)->Rrotate(AsAttributeNode(node));
}
EmpAnchorNode *Next(EmpAnchorNode *node) {
    AttributeMap::Iterator it = { AsAttributeNode(node) };
    it.Inc();
    return reinterpret_cast<EmpAnchorNode *>(it.node);
}
EmpAnchorNode *Previous(EmpAnchorNode *node) {
    AttributeMap::Iterator it = { AsAttributeNode(node) };
    it.Dec();
    return reinterpret_cast<EmpAnchorNode *>(it.node);
}

// The next node in order, as erase(first, last) has it inline: the head stays where it is
EmpAnchorNode *InlineNext(EmpAnchorNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    EmpAnchorNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

// A locator whose name starts "emp" and goes on
bool IsAnchor(const ArticleEffect *effect) {
    return effect->type == ArticleEffect::kTypeLocator && strncmp(effect->locator.name, "emp", 3) == 0 &&
           strlen(effect->locator.name) > 3;
}

} // namespace

// =============================================================================================================
// REmp
// =============================================================================================================

// FUNC_AT(0x0009b680)
void REmp::Init() {
    EmpLightParams.simStep = &SimStep.step;
    EmpLightParams.lights = EmpLights;
    for (int i = 0; i < 8; i++)
        EmpLights[i] = kZero;

    float turns = 0.0f;
    float zA = -1.0f;
    for (int band = 0; band < kSphereBands; band++) {
        // Eight words from the band's 35th texture coordinate are cleared: past the last band they reach the first
        // band's first position and a half
        uint8_t *words = reinterpret_cast<uint8_t *>(EmpSphere.uvs[band]) + (kBandVertices - 2) * sizeof(CylinderUV);
        memset(words, 0, 8 * sizeof(uint32_t));

        double start = double(turns) - kQuarterTurn;
        float a = float(start);
        float b = float(start + kTwelfthTurn);
        float radiusB = float(CosineTurns(b));
        float radiusA = float(CosineTurns(a));
        // |sin a - sin b|, each side evaluated twice (an ABS macro's way); sin a is rounded through a double, and the
        // compiler subtracts sin b on the x87 except in the negation (RealMath.h)
        double height = MinusSineTurns(float(AsDouble(SineTurns(a))), b) > 0.0
                            ? MinusSineTurns(float(AsDouble(SineTurns(a))), b)
                            : -(float(AsDouble(SineTurns(a))) - SineTurns(b));
        float zB = float(height + zA);
        CylinderSection(EmpSphere.positions[band], EmpSphere.uvs[band], radiusB, radiusA, zA, zB);
        turns += kTwelfthTurn;
        zA = zB;
    }

    EmpSphereState.SetTextureEnable(true);
    EmpSphereState.SetPrimitiveType(kPrimitiveStrip);
    EmpSphereState.SetAlphaTestEnable(false);
    EmpSphereState.SetTransparencyMethod(1);
    for (int i = 0; i < kSphereBandVertices; i++)
        EmpSphereColours[i] = 0x40404040;

    EmpSphereGeoPrim = NewEmpGeoPrim();
    EmpRingGeoPrim = NewEmpGeoPrim();

    EAGL::GeoPrimParam *sphere = EmpSphereGeoPrim->params;
    sphere[kEmpParamConstants].Set(1, &EmpConstants);
    sphere[kEmpParamIndices].Set(kSphereBandVertices, EmpIndices);
    sphere[kEmpParamTexture].count = 1;
    sphere[kEmpParamTexture].SetData(EmpTexture(kTextureEmpA));
    sphere[kEmpParamTexCoords].count = kSphereBandVertices;
    sphere[kEmpParamColours].Set(kSphereBandVertices, EmpSphereColours);
    sphere[kEmpParamPositions].count = kSphereBandVertices;
    sphere[kEmpParamState].Set(1, &EmpSphereState);
    sphere[kEmpParam10].Set(1, &EmpParam10Vectors[0]);
    sphere[kEmpParamSimStep].Set(1, &SimStep.step);
    sphere[kEmpParamVertexCount].Set(1, EmpSphereVertexCount);
    EmpRingGeoPrim->params[kEmpParamFishEye].count = 1;
    EmpRingGeoPrim->params[kEmpParamFishEye].SetData(TheReflection->GetReflectionMapWarpageData());
    sphere[kEmpParamFishEye].count = 1;
    sphere[kEmpParamFishEye].SetData(TheReflection->GetReflectionMapWarpageData());

    EmpRingState.SetTextureEnable(true);
    EmpRingState.SetPrimitiveType(kPrimitiveStrip);
    EmpRingState.SetTransparencyMethod(1);
    EmpRingState.SetAlphaBlendMode(2);
    EmpRingState.SetShading(1);

    EAGL::GeoPrimParam *ring = EmpRingGeoPrim->params;
    ring[kEmpParamConstants].Set(1, &EmpConstants);
    ring[kEmpParamIndices].Set(kSphereBandVertices, EmpIndices);
    ring[kEmpParamTexCoords].Set(kRingVertices, EmpRingUVs);
    ring[kEmpParamTexture].count = 1;
    ring[kEmpParamTexture].SetData(EmpTexture(kTextureEmp4));
    ring[kEmpParamColours].Set(kRingVertices, EmpRingColours);
    ring[kEmpParamPositions].Set(kRingVertices, EmpRingPositions);
    ring[kEmpParamState].Set(1, &EmpRingState);
    ring[kEmpParam10].Set(1, &EmpParam10Vectors[2]);
    ring[kEmpParamSimStep].Set(1, &SimStep.step);
    ring[kEmpParamVertexCount].Set(1, EmpRingVertexCount);
    sphere[kEmpParamLights].SetData(fgLightManager->positionalLights);
    ring[kEmpParamLights].SetData(fgLightManager->positionalLights);

    turns = 0.0f;
    for (int i = 0; i < kRingSteps; i++) {
        Coord3 *outer = &EmpRingPositions[i * 2];
        Coord3 *inner = &EmpRingPositions[i * 2 + 1];
        outer->x = float(SineTurns(turns));
        inner->x = float(SineTurnsTimes(turns, kInnerRadius));
        inner->y = 0.5f;
        outer->y = 0.5f;
        outer->z = float(CosineTurns(turns));
        inner->z = float(CosineTurnsTimes(turns, kInnerRadius));
        float u = float(i & 1);
        EmpRingUVs[i * 2][1] = 1.0f;
        EmpRingUVs[i * 2 + 1][0] = u;
        EmpRingUVs[i * 2 + 1][1] = 0.0f;
        EmpRingUVs[i * 2][0] = u;
        turns += kRingStep;
    }
}

// FUNC_AT(0x0009bb30)
void REmp::DrawShootingEffect(const MATRIX4 *transform, float progress) {
    fgRenderer->renderContext->SetZWritesEnable(0);
    uint32_t grey = Ftol(double(kRingAlpha) * (1.0 - progress)) & 0xff;
    uint32_t colour = 0x7f000000 | grey << 16 | grey << 8 | grey;
    for (int i = 0; i < kRingVertices; i++)
        EmpRingColours[i] = colour;

    MATRIX4 m;
    float scale = kRingScale * progress;
    BuildScale(&m, scale, 1.0f, scale);
    VU0_MATRIX4_mult(&m, &m, transform);
    for (int i = 0; i < kRingSteps; i++) {
        EmpRingUVs[i * 2][1] = 1.0f;
        EmpRingUVs[i * 2 + 1][1] = 0.0f;
    }
    EmpRingState.SetAlphaBlendMode(2);

    EAGL::DynamicModel model;
    model.Construct();
    model.SetModelMatrix(reinterpret_cast<const float *>(&m));
    model.AddGeoPrim(reinterpret_cast<EAGL::GeoPrim *>(EmpRingGeoPrim));
    EmpRingGeoPrim->params[kEmpParam10].SetData(&EmpParam10Vectors[2]);
    model.Draw();
    EmpRingGeoPrim->params[kEmpParam10].SetData(&EmpParam10Vectors[3]);
    model.Draw();
    fgRenderer->renderContext->SetZWritesEnable(1);
    model.Destruct();
}

// FUNC_AT(0x0009bce0)
void REmp::Kill() {
    DeleteEmpGeoPrim(EmpSphereGeoPrim);
    DeleteEmpGeoPrim(EmpRingGeoPrim);
}

// =============================================================================================================
// The anchor lists and the map of them
// =============================================================================================================

// FUNC_AT(0x0009c370)
void EmpAnchorList::Clear() {
    if (anchors != NULL)
        OperatorDelete(anchors);
    anchors = NULL;
    count = 0;
}

// FUNC_AT(0x0009c3a0)
EmpAnchorList* EmpAnchorList::Assign(const EmpAnchorList *other) {
    Clear();
    count = other->count;
    anchors = static_cast<EmpAnchor **>(OperatorNewArray(count * sizeof(EmpAnchor *)));
    for (int i = 0; i < count; i++)
        anchors[i] = other->anchors[i];
    return this;
}

// FUNC_AT(0x0009c400)
void EmpAnchorGroup::Destruct() {
    EMPEFFECT_UNTESTED("EmpAnchorGroup::Destruct (0x0009c400)");
    list.Clear();
}

// FUNC_AT(0x0009c4a0)
void EmpAnchorNode::DestroyValue() {
    value.list.Clear();
}

// FUNC_AT(0x0009c330)
EmpAnchorNode* EmpAnchorMap::Max(EmpAnchorNode *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}

// FUNC_AT(0x0009c350)
EmpAnchorNode* EmpAnchorMap::Min(EmpAnchorNode *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

// FUNC_AT(0x0009c420)
EmpAnchorNode* EmpAnchorMap::BuyHead() {
    return BuyMapHead<EmpAnchorNode>();
}

// FUNC_AT(0x0009c4c0)
EmpAnchorNode* EmpAnchorMap::BuyNode(EmpAnchorNode *left, EmpAnchorNode *parent, EmpAnchorNode *right,
                                     const EmpAnchorGroup *value, uint8_t color) {
    EmpAnchorNode *node = static_cast<EmpAnchorNode *>(UMemory::FastAlloc(sizeof(EmpAnchorNode), "STL"));
    if (node != NULL) {
        node->left = left;
        node->parent = parent;
        node->right = right;
        node->value.group = value->group;
        node->value.list.anchors = NULL;
        node->value.list.count = 0;
        node->value.list.Assign(&value->list);
        node->color = color;
        node->isNil = 0;
    }
    return node;
}

// FUNC_AT(0x0009c570)
void EmpAnchorMap::EraseSubtree(EmpAnchorNode *node) {
    for (EmpAnchorNode *next; !node->isNil; node = next) {
        EraseSubtree(node->right);
        next = node->left;
        node->DestroyValue();
        if (node != NULL)
            UMemory::FastFree(node, sizeof(EmpAnchorNode));
    }
}

// FUNC_AT(0x0009c5c0)
EmpAnchorNode** EmpAnchorMap::InsertAt(EmpAnchorNode **result, bool addLeft, EmpAnchorNode *where,
                                       const EmpAnchorGroup *value) {
    if (size >= 0x15555554)
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    EmpAnchorNode *node = BuyNode(head, where, head, value, kTreeRed);
    size++;
    if (where == head) {
        head->parent = node;
        head->left = node;
        head->right = node;
    } else if (addLeft) {
        where->left = node;
        if (where == head->left)
            head->left = node;
    } else {
        where->right = node;
        if (where == head->right)
            head->right = node;
    }

    for (EmpAnchorNode *x = node; x->parent->color == kTreeRed;) {
        EmpAnchorNode *parent = x->parent;
        EmpAnchorNode *grandparent = parent->parent;
        if (parent == grandparent->left) {
            EmpAnchorNode *uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    Lrotate(this, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Rrotate(this, x->parent->parent);
            }
        } else {
            EmpAnchorNode *uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    Rrotate(this, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Lrotate(this, x->parent->parent);
            }
        }
    }
    head->parent->color = kTreeBlack;
    *result = node;
    return result;
}

// FUNC_AT(0x0009c980)
EmpAnchorNode** EmpAnchorMap::EraseAt(EmpAnchorNode **result, EmpAnchorNode *where) {
    if (where->isNil)
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    EmpAnchorNode *erased = where;
    EmpAnchorNode *next = Next(where);

    EmpAnchorNode *pnode = where;   // the node that really leaves its place: where, or its successor
    EmpAnchorNode *fixnode;         // the node that takes pnode's place
    EmpAnchorNode *fixnodeParent;
    if (where->left->isNil) {
        fixnode = where->right;
    } else if (where->right->isNil) {
        fixnode = where->left;
    } else {
        pnode = next;
        fixnode = pnode->right;
    }

    if (pnode == where) {
        fixnodeParent = where->parent;
        if (!fixnode->isNil)
            fixnode->parent = fixnodeParent;
        if (head->parent == where)
            head->parent = fixnode;
        else if (fixnodeParent->left == where)
            fixnodeParent->left = fixnode;
        else
            fixnodeParent->right = fixnode;
        if (head->left == where)
            head->left = fixnode->isNil ? fixnodeParent : Min(fixnode);
        if (head->right == where)
            head->right = fixnode->isNil ? fixnodeParent : Max(fixnode);
    } else {
        where->left->parent = pnode;
        pnode->left = where->left;
        if (pnode == where->right) {
            fixnodeParent = pnode;
        } else {
            fixnodeParent = pnode->parent;
            if (!fixnode->isNil)
                fixnode->parent = fixnodeParent;
            fixnodeParent->left = fixnode;
            pnode->right = where->right;
            where->right->parent = pnode;
        }
        if (head->parent == where)
            head->parent = pnode;
        else if (where->parent->left == where)
            where->parent->left = pnode;
        else
            where->parent->right = pnode;
        pnode->parent = where->parent;
        uint8_t color = pnode->color;
        pnode->color = where->color;
        where->color = color;
    }

    if (erased->color == kTreeBlack) {
        for (; fixnode != head->parent && fixnode->color == kTreeBlack;
             fixnode = fixnodeParent, fixnodeParent = fixnodeParent->parent) {
            if (fixnode == fixnodeParent->left) {
                EmpAnchorNode *sibling = fixnodeParent->right;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Lrotate(this, fixnodeParent);
                    sibling = fixnodeParent->right;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->left->color == kTreeBlack && sibling->right->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->right->color == kTreeBlack) {
                    sibling->left->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    Rrotate(this, sibling);
                    sibling = fixnodeParent->right;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->right->color = kTreeBlack;
                Lrotate(this, fixnodeParent);
                break;
            } else {
                EmpAnchorNode *sibling = fixnodeParent->left;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    Rrotate(this, fixnodeParent);
                    sibling = fixnodeParent->left;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->right->color == kTreeBlack && sibling->left->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->left->color == kTreeBlack) {
                    sibling->right->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    Lrotate(this, sibling);
                    sibling = fixnodeParent->left;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->left->color = kTreeBlack;
                Rrotate(this, fixnodeParent);
                break;
            }
        }
        fixnode->color = kTreeBlack;
    }

    erased->DestroyValue();
    UMemory::FastFree(erased, sizeof(EmpAnchorNode));
    if (size > 0)
        size--;
    *result = next;
    return result;
}

// FUNC_AT(0x0009cf40)
EmpAnchorMap::InsertResult* EmpAnchorMap::InsertUnique(InsertResult *result, const EmpAnchorGroup *value) {
    EmpAnchorNode *where = head;
    bool addLeft = true;
    for (EmpAnchorNode *node = head->parent; !node->isNil;) {
        where = node;
        addLeft = value->group < node->value.group;
        node = addLeft ? node->left : node->right;
    }
    EmpAnchorNode *before = where;
    if (addLeft) {
        if (where == head->left) {
            EmpAnchorNode *inserted;
            InsertAt(&inserted, true, where, value);
            result->where = inserted;
            result->inserted = true;
            return result;
        }
        before = Previous(before);
    }
    if (before->value.group < value->group) {
        EmpAnchorNode *inserted;
        InsertAt(&inserted, addLeft, where, value);
        result->where = inserted;
        result->inserted = true;
        return result;
    }
    result->where = before;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x0009d0c0)
EmpAnchorNode** EmpAnchorMap::EraseRange(EmpAnchorNode **result, EmpAnchorNode *first, EmpAnchorNode *last) {
    if (first == head->left && last == head) {
        EraseSubtree(head->parent);
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
        *result = head->left;
        return result;
    }
    while (first != last) {
        EmpAnchorNode *where = first;
        first = InlineNext(first);
        EmpAnchorNode *ignored;
        EraseAt(&ignored, where);
    }
    *result = first;
    return result;
}

// FUNC_AT(0x0009d240)
EmpAnchorList* EmpAnchorMap::Lookup(const uint32_t *group) {
    EmpAnchorList empty = { NULL, 0 };
    EmpAnchorGroup value;
    value.group = *group;
    value.list.anchors = NULL;
    value.list.count = 0;
    value.list.Assign(&empty);
    InsertResult inserted;
    InsertUnique(&inserted, &value);
    if (value.list.anchors != NULL)
        OperatorDelete(value.list.anchors);
    return &inserted.where->value.list;
}

// FUNC_AT(0x0009d5f0)
void EmpAnchorMap::Destruct() {
    EmpAnchorNode *ignored;
    EraseRange(&ignored, head->left, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(EmpAnchorNode));
    head = NULL;
    size = 0;
}

// =============================================================================================================
// FindAnchors' count per group: the data layer's tree
// =============================================================================================================

// FUNC_AT(0x0009c310)
TreeNode* SharedTreeMin(TreeNode *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

// FUNC_AT(0x0009c460)
void EmpGroupCountMap::EraseSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x0009c7a0)
TreeNode** EmpGroupCountMap::InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value) {
    return Tree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x0009cc70)
TreeNode** EmpGroupCountMap::EraseAt(TreeNode **result, TreeNode *where) {
    return Tree::EraseAt(result, where);
}

// FUNC_AT(0x0009d000)
TreeInsertResult* EmpGroupCountMap::InsertUnique(TreeInsertResult *result, const TreePair *value) {
    TreeNode *where = head;
    bool addLeft = true;
    for (TreeNode *node = head->parent; !node->isNil;) {
        where = node;
        addLeft = value->tag < node->value.tag;
        node = addLeft ? node->left : node->right;
    }
    SharedTreeIterator before = { where };
    if (addLeft) {
        if (where == head->left) {
            TreeNode *inserted;
            InsertAt(&inserted, true, where, value);
            result->where = inserted;
            result->inserted = true;
            return result;
        }
        before.Dec();
    }
    if (before.node->value.tag < value->tag) {
        TreeNode *inserted;
        InsertAt(&inserted, addLeft, where, value);
        result->where = inserted;
        result->inserted = true;
        return result;
    }
    result->where = before.node;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x0009d180)
TreeNode** EmpGroupCountMap::EraseRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    return Tree::EraseRange(result, first, last);
}

// FUNC_AT(0x0009d960)
void EmpGroupCountMap::Destroy() {
    Tree::Destroy();
}

// =============================================================================================================
// REmpBolts
// =============================================================================================================

// FUNC_AT(0x0009bd50)
void REmpBolts::LoadAttributes() {
    dbattrib_s8("EMP bolts snap to BB", &kSnapToBoundingBox, 0, 1, 0, 1.0f, NULL);
    dbattrib_s8("EMP bolts allow vehicle", &kAllowVehicleBolts, 0, 1, 0, 1.0f, NULL);
    dbattrib_s8("EMP bolts allow world", &kAllowWorldBolts, 0, 1, 0, 1.0f, NULL);
    dbattrib_u8("EMP bolts num alive", &kTargetBoltsAlive, 1, 32, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts scale lifetime", &kScaleLifetime, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts world hit pct", &kHitWorldPct, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts world hit range", &kHitWorldRange, 0.0f, 30.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts world perturb", &kWorldPerturb, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts world flash size", &kWorldFlashSize, 0.0f, 50.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts world arc factor", &kWorldArcFactor, 0.0f, 10.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle arc factor", &kVehicleArcFactor, 0.0f, 50.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle min drift", &kVehicleDriftMin, 0.0f, 50.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle max drift", &kVehicleDriftMax, 0.0f, 10.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle drift var", &kVehicleDriftVariance, 0.0f, 2.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle perturb", &kVehiclePerturb, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle spark rate", &kVehicleSparkRate, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle jolt force", &kVehicleJoltForce, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts vehicle jolt torque", &kVehicleJoltTorque, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_s8("EMP bolts vehicle segments", &kVehicleSegments, 2, 5, 0, 1.0f, NULL);
    dbattrib_s8("EMP bolts vehicle levels", &kVehicleLevels, 1, 4, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts center pull pct", &kCenterPullPct, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_s8("EMP bolts glow enabled", &kGlowEnable, 0, 1, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts glow strength", &kGlowStrength, 0.0f, 50.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts glow amplitude", &kGlowAmplitude, 0.0f, 10.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts glow frequency", &kGlowFrequency, 0.1f, 10.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts glow noise strength", &kGlowNoiseStrength, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts glow noise range", &kGlowNoiseRange, 0.0f, 5.0f, 0, 1.0f, NULL);
    dbattrib_float("EMP bolts glow scatter", &kGlowScatter, 0.0f, 10.0f, 0, 1.0f, NULL);
    dbattrib_argb("EMP bolts glow colour", &kGlowColour, &GlowColourFlag, 0);
}

// FUNC_AT(0x0009dc90)
REmpBolts* REmpBolts::Construct(RSceneObj *vehicle) {
    this->vehicle = vehicle;
    lastUpdateTime = fgSimulation.stepCount;
    startTime = fgSimulation.stepCount;
    intensity = 1.0f;
    anchors = NULL;
    numAnchors = 0;
    // The map's allocator byte is a copy of an unset one in the original; zero here
    groups.allocator = 0;
    groups.head = EmpAnchorMap::BuyHead();
    groups.head->isNil = 1;
    groups.head->parent = groups.head;
    groups.head->left = groups.head;
    groups.head->right = groups.head;
    groups.size = 0;
    numBolts = 0;
    boltCursor = 0;
    FindAnchors();
    return this;
}

// FUNC_AT(0x0009d630)
void REmpBolts::Destruct() {
    vehicle = NULL;
    groups.Destruct();
    if (anchors != NULL)
        OperatorDelete(anchors);
    anchors = NULL;
    numAnchors = 0;
}

// FUNC_AT(0x0009d9a0)
void REmpBolts::FindAnchors() {
    Handle *handle = vehicle->animHandle;
    int effectCount = handle->effectCount;
    ArticleEffect *effects = handle->effects;

    int count = 0;
    for (int i = 0; i < effectCount; i++) {
        if (IsAnchor(&effects[i]))
            count++;
    }
    if (anchors != NULL)
        OperatorDelete(anchors);
    anchors = NULL;
    numAnchors = count;
    anchors = static_cast<EmpAnchor *>(OperatorNewArray(count * sizeof(EmpAnchor)));
    if (count <= 1)
        return;

    EmpAnchor *anchor = anchors;
    for (int i = 0; i < effectCount; i++) {
        const ArticleEffect *effect = &effects[i];
        if (!IsAnchor(effect))
            continue;
        const char *name = effect->locator.name;
        anchor->effect = effect;
        anchor->group = name[3];
        anchor->linkCount = strlen(name) - 4;
        for (uint32_t j = 0; j < anchor->linkCount; j++)
            anchor->links[j] = name[4 + j];
        anchor++;
    }

    EmpGroupCountMap counts;
    counts.allocator = 0;
    counts.Init();
    for (int i = 0; i < numAnchors; i++) {
        TreePair value;
        value.tag = anchors[i].group;
        value.index = 0;
        TreeInsertResult inserted;
        counts.InsertUnique(&inserted, &value);
        inserted.where->value.index++;
    }
    for (TreeNode *node = counts.Begin(); node != counts.End(); node = TreeNext(node)) {
        uint32_t group = node->value.tag;
        int32_t groupCount = node->value.index;
        EmpAnchorList *list = groups.Lookup(&group);
        if (list->anchors != NULL)
            OperatorDelete(list->anchors);
        list->anchors = NULL;
        list->count = groupCount;
        list->anchors = static_cast<EmpAnchor **>(OperatorNewArray(groupCount * sizeof(EmpAnchor *)));
        int filled = 0;
        for (int i = 0; i < numAnchors; i++) {
            if (anchors[i].group == group)
                list->anchors[filled++] = &anchors[i];
        }
    }
    counts.Destroy();
}

// FUNC_AT(0x0009d7c0)
void REmpBolts::Draw() {
    Update();
    if (!kGlowEnable)
        return;

    double radians = double(fgSimulation.stepCount) * fgSimulation.timeStep * kGlowFrequency;
    uint32_t random = RandomShort();
    float wave = GlowWave(radians, kGlowAmplitude, kGlowStrength);
    float noise = float(random * double(kGlowNoiseRange) * kGlowNoiseStrength * kOneOver65536);
    Coord3 position;
    vehicle->GetPosition(&position);
    Coord3 scatter;
    scatter.x = float(Util_GaussRandom());
    scatter.y = float(Util_GaussRandom());
    scatter.z = float(Util_GaussRandom());
    VU0_v4scale(&scatter, kGlowScatter, &scatter);
    VU0_v3add(&position, &scatter, &position);

    Coord4 light = { position.x, position.y, position.z, noise + wave };
    Coord4 colour;
    colour.x = int((kGlowColour >> 16) & 0xff) * kOneOver255;
    colour.y = int((kGlowColour >> 8) & 0xff) * kOneOver255;
    colour.z = int(kGlowColour & 0xff) * kOneOver255;
    colour.w = int(kGlowColour >> 24) * kOneOver255;
    fgLightManager->AddPositionalLight(&light, &colour);
}

// FUNC_AT(0x0009d6b0)
void REmpBolts::Update() {
    uint32_t steps = fgSimulation.stepCount - lastUpdateTime;
    if (steps != 0) {
        uint32_t checks = numBolts < steps ? numBolts : steps;
        if (checks > 8)
            checks = 8;
        for (uint32_t i = 0; i < checks; i++) {
            if (!TheLightning->IsBoltAlive(bolts[boltCursor].handle)) {
                if (numBolts > 1)
                    bolts[boltCursor] = bolts[numBolts - 1];
                numBolts--;
            }
            if (++boltCursor >= numBolts)
                boltCursor = 0;
        }
        uint32_t spawns = steps > 4 ? 4 : steps;
        for (uint32_t i = 0; i < spawns; i++) {
            if (numBolts < kTargetBoltsAlive)
                TrySpawnBolt();
        }
    }
    lastUpdateTime = fgSimulation.stepCount;
}

// FUNC_AT(0x0009d320)
void REmpBolts::TrySpawnBolt() {
    if (numAnchors <= 0)
        return;
    int pick = RandomShort();
    EmpAnchor *from = &anchors[pick * numAnchors >> 16];
    if (from == NULL)
        return;

    if (RandomShort() * kOneOver65536 < kHitWorldPct) {
        // A ray from the car's position, kHitWorldRange long, along the locator's direction from the car's origin,
        // perturbed
        Coord3 position;
        vehicle->GetPosition(&position);
        MATRIX4 locator;
        vehicle->LocateFX(from->effect, &locator, false);
        Coord4 direction;
        VU0_MATRIX4_vect3mult(&kZero, &locator, &direction);
        VU0_v4unitxyz(&direction, &direction);
        Coord3 perturbed;
        Coord3 reach = *Util_PerturbVector(&perturbed, &direction, kWorldPerturb);
        VU0_v4scale(&reach, kHitWorldRange, &reach);
        Coord4 segment[2];
        segment[0] = { position.x, position.y, position.z, 1.0f };
        Coord4 offset = { reach.x, reach.y, reach.z, 1.0f };
        VU0_v4add4(&offset, &segment[0], &segment[1]);      // the end's w is 2

        WorldCollisionInfo hit;
        hit.Construct();
        if (fgCollisionMgr->CheckHitWorld(segment, &hit) > 0) {
            CreateWorldBolt(from, Xyz(&hit.point));
            Shell::ApplyWorldEffects(Xyz(&hit.point), &hit.normal, 1, 0, &hit.faceType, 0, vehicle->physics);
            fgCollisionMgr->CheckHitWindow(&hit, true, 0);
            Coord4 flash = { hit.point.x, hit.point.y, hit.point.z, kWorldFlashSize };
            Coord4 colour = kFlashColour;
            fgLightManager->AddPositionalLight(&flash, &colour);
        }
    } else {
        EmpAnchor *to = GetRandomAnchor(from->group);
        if (to != from)
            CreateVehicleBolt(from, to);
    }
}

// FUNC_AT(0x0009d2d0)
EmpAnchor* REmpBolts::GetRandomAnchor(uint32_t group) {
    EmpAnchorList *list = groups.Lookup(&group);
    if (list->count > 0) {
        int pick = RandomShort();
        return list->anchors[pick * list->count >> 16];
    }
    return NULL;
}

// FUNC_AT(0x0009c0e0)
void REmpBolts::CreateVehicleBolt(EmpAnchor *from, EmpAnchor *to) {
    int pick = RandomShort();
    if (kAllowVehicleBolts) {
        int ticks = pick * fgSimulation.stepsPerSecond >> 16;
        int life = Ftol(ticks * double(kScaleLifetime));
        TheLightning->AddRoundedBolt(vehicle, from->effect, to->effect, kVehicleDriftMin, kVehicleDriftMax,
                                     kVehicleDriftVariance, kVehiclePerturb, kSnapToBoundingBox != 0,
                                     kVehicleSegments, kVehicleLevels, kVehicleArcFactor, life);
    }
    if (RandomShort() * kOneOver65536 < kVehicleSparkRate) {
        MATRIX4 locator;
        vehicle->LocateFX(to->effect, &locator, true);
        Coord4 position;
        VU0_MATRIX4_vect4mult(&kOrigin, &locator, &position);
        // The original subtracts a vector it never sets (whatever its stack holds); zero here
        Coord4 unset = kZero;
        Coord4 direction;
        VU0_v4sub4(&position, &unset, &direction);
        VU0_v4unit(&direction, &direction);
        WorldCollisionInfo hit;
        hit.Construct();
        Shell::ApplyWorldEffects(Xyz(&position), &direction, 1, 0, &hit.faceType, 0, vehicle->physics);
    }
}

// FUNC_AT(0x0009c260)
void REmpBolts::CreateWorldBolt(EmpAnchor *from, const Coord3 *hit) {
    float bulge = kWorldArcFactor;
    // The generator stepped twice for one draw, its first product's bit 8 the sign
    uint32_t product = RandomMultiplier * RandomSeed;
    RandomSeed = RandomMultiplier * product & 0xffff;
    if (product & 0x100)
        bulge *= -1.0f;
    if (kAllowWorldBolts)
        TheLightning->AddBolt(&kZeroPoint, vehicle, from->effect, hit, NULL, NULL,
                              bulge, fgSimulation.stepsPerSecond / 10);
}
