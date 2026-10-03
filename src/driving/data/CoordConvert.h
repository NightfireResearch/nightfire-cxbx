#ifndef DRIVING_DATA_COORDCONVERT_H_
#define DRIVING_DATA_COORDCONVERT_H_

// Float_COORD3toCOORD4 / Float_COORD4toCOORD3: a 3-float coordinate widened to 4 floats (w = 0) and narrowed back,
// as the vehicle physics, the AI and the weapon events pass vectors between their two layouts. Both return the
// struct by value, through the hidden result pointer the declarations spell out.

#include <stdint.h>

struct Coord3 {
    float x, y, z;
};

struct Coord4 {
    float x, y, z, w;
};

Coord4* Float_COORD3toCOORD4(Coord4 *result, const Coord3 *v);   // 0x0005c9f0
Coord3* Float_COORD4toCOORD3(Coord3 *result, const Coord4 *v);   // 0x0005ca40

#endif // DRIVING_DATA_COORDCONVERT_H_
