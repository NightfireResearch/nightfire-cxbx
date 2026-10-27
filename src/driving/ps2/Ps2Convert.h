#ifndef DRIVING_PS2_PS2CONVERT_H_
#define DRIVING_PS2_PS2CONVERT_H_

// The conversions Ps2_ConvertFile (Ps2Files.cpp) applies to a file as it loads: each gives false for a file it does
// not convert.

#include <stdint.h>
#include <vector>

// A PS2 image container (SHPS) as the Xbox's (SHPX) (Ps2Shape.cpp)
bool Ps2_ConvertShapes(const uint8_t *file, std::vector<uint8_t> *converted);

// A controller configuration (.def) with the PS2 pad's control names as the Xbox pad's (Ps2Files.cpp)
bool Ps2_ConvertControls(const char *path, const uint8_t *file, int size, std::vector<uint8_t> *converted);

#endif // DRIVING_PS2_PS2CONVERT_H_
