#pragma fp_contract(off)

#include "ShadowMap.h"

#include <math.h>
#include <string.h>

#include "Materials.h"
#include "Renderer.h"                     // RRenderer, fgRenderer
#include "RSceneObj.hpp"
#include "TextureContext.h"
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../data/DebugVariables.h"
#include "../eagl/GeoPrimState.h"
#include "../eagl/RenderContext.h"
#include "../eagl/Tar.h"
#include "../eagl/View.h"
#include "../engine/StaticInit.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"       // Abs
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../game/Vehicle.h"           // PVehicle's car names
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"

// ---------------------------------------------------------------------------------------------------------------
// RShadowMap (0x000a5820-0x000a6690), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

class RTextureContext;

// ---- the game's code not ported here
#define RVehicle_RenderShadowGeometry ((void (__fastcall *)(RSceneObj *, int))0x00096410)
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)

// ---- globals
#define Sim ((void *)0x00233ff0)                            // the Simulation
#define CarShadowSizes (*(CarShadowSize **)0x00201768)      // one per car name, made by the first LoadAttributes
#define CarToEdit U32_AT(0x00201784)                        // "Car to edit"
#define ProjectedShadows (*(bool *)0x001c5078)              // "Enable projected shadows"
#define PS2ShadowStrength I32_AT(0x001c507c)                // "PS2 shadow strength"
// The render methods' constants that LookupVariable answers
#define ShadowFatnessConstants ((void *)0x00201770)
#define ShadowPlaneConstants ((void *)0x001c5080)
#define ZeroOneTwoThree ((void *)0x001c5094)
// The simple shadow's material: a function-local static USimpleTexturedMaterial, made on first use
#define ShadowMaterial ((USimpleTexturedMaterial *)0x00201790)
#define ShadowMaterialMade U32_AT(0x002017f0)
#define OffOnNames ((const char *const *)0x001b6638)        // "Off", "On"

static void **const kShadowMapVtable = (void **)0x00192fd4;
static const USingletonVtable *const kShadowMapSingletonVtable = (const USingletonVtable *)0x00192fc8;
static const USingletonVtable *const kSingletonVtable = (const USingletonVtable *)0x0018beb0;

constexpr uint32_t kShadowTexture = 0x64687363;     // 'cshd'
constexpr int kTextureSize = 0x200;
constexpr int kTextureDepth = 0x10;
constexpr uint32_t kAddressClamp = 3;
constexpr uint32_t kClearAll = 7;                   // colour, depth and stencil
constexpr uint32_t kBackground = 0x7fffffff;
constexpr int32_t kVehicleType = 1;                 // PhysicsObject::type of the cars that get a simple shadow
constexpr float kDefaultShadowSize = 0.8f;
constexpr int32_t kDefaultPS2ShadowStrength = 0x18;
constexpr float kBoxMargin = 0.15f;
constexpr float kViewDistance = -20.0f;
constexpr int kLeverMethod = 61;                    // PhysicsObject's vtable slot: a lever's height (the name is ours)

// OpenGL's blend factors and equation, as GeoPrimStateExtension::SetAlphaBlend takes them
constexpr uint32_t kGlDstColor = 0x306;
constexpr uint32_t kGlOneMinusSrcAlpha = 0x303;
constexpr uint32_t kGlFuncAdd = 0x8006;

// A lever's height, the physics object's virtual method; unrounded
static double LeverHeight(PhysicsObject *physics, int lever) {
    typedef double (PhysicsObject::*LeverHeightMethod)(int lever);
    return (physics->*XbeVirtual<LeverHeightMethod>(physics, kLeverMethod))(lever);
}

// FUNC_AT(0x000a5820)
void RShadowMap::LoadAttributes() {
    if (CarShadowSizes == NULL)
        CarShadowSizes = static_cast<CarShadowSize *>(OperatorNewArray(PVehicle::GetNameCount() * sizeof(CarShadowSize)));
    for (uint32_t car = 0; car < PVehicle::GetNameCount(); car++) {
        CarShadowSizes[car].width = kDefaultShadowSize;
        CarShadowSizes[car].length = kDefaultShadowSize;
    }
    PS2ShadowStrength = kDefaultPS2ShadowStrength;
    const char *const *carNames = PVehicle::GetCarNames();
    uint32_t lastCar = PVehicle::GetNameCount() - 1;
    dbindex("Car to edit", &CarToEdit, 0, lastCar, carNames);
    dbattrib_float("Shadow width", &CarShadowSizes[0].width, 0.0f, 2.0f, sizeof(CarShadowSize), 1.0f, NULL);
    dbattrib_float("Shadow length", &CarShadowSizes[0].length, 0.0f, 2.0f, sizeof(CarShadowSize), 1.0f, NULL);
    dbendindex();
    dbattrib_bool("Enable projected shadows", &ProjectedShadows, 0, 1, 0, -1.0f, OffOnNames);
    dbattrib_s8("PS2 shadow strength", &PS2ShadowStrength, 0, 0xff, 0, 1.0f, NULL);
}

// FUNC_AT(0x000a5930)
void RShadowMap::SetupFrameBuffers() {
    data->context->SetupFrameBuffers(data->colourTarget, data->depthTarget);
}

// FUNC_AT(0x000a5950)
bool RShadowMap::ProjectedShadowsEnabled() {
    return ProjectedShadows;
}

// FUNC_AT(0x000a5960)
void RShadowMap::ClearShadowMap() {
    ShadowPrivateData *shadow = TheShadowMap->data;
    shadow->context->SetupFrameBuffers(shadow->colourTarget, shadow->depthTarget);
    data->context->BeginFrame();
    fgRenderer->SetCurrentView(this);
    active = 1;
    viewPort->BeginView();
    viewPort->ClearViewPort(kClearAll);
    EndView();
    data->context->EndFrame();
}

// FUNC_AT(0x000a59c0)
void* RShadowMap::LookupVariable(const char *name, bool *found) {
    void *variable = NULL;
    if (strcmp(name, "GAME::ShadowFatnessConstants") == 0)
        variable = ShadowFatnessConstants;
    if (strcmp(name, "GAME::ShadowPlaneConstants") == 0)
        variable = ShadowPlaneConstants;
    if (strcmp(name, "GAME::ZeroOneTwoThree") == 0)
        variable = ZeroOneTwoThree;
    RShadowMap *shadowMap = TheShadowMap;
    if (strcmp(name, "GAME::ShadowTexture") == 0)
        variable = shadowMap->data->colourTarget;
    if (strcmp(name, "GAME::ShadowMatrix") == 0)
        variable = &shadowMap->data->matrix;
    *found = variable != NULL;
    return variable;
}

// FUNC_AT(0x000a5a50)
ShadowPrivateData* ShadowPrivateData::Construct() {
    colourTarget = EAGL_TARRenderTarget(kTextureSize, kTextureSize, kTextureDepth, 1);
    depthTarget = EAGL_TARDepthSurface(kTextureSize, kTextureSize, kTextureDepth);
    colourTarget->address0 = kAddressClamp;
    colourTarget->addressU = kAddressClamp;
    colourTarget->addressV = kAddressClamp;
    colourTarget->addressW = kAddressClamp;
    context = fgRenderer->device->Extension()->NewTextureRenderContext();
    context->SetupFrameBuffers(colourTarget, depthTarget);
    return this;
}

// FUNC_AT(0x000a5ac0)
RShadowMap* RShadowMap::Construct() {
    RViewCamera::Construct(NULL);
    singleton.base.vtable = kSingletonVtable;
    vtable = kShadowMapVtable;
    singleton.base.vtable = kShadowMapSingletonVtable;
    ShadowPrivateData *made = static_cast<ShadowPrivateData *>(OperatorNew(sizeof(ShadowPrivateData)));
    data = made != NULL ? made->Construct() : NULL;
    data->texture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kShadowTexture, 0);
    data->texture->address0 = kAddressClamp;
    data->texture->addressU = kAddressClamp;
    data->texture->addressV = kAddressClamp;
    data->texture->addressW = kAddressClamp;
    savedViewPort = viewPort;
    viewPort = data->context->NewViewPort();
    viewPort->SetShape(0.0f, 0.0f, (float)kTextureSize, (float)kTextureSize, 0.0f, 1.0f);
    viewPort->SetBackgroundColour(kBackground);
    viewPort->SetOrthographicScreenSpace(1.0f, 1.0f, 100.0f);
    return this;
}

// FUNC_AT(0x000a5be0)
void RShadowMap::Kill() {
    RShadowMap *shadowMap = TheShadowMap;
    if (shadowMap != NULL) {
        typedef RShadowMap *(RShadowMap::*DeletingDestructor)(unsigned flags);
        (shadowMap->*XbeVirtual<DeletingDestructor>(shadowMap, 0))(1);
    }
}

// FUNC_AT(0x000a5c00)
RShadowMap* ShadowMapSingleton::Delete(unsigned flags) {
    RShadowMap *shadowMap = reinterpret_cast<RShadowMap *>(reinterpret_cast<uint8_t *>(this) - sizeof(RViewCamera));
    return shadowMap->Delete(flags);
}

// FUNC_AT(0x000a5c10)
void RShadowMap::Destruct() {
    vtable = kShadowMapVtable;
    singleton.base.vtable = kShadowMapSingletonVtable;
    data->context->DeleteViewPort(viewPort);
    viewPort = savedViewPort;
    if (data != NULL)
        OperatorDelete(data);
    singleton.base.vtable = kSingletonVtable;
    RViewCamera::Destruct();
}

// FUNC_AT(0x000a5c90)
void RShadowMap::SetCamera(RSceneObj *vehicle, const Coord4 *direction) {
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, vehicle->physics->rigidBodySlot);
    alignas(16) MATRIX4 orientation = body->info->orientation;

    // A frame looking along the direction, up as near the world's as it can be, turned into the world's
    alignas(16) MATRIX4 view;
    const Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
    view.mtx[2][0] = direction->x;
    view.mtx[2][1] = direction->y;
    view.mtx[2][2] = direction->z;
    VU0_v4unitcrossprodxyz(&up, direction, view.mtx[0]);
    VU0_v4unitcrossprodxyz(direction, view.mtx[0], view.mtx[1]);
    view.mtx[3][0] = 0.0f;
    view.mtx[3][1] = 0.0f;
    view.mtx[3][2] = 0.0f;
    view.mtx[0][3] = 0.0f;
    view.mtx[1][3] = 0.0f;
    view.mtx[2][3] = 0.0f;
    view.mtx[3][3] = 1.0f;
    VU0_MATRIX4_transpose(&view, &view);
    data->matrix = view;
    VU0_MATRIX4_mult(&view, &orientation, &view);

    // The car's box seen along it: the largest of its corners' distances across and up
    Coord4 corner;
    vehicle->GetBoundingDimensions(&corner);
    float widest = 0.0f, highest = 0.0f;
    for (int i = 0; i < 2; i++) {
        corner.x = -corner.x;
        for (int j = 0; j < 2; j++) {
            corner.y = -corner.y;
            for (int k = 0; k < 2; k++) {
                corner.z = -corner.z;
                Coord4 seen;
                TransformPoint(&view, &corner, &seen);
                float across = Abs(seen.x);
                float upward = Abs(seen.y);
                if (across > widest)
                    widest = across;
                if (upward > highest)
                    highest = upward;
            }
        }
    }

    // Fitted to it, and back from the car
    float scaleX = (float)(1.0 / ((double)widest + kBoxMargin));
    float scaleY = (float)(1.0 / ((double)highest + kBoxMargin));
    alignas(16) MATRIX4 step;
    BuildScaleXYZ(&step, scaleX, scaleY, 1.0f);
    VU0_MATRIX4_mult(&view, &view, &step);
    BuildTranslate(&step, 0.0f, 0.0f, kViewDistance);
    VU0_MATRIX4_mult(&view, &view, &step);
    viewPort->SetViewMatrix(&view.mtx[0][0]);

    // The texture's matrix: the world from the car's position, through the same frame and fit, into 0..1
    body = Simulation_GetRigidBody(Sim, 0, vehicle->physics->rigidBodySlot);
    BuildScaleXYZ(&step, scaleX, scaleY, 1.0f);
    const Coord3 *position = &body->position;
    VU0_MATRIX4_mult(&data->matrix, &data->matrix, &step);
    BuildTranslate(&step, -position->x, -position->y, -position->z);
    VU0_MATRIX4_mult(&data->matrix, &step, &data->matrix);
    BuildScaleXYZ(&step, 0.5f, -0.5f, 1.0f);
    VU0_MATRIX4_mult(&data->matrix, &data->matrix, &step);
    BuildTranslate(&step, 0.5f, 0.5f, 0.0f);
    VU0_MATRIX4_mult(&data->matrix, &data->matrix, &step);
}

// FUNC_AT(0x000a6050)
void RShadowMap::Begin(RSceneObj *vehicle, const Coord4 *direction) {
    ShadowPrivateData *shadow = TheShadowMap->data;
    shadow->context->SetupFrameBuffers(shadow->colourTarget, shadow->depthTarget);
    data->context->BeginFrame();
    fgRenderer->SetCurrentView(this);
    active = 1;
    viewPort->BeginView();
    viewPort->ClearViewPort(kClearAll);
    SetCamera(vehicle, direction);
}

// FUNC_AT(0x000a60b0)
void RShadowMap::Draw(RSceneObj *vehicle, const Coord4 *direction) {
    Begin(vehicle, direction);
    RVehicle_RenderShadowGeometry(vehicle, 0);
    EndView();
    data->context->EndFrame();
}

// FUNC_AT(0x000a60f0)
void RShadowMap::DrawSimpleShadow(RSceneObj *vehicle) {
    bool alphaWrites = fgRenderer->DisableAlphaWrites();
    const Coord4 uvs[4] = {
        { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
    };
    if ((ShadowMaterialMade & 1) == 0) {
        ShadowMaterialMade |= 1;
        ShadowMaterial->Construct();
        CRT_atexit(DestroyStatic_00201790);
    }
    ShadowMaterial->SetTransparencyMethod(1);
    ShadowMaterial->Extension()->SetAlphaBlend(kGlDstColor, kGlOneMinusSrcAlpha, kGlFuncAdd);

    Coord4 corners[4];
    uint32_t colours[4];
    TexturedGeoPrim *request = TexturedRequests[TexturedRequestIndex];
    request->positions.SetData(corners);
    request->texCoords.SetData(uvs);
    request->texture.SetData(data->texture);

    PhysicsObject *physics = vehicle->physics;
    if (physics->type != kVehicleType)
        return;
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, physics->rigidBodySlot);
    RigidBodyInfo *info = body->info;
    if (info->ground.y < 0.1f)
        return;

    // The wheels on the ground: their places, how far the lowest has dropped below its lever, and the shadow's
    // height, a little above their average
    Coord4 centre = { body->position.x, 0.0f, body->position.z, 1.0f };
    float drop = 0.0f;
    Coord4 wheels[4];
    for (int i = 0; i < 4; i++) {
        VU0_v3add(&info->worldLevers[i], &body->position, &wheels[i]);
        double height = LeverHeight(physics, i);
        float below = (float)-height;
        drop = drop < below ? below : drop;
        double y = height + wheels[i].y;
        wheels[i].w = 1.0f;
        wheels[i].y = (float)y;
        centre.y = (float)(y + centre.y);
    }
    float limited = 2.0f < drop ? 2.0f : drop;
    float strength = (float)((2.0 - limited) * 0.5 * 255.0);
    int level = RoundToInt(strength);
    if (level < 1)
        return;
    centre.y = (float)(centre.y * 0.25 + 0.1f);

    // The quad's half-axes from the wheels, given up when the car leans too far
    Coord4 across, along, rightSide, rearSide;
    VU0_v4sub4(&wheels[1], &wheels[0], &across);
    VU0_v4sub4(&wheels[2], &wheels[0], &along);
    VU0_v4sub4(&wheels[3], &wheels[1], &rightSide);
    VU0_v4sub4(&wheels[3], &wheels[2], &rearSide);
    VU0_v4add4(&across, &rearSide, &across);
    VU0_v4add4(&along, &rightSide, &along);
    if (fabs(across.y) > 3.0f)
        return;
    if (fabs(along.y) > 3.7f)
        return;
    const CarShadowSize *size = &CarShadowSizes[vehicle->renderTypeIndex];
    float halfLength = (float)(size->length * 0.5);
    VU0_v4scale4(&across, (float)(size->width * 0.5), &across);
    VU0_v4scale4(&along, halfLength, &along);

    VU0_v4sub4(&centre, &across, &corners[0]);
    VU0_v4sub4(&corners[0], &along, &corners[0]);
    VU0_v4add4(&centre, &across, &corners[1]);
    VU0_v4sub4(&corners[1], &along, &corners[1]);
    VU0_v4sub4(&centre, &across, &corners[2]);
    VU0_v4add4(&corners[2], &along, &corners[2]);
    VU0_v4add4(&centre, &across, &corners[3]);
    VU0_v4add4(&corners[3], &along, &corners[3]);

    uint32_t grey = uint32_t(level);
    uint32_t colour = grey << 24 | grey << 16 | grey << 8 | grey;
    for (int i = 0; i < 4; i++)
        colours[i] = colour;
    TexturedRequests[TexturedRequestIndex]->colours.SetData(colours);
    fgRenderer->renderContext->SetZWritesEnable(0);
    ShadowMaterial->Draw(kTriangleStrip, 4, NULL);
    fgRenderer->renderContext->SetZWritesEnable(1);
    if (alphaWrites)
        fgRenderer->EnableAlphaWrites();
}

// FUNC_AT(0x000a6670)
RShadowMap* RShadowMap::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RShadowMap));
    return this;
}
