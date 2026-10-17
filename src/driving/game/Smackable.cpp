#pragma fp_contract(off)

#include "Smackable.h"

#include <string.h>

#include "Simulation.h"
#include "Vehicle.h"                    // fgDamageLevels
#include "WeaponSound.h"                // ASmackable
#include "../anim/AnimEngine.h"         // Handle
#include "../audio/Sound.h"             // ABaseSound
#include "../data/AttributeSet.h"
#include "../data/Carp.h"               // CARP::BaseDesc
#include "../engine/PhysicsUtil.h"      // Util_GenerateTensor
#include "../engine/StaticInit.h"       // DestroyStatic_001e9294
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsNamespace.h"    // PhysicsData
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionInstance.h" // WTrigger
#include "../world/World.h"             // fgWorld, ProcAnimState
#include "../platform/CRuntime.h"

// ---------------------------------------------------------------------------------------------------------------
// Smackable (0x00075300-0x00075c40), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals
// XBE_GLOBAL(0x001e9294, 0x2c)
PhysicsData DummyPhysicsData;

// XBE_GLOBAL(0x001e92c0, 4)
uint32_t DummyPhysicsDataMade;

namespace {

const Coord3 kZero = {0.0f, 0.0f, 0.0f};

// ApplyDamage's kinds (as PBondCar's)
enum SmackableDamageKind : int32_t {
    kDamageBullet = 2,              // pushes it
    kDamageKind3 = 3,
};

// The stimuli ApplyDamage sends its animations (neutral names); every one queued (mode 2)
enum SmackableStimulus : uint8_t {
    kStimulusHit02 = 0x02,
    kStimulusImpact04 = 0x04,       // a hit's damage above DamageLevels::impact[2]
    kStimulusImpact05 = 0x05,       // ... impact[1]
    kStimulusImpact06 = 0x06,       // ... impact[0]; also the kill's
    kStimulusKill07 = 0x07,         // the kill's, for kind 3
    kStimulusStage13 = 0x13,        // the damage stages 1 to 4: 0x13 to 0x16
    kStimulusStage14 = 0x14,
    kStimulusStage15 = 0x15,
    kStimulusStage16 = 0x16,
    kStimulusImpact19 = 0x19,       // ... impact[3]
};

constexpr int kStimulusMode = 2;
constexpr uint16_t kAllZones = 0xffff;
constexpr int kDamageStages = 4;
constexpr int kRigidKind = 4;               // its rigid body's kind
constexpr int kDamageDone = 0x10;
constexpr float kMovingSpeed = 0.1f;
constexpr float kMoveSeconds = 10.0f;
constexpr float kBulletPushLimit = 10.0f;   // a bullet's damage up to it pushes
constexpr float kBulletPush = 1600.0f;      // the push per point of damage
constexpr float kLiftShare = 0.1f;          // the push's least upward share
constexpr float kHitScale = 32.0f;          // a hit's damage per hit point, scaled into the impact levels
constexpr float kDamageByteScale = 1.0f / 256.0f;
constexpr float kFullHealthImpact = 0.9f;   // PlayCollision's impact: at full health or killed
constexpr float kDamagedImpact = 0.5f;      // ... more than half the hit points lost
constexpr float kLightImpact = 0.1f;        // ... less
constexpr int kCollisionSurface = 0xf;      // PlayCollision's surface

// RSceneObj::TriggerFX through the render object's vtable (slot 15)
void TriggerFX(RSceneObj *render, int type, uint32_t which) {
    typedef void (RSceneObj::*TriggerFXMethod)(int type, uint32_t which, uint32_t unused, const Coord3 *position,
                                               const Coord3 *direction, float intensity);
    (render->*XbeVirtual<TriggerFXMethod>(render, 15))(type, which, 0, &kZero, &kZero, -1.0f);
}

bool IsStillActive(ABaseSound *sound) {                         // ASceneObj's vtable slot 6
    typedef bool (ABaseSound::*IsStillActiveMethod)();
    return (sound->*XbeVirtual<IsStillActiveMethod>(sound, 6))();
}

void PlayCollision(ABaseSound *sound, float impact, const Coord3 *position, const Coord3 *velocity,
                   int surface) {                               // slot 5
    typedef void (ABaseSound::*PlayCollisionMethod)(float impact, const Coord3 *position, const Coord3 *velocity,
                                                    int surface);
    (sound->*XbeVirtual<PlayCollisionMethod>(sound, 5))(impact, position, velocity, surface);
}

// The stimulus of a hit's damage (a share of the hit points, scaled), if above the least impact level: the first
// test on the value before it is rounded, the others on the float
void SendImpactStimulus(Handle *animation, uint32_t step, double unrounded) {
    float hit = float(unrounded);
    uint8_t stimulus;
    if (unrounded > fgDamageLevels.impact[0])
        stimulus = kStimulusImpact06;
    else if (hit > fgDamageLevels.impact[1])
        stimulus = kStimulusImpact05;
    else if (hit > fgDamageLevels.impact[2])
        stimulus = kStimulusImpact04;
    else if (hit > fgDamageLevels.impact[3])
        stimulus = kStimulusImpact19;
    else
        return;
    animation->ProcessStimuliZones(stimulus, kAllZones, step, kStimulusMode);
}

// The stimulus of the damage's stage, and the damage effects
void SendStageStimuli(RSceneObj *render, Handle *animation, uint32_t step, float damage) {
    for (uint32_t stage = kDamageStages; stage > 0; stage--) {
        if (damage >= fgDamageLevels.zone[stage]) {
            animation->ProcessStimuliZones(kStimulusStage13 + stage - 1, kAllZones, step, kStimulusMode);
            break;
        }
    }
    if (damage > fgDamageLevels.zone[1])
        TriggerFX(render, RSceneObj::kFXDamage, kAllZones);
    if (damage > fgDamageLevels.effects)
        TriggerFX(render, RSceneObj::kFXDamageEffects, kAllZones);
}

// The article's "smackable" PhysicsData, if it has one
PhysicsData *ArticlePhysicsData(RSceneObj *render) {
    return PtrFrom32<PhysicsData>(render->Desc()->physicsAttributes.value);
}

} // namespace

// ---- construction

// FUNC_AT(0x00075920)
Smackable* Smackable::Construct(RSceneObj *render, WTrigger *trigger, const Coord3 *velocity,
                                uint32_t sleepDelay) {
    PhysicsData *articleData = ArticlePhysicsData(render);
    if (articleData != NULL) {
        PhysicsObject::Construct(articleData->attributes, kObjectSmackable);
    } else {
        AttributeSet defaults;
        defaults.Construct("smackable", "default");
        PhysicsObject::Construct(defaults, kObjectSmackable);
        defaults.Destruct();
    }
    this->sleepDelay = sleepDelay;
    vtable = kSmackableVtable;
    moveCountdown = Ftol(double(fgSimulation.stepsPerSecond) * kMoveSeconds);
    this->trigger = trigger;
    data = ArticlePhysicsData(render);
    if (data == NULL) {
        // A function-local static in the original
        if ((DummyPhysicsDataMade & 1) == 0) {
            DummyPhysicsDataMade |= 1;
            DummyPhysicsData.Construct("Dummy");
            Crt::atexit(DestroyStatic_001e9294);
        }
        data = &DummyPhysicsData;
    }

    procAnimState = fgWorld->GetProcAnimStateFromInstance(render->Instance());
    hitPoints = data->hitPoints;
    if (procAnimState != NULL)
        hitPoints = float(hitPoints - double(procAnimState->damage) * data->hitPoints * kDamageByteScale);
    SetHitPointLoc(&hitPoints);

    ASmackable *block = static_cast<ASmackable *>(ABaseSound::OperatorNew(sizeof(ASmackable), "ASmackable"));
    sound = block != NULL ? block->Construct(data->impactSoundLo, data->impactSoundMed, data->impactSoundHi,
                                             data->scrapeSound, "Smackables")
                          : NULL;
    SetAudioObject(sound);
    SetRenderObject(render);

    MATRIX4 orientation = render->Instance()->Matrix();
    orientation.mtx[0][3] = 0.0f;
    orientation.mtx[1][3] = 0.0f;
    orientation.mtx[2][3] = 0.0f;
    orientation.mtx[3][3] = 1.0f;
    Coord4 halfExtents;
    render->GetBoundingDimensions(&halfExtents);
    float mass = data->mass;
    Coord3 tensor;
    Coord3 inverseInertia = *Util_GenerateTensor(&tensor, mass, halfExtents.x, halfExtents.y, halfExtents.z);
    MATRIX4 rotation = orientation;
    bool moving = VU0_v3lengthsquare(velocity) > 0.0f;
    bool detach;
    if (trigger == NULL)
        detach = data->forceDetach != 0;
    else
        detach = (trigger->flags & WTrigger::kFlag100) && data->forceDetach;

    Coord3 position = {orientation.mtx[3][0], orientation.mtx[3][1], orientation.mtx[3][2]};
    RigidBody *body = fgSimulation.GetRigidBody(rigidBodySlot);
    if (body != NULL)
        body->Construct(int8_t(rigidBodySlot), kRigidKind, &position, velocity, &kZero, &rotation, mass,
                        &inverseInertia, &halfExtents, this, moving, detach);
    return this;
}

// FUNC_AT(0x00075300)
void Smackable::Destruct() {
    vtable = kSmackableVtable;
    sound->Detatch();
    SetAudioObject(NULL);
    if (trigger != NULL)
        SetRenderObject(NULL);
    PhysicsObject::Destruct();
}

// FUNC_AT(0x00075530)
Smackable* Smackable::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(Smackable));
    return this;
}

// ---- resting

// FUNC_AT(0x00075370)
void Smackable::GoToSleep() {
    RSceneObj *render = renderObject;
    if (trigger != NULL) {
        RigidBody *body = fgSimulation.GetRigidBody(rigidBodySlot);
        bool placed;
        if (body->flags & RigidBody::kFlag1) {
            trigger->flags |= WTrigger::kFlag100;
            placed = true;
        } else {
            trigger->flags &= ~WTrigger::kFlag100;
            placed = trigger->UpdateRotPos(&body->orientation, &body->position);
        }
        if (placed) {
            trigger->flags |= WTrigger::kEnabled;
            // The damage as a byte: the lost hit points in 256ths of the most, rounded up, negated
            uint32_t most = Ftol(data->hitPoints);
            uint32_t left = Ftol(hitPoints);
            procAnimState->damage = uint8_t(-uint8_t((left + most - 1) * 256 / most));
            fgSimulation.DeletePhysicsObject(this);
            return;
        }
        render->Hide();
    }
    fgSimulation.DeletePhysicsObject(this);
}

// FUNC_AT(0x00075420)
void Smackable::Simulate() {
    ABaseSound *audio = audioObject;
    if (audio != NULL) {
        audio->position = *GetPosition();
        audioObject->velocity = *GetLinearVelocity();
    }
    if (VU0_v3length(&fgSimulation.GetRigidBody(rigidBodySlot)->velocity) > kMovingSpeed)
        moveCountdown = Ftol(double(fgSimulation.stepsPerSecond) * kMoveSeconds);
    if (moveCountdown != 0)
        moveCountdown--;
    bool animating = renderObject != NULL ? renderObject->animHandle->AnySystemPlaying() : false;
    bool sounding = audioObject != NULL ? IsStillActive(audioObject) : false;
    if (fgSimulation.GetRigidBody(rigidBodySlot)->sleepState != RigidBody::kAwake && !animating &&
        !sounding) {
        if (sleepDelay > 0)
            sleepDelay--;
        else
            GoToSleep();
    }
    PhysicsObject::Simulate();
}

// ---- damage

// FUNC_AT(0x00075560)
int Smackable::ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float, int kind,
                           const uint32_t *) {
    bool fullHealth = hitPoints == data->hitPoints;
    bool killed = false;
    ABaseSound *audio = audioObject;
    RSceneObj *render = renderObject;
    uint32_t step = fgSimulation.stepCount;
    Handle *animation = render->animHandle;
    if (!(GetHitPoints() > 0.0f)) {
        killed = true;
    } else {
        LoseHitPoints(amount);
        if (GetHitPoints() <= 0.0f)
            killed = true;
    }

    if (kind == kDamageBullet && amount <= kBulletPushLimit) {
        // A push along the shot, lifting it by at least a tenth of the push
        float push = amount * kBulletPush;
        Coord4 force;
        VU0_v4sub(to, from, &force);
        VU0_v4unitxyz(&force, &force);
        VU0_v4scale(&force, push, &force);
        double lift = double(push) * kLiftShare;
        if (force.y > lift)
            lift = force.y;
        force.y = float(lift + lift);
        Coord3 torque = {0.0f, 0.0f, 0.0f};
        ApplyForces(Xyz(&force), &torque);
    }

    float impact;
    if (killed) {
        animation->ProcessStimuli(kStimulusHit02, step, kStimulusMode);
        animation->ProcessStimuli(kind == kDamageKind3 ? kStimulusKill07 : kStimulusImpact06, step,
                                  kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage16, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage15, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage14, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage13, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusImpact06, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusImpact05, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusImpact04, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusImpact19, kAllZones, step, kStimulusMode);
        TriggerFX(render, RSceneObj::kFXDamage, kAllZones);
        impact = kFullHealthImpact;
    } else {
        // The hit's damage and the damage so far, as shares of the hit points (the hit's from the reciprocal before
        // it is rounded)
        double perHitPoint = 1.0 / data->hitPoints;
        float inverse = float(perHitPoint);
        SendImpactStimulus(animation, step, perHitPoint * amount * kHitScale);
        float lost = float((double(data->hitPoints) - GetHitPoints()) * inverse);
        SendStageStimuli(render, animation, step, lost);
        if (fullHealth)
            impact = kFullHealthImpact;
        else if (lost > fgDamageLevels.impact[1])
            impact = kDamagedImpact;
        else
            impact = kLightImpact;
    }

    if (audio != NULL) {
        Coord3 *velocity = GetLinearVelocity();
        Coord3 *position = GetPosition();
        PlayCollision(audio, impact, position, velocity, kCollisionSurface);
    }
    return kDamageDone;
}
