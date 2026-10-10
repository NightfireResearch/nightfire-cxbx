#pragma fp_contract(off)

#include "Helicopter.h"

#include <bit>
#include <math.h>

#include "Missile.h"
#include "VehicleSound.h"               // AHelicopter
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../anim/AnimEngine.h"         // Handle
#include "../audio/Sound.h"             // ABaseSound
#include "../data/Carp.h"               // CARP::BaseDesc
#include "../engine/ActionQueue.hpp"
#include "../engine/MissionManager.h"
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/SimpleRigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"

// ---------------------------------------------------------------------------------------------------------------
// PHelicopter (0x0006d8f0-0x0006e330 and 0x0006e330-0x0006ec80), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define RVehicle_Construct ((RSceneObj *(__fastcall *)(void *, int, void *unknown))0x000953e0)
#define AHelicopter_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *name))0x0012c7f0)
#define AIElementController_ForceVehicleToSleep ((void (__fastcall *)(void *, int, HelicopterAI *vehicle))0x000289f0)
#define AIHelicopter_DisableTargetBeacon ((void (__fastcall *)(HelicopterAI *, int))0x000318d0)
#define SMissionManager_IncShotsHit ((void (__fastcall *)(SMissionManager *, int, bool hit))0x000b6720)
#define SMissionManager_IncKills ((void (__fastcall *)(SMissionManager *, int, int kills))0x000b6770)
#define SMissionManager_ProgrammerDefinedEvent ((void (__fastcall *)(SMissionManager *, int, int event, const char *text))0x000b72f0)

// ---- globals
#define Sim ((void *)0x00233ff0)                                    // the Simulation
#define SimulationRandom (*(SimRandom **)0x00233ff0)                // the Simulation's first word
#define SimStepCount I32_AT(0x00234e34)
#define fgAIElementController PTR_AT(0x001ddee4)
#define ZeroVector (*(const Coord3 *)0x00243030)                    // the game's zero vector, never written
constexpr uint32_t kZeroVectorAddress = 0x00243030;

static_assert(sizeof(ActionQueue) == 0x974, "an action queue is 0x974 bytes");

namespace {

// The stimuli ApplyDamage sends its animations
enum HelicopterStimulus : uint8_t {
    kStimulusHit02 = 0x02,          // the whole body hit (kind 0)
    kStimulusImpact04 = 0x04,       // a hit's damage above DamageLevels::impact[2]
    kStimulusImpact05 = 0x05,       // ... impact[1]
    kStimulusImpact06 = 0x06,       // ... impact[0]
    kStimulusStart08 = 0x08,        // the constructor's, to system 0x10
    kStimulusStage13 = 0x13,        // the zones at damage stage 1; 0x14 and 0x15 stages 2 and 3
    kStimulusKilled16 = 0x16,
    kStimulusImpact19 = 0x19,       // ... impact[3]
};

constexpr uint32_t kStartSystem = 0x10;
constexpr int kStimulusMode = 2;
constexpr int kMissionEvent17 = 0x17;
constexpr uint32_t kRotorEffect = 0x20000;          // TriggerFX's effect bits (names ours)
constexpr uint32_t kBlinkEffect = 0x40000;          // on and off every 32 steps
constexpr uint32_t kSpawnEffect = 0x2000000;
constexpr uint32_t kAllZones = 0xffff;
constexpr uint32_t kRandomDamageZones = 0x3ca5;     // ApplyDamage answers 0x70 half the time in these
constexpr float kGasSpeed = 50.0f;
constexpr float kBrakeScale = -3.5f;
constexpr float kGravity = -9.8f;
constexpr float kEase = 0.05f;
constexpr float kRollScale = 0.7f;
constexpr float kStrafeTiltScale = 0.007f;
constexpr float kSteerScale = 0.015f;
constexpr float kDeadZone = 0.2f;
constexpr float kSpreadScale = 0.2f;                // a hit's damage in the zones it spreads to
constexpr float kOneOver65536 = 1.0f / 65536.0f;
static_assert(std::bit_cast<uint32_t>(kGravity) == 0xc11ccccd && std::bit_cast<uint32_t>(kEase) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(kRollScale) == 0x3f333333 &&
              std::bit_cast<uint32_t>(kStrafeTiltScale) == 0x3be56042 &&
              std::bit_cast<uint32_t>(kSteerScale) == 0x3c75c28f && std::bit_cast<uint32_t>(kDeadZone) == 0x3e4ccccd &&
              std::bit_cast<uint32_t>(0.1f) == 0x3dcccccd && std::bit_cast<uint32_t>(kOneOver65536) == 0x37800000,
              "the original's constants");

// -FSIN(x * scale) as the x87 computes it (FSIN's result is rounded only by the caller's store)
__declspec(naked) float NegativeSine(double x, float scale) {
    __asm {
        fld qword ptr [esp + 4]
        fmul dword ptr [esp + 12]
        fsin
        fchs
        ret
    }
}

Coord3 *Xyz(Coord4 *v) {
    return reinterpret_cast<Coord3 *>(v);
}

// RSceneObj::TriggerFX through the render object's vtable (slot 15)
void TriggerFX(RSceneObj *render, int type, uint32_t which, uint32_t unused3 = 0) {
    (render->*XbeVirtual<decltype(&RSceneObj::TriggerFX)>(render, 15))(type, which, unused3, kZeroVectorAddress,
                                                                       kZeroVectorAddress, -1.0f);
}

} // namespace

// ---- construction

// FUNC_AT(0x0006d8f0)
PHelicopter* PHelicopter::Construct(const char *name, float speed, Coord3 direction, Coord3 position) {
    PhysicsObject::Construct("pvehicle", name, 10, 0, true, false);
    targetPos = position;
    destPos = position;
    Coord3 velocity = direction;
    vtable = reinterpret_cast<void **>(kPHelicopterVtable);
    heliClass = 1;
    roll = 0.0f;
    unknown74 = 0.0f;
    unknown78 = 1.0f;
    damageScale = -1.0f;
    ai = NULL;
    scoreable = true;
    proximityDestructEnabled = 0;
    destructDistance = 2.0f;
    v3scale(1, &velocity, speed, &velocity);
    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, &direction);
    actionQueue = NULL;
    unknownA8 = 0;
    controlGas = 0.0f;
    controlStrafe = 0.0f;
    controlSteer = 0.0f;
    controlAltitude = 0.0f;
    sleepStep = 0;
    for (int i = 0; i < kDamageZoneCount; i++) {
        damageZones[i].unknown04 = 0.0f;
        damageZones[i].unknown00 = 0.0f;
    }

    void *block = UMemory::FastAlloc(0x370, "RVehicle");
    SetRenderObject(block != NULL ? RVehicle_Construct(block, 0, NULL) : NULL);
    const char *label = attributes.Name();
    renderObject->Load("data\\car\\model\\", PVehicle::RenderNameAttrib(&attributes), label, 0);
    renderObject->renderTypeIndex = PVehicle::NameToIndex(name);
    proximityDestructEnabled = attributes.LookupBool("PROXIMITY_DESTRUCT", NULL);
    destructDistance = attributes.LookupFloat("DESTRUCT_DIST", NULL);

    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleHelicopter, &position, &velocity, &ZeroVector, &orientation, 3.0f,
                        500.0f);
    if (!proximityDestructEnabled)
        renderObject->ScaleBoundingRadius(3.0f);
    Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags |=
        SimpleRigidBody::kMoves | SimpleRigidBody::kTouchesTriggers;
    TriggerFX(renderObject, RSceneObj::kFXOn, kSpawnEffect, SimStepCount);
    renderObject->animHandle->ProcessStimuli(kStartSystem, kStimulusStart08, SimStepCount, kStimulusMode);

    void *sound = ABaseSound::OperatorNew(sizeof(AHelicopter), "AHelicopter");
    SetAudioObject(sound != NULL ? AHelicopter_Construct(sound, 0, "Helicopter") : NULL);
    AHelicopter *audio = static_cast<AHelicopter *>(audioObject);
    audio->randomPhase = float(double(int32_t(SimulationRandom->Generate())) * 0.1f * kOneOver65536);
    ai = NULL;
    return this;
}

// FUNC_AT(0x0006e290)
void PHelicopter::Destruct() {
    vtable = reinterpret_cast<void **>(kPHelicopterVtable);
    ActionQueue *queue = actionQueue;
    if (queue != NULL) {
        queue->Destruct();
        UMemory::FastFree(queue, sizeof(ActionQueue));
    }
    PhysicsObject::Destruct();
}

// FUNC_AT(0x0006e300)
PHelicopter* PHelicopter::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(PHelicopter));
    return this;
}

// ---- flying

// FUNC_AT(0x0006dc30)
void PHelicopter::GetControllerInput() {
    while (!actionQueue->IsEmpty()) {
        ActionRef ref;
        actionQueue->GetAction(&ref);
        ActionData *action = ref.data;
        float value = 0.0f;
        if (action != NULL) {
            value = action->value;
            if (value > -kDeadZone && value < kDeadZone)
                value = 0.0f;
        }
        switch (action != NULL ? action->action : 0) {
        case kDebugAxisX:
            controlStrafe = value;
            break;
        case kDebugAxisY:
            controlGas = value * -1.0f;
            break;
        case kDebugAxisRX:
            controlSteer = value;
            break;
        case kDebugAxisRY:
            controlAltitude = value * -1.0f;
            break;
        }
        actionQueue->PopAction();
    }
}

// FUNC_AT(0x0006dd10)
DamageZone* PHelicopter::GetDamageZones(uint32_t *count) {
    *count = kDamageZoneCount;
    return damageZones;
}

// FUNC_AT(0x0006dd30)
void PHelicopter::Simulate() {
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    if (sleepStep != 0 && SimStepCount > sleepStep) {
        AIElementController_ForceVehicleToSleep(fgAIElementController, 0, ai);
        body->velocity = ZeroVector;
    }

    if (unknownA8 == 1) {
        if (heliClass == 0)
            GetControllerInput();
        Coord4 acceleration = {0.0f, 0.0f, 0.0f, 0.0f};
        Coord4 forward = {};
        body->GetForwardVector(Xyz(&forward));
        Coord4 up = {0.0f, 1.0f, 0.0f, 0.0f};
        forward.y = 0.0f;
        VU0_v4unitxyz(&forward, &forward);
        Coord4 right = {};
        VU0_v4crossprodxyz(&up, &forward, &right);
        Coord4 velocity = {body->velocity.x, body->velocity.y, body->velocity.z, 0.0f};
        Coord4 heading = {};
        VU0_v4unitxyz(&velocity, &heading);
        float along = v3dotprod(&heading, &forward);

        // Thrust towards the speed the gas asks for; braking past it, or against the way it is going
        float speed = body->GetScalarVelocity() * along;
        float target = controlGas * kGasSpeed;
        float thrust = float(fabs(double(speed) - target) * controlGas);
        if (target == 0.0f || (double(target) * speed > 0.0 && fabsf(target) < fabsf(speed)) ||
            (speed > 5.0f && target < 0.0f) || (speed < -5.0f && target > 0.0f))
            thrust = speed * kBrakeScale;

        VU0_v4unitxyz(&forward, &forward);
        Coord4 step = {};
        VU0_v4scale(&forward, thrust, &step);
        VU0_v3add(&acceleration, &step, &acceleration);
        v3dotprod(&heading, &right);    // computed and not used
        VU0_v4scale(&right, controlStrafe, &step);
        VU0_v3add(&acceleration, &step, &acceleration);
        Coord3 gravity = {0.0f, kGravity, 0.0f};
        body->Accelerate(&gravity);
        VU0_v4scale(&up, controlAltitude, &step);
        VU0_v3add(&acceleration, &step, &acceleration);
        body->Accelerate(Xyz(&acceleration));

        // Level the body, tilted by the eased gas and strafe, then turned by the steering
        body->GetForwardVector(Xyz(&forward));
        forward.y = 0.0f;
        body->GetRightVector(Xyz(&right));
        right.y = 0.0f;
        Coord4 widened;
        Coord4 level = *Float_COORD3toCOORD4(&widened, Xyz(&forward));
        Coord4 side = *Float_COORD3toCOORD4(&widened, Xyz(&right));
        double easedRoll = (double(controlGas) - roll) * kEase + roll;
        roll = float(easedRoll);
        double eased74 = (double(controlStrafe) - unknown74) * kEase + unknown74;
        unknown74 = float(eased74);
        level.y = NegativeSine(easedRoll, kRollScale);
        side.y = NegativeSine(eased74, kStrafeTiltScale);
        MATRIX4 orientation = {};
        VU0_v4unitcrossprodxyz(&level, &side, orientation.mtx[1]);
        VU0_v4unitcrossprodxyz(&side, orientation.mtx[1], orientation.mtx[2]);
        VU0_v4crossprodxyz(orientation.mtx[1], orientation.mtx[2], orientation.mtx[0]);
        VU0_v4Init(orientation.mtx[3]);
        double yaw = double(controlSteer) * kSteerScale;
        if (yaw != 0.0) {
            MATRIX4 turn;
            VU0_MATRIX4setyrot(&turn, float(yaw));
            VU0_MATRIX4_mult(&orientation, &turn, &orientation);
        }
        Coord4 quaternion;
        VU0_m4toquat(&quaternion, &orientation);
        body->SetOrientation(&quaternion);
    }

    audioObject->position = body->position;
    audioObject->velocity = body->velocity;
    TriggerFX(renderObject, RSceneObj::kFXOn, kRotorEffect);
    TriggerFX(renderObject, (SimStepCount & 0x20) ? RSceneObj::kFXOn : RSceneObj::kFXOff, kBlinkEffect);
    PhysicsObject::Simulate();
}

// ---- damage

// FUNC_AT(0x0006e330)
int PHelicopter::ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                             const uint32_t *sourceSig) {
    uint32_t step = SimStepCount;
    RSceneObj *render = renderObject;
    CARP::BaseDesc *desc = render->Desc();
    Handle *animation = render->animHandle;
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    Coord4 halfExtents;
    render->GetBoundingDimensions(&halfExtents);
    float renderOffset = float(render->GetRenderOffsetVirtual());

    // The segment in the body's frame, against its box
    Coord4 start = {from->x, from->y, from->z, 0.0f};
    Coord4 end = {to->x, to->y, to->z, 0.0f};
    Coord4 position = {body->position.x, body->position.y, body->position.z, 0.0f};
    VU0_v4sub(&start, &position, &start);
    VU0_v4sub(&end, &position, &end);
    MATRIX4 rotation;
    VU0_quattom4(&rotation, &body->orientation);
    MATRIX4 inverse;
    VU0_MATRIX4_transpose(&inverse, &rotation);
    VU0_MATRIX4_vect3mult(&start, &inverse, &start);
    VU0_MATRIX4_vect3mult(&end, &inverse, &end);
    Coord4 hit;
    if (!FindOBBIntersect(&halfExtents, &start, &end, &hit))
        return 0;

    bool byPlayer = false;
    PhysicsObject *source = Simulation_FindPhysicsObjectSignature(Sim, 0, *sourceSig);
    if (source != NULL && source->IsOwnedBy(Simulation_GetPlayerObject(Sim, 0)))
        byPlayer = true;
    bool scoring = scoreable;
    if (!byPlayer && kind == 2)
        scoring = false;
    else if (scoring && amount >= 5.0f)
        SMissionManager_IncShotsHit(glbMissionManager, 0, kind == 2);

    if (damageScale < 0.0f)
        damageScale = 32.0f / GetHitPoints();
    hit.y = float(double(hit.y) * 0.5f + renderOffset);
    int zone = desc->CalcDamageZone(&hit.x);

    if (GetHitPoints() <= 0.0f) {
        if (kind == 1)
            sleepStep = SimStepCount;
    } else {
        LoseHitPoints(amount);
        if (GetHitPoints() <= 0.0f) {
            if (scoring) {
                SMissionManager_IncKills(glbMissionManager, 0, 1);
                WTargetable *beacon = ai->targetBeacon;
                if (beacon != NULL) {
                    for (Missile **missile = SimulationMissilesFirst; missile != SimulationMissilesLast; missile++) {
                        if ((*missile)->target == beacon && !(*missile)->unknown70A)
                            SMissionManager_IncShotsHit(glbMissionManager, 0, false);
                    }
                }
                scoreable = false;
            }
            ai->navigateMode = 1;
            ai->attackMode = 0x10;
            AIHelicopter_DisableTargetBeacon(ai, 0);
            animation->ProcessStimuliZones(kStimulusKilled16, kAllZones, step, kStimulusMode);
        }
    }

    float damage = float((1.0 - split) * amount * damageScale);
    float splitDamage = float(double(amount) * split * damageScale);
    if (kind == 0) {
        // Every zone takes the whole hit
        SMissionManager_ProgrammerDefinedEvent(glbMissionManager, 0, kMissionEvent17, "");
        animation->ProcessStimuli(kStimulusHit02, step, kStimulusMode);
        TriggerFX(render, RSceneObj::kFXDamage, kAllZones, kind);
        for (int i = 0; i < kDamageZoneCount; i++) {
            float first = damage + damageZones[i].unknown00;
            damageZones[i].unknown00 = first < 1.0f ? first : 1.0f;
            float second = splitDamage + damageZones[i].unknown04;
            damageZones[i].unknown04 = second < 2.0f ? second : 2.0f;
        }
        audioObject->volume = 0.0f;
    } else {
        // The zone hit, and the zones it spreads to at a fifth
        float total = splitDamage + damage;
        uint32_t zones = 1u << zone;
        bool impact = true;
        uint8_t stimulus = 0;
        if (total > fgDamageLevels.impact[0])
            stimulus = kStimulusImpact06;
        else if (total > fgDamageLevels.impact[1])
            stimulus = kStimulusImpact05;
        else if (total > fgDamageLevels.impact[2])
            stimulus = kStimulusImpact04;
        else if (total > fgDamageLevels.impact[3])
            stimulus = kStimulusImpact19;
        else
            impact = false;
        if (kind == 3 ||
            double(damageZones[zone].unknown04) + damageZones[zone].unknown00 > fgDamageLevels.spread)
            zones |= desc->GetZoneBits(zone);

        uint32_t stageZones[4] = {0, 0, 0, 0};
        uint32_t damagedZones = 0;
        uint32_t effectZones = 0;
        uint32_t hitZones = 0;
        for (int i = 0; i < kDamageZoneCount; i++) {
            DamageZone &z = damageZones[i];
            uint32_t bit = 1u << i;
            float worst;
            if (zones & bit) {
                float scale = i == zone ? 1.0f : kSpreadScale;
                double first = double(damage) * scale + z.unknown00;
                z.unknown00 = first < 1.0 ? float(first) : 1.0f;
                double second = double(splitDamage) * scale + z.unknown04;
                z.unknown04 = second < 1.0 ? float(second) : 1.0f;
                hitZones |= bit;
                worst = z.unknown00 < z.unknown04 ? z.unknown04 : z.unknown00;
                for (int stage = 4; stage > 0; stage--) {
                    if (worst >= fgDamageLevels.zone[stage]) {
                        stageZones[stage - 1] |= bit;
                        break;
                    }
                }
            } else {
                worst = z.unknown00 < z.unknown04 ? z.unknown04 : z.unknown00;
            }
            if (worst > fgDamageLevels.zone[1])
                damagedZones |= bit;
            if (worst > fgDamageLevels.effects)
                effectZones |= bit;
        }

        // Stages 1 to 3 only: stage 4's zones are gathered but not sent
        for (int stage = 0; stage < 3; stage++) {
            if (stageZones[stage] != 0)
                animation->ProcessStimuliZones(kStimulusStage13 + stage, stageZones[stage], step, kStimulusMode);
        }
        if (impact && hitZones != 0)
            animation->ProcessStimuliZones(stimulus, hitZones, step, kStimulusMode);
        if (damagedZones != 0)
            TriggerFX(render, RSceneObj::kFXDamage, damagedZones);
        if (effectZones != 0)
            TriggerFX(render, RSceneObj::kFXDamageEffects, effectZones);
    }

    uint32_t random = SimulationRandom->Generate();
    if (((1u << zone) & kRandomDamageZones) && int32_t(random) * kOneOver65536 < 0.5f)
        return 0x70;
    return 0x10;
}
