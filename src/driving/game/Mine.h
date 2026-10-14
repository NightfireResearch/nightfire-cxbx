#ifndef DRIVING_GAME_MINE_H_
#define DRIVING_GAME_MINE_H_

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../physics/PhysicsObject.h"
#include "WeaponManager.h"              // DrivingWeapon

// ---------------------------------------------------------------------------------------------------------------
// Mine (0x88 bytes, vtable 0x0018ee28: PhysicsObject's seven slots): a "weapon" PhysicsObject of type 5 on a
// simple body (kSimpleMine), dropped by a mine weapon (its id the mine's kind). A time mine goes off when its timer
// runs out, a proximity or floating mine when a rigid body it does not belong to comes within its radius, any of
// them when hit hard enough, a floating mine also when it meets the world. See Mine.cpp.
// ---------------------------------------------------------------------------------------------------------------

struct SimpleRigidBody;
struct WTargetable;

class Mine : public PhysicsObject {
public:
    float radius;                   // +0x6c proximity mines' reach
    int32_t kind;                   // +0x70 its weapon (DrivingWeapon: the four mines)
    int32_t timer;                  // +0x74 a time mine's steps left
    int32_t armStep;                // +0x78 the step from which CheckForProximity looks (a second after it is laid)
    uint8_t detonate;               // +0x7c goes off at the next Simulate
    uint8_t unknown7d[3];
    WTargetable *target;            // +0x80 NULL from the constructor; released by the destructor
    uint8_t effectsStarted;         // +0x84 the first Simulate's effects are on
    uint8_t unknown85[3];

    // `direction` times half the speed is its first velocity; a floating mine is thrown, perturbed, at 50
    Mine* Construct(int kind, const Coord3 *direction, const Coord3 *position, float speed,
                    uint32_t ownerSig);                                                         // 0x0005e480
    void Destruct();                                                                            // 0x0005e180
    // Goes off if a rigid body it does not belong to is in the cells round its own and within its radius
    void CheckForProximity();                                                                   // 0x0005e200
    // Goes off when the timer runs out
    void CheckTimer();                                                                          // 0x0005e400

    // ---- virtual (the slot in the vtable)
    Mine* Delete(unsigned flags);                                                               // 0  0x0005e6e0
    // A hit above 5 sets it off; answers 0
    int ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                    const uint32_t *sourceSig);                                                 // 2  0x0005e3e0
    void Simulate();                                                                            // 4  0x0005e710

private:
    void Explode(SimpleRigidBody *body, const Coord3 *forward);
};
static_assert(sizeof(Mine) == 0x88, "a mine is 0x88 bytes");
static_assert(offsetof(Mine, radius) == 0x6c && offsetof(Mine, armStep) == 0x78 && offsetof(Mine, target) == 0x80 &&
              offsetof(Mine, effectsStarted) == 0x84, "Mine layout");

const uint32_t kMineVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_0018ee28));

#endif // DRIVING_GAME_MINE_H_
