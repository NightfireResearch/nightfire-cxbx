#ifndef DRIVING_DEVTOOLS_RIGIDRESOLVESHADOW_H_
#define DRIVING_DEVTOOLS_RIGIDRESOLVESHADOW_H_

// NIGHTFIRE_RIGIDRESOLVESHADOW=1: RigidBody's collision response (physics/RigidBodyResolve.cpp - ResolveCollision,
// GenerateImpulse) against the originals, on copies of the live bodies. See RigidResolveShadow.cpp. Run from the
// first simulation tick, once the mission's bodies exist.
void RigidResolveShadow_Run(void);

#endif // DRIVING_DEVTOOLS_RIGIDRESOLVESHADOW_H_
