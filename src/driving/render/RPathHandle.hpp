#ifndef DRIVING_RENDER_RPATHHANDLE_HPP_
#define DRIVING_RENDER_RPATHHANDLE_HPP_

#include <stddef.h>
#include <stdint.h>

#include "../data/Carp.h"

// A path of the path engine (RPathEngine, not ported; its list node's value): only the fields the world and the
// trigger manager read.
struct RPathHandle {
    CARP::Instance *instance;   // +0x00
    uint8_t unknown04[0x9c];
    void *unknownA0;            // NULL: the path's instance is tested
};
static_assert(offsetof(RPathHandle, unknownA0) == 0xa0, "path handle layout");

#endif // DRIVING_RENDER_RPATHHANDLE_HPP_
