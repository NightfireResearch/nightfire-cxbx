#ifndef DRIVING_DEVTOOLS_GEOPRIMSHADOW_H_
#define DRIVING_DEVTOOLS_GEOPRIMSHADOW_H_

// NIGHTFIRE_GEOPRIMSHADOW=1: eagl/GeoPrimState.cpp against the originals - the setters, getters and constructors on
// random objects, Apply on random states in sequences and on every state the game draws with. See
// GeoPrimShadow.cpp. Run from the first simulation tick, once the device and the render context exist.
void GeoPrimShadow_Run(void);

#endif // DRIVING_DEVTOOLS_GEOPRIMSHADOW_H_
