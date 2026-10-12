#include "AIVehicle.h"

#include "AISplinePath.h"
#include "../data/CoordConvert.h"       // Float_COORD3toCOORD4
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "Simulation.h"                 // fgSimulation
#include "../platform/RealMath.h"
#include "../world/RoadNav.h"

// ---------------------------------------------------------------------------------------------------------------
// AIVehicle (0x000357b0-0x00035c00), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals


namespace {

const MATRIX4 kIdentity = { {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
} };

} // namespace

// FUNC_AT(0x000357b0)
AIVehicle* AIVehicle::Construct() {
    vtable = reinterpret_cast<void **>(kAIVehicleVtable);
    vehicleType = kAINoVehicle;
    active = false;
    physicsObject = NULL;
    splinePath = NULL;
    rotPos = kIdentity;
    WRoadNav *nav = static_cast<WRoadNav *>(UMemory::FastAlloc(sizeof(WRoadNav), "WRoadNav"));
    driveToNav = nav != NULL ? nav->Construct() : NULL;
    return this;
}

// FUNC_AT(0x00035840)
PhysicsObject* AIVehicle::GetPhysicsObject() {
    return physicsObject;
}

// FUNC_AT(0x00035850)
void AIVehicle::SetPhysicsObject(PhysicsObject *object) {
    physicsObject = object;
}

// FUNC_AT(0x00035860)
void AIVehicle::SetSplinePath(CARP::AISpline *spline) {
    if (spline == NULL) {
        if (splinePath != NULL) {
            splinePath->Destruct();
            OperatorDelete(splinePath);
        }
        splinePath = NULL;
    } else if (splinePath == NULL) {
        AISplinePath *made = static_cast<AISplinePath *>(OperatorNew(sizeof(AISplinePath)));
        splinePath = made != NULL ? made->Construct(spline) : NULL;
    } else {
        splinePath->Reset(spline);
    }
}

// FUNC_AT(0x00035930)
void AIVehicle::SetNextSplinePath(CARP::AISpline *spline) {
    if (splinePath == NULL) {
        AISplinePath *made = static_cast<AISplinePath *>(OperatorNew(sizeof(AISplinePath)));
        splinePath = made != NULL ? made->Construct(spline) : NULL;
    } else {
        splinePath->SetNext(spline);
    }
}

// FUNC_AT(0x000359d0)
AISplinePath* AIVehicle::GetSplinePath() {
    return splinePath;
}

// FUNC_AT(0x000359e0)
Coord4* AIVehicle::GetPosition() {
    Coord4 *position = reinterpret_cast<Coord4 *>(rotPos.mtx[3]);
    if (physicsObject != NULL) {
        Coord3 at = *physicsObject->GetPosition();
        position->x = at.x;
        position->y = at.y;
        position->z = at.z;
    }
    return position;
}

// FUNC_AT(0x00035a20)
MATRIX4* AIVehicle::GetRotPos() {
    if (physicsObject != NULL) {
        Coord4 position;
        if ((physicsObject->flags & PhysicsObject::kSimpleBody) == 0) {
            Coord3 at = fgSimulation.GetRigidBody(physicsObject->rigidBodySlot)->position;
            Float_COORD3toCOORD4(&position, &at);
            Coord4 orientation = fgSimulation.GetRigidBody(physicsObject->rigidBodySlot)->orientation;
            VU0_quattom4(&rotPos, &orientation);
        } else {
            SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(physicsObject->rigidBodySlot);
            Float_COORD3toCOORD4(&position, &body->position);
            fgSimulation.GetSimpleRigidBody(physicsObject->rigidBodySlot)->RecalcOrientMat(&rotPos);
        }
        MATRIX4_multxlate(&rotPos, &position, &rotPos);
    }
    return &rotPos;
}

// FUNC_AT(0x00035b40)
void AIVehicle::Destruct() {
    vtable = reinterpret_cast<void **>(kAIVehicleVtable);
    if (splinePath != NULL) {
        splinePath->Destruct();
        OperatorDelete(splinePath);
    }
    vehicleType = kAINoVehicle;
    active = false;
    if (driveToNav != NULL) {
        driveToNav->Destruct();
        UMemory::FastFree(driveToNav, sizeof(WRoadNav));
    }
}

// FUNC_AT(0x00035b90)
AIVehicle* AIVehicle::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(AIVehicle));
    return this;
}
