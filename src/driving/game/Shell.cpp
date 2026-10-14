#pragma fp_contract(off)

#include "Shell.h"
#include "../Vtables.h"   // our copies of the game's vtables

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AICharacter.h"
#include "AIElements.h"
#include "Explosion.h"                  // ExplosionType
#include "Gfx.h"
#include "Human.h"
#include "Simulation.h"
#include "Vehicle.h"                    // PVehicle, CarPhysics
#include "WeaponEffects.h"              // BulletStreakKinds, RBulletStreak, RScorchWorld
#include "../../helpers.h"
#include "../anim/AnimEngine.h"         // Handle
#include "../anim/Manager.h"            // MakeCoord4 (from a Coord3)
#include "../audio/Bank.h"              // fgBanks
#include "../audio/Sound.h"             // ABaseSound, AOneShotSound
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/OBB.h"
#include "../engine/PhysicsUtil.h"      // Util_*
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"       // RigidBody, DamageSourceSig
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/Lights.h"           // fgLightManager
#include "../render/Particles.h"        // fgParticleSystems
#include "../render/RSceneObj.hpp"      // RSceneObj, RandomScaled
#include "../world/CollisionManager.h"  // fgCollisionMgr
#include "../world/World.h"             // fgWorld
#include "../world/WorldPos.h"
#include "../logging.h"

// ---------------------------------------------------------------------------------------------------------------
// RayShell (0x00071930-0x00073150) and Shell (0x00074660-0x000752f0), ported from the listing. See Shell.h.
//
// The x87 chains are computed in double in the original's order and rounded where it stores a float; comparisons
// fail on NaN where the original's jumps do. Words the original leaves as its stack had them (a vector's w that
// only the xyz functions touch, WorldCollisionInfo's segment words, which its inlined constructor copies from
// uninitialised stack) are zero here. One dead computation is left out: CollideWithObjects' first offset from the
// origin to the object.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

// The bits SimpleRigidBody::CheckCollisions set for each simple body, by slot (rigid bodies 0-63, simple 64-159)

// ---- ours

// XBE_GLOBAL(0x001e7ad0, 0x200)
RayShellTracerSource RayShellTracerSources[kRayShellTracers];
// XBE_GLOBAL(0x001e7cd0, 4)
int32_t RayShellTracerCount;
// XBE_GLOBAL(0x001e7ce0, 0x800)
RayShellTracer RayShellTracers[kRayShellTracers];
// XBE_GLOBAL(0x001e84e0, 4)
int32_t RayShellFlashCount;
// XBE_GLOBAL(0x001e84e4, 4)
int32_t ActiveRayShellCount;
// XBE_GLOBAL(0x001e84e8, 4)
int32_t RayShellExplosionStep;
// XBE_GLOBAL(0x001e84f0, 0x80)
RayShellFlash RayShellFlashes[kRayShellFlashes];
// XBE_GLOBAL(0x001e8570, 0xc00)
ActiveRayShell ActiveRayShells[kActiveRayShells];
// XBE_GLOBAL(0x001c3df4, 4)
int32_t TriggerHittingRayShell = -1;

// XBE_GLOBAL(0x001e9280, 4)
uint32_t ShellConcreteSounds;
// XBE_GLOBAL(0x001e9284, 4)
int32_t ShellRicochetSound;
// XBE_GLOBAL(0x001e9288, 4)
RParticleSystemData *ShellSparks;
// XBE_GLOBAL(0x001e928c, 4)
uint32_t ShellSparksGuard;

namespace {

void **const kRayShellVtable = (void **)Vtable_RayShell;
void **const kShellVtable = (void **)Vtable_Shell;

// PhysicsObject::type

constexpr unsigned kSimulationBodies = 160;     // the ordered bodies: 64 rigid, then 96 simple
constexpr int kDamageKindShell = 2;             // ApplyDamage's kind
constexpr float kDamageSpread = 0.25f;          // ApplyDamage's fourth argument

// RayShell
constexpr float kMaxRange = 220.0f;
constexpr float kLaterPassRange = 3.0f;
constexpr float kLaterPassBackOff = 4.0f;       // a later pass starts this far short of the last hit
constexpr float kNoHitDistance = 150.0f;
constexpr float kNearWorld = 20.0f;             // a first-pass world hit nearer than this: the later passes test it
constexpr int kTracerLifeSteps = 60;
constexpr float kTracerStartOffset = 2.0f;      // kind 0's tracers start this far along
constexpr float kStepFraction = 0.25f;          // CollideWithWorld's step: a quarter of the range...
constexpr float kMaxStep = 55.0f;               // ... at most this
constexpr float kScorchSize = 0.0625f;
constexpr int kHeavyShellType = 0x1d;           // five times the scorch
constexpr float kHeavyScorchScale = 5.0f;
constexpr int kScorchTypeRay = 2;
constexpr int kCheatExplosionKind = -1;
constexpr float kCheatExplosionSize = 50.0f;
constexpr int kCheatExplosionArg6 = 5;
constexpr int kCheatExplosionsPerSecond = 5;
constexpr float kFlashRadius = 6.0f;            // the flash light's w
constexpr float kFlashLevel = 0.2f;
// A human hit by the player: the sphere CollideWithObjects tests when its character has no hit shape
constexpr float kHumanRadius = 0.35f;
constexpr float kHumanStretch = 2.5f;
const Coord4 kRayHalfExtents = {0.01f, 0.01f, 250.0f, 1.0f};   // the ray's box against a rigid body's

// Shell
constexpr int kSlowShellType = 0x10;
constexpr float kSpeedVariation = 30.0f;
constexpr float kSlowShellSpeed = 170.0f;
constexpr float kShellSpeed = 200.0f;
constexpr float kShellLifeSeconds = 1.3f;
constexpr float kShellRadius = 0.3f;
constexpr float kShellMass = 0.1f;
constexpr float kHitDamageScale = 1.0f;
constexpr float kShellScorchScale = 0.005f;     // of the damage
constexpr int kScorchTypeShell = 5;
constexpr float kExplodesAbove = 1.0f;          // explosionSize

// ApplyWorldEffects
constexpr float kStraightUp = 0.9f;             // the normal's dot with (0, 1, 0, 1) above this: built from x instead
constexpr uint32_t kSparksTag = 0x53504b42;     // 'SPKB'
constexpr int kSparksCount = 2;
constexpr int kAIZoneImpact = 3;
constexpr int kDamageResultFlesh = 0x80;        // ApplyDamage's answer for a body that bleeds
constexpr float kRicochetChance = 0.25f;
constexpr float kOneIn65536 = 1.0f / 65536.0f;
constexpr int kWeaponBankSlot = 4;
constexpr float kImpactMinDistance = 10.0f;
constexpr float kImpactMaxDistance = 50.0f;

// The face types ApplyWorldEffects tells apart (the low nibble and high nibble of the face's type are also read
// apart, for the sound)
enum ShellFaceType : uint8_t {
    kFaceSplash = 0x06,                 // "bulletsplash"
    kFaceSnow08 = 0x08,                 // these four: "snowbullethit", no sparks
    kFaceSnow09 = 0x09,
    kFaceSnow30 = 0x30,
    kFaceSnow40 = 0x40,
    kFaceHut = 0x0c,                    // "bulletHut"
    kFaceCar = 0x0d,                    // "bulletcar"
    kFaceFlesh = 0x80,                  // "Henbult", "SFX_BulletFlesh"
    kFaceBrokenWindow = 0x70,           // WCollisionMgr::CheckHitWindow's; no scorch, "SFX_BulletGlass"
};
enum ShellFaceSound : uint8_t {         // the type's high nibble ...
    kSoundMetal90 = 0x90,
    kSoundMetal10 = 0x10,
    kSoundWood = 0x20,
    kSoundDirt = 0x50,
    kSoundGlass = 0x70,
    kSoundFlesh = 0x80,
};
enum ShellFaceSoundLow : uint8_t {      // ... and its low nibble
    kLowDirt = 0x5,
    kLowWood = 0x7,
    kLowMetal = 0xd,
};
constexpr uint8_t kFaceSubTypeNoScorch = 0x02;

// The shell types with incidental effects of their own on the world
constexpr int kShellTypeBullgat = 0x1a;
constexpr int kShellTypeRockH = 0x1b;
constexpr int kShellTypeRock = 0x1d;

// ApplyWorldDamage's stimuli for the scene object hit, by the damage
enum ShellStimulus : uint8_t {
    kStimulusDamage10 = 0x13,
    kStimulusDamage50 = 0x14,
    kStimulusDamage250 = 0x15,
    kStimulusDamage500 = 0x16,
};
constexpr int kStimulusMode = 2;

const char *const kMetalSounds[5] = {
    "SFX_BulletMetal1", "SFX_BulletMetal2", "SFX_BulletMetal3", "SFX_BulletMetal4", "SFX_BulletMetal5",
};
const char *const kRicochetSounds[3] = { "SFX_Ricochet1", "SFX_Ricochet2", "SFX_Ricochet3" };

const Coord3 kZero3 = {0.0f, 0.0f, 0.0f};
const Coord4 kOrigin4 = {0.0f, 0.0f, 0.0f, 1.0f};

// Four vector rows, as ApplyWorldEffects builds its orientation
struct ShellFrame {
    Coord4 row[4];
};

PhysicsObject *FindObject(uint32_t sig) {
    return fgSimulation.FindPhysicsObjectSignature(sig);
}

bool IsSnow(uint8_t faceType) {
    return faceType == kFaceSnow09 || faceType == kFaceSnow08 || faceType == kFaceSnow30 || faceType == kFaceSnow40;
}

// The player's car's physics has subPhysics set
bool PlayerCarSubPhysics() {
    return fgSimulation.cars.first[0]->GetPhysics()->subPhysics != 0;
}

// With the cheat flag, a player's hit sets off an explosion at most five times a second (CollideWithWorld and
// DamagePhysicsObject share it)
void CheatExplosion(uint32_t ownerSig, const Coord3 *position) {
    if (fgSimulation.stepCount > fgSimulation.stepsPerSecond / kCheatExplosionsPerSecond + RayShellExplosionStep ||
        fgSimulation.stepCount == RayShellExplosionStep) {
        RayShellExplosionStep = fgSimulation.stepCount;
        PhysicsObject *player = fgSimulation.GetPlayerObject();
        if (FindObject(ownerSig)->IsOwnedBy(player) && (Launch.flags & kLaunchFlag1000)) {
            Coord3 up = {0.0f, 1.0f, 0.0f};
            fgSimulation.SpawnExplosion(kMissileExplosion, kCheatExplosionKind, &up, position, kCheatExplosionSize,
                                        kCheatExplosionArg6, NULL, DamageSourceSig);
        }
    }
}

}  // namespace

// ---- RayShell: the trigger queue

// FUNC_AT(0x00071930)
int RayShell::GetNumActiveRayShells() {
    return ActiveRayShellCount;
}

// FUNC_AT(0x00071940)
ActiveRayShell* RayShell::GetActiveRayShell(int index) {
    return &ActiveRayShells[index];
}

// FUNC_AT(0x00071960)
void RayShell::ClearActiveRayShells() {
    ActiveRayShellCount = 0;
}

// FUNC_AT(0x00071970)
void RayShell::SetTriggerHittingRayShell(int index) {
    TriggerHittingRayShell = index;
}

// FUNC_AT(0x00071980)
ActiveRayShell* RayShell::GetTriggerHittingRayShell() {
    if (TriggerHittingRayShell == -1)
        return NULL;
    return &ActiveRayShells[TriggerHittingRayShell];
}

// FUNC_AT(0x00071d80)
void RayShell::AddNewRayShellForTriggerCheck(float distance) {
    if (ActiveRayShellCount + 1 >= kActiveRayShells) {
        dbg_wprintf("TOO MANY RAY SHELLS\n");
        return;
    }
    ActiveRayShell &ray = ActiveRayShells[ActiveRayShellCount];
    ray.ownerSignature = ownerSig;
    ray.start = *Xyz(&origin);
    Coord4 reach;
    VU0_v4scale(&direction, distance, &reach);
    VU0_v3add(&origin, &reach, &ray.end);
    ray.radius = distance * 0.5f;
    ray.damage = damage;
    ActiveRayShellCount++;
}

// ---- RayShell: tracers and flashes

// FUNC_AT(0x000719a0)
void RayShell::ClearTracers(uint32_t before) {
    int count = RayShellTracerCount;
    if (count <= 0)
        return;
    for (int i = 0; i < count; i++) {
        if (RayShellTracers[i].time < before) {
            if (i < count - 1) {
                RayShellTracers[i] = RayShellTracers[count - 1];
                RayShellTracerSources[i] = RayShellTracerSources[count - 1];
            }
            count--;
            i--;
        }
    }
    RayShellTracerCount = count;
}

// FUNC_AT(0x00071a60)
void RayShell::DrawTracers() {
    uint32_t now = fgSimulation.stepCount;
    ClearTracers(now - kTracerLifeSteps);
    for (int i = 0; i < RayShellTracerCount; i++) {
        RayShellTracer &tracer = RayShellTracers[i];
        float age = (float)(double(now - tracer.time) * fgSimulation.timeStep);
        const RayShellTracerSource &source = RayShellTracerSources[i];
        if (source.object != NULL && fgHandObj->shown) {
            MATRIX4 transform;
            source.object->LocateFX(source.effect, &transform, true);
            age = age * age;
            tracer.start.x = transform.mtx[3][0];
            tracer.start.y = transform.mtx[3][1];
            tracer.start.z = transform.mtx[3][2];
        }
        Coord4 path;
        VU0_v4sub(&tracer.end, &tracer.start, &path);
        float lengthSquared = VU0_v3lengthsquare(&path);
        VU0_v4unitxyz(&path, &path);
        const BulletStreakKind &kind = BulletStreakKinds[tracer.kind];
        Coord4 head;
        VU0_v4scaleadd(&path, (float)(double(kind.speed) * age + kind.length), &tracer.start, &head);
        if (VU0_v3distancesquare(&tracer.start, &head) > lengthSquared) {
            // past its end: the last tracer takes its place
            int last = RayShellTracerCount - 1;
            if (i < last) {
                RayShellTracers[i] = RayShellTracers[last];
                RayShellTracerSources[i] = RayShellTracerSources[last];
            }
            RayShellTracerCount = last;
            i--;
        } else {
            RBulletStreak::Add(Xyz(&head), Xyz(&path), tracer.kind);
        }
    }
}

// FUNC_AT(0x00071c80)
void RayShell::DrawFlashes() {
    if (RayShellFlashCount < 1)
        return;
    const Coord3 &at = RayShellFlashes[0].position;
    Coord4 position = {at.x, at.y, at.z, kFlashRadius};
    Coord4 colour = {kFlashLevel, kFlashLevel, kFlashLevel, kFlashLevel};
    fgLightManager->AddPositionalLight(&position, &colour);
    int count = RayShellFlashCount;
    if (count > 1)
        memmove(&RayShellFlashes[0], &RayShellFlashes[1], (count - 1) * sizeof(RayShellFlash));
    RayShellFlashCount = count - 1;
}

// FUNC_AT(0x00071d40)
void RayShell::Reset() {
    ClearTracers(fgSimulation.stepCount + 1);
    ActiveRayShellCount = 0;
    RayShellFlashCount = 0;
    for (int i = 0; i < kRayShellTracers; i++)
        RayShellTracerSources[i].object = NULL;
    RayShellExplosionStep = 0;
}

// FUNC_AT(0x00071fb0)
void RayShell::StaticStoreTracer(const Coord3 *start, const Coord3 *end, int kind, RSceneObj *object,
                                 const ArticleEffect *effect) {
    if (kind < 0 || RayShellTracerCount >= kRayShellTracers)
        return;
    int i = RayShellTracerCount;
    RayShellTracer &tracer = RayShellTracers[i];
    tracer.start = *start;
    tracer.end = *end;
    tracer.time = fgSimulation.stepCount;
    tracer.kind = kind;
    RayShellTracerSources[i].object = object;
    RayShellTracerSources[i].effect = effect;
    if (kind == 0 && VU0_v3distancesquare(start, end) > kTracerStartOffset * kTracerStartOffset) {
        Coord4 along;
        along.x = end->x - start->x;
        along.y = end->y - start->y;
        along.z = end->z - start->z;
        along.w = 0.0f;
        VU0_v4unitxyz(&along, &along);
        VU0_v4scale(&along, kTracerStartOffset, &along);
        VU0_v3add(&RayShellTracers[RayShellTracerCount].start, &along, &RayShellTracers[RayShellTracerCount].start);
    }
    RayShellTracerCount++;
}

// FUNC_AT(0x000720d0)
void RayShell::AddFlash(uint32_t ownerSig, const Coord3 *position) {
    if (RayShellFlashCount >= kRayShellFlashes)
        return;
    PhysicsObject *owner = FindObject(ownerSig);
    if (owner == NULL || owner == fgSimulation.GetPlayerObject())
        return;
    if (owner->type != kObjectHuman && owner->type != kObjectSentry)
        return;
    for (int i = 0; i < RayShellFlashCount; i++) {
        if (RayShellFlashes[i].ownerSig == ownerSig)
            return;
    }
    RayShellFlash &flash = RayShellFlashes[RayShellFlashCount];
    flash.ownerSig = ownerSig;
    flash.position = *position;
    RayShellFlashCount++;
}

// ---- RayShell: the shot

// FUNC_AT(0x00072160)
RayShell* RayShell::Delete(unsigned flags) {
    vtable = kRayShellVtable;
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RayShell));
    return this;
}

// FUNC_AT(0x00071e40)
void RayShell::ConsiderVictim(uint32_t sig, const Coord3 *hitPoint, const Coord3 *impactPoint, float distance) {
    if (numVictims == 0 || distance < victims[0].distance) {
        victims[0].hitPoint = *hitPoint;
        victims[0].impactPoint = *impactPoint;
        victims[0].sig = sig;
        victims[0].distance = distance;
        if (numVictims == 0)
            numVictims = 1;
        return;
    }
    PhysicsObject *object = FindObject(sig);
    PhysicsObject *nearest = FindObject(victims[0].sig);
    if (object->type == kObjectHuman) {
        if (!static_cast<Human *>(object)->unknown8c)
            return;
    } else if (!(distance < victims[0].distance) ||
               !(double(victims[0].distance) - nearest->GetRadius() < distance)) {
        return;
    }
    if (numVictims == 1 || distance < victims[1].distance) {
        victims[1].hitPoint = *hitPoint;
        victims[1].impactPoint = *impactPoint;
        victims[1].sig = sig;
        victims[1].distance = distance;
        if (numVictims == 1)
            numVictims = 2;
    }
}

// FUNC_AT(0x00072190)
bool RayShell::CollideWithObjects() {
    for (unsigned index = 0; index < kSimulationBodies; index++) {
        PhysicsObject *object = fgSimulation.GetOrderedBody(index);
        if (object == NULL || object->IsOwnedBy(FindObject(ownerSig)))
            continue;
        if ((object->flags & PhysicsObject::kSimpleBody) &&
            (fgSimulation.GetSimpleRigidBody(object->rigidBodySlot)->flags & SimpleRigidBody::kFlag01))
            continue;

        bool stretched = false;
        float radius = object->GetRadius();
        float lift = 0.0f;
        float stretch = 1.0f;
        const Coord3 *position = object->GetPosition();
        Coord4 centre = {position->x, position->y, position->z, 0.0f};
        if (object->type == kObjectHuman) {
            // only the player's shots hit humans
            if (FindObject(ownerSig) != fgSimulation.GetPlayerObject())
                continue;
            AICharacter *character = static_cast<Human *>(object)->character;
            if (character != NULL) {
                VU0_MATRIX4_vect3mult(&character->centre, &character->transform, &centre);
                stretched = true;
                if (character->hitShape != NULL) {
                    radius = character->hitShape->radius;
                    lift = character->hitShape->lift;
                    stretch = character->hitShape->stretch;
                } else {
                    radius = kHumanRadius;
                    lift = 0.0f;
                    stretch = kHumanStretch;
                }
            }
        }

        Coord4 hit = {0.0f, 0.0f, 0.0f, 0.0f};
        bool met;
        if (stretched) {
            centre.y = centre.y + lift;
            met = Util_CollideRayWithStretchedSphere(&origin, &direction, &centre, radius, stretch, Xyz(&hit));
        } else {
            met = Util_CollideRayWithSphere(&origin, &direction, &centre, radius, Xyz(&hit));
        }
        if (!met)
            continue;

        Coord4 impact = {hit.x, hit.y, hit.z, 0.0f};
        if (object->flags & PhysicsObject::kSimpleBody) {
            // the sphere's surface towards the origin
            Coord4 back;
            VU0_v4sub(&origin, &hit, &back);
            VU0_v4unitxyz(&back, &back);
            VU0_v4scaleadd(&back, radius, &hit, &impact);
        } else {
            // a rigid body: where the ray enters its box, if a thin box along the ray overlaps it
            bool overlaps = false;
            RigidBody *body = fgSimulation.GetRigidBody(object->rigidBodySlot);
            MATRIX4 bodyFrame = body->info->orientation;
            MATRIX4 generated;
            MATRIX4 rayFrame = *Util_GenerateMatrix(&generated, Xyz(&direction));
            Coord4 made;
            Coord4 rayCentre = *MakeCoord4(&made, Xyz(&hit), 1.0f);
            Coord4 rayHalfExtents = kRayHalfExtents;
            Coord4 bodyCentre = *MakeCoord4(&made, &body->position, 1.0f);
            Coord4 halfExtents = body->info->halfExtents;
            OBB bodyBox, rayBox;
            bodyBox.Construct();
            rayBox.Construct();
            bodyBox.Reset(&bodyFrame, &bodyCentre, &halfExtents);
            rayBox.Reset(&rayFrame, &rayCentre, &rayHalfExtents);
            if (rayBox.CheckOBBOverlap(&bodyBox)) {
                overlaps = true;
                // the intersection takes the box's frame with the bottom of the box as its origin
                Coord4 bottom = {bodyCentre.x, bodyCentre.y, bodyCentre.z, 0.0f};
                VU0_v4scaleadd(&body->info->orientation.mtx[1], -halfExtents.y, &bottom, &bottom);
                bodyFrame.mtx[3][0] = bottom.x;
                bodyFrame.mtx[3][1] = bottom.y;
                bodyFrame.mtx[3][2] = bottom.z;
                bodyFrame.mtx[3][3] = 1.0f;
                Coord4 segment[2];
                segment[0] = *MakeCoord4(&made, Xyz(&origin), 1.0f);
                segment[1] = *MakeCoord4(&made, Xyz(&hit), 1.0f);
                Coord4 entry;
                if (fgCollisionMgr->GetOBBObjectIntersection(segment, &bodyFrame, &halfExtents, &entry)) {
                    impact.x = entry.x;
                    impact.y = entry.y;
                    impact.z = entry.z;
                }
            }
            if (!overlaps)
                continue;
        }

        Coord4 ray;
        VU0_v4sub(&hit, &origin, &ray);
        float distance = VU0_v3length(&ray);
        uint32_t sig;
        object->GetSig(&sig);
        ConsiderVictim(sig, Xyz(&hit), Xyz(&impact), distance);
    }
    return numVictims > 0;
}

// FUNC_AT(0x00072790)
bool RayShell::CollideWithWorld(float *distance) {
    WWorldPos place;
    place.Construct();
    Coord4 heading = {direction.x, direction.y, direction.z, 1.0f};
    float limit = range;
    if (numVictims > 0 && victims[0].distance < range)
        limit = victims[0].distance;
    float travelled = 0.0f;
    float step = limit * kStepFraction;
    if (kMaxStep < step)
        step = kMaxStep;
    Coord4 segment[2];
    segment[0] = Coord4{origin.x, origin.y, origin.z, 1.0f};
    Coord4 advance;
    VU0_v4scale4(&heading, step, &advance);
    bool hit = false;
    advance.w = 1.0f;
    while (travelled < limit) {
        VU0_v4add4(&segment[0], &advance, &segment[1]);
        WorldCollisionInfo info = {};   // its segment words zero: the original copies uninitialised stack
        info.point.w = 1.0f;
        if (fgCollisionMgr->CheckHitWorld(segment, &info) > 0) {
            fgCollisionMgr->CheckHitWindow(&info, true, 0);
            float hitDistance = vec3distance(&info.point, &origin);
            if (numVictims == 0 || hitDistance < *distance) {
                *distance = hitDistance;
                float scorchSize = kScorchSize;
                if (shellType == kHeavyShellType)
                    scorchSize = scorchSize * kHeavyScorchScale;
                Shell::ApplyWorldEffects(Xyz(&info.point), &info.normal, 1, shellType, &info.faceType, 0, NULL);
                Shell::ApplyWorldDamage(damage, &place, scorchSize, kScorchTypeRay, &info);
                StaticStoreTracer(&muzzle, Xyz(&info.point), streakKind, muzzleObject, muzzleEffect);
                AddFlash(ownerSig, Xyz(&origin));
                hit = true;
                CheatExplosion(ownerSig, Xyz(&info.point));
            }
        }
        segment[0] = segment[1];
        travelled = step + travelled;
        if (hit)
            break;
    }
    return hit;
}

// FUNC_AT(0x00072ba0)
void RayShell::DamagePhysicsObject(uint32_t sig, const Coord3 *hitPoint, const Coord3 *impactPoint) {
    PhysicsObject *object = FindObject(sig);
    if (object == NULL)
        return;
    int result = object->ApplyDamageVirtual(&origin, hitPoint, damage, kDamageSpread, kDamageKindShell, &ownerSig);
    Coord4 heading = {direction.x, direction.y, direction.z, 1.0f};
    uint8_t noFace[2] = {0, 0};
    Shell::ApplyWorldEffects(impactPoint, &heading, 1, shellType, noFace, result, object);
    StaticStoreTracer(&muzzle, hitPoint, streakKind, muzzleObject, muzzleEffect);
    AddFlash(ownerSig, Xyz(&origin));
    CheatExplosion(ownerSig, hitPoint);
    PhysicsObject *owner = FindObject(ownerSig);
    if (owner == fgSimulation.GetPlayerObject() && object->type == kObjectVehicle)
        static_cast<PVehicle *>(object)->AddDamageByPlayer(damage);
}

// FUNC_AT(0x00072d60)
void RayShell::Fire(float *distance) {
    switch (numVictims) {
    case 0: {
        Coord4 reach = {direction.x, direction.y, direction.z, 0.0f};
        VU0_v4scale(&reach, range, &reach);
        Coord4 end;
        VU0_v3add(&muzzle, &reach, &end);
        StaticStoreTracer(&muzzle, Xyz(&end), streakKind, muzzleObject, muzzleEffect);
        AddFlash(ownerSig, Xyz(&origin));
        *distance = range;
        break;
    }
    case 2: {
        int type = FindObject(victims[0].sig)->type;
        if (type == kObjectVehicle || type == kObjectHelicopter) {
            // through a car to the second victim, at the first's impact point
            DamagePhysicsObject(victims[1].sig, &victims[1].hitPoint, &victims[0].impactPoint);
            *distance = victims[1].distance;
            break;
        }
    }
        // fall through: the nearest
    case 1:
        DamagePhysicsObject(victims[0].sig, &victims[0].hitPoint, &victims[0].impactPoint);
        *distance = victims[0].distance;
        break;
    }
}

// FUNC_AT(0x00072e90)
RayShell* RayShell::Construct(int shellType, uint32_t ownerSig, const Coord3 *origin, const Coord3 *direction,
                              const Coord3 *muzzle, RSceneObj *muzzleObject, const ArticleEffect *muzzleEffect,
                              float range, float damage, int passes, float spread, int streakKind) {
    vtable = kRayShellVtable;
    *Xyz(&this->origin) = *origin;
    this->damage = damage;
    this->range = range < kMaxRange ? range : kMaxRange;
    this->muzzle = *muzzle;
    this->shellType = shellType;
    this->ownerSig = ownerSig;
    victims[0].sig = 0;
    victims[1].sig = 0;
    this->streakKind = streakKind;
    this->muzzleObject = muzzleObject;
    this->muzzleEffect = muzzleEffect;
    PhysicsObject *player = fgSimulation.GetPlayerObject();
    if (FindObject(this->ownerSig)->IsOwnedBy(player) && (Launch.flags & kLaunchFlag1000))
        this->damage = this->damage + this->damage;

    float distance = 0.0f;
    bool hitObject = false;
    bool nearWorld = false;
    for (int pass = 0; pass < passes; pass++) {
        numVictims = 0;
        if (pass == 0) {
            VU0_v4unitxyz(direction, &this->direction);
            startDirection = *Xyz(&this->direction);
            position = *Xyz(&this->origin);
            CollideWithObjects();
            if (numVictims != 0)
                hitObject = true;
        } else {
            // perturbed, from just short of the last hit
            Coord3 turned;
            *Xyz(&this->direction) = *Util_PerturbVector(&turned, reinterpret_cast<const Coord4 *>(&startDirection),
                                                         spread);
            Coord4 back;
            VU0_v4scale(&this->direction, distance - kLaterPassBackOff, &back);
            VU0_v3add(&position, &back, &this->origin);
            this->range = kLaterPassRange < kMaxRange ? kLaterPassRange : kMaxRange;
            if (hitObject)
                CollideWithObjects();
        }
        if (numVictims > 0)
            distance = victims[0].distance - FindObject(victims[0].sig)->GetRadius();
        else
            distance = kNoHitDistance;

        bool hitWorld = false;
        if (pass == 0) {
            hitWorld = CollideWithWorld(&distance);
            if (hitWorld && distance < kNearWorld)
                nearWorld = true;
        } else if (nearWorld) {
            hitWorld = CollideWithWorld(&distance);
        }
        if (!hitWorld)
            Fire(&distance);
        if (pass == 0)
            AddNewRayShellForTriggerCheck(distance);
        if (numVictims == 0 && !hitWorld)
            break;
    }

    typedef RayShell *(RayShell::*DeleteMethod)(unsigned flags);
    (this->*XbeVirtual<DeleteMethod>(this, 0))(1);
    return this;
}

// ---- Shell

// FUNC_AT(0x00074660)
void Shell::Destruct() {
    vtable = kShellVtable;
    PhysicsObject::Destruct();
}

// FUNC_AT(0x00074980)
Shell* Shell::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(Shell));
    return this;
}

// FUNC_AT(0x00074770)
Shell* Shell::Construct(int shellType, const Coord3 *direction, const Coord3 *origin, uint32_t ownerSig,
                        float damage, float explosionSize, int streakKind) {
    PhysicsObject::Construct("weapon", "Shell", kObjectShell, ownerSig, false, false);
    vtable = kShellVtable;
    this->damage = damage;
    this->shellType = shellType;
    this->explosionSize = explosionSize;
    this->streakKind = streakKind;
    Coord4 velocity = {direction->x, direction->y, direction->z, 0.0f};
    float speed;
    if (shellType == kSlowShellType) {
        speed = (float)(RandomScaled(kSpeedVariation) + kSlowShellSpeed);
        stepsLeft = fgSimulation.stepsPerSecond;
    } else {
        speed = (float)(RandomScaled(kSpeedVariation) + kShellSpeed);
        stepsLeft = Ftol(double(fgSimulation.stepsPerSecond) * kShellLifeSeconds);
    }
    if (streakKind > -1)
        speed = BulletStreakKinds[streakKind].speed;
    VU0_v4scale(&velocity, speed, &velocity);
    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    this->origin = *origin;
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleShell, origin, Xyz(&velocity), &kZero3, &orientation, kShellRadius,
                        kShellMass);
    fgSimulation.GetSimpleRigidBody(rigidBodySlot)->flags |=
        SimpleRigidBody::kFlag01 | SimpleRigidBody::kMoves | SimpleRigidBody::kNearSimpleHits |
        SimpleRigidBody::kBoxRigidHits | SimpleRigidBody::kTouchesTriggers;
    return this;
}

// FUNC_AT(0x00074670)
void Shell::ApplyWorldDamage(float damage, WWorldPos *place, float scorchSize, int scorchType,
                             const WorldCollisionInfo *info) {
    if (!info->flag52 && info->faceType != kFaceBrokenWindow && !(info->faceSubType & kFaceSubTypeNoScorch))
        RScorchWorld::ScorchWorld(place, &info->point, &info->normal, scorchSize, scorchType);
    if (info->faceInstance == NULL)
        return;
    MATRIX4 *renderInstance = info->faceInstance->GetRenderInstance();
    if (renderInstance == NULL)
        return;
    RSceneObj *sceneObj = fgWorld->GetSceneObjFromInstance(reinterpret_cast<const CARP::Instance *>(renderInstance));
    if (sceneObj == NULL)
        return;
    if (damage >= 500.0f)
        sceneObj->animHandle->ProcessStimuli(kStimulusDamage500, fgSimulation.stepCount, kStimulusMode);
    if (damage >= 250.0f)
        sceneObj->animHandle->ProcessStimuli(kStimulusDamage250, fgSimulation.stepCount, kStimulusMode);
    if (damage >= 50.0f)
        sceneObj->animHandle->ProcessStimuli(kStimulusDamage50, fgSimulation.stepCount, kStimulusMode);
    if (damage >= 10.0f)
        sceneObj->animHandle->ProcessStimuli(kStimulusDamage10, fgSimulation.stepCount, kStimulusMode);
}

// FUNC_AT(0x000749b0)
void Shell::ApplyWorldEffects(const Coord3 *position, const Coord4 *normal, int, int shellType,
                              const uint8_t *faceType, int damageResult, PhysicsObject *object) {
    // an orientation with the normal as z
    ShellFrame frame;
    frame.row[1] = Coord4{0.0f, 1.0f, 0.0f, 1.0f};
    frame.row[2] = *normal;
    if (v4dotprod(normal, &frame.row[1]) > kStraightUp) {
        frame.row[0] = Coord4{1.0f, 0.0f, 0.0f, 1.0f};
        VU0_v4unitcrossprod1(&frame.row[2], &frame.row[0], &frame.row[1]);
        VU0_v4unitcrossprod1(&frame.row[1], &frame.row[2], &frame.row[0]);
    } else {
        VU0_v4unitcrossprod1(&frame.row[1], &frame.row[2], &frame.row[0]);
        VU0_v4unitcrossprod1(&frame.row[2], &frame.row[0], &frame.row[1]);
    }
    frame.row[3] = kOrigin4;
    Coord4 orientation;
    VU0_m4toquat(&orientation, &frame);

    const char *effect = "bullet";
    if (damageResult == kDamageResultFlesh || *faceType == kFaceFlesh) {
        effect = "Henbult";
    } else if (object == fgSimulation.GetPlayerObject()) {
        effect = PlayerCarSubPhysics() ? NULL : "bullethe";
    } else if (object != NULL && (object->type == kObjectVehicle || object->type == kObjectHelicopter)) {
        effect = "bulletcar";
    } else if (IsSnow(*faceType)) {
        effect = "snowbullethit";
    } else if (*faceType == kFaceSplash) {
        orientation = Coord4{0.0f, 1.0f, 0.0f, 0.0f};
        effect = "bulletsplash";
    } else if (*faceType == kFaceHut) {
        effect = "bulletHut";
    } else if (*faceType == kFaceCar) {
        effect = "bulletcar";
    } else if (object == NULL) {
        if (shellType == kShellTypeRockH)
            effect = "bulletrockH";
        else if (shellType == kShellTypeRock)
            effect = "bulletrock";
        else if (shellType == kShellTypeBullgat)
            effect = "Bullgat";
    }

    // A function-local static in the original
    if (!(ShellSparksGuard & 1)) {
        ShellSparksGuard |= 1;
        ShellSparks = fgParticleSystems->library.FindSystemViaTag(kSparksTag);
    }
    if (ShellSparks != NULL && !IsSnow(*faceType) &&
        (object != fgSimulation.GetPlayerObject() || !PlayerCarSubPhysics()))
        fgParticleSystems->CreateParticles(ShellSparks, position, Xyz(normal), kSparksCount);
    if (effect != NULL)
        GFX::TriggerIncidental(GFX::LookupReference(effect), position, &orientation, &kZero3, &kOrigin4, NULL, 0, NULL);
    AIZoneController::Get()->RegisterZone(kAIZoneImpact, position);

    ABank *bank = fgBanks[kWeaponBankSlot];
    uint8_t material = *faceType & 0xf0;
    uint8_t low = *faceType & 0x0f;
    const char *patch;
    const char *mix;
    if (int(fgSimulation.random->Generate()) * kOneIn65536 < kRicochetChance) {
        patch = kRicochetSounds[ShellRicochetSound];
        if (++ShellRicochetSound >= 3)
            ShellRicochetSound = 0;
        mix = "Bullet Ricochet";
    } else {
        if (material == kSoundMetal90 || material == kSoundMetal10 || low == kLowMetal)
            patch = kMetalSounds[int(fgSimulation.random->Generate()) * 5 / 65536];
        else if (low == kLowWood || material == kSoundWood)
            patch = "SFX_BulletWood";
        else if (low == kLowDirt || material == kSoundDirt)
            patch = "SFX_BulletDirt";
        else if (material == kSoundGlass)
            patch = "SFX_BulletGlass";
        else if (material == kSoundFlesh)
            patch = "SFX_BulletFlesh";
        else
            patch = (ShellConcreteSounds++ & 1) == 0 ? "SFX_BulletConcrete1" : "SFX_BulletConcrete2";
        mix = "Bullet Impact";
    }
    void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    AOneShotSound *sound = NULL;
    if (memory != NULL)
        sound = static_cast<AOneShotSound *>(memory)->ConstructAt(bank->handle, bank->index.Lookup(patch), position, 1.0f, mix,
                                                                  false);
    sound->minDistance = kImpactMinDistance;
    sound->maxDistance = kImpactMaxDistance;
    sound->maxDistanceSq = kImpactMaxDistance * kImpactMaxDistance;
}

// FUNC_AT(0x00074e80)
void Shell::Simulate() {
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    if (--stepsLeft <= 0) {
        fgSimulation.DeletePhysicsObjectThunk(this);
        PhysicsObject::Simulate();
        return;
    }

    // the first body it touched, not its owner
    // read as three 64-bit words, as the original reads them
    const uint64_t *hits = reinterpret_cast<const uint64_t *>(fgSimulation.simpleBodyHits[body->slot].words);
    for (unsigned index = 0; index < kSimulationBodies; index++) {
        PhysicsObject *object = fgSimulation.GetOrderedBody(index);
        if (!((hits[index / 64] >> (index % 64)) & 1) || IsOwnedBy(object))
            continue;
        const Coord3 *at = object->GetPosition();
        int result = object->ApplyDamageVirtual(&origin, &body->position, kHitDamageScale * damage, kDamageSpread,
                                                kDamageKindShell, DamageSourceSig);
        Coord4 objectPosition = {at->x, at->y, at->z, 1.0f};
        const Coord3 *moving = object->GetLinearVelocity();
        Coord4 velocity = {moving->x, moving->y, moving->z, 1.0f};
        uint8_t noFace[2] = {0, 0};
        if (explosionSize > kExplodesAbove) {
            Coord4 axis;
            VU0_ExtractZAxis3FromQuat(&body->orientation, &axis);
            fgSimulation.SpawnExplosion(kMissileExplosion, shellType, Xyz(&axis), &body->position, explosionSize, 0, object,
                                        DamageSourceSig);
            ApplyWorldEffects(Xyz(&objectPosition), &velocity, 2, shellType, noFace, 0, NULL);
        } else {
            ApplyWorldEffects(Xyz(&objectPosition), &velocity, 1, shellType, noFace, result, NULL);
        }
        if (IsOwnedBy(fgSimulation.GetPlayerObject()) && object->type == kObjectVehicle)
            static_cast<PVehicle *>(object)->AddDamageByPlayer(damage);
        PhysicsObject::Simulate();
        fgSimulation.DeletePhysicsObjectThunk(this);
        return;
    }

    // else the world along the step
    Coord4 segment[2];
    segment[0] = Coord4{body->position.x, body->position.y, body->position.z, 1.0f};
    Coord4 step = {body->velocity.x, body->velocity.y, body->velocity.z, 1.0f};
    VU0_v4scale4(&step, fgSimulation.timeStep, &step);
    VU0_v4add4(&segment[0], &step, &segment[1]);
    WorldCollisionInfo info = {};       // its segment words zero: the original copies uninitialised stack
    info.point.w = 1.0f;
    fgCollisionMgr->CheckHitWorld(segment, &info);
    if (info.hitType != kHitNone) {
        fgCollisionMgr->CheckHitWindow(&info, true, 0);
        if (explosionSize > kExplodesAbove) {
            Coord4 axis;
            VU0_ExtractZAxis3FromQuat(&body->orientation, &axis);
            fgSimulation.SpawnExplosion(kMissileExplosion, shellType, Xyz(&axis), Xyz(&info.point), explosionSize, 0, NULL,
                                        DamageSourceSig);
            ApplyWorldEffects(Xyz(&info.point), &info.normal, 2, shellType, &info.faceType, 0, NULL);
        } else {
            ApplyWorldEffects(Xyz(&info.point), &info.normal, 1, shellType, &info.faceType, 0, NULL);
        }
        ApplyWorldDamage(damage, &worldPos, damage * kShellScorchScale, kScorchTypeShell, &info);
        fgSimulation.DeletePhysicsObjectThunk(this);
    }
    PhysicsObject::Simulate();
}
