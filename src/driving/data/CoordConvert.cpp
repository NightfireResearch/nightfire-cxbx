// The coordinate conversions. See CoordConvert.h. The originals copy through a local temporary with integer moves,
// so every float, NaNs included, keeps its bits.

#include "CoordConvert.h"

// FUNC_AT(0x0005c9f0)
Coord4* Float_COORD3toCOORD4(Coord4 *result, const Coord3 *v) {
    Coord4 widened = { v->x, v->y, v->z, 0.0f };
    *result = widened;
    return result;
}

// FUNC_AT(0x0005ca40)
Coord3* Float_COORD4toCOORD3(Coord3 *result, const Coord4 *v) {
    Coord3 narrowed = { v->x, v->y, v->z };
    *result = narrowed;
    return result;
}
