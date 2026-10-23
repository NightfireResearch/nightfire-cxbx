#pragma fp_contract(off)

#include "VehicleRender.h"
#include "../engine/GameLoop.h"      // Launch
#include "../platform/XboxXapi.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "BondCar.h"                        // PBondCar::CalculateRPM
#include "EmpEffect.h"
#include "MissionManager.h"       // glbMissionManager
#include "Simulation.h"          // Simulation_GetRigidBody
#include "Vehicle.h"                        // PVehicle, CarPhysics, fgDamageLevels
#include "VehicleParticle.h"
#include "WeaponEffects.h"                  // RMuzzleFlash
#include "../../common/xbeOverload.h"       // XbeVirtual
#include "../../helpers.h"
#include "../anim/AnimEngine.h"             // Handle
#include "../anim/Manager.h"                // IndexedTagGroup
#include "../camera/CameraIniLoader.h"      // fgCameraTables
#include "../camera/PlayerCamera.h"         // RPlayerCamera
#include "../data/AttributeSet.h"
#include "../data/DebugVariables.h"         // dbindex, dbattrib_float
#include "../data/RCARPFile.h"
#include "../data/Tuning.h"                 // TuningDBMgr
#include "../eagl/Model.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"          // MEM_copy, MEM_fill
#include "../render/Lights.h"               // fgLightManager
#include "../render/RenderHigh.h"           // fgRenderHigh
#include "../render/Renderer.h"             // RRenderSharedData, the instance draws
#include "../render/ShadowMap.h"            // TheShadowMap
#include "../render/TimeData.h"             // GameTick
#include "../world/World.h"                 // WorldArticle
#include "../platform/CRuntime.h"
#include "../platform/MethodPointer.h"

// XBE_GLOBAL(0x001f2a50, 0x200)
alignas(16) VehicleSharedBuffers VehicleBuffers;

// ---------------------------------------------------------------------------------------------------------------
// RVehicle, the cars' render object (0x000953e0-0x00096820), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// XAPI's GetLocalTime (the platform layer's)
#define XapiGetLocalTime (reinterpret_cast<void (__stdcall *)(LocalTime *time)>(Xbox_GetLocalTime))

// ---- globals
extern uint8_t WorldViewMade;   // set by RRenderWorldCamera

namespace {

// What GetLocalTime fills (SYSTEMTIME)
struct LocalTime {
    uint16_t year, month, dayOfWeek, day;
    uint16_t hour, minute, second, milliseconds;
};

// A needle's range of values, and the position the range maps to (offset + scale * the fraction)
struct NeedleRange {
    float minimum;
    float maximum;
    float offset;
    float scale;
};
constexpr uint8_t kProcAnimXFormTransform = 0xfe;  // an instance turned by a bone (CARP::Instance::procAnimType)
constexpr int kInitialSwitchTick = -10000;
constexpr float kDefaultIlluminate = 0.5f;
constexpr int kFlashTicks = 5;
constexpr double kFlashFade = 0.75;             // a flash's size, by each tick it has been drawn
constexpr int kLargeFlash = 45;                 // StartMuzzleFlash's type for a kMuzzleSnowmobileTurret flash 3 to 4 in size
constexpr float kLargeFlashBase = 3.0f;
constexpr float kOneIn65536 = 1.0f / 65536.0f;
constexpr float kFlashSizeRange = 0.2f;
constexpr float kFlashSizeBase = 0.6f;
constexpr float kZoneShadeScale = 3.0f;         // a damage zone's z: its second share times this
constexpr float kZoneLit = 255.0f;              // its w above fgDamageLevels.effects
constexpr float kOneThirtieth = 1.0f / 30.0f;   // a switch effect fades over 30 ticks
constexpr float kMaxSteer = 0.075f;
constexpr float kSteerStep = 0.01f;             // the steering's most change in a call
constexpr float kInteriorSteerStep = 0.002f;    // ... the player's in a camera mode inside the car
constexpr float kMaxPitchStep = 0.075f;         // a submarine's fins
constexpr float kMaxRudderStep = 0.1f;
constexpr float kRudderScale = 0.15f;
constexpr float kPitchScale = 0.07f;
constexpr float kSteeringWheelTurn = -750.0f;   // params[0]: the steering wheel's angle per unit of steering
constexpr float kKilometresPerHour = 3.6f;      // from metres a second
constexpr float kSpeedoScale = 1.5f;
constexpr float kOneSixtieth = 1.0f / 60.0f;
constexpr int kClockHours = 12;
constexpr float kShadowRadius = 5.0f;           // Render's view distance radius
constexpr float kNearShadowDistance = 0.125f;
static_assert(std::bit_cast<uint64_t>(kFlashFade) == 0x3fe8000000000000ull &&
              std::bit_cast<uint32_t>(kOneIn65536) == 0x37800000 && std::bit_cast<uint32_t>(kFlashSizeRange) == 0x3e4ccccd &&
              std::bit_cast<uint32_t>(kFlashSizeBase) == 0x3f19999a && std::bit_cast<uint32_t>(kOneThirtieth) == 0x3d088889 &&
              std::bit_cast<uint32_t>(kMaxSteer) == 0x3d99999a && std::bit_cast<uint32_t>(kSteerStep) == 0x3c23d70a &&
              std::bit_cast<uint32_t>(kInteriorSteerStep) == 0x3b03126f && std::bit_cast<uint32_t>(kMaxRudderStep) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(kRudderScale) == 0x3e19999a && std::bit_cast<uint32_t>(kPitchScale) == 0x3d8f5c29 &&
              std::bit_cast<uint32_t>(kKilometresPerHour) == 0x40666666 && std::bit_cast<uint32_t>(kOneSixtieth) == 0x3c888889,
              "the original's constants");

// The tags PostLoad and StartColourDissolve look for
constexpr uint32_t kTagArticles = 0x41727469;               // 'Arti'
constexpr uint32_t kTagArticle = 0x61732020;                // 'as  '
constexpr uint32_t kTagColourVariation = 0x64692020;        // 'id  '

// XBE_GLOBAL(0x001c46b8, 0x40)
const char *const SwitchEffectNames[RVehicle::kSwitchEffects] = {
    "Booster [0]", "Headlights [1]", "Brakelights [2]", "Left Signal [3]", "Right Signal [4]", "Reverse [5]",
    "Smokescreen [6]", "Dashboard [7]", "Always On [8]", "Left Button [9]", "Right Button [10]", "Misc [11]",
    "Misc [12]", "Misc [13]", "Misc [14]", "Misc [15]",
};
// XBE_GLOBAL(0x001c46fc, 0x10)
const char *const WheelBoneNames[RVehicle::kWheels] = {
    "leftfrontwheel", "rightfrontwheel", "leftrearwheel", "rightrearwheel",
};
// XBE_GLOBAL(0x001c470c, 0x10)
const char *const FinBoneNames[RVehicle::kWheels] = {
    "leftfin", "rightfin", "leftrudder", "rightrudder",
};
// XBE_GLOBAL(0x001c471c, 0x10)
const char *const HubBoneNames[RVehicle::kWheels] = {
    "leftfronthub", "rightfronthub", "leftrearhub", "rightrearhub",
};
// XBE_GLOBAL(0x001c4730, 0x70)
const NeedleRange NeedleRanges[7] = {
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 330.0f, 120.0f, -225.0f },          // kNeedleSpeed
    { 0.0f, 7000.0f, 121.0f, -210.0f },         // kNeedleRevs
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 12.0f, 0.0f, -360.0f },             // kNeedleHours
    { 0.0f, 60.0f, 0.0f, -360.0f },             // kNeedleMinutes
};

}  // namespace

// XBE_GLOBAL(0x001f2c68, 4)
uint32_t SwitchEffectIndex;
// XBE_GLOBAL(0x001f2c6c, 4)
int32_t ClearEmpVictim;

namespace {

#if NIGHTFIRE_X87
// The C runtime's pow (_CIpow at 0x00132a94, the game's own): the base below the exponent, answered on the x87;
// times factor there, before the power is rounded
__declspec(naked) double CrtPowTimes(double, double, double factor) {
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        call Crt::CIpow
        fmul qword ptr [esp + 20]
        ret
    }
}
#else
// The C runtime's pow (_CIpow at 0x00132a94, the game's own), times factor
double CrtPowTimes(double base, double exponent, double factor) {
    return Crt::CIpow(base, exponent) * factor;
}
#endif

// ---- the car's virtual methods (PBondCar's)
DamageZone *GetDamageZones(PhysicsObject *object, uint32_t *count) {
    return (object->*XbeVirtual<decltype(&PhysicsObject::GetDamageZones)>(object, 3))(count);
}

// The particles' scalar deleting destructor, their vtable's slot 0
void DeleteParticles(RVehicleParticle *particles) {
    typedef void *(METHOD_CALL *DeleteFn)(RVehicleParticle *, METHOD_EDX unsigned flags);
    DeleteFn deleteParticles = reinterpret_cast<DeleteFn>((*reinterpret_cast<void ***>(particles))[0]);
    deleteParticles(particles, METHOD_EDX_ARG 1);
}

// The mode the player's first camera is in
const CameraModeInfo &PlayerCameraMode() {
    return fgCameraTables.modes[fgRenderHigh->views[0].camera->cameraMode];
}

// The player's first camera looks from the bumper, or is on its way back to a mode that does
bool PlayerInBumperView() {
    RPlayerCamera *camera = fgRenderHigh->views[0].camera;
    if (fgCameraTables.modes[camera->cameraMode].type == kCameraBumper)
        return true;
    return camera->unknown13C > 0 && fgCameraTables.modes[camera->lastSelectableCameraMode].type == kCameraBumper;
}

}  // namespace

// ---- construction

// FUNC_AT(0x000953e0)
RVehicle* RVehicle::Construct(PVehicle *car) {
    RSkeletalObj::Construct(&instance);
    vtable = reinterpret_cast<RSceneObj_vtbl *>(kRVehicleVtable);
    steer = 0.0f;
    pitch = 0.0f;
    colourDissolveTick = 0;
    viewDrawCount = 0;
    rudder = 0.0f;
    viewDrawList = NULL;
    colourDissolve = NULL;
    shadowModel = NULL;
    renderType4Model = NULL;
    particles = NULL;
    empShootingIntensity = 0.0f;
    empVictim = 0;
    empBolts = NULL;
    flags |= kVehicle;
    if (car != NULL) {
        void *block = UMemory::FastAlloc(sizeof(RVehicleParticle), "RVehicleParticle");
        particles = block != NULL ? static_cast<RVehicleParticle *>(block)->Construct(car) : NULL;
    }
    for (int i = 0; i < kMuzzles; i++) {
        muzzlePosition[i].w = 1.0f;
        muzzleDirection[i].w = 0.0f;
        muzzleEndTick[i] = 0;
    }
    for (int i = 0; i < kSwitchEffects; i++) {
        switchIntensity[i] = 0.0f;
        switchStartTick[i] = kInitialSwitchTick;
    }
    switchIntensity[8] = 1.0f;
    switchIntensity[7] = 1.0f;
    switchIntensity[11] = 1.0f;
    switchIntensity[12] = 1.0f;
    switchIntensity[13] = 1.0f;
    switchIntensity[14] = 1.0f;
    switchIntensity[15] = 1.0f;
    VU0_MATRIX4Init(&instance);
    for (int i = 0; i < kWheels; i++) {
        wheelBone[i] = 0;
        hubBone[i] = 0;
    }
    flags |= kDrawLast;
    return this;
}

// FUNC_AT(0x00096430)
void RVehicle::Destruct() {
    vtable = reinterpret_cast<RSceneObj_vtbl *>(kRVehicleVtable);
    if (particles != NULL)
        DeleteParticles(particles);
    if (empBolts != NULL) {
        REmpBolts *bolts = empBolts;
        bolts->Destruct();
        OperatorDelete(bolts);
        empBolts = NULL;
    }
    RSkeletalObj::Destruct();
}

// FUNC_AT(0x000967f0)
RVehicle* RVehicle::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RVehicle));
    return this;
}

// ---- loading

// FUNC_AT(0x00095560)
void* RVehicle::ResolveObjectData(const char *name, bool *found) {
    *found = false;
    if (strcmp(name, "EAGL::DamageZones") == 0) {
        *found = true;
        return damageZones;
    }
    return RSceneObj::ResolveObjectData(name, found);
}

// FUNC_AT(0x000955b0)
void RVehicle::SetViewDrawList(uint32_t index) {
    viewDrawList = static_cast<const uint16_t *>(GetViewDrawList(index, &viewDrawCount));
}

// The wheels are found by their bones (a model without a skeleton gets four, one per wheel) and the wheel
// instances by their proc-anim index. The search through the instances goes on for each wheel from where the
// last one stopped, as the original's does.
// FUNC_AT(0x000955d0)
void RVehicle::PostLoad() {
    RSkeletalObj::PostLoad();
    bool madeSkeleton = false;
    if (GetNumBones() == 0) {
        AllocateSkeleton(kWheels);
        madeSkeleton = true;
    }
    const CARP::BaseDesc *desc = Desc();
    float bottom = (float)(desc->height - (double(desc->bboxMax[1]) - desc->bboxMin[1]));
    uint32_t count = animHandle->GetInstanceCount();
    CARP::Instance *next = animHandle->GetInstances();
    for (int wheel = 0; wheel < kWheels; wheel++) {
        hubBone[wheel] = GetBoneIndex(HubBoneNames[wheel]);
        if (physics->type == kObjectVehicle && static_cast<PVehicle *>(physics)->GetPhysics()->isSub)
            hubBone[wheel] = GetBoneIndex(FinBoneNames[wheel]);
        wheelBone[wheel] = GetBoneIndex(WheelBoneNames[wheel]);
        wheelHeight[wheel] = 0.0f;
        if (madeSkeleton)
            wheelBone[wheel] = wheel + 1;
        for (uint32_t i = 0; i < count; i++) {
            const CARP::Instance *candidate = next++;
            if (candidate->procAnimType == kProcAnimXFormTransform && candidate->procAnimIndex == wheelBone[wheel]) {
                wheelHeight[wheel] = candidate->position[1] - bottom;
                break;
            }
        }
    }

    shadowModel = NULL;
    renderType4Model = NULL;
    if (physics != NULL && !(physics->flags & PhysicsObject::kSimpleBody)) {
        UGroup *articles = static_cast<IndexedTagGroup *>(carpFile->root)->LocateTag(kTagArticles, -1);
        for (UData *item = articles->DataLocateFirst(kTagArticle, 0, uint32_t(-1));
             item != articles->DataEnd() && item->MatchTag() == kTagArticle; item++) {
            WorldArticle *article = reinterpret_cast<WorldArticle *>(item->Data());
            if (article->renderType == WorldArticle::kRenderType3)
                shadowModel = article->lods[0].model;
            if (article->renderType == WorldArticle::kRenderType4)
                renderType4Model = article->lods[0].model;
        }
    }
}

// ---- the shared buffers

// FUNC_AT(0x000958a0)
void RVehicle::InitSharedBuffers() {
    for (int i = 0; i < kSwitchEffects; i++) {
        SwitchEffectData &effect = VehicleBuffers.switches[i];
        effect.vScroll = 0.0f;
        effect.glow = 0.0f;
        effect.illuminate = kDefaultIlluminate;
        effect.intensity = 0.0f;
    }
    TuningDBMgr->LoadDatabase("Render:SwitchEffects", Launch.missionName, 0, false);
    dbindex("Switch Effect", &SwitchEffectIndex, 0, kSwitchEffects - 1, SwitchEffectNames);
    dbattrib_float("V Scroll", &VehicleBuffers.switches[0].vScroll, 0.0f, 1.0f, sizeof(SwitchEffectData), 1.0f, NULL);
    dbattrib_float("Glow", &VehicleBuffers.switches[0].glow, 0.0f, 1.0f, sizeof(SwitchEffectData), 1.0f, NULL);
    dbattrib_float("Illuminate", &VehicleBuffers.switches[0].illuminate, 0.0f, 1.0f, sizeof(SwitchEffectData), 1.0f,
                   NULL);
    dbendindex();
    TuningDBMgr->CloseCurrent();
}

// FUNC_AT(0x00095970)
void RVehicle::ClearSharedBuffers() {
    for (int i = 0; i < kSwitchEffects; i++)
        VehicleBuffers.switches[i].intensity = 0.0f;
    MEM_fill(VehicleBuffers.damageZones, 0, sizeof(VehicleBuffers.damageZones));
}

float SwitchEffectFade(uint32_t now, int32_t start) {
    if (start <= 0)
        return 0.0f;
    int32_t elapsed = int32_t(now - start);
    if (!(elapsed > 0))
        return 0.0f;
    float fade = (float)(1.0 - double(elapsed) * kOneThirtieth);
    return 0.0f < fade ? fade : 0.0f;
}

// FUNC_AT(0x00095ff0)
void RVehicle::SetPerObjectBuffers() {
    uint32_t now = GameTick;
    switchIntensity[kSwitchLeftButton] = SwitchEffectFade(now, switchStartTick[kSwitchLeftButton]);
    switchIntensity[kSwitchRightButton] = SwitchEffectFade(now, switchStartTick[kSwitchRightButton]);
    for (int i = 0; i < kSwitchEffects; i++)
        VehicleBuffers.switches[i].intensity = switchIntensity[i];
    MEM_copy(VehicleBuffers.damageZones, damageZones, sizeof(damageZones));
}

// ---- effects

// FUNC_AT(0x000959b0)
void RVehicle::TriggerIlluminate(int index, int32_t tick, int) {
    switchStartTick[index] = tick;
    switchIntensity[index] = 1.0f;
}

// FUNC_AT(0x000959d0)
void RVehicle::SetEMPVictimEffect(bool on) {
    empVictim = on;
    empVictimTick = fgSimulation.stepCount;
}

// FUNC_AT(0x000959f0)
void RVehicle::TriggerTurboBoostSmoke(int ticks) {
    if (particles != NULL)
        particles->TriggerTireSmoke(ticks);
}

// FUNC_AT(0x00095a10)
void RVehicle::TriggerEmpBolts() {
    if (empBolts == NULL) {
        void *block = OperatorNew(sizeof(REmpBolts));
        empBolts = block != NULL ? static_cast<REmpBolts *>(block)->Construct(this) : NULL;
    }
}

// FUNC_AT(0x00095ce0)
void RVehicle::StartMuzzleFlash(const Coord3 *position, const Coord3 *direction, int muzzle, int type) {
    if (muzzleEndTick[muzzle] > GameTick)
        return;
    muzzlePosition[muzzle].x = position->x;
    muzzlePosition[muzzle].y = position->y;
    muzzlePosition[muzzle].z = position->z;
    muzzleDirection[muzzle].x = direction->x;
    muzzleDirection[muzzle].y = direction->y;
    muzzleDirection[muzzle].z = direction->z;
    muzzleEndTick[muzzle] = GameTick + kFlashTicks;
    bool hasTurret = false;
    if (physics->type == kObjectVehicle)
        hasTurret = physics->attributes.LookupBool("HAS_TURRET", NULL);
    if (type == kLargeFlash) {
        uint32_t random = RandomShort();
        muzzleType[muzzle] = kMuzzleSnowmobileTurret;
        muzzleSize[muzzle] = (float)(double(int32_t(random)) * kOneIn65536 + kLargeFlashBase);
    } else if (hasTurret) {
        muzzleSize[muzzle] = (float)(RandomScaled(kFlashSizeRange) + kFlashSizeBase);
        muzzleType[muzzle] =
            static_cast<PVehicle *>(physics)->GetPhysics()->isSnowmobile != 0 ? kMuzzleSnowmobileTurret : kMuzzleTurret;
    } else {
        muzzleSize[muzzle] = (float)(RandomScaled(kFlashSizeRange) + kFlashSizeBase);
        muzzleType[muzzle] = kMuzzlePOV;
    }
}

// FUNC_AT(0x00095e20)
void RVehicle::StartColourDissolve(uint32_t, uint32_t) {
    colourDissolve = NULL;
    UGroup *model = ModelGroup();
    UData *item = model->DataLocateFirst(kTagColourVariation, uint32_t(variation) << 8, uint32_t(-1));
    if (item != model->DataEnd() && item->MatchTag() == kTagColourVariation &&
        uint32_t(item->TagIndex()) >> 8 == variation) {
        colourDissolve = reinterpret_cast<uint32_t *>(item->Data());
        colourDissolveTick = GameTick;
        SetVariation(colourDissolve[0]);
    }
}

// FUNC_AT(0x00095ed0)
void RVehicle::TriggerFX(int type, uint32_t which, uint32_t extra, const Coord3 *point, const Coord3 *direction,
                         float intensity) {
    switch (type) {
    case kFXColourDissolve:
        StartColourDissolve(which, extra);
        return;
    case kFXSparks:
        if (particles != NULL) {
            particles->TriggerSparks(point, *direction, 1.0f, which);
            return;
        }
        break;
    case kFXMuzzleFlash:
        StartMuzzleFlash(point, direction, which, extra);
        return;
    case kFXSwitchOn:
        switchIntensity[which] = 1.0f;
        return;
    case kFXSwitchOff:
        switchIntensity[which] = 0.0f;
        return;
    }
    RSceneObj::TriggerFX(type, which, extra, point, direction, intensity);
}

// FUNC_AT(0x00095b60)
void RVehicle::RenderFX(const MATRIX4 *transform) {
    for (int i = 0; i < kMuzzles; i++) {
        if (muzzleEndTick[i] > GameTick) {
            uint32_t ticksDrawn = GameTick - muzzleEndTick[i] + kFlashTicks;
            float size = (float)CrtPowTimes(kFlashFade, double(ticksDrawn), muzzleSize[i]);
            if (muzzleType[i] == kMuzzlePOV)
                RMuzzleFlash::DrawPOV(transform, &muzzlePosition[i], 0, size);
            else
                RMuzzleFlash::Draw(transform, &muzzlePosition[i], &muzzleDirection[i], muzzleType[i], size);
        }
    }
    if (particles != NULL)
        particles->UpdateCar(true);
    if (empVictim || ClearEmpVictim == 1)
        empVictim = 0;
    if (empShootingIntensity > 0.0f)
        REmp::DrawShootingEffect(transform, empShootingIntensity);
    RRenderSharedData::SendPerViewPort();
    RenderEffects(false);
}

// ---- drawing

// FUNC_AT(0x00095a80)
void RVehicle::ComputeDamage() {
    if (physics == NULL) {
        for (int i = 0; i < kDamageZones; i++) {
            damageZones[i].x = 0.0f;
            damageZones[i].y = 0.0f;
            damageZones[i].z = 0.0f;
            damageZones[i].w = 0.0f;
        }
        return;
    }
    uint32_t count;
    DamageZone *zones = GetDamageZones(physics, &count);
    for (uint32_t i = 0; i < count; i++) {
        Coord4 &zone = damageZones[i];
        zone.x = zones[i].unknown00;
        zone.y = zones[i].unknown04;
        zone.z = zones[i].unknown04 * kZoneShadeScale;
        zone.w = zone.x > fgDamageLevels.effects ? kZoneLit : 0.0f;
    }
}

// FUNC_AT(0x00095b30)
void RVehicle::RenderShadow() {
    if (shadowModel != NULL)
        TheShadowMap->Draw(this, &fgLightManager->specularLight);
    else
        TheShadowMap->ClearShadowMap();
}

// FUNC_AT(0x00095c60)
double RVehicle::GetNeedlePos(int needle, float value) {
    const NeedleRange &range = NeedleRanges[needle];
    if (value < range.minimum)
        value = range.minimum;
    if (value > range.maximum)
        value = range.maximum;
    return (double(value) - range.minimum) / (double(range.maximum) - range.minimum) * range.scale + range.offset;
}

// FUNC_AT(0x00095cc0)
double RVehicle::GetRenderOffset() {
    const CARP::BaseDesc *desc = Desc();
    double half = double(desc->height) * 0.5f;
    return half + (half - (double(desc->bboxMax[1]) - desc->bboxMin[1]) * 0.5f);
}

// Each wheel's hub turns in y by the steering (a submarine's fins and rudders by its controls) and drops by the
// suspension; the wheel turns in x by its axle's spin, under the hub. A bone index of 0 turns a scratch matrix.
// FUNC_AT(0x00096060)
float RVehicle::ComputeWheels() {
    PVehicle *car = static_cast<PVehicle *>(physics);
    if (car == NULL || car->type != kObjectVehicle)
        return 0.0f;
    float spin[kWheels];
    spin[0] = spin[1] = car->GetCarWheelSpinAngle(0);
    spin[2] = spin[3] = car->GetCarWheelSpinAngle(1);

    float steerStep = kSteerStep;
    if ((car->GetCarClass() == kPlayerCar || car->GetCarClass() == kCarClass1) && PlayerCameraMode().interiorView)
        steerStep = kInteriorSteerStep;
    float steering = car->GetCarSteer();
    if (!(steering < kMaxSteer))
        steering = kMaxSteer;
    else if (steering <= -kMaxSteer)
        steering = -kMaxSteer;
    double change = double(steering) - steer;
    if (change > steerStep)
        steering = steerStep + steer;
    if (change < -steerStep)
        steering = steer - steerStep;

    float yaw[kWheels] = { steering, -steering, 0.0f, 0.0f };
    float negativeSteering = -steering;
    if (car->GetPhysics()->isSub) {
        BondCarControl control;
        float vertical = car->GetCarControl(&control)->steeringVertical;
        float verticalChange = vertical - pitch;
        float brake = car->GetCarControl(&control)->brake;
        double turn = car->IsReversing() ? 0.0f : brake;
        float turnChange = (float)(turn - rudder);
        if (verticalChange > kMaxPitchStep)
            vertical = pitch + kMaxPitchStep;
        if (verticalChange < -kMaxPitchStep)
            vertical = pitch - kMaxPitchStep;
        // the rudder's limited turn stays unrounded for the fins
        if (turnChange > kMaxRudderStep)
            turn = double(rudder) + kMaxRudderStep;
        if (turnChange < -kMaxRudderStep)
            turn = double(rudder) - kMaxRudderStep;
        rudder = (float)turn;
        pitch = vertical;
        double rudderTurn = turn * kRudderScale;
        double pitchTurn = double(vertical) * kPitchScale;
        yaw[0] = (float)(steering + pitchTurn);
        yaw[1] = (float)(pitchTurn + yaw[1]);
        yaw[2] = (float)(negativeSteering - rudderTurn);
        yaw[3] = (float)(rudderTurn - steering);
    }
    steer = steering;

    float compressionLimit = car->GetPhysics()->springCompressionLimitDraw;
    float restLength = car->GetPhysics()->springRestLength;
    float wheelRadius = car->GetPhysics()->wheelRadius;
    for (int wheel = 0; wheel < kWheels; wheel++) {
        alignas(16) MATRIX4 hubScratch;
        alignas(16) MATRIX4 wheelScratch;
        MATRIX4 *hub = hubBone[wheel] == 0 ? &hubScratch : &bones[hubBone[wheel] - 1];
        VU0_MATRIX4setyrot(hub, yaw[wheel]);
        float compression = car->GetSuspensionCompression(wheel);
        float drop = compressionLimit < compression ? compressionLimit : compression;
        hub->mtx[3][1] = (float)(((double(wheelRadius) + drop) - wheelHeight[wheel]) - restLength);
        MATRIX4 *rim = wheelBone[wheel] == 0 ? &wheelScratch : &bones[wheelBone[wheel] - 1];
        VU0_MATRIX4setxrot(rim, spin[wheel]);
        VU0_MATRIX4_mult(rim, rim, hub);
    }
    return yaw[0];
}

// FUNC_AT(0x00096410)
void RVehicle::RenderShadowGeometry() {
    if (shadowModel != NULL)
        shadowModel->Draw(shadowModel->matrix);
}

// The player's car also moves its dashboard: the speedometer (zero when not moving forward), the rev counter and
// the clock's hands (the host's local time).
// FUNC_AT(0x000964b0)
void RVehicle::UpdatePosition(bool update) {
    RSkeletalObj::UpdatePosition(update);
    params[0] = (float)(double(ComputeWheels()) * kSteeringWheelTurn);
    PhysicsObject *car = physics;
    if (car != fgSimulation.cars.first[0])
        return;
    float speed = (float)(double(static_cast<PVehicle *>(car)->GetCarSpeed()) * kKilometresPerHour * kSpeedoScale);
    RigidBody *body = fgSimulation.GetRigidBody(car->rigidBodySlot);
    RigidBody *moving = fgSimulation.GetRigidBody(car->rigidBodySlot);
    if (v3dotprod(&moving->velocity, body->info->orientation.mtx[2]) <= 0.0f)
        speed = 0.0f;
    params[kNeedleSpeed] = (float)GetNeedlePos(kNeedleSpeed, speed);
    params[kNeedleRevs] = (float)GetNeedlePos(kNeedleRevs, (float)static_cast<PBondCar *>(car)->CalculateRPM());
    LocalTime time;
    XapiGetLocalTime(&time);
    params[kNeedleHours] = (float)GetNeedlePos(kNeedleHours, float(time.hour % kClockHours));
    int32_t second = time.second;
    int32_t minute = time.minute;
    params[kNeedleMinutes] = (float)GetNeedlePos(kNeedleMinutes, (float)(double(second) * kOneSixtieth + minute));
}

// The player's car is not drawn from its bumper camera. The shadow: none with projected shadows on for the
// player's car; a simple shadow (unless NO_SHADOW) once the world's view is made or the mission asks for it;
// otherwise the projected one, when the car is that near.
// FUNC_AT(0x000965e0)
void RVehicle::Render() {
    bool isPlayer = physics == fgSimulation.cars.first[0];
    bool simpleShadow = WorldViewMade || (isPlayer && glbMissionManager->autoDrive);
    if (isPlayer && PlayerInBumperView())
        return;
    alignas(16) MATRIX4 transform;
    GetTransform(&transform);
    float distance = (float)GetArticleViewDistance(reinterpret_cast<const CARP::Instance *>(&transform), kShadowRadius);
    if (!(RShadowMap::ProjectedShadowsEnabled() && isPlayer)) {
        if (simpleShadow) {
            if (physics != NULL && !physics->attributes.LookupBool("NO_SHADOW", NULL))
                TheShadowMap->DrawSimpleShadow(this);
        } else if (distance < kNearShadowDistance) {
            RenderShadowVirtual();
        }
    }
    ComputeDamage();
    RRenderSharedData::SendPerViewPort();
    SetProcAnimStateVirtual();
    if (viewDrawList != NULL)
        DrawIndexedInstanceList(animHandle->Instances(), viewDrawList, &transform, animHandle->states, viewDrawCount,
                                distance, this);
    else
        DrawInstanceList(animHandle->Instances(), &transform, animHandle->states, animHandle->instanceCount,
                         distance, this);
    RRenderSharedData::MarkAsDirty();
    if (empBolts != NULL)
        empBolts->Draw();
}

// FUNC_AT(0x00096770)
void RVehicle::RenderDeferredEffects() {
    if (physics == fgSimulation.cars.first[0] && PlayerInBumperView())
        return;
    alignas(16) MATRIX4 transform;
    GetTransform(&transform);
    RenderFX(&transform);
}
