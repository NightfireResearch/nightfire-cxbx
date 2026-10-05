#ifndef DRIVING_DEVTOOLS_COLQUERYSHADOW_H_
#define DRIVING_DEVTOOLS_COLQUERYSHADOW_H_

// NIGHTFIRE_COLQUERYSHADOW=1: WCollisionMgr's queries and the collision lists' library code (world/
// CollisionQueries.cpp) against the originals, on the loaded track. Call once the track's collision data is loaded
// (the first simulation tick); returns at once unless the variable is set.
void ColQueryShadow_Run(void);

#endif // DRIVING_DEVTOOLS_COLQUERYSHADOW_H_
