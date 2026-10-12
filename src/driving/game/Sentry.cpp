#pragma fp_contract(off)

#include "Sentry.h"

#include <bit>
#include <math.h>
#include <string.h>

#include "CharacterSound.h"             // MakeCoord3
#include "Missile.h"
#include "MissionManager.h"
#include "Simulation.h"
#include "Vehicle.h"                    // fgDamageLevels
#include "WeaponEffects.h"              // RMuzzleFlash
#include "WeaponSound.h"                // ASentry
#include "../anim/AnimEngine.h"         // Handle
#include "../audio/Sound.h"             // ABaseSound
#include "../camera/CameraSpline.h"     // CrtAcosTimes
#include "../data/Carp.h"               // CARP::Instance
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RSceneObj.hpp"
#include "../world/Targeting.h"
#include "../world/World.h"             // fgWorld

// ---------------------------------------------------------------------------------------------------------------
// Sentry (0x00073150-0x00074660), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet


// ---- globals

// XBE_GLOBAL(0x001e9170, 0x100)
SentryMuzzleFlash SentryMuzzleFlashes[kSentryMuzzleFlashes];
// XBE_GLOBAL(0x001e9278, 4)
int32_t SentryMuzzleFlashCount;

namespace {

const Coord3 kZero = {0.0f, 0.0f, 0.0f};
alignas(16) const Coord4 kZAxis = {0.0f, 0.0f, 1.0f, 0.0f};
alignas(16) const MATRIX4 kIdentity = { {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
} };

// ApplyDamage's kinds (as PBondCar's)
enum SentryDamageKind : int32_t {
    kDamageBullet = 2,
    kDamageKind3 = 3,
};

// The stimuli sent to the animations (neutral names); every one queued (mode 2)
enum SentryStimulus : uint8_t {
    kStimulusHit02 = 0x02,
    kStimulusImpact04 = 0x04,       // a hit leaving it above DamageLevels::impact[2] of its hit points
    kStimulusImpact05 = 0x05,       // ... impact[1]
    kStimulusImpact06 = 0x06,       // ... impact[0]; also the kill's
    kStimulusKill07 = 0x07,         // the kill's, for kind 3
    kStimulusFireStart08 = 0x08,    // the fire system's, before the first shot; the aim system's frames' stimulus
    kStimulusFire0A = 0x0a,         // each shot
    kStimulusShell0B = 0x0b,        // each shot while shotsLeft lasts
    kStimulusStage13 = 0x13,        // the damage stages 1 to 4: 0x13 to 0x16
    kStimulusStage14 = 0x14,
    kStimulusStage15 = 0x15,
    kStimulusStage16 = 0x16,
    kStimulusImpact19 = 0x19,       // ... impact[3]
};

// An animation system's states (RAnimEngine::Handle::GetSystemState)
enum SentrySystemState : uint32_t {
    kSystemState2 = 2,              // the aim system: turns by its frame; the sentry becomes a target
    kSystemState4 = 4,              // neither rotation turns nor does it fire
};

constexpr int kStimulusMode = 2;
constexpr uint16_t kAllZones = 0xffff;
constexpr int kDamageStages = 4;
constexpr uint32_t kStartAnimation = 8;     // PlayAnimation's stimulus
constexpr int kDamageDone = 0x10;
constexpr float kScoredDamage = 5.0f;
constexpr float kDegreesToTurns = 1.0f / 360.0f;
constexpr float kRadiansToTurns = 0.159154937f;     // 1 / (2 pi)
constexpr float kDimensionSteps[2] = {0.25f, 16.0f};    // CARP::Instance::packedDimensions' units
constexpr float kBodyMass = 1000.0f;
constexpr float kHearingScale = 0.9f;       // the sound's least distance, of the range
constexpr float kFrameOffset = 60.0f;       // taken off an aim animation's frames if it has more
constexpr uint8_t kAimFrameRate = 1;
constexpr float kTurnTolerance = 0.001f;    // a turn smaller than it is no turn to the sound
constexpr float kSRBOnTarget = 0.01f;
constexpr float kAnimOnTarget = 0.01f;
constexpr float kFlatTolerance = 0.0001f;   // TrackTarget: a perpendicular component within it counts as none
constexpr float kAlmostOne = 0.999998987f;  // ComputeTargetAngle's cosine limits
constexpr float kFlashSize = 5.0f;
constexpr float kFlashFade = 0.33f;
constexpr float kFlashGone = 0.1f;
constexpr int kFlashType = 1;               // RMuzzleFlash::Draw's type
static_assert(std::bit_cast<uint32_t>(kDegreesToTurns) == 0x3b360b61 &&
              std::bit_cast<uint32_t>(kRadiansToTurns) == 0x3e22f983 &&
              std::bit_cast<uint32_t>(kHearingScale) == 0x3f666666 &&
              std::bit_cast<uint32_t>(kTurnTolerance) == 0x3a83126f &&
              std::bit_cast<uint32_t>(kSRBOnTarget) == 0x3c23d70a &&
              std::bit_cast<uint32_t>(kFlatTolerance) == 0x38d1b717 &&
              std::bit_cast<uint32_t>(kAlmostOne) == 0x3f7fffef && std::bit_cast<uint32_t>(kFlashFade) == 0x3ea8f5c3 &&
              std::bit_cast<uint32_t>(kFlashGone) == 0x3dcccccd, "the original's constants");

// RSceneObj::TriggerFX through the render object's vtable (slot 15)
void TriggerFX(RSceneObj *render, int type, uint32_t which) {
    typedef void (RSceneObj::*TriggerFXMethod)(int type, uint32_t which, uint32_t unused, const Coord3 *position,
                                               const Coord3 *direction, float intensity);
    (render->*XbeVirtual<TriggerFXMethod>(render, 15))(type, which, 0, &kZero, &kZero, -1.0f);
}

// A vector's component along an axis (Sentry::Axis)
float &Along(Coord4 &v, int axis) {
    return reinterpret_cast<float *>(&v)[axis];
}

// Whether `angle` moved at least the tolerance away from `previous`
bool Turned(float angle, float previous) {
    return double(angle) - kTurnTolerance >= previous || double(angle) + kTurnTolerance <= previous;
}

// The turning flag of a turn about `axis`: sideways about y, else up and down
void SetTurning(ASceneObj *sound, int axis, bool turning) {
    if (axis == Sentry::kAxisY)
        sound->turningSideways = turning;
    else
        sound->turningUpDown = turning;
}

void ReleaseTarget(WTargetable *target) {
    TargetPicker.UnregisterTarget(target);
    target->ownerType = kTargetPoint;
    target->RemoveReference();
}

} // namespace

// ---- construction

// FUNC_AT(0x00073a90)
Sentry* Sentry::Construct(CARP::Instance *instance, int srbAxis, float srbSpeed, float srbLimit, int animAxis,
                          float animSpeed, float animLimit, uint32_t aimSystem, uint32_t fireSystem,
                          uint32_t shots, float shotSeconds, float range, uint32_t targetSig, float hitPoints,
                          uint32_t holdSteps, uint8_t scoreable) {
    PhysicsObject::Construct("sentry", "Sentry", kObjectSentry, 0, false, false);
    this->srbAxis = srbAxis;
    vtable = reinterpret_cast<void **>(kSentryVtable);
    this->animAxis = animAxis;
    srbRate = float(double(fgSimulation.timeStep) * kDegreesToTurns * srbSpeed);
    this->srbLimit = srbLimit;
    srbAngle = 0.0f;
    animAngle = 0.0f;
    animRate = float(double(fgSimulation.timeStep) * kDegreesToTurns * animSpeed);
    this->animLimit = fabsf(animLimit);
    rangeSq = range * range;
    animReversed = animLimit < 0.0f;
    this->scoreable = scoreable;
    this->aimSystem = aimSystem;
    shotsLeft = shots;
    this->fireSystem = fireSystem;
    aimInstance = 0;
    this->holdSteps = holdSteps;
    holdCountdown = holdSteps;
    shotInterval = Ftol(ceil(double(fgSimulation.stepsPerSecond) * shotSeconds));
    nextShotStep = 0;
    this->targetSig = targetSig;
    targetable = NULL;
    this->hitPoints = hitPoints;
    inverseHitPoints = 1.0f / hitPoints;
    SetHitPointLoc(&this->hitPoints);
    RSceneObj *render = fgWorld->GetSceneObjFromInstance(instance);
    SetRenderObject(render);
    animFrames = -1.0f;
    if (this->scoreable)
        glbMissionManager->unknown500++;

    bool aims = rangeSq > 0.0f;
    float radius = renderObject->GetBoundingRadius() * 0.5f;
    MATRIX4 orientation = instance->Matrix();
    orientation.mtx[0][3] = 0.0f;
    orientation.mtx[1][3] = 0.0f;
    orientation.mtx[2][3] = 0.0f;
    orientation.mtx[3][3] = 1.0f;
    if (aims) {
        // The radius from the aim instance's size, and the two angles the instance's z axis starts at
        Handle *animation = render->animHandle;
        aimInstance = animation->GetBestSystemInstanceIndex(aimSystem);
        uint32_t packed = animation->Instances()[aimInstance].packedDimensions;
        radius = float(double(packed & 0x3ff) * kDimensionSteps[(packed >> 30) & 1] * 0.5f);
        Coord4 flat = {orientation.mtx[2][0], orientation.mtx[2][1], orientation.mtx[2][2], 0.0f};
        Along(flat, srbAxis) = 0.0f;
        VU0_v4unitxyz(&flat, &flat);
        float cosine = v3dotprod(&flat, &kZAxis);
        if (1.0f < cosine)
            cosine = 1.0f;
        if (cosine < -1.0f)
            cosine = -1.0f;
        srbAngle = float(CrtAcosTimes(cosine, &kRadiansToTurns));
        float along = v3dotprod(&flat, orientation.mtx[2]);
        double ratio = along / VU0_v3length(orientation.mtx[2]);
        cosine = float(ratio);
        if (ratio < -1.0)
            cosine = -1.0f;
        if (1.0f < cosine)
            cosine = 1.0f;
        animAngle = float(CrtAcosTimes(cosine, &kRadiansToTurns));
        VU0_MATRIX4Init(&orientation);
    } else {
        orientation.mtx[3][2] = 0.0f;
        orientation.mtx[3][1] = 0.0f;
        orientation.mtx[3][0] = 0.0f;
    }

    Coord3 position = {instance->position[0], instance->position[1], instance->position[2]};
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleSentry, &position, &kZero, &kZero, &orientation, radius, kBodyMass);
    fgSimulation.GetSimpleRigidBody(rigidBodySlot);      // (its answer not used)

    ASentry *sound = static_cast<ASentry *>(ABaseSound::OperatorNew(sizeof(ASentry), "ASentry"));
    SetAudioObject(sound != NULL ? sound->Construct() : NULL);
    audioObject->minDistance = range * kHearingScale;
    double reach = double(range) + range;
    audioObject->maxDistance = float(reach);
    audioObject->maxDistanceSq = float(reach * reach);
    audioObject->position = position;
    PlayAnimation(kStartAnimation);
    if (aims) {
        RotateSRB(srbAngle);
        RotateAnim(animAngle);
    }
    return this;
}

// FUNC_AT(0x00073150)
void Sentry::Destruct() {
    vtable = reinterpret_cast<void **>(kSentryVtable);
    SetRenderObject(NULL);
    if (targetable != NULL) {
        ReleaseTarget(targetable);
        targetable = NULL;
    }
    PhysicsObject::Destruct();
}

// FUNC_AT(0x000735d0)
Sentry* Sentry::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(Sentry));
    return this;
}

// ---- targets

// FUNC_AT(0x00073330)
WTargetable* Sentry::GetTarget() {
    if (targetSig == 0)
        return NULL;
    // The signature's owner index (bits 24-30) in the simple bodies' table (bit 31) or the rigid ones'
    uint32_t index = (targetSig >> 24) & 0x7f;
    PhysicsObject *owner = (targetSig >> 31) ? fgSimulation.simpleRigidBodyOwners[index] : fgSimulation.rigidBodyOwners[index];
    if (owner == NULL)
        return NULL;
    WTargetable *block = static_cast<WTargetable *>(UMemory::FastAlloc(sizeof(WTargetable), "WTargetable"));
    WTargetable *target = block != NULL ? block->Construct(owner) : NULL;
    target->isTemporary = 1;
    return target;
}

// FUNC_AT(0x000731e0)
double Sentry::ComputeTargetAngle(const Coord4 *axis, const Coord4 *direction, const Coord4 *side, int which) {
    Coord4 a = {axis->x, axis->y, axis->z, 0.0f};
    Coord4 b = {direction->x, direction->y, direction->z, 0.0f};
    // (another `which` leaves the flag the original's uninitialised stack byte)
    bool reversed = false;
    switch (which) {
    case kAxisX:
        a.x = 0.0f;
        b.x = 0.0f;
        reversed = side->x > 0.0f;
        break;
    case kAxisY:
        a.y = 0.0f;
        b.y = 0.0f;
        reversed = side->y > 0.0f;
        break;
    case kAxisZ:
        a.z = 0.0f;
        b.z = 0.0f;
        reversed = side->z > 0.0f;
        break;
    }
    VU0_v4unitxyz(&b, &b);
    VU0_v4unitxyz(&a, &a);
    float cosine = v3dotprod(&b, &a);
    if (cosine >= 1.0f)
        cosine = kAlmostOne;
    else if (cosine <= -1.0f)
        cosine = -kAlmostOne;
    double angle = CrtAcosTimes(cosine, &kRadiansToTurns);
    if (reversed)
        angle = 1.0 - angle;
    while (angle < 0.0)
        angle += 1.0;
    return angle;
}

// ---- turning

// FUNC_AT(0x00073600)
double Sentry::ComputeNewRotation(float current, float target, float rate, float limit) {
    // How far round each way, in turns
    float forward = target - current;
    if (forward < 0.0f)
        forward += 1.0f;
    double back = double(current) - target;
    if (back < 0.0)
        back += 1.0;

    if (forward < back) {
        if (!(forward < rate))
            forward = rate;
        double turned = double(forward) + current;
        while (turned > 1.0)
            turned -= 1.0;
        if (turned > limit && turned < 1.0 - limit)
            return limit;
        return turned;
    }
    if (!(back < rate))
        back = rate;
    double turned = double(current) - back;
    while (turned < 0.0)
        turned += 1.0;
    if (turned > limit) {
        float other = float(1.0 - limit);
        if (turned < other)
            return other;
    }
    return turned;
}

// FUNC_AT(0x00073730)
bool Sentry::RotateSRB(float target) {
    if (renderObject->animHandle->GetSystemState(aimSystem) == kSystemState4)
        return false;
    float previous = srbAngle;
    float angle = float(ComputeNewRotation(srbAngle, target, srbRate, srbLimit));
    srbAngle = angle;
    MATRIX4 rotation = {};      // for another axis the original keeps the matrix its stack holds
    switch (srbAxis) {
    case kAxisX:
        VU0_MATRIX4setxrot(&rotation, angle);
        break;
    case kAxisY:
        VU0_MATRIX4setyrot(&rotation, angle);
        break;
    case kAxisZ:
        VU0_MATRIX4setzrot(&rotation, angle);
        break;
    }
    SetTurning(static_cast<ASceneObj *>(audioObject), srbAxis, Turned(srbAngle, previous));
    fgSimulation.GetSimpleRigidBody(rigidBodySlot)->SetOrientMat(&rotation);
    return fabs(double(srbAngle) - target) < kSRBOnTarget;
}

// FUNC_AT(0x000738b0)
bool Sentry::RotateAnim(float target) {
    Handle *animation = renderObject->animHandle;
    if (animation->GetSystemState(aimSystem) == kSystemState4)
        return false;
    float previous = animAngle;
    if (!animation->IsSystemPlaying(aimSystem) && animation->GetSystemState(aimSystem) == kSystemState2) {
        if (animFrames < 0.0f) {
            animation->SetFrameRate(aimSystem, kStimulusFireStart08, kAimFrameRate);
            double frames = animation->NumFrames(aimSystem, kStimulusFireStart08);
            animFrames = float(frames);
            if (frames > kFrameOffset)
                animFrames = float(frames - kFrameOffset);
        }
        // The frame follows the angle, from the end unless reversed
        double angle = ComputeNewRotation(animAngle, target, animRate, animLimit);
        animAngle = float(angle);
        if (!animReversed)
            angle = 1.0 - angle;
        animation->SetFrame(aimSystem, kStimulusFireStart08, Ftol(angle * animFrames));
    }
    SetTurning(static_cast<ASceneObj *>(audioObject), animAxis, Turned(animAngle, previous));
    return fabs(double(animAngle) - target) < kAnimOnTarget;
}

// FUNC_AT(0x00074280)
bool Sentry::TrackTarget(bool *inRange) {
    *inRange = false;
    PhysicsObject *target = fgSimulation.FindPhysicsObjectSignature(targetSig);
    if (target == NULL) {
        targetSig = 0;
        return false;
    }
    MATRIX4 aim;
    renderObject->GetInstancePosition(aimInstance, &aim, true);
    const Coord3 *targetPosition;
    if (!(target->flags & PhysicsObject::kSimpleBody))
        targetPosition = &fgSimulation.GetRigidBody(target->rigidBodySlot)->position;
    else
        targetPosition = &fgSimulation.GetSimpleRigidBody(target->rigidBodySlot)->position;
    Coord4 offset;
    VU0_v4sub(targetPosition, aim.mtx[3], &offset);
    if (!(VU0_v3lengthsquare(&offset) <= rangeSq)) {
        targetSig = 0;
        return false;
    }
    *inRange = true;

    // The body's turn: about the axis perpendicular to the offset and z
    Coord4 perpendicular;
    VU0_v4crossprodxyz(&offset, &kZAxis, &perpendicular);
    float &component = Along(perpendicular, srbAxis);
    if (component <= kFlatTolerance && component >= -kFlatTolerance)
        component = 1.0f;
    VU0_v4unitxyz(&perpendicular, &perpendicular);
    bool srbOnTarget = RotateSRB(float(ComputeTargetAngle(&kZAxis, &offset, &perpendicular, srbAxis)));

    // The animation's: the offset turned into the plane of its axis and z
    Coord3 made;
    Coord3 turned;
    switch (animAxis) {
    case kAxisX: {
        float y = offset.y;
        offset.y = 0.0f;
        turned = *MakeCoord3(&made, 0.0f, y, VU0_v3length(&offset));
        offset.x = turned.x;
        offset.y = turned.y;
        offset.z = turned.z;
        break;
    }
    case kAxisY: {
        float x = offset.x;
        offset.x = 0.0f;
        turned = *MakeCoord3(&made, x, 0.0f, VU0_v3length(&offset));
        offset.x = turned.x;
        offset.y = turned.y;
        offset.z = turned.z;
        break;
    }
    case kAxisZ: {
        float z = offset.z;
        offset.z = 0.0f;
        turned = *MakeCoord3(&made, VU0_v3length(&offset), z, 0.0f);
        offset.x = turned.x;
        offset.y = turned.y;
        offset.z = turned.z;
        break;
    }
    }
    VU0_v4unitcrossprodxyz(&offset, &kZAxis, &perpendicular);
    bool animOnTarget = RotateAnim(float(ComputeTargetAngle(&kZAxis, &offset, &perpendicular, animAxis)));
    return animOnTarget & srbOnTarget;
}

// ---- firing

// FUNC_AT(0x000733f0)
void Sentry::Shoot() {
    Handle *animation = renderObject->animHandle;
    if (animation->GetSystemState(aimSystem) == kSystemState4 ||
        animation->GetSystemState(fireSystem) == kSystemState4)
        return;
    if (nextShotStep == 0)
        animation->ProcessStimuli(fireSystem, kStimulusFireStart08, fgSimulation.stepCount, kStimulusMode);
    animation->ProcessStimuli(fireSystem, kStimulusFire0A, fgSimulation.stepCount, kStimulusMode);
    if (shotsLeft > 0) {
        animation->ProcessStimuli(fireSystem, kStimulusShell0B, fgSimulation.stepCount, kStimulusMode);
        shotsLeft--;
    }
    nextShotStep = shotInterval + fgSimulation.stepCount;
}

// FUNC_AT(0x000734a0)
void Sentry::AddMuzzleFlash(const Coord4 *position, const Coord3 *direction) {
    if (SentryMuzzleFlashCount >= kSentryMuzzleFlashes)
        return;
    SentryMuzzleFlash &flash = SentryMuzzleFlashes[SentryMuzzleFlashCount++];
    flash.position = *position;
    flash.direction = *direction;
    flash.size = kFlashSize;
}

// FUNC_AT(0x00073500)
void Sentry::DrawMuzzleFlashes() {
    for (int i = 0; i < SentryMuzzleFlashCount; i++) {
        SentryMuzzleFlash &flash = SentryMuzzleFlashes[i];
        // (the original leaves the direction's w uninitialised)
        Coord4 direction = {flash.direction.x, flash.direction.y, flash.direction.z, 0.0f};
        RMuzzleFlash::Draw(&kIdentity, &flash.position, &direction, kFlashType, flash.size);
        // Tested before it is rounded
        double faded = double(flash.size) * kFlashFade;
        flash.size = float(faded);
        if (faded < kFlashGone) {
            // The last one takes its place (and is drawn next time)
            SentryMuzzleFlashCount--;
            if (i < SentryMuzzleFlashCount)
                flash = SentryMuzzleFlashes[SentryMuzzleFlashCount];
        }
    }
}

// ---- damage and the step

// FUNC_AT(0x00073f80)
int Sentry::ApplyDamage(const Coord3 *, const Coord3 *, float amount, float, int kind, const uint32_t *sourceSig) {
    if (hitPoints <= 0.0f)
        return kDamageDone;
    bool byPlayer = false;
    PhysicsObject *source = fgSimulation.FindPhysicsObjectSignature(*sourceSig);
    if (source != NULL && source->IsOwnedBy(fgSimulation.GetPlayerObject()))
        byPlayer = true;
    bool scoring = scoreable;
    if (!byPlayer && kind == kDamageBullet)
        scoring = false;
    else if (scoring && amount >= kScoredDamage)
        glbMissionManager->IncShotsHit(kind == kDamageBullet);

    RSceneObj *render = renderObject;
    uint32_t step = fgSimulation.stepCount;
    Handle *animation = render->animHandle;
    LoseHitPoints(amount);
    if (hitPoints <= 0.0f) {
        animation->ProcessStimuli(kStimulusHit02, step, kStimulusMode);
        animation->ProcessStimuli(kind == kDamageKind3 ? kStimulusKill07 : kStimulusImpact06, step,
                                  kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage16, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage15, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage14, kAllZones, step, kStimulusMode);
        animation->ProcessStimuliZones(kStimulusStage13, kAllZones, step, kStimulusMode);
        TriggerFX(render, RSceneObj::kFXDamage, kAllZones);
        if (scoring) {
            glbMissionManager->IncKills(1);
            // The missiles still homing on it count as hits
            if (targetable != NULL) {
                for (Missile **missile = fgSimulation.missiles.first; missile != fgSimulation.missiles.last; missile++) {
                    if ((*missile)->target == targetable && !(*missile)->exploded)
                        glbMissionManager->IncShotsHit(false);
                }
            }
        }
        fgSimulation.DeletePhysicsObjectThunk(this);
        return kDamageDone;
    }

    // What is left of the hit points (the first test on the product before it is rounded)
    double product = double(inverseHitPoints) * hitPoints;
    float left = float(product);
    uint8_t stimulus;
    if (product > fgDamageLevels.impact[0])
        stimulus = kStimulusImpact06;
    else if (left > fgDamageLevels.impact[1])
        stimulus = kStimulusImpact05;
    else if (left > fgDamageLevels.impact[2])
        stimulus = kStimulusImpact04;
    else if (left > fgDamageLevels.impact[3])
        stimulus = kStimulusImpact19;
    else
        stimulus = 0;
    if (stimulus != 0)
        animation->ProcessStimuliZones(stimulus, kAllZones, step, kStimulusMode);
    for (uint32_t stage = kDamageStages; stage > 0; stage--) {
        if (left >= fgDamageLevels.zone[stage]) {
            animation->ProcessStimuliZones(kStimulusStage13 + stage - 1, kAllZones, step, kStimulusMode);
            break;
        }
    }
    if (left > fgDamageLevels.zone[1])
        TriggerFX(render, RSceneObj::kFXDamage, kAllZones);
    if (left > fgDamageLevels.effects)
        TriggerFX(render, RSceneObj::kFXDamageEffects, kAllZones);
    return kDamageDone;
}

// FUNC_AT(0x000744d0)
void Sentry::Simulate() {
    bool locked = false;
    bool firing = false;
    bool inRange = false;
    if (hitPoints > 0.0f) {
        if (targetable == NULL && renderObject->animHandle->GetSystemState(aimSystem) == kSystemState2) {
            WTargetable *block =
                static_cast<WTargetable *>(UMemory::FastAlloc(sizeof(WTargetable), "WTargetable"));
            targetable = block != NULL ? block->Construct(this) : NULL;
            TargetPicker.RegisterTarget(targetable);
        }
        if (targetSig != 0) {
            if (TrackTarget(&inRange)) {
                locked = true;
                if (holdCountdown != 0) {
                    holdCountdown--;
                } else if (shotsLeft > 0) {
                    firing = true;
                    if (fgSimulation.stepCount >= nextShotStep)
                        Shoot();
                }
            } else if (!inRange && holdCountdown < holdSteps) {
                holdCountdown++;
            }
        } else {
            uint32_t sig;
            targetSig = *fgSimulation.GetPlayerObject()->GetSig(&sig);
        }
    } else if (targetable != NULL) {
        ReleaseTarget(targetable);
        targetable = NULL;
    }
    ASceneObj *sound = static_cast<ASceneObj *>(audioObject);
    sound->servo = locked;
    sound->firing = firing;
    PhysicsObject::Simulate();
}
