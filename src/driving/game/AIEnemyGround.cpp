#pragma fp_contract(off)

#include "AIEnemyGround.h"

#include <bit>
#include <math.h>

#include "CharacterSound.h"             // ACharacter
#include "Human.h"
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "../anim/Actor.h"              // ActActor
#include "../anim/Poser.h"              // ActPoser
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../engine/PhysicsUtil.h"      // Util_CollideRayWithStretchedSphere
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../world/Collider.h"
#include "../world/Targeting.h"         // WTargetable, TargetPicker
#include "../world/WorldPos.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyGround (0x0001da60-0x0001fab3), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

namespace {

// The ground enemies' injury zones (AICharacterHitShape)
AICharacterHitShape GroundHitShape = {4, 15, kEnemyZoneMap, 0.35f, -0.15f, 3.0f};

// The animations, by weapon (1 or 2; row 0 is -1s)
const int32_t kArmAnimations[3] = {-1, 19, 15};
const int32_t kUnarmAnimations[3] = {-1, 18, 14};
const int32_t kReloadAnimations[3] = {-1, 20, -1};
// Aiming: [weapon][1][1] in general, [weapon][0][1] with the target on the side 0 at bearing 25, 45 or 90 (only
// those two entries are read)
const int32_t kAimAnimations[3][2][3] = {
    {{-1, -1, -1}, {-1, -1, -1}},
    {{-1, 26, -1}, {-1, 27, -1}},
    {{-1, 17, -1}, {-1, 18, -1}},
};
const int32_t kFireAnimations[3][6] = {{-1, -1, -1, -1, -1, -1}, {4, 3, 5, 6, 3, 7}, {1, 1, 1, 1, 1, 1}};
const int32_t kFireAboveAnimations[3][2] = {{-1, -1}, {8, 9}, {2, 3}};
const int32_t kRunAnimations[3] = {-1, 21, 16};
// Dodges: [SimRandom][side], and the high ones
const int32_t kDodgeAnimations[2][2] = {{24, 25}, {22, 23}};
const int32_t kHighDodgeAnimations[2] = {1, 2};
const int32_t kWanderAnimations[3] = {0, 1, 2};
constexpr int32_t kWanderAimAnimation = 3;
constexpr int32_t kThrownAnimation = 12;            // bank 2
// Hits: the claustrophobic's by weapon, the others' by weapon and zone (each list ended by a 0 or by its fourth)
const int32_t kClaustrophobicHitAnimations[3][3] = {{-1, -1, -1}, {14, 15, 16}, {10, 11, 12}};
const int32_t kHitAnimations[3][8][4] = {
    {{-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}},
    {{10}, {11}, {17}, {14, 15, 16}, {11}, {10}, {13}, {12}},
    {{4}, {5, 6}, {13}, {10, 11, 12}, {5}, {4}, {9}, {7, 8}},
};
// Deaths (bank 2): a shot's screams, the claustrophobic's, the others' by weapon and zone, and zone 1's
const int32_t kScreamAnimations[7] = {5, 6, 7, 8, 9, 10, 11};
const int32_t kClaustrophobicDeathAnimations[4] = {15, 17, 18, 19};
const int32_t kDeathAnimations[3][8][4] = {
    {{-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}},
    {{2}, {13, 14}, {19, 4}, {17, 18}, {0}, {1, 2, 3}, {16}, {15}},
    {{2}, {13, 14}, {19, 4}, {17, 18}, {0}, {1, 2, 3}, {16}, {15}},
};
const int32_t kHeadDeathAnimations[4] = {0, 17, 18, 19};

constexpr int kKindShot = 3;                        // PlayZoneInjuryAnim's kind 0 counts as this
constexpr int kZoneHead = 1;
constexpr float kRandomScale = 1.0f / 65536.0f;
constexpr float kDodgeChance = 0.01f;
constexpr float kAimCone = 0.707f;                  // the player's aim within 45 degrees
static_assert(std::bit_cast<uint32_t>(kAimCone) == 0x3f34fdf4, "the original's constant");
constexpr float kAimRadiusScale = 1.25f;
constexpr float kHighDodgeLift = 0.5f;
constexpr float kThreatMargin = 6.0f;               // past the car's half width
constexpr float kThreatReachScale = 4.0f;           // times the car's half length
constexpr float kThreatNear = 10.0f;
constexpr float kThreatFar = 80.0f;
constexpr float kThreatSlow = 10.0f;
constexpr float kThreatFast = 40.0f;
constexpr float kThreatScale = 0.1f;
constexpr float kThreatScaleMax = 1.5f;
// The bearings' bucket edges, in turns: ThreatenedByVehicle's (22.5, 67.5 and 135 degrees) and TargetIsAcquired's
// (12.5, 37.5 and 62.5)
constexpr float kThreat0 = 0.0625f;
constexpr float kThreat45 = 0.1875f;
constexpr float kThreat90 = 0.375f;
constexpr float kAcquired0 = 5.0f / 144.0f;
constexpr float kAcquired25 = 15.0f / 144.0f;
constexpr float kAcquired45 = 25.0f / 144.0f;
static_assert(std::bit_cast<uint32_t>(kAcquired0) == 0x3d0e38e4 && std::bit_cast<uint32_t>(kAcquired25) == 0x3dd55555 &&
              std::bit_cast<uint32_t>(kAcquired45) == 0x3e31c71c, "the original's constants");
constexpr float kTargetAbove = 0.5f;                // the way to the target's height, normalised
constexpr float kWanderAimFrame = 14.5f;            // from this frame the wandering turns from its weapon
constexpr float kAimTurnRate = 10.0f;
constexpr float kFireTurnRate = 0.5f;
constexpr float kRunDistance = 3.0f;
constexpr float kColliderRadius = 4.5f;
constexpr float kColliderRefresh = 4.0f;            // the collider is rebuilt when it leaves this
constexpr float kColliderRebuilt = 5.0f;
constexpr float kFallHeight = 2.0f;
constexpr float kFallFadeScale = 0.25f;             // seconds of fade per unit fallen, at least one
constexpr float kBlendTime = 0.25f;
constexpr int kDeathFadeSeconds = 4;

RigidBody *BodyOf(PhysicsObject *object) {
    return fgSimulation.GetRigidBody(object->rigidBodySlot);
}

// The leading entries of a list that are not 0 (at most four)
int CountAnimations(const int32_t *list) {
    int count = 0;
    for (int i = 0; i < 4 && list[i] != 0; i++)
        count++;
    return count;
}

} // namespace

// The last of each list PlayZoneInjuryAnim picked, which it does not pick twice running
// XBE_GLOBAL(0x001b54c8, 4)
int32_t EnemyGroundLastHit = -1;
// XBE_GLOBAL(0x001b57f0, 4)
int32_t EnemyGroundLastDeath = -1;
// XBE_GLOBAL(0x001b57f4, 4)
int32_t EnemyGroundLastScream = -1;

// FUNC_AT(0x0001da60)
void AICharacterEnemyGround::DoDodging() {
    if (subState == kStateRunning)
        CallSlot(kSlotFirstState + kStateRunning);
}

// FUNC_AT(0x0001da70)
void AICharacterEnemyGround::DoTilting() {
    MoveToPoint(reinterpret_cast<const Coord3 *>(thrownTransform.mtx[3]), true);
}

// FUNC_AT(0x0001da80)
bool AICharacterEnemyGround::AvoidThreat(uint8_t *side, uint8_t *high) {
    if (!glbMissionManager->autoDrive || !avoiding || weapon != kEnemyWeapon1)
        return false;

    if (avoided == 0 && !(fgSimulation.random->Generate() * double(kRandomScale) < kDodgeChance)) {
        // Only when the player's aim is on it: within the cone, and the ray through its stretched sphere
        RPlayerCamera *camera = fgRenderHigh->views[0].camera;
        const Coord4 *aim = camera->GetForwardAimVec4(1.0f);
        Coord4 forward;
        forward.x = aim->x;
        forward.y = aim->y;
        forward.z = aim->z;
        forward.w = 0.0f;
        const float *eye = camera->matrix.mtx[3];
        Coord4 toCharacter;
        VU0_v4sub(transform.mtx[3], eye, &toCharacter);
        VU0_v4unitxyz(&toCharacter, &toCharacter);
        if (v3dotprod(&forward, &toCharacter) < kAimCone)
            return false;

        const AICharacterHitShape *shape = hitShape;
        Coord4 centre;
        centre.x = transform.mtx[3][0];
        centre.y = transform.mtx[3][1] + shape->lift;
        centre.z = transform.mtx[3][2];
        centre.w = 0.0f;
        Coord3 hit;
        if (!Util_CollideRayWithStretchedSphere(reinterpret_cast<const Coord4 *>(eye), &forward, &centre,
                                                shape->radius * kAimRadiusScale, shape->stretch, &hit))
            return false;
        *side = uint8_t(int(fgSimulation.random->Generate()) * 2 >> 16);
        *high = hit.y - double(centre.y) > kHighDodgeLift;
        return true;
    }

    *side = uint8_t(int(fgSimulation.random->Generate()) * 2 >> 16);
    *high = fgSimulation.random->Generate() * double(kRandomScale) < 0.5;
    return true;
}

// FUNC_AT(0x0001dc50)
double AICharacterEnemyGround::GetZoneHitPointScale(int zone, float) {
    switch (zone) {
    case 0:
        return 0.0;
    case 1:
        return 1000.0;
    default:
        return 1.0;
    }
}

// FUNC_AT(0x0001dc80)
AICharacterEnemyGround* AICharacterEnemyGround::Construct(const MATRIX4 *transform, int weapon, int female,
                                                          int unknown17c, bool registerTarget) {
    AICharacter::Construct();
    vtable = reinterpret_cast<void **>(kAICharacterEnemyGroundVtable);
    unknown144 = 800;
    unknown148 = 0;
    threatSide = 0;
    threatAngle = 0.0f;
    threatForward = {0.0f, 0.0f, 0.0f};
    this->unknown17c = unknown17c;
    unknown16c = 1.0f;
    unknown170 = 0;
    targetAbove = 0;
    collider = NULL;
    firstShot = 1;
    if (weapon < kEnemyWeapon1)
        weapon = kEnemyWeapon1;
    else if (weapon > kEnemyWeapon2)
        weapon = kEnemyWeapon2;
    this->transform = *transform;
    this->female = female;
    this->weapon = weapon;

    targetable = NULL;
    if (registerTarget) {
        void *block = UMemory::FastAlloc(sizeof(WTargetable), "WTargetable");
        targetable = block != NULL ? static_cast<WTargetable *>(block)->Construct(this) : NULL;
        TargetPicker.RegisterTarget(targetable);
    }
    claustrophobic = 0;
    worker = 0;
    avoided = 0;
    lastDodgeSide = 0;
    dodgeSide = 0;
    const Coord3 *position = reinterpret_cast<const Coord3 *>(transform->mtx[3]);
    groundHeight = position->y;
    previousPosition = *position;
    if (weapon >= kEnemyWeapon2)
        clipSize = 0;
    if (weapon == kEnemyWeapon1) {
        rounds = 25;
        clipSize = 25;
        animBank = kBankGround1;
    } else {
        animBank = weapon == kEnemyWeapon2 ? kBankGround2 : kBankGround1;
    }
    hitShape = &GroundHitShape;
    void *block = UMemory::FastAlloc(sizeof(WCollider), "WCollider");
    collider = block != NULL ? static_cast<WCollider *>(block)->Construct(position, kColliderRadius, false,
                                                                         WCollider::kCollideBarriers)
                             : NULL;
    return this;
}

// FUNC_AT(0x0001de80)
int AICharacterEnemyGround::IsAlive() {
    if (state == kStateDead || state == kStateDying || state == kStateTilting)
        return 0;
    return 1;
}

// FUNC_AT(0x0001dea0)
void AICharacterEnemyGround::Destruct() {
    vtable = reinterpret_cast<void **>(kAICharacterEnemyGroundVtable);
    if (targetable != NULL) {
        TargetPicker.UnregisterTarget(targetable);
        targetable->ownerType = kTargetPoint;
        targetable->RemoveReference();
    }
    if (collider != NULL) {
        collider->Destruct();
        UMemory::FastFree(collider, sizeof(WCollider));
    }
    AICharacter::Destruct();
}

// FUNC_AT(0x0001df40)
AICharacterEnemyGround* AICharacterEnemyGround::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterEnemyGround));
    return this;
}

// FUNC_AT(0x0001df70)
void AICharacterEnemyGround::DoInitial() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    if (worker) {
        state = kStateWandering;
        subState = 0;
        return;
    }
    state = animation != 0 ? kStateArmed : kStateNeutral;
}

// FUNC_AT(0x0001dfc0)
void AICharacterEnemyGround::DoNeutral() {
    if (!AICharacterEnemy::TargetIsInRange())
        return;
    state = kStateArming;
    PlayAnimation(kArmAnimations[weapon]);
}

// FUNC_AT(0x0001e010)
void AICharacterEnemyGround::DoAvoiding() {
    if (!actorLink->actor->IsAnimationDone())
        return;
    if (!avoiding || vec3distance(transform.mtx[3], &avoidCentre) > avoidRadius)
        avoided = 1;
    state = kStateArmed;
}

// FUNC_AT(0x0001e070)
void AICharacterEnemyGround::DoArming() {
    if (actorLink->actor->IsAnimationDone())
        state = kStateArmed;
}

// FUNC_AT(0x0001e090)
void AICharacterEnemyGround::DoArmed() {
    if (!AICharacterEnemy::TargetIsInRange()) {
        state = kStateUnarming;
        PlayAnimation(kUnarmAnimations[weapon]);
        return;
    }
    if (NeedsToReload()) {
        rounds = clipSize;
        state = kStateReloading;
        PlayAnimation(kReloadAnimations[weapon]);
        return;
    }
    if (!AICharacterEnemy::TargetIsAcquired()) {
        state = kStateAiming;
        int bearing = targetSide << 8 | targetBearing;
        if (bearing == kBearing25 || bearing == kBearing45 || bearing == kBearing90)
            PlayAnimation(kAimAnimations[weapon][0][1]);
        else
            PlayAnimation(kAimAnimations[weapon][1][1]);
        return;
    }

    uint8_t side, high;
    if (AvoidThreat(&side, &high)) {
        state = kStateAvoiding;
        if (high && fgSimulation.random->Fraction() < 0.5) {
            PlayAnimation(kHighDodgeAnimations[int(fgSimulation.random->Generate()) * 2 >> 16]);
            return;
        }
        // Not the way it last went, after it has avoided
        if (side)
            dodgeSide = lastDodgeSide == 0 && avoided == 1 ? 1 : 0;
        else
            dodgeSide = lastDodgeSide == 1 && avoided == 1 ? 0 : 1;
        lastDodgeSide = dodgeSide;
        avoided = 0;
        PlayAnimation(kDodgeAnimations[int(fgSimulation.random->Generate()) * 2 >> 16][dodgeSide]);
        return;
    }

    if (firing && fgSimulation.stepCount >= nextShotStep) {
        state = kStateFiring;
        nextShotStep = shotInterval + fgSimulation.stepCount;
        rounds--;
        if (targetAbove) {
            PlayAnimation(kFireAboveAnimations[weapon][int(fgSimulation.random->Generate()) * 2 >> 16]);
        } else {
            int pick = int(fgSimulation.random->Generate()) * 6 >> 16;
            if (firstShot) {
                firstShot = 0;
                pick = 0;
            }
            PlayAnimation(kFireAnimations[weapon][pick]);
        }
    }
}

// FUNC_AT(0x0001e380)
void AICharacterEnemyGround::DoDying() {
    if (scoreable) {
        glbMissionManager->IncKills(1);
        scoreable = 0;
    }
    if (actorLink->actor->IsAnimationDone() && state != kStateDead) {
        state = kStateDead;
        int steps = fgSimulation.stepsPerSecond * kDeathFadeSeconds;
        ActActor *actor = actorLink->actor;
        actor->alpha = float(1.0 / steps);
        actor->fadeTime = float(steps);
    }
}

// FUNC_AT(0x0001e400)
void AICharacterEnemyGround::UpdateRotPos() {
    if (thrown)
        MatrixCopy(&thrownTransform, &transform);
    else
        actorLink->actor->GetActorLocalPosOri(&transform);
}

// FUNC_AT(0x0001e440)
bool AICharacterEnemyGround::ThreatenedByVehicle() {
    for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
        Coord4 toCharacter;
        VU0_v4sub(transform.mtx[3], &BodyOf(*car)->position, &toCharacter);
        float side = v3dotprod(BodyOf(*car)->info->orientation.mtx[0], &toCharacter);
        double halfWidth = BodyOf(*car)->info->halfExtents.x + double(kThreatMargin);
        if (-halfWidth > side || halfWidth < side)
            continue;

        float ahead = v3dotprod(BodyOf(*car)->info->orientation.mtx[2], &toCharacter);
        RigidBody *body = BodyOf(*car);
        float speed = v3dotprod(BodyOf(*car)->info->orientation.mtx[2], &body->velocity);
        float reach = BodyOf(*car)->info->halfExtents.z * kThreatReachScale;
        double reachNow = BodyOf(*car)->info->halfExtents.z * double(kThreatReachScale);
        // Within its reach either way, or coming at the character within it
        bool threat = -reachNow < ahead && ahead < reachNow;
        if (!threat && 0.0f < speed && 0.0f < ahead && ahead < reach)
            threat = true;
        if (!threat && speed < 0.0f && -reach < ahead && ahead < 0.0f)
            threat = true;
        if (!threat)
            continue;

        // How urgent: nearer and faster scale it up, between 1 and 1.5
        float distance = VU0_v3length(&toCharacter);
        if (distance > kThreatFar)
            distance = kThreatFar;
        else if (distance < kThreatNear)
            distance = kThreatNear;
        unknown16c = float(kThreatFar / double(distance));
        float carSpeed = VU0_v3length(&BodyOf(*car)->velocity);
        if (carSpeed > kThreatFast)
            carSpeed = kThreatFast;
        else if (carSpeed < kThreatSlow)
            carSpeed = kThreatSlow;
        double scale = carSpeed * double(unknown16c) * kThreatScale;
        unknown16c = float(scale);
        if (scale > kThreatScaleMax)
            unknown16c = kThreatScaleMax;
        if (unknown16c < 1.0f)
            unknown16c = 1.0f;

        // The car's heading from the character's facing, and its bucket
        RigidBodyInfo *forZ = BodyOf(*car)->info;
        RigidBodyInfo *forX = BodyOf(*car)->info;
        threatAngle = float(AtanTurns(-forX->orientation.mtx[2][0], -forZ->orientation.mtx[2][2]));
        double turn = threatAngle - AtanTurns(transform.mtx[2][0], transform.mtx[2][2]);
        threatAngle = float(turn);
        if (turn < -0.5)
            threatAngle = float(turn + 1.0);
        if (threatAngle > 0.5f)
            threatAngle = float(threatAngle - 1.0);
        float angle = threatAngle;
        targetSide = angle > 0.0f;
        if (-kThreat0 < angle && angle <= 0.0f)
            targetBearing = kBearingAhead;
        else if (-kThreat45 < angle && angle <= -kThreat0)
            targetBearing = kBearing45;
        else if (-kThreat90 < angle && angle <= -kThreat45)
            targetBearing = kBearing90;
        else if (angle <= -kThreat90)
            targetBearing = kBearing127;
        else if (0.0f <= angle && angle < kThreat0)
            targetBearing = kBearingAhead;
        else if (kThreat0 <= angle && angle < kThreat45)
            targetBearing = kBearing45;
        else if (kThreat45 <= angle && angle < kThreat90)
            targetBearing = kBearing90;
        else if (angle >= kThreat90)
            targetBearing = kBearing127;

        threatSide = side < 0.0f;
        const float *forward = BodyOf(*car)->info->orientation.mtx[2];
        threatForward = {forward[0], forward[1], forward[2]};
        return true;
    }
    return false;
}

// FUNC_AT(0x0001e9a0)
double AICharacterEnemyGround::GetAngleToTarget(const Coord3 *target, bool fromWeapon) {
    double turn;
    if (fromWeapon) {
        MATRIX4 weaponTransform;
        actorLink->actor->GetWeaponPosition(&weaponTransform, true, 0);
        float bearing = float(AtanTurns(float(target->x - weaponTransform.mtx[3][0]),
                                        float(target->z - weaponTransform.mtx[3][2])));
        turn = bearing - AtanTurns(weaponTransform.mtx[2][0], weaponTransform.mtx[2][2]);
    } else {
        float bearing = float(AtanTurns(float(target->x - transform.mtx[3][0]),
                                        float(target->z - transform.mtx[3][2])));
        turn = bearing - AtanTurns(transform.mtx[2][0], transform.mtx[2][2]);
    }
    if (turn < -0.5)
        turn += 1.0;
    if (turn > 0.5)
        turn -= 1.0;
    return turn;
}

// FUNC_AT(0x0001ea60)
void AICharacterEnemyGround::PlayZoneInjuryAnim(int, float amount, int kind) {
    if (kind == 0)
        kind = kKindShot;
    float hp = hitPoints != NULL ? *hitPoints : 1.0f;

    if (amount >= hp) {
        if (state == kStateDying || state == kStateDead || state == kStateTilting)
            return;
        if (kind == kKindShot) {
            GetACharacter()->Say("HScream");
            state = kStateDying;
            int count = 7;
            if (claustrophobic) {
                actorLink->actor->SetSuppressAnimationTranslation(true);
                count = 6;
            }
            int pick = int(fgSimulation.random->Generate()) * count >> 16;
            if (pick == EnemyGroundLastScream)
                pick = (pick + 1) % count;
            EnemyGroundLastScream = pick;
            animBank = kBankDeath;
            CrossFadeTo(kBankDeath, kScreamAnimations[pick]);
        } else if (claustrophobic) {
            GetACharacter()->Say("HDeath");
            state = kStateDying;
            actorLink->actor->SetSuppressAnimationTranslation(true);
            int pick = int(fgSimulation.random->Generate()) * 4 >> 16;
            if (pick == EnemyGroundLastDeath)
                pick = (pick + 1) % 4;
            EnemyGroundLastDeath = pick;
            animBank = kBankDeath;
            CrossFadeTo(kBankDeath, kClaustrophobicDeathAnimations[pick]);
        } else {
            GetACharacter()->Say("HDeath");
            state = kStateDying;
            if (zoneLastHit == 2 || zoneLastHit == 3 || zoneLastHit == 4)
                zoneLastHit = (int(fgSimulation.random->Generate()) * 4 >> 16) + 2;
            int count = CountAnimations(kDeathAnimations[weapon][zoneLastHit]);
            int next;
            if (zoneLastHit == kZoneHead && fgSimulation.random->Generate() * double(kRandomScale) > 0.25)
                next = kHeadDeathAnimations[int(fgSimulation.random->Generate()) * 4 >> 16];
            else
                next = kDeathAnimations[weapon][zoneLastHit][int(fgSimulation.random->Generate()) * count >> 16];
            animBank = kBankDeath;
            CrossFadeTo(kBankDeath, next);
        }
        if (targetable != NULL)
            TargetPicker.UnregisterTarget(targetable);
        return;
    }

    if (state == kStateDying || state == kStateDead || state == kStateTilting)
        return;
    GetACharacter()->Say("HGrunt");
    if (state == kStateInitial)
        return;
    state = kState14;
    int count = CountAnimations(kHitAnimations[weapon][zoneLastHit]);
    int pick = int(fgSimulation.random->Generate()) * count >> 16;
    if (pick == EnemyGroundLastHit)
        pick = (pick + 1) % count;
    EnemyGroundLastHit = pick;
    int next = kHitAnimations[weapon][zoneLastHit][pick];
    if (claustrophobic)
        next = kClaustrophobicHitAnimations[weapon][int(fgSimulation.random->Generate()) * 3 >> 16];
    CrossFadeTo(animBank, next);
}

// FUNC_AT(0x0001ee60)
void AICharacterEnemyGround::DoWandering() {
    ActActor *actor = actorLink->actor;
    if (subState == 0) {
        if (!actor->IsAnimationDone())
            return;
        if (AICharacterEnemy::TargetIsInRange()) {
            animation = kWanderAimAnimation;
            animController = actor->SetNewAnimation(kBankWandering, animation);
            subState = kBankWandering;
        } else {
            animation = kWanderAnimations[int(fgSimulation.random->Generate()) * 3 >> 16];
            animController = actor->SetNewAnimation(kBankWandering, animation);
        }
        crossFade = NULL;
        return;
    }
    if (subState != kBankWandering)
        return;

    // Turning to the player's car, from the weapon once the animation has raised it
    float frame = animController->GetCurrentFrame();
    MATRIX4 weaponTransform;
    actor->GetWeaponPosition(&weaponTransform, true, 0);
    Coord3 target = BodyOf(fgSimulation.cars.first[0])->position;
    double turn = GetAngleToTarget(&target, frame >= kWanderAimFrame);
    double step = fgSimulation.timeStep * turn;
    actor->CurrentPositionRotateY(float(step + step));
    if (!actor->IsAnimationDone())
        return;
    if (weapon == kEnemyWeapon1) {
        animBank = kBankGround1;
        state = kStateArming;
        subState = 0;
    } else {
        state = kStateArming;
        subState = 0;
        animBank = weapon == kEnemyWeapon2 ? kBankGround2 : kBankGround1;
    }
}

// FUNC_AT(0x0001f000)
void AICharacterEnemyGround::DoAiming() {
    MATRIX4 weaponTransform;
    actorLink->actor->GetWeaponPosition(&weaponTransform, true, 0);
    Coord3 target = hasCommandTarget ? commandTarget : BodyOf(fgSimulation.cars.first[0])->position;
    double turn = GetAngleToTarget(&target, true);
    actorLink->actor->CurrentPositionRotateY(float(fgSimulation.timeStep * turn * kAimTurnRate));
    if (actorLink->actor->IsAnimationDone())
        state = kStateArmed;
}

// FUNC_AT(0x0001f0c0)
void AICharacterEnemyGround::DoFiring() {
    MATRIX4 weaponTransform;
    actorLink->actor->GetWeaponPosition(&weaponTransform, true, 0);
    Coord3 target = hasCommandTarget ? commandTarget : BodyOf(fgSimulation.cars.first[0])->position;
    double turn = GetAngleToTarget(&target, true);
    actorLink->actor->CurrentPositionRotateY(float(fgSimulation.timeStep * turn * kFireTurnRate));
    if (actorLink->actor->IsAnimationDone())
        state = kStateArmed;
}

// FUNC_AT(0x0001f180)
void AICharacterEnemyGround::DoRunning() {
    if ((crossFade == NULL || crossFade->poser->mode == kPoseModeNormal) && ThreatenedByVehicle()) {
        // Away across the car's path, along the ground, and round a barrier in the way
        Coord4 away;
        if (threatSide)
            away = {-threatForward.z, threatForward.y, threatForward.x, 0.0f};
        else
            away = {threatForward.z, threatForward.y, -threatForward.x, 0.0f};
        Coord4 target;
        VU0_v4scaleadd(&away, kRunDistance, transform.mtx[3], &target);
        target.y = transform.mtx[3][1];
        Coord4 segment[2] = {
            {transform.mtx[3][0], transform.mtx[3][1], transform.mtx[3][2], 1.0f},
            {target.x, transform.mtx[3][1], target.z, 1.0f},
        };
        WorldCollisionInfo info;
        info.Construct();
        if (collider->GetWorldNormal(segment, &info)) {
            VU0_v4scaleadd(&info.normal, 1.0f, &info.point, &target);
            Coord4 offset;
            VU0_v4sub(&target, transform.mtx[3], &offset);
            VU0_v4unitxyz(&offset, &offset);
            VU0_v4scaleadd(&offset, kRunDistance, &target, &target);
        }
        double turn = GetAngleToTarget(Xyz(&target), false);
        actorLink->actor->CurrentPositionRotateY(float(fgSimulation.timeStep * turn * kAimTurnRate));
    }
    if (!actorLink->actor->IsAnimationDone())
        return;
    if (!ThreatenedByVehicle()) {
        state = kStateArmed;
        return;
    }
    PlayAnimation(kRunAnimations[weapon]);
}

// FUNC_AT(0x0001f3c0)
void AICharacterEnemyGround::HandleInterrupts() {
    float hp = hitPoints != NULL ? *hitPoints : 1.0f;
    newDamage = float(initialHitPoints - double(hp) + newDamage);
    initialHitPoints = hitPoints != NULL ? *hitPoints : 1.0f;

    // A car coming at it: dodge, then run
    if (unknown17c == 0 && state != kStateDodging && state != kState14 && state != kStateDying &&
        state != kStateDead && state != kStateTilting && ThreatenedByVehicle()) {
        state = kStateDodging;
        subState = kStateRunning;
        CrossFadeTo(animBank, kRunAnimations[weapon]);
    }

    const Coord3 *position = &Position();
    if (!collider->InRegion(position, kColliderRefresh, WCollider::kCollideBarriers)) {
        if (collider != NULL) {
            collider->Destruct();
            UMemory::FastFree(collider, sizeof(WCollider));
        }
        void *block = UMemory::FastAlloc(sizeof(WCollider), "WCollider");
        collider = block != NULL ? static_cast<WCollider *>(block)->Construct(position, kColliderRebuilt, false,
                                                                             WCollider::kCollideBarriers)
                                 : NULL;
    }

    // The ground fallen away under it by two or more: thrown, fading out
    human->worldPos.FindClosestFace(position, true);
    if (human->worldPos.valid) {
        float ground = float(human->worldPos.HeightAtPoint(position, false));
        if (state != kStateTilting && fabs(ground - double(groundHeight)) >= kFallHeight) {
            if (scoreable) {
                glbMissionManager->IncKills(1);
                scoreable = 0;
            }
            double fade = fabs(ground - double(groundHeight)) * kFallFadeScale;
            if (fade < 1.0)
                fade = 1.0;
            double steps = fade * fgSimulation.stepsPerSecond;
            ActActor *actor = actorLink->actor;
            actor->alpha = float(1.0 / steps);
            actor->fadeTime = float(steps);
            GetACharacter()->Say("HYell");
            actorLink->actor->TurnShadowsOff();
            actorLink->actor->followGround = false;
            MatrixCopy(&transform, &thrownTransform);
            thrownVelocity = {0.0f, 0.0f, 0.0f};
            VU0_v4sub(position, &previousPosition, &thrownVelocity);
            VU0_v4scale(&thrownVelocity, float(fgSimulation.stepsPerSecond), &thrownVelocity);
            thrown = 1;
            state = kStateTilting;
            animBank = kBankDeath;
            if (crossFade != NULL && crossFade->poser->mode != kPoseModeNormal)
                PlayAnimation(kThrownAnimation);
            else
                CrossFadeTo(kBankDeath, kThrownAnimation);
        } else {
            groundHeight = ground;
        }
    }

    // Kept out of the barriers: a step into one is taken back
    Coord4 last = {position->x, position->y, position->z, 0.0f};
    if (state != kStateTilting && state != kStateInitial) {
        Coord4 moved;
        VU0_v4sub(position, &previousPosition, &moved);
        Coord4 ahead = {moved.x, 0.0f, moved.z, 0.0f};
        VU0_v4unitxyz(&ahead, &ahead);
        // The step's direction scaled by its own length (one, or zero), from the position
        VU0_v4scaleadd(&ahead, VU0_v3length(&ahead), position, &ahead);
        Coord4 segment[2] = {
            {position->x, position->y, position->z, 1.0f},
            {ahead.x, ahead.y, ahead.z, 1.0f},
        };
        WorldCollisionInfo info;
        info.Construct();
        if (collider->GetWorldNormal(segment, &info)) {
            Coord4 back = {-moved.x, -moved.y, -moved.z, 0.0f};
            Coord4 previous = {previousPosition.x, previousPosition.y, previousPosition.z, 0.0f};
            VU0_v3add(&previous, &back, &last);
            MoveToPoint(Xyz(&previous), false);
        }
    }
    previousPosition = *Xyz(&last);
    newDamage = 0.0f;
}

// FUNC_AT(0x0001f8f0)
int AICharacterEnemyGround::TargetIsAcquired() {
    MATRIX4 weaponTransform;
    actorLink->actor->GetWeaponPosition(&weaponTransform, true, 0);
    const Coord3 &to = hasCommandTarget ? commandTarget : BodyOf(fgSimulation.cars.first[0])->position;
    Coord4 target = {to.x, to.y, to.z, 0.0f};
    Coord4 toTarget;
    VU0_v4sub(&target, transform.mtx[3], &toTarget);
    VU0_v4unitxyz(&toTarget, &toTarget);
    targetAbove = toTarget.y > kTargetAbove;

    double turn = GetAngleToTarget(Xyz(&target), true);
    float angle = float(turn);
    targetSide = turn > 0.0;
    if (angle < -kAcquired45 || kAcquired45 < angle)
        targetBearing = kBearing90;
    else if (angle < -kAcquired25 || kAcquired25 < angle)
        targetBearing = kBearing45;
    else if (angle < -kAcquired0 || kAcquired0 < angle)
        targetBearing = kBearing25;
    else
        targetBearing = kBearingAhead;
    return targetBearing == kBearingAhead;
}
