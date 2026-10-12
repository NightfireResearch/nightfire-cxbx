#pragma fp_contract(off)

#include "AIHands.h"

#include <string.h>
#include <xmmintrin.h>

#include "Human.h"
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "WeaponManager.h"              // SWeaponManager
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../anim/Actor.h"              // ActActor, ActActorDatabase
#include "../anim/Character.h"          // ActCharacter::LoadCharacter
#include "../anim/Controllers.h"        // AnimationController
#include "../anim/IK.h"                 // ActIKSolveInfo
#include "../audio/Sound.h"             // AMenuSound
#include "../camera/CameraIniLoader.h"  // fgCameraTables
#include "../camera/PlayerCamState.h"
#include "../camera/PlayerCamera.h"
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../render/RSceneObj.hpp"      // LookupFXHandle, LocateFX
#include "../world/Targeting.h"         // PointerListNode

// ---------------------------------------------------------------------------------------------------------------
// AICharacterBond (0x0001cbb0-0x0001d530, 0x00023950) and AICharacterHands (0x000227a0-0x00023670; BlendVectors,
// placed among the hands' methods, is RealMath's), ported from the listing. Random numbers: SimRandom,
// in the hands' UpdateRotPos (two for a shot's jitter while firing, one while the kick eases).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- Bond

constexpr int kFirstBond = 1;
constexpr int kLastBond = 3;
constexpr int kBondSides = 2;
constexpr int kBondBank = 0;
constexpr float kBondArmingScale = 1.45f;      // the arming animation's time scale
constexpr float kBondFiringScale = 1.5f;

// Bond's animations: the neutral one by side, then the arming and firing sequences' three steps each
enum BondAnimation {
    kBondArming1 = 2,
    kBondArming2 = 3,
    kBondArming3 = 4,
    kBondFiring1 = 5,
    kBondFiring2 = 6,
    kBondFiring3 = 7,
};
const int32_t kBondAnimations[8] = {8, 0, 3, 2, 1, 6, 5, 4};

// The two sides' descriptions, by the mission set's choice
const char *const kBondDescriptions[(kLastBond - kFirstBond + 1) * kBondSides] = {
    "2.c_bond.h_bond.t_bond.100", "4.a_hand.b_hand.90",
    "2.c_bondc.h_bond.t_bondc.100", "4.a_handc.b_handc.90",
    "2.c_bond.h_bond.t_bondt.100", "4.a_hand.b_hand.90",
};

// The shoulders' IK axes, and the wrists' pose weights
const Coord4 kBondShoulderAxes[2] = {
    {0.0f, -1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
};
const float kBondWristWeights[2] = {1.0f, 1.0f};

// ---- the hands

constexpr int kNoAnimation = -1;
constexpr float kHandsFovScale = 0.5f;
constexpr float kAimYawScale = 0.4f;           // GetAimMatrix4's
constexpr float kAimPitchScale = 0.08f;
constexpr float kAimZoomFrom = 4.9f;           // the aim animation's frames between which the camera zooms
constexpr float kAimZoomTo = 5.3f;
constexpr int kSniperRifle = 3;
constexpr float kBoltFrom = 13.9f;             // the sniper rifle's fire animation's frames for the bolt's sound
constexpr float kBoltTo = 14.1f;
constexpr int kBoltBank = 4;
constexpr float kSwayRate = 0.5f;              // per second, of the way to the car's offset
constexpr float kCameraRate = 7.0f;
constexpr float kMaxSway = 0.13333332538604736f;
constexpr float kSwayScale = 0.15f;
constexpr float kSwaySide = 0.2f;
constexpr double kJitterScale = 1.525878978725359e-08;     // 0.001 / 65536: a shot's jitter, either way
constexpr double kJitterOffset = 0.0005000000237487257;
constexpr float kKickReady = -0.0125f;         // a shot kicks again once eased back this far
constexpr float kKick = -0.025f;
constexpr double kKickRecovery = 7.629394360719743e-08;    // 0.005 / 65536 a step at most

// Each weapon's animations (none for weapon 0)
const int32_t kHandNeutralAnimations[kHandWeapons] = {-1, 1, 2, 4, 0, 1, 0, 1};
const int32_t kHandIdleAnimations[kHandWeapons] = {-1, 6, 7, 1, 6, 7, 5, 6};
const int32_t kHandUnarmAnimations[kHandWeapons] = {-1, -1, -1, 3, -1, -1, -1, -1};
const int32_t kHandReloadAnimations[kHandWeapons] = {-1, 8, 9, 5, 8, 9, 7, 8};
const int32_t kHandFireAnimations[kHandWeapons][2] = {
    {-1, -1}, {4, 4}, {5, 3}, {0, 0}, {4, 4}, {5, 3}, {3, 3}, {4, 2},
};
const int32_t kHandAimAnimations[kHandWeapons] = {-1, -1, -1, 7, -1, -1, -1, -1};
const int32_t kHandAimReleaseAnimations[kHandWeapons] = {-1, -1, -1, 6, -1, -1, -1, -1};
const int32_t kHandInitialAnimations[kHandWeapons] = {-1, 8, 9, 2, 8, 9, 7, 8};
const int32_t kHandBanks[kHandWeapons] = {0, 12, 12, 14, 16, 16, 15, 15};
const char *const kHandDescriptions[kHandWeapons] = {
    NULL, "5.a_ssm.b_ssm.110", "5.a_ssm.b_ssm.110", "6.a_snipe.b_snipe.70", "7.a_ul.b_ul.140", "7.a_ul.b_ul.140",
    "8.a_fod.b_fod.100", "8.a_fod.b_fod.100",
};
const char *const kHandWeaponNames[kHandWeapons] = {
    NULL, "ssm_mg", "ssm_gr", "sniper", "ul_mg", "ul_gr", "fod_mg", "fod_gr",
};
const char *const kHandPositionNames[kHandWeapons] = {
    NULL, "AIC_HSSNOW_POS", "AIC_HSSNOW_POS", "AIC_HSNIPE_POS", "AIC_HULTRA_POS", "AIC_HULTRA_POS", "AIC_HFOD_POS",
    "AIC_HFOD_POS",
};
const char *const kHandRotationNames[kHandWeapons] = {
    NULL, "AIC_HSSNOW_ROT", "AIC_HSSNOW_ROT", "AIC_HSNIPE_ROT", "AIC_HULTRA_ROT", "AIC_HULTRA_ROT", "AIC_HFOD_ROT",
    "AIC_HFOD_ROT",
};

ActActor *ActorOf(PointerListNode *handle) {
    return static_cast<ActActor *>(handle->value);
}

// The character's SetActor through its vtable (slot 2)
void SetActorOf(AICharacter *character, PointerListNode *handle) {
    typedef void (AICharacter::*Method)(AICharacterActorLink *actorLink);
    (character->*XbeVirtual<Method>(character, AICharacter::kSlotSetActor))(
        reinterpret_cast<AICharacterActorLink *>(handle));
}

RigidBody *PlayerBody() {
    return fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot);
}

RPlayerCamera *PlayerCamera() {
    return fgRenderHigh->views[0].camera;
}

bool HandWeaponLoaded(int weapon) {
    return (WeaponManager->weapons[kFirstHandWeapon + weapon].flags & kWeaponLoaded) != 0;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// AICharacterBond
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0001cbb0)
AICharacterBond* AICharacterBond::Construct(const MATRIX4 *transform, int side) {
    AICharacter::Construct();
    vtable = reinterpret_cast<void **>(kAICharacterBondVtable);
    this->transform = *transform;
    this->side = side;
    animBank = kBondBank;
    return this;
}

// FUNC_AT(0x0001cbf0)
void AICharacterBond::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterBondVtable);
    AICharacter::Destruct();
}

// FUNC_AT(0x0001cc00)
AICharacterBond* AICharacterBond::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterBond));
    return this;
}

// FUNC_AT(0x0001cc30)
void AICharacterBond::GetRotPos(AICharacterBond *, MATRIX4 *transform) {
    *transform = PlayerBody()->info->orientation;
    transform->mtx[3][0] = PlayerBody()->position.x;
    transform->mtx[3][1] = PlayerBody()->position.y;
    transform->mtx[3][2] = PlayerBody()->position.z;
    transform->mtx[3][3] = 1.0f;
}

// FUNC_AT(0x0001ccc0)
void AICharacterBond::GetRideRotPos(AICharacterBond *, MATRIX4 *transform, Coord4 *velocity) {
    *transform = PlayerBody()->info->orientation;
    transform->mtx[3][0] = PlayerBody()->position.x;
    transform->mtx[3][1] = PlayerBody()->position.y;
    transform->mtx[3][2] = PlayerBody()->position.z;
    transform->mtx[3][3] = 1.0f;
    const Coord3 &carVelocity = PlayerBody()->velocity;
    velocity->x = carVelocity.x;
    velocity->y = carVelocity.y;
    velocity->z = carVelocity.z;
}

// FUNC_AT(0x0001cd80)
void AICharacterBond::Init() {
    int bond = glbMissionManager->missionSet->bond;
    if (bond < kFirstBond || bond > kLastBond)
        return;
    ArticleEffect *left = fgSimulation.cars.first[0]->renderObject->LookupFXHandle("steerleft", 0);
    ArticleEffect *right = fgSimulation.cars.first[0]->renderObject->LookupFXHandle("steerright", 0);
    if (left == NULL || right == NULL)
        return;
    const AttributeVector *seat = fgSimulation.cars.first[0]->attributes.LookupVector("AIC_BOND_POS", NULL);

    const char *const *descriptions = &kBondDescriptions[(bond - kFirstBond) * kBondSides];
    for (int side = 0; side < kBondSides; side++) {
        ActCharacter::LoadCharacter(descriptions[side], NULL, NULL);
        if (_stricmp(fgSimulation.cars.first[0]->GetCarType(), "helicopter") == 0)
            continue;

        MATRIX4 transform;
        VU0_MATRIX4Init(&transform);
        transform.mtx[3][0] = seat->x;
        transform.mtx[3][1] = seat->y;
        transform.mtx[3][2] = seat->z;
        transform.mtx[3][3] = seat->w;
        void *block = UMemory::FastAlloc(sizeof(AICharacterBond), "AICharacter");
        AICharacterBond *character =
            block != NULL ? static_cast<AICharacterBond *>(block)->Construct(&transform, side) : NULL;
        Human *human = static_cast<Human *>(fgSimulation.SpawnHumanObject(
            0, reinterpret_cast<const Coord3 *>(transform.mtx[2]), reinterpret_cast<const Coord3 *>(transform.mtx[3]), 0));
        PointerListNode *handle;
        ActActorDatabase::GetNewActorHandle(&handle, reinterpret_cast<int>(character), descriptions[side], NULL,
                                            reinterpret_cast<ActActorTransformCallback>(&GetRideRotPos), &transform,
                                            false);
        character->steerLeft = left;
        character->steerRight = right;

        // The arms reach for the wheel
        ActActor *actor = ActorOf(handle);
        int bones[2];
        bones[0] = actor->GetBoneIndex("Root.LShoulder");
        bones[1] = ActorOf(handle)->GetBoneIndex("Root.RShoulder");
        bool transformTargets[2] = {false, false};
        ActorOf(handle)->CreateIKs(2, bones, kBondShoulderAxes, transformTargets);
        ActorOf(handle)->SetIKInfoArray(2, BondHandIK);
        bones[0] = ActorOf(handle)->GetBoneIndex("Root.LWrist");
        bones[1] = ActorOf(handle)->GetBoneIndex("Root.RWrist");
        ActorOf(handle)->CreateGlobalPoseOverrides(2, bones, false);

        SetActorOf(character, handle);
        if (human != NULL) {
            human->SetAICharacter(character);
            human->Deactivate();
        }
        ActorOf(handle)->SetNewAnimation(kBondBank, kBondAnimations[side]);
    }
}

// FUNC_AT(0x0001d090)
void AICharacterBond::DoNeutral() {
    state = kStateIdling;
    animation = kBondAnimations[side];
    actorLink->actor->SetNewAnimation(animBank, animation);
}

// FUNC_AT(0x0001d0c0)
void AICharacterBond::DoInitial() {
    if (actorLink->actor->IsAnimationDone())
        state = kStateNeutral;
}

// The sequence's steps, each once the last is done
// FUNC_AT(0x0001d0e0)
void AICharacterBond::DoArming() {
    switch (subState) {
    case kStateArming:
        if (actorLink->actor->IsAnimationDone()) {
            subState = kStateFiring;
            animation = kBondAnimations[kBondArming2];
            actorLink->actor->SetNewAnimation(animBank, kBondAnimations[kBondArming2]);
        }
        break;
    case kStateFiring:
        if (actorLink->actor->IsAnimationDone()) {
            subState = kStateUnarming;
            animation = kBondAnimations[kBondArming3];
            actorLink->actor->SetNewAnimation(animBank, kBondAnimations[kBondArming3]);
        }
        break;
    case kStateUnarming:
        if (actorLink->actor->IsAnimationDone()) {
            state = kStateNeutral;
            subState = kStateInitial;
            actorLink->actor->SetTimeScale(1.0f);
        }
        break;
    }
}

// The first step repeats while the fire button is held
// FUNC_AT(0x0001d1c0)
void AICharacterBond::DoFiring() {
    switch (subState) {
    case kStateArming:
        if (actorLink->actor->IsAnimationDone()) {
            if (!BondFirePressed)
                subState = kStateFiring;
            animation = kBondAnimations[kBondFiring2];
            actorLink->actor->SetNewAnimation(animBank, kBondAnimations[kBondFiring2]);
        }
        break;
    case kStateFiring:
        if (actorLink->actor->IsAnimationDone()) {
            subState = kStateUnarming;
            animation = kBondAnimations[kBondFiring3];
            actorLink->actor->SetNewAnimation(animBank, kBondAnimations[kBondFiring3]);
        }
        break;
    case kStateUnarming:
        if (actorLink->actor->IsAnimationDone()) {
            state = kStateNeutral;
            subState = kStateInitial;
            actorLink->actor->SetTimeScale(1.0f);
        }
        break;
    }
}

// FUNC_AT(0x0001d2a0)
void AICharacterBond::HandleInterrupts() {
    if (_stricmp(fgSimulation.cars.first[0]->GetCarType(), "helicopter") == 0) {
        actorLink->actor->visible = false;
        return;
    }

    // The hands on the wheel's places, in the car's frame: the IK targets, and the wrists' poses
    MATRIX4 left, right;
    fgSimulation.cars.first[0]->renderObject->LocateFX(steerLeft, &left, true);
    fgSimulation.cars.first[0]->renderObject->LocateFX(steerRight, &right, true);
    MATRIX4 toCar;
    GetRotPos(this, &toCar);
    OrthoInverse(&toCar);
    MATRIX4 wrists[2];
    VU0_MATRIX4_mult(&wrists[0], &left, &toCar);
    VU0_MATRIX4_mult(&wrists[1], &right, &toCar);
    for (int i = 0; i < 2; i++) {
        const float *place = wrists[i].mtx[3];
        BondHandIK[i].target = {place[0], place[1], place[2], place[3]};
    }
    actorLink->actor->SetIKInfoArray(2, BondHandIK);
    actorLink->actor->SetGlobalPoseOverrides(2, reinterpret_cast<const Transform *>(wrists), kBondWristWeights);

    // Shown only on the side the camera's view is not from, and never in auto-drive
    const CameraModeInfo &mode = fgCameraTables.modes[PlayerCamera()->cameraMode];
    actorLink->actor->visible = int8_t(mode.interiorView) == side;
    if (glbMissionManager->autoDrive)
        actorLink->actor->visible = false;

    if (side != 1)
        return;
    if (BondSelectPressed) {
        if (state == kStateArming)
            return;
        state = kStateArming;
        subState = kStateArming;
        animation = kBondAnimations[kBondArming1];
        actorLink->actor->SetNewAnimation(animBank, kBondAnimations[kBondArming1]);
        BondSelectPressed = 0;
        actorLink->actor->SetTimeScale(kBondArmingScale);
    } else if (BondFirePressed) {
        if (state == kStateFiring)
            return;
        state = kStateFiring;
        subState = kStateArming;
        animation = kBondAnimations[kBondFiring1];
        actorLink->actor->SetNewAnimation(animBank, kBondAnimations[kBondFiring1]);
        actorLink->actor->SetTimeScale(kBondFiringScale);
    }
}

// FUNC_AT(0x00023950)
void AICharacterBond::UpdateRotPos() {
    MATRIX4 ride;
    GetRotPos(this, &ride);
    actorLink->actor->GetActorLocalPosOri(&transform);
    VU0_MATRIX4_mult(&transform, &transform, &ride);
}

// ---------------------------------------------------------------------------------------------------------------
// AICharacterHands
// ---------------------------------------------------------------------------------------------------------------

void AICharacterHands::Construct(int weapon) {
    AICharacter::Construct();
    this->weapon = weapon;
    vtable = reinterpret_cast<void **>(kAICharacterHandsVtable);
    reloadPending = 0;
    hideOnSwitch = 1;
    nextWeapon = 0;
    spread = 0.0f;
    aimFraction = 0.0f;
    shown = 0;
    animBank = kHandBanks[weapon];
}

// FUNC_AT(0x00022820)
void AICharacterHands::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterHandsVtable);
    AICharacter::Destruct();
}

// FUNC_AT(0x000228b0)
AICharacterHands* AICharacterHands::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterHands));
    return this;
}

// FUNC_AT(0x00022930)
void AICharacterHands::Init() {
    memset(HandsStatics.fireSide, 0, sizeof(HandsStatics.fireSide));
    memset(HandsStatics.handles, 0, sizeof(HandsStatics.handles));
    int first = 0;
    HandsStatics.lockOn = 0;
    HandsStatics.aiming = 0;
    for (int weapon = kHandWeapons - 1; weapon > 0; weapon--) {
        if (HandWeaponLoaded(weapon)) {
            ActCharacter::LoadCharacter(kHandDescriptions[weapon], kHandWeaponNames[weapon], NULL);
            first = weapon;
        }
    }
    if (first == 0)
        return;

    void *block = UMemory::FastAlloc(sizeof(AICharacterHands), "AICharacter");
    AICharacterHands *hands = NULL;
    if (block != NULL) {
        hands = static_cast<AICharacterHands *>(block);
        hands->Construct(first);
    }
    uint32_t ownerSig;
    fgSimulation.cars.first[0]->GetSig(&ownerSig);
    const MATRIX4 &base = HandsStatics.baseTransform;
    Human *human = static_cast<Human *>(fgSimulation.SpawnHumanObject(0, reinterpret_cast<const Coord3 *>(base.mtx[2]),
                                               reinterpret_cast<const Coord3 *>(base.mtx[3]), ownerSig));
    human->SetOwnerObject(fgSimulation.GetPlayerObject());

    // An actor for each weapon, at its place in the car, hidden and drawn in the view's second pass
    for (int weapon = 1; weapon < kHandWeapons; weapon++) {
        if (!HandWeaponLoaded(weapon))
            continue;
        PVehicle *car = fgSimulation.cars.first[0];
        const AttributeVector *offset = car->attributes.LookupVector(kHandPositionNames[weapon], NULL);
        const AttributeVector *rotation = car->attributes.LookupVector(kHandRotationNames[weapon], NULL);
        MATRIX4 transform = HandsStatics.baseTransform;
        transform.mtx[3][0] = transform.mtx[3][0] + offset->x;
        transform.mtx[3][1] = transform.mtx[3][1] + offset->y;
        transform.mtx[3][2] = transform.mtx[3][2] + offset->z;
        PointerListNode *handle;
        ActActorDatabase::GetNewActorHandle(&handle, reinterpret_cast<int>(hands), kHandDescriptions[weapon],
                                            kHandWeaponNames[weapon], NULL,
                                            reinterpret_cast<ActActorTransformCallback>(&GetRotPos), &transform,
                                            false);
        PointerListNode *actorHandle = handle;
        HandsStatics.handles[weapon] = reinterpret_cast<AICharacterActorLink *>(actorHandle);
        ActorOf(actorHandle)->fovScale = kHandsFovScale;
        ActorOf(actorHandle)->SetNewAnimation(kHandBanks[weapon], kHandInitialAnimations[weapon]);
        ActorOf(actorHandle)->visible = false;
        ActorOf(actorHandle)->fovConversion = 1;
        ActorOf(actorHandle)->RotateActorX(rotation->x);
        ActorOf(actorHandle)->RotateActor(rotation->y);
    }

    if (hands != NULL) {
        SetActorOf(hands, reinterpret_cast<PointerListNode *>(HandsStatics.handles[first]));
        fgHandObj = hands;
        hands->state = kStateArming;
    }
    if (human != NULL) {
        human->SetAICharacter(hands);
        HandsStatics.human = human;
        human->Deactivate();
    }
}

// FUNC_AT(0x000227c0)
void AICharacterHands::Shutdown() {
    for (int weapon = 1; weapon < kHandWeapons; weapon++) {
        if (HandsStatics.handles[weapon] != NULL) {
            ActActorDatabase::KillActorByHandle(reinterpret_cast<PointerListNode *>(HandsStatics.handles[weapon]));
            HandsStatics.handles[weapon] = NULL;
        }
    }
    if (fgHandObj != NULL) {
        fgHandObj->actorLink = NULL;
        fgHandObj = NULL;
    }
    if (HandsStatics.human != NULL)
        HandsStatics.human = NULL;
}

// FUNC_AT(0x000227a0)
float AICharacterHands::GetAimFraction() {
    if (fgHandObj == NULL)
        return 0.0f;
    return fgHandObj->aimFraction;
}

// FUNC_AT(0x000228e0)
void AICharacterHands::GetRotPos(AICharacterHands *hands, MATRIX4 *transform, Coord4 *velocity) {
    *transform = hands->transform;
    const Coord3 &carVelocity = PlayerBody()->velocity;
    velocity->x = carVelocity.x;
    velocity->y = carVelocity.y;
    velocity->z = carVelocity.z;
}

// FUNC_AT(0x00022830)
bool AICharacterHands::AbleToFire() {
    return state == kStateIdling && !reloadPending;
}

// FUNC_AT(0x00022860)
int AICharacterHands::GetWeapon() {
    return weapon;
}

// FUNC_AT(0x00022c30)
void AICharacterHands::ShowHands() {
    actorLink->actor->visible = true;
    shown = 1;
}

// FUNC_AT(0x00022c50)
void AICharacterHands::HideHands() {
    if (state == kStateNeutral && subState == kStateUnarming) {
        state = kStateIdling;
        if (nextWeapon != 0)
            weapon = nextWeapon;
        nextWeapon = 0;
        if (hideOnSwitch)
            actorLink->actor->visible = false;
        actorLink = HandsStatics.handles[weapon];
    }
    actorLink->actor->visible = false;
    shown = 0;
}

void AICharacterHands::SwitchActor() {
    if (hideOnSwitch)
        actorLink->actor->visible = false;
    actorLink = HandsStatics.handles[weapon];
    if (shown)
        actorLink->actor->visible = true;
    animation = kHandNeutralAnimations[weapon];
    animBank = kHandBanks[weapon];
    actorLink->actor->SetNewAnimation(animBank, animation);
}

// FUNC_AT(0x00022cc0)
void AICharacterHands::ChangeWeapon(int weapon) {
    if (weapon <= 0 || weapon >= kHandWeapons)
        weapon = 1;
    if (state != kStateIdling) {
        nextWeapon = weapon;
        return;
    }
    // The old weapon's unarming first, if it has one (DoNeutral and DoUnarming then arm the new one)
    if (kHandUnarmAnimations[this->weapon] != kNoAnimation) {
        state = kStateNeutral;
        subState = kStateUnarming;
        animation = kHandUnarmAnimations[this->weapon];
        actorLink->actor->SetNewAnimation(animBank, animation);
        this->weapon = weapon;
        nextWeapon = 0;
        return;
    }
    state = kStateNeutral;
    subState = kStateArming;
    this->weapon = weapon;
    nextWeapon = 0;
    SwitchActor();
}

// FUNC_AT(0x00022dd0)
void AICharacterHands::Fire() {
    if (state != kStateIdling)
        return;
    state = kStateFiring;
    animation = kHandFireAnimations[weapon][HandsStatics.fireSide[weapon]];
    animController = actorLink->actor->SetNewAnimation(animBank, animation);
    HandsStatics.fireSide[weapon] ^= 1;
}

// FUNC_AT(0x00022e20)
void AICharacterHands::Reload() {
    if (state != kStateIdling) {
        reloadPending = 1;
        return;
    }
    state = kStateReloading;
    animation = kHandReloadAnimations[weapon];
    actorLink->actor->SetNewAnimation(animBank, animation);
    reloadPending = 0;
}

// FUNC_AT(0x00022e70)
void AICharacterHands::DoNeutral() {
    if (subState == kStateArming) {
        CallSlot(kSlotFirstState + kStateArming);
        return;
    }
    if (subState != kStateUnarming)
        return;
    CallSlot(kSlotFirstState + kStateUnarming);
    if (state == kStateNeutral)
        return;
    state = kStateNeutral;
    subState = kStateArming;
    SwitchActor();
}

// FUNC_AT(0x00022f10)
void AICharacterHands::DoIdling() {
    if (reloadPending) {
        Reload();
        return;
    }
    if (HandsStatics.aiming != HandsStatics.lockOn) {
        CallSlot(kSlotFirstState + kStateArmed);
        return;
    }
    if (actorLink->actor->IsAnimationDone()) {
        animation = kHandIdleAnimations[weapon];
        actorLink->actor->SetNewAnimation(animBank, animation);
    }
}

// FUNC_AT(0x00022f70)
void AICharacterHands::DoArming() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    state = kStateIdling;
    if (nextWeapon != 0)
        ChangeWeapon(nextWeapon);
    nextWeapon = 0;
}

// FUNC_AT(0x00022fb0)
void AICharacterHands::DoArmed() {
    int current = weapon;
    if (HandsStatics.aiming) {
        if (kHandAimReleaseAnimations[current] != kNoAnimation && state != kStateFiring && state != kStateReloading) {
            state = kStateAiming;
            animation = kHandAimReleaseAnimations[current];
            animController = actorLink->actor->SetNewAnimation(animBank, animation);
        }
        PlayerCamera()->state->AimRelease();
        if (shown)
            actorLink->actor->visible = true;
    } else if (kHandAimAnimations[current] != kNoAnimation) {
        // The camera zooms part way through the animation (DoAiming)
        state = kStateAiming;
        animation = kHandAimAnimations[current];
        animController = actorLink->actor->SetNewAnimation(animBank, animation);
    } else {
        PlayerCamera()->state->AimZoom();
    }
    HandsStatics.aiming ^= 1;
}

// FUNC_AT(0x000230a0)
void AICharacterHands::DoAiming() {
    if (actorLink->actor->IsAnimationDone())
        state = kStateIdling;
    float frame = animController->GetCurrentFrame();
    if (!HandsStatics.aiming) {
        float remaining = float(animController->GetRemainingTime());
        aimFraction = float(remaining / animController->GetTotalTime());
        return;
    }
    if (kAimZoomFrom < frame && frame < kAimZoomTo) {
        actorLink->actor->visible = false;
        PlayerCamera()->state->AimZoom();
    }
    float current = float(animController->GetCurrentTimeSeconds());
    aimFraction = float(current / animController->GetTotalTime());
}

// FUNC_AT(0x00023170)
void AICharacterHands::DoFiring() {
    float frame = animController->GetCurrentFrame();
    if ((HandsStatics.aiming || kHandAimAnimations[weapon] == kNoAnimation) &&
        HandsStatics.aiming != HandsStatics.lockOn) {
        CallSlot(kSlotFirstState + kStateArmed);
        return;
    }
    if (weapon == kSniperRifle && kBoltFrom < frame && frame < kBoltTo) {
        AMenuSound::Trigger(kBoltBank, "SFX_sniperbolt", "POV Weapons", 0);
        return;
    }
    if (actorLink->actor->IsAnimationDone()) {
        state = kStateIdling;
        if (reloadPending)
            Reload();
    }
}

// FUNC_AT(0x00023230)
void AICharacterHands::DoReloading() {
    if ((HandsStatics.aiming || kHandAimAnimations[weapon] == kNoAnimation) &&
        HandsStatics.aiming != HandsStatics.lockOn) {
        CallSlot(kSlotFirstState + kStateArmed);
        return;
    }
    if (actorLink->actor->IsAnimationDone())
        state = kStateIdling;
}

// FUNC_AT(0x00023280)
void AICharacterHands::DoUnarming() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    state = kStateInitial;
    if (nextWeapon != 0)
        weapon = nextWeapon;
    nextWeapon = 0;
}

// FUNC_AT(0x000232b0)
void AICharacterHands::UpdateRotPos() {
    RPlayerCamera *camera = PlayerCamera();
    (camera->*XbeVirtual<void (RPlayerCamera::*)()>(camera, 3))();     // UpdateCamera
    transform = *camera->GetAimMatrix4(kAimYawScale, kAimPitchScale);
    float *position = transform.mtx[3];
    const float *eye = camera->matrix.mtx[3];
    position[0] = eye[0];
    position[1] = eye[1];
    position[2] = eye[2];
    position[3] = eye[3];

    // In auto-drive, the guns sway with the car: the camera's offset from it eased in, the offset's change eased
    // again, its height kept within 0.133
    if ((weapon == 1 || weapon == 2) && glbMissionManager->autoDrive) {
        Coord4 offset = {};
        VU0_v4sub(position, &PlayerBody()->position, &offset);
        float rate = fgSimulation.timeStep * kSwayRate;
        BlendVectors(&HandsStatics.cameraOffset, &HandsStatics.cameraOffset, &offset, rate);
        VU0_v4sub(&offset, &HandsStatics.cameraOffset, &offset);
        float cameraRate = fgSimulation.timeStep * kCameraRate;
        BlendVectors(&HandsStatics.swayOffset, &HandsStatics.swayOffset, &offset, cameraRate);
        if (HandsStatics.swayOffset.y > kMaxSway)
            HandsStatics.swayOffset.y = kMaxSway;
        else if (HandsStatics.swayOffset.y < -kMaxSway)
            HandsStatics.swayOffset.y = -kMaxSway;
        VU0_v4scale(&HandsStatics.swayOffset, kSwayScale, &offset);
        float sideways = v3dotprod(PlayerBody()->info->orientation.mtx[0], &offset) * kSwaySide;
        float carSpeed = VU0_v3lengthxz(&PlayerBody()->velocity);
        if (1.0f < carSpeed)
            carSpeed = 1.0f;
        offset.y = carSpeed * offset.y;
        position[0] = float(double(sideways) * camera->matrix.mtx[0][0] + position[0]);
        position[1] = position[1] - offset.y;
        position[2] = float(double(sideways) * camera->matrix.mtx[0][2] + position[2]);
    }

    // The guns kick: each shot jitters the aim's position and pushes it back along the camera's facing, then it
    // eases back
    if (weapon == 1 || weapon == 4 || weapon == 6) {
        const float *facing = camera->matrix.mtx[2];
        Coord4 back = {facing[0], facing[1], facing[2], 0.0f};
        Coord4 at = {position[0], position[1], position[2], 0.0f};
        if (state == kStateFiring) {
            at.x = float(double(int32_t(fgSimulation.random->Generate())) * kJitterScale - kJitterOffset + at.x);
            at.z = float(double(int32_t(fgSimulation.random->Generate())) * kJitterScale - kJitterOffset + at.z);
            if (spread >= kKickReady)
                spread = kKick;
        }
        if (spread < 0.0f) {
            VU0_v4scaleadd(&back, spread, &at, &at);
            double eased = double(int32_t(fgSimulation.random->Generate())) * kKickRecovery + spread;
            if (eased > 0.0)
                eased = 0.0;
            spread = float(eased);
        }
        position[0] = at.x;
        position[1] = at.y;
        position[2] = at.z;
    }
}
