#ifndef DRIVING_DEVTOOLS_COLLISTSHADOW_H_
#define DRIVING_DEVTOOLS_COLLISTSHADOW_H_

// NIGHTFIRE_COLLISTSHADOW=1: WCollisionMgr's lists, hit checks, window map and article swap
// (world/CollisionManager.cpp) against the originals on the loaded track, once, from the first simulation tick.
// See ColListShadow.cpp.
void ColListShadow_Run(void);

#endif // DRIVING_DEVTOOLS_COLLISTSHADOW_H_
