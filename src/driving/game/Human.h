#ifndef DRIVING_GAME_HUMAN_H_
#define DRIVING_GAME_HUMAN_H_

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../physics/PhysicsObject.h"

class AICharacter;

// ---------------------------------------------------------------------------------------------------------------
// Human (0x90 bytes, vtable 0x0018edf4: PhysicsObject's seven slots): a "human" PhysicsObject of type 4 on a simple
// body (kSimpleHuman) that an AI character drives - each step the body takes the character's matrix and stops. Its
// hit points are the character's; ApplyDamage hands the character the hit. See Human.cpp.
// ---------------------------------------------------------------------------------------------------------------

class Human : public PhysicsObject {
public:
    int32_t unknown6c;              // +0x6c the constructor's first argument; 1: ApplyDamage does nothing
    AICharacter *character;    // +0x70 deleted by the destructor
    uint8_t dead;                   // +0x74 set when a hit takes the last hit point
    uint8_t unknown75[0xb];
    Coord3 lastHit;                 // +0x80 where the last hit struck
    uint8_t unknown8c;              // +0x8c zero from the constructor
    uint8_t unknown8d[3];

    // `direction` gives the body's orientation
    Human* Construct(int unknown6c, const Coord3 *direction, const Coord3 *position, uint32_t ownerSig); // 0x0005dd90
    void Destruct();                                                                            // 0x0005dc60
    // Its body follows the character, whose hit points are its own (or, without any, the body is switched off)
    void SetAICharacter(AICharacter *character);                                           // 0x0005ded0
    // Its body's collision checks on and off (SimpleRigidBody::kFlag01)
    void Activate();                                                                            // 0x0005dcc0
    void Deactivate();                                                                          // 0x0005dce0

    // ---- virtual (the slot in the vtable)
    Human* Delete(unsigned flags);                                                              // 0  0x0005dea0
    // 0 when ignored, else 0x80
    int ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                    const uint32_t *sourceSig);                                                 // 2  0x0005dfa0
    void Simulate();                                                                            // 4  0x0005dd00

private:
    void FollowCharacter();
};
LAYOUT_32(sizeof(Human) == 0x90, "a human is 0x90 bytes");
LAYOUT_32(offsetof(Human, character) == 0x70 && offsetof(Human, dead) == 0x74 &&
          offsetof(Human, lastHit) == 0x80 && offsetof(Human, unknown8c) == 0x8c, "Human layout");

void **const kHumanVtable = Vtable_Human;

#endif // DRIVING_GAME_HUMAN_H_
