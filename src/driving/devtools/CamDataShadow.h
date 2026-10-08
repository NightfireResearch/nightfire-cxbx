#ifndef DRIVING_DEVTOOLS_CAMDATASHADOW_H_
#define DRIVING_DEVTOOLS_CAMDATASHADOW_H_

// NIGHTFIRE_CAMDATASHADOW=1: the cameras' data and helpers (camera/CameraIniLoader.cpp, CameraSpline.cpp,
// DirectorQueue.cpp, PlayerCamState.cpp) against the originals. Call once the player camera exists (the first
// simulation tick); returns at once unless the variable is set.
void CamDataShadow_Run(void);

#endif // DRIVING_DEVTOOLS_CAMDATASHADOW_H_
