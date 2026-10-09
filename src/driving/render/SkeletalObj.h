#ifndef DRIVING_RENDER_SKELETALOBJ_H_
#define DRIVING_RENDER_SKELETALOBJ_H_

// ---------------------------------------------------------------------------------------------------------------
// RSkeletalObj (0xa0 bytes): a scene object with bones - one matrix per proc-anim transform its instances use
// (ProcAnimXFormTransform's list, anim/ProcAnim.h) and twelve proc-anim parameters. Its animation handle's effects
// of type 8 are springs: each swings its instance's bone about an axis, driven by the body's motion, gravity and a
// pull towards a limit angle, until it is released. Vtable 0x00191c78 (RSceneObj's 19 slots: the destructor,
// SetProcAnimState, UpdatePosition and PostLoad are its own). See SkeletalObj.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "RSceneObj.hpp"
#include "../anim/AnimEngine.h"          // Handle
#include "../data/Carp.h"                // CARP::Instance, MATRIX4
#include "../data/CoordConvert.h"        // Coord3, Coord4

struct RigidBody;

class RSkeletalObj : public RSceneObj {
public:
    static constexpr int kParams = 12;

    float params[kParams];                // +0x40 the proc-anim parameter list
    uint64_t springEffects;               // +0x70 the handle's spring effects, by index (PostLoad)
    uint32_t numBones;                    // +0x78
    MATRIX4 *bones;                       // +0x7c the proc-anim transform list, from the pools as "Bones"
    // Ghidra's names; both hold the springs' point velocity, the last step's and this one's. A w of -1 (the
    // constructor's) means not started.
    Coord4 springPrevPos;                 // +0x80
    Coord4 springPos;                     // +0x90

    RSkeletalObj* Construct(CARP::Instance *instance);                                              // 0x00090b90
    void Destruct();                                                                                // 0x00090c10
    RSkeletalObj* Delete(unsigned flags);                                                           // 0x000912d0
    // Frees the bones and makes `count` identity matrices (none for 0).
    void AllocateSkeleton(uint32_t count);                                                          // 0x00090c80
    // Steps one spring: its angle and angular velocity, from the body's local velocities at the lever's end and the
    // springs' last two point velocities. False once the spring is released (it is no longer updated).
    bool UpdateSpringMassSystem(RigidBody *body, CARP::Instance *instance, const Coord3 *point, const Coord3 *axis,
                                float torqueThreshold, float mass, float limit, float *angle,
                                float *angularVelocity);                                            // 0x00090cf0
    // Hands the proc-anim functions this object's bones and parameters.
    void SetProcAnimState();                                                                        // 0x000912b0
    // Bones for the model's skeleton, and the handle's spring effects noted.
    void PostLoad();                                                                                // 0x00091300
    // RSceneObj's, then each live spring stepped and its bone set from its angle.
    void UpdatePosition(bool force);                                                                // 0x00091360
};
static_assert(sizeof(RSkeletalObj) == 0xa0, "RSkeletalObj is 0xa0 bytes");

#endif // DRIVING_RENDER_SKELETALOBJ_H_
