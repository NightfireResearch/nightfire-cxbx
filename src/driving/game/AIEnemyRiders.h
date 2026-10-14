#ifndef DRIVING_GAME_AIENEMYRIDERS_H_
#define DRIVING_GAME_AIENEMYRIDERS_H_

// ---------------------------------------------------------------------------------------------------------------
// The enemies in cars (AIElementController::WakeUp makes them for a vehicle element's members, each at a seat
// its car's attributes give), on AIEnemy.h's AICharacterEnemyRider:
//   AICharacterEnemySnow (0x170, vtable 0x0018a948; seat AIC_ESNOW_POS): leans with the car's steering, aims its
//     head and arm by global pose overrides, and is thrown from the car when it dies.
//   AICharacterEnemySSnow (0x150, vtable 0x0018aa18): weapon 1 at AIC_EVGUN_POS, weapon 0 at AIC_EVDRI_POS;
//     weapon 1 aims, turned by the car AI's unknown204.
//   AICharacterEnemySunroof (0x170, vtable 0x0018aac0; AIC_ESUNR_POS): comes up (visible, the car's animation
//     system 0x13 stimulated) to fire, leans with the car's acceleration, a target for the player's aim while up.
//   AICharacterEnemyWindow (0x160, vtable 0x0018ab68; AIC_EWIND_POS): comes out (visible) to fire.
// See AIEnemyRiders.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "AIEnemy.h"

struct WTargetable;

// Whether the car's wheels average 0.12 or more of compression (the Snow and SSnow kinds' bump)
bool IsBumping(PBondCar *vehicle);                                                              // 0x0001fc80

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemySnow
// ---------------------------------------------------------------------------------------------------------------

class AICharacterEnemySnow : public AICharacterEnemyRider {
public:
    int32_t leanDirection;          // +0x144 the lean's side: 0 steering left, 1 right
    int32_t aimPitchDir;            // +0x148 xor the target's side: the way the pitch turns (0 up, 1 down)
    int32_t aimTimer;               // +0x14c steps the arm has been fully up or down
    int32_t aimTimerLimit;          // +0x150 ... before it moves again (SimRandom: up to 180)
    float targetAngle;              // +0x154 TargetIsInRange's bearing of the target, in turns
    float aimAngles[2];             // +0x158 degrees: the head's yaw (-90 to 90) and the arm's pitch (-70 to 120)
    float blendWeights[2];          // +0x160 the head's and the arm's pose weights, 0 to 1
    float blendRates[2];            // +0x168 added to the weights each step

    AICharacterEnemySnow* Construct(const MATRIX4 *transform, int weapon, PBondCar *vehicle);  // 0x0001fd40
    void Destruct();                                                                            // 0x0001fc70
    AICharacterEnemySnow* Delete(unsigned flags);                                   // 0  0x0001fdd0

    // The car's orientation and position, or the thrown matrix once thrown
    static void GetRotPos(AICharacterEnemySnow *rider, MATRIX4 *transform);                     // 0x0001fac0
    // ... and its velocity (or the thrown velocity): the actor's ride callback
    static void GetRotPos(AICharacterEnemySnow *rider, MATRIX4 *transform, Coord4 *velocity);   // 0x0001fb70

    // ---- virtual (the slot in the vtable)
    // The actor, and its neck and right shoulder's pose overrides; also Window's
    void SetActor(AICharacterActorLink *actorLink);                                 // 2  0x00021df0
    void PlayZoneInjuryAnim(int zone, float amount, int kind);                      // 6  0x00020870
    void DoNeutral();                                                               // 8  0x0001fe00
    void DoArmed();                                                                 // 16 0x0001ff90
    // The head turned towards the target and the arm's pitch, blended in and out
    void DoAiming();                                                                // 17 0x00020090
    // Thrown from the car
    void DoDying();                                                                 // 22 0x000203c0
    void DoTilting();                                                               // 23 0x0001fcd0
    void DoRunning();                                                               // 27 0x000205b0
    void DoLeaning();                                                               // 28 0x000205e0
    void HandleInterrupts();                                                        // 30 0x0001fcf0
    void UpdateRotPos();                                                            // 32 0x000206d0
    // The target (the car AI's, else the player's car) within range, and within 120 degrees ahead
    bool TargetIsInRange();                                                         // 33 0x00020710
    // The target within a degree of the weapon's facing; also Window's
    int TargetIsAcquired();                                                         // 35 0x000224f0
};
static_assert(sizeof(AICharacterEnemySnow) == 0x170, "AICharacterEnemySnow is 0x170 bytes");
static_assert(offsetof(AICharacterEnemySnow, targetAngle) == 0x154 &&
              offsetof(AICharacterEnemySnow, aimAngles) == 0x158 &&
              offsetof(AICharacterEnemySnow, blendRates) == 0x168, "AICharacterEnemySnow layout");

const uint32_t kAICharacterEnemySnowVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_AICharacterEnemySnow));

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemySSnow
// ---------------------------------------------------------------------------------------------------------------

class AICharacterEnemySSnow : public AICharacterEnemyRider {
public:
    uint8_t unknown144[0xc];

    // Ghidra: FUN_00020980
    AICharacterEnemySSnow* Construct(const MATRIX4 *transform, int weapon, PBondCar *vehicle); // 0x00020980
    void Destruct();                                                                            // 0x00020970
    AICharacterEnemySSnow* Delete(unsigned flags);                                  // 0  0x000209d0

    // ---- virtual (the slot in the vtable)
    void PlayZoneInjuryAnim(int zone, float amount, int kind);                      // 6  0x00020c70
    void DoNeutral();                                                               // 8  0x00020a00
    // Turned to the car's facing less the car AI's unknown204
    void DoAiming();                                                                // 17 0x00020a60
    void DoDying();                                                                 // 22 0x00020b40
    void HandleInterrupts();                                                        // 30 0x00020bb0
    bool TargetIsInRange();                                                         // 33 0x00020c30
};
static_assert(sizeof(AICharacterEnemySSnow) == 0x150, "AICharacterEnemySSnow is 0x150 bytes");

const uint32_t kAICharacterEnemySSnowVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_0018aa18));

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemySunroof
// ---------------------------------------------------------------------------------------------------------------

class AICharacterEnemySunroof : public AICharacterEnemyRider {
public:
    uint8_t unknown144[0xc];
    Coord3 lastVelocity;            // +0x150 the car's velocity at the last lean check
    int32_t leanDirection;          // +0x15c 1-4: forward, back, right, left by the car's acceleration; 0 none
    AICharacter *partnerA;          // +0x160 the other characters in the car (DoInitial): while either is up,
    AICharacter *partnerB;          // +0x164 ... this one stays down
    WTargetable *targetable;        // +0x168 registered while it is up
    uint8_t unknown16c[4];

    AICharacterEnemySunroof* Construct(const MATRIX4 *transform, int weapon, int female,
                                       PBondCar *vehicle);                                      // 0x000210f0
    void Destruct();                                                                            // 0x00020ec0
    AICharacterEnemySunroof* Delete(unsigned flags);                                // 0  0x00021230

    // leanDirection from the car's acceleration since the last check (only set when it was 0); true when there
    // is one and it is the same as before (Ghidra: FUN_00020f90; the name is ours)
    bool UpdateLeanDirection();                                                                 // 0x00020f90

    // ---- virtual (the slot in the vtable)
    void PlayZoneInjuryAnim(int zone, float amount, int kind);                      // 6  0x00021b70
    // Down, hidden, its partners found
    void DoInitial();                                                               // 7  0x00021260
    void DoNeutral();                                                               // 8  0x00021300
    void DoArmed();                                                                 // 16 0x000213c0
    // Turned a little towards the target's side (an entry Ghidra merged into DoArmed)
    void DoAiming();                                                                // 17 0x000215b0
    void DoUnarming();                                                              // 20 0x00021630
    void DoDying();                                                                 // 22 0x00021670
    // Ghidra: FUN_00020f40
    void DoTilting();                                                               // 23 0x00020f40
    void DoRunning();                                                               // 27 0x00021710
    void DoLeaning();                                                               // 28 0x00021740
    void HandleInterrupts();                                                        // 30 0x000217e0
    bool TargetIsInRange();                                                         // 33 0x000218f0
    int TargetIsAcquired();                                                         // 35 0x000219e0
};
static_assert(sizeof(AICharacterEnemySunroof) == 0x170, "AICharacterEnemySunroof is 0x170 bytes");
static_assert(offsetof(AICharacterEnemySunroof, lastVelocity) == 0x150 &&
              offsetof(AICharacterEnemySunroof, partnerA) == 0x160 &&
              offsetof(AICharacterEnemySunroof, targetable) == 0x168, "AICharacterEnemySunroof layout");

const uint32_t kAICharacterEnemySunroofVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_AICharacterEnemySunroof));

// ---------------------------------------------------------------------------------------------------------------
// AICharacterEnemyWindow
// ---------------------------------------------------------------------------------------------------------------

class AICharacterEnemyWindow : public AICharacterEnemyRider {
public:
    float aimYaw;                   // +0x144 degrees, -60 to 80: the shoulder's; the neck's is 55 less
    AICharacter *partnerA;          // +0x148 the other characters in the car (DoInitial)
    AICharacter *partnerB;          // +0x14c
    int32_t aimSide;                // +0x150 xor the target's side: the way the aim turns
    int32_t armTimer;               // +0x154 steps it stays out after arming (300 seconds' worth)
    uint8_t unknown158[8];

    AICharacterEnemyWindow* Construct(const MATRIX4 *transform, PBondCar *vehicle);    // 0x00021d50
    void Destruct();                                                                    // 0x00021d40
    AICharacterEnemyWindow* Delete(unsigned flags);                                 // 0  0x00021dc0

    // ---- virtual (the slot in the vtable)
    void PlayZoneInjuryAnim(int zone, float amount, int kind);                      // 6  0x00022680
    void DoInitial();                                                               // 7  0x00021e70
    void DoNeutral();                                                               // 8  0x00021f10
    // The step between states: subState is the state it goes to
    void DoIdling();                                                                // 9  0x00021f70
    void DoArming();                                                                // 15 0x00022010
    void DoArmed();                                                                 // 16 0x00022050
    void DoAiming();                                                                // 17 0x00022150
    // Aiming until the animation is done; also Sunroof's 15, 18 and 19
    void DoFiring();                                                                // 18 0x00021600
    void DoUnarming();                                                              // 20 0x00022280
    void DoDying();                                                                 // 22 0x000222c0
    void HandleInterrupts();                                                        // 30 0x00022310
    bool TargetIsInRange();                                                         // 33 0x000223a0
};
static_assert(sizeof(AICharacterEnemyWindow) == 0x160, "AICharacterEnemyWindow is 0x160 bytes");
static_assert(offsetof(AICharacterEnemyWindow, aimSide) == 0x150 &&
              offsetof(AICharacterEnemyWindow, armTimer) == 0x154, "AICharacterEnemyWindow layout");

const uint32_t kAICharacterEnemyWindowVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_AICharacterEnemyWindow));

#endif // DRIVING_GAME_AIENEMYRIDERS_H_
