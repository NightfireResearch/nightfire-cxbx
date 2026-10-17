#pragma fp_contract(off)

#include "AIElements.h"

#include <math.h>
#include <string.h>

#include "AICharacter.h"
#include "AIEnemy.h"                    // EnemyAnimBank
#include "AIEnemyGround.h"              // AICharacterEnemyGround
#include "AIEnemyRiders.h"              // the riders
#include "AIVehicle.h"                  // AIVehicle, AIGroundVehicle
#include "AIVehicleController.h"
#include "EventsA.h"                    // EAICommand
#include "Helicopter.h"                 // PHelicopter
#include "Human.h"
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../EventManager.hpp"          // Event::operator new
#include "../anim/Actor.h"              // ActActor, ActActorDatabase
#include "../anim/Character.h"          // ActCharacter::LoadCharacter
#include "../anim/Weapon.h"             // ActWeapon
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../data/AttributeSet.h"
#include "../data/UData.h"
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/CoreFoundation.h"   // AssertMessage
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../render/RSceneObj.hpp"      // RSceneObj::PreLoad
#include "../world/Targeting.h"         // PointerListNode
#include "../world/World.h"             // fgWorld
#include "../platform/CRuntime.h"

// XBE_GLOBAL(0x001b64ec, 0x1)
uint8_t gLimitHenchSpawns = 1;
// XBE_GLOBAL(0x001ddee4, 0x4)
AIElementController *fgAIElementController;
// XBE_GLOBAL(0x001e1ee8, 0x380)
AIZoneStatics AIZoneData;

// ---------------------------------------------------------------------------------------------------------------
// AIElementTracking and AIElementController (0x00027cb0-0x00029d60), AIZoneController (0x00036a00-0x00036c90), the
// road network's count copies (0x00023990, 0x000239b0) and RCamera's field of view setter (0x00011000), ported
// from the listing. No random numbers.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

// A new element's sleepRange (a static initialiser's 200 + 20)
// XBE_GLOBAL(0x001ddefc, 4)
float DefaultSleepRange = 220.0f;

namespace {

constexpr float kDefaultWakeRange = 200.0f;
constexpr float kSleepMargin = 20.0f;
constexpr int kMaxInterval = 4;                 // steps between a far element's looks
constexpr int kDefaultCharacterSlots = 32;
constexpr int kDefaultCarSlots = 6;
constexpr int kDefaultHelicopterSlots = 16;
constexpr int kCarClassFlag08 = 2;              // a car element's spawn class, with kElementTargetBeacon
constexpr int kCarClassElement = 4;             // ... without
constexpr int kTrafficCarClass = 3;
constexpr float kTrafficClearance = 6.0f;       // a traffic car this near a waking car's place (beyond its half
                                                // length) is unspawned
constexpr float kGroundSkillScale = 30.0f;      // kSkill3 and kSkill5 on a ground enemy's range squared
constexpr float kRiderSkillScale = 9.0f;        // ... on a rider's
constexpr float kSkill2Scale = 4.0f;
constexpr float kMinDistanceEvent = 120.0f;     // Update's event 1: no element nearer than this
constexpr int kElementEvent = 1;
constexpr size_t kEAICommandSize = sizeof(EAICommand);
constexpr int kFirstGroundAnimation = 31;       // AIElementCharacterInfo::animation: the table's first
constexpr int kLastGroundAnimation = 48;
constexpr int kGroundAnimationDefault = 30;
constexpr int kGroundAnimationBank = 5;
constexpr int kGroundWeapon2 = 2;

// The characters' descriptions and weapons, by AIElementCharacterInfo::description and ::weapon
const char *const kElementDescriptions[33] = {
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_pmask.h_pmask.t_paris.95",
    "2.h_pmask.h_pmask.t_paris.95", "2.h_pmask.h_pmask.t_paris.95", "2.h_pmask.h_pmask.t_paris.95",
    "2.h_snow.h_snow.t_snow.100", "2.h_snowt.h_snowt.t_snow.100", "2.h_snowt.h_snowt1.t_snow.100",
    "2.h_jguya.h_jguya.t_jguya.100", "2.h_jguyb.h_jguyb.t_jguyb.100", "2.h_jguyb.h_jguyc.t_jguyc.100",
    "2.h_julg.h_julg.t_julg.100", "2.h_julg.h_julg.t_julg.100", "3.c_domm.h_dommin.t_dommin.100",
    "2.h_jwrkr.h_jwrkr.t_jwrkr.100", "2.h_snow.h_snowh.t_snowh.100", "2.h_snowt.h_snowth.t_snowh.100",
};
const char *const kElementWeapons[5] = {NULL, "ssmg", "bazooka", NULL, NULL};

// The ground enemy's starting animations (bank 5), by AIElementCharacterInfo::animation from 31
const int32_t kGroundAnimations[kLastGroundAnimation - kFirstGroundAnimation + 1] = {
    2, 3, 0, 1, 4, 5, 0, 6, 7, 0, 12, 13, 8, 9, 14, 15, 10, 11,
};

// The descriptions whose characters' weapon has unknown164 4
const char kSnowHeavyDescription[] = ".t_snowh.";

PVehicle *Car(PhysicsObject *object) {
    return static_cast<PVehicle *>(object);
}

AIGroundVehicle *GroundVehicleOf(PhysicsObject *car) {
    return Car(car)->GetAIGroundVehiclePtr();
}

int NumTires(PhysicsObject *car) {
    return Car(car)->GetNumTires();
}

ActActor *ActorOf(PointerListNode *handle) {
    return static_cast<ActActor *>(handle->value);
}

// The first weapon of a character's actor (its handle's actor's character's)
ActWeapon *FirstWeaponOf(AICharacter *character) {
    return character->actorLink->actor->character->weapons[0];
}

const Coord3 &Row(const MATRIX4 &matrix, int row) {
    return *reinterpret_cast<const Coord3 *>(matrix.mtx[row]);
}

Coord4 RowOf(const MATRIX4 &matrix, int row) {
    return {matrix.mtx[row][0], matrix.mtx[row][1], matrix.mtx[row][2], 0.0f};
}

// The riders' actor callbacks: the car's orientation, position and velocity
ActActorTransformCallback RiderCallback() {
    typedef void (*Callback)(AICharacterEnemyRider *rider, MATRIX4 *transform, Coord4 *velocity);
    return reinterpret_cast<ActActorTransformCallback>(static_cast<Callback>(&AICharacterEnemyRider::GetRotPos));
}
ActActorTransformCallback SnowCallback() {
    typedef void (*Callback)(AICharacterEnemySnow *rider, MATRIX4 *transform, Coord4 *velocity);
    return reinterpret_cast<ActActorTransformCallback>(static_cast<Callback>(&AICharacterEnemySnow::GetRotPos));
}

// The character's SetActor through its vtable (slot 2: the enemies override it)
void SetActorOf(AICharacter *character, PointerListNode *handle) {
    typedef void (AICharacter::*Method)(AICharacterActorLink *actorLink);
    (character->*XbeVirtual<Method>(character, AICharacter::kSlotSetActor))(
        reinterpret_cast<AICharacterActorLink *>(handle));
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// AIElementTracking
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00027cb0)
void AIElementTracking::SetWakeRange(float range) {
    if (range > 0.0f) {
        wakeRange = range;
        sleepRange = kSleepMargin + range;
    }
}

// FUNC_AT(0x00027ce0)
bool AIElementTracking::GetProperty(const char *name) {
    if (strcmp(name, "EMPTY") == 0) {
        if (element->type != kAIElementVehicle)
            return false;
        if (element->memberCount <= 0)
            return true;
        double hitPoints = 0.0;
        for (int i = 0; i < element->memberCount; i++)
            hitPoints += element->members[i].hitPoints;
        return hitPoints == 0.0;
    }
    if (strcmp(name, "BLOWOUT") == 0) {
        if (element->type != kAIElementVehicle || element->state != kElementAwake || element->car.helicopter)
            return false;
        if (NumTires(element->object) >= 4)
            return false;
        if (element->memberCount <= 0)
            return false;
        for (int i = 0; i < element->memberCount; i++) {
            if (element->members[i].hitPoints <= 0.0f)
                return false;
        }
        return true;
    }
    if (strcmp(name, "SMOKED") == 0) {
        if (element->type != kAIElementVehicle || element->car.helicopter || element->state != kElementAwake ||
            element->object == NULL)
            return false;
        AIGroundVehicle *vehicle = GroundVehicleOf(element->object);
        return vehicle != NULL && vehicle->enteredZone;
    }
    if (strcmp(name, "OILED") == 0) {
        if (element->type != kAIElementVehicle || element->car.helicopter || element->state != kElementAwake ||
            element->object == NULL)
            return false;
        AIGroundVehicle *vehicle = GroundVehicleOf(element->object);
        return vehicle != NULL && vehicle->oiled;
    }
    if (strcmp(name, "AWAKE") == 0) {
        bool awake = element->state == kElementAwake;
        if (awake) {
            switch (element->type) {
            case kAIElementCharacter:
                if (!element->character->IsAlive())
                    awake = false;
                break;
            case kAIElementVehicle:
                if (element->object->GetHitPoints() <= 0.0f)
                    awake = false;
                break;
            }
        }
        return awake;
    }
    return false;
}

// FUNC_AT(0x00027f00)
void AIElementTracking::UpdatePosition(const Coord4 *player) {
    if (element->state == kElementAwake) {
        switch (element->type) {
        case kAIElementCharacter:
            position = element->character->Position();
            break;
        case kAIElementVehicle: {
            const Coord4 *at = element->vehicle->GetPosition();
            position = {at->x, at->y, at->z};
            break;
        }
        }
    } else {
        position = Row(element->transform, 3);
    }
    distance = vec3distance(&position, player);

    // How far ahead of the camera it lies: 1 straight ahead, 0 behind
    const float *axis = fgRenderHigh->views[0].camera->matrix.mtx[2];
    Coord4 forward = {axis[0], axis[1], axis[2], 0.0f};
    Coord4 toElement = {};
    VU0_v4sub(&position, player, &toElement);
    VU0_v4unitxyz(&toElement, &toElement);
    VU0_v4unitxyz(&forward, &forward);
    double facing = (v3dotprod(&forward, &toElement) + 1.0) * 0.5;
    if (distance > 0.0f) {
        double weighted = facing * (1.0 / distance);
        closeness = float(weighted);
        if (weighted < 0.0)
            closeness = 0.0f;
    } else {
        closeness = 1.0f;
    }

    if (element->state == kElementAwake) {
        switch (element->type) {
        case kAIElementCharacter:
            speed = VU0_v3length(element->person.human->GetLinearVelocity());
            break;
        case kAIElementVehicle:
            speed = VU0_v3length(element->object->GetLinearVelocity());
            break;
        }
    } else {
        speed = 0.0f;
    }

    float damage = lastHitPoints - element->hitPoints;
    lastHitPoints = element->hitPoints;
    if (element->state == kElementAwake && element->hitPoints > 0.0f) {
        AIElementStats &stats = controller->stats;
        if (element->hitPoints > stats.maxHitPoints)
            stats.maxHitPoints = element->hitPoints;
        if (element->hitPoints < stats.minHitPoints)
            stats.minHitPoints = element->hitPoints;
        if (damage > stats.maxDamage)
            stats.maxDamage = damage;
        if (speed > stats.maxSpeed)
            stats.maxSpeed = speed;
        if (speed < stats.minSpeed)
            stats.minSpeed = speed;
        if (distance > stats.maxDistance)
            stats.maxDistance = distance;
        if (distance < stats.minDistance)
            stats.minDistance = distance;
    }

    // A car with a flat tyre, smoked or oiled
    if (element->state == kElementAwake && element->type == kAIElementVehicle && !element->car.helicopter) {
        PhysicsObject *car = element->object;
        if (NumTires(car) < 4)
            element->flags |= kElementMightAsWellBeDead;
        AIGroundVehicle *vehicle = GroundVehicleOf(car);
        if (vehicle != NULL) {
            if (vehicle->oiled)
                element->flags |= kElementMightAsWellBeDead;
            if (vehicle->enteredZone)
                element->flags |= kElementMightAsWellBeDead;
        }
    }
}

// FUNC_AT(0x00028ab0)
void AIElementTracking::Update(const Coord4 *player) {
    if (element->state == kElementDead)
        return;
    int step = fgSimulation.stepCount;
    if (step < nextUpdate)
        return;

    bool beyondSleepRange = false;
    UpdatePosition(player);
    if (distance < wakeRange) {
        interval = 1;
        inRange = 1;
    } else {
        if (interval < kMaxInterval)
            interval++;
        inRange = 0;
        if (element->state == kElementAwake && distance > sleepRange)
            beyondSleepRange = true;
    }
    uint16_t flags = element->flags;
    if ((((flags & kElementDeactivated) && element->state == kElementAwake) || beyondSleepRange) &&
        !(flags & kElementForceAwake))
        controller->GoToSleep(element);
    nextUpdate = step + interval;
}

// ---------------------------------------------------------------------------------------------------------------
// AIElementController
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00028190)
AIElementController* AIElementController::Construct(UData *data) {
    count = data->count;
    elements = static_cast<AIElement *>(OperatorNewArray(count * sizeof(AIElement)));
    wakeList = static_cast<int32_t *>(OperatorNewArray(count * sizeof(int32_t)));
    rangeList = static_cast<AIElementRange *>(OperatorNewArray(count * sizeof(AIElementRange)));
    trackingBlock = static_cast<AIElementTracking *>(OperatorNewArray(count * sizeof(AIElementTracking)));
    tracking = trackingBlock;
    carpElements = reinterpret_cast<AIElement *>(data->Data());
    memcpy(elements, data->Data(), count * sizeof(AIElement));

    // On the hardest difficulty of snow2a_mis4, the paris_hench cars come as junglesnow, with no riders
    if (strcmp(fgWorld->trackName, "snow2a_mis4") == 0 && Launch.difficulty == 2) {
        for (int i = 0; i < count; i++) {
            AIElement &element = elements[i];
            if (element.type != kAIElementVehicle)
                continue;
            AttributeSet attributes;
            const char *renderName = PVehicle::RenderNameAttrib(PVehicle::GetNamedAttribs(&attributes,
                                                                                          element.car.name));
            attributes.Destruct();
            if (strncmp(renderName, "paris_hench", 11) == 0) {
                memcpy(element.car.name, "junglesnow", sizeof("junglesnow"));
                element.memberCount = 0;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        AIElement &element = elements[i];
        AIElementTracking &track = tracking[i];
        track.nextUpdate = fgSimulation.stepCount;
        track.interval = 1;
        track.distance = 0.0f;
        track.closeness = 0.0f;
        track.wakeRange = kDefaultWakeRange;
        track.sleepRange = DefaultSleepRange;
        track.inRange = 0;
        track.element = &element;
        track.controller = this;

        if (element.type == kAIElementVehicle) {
            AttributeSet attributes;
            const char *renderName = PVehicle::RenderNameAttrib(PVehicle::GetNamedAttribs(&attributes,
                                                                                          element.car.name));
            attributes.Destruct();
            if (renderName == NULL)
                AssertMessage("Could not find attribute rendername for %s", element.car.name);
            RSceneObj::PreLoad("data\\car\\model\\", renderName, element.car.name);
        }

        if (ActorDatabase != NULL) {
            if (element.type == kAIElementCharacter) {
                ActCharacter::LoadCharacter(kElementDescriptions[element.person.info.description],
                                            kElementWeapons[element.person.info.weapon], NULL);
            } else if (element.type == kAIElementVehicle) {
                for (int m = 0; m < element.memberCount; m++)
                    ActCharacter::LoadCharacter(kElementDescriptions[element.members[m].description],
                                                kElementWeapons[element.members[m].weapon], NULL);
            }
        }

        // The hit points scaled for the difficulty: the riders', then the vehicle's (or the character's)
        if (element.type == kAIElementCharacter) {
            element.hitPoints = glbMissionManager->ShowMissionText() * element.hitPoints;
        } else if (element.type == kAIElementVehicle) {
            for (int m = 0; m < element.memberCount; m++)
                element.members[m].hitPoints = glbMissionManager->ShowMissionText() *
                                               element.members[m].hitPoints;
            element.hitPoints = glbMissionManager->ShowMissionText() * element.hitPoints;
        }
    }

    bool found = false;
    characterSlots = fgWorld->attributes.LookupInt("HENCH_SLOTS_AVAIL", &found);
    if (!found)
        characterSlots = kDefaultCharacterSlots;
    carSlots = fgWorld->attributes.LookupInt("VEHICLE_SLOTS_AVAIL", &found);
    if (!found)
        carSlots = kDefaultCarSlots;
    helicopterSlots = fgWorld->attributes.LookupInt("HELI_SLOTS_AVAIL", &found);
    if (!found)
        helicopterSlots = kDefaultHelicopterSlots;
    awakeCharacters = 0;
    awakeCars = 0;
    awakeHelicopters = 0;
    cars = 0;
    mightAsWellBeDead = 0;
    return this;
}

// FUNC_AT(0x000285b0)
void AIElementController::Create(UData *data) {
    void *block = OperatorNew(sizeof(AIElementController));
    fgAIElementController = block != NULL ? static_cast<AIElementController *>(block)->Construct(data) : NULL;
}

// FUNC_AT(0x00028b60)
void AIElementController::Destroy() {
    AIElementController *controller = fgAIElementController;
    if (controller != NULL) {
        OperatorDelete(controller->elements);
        OperatorDelete(controller->wakeList);
        OperatorDelete(controller->rangeList);
        OperatorDelete(controller->trackingBlock);
        OperatorDelete(controller);
    }
    fgAIElementController = NULL;
}

// FUNC_AT(0x00028620)
AIElementStats* AIElementController::GetStats() {
    return &stats;
}

// FUNC_AT(0x00028630)
void AIElementController::EveryoneChill() {
    for (int i = 0; i < count; i++) {
        AIElement &element = elements[i];
        if (element.type != kAIElementVehicle || element.car.helicopter)
            continue;
        PhysicsObject *car = element.object;
        if (car == NULL)
            continue;
        Car(car)->SetCarControlGas(0.0f);
        Car(car)->SetCarControlBrake(0.0f);
        Car(car)->SetCarControlHandBrake(true);
        AIGroundVehicle *vehicle = GroundVehicleOf(car);
        if (vehicle != NULL) {
            vehicle->SetAttackMode(kGroundAttackMove);
            vehicle->SetNavigateMode(kGroundNavigateNone, NULL);
        }
    }
}

// FUNC_AT(0x000286b0)
bool AIElementController::OkToSpawnHench() {
    return cars < carSlots;
}

// The record's index is found from the one controller's records, made positive
// FUNC_AT(0x000286d0)
AIElement* AIElementController::RedirectCarpElement(AIElement *carpElement) {
    int index = int(reinterpret_cast<uint8_t *>(carpElement) -
                    reinterpret_cast<uint8_t *>(fgAIElementController->carpElements)) >> 8;
    if (index < 0)
        index = -index;
    return &elements[index];
}

// FUNC_AT(0x00028700)
AIElementTracking* AIElementController::GetCarpElementTracking(AIElement *carpElement) {
    int index = int(reinterpret_cast<uint8_t *>(carpElement) -
                    reinterpret_cast<uint8_t *>(fgAIElementController->carpElements)) >> 8;
    if (index < 0)
        index = -index;
    return &tracking[index];
}

// FUNC_AT(0x00028730)
void AIElementController::CalcMightAsWellBeDead() {
    int dead = 0;
    for (int i = 0; i < count; i++) {
        const AIElement &element = elements[i];
        if (element.type != kAIElementVehicle || !(element.flags & kElementMightAsWellBeDead) ||
            (element.flags & kElementNonScoreable) || !(element.hitPoints > 0.0f))
            continue;
        dead++;
        for (int m = 0; m < element.memberCount; m++) {
            if (element.members[m].hitPoints > 0.0f)
                dead++;
        }
    }
    mightAsWellBeDead = dead;
}

// FUNC_AT(0x000287c0)
int AIElementController::GetNumMightAsWellBeDead() {
    return mightAsWellBeDead;
}

// FUNC_AT(0x000287d0)
void AIElementController::GoToSleep(AIElement *element) {
    if (element->state != kElementAwake)
        return;

    switch (element->type) {
    case kAIElementCharacter:
        if (!(element->flags & kElementFlag01))
            element->transform = element->character->transform;
        element->state = element->character->IsAlive() ? kElementAsleep : kElementDead;
        awakeCharacters--;
        element->character = NULL;
        fgSimulation.DeletePhysicsObjectThunk(element->person.human);
        element->person.human = NULL;
        break;

    case kAIElementVehicle: {
        if (!(element->flags & kElementFlag01))
            element->transform = *element->vehicle->GetRotPos();
        bool intact = true;
        bool shredded = false;
        for (int m = 0; m < element->memberCount; m++) {
            if (element->memberCharacters[m] != NULL) {
                element->memberCharacters[m] = NULL;
                fgSimulation.DeletePhysicsObjectThunk(element->memberHumans[m]);
                element->memberHumans[m] = NULL;
                awakeCharacters--;
            }
        }
        float hitPoints;
        PhysicsObject *object = element->object;
        if (element->car.helicopter) {
            awakeHelicopters--;
            hitPoints = object->GetHitPoints();
            AIVehicleController::Get()->DeRegisterHelicopter(static_cast<PHelicopter *>(element->object));
            fgSimulation.DeletePhysicsObjectThunk(element->object);
        } else {
            awakeCars--;
            hitPoints = object->GetHitPoints();
            PVehicle *car = Car(element->object);
            intact = car->GetResetAvailable() == 0;
            shredded = car->IsTyreShredded(0) || car->IsTyreShredded(1) || car->IsTyreShredded(2) ||
                       car->IsTyreShredded(3);
            AIVehicleController::Get()->DeRegisterSimCar(car);
            fgSimulation.DeletePhysicsObjectThunk(element->object);
        }
        element->object = NULL;
        element->state = hitPoints > 0.0f && intact && !shredded ? kElementAsleep : kElementDead;
        element->vehicle = NULL;
        break;
    }
    }
}

// FUNC_AT(0x000289f0)
void AIElementController::ForceVehicleToSleep(AIVehicle *vehicle) {
    for (int i = 0; i < count; i++) {
        AIElement *element = &elements[i];
        if (element->state == kElementAwake && element->type == kAIElementVehicle && element->vehicle == vehicle) {
            GoToSleep(element);
            return;
        }
    }
}

// FUNC_AT(0x00028a40)
void AIElementController::SyncRuleInfo(AIElement *element) {
    if (element->state != kElementAwake)
        return;
    switch (element->type) {
    case kAIElementCharacter:
        if (!(element->flags & kElementFlag01))
            element->transform = element->character->transform;
        break;
    case kAIElementVehicle:
        if (!(element->flags & kElementFlag01))
            element->transform = *element->vehicle->GetRotPos();
        break;
    }
}

// The jump table at 0x0002983c (difficulty 1-6) and the byte table at 0x00029870 with its jump table at 0x00029854
// (seats 3-17; 5-13 make nothing) were decoded from their bytes.
// The binary's measured facts have ESI read on entry, reaching the riders' skill test through the seat table; ESI is
// set before the riders' loop, and the one caller hands `this` in ECX and ESI alike, so a plain method stands in
// (AUTOLTCG: placed by Ghidra's name, without the register check).
// AUTOLTCG
void AIElementController::WakeUp(AIElement *element) {
    if (element->state != kElementAsleep)
        return;

    int difficulty = glbMissionManager->difficulty;
    switch (element->difficulty) {
    case kElementHarder:
        if (difficulty != 1 && difficulty != 2)
            return;
        break;
    case kElementHardest:
        if (difficulty != 2)
            return;
        break;
    case kElementEasiest:
        if (difficulty != 0)
            return;
        break;
    case kElementMedium:
        if (difficulty != 1)
            return;
        break;
    case kElementEasier:
        if (difficulty != 0 && difficulty != 1)
            return;
        break;
    case kElementNotMedium:
        if (difficulty != 0 && difficulty != 2)
            return;
        break;
    }

    switch (element->type) {
    case kAIElementVehicle: {
        Coord4 direction = RowOf(element->transform, 2);
        Coord4 position = RowOf(element->transform, 3);
        // The riders' owner. A helicopter's riders get whatever the original's stack held there (here NULL).
        PhysicsObject *vehicleObject = NULL;
        if (element->car.helicopter) {
            PHelicopter *helicopter = static_cast<PHelicopter *>(
                fgSimulation.SpawnHelicopterObject(element->car.name, Xyz(&direction), Xyz(&position), 0));
            vehicleObject = helicopter;
            AIVehicleController::Get()->RegisterHelicopter(helicopter);
            element->object = helicopter;
            element->vehicle = reinterpret_cast<AIVehicle *>(helicopter->ai);
            element->state = kElementAwake;
            helicopter->SetHitPointLoc(&element->hitPoints);
            if (element->flags & kElementNonScoreable)
                helicopter->scoreable = false;
            if (element->command != NULL) {
                void *event = Event::operator new(kEAICommandSize);
                if (event != NULL)
                    static_cast<EAICommand *>(event)->Construct(element->command, NULL, NULL, element->vehicle, NULL, 1,
                                         helicopter->rigidBodySlot);
            }
            awakeHelicopters++;
        } else {
            // Traffic in the way unspawned
            for (PVehicle **traffic = fgSimulation.cars.first; traffic != fgSimulation.cars.last; traffic++) {
                PVehicle *car = *traffic;
                if (car->GetCarClass() != kTrafficCarClass)
                    continue;
                RigidBody *body = fgSimulation.GetRigidBody((*traffic)->rigidBodySlot);
                Coord4 at = {body->position.x, body->position.y, body->position.z, 0.0f};
                RigidBodyInfo *info = body->info;
                if (!(info->halfExtents.z + double(kTrafficClearance) < vec3distance(&at, &position)))
                    GroundVehicleOf(car)->UnSpawn();
            }
            int carClass = (element->flags & kElementTargetBeacon) ? kCarClassFlag08 : kCarClassElement;
            PhysicsObject *car = fgSimulation.SpawnCarObject(carClass, element->car.name, element->car.colour,
                                                           Xyz(&direction), Xyz(&position));
            vehicleObject = car;
            if (car == NULL)
                return;
            Car(car)->ResetCar(Xyz(&position), Xyz(&direction));
            AIVehicleController::Get()->RegisterSimCar(Car(car));
            car->SetHitPointLoc(&element->hitPoints);
            if (element->flags & kElementNonScoreable)
                Car(car)->SetScoreable(false);
            element->object = car;
            element->vehicle = GroundVehicleOf(car);
            element->state = kElementAwake;
            if (element->command != NULL) {
                void *event = Event::operator new(kEAICommandSize);
                if (event != NULL)
                    static_cast<EAICommand *>(event)->Construct(element->command, NULL, NULL, element->vehicle, NULL, 0,
                                         car->rigidBodySlot);
            }
            awakeCars++;
        }

        // The mission's enemy count, the first time
        if (!(element->flags & (kElementWoken | kElementNonScoreable))) {
            glbMissionManager->enemyCount++;
            for (int m = 0; m < element->memberCount; m++)
                glbMissionManager->enemyCount++;
        }
        element->flags |= kElementWoken;

        if (ActorDatabase == NULL)
            return;
        PBondCar *riderCar = static_cast<PBondCar *>(vehicleObject);    // a helicopter's riders get it too
        for (int m = 0; m < element->memberCount; m++) {
            AIElementCharacterInfo &member = element->members[m];
            if (member.hitPoints <= 0.0f)
                continue;

            MATRIX4 seat;
            VU0_MATRIX4Init(&seat);
            const AttributeVector *offset;
            void *block;
            PointerListNode *handle;
            AICharacter *rider;
            const char *description = kElementDescriptions[member.description];
            switch (member.kind) {
            case kSeatWindow:
                offset = vehicleObject->attributes.LookupVector("AIC_EWIND_POS", NULL);
                seat.mtx[3][0] = offset->x;
                seat.mtx[3][1] = offset->y;
                seat.mtx[3][2] = offset->z;
                seat.mtx[3][3] = offset->w;
                block = UMemory::FastAlloc(sizeof(AICharacterEnemyWindow), "AICharacter");
                rider = block != NULL ? static_cast<AICharacterEnemyWindow *>(block)->Construct(&seat, riderCar) : NULL;
                ActActorDatabase::GetNewActorHandle(&handle, rider, description,
                                                    kElementWeapons[member.weapon], RiderCallback(),
                                                    &seat, false);
                SetActorOf(rider, handle);
                ActorOf(handle)->SetNewAnimation(kBankWindow, 2);
                element->memberCharacters[m] = rider;
                awakeCharacters++;
                break;
            case kSeatSunroof:
                offset = vehicleObject->attributes.LookupVector("AIC_ESUNR_POS", NULL);
                seat.mtx[3][0] = offset->x;
                seat.mtx[3][1] = offset->y;
                seat.mtx[3][2] = offset->z;
                seat.mtx[3][3] = offset->w;
                block = UMemory::FastAlloc(sizeof(AICharacterEnemySunroof), "AICharacter");
                rider = block != NULL
                            ? static_cast<AICharacterEnemySunroof *>(block)->Construct(&seat, member.weapon, 0, riderCar)
                            : NULL;
                ActActorDatabase::GetNewActorHandle(&handle, rider, description,
                                                    kElementWeapons[member.weapon], RiderCallback(),
                                                    &seat, false);
                SetActorOf(rider, handle);
                ActorOf(handle)->SetNewAnimation(kBankSunroof1, 0x12);
                element->memberCharacters[m] = rider;
                awakeCharacters++;
                break;
            case kSeatSnow:
                offset = vehicleObject->attributes.LookupVector("AIC_ESNOW_POS", NULL);
                seat.mtx[3][0] = offset->x;
                seat.mtx[3][1] = offset->y;
                seat.mtx[3][2] = offset->z;
                seat.mtx[3][3] = offset->w;
                block = UMemory::FastAlloc(sizeof(AICharacterEnemySnow), "AICharacter");
                rider = block != NULL
                            ? static_cast<AICharacterEnemySnow *>(block)->Construct(&seat, member.weapon, riderCar)
                            : NULL;
                ActActorDatabase::GetNewActorHandle(&handle, rider, description,
                                                    kElementWeapons[member.weapon], SnowCallback(),
                                                    &seat, false);
                SetActorOf(rider, handle);
                ActorOf(handle)->SetNewAnimation(kBankSnow, 7);
                element->memberCharacters[m] = rider;
                awakeCharacters++;
                break;
            case kSeatGunner:
                offset = vehicleObject->attributes.LookupVector("AIC_EVGUN_POS", NULL);
                seat.mtx[3][0] = offset->x;
                seat.mtx[3][1] = offset->y;
                seat.mtx[3][2] = offset->z;
                seat.mtx[3][3] = offset->w;
                block = UMemory::FastAlloc(sizeof(AICharacterEnemySSnow), "AICharacter");
                rider = block != NULL ? static_cast<AICharacterEnemySSnow *>(block)->Construct(&seat, 1, riderCar)
                                      : NULL;
                ActActorDatabase::GetNewActorHandle(&handle, rider, description, NULL,
                                                    RiderCallback(), &seat, false);
                SetActorOf(rider, handle);
                ActorOf(handle)->SetNewAnimation(kBankSSnow, 6);
                element->memberCharacters[m] = rider;
                awakeCharacters++;
                break;
            case kSeatSnowDriver:
                offset = vehicleObject->attributes.LookupVector("AIC_EVDRI_POS", NULL);
                seat.mtx[3][0] = offset->x;
                seat.mtx[3][1] = offset->y;
                seat.mtx[3][2] = offset->z;
                seat.mtx[3][3] = offset->w;
                block = UMemory::FastAlloc(sizeof(AICharacterEnemySSnow), "AICharacter");
                rider = block != NULL ? static_cast<AICharacterEnemySSnow *>(block)->Construct(&seat, 0, riderCar)
                                      : NULL;
                ActActorDatabase::GetNewActorHandle(&handle, rider, description, NULL,
                                                    RiderCallback(), &seat, false);
                SetActorOf(rider, handle);
                ActorOf(handle)->SetNewAnimation(kBankSSnow, 1);
                element->memberCharacters[m] = rider;
                awakeCharacters++;
                break;
            case kSeatDriver:
                offset = vehicleObject->attributes.LookupVector("AIC_BOND_POS", NULL);
                seat.mtx[3][0] = offset->x;
                seat.mtx[3][1] = offset->y;
                seat.mtx[3][2] = offset->z;
                seat.mtx[3][3] = offset->w;
                block = UMemory::FastAlloc(sizeof(AICharacterEnemyDriver), "AICharacter");
                rider = block != NULL ? static_cast<AICharacterEnemyDriver *>(block)->Construct(&seat, riderCar) : NULL;
                ActActorDatabase::GetNewActorHandle(&handle, rider, description, NULL,
                                                    RiderCallback(), &seat, false);
                SetActorOf(rider, handle);
                ActorOf(handle)->SetNewAnimation(kBankDriver, 8);
                element->memberCharacters[m] = rider;
                awakeCharacters++;
                break;
            }

            // A seat that made no rider leaves the slot as it was; an empty slot ends the riders
            rider = element->memberCharacters[m];
            if (rider == NULL)
                return;
            if (member.skill == kSkill2 || member.skill == kSkill4)
                rider->targetRangeSquared = rider->targetRangeSquared * kSkill2Scale;
            if (member.skill == kSkill3 || member.skill == kSkill5)
                element->memberCharacters[m]->targetRangeSquared =
                    element->memberCharacters[m]->targetRangeSquared * kRiderSkillScale;
            Human *human = static_cast<Human *>(fgSimulation.SpawnHumanObject(0, &Row(element->transform, 2),
                                                       &Row(element->transform, 3), 0));
            element->memberHumans[m] = human;
            if (human != NULL) {
                AICharacter *character = element->memberCharacters[m];
                character->hitPoints = &member.hitPoints;
                character->initialHitPoints = member.hitPoints;
                element->memberCharacters[m]->human = element->memberHumans[m];
                element->memberHumans[m]->SetAICharacter(element->memberCharacters[m]);
                element->memberHumans[m]->SetOwnerObject(vehicleObject);
                element->memberHumans[m]->Activate();
                element->memberHumans[m]->unknown8c = 1;
                ActWeapon *weapon = FirstWeaponOf(element->memberCharacters[m]);
                if (weapon != NULL)
                    weapon->characterInfo = &member;
            }
            if (element->flags & kElementNonScoreable)
                element->memberCharacters[m]->scoreable = 0;
        }
        break;
    }

    case kAIElementCharacter: {
        if (ActorDatabase == NULL)
            return;
        AIElementCharacterInfo &info = element->person.info;
        if (info.kind == kGroundEnemy || info.kind == kGroundEnemy10 || info.kind == kGroundWorker) {
            int animation;
            int bank;
            int start = info.animation;
            if (start > kGroundAnimationDefault) {
                if (start > kLastGroundAnimation)
                    start = kLastGroundAnimation;
                animation = kGroundAnimations[start - kFirstGroundAnimation];
                bank = kGroundAnimationBank;
            } else {
                animation = 0;
                bank = info.weapon == kGroundWeapon2 ? kBankGround2 : kBankGround1;
            }
            int unknown17c = info.kind == kGroundEnemy10 ? 1 : 0;
            bool registerTarget = (element->flags & kElementTargetBeacon) != 0;
            void *block = UMemory::FastAlloc(sizeof(AICharacterEnemyGround), "AICharacter");
            AICharacter *character =
                block != NULL ? static_cast<AICharacterEnemyGround *>(block)->Construct(&element->transform, info.weapon, 0,
                                                                                         unknown17c, registerTarget)
                              : NULL;
            PointerListNode *handle;
            ActActorDatabase::GetNewActorHandle(&handle, character,
                                                kElementDescriptions[info.description], kElementWeapons[info.weapon],
                                                NULL, &element->transform, false);
            if (strstr(kElementDescriptions[info.description], kSnowHeavyDescription) != NULL) {
                ActWeapon *weapon = ActorOf(handle)->character->weapons[0];
                if (weapon != NULL)
                    weapon->unknown164 = 4;
            }
            AICharacterEnemyGround *ground = static_cast<AICharacterEnemyGround *>(character);
            SetActorOf(character, handle);
            character->animation = animation;
            ground->initialBank = bank;
            ActorOf(handle)->SetNewAnimation(bank, animation);
            if (info.skill == kSkill1 || info.skill == kSkill4 || info.skill == kSkill5)
                ground->claustrophobic = 1;
            if (info.kind == kGroundWorker)
                ground->worker = 1;
            element->state = kElementAwake;
            element->character = character;
            awakeCharacters++;
        }

        AICharacter *character = element->character;
        if (character == NULL)
            return;
        if (info.skill == kSkill2 || info.skill == kSkill4)
            character->targetRangeSquared = character->targetRangeSquared * kSkill2Scale;
        if (info.skill == kSkill3 || info.skill == kSkill5)
            element->character->targetRangeSquared = element->character->targetRangeSquared * kGroundSkillScale;
        Human *human = static_cast<Human *>(fgSimulation.SpawnHumanObject(0, &Row(element->transform, 2),
                                                   &Row(element->transform, 3), 0));
        element->person.human = human;
        if (human != NULL) {
            fgSimulation.GetSimpleRigidBody(human->rigidBodySlot)->SetCanHitTrigger(true);
            element->character->SetHitPointLoc(&element->hitPoints);
            element->character->human = element->person.human;
            element->person.human->SetAICharacter(element->character);
            element->person.human->Activate();
            ActWeapon *weapon = FirstWeaponOf(element->character);
            if (weapon != NULL)
                weapon->characterInfo = &element->person.info;
        }
        if (!(element->flags & (kElementWoken | kElementNonScoreable)))
            glbMissionManager->enemyCount++;
        if (element->flags & kElementNonScoreable)
            element->character->scoreable = 0;
        element->flags |= kElementWoken;
        break;
    }
    }
}

// FUNC_AT(0x00028a90)
int CompareRanges(const void *a, const void *b) {
    const AIElementRange *first = static_cast<const AIElementRange *>(a);
    const AIElementRange *second = static_cast<const AIElementRange *>(b);
    return first->closeness < second->closeness ? 1 : -1;
}

// FUNC_AT(0x00029880)
void AIElementController::Update() {
    int32_t *wake = wakeList;
    AIElementRange *range = rangeList;
    stats.maxHitPoints = 0.0f;
    stats.minHitPoints = 1000000.0f;
    stats.maxDamage = 0.0f;
    stats.maxSpeed = 0.0f;
    stats.minSpeed = 1000000.0f;
    stats.maxDistance = 0.0f;
    stats.minDistance = 1000000.0f;

    // The elements in range: those asleep counted by what waking them takes
    int inRangeCount = 0;
    int wakeCount = 0;
    int characters = 0;
    int carsToWake = 0;
    int helicopters = 0;
    const Coord3 &at = fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->position;
    Coord4 player = {at.x, at.y, at.z, 0.0f};
    for (int i = 0; i < count; i++) {
        tracking[i].Update(&player);
        tracking[i].wake = 0;
        AIElement &element = elements[i];
        if (!tracking[i].inRange && !(element.flags & kElementForceAwake))
            continue;
        if (element.state == kElementAsleep) {
            if (element.flags & kElementDeactivated)
                continue;
            switch (element.type) {
            case kAIElementCharacter:
                characters++;
                break;
            case kAIElementVehicle:
                if (element.car.helicopter)
                    helicopters++;
                else
                    carsToWake++;
                characters += element.memberCount;
                break;
            }
            *wake++ = i;
            wakeCount++;
        }
        if (element.flags & kElementDeactivated)
            continue;
        range->index = i;
        range->closeness = tracking[i].closeness;
        range++;
        inRangeCount++;
    }

    cars = awakeCars + carsToWake;
    if (helicopters + carsToWake + characters > 0) {
        if (characterSlots - awakeCharacters >= characters && carSlots - awakeCars >= carsToWake &&
            helicopterSlots - awakeHelicopters >= helicopters) {
            for (int i = 0; i < wakeCount; i++)
                WakeUp(&elements[wakeList[i]]);
        } else {
            // Not all fit: the slots handed out again, the elements that must stay awake first, then the
            // closest ahead
            Crt::qsort(rangeList, inRangeCount, sizeof(AIElementRange), CompareRanges);
            int charactersLeft = characterSlots;
            int carsLeft = carSlots;
            int helicoptersLeft = helicopterSlots;
            for (int i = 0; i < inRangeCount; i++) {
                int index = rangeList[i].index;
                const AIElement &element = elements[index];
                if (!(element.flags & kElementForceAwake))
                    continue;
                switch (element.type) {
                case kAIElementCharacter:
                    charactersLeft--;
                    tracking[index].wake = 1;
                    break;
                case kAIElementVehicle:
                    if (element.car.helicopter) {
                        helicoptersLeft--;
                        tracking[index].wake = 1;
                    } else {
                        carsLeft--;
                        charactersLeft -= element.memberCount;
                        tracking[index].wake = 1;
                    }
                    break;
                }
            }
            for (int i = 0; i < inRangeCount; i++) {
                int index = rangeList[i].index;
                const AIElement &element = elements[index];
                if (element.flags & kElementForceAwake)
                    continue;
                switch (element.type) {
                case kAIElementCharacter:
                    if (charactersLeft > 0) {
                        charactersLeft--;
                        tracking[index].wake = 1;
                    }
                    break;
                case kAIElementVehicle:
                    if (element.car.helicopter) {
                        if (helicoptersLeft > 0) {
                            helicoptersLeft--;
                            tracking[index].wake = 1;
                        }
                    } else if (carsLeft > 0 && charactersLeft >= element.memberCount) {
                        carsLeft--;
                        charactersLeft -= element.memberCount;
                        tracking[index].wake = 1;
                    }
                    break;
                }
            }
            for (int i = 0; i < count; i++) {
                if (elements[i].state == kElementAwake && !tracking[i].wake)
                    GoToSleep(&elements[i]);
            }
            for (int i = 0; i < count; i++) {
                if (elements[i].state == kElementAsleep && tracking[i].wake)
                    WakeUp(&elements[i]);
            }
        }
    }

    if (stats.minDistance < kMinDistanceEvent)
        glbMissionManager->ClearProgrammerEvent(kElementEvent);
    else
        glbMissionManager->ProgrammerDefinedEvent(kElementEvent, "");
}

// ---------------------------------------------------------------------------------------------------------------
// The functions placed among the controller's
// ---------------------------------------------------------------------------------------------------------------


// FUNC_AT(0x00029d20)
float FloatCeil(float x) {
    return float(ceil(x));
}

// FUNC_AT(0x00029d40)
float FloatFloor(float x) {
    return float(floor(x));
}


// ---------------------------------------------------------------------------------------------------------------
// AIZoneController
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00036a00)
AIZone* AIZone::Construct() {
    active = 0;
    type = 0;
    return this;
}

// FUNC_AT(0x00036a10)
AIZoneController* AIZoneController::Get() {
    return AIZoneData.instance;
}

// FUNC_AT(0x00036a20)
void AIZoneController::Init() {
    if (AIZoneData.instance == NULL)
        AIZoneData.instance = static_cast<AIZoneController *>(OperatorNew(sizeof(AIZoneController)));
    AIZoneData.lifeSteps[kZoneType2] = 15;
    AIZoneData.mergeSteps[kZoneType2] = 15;
    AIZoneData.lifeSteps[kZoneType3] = 15;
    AIZoneData.mergeSteps[kZoneType3] = 15;
    AIZoneData.lifeSteps[kZoneType1] = 0;
    AIZoneData.mergeSteps[kZoneType1] = 0;
    AIZoneData.radiusSquared[kZoneType1] = 0.0f;
    AIZoneData.radiusSquared[kZoneType2] = 625.0f;
    AIZoneData.radiusSquared[kZoneType3] = 1225.0f;
    AIZoneData.lifeSteps[kZoneType4] = 175;
    AIZoneData.mergeSteps[kZoneType4] = 85;
    AIZoneData.radiusSquared[kZoneType4] = 12.25f;
    for (int i = 0; i < kZones; i++)
        AIZoneData.zones[i].active = 0;
}

// FUNC_AT(0x00036ac0)
void AIZoneController::Update() {
    int16_t active = AIZoneData.activeCount;
    for (int i = 0; i < kZones; i++) {
        AIZone &zone = AIZoneData.zones[i];
        if (!zone.active)
            continue;
        if (--zone.stepsLeft <= 0) {
            zone.active = 0;
            active--;
        }
    }
    AIZoneData.activeCount = active;
}

// FUNC_AT(0x00036b00)
void AIZoneController::Shutdown() {
    if (AIZoneData.instance != NULL)
        OperatorDelete(AIZoneData.instance);
    AIZoneData.instance = NULL;
}

// The type's radius is looked up by the low half of the zone's type
// FUNC_AT(0x00036b20)
bool AIZoneController::InZone(int zone, const Coord3 *point, float margin) {
    const AIZone &record = AIZoneData.zones[zone];
    if (!record.active)
        return false;
    int16_t type = int16_t(record.type);
    Coord4 position = {record.position.x, record.position.y, record.position.z, 0.0f};
    double distanceSquared = double(VU0_v3distancesquare(&position, point)) - margin;
    if (distanceSquared < 0.0)
        distanceSquared = 0.0;
    return distanceSquared <= AIZoneData.radiusSquared[type];
}

// FUNC_AT(0x00036bb0)
void AIZoneController::RegisterZone(int type, const Coord3 *position) {
    if (AIZoneData.mergeSteps[type] <= 0)
        return;
    AIZone *free = NULL;
    int16_t active = AIZoneData.activeCount;
    for (int i = 0; i < kZones; i++) {
        AIZone &zone = AIZoneData.zones[i];
        if (free == NULL && !zone.active)
            free = &zone;
        if (active > 0 && zone.active) {
            active--;
            if (zone.type == type && zone.stepsLeft >= AIZoneData.mergeSteps[type] &&
                AIZoneData.instance->InZone(i, position, AIZoneData.radiusSquared[type]))
                return;
        }
    }
    if (free == NULL)
        return;
    free->active = 1;
    free->type = type;
    AIZoneData.activeCount++;
    free->position = *position;
    free->radiusSquared = AIZoneData.radiusSquared[type];
    free->stepsLeft = AIZoneData.lifeSteps[type];
}
