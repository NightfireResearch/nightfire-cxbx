#ifndef DRIVING_GC_GCCONVERT_H_
#define DRIVING_GC_GCCONVERT_H_

// The conversions Gc_ConvertFile (GcFiles.cpp) applies to a file as it loads: each gives false for a file it does not
// convert. No game state: the offline checker uses them too.

#include <stdint.h>
#include <vector>

// A GameCube image container (SHPG) as the Xbox's (SHPX) (GcShape.cpp)
bool Gc_ConvertShapes(const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted);

// A GameCube font (FNTG) as the Xbox's (FNTX): its header and tables turned round, its image converted (GcShape.cpp)
bool Gc_ConvertFont(const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted);

// A data group (.crp, .gal) turned round record by record (GcUData.h), a gallery's image containers converted and
// each moved to the end (GcShape.cpp)
bool Gc_ConvertDataGroup(const char *path, const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted);

// A controller configuration (.def) with the GameCube pad's control names as the Xbox pad's (GcFiles.cpp)
bool Gc_ConvertControls(const char *path, const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted);

#endif // DRIVING_GC_GCCONVERT_H_
