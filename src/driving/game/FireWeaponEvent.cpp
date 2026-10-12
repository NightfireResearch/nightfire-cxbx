#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#pragma fp_contract(off)

#include "FireWeaponEvent.h"

#include <stdio.h>
#include <string.h>

#include "AIViews.h"                      // AIGroundVehicle, AIHelicopter, AICharacter
#include "Helicopter.h"                   // PHelicopter::ai
#include "Human.h"
#include "Missile.h"
#include "MissionManager.h"               // glbMissionManager
#include "Sentry.h"
#include "Shell.h"                        // RayShell
#include "Simulation.h"                   // fgSimulation
#include "Vehicle.h"                      // PVehicle's vtable slots
#include "WeaponEffects.h"                // BulletStreakKinds
#include "WeaponManager.h"
#include "../anim/Manager.h"              // MakeCoord4
#include "../anim/Weapon.h"               // ActWeapon
#include "../audio/Bank.h"                // fgBanks
#include "../audio/Sound.h"               // ABaseSound, AOneShotSound, AMenuSound
#include "../camera/PlayerCamera.h"       // RPlayerCamera
#include "../data/CoordConvert.h"         // Float_COORD4toCOORD3
#include "../engine/Feedback.h"           // IFeedback
#include "../engine/GameLoop.h"           // LaunchPage
#include "../engine/PhysicsUtil.h"        // Util_PerturbVector
#include "../engine/SimRandom.h"          // SimRandom, Noise
#include "../engine/UMemory.hpp"          // UMemory::FastAlloc
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RenderHigh.h"         // fgRenderHigh
#include "../render/RSceneObj.hpp"
#include "../world/Targeting.h"           // WTargetable, TargetPicker
#include "../world/Trigger.h"             // gEventDynamicData
#include "../world/World.h"               // ArticleEffect
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// EFireWeapon: see FireWeaponEvent.h.
// ---------------------------------------------------------------------------------------------------------------


// The henchmen's gun sounds' counters (EFireWeapon's alone; names ours)
// XBE_GLOBAL(0x001e23a8, 0x4)
int32_t gCarGunShots;            // an AI car's: the sound on every other shot in "snow1a_mis3"
// XBE_GLOBAL(0x001e23ac, 0x4)
uint32_t gHenchGunPick;          // picks between the two machine gun sounds
// XBE_GLOBAL(0x001e23b0, 0x4)
uint32_t gHenchGunShots;         // a holder's weapon's: as gCarGunShots

// ---- the vtables (the game's; each has the one slot, the scalar deleting destructor)

#define kEFireWeaponVtable ((vtable_Event *)0x0018c520)

namespace {

// Bullets are ray shells (the byte at 0x001c3df0, which nothing writes, is 1); without it, shells
const bool kRayShellBullets = true;

const int kWeaponBankSlot = 4;

// What a spawned missile, mine or grenade's scene object is given of the firing instance (UseInstance's last two
// arguments)
const uint8_t kShotProcAnimType = 0xfb;
const uint16_t kShotProcAnimIndex = 0x40;

// AIZoneController::RegisterZone's type for the player's shots
const int kZonePlayerFire = 2;

// AttributeSet::LookupInt's answer for a car that flies (IS_FLYING) and an AI that aims well (AI_GOODAIM)
const int kAttributeSet = 1;

const char *const kHenchGunPatches[2] = {"SFX_HMachGun1", "SFX_HMachGun2"};

const MATRIX4 kIdentity = { {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
} };

Coord4 *Row(MATRIX4 *matrix, int row) {
    return reinterpret_cast<Coord4 *>(matrix->mtx[row]);
}

int Clamp(int value, int low, int high) {
    if (value > high)
        return high;
    if (value < low)
        return low;
    return value;
}

// A shooter's streak counter: counts the shot, and answers whether this shot has no streak - every shot but the
// one the counter starts from, in the streak kind's count
template <class T> bool CountStreakShot(T &shots, int32_t period) {
    bool counting = shots != 0;
    shots++;
    if (int32_t(shots) >= period)
        shots = 0;
    return counting;
}

// The AI's aim noise by difficulty (SMissionManager::difficulty 1, 2, other)
float AimNoiseScale() {
    switch (glbMissionManager->difficulty) {
    case 1:
        return 0.01f;
    case 2:
        return 0.005f;
    default:
        return 0.015f;
    }
}

// The AI's aim: `direction` turned by noise that follows the Simulation's steps, phased by the shooter's body slot
// (the signature's bits 24-30), then made the shot's aim (w 1); `direction` itself if the roll hits
void AimWithNoise(Coord4 *aim, const Coord4 *direction, uint32_t ownerSig, float noiseScale, bool hit) {
    float phase = (float)((ownerSig >> 24) & 0x7f);
    double n = Noise::Noise1((float)(fgSimulation.stepCount * (double)0.02f + phase));
    double offset = n * noiseScale;
    float yTurns = (float)(offset + offset);
    n = Noise::Noise1((float)(fgSimulation.stepCount * (double)0.02f + phase + 275.0f));
    offset = n * noiseScale;
    float zTurns = (float)(offset + offset);
    Coord3 turned;
    Coord3 perturbed = *Util_PerturbVector(&turned, yTurns, zTurns, direction);
    alignas(16) Coord4 made;
    *aim = *MakeCoord4(&made, &perturbed, 1.0f);
    if (hit)
        *aim = *MakeCoord4(&made, Xyz(direction), 1.0f);
}

// A target at a point for the shot (WTargetPicker's temporary kind), and the aim lowered by half. Written to without
// a check that it was made, as in the original.
WTargetable *TemporaryTarget(const Coord3 *at, Coord4 *aim) {
    void *memory = UMemory::FastAlloc(sizeof(WTargetable), "WTargetable");
    WTargetable *target = NULL;
    if (memory != NULL)
        target = static_cast<WTargetable *>(memory)->Construct(at->x, at->y, at->z);
    target->isTemporary = 1;
    aim->y = aim->y * 0.5f;
    return target;
}

}  // namespace

// FUNC_AT(0x00040090)
void EFireWeapon_MakeEvent_Callback(const EFireWeapon::Record *record) {
    EFireWeapon *event = new EFireWeapon;
    if (event != NULL) {
        event->vtable = kEFireWeaponVtable;
        event->effect = record->effect;
        event->sceneObj = gEventDynamicData.sceneObj;
        event->instance = gEventDynamicData.instance;
        event->instanceIndex = gEventDynamicData.instanceIndex;
        event->weapon = gEventDynamicData.weapon;
    }
}

// The shot's frame is the muzzle effect's (or the instance's), in the world: the player's weapons take the
// instance's frame from an identity instead. The shot leaves from its row 3 along row 0 (a player's muzzle), 1
// (another's muzzle) or 2 (no muzzle) unless the aim below replaces that. Weapon types without a case spawn nothing
// and make no sound.
void EFireWeapon::Process() {
    bool playerWeapon = weapon != NULL && (weapon->flags & ActWeapon::kPlayerOwned) != 0;
    uint32_t muzzleInstance = instanceIndex;
    ArticleEffect *gfxEffect = NULL;
    RSceneObj *gfxSceneObj = NULL;
    alignas(16) MATRIX4 frame;
    int aimRow;
    if (effect != NULL) {
        sceneObj->LocateFX(effect, &frame, true);
        if (effect->type == ArticleEffect::kTypeGfx) {
            gfxEffect = effect;
            gfxSceneObj = sceneObj;
            muzzleInstance = effect->instance;
        } else if (effect->type == ArticleEffect::kTypeGlare) {
            muzzleInstance = effect->instance;
        }
        aimRow = playerWeapon ? 0 : 1;
    } else {
        sceneObj->GetInstancePosition(muzzleInstance, &frame, true);
        aimRow = 2;
    }
    if (playerWeapon) {
        frame = kIdentity;
        sceneObj->TransformMatrixByInstancePosition(muzzleInstance, &frame, true);
    } else {
        sceneObj->TransformToWorldSpaceVirtual(&frame);
    }

    // ---- who fires
    WTargetable *target = NULL;
    PhysicsObject *owner = sceneObj->physics;
    if (owner == NULL && weapon != NULL)
        owner = weapon->owner;
    uint32_t ownerSig = 0;
    uint32_t sig;
    if (playerWeapon)
        ownerSig = *fgSimulation.GetPlayerObject()->GetSig(&sig);
    else if (owner != NULL)
        ownerSig = *owner->GetSig(&sig);
    uint32_t system = sceneObj->GetInstanceSystemID(instanceIndex);
    int weaponType = WeaponManager->GetWeaponFromSystem(system);

    bool fromHelicopter = owner != NULL && owner->type == kObjectHelicopter;
    AIHelicopter *helicopterAim = NULL;
    if (fromHelicopter)     // fields game/Helicopter.h's view of the AI does not map yet
        helicopterAim = static_cast<PHelicopter *>(owner)->ai;
    bool fromCar = owner != NULL && owner->type == kObjectVehicle;
    bool fromPlayerCar = fromCar && owner == fgSimulation.cars.first[0];
    bool playerAims = (glbMissionManager->autoDrive && fromPlayerCar) || playerWeapon;
    bool fromSentry = owner != NULL && owner->type == kObjectSentry;
    bool flyingCar = fromCar && owner->attributes.LookupInt("IS_FLYING", NULL) == kAttributeSet && !fromHelicopter &&
                     !fromPlayerCar;
    if (fromPlayerCar || playerWeapon) {
        target = TargetPicker.GetSelectedTarget();
        TargetPicker.CycleToNextTarget();
    }

    Coord3 ownerVelocity;
    float ownerSpeed;
    if (owner != NULL) {
        if (!(owner->flags & PhysicsObject::kSimpleBody)) {
            RigidBody *body = fgSimulation.GetRigidBody(owner->rigidBodySlot);
            ownerSpeed = VU0_v3length(&body->velocity);
            ownerVelocity = body->velocity;
            fgSimulation.GetRigidBody(owner->rigidBodySlot);     // asked for again, the answer not used
        } else {
            ownerVelocity = fgSimulation.GetSimpleRigidBody(owner->rigidBodySlot)->velocity;
            ownerSpeed = fgSimulation.GetSimpleRigidBody(owner->rigidBodySlot)->GetScalarVelocity();
        }
    } else if (playerWeapon) {
        ownerVelocity = fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->velocity;
        ownerSpeed = VU0_v3length(&fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->velocity);
    } else {
        ownerVelocity.x = 0.0f;
        ownerVelocity.y = 0.0f;
        ownerVelocity.z = 0.0f;
        ownerSpeed = 0.0f;
    }

    // The streak: -1 (none) on all but one shot in the streak kind's count
    int streakType = 0;
    if (playerWeapon) {
        if (weaponType == kWeaponSniperRifle) {
            streakType = -1;
        } else {
            streakType = fgSimulation.cars.first[0]->attributes.LookupInt("BULLET_STREAK_TYPE", NULL);
            if (CountStreakShot(fgSimulation.cars.first[0]->unknown68, BulletStreakKinds[streakType].count))
                streakType = -1;
        }
    } else if (owner != NULL) {
        streakType = owner->attributes.LookupInt("BULLET_STREAK_TYPE", NULL);
        if (CountStreakShot(owner->unknown68, BulletStreakKinds[streakType].count))
            streakType = -1;
    } else if (weapon != NULL) {
        streakType = weapon->unknown164;
        if (CountStreakShot(weapon->unknown160, BulletStreakKinds[streakType].count))
            streakType = -1;
    }
    if (!playerAims) {
        gfxEffect = NULL;
        gfxSceneObj = NULL;
    }

    // ---- the aim
    Coord4 *aim = Row(&frame, aimRow);
    Coord4 *position = Row(&frame, 3);
    alignas(16) Coord4 origin = *position;
    float accuracyScale = 1.0f;
    if (fromHelicopter) {
        if (weaponType == kWeaponMissile)
            target = TemporaryTarget(&helicopterAim->target, aim);
        else if (weaponType == kWeaponGun)
            *aim = helicopterAim->aim;
    } else if (playerAims) {
        RPlayerCamera *camera = fgRenderHigh->views[0].camera;
        *aim = *camera->GetForwardAimVec4(1.0f);
        if (weaponType != kWeaponRocket && weaponType != kWeaponSnowmobileRocket &&
            weaponType != kWeaponUltralightRocket && weaponType != kWeaponFodRocket)
            *position = *Row(&fgRenderHigh->views[0].camera->matrix, 3);
    } else if ((!playerWeapon && weapon != NULL) || fromSentry) {
        // A holder's weapon or a sentry: at the character's command target, else at the player's car (leading it
        // with weapon 1)
        WeaponAccuracy *accuracyInfo = NULL;
        AICharacter *character = NULL;
        alignas(16) Coord4 targetPoint;
        bool commanded = false;
        if (weapon != NULL) {
            accuracyInfo = weapon->accuracy;
            character = static_cast<Human *>(weapon->owner)->character;
            if (accuracyInfo != NULL)
                accuracyScale = Clamp(accuracyInfo->aimScale + 100, 0, 200) * 0.01f;
            if (character != NULL && character->hasCommandTarget == 1) {
                targetPoint.x = character->commandTarget.x;
                targetPoint.y = character->commandTarget.y;
                targetPoint.z = character->commandTarget.z;
                commanded = true;
            }
        }
        if (!commanded) {
            Coord3 *player = fgSimulation.cars.first[0]->GetPosition();
            targetPoint.x = player->x;
            targetPoint.y = player->y;
            targetPoint.z = player->z;
            if (weaponType == kWeaponRocket) {
                Coord3 *velocity = fgSimulation.cars.first[0]->GetLinearVelocity();
                alignas(16) Coord4 lead = {velocity->x, velocity->y, velocity->z, 0.0f};
                double ahead = vec3distance(&targetPoint, position) * (double)0.75f;
                if (20.0 > ahead)
                    ahead = 20.0;
                VU0_v4scaleadd(&lead, (float)(fgSimulation.timeStep * ahead), &targetPoint, &targetPoint);
            } else if (weaponType == kWeaponMissile && fromSentry) {
                target = static_cast<Sentry *>(owner)->GetTarget();
            }
        }
        alignas(16) Coord4 toTarget;
        VU0_v4sub(&targetPoint, position, &toTarget);
        alignas(16) Coord4 direction;
        VU0_v4unitxyz(&toTarget, &direction);
        if (target == NULL) {
            int accuracy = accuracyInfo != NULL ? accuracyInfo->accuracy : 100;
            float noiseScale = AimNoiseScale();
            accuracy += glbMissionManager->GetEnemyAccuracyBoost();
            bool clamp = true;
            if (character != NULL) {
                AICharacterAttitude *attitude = character->GetAttitude();
                if (attitude != NULL)
                    accuracy += (attitude->GetAccuracyGrade() - 4) * 30;
                if (weaponType == kWeaponRocket && direction.y < -0.2f) {
                    direction.y = -0.2f;
                    VU0_v4unitxyz(&direction, &direction);
                    accuracy = 0;
                    clamp = false;
                }
            }
            if (clamp)
                accuracy = Clamp(accuracy, 0, 100);
            int roll = int32_t(fgSimulation.random->Generate() * 100) >> 16;
            AimWithNoise(aim, &direction, ownerSig, noiseScale, roll < accuracy);
        }
    } else if (fromCar && !fromPlayerCar) {
        // An AI car: at the player's car's beacon, or by the AI's aim with its accuracy and noise
        PVehicle *car = static_cast<PVehicle *>(owner);
        AIGroundVehicle *ai = car->GetAIGroundVehiclePtr();
        accuracyScale = Clamp(ai->aimScale + 100, 0, 200) * 0.01f;
        if (weaponType == kWeaponTorpedo) {
            target = fgSimulation.cars.first[0]->GetTargetBeacon();
        } else if (weaponType == kWeaponMissile) {
            int8_t aiAccuracy = ai->accuracy;
            int chance = Clamp(glbMissionManager->GetEnemyAccuracyBoost() + aiAccuracy, 0, 100);
            int roll = int32_t(fgSimulation.random->Generate() * 100) >> 16;
            if (roll < chance)
                target = fgSimulation.cars.first[0]->GetTargetBeacon();
            else
                target = TemporaryTarget(ai->GetTargetPos(), aim);
        } else if (weaponType == kWeaponGun || weaponType == kWeaponRocket || weaponType == kWeaponCannon) {
            int8_t aiAccuracy = ai->accuracy;
            int chance = Clamp(glbMissionManager->GetEnemyAccuracyBoost() + aiAccuracy, 0, 100);
            int roll = int32_t(fgSimulation.random->Generate() * 100) >> 16;
            alignas(16) Coord4 aimAt;
            if (weaponType == kWeaponGun) {
                if (owner->attributes.LookupInt("AI_GOODAIM", NULL) == kAttributeSet) {
                    Coord3 *player = fgSimulation.cars.first[0]->GetPosition();
                    alignas(16) Coord4 playerPoint = {player->x, player->y, player->z, 0.0f};
                    alignas(16) Coord4 toPlayer;
                    VU0_v4sub(&playerPoint, position, &toPlayer);
                    VU0_v4unitxyz(&toPlayer, &aimAt);
                } else {
                    aimAt.x = ai->aimPoint.x;
                    aimAt.y = ai->aimPoint.y;
                    aimAt.z = ai->aimPoint.z;
                }
            } else if (weaponType == kWeaponRocket) {
                aimAt.x = ai->leadPoint.x;
                aimAt.y = ai->leadPoint.y;
                aimAt.z = ai->leadPoint.z;
            } else {
                aimAt.x = ai->aimPoint.x;
                aimAt.y = ai->aimPoint.y;
                aimAt.z = ai->aimPoint.z;
            }
            aimAt.w = 0.0f;     // stack garbage in the original; Util_PerturbVector and MakeCoord4 read x, y and z
            AimWithNoise(aim, &aimAt, ownerSig, AimNoiseScale(), roll < chance);
        }
    }

    // ---- the shot
    float power = (float)(WeaponManager->weapons[weaponType].damage * (double)accuracyScale);
    if (fromPlayerCar)
        fgRenderHigh->views[0].camera->WeaponFired(weaponType);

    PhysicsObject *shot = NULL;
    switch (weaponType) {
    case kWeaponGun:
    case kWeaponP2K: {
        if (fromPlayerCar && !playerWeapon && target != NULL && WeaponManager->turretOnTarget &&
            fgSimulation.cars.first[0]->attributes.LookupBool("HAS_TURRET", NULL)) {
            VU0_v4sub(&target->position, position, aim);
            VU0_v4unit(aim, aim);
        }
        double basePower = fromHelicopter ? 5.0
                                          : playerAims ? WeaponManager->weapons[kWeaponSnowmobileGun].damage
                                                       : WeaponManager->weapons[kWeaponGun].damage;
        power = (float)(basePower * accuracyScale);
        if (kRayShellBullets) {
            void *memory = UMemory::FastAlloc(0xe0, "RayShell");
            if (memory != NULL)
                static_cast<RayShell *>(memory)->Construct(weaponType, ownerSig, Xyz(position), Xyz(aim), Xyz(&origin),
                                                           gfxSceneObj, gfxEffect, 220.0f, power, 1, 0.0f, streakType);
        } else {
            if (playerAims) {
                Coord3 spread;
                Coord3 turned = *Util_PerturbVector(&spread, aim, 0.002f);
                alignas(16) Coord4 made;
                *aim = *MakeCoord4(&made, &turned, 1.0f);
            }
            fgSimulation.SpawnShellObject(weaponType, Xyz(aim), Xyz(position), ownerSig, power, 0.0f, streakType);
        }
        break;
    }
    case kWeaponRocket:
    case kWeaponSnowmobileRocket:
    case kWeaponUltralightRocket:
    case kWeaponFodRocket:
        if (!playerAims || fromHelicopter) {
            shot = fgSimulation.SpawnMissileObject(weaponType, ownerSpeed, Xyz(aim), Xyz(position), ownerSig, power,
                                                   target, system);
            if (shot != NULL)
                shot->renderObject->UseInstance(sceneObj, muzzleInstance, &kIdentity, kShotProcAnimType,
                                                kShotProcAnimIndex);
        } else {
            // The player's: with no target and the camera not aiming, the selected AI helicopter's beacon, if it
            // is near still and alive, as the snowmobile rocket. The missile gets the car's velocity too.
            Coord3 converted;
            Coord3 from = *Float_COORD4toCOORD3(&converted, position);
            int missileType = weaponType;
            Coord3 along = *Float_COORD4toCOORD3(&converted, aim);
            if (target == NULL && !fgRenderHigh->views[0].camera->CameraAiming()) {
                WTargetable *selected = TargetPicker.GetSelectedTarget();
                if (selected != NULL && selected->ownerType == kTargetVehicle) {
                    AIVehicle *vehicle = static_cast<AIVehicle *>(selected->owner);
                    if (vehicle != NULL && vehicle->vehicleType == kAIHelicopter) {
                        PhysicsObject *physics = vehicle->GetPhysicsObject();
                        if (VU0_v3length(physics->GetLinearVelocity()) < 2.0f && physics->GetHitPoints() > 0.0f) {
                            target = static_cast<AIHelicopter *>(vehicle)->targetBeacon;
                            if (target != NULL) {
                                target->AddReference();
                                missileType = kWeaponSnowmobileRocket;
                            }
                        }
                    }
                }
            }
            shot = fgSimulation.SpawnMissileObject(missileType, ownerSpeed, &along, &from, ownerSig, power, target,
                                                   system);
            if (shot != NULL) {
                shot->renderObject->UseInstance(sceneObj, muzzleInstance, &kIdentity, kShotProcAnimType,
                                                kShotProcAnimIndex);
                alignas(16) Coord4 velocity;
                const Coord3 &shotVelocity = fgSimulation.GetSimpleRigidBody(shot->rigidBodySlot)->velocity;
                velocity.x = shotVelocity.x;
                velocity.y = shotVelocity.y;
                velocity.z = shotVelocity.z;
                VU0_v3add(&velocity, &ownerVelocity, &velocity);
                fgSimulation.GetSimpleRigidBody(shot->rigidBodySlot)->velocity = *Xyz(&velocity);
            }
        }
        break;
    case kWeaponMissile:
    case kWeaponQPulse:
    case kWeaponTorpedo:
        shot = fgSimulation.SpawnMissileObject(weaponType, ownerSpeed, Xyz(aim), Xyz(position), ownerSig, power, target,
                                               system);
        if (shot != NULL)
            shot->renderObject->UseInstance(sceneObj, muzzleInstance, &kIdentity, kShotProcAnimType,
                                            kShotProcAnimIndex);
        break;
    case kWeaponRemoteTorpedo:
    case kWeaponCh6RemoteRocket:
        shot = fgSimulation.SpawnMissileObject(weaponType, ownerSpeed, Xyz(aim), Xyz(position), ownerSig, power, target,
                                               system);
        if (shot != NULL) {
            shot->renderObject->UseInstance(sceneObj, muzzleInstance, &kIdentity, kShotProcAnimType,
                                            kShotProcAnimIndex);
            glbMissionManager->SetDriveMissile(static_cast<Missile *>(shot));
        }
        break;
    case kWeaponFloatingMine:
        shot = fgSimulation.SpawnMineObject(kWeaponFloatingMine, Xyz(aim), Xyz(position), 0.0f, ownerSig);
        if (shot != NULL)
            shot->renderObject->UseInstance(sceneObj, muzzleInstance, &kIdentity, kShotProcAnimType,
                                            kShotProcAnimIndex);
        break;
    case kWeaponCannon:
        fgSimulation.SpawnShellObject(kWeaponCannon, Xyz(aim), Xyz(position), ownerSig, power * 0.75f, power * 0.25f,
                                      streakType);
        break;
    case kWeaponGrenade: {
        // Lobbed: the aim raised, the grenade given half again its velocity plus the shooter's, and moved three
        // along the aim (level)
        alignas(16) Coord4 lob = {aim->x, (float)(aim->y * (double)1.6f + 0.2f), aim->z, 0.0f};
        VU0_v4unitxyz(&lob, &lob);
        shot = fgSimulation.SpawnGrenadeObject(kWeaponGrenade, Xyz(&lob), Xyz(position), 55.0f, ownerSig);
        if (shot != NULL) {
            SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(shot->rigidBodySlot);
            alignas(16) Coord4 velocity = {body->velocity.x, body->velocity.y, body->velocity.z, 0.0f};
            VU0_v4scaleadd(&velocity, 1.5f, &ownerVelocity, &velocity);
            body->velocity = *Xyz(&velocity);
            lob.y = 0.0f;
            alignas(16) Coord4 at = {body->position.x, body->position.y, body->position.z, 0.0f};
            VU0_v4scaleadd(&lob, 3.0f, &at, &at);
            body->position = *Xyz(&at);
            shot->renderObject->UseInstance(sceneObj, muzzleInstance, &kIdentity, kShotProcAnimType,
                                            kShotProcAnimIndex);
        }
        break;
    }
    case kWeaponSnowmobileGun:
    case kWeaponUltralightGun:
    case kWeaponFodGun: {
        // the ultralight gun's damage for all three
        float rayPower = (float)WeaponManager->weapons[kWeaponUltralightGun].damage;
        void *memory = UMemory::FastAlloc(0xe0, "RayShell");
        if (memory != NULL)
            static_cast<RayShell *>(memory)->Construct(weaponType, ownerSig, Xyz(position), Xyz(aim), Xyz(&origin),
                                                       gfxSceneObj, gfxEffect, 220.0f, rayPower, 1, 0.0f, streakType);
        break;
    }
    case kWeaponSniperRifle: {
        float rayPower = (float)WeaponManager->weapons[kWeaponSniperRifle].damage;
        void *memory = UMemory::FastAlloc(0xe0, "RayShell");
        if (memory != NULL)
            static_cast<RayShell *>(memory)->Construct(kWeaponSniperRifle, ownerSig, Xyz(position), Xyz(aim),
                                                       Xyz(&origin), gfxSceneObj, gfxEffect, 400.0f, rayPower, 1, 0.0f,
                                                       streakType);
        break;
    }
    default:
        return;
    }

    // ---- the sound: the weapon's "SFX_<name>Fire" from bank slot 4, the henchmen's machine guns their own
    ABank *bank = fgBanks[kWeaponBankSlot];
    WeaponRecord *fired = &WeaponManager->weapons[weaponType];
    char patchName[0x3c];
    sprintf(patchName, "SFX_%sFire", fired->sound);
    int patch = bank->index.Lookup(patchName);
    bool rumble = false;
    switch (weaponType) {
    case kWeaponGun:
        if (weapon != NULL) {
            if (!(weapon->flags & ActWeapon::kPlayerOwned)) {
                gHenchGunShots++;
                gHenchGunShots &= _stricmp(Launch.missionName, "snow1a_mis3") == 0;
                if (gHenchGunShots == 0) {
                    const char *gun = kHenchGunPatches[gHenchGunPick & 1];
                    gHenchGunPick++;
                    int gunPatch = bank->index.Lookup(gun);
                    void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
                    AOneShotSound *sound = NULL;
                    if (memory != NULL)
                        sound = static_cast<AOneShotSound *>(memory)->Construct(bank->handle, gunPatch, Xyz(position),
                                                                                &ownerVelocity, 1.0f, "Henchmen Fire");
                    sound->minDistance = 20.0f;
                    sound->maxDistance = 200.0f;
                    sound->maxDistanceSq = 40000.0f;
                }
            }
        } else if (fromCar && !fromPlayerCar) {
            gCarGunShots++;
            if (gCarGunShots > 1)
                gCarGunShots = 0;
            gCarGunShots *= _stricmp(Launch.missionName, "snow1a_mis3") == 0;
            if (gCarGunShots == 0) {
                int gunPatch = bank->index.Lookup(flyingCar ? "SFX_HumntmachFire" : "SFX_hmntmachfire");
                void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
                AOneShotSound *sound = NULL;
                if (memory != NULL)
                    sound = static_cast<AOneShotSound *>(memory)->Construct(bank->handle, gunPatch, Xyz(position),
                                                                            &ownerVelocity, 1.0f, "Henchmen Fire");
                sound->minDistance = 20.0f;
                sound->maxDistance = 300.0f;
                sound->maxDistanceSq = 90000.0f;
                if (flyingCar)
                    sound->pitch = 0.75f;
            }
        }
        rumble = true;
        break;
    case kWeaponRocket:
    case kWeaponMissile:
    case kWeaponProxMine:
    case kWeaponCannon:
    case kWeaponGrenade:
    case kWeaponTorpedo: {
        // A holder's weapon that is not the player's leaves the mix name as the stack had it (see the report)
        char mix[0x2c] = {};
        if (weapon != NULL) {
            if (weapon->flags & ActWeapon::kPlayerOwned)
                sprintf(mix, "Weapons Fire");
        } else {
            sprintf(mix, fromPlayerCar ? "Weapons Fire" : "Henchmen Fire");
        }
        if (weaponType == kWeaponTorpedo && fromCar && !fromPlayerCar)
            patch = bank->index.Lookup("SFX_htorpedofire");
        void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
        AOneShotSound *sound = NULL;
        if (memory != NULL)
            sound = static_cast<AOneShotSound *>(memory)->Construct(bank->handle, patch, Xyz(position), &ownerVelocity,
                                                                    1.0f, mix);
        sound->minDistance = 50.0f;
        sound->maxDistance = 200.0f;
        sound->maxDistanceSq = 40000.0f;
        break;
    }
    case kWeaponQBooster:
    case kWeaponQSlick:
    case kWeaponQSmoke:
    case kWeaponQPulse:
    case kWeapon2Wheels:
    case kWeaponRemoteTorpedo:
    case kWeaponSnowmobileRocket:
    case kWeaponSniperRifle:
    case kWeaponUltralightRocket:
    case kWeaponFodRocket:
        AMenuSound::Trigger(fired->soundBank, patch, "POV Weapons", 0);
        break;
    default:
        break;
    }

    // ---- the force feedback: a rumble for the machine guns and ray shells, a jolt for the rest
    if (weaponType == kWeaponGun || weaponType == kWeaponFodGun || weaponType == kWeaponUltralightGun ||
        weaponType == kWeaponSnowmobileGun)
        rumble = true;
    if (rumble) {
        if (playerAims)
            fgSimulation.cars.first[0]->feedbackObject->SetRumble(0.51f, 0.2f);
        else if (owner != NULL && owner->feedbackObject != NULL)
            owner->feedbackObject->SetRumble(0.51f, 0.2f);
    } else {
        if (playerAims)
            fgSimulation.cars.first[0]->feedbackObject->ApplyJolt(1.0f);
        else if (owner != NULL && owner->feedbackObject != NULL)
            owner->feedbackObject->ApplyJolt(1.0f);
    }
    if (fromPlayerCar)
        AIZoneController::Get()->RegisterZone(kZonePlayerFire, Xyz(position));
}

// FUNC_AT(0x0004ac40)
void EFireWeapon::Destruct() {
    vtable = kEFireWeaponVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x0004ec40)
EFireWeapon* EFireWeapon::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}
