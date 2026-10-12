#ifndef DRIVING_GAME_EXPLOSION_H_
#define DRIVING_GAME_EXPLOSION_H_

// ---------------------------------------------------------------------------------------------------------------
// Explosion (0x98 bytes, "explosion", physics object type 3; vtable 0x0018ed58): a simple body growing from a point.
// Each step the bodies its sphere reaches for the first time are pushed away from its centre and spun at random,
// and take damage, both falling off with its radius; it is deleted once too large or too weak. The Simulation keeps
// the live ones in a vector (the player camera's shake reads their size). See Explosion.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3
#include "../engine/SimRandom.h"        // BitSet192
#include "../physics/PhysicsObject.h"
#include "../world/CollisionTypes.h"    // StripTriangle

// An explosion's type: its name in the "explosion" attribute class
enum ExplosionType : int32_t {
    kExplosion = 0,
    kShellExplosion = 1,
    kMissileExplosion = 2,
    kMineExplosion = 3,
    kFireExplosion = 4,
    kFlamerExplosion = 5,
    kFlashExplosion = 6,            // pushes, no damage
    kNuclearExplosion = 7,
};

class Explosion : public PhysicsObject {
public:
    int32_t explosionType;          // +0x6c ExplosionType
    float size;                     // +0x70 the damage and push at its smallest
    PhysicsObject *directHit;       // +0x74 the object it was set off on, which takes the full size as damage
    BitSet192 bodiesHit;            // +0x78 a bit per body (rigid 0-63, simple 64-159) already hit
    uint8_t unknown90[8];

    // Explosion::Explosion: at `position`, of the size, set off on `directHit` (or none) by the owner with the
    // signature. The second argument is not read.
    Explosion* Construct(int type, int unused, const Coord3 *position, float size, PhysicsObject *directHit,
                         const uint32_t *ownerSig);                                             // 0x0005cd70
    void Destruct();                                                                            // 0x0005cd60
    // The scalar deleting destructor, vtable slot 0 (Ghidra: FUN_0005ce80)
    Explosion* Delete(unsigned flags);                                                          // 0x0005ce80
    // Vtable slot 4
    void Simulate();                                                                            // 0x0005ceb0
};
static_assert(sizeof(Explosion) == 0x98, "an explosion is 0x98 bytes");
static_assert(offsetof(Explosion, explosionType) == 0x6c && offsetof(Explosion, size) == 0x70 &&
              offsetof(Explosion, bodiesHit) == 0x78, "Explosion layout");

// 1 / Simulate's scale (Explosion.cpp), as a static initialiser made it
extern float ExplosionInverseScale;

#endif // DRIVING_GAME_EXPLOSION_H_
