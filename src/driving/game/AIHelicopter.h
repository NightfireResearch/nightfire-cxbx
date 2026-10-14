#ifndef DRIVING_GAME_AIHELICOPTER_H_
#define DRIVING_GAME_AIHELICOPTER_H_

// ---------------------------------------------------------------------------------------------------------------
// AIHelicopter (0x140 bytes, vtable 0x0018b950): the AI flying a helicopter (PHelicopter, game/Helicopter.h). Each
// step, while it is active and commanded, Update aims it at its target vehicle (the first player's car) and runs
// DoAgentLogic, which acts by attack mode: hovering, following the target at a range, chasing it along the road,
// firing rockets and guns at it, or crashing once it has lost its hit points. ControlMoveState moves it by navigate
// mode - straight at its destination, along the road network after the target, or along a spline - and
// ManeuverToPos turns that into the helicopter's controls. AIVehicleController (AIVehicleController.h) holds the
// helicopters, one per helicopter the AI elements declare. See AIHelicopter.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "AIVehicle.h"

struct WTargetable;

// AIHelicopter::navigateMode (the names are ours)
enum AIHelicopterNavigateMode : int32_t {
    kHeliNavigateNone = 1,          // Reset's; NewState makes it kHeliNavigateDirect
    kHeliNavigateDirect = 2,        // straight at the destination
    kHeliNavigateRoad = 3,          // along the road network, after the target vehicle
    kHeliNavigateSpline = 5,        // carried along the spline path
};

// AIHelicopter::attackMode: what DoAgentLogic does in each (the names are ours)
enum AIHelicopterAttackMode : int32_t {
    kHeliAttackMove = 1,            // moves only (Reset's)
    kHeliAttackHold = 2,            // stays at the destination, still
    kHeliAttackFollow = 3,          // follows the target at the range, firing rockets
    kHeliAttackMove4 = 4,           // as kHeliAttackMove
    kHeliAttackFaceTarget = 5,      // turns to face the target
    kHeliAttackChaseRockets = 6,    // chases the target along the road, firing rockets
    kHeliAttackChaseGuns = 7,       // ... firing the guns
    kHeliAttackChase = 8,           // ... without firing
    kHeliAttackRockets = 9,         // fires rockets at the target (or the command's position)
    kHeliAttackGuns = 10,           // fires the guns at it (NewState's default)
    kHeliAttackMoveToPosition = 11, // moves to the command's position
    kHeliAttackCrash = 16,          // out of hit points: falls until it hits something
};

// GetPlayerLaneOffsets' answer (a static record; the names are ours)
struct AIHelicopterLaneOffsets {
    float left;                     // the drivable width either side of the target's road, from its point of view
    float right;
    float laneOffset;               // the target's navigator's lane offset
};

class AIHelicopter : public AIVehicle {
public:
    uint8_t unknown80[4];
    int32_t navigateMode;           // +0x84 AIHelicopterNavigateMode
    int32_t targetMode;             // +0x88 0: the target vehicle; else targetPosition
    int32_t attackMode;             // +0x8c AIHelicopterAttackMode
    float speedScale;               // +0x90 the command's
    float maxSpeed;                 // +0x94
    WTargetable *targetBeacon;      // +0x98 the player's targets can pick it while it is set
    WRoadNav *localNav;             // +0x9c ManeuverToPos's, placed at the helicopter (name ours)
    Coord3 targetPosition;          // +0xa0 the command's position in the firing modes
    AIGroundVehicle *targetVehicle; // +0xac the first player's car, from NewState
    Coord3 destination;             // +0xb0
    float destinationDistance;      // +0xbc in the ground plane
    Coord3 destinationDirection;    // +0xc0 unit (GetDirectionToTarget)
    float range;                    // +0xcc at least 5
    Coord3 flatDirection;           // +0xd0 ... the same in the ground plane: where it heads
    uint32_t unknownDC;
    uint8_t unknownE0[4];
    int16_t targetSegment;          // +0xe4 the target's road segment
    uint8_t pauseSteps;             // +0xe6 ManeuverToPos holds still while it counts them down
    uint8_t unknownE7;
    int32_t unknownE8;
    uint8_t shotsLeft;              // +0xec FireBullets counts them down; in kHeliAttackGuns it stops at 0
    uint8_t nearSide;               // +0xed the command's range was not negative
    int8_t crashStep;               // +0xee kHeliAttackCrash's steps; DoProximityDestruct's flag
    uint8_t unknownEF[0x11];
    Coord3 rocketAim;               // +0x100 the rockets' direction (EFireWeapon's gun aim: see RocketAimVector)
    uint8_t unknown10C;
    uint8_t lanesChanged;           // +0x10d DoAgentLogic changed the navigator's lane this step
    uint8_t gunSide;                // +0x10e the gun that fires next (system 7 or 8)
    uint8_t unknown10F;
    int16_t bulletDelay;            // +0x110 steps before the guns fire again
    int16_t rocketDelay;            // +0x112 ... the rockets
    uint8_t unknown114[0xc];
    Coord3 bulletTarget;            // +0x120 where the guns' last burst went
    float altitude;                 // +0x12c over the ground, at least 3
    float fireDelay;                // +0x130 seconds between shots
    uint8_t unknown134[0xc];

    // rocketAim and the word after it as one vector (EFireWeapon copies all sixteen bytes)
    const Coord4 &RocketAimVector() const { return *reinterpret_cast<const Coord4 *>(&rocketAim); }

    AIHelicopter* Construct();                                                                  // 0x00032a60
    void Destruct();                                                                            // 0x000321d0

    // A target for the player's weapons, made and registered with the target picker (the argument is not read)
    void EnableTargetBeacon(int unused);                                                        // 0x00031850
    void DisableTargetBeacon();                                                                 // 0x000318d0
    // destinationDirection and flatDirection towards the point
    void GetDirectionToTarget(const Coord3 *target);                                            // 0x00031910
    AIHelicopterLaneOffsets* GetPlayerLaneOffsets();                                            // 0x00031990
    // Fires the rockets at the target when it is within `range` and ahead; `heading` is its forward vector
    void FireRockets(float range, const Coord3 *position, const Coord3 *target, const Coord3 *heading);   // 0x00031a50
    // The controls that take it towards the target at `speed`, `altitude` over the ground (0: at the target's
    // height); `steer` lets it strafe towards the target
    void ManeuverToPos(Coord3 *target, float altitude, float speed, bool steer);                // 0x00031cd0
    // The guns, at the target, within `range`, ahead in the ground plane; over a speed of 5 only when its nose is
    // down. `heading` is flattened and made unit.
    void FireBullets(float range, float speed, const Coord3 *position, const Coord3 *target,
                     Coord3 *heading);                                                          // 0x000324d0
    void NewState(AICommand *command);                                                          // 0x00032700
    // Half the speed, between 10 and 20 (the second argument is not read); unrounded
    double GetLookAhead(float speed, float unused);                                             // 0x00032a20
    void ControlMoveState(bool steer, bool faceForward);                                        // 0x00032af0
    void DoAgentLogic();                                                                        // 0x000331c0
    // The proximity destruct: true when it went off (or is going off) this step
    bool DoProximityDestruct();                                                                 // 0x00034600

    // ---- AIVehicle's virtual methods (the slot)
    AIHelicopter* Delete(unsigned flags);       // 0, the vector deleting destructor            // 0x00035df0
    void Reset();                               // 1                                            // 0x00032280
    void Update();                              // 2                                            // 0x00034760
    void EngageSplinePath(CARP::AISpline *spline);  // 3                                        // 0x00031c80
    void DisengageSplinePath();                 // 4                                            // 0x00031ca0
};
static_assert(sizeof(AIHelicopter) == 0x140, "an AI helicopter is 0x140 bytes");
static_assert(offsetof(AIHelicopter, navigateMode) == 0x84 && offsetof(AIHelicopter, targetBeacon) == 0x98 &&
              offsetof(AIHelicopter, targetPosition) == 0xa0 && offsetof(AIHelicopter, destination) == 0xb0 &&
              offsetof(AIHelicopter, range) == 0xcc && offsetof(AIHelicopter, targetSegment) == 0xe4 &&
              offsetof(AIHelicopter, shotsLeft) == 0xec && offsetof(AIHelicopter, rocketAim) == 0x100 &&
              offsetof(AIHelicopter, bulletDelay) == 0x110 && offsetof(AIHelicopter, bulletTarget) == 0x120 &&
              offsetof(AIHelicopter, fireDelay) == 0x130, "AIHelicopter layout");

const uint32_t kAIHelicopterVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_AIHelicopter));

// GetPlayerLaneOffsets' record
extern AIHelicopterLaneOffsets PlayerLaneOffsets;

#endif // DRIVING_GAME_AIHELICOPTER_H_
