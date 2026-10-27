#pragma fp_contract(off)

#include "AIPedestrian.h"

#include <bit>
#include <math.h>
#include <string.h>

#include "AIElements.h"
#include "AIRoadSpawn.h"
#include "CharacterSound.h"             // ACharacter
#include "Human.h"
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "../engine/GameLoop.h"      // Launch
#include "../anim/Actor.h"              // ActActor, ActActorDatabase
#include "../anim/Character.h"          // ActCharacter
#include "../anim/Controllers.h"        // AnimationController
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"            // Ftol
#include "../world/Collider.h"
#include "../world/CollisionManager.h"  // fgCollisionMgr
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../world/World.h"             // fgWorld
#include "../world/WorldPos.h"

// XBE_GLOBAL(0x001b6078, 0x310)
PedestrianSet PedSets[kPedestrianSets] = {
    { 16, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, 0, {} },
    { 1, { 16 }, 6, { 1, 2, 3, 4, 5, 6 } },
    { 6, { 16, 17, 18, 19, 20, 21 }, 0, {} },
};
// XBE_GLOBAL(0x001b6388, 0x4)
int32_t InvalidatedPed = -1;
// XBE_GLOBAL(0x001ddb40, 0x39c)
alignas(16) PedestrianStatics Pedestrians;

// ---------------------------------------------------------------------------------------------------------------
// AICharacterPedestrian (0x000239d0-0x00027c80), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

namespace {

// The pedestrians' descriptions (PedestrianSet::descriptions)
const char *const kPedDescriptions[22] = {
    "0.p_mjack.b_mjacka.100", "0.p_mjack.b_mjackb.100", "0.p_mlcot.b_mlcota.100", "0.p_mlcot.b_mlcotb.100",
    "0.p_mleat.b_mleata.100", "0.p_mleat.b_mleatb.100", "0.p_mvest.b_mvesta.100", "0.p_mvest.b_mvestb.100",
    "1.p_fjack.b_fjacka.100", "1.p_fjack.b_fjackb.100", "1.p_fcoat.b_fcoata.100", "1.p_fcoat.b_fcoatb.100",
    "1.p_fpant.b_fpanta.100", "1.p_fpant.b_fpantb.100", "1.p_fskrt.b_fskrta.100", "1.p_fskrt.b_fskrtb.100",
    "1.p_fswet.b_fsweta.100", "1.p_fswet.b_fswetb.100", "0.p_mbord.b_mborda.100", "0.p_mbord.b_mbordb.100",
    "0.p_mswet.b_msweta.100", "0.p_mswet.b_mswetb.100",
};

// The special pedestrians (PedestrianSet::specials, AICharacterPedestrian::specialType); kind 0 is all zeros
const PedestrianSpecial kSpecialPeds[7] = {
    {0, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}, 0, NULL},
    {3, {0, -1, -1, -1, -1, -1, -1, -1}, {0, -1, -1, -1, -1, -1, -1, -1}, 1, "1.p_fjack.b_fjacka.100"},
    {4, {1, -1, -1, -1, -1, -1, -1, -1}, {1, -1, -1, -1, -1, -1, -1, -1}, 1, "1.p_fjack.b_fjackb.100"},
    {5, {2, -1, -1, -1, -1, -1, -1, -1}, {2, -1, -1, -1, -1, -1, -1, -1}, 1, "1.p_fswet.b_fsweta.100"},
    {9, {6, -1, -1, -1, -1, -1, -1, -1}, {6, -1, -1, -1, -1, -1, -1, -1}, 0, "0.p_mbord.b_mborda.100"},
    {10, {7, -1, -1, -1, -1, -1, -1, -1}, {7, -1, -1, -1, -1, -1, -1, -1}, 1, "0.p_mbord.b_mbordb.100"},
    {11, {8, -1, -1, -1, -1, -1, -1, -1}, {8, -1, -1, -1, -1, -1, -1, -1}, 1, "0.p_mswet.b_msweta.100"},
};

// The talking animations GetRandomConvAnim picks from
const int32_t kFemaleTalkAnimations[6] = {6, 7, 0, 1, 2, 3};
const int32_t kMaleTalkAnimations[11] = {28, 29, 11, 12, 13, 14, 15, 16, 17, 18, 19};

// The pedestrians' animations (the female's, then the male's where they differ; names ours, after the states
// that play them)
enum PedestrianAnimation : int32_t {
    kAnimFemaleAvoid = 4,
    kAnimFemaleAvoidEnd = 5,
    kAnimFemaleTurnEnd = 6,
    kAnimStand = 6,                 // Init's
    kAnimFemaleTurnBack = 8,
    kAnimFemaleWalk = 9,
    kAnimFemaleTurn = 10,
    kAnimDodgeLeft = 0x15,
    kAnimDodgeRight = 0x16,
    kAnimFlee = 0x17,
    kAnimLean = 0x18,
    kAnimMaleAvoid = 0x19,
    kAnimStartled = 0x1a,
    kAnimMaleAvoidEnd = 0x1b,
    kAnimMaleTurnEnd = 0x1c,
    kAnimMaleTurnBack = 0x1e,
    kAnimMaleWalk = 0x1f,
    kAnimMaleTurn = 0x20,
};

constexpr int kSpecialAnimBank = 7;         // a special pedestrian's animations (specialType 1-31)
constexpr int kPedAnimBank = 6;
constexpr float kFar = 9999.9f;             // a dead pedestrian's distances
constexpr float kNoThreatDistance = 999.0f;
constexpr float kNoWall = 999.9f;
constexpr float kLeanRange = 3.2f;          // the wall searches' range
constexpr float kColliderRadius = 4.5f;
constexpr float kWallOffsetScale = 0.12987013f;
static_assert(std::bit_cast<uint32_t>(kWallOffsetScale) == 0x3e04fcad, "the original's constant");
constexpr float kAnimSpeedStep = 0.0075f;   // each pedestrian's animations this much faster than the last
constexpr float kAimRangeSquared = 225.0f;
constexpr float kAimTurns = 1.0f / 3.0f;
constexpr float kNeckStep = 3.0f;           // degrees a step
constexpr float kNeckLimit = 80.0f;
constexpr float kNeckDeadZone = 1.5f;
constexpr float kNeckWeightStep = 0.1f;
constexpr int kMaxAimTime = 180;
constexpr float kFaceSlowTurns = 0.048750002f;  // FaceTarget turns at its rate beyond these (modes 1, 2)
constexpr float kFaceFastTurns = 0.081250004f;
static_assert(std::bit_cast<uint32_t>(kFaceSlowTurns) == 0x3d47ae15 &&
              std::bit_cast<uint32_t>(kFaceFastTurns) == 0x3da66667,
              "the original's constants (a bit above 0.04875 and 0.08125)");
constexpr int kSurfaceDrop6 = 6;            // the faces PreventFalling turns back from (FaceTag::type)
constexpr int kSurfaceDrop12 = 12;
constexpr int kVoiceDelayMin = 30;
constexpr int kSpawnRoadEnd = 1;            // special roads start at their segment's end node

Coord4 ToCoord4(const Coord3 &v) {
    Coord4 out;
    out.x = v.x;
    out.y = v.y;
    out.z = v.z;
    out.w = 0.0f;
    return out;
}

// WRoadNetwork's count of the pedestrians on a road, down, as the compiler inlined 0x000239b0
void RoadPedsDown(int road) {
    if (road >= 0 && road < fgRoadNetworkData.roadCount)
        fgRoadNetworkData.roads[road]->pedestrianCount--;
}

// ... and up, as it inlined 0x00023990
void RoadPedsUp(int road) {
    if (road >= 0 && road < fgRoadNetworkData.roadCount)
        fgRoadNetworkData.roads[road]->pedestrianCount++;
}

// AIZoneController's zones are statics reached through its Get(), which the original calls for each use
AIZone &Zone(int zone) {
    AIZoneController::Get();
    return AIZoneData.zones[zone];
}

int ZoneCount() {
    AIZoneController::Get();
    return kZones;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// The pedestrians
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00026800)
void AICharacterPedestrian::Init(int) {
    Pedestrians.made = 0;
    WRoadNetwork *network = WRoadNetwork::Get();
    if (!fgRoadNetworkData.loaded)
        return;

    Pedestrians.pedBase = GetPedBase();
    bool found = false;
#if NIGHTFIRE_DRIVING_DATA_PS2
    int types = fgWorld->attributes.LookupInt("NUM_PED_TYPES_PS2", &found);   // the PS2 release's key
#else
    int types = fgWorld->attributes.LookupInt("NUM_PED_TYPES_XBOX", &found);
#endif
    if (found)
        PedSets[Pedestrians.pedBase].count = types;

    // The special roads: each one's end node, and the point ten along its direction from there
    int segments[kMaxSpecialRoads];
    Pedestrians.specialRoadCount = network->GetSpecialRoads(segments, kMaxSpecialRoads);
    for (int i = 0; i < Pedestrians.specialRoadCount; i++) {
        PedestrianSpecialRoad &road = Pedestrians.specialRoads[i];
        WRoadSegment *segment = fgRoadNetworkData.segments[segments[i]];
        WRoadNode *node = fgRoadNetworkData.nodes[segment->node[kSpawnRoadEnd]];
        road.segment = segments[i];
        Coord4 end = ToCoord4(node->position);
        road.x = end.x;
        road.z = end.z;
        Coord4 ahead = ToCoord4(segment->forward);
        ahead.x = ahead.x * 10.0f;
        ahead.y = ahead.y * 10.0f;
        ahead.z = ahead.z * 10.0f;
        VU0_v3add(&end, &ahead, &ahead);
        road.aheadZ = ahead.z;
        road.aheadX = ahead.x;
        road.ped = -1;
    }

    const PedestrianSet &set = PedSets[Pedestrians.pedBase];
    Pedestrians.count = set.count != 0 || set.specialCount != 0 ? kMaxPedestrians : 0;
    Pedestrians.playerPosition = {0.0f, 0.0f, 0.0f};
    Pedestrians.unknown330 = 0;
    InvalidatedPed = -1;
    Pedestrians.pendingSpecialRoad = -1;
    Pedestrians.nextDescription = 0;
    Pedestrians.spawnPhaseDone = 0;
    Pedestrians.voiceDelay = 0;
    LoadPedTextures();

    // Made far off, then recycled to their spawn points
    MATRIX4 transform;
    VU0_MATRIX4Init(&transform);
    transform.mtx[3][0] = 40000.0f;
    transform.mtx[3][1] = 0.0f;
    transform.mtx[3][2] = 30000.0f;
    transform.mtx[3][3] = 1.0f;
    for (int i = 0; i < Pedestrians.count; i++) {
        void *block = UMemory::FastAlloc(sizeof(AICharacterPedestrian), "AICharacter");
        Pedestrians.peds[i] = block != NULL ? static_cast<AICharacterPedestrian *>(block)->Construct() : NULL;
        AICharacterPedestrian *ped = Pedestrians.peds[i];
        if (ped == NULL)
            continue;
        ped->transform = transform;
        Human *human = static_cast<Human *>(fgSimulation.SpawnHumanObject(
            1, reinterpret_cast<const Coord3 *>(ped->transform.mtx[2]), &ped->Position(), 0));
        if (human == NULL) {
            if (Pedestrians.peds[i] != NULL)
                Pedestrians.peds[i]->AICharacter::Delete(1);
            Pedestrians.peds[i] = NULL;
            continue;
        }
        block = UMemory::FastAlloc(sizeof(WWorldPos), "WWorldPos");
        Pedestrians.peds[i]->worldPos = block != NULL ? static_cast<WWorldPos *>(block)->Construct() : NULL;
        Pedestrians.peds[i]->animSpeed = float(i * double(kAnimSpeedStep) + 1.0);
        const char *description = GetRandomPedTexture();
        Pedestrians.peds[i]->female = uint8_t(description[0]) & 1;
        PointerListNode *handle;
        ActActorDatabase::GetNewActorHandle(&handle, Pedestrians.peds[i], description, NULL,
                                            NULL, &transform, false);
        // The original calls slots 2 (SetActor) here and in Recycle through the vtable: a pedestrian's own
        Pedestrians.peds[i]->SetActor(reinterpret_cast<AICharacterActorLink *>(handle));
        Pedestrians.peds[i]->PlayAnim(kAnimStand, 1.0f);
        Pedestrians.peds[i]->body = human;
        human->SetAICharacter(Pedestrians.peds[i]);
        block = UMemory::FastAlloc(sizeof(WRoadNav), "WRoadNav");
        Pedestrians.peds[i]->nav = block != NULL ? static_cast<WRoadNav *>(block)->Construct() : NULL;
        Pedestrians.peds[i]->nav->mode = kNavSidewalk;
        ped = Pedestrians.peds[i];
        ped->index = i;
        ped->buddy = -1;
        ped->specialRoad = -1;
        ped->closestPed = -1;
        ped->specialType = 0;
        ped->playerDistance = kFar;
        ped->road = -1;
        ped->threat = NULL;
        ped->threatLevel = kThreatNone;
        ped->active = 0;
        ped->nextThreatCheck = (i & 3) + 8;
        ped->nextWallCheck = 0;
        ped->wallResult = 0;
        ped->state = kStateInitial;
        ped->pedFlags = 0;
    }

    for (int i = 0; i < Pedestrians.count; i++) {
        Pedestrians.peds[i]->KillSelf(false);
        Pedestrians.peds[i]->Recycle();
    }
    Pedestrians.spawnPhaseDone = 1;
    Pedestrians.made = 1;
    Pedestrians.pendingSpecialRoad = -1;
}

// The constructor, which Init inlines
AICharacterPedestrian* AICharacterPedestrian::Construct() {
    AICharacter::Construct();
    vtable = kAICharacterPedestrianVtable;
    collider = NULL;
    worldPos = NULL;
    animBank = kPedAnimBank;
    aimTimer = 0;
    aimTime = 0;
    aimTurns = 0.0f;
    neckYaw = 0.0f;
    neckWeight = 0.0f;
    neckWeightStep = 0.0f;
    return this;
}

// FUNC_AT(0x00023fd0)
void AICharacterPedestrian::Destroy() {
    if (!Pedestrians.made)
        return;
    for (int i = 0; i < Pedestrians.count; i++) {
        if (Pedestrians.peds[i] == NULL)
            continue;
        WRoadNav *nav = Pedestrians.peds[i]->nav;
        if (nav != NULL) {
            nav->Destruct();
            UMemory::FastFree(nav, sizeof(WRoadNav));
        }
        WWorldPos *worldPos = Pedestrians.peds[i]->worldPos;
        if (worldPos != NULL) {
            NullFunction();     // WWorldPos's destructor, the empty function
            UMemory::FastFree(worldPos, sizeof(WWorldPos));
        }
        Pedestrians.peds[i] = NULL;
    }
}

// FUNC_AT(0x00026ed0)
void AICharacterPedestrian::Manage() {
    if (!Pedestrians.made)
        return;
    if (Pedestrians.voiceDelay > 0)
        Pedestrians.voiceDelay--;
    else
        Pedestrians.voiceDelay = 0;
    Pedestrians.playerPosition = *fgSimulation.cars.first[0]->GetPosition();

    // A special road the player nears (between 130 and 250 away) gets the next recycled pedestrian; a special one
    // the player has left (beyond 250) is killed
    for (int i = 0; i < Pedestrians.specialRoadCount; i++) {
        PedestrianSpecialRoad &road = Pedestrians.specialRoads[i];
        Coord4 point = {road.x, 0.0f, road.z, 0.0f};
        if (road.ped == -1) {
            if (Pedestrians.pendingSpecialRoad != -1)
                continue;
            float distance = VU0_v3distancesquarexz(&Pedestrians.playerPosition, &point);
            if (distance > 16875.0f && distance < 62500.0f) {
                InvalidateFurthestPed(-1);
                Pedestrians.pendingSpecialRoad = i;
                break;
            }
        } else {
            if (VU0_v3distancesquarexz(&Pedestrians.playerPosition, &point) > 62500.0f && road.ped != -1) {
                Pedestrians.peds[road.ped]->KillSelf(true);
                road.ped = -1;
            }
        }
    }

    // At most one dead pedestrian recycled a step
    for (int i = 0; i < Pedestrians.count; i++) {
        AICharacterPedestrian *ped = Pedestrians.peds[i];
        ped->stateTimer++;
        ped->animTimer++;
        ped->previousPlayerDistance = ped->playerDistance;
        ped->playerDistance = VU0_v3distancexz(&Pedestrians.playerPosition, &ped->Position());
        ped->UpdateClosestPed(false);
        if (ped->state == kStateDead && ped->Recycle())
            return;
    }
}

// FUNC_AT(0x000239d0)
int AICharacterPedestrian::GetPedBase() {
    if (strstr(Launch.missionName, "snow1a") != NULL)
        return 1;
    if (strstr(Launch.missionName, "snow2a") != NULL)
        return 2;
    if (strstr(Launch.missionName, "junglea") != NULL)
        return 3;
    if (strstr(Launch.missionName, "jungleb") != NULL)
        return 4;
    if (strstr(Launch.missionName, "uw_") != NULL)
        return 5;
    if (strstr(Launch.missionName, "junglec") != NULL)
        return 6;
    return 0;
}

// FUNC_AT(0x00024060)
void AICharacterPedestrian::LoadPedTextures() {
    const PedestrianSet &set = PedSets[Pedestrians.pedBase];
    for (int i = 0; i < set.count; i++)
        ActCharacter::LoadCharacter(kPedDescriptions[set.descriptions[i]], NULL, NULL);
    if (set.specialCount > 0) {
        for (int i = 0; i < set.specialCount; i++)
            ActCharacter::LoadCharacter(kSpecialPeds[set.specials[i]].description, NULL, NULL);
    }
}

// FUNC_AT(0x000240e0)
const char* AICharacterPedestrian::GetRandomPedTexture() {
    const PedestrianSet &set = PedSets[Pedestrians.pedBase];
    int turn = Pedestrians.nextDescription++;
    if (set.count == 0) {
        if (Pedestrians.nextDescription == set.specialCount)
            Pedestrians.nextDescription = 0;
        return kSpecialPeds[set.specials[turn]].description;
    }
    if (Pedestrians.nextDescription == set.count)
        Pedestrians.nextDescription = 0;
    return kPedDescriptions[set.descriptions[turn]];
}

// FUNC_AT(0x00023a90)
int AICharacterPedestrian::GetRandomConvAnim(bool female) {
    if (female)
        return kFemaleTalkAnimations[int(fgSimulation.random->Generate()) * 6 >> 16];
    return kMaleTalkAnimations[int(fgSimulation.random->Generate()) * 11 >> 16];
}

// FUNC_AT(0x00023ad0)
bool AICharacterPedestrian::PedInRange(const Coord3 *point, float range, bool includePaired) {
    Coord4 from = ToCoord4(*point);
    for (int i = 0; i < Pedestrians.count; i++) {
        AICharacterPedestrian *ped = Pedestrians.peds[i];
        bool counts = true;
        if ((ped->pedFlags & (kPedPaired | kPedSpecial)) && !includePaired)
            counts = false;
        if (ped->state == kStateDead || !counts)
            continue;
        Coord4 position = ToCoord4(ped->Position());
        if (VU0_v3distancexz(&from, &position) < range)
            return true;
    }
    return false;
}

// FUNC_AT(0x00023bc0)
double AICharacterPedestrian::GetDegFromAng(float turns, bool buckets) {
    double degrees = turns * 360.0;
    if (!buckets)
        return degrees;
    if (!(degrees < -22.5) && degrees <= 22.5)
        return 0.0;
    if (!(degrees < 22.5) && degrees <= 67.5)
        return 45.0;
    if (!(degrees < 67.5) && degrees <= 112.5)
        return 90.0;
    if (!(degrees < 112.5) && degrees <= 157.5)
        return 135.0;
    if (!(degrees < -67.5) && degrees <= -22.5)
        return -45.0;
    if (!(degrees < -112.5) && degrees <= -67.5)
        return -90.0;
    if (!(degrees < -157.5) && degrees <= -112.5)
        return -135.0;
    return 180.0;
}

// FUNC_AT(0x00024730)
void AICharacterPedestrian::ForceRecycle(int index) {
    Pedestrians.peds[index]->KillSelf(false);
}

// FUNC_AT(0x00025430)
int AICharacterPedestrian::InvalidateFurthestPed(int except) {
    float furthest = 0.0f;
    int found = -1;
    int invalidated = InvalidatedPed;
    for (int i = 0; i < Pedestrians.count; i++) {
        AICharacterPedestrian *ped = Pedestrians.peds[i];
        if (ped->state == kStateDead) {
            found = i;
            break;
        }
        if (i != except && ped->buddy == -1 && i != invalidated && !(ped->pedFlags & kPedSpecial) &&
            furthest < ped->playerDistance) {
            furthest = ped->playerDistance;
            found = i;
        }
    }
    if (found != -1)
        ForceRecycle(found);
    return found;
}

// ---------------------------------------------------------------------------------------------------------------
// One pedestrian
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00023f50)
void AICharacterPedestrian::Destruct() {
    vtable = kAICharacterPedestrianVtable;
    if (collider != NULL) {
        collider->Destruct();
        UMemory::FastFree(collider, sizeof(WCollider));
        collider = NULL;
    }
    AICharacter::Destruct();
}

// FUNC_AT(0x00024700)
AICharacterPedestrian* AICharacterPedestrian::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AICharacterPedestrian));
    return this;
}

void AICharacterPedestrian::SetState(int newState) {
    previousState = state;
    state = newState;
    subState = 0;
    stateTimer = 0;
}

// The pedestrians' shared voice: a line, then none from any of them for a while
void AICharacterPedestrian::Speak() {
    Pedestrians.voiceDelay =
        uint8_t((int(fgSimulation.random->Generate()) * fgSimulation.stepsPerSecond >> 16) + kVoiceDelayMin);
    GetACharacter()->Say(female ? "FPed" : "MPed");
}

// FUNC_AT(0x00024140)
void AICharacterPedestrian::KillSelf(bool force) {
    WRoadNetwork *network = WRoadNetwork::Get();
    if (index == InvalidatedPed)
        return;
    if ((pedFlags & kPedSpecial) && !force)
        return;
    if (state == kStateDead)
        return;
    previousState = state;
    previousPlayerDistance = kFar;
    playerDistance = kFar;
    state = kStateDead;
    subState = 0;
    stateTimer = 0;
    if (road != -1) {
        RoadPedsDown(road);
        road = -1;
    }
    if ((pedFlags & kPedPaired) && buddy != -1) {
        AICharacterPedestrian *partner = Pedestrians.peds[buddy];
        partner->previousState = partner->state;
        partner->state = kStateDead;
        partner->subState = 0;
        partner->stateTimer = 0;
        partner->buddy = -1;
        partner->pedFlags = 0;
        if (partner->road != -1) {
            network->RemoveRoadPedestrian(partner->road);
            partner->road = -1;
        }
        buddy = -1;
    }
    pedFlags = 0;
}

// FUNC_AT(0x000254c0)
bool AICharacterPedestrian::Recycle() {
    Coord4 position = {0.0f, 0.0f, 0.0f, 0.0f};
    Coord4 target = {0.0f, 0.0f, 0.0f, 0.0f};
    Coord4 home = {0.0f, 0.0f, 0.0f, 0.0f};
    AIRoadSpawn *spawn = AIRoadSpawn::Get();
    int16_t segmentIndex = -1;
    int8_t lane = 0;
    float along = 0.0f;
    WRoadNetwork::Get();
    bool paired = false;
    const PedestrianSpecial *special = NULL;
    if (index == InvalidatedPed)
        return false;

    // The segment: a road spawn's, the pending special road's, or the invalidated pedestrian's
    if (InvalidatedPed == -1 && Pedestrians.pendingSpecialRoad == -1) {
        if (!spawn->GetRandomPedSpawn(&segmentIndex, &lane, &along, true))
            return false;
    } else if (Pedestrians.pendingSpecialRoad != -1) {
        segmentIndex = int16_t(Pedestrians.specialRoads[Pedestrians.pendingSpecialRoad].segment);
    } else {
        segmentIndex = Pedestrians.spawnSegment;
    }

    if (road != -1)
        RoadPedsDown(road);
    WRoadSegment *segment = fgRoadNetworkData.segments[segmentIndex];
    road = segment->road;
    if (road != -1)
        RoadPedsUp(road);
    if (specialRoad != -1) {
        Pedestrians.specialRoads[specialRoad].ped = -1;
        specialRoad = -1;
    }
    if ((segment->flags & kSegmentSpecial) && segment->unknown59 != 0) {
        specialType = segment->unknown59;
        special = &kSpecialPeds[specialType];
    }

    float homeY = home.y;
    if (InvalidatedPed != -1) {
        // Beside the invalidated pedestrian's spawn, facing it, and talking with it
        position = ToCoord4(Pedestrians.spawnPosition);
        target = ToCoord4(Pedestrians.spawnTarget);
        home.x = Pedestrians.spawnPosition.x;
        homeY = Pedestrians.spawnPosition.y;
        home.z = Pedestrians.spawnPosition.z;
        paired = true;
        VU0_v4scale(&target, -1.0f, &target);
        Coord4 back = ToCoord4(segment->forward);
        back.x = back.x * -0.75f;
        back.z = back.z * -0.75f;
        back.y = 0.0f;
        VU0_v3add(&position, &back, &position);
        Pedestrians.peds[InvalidatedPed]->buddy = index;
        Pedestrians.peds[index]->buddy = InvalidatedPed;
        InvalidatedPed = -1;
    } else if (Pedestrians.spawnPhaseDone && (segment->flags & kSegmentSpecial)) {
        // At the special segment's end node; an ordinary pedestrian there invalidates another to talk with
        paired = true;
        if (special == NULL) {
            InvalidatedPed = index;
            InvalidateFurthestPed(index);
        }
        segment = fgRoadNetworkData.segments[segmentIndex];
        WRoadNetwork::Get();
        WRoadNode *node = fgRoadNetworkData.nodes[segment->node[kSpawnRoadEnd]];
        WRoadNetwork::Get();
        position.x = node->position.x;
        position.y = node->position.y;
        position.z = node->position.z;
        target = {0.0f, 0.0f, 0.0f, target.w};
        if (special == NULL) {
            Pedestrians.spawnTarget = {0.0f, 0.0f, 0.0f};
            Pedestrians.spawnPosition.x = position.x;
            Pedestrians.spawnSegment = segmentIndex;
            home.x = position.x;
            Pedestrians.spawnPosition.y = position.y;
            Pedestrians.spawnPosition.z = position.z;
            homeY = position.y;
            home.z = position.z;
            Coord4 ahead = ToCoord4(segment->forward);
            ahead.x = ahead.x * 0.75f;
            ahead.z = ahead.z * 0.75f;
            ahead.y = 0.0f;
            VU0_v3add(&position, &ahead, &position);
        }
    } else {
        // On the road spawn's pavement
        nav->InitAtSegment(segmentIndex, lane, along, true);
        position = ToCoord4(nav->roadPoint);
        position.y = float(position.y + 0.5);
        target = ToCoord4(nav->direction);
        if (special == NULL && !paired && !ForceNearRespawn && PedInRange(Xyz(&position), 15.0f, false)) {
            KillSelf(false);
            return false;
        }
    }

    if (!spawn->PedRespawnPosCheck(Xyz(&position))) {
        KillSelf(false);
        return false;
    }

    MATRIX4 transform;
    VU0_MATRIX4Init(&transform);
    transform.mtx[3][0] = position.x;
    transform.mtx[3][1] = position.y;
    transform.mtx[3][2] = position.z;
    transform.mtx[3][3] = 1.0f;
    if (paired) {
        homeTarget = *Xyz(&position);
        nextTarget = {home.x, homeY, home.z};
    }
    const char *description = special != NULL ? special->description : GetRandomPedTexture();
    female = uint8_t(description[0]) & 1;
    if (actorLink != NULL) {
        ActActorDatabase::KillActorByHandle(reinterpret_cast<PointerListNode *>(actorLink));
        actorLink = NULL;
    }
    PointerListNode *handle;
    ActActorDatabase::GetNewActorHandle(&handle, this, description, NULL, NULL, &transform,
                                        false);
    SetActor(reinterpret_cast<AICharacterActorLink *>(handle));
    UpdateRotPos();

    if (paired) {
        if (special != NULL) {
            int pending = Pedestrians.pendingSpecialRoad;
            Pedestrians.specialRoads[pending].ped = index;
            specialRoad = pending;
            specialType = segment->unknown59;
            SetState(kStateNeutral);
            PlayAnim(special->idleAnimation, 1.0f);
            home.x = Pedestrians.specialRoads[pending].aheadX;
            home.z = Pedestrians.specialRoads[pending].aheadZ;
            homeY = 0.0f;
            Pedestrians.pendingSpecialRoad = -1;
        } else {
            SetState(kStateIdling);
            PlayAnim(GetRandomConvAnim(female != 0), 1.0f);
        }
        this->target = {home.x, homeY, home.z};
    } else {
        SetState(kStateWalking);
        PlayAnim(female ? kAnimFemaleWalk : kAnimMaleWalk, 1.0f);
    }

    aimTarget = {0.0f, 0.0f, 0.0f};
    threat = NULL;
    threatLevel = kThreatNone;
    turnTowards = -1;
    lastWallRange = 0.0f;
    aiming = 0;
    if (special != NULL) {
        pedFlags = kPedSpecial;
        active = 1;
        return true;
    }
    pedFlags = paired ? kPedPaired : 0;
    active = 1;
    return true;
}

// FUNC_AT(0x00023ba0)
void AICharacterPedestrian::SetTarget(const Coord3 *point) {
    target = *point;
}

// FUNC_AT(0x00023ce0)
int AICharacterPedestrian::UpdateClosestPed(bool facingOnly) {
    int myRoad = road;
    Coord4 position = ToCoord4(Position());
    previousClosestPedDist = closestPedDist;
    previousClosestPed = closestPed;
    int closest = -1;
    float closestDist = kFar;
    Coord4 other;
    if (active) {
        for (int i = 0; i < Pedestrians.count; i++) {
            if (i == index)
                continue;
            AICharacterPedestrian *ped = Pedestrians.peds[i];
            if (!ped->active || (ped->road != myRoad && ped->road != -1))
                continue;
            other = ToCoord4(ped->Position());
            Coord4 offset;
            VU0_v4sub(&other, &position, &offset);
            float distance = VU0_v3length(&offset);
            if (!(distance < closestDist))
                continue;
            if (facingOnly && !(v3dotprod(transform.mtx[2], &offset) > 0.0f))
                continue;
            closestDist = distance;
            closest = i;
        }
        if (closest != -1) {
            other = ToCoord4(Pedestrians.peds[closest]->Position());
            closestDist = vec3distance(&position, &other);
        }
    }
    closestPed = closest;
    closestPedDist = closestDist;
    return closest;
}

// FUNC_AT(0x00023e60)
void AICharacterPedestrian::UpdateThreatRelativeDirection(const Coord3 *point, float *direction, float *angle) {
    double relative = AtanTurns(point->x - transform.mtx[3][0], point->z - transform.mtx[3][2]) - heading;
    float turns = float(relative);
    if (relative < -0.5)
        turns = float(turns + 1.0);
    if (turns > 0.5f)
        turns = float(turns - 1.0);
    *direction = float(GetDegFromAng(turns, true));
    *angle = float(turns * 360.0);
    // The answer is not used
    Coord4 forward;
    fgSimulation.GetSimpleRigidBody(body->rigidBodySlot)->GetForwardVector(Xyz(&forward));
}

// FUNC_AT(0x000253b0)
int AICharacterPedestrian::PlayAnim(int animation, float speed) {
    int bank = animBank;
    if (specialType > 0 && specialType < 32)
        bank = kSpecialAnimBank;
    this->animation = animation;
    playAnimController = actorLink->actor->SetNewAnimation(bank, animation);
    crossFade = NULL;
    int frames = Ftol(playAnimController->GetTotalFrames());
    actorLink->actor->SetTimeScale(float(speed * double(animSpeed)));
    animTimer = 0;
    return frames;
}

// FUNC_AT(0x00024db0)
double AICharacterPedestrian::UpdateAnimSpeed() {
    float distance = threatDistance;
    float scale = 1.0f;
    if (threatLevel != kThreatNone && threatLevel != kThreat3 && distance < 15.0f) {
        if (distance < 2.0f)
            distance = 2.0f;
        scale = float((15.0 - distance) * 0.19f + 1.0);
    }
    actorLink->actor->SetTimeScale(float(scale * double(animSpeed)));
    animSpeedScale = scale;
    return scale;
}

// FUNC_AT(0x00024c30)
bool AICharacterPedestrian::FaceTarget(int mode) {
    float scale = 1.0f;
    bool done = false;
    float bearing = float(AtanTurns(target.x - transform.mtx[3][0], target.z - transform.mtx[3][2]));
    double relative = bearing - AtanTurns(transform.mtx[2][0], transform.mtx[2][2]);
    float turns = float(relative);
    if (relative < -0.5)
        turns = float(turns + 1.0);
    if (turns > 0.5f)
        turns = float(turns - 1.0);
    if (turns > -0.01f && turns < 0.01f) {
        actorLink->actor->CurrentPositionRotateY(turns);
        UpdateRotPos();
        return true;
    }

    // Modes 1 and 2 turn at a rate beyond a small angle; mode 3 turns at once and answers true
    double rate = scale;
    if (mode == 1) {
        if (turns < -kFaceSlowTurns || turns > kFaceSlowTurns)
            rate = fgSimulation.timeStep * 7.5;
    } else if (mode == 2) {
        if (turns < -kFaceFastTurns || turns > kFaceFastTurns)
            rate = fgSimulation.timeStep * 12.5;
    } else if (mode == 3) {
        done = true;
    }
    actorLink->actor->CurrentPositionRotateY(float(turns * rate));
    UpdateRotPos();
    return done;
}

// FUNC_AT(0x00024e50)
bool AICharacterPedestrian::ReachedTarget(bool moveOrigin) {
    Coord4 position = ToCoord4(Position());
    Coord4 goal = ToCoord4(target);
    if (VU0_v3distancexz(&position, &goal) < 1.0f) {
        if (moveOrigin)
            actorLink->actor->ChangeAnimationOrigin(&target);
        return true;
    }
    return false;
}

// FUNC_AT(0x00024ee0)
PhysicsObject* AICharacterPedestrian::FindClosestCar(float range) {
    PhysicsObject *closest = NULL;
    float closestDistance = range;
    for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
        Coord4 carPosition = ToCoord4(*(*car)->GetPosition());
        Coord4 position = ToCoord4(Position());
        // Squared, against the range as given
        float distance = VU0_v3distancesquarexz(&position, &carPosition);
        if (distance < closestDistance) {
            closest = *car;
            closestDistance = distance;
        }
    }
    return closest;
}

// FUNC_AT(0x00024280)
int AICharacterPedestrian::CheckVehicleThreat(PhysicsObject *car, bool checkClass) {
    Coord4 carPosition = ToCoord4(fgSimulation.GetRigidBody(car->rigidBodySlot)->position);
    if (checkClass && static_cast<PVehicle *>(car)->GetCarClass() == 3 &&
        !(fgSimulation.GetRigidBody(car->rigidBodySlot)->flags & RigidBody::kFlag0))
        return kThreatNone;
    if (fabs(double(transform.mtx[3][1]) - carPosition.y) > 5.0)
        return kThreatNone;
    Coord4 offset;
    VU0_v4sub(&Position(), &carPosition, &offset);
    threatDistance = VU0_v3length(&offset);
    if (threatDistance > 35.0f)
        return kThreatNone;

    float speed = static_cast<PVehicle *>(car)->GetCarSpeed();
    float side = v3dotprod(&fgSimulation.GetRigidBody(car->rigidBodySlot)->info->orientation.mtx[0], &offset);
    float along = v3dotprod(&fgSimulation.GetRigidBody(car->rigidBodySlot)->info->orientation.mtx[2], &offset);
    RigidBodyInfo *info = fgSimulation.GetRigidBody(car->rigidBodySlot)->info;
    // The car's half width, and more alongside its front and back, the nearer its middle
    double widen = 0.0;
    float absAlong = float(fabs(double(along)));
    if (absAlong < 15.0f) {
        widen = (15.0 - absAlong) * double(0.175f);
        if (!(widen <= 2.5))
            widen = 2.5;
    }
    double reach = info->halfExtents.x + widen;
    if ((-reach > side || side > reach) && speed > 1.0f)
        return kThreatNone;

    float stopReach = float(speed * double(1.1f) + 2.5);
    UpdateThreatRelativeDirection(Xyz(&carPosition), &threatDirection, &threatAngle);
    threatPosition = *Xyz(&carPosition);
    threatForward = *reinterpret_cast<const Coord3 *>(
        fgSimulation.GetRigidBody(car->rigidBodySlot)->info->orientation.mtx[2]);
    threatRight = *reinterpret_cast<const Coord3 *>(
        fgSimulation.GetRigidBody(car->rigidBodySlot)->info->orientation.mtx[0]);
    RigidBody *body = fgSimulation.GetRigidBody(car->rigidBodySlot);
    if (threatDistance > 2.5f && body->info->halfExtents.z + 2.0 > threatDistance && speed < 1.0f &&
        threatDirection == 0.0f)
        return kThreat3;
    if (threatDistance > stopReach)
        return kThreatNone;
    threatOnRight = side < 0.0f ? 0 : 1;

    // Whether the car is moving at the pedestrian, forwards or backwards
    Coord4 heading;
    VU0_v4unitxyz(&fgSimulation.GetRigidBody(car->rigidBodySlot)->velocity, &heading);
    float moving = v3dotprod(&fgSimulation.GetRigidBody(car->rigidBodySlot)->info->orientation.mtx[2], &heading);
    Coord4 toPed;
    VU0_v4sub(&Position(), &carPosition, &toPed);
    VU0_v4unitxyz(&toPed, &toPed);
    float facing = v3dotprod(&fgSimulation.GetRigidBody(car->rigidBodySlot)->info->orientation.mtx[2], &toPed);
    bool coming;
    if (moving > 0.1f && facing > 0.707f && along > 0.0f) {
        coming = true;
    } else if (moving < -0.1f && facing < -0.707f && along < 0.0f) {
        threatForward.x = threatForward.x * -1.0f;
        threatForward.y = threatForward.y * -1.0f;
        threatForward.z = threatForward.z * -1.0f;
        coming = true;
    } else {
        coming = false;
    }
    if (coming) {
        if (threatDistance < 4.5f)
            return kThreat2;
        if (threatDistance < stopReach)
            return kThreat1;
        return kThreatNone;
    }
    if (stopReach * 0.5 > threatDistance)
        return kThreat1;
    return kThreatNone;
}

// FUNC_AT(0x00026760)
int AICharacterPedestrian::ThreatenedByVehicle() {
    if (fgSimulation.stepCount >= nextThreatCheck && playerDistance < 100.0f) {
        float distance = threatDistance;
        nextThreatCheck = fgSimulation.stepCount + 4;
        previousThreat = threat;
        threatLevel = kThreatNone;
        lastThreatDistance = distance;
        threat = FindClosestCar(1000.0f);
        if (threat != NULL) {
            threatLevel = CheckVehicleThreat(threat, true);
            if (threatLevel == kThreatNone) {
                threat = NULL;
                threatDistance = kNoThreatDistance;
            }
        }
    }
    UpdateAnimSpeed();
    return threatLevel;
}

// FUNC_AT(0x00024f80)
int AICharacterPedestrian::FindPointAgainstWall(float range) {
    Coord4 position = ToCoord4(Position());
    bool found = false;
    float nearest = kNoWall;
    if (!(fgSimulation.stepCount > nextWallCheck) && range == lastWallRange)
        return wallResult;

    position.y = float(position.y - 1.0);
    nextWallCheck = fgSimulation.stepCount + (index & 31) + fgSimulation.stepsPerSecond / 2;
    lastWallRange = range;
    if (collider == NULL) {
        void *block = UMemory::FastAlloc(sizeof(WCollider), "WCollider");
        collider = block != NULL ? static_cast<WCollider *>(block)->Construct(Xyz(&position), kColliderRadius, false,
                                                                              WCollider::kCollideBarriers) : NULL;
    } else if (!collider->InRegion(Xyz(&position), range, WCollider::kCollideBarriers)) {
        // A new one, made with no barriers masked
        if (collider != NULL) {
            collider->Destruct();
            UMemory::FastFree(collider, sizeof(WCollider));
        }
        fgCollisionMgr->barrierMask = 0;
        void *block = UMemory::FastAlloc(sizeof(WCollider), "WCollider");
        collider = block != NULL ? static_cast<WCollider *>(block)->Construct(Xyz(&position), kColliderRadius, false,
                                                                              WCollider::kCollideBarriers) : NULL;
        fgCollisionMgr->barrierMask = 0x10;
    }
    position.y = float(position.y + 1.0);

    BarrierList &barriers = collider->barriers;
    if (barriers.first == NULL || barriers.Count() == 0) {
        wallResult = 0;
        return wallResult;
    }
    for (BarrierListEntry *entry = barriers.first; entry != barriers.last; entry++) {
        const CollisionBarrier &barrier = entry->barrier;
        Coord4 start = {barrier.x0, barrier.y0, barrier.z0, 0.0f};
        Coord4 end = {barrier.x1, barrier.y1, barrier.z1, 0.0f};
        // Within 0.75 of the pedestrian's height at one end at least, and not level
        double low = position.y - 0.75;
        if (start.y < low && end.y < low)
            continue;
        double high = position.y + 0.75;
        if (start.y > high && end.y > high)
            continue;
        if (fabs(double(end.y) - start.y) < 0.25)
            continue;
        if (start.y < end.y)
            end.y = start.y;
        else if (start.y > end.y)
            start.y = end.y;

        // The pedestrian's projection on the wall, across the ground
        float dot = float((double(position.z) - start.z) * (double(end.z) - start.z) +
                          (double(position.x) - start.x) * (double(end.x) - start.x));
        Coord4 wall;
        VU0_v4sub(&end, &start, &wall);
        double length = VU0_v3length(&wall);
        float t = float(dot / (length * length));
        if (t < 0.0f || t > 1.0f)
            continue;
        Coord4 point;
        point.x = float((double(end.x) - start.x) * t + start.x);
        point.y = start.y;
        point.z = float((double(end.z) - start.z) * t + start.z);
        Coord4 offset;
        VU0_v4sub(&point, &position, &offset);
        float distance = VU0_v3length(&offset);
        if (distance < nearest) {
            nearest = distance;
            wallPoint = *Xyz(&point);
            wallOffset = *Xyz(&offset);
            wallOffset.x = wallOffset.x * kWallOffsetScale;
            wallOffset.y = 0.0f;
            found = true;
            wallResult = 1;
            wallOffset.z = wallOffset.z * kWallOffsetScale;
        }
    }
    if (!found)
        wallResult = 0;
    return wallResult;
}

// FUNC_AT(0x00025d10)
void AICharacterPedestrian::PreventFalling() {
    Coord4 ahead = ToCoord4(*reinterpret_cast<const Coord3 *>(transform.mtx[2]));
    Coord4 position = ToCoord4(Position());
    ahead.x = ahead.x * 5.5f;
    ahead.z = ahead.z * 5.5f;
    Coord4 probe;
    VU0_v3add(&position, &ahead, &probe);
    worldPos->FindClosestFace(Xyz(&probe), true);
    int surface = worldPos->face.corner[2].tag.type;
    if (surface == kSurfaceDrop12 || surface == kSurfaceDrop6) {
        ahead.x = ahead.x * -10.0f;
        ahead.z = ahead.z * -10.0f;
        VU0_v3add(&position, &ahead, &probe);
        target = *Xyz(&probe);
        FaceTarget(3);
    }
}

// FUNC_AT(0x00025e00)
void AICharacterPedestrian::SetNewTargetBasedOnThreat(int threat) {
    double distance = 1000.0;
    Coord4 base = ToCoord4(threatPosition);
    Coord4 away = ToCoord4(threatForward);
    if (threat == kThreat2 || threat == kThreat3)
        away = ToCoord4(threatRight);
    if (threat == kThreatNone || threat == kThreat2)
        distance = -1000.0;
    away.x = float(away.x * distance);
    away.z = float(away.z * distance);
    Coord4 goal;
    VU0_v3add(&base, &away, &goal);
    PlayAnim(kAnimFlee, 1.0f);
    target = *Xyz(&goal);
    FaceTarget(3);
}

// FUNC_AT(0x00026620)
void AICharacterPedestrian::MaintainHeading(int mode) {
    if (active && state != kStateDead) {
        if (!(pedFlags & (kPedPaired | kPedSpecial))) {
            Coord4 navPoint = ToCoord4(nav->roadPoint);
            Coord4 position = ToCoord4(Position());
            WRoadNetwork *network = WRoadNetwork::Get();
            float distance = VU0_v3distancexz(&position, &navPoint);
            Coord4 heading = ToCoord4(nav->roadPoint);
            target = nav->roadPoint;
            // Keep the navigator 2.5 ahead
            if (distance < 2.5f)
                nav->IncNavPosition(float(2.5 - distance), Xyz(&heading), -1);
            int newRoad = fgRoadNetworkData.segments[nav->segment]->road;
            if (newRoad != road) {
                if (road != -1)
                    network->RemoveRoadPedestrian(road);
                if (newRoad != -1)
                    network->AddRoadPedestrian(newRoad);
                road = newRoad;
            }
        }
        FaceTarget(mode);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Virtual
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000248b0)
void AICharacterPedestrian::SetActor(AICharacterActorLink *actorLink) {
    this->actorLink = actorLink;
    int bone = actorLink->actor->GetBoneIndex("Root.Neck");
    if (bone != -1)
        actorLink->actor->CreateGlobalPoseOverrides(1, &bone, true);
}

// FUNC_AT(0x00024870)
void AICharacterPedestrian::UpdateRotPos() {
    if (state == kStateDead)
        return;
    actorLink->actor->GetActorLocalPosOri(&transform);
    double turns = AtanTurns(transform.mtx[2][0], transform.mtx[2][2]);
    heading = float(turns);
    headingDegrees = float(turns * 360.0);
}

// FUNC_AT(0x00024b30)
bool AICharacterPedestrian::TargetIsInRange() {
    const Coord3 *aim =
        aiming ? &aimTarget : &fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->position;
    Coord4 goal = ToCoord4(*aim);
    if (VU0_v3distancesquare(&goal, &Position()) > kAimRangeSquared)
        return false;
    float bearing = float(AtanTurns(goal.x - transform.mtx[3][0], goal.z - transform.mtx[3][2]));
    double turns = bearing - AtanTurns(transform.mtx[2][0], transform.mtx[2][2]);
    if (turns < -0.5)
        turns += 1.0;
    if (turns > 0.5)
        turns -= 1.0;
    aimTurns = float(turns);
    return -kAimTurns < turns && turns < kAimTurns;
}

// FUNC_AT(0x00024910)
void AICharacterPedestrian::DoAiming() {
    // The neck turns towards the target 3 degrees a step, within 80 either way...
    if (TargetIsInRange()) {
        double wanted = aimTurns * -360.0;
        double difference = neckYaw - wanted;
        if (difference < 0.0)
            difference = -difference;
        if (difference > kNeckDeadZone) {
            if (wanted < neckYaw)
                neckYaw = float(neckYaw - kNeckStep);
            else
                neckYaw = float(neckYaw + kNeckStep);
        }
        if (neckYaw < -kNeckLimit)
            neckYaw = -kNeckLimit;
        if (neckYaw > kNeckLimit)
            neckYaw = kNeckLimit;
    }

    // ... and its override fades in and out, holding for a random time either way
    double weight = double(neckWeight) + neckWeightStep;
    neckWeight = float(weight);
    if (weight < 0.0) {
        neckWeight = 0.0f;
        neckWeightStep = 0.0f;
    }
    if (neckWeight > 1.0f) {
        neckWeight = 1.0f;
        neckWeightStep = 0.0f;
    }
    if (TargetIsInRange()) {
        if (neckWeight <= 0.0f && ++aimTimer > aimTime) {
            aimTimer = 0;
            aimTime = int(fgSimulation.random->Generate()) * kMaxAimTime >> 16;
            neckWeightStep = kNeckWeightStep;
        }
        if (neckWeight >= 1.0f && ++aimTimer > aimTime) {
            aimTimer = 0;
            aimTime = int(fgSimulation.random->Generate()) * kMaxAimTime >> 16;
            aiming = 0;
            neckWeightStep = -kNeckWeightStep;
        }
    } else {
        neckWeightStep = -kNeckWeightStep;
    }
    MATRIX4 turn;
    BuildRotate(&turn, neckYaw, 0.0f, 1.0f, 0.0f);
    actorLink->actor->SetGlobalPoseOverride(0, reinterpret_cast<const Transform *>(&turn), neckWeight);
}

// FUNC_AT(0x00025ef0)
void AICharacterPedestrian::DoNeutral() {
    const PedestrianSpecial &kind = kSpecialPeds[specialType];
    if (subState == 0) {
        FaceTarget(3);
        subState++;
    } else if (subState == 1 && actorLink->actor->IsAnimationDone()) {
        PlayAnim(kind.idleAnimation, 1.0f);
    }
}

// FUNC_AT(0x00025f40)
void AICharacterPedestrian::DoIdling() {
    if (subState == 0) {
        subState = 1;
        return;
    }
    if (!actorLink->actor->IsAnimationDone())
        return;
    if (pedFlags & kPedSpecial) {
        PlayAnim(kSpecialPeds[specialType].idleAnimation, 1.0f);
        FaceTarget(3);
        return;
    }
    PlayAnim(GetRandomConvAnim(female != 0), 1.0f);
    FaceTarget(3);
}

// FUNC_AT(0x000270b0)
void AICharacterPedestrian::DoWalking() {
    if (actorLink->actor->IsAnimationDone()) {
        if ((pedFlags & (kPedPaired | kPedSpecial)) && ReachedTarget(true)) {
            SetState(kStateIdling);
            target = nextTarget;
            return;
        }
        PlayAnim(female ? kAnimFemaleWalk : kAnimMaleWalk, 1.0f);
    }
    MaintainHeading(1);
}

// FUNC_AT(0x00027150)
void AICharacterPedestrian::DoStartled() {
    if (subState == 0) {
        if (actorLink->actor->IsAnimationDone()) {
            SetNewTargetBasedOnThreat(kThreat1);
            subState++;
        }
        return;
    }
    if (subState == 1) {
        turnTowards = -1;
        if (ThreatenedByVehicle() != kThreatNone) {
            float turn = 0.25f;
            if (threatAngle < 180.0f)
                turn = -0.25f;
            PlayAnim(kAnimStartled, 1.0f);
            actorLink->actor->CurrentPositionRotateY(turn);
            subState++;
            return;
        }
    } else if (subState == 2) {
        if (!actorLink->actor->IsAnimationDone())
            return;
        if (pedFlags & kPedSpecial)
            SetTarget(&homeTarget);
    } else {
        return;
    }
    SetState(kStateWalking);
}

// FUNC_AT(0x00027240)
void AICharacterPedestrian::DoAvoiding() {
    if (subState == 0) {
        if (FindPointAgainstWall(kLeanRange) == 1) {
            SetState(kStateLeaning);
            return;
        }
        PlayAnim(female ? kAnimFemaleAvoid : kAnimMaleAvoid, 1.0f);
        if (Pedestrians.voiceDelay == 0)
            Speak();
        if (pedFlags & (kPedPaired | kPedSpecial | kPedFlag4)) {
            Coord4 back = ToCoord4(*reinterpret_cast<const Coord3 *>(transform.mtx[2]));
            Coord4 position = ToCoord4(Position());
            back.x = back.x * -250.0f;
            back.z = back.z * -250.0f;
            Coord4 goal;
            VU0_v3add(&position, &back, &goal);
            target = *Xyz(&goal);
            FaceTarget(3);
        }
        subState++;
    } else if (subState == 1) {
        if (actorLink->actor->IsAnimationDone()) {
            if (FindPointAgainstWall(kLeanRange) == 1) {
                SetState(kStateLeaning);
            } else if (stateTimer > 200) {
                subState++;
            } else {
                if (pedFlags & (kPedPaired | kPedSpecial))
                    PreventFalling();
                PlayAnim(female ? kAnimFemaleAvoid : kAnimMaleAvoid, 1.0f);
            }
        }
    } else if (subState == 2) {
        if (actorLink->actor->IsAnimationDone()) {
            PlayAnim(female ? kAnimFemaleAvoidEnd : kAnimMaleAvoidEnd, 1.0f);
            if (pedFlags & (kPedPaired | kPedSpecial))
                SetTarget(&homeTarget);
            subState++;
        }
    } else if (subState == 3) {
        if (actorLink->actor->IsAnimationDone())
            SetState(kStateWalking);
    }

    if (subState >= 1 && subState <= 3 && animTimer < 5) {
        if (pedFlags & (kPedPaired | kPedSpecial))
            FaceTarget(2);
        else
            MaintainHeading(2);
    }
}

// FUNC_AT(0x000274c0)
void AICharacterPedestrian::DoDodging() {
    if (subState == 0) {
        turnTowards = -1;
        if ((pedFlags & (kPedPaired | kPedSpecial)) || threatDistance < 8.0f) {
            Coord4 base = ToCoord4(threatPosition);
            Coord4 ahead = ToCoord4(threatForward);
            ahead.x = ahead.x * 1000.0f;
            ahead.z = ahead.z * 1000.0f;
            Coord4 goal;
            VU0_v3add(&base, &ahead, &goal);
            PlayAnim(kAnimFlee, 1.0f);
            target = *Xyz(&goal);
            FaceTarget(3);
            subState++;
        }
        if (Pedestrians.voiceDelay == 0)
            Speak();
        subState++;
        return;
    }
    if (subState == 1) {
        PlayAnim(nav->lane >= 0 ? kAnimDodgeRight : kAnimDodgeLeft, 1.0f);
        subState += 2;
        dodges = 0;
        return;
    }
    if (subState == 2) {
        ThreatenedByVehicle();
        PlayAnim(threatOnRight != 0 ? kAnimDodgeRight : kAnimDodgeLeft, 1.0f);
        subState++;
        dodges = 0;
        return;
    }
    if (subState == 3) {
        if (animTimer < 5)
            PreventFalling();
        if (!actorLink->actor->IsAnimationDone())
            return;
        if (FindPointAgainstWall(kLeanRange) == 1) {
            SetState(kStateLeaning);
            return;
        }
        if (dodges >= 25) {
            subState++;
            return;
        }
        if (ThreatenedByVehicle() != kThreatNone && dodges > 1 && threatDistance < 7.5f &&
            threatDistance < lastThreatDistance) {
            SetNewTargetBasedOnThreat(kThreat1);
            SetState(kStateDodging);
            subState = 2;
            return;
        }
        PlayAnim(female ? kAnimFemaleAvoid : kAnimMaleAvoid, 1.0f);
        dodges++;
        return;
    }
    if (subState == 4) {
        if (!actorLink->actor->IsAnimationDone())
            return;
        if (ThreatenedByVehicle() != kThreatNone) {
            if (FindPointAgainstWall(kLeanRange) == 1) {
                SetState(kStateLeaning);
            } else {
                SetNewTargetBasedOnThreat(kThreat1);
                SetState(kStateDodging);
                subState = 2;
            }
            return;
        }
        SetState(kStateWalking);
        if (pedFlags & (kPedPaired | kPedSpecial))
            SetTarget(&homeTarget);
        PlayAnim(female ? kAnimFemaleAvoidEnd : kAnimMaleAvoidEnd, 1.0f);
    }
}

// FUNC_AT(0x00025fc0)
void AICharacterPedestrian::DoDying() {
    const PedestrianSpecial &kind = kSpecialPeds[specialType];
    // Each sequence from its first animation (sub-state 0 the dying one, 100 the zone one), one after another
    if (subState == 0) {
        if (Pedestrians.voiceDelay == 0)
            Speak();
        PlayAnim(kind.dyingAnimations[0], 1.0f);
        subState++;
        return;
    }
    if (subState == 100) {
        if (Pedestrians.voiceDelay == 0)
            Speak();
        PlayAnim(kind.zoneAnimations[0], 1.0f);
        subState++;
        return;
    }
    if (subState < 100) {
        if (!actorLink->actor->IsAnimationDone())
            return;
        if (subState < 8 && kind.dyingAnimations[subState] != -1) {
            PlayAnim(kind.dyingAnimations[subState], 1.0f);
            subState++;
        }
        return;
    }
    if (!actorLink->actor->IsAnimationDone())
        return;
    if (subState - 100 < 8 && kind.zoneAnimations[subState - 100] != -1) {
        PlayAnim(kind.zoneAnimations[subState - 100], 1.0f);
        subState++;
        return;
    }
    if (kind.walksAway == 0) {
        KillSelf(false);
        return;
    }
    SetState(kStateWalking);
    SetTarget(&homeTarget);
}

// FUNC_AT(0x00026290)
void AICharacterPedestrian::DoTurning() {
    if (subState == 0) {
        subState = 1;
        return;
    }
    if (subState == 1) {
        if (actorLink->actor->IsAnimationDone()) {
            PlayAnim(female ? kAnimFemaleTurn : kAnimMaleTurn, 1.0f);
            subState++;
        }
        return;
    }
    if (subState == 2) {
        // Turned while the threat lasts (-2), or while the pedestrian it turned to is turning too
        bool keepTurning;
        if (turnTowards == -2)
            keepTurning = threatLevel != kThreatNone;
        else
            keepTurning = Pedestrians.peds[turnTowards]->turnTowards != -1;
        if (keepTurning) {
            if (actorLink->actor->IsAnimationDone())
                PlayAnim(female ? kAnimFemaleTurnEnd : kAnimMaleTurnEnd, 1.0f);
        } else if (actorLink->actor->IsAnimationDone()) {
            PlayAnim(female ? kAnimFemaleTurnBack : kAnimMaleTurnBack, 1.0f);
            subState++;
        }
        return;
    }
    if (subState == 3 && actorLink->actor->IsAnimationDone()) {
        SetState(kStateWalking);
        turnTowards = -1;
    }
}

// FUNC_AT(0x000263d0)
void AICharacterPedestrian::DoLeaning() {
    if (subState == 0) {
        // Facing away from the wall, ten times as far off
        Coord4 position = ToCoord4(Position());
        Coord4 away;
        VU0_v4sub(&wallPoint, &position, &away);
        away.x = away.x * -10.0f;
        away.z = away.z * -10.0f;
        VU0_v3add(&position, &away, &target);
        PlayAnim(kAnimLean, 1.0f);
        if (Pedestrians.voiceDelay == 0)
            Speak();
        leanFrame = 0;
        turnTowards = -1;
        subState++;
        return;
    }
    if (subState == 1) {
        if (actorLink->actor->IsAnimationDone()) {
            subState++;
            return;
        }
        // Stepping back to the wall over frames 3-9
        int lastFrame = leanFrame;
        leanFrame = Ftol(playAnimController->GetCurrentFrame());
        if (leanFrame != lastFrame && leanFrame >= 3 && leanFrame < 10) {
            Coord4 origin = ToCoord4(Position());
            origin.x = float(origin.x + wallOffset.x);
            origin.y = 0.0f;
            origin.z = float(origin.z + wallOffset.z);
            actorLink->actor->ChangeAnimationOrigin(Xyz(&origin));
            FaceTarget(2);
        }
        return;
    }
    if (subState == 2) {
        if (!actorLink->actor->IsAnimationDone()) {
            FaceTarget(1);
        } else if (playerDistance < 75.0f) {
            PlayAnim(kAnimFlee, 1.0f);
        } else {
            subState++;
        }
        return;
    }
    if (subState == 3) {
        if (!actorLink->actor->IsAnimationDone()) {
            FaceTarget(1);
            return;
        }
        if (pedFlags & (kPedPaired | kPedSpecial))
            SetTarget(&homeTarget);
        SetState(kStateWalking);
    }
}

// FUNC_AT(0x00023b70)
void AICharacterPedestrian::DoDead() {
    if (subState != 0)
        return;
    if (actorLink != NULL) {
        ActActorDatabase::KillActorByHandle(reinterpret_cast<PointerListNode *>(actorLink));
        actorLink = NULL;
    }
    subState++;
}

// FUNC_AT(0x00027800)
void AICharacterPedestrian::HandleInterrupts() {
    int oldState = state;
    if (state == kStateDead)
        return;
    Coord4 position = ToCoord4(Position());
    if (AIRoadSpawn::Get()->PedRespawnAvailable(Xyz(&position))) {
        KillSelf(false);
        return;
    }
    const PedestrianSpecial *special = NULL;
    if (pedFlags & kPedSpecial)
        special = &kSpecialPeds[specialType];
    // A quarter of the pedestrians each step
    if ((index ^ fgSimulation.stepCount) & 3)
        return;
    DoAiming();

    if (oldState == kStateWalking || oldState == kStateIdling || oldState == kStateRunning ||
        oldState == kStateAvoiding || oldState == kStateNeutral || oldState == kStateTurning) {
        int threat = ThreatenedByVehicle();
        if (threat == kThreatNone) {
            if (oldState == kStateAvoiding && FindPointAgainstWall(kLeanRange) == 1) {
                SetState(kStateLeaning);
                return;
            }
        } else if (threat == kThreat3) {
            if (oldState == kStateWalking) {
                turnTowards = -2;
                SetState(kStateTurning);
                return;
            }
        } else {
            if (special != NULL && special->dyingAnimations[0] != -1) {
                SetState(kStateDying);
                return;
            }
            if (FindPointAgainstWall(kLeanRange) != 1) {
                SetState(threat == kThreat2 ? kStateStartled : kStateDodging);
                return;
            }
            SetState(kStateLeaning);
        }
    }

    if (ThreatenedByVehicle() != kThreatNone)
        return;
    if (oldState == kStateLeaning || oldState == kStateStartled || oldState == kStateDodging ||
        oldState == kStateTurning || oldState == kStateDying)
        return;
    // Turning to a nearing pedestrian that is turning to another
    if (closestPed != -1 && closestPedDist < previousClosestPedDist && closestPedDist < 10.0f) {
        int other = Pedestrians.peds[closestPed]->turnTowards;
        if (other != -1 && other != index) {
            turnTowards = closestPed;
            SetState(kStateTurning);
            return;
        }
    }

    // In an AI zone: aiming at it, and avoiding (or a special pedestrian's zone sequence)
    for (int zone = 0; zone < ZoneCount(); zone++) {
        if ((Zone(zone).type == 2 || Zone(zone).type == 3) &&
            AIZoneController::Get()->InZone(zone, &Position(), 1.0f)) {
            aiming = 1;
            aimTarget = Zone(zone).position;
            aimTimer = aimTime;
            if (special == NULL) {
                SetState(kStateAvoiding);
                if (oldState == kStateAvoiding)
                    subState = 1;
                return;
            }
            if (special->zoneAnimations[0] != -1) {
                SetState(kStateDying);
                subState = 100;
                return;
            }
        }
    }
}
