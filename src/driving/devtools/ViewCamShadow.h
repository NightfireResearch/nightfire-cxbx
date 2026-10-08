#ifndef DRIVING_DEVTOOLS_VIEWCAMSHADOW_H_
#define DRIVING_DEVTOOLS_VIEWCAMSHADOW_H_

// NIGHTFIRE_VIEWCAMSHADOW=1: camera/Camera.cpp and WorldCamera.cpp (RCamera, RViewCamera, RWorldCamera, the world
// view's culling, RPlayerViewCamera::ConfigureView and the helpers) against the originals, on copies of the live
// cameras. See ViewCamShadow.cpp. Run from the first simulation tick, once the player's camera and view exist.
void ViewCamShadow_Run(void);

#endif // DRIVING_DEVTOOLS_VIEWCAMSHADOW_H_
