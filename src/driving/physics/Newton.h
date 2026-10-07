#ifndef DRIVING_PHYSICS_NEWTON_H_
#define DRIVING_PHYSICS_NEWTON_H_

#include <stddef.h>
#include <stdint.h>

#include "PhysicsObject.h"
#include "../data/Carp.h"               // CARP::Instance
#include "../data/CoordConvert.h"       // Coord3, Coord4

// ---------------------------------------------------------------------------------------------------------------
// Newton (0x90 bytes, "Newton", physics object type 9): a render instance (or a model's instances) drawn on a
// simple body, thrown with a random spin and kick, bouncing off the ground for a lifetime, then deleted.
// SpawnFromEvent makes one from an instance or a scene object. See Newton.cpp.
// ---------------------------------------------------------------------------------------------------------------

class RSceneObj;

// SpawnFromEvent's flags
enum NewtonSpawnFlags : uint8_t {
    kNewtonSpawnTowardsView = 0x01,     // thrown at the renderer's point (+0x30) ahead of the player (the name is ours)
};

struct Newton : PhysicsObject {
    float hitPoints;            // +0x6c the hit point location: mass * 10
    int32_t stepsLeft;          // +0x70 the lifetime, in simulation steps
    uint32_t unknown74;         // +0x74 0
    uint8_t unknown78[8];
    Coord4 lastPosition;        // +0x80 the body's position after the last step (w 1)

    // Newton::Newton: the body placed at `position`, oriented along `direction`, moving at momentum / mass, with
    // `spin` its simple body's acceleration; drawn with `instanceCount` instances from `instances` (a lone one
    // copied, unflagged and moved to the origin).
    Newton* Construct(const Coord3 *direction, const Coord3 *position, const Coord3 *momentum, const Coord3 *spin,
                      CARP::Instance *instances, int instanceCount, float mass, float lifetime);  // 0x00061060
    void Destruct();                                                                            // 0x00060b60
    Newton* Delete(unsigned flags);         // the scalar deleting destructor, vtable slot 0     // 0x000612f0
    // Vtable slot 4: one step - the lifetime, a bounce off the ground under the step, the orientation integrated
    // from the spin, gravity
    void Simulate();                                                                            // 0x00060b70
    // Vtable slot 2: no damage; answers 0x20
    int ApplyDamage(const void *unknown1, const void *unknown2, float amount, float unknown4, int kind,
                    const uint32_t *sourceSig);                                                 // 0x00061050

    // A Newton from an instance (or `sceneObj`'s), moving as the body `body` was (a simple one with simpleBody;
    // none if negative); the scene object built from that instance is hidden, and the instance gets flag 0x01.
    static void SpawnFromEvent(float mass, float lifetime, CARP::Instance *instance, bool simpleBody, int body,
                               RSceneObj *sceneObj, uint8_t flags);                             // 0x00061310
};
static_assert(sizeof(Newton) == 0x90, "a Newton is 0x90 bytes");

#endif // DRIVING_PHYSICS_NEWTON_H_
