#pragma fp_contract(off)

#include "Missile.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bit>

#include "Explosion.h"                  // ExplosionType, SimpleBodyHits
#include "Shell.h"                    // Shell::ApplyWorldDamage
#include "SoftZone.h"                   // PBondCar_PosInSoftZone
#include "Vehicle.h"                    // PVehicle
#include "WeaponManager.h"              // DrivingWeapon
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../anim/AnimEngine.h"         // Handle
#include "../anim/Manager.h"            // MakeCoord4
#include "../audio/Bank.h"              // fgBanks
#include "../audio/Sound.h"             // ABaseSound, AOneShotSound, AMenuSound
#include "../audio/ZoomObj.h"           // ASound
#include "../camera/CameraSpline.h"     // RCameraMath
#include "../engine/CoreFoundation.h"   // NullFunction, GameEmptyString
#include "../engine/MissionManager.h"
#include "../engine/OBB.h"
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"       // RigidBodyInfo, SimpleBodyOwners, SimStepsPerSecond, DamageSourceSig
#include "../physics/SimpleRigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/Lights.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"
#include "../world/Targeting.h"
#include "../world/WorldPos.h"

// ---------------------------------------------------------------------------------------------------------------
// Missile (0x0005e8c0-0x00060b60), ported from the listing. The x87 chains are computed in double in the
// original's order and rounded where it stores a float; comparisons fail on NaN where the original's jumps do.
// The constructor's switch on the weapon is a jump table through a byte index (0x0005f2fc, 0x0005f324), decoded
// from its bytes.
//
// Random numbers: the constructor of a Missile fired by a sentry draws two from the Simulation's SimRandom
// (FUN_00040070's 32-bit word).
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's, not ported yet

// A random 32-bit word: the first draw inverted in the top half, the second below
#define SimRandom_Word ((uint32_t (__fastcall *)(SimRandom *, int))0x00040070)
// AOneShotSound's constructor: a patch of a bank at a place
#define AOneShotSound_Construct ((AOneShotSound *(__fastcall *)(void *, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity, float volume, const char *mix))0x000476f0)
// game.weapons package C

// ---- globals

#define Sim ((void *)0x00233ff0)                            // the Simulation
#define SimulationRandom (*(SimRandom **)0x00233ff0)        // the Simulation's first word
#define SimState I32_AT(0x00234e24)                         // Sim.simState
#define SimTimeStep FLOAT_AT(0x00234e30)                    // the simulation's step, in seconds
#define SimStepCount I32_AT(0x00234e34)                     // the Simulation's steps so far
#define PlayerPhysicsObject (*(PhysicsObject **)PTR_AT(0x00234e40))     // the first player's object
#define IdentityMatrix (*(const MATRIX4 **)0x001c4654)
#define TuningLevel ((const char *)0x00244084)              // the level the tuning databases are read for

// The tuning (Missile.h)
// XBE_GLOBAL(0x001c3680, 0x4)
float MissileSteerScale = 0.004f;
// XBE_GLOBAL(0x001c3684, 0x4)
float MissileSteerRate = 0.005f;
// XBE_GLOBAL(0x001c3688, 0x4)
float RemoteRocketSpeed = 34.5f;
// XBE_GLOBAL(0x001c368c, 0x4)
float RemoteRocketThrust = 7.2f;
// XBE_GLOBAL(0x001c3690, 0x4)
float RemoteRocketLaunchSpeed = 3.0f;
// XBE_GLOBAL(0x001c3694, 0x4)
float RemoteRocketTurn = 2.0f;
// XBE_GLOBAL(0x001c3698, 0x4)
int32_t RemoteRocketLife = 6;
// XBE_GLOBAL(0x001c369c, 0x4)
float MissileGroundClearance = 0.4f;
// XBE_GLOBAL(0x001c36a0, 0x4)
float MissileGroundFollowHeight = 5.0f;
// XBE_GLOBAL(0x001c36a8, 0x4)
float MissileSoundPitch = 1.0f;
// XBE_GLOBAL(0x001c36ac, 0x1)
bool MissileWobbleFlip = true;

namespace {

void **const kMissileVtable = (void **)0x0018eef8;
RSceneObj_vtbl *const kRAutonomousObjVtable = (RSceneObj_vtbl *)0x0018a398;
constexpr int kBodies = 160;                        // the rigid bodies (64), then the simple ones (96)
constexpr int kWeaponBank = 4;                      // fgBanks' slot of the weapon sounds
constexpr int kDamageBullet = 2;                    // ApplyDamage's kind for a hit on a vehicle
constexpr int kSimPaused = 3;                       // Sim.simState
constexpr int kProgrammerEvent21 = 0x15;
constexpr uint8_t kFaceType14 = 0xe;

constexpr uint16_t kMissileBodyFlags = SimpleRigidBody::kFlag01 | SimpleRigidBody::kMoves |
                                       SimpleRigidBody::kBoxRigidHits | SimpleRigidBody::kBoxSimpleHits |
                                       SimpleRigidBody::kTouchesTriggers;

const Coord3 kZero = { 0.0f, 0.0f, 0.0f };

// The constructor
constexpr float kRadius = 0.3f;
constexpr float kFODRadius = 0.6f;
constexpr float kMass = 4.0f;
constexpr float kHomingDelayShort = 0.1f;           // seconds
constexpr float kHomingDelayMissile = 0.2f;
constexpr float kHomingDelayQPulse = 0.33f;
constexpr float kHomingDelayLong = 0.5f;
constexpr float kAlphaSubNearSquared = 10000.0f;
constexpr Coord4 kUp = { 0.0f, 1.0f, 0.0f, 0.0f };
constexpr float kSteepUp = 0.95f;                   // a sentry's shot closer than this to straight up is turned
constexpr float kWobbleY = 0.5f;
constexpr float kWobbleX = 0.2f;
constexpr float kWideWobbleX = 1.0f;
constexpr float kPitchStep = 0.01f;
constexpr float kPitchWrap = 0.04f;
constexpr float kSoundMinDistance = 15.0f;
constexpr float kTorpedoMaxDistance = 300.0f;

// Render
constexpr float kQPulseLightW = 4.0f;
constexpr Coord4 kQPulseLightColour = { 0.3f, 0.3f, 1.0f, 0.0f };

// SetUserSteer
constexpr float kSteerDeadZone = 0.2f;

// Simulate
constexpr float kFadeInStep = 1.0f / 15.0f;
static_assert(std::bit_cast<uint32_t>(kFadeInStep) == 0x3d888889, "the fade's step");

// CheckForCollision
constexpr float kSoftZoneDepth = 10.0f;             // a remote torpedo detonates over a soft zone this far below
constexpr float kLevelRatio = 4.0f;                 // level flight: the speed over this times the vertical speed
constexpr float kClearanceBelow = -2.0f;
constexpr float kTorpedoReach = 1.5f;
constexpr float kScorchPerDamage = 0.008f;
constexpr int kScorchType = 5;
constexpr float kHitReach = 10.0f;                  // the damage's segment, either side of the hit
constexpr float kHitDamage = 32.0f;
constexpr Coord4 kHalfExtents = { 0.1f, 0.1f, 0.5f, 0.0f };
constexpr float kEmpMinDistance = 50.0f;
constexpr float kEmpMaxDistance = 500.0f;

// SteerMissile
constexpr float kUnlockedSpeed = 0.3f;
constexpr float kMaxPitch = 0.99f;                  // the nose's height (the orientation's z row's y) steered to
constexpr float kSteerBlend = 0.65f;
constexpr float kLevelBlend = 0.02f;
constexpr float kLeadTime = 3.0f;                   // the target's velocity over this many steps is led
constexpr float kLockSoundRate = 0.0001f;
constexpr float kMinTurnDistanceSquared = 4.0f;
constexpr float kFacingMin = 0.1f;
constexpr float kProximitySquared = 0.25f;
constexpr float kTorpedoFacingMin = 0.5f;
constexpr float kPointReachedSquared = 2.0f;
constexpr float kLockedFacing = 0.99f;
constexpr float kAlignBlend = 0.1f;
constexpr float kWobbleMin = 0.01f;

bool IsDriveMissile(const Missile *missile) {
    return glbMissionManager->driveMissile == missile;
}

void EndDrive() {
    SMissionManager_SetDriveMissile(glbMissionManager, 0, NULL);
}

// The Q-Pulse's detonation, heard at the body
void PlayEmpDetonate(const Coord3 *position, const Coord3 *velocity) {
    ABank *bank = fgBanks[kWeaponBank];
    void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    AOneShotSound *sound = NULL;
    if (block != NULL)
        sound = AOneShotSound_Construct(block, 0, bank->handle, bank->index.Lookup("SFX_EMPdetonate"), position,
                                        velocity, 1.0f, "POV Weapons");
    sound->minDistance = kEmpMinDistance;
    sound->maxDistance = kEmpMaxDistance;
    sound->maxDistanceSq = kEmpMaxDistance * kEmpMaxDistance;
    sound->fxLevel = 1.0f;
}

// One step of the drive missile's turn or pitch towards the user's, by the steer rate
float EaseTowards(float value, float wanted) {
    double up = double(MissileSteerRate) + value;
    if (up < wanted)
        return float(up);
    double down = double(value) - MissileSteerRate;
    return down > wanted ? float(down) : wanted;
}

bool HitsBody(const BitSet192 *bodies, int index) {
    return (bodies->words[index / 32] >> (index % 32) & 1) != 0;
}

}  // namespace

// FUNC_AT(0x0005e8c0)
Missile* Missile::Construct(int weapon, float speed, const Coord3 *direction, const Coord3 *position,
                            uint32_t ownerSig, float damage, WTargetable *target, int variant) {
    PhysicsObject::Construct("weapon", "Missile", kObjectMissile, ownerSig, false, false);
    vtable = kMissileVtable;
    this->target = target;
    unknown7C = 0.0f;
    lingerSteps = SimStepsPerSecond * 2;
    launchStep = SimStepCount;
    // RMissileStreak's constructor (folded with EAGL's Property's, 0x000ed310)
    streak.count = 0;
    streak.tick = 0;
    streak.type = 0;
    this->weapon = weapon;
    detonate = 0;
    exploded = 0;
    fadeIn = 0.0f;
    this->damage = damage;
    lockSoundStep = SimStepCount + 5;
    steerX = 0.0f;
    steerY = 0.0f;
    userSteerX = 0.0f;
    userSteerY = 0.0f;
    steerScale = MissileSteerScale;
    armStep = SimStepCount;

    float radius = kRadius;
    alignas(16) MATRIX4 generated;
    alignas(16) MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);

    // An owner that is not the player's, and the owner if it is a vehicle
    bool notPlayers = true;
    PhysicsObject *vehicleOwner = NULL;
    PhysicsObject *owner = Simulation_FindPhysicsObjectSignature(Sim, 0, ownerSig);
    if (owner != NULL) {
        notPlayers = owner != Simulation_GetPlayerObject(Sim, 0);
        if (owner->type == kObjectVehicle)
            vehicleOwner = owner;
    }
    streak.type = weapon == kWeaponSnowmobileRocket ? 1 : 0;

    float launchSpeed;
    const char *transitSound;
    switch (weapon) {
    case kWeaponQPulse:
        if (this->target == NULL)
            this->target = TargetPicker.GetWorldTarget();
        maxSpeed = 175.0f;
        thrust = 17.5f;
        turnRate = 1.0f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayQPulse);
        launchSpeed = speed + 4.0f;
        homingFull = 0;
        lifeSteps = SimStepsPerSecond * 5;
        lingerSteps = SimStepsPerSecond;
        transitSound = "SFX_EMPtrans";
        break;
    case kWeaponRemoteTorpedo:
        this->target = NULL;
        maxSpeed = 40.0f;
        thrust = 4.0f;
        turnRate = 1.0f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayQPulse);
        homingFull = 0;
        lifeSteps = SimStepsPerSecond * 10;
        launchSpeed = 8.0f;
        transitSound = "SFX_vgtorpTrans";
        streak.type = 2;
        break;
    case kWeaponTorpedo:
        if (this->target == NULL)
            this->target = TargetPicker.GetWorldTarget();
        if (notPlayers && vehicleOwner != NULL) {
            if (_stricmp(static_cast<PVehicle *>(vehicleOwner)->GetCarType(), "alpha_sub") == 0) {
                if (this->target != NULL && VU0_v3distancesquare(position, this->target) < kAlphaSubNearSquared) {
                    this->damage = 2000.0f;
                    maxSpeed = 60.0f;
                    thrust = 30.0f;
                    turnRate = 1.0f;
                    unknown7C = -1.0f;
                } else {
                    this->damage = 750.0f;
                    maxSpeed = 40.0f;
                    thrust = 12.0f;
                    turnRate = 0.01f;
                    unknown7C = 0.8f;
                }
                lifeSteps = SimStepsPerSecond * 8;
            } else {
                maxSpeed = 20.0f;
                thrust = 10.0f;
                turnRate = 0.03f;
                unknown7C = 0.8f;
                lifeSteps = SimStepsPerSecond * 5;
            }
        } else {
            maxSpeed = 50.0f;
            thrust = 25.0f;
            turnRate = 0.1f;
            unknown7C = -1.0f;
            lifeSteps = SimStepsPerSecond * 5;
        }
        launchSpeed = speed + 1.0f;
        homingDelay = 0;
        homingFull = 1;
        transitSound = notPlayers ? "SFX_htorpedotrans" : "SFX_vtorpedoTrans";
        streak.type = 2;
        break;
    case kWeaponMissile:
        if (this->target == NULL)
            this->target = TargetPicker.GetWorldTarget();
        maxSpeed = 250.0f;
        thrust = 50.0f;
        turnRate = 1.0f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayMissile);
        launchSpeed = speed + 7.0f;
        homingFull = 0;
        lifeSteps = SimStepsPerSecond * 4;
        transitSound = "SFX_MissleTrans";
        // An owner on a simple body (the signature's top bit; its slot in bits 24-30)
        if (int32_t(ownerSig) < 0) {
            PhysicsObject *shooter = SimpleBodyOwners[ownerSig >> 24 & 0x7f];
            if (shooter->type == kObjectHelicopter) {
                thrust = 100.0f;
                armStep = Ftol(double(SimStepsPerSecond) * kHomingDelayLong + armStep);
                homingFull = 1;
            } else if (shooter->type == kObjectSentry) {
                // A random homing delay, thrust, turn and (fired nearly straight up) heading
                alignas(16) Coord4 spread;
                BytesToCoordXYZW(SimRandom_Word(SimulationRandom, 0), NULL, NULL, NULL, &spread);
                homingDelay = Ftol(double(homingDelay) * spread.w * 2.0);
                thrust = float((double(spread.x) + 1.25) * thrust);
                turnRate = float((double(spread.y) + 0.25) * turnRate);
                if (v3dotprod(&kUp, direction) > kSteepUp) {
                    alignas(16) MATRIX4 turn;
                    VU0_MATRIX4setyrot(&turn, spread.z);
                    VU0_MATRIX4_mult(&orientation, &orientation, &turn);
                }
            }
        }
        break;
    case kWeaponSnowmobileRocket:
        this->target = NULL;
        launchSpeed = speed + 5.0f;
        maxSpeed = 250.0f;
        thrust = 125.0f;
        turnRate = 1.0f;
        homingDelay = 0;
        homingFull = 1;
        lifeSteps = SimStepsPerSecond * 5;
        transitSound = GameEmptyString;
        break;
    case kWeaponUltralightRocket:
        if (this->target == NULL)
            this->target = TargetPicker.GetWorldTarget();
        maxSpeed = 800.0f;
        thrust = 80.0f;
        turnRate = 1.0f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayLong);
        launchSpeed = 10.0f;
        transitSound = "SFX_UMissileTrans";
        homingFull = 1;
        lifeSteps = SimStepsPerSecond * 5;
        break;
    case kWeaponCh6RemoteRocket:
        this->target = NULL;
        maxSpeed = RemoteRocketSpeed;
        thrust = RemoteRocketThrust;
        turnRate = RemoteRocketTurn * 0.05f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayShort);
        homingFull = 1;
        lifeSteps = SimStepsPerSecond * RemoteRocketLife;
        armStep += 6;
        launchSpeed = RemoteRocketLaunchSpeed + speed;
        transitSound = "SFX_MissleTrans";
        break;
    case kWeaponRocket:
        this->target = NULL;
        maxSpeed = 250.0f;
        thrust = 125.0f;
        turnRate = 1.0f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayMissile);
        launchSpeed = speed + 5.0f;
        homingFull = 1;
        lifeSteps = SimStepsPerSecond * 5;
        if (_stricmp(TuningLevel, "snow1a_mis3") == 0 || _stricmp(TuningLevel, "snow1a") == 0)
            transitSound = GameEmptyString;
        else
            transitSound = "SFX_RocketTrans";
        armStep += 10;
        break;
    case kWeaponFodRocket:
        this->target = NULL;
        thrust = 0.0f;
        maxSpeed = 300.0f;
        turnRate = 1.0f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayShort);
        homingFull = 1;
        lifeSteps = SimStepsPerSecond * 5;
        launchSpeed = 300.0f;
        transitSound = "SFX_RocketTrans";
        radius = kFODRadius;
        break;
    default:
        this->target = NULL;
        thrust = 0.0f;
        maxSpeed = 200.0f;
        turnRate = 1.0f;
        homingDelay = Ftol(double(SimStepsPerSecond) * kHomingDelayShort);
        launchSpeed = 200.0f;
        transitSound = "SFX_RocketTrans";
        homingFull = 1;
        lifeSteps = SimStepsPerSecond * 5;
        break;
    }

    // The player's Missiles corkscrew, each the other way from the last
    alignas(16) Coord4 velocity = { direction->x, direction->y, direction->z, 0.0f };
    if (weapon == kWeaponMissile && !notPlayers) {
        wobbleY = kWobbleY;
        wobbleX = variant == 7 || variant == 8 ? kWideWobbleX : kWobbleX;
        MissileWobbleFlip = !MissileWobbleFlip;
        if (MissileWobbleFlip) {
            wobbleY = -kWobbleY;
            wobbleX = -wobbleX;
        }
    } else {
        wobbleY = 0.0f;
        wobbleX = 0.0f;
    }
    wobblePhase = 0.0f;

    VU0_v4unitxyz(&velocity, &velocity);
    v3scale(1, &velocity, launchSpeed, &velocity);
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleMissile, position, reinterpret_cast<Coord3 *>(&velocity), &kZero,
                        &orientation, radius, kMass);
    Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags |= kMissileBodyFlags;

    RAutonomousObj *render = static_cast<RAutonomousObj *>(UMemory::FastAlloc(sizeof(RAutonomousObj), "RAutonomousObj"));
    if (render != NULL) {
        memcpy(&render->instance, IdentityMatrix, sizeof(MATRIX4));
        render->Construct(&render->instance);
        render->vtable = kRAutonomousObjVtable;
    }
    SetRenderObject(render);

    ABank *bank = fgBanks[kWeaponBank];
    ASound *transit = static_cast<ASound *>(ABaseSound::OperatorNew(sizeof(ASound), "ASound"));
    if (transit != NULL) {
        const char *mix = weapon == kWeaponQPulse ? "POV Weapons" : "Weapons Transit";
        transit = transit->Construct(bank->handle, bank->index.Lookup(transitSound), mix);
    }
    sound = transit;
    // The pitch steps up from 1 and wraps at 0.04: it is always 1
    double pitch = double(MissileSoundPitch) + kPitchStep;
    MissileSoundPitch = float(pitch);
    if (pitch >= kPitchWrap)
        MissileSoundPitch = 1.0f;
    transit->pitch = MissileSoundPitch;
    sound->minDistance = kSoundMinDistance;
    if (this->weapon == kWeaponTorpedo) {
        sound->maxDistance = kTorpedoMaxDistance;
        sound->maxDistanceSq = kTorpedoMaxDistance * kTorpedoMaxDistance;
    }
    if (this->target != NULL && !this->target->isTemporary)
        this->target->AddReference();
    return this;
}

// FUNC_AT(0x0005f350)
void Missile::Destruct() {
    vtable = kMissileVtable;
    if (sound != NULL)
        sound->CallDelete(1);
    if (target != NULL)
        target->RemoveReference();
    NullFunction();     // ~RMissileStreak
    PhysicsObject::Destruct();
}

// FUNC_AT(0x0005f6a0)
Missile* Missile::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(Missile));
    return this;
}

// The position is the render object's first effect locator's, else the body's. The light grows over the first
// quarter second (the square of twice the steps left of it), then with the steps since launch.
// FUNC_AT(0x0005f3d0)
void Missile::Render(bool firstView) {
    alignas(16) Coord4 position;
    if (renderObject != NULL && renderObject->animHandle->effectCount > 0) {
        alignas(16) MATRIX4 locator;
        renderObject->LocateFX(renderObject->animHandle->effects, &locator, true);
        position = { locator.mtx[3][0], locator.mtx[3][1], locator.mtx[3][2], locator.mtx[3][3] };
    } else {
        SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
        position = { body->position.x, body->position.y, body->position.z, 1.0f };
    }

    if (weapon == kWeaponQPulse) {
        if (SimStepCount - launchStep > homingDelay) {
            alignas(16) Coord4 lightPosition = { position.x, position.y, position.z, kQPulseLightW };
            alignas(16) Coord4 colour = kQPulseLightColour;
            if (exploded) {
                lightPosition.w = 1.0f;
                VU0_v4scale(&colour, float(double(lingerSteps) * SimTimeStep), &colour);
            }
            fgLightManager->AddPositionalLight(&lightPosition, &colour);
        }
        return;
    }

    if (firstView) {
        int lightType = weapon == kWeaponRocket ? kMissileLightRocket
                        : weapon == kWeaponSnowmobileRocket ? kMissileLightSnowmobileRocket : kMissileLightMissile;
        if (!exploded && weapon != kWeaponTorpedo) {
            uint32_t age = SimStepCount - launchStep;
            uint32_t quarterSecond = uint32_t(SimStepsPerSecond) >> 2;
            double scale;
            if (age < quarterSecond) {
                double fresh = double(quarterSecond - age) * 2.0;
                scale = fresh * fresh;
            } else {
                scale = double(age);
            }
            fgLightManager->highLevel.AddMissile(&position, lightType, float(SimTimeStep * scale));
        }
        if (SimState != kSimPaused) {
            if (exploded)
                streak.Update();
            else
                streak.Update(&position);
        }
    }
    if (weapon != kWeaponMissile && weapon != kWeaponFodRocket)
        streak.Draw();
}

// FUNC_AT(0x0005f600)
void Missile::SetUserSteerX(float x) {
    if (fabsf(x) > kSteerDeadZone)
        userSteerX = x * steerScale;
    else
        userSteerX = 0.0f;
}

// FUNC_AT(0x0005f640)
void Missile::SetUserSteerY(float y) {
    if (fabsf(y) > kSteerDeadZone)
        userSteerY = y * steerScale;
    else
        userSteerY = 0.0f;
}

// FUNC_AT(0x0005f680)
void Missile::SetTarget(WTargetable *target) {
    this->target = target;
    if (target != NULL && !target->isTemporary)
        target->AddReference();
}

// In flight: steer, check for a hit, then the transit sound fades in and follows the body. Once exploded the
// body stops and the render object hides for the linger, then the object is deleted.
// FUNC_AT(0x00060a30)
void Missile::Simulate() {
    if (!exploded) {
        SteerMissile();
        CheckForCollision();
        PhysicsObject::Simulate();
        if (fadeIn < 1.0f) {
            double louder = double(fadeIn) + kFadeInStep;
            fadeIn = float(louder);
            if (louder > 1.0)
                fadeIn = 1.0f;
        }
        sound->volume = fadeIn;
        sound->position = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->position;
        sound->velocity = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->velocity;
        return;
    }
    if (lingerSteps-- != 0) {
        Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->velocity = kZero;
        renderObject->Hide();
        renderObject->StopFX();
        sound->volume = 0.0f;
    } else {
        Simulation_DeletePhysicsObject(Sim, 0, this);
    }
}

// Detonation (out of life, the mission manager's cinematicMode for a Q-Pulse, a remote torpedo over a soft zone, or
// SteerMissile's proximity) explodes it where it is. Once armed, the first body it touches that is not its owner's
// (a rigid body's box tested against its own) takes the hit: a Q-Pulse attacks a vehicle with its EMP; the
// player's missiles damage a vehicle; and it explodes. Then the step's segment is tested against the world - the
// player's level missiles first kept a little above the ground - and a hit breaks windows, scorches and explodes.
// FUNC_AT(0x0005f6d0)
void Missile::CheckForCollision() {
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    lifeSteps--;
    if (lifeSteps <= 0)
        detonate = 1;
    if (weapon == kWeaponQPulse && glbMissionManager->cinematicMode != 0)
        detonate = 1;
    if (weapon == kWeaponRemoteTorpedo) {
        alignas(16) Coord4 below = { body->position.x, body->position.y - kSoftZoneDepth, body->position.z, 0.0f };
        if (PBondCar_PosInSoftZone(&below))
            detonate = 1;
    }

    if (detonate) {
        if (weapon == kWeaponQPulse) {
            PlayEmpDetonate(&body->position, &body->velocity);
        } else {
            alignas(16) Coord4 forward;
            body->GetForwardVector(reinterpret_cast<Coord3 *>(&forward));
            Simulation_SpawnExplosion(Sim, 0, kMissileExplosion, weapon, Xyz(&forward), &body->position, damage, 0, NULL,
                                      DamageSourceSig);
        }
        if (IsDriveMissile(this))
            EndDrive();
        exploded = 1;
        return;
    }

    const BitSet192 *touched = &SimpleBodyHits[body->slot];
    if (SimStepCount > armStep) {
        for (int i = 0; i < kBodies; i++) {
            PhysicsObject *other = Simulation_GetOrderedBody(Sim, 0, i);
            if (!HitsBody(touched, i) || IsOwnedBy(other))
                continue;
            bool hit = true;
            if (weapon == kWeaponQPulse) {
                if (other->type == kObjectVehicle) {
                    PVehicle *vehicle = static_cast<PVehicle *>(other);
                    (vehicle->*XbeVirtual<void (PVehicle::*)()>(vehicle, PVehicle::kAttackWithEmp))();
                } else {
                    PlayEmpDetonate(&body->position, &body->velocity);
                }
            } else {
                if (!(other->flags & kSimpleBody)) {
                    alignas(16) OBB theirs;
                    alignas(16) OBB ours;
                    theirs.Construct();
                    ours.Construct();
                    alignas(16) Coord4 made;
                    alignas(16) Coord4 otherPosition = *MakeCoord4(&made, other->GetPosition(), 1.0f);
                    alignas(16) Coord4 ownPosition = *MakeCoord4(&made, &body->position, 1.0f);
                    alignas(16) Coord4 halfExtents = kHalfExtents;
                    halfExtents.w = body->radius;
                    alignas(16) MATRIX4 ownOrientation;
                    VU0_quattom4(&ownOrientation, &body->orientation);
                    const Coord4 *otherHalfExtents =
                        &Simulation_GetRigidBody(Sim, 0, other->rigidBodySlot)->info->halfExtents;
                    theirs.Reset(&Simulation_GetRigidBody(Sim, 0, other->rigidBodySlot)->info->orientation,
                                 &otherPosition, otherHalfExtents);
                    ours.Reset(&ownOrientation, &ownPosition, &halfExtents);
                    if (!ours.CheckOBBOverlap(&theirs))
                        hit = false;
                    NullFunction();     // ~OBB
                    NullFunction();
                }
                if (hit) {
                    alignas(16) Coord4 at = { body->position.x, body->position.y, body->position.z, 0.0f };
                    alignas(16) Coord4 forward;
                    VU0_ExtractZAxis3FromQuat(&body->orientation, &forward);
                    if (IsOwnedBy(Simulation_GetPlayerObject(Sim, 0)) && other->type == kObjectVehicle) {
                        static_cast<PVehicle *>(other)->AddDamageByPlayer(damage);
                        alignas(16) Coord4 heading, behind, ahead;
                        VU0_v4unitxyz(&body->velocity, &heading);
                        VU0_v4scaleadd(&heading, -kHitReach, &at, &behind);
                        VU0_v4scaleadd(&heading, kHitReach, &at, &ahead);
                        other->ApplyDamageVirtual(&behind, &ahead, kHitDamage, 0.0f, kDamageBullet, DamageSourceSig);
                    }
                    Simulation_SpawnExplosion(Sim, 0, kMissileExplosion, weapon, Xyz(&forward), Xyz(&at), damage, 0, other,
                                              DamageSourceSig);
                }
            }
            if (IsDriveMissile(this))
                EndDrive();
            if (hit) {
                exploded = 1;
                return;
            }
        }
    }

    // The step's segment, back from the body's position by its velocity
    alignas(16) Coord4 segment[2];
    segment[1] = { body->position.x, body->position.y, body->position.z, 1.0f };
    alignas(16) Coord4 step = { body->velocity.x, body->velocity.y, body->velocity.z, 1.0f };
    float climb = body->velocity.y < 0.0f ? -body->velocity.y : body->velocity.y;
    float steep = climb * kLevelRatio;
    bool level = VU0_v3length(&step) > steep;
    VU0_v4scale(&step, SimTimeStep, &step);
    segment[0].w = 0.0f;                // the original leaves the start's w as its stack had it
    VU0_v4sub(&segment[1], &step, &segment[0]);

    if (level && weapon != kWeaponSnowmobileRocket && weapon != kWeaponUltralightRocket &&
        weapon != kWeaponFodRocket && weapon != kWeaponTorpedo && weapon != kWeaponRemoteTorpedo &&
        IsOwnedBy(Simulation_GetPlayerObject(Sim, 0))) {
        Coord3 *end = reinterpret_cast<Coord3 *>(&segment[1]);
        worldPos.FindClosestFace(end, true);
        float ground = float(worldPos.HeightAtPoint(end, false));
        if (worldPos.valid) {
            double above = double(ground) - segment[1].y;
            if (above > double(MissileGroundClearance) * kClearanceBelow && above < MissileGroundFollowHeight) {
                segment[1].y = float(double(ground) + MissileGroundClearance);
                body->position = { segment[1].x, segment[1].y, segment[1].z };
            }
        }
    }

    WorldCollisionInfo info = {};
    info.point = { 0.0f, 0.0f, 0.0f, 1.0f };
    info.segmentStart = segment[0];
    info.segmentEnd = segment[1];
    info.instance = NULL;
    info.groupId = 0;
    info.object = NULL;
    info.faceType = 0;
    info.faceSubType = 0;
    info.flag52 = 0;
    info.hitType = 0;
    info.unknown54 = 0;
    info.faceInstance = NULL;
    if (weapon == kWeaponRemoteTorpedo) {
        alignas(16) Coord4 reach;
        body->GetForwardVector(reinterpret_cast<Coord3 *>(&reach));
        VU0_v4scale(&reach, kTorpedoReach, &reach);
        VU0_v3add(&segment[1], &reach, &segment[1]);
    }
    fgCollisionMgr->CheckHitWorld(segment, &info);
    if (!info.hitType)
        return;

    fgCollisionMgr->CheckHitWindow(&info, true, 0);
    if (weapon != kWeaponQPulse) {
        Shell::ApplyWorldDamage(damage, &worldPos, damage * kScorchPerDamage, kScorchType, &info);
        alignas(16) Coord4 forward;
        body->GetForwardVector(reinterpret_cast<Coord3 *>(&forward));
        Simulation_SpawnExplosion(Sim, 0, kMissileExplosion, weapon, Xyz(&forward), Xyz(&info.point), damage, info.faceType,
                                  NULL, DamageSourceSig);
    }
    if ((weapon == kWeaponTorpedo || weapon == kWeaponRemoteTorpedo) && info.faceType == kFaceType14)
        SMissionManager_ProgrammerDefinedEvent(glbMissionManager, 0, kProgrammerEvent21, GameEmptyString);
    if (IsDriveMissile(this))
        EndDrive();
    if (weapon == kWeaponQPulse)
        PlayEmpDetonate(&body->position, &body->velocity);
    exploded = 1;
}

// The drive missile turns and pitches by the user's steering, then rolls back towards level. Once the homing delay
// is over it turns towards the target (led by its velocity), more each step for the first two seconds, and slows
// unless facing it; it detonates within half a unit of a target that follows something, and gives up a fixed point
// once there (a torpedo: any target once it faces away). Before then it turns to its velocity. The thrust pushes
// it towards its top speed; the player's Missile corkscrews about its path, the corkscrew decaying.
// FUNC_AT(0x00060110)
void Missile::SteerMissile() {
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    float topSpeed = maxSpeed;
    float speed = body->GetScalarVelocity();
    if (target == NULL)
        homingFull = 1;
    else if (!homingFull)
        topSpeed = topSpeed * kUnlockedSpeed;
    alignas(16) MATRIX4 unused;
    body->RecalcOrientMat(&unused);
    alignas(16) Coord4 forward;
    body->GetForwardVector(reinterpret_cast<Coord3 *>(&forward));
    if (thrust > maxSpeed)
        thrust = maxSpeed;

    if (IsDriveMissile(this)) {
        steerX = EaseTowards(steerX, userSteerX);
        steerY = EaseTowards(steerY, userSteerY);
        alignas(16) Coord4 q;
        VU0_v4copy(&body->orientation, &q);
        alignas(16) MATRIX4 turned;
        VU0_quattom4(&turned, &q);
        alignas(16) MATRIX4 rotation;
        if (steerX != 0.0f) {
            VU0_MATRIX4setyrot(&rotation, steerX);
            VU0_MATRIX3x4_mult(&turned, &rotation, &turned);
        }
        // Pitch up while the nose is below kMaxPitch, down while above -kMaxPitch
        float noseY = turned.mtx[2][1];
        if ((steerY > 0.0f && noseY < kMaxPitch) || (steerY < 0.0f && noseY > -kMaxPitch)) {
            RCameraMath::BuildRotationMat4(&rotation, -steerY, reinterpret_cast<const Coord4 *>(turned.mtx[0]));
            VU0_MATRIX3x4_mult(&turned, &rotation, &turned);
        }
        alignas(16) Coord4 steered;
        VU0_m4toquat(&steered, &turned);
        VU0_fastqslerp(&q, &steered, &q, kSteerBlend);
        body->SetOrientation(&q);

        // Back towards level: the frame about the new heading (turned by nothing about it)
        alignas(16) Coord4 heading;
        body->GetForwardVector(reinterpret_cast<Coord3 *>(&heading));
        alignas(16) MATRIX4 generated;
        turned = *Util_GenerateMatrix(&generated, reinterpret_cast<Coord3 *>(&heading));
        RCameraMath::BuildRotationMat4(&rotation, 0.0f, reinterpret_cast<const Coord4 *>(turned.mtx[2]));
        VU0_MATRIX3x4_mult(&turned, &rotation, &turned);
        VU0_m4toquat(&steered, &turned);
        VU0_fastqslerp(&q, &steered, &q, kLevelBlend);
        body->SetOrientation(&q);
        alignas(16) Coord4 velocity;
        VU0_ExtractZAxis3FromQuat(&q, &velocity);
        VU0_v4scale(&velocity, body->GetScalarVelocity(), &velocity);
        body->velocity = { velocity.x, velocity.y, velocity.z };
    }

    if (target != NULL && SimStepCount - launchStep > homingDelay) {
        alignas(16) Coord4 aim = { target->position.x, target->position.y, target->position.z, 0.0f };
        alignas(16) Coord4 targetVelocity;
        if (target->GetVelocity(reinterpret_cast<Coord3 *>(&targetVelocity)))
            VU0_v4scaleadd(&targetVelocity, SimTimeStep * kLeadTime, &aim, &aim);
        alignas(16) Coord4 toTarget;
        VU0_v4sub(&aim, &body->position, &toTarget);
        float distanceSquared = VU0_v3lengthsquare(&toTarget);
        VU0_v4unitxyz(&toTarget, &toTarget);

        // The player's own target: the lock sound, more often the closer it is
        PVehicle *player = static_cast<PVehicle *>(PlayerPhysicsObject);
        WTargetable *beacon = (player->*XbeVirtual<WTargetable *(PVehicle::*)()>(player, PVehicle::kGetTargetBeacon))();
        if (target == beacon && lockSoundStep < SimStepCount) {
            AMenuSound::Trigger(0, "SFX_Htargetlock", "Tracking System", kSoundViewShared);
            lockSoundStep += Truncate(float(double(SimStepsPerSecond) * distanceSquared * kLockSoundRate));
        }

        if (distanceSquared > kMinTurnDistanceSquared) {
            alignas(16) MATRIX4 generated;
            alignas(16) MATRIX4 facing = *Util_GenerateMatrix(&generated, reinterpret_cast<Coord3 *>(&toTarget));
            alignas(16) Coord4 wanted;
            VU0_m4toquat(&wanted, &facing);
            double rate = homingFull ? double(turnRate) : double(turnRate) * 0.5;
            double ramp = double(SimStepCount - launchStep - homingDelay) * SimTimeStep * 0.5;
            if (1.0 < ramp)
                ramp = 1.0;
            alignas(16) Coord4 turned;
            VU0_fastqslerp(&body->orientation, &wanted, &turned, float(ramp * rate));
            body->SetOrientation(&turned);
            alignas(16) Coord4 velocity;
            VU0_ExtractZAxis3FromQuat(&turned, &velocity);
            VU0_v4scale(&velocity, body->GetScalarVelocity(), &velocity);
            body->velocity = { velocity.x, velocity.y, velocity.z };
        }

        alignas(16) Coord4 heading;
        VU0_v4unitxyz(&body->velocity, &heading);
        float facing = v3dotprod(&toTarget, &heading);
        if (facing > kFacingMin)
            topSpeed = facing * topSpeed;
        else
            topSpeed = topSpeed * kFacingMin;
        detonate = target->ownerType != kTargetPoint && distanceSquared <= kProximitySquared;
        if (weapon == kWeaponTorpedo) {
            if (facing < kTorpedoFacingMin) {
                target->RemoveReference();
                target = NULL;
                lifeSteps = SimStepsPerSecond >> 3;
            }
        } else if (target->ownerType == kTargetPoint && distanceSquared <= kPointReachedSquared) {
            target->RemoveReference();
            target = NULL;
        }
        if (facing > kLockedFacing)
            homingFull = 1;
    } else if (SimStepCount - launchStep <= homingDelay) {
        alignas(16) Coord4 heading;
        VU0_v4unitxyz(&body->velocity, &heading);
        alignas(16) MATRIX4 generated;
        alignas(16) MATRIX4 facing = *Util_GenerateMatrix(&generated, reinterpret_cast<Coord3 *>(&heading));
        alignas(16) Coord4 wanted, turned;
        VU0_m4toquat(&wanted, &facing);
        VU0_fastqslerp(&body->orientation, &wanted, &turned, kAlignBlend);
        body->SetOrientation(&turned);
    }

    alignas(16) Coord4 acceleration;
    VU0_v4scale(&forward, speed < topSpeed ? thrust : -thrust, &acceleration);
    body->Accelerate(reinterpret_cast<Coord3 *>(&acceleration));

    if (wobbleX > kWobbleMin || wobbleX < -kWobbleMin) {
        double age = double(SimStepCount - launchStep) * SimTimeStep;
        float ramp = 1.0 < age ? 1.0f : float(age);
        wobbleX = float((1.0 - (double(SimTimeStep) + SimTimeStep)) * wobbleX);
        wobbleY = float((1.0 - (double(SimTimeStep) + SimTimeStep)) * wobbleY);
        wobblePhase = wobblePhase + SimTimeStep;
        while (wobblePhase > 1.0f)
            wobblePhase = wobblePhase - 1.0f;
        alignas(16) Coord4 drift;
        drift.x = float(-(CosineTurns(wobblePhase) * body->velocity.z * wobbleX));
        drift.z = float(CosineTurns(wobblePhase) * wobbleX * body->velocity.x);
        drift.y = float(SineTurns(wobblePhase) * wobbleY);
        drift.w = 0.0f;
        VU0_v4scaleadd(&drift, ramp * SimTimeStep, &body->position, &drift);
        body->position = { drift.x, drift.y, drift.z };
    }
}
