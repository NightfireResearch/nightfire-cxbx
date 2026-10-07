#ifndef DRIVING_DEVTOOLS_PHYSOBJSHADOW_H_
#define DRIVING_DEVTOOLS_PHYSOBJSHADOW_H_

// NIGHTFIRE_PHYSOBJSHADOW=1: PhysicsObject, SimpleRigidBody and PhysicsNamespace (physics/PhysicsObject.cpp,
// SimpleRigidBody.cpp, PhysicsNamespace.cpp) against the originals, on the mission's bodies and copies of them. See
// PhysObjShadow.cpp. Run from the first simulation tick, once the mission's bodies exist.
void PhysObjShadow_Run(void);

#endif // DRIVING_DEVTOOLS_PHYSOBJSHADOW_H_
