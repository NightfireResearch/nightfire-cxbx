#pragma fp_contract(off)

#include "EventsB.h"

#include "../Scheduler.hpp"               // RegisterEvent, Scheduler, Schedule
#include "../audio/Bank.h"                // fgBanks
#include "../audio/Sound.h"               // ABaseSound, AOneShotSound
#include "../audio/ZoomObj.h"             // ASound
#include "../engine/GameLoop.h"           // Launch
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"          // OperatorNew
#include "AICharacter.h"
#include "AIElements.h"
#include "AIHelicopter.h"
#include "AIPedestrian.h"
#include "AIRoadSpawn.h"
#include "AIVehicleController.h"
#include "BondCar.h"                     // PBondCar::scoreable
#include "EventsC.h"                      // EPathUtil_GetFromInstanceOrAI
#include "Gfx.h"
#include "Helicopter.h"                  // PHelicopter::scoreable
#include "Hud.h"
#include "Human.h"
#include "MissionManager.h"               // glbMissionManager
#include "Simulation.h"                   // fgSimulation
#include "Vehicle.h"                     // PVehicle's vtable slots
#include "WeaponManager.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"         // RigidBody, DamageSourceSig
#include "../physics/RigidBodyResolve.h"  // CollisionImpact
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../render/Colorize.h"
#include "../render/PathEngine.h"         // RPathHandle
#include "../render/RSceneObj.hpp"        // RSceneObj, RandomScaled
#include "../world/CollisionInstance.h"   // WTrigger
#include "../world/Trigger.h"             // gEventDynamicData, TriggerEvents
#include "../world/World.h"               // fgWorld, ArticleEffect
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// Mission events EExit .. EPathSFX: see EventsB.h.
// ---------------------------------------------------------------------------------------------------------------

#define gFeedbackAllowed BOOL8_AT(0x001b6f50)           // IFeedback's: clear, no force feedback
// The C runtime's printf (the game's)
#define CRT_printf ((int (*)(const char *, ...))0x00132192)
#define gSoundOption BOOL8_AT(0x001b7054)               // ASoundManager::Init's second argument
#define fgScheduler (*(Scheduler **)0x001e520c)

// The glass sounds' counters (EHitWindow's alone)
// XBE_GLOBAL(0x001e239c, 0x4)
uint32_t gGlassShatterCount;     // picks between the two shatter sounds (name ours)
// XBE_GLOBAL(0x001e2398, 0x4)
uint32_t gStoreAlarmCount;       // the alarm sounds on every eighth low pane (name ours)

// ---- the vtables (the game's; each has the one slot, the scalar deleting destructor)

#define kEExitVtable ((vtable_Event *)0x0018c4fc)
#define kEFESimFrameUpdateVtable ((vtable_Event *)0x0018c500)
#define kEFailMissionObjectiveVtable ((vtable_Event *)0x0018c504)
#define kEFireEventListVtable ((vtable_Event *)0x0018c508)
#define kEFireRandomTriggerVtable ((vtable_Event *)0x0018c50c)
#define kEFireTriggerSpeedConditionVtable ((vtable_Event *)0x0018c510)
#define kEForceCarStopVtable ((vtable_Event *)0x0018c524)
#define kEForceNearPedRespawnVtable ((vtable_Event *)0x0018c528)
#define kEGadgetOffVtable ((vtable_Event *)0x0018c52c)
#define kEGadgetOnVtable ((vtable_Event *)0x0018c530)
#define kEHideInstanceVtable ((vtable_Event *)0x0018c534)
#define kEHideInstanceStaticVtable ((vtable_Event *)0x0018c538)
#define kEHitWindowVtable ((vtable_Event *)0x0018c53c)
#define kEIncMagicCounterVtable ((vtable_Event *)0x0018c540)
#define kEInflictDamageVtable ((vtable_Event *)0x0018c544)
#define kEInfraRedOffVtable ((vtable_Event *)0x0018c548)
#define kEInfraRedOnVtable ((vtable_Event *)0x0018c54c)
#define kEInsertObjectiveVtable ((vtable_Event *)0x0018c550)
#define kEKickAIElementVtable ((vtable_Event *)0x0018c554)
#define kEKickObjectVtable ((vtable_Event *)0x0018c55c)
#define kEKillAIElementVtable ((vtable_Event *)0x0018c560)
#define kEKillObjectVtable ((vtable_Event *)0x0018c564)
#define kEKillSentryVtable ((vtable_Event *)0x0018c568)
#define kELavaHazeVtable ((vtable_Event *)0x0018c56c)
#define kELetterBoxOffVtable ((vtable_Event *)0x0018c570)
#define kELetterBoxOnVtable ((vtable_Event *)0x0018c574)
#define kEMaybeActivateAIElementVtable ((vtable_Event *)0x0018c578)
#define kEMuzzleFlashVtable ((vtable_Event *)0x0018c57c)
#define kENuclearBlastVtable ((vtable_Event *)0x0018c580)
#define kEObjectiveAddVtable ((vtable_Event *)0x0018c584)
#define kEObjectiveDisplayTextVtable ((vtable_Event *)0x0018c588)
#define kEObjectiveFailVtable ((vtable_Event *)0x0018c58c)
#define kEObjectiveInsertVtable ((vtable_Event *)0x0018c590)
#define kEObjectivePassVtable ((vtable_Event *)0x0018c594)
#define kEObjectiveSetCurrentVtable ((vtable_Event *)0x0018c598)
#define kEObjectiveSetIncompleteVtable ((vtable_Event *)0x0018c59c)
#define kEPassMissionObjectiveVtable ((vtable_Event *)0x0018c5a0)
#define kEPassObjectiveVtable ((vtable_Event *)0x0018c5a4)
#define kEPathChangeThrottleDynamicVtable ((vtable_Event *)0x0018c5a8)
#define kEPathChangeThrottleStaticVtable ((vtable_Event *)0x0018c5ac)
#define kEPathMultiplyThrottleDynamicVtable ((vtable_Event *)0x0018c5b0)
#define kEPathMultiplyThrottleStaticVtable ((vtable_Event *)0x0018c5b4)
#define kEPathSFXVtable ((vtable_Event *)0x0018c5b8)

namespace {

// The bank slots the glass and path sounds come from
const int kBankSlotPath = 0;
const int kBankSlotStoreAlarm = 0;
const int kBankSlotHeadlight = 1;
const int kBankSlotGlass = 2;

const char *const kGlassShatterPatches[2] = {"SFX_GlassShatter1", "SFX_GlassShatter2"};

// CARP::Instance::flags' bit EHideInstance sets (name ours, after the event)
const uint8_t kInstanceHidden = 0x01;

// RSceneObj::TriggerFX's type for a muzzle flash (EMuzzleFlash's)
const int kFXMuzzleFlash = 8;

// PhysicsObject::ApplyDamage's kinds as these events pass them
const int kDamageKick = 0;
const int kDamageInflicted = 3;

// The Simulation's rigid body slots (PhysicsObjects' size)
const uint32_t kMaxRigidBodies = 64;

// The path sound's task: EPathSFXUpdate's event number
const int kEventPathSFXUpdate = 0x10f;

const MATRIX4 kIdentity = { {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
} };
alignas(16) const Coord4 kZero = {0.0f, 0.0f, 0.0f, 0.0f};
alignas(16) const Coord4 kUnitW = {0.0f, 0.0f, 0.0f, 1.0f};
alignas(16) const Coord4 kUp = {0.0f, 1.0f, 0.0f, 0.0f};

// EKickAIElement's random spin: each byte of 32 random bits, as 0..1, less a half, times these
alignas(16) const Coord4 kSpinOffset = {-0.5f, -0.5f, -0.5f, 0.0f};
alignas(16) const Coord4 kSpinScale = {6000.0f, 7000.0f, 6000.0f, 0.0f};


}  // namespace

// ---- the HUD and the mission's end

// FUNC_AT(0x0003fbf0)
void EExit_MakeEvent_Callback(const void *) {
    EExit *event = new EExit;
    if (event != NULL)
        event->vtable = kEExitVtable;
}

// FUNC_AT(0x0003fb70)
EExit* EExit::Construct() {
    vtable = kEExitVtable;
    return this;
}

void EExit::Process() {
    GHud::TheApp()->PauseOff();
    gFeedbackAllowed = false;
    fgSimulation.simState = kSimFinished;
    glbMissionManager->TallyScores(&Launch, true);
}

// FUNC_AT(0x0003fb80)
void EExit::Destruct() {
    vtable = kEExitVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047a70)
EExit* EExit::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003fc60)
void EFESimFrameUpdate_MakeEvent_Callback(const void *) {
    EFESimFrameUpdate *event = new EFESimFrameUpdate;
    if (event != NULL)
        event->vtable = kEFESimFrameUpdateVtable;
}

void EFESimFrameUpdate::Process() {
    fgSimulation.AdvanceStep();
}

// FUNC_AT(0x0003fc10)
void EFESimFrameUpdate::Destruct() {
    vtable = kEFESimFrameUpdateVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047a90)
EFESimFrameUpdate* EFESimFrameUpdate::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040250)
void EGadgetOff_MakeEvent_Callback(const void *) {
    EGadgetOff *event = new EGadgetOff;
    if (event != NULL)
        event->vtable = kEGadgetOffVtable;
}

void EGadgetOff::Process() {
    GHud::TheApp()->GadgetOff();
}

// FUNC_AT(0x00040200)
void EGadgetOff::Destruct() {
    vtable = kEGadgetOffVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047b10)
EGadgetOff* EGadgetOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000402d0)
void EGadgetOn_MakeEvent_Callback(const EGadgetOn::Record *record) {
    EGadgetOn *event = new EGadgetOn;
    if (event != NULL) {
        event->vtable = kEGadgetOnVtable;
        event->gadget = record->gadget;
        event->value = record->value;
    }
}

void EGadgetOn::Process() {
    GHud::TheApp()->GadgetOn(gadget, value);
}

// FUNC_AT(0x00040270)
void EGadgetOn::Destruct() {
    vtable = kEGadgetOnVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047b30)
EGadgetOn* EGadgetOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040b20)
void ELavaHaze_MakeEvent_Callback(const ELavaHaze::Record *record) {
    ELavaHaze *event = new ELavaHaze;
    if (event != NULL) {
        event->vtable = kELavaHazeVtable;
        event->on = record->on;
    }
}

void ELavaHaze::Process() {
    GHud::TheApp()->EnableLavaDamageEffect(on != 0);
}

// FUNC_AT(0x00040ac0)
void ELavaHaze::Destruct() {
    vtable = kELavaHazeVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047f50)
ELavaHaze* ELavaHaze::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040bb0)
void ELetterBoxOff_MakeEvent_Callback(const ELetterBoxOff::Record *record) {
    ELetterBoxOff *event = new ELetterBoxOff;
    if (event != NULL) {
        event->vtable = kELetterBoxOffVtable;
        event->flag = record->flag;
    }
}

void ELetterBoxOff::Process() {
    GHud::TheApp()->LetterBoxOff(flag != 0);
    glbMissionManager->ClearCinematicMode(kCinematicCameraAnim | kCinematicCamera | kCinematicLetterBox);
    gFeedbackAllowed = true;
}

// FUNC_AT(0x00040b40)
void ELetterBoxOff::Destruct() {
    vtable = kELetterBoxOffVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047f70)
ELetterBoxOff* ELetterBoxOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040c40)
void ELetterBoxOn_MakeEvent_Callback(const ELetterBoxOn::Record *record) {
    ELetterBoxOn *event = new ELetterBoxOn;
    if (event != NULL) {
        event->vtable = kELetterBoxOnVtable;
        event->flag = record->flag;
    }
}

void ELetterBoxOn::Process() {
    gFeedbackAllowed = false;
    GHud::TheApp()->LetterBoxOn(flag != 0);
    glbMissionManager->SetCinematicMode(kCinematicLetterBox);
    Colorize->DisableMotionBlur();
}

// FUNC_AT(0x00040bd0)
void ELetterBoxOn::Destruct() {
    vtable = kELetterBoxOnVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047f90)
ELetterBoxOn* ELetterBoxOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040640)
void EInfraRedOff_MakeEvent_Callback(const void *) {
    EInfraRedOff *event = new EInfraRedOff;
    if (event != NULL)
        event->vtable = kEInfraRedOffVtable;
}

void EInfraRedOff::Process() {
    Colorize->SetEnabled(RColorize::kModeOff);
}

// FUNC_AT(0x000405f0)
void EInfraRedOff::Destruct() {
    vtable = kEInfraRedOffVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047e90)
EInfraRedOff* EInfraRedOff::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000406b0)
void EInfraRedOn_MakeEvent_Callback(const void *) {
    EInfraRedOn *event = new EInfraRedOn;
    if (event != NULL)
        event->vtable = kEInfraRedOnVtable;
}

void EInfraRedOn::Process() {
    Colorize->SetEnabled(RColorize::kModeInfrared);
}

// FUNC_AT(0x00040660)
void EInfraRedOn::Destruct() {
    vtable = kEInfraRedOnVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047eb0)
EInfraRedOn* EInfraRedOn::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// ---- the mission manager: objectives and counters

// FUNC_AT(0x0003fc80)
void EFailMissionObjective_MakeEvent_Callback(const EFailMissionObjective::Record *record) {
    EFailMissionObjective *event = new EFailMissionObjective;
    if (event != NULL) {
        event->vtable = kEFailMissionObjectiveVtable;
        event->objective = record->objective;
    }
}

// FUNC_AT(0x00041330)
void EPassMissionObjective_MakeEvent_Callback(const EPassMissionObjective::Record *record) {
    EPassMissionObjective *event = new EPassMissionObjective;
    if (event != NULL) {
        event->vtable = kEPassMissionObjectiveVtable;
        event->objective = record->objective;
    }
}

// FUNC_AT(0x00041350)
void EPassObjective_MakeEvent_Callback(const EPassObjective::Record *record) {
    EPassObjective *event = new EPassObjective;
    if (event != NULL) {
        event->vtable = kEPassObjectiveVtable;
        event->objective = record->objective;
        event->unknown08 = record->unknown08;
    }
}

// FUNC_AT(0x000406d0)
void EInsertObjective_MakeEvent_Callback(const EInsertObjective::Record *record) {
    EInsertObjective *event = new EInsertObjective;
    if (event != NULL) {
        event->vtable = kEInsertObjectiveVtable;
        for (int i = 0; i < 5; i++)
            event->unknown04[i] = record->unknown04[i];
    }
}

// FUNC_AT(0x00047ed0)
EInsertObjective* EInsertObjective::Delete(unsigned flags) {
    return DeleteBaseEvent(this, flags);
}

// FUNC_AT(0x00040510)
void EIncMagicCounter_MakeEvent_Callback(const EIncMagicCounter::Record *record) {
    EIncMagicCounter *event = new EIncMagicCounter;
    if (event != NULL) {
        event->vtable = kEIncMagicCounterVtable;
        event->counter = record->counter;
    }
}

void EIncMagicCounter::Process() {
    glbMissionManager->IncMagicCounter(counter);
}

// FUNC_AT(0x000404c0)
void EIncMagicCounter::Destruct() {
    vtable = kEIncMagicCounterVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047e50)
EIncMagicCounter* EIncMagicCounter::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040f70)
void EObjectiveAdd_MakeEvent_Callback(const EObjectiveAdd::Record *record) {
    EObjectiveAdd *event = new EObjectiveAdd;
    if (event != NULL) {
        event->vtable = kEObjectiveAddVtable;
        event->id = record->id;
        event->unknown08 = record->unknown08;
        event->score = record->score;
        event->unknown10 = record->unknown10;
    }
}

void EObjectiveAdd::Process() {
    glbMissionManager->AddObjective(id, unknown08, score, unknown10);
}

// FUNC_AT(0x00040f10)
void EObjectiveAdd::Destruct() {
    vtable = kEObjectiveAddVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047ff0)
EObjectiveAdd* EObjectiveAdd::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041140)
void EObjectiveInsert_MakeEvent_Callback(const EObjectiveInsert::Record *record) {
    EObjectiveInsert *event = new EObjectiveInsert;
    if (event != NULL) {
        event->vtable = kEObjectiveInsertVtable;
        event->beforeId = record->beforeId;
        event->id = record->id;
        event->unknown0c = record->unknown0c;
        event->score = record->score;
        event->unknown14 = record->unknown14;
    }
}

void EObjectiveInsert::Process() {
    glbMissionManager->InsertObjective(beforeId, id, unknown0c, score, unknown14);
}

// FUNC_AT(0x000410e0)
void EObjectiveInsert::Destruct() {
    vtable = kEObjectiveInsertVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048050)
EObjectiveInsert* EObjectiveInsert::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041010)
void EObjectiveDisplayText_MakeEvent_Callback(const EObjectiveDisplayText::Record *record) {
    EObjectiveDisplayText *event = new EObjectiveDisplayText;
    if (event != NULL) {
        event->vtable = kEObjectiveDisplayTextVtable;
        event->id = record->id;
        event->flag = record->flag;
    }
}

void EObjectiveDisplayText::Process() {
    GHud::TheApp()->TriggerObjectiveMsg(kObjectiveMsgCurrent, id, flag != 0);
}

// FUNC_AT(0x00040fb0)
void EObjectiveDisplayText::Destruct() {
    vtable = kEObjectiveDisplayTextVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048010)
EObjectiveDisplayText* EObjectiveDisplayText::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000410b0)
void EObjectiveFail_MakeEvent_Callback(const ObjectiveChangeRecord *record) {
    EObjectiveFail *event = new EObjectiveFail;
    if (event != NULL) {
        event->vtable = kEObjectiveFailVtable;
        event->id = record->id;
        event->showMessage = record->showMessage;
    }
}

void EObjectiveFail::Process() {
    glbMissionManager->FailObjective(id);
    if (showMessage)
        GHud::TheApp()->TriggerObjectiveMsg(kObjectiveMsgFailed, id, false);
}

// FUNC_AT(0x00041040)
void EObjectiveFail::Destruct() {
    vtable = kEObjectiveFailVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048030)
EObjectiveFail* EObjectiveFail::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000411f0)
void EObjectivePass_MakeEvent_Callback(const ObjectiveChangeRecord *record) {
    EObjectivePass *event = new EObjectivePass;
    if (event != NULL) {
        event->vtable = kEObjectivePassVtable;
        event->id = record->id;
        event->showMessage = record->showMessage;
    }
}

void EObjectivePass::Process() {
    glbMissionManager->PassObjective(id);
    if (showMessage)
        GHud::TheApp()->TriggerObjectiveMsg(kObjectiveMsgPassed, id, false);
}

// FUNC_AT(0x00041180)
void EObjectivePass::Destruct() {
    vtable = kEObjectivePassVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048070)
EObjectivePass* EObjectivePass::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041290)
void EObjectiveSetCurrent_MakeEvent_Callback(const ObjectiveChangeRecord *record) {
    EObjectiveSetCurrent *event = new EObjectiveSetCurrent;
    if (event != NULL) {
        event->vtable = kEObjectiveSetCurrentVtable;
        event->id = record->id;
        event->showMessage = record->showMessage;
    }
}

void EObjectiveSetCurrent::Process() {
    glbMissionManager->SetCurrentObjective(id);
    if (showMessage)
        GHud::TheApp()->TriggerObjectiveMsg(kObjectiveMsgCurrent, id, false);
}

// FUNC_AT(0x00041220)
void EObjectiveSetCurrent::Destruct() {
    vtable = kEObjectiveSetCurrentVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048090)
EObjectiveSetCurrent* EObjectiveSetCurrent::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041310)
void EObjectiveSetIncomplete_MakeEvent_Callback(const EObjectiveSetIncomplete::Record *record) {
    EObjectiveSetIncomplete *event = new EObjectiveSetIncomplete;
    if (event != NULL) {
        event->vtable = kEObjectiveSetIncompleteVtable;
        event->id = record->id;
    }
}

void EObjectiveSetIncomplete::Process() {
    glbMissionManager->SetIncompleteObjective(id);
}

// FUNC_AT(0x000412c0)
void EObjectiveSetIncomplete::Destruct() {
    vtable = kEObjectiveSetIncompleteVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x000480b0)
EObjectiveSetIncomplete* EObjectiveSetIncomplete::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// ---- triggers

// FUNC_AT(0x0003fd20)
void EFireEventList_MakeEvent_Callback(const EFireEventList::Record *record) {
    EFireEventList *event = new EFireEventList;
    if (event != NULL) {
        event->vtable = kEFireEventListVtable;
        event->events = record->events;
    }
}

// The count is compared unsigned, and re-read after each event.
void EFireEventList::Process() {
    for (uint32_t i = 0; i < uint32_t(events->count); i++) {
        TriggerEvent *event = &events->Events()[i];
        RegisterEvent::LookupEvent(event->type)(reinterpret_cast<uintptr_t>(event->Data()));
    }
}

// FUNC_AT(0x0003fca0)
void EFireEventList::Destruct() {
    vtable = kEFireEventListVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047ab0)
EFireEventList* EFireEventList::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x0003fe40)
void EFireRandomTrigger_MakeEvent_Callback(const EFireRandomTrigger::Record *record) {
    EFireRandomTrigger *event = new EFireRandomTrigger;
    if (event != NULL) {
        event->vtable = kEFireRandomTriggerVtable;
        for (int i = 0; i < 4; i++) {
            event->choices[i].trigger = reinterpret_cast<WTrigger *>(uintptr_t(record->choices[i].trigger.value));
            event->choices[i].weight = record->choices[i].weight;
        }
        event->flag = gEventDynamicData.flag;
        event->index = gEventDynamicData.index;
    }
}

// FUNC_AT(0x0003feb0)
void EFireRandomTrigger_ResolveEvent_Callback(EFireRandomTrigger::Record *record, UGroup *shared, UGroup *parent) {
    for (int i = 0; i < 4; i++)
        record->choices[i].trigger.Construct(shared, parent);
}

// The draw is the renderer's random sequence (RandomScaled's, inlined). The running sums of the weights are kept
// on the x87 stack, rounded to floats only where they are stored.
void EFireRandomTrigger::Process() {
    float roll = (float)RandomScaled(100.0f);
    double sum = (double)choices[0].weight + choices[1].weight;
    float upToSecond = (float)sum;
    float upToThird = (float)(sum + choices[2].weight);
    WTrigger *chosen;
    if (roll <= choices[0].weight)
        chosen = choices[0].trigger;
    else if (roll <= upToSecond)
        chosen = choices[1].trigger;
    else if (roll <= upToThird)
        chosen = choices[2].trigger;
    else if (roll <= (double)upToThird + choices[3].weight)
        chosen = choices[3].trigger;
    else
        return;
    if (chosen != NULL)
        chosen->FireEvents(flag, index, NULL);
}

// FUNC_AT(0x0003fd40)
void EFireRandomTrigger::Destruct() {
    vtable = kEFireRandomTriggerVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047ad0)
EFireRandomTrigger* EFireRandomTrigger::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040020)
void EFireTriggerSpeedCondition_MakeEvent_Callback(const EFireTriggerSpeedCondition::Record *record) {
    EFireTriggerSpeedCondition *event = new EFireTriggerSpeedCondition;
    if (event != NULL) {
        event->vtable = kEFireTriggerSpeedConditionVtable;
        event->trigger = record->trigger;
        event->speed = record->speed;
        event->below = record->below;
        event->flag = gEventDynamicData.flag;
        event->index = gEventDynamicData.index;
    }
}

void EFireTriggerSpeedCondition::Process() {
    if (trigger == NULL)
        return;
    const Coord3 &velocity = fgSimulation.GetRigidBody(index)->velocity;
    float bodySpeed = VU0_sqrt((float)((double)velocity.x * velocity.x + (double)velocity.y * velocity.y +
                                       (double)velocity.z * velocity.z));
    if (below != 0 ? bodySpeed < speed : bodySpeed > speed)
        trigger->FireEvents(flag, index, NULL);
}

// FUNC_AT(0x0003ff60)
void EFireTriggerSpeedCondition::Destruct() {
    vtable = kEFireTriggerSpeedConditionVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047af0)
EFireTriggerSpeedCondition* EFireTriggerSpeedCondition::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000405b0)
void EInflictDamage_MakeEvent_Callback(const EInflictDamage::Record *record) {
    EInflictDamage *event = new EInflictDamage;
    if (event != NULL) {
        event->vtable = kEInflictDamageVtable;
        event->index = gEventDynamicData.index;
        event->flag = gEventDynamicData.flag;
        event->trigger = gEventDynamicData.trigger;
        event->amount = record->amount;
    }
}

void EInflictDamage::Process() {
    if (flag || index >= kMaxRigidBodies)
        return;
    PhysicsObject *object = fgSimulation.rigidBodyOwners[index];
    object->ApplyDamageVirtual(trigger, object->GetPosition(), amount, 0.0f, kDamageInflicted, DamageSourceSig);
}

// FUNC_AT(0x00040530)
void EInflictDamage::Destruct() {
    vtable = kEInflictDamageVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047e70)
EInflictDamage* EInflictDamage::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// ---- world objects

// FUNC_AT(0x00040320)
void EHideInstance_MakeEvent_Callback(const void *) {
    EHideInstance *event = new EHideInstance;
    if (event != NULL) {
        event->vtable = kEHideInstanceVtable;
        event->instance = gEventDynamicData.instance;
    }
}

void EHideInstance::Process() {
    if (instance != NULL)
        instance->flags |= kInstanceHidden;
}

// FUNC_AT(0x00040300)
void EHideInstance::Destruct() {
    vtable = kEHideInstanceVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047b50)
EHideInstance* EHideInstance::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000403c0)
void EHideInstanceStatic_MakeEvent_Callback(const EHideInstanceStatic::Record *record) {
    EHideInstanceStatic *event = new EHideInstanceStatic;
    if (event != NULL) {
        event->vtable = kEHideInstanceStaticVtable;
        event->instance = record->instance;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

void EHideInstanceStatic::Process() {
    if (instance == NULL)
        return;
    RSceneObj *object = fgWorld->GetSceneObjFromInstance(instance);
    if (object != NULL)
        object->Hide();
    else if (sceneObj != NULL)
        instance = sceneObj->ConvertInstanceToLocal(instance);
    instance->flags |= kInstanceHidden;
}

// FUNC_AT(0x00040340)
void EHideInstanceStatic::Destruct() {
    vtable = kEHideInstanceStaticVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047b70)
EHideInstanceStatic* EHideInstanceStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000403f0)
EHitWindow* EHitWindow::Construct(Coord4 point, Coord4 normal, WindowHit hit, int flag) {
    vtable = kEHitWindowVtable;
    this->point = point;
    this->normal = normal;
    this->hit = hit;
    this->flag = flag;
    return this;
}

// FUNC_AT(0x00040450)
void EHitWindow_MakeEvent_Callback(const EHitWindow *record) {
    EHitWindow *event = new EHitWindow;
    if (event != NULL)
        event->Construct(record->point, record->normal, record->hit, record->flag);
}

// The sounds are written to without a check that they were made, as in the original.
void EHitWindow::Process() {
    if (hit.hits == 0) {
        ABank *bank = fgBanks[kBankSlotGlass];
        void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
        AOneShotSound *sound = NULL;
        if (memory != NULL) {
            const char *patch = kGlassShatterPatches[gGlassShatterCount & 1];
            gGlassShatterCount++;
            sound = static_cast<AOneShotSound *>(memory)->ConstructAt(bank->handle, bank->index.Lookup(patch),
                                                                      Xyz(&point), 1.0f, "Explosions", false);
        }
        sound->minDistance = 5.0f;
        sound->maxDistance = 200.0f;
        sound->maxDistanceSq = 40000.0f;
        sound->fxLevel = 1.0f;
        if (point.y < 10.0f) {
            if ((gStoreAlarmCount & 7) == 0) {
                bank = fgBanks[kBankSlotStoreAlarm];
                memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
                AOneShotSound *alarm = NULL;
                if (memory != NULL)
                    alarm = static_cast<AOneShotSound *>(memory)->ConstructAt(bank->handle,
                                                                              bank->index.Lookup("SFX_StoreAlarm"),
                                                                              Xyz(&point), 1.0f, "Explosions", false);
                alarm->minDistance = 2.0f;
                alarm->maxDistance = 70.0f;
                alarm->maxDistanceSq = 4900.0f;
            }
            gStoreAlarmCount++;
        }
    } else if (hit.hits < 5) {
        ABank *bank = fgBanks[kBankSlotHeadlight];
        void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
        AOneShotSound *sound = NULL;
        if (memory != NULL)
            sound = static_cast<AOneShotSound *>(memory)->ConstructAt(bank->handle, bank->index.Lookup("SFX_Headlight"),
                                                                      Xyz(&point), 1.0f, "Explosions", false);
        sound->minDistance = 5.0f;
        sound->maxDistance = 200.0f;
        sound->maxDistanceSq = 40000.0f;
        sound->fxLevel = 1.0f;
    }

    if (hit.hits < 1) {
        alignas(16) Coord4 rotation;
        VU0_m4toquat(&rotation, &kIdentity);
        UGroup *glass = GFX::LookupReference("Wd_Glass");
        GFX::TriggerIncidental(glass, Xyz(&point), &rotation, Xyz(&kZero), &kUnitW, NULL, 0, NULL);
        if (flag) {
            alignas(16) Coord4 middle;
            middle.x = (float)(((double)hit.corner1.x + hit.corner0.x) * 0.5);
            middle.y = (float)(((double)hit.corner1.y + hit.corner0.y) * 0.5);
            middle.z = (float)(((double)hit.corner1.z + hit.corner0.z) * 0.5);
            middle.w = 0.0f;    // stack garbage in the original
            UGroup *smoke = GFX::LookupReference("Wd_Wsmk");
            GFX::TriggerIncidental(smoke, Xyz(&middle), &rotation, Xyz(&kZero), &kUnitW, NULL, 0, NULL);
        }
    }
}

// FUNC_AT(0x00047b90)
void EHitWindow::Destruct() {
    vtable = kEHitWindowVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x0004c7d0)
EHitWindow* EHitWindow::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000408f0)
void EKickObject_MakeEvent_Callback(const EKickObject::Record *record) {
    EKickObject *event = new EKickObject;
    if (event != NULL) {
        event->vtable = kEKickObjectVtable;
        event->flag = gEventDynamicData.flag;
        event->index = gEventDynamicData.index;
        event->impulse = record->impulse;
        event->impulseRandom = record->impulseRandom;
    }
}

// The impulse is at one of the body's levers (in the world's frame), scaled by four random bytes each 0..1 -
// unscaled, at the body's centre, if the player's car is touching it - pushing by the impulse plus the fourth
// byte's share of impulseRandom.
void EKickObject::Process() {
    RigidBody *body = fgSimulation.GetRigidBody(index);
    if (body->kind == 1)
        return;
    RigidBodyInfo *info = body->info;
    int lever = int32_t(fgSimulation.random->Generate() * 4) >> 16;
    CollisionImpact impact;
    impact.Construct();
    alignas(16) Coord4 centre = {body->position.x, body->position.y, body->position.z, 1.0f};
    alignas(16) Coord4 point;
    VU0_v4copy(&centre, &point);
    alignas(16) Coord4 offset;
    BytesToCoordXYZW(fgSimulation.random->GenerateBits(), NULL, NULL, NULL, &offset);
    PhysicsObject *player = fgSimulation.cars.first[0];
    Coord3 *playerPosition = player->GetPosition();
    float reach = player->GetRadius() + body->radius;
    if (vec3distance(playerPosition, &point) < (double)reach + 0.25) {
        offset.x = 0.0f;
        offset.y = 0.0f;
        offset.z = 0.0f;
        offset.w = 0.0f;
    }
    VU0_v4multxyz(&info->worldLevers[lever], &offset, &offset);
    VU0_v3add(&offset, &point, &offset);
    float push = (float)((double)offset.w * impulseRandom + impulse);
    offset.w = 0.0f;
    body->GenerateImpulse(&impact, &kUp, &offset, push, false, 0, 1.0f);
    fgSimulation.rigidBodyOwners[index]->ApplyDamageVirtual(&body->position, &body->position, 1000000.0f, 0.5f,
                                                            kDamageKick, DamageSourceSig);
}

// FUNC_AT(0x0004c7f0)
void EKickObject::Destruct() {
    vtable = kEKickObjectVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x0004ec80)
EKickObject* EKickObject::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040a00)
void EKillObject_MakeEvent_Callback(const EKillObject::Record *record) {
    EKillObject *event = new EKillObject;
    if (event != NULL) {
        event->vtable = kEKillObjectVtable;
        event->instance = record->instance;
        event->sceneObj = gEventDynamicData.sceneObj;
    }
}

void EKillObject::Process() {
    if (sceneObj == NULL)
        sceneObj = fgWorld->GetSceneObjFromInstance(instance);
    if (sceneObj == NULL)
        return;
    PhysicsObject *physics = sceneObj->physics;
    if (physics == NULL)
        return;
    AIGroundVehicle *vehicle = AIVehicleController::Get()->FindAgentGroundVehiclePtr(physics);
    if (vehicle != NULL)
        fgAIElementController->ForceVehicleToSleep(vehicle);
    else
        fgSimulation.DeletePhysicsObject(physics);
}

// FUNC_AT(0x00040960)
void EKillObject::Destruct() {
    vtable = kEKillObjectVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047f10)
EKillObject* EKillObject::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040aa0)
void EKillSentry_MakeEvent_Callback(const EKillSentry::Record *record) {
    EKillSentry *event = new EKillSentry;
    if (event != NULL) {
        event->vtable = kEKillSentryVtable;
        event->instance = record->instance;
    }
}

void EKillSentry::Process() {
    if (instance == NULL)
        return;
    RSceneObj *object = fgWorld->GetSceneObjFromInstance(instance);
    if (object != NULL && object->physics != NULL)
        fgSimulation.DeletePhysicsObjectThunk(object->physics);
}

// FUNC_AT(0x00040a30)
void EKillSentry::Destruct() {
    vtable = kEKillSentryVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047f30)
EKillSentry* EKillSentry::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040e10)
void EMuzzleFlash_MakeEvent_Callback(const EMuzzleFlash::Record *record) {
    EMuzzleFlash *event = new EMuzzleFlash;
    if (event != NULL) {
        event->vtable = kEMuzzleFlashVtable;
        event->effect = record->effect;
        event->sceneObj = gEventDynamicData.sceneObj;
        event->instanceIndex = gEventDynamicData.instanceIndex;
    }
}

// TriggerFX's second argument is whether the flash's position (in the instance's frame) has a negative x. The
// direction is the effect's rotation turned by the instance's frame without its translation.
void EMuzzleFlash::Process() {
    if (effect == NULL || sceneObj == NULL)
        return;
    uint32_t system = sceneObj->GetInstanceSystemID(instanceIndex);
    alignas(16) MATRIX4 frame;
    sceneObj->GetInstancePosition(effect->instance, &frame, false);
    alignas(16) Coord4 position;
    MATRIX4_TransformPoint(&frame, &effect->position, &position);
    frame.mtx[3][0] = 0.0f;
    frame.mtx[3][1] = 0.0f;
    frame.mtx[3][2] = 0.0f;
    alignas(16) Coord4 direction = {0.0f, 1.0f, 0.0f, 0.0f};
    alignas(16) MATRIX4 rotation;
    VU0_quattom4(&rotation, &effect->gfx.rotation);
    VU0_MATRIX4_mult(&rotation, &rotation, &frame);
    VU0_MATRIX4_vect3mult(&direction, &rotation, &direction);
    sceneObj->TriggerFXVirtual(kFXMuzzleFlash, position.x < 0.0f, system, &position, &direction, -1.0f);
}

// FUNC_AT(0x00040cb0)
void EMuzzleFlash::Destruct() {
    vtable = kEMuzzleFlashVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047fb0)
EMuzzleFlash* EMuzzleFlash::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040ee0)
void ENuclearBlast_MakeEvent_Callback(const ENuclearBlast::Record *record) {
    ENuclearBlast *event = new ENuclearBlast;
    if (event != NULL) {
        event->vtable = kENuclearBlastVtable;
        event->strength = record->strength;
        event->trigger = record->trigger;
    }
}

void ENuclearBlast::Process() {
    alignas(16) Coord3 position = *trigger;
    if (strength < 0.0f)
        strength = 1.0f;
    WeaponManager->StartNuclearSequence(&position, strength);
}

// FUNC_AT(0x00040e50)
void ENuclearBlast::Destruct() {
    vtable = kENuclearBlastVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047fd0)
ENuclearBlast* ENuclearBlast::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// ---- the AI and the player's car

// FUNC_AT(0x000400e0)
void EForceCarStop_MakeEvent_Callback(const EForceCarStop::Record *record) {
    EForceCarStop *event = new EForceCarStop;
    if (event != NULL) {
        event->vtable = kEForceCarStopVtable;
        event->hard = record->hard;
    }
}

void EForceCarStop::Process() {
    fgSimulation.cars.first[0]->ForceStopOn(hard != 0 ? 3 : 1);
}

// FUNC_AT(0x0004c770)
void EForceCarStop::Destruct() {
    vtable = kEForceCarStopVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x0004ec60)
EForceCarStop* EForceCarStop::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// The road spawner is asked for twice; the second answer is not used.
// FUNC_AT(0x00040100)
EForceNearPedRespawn* EForceNearPedRespawn::Construct(float unknown04, float unknown08) {
    vtable = kEForceNearPedRespawnVtable;
    this->unknown04 = unknown04;
    this->unknown08 = unknown08;
    AIRoadSpawn::Get()->RefreshSpawnData(true, unknown04, unknown08);
    for (int i = 0; i < Pedestrians.count; i++)
        Pedestrians.peds[i]->Recycle();
    AIRoadSpawn::Get();
    ForceNearRespawn = false;
    return this;
}

// FUNC_AT(0x000401a0)
void EForceNearPedRespawn_MakeEvent_Callback(const EForceNearPedRespawn::Record *record) {
    EForceNearPedRespawn *event = new EForceNearPedRespawn;
    if (event != NULL)
        event->Construct(record->unknown04, record->unknown08);
}

// FUNC_AT(0x00048700)
EForceNearPedRespawn* EForceNearPedRespawn::Delete(unsigned flags) {
    return DeleteBaseEvent(this, flags);
}

// FUNC_AT(0x000408c0)
void EKickAIElement_MakeEvent_Callback(const EKickAIElement::Record *record) {
    EKickAIElement *event = new EKickAIElement;
    if (event != NULL) {
        event->vtable = kEKickAIElementVtable;
        event->element = record->element;
        event->trigger = record->trigger;
        event->force = record->force;
    }
}

// A vehicle element's helicopter flag is read and not used.
void EKickAIElement::Process() {
    CARP::AIElement *ai = fgAIElementController->RedirectCarpElement(element);
    AIElementTracking *tracking = fgAIElementController->GetCarpElementTracking(element);
    PhysicsObject *target;
    switch (ai->type) {
    case CARP::kAIElementCharacter:
        target = ai->person.human;
        break;
    case CARP::kAIElementVehicle:
        target = ai->object;
        break;
    default:
        return;
    }
    if (target == NULL)
        return;
    alignas(16) Coord4 from = {tracking->position.x, tracking->position.y, tracking->position.z, 0.0f};
    alignas(16) Coord4 to = {trigger->x, trigger->y, trigger->z, 0.0f};
    alignas(16) Coord4 push;
    VU0_v4sub(&from, &to, &push);
    VU0_v4unitxyz(&push, &push);
    VU0_v4scale(&push, force * 100000.0f, &push);
    alignas(16) Coord4 spin = {};
    BytesToCoordXYZ(fgSimulation.random->GenerateBits(), &kSpinOffset, &kSpinScale, NULL, &spin);
    VU0_v4scale(&spin, force * 100.0f, &spin);
    target->ApplyForces(Xyz(&push), Xyz(&spin));
}

// FUNC_AT(0x00040710)
void EKickAIElement::Destruct() {
    vtable = kEKickAIElementVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00047ef0)
EKickAIElement* EKickAIElement::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040930)
void EKillAIElement_MakeEvent_Callback(const EKillAIElement::Record *record) {
    EKillAIElement *event = new EKillAIElement;
    if (event != NULL) {
        event->vtable = kEKillAIElementVtable;
        event->element = record->element;
        event->scores = record->scores;
    }
}

// The damage is from the element's tracked position, along the line from the player's car to it.
void EKillAIElement::Process() {
    if (element == NULL)
        return;
    CARP::AIElement *ai = fgAIElementController->RedirectCarpElement(element);
    AIElementTracking *tracking = fgAIElementController->GetCarpElementTracking(element);
    alignas(16) Coord4 position = {tracking->position.x, tracking->position.y, tracking->position.z, 0.0f};
    Coord3 *player = fgSimulation.cars.first[0]->GetPosition();
    alignas(16) Coord4 playerPosition = {player->x, player->y, player->z, 0.0f};
    alignas(16) Coord4 direction = {};
    VU0_v4sub(&position, &playerPosition, &direction);

    PhysicsObject *victim;
    if (ai->type == CARP::kAIElementCharacter) {
        victim = ai->person.human;
        if (scores && ai->character != NULL && ai->character->scoreable) {
            glbMissionManager->IncKills(1);
            ai->character->scoreable = false;
        }
    } else if (ai->type == CARP::kAIElementVehicle) {
        victim = ai->object;
        // The test the wrong way round, as the original has it: a helicopter is handled as a car, a car as a
        // helicopter
        if (ai->car.helicopter) {
            if (scores) {
                if (victim == NULL)
                    return;
                PBondCar *car = static_cast<PBondCar *>(victim);
                if (car->scoreable) {
                    glbMissionManager->IncKills(1);
                    car->PVehicle::SetScoreable(false);   // through the vtable
                }
            }
        } else if (scores) {
            if (victim == NULL)
                return;
            if (static_cast<PHelicopter *>(victim)->scoreable) {
                glbMissionManager->IncKills(1);
                static_cast<PHelicopter *>(ai->object)->scoreable = false;
            }
        }
    } else {
        return;
    }
    if (victim != NULL)
        victim->ApplyDamageVirtual(&position, &direction, 1000000.0f, 0.5f, kDamageKick, DamageSourceSig);
}

// FUNC_AT(0x0004ca20)
void EKillAIElement::Destruct() {
    vtable = kEKillAIElementVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x0004eca0)
EKillAIElement* EKillAIElement::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00040c60)
void EMaybeActivateAIElement_MakeEvent_Callback(const EMaybeActivateAIElement::Record *record) {
    EMaybeActivateAIElement *event = new EMaybeActivateAIElement;
    if (event != NULL) {
        event->vtable = kEMaybeActivateAIElementVtable;
        event->element = record->element;
        event->vehicleElement = record->vehicleElement;
        event->chance = record->chance;
        event->trigger = gEventDynamicData.trigger;
        event->flag = gEventDynamicData.flag;
        event->index = gEventDynamicData.index;
    }
}

void EMaybeActivateAIElement::Process() {
    bool allowed = true;
    if (trigger != NULL) {
        if (vehicleElement != NULL) {
            CARP::AIElement *ai = fgAIElementController->RedirectCarpElement(vehicleElement);
            if (ai->state == kElementAwake) {
                AIVehicle *vehicle = ai->vehicle;
                if (flag) {
                    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(index);
                    if (vehicle->vehicleType == kAIHelicopter && vehicle->active) {
                        PhysicsObject *physics = vehicle->GetPhysicsObject();
                        if (fgSimulation.GetSimpleRigidBody(physics->rigidBodySlot) != body)
                            return;
                        trigger->flags &= ~WTrigger::kEnabled;
                        CRT_printf("The Helicopter hit the Trigger.\n");
                    }
                } else {
                    RigidBody *body = fgSimulation.GetRigidBody(index);
                    if (vehicle->vehicleType == kAIGroundVehicle && vehicle->active) {
                        PhysicsObject *physics = vehicle->GetPhysicsObject();
                        if (fgSimulation.GetRigidBody(physics->rigidBodySlot) != body)
                            return;
                        trigger->flags &= ~WTrigger::kEnabled;
                    }
                }
            }
        } else {
            if (flag)
                return;
            RigidBody *body = fgSimulation.GetRigidBody(index);
            if (fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot) != body)
                return;
            trigger->flags &= ~WTrigger::kEnabled;
        }
    }

    CARP::AIElement *ai = fgAIElementController->RedirectCarpElement(element);
    if (gLimitHenchSpawns && ai->type == CARP::kAIElementVehicle && !(ai->flags & kElementForceAwake) &&
        !fgAIElementController->OkToSpawnHench())
        allowed = false;
    int roll = int32_t(fgSimulation.random->Generate() * 100) >> 16;
    if (roll >= chance && allowed)
        ai->flags &= ~kElementDeactivated;
}

// FUNC_AT(0x0004cbc0)
void EMaybeActivateAIElement::Destruct() {
    vtable = kEMaybeActivateAIElementVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x0004ecc0)
EMaybeActivateAIElement* EMaybeActivateAIElement::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// ---- paths

// FUNC_AT(0x00041410)
void EPathChangeThrottleDynamic_MakeEvent_Callback(const EPathChangeThrottleDynamic::Record *record) {
    EPathChangeThrottleDynamic *event = new EPathChangeThrottleDynamic;
    if (event != NULL) {
        event->vtable = kEPathChangeThrottleDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->accelDelay = record->accelDelay;
        event->acceleration = record->acceleration;
        event->throttle = record->throttle;
    }
}

void EPathChangeThrottleDynamic::Process() {
    if (instance == NULL)
        return;
    RSceneObj *object = fgWorld->GetSceneObjFromInstance(instance);
    if (object == NULL)
        return;
    RPathHandle *path = object->pathHandle;
    if (path == NULL)
        return;
    path->SetAccelDelay(accelDelay);
    path->SetAcceleration(acceleration);
    path->SetDesiredThrottle(throttle);
    path->SetRunning(true);
}

// FUNC_AT(0x00041380)
void EPathChangeThrottleDynamic::Destruct() {
    vtable = kEPathChangeThrottleDynamicVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x000480d0)
EPathChangeThrottleDynamic* EPathChangeThrottleDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000414e0)
void EPathChangeThrottleStatic_MakeEvent_Callback(const EPathChangeThrottleStatic::Record *record) {
    EPathChangeThrottleStatic *event = new EPathChangeThrottleStatic;
    if (event != NULL) {
        event->vtable = kEPathChangeThrottleStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->accelDelay = record->accelDelay;
        event->acceleration = record->acceleration;
        event->throttle = record->throttle;
    }
}

void EPathChangeThrottleStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path == NULL)
        return;
    path->SetAccelDelay(accelDelay);
    path->SetAcceleration(acceleration);
    path->SetDesiredThrottle(throttle);
    path->SetRunning(true);
}

// FUNC_AT(0x00041450)
void EPathChangeThrottleStatic::Destruct() {
    vtable = kEPathChangeThrottleStaticVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x000480f0)
EPathChangeThrottleStatic* EPathChangeThrottleStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000415c0)
void EPathMultiplyThrottleDynamic_MakeEvent_Callback(const EPathMultiplyThrottleDynamic::Record *record) {
    EPathMultiplyThrottleDynamic *event = new EPathMultiplyThrottleDynamic;
    if (event != NULL) {
        event->vtable = kEPathMultiplyThrottleDynamicVtable;
        event->instance = gEventDynamicData.instance;
        event->accelDelay = record->accelDelay;
        event->acceleration = record->acceleration;
        event->scale = record->scale;
    }
}

void EPathMultiplyThrottleDynamic::Process() {
    if (instance == NULL)
        return;
    RSceneObj *object = fgWorld->GetSceneObjFromInstance(instance);
    if (object == NULL)
        return;
    RPathHandle *path = object->pathHandle;
    if (path == NULL)
        return;
    float throttle = path->throttle;
    path->SetAccelDelay(accelDelay);
    path->SetAcceleration(acceleration);
    path->SetDesiredThrottle(throttle * scale);
    path->SetRunning(true);
}

// FUNC_AT(0x00041520)
void EPathMultiplyThrottleDynamic::Destruct() {
    vtable = kEPathMultiplyThrottleDynamicVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048110)
EPathMultiplyThrottleDynamic* EPathMultiplyThrottleDynamic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x000416a0)
void EPathMultiplyThrottleStatic_MakeEvent_Callback(const EPathMultiplyThrottleStatic::Record *record) {
    EPathMultiplyThrottleStatic *event = new EPathMultiplyThrottleStatic;
    if (event != NULL) {
        event->vtable = kEPathMultiplyThrottleStaticVtable;
        event->instance = record->instance;
        event->element = record->element;
        event->accelDelay = record->accelDelay;
        event->acceleration = record->acceleration;
        event->scale = record->scale;
    }
}

void EPathMultiplyThrottleStatic::Process() {
    RPathHandle *path = EPathUtil_GetFromInstanceOrAI(instance, element);
    if (path == NULL)
        return;
    float throttle = path->throttle;
    path->SetAccelDelay(accelDelay);
    path->SetAcceleration(acceleration);
    path->SetDesiredThrottle(throttle * scale);
    path->SetRunning(true);
}

// FUNC_AT(0x00041600)
void EPathMultiplyThrottleStatic::Destruct() {
    vtable = kEPathMultiplyThrottleStaticVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048130)
EPathMultiplyThrottleStatic* EPathMultiplyThrottleStatic::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}

// FUNC_AT(0x00041810)
void EPathSFX_MakeEvent_Callback(const EPathSFX::Record *record) {
    EPathSFX *event = new EPathSFX;
    if (event != NULL) {
        event->vtable = kEPathSFXVtable;
        event->instance = record->instance;
        event->patch = record->patch;
        event->unknown0c = record->unknown0c;
        event->minDistance = record->minDistance;
        event->maxDistance = record->maxDistance;
    }
}

// The sound is written to without a check that it was made, as in the original.
void EPathSFX::Process() {
    if (!gSoundOption)
        return;
    RSceneObj *object = fgWorld->GetSceneObjFromInstance(instance);
    if (object == NULL)
        return;
    RPathHandle *path = object->pathHandle;
    if (path == NULL || !path->running)
        return;
    ABank *bank = fgBanks[kBankSlotPath];
    void *memory = ABaseSound::OperatorNew(sizeof(ASound), "ASound");
    ASound *sound = NULL;
    if (memory != NULL)
        sound = static_cast<ASound *>(memory)->Construct(bank->handle, bank->index.Lookup(patch), "Object Sounds");
    sound->minDistance = minDistance;
    sound->maxDistance = maxDistance;
    sound->maxDistanceSq = maxDistance * maxDistance;

    PathSFXUpdateData *update = static_cast<PathSFXUpdateData *>(OperatorNew(sizeof(PathSFXUpdateData)));
    update->self = update;
    update->path = path;
    update->sound = sound;
    update->unknown10 = unknown0c;
    update->taskNum = fgScheduler->s_SimRate->AddTask(kEventPathSFXUpdate, reinterpret_cast<uintptr_t>(update), 0,
                                                      true, 0, 0);
}

// FUNC_AT(0x000416e0)
void EPathSFX::Destruct() {
    vtable = kEPathSFXVtable;
    Process();
    vtable = kEventVtable;
}

// FUNC_AT(0x00048150)
EPathSFX* EPathSFX::Delete(unsigned flags) {
    return DeleteEvent(this, flags);
}
