#ifndef DRIVING_GAME_AIPEDESTRIAN_H_
#define DRIVING_GAME_AIPEDESTRIAN_H_

// ---------------------------------------------------------------------------------------------------------------
// AICharacterPedestrian (0x290 bytes, vtable 0x0018b470, 34 slots): the people on the pavements. Init makes up to
// sixteen for a track with pedestrians (the level's set of descriptions, PedSets), each on a Human and a road
// navigator; Manage runs them every step - the player's distance, the nearest other pedestrian, and recycling a
// dead one to a new spawn point (a road spawn, or one of the track's special roads). Their states walk, idle and
// talk in pairs, and react to cars: startled, dodging, avoiding, leaning on a wall, dying. See AIPedestrian.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "AICharacter.h"
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../../helpers.h"
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../world/CollisionTypes.h"    // BarrierList

class AnimationController;
class Human;
class WCollider;
class WRoadNav;
class WWorldPos;
struct PhysicsObject;

// AICharacterPedestrian::pedFlags (names ours)
enum PedestrianFlags : uint8_t {
    kPedPaired = 0x1,               // talking with `buddy`
    kPedSpecial = 0x2,              // a special pedestrian (specialType), spawned on a special road
    kPedFlag4 = 0x4,
};

// What CheckVehicleThreat makes of a car (ThreatenedByVehicle's answer; neutral names)
enum PedestrianThreat : int32_t {
    kThreatNone = 0,
    kThreat1 = 1,                   // within the car's stopping reach and coming, or half of it and not
    kThreat2 = 2,                   // coming, within 4.5
    kThreat3 = 3,                   // slow, close and dead ahead of the pedestrian's facing
};

// A special pedestrian kind (0x4c bytes; SpecialPeds): the animations it plays, and its description
struct PedestrianSpecial {
    int32_t idleAnimation;          // +0x00 DoNeutral's and DoIdling's
    int32_t dyingAnimations[8];     // +0x04 DoDying's sequence, ended by -1; -1 first: it does not die when hit
    int32_t zoneAnimations[8];      // +0x24 the sequence in an AI zone, ended by -1; -1 first: it ignores zones
    int32_t walksAway;              // +0x44 zero: dead once its sequence ends, else it walks to homeTarget
    const char *description;        // +0x48 ActCharacter::LoadCharacter's
};
LAYOUT_32(sizeof(PedestrianSpecial) == 0x4c, "a special pedestrian kind is 0x4c bytes");

// A level's pedestrians (0x70 bytes; PedSets, by GetPedBase)
struct PedestrianSet {
    int32_t count;                  // +0x00 Init sets it from the world's NUM_PED_TYPES_XBOX, if it has one
    int32_t descriptions[20];       // +0x04 PedDescriptions
    int32_t specialCount;           // +0x54
    int32_t specials[6];            // +0x58 SpecialPeds
};
static_assert(sizeof(PedestrianSet) == 0x70, "a pedestrian set is 0x70 bytes");

// A special road (0x18 bytes): one of the road network's special segments, where a special pedestrian stands
struct PedestrianSpecialRoad {
    int32_t ped;                    // +0x00 the pedestrian there, -1 none
    int32_t segment;                // +0x04
    float x, z;                     // +0x08 the segment's end node
    float aheadX, aheadZ;           // +0x10 ... ten along the segment's direction from it
};
static_assert(sizeof(PedestrianSpecialRoad) == 0x18, "a special road is 24 bytes");

class AICharacterPedestrian;

constexpr int kMaxPedestrians = 16;
constexpr int kMaxSpecialRoads = 32;

// The pedestrians' statics (0x001ddb40-0x001ddedc; names ours)
struct PedestrianStatics {
    PedestrianSpecialRoad specialRoads[kMaxSpecialRoads];   // +0x000
    int32_t specialRoadCount;       // +0x300
    uint8_t voiceDelay;             // +0x304 steps until a pedestrian may speak again
    uint8_t unknown305[3];
    int16_t spawnSegment;           // +0x308 the last spawn's segment
    uint8_t unknown30a[6];
    Coord3 spawnTarget;             // +0x310 the last spawn's target
    uint32_t unknown31c;
    Coord3 spawnPosition;           // +0x320 the last spawn's position
    int32_t pendingSpecialRoad;     // +0x32c the special road the next recycle fills, -1 none
    int32_t unknown330;             // +0x330 zeroed by Init
    uint8_t unknown334[0xc];
    Coord3 playerPosition;          // +0x340 Manage's
    int32_t pedBase;                // +0x34c GetPedBase's
    int32_t nextDescription;        // +0x350 GetRandomPedTexture's turn
    uint8_t spawnPhaseDone;         // +0x354 Init has spawned them all
    uint8_t made;                   // +0x355 Init made them; Manage and Destroy do nothing until then
    uint8_t unknown356[2];
    AICharacterPedestrian *peds[kMaxPedestrians];   // +0x358
    int32_t count;                  // +0x398 16, or 0 for a level without pedestrians
};
LAYOUT_32(sizeof(PedestrianStatics) == 0x39c, "the pedestrians' statics are 0x39c bytes");
LAYOUT_32(offsetof(PedestrianStatics, specialRoadCount) == 0x300 &&
          offsetof(PedestrianStatics, pendingSpecialRoad) == 0x32c &&
          offsetof(PedestrianStatics, playerPosition) == 0x340 && offsetof(PedestrianStatics, peds) == 0x358 &&
          offsetof(PedestrianStatics, count) == 0x398, "PedestrianStatics layout");

alignas(16) extern PedestrianStatics Pedestrians;
constexpr int kPedestrianSets = 7;
extern PedestrianSet PedSets[kPedestrianSets];         // by GetPedBase
extern int32_t InvalidatedPed;                          // the pedestrian about to be recycled, -1 none

class AICharacterPedestrian : public AICharacter {
public:
    int32_t index;                  // +0x140 in Pedestrians.peds
    WWorldPos *worldPos;            // +0x144 the ground PreventFalling looks at
    WCollider *collider;            // +0x148 FindPointAgainstWall's, made on first use
    int32_t previousState;          // +0x14c
    uint8_t pedFlags;               // +0x150 PedestrianFlags
    uint8_t dodges;                 // +0x151 DoDodging's count
    uint8_t unknown152[2];
    int32_t buddy;                  // +0x154 the pedestrian it talks with, -1 none
    int32_t specialRoad;            // +0x158 the special road it stands on, -1 none
    int32_t specialType;            // +0x15c SpecialPeds; 1-31 play from bank 7
    int32_t closestPed;             // +0x160 UpdateClosestPed's, -1 none
    float closestPedDist;           // +0x164
    int32_t previousClosestPed;     // +0x168
    float previousClosestPedDist;   // +0x16c
    Coord3 homeTarget;              // +0x170
    uint32_t unknown17c;
    Coord3 nextTarget;              // +0x180
    uint32_t unknown18c;
    Coord3 target;                  // +0x190 where it is going
    int32_t stateTimer;             // +0x19c steps in the state
    int32_t animTimer;              // +0x1a0 steps since the animation was set
    float heading;                  // +0x1a4 turns
    float headingDegrees;           // +0x1a8
    float playerDistance;           // +0x1ac across the ground; 9999.9 when dead
    float previousPlayerDistance;   // +0x1b0
    uint8_t active;                 // +0x1b4 spawned
    uint8_t unknown1b5[3];
    int32_t road;                   // +0x1b8 the road it is counted on (WRoad::pedestrianCount), -1 none
    Human *body;                    // +0x1bc
    WRoadNav *nav;                  // +0x1c0 kNavSidewalk
    AnimationController *playAnimController;    // +0x1c4 PlayAnim's
    float animSpeed;                // +0x1c8 1 + 0.0075 x its index
    float animSpeedScale;           // +0x1cc UpdateAnimSpeed's
    Coord3 wallPoint;               // +0x1d0 FindPointAgainstWall's: the nearest point on the wall
    uint32_t unknown1dc;
    Coord3 wallOffset;              // +0x1e0 ... and the way from it to the pedestrian, scaled
    int32_t leanFrame;              // +0x1ec DoLeaning's last animation frame
    PhysicsObject *previousThreat;  // +0x1f0
    PhysicsObject *threat;          // +0x1f4 the car ThreatenedByVehicle last looked at, NULL none
    int32_t nextThreatCheck;        // +0x1f8 the simulation step of the next look
    int32_t threatLevel;            // +0x1fc PedestrianThreat
    int32_t threatOnRight;          // +0x200 the threat's forward speed is positive (1) or not
    float threatDirection;          // +0x204 the threat's bearing, in eighths (GetDegFromAng's)
    float threatAngle;              // +0x208 the threat's bearing, degrees 0-360
    float threatDistance;           // +0x20c 999 when none
    Coord3 threatPosition;          // +0x210
    uint32_t unknown21c;
    Coord3 threatForward;           // +0x220 the threat's forward axis (reversed when backing up)
    uint32_t unknown22c;
    Coord3 threatRight;             // +0x230 the threat's right axis
    float lastThreatDistance;       // +0x23c threatDistance at the last look
    int32_t wallResult;             // +0x240 FindPointAgainstWall's last answer
    int32_t nextWallCheck;          // +0x244 the step its next search may run
    float lastWallRange;            // +0x248 the range it last searched
    int32_t turnTowards;            // +0x24c the pedestrian to face, -1 none, -2 the threat
    uint8_t aiming;                 // +0x250 aimTarget holds
    uint8_t unknown251[0xf];
    Coord3 aimTarget;               // +0x260 an AI zone's position
    int32_t aimTimer;               // +0x26c
    int32_t aimTime;                // +0x270 SimRandom: up to 180 steps
    float aimTurns;                 // +0x274 the target's bearing from the heading, -0.5 to 0.5
    float neckYaw;                  // +0x278 degrees, -80 to 80
    float neckWeight;               // +0x27c 0 to 1
    float neckWeightStep;           // +0x280 0.1 or -0.1
    uint8_t unknown284[0xc];

    // The constructor (Init inlines it)
    AICharacterPedestrian* Construct();
    void Destruct();                                                                            // 0x00023f50
    AICharacterPedestrian* Delete(unsigned flags);                                              // 0  0x00024700

    // ---- the pedestrians
    // Makes them (the argument is not read)
    static void Init(int unused);                                                               // 0x00026800
    static void Destroy();                                                                      // 0x00023fd0
    static void Manage();                                                                       // 0x00026ed0
    // The level's pedestrian set: by the tuning level's name, 0 for any other
    static int GetPedBase();                                                                    // 0x000239d0
    static void LoadPedTextures();                                                              // 0x00024060
    // The set's next description in turn
    static const char* GetRandomPedTexture();                                                   // 0x000240e0
    // A talking animation, by SimRandom
    static int GetRandomConvAnim(bool female);                                                  // 0x00023a90
    // A living pedestrian (not special or paired, unless includePaired) within range across the ground
    static bool PedInRange(const Coord3 *point, float range, bool includePaired);              // 0x00023ad0
    // turns in degrees; with buckets, the nearest of the eight compass points (-135 to 180), as a double
    static double GetDegFromAng(float turns, bool buckets);                                     // 0x00023bc0
    static void ForceRecycle(int index);                                                        // 0x00024730
    // Kills the pedestrian furthest from the player, other than `except`, that may be recycled; its index
    static int InvalidateFurthestPed(int except);                                               // 0x00025430

    // ---- one pedestrian
    // Dead (and its talking partner too), and off its road; not the one about to be recycled, nor a special one
    // unless forced
    void KillSelf(bool force);                                                                  // 0x00024140
    // A new place, description and state; false when there is nowhere to spawn
    bool Recycle();                                                                             // 0x000254c0
    void SetTarget(const Coord3 *point);                                                        // 0x00023ba0
    // The nearest living pedestrian on its road (ahead of it, if facingOnly); -1 none
    int UpdateClosestPed(bool facingOnly);                                                      // 0x00023ce0
    void UpdateThreatRelativeDirection(const Coord3 *point, float *direction, float *angle);   // 0x00023e60
    // The animation from its bank (bank 7 for a special one), at speed x animSpeed; answers its frames
    int PlayAnim(int animation, float speed);                                                   // 0x000253b0
    // The time scale for the threat's distance (slower when a car is close); answers the scale, unrounded
    double UpdateAnimSpeed();                                                                   // 0x00024db0
    // Turns towards target; true once facing it (mode 3 turns at once)
    bool FaceTarget(int mode);                                                                  // 0x00024c30
    // Within one of target across the ground (moving the animation origin there if asked)
    bool ReachedTarget(bool moveOrigin);                                                        // 0x00024e50
    // The car nearest across the ground within range, NULL none
    PhysicsObject* FindClosestCar(float range);                                                 // 0x00024ee0
    int CheckVehicleThreat(PhysicsObject *car, bool checkClass);                                // 0x00024280
    int ThreatenedByVehicle();                                                                  // 0x00026760
    // 1: a wall within range to lean on (wallPoint, wallOffset); searched at most every half second
    int FindPointAgainstWall(float range);                                                      // 0x00024f80
    void PreventFalling();                                                                      // 0x00025d10
    void SetNewTargetBasedOnThreat(int threat);                                                 // 0x00025e00
    void MaintainHeading(int mode);                                                             // 0x00026620

    // ---- virtual (the slot in the vtable)
    void SetActor(AICharacterActorLink *actorLink);                                             // 2  0x000248b0
    void DoNeutral();                                                                           // 8  0x00025ef0
    void DoIdling();                                                                            // 9  0x00025f40
    void DoWalking();                                                                           // 10 0x000270b0
    void DoAvoiding();                                                                          // 12 0x00027240
    void DoStartled();                                                                          // 13 0x00027150
    void DoDodging();                                                                           // 14 0x000274c0
    void DoAiming();                                                                            // 17 0x00024910
    void DoDying();                                                                             // 22 0x00025fc0
    void DoTurning();                                                                           // 24 0x00026290
    void DoLeaning();                                                                           // 28 0x000263d0
    void DoDead();                                                                              // 29 0x00023b70
    void HandleInterrupts();                                                                    // 30 0x00027800
    void UpdateRotPos();                                                                        // 32 0x00024870
    bool TargetIsInRange();                                                                     // 33 0x00024b30

private:
    void SetState(int newState);
    void Speak();
};
LAYOUT_32(sizeof(AICharacterPedestrian) == 0x290, "a pedestrian is 0x290 bytes");
LAYOUT_32(offsetof(AICharacterPedestrian, index) == 0x140 && offsetof(AICharacterPedestrian, pedFlags) == 0x150 &&
          offsetof(AICharacterPedestrian, homeTarget) == 0x170 && offsetof(AICharacterPedestrian, target) == 0x190 &&
          offsetof(AICharacterPedestrian, active) == 0x1b4 && offsetof(AICharacterPedestrian, nav) == 0x1c0 &&
          offsetof(AICharacterPedestrian, wallPoint) == 0x1d0 && offsetof(AICharacterPedestrian, threat) == 0x1f4 &&
          offsetof(AICharacterPedestrian, threatPosition) == 0x210 &&
          offsetof(AICharacterPedestrian, threatRight) == 0x230 &&
          offsetof(AICharacterPedestrian, turnTowards) == 0x24c && offsetof(AICharacterPedestrian, aimTarget) == 0x260 &&
          offsetof(AICharacterPedestrian, neckWeightStep) == 0x280, "AICharacterPedestrian layout");

void **const kAICharacterPedestrianVtable = Vtable_0018b470;

#endif // DRIVING_GAME_AIPEDESTRIAN_H_
