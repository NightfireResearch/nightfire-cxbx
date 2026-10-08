#ifndef DRIVING_DEVTOOLS_PLAYERCAMSHADOWB_H_
#define DRIVING_DEVTOOLS_PLAYERCAMSHADOWB_H_

// NIGHTFIRE_PLAYERCAMSHADOWB=1: camera/PlayerCameraB.cpp against the originals on copies of the live player camera
// - the pitch and yaw limits, zoom, the ellipse round objects, transitions, shake set-up, the mode changes queued
// and made, the auto-drive rotations, and the spline, fixed and auto-drive cameras. Call once the player camera
// exists (the first simulation tick); returns at once unless the variable is set. =2 names each case before it
// runs.
void PlayerCamShadowB_Run(void);

#endif // DRIVING_DEVTOOLS_PLAYERCAMSHADOWB_H_
