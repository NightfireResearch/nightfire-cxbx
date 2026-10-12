#ifndef DRIVING_GAME_AIROADSPAWN_H_
#define DRIVING_GAME_AIROADSPAWN_H_

// ---------------------------------------------------------------------------------------------------------------
// AIRoadSpawn (a one-byte singleton): where traffic cars and pedestrians come back. Every 90 steps (or when forced)
// RefreshSpawnData lists up to 50 road segments in the grid cells round the first player's car (or the camera)
// that traffic may use; the Get...Spawn functions pick one of them at random, with a lane or pavement side and a
// place along it, and the ...Available and ...PosCheck functions say whether a place is out of the player's
// sight. See AIRoadSpawn.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3
#include "../../helpers.h"

constexpr int kMaxSpawnSegments = 50;

class AIRoadSpawn {
public:
    uint8_t unused;

    static AIRoadSpawn* Get();                                                                  // 0x000347e0
    static void Init();                                                                         // 0x000347f0
    static void Restart();                                                                      // 0x00034830
    static void Shutdown();                                                                     // 0x000349e0

    // A lane (negative: the left; picked in turn by the spawns so far) and a place along the segment, and the way
    // along it a car there drives
    void GetTrafficSpawnPointOnSegment(short *segment, int8_t *lane, float *along, bool *forwards);    // 0x00034860
    // A pavement side (beyond the lanes, negative: the left) and a place along the segment, at random
    void GetPedSpawnPointOnSegment(short *segment, int8_t *side, float *along);                 // 0x00034960
    // Far enough from the first player's car (`margin` taken off the distance)
    bool TrafficRespawnPosCheck(const Coord3 *position, float margin);                          // 0x00034a00
    // Out of sight: past the near radius, and outside the auto-drive camera's view or past the far radius (the
    // second argument is not read)
    bool TrafficRespawnAvailable(const Coord3 *position, float unused);                         // 0x00034a70
    bool PedRespawnPosCheck(const Coord3 *position);                                            // 0x00034bb0
    bool PedRespawnAvailable(const Coord3 *position);                                           // 0x00034d30
    // The segment list round the player's car (the camera when forced), unless it is not yet due
    void RefreshSpawnData(bool force, float nearRadius, float farRadius);                       // 0x00034ed0
    // A place for a traffic car on a listed segment whose road has room, away from every car; false if none
    bool GetRandomTrafficSpawn(short *segment, int8_t *lane, float *along, bool *forwards);     // 0x00035130
    // A place for a pedestrian beside a listed segment with pavements (or a special one, if allowed); false if none
    bool GetRandomPedSpawn(short *segment, int8_t *side, float *along, bool allowSpecial);     // 0x00035470
};
static_assert(sizeof(AIRoadSpawn) == 1, "Init news one byte for the road spawn");


// The road spawn's statics (the names are ours)
extern AIRoadSpawn *AIRoadSpawnInstance;
extern int32_t SpawnSegments[kMaxSpawnSegments];  // RefreshSpawnData's list, -1 past its end
extern int32_t SpawnSegmentCount;
extern int32_t NextSpawnRefreshStep;
extern uint8_t SpawnRestarted;                  // set by Init and Restart; nothing reads it
extern int32_t TrafficLaneTurn;               // the traffic spawns so far: the next one's lane
extern float SpawnNearRadius;                   // RefreshSpawnData's radii
extern float SpawnFarRadius;
extern uint32_t SpawnNavMade;                   // GetRandomTrafficSpawn's static navigator is constructed

// Set: every spawn place passes the checks, and a pedestrian spawns without its crowding check (RefreshSpawnData
// when forced, EForceNearPedRespawn)
#define ForceNearRespawn BOOL8_AT(0x001de814)

#endif // DRIVING_GAME_AIROADSPAWN_H_
