#ifndef DRIVING_DEVTOOLS_RIGIDCORESHADOW_H_
#define DRIVING_DEVTOOLS_RIGIDCORESHADOW_H_

// NIGHTFIRE_RIGIDCORESHADOW=1: RigidBody's core (physics/RigidBody.cpp - construction, ResetObject, the initial
// forces, the levers against the ground, ModifyLevers, ScaleObjObjForces, ControlSleep, the integration) against the
// originals, on copies of the live bodies. See RigidCoreShadow.cpp. Run from the first simulation tick, once the
// mission's bodies exist.
void RigidCoreShadow_Run(void);

#endif // DRIVING_DEVTOOLS_RIGIDCORESHADOW_H_
