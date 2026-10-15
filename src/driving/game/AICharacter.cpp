#pragma fp_contract(off)

#include "AICharacter.h"

#include <string.h>

#include "AIPedestrian.h"
#include "CharacterSound.h"             // ACharacter
#include "Human.h"
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "../anim/Actor.h"              // ActActor, ActActorDatabase
#include "../anim/Character.h"          // ActCharacter
#include "../camera/CameraIniLoader.h"  // fgCameraTables
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/PhysicsUtil.h"      // Util_VecToPoleDepthAndLong, Util_CharMapLookup
#include "../engine/UMemory.hpp"
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../platform/RealMath.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// AICharacter (0x0001c190-0x0001ca50, 0x0001da50, 0x00027c80) and AICharacterPassenger (0x00023670-0x00023950),
// ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const MATRIX4 kIdentity = { {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
} };

constexpr float kThrownDrag = 0.5f;         // a thrown character's horizontal speed lost per second, as a share
constexpr float kGravity = 13.0f;
constexpr float kGroundClearance = 0.35f;   // a bounce is under the ground less this
constexpr float kBounceKeep = 0.25f;        // the horizontal speed kept by a bounce
constexpr float kBounceRebound = -0.3f;     // and the vertical
constexpr float kZoneLongitudeOffset = 0.1f;

// The passengers, by kind (0-4)
const int32_t kPassengerAnimations[5] = {8, 8, 8, 2, 0};
const int32_t kPassengerBanks[5] = {0, 0, 0, 16, 12};
const char *const kPassengerDescriptions[5] = {
    "3.c_alura.h_alura.t_alura.100",
    "3.c_domm.h_dommin.t_dommin.100",
    "3.c_zoe.h_zoe.t_zoe.100",
    "3.c_alura.h_alura.t_alura.100",
    "3.c_zoe.h_zoe.t_zoe.100",
};
constexpr int kFirstPassenger = 1;
constexpr int kLastPassenger = 5;

PointerListNode *ActorHandle(AICharacterActorLink *link) {
    return reinterpret_cast<PointerListNode *>(link);
}

} // namespace

// ---- construction

// FUNC_AT(0x0001c870)
AICharacter* AICharacter::Construct() {
    vtable = kAICharacterVtable;
    transform = kIdentity;
    rounds = 15;
    clipSize = 15;
    state = kStateInitial;
    subState = 0;
    animation = 0;
    weapon = 0;
    unknown6c = 0;
    female = 0;
    targetRangeSquared = 75.0f * 75.0f;
    hitPoints = NULL;
    newDamage = 0.0f;
    human = NULL;
    animBank = 0;
    targetSide = 0;
    targetBearing = 0;
    shotInterval = 60;
    nextShotStep = 0;
    voice = NULL;
    actorLink = NULL;
    firing = 1;
    scoreable = 1;
    animController = NULL;
    crossFade = NULL;
    hitShape = NULL;
    hasCommandTarget = 0;
    thrown = 0;
    bounces = 0;
    avoidRadius = 0.0f;
    avoiding = 0;
    commandTarget = {0.0f, 0.0f, 0.0f};
    VU0_MATRIX4Init(&thrownTransform);
    centre = {-0.08f, 0.0f, 0.12f};
    thrownVelocity = {0.0f, 0.0f, 0.0f};
    avoidCentre = {0.0f, 0.0f, 0.0f};
    return this;
}

// FUNC_AT(0x0001c1d0)
void AICharacter::Destruct() {
    vtable = kAICharacterVtable;
    if (voice != NULL)
        voice->CallDelete(1);
    if (actorLink != NULL)
        ActActorDatabase::KillActorByHandle(ActorHandle(actorLink));
}

// FUNC_AT(0x0001da50)
void AICharacter::DestructThunk() {
    Destruct();
}

// FUNC_AT(0x0001c640)
AICharacter* AICharacter::DeleteBase(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacter));
    return this;
}

// ---- the track's characters

// FUNC_AT(0x0001c190)
void AICharacter::Init() {
    AICharacterPedestrian::Init(kMaxPedestrians);
    AICharacterPassenger::Init();
    AICharacterBond::Init();
    AICharacterHands::Init();
}

// FUNC_AT(0x0001c1b0)
void AICharacter::Shutdown() {
    AICharacterHands::Shutdown();
    NullFunction();
    NullFunction();
    AICharacterPedestrian::Destroy();
}

// ---- the step

// FUNC_AT(0x0001c2b0)
void AICharacter::Execute() {
    CallSlot(kSlotUpdateRotPos);
    CallSlot(kSlotHandleInterrupts);

    if (thrown) {
        thrownVelocity.x = float((1.0 - fgSimulation.timeStep * double(kThrownDrag)) * thrownVelocity.x);
        thrownVelocity.y = float(thrownVelocity.y - fgSimulation.timeStep * double(kGravity));
        thrownVelocity.z = float((1.0 - fgSimulation.timeStep * double(kThrownDrag)) * thrownVelocity.z);
        Coord4 step, next;
        VU0_v4scale(&thrownVelocity, fgSimulation.timeStep, &step);
        Coord3 *position = reinterpret_cast<Coord3 *>(thrownTransform.mtx[3]);
        VU0_v3add(&step, position, &next);
        if (bounces) {
            human->worldPos.FindClosestFace(&Position(), true);
            double floor = human->worldPos.HeightAtPoint(&Position(), false) - kGroundClearance;
            if (next.y < floor) {
                thrownVelocity.x = thrownVelocity.x * kBounceKeep;
                thrownVelocity.y = thrownVelocity.y * kBounceRebound;
                thrownVelocity.z = thrownVelocity.z * kBounceKeep;
                next.y = float(floor);
            }
        }
        *position = *Xyz(&next);
    }

    // The original's switch over the 23 states, each calling its handler's slot
    if (uint32_t(state) <= kStateDead)
        CallSlot(kSlotFirstState + state);

    if (voice != NULL) {
        if (voice->voice != NULL) {
            voice->position = Position();
        } else {
            voice->CallDelete(1);
            voice = NULL;
        }
    }
}

// FUNC_AT(0x0001c230)
ACharacter* AICharacter::GetACharacter() {
    if (voice == NULL) {
        void *block = ABaseSound::OperatorNew(sizeof(ACharacter), "ACharacter");
        voice = block != NULL ? static_cast<ACharacter *>(block)->Construct("Characters") : NULL;
    }
    return voice;
}

// FUNC_AT(0x0001c5a0)
int AICharacter::GetZoneLastHit() {
    return zoneLastHit;
}

// FUNC_AT(0x0001c9c0)
void AICharacter::MoveToPoint(const Coord3 *point, bool setHeight) {
    Coord4 position;
    actorLink->actor->GetActorWorldPosition(&position);
    Coord4 origin;
    actorLink->actor->GetAnimationOrigin(Xyz(&origin));
    float height = origin.y;
    Coord4 offset;
    VU0_v4sub(&position, &origin, &offset);
    VU0_v4sub(point, &offset, &origin);
    if (!setHeight)
        origin.y = height;
    actorLink->actor->SetAnimationOrigin(Xyz(&origin));
}

// FUNC_AT(0x00027c80)
void AICharacter::SetHitPointLoc(float *hitPoints) {
    this->hitPoints = hitPoints;
    if (hitPoints != NULL)
        initialHitPoints = *hitPoints;
}

// ---- virtual

// FUNC_AT(0x0001c200)
int AICharacter::IsAliveBase() {
    if (state == kStateDead || state == kStateDying)
        return 0;
    return 1;
}

// FUNC_AT(0x0001c220)
void AICharacter::SetActor(AICharacterActorLink *actorLink) {
    this->actorLink = actorLink;
}

// FUNC_AT(0x0001c690)
void AICharacter::NotifyZoneDamageBase(const Coord3 *point) {
    const AICharacterHitShape *shape = hitShape;
    zoneLastHit = 0;
    if (shape == NULL)
        return;

    // The point in the character's frame, on the unit sphere the shape stretches
    MATRIX4 toLocal = transform;
    OrthoInverse(&toLocal);
    Coord4 local;
    local.x = point->x;
    local.y = point->y - shape->lift;
    local.z = point->z;
    VU0_MATRIX4_vect3mult(&local, &toLocal, &local);
    double y = local.y / shape->stretch;
    double scale = 1.0 / shape->radius;
    local.x = float(local.x * scale);
    local.y = float(scale * y);
    local.z = float(scale * local.z);

    float depth, longitude;
    Util_VecToPoleDepthAndLong(Xyz(&local), &depth, &longitude);
    double u = longitude - double(kZoneLongitudeOffset);
    while (u < 0.0)
        u += 1.0;
    while (u > 1.0)
        u -= 1.0;
    if (1.0f < depth)
        depth = 1.0f;
    else if (depth < 0.0f)
        depth = 0.0f;
    if (1.0 < u)
        longitude = 1.0f;
    else if (u < 0.0)
        longitude = 0.0f;
    else
        longitude = float(u);
    zoneLastHit = Util_CharMapLookup(longitude, depth, shape->zoneMap, shape->width, shape->height);
}

// FUNC_AT(0x0001c5b0)
double AICharacter::GetZoneHitPointScaleBase(int zone, float) {
    switch (zone) {
    case 0:
        return 0.0;
    case 1:
        return 6.0;
    default:
        return 1.0;
    }
}

// FUNC_AT(0x0001c850)
void AICharacter::UpdateRotPos() {
    actorLink->actor->GetActorLocalPosOri(&transform);
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterPassenger
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00023670)
AICharacterPassenger* AICharacterPassenger::Construct(const MATRIX4 *transform, int kind) {
    AICharacter::Construct();
    vtable = kAICharacterPassengerVtable;
    unknown140 = 0;
    this->transform = *transform;
    this->kind = kind;
    animBank = kPassengerBanks[kind];
    return this;
}

// FUNC_AT(0x000236c0)
void AICharacterPassenger::Destruct() {
    vtable = kAICharacterPassengerVtable;
    AICharacter::Destruct();
}

// FUNC_AT(0x000236d0)
AICharacterPassenger* AICharacterPassenger::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterPassenger));
    return this;
}

// FUNC_AT(0x00023700)
void AICharacterPassenger::Init() {
    int passenger = glbMissionManager->missionSet->passenger;
    if (passenger < kFirstPassenger || passenger > kLastPassenger)
        return;
    const AttributeVector *seat = fgSimulation.cars.first[0]->attributes.LookupVector("AIC_PASS_POS", NULL);
    ActCharacter::LoadCharacter(kPassengerDescriptions[passenger - kFirstPassenger], NULL, NULL);
    int kind = passenger - kFirstPassenger;
    if (_stricmp(fgSimulation.cars.first[0]->GetCarType(), "helicopter") == 0)
        return;

    MATRIX4 transform;
    VU0_MATRIX4Init(&transform);
    transform.mtx[3][0] = seat->x;
    transform.mtx[3][1] = seat->y;
    transform.mtx[3][2] = seat->z;
    transform.mtx[3][3] = seat->w;
    void *block = UMemory::FastAlloc(sizeof(AICharacterPassenger), "AICharacter");
    AICharacterPassenger *character =
        block != NULL ? static_cast<AICharacterPassenger *>(block)->Construct(&transform, kind) : NULL;
    Human *human = static_cast<Human *>(fgSimulation.SpawnHumanObject(
        0, reinterpret_cast<const Coord3 *>(transform.mtx[2]), reinterpret_cast<const Coord3 *>(transform.mtx[3]), 0));
    PointerListNode *handle;
    ActActorDatabase::GetNewActorHandle(&handle, character, kPassengerDescriptions[kind], NULL,
                                        reinterpret_cast<ActActorTransformCallback>(&AICharacterBond::GetRideRotPos),
                                        &transform, false);
    // The original calls slot 2 (SetActor); a passenger's is AICharacter's
    if (character != NULL)
        character->SetActor(reinterpret_cast<AICharacterActorLink *>(handle));
    if (human != NULL) {
        human->SetAICharacter(character);
        human->Deactivate();
    }
    static_cast<ActActor *>(handle->value)->SetNewAnimation(kPassengerBanks[kind], kPassengerAnimations[kind]);
}

// FUNC_AT(0x00023890)
void AICharacterPassenger::DoNeutral() {
    state = kStateIdling;
    animation = kPassengerAnimations[kind];
    actorLink->actor->SetNewAnimation(animBank, animation);
}

// FUNC_AT(0x000238c0)
void AICharacterPassenger::HandleInterrupts() {
    if (_stricmp(fgSimulation.cars.first[0]->GetCarType(), "helicopter") == 0) {
        actorLink->actor->visible = false;
        return;
    }
    bool interior = fgCameraTables.modes[fgRenderHigh->views[0].camera->cameraMode].interiorView != 0;
    actorLink->actor->visible = !interior;
    if (glbMissionManager->autoDrive)
        actorLink->actor->visible = false;
}
