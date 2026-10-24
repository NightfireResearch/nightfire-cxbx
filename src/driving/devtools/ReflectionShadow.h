#ifndef DRIVING_DEVTOOLS_REFLECTIONSHADOW_H_
#define DRIVING_DEVTOOLS_REFLECTIONSHADOW_H_

// NIGHTFIRE_REFLECTIONSHADOW=1: render/Reflection.cpp, Colorize.cpp, Decals.cpp, Gain.cpp and LensFlare.cpp against
// the originals - the sphere map maths, the reflection's queries and scene object slots, the colouring's modes, the
// decal ring, the lens flares' bookkeeping and projection, the feature switches. See ReflectionShadow.cpp. Run from
// the first simulation tick, once the renderer's managers exist.
void ReflectionShadow_Run(void);

#endif // DRIVING_DEVTOOLS_REFLECTIONSHADOW_H_
