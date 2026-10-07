#ifndef DRIVING_DEVTOOLS_RIGIDCOLLIDESHADOW_H_
#define DRIVING_DEVTOOLS_RIGIDCOLLIDESHADOW_H_

// NIGHTFIRE_RIGIDCOLLIDESHADOW=1: RigidBody's collision detection (physics/RigidBodyCollide.cpp) against the
// originals on copies of the live bodies, once, from the first simulation tick. See RigidCollideShadow.cpp.
void RigidCollideShadow_Run(void);

#endif // DRIVING_DEVTOOLS_RIGIDCOLLIDESHADOW_H_
