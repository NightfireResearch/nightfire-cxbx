#ifndef DRIVING_GAME_SMACKABLE_H_
#define DRIVING_GAME_SMACKABLE_H_

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3
#include "../physics/PhysicsObject.h"

// ---------------------------------------------------------------------------------------------------------------
// Smackable (0x88 bytes, vtable 0x00190248: PhysicsObject's seven slots): a scene object of the world given a rigid
// body of kind 4 - a PhysicsObject of type 2 with its article's "smackable" PhysicsData (or the "Dummy" one), an
// ASmackable sound, and the scene object's trigger and proc-anim state. Hits push it and play its damage stimuli;
// once it has come to rest it hands its place back to its trigger and its damage to the proc-anim state, and is
// deleted. See Smackable.cpp.
// ---------------------------------------------------------------------------------------------------------------

class ASmackable;
class RSceneObj;
class WTrigger;
struct PhysicsData;
struct ProcAnimState;

class Smackable : public PhysicsObject {
public:
    uint32_t sleepDelay;            // +0x6c the constructor's: Simulate's steps at rest before GoToSleep
    int32_t moveCountdown;          // +0x70 ten seconds of steps, restarted while it moves faster than 0.1 and
                                    //       counted down (names ours)
    float hitPoints;                // +0x74 the PhysicsData's, less the proc-anim state's damage
    PhysicsData *data;              // +0x78 its article's, or "Dummy"
    WTrigger *trigger;              // +0x7c NULL: none
    ProcAnimState *procAnimState;   // +0x80 its instance's (WWorld::GetProcAnimStateFromInstance)
    ASmackable *sound;              // +0x84 the audio object, detached by the destructor

    Smackable* Construct(RSceneObj *render, WTrigger *trigger, const Coord3 *velocity,
                         uint32_t sleepDelay);                                                  // 0x00075920
    void Destruct();                                                                            // 0x00075300
    // Hands it back to its trigger (when the trigger takes its place) and proc-anim state, and deletes it
    void GoToSleep();                                                                           // 0x00075370

    // ---- virtual (the slot in the vtable)
    Smackable* Delete(unsigned flags);                                                          // 0  0x00075530
    // Answers 0x10
    int ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                    const uint32_t *sourceSig);                                                 // 2  0x00075560
    void Simulate();                                                                            // 4  0x00075420
};
static_assert(sizeof(Smackable) == 0x88, "a smackable is 0x88 bytes");
static_assert(offsetof(Smackable, sleepDelay) == 0x6c && offsetof(Smackable, hitPoints) == 0x74 &&
              offsetof(Smackable, trigger) == 0x7c && offsetof(Smackable, sound) == 0x84, "Smackable layout");

const uint32_t kSmackableVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_Smackable));

// The PhysicsData of an article without one ("Dummy", a function-local static of the constructor's), and its guard
// (bit 0: constructed)
extern PhysicsData DummyPhysicsData;
extern uint32_t DummyPhysicsDataMade;

#endif // DRIVING_GAME_SMACKABLE_H_
