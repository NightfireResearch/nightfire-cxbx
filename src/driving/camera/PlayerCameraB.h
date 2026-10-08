#ifndef DRIVING_CAMERA_PLAYERCAMERAB_H_
#define DRIVING_CAMERA_PLAYERCAMERAB_H_

// RPlayerCamera, 0x00083190..0x00086bb0: the auto-drive camera's pitch and yaw limits, zoom, the director's mode
// changes, transitions between camera positions, shake set-up, and the spline, fixed and auto-drive cameras with
// the auto-drive arms' weapon animations. The methods are declared on the class in PlayerCamera.h; see
// PlayerCameraB.cpp.

#include <stddef.h>
#include <stdint.h>

#include "CameraIniLoader.h"
#include "PlayerCamera.h"

// ---- the free function among the methods

// Pushes both auto-drive rotations away from zero by a thousandth of the smaller's share of the larger, keeping
// each in [-1, 1] (Ghidra: FUN_000846d0; the name is ours). The arguments come in ECX and EDX.        0x000846d0
void __fastcall BoostDiagonalRotation(float *x, float *y);

#endif  // DRIVING_CAMERA_PLAYERCAMERAB_H_
