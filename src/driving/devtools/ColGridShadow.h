#ifndef DRIVING_DEVTOOLS_COLGRIDSHADOW_H_
#define DRIVING_DEVTOOLS_COLGRIDSHADOW_H_

// NIGHTFIRE_COLGRIDSHADOW=1: the collision package's colliders, instances, grid and scene tree (world/Collider.cpp,
// CollisionInstance.cpp, Grid.cpp, Tree.cpp) against the originals, over the loaded track. See ColGridShadow.cpp.
// Run from the first simulation tick, once the track's collision data is loaded.
void ColGridShadow_Run(void);

#endif // DRIVING_DEVTOOLS_COLGRIDSHADOW_H_
