#pragma fp_contract(off)

#include "Human.h"

#include "../anim/Actor.h"              // ActActor
#include "../anim/Character.h"          // ActCharacter
#include "../anim/Weapon.h"             // ActWeapon
#include "../engine/MissionManager.h"
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix
#include "../engine/UMemory.hpp"
#include "../physics/SimpleRigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"

// ---------------------------------------------------------------------------------------------------------------
// Human (0x0005dc60-0x0005e180), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet


// ---- globals
#define Sim ((void *)0x00233ff0)                                    // the Simulation

namespace {

const Coord3 kZero = {0.0f, 0.0f, 0.0f};

// ApplyDamage's kinds (as PBondCar's)
enum HumanDamageKind : int32_t {
    kDamageBullet = 2,
    kDamageKind3 = 3,
};

constexpr int kIgnoredType = 1;             // unknown6c's value that takes no damage
constexpr float kKind3MinDamage = 20.0f;    // kind 3 hits below it are ignored
constexpr float kScoredDamage = 5.0f;       // the least damage that counts as a shot hit
constexpr int kDamageTaken = 0x80;
constexpr float kBodyRadius = 0.75f;
constexpr float kBodyMass = 100.0f;

} // namespace

// ---- construction

// FUNC_AT(0x0005dd90)
Human* Human::Construct(int unknown6c, const Coord3 *direction, const Coord3 *position, uint32_t ownerSig) {
    PhysicsObject::Construct("human", "Human", kObjectHuman, ownerSig, false, false);
    vtable = reinterpret_cast<void **>(kHumanVtable);
    this->unknown6c = unknown6c;
    character = NULL;
    dead = 0;
    unknown8c = 0;
    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleHuman, position, &kZero, &kZero, &orientation, kBodyRadius, kBodyMass);
    Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags |= SimpleRigidBody::kFlag01;
    return this;
}

// FUNC_AT(0x0005dc60)
void Human::Destruct() {
    vtable = reinterpret_cast<void **>(kHumanVtable);
    if (character != NULL)
        character->Delete(1);
    character = NULL;
    PhysicsObject::Destruct();
}

// FUNC_AT(0x0005dea0)
Human* Human::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(Human));
    return this;
}

// The body at the character's matrix, at rest
void Human::FollowCharacter() {
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    body->SetOrientMat(&character->transform);
    const float *origin = character->transform.mtx[3];
    body->position = {origin[0], origin[1], origin[2]};
    body->acceleration = kZero;
    body->velocity = kZero;
}

// FUNC_AT(0x0005ded0)
void Human::SetAICharacter(AICharacter *character) {
    this->character = character;
    if (character->hitPoints != NULL)
        SetHitPointLoc(character->hitPoints);
    else
        Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags |= SimpleRigidBody::kFlag01;
    ActCharacter *actor = character->actorLink->actor->character;
    for (int i = 0; i < ActCharacter::kWeapons; i++) {
        if (actor->weapons[i] != NULL)
            actor->weapons[i]->SetOwner(this);
    }
    FollowCharacter();
}

// FUNC_AT(0x0005dcc0)
void Human::Activate() {
    Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags &= ~SimpleRigidBody::kFlag01;
}

// FUNC_AT(0x0005dce0)
void Human::Deactivate() {
    Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags |= SimpleRigidBody::kFlag01;
}

// FUNC_AT(0x0005dd00)
void Human::Simulate() {
    if (character == NULL)
        return;
    AICharacter_Execute(character, 0);
    FollowCharacter();
    PhysicsObject::Simulate();
}

// ---- damage

// FUNC_AT(0x0005dfa0)
int Human::ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float, int kind,
                       const uint32_t *sourceSig) {
    if (unknown6c == kIgnoredType)
        return 0;
    AICharacter *ai = character;
    if (ai->actorLink->actor->visible != 1 || dead)
        return 0;
    if (amount < kKind3MinDamage && kind == kDamageKind3)
        return 0;

    bool scoring = ai->scoreable;
    PhysicsObject *source = Simulation_FindPhysicsObjectSignature(Sim, 0, *sourceSig);
    if (source != NULL && !source->IsOwnedBy(Simulation_GetPlayerObject(Sim, 0)) && kind == kDamageBullet) {
        lastHit = *to;
    } else {
        if (scoring && amount >= kScoredDamage)
            SMissionManager_IncShotsHit(glbMissionManager, 0, kind == kDamageBullet);
        if (kind == kDamageKind3 && ai->hitShape != NULL) {
            // Moved along the hit's line from where it started
            Coord4 line;
            VU0_v4sub(to, from, &line);
            VU0_v4unitxyz(&line, &line);
            VU0_v4scaleadd(&line, ai->hitShape->radius, from, &lastHit);
        } else {
            lastHit = *to;
        }
    }

    if (!dead && ai->hitShape != NULL) {
        ai->NotifyZoneDamage(&lastHit);
        int zone = AICharacter_GetZoneLastHit(ai, 0);
        amount = float(ai->GetZoneHitPointScale(zone, amount) * amount);
        ai->PlayZoneInjuryAnim(zone, amount, kind);
    }
    LoseHitPoints(amount);
    dead = GetHitPoints() <= 0.0f;
    if (dead)
        Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags |= SimpleRigidBody::kFlag01;
    return kDamageTaken;
}
