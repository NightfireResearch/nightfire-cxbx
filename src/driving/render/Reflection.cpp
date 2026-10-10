#pragma fp_contract(off)

#include "Reflection.h"

#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../camera/PlayerCamera.h"
#include "../data/DebugVariables.h"
#include "../data/Tuning.h"
#include "../eagl/Model.h"
#include "../eagl/RenderContext.h"
#include "../eagl/Tar.h"
#include "../eagl/View.h"
#include "../engine/GameLoop.h"           // LaunchPage
#include "../engine/UMemory.hpp"
#include "../game/Vehicle.h"           // PVehicle's car names
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"        // MOUSE_setbounds
#include "../platform/RealSystem.h"       // CPU_detect
#include "../platform/X87.h"              // QuietNaN
#include "../world/Render.h"              // WRender, fgRender, CachedDrawInfo
#include "Draw.h"
#include "Fog.h"
#include "Materials.h"
#include "RenderHigh.h"
#include "TextureContext.h"
#include "RGlareManager.hpp"
#include "RSceneObj.hpp"
#include "Renderer.h"

// ---------------------------------------------------------------------------------------------------------------
// FeatureManager and RReflection (0x000980e0-0x00099e20), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

// ---- globals
#define Features (*(FeatureManagerData *)0x001f2d78)
#define CpuType U32_AT(0x001c4810)                      // CPU_detect's (names ours)
#define FeatureDefault50 I32_AT(0x001c4820)             // copied to FeatureManagerData::unknown50
#define WarpageX FLOAT_AT(0x001c482c)                   // 1 (names ours)
#define WarpageY FLOAT_AT(0x001c4830)                   // 1
#define ReflNearZ FLOAT_AT(0x001c4834)                  // 23
#define ReflFarZ FLOAT_AT(0x001c4838)                   // 2500
#define ReflWorldRadius FLOAT_AT(0x001c483c)            // 50: DrawWorldAtPoint's radius
#define ReflDrawWorld I32_AT(0x001c4840)                // 1
#define ReflDrawBack BOOL8_AT(0x001c4844)               // 1: the backwards hemisphere into the sphere map
#define ReflDrawFront BOOL8_AT(0x001c4845)              // 1: the forwards one
#define ReflUpdating BOOL8_AT(0x001f2e00)               // UpdateMaps is running
// "Car to edit": a function-local static, its first value and its guard
#define CarToEdit U32_AT(0x001f2e08)
#define CarToEditInitial U32_AT(0x001f2e0c)
#define CarToEditGuard U32_AT(0x001f2e10)
#define Launch (*(LaunchPage *)0x00243b90)

namespace {

constexpr uintptr_t kRReflectionVtable = 0x001926a4;
constexpr int kMapSize = 256;                   // the offscreen buffers' width and height
constexpr uint32_t kAddressClamp = 3;           // EAGL::TAR's address modes: D3DTADDRESS_CLAMP
constexpr float kFarDistance = 100000000.0f;    // an empty slot's distance
constexpr int kMaxRank = 3;
constexpr int kPrimitiveTriangleStrip = 6;
constexpr float kOneOver255 = 1.0f / 255.0f;
static_assert(std::bit_cast<uint32_t>(kOneOver255) == 0x3b808081, "the original's 1/255");

// Texture tags (FindOrCreateTexture's)
constexpr uint32_t kTextureCref = 0x66657263;   // 'cref'
constexpr uint32_t kTextureWref = 0x66657277;   // 'wref'
constexpr uint32_t kTextureRmsk = 0x6b736d72;   // 'rmsk'
constexpr uint32_t kTextureCarS = 0x53726163;   // 'carS'

// The sphere map's strips: from just off the pole towards the other, a fifth of the height at a time, each strip
// round the full turn in twentieths
constexpr float kPoleOffset = 0.001f;
constexpr float kStripHeight = 0.2f;
constexpr float kStripEnd = 0.9999f;
constexpr float kTurnStep = 0.05f;
constexpr float kTurnsEnd = 1.0001f;
static_assert(std::bit_cast<uint32_t>(kPoleOffset) == 0x3a83126f && std::bit_cast<uint32_t>(kStripEnd) == 0x3f7ff972 &&
              std::bit_cast<uint32_t>(kTurnsEnd) == 0x3f800347, "the strips' constants");

void Clamp(EAGL::TAR *texture) {
    texture->address0 = kAddressClamp;
    texture->addressU = kAddressClamp;
    texture->addressV = kAddressClamp;
    texture->addressW = kAddressClamp;
}

// A texture of the texture context manager's context 4, or of context 5 loaded from ext.xsh if it is not there
EAGL::TAR *ExtensionTexture(uint32_t tag) {
    EAGL::TAR *texture = TheTextureContextManager->FindOrCreateTexture(tag, 4);
    if (texture == NULL) {
        TheTextureContextManager->NewContext("data\\render\\ext.xsh", 5);
        texture = TheTextureContextManager->FindOrCreateTexture(tag, 5);
    }
    return texture;
}

EAGL::TAR *ContextTexture(uint32_t tag) {
    return RTextureContextManager::GetContext(0)->FindOrCreateTexture(tag, 0);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// FeatureManager

// FUNC_AT(0x000980e0)
void FeatureManager::SetTexelsAreOffset(bool, bool) {
    Features.texelsAreOffset[0] = 0;
    Features.texelsAreOffset[1] = 0;
}

// FUNC_AT(0x000980f0)
void FeatureManager::SetUserSpecifiedRenderFeatures() {
    if (Features.locked)
        return;
    CpuType = CPU_detect();
    Features.unknown50 = FeatureDefault50;
    Features.unknown42 = 0;
    Features.unknown78 = 0;
    Features.unknown65 = 0;
    Features.unknown60 = 0x100;
    Features.unknown66 = 1;
    Features.unknown7C = 1;
    Features.unknown67 = 1;
    Features.unknown6C = 7;
    Features.unknown70 = 300;
    Features.unknown74 = 3;
    for (int i = 0; i < 4; i++)
        Features.unknown48[i] = 1;
    Features.unknown44 = 2;
    Features.unknown54 = 400.0f;
    Features.unknown4C = 400.0f;
    Features.unknown64 = 0;
    Features.unknown43 = 1;
    Features.unknown41 = 0;
    Features.unknown40 = 0;
    Features.unknown58 = 8;
    Features.unknown5C = 1;
}

// FUNC_AT(0x000981c0)
void FeatureManager::Init(int width, int height, int depth, int resolutionStamp) {
    Features.resolutionStamp = resolutionStamp;
    Features.width = width;
    Features.height = height;
    Features.depth = depth;
    Features.initialised = 1;
    MOUSE_setbounds(0, 0, 0, width, height);
    SetUserSpecifiedRenderFeatures();
}

// ---------------------------------------------------------------------------------------------------------------
// RReflection's private data

// FUNC_AT(0x00098b60)
RReflection::ReflPrivateData* RReflection::ReflPrivateData::Construct() {
    for (int i = 0; i < 3; i++)
        buffers[i].Construct(kMapSize, kMapSize, 32, true, false);
    states[0].Construct();
    states[1].Construct();
    unknown260 = 1;

    charactersIndex = PVehicle::GetNameCount();
    dynamicObjectsIndex = charactersIndex + 1;
    lightingCount = dynamicObjectsIndex + 1;
    lighting = static_cast<ReflLighting *>(OperatorNewArray(lightingCount * sizeof(ReflLighting)));
    lightingNames = static_cast<const char **>(OperatorNewArray(lightingCount * sizeof(const char *)));
    uint32_t car;
    for (car = 0; car < PVehicle::GetNameCount(); car++)
        lightingNames[car] = PVehicle::GetCarNames()[car];
    lightingNames[car] = "Characters";
    lightingNames[car + 1] = "Dynamic Objects";

    Clamp(buffers[2].colourTarget);
    rmskTexture = ContextTexture(kTextureRmsk);
    carSTexture = ContextTexture(kTextureCarS);
    Clamp(carSTexture);

    for (int i = 0; i < 4; i++) {
        ranks[i] = i;
        distances[i] = kFarDistance;
        objects[i] = NULL;
    }

    for (int i = 0; i < lightingCount; i++) {
        ReflLighting *record = &lighting[i];
        for (int kind = kReflGlass; kind <= kReflDash; kind++) {
            ReflMaterial *material = &record->materials[kind];
            material->ambient = 0.5f;
            material->diffuse = 0.5f;
            material->reflect = 0.8f;
            material->specular = 1.0f;
            material->ambient *= 0.5f;
            material->diffuse *= 0.5f;
        }
        record->materials[kReflChrome].ambient = 0.0f;
        record->materials[kReflChrome].diffuse = 0.0f;
        record->materials[kReflChrome].reflect = 1.0f;
        record->materials[kReflGlossy].ambient = 0.52f;
        record->materials[kReflGlossy].diffuse = 0.2f;
        record->materials[kReflGlossy].reflect = 0.2f;
        record->materials[kReflDull].ambient = 0.2f;
        record->materials[kReflDull].diffuse = 0.2f;
        record->fresnel = 0.3f;
        record->reflectionWarp = 0.485f;
        record->specularSpot = 3.0f;
        record->specularStrength = 1.0f;
        record->spherifyNormals = 0.4f;
    }
    ReflLighting *characters = &lighting[charactersIndex];
    characters->materials[kReflGlossy].ambient = 1.0f;
    characters->materials[kReflGlossy].diffuse = 1.0f;
    characters->materials[kReflDull].ambient = 1.0f;
    characters->materials[kReflDull].diffuse = 1.0f;

    eyeHeight = 4.0f;
    unknown10 = 120.0f;
    // The loop sets the same fields every time round
    for (int i = 0; i < lightingCount; i++) {
        skyBright = 0xdf;
        unknown02 = 0x18;
        unknown04 = 0x78787878;
        unknown08 = 1;
        unknown09 = 1;
    }

    TuningDBMgr->LoadDatabase("Render:CarRender", Launch.missionName, 0, false);
    dbattrib_f("Sky bright", reinterpret_cast<char *>(&skyBright), 0, 0xff, 0, 1.0f, NULL);
    if (!(CarToEditGuard & 1)) {
        CarToEditGuard |= 1;
        CarToEdit = CarToEditInitial;
    }
    dbindex("Car to edit", &CarToEdit, 0, lightingCount - 1, lightingNames);
    const uint32_t stride = sizeof(ReflLighting);
    dbattrib_float("Fresnel", &lighting->fresnel, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("Reflection Warp", &lighting->reflectionWarp, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("Spherify Normals", &lighting->spherifyNormals, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("SpecularSpot", &lighting->specularSpot, 0.0f, 3.0f, stride, 1.0f, NULL);
    dbattrib_float("[Paint]Diffuse", &lighting->materials[kReflGlossy].diffuse, 0.0f, 2.0f, stride, 1.0f, NULL);
    dbattrib_float("[Paint]Ambient", &lighting->materials[kReflGlossy].ambient, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[Paint]Reflect", &lighting->materials[kReflGlossy].reflect, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[Paint]Specular", &lighting->materials[kReflGlossy].specular, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[Dull]Diffuse", &lighting->materials[kReflDull].diffuse, 0.0f, 2.0f, stride, 1.0f, NULL);
    dbattrib_float("[Dull]Ambient", &lighting->materials[kReflDull].ambient, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[SpecBump]Diffuse", &lighting->materials[kReflSpecular].diffuse, 0.0f, 2.0f, stride, 1.0f,
                   NULL);
    dbattrib_float("[SpecBump]Ambient", &lighting->materials[kReflSpecular].ambient, 0.0f, 1.0f, stride, 1.0f,
                   NULL);
    dbattrib_float("[Glass]Diffuse", &lighting->materials[kReflGlass].diffuse, 0.0f, 2.0f, stride, 1.0f, NULL);
    dbattrib_float("[Glass]Ambient", &lighting->materials[kReflGlass].ambient, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[Glass]Reflect", &lighting->materials[kReflGlass].reflect, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[Chrome]Diffuse", &lighting->materials[kReflChrome].diffuse, 0.0f, 2.0f, stride, 1.0f, NULL);
    dbattrib_float("[Chrome]Ambient", &lighting->materials[kReflChrome].ambient, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[Chrome]Reflect", &lighting->materials[kReflChrome].reflect, 0.0f, 1.0f, stride, 1.0f, NULL);
    dbattrib_float("[Chrome]Specular", &lighting->materials[kReflChrome].specular, 0.0f, 1.0f, stride, 1.0f,
                   NULL);
    dbendindex();
    TuningDBMgr->CloseCurrent();

    // Whatever the tuning said
    for (int i = 0; i < lightingCount; i++)
        lighting[i].reflectionWarp = 0.485f;
    return this;
}

// The records go with delete, not delete[], and the names are not freed: kept as the original has them.
// FUNC_AT(0x000982a0)
void RReflection::ReflPrivateData::Destruct() {
    OperatorDelete(lighting);
    states[1].Destruct();
    states[0].Destruct();
    for (int i = 2; i >= 0; i--)
        buffers[i].Destruct();
}

// ---------------------------------------------------------------------------------------------------------------
// RReflection: construction and the textures

// FUNC_AT(0x000994c0)
RReflection* RReflection::Construct() {
    RViewCamera::Construct(NULL);
    vtable = reinterpret_cast<void **>(kRReflectionVtable);
    ReflPrivateData *data =
        static_cast<ReflPrivateData *>(UMemory::FastAlloc(sizeof(ReflPrivateData), "RReflection::ReflPrivateData"));
    privateData = data != NULL ? data->Construct() : NULL;
    EnableReflectionMapWarpage(false);
    privateData->savedViewPort = viewPort;
    farthestDistance = kFarDistance;
    return this;
}

// FUNC_AT(0x00099590)
void RReflection::Destruct() {
    vtable = reinterpret_cast<void **>(kRReflectionVtable);
    ReflPrivateData *data = privateData;
    viewPort = data->savedViewPort;
    if (data != NULL) {
        data->Destruct();
        UMemory::FastFree(data, sizeof(ReflPrivateData));
    }
    RViewCamera::Destruct();
}

// FUNC_AT(0x00099cc0)
RReflection* RReflection::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RReflection));
    return this;
}

// FUNC_AT(0x00099c50)
void RReflection::Init() {
    RReflection *reflection = static_cast<RReflection *>(UMemory::FastAlloc(sizeof(RReflection), "RReflection"));
    TheReflection = reflection != NULL ? reflection->Construct() : NULL;
}

// FUNC_AT(0x00098340)
void RReflection::Kill() {
    RReflection *reflection = TheReflection;
    if (reflection != NULL)
        (reflection->*XbeVirtual<decltype(&RReflection::Delete)>(reflection, 0))(1);
    TheReflection = NULL;
}

// FUNC_AT(0x000993e0)
void RReflection::InitPostSim() {
    privateData->crefTexture = ExtensionTexture(kTextureCref);
    Clamp(privateData->crefTexture);
    privateData->wrefTexture = ExtensionTexture(kTextureWref);
    Clamp(privateData->wrefTexture);
}

// ---------------------------------------------------------------------------------------------------------------
// What the car render methods read

// FUNC_AT(0x00098210)
Coord4* RReflection::GetReflectionMapWarpageData() {
    return &privateData->warpage;
}

// FUNC_AT(0x00098220)
void RReflection::EnableReflectionMapWarpage(bool enable) {
    Coord4 warpage;
    warpage.z = 0.0f;
    if (enable) {
        warpage.x = WarpageX;
        warpage.y = WarpageY;
        warpage.w = WarpageY + 1.0f;
    } else {
        warpage.x = 1.0f;
        warpage.y = 0.0f;
        warpage.w = 1.0f;
    }
    privateData->warpage = warpage;
}

// FUNC_AT(0x00098360)
void RReflection::SetReflectiveSpecularStrength(int index, float strength) {
    privateData->lighting[index].specularStrength = QuietNaN(strength);   // moved through the x87
}

// FUNC_AT(0x00098380)
ReflMaterial* RReflection::LightingProps(int kind, const char *name) {
    int index;
    if (CRT_stricmp(name, "Character") == 0)
        index = privateData->charactersIndex;
    else if (CRT_stricmp(name, "Dynamic Objects") == 0)
        index = privateData->dynamicObjectsIndex;
    else
        index = PVehicle::NameToIndex(name);
    return &privateData->lighting[index].materials[kind];
}

// FUNC_AT(0x00098410)
float* RReflection::GetReflectionData2(const char *name) {
    int index;
    if (CRT_stricmp(name, "Character") == 0)
        index = privateData->charactersIndex;
    else
        index = PVehicle::NameToIndex(name);
    return &privateData->lighting[index].fresnel;
}

// FUNC_AT(0x00098470)
Coord3* RReflection::GetReflectionCarPos() {
    return &privateData->carPosition;
}

// FUNC_AT(0x00098480)
EAGL::TAR* RReflection::TextureWeaponEnvMap() {
    return privateData->wrefTexture;
}

// FUNC_AT(0x00098490)
EAGL::TAR* RReflection::TextureSpecular() {
    return privateData->carSTexture;
}

// FUNC_AT(0x000984a0)
EAGL::TAR* RReflection::Texture() {
    return privateData->buffers[2].colourTarget;
}

// FUNC_AT(0x000984b0)
MATRIX4* RReflection::GetReflectionMatrix() {
    return &privateData->reflectionMatrix;
}

// FUNC_AT(0x00099610)
void RReflection::SetReflectivity(RSceneObj *object) {
    int index = object->renderTypeIndex;
    Coord3 position;
    object->GetPosition(&position);
    privateData->carPosition = position;
    privateData->spherifyNormals = privateData->lighting[index].spherifyNormals;
}

// ---------------------------------------------------------------------------------------------------------------
// The scene objects drawn into the reflection. The four slots keep ranks 0..3 by distance; a new object takes the
// slot ranked 3 and every slot not nearer than it moves down a rank. The original does not check that a slot
// ranked 3 was found.

// FUNC_AT(0x00098520)
void RReflection::PrivateSubmitSceneObj(float distance, RSceneObj *object) {
    ReflPrivateData *data = privateData;
    for (int i = 0; i < 4; i++) {
        if (data->objects[i] == object)
            return;
    }
    int slot = -1;
    int nearer = 0;
    for (int i = 0; i < 4; i++) {
        if (data->ranks[i] == kMaxRank)
            slot = i;
        if (distance > data->distances[i])
            nearer++;
        else
            data->ranks[i]++;
    }
    data->ranks[slot] = nearer;
    data->distances[slot] = distance;
    data->objects[slot] = object;
    farthestDistance = distance;
    for (int i = 0; i < 4; i++) {
        if (data->distances[i] > farthestDistance)
            farthestDistance = data->distances[i];
    }
}

// FUNC_AT(0x000986a0)
void RReflection::DeregisterSceneObj(RSceneObj *object) {
    ReflPrivateData *data = privateData;
    int slot = -1;
    int rank = 0;
    for (int i = 0; i < 4; i++) {
        if (object == data->objects[i]) {
            rank = data->ranks[i];
            slot = i;
        }
    }
    if (slot == -1)
        return;
    for (int i = 0; i < 4; i++) {
        if (data->ranks[i] > rank)
            data->ranks[i]--;
        if (data->ranks[i] == 0)
            farthestDistance = data->distances[i];
    }
    data->ranks[slot] = kMaxRank;
    data->distances[slot] = kFarDistance;
    data->objects[slot] = NULL;
    for (int i = 0; i < 4; i++) {
        if (data->distances[i] > farthestDistance)
            farthestDistance = data->distances[i];
    }
}

// FUNC_AT(0x00098850)
void RReflection::SceneObjRender(EAGL::ViewPort *) {
    typedef void (RSceneObj::*RenderSimple)();
    bool vehiclesAllowed = RRenderSharedData::SetVehiclesAllowed(true);
    for (int i = 0; i < 4; i++) {
        RSceneObj *object = privateData->objects[i];
        if (object != NULL)
            (object->*XbeVirtual<RenderSimple>(object, 3))();
    }
    RRenderSharedData::SetVehiclesAllowed(vehiclesAllowed);
}

// FUNC_AT(0x000988a0)
void RReflection::ResetSceneObjDistances() {
    ReflPrivateData *data = privateData;
    farthestDistance = 0.0f;
    RSceneObj *objects[4];
    for (int i = 0; i < 4; i++)
        objects[i] = data->objects[i];
    for (int i = 0; i < 4; i++) {
        data->ranks[i] = i;
        data->distances[i] = kFarDistance;
        data->objects[i] = NULL;
    }
    farthestDistance = kFarDistance;
    for (int i = 0; i < 4; i++) {
        if (objects[i] != NULL)
            PrivateSubmitSceneObj(float(objects[i]->GetViewDistance()), objects[i]);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing the maps

// FUNC_AT(0x000984c0)
void RReflection::Begin(ROffscreenBuffer *buffer) {
    buffer->Begin();
    viewPort = buffer->viewPort;
    // The original sets the view through a one-line setter the linker shared with FnDefaultAnimBank::Init
    fgRenderer->currentView = this;
    active = 1;
    fgRenderer->cameraPosition = *MatrixRow(&camera->matrix, 3);
    fgRenderer->cameraPosition.w = 1.0f;
}

// FUNC_AT(0x00099670)
void RReflection::SetupView(const Coord3 *eye, const MATRIX4 *frame, float side) {
    viewPort->SetOrthographicScreenSpace(1.0f, ReflNearZ, ReflFarZ);
    MATRIX4 view = *frame;
    for (int i = 0; i < 3; i++)
        view.mtx[2][i] *= side;
    view.mtx[3][0] = eye->x;
    view.mtx[3][1] = eye->y;
    view.mtx[3][2] = eye->z;
    for (int i = 0; i < 3; i++)
        view.mtx[0][i] *= side;
    view.mtx[3][1] += privateData->eyeHeight;
    camera->SetMatrix4(&view);

    RCamera mirrored;
    mirrored.ConstructCopy(camera);
    mirrored.ConvertToRHCS();
    mirrored.CreateMatrix4Inv();
    if (side != 0.0f)
        privateData->reflectionMatrix = mirrored.inverse;
    privateData->viewDirection = *MatrixRow(&view, 2);
    viewPort->SetViewMatrix(&mirrored.inverse.mtx[0][0]);
}

// FUNC_AT(0x000997d0)
void RReflection::RenderView(float side, const MATRIX4 *frame, const Coord3 *eye) {
    CachedDrawInfo draws;
    SetupView(eye, frame, side);
    FogParams *fog = Fog->params;
    float savedStart = fog->start, savedEnd = fog->end, savedDensity = fog->density;
    uint32_t savedMode = fog->mode;
    fog->density = 0.0f;
    fog->mode = std::bit_cast<uint32_t>(4080.0f);  // the original stores a float there
    RRenderSharedData::SendPerViewPort();
    TheGlareManager->enabled = false;
    EnableReflectionMapWarpage(true);
    fgRenderer->renderContext->SetZWritesEnable(1);
    if (ReflDrawWorld != 0) {
        fgRender->DrawWorldAtPoint(&draws, eye, ReflWorldRadius, privateData->skyBright * kOneOver255,
                                   privateData->viewDirection);
        SceneObjRender(viewPort);
    }
    fgRenderer->FlushDrawLists();
    TheGlareManager->enabled = true;
    EnableReflectionMapWarpage(false);
    fog->start = savedStart;
    fog->end = savedEnd;
    fog->density = savedDensity;
    fog->mode = savedMode;
}

// FUNC_AT(0x00099ce0)
void RReflection::UpdateMaps(const Coord3 *eye) {
    if (ReflUpdating)
        return;
    ReflUpdating = 1;
    MATRIX4 frame = fgRenderHigh->views[0].view->camera->matrix;
    for (int side = 0; side < 2; side++) {
        Begin(&privateData->buffers[side]);
        RenderView(side == 0 ? 1.0f : -1.0f, &frame, eye);
        active = 0;
        fgRenderer->EndView();
        (&privateData->buffers[side])->End();
    }
    Begin(&privateData->buffers[2]);
    ReflMapPair maps = { { privateData->buffers[0].colourTarget, privateData->buffers[1].colourTarget } };
    RViewCamera::SetViewPortToUnitTransformMode(viewPort, 0.5f, 10.0f);
    maps.DrawSphereMap();
    active = 0;
    fgRenderer->EndView();
    (&privateData->buffers[2])->End();
    ReflUpdating = 0;
}

// FUNC_AT(0x00098980)
void ReflSphereMapCoords(const Coord4 *direction, Coord4 *out) {
    Coord4 unit = {};
    VU0_v4unitxyz(direction, &unit);
    double z = double(unit.z) + 1.0;
    float lengthSquared = float(z * z + double(unit.y) * unit.y + double(unit.x) * unit.x);
    double scale = 1.0 / (2.0 * sqrt(double(lengthSquared)));   // REAL_sqrtf's FSQRT, unrounded
    out->z = 1.0f;
    out->w = 1.0f;
    out->x = float(unit.x * scale + 0.5);
    out->y = float(scale * unit.y + 0.5);
}

// FUNC_AT(0x00098a00)
void ReflHemisphereCoords(const Coord4 *direction, Coord4 *out, bool *front) {
    Coord4 unit = {};
    VU0_v4unitxyz(direction, &unit);
    if (unit.z <= 0.0f) {
        double scale = 1.0 / (1.0 - unit.z);
        out->x = float(unit.x * scale);
        out->y = float(scale * unit.y);
        *front = true;
    } else {
        double scale = 1.0 / (unit.z + 1.0);
        out->x = float(-(unit.x * scale));
        out->y = float(scale * unit.y);
        *front = false;
    }
    out->z = 1.0f;
    out->w = 1.0f;
    out->x = float((out->x + 1.0) * 0.5);
    out->y = float((out->y + 1.0) * 0.5);
}

// FUNC_AT(0x00098ab0)
void ReflMapVertex(float height, float turns, bool flip, Coord4 *position, Coord4 *uv) {
    if (height >= 1.0f)
        height = kStripEnd;
    if (height <= -1.0f)
        height = -kStripEnd;
    Coord4 direction;
    direction.z = (flip ? -1.0f : 1.0f) - height;
    direction.x = float(SineTurns(turns) * height);
    direction.y = float(CosineTurns(turns) * height);
    direction.w = 0.0f;
    Coord4 unit = {};
    VU0_v4unitxyz(&direction, &unit);
    ReflSphereMapCoords(&unit, position);
    ReflHemisphereCoords(&unit, uv, &flip);     // into the argument's slot, as the original does
}

// FUNC_AT(0x00099990)
void ReflMapPair::DrawSphereMap() {
    uint32_t colours[256];
    Coord4 positions[256];
    Coord4 uvs[256];
    for (int i = 0; i < 256; i++)
        colours[i] = 0xffffffff;
    USimpleTexturedMaterial material;
    material.Construct();

    // The first hemisphere's strips go down from -0.001, the second's up from 0.001
    for (int half = 0; half < 2; half++) {
        bool first = half == 0;
        if (!(first ? ReflDrawFront : ReflDrawBack))
            continue;
        float height = first ? -kPoleOffset : kPoleOffset;
        for (;;) {
            float next = first ? height - kStripHeight : height + kStripHeight;
            int count = 0;
            float turns = 0.0f;
            double nextTurns;
            do {
                ReflMapVertex(height, turns, first, &positions[count], &uvs[count]);
                ReflMapVertex(next, turns, first, &positions[count + 1], &uvs[count + 1]);
                count += 2;
                nextTurns = double(turns) + kTurnStep;     // compared before it is rounded
                turns = float(nextTurns);
            } while (nextTurns <= kTurnsEnd);
            TexturedGeoPrim *request = TexturedRequests[TexturedRequestIndex];
            request->texture.SetData(hemispheres[half]);
            request->positions.SetData(positions);
            request->colours.SetData(colours);
            request->texCoords.SetData(uvs);
            material.Draw(kPrimitiveTriangleStrip, count, NULL);
            height = next;
            if (first ? !(height > -kStripEnd) : !(height < kStripEnd))
                break;
        }
    }
    material.Destruct();
}
