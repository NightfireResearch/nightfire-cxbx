#pragma fp_contract(off)

#include "WorldCamera.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <xmmintrin.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../data/Carp.h"
#include "../data/DebugVariables.h"
#include "../data/IniFiles.h"
#include "../eagl/RenderContext.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/CoreContainers.h"     // GameVector
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RGlareManager.hpp"
#include "../render/RPathHandle.hpp"
#include "../render/RSceneObj.hpp"
#include "../world/Render.h"              // WRender, fgRender
#include "../world/Targeting.h"           // TargetPicker
#include "../world/VisCurtain.h"          // IsVisibleAgainstCurtains

// ---------------------------------------------------------------------------------------------------------------
// RWorldCamera (0x00097100-0x000980e0), RRenderWorldCamera (0x0008c8e0-0x0008d120), RPlayerViewCamera
// (0x0008a3f0-0x0008a4f0), and three helpers (0x000970a0, 0x0008d120, 0x00013020), ported from the listing.
//
// A world camera's anchor is checked before each use, as the game inlines it (ValidAnchor): an anchor whose type
// is not a rigid body's and whose body is not a simple one, or whose body slot is out of range, is dropped.
//
// The world view draws in the original's order, every call the original makes: the drawing is tested by lockstep
// runs and draw traces.
// ---------------------------------------------------------------------------------------------------------------

class AIVehicle;
class GHud;
class Grenade;
class Missile;
class RTyreTrack;
struct RigidBody;

namespace {

// The animation engine's instance of a camera animation system (names ours; only the fields read here)
struct AnimSystemInstance {
    uint8_t unknown00[0xc];
    uint8_t flags;                      // +0x0c kInstanceStopped
    uint8_t track;                      // +0x0d
    uint16_t frame;                     // +0x0e
    uint8_t unknown10[0xc];
    CameraAnimData *data;               // +0x1c
};

enum AnimSystemInstanceFlags : uint8_t {
    kInstanceFlag10 = 0x10,             // UpdateAnimationCam answers false
};

// What a 'Cams' instance's article description's third word leads to (name ours)
struct CameraAnimHeader {
    uint32_t unknown00;
    uint16_t id;                        // +0x04 LoadSingleAnimationFromList's
};

// A shell (an entry of the Simulation's shells): a physics object with a simple body
struct ShellFields : PhysicsObject {
    uint8_t unknown6C[0x20];
    int32_t streak;                     // +0x8c the bullet streak's kind; none below 0
};

// The scene object's description, as far as SetAnchor reads it: its first word is its group
struct SceneObjDescFields {
    UGroup *group;
};

// The particle system manager (only this field)
struct ParticleSystemManagerFields {
    uint8_t unknown00[0x14];
    uint32_t unknown14;                 // +0x14 copied to 0x001ec46c before the update
};

} // namespace

// ---- the game's code not ported yet
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define Simulation_GetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int slot))0x000b2730)
#define Handle_Create ((Handle *(*)(int unknown, uint32_t tick, CARP::Instance *instance, int, int))0x00078210)
#define Handle_Delete ((void (*)(Handle *handle, uint32_t size))0x00076810)
#define Handle_Stop ((void (__fastcall *)(Handle *, int))0x00078200)
// Answers a 16-bit id; the game stores all of EAX
#define Handle_GetInstanceSystemID ((uint32_t (__fastcall *)(Handle *, int, uint32_t index))0x00076900)
#define Handle_IsSystemPlaying ((bool (__fastcall *)(Handle *, int, uint32_t system))0x000774a0)
#define Handle_GetFirstSystemInstance ((AnimSystemInstance *(__fastcall *)(Handle *, int, uint32_t system))0x000776a0)
#define Handle_ProcessStimuli ((void (__fastcall *)(Handle *, int, uint32_t system, uint32_t stimulus, uint32_t step, int unknown))0x00077d70)
#define AISplinePath_Construct ((AISplinePath *(__fastcall *)(AISplinePath *, int, void *path))0x00035680)
#define AISplinePath_Destruct ((void (__fastcall *)(AISplinePath *, int))0x00035730)
#define AISplinePath_Reset ((void (__fastcall *)(AISplinePath *, int, void *path))0x00035740)
#define AIVehicle_GetSplinePath ((AISplinePath *(__fastcall *)(AIVehicle *, int))0x000359d0)
#define RPathHandle_SetNextPath ((void (__fastcall *)(RPathHandle *, int, CARP::PathInfo *path))0x0007fec0)
#define FUN_00022870 ((void (*)(Coord4 *out, const Coord4 *a, const Coord4 *b, float t))0x00022870)
#define FUN_00080980 ((void (*)(MATRIX4 *frame))0x00080980)
#define FUN_000809e0 ((void (*)(MATRIX4 *frame))0x000809e0)
#define ActActor_DrawWeapons ((void (__fastcall *)(ActActor *, int, RViewCamera *view, bool unknown))0x000118a0)
#define ActActorDatabase_PrepareActorsForCulling ((void (*)())0x00012580)
#define ActActorDatabase_GetNextActorCullInfo ((int (*)(Coord4 *sphere, float *height, bool *checkFar, float *farScale))0x00012f40)
#define ActActorDatabase_SetActorCull ((void (*)(int item, bool culled, float distance))0x0008c8d0)
#define ActActorDatabase_SetupFOVConversions ((void (__fastcall *)(ActActorDatabase *, int, RViewCamera *view))0x000130a0)
#define ActActorDatabase_DrawAll ((void (__fastcall *)(ActActorDatabase *, int, RViewCamera *view, bool, bool))0x00012fd0)
#define RRenderWorldCulling_IsInFrustum2d ((bool (__fastcall *)(void *, int, const Coord4 *sphere, float radius, bool checkFar, float farScale, float *distance))0x0008d180)
#define RRenderWorldCulling_Setup2dFrustrum ((void (__fastcall *)(void *, int, const Coord4 *position, const MATRIX4 *frame, float fieldOfView, float range))0x0008d440)
#define RSceneObj_PrepareSceneObjsForCulling ((void (*)(CachedDrawInfo *list))0x0008d830)
#define RSceneObj_GetNextSceneObjCullInfo ((int (*)(Coord4 *sphere, float *height, bool *checkFar, float *farScale))0x0008d860)
#define RSceneObj_SetSceneObjectCull ((void (*)(int item, bool culled, float distance))0x0008f000)
#define RSceneObj_RenderAllNormal ((void (*)())0x0008d9f0)
#define RSceneObj_RenderAllDrawLast ((void (*)())0x0008da30)
#define RSceneObj_RenderAllDeferredEffects ((void (*)())0x0008da60)
#define GFXGallery_CULL_Start ((void (__fastcall *)(void *, int))0x000d3aa0)
#define GFX_CULL_GetNextCanvas ((int (*)(Coord4 *sphere, float *height, bool *checkFar, float *farScale))0x000d3450)
#define GGallery_CULL_SetCanvas ((void (*)(int item, bool culled, float distance))0x000d3470)
#define GFX_Update ((void (*)())0x000d34c0)
#define RRenderer_FlushDrawLists ((void (__fastcall *)(CameraRendererFields *, int))0x0007d020)
#define RRenderer_EnableAlphaWrites ((void (__fastcall *)(CameraRendererFields *, int))0x0007d0b0)
#define RRenderer_DisableAlphaWrites ((void (__fastcall *)(CameraRendererFields *, int))0x0007d0e0)
#define RFog_DisableFog ((void (__fastcall *)(void *, int))0x0007d7a0)
#define RFog_EnableFog ((void (__fastcall *)(void *, int))0x0007d820)
#define RRenderSharedData_SetVehiclesAllowed ((bool (*)(bool allowed))0x0007e1a0)
#define RRenderSharedData_SendPerViewPort ((void (*)())0x0007e1c0)
#define RLightManager_AddPositionalLight ((void (__fastcall *)(void *, int, const Coord4 *position, const Coord4 *colour))0x0007e440)
#define RReflection_EnableReflectionMapWarpage ((void (__fastcall *)(void *, int, bool enable))0x00098220)
#define RColorize_Draw ((void (__fastcall *)(void *, int))0x0009a6e0)
#define RBulletStreak_Draw ((void (*)())0x00099e20)
#define RBulletStreak_Add ((void (*)(const Coord3 *position, const Coord4 *direction, int kind))0x0009a3b0)
#define RDecalManager_DrawDecals ((void (__fastcall *)(void *, int))0x0009b2d0)
#define RGain_Draw ((void (__fastcall *)(void *, int))0x0009dec0)
#define RLensFlareManager_Enable ((void (__fastcall *)(void *, int, bool enable))0x0009e1f0)
#define RLensFlareManager_DrawFlares ((void (__fastcall *)(void *, int))0x0009e540)
#define RLensFlareManager_TestFlares ((void (__fastcall *)(void *, int, RViewCamera *view))0x0009e720)
#define RLightning_Draw ((void (__fastcall *)(void *, int))0x0009fd20)
#define RMissileCam_Draw ((void (__fastcall *)(void *, int))0x000a0b50)
#define RParticleSystemManager_UpdateAndRenderAllSystems ((void (__fastcall *)(ParticleSystemManagerFields *, int))0x000a1e30)
#define RParticleSystemManager_UpdateSpawnAllSystems ((void (__fastcall *)(ParticleSystemManagerFields *, int))0x000a2930)
#define RParticulate_Draw ((void (__fastcall *)(void *, int))0x000a3e60)
#define RParticulate_Update ((void (__fastcall *)(void *, int))0x000a3fd0)
#define RPostProcessing_Draw ((void (__fastcall *)(void *, int))0x000a4ed0)
#define RPostProcessing_GrabBackBuffer ((void (__fastcall *)(void *, int))0x000a5020)
#define RShadowMap_FUN_000a5930 ((void (__fastcall *)(void *, int))0x000a5930)
#define RSniperZoom_GrabBackBuffer ((void (__fastcall *)(void *, int))0x000a68a0)
#define RSniperZoom_Draw ((void (__fastcall *)(void *, int))0x000a6bc0)
#define RWindow_DrawBrokenWindows ((void (*)(float unknown))0x000a8c10)
#define RDebris_Draw ((void (__fastcall *)(void *, int))0x000a9660)
#define RTyreTrack_Draw ((void (__fastcall *)(RTyreTrack *, int))0x000ac250)
#define RayShell_DrawTracers ((void (*)())0x00071a60)
#define RayShell_DrawFlashes ((void (*)())0x00071c80)
#define Sentry_DrawMuzzleFlashes ((void (*)())0x00073500)
#define Missile_Render ((void (__fastcall *)(Missile *, int, bool firstView))0x0005f3d0)
#define Grenade_Render ((void (__fastcall *)(Grenade *, int, bool firstView))0x0005d550)
#define GHud_TheApp ((GHud *(*)())0x000d7c10)
#define GHud_UpdateTargets ((void (__fastcall *)(GHud *, int))0x000e1a90)

// ---- globals
#define RWorldCameraVtable ((void **)0x001924e4)
#define RRenderWorldCameraVtable ((void **)0x00191b2c)
#define RPlayerViewCameraVtable ((void **)0x00191910)
#define fgRenderer (*(CameraRendererFields **)0x001ebff4)
#define Sim ((void *)0x00233ff0)                                // the Simulation
#define SimStepCount U32_AT(0x00234e34)
// The Simulation's lists of physics objects (Ghidra's names): its cars, the player's first, its missiles,
// grenades and shells
#define SimCars (*(GameVector<RigidVehicle *> *)0x00234e3c)
#define SimMissiles (*(GameVector<Missile *> *)0x00234e7c)
#define SimGrenades (*(GameVector<Grenade *> *)0x00234e9c)
#define SimShells (*(GameVector<ShellFields *> *)0x00234eac)
#define GameTick U32_AT(0x001f2a4c)                             // the game's tick (RGlareManager.cpp's GlareTick)
#define IdentityMatrix4 (*(const MATRIX4 *)0x001d4c10)
// GetAnchorMatrix4's matrix, made the identity on its first call (a function-local static and its guard)
#define AnchorMatrix (*(MATRIX4 *)0x001f2d20)
#define AnchorMatrixGuard U32_AT(0x001f2d60)
#define AnchorNoPosition (*(Coord3 *)0x001c47f0)                // zero (names ours)
#define AnchorNoVelocity (*(Coord3 *)0x001c4800)
#define HeadlightsEnabled (*(bool *)0x001c4618)                 // "Enable headlights"
#define OffOnNames ((const char *const *)0x001b6638)            // "Off", "On"
#define PostProcessingEnabled BOOL8_AT(0x001c4620)              // (names ours)
#define CullDistance FLOAT_AT(0x001c4624)                       // ESetCullDistanceFactor's; 400
#define WorldViewMade BOOL8_AT(0x001f2c50)                      // set by RRenderWorldCamera; RVehicle::Render reads it
#define ParticleSystemsUnknown U32_AT(0x001ec46c)
#define WorldCulling ((void *)0x001f2c80)                       // the RRenderWorldCulling
#define ActorDatabase (*(ActActorDatabase **)0x001dd9a0)
#define Pass0Draws ((CachedDrawInfo *)0x001ef0c0)               // pass 0 of the track's draws, drawn last
#define Pass1Draws ((CachedDrawInfo *)0x001ec4b8)               // pass 1, drawn after the cars (names ours)
#define Fog (*(void **)0x001ec004)
#define LightManager (*(void **)0x001ec260)
#define Colorize (*(void **)0x001f6898)
#define Reflection (*(void **)0x001f2dfc)
#define DecalManager (*(void **)0x001fe820)
#define Gain (*(void **)0x00200f20)
#define LensFlares (*(void **)0x00200f44)
#define Lightning (*(void **)0x00200f4c)
#define MissileCam (*(void **)0x00200f54)
#define ParticleSystems (*(ParticleSystemManagerFields **)0x00201730)
#define Particulate (*(void **)0x00201754)
#define PostProcessing (*(void **)0x0020175c)
#define ShadowMap (*(void **)0x0020176c)
#define SniperZoom (*(void **)0x00201818)
#define Debris (*(void **)0x00202a80)
#define GlareManager (*(RGlareManager **)0x00208cb4)
#define Gallery (*(void **)0x0023f320)

namespace {

constexpr int32_t kTypeRigidBody = 1;           // PhysicsObject::type: its body is a RigidBody
constexpr int kBodySlots = 0x40;                // the slots an anchor's body may be in
constexpr uint32_t kTagCams = 0x43616d73;       // 'Cams'
constexpr uint32_t kHandleSize = 0x40;          // an RAnimEngine::Handle
constexpr uint32_t kAISplinePathSize = 0xb0;
constexpr uint32_t kCameraStimulus = 8;         // PlayCurrentAnimation's stimulus
constexpr float kDefaultFieldOfView = 33.0f;
constexpr float kMinZoom = 2.0f;
constexpr float kSecondsPerFrame = 1.0f / 60.0f;
constexpr float kAnimZoomStep = 1.5f;
constexpr float kLookAtEase = 0.05f;            // of the way to the new lookAt each call
constexpr uint8_t kAnimMirror = 0x1;            // UpdateAnimationCam's flags
constexpr uint8_t kAnimFlag2 = 0x2;             // FUN_000809e0 for the axes, not FUN_00080980
constexpr float kCullFar = 120.0f;              // GenerateCurtainsAndNodes's second argument
constexpr float kGuardBandSize = 4.0f;
constexpr float kBrokenWindowsUnknown = 75.0f;
constexpr int kWheels = 4;
constexpr float kHeadlightReach = 14.0f;        // ahead of the car
constexpr float kHeadlightW = 2.5f;             // the light's fourth word
constexpr float kHeadlightColour = 0.2f;
constexpr float kNarrowFieldOfView = 32.0f;     // below it the culling reaches further
constexpr float kNarrowCullScale = 1.1f;
constexpr float kWidescreenFovScale = 1.25f;
constexpr uint8_t kHeadlightsBroken = 0x5;      // both these damage zones: no headlight
static_assert(std::bit_cast<uint32_t>(kSecondsPerFrame) == 0x3c888889 && std::bit_cast<uint32_t>(kLookAtEase) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(kHeadlightColour) == 0x3e4ccccd && std::bit_cast<uint32_t>(kNarrowCullScale) == 0x3f8ccccd,
              "the original's constants");

char inputQueueName[] = "WorldCameraQ";
static_assert(sizeof(ActionQueue) == 0x974, "an ActionQueue is 0x974 bytes");

RigidBody *RigidBodyOf(const PhysicsObject *object) {
    return Simulation_GetRigidBody(Sim, 0, object->rigidBodySlot);
}

SimpleRigidBody *SimpleBodyOf(const PhysicsObject *object) {
    return Simulation_GetSimpleRigidBody(Sim, 0, object->rigidBodySlot);
}

void DeleteHandle(Handle *handle) {
    Handle_Stop(handle, 0);
    Handle_Delete(handle, kHandleSize);
}

uint16_t CameraAnimId(const CARP::Instance *instance) {
    const CARP::BaseDesc *desc = reinterpret_cast<const CARP::BaseDesc *>(instance->articleDesc.value);
    return reinterpret_cast<const CameraAnimHeader *>(desc->unknown08)->id;
}

// value + t * (target - value) in x, y and z, w kept, in single precision (FUN_00022870, inlined in the game)
void Ease(Coord4 *value, const Coord4 *target, float t) {
    __m128 a = _mm_setr_ps(value->x, value->y, value->z, 0.0f);
    __m128 b = _mm_setr_ps(target->x, target->y, target->z, 0.0f);
    __m128 eased = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(b, a)), a);
    float lanes[4];
    _mm_storeu_ps(lanes, eased);
    value->x = lanes[0];
    value->y = lanes[1];
    value->z = lanes[2];
}

// ---- virtual methods of the game's objects (the slot in the vtable; the names are ours)

// A vehicle's slot 12: whether it may be reset
int ResetAvailable(PhysicsObject *object) {
    typedef int (PhysicsObject::*Method)();
    return (object->*XbeVirtual<Method>(object, 12))();
}

// A vehicle's slot 10
AIVehicle *AIVehicleOf(RigidVehicle *car) {
    typedef AIVehicle *(RigidVehicle::*Method)();
    return (car->*XbeVirtual<Method>(car, 10))();
}

// A vehicle's slot 41: a wheel's tyre track, NULL if none
RTyreTrack *TyreTrack(RigidVehicle *car, int wheel) {
    typedef RTyreTrack *(RigidVehicle::*Method)(int wheel);
    return (car->*XbeVirtual<Method>(car, 41))(wheel);
}

// A render object's slot 9: its offset, answered in ST0 unrounded
double RenderOffset(RSceneObj *object) {
    typedef double (RSceneObj::*Method)();
    return (object->*XbeVirtual<Method>(object, 9))();
}

} // namespace

// ---- helpers

// FUNC_AT(0x000970a0)
int FloorToInt(float value) {
    // NaN takes the plain truncation, as the original's FCOMP + TEST AH,5 + JP does
    if (!(value < 0.0f) || (double)Ftol(value) == value)
        return Ftol(value);
    return Ftol(value) - 1;
}

// FUNC_AT(0x0008d120)
void TransformPointXZ(const Coord3 *point, const MATRIX4 *matrix, Coord4 *out) {
    const float (*m)[4] = matrix->mtx;
    out->x = (float)((double)m[2][0] * point->z + (double)point->x * m[0][0] - m[3][0]);
    out->y = (float)((double)m[0][1] * point->x + (double)m[2][1] * point->z - m[3][1]);
    out->z = (float)((double)m[0][2] * point->x + (double)m[2][2] * point->z - m[3][2]);
    out->w = (float)((double)m[0][3] * point->x + (double)m[2][3] * point->z - m[3][3]);
}

// FUNC_AT(0x00013020)
void ActActorDatabase::DrawActorWeapons(RViewCamera *view, bool unknown) {
    for (ActActorNode *node = actors == NULL ? NULL : actors->next; node != actors; node = node->next) {
        if (!node->actor->culled)
            ActActor_DrawWeapons(node->actor, 0, view, unknown);
    }
}

// ---- RWorldCamera

PhysicsObject* RWorldCamera::ValidAnchor() {
    if (anchor == NULL)
        return NULL;
    if ((anchor->type == kTypeRigidBody || (anchor->flags & PhysicsObject::kSimpleBody)) &&
        anchor->rigidBodySlot >= 0 && anchor->rigidBodySlot < kBodySlots)
        return anchor;
    anchor = NULL;
    return NULL;
}

// FUNC_AT(0x00098070)
RWorldCamera* RWorldCamera::Construct() {
    RCamera::Construct();
    vtable = RWorldCameraVtable;
    animHandle = NULL;
    aiSplinePath = NULL;
    inputQueue = NULL;
    anchor = NULL;
    cameraAnims = NULL;
    cameraAnimCount = 0;
    RestartCamera();
    return this;
}

// FUNC_AT(0x000979f0)
void RWorldCamera::Destruct() {
    vtable = RWorldCameraVtable;
    if (animHandle != NULL)
        DeleteHandle(animHandle);
    if (aiSplinePath != NULL) {
        AISplinePath_Destruct(aiSplinePath, 0);
        OperatorDelete(aiSplinePath);
    }
    if (inputQueue != NULL) {
        inputQueue->Destruct();
        UMemory::FastFree(inputQueue, sizeof(ActionQueue));
    }
    RCamera::Destruct();
}

// FUNC_AT(0x00097eb0)
RWorldCamera* RWorldCamera::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RWorldCamera));
    return this;
}

// FUNC_AT(0x00097100)
void RWorldCamera::SetViewingTransform(const Coord4 *up, const void *velocity, int) {
    Coord4 *right = MatrixRow(&matrix, 0);
    Coord4 *top = MatrixRow(&matrix, 1);
    Coord4 *forward = MatrixRow(&matrix, 2);
    matrixChanged = 1;
    VU0_v4sub(&lookAt, &eye, forward);
    VU0_v4unitxyz(forward, forward);
    VU0_v4unitcrossprodxyz(up, forward, right);
    VU0_v4crossprodxyz(forward, right, top);
    VU0_v4copy(&eye, MatrixRow(&matrix, 3));
    forward->w = 0.0f;
    top->w = 0.0f;
    right->w = 0.0f;
    matrix.mtx[3][3] = 1.0f;
    if (velocity != NULL)
        VU0_v4copy(velocity, &unknown50);
}

// FUNC_AT(0x00097190)
void RWorldCamera::SetViewingTransform(const Coord4 *up, const float *offset, const void *velocity, int) {
    Coord4 *right = MatrixRow(&matrix, 0);
    Coord4 *top = MatrixRow(&matrix, 1);
    Coord4 *forward = MatrixRow(&matrix, 2);
    matrixChanged = 1;
    VU0_v4sub(&lookAt, &eye, forward);
    VU0_v4scaleadd(up, offset[1], forward, forward);
    VU0_v4scaleadd(right, offset[0], forward, forward);
    VU0_v4unitxyz(forward, forward);
    VU0_v4unitcrossprodxyz(up, forward, right);
    VU0_v4crossprodxyz(forward, right, top);
    VU0_v4copy(&eye, MatrixRow(&matrix, 3));
    forward->w = 0.0f;
    top->w = 0.0f;
    right->w = 0.0f;
    matrix.mtx[3][3] = 1.0f;
    if (velocity != NULL)
        VU0_v4copy(velocity, &unknown50);
}

// FUNC_AT(0x00097240)
float RWorldCamera::GetAnchorSpeed() {
    PhysicsObject *object = ValidAnchor();
    if (object == NULL)
        return 0.0f;
    if (object->type == kTypeRigidBody)
        return VU0_v3lengthxz(&RigidBodyOf(object)->velocity);
    if (object->flags & PhysicsObject::kSimpleBody)
        return SimpleBodyOf(object)->GetScalarVelocity();
    return 0.0f;
}

// The force on the anchor's body along its x and z axes over its mass; zero for a simple body.
// FUNC_AT(0x000972c0)
void RWorldCamera::GetAnchorAcceleration(Coord4 *acceleration) {
    PhysicsObject *object = ValidAnchor();
    if (object == NULL)
        return;
    if (object->type == kTypeRigidBody) {
        const Coord3 *force = &RigidBodyOf(object)->force;
        const MATRIX4 *frame = &RigidBodyOf(object)->info->orientation;
        acceleration->x = v3dotprod(force, MatrixRow(frame, 0));
        acceleration->y = 0.0f;
        acceleration->z = v3dotprod(force, MatrixRow(frame, 2));
        acceleration->w = 0.0f;
        VU0_v4scale(acceleration, 1.0f / RigidBodyOf(object)->mass, acceleration);
    } else {
        VU0_v4Init(acceleration);
        acceleration->w = 0.0f;
    }
}

// FUNC_AT(0x000973c0)
MATRIX4* RWorldCamera::GetAnchorMatrix4() {
    if (!(AnchorMatrixGuard & 1)) {
        AnchorMatrix = IdentityMatrix4;
        AnchorMatrixGuard |= 1;
    }
    PhysicsObject *object = ValidAnchor();
    if (object == NULL)
        return &AnchorMatrix;
    if (!(object->flags & PhysicsObject::kSimpleBody))
        return &RigidBodyOf(object)->info->orientation;
    SimpleBodyOf(object)->RecalcOrientMat(&AnchorMatrix);
    return &AnchorMatrix;
}

// FUNC_AT(0x00097470)
Coord3* RWorldCamera::GetAnchorPosition() {
    PhysicsObject *object = ValidAnchor();
    if (object == NULL)
        return &AnchorNoPosition;
    if (!(object->flags & PhysicsObject::kSimpleBody))
        return &RigidBodyOf(object)->position;
    return &SimpleBodyOf(object)->position;
}

// FUNC_AT(0x000974e0)
int RWorldCamera::GetAnchorResetAvailable() {
    PhysicsObject *object = ValidAnchor();
    if (object == NULL || object->type != kTypeRigidBody)
        return 0;
    return ResetAvailable(object);
}

// FUNC_AT(0x00097530)
void RWorldCamera::SetAnchor(PhysicsObject *anchor) {
    this->anchor = anchor;
    if (ValidAnchor() == NULL)
        return;
    modeChangeFlags |= kAnchorChanged;
    UGroup *group = static_cast<SceneObjDescFields *>(anchor->renderObject->baseDesc)->group;
    UData *cams = group->DataLocateTag(kTagCams);
    if (cams == group->DataEnd())
        return;
    cameraAnimCount = cams->count;
    cameraAnims = reinterpret_cast<CARP::Instance *>(cams->Data());
}

// FUNC_AT(0x000975d0)
double RWorldCamera::GetAnchorRenderOffset() {
    PhysicsObject *object = ValidAnchor();
    if (object == NULL)
        return 0.0;
    return RenderOffset(object->renderObject);
}

// A rigid body anchor's velocity is the player's AI vehicle's spline path's while it has one.
// FUNC_AT(0x00097fc0)
Coord3* RWorldCamera::GetAnchorLinearVelocity() {
    PhysicsObject *object = ValidAnchor();
    if (object == NULL)
        return &AnchorNoVelocity;
    if (object->flags & PhysicsObject::kSimpleBody)
        return &SimpleBodyOf(object)->velocity;
    AIVehicle *vehicle = AIVehicleOf(SimCars.first[0]);
    if (vehicle != NULL && AIVehicle_GetSplinePath(vehicle, 0) != NULL)
        return &AIVehicle_GetSplinePath(vehicle, 0)->path->velocity;
    return &RigidBodyOf(object)->velocity;
}

// FUNC_AT(0x00097620)
void RWorldCamera::StartCameraInputReceiver() {
    if (inputQueue != NULL)
        return;
    ActionQueue *queue = static_cast<ActionQueue *>(UMemory::FastAlloc(sizeof(ActionQueue), "ActionQueue"));
    inputQueue = queue != NULL ? queue->Construct(inputQueueName) : NULL;
}

// FUNC_AT(0x00097690)
void RWorldCamera::ReceiveCameraInput() {
    if (inputQueue == NULL)
        return;
    while (!inputQueue->IsEmpty()) {
        ActionRef ref;
        inputQueue->GetAction(&ref);
        int32_t action = 0;
        float value = 0.0f;
        if (ref.data != NULL) {
            action = ref.data->action;
            value = ref.data->value;
        }
        CameraInputCallbackVirtual(action, value);
        inputQueue->PopAction();
    }
}

// FUNC_AT(0x00097710)
void RWorldCamera::SetCameraZoom(float target, float step) {
    double zoom;
    if (target > (double)fieldOfView + step)
        zoom = (double)fieldOfView + step;
    else if (target < (double)fieldOfView - step)
        zoom = (double)fieldOfView - step;
    else
        zoom = target;
    if (zoom > kMinZoom)
        fieldOfView = (float)zoom;
}

// FUNC_AT(0x00097770)
bool RWorldCamera::LoadSingleAnimation(CARP::Instance *instance) {
    if (animHandle != NULL)
        DeleteHandle(animHandle);
    animHandle = Handle_Create(1, GameTick, instance, 0, 0);
    animSystemId = Handle_GetInstanceSystemID(animHandle, 0, 0);
    return animHandle != NULL;
}

// FUNC_AT(0x000977d0)
bool RWorldCamera::LoadAISplinePathAnimation(void *path) {
    if (aiSplinePath == NULL) {
        AISplinePath *made = static_cast<AISplinePath *>(OperatorNew(kAISplinePathSize));
        aiSplinePath = made != NULL ? AISplinePath_Construct(made, 0, path) : NULL;
    } else {
        AISplinePath_Reset(aiSplinePath, 0, path);
    }
    RPathHandle_SetNextPath(aiSplinePath->path, 0, NULL);
    return true;
}

// FUNC_AT(0x00097870)
bool RWorldCamera::LoadSingleAnimationFromList(uint32_t id) {
    if (cameraAnims == NULL)
        return false;
    uint32_t i;
    for (i = 0; i < cameraAnimCount; i++) {
        if (CameraAnimId(&cameraAnims[i]) == id)
            break;
    }
    if (i >= cameraAnimCount || CameraAnimId(&cameraAnims[i]) != id)
        return false;
    if (animHandle != NULL)
        DeleteHandle(animHandle);
    animHandle = Handle_Create(1, GameTick, &cameraAnims[i], 0, 0);
    if (animHandle == NULL)
        return false;
    animSystemId = id;
    return true;
}

// FUNC_AT(0x00097930)
bool RWorldCamera::PlayCurrentAnimation() {
    if (animHandle == NULL)
        return false;
    Handle_ProcessStimuli(animHandle, 0, animSystemId, kCameraStimulus, SimStepCount, 2);
    return Handle_IsSystemPlaying(animHandle, 0, animSystemId);
}

// FUNC_AT(0x00097970)
void RWorldCamera::ReadAnchorInfo(IniFiles *ini, const char *section, CameraAnchorInfo *info) {
    info->offset.x = (float)ini->ReadFloat(section, "Anchor_X", 0.0f);
    info->offset.y = (float)ini->ReadFloat(section, "Anchor_Y", 0.0f);
    info->offset.z = (float)ini->ReadFloat(section, "Anchor_Z", 0.0f);
    info->slideDist = (float)ini->ReadFloat(section, "Anchor_Slide_Dist", 0.0f);
    info->slideRate = (float)ini->ReadFloat(section, "Anchor_Slide_Rate", 0.0f);
    info->slideRecoveryRate = (float)ini->ReadFloat(section, "Anchor_Slide_Rec_Rate", 0.0f);
}

// The frame between the two keys the time falls between: their rotations slerped, their positions eased into
// the eye.
// FUNC_AT(0x00097aa0)
bool RWorldCamera::UpdateAnimationCam(MATRIX4 *frame, uint8_t flags) {
    if (animHandle == NULL)
        return false;
    AnimSystemInstance *instance = Handle_GetFirstSystemInstance(animHandle, 0, animSystemId);
    if (instance == NULL || instance->data == NULL)
        return false;
    const CameraAnimTrack *track = &instance->data->tracks[instance->track];
    if (instance->flags & kInstanceFlag10)
        return false;
    bool playing = Handle_IsSystemPlaying(animHandle, 0, animSystemId);
    if (!playing)
        return playing;

    float time = (float)((double)int32_t(instance->frame * track->framesPerKey) * kSecondsPerFrame);
    int key = FloorToInt(time);
    float t = (float)(time - (double)uint32_t(key));    // the key loaded signed, 2^32 added when negative
    uint32_t from = key;
    if (from >= track->keyCount)
        from = track->keyCount - 1;
    uint32_t to = from + 1;
    if (to >= track->keyCount)
        to = track->keyCount - 1;
    Coord4 rotation;
    VU0_fastqslerp(&track->keys[from].rotation, &track->keys[to].rotation, &rotation, t);
    VU0_quattom4(frame, &rotation);
    if (flags & kAnimFlag2)
        FUN_000809e0(frame);
    else
        FUN_00080980(frame);
    FUN_00022870(&eye, &track->keys[from].position, &track->keys[to].position, t);
    if (flags & kAnimMirror) {
        frame->mtx[0][0] = -frame->mtx[0][0];
        frame->mtx[1][0] = -frame->mtx[1][0];
        frame->mtx[2][0] = -frame->mtx[2][0];
        eye.x = -eye.x;
        VU0_v4scale4(MatrixRow(frame, 0), -1.0f, MatrixRow(frame, 0));
    }
    eye.w = 1.0f;
    VU0_v4copy(&eye, MatrixRow(frame, 3));
    SetCameraZoom(kDefaultFieldOfView, kAnimZoomStep);
    return playing;
}

// FUNC_AT(0x00097c60)
void RWorldCamera::AnchorCamera(bool smooth, const Coord3 *offset) {
    Coord3 *position = GetAnchorPosition();
    MATRIX4 *frame = GetAnchorMatrix4();
    Coord4 local = { offset->x, offset->y, offset->z, 0.0f };
    if (smooth) {
        VU0_MATRIX4_vect3rotate(&local, frame, &local);
        Ease(&lookAtOffset, &local, kLookAtEase);
    } else {
        VU0_MATRIX4_vect3rotate(&local, frame, &lookAtOffset);
    }
    VU0_v3add(&lookAtOffset, position, &lookAt);
}

// FUNC_AT(0x00097d70)
void RWorldCamera::AnchorRelativeCamera(bool smooth, const Coord3 *offset) {
    MATRIX4 *frame = GetAnchorMatrix4();
    Coord3 *position = GetAnchorPosition();
    Coord4 local = { 0.0f, (float)(offset->y - GetAnchorRenderOffset()), offset->z, 0.0f };
    if (smooth)
        Ease(&lookAtOffset, &local, kLookAtEase);
    else
        VU0_v4copy(&local, &lookAtOffset);
    VU0_MATRIX4_vect3rotate(&lookAtOffset, frame, &lookAtOffset);
    VU0_v3add(&lookAtOffset, position, &lookAt);
}

// FUNC_AT(0x00097ee0)
void RWorldCamera::RestartCamera() {
    SetAnchor(SimCars.first[0]);
    modeChangeFlags |= kAnchorChanged;
    VU0_v4Init(&eye);
    VU0_v4Init(&lookAt);
    VU0_v4Init(&unknown100);
    VU0_v4Init(&lookAtOffset);
    fieldOfView = kDefaultFieldOfView;
    zoomFov = kDefaultFieldOfView;
    zoomFovTarget = kDefaultFieldOfView;
    animSystemId = 0;
    if (inputQueue != NULL)
        inputQueue->Flush();
    if (animHandle != NULL) {
        DeleteHandle(animHandle);
        animHandle = NULL;
    }
    if (aiSplinePath != NULL) {
        AISplinePath_Destruct(aiSplinePath, 0);
        OperatorDelete(aiSplinePath);
        aiSplinePath = NULL;
    }
}

// ---- RRenderWorldCamera

// FUNC_AT(0x0008c8e0)
RRenderWorldCamera* RRenderWorldCamera::Construct(RCamera *camera) {
    RViewCamera::Construct(camera);
    vtable = RRenderWorldCameraVtable;
    RLensFlareManager_Enable(LensFlares, 0, true);
    WorldViewMade = 1;
    SetGuardBandSize(kGuardBandSize);
    return this;
}

// FUNC_AT(0x0008c950)
void RRenderWorldCamera::Destruct() {
    vtable = RRenderWorldCameraVtable;
    RViewCamera::Destruct();
}

// FUNC_AT(0x0008ccf0)
RRenderWorldCamera* RRenderWorldCamera::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RRenderWorldCamera));
    return this;
}

// FUNC_AT(0x0008c960)
void RRenderWorldCamera::LoadAttributes() {
    dbattrib_bool("Enable headlights", &HeadlightsEnabled, 0, 1, 0, -1.0f, OffOnNames);
}

// FUNC_AT(0x0008c990)
void RRenderWorldCamera::PreRender() {
    RReflection_EnableReflectionMapWarpage(Reflection, 0, false);
    ConfigureViewVirtual();
}

// An item with the far-plane test is tested against the curtains with no radius.
// FUNC_AT(0x0008c9b0)
void RRenderWorldCamera::CullModule(CullNextItem next, CullSetItem set, bool) {
    Coord4 sphere;
    float height;
    bool checkFar;
    float farScale;
    for (int item = next(&sphere, &height, &checkFar, &farScale); item != -1;
         item = next(&sphere, &height, &checkFar, &farScale)) {
        float distance = 0.0f;
        if (!RRenderWorldCulling_IsInFrustum2d(WorldCulling, 0, &sphere, sphere.w, checkFar, farScale, &distance)) {
            set(item, true, distance);
            continue;
        }
        if (checkFar)
            sphere.w = 0.0f;
        set(item, !IsVisibleAgainstCurtains(&sphere, height), distance);
    }
}

// FUNC_AT(0x0008ca90)
CachedDrawInfo* RRenderWorldCamera::PerformCulling() {
    CachedDrawInfo *list = fgRender->GenerateCurtainsAndNodes(camera, kCullFar);
    WRender::PrepareForCull(list);
    CullModule(WRender::GetNextPoint, WRender::SetCull, true);
    if (ActorDatabase != NULL) {
        ActActorDatabase_PrepareActorsForCulling();
        CullModule(ActActorDatabase_GetNextActorCullInfo, ActActorDatabase_SetActorCull, false);
    }
    RSceneObj_PrepareSceneObjsForCulling(list);
    CullModule(RSceneObj_GetNextSceneObjCullInfo, RSceneObj_SetSceneObjectCull, false);
    GFXGallery_CULL_Start(Gallery, 0);
    CullModule(GFX_CULL_GetNextCanvas, GGallery_CULL_SetCanvas, false);
    return list;
}

// FUNC_AT(0x0008cb30)
void RRenderWorldCamera::DrawStaticWorldGeometry(CachedDrawInfo *list) {
    RRenderer_EnableAlphaWrites(fgRenderer, 0);
    RFog_EnableFog(Fog, 0);
    fgRender->DrawWorld(list, 1);
    TargetPicker.UpdateTargets();
    GHud_UpdateTargets(GHud_TheApp(), 0);
    RFog_DisableFog(Fog, 0);
    RRenderer_DisableAlphaWrites(fgRenderer, 0);
}

// Pass 0, farthest first, without depth writes.
// FUNC_AT(0x0008cb90)
void RRenderWorldCamera::DrawFinalStaticWorldGeometry(CachedDrawInfo *list) {
    fgRenderer->renderContext->SetZWritesEnable(0);
    RFog_EnableFog(Fog, 0);
    RRenderSharedData_SendPerViewPort();
    fgRender->DrawPass(list, 0, 0, 2);
    RFog_DisableFog(Fog, 0);
    fgRenderer->renderContext->SetZWritesEnable(1);
}

// FUNC_AT(0x0008cbf0)
void RRenderWorldCamera::DrawPostProcessingEffects() {
    if (!PostProcessingEnabled)
        return;
    RPostProcessing_GrabBackBuffer(PostProcessing, 0);
    RSniperZoom_GrabBackBuffer(SniperZoom, 0);
    fgRenderer->currentView->SetDeviceTransformMode();
    RLensFlareManager_TestFlares(LensFlares, 0, this);
    RLensFlareManager_DrawFlares(LensFlares, 0);
    RColorize_Draw(Colorize, 0);
    RGain_Draw(Gain, 0);
    RSniperZoom_Draw(SniperZoom, 0);
    RPostProcessing_Draw(PostProcessing, 0);
    RMissileCam_Draw(MissileCam, 0);
}

// FUNC_AT(0x0008cc70)
void RRenderWorldCamera::DrawVehiclesAndDeferredSceneObjects() {
    RRenderer_EnableAlphaWrites(fgRenderer, 0);
    RFog_EnableFog(Fog, 0);
    bool vehiclesAllowed = RRenderSharedData_SetVehiclesAllowed(true);
    RRenderSharedData_SendPerViewPort();
    RShadowMap_FUN_000a5930(ShadowMap, 0);
    RRenderSharedData_SendPerViewPort();
    RSceneObj_RenderAllDrawLast();
    RRenderer_DisableAlphaWrites(fgRenderer, 0);
    RRenderSharedData_SendPerViewPort();
    RRenderer_FlushDrawLists(fgRenderer, 0);
    NullFunction();             // called on the shadow map
    RRenderSharedData_SetVehiclesAllowed(vehiclesAllowed);
    RFog_DisableFog(Fog, 0);
}

// FUNC_AT(0x0008cd10)
void RRenderWorldCamera::DrawTyreTracks() {
    for (RigidVehicle **car = SimCars.first; car != SimCars.last; car++) {
        for (int wheel = 0; wheel < kWheels; wheel++) {
            if (TyreTrack(*car, wheel) != NULL)
                RTyreTrack_Draw(TyreTrack(*car, wheel), 0);
        }
    }
}

// FUNC_AT(0x0008cd60)
void RRenderWorldCamera::DrawBulletStreaks() {
    for (ShellFields **shell = SimShells.first; shell != SimShells.last; shell++) {
        Coord4 direction;
        VU0_v4unitxyz(&SimpleBodyOf(*shell)->velocity, &direction);
        int streak = (*shell)->streak;
        if (streak > -1)
            RBulletStreak_Add(&SimpleBodyOf(*shell)->position, &direction, streak);
    }
    RayShell_DrawTracers();
    RayShell_DrawFlashes();
    for (Missile **missile = SimMissiles.first; missile != SimMissiles.last; missile++)
        Missile_Render(*missile, 0, viewId == 0);
    for (Grenade **grenade = SimGrenades.first; grenade != SimGrenades.last; grenade++)
        Grenade_Render(*grenade, 0, viewId == 0);
}

// A light ahead of the player's car.
// FUNC_AT(0x0008ce50)
void RRenderWorldCamera::AddPlayerHeadlight() {
    RigidVehicle *car = SimCars.first[0];
    const Coord3 *position = &RigidBodyOf(car)->position;
    const Coord4 *forward = MatrixRow(&RigidBodyOf(car)->info->orientation, 2);
    Coord4 light = { position->x, position->y, position->z, kHeadlightW };
    Coord4 colour = { kHeadlightColour, kHeadlightColour, kHeadlightColour, kHeadlightColour };
    light.x = (float)((double)forward->x * kHeadlightReach + light.x);
    light.y = (float)((double)forward->y * kHeadlightReach + light.y);
    light.z = (float)((double)forward->z * kHeadlightReach + light.z);
    RLightManager_AddPositionalLight(LightManager, 0, &light, &colour);
}

// FUNC_AT(0x0008cf10)
void RRenderWorldCamera::DrawEffects() {
    DrawBulletStreaks();
    RWindow_DrawBrokenWindows(kBrokenWindowsUnknown);
    RDecalManager_DrawDecals(DecalManager, 0);
    RBulletStreak_Draw();
    RLightning_Draw(Lightning, 0);
    RSceneObj_RenderAllDeferredEffects();
    GFX_Update();
    RDebris_Draw(Debris, 0);
    GlareManager->DrawGlares(true);
    RParticleSystemManager_UpdateSpawnAllSystems(ParticleSystems, 0);
    ParticleSystemsUnknown = ParticleSystems->unknown14;
    RParticleSystemManager_UpdateAndRenderAllSystems(ParticleSystems, 0);
    Sentry_DrawMuzzleFlashes();
    RParticulate_Update(Particulate, 0);
    RParticulate_Draw(Particulate, 0);
}

// FUNC_AT(0x0008cfa0)
void RRenderWorldCamera::DoRender() {
    CachedDrawInfo *list = PerformCulling();
    RRenderSharedData_SendPerViewPort();
    if (ActorDatabase != NULL)
        ActActorDatabase_SetupFOVConversions(ActorDatabase, 0, this);
    DrawStaticWorldGeometry(list);
    fgRender->CopyDrawPasses(list, Pass0Draws, 0, 0);
    fgRender->CopyDrawPasses(list, Pass1Draws, 1, 1);
    if (ActorDatabase != NULL)
        ActActorDatabase_DrawAll(ActorDatabase, 0, this, false, false);

    RRenderer_EnableAlphaWrites(fgRenderer, 0);
    RFog_EnableFog(Fog, 0);
    RRenderSharedData_SendPerViewPort();
    RSceneObj_RenderAllNormal();
    RFog_DisableFog(Fog, 0);
    RRenderer_DisableAlphaWrites(fgRenderer, 0);
    DrawTyreTracks();
    DrawVehiclesAndDeferredSceneObjects();

    RFog_EnableFog(Fog, 0);
    RRenderSharedData_SendPerViewPort();
    fgRender->DrawPass(Pass1Draws, 1, 1, 2);
    RFog_DisableFog(Fog, 0);
    if (ActorDatabase != NULL)
        ActorDatabase->DrawActorWeapons(this, false);
    DrawFinalStaticWorldGeometry(Pass0Draws);
    TargetPicker.DrawTargetingSystem();
    DrawEffects();
    if (ActorDatabase != NULL) {
        RRenderer_EnableAlphaWrites(fgRenderer, 0);
        ActActorDatabase_DrawAll(ActorDatabase, 0, this, true, true);
        RRenderer_FlushDrawLists(fgRenderer, 0);
        RRenderer_DisableAlphaWrites(fgRenderer, 0);
    }
    DrawPostProcessingEffects();
    if (HeadlightsEnabled && (SimCars.first[0]->renderObject->damagedZones & kHeadlightsBroken) != kHeadlightsBroken)
        AddPlayerHeadlight();
}

// ---- RPlayerViewCamera

// FUNC_AT(0x0008a3f0)
RPlayerViewCamera* RPlayerViewCamera::Construct(RCamera *camera) {
    RRenderWorldCamera::Construct(camera);
    vtable = RPlayerViewCameraVtable;
    return this;
}

// FUNC_AT(0x0008a410)
void RPlayerViewCamera::Destruct() {
    vtable = RPlayerViewCameraVtable;
    RRenderWorldCamera::Destruct();
}

// FUNC_AT(0x0008a4d0)
RPlayerViewCamera* RPlayerViewCamera::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RPlayerViewCamera));
    return this;
}

// FUNC_AT(0x0008a420)
void RPlayerViewCamera::ConfigureView() {
    fgRenderer->unknown20 = 0;
    RefreshLODMultiplier();
    float range = CullDistance;
    if (camera->fieldOfView < kNarrowFieldOfView)
        range = range * kNarrowCullScale;
    float fieldOfView = camera->fieldOfView;
    if (fgRenderer->widescreen)
        fieldOfView = fieldOfView * kWidescreenFovScale;
    Coord4 position = *MatrixRow(&camera->matrix, 3);
    RRenderWorldCulling_Setup2dFrustrum(WorldCulling, 0, &position, &camera->matrix, fieldOfView, range);
}
