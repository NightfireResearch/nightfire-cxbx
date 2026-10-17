#ifndef DRIVING_CAMERA_PLAYERCAMERAC_H_
#define DRIVING_CAMERA_PLAYERCAMERAC_H_

// RPlayerCamera, 0x00086bb0..0x00089b70: collisions, shaking, the aim matrix, UpdateCamera, the heli, tumble and
// ellipse cameras, the Set*Camera functions, construction, restart, the old and last selectable modes, and the
// animation triggers. The methods are declared on the class in PlayerCamera.h; see PlayerCameraC.cpp.

#include "PlayerCamera.h"

extern int32_t HeliGroundCount;         // the heli camera's ground-contact count, 0-80
extern Coord4 HeliGroundPoint;          // where its last ground probe hit
extern MATRIX4 AimMatrix;               // GetAimMatrix4's answer

#endif  // DRIVING_CAMERA_PLAYERCAMERAC_H_
