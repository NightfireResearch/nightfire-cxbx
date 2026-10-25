#ifndef DRIVING_DEVTOOLS_WORLDSHADOW_H_
#define DRIVING_DEVTOOLS_WORLDSHADOW_H_

// NIGHTFIRE_WORLDSHADOW=1: the pure ports among the world's (world/VisCurtain.cpp, SoundMap.cpp and World.cpp's
// queries and scene object vector) against the originals. See WorldShadow.cpp. Run from the first simulation tick,
// once the track is open.
void WorldShadow_Run(void);

#endif // DRIVING_DEVTOOLS_WORLDSHADOW_H_
