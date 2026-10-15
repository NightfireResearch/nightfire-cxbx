#ifndef DRIVING_GAME_AIENEMYGROUND_H_
#define DRIVING_GAME_AIENEMYGROUND_H_

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyGround (0x1c0 bytes, vtable 0x0018a840, 36 slots): an enemy on foot (AIElementController::WakeUp
// makes one for a character element). It arms when the player's car comes within range, turns to face it and
// fires; it dodges and runs from cars heading at it, dodges the player's aim in auto-drive, and falls (thrown) when
// the ground drops away under it. See AIEnemyGround.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "AIEnemy.h"
#include "../../common/xbeClass.h" // LAYOUT_32

class WCollider;
struct WTargetable;

// The target's bearing as the ground enemy sorts it (AICharacter::targetBearing: TargetIsAcquired's, or
// ThreatenedByVehicle's for a car; the names are ours)
enum EnemyBearing : int32_t {
    kBearingAhead = 0,
    kBearing25 = 25,
    kBearing45 = 45,
    kBearing90 = 90,
    kBearing127 = 127,              // ThreatenedByVehicle's: 135 degrees or more
};

class AICharacterEnemyGround : public AICharacterEnemy {
public:
    WTargetable *targetable;        // +0x140 its target for the player's aim, if the constructor was asked for one
    int32_t unknown144;             // +0x144 800
    int32_t unknown148;
    int32_t threatSide;             // +0x14c 1: the character is on the threatening car's negative x side
    float threatAngle;              // +0x150 the threatening car's heading from the character's facing, in turns
                                    //        (-0.5 to 0.5)
    uint8_t unknown154[0xc];
    Coord3 threatForward;           // +0x160 the threatening car's forward axis
    float unknown16c;               // +0x16c 1 to 1.5: by the threatening car's nearness and speed
    int32_t unknown170;
    int32_t targetAbove;            // +0x174 the target is more than 30 degrees up (TargetIsAcquired)
    int32_t initialBank;            // +0x178 WakeUp's
    int32_t unknown17c;             // +0x17c set: HandleInterrupts does not look for threatening cars
    int32_t claustrophobic;         // +0x180 dies where it stands (animation translation suppressed)
    int32_t dodgeSide;              // +0x184 the last dodge's side
    int32_t lastDodgeSide;          // +0x188
    int32_t avoided;                // +0x18c set by DoAvoiding when it ends outside the avoid zone; cleared by a
                                    //        dodge
    float groundHeight;             // +0x190 the ground under it at the last step; a drop of 2 or more throws it
    uint8_t unknown194[0xc];
    Coord3 previousPosition;        // +0x1a0 its position at the last step
    int32_t worker;                 // +0x1ac wanders before it arms
    WCollider *collider;            // +0x1b0 the barriers around it
    int32_t firstShot;              // +0x1b4 1 until its first shot, which plays the first of the fire animations
    uint8_t unknown1b8[8];

    // weapon: kept to 1-2. registerTarget: a target for the player's aim, registered with the picker.
    AICharacterEnemyGround* Construct(const MATRIX4 *transform, int weapon, int female, int unknown17c,
                                      bool registerTarget);                                         // 0x0001dc80
    void Destruct();                                                                                // 0x0001dea0
    AICharacterEnemyGround* Delete(unsigned flags);                                 // 0  0x0001df40

    // A car of the Simulation's heading at it, or about to hit it: the threat's fields set from the first
    bool ThreatenedByVehicle();                                                                     // 0x0001e440
    // The bearing of target from its facing (from its weapon if asked), in turns; left unrounded
    double GetAngleToTarget(const Coord3 *target, bool fromWeapon);                                 // 0x0001e9a0
    // In auto-drive, armed with weapon 1 and avoiding: whether to dodge (always after it has avoided, else when the
    // player's aim is on it, or 1% of the time); side is SimRandom's 0 or 1, high whether to dodge high
    bool AvoidThreat(uint8_t *side, uint8_t *high);                                                 // 0x0001da80

    // ---- virtual (the slot in the vtable)
    // Neither dying, dead nor thrown (a full-register 0 or 1)
    int IsAlive();                                                                  // 1  0x0001de80
    // Zone 0 takes nothing, zone 1 a thousand times the hit, the others the hit
    double GetZoneHitPointScale(int zone, float amount);                            // 5  0x0001dc50
    // A hit (kind 0 counts as 3, a shot): a death of its kind if it takes the last hit points, else a hit
    // animation
    void PlayZoneInjuryAnim(int zone, float amount, int kind);                      // 6  0x0001ea60
    void DoInitial();                                                               // 7  0x0001df70
    void DoNeutral();                                                               // 8  0x0001dfc0
    void DoWandering();                                                             // 11 0x0001ee60
    void DoAvoiding();                                                              // 12 0x0001e010
    void DoDodging();                                                               // 14 0x0001da60
    // Armed once the animation is done; also the slots 19 and 21, Snow's 15 and 18, Sunroof's 21 and Window's 21
    void DoArming();                                                                // 15 0x0001e070
    void DoArmed();                                                                 // 16 0x0001e090
    void DoAiming();                                                                // 17 0x0001f000
    void DoFiring();                                                                // 18 0x0001f0c0
    void DoDying();                                                                 // 22 0x0001e380
    void DoTilting();                                                               // 23 0x0001da70
    void DoRunning();                                                               // 27 0x0001f180
    void HandleInterrupts();                                                        // 30 0x0001f3c0
    void UpdateRotPos();                                                            // 32 0x0001e400
    // The target (the command's, else the player's car) within 12.5 degrees of its weapon's facing; sets the
    // bearing's side and bucket (targetSide, targetBearing) and targetAbove
    int TargetIsAcquired();                                                         // 35 0x0001f8f0
};
LAYOUT_32(sizeof(AICharacterEnemyGround) == 0x1c0, "a ground enemy is 0x1c0 bytes");
LAYOUT_32(offsetof(AICharacterEnemyGround, threatSide) == 0x14c &&
          offsetof(AICharacterEnemyGround, threatForward) == 0x160 &&
          offsetof(AICharacterEnemyGround, targetAbove) == 0x174 &&
          offsetof(AICharacterEnemyGround, claustrophobic) == 0x180 &&
          offsetof(AICharacterEnemyGround, groundHeight) == 0x190 &&
          offsetof(AICharacterEnemyGround, previousPosition) == 0x1a0 &&
          offsetof(AICharacterEnemyGround, collider) == 0x1b0, "AICharacterEnemyGround layout");

void **const kAICharacterEnemyGroundVtable = Vtable_AICharacterEnemyGround;

#endif // DRIVING_GAME_AIENEMYGROUND_H_
