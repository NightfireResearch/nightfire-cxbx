#ifndef DRIVING_DEVTOOLS_LIGHTSHADOW_H_
#define DRIVING_DEVTOOLS_LIGHTSHADOW_H_

// NIGHTFIRE_LIGHTSHADOW=1: render/Lights.cpp, render/PathEngine.cpp and the pure parts of render/DebugView.cpp
// against the originals, on the live light manager and path handles. Call once the track is loaded (the first
// simulation tick); returns at once unless the variable is set.
void LightShadow_Run(void);

#endif // DRIVING_DEVTOOLS_LIGHTSHADOW_H_
