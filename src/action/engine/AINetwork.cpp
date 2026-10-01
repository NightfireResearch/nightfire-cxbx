// Fragment for src/action/engine/AINetwork.cpp. The whole file after the change is below: the AUTOGEN declaration of
// AINetwork_FreeEmitter is replaced by our definition.

#include "AINetwork.h"
#include "../memory.h"

#include <string.h>

// AIRoute_tag.flags: the route has been planned and is still good (AINetwork_ValidateRoute sets it,
// AINetwork_InvalidateRoute clears it)
#define AIROUTE_VALID 0x10

// An emitter's data: allocation type malloc_bot_path3 (0x4e00), aligned to 8 (Mem_Malloc takes the alignment from
// the flags' low byte)
#define MALLOC_BOT_PATH3_ALIGN8 ((MallocFlags)0x4e08)

// Whether a route is planned and still good. Returns the flag itself (0x10), not 1: callers only test it for zero.
// AUTOINJECT
uchar AINetwork_RouteIsValid(AIRoute_tag *route) {
    if (route == NULL)
        return 0;
    return (uchar)(route->flags & AIROUTE_VALID);
}

// Gives an emitter a byte per node of its path (`size` is the path's node count, or the largest over all paths for
// the drones' shared safety emitter). Marked allocated before the allocation, and even if it fails; the data is left
// as Mem_Malloc gives it (AINetwork_EmitPath fills it).
// AUTOINJECT
void AINetwork_AllocEmitter(AIEmitter_tag *emitter, uint size) {
    emitter->allocated = 1;
    emitter->size = size;
    emitter->data = (uchar *)Mem_Malloc(size, MALLOC_BOT_PATH3_ALIGN8, 0);
}

// Frees an emitter's data and clears the whole emitter. An emitter without data (never allocated, already freed, or
// whose allocation failed) is left exactly as it is - including its allocated flag.
// AUTOINJECT
void AINetwork_FreeEmitter(AIEmitter_tag *emitter) {
    if (emitter == NULL || emitter->data == NULL)
        return;
    Mem_Free((void **)&emitter->data);
    memset(emitter, 0, sizeof(AIEmitter_tag));
}
