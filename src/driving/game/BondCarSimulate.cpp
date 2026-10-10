#include "BondCarSimulate.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <bit>
#include <xmmintrin.h>

#include "AIVehicle.h"
#include "BondCarBasics.h"          // GetCarColourVariation
#include "BondCarState.h"           // CarSurface, the BondCar_* tuning
#include "Missile.h"
#include "Vehicle.h"
#include "VehicleSound.h"
#include "../anim/AnimEngine.h"
#include "../audio/Bank.h"
#include "../audio/Sound.h"
#include "../camera/CameraIniLoader.h"
#include "../camera/PlayerCamera.h"
#include "../data/Carp.h"
#include "../data/StdStreams.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/Feedback.h"
#include "../engine/MissionManager.h"
#include "../engine/PhysicsUtil.h"
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RenderHigh.h"
#include "../render/RSceneObj.hpp"
#include "../world/Targeting.h"
#include "../../common/xbeOverload.h"
#include "../../helpers.h"

#pragma fp_contract(off)

// ---- the game's globals

#define Sim ((void *)0x00233ff0)
#define SimulationRandom (*(SimRandom **)0x00233ff0)        // the Simulation's first word
#define SimStepCount I32_AT(0x00234e34)
#define SimState I32_AT(0x00234e24)
#define PlayerPhysicsObject (*(PhysicsObject **)PTR_AT(0x00234e40))
#define ZeroVector (*(const Coord3 *)0x00243030)            // the game's zero vector, never written
#define OverrideCarType (*(const char **)0x001e7a8c)        // used for a car of class 0 when set (name ours)
#define ControllerPort I32_AT(0x00244520)                   // the launch page's controller port
#define AIControllerInstance (*(void **)0x001ddee4)          // AIElementController's one instance (name ours)
#define WeaponManager (*(BondCarWeapons **)0x0023923c)

// ---- calls to originals not ported

#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)
#define RVehicle_Construct ((RSceneObj *(__fastcall *)(void *, int, PBondCar *owner))0x000953e0)
#define RVehicle_SetEMPVictimEffect ((void (__fastcall *)(RSceneObj *, int, bool on))0x000959d0)
#define AIVehicleController_Get ((void *(*)())0x00035c00)
// Static in effect: ECX (the controller) is not read
#define AIVehicleController_FindAgentGroundVehiclePtr ((AIGroundVehicle *(__fastcall *)(void *, int, PBondCar *vehicle))0x00035e90)
#define AIElementController_ForceVehicleToSleep ((void (__fastcall *)(void *, int, AIGroundVehicle *vehicle))0x000289f0)
#define SMissionManager_IncPlayerDamage ((void (__fastcall *)(SMissionManager *, int, float damage))0x000b66c0)
#define SMissionManager_IncShotsHit ((void (__fastcall *)(SMissionManager *, int, bool hit))0x000b6720)
#define SMissionManager_IncKills ((void (__fastcall *)(SMissionManager *, int, int kills))0x000b6770)
#define GHud_TheApp ((void *(*)())0x000d7c10)
#define GHud_TriggerDamageFlash ((void (__fastcall *)(void *, int, float damage))0x000d87a0)
// AOneShotSound's constructor: a patch of a bank at a place
#define AOneShotSound_Construct ((AOneShotSound *(__fastcall *)(void *, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity, float volume, const char *mix))0x000476f0)
// The engine sounds' constructors and methods (AVehicle and its kinds)
#define ATrafficVehicle_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *engine, const char *mix))0x0012cb90)
#define AHelicopter_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *mix))0x0012c7f0)
#define APlayerTank_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *engine))0x0012c550)
#define ASubmersible_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *mix))0x0012b960)
#define ASnowMobile_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *mix))0x0012b5f0)
#define AUltraLite_Construct ((ABaseSound *(__fastcall *)(void *, int))0x0012a440)
#define AFingoDeath_Construct ((ABaseSound *(__fastcall *)(void *, int))0x00129a30)
#define APlayerVehicle_Construct ((ABaseSound *(__fastcall *)(void *, int, const char *engine))0x001284b0)
#define APlayerHeli_Construct ((ABaseSound *(__fastcall *)(void *, int))0x00128a40)
#define AVehicle_ChooseHorn ((void (__fastcall *)(ABaseSound *, int, const char *horn, float volume, float pitch))0x00120960)
#define AVehicle_ActivateGun ((void (__fastcall *)(ABaseSound *, int, const char *mix))0x0011e9a0)
#define AVehicle_ActivateWind ((void (__fastcall *)(ABaseSound *, int))0x0011eaf0)
#define ATrafficVehicle_AddSiren ((void (__fastcall *)(ABaseSound *, int, const char *patch))0x0012ce30)
// The gun's fire patch, the string passed by value (the callee destroys it)
#define AGun_SetFirePatch ((void (__fastcall *)(void *, int, GameStd::String patch))0x001296b0)

namespace {

// The weapon manager (SWeaponManager, not ported): what Simulate reads
struct BondCarWeaponSlot {
    int32_t weapon;                 // +0x00
    uint8_t unknown04[0x14];
    int32_t unknown18;              // +0x18
    uint8_t unknown1C[0x38];
};
static_assert(sizeof(BondCarWeaponSlot) == 0x54, "a weapon slot is 84 bytes");

struct BondCarWeapons {
    uint32_t unknown00;
    int32_t current;                // +0x04
    uint8_t unknown08[8];
    BondCarWeaponSlot *slots;       // +0x10
    uint8_t unknown14[0x13];
    uint8_t unknown27;              // +0x27
};
static_assert(offsetof(BondCarWeapons, unknown27) == 0x27, "weapon manager layout");

// The weapons Simulate tests for (neutral names)
enum BondCarWeapon : int32_t {
    kWeapon0 = 0,
    kWeapon1B = 0x1b,
    kWeapon18 = 0x18,
    kWeapon1D = 0x1d,
};

// ApplyDamage's kinds
enum DamageKind : int32_t {
    kDamageKind0 = 0,
    kDamageKind1 = 1,
    kDamageBullet = 2,              // pushes the car; can shoot out a tyre
    kDamageKind3 = 3,               // Simulate's own when the car stays upside down
};

// ApplyDamage's answers
enum DamageResult : int {
    kDamageMissed = 0,
    kDamageDone = 0x10,
    kDamageSide = 0x70,
    kDamageFront = 0x80,
    kDamageShielded = 0x90,
};

// The animation stimuli sent here (neutral names); every one queued (mode 2)
enum : uint8_t {
    kStimulus2 = 2,
    kStimulus3 = 3,
    kStimulus4 = 4,
    kStimulus5 = 5,
    kStimulus6 = 6,
    kStimulus7 = 7,
    kStimulusZone1 = 0x13,          // the zones whose damage reached fgDamageLevels.zone[1], then [2] .. [4]
    kStimulusZone4 = 0x16,
    kStimulus19 = 0x19,
};
constexpr int kQueueAll = 2;
constexpr uint32_t kTyreSystemIds[kCarWheels] = { 1, 2, 3, 4 };    // 0x0018f748
constexpr uint32_t kSystem9 = 9;

constexpr uint32_t kBubbleEffect = 0x800000;    // the effect bit the submarine and snowmobile fade (name ours)
constexpr uint16_t kAllZones = 0xffff;
constexpr uint32_t kSideZones = 0x3ca5;         // the zones answered as side hits (kDamageSide)
constexpr uint32_t kFrontZones = 0x3f18;        // ... as front hits (kDamageFront), for class 1

constexpr int kResetSteps = 30;                 // ResetCar after this many steps upside down
constexpr int kResetStepsLimit = 120;
constexpr float kUpsideDown = 0.3f;             // GetOrientToGround below it
constexpr float kRandomScale = 1.0f / 65536.0f;
constexpr float kSteerScale = 0.3f / 65536.0f;
static_assert(std::bit_cast<uint32_t>(kUpsideDown) == 0x3e99999a && std::bit_cast<uint32_t>(kRandomScale) == 0x37800000 &&
              std::bit_cast<uint32_t>(kSteerScale) == 0x3699999a && std::bit_cast<uint32_t>(0.1f) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(0.2f) == 0x3e4ccccd && std::bit_cast<uint32_t>(0.7f) == 0x3f333333,
              "the original's constants");

// Calls through the car's vtable (PVehicle::Slot), reaching whichever override the object has
template <class Method, class... Args>
auto Virtual(PBondCar *car, int slot, Method, Args... args) {
    return (car->*XbeVirtual<Method>(car, slot))(args...);
}

// The render object's virtual SetViewDrawList (slot 1) and TriggerFX (slot 15, its unread arguments as the game
// passes them)
void SetViewDrawList(RSceneObj *object, int list) {
    typedef void (RSceneObj::*Method)(int list);
    (object->*XbeVirtual<Method>(object, 1))(list);
}

void TriggerFX(RSceneObj *object, RSceneObj::FXType type, uint32_t which, float intensity) {
    typedef void (RSceneObj::*Method)(int type, uint32_t which, uint32_t unused3, const Coord3 *unused4,
                                      const Coord3 *unused5, float intensity);
    (object->*XbeVirtual<Method>(object, 15))(type, which, 0, &ZeroVector, &ZeroVector, intensity);
}

const Coord3 *XYZ(const Coord4 *v) {
    return reinterpret_cast<const Coord3 *>(v);
}

// a + (b - a) * t in x, y and z: the SSE lerp the compiler inlined (MOVSS and MOVHPS put x in lane 0 and y, z in
// lanes 2 and 3)
void LerpXYZ(Coord3 *a, const Coord4 *b, float t) {
    __m128 from = _mm_set_ps(a->z, a->y, 0.0f, a->x);
    __m128 to = _mm_set_ps(b->z, b->y, 0.0f, b->x);
    __m128 r = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(to, from)), from);
    float lanes[4];
    _mm_storeu_ps(lanes, r);
    a->x = lanes[0];
    a->y = lanes[2];
    a->z = lanes[3];
}

// The intensity of the bubble effect at `speed` above 5: a tenth of the excess, at most 1
float BubbleIntensity(float speed) {
    float intensity = (float)((speed - 5.0) * 0.1f);
    return intensity < 1.0f ? intensity : 1.0f;
}

uint8_t WheelSurface(const PBondCar *car, int wheel) {
    return car->wheels[wheel].face.corner[2].tag.type;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0006d350)
PBondCar* PBondCar::Construct(int carClass, const char *carType, uint32_t colour, const Coord3 *direction,
                              const Coord3 *position) {
    PVehicle::Construct(carType);
    vtable = reinterpret_cast<void **>(kPBondCarVtable);
    for (int wheel = 0; wheel < kCarWheels; wheel++)
        wheels[wheel].Construct();

    shieldPointLoc = NULL;
    secondaryType = NULL;
    this->carClass = carClass;
    this->carType = carType;
    carColour = colour;
    subRolling = 1;
    scoreable = 1;
    unknown408 = 0;
    forceRollDirection = 0;
    forceStop = 0;
    rollSub = 0;

    const char *name = carType;
    if (OverrideCarType != NULL && carClass == kPlayerCar)
        name = OverrideCarType;
    if (CRT_stricmp(carType, name) != 0) {
        attributes.SetName(name, false);
        this->carType = name;
    }

    secondaryType = attributes.LookupString("SECONDARY_TYPE", NULL);
    unknown2DC = -1.0f;
    physics = PVehicle::CarPhysicsAttrib(&attributes);
    laserActive = 0;
    unknown395[0] = 0;
    unknown395[1] = 0;
    unknown395[2] = 0;

    if (renderObject == NULL) {
        void *block = UMemory::FastAlloc(0x370, "RVehicle");
        SetRenderObject(block != NULL ? RVehicle_Construct(block, 0, this) : NULL);
        RSceneObj *render = renderObject;
        uint32_t variation = GetCarColourVariation(this->carType, colour);
        const char *label = attributes.Name();
        render->Load("data\\car\\model\\", PVehicle::RenderNameAttrib(&attributes), label, variation);
        renderObject->renderTypeIndex = PVehicle::NameToIndex(this->carType);
    }

    Coord4 dimensions;
    renderObject->GetBoundingDimensions(&dimensions);
    for (int glare = 0; glare < 16; glare++)
        glareOn[glare] = 0;
    unknown375 = 0;
    if (this->carClass == kPlayerCar) {
        WTargetable *target = static_cast<WTargetable *>(UMemory::FastAlloc(sizeof(WTargetable), "WTargetable"));
        targetBeacon = target != NULL ? target->Construct(this) : NULL;
    } else if (this->carClass == kCarClass2) {
        WTargetable *target = static_cast<WTargetable *>(UMemory::FastAlloc(sizeof(WTargetable), "WTargetable"));
        targetBeacon = target != NULL ? target->Construct(this) : NULL;
        TargetPicker.RegisterTarget(targetBeacon);
    } else {
        targetBeacon = NULL;
        if (this->carClass == kCarClass4) {
            this->carClass = kCarClass2;
            unknown375 = 1;
        }
    }

    turnSignalMode = 0;
    turnSignalStart = 0;
    turnSignalPeriod = 60;
    turnSignalLit = 0.4f;
    tyreBlowOuts = carClass != kPlayerCar && !physics->isSub && !physics->isSnowmobile &&
                   !attributes.LookupBool("IS_TANK", NULL) && !attributes.LookupBool("IS_HELICOPTER", NULL) &&
                   !attributes.LookupBool("TYRES_INVUNERABLE", NULL);
    empActive = 0;
    empTimer = 0;
    resetDirection = *direction;
    hitPoints = 700.0f;
    SetHitPointLoc(&hitPoints);
    immunity = 0;

    if (carClass == kPlayerCar) {
        ActionQueue *queue = static_cast<ActionQueue *>(UMemory::FastAlloc(0x974, "ActionQueue"));
        actionQueue = queue != NULL ? queue->Construct((char *)"CarQ") : NULL;
    } else {
        actionQueue = NULL;
    }
    control.firePrimary = 0;
    InitializeCarVariables(true);
    ResetDamage();

    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    int8_t kind;
    switch (carClass) {
    case kPlayerCar:
    case kCarClass1:
        kind = 1;
        break;
    case kCarClass3:
        kind = 3;
        break;
    default:
        kind = 2;
        break;
    }

    Coord3 velocity = { 0.0f, 0.0f, 0.0f };
    Coord3 tensor;
    Coord3 inverseInertia = *Util_GenerateCarTensor(&tensor, attributes.LookupFloat("MASS", NULL), dimensions.x,
                                                    dimensions.y, dimensions.z);
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    if (body != NULL)
        body->Construct(int8_t(rigidBodySlot), kind, position, &velocity, &ZeroVector, &orientation,
                        attributes.LookupFloat("MASS", NULL), &inverseInertia, &dimensions, this, true, false);
    SetOwnerObject(this);
    InitTyreTracks();
    InitAudioObject(carClass);

    if (carClass == kPlayerCar) {
        IFeedback *feedback = static_cast<IFeedback *>(UMemory::FastAlloc(sizeof(IFeedback), "IFeedback"));
        SetFeedbackObject(feedback != NULL ? feedback->Construct(ControllerPort) : NULL);
    }
    aiGroundVehicle = NULL;
    if (carClass == kPlayerCar)
        GlareOn(1);
    return this;
}

// The engine sound for the car's class and type, with its volume and distances
// FUNC_AT(0x0006cc20)
void PBondCar::InitAudioObject(int carClass) {
    const char *engine = attributes.LookupString("SFX_ENGINE", NULL);
    float volume = attributes.LookupFloat("SFX_VOLUME", NULL);
    int16_t hasSiren = attributes.LookupInt("HAS_SIREN", NULL);
    int16_t isHeli = attributes.LookupInt("IS_HELI", NULL);
    float skidMultiple = attributes.LookupFloat("SFX_SKIDMULTIPLE", NULL);
    int16_t isFlying = attributes.LookupInt("IS_FLYING", NULL);
    // (The original also builds "SFX_<engine>LdEn" in a local buffer, and does not use it.)

    float minDistance = 30.0f;
    float maxDistance = 400.0f;
    float maxDistanceSq = 160000.0f;
    switch (carClass) {
    case kPlayerCar: {
        void *block;
        if (CRT_stricmp(carType, "tank") == 0) {
            block = ABaseSound::OperatorNew(0x210, "APlayerTank");
            audio = block != NULL ? APlayerTank_Construct(block, 0, engine) : NULL;
        } else if (CRT_stricmp(carType, "helicopter") == 0 || CRT_stricmp(carType, "helivanquish") == 0) {
            block = ABaseSound::OperatorNew(0x210, "APlayerHeli");
            audio = block != NULL ? APlayerHeli_Construct(block, 0) : NULL;
        } else if (CRT_stricmp(carType, "vanquishsub") == 0) {
            block = ABaseSound::OperatorNew(0x240, "ASubmersible");
            audio = block != NULL ? ASubmersible_Construct(block, 0, "Sub") : NULL;
        } else if (CRT_stricmp(carType, "supersnow") == 0) {
            block = ABaseSound::OperatorNew(0x260, "ASnowMobile");
            audio = block != NULL ? ASnowMobile_Construct(block, 0, "SupSnow") : NULL;
        } else if (CRT_stricmp(carType, "ultralight") == 0) {
            block = ABaseSound::OperatorNew(0x230, "AUltraLite");
            audio = block != NULL ? AUltraLite_Construct(block, 0) : NULL;
        } else if (CRT_stricmp(carType, "fodbase") == 0) {
            block = ABaseSound::OperatorNew(0x200, "AFingoDeath");
            audio = block != NULL ? AFingoDeath_Construct(block, 0) : NULL;
        } else if (CRT_stricmp(carType, "jungle_truck") == 0) {
            block = ABaseSound::OperatorNew(0x240, "ATrafficVehicle");
            audio = block != NULL ? ATrafficVehicle_Construct(block, 0, engine, "Traffic Cars") : NULL;
            AVehicle_ActivateGun(audio, 0, "Machine Gun");
            AVehicle_ActivateWind(audio, 0);
            AVehicle *sound = static_cast<AVehicle *>(audio);
            sound->unknownD0 = 7;
            GameStd::String patch;
            patch.ConstructText("SFX_mntmachfire");
            AGun_SetFirePatch(sound->gun, 0, patch);
            sound->unknownC5 = 0;
        } else {
            block = ABaseSound::OperatorNew(0x220, "APlayerVehicle");
            audio = block != NULL ? APlayerVehicle_Construct(block, 0, engine) : NULL;
        }
        audio->volume = volume;
        break;
    }
    case kCarClass2: {
        void *block;
        if (isHeli) {
            block = ABaseSound::OperatorNew(sizeof(AHelicopter), "AHelicopter");
            audio = block != NULL ? AHelicopter_Construct(block, 0, "Helicopter") : NULL;
        } else {
            block = ABaseSound::OperatorNew(0x240, "ATrafficVehicle");
            audio = block != NULL ? ATrafficVehicle_Construct(block, 0, engine, "Opponents") : NULL;
        }
        audio->volume = volume;
        if (isHeli) {
            int roll = SimulationRandom->Generate();
            static_cast<AHelicopter *>(audio)->randomPhase = (float)(double(roll) * 0.2f * kRandomScale);
        } else {
            int roll = SimulationRandom->Generate();
            static_cast<ATrafficVehicle *>(audio)->randomPhase = (float)(double(roll) * 0.2f * kRandomScale);
            if (hasSiren)
                ATrafficVehicle_AddSiren(audio, 0, "SFX_copsiren");
        }
        if (isHeli || isFlying) {
            minDistance = 100.0f;
            maxDistance = 600.0f;
            maxDistanceSq = 360000.0f;
        }
        break;
    }
    case kCarClass4: {
        const char *mix = CRT_stricmp(engine, "750") == 0 || CRT_stricmp(engine, "Nsub") == 0 ? "Opponents"
                                                                                             : "Friendly";
        void *block = ABaseSound::OperatorNew(0x240, "ATrafficVehicle");
        audio = block != NULL ? ATrafficVehicle_Construct(block, 0, engine, mix) : NULL;
        int roll = SimulationRandom->Generate();
        static_cast<ATrafficVehicle *>(audio)->randomPhase = (float)(double(roll) * 0.2f * kRandomScale);
        if (hasSiren)
            ATrafficVehicle_AddSiren(audio, 0, "SFX_copsiren");
        audio->volume = volume;
        break;
    }
    default: {
        void *block = ABaseSound::OperatorNew(0x240, "ATrafficVehicle");
        audio = block != NULL ? ATrafficVehicle_Construct(block, 0, engine, "Traffic Cars") : NULL;
        float hornPitch = attributes.LookupFloat("SFX_HORNPITCH", NULL);
        const char *horn = attributes.LookupString("SFX_HORN", NULL);
        AVehicle_ChooseHorn(audio, 0, horn, 1.0f, hornPitch);
        audio->volume = volume;
        minDistance = 10.0f;
        maxDistance = 200.0f;
        maxDistanceSq = 40000.0f;
        break;
    }
    }
    audio->minDistance = minDistance;
    audio->maxDistance = maxDistance;
    audio->maxDistanceSq = maxDistanceSq;

    if (skidMultiple >= 1.0f)
        static_cast<AVehicle *>(audio)->skidMultiple = skidMultiple;
    SetAudioObject(audio);
}

// ---------------------------------------------------------------------------------------------------------------
// The step
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0006a840)
void PBondCar::Simulate() {
    alignas(16) BondCarScratch scratch;
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    if (body->sleepState == RigidBody::kFrozen)
        return;
    if (inShock)
        inShock--;
    if (damageByPlayerTimer)
        damageByPlayerTimer--;
    RPlayerCamera *camera = fgRenderHigh->views[0].camera;
    if (carClass == kCarClass3 && !(body->flags & RigidBody::kFlag0) &&
        VU0_v3distancesquare(&body->position, MatrixRow(&camera->matrix, 3)) > BondCar_FarDistanceSq &&
        ((uint8_t(body->ownerIndex) ^ SimStepCount) & 1) != 0)
        return;

    BondCarScratchPad = &scratch;
    if (targetBeacon != NULL && GetHitPoints() <= 0.0f)
        Virtual(this, PVehicle::kDisableTargetBeacon, &PBondCar::DisableTargetBeacon);
    laserActive = laserTimer > 0;
    if (laserTimer > 0)
        laserTimer--;

    int drawList = 1;
    if (carClass == kPlayerCar || carClass == kCarClass1) {
        if (fgCameraTables.modes[fgRenderHigh->views[0].camera->cameraMode].interiorView)
            drawList = 2;
        else if (glbMissionManager->unknown478)
            drawList = 5;
    }
    SetViewDrawList(renderObject, drawList);

    if (carClass == kPlayerCar) {
        if (body->GetOrientToGround() < kUpsideDown && body->groundContacts) {
            if (resetAvailable < kResetSteps)
                resetAvailable++;
        } else {
            resetAvailable = 0;
            LerpXYZ(&resetDirection, MatrixRow(&body->info->orientation, 2), 0.1f);
        }
        Virtual(this, PVehicle::kGetControllerInput, &PBondCar::GetControllerInput);

        float speed = v3dotprod(&body->velocity, MatrixRow(&body->info->orientation, 2));
        if (control.brake > 0.0f && speed < 0.0f)
            Virtual(this, PVehicle::kGlareOn, &PBondCar::GlareOn, 5);
        else if (speed >= 0.0f)
            GlareOff(5);
        if (speed <= 0.0f ? control.gas > 0.0f && speed < -2.5f : control.brake > 0.0f)
            Virtual(this, PVehicle::kGlareOn, &PBondCar::GlareOn, 2);
        else
            GlareOff(2);

        if (physics->isSub == 1) {
            if (speed > 5.0f)
                TriggerFX(renderObject, RSceneObj::kFXOn, kBubbleEffect, BubbleIntensity(speed));
            else
                TriggerFX(renderObject, RSceneObj::kFXSuppress, kBubbleEffect, -1.0f);
        }
        HandleTurnSignals();
        if (empActive && SimStepCount > empTimer) {
            RVehicle_SetEMPVictimEffect(renderObject, 0, false);
            empActive = 0;
        }
    } else if (carClass == kCarClass1) {
        if (body->GetOrientToGround() < kUpsideDown && body->groundContacts) {
            if (resetAvailable < kResetSteps)
                resetAvailable++;
        } else {
            resetAvailable = 0;
        }
    } else {
        if (body->GetOrientToGround() < kUpsideDown && body->groundContacts)
            resetAvailable++;
        else
            resetAvailable = 0;
        if (resetAvailable > kResetStepsLimit)
            resetAvailable = kResetStepsLimit;
        // Upside down, the car damages itself
        if (resetAvailable != 0 && GetHitPoints() > 0.0f) {
            const Coord4 origin = { 0.0f, 0.0f, 0.0f, 0.0f };
            ApplyDamageVirtual(&origin, GetPosition(), 100.0f, 0.5f, kDamageKind3, DamageSourceSig);
        }
    }

    if (physics->isSnowmobile) {
        bool onSpline = carClass == kCarClass2 &&
                        Virtual(this, PVehicle::kGetAIGroundVehiclePtr, &PBondCar::GetAIGroundVehiclePtr) != NULL &&
                        AIVehicle_GetSplinePath(Virtual(this, PVehicle::kGetAIGroundVehiclePtr,
                                                        &PBondCar::GetAIGroundVehiclePtr), 0) != NULL;
        float speed = v3dotprod(&body->velocity, MatrixRow(&body->info->orientation, 2));
        if ((onSpline || (speed > 5.0f && (Virtual(this, PVehicle::kIsWheelOnGround, &PBondCar::IsWheelOnGround,
                                                   int8_t(2)) ||
                                           Virtual(this, PVehicle::kIsWheelOnGround, &PBondCar::IsWheelOnGround,
                                                   int8_t(3))))) &&
            (WheelSurface(this, 2) == kSNOW || WheelSurface(this, 3) == kICE))
            TriggerFX(renderObject, RSceneObj::kFXOn, kBubbleEffect, BubbleIntensity(speed));
        else
            TriggerFX(renderObject, RSceneObj::kFXSuppress, kBubbleEffect, -1.0f);
    }

    if (inShock) {
        control.gas = 0.0f;
        control.brake = 0.0f;
        control.steering = 0.0f;
    }
    if (SimStepCount < controlLockTimer)
        control = lockedControl;
    if ((carClass == kPlayerCar || carClass == kCarClass1) && resetAvailable == kResetSteps) {
        typedef void (PBondCar::*ResetCarMethod)(bool unknown);
        if (carClass == kCarClass1) {
            Simulation_GetRigidBody(Sim, 0, rigidBodySlot)->info->unknown4fc = 1;
            Virtual(this, PVehicle::kResetCar, ResetCarMethod(&PBondCar::ResetCar), true);
        } else {
            Virtual(this, PVehicle::kResetCar, ResetCarMethod(&PBondCar::ResetCar), false);
        }
    }

    if (Virtual(this, PVehicle::kGetAIGroundVehiclePtr, &PBondCar::GetAIGroundVehiclePtr) != NULL &&
        AIVehicle_GetSplinePath(Virtual(this, PVehicle::kGetAIGroundVehiclePtr, &PBondCar::GetAIGroundVehiclePtr),
                                0) != NULL &&
        GetHitPoints() > 0.0f)
        ProcessSplinePhysics();
    else if (physics->subPhysics)
        ProcessSubmarinePhysics();
    else if (Virtual(this, PVehicle::kGetPhysics, &PBondCar::GetPhysics)->isSnowmobile)
        ProcessSnowmobilePhysics();
    else if (carClass == kPlayerCar)
        ProcessPhysics();
    else
        ProcessSimplePhysics();

    // ---- the engine sound's inputs
    float rpm;
    float frontSlip;
    float rearSlip;
    float throttle;
    BondCarControl controls;
    if (physics->subPhysics == 1) {
        frontSlip = 0.0f;
        rearSlip = 0.0f;
        throttle = 1.0f;
        rpm = (float)(VU0_v3length(&Simulation_GetRigidBody(Sim, 0, rigidBodySlot)->velocity) * 60.0 + 2000.0);
    } else {
        float slip1 = Virtual(this, PVehicle::kGetCarWheelSlip, &PBondCar::GetCarWheelSlip, 1);
        frontSlip = (float)((Virtual(this, PVehicle::kGetCarWheelSlip, &PBondCar::GetCarWheelSlip, 0) +
                             double(slip1)) * 0.5);
        float slip3 = Virtual(this, PVehicle::kGetCarWheelSlip, &PBondCar::GetCarWheelSlip, 3);
        rearSlip = (float)((Virtual(this, PVehicle::kGetCarWheelSlip, &PBondCar::GetCarWheelSlip, 2) +
                            double(slip3)) * 0.5);
        if (Virtual(this, PVehicle::kGetCarClass, &PBondCar::GetCarClass) == kCarClass1) {
            if (frontSlip > 0.1f)
                frontSlip = 1.0f;
            if (rearSlip > 0.1f)
                rearSlip = 1.0f;
            float speedXZ = VU0_v3lengthxz(&body->velocity);
            if (!(Virtual(this, PVehicle::kGetCarControl, &PBondCar::GetCarControl, &controls)->brake > 0.1f) &&
                !Virtual(this, PVehicle::kGetCarControl, &PBondCar::GetCarControl, &controls)->handBrake) {
                if (CRT_stricmp(carType, "tank") != 0 && speedXZ > 2.0f && speedXZ < 4.0f)
                    rearSlip = 1.0f;
                throttle = 1.0f;
            } else {
                if (speedXZ > 0.1f && speedXZ < 8.0f)
                    rearSlip = 1.0f;
                throttle = 0.0f;
            }
            rpm = (float)(CalculateRPM() * 1.25f);
        } else {
            throttle = Virtual(this, PVehicle::kGetCarControl, &PBondCar::GetCarControl, &controls)->gas;
            rpm = (float)CalculateRPM();
        }
    }

    bool engineOn;
    if (GetHitPoints() <= 0.0f)
        engineOn = false;
    else if (Virtual(this, PVehicle::kGetCarClass, &PBondCar::GetCarClass) > kCarClass1 && aiGroundVehicle != NULL)
        engineOn = aiGroundVehicle->unknown88 != 1;
    else
        engineOn = true;

    // The original asks for the class twice and does not use the first answer
    Virtual(this, PVehicle::kGetCarClass, &PBondCar::GetCarClass);
    uint8_t weaponFlagC0 = 0;
    uint8_t weaponFlagC1 = 0;
    BondCarWeapons *weapons = WeaponManager;
    if (Virtual(this, PVehicle::kGetCarClass, &PBondCar::GetCarClass) == kCarClass1) {
        int32_t weapon = weapons->slots[weapons->current].weapon;
        if ((SimStepCount >= controlLockTimer && weapon == kWeapon18) || weapon == kWeapon1D ||
            weapon == kWeapon1B) {
            weaponFlagC0 = 1;
            weaponFlagC1 = weapons->unknown27;
        }
    } else {
        BondCarWeaponSlot *slot = &weapons->slots[weapons->current];
        weaponFlagC0 = 1;
        if (slot->weapon == kWeapon0 && slot->unknown18 > 0 && control.unknown1B) {
            RSceneObj *playerRender = PlayerPhysicsObject->renderObject;
            if (playerRender == NULL) {
                weaponFlagC1 = 1;
            } else {
                uint32_t state = playerRender->animHandle->GetSystemState(kSystem9);
                weaponFlagC1 = state == 2 || state == 3;
            }
        }
    }
    SMissionManager *mission = glbMissionManager;
    if (mission->unknown474 == 3 || mission->unknown474 == 4 || SimState == 3 || mission->unknown4f0 != 0)
        weaponFlagC1 = 0;

    int surfaces[kCarWheels];
    for (int wheel = 0; wheel < kCarWheels; wheel++)
        surfaces[wheel] = Virtual(this, PVehicle::kIsWheelOnGround, &PBondCar::IsWheelOnGround, int8_t(wheel))
                              ? WheelSurface(this, wheel) : 0;
    if (physics->subPhysics == 1) {
        for (int wheel = 0; wheel < kCarWheels; wheel++)
            surfaces[wheel] = 1;
    }

    AVehicle *sound = static_cast<AVehicle *>(Virtual(this, PVehicle::kGetAudio, &PBondCar::GetAudio));
    sound->rpm = rpm;
    sound->engineOn = engineOn;
    sound->throttle = throttle;
    sound->frontSlip = frontSlip < 1.0f ? frontSlip : 1.0f;
    sound->rearSlip = rearSlip < 1.0f ? rearSlip : 1.0f;
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        sound->previousSurfaces[wheel] = sound->surfaces[wheel];
        sound->surfaces[wheel] = surfaces[wheel];
    }
    sound->weaponFlags = weaponFlagC0;
    sound->unknownC1 = weaponFlagC1;
    sound->position = body->position;
    sound->velocity = body->velocity;
    const MATRIX4 *orientation = &body->info->orientation;
    sound->SetOrientation(XYZ(MatrixRow(orientation, 2)), XYZ(MatrixRow(orientation, 0)),
                          XYZ(MatrixRow(orientation, 1)));
    const Coord4 *aim = fgRenderHigh->views[0].camera->GetForwardAimVec4(1.0f);
    sound->cameraAim.x = aim->x;
    sound->cameraAim.y = aim->y;
    sound->cameraAim.z = aim->z;
    sound->speed = carSpeed;
    sound->steering = control.steering;
    sound->steeringVertical = control.steeringVertical;
    sound->unknown150 = unknown2EC;
    for (int wheel = 0; wheel < kCarWheels; wheel++)
        sound->suspensionCompression[wheel] = suspensionCompression[wheel];
    sound->unknown17A = unknown278 > 0.0f;
    sound->unknown17B = unknown27C > 0.0f;

    if (carClass == kPlayerCar)
        targetBeacon->UpdatePosition();
    ControlTyreTracks();
    PhysicsObject::Simulate();

    if (GetHitPoints() <= 0.0f && carClass == kCarClass2 &&
        (CRT_stricmp(carType, "tunnel") == 0 || CRT_stricmp(carType, "openheli") == 0 ||
         CRT_stricmp(carType, "redheli") == 0) &&
        !renderObject->animHandle->AnySystemPlaying()) {
        AIGroundVehicle *agent = AIVehicleController_FindAgentGroundVehiclePtr(AIVehicleController_Get(), 0, this);
        if (agent != NULL) {
            AIElementController_ForceVehicleToSleep(AIControllerInstance, 0, agent);
            if (scoreable) {
                SMissionManager_IncKills(glbMissionManager, 0, 1);
                Virtual(this, PVehicle::kSetScoreable, &PBondCar::SetScoreable, false);
            }
        }
    }

    // The mission editor's switches, effects 16 to 31. Only bit 0 of each mask is tested, shifted, as the original
    // does.
    for (int effect = 0; effect < 16; effect++) {
        if ((missionEditorOnMask & 1) << effect)
            TriggerFX(renderObject, RSceneObj::kFXOn, 1u << (effect + 16), -1.0f);
        if ((missionEditorOffMask & 1) << effect)
            TriggerFX(renderObject, RSceneObj::kFXSuppress, 1u << (effect + 16), -1.0f);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------------------------------------------

namespace {



// A zone's two shares of damage added, each kept at most its limit (1 and 2)
void AddZoneDamage(DamageZone *zone, float first, float second) {
    float sum = first + zone->unknown00;
    zone->unknown00 = sum < 1.0f ? sum : 1.0f;
    sum = second + zone->unknown04;
    zone->unknown04 = sum < 2.0f ? sum : 2.0f;
}

// The greater of a zone's two shares (the first when they are unordered)
float ZoneLevel(const DamageZone *zone) {
    return zone->unknown00 < zone->unknown04 ? zone->unknown04 : zone->unknown00;
}

}  // namespace

// FUNC_AT(0x0006b6f0)
int PBondCar::ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                          const uint32_t *sourceSig) {
    bool immune = immunity;
    bool tyreShot = tyreBlowOuts && kind == kDamageBullet;
    if (carClass == kPlayerCar || carClass == kCarClass1) {
        SMissionManager_IncPlayerDamage(glbMissionManager, 0, amount);
        if (glbMissionManager->unknown4f0 != 0)
            immune = true;
    }
    if ((kind == kDamageBullet || kind == kDamageKind3) && amount > 1.0f &&
        (carClass == kPlayerCar || carClass == kCarClass1) && glbMissionManager->unknown478 && !immune)
        GHud_TriggerDamageFlash(GHud_TheApp(), 0, amount);
    if (shieldPointLoc != NULL) {
        if (*shieldPointLoc > amount) {
            *shieldPointLoc = *shieldPointLoc - amount;
            return kDamageShielded;
        }
        amount = amount - *shieldPointLoc;
        *shieldPointLoc = 0.0f;
    }

    // Where the segment meets the car's box, in the car's frame, and the damage zone there
    uint32_t step = SimStepCount;
    RSceneObj *render = renderObject;
    CARP::BaseDesc *desc = static_cast<CARP::BaseDesc *>(render->baseDesc);
    Handle *animation = render->animHandle;
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
    Coord4 dimensions;
    render->GetBoundingDimensions(&dimensions);
    float renderOffset = (float)render->GetRenderOffsetVirtual();
    Coord4 localFrom;
    Coord4 localTo;
    VU0_v4sub(from, &body->position, &localFrom);
    VU0_v4sub(to, &body->position, &localTo);
    body->ConvertWorldToLocal(&localFrom);
    body->ConvertWorldToLocal(&localTo);
    Coord4 hit = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!FindOBBIntersect(&dimensions, &localFrom, &localTo, &hit))
        return kDamageMissed;

    bool fromPlayer = false;
    PhysicsObject *source = Simulation_FindPhysicsObjectSignature(Sim, 0, *sourceSig);
    if (source != NULL && source->IsOwnedBy(Simulation_GetPlayerObject(Sim, 0)))
        fromPlayer = true;
    if ((fromPlayer || kind != kDamageBullet) && kind != kDamageKind1 && scoreable && carClass == kCarClass2 &&
        amount >= 5.0f && GetHitPoints() > 0.0f)
        SMissionManager_IncShotsHit(glbMissionManager, 0, kind == kDamageBullet);
    if (unknown2DC < 0.0f)
        unknown2DC = (float)(32.0 / GetHitPoints());

    Coord4 zonePoint = { hit.x, (float)(hit.y * 0.5 + renderOffset), hit.z, 0.0f };
    int zone = desc->CalcDamageZone(&zonePoint.x);
    uint32_t zoneBit = 1u << zone;
    uint32_t zones = zoneBit;

    // A bullet can shoot out the tyre it hits
    if (tyreShot) {
        bool sideHit = fabs(double(Abs(hit.x)) - Abs(dimensions.x)) < fabs(double(Abs(hit.z)) - Abs(dimensions.z));
        double radius = double(BondCar_TYRE_DAMAGE_RADIUS) * physics->wheelRadius;
        float radiusSq = (float)(radius * radius);
        RigidBodyInfo *info = body->info;
        uint32_t points = Ftol(amount);
        for (int wheel = 0; wheel < kCarWheels; wheel++) {
            if (tyreDamagePoints[wheel] == 0)
                continue;
            const Coord4 *lever = &info->levers[wheel];
            Coord4 nearest = *lever;
            if (sideHit) {
                if ((lever->x > 0.0f) != (hit.x > 0.0f))
                    continue;
                nearest.x = hit.x;
            } else {
                bool endHit = fabs(double(hit.z) / hit.x) > fabs(double(dimensions.z) / dimensions.x);
                if ((lever->z > 0.0f) != (hit.z > 0.0f) || !endHit)
                    continue;
                nearest.z = hit.z;
            }
            if (VU0_v3distancesquare(&nearest, &hit) < radiusSq) {
                if (tyreDamagePoints[wheel] > points) {
                    tyreDamagePoints[wheel] -= points;
                } else {
                    tyreDamagePoints[wheel] = 0;
                    animation->ProcessStimuli(kTyreSystemIds[wheel], kStimulus3, SimStepCount, kQueueAll);
                    ABank *bank = fgBanks[4];
                    void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
                    AOneShotSound *blowout = NULL;
                    if (block != NULL) {
                        RigidBody *playerBody = Simulation_GetRigidBody(Sim, 0, PlayerPhysicsObject->rigidBodySlot);
                        int patch = bank->index.Lookup("SFX_TireBlowOut");
                        blowout = AOneShotSound_Construct(block, 0, bank->handle, patch, to, &playerBody->velocity,
                                                          1.0f, "Blowout");
                    }
                    blowout->fxLevel = 1.0f;
                }
                amount = 0.0f;
                break;
            }
        }
    }

    // Hit points, and death
    bool wasDead = GetHitPoints() <= 0.0f;
    if (!immune)
        LoseHitPoints(amount);
    bool killed = physics->subPhysics ? wasDead && kind == kDamageKind1 : GetHitPoints() <= 0.0f;
    if (!wasDead) {
        if (killed && carClass == kCarClass2 && scoreable) {
            SMissionManager_IncKills(glbMissionManager, 0, 1);
            Virtual(this, PVehicle::kSetScoreable, &PBondCar::SetScoreable, false);
            if (targetBeacon != NULL) {
                for (Missile **missile = SimulationMissilesFirst; missile != SimulationMissilesLast; missile++) {
                    if ((*missile)->target == targetBeacon && !(*missile)->unknown70A)
                        SMissionManager_IncShotsHit(glbMissionManager, 0, false);
                }
            }
        }
        // A submarine that dies wanders off and sinks
        if (GetHitPoints() <= 0.0f && physics->subPhysics) {
            if (scoreable) {
                SMissionManager_IncKills(glbMissionManager, 0, 1);
                Virtual(this, PVehicle::kSetScoreable, &PBondCar::SetScoreable, false);
            }
            Virtual(this, PVehicle::kSetVisualDamage, &PBondCar::SetVisualDamage, 1.0f, 1.0f, 1.0f, 1.0f);
            int roll = SimulationRandom->Generate();
            control.steering = (float)(double(roll) * kSteerScale + 0.7f);
            roll = SimulationRandom->Generate();
            if (double(roll) * kRandomScale > 0.5)
                control.steering = -control.steering;
            roll = SimulationRandom->Generate();
            control.gas = 0.5f;
            control.brake = 0.0f;
            control.steeringVertical = (float)((double(roll) * kSteerScale + 0.1f) * 0.3f);
            if (attributes.LookupInt("IS_HELI", NULL)) {
                control.gas = 1.0f;
                RigidBody *moving = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
                RigidBody *placed = Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
                // The engine sound's vtable slot 4
                typedef void (ABaseSound::*Method)(float volume, const Coord3 *position, const Coord3 *velocity);
                (audio->*XbeVirtual<Method>(audio, 4))(1.0f, &placed->position, &moving->velocity);
            }
        }
    }

    // A bullet pushes the car along its path, lifts it and spins it at random
    if (kind == kDamageBullet) {
        float push = (float)(fgDamageImpulseScale * double(amount));
        Coord4 force;
        VU0_v4sub(to, from, &force);
        VU0_v4unitxyz(&force, &force);
        VU0_v4scale(&force, push, &force);
        double lift = double(push) * 0.1f;
        if (force.y > lift)
            lift = force.y;
        force.y = (float)(lift + lift);
        const Coord3 spinOffset = { -0.5f, -0.5f, -0.5f };
        const Coord3 spinScale = { 6000.0f, 7000.0f, 6000.0f };
        uint32_t spinBits = ~SimulationRandom->Generate() << 16;
        spinBits |= SimulationRandom->Generate();
        Coord4 torque;
        BytesToCoordXYZ(spinBits, &spinOffset, &spinScale, NULL, &torque);
        VU0_v4scale(&torque, amount, &torque);
        if (this != Simulation_GetPlayerObject(Sim, 0) || !glbMissionManager->unknown478)
            ApplyForces(XYZ(&force), XYZ(&torque));
    }

    // The damage zones: the damage is split in two shares, scaled by the car's damage scale
    float firstShare = (float)((1.0 - split) * unknown2DC * amount);
    float secondShare = (float)(double(amount) * unknown2DC * split);
    if (killed) {
        animation->ProcessStimuli(kStimulus2, step, kQueueAll);
        animation->ProcessStimuli(kind == kDamageKind3 ? kStimulus7 : kStimulus6, step, kQueueAll);
        for (int stimulus = kStimulusZone4; stimulus >= kStimulusZone1; stimulus--)
            animation->ProcessStimuliZones(stimulus, kAllZones, step, kQueueAll);
        TriggerFX(render, RSceneObj::kFXDamage, kAllZones, -1.0f);
        if (kind == kDamageKind0) {
            for (int i = 0; i < kDamageZoneCount; i++)
                AddZoneDamage(&damageZones[i], firstShare, secondShare);
        } else {
            uint32_t spread = zones | desc->GetZoneBits(zone);
            for (int i = 0; i < kDamageZoneCount; i++) {
                if (spread & (1u << i))
                    AddZoneDamage(&damageZones[i], firstShare, secondShare);
            }
        }
        if (physics->subPhysics) {
            if (attributes.LookupBool("IS_ALPHASUB", NULL)) {
                control.steering = 0.0f;
                control.steeringVertical = 0.0f;
                control.gas = 0.0f;
            } else {
                AIGroundVehicle *ai = Virtual(this, PVehicle::kGetAIGroundVehiclePtr, &PBondCar::GetAIGroundVehiclePtr);
                if (ai != NULL) {
                    AIElementController_ForceVehicleToSleep(AIControllerInstance, 0, ai);
                    Simulation_GetRigidBody(Sim, 0, rigidBodySlot)->velocity = ZeroVector;
                }
            }
        }
    } else {
        bool stimulate = true;
        uint8_t stimulus = 0;
        float total = secondShare + firstShare;
        if (total > fgDamageLevels.impact[0])
            stimulus = kStimulus6;
        else if (total > fgDamageLevels.impact[1])
            stimulus = kStimulus5;
        else if (total > fgDamageLevels.impact[2])
            stimulus = kStimulus4;
        else if (total > fgDamageLevels.impact[3])
            stimulus = kStimulus19;
        else
            stimulate = false;
        if (kind == kDamageKind3 ||
            double(damageZones[zone].unknown00) + damageZones[zone].unknown04 > fgDamageLevels.spread)
            zones |= desc->GetZoneBits(zone);

        // Each share is kept at most its limit, or what the zone already had
        float firstLimit = immune ? 0.5f : 1.0f;
        float secondLimit = firstLimit;
        if (kind == kDamageKind3)
            secondLimit = secondLimit + secondLimit;
        uint32_t levelZones[4] = { 0, 0, 0, 0 };
        uint32_t damagedZones = 0;
        uint32_t effectZones = 0;
        uint32_t changedZones = 0;
        for (int i = 0; i < kDamageZoneCount; i++) {
            uint32_t bit = 1u << i;
            DamageZone *z = &damageZones[i];
            float level;
            if (zones & bit) {
                float firstCap = firstLimit < z->unknown00 ? z->unknown00 : firstLimit;
                float secondCap = secondLimit < z->unknown04 ? z->unknown04 : secondLimit;
                float scale = i == zone ? 1.0f : 0.2f;
                float share = (float)(double(firstShare) * scale + z->unknown00);
                z->unknown00 = share < firstCap ? share : firstCap;
                share = (float)(double(secondShare) * scale + z->unknown04);
                z->unknown04 = share < secondCap ? share : secondCap;
                changedZones |= bit;
                level = ZoneLevel(z);
                for (int j = 4; j > 0; j--) {
                    if (level >= fgDamageLevels.zone[j]) {
                        levelZones[j - 1] |= bit;
                        break;
                    }
                }
            } else {
                level = ZoneLevel(z);
            }
            if (level > fgDamageLevels.zone[2])
                damagedZones |= bit;
            if (level > fgDamageLevels.effects)
                effectZones |= bit;
        }
        for (int j = 0; j < 4; j++) {
            if (levelZones[j] != 0 && !(physics->subPhysics && kStimulusZone1 + j == kStimulusZone4))
                animation->ProcessStimuliZones(kStimulusZone1 + j, uint16_t(levelZones[j]), step, kQueueAll);
        }
        if (stimulate && changedZones != 0)
            animation->ProcessStimuliZones(stimulus, uint16_t(changedZones), step, kQueueAll);
        if (damagedZones != 0)
            TriggerFX(render, RSceneObj::kFXDamage, damagedZones, -1.0f);
        if (effectZones != 0)
            TriggerFX(render, RSceneObj::kFXDamageEffects, effectZones, -1.0f);
    }

    if (killed)
        return kDamageDone;
    bool sideZone = (zoneBit & kSideZones) != 0;
    int roll = SimulationRandom->Generate();
    float chance = (float)(double(roll) * kRandomScale);
    if ((zoneBit & kFrontZones) && Virtual(this, PVehicle::kGetCarClass, &PBondCar::GetCarClass) == kCarClass1 &&
        chance < 0.5f)
        return kDamageFront;
    if (sideZone && chance < 0.5f)
        return kDamageSide;
    return kDamageDone;
}
