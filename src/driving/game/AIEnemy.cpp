#pragma fp_contract(off)

#include "AIEnemy.h"

#include <bit>

#include "../anim/Actor.h"              // ActActor
#include "../anim/IK.h"                 // ActIKSolveInfo
#include "../eagl/Transform.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "Simulation.h"                 // fgSimulation
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"      // LookupFXHandle, LocateFX
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemy (0x0001c5e0, 0x0001ca50-0x0001cb9a), AICharacterEnemyRider's folded methods (0x0001d530,
// 0x0001d990, 0x0001fd30, 0x00020e30), AICharacterEnemyDriver (0x0001d5f0-0x0001d9cf), and the helpers placed among
// them (0x0001c600, 0x0001c620, 0x0001d9d0, 0x0001d9f0, 0x00022770), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

namespace {

constexpr float kFacingWindow = 1.0f / 180.0f;   // TargetIsAcquired's: a degree either way, in turns
static_assert(std::bit_cast<uint32_t>(kFacingWindow) == 0x3bb60b61, "the original's constant");
constexpr float kRandomScale = 1.0f / 65536.0f;

// The driver's idle animations, in turn
const int32_t kDriverIdleAnimations[3] = {8, 8, 7};

// The driver's shoulders' IK axes
const Coord4 kDriverShoulderAxes[2] = {
    {0.0f, -1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
};
// ... and its wrists' pose weights
const float kDriverWristWeights[2] = {1.0f, 1.0f};

// atan_turns answers in ST0 unrounded; read as the double it is rather than through its float declaration
typedef double (*UnroundedTurnsFn)(float y, float x);
double AtanTurns(float y, float x) {
    return reinterpret_cast<UnroundedTurnsFn>(&atan_turns)(y, x);
}

const Coord3 &BodyPosition(PhysicsObject *object) {
    return fgSimulation.GetRigidBody(object->rigidBodySlot)->position;
}

} // namespace

const char kEnemyZoneMap[60] = {
    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  3, 4, 4, 2,  3, 4, 4, 2,
    3, 4, 4, 2,  5, 4, 4, 5,  5, 4, 4, 5,  7, 7, 6, 6,  7, 7, 6, 6,
    7, 7, 6, 6,  7, 7, 6, 6,  7, 7, 6, 6,  7, 7, 6, 6,  7, 7, 6, 6,
};

// The driver's hands' IK, the targets on the wheel set each step (initial values the original's)
// XBE_GLOBAL(0x001b52c0, 0x60)
ActIKSolveInfo DriverHandIK[2] = {
    {{std::bit_cast<float>(0xbf239581u), std::bit_cast<float>(0x3d89374cu), std::bit_cast<float>(0x3ea4dd2fu), 1.0f},
     {std::bit_cast<float>(0x3f5db22du), -0.5f, 0.0f, 1.0f}, 1.0f, false, {}},
    {{std::bit_cast<float>(0xbe4fdf3bu), std::bit_cast<float>(0x3d48b439u), std::bit_cast<float>(0x3ea66666u), 1.0f},
     {std::bit_cast<float>(0x3f5db22du), -0.5f, 0.0f, 1.0f}, 1.0f, false, {}},
};

// ---------------------------------------------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0001c600)
float MinFloat(float a, float b) {
    return a < b ? a : b;
}

// FUNC_AT(0x0001c620)
float MaxFloat(float a, float b) {
    return a > b ? a : b;
}


// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemy
// ---------------------------------------------------------------------------------------------------------------

void AICharacterEnemy::PlayAnimation(int index) {
    animation = index;
    actorLink->actor->SetNewAnimation(animBank, animation);
    crossFade = NULL;
}

void AICharacterEnemy::CrossFadeTo(int bank, int index) {
    animation = index;
    if (crossFade == NULL)
        crossFade = static_cast<CrossFadeAnimationController *>(
            actorLink->actor->SetNewCrossFadeAnimation(bank, animation, 0.25f));
    else
        crossFade->StartNewCrossFade(bank, animation, 0.25f);
}

// FUNC_AT(0x0001ca50)
int AICharacterEnemy::TargetIsInRangeBase() {
    return VU0_v3distancesquare(&BodyPosition(fgSimulation.cars.first[0]), transform.mtx[3]) < targetRangeSquared;
}

// FUNC_AT(0x0001c5e0)
int AICharacterEnemy::NeedsToReloadBase() {
    if (clipSize == 0)
        return 0;
    return rounds <= 0;
}

// FUNC_AT(0x0001ca90)
int AICharacterEnemy::TargetIsAcquiredBase() {
    MATRIX4 weaponTransform;
    actorLink->actor->GetWeaponPosition(&weaponTransform, true, 0);
    const MATRIX4 *from = weapon == kEnemyWeapon3 ? &transform : &weaponTransform;

    // Two looks at the player's car's body: z from the first, x from the second
    RigidBody *forZ = fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot);
    RigidBody *forX = fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot);
    float bearing = float(AtanTurns(float(forX->position.x - from->mtx[3][0]),
                                    float(forZ->position.z - from->mtx[3][2])));
    double turn = bearing - AtanTurns(from->mtx[2][0], from->mtx[2][2]);
    if (turn < -0.5)
        turn += 1.0;
    if (turn > 0.5)
        turn -= 1.0;
    targetSide = turn > 0.0;
    return -kFacingWindow < turn && turn < kFacingWindow;
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyRider
// ---------------------------------------------------------------------------------------------------------------

RigidBody *AICharacterEnemyRider::VehicleBody() {
    return fgSimulation.GetRigidBody(vehicle->rigidBodySlot);
}

// FUNC_AT(0x0001d530)
void AICharacterEnemyRider::GetRotPos(AICharacterEnemyRider *rider, MATRIX4 *transform, Coord4 *velocity) {
    *transform = rider->VehicleBody()->info->orientation;
    transform->mtx[3][0] = rider->VehicleBody()->position.x;
    transform->mtx[3][1] = rider->VehicleBody()->position.y;
    transform->mtx[3][2] = rider->VehicleBody()->position.z;
    transform->mtx[3][3] = 1.0f;
    const Coord3 &carVelocity = rider->VehicleBody()->velocity;
    velocity->x = carVelocity.x;
    velocity->y = carVelocity.y;
    velocity->z = carVelocity.z;
}

// FUNC_AT(0x00020e30)
void AICharacterEnemyRider::GetRotPos(AICharacterEnemyRider *rider, MATRIX4 *transform) {
    *transform = rider->VehicleBody()->info->orientation;
    transform->mtx[3][0] = rider->VehicleBody()->position.x;
    transform->mtx[3][1] = rider->VehicleBody()->position.y;
    transform->mtx[3][2] = rider->VehicleBody()->position.z;
    transform->mtx[3][3] = 1.0f;
}

// FUNC_AT(0x0001fd30)
PBondCar* AICharacterEnemyRider::GetVehiclePtr() {
    return vehicle;
}

// FUNC_AT(0x0001d990)
void AICharacterEnemyRider::UpdateRotPos() {
    MATRIX4 ride;
    GetRotPos(this, &ride);
    actorLink->actor->GetActorLocalPosOri(&transform);
    VU0_MATRIX4_mult(&transform, &transform, &ride);
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyDriver
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0001d5f0)
AICharacterEnemyDriver* AICharacterEnemyDriver::Construct(const MATRIX4 *transform, PBondCar *vehicle) {
    AICharacter::Construct();
    this->vehicle = vehicle;
    steerLeft = NULL;
    steerRight = NULL;
    idleAnimation = 0;
    vtable = reinterpret_cast<void **>(kAICharacterEnemyDriverVtable);
    this->transform = *transform;
    animBank = kBankDriver;
    return this;
}

// FUNC_AT(0x0001d640)
void AICharacterEnemyDriver::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterEnemyDriverVtable);
    AICharacter::Destruct();
}

// FUNC_AT(0x0001d650)
AICharacterEnemyDriver* AICharacterEnemyDriver::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterEnemyDriver));
    return this;
}

// FUNC_AT(0x0001d680)
void AICharacterEnemyDriver::DoInitial() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    state = kStateNeutral;
    ArticleEffect *left = vehicle->renderObject->LookupFXHandle("steerleft", 0);
    ArticleEffect *right = vehicle->renderObject->LookupFXHandle("steerright", 0);
    if (left == NULL || right == NULL)
        return;
    steerRight = right;
    steerLeft = left;

    int bones[2];
    bones[0] = actorLink->actor->GetBoneIndex("Root.LShoulder");
    bones[1] = actorLink->actor->GetBoneIndex("Root.RShoulder");
    bool transformTargets[2] = {false, false};
    actorLink->actor->CreateIKs(2, bones, kDriverShoulderAxes, transformTargets);
    actorLink->actor->SetIKInfoArray(2, BondHandIK);
    bones[0] = actorLink->actor->GetBoneIndex("Root.LWrist");
    bones[1] = actorLink->actor->GetBoneIndex("Root.RWrist");
    actorLink->actor->CreateGlobalPoseOverrides(2, bones, false);
}

// FUNC_AT(0x0001d7f0)
void AICharacterEnemyDriver::DoNeutral() {
    state = kStateIdling;
    if (++idleAnimation >= 3)
        idleAnimation = 0;
    animation = kDriverIdleAnimations[idleAnimation];
    actorLink->actor->SetNewAnimation(animBank, animation);
}

// FUNC_AT(0x0001d840)
void AICharacterEnemyDriver::HandleInterrupts() {
    if (steerLeft == NULL || steerRight == NULL)
        return;
    MATRIX4 left, right;
    vehicle->renderObject->LocateFX(steerLeft, &left, true);
    vehicle->renderObject->LocateFX(steerRight, &right, true);
    MATRIX4 toCar;
    GetRotPos(this, &toCar);
    OrthoInverse(&toCar);

    // The wheel's places in the car's frame: the IK targets, and the wrists' poses
    MATRIX4 wrists[2];
    VU0_MATRIX4_mult(&wrists[0], &left, &toCar);
    VU0_MATRIX4_mult(&wrists[1], &right, &toCar);
    for (int i = 0; i < 2; i++) {
        const float *place = wrists[i].mtx[3];
        DriverHandIK[i].target = {place[0], place[1], place[2], place[3]};
    }
    actorLink->actor->SetIKInfoArray(2, DriverHandIK);
    actorLink->actor->SetGlobalPoseOverrides(2, reinterpret_cast<const Transform *>(wrists), kDriverWristWeights);
}
