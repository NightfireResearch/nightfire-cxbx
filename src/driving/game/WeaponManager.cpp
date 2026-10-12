#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#pragma fp_contract(off)

#include "WeaponManager.h"

#include <math.h>
#include <string.h>

#include "AICharacter.h"
#include "AIElements.h"
#include "AIHelicopter.h"
#include "AIVehicleController.h"
#include "Explosion.h"                  // ExplosionType
#include "Helicopter.h"                 // PHelicopter
#include "Hud.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "Vehicle.h"                    // PVehicle, CarPhysics
#include "Missile.h"
#include "WeaponEffects.h"              // RScorchWorld
#include "../anim/AnimEngine.h"         // Handle
#include "../audio/Bank.h"              // ABank, fgBanks
#include "../audio/Sound.h"             // ABaseSound, AMenuSound, AOneShotSound
#include "../camera/CameraIniLoader.h"  // fgCameraTables
#include "../camera/PlayerCamState.h"   // RPlayerCamState
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../data/AttributeSet.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/StaticInit.h"       // WeaponDefinitions
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/RigidBodyResolve.h"    // CollisionImpact
#include "../platform/RealMath.h"       // the VU0 vector functions
#include "../platform/X87.h"            // Ftol, RoundToInt
#include "../render/Colorize.h"
#include "../render/RSceneObj.hpp"
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../world/Targeting.h"         // TargetPicker
#include "../world/WorldPos.h"
#include "../camera/CameraSpline.h"     // CrtAcosTimes
#include "../../helpers.h"
#include "../../common/xbeOverload.h"   // XbeVirtual

// ---------------------------------------------------------------------------------------------------------------
// SWeaponManager (0x000ba7f0-0x000bce10), ported from the listing. See WeaponManager.h.
// ---------------------------------------------------------------------------------------------------------------

namespace {


// The animation stimuli and system states the weapons use (names ours)
enum WeaponStimulus : int {
    kStimulusDeploy = 8,        // a selected weapon's system at rest
    kStimulusStow = 9,          // a deselected weapon's deployed system
    kStimulusFire = 10,
    kStimulusReload = 11,
};

enum WeaponSystemState : uint32_t {
    kSystemAtRest = 1,
    kSystemDeployed = 2,        // the systems of most weapons fire only in this state
};

constexpr int kAnimSystemIds = 0x4a;                // the system ids Update deploys and stows
constexpr int kMaxWeaponSystems = 8;
constexpr uint32_t kTurretSystem = 0x10;           // the turret's system: its frame is the turret's angle
constexpr uint8_t kTurretStimulus = 8;
constexpr uint32_t kSystem36 = 0x36;
constexpr int kStimulusMode = 2;                // ProcessStimuli's mode for every call here

constexpr uint32_t kLaunchCheat2 = 0x2;             // Launch.cheats: the motion blur at least 80

// The view the sounds play in (AMenuSound::Trigger's last argument)
enum {
    kViewNone = 0,
    kViewShared = 2,
};

constexpr int kWeaponBankSlot = 4;                  // fgBanks' slot of the weapon and explosion sounds
constexpr int kSecondaryFireEvent = 0x16;           // the mission's event for a secondary shot

// The action's id, 0 for an empty reference
int ActionId(const ActionRef &ref) {
    return ref.data != NULL ? ref.data->action : kNullAction;
}

}  // namespace

// ---- the weapon tables (only this file reads them)

// Each weapon's animation systems on the player car, in firing order, ended by 0
// XBE_GLOBAL(0x001c9050, 0x400)
const int32_t WeaponSystems[kWeaponCount][kMaxWeaponSystems] = {
    { 9 }, { 5, 6 }, { 7, 8, 52, 53 }, { 13 }, { 25 }, { 25 }, { 25 }, { 65 },
    { 0 }, { 23 }, { 12 }, { 14 }, { 0 }, { 22 }, { 36 }, { 54 },
    { 72 }, { 0 }, { 0 }, { 38 }, { 0 }, { 61, 62 }, { 63, 64 }, { 33 },
    { 34 }, { 35 }, { 37 }, { 46 }, { 45 }, { 70 }, { 71 }, { 47 },
};

// The weapons in the order the selection steps through them
// XBE_GLOBAL(0x001c9450, 0x80)
const int32_t WeaponSelectionOrder[kWeaponCount] = {
    kWeaponGun, kWeaponRocket, kWeaponMissile, kWeaponQBooster, kWeaponQPulse, kWeaponTimeMine, kWeaponProxMine,
    kWeaponRemMine, kWeaponFloatingMine, kWeaponDetonator, kWeaponSpikes, kWeaponQSlick, kWeaponQSmoke,
    kWeaponQWorm, kWeaponLaser, kWeaponRadar, kWeaponCannon, kWeaponQVision, kWeapon2Wheels, kWeaponGrenade,
    kWeaponQCharge, kWeaponTorpedo, kWeaponRemoteTorpedo, kWeaponP2K, kWeaponSnowmobileGun,
    kWeaponSnowmobileRocket, kWeaponSniperRifle, kWeaponUltralightGun, kWeaponUltralightRocket, kWeaponFodGun,
    kWeaponFodRocket, kWeaponCh6RemoteRocket,
};

// Each weapon's time between shots, in seconds
// XBE_GLOBAL(0x001c8fd0, 0x80)
const float WeaponRefireSeconds[kWeaponCount] = {
    1.0f / 6.0f, 0.25f, 2.0f / 3.0f, 4.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f,
    1.0f, 2.0f, 2.0f, 1.0f, 1.0f, 3.0f, 0.25f, 1.0f / 12.0f,
    1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f, 0.05f, 1.0f, 1.0f / 12.0f, 5.0f, 1.0f / 12.0f,
};
static_assert(std::bit_cast<uint32_t>(1.0f / 6.0f) == 0x3e2aaaab && std::bit_cast<uint32_t>(2.0f / 3.0f) == 0x3f2aaaab &&
              std::bit_cast<uint32_t>(1.0f / 12.0f) == 0x3daaaaab && std::bit_cast<uint32_t>(0.05f) == 0x3d4ccccd,
              "the refire times");

// ---- calls to originals not ported

// AOneShotSound's constructor at a place, without a velocity

static_assert(sizeof(ActionQueue) == 0x974, "an action queue is 0x974 bytes");

namespace {

RPlayerCamera *PlayerCamera() {
    return fgRenderHigh->views[0].camera;
}

PhysicsObject *PlayerObject() {
    return fgSimulation.GetPlayerObject();
}

RigidBody *PlayerCarBody() {
    return fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot);
}

bool PlayerHasTurret() {
    return PlayerObject()->attributes.LookupBool("HAS_TURRET", NULL);
}

// RVehicle::TriggerIlluminate, vtable slot 12 of the player car's render object
void TriggerIlluminate(RSceneObj *object, int index, int32_t tick, int fadeTicks) {
    typedef void (RSceneObj::*Method)(int index, int32_t tick, int fadeTicks);
    (object->*XbeVirtual<Method>(object, 12))(index, tick, fadeTicks);
}

// The player car's switch lights: 9 for a select or the special fire, 10 for the primary and secondary fire
void LightSwitch(int index) {
    TriggerIlluminate(fgSimulation.cars.first[0]->renderObject, index, fgSimulation.stepCount + 10, 30);
}

// PBondCar's virtual methods the weapons call
bool EnableRocketBoost(PVehicle *car) {
    return (car->*XbeVirtual<bool (PVehicle::*)()>(car, PVehicle::kEnableRocketBoost))();
}

void EnableTwoWheelStunt(PVehicle *car) {
    (car->*XbeVirtual<void (PVehicle::*)()>(car, PVehicle::kEnableTwoWheelStunt))();
}

void FireLaser(PVehicle *car) {
    (car->*XbeVirtual<void (PVehicle::*)()>(car, PVehicle::kFireLaser))();
}

// "SFX_<sound><suffix>" of the weapon's bank, played once
void PlayWeaponSound(int bank, const char *sound, const char *suffix, const char *mixName) {
    char patch[32] = "SFX_";
    strcat(patch, sound);
    strcat(patch, suffix);
    AMenuSound::Trigger(bank, patch, mixName, kViewNone);
}

void DryFire(const char *mixName) {
    AMenuSound::Trigger(kWeaponBankSlot, "SFX_DryFire", mixName, kViewNone);
}

// The mission's event (not for the remote torpedo), then the selection fired, through the one manager as the
// original does
void FireSelection() {
    SWeaponManager *manager = WeaponManager;
    if (manager->secondary != kWeaponRemoteTorpedo)
        glbMissionManager->ProgrammerDefinedEvent(kSecondaryFireEvent, "");
    manager->FireWeapon(manager->secondary);
}

// An explosion sound at the place, heard from 30 to 400
void PlayExplosionSound(const char *patch, const Coord3 *position) {
    ABank *bank = fgBanks[kWeaponBankSlot];
    void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    AOneShotSound *sound = NULL;
    if (memory != NULL)
        sound = static_cast<AOneShotSound *>(memory)->ConstructAt(bank->handle, bank->index.Lookup(patch), position, 1.0f,
                                                                  "Explosions", false);
    // As the original, written whether or not the allocation succeeded
    sound->minDistance = 30.0f;
    sound->maxDistance = 400.0f;
    sound->maxDistanceSq = 160000.0f;
    sound->fxLevel = 1.0f;
}

}  // namespace

// =============================================================================================================
// Construction, the weapon table
// =============================================================================================================

// FUNC_AT(0x000bb220)
SWeaponManager* SWeaponManager::Construct() {
    weapons = static_cast<WeaponRecord *>(OperatorNewArray(sizeof(WeaponRecord) * kWeaponCount));
    playerCar = NULL;
    ActionQueue *queue = static_cast<ActionQueue *>(UMemory::FastAlloc(sizeof(ActionQueue), "ActionQueue"));
    actionQueue = queue != NULL ? queue->Construct((char *)"WeaponManQ") : NULL;
    Reset();
    return this;
}

// FUNC_AT(0x000bad40)
void SWeaponManager::Destruct() {
    OperatorDelete(weapons);
    if (actionQueue != NULL) {
        actionQueue->Destruct();
        UMemory::FastFree(actionQueue, sizeof(ActionQueue));
    }
}

// FUNC_AT(0x000bad70)
void SWeaponManager::Reset() {
    for (int i = 0; i < kWeaponCount; i++)
        WeaponDefinitions[i].refireSteps = Ftol(double(fgSimulation.stepsPerSecond) * WeaponRefireSeconds[i]);
    memcpy(weapons, WeaponDefinitions, sizeof(WeaponRecord) * kWeaponCount);
    weapons[kWeaponGun].flags |= kWeaponLoaded;
    actionQueue->Flush();

    primary = -1;
    secondary = kWeaponGun;
    special = -1;
    otherSecondary = kWeaponGun;
    lastFired = -1;
    lastTarget = NULL;
    firingPrimary = false;
    firingSecondary = false;
    firingSpecial = false;
    handsFired = false;
    reloading = false;
    unknown29 = 0;
    unknown30 = 0;
    doubleShot = false;
    boostBlur = 0;
    unknown38 = 0;
    switchStep = 0;
    nuclearState = kNuclearNone;
    nuclearStep = 0;
    nuclearScale = 0.0f;
    clickCount = 0;
    turretAngle = 0.0f;
    specialTrigger = NULL;
    specialRange = 0.0f;
    if (Launch.flags & kLaunchFlag8)
        doubleShot = true;

    for (int weapon = 0; weapon < kWeaponCount; weapon++) {
        sortRank[weapon] = -1;
        for (int place = 0; place < kWeaponCount; place++) {
            if (WeaponSelectionOrder[place] == weapon) {
                sortRank[weapon] = place;
                break;
            }
        }
    }
}

// FUNC_AT(0x000ba990)
void SWeaponManager::SetPlayerCar(RSceneObj *car) {
    playerCar = car;
    glbMissionManager->InitPlayerCar();
    bool found;
    int missiles = PlayerObject()->attributes.LookupInt("MISSILES_FIRED_SIMULTANEOUSLY", &found);
    if (found) {
        weapons[kWeaponMissile].clipSize = missiles * 2;
        weapons[kWeaponMissile].loaded = missiles * 2;
    }
}

// FUNC_AT(0x000ba9f0)
void SWeaponManager::LoadWeapon(int weapon, int ammo, int clipSize, int damage) {
    WeaponRecord &record = weapons[weapon];
    record.flags |= kWeaponLoaded;
    if (clipSize > 0 && glbMissionManager->autoDrive)
        record.clipSize = clipSize;
    if (damage > 0)
        record.damage = damage;
    if (glbMissionManager->autoDrive)
        record.loaded = record.clipSize < ammo ? record.clipSize : ammo;
    record.ammo += ammo;
}

// FUNC_AT(0x000baa70)
void SWeaponManager::UnloadWeapon(int weapon) {
    weapons[weapon].flags &= ~kWeaponLoaded;
    weapons[weapon].ammo = 0;
    if (weapon == secondary)
        secondary = -1;
    if (weapon == special)
        special = -1;
}

// FUNC_AT(0x000bae50)
void SWeaponManager::SetSpecialWeapon(int weapon, const Coord3 *trigger, float range) {
    specialTrigger = trigger;
    specialRange = range;
    if (special != -1)
        weapons[special].ammo = 0;
    if (weapon == kWeaponMissile) {
        WeaponRecord &missile = weapons[kWeaponMissile];
        missile.flags |= kWeaponLoaded;
        if (glbMissionManager->autoDrive)
            missile.loaded = missile.clipSize < 200 ? missile.clipSize : 200;
        missile.ammo += 200;
        secondary = kWeaponMissile;
        special = -1;
        return;
    }
    special = weapon;
}

// FUNC_AT(0x000bad00)
void SWeaponManager::StartNuclearSequence(const Coord3 *position, float scale) {
    nuclearState = kNuclearCountdown;
    nuclearStep = fgSimulation.stepCount + fgSimulation.stepsPerSecond;
    nuclearPosition = *position;
    nuclearScale = scale;
}

// =============================================================================================================
// The selection
// =============================================================================================================

// FUNC_AT(0x000baab0)
void SWeaponManager::SetSecondary(int weapon) {
    secondary = weapon;
    if (fgRenderHigh != NULL)
        PlayerCamera()->UpdateCurrentArm();
}

// FUNC_AT(0x000ba7f0)
void SWeaponManager::SortWeaponList() {
    int count = 0;
    for (int weapon = 0; weapon < kWeaponCount; weapon++) {
        bool handsWeapon = weapon >= kFirstHandsWeapon;
        if (glbMissionManager->autoDrive ? !handsWeapon : handsWeapon)
            continue;
        const WeaponRecord &record = weapons[weapon];
        if ((weapon == secondary || ((record.flags & kWeaponLoaded) && record.ammo > 0)) && weapon != special)
            sorted[count++] = weapon;
    }
    sortedCount = count;

    for (int first = 0; first < count - 1; first++) {
        for (int i = first; i < count - 1; i++) {
            int a = sorted[i];
            if (sortRank[a] > sortRank[sorted[i + 1]]) {
                sorted[i] = sorted[i + 1];
                sorted[i + 1] = a;
            }
        }
    }
}

int SWeaponManager::FindSortedSecondary() {
    for (int i = 0; i < sortedCount; i++) {
        if (sorted[i] == secondary)
            return i;
    }
    return -1;
}

// FUNC_AT(0x000baad0)
int SWeaponManager::GetPreviousSecondaryType() {
    if (sortedCount == 0)
        SortWeaponList();
    uint32_t count = sortedCount;
    if (count == 0)
        return 0;
    int index = FindSortedSecondary();
    if (index == -1)
        index = 0;
    return sorted[(count - 1 + index) % count];
}

// FUNC_AT(0x000bab20)
int SWeaponManager::GetNextSecondaryType() {
    if (sortedCount == 0)
        SortWeaponList();
    uint32_t count = sortedCount;
    if (count == 0)
        return 0;
    int index = FindSortedSecondary();
    if (index == -1)
        index = 0;
    return sorted[(index + 1) % count];
}

void SWeaponManager::FinishReload() {
    if (reloading && glbMissionManager->autoDrive) {
        WeaponRecord &record = weapons[secondary];
        if (record.ammo > 0 && !(record.flags & kWeaponReloading)) {
            record.loaded = record.ammo < record.clipSize ? record.ammo : record.clipSize;
            reloading = false;
        }
    }
}

// FUNC_AT(0x000baee0)
void SWeaponManager::SelectSecondary(bool next) {
    FinishReload();
    int weapon = next ? GetNextSecondaryType() : GetPreviousSecondaryType();
    if (weapon != secondary) {
        secondary = weapon;
        switchStep = fgSimulation.stepsPerSecond / 3 + fgSimulation.stepCount;
    }
}

// =============================================================================================================
// Firing
// =============================================================================================================

// FUNC_AT(0x000bab80)
void SWeaponManager::DoWeaponAnim(int weapon, int stimulus, int index) {
    playerCar->animHandle->ProcessStimuli(WeaponSystems[weapon][index], stimulus, fgSimulation.stepCount, kStimulusMode);
}

// FUNC_AT(0x000babb0)
int SWeaponManager::GetWeaponFromSystem(int system) {
    for (int weapon = 0; weapon < kWeaponCount; weapon++) {
        for (int i = 0; i < kMaxWeaponSystems && WeaponSystems[weapon][i] != 0; i++) {
            if (WeaponSystems[weapon][i] == system)
                return weapon;
        }
    }
    return -1;
}

// FUNC_AT(0x000bb2b0)
void SWeaponManager::FireWeapon(int weapon) {
    int32_t step = fgSimulation.stepCount;
    WTargetable *target = TargetPicker.GetSelectedTarget();
    WeaponRecord &record = weapons[weapon];

    if (target != NULL && target->ownerType == kTargetPhysicsObject && target->physics != NULL &&
        target->physics->attributes.LookupBool("ONLY_TARGET_WITH_EMP", NULL) && weapon != kWeaponQPulse &&
        weapon != kWeaponGun) {
        DryFire("Weapons Fire");
        return;
    }
    if (record.ammo < 1) {
        handsFired = false;
        return;
    }
    RPlayerCamera *camera = PlayerCamera();
    if (glbMissionManager->superEasy && !camera->state->lockedOn && weapon == kWeaponSniperRifle) {
        DryFire("POV Weapons");
        return;
    }
    if (switchStep >= fgSimulation.stepCount)
        return;
    if (record.fireSteps[record.fireSystem] + record.refireSteps > step)
        return;

    if (glbMissionManager->autoDrive && fgHandObj != NULL) {
        if (!fgHandObj->AbleToFire())
            return;
        fgHandObj->Fire();
        handsFired = true;
        record.fireSteps[record.fireSystem] = step;
    } else {
        if (record.flags & kWeaponAnimating)
            return;
        switch (weapon) {
        case kWeaponGun:
        case kWeaponRocket:
        case kWeaponMissile:
        case kWeaponCannon:
        case kWeaponQSlick:
        case kWeaponTimeMine:
        case kWeaponProxMine:
        case kWeaponRemMine:
        case kWeaponFloatingMine:
        case kWeaponQCharge:
        case kWeaponQWorm:
        case kWeapon2Wheels:
            break;
        default:
            if (playerCar->animHandle->GetSystemState(WeaponSystems[weapon][record.fireSystem]) != kSystemDeployed)
                return;
        }

        if (weapon == kWeaponMissile) {
            if (doubleShot && record.ammo >= record.clipSize) {
                record.fireSystem = 0;
                record.fireSteps[0] = step;
                DoWeaponAnim(weapon, kStimulusFire, record.fireSystem++);
                if (record.clipSize == 4)
                    record.loaded--;
                record.fireSteps[record.fireSystem] = step;
                DoWeaponAnim(weapon, kStimulusFire, record.fireSystem++);
                record.loaded--;
            }
            record.fireSteps[record.fireSystem] = step;
            DoWeaponAnim(weapon, kStimulusFire, record.fireSystem++);
            if (record.clipSize == 4)
                record.loaded--;
        } else if (weapon == kWeaponTorpedo && doubleShot && record.ammo >= record.clipSize) {
            record.fireSystem = 0;
            record.fireSteps[0] = step;
            DoWeaponAnim(weapon, kStimulusFire, record.fireSystem++);
            record.loaded--;
        }
        record.fireSteps[record.fireSystem] = step;
        DoWeaponAnim(weapon, kStimulusFire, record.fireSystem++);
        record.loaded--;
        if (WeaponSystems[weapon][record.fireSystem] == 0)
            record.fireSystem = 0;
    }

    if (record.countsShots)
        glbMissionManager->IncShotsFired(1);
    record.shotsFired++;
    if (weapon == kWeaponGun || weapon == kWeaponSnowmobileGun || weapon == kWeaponUltralightGun ||
        weapon == kWeaponFodGun) {
        if (!PlayerHasTurret() && record.countsShots)
            glbMissionManager->IncShotsFired(1);
    }

    switch (weapon) {
    case kWeaponQBooster:
        AMenuSound::Trigger(kWeaponBankSlot, "SFX_boostFire", "Weapons Fire", kViewNone);
        if (EnableRocketBoost(fgSimulation.cars.first[0]))
            boostBlur = 120;
        break;
    case kWeaponDetonator:
        fgSimulation.DetonateRemoteMines();
        break;
    case kWeaponQSlick: {
        PlayerCarBody();
        WWorldPos place;
        place.Construct();
        // The original converts a local it never sets: the scorch is placed by uninitialised stack. Ours is zero.
        Coord3 unset = {0.0f, 0.0f, 0.0f};
        Coord4 widened;
        Coord4 point = *Float_COORD3toCOORD4(&widened, &unset);
        RScorchWorld::ScorchGround(&place, &point, 4.2f, 4);
        NullFunction();     // WWorldPos's destructor
        break;
    }
    case kWeaponQSmoke:
        AMenuSound::Trigger(kWeaponBankSlot, "SFX_smkscrnFire", "Weapons Fire", kViewNone);
        break;
    case kWeaponQWorm:
        AMenuSound::Trigger(0, "SFX_uploadcode", "Menu Sounds", kViewShared);
        glbMissionManager->IncMagicCounter(0x1f);
        break;
    case kWeaponLaser:
        FireLaser(fgSimulation.cars.first[0]);
        break;
    case kWeaponRadar:
        AMenuSound::Trigger(0, "SFX_downloadcode", "Menu Sounds", kViewShared);
        glbMissionManager->IncMagicCounter(0x20);
        break;
    case kWeapon2Wheels:
        AMenuSound::Trigger(kWeaponBankSlot, "SFX_twowheelfire", "Weapons Fire", kViewNone);
        EnableTwoWheelStunt(fgSimulation.cars.first[0]);
        break;
    case kWeaponQCharge:
        glbMissionManager->SetMagicCounter(0x22, 1);
        break;
    default:
        break;
    }

    bool unlimited = gPlayerCanNeverLose;
    if (Launch.flags & kLaunchFlag2)
        unlimited = true;
    int used = 0;
    if (weapon != kWeaponGun && !unlimited) {
        if (weapon == kWeaponUltralightRocket) {
            used = 2;
        } else if (weapon == kWeaponMissile) {
            bool found;
            used = PlayerObject()->attributes.LookupInt("MISSILES_FIRED_SIMULTANEOUSLY", &found);
            if (!found)
                used = 2;
            if (doubleShot && record.ammo >= used * 2)
                used *= 2;
        } else if (weapon == kWeaponRocket) {
            bool found;
            used = PlayerObject()->attributes.LookupInt("ROCKETS_FIRED_SIMULTANEOUSLY", &found);
            if (!found)
                used = 2;
        } else {
            used = 1;
            if (weapon == kWeaponTorpedo && doubleShot && record.ammo >= 2)
                used = 2;
        }
        record.ammo -= used;
        if (used > 1 && record.countsShots)
            glbMissionManager->IncShotsFired(used - 1);
    }
    if (!gPlayerCanNeverLose) {
        bool reload = true;
        if (glbMissionManager->autoDrive) {
            record.loaded -= used;
            if (record.loaded < 0)
                record.loaded = 0;
            reload = record.ammo > 0 && record.loaded < 1;
        }
        if (reload)
            record.flags |= kWeaponReloading;
    }
    lastFired = weapon;
    lastTarget = target;
}

// FUNC_AT(0x000babf0)
void SWeaponManager::UserReloading() {
    WeaponRecord &record = weapons[secondary];
    if (fgHandObj == NULL || !fgHandObj->AbleToFire())
        return;
    if (record.loaded < record.clipSize && record.ammo > 0 && record.loaded < record.ammo) {
        reloading = true;
        if (record.soundBank > -1)
            PlayWeaponSound(record.soundBank, record.sound, "Reload", "POV Weapons");
        if (fgHandObj != NULL)
            fgHandObj->Reload();
    }
}

// =============================================================================================================
// The turret and the nuclear blast
// =============================================================================================================

// FUNC_AT(0x000baf60)
void SWeaponManager::UpdateTurret() {
    static const float kTurnsPerRadian = 0.15915494f;
    static_assert(std::bit_cast<uint32_t>(0.15915494f) == 0x3e22f983, "1 / 2pi");
    const float kTurn = 0.0025f;    // a step's turn of the turret

    if (fgSimulation.simState == kSimPaused)
        return;
    if (!PlayerHasTurret())
        return;

    WTargetable *target = TargetPicker.GetSelectedTarget();
    float angle = 0.0f;
    if (target != NULL && (secondary == kWeaponGun || secondary == kWeaponCannon)) {
        int slot = playerCar->physics->rigidBodySlot;
        Coord4 direction;
        VU0_v4sub(target, &fgSimulation.GetRigidBody(slot)->position, &direction);
        VU0_v4unitxyz(&direction, &direction);
        const float *forward = fgSimulation.GetRigidBody(playerCar->physics->rigidBodySlot)->info->orientation.mtx[2];
        float cosine = v3dotprod(&direction, forward);
        if (cosine < -1.0f)
            cosine = -1.0f;
        if (1.0f < cosine)
            cosine = 1.0f;
        angle = float(CrtAcosTimes(cosine, &kTurnsPerRadian));
        forward = fgSimulation.GetRigidBody(playerCar->physics->rigidBodySlot)->info->orientation.mtx[2];
        Coord4 side;
        VU0_v4crossprodxyz(&direction, forward, &side);
        if (side.y < 0.0f)
            angle = 1.0f - angle;
    }

    if (!(fabs(double(angle) - turretAngle) > double(kTurn))) {
        turretAngle = angle;
    } else {
        // Turned a step towards the angle, the short way across zero
        bool down;
        if (angle > turretAngle && angle >= 0.75f)
            down = turretAngle <= 0.25f;
        else if (angle < turretAngle && angle <= 0.25f)
            down = !(turretAngle >= 0.75f);
        else
            down = !(angle > turretAngle);
        turretAngle = down ? turretAngle - kTurn : turretAngle + kTurn;
        if (turretAngle > 1.0f)
            turretAngle = turretAngle - 1.0f;
        else if (turretAngle < 0.0f)
            turretAngle = turretAngle + 1.0f;
    }

    int frame = RoundToInt(turretAngle * 360.0f);
    if (playerCar->animHandle != NULL)
        playerCar->animHandle->SetFrame(kTurretSystem, kTurretStimulus, frame);
    turretOnTarget = turretAngle == angle;
}

// FUNC_AT(0x000bb9b0)
void SWeaponManager::HandleNuclearSequence() {
    if (nuclearState == kNuclearCountdown) {
        if (fgSimulation.stepCount < nuclearStep) {
            float brightness = float(double(fgSimulation.stepCount - nuclearStep) * fgSimulation.timeStep);
            if (brightness > 1.0f)
                brightness = 1.0f;
            Colorize->SetEnabled(RColorize::kMode3);
            Colorize->blastStrength = brightness;
            PlayExplosionSound("SFX_SmallExp", &nuclearPosition);
            return;
        }

        const Coord3 up = {0.0f, 1.0f, 0.0f};
        fgSimulation.SpawnExplosion(kNuclearExplosion, -1, &up, &nuclearPosition, nuclearScale * 10000.0f, 0, NULL,
                                    DamageSourceSig);
        CollisionImpact impact;
        impact.Construct();
        RigidBody *body = fgSimulation.GetRigidBody(PlayerObject()->rigidBodySlot);

        // The player's car is pushed away from the blast, from a point a unit towards it at the ground's height
        Coord4 point = {body->position.x, body->position.y, body->position.z, 1.0f};
        Coord4 blast = {nuclearPosition.x, nuclearPosition.y, nuclearPosition.z, 1.0f};
        Coord4 towards, away;
        VU0_v4sub4(&blast, &point, &towards);
        VU0_v4unit(&towards, &towards);
        VU0_v4sub4(&point, &blast, &away);
        VU0_v4unit(&away, &away);
        VU0_v4add4(&point, &towards, &point);
        WWorldPos place;
        place.Construct();
        point.z = float(place.HeightAtPoint(&body->position, false));
        Coord4 push;
        VU0_v4scale4(&away, 1000.0f, &push);
        push.z = push.z + push.z;
        body->GenerateImpulse(&impact, &push, &point, 10000.0f, false, 0, 1.0f);

        RPlayerCamera *camera = PlayerCamera();
        camera->SetAnchor(fgSimulation.cars.first[0]);
        camera->TriggerFixedCamera(reinterpret_cast<Coord4 *>(camera->matrix.mtx[3]), 0, 0);
        nuclearState = kNuclearBlast;
        PlayExplosionSound("SFX_HugeExp", reinterpret_cast<const Coord3 *>(&point));
        NullFunction();     // WWorldPos's destructor
    } else if (nuclearState == kNuclearBlast) {
        float brightness = float(double(fgSimulation.stepCount - nuclearStep) * fgSimulation.timeStep * 2.0);
        if (brightness > 1.0f)
            brightness = 1.0f;
        Colorize->SetEnabled(RColorize::kMode3);
        Colorize->blastStrength = float(brightness * 4.0 + 1.0);
    }
}

// =============================================================================================================
// The update
// =============================================================================================================

// FUNC_AT(0x000bbdc0)
void SWeaponManager::Update() {
    SMissionManager *mission = glbMissionManager;
    bool missionOver = mission->missionState == kMissionWon || mission->missionState == kMissionLost;
    if (missionOver || mission->cinematicMode != 0) {
        firingPrimary = false;
        firingSecondary = false;
        firingSpecial = false;
        handsFired = false;
    }
    bool paused = missionOver || fgSimulation.simState == kSimPaused || glbMissionManager->cinematicMode != 0;
    if (GHud::TheApp() != NULL && !paused && GHud::TheApp()->paused)
        paused = true;

    // The animation systems to deploy and to stow, by id
    bool deploy[kAnimSystemIds] = {};
    bool stow[kAnimSystemIds] = {};

    // The selection swapped with the other mode's when the mode changes, or the first selectable
    if (glbMissionManager->autoDrive && secondary < kFirstHandsWeapon) {
        int weapon = otherSecondary;
        otherSecondary = secondary;
        secondary = weapon;
        if (!(weapon >= kFirstHandsWeapon && weapons[weapon].ammo >= 1 && (weapons[weapon].flags & kWeaponLoaded))) {
            for (int i = kFirstHandsWeapon; i < kWeaponCount; i++) {
                if (weapons[i].ammo > 0 && (weapons[i].flags & kWeaponLoaded)) {
                    secondary = i;
                    break;
                }
            }
        }
    }
    if (glbMissionManager->autoDrive) {
        WeaponRecord &record = weapons[secondary];
        if (record.ammo > 0 && reloading && fgHandObj != NULL && fgHandObj->AbleToFire() &&
            !(record.flags & kWeaponReloading)) {
            record.loaded = record.ammo < record.clipSize ? record.ammo : record.clipSize;
            reloading = false;
        }
    }
    if (!glbMissionManager->autoDrive && secondary >= kFirstHandsWeapon) {
        int weapon = otherSecondary;
        otherSecondary = secondary;
        secondary = weapon;
        if (!(weapon < kFirstHandsWeapon && weapons[weapon].ammo >= 1 && (weapons[weapon].flags & kWeaponLoaded))) {
            for (int i = 0; i < kFirstHandsWeapon; i++) {
                if (weapons[i].ammo > 0 && (weapons[i].flags & kWeaponLoaded)) {
                    secondary = i;
                    break;
                }
            }
        }
    }
    if (glbMissionManager->autoDrive && fgHandObj != NULL) {
        if (fgHandObj->GetWeapon() + kFirstHandsWeapon != secondary && switchStep < fgSimulation.stepCount) {
            fgHandObj->ChangeWeapon(secondary - kFirstHandsWeapon);
            WeaponRecord &record = weapons[secondary];
            if (!(record.flags & kWeaponDeployed))
                record.flags |= kWeaponDeploying;
        }
    }
    SortWeaponList();
    if (!glbMissionManager->autoDrive && secondary != kWeaponGun) {
        firingSecondary = false;
        handsFired = false;
    }

    // The actions
    while (!actionQueue->IsEmpty()) {
        ActionRef ref;
        actionQueue->GetAction(&ref);
        switch (ActionId(ref)) {
        case kActionFirePrimary:
            if (!paused) {
                firingPrimary = true;
                BondFirePressed = 1;
                LightSwitch(10);
            }
            break;
        case kActionFirePrimaryRelease:
            firingPrimary = false;
            BondFirePressed = 0;
            break;
        case kActionFireSecondary:
            if (!paused) {
                if (secondary != kWeaponGun && !firingSecondary && weapons[secondary].ammo <= 0)
                    DryFire("POV Weapons");
                bool pressed = !firingSecondary;
                firingSecondary = true;
                BondFirePressed = 1;
                LightSwitch(10);
                if (glbMissionManager->driveMissile == NULL)
                    FireSelection();
                else if (pressed)
                    glbMissionManager->driveMissile->detonate = 1;
            }
            break;
        case kActionFireSecondaryRelease:
            firingSecondary = false;
            handsFired = false;
            BondFirePressed = 0;
            break;
        case kActionToggleSecondary:
        case kActionToggleSecondaryUp:
            if (!paused && !firingSecondary && !PlayerCamera()->CameraAiming() &&
                glbMissionManager->driveMissile == NULL) {
                WeaponManager->SelectSecondary(true);
                BondSelectPressed = 1;
                LightSwitch(9);
            }
            break;
        case kActionToggleSecondaryDown:
            if (!paused && !firingSecondary && !PlayerCamera()->CameraAiming() &&
                glbMissionManager->driveMissile == NULL) {
                WeaponManager->SelectSecondary(false);
                BondSelectPressed = 1;
                LightSwitch(9);
            }
            break;
        case kActionFireGadget:
            if (!paused && special != -1 && glbMissionManager->driveMissile == NULL) {
                if (!firingSpecial && weapons[special].ammo <= 0 && fgSimulation.cars.first[0]->GetPhysics()->isSub == 0)
                    DryFire("POV Weapons");
                firingSpecial = true;
                BondSelectPressed = 1;
                LightSwitch(9);
            }
            break;
        case kActionFireGadgetRelease:
            firingSpecial = false;
            break;
        case kActionPovReload:
            if (!paused && !firingPrimary && !firingSecondary)
                UserReloading();
            break;
        default:
            break;
        }
        actionQueue->PopAction();
    }

    // An empty selection steps on, when the option is set
    if (Launch.autoSwitch != 0) {
        bool empty = weapons[secondary].ammo < 1 && weapons[secondary].loaded < 1;
        bool aiming = false;
        if (glbMissionManager->autoDrive) {
            if (fgHandObj == NULL)
                empty = false;
            aiming = PlayerCamera()->CameraAiming();
        }
        if (!aiming && empty) {
            FinishReload();
            int weapon = GetNextSecondaryType();
            if (weapon != secondary) {
                secondary = weapon;
                switchStep = fgSimulation.stepsPerSecond / 3 + fgSimulation.stepCount;
            }
        }
    }

    // The fire buttons held
    if (glbMissionManager->autoDrive) {
        if (firingSecondary && (secondary == kWeaponSnowmobileGun || secondary == kWeaponUltralightGun ||
                                secondary == kWeaponFodGun))
            FireSelection();
    } else if (firingPrimary) {
        WeaponManager->FireWeapon(WeaponManager->primary);
    }
    if (firingSecondary && secondary == kWeaponGun)
        FireSelection();
    if (firingSpecial && special != -1) {
        bool inRange = true;
        if (specialTrigger != NULL) {
            Coord3 trigger = *specialTrigger;
            Coord3 position = *PlayerObject()->GetPosition();
            float distanceSquared = VU0_v3distancesquare(&trigger, &position);
            inRange = !(distanceSquared > double(specialRange) * specialRange);
        }
        if (inRange) {
            SWeaponManager *manager = WeaponManager;
            WeaponRecord &record = manager->weapons[manager->special];
            record.flags |= kWeaponLoaded;
            if (glbMissionManager->autoDrive)
                record.loaded = record.clipSize < 1 ? record.clipSize : 1;
            record.ammo++;
            manager->FireWeapon(manager->special);
        }
    }

    // Reloads, and the systems each weapon's state calls for
    for (int weapon = 0; weapon < kWeaponCount; weapon++) {
        WeaponRecord &record = weapons[weapon];
        if (weapon == primary || weapon == secondary || weapon == special) {
            bool more = true;
            bool reloadSound = false;
            while ((record.flags & kWeaponReloading) && more) {
                if (record.fireSteps[record.reloadSystem] + record.refireSteps - 5 >= fgSimulation.stepCount) {
                    more = false;
                    continue;
                }
                if (glbMissionManager->autoDrive && fgHandObj != NULL) {
                    if (!fgHandObj->AbleToFire() || record.ammo <= 0 || record.loaded >= 1) {
                        more = false;
                        continue;
                    }
                } else {
                    more = record.ammo - record.loaded > 0;
                    if (!more)
                        continue;
                }

                if (glbMissionManager->autoDrive && fgHandObj != NULL) {
                    fgHandObj->Reload();
                } else {
                    playerCar->animHandle->ProcessStimuli(WeaponSystems[weapon][record.reloadSystem], kStimulusReload,
                                                          fgSimulation.stepCount, kStimulusMode);
                    // The missile's even systems load only in fours
                    if (weapon == kWeaponMissile && (record.reloadSystem & 1) == 0 && record.clipSize != 4) {
                        reloadSound = true;
                    } else {
                        record.loaded++;
                        if (weapon != kWeaponGun)
                            reloadSound = true;
                    }
                }
                reloading = true;
                record.reloadSystem++;
                if (WeaponSystems[weapon][record.reloadSystem] == 0)
                    record.reloadSystem = 0;
                if (record.reloadSystem == record.fireSystem)
                    record.flags &= ~kWeaponReloading;
            }
            if (reloadSound && record.soundBank > -1)
                PlayWeaponSound(record.soundBank, record.sound, "Reload",
                                glbMissionManager->autoDrive ? "POV Weapons" : "Weapons Reload");
        }

        for (const int32_t *system = WeaponSystems[weapon]; *system != 0; system++) {
            int id = *system;
            if (weapon == special || (record.ammo > 0 && (weapon == primary || weapon == secondary))) {
                deploy[id] = true;
                stow[id] = false;
            } else if (!deploy[id]) {
                stow[id] = true;
            }
        }
    }

    if (switchStep < fgSimulation.stepCount) {
        for (int id = 0; id < kAnimSystemIds; id++) {
            Handle *anim = playerCar->animHandle;
            if (anim->IsSystemPlaying(id))
                continue;
            uint32_t state = anim->GetSystemState(id);
            if (deploy[id] && state == kSystemAtRest) {
                anim->ProcessStimuli(id, kStimulusDeploy, fgSimulation.stepCount, kStimulusMode);
                WeaponRecord &record = weapons[GetWeaponFromSystem(id)];
                if (!(record.flags & kWeaponDeployed))
                    record.flags |= kWeaponDeploying;
            } else if (stow[id] && state == kSystemDeployed) {
                anim->ProcessStimuli(id, kStimulusStow, fgSimulation.stepCount, kStimulusMode);
            }
        }
    }

    for (int weapon = 0; weapon < kWeaponCount; weapon++) {
        WeaponRecord &record = weapons[weapon];
        if (record.flags & kWeaponDeploying) {
            if (record.soundBank > -1)
                PlayWeaponSound(record.soundBank, record.sound, "Init",
                                glbMissionManager->autoDrive ? "POV Weapons" : "Weapons Init");
            record.flags = (record.flags & ~kWeaponDeploying) | kWeaponDeployed;
        }
        if (weapon != primary && weapon != secondary && weapon != special)
            record.flags &= ~kWeaponDeployed;
    }

    for (int weapon = 0; weapon < kWeaponCount; weapon++) {
        WeaponRecord &record = weapons[weapon];
        if (playerCar->animHandle->IsSystemPlaying(WeaponSystems[record.id][record.fireSystem]))
            record.flags |= kWeaponAnimating;
        else
            record.flags &= ~kWeaponAnimating;
    }

    // The smoke screen, while its system plays after a shot, makes a zone behind the car
    const WeaponRecord &smoke = weapons[kWeaponQSmoke];
    if ((smoke.flags & kWeaponAnimating) && lastFired == kWeaponQSmoke &&
        smoke.fireSteps[smoke.fireSystem] + smoke.refireSteps - 5 > fgSimulation.stepCount) {
        RigidBody *body = PlayerCarBody();
        Coord4 behind;
        VU0_v4scaleadd(body->info->orientation.mtx[2], -body->info->halfExtents.z, &body->position, &behind);
        AIZoneController::Get()->RegisterZone(4, Xyz(&behind));
    }

    if (_stricmp(fgSimulation.cars.first[0]->GetCarType(), "rental") == 0)
        TargetPicker.DeactivateTargeting();
    Handle *anim = playerCar->animHandle;
    if (!anim->IsSystemPlaying(kSystem36))
        anim->GetSystemState(kSystem36);
    UpdateTurret();

    // The Q-Booster's motion blur
    if (boostBlur > 0)
        boostBlur = Ftol(double(boostBlur) * 0.99f);
    int blur = Ftol(double(boostBlur) * 1.5f);
    if (blur > 96)
        blur = 96;
    if ((Launch.cheats & kLaunchCheat2) && blur < 80)
        blur = 80;
    mission = glbMissionManager;
    if (mission->missionState == kMissionWon || mission->missionState == kMissionLost || fgSimulation.simState == kSimPaused ||
        mission->cinematicMode != 0)
        blur = 0;
    if (GHud::TheApp() != NULL && !GHud::TheApp()->LavaDamageEffectEnabled())
        Colorize->EnableMotionBlur(float(blur * double(0.0078125f)));

    HandleNuclearSequence();

    // A click, faster the nearer the nearest proximity-destruct helicopter
    bool found = false;
    float nearest = 10000.0f;
    for (int i = 0; i < fgAIElementController->helicopterSlots; i++) {
        AIHelicopter *helicopter = AIVehicleController::Get()->GetHelicopter(i);
        if (helicopter->active &&
            static_cast<PHelicopter *>(helicopter->GetPhysicsObject())->proximityDestructEnabled) {
            found = true;
            const Coord4 *at = helicopter->GetPosition();
            Coord4 helicopterPosition = {at->x, at->y, at->z, 0.0f};
            const Coord3 *player = PlayerObject()->GetPosition();
            Coord4 playerPosition = {player->x, player->y, player->z, 0.0f};
            Coord4 offset;
            VU0_v4sub(&helicopterPosition, &playerPosition, &offset);
            float distance = VU0_v3length(&offset);
            if (distance < nearest)
                nearest = distance;
        }
    }
    if (found && nearest < fgSimulation.stepsPerSecond) {
        if (++clickCount >= Ftol(nearest)) {
            clickCount = 0;
            if (!GHud::TheApp()->paused) {
                int mode = PlayerCamera()->cameraMode;
                if (mode >= fgCameraTables.modeCount ||
                    (fgCameraTables.modes[mode].type != kCameraAnimation &&
                     fgCameraTables.modes[mode].type != kCameraAIPathAnimation))
                    AMenuSound::Trigger(0, "SFX_distanceclick", "Menu Sounds", kViewShared);
            }
        }
    } else {
        clickCount = 0;
    }

    // Targeting: in auto-drive, and for the weapons that aim
    if (glbMissionManager->autoDrive) {
        TargetPicker.ActivateTargeting(kTargetingAutoDrive);
        return;
    }
    SWeaponManager *manager = WeaponManager;
    // With no special weapon this reads the word before the array, as the original does
    int specialId = manager->weapons[manager->special].id;
    int secondaryId = manager->weapons[manager->secondary].id;
    if (specialId != kWeaponQPulse && secondaryId != kWeaponMissile && secondaryId != kWeaponTorpedo &&
        !PlayerHasTurret()) {
        TargetPicker.DeactivateTargeting();
        return;
    }
    TargetPicker.ActivateTargeting(kTargetingNormal);
}
