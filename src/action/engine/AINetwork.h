#ifndef AINETWORK_H_
#define AINETWORK_H_

#include "../actionhelpers.h"

typedef struct {
    _VECTOR pos;
    cel_tag *cel;
} CelPos_tag;

// FIXME: Implement properly
typedef struct {
    uint placeholder;
} AIPath_tag;

typedef struct {
    obj_tag *someObj;
    CelPos_tag celPos;
    AIPath_tag *someAiPath;
    uint dataSize;
    void* someDataPtr;
    char unknown[0x8];
} AIEmitter_tag;

static_assert(sizeof(AIEmitter_tag) == 0x28, "AIEmitter_tag is wrong size");

void AINetwork_FreeEmitter(AIEmitter_tag *param_1);

#endif // AINETWORK_H_