#ifndef DRIVING_GAME_GRENADE_H_
#define DRIVING_GAME_GRENADE_H_

// ---------------------------------------------------------------------------------------------------------------
// Grenade (0x6e0 bytes, "Grenade", physics object type 8, vtable 0x0018ed98): a thrown grenade on a simple body
// (kSimpleGrenade) under gravity, drawn with "grenade.crp" and a smoke trail. It bounces off the world (losing
// three quarters of its speed, with a small explosion at the first bounce) and explodes on its second bounce or when
// its three-second fuse runs out - not in its first five steps. See Grenade.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3
#include "../physics/PhysicsObject.h"
#include "WeaponEffects.h"              // RMissileStreak

class Grenade : public PhysicsObject {
public:
    uint32_t unknown6c;
    RMissileStreak streak;              // +0x70
    uint8_t unknown680[0x40];
    int32_t weaponType;                 // +0x6c0 the explosions' second argument
    float unknown6c4;                   // +0x6c4 0.5 from the constructor
    int32_t fuse;                       // +0x6c8 the steps left
    int32_t armedStep;                  // +0x6cc the simulation step from which it may explode
    int32_t bounces;                    // +0x6d0
    uint8_t explode;                    // +0x6d4 set: explodes at the end of this step
    uint8_t unknown6d5[0xb];

    // Grenade::Grenade: thrown from `position` along `direction` (a unit vector) at half `speed`, by the owner with
    // the signature
    Grenade* Construct(int weaponType, const Coord3 *direction, const Coord3 *position, float speed,
                       uint32_t ownerSig);                                                      // 0x0005d5c0
    void Destruct();                                                                            // 0x0005d4f0
    Grenade* Delete(unsigned flags);    // the scalar deleting destructor, vtable slot 0         // 0x0005d7a0
    // The trail: a new point at the body's position in the first view, then drawn
    void Render(bool firstView);                                                                // 0x0005d550
    // Vtable slot 4
    void Simulate();                                                                            // 0x0005d7d0
};
LAYOUT_32(sizeof(Grenade) == 0x6e0, "a grenade is 0x6e0 bytes");
LAYOUT_32(offsetof(Grenade, streak) == 0x70 && offsetof(Grenade, weaponType) == 0x6c0 &&
          offsetof(Grenade, fuse) == 0x6c8 && offsetof(Grenade, explode) == 0x6d4, "Grenade layout");

#endif // DRIVING_GAME_GRENADE_H_
