#ifndef DRIVING_CAMERA_PLAYERCAMERAA_H_
#define DRIVING_CAMERA_PLAYERCAMERAA_H_

// RPlayerCamera, 0x00080a60..0x00083190: the small methods, the bumper, dashboard and animation cameras,
// UpdateADTargetAngles, and the free helpers between them. The methods are declared on the class in PlayerCamera.h;
// see PlayerCameraA.cpp.

#include "PlayerCamera.h"

// Component `axis` (0 x, 1 y, 2 z) of the cross product (a - origin) x (b - origin), unrounded
double PointDir(int axis, const Coord4 *a, const Coord4 *b, const Coord4 *origin);                 // 0x00080f50

// Whether the point lies in the triangle a, b, c, judged in the plane of the two axes other than the largest
// component of the triangle's normal - the components compared signed, not by size
bool PointInTriangle(const Coord4 *point, const Coord4 *a, const Coord4 *b, const Coord4 *c);       // 0x00080fe0

// FUN_00081b00, FUN_00081b60 (the names are ours): the arc cosine and arc sine, in turns, of the value clamped to
// -1..1, through the C runtime's own; unrounded
double CameraAcosTurns(float value);                                                                // 0x00081b00
double CameraAsinTurns(float value);                                                                // 0x00081b60

// FUN_00081bc0 (the name is ours): the value held between low and high (high wins when low is above it)
float CameraClamp(float high, float value, float low);                                              // 0x00081bc0

#endif // DRIVING_CAMERA_PLAYERCAMERAA_H_
