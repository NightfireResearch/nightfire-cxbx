#pragma fp_contract(off)

#include "WorldCamera.h"
#include "../Vtables.h"   // our copies of the game's vtables

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <xmmintrin.h>

#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../anim/Actor.h"              // ActActorDatabase
#include "../anim/AnimEngine.h"         // Handle
#include "../game/AISplinePath.h"
#include "../game/AIVehicle.h"
#include "../game/EffectsGallery.h"
#include "../game/Gallery.h"
#include "../game/Gfx.h"
#include "../game/Hud.h"
#include "../game/Simulation.h"
#include "../render/Colorize.h"
#include "../render/Decals.h"
#include "../render/Fog.h"
#include "../render/Gain.h"
#include "../render/LensFlare.h"
#include "../render/Lightning.h"
#include "../render/Lights.h"
#include "../render/ParticleCache.h"
#include "../render/Particles.h"
#include "../render/Particulate.h"
#include "../render/PathEngine.h"
#include "../render/PostProcessing.h"
#include "../render/Reflection.h"
#include "../render/ShadowMap.h"
#include "../render/SkyWater.h"
#include "../render/WorldCulling.h"
#include "../world/World.h"              // ArticleOf
#include "../data/Carp.h"
#include "../data/DebugVariables.h"
#include "../data/IniFiles.h"
#include "../eagl/RenderContext.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/CoreContainers.h"     // GameVector
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../game/Debris.h"
#include "../game/Grenade.h"
#include "../game/Missile.h"
#include "../game/Sentry.h"
#include "../game/Shell.h"
#include "../game/TyreTrack.h"
#include "../game/WeaponCameras.h"        // RMissileCam, RSniperZoom
#include "../game/WeaponEffects.h"        // RBulletStreak
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
#include "../render/Renderer.h"
#include "../../common/xbeClass.h"        // LAYOUT_32

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

struct RigidBody;

namespace {

// The flags of a camera animation system's first instance (CARP::Instance::flags)
enum AnimSystemInstanceFlags : uint8_t {
    kInstanceFlag10 = 0x10,             // UpdateAnimationCam answers false
};

// The scene object's description, as far as SetAnchor reads it: its first word is its group
struct SceneObjDescFields {
    Ptr32<UGroup> group;
};

} // namespace

// ---- globals
#define RWorldCameraVtable ((void **)Vtable_RWorldCamera)
#define RRenderWorldCameraVtable ((void **)Vtable_RRenderWorldCamera)
#define RPlayerViewCameraVtable ((void **)Vtable_RPlayerViewCamera)
// The Simulation's cars, the player's first (Ghidra's name)
// An object only the static initialisers construct and destroy (name ours)
// XBE_GLOBAL(0x001f2610, 0x4c)
alignas(16) EAGL::GeoPrimState WorldCameraGeoPrimState;
// XBE_GLOBAL(0x001f2a4c, 0x4)
uint32_t GameTick;   // the game's tick (RGlareManager.cpp's GlareTick)
// GetAnchorMatrix4's matrix, made the identity on its first call (a function-local static and its guard)
// XBE_GLOBAL(0x001f2d20, 0x40)
alignas(16) MATRIX4 AnchorMatrix;
// XBE_GLOBAL(0x001f2d60, 4)
uint32_t AnchorMatrixGuard;
// The anchor's position and velocity without one (names ours): zero, w 1; the callers read all four words
// XBE_GLOBAL(0x001c47f0, 0x10)
alignas(16) Coord4 AnchorNoPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
// XBE_GLOBAL(0x001c4800, 0x10)
alignas(16) Coord4 AnchorNoVelocity = { 0.0f, 0.0f, 0.0f, 1.0f };
// XBE_GLOBAL(0x001c4618, 1)
static bool HeadlightsEnabled = true;                   // "Enable headlights"
// XBE_GLOBAL(0x001b6638, 8)
const char *OffOnNames[2] = { "Off", "On" };
// XBE_GLOBAL(0x001c4620, 1)
static bool PostProcessingEnabled = true;               // (names ours)
// XBE_GLOBAL(0x001c4624, 0x4)
float CullDistance = 400.0f;                                // ESetCullDistanceFactor's
// XBE_GLOBAL(0x001f2c50, 0x1)
uint8_t WorldViewMade;   // set by RRenderWorldCamera; RVehicle::Render reads it
// XBE_GLOBAL(0x001ec46c, 4)
static uint32_t ParticleSystemsUnknown;
// XBE_GLOBAL(0x001ef0c0, 0x2c08)
alignas(16) CachedDrawInfo Pass0Draws;   // pass 0 of the track's draws, drawn last
// XBE_GLOBAL(0x001ec4b8, 0x2c08)
static CachedDrawInfo Pass1Draws;                       // pass 1, drawn after the cars (names ours)

namespace {

constexpr int32_t kTypeRigidBody = 1;           // PhysicsObject::type: its body is a RigidBody
constexpr int kBodySlots = 0x40;                // the slots an anchor's body may be in
constexpr uint32_t kTagCams = 0x43616d73;       // 'Cams'
constexpr uint32_t kHandleSize = sizeof(Handle);   // an RAnimEngine::Handle
constexpr uint32_t kCameraStimulus = 8;         // PlayCurrentAnimation's stimulus
constexpr float kDefaultFieldOfView = 33.0f;
constexpr float kMinZoom = 2.0f;
constexpr float kSecondsPerFrame = 1.0f / 60.0f;
constexpr float kAnimZoomStep = 1.5f;
constexpr float kLookAtEase = 0.05f;            // of the way to the new lookAt each call
constexpr uint8_t kAnimMirror = 0x1;            // UpdateAnimationCam's flags
constexpr uint8_t kAnimFlag2 = 0x2;             // FlipFrameRows for the axes, not TurnFrameRows
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
LAYOUT_32(sizeof(ActionQueue) == 0x974, "an ActionQueue is 0x974 bytes");

RigidBody *RigidBodyOf(const PhysicsObject *object) {
    return fgSimulation.GetRigidBody(object->rigidBodySlot);
}

SimpleRigidBody *SimpleBodyOf(const PhysicsObject *object) {
    return fgSimulation.GetSimpleRigidBody(object->rigidBodySlot);
}

void DeleteHandle(Handle *handle) {
    handle->Stop();
    Handle::OperatorDelete(handle, kHandleSize);
}

uint16_t CameraAnimId(const CARP::Instance *instance) {
    return ArticleOf(instance)->animInfos->systemId;
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
AIVehicle *AIVehicleOf(PVehicle *car) {
    typedef AIVehicle *(PVehicle::*Method)();
    return (car->*XbeVirtual<Method>(car, 10))();
}

// A vehicle's slot 41: a wheel's tyre track, NULL if none
RTyreTrack *TyreTrack(PVehicle *car, int wheel) {
    typedef RTyreTrack *(PVehicle::*Method)(int wheel);
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
        aiSplinePath->Destruct();
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
        AnchorMatrix = { { { 1.0f, 0.0f, 0.0f, 0.0f },
                           { 0.0f, 1.0f, 0.0f, 0.0f },
                           { 0.0f, 0.0f, 1.0f, 0.0f },
                           { 0.0f, 0.0f, 0.0f, 1.0f } } };
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
        return reinterpret_cast<Coord3 *>(&AnchorNoPosition);
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
        return reinterpret_cast<Coord3 *>(&AnchorNoVelocity);
    if (object->flags & PhysicsObject::kSimpleBody)
        return &SimpleBodyOf(object)->velocity;
    AIVehicle *vehicle = AIVehicleOf(fgSimulation.cars.first[0]);
    if (vehicle != NULL && vehicle->GetSplinePath() != NULL)
        return &vehicle->GetSplinePath()->path->velocity;
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
    animHandle = Handle::Create(1, GameTick, instance, NULL, NULL);
    animSystemId = animHandle->GetInstanceSystemID(0);
    return animHandle != NULL;
}

// FUNC_AT(0x000977d0)
bool RWorldCamera::LoadAISplinePathAnimation(void *path) {
    if (aiSplinePath == NULL) {
        AISplinePath *made = static_cast<AISplinePath *>(OperatorNew(sizeof(AISplinePath)));
        aiSplinePath = made != NULL ? made->Construct(static_cast<CARP::AISpline *>(path)) : NULL;
    } else {
        aiSplinePath->Reset(static_cast<CARP::AISpline *>(path));
    }
    aiSplinePath->path->SetNextPath(NULL);
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
    animHandle = Handle::Create(1, GameTick, &cameraAnims[i], NULL, NULL);
    if (animHandle == NULL)
        return false;
    animSystemId = id;
    return true;
}

// FUNC_AT(0x00097930)
bool RWorldCamera::PlayCurrentAnimation() {
    if (animHandle == NULL)
        return false;
    animHandle->ProcessStimuli(animSystemId, kCameraStimulus, fgSimulation.stepCount, 2);
    return animHandle->IsSystemPlaying(animSystemId);
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
    CARP::Instance *instance = animHandle->GetFirstSystemInstance(animSystemId);
    if (instance == NULL || ArticleOf(instance) == NULL)
        return false;
    const CARP::AnimInfo *track = &ArticleOf(instance)->animInfos[instance->procAnimType];
    if (instance->flags & kInstanceFlag10)
        return false;
    bool playing = animHandle->IsSystemPlaying(animSystemId);
    if (!playing)
        return playing;

    float time = (float)((double)int32_t(instance->procAnimIndex * track->frameRate) * kSecondsPerFrame);
    int key = FloorToInt(time);
    float t = (float)(time - (double)uint32_t(key));    // the key loaded signed, 2^32 added when negative
    uint32_t from = key;
    if (from >= track->frameCount)
        from = track->frameCount - 1;
    uint32_t to = from + 1;
    if (to >= track->frameCount)
        to = track->frameCount - 1;
    Coord4 rotation;
    VU0_fastqslerp(&track->keys[from].rotation, &track->keys[to].rotation, &rotation, t);
    VU0_quattom4(frame, &rotation);
    if (flags & kAnimFlag2)
        FlipFrameRows(frame);
    else
        TurnFrameRows(frame);
    BlendVectors(&eye, AsVector4(&track->keys[from].position), AsVector4(&track->keys[to].position), t);
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
    SetAnchor(fgSimulation.cars.first[0]);
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
        aiSplinePath->Destruct();
        OperatorDelete(aiSplinePath);
        aiSplinePath = NULL;
    }
}

// ---- RRenderWorldCamera

// FUNC_AT(0x0008c8e0)
RRenderWorldCamera* RRenderWorldCamera::Construct(RCamera *camera) {
    RViewCamera::Construct(camera);
    vtable = RRenderWorldCameraVtable;
    TheLensFlareManager->Enable(true);
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
    TheReflection->EnableReflectionMapWarpage(false);
    ConfigureViewVirtual();
}

// An item with the far-plane test is tested against the curtains with no radius.
// FUNC_AT(0x0008c9b0)
void RRenderWorldCamera::CullModule(CullNextItem next, CullSetItem set, bool) {
    Coord4 sphere;
    float height;
    bool checkFar;
    float farScale;
    for (intptr_t item = next(&sphere, &height, &checkFar, &farScale); item != -1;
         item = next(&sphere, &height, &checkFar, &farScale)) {
        float distance = 0.0f;
        if (!fgWorldCulling.IsInFrustum2d(&sphere, sphere.w, checkFar, farScale, &distance)) {
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
        ActActorDatabase::PrepareActorsForCulling();
        CullModule(ActActorDatabase::GetNextActorCullInfo, ActActorDatabase::SetActorCull, false);
    }
    RSceneObj::PrepareSceneObjsForCulling(list);
    CullModule(RSceneObj::GetNextSceneObjCullInfo, RSceneObj::SetSceneObjectCull, false);
    TheEffectsGallery->CULL_Start();
    CullModule(GFX::CULL_GetNextCanvas, GGallery::CULL_SetCanvas, false);
    return list;
}

// FUNC_AT(0x0008cb30)
void RRenderWorldCamera::DrawStaticWorldGeometry(CachedDrawInfo *list) {
    fgRenderer->EnableAlphaWrites();
    Fog->EnableFog();
    fgRender->DrawWorld(list, 1);
    TargetPicker.UpdateTargets();
    GHud::TheApp()->UpdateTargets();
    Fog->DisableFog();
    fgRenderer->DisableAlphaWrites();
}

// Pass 0, farthest first, without depth writes.
// FUNC_AT(0x0008cb90)
void RRenderWorldCamera::DrawFinalStaticWorldGeometry(CachedDrawInfo *list) {
    fgRenderer->renderContext->SetZWritesEnable(0);
    Fog->EnableFog();
    RRenderSharedData::SendPerViewPort();
    fgRender->DrawPass(list, 0, 0, 2);
    Fog->DisableFog();
    fgRenderer->renderContext->SetZWritesEnable(1);
}

// FUNC_AT(0x0008cbf0)
void RRenderWorldCamera::DrawPostProcessingEffects() {
    if (!PostProcessingEnabled)
        return;
    ThePostProcessing->GrabBackBuffer();
    SniperZoom->GrabBackBuffer();
    fgRenderer->currentView->SetDeviceTransformMode();
    TheLensFlareManager->TestFlares(this);
    TheLensFlareManager->DrawFlares();
    Colorize->Draw();
    TheGain->Draw();
    SniperZoom->Draw();
    ThePostProcessing->Draw();
    MissileCam->Draw();
}

// FUNC_AT(0x0008cc70)
void RRenderWorldCamera::DrawVehiclesAndDeferredSceneObjects() {
    fgRenderer->EnableAlphaWrites();
    Fog->EnableFog();
    bool vehiclesAllowed = RRenderSharedData::SetVehiclesAllowed(true);
    RRenderSharedData::SendPerViewPort();
    TheShadowMap->SetupFrameBuffers();
    RRenderSharedData::SendPerViewPort();
    RSceneObj::RenderAllDrawLast();
    fgRenderer->DisableAlphaWrites();
    RRenderSharedData::SendPerViewPort();
    fgRenderer->FlushDrawLists();
    NullFunction();             // called on the shadow map
    RRenderSharedData::SetVehiclesAllowed(vehiclesAllowed);
    Fog->DisableFog();
}

// FUNC_AT(0x0008cd10)
void RRenderWorldCamera::DrawTyreTracks() {
    for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
        for (int wheel = 0; wheel < kWheels; wheel++) {
            if (TyreTrack(*car, wheel) != NULL)
                TyreTrack(*car, wheel)->Draw();
        }
    }
}

// FUNC_AT(0x0008cd60)
void RRenderWorldCamera::DrawBulletStreaks() {
    for (Shell **shell = fgSimulation.shells.first; shell != fgSimulation.shells.last; shell++) {
        Coord4 direction;
        VU0_v4unitxyz(&SimpleBodyOf(*shell)->velocity, &direction);
        int streak = (*shell)->streakKind;
        if (streak > -1)
            RBulletStreak::Add(&SimpleBodyOf(*shell)->position, AsCoord3(&direction), streak);
    }
    RayShell::DrawTracers();
    RayShell::DrawFlashes();
    for (Missile **missile = fgSimulation.missiles.first; missile != fgSimulation.missiles.last; missile++)
        (*missile)->Render(viewId == 0);
    for (Grenade **grenade = fgSimulation.grenades.first; grenade != fgSimulation.grenades.last; grenade++)
        (*grenade)->Render(viewId == 0);
}

// A light ahead of the player's car.
// FUNC_AT(0x0008ce50)
void RRenderWorldCamera::AddPlayerHeadlight() {
    PVehicle *car = fgSimulation.cars.first[0];
    const Coord3 *position = &RigidBodyOf(car)->position;
    const Coord4 *forward = MatrixRow(&RigidBodyOf(car)->info->orientation, 2);
    Coord4 light = { position->x, position->y, position->z, kHeadlightW };
    Coord4 colour = { kHeadlightColour, kHeadlightColour, kHeadlightColour, kHeadlightColour };
    light.x = (float)((double)forward->x * kHeadlightReach + light.x);
    light.y = (float)((double)forward->y * kHeadlightReach + light.y);
    light.z = (float)((double)forward->z * kHeadlightReach + light.z);
    fgLightManager->AddPositionalLight(&light, &colour);
}

// FUNC_AT(0x0008cf10)
void RRenderWorldCamera::DrawEffects() {
    DrawBulletStreaks();
    RWindow::DrawBrokenWindows(kBrokenWindowsUnknown);
    TheDecalManager->DrawDecals();
    RBulletStreak::Draw();
    TheLightning->Draw();
    RSceneObj::RenderAllDeferredEffects();
    GFX::Update();
    Debris->Draw();
    TheGlareManager->DrawGlares(true);
    fgParticleSystems->UpdateSpawnAllSystems();
    ParticleSystemsUnknown = fgParticleSystems->cache.count;
    fgParticleSystems->UpdateAndRenderAllSystems();
    Sentry::DrawMuzzleFlashes();
    fgParticulate->Update();
    fgParticulate->Draw();
}

// FUNC_AT(0x0008cfa0)
void RRenderWorldCamera::DoRender() {
    CachedDrawInfo *list = PerformCulling();
    RRenderSharedData::SendPerViewPort();
    if (ActorDatabase != NULL)
        ActorDatabase->SetupFOVConversions(this);
    DrawStaticWorldGeometry(list);
    fgRender->CopyDrawPasses(list, &Pass0Draws, 0, 0);
    fgRender->CopyDrawPasses(list, &Pass1Draws, 1, 1);
    if (ActorDatabase != NULL)
        ActorDatabase->DrawAll(this, false, false);

    fgRenderer->EnableAlphaWrites();
    Fog->EnableFog();
    RRenderSharedData::SendPerViewPort();
    RSceneObj::RenderAllNormal();
    Fog->DisableFog();
    fgRenderer->DisableAlphaWrites();
    DrawTyreTracks();
    DrawVehiclesAndDeferredSceneObjects();

    Fog->EnableFog();
    RRenderSharedData::SendPerViewPort();
    fgRender->DrawPass(&Pass1Draws, 1, 1, 2);
    Fog->DisableFog();
    if (ActorDatabase != NULL)
        ActorDatabase->DrawActorWeapons(this, false);
    DrawFinalStaticWorldGeometry(&Pass0Draws);
    TargetPicker.DrawTargetingSystem();
    DrawEffects();
    if (ActorDatabase != NULL) {
        fgRenderer->EnableAlphaWrites();
        ActorDatabase->DrawAll(this, true, true);
        fgRenderer->FlushDrawLists();
        fgRenderer->DisableAlphaWrites();
    }
    DrawPostProcessingEffects();
    if (HeadlightsEnabled && (fgSimulation.cars.first[0]->renderObject->damagedZones & kHeadlightsBroken) != kHeadlightsBroken)
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
    fgWorldCulling.Setup2dFrustrum(&position, &camera->matrix, fieldOfView, range);
}
