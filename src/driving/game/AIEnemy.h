#ifndef DRIVING_GAME_AIENEMY_H_
#define DRIVING_GAME_AIENEMY_H_

// ---------------------------------------------------------------------------------------------------------------
// The enemy characters (AIElementController::WakeUp makes them for the mission's AI elements), on AICharacter:
//   AICharacterEnemy: the enemies' base - three more vtable slots, 33 TargetIsInRange, 34 NeedsToReload,
//     35 TargetIsAcquired. No fields or vtable of its own.
//   AICharacterEnemyRider (the name is ours): the kinds that ride in a car (vehicle at +0x140), and the methods
//     their identical code shares (the linker folded each to one address).
//   AICharacterEnemyDriver (0x150, vtable 0x0018a788; seat AIC_BOND_POS): its hands kept on the car's wheel (IK).
//     Its vtable has only AICharacter's 33 slots.
// The others: the ground enemy (AIEnemyGround.h), and the Snow, SSnow, Sunroof and Window kinds in cars
// (AIEnemyRiders.h). Also here, the small shared functions the linker put among the enemies' code.
// See AIEnemy.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "AICharacter.h"
#include "BondCar.h"                    // PBondCar
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../anim/Controllers.h"        // AnimationController, CrossFadeAnimationController
#include "../engine/SimRandom.h"
#include "../world/CollisionTypes.h"    // WorldCollisionInfo

class AIVehicle;
struct ArticleEffect;
struct RigidBody;

// ---- shared helpers the linker placed here

// The lesser and greater of two floats (physics/PhysicsMath.h's Min and Max, out of line for the original's
// callers); the second when they are unordered. The names are ours.
float MinFloat(float a, float b);                                                               // 0x0001c600
float MaxFloat(float a, float b);                                                               // 0x0001c620


// The enemies' injury zones: 15 rows of 4 (AICharacterHitShape::zoneMap of every enemy's hit shape)
extern const char kEnemyZoneMap[60];

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemy
// ---------------------------------------------------------------------------------------------------------------

// The weapons an enemy holds (AICharacter::weapon: the constructors keep it to 1-2; 3 aims from the body)
enum EnemyWeapon : int32_t {
    kEnemyWeapon1 = 1,
    kEnemyWeapon2 = 2,
    kEnemyWeapon3 = 3,
};

// The animation banks the enemies' constructors choose (AICharacter::animBank)
enum EnemyAnimBank : int32_t {
    kBankDriver = 0,
    kBankSSnow = 1,
    kBankDeath = 2,                 // the ground enemy's deaths and its fall
    kBankGround2 = 3,               // the ground enemy with weapon 2
    kBankGround1 = 4,               // ... with weapon 1
    kBankSnow = 8,
    kBankSunroofDeath = 9,
    kBankSunroof2 = 10,
    kBankSunroof1 = 11,
    kBankWindow = 13,
    kBankWandering = 17,            // the ground enemy's wandering
};

class AICharacterEnemy : public AICharacter {
public:
    // The vtable's slots past AICharacter's own
    enum EnemySlot {
        kSlotTargetIsInRange = 33,
        kSlotNeedsToReload = 34,
        kSlotTargetIsAcquired = 35,
    };

    // ---- virtual (the slot in the vtable)
    // Within targetRange of the player's car (a full-register 0 or 1)
    int TargetIsInRangeBase();                                                      // 33 0x0001ca50
    // A weapon with a clip that is empty. No kind overrides it.
    int NeedsToReloadBase();                                                        // 34 0x0001c5e0
    // The player's car within a degree either way of the character's facing, from its weapon (or its body, with
    // weapon 3); targetSide says which side it is on
    int TargetIsAcquiredBase();                                                     // 35 0x0001ca90

    // ---- calls through the vtable, reaching the object's own override
    bool TargetIsInRange() {                                                        // 33
        return (this->*XbeVirtual<bool (AICharacterEnemy::*)()>(this, kSlotTargetIsInRange))();
    }
    bool NeedsToReload() {                                                          // 34
        return (this->*XbeVirtual<bool (AICharacterEnemy::*)()>(this, kSlotNeedsToReload))();
    }
    bool TargetIsAcquired() {                                                       // 35
        return (this->*XbeVirtual<bool (AICharacterEnemy::*)()>(this, kSlotTargetIsAcquired))();
    }

protected:
    // The animation from the character's bank, the cross-fade dropped
    void PlayAnimation(int index);
    // The animation from the bank, blended in a quarter of a second through the cross-fade (made if there is none)
    void CrossFadeTo(int bank, int index);
};
static_assert(sizeof(AICharacterEnemy) == 0x140, "the enemies' base adds no fields");

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyRider (the name is ours): an enemy in a car
// ---------------------------------------------------------------------------------------------------------------

class AICharacterEnemyRider : public AICharacterEnemy {
public:
    PBondCar *vehicle;              // +0x140

    // The car's orientation and position (the actor's ride callback: the character is the callback's id), and
    // its velocity. Also every rider's callback but Snow's.
    static void GetRotPos(AICharacterEnemyRider *rider, MATRIX4 *transform, Coord4 *velocity);  // 0x0001d530
    // ... without the velocity
    static void GetRotPos(AICharacterEnemyRider *rider, MATRIX4 *transform);                    // 0x00020e30

    // ---- virtual (the slot in the vtable)
    // Slot 3 of the Snow, SSnow, Sunroof and Window kinds (Ghidra: AICharacterEnemySSnow's)
    PBondCar* GetVehiclePtr();                                                      // 3  0x0001fd30
    // transform: the actor's local matrix through the car's; slot 32 of the Driver, SSnow, Sunroof and Window
    // kinds (Ghidra: AICharacterEnemyDriver's)
    void UpdateRotPos();                                                            // 32 0x0001d990

protected:
    RigidBody *VehicleBody();
};
static_assert(offsetof(AICharacterEnemyRider, vehicle) == 0x140, "AICharacterEnemyRider layout");

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyDriver
// ---------------------------------------------------------------------------------------------------------------

class AICharacterEnemyDriver : public AICharacterEnemyRider {
public:
    ArticleEffect *steerLeft;       // +0x144 the car's "steerleft" effect: the left hand's place on the wheel
    ArticleEffect *steerRight;      // +0x148 ... "steerright"
    uint32_t idleAnimation;         // +0x14c DoNeutral's turn through its three

    AICharacterEnemyDriver* Construct(const MATRIX4 *transform, PBondCar *vehicle);    // 0x0001d5f0
    void Destruct();                                                                    // 0x0001d640
    AICharacterEnemyDriver* Delete(unsigned flags);                                 // 0  0x0001d650

    // ---- virtual (the slot in the vtable)
    // Once the animation is done: neutral, and the arms' IK on the wheel if the car has both effects
    void DoInitial();                                                               // 7  0x0001d680
    void DoNeutral();                                                               // 8  0x0001d7f0
    // The hands kept on the wheel: the IK targets and the wrists' poses from the effects
    void HandleInterrupts();                                                        // 30 0x0001d840
};
static_assert(sizeof(AICharacterEnemyDriver) == 0x150, "a driver is 0x150 bytes");

constexpr uint32_t kAICharacterEnemyDriverVtable = 0x0018a788;

#endif // DRIVING_GAME_AIENEMY_H_
