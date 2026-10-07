#ifndef DRIVING_DEVTOOLS_ROADNAVSHADOW_H_
#define DRIVING_DEVTOOLS_ROADNAVSHADOW_H_

// NIGHTFIRE_ROADNAVSHADOW=1: WRoadNav (world/RoadNav.cpp) against the originals, navigators placed and driven over
// the loaded track's road network. Call once the track is loaded (the first simulation tick); returns at once
// unless the variable is set.
void RoadNavShadow_Run(void);

#endif // DRIVING_DEVTOOLS_ROADNAVSHADOW_H_
