#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif
#pragma fp_contract(off)

#include "MissionManager.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AICharacter.h"                    // fgHandObj
#include "Missile.h"
#include "Simulation.h"
#include "Vehicle.h"                        // PVehicle's slots
#include "WeaponManager.h"
#include "../../common/xbeOverload.h"       // XbeVirtual
#include "../../helpers.h"
#include "../EventManager.hpp"              // Event
#include "../Scheduler.hpp"
#include "../camera/PlayerCamera.h"         // RPlayerCamera
#include "../data/AttributeSet.h"
#include "../data/Carp.h"                   // CARP::Instance
#include "../data/UData.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/CoreFoundation.h"       // ThrowLengthError
#include "../engine/GameInterfaces.hpp"     // GHud
#include "../engine/GameLoop.h"             // Bond_SetExitToDriving
#include "../engine/IOModule.hpp"
#include "../engine/InputConfig.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"                // Ftol
#include "../render/Colorize.h"
#include "../render/RenderHigh.h"           // fgRenderHigh
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"      // fgCollisionMgr
#include "../world/Targeting.h"             // TargetPicker
#include "../world/World.h"                 // fgWorld

// ---------------------------------------------------------------------------------------------------------------
// SMissionManager (0x000b60a0-0x000b9500): its state, the rules, the objectives and its lifetime. The health,
// counters and scores are in MissionScores.cpp.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define GHud_SetAutoDrive ((void (__fastcall *)(GHud *, int, bool on))0x000d9040)
#define GHud_GetBlackFadePct ((float (__fastcall *)(GHud *, int))0x000d7d50)
#define GHud_GetBlackFadeAnimating ((bool (__fastcall *)(GHud *, int))0x000d7d40)
#define GHud_FadeInFromBlack ((void (__fastcall *)(GHud *, int, int steps))0x000d7c70)
#define GHud_FadeOutToBlack ((void (__fastcall *)(GHud *, int, int steps))0x000d7cd0)
#define GHud_GetLetterBoxMode ((int (__fastcall *)(GHud *, int))0x000d9b90)
#define GHud_RadarRadiusInMeters ((float (__fastcall *)(GHud *, int))0x000d7d90)
#define AIElementController_RedirectCarpElement ((CARP::AIElement *(__fastcall *)(void *, int, CARP::AIElement *element))0x000286d0)
#define AIElementController_SyncRuleInfo ((void (__fastcall *)(void *, int, CARP::AIElement *element))0x00028a40)
#define AIElementController_CalcMightAsWellBeDead ((void (__fastcall *)(void *, int))0x00028730)
#define AIElementController_GetNumMightAsWellBeDead ((int (__fastcall *)(void *, int))0x000287c0)
#define ERestart_Construct ((void *(__fastcall *)(void *, int))0x00043460)

// ---- the game's globals
#define fgAIElementController (*(void **)0x001ddee4)
#define fgScheduler (*(Scheduler **)0x001e520c)
#define gSuperEasy BOOL8_AT(0x001e476c)

// XBE_GLOBAL(0x00238f20, 0x2f8)
MissionSet DefaultMissionSet;
// Nothing in the game sets these (names ours, after what reading them does); with gPlayerCanNeverLose,
// MissionHealthTimes100 makes ResetCharInfo give a hundred times the health
// XBE_GLOBAL(0x00239219, 0x1)
uint8_t MissionHealthTimes100;
// XBE_GLOBAL(0x0023921a, 0x1)
uint8_t CarOverrideLotus;
// XBE_GLOBAL(0x0023921b, 0x1)
uint8_t CarOverrideVan;
// XBE_GLOBAL(0x0023921c, 0x1)
uint8_t CarOverrideLimo;
// XBE_GLOBAL(0x0023921d, 0x1)
uint8_t CarOverrideTaxi;
// XBE_GLOBAL(0x0023921e, 0x1)
uint8_t CarOverrideSmart;
// The maths header's constants, this unit's copies as the static initialisers made them (1 / FLT_MIN, 2 pi,
// 1 / 2 pi, 180 / pi and its reciprocal, pi being 3.1415925); nothing reads them
// XBE_GLOBAL(0x00239224, 4)
float MissionManagerInverseFloatMin = 8.507059e+37f;
// XBE_GLOBAL(0x00239228, 4)
float MissionManagerTwoPi = 6.283185f;
// XBE_GLOBAL(0x0023922c, 4)
float MissionManagerInverseTwoPi = 0.15915495f;
// XBE_GLOBAL(0x00239230, 4)
float MissionManagerDegreesPerRadian = 57.295784f;
// XBE_GLOBAL(0x00239234, 4)
float MissionManagerRadiansPerDegree = 0.01745329f;
// The cinematic mask as SetCinematicMode, ClearCinematicMode, Win and Lose last published it (ASnowMobile::PlayCarving
// reads it)
// XBE_GLOBAL(0x00243a64, 4)
int32_t MissionCinematicMode;

namespace {

constexpr int kDefaultAmmo = 99;
constexpr int kMaxSetAmmo = 10000;
constexpr int kObjectiveCheckInterval = 10;     // steps between CheckObjectiveVisibleToUser's
constexpr uint8_t kForceStopMissile = 8;        // ForceStopOn's flag while a missile is steered

// The HUD's pause flag (provisional view of GHud's +0x2b0)
struct MissionHudView {
    uint8_t unknown000[0x2b0];
    uint8_t paused;             // +0x2b0
};

RPlayerCamera *FirstCamera() {
    return fgRenderHigh->views[0].camera;
}

void SetShieldPointLoc(PVehicle *car, float *shield) {
    (car->*XbeVirtual<void (PVehicle::*)(float *)>(car, PVehicle::kSetShieldPointLoc))(shield);
}

void ForceStopOn(PVehicle *car, uint8_t flags) {
    (car->*XbeVirtual<void (PVehicle::*)(uint8_t)>(car, PVehicle::kForceStopOn))(flags);
}

void ForceStopOff(PVehicle *car, uint8_t flags) {
    (car->*XbeVirtual<void (PVehicle::*)(uint8_t)>(car, PVehicle::kForceStopOff))(flags);
}

void DisableTyreBlowOuts(PhysicsObject *vehicle) {
    (vehicle->*XbeVirtual<void (PhysicsObject::*)()>(vehicle, PVehicle::kDisableTyreBlowOuts))();
}

// A rule's scalar deleting destructor, its vtable's slot 0
void DeleteRule(SMissionRule *rule) {
    (rule->*XbeVirtual<decltype(&SMissionRule::Delete)>(rule, 0))(1);
}

CARP::AIElement *Redirect(CARP::AIElement *element) {
    return AIElementController_RedirectCarpElement(fgAIElementController, 0, element);
}

// An objective vehicle's tyres cannot be shot out
void ProtectObjectiveVehicle(CARP::AIElement *element) {
    CARP::AIElement *live = Redirect(element);
    if (live->type == 1 && live->unknown48 == 1 && !live->unknown77 && live->object != NULL)
        DisableTyreBlowOuts(live->object);
}

void RunRules(MissionList *rules) {
    PointerListNode *node = rules->Begin();
    while (node != rules->head) {
        SMissionRule *rule = static_cast<SMissionRule *>(node->value);
        node = node->next;
        rule->DoRule();
    }
}

MissionObjective *FindObjective(MissionList *objectives, int id) {
    for (PointerListNode *node = objectives->Begin(); node != objectives->head; node = node->next) {
        if (static_cast<MissionObjective *>(node->value)->id == id)
            return static_cast<MissionObjective *>(node->value);
    }
    return NULL;
}

MissionObjective *NewObjective(int id, int unknown04, int score, int unknown0c) {
    MissionObjective *objective = static_cast<MissionObjective *>(OperatorNew(sizeof(MissionObjective)));
    if (objective != NULL) {
        objective->id = id;
        objective->unknown04 = unknown04;
        objective->score = score;
        objective->unknown0c = unknown0c;
        objective->state = kObjectiveIncomplete;
    }
    return objective;
}

}  // namespace

// =============================================================================================================
// The lists
// =============================================================================================================

// FUNC_AT(0x000b8490)
PointerListNode* MissionList::BuyHead() {
    PointerListNode *node = static_cast<PointerListNode *>(UMemory::FastAlloc(sizeof(PointerListNode), "STL"));
    if (node != NULL) {
        node->next = node;
        node->prev = node;
    }
    return node;
}

// FUNC_AT(0x000b9090)
void MissionList::IncreaseSize(uint32_t count) {
    if (0x3fffffff - size < count)
        ThrowLengthError("list<T> too long");
    size += count;
}

void MissionList::PushBack(void *value) {
    PointerListNode *end = head;
    PointerListNode *node = BuyNode(end, end->prev, &value);
    IncreaseSize(1);
    end->prev = node;
    node->prev->next = node;
}

// =============================================================================================================
// Lifetime
// =============================================================================================================

// FUNC_AT(0x000b8c30)
SMissionManager* SMissionManager::Construct() {
    objectives.Construct();
    messages.Construct();
    globalRules.Construct();
    for (int stage = 0; stage < kMissionStages; stage++)
        stageRules[stage].Construct();
    for (int timer = 0; timer < kMissionTimers; timer++)
        timers[timer].Construct();
    for (int i = 0; i < kSectionProfiles; i++) {
        MissionSectionProfile &section = sections[i];
        section.name[0] = '\0';
        section.minFramesPerSecond = 1000.0f;
        section.maxFramesPerSecond = 0.0f;
        section.totalFramesPerSecond = 0.0f;
        section.frames = 0;
    }
    rules = NULL;
    unknown428 = -1;
    missionState = kMissionNotStarted;
    autoDrive = 0;
    missileHasTarget = 0;
    ActionQueue *queue = static_cast<ActionQueue *>(UMemory::FastAlloc(sizeof(ActionQueue), "ActionQueue"));
    actionQueue = queue != NULL ? queue->Construct(const_cast<char *>("MissionManQ")) : NULL;
    driveMissile = NULL;
    steerY2 = 0.0f;
    steerX2 = 0.0f;
    steerY = 0.0f;
    steerX = 0.0f;
    difficulty = Launch.difficulty;
    loadCount = 0;

    memset(&DefaultMissionSet, 0, sizeof(DefaultMissionSet));
    strcpy(DefaultMissionSet.name, "vanquish");
    DefaultMissionSet.maxHealth = 3000;
    DefaultMissionSet.health = 3000;
    DefaultMissionSet.maxShield = 1000;
    DefaultMissionSet.shield = 0;
    missionSet = &DefaultMissionSet;
    SetCarType();

    if (!fgWorld->attributes.LookupInt("ACCUMULATE_SCORES", NULL))
        ClearScoreBuffer(&Launch);
    for (int score = 0; score < kScoreCount; score++)
        startScores[score] = Launch.accumulatedScores[score];
    ClearState();
    RestoreLives();
    return this;
}

// FUNC_AT(0x000b8e70)
void SMissionManager::Destruct() {
    DestroyRuleList(&globalRules);
    for (int stage = 0; stage < kMissionStages; stage++)
        DestroyRuleList(&stageRules[stage]);
    stageCount = 0;
    nextStage = 0;
    currentStage = 0;
    if (actionQueue != NULL) {
        actionQueue->Destruct();
        UMemory::FastFree(actionQueue, sizeof(ActionQueue));
    }
    ClearObjectiveList();
    ClearMessageList();
    // (the timers' destructor is an empty function)
    for (int stage = kMissionStages - 1; stage >= 0; stage--)
        stageRules[stage].Destruct();
    globalRules.Destruct();
    messages.Destruct();
    objectives.Destruct();
}

// FUNC_AT(0x000b8ff0)
void SMissionManager::Create() {
    SMissionManager *manager = static_cast<SMissionManager *>(OperatorNew(sizeof(SMissionManager)));
    glbMissionManager = manager != NULL ? manager->Construct() : NULL;
}

// FUNC_AT(0x000b9060)
void SMissionManager::Destroy() {
    SMissionManager *manager = glbMissionManager;
    if (manager != NULL) {
        manager->Destruct();
        OperatorDelete(manager);
    }
    glbMissionManager = NULL;
}

// FUNC_AT(0x000b9140)
void SMissionManager::Load(UData *ruleData, UData *missionSetData) {
    MissionRuleRecord *records = reinterpret_cast<MissionRuleRecord *>(ruleData->Data());
    int count = ruleData->count;
    DestroyRuleList(&globalRules);
    for (int stage = 0; stage < kMissionStages; stage++)
        DestroyRuleList(&stageRules[stage]);
    stageCount = 0;
    nextStage = 0;
    currentStage = 0;
    for (int i = 0; i < count; i++) {
        SMissionRule *rule = SMissionRule::Instantiate(&records[i]);
        int stage = records[i].stage;
        if (stage >= 0) {
            stageRules[stage].PushBack(rule);
            if (records[i].stage >= stageCount)
                stageCount = records[i].stage + 1;
        } else {
            globalRules.PushBack(rule);
        }
    }
    rules = ruleData;
    missionSets = missionSetData;
    if (missionSetData != NULL)
        missionSet = reinterpret_cast<MissionSet *>(missionSetData->Data());
    else
        missionSet = &DefaultMissionSet;
    SetCarType();
    LoadWeaponSet();
    ResetCharInfo();

    // A set without them gets 1s (the level's data is written)
    if (missionSet->enemyGlueFactor[0] == 0.0f) {
        missionSet->enemyGlueFactor[0] = 1.0f;
        missionSet->enemyGlueFactor[1] = 1.0f;
        missionSet->enemyGlueFactor[2] = 1.0f;
    }
    if (missionSet->missionText[0] == 0.0f) {
        missionSet->missionText[0] = 1.0f;
        missionSet->missionText[1] = 1.0f;
        missionSet->missionText[2] = 1.0f;
    }
    if (Launch.unknowna3c == 3 || Launch.unknowna3c == 4)
        events[kEvent05] = 1;
    loadCount++;
}

// FUNC_AT(0x000b8bf0)
void SMissionManager::Reset() {
    actionQueue->Flush();
    timers[0].Reset(0);
    LoadWeaponSet();
    missionState = kMissionNotStarted;
    nextStage = 0;
    ClearState();
}

// FUNC_AT(0x000b8a30)
void SMissionManager::ClearState() {
    stageCount = 0;
    currentStage = 0;
    nextStage = 0;
    shotsFired = 0;
    shotsHit = 0;
    kills = 0;
    damage = 0.0f;
    cinematicMode = 0;
    objectiveCheckStep = 0;
    objectiveSeenStep = 0;
    unknown4fc = 0;
    unknown500 = 0;
    lastDamageStep = 0;
    stylinCount = 0;
    missionEndTime = 0;
    skippingCinematic = 0;
    superEasy = 0;
    lastShotStep = 0;
    if (gSuperEasy)
        superEasy = 1;
    for (int slot = 0; slot < kObjectiveSlots; slot++) {
        objectiveType[slot] = kObjectiveNone;
        objectiveVisible[slot] = 0;
    }
    for (int timer = 0; timer < kMissionTimers; timer++) {
        timers[timer].Reset(0);
        timers[timer].Stop();
    }
    ResetCharInfo();
    memset(events, 0, sizeof(events));
    memset(scores, 0, sizeof(scores));
    for (int action = 0; action < kTimedActions; action++)
        timedActions[action].active = 0;
    for (int counter = 0; counter < kMagicCounters; counter++) {
        magicCounters[counter] = 0;
        switch (counter) {
        case 6:
        case 7:
        case 10:
            magicThresholds[counter] = 6;
            break;
        case 8:
            magicThresholds[counter] = 3;
            break;
        case 9:
            magicThresholds[counter] = 2;
            break;
        case 19:
            magicThresholds[counter] = 4;
            break;
        default:
            magicThresholds[counter] = -1;
            break;
        }
    }
    if (superEasy)
        events[kEventSuperEasy] = 1;
    sectionCount = 0;
    currentSection = -1;
    for (int i = 0; i < kSectionProfiles; i++) {
        MissionSectionProfile &section = sections[i];
        section.name[0] = '\0';
        section.minFramesPerSecond = 1000.0f;
        section.maxFramesPerSecond = 0.0f;
        section.totalFramesPerSecond = 0.0f;
        section.frames = 0;
    }
    ClearObjectiveList();
    ClearMessageList();
}

// FUNC_AT(0x000b8900)
void SMissionManager::DestroyRuleList(MissionList *list) {
    PointerListNode *node = list->Begin();
    while (node != list->head) {
        SMissionRule *rule = static_cast<SMissionRule *>(node->value);
        node = node->next;
        if (rule != NULL)
            DeleteRule(rule);
    }
    PointerListNode *erased;
    list->Erase(&erased, list->Begin(), list->head);
}

// FUNC_AT(0x000b8960)
void SMissionManager::ClearObjectiveList() {
    for (PointerListNode *node = objectives.Begin(); node != objectives.head; node = node->next) {
        if (node->value != NULL)
            OperatorDelete(node->value);
    }
    PointerListNode *erased;
    objectives.Erase(&erased, objectives.Begin(), objectives.head);
}

// FUNC_AT(0x000b89c0)
void SMissionManager::ClearMessageList() {
    for (PointerListNode *node = messages.Begin(); node != messages.head; node = node->next) {
        if (node->value != NULL)
            OperatorDelete(node->value);
    }
    PointerListNode *erased;
    messages.Erase(&erased, messages.Begin(), messages.head);
}

// =============================================================================================================
// State and the rules
// =============================================================================================================

// FUNC_AT(0x000b60a0)
void SMissionManager::SetCarType() {
    carType = missionSet->name;
    if (_stricmp(carType, "db5") != 0 && _stricmp(carType, "z8") != 0)
        return;
    if (CarOverrideLotus)
        carType = "lotus";
    else if (CarOverrideVan)
        carType = "playervan";
    else if (CarOverrideLimo)
        carType = "playerlimo";
    else if (CarOverrideTaxi)
        carType = "playertaxi";
    else if (CarOverrideSmart)
        carType = "playersmart";
}

// FUNC_AT(0x000b6140)
const char* SMissionManager::GetCarType() {
    return (Launch.flags & kLaunchJungleTruck) ? "jungle_truck" : carType;
}

// FUNC_AT(0x000b6160)
void SMissionManager::LoadWeaponSet() {
    WeaponManager->Reset();
    if (missionSet == &DefaultMissionSet) {
        if (strcmp(Launch.missionName, "uw_map") == 0) {
            WeaponManager->LoadWeapon(kWeaponTorpedo, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponRemoteTorpedo, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponQBooster, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponFloatingMine, kDefaultAmmo, 0, 0);
            WeaponManager->SetSecondary(kWeaponTorpedo);
        } else {
            WeaponManager->LoadWeapon(kWeaponGun, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponRocket, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponMissile, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponQPulse, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponQSlick, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponQSmoke, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponQBooster, kDefaultAmmo, 0, 0);
            WeaponManager->LoadWeapon(kWeaponRadar, kDefaultAmmo, 0, 0);
            WeaponManager->SetSecondary(kWeaponRocket);
        }
        return;
    }

    int secondary = -1;
    for (int weapon = 0; weapon < kWeaponCount; weapon++) {
        if (weapon == kWeaponP2K)
            continue;
        MissionSet *set = missionSet;
        if (!set->weaponEnabled[weapon] || set->weaponAmmo[weapon] > kMaxSetAmmo)
            continue;
        WeaponManager->LoadWeapon(weapon, set->weaponAmmo[weapon], set->weaponClipSize[weapon],
                                  set->weaponDamage[weapon]);
        if (secondary == -1 && weapon != kWeaponGun && missionSet->weaponAmmo[weapon] > 0)
            secondary = weapon;
    }
    if (secondary == -1) {
        secondary = kWeaponGun;
        WeaponManager->LoadWeapon(kWeaponGun, 0, 0, 0);
    } else if (secondary == kWeaponUltralightGun) {
        MissionSet *set = missionSet;
        if (set->weaponEnabled[kWeaponUltralightRocket] && set->weaponAmmo[kWeaponUltralightRocket] > 0 &&
            _stricmp(Launch.missionName, "jungleb_mis13b") == 0)
            secondary = kWeaponUltralightRocket;
    }
    WeaponManager->SetSecondary(secondary);
    TargetPicker.ActivateTargeting(kTargetingNormal);
}

// FUNC_AT(0x000b6380)
void SMissionManager::Start() {
    if (missionState != kMissionWon && missionState != kMissionLost)
        missionState = kMissionRunning;
}

// FUNC_AT(0x000b63a0)
void SMissionManager::Stop() {
    if (missionState != kMissionWon && missionState != kMissionLost)
        missionState = kMissionStopped;
}

// FUNC_AT(0x000b64b0)
void SMissionManager::ChangeStage(int stage) {
    nextStage = stage;
}

// FUNC_AT(0x000b64c0)
void SMissionManager::SetAutoDrive(bool on, bool changeControls) {
    if (uint8_t(on) != autoDrive) {
        if (on) {
            if (changeControls) {
                IOModule::GetIOModule()->ReleaseAllButtons();
                InputConfigManager::Get()->SetConfig(kInputPOV, -1);
                IOModule::GetIOModule()->resendAll = true;
            }
            if (fgHandObj != NULL)
                AICharacterHands_ShowHands(fgHandObj, 0);
            TargetPicker.ActivateTargeting(kTargetingAutoDrive);
        } else {
            if (changeControls) {
                IOModule::GetIOModule()->ReleaseAllButtons();
                InputConfigManager::Get()->SetConfig(kInputDriving, -1);
                IOModule::GetIOModule()->resendAll = true;
            }
            if (fgHandObj != NULL)
                AICharacterHands_HideHands(fgHandObj, 0);
            TargetPicker.DeactivateTargeting();
        }
    }
    if (changeControls)
        GHud_SetAutoDrive(GHud::TheApp(), 0, on);
    autoDrive = on;
}

// FUNC_AT(0x000b6590)
int SMissionManager::GoingToSkipCinematic() {
    return skippingCinematic && cinematicMode != 0 ? 1 : 0;
}

// FUNC_AT(0x000b65b0)
int SMissionManager::IsSkippingCinematic() {
    if (skippingCinematic && cinematicMode != 0 && GHud_GetBlackFadePct(GHud::TheApp(), 0) == 1.0f)
        return 1;
    return 0;
}

// FUNC_AT(0x000b7950)
void SMissionManager::ClearCinematicMode(int mask) {
    cinematicMode &= ~mask;
    if (cinematicMode != 0)
        return;
    events[kEventCinematicSkipped] = 0;
    fgScheduler->cinematicModeSkipping = 0;
    MissionCinematicMode = cinematicMode;
    if (GHud_GetBlackFadePct(GHud::TheApp(), 0) > 0.0f || GHud_GetBlackFadeAnimating(GHud::TheApp(), 0))
        GHud_FadeInFromBlack(GHud::TheApp(), 0, fgSimulation.stepsPerSecond >> 1);
    skippingCinematic = 0;
}

// FUNC_AT(0x000b79d0)
void SMissionManager::SkipCinematic() {
    if (cinematicMode != 0 && !skippingCinematic && missionState == kMissionRunning &&
        GHud_GetLetterBoxMode(GHud::TheApp(), 0) == 0 && fgSimulation.stepCount > fgSimulation.stepsPerSecond * 2) {
        events[kEventCinematicSkipped] = 1;
        skippingCinematic = 1;
        GHud_FadeOutToBlack(GHud::TheApp(), 0, fgSimulation.stepsPerSecond >> 1);
    }
}

// FUNC_AT(0x000b8160)
void SMissionManager::SetCinematicMode(int mask) {
    if (mask == 0) {
        ClearCinematicMode(-1);
        MissionCinematicMode = cinematicMode;
        return;
    }
    cinematicMode |= mask;
    MissionCinematicMode = cinematicMode;
}

// FUNC_AT(0x000b72f0)
void SMissionManager::ProgrammerDefinedEvent(int event, const char *) {
    events[event] = 1;
}

// FUNC_AT(0x000b7300)
bool SMissionManager::CheckProgrammerEvent(int event) {
    return events[event];
}

// FUNC_AT(0x000b7310)
void SMissionManager::ClearProgrammerEvent(int event) {
    events[event] = 0;
}

// FUNC_AT(0x000b7320)
void SMissionManager::StartSectionProfile(const char *name) {
    if (name == NULL || strlen(name) == 0) {
        currentSection = -1;
        return;
    }
    if (currentSection >= 0 && strcmp(name, sections[currentSection].name) == 0)
        return;
    for (int i = 0; i < sectionCount; i++) {
        if (strcmp(name, sections[i].name) == 0) {
            currentSection = i;
            return;
        }
    }
    if (sectionCount >= kSectionProfiles) {
        currentSection = -1;
        return;
    }
    MissionSectionProfile &section = sections[sectionCount];
    section.name[0] = '\0';
    section.minFramesPerSecond = 1000.0f;
    section.maxFramesPerSecond = 0.0f;
    section.totalFramesPerSecond = 0.0f;
    section.frames = 0;
    strncpy(section.name, name, sizeof(section.name) - 1);
    currentSection = sectionCount;
    sectionCount++;
}

// FUNC_AT(0x000b7ae0)
void SMissionManager::UpdateAutoDriveMissile() {
    if (driveMissile == NULL)
        return;
    while (!actionQueue->IsEmpty()) {
        ActionRef ref;
        actionQueue->GetAction(&ref);
        ActionData *action = ref.data;
        float value = action != NULL ? action->value : 0.0f;
        float steer;
        switch (action != NULL ? action->action : kNullAction) {
        case kActionChangeCamera:
            if (cinematicMode != 0)
                events[kEventCinematicSkipped] = 1;
            break;
        // Each input of a pair is kept; the one further from the centre steers. On a tie the left stick's
        // horizontal and vertical inputs give way to the right stick's, which keep their new value.
        case kActionCamRotateX:
            steerX = value;
            steer = fabsf(steerX2) < fabsf(value) ? value : steerX2;
            driveMissile->SetUserSteerX(steer);
            break;
        case kActionCamRotateX2:
            steerX2 = value;
            steer = fabsf(value) < fabsf(steerX) ? steerX : value;
            driveMissile->SetUserSteerX(steer);
            break;
        case kActionCamRotateY:
            value = -value;
            steerY = value;
            steer = fabsf(steerY2) < fabsf(value) ? value : steerY2;
            driveMissile->SetUserSteerY(steer);
            break;
        case kActionCamRotateYInv:
            steerY = value;
            steer = fabsf(steerY2) < fabsf(value) ? value : steerY2;
            driveMissile->SetUserSteerY(steer);
            break;
        case kActionCamRotateY2:
            value = -value;
            steerY2 = value;
            steer = fabsf(value) < fabsf(steerY) ? steerY : value;
            driveMissile->SetUserSteerY(steer);
            break;
        case kActionCamRotateY2Inv:
            steerY2 = value;
            steer = fabsf(value) < fabsf(steerY) ? steerY : value;
            driveMissile->SetUserSteerY(steer);
            break;
        case kActionAutoDriveLock:
            missileHasTarget = 1;
            driveMissile->SetTarget(TargetPicker.GetSelectedTarget());
            break;
        case kActionAutoDriveLockRelease:
            missileHasTarget = 0;
            driveMissile->SetTarget(NULL);
            break;
        default:
            break;
        }
        actionQueue->PopAction();
    }
}

// FUNC_AT(0x000b8090)
void SMissionManager::InitPlayerCar() {
    SetShieldPointLoc(fgSimulation.cars.first[0], &playerShield);
}

// FUNC_AT(0x000b80b0)
void SMissionManager::SetDriveMissile(Missile *missile) {
    driveMissile = missile;
    steerY2 = 0.0f;
    steerX2 = 0.0f;
    steerY = 0.0f;
    steerX = 0.0f;
    if (missile != NULL) {
        FirstCamera()->SetMissileCamera(missile);
        if (missileHasTarget == 1)
            driveMissile->SetTarget(TargetPicker.GetSelectedTarget());
        ForceStopOn(fgSimulation.cars.first[0], kForceStopMissile);
    } else {
        if (autoDrive)
            Colorize->SetEnabled(RColorize::kModeOff);
        else
            FirstCamera()->EndMissileCamera();
        ForceStopOff(fgSimulation.cars.first[0], kForceStopMissile);
    }
}

// FUNC_AT(0x000b84b0)
void SMissionManager::Win() {
    missionState = kMissionWon;
    AIElementController_CalcMightAsWellBeDead(fgAIElementController, 0);
    kills += AIElementController_GetNumMightAsWellBeDead(fgAIElementController, 0);
    CalcEndMissionScores();
    if (missionEndTime == 0)
        missionEndTime = Ftol(double(fgSimulation.stepCount) * fgSimulation.timeStep);
    if (IsSkippingCinematic()) {
        ClearCinematicMode(-1);
        MissionCinematicMode = cinematicMode;
    }
    int next = fgWorld->attributes.LookupInt("CHAIN_NEXT_MISSION", NULL);
    const char *nextName = fgWorld->attributes.LookupString("CHAIN_NEXT_MISSION_NAME", NULL);
    if (next != 0 && nextName != NULL)
        Bond_SetExitToDriving(next, nextName, false);
}

// FUNC_AT(0x000b8580)
void SMissionManager::Lose() {
    missionState = kMissionLost;
    if (IsSkippingCinematic()) {
        ClearCinematicMode(-1);
        MissionCinematicMode = cinematicMode;
    }
}

// FUNC_AT(0x000b85d0)
void SMissionManager::CallStage(int stage) {
    RunRules(&stageRules[stage]);
}

// FUNC_AT(0x000b8610)
void SMissionManager::ProcessRules() {
    if (missionState != kMissionRunning)
        return;
    RunRules(&globalRules);
    RunRules(&stageRules[currentStage]);
    currentStage = nextStage;
}

// FUNC_AT(0x000b8680)
void SMissionManager::Update() {
    if (Launch.cheats & 1)
        fgScheduler->timeScale = 2.0f;
    if (IsSkippingCinematic() && missionState != kMissionRunning)
        ClearCinematicMode(-1);
    if (IsSkippingCinematic())
        fgScheduler->cinematicModeSkipping = 1;
    UpdateAutoDriveMissile();
    UpdateCharInfo();
    UpdateObjectiveSpecial();
    UpdateTimedActions();
    if (currentSection >= 0) {
        MissionSectionProfile &section = sections[currentSection];
        float rate = fgRenderHigh->framesPerSecond;
        if (rate < section.minFramesPerSecond)
            section.minFramesPerSecond = rate;
        if (rate > section.maxFramesPerSecond)
            section.maxFramesPerSecond = rate;
        section.frames++;
        section.totalFramesPerSecond += rate;
    }
    ProcessRules();
    if (Launch.restartStep > 0 && fgSimulation.stepCount > Launch.restartStep) {
        Launch.restartCount++;
        void *restart = Event::operator new(4);
        if (restart != NULL)
            ERestart_Construct(restart, 0);
    }
}

// =============================================================================================================
// Objectives
// =============================================================================================================

// FUNC_AT(0x000b6c30)
void SMissionManager::SetObjectiveComment(int slot, uint32_t comment) {
    objectiveComment[slot] = comment;
}

// FUNC_AT(0x000b6c50)
void SMissionManager::ClearObjective(int slot) {
    objectiveVisible[slot] = 0;
    objectiveType[slot] = kObjectiveNone;
}

// FUNC_AT(0x000b6c70)
const Coord4* SMissionManager::GetCurrObjective(int slot) {
    switch (objectiveType[slot]) {
    case kObjectiveMarker:
        return &objectiveMarker[slot]->position;
    case kObjectiveElement: {
        CARP::AIElement *element = Redirect(objectiveElement[slot]);
        AIElementController_SyncRuleInfo(fgAIElementController, 0, element);
        return &element->position;
    }
    default:
        return NULL;
    }
}

// FUNC_AT(0x000b6cc0)
uint32_t SMissionManager::GetObjectiveComment(int slot) {
    return objectiveComment[slot];
}

// FUNC_AT(0x000b6cd0)
double SMissionManager::GetObjectiveArrowHeight(int slot) {
    switch (objectiveType[slot]) {
    case kObjectiveMarker:
        return objectiveMarker[slot]->arrowHeight;
    case kObjectiveElement: {
        CARP::AIElement *element = Redirect(objectiveElement[slot]);
        if (element->type == 1 && element->object != NULL)
            return element->object->renderObject->Instance()->SizeY() * 0.25f;
        return 0.0;
    }
    default:
        return 0.0;
    }
}

// FUNC_AT(0x000b6d40)
bool SMissionManager::IsObjectiveVisibleToUser(int slot) {
    return objectiveVisible[slot];
}

// FUNC_AT(0x000b6d50)
bool SMissionManager::CheckObjectiveVisibleToUser(int slot) {
    if (slot != 0)
        return false;
    const Coord4 *objective = GetCurrObjective(0);
    if (objective == NULL)
        return false;
    Coord4 sight[2];            // the camera to the objective
    memcpy(&sight[0], FirstCamera()->matrix.mtx[3], sizeof(Coord4));
    sight[1].x = objective->x;
    sight[1].y = objective->y;
    sight[1].z = objective->z;
    sight[1].w = 1.0f;
    if (vec3distance(&sight[0], &sight[1]) <= 200.0f)
        return fgCollisionMgr->StepCheckHitWorld(sight, 16.0f) == 0;
    return false;
}

// FUNC_AT(0x000b7f60)
void SMissionManager::SetObjective(int slot, const ObjectiveMarker *marker) {
    if (marker == NULL) {
        objectiveVisible[slot] = 0;
        objectiveType[slot] = kObjectiveNone;
        return;
    }
    objectiveType[slot] = kObjectiveMarker;
    objectiveMarker[slot] = marker;
    objectiveVisible[slot] = 0;
    objectiveCheckStep = fgSimulation.stepCount;
}

// FUNC_AT(0x000b7fb0)
void SMissionManager::SetObjective(int slot, const ObjectiveKind2Target *target) {
    if (target == NULL) {
        objectiveVisible[slot] = 0;
        objectiveType[slot] = kObjectiveNone;
        return;
    }
    objectiveType[slot] = kObjectiveKind2;
    objectiveTarget[slot] = target;
    objectiveVisible[slot] = 0;
    objectiveCheckStep = fgSimulation.stepCount;
}

// FUNC_AT(0x000b8000)
void SMissionManager::SetObjective(int slot, CARP::AIElement *element) {
    if (element == NULL) {
        objectiveVisible[slot] = 0;
        objectiveType[slot] = kObjectiveNone;
        return;
    }
    objectiveType[slot] = kObjectiveElement;
    objectiveElement[slot] = element;
    ProtectObjectiveVehicle(element);
    objectiveVisible[slot] = 0;
    objectiveCheckStep = fgSimulation.stepCount;
}

// FUNC_AT(0x000b81a0)
void SMissionManager::UpdateObjectiveSpecial() {
    for (int slot = 0; slot < kObjectiveSlots; slot++) {
        if (objectiveType[slot] == kObjectiveElement)
            ProtectObjectiveVehicle(objectiveElement[slot]);
    }
    if (fgSimulation.stepCount >= objectiveCheckStep) {
        for (int slot = 0; slot < kObjectiveSlots; slot++)
            objectiveVisible[slot] = CheckObjectiveVisibleToUser(slot);
        objectiveCheckStep = fgSimulation.stepCount + kObjectiveCheckInterval;
    }

    // The objective counts as seen while it is within radar range or ahead of the car (its velocity, or its
    // heading when nearly still)
    const Coord4 *objective = GetCurrObjective(1);
    if (objective == NULL)
        objective = GetCurrObjective(0);
    if (objective == NULL) {
        objectiveSeenStep = fgSimulation.stepCount;
    } else {
        const Coord3 *position = fgSimulation.cars.first[0]->GetPosition();
        Coord4 car = { position->x, position->y, position->z, 0.0f };
        Coord4 target = { objective->x, objective->y, objective->z, 0.0f };
        float radius = GHud::TheApp() != NULL ? GHud_RadarRadiusInMeters(GHud::TheApp(), 0) : 16.0f;
        if (vec3distance(&car, &target) < radius) {
            objectiveSeenStep = fgSimulation.stepCount;
        } else {
            const Coord3 *heading;
            if (VU0_v3length(&fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->velocity) > 0.1f)
                heading = &fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->velocity;
            else
                heading = reinterpret_cast<const Coord3 *>(
                    fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->info->orientation.mtx[2]);
            Coord4 forward = { heading->x, heading->y, heading->z, 0.0f };
            Coord4 toTarget;
            VU0_v4sub(&target, &car, &toTarget);
            VU0_v4unitxyz(&forward, &forward);
            VU0_v4unitxyz(&toTarget, &toTarget);
            if (v3dotprod(&forward, &toTarget) > -0.7f)
                objectiveSeenStep = fgSimulation.stepCount;
        }
    }
    if (fgSimulation.stepCount - objectiveSeenStep > fgSimulation.stepsPerSecond * 5) {
        events[kEventObjectiveNotSeen] = 1;
        objectiveSeenStep = fgSimulation.stepCount;
    } else {
        events[kEventObjectiveNotSeen] = 0;
    }
}

// FUNC_AT(0x000b8460)
MissionObjective* SMissionManager::GetObjectiveByIndex(int index) {
    int i = 0;
    for (PointerListNode *node = objectives.Begin(); node != objectives.head; node = node->next) {
        if (i == index)
            return static_cast<MissionObjective *>(node->value);
        i++;
    }
    return NULL;
}

// FUNC_AT(0x000b87f0)
void SMissionManager::PassObjective(int id) {
    MissionObjective *objective = FindObjective(&objectives, id);
    if (objective != NULL) {
        objective->state = kObjectivePassed;
        glbMissionManager->scores[kScoreObjectives] += objective->score;
    }
}

// FUNC_AT(0x000b8840)
void SMissionManager::FailObjective(int id) {
    MissionObjective *objective = FindObjective(&objectives, id);
    if (objective != NULL)
        objective->state = kObjectiveFailed;
}

// FUNC_AT(0x000b8880)
void SMissionManager::SetCurrentObjective(int id) {
    MissionObjective *objective = FindObjective(&objectives, id);
    if (objective != NULL)
        objective->state = kObjectiveCurrent;
}

// FUNC_AT(0x000b88c0)
void SMissionManager::SetIncompleteObjective(int id) {
    MissionObjective *objective = FindObjective(&objectives, id);
    if (objective != NULL)
        objective->state = kObjectiveIncomplete;
}

// FUNC_AT(0x000b9320)
void SMissionManager::AddObjective(int id, int unknown04, int score, int unknown0c) {
    if (objectives.size >= kMaxObjectives)
        return;
    objectives.PushBack(NewObjective(id, unknown04, score, unknown0c));
    if (objectives.size == 1)
        static_cast<MissionObjective *>(objectives.Begin()->value)->state = kObjectiveCurrent;
}

// FUNC_AT(0x000b93c0)
void SMissionManager::InsertObjective(int beforeId, int id, int unknown04, int score, int unknown0c) {
    if (objectives.size >= kMaxObjectives)
        return;
    PointerListNode *before = objectives.Begin();
    while (before != objectives.head && static_cast<MissionObjective *>(before->value)->id != beforeId)
        before = before->next;
    if (before == objectives.head) {
        AddObjective(id, unknown04, score, unknown0c);
        return;
    }
    void *objective = NewObjective(id, unknown04, score, unknown0c);
    PointerListNode *node = objectives.BuyNode(before, before->prev, &objective);
    objectives.IncreaseSize(1);
    before->prev = node;
    node->prev->next = node;
    if (objectives.size == 1)
        static_cast<MissionObjective *>(objectives.Begin()->value)->state = kObjectiveCurrent;
}

// FUNC_AT(0x000b94a0)
void SMissionManager::AddMessage(int unknown00, int unknown04, int unknown08) {
    MissionMessage *message = static_cast<MissionMessage *>(OperatorNew(sizeof(MissionMessage)));
    if (message != NULL) {
        message->unknown00 = unknown00;
        message->unknown04 = unknown04;
        message->unknown08 = unknown08;
    }
    messages.PushBack(message);
}
