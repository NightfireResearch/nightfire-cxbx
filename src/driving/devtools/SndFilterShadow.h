#ifndef DRIVING_DEVTOOLS_SNDFILTERSHADOW_H_
#define DRIVING_DEVTOOLS_SNDFILTERSHADOW_H_

// NIGHTFIRE_SNDFILTERSHADOW=1: EA's SFILTER graph (sound/snd/Filters.cpp, FiltersUnused.cpp) against the originals,
// one node at a time on recorded and synthetic signals. See SndFilterShadow.cpp.
void SndFilterShadow_Run(void);

#endif // DRIVING_DEVTOOLS_SNDFILTERSHADOW_H_
