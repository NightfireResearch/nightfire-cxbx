#pragma fp_contract(off)

#include "AIEnemyRiders.h"

#include <bit>

#include "AIVehicle.h"                  // AIVehicle::active
#include "CharacterSound.h"             // ACharacter
#include "Human.h"
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "../anim/Actor.h"              // ActActor
#include "../anim/AnimEngine.h"         // RAnimEngine::Handle
#include "../eagl/Transform.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"      // RSceneObj::animHandle
#include "../world/Targeting.h"         // WTargetable, TargetPicker
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemySnow (0x0001fac0-0x0001fd2f, 0x0001fd40-0x0002096f, 0x00021df0, 0x000224f0),
// AICharacterEnemySSnow (0x00020970-0x00020e2f), AICharacterEnemySunroof (0x00020ec0-0x00021d3f, but 0x00021600)
// and AICharacterEnemyWindow (0x00021600, 0x00021d40-0x0002276f, but 0x00021df0 and 0x000224f0), ported from the
// listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// The riders' injury zones (AICharacterHitShape)
AICharacterHitShape RiderHitShape = {4, 15, kEnemyZoneMap, 0.35f, 0.15f, 2.0f};

// Snow's animations (bank 8)
constexpr int32_t kSnowArmAnimation = 20;
constexpr int32_t kSnowUnarmAnimation = 19;
constexpr int32_t kSnowIdleAnimation = 7;
constexpr int32_t kSnowBumpAnimation = 0;
const int32_t kSnowLeanAnimations[2] = {13, 16};        // by leanDirection: into the lean, holding it, out of it
const int32_t kSnowLeanHoldAnimations[2] = {14, 17};
const int32_t kSnowLeanEndAnimations[2] = {15, 18};
const int32_t kSnowFireAnimations[5] = {3, 4, 5, 6, 3};
const int32_t kSnowHitAnimations[5] = {9, 8, 10, 11, 12};
const int32_t kSnowThrownAnimations[2] = {1, 2};        // the car destroyed
const int32_t kSnowDeathAnimations[2] = {21, 22};       // ... or not

// SSnow's (bank 1), by weapon
struct SSnowAnimations {
    int32_t idle;
    int32_t bump;
};
const SSnowAnimations kSSnowAnimations[2] = {{1, 0}, {6, 4}};
const int32_t kSSnowHitAnimations[2][2] = {{3, 2}, {8, 7}};
const int32_t kSSnowDeathAnimations[2] = {2, 5};

// Sunroof's (banks 10 and 11), by weapon (1 or 2; row 0 is -1s); the leans by leanDirection - 1
const int32_t kSunroofArmAnimations[3] = {-1, 19, 22};
const int32_t kSunroofUnarmAnimations[3] = {-1, 18, 21};
const int32_t kSunroofReloadAnimations[3] = {-1, 20, -1};
const int32_t kSunroofLeanAnimations[3][4] = {{-1, -1, -1, -1}, {12, 15, 12, 15}, {9, 12, 15, 18}};
const int32_t kSunroofLeanHoldAnimations[3][4] = {{-1, -1, -1, -1}, {13, 16, 13, 16}, {10, 13, 16, 19}};
const int32_t kSunroofLeanEndAnimations[3][4] = {{-1, -1, -1, -1}, {14, 17, 14, 17}, {11, 14, 17, 20}};
const int32_t kSunroofFireAnimations[3][6] = {{-1, -1, -1, -1, -1, -1}, {1, 0, 2, 3, 0, 4}, {0, 1, 0, 1, 0, 1}};
// Hits and deaths (bank 9) by weapon and zone, each list ended by a 0 or by its fourth
const int32_t kSunroofHitAnimations[3][8][4] = {
    {{-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}},
    {{5}, {7}, {11}, {8, 9, 10}, {5}, {5, 6}, {5}, {6}},
    {{2}, {4}, {8}, {5, 6, 7}, {2}, {2, 3}, {2}, {3}},
};
const int32_t kSunroofDeathAnimations[3][8][4] = {
    {{-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}},
    {{0}, {1, 2}, {6}, {3, 4, 5}, {0}, {0}, {0}, {0}},
    {{0}, {1, 2}, {6}, {3, 4, 5}, {0}, {0}, {0}, {0}},
};

// Window's (bank 13)
constexpr int32_t kWindowArmAnimation = 10;
constexpr int32_t kWindowUnarmAnimation = 9;
const int32_t kWindowFireAnimations[5] = {3, 6, 4, 6, 5};
const int32_t kWindowHitAnimations[2] = {7, 8};
const int32_t kWindowDeathAnimations[2] = {0, 1};

// The car's animation system Sunroof stimulates as it comes up and goes down (Handle::ProcessStimuli)
constexpr uint32_t kCarAnimSystem = 0x13;
constexpr uint8_t kStimulusUp = 8;
constexpr uint8_t kStimulusDown = 9;
constexpr int kStimulusMode = 2;

constexpr int kWheels = 4;
constexpr float kBumpCompression = 0.12f;
static_assert(std::bit_cast<uint32_t>(kBumpCompression) == 0x3df5c28f, "the original's constant");
constexpr float kLeanSteering = 0.1f;
constexpr float kHeadBlendRate = 0.05f;
constexpr float kArmBlendRate = 0.03f;
constexpr float kFacingWindow = 1.0f / 180.0f;
static_assert(std::bit_cast<uint32_t>(kFacingWindow) == 0x3bb60b61, "the original's constant");
constexpr float kSnowRange = 1.0f / 3.0f;               // Snow's target within 120 degrees
constexpr float kWindowRangeLeft = 0.25f;               // Window's: 90 degrees one way...
constexpr float kWindowRangeRight = 7.0f / 18.0f;       // ... 140 the other
static_assert(std::bit_cast<uint32_t>(kSnowRange) == 0x3eaaaaab &&
              std::bit_cast<uint32_t>(kWindowRangeRight) == 0x3ec71c72, "the original's constants");
constexpr float kSunroofTurn = 0.005f;
constexpr float kBlendTime = 0.25f;
constexpr float kLeanAcceleration = 45.0f;              // along the car, and 40 across it
constexpr float kLeanAccelerationSide = 40.0f;
constexpr int kDeathFadeSeconds = 4;
constexpr int kWindowArmSeconds = 300;

RigidBody *BodyOf(PhysicsObject *object) {
    return fgSimulation.GetRigidBody(object->rigidBodySlot);
}

int CountAnimations(const int32_t *list) {
    int count = 0;
    for (int i = 0; i < 4 && list[i] != 0; i++)
        count++;
    return count;
}

// The car AI's target while it has an active one, else the player's car
Coord3 GunnerTarget(PBondCar *car) {
    if (car->PVehicle::GetAIGroundVehiclePtr() != NULL && car->PVehicle::GetAIGroundVehiclePtr()->target != NULL &&
        car->PVehicle::GetAIGroundVehiclePtr()->target->active)
        return *Xyz(car->PVehicle::GetAIGroundVehiclePtr()->target->GetPosition());
    return BodyOf(fgSimulation.cars.first[0])->position;
}

// The bearing of the target from the matrix's facing, in turns (-0.5 to 0.5), left unrounded
double TurnsTo(const Coord3 *target, const MATRIX4 *from) {
    float bearing = float(AtanTurns(float(target->x - from->mtx[3][0]), float(target->z - from->mtx[3][2])));
    double turn = bearing - AtanTurns(from->mtx[2][0], from->mtx[2][2]);
    if (turn < -0.5)
        turn += 1.0;
    if (turn > 0.5)
        turn -= 1.0;
    return turn;
}

} // namespace

// FUNC_AT(0x0001fc80)
bool IsBumping(PBondCar *vehicle) {
    float compression = 0.0f;
    for (int wheel = 0; wheel < kWheels; wheel++)
        compression += vehicle->PVehicle::GetSuspensionCompression(wheel);
    return compression * 0.25f >= kBumpCompression;
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemySnow
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0001fac0)
void AICharacterEnemySnow::GetRotPos(AICharacterEnemySnow *rider, MATRIX4 *transform) {
    if (rider->thrown) {
        MatrixCopy(&rider->thrownTransform, transform);
        return;
    }
    *transform = rider->VehicleBody()->info->orientation;
    transform->mtx[3][0] = rider->VehicleBody()->position.x;
    transform->mtx[3][1] = rider->VehicleBody()->position.y;
    transform->mtx[3][2] = rider->VehicleBody()->position.z;
    transform->mtx[3][3] = 1.0f;
}

// FUNC_AT(0x0001fb70)
void AICharacterEnemySnow::GetRotPos(AICharacterEnemySnow *rider, MATRIX4 *transform, Coord4 *velocity) {
    if (rider->thrown) {
        MatrixCopy(&rider->thrownTransform, transform);
        velocity->x = rider->thrownVelocity.x;
        velocity->y = rider->thrownVelocity.y;
        velocity->z = rider->thrownVelocity.z;
        return;
    }
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

// FUNC_AT(0x0001fc70)
void AICharacterEnemySnow::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterEnemySnowVtable);
    AICharacter::Destruct();
}

// FUNC_AT(0x0001fcd0)
void AICharacterEnemySnow::DoTilting() {
    switch (subState) {
    case kStateRunning:
        CallSlot(kSlotFirstState + kStateRunning);
        break;
    case kStateLeaning:
        CallSlot(kSlotFirstState + kStateLeaning);
        break;
    }
}

// FUNC_AT(0x0001fcf0)
void AICharacterEnemySnow::HandleInterrupts() {
    CallSlot(kSlotFirstState + kStateAiming);
    if (vehicle->GetHitPoints() <= 0.0f && state != kStateDying && state != kStateDead)
        state = kStateDying;
}

// FUNC_AT(0x0001fd40)
AICharacterEnemySnow* AICharacterEnemySnow::Construct(const MATRIX4 *transform, int weapon, PBondCar *vehicle) {
    AICharacter::Construct();
    this->vehicle = vehicle;
    vtable = reinterpret_cast<void **>(kAICharacterEnemySnowVtable);
    leanDirection = 0;
    aimPitchDir = 0;
    aimTimer = 0;
    aimTimerLimit = 0;
    targetAngle = 0.0f;
    this->transform = *transform;
    this->weapon = weapon;
    animBank = kBankSnow;
    hitShape = &RiderHitShape;
    for (int i = 0; i < 2; i++) {
        aimAngles[i] = 0.0f;
        blendWeights[i] = 0.0f;
        blendRates[i] = 0.0f;
    }
    return this;
}

// FUNC_AT(0x0001fdd0)
AICharacterEnemySnow* AICharacterEnemySnow::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterEnemySnow));
    return this;
}

// FUNC_AT(0x0001fe00)
void AICharacterEnemySnow::DoNeutral() {
    BondCarControl control;
    if (-kLeanSteering > vehicle->PVehicle::GetCarControl(&control)->steering) {
        state = kStateTilting;
        subState = kStateLeaning;
        leanDirection = 0;
        PlayAnimation(kSnowLeanAnimations[0]);
    } else if (vehicle->PVehicle::GetCarControl(&control)->steering > kLeanSteering) {
        state = kStateTilting;
        subState = kStateLeaning;
        leanDirection = 1;
        PlayAnimation(kSnowLeanAnimations[1]);
    } else if (IsBumping(vehicle)) {
        state = kState14;
        PlayAnimation(kSnowBumpAnimation);
    } else if (weapon == kEnemyWeapon1 && AICharacterEnemy::TargetIsInRange()) {
        state = kStateArming;
        animation = kSnowArmAnimation;
        actorLink->actor->SetNewAnimation(animBank, animation);
        blendRates[0] = kHeadBlendRate;
        blendRates[1] = kArmBlendRate;
        crossFade = NULL;
    } else {
        state = kStateIdling;
        PlayAnimation(kSnowIdleAnimation);
    }
}

// FUNC_AT(0x0001ff90)
void AICharacterEnemySnow::DoArmed() {
    if (!AICharacterEnemy::TargetIsInRange()) {
        state = kStateUnarming;
        PlayAnimation(kSnowUnarmAnimation);
        blendRates[0] = -kHeadBlendRate;
        blendRates[1] = -kArmBlendRate;
        return;
    }
    if (firing && fgSimulation.stepCount >= nextShotStep) {
        state = kStateFiring;
        nextShotStep = shotInterval + fgSimulation.stepCount;
        rounds--;
        int pick = int(fgSimulation.random->Generate()) * 5 >> 16;
        if (!AICharacterEnemy::TargetIsAcquired())
            pick = 0;
        PlayAnimation(kSnowFireAnimations[pick]);
    }
}

// FUNC_AT(0x00020090)
void AICharacterEnemySnow::DoAiming() {
    // The head's yaw follows the target three degrees a step, once it is more than one and a half off
    if (AICharacterEnemy::TargetIsInRange()) {
        double toTarget = targetAngle * double(-360.0f);
        double off = aimAngles[0] - toTarget;
        if (off < 0.0)
            off = -off;
        if (off > 1.5) {
            if (toTarget < aimAngles[0])
                aimAngles[0] -= 3.0f;
            else
                aimAngles[0] += 3.0f;
        }
        if (aimAngles[0] < -90.0f)
            aimAngles[0] = -90.0f;
        if (aimAngles[0] > 90.0f)
            aimAngles[0] = 90.0f;
    }
    // The arm's pitch two degrees a step, until the target is acquired
    if (!AICharacterEnemy::TargetIsAcquired()) {
        switch (aimPitchDir ^ targetSide) {
        case 0:
            aimAngles[1] += 2.0f;
            break;
        case 1:
            aimAngles[1] -= 2.0f;
            break;
        }
        if (aimAngles[1] < -70.0f)
            aimAngles[1] = -70.0f;
        if (aimAngles[1] > 120.0f)
            aimAngles[1] = 120.0f;
    }

    for (int i = 0; i < 2; i++) {
        double weight = double(blendRates[i]) + blendWeights[i];
        blendWeights[i] = float(weight);
        if (weight < 0.0) {
            blendWeights[i] = 0.0f;
            blendRates[i] = 0.0f;
        } else if (weight > 1.0) {
            blendWeights[i] = 1.0f;
            blendRates[i] = 0.0f;
        }
    }
    // In range, the head turns away and back at random intervals
    if (AICharacterEnemy::TargetIsInRange()) {
        if (blendWeights[0] <= 0.0f && ++aimTimer > aimTimerLimit) {
            aimTimer = 0;
            aimTimerLimit = int(fgSimulation.random->Generate()) * 180 >> 16;
            blendRates[0] = kHeadBlendRate;
        }
        if (blendWeights[0] >= 1.0f && ++aimTimer > aimTimerLimit) {
            aimTimer = 0;
            aimTimerLimit = int(fgSimulation.random->Generate()) * 180 >> 16;
            blendRates[0] = -kHeadBlendRate;
        }
    }

    Transform poses[2];
    MATRIX4 turn, tilt;
    BuildRotate(&turn, aimAngles[0], 0.0f, 0.57f, -0.82f);
    BuildRotate(&tilt, -25.0f, 1.0f, 0.0f, 0.0f);
    VU0_MATRIX4_mult(&poses[0], &tilt, &turn);
    BuildRotate(&turn, aimAngles[1], 0.1f, 0.2f, -0.4f);
    BuildRotate(&tilt, -100.0f, 1.0f, 0.0f, 0.0f);
    VU0_MATRIX4_mult(&poses[1], &tilt, &turn);
    if (weapon == kEnemyWeapon1)
        actorLink->actor->SetGlobalPoseOverrides(2, poses, blendWeights);
    else
        actorLink->actor->SetGlobalPoseOverride(0, &poses[0], blendWeights[0]);
}

// FUNC_AT(0x000203c0)
void AICharacterEnemySnow::DoDying() {
    GetRotPos(this, &thrownTransform, reinterpret_cast<Coord4 *>(&thrownVelocity));
    thrown = 1;
    bounces = 1;
    if (vehicle->GetHitPoints() <= 0.0f) {
        // Thrown wide and high
        int spread = int(fgSimulation.random->Generate());
        VU0_v4scaleadd(VehicleBody()->info->orientation.mtx[0], float(spread * double(5.0f / 65536.0f) - 2.5f),
                       &thrownVelocity, &thrownVelocity);
        thrownVelocity.y += 10.0f;
        PlayAnimation(kSnowThrownAnimations[int(fgSimulation.random->Generate()) * 2 >> 16]);
        GetACharacter()->Say("HScream");
    } else {
        VU0_v4scale(&thrownVelocity, 0.25f, &thrownVelocity);
        int spread = int(fgSimulation.random->Generate());
        VU0_v4scaleadd(VehicleBody()->info->orientation.mtx[0], float(spread * double(2.0f / 65536.0f) - 1.0f),
                       &thrownVelocity, &thrownVelocity);
        thrownVelocity.y += 6.0f;
        PlayAnimation(kSnowDeathAnimations[int(fgSimulation.random->Generate()) * 2 >> 16]);
        GetACharacter()->Say("HDeath");
        PBondCar *car = vehicle;
        if (car->scoreable) {
            glbMissionManager->IncKills(1);
            car->PVehicle::SetScoreable(false);
        }
    }
    if (scoreable) {
        glbMissionManager->IncKills(1);
        scoreable = 0;
    }
    state = kStateDead;
}

// FUNC_AT(0x000205b0)
void AICharacterEnemySnow::DoRunning() {
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateNeutral;
        subState = 0;
    }
}

// FUNC_AT(0x000205e0)
void AICharacterEnemySnow::DoLeaning() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    BondCarControl control;
    if (-kLeanSteering > vehicle->PVehicle::GetCarControl(&control)->steering && leanDirection == 0) {
        PlayAnimation(kSnowLeanHoldAnimations[0]);
        return;
    }
    if (vehicle->PVehicle::GetCarControl(&control)->steering > kLeanSteering && leanDirection != 0) {
        PlayAnimation(kSnowLeanHoldAnimations[leanDirection]);
        return;
    }
    subState = kStateRunning;
    PlayAnimation(kSnowLeanEndAnimations[leanDirection]);
}

// FUNC_AT(0x000206d0)
void AICharacterEnemySnow::UpdateRotPos() {
    MATRIX4 ride;
    GetRotPos(this, &ride);
    actorLink->actor->GetActorLocalPosOri(&transform);
    VU0_MATRIX4_mult(&transform, &transform, &ride);
}

// FUNC_AT(0x00020710)
bool AICharacterEnemySnow::TargetIsInRange() {
    MATRIX4 ride;
    GetRotPos(this, &ride);
    Coord3 target = GunnerTarget(GetVehicle());
    if (VU0_v3distancesquare(&target, ride.mtx[3]) > targetRangeSquared)
        return false;
    double turn = TurnsTo(&target, &ride);
    targetAngle = float(turn);
    return -kSnowRange < turn && turn < kSnowRange;
}

// FUNC_AT(0x00020870)
void AICharacterEnemySnow::PlayZoneInjuryAnim(int, float amount, int) {
    float hp = hitPoints != NULL ? *hitPoints : 1.0f;
    if (state == kStateDying || state == kStateDead)
        return;
    if (amount >= hp) {
        AIGroundVehicle *ai = vehicle->PVehicle::GetAIGroundVehiclePtr();
        if (ai != NULL)
            ai->stopped = 1;
        state = kStateDying;
    } else {
        GetACharacter()->Say("HGrunt");
        state = kState14;
        CrossFadeTo(animBank, kSnowHitAnimations[int(fgSimulation.random->Generate()) * 5 >> 16]);
    }
    blendRates[0] = -kHeadBlendRate;
    blendRates[1] = -kArmBlendRate;
}

// FUNC_AT(0x00021df0)
void AICharacterEnemySnow::SetActor(AICharacterActorLink *actorLink) {
    this->actorLink = actorLink;
    int bones[2] = {-1, -1};
    bones[0] = actorLink->actor->GetBoneIndex("Root.Neck");
    if (bones[0] == -1)
        return;
    bones[1] = this->actorLink->actor->GetBoneIndex("Root.RShoulder");
    if (bones[1] == -1)
        return;
    this->actorLink->actor->CreateGlobalPoseOverrides(2, bones, true);
}

// FUNC_AT(0x000224f0)
int AICharacterEnemySnow::TargetIsAcquired() {
    MATRIX4 weaponTransform;
    actorLink->actor->GetWeaponPosition(&weaponTransform, true, 0);
    PBondCar *car = GetVehicle();
    Coord3 target;
    if (car->PVehicle::GetAIGroundVehiclePtr() != NULL && car->PVehicle::GetAIGroundVehiclePtr()->target != NULL &&
        car->PVehicle::GetAIGroundVehiclePtr()->target->active) {
        const Coord4 *position = car->PVehicle::GetAIGroundVehiclePtr()->target->GetPosition();
        target = *Xyz(position);
        commandTarget = *Xyz(position);
        hasCommandTarget = 1;
    } else {
        target = BodyOf(fgSimulation.cars.first[0])->position;
        hasCommandTarget = 0;
    }
    double turn = TurnsTo(&target, &weaponTransform);
    targetSide = turn > 0.0;
    return -kFacingWindow < turn && turn < kFacingWindow;
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemySSnow
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00020970)
void AICharacterEnemySSnow::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterEnemySSnowVtable);
    AICharacter::Destruct();
}

// FUNC_AT(0x00020980)
AICharacterEnemySSnow* AICharacterEnemySSnow::Construct(const MATRIX4 *transform, int weapon, PBondCar *vehicle) {
    AICharacter::Construct();
    this->vehicle = vehicle;
    vtable = reinterpret_cast<void **>(kAICharacterEnemySSnowVtable);
    this->transform = *transform;
    this->weapon = weapon;
    animBank = kBankSSnow;
    hitShape = &RiderHitShape;
    return this;
}

// FUNC_AT(0x000209d0)
AICharacterEnemySSnow* AICharacterEnemySSnow::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterEnemySSnow));
    return this;
}

// FUNC_AT(0x00020a00)
void AICharacterEnemySSnow::DoNeutral() {
    if (IsBumping(vehicle)) {
        state = kState14;
        PlayAnimation(kSSnowAnimations[weapon].bump);
    } else {
        state = kStateIdling;
        PlayAnimation(kSSnowAnimations[weapon].idle);
    }
}

// FUNC_AT(0x00020a60)
void AICharacterEnemySSnow::DoAiming() {
    if (vehicle->PVehicle::GetAIGroundVehiclePtr() == NULL)
        return;
    float aiTurn = vehicle->PVehicle::GetAIGroundVehiclePtr()->turretAngle;
    // The car's facing: z from the first look at its body, x from the second
    RigidBodyInfo *forZ = VehicleBody()->info;
    RigidBodyInfo *forX = VehicleBody()->info;
    float turn = float(AtanTurns(forX->orientation.mtx[2][0], forZ->orientation.mtx[2][2]) + -aiTurn);
    double off = turn - AtanTurns(transform.mtx[2][0], transform.mtx[2][2]);
    turn = float(off);
    if (off < -0.5)
        turn += 1.0f;
    if (turn > 0.5f)
        turn -= 1.0f;
    actorLink->actor->RotateActor(turn);
}

// FUNC_AT(0x00020b40)
void AICharacterEnemySSnow::DoDying() {
    if (scoreable) {
        glbMissionManager->IncKills(1);
        scoreable = 0;
    }
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateDead;
        int steps = fgSimulation.stepsPerSecond * kDeathFadeSeconds;
        ActActor *actor = actorLink->actor;
        actor->alpha = float(1.0 / steps);
        actor->fadeTime = float(steps);
    }
}

// FUNC_AT(0x00020bb0)
void AICharacterEnemySSnow::HandleInterrupts() {
    if (vehicle->GetHitPoints() <= 0.0f && state != kStateDying && state != kStateDead) {
        state = kStateDying;
        PlayAnimation(kSSnowDeathAnimations[weapon]);
        return;
    }
    if (crossFade == NULL && (weapon & 1))
        CallSlot(kSlotFirstState + kStateAiming);
}

// FUNC_AT(0x00020c30)
bool AICharacterEnemySSnow::TargetIsInRange() {
    return !(VU0_v3distancesquare(&BodyOf(fgSimulation.cars.first[0])->position, transform.mtx[3]) >
             targetRangeSquared);
}

// FUNC_AT(0x00020c70)
void AICharacterEnemySSnow::PlayZoneInjuryAnim(int, float amount, int) {
    float hp = hitPoints != NULL ? *hitPoints : 1.0f;
    if (state == kStateDying || state == kStateDead)
        return;
    if (amount >= hp) {
        GetACharacter()->Say("HDeath");
        state = kStateDying;
        CrossFadeTo(animBank, kSSnowDeathAnimations[weapon]);
        if ((weapon & 1) && vehicle->PVehicle::GetAIGroundVehiclePtr() != NULL)
            vehicle->PVehicle::GetAIGroundVehiclePtr()->weaponsEnabled = 0;
        int snowmobile = vehicle->attributes.LookupInt("IS_SNOWMOBILE", NULL);
        if (!(weapon & 1) && vehicle->PVehicle::GetAIGroundVehiclePtr() != NULL && snowmobile)
            vehicle->PVehicle::GetAIGroundVehiclePtr()->stopped = 1;
        return;
    }
    GetACharacter()->Say("HGrunt");
    state = kState14;
    CrossFadeTo(animBank, kSSnowHitAnimations[weapon][int(fgSimulation.random->Generate()) * 2 >> 16]);
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemySunroof
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00020ec0)
void AICharacterEnemySunroof::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterEnemySunroofVtable);
    if (targetable != NULL) {
        TargetPicker.UnregisterTarget(targetable);
        targetable->ownerType = kTargetPoint;
        targetable->RemoveReference();
    }
    AICharacter::Destruct();
}

// FUNC_AT(0x00020f40)
void AICharacterEnemySunroof::DoTilting() {
    switch (subState) {
    case kStateRunning:
        CallSlot(kSlotFirstState + kStateRunning);
        break;
    case kStateLeaning:
        CallSlot(kSlotFirstState + kStateLeaning);
        break;
    }
    lastVelocity = VehicleBody()->velocity;
}

// FUNC_AT(0x00020f90)
bool AICharacterEnemySunroof::UpdateLeanDirection() {
    bool leaning = true;
    int direction = 0;
    Coord4 acceleration;
    VU0_v4sub(&VehicleBody()->velocity, &lastVelocity, &acceleration);
    if (v3dotprod(VehicleBody()->info->orientation.mtx[2], &acceleration) > kLeanAcceleration)
        direction = 1;
    else if (v3dotprod(VehicleBody()->info->orientation.mtx[2], &acceleration) < -kLeanAcceleration)
        direction = 2;
    else if (v3dotprod(VehicleBody()->info->orientation.mtx[0], &acceleration) > kLeanAccelerationSide)
        direction = 3;
    else if (v3dotprod(VehicleBody()->info->orientation.mtx[0], &acceleration) < -kLeanAccelerationSide)
        direction = 4;
    else
        leaning = false;
    if (leanDirection == 0) {
        leanDirection = direction;
        return leaning;
    }
    return leanDirection == direction ? leaning : false;
}

// FUNC_AT(0x000210f0)
AICharacterEnemySunroof* AICharacterEnemySunroof::Construct(const MATRIX4 *transform, int weapon, int female,
                                                            PBondCar *vehicle) {
    AICharacter::Construct();
    vtable = reinterpret_cast<void **>(kAICharacterEnemySunroofVtable);
    this->vehicle = vehicle;
    lastVelocity = BodyOf(vehicle)->velocity;
    leanDirection = 0;
    partnerA = NULL;
    partnerB = NULL;
    if (weapon < kEnemyWeapon1)
        weapon = kEnemyWeapon1;
    else if (weapon > kEnemyWeapon2)
        weapon = kEnemyWeapon2;
    this->transform = *transform;
    this->weapon = weapon;
    this->female = female;
    void *block = UMemory::FastAlloc(sizeof(WTargetable), "WTargetable");
    targetable = block != NULL ? static_cast<WTargetable *>(block)->Construct(this) : NULL;
    if (weapon >= kEnemyWeapon2)
        clipSize = 0;
    hitShape = &RiderHitShape;
    if (weapon == kEnemyWeapon1) {
        rounds = 25;
        clipSize = 25;
        animBank = kBankSunroof1;
    } else {
        animBank = weapon == kEnemyWeapon2 ? kBankSunroof2 : kBankSunroof1;
    }
    return this;
}

// FUNC_AT(0x00021230)
AICharacterEnemySunroof* AICharacterEnemySunroof::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterEnemySunroof));
    return this;
}

// FUNC_AT(0x00021260)
void AICharacterEnemySunroof::DoInitial() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    state = kStateNeutral;
    actorLink->actor->visible = false;
    human->Deactivate();
    for (Human **other = fgSimulation.humans.first; other != fgSimulation.humans.last; other++) {
        AICharacter *character = (*other)->character;
        if (character == NULL || character == this || character->GetVehicle() != vehicle)
            continue;
        if (partnerA == NULL)
            partnerA = character;
        else if (partnerB == NULL)
            partnerB = character;
    }
}

// FUNC_AT(0x00021300)
void AICharacterEnemySunroof::DoNeutral() {
    if (vehicle->PVehicle::GetResetAvailable() != 0 || !AICharacterEnemy::TargetIsInRange())
        return;
    GetVehicle()->PVehicle::DisableTargetBeacon();
    if (targetable != NULL)
        TargetPicker.RegisterTarget(targetable);
    state = kStateArming;
    actorLink->actor->visible = true;
    human->Activate();
    vehicle->renderObject->animHandle->ProcessStimuli(kCarAnimSystem, kStimulusUp, fgSimulation.stepCount,
                                                      kStimulusMode);
    PlayAnimation(kSunroofArmAnimations[weapon]);
}

// FUNC_AT(0x000213c0)
void AICharacterEnemySunroof::DoArmed() {
    if (vehicle->PVehicle::GetResetAvailable() != 0 || !AICharacterEnemy::TargetIsInRange()) {
        GetVehicle()->PVehicle::EnableTargetBeacon(false);
        if (targetable != NULL)
            TargetPicker.UnregisterTarget(targetable);
        state = kStateUnarming;
        vehicle->renderObject->animHandle->ProcessStimuli(kCarAnimSystem, kStimulusDown, fgSimulation.stepCount,
                                                          kStimulusMode);
        PlayAnimation(kSunroofUnarmAnimations[weapon]);
        return;
    }
    if (UpdateLeanDirection()) {
        state = kStateTilting;
        subState = kStateLeaning;
        PlayAnimation(kSunroofLeanAnimations[weapon][leanDirection - 1]);
        return;
    }
    if (NeedsToReload()) {
        rounds = clipSize;
        state = kStateReloading;
        PlayAnimation(kSunroofReloadAnimations[weapon]);
        return;
    }
    CallSlot(kSlotFirstState + kStateAiming);
    if (firing && AICharacterEnemy::TargetIsAcquired() && fgSimulation.stepCount >= nextShotStep) {
        state = kStateFiring;
        nextShotStep = shotInterval + fgSimulation.stepCount;
        rounds--;
        PlayAnimation(kSunroofFireAnimations[weapon][int(fgSimulation.random->Generate()) * 6 >> 16]);
    }
}

// FUNC_AT(0x000215b0)
void AICharacterEnemySunroof::DoAiming() {
    if (AICharacterEnemy::TargetIsAcquired())
        return;
    float turn = 0.0f;
    switch (targetSide) {
    case 0:
        turn = -kSunroofTurn;
        break;
    case 1:
        turn = kSunroofTurn;
        break;
    }
    actorLink->actor->RotateActor(turn);
}

// FUNC_AT(0x00021630)
void AICharacterEnemySunroof::DoUnarming() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    state = kStateNeutral;
    actorLink->actor->visible = false;
    human->Deactivate();
}

// FUNC_AT(0x00021670)
void AICharacterEnemySunroof::DoDying() {
    if (targetable != NULL)
        TargetPicker.UnregisterTarget(targetable);
    GetVehicle()->PVehicle::EnableTargetBeacon(false);
    if (scoreable) {
        glbMissionManager->IncKills(1);
        scoreable = 0;
    }
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateDead;
        actorLink->actor->visible = false;
        human->Deactivate();
        vehicle->renderObject->animHandle->ProcessStimuli(kCarAnimSystem, kStimulusDown, fgSimulation.stepCount,
                                                          kStimulusMode);
    }
}

// FUNC_AT(0x00021710)
void AICharacterEnemySunroof::DoRunning() {
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateArmed;
        subState = 0;
    }
}

// FUNC_AT(0x00021740)
void AICharacterEnemySunroof::DoLeaning() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    if (UpdateLeanDirection()) {
        PlayAnimation(kSunroofLeanHoldAnimations[weapon][leanDirection - 1]);
        return;
    }
    subState = kStateRunning;
    PlayAnimation(kSunroofLeanEndAnimations[weapon][leanDirection - 1]);
    leanDirection = 0;
}

// FUNC_AT(0x000217e0)
void AICharacterEnemySunroof::HandleInterrupts() {
    if (!actorLink->actor->visible) {
        // Down: dies with the car
        if (vehicle->GetHitPoints() <= 0.0f && state != kStateDead && state != kStateDying &&
            !(state == kStateIdling && subState == kStateDead))
            state = kStateDying;
        return;
    }
    float hp = hitPoints != NULL ? *hitPoints : 1.0f;
    newDamage = float(initialHitPoints - double(hp) + newDamage);
    initialHitPoints = hitPoints != NULL ? *hitPoints : 1.0f;
    int missionState = glbMissionManager->missionState;
    if (missionState != kMissionWon && missionState != kMissionLost && !(vehicle->GetHitPoints() <= 0.0f)) {
        newDamage = 0.0f;
        return;
    }
    if (state == kStateDying || state == kStateDead)
        return;
    state = kStateDying;
    PlayAnimation(kSunroofUnarmAnimations[weapon]);
}

// FUNC_AT(0x000218f0)
bool AICharacterEnemySunroof::TargetIsInRange() {
    Coord3 target = GunnerTarget(GetVehicle());
    if (VU0_v3distancesquare(&target, transform.mtx[3]) > targetRangeSquared)
        return false;
    // Not while a partner is up
    if (partnerA != NULL && partnerA->actorLink->actor->visible)
        return false;
    if (partnerB != NULL && partnerB->actorLink->actor->visible)
        return false;
    return true;
}

// FUNC_AT(0x000219e0)
int AICharacterEnemySunroof::TargetIsAcquired() {
    MATRIX4 weaponTransform;
    actorLink->actor->GetWeaponPosition(&weaponTransform, true, 0);
    PBondCar *car = GetVehicle();
    Coord3 target;
    if (car->PVehicle::GetAIGroundVehiclePtr() != NULL && car->PVehicle::GetAIGroundVehiclePtr()->target != NULL &&
        car->PVehicle::GetAIGroundVehiclePtr()->target->active) {
        const Coord4 *position = car->PVehicle::GetAIGroundVehiclePtr()->target->GetPosition();
        target = *Xyz(position);
        commandTarget = *Xyz(position);
        hasCommandTarget = 1;
    } else {
        target = BodyOf(fgSimulation.cars.first[0])->position;
        hasCommandTarget = 0;
    }
    double turn = TurnsTo(&target, weapon == kEnemyWeapon3 ? &transform : &weaponTransform);
    targetSide = turn > 0.0;
    return -kFacingWindow < turn && turn < kFacingWindow;
}

// FUNC_AT(0x00021b70)
void AICharacterEnemySunroof::PlayZoneInjuryAnim(int, float amount, int) {
    float hp = hitPoints != NULL ? *hitPoints : 1.0f;
    bool dies = amount >= hp || vehicle->PVehicle::EmpActive();
    if (state == kStateDying || state == kStateDead)
        return;
    if (dies) {
        GetACharacter()->Say("HDeath");
        state = kStateDying;
        const int32_t *list = kSunroofDeathAnimations[weapon][zoneLastHit];
        int count = CountAnimations(list);
        animBank = kBankSunroofDeath;
        CrossFadeTo(kBankSunroofDeath, list[int(fgSimulation.random->Generate()) * count >> 16]);
        return;
    }
    GetACharacter()->Say("HGrunt");
    state = kState14;
    const int32_t *list = kSunroofHitAnimations[weapon][zoneLastHit];
    int count = CountAnimations(list);
    CrossFadeTo(animBank, list[int(fgSimulation.random->Generate()) * count >> 16]);
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyWindow
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00021600)
void AICharacterEnemyWindow::DoFiring() {
    CallSlot(kSlotFirstState + kStateAiming);
    if (actorLink->actor->IsAnimationDone())
        state = kStateArmed;
}

// FUNC_AT(0x00021d40)
void AICharacterEnemyWindow::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterEnemyWindowVtable);
    AICharacter::Destruct();
}

// FUNC_AT(0x00021d50)
AICharacterEnemyWindow* AICharacterEnemyWindow::Construct(const MATRIX4 *transform, PBondCar *vehicle) {
    AICharacter::Construct();
    this->vehicle = vehicle;
    aimYaw = 0.0f;
    partnerA = NULL;
    partnerB = NULL;
    aimSide = 0;
    armTimer = 0;
    vtable = reinterpret_cast<void **>(kAICharacterEnemyWindowVtable);
    this->transform = *transform;
    animBank = kBankWindow;
    hitShape = &RiderHitShape;
    return this;
}

// FUNC_AT(0x00021dc0)
AICharacterEnemyWindow* AICharacterEnemyWindow::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterEnemyWindow));
    return this;
}

// FUNC_AT(0x00021e70)
void AICharacterEnemyWindow::DoInitial() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    state = kStateNeutral;
    actorLink->actor->visible = false;
    human->Deactivate();
    for (Human **other = fgSimulation.humans.first; other != fgSimulation.humans.last; other++) {
        AICharacter *character = (*other)->character;
        if (character == NULL || character == this || character->GetVehicle() != vehicle)
            continue;
        if (partnerA == NULL)
            partnerA = character;
        else if (partnerB == NULL)
            partnerB = character;
    }
}

// FUNC_AT(0x00021f10)
void AICharacterEnemyWindow::DoNeutral() {
    if (vehicle->PVehicle::GetResetAvailable() != 0 || !AICharacterEnemy::TargetIsInRange())
        return;
    state = kStateIdling;
    subState = kStateArming;
    actorLink->actor->visible = true;
    human->Activate();
    armTimer = fgSimulation.stepsPerSecond * kWindowArmSeconds;
}

// FUNC_AT(0x00021f70)
void AICharacterEnemyWindow::DoIdling() {
    if (subState == kStateArming) {
        actorLink->actor->SetTimeScale(2.0f);
        animation = kWindowArmAnimation;
        actorLink->actor->SetNewAnimation(animBank, animation);
        crossFade = NULL;
    } else if (subState == kStateNeutral || subState == kStateDead) {
        actorLink->actor->visible = false;
        human->Deactivate();
    }
    state = subState;
    subState = 0;
}

// FUNC_AT(0x00022010)
void AICharacterEnemyWindow::DoArming() {
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateArmed;
        actorLink->actor->SetTimeScale(1.0f);
    }
}

// FUNC_AT(0x00022050)
void AICharacterEnemyWindow::DoArmed() {
    if (vehicle->PVehicle::GetResetAvailable() != 0 || (armTimer <= 0 && !AICharacterEnemy::TargetIsInRange())) {
        state = kStateUnarming;
        actorLink->actor->SetTimeScale(2.0f);
        PlayAnimation(kWindowUnarmAnimation);
        return;
    }
    CallSlot(kSlotFirstState + kStateAiming);
    if (firing && fgSimulation.stepCount >= nextShotStep) {
        state = kStateFiring;
        nextShotStep = shotInterval + fgSimulation.stepCount;
        rounds--;
        PlayAnimation(kWindowFireAnimations[int(fgSimulation.random->Generate()) * 5 >> 16]);
    }
}

// FUNC_AT(0x00022150)
void AICharacterEnemyWindow::DoAiming() {
    if (AICharacterEnemy::TargetIsAcquired())
        return;
    float step = 0.0f;
    switch (aimSide ^ targetSide) {
    case 0:
        step = 2.0f;
        break;
    case 1:
        step = -2.0f;
        break;
    }
    // The shoulder turns two degrees a step; the neck turns with it, 55 behind, within -80 to 30
    double yaw = step + aimYaw;
    aimYaw = float(yaw);
    double neckYaw = yaw - 55.0f;
    float neck = float(neckYaw);
    if (neckYaw < -80.0)
        neck = -80.0f;
    else if (neck > 30.0f)
        neck = 30.0f;
    if (yaw < -60.0)
        aimYaw = -60.0f;
    if (aimYaw > 80.0f)
        aimYaw = 80.0f;

    Transform poses[2];
    BuildRotate(&poses[0], neck, 0.0f, 1.0f, 0.0f);
    BuildRotate(&poses[1], aimYaw, 0.0f, 1.0f, 0.0f);
    float weights[2] = {1.0f, 1.0f};
    actorLink->actor->SetGlobalPoseOverrides(2, poses, weights);
}

// FUNC_AT(0x00022280)
void AICharacterEnemyWindow::DoUnarming() {
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateIdling;
        subState = kStateNeutral;
        actorLink->actor->SetTimeScale(1.0f);
    }
}

// FUNC_AT(0x000222c0)
void AICharacterEnemyWindow::DoDying() {
    if (scoreable) {
        glbMissionManager->IncKills(1);
        scoreable = 0;
    }
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateIdling;
        subState = kStateDead;
    }
}

// FUNC_AT(0x00022310)
void AICharacterEnemyWindow::HandleInterrupts() {
    if (vehicle->GetHitPoints() <= 0.0f && state != kStateDying && state != kStateDead && state != kStateIdling) {
        state = kStateDying;
        if (actorLink->actor->visible)
            PlayAnimation(kWindowDeathAnimations[0]);
        return;
    }
    if (armTimer > 0)
        armTimer--;
}

// FUNC_AT(0x000223a0)
bool AICharacterEnemyWindow::TargetIsInRange() {
    MATRIX4 ride;
    AICharacterEnemyRider::GetRotPos(this, &ride);
    Coord3 target = GunnerTarget(GetVehicle());
    if (VU0_v3distancesquare(&target, ride.mtx[3]) > targetRangeSquared)
        return false;
    double turn = TurnsTo(&target, &ride);
    return -kWindowRangeLeft < turn && turn < kWindowRangeRight;
}

// FUNC_AT(0x00022680)
void AICharacterEnemyWindow::PlayZoneInjuryAnim(int, float amount, int) {
    float hp = hitPoints != NULL ? *hitPoints : 1.0f;
    if (state == kStateDying || state == kStateDead)
        return;
    if (amount >= hp) {
        GetACharacter()->Say("HDeath");
        state = kStateDying;
        CrossFadeTo(animBank, kWindowDeathAnimations[int(fgSimulation.random->Generate()) * 2 >> 16]);
        return;
    }
    GetACharacter()->Say("HGrunt");
    state = kState14;
    CrossFadeTo(animBank, kWindowHitAnimations[int(fgSimulation.random->Generate()) * 2 >> 16]);
}
