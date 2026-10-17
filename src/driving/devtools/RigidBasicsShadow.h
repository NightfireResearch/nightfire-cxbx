#ifndef DRIVING_DEVTOOLS_RIGIDBASICSSHADOW_H_
#define DRIVING_DEVTOOLS_RIGIDBASICSSHADOW_H_

// NIGHTFIRE_RIGIDBASICSSHADOW=1: RigidBody's small methods (physics/RigidBodyBasics.cpp) and Newton's step and
// damage (physics/Newton.cpp) against the originals, on copies of the loaded mission's bodies. See
// RigidBasicsShadow.cpp. Run from the first simulation tick, once the bodies exist.
void RigidBasicsShadow_Run(void);

#endif // DRIVING_DEVTOOLS_RIGIDBASICSSHADOW_H_
