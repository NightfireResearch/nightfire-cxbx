#ifndef DRIVING_DEVTOOLS_ROADNETSHADOW_H_
#define DRIVING_DEVTOOLS_ROADNETSHADOW_H_

// NIGHTFIRE_ROADNETSHADOW=1: WRoadNetwork (world/RoadNetwork.cpp) against the originals, on the loaded track's road
// network. Call once the track is loaded (the first simulation tick); returns at once unless the variable is set.
void RoadNetShadow_Run(void);

#endif // DRIVING_DEVTOOLS_ROADNETSHADOW_H_
