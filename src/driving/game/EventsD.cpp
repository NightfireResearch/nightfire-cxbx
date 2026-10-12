#pragma fp_contract(off)

#include "EventsD.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>               // sprintf
#include <string.h>              // _stricmp, strcpy

#include <bit>
#include <mmintrin.h>            // _mm_empty

#include "../Scheduler.hpp"             // Scheduler
#include "../audio/Bank.h"              // ABank, fgBanks
#include "../audio/Fader.h"             // AFader
#include "../audio/Sound.h"             // ABaseSound, AOneShotSound, LimitedSoundCount
#include "../audio/SoundManager.h"      // ASoundManager, StreamNameList
#include "../audio/Stream.h"            // AStream
#include "../data/Carp.h"               // CARP::Instance, CARP::TagReference
#include "../eagl/RenderContext.h"      // EAGL::RenderContext
#include "../eagl/View.h"               // EAGL::ViewPort
#include "../engine/ActionQueue.hpp"    // ActionQueueManager
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/Feedback.h"         // IFeedback
#include "../engine/GameInterfaces.hpp" // GHud, GFX, GSubtitles
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/IOModule.hpp"       // IOModule
#include "../engine/OBB.h"              // OBB
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix, Util_CollideRayWithSphere
#include "../engine/PlayMPC.hpp"        // PlayMPC
#include "../engine/UGroup.h"           // UGroup, UData
#include "../engine/UMemory.hpp"        // UMemory::FastAlloc
#include "../anim/Actor.h"              // ActorDatabase
#include "../physics/Newton.h"          // Newton::SpawnFromEvent
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"       // RigidBody, PhysicsObjects, SimpleBodyOwners, DamageSourceSig
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"       // the VU0 and D3DX routines
#include "../platform/RealSystem.h"     // TIMER_gettick
#include "../platform/X87.h"            // Ftol
#include "../render/Lights.h"           // fgLightManager
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../render/Renderer.h"         // fgRenderer
#include "../render/RSceneObj.hpp"      // RSceneObj, RAutonomousObj
#include "../world/CollisionInstance.h" // WTrigger
#include "../world/Grid.h"              // WGrid
#include "../world/RoadNav.h"           // WRoadNav
#include "../world/Trigger.h"           // gEventDynamicData
#include "../world/TriggerManager.h"    // fgTriggerManager
#include "../world/World.h"             // fgWorld, ArticleEffect
#include "../world/WorldPos.h"          // WWorldPos
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "AIViews.h"
#include "Human.h"
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle, CarPhysics
#include "VehicleSound.h"               // AVehicle
#include "WeaponEffects.h"              // RScorchWorld
#include "WeaponManager.h"

// ---------------------------------------------------------------------------------------------------------------
// The events of package D (0x00044400-0x000472c0, 0x00048ac0-0x00048ec0 and 0x0004db10-0x0004f170; Event's own
// destructor at 0x00043640 is EventManager.cpp's): their MakeEvent and ResolveEvent callbacks, their constructors,
// their destructors (where each does its work, in Process) and their scalar deleting destructors. Ported from the
// listing; the AI and front-end calls go to the originals through game/AIViews.h and engine/GameInterfaces.hpp.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

#define fgScheduler (*(Scheduler **)0x001e520c)
#define IdentityMatrix (*(const MATRIX4 **)0x001c4654)
#define gStreams BOOL8_AT(0x001b7056)
#define gPlayMovies BOOL8_AT(0x001b7057)
#define gSubtitles BOOL8_AT(0x001e476d)
#define fgFailedStreams (*(StreamNameList **)0x00243a5c)
#define AudioClock FLOAT_AT(0x00243a6c)
// The C runtime's _clearfp (the game's)
#define CRT_clearfp ((unsigned (*)())0x00133c9e)

// The ticks each of the explosions' four sound slots may play again at, the slot to try first, and the next of the
// three small explosion effects (written only here; the names are ours)
// XBE_GLOBAL(0x001d8058, 0xc)
char StingerChannel[12] = "Speech";
// XBE_GLOBAL(0x001e23b4, 0x10)
int32_t ExplosionSoundDue[4];
// XBE_GLOBAL(0x001e23c4, 0x4)
uint32_t ExplosionSoundSlot;
// XBE_GLOBAL(0x001e23c8, 0x4)
uint32_t ExplosionEffectSlot;

// ---- the vtables (the game's; each has the one slot, the scalar deleting destructor)

#define kESetRecordedGameTimeVtable ((vtable_Event *)0x0018c730)
#define kESetReflectiveObjectVtable ((vtable_Event *)0x0018c734)
#define kESetRenderVariationVtable ((vtable_Event *)0x0018c738)
#define kESetSimRateVtable ((vtable_Event *)0x0018c73c)
#define kESetSpecialWeaponVtable ((vtable_Event *)0x0018c740)
#define kESetSubRollDirectionVtable ((vtable_Event *)0x0018c744)
#define kESetSubtitleVtable ((vtable_Event *)0x0018c748)
#define kESetTerrainVtable ((vtable_Event *)0x0018c74c)
#define kESetTimerVtable ((vtable_Event *)0x0018c750)
#define kESetVideoVtable ((vtable_Event *)0x0018c754)
#define kESetVisualDamageVtable ((vtable_Event *)0x0018c7a0)
#define kESetWaypointVtable ((vtable_Event *)0x0018c7a4)
#define kEShakeVtable ((vtable_Event *)0x0018c7a8)
#define kEShowInstanceVtable ((vtable_Event *)0x0018c7ac)
#define kEShowInstanceStaticVtable ((vtable_Event *)0x0018c7b0)
#define kESimEndFrameVtable ((vtable_Event *)0x0018c7b4)
#define kESimFrameUpdateVtable ((vtable_Event *)0x0018c7b8)
#define kESpawnDramaticSmackableVtable ((vtable_Event *)0x0018c7bc)
#define kESpawnExplosionVtable ((vtable_Event *)0x0018c7c0)
#define kESpawnExplosionStaticVtable ((vtable_Event *)0x0018c7c4)
#define kESpawnForceEffectVtable ((vtable_Event *)0x0018c7c8)
#define kESpawnSentryVtable ((vtable_Event *)0x0018c7e0)
#define kESpawnSimplePhysicsVtable ((vtable_Event *)0x0018c7e4)
#define kESpawnSmackableVtable ((vtable_Event *)0x0018c7e8)
#define kESpawnTrafficVtable ((vtable_Event *)0x0018c7ec)
#define kEStartEffectVtable ((vtable_Event *)0x0018c7f0)
#define kEStartMissionVtable ((vtable_Event *)0x0018c7f4)
#define kEStartTimedActionVtable ((vtable_Event *)0x0018c7f8)
#define kEStartTimerVtable ((vtable_Event *)0x0018c7fc)
#define kEStopEffectVtable ((vtable_Event *)0x0018c800)
#define kEStopSoundPosVtable ((vtable_Event *)0x0018c804)
#define kEStopTimedActionVtable ((vtable_Event *)0x0018c808)
#define kEStopTimerVtable ((vtable_Event *)0x0018c80c)
#define kEStreamEventVtable ((vtable_Event *)0x0018c810)
#define kESuppressEffectVtable ((vtable_Event *)0x0018c814)
#define kESwitchEffectOffVtable ((vtable_Event *)0x0018c818)
#define kESwitchEffectOnVtable ((vtable_Event *)0x0018c81c)
#define kESwitchStingerChannelVtable ((vtable_Event *)0x0018c820)
#define kETargetBeaconOnOffVtable ((vtable_Event *)0x0018c824)
#define kETimerDrawOffVtable ((vtable_Event *)0x0018c828)
#define kETimerDrawOnVtable ((vtable_Event *)0x0018c82c)
#define kETwoWheelsOffVtable ((vtable_Event *)0x0018c830)
#define kETwoWheelsOnVtable ((vtable_Event *)0x0018c834)
#define kEUnloadWeaponVtable ((vtable_Event *)0x0018c838)
#define kEWakeupSmackableVtable ((vtable_Event *)0x0018c83c)

#define kRAutonomousObjVtable ((RSceneObj_vtbl *)0x0018a398)
#define kALimitedSoundVtable 0x0018c8c8u

namespace {


constexpr float kMaxSimRate = 4.0f;
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<uint32_t>(kPercent) == 0x3c23d70a, "0.01");
constexpr float kOneSixtieth = 1.0f / 60.0f;
static_assert(std::bit_cast<uint32_t>(kOneSixtieth) == 0x3c888889, "1/60");
constexpr float kOneTenth = 0.1f;
static_assert(std::bit_cast<uint32_t>(kOneTenth) == 0x3dcccccd, "0.1");
constexpr float kUpright = 0.9f;            // a direction this close to up builds its basis from x instead
static_assert(std::bit_cast<uint32_t>(kUpright) == 0x3f666666, "0.9");
constexpr float kMinDistance = 0.0001f;
static_assert(std::bit_cast<uint32_t>(kMinDistance) == 0x38d1b717, "0.0001");

// The zero vector the game hands TriggerFX and GFX::Trigger, and (0, 0, 0, 1), the identity rotation
const Coord4 kZero = { 0.0f, 0.0f, 0.0f, 0.0f };
const Coord4 kNoRotation = { 0.0f, 0.0f, 0.0f, 1.0f };

// The CARP tag of a model's instance lists ('in  '; with a variation, the variation in the low half)
constexpr uint32_t kTagIn = 0x696e2020;
constexpr uint32_t kTagTypeMask = 0xffff0000;
constexpr uint32_t kNoVariation = 0xffffffff;
constexpr uint32_t kTagName = 0x4e616d65;   // 'Name'

// The mission's timer whose running ticks (EStartTimer); the HUD's waypoint slot
constexpr int kTickingTimer = 1;
constexpr int kWaypointSlot = 1;

// An AI element's flag ETargetBeaconOnOff sets and clears when it has no vehicle to command (the name is ours)
constexpr uint8_t kElementBeacon = 0x08;

// ESpawnTraffic's orders, and the frames a spawned car is left alone (AIGroundVehicle::SetAttackMode's and
// SetNavigateMode's modes)
constexpr uint8_t kTrafficAttackMode = 12;
constexpr uint8_t kTrafficNavigateMode = 3;
constexpr int kTrafficSettleSeconds = 5;

// The smackables' spawn: the Simulation's fourth argument in seconds, and the random spin's range
constexpr int kSmackableSeconds = 30;
const Coord4 kSpinOffset = { -0.5f, -0.5f, -0.5f, 0.0f };
const Coord4 kSpinScale = { 400000.0f, 400000.0f, 400000.0f, 0.0f };

// EWakeupSmackable's newton, and the ray shell's box and damage
constexpr float kNewtonMass = 1.0f;
constexpr float kNewtonLifetime = 20.0f;
constexpr float kRayHalfWidth = 0.25f;
constexpr float kRayDamageScale = 0.25f;
constexpr int kRayDamageKind = 2;

// The explosions: the force explosion's type, the type passed with it, the AI zone they register, the scorch, and
// the distances of the jolt
constexpr uint32_t kForceExplosionType = 6;
constexpr uint32_t kForceExplosionKind = 2;
constexpr int kExplosionZone = 3;
constexpr float kScorchSize = 8.0f;
constexpr int kScorchType = 5;
constexpr float kJoltRange = 20.0f;
constexpr float kJoltDistance = 10.0f;
constexpr float kLargeForce = 600.0f;
constexpr float kMediumForce = 200.0f;
constexpr uint32_t kFODHitDamage = 0x1e;
constexpr uint32_t kSurfaceSnow = 9;
constexpr uint32_t kSurfaceWater = 6;

// The explosion sounds: their bank slot, the ticks a slot waits, their distances (times 5 on "jungleb_mis13b")
constexpr int kExplosionBankSlot = 4;
constexpr int kExplosionSoundTicks = 30;
constexpr float kNearMinDistance = 100.0f;
constexpr float kNearMaxDistance = 400.0f;
constexpr float kFarMinDistance = 1000.0f;
constexpr float kFarMaxDistance = 5000.0f;

const char *const kExplosionSounds[4] = { "SFX_HugeExp", "SFX_LrgExp", "SFX_MedExp", "SFX_SmallExp" };
const char *const kGroundExplosions[3] = { "Ex_SmallD", "Ex_SmallD2", "Ex_SmallD3" };
const char *const kSmallExplosions[3] = { "Ex_Small", "Ex_Small2", "Ex_Small3" };
const char *const kPlayerExplosions[3] = { "Ex_SmB00", "Ex_SmB01", "Ex_SmB02" };

// The movies' viewport
constexpr float kMovieMinZ = 0.01f;
constexpr float kMovieMaxZ = 1.0f;
constexpr float kMovieNearZ = 0.0f;
constexpr float kMovieFarZ = 200.0f;
constexpr int kMaxMovieVolume = 127;

PVehicle *PlayerCar() {
    return fgSimulation.cars.first[0];
}

Coord4 &Row(MATRIX4 &m, int row) {
    return *reinterpret_cast<Coord4 *>(m.mtx[row]);
}

// The owner of the body an event's data names (the Simulation's tables), NULL for none
PhysicsObject *BodyOwner(bool simple, int slot) {
    return simple ? fgSimulation.simpleRigidBodyOwners[slot] : fgSimulation.rigidBodyOwners[slot];
}

// A basis whose row 2 is `direction`: rows 0 and 1 from crossing it with up (0, 1, 0), or with x (1, 0, 0) when it is
// within the dot product kUpright of up (taken over all four words), row 3 (0, 0, 0, 1). The two explosions build
// their rotation from it.
void BasisFromDirection(MATRIX4 *m, const Coord4 *direction) {
    Coord4 &row0 = Row(*m, 0);
    Coord4 &row1 = Row(*m, 1);
    Coord4 &row2 = Row(*m, 2);
    row1 = { 0.0f, 1.0f, 0.0f, 1.0f };
    row2 = *direction;
    if (v4dotprod(direction, &row1) > kUpright) {
        row0 = { 1.0f, 0.0f, 0.0f, 1.0f };
        VU0_v4unitcrossprod1(&row2, &row0, &row1);
        VU0_v4unitcrossprod1(&row1, &row2, &row0);
    } else {
        VU0_v4unitcrossprod1(&row1, &row2, &row0);
        VU0_v4unitcrossprod1(&row2, &row0, &row1);
    }
    Row(*m, 3) = { 0.0f, 0.0f, 0.0f, 1.0f };
}

} // namespace

// =============================================================================================================
// The mission's settings
// =============================================================================================================

// FUNC_AT(0x00044400)
void ESetRecordedGameTime::Destruct() {
    vtable = kESetRecordedGameTimeVtable;
    Process();
    vtable = kEventVtable;
}

void ESetRecordedGameTime::Process() {
    glbMissionManager->missionEndTime = Ftol(double(fgSimulation.stepCount) * fgSimulation.timeStep);
}

// FUNC_AT(0x00048ac0)
ESetRecordedGameTime* ESetRecordedGameTime::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044430)
void ESetRecordedGameTime_MakeEvent_Callback(const void *) {
    ESetRecordedGameTime *event = new ESetRecordedGameTime;
    if (event != NULL)
        event->vtable = kESetRecordedGameTimeVtable;
}

// FUNC_AT(0x00044450)
void ESetReflectiveObject::Destruct() {
    vtable = kESetReflectiveObjectVtable;
    Process();
    vtable = kEventVtable;
}

// The signature of the element's object (none if it has none), of its character's body, or of the player's car
void ESetReflectiveObject::Process() {
    uint32_t signature = 0;
    uint32_t result;
    if (element == NULL) {
        signature = *fgSimulation.GetPlayerObject()->GetSig(&result);
    } else if (element->type == CARP::kAIElementVehicle) {
        if (element->object != NULL)
            signature = *element->object->GetSig(&result);
    } else if (element->type == CARP::kAIElementCharacter) {
        signature = *element->characterObject->GetSig(&result);
    }
    fgSimulation.SetReflectiveObject(signature);
}

// FUNC_AT(0x00048ae0)
ESetReflectiveObject* ESetReflectiveObject::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000444f0)
void ESetReflectiveObject_MakeEvent_Callback(const ESetReflectiveObject::Record *record) {
    ESetReflectiveObject *event = new ESetReflectiveObject;
    if (event != NULL) {
        event->vtable = kESetReflectiveObjectVtable;
        event->element = record->element;
    }
}

// FUNC_AT(0x00044510)
void ESetRenderVariation::Destruct() {
    vtable = kESetRenderVariationVtable;
    Process();
    vtable = kEventVtable;
}

void ESetRenderVariation::Process() {
    if (instance == NULL)
        return;
    CARP::BaseDesc *desc = reinterpret_cast<CARP::BaseDesc *>(uintptr_t(instance->articleDesc.value));
    if (desc == NULL)
        return;
    // the model's reference leads to a pointer to its group
    UGroup *model = *reinterpret_cast<UGroup **>(uintptr_t(desc->model.value));
    UData *list = model->DataLocateTag(variation == kNoVariation ? kTagIn : (variation | (kTagIn & kTagTypeMask)));
    if (list == model->DataEnd())
        return;
    RSceneObj *sceneObj = fgWorld->GetSceneObjFromInstance(instance);
    if (sceneObj != NULL) {
        sceneObj->UseArticle(model, variation);
    } else {
        const CARP::Instance *first = reinterpret_cast<const CARP::Instance *>(list->Data());
        instance->articleDesc = first->articleDesc;
    }
}

// FUNC_AT(0x00048b00)
ESetRenderVariation* ESetRenderVariation::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000445e0)
void ESetRenderVariation_MakeEvent_Callback(const ESetRenderVariation::Record *record) {
    ESetRenderVariation *event = new ESetRenderVariation;
    if (event != NULL) {
        event->vtable = kESetRenderVariationVtable;
        event->instance = record->instance;
        event->variation = record->variation;
    }
}

// FUNC_AT(0x00044610)
void ESetSimRate::Destruct() {
    vtable = kESetSimRateVtable;
    Process();
    vtable = kEventVtable;
}

void ESetSimRate::Process() {
    float scale = rate;
    if (scale > kMaxSimRate)
        scale = kMaxSimRate;
    fgScheduler->timeScale = scale;
}

// FUNC_AT(0x00048b20)
ESetSimRate* ESetSimRate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044640)
void ESetSimRate_MakeEvent_Callback(const ESetSimRate::Record *record) {
    ESetSimRate *event = new ESetSimRate;
    if (event != NULL) {
        event->vtable = kESetSimRateVtable;
        event->rate = record->rate;
    }
}

// FUNC_AT(0x00044660)
void ESetSpecialWeapon::Destruct() {
    vtable = kESetSpecialWeaponVtable;
    Process();
    vtable = kEventVtable;
}

void ESetSpecialWeapon::Process() {
    WeaponManager->SetSpecialWeapon(weapon, trigger, range);
}

// FUNC_AT(0x00048b40)
ESetSpecialWeapon* ESetSpecialWeapon::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000446c0)
void ESetSpecialWeapon_MakeEvent_Callback(const ESetSpecialWeapon::Record *record) {
    ESetSpecialWeapon *event = new ESetSpecialWeapon;
    if (event != NULL) {
        event->vtable = kESetSpecialWeaponVtable;
        event->weapon = record->weapon;
        event->trigger = record->trigger;
        event->range = record->range;
    }
}

// FUNC_AT(0x000446f0)
void ESetSpecialWeapon_ResolveEvent_Callback(ESetSpecialWeapon::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->trigger)->Construct(shared, parent);
}

// FUNC_AT(0x0004db10)
void ESetSubRollDirection::Destruct() {
    vtable = kESetSubRollDirectionVtable;
    Process();
    vtable = kEventVtable;
}

void ESetSubRollDirection::Process() {
    PlayerCar()->ForceRollDirection(uint8_t(direction));
}

// FUNC_AT(0x0004ee80)
ESetSubRollDirection* ESetSubRollDirection::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044740)
void ESetSubRollDirection_MakeEvent_Callback(const ESetSubRollDirection::Record *record) {
    ESetSubRollDirection *event = new ESetSubRollDirection;
    if (event != NULL) {
        event->vtable = kESetSubRollDirectionVtable;
        event->direction = record->direction;
    }
}

// FUNC_AT(0x00044760)
void ESetSubtitle::Destruct() {
    vtable = kESetSubtitleVtable;
    Process();
    vtable = kEventVtable;
}

void ESetSubtitle::Process() {
    if (gSubtitles && !glbMissionManager->IsSkippingCinematic() && GHud::TheApp() != NULL)
        GSubtitles::TheApp()->DisplaySubtitle(text, time);
}

// FUNC_AT(0x00048b60)
ESetSubtitle* ESetSubtitle::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000447e0)
void ESetSubtitle_MakeEvent_Callback(const ESetSubtitle::Record *record) {
    ESetSubtitle *event = new ESetSubtitle;
    if (event != NULL) {
        event->vtable = kESetSubtitleVtable;
        event->text = record->text;
        event->time = record->time;
    }
}

// FUNC_AT(0x0004db70)
void ESetTerrain::Destruct() {
    vtable = kESetTerrainVtable;
    Process();
    vtable = kEventVtable;
}

void ESetTerrain::Process() {
    PlayerCar()->GetAudio()->terrain = int16_t(terrain);
}

// FUNC_AT(0x0004eea0)
ESetTerrain* ESetTerrain::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044810)
void ESetTerrain_MakeEvent_Callback(const ESetTerrain::Record *record) {
    ESetTerrain *event = new ESetTerrain;
    if (event != NULL) {
        event->vtable = kESetTerrainVtable;
        event->terrain = record->terrain;
    }
}

// FUNC_AT(0x0004dbd0)
void ESetVisualDamage::Destruct() {
    vtable = kESetVisualDamageVtable;
    Process();
    vtable = kEventVtable;
}

void ESetVisualDamage::Process() {
    float fraction0 = float(double(damage[0]) * kPercent);
    float fraction1 = float(double(damage[1]) * kPercent);
    float fraction2 = float(double(damage[2]) * kPercent);
    float fraction3 = float(double(damage[3]) * kPercent);
    if (element != NULL) {
        CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
        if (redirected->type == CARP::kAIElementVehicle && !redirected->unknown77)
            (static_cast<PVehicle *>(redirected->object))->SetVisualDamage(fraction0, fraction1, fraction2, fraction3);
    } else {
        PlayerCar()->SetVisualDamage(fraction0, fraction1, fraction2, fraction3);
    }
}

// FUNC_AT(0x0004eec0)
ESetVisualDamage* ESetVisualDamage::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044be0)
void ESetVisualDamage_MakeEvent_Callback(const ESetVisualDamage::Record *record) {
    ESetVisualDamage *event = new ESetVisualDamage;
    if (event != NULL) {
        event->vtable = kESetVisualDamageVtable;
        event->element = record->element;
        event->damage[0] = record->damage[0];
        event->damage[1] = record->damage[1];
        event->damage[2] = record->damage[2];
        event->damage[3] = record->damage[3];
    }
}

// FUNC_AT(0x00044c20)
void ESetWaypoint::Destruct() {
    vtable = kESetWaypointVtable;
    Process();
    vtable = kEventVtable;
}

void ESetWaypoint::Process() {
    if (marker != NULL)
        glbMissionManager->SetObjective(kWaypointSlot, marker);
    else if (target != NULL)
        glbMissionManager->SetObjective(kWaypointSlot, target);
    else if (element != NULL)
        glbMissionManager->SetObjective(kWaypointSlot, element);
    else
        glbMissionManager->ClearObjective(kWaypointSlot);
}

// FUNC_AT(0x00048bc0)
ESetWaypoint* ESetWaypoint::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044cb0)
void ESetWaypoint_MakeEvent_Callback(const ESetWaypoint::Record *record) {
    ESetWaypoint *event = new ESetWaypoint;
    if (event != NULL) {
        event->vtable = kESetWaypointVtable;
        event->marker = record->marker;
        event->target = record->target;
        event->element = record->element;
    }
}

// =============================================================================================================
// The timers
// =============================================================================================================

// FUNC_AT(0x00044830)
ESetTimer* ESetTimer::Construct(int timer, float seconds, int show) {
    this->seconds = seconds;
    this->show = show;
    vtable = kESetTimerVtable;
    this->timer = timer;
    glbMissionManager->timers[timer].Reset(Ftol(double(fgSimulation.stepsPerSecond) * seconds + 1.0));
    return this;
}

// FUNC_AT(0x000448b0)
void ESetTimer::Destruct() {
    vtable = kESetTimerVtable;
    Process();
    vtable = kEventVtable;
}

void ESetTimer::Process() {
    if (show != 0)
        GHud::TheApp()->TriggerTimerOn(timer);
}

// FUNC_AT(0x00048b80)
ESetTimer* ESetTimer::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044910)
void ESetTimer_MakeEvent_Callback(const ESetTimer::Record *record) {
    ESetTimer *event = new ESetTimer;
    if (event != NULL)
        event->Construct(record->timer, record->seconds, record->show);
}

// FUNC_AT(0x00046290)
EStartTimer* EStartTimer::Construct(int timer, int rate) {
    vtable = kEStartTimerVtable;
    this->timer = timer;
    this->rate = rate;
    glbMissionManager->timers[timer].Start(rate);
    if (timer == kTickingTimer)
        glbMissionManager->timers[timer].SetTicking(true);
    return this;
}

// FUNC_AT(0x00046320)
void EStartTimer_MakeEvent_Callback(const EStartTimer::Record *record) {
    EStartTimer *event = new EStartTimer;
    if (event != NULL)
        event->Construct(record->timer, record->rate);
}

// FUNC_AT(0x00046620)
EStopTimer* EStopTimer::Construct(int timer) {
    this->timer = timer;
    vtable = kEStopTimerVtable;
    glbMissionManager->timers[timer].Stop();
    return this;
}

// FUNC_AT(0x00046680)
void EStopTimer_MakeEvent_Callback(const EStopTimer::Record *record) {
    EStopTimer *event = new EStopTimer;
    if (event != NULL)
        event->Construct(record->timer);
}

// FUNC_AT(0x000461b0)
EStartTimedAction* EStartTimedAction::Construct(int action, float timeLimit, float fullPointsTime, int minPoints,
                                                int maxPoints) {
    this->fullPointsTime = fullPointsTime;
    this->timeLimit = timeLimit;
    this->minPoints = minPoints;
    this->maxPoints = maxPoints;
    vtable = kEStartTimedActionVtable;
    this->action = action;
    glbMissionManager->StartTimedAction(action, fullPointsTime, timeLimit, minPoints, maxPoints);
    return this;
}

// FUNC_AT(0x00046230)
void EStartTimedAction_MakeEvent_Callback(const EStartTimedAction::Record *record) {
    EStartTimedAction *event = new EStartTimedAction;
    if (event != NULL)
        event->Construct(record->action, record->timeLimit, record->fullPointsTime, record->minPoints,
                         record->maxPoints);
}

// FUNC_AT(0x000465b0)
void EStopTimedAction::Destruct() {
    vtable = kEStopTimedActionVtable;
    Process();
    vtable = kEventVtable;
}

void EStopTimedAction::Process() {
    glbMissionManager->CompleteTimedAction(action);
}

// FUNC_AT(0x00048d80)
EStopTimedAction* EStopTimedAction::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046600)
void EStopTimedAction_MakeEvent_Callback(const EStopTimedAction::Record *record) {
    EStopTimedAction *event = new EStopTimedAction;
    if (event != NULL) {
        event->vtable = kEStopTimedActionVtable;
        event->action = record->action;
    }
}

// FUNC_AT(0x00046ae0)
void ETimerDrawOff::Destruct() {
    vtable = kETimerDrawOffVtable;
    Process();
    vtable = kEventVtable;
}

void ETimerDrawOff::Process() {
    GHud::TheApp()->TriggerTimerOff(timer);
}

// FUNC_AT(0x00048e40)
ETimerDrawOff* ETimerDrawOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046b40)
void ETimerDrawOff_MakeEvent_Callback(const ETimerDrawOff::Record *record) {
    ETimerDrawOff *event = new ETimerDrawOff;
    if (event != NULL) {
        event->vtable = kETimerDrawOffVtable;
        event->timer = record->timer;
    }
}

// FUNC_AT(0x00046b60)
void ETimerDrawOn::Destruct() {
    vtable = kETimerDrawOnVtable;
    Process();
    vtable = kEventVtable;
}

void ETimerDrawOn::Process() {
    GHud::TheApp()->TriggerTimerOn(timer);
}

// FUNC_AT(0x00048e60)
ETimerDrawOn* ETimerDrawOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046bc0)
void ETimerDrawOn_MakeEvent_Callback(const ETimerDrawOn::Record *record) {
    ETimerDrawOn *event = new ETimerDrawOn;
    if (event != NULL) {
        event->vtable = kETimerDrawOnVtable;
        event->timer = record->timer;
    }
}

// =============================================================================================================
// Movies, streams and stingers
// =============================================================================================================

// FUNC_AT(0x00044970)
void ESetVideo::Destruct() {
    vtable = kESetVideoVtable;
    Process();
    vtable = kEventVtable;
}

void ESetVideo::Process() {
    GLoadingScreen_Status("*** ESetVideo enter...\n");
    if (gPlayMovies && movie != NULL) {
        char path[0x80];
        sprintf(path, "%s\\%s", GLocale::GetSubDirectory(), movie);
        EAGL::RenderContext *context = fgRenderer->renderContext;
        PlayMPC player;
        player.Construct(fgRenderer->device, context, Launch.controllerPort, 0);
        player.Init(path, Launch.effectsVolume * kMaxMovieVolume / 100, false);
        EAGL::ViewPort *view = context->NewViewPort();
        view->SetShape(0.0f, 0.0f, float(fgRenderer->screenWidth), float(fgRenderer->screenHeight), kMovieMinZ,
                       kMovieMaxZ);
        view->SetOrthographic(kMovieNearZ, kMovieFarZ);
        IOModule::GetIOModule()->EnableUpdating(false);
        IFeedback::Pause();
        ActionQueueManager::GetActionQueueManager()->FlushAllQueues();
        ASoundManager::Pause();
        if (subtitles != NULL && gSubtitles) {
            char file[0x80];
            sprintf(file, "data\\loading\\%s", subtitles);
            GSubtitles::TheApp()->LoadSubtitles(file);
            player.Play(view, fgRenderer->widescreen != 0, GSubtitles_DrawFrame);
            GSubtitles::TheApp()->ClearSubtitles();
        } else {
            player.Play(view, fgRenderer->widescreen != 0, NULL);
        }
        context->DeleteViewPort(view);
        player.Destruct();
        IOModule::GetIOModule()->resyncDevices = true;
        IOModule::GetIOModule()->EnableUpdating(true);
        ActionQueueManager::GetActionQueueManager()->FlushAllQueues();
        ASoundManager::Resume();
        _mm_empty();
        CRT_clearfp();
    }
    GLoadingScreen_Status("*** ESetVideo exit...\n");
}

// FUNC_AT(0x00048ba0)
ESetVideo* ESetVideo::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044bb0)
void ESetVideo_MakeEvent_Callback(const ESetVideo::Record *record) {
    ESetVideo *event = new ESetVideo;
    if (event != NULL) {
        event->vtable = kESetVideoVtable;
        event->movie = record->movie;
        event->subtitles = record->subtitles;
    }
}

// FUNC_AT(0x0004ef60)
void EStreamEvent::Destruct() {
    vtable = kEStreamEventVtable;
    Process();
    vtable = kEventVtable;
}

// As in the original, the stream's name is compared with "nis" before it is tested for NULL.
void EStreamEvent::Process() {
    if (!gStreams)
        return;
    if (onlyAlive != 0 && glbMissionManager->GetPlayerHealth(true) <= 0.0)
        return;
    bool nis = _stricmp(stream, "nis") == 0;
    bool play = true;
    if (glbMissionManager->GoingToSkipCinematic())
        play = false;
    if (stream == NULL || name == NULL)
        return;
    bool failed = false;
    for (PointerListNode *node = fgFailedStreams->Begin(); node != fgFailedStreams->head; node = node->next)
        if (_stricmp(name, static_cast<const char *>(node->value)) == 0)
            failed = true;
    if (failed)
        return;
    if (_stricmp(stream, "speech") == 0) {
        if (play)
            AFader::Get("SpeechVsAmbience")->QueuePrimary(name, float(fadeTime), false, held != 0);
    } else if (nis) {
        AFader::Get("SpeechVsAmbience")->QueuePrimary(name, 0.0f, nis, held != 0);
    } else {
        AStream::Get(stream)->Event(name, float(fadeTime), loop != 0, false, held != 0);
    }
}

// FUNC_AT(0x0004f150)
EStreamEvent* EStreamEvent::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000466d0)
void EStreamEvent_MakeEvent_Callback(const EStreamEvent::Record *record) {
    EStreamEvent *event = new EStreamEvent;
    if (event != NULL) {
        event->vtable = kEStreamEventVtable;
        event->stream = record->stream;
        event->name = record->name;
        event->fadeTime = record->fadeTime;
        event->loop = record->loop;
        event->onlyAlive = record->onlyAlive;
        event->held = record->held;
    }
}

// FUNC_AT(0x00046990)
void ESwitchStingerChannel::Destruct() {
    vtable = kESwitchStingerChannelVtable;
    Process();
    vtable = kEventVtable;
}

void ESwitchStingerChannel::Process() {
    if (_stricmp(channel, "music") == 0 || _stricmp(channel, "speech") == 0)
        strcpy(StingerChannel, channel);
}

// FUNC_AT(0x00048e00)
ESwitchStingerChannel* ESwitchStingerChannel::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000469f0)
void ESwitchStingerChannel_MakeEvent_Callback(const ESwitchStingerChannel::Record *record) {
    ESwitchStingerChannel *event = new ESwitchStingerChannel;
    if (event != NULL) {
        event->vtable = kESwitchStingerChannelVtable;
        event->channel = record->channel;
    }
}

// FUNC_AT(0x00046540)
void EStopSoundPos::Destruct() {
    vtable = kEStopSoundPosVtable;
    Process();
    vtable = kEventVtable;
}

void EStopSoundPos::Process() {
    ASoundManager::StopSoundPos(position);
}

// FUNC_AT(0x00048d60)
EStopSoundPos* EStopSoundPos::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046590)
void EStopSoundPos_MakeEvent_Callback(const EStopSoundPos::Record *record) {
    EStopSoundPos *event = new EStopSoundPos;
    if (event != NULL) {
        event->vtable = kEStopSoundPosVtable;
        event->position = record->position;
    }
}

// =============================================================================================================
// The player's car
// =============================================================================================================

// FUNC_AT(0x0004dcb0)
void EShake::Destruct() {
    vtable = kEShakeVtable;
    Process();
    vtable = kEventVtable;
}

void EShake::Process() {
    PlayerCar()->feedbackObject->ApplyJolt(1.0f);
}

// FUNC_AT(0x0004eee0)
EShake* EShake::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044ce0)
void EShake_MakeEvent_Callback(const void *) {
    EShake *event = new EShake;
    if (event != NULL)
        event->vtable = kEShakeVtable;
}

// FUNC_AT(0x0004e9c0)
void ETwoWheelsOff::Destruct() {
    vtable = kETwoWheelsOffVtable;
    Process();
    vtable = kEventVtable;
}

void ETwoWheelsOff::Process() {
    PlayerCar()->DisableTwoWheelStunt();
}

// FUNC_AT(0x0004f110)
ETwoWheelsOff* ETwoWheelsOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046be0)
void ETwoWheelsOff_MakeEvent_Callback(const void *) {
    ETwoWheelsOff *event = new ETwoWheelsOff;
    if (event != NULL)
        event->vtable = kETwoWheelsOffVtable;
}

// FUNC_AT(0x0004ea10)
void ETwoWheelsOn::Destruct() {
    vtable = kETwoWheelsOnVtable;
    Process();
    vtable = kEventVtable;
}

void ETwoWheelsOn::Process() {
    PlayerCar()->EnableTwoWheelStunt();
}

// FUNC_AT(0x0004f130)
ETwoWheelsOn* ETwoWheelsOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046c00)
void ETwoWheelsOn_MakeEvent_Callback(const void *) {
    ETwoWheelsOn *event = new ETwoWheelsOn;
    if (event != NULL)
        event->vtable = kETwoWheelsOnVtable;
}

// FUNC_AT(0x00046c20)
void EUnloadWeapon::Destruct() {
    vtable = kEUnloadWeaponVtable;
    Process();
    vtable = kEventVtable;
}

void EUnloadWeapon::Process() {
    WeaponManager->UnloadWeapon(weapon);
}

// FUNC_AT(0x00048e80)
EUnloadWeapon* EUnloadWeapon::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046c70)
void EUnloadWeapon_MakeEvent_Callback(const EUnloadWeapon::Record *record) {
    EUnloadWeapon *event = new EUnloadWeapon;
    if (event != NULL) {
        event->vtable = kEUnloadWeaponVtable;
        event->weapon = record->weapon;
    }
}

// FUNC_AT(0x0004e860)
void EStartMission::Destruct() {
    vtable = kEStartMissionVtable;
    Process();
    vtable = kEventVtable;
}

void EStartMission::Process() {
    Coord4 position = { trigger->position.x, trigger->position.y, trigger->position.z, 1.0f };
    Coord4 direction = { trigger->forward.x, trigger->forward.y, trigger->forward.z, 1.0f };
    fgSimulation.SetStartConditions(Xyz(&position), Xyz(&direction));
    PlayerCar()->ResetCar(Xyz(&position), Xyz(&direction));
    Coord4 momentum = { direction.x, direction.y, direction.z, 0.0f };
    float scale = fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot)->mass * speed;
    VU0_v4unitxyz(&momentum, &momentum);
    VU0_v4scale(&momentum, scale, &momentum);
    fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot)->momentum = *Xyz(&momentum);
    IOModule::GetIOModule()->resendAll = true;
}

// FUNC_AT(0x0004ef40)
EStartMission* EStartMission::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046180)
void EStartMission_MakeEvent_Callback(const EStartMission::Record *record) {
    EStartMission *event = new EStartMission;
    if (event != NULL) {
        event->vtable = kEStartMissionVtable;
        event->trigger = gEventDynamicData.trigger;
        event->speed = record->speed;
    }
}

// =============================================================================================================
// The simulation's frame
// =============================================================================================================

// FUNC_AT(0x0004dd10)
void ESimFrameUpdate::Destruct() {
    vtable = kESimFrameUpdateVtable;
    Process();
    vtable = kEventVtable;
}

void ESimFrameUpdate::Process() {
    fgSimulation.DeleteDeadObjects();
    RigidBody::ResetRigidBodySPThunk();
    fgSimulation.AdvanceStep();
    fgSimulation.SimulateGame();
    if (fgSimulation.simState == kSimPaused)
        PlayerCar()->GetControllerInput();
    fgAIElementController->Update();
    if (ActorDatabase != NULL)
        ActorDatabase->UpdateAll();
    glbMissionManager->Update();
    WeaponManager->Update();
    fgRenderHigh->ReceiveCameraInput();
    if (fgSimulation.simState != kSimPaused)
        fgTriggerManager->Update();
    WGrid::UpdateDynamicNodes();
    if (fgSimulation.simState != kSimPaused) {
        FramesSinceAudioUpdate++;
        AudioClock += kOneSixtieth;
    }
    NullFunction();     // the Simulation's end of frame
}

// FUNC_AT(0x0004ef00)
ESimFrameUpdate* ESimFrameUpdate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044e60)
void ESimFrameUpdate_MakeEvent_Callback(const void *) {
    ESimFrameUpdate *event = new ESimFrameUpdate;
    if (event != NULL)
        event->vtable = kESimFrameUpdateVtable;
}

// FUNC_AT(0x00044df0)
void ESimEndFrame::Destruct() {
    vtable = kESimEndFrameVtable;
    Process();
    vtable = kEventVtable;
}

void ESimEndFrame::Process() {
    NullFunction();     // the Simulation's end of frame
}

// FUNC_AT(0x00048c20)
ESimEndFrame* ESimEndFrame::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044e40)
void ESimEndFrame_MakeEvent_Callback(const void *) {
    ESimEndFrame *event = new ESimEndFrame;
    if (event != NULL)
        event->vtable = kESimEndFrameVtable;
}

// =============================================================================================================
// Instances
// =============================================================================================================

// FUNC_AT(0x00044d00)
void EShowInstance::Destruct() {
    vtable = kEShowInstanceVtable;
    Process();
    vtable = kEventVtable;
}

void EShowInstance::Process() {
    instance->flags &= ~kWorldInstanceSceneObj;
}

// FUNC_AT(0x00048be0)
EShowInstance* EShowInstance::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044d20)
void EShowInstance_MakeEvent_Callback(const void *) {
    EShowInstance *event = new EShowInstance;
    if (event != NULL) {
        event->vtable = kEShowInstanceVtable;
        event->instance = gEventDynamicData.instance;
    }
}

// FUNC_AT(0x00044d40)
void EShowInstanceStatic::Destruct() {
    vtable = kEShowInstanceStaticVtable;
    Process();
    vtable = kEventVtable;
}

void EShowInstanceStatic::Process() {
    if (instance == NULL)
        return;
    RSceneObj *object = fgWorld->GetSceneObjFromInstance(instance);
    if (object != NULL)
        object->Show();
    else if (sceneObj != NULL)
        instance = sceneObj->ConvertInstanceToLocal(instance);
    instance->flags &= ~kWorldInstanceSceneObj;
}

// FUNC_AT(0x00048c00)
EShowInstanceStatic* EShowInstanceStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044dc0)
void EShowInstanceStatic_MakeEvent_Callback(const EShowInstanceStatic::Record *record) {
    EShowInstanceStatic *event = new EShowInstanceStatic;
    if (event != NULL) {
        event->vtable = kEShowInstanceStaticVtable;
        event->instance = record->instance;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// =============================================================================================================
// The scene objects' effects
// =============================================================================================================

void EEffectEvent::Trigger(int typeById, int typeByZone) {
    int type;
    uint32_t which;
    if (effect != NULL) {
        which = effect->id;
        type = typeById;
    } else {
        which = 1u << ((zone + 16) & 31);   // the shift's count taken mod 32, as the CPU does
        type = typeByZone;
    }
    if (sceneObj == NULL) {
        if (unknown0c != NULL && ref != NULL) {
            sceneObj = ref->sceneObj;
        } else if (bodySlot != -1) {
            PhysicsObject *owner = BodyOwner(bySimpleBody, bodySlot);
            if (owner != NULL)
                sceneObj = owner->renderObject;
        }
    }
    if (sceneObj != NULL)
        sceneObj->TriggerFXVirtual(type, which, 0, &kZero, &kZero, -1.0f);
}

namespace {

void MakeEffectEvent(EEffectEvent *event, vtable_Event *vtable, const EEffectEvent::Record *record) {
    event->vtable = vtable;
    event->effect = record->effect;
    event->sceneObj = gEventDynamicData.sceneObj;
    event->unknown0c = record->unknown08;
    event->ref = record->ref;
    event->bySimpleBody = gEventDynamicData.flag;
    event->bodySlot = gEventDynamicData.index;
    event->zone = record->zone;
}

} // namespace

// FUNC_AT(0x00046050)
void EStartEffect::Destruct() {
    vtable = kEStartEffectVtable;
    Process();
    vtable = kEventVtable;
}

void EStartEffect::Process() {
    Trigger(RSceneObj::kFXOnById, RSceneObj::kFXOn);
}

// FUNC_AT(0x00048d20)
EStartEffect* EStartEffect::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046120)
void EStartEffect_MakeEvent_Callback(const EEffectEvent::Record *record) {
    EStartEffect *event = new EStartEffect;
    if (event != NULL)
        MakeEffectEvent(event, kEStartEffectVtable, record);
}

// FUNC_AT(0x00046380)
void EStopEffect::Destruct() {
    vtable = kEStopEffectVtable;
    Process();
    vtable = kEventVtable;
}

void EStopEffect::Process() {
    Trigger(RSceneObj::kFXOffById, RSceneObj::kFXOff);
}

// FUNC_AT(0x00048d40)
EStopEffect* EStopEffect::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046450)
void EStopEffect_MakeEvent_Callback(const EEffectEvent::Record *record) {
    EStopEffect *event = new EStopEffect;
    if (event != NULL)
        MakeEffectEvent(event, kEStopEffectVtable, record);
}

// FUNC_AT(0x000464b0)
void EStopEffect_ResolveEvent_Callback(EEffectEvent::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->effect)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->unknown08)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->ref)->Construct(shared, parent);
}

// FUNC_AT(0x00046720)
void ESuppressEffect::Destruct() {
    vtable = kESuppressEffectVtable;
    Process();
    vtable = kEventVtable;
}

void ESuppressEffect::Process() {
    Trigger(RSceneObj::kFXSuppressById, RSceneObj::kFXSuppress);
}

// FUNC_AT(0x00048da0)
ESuppressEffect* ESuppressEffect::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000467f0)
void ESuppressEffect_MakeEvent_Callback(const EEffectEvent::Record *record) {
    ESuppressEffect *event = new ESuppressEffect;
    if (event != NULL)
        MakeEffectEvent(event, kESuppressEffectVtable, record);
}

// FUNC_AT(0x00046850)
void ESwitchEffectOff::Destruct() {
    vtable = kESwitchEffectOffVtable;
    Process();
    vtable = kEventVtable;
}

void ESwitchEffectOff::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->type == CARP::kAIElementVehicle)
        static_cast<PVehicle *>(redirected->object)->MissionEditorSwitch(false, which);
}

// FUNC_AT(0x00048dc0)
ESwitchEffectOff* ESwitchEffectOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000468c0)
void ESwitchEffectOff_MakeEvent_Callback(const ESwitchEffectOff::Record *record) {
    ESwitchEffectOff *event = new ESwitchEffectOff;
    if (event != NULL) {
        event->vtable = kESwitchEffectOffVtable;
        event->element = record->element;
        event->which = record->which;
    }
}

// FUNC_AT(0x000468f0)
void ESwitchEffectOn::Destruct() {
    vtable = kESwitchEffectOnVtable;
    Process();
    vtable = kEventVtable;
}

void ESwitchEffectOn::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->type == CARP::kAIElementVehicle)
        static_cast<PVehicle *>(redirected->object)->MissionEditorSwitch(true, which);
}

// FUNC_AT(0x00048de0)
ESwitchEffectOn* ESwitchEffectOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046960)
void ESwitchEffectOn_MakeEvent_Callback(const ESwitchEffectOn::Record *record) {
    ESwitchEffectOn *event = new ESwitchEffectOn;
    if (event != NULL) {
        event->vtable = kESwitchEffectOnVtable;
        event->element = record->element;
        event->which = record->which;
    }
}

// FUNC_AT(0x00046a10)
void ETargetBeaconOnOff::Destruct() {
    vtable = kETargetBeaconOnOffVtable;
    Process();
    vtable = kEventVtable;
}

void ETargetBeaconOnOff::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *redirected = fgAIElementController->RedirectCarpElement(element);
    if (redirected->unknown48 == 1) {
        if (redirected->type != CARP::kAIElementVehicle || redirected->object == NULL)
            return;
        PVehicle *car = static_cast<PVehicle *>(redirected->object);
        if (on != 0)
            car->EnableTargetBeacon(1);
        else
            car->DisableTargetBeacon();
    } else if (on != 0) {
        redirected->flags |= kElementBeacon;
    } else {
        redirected->flags &= ~kElementBeacon;
    }
}

// FUNC_AT(0x00048e20)
ETargetBeaconOnOff* ETargetBeaconOnOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046ab0)
void ETargetBeaconOnOff_MakeEvent_Callback(const ETargetBeaconOnOff::Record *record) {
    ETargetBeaconOnOff *event = new ETargetBeaconOnOff;
    if (event != NULL) {
        event->vtable = kETargetBeaconOnOffVtable;
        event->element = record->element;
        event->on = record->on;
    }
}

// =============================================================================================================
// The explosions
// =============================================================================================================

// FUNC_AT(0x00044f40)
ESpawnExplosion* ESpawnExplosion::Construct(Coord4 position, Coord4 direction, RSceneObj *sceneObj,
                                            uint8_t bySimpleBody, int bodySlot, PhysicsObject *source, uint32_t kind,
                                            uint32_t damageKind, float force, uint32_t surface,
                                            ArticleEffect *effect, uint32_t unknown48, const char *effectName) {
    this->position = position;
    this->direction = direction;
    this->sceneObj = sceneObj;
    this->bySimpleBody = bySimpleBody;
    this->bodySlot = bodySlot;
    this->source = source;
    this->kind = kind;
    this->damageKind = damageKind;
    this->force = force;
    this->surface = surface;
    this->effect = effect;
    vtable = kESpawnExplosionVtable;
    this->unknown48 = unknown48;
    this->effectName = effectName;
    return this;
}

// FUNC_AT(0x0004de70)
void ESpawnExplosion::Destruct() {
    vtable = kESpawnExplosionVtable;
    Process();
    vtable = kEventVtable;
}

// Where it is (place: in the instance's frame when there is a scene object; at: in the world) and its rotation; the
// effect; the jolt and the explosion object; the scorch, the light, the AI zone; the sounds.
void ESpawnExplosion::Process() {
    Coord4 rotation = kNoRotation;      // the effect's rotation in the world
    Coord4 place = kNoRotation;
    Coord4 placeRotation = kNoRotation; // the effect's own, or the instance's
    Coord4 at;
    MATRIX4 m;
    uint32_t instanceIndex = 0xffffffff;
    bool useGfx = true;
    void *reference = NULL;

    if (effect != NULL) {
        instanceIndex = effect->instance;
        reference = effect->gfx.reference;
        place = { effect->position.x, effect->position.y, effect->position.z, 1.0f };
        placeRotation = effect->gfx.rotation;
        if (sceneObj != NULL) {
            useGfx = (effect->flags & ArticleEffect::kHandleFlags) == 0;
            sceneObj->GetInstancePosition(instanceIndex, &m, true);
            TransformPoint(&m, &place, &at);
            m.mtx[3][2] = 0.0f;
            m.mtx[3][1] = 0.0f;
            m.mtx[3][0] = 0.0f;
            Coord4 instanceRotation;
            VU0_m4toquat(&instanceRotation, &m);
            D3DXQuaternionMultiply(&rotation, &placeRotation, &instanceRotation);
        } else {
            at = place;
            rotation = placeRotation;
        }
    } else if (sceneObj != NULL) {
        sceneObj->GetInstancePosition(0xffffffff, &m, true);
        place = Row(m, 3);
        at = Row(m, 3);
        m.mtx[3][2] = 0.0f;
        m.mtx[3][1] = 0.0f;
        m.mtx[3][0] = 0.0f;
        VU0_m4toquat(&placeRotation, &m);
        rotation = placeRotation;
    } else {
        place = position;
        at = position;
        BasisFromDirection(&m, &direction);
        VU0_m4toquat(&placeRotation, &m);
        rotation = placeRotation;
    }
    Coord4 up = { 0.0f, 1.0f, 0.0f, 1.0f };
    VU0_quattom4(&m, &rotation);
    VU0_MATRIX4_vect4mult(&up, &m, &up);

    if (reference == NULL) {
        if (effectName != NULL)
            reference = GFX::LookupReference(effectName);
        else if (damageKind == kFODHitDamage)
            reference = GFX::LookupReference("FODHitEffect");
        else if (surface == kSurfaceSnow)
            reference = GFX::LookupReference("snowspecificexplosion");
        else if (surface == kSurfaceWater)
            reference = GFX::LookupReference("rocketsplash");
        else if (PlayerCar()->GetPhysics()->isSub != 0) {
            if (force > kLargeForce)
                reference = GFX::LookupReference("Un_ExplL");
            else if (force > kMediumForce)
                reference = GFX::LookupReference("Un_ExplM");
            else
                reference = GFX::LookupReference("Un_ExplS");
        } else if (surface == 2 || surface == 3 || surface == 4 || surface == 5 || surface == 10 ||
                   surface == 0x50 || surface == 0x60) {
            reference = GFX::LookupReference(kGroundExplosions[fgSimulation.stepCount % 3]);
        } else {
            const char *const *names = source == fgSimulation.GetPlayerObject() ? kPlayerExplosions
                                                                                : kSmallExplosions;
            reference = GFX::LookupReference(names[ExplosionEffectSlot]);
            ExplosionEffectSlot = (ExplosionEffectSlot + 1) % 3;
        }
        useGfx = reference != NULL;
    }
    if (useGfx) {
        if (sceneObj != NULL)
            GFX::Trigger(reference, Xyz(&kZero), &kNoRotation, Xyz(&place), &placeRotation, sceneObj,
                         instanceIndex, NULL);
        else
            GFX::Trigger(reference, Xyz(&place), &placeRotation, Xyz(&kZero), &kNoRotation, NULL, 0,
                         NULL);
    } else if (reference != NULL) {
        sceneObj->TriggerFXVirtual(RSceneObj::kFXOnById, effect->id, 0, &kZero, &kZero, -1.0f);
    }

    PhysicsObject *owner = NULL;
    if (bodySlot != -1)
        owner = BodyOwner(bySimpleBody, bodySlot);
    Coord4 player;
    *reinterpret_cast<Coord3 *>(&player) = fgSimulation.GetRigidBody(PlayerCar()->rigidBodySlot)->position;
    player.w = 0.0f;
    float distance = vec3distance(&player, &at);
    if (distance < kMinDistance)
        distance = kMinDistance;
    if (distance <= kJoltRange && PlayerCar()->feedbackObject != NULL) {
        double strength = kJoltDistance / double(distance);
        PlayerCar()->feedbackObject->ApplyJolt(strength > 1.0 ? 1.0f : float(strength));
    }
    fgSimulation.SpawnExplosionObject(kind, damageKind, Xyz(&up), Xyz(&at), force, owner, DamageSourceSig);

    WWorldPos ground;
    ground.Construct();
    RScorchWorld::ScorchGround(&ground, &at, kScorchSize, kScorchType);
    fgLightManager->highLevel.AddExplosion(&at, force);
    AIZoneController::Get()->RegisterZone(kExplosionZone, Xyz(&at));

    // The first of the four sound slots whose time has come plays the explosion's sound and its distant bass. As in
    // the original, a sound operator new could not make is written through regardless.
    uint32_t first = ExplosionSoundSlot & 3;
    do {
        if (ExplosionSoundDue[ExplosionSoundSlot & 3] < TIMER_gettick()) {
            ExplosionSoundDue[ExplosionSoundSlot & 3] = TIMER_gettick() + kExplosionSoundTicks;
            ABank *bank = fgBanks[kExplosionBankSlot];
            float scale = 1.0f;
            if (_stricmp(Launch.missionName, "jungleb_mis13b") == 0)
                scale = 5.0f;
            void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
            AOneShotSound *blast = NULL;
            if (memory != NULL) {
                int handle = bank->handle;
                int patch = bank->index.Lookup(kExplosionSounds[ExplosionSoundSlot & 3]);
                blast = static_cast<AOneShotSound *>(memory)->ConstructAt(handle, patch, Xyz(&at), 1.0f,
                                                                          "Explosions", false);
            }
            blast->fxLevel = 1.0f;
            blast->minDistance = scale * kNearMinDistance;
            double blastMax = double(scale) * kNearMaxDistance;
            blast->maxDistance = float(blastMax);
            blast->maxDistanceSq = float(blastMax * blastMax);
            memory = ABaseSound::OperatorNew(sizeof(ALimitedSound), "AOneShotSound");
            AOneShotSound *bass = NULL;
            if (memory != NULL) {
                int handle = bank->handle;
                int patch = bank->index.Lookup("SFX_DistExp");
                bass = static_cast<AOneShotSound *>(memory)->ConstructAt(handle, patch, Xyz(&at), 1.0f,
                                                                         "ExplosionBass", false);
                bass->vtable = kALimitedSoundVtable;
                LimitedSoundCount++;
            }
            ExplosionSoundSlot++;
            bass->minDistance = scale * kFarMinDistance;
            double bassMax = double(scale) * kFarMaxDistance;
            bass->maxDistance = float(bassMax);
            bass->maxDistanceSq = float(bassMax * bassMax);
            break;
        }
        ExplosionSoundSlot++;
    } while ((ExplosionSoundSlot & 3) != first);
    NullFunction();     // WWorldPos's destructor
}

// FUNC_AT(0x0004ef20)
ESpawnExplosion* ESpawnExplosion::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044fe0)
void ESpawnExplosion_MakeEvent_Callback(const ESpawnExplosion::Record *record) {
    ESpawnExplosion *event = new ESpawnExplosion;
    if (event != NULL)
        event->Construct(gEventDynamicData.position, *reinterpret_cast<const Coord4 *>(gEventDynamicData.unknown20),
                         gEventDynamicData.sceneObj, gEventDynamicData.flag, gEventDynamicData.index, record->source,
                         record->kind, record->damageKind, record->force, record->surface, record->effect,
                         record->unknown1c, record->effectName);
}

// FUNC_AT(0x00045090)
void ESpawnExplosion_ResolveEvent_Callback(ESpawnExplosion::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->source)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->effect)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->effectName)->Construct(shared, parent);
}

// FUNC_AT(0x00045120)
void ESpawnExplosionStatic::Destruct() {
    vtable = kESpawnExplosionStaticVtable;
    Process();
    vtable = kEventVtable;
}

// Without a position or an instance, the original leaves the position as its stack had it; the port's is zero.
void ESpawnExplosionStatic::Process() {
    Coord4 at = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (position != NULL) {
        at.x = position->x;
        at.y = position->y;
        at.z = position->z;
    } else if (instance != NULL) {
        at.x = instance->position[0];
        at.y = instance->position[1];
        at.z = instance->position[2];
    }
    const Coord4 facing = { 0.0f, 0.0f, 1.0f, 1.0f };
    ESpawnExplosion *explosion = new ESpawnExplosion;
    if (explosion != NULL)
        explosion->Construct(at, facing, NULL, false, -1, NULL, kind, damageKind, force, surface, NULL, 0,
                             effectName);
}

// FUNC_AT(0x00048c60)
ESpawnExplosionStatic* ESpawnExplosionStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00045250)
void ESpawnExplosionStatic_MakeEvent_Callback(const ESpawnExplosionStatic::Record *record) {
    ESpawnExplosionStatic *event = new ESpawnExplosionStatic;
    if (event != NULL) {
        event->vtable = kESpawnExplosionStaticVtable;
        event->position = record->position;
        event->instance = record->instance;
        event->kind = record->kind;
        event->damageKind = record->damageKind;
        event->force = record->force;
        event->surface = record->surface;
        event->effectName = record->effectName;
    }
}

// FUNC_AT(0x000452a0)
void ESpawnExplosionStatic_ResolveEvent_Callback(ESpawnExplosionStatic::Record *record, UGroup *shared,
                                                 UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->position)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->instance)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->effectName)->Construct(shared, parent);
}

// FUNC_AT(0x00045330)
ESpawnForceEffect* ESpawnForceEffect::Construct(Coord4 position, Coord4 direction, RSceneObj *sceneObj, float force,
                                                uint32_t surface, ArticleEffect *effect) {
    this->position = position;
    this->direction = direction;
    this->sceneObj = sceneObj;
    this->force = force;
    vtable = kESpawnForceEffectVtable;
    this->surface = surface;
    this->effect = effect;
    return this;
}

// FUNC_AT(0x000453a0)
void ESpawnForceEffect::Destruct() {
    vtable = kESpawnForceEffectVtable;
    Process();
    vtable = kEventVtable;
}

// With an article effect, the explosion is where the effect is on the scene object's instance; without one, an
// effect of its own is made up at the position, rotated to the direction (and, as in the original, the event's
// effect is left pointing at it).
void ESpawnForceEffect::Process() {
    Coord4 at = position;
    Coord4 facing = direction;
    uint32_t instanceIndex = 0xffffffff;
    bool useGfx = true;
    void *reference = NULL;
    MATRIX4 m;
    ArticleEffect own;
    if (effect != NULL) {
        reference = effect->gfx.reference;
        instanceIndex = effect->instance;
        MATRIX4 instanceMatrix;
        if (sceneObj != NULL) {
            sceneObj->GetInstancePosition(instanceIndex, &instanceMatrix, true);
            useGfx = (effect->flags & ArticleEffect::kHandleFlags) == 0;
        } else {
            VU0_MATRIX4Init(&instanceMatrix);
        }
        VU0_quattom4(&m, &effect->gfx.rotation);
        Row(m, 3) = { effect->position.x, effect->position.y, effect->position.z, 1.0f };
        VU0_MATRIX4_mult(&m, &m, &instanceMatrix);
        facing = Row(m, 2);
        at = Row(m, 3);
        if (!useGfx) {
            sceneObj->TriggerFXVirtual(RSceneObj::kFXOnById, effect->id, 0, &kZero, &kZero, -1.0f);
            fgSimulation.SpawnExplosionObject(kForceExplosionType, kForceExplosionKind, Xyz(&facing), Xyz(&at), force,
                                              NULL, DamageSourceSig);
            return;
        }
    } else {
        BasisFromDirection(&m, &direction);
        Coord4 rotation;
        VU0_m4toquat(&rotation, &m);
        own.position = *Xyz(&at);
        own.unknown0C = at.w;
        own.gfx.rotation = rotation;
        effect = &own;
    }
    if (reference == NULL)
        reference = GFX::LookupReference(surface == kSurfaceSnow ? "Ex_Snow" : "Ex_Small");
    if (reference != NULL)
        GFX::Trigger(reference, &effect->position, &effect->gfx.rotation, Xyz(&kZero), &kNoRotation,
                     sceneObj, instanceIndex, NULL);
    fgSimulation.SpawnExplosionObject(kForceExplosionType, kForceExplosionKind, Xyz(&facing), Xyz(&at), force, NULL,
                                      DamageSourceSig);
}

// FUNC_AT(0x00048c80)
ESpawnForceEffect* ESpawnForceEffect::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00045720)
void ESpawnForceEffect_MakeEvent_Callback(const ESpawnForceEffect::Record *record) {
    ESpawnForceEffect *event = new ESpawnForceEffect;
    if (event != NULL)
        event->Construct(gEventDynamicData.position, *reinterpret_cast<const Coord4 *>(gEventDynamicData.unknown20),
                         gEventDynamicData.sceneObj, record->force, record->surface, record->effect);
}

// FUNC_AT(0x000457a0)
void ESpawnForceEffect_ResolveEvent_Callback(ESpawnForceEffect::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->effect)->Construct(shared, parent);
}

// =============================================================================================================
// The other spawns
// =============================================================================================================

// FUNC_AT(0x000457f0)
ESpawnSentry* ESpawnSentry::Construct(CARP::Instance *instance, SentrySetup setup, float hitPoints, uint32_t holdSteps,
                                      uint32_t scoreable, uint8_t bySimpleBody, int bodySlot) {
    this->instance = instance;
    this->setup = setup;
    this->hitPoints = hitPoints;
    this->holdSteps = holdSteps;
    this->scoreable = scoreable;
    vtable = kESpawnSentryVtable;
    this->bySimpleBody = bySimpleBody;
    this->bodySlot = bodySlot;
    return this;
}

// FUNC_AT(0x00045880)
void ESpawnSentry::Destruct() {
    vtable = kESpawnSentryVtable;
    Process();
    vtable = kEventVtable;
}

void ESpawnSentry::Process() {
    if (instance == NULL)
        return;
    uint32_t ownerSig = 0;
    if (bodySlot != -1)
        ownerSig = bySimpleBody ? fgSimulation.simpleRigidBodySigs[bodySlot] : fgSimulation.rigidBodySigs[bodySlot];
    fgSimulation.SpawnSentryObject(instance, setup.srbAxis, setup.srbSpeed, setup.srbLimit, setup.animAxis,
                                   setup.animSpeed, setup.animLimit, setup.aimSystem, setup.fireSystem, setup.shots,
                                   setup.shotSeconds, setup.range, ownerSig, hitPoints, holdSteps, scoreable != 0);
}

// FUNC_AT(0x00048ca0)
ESpawnSentry* ESpawnSentry::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00045940)
void ESpawnSentry_MakeEvent_Callback(const ESpawnSentry::Record *record) {
    ESpawnSentry *event = new ESpawnSentry;
    if (event != NULL)
        event->Construct(record->instance, record->setup, record->hitPoints, record->holdSteps, record->scoreable,
                         gEventDynamicData.flag, gEventDynamicData.index);
}

// FUNC_AT(0x00044e80)
void ESpawnDramaticSmackable::Destruct() {
    vtable = kESpawnDramaticSmackableVtable;
    Process();
    vtable = kEventVtable;
}

void ESpawnDramaticSmackable::Process() {
    Newton::SpawnFromEvent(mass, lifetime, instance, bySimpleBody, bodySlot, sceneObj, kNewtonSpawnTowardsView);
}

// FUNC_AT(0x00048c40)
ESpawnDramaticSmackable* ESpawnDramaticSmackable::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00044ef0)
void ESpawnDramaticSmackable_MakeEvent_Callback(const ESpawnDramaticSmackable::Record *record) {
    ESpawnDramaticSmackable *event = new ESpawnDramaticSmackable;
    if (event != NULL) {
        event->vtable = kESpawnDramaticSmackableVtable;
        event->mass = record->mass;
        event->instance = record->instance;
        event->lifetime = record->lifetime;
        event->bySimpleBody = gEventDynamicData.flag;
        event->bodySlot = gEventDynamicData.index;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// FUNC_AT(0x000459b0)
void ESpawnSimplePhysics::Destruct() {
    vtable = kESpawnSimplePhysicsVtable;
    Process();
    vtable = kEventVtable;
}

void ESpawnSimplePhysics::Process() {
    Newton::SpawnFromEvent(mass, lifetime, instance, bySimpleBody, bodySlot, sceneObj, 0);
}

// FUNC_AT(0x00048cc0)
ESpawnSimplePhysics* ESpawnSimplePhysics::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00045a20)
void ESpawnSimplePhysics_MakeEvent_Callback(const ESpawnSimplePhysics::Record *record) {
    ESpawnSimplePhysics *event = new ESpawnSimplePhysics;
    if (event != NULL) {
        event->vtable = kESpawnSimplePhysicsVtable;
        event->mass = record->mass;
        event->instance = record->instance;
        event->lifetime = record->lifetime;
        event->bySimpleBody = gEventDynamicData.flag;
        event->bodySlot = gEventDynamicData.index;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

// FUNC_AT(0x00045a70)
void ESpawnSmackable::Destruct() {
    vtable = kESpawnSmackableVtable;
    Process();
    vtable = kEventVtable;
}

// The model's 'Name' item and its items are looked up and not used.
void ESpawnSmackable::Process() {
    if (model == NULL)
        return;
    model->DataLocateTag(kTagName);
    model->GetArray();
    Coord3 velocity = *Xyz(&kZero);
    MATRIX4 transform;
    if (sceneObj != NULL) {
        sceneObj->GetInstancePosition(instanceIndex, &transform, true);
        if (sceneObj->GetVelocityVirtual() != NULL)
            velocity = *sceneObj->GetVelocityVirtual();
    } else {
        if (instance == NULL)
            return;
        transform = instance->Matrix();
        transform.mtx[0][3] = 0.0f;
        transform.mtx[1][3] = 0.0f;
        transform.mtx[2][3] = 0.0f;
        transform.mtx[3][3] = 1.0f;
    }
    RAutonomousObj *object =
        static_cast<RAutonomousObj *>(UMemory::FastAlloc(sizeof(RAutonomousObj), "RAutonomousObj"));
    if (object != NULL) {
        memcpy(&object->instance, IdentityMatrix, sizeof object->instance);
        object->Construct(&object->instance);
        object->vtable = kRAutonomousObjVtable;
    }
    if (!object->UseArticle(model, article)) {
        if (object != NULL)
            object->DeleteVirtual(1);
        return;
    }
    object->SetTransform(&transform);
    fgSimulation.SpawnSmackableObject(object, NULL, &velocity, fgSimulation.stepsPerSecond * kSmackableSeconds);
    PhysicsObject *physics = object->physics;
    if (physics == NULL)
        return;
    Coord4 force = { kZero.x, kZero.y, kZero.z, 0.0f };
    Coord4 spin;
    BytesToCoordXYZ(RandomWord(), &kSpinOffset, &kSpinScale, NULL, &spin);
    physics->ApplyForces(Xyz(&force), Xyz(&spin));
}

// FUNC_AT(0x00048ce0)
ESpawnSmackable* ESpawnSmackable::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00045cb0)
void ESpawnSmackable_MakeEvent_Callback(const ESpawnSmackable::Record *record) {
    ESpawnSmackable *event = new ESpawnSmackable;
    if (event != NULL) {
        event->vtable = kESpawnSmackableVtable;
        event->model = record->model;
        event->article = record->article;
        event->instance = record->instance;
        event->sceneObj = gEventDynamicData.sceneObj;
        event->instanceIndex = gEventDynamicData.instanceIndex;
    }
}

// FUNC_AT(0x00045d00)
void ESpawnSmackable_ResolveEvent_Callback(ESpawnSmackable::Record *record, UGroup *shared, UGroup *parent) {
    reinterpret_cast<CARP::TagReference *>(&record->model)->Construct(shared, parent);
    reinterpret_cast<CARP::TagReference *>(&record->instance)->Construct(shared, parent);
}

// FUNC_AT(0x00045d70)
void ESpawnTraffic::Destruct() {
    vtable = kESpawnTrafficVtable;
    Process();
    vtable = kEventVtable;
}

// Each car is asked for again for each call, as in the original (the controller's Get and GetTrafficVehicle)
void ESpawnTraffic::Process() {
    for (int i = 0; i < kTrafficVehicles; i++) {
        PhysicsObject *physics = AIVehicleController::Get()->GetTrafficVehicle(i)->GetPhysicsObject();
        if (fgSimulation.GetRigidBody(physics->rigidBodySlot)->sleepState != RigidBody::kFrozen)
            continue;
        Coord3 position = trigger->position;
        Coord3 heading = trigger->forward;
        AIVehicleController::Get()->GetTrafficVehicle(i)->SetAttackMode(kTrafficAttackMode);
        AIVehicleController::Get()->GetTrafficVehicle(i)->SetNavigateMode(kTrafficNavigateMode, NULL);
        static_cast<PVehicle *>(AIVehicleController::Get()->GetTrafficVehicle(i)->GetPhysicsObject())->ResetDamage();
        static_cast<PVehicle *>(AIVehicleController::Get()->GetTrafficVehicle(i)->GetPhysicsObject())->ClearEMPState();
        static_cast<PVehicle *>(AIVehicleController::Get()->GetTrafficVehicle(i)->GetPhysicsObject())
            ->ResetCar(&position, &heading);
        int settled = fgSimulation.stepCount + fgSimulation.stepsPerSecond * kTrafficSettleSeconds;
        AIVehicleController::Get()->GetTrafficVehicle(i)->settleStep = settled;
        AIVehicleController::Get()->GetTrafficVehicle(i)->driveToNav->InitAtPoint(&position, &heading, false);
        // the count's two members inlined, each with its own Get
        int16_t count = int16_t(TrafficSpawnCount + 1);
        AIVehicleController::Get();
        AIVehicleController::Get();
        TrafficSpawnCount = count < 0 ? 0 : count;
        if (speed > 0.0f) {
            physics = AIVehicleController::Get()->GetTrafficVehicle(i)->GetPhysicsObject();
            const Coord4 &forward = Row(fgSimulation.GetRigidBody(physics->rigidBodySlot)->info->orientation, 2);
            Coord4 momentum = { forward.x, forward.y, forward.z, 0.0f };
            physics = AIVehicleController::Get()->GetTrafficVehicle(i)->GetPhysicsObject();
            float scale = fgSimulation.GetRigidBody(physics->rigidBodySlot)->mass * speed;
            VU0_v4unitxyz(&momentum, &momentum);
            VU0_v4scale(&momentum, scale, &momentum);
            physics = AIVehicleController::Get()->GetTrafficVehicle(i)->GetPhysicsObject();
            fgSimulation.GetRigidBody(physics->rigidBodySlot)->momentum = *Xyz(&momentum);
        }
        break;
    }
}

// FUNC_AT(0x00048d00)
ESpawnTraffic* ESpawnTraffic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00046020)
void ESpawnTraffic_MakeEvent_Callback(const ESpawnTraffic::Record *record) {
    ESpawnTraffic *event = new ESpawnTraffic;
    if (event != NULL) {
        event->vtable = kESpawnTrafficVtable;
        event->trigger = gEventDynamicData.trigger;
        event->speed = record->speed;
    }
}

// FUNC_AT(0x00046c90)
EWakeupSmackable* EWakeupSmackable::Construct(RSceneObj *sceneObj, CARP::Instance *instance, EventSceneRef *ref,
                                              uint8_t hasRayShell, Coord3 rayStart, Coord3 rayEnd, float damage,
                                              WTrigger *trigger, uint8_t bySimpleBody, int bodySlot) {
    this->sceneObj = sceneObj;
    this->instance = instance;
    this->ref = ref;
    this->hasRayShell = hasRayShell;
    this->rayStart = rayStart;
    this->rayEnd = rayEnd;
    this->damage = damage;
    this->trigger = trigger;
    vtable = kEWakeupSmackableVtable;
    this->bySimpleBody = bySimpleBody;
    this->bodySlot = bodySlot;
    return this;
}

// FUNC_AT(0x00046d10)
void EWakeupSmackable::Destruct() {
    vtable = kEWakeupSmackableVtable;
    Process();
    vtable = kEventVtable;
}

// The ray shell's damage needs its ray within the body's sphere and the ray's box (a quarter wide, as long as the
// ray) to overlap the body's. The original leaves the w words of the ray's vectors and of the body's position as its
// stack had them (none is read); the port's are zero.
void EWakeupSmackable::Process() {
    if (sceneObj == NULL) {
        sceneObj = ref->sceneObj;
        if (sceneObj == NULL)
            return;
    }
    PhysicsObject *object = NULL;
    if (ref->flags & EventSceneRef::kSmackable) {
        Coord3 velocity = *Xyz(&kZero);
        if (sceneObj->GetVelocityVirtual() != NULL)
            velocity = *sceneObj->GetVelocityVirtual();
        object = fgSimulation.SpawnSmackableObject(sceneObj, trigger, &velocity,
                                                   Ftol(double(fgSimulation.stepsPerSecond) * kOneTenth));
    } else if (ref->flags & EventSceneRef::kNewton) {
        Coord3 momentum;
        Coord3 spin;
        if (bodySlot == -1) {
            momentum = *Xyz(&kZero);
            spin = *Xyz(&kZero);
        } else if (bySimpleBody) {
            SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(bodySlot);
            momentum = body->velocity;
            spin = body->acceleration;
            body->GetScalarVelocity();
        } else {
            RigidBody *body = fgSimulation.GetRigidBody(bodySlot);
            momentum = body->velocity;
            spin = body->angularVelocity;
            VU0_v3length(&body->velocity);
        }
        object = fgSimulation.SpawnNewtonObject(reinterpret_cast<const Coord3 *>(instance->axisZ),
                                                reinterpret_cast<const Coord3 *>(instance->position), &momentum, &spin,
                                                instance, 1, kNewtonMass, kNewtonLifetime);
    }
    if (object == NULL) {
        trigger->flags |= WTrigger::kEnabled;
        return;
    }
    if (!hasRayShell || (ref->flags & EventSceneRef::kNewton))
        return;

    Coord4 start = { rayStart.x, rayStart.y, rayStart.z, 0.0f };
    Coord4 end = { rayEnd.x, rayEnd.y, rayEnd.z, 0.0f };
    RigidBody *body = fgSimulation.GetRigidBody(object->rigidBodySlot);
    RigidBodyInfo *info = body->info;
    Coord4 ray = { 0.0f, 0.0f, 0.0f, 0.0f };
    VU0_v4sub(&end, &start, &ray);
    Coord4 heading = { 0.0f, 0.0f, 0.0f, 0.0f };
    VU0_v4unitxyz(&ray, &heading);
    float length = VU0_v3length(&ray);
    float radius = body->radius;
    MATRIX4 bodyMatrix = info->orientation;
    MATRIX4 generated;
    MATRIX4 rayMatrix = *Util_GenerateMatrix(&generated, Xyz(&heading));
    Coord4 bodyPosition = { body->position.x, body->position.y, body->position.z, 0.0f };
    Coord4 origin = { start.x, start.y, start.z, 0.0f };
    Coord4 bodyCentre = { bodyPosition.x, bodyPosition.y, bodyPosition.z, 1.0f };
    Coord4 rayCentre = { origin.x, origin.y, origin.z, 1.0f };
    Coord4 bodyExtents = info->halfExtents;
    Coord4 rayExtents = { kRayHalfWidth, kRayHalfWidth, length, 1.0f };
    Coord3 hit;
    if (!Util_CollideRayWithSphere(&origin, &heading, &bodyPosition, radius, &hit))
        return;
    OBB bodyBox;
    bodyBox.Construct();
    OBB rayBox;
    rayBox.Construct();
    bodyBox.Reset(&bodyMatrix, &bodyCentre, &bodyExtents);
    rayBox.Reset(&rayMatrix, &rayCentre, &rayExtents);
    if (rayBox.CheckOBBOverlap(&bodyBox))
        object->ApplyDamageVirtual(&start, &end, damage, kRayDamageScale, kRayDamageKind, DamageSourceSig);
    NullFunction();     // the boxes' destructors
    NullFunction();
}

// FUNC_AT(0x00048ea0)
EWakeupSmackable* EWakeupSmackable::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00047230)
void EWakeupSmackable_MakeEvent_Callback(const EWakeupSmackable::Record *record) {
    EWakeupSmackable *event = new EWakeupSmackable;
    if (event != NULL)
        event->Construct(record->sceneObj, record->instance, record->ref, gEventDynamicData.hasRayShell,
                         gEventDynamicData.rayStart, gEventDynamicData.rayEnd, gEventDynamicData.rayValue,
                         gEventDynamicData.trigger, gEventDynamicData.flag, gEventDynamicData.index);
}
