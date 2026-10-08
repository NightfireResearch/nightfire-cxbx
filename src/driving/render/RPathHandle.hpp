#ifndef DRIVING_RENDER_RPATHHANDLE_HPP_
#define DRIVING_RENDER_RPATHHANDLE_HPP_

#include <stddef.h>
#include <stdint.h>

#include "../data/Carp.h"
#include "../data/CoordConvert.h"   // Coord3

// A path of the path engine (RPathEngine, not ported; its list node's value): only the fields the world, the
// trigger manager and the cameras read.
struct RPathHandle {
    CARP::Instance *instance;   // +0x00
    uint8_t unknown04[0x84];
    Coord3 velocity;            // +0x88
    uint8_t unknown94[0xc];
    void *unknownA0;            // NULL: the path's instance is tested
};
static_assert(offsetof(RPathHandle, velocity) == 0x88 && offsetof(RPathHandle, unknownA0) == 0xa0,
              "path handle layout");

#endif // DRIVING_RENDER_RPATHHANDLE_HPP_
