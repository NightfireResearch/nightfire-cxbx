#pragma fp_contract(off)

#include "AIRoadSpawn.h"

#include "AIVehicle.h"                  // AIGroundVehicle, AIVEHICLE_UNTESTED
#include "MissionManager.h"             // glbMissionManager
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../camera/Camera.h"           // RViewCamera, RCamera
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/CoreFoundation.h"   // ThrowLengthError
#include "../engine/SimRandom.h"
#include "../engine/StaticInit.h"       // DestroyStatic_001de820
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../world/CollisionTypes.h"    // WGridCellList, WGridNode
#include "../world/Grid.h"              // TheGrid
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../platform/CRuntime.h"

// XBE_GLOBAL(0x001de814, 0x1)
uint8_t ForceNearRespawn;

// ---------------------------------------------------------------------------------------------------------------
// AIRoadSpawn (0x000347e0-0x00035610), and the grid cell list's length error (0x00034e50), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

// GetRandomTrafficSpawn's navigator, a function-local static (destroyed at exit by DestroyStatic_001de820)
// XBE_GLOBAL(0x001de820, 0xc0)
alignas(16) WRoadNav SpawnNav;

// XBE_GLOBAL(0x001de738, 0xc8)
int32_t SpawnSegments[kMaxSpawnSegments];
// XBE_GLOBAL(0x001de800, 4)
int32_t NextSpawnRefreshStep;
// XBE_GLOBAL(0x001de804, 4)
AIRoadSpawn *AIRoadSpawnInstance;
// XBE_GLOBAL(0x001de808, 4)
int32_t SpawnSegmentCount;
// XBE_GLOBAL(0x001de80c, 1)
uint8_t SpawnRestarted;
// XBE_GLOBAL(0x001de810, 4)
int32_t TrafficLaneTurn;
// XBE_GLOBAL(0x001de8e0, 4)
uint32_t SpawnNavMade;
// XBE_GLOBAL(0x001b6614, 4)
float SpawnNearRadius = 150.0f;
// XBE_GLOBAL(0x001b6618, 4)
float SpawnFarRadius = 250.0f;

namespace {

constexpr float kRandomScale = 1.0f / 65536.0f;
constexpr int32_t kForcedRefreshSteps = 1;
constexpr int32_t kRefreshSteps = 89;       // the next refresh is due on the 90th step
constexpr float kTrafficNearRadius = 75.0f; // the Get...Spawn functions' radii
constexpr float kTrafficFarRadius = 150.0f;
constexpr float kCellScale = 0.2f;          // world units to the rigid bodies' cells (RigidBodyInfo::zone)
constexpr int kTrafficCellGap = 4;          // cells between a new traffic car and any other traffic car
constexpr int kCarCellGap = 8;              // ... and any other car
constexpr int kPedTries = 10;
constexpr float kInViewTraffic = 0.65f;     // the camera's view, as the cosine of the angle off its axis
constexpr float kInViewFront = 0.8f;
constexpr float kInView = 0.2f;
constexpr int32_t kPedRespawnFirstStep = 5;

constexpr uint16_t kSegmentFlag40 = 0x0040;  // GetRandomPedSpawn passes such segments over (no name in RoadNetwork.h)

// Segments that never take traffic
constexpr uint16_t kNoTrafficFlags = kSegmentNonDirectional | kSegmentFlag02 | kSegmentFlag08 | kSegmentFlag80 |
                                     kSegmentFlag8000;

const Coord3 &PlayerCarPosition() {
    return fgSimulation.GetRigidBody(fgSimulation.cars.first[0]->rigidBodySlot)->position;
}

// The first player view's camera: its frame's rows 2 (its axis) and 3 (its position)
const MATRIX4 &CameraFrame() {
    return fgRenderHigh->views[0].view->camera->matrix;
}

// The direction from the camera to the position against the camera's axis
float CosineOffCameraAxis(const Coord3 *position) {
    Coord4 camera;
    *Xyz(&camera) = *reinterpret_cast<const Coord3 *>(CameraFrame().mtx[3]);
    Coord4 axis;
    *Xyz(&axis) = *reinterpret_cast<const Coord3 *>(CameraFrame().mtx[2]);
    Coord4 toPosition;
    VU0_v4sub(position, &camera, &toPosition);
    VU0_v4unitxyz(&toPosition, &toPosition);
    return v3dotprod(&toPosition, &axis);
}

AIGroundVehicle *GetAIGroundVehiclePtr(PVehicle *car) {
    return car->GetAIGroundVehiclePtr();
}

int Distance(int a, int b) {
    return a - b < 0 ? b - a : a - b;
}

} // namespace

// ---- the spawner

// FUNC_AT(0x000347e0)
AIRoadSpawn* AIRoadSpawn::Get() {
    return AIRoadSpawnInstance;
}

// FUNC_AT(0x000347f0)
void AIRoadSpawn::Init() {
    if (AIRoadSpawnInstance == NULL) {
        AIRoadSpawnInstance = static_cast<AIRoadSpawn *>(OperatorNew(sizeof(AIRoadSpawn)));
        NextSpawnRefreshStep = fgSimulation.stepCount;
        for (int i = 0; i < kMaxSpawnSegments; i++)
            SpawnSegments[i] = -1;
        SpawnRestarted = 1;
    }
}

// FUNC_AT(0x00034830)
void AIRoadSpawn::Restart() {
    if (AIRoadSpawnInstance != NULL) {
        NextSpawnRefreshStep = fgSimulation.stepCount;
        for (int i = 0; i < kMaxSpawnSegments; i++)
            SpawnSegments[i] = -1;
        SpawnRestarted = 1;
    }
}

// FUNC_AT(0x000349e0)
void AIRoadSpawn::Shutdown() {
    if (AIRoadSpawnInstance != NULL)
        OperatorDelete(AIRoadSpawnInstance);
    AIRoadSpawnInstance = NULL;
}

// ---- places on a segment

// FUNC_AT(0x00034860)
void AIRoadSpawn::GetTrafficSpawnPointOnSegment(short *segment, int8_t *lane, float *along, bool *forwards) {
    WRoadSegment *road = fgRoadNetworkData.segments[*segment];
    *along = float(fgSimulation.random->Generate()) * kRandomScale;
    short roadIndex = fgRoadNetworkData.segments[*segment]->road;
    int lanes = road->leftLanes + road->rightLanes;
    int pick;
    if (roadIndex != -1)
        pick = (fgRoadNetworkData.roads[roadIndex]->carCount + TrafficLaneTurn) % lanes;
    else
        pick = TrafficLaneTurn % lanes;
    // The right lanes count down from rightLanes to 1, then the left ones from -1
    if (pick < road->rightLanes)
        *lane = int8_t(road->rightLanes - pick);
    else
        *lane = int8_t(road->rightLanes - pick - 1);

    // unknown58 has a bit for each lane, which says (or with unknownAC, does not say) that its traffic goes the
    // other way
    uint8_t lanesReversed = road->unknown58;
    *forwards = true;
    if (fgRoadNetworkData.unknownAC) {
        if ((lanesReversed & (1 << pick)) == 0) {
            *lane = int8_t(-*lane);
            *forwards = false;
        }
    } else if (lanesReversed & (1 << pick)) {
        *lane = int8_t(-*lane);
        *forwards = false;
    }
}

// FUNC_AT(0x00034960)
void AIRoadSpawn::GetPedSpawnPointOnSegment(short *segment, int8_t *side, float *along) {
    WRoadSegment *road = fgRoadNetworkData.segments[*segment];
    *along = float(fgSimulation.random->Generate()) * kRandomScale;
    int lanes = road->leftLanes + road->rightLanes;
    if (int(fgSimulation.random->Generate()) * lanes >> 16 < road->rightLanes)
        *side = int8_t(road->rightLanes + 1);
    else
        *side = int8_t(-1 - road->leftLanes);
}

// ---- out of sight

// FUNC_AT(0x00034a00)
bool AIRoadSpawn::TrafficRespawnPosCheck(const Coord3 *position, float margin) {
    if (ForceNearRespawn)
        return true;
    Coord4 player;
    *Xyz(&player) = PlayerCarPosition();
    return !(double(VU0_v3distancexz(&player, position)) - margin <= SpawnFarRadius);
}

// FUNC_AT(0x00034a70)
bool AIRoadSpawn::TrafficRespawnAvailable(const Coord3 *position, float unused) {
    if (ForceNearRespawn)
        return true;
    if (glbMissionManager->cinematicMode != 0)
        return false;
    Coord4 player;
    *Xyz(&player) = PlayerCarPosition();
    float distance = VU0_v3distancexz(&player, position);
    if (distance <= SpawnNearRadius)
        return false;
    if (glbMissionManager->autoDrive)
        return CosineOffCameraAxis(position) < kInViewTraffic;
    return distance > SpawnFarRadius;
}

// FUNC_AT(0x00034bb0)
bool AIRoadSpawn::PedRespawnPosCheck(const Coord3 *position) {
    if (ForceNearRespawn)
        return true;
    if (glbMissionManager->autoDrive && fgSimulation.stepCount > kPedRespawnFirstStep) {
        Coord4 camera;
        *Xyz(&camera) = *reinterpret_cast<const Coord3 *>(CameraFrame().mtx[3]);
        Coord4 axis;
        *Xyz(&axis) = *reinterpret_cast<const Coord3 *>(CameraFrame().mtx[2]);
        Coord4 toPosition;
        VU0_v4sub(position, &camera, &toPosition);
        VU0_v4unitxyz(&toPosition, &toPosition);
        Coord4 player;
        *Xyz(&player) = PlayerCarPosition();
        float distance = VU0_v3distancexz(&player, position);
        float cosine = v3dotprod(&toPosition, &axis);
        if (cosine > kInViewFront && distance < SpawnNearRadius)
            return false;
        return !(cosine < kInView);
    }
    Coord4 player;
    *Xyz(&player) = PlayerCarPosition();
    return VU0_v3distancexz(&player, position) > SpawnNearRadius;
}

// FUNC_AT(0x00034d30)
bool AIRoadSpawn::PedRespawnAvailable(const Coord3 *position) {
    if (ForceNearRespawn)
        return true;
    if (glbMissionManager->cinematicMode != 0)
        return false;
    Coord4 player;
    *Xyz(&player) = PlayerCarPosition();
    if (VU0_v3distancexz(&player, position) > SpawnFarRadius)
        return true;
    if (!glbMissionManager->autoDrive)
        return false;
    return CosineOffCameraAxis(position) < kInView;
}

// ---- the segment list

// FUNC_AT(0x00034ed0)
void AIRoadSpawn::RefreshSpawnData(bool force, float nearRadius, float farRadius) {
    if (!force && NextSpawnRefreshStep > fgSimulation.stepCount)
        return;
    Coord3 centre = PlayerCarPosition();
    if (force) {
        centre = *reinterpret_cast<const Coord3 *>(CameraFrame().mtx[3]);
        NextSpawnRefreshStep = fgSimulation.stepCount + kForcedRefreshSteps;
        ForceNearRespawn = true;
    } else {
        NextSpawnRefreshStep = fgSimulation.stepCount + kRefreshSteps;
        ForceNearRespawn = false;
    }
    fgRoadNetworkData.queryStamp++;
    if (!fgRoadNetworkData.unknownAD)
        return;

    WGridCellList cells;
    cells.allocator = 0;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    SpawnNearRadius = nearRadius;
    SpawnFarRadius = farRadius;
    WGrid *grid = TheGrid;
    grid->FindNodes(&centre, farRadius, &cells);
    int count = 0;
    for (uint32_t *cell = cells.first; cell != cells.last; cell++) {
        WGridNode *node = grid->nodes[*cell];
        if (node == NULL || node->staticCount[kGridRoadSegment] == 0)
            continue;
        const uint16_t *ids = node->StaticIndices(kGridRoadSegment);
        for (int i = 0; i < node->staticCount[kGridRoadSegment]; i++) {
            int id = int16_t(ids[i]);
            if (uint32_t(id) >= uint32_t(fgRoadNetworkData.segmentCount))
                continue;
            if (count >= kMaxSpawnSegments)
                break;
            WRoadSegment *road = fgRoadNetworkData.segments[id];
            if (road->queryStamp == fgRoadNetworkData.queryStamp)
                continue;
            road->queryStamp = fgRoadNetworkData.queryStamp;
            uint16_t flags = road->flags;
            if (flags & kNoTrafficFlags)
                continue;
            if ((flags & kSegmentLaneType2) && (flags & kSegmentFlag1000))
                continue;
            if ((flags & kSegmentSpecial) && road->unknown59)
                continue;
            SpawnSegments[count++] = id;
            if (count >= kMaxSpawnSegments)
                break;
        }
    }
    for (int i = count; i < kMaxSpawnSegments; i++)
        SpawnSegments[i] = -1;
    SpawnSegmentCount = count;
    if (cells.first != NULL)
        UMemory::FastFree(cells.first, cells.Capacity() * sizeof(uint32_t));
}

// ---- the spawns

// FUNC_AT(0x00035130)
bool AIRoadSpawn::GetRandomTrafficSpawn(short *segment, int8_t *lane, float *along, bool *forwards) {
    AIRoadSpawnInstance->RefreshSpawnData(false, kTrafficNearRadius, kTrafficFarRadius);
    if (glbMissionManager->cinematicMode != 0)
        return false;
    int count = SpawnSegmentCount;
    if (count <= 0)
        return false;
    *segment = short(SpawnSegments[int(fgSimulation.random->Generate()) * count >> 16]);
    if (*segment < 0)
        return false;
    short roadIndex = fgRoadNetworkData.segments[*segment]->road;
    if (roadIndex == -1)
        return false;
    WRoad *road = fgRoadNetworkData.roads[roadIndex];
    if (road->carCount >= road->unknown2c)
        return false;
    GetTrafficSpawnPointOnSegment(segment, lane, along, forwards);

    if ((SpawnNavMade & 1) == 0) {
        SpawnNavMade |= 1;
        SpawnNav.Construct();
        Crt::atexit(DestroyStatic_001de820);
    }
    SpawnNav.mode = kNavLane;
    SpawnNav.InitAtSegment(*segment, *lane, *along, *forwards);
    Coord4 at;
    *Xyz(&at) = SpawnNav.roadPoint;
    int cellX = RoundToInt(at.x * kCellScale);
    int cellZ = RoundToInt(at.z * kCellScale);

    // No closer to another car than a few cells
    PlayerCarPosition();                // the original fetches the body here and does not use it
    for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++) {
        AIGroundVehicle *vehicle = GetAIGroundVehiclePtr(*car);
        RigidBodyInfo *info = fgSimulation.GetRigidBody((*car)->rigidBodySlot)->info;
        int zoneX = info->zone[0];
        int zoneZ = info->zone[2];
        if (!vehicle->active)
            continue;
        if (vehicle->type == kAIGroundTraffic) {
            // The original also reads the hit points and the two navigators' lanes here, and uses neither
            (*car)->GetHitPoints();
            if (Distance(cellX, zoneX) <= kTrafficCellGap && Distance(cellZ, zoneZ) <= kTrafficCellGap)
                return false;
        } else {
            if (Distance(cellX, zoneX) <= kCarCellGap && Distance(cellZ, zoneZ) <= kCarCellGap)
                return false;
        }
    }

    Coord4 player;
    *Xyz(&player) = PlayerCarPosition();
    if (!(VU0_v3distancexz(&player, &at) >= SpawnNearRadius))
        return false;
    TrafficLaneTurn++;
    return true;
}

// FUNC_AT(0x00035470)
bool AIRoadSpawn::GetRandomPedSpawn(short *segment, int8_t *side, float *along, bool allowSpecial) {
    int tries = kPedTries;
    short found = -1;
    *segment = -1;
    RefreshSpawnData(false, kTrafficNearRadius, kTrafficFarRadius);
    if (glbMissionManager->cinematicMode != 0)
        return false;
    if (SpawnSegmentCount <= 0)
        return false;
    if (SpawnSegmentCount < kPedTries)
        tries = SpawnSegmentCount;
    for (int i = 0; i < tries; i++) {
        *segment = short(SpawnSegments[int(fgSimulation.random->Generate()) * SpawnSegmentCount >> 16]);
        if (*segment < 0)
            continue;
        WRoadSegment *road = fgRoadNetworkData.segments[*segment];
        uint16_t flags = road->flags;
        if (flags & kSegmentSpecial) {
            if (!allowSpecial)
                continue;
            flags = kSegmentSpecial;
        } else if ((flags & kSegmentFlag40) || !(flags & kSegmentLeftMargin) || !(flags & kSegmentRightMargin)) {
            continue;
        }
        short roadIndex = fgRoadNetworkData.segments[*segment]->road;
        if (roadIndex == -1)
            continue;
        WRoad *roadRecord = fgRoadNetworkData.roads[roadIndex];
        uint8_t pedestrians = roadRecord->pedestrianCount;
        if (pedestrians >= roadRecord->unknown2d)
            continue;
        // An ordinary segment (or a special one with unknown59) is kept while the search goes on; an empty
        // special one ends it
        if (!(flags & kSegmentSpecial) || road->unknown59 != 0) {
            found = *segment;
            continue;
        }
        if (pedestrians == 0) {
            found = *segment;
            break;
        }
    }
    *segment = found;
    if (found == -1)
        return false;
    GetPedSpawnPointOnSegment(segment, side, along);
    return true;
}
